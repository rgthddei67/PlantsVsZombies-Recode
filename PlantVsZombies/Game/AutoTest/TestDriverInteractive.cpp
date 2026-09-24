#include "TestDriver.h"
#include "../../GameApp.h"
#include "../../DeltaTime.h"
#include "../SceneManager.h"
#include "../GameScene.h"
#include "../CardSlotManager.h"
#include "../Sun.h"
#include "../Shovel.h"
#include <filesystem>
#include <stdexcept>

namespace {
	constexpr int kInboxPollMilliseconds = 100; // 空闲信箱每 100 毫秒检查一次，避免逐帧磁盘访问
	constexpr int kMaxRequestCommands = 64; // 每批操作上限，防止交互请求长时间占用主线程
	constexpr int kMaxAdvanceSteps = 3600; // 一次最多推进 60 秒的固定步；游戏倍速仍按当前选择生效
	constexpr std::uintmax_t kMaxRequestBytes = 65536; // 单个指令文件最大 64 KiB

	/** 仅复制要求的观测字段；完整诊断数据仍由 BuildStateJson 维护。 */
	nlohmann::json Pick(const nlohmann::json& source, std::initializer_list<const char*> keys) {
		nlohmann::json result = nlohmann::json::object();
		for (const char* key : keys) if (source.contains(key)) result[key] = source[key];
		return result;
	}
}

void TestDriver::OnSceneUpdated() {
	if (mInteractiveReady && mAdvanceSteps > 0) {
		--mAdvanceSteps;
		++mSimulationSteps;
	}
}

void TestDriver::BeginInteractive() {
	mSession = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
	mLiveDir = std::filesystem::absolute(std::filesystem::path(mOutDir) / ("live-" + mSession)).string();
	std::error_code error;
	std::filesystem::create_directories(mLiveDir, error);
	if (error) { Fail("interactive: cannot create mailbox: " + error.message()); return; }
	mInteractiveReady = true;
	mInteractiveBusy = false;
	Log("interactive ready: " + mLiveDir);
	PublishInteractiveReply();
}

void TestDriver::PollInteractive() {
	const auto now = std::chrono::steady_clock::now();
	if (now < mNextInboxPoll) return;
	mNextInboxPoll = now + std::chrono::milliseconds(kInboxPollMilliseconds);
	const auto nextId = mRequestId + 1;
	const auto path = std::filesystem::path(mLiveDir) / ("request_" + std::to_string(nextId) + ".json");
	std::error_code error;
	if (!std::filesystem::is_regular_file(path, error)) return;
	// 文件刚发布时可能被系统短暂占用；未取得读取句柄前不消费序号，也不把它误判为空 JSON。
	std::ifstream input(path);
	if (!input.is_open()) return;

	// 只消费严格递增的序号；旧文件保留作记录，重试不会再次落种或推进时间。
	mRequestId = nextId;
	mInteractiveResults = nlohmann::json::array();
	mInteractiveFullState = false;
	mInteractiveBusy = true;
	mCommands.clear();
	mIndex = 0;
	mFramesLeft = -1;
	mInputPhase = -1;
	mCaptureTicket = 0;
	mTimeoutAccum = 0.0f;
	try {
		if (std::filesystem::file_size(path) > kMaxRequestBytes) throw std::runtime_error("request_too_large");
		nlohmann::json request;
		input >> request;
		if (request.at("session").get<std::string>() != mSession
			|| request.at("id").get<uint64_t>() != nextId) throw std::runtime_error("session_or_id_mismatch");
		const auto& commands = request.at("commands");
		if (!commands.is_array() || commands.empty() || commands.size() > kMaxRequestCommands)
			throw std::runtime_error("invalid_commands");
		mInteractiveFullState = request.value("fullState", false);
		// 在执行任何操作前检查命令形状，避免 malformed 后半批造成不明确的部分提交。
		for (const auto& command : commands) {
			if (!command.is_object() || !command.contains("op") || !command["op"].is_string())
				throw std::runtime_error("invalid_command");
			if (command["op"] == "screenshot") {
				const auto name = command.value("name", "shot.png");
				if (name.empty() || name.find_first_of("/\\:") != std::string::npos
					|| std::filesystem::path(name).extension() != ".png")
					throw std::runtime_error("invalid_screenshot_name");
			}
		}
		for (const auto& command : commands) mCommands.push_back(command);
		WriteStatus("running");
	}
	catch (const std::exception& e) {
		mInteractiveResults.push_back({ {"ok", false}, {"reason", e.what()} });
		CompleteInteractive();
	}
}

bool TestDriver::ExecuteInteractive(const nlohmann::json& command) {
	const std::string op = command.at("op").get<std::string>();
	std::string reason;
	try {
		auto* scene = dynamic_cast<GameScene*>(SceneManager::GetInstance().GetCurrentScene());
		auto* manager = scene ? scene->GetCardSlotManager() : nullptr;
		if (op == "advance") {
			if (mFramesLeft < 0) {
				if (!command.at("steps").is_number_integer()) throw std::runtime_error("steps_must_be_integer");
				const int steps = command.at("steps").get<int>();
				if (steps < 0 || steps > kMaxAdvanceSteps) reason = "invalid_steps";
				else if (DeltaTime::IsPaused()) reason = "player_paused";
				else {
					mFramesLeft = steps;
					mAdvanceSteps = steps;
				}
			}
			if (mAdvanceSteps > 0) return false;
		}
		else if (op == "observe") { /* 请求结束时统一导出冻结局面。 */ }
		else if (op == "quit") mInteractiveQuit = true;
		else if (!manager || !manager->CanAcceptGameplayInput()
			|| scene->GetUIManager().GetActiveMessageBoxCount() != 0) reason = "gameplay_input_blocked";
		else if (op == "player_plant") {
			for (const char* key : {"slot", "row", "col"})
				if (!command.at(key).is_number_integer()) throw std::runtime_error("plant_coordinates_must_be_integers");
			reason = manager->TryPlantFromSlot(command.at("slot").get<int>(),
				command.at("row").get<int>(), command.at("col").get<int>());
		}
		else if (op == "player_shovel") {
			Board* board = scene->GetBoard();
			const int row = command.at("row").get<int>(), col = command.at("col").get<int>();
			if (!board->GetCell(row, col)) reason = "invalid_cell";
			else {
				manager->DeselectCard();
				board->ActivateShovel();
				auto shovel = board->mShovel.lock();
				if (!shovel || !shovel->TryShovelAtPosition(board->GetCellCenterPosition(row, col))) reason = "no_shovel_target";
			}
		}
		else if (op == "buy_ice") {
			if (!scene->GetBoard()->BuyColdStorageIce(command.value("large",false))) reason = "order_unavailable";
		}
		else if (op == "collect_sun") {
			auto* sun = dynamic_cast<Sun*>(scene->GetBoard()->mEntityRegistry.GetCoin(command.at("id").get<int>()));
			auto* clickable = sun ? sun->GetClickable() : nullptr;
			if (!sun || !sun->IsActive() || sun->IsCollected() || !clickable
				|| !clickable->IsClickable || !clickable->onClick) reason = "sun_unavailable";
			else clickable->onClick(); // 复用玩家收集动画，阳光飞回后才记账。
		}
		else reason = "unsupported_operation";
	}
	catch (const std::exception& e) { reason = std::string("invalid_arguments: ") + e.what(); }
	if (!reason.empty()) Log("interactive " + op + " rejected: " + reason);
	mInteractiveResults.push_back({ {"op", op}, {"ok", reason.empty()}, {"reason", reason} });
	return true;
}

nlohmann::json TestDriver::BuildInteractiveState() {
	nlohmann::json full;
	if (!BuildStateJson("interactive", full)) return {};
	if (mInteractiveFullState) return full;
	auto compact = Pick(full, {"scene", "boardState", "level", "levelName", "rows", "columns", "sun",
		"wave", "maxWave", "paused", "pauseMenuOpen", "cards", "suns", "weather", "trophy",
		"coldStorage", "plantCount", "zombieCount", "mowerCount", "skySunCountdownMs", "nextWaveCountdownMs", "cells", "testAudio"});
	for (const char* key : {"plants", "zombies"}) {
		compact[key] = nlohmann::json::array();
		if (full.contains(key)) for (const auto& entity : full[key]) {
			compact[key].push_back(Pick(entity, {"id", "type", "row", "col", "xInt", "yInt", "health",
				"maxHealth", "bodyHealth", "bodyMaxHealth", "countableExecutionHealth", "sleeping", "squished"}));
		}
	}
	// 冷却与资金由卡本身导出；legalCells 只说明当前地形/占位资格，不能代替交易时复核。
	auto* scene = dynamic_cast<GameScene*>(SceneManager::GetInstance().GetCurrentScene());
	if (scene && scene->GetCardSlotManager() && compact.contains("cards")) {
		const auto& cards = scene->GetCardSlotManager()->GetCards();
		for (size_t slot = 0; slot < cards.size() && slot < compact["cards"].size(); ++slot) {
			auto& cardState = compact["cards"][slot];
			cardState["slot"] = slot;
			cardState["legalCells"] = nlohmann::json::array();
			if (!cards[slot] || cards[slot]->GetGameplayPlantType() == PlantType::PLANT_CARRYVINE) continue;
			for (int row = 0; row < scene->GetBoard()->mRows; ++row)
				for (int col = 0; col < scene->GetBoard()->mColumns; ++col)
					if (scene->GetBoard()->CanPlantAt(cards[slot]->GetGameplayPlantType(), row, col))
						cardState["legalCells"].push_back({row, col});
		}
	}
	return compact;
}

void TestDriver::PublishInteractiveReply() {
	const auto state = BuildInteractiveState();
	if (!mActive) return;
	const nlohmann::json response = {
		{"session", mSession}, {"id", mRequestId}, {"simulationSteps", mSimulationSteps},
		{"gameTimeSeconds", DeltaTime::GetTotalTime()},
		{"waiting", !mInteractiveQuit}, {"results", mInteractiveResults}, {"state", state}
	};
	const auto path = std::filesystem::path(mLiveDir) / ("response_" + std::to_string(mRequestId) + ".json");
	const auto temporary = path.string() + ".tmp";
	try {
		{ std::ofstream output(temporary); output << response.dump(); output.close();
			if (!output) throw std::runtime_error("response_write_failed"); }
		std::filesystem::rename(temporary, path); // 独立序号文件只发布一次，客户端不会读到半份 JSON。
		WriteStatus(mInteractiveQuit ? "stopping" : "waiting");
	}
	catch (const std::exception& e) { Fail(std::string("interactive: ") + e.what()); }
}

void TestDriver::CompleteInteractive() {
	mInteractiveBusy = false;
	PublishInteractiveReply();
	if (mActive && mInteractiveQuit) Finish();
}
