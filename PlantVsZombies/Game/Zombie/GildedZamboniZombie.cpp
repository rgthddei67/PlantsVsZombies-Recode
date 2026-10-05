#include "GildedZamboniZombie.h"
#include "GoldenIceRules.h"

#include "ZombieBirthVitalsRules.h"
#include "../../DeltaTime.h"
#include "../../GameRandom.h"
#include "Game/Board/Board.h"

#include <algorithm>

namespace {
	constexpr int kGildedZamboniHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_GILDED_ZAMBONI).body;             // 鎏金冰车本体血量
	constexpr float kGildedBaseDriveMultiplier = 0.72f;   // 相对普通冰车速度曲线的基础移速倍率
	constexpr int kCaltropHitDamage = 100;                 // 地刺每次扎中鎏金冰车造成的固定基础伤害
	constexpr int kChomperBiteDamage = 50;                 // 鎏金冰车拒吞时保留的特殊基础伤害
	constexpr float kPreviewDriveAnimSpeedMin = 0.50f;    // 慢速车辆驾驶动画最小基础倍率
	constexpr float kPreviewDriveAnimSpeedMax = 0.65f;    // 慢速车辆驾驶动画最大基础倍率
}

void GildedZamboniZombie::SetupZombie()
{
	// 完整复用普通冰车的车辆碰撞、死亡、破损和既有动画时间线，不注册新帧事件。
	ZamboniZombie::SetupZombie();
	mBodyMaxHealth = kGildedZamboniHealth;
	mBodyHealth = kGildedZamboniHealth;
	mGoldenTrailMinX = mBoard ? mBoard->GetIceTrailRightX() : 0.0f;
	SetAnimationSpeed(GameRandom::Range(
		kPreviewDriveAnimSpeedMin, kPreviewDriveAnimSpeedMax));
}

void GildedZamboniZombie::Update()
{
	ZamboniZombie::Update();
	if (mIsPreview || mIsDead) return;
	UpdateAcceleration(DeltaTime::GetDeltaTime());
}

void GildedZamboniZombie::UpdateAcceleration(float deltaTime)
{
	if (deltaTime <= 0.0f || mAccelerationStage >= 3) return;
	mUndamagedTime = std::min(GoldenIceRules::ThirdAcceleration, mUndamagedTime + deltaTime);
	const int nextStage = GoldenIceRules::AccelerationStage(mUndamagedTime);
	if (nextStage == mAccelerationStage) return;

	mAccelerationStage = nextStage;
	UpdateAnimSpeed();
}

void GildedZamboniZombie::ResetAcceleration()
{
	const bool hadAcceleration = mAccelerationStage > 0;
	mUndamagedTime = 0.0f;
	mAccelerationStage = 0;
	if (hadAcceleration) {
		UpdateAnimSpeed();
	}
}

void GildedZamboniZombie::TakeBodyDamage(int damage)
{
	if (damage <= 0 || mIsDead) return;
	ResetAcceleration();
	ZamboniZombie::TakeBodyDamage(damage);
}

bool GildedZamboniZombie::TakePlantInstantKill()
{
	// 车辆只拒绝吞食；实际数值由专属伤害调整入口保留为 50。
	return false;
}

int GildedZamboniZombie::AdjustRejectedChomperBiteDamage(int /*damage*/) const
{
	return kChomperBiteDamage;
}

bool GildedZamboniZombie::HandleCaltropHit(Caltrop& /*caltrop*/)
{
	if (mIsDead) return true;
	// 地刺保留在场并按自身攻击周期继续扎刺；车辆不会爆胎或进入延迟死亡。
	TakeDamage(kCaltropHitDamage, DamageSource::PLANT);
	return true;
}

void GildedZamboniZombie::LayIceTrails(const Vector& stableVisualOrigin)
{
	if (!mBoard) return;
	const float frontX = GetIceTrailFrontX(stableVisualOrigin);
	mGoldenTrailMinX = mGoldenTrailMinX > 0.0f
		? std::min(mGoldenTrailMinX, frontX) : frontX;
	for (int row = mRow - 1; row <= mRow + 1; ++row) {
		// 铺路范围独立于碾压范围；Board 另行集中拒绝越界与水路。
		mBoard->ExtendGoldenIceTrail(row, frontX);
	}
}

bool GildedZamboniZombie::CanCrushRow(int row) const
{
	return row == mRow;
}

float GildedZamboniZombie::GetBaseDriveSpeedMultiplier() const
{
	return kGildedBaseDriveMultiplier;
}

float GildedZamboniZombie::GetAbilityAnimSpeedMultiplier() const
{
	return static_cast<float>(1 << std::clamp(mAccelerationStage, 0, 3));
}

float GildedZamboniZombie::GetAmplifiedAbilitySpeedMultiplier() const
{
	return GetAccelerationMultiplier();
}

float GildedZamboniZombie::GetAccelerationMultiplier() const
{
	return std::min(GoldenIceRules::AccelerationCap,
		AmplifySpeedMultiplierForGoldenIce(GetAbilityAnimSpeedMultiplier()));
}

bool GildedZamboniZombie::ProvidesGoldenIceEffectAt(
	int row, float worldX, bool includeVehicleBody) const
{
	if (!mBoard || mIsPreview || mIsDead || std::abs(row - mRow) > 1
		|| mBoard->IsPoolRow(row)) return false;

	const float leftX = includeVehicleBody
		? std::min(mGoldenTrailMinX, GetPosition().x - GoldenIceRules::BodyPadding)
		: mGoldenTrailMinX;
	return worldX >= leftX && worldX <= mBoard->GetIceTrailRightX();
}

void GildedZamboniZombie::SaveExtraData(nlohmann::json& j) const
{
	ZamboniZombie::SaveExtraData(j);
	j["undamagedTime"] = mUndamagedTime;
	j["goldenTrailMinX"] = mGoldenTrailMinX;
}

void GildedZamboniZombie::LoadExtraData(const nlohmann::json& j)
{
	ZamboniZombie::LoadExtraData(j);
	mUndamagedTime = std::clamp(
		j.value("undamagedTime", 0.0f), 0.0f, GoldenIceRules::ThirdAcceleration);
	mAccelerationStage = GoldenIceRules::AccelerationStage(mUndamagedTime);
	const float trailRight = mBoard ? mBoard->GetIceTrailRightX() : mGoldenTrailMinX;
	mGoldenTrailMinX = std::clamp(
		j.value("goldenTrailMinX", trailRight), 25.0f, trailRight);
	UpdateAnimSpeed();
}

ZombieMovementRules::BirthProfile GildedZamboniZombie::GetBirthMovementProfile()
{
	auto p=ZamboniZombie::GetBirthMovementProfile();
	p.velocityMinimum*=kGildedBaseDriveMultiplier;
	p.velocityMaximum*=kGildedBaseDriveMultiplier;
	return p;
}
