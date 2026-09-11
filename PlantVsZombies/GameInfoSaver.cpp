#include "GameInfoSaver.h"
#include "SaveLocation.h"
#include "SaveMigration.h"
#include "SaveSchema.h"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include "GameApp.h"
#include "Game/Board/Board.h"
#include "Game/Board/BoardPresentation.h"
#include "./Game/AudioSystem.h"
#include "./Game/Bullet/Bullet.h"
#include "./Game/Plant/Plant.h"
#include "./Game/Zombie/Zombie.h"
#include "./Game/LawnMower.h"
#include "./Game/Sun.h"
#include "./Game/Trophy.h"
#include "./Game/Crater.h"
#include "./Game/Ladder.h"
#include "./Game/IceWall.h"
#include "./Game/GroundRift.h"
#include "./Game/Bullet/BulletType.h"
#include "./Game/CardSlotManager.h"
#include "./Game/Card.h"
#include "./Game/Transform.h"
#include "./Game/GameObjectManager.h"
#include "./Game/AnimatedObject.h"
#include "./Game/AdventureProgression.h"
#include "Logger.h"

namespace {
	constexpr int kPoolGridSaveVersion = 2; // 第三大关六行+双植物槽及统一背景纵坐标的存档结构版本
	constexpr std::size_t kMaxRememberedCardNames = 64; // 玩家存档容纳的历史选卡名上限，防止损坏档无限扩张
	// ---- 存档根目录 -------------------------------------------------------------
	// Windows 使用系统“保存的游戏”目录；Linux 暂沿用相对目录；Android 使用应用私有目录。
	// AutoTest（包括 -AutoTestLoadSave）固定返回旧相对目录，确保只触碰构建目录里的测试档。
	const std::string& GetLegacySaveRoot() {
		static const std::string root = "./saves";
		return root;
	}

	/** 获取平台推荐的持久化目录；失败时回退旧目录，保证玩家仍可正常存档。 */
	std::string DiscoverPreferredSaveRoot() {
		const std::string root = SaveLocation::DiscoverPreferredRoot(GetLegacySaveRoot());
#if defined(_WIN32) && !defined(__ANDROID__)
		if (root == GetLegacySaveRoot()) {
			LOG_WARN("GameInfoSaver") << "无法获取 Windows 保存的游戏目录，继续使用旧存档目录 ./saves";
		}
#endif
		return root;
	}

	struct SaveRootState {
		std::string root;
	};

	/** 首次真实存档访问时创建中央目录并迁移旧档；目标不可用才整体回退旧目录。 */
	const SaveRootState& GetSaveRootState() {
		static const SaveRootState state = []() {
			SaveRootState result{ DiscoverPreferredSaveRoot() };
			if (result.root == GetLegacySaveRoot()) {
				return result;
			}

			std::error_code directoryError;
			const auto destination = std::filesystem::u8path(result.root);
			std::filesystem::create_directories(destination, directoryError);
			if (directoryError || !std::filesystem::is_directory(destination, directoryError) ||
				directoryError) {
				LOG_WARN("GameInfoSaver") << "中央存档目录不可用，继续使用旧目录 ./saves";
				result.root = GetLegacySaveRoot();
				return result;
			}

			const auto migration = SaveMigration::MigrateDirectory(
				std::filesystem::u8path(GetLegacySaveRoot()), destination);
			if (migration.migratedFileCount > 0 || migration.duplicateFileCount > 0) {
				LOG_INFO("GameInfoSaver") << "旧存档迁移完成: 新迁移 "
					<< migration.migratedFileCount << " 个，清理重复 "
					<< migration.duplicateFileCount << " 个";
			}
			if (migration.conflictFileCount > 0) {
				LOG_WARN("GameInfoSaver") << "发现 " << migration.conflictFileCount
					<< " 个同名但内容不同的旧存档；中央存档优先，冲突旧档已保留在 ./saves";
			}
			if (migration.errorCount > 0) {
				LOG_WARN("GameInfoSaver") << "旧存档迁移有 " << migration.errorCount
					<< " 项失败；源文件已保留，缺失存档将从 ./saves 回退读取";
			}
			return result;
		}();
		return state;
	}

	/** 返回本次运行的写入根目录；AutoTest 永远使用构建目录下的隔离路径。 */
	const std::string& GetSaveRoot() {
		if (GameAPP::mAutoTestMode) {
			return GetLegacySaveRoot();
		}
		return GetSaveRootState().root;
	}

	/** 优先读取中央存档；迁移失败且中央文件缺失时，逐文件回退旧目录。 */
	std::string GetSaveFileForRead(const std::string& fileName) {
		const std::string primary = FileManager::CombinePath(GetSaveRoot(), fileName);
		if (GameAPP::mAutoTestMode || GetSaveRoot() == GetLegacySaveRoot() ||
			FileManager::FileExists(primary)) {
			return primary;
		}

		const std::string legacy = FileManager::CombinePath(GetLegacySaveRoot(), fileName);
		return FileManager::FileExists(legacy) ? legacy : primary;
	}

	/** 删除中央及遗留目录中的同名关卡档，避免回退机制让已删除的旧档再次出现。 */
	bool DeleteSaveFile(const std::string& fileName) {
		bool found = false;
		bool deletedAll = true;
		const std::string primary = FileManager::CombinePath(GetSaveRoot(), fileName);
		if (FileManager::FileExists(primary)) {
			found = true;
			deletedAll = FileManager::DeleteFile(primary) && deletedAll;
		}

		if (!GameAPP::mAutoTestMode && GetSaveRoot() != GetLegacySaveRoot()) {
			const std::string legacy = FileManager::CombinePath(GetLegacySaveRoot(), fileName);
			if (FileManager::FileExists(legacy)) {
				found = true;
				deletedAll = FileManager::DeleteFile(legacy) && deletedAll;
			}
		}
		return found && deletedAll;
	}

	// ---- Animator 播放状态机的统一存读档 ----------------------------------------
	// 历史上只持久化 animTrack(当前轨道) + animFrame(当前帧)，读档时一律 PlayTrack(track)。
	// 但 PlayTrack 会把 mPlayingState 强制写成 PLAY_REPEAT，于是一只正在 PlayTrackOnce 的
	// 单位(长大/起跳/射击/喘气…)读档后会把那条一次性轨道当循环永远播放，再也切不回目标轨道。
	// 修复：把整台状态机(播放状态 + 目标轨道 + 回切速度/混合 + 基础/clip 速度)都存下来，
	// 读档时按状态用 PlayTrackOnce 重建一次性播放。旧存档无新字段 → 默认 PLAY_REPEAT，
	// 行为与从前逐位一致(向后兼容)。Chomper/PaperZombie/PotatoMine 的 LoadExtraData 在本
	// 函数之后运行，仍可按需覆盖(它们还负责帧事件等本层无法序列化的东西)。
	void SaveAnimState(nlohmann::json& j, const AnimatedObject* obj) {
		j["animTrack"] = obj->GetCurrentTrackName();
		j["animFrame"] = obj->GetCurrentFrame();
		j["animSpeed"] = obj->GetAnimationSpeed();        // 基础速度(base)
		j["animClipSpeed"] = obj->GetClipSpeed();         // 当前轨道 clip 覆盖(0=回落 base)
		j["animPlayState"] = static_cast<int>(obj->GetPlayingState());
		j["animTargetTrack"] = obj->GetTargetTrack();     // PlayTrackOnce 播完后的回切轨道
		j["animTargetTrackSpeed"] = obj->GetTargetTrackSpeed();
		j["animTargetTrackBlendTime"] = obj->GetTargetTrackBlendTime();
	}

	void RestoreAnimState(const nlohmann::json& j, AnimatedObject* obj) {
		// 无命名轨道的待机对象也保存基础速度，必须在空轨道早退之前恢复。
		if (j.contains("animSpeed")) {
			obj->SetAnimationSpeed(j.value("animSpeed", 1.0f));
		}
		std::string track = j.value("animTrack", std::string{});
		if (track.empty()) return;

		float clipSpeed = j.value("animClipSpeed", 0.0f);   // 缺省 0 = 回落 base，与旧行为一致
		PlayState state = static_cast<PlayState>(
			j.value("animPlayState", static_cast<int>(PlayState::PLAY_REPEAT)));  // 旧存档→循环

		if (state == PlayState::PLAY_ONCE_TO || state == PlayState::PLAY_ONCE) {
			// 重建一次性播放：播完后切到目标轨道(PLAY_ONCE 时目标为空 = 播完即停)
			obj->PlayTrackOnce(track,
				j.value("animTargetTrack", std::string{}),
				clipSpeed, 0.0f,
				j.value("animTargetTrackSpeed", 0.0f),
				j.value("animTargetTrackBlendTime", 0.5f));
		}
		else {
			obj->PlayTrack(track, clipSpeed);
		}

		// 基础速度与 clip 正交，单独恢复；仅当存档含该字段时才覆盖(旧存档不动，保持历史行为)
		if (j.contains("animSpeed")) {
			obj->SetAnimationSpeed(j.value("animSpeed", 1.0f));
		}
		// PlayTrack/PlayTrackOnce 会把帧重置到轨道起点，最后再恢复保存时的帧进度
		obj->SetCurrentFrame(j.value("animFrame", 0.0f));
	}
}

// 存档/读档的实际逻辑放在下列 *Impl 成员函数中（须为成员：Board 仅 friend 本类，自由
// 函数无私有成员访问权）；文件末尾的公有接口只做一层 try/catch 包裹（异常安全边界）。
// 命名加 Impl 后缀避免与同名公有方法构成无限递归调用。
bool GameInfoSaver::SavePlayerInfoImpl()
{
	if (GameAPP::mAutoTestMode) return true;   // AutoTest：不碰玩家存档
	auto& gameApp = GameAPP::GetInstance();

	FileManager::CreateDirectory(GetSaveRoot());

	nlohmann::json j;
	j["schemaVersion"] = SaveSchema::kCurrentPlayerVersion;
	j["vsync"] = gameApp.mVsync;
	j["fullscreen"] = gameApp.mFullscreen;
	j["difficulty"] = gameApp.Difficulty;
	j["adventureLevel"] = gameApp.mAdventureLevel;
	j["encounteredEliteDancer"] = gameApp.mEncounteredEliteDancer;
	j["crazyDaveTutorialsSeen"] = gameApp.mCrazyDaveTutorialsSeen;
	j["developerSelectedLevel"] = gameApp.mDeveloperSelectedLevel;
	j["developerSelectedZombie"] = gameApp.mDeveloperSelectedZombie;
	j["showPlantHP"] = gameApp.mShowPlantHP;
	j["showZombieHP"] = gameApp.mShowZombieHP;
	j["autoCollected"] = gameApp.mAutoCollected;
	j["enableMonteCarloAI"] = gameApp.mEnableMonteCarloAI;
	j["advancedPauseEnabled"] = gameApp.mAdvancedPauseEnabled;
	j["hxyModeEnabled"] = gameApp.mHxyModeEnabled;
	j["openingTyphoonProtectionEnabled"] = gameApp.mOpeningTyphoonProtectionEnabled;
	j["typhoonWeatherEnabled"] = gameApp.mTyphoonWeatherEnabled;
	j["lastSelectedCards"] = gameApp.mLastSelectedCards;
	j["soundVolume"] = AudioSystem::GetSoundVolume();
	j["musicVolume"] = AudioSystem::GetMusicVolume();
	j["havecards"] = gameApp.mHaveCards;

	return FileManager::SaveJsonFile(
		FileManager::CombinePath(GetSaveRoot(), "PlayerInfo.json"), j);
}

bool GameInfoSaver::LoadPlayerInfoImpl()
{
	if (GameAPP::mAutoTestMode) return true;   // AutoTest：全默认状态，保证确定性
	nlohmann::json j;
	if (!FileManager::LoadJsonFile(GetSaveFileForRead("PlayerInfo.json"), j))
		return false;
	std::string schemaError;
	if (!SaveSchema::UpgradePlayerDocument(j, schemaError)) {
		LOG_WARN("Save") << "拒绝加载玩家存档: " << schemaError;
		return false;
	}

	auto& gameApp = GameAPP::GetInstance();
	gameApp.mVsync = j.value("vsync", false);
	gameApp.mFullscreen = j.value("fullscreen", false);
	gameApp.Difficulty = j.value("difficulty", 1);
	gameApp.mAdventureLevel = j.value("adventureLevel", 1);
	gameApp.mEncounteredEliteDancer = j.value("encounteredEliteDancer", false);
	gameApp.mCrazyDaveTutorialsSeen.clear();
	if (auto it = j.find("crazyDaveTutorialsSeen"); it != j.end() && it->is_array()) {
		// 旧档没有该字段时自然为空；损坏档只接收当前冒险流程内的整数关卡号。
		for (const auto& savedLevel : *it) {
			if (!savedLevel.is_number_integer()) continue;
			const int level = savedLevel.get<int>();
			if (AdventureProgression::IsAdventureLevel(level)) {
				gameApp.mCrazyDaveTutorialsSeen.push_back(level);
			}
		}
		std::sort(gameApp.mCrazyDaveTutorialsSeen.begin(),
			gameApp.mCrazyDaveTutorialsSeen.end());
		gameApp.mCrazyDaveTutorialsSeen.erase(
			std::unique(gameApp.mCrazyDaveTutorialsSeen.begin(),
				gameApp.mCrazyDaveTutorialsSeen.end()),
			gameApp.mCrazyDaveTutorialsSeen.end());
	}
	gameApp.mDeveloperSelectedLevel = std::max(1, j.value("developerSelectedLevel", 1));
	gameApp.mDeveloperSelectedZombie =
		j.value("developerSelectedZombie", std::string("ZOMBIE_NORMAL"));
	gameApp.mShowPlantHP = j.value("showPlantHP", false);
	gameApp.mShowZombieHP = j.value("showZombieHP", false);
	gameApp.mAutoCollected = j.value("autoCollected", true);
	gameApp.mEnableMonteCarloAI = j.value("enableMonteCarloAI", true);
	gameApp.mAdvancedPauseEnabled = j.value("advancedPauseEnabled", false);
	gameApp.mHxyModeEnabled = j.value("hxyModeEnabled", false);
	gameApp.mOpeningTyphoonProtectionEnabled =
		j.value("openingTyphoonProtectionEnabled", true);
	gameApp.mTyphoonWeatherEnabled = j.value("typhoonWeatherEnabled", true);
	gameApp.mLastSelectedCards.clear();
	if (auto it = j.find("lastSelectedCards"); it != j.end() && it->is_array()) {
		// 只接收字符串并限制数量；未知或已移除的枚举名留到选卡界面按当前注册表过滤。
		for (const auto& cardName : *it) {
			if (!cardName.is_string()) continue;
			gameApp.mLastSelectedCards.push_back(cardName.get<std::string>());
			if (gameApp.mLastSelectedCards.size() >= kMaxRememberedCardNames) break;
		}
	}
	gameApp.mHaveCards = j.value("havecards",
		std::vector<PlantType>{PlantType::PLANT_PEASHOOTER});

	AudioSystem::SetSoundVolume(j.value("soundVolume", 0.5f));
	AudioSystem::SetMusicVolume(j.value("musicVolume", 0.5f));

	return true;
}

bool GameInfoSaver::SerializeLevelDataToPath(Board* board, CardSlotManager* manager,
	const std::string& filename)
{
	const bool stateOk = (board->mBoardState == BoardState::GAME) ||
		(board->mIsSurvival && board->mBoardState == BoardState::CHOOSE_CARD);
	if (!stateOk) return false;
	nlohmann::json document;
	return SerializeLevelDocument(board, manager, document) && FileManager::SaveJsonFile(filename, document);
}

bool GameInfoSaver::SerializeLevelDocument(Board* board, CardSlotManager* manager, nlohmann::json& j)
{
	j = nlohmann::json::object();
	j["schemaVersion"] = SaveSchema::kCurrentLevelVersion;

	// Board 状态
	j["boardState"] = static_cast<int>(board->mBoardState);
	if (board->mLevel >= 19 && board->mLevel <= 27) {
		j["poolGridVersion"] = kPoolGridSaveVersion;
	}
	j["isSurvival"] = board->mIsSurvival;
	j["hxyModeEnabled"] = board->mHxyModeEnabled;
	j["survivalRound"] = board->mSurvivalRound;
	if (board->mIsSurvival) {
		nlohmann::json perks;                    // 不直接写 j["perks"]：operator[] 会先物化成 null
		board->GetPerkManager().Save(perks);     // 零词条时 Save 不写任何键 → perks 仍为 null
		if (!perks.is_null()) j["perks"] = perks;   // 仅有词条时才落盘；否则省略，等同旧档天然兼容
		j["plantDamageEchoHitCounter"] = board->GetPlantDamageEchoHitCounter();

		// 冻结本轮已 roll 出的随机出怪池：持久化实际 mSpawnZombieList，读档直接还原而非
		// 重 roll（见 Load 端）。否则退出重进/读档会重跑 BuildSurvivalSpawnList 刷新随机子集，
		// 玩家可借此反复刷出怪池直到阵容有利（原版生存出怪恒定，不可刷）。
		nlohmann::json spawnList = nlohmann::json::array();
		for (ZombieType t : board->GetSpawnZombieList())
			spawnList.push_back(static_cast<int>(t));
		j["spawnList"] = spawnList;
	}
	j["sun"] = board->mSun;
	j["sunCountDown"] = board->mSunCountDown;
	j["poolSunCountDown"] = board->mPoolSunCountDown;
	j["currentWave"] = board->mCurrentWave;
	j["boardFrame"] = board->mBoardFrame;   // 舞王全队齐舞的节拍源，读档保节拍连续
	j["iceTrails"] = nlohmann::json::array();
	for (int row = 0; row < board->mRows
		&& row < static_cast<int>(board->mIceTimer.size()); ++row) {
		j["iceTrails"].push_back({
			{ "minX", board->mIceMinX[row] },
			{ "timer", board->mIceTimer[row] },
		});
	}
	j["goldenIceTrails"] = nlohmann::json::array();
	for (int row = 0; row < board->mRows
		&& row < static_cast<int>(board->mGoldenIceTimer.size()); ++row) {
		j["goldenIceTrails"].push_back({
			{ "minX", board->mGoldenIceMinX[row] },
			{ "timer", board->mGoldenIceTimer[row] },
		});
	}
	j["eliteScaredyShroomsPlanted"] = board->mEliteScaredyShroomsPlanted;
	j["weatherInitialized"] = board->mWeatherInitialized;
	j["rainIntensity"] = static_cast<int>(board->mRainIntensity);
	j["previousRainIntensity"] = static_cast<int>(board->mPreviousRainIntensity);
	j["forecastRainIntensity"] = static_cast<int>(board->mForecastRainIntensity);
	j["actualForecastRainIntensity"] = static_cast<int>(board->mActualForecastRainIntensity);
	j["weatherTimer"] = board->mWeatherTimer;
	j["weatherTransitionTimer"] = board->mWeatherTransitionTimer;
	j["lightningTimer"] = board->mLightningTimer;
	j["rainCanIntensify"] = board->mRainCanIntensify;
	j["rainCanHold"] = board->mRainCanHold;
	j["weatherForecastReady"] = board->mWeatherForecastReady;
	j["weatherForecastDisrupted"] = board->mWeatherForecastDisrupted;
	j["weatherPanelInterferenceTimer"] = board->mWeatherPanelInterferenceTimer;
	j["winterTemperatureInitialized"] = board->mWinterTemperatureInitialized;
	j["openingColdWavePlanInitialized"] = board->mOpeningColdWavePlanInitialized;
	j["coldWavePhase"] = static_cast<int>(board->mColdWavePhase);
	j["coldWaveStrength"] = static_cast<int>(board->mColdWaveStrength);
	j["coldWaveTimer"] = board->mColdWaveTimer;
	j["ambientTemperatureC"] = board->mAmbientTemperatureC;
	j["coldWaveTargetTemperatureC"] = board->mColdWaveTargetTemperatureC;
	j["coldWaveCoolingDuration"] = board->mColdWaveCoolingDuration;
	j["coldWaveHoldDuration"] = board->mColdWaveHoldDuration;
	j["coldWaveThawDuration"] = board->mColdWaveThawDuration;
	j["coldWaveForecastDisrupted"] = board->mColdWaveForecastDisrupted;
	j["winterFrostVariant"] = board->mWinterFrostVariant;
	if (board->IsMineBackground()) {
		j["mine"] = { {"rocks", board->mMineGrid.rock}, {"digCell", board->mMineDigCell},
			{"digRemaining", board->mMineDigRemaining}, {"tutorialSeen", board->mMineTutorialSeen},
			{"fogElapsed",board->mMineFogElapsed}, {"fogNextWave",board->mMineFogNextWave},
			{"fogTutorialSeen",board->mMineFogTutorialSeen}, {"fogNotice",board->mMineFogNoticeRemaining},
			{"plannedWave", board->mMinePlannedWave}, {"wavePlan", board->mMineWavePlan} };
	}
	j["echoWaves"] = nlohmann::json::array();
	for (const auto& wave : board->mEchoWaves) j["echoWaves"].push_back({
		{"row",wave.row},{"column",wave.column},{"elapsed",wave.elapsed},
		{"distances",wave.distances},{"hitIDs",wave.hitIDs},{"hitIceWall",wave.hitIceWall}});
	j["polarNightInitialized"] = board->mPolarNightInitialized;
	j["polarNightPhase"] = static_cast<int>(board->mPolarNightPhase);
	j["polarPlanIsWhiteout"] = board->mPolarPlanIsWhiteout;
	j["polarLastPlanWasFalse"] = board->mPolarLastPlanWasFalse;
	j["polarFirstWhiteoutCompleted"] = board->mPolarFirstWhiteoutCompleted;
	j["polarDangerMask"] = board->mPolarDangerMask;
	j["polarTemperatureC"] = board->mPolarTemperatureC;
	j["polarHumidityPercent"] = board->mPolarHumidityPercent;
	j["polarWindSpeedMps"] = board->mPolarWindSpeedMps;
	j["polarStartTemperatureC"] = board->mPolarStartTemperatureC;
	j["polarStartHumidityPercent"] = board->mPolarStartHumidityPercent;
	j["polarStartWindSpeedMps"] = board->mPolarStartWindSpeedMps;
	j["polarTargetTemperatureC"] = board->mPolarTargetTemperatureC;
	j["polarTargetHumidityPercent"] = board->mPolarTargetHumidityPercent;
	j["polarTargetWindSpeedMps"] = board->mPolarTargetWindSpeedMps;
	j["polarPhaseTimer"] = board->mPolarPhaseTimer;
	j["polarPhaseDuration"] = board->mPolarPhaseDuration;
	j["polarAllDangerTimer"] = board->mPolarAllDangerTimer;
	j["polarHighHumidityTimer"] = board->mPolarHighHumidityTimer;
	j["polarHumidityEpisodeConsumed"] = board->mPolarHumidityEpisodeConsumed;
	j["polarTutorialHoleBatchConsumed"] = board->mPolarTutorialHoleBatchConsumed;
	j["polarVerticalWindDirection"] = static_cast<int>(
		board->mPolarVerticalWindDirection);
	j["polarWhiteoutTimer"] = board->mPolarWhiteoutTimer;
	j["polarFluctuationTimer"] = board->mPolarFluctuationTimer;
	j["polarFluctuationDuration"] = board->mPolarFluctuationDuration;
	j["polarFinalWaveUpgradeApplied"] = board->mPolarFinalWaveUpgradeApplied;
	j["snowHoles"] = nlohmann::json::array();
	for (const SnowHoleState& hole : board->mSnowHoles) {
		j["snowHoles"].push_back({
			{ "column", hole.column },
			{ "phase", static_cast<int>(hole.phase) },
			{ "timer", hole.timer },
		});
	}
	j["pendingSnowHoleSpawns"] = nlohmann::json::array();
	for (const Board::PendingSnowHoleSpawn& pending : board->mPendingSnowHoleSpawns) {
		j["pendingSnowHoleSpawns"].push_back({
			{ "type", static_cast<int>(pending.type) },
			{ "row", pending.row },
			{ "holeColumn", pending.holeColumn },
			{ "spawnWave", pending.spawnWave },
			{ "timer", pending.timer },
			{ "tutorialSnowBurrow", pending.tutorialSnowBurrow },
		});
	}
	j["pendingAuroraRifts"] = nlohmann::json::array();
	for (const Board::PendingAuroraRift& rift : board->mPendingAuroraRifts) {
		j["pendingAuroraRifts"].push_back({
			{ "type", static_cast<int>(rift.type) }, { "row", rift.row },
			{ "column", rift.column }, { "spawnWave", rift.spawnWave },
			{ "timer", rift.timer }, { "transactionID", rift.transactionID },
			{ "ownerZombieID", rift.ownerZombieID },
		});
	}
	j["temporalAnchors"] = nlohmann::json::array();
	for (const Board::TemporalAnchor& anchor : board->mTemporalAnchors) {
		nlohmann::json targets = nlohmann::json::array();
		for (const Board::TemporalTargetSnapshot& target : anchor.targets) {
			targets.push_back({
				{ "zombieID", target.zombieID }, { "type", static_cast<int>(target.type) },
				{ "row", target.row }, { "x", target.x },
				{ "mineRowOffset", target.mineRowOffset }, { "mineTargetCell", target.mineTargetCell },
				{ "bodyHealth", target.bodyHealth }, { "helmType", static_cast<int>(target.helmType) },
				{ "helmHealth", target.helmHealth }, { "shieldType", static_cast<int>(target.shieldType) },
				{ "shieldHealth", target.shieldHealth }, { "slowTimer", target.slowTimer },
				{ "frozenTimer", target.frozenTimer }, { "butterTimer", target.butterTimer },
				{ "paralysisTimer", target.paralysisTimer }, { "hasHead", target.hasHead },
				{ "hasArm", target.hasArm }, { "irreversible", target.irreversible },
				{ "restoreHelm", target.restoreHelm },
				{ "restoreShield", target.restoreShield },
				{ "specialActionSubmitted", target.specialActionSubmitted },
				{ "abilityStateValid", target.abilityStateValid },
				{ "abilityPhase", target.abilityPhase },
				{ "abilityRemaining", target.abilityRemaining },
				{ "abilityReleaseCount", target.abilityReleaseCount },
			});
		}
		j["temporalAnchors"].push_back({
			{ "ownerZombieID", anchor.ownerZombieID },
			{ "timer", anchor.timer }, { "targets", std::move(targets) },
		});
	}
	j["nextDiscontinuousTransactionID"] = board->mNextDiscontinuousTransactionID;
	j["dawnNavigationTimer"] = board->mDawnNavigationTimer;
	j["stormyNightInitialized"] = board->mStormyNightInitialized;
	j["stormyNightFlashPattern"] = board->mStormyNightFlashPattern;
	j["stormyNightFlashTimer"] = board->mStormyNightFlashTimer;
	j["roofRunoffCharge"] = board->mRoofRunoffCharge;
	j["roofRunoffRetainedCharge"] = board->mRoofRunoffRetainedCharge;
	j["roofRunoffPhase"] = static_cast<int>(board->mRoofRunoffPhase);
	j["roofRunoffPhaseTimer"] = board->mRoofRunoffPhaseTimer;
	j["roofRunoffRowMask"] = board->mRoofRunoffRowMask;
	j["nightRoofCharge"] = board->mNightRoofCharge;
	j["nightRoofOvercharge"] = board->mNightRoofOvercharge;
	j["nightRoofChargePhase"] = static_cast<int>(board->mNightRoofChargePhase);
	j["nightRoofChargePhaseTimer"] = board->mNightRoofChargePhaseTimer;
	j["nightRoofChargeRow"] = board->mNightRoofChargeRow;
	j["nightRoofChargeGuided"] = board->mNightRoofChargeGuided;
	j["nightRoofChargeGuideID"] = board->mNightRoofChargeGuideID;
	j["nightRoofHijackerSelectionAttempted"] = board->mNightRoofHijackerSelectionAttempted;
	j["nightRoofHijackerID"] = board->mNightRoofHijackerID;
	j["nightRoofHijackerWarningExtended"] = board->mNightRoofHijackerWarningExtended;
	j["nightRoofHijackerFinalizing"] = board->mNightRoofHijackerFinalizing;
	j["fogWeatherInitialized"] = board->mFogWeatherInitialized;
	j["fogWeatherIntensity"] = static_cast<int>(board->mFogWeatherIntensity);
	j["forecastFogWeatherIntensity"] =
		static_cast<int>(board->mForecastFogWeatherIntensity);
	j["actualForecastFogWeatherIntensity"] =
		static_cast<int>(board->mActualForecastFogWeatherIntensity);
	j["fogWeatherTimer"] = board->mFogWeatherTimer;
	j["fogWeatherForecastReady"] = board->mFogWeatherForecastReady;
	j["fogWeatherForecastDisrupted"] = board->mFogWeatherForecastDisrupted;
	j["fogDispersal"] = board->mFogDispersal;
	j["fogVisualOffsetX"] = board->mFogVisualOffsetX;
	j["pendingHeavyTyphoonPrepared"] = board->mPendingHeavyTyphoonPrepared;
	j["pendingHeavyTyphoonOpeningProtected"] =
		board->mPendingHeavyTyphoonOpeningProtected;
	j["pendingHeavyTyphoonStrength"] = static_cast<int>(board->mPendingHeavyTyphoonStrength);
	j["pendingHeavyWindDirection"] = static_cast<int>(board->mPendingHeavyWindDirection);
	j["pendingHeavyTyphoonStrengthTimer"] = board->mPendingHeavyTyphoonStrengthTimer;
	j["pendingHeavyWindDirectionTimer"] = board->mPendingHeavyWindDirectionTimer;
	j["pendingHeavyWindGustTimer"] = board->mPendingHeavyWindGustTimer;
	j["pendingHeavyTyphoonGustsRemaining"] = board->mPendingHeavyTyphoonGustsRemaining;
	j["pendingHeavyRainPromptVariant"] = board->mPendingHeavyRainPromptVariant;
	j["typhoonStrength"] = static_cast<int>(board->mTyphoonStrength);
	j["windDirection"] = static_cast<int>(board->mWindDirection);
	j["typhoonStrengthTimer"] = board->mTyphoonStrengthTimer;
	j["windDirectionTimer"] = board->mWindDirectionTimer;
	j["windGustTimer"] = board->mWindGustTimer;
	j["typhoonGustsRemaining"] = board->mTyphoonGustsRemaining;
	j["typhoonGustActive"] = board->mTyphoonGustActive;
	j["activeGustStrength"] = static_cast<int>(board->mActiveGustStrength);
	j["activeGustDirection"] = static_cast<int>(board->mActiveGustDirection);
	j["activeGustDuration"] = board->mActiveGustDuration;
	j["activeGustTimer"] = board->mActiveGustTimer;
	j["activeGustPlantMoveTimer"] = board->mActiveGustPlantMoveTimer;
	j["activeGustPlantMoved"] = board->mActiveGustPlantMoved;
	j["weakWeatherPhasesSinceHeavy"] = board->mWeakWeatherPhasesSinceHeavy;
	j["heavyPhasesWithoutTyphoon"] = board->mHeavyPhasesWithoutTyphoon;
	j["eliteDancersSpawnedThisWave"] = board->mEliteDancersSpawnedThisWave;
	j["reinforcedDoorsSpawnedThisWave"] = board->mReinforcedDoorsSpawnedThisWave;
	j["elitePolevaultersSpawnedThisWave"] = board->mElitePolevaultersSpawnedThisWave;
	j["gildedZambonisSpawnedThisWave"] = board->mGildedZambonisSpawnedThisWave;
	j["eliteDolphinRidersSpawnedThisWave"] = board->mEliteDolphinRidersSpawnedThisWave;
	j["eliteJackInTheBoxesSpawnedThisWave"] =
		board->mEliteJackInTheBoxesSpawnedThisWave;
	j["eliteDiggersSpawnedThisWave"] = board->mEliteDiggersSpawnedThisWave;
	j["elitePogosSpawnedThisWave"] = board->mElitePogosSpawnedThisWave;
	j["eliteLaddersSpawnedThisWave"] = board->mEliteLaddersSpawnedThisWave;
	j["eliteCatapultsSpawnedThisWave"] = board->mEliteCatapultsSpawnedThisWave;
	j["redeyeGargantuarsSpawnedThisWave"] = board->mRedeyeGargantuarsSpawnedThisWave;
	j["insulatorsSpawnedThisWave"] = board->mInsulatorsSpawnedThisWave;
	j["hijackersSpawnedThisWave"] = board->mHijackersSpawnedThisWave;
	j["hijackerSpawnCooldownWavesRemaining"] =
		board->mHijackerSpawnCooldownWavesRemaining;
	j["hijackerSpawnBlockedThisWave"] = board->mHijackerSpawnBlockedThisWave;
	j["groundingZombiesSpawnedThisWave"] = board->mGroundingZombiesSpawnedThisWave;
	j["bobsledTeamsSpawnedThisWave"] = board->mBobsledTeamsSpawnedThisWave;
	j["iceWallEngineersSpawnedThisWave"] = board->mIceWallEngineersSpawnedThisWave;
	j["iceCrackDrillsSpawnedThisWave"] = board->mIceCrackDrillsSpawnedThisWave;
	j["weatherJammersSpawnedThisWave"] = board->mWeatherJammersSpawnedThisWave;
	j["iceStatueExecutionersSpawnedThisWave"] =
		board->mIceStatueExecutionersSpawnedThisWave;
	j["snowBurrowsSpawnedThisWave"] = board->mSnowBurrowsSpawnedThisWave;
	j["snowBurrowTutorialHoleSpawnConsumed"] =
		board->mSnowBurrowTutorialHoleSpawnConsumed;
	j["adaptiveHelmetsSpawnedThisWave"] = board->mAdaptiveHelmetsSpawnedThisWave;
	j["adaptiveHelmetTutorialWaveSpawned"] =
		board->mAdaptiveHelmetTutorialWaveSpawned;
	j["thermalSnipersSpawnedThisWave"] = board->mThermalSnipersSpawnedThisWave;
	j["thermalSniperTutorialSpawned"] = board->mThermalSniperTutorialSpawned;
	j["auroraPriestsSpawnedThisWave"] = board->mAuroraPriestsSpawnedThisWave;
	j["clockmakersSpawnedThisWave"] = board->mClockmakersSpawnedThisWave;
	j["crystalMinersSpawnedThisWave"] = board->mCrystalMinersSpawnedThisWave;
	j["auroraPriestGuaranteeConsumed"] = board->mAuroraPriestGuaranteeConsumed;
	j["clockmakerGuaranteeConsumed"] = board->mClockmakerGuaranteeConsumed;
	j["mistFuelDropAccumulator"] = board->mMistFuelDropAccumulator;
	WeatherPresentationState weatherPresentation;
	if (auto* presentation = board->GetPresentation()) {
		weatherPresentation = presentation->CaptureWeatherPresentationState();
	}
	j["currentWeatherNoticeTimer"] = weatherPresentation.currentWeatherNoticeTimer;
	j["weatherForecastFailureTimer"] = weatherPresentation.forecastFailureTimer;
	j["failedForecastRainIntensity"] =
		static_cast<int>(weatherPresentation.failedForecast);
	j["weatherForecastFailureActualIntensity"] =
		static_cast<int>(weatherPresentation.actualForecast);
	j["failedForecastTyphoonStrength"] =
		static_cast<int>(weatherPresentation.failedForecastTyphoon);
	j["weatherForecastFailureActualTyphoonStrength"] =
		static_cast<int>(weatherPresentation.actualForecastTyphoon);
	j["maxWave"] = board->mMaxWave;
	j["zombieCountDown"] = board->mZombieCountDown;
	j["totalZombieHP"] = board->mTotalZombieHP;
	j["currentWaveZombieHP"] = board->mCurrectWaveZombieHP;
	j["nextWaveSpawnZombieHP"] = board->mNextWaveSpawnZombieHP;

	// 保存 EntityRegistry 的 ID 计数器
	j["nextPlantID"] = board->mEntityRegistry.GetNextPlantID();
	j["nextZombieID"] = board->mEntityRegistry.GetNextZombieID();
	j["nextBulletID"] = board->mEntityRegistry.GetNextBulletID();
	j["nextCoinID"] = board->mEntityRegistry.GetNextCoinID();
	j["nextMowerID"] = board->mEntityRegistry.GetNextMowerID();

	// 植物
	nlohmann::json plantsArr = nlohmann::json::array();
	for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
		auto plant = board->mEntityRegistry.GetPlant(id);
		// 死亡/替换先失活、下一帧才从注册表移除；此间存档不能把旧壳重新保存为活株。
		if (!plant || !plant->IsActive()) continue;
		nlohmann::json p;
		p["id"] = id;
		p["type"] = static_cast<int>(plant->mPlantType);
		// 阵风触发时 row/column 已先落到目标格；纯视觉追赶偏移不保存，读档稳定吸附目标格。
		p["row"] = plant->mRow;
		p["column"] = plant->mColumn;
		p["health"] = plant->mPlantHealth;
		p["maxHealth"] = plant->mPlantMaxHealth;
		p["isSleeping"] = plant->GetSleepState();
		p["wakeUpTimer"] = plant->GetWakeUpTimeRemaining();
		p["shutdownTimer"] = plant->GetShutdownTimeRemaining();
		p["unyieldingRootsSpent"] = plant->HasSpentUnyieldingRoots();
		p["unyieldingRootsTimer"] = plant->GetUnyieldingRootsTimeRemaining();
		p["iceSealOwnerZombieID"] = plant->GetIceSealOwnerZombieID();
		if (plant->IsImitated()) p["imitated"] = true;
		p["isSquished"] = plant->IsSquished();
		p["occupiesGridSlot"] = plant->OccupiesGridSlot();
		if (plant->IsSquished()) {
			const Vector squishVisual = plant->GetSquishVisualPosition();
			p["squishTimer"] = plant->GetSquishTimeRemaining();
			p["squishVisualX"] = squishVisual.x;
			p["squishVisualY"] = squishVisual.y;
		}
		SaveAnimState(p, plant);

		nlohmann::json extraData;
		plant->SaveExtraData(extraData);
		if (!extraData.empty()) {
			p["extraData"] = extraData;
		}
		plantsArr.push_back(p);
	}
	j["plants"] = plantsArr;

	// 小推车
	nlohmann::json mowersArr = nlohmann::json::array();
	for (int id : board->mEntityRegistry.GetAllMowerIDs()) {
		auto* mower = board->mEntityRegistry.GetMower(id);
		if (!mower) continue;
		nlohmann::json m;
		m["id"] = id;
		m["type"] = static_cast<int>(mower->mMowerType);
		m["row"] = mower->mRow;
		m["state"] = static_cast<int>(mower->mState);
		m["mowerHeight"] = static_cast<int>(mower->mMowerHeight);
		m["poolVisualOffsetY"] = mower->mPoolVisualOffsetY;
		m["speed"] = mower->mSpeed;
		m["x"] = mower->GetPosition().x;
		m["y"] = mower->GetPosition().y;
		SaveAnimState(m, mower);
		mowersArr.push_back(m);
	}
	j["mowers"] = mowersArr;

	// 僵尸
	nlohmann::json zombiesArr = nlohmann::json::array();
	for (int id : board->mEntityRegistry.GetAllZombieIDs()) {
		auto zombie = board->mEntityRegistry.GetZombie(id);
		// 濒死僵尸 Die() 后 shared_ptr 仍滞留在 GameObjectManager 的待删队列中（要到下一帧 flush 才释放），
		// 其 weak_ptr 此刻仍可 lock，会被 GetAllZombieIDs 返回。mActive 已在 Die() 置 false，借此排除，
		// 避免把触发轮清的那只死尸序列化进存档（否则重载会复活成血量≤0 的幽灵僵尸）。
		if (!zombie || !zombie->IsActive()) continue;
		nlohmann::json z;
		z["id"] = id;
		z["type"] = static_cast<int>(zombie->mZombieType);
		z["row"] = zombie->mRow;
		z["x"] = zombie->GetPosition().x;
		z["y"] = zombie->GetPosition().y;

		z["bodyHealth"] = zombie->mBodyHealth;
		z["bodyMaxHealth"] = zombie->mBodyMaxHealth;
		z["helmType"] = zombie->mHelmType;
		z["helmHealth"] = zombie->mHelmHealth;
		z["helmMaxHealth"] = zombie->mHelmMaxHealth;
		z["shieldType"] = zombie->mShieldType;
		z["shieldHealth"] = zombie->mShieldHealth;
		z["shieldMaxHealth"] = zombie->mShieldMaxHealth;

		z["spawnWave"] = zombie->mSpawnWave;
		z["attackDamage"] = zombie->mAttackDamage;
		z["needDropArm"] = zombie->mNeedDropArm;
		z["needDropHead"] = zombie->mNeedDropHead;
		zombie->SaveProtectedData(z);
		SaveAnimState(z, zombie);
		nlohmann::json extraData;
		zombie->SaveExtraData(extraData);
		if (!extraData.empty()) {
			z["extraData"] = extraData;
		}
		zombiesArr.push_back(z);
	}
	j["zombies"] = zombiesArr;

	// 子弹
	nlohmann::json bulletsArr = nlohmann::json::array();
	for (int id : board->mEntityRegistry.GetAllBulletIDs()) {
		auto bullet = board->mEntityRegistry.GetBullet(id);
		if (!bullet) continue;
		nlohmann::json b;
		b["type"] = static_cast<int>(bullet->mBulletType);
		b["poolType"] = static_cast<int>(bullet->GetPoolType());
		b["plantOriginKind"] = static_cast<int>(bullet->mPlantDamageOrigin.kind);
		b["plantOriginLineage"] = static_cast<int>(bullet->mPlantDamageOrigin.lineage);
		b["id"] = id;
		b["row"] = bullet->mRow;
		b["x"] = bullet->GetPosition().x;
		b["y"] = bullet->GetPosition().y;
		b["damage"] = bullet->GetBulletDamage();
		b["velocityX"] = bullet->GetVelocityX();
		b["velocityY"] = bullet->GetVelocityY();
		b["rotationDegrees"] = bullet->GetRotationDegrees();
		b["rotationSpeedDegrees"] = bullet->GetRotationSpeedDegrees();
		b["hitTorchwoodColumn"] = bullet->GetHitTorchwoodColumn();
		b["hitAuroraTorchwoodColumn"] = bullet->GetHitAuroraTorchwoodColumn();
		b["auroraHitZombieIDs"] = bullet->GetAuroraHitZombieIDs();
		b["auroraPlayedHitSound"] = bullet->HasPlayedAuroraHitSound();
		b["piercedZombieIDs"] = bullet->GetPiercedZombieIDs();
		b["spikeDamageRemainders"] = bullet->GetSpikeDamageRemainders();
		b["threepeaterMotion"] = bullet->IsThreepeaterMotion();
		b["targetsFlying"] = bullet->TargetsFlying();
		b["polarGuided"] = bullet->IsPolarGuided();
		if (bullet->mBulletType == BulletType::BULLET_THERMAL_PULSE) {
			b["thermalEndpointX"] = bullet->GetThermalEndpointX();
			b["thermalEndpointY"] = bullet->GetThermalEndpointY();
			b["thermalOriginalPlantID"] = bullet->GetThermalOriginalPlantID();
		}
		b["lobbedMotion"] = bullet->IsLobbedMotion();
		if (bullet->IsLobbedMotion()) {
			b["lobStartX"] = bullet->GetLobStart().x;
			b["lobStartY"] = bullet->GetLobStart().y;
			b["lobTargetX"] = bullet->GetLobTarget().x;
			b["lobTargetY"] = bullet->GetLobTarget().y;
			b["lobElapsed"] = bullet->GetLobElapsed();
			b["lobDuration"] = bullet->GetLobDuration();
			b["lobApexHeight"] = bullet->GetLobApexHeight();
			b["lobTargetsIceWall"] = bullet->TargetsIceWall();
			b["polarWindMiss"] = bullet->IsPolarWindMiss();
		}
		b["cobCannonMotion"] = bullet->IsCobCannonMotion();
		if (bullet->IsCobCannonMotion()) {
			b["cobStartX"] = bullet->GetCobStart().x;
			b["cobStartY"] = bullet->GetCobStart().y;
			b["cobTargetX"] = bullet->GetCobTarget().x;
			b["cobTargetY"] = bullet->GetCobTarget().y;
			b["cobTargetRow"] = bullet->GetCobTargetRow();
			b["cobElapsed"] = bullet->GetCobElapsed();
			b["cobDuration"] = bullet->GetCobDuration();
			b["polarWindMiss"] = bullet->IsPolarWindMiss();
		}
		bulletsArr.push_back(b);
	}
	j["bullets"] = bulletsArr;

	// 太阳
	nlohmann::json sunsArr = nlohmann::json::array();
	for (int id : board->mEntityRegistry.GetAllCoinIDs()) {
		auto* coin = board->mEntityRegistry.GetCoin(id);
		if (!coin) continue;
		auto* sun = dynamic_cast<Sun*>(coin);
		if (sun) {
			nlohmann::json s;
			s["id"] = id;
			s["x"] = sun->GetPosition().x;
			s["y"] = sun->GetPosition().y;
			SaveAnimState(s, sun);
			s["small"] = (dynamic_cast<SmallSun*>(coin) != nullptr);	// 区分大/小阳光
			sunsArr.push_back(s);
		}
	}
	j["suns"] = sunsArr;

	// 奖杯（每关至多一个，Board 直接持有引用，不走金币表）
	nlohmann::json trophiesArr = nlohmann::json::array();
	if (auto trophy = board->mTrophy.lock()) {
		nlohmann::json t;
		t["x"] = trophy->GetPosition().x;
		t["y"] = trophy->GetPosition().y;
		trophiesArr.push_back(t);
	}
	j["trophies"] = trophiesArr;

	// 弹坑（毁灭菇）：row/col/剩余秒数即可完整还原；无弹坑时省略字段，旧档天然兼容
	nlohmann::json cratersArr = nlohmann::json::array();
	for (auto& weak : board->mCraters) {
		auto crater = weak.lock();
		if (!crater || !crater->IsActive()) continue;
		cratersArr.push_back({
			{ "row", crater->mRow },
			{ "column", crater->mColumn },
			{ "timeLeft", crater->mTimeLeft },
		});
	}
	if (!cratersArr.empty()) j["craters"] = cratersArr;

	// 已放置扶梯保存格子与样式；旧档缺 style 时按经典扶梯恢复。
	nlohmann::json laddersArr = nlohmann::json::array();
	for (auto& weak : board->mLadders) {
		auto ladder = weak.lock();
		if (!ladder || !ladder->IsActive()) continue;
		laddersArr.push_back({
			{ "row", ladder->mRow },
			{ "column", ladder->mColumn },
			{ "style", static_cast<int>(ladder->GetStyle()) },
		});
	}
	if (!laddersArr.empty()) j["ladders"] = laddersArr;

	// 完整墙与工程师生命独立；施工墙额外保存完成态和施工者 ID，用于读档后继续或孤儿回收。
	if (auto wall = board->mIceWall.lock(); wall && wall->IsActive()) {
		j["iceWall"] = {
			{ "row", wall->GetRow() },
			{ "centerX", wall->GetCenterX() },
			{ "health", wall->GetHealth() },
			{ "maxHealth", wall->GetMaxHealth() },
			{ "thawDamageRemainder", wall->GetThawDamageRemainder() },
			{ "constructionComplete", wall->IsConstructionComplete() },
			{ "builderZombieID", wall->GetBuilderZombieID() },
		};
	}

	// 已提交地裂与来源僵尸完全独立；保存传播前沿、下一列和雪锚累计倍率即可无重播恢复。
	nlohmann::json groundRifts = nlohmann::json::array();
	for (GroundRift* rift : board->GetGroundRifts()) {
		if (!rift || !rift->IsActive()) continue;
		groundRifts.push_back({
			{ "row", rift->GetRow() },
			{ "frontX", rift->GetFrontX() },
			{ "nextColumn", rift->GetNextColumn() },
			{ "downstreamDamageMultiplier", rift->GetDownstreamDamageMultiplier() },
		});
	}
	if (!groundRifts.empty()) j["groundRifts"] = std::move(groundRifts);

	// 卡牌只在 GAME 中代表已提交卡组。CHOOSE_CARD 中的卡槽必须为空；即便未来流程意外留下
	// 旧卡，也不能把它序列化成下一轮已提交卡组，否则读档后再次选卡会叠出两套卡牌。
	nlohmann::json cardsArr = nlohmann::json::array();
	if (manager && board->mBoardState == BoardState::GAME) {
		for (auto* card : manager->GetCards()) {
			if (!card) continue;
			auto transform = card->GetTransform();
			if (!transform) continue;
			nlohmann::json c;
			c["plantType"] = static_cast<int>(card->GetPlantType());
			c["posX"] = transform->GetPosition().x;
			c["posY"] = transform->GetPosition().y;
			c["sunCost"] = card->GetSunCost();
			c["cooldownTime"] = card->GetCooldownTime();
			c["isCooldown"] = card->IsCooldown();
			c["cooldownTimer"] = card->GetCooldownTimer();
			if (card->GetPlantType() == PlantType::PLANT_IMITATER) {
				c["imitaterTarget"] = static_cast<int>(card->GetImitaterTarget());
			}
			if (card->GetGameplayPlantType() == PlantType::PLANT_BLOVER) {
				c["bloverDirection"] =
					static_cast<int>(card->GetBloverDirection());
			}
			cardsArr.push_back(c);
		}
	}
	j["cards"] = cardsArr;

	// 生存轮间空槽重选的冷却快照：选卡阶段卡槽已被清空，仍在冷却的卡牌冷却进度暂存在 GameScene 的
	// mSurvivalCardCooldowns 里（而非卡槽）。它是该进度的唯一载体，必须单独持久化，否则选卡界面
	// 退出重进后快照丢失 → 选完卡冷却被清零。仅 survival 写入，普通模式无此字段、行为不变。
	if (board->mIsSurvival && board->GetPresentation()) {
		nlohmann::json cooldownArr = nlohmann::json::array();
		for (const auto& [type, cd] :
			board->GetPresentation()->GetSurvivalCardCooldowns()) {
			nlohmann::json c;
			c["plantType"] = static_cast<int>(type);
			c["cooldownTimer"] = cd.first;
			c["cooldownTime"] = cd.second;
			cooldownArr.push_back(c);
		}
		j["survivalCardCooldowns"] = cooldownArr;
	}

	return true;
}

bool GameInfoSaver::SaveLevelDataImpl(Board* board, CardSlotManager* manager)
{
	if (GameAPP::mAutoTestMode) return true;   // AutoTest：不写关卡存档
	FileManager::CreateDirectory(GetSaveRoot());
	const std::string filename = FileManager::CombinePath(GetSaveRoot(),
		"level" + std::to_string(board->mLevel) + "_data.json");
	return SerializeLevelDataToPath(board, manager, filename);
}

bool GameInfoSaver::DeserializeLevelDataFromPath(Board* board, CardSlotManager* manager,
	const std::string& filename)
{
	nlohmann::json j;
	if (!FileManager::LoadJsonFile(filename, j))
		return false;
	return DeserializeLevelDocument(board, manager, std::move(j));
}

bool GameInfoSaver::DeserializeLevelDocument(Board* board, CardSlotManager* manager, nlohmann::json j)
{
	std::string schemaError;
	if (!SaveSchema::UpgradeLevelDocument(j, schemaError)) {
		LOG_WARN("Save") << "拒绝加载关卡快照: " << schemaError;
		return false;
	}
	// 旧 3-1~3-9 存档使用五行或上移 40px 的泳池坐标；保留文件但拒绝加载，
	// 避免绝对 Y 入档的清洁车、子弹等对象与新网格错层。
	if (board->mLevel >= 19 && board->mLevel <= 27
		&& j.value("poolGridVersion", 0) != kPoolGridSaveVersion) {
		LOG_WARN("Save") << "忽略旧版泳池坐标存档";
		return false;
	}

	board->mIsLoadSave = true;		// 读档标记

	// 恢复 Board 状态
	board->mBoardState = static_cast<BoardState>(j.value("boardState", static_cast<int>(BoardState::GAME)));
	board->mIsSurvival = j.value("isSurvival", false);
	board->mHxyModeEnabled = j.value("hxyModeEnabled", false);
	board->mSurvivalRound = j.value("survivalRound", 1);
	if (board->mIsSurvival) {
		if (j.contains("perks")) board->GetPerkManager().Load(j["perks"]);   // 旧档无 perks 字段→天然兼容
		board->mPlantDamageEchoHitCounter = std::clamp(
			j.value("plantDamageEchoHitCounter", 0), 0, 9);

		// 还原冻结的出怪池（防刷怪）：存档若带 spawnList 字段则按当前无尽候选资格校验并去重，
		// 同时挡住手改/损坏档的越界 ZombieType，也不让旧档恢复已明确禁入的候选；旧档无此
		// 字段→回退到按轮次重建（旧行为，天然兼容），重建后下次存档即写入字段冻结。
		if (j.contains("spawnList") && j["spawnList"].is_array() && !j["spawnList"].empty()) {
			std::vector<ZombieType> list;
			for (auto& v : j["spawnList"]) {
				int val = v.get<int>();
				if (val < 0 || val >= static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)) continue;
				const ZombieType type = static_cast<ZombieType>(val);
				if (!board->CanZombieTypeEnterSurvivalPool(type, board->mSurvivalRound)) continue;
				bool dup = false;
				for (ZombieType seen : list)
					if (static_cast<int>(seen) == val) { dup = true; break; }
				if (dup) continue;
				list.push_back(type);
			}
			if (!list.empty()) board->SetZombieSpawnList(list);                   // 还原冻结池
			else board->BuildSurvivalSpawnList(board->mSurvivalRound);            // 全损坏→兜底重建
		} else {
			board->BuildSurvivalSpawnList(board->mSurvivalRound);                // 旧档/无字段→旧行为
		}
		board->UpdateSurvivalLevelName();
		// 轮间（CHOOSE_CARD）读档：Board 构造时按第1轮建的预览僵尸阵容是错的，
		// 此刻 survivalRound/出怪表已恢复，销毁旧预览并按正确轮次重建。
		// （GAME 状态读档不处理：OnEnter 会走 StartGame 销毁预览）
		if (board->mBoardState == BoardState::CHOOSE_CARD) {
			board->DestroyPreviewZombies();
			board->CreatePreviewZombies();
		}
	}
	board->mSun = j.value("sun", 50);
	board->mSunCountDown = std::clamp(
		j.value("sunCountDown", 5.0f), 0.0f, SPAWN_SUN_TIME);
	board->mPoolSunCountDown = std::clamp(
		j.value("poolSunCountDown", POOL_SUN_SPAWN_TIME),
		0.0f, POOL_SUN_SPAWN_TIME);
	board->mCurrentWave = j.value("currentWave", 0);
	board->mBoardFrame = j.value("boardFrame", 0);
	{
		const float iceRight = board->GetIceTrailRightX();
		board->mIceMinX.fill(iceRight);
		board->mIceTimer.fill(0.0f);
		const auto& trails = j.value("iceTrails", nlohmann::json::array());
		for (int row = 0; row < board->mRows
			&& row < static_cast<int>(board->mIceTimer.size())
			&& row < static_cast<int>(trails.size()); ++row) {
			if (!trails[row].is_object()) continue;
			board->mIceTimer[row] = std::clamp(
				trails[row].value("timer", 0.0f), 0.0f, 30.0f);
			board->mIceMinX[row] = board->mIceTimer[row] > 0.0f
				? std::clamp(trails[row].value("minX", iceRight), 25.0f, iceRight)
				: iceRight;
		}
		board->mGoldenIceMinX.fill(iceRight);
		board->mGoldenIceTimer.fill(0.0f);
		const auto& goldenTrails = j.value("goldenIceTrails", nlohmann::json::array());
		for (int row = 0; row < board->mRows
			&& row < static_cast<int>(board->mGoldenIceTimer.size())
			&& row < static_cast<int>(goldenTrails.size()); ++row) {
			if (!goldenTrails[row].is_object()) continue;
			board->mGoldenIceTimer[row] = std::clamp(
				goldenTrails[row].value("timer", 0.0f), 0.0f, 30.0f);
			board->mGoldenIceMinX[row] = board->mGoldenIceTimer[row] > 0.0f
				? std::clamp(goldenTrails[row].value("minX", iceRight), 25.0f, iceRight)
				: iceRight;
		}
	}
	// 历史漏计档即使已有累计字段，也不能低于场上本体及尚未变身模仿者占用的次数。
	int savedEliteCount = 0;
	for (const auto& plantData : j.value("plants", nlohmann::json::array())) {
		int placementType = plantData.value("type", -1);
		if (placementType == static_cast<int>(PlantType::PLANT_IMITATER)
			&& plantData.contains("extraData")) {
			placementType = plantData["extraData"].value("targetType", -1);
		}
		if (placementType == static_cast<int>(PlantType::PLANT_ELITE_SCAREDYSHROOM)) {
			++savedEliteCount;
		}
	}
	board->mEliteScaredyShroomsPlanted = std::clamp(
		std::max(j.value("eliteScaredyShroomsPlanted", 0), savedEliteCount),
		0, board->GetEliteScaredyShroomPlantLimit());
	board->mMistFuelDropAccumulator = std::clamp(
		j.value("mistFuelDropAccumulator", 0.0f), 0.0f, 1.0f);
	board->mMistFuelAssignedThisWave = 0;
	board->mActivePlanternID = NULL_PLANT_ID;
	board->mWeatherInitialized = j.value("weatherInitialized", j.contains("rainIntensity"));
	const int rainValue = j.value("rainIntensity", static_cast<int>(RainIntensity::CLEAR));
	board->mRainIntensity = (rainValue >= static_cast<int>(RainIntensity::CLEAR)
		&& rainValue <= static_cast<int>(RainIntensity::HEAVY))
		? static_cast<RainIntensity>(rainValue) : RainIntensity::CLEAR;
	const int previousRainValue = j.value("previousRainIntensity", rainValue);
	const bool validPreviousRain = previousRainValue >= static_cast<int>(RainIntensity::CLEAR)
		&& previousRainValue <= static_cast<int>(RainIntensity::HEAVY);
	// 旧档没有过渡字段时直接落在目标天气；损坏枚举也按目标天气稳定恢复。
	board->RestoreWeatherTransition(
		validPreviousRain ? static_cast<RainIntensity>(previousRainValue) : board->mRainIntensity,
		validPreviousRain ? j.value("weatherTransitionTimer", 0.0f) : 0.0f);
	const int forecastRainValue = j.value("forecastRainIntensity",
		static_cast<int>(RainIntensity::CLEAR));
	const bool validForecastRain = forecastRainValue >= static_cast<int>(RainIntensity::CLEAR)
		&& forecastRainValue <= static_cast<int>(RainIntensity::HEAVY);
	const int actualForecastRainValue = j.value("actualForecastRainIntensity",
		static_cast<int>(RainIntensity::CLEAR));
	const bool validActualForecastRain = actualForecastRainValue >= static_cast<int>(RainIntensity::CLEAR)
		&& actualForecastRainValue <= static_cast<int>(RainIntensity::HEAVY);
	board->mWeatherForecastReady = j.value("weatherForecastReady", false)
		&& j.contains("actualForecastRainIntensity")
		&& validForecastRain && validActualForecastRain;
	board->mForecastRainIntensity = board->mWeatherForecastReady
		? static_cast<RainIntensity>(forecastRainValue) : RainIntensity::CLEAR;
	board->mActualForecastRainIntensity = board->mWeatherForecastReady
		? static_cast<RainIntensity>(actualForecastRainValue) : RainIntensity::CLEAR;
	board->mWeatherForecastDisrupted = board->mWeatherForecastReady
		&& j.value("weatherForecastDisrupted", false);
	// 旧档缺字段时没有整栏黑障；损坏值夹紧到与运行时叠加相同的剩余时长上限。
	board->mWeatherPanelInterferenceTimer = board->SupportsWeatherPanelInterference()
		? std::clamp(j.value("weatherPanelInterferenceTimer", 0.0f), 0.0f,
			board->GetMaximumWeatherPanelInterferenceDuration())
		: 0.0f;
	const int coldWavePhaseValue = j.value("coldWavePhase",
		static_cast<int>(ColdWavePhase::CALM));
	const bool validColdWavePhase = coldWavePhaseValue >= static_cast<int>(ColdWavePhase::CALM)
		&& coldWavePhaseValue <= static_cast<int>(ColdWavePhase::THAWING);
	const int coldWaveStrengthValue = j.value("coldWaveStrength",
		static_cast<int>(ColdWaveStrength::STRONG));
	const bool validColdWaveStrength = coldWaveStrengthValue
		>= static_cast<int>(ColdWaveStrength::WEAK)
		&& coldWaveStrengthValue <= static_cast<int>(ColdWaveStrength::STRONG);
	board->mWinterTemperatureInitialized = board->SupportsWinterTemperature()
		&& validColdWavePhase && validColdWaveStrength
		&& j.value("winterTemperatureInitialized", false);
	// v7 以前的关卡档把开幕寒潮视为尚未消费；恢复后的 StartGame 会从 12 秒倒计时重新排入。
	board->mOpeningColdWavePlanInitialized = board->SupportsWinterTemperature()
		&& j.value("openingColdWavePlanInitialized", false);
	board->mColdWavePhase = board->mWinterTemperatureInitialized
		? static_cast<ColdWavePhase>(coldWavePhaseValue) : ColdWavePhase::CALM;
	board->mColdWaveStrength = board->mWinterTemperatureInitialized
		? static_cast<ColdWaveStrength>(coldWaveStrengthValue) : ColdWaveStrength::STRONG;
	board->mColdWaveTimer = board->mWinterTemperatureInitialized
		? std::max(0.0f, j.value("coldWaveTimer", 0.0f)) : 0.0f;
	board->mAmbientTemperatureC = board->mWinterTemperatureInitialized
		? std::clamp(j.value("ambientTemperatureC", 6.0f), -12.0f, 6.0f)
		: 6.0f;
	// 旧档缺少计划字段时沿用此前 -12°C/20s/32s 行为，平稳结束当前一轮后再进入新随机规则。
	board->mColdWaveTargetTemperatureC = board->mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveTargetTemperatureC", -12.0f), -12.0f, 0.0f)
		: -12.0f;
	board->mColdWaveCoolingDuration = board->mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveCoolingDuration", 20.0f), 1.0f, 120.0f)
		: 20.0f;
	board->mColdWaveHoldDuration = board->mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveHoldDuration", 57.5f), 1.0f, 180.0f)
		: 57.5f;
	board->mColdWaveThawDuration = board->mWinterTemperatureInitialized
		? std::clamp(j.value("coldWaveThawDuration", 32.0f), 1.0f, 120.0f)
		: 32.0f;
	board->mColdWaveForecastDisrupted = board->mWinterTemperatureInitialized
		&& board->mColdWavePhase == ColdWavePhase::CALM
		&& j.value("coldWaveForecastDisrupted", false);
	board->mWinterFrostVariant = board->mWinterTemperatureInitialized
		? std::clamp(j.value("winterFrostVariant", 0), 0, 2) : 0;
	if (board->IsMineBackground() && j.contains("mine") && j["mine"].is_object()) {
		const auto& mine = j["mine"];
		board->mMineFogElapsed = std::clamp(mine.value("fogElapsed",-1.0f),-1.0f,60.0f);
		board->mMineFogNextWave = std::max(10,mine.value("fogNextWave",10));
		board->mMineFogTutorialSeen = mine.value("fogTutorialSeen",false);
		board->mMineFogNoticeRemaining = std::clamp(mine.value("fogNotice",0.0f),0.0f,8.0f);
		if (mine.contains("rocks") && mine["rocks"].is_array() && mine["rocks"].size() == MineGrid::Count) {
			for (int cell = 0; cell < MineGrid::Count; ++cell) {
				if (mine["rocks"][cell].is_boolean())
					board->mMineGrid.rock[cell] = board->mMineGrid.rock[cell] && mine["rocks"][cell].get<bool>();
			}
		}
		board->mMineGrid.Rebuild();
		board->mMineDigCell = mine.value("digCell", -1);
		board->mMineDigRemaining = std::clamp(mine.value("digRemaining", 0.0f), 0.0f, 8.0f);
		if (board->mMineDigCell < 0 || board->mMineDigCell >= MineGrid::Count
			|| !board->mMineGrid.CanExcavate(board->mMineDigCell / MineGrid::Columns, board->mMineDigCell % MineGrid::Columns)
			|| !std::isfinite(board->mMineDigRemaining)) {
			board->mMineDigCell = -1;
			board->mMineDigRemaining = 0.0f;
		}
		board->mMineTutorialSeen = mine.value("tutorialSeen", false);
		board->mMinePlannedWave = mine.value("plannedWave", -1);
		board->mMineWavePlan.clear();
		if (mine.contains("wavePlan") && mine["wavePlan"].is_array()) {
			for (const auto& entry : mine["wavePlan"]) {
				if (!entry.is_array() || entry.size() != 2 || !entry[0].is_number_integer() || !entry[1].is_number_integer()) continue;
				const int type = entry[0].get<int>(), row = entry[1].get<int>();
				if (type < 0 || type >= static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)
					|| row < 0 || row >= MineGrid::Rows || !board->mMineGrid.entrance[row]) continue;
				board->mMineWavePlan.emplace_back(static_cast<ZombieType>(type), row);
			}
		}
	}
	const int polarPhaseValue = j.value("polarNightPhase",
		static_cast<int>(PolarNightPhase::DORMANT));
	const bool validPolarPhase = polarPhaseValue
		>= static_cast<int>(PolarNightPhase::DORMANT)
		&& polarPhaseValue <= static_cast<int>(PolarNightPhase::FADE);
	const int verticalWindValue = j.value("polarVerticalWindDirection",
		static_cast<int>(VerticalWindDirection::NONE));
	const bool validVerticalWind = verticalWindValue
		>= static_cast<int>(VerticalWindDirection::NONE)
		&& verticalWindValue <= static_cast<int>(VerticalWindDirection::DOWN);
	board->mPolarNightInitialized = board->SupportsPolarNightEnvironment()
		&& validPolarPhase && validVerticalWind
		&& j.value("polarNightInitialized", false);
	board->mSnowHoles.fill({});
	board->mPendingSnowHoleSpawns.clear();
	board->mPolarWindParticleTimer = 0.0f;
	board->mPolarWindVisualStrength = 0.0f;
	if (board->mPolarNightInitialized) {
		board->mPolarNightPhase = static_cast<PolarNightPhase>(polarPhaseValue);
		board->mPolarPlanIsWhiteout = j.value("polarPlanIsWhiteout", false);
		board->mPolarLastPlanWasFalse = j.value("polarLastPlanWasFalse", false);
		board->mPolarFirstWhiteoutCompleted = j.value(
			"polarFirstWhiteoutCompleted", false);
		board->mPolarDangerMask = std::clamp(j.value("polarDangerMask", 0), 0, 7);
		board->mPolarTemperatureC = std::clamp(
			j.value("polarTemperatureC", -14.0f), -25.0f, -2.0f);
		board->mPolarHumidityPercent = std::clamp(
			j.value("polarHumidityPercent", 58.0f), 0.0f, 100.0f);
		board->mPolarWindSpeedMps = std::clamp(
			j.value("polarWindSpeedMps", 8.0f), 0.0f, 30.0f);
		board->mPolarStartTemperatureC = std::clamp(
			j.value("polarStartTemperatureC", -14.0f), -25.0f, -2.0f);
		board->mPolarStartHumidityPercent = std::clamp(
			j.value("polarStartHumidityPercent", 58.0f), 0.0f, 100.0f);
		board->mPolarStartWindSpeedMps = std::clamp(
			j.value("polarStartWindSpeedMps", 8.0f), 0.0f, 30.0f);
		board->mPolarTargetTemperatureC = std::clamp(
			j.value("polarTargetTemperatureC", -14.0f), -25.0f, -2.0f);
		board->mPolarTargetHumidityPercent = std::clamp(
			j.value("polarTargetHumidityPercent", 58.0f), 0.0f, 100.0f);
		board->mPolarTargetWindSpeedMps = std::clamp(
			j.value("polarTargetWindSpeedMps", 8.0f), 0.0f, 30.0f);
		board->mPolarPhaseTimer = std::clamp(
			j.value("polarPhaseTimer", 0.0f), 0.0f, 3600.0f);
		board->mPolarPhaseDuration = std::clamp(
			j.value("polarPhaseDuration", 0.0f), 0.0f, 3600.0f);
		board->mPolarAllDangerTimer = std::clamp(
			j.value("polarAllDangerTimer", 0.0f), 0.0f, 5.0f);
		board->mPolarHighHumidityTimer = std::clamp(
			j.value("polarHighHumidityTimer", 0.0f), 0.0f, 3.0f);
		board->mPolarHumidityEpisodeConsumed = j.value(
			"polarHumidityEpisodeConsumed", false);
		board->mPolarTutorialHoleBatchConsumed = j.value(
			"polarTutorialHoleBatchConsumed", false);
		board->mPolarVerticalWindDirection = static_cast<VerticalWindDirection>(
			verticalWindValue);
		board->mPolarWhiteoutTimer = std::clamp(
			j.value("polarWhiteoutTimer", 0.0f), 0.0f, 60.0f);
		board->mPolarFluctuationDuration = std::clamp(
			j.value("polarFluctuationDuration", 0.0f), 0.0f, 10.0f);
		board->mPolarFluctuationTimer = std::clamp(
			j.value("polarFluctuationTimer", 0.0f), 0.0f,
			board->mPolarFluctuationDuration);
		// v8 早期档缺少微波动标记；把淡出阶段规范成当前五秒回落日程，已有基线值则稳定保持。
		if (board->mPolarNightPhase == PolarNightPhase::FADE
			&& !j.contains("polarFluctuationDuration")) {
			board->mPolarStartTemperatureC = board->mPolarTemperatureC;
			board->mPolarStartHumidityPercent = board->mPolarHumidityPercent;
			board->mPolarStartWindSpeedMps = board->mPolarWindSpeedMps;
			board->mPolarTargetTemperatureC = -14.0f;
			board->mPolarTargetHumidityPercent = 58.0f;
			board->mPolarTargetWindSpeedMps = 8.0f;
			board->mPolarPhaseTimer = 0.0f;
			board->mPolarPhaseDuration = 5.0f;
		}
		board->mPolarFinalWaveUpgradeApplied = j.value(
			"polarFinalWaveUpgradeApplied", false);
		const auto& holes = j.value("snowHoles", nlohmann::json::array());
		for (int row = 0; row < static_cast<int>(board->mSnowHoles.size())
			&& row < static_cast<int>(holes.size()); ++row) {
			if (!holes[row].is_object()) continue;
			const int column = holes[row].value("column", -1);
			const int phase = holes[row].value("phase",
				static_cast<int>(SnowHolePhase::NONE));
			if (column < 4 || column > 6
				|| phase < static_cast<int>(SnowHolePhase::FORMING)
				|| phase > static_cast<int>(SnowHolePhase::ACTIVE)) continue;
			board->mSnowHoles[row] = {
				column, static_cast<SnowHolePhase>(phase),
				phase == static_cast<int>(SnowHolePhase::FORMING)
					? std::clamp(holes[row].value("timer", 0.0f), 0.0f, 2.0f)
					: 0.0f
			};
		}
		const auto& pendingSpawns = j.value(
			"pendingSnowHoleSpawns", nlohmann::json::array());
		for (const auto& pending : pendingSpawns) {
			if (!pending.is_object()) continue;
			const int type = pending.value("type",
				static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES));
			const int row = pending.value("row", -1);
			const int column = pending.value("holeColumn", -1);
			if (type < 0 || type >= static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)
				|| row < 0 || row >= board->mRows || column < 4 || column > 6) {
				continue;
			}
			board->mPendingSnowHoleSpawns.push_back({
				static_cast<ZombieType>(type), row, column,
				std::clamp(pending.value("spawnWave", 0), 0, 10000),
				std::clamp(pending.value("timer", 0.0f), 0.0f, 1.0f),
				pending.value("tutorialSnowBurrow", false),
			});
		}
	}
	else {
		board->mPolarNightPhase = PolarNightPhase::DORMANT;
		board->mPolarVerticalWindDirection = VerticalWindDirection::NONE;
		board->mPolarFluctuationTimer = 0.0f;
		board->mPolarFluctuationDuration = 0.0f;
	}
	board->mPendingAuroraRifts.clear();
	board->mEchoWaves.clear();
	if (j.contains("echoWaves") && j["echoWaves"].is_array()) for (const auto& saved : j["echoWaves"]) {
		if (!saved.is_object()) continue;
		Board::EchoWave wave;
		wave.row = saved.value("row",0); wave.column = saved.value("column",0);
		wave.elapsed = saved.value("elapsed",0.0f);
		wave.hitIceWall = saved.value("hitIceWall",false);
		wave.distances = saved.value("distances",std::vector<int>{});
		wave.hitIDs = saved.value("hitIDs",std::vector<int>{});
		if (!std::isfinite(wave.elapsed) || wave.elapsed < 0 || wave.elapsed > 1.55f
			|| wave.row < 0 || wave.row >= board->mRows || wave.column < 0 || wave.column >= board->mColumns
			|| wave.distances.size() != static_cast<size_t>(board->mRows * board->mColumns)
			|| std::any_of(wave.distances.begin(),wave.distances.end(),[](int d){return d < -1 || d > 6;})) continue;
		board->mEchoWaves.push_back(std::move(wave));
	}
	board->mTemporalAnchors.clear();
	board->mNextDiscontinuousTransactionID = std::max(1,
		j.value("nextDiscontinuousTransactionID", 1));
	board->mDawnNavigationTimer = board->SupportsPolarNightEnvironment()
		? std::clamp(j.value("dawnNavigationTimer", 0.0f), 0.0f, 8.0f) : 0.0f;
	// 裂隙和时间锚是僵尸提交的独立事务，任何地图读档都必须恢复。
	{
		for (const auto& saved : j.value("pendingAuroraRifts", nlohmann::json::array())) {
			if (!saved.is_object()) continue;
			const int type = saved.value("type", -1);
			const int row = saved.value("row", -1);
			const int column = saved.value("column", -1);
			if (type < 0 || type >= static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)
				|| row < 0 || row >= board->mRows || column < 2 || column > 6) continue;
			board->mPendingAuroraRifts.push_back({ static_cast<ZombieType>(type),
				row, column, std::max(0, saved.value("spawnWave", 0)),
				std::clamp(saved.value("timer", 0.0f), 0.0f, 0.8f),
				std::max(1, saved.value("transactionID", 1)),
				saved.value("ownerZombieID", -1) });
		}
		for (const auto& savedAnchor : j.value("temporalAnchors", nlohmann::json::array())) {
			if (!savedAnchor.is_object() || board->mTemporalAnchors.size() >= 2) continue;
			Board::TemporalAnchor anchor;
			anchor.ownerZombieID = savedAnchor.value("ownerZombieID", -1);
			anchor.timer = std::clamp(savedAnchor.value("timer", 0.0f), 0.0f, 6.0f);
			for (const auto& savedTarget : savedAnchor.value(
				"targets", nlohmann::json::array())) {
				if (!savedTarget.is_object() || anchor.targets.size() >= 12) break;
				const int type = savedTarget.value("type", -1);
				const int row = savedTarget.value("row", -1);
				const int id = savedTarget.value("zombieID", -1);
				if (type < 0 || type >= static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)
					|| row < 0 || row >= board->mRows || id <= 0) continue;
				Board::TemporalTargetSnapshot target;
				target.zombieID = id;
				target.type = static_cast<ZombieType>(type);
				target.row = row;
				target.x = std::clamp(savedTarget.value("x", 1100.0f), -200.0f, 1800.0f);
				target.mineRowOffset = std::clamp(savedTarget.value("mineRowOffset", 0.0f),
					-CELL_COLLIDER_SIZE_Y * 0.5f, CELL_COLLIDER_SIZE_Y * 0.5f);
				target.mineTargetCell = std::clamp(savedTarget.value("mineTargetCell", -1), -1, MineGrid::Count - 1);
				target.bodyHealth = std::max(1, savedTarget.value("bodyHealth", 1));
				target.helmType = static_cast<HelmType>(std::clamp(savedTarget.value(
					"helmType", static_cast<int>(HelmType::HELMTYPE_NONE)),
					static_cast<int>(HelmType::HELMTYPE_NONE),
					static_cast<int>(HelmType::HELMTYPE_CRYSTAL_HORN)));
				target.helmHealth = std::max(0, savedTarget.value("helmHealth", 0));
				target.shieldType = static_cast<ShieldType>(std::clamp(savedTarget.value(
					"shieldType", static_cast<int>(ShieldType::SHIELDTYPE_NONE)),
					static_cast<int>(ShieldType::SHIELDTYPE_NONE),
					static_cast<int>(ShieldType::SHIELDTYPE_LADDER)));
				target.shieldHealth = std::max(0, savedTarget.value("shieldHealth", 0));
				target.slowTimer = std::max(0.0f, savedTarget.value("slowTimer", 0.0f));
				target.frozenTimer = std::max(0.0f, savedTarget.value("frozenTimer", 0.0f));
				target.butterTimer = std::max(0.0f, savedTarget.value("butterTimer", 0.0f));
				target.paralysisTimer = std::max(0.0f, savedTarget.value("paralysisTimer", 0.0f));
				target.hasHead = savedTarget.value("hasHead", true);
				target.hasArm = savedTarget.value("hasArm", true);
				target.irreversible = savedTarget.value("irreversible", false);
				target.restoreHelm = savedTarget.value("restoreHelm", true);
				target.restoreShield = savedTarget.value("restoreShield", true);
				target.specialActionSubmitted = savedTarget.value(
					"specialActionSubmitted", false);
				target.abilityStateValid = savedTarget.value("abilityStateValid", false);
				target.abilityReleaseCount = std::max(0, savedTarget.value("abilityReleaseCount", 0));
				// phase 是由具体僵尸解释的不透明编码；适应头盔会在其中携带完整植物谱系。
				target.abilityPhase = std::clamp(savedTarget.value("abilityPhase", -1), -1,
					static_cast<int>(PlantType::NUM_PLANT_TYPES) + 1);
				target.abilityRemaining = std::clamp(savedTarget.value(
					"abilityRemaining", 0.0f), 0.0f, 10.0f);
				anchor.targets.push_back(target);
			}
			if (!anchor.targets.empty()) board->mTemporalAnchors.push_back(std::move(anchor));
		}
	}
	if (auto* presentation = board->GetPresentation()) {
		// 缺字段的旧档按 0 秒恢复，避免读入雨中存档时把已消失的展板重新显示 5 秒。
		const int failedForecastRainValue = j.value("failedForecastRainIntensity",
			static_cast<int>(RainIntensity::CLEAR));
		const int failureActualRainValue = j.value("weatherForecastFailureActualIntensity",
			static_cast<int>(RainIntensity::CLEAR));
		const int failedForecastTyphoonValue = j.value("failedForecastTyphoonStrength",
			static_cast<int>(TyphoonStrength::NONE));
		const int failureActualTyphoonValue = j.value(
			"weatherForecastFailureActualTyphoonStrength",
			static_cast<int>(TyphoonStrength::NONE));
		const bool validFailedForecastRain = failedForecastRainValue >= static_cast<int>(RainIntensity::CLEAR)
			&& failedForecastRainValue <= static_cast<int>(RainIntensity::HEAVY);
		const bool validFailureActualRain = failureActualRainValue >= static_cast<int>(RainIntensity::CLEAR)
			&& failureActualRainValue <= static_cast<int>(RainIntensity::HEAVY);
		const bool validFailedForecastTyphoon = failedForecastTyphoonValue
			>= static_cast<int>(TyphoonStrength::NONE)
			&& failedForecastTyphoonValue <= static_cast<int>(TyphoonStrength::SUPER);
		const bool validFailureActualTyphoon = failureActualTyphoonValue
			>= static_cast<int>(TyphoonStrength::NONE)
			&& failureActualTyphoonValue <= static_cast<int>(TyphoonStrength::SUPER);
		// 旧档或损坏字段按 0 秒恢复，已经消失的失败提示不会在读档后重播。
		presentation->RestoreWeatherPresentationState(WeatherPresentationState{
			j.value("currentWeatherNoticeTimer", 0.0f),
			validFailedForecastRain && validFailureActualRain
				&& validFailedForecastTyphoon && validFailureActualTyphoon
				? j.value("weatherForecastFailureTimer", 0.0f) : 0.0f,
			validFailedForecastRain
				? static_cast<RainIntensity>(failedForecastRainValue)
				: RainIntensity::CLEAR,
			validFailureActualRain
				? static_cast<RainIntensity>(failureActualRainValue)
				: RainIntensity::CLEAR,
			validFailedForecastTyphoon
				&& failedForecastRainValue == static_cast<int>(RainIntensity::HEAVY)
				? static_cast<TyphoonStrength>(failedForecastTyphoonValue)
				: TyphoonStrength::NONE,
			validFailureActualTyphoon
				&& failureActualRainValue == static_cast<int>(RainIntensity::HEAVY)
				? static_cast<TyphoonStrength>(failureActualTyphoonValue)
				: TyphoonStrength::NONE
		});
	}
	board->mWeatherTimer = std::max(0.0f, j.value("weatherTimer", 0.0f));
	board->mLightningTimer = std::max(0.0f, j.value("lightningTimer", 0.0f));
	// 暴风雨夜由关卡与波次派生。旧档不带初始化字段时交给 StartGame 补齐一次；
	// 新档保存闪光节奏和计时，避免读档重放入场闪电或返还一次性阵风。
	board->mStormyNightInitialized = board->IsStormyNightActive()
		&& j.value("stormyNightInitialized", false);
	if (board->mStormyNightInitialized) {
		board->mStormyNightFlashPattern = std::clamp(
			j.value("stormyNightFlashPattern", 2), 1, 3);
		board->mStormyNightFlashTimer = std::clamp(
			j.value("stormyNightFlashTimer", 1.5f), 0.0f, 12.0f);
	}
	else {
		board->mStormyNightFlashPattern = 0;
		board->mStormyNightFlashTimer = 0.0f;
	}
	const int roofRunoffPhaseValue = j.value("roofRunoffPhase",
		static_cast<int>(RoofRunoffPhase::IDLE));
	const RoofRunoffPhase roofRunoffPhase = roofRunoffPhaseValue
		>= static_cast<int>(RoofRunoffPhase::IDLE)
		&& roofRunoffPhaseValue <= static_cast<int>(RoofRunoffPhase::FLOWING)
		? static_cast<RoofRunoffPhase>(roofRunoffPhaseValue)
		: RoofRunoffPhase::IDLE;
	// 锁定行组和倒计时会影响后续冲刷；旧档缺字段时从空积累开始，不伪造一次事件。
	board->RestoreRoofRunoffState(j.value("roofRunoffCharge", 0.0f),
		roofRunoffPhase, j.value("roofRunoffRowMask", 0),
		j.value("roofRunoffPhaseTimer", 0.0f),
		j.value("roofRunoffRetainedCharge", 30.0f));
	const int nightRoofChargePhaseValue = j.value("nightRoofChargePhase",
		static_cast<int>(NightRoofChargePhase::CHARGING));
	const NightRoofChargePhase nightRoofChargePhase = nightRoofChargePhaseValue
		>= static_cast<int>(NightRoofChargePhase::CHARGING)
		&& nightRoofChargePhaseValue <= static_cast<int>(NightRoofChargePhase::DISCHARGING)
		? static_cast<NightRoofChargePhase>(nightRoofChargePhaseValue)
		: NightRoofChargePhase::CHARGING;
	// 旧档缺雷荷字段时从空积累开始；活动阶段沿用已锁定路线、倒计时和余电，不重新抽取。
	board->RestoreNightRoofChargeState(j.value("nightRoofCharge", 0.0f),
		nightRoofChargePhase, j.value("nightRoofChargeRow", -1),
		j.value("nightRoofChargePhaseTimer", 0.0f),
		j.value("nightRoofOvercharge", 0.0f),
		j.value("nightRoofHijackerSelectionAttempted", false),
		j.value("nightRoofHijackerID", NULL_ZOMBIE_ID),
		j.value("nightRoofHijackerWarningExtended", false),
		j.value("nightRoofHijackerFinalizing", false),
		j.value("nightRoofChargeGuided", false),
		j.value("nightRoofChargeGuideID", NULL_ZOMBIE_ID));
	// 旧版天气存档没有该字段时按 false：少一次增强机会比读档后凭空再增强更稳妥。
	board->mRainCanIntensify = board->mRainIntensity == RainIntensity::LIGHT
		&& j.value("rainCanIntensify", false);
	// 旧档没有续期字段时按已消费处理；小雨永远不开放同档续期。
	board->mRainCanHold = (board->mRainIntensity == RainIntensity::MEDIUM
		|| board->mRainIntensity == RainIntensity::HEAVY)
		&& j.value("rainCanHold", false);
	if (j.contains("fogWeatherIntensity")) {
		const int fogWeatherValue = j.value("fogWeatherIntensity",
			static_cast<int>(FogWeatherIntensity::DEFAULT));
		const int forecastFogWeatherValue = j.value("forecastFogWeatherIntensity",
			static_cast<int>(FogWeatherIntensity::DEFAULT));
		const int actualForecastFogWeatherValue = j.value("actualForecastFogWeatherIntensity",
			static_cast<int>(FogWeatherIntensity::DEFAULT));
		const bool validFogWeather = fogWeatherValue
			>= static_cast<int>(FogWeatherIntensity::DEFAULT)
			&& fogWeatherValue <= static_cast<int>(FogWeatherIntensity::DENSE);
		const bool validForecastFogWeather = forecastFogWeatherValue
			>= static_cast<int>(FogWeatherIntensity::DEFAULT)
			&& forecastFogWeatherValue <= static_cast<int>(FogWeatherIntensity::DENSE);
		const bool validActualForecastFogWeather = actualForecastFogWeatherValue
			>= static_cast<int>(FogWeatherIntensity::DEFAULT)
			&& actualForecastFogWeatherValue <= static_cast<int>(FogWeatherIntensity::DENSE);
		const bool fogForecastReady = j.value("fogWeatherForecastReady", false)
			&& j.contains("actualForecastFogWeatherIntensity")
			&& validForecastFogWeather && validActualForecastFogWeather;
		// 关卡基准由当前进度派生；这里只恢复雾势、未来抽取和台风驱散结果。
		board->RestoreFogState(
			j.value("fogWeatherInitialized", true),
			validFogWeather
				? static_cast<FogWeatherIntensity>(fogWeatherValue)
				: FogWeatherIntensity::DEFAULT,
			fogForecastReady
				? static_cast<FogWeatherIntensity>(forecastFogWeatherValue)
				: FogWeatherIntensity::DEFAULT,
			fogForecastReady
				? static_cast<FogWeatherIntensity>(actualForecastFogWeatherValue)
				: FogWeatherIntensity::DEFAULT,
			j.value("fogWeatherTimer", 0.0f), fogForecastReady,
			j.value("fogDispersal", 0.0f), j.value("fogVisualOffsetX", 0.0f));
		board->mFogWeatherForecastDisrupted = fogForecastReady
			&& j.value("fogWeatherForecastDisrupted", false);
	}
	const int pendingTyphoonValue = j.value("pendingHeavyTyphoonStrength",
		static_cast<int>(TyphoonStrength::NONE));
	const int pendingWindValue = j.value("pendingHeavyWindDirection",
		static_cast<int>(WindDirection::NONE));
	const TyphoonStrength pendingTyphoonStrength =
		pendingTyphoonValue >= static_cast<int>(TyphoonStrength::NONE)
		&& pendingTyphoonValue <= static_cast<int>(TyphoonStrength::SUPER)
		? static_cast<TyphoonStrength>(pendingTyphoonValue) : TyphoonStrength::NONE;
	const WindDirection pendingWindDirection =
		pendingWindValue >= static_cast<int>(WindDirection::NONE)
		&& pendingWindValue <= static_cast<int>(WindDirection::TOWARD_FRONT)
		? static_cast<WindDirection>(pendingWindValue) : WindDirection::NONE;
	// 公开大雨警报等级或真实新大雨台风初态必须原样恢复；旧档缺字段时由后续 Update 补抽一次。
	board->RestorePendingHeavyTyphoon(
		j.value("pendingHeavyTyphoonPrepared", false),
		j.value("pendingHeavyTyphoonOpeningProtected", false),
		pendingTyphoonStrength, pendingWindDirection,
		j.value("pendingHeavyTyphoonStrengthTimer", 0.0f),
		j.value("pendingHeavyWindGustTimer", 0.0f),
		j.value("pendingHeavyWindDirectionTimer", 0.0f),
		j.value("pendingHeavyTyphoonGustsRemaining", 0),
		j.value("pendingHeavyRainPromptVariant", 0));
	const int typhoonValue = j.value("typhoonStrength",
		static_cast<int>(TyphoonStrength::NONE));
	const int windDirectionValue = j.value("windDirection",
		static_cast<int>(WindDirection::NONE));
	const TyphoonStrength typhoonStrength = typhoonValue >= static_cast<int>(TyphoonStrength::NONE)
		&& typhoonValue <= static_cast<int>(TyphoonStrength::SUPER)
		? static_cast<TyphoonStrength>(typhoonValue) : TyphoonStrength::NONE;
	const WindDirection windDirection = windDirectionValue >= static_cast<int>(WindDirection::NONE)
		&& windDirectionValue <= static_cast<int>(WindDirection::TOWARD_FRONT)
		? static_cast<WindDirection>(windDirectionValue) : WindDirection::NONE;
	// 台风强度、风向和计时都是已经判定过的结果；旧档或损坏组合只退化为无台风，绝不重 roll。
	board->RestoreTyphoonState(typhoonStrength, windDirection,
		j.value("typhoonStrengthTimer", 0.0f), j.value("windGustTimer", 0.0f),
		j.value("windDirectionTimer", 0.0f),
		j.value("typhoonGustsRemaining", 0));
	const int activeGustStrengthValue = j.value("activeGustStrength",
		static_cast<int>(TyphoonStrength::NONE));
	const int activeGustDirectionValue = j.value("activeGustDirection",
		static_cast<int>(WindDirection::NONE));
	const TyphoonStrength activeGustStrength = activeGustStrengthValue
		>= static_cast<int>(TyphoonStrength::NONE)
		&& activeGustStrengthValue <= static_cast<int>(TyphoonStrength::SUPER)
		? static_cast<TyphoonStrength>(activeGustStrengthValue) : TyphoonStrength::NONE;
	const WindDirection activeGustDirection = activeGustDirectionValue
		>= static_cast<int>(WindDirection::NONE)
		&& activeGustDirectionValue <= static_cast<int>(WindDirection::TOWARD_FRONT)
		? static_cast<WindDirection>(activeGustDirectionValue) : WindDirection::NONE;
	// 活动阵风的锁定值、余时和植物结算标记必须入档，否则会中途停风或重复换格。
	board->RestoreActiveTyphoonGust(j.value("typhoonGustActive", false),
		activeGustStrength, activeGustDirection,
		j.value("activeGustDuration", 0.0f), j.value("activeGustTimer", 0.0f),
		j.value("activeGustPlantMoveTimer", 0.0f),
		j.value("activeGustPlantMoved", false));
	// 弱天气计数决定下一轮是否强制大雨；旧档从零开始，不凭空制造高压天气。
	board->RestoreWeakWeatherPity(j.value("weakWeatherPhasesSinceHeavy", 0));
	// 台风保底计数影响下一次大雨的概率，必须随档恢复；旧档默认从零开始。
	board->RestoreTyphoonPity(j.value("heavyPhasesWithoutTyphoon", 0));
	// 每波生成计数必须随当前波恢复；旧版单台风布尔字段按已生成 1 只迁移。
	const int legacyEliteDancerCount = j.value("eliteDancerSpawnedThisTyphoon", false) ? 1 : 0;
	board->RestoreEliteDancerWaveSpawnCount(
		j.value("eliteDancersSpawnedThisWave", legacyEliteDancerCount));
	board->RestoreReinforcedDoorWaveSpawnCount(
		j.value("reinforcedDoorsSpawnedThisWave", 0));
	board->RestoreElitePolevaulterWaveSpawnCount(
		j.value("elitePolevaultersSpawnedThisWave", 0));
	board->RestoreGildedZamboniWaveSpawnCount(
		j.value("gildedZambonisSpawnedThisWave", 0));
	board->RestoreEliteDolphinRiderWaveSpawnCount(
		j.value("eliteDolphinRidersSpawnedThisWave", 0));
	board->RestoreEliteJackInTheBoxWaveSpawnCount(
		j.value("eliteJackInTheBoxesSpawnedThisWave", 0));
	board->RestoreEliteDiggerWaveSpawnCount(
		j.value("eliteDiggersSpawnedThisWave", 0));
	board->RestoreElitePogoWaveSpawnCount(
		j.value("elitePogosSpawnedThisWave", 0));
	board->RestoreEliteLadderWaveSpawnCount(
		j.value("eliteLaddersSpawnedThisWave", 0));
	board->RestoreEliteCatapultWaveSpawnCount(
		j.value("eliteCatapultsSpawnedThisWave", 0));
	board->RestoreRedeyeGargantuarWaveSpawnCount(
		j.value("redeyeGargantuarsSpawnedThisWave", 0));
	board->RestoreInsulatorWaveSpawnCount(
		j.value("insulatorsSpawnedThisWave", 0));
	board->RestoreHijackerWaveSpawnCount(
		j.value("hijackersSpawnedThisWave", 0));
	board->RestoreHijackerSpawnCooldown(
		j.value("hijackerSpawnCooldownWavesRemaining", 0),
		j.value("hijackerSpawnBlockedThisWave", false));
	board->RestoreGroundingZombieWaveSpawnCount(
		j.value("groundingZombiesSpawnedThisWave", 0));
	board->RestoreBobsledTeamWaveSpawnCount(
		j.value("bobsledTeamsSpawnedThisWave", 0));
	board->RestoreIceWallEngineerWaveSpawnCount(
		j.value("iceWallEngineersSpawnedThisWave", 0));
	board->RestoreIceCrackDrillWaveSpawnCount(
		j.value("iceCrackDrillsSpawnedThisWave", 0));
	board->RestoreWeatherJammerWaveSpawnCount(
		j.value("weatherJammersSpawnedThisWave", 0));
	board->RestoreIceStatueExecutionerWaveSpawnCount(
		j.value("iceStatueExecutionersSpawnedThisWave", 0));
	board->RestoreSnowBurrowSpawnState(
		j.value("snowBurrowsSpawnedThisWave", 0),
		j.value("snowBurrowTutorialHoleSpawnConsumed", false));
	board->RestoreAdaptiveHelmetSpawnState(
		j.value("adaptiveHelmetsSpawnedThisWave", 0),
		j.value("adaptiveHelmetTutorialWaveSpawned", false));
	board->RestoreThermalSniperSpawnState(
		j.value("thermalSnipersSpawnedThisWave", 0),
		j.value("thermalSniperTutorialSpawned", false));
	board->mAuroraPriestsSpawnedThisWave = std::clamp(
		j.value("auroraPriestsSpawnedThisWave", 0), 0, 3);
	board->mClockmakersSpawnedThisWave = std::clamp(
		j.value("clockmakersSpawnedThisWave", 0), 0, 3);
	board->mCrystalMinersSpawnedThisWave = std::clamp(j.value("crystalMinersSpawnedThisWave",0),0,1);
	board->mAuroraPriestGuaranteeConsumed =
		j.value("auroraPriestGuaranteeConsumed", false);
	board->mClockmakerGuaranteeConsumed =
		j.value("clockmakerGuaranteeConsumed", false);
	board->mRainVisualActive = false;   // 粒子不入存档，StartGame 按剩余时间重建
	board->mMaxWave = j.value("maxWave", 10);
	board->mZombieCountDown = j.value("zombieCountDown", 20.0f);
	board->mTotalZombieHP = j.value("totalZombieHP", 0LL);
	board->mCurrectWaveZombieHP = j.value("currentWaveZombieHP", 0LL);
	board->mNextWaveSpawnZombieHP = j.value("nextWaveSpawnZombieHP", 0LL);

	// 恢复 EntityRegistry 的 ID 计数器（向后兼容：旧存档没有则使用默认值）
	board->mEntityRegistry.SetNextPlantID(j.value("nextPlantID", 1));
	board->mEntityRegistry.SetNextZombieID(j.value("nextZombieID", 1));
	board->mEntityRegistry.SetNextBulletID(j.value("nextBulletID", 1));
	board->mEntityRegistry.SetNextCoinID(j.value("nextCoinID", 1));
	board->mEntityRegistry.SetNextMowerID(j.value("nextMowerID", 1));

	// 压扁残影、离地倭瓜与后来补种的植物可以同格共存。先恢复并释放非占格实体，
	// 再恢复正常植物，避免无序存档数组令旧实体的创建过程覆盖同格新植物 ID。
	const auto savedPlants = j.value("plants", nlohmann::json::array());
	auto restorePlant = [&](const nlohmann::json& p) {
		PlantType type = static_cast<PlantType>(p["type"].get<int>());
		int row = p["row"].get<int>();
		int col = p["column"].get<int>();
		int health = p["health"].get<int>();
		int maxHealth = p["maxHealth"].get<int>();
		bool isSleeping = p["isSleeping"].get<bool>();
		int id = p.value("id", NULL_PLANT_ID);

		Plant* plant = nullptr;
		if (type == PlantType::PLANT_IMITATER && p.contains("extraData")) {
			const PlantType targetType = static_cast<PlantType>(
				p["extraData"].value("targetType",
					static_cast<int>(PlantType::NUM_PLANT_TYPES)));
			plant = id != NULL_PLANT_ID
				? board->CreateImitaterPlantWithID(targetType, row, col, id)
				: board->CreatePlantInternal(PlantType::PLANT_IMITATER, targetType,
					row, col, false, false, false);
		}
		else if (id != NULL_PLANT_ID) {
			plant = board->CreatePlantWithID(type, row, col, id);
		}
		else {
			plant = board->CreatePlant(type, row, col);
		}

		if (plant) {
			plant->mPlantHealth = health;
			plant->mPlantMaxHealth = maxHealth;
			// 原始状态恢复不得重播咖啡豆唤醒音效或品种激活反馈；旧档缺字段时为中性 0。
			plant->RestoreSleepState(isSleeping, p.value("wakeUpTimer", 0.0f));
			// 通用停机是实体快照状态；读档只恢复剩余时间，不重新结算来源技能。
			plant->RestoreShutdown(p.value("shutdownTimer", 0.0f));
			plant->RestoreUnyieldingRootsState(
				p.value("unyieldingRootsSpent", false),
				p.value("unyieldingRootsTimer", 0.0f));
			plant->RestoreIceSeal(
				p.value("iceSealOwnerZombieID", NULL_ZOMBIE_ID));
			RestoreAnimState(p, plant);
			if (p.contains("extraData")) {
				plant->LoadExtraData(p["extraData"]);
			}
			if (p.value("imitated", false)) {
				plant->SetImitatedAppearance(true);
			}
			// 派生类读档可能重播专属轨道；最后恢复压扁态，确保终态仍暂停且不占格。
			if (p.value("isSquished", false)) {
				const Vector fallbackVisual = plant->GetVisualPosition();
				plant->RestoreSquishState(
					p.value("squishTimer", 0.0f),
					Vector(p.value("squishVisualX", fallbackVisual.x),
						p.value("squishVisualY", fallbackVisual.y)));
			}
		}
	};
	for (const bool occupiesGridSlotPass : { false, true }) {
		for (const auto& p : savedPlants) {
			const bool occupiesGridSlot = p.value(
				"occupiesGridSlot", !p.value("isSquished", false));
			if (occupiesGridSlot == occupiesGridSlotPass) {
				restorePlant(p);
			}
		}
	}

	// 恢复小推车
	for (auto& m : j.value("mowers", nlohmann::json::array())) {
		MowerType type = static_cast<MowerType>(m["type"].get<int>());
		int row = m["row"].get<int>();
		float x = m["x"].get<float>();
		float y = m["y"].get<float>();
		int id = m.value("id", NULL_MOWER_ID);

		Mower* mower = nullptr;
		if (id != NULL_MOWER_ID) {
			mower = board->CreateMowerWithID(type, row, x, y, id);
		}
		else {
			mower = board->CreateMower(type, row);
		}

		if (mower) {
			mower->mState = static_cast<MowerState>(m.value("state", 0));
			mower->mSpeed = m.value("speed", 300.0f);
			// 开发期屋顶存档可能仍记录草车的 anim_normal；车型规范化后不可把旧轨道灌入 RoofCleaner。
			if (!(board->IsRoofBackground() && type != MowerType::ROOF)) {
				RestoreAnimState(m, mower);
			}
			const int heightValue = m.value("mowerHeight", static_cast<int>(MowerHeight::LAND));
			const MowerHeight height = heightValue >= static_cast<int>(MowerHeight::LAND)
				&& heightValue <= static_cast<int>(MowerHeight::EXITING)
				? static_cast<MowerHeight>(heightValue) : MowerHeight::LAND;
			mower->RestorePoolVisualState(height, m.value("poolVisualOffsetY", 0.0f));
		}
	}

	// 恢复僵尸
	for (auto& z : j.value("zombies", nlohmann::json::array())) {
		ZombieType type = static_cast<ZombieType>(z["type"].get<int>());
		int   row = z["row"].get<int>();
		float x = z["x"].get<float>();
		int   id = z.value("id", NULL_ZOMBIE_ID);

		Zombie* zombie = nullptr;
		if (id != NULL_ZOMBIE_ID) {
			zombie = board->CreateZombieWithID(type, row, x, id);
		}
		else {
			zombie = board->CreateZombie(type, row, x);
		}

		if (zombie) {
			// 大蒜换行会让逻辑行先切换、Transform Y 再平滑追上；旧档缺 y 时沿用新建实体的行基准。
			Vector savedPosition = zombie->GetPosition();
			savedPosition.y = z.value("y", savedPosition.y);
			zombie->SetPosition(savedPosition);
			zombie->mBodyHealth = z.value("bodyHealth", 270);
			zombie->mBodyMaxHealth = z.value("bodyMaxHealth", 270);
			zombie->mHelmType = static_cast<HelmType>(z.value("helmType", HelmType::HELMTYPE_NONE));
			zombie->mHelmHealth = z.value("helmHealth", 0);
			zombie->mHelmMaxHealth = z.value("helmMaxHealth", 0);
			zombie->mShieldType = static_cast<ShieldType>(z.value("shieldType", ShieldType::SHIELDTYPE_NONE));
			zombie->mShieldHealth = z.value("shieldHealth", 0);
			zombie->mShieldMaxHealth = z.value("shieldMaxHealth", 0);
			zombie->mSpawnWave = z.value("spawnWave", -1);
			zombie->mAttackDamage = z.value("attackDamage", 50);
			zombie->mNeedDropArm = z.value("needDropArm", true);
			zombie->mNeedDropHead = z.value("needDropHead", true);

			zombie->LoadProtectedData(z);
			RestoreAnimState(z, zombie);

			if (z.contains("extraData")) {
				zombie->LoadExtraData(z["extraData"]);
			}

			zombie->ZombieItemUpdate();
			zombie->FinalizeProtectedLoad();
		}
	}

	// 验证僵尸进食状态（防止植物不存在时崩溃）
	for (int id : board->mEntityRegistry.GetAllZombieIDs()) {
		auto zombie = board->mEntityRegistry.GetZombie(id);
		if (!zombie) continue;
		zombie->ValidateEatingState(board->mEntityRegistry);
	}
	// Board 的锁定 ID 要等全部僵尸按稳定 ID 恢复后才能校验，避免加载顺序触发重新随机。
	board->FinalizeNightRoofHijackerLoad();
	board->FinalizeIceStatueExecutionerLoad();

	// 恢复子弹
	for (auto& b : j.value("bullets", nlohmann::json::array())) {
		const BulletType type = static_cast<BulletType>(b["type"].get<int>());
		const BulletType poolType = static_cast<BulletType>(
			b.value("poolType", static_cast<int>(type)));
		int   row = b["row"].get<int>();
		float x = b["x"].get<float>();
		float y = b["y"].get<float>();
		int   id = b.value("id", NULL_BULLET_ID);

		Bullet* bullet = nullptr;
		if (id != NULL_BULLET_ID) {
			bullet = board->CreateBulletWithID(poolType, row, Vector(x, y), id);
		}
		else {
			bullet = board->CreateBullet(poolType, row, Vector(x, y));
		}

		if (bullet) {
			PlantDamageOrigin savedOrigin;
			savedOrigin.kind = static_cast<PlantDamageOriginKind>(std::clamp(
				b.value("plantOriginKind", 0),
				static_cast<int>(PlantDamageOriginKind::NONE),
				static_cast<int>(PlantDamageOriginKind::ASH)));
			savedOrigin.lineage = static_cast<PlantType>(std::clamp(
				b.value("plantOriginLineage", static_cast<int>(PlantType::NUM_PLANT_TYPES)),
				static_cast<int>(PlantType::PLANT_PEASHOOTER),
				static_cast<int>(PlantType::NUM_PLANT_TYPES)));
			bullet->SetPlantDamageOrigin(savedOrigin.IsValid() ? savedOrigin : PlantDamageOrigin{});
			bullet->RestoreSavedPresentationState(
				type, b.value("hitTorchwoodColumn", -1),
				b.value("hitAuroraTorchwoodColumn", -1));
			bullet->SetBulletDamage(b["damage"].get<int>());
			bullet->SetVelocityX(b["velocityX"].get<float>());
			bullet->SetVelocityY(b["velocityY"].get<float>());
			bullet->SetRotationDegrees(b.value("rotationDegrees", 0.0f));
			bullet->SetRotationSpeedDegrees(
				b.value("rotationSpeedDegrees", 0.0f));
			bullet->SetTargetsFlying(b.value("targetsFlying", false));
			bullet->RestorePiercedZombieState(
				b.value("piercedZombieIDs", std::vector<int>{}),
				b.value("spikeDamageRemainders", std::vector<float>{}));
			bullet->RestoreAuroraState(
				b.value("auroraHitZombieIDs", std::vector<int>{}),
				b.value("auroraPlayedHitSound", false));
			if (b.value("threepeaterMotion", false)) {
				// 先恢复运动类型以重建阴影布局，再用存档速度覆盖初始值继续衰减。
				const float savedVelocityY = bullet->GetVelocityY();
				const int sourceRow = savedVelocityY < 0.0f ? row + 1 : row - 1;
				bullet->EnableThreepeaterMotion(sourceRow);
				bullet->SetVelocityY(savedVelocityY);
			}
			if (b.value("lobbedMotion", false)) {
				bullet->RestoreLobbedMotion(
					Vector(b.value("lobStartX", x), b.value("lobStartY", y)),
					Vector(b.value("lobTargetX", x), b.value("lobTargetY", y)),
					b.value("lobElapsed", 0.0f),
					b.value("lobDuration", 1.2f),
					b.value("lobApexHeight", 0.0f),
					b.value("lobTargetsIceWall", false),
					b.value("polarWindMiss", false),
					b.value("polarGuided", false));
			}
			if (b.value("cobCannonMotion", false)) {
				bullet->RestoreCobCannonMotion(
					Vector(b.value("cobStartX", x), b.value("cobStartY", y)),
					Vector(b.value("cobTargetX", x), b.value("cobTargetY", y)),
					b.value("cobTargetRow", row),
					b.value("cobElapsed", 0.0f),
					b.value("cobDuration", 1.4f),
					b.value("polarWindMiss", false),
					b.value("polarGuided", false));
			}
			if (type == BulletType::BULLET_THERMAL_PULSE) {
				bullet->ConfigureThermalPulse(
					Vector(b.value("thermalEndpointX", x),
						b.value("thermalEndpointY", y)),
					b.value("thermalOriginalPlantID", NULL_PLANT_ID));
			}
		}
	}

	// 恢复太阳
	for (auto& s : j.value("suns", nlohmann::json::array())) {
		float x = s["x"].get<float>();
		float y = s["y"].get<float>();
		int  id = s.value("id", NULL_COIN_ID);
		bool small = s.value("small", false);	// 旧存档无此字段→普通阳光，向后兼容

		Sun* sun = nullptr;
		if (id != NULL_COIN_ID) {
			sun = small ? static_cast<Sun*>(board->CreateSmallSunWithID(Vector(x, y), false, id))
						: board->CreateSunWithID(Vector(x, y), false, id);
		}
		else {
			sun = small ? static_cast<Sun*>(board->CreateSmallSun(Vector(x, y), false))
						: board->CreateSun(Vector(x, y), false);
		}

		if (sun) {
			RestoreAnimState(s, sun);
		}
	}

	// 恢复奖杯（旧存档带 "id" 字段，已不再使用，直接忽略）
	for (auto& t : j.value("trophies", nlohmann::json::array())) {
		float x = t["x"].get<float>();
		float y = t["y"].get<float>();
		board->CreateTrophy(Vector(x, y));
	}

	// 恢复弹坑（毁灭菇）；旧档无 craters 字段 → 空数组，天然兼容
	for (auto& c : j.value("craters", nlohmann::json::array())) {
		board->AddCrater(c.value("row", 0), c.value("column", 0),
			c.value("timeLeft", Crater::CRATER_DURATION));
	}

	// CHOOSE_CARD 存档中的 cards 来自旧版词条选择退出 bug：它们是上一轮卡组，不是下一轮
	// 已提交选择。此时禁止恢复到卡槽；若显式冷却快照为空，则只迁移其中仍在冷却的进度。
	const bool isSurvivalCardSelect = board->mIsSurvival
		&& board->mBoardState == BoardState::CHOOSE_CARD;
	std::unordered_map<PlantType, std::pair<float, float>> legacyCardCooldowns;
	for (auto& c : j.value("cards", nlohmann::json::array())) {
		if (isSurvivalCardSelect) {
			if (c.value("isCooldown", false)) {
				PlantType type = static_cast<PlantType>(c["plantType"].get<int>());
				legacyCardCooldowns[type] = {
					c.value("cooldownTimer", 0.0f), c.value("cooldownTime", 0.0f)
				};
			}
			continue;
		}
		if (manager) {
			PlantType plantType = static_cast<PlantType>(c["plantType"].get<int>());
			float posX = c["posX"].get<float>();
			float posY = c["posY"].get<float>();
			int sunCost = c["sunCost"].get<int>();
			float cooldownTime = c["cooldownTime"].get<float>();
			bool isCooldown = c["isCooldown"].get<bool>();
			float cooldownTimer = c["cooldownTimer"].get<float>();

			auto card = GameObjectManager::GetInstance().CreateGameObjectImmediate<Card>(
				LAYER_UI, plantType, sunCost, cooldownTime, false);
			if (!card) continue;
			if (plantType == PlantType::PLANT_IMITATER) {
				const PlantType targetType = static_cast<PlantType>(c.value(
					"imitaterTarget", static_cast<int>(PlantType::NUM_PLANT_TYPES)));
				if (!card->SetImitaterTarget(targetType)) {
					GameObjectManager::GetInstance().DestroyGameObject(card);
					continue;
				}
			}

			if (auto transform = card->GetTransform()) {
				transform->SetPosition(Vector(posX, posY));
			}
			if (isCooldown) {
				card->RestoreCooldown(cooldownTimer, cooldownTime);
			}
			if (card->GetGameplayPlantType() == PlantType::PLANT_BLOVER) {
				const int direction = c.value("bloverDirection",
					static_cast<int>(WindDirection::TOWARD_FRONT));
				card->SetBloverDirection(
					direction == static_cast<int>(WindDirection::TOWARD_HOUSE)
					? WindDirection::TOWARD_HOUSE
					: WindDirection::TOWARD_FRONT);
			}
			manager->AddCard(card);
		}
	}

	// 恢复已放置扶梯；旧档无 ladders 字段时保持空集合，缺 style 时恢复经典样式。
	for (auto& ladder : j.value("ladders", nlohmann::json::array())) {
		const int savedStyle = std::clamp(ladder.value("style", 0), 0,
			static_cast<int>(LadderStyle::ELITE));
		board->AddLadder(ladder.value("row", 0), ladder.value("column", 0),
			static_cast<LadderStyle>(savedStyle));
	}

	// 旧档无施工字段时按既有完整墙恢复；未完成墙由施工者 ID 在首帧继续或孤儿回收。
	if (const auto it = j.find("iceWall"); it != j.end() && it->is_object()) {
		board->AddIceWall(it->value("row", 0), it->value("centerX", 0.0f),
			it->value("health", IceWall::kDefaultHealth),
			it->value("maxHealth", IceWall::kDefaultHealth),
			it->value("thawDamageRemainder", 0.0f),
			it->value("constructionComplete", true),
			it->value("builderZombieID", NULL_ZOMBIE_ID));
	}
	for (const auto& rift : j.value("groundRifts", nlohmann::json::array())) {
		if (!rift.is_object()) continue;
		board->AddGroundRift(
			rift.value("row", 0),
			rift.value("frontX", 0.0f),
			rift.value("nextColumn", -1),
			rift.value("downstreamDamageMultiplier", 1.0f));
	}

	// 恢复生存轮间冷却快照（见 SaveLevelData 同名字段注释）。必须在 ChooseCardComplete 还原冷却之前就位，
	// 而本函数在 OnEnter 选卡分支之前执行，时序成立。旧版问题档的显式快照为空时，从被丢弃的
	// 上一轮 cards 迁移冷却数据，既阻止重复卡牌，也不损失原本仍在冷却的进度。
	if (board->mIsSurvival && board->GetPresentation()) {
		SurvivalCardCooldownMap cooldowns;
		if (j.contains("survivalCardCooldowns") && j["survivalCardCooldowns"].is_array()) {
			for (auto& c : j["survivalCardCooldowns"]) {
				PlantType type = static_cast<PlantType>(c["plantType"].get<int>());
				cooldowns[type] = { c.value("cooldownTimer", 0.0f), c.value("cooldownTime", 0.0f) };
			}
		}
		if (cooldowns.empty() && isSurvivalCardSelect)
			cooldowns = std::move(legacyCardCooldowns);
		board->GetPresentation()->SetSurvivalCardCooldowns(std::move(cooldowns));
	}

	// 恢复旗子升起状态，并立刻对齐进度条滑块（跳过缓动动画）
	if (board->mCurrentWave > 0 && board->GetPresentation()) {
		board->GetPresentation()->RestoreWaveProgress();
	}

	return true;
}

bool GameInfoSaver::LoadLevelDataImpl(Board* board, CardSlotManager* manager)
{
	// 临时图鉴返回在 AutoTest 与正式游戏走同一内存恢复路径，不依赖磁盘存档。
	if (mAlmanacReturnQueued) {
		mAlmanacReturnQueued = false;
		if (board->mLevel != mAlmanacReturnLevel) return false;
		const bool restored = DeserializeLevelDocument(board, manager, mAlmanacReturnDocument);
		if (restored) ClearAlmanacReturn();
		return restored;
	}
	// 显式快照覆盖只消费一次：先清路径再解析，任何返回或异常都不会污染后续场景。
	if (GameAPP::mAutoTestMode && !mAutoTestSnapshotLoadPath.empty()) {
		const std::string filename = mAutoTestSnapshotLoadPath;
		mAutoTestSnapshotLoadPath.clear();
		mAutoTestSnapshotLoadAttempted = true;
		mAutoTestSnapshotLoadSucceeded = false;
		mAutoTestSnapshotLoadError.clear();
		try {
			mAutoTestSnapshotLoadSucceeded =
				DeserializeLevelDataFromPath(board, manager, filename);
			if (!mAutoTestSnapshotLoadSucceeded) {
				mAutoTestSnapshotLoadError = "正式反序列化返回失败";
			}
		}
		catch (const std::exception& e) {
			mAutoTestSnapshotLoadError = e.what();
		}
		return mAutoTestSnapshotLoadSucceeded;
	}

	// AutoTest 默认仍是确定性的全新关卡；仅显式 -AutoTestLoadSave 时读取当前 CWD 下的
	// 关卡存档。写入和删除入口始终短路，因此问题存档在测试后保持逐字节不变。
	if (GameAPP::mAutoTestMode && !GameAPP::mAutoTestLoadSave) return true;
	const std::string filename = GetSaveFileForRead(
		"level" + std::to_string(board->mLevel) + "_data.json");
	return DeserializeLevelDataFromPath(board, manager, filename);
}

bool GameInfoSaver::DeleteLevelData(Board* board)
{
	return board && DeleteLevelData(board->mLevel);
}

bool GameInfoSaver::DeleteLevelData(int level)
{
	if (GameAPP::mAutoTestMode) return true;   // AutoTest（包括读档复现模式）绝不删除真实存档
	return DeleteSaveFile("level" + std::to_string(level) + "_data.json");
}

// ── 异常安全边界 ──────────────────────────────────────────────────────────────
// 公有存档/读档接口统一包一层 try/catch：任何序列化异常（nlohmann 的 type_error/
// parse_error、缺键、类型不符等，均派生自 std::exception）都转成返回 false（沿用既有
// “失败”契约），不再逸出到 CrashHandler 直接崩坏整个游戏。
// 注意：LoadLevelData 中途抛异常时 Board 可能已部分加载（mIsLoadSave/已建实体不会回滚），
// 返回 false 仅保证“不崩”；调用方应把 false 视为读档失败、按全新关卡处理。
bool GameInfoSaver::SavePlayerInfo()
{
	try { return SavePlayerInfoImpl(); }
	catch (const std::exception& e) {
		LOG_ERROR("GameInfoSaver") << "SavePlayerInfo 异常，已跳过保存: " << e.what();
		return false;
	}
}

bool GameInfoSaver::LoadPlayerInfo()
{
	try { return LoadPlayerInfoImpl(); }
	catch (const std::exception& e) {
		LOG_ERROR("GameInfoSaver") << "LoadPlayerInfo 异常，按默认设置继续: " << e.what();
		return false;
	}
}

bool GameInfoSaver::SaveLevelData(Board* board, CardSlotManager* manager)
{
	try { return SaveLevelDataImpl(board, manager); }
	catch (const std::exception& e) {
		LOG_ERROR("GameInfoSaver") << "SaveLevelData 异常，已跳过保存: " << e.what();
		return false;
	}
}

bool GameInfoSaver::LoadLevelData(Board* board, CardSlotManager* manager)
{
	try { return LoadLevelDataImpl(board, manager); }
	catch (const std::exception& e) {
		LOG_ERROR("GameInfoSaver") << "LoadLevelData 异常，已放弃读档（关卡将重新开始）: " << e.what();
		return false;
	}
}

bool GameInfoSaver::SaveAutoTestLevelSnapshot(Board* board, CardSlotManager* manager,
	const std::string& filename)
{
	if (!GameAPP::mAutoTestMode || !board || !manager || filename.empty()) return false;
	try {
		std::error_code ec;
		const auto parent = std::filesystem::u8path(filename).parent_path();
		if (!parent.empty()) {
			std::filesystem::create_directories(parent, ec);
			if (ec) return false;
		}
		return SerializeLevelDataToPath(board, manager, filename);
	}
	catch (const std::exception& e) {
		LOG_ERROR("GameInfoSaver") << "AutoTest 快照保存失败: " << e.what();
		return false;
	}
}

bool GameInfoSaver::QueueAutoTestLevelSnapshotLoad(const std::string& filename)
{
	if (!GameAPP::mAutoTestMode || filename.empty() || !mAutoTestSnapshotLoadPath.empty()
		|| mAutoTestSnapshotLoadAttempted) {
		return false;
	}
	mAutoTestSnapshotLoadPath = filename;
	mAutoTestSnapshotLoadAttempted = false;
	mAutoTestSnapshotLoadSucceeded = false;
	mAutoTestSnapshotLoadError.clear();
	return true;
}

bool GameInfoSaver::ConsumeAutoTestLevelSnapshotLoadResult(std::string& error)
{
	if (!mAutoTestSnapshotLoadAttempted) {
		mAutoTestSnapshotLoadPath.clear();
		error = "新 GameScene 未尝试加载快照";
		return false;
	}
	const bool succeeded = mAutoTestSnapshotLoadSucceeded;
	error = mAutoTestSnapshotLoadError;
	mAutoTestSnapshotLoadAttempted = false;
	mAutoTestSnapshotLoadSucceeded = false;
	mAutoTestSnapshotLoadError.clear();
	return succeeded;
}

void GameInfoSaver::CancelAutoTestLevelSnapshotLoad()
{
	mAutoTestSnapshotLoadPath.clear();
	mAutoTestSnapshotLoadAttempted = false;
	mAutoTestSnapshotLoadSucceeded = false;
	mAutoTestSnapshotLoadError.clear();
}

bool GameInfoSaver::CaptureAlmanacReturn(Board* board, CardSlotManager* manager)
{
	if (!board || !manager) return false;
	try {
		nlohmann::json document;
		if (!SerializeLevelDocument(board, manager, document)) return false;
		mAlmanacReturnDocument = std::move(document);
		mAlmanacReturnLevel = board->mLevel;
		mAlmanacReturnQueued = false;
		return true;
	}
	catch (const std::exception& e) {
		LOG_ERROR("GameInfoSaver") << "无法保存图鉴返回现场，保留当前游戏: " << e.what();
		return false;
	}
}

bool GameInfoSaver::QueueAlmanacReturn()
{
	if (mAlmanacReturnLevel < 0 || !mAlmanacReturnDocument.is_object()) return false;
	mAlmanacReturnQueued = true;
	return true;
}

void GameInfoSaver::ClearAlmanacReturn()
{
	mAlmanacReturnDocument = nullptr;
	mAlmanacReturnLevel = -1;
	mAlmanacReturnQueued = false;
}
