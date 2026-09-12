#include "CardSlotManager.h"
#include "../Logger.h"
#include "../ResourceKeys.h"
#include "GameObject.h"
#include "GameObjectManager.h"
#include "Card.h"
#include "../UI/InputHandler.h"
#include "./Plant/GameDataManager.h"
#include "AudioSystem.h"
#include "./Plant/Plant.h"
#include "./Plant/Blover.h"
#include "./Plant/Imitater.h"
#include "./Plant/Plantern.h"
#include "ShadowComponent.h"
#include "../GameApp.h"
#include "../CursorManager.h"
#include <cmath>

namespace {
	constexpr float kPlanternMenuTopOffset = 74.0f; // 挡位菜单相对卡片顶部的纵向偏移，单位：UI px
	constexpr float kPlanternMenuButtonWidth = 50.0f; // 单个挡位按钮宽度，单位：UI px
	constexpr float kPlanternMenuButtonHeight = 21.0f; // 单个挡位按钮高度，单位：UI px
	constexpr float kPlanternMenuButtonGap = 2.0f; // 相邻挡位按钮的纵向间隔，单位：UI px
	constexpr int kPlanternMenuFontSize = 15; // 挡位标签字号，单位：逻辑 px

	/** 返回挡位菜单的逻辑锚点；始终直接对齐路灯花卡槽下方。 */
	Vector GetPlanternMenuAnchor(Card* card)
	{
		if (!card || !card->GetTransform()) return Vector::zero();
		const Vector cardAnchor = card->GetTransform()->GetPosition();
		return Vector(cardAnchor.x, cardAnchor.y + kPlanternMenuTopOffset);
	}
}

CardSlotManager::CardSlotManager(Board* board)
	: mBoard(board)
{
	if (!mBoard) {
		LOG_WARN("CardSlotManager") << "CardSlotManager created without Board reference!";
	}
}

/** 先解除所有捕获本控制器的回调，再断开 Card/Board 的非拥有引用。 */
CardSlotManager::~CardSlotManager()
{
	if (mBoard && mBoard->mCursorObjectManager.IsActive(CursorObjectType::PLANT_PREVIEW)) {
		mBoard->mCursorObjectManager.ClearActive();
	}
	else {
		DeselectCard();
	}
	mPlanternGearMenuOpen = false;

	if (mBoard) {
		for (int row = 0; row < mBoard->mRows; ++row) {
			for (int col = 0; col < mBoard->mColumns; ++col) {
				if (Cell* cell = mBoard->GetCell(row, col)) {
					cell->SetClickCallback({});
				}
			}
		}
	}
	for (Card* card : cards) {
		if (card) card->BindCardSlotManager(nullptr);
	}
	cards.clear();
	selectedCard = nullptr;
	mBoard = nullptr;
}

void CardSlotManager::Start() {
	// 为所有Cell设置点击回调
	if (mBoard) {
		for (int row = 0; row < mBoard->mRows; ++row) {
			for (int col = 0; col < mBoard->mColumns; ++col) {
				Cell* cell = mBoard->GetCell(row, col);
				if (cell) {
					cell->SetClickCallback([this](int, int) {
						// Cell 碰撞框共用边界且两侧都包含端点；点击也走预览的唯一解析，
						// 避免 ClickableComponent 的渲染顺序选择出另一个相邻格。
						const Vector mouseWorld =
							GameAPP::GetInstance().GetInputHandler().GetMouseWorldPosition();
						if (Cell* clickedCell = FindCellAtWorldPosition(mouseWorld)) {
							HandleCellClick(clickedCell->mRow, clickedCell->mColumn);
						}
						});
				}
			}
		}
	}
}

void CardSlotManager::Update() {
	// 非战斗选卡/词条页与普通暂停都保留已有预览，但不处理任何玩法输入。
	if (!CanAcceptGameplayInput()) {
		return;
	}

	const bool bloverDirectionChanged = UpdateBloverDirectionInput();
	UpdatePlanternGearMenuInput();
	UpdatePlanternHoverCursor();
	UpdateCobCannonHoverCursor();
	UpdateCobCannonTargetingInput();

	// 如果有选中的卡牌，更新鼠标悬停的Cell
	auto* selected = selectedCard;
	if (selected) {
		auto& input = GameAPP::GetInstance().GetInputHandler();
		// 右键取消选择
		if (!bloverDirectionChanged
			&& input.IsMouseButtonPressed(SDL_BUTTON_RIGHT)) {
			DeselectCard();
			mBoard->mCursorObjectManager.ClearActive();
		}
	}

	// 检测阳光变化，更新所有卡牌状态
	if (mBoard && mLastSun != mBoard->GetSun()) {
		mLastSun = mBoard->GetSun();
		UpdateAllCardsState();
	}
}

void CardSlotManager::PrepareDraw(Graphics* g) {
	if (selectedCard) {
		Vector mouseScreen = GameAPP::GetInstance().GetInputHandler().GetMousePosition();

		// 只在本帧最终相机确定后写一次世界坐标，随后 GameObjectManager 直接提交该位置。
		UpdatePlantPreviewPosition(g, mouseScreen);
	}
}

void CardSlotManager::CapturePreviewRenderState(Graphics* g)
{
	mPreviewRenderProbeReady = false;
	mPreviewRenderMouseOffsetX = 0;
	mPreviewRenderMouseOffsetY = 0;
	if (!g || !plantPreview) return;

	const auto animator = plantPreview->GetAnimatorInternal();
	if (!animator) return;
	const AnimatorRenderProbe& probe = animator->GetLastRenderProbe();
	if (!probe.hasGeometry) return;

	// 使用本帧真正提交的视觉基点和仍生效的同一相机反投影；这能区分“变换已在
	// 绘制后修正”与“画面实际贴住鼠标”，不会让下一帧 Transform 状态制造假绿。
	const Vector renderedAnchorWorld =
		Vector(probe.baseX, probe.baseY) - plantPreview->GetStaticVisualOffset();
	const Vector renderedLogical = g->WorldToLogical(
		renderedAnchorWorld.x, renderedAnchorWorld.y);
	const Vector mouseLogical =
		GameAPP::GetInstance().GetInputHandler().GetMousePosition();
	mPreviewRenderMouseOffsetX = static_cast<int>(std::lround(
		renderedLogical.x - mouseLogical.x));
	mPreviewRenderMouseOffsetY = static_cast<int>(std::lround(
		renderedLogical.y - mouseLogical.y));
	mPreviewRenderProbeReady = true;
}

bool CardSlotManager::HasInteractablePlanternAt(int row, int col) const
{
	if (!mBoard) return false;
	Plant* plant = mBoard->GetNormalPlantAt(row, col);
	return plant && plant->IsActive() && !plant->IsSquished()
		&& plant->mPlantType == PlantType::PLANT_PLANTERN
		&& FindPlanternCard();
}

void CardSlotManager::UpdatePlanternHoverCursor() const
{
	// 手持植物、铲子或炮击准星时，路灯花不覆盖当前操作，也不把鼠标改成手型。
	if (!CanAcceptGameplayInput() || selectedCard
		|| mBoard->mCursorObjectManager.GetActiveType() != CursorObjectType::NONE) {
		return;
	}
	const Vector mouseWorld =
		GameAPP::GetInstance().GetInputHandler().GetMouseWorldPosition();
	const Cell* hoveredCell = FindCellAtWorldPosition(mouseWorld);
	if (hoveredCell && HasInteractablePlanternAt(
		hoveredCell->mRow, hoveredCell->mColumn)) {
		CursorManager::GetInstance().IncrementHoverCount();
	}
}

void CardSlotManager::UpdateCobCannonHoverCursor() const
{
	// 拿着植物、铲子或其他场景手持物时，格子点击有更高语义，不能提示或触发炮击。
	if (!CanAcceptGameplayInput() || selectedCard
		|| mBoard->mCursorObjectManager.GetActiveType() != CursorObjectType::NONE) {
		return;
	}
	const Vector mouseWorld =
		GameAPP::GetInstance().GetInputHandler().GetMouseWorldPosition();
	const Cell* hoveredCell = FindCellAtWorldPosition(mouseWorld);
	if (hoveredCell && mBoard->CanBeginCobCannonTargeting(
		hoveredCell->mRow, hoveredCell->mColumn)) {
		CursorManager::GetInstance().IncrementHoverCount();
	}
}

void CardSlotManager::UpdateCobCannonTargetingInput()
{
	if (!mBoard || !mBoard->IsCobCannonTargeting()) {
		mSuppressCobTargetRelease = false;
		return;
	}

	auto& input = GameAPP::GetInstance().GetInputHandler();
	if (input.IsMouseButtonPressed(SDL_BUTTON_RIGHT)) {
		mSuppressCobTargetRelease = false;
		mBoard->mCursorObjectManager.ClearActive();
		return;
	}
	if (mSuppressCobTargetRelease) {
		// 场景可能在本控制器 Update 之后才派发 Cell::onClick；保护标志最多存活到下一步。
		mSuppressCobTargetRelease = false;
		if (input.IsMouseButtonReleased(SDL_BUTTON_LEFT)) return;
	}
	if (!input.IsMouseButtonReleased(SDL_BUTTON_LEFT)) return;
	mBoard->FireTargetedCobCannonAt(input.GetMouseWorldPosition());
}

void CardSlotManager::UpdateAllCardsState() {
	for (auto* card : cards) {
		if (!card) continue;
		card->ForceStateUpdate();
	}
}

void CardSlotManager::AddCard(Card* card) {
	if (!card) return;
	card->BindCardSlotManager(this);
	cards.push_back(card);
}

void CardSlotManager::ClearAllCards() {
	DeselectCard();
	mPlanternGearMenuOpen = false;
	for (auto* card : cards) {
		if (!card) continue;
		card->BindCardSlotManager(nullptr);
		GameObjectManager::GetInstance().DestroyGameObject(card);
	}
	cards.clear();
	selectedCard = nullptr;
}

void CardSlotManager::SelectCard(Card* card) {
	if (!card || !CanAcceptGameplayInput()) return;

	if (!card->IsReady()) {
		return;
	}

	// 数量用尽时与阳光不足一样禁止选中，避免拿起一张全场都无法落下的卡。
	if (!CanUsePlant(card->GetGameplayPlantType(), card->GetSunCost())) {
		return;
	}

	// 如果点击的是已选中的卡牌，取消选择
	if (selectedCard == card) {
		DeselectCard();
		return;
	}

	// 取消之前的选择
	if (selectedCard) {
		selectedCard->SetSelected(false);
	}

	// 通过 CursorObjectManager 清除当前手持物（如铲子）
	mBoard->mCursorObjectManager.Activate(CursorObjectType::PLANT_PREVIEW, [this]() {
		DeselectCard();
		});

	// 选择新卡牌
	selectedCard = card;
	card->SetSelected(true);
	CreatePlantPreview(card->GetGameplayPlantType());
}

void CardSlotManager::DeselectCard() {
	mRelocationSourceID = NULL_PLANT_ID;
	if (selectedCard) {
		selectedCard->SetSelected(false);
		selectedCard = nullptr;
		mHoveredCell = nullptr;
	}
	DestroyPlantPreview();
	DestroyCellPlantPreview();
}

bool CardSlotManager::CanAfford(int cost) const {
	if (GameAPP::mDevelopMode && GameAPP::mDevFreePlant) return true;   // 开发者作弊：无视阳光
	return mBoard ? mBoard->GetSun() >= cost : false;
}

void CardSlotManager::TogglePlanternGearMenu()
{
	if (!CanAcceptGameplayInput()) return;
	if (!mBoard || !mBoard->GetActivePlantern() || !FindPlanternCard()) {
		mPlanternGearMenuOpen = false;
		return;
	}
	DeselectCard();
	mBoard->mCursorObjectManager.ClearActive();
	mPlanternGearMenuOpen = !mPlanternGearMenuOpen;
}

Card* CardSlotManager::FindPlanternCard() const
{
	for (Card* card : cards) {
		if (!card) continue;
		if (card->GetGameplayPlantType() == PlantType::PLANT_PLANTERN) {
			return card;
		}
	}
	return nullptr;
}

void CardSlotManager::UpdatePlanternGearMenuInput()
{
	if (!mPlanternGearMenuOpen) return;
	Card* card = FindPlanternCard();
	if (!mBoard || !mBoard->GetActivePlantern() || !card) {
		mPlanternGearMenuOpen = false;
		return;
	}

	auto& input = GameAPP::GetInstance().GetInputHandler();
	if (input.IsMouseButtonPressed(SDL_BUTTON_RIGHT)) {
		mPlanternGearMenuOpen = false;
		return;
	}
	if (!input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) return;

	const Vector anchor = GetPlanternMenuAnchor(card);
	const Vector mouse = input.GetMousePosition();
	for (int gear = 0; gear <= 3; ++gear) {
		const float y = anchor.y
			+ gear * (kPlanternMenuButtonHeight + kPlanternMenuButtonGap);
		if (mouse.x < anchor.x || mouse.x > anchor.x + kPlanternMenuButtonWidth
			|| mouse.y < y || mouse.y > y + kPlanternMenuButtonHeight) {
			continue;
		}
		mBoard->SetPlanternGear(static_cast<PlanternGear>(gear));
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CLICKSEED, 0.45f);
		return;
	}
}

bool CardSlotManager::UpdateBloverDirectionInput()
{
	auto& input = GameAPP::GetInstance().GetInputHandler();
	if (!input.IsMouseButtonPressed(SDL_BUTTON_RIGHT)) return false;

	const Vector mouse = input.GetMousePosition();
	for (auto it = cards.rbegin(); it != cards.rend(); ++it) {
		Card* card = *it;
		if (!card || !card->IsActive()) continue;
		auto* collider = card->GetCollider();
		if (!collider || card->GetGameplayPlantType() != PlantType::PLANT_BLOVER
			|| !collider->mEnabled || !collider->ContainsPoint(mouse)) {
			continue;
		}

		if (selectedCard && selectedCard != card) {
			DeselectCard();
			if (mBoard) mBoard->mCursorObjectManager.ClearActive();
		}
		card->ToggleBloverDirection();
		ApplySelectedBloverDirection(plantPreview);
		ApplySelectedBloverDirection(cellPlantPreview);
		mPlanternGearMenuOpen = false;
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CLICKSEED, 0.45f);
		return true;
	}
	return false;
}

void CardSlotManager::ApplySelectedBloverDirection(Plant* plant) const
{
	auto* blover = dynamic_cast<Blover*>(plant);
	if (blover && selectedCard
		&& selectedCard->GetGameplayPlantType() == PlantType::PLANT_BLOVER) {
		blover->SetBlowDirection(selectedCard->GetBloverDirection());
	}
}

void CardSlotManager::DrawPlanternGearMenu(Graphics* g)
{
	if (!g || !mPlanternGearMenuOpen || !mBoard || !mBoard->GetActivePlantern()) return;
	Card* card = FindPlanternCard();
	if (!card || !card->GetTransform()) return;

	const Vector logical = GetPlanternMenuAnchor(card);
	const Vector anchor = g->LogicalToWorld(logical.x, logical.y);
	const int currentGear = mBoard->GetPlanternGearValue();
	static const char* labels[] = { "0", "I", "II", "III" };
	const float panelHeight = 4.0f * kPlanternMenuButtonHeight
		+ 3.0f * kPlanternMenuButtonGap + 4.0f;
	g->FillRect(anchor.x - 2.0f, anchor.y - 2.0f,
		kPlanternMenuButtonWidth + 4.0f, panelHeight,
		glm::vec4(28.0f, 24.0f, 20.0f, 225.0f));

	for (int gear = 0; gear <= 3; ++gear) {
		const float y = anchor.y
			+ gear * (kPlanternMenuButtonHeight + kPlanternMenuButtonGap);
		const bool selected = gear == currentGear;
		g->FillRect(anchor.x, y, kPlanternMenuButtonWidth, kPlanternMenuButtonHeight,
			selected
				? glm::vec4(245.0f, 184.0f, 58.0f, 245.0f)
				: glm::vec4(86.0f, 76.0f, 62.0f, 235.0f));
		const float labelWidth = g->MeasureTextWidth(labels[gear],
			ResourceKeys::Fonts::FONT_FZCQ, kPlanternMenuFontSize);
		const float labelX = anchor.x + (kPlanternMenuButtonWidth - labelWidth) * 0.5f;
		g->DrawGlyphRun(labels[gear], ResourceKeys::Fonts::FONT_FZCQ, kPlanternMenuFontSize,
			selected
				? glm::vec4(42.0f, 29.0f, 14.0f, 255.0f)
				: glm::vec4(245.0f, 235.0f, 205.0f, 255.0f),
			labelX, y + 2.0f);
	}
}

bool CardSlotManager::CanUsePlant(PlantType type, int cost) const {
	return mBoard && CanAfford(cost) && mBoard->HasPlantingQuota(type)
		&& mBoard->HasPlantingRequirement(type);
}

bool CardSlotManager::SpendSun(int cost) {
	if (!mBoard) {
		LOG_ERROR("CardSlotManager") << "No Board reference, cannot spend sun";
		return false;
	}

	if (GameAPP::mDevelopMode && GameAPP::mDevFreePlant) {
		UpdateAllCardsState();
		return true;                                   // 开发者作弊：视为支付成功但不扣阳光
	}

	if (CanAfford(cost)) {
		mBoard->SubSun(cost);
		UpdateAllCardsState();
		return true;
	}
	return false;
}

void CardSlotManager::DestroyPlantPreview() {
	if (plantPreview) {
		plantPreview->Die();
		plantPreview = nullptr;
	}
}

void CardSlotManager::DestroyCellPlantPreview() {
	if (cellPlantPreview) {
		cellPlantPreview->Die();
		cellPlantPreview = nullptr;
	}
}

void CardSlotManager::CreatePlantPreview(PlantType plantType) {
	DestroyPlantPreview();

	if (mBoard) {
		plantPreview = mBoard->CreatePlant(plantType, 0, 0, true, true);
		if (plantPreview && selectedCard
			&& selectedCard->GetPlantType() == PlantType::PLANT_IMITATER) {
			plantPreview->SetImitatedAppearance(true);
		}
		if (!plantPreview) return;
		plantPreview->PauseAnimation();
		plantPreview->SetRenderOrder(LAYER_EFFECTS + 10000);
		plantPreview->RemoveShadow();
		ApplySelectedBloverDirection(plantPreview);
	}
}

void CardSlotManager::CreateCellPlantPreview(PlantType plantType, Cell* cell) {
	DestroyCellPlantPreview();

	if (mBoard && cell) {
		int anchorRow = cell->mRow;
		int anchorColumn = cell->mColumn;
		if (!mBoard->ResolvePlantPlacementAnchor(plantType, cell->mRow, cell->mColumn,
			anchorRow, anchorColumn)) return;
		Cell* anchorCell = mBoard->GetCell(anchorRow, anchorColumn);
		if (!anchorCell) return;
		cellPlantPreview = mBoard->CreatePlant(plantType, 0, 0, true, true);
		if (cellPlantPreview) {
			if (selectedCard
				&& selectedCard->GetPlantType() == PlantType::PLANT_IMITATER) {
				cellPlantPreview->SetImitatedAppearance(true);
			}
			Vector centerPos = anchorCell->GetCenterPosition();          // 世界坐标
			Plant* supportPlant = mBoard->GetUnderPlantAt(anchorRow, anchorColumn);
			if (!cellPlantPreview->IsRoofSupportPlant() && supportPlant
				&& supportPlant->IsRoofSupportPlant()) {
				// 预览实体没有真实 row/column，需在落点处显式复用花盆抬升口径。
				centerPos.y += Plant::kFlowerPotVisualLiftY;
			}

			// 落点幽灵必须盖在该格已有承载植物之上，否则睡莲会遮住待种植物。
			Plant* topPlant = mBoard->GetTopPlantAt(anchorRow, anchorColumn);
			const int previewRenderOrder = topPlant
				? topPlant->GetRenderOrder() + 1 : LAYER_GAME_PLANT;
			cellPlantPreview->SetRenderOrder(previewRenderOrder);

			if (auto transform = cellPlantPreview->GetTransform()) {
				transform->SetPosition(centerPos);             // 设置为世界坐标
			}

			cellPlantPreview->SetAlpha(0.35f);
			cellPlantPreview->RemoveShadow();
			cellPlantPreview->PauseAnimation();
			ApplySelectedBloverDirection(cellPlantPreview);
		}
	}
}

void CardSlotManager::UpdatePlantPreviewPosition(Graphics* g, const Vector& mouseScreen) {
	if (!plantPreview) return;

	auto* selected = selectedCard;
	if (!selected) return;

	// 屏幕坐标转世界坐标
	Vector mouseWorld = g->LogicalToWorld(mouseScreen.x, mouseScreen.y);

	Cell* hoveredCell = FindCellAtWorldPosition(mouseWorld);
	if (selected->GetGameplayPlantType() == PlantType::PLANT_CARRYVINE) {
		DestroyCellPlantPreview();
		mHoveredCell = hoveredCell;
		UpdatePreviewToMouse(mouseWorld);
		return;
	}

	bool isOverCellWithPlant = false;
	if (hoveredCell && mBoard) {
		isOverCellWithPlant = !mBoard->CanPlantAt(selected->GetGameplayPlantType(),
			hoveredCell->mRow, hoveredCell->mColumn);
	}

	if (isOverCellWithPlant) {
		DestroyCellPlantPreview();
		hoveredCell = nullptr;
	}

	if (hoveredCell != mHoveredCell) {
		DestroyCellPlantPreview();

		if (hoveredCell) {
			CreateCellPlantPreview(selected->GetGameplayPlantType(), hoveredCell);
		}

		mHoveredCell = hoveredCell;
	}

	if (cellPlantPreview && hoveredCell) {
		int anchorRow = hoveredCell->mRow;
		int anchorColumn = hoveredCell->mColumn;
		if (!mBoard->ResolvePlantPlacementAnchor(selected->GetGameplayPlantType(),
			hoveredCell->mRow, hoveredCell->mColumn, anchorRow, anchorColumn)) return;
		Cell* anchorCell = mBoard->GetCell(anchorRow, anchorColumn);
		if (!anchorCell) return;
		Vector centerPos = anchorCell->GetCenterPosition();               // 世界坐标

		if (auto transform = cellPlantPreview->GetTransform()) {
			transform->SetPosition(centerPos);                         // 设置世界坐标
		}
	}

	// 更新鼠标预览植物位置
	UpdatePreviewToMouse(mouseWorld);
}

Cell* CardSlotManager::FindCellAtWorldPosition(const Vector& position) const {
	if (!mBoard) return nullptr;

	// ContainsPoint 对矩形四边均为闭区间；按固定行列顺序返回第一个命中，
	// 使水平、垂直乃至四格交点都只有一个稳定归属。
	for (int row = 0; row < mBoard->mRows; ++row) {
		for (int col = 0; col < mBoard->mColumns; ++col) {
			Cell* cell = mBoard->GetCell(row, col);
			if (!cell) continue;

			auto* collider = cell->GetCollider();
			if (collider && collider->mEnabled && collider->ContainsPoint(position)) {
				return cell;
			}
		}
	}
	return nullptr;
}

void CardSlotManager::UpdatePreviewToMouse(const Vector& mouseWorld) {
	if (plantPreview) {
		if (auto transform = plantPreview->GetTransform()) {
			transform->SetPosition(mouseWorld);      // 世界坐标
		}

		plantPreview->mRow = -1;
		plantPreview->mColumn = -1;
	}
}

void CardSlotManager::HandleCellClick(int row, int col) {
	if (mBoard && mBoard->mMineToolActive) return;
	if (!CanAcceptGameplayInput()) return;
	if (mBoard->IsCobCannonTargeting()) {
		// 落点释放由全战场输入入口统一处理；Cell 只负责开始瞄准与普通落种。
		return;
	}
	if (!selectedCard) {
		if (mBoard->mCursorObjectManager.GetActiveType() == CursorObjectType::NONE
			&& mBoard->ActivateDawnLotusAt(row, col)) return;
		if (mBoard->mCursorObjectManager.GetActiveType() == CursorObjectType::NONE
			&& HasInteractablePlanternAt(row, col)) {
			TogglePlanternGearMenu();
			return;
		}
		mSuppressCobTargetRelease = mBoard->BeginCobCannonTargeting(row, col);
		return;
	}

	Cell* cell = mBoard->GetCell(row, col);
	if (!cell) return;
	if (selectedCard->GetGameplayPlantType() == PlantType::PLANT_CARRYVINE) {
		if (mRelocationSourceID == NULL_PLANT_ID) {
			mRelocationSourceID = mBoard->GetRelocationSourceID(row, col);
			if (mRelocationSourceID != NULL_PLANT_ID)
				AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CLICKSEED, 0.45f);
			return;
		}
		// 来源在等待点击期间可能已死亡或被抓；提交边沿重新校验整组，失败不扣费。
		if (!selectedCard->IsReady() || !CanAfford(selectedCard->GetSunCost())) return;
		if (!mBoard->RelocatePlantGroup(mRelocationSourceID, row, col)) return;
		SpendSun(selectedCard->GetSunCost());
		selectedCard->StartCooldown();
		AudioSystem::PlaySound(mBoard->IsPoolSquare(row, col)
			? ResourceKeys::Sounds::SOUND_PLANT_ONWATER
			: ResourceKeys::Sounds::SOUND_PLANT, 0.5f);
		DeselectCard();
		mBoard->mCursorObjectManager.ClearActive();
		return;
	}

	if (CanPlaceInCell(cell)) {
		PlacePlantInCell(row, col);
	}
}

bool CardSlotManager::CanPlaceInCell(Cell* cell) const {
	if (!selectedCard || !cell || !CanAcceptGameplayInput()) return false;

	// 检查阳光是否足够
	if (!mBoard || !mBoard->CanPlantAt(selectedCard->GetGameplayPlantType(),
		cell->mRow, cell->mColumn)) return false;
	if (!CanAfford(selectedCard->GetSunCost())) {
		return false;
	}

	return true;
}

std::string CardSlotManager::TryPlantFromSlot(int slot, int row, int col) {
	if (!CanAcceptGameplayInput()) return "gameplay_input_blocked";
	if (GameAPP::mDevelopMode && (GameAPP::mDevFreePlant || GameAPP::mDevNoCooldown))
		return "developer_cheat_enabled";
	if (slot < 0 || slot >= static_cast<int>(cards.size()) || !cards[slot]) return "invalid_slot";
	Card* card = cards[slot];
	if (!card->IsReady()) return "cooldown";
	if (mBoard->GetSun() < card->GetSunCost()) return "insufficient_sun";
	if (!mBoard->HasPlantingQuota(card->GetGameplayPlantType())) return "planting_quota";
	if (!mBoard->HasPlantingRequirement(card->GetGameplayPlantType())) return "planting_requirement";
	// 搬搬藤是两阶段搬运工具，不能伪装成一次普通落种。
	if (card->GetGameplayPlantType() == PlantType::PLANT_CARRYVINE) return "requires_relocation";
	if (!mBoard->GetCell(row, col) || !mBoard->CanPlantAt(card->GetGameplayPlantType(), row, col))
		return "invalid_cell";
	DeselectCard();
	SelectCard(card);
	if (selectedCard != card) return "selection_rejected";
	return PlacePlantInCell(row, col) ? "" : "creation_rejected";
}

/** 提交已选卡的正式落种；只有创建成功才结算费用与冷却。 */
bool CardSlotManager::PlacePlantInCell(int row, int col) {
	if (!selectedCard || !mBoard || !selectedCard->IsReady()) return false;

	Cell* cell = mBoard->GetCell(row, col);
	if (!CanPlaceInCell(cell)) return false;

	DestroyPlantPreview();
	DestroyCellPlantPreview();
	AudioSystem::PlaySound(mBoard->IsPoolSquare(row, col)
		? ResourceKeys::Sounds::SOUND_PLANT_ONWATER
		: ResourceKeys::Sounds::SOUND_PLANT, 0.5f);

	// 创建植物
	Plant* plant = selectedCard->GetPlantType() == PlantType::PLANT_IMITATER
		? mBoard->CreateImitaterPlant(selectedCard->GetImitaterTarget(), row, col)
		: mBoard->CreatePlayerPlant(selectedCard->GetPlantType(), row, col);

	if (plant) {
		// 校验和创建位于同一主线程事务；失败创建不得吞掉玩家阳光。
		SpendSun(selectedCard->GetSunCost());
		if (auto* blover = dynamic_cast<Blover*>(plant)) {
			blover->SetBlowDirection(selectedCard->GetBloverDirection());
		}
		if (auto* imitater = dynamic_cast<Imitater*>(plant)) {
			imitater->SetInheritedBloverDirection(selectedCard->GetBloverDirection());
		}
		selectedCard->StartCooldown();
	}

	// 取消选择
	DeselectCard();
	mBoard->mCursorObjectManager.ClearActive();
	return plant != nullptr;
}

PlantType CardSlotManager::GetSelectedPlantType() const {
	if (selectedCard) return selectedCard->GetGameplayPlantType();
	return PlantType::NUM_PLANT_TYPES;
}

void CardSlotManager::DrawRelocationHint(Graphics* g)
{
	if (!g || GetSelectedPlantType() != PlantType::PLANT_CARRYVINE) return;
	const char* message = mRelocationSourceID == NULL_PLANT_ID
		? u8"搬搬藤：点击要搬运的植物组 · 右键取消"
		: u8"搬搬藤：点击合法空格 · 75阳光 · 右键取消";
	const Vector p = g->LogicalToWorld(260.0f, 76.0f);
	g->DrawGlyphRun(message, ResourceKeys::Fonts::FONT_FZCQ, 18,
		glm::vec4(255, 239, 156, 255), p.x, p.y);
}
