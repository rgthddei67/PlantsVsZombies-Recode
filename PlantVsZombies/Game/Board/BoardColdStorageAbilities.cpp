#include "Board.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/GameDataManager.h"
#include "ColdStorageSkillRules.h"
#include "DeltaTime.h"
#include "Graphics.h"
#include "ResourceKeys.h"
#include <algorithm>
#include <cmath>

int Board::GetPlantIcePaymentCost(PlantType type) const
{
	return GetPlantAbilityIceCost(GetPlantIceCost(type));
}

int Board::GetPlantAbilityIceCost(int baseCost) const
{
	if (!IsColdStorage() || mColdStorage.discountRemaining <= 0 || baseCost <= 0) return baseCost;
	return baseCost / ColdStorageSkillRules::DiscountDivisor
		+ (baseCost % ColdStorageSkillRules::DiscountDivisor != 0);
}

bool Board::TryActivateIceVoucher()
{
	const int sunCost = GameDataManager::GetInstance().GetPlantSunCost(PlantType::PLANT_ICEVOUCHER);
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || DeltaTime::IsPaused()
		|| sunCost < 0 || mSun < sunCost) return false;
	SubSun(sunCost);
	mColdStorage.discountRemaining = ColdStorageSkillRules::DiscountDuration;
	return true;
}

bool Board::CanUseColdStoragePrecisionStrike() const
{
	return IsColdStorage() && mBoardState == BoardState::GAME && !mTrophySpawned && !DeltaTime::IsPaused()
		&& mColdStorage.decisions >= ColdStorageSkillRules::StrikeUnlockWave
		&& mColdStorage.enemyIce >= ColdStorageSkillRules::StrikeIceCost
		&& mColdStorage.strikeCooldownRemaining <= 0 && mColdStorage.strikeTargetID < 0;
}

bool Board::TryStartColdStoragePrecisionStrike(int plantID)
{
	if (!CanUseColdStoragePrecisionStrike()) return false;
	const Plant* target = mEntityRegistry.GetPlant(plantID);
	if (!target || !target->IsActive() || target->mIsPreview || target->IsSquished() || target->mPlantHealth <= 0) return false;
	// 策略只提供意图，正式钱包和目标资格必须在主线程提交边沿重新校验。
	if (!TrySpendZombieAbilityIce(ColdStorageSkillRules::StrikeIceCost)) return false;
	mColdStorage.strikeTargetID = plantID;
	mColdStorage.strikeAimRemaining = ColdStorageSkillRules::StrikeAimDuration;
	mColdStorage.strikeCooldownRemaining = ColdStorageSkillRules::StrikeCooldown;
	return true;
}

void Board::UpdateColdStorageSkills(float dt)
{
	if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || DeltaTime::IsPaused() || dt <= 0) return;
	auto& s = mColdStorage;
	s.discountRemaining = std::max(0.0f, s.discountRemaining - dt);
	s.strikeCooldownRemaining = std::max(0.0f, s.strikeCooldownRemaining - dt);
	if (s.strikeTargetID < 0) return;
	s.strikeAimRemaining = std::max(0.0f, s.strikeAimRemaining - dt);
	if (s.strikeAimRemaining > 0) return;
	Plant* target = mEntityRegistry.GetPlant(s.strikeTargetID);
	// 先关闭事务，死亡副作用与之后的保存都不能再次结算同一单。
	s.strikeTargetID = -1;
	if (!target || !target->IsActive() || target->mIsPreview || target->IsSquished() || target->mPlantHealth <= 0) return;
	const PlantType placementType = target->GetPlacementType();
	// 直接结束目标生命周期，绕过南瓜、血量、词条保命与冰像防御；仍沿用占格/附件清理。
	if (target->IsIceSealed()) target->ReleaseIceSeal(target->GetIceSealOwnerZombieID());
	target->Die();
	if (!target->IsActive()) RewardColdStoragePlantKill(placementType);
}

void Board::DrawColdStoragePrecisionStrike(Graphics* g) const
{
	if (!g || !IsColdStorage() || mColdStorage.strikeTargetID < 0 || mTrophySpawned) return;
	const Plant* target = mEntityRegistry.GetPlant(mColdStorage.strikeTargetID);
	if (!target || !target->IsActive() || target->IsSquished()) return;
	const auto anchor = target->GetVisualAnchorPosition() + target->mBungeeVisualOffset;
	const float x = anchor.x, y = anchor.y - 25.0f;
	const float fraction = mColdStorage.strikeAimRemaining / ColdStorageSkillRules::StrikeAimDuration;
	const float radius = 30.0f + 12.0f * fraction;
	const glm::vec4 red(255, 70, 55, 255), shadow(45, 8, 8, 230);
	g->DrawCircle(x, y, radius + 2, shadow);
	g->DrawCircle(x, y, radius, red);
	g->DrawCircle(x, y, 12, red);
	for (int sign : {-1, 1}) {
		g->DrawLine(x + sign * (radius - 8), y, x + sign * (radius + 12), y, red);
		g->DrawLine(x, y + sign * (radius - 8), x, y + sign * (radius + 12), red);
	}
	const std::string label = u8"精准清除 " + std::to_string(static_cast<int>(std::ceil(mColdStorage.strikeAimRemaining))) + u8"秒";
	const auto font = ResourceKeys::Fonts::FONT_FZCQ;
	const float width = g->MeasureTextWidth(label, font, 14);
	g->FillRect(x - width / 2 - 4, y - radius - 27, width + 8, 20, shadow);
	g->DrawText(label, font, 14, glm::vec4(255, 205, 180, 255), x - width / 2, y - radius - 26);
}

bool Board::TrySpendPlantAbilityResource(int ice, int sun)
{
	if (mBoardState != BoardState::GAME || mTrophySpawned || ice < 0 || sun < 0) return false;
	if (IsColdStorage()) {
		ice = GetPlantAbilityIceCost(ice);
		if (mColdStorage.playerIce < ice) return false;
		mColdStorage.playerIce -= ice;
	} else {
		if (mSun < sun) return false;
		SubSun(sun);
	}
	return true;
}

bool Board::TrySpendZombieAbilityIce(int ice)
{
	if (mBoardState != BoardState::GAME || mTrophySpawned || ice < 0) return false;
	if (!IsColdStorage()) return true;
	if (mColdStorage.enemyIce < ice) return false;
	mColdStorage.enemyIce -= ice;
	mColdStorage.spent += ice;
	mColdStorage.incomeWindow.push_back({mColdStorage.elapsed, 0, ice});
	return true;
}

float Board::GetAreaPlantAttackSpeedBonus(const Plant* target) const
{
	if (!target || target->mIsPreview || !target->IsActive()) return 0.0f;
	float bonus = 0.0f;
	// 领域跟随当前格位，按来源相加；查询九格不保留指针，不需要另存目标 buff。
	for (int row = std::max(0, target->mRow - 1); row <= std::min(mRows - 1, target->mRow + 1); ++row)
		for (int col = std::max(0, target->mColumn - 1); col <= std::min(mColumns - 1, target->mColumn + 1); ++col)
			if (const auto* source = GetNormalPlantAt(row, col)) bonus += source->GetAreaAttackSpeedBonus();
	return bonus;
}
