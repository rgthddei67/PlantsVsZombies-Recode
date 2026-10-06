#include "CardSlotManager.h"
#include "Game/Plant/ColdPineapple.h"
#include "GameApp.h"
#include "UI/InputHandler.h"
#include "DeltaTime.h"
#include "Graphics.h"
#include "ResourceKeys.h"
#include "CursorManager.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kHoldSeconds = 0.45f; // 长按门槛，真实交互秒数，不随游戏倍速
constexpr float kDragDistance = 15.0f; // 手指滑动取消阈值，逻辑像素
constexpr float kWidth = 110.0f; // 操作条宽度，逻辑像素
constexpr float kHeight = 32.0f; // 操作条高度，逻辑像素，适合触摸
/** 按实际视觉位置定位同一块可绘制、可点击的操作条。 */
Vector MenuPosition(const Plant& plant)
{
	const auto p = plant.GetVisualAnchorPosition();
	return Vector(std::clamp(p.x - kWidth / 2, 0.0f, SCENE_WIDTH - kWidth),
		std::clamp(p.y + 48, 85.0f, SCENE_HEIGHT - kHeight - 2));
}
bool Contains(const Vector& point, const Vector& origin)
{
	return point.x >= origin.x && point.x <= origin.x + kWidth
		&& point.y >= origin.y && point.y <= origin.y + kHeight;
}
}

void CardSlotManager::UpdatePlantAbilityInput()
{
	auto& input = GameAPP::GetInstance().GetInputHandler();
	mPlantAbilityHintRemaining = std::max(0.0f, mPlantAbilityHintRemaining - DeltaTime::GetUnscaledDeltaTime());
	if (selectedCard || mBoard->mCursorObjectManager.GetActiveType() != CursorObjectType::NONE
		|| mBoard->mTrophySpawned) {
		mPlantAbilityPressID = mPlantAbilityMenuID = NULL_PLANT_ID;
		mPlantAbilityButtonPressed = false;
		return;
	}
	const Vector point = input.GetMouseWorldPosition();
	auto resolve = [&](int id) {
		auto* p = mBoard->mEntityRegistry.GetPlant(id);
		return p && p->HasManualAbility() && p->IsActive() && !p->IsSquished() && !p->IsBungeeTargeted() ? p : nullptr;
	};
	Cell* cell = FindCellAtWorldPosition(point);
	auto* hovered = cell ? mBoard->GetNormalPlantAt(cell->mRow, cell->mColumn) : nullptr;
	if (hovered && !resolve(hovered->mPlantID)) hovered = nullptr;
	auto* menu = resolve(mPlantAbilityMenuID);
	if (!menu) mPlantAbilityMenuID = NULL_PLANT_ID;
	bool overButton = menu && Contains(point, MenuPosition(*menu));
#if !defined(__ANDROID__)
	// 允许从本体移到脚边按钮；按钮覆盖相邻格时优先保留当前菜单。
	if (!overButton && hovered) mPlantAbilityMenuID = hovered->mPlantID;
	else if (!overButton && !hovered && mPlantAbilityPressID == NULL_PLANT_ID && !mPlantAbilityButtonPressed)
		mPlantAbilityMenuID = NULL_PLANT_ID;
	menu = resolve(mPlantAbilityMenuID);
	overButton = menu && Contains(point, MenuPosition(*menu));
#endif
	if (hovered || overButton) CursorManager::GetInstance().IncrementHoverCount();
	if (!mPlantAbilityHintShown) {
		for (int row = 0; row < mBoard->mRows && !mPlantAbilityHintShown; ++row)
			for (int col = 0; col < mBoard->mColumns; ++col)
				if (auto* plant = mBoard->GetNormalPlantAt(row, col); plant && plant->HasManualAbility()) {
					mPlantAbilityHintShown = true;
					mPlantAbilityHintRemaining = 5.0f;
					break;
				}
	}
	if (input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
		mPlantAbilityButtonPressed = overButton;
		mPlantAbilityPressID = !overButton && hovered ? hovered->mPlantID : NULL_PLANT_ID;
		mPlantAbilityPressPosition = point;
		mPlantAbilityHoldSeconds = 0;
		mPlantAbilityLongPress = mPlantAbilityPressCancelled = false;
		if (!overButton && !hovered) mPlantAbilityMenuID = NULL_PLANT_ID;
	}
	if (mPlantAbilityPressID != NULL_PLANT_ID && input.IsMouseButtonDown(SDL_BUTTON_LEFT)) {
		const float dx = point.x - mPlantAbilityPressPosition.x, dy = point.y - mPlantAbilityPressPosition.y;
		if (dx * dx + dy * dy > kDragDistance * kDragDistance) mPlantAbilityPressCancelled = true;
		// 全局暂停把 unscaledDelta 也置零；手势仍按 UI 固定步推进，不修改游戏时钟。
		mPlantAbilityHoldSeconds += DeltaTime::IsPaused()
			? DeltaTime::GetFixedStep() : DeltaTime::GetUnscaledDeltaTime();
		if (!mPlantAbilityPressCancelled && mPlantAbilityHoldSeconds >= kHoldSeconds) {
			mPlantAbilityLongPress = true;
			mPlantAbilityMenuID = mPlantAbilityPressID;
		}
	}
	if (input.IsMouseButtonReleased(SDL_BUTTON_LEFT)) {
		mPlantAbilityConsumed = mPlantAbilityButtonPressed || mPlantAbilityPressID != NULL_PLANT_ID;
		if (mPlantAbilityButtonPressed && menu && overButton) menu->SetAbilityAutomatic(!menu->IsAbilityAutomatic());
		else if (!DeltaTime::IsPaused() && CanAcceptGameplayInput()
			&& !mPlantAbilityLongPress && !mPlantAbilityPressCancelled) {
			if (auto* pressed = resolve(mPlantAbilityPressID); pressed && hovered == pressed) pressed->TryActivateManualAbility();
		}
		mPlantAbilityPressID = NULL_PLANT_ID;
		mPlantAbilityButtonPressed = false;
	} else if (!input.IsMouseButtonDown(SDL_BUTTON_LEFT) && !input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
		// 失焦ResetInput后没有释放边沿，丢弃未完成手势。
		mPlantAbilityPressID = NULL_PLANT_ID;
		mPlantAbilityButtonPressed = false;
	}
}

void CardSlotManager::DrawPlantAbilityMenu(Graphics* g)
{
	if (!g || !mBoard || mBoard->mBoardState != BoardState::GAME || mBoard->mTrophySpawned) return;
	if (mPlantAbilityHintRemaining > 0) {
#if defined(__ANDROID__)
		const char* hint = "菠萝／冰仓坚果：轻点发动，长按设置";
#else
		const char* hint = "菠萝／冰仓坚果：点击发动，悬停设置";
#endif
		g->FillRect(320, 555, 460, 29, glm::vec4(20, 40, 50, 220));
		g->DrawText(hint, ResourceKeys::Fonts::FONT_FZCQ, 17, glm::vec4(185, 250, 255, 255), 338, 558);
	}
	const auto* p = mBoard->mEntityRegistry.GetPlant(mPlantAbilityMenuID);
	if (!p || !p->HasManualAbility() || !p->IsActive() || p->IsSquished() || p->IsBungeeTargeted()) return;
	// 每格按Board坡面坐标画带宽边界，不把屋顶九格误画成同一水平矩形。
	if (dynamic_cast<const ColdPineapple*>(p))
	for (int row = std::max(0, p->mRow - 1); row <= std::min(mBoard->mRows - 1, p->mRow + 1); ++row)
		for (int col = std::max(0, p->mColumn - 1); col <= std::min(mBoard->mColumns - 1, p->mColumn + 1); ++col) {
			const auto c = mBoard->GetCellCenterPosition(row, col);
			const float x = c.x - CELL_COLLIDER_SIZE_X / 2, y = c.y - mBoard->GetCellHeight() / 2;
			g->FillRect(x + 2, y + 2, CELL_COLLIDER_SIZE_X - 4, 3, glm::vec4(100, 235, 255, 170));
			g->FillRect(x + 2, y + 2, 3, mBoard->GetCellHeight() - 4, glm::vec4(100, 235, 255, 170));
			g->FillRect(x + 2, y + mBoard->GetCellHeight() - 5, CELL_COLLIDER_SIZE_X - 4, 3, glm::vec4(100, 235, 255, 170));
			g->FillRect(x + CELL_COLLIDER_SIZE_X - 5, y + 2, 3, mBoard->GetCellHeight() - 4, glm::vec4(100, 235, 255, 170));
		}
	// 操作条最后绘制，范围边界不会划过按钮和费用文字。
	const auto pos = MenuPosition(*p);
	const glm::vec4 cyan(140, 240, 255, 255);
	g->FillRect(pos.x, pos.y, kWidth, kHeight, glm::vec4(25, 50, 65, 245));
	g->DrawRect(pos.x, pos.y, kWidth, kHeight, cyan);
	const std::string text = p->IsAbilityAutomatic() ? "自动 → 手动" : "手动 → 自动";
	g->DrawText(text, ResourceKeys::Fonts::FONT_FZCQ, 15, cyan, pos.x + 5, pos.y + 5);
	const std::string cost = p->GetManualAbilityDescription();
	const auto size = g->MeasureTextSize(cost, ResourceKeys::Fonts::FONT_FZCQ, 13);
	const float cx = std::clamp(pos.x + kWidth / 2 - size.x / 2, 2.0f, SCENE_WIDTH - size.x - 2);
	g->FillRect(cx - 2, pos.y - 19, size.x + 4, 18, glm::vec4(25, 50, 65, 240));
	g->DrawText(cost, ResourceKeys::Fonts::FONT_FZCQ, 13, cyan, cx, pos.y - 19);

}
