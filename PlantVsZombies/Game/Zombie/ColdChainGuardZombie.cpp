#include "ColdChainGuardZombie.h"
#include "ColdChainGuardRules.h"
#include "Game/Board/Board.h"
#include "ResourceManager.h"
#include "DeltaTime.h"
#include <cmath>

using namespace ColdChainGuardRules;

namespace {
constexpr float kShieldOffsetX = -24.0f; // 冰盾左上角相对内前臂锚点，动画像素
constexpr float kShieldOffsetY = -17.0f; // 冰盾左上角相对内前臂锚点，动画像素
}

void ColdChainGuardZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = kBodyHealth;
	mHelmType = HelmType::HELMTYPE_ICE_SHIELD;
	mHelmHealth = mHelmMaxHealth = kShieldHealth;
	mRepairRemaining = kRepairInterval;
	CheckHelmImage();
}

bool ColdChainGuardZombie::HasIceShield() const
{
	return mHelmType == HelmType::HELMTYPE_ICE_SHIELD && mHelmHealth > 0;
}

void ColdChainGuardZombie::TakePlantAshDamage(int damage)
{
	if (damage <= 0 || !mBoard) return;
	const int scaled = ScaleStatusDamage(mBoard->GetPerkManager().ScaleTotalDamageToZombie(damage));
	// 基类只按本体判断化灰，会跳过这面高耐久一类防具；预测不重复应用伤害倍率。
	if (CanBeCharred() && static_cast<int64_t>(mBodyHealth) + std::max(0, mHelmHealth) <= scaled) {
		Charred();
		return;
	}
	TakeDamage(damage, DamageSource::PLANT_ASH, false, false, false, PlantDamageOrigin::Ash());
}

void ColdChainGuardZombie::Update()
{
	Zombie::Update();
	if (mIsPreview || !mBoard || mBoard->mBoardState != BoardState::GAME || mBoard->mTrophySpawned
		|| DeltaTime::IsPaused() || !IsActive() || IsDying() || !HasHead() || !HasIceShield()
		|| IsImmobilized() || IsTangleKelpTarget()) return;
	mRepairRemaining -= DeltaTime::GetDeltaTime();
	if (mRepairRemaining > 0.0f) return;
	mRepairRemaining += kRepairInterval;
	if (mHelmHealth >= mHelmMaxHealth) return;
	// 魅惑后保留原周期，费用归玩家；其他地图仍通过同一事务入口免费修复。
	const bool paid = IsMindControlled()
		? mBoard->TrySpendPlantAbilityResource(kRepairIce, 0) : mBoard->TrySpendZombieAbilityIce(kRepairIce);
	if (!paid) return;
	mHelmHealth = std::min(mHelmMaxHealth, mHelmHealth + kRepairHealth);
	CheckHelmImage();
	SetGlowingTimer(0.2f);
}

void ColdChainGuardZombie::SyncShieldPresentation() const
{
	const char* key = mShieldStage >= 2 ? "IMAGE_COLDCHAIN_SHIELD_CRACKED2"
		: mShieldStage == 1 ? "IMAGE_COLDCHAIN_SHIELD_CRACKED1" : "IMAGE_COLDCHAIN_SHIELD";
	mAnimator->SetTrackFollowerImage("anim_innerarm2", "cold_chain_shield",
		ResourceManager::GetInstance().GetTexture(key, false), kShieldOffsetX, kShieldOffsetY, 1.0f, 1.0f,
		/*drawAfterAllTracks=*/true, /*inheritOverlayEffect=*/true,
		/*inheritGlowEffect=*/true); // 一类冰盾与本体共用受击白光，破损换图也保留继承。
	mAnimator->SetTrackFollowerVisible("anim_innerarm2", "cold_chain_shield", HasIceShield());
}

void ColdChainGuardZombie::CheckHelmImage()
{
	mShieldStage = !HasIceShield() ? -1 : mHelmHealth <= mHelmMaxHealth / 3 ? 2
		: mHelmHealth <= mHelmMaxHealth * 2 / 3 ? 1 : 0;
	SyncShieldPresentation();
}

void ColdChainGuardZombie::HelmDrop()
{
	Zombie::HelmDrop();
	mRepairRemaining = kRepairInterval;
	CheckHelmImage();
}

void ColdChainGuardZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	SyncShieldPresentation();
}

void ColdChainGuardZombie::SaveExtraData(nlohmann::json& j) const
{
	j["guardRepairRemaining"] = mRepairRemaining;
}

bool ColdChainGuardZombie::CaptureTemporalAbilityState(ZombieTemporalAbilityState& state) const
{
	state = { 0, mRepairRemaining, 0 };
	return true;
}

void ColdChainGuardZombie::RestoreTemporalAbilityState(const ZombieTemporalAbilityState& state)
{
	LoadExtraData({ { "guardRepairRemaining", state.remaining } });
}

void ColdChainGuardZombie::LoadExtraData(const nlohmann::json& j)
{
	const float remaining = j.value("guardRepairRemaining", kRepairInterval);
	mRepairRemaining = std::isfinite(remaining) ? std::clamp(remaining, 0.0f, kRepairInterval) : kRepairInterval;
	// 防具当前/最大值由正式加载器恢复；不能重新套出生常量或复建已掉落冰盾。
	CheckHelmImage();
}
