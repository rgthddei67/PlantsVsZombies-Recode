#include "GameScene.h"
#include "Game/Board/Board.h"
#include "Game/CardSlotManager.h"
#include "UI/Button.h"
#include "DeltaTime.h"
#include <cmath>

void GameScene::CreateColdStorageShop()
{
	if (!mBoard || !mBoard->IsColdStorage()) return;
	mColdStorageShopOpen = false;
	const std::array<Vector, 3> positions{Vector(860, 3), Vector(14, 144), Vector(14, 196)};
	const std::array<const char*, 3> labels{u8"冰块商店", u8"40冰 / 100阳光 · 5秒", u8"100冰 / 225阳光 · 10秒"};
	for (int i = 0; i < 3; ++i) {
		auto button = mUIManager.CreateButton(positions[i], i == 0 ? Vector(124, 40) : Vector(152, 42));
		button->SetImageKeys(ResourceKeys::Textures::IMAGE_BUTTONSMALL);
		button->SetText(labels[i], ResourceKeys::Fonts::FONT_FZCQ, i == 0 ? 16 : 12);
		button->SetTextColor(glm::vec4(165, 245, 255, 255));
		button->SetEnabled(false);
		button->SetSkipDraw(true);
		button->SetClickCallBack([this, i](bool) {
			if (!mBoard || mBoard->mBoardState != BoardState::GAME || DeltaTime::IsPaused()) return;
			if (i == 0) {
				mColdStorageShopOpen = !mColdStorageShopOpen;
				if (mCardSlotManager) mCardSlotManager->DeselectCard();
			}
			else if (mBoard->BuyColdStorageIce(i == 2)) mColdStorageShopOpen = false;
		});
		mColdStorageShopButtons[i] = button;
	}
}

/** 每帧同步可见与可点击状态；关闭的订单按钮不保留任何绘制。 */
void GameScene::UpdateColdStorageShop()
{
	if (!mBoard || !mBoard->IsColdStorage()) return;
	const bool active = mBoard->mBoardState == BoardState::GAME && !mBoard->mTrophySpawned;
	for (int i = 0; i < 3; ++i) if (auto button = mColdStorageShopButtons[i].lock()) {
		button->SetEnabled(active && (i == 0 || mColdStorageShopOpen));
		button->SetSkipDraw(!active || (i != 0 && !mColdStorageShopOpen));
		button->SetCanClick(!DeltaTime::IsPaused() && (i == 0 ||
			(mBoard->mColdStorage.orderIce == 0 && mBoard->GetSun() >= (i == 1 ? 100 : 225))));
	}
}

/** 在世界之后、按钮之前绘制场外面板和配送提示，保持可玩格与推车无遮挡。 */
void GameScene::DrawColdStorageShop(Graphics* g)
{
	if (!mBoard || !mBoard->IsColdStorage() || mBoard->mBoardState != BoardState::GAME) return;
	const auto& ice = mBoard->mColdStorage;
	g->FillRect(590, 573, 235, 27, glm::vec4(20, 35, 40, 190));
	g->DrawGlyphRun(mBoard->mLevelName + u8"  第" + std::to_string(ice.decisions) + u8"波",
		ResourceKeys::Fonts::FONT_FZCQ, 18, glm::vec4(255, 235, 175, 255), 600, 575);
	if (mColdStorageShopOpen) {
		g->FillRect(8, 108, 164, 158, glm::vec4(18, 47, 57, 240));
		g->DrawGlyphRun(u8"冷藏站 · 冰块配送", ResourceKeys::Fonts::FONT_FZCQ,
			14, glm::vec4(207, 245, 250, 255), 20, 116);
		g->DrawGlyphRun(u8"一单完成后再接单", ResourceKeys::Fonts::FONT_FZCQ,
			12, glm::vec4(207, 220, 220, 255), 20, 244);
	}
	if (ice.orderIce > 0) {
		g->FillRect(748, 45, 235, 23, glm::vec4(18, 47, 57, 230));
		g->DrawGlyphRun(u8"配送 " + std::to_string(ice.orderIce) + u8"冰 · " +
			std::to_string(static_cast<int>(std::ceil(ice.orderRemaining))) + u8"秒", ResourceKeys::Fonts::FONT_FZCQ,
			15, glm::vec4(150, 240, 255, 255), 755, 47);
	}
}
