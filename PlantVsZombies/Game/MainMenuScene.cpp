#include "MainMenuScene.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include "AudioSystem.h"
#include "../ResourceKeys.h"
#include "../UI/GameMessageBox.h"
#include "Game/Board/Board.h"
#include "AdventureProgression.h"
#include "../Logger.h"

#include <algorithm>

namespace
{
	constexpr int kSecondAreaFirstLevel = AdventureProgression::LEVELS_PER_AREA + 1; // 主菜单跳关目标：2-1
	const Vector kConsoleButtonPosition(920.0f, 548.0f); // 主菜单右下角控制台入口左上角坐标
	const Vector kConsoleButtonSize(150.0f, 40.0f); // 控制台入口按钮尺寸，单位：逻辑像素
	const Vector kConsoleOptionHitSize(620.0f, 46.0f); // 控制台选项整行命中区域，单位：逻辑像素
	constexpr float kConsoleTooltipMaxWidth = 770.0f; // 控制台说明框最大宽度，高度随内容自适应，单位：逻辑像素
}

MainMenuScene::~MainMenuScene() = default;

std::shared_ptr<Button> MainMenuScene::GetSurvivalButton() const
{
	return mMainMenuButtons ? mMainMenuButtons->GetSurvivalButton() : nullptr;
}

std::shared_ptr<Button> MainMenuScene::GetMiniGamesButton() const
{
	return mMainMenuButtons ? mMainMenuButtons->GetMiniGamesButton() : nullptr;
}

void MainMenuScene::OnEnter()
{
	// 首页入口开始新的图鉴导航，不能继承已结束的关卡返回目标。
	GameAPP::GetInstance().mGameInfoSaver.ClearAlmanacReturn();
	Scene::OnEnter();
	mMainMenuButtons = std::make_unique<MainMenuButtons>(&mUIManager, this);
	mMainMenuButtons->Initialize();
	AudioSystem::PlayMusic(ResourceKeys::Music::MUSIC_MAINMENU, -1);
}

void MainMenuScene::OnExit()
{
	mSkipToSecondAreaButton.reset();
	mOpitionButton.reset();
	mConsoleButton.reset();
	mExitButton.reset();
	mAlmanacButton.reset();
	Scene::OnExit();
	// UIManager 先销毁捕获控制器 this 的 Button 回调，再释放控制器本身。
	mMainMenuButtons.reset();
}

void MainMenuScene::Update()
{
	Scene::Update();
	if (mReadyToRefreshConsole) {
		// 复选框回调结束后重建面板，使依赖总开关的选项立即出现或消失。
		mReadyToRefreshConsole = false;
		if (auto menu = mConsoleMenu.lock()) menu->Close();
		mOpenConsole = false;
		OpenConsole();
	}
	if (mReadyToSwitchAdventureLevel) {
		mReadyToSwitchAdventureLevel = false;
		auto& gameApp = GameAPP::GetInstance();
		auto& SceneMgr = SceneManager::GetInstance();

		gameApp.GetGraphics().SetCameraPosition(0, 0);
		SceneMgr.SetGlobalData("GameSelectMode", "adventure");
		SceneMgr.SwitchTo("GameSelectScene");
		return;
	}
	if (mReadyToSkipToSecondArea) {
		mReadyToSkipToSecondArea = false;
		SkipToSecondArea();
		return;
	}
	if (mReadyToSwitchAlmanac) {
		mReadyToSwitchAlmanac = false;
		auto& gameApp = GameAPP::GetInstance();
		auto& SceneMgr = SceneManager::GetInstance();
		gameApp.GetGraphics().SetCameraPosition(0, 0);
		SceneMgr.SwitchTo("AlmanacScene");
		// SwitchTo 已销毁当前主菜单，不能继续读取其余入口标志。
		return;
	}
	if (mReadyToSwitchMiniGames) {
		mReadyToSwitchMiniGames = false;
		auto& scenes = SceneManager::GetInstance();
		GameAPP::GetInstance().GetGraphics().SetCameraPosition(0, 0);
		scenes.SetGlobalData("GameSelectMode", "minigames");
		scenes.SwitchTo("GameSelectScene");
		return;
	}
	if (mReadyToSwitchSurvival) {
		mReadyToSwitchSurvival = false;
		auto& gameApp = GameAPP::GetInstance();
		auto& SceneMgr = SceneManager::GetInstance();
		gameApp.GetGraphics().SetCameraPosition(0, 0);
		// 不再直进无尽关，先进「选择关卡」界面，由玩家选择具体无尽地形。
		SceneMgr.SetGlobalData("GameSelectMode", "survival");
		SceneMgr.SwitchTo("GameSelectScene");
		return;
	}
}

/** 注册主菜单绘制层；全部通关后关卡角标仍指向最后一个可重玩的冒险关。 */
void MainMenuScene::BuildDrawCommands()
{
	Scene::BuildDrawCommands();
	RegisterDrawCommand("DrawLevel",
		[this](Graphics* g) {
			if (this->mOpenMenu || this->mOpenConsole) return;
			auto& gameApp = GameAPP::GetInstance();
			// 通关哨兵用于保存完成状态，不作为尚未存在的下一大关显示。
			const int displayLevel = std::clamp(gameApp.mAdventureLevel, 1, AdventureProgression::LAST_ADVENTURE_LEVEL);
			int mBigLevel = AdventureProgression::GetAreaNumber(displayLevel);
			int mSmallLevel = AdventureProgression::GetLevelNumberInArea(displayLevel);
			// 坐标与冒险按钮 (545,85) 缩放 1.00 绑定：石碑贴图内角标的相对位置换算而来
			gameApp.DrawText(std::to_string(mBigLevel), Vector(695, 168),
				glm::vec4(255.0f, 255.0f, 255.0f, 255.0f));
			gameApp.DrawText(std::to_string(mSmallLevel), Vector(718, 170),
				glm::vec4(255.0f, 255.0f, 255.0f, 255.0f));
		},
		LAYER_UI + 10000);
	RegisterDrawCommand("DrawButton",
		[this](Graphics* g) {
			// 模态窗口只接管输入；背景入口仍先绘制，再由 UIManager 把 MessageBox 盖在最上层。
			if (mMainMenuButtons) {
				mMainMenuButtons->Draw(g);
			}
			if (mOpitionButton) {
				mOpitionButton->Draw(g);
			}
			if (mConsoleButton) {
				mConsoleButton->Draw(g);
			}
			if (mAlmanacButton) {
				mAlmanacButton->Draw(g);
			}
			if (mSkipToSecondAreaButton) {
				mSkipToSecondAreaButton->Draw(g);
			}
			if (mExitButton) {
				mExitButton->Draw(g);
			}
		},
		LAYER_UI - 100);

	SortDrawCommands();

	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_BG, 0.0f, 0.0f, 12.0f, 12.0f, -10);
	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_BG_CENTER, 80.0f, 300.0f, 1.0f, 1.0f, 0);
	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_BG_LEFT, 0.0f, 0.0f, 1.0f, 1.0f, 3);
	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_BG_RIGHT, 210.0f, 40.0f, 1.0f, 1.0f, 5);
	// 花瓶
	mOpitionButton = mUIManager.CreateButton(Vector(704, 485), Vector(48 * 1.5f, 22 * 1.5f));
	mOpitionButton->SetAsCheckbox(false);
	mOpitionButton->SetSkipDraw(true);
	mOpitionButton->SetImageKeys(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_OPTIONS1,
		ResourceKeys::Textures::IMAGE_SELECTORSCREEN_OPTIONS2,
		ResourceKeys::Textures::IMAGE_SELECTORSCREEN_OPTIONS2,
		ResourceKeys::Textures::IMAGE_SELECTORSCREEN_OPTIONS2);
	mOpitionButton->SetClickCallBack([this](bool) {
		this->OpenMenu();
		});
	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_HELP1, 786.0f, 515.0f, 1.0f, 1.0f, 10);
	mExitButton = mUIManager.CreateButton(Vector(855, 495), Vector(47 * 1.2f, 27 * 1.2f));
	mExitButton->SetAsCheckbox(false);
	mExitButton->SetSkipDraw(true);
	mExitButton->SetImageKeys(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_QUIT1,
		ResourceKeys::Textures::IMAGE_SELECTORSCREEN_QUIT2,
		ResourceKeys::Textures::IMAGE_SELECTORSCREEN_QUIT2,
		ResourceKeys::Textures::IMAGE_SELECTORSCREEN_QUIT2);
	mExitButton->SetClickCallBack([](bool) {
		GameAPP::GetInstance().SetRunning(false);
		});

	mConsoleButton = mUIManager.CreateButton(kConsoleButtonPosition, kConsoleButtonSize);
	mConsoleButton->SetAsCheckbox(false);
	mConsoleButton->SetSkipDraw(true);
	mConsoleButton->SetText(u8"控制台", ResourceKeys::Fonts::FONT_FZCQ, 18);
	mConsoleButton->SetTextColor(glm::vec4{ 53, 191, 61, 255 });
	mConsoleButton->SetHoverTextColor(glm::vec4{ 53, 240, 61, 255 });
	mConsoleButton->SetImageKeys(
		ResourceKeys::Textures::IMAGE_BUTTONSMALL,
		ResourceKeys::Textures::IMAGE_BUTTONSMALL,
		ResourceKeys::Textures::IMAGE_BUTTONSMALL,
		ResourceKeys::Textures::IMAGE_BUTTONSMALL);
	mConsoleButton->SetClickCallBack([this](bool) {
		OpenConsole();
		});

	// 花
	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_FLOWER1, 825.0f, 420.0f, 1.0f, 1.0f, 12);
	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_FLOWER2, 785.0f, 439.0f, 1.0f, 1.0f, 12);
	AddTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_FLOWER3, 870.0f, 450.0f, 1.0f, 1.0f, 12);

	mAlmanacButton = mUIManager.CreateButton(Vector(521, 441), Vector(99 * 1.0f, 99 * 1.0f));
	mAlmanacButton->SetAsCheckbox(false);
	mAlmanacButton->SetSkipDraw(true);
	mAlmanacButton->SetImageKeys("IMAGE_SELECTORSCREEN_ALMANAC",
		"IMAGE_SELECTORSCREEN_ALMANAC",
		"IMAGE_SELECTORSCREEN_ALMANAC",
		"IMAGE_SELECTORSCREEN_ALMANAC");
	mAlmanacButton->SetClickCallBack([this](bool) {
		this->mReadyToSwitchAlmanac = true;
		});

	// 跳关只服务尚未到达 2-1 的存档；进入第二大关后不再占用主菜单空间。
	if (GameAPP::GetInstance().mAdventureLevel < kSecondAreaFirstLevel) {
		mSkipToSecondAreaButton = mUIManager.CreateButton(
			Vector(330, 535), Vector(213 * 0.9f, 50 * 0.9f));
		mSkipToSecondAreaButton->SetAsCheckbox(false);
		mSkipToSecondAreaButton->SetSkipDraw(true);
		mSkipToSecondAreaButton->SetText(u8"跳到 2-1",
			ResourceKeys::Fonts::FONT_FZCQ, 18);
		mSkipToSecondAreaButton->SetTextColor(glm::vec4{ 53, 191, 61, 255 });
		mSkipToSecondAreaButton->SetHoverTextColor(glm::vec4{ 53, 240, 61, 255 });
		mSkipToSecondAreaButton->SetImageKeys(
			ResourceKeys::Textures::IMAGE_BUTTONBIG,
			ResourceKeys::Textures::IMAGE_BUTTONBIG,
			ResourceKeys::Textures::IMAGE_BUTTONBIG,
			ResourceKeys::Textures::IMAGE_BUTTONBIG);
		mSkipToSecondAreaButton->SetClickCallBack([this](bool) {
			DeltaTime::SetPaused(false);
			mReadyToSkipToSecondArea = true;
			});
	}
}

void MainMenuScene::SkipToSecondArea()
{
	auto& gameApp = GameAPP::GetInstance();

	// 先保证初始豌豆射手存在，再按正式奖励表补齐已经跳过的 1-1～1-9 奖励。
	auto ensureCard = [&gameApp](PlantType type) {
		if (type == AdventureProgression::NO_PLANT_REWARD) return;
		if (std::find(gameApp.mHaveCards.begin(), gameApp.mHaveCards.end(), type) ==
			gameApp.mHaveCards.end()) {
			gameApp.mHaveCards.push_back(type);
		}
		};
	ensureCard(PlantType::PLANT_PEASHOOTER);
	for (int completedLevel = 1; completedLevel < kSecondAreaFirstLevel; ++completedLevel) {
		ensureCard(AdventureProgression::GetPlantReward(completedLevel));
	}

	// 只提升、不回退玩家的永久进度；无论当前进度多高，本按钮的游玩入口固定为 2-1。
	gameApp.mAdventureLevel = std::max(gameApp.mAdventureLevel, kSecondAreaFirstLevel);
	if (!gameApp.mGameInfoSaver.SavePlayerInfo()) {
		LOG_ERROR("MainMenu") << "跳到 2-1 后无法立即保存冒险进度，将在退出游戏时重试。";
	}

	gameApp.GetGraphics().SetCameraPosition(0, 0);
	auto& sceneManager = SceneManager::GetInstance();
	sceneManager.SetGlobalData("EnterLevel", std::to_string(kSecondAreaFirstLevel));
	sceneManager.SwitchTo("GameScene");
}

void MainMenuScene::OpenMenu()
{
	if (mOpenMenu || mOpenConsole) return;

	mOpenMenu = true;
	DeltaTime::SetPaused(true);
	auto& gameApp = GameAPP::GetInstance();
	const glm::vec4 labelColor{ 107, 109, 144, 255 };
	// 四个复选框初始态来自各自不同的状态变量（mVsync / IsFullscreen / mShowPlantHP /
	// mShowZombieHP），Builder 写法按项绑定 initChecked，原"按槽位赋值错位"bug 类别不复存在
	mMenu = GameMessageBox::Builder(Vector(SCENE_WIDTH / 2 + 50, SCENE_HEIGHT / 2 - 80.0f))
		.Background(ResourceKeys::Textures::IMAGE_OPTIONS_MENUBACK)
		.ControlFont(ResourceKeys::Fonts::FONT_FZJT)
		.Button(u8"返回游戏", Vector(400, 430), Vector(360, 100), 40, [this]() {
			mOpenMenu = false;
			DeltaTime::SetPaused(false);
		}, ResourceKeys::Textures::IMAGE_OPTIONS_BACKTOGAMEBUTTON0)
		.Checkbox(Vector(510, 250), Vector(42, 39), []() {
			auto& app = GameAPP::GetInstance();
			app.ApplyVsync(!app.mVsync);
		}, gameApp.mVsync)
		.Checkbox(Vector(510, 290), Vector(42, 39), []() {
			auto& app = GameAPP::GetInstance();
			app.SetFullscreen(!app.IsFullscreen());
		}, gameApp.IsFullscreen())
		.Checkbox(Vector(510, 330), Vector(42, 39), []() {
			auto& app = GameAPP::GetInstance();
			app.mShowPlantHP = !app.mShowPlantHP;
		}, gameApp.mShowPlantHP)
		.Checkbox(Vector(510, 370), Vector(42, 39), []() {
			auto& app = GameAPP::GetInstance();
			app.mShowZombieHP = !app.mShowZombieHP;
		}, gameApp.mShowZombieHP)
		.Slider(Vector(530, 175), Vector(135, 10), 0.0f, 1.0f, AudioSystem::GetMusicVolume(),
			[](float v) { AudioSystem::SetMusicVolume(v); })
		.Slider(Vector(530, 200), Vector(135, 10), 0.0f, 1.0f, AudioSystem::GetSoundVolume(),
			[](float v) { AudioSystem::SetSoundVolume(v); })
		.Slider(Vector(530, 225), Vector(135, 10), 1, 4,
			static_cast<float>(GameAPP::GetInstance().Difficulty),
			[](float v) { GameAPP::GetInstance().Difficulty = static_cast<int>(v); }, true)
		.Text(Vector(480, 165), 22, u8"音乐", labelColor)
		.Text(Vector(480, 190), 22, u8"音效", labelColor)
		.Text(Vector(480, 215), 22, u8"难度", labelColor)
		.Text(Vector(555, 254), 18, u8"垂直同步", labelColor)
		.Text(Vector(555, 294), 18, u8"全屏", labelColor)
		.Text(Vector(555, 334), 18, u8"植物血量显示", labelColor)
		.Text(Vector(555, 374), 18, u8"僵尸血量显示", labelColor)
		.Show();
}

void MainMenuScene::OpenConsole()
{
	if (mOpenMenu || mOpenConsole) return;

	mOpenConsole = true;
	DeltaTime::SetPaused(true);

	auto& gameApp = GameAPP::GetInstance();
	const Vector panelCenter(SCENE_WIDTH / 2.0f, SCENE_HEIGHT / 2.0f);
	const glm::vec4 titleColor{ 53, 191, 61, 255 };
	const glm::vec4 labelColor{ 245, 214, 127, 255 };
	GameMessageBox::Builder builder(panelCenter);
	builder
		.Panel(static_cast<float>(SCENE_WIDTH), static_cast<float>(SCENE_HEIGHT))
		.Text(panelCenter + Vector(-76.0f, -190.0f), 38, u8"控制台", titleColor)
		.TooltipPanel(kConsoleTooltipMaxWidth, 17.0f)
		.Checkbox(panelCenter + Vector(-205.0f, -135.0f), Vector(50.0f, 46.0f), []() {
			auto& app = GameAPP::GetInstance();
			app.mHxyModeEnabled = !app.mHxyModeEnabled;
		}, gameApp.mHxyModeEnabled,
			u8"新开局生效：出怪预算固定为难度1的70%（实际只数随种类和固定出怪变化）；开局额外300阳光；僵尸所有防具（含气球）的初始及最大血量为原来的75%，本体血量不变。续局沿用该局设置，不重复赠送阳光。",
			kConsoleOptionHitSize)
		.Text(panelCenter + Vector(-140.0f, -120.0f), 22,
			u8"HXY专属", labelColor)
		.Checkbox(panelCenter + Vector(-205.0f, -75.0f), Vector(50.0f, 46.0f), []() {
			auto& app = GameAPP::GetInstance();
			app.mEnableMonteCarloAI = !app.mEnableMonteCarloAI;
		}, gameApp.mEnableMonteCarloAI,
			u8"让部分僵尸模拟未来战局后选择目标；关闭时改用更简单、较省性能的决策。不建议关闭。",
			kConsoleOptionHitSize)
		.Text(panelCenter + Vector(-140.0f, -60.0f), 22,
			u8"蒙特卡洛模拟未来AI", labelColor)
		.Checkbox(panelCenter + Vector(-205.0f, -15.0f), Vector(50.0f, 46.0f), []() {
			auto& app = GameAPP::GetInstance();
			app.mAdvancedPauseEnabled = !app.mAdvancedPauseEnabled;
		}, gameApp.mAdvancedPauseEnabled,
			u8"开启后，空格暂停时仍可选择卡片和种植；关闭后，暂停会锁住战斗操作。建议开启。",
			kConsoleOptionHitSize)
		.Text(panelCenter + Vector(-140.0f, 0.0f), 22,
			u8"高级暂停（暂停时可选卡和种植）", labelColor)
		.Checkbox(panelCenter + Vector(-205.0f, 45.0f), Vector(50.0f, 46.0f), [this]() {
			auto& app = GameAPP::GetInstance();
			app.mTyphoonWeatherEnabled = !app.mTyphoonWeatherEnabled;
			mReadyToRefreshConsole = true;
		}, gameApp.mTyphoonWeatherEnabled,
			u8"决定关卡是否可能出现台风；关闭后，台风概率、预警和效果都会停用。游玩生存模式建议关闭。注: 台风有较大运气成分，不愿意接受太多运气的玩家建议关闭；但是关闭会影响部分关卡（2-9等）的体验（变简单）不建议关闭。",
			kConsoleOptionHitSize)
		.Text(panelCenter + Vector(-140.0f, 60.0f), 22,
			u8"会出现台风天气", labelColor);
	if (gameApp.mTyphoonWeatherEnabled) {
		builder.Checkbox(panelCenter + Vector(-205.0f, 105.0f), Vector(50.0f, 46.0f), []() {
			auto& app = GameAPP::GetInstance();
			app.mOpeningTyphoonProtectionEnabled = !app.mOpeningTyphoonProtectionEnabled;
		}, gameApp.mOpeningTyphoonProtectionEnabled,
			u8"开启后，普通冒险与生存第一轮的第1～5波不会附加台风。建议开启。",
			kConsoleOptionHitSize)
		.Text(panelCenter + Vector(-140.0f, 120.0f), 22,
			u8"开局台风保护（第1～5波）", labelColor);
	}
	mConsoleMenu = builder
		.Button(u8"关闭", panelCenter + Vector(-90.0f, 180.0f), Vector(180.0f, 52.0f),
			24, [this]() { CloseConsole(); })
		.Show();
}

std::string MainMenuScene::GetConsoleTooltipText() const
{
	if (auto menu = mConsoleMenu.lock()) {
		return menu->GetHoveredTooltipText();
	}
	return {};
}

Vector MainMenuScene::GetConsoleTooltipPosition() const
{
	if (auto menu = mConsoleMenu.lock()) {
		return menu->GetTooltipDrawPosition();
	}
	return Vector::zero();
}

void MainMenuScene::CloseConsole()
{
	mOpenConsole = false;
	DeltaTime::SetPaused(false);
}
