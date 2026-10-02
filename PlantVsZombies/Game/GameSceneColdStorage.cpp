#include "GameScene.h"
#include "Game/Board/Board.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include "Game/CardSlotManager.h"
#include "UI/Button.h"
#include "DeltaTime.h"
#include <cmath>

/** 创建配送和时间干扰按钮；全部交易由 Board 在点击时重新校验。 */
void GameScene::CreateColdStorageShop()
{
	if (!mBoard || !mBoard->IsColdStorage()) return;
	mColdStorageShopOpen = false;
	const std::array<Vector, 4> positions{Vector(860, 3), Vector(14, 144), Vector(14, 196), Vector(14, 272)};
	const std::array<std::string, 4> labels{u8"冰块商店", u8"40冰 / 100阳光 · 5秒", u8"100冰 / 225阳光 · 10秒", u8"时间干扰 · " + std::to_string(ColdStorageSkillRules::InterferenceIceCost) + u8"冰"};
	for (int i = 0; i < 4; ++i) {
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
			else if (i == 3 ? mBoard->TryActivateTemporalInterference() : mBoard->BuyColdStorageIce(i == 2)) mColdStorageShopOpen = false;
		});
		mColdStorageShopButtons[i] = button;
	}
}

/** 每帧同步可见与可点击状态；关闭的订单按钮不保留任何绘制。 */
void GameScene::UpdateColdStorageShop()
{
	if (!mBoard || !mBoard->IsColdStorage()) return;
	const bool active = mBoard->mBoardState == BoardState::GAME && !mBoard->mTrophySpawned;
	for (int i = 0; i < 4; ++i) if (auto button = mColdStorageShopButtons[i].lock()) {
		const bool visible = active && (i == 0 || mColdStorageShopOpen) && (i != 3 || mBoard->SupportsTemporalInterference());
		button->SetEnabled(visible);
		button->SetSkipDraw(!visible);
		button->SetCanClick(!DeltaTime::IsPaused() && (i == 0 ||
			(i == 3 ? mBoard->CanUseTemporalInterference() :
			(mBoard->mColdStorage.orderIce == 0 && mBoard->GetSun() >= (i == 1 ? 100 : 225)))));
	}
}

/** 在世界之后、按钮之前绘制场外面板和配送提示，保持可玩格与推车无遮挡。 */
void GameScene::DrawColdStorageShop(Graphics* g)
{
	if (!mBoard || !mBoard->IsColdStorage() || mBoard->mBoardState != BoardState::GAME) return;
	const auto& ice = mBoard->mColdStorage;
	mBoard->DrawColdStoragePrecisionStrike(g);
	if (ice.interferenceRemaining > 0) {
		g->FillRect(365, 99, 255, 25, glm::vec4(18, 47, 57, 230));
		g->DrawGlyphRun(u8"时间干扰 · 禁锚 " + std::to_string(static_cast<int>(std::ceil(ice.interferenceRemaining))) + u8"秒",
			ResourceKeys::Fonts::FONT_FZCQ, 16, glm::vec4(165, 245, 255, 255), 373, 102);
	}
	if (ice.discountRemaining > 0) {
		g->FillRect(365, 72, 255, 25, glm::vec4(18, 47, 57, 230));
		g->DrawGlyphRun(u8"冰惠券 · 冰费减半 " + std::to_string(static_cast<int>(std::ceil(ice.discountRemaining))) + u8"秒",
			ResourceKeys::Fonts::FONT_FZCQ, 16, glm::vec4(165, 245, 255, 255), 373, 75);
	}
	g->FillRect(590, 573, 235, 27, glm::vec4(20, 35, 40, 190));
	g->DrawGlyphRun(mBoard->mLevelName + u8"  第" + std::to_string(ice.decisions) + u8"波",
		ResourceKeys::Fonts::FONT_FZCQ, 18, glm::vec4(255, 235, 175, 255), 600, 575);
	if (mColdStorageShopOpen) {
		g->FillRect(8, 108, 164, mBoard->SupportsTemporalInterference() ? 282 : 158, glm::vec4(18, 47, 57, 240));
		g->DrawGlyphRun(u8"冷藏站 · 冰块配送", ResourceKeys::Fonts::FONT_FZCQ,
			14, glm::vec4(207, 245, 250, 255), 20, 116);
		g->DrawGlyphRun(u8"一单完成后再接单", ResourceKeys::Fonts::FONT_FZCQ,
			12, glm::vec4(207, 220, 220, 255), 20, 244);
		if (mBoard->SupportsTemporalInterference()) {
			const auto font = ResourceKeys::Fonts::FONT_FZCQ;
			const glm::vec4 color(207, 220, 220, 255);
			g->DrawGlyphRun(u8"全场解锚，禁锚" + std::to_string(static_cast<int>(ColdStorageSkillRules::InterferenceDuration)) + u8"秒", font, 12, color, 20, 320);
			g->DrawGlyphRun(ice.interferenceCooldownRemaining > 0
				? u8"冷却 " + std::to_string(static_cast<int>(std::ceil(ice.interferenceCooldownRemaining))) + u8"秒"
				: u8"时间干扰就绪", font, 12, color, 20, 338);
			if (mBoard->HasColdStorageOpeningBonus(ColdStorageOpeningBonus::ELITE_QUOTA)) {
				g->DrawGlyphRun(u8"精英在场 " + std::to_string(mBoard->GetActiveEliteScaredyShroomCount()) + "/"
					+ std::to_string(mBoard->GetEliteScaredyShroomPlantLimit()), font, 12, color, 20, 354);
				g->DrawGlyphRun(u8"累计种植 " + std::to_string(mBoard->GetEliteScaredyShroomsPlanted()) + "/"
					+ std::to_string(mBoard->GetEliteScaredyShroomTotalPlantLimit()), font, 12, color, 20, 370);
			}
		}
	}
	if (ice.orderIce > 0) {
		g->FillRect(748, 45, 235, 23, glm::vec4(18, 47, 57, 230));
		g->DrawGlyphRun(u8"配送 " + std::to_string(ice.orderIce) + u8"冰 · " +
			std::to_string(static_cast<int>(std::ceil(ice.orderRemaining))) + u8"秒", ResourceKeys::Fonts::FONT_FZCQ,
			15, glm::vec4(150, 240, 255, 255), 755, 47);
	}
}
