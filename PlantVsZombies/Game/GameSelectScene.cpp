#include "GameSelectScene.h"

#include "AdventureProgression.h"
#include "MiniGameDefinition.h"
#include "AudioSystem.h"
#include "Game/Board/Board.h"
#include "SceneManager.h"
#include "PlantAlmanacScene.h"
#include "UI/GameMessageBox.h"
#include "../GameApp.h"

#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <string>

namespace {
constexpr int SELECT_COLUMNS = 6; // 每页卡片列数
constexpr int SELECT_ROWS = 3; // 每页卡片行数
constexpr int SELECT_ENTRIES_PER_PAGE = SELECT_COLUMNS * SELECT_ROWS; // 每页最多关卡数
constexpr float SELECT_CARD_SCALE = 0.95f; // Challenge_Window 原图的显示倍率
constexpr float SELECT_CARD_WIDTH = 118.0f * SELECT_CARD_SCALE; // 关卡卡片宽度，单位：逻辑像素
constexpr float SELECT_CARD_HEIGHT = 120.0f * SELECT_CARD_SCALE; // 关卡卡片高度，单位：逻辑像素
constexpr float CARD_START_X = 115.0f; // 第一列卡片左上角 X
constexpr float CARD_START_Y = 125.0f; // 第一行卡片左上角 Y
constexpr float CARD_PITCH_X = 150.0f; // 相邻卡片列间距，单位：逻辑像素
constexpr float CARD_PITCH_Y = 140.0f; // 相邻卡片行间距，单位：逻辑像素
constexpr float PREVIOUS_PAGE_BUTTON_X = 180.0f; // 左下翻页按钮 X，避开返回按钮的右边界 169
constexpr float NEXT_PAGE_BUTTON_X = 1015.0f; // 右下翻页按钮 X
constexpr float PAGE_BUTTON_Y = 535.0f; // 翻页按钮 Y，避开第三行卡片
constexpr float PAGE_BUTTON_SIZE = 60.0f; // 复用 Zen_NextGarden 原图尺寸
constexpr float PAGE_BACK_ROTATION = 180.0f; // 上一页箭头朝向，单位：度
constexpr float PAGE_FORWARD_ROTATION = 0.0f; // 下一页箭头朝向，单位：度
constexpr float SKIP_DIALOG_SCALE = 1.2f; // 跳关确认框沿用游戏短弹窗倍率
constexpr float FALLBACK_CROP_LEFT_RATIO = 0.56f; // 无缩略图时正式背景裁剪区的归一化左边界
constexpr float FALLBACK_CROP_TOP_RATIO = 0.12f; // 无缩略图时正式背景裁剪区的归一化上边界
constexpr float FALLBACK_CROP_WIDTH_RATIO = 0.19f; // 无缩略图时正式背景裁剪区的归一化宽度

struct PreviewSource {
	const std::string* textureKey = nullptr;
	bool cropFromBackground = false;
};

Vector GetCardPosition(std::size_t pageIndex)
{
	const int row = static_cast<int>(pageIndex) / SELECT_COLUMNS;
	const int column = static_cast<int>(pageIndex) % SELECT_COLUMNS;
	return Vector(CARD_START_X + column * CARD_PITCH_X,
		CARD_START_Y + row * CARD_PITCH_Y);
}

PreviewSource GetPreviewSource(int level)
{
	switch (GameAPP::GetInstance().GetBackgroundID(level)) {
	case Background::GROUND_DAY:
		return { &ResourceKeys::Textures::IMAGE_ALMANAC_GROUNDDAY, false };
	case Background::GROUND_NIGHT:
		return { &ResourceKeys::Textures::IMAGE_ALMANAC_GROUNDNIGHT, false };
	case Background::WATER_POOL:
		return { &ResourceKeys::Textures::IMAGE_ALMANAC_GROUNDPOOL, false };
	case Background::NIGHT_WATER_POOL:
		return { &ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHTPOOL, true };
	case Background::ROOF:
		return { &ResourceKeys::Textures::IMAGE_BACKGROUND_ROOF, true };
	case Background::NIGHT_ROOF:
		return { &ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHTROOF, true };
	case Background::WINTER_GARDEN:
		return { &ResourceKeys::Textures::IMAGE_BACKGROUND_WINTERGARDEN, true };
	case Background::HOT_COLD_STORAGE:
		return { &ResourceKeys::Textures::IMAGE_BACKGROUND_HOT_COLD_STORAGE, true };
	case Background::GLOOMCRYSTAL_MINE:
		return { &ResourceKeys::Textures::IMAGE_BACKGROUND_GLOOMCRYSTAL_MINE, true };
	case Background::POLAR_NIGHT_SNOWFIELD:
		return { &ResourceKeys::Textures::IMAGE_BACKGROUND_POLAR_NIGHT, true };
	}
	return { &ResourceKeys::Textures::IMAGE_BACKGROUND_DAY, true };
}

std::string GetLevelLabel(GameSelectScene::SelectMode mode, int level)
{
	if (mode == GameSelectScene::SelectMode::MINIGAMES) return MiniGame::GetName(level);
	if (mode == GameSelectScene::SelectMode::ADVENTURE) {
		return std::to_string(AdventureProgression::GetAreaNumber(level)) + "-"
			+ std::to_string(AdventureProgression::GetLevelNumberInArea(level));
	}
	if (const auto* definition = FindSurvivalEndlessDefinition(level)) {
		return definition->label;
	}
	return std::to_string(level);
}

// 在指定中心和最大宽度内按真实字形尺寸缩小并居中文字。
void DrawFittedCenteredText(GameAPP& app, const std::string& text,
	float centerX, float centerY, float maxWidth, const glm::vec4& color,
	const std::string& fontKey, int maxSize, int minSize)
{
	int fontSize = minSize;
	int textWidth = 0;
	int textHeight = 0;
	for (int size = maxSize; size >= minSize; --size) {
		TTF_Font* font = ResourceManager::GetInstance().GetFont(fontKey, size);
		int width = 0;
		int height = 0;
		if (font) TTF_SizeUTF8(font, text.c_str(), &width, &height);
		fontSize = size;
		textWidth = width;
		textHeight = height;
		if (width <= maxWidth) break;
	}
	app.DrawText(text,
		Vector(centerX - textWidth * 0.5f, centerY - textHeight * 0.5f),
		color, fontKey, fontSize);
}
} // namespace

void GameSelectScene::BuildDrawCommands()
{
	Scene::BuildDrawCommands();

	// Challenge_Background 为 1280x720，铺满本项目 1100x600 的逻辑画面。
	AddTexture(ResourceKeys::Textures::IMAGE_CHALLENGE_BACKGROUND,
		0.0f, 0.0f, 1100.0f / 1280.0f, 600.0f / 720.0f, -1000, false);

	mBackMenuButton = mUIManager.CreateButton(Vector(7, 560), Vector(162, 26));
	mBackMenuButton->SetAsCheckbox(false);
	mBackMenuButton->SetImageKeys(
		"IMAGE_ALMANAC_INDEXBUTTON",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT");
	mBackMenuButton->SetText(u8"返回菜单", ResourceKeys::Fonts::FONT_FZJZ, 18);
	mBackMenuButton->SetTextColor(glm::vec4(52, 51, 93, 255));
	mBackMenuButton->SetHoverTextColor(glm::vec4(52, 51, 93, 255));
	mBackMenuButton->SetClickCallBack([this](bool) {
		mReadyToSwitchMainMenu = true;
	});

	mPreviousPageButton = mUIManager.CreateButton(
		Vector(PREVIOUS_PAGE_BUTTON_X, PAGE_BUTTON_Y),
		Vector(PAGE_BUTTON_SIZE, PAGE_BUTTON_SIZE));
	mPreviousPageButton->SetAsCheckbox(false);
	mPreviousPageButton->SetImageKeys(ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
		ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
		ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN);
	mPreviousPageButton->SetImageRotationDegrees(PAGE_BACK_ROTATION);
	mPreviousPageButton->SetClickCallBack([this](bool) {
		mPendingPageDelta = -1;
	});

	mNextPageButton = mUIManager.CreateButton(
		Vector(NEXT_PAGE_BUTTON_X, PAGE_BUTTON_Y),
		Vector(PAGE_BUTTON_SIZE, PAGE_BUTTON_SIZE));
	mNextPageButton->SetAsCheckbox(false);
	mNextPageButton->SetImageKeys(ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
		ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
		ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN);
	mNextPageButton->SetImageRotationDegrees(PAGE_FORWARD_ROTATION);
	mNextPageButton->SetClickCallBack([this](bool) {
		mPendingPageDelta = 1;
	});

	mSkipLevelButton = mUIManager.CreateButton(Vector(800, 552), Vector(190, 32));
	mSkipLevelButton->SetAsCheckbox(false);
	mSkipLevelButton->SetImageKeys("IMAGE_ALMANAC_INDEXBUTTON",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT", "IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT");
	mSkipLevelButton->SetText(u8"跳过本关", ResourceKeys::Fonts::FONT_FZJZ, 20);
	mSkipLevelButton->SetTextColor(glm::vec4(52, 51, 93, 255));
	mSkipLevelButton->SetHoverTextColor(glm::vec4(52, 51, 93, 255));
	mSkipLevelButton->SetClickCallBack([this](bool) { ConfirmSkipLevel(); });

	CreateCurrentPageCards();
	RefreshPageButtonState();

	// 地面预览必须随当前页变化，因此使用动态绘制命令而不是静态 Scene 纹理列表。
	RegisterDrawCommand("DrawSelectGroundPreviews", [this](Graphics* graphics) {
		for (std::size_t index = 0; index < mCurrentPageLevels.size(); ++index) {
			const Vector cardPosition = GetCardPosition(index);
			const PreviewSource preview = GetPreviewSource(mCurrentPageLevels[index]);
			const Texture* texture = preview.textureKey
				? ResourceManager::GetInstance().GetTexture(*preview.textureKey) : nullptr;
			if (!texture) continue;

			constexpr float bleed = 2.0f; // 预览略伸入卡框，避免透明开口边缘出现缝隙
			const float offsetX = 20.0f * SELECT_CARD_SCALE - bleed;
			const float offsetY = 8.0f * SELECT_CARD_SCALE - bleed;
			const float width = 77.0f * SELECT_CARD_SCALE + 2.0f * bleed;
			const float height = 59.0f * SELECT_CARD_SCALE + 2.0f * bleed;
			const float previewX = cardPosition.x + offsetX;
			const float previewY = cardPosition.y + offsetY;

			if (!preview.cropFromBackground) {
				graphics->DrawTexture(texture, previewX, previewY, width, height);
				continue;
			}

			// 没有专用缩略图时，从对应正式背景的同一归一化区域裁出预览。
			const float sourceX = texture->width * FALLBACK_CROP_LEFT_RATIO;
			const float sourceY = texture->height * FALLBACK_CROP_TOP_RATIO;
			const float sourceWidth = texture->width * FALLBACK_CROP_WIDTH_RATIO;
			const float scale = width / sourceWidth;
			graphics->PushClipRect(static_cast<int>(previewX), static_cast<int>(previewY),
				static_cast<int>(width + 1.0f), static_cast<int>(height + 1.0f));
			graphics->DrawTexture(texture,
				previewX - sourceX * scale,
				previewY - sourceY * scale,
				texture->width * scale, texture->height * scale);
			graphics->PopClipRect();
		}
	}, LAYER_UI - 1);

	RegisterDrawCommand("DrawSelectTexts", [this](Graphics*) {
		auto& gameApp = GameAPP::GetInstance();
		const std::string title = mSelectMode == SelectMode::ADVENTURE
			? u8"选择冒险关卡" : (mSelectMode == SelectMode::MINIGAMES
				? u8"选择小游戏" : u8"选择生存关卡");
		DrawFittedCenteredText(gameApp, title, 552.0f, 82.0f, 500.0f,
			glm::vec4(0, 0, 0, 255), ResourceKeys::Fonts::FONT_FZJZ, 37, 24);
		DrawFittedCenteredText(gameApp, title, 550.0f, 80.0f, 500.0f,
			glm::vec4(219, 219, 219, 219), ResourceKeys::Fonts::FONT_FZJZ, 37, 24);

		for (std::size_t index = 0; index < mCurrentPageLevels.size(); ++index) {
			const Vector cardPosition = GetCardPosition(index);
			if (IsAdventureLevelCompleted(mCurrentPageLevels[index])) {
				if (const Texture* trophy = ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_MINIGAME_TROPHY)) {
					// 沿用经典 MiniGamesWidget 的左上角原生奖杯覆盖位置，并随卡框等比缩放。
					gameApp.GetGraphics().DrawTexture(trophy,
						cardPosition.x + 3.0f * SELECT_CARD_SCALE,
						cardPosition.y + 6.0f * SELECT_CARD_SCALE,
						trophy->width * SELECT_CARD_SCALE, trophy->height * SELECT_CARD_SCALE);
				}
			}
			DrawFittedCenteredText(gameApp,
				GetLevelLabel(mSelectMode, mCurrentPageLevels[index]),
				cardPosition.x + SELECT_CARD_WIDTH * 0.5f,
				cardPosition.y + SELECT_CARD_HEIGHT * 0.73f,
				SELECT_CARD_WIDTH * 0.82f, glm::vec4(46, 46, 84, 255),
				ResourceKeys::Fonts::FONT_FZJZ, 16, 9);
		}

		if (mSelectMode == SelectMode::MINIGAMES) {
			DrawFittedCenteredText(gameApp, u8"最后的家底：3000 阳光，七种植物，守住十波！",
				650.0f, 300.0f, 650.0f, glm::vec4(46, 46, 84, 255),
				ResourceKeys::Fonts::FONT_FZJZ, 25, 18);
			DrawFittedCenteredText(gameApp, u8"开局 60 秒布阵；全程没有阳光补给，记得留钱救场。",
				650.0f, 342.0f, 670.0f, glm::vec4(46, 46, 84, 255),
				ResourceKeys::Fonts::FONT_FZJZ, 21, 16);
			DrawFittedCenteredText(gameApp, u8"大混战：冷藏站地图，3000 阳光，对手带 850 冰，兵种逐波解锁。",
				650.0f, 402.0f, 710.0f, glm::vec4(46, 46, 84, 255),
				ResourceKeys::Fonts::FONT_FZJZ, 23, 16);
		}
		if (GetPageCount() > 1) {
			const std::string pageText = std::to_string(mCurrentPage + 1) + " / "
				+ std::to_string(GetPageCount());
			DrawFittedCenteredText(gameApp, pageText, 550.0f, 573.0f, 160.0f,
				glm::vec4(52, 51, 93, 255), ResourceKeys::Fonts::FONT_FZJZ, 18, 14);
		}
	}, LAYER_UI + 100);

	SortDrawCommands();
}

void GameSelectScene::BuildAvailableLevels()
{
	mAvailableLevels.clear();
	if (mSelectMode == SelectMode::MINIGAMES) {
		mAvailableLevels.push_back(MiniGame::LAST_SAVINGS_LEVEL);
		mAvailableLevels.push_back(MiniGame::BRAWL_LEVEL);
		return;
	}
	if (mSelectMode == SelectMode::ADVENTURE) {
		const int unlockedLevel = std::clamp(GameAPP::GetInstance().mAdventureLevel,
			1, AdventureProgression::LAST_ADVENTURE_LEVEL);
		mAvailableLevels.reserve(static_cast<std::size_t>(unlockedLevel));
		for (int level = 1; level <= unlockedLevel; ++level) {
			mAvailableLevels.push_back(level);
		}
		return;
	}

	mAvailableLevels.reserve(SURVIVAL_ENDLESS_DEFINITIONS.size());
	for (const auto& definition : SURVIVAL_ENDLESS_DEFINITIONS) {
		if (AdventureProgression::HasCompletedArea(
			GameAPP::GetInstance().mAdventureLevel,
			definition.requiredAdventureArea)) {
			mAvailableLevels.push_back(definition.level);
		}
	}
}

int GameSelectScene::GetPageCount() const
{
	return std::max(1, static_cast<int>((mAvailableLevels.size()
		+ SELECT_ENTRIES_PER_PAGE - 1) / SELECT_ENTRIES_PER_PAGE));
}

bool GameSelectScene::IsAdventureLevelCompleted(int level) const
{
	return mSelectMode == SelectMode::ADVENTURE
		&& AdventureProgression::IsAdventureLevel(level)
		&& level < GameAPP::GetInstance().mAdventureLevel;
}

void GameSelectScene::CreateCurrentPageCards()
{
	for (const auto& card : mCards) {
		mUIManager.RemoveButton(card);
	}
	mCards.clear();
	mCurrentPageLevels.clear();

	const std::size_t first = static_cast<std::size_t>(mCurrentPage)
		* SELECT_ENTRIES_PER_PAGE;
	const std::size_t last = std::min(first + SELECT_ENTRIES_PER_PAGE,
		mAvailableLevels.size());
	if (first >= last) return;
	mCurrentPageLevels.assign(mAvailableLevels.begin() + first,
		mAvailableLevels.begin() + last);

	for (std::size_t index = 0; index < mCurrentPageLevels.size(); ++index) {
		const int enterLevel = mCurrentPageLevels[index];
		const Vector position = GetCardPosition(index);
		auto card = mUIManager.CreateButton(position, Vector(SELECT_CARD_WIDTH, SELECT_CARD_HEIGHT));
		card->SetAsCheckbox(false);
		card->SetImageKeys(ResourceKeys::Textures::IMAGE_CHALLENGE_WINDOW,
			ResourceKeys::Textures::IMAGE_CHALLENGE_WINDOW_HIGHLIGHT,
			ResourceKeys::Textures::IMAGE_CHALLENGE_WINDOW_HIGHLIGHT,
			ResourceKeys::Textures::IMAGE_CHALLENGE_WINDOW_HIGHLIGHT);
		card->SetClickCallBack([this, enterLevel](bool) {
			mPendingEnterLevel = enterLevel;
		});
		mCards.push_back(std::move(card));
	}
}

void GameSelectScene::RefreshPageButtonState()
{
	const int pageCount = GetPageCount();
	mCurrentPage = std::clamp(mCurrentPage, 0, pageCount - 1);
	if (mPreviousPageButton) {
		const bool visible = mCurrentPage > 0;
		mPreviousPageButton->SetEnabled(visible);
		mPreviousPageButton->SetSkipDraw(!visible);
		mPreviousPageButton->SetImageRotationDegrees(PAGE_BACK_ROTATION);
	}
	if (mNextPageButton) {
		const bool visible = mCurrentPage + 1 < pageCount;
		mNextPageButton->SetEnabled(visible);
		mNextPageButton->SetSkipDraw(!visible);
		mNextPageButton->SetImageRotationDegrees(PAGE_FORWARD_ROTATION);
	}
	if (mSkipLevelButton) {
		const bool visible = GetSkippableLevel() > 0;
		mSkipLevelButton->SetEnabled(visible);
		mSkipLevelButton->SetSkipDraw(!visible);
	}
}

int GameSelectScene::GetSkippableLevel() const
{
	const int level = GameAPP::GetInstance().mAdventureLevel;
	return mSelectMode == SelectMode::ADVENTURE
		&& AdventureProgression::IsAdventureLevel(level)
		&& std::find(mCurrentPageLevels.begin(), mCurrentPageLevels.end(), level)
			!= mCurrentPageLevels.end() ? level : -1;
}

void GameSelectScene::ConfirmSkipLevel()
{
	const int level = GetSkippableLevel();
	if (level < 0) return;
	const std::string message = u8"跳过第 " + GetLevelLabel(mSelectMode, level)
		+ u8" 关，领取本关奖励并解锁下一关？";
	GameMessageBox::Builder(Vector(SCENE_WIDTH / 2, SCENE_HEIGHT / 2))
		.Title(u8"跳过本关")
		.Message(message)
		.Scale(SKIP_DIALOG_SCALE)
		.Button(u8"确认跳过", Vector::zero(), Vector(100, 42), 18,
			[this, level]() { mPendingSkipLevel = level; })
		.Button(u8"取消", Vector::zero(), Vector(100, 42), 18, []() {})
		.Show();
}

void GameSelectScene::CompleteSkippedLevel(int level)
{
	// 只提交确认框里显示的关卡；即使进度在确认期间变化，也不能顺带跳下一关。
	if (level != GetSkippableLevel()) return;
	auto& app = GameAPP::GetInstance();
	const auto oldCardCount = app.mHaveCards.size();
	const PlantType reward = AdventureProgression::AdvanceProgress(level,
		app.mAdventureLevel, app.mHaveCards);
	if (!app.mGameInfoSaver.SavePlayerInfo()) {
		// 保存失败不消耗跳关，不删除原续局；允许玩家关闭提示后重试。
		app.mAdventureLevel = level;
		app.mHaveCards.resize(oldCardCount);
		GameMessageBox::Builder(Vector(SCENE_WIDTH / 2, SCENE_HEIGHT / 2))
			.Title(u8"保存失败")
			.Message(u8"未能保存进度，本关尚未跳过，请重试。")
			.Scale(SKIP_DIALOG_SCALE)
			.Button(u8"确定", Vector::zero(), Vector(100, 42), 18, []() {})
			.Show();
		return;
	}
	app.mGameInfoSaver.DeleteLevelData(level);
	if (reward != AdventureProgression::NO_PLANT_REWARD) {
		auto& scenes = SceneManager::GetInstance();
		scenes.RegisterScene<PlantAlmanacScene>("PlantRewardScene", reward);
		scenes.SwitchTo("PlantRewardScene");
		return;
	}

	// 没有新卡时留在选关页；跨页解锁后自动定位新的待挑战关。
	BuildAvailableLevels();
	mCurrentPage = GetPageCount() - 1;
	CreateCurrentPageCards();
	RefreshPageButtonState();
}

std::shared_ptr<Button> GameSelectScene::GetCardButton(int level) const
{
	for (std::size_t index = 0; index < mCurrentPageLevels.size(); ++index) {
		if (mCurrentPageLevels[index] == level && index < mCards.size()) {
			return mCards[index];
		}
	}
	return nullptr;
}

void GameSelectScene::Update()
{
	Scene::Update();

	if (mPendingSkipLevel > 0) {
		const int level = mPendingSkipLevel;
		mPendingSkipLevel = -1;
		CompleteSkippedLevel(level);
		return; // 奖励页切换可能已经销毁本场景，禁止继续访问成员。
	}

	// 按钮回调只登记翻页请求；离开 ButtonManager 遍历后再安全替换当前页按钮。
	if (mPendingPageDelta != 0) {
		const int targetPage = std::clamp(mCurrentPage + mPendingPageDelta,
			0, GetPageCount() - 1);
		mPendingPageDelta = 0;
		if (targetPage != mCurrentPage) {
			mCurrentPage = targetPage;
			CreateCurrentPageCards();
			RefreshPageButtonState();
		}
	}

	if (mReadyToSwitchMainMenu) {
		mReadyToSwitchMainMenu = false;
		SceneManager::GetInstance().SwitchTo("MainMenuScene");
		return;
	}
	if (mPendingEnterLevel >= 0) {
		const int enterLevel = mPendingEnterLevel;
		mPendingEnterLevel = -1;
		auto& gameApp = GameAPP::GetInstance();
		auto& sceneManager = SceneManager::GetInstance();
		gameApp.GetGraphics().SetCameraPosition(0, 0);
		sceneManager.SetGlobalData("EnterLevel", std::to_string(enterLevel));
		sceneManager.SwitchTo("GameScene");
	}
}

void GameSelectScene::OnEnter()
{
	const std::string mode = SceneManager::GetInstance().GetGlobalData(
		"GameSelectMode", "survival");
	mSelectMode = mode == "adventure" ? SelectMode::ADVENTURE
		: (mode == "minigames" ? SelectMode::MINIGAMES : SelectMode::SURVIVAL);
	mCurrentPage = 0;
	mPendingPageDelta = 0;
	mPendingEnterLevel = -1;
	mReadyToSwitchMainMenu = false;
	mPendingSkipLevel = -1;
	BuildAvailableLevels();
	if (mSelectMode == SelectMode::ADVENTURE && !mAvailableLevels.empty()) {
		// 冒险入口默认展示当前可挑战关所在页；全部通关后自然停在最后一页。
		mCurrentPage = static_cast<int>((mAvailableLevels.size() - 1)
			/ SELECT_ENTRIES_PER_PAGE);
	}
	Scene::OnEnter();
	AudioSystem::PlayMusic(ResourceKeys::Music::MUSIC_CHOOSEYOURSEEDS, -1);
}

void GameSelectScene::OnExit()
{
	mBackMenuButton.reset();
	mPreviousPageButton.reset();
	mNextPageButton.reset();
	mSkipLevelButton.reset();
	mCards.clear();
	mAvailableLevels.clear();
	mCurrentPageLevels.clear();
	Scene::OnExit();
}
