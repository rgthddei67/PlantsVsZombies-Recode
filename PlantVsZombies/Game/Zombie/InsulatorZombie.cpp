#include "InsulatorZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../../DeltaTime.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kBodyHealth = 300;                    // 绝缘僵尸本体生命
	constexpr int kArmorHealth = 1200;                  // 单层陶瓷绝缘胸甲生命
	constexpr int kFirstCrackThreshold = 800;           // 进入轻裂纹阶段的剩余生命阈值
	constexpr int kHeavyCrackThreshold = 400;           // 进入重裂纹阶段的剩余生命阈值
	constexpr float kWetLingeringSeconds = 6.0f;        // 离开冲刷坡面后的持续湿润时间
	constexpr float kOverloadSeconds = 15.0f;           // 成功吸收放电后的过载时间
	constexpr float kOverloadMoveMultiplier = 2.2f;     // 过载自主移动与动画倍率
	constexpr float kOverloadBiteMultiplier = 2.0f;     // 过载啃咬倍率，50 点提升到 100 点
	constexpr float kProtectionRadiusCells = 1.5f;      // 同排放电掩护的水平格距
	constexpr float kWetArmorPlantMultiplier = 1.5f;    // 湿润胸甲受到的植物伤害倍率
	constexpr int kWetSlopeDischargeDamage = 360;       // 湿坡放电对绝缘胸甲的固定伤害
	constexpr int kMagnetBacklashDamage = 150;          // 磁力菇成功吸走胸甲后的本体反噬
	constexpr float kMagnetDestinationX = 20.0f;        // 胸甲吸到磁力菇附近的局部 X
	constexpr float kMagnetDestinationY = 14.0f;        // 胸甲吸到磁力菇附近的局部 Y
	constexpr float kMagnetDestinationJitter = 8.0f;    // 离体胸甲终点的随机扰动，单位 px
	constexpr float kArmorDrawScale = 0.9f;             // 磁力菇吸出的离体胸甲绘制倍率
	constexpr float kMaximumSavedTimer = 30.0f;         // 损坏存档计时器的防御性夹紧上界
	constexpr float kArmorFollowerOffsetX = -12.0f;     // 胸甲相对 Zombie_body 向行进方向前移，避免看成背包
	constexpr float kArmorFollowerOffsetY = 0.0f;       // 胸甲相对身体轨道的垂直偏移
	constexpr float kArmorFollowerScale = 1.1f;         // 抵消身体轨道约 0.8 倍缩放后的胸甲视觉尺寸
	constexpr const char* kArmorFollowerSlot =
		"insulator_armor";                               // 身体轨道上的独立绝缘胸甲 follower 槽

	const char* ArmorImageKey(ArmorBrokenState stage)
	{
		switch (stage) {
		case ArmorBrokenState::A_LITTLE_BROKEN:
			return "IMAGE_ZOMBIE_INSULATOR_ARMOR2";
		case ArmorBrokenState::REALLY_BROKEN:
			return "IMAGE_ZOMBIE_INSULATOR_ARMOR3";
		default:
			return "IMAGE_ZOMBIE_INSULATOR_ARMOR1";
		}
	}

	bool IsLightProjectile(BulletType type)
	{
		switch (type) {
		case BulletType::BULLET_PEA:
		case BulletType::BULLET_SNOWPEA:
		case BulletType::BULLET_TOXICPEA:
		case BulletType::BULLET_PUFF:
		case BulletType::BULLET_STAR:
		case BulletType::BULLET_AURORA_PEA:
			return true;
		default:
			return false;
		}
	}
}

void InsulatorZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mHelmType = HelmType::HELMTYPE_INSULATOR;
	mHelmHealth = kArmorHealth;
	mHelmMaxHealth = kArmorHealth;
	mArmorStage = ArmorBrokenState::NO_BROKEN;
	mWetTimer = 0.0f;
	mOverloadTimer = 0.0f;
	mWetArmorDamageRemainder = 0.0f;
	ConfigureArmorFollower();
	RefreshArmorPresentation();
}

void InsulatorZombie::ConfigureArmorFollower()
{
	if (mArmorFollowerConfigured || !mAnimator
		|| !mAnimator->HasTrack("Zombie_body")) return;
	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_INSULATOR_ARMOR1, false);
	if (!texture) return;
	// 胸甲必须盖在躯干和内外手臂之上；延迟 follower 同时保留身体轨道的完整仿射变换。
	mAnimator->SetTrackFollowerImage("Zombie_body", kArmorFollowerSlot, texture,
		kArmorFollowerOffsetX, kArmorFollowerOffsetY,
		kArmorFollowerScale, kArmorFollowerScale,
		/*drawAfterAllTracks=*/true, /*inheritOverlayEffect=*/true,
		/*inheritGlowEffect=*/true); // 陶瓷胸甲是一类防具，与本体同步受击白光。
	mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot, true);
	mArmorFollowerConfigured = true;
}

void InsulatorZombie::Update()
{
	if (!mIsPreview && IsActive() && !mIsDead) {
		const float deltaTime = DeltaTime::GetDeltaTime();
		const bool hadOverloadTimer = mOverloadTimer > 0.0f;
		if (IsOnWetRunoffSlope()) {
			mWetTimer = kWetLingeringSeconds;
			mOverloadTimer = 0.0f;
		}
		else {
			mWetTimer = std::max(0.0f, mWetTimer - deltaTime);
			mOverloadTimer = std::max(0.0f, mOverloadTimer - deltaTime);
		}
		if (mWetTimer <= 0.0f) mWetArmorDamageRemainder = 0.0f;
		if (hadOverloadTimer != IsOverloaded()) UpdateAnimSpeed();
	}
	Zombie::Update();
}

bool InsulatorZombie::IsOnWetRunoffSlope() const
{
	return mBoard && mBoard->IsRoofRunoffFlowing()
		&& mBoard->IsRoofRunoffRowSelected(mRow)
		&& GetPosition().x <= mBoard->GetRoofSlopeEndX();
}

bool InsulatorZombie::IsWet() const
{
	return mWetTimer > 0.0f || IsOnWetRunoffSlope();
}

bool InsulatorZombie::IsOverloaded() const
{
	return mOverloadTimer > 0.0f && mHelmType == HelmType::HELMTYPE_INSULATOR
		&& mHelmHealth > 0 && !IsWet();
}

float InsulatorZombie::GetAbilityAnimSpeedMultiplier() const
{
	return IsOverloaded() ? kOverloadMoveMultiplier : 1.0f;
}

float InsulatorZombie::GetAbilityBiteDamageMultiplier() const
{
	return IsOverloaded() ? kOverloadBiteMultiplier : 1.0f;
}

int InsulatorZombie::ModifyProjectileDamage(int damage, BulletType bulletType) const
{
	if (damage <= 0 || IsWet() || mHelmType != HelmType::HELMTYPE_INSULATOR
		|| mHelmHealth <= 0 || !IsLightProjectile(bulletType)) {
		return damage;
	}
	return std::max(1, (damage + 1) / 2);
}

float InsulatorZombie::ModifySpikeFrameDamage(float damage, bool) const
{
	if (damage <= 0 || IsWet() || mHelmType != HelmType::HELMTYPE_INSULATOR
		|| mHelmHealth <= 0) {
		return damage;
	}
	return damage * 0.5f;
}

int InsulatorZombie::TakeHelmDamageFromSource(int damage, DamageSource source)
{
	if (damage <= 0 || mHelmType != HelmType::HELMTYPE_INSULATOR
		|| mHelmHealth <= 0 || !IsWet()
		|| (source != DamageSource::PLANT && source != DamageSource::PLANT_ASH)) {
		return Zombie::TakeHelmDamageFromSource(damage, source);
	}

	// 伤害倍率只作用于胸甲实际消费的原始份额；破甲后仍以未消费的原伤害进入本体。
	const int armorHealthBefore = mHelmHealth;
	const float exactArmorDamage = static_cast<float>(damage)
		* kWetArmorPlantMultiplier + mWetArmorDamageRemainder;
	const int boostedArmorDamage = std::max(1,
		static_cast<int>(std::floor(exactArmorDamage)));
	if (boostedArmorDamage < armorHealthBefore) {
		mWetArmorDamageRemainder = exactArmorDamage
			- static_cast<float>(boostedArmorDamage);
		TakeHelmDamage(boostedArmorDamage);
		return 0;
	}

	const float rawNeeded = (static_cast<float>(armorHealthBefore)
		- mWetArmorDamageRemainder) / kWetArmorPlantMultiplier;
	const int rawConsumed = std::clamp(
		static_cast<int>(std::ceil(rawNeeded)), 1, damage);
	mWetArmorDamageRemainder = 0.0f;
	TakeHelmDamage(armorHealthBefore);
	return damage - rawConsumed;
}

bool InsulatorZombie::CanProtectFromNightRoofCharge(const Zombie* target) const
{
	if (!target || !IsActive() || mIsDead || mIsDying || IsWet()
		|| mHelmType != HelmType::HELMTYPE_INSULATOR || mHelmHealth <= 0
		|| (mBoard && mBoard->IsNightRoofChargeProtectionSuppressed(this))
		|| !CanBeAffectedByGroundHazards() || !target->CanBeAffectedByGroundHazards()
		|| target->mRow != mRow || target->IsMindControlled() != IsMindControlled()) {
		return false;
	}
	return std::abs(target->GetPosition().x - GetPosition().x)
		<= kProtectionRadiusCells * CELL_COLLIDER_SIZE_X;
}

bool InsulatorZombie::AbsorbNightRoofChargeFor(Zombie* target, int damage)
{
	if (!CanProtectFromNightRoofCharge(target) || damage <= 0) return false;
	if (!TakeArmorDamageNoOverflow(damage)) return false;
	BeginOverload();
	return true;
}

void InsulatorZombie::TakeNightRoofChargeImpact(
	int damage, float paralysisDuration, bool onWetSlope)
{
	if (mHelmType != HelmType::HELMTYPE_INSULATOR || mHelmHealth <= 0) {
		Zombie::TakeNightRoofChargeImpact(damage, paralysisDuration, onWetSlope);
		return;
	}
	TakeArmorDamageNoOverflow(onWetSlope ? kWetSlopeDischargeDamage : damage);
	if (IsWet()) {
		if (IsActive() && !IsDying()) ApplyParalysis(paralysisDuration);
	}
	else if (!mBoard || !mBoard->IsNightRoofChargeProtectionSuppressed(this)) {
		BeginOverload();
	}
}

bool InsulatorZombie::CanBeCharred() const
{
	// 灰烬对陶瓷甲不减伤，但必须先走一类防具生命层，不能按本体阈值直接绕甲化灰。
	return mHelmType != HelmType::HELMTYPE_INSULATOR && Zombie::CanBeCharred();
}

bool InsulatorZombie::TakeArmorDamageNoOverflow(int damage)
{
	if (damage <= 0 || mHelmType != HelmType::HELMTYPE_INSULATOR
		|| mHelmHealth <= 0) {
		return false;
	}
	TakeHelmDamage(std::min(damage, mHelmHealth));
	SetGlowingTimer(0.1f);
	return true;
}

void InsulatorZombie::BeginOverload()
{
	if (IsWet() || mHelmType != HelmType::HELMTYPE_INSULATOR
		|| mHelmHealth <= 0) {
		mOverloadTimer = 0.0f;
		return;
	}
	mOverloadTimer = kOverloadSeconds;
	UpdateAnimSpeed();
}

void InsulatorZombie::CheckHelmImage()
{
	RefreshArmorPresentation();
}

void InsulatorZombie::RefreshArmorPresentation()
{
	if (mHelmType != HelmType::HELMTYPE_INSULATOR || mHelmHealth <= 0) {
		mArmorStage = ArmorBrokenState::NONE;
		if (mAnimator) {
			mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot, false);
		}
		return;
	}
	mArmorStage = mHelmHealth > kFirstCrackThreshold
		? ArmorBrokenState::NO_BROKEN
		: (mHelmHealth > kHeavyCrackThreshold
			? ArmorBrokenState::A_LITTLE_BROKEN
			: ArmorBrokenState::REALLY_BROKEN);
	if (!mArmorFollowerConfigured) ConfigureArmorFollower();
	if (!mArmorFollowerConfigured) return;
	mAnimator->SetTrackFollowerImage("Zombie_body", kArmorFollowerSlot,
		ResourceManager::GetInstance().GetTexture(ArmorImageKey(mArmorStage), false),
		kArmorFollowerOffsetX, kArmorFollowerOffsetY,
		kArmorFollowerScale, kArmorFollowerScale,
		/*drawAfterAllTracks=*/true, /*inheritOverlayEffect=*/true,
		/*inheritGlowEffect=*/true);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot,
		!mIsDead && !mIsDying);
}

void InsulatorZombie::HelmDrop()
{
	if (mHelmType != HelmType::HELMTYPE_INSULATOR) return;
	Zombie::HelmDrop();
	mArmorStage = ArmorBrokenState::NONE;
	mOverloadTimer = 0.0f;
	mWetArmorDamageRemainder = 0.0f;
	if (mAnimator) {
		mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot, false);
	}
	UpdateAnimSpeed();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CERAMIC, 0.35f);
}

void InsulatorZombie::Die()
{
	if (mAnimator) {
		mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot, false);
	}
	Zombie::Die();
}

bool InsulatorZombie::HasMagneticItem() const
{
	return mHelmType == HelmType::HELMTYPE_INSULATOR && mHelmHealth > 0;
}

bool InsulatorZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	item.textureKey = ArmorImageKey(mArmorStage);
	item.worldPosition = GetTrackWorldPosition("Zombie_body");
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(
			-kMagnetDestinationJitter, kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(
			-kMagnetDestinationJitter, kMagnetDestinationJitter));
	item.drawScale = kArmorDrawScale;
	item.extractorSelfDamage = kMagnetBacklashDamage;
	mHelmHealth = 0;
	Zombie::HelmDrop();
	mArmorStage = ArmorBrokenState::NONE;
	mOverloadTimer = 0.0f;
	mWetArmorDamageRemainder = 0.0f;
	if (mAnimator) {
		mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot, false);
	}
	UpdateAnimSpeed();
	return true;
}

void InsulatorZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mArmorFollowerConfigured || !mAnimator) return;
	if (mHelmType != HelmType::HELMTYPE_INSULATOR || mHelmHealth <= 0
		|| mArmorStage == ArmorBrokenState::NONE || mIsDead || mIsDying) {
		mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot, false);
		return;
	}
	mAnimator->SetTrackFollowerImage("Zombie_body", kArmorFollowerSlot,
		ResourceManager::GetInstance().GetTexture(ArmorImageKey(mArmorStage), false),
		kArmorFollowerOffsetX, kArmorFollowerOffsetY,
		kArmorFollowerScale, kArmorFollowerScale,
		/*drawAfterAllTracks=*/true, /*inheritOverlayEffect=*/true,
		/*inheritGlowEffect=*/true);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot, true);
}

bool InsulatorZombie::IsArmorVisible() const
{
	return mArmorFollowerConfigured && mAnimator
		&& mAnimator->GetTrackFollowerVisible("Zombie_body", kArmorFollowerSlot)
		&& mHelmType == HelmType::HELMTYPE_INSULATOR && mHelmHealth > 0;
}

void InsulatorZombie::SaveExtraData(nlohmann::json& j) const
{
	j["armorStage"] = static_cast<int>(mArmorStage);
	j["wetTimer"] = mWetTimer;
	j["overloadTimer"] = mOverloadTimer;
	j["wetArmorDamageRemainder"] = mWetArmorDamageRemainder;
}

void InsulatorZombie::LoadExtraData(const nlohmann::json& j)
{
	mWetTimer = std::clamp(j.value("wetTimer", 0.0f), 0.0f, kMaximumSavedTimer);
	mOverloadTimer = std::clamp(
		j.value("overloadTimer", 0.0f), 0.0f, kMaximumSavedTimer);
	mWetArmorDamageRemainder = std::clamp(
		j.value("wetArmorDamageRemainder", 0.0f), 0.0f, 0.999f);
	if (mWetTimer > 0.0f || mHelmType != HelmType::HELMTYPE_INSULATOR
		|| mHelmHealth <= 0) {
		mOverloadTimer = 0.0f;
	}
	RefreshArmorPresentation();
	UpdateAnimSpeed();
}
