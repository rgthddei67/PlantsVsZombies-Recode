#include "ChooseCardUI.h"
#include "SceneManager.h"
#include "../ResourceKeys.h"
#include "../ResourceManager.h"
#include "GameScene.h"
#include "./Plant/PlantType.h"
#include "./Plant/GameDataManager.h"
#include "./Plant/PlantUpgradeRules.h"
#include "./AudioSystem.h"
#include "../GameRandom.h"
#include "CardSlotManager.h"
#include "../GameApp.h"
#include <algorithm>
#include <memory>

namespace {
	constexpr float kStartButtonX = 360.0f; // “一起摇滚吧”按钮左上角 X，单位：UI px
	constexpr float kStartButtonY = 550.0f; // “一起摇滚吧”按钮左上角 Y，单位：UI px
	constexpr float kStartButtonWidth = 156.0f * 0.9f; // “一起摇滚吧”按钮宽度，单位：UI px
	constexpr float kStartButtonHeight = 42.0f * 0.9f; // “一起摇滚吧”按钮高度，单位：UI px
	constexpr float kRestoreButtonScale = 0.8f; // Button2 原图缩放倍率，避免遮挡面板右上角装饰
	constexpr float kRestoreButtonTextureWidth = 111.0f; // SeedChooser_Button2 原图宽度，单位：UI px
	constexpr float kRestoreButtonTextureHeight = 26.0f; // SeedChooser_Button2 原图高度，单位：UI px
	constexpr float kRestoreButtonRightInset = 12.0f; // 按钮右边缘距选卡面板右边缘，单位：UI px
	constexpr float kRestoreButtonTopInset = 8.0f; // 按钮上边缘距选卡面板上边缘，单位：UI px
	constexpr float kPageButtonSize = 60.0f; // Zen_NextGarden 原图边长，单位：UI px
	constexpr float kPageButtonStartGap = 10.0f; // 翻页箭头与开始按钮边缘间距，单位：UI px
	constexpr float kPageForwardRotation = 0.0f; // 第一页向右箭头旋转角度，单位：度
	constexpr float kPageBackRotation = 180.0f; // 第二页向左箭头旋转角度，单位：度
	constexpr float kImitaterDialogX = 290.0f; // 模仿者目标面板左上角 X，单位：UI px
	constexpr float kImitaterDialogY = 42.0f; // 模仿者目标面板左上角 Y，单位：UI px
	constexpr float kImitaterDialogWidth = 520.0f; // 模仿者目标面板宽度，单位：UI px
	constexpr float kImitaterDialogHeight = 496.0f; // 模仿者目标面板高度，单位：UI px
	constexpr float kImitaterFooterHeight = 48.0f; // 面板底部导航预留高度，单位：UI px
	constexpr float kImitaterPageButtonSize = 40.0f; // 弹窗导航箭头边长，单位：UI px
	constexpr float kImitaterCancelX = 718.0f; // 模态取消按钮左上角 X，单位：UI px
	constexpr float kImitaterCancelY = 50.0f; // 模态取消按钮左上角 Y，单位：UI px
	constexpr float kImitaterCancelWidth = 76.0f; // 模态取消按钮宽度，单位：UI px
	constexpr float kImitaterCancelHeight = 25.0f; // 模态取消按钮高度，单位：UI px
	constexpr float kImitaterCardX = 673.0f; // AddOn 左缘=665，精确贴住选卡面板右边缘，单位：UI px
	constexpr float kImitaterCardY = 511.0f; // AddOn 底缘=593，与选卡面板底边对齐，单位：UI px
	constexpr float kImitaterAddOnX = 665.0f; // 固定背景左缘紧贴选卡面板，单位：UI px
	constexpr float kImitaterAddOnY = 500.0f; // 固定背景顶缘，单位：UI px
	constexpr float kImitaterAddOnWidth = 66.0f; // 原始 AddOn 宽度，禁止拉伸，单位：UI px
	constexpr float kImitaterAddOnHeight = 93.0f; // 原始 AddOn 高度，禁止拉伸，单位：UI px
	constexpr RenderLayer kImitaterDialogLayer =
		static_cast<RenderLayer>(LAYER_UI + 1000); // 高于同层全部主卡，低于 Scene UI 按钮
	constexpr RenderLayer kImitaterDialogCardLayer =
		static_cast<RenderLayer>(LAYER_UI + 2000); // 临时目标卡位于模态背景之上
}

/** 把模态遮罩放在普通选卡 Card 之上、临时目标 Card 之下。 */
class ImitaterDialogOverlay final : public GameObject {
public:
	explicit ImitaterDialogOverlay(ChooseCardUI* owner) : mOwner(owner) {
		mIsUI = true;
	}

	void Draw(Graphics* g) override {
		if (mOwner) mOwner->DrawImitaterDialog(g);
	}

	void DetachOwner() { mOwner = nullptr; }

private:
	ChooseCardUI* mOwner = nullptr; // ChooseCardUI 管理本对象生命周期
};

ChooseCardUI::ChooseCardUI(GameScene* gameScene)
{
	this->mIsUI = true;
	this->SetName("ChooseCardUI");
	mCards.reserve(64);
	mSelectedCards.reserve(16);
	mImitaterDialogCards.reserve(48);
	mGameScene = gameScene;
	if (!mGameScene) return;
	CreateTransform(60.0f, 800.0f);

	mCardUITexture = ResourceManager::GetInstance().
		GetTexture(ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BACKGROUND);
	mImitaterAddOnTexture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_SEEDCHOOSER_IMITATERADDON);
	auto button = mGameScene->GetUIManager().CreateButton(
		Vector(kStartButtonX, kStartButtonY),
		Vector(kStartButtonWidth, kStartButtonHeight));
	mButton = button;
	button->SetAsCheckbox(false);
	button->SetImageKeys(ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON_DISABLED,
		ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON,
		ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON);
	button->SetTextColor({ 211, 157, 42 ,255 });
	button->SetHoverTextColor({ 211, 157, 42 ,255 });
	button->SetText(u8"  一起摇滚吧！", ResourceKeys::Fonts::FONT_FZCQ, 20);
	button->SetEnabled(false);
	button->SetClickCallBack([this](bool isChecked) {
		if (mGameScene) {
			mGameScene->ChooseCardComplete();
		}
		});

	auto restoreButton = mGameScene->GetUIManager().CreateButton(
		Vector::zero(),
		Vector(kRestoreButtonTextureWidth * kRestoreButtonScale,
			kRestoreButtonTextureHeight * kRestoreButtonScale));
	mRestoreButton = restoreButton;
	restoreButton->SetAsCheckbox(false);
	restoreButton->SetImageKeys(ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON2,
		ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON2_GLOW,
		ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON2);
	restoreButton->SetText(u8"上次选卡", ResourceKeys::Fonts::FONT_FZCQ, 13);
	restoreButton->SetTextColor({ 42, 42, 90, 255 });
	restoreButton->SetHoverTextColor({ 42, 42, 90, 255 });
	restoreButton->SetEnabled(false);
	restoreButton->SetClickCallBack([this](bool) {
		RestoreLastSelectedCards();
		});

	auto pageButton = mGameScene->GetUIManager().CreateButton(
		Vector::zero(), Vector(kPageButtonSize, kPageButtonSize));
	mPageButton = pageButton;
	pageButton->SetAsCheckbox(false);
	pageButton->SetImageKeys(ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
		ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
		ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN);
	pageButton->SetEnabled(false);
	pageButton->SetSkipDraw(true);
	pageButton->SetClickCallBack([this](bool) {
		TogglePage();
		});

	auto cancelButton = mGameScene->GetUIManager().CreateButton(
		Vector(kImitaterCancelX, kImitaterCancelY),
		Vector(kImitaterCancelWidth, kImitaterCancelHeight));
	mImitaterCancelButton = cancelButton;
	cancelButton->SetAsCheckbox(false);
	cancelButton->SetImageKeys(ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON2,
		ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON2_GLOW,
		ResourceKeys::Textures::IMAGE_SEEDCHOOSER_BUTTON2);
	cancelButton->SetText(u8"取消", ResourceKeys::Fonts::FONT_FZCQ, 14);
	cancelButton->SetEnabled(false);
	cancelButton->SetSkipDraw(true);
	cancelButton->SetClickCallBack([this](bool) {
		CloseImitaterDialog();
		});
	// 页脚位于五行卡片之后；两个独立方向也支持以后继续增加候选页。
	for (const int direction : { -1, 1 }) {
		auto navigation = mGameScene->GetUIManager().CreateButton(
			Vector(kImitaterDialogX + kImitaterDialogWidth * 0.5f
				+ direction * 90.0f - kImitaterPageButtonSize * 0.5f,
				kImitaterDialogY + kImitaterDialogHeight - kImitaterFooterHeight + 4.0f),
			Vector(kImitaterPageButtonSize, kImitaterPageButtonSize));
		if (direction < 0) mImitaterPreviousPageButton = navigation;
		else mImitaterNextPageButton = navigation;
		navigation->SetAsCheckbox(false);
		navigation->SetImageKeys(ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
			ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN,
			ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN);
		navigation->SetImageRotationDegrees(direction < 0
			? kPageBackRotation : kPageForwardRotation);
		navigation->SetEnabled(false);
		navigation->SetSkipDraw(true);
		navigation->SetClickCallBack([this, direction](bool) {
			ChangeImitaterDialogPage(direction);
			});
	}
	SyncRestoreButtonPosition();
	SyncPageButtonPosition();
}

ChooseCardUI::~ChooseCardUI() {
	DestroyImitaterDialogCards();
	DestroyImitaterDialogOverlay();
	SceneManager::GetInstance().GetCurrectSceneUIManager().RemoveButton(mButton.lock());
	SceneManager::GetInstance().GetCurrectSceneUIManager().RemoveButton(mRestoreButton.lock());
	SceneManager::GetInstance().GetCurrectSceneUIManager().RemoveButton(mPageButton.lock());
	SceneManager::GetInstance().GetCurrectSceneUIManager().RemoveButton(
		mImitaterCancelButton.lock());
	SceneManager::GetInstance().GetCurrectSceneUIManager().RemoveButton(mImitaterPreviousPageButton.lock());
	SceneManager::GetInstance().GetCurrectSceneUIManager().RemoveButton(mImitaterNextPageButton.lock());
	mGameScene = nullptr;
	for (Card* card : mCards) {
		if (card) card->BindChooseCardUI(nullptr);
	}
	if (mImitaterCard) mImitaterCard->BindChooseCardUI(nullptr);
	// TODO: 物体析构的时候，如果有其他物体没有销毁，不要在这个时候销毁，因为没用
}

void ChooseCardUI::Update() {
	GameObject::Update();
	SyncRestoreButtonPosition();
	SyncPageButtonPosition();
}

void ChooseCardUI::SetPosition(const Vector& position) {
	if (GetTransform()) GetTransform()->SetPosition(position);
	SyncRestoreButtonPosition();
	SyncPageButtonPosition();
}

void ChooseCardUI::RemoveAllCards() {
	if (mImitaterDialogOpen) CloseImitaterDialog();
	for (auto* card : mCards) {
		if (card) card->BindChooseCardUI(nullptr);
		GameObjectManager::GetInstance().DestroyGameObject(card);
	}
	mCards.clear();
	if (mImitaterCard) {
		mImitaterCard->BindChooseCardUI(nullptr);
		GameObjectManager::GetInstance().DestroyGameObject(mImitaterCard);
		mImitaterCard = nullptr;
	}
	mSelectedCards.clear();
	mCurrentPage = 0;
	RefreshPageButtonState();
}

void ChooseCardUI::TransferSelectedCardsTo(CardSlotManager* manager) {
	for (auto* card : mSelectedCards) {
		card->BindChooseCardUI(nullptr);
		// 设置卡牌状态为游戏内
		card->SetIsInChooseCardUI(false);
		// 添加到卡槽管理器
		if (manager) {
			manager->AddCard(card);
		}
		// 从 mCards 中移除
		auto it = std::find(mCards.begin(), mCards.end(), card);
		if (it != mCards.end()) {
			mCards.erase(it);
		}
		if (card == mImitaterCard) mImitaterCard = nullptr;
	}
	mSelectedCards.clear();
	RefreshPageButtonState();
	SyncCardPageVisibility();
}

void ChooseCardUI::Draw(Graphics* g) {
	if (!g) return;
	// 绘制背景
	if (mCardUITexture) {
		Vector pos = this->GetPosition();
		Vector newpos = g->LogicalToWorld(pos.x, pos.y);
		int w = mCardUITexture->width;
		int h = mCardUITexture->height;

		g->DrawTexture(mCardUITexture,
			newpos.x, newpos.y,
			static_cast<float>(w), static_cast<float>(h));
	}
	if (mGameScene && MiniGame::IsMiniGame(mGameScene->GetBoard()->mLevel)) {
		// 文字与面板背景同属 UI：先转世界坐标，抵消选卡阶段的镜头平移。
		const Vector logical = GetPosition();
		const Vector pos = g->LogicalToWorld(logical.x, logical.y);
		auto& app = GameAPP::GetInstance();
		app.DrawText(MiniGame::NAME, pos + Vector(105, 155), {255, 221, 130, 255},
			ResourceKeys::Fonts::FONT_FZJZ, 32);
		const std::array<const char*, 5> lines = {
			u8"3000 阳光，就是你全部的家底。",
			u8"开局 60 秒布阵，守住十波进攻。",
			u8"整局没有阳光补给，铲除不退款。",
			u8"七张卡已备好，留些预算用于救场。",
			u8"准备好了，就一起摇滚吧！"
		};
		for (std::size_t i = 0; i < lines.size(); ++i) {
			app.DrawText(lines[i], pos + Vector(35, 220 + static_cast<float>(i) * 42),
				{245, 216, 169, 255}, ResourceKeys::Fonts::FONT_FZJZ, 20);
		}
	}
	// 原版 AddOn 属于 SeedChooser 背景：模仿卡飞到卡槽后，右侧木框仍留在原处。
	if (mImitaterAddOnTexture && mImitaterCard) {
		const Vector addOn = g->LogicalToWorld(kImitaterAddOnX, kImitaterAddOnY);
		g->DrawTexture(mImitaterAddOnTexture, addOn.x, addOn.y,
			kImitaterAddOnWidth, kImitaterAddOnHeight);
	}
}

void ChooseCardUI::DrawImitaterDialog(Graphics* g) const
{
	if (!mImitaterDialogOpen || !g) return;
	const Vector screenTopLeft = g->LogicalToWorld(0.0f, 0.0f);
	g->FillRect(screenTopLeft.x, screenTopLeft.y, 1100.0f, 600.0f,
		glm::vec4(18.0f, 10.0f, 7.0f, 178.0f));
	const Vector panel = g->LogicalToWorld(kImitaterDialogX, kImitaterDialogY);
	const bool multiplePages = GetImitaterDialogPageCount() > 1;
	const float panelHeight = kImitaterDialogHeight;
	g->FillRect(panel.x, panel.y, kImitaterDialogWidth, panelHeight,
		glm::vec4(104.0f, 48.0f, 24.0f, 248.0f));
	g->DrawRect(panel.x, panel.y, kImitaterDialogWidth, panelHeight,
		glm::vec4(244.0f, 145.0f, 48.0f, 255.0f));
	g->DrawRect(panel.x + 3.0f, panel.y + 3.0f,
		kImitaterDialogWidth - 6.0f, panelHeight - 6.0f,
		glm::vec4(74.0f, 29.0f, 16.0f, 255.0f));
	const std::string title = u8"选择模仿的植物";
	const float titleWidth = g->MeasureTextWidth(
		title, ResourceKeys::Fonts::FONT_FZCQ, 24);
	g->DrawGlyphRun(title, ResourceKeys::Fonts::FONT_FZCQ, 24,
		glm::vec4(255.0f, 198.0f, 62.0f, 255.0f),
		panel.x + (kImitaterDialogWidth - titleWidth) * 0.5f,
		panel.y + 9.0f);
	if (multiplePages) {
		const std::string page = std::to_string(mImitaterDialogPage + 1)
			+ " / " + std::to_string(GetImitaterDialogPageCount());
		const float width = g->MeasureTextWidth(page, ResourceKeys::Fonts::FONT_FZCQ, 20);
		g->DrawGlyphRun(page, ResourceKeys::Fonts::FONT_FZCQ, 20,
			glm::vec4(255.0f, 198.0f, 62.0f, 255.0f),
			panel.x + (kImitaterDialogWidth - width) * 0.5f,
			panel.y + kImitaterDialogHeight - kImitaterFooterHeight + 12.0f);
	}
}

void ChooseCardUI::AddCard(PlantType type) {
	if (type == PlantType::PLANT_IMITATER && mImitaterCard) return;
	// 计算当前卡牌数量对应的行列
	int cardCount = static_cast<int>(mCards.size());
	int pageSlot = cardCount % CARDS_PER_PAGE;
	int row = pageSlot / MAX_CARDS_PER_ROW;
	int col = pageSlot % MAX_CARDS_PER_ROW;

	// 计算位置
	float posX = type == PlantType::PLANT_IMITATER
		? kImitaterCardX : START_X + col * (CARD_WIDTH + CARD_HORIZONTAL_SPACING);
	float posY = type == PlantType::PLANT_IMITATER
		? kImitaterCardY : START_Y + row * (CARD_HEIGHT + CARD_VERTICAL_SPACING);

	auto& gameMgr = GameDataManager::GetInstance();

	auto card = GameObjectManager::GetInstance().
		CreateGameObjectImmediate<Card>(LAYER_UI, type,
			gameMgr.GetPlantSunCost(type), gameMgr.GetPlantCooldown(type), true);

	if (auto transform = card->GetTransform()) {
		transform->SetPosition(Vector(posX, posY));
	}
	card->SetOriginalPosition(Vector(posX, posY));
	card->BindChooseCardUI(this);
	card->mIsUI = true;
	if (type == PlantType::PLANT_IMITATER) mImitaterCard = card;
	else mCards.push_back(card);
	RefreshPageButtonState();
	SyncCardPageVisibility();
}

void ChooseCardUI::RemoveCard(Card* card)
{
	if (card == mImitaterCard) mImitaterCard = nullptr;
	auto it = std::find(mCards.begin(), mCards.end(), card);
	if (it != mCards.end()) {
		card->BindChooseCardUI(nullptr);
		mCards.erase(it);
	}

	// 如果已选中，也从选中列表中移除
	auto selIt = std::find(mSelectedCards.begin(), mSelectedCards.end(), card);
	if (selIt != mSelectedCards.end()) {
		mSelectedCards.erase(selIt);
		UpdateTargetPositions();
	}

	GameObjectManager::GetInstance().DestroyGameObject(card);
	RefreshPageButtonState();
	SyncCardPageVisibility();
}

void ChooseCardUI::AddAllCard() {
	// 试玩卡池独立于冒险拥有记录，并预选全部七张，玩家可直接开始。
	if (mGameScene && MiniGame::IsMiniGame(mGameScene->GetBoard()->mLevel)) {
		for (PlantType type : MiniGame::CARDS) {
			AddCard(type);
			ToggleCardSelection(FindCardByType(type));
		}
		RefreshRestoreButtonState();
		RefreshPageButtonState();
		SyncCardPageVisibility();
		return;
	}
	const auto& haveCards = GameAPP::GetInstance().mHaveCards;
	auto& gameData = GameDataManager::GetInstance();
	for (const auto& card : haveCards) {
		// 冒险进度可以先记录后续关卡奖励；对应植物尚未实装时先不把空工厂卡放进选卡界面。
		if (!gameData.HasPlant(card)) continue;
		AddCard(card);
	}
	RefreshRestoreButtonState();
	RefreshPageButtonState();
	SyncCardPageVisibility();
}

Card* ChooseCardUI::FindCardByType(PlantType type) {
	if (mImitaterDialogOpen) {
		for (Card* option : mImitaterDialogCards) {
			if (option && option->HasImitaterTarget()
				&& option->GetImitaterTarget() == type) {
				return option;
			}
		}
	}
	if (type == PlantType::PLANT_IMITATER) return mImitaterCard;
	for (auto* card : mCards) {
		if (!card) continue;
		if (card->GetPlantType() == type) return card;
	}
	return nullptr;
}

bool ChooseCardUI::ToggleCardSelection(Card* card) {
	if (!card) return false;

	auto it = std::find(mSelectedCards.begin(), mSelectedCards.end(), card);
	if (it != mSelectedCards.end()) {
		// 已选中 -> 移除
		mSelectedCards.erase(it);
		if (card->GetPlantType() == PlantType::PLANT_IMITATER) {
			card->ClearImitaterTarget();
		}
		UpdateTargetPositions();
		return false;
	}
	else {
		// 未选中 -> 添加，检查数量限制
		if (mSelectedCards.size() >= MAX_SELECTED) {
			return false;
		}
		mSelectedCards.push_back(card);
		UpdateTargetPositions();
		return true;
	}
}

bool ChooseCardUI::HandleCardClick(Card* card)
{
	if (!card) return false;
	if (mImitaterDialogOpen) return SelectImitaterTarget(card);
	if (card->GetPlantType() != PlantType::PLANT_IMITATER) {
		return ToggleCardSelection(card);
	}
	if (IsCardSelected(card)) {
		ToggleCardSelection(card);
		return false;
	}
	return OpenImitaterDialog(card);
}

std::vector<PlantType> ChooseCardUI::GetSelectedCardTypes() {
	std::vector<PlantType> types;
	types.reserve(mSelectedCards.size());
	for (Card* card : mSelectedCards) {
		if (!card) continue;
		types.push_back(card->GetPlantType());
	}
	return types;
}

std::vector<std::string> ChooseCardUI::GetSelectedCardKeys() const
{
	std::vector<std::string> keys;
	keys.reserve(mSelectedCards.size());
	auto& gameData = GameDataManager::GetInstance();
	for (const Card* card : mSelectedCards) {
		if (!card) continue;
		const std::string identity = gameData.PlantTypeToEnumName(card->GetPlantType());
		if (identity == "PLANT_NONE") continue;
		if (card->GetPlantType() == PlantType::PLANT_IMITATER
			&& card->HasImitaterTarget()) {
			keys.push_back(identity + ":"
				+ gameData.PlantTypeToEnumName(card->GetImitaterTarget()));
		}
		else {
			keys.push_back(identity);
		}
	}
	return keys;
}

std::vector<Card*> ChooseCardUI::ResolveRestorableCards(bool applyImitaterTargets) {
	std::vector<Card*> cards;
	cards.reserve(MAX_SELECTED);
	std::vector<PlantType> seenTypes;
	seenTypes.reserve(MAX_SELECTED);
	auto& gameData = GameDataManager::GetInstance();
	for (const std::string& cardKey : GameAPP::GetInstance().mLastSelectedCards) {
		const std::size_t separator = cardKey.find(':');
		const std::string cardName = cardKey.substr(0, separator);
		const PlantType type = gameData.StringToPlantType(cardName);
		if (type == PlantType::NUM_PLANT_TYPES
			|| gameData.PlantTypeToEnumName(type) != cardName
			|| std::find(seenTypes.begin(), seenTypes.end(), type) != seenTypes.end()) {
			continue;
		}
		Card* card = FindCardByType(type);
		if (!card) continue;
		if (type == PlantType::PLANT_IMITATER) {
			if (separator == std::string::npos) continue;
			const std::string targetName = cardKey.substr(separator + 1);
			const PlantType target = gameData.StringToPlantType(targetName);
			if (target == PlantType::NUM_PLANT_TYPES
				|| gameData.PlantTypeToEnumName(target) != targetName
				|| (target == PlantType::PLANT_IMITATER || target == PlantType::PLANT_CARRYVINE)
				|| IsUpgradePlantType(target)
				|| !gameData.HasPlant(target)
				|| !FindCardByType(target)) {
				continue;
			}
			if (applyImitaterTargets && !card->SetImitaterTarget(target)) continue;
		}
		seenTypes.push_back(type);
		cards.push_back(card);
		if (cards.size() >= MAX_SELECTED) break;
	}
	return cards;
}

bool ChooseCardUI::RestoreLastSelectedCards() {
	auto restoredCards = ResolveRestorableCards(true);
	if (restoredCards.empty()) return false;

	// 整组替换若不再包含模仿者，也要清掉旧目标，保持右侧入口卡恢复原貌。
	if (mImitaterCard
		&& std::find(restoredCards.begin(), restoredCards.end(), mImitaterCard)
			== restoredCards.end()) {
		mImitaterCard->ClearImitaterTarget();
	}
	// 整组替换当前选择后只刷新一次目标位置，使全部卡片沿既有飞行动画进入对应槽位。
	mSelectedCards = std::move(restoredCards);
	UpdateTargetPositions();
	return true;
}

int ChooseCardUI::GetPageCount() const {
	if (mCards.empty()) return 1;
	return (static_cast<int>(mCards.size()) + CARDS_PER_PAGE - 1)
		/ CARDS_PER_PAGE;
}

std::vector<PlantType> ChooseCardUI::GetVisibleCardTypes() const {
	std::vector<PlantType> types;
	for (Card* card : mCards) {
		if (!card || !card->IsActive()) continue;
		types.push_back(card->GetPlantType());
	}
	return types;
}

std::vector<PlantType> ChooseCardUI::GetHiddenCardTypes() const {
	std::vector<PlantType> types;
	for (Card* card : mCards) {
		if (!card || card->IsActive()) continue;
		types.push_back(card->GetPlantType());
	}
	return types;
}

std::vector<PlantType> ChooseCardUI::GetImitaterDialogOptionTypes() const
{
	std::vector<PlantType> types;
	if (!mImitaterDialogOpen) return types;
	for (const Card* card : mImitaterDialogCards) {
		if (card && card->HasImitaterTarget()) types.push_back(card->GetImitaterTarget());
	}
	return types;
}

int ChooseCardUI::GetImitaterDialogPageCount() const
{
	return std::max(1, (static_cast<int>(mImitaterDialogCards.size())
		+ IMITATER_DIALOG_CARDS_PER_PAGE - 1) / IMITATER_DIALOG_CARDS_PER_PAGE);
}

std::vector<PlantType> ChooseCardUI::GetVisibleImitaterDialogOptionTypes() const
{
	std::vector<PlantType> types;
	for (const Card* card : mImitaterDialogCards) {
		if (card && card->IsActive() && card->HasImitaterTarget()) {
			types.push_back(card->GetImitaterTarget());
		}
	}
	return types;
}

void ChooseCardUI::ChangeImitaterDialogPage(int delta)
{
	if (!mImitaterDialogOpen) return;
	mImitaterDialogPage = std::clamp(mImitaterDialogPage + delta,
		0, GetImitaterDialogPageCount() - 1);
	SyncCardPageVisibility();
	RefreshImitaterDialogControls();
}

void ChooseCardUI::SyncRestoreButtonPosition() {
	auto button = mRestoreButton.lock();
	if (!button || !GetTransform() || !mCardUITexture) return;
	const Vector panelPosition = GetTransform()->GetPosition();
	const float buttonWidth = kRestoreButtonTextureWidth * kRestoreButtonScale;
	button->SetPosition(Vector(
		panelPosition.x + static_cast<float>(mCardUITexture->width)
			- buttonWidth - kRestoreButtonRightInset,
		panelPosition.y + kRestoreButtonTopInset));
}

void ChooseCardUI::SyncPageButtonPosition() {
	auto button = mPageButton.lock();
	if (!button) return;

	// 两页箭头围绕开始按钮对称放置：首页向右继续，次页向左返回。
	const float buttonY =
		kStartButtonY + (kStartButtonHeight - kPageButtonSize) * 0.5f;
	if (mCurrentPage == 0) {
		button->SetPosition(Vector(
			kStartButtonX + kStartButtonWidth + kPageButtonStartGap,
			buttonY));
		button->SetImageRotationDegrees(kPageForwardRotation);
	}
	else {
		button->SetPosition(Vector(
			kStartButtonX - kPageButtonStartGap - kPageButtonSize,
			buttonY));
		button->SetImageRotationDegrees(kPageBackRotation);
	}
}

void ChooseCardUI::RefreshRestoreButtonState() {
	if (auto button = mRestoreButton.lock()) {
		const bool miniGame = mGameScene && MiniGame::IsMiniGame(mGameScene->GetBoard()->mLevel);
		button->SetSkipDraw(miniGame);
		button->SetEnabled(!miniGame && !ResolveRestorableCards(false).empty());
	}
}

void ChooseCardUI::RefreshPageButtonState() {
	const int pageCount = GetPageCount();
	if (mCurrentPage >= pageCount) mCurrentPage = pageCount - 1;
	if (mCurrentPage < 0) mCurrentPage = 0;

	if (auto button = mPageButton.lock()) {
		const bool hasNextPage = pageCount > 1;
		button->SetEnabled(hasNextPage);
		button->SetSkipDraw(!hasNextPage);
	}
	SyncPageButtonPosition();
}

void ChooseCardUI::SyncCardPageVisibility() {
	// 已选卡脱离网格页限制并留在顶部；其余卡只有所属页活动。模态窗打开时
	// 仍保留这些卡的绘制和移动，只关闭碰撞，让它们透过遮罩留在目标窗下面。
	for (size_t i = 0; i < mCards.size(); ++i) {
		Card* card = mCards[i];
		if (!card) continue;
		const bool selected = IsCardSelected(card);
		const bool belongsToCurrentPage =
			static_cast<int>(i / CARDS_PER_PAGE) == mCurrentPage;
		const bool visible = selected || belongsToCurrentPage;
		if (!visible) card->SnapToOriginalPosition();
		card->SetActive(visible);
		card->SetChooseCardInputEnabled(visible && !mImitaterDialogOpen);
	}
	if (mImitaterCard) {
		mImitaterCard->SetActive(true);
		mImitaterCard->SetChooseCardInputEnabled(!mImitaterDialogOpen);
	}
	for (size_t i = 0; i < mImitaterDialogCards.size(); ++i) {
		Card* card = mImitaterDialogCards[i];
		if (!card) continue;
		const bool visible = mImitaterDialogOpen
			&& static_cast<int>(i / IMITATER_DIALOG_CARDS_PER_PAGE) == mImitaterDialogPage;
		card->SetActive(visible);
		card->SetChooseCardInputEnabled(visible);
	}
}

void ChooseCardUI::TogglePage() {
	if (GetPageCount() <= 1) return;
	// 当前完整卡池只有两页；同一个箭头在首页前进、次页返回。
	mCurrentPage = mCurrentPage == 0 ? 1 : 0;
	SyncCardPageVisibility();
	SyncPageButtonPosition();
}

void ChooseCardUI::UpdateTargetPositions(bool playSound) {
	// 为所有卡牌计算目标位置
	if (playSound) {
		int random = GameRandom::Range(0, 1);
		if (random == 0) {
			AudioSystem::PlaySound
			(ResourceKeys::Sounds::SOUND_CHOOSEPLANT1, 0.4f);
		}
		else {
			AudioSystem::PlaySound
			(ResourceKeys::Sounds::SOUND_CHOOSEPLANT2, 0.4f);
		}
	}

	for (size_t i = 0; i < mCards.size(); i++) {
		auto* card = mCards[i];
		Vector targetPos = Vector(0, 0);
		// 检查是否在选中列表中
		auto it = std::find(mSelectedCards.begin(), mSelectedCards.end(), card);
		if (it != mSelectedCards.end()) {
			// 根据在列表中的索引计算槽位位置
			int index = static_cast<int>(it - mSelectedCards.begin());
			targetPos.x = SLOT_START_X + index * SLOT_SPACING;
			targetPos.y = SLOT_START_Y;
		}
		else {
			// 返回原始位置
			targetPos = card->GetOriginalPosition();
		}
		// 设置目标位置，启动动画
		card->SetTargetPosition(targetPos);
	}
	if (mImitaterCard) {
		Vector targetPos = mImitaterCard->GetOriginalPosition();
		auto it = std::find(mSelectedCards.begin(), mSelectedCards.end(), mImitaterCard);
		if (it != mSelectedCards.end()) {
			const int index = static_cast<int>(it - mSelectedCards.begin());
			targetPos = Vector(SLOT_START_X + index * SLOT_SPACING, SLOT_START_Y);
		}
		mImitaterCard->SetTargetPosition(targetPos);
	}
	SyncCardPageVisibility();
}

bool ChooseCardUI::OpenImitaterDialog(Card* imitaterCard)
{
	if (mImitaterDialogOpen || !imitaterCard
		|| imitaterCard->GetPlantType() != PlantType::PLANT_IMITATER
		|| mSelectedCards.size() >= MAX_SELECTED) {
		return false;
	}
	mImitaterDialogOpen = true;
	mImitaterDialogPage = 0;
	mPendingImitaterCard = imitaterCard;
	DestroyImitaterDialogCards();
	DestroyImitaterDialogOverlay();
	mImitaterDialogOverlay = GameObjectManager::GetInstance().
		CreateGameObjectImmediate<ImitaterDialogOverlay>(kImitaterDialogLayer, this);
	auto& gameData = GameDataManager::GetInstance();
	int optionIndex = 0;
	for (const Card* sourceCard : mCards) {
		if (!sourceCard) continue;
		const PlantType target = sourceCard->GetPlantType();
		// C# SeedPacketsWidget 的模仿模式只遍历前 44 张基础卡，不包含紫卡升级。
		if ((target == PlantType::PLANT_IMITATER || target == PlantType::PLANT_CARRYVINE) || IsUpgradePlantType(target)) continue;
		const int pageSlot = optionIndex % IMITATER_DIALOG_CARDS_PER_PAGE;
		const int row = pageSlot / IMITATER_DIALOG_CARDS_PER_ROW;
		const int column = pageSlot % IMITATER_DIALOG_CARDS_PER_ROW;
		Card* option = GameObjectManager::GetInstance().CreateGameObjectImmediate<Card>(
			kImitaterDialogCardLayer, PlantType::PLANT_IMITATER,
			gameData.GetPlantSunCost(PlantType::PLANT_IMITATER),
			gameData.GetPlantCooldown(PlantType::PLANT_IMITATER), true);
		if (!option->SetImitaterTarget(target)) {
			GameObjectManager::GetInstance().DestroyGameObject(option);
			continue;
		}
		const Vector position(
			IMITATER_DIALOG_START_X + column * (CARD_WIDTH + CARD_HORIZONTAL_SPACING),
			IMITATER_DIALOG_START_Y + row * (CARD_HEIGHT + CARD_VERTICAL_SPACING));
		option->SetPositionImmediate(position);
		option->SetOriginalPosition(position);
		option->BindChooseCardUI(this);
		option->mIsUI = true;
		mImitaterDialogCards.push_back(option);
		++optionIndex;
	}
	SyncCardPageVisibility();
	RefreshImitaterDialogControls();
	return true;
}

void ChooseCardUI::CloseImitaterDialog()
{
	if (!mImitaterDialogOpen) return;
	mImitaterDialogOpen = false;
	mPendingImitaterCard = nullptr;
	DestroyImitaterDialogCards();
	DestroyImitaterDialogOverlay();
	RefreshImitaterDialogControls();
	UpdateTargetPositions(false);
}

void ChooseCardUI::DestroyImitaterDialogCards()
{
	for (Card* card : mImitaterDialogCards) {
		if (!card) continue;
		card->SetActive(false);
		card->BindChooseCardUI(nullptr);
		GameObjectManager::GetInstance().DestroyGameObject(card);
	}
	mImitaterDialogCards.clear();
}

void ChooseCardUI::DestroyImitaterDialogOverlay()
{
	if (!mImitaterDialogOverlay) return;
	mImitaterDialogOverlay->DetachOwner();
	mImitaterDialogOverlay->SetActive(false);
	GameObjectManager::GetInstance().DestroyGameObject(mImitaterDialogOverlay);
	mImitaterDialogOverlay = nullptr;
}

bool ChooseCardUI::SelectImitaterTarget(Card* targetCard)
{
	if (!mImitaterDialogOpen || !mPendingImitaterCard || !targetCard
		|| !targetCard->IsActive() || !targetCard->HasImitaterTarget()
		|| std::find(mImitaterDialogCards.begin(), mImitaterDialogCards.end(), targetCard)
			== mImitaterDialogCards.end()) {
		return false;
	}
	const PlantType target = targetCard->GetImitaterTarget();
	if (IsUpgradePlantType(target)) return false;
	Card* imitaterCard = mPendingImitaterCard;
	if (!imitaterCard->SetImitaterTarget(target)) return false;
	CloseImitaterDialog();
	return ToggleCardSelection(imitaterCard);
}

void ChooseCardUI::RefreshImitaterDialogControls()
{
	const bool showPages = mImitaterDialogOpen && GetImitaterDialogPageCount() > 1;
	if (auto button = mImitaterPreviousPageButton.lock()) {
		button->SetSkipDraw(!showPages);
		button->SetEnabled(showPages && mImitaterDialogPage > 0);
	}
	if (auto button = mImitaterNextPageButton.lock()) {
		button->SetSkipDraw(!showPages);
		button->SetEnabled(showPages && mImitaterDialogPage + 1 < GetImitaterDialogPageCount());
	}
	if (auto button = mButton.lock()) {
		button->SetEnabled(!mImitaterDialogOpen);
		button->SetSkipDraw(mImitaterDialogOpen);
	}
	if (auto button = mRestoreButton.lock()) {
		button->SetSkipDraw(mImitaterDialogOpen);
		button->SetEnabled(!mImitaterDialogOpen
			&& !ResolveRestorableCards(false).empty());
	}
	if (auto button = mPageButton.lock()) {
		if (mImitaterDialogOpen) {
			button->SetEnabled(false);
			button->SetSkipDraw(true);
		}
		else {
			RefreshPageButtonState();
		}
	}
	if (auto button = mImitaterCancelButton.lock()) {
		button->SetEnabled(mImitaterDialogOpen);
		button->SetSkipDraw(!mImitaterDialogOpen);
	}
}

bool ChooseCardUI::IsCardSelected(Card* card) const {
	return std::find(mSelectedCards.begin(), mSelectedCards.end(), card) != mSelectedCards.end();
}

float ChooseCardUI::GetGameSlotRightEdge() {
	return SLOT_START_X + static_cast<float>((MAX_SELECTED - 1) * SLOT_SPACING + CARD_WIDTH);
}
