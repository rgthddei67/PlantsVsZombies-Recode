#include "Squash.h"

#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../Zombie/FootballZombie.h"
#include "../Zombie/Polevaulter.h"
#include "../Zombie/Zombie.h"
#include "../../ParticleSystem/ParticleSystem.h"

#include <algorithm>
#include <cfloat>

namespace {
	constexpr float kLookDuration = 0.8f;             // 原版观察目标的持续时间，单位：秒
	constexpr float kPreLaunchDuration = 0.3f;        // 原版起跳动画预备时间，单位：秒
	constexpr float kRiseDuration = 0.5f;             // 原版上升状态总时长，单位：秒
	constexpr float kRiseMoveDuration = 0.3f;         // 上升阶段实际插值移动时长，余下 0.2 秒停在最高点
	constexpr float kFallDuration = 0.1f;             // 原版从最高点砸到地面的时长，单位：秒
	constexpr float kDamageTimeLeft = 0.04f;          // 原版 falling countdown==4 的伤害触发点，单位：秒
	constexpr float kLandedDuration = 1.0f;           // 草地落地后压扁造型保留时间，单位：秒
	constexpr float kJumpHeight = 120.0f;             // 原版跳到落点上方的高度，单位：像素
	constexpr float kLandingOffsetY = 8.0f;           // 原版落点相对格子顶点的 +8，换算后仍为中心 +8 像素
	constexpr float kTargetLeadSeconds = 0.3f;        // 起跳前按原版 30 厘秒预测目标水平位置
	constexpr float kTriggerGap = 70.0f;              // 普通僵尸触发观察的最大水平间隙，单位：像素
	constexpr float kEatingTriggerGap = 110.0f;       // 正在啃食时放宽的最大水平间隙，单位：像素
	constexpr float kAttackRectLeft = -20.0f;         // C# mX+20 换算到本项目格子中心后的攻击矩形左偏移
	constexpr float kAttackRectWidth = 45.0f;         // 原版倭瓜攻击矩形宽度，单位：像素
	constexpr float kSpecialBehindReach = 60.0f;      // 撑杆/橄榄球等目标允许落在身后仍被锁定的距离
	constexpr float kFootballDamageGap = 20.0f;       // 原版橄榄球僵尸允许与砸击矩形相隔 20 像素仍受伤
	constexpr int kSquashDamage = 1800;               // 原版倭瓜单次普通伤害
	constexpr float kLookAndJumpUpSpeed = 2.0f;       // 24fps / Squash.reanim 基础 12fps
	constexpr float kJumpDownSpeed = 5.0f;            // 60fps / Squash.reanim 基础 12fps
	constexpr float kShadowOffsetX = 3.0f;            // 倭瓜阴影相对落点的水平偏移，单位：像素
	constexpr float kShadowOffsetY = 25.0f;           // 倭瓜站立时阴影相对格子中心的垂直偏移，单位：像素

	float HorizontalOverlap(const SDL_FRect& a, const SDL_FRect& b)
	{
		const float left = std::max(a.x, b.x);
		const float right = std::min(a.x + a.w, b.x + b.w);
		return right - left;
	}

	float SmoothStep(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
}

void Squash::SetupPlant()
{
	SetAnimationSpeed(GameRandom::Range(10.0f / 12.0f, 15.0f / 12.0f));
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(kShadowOffsetX, kShadowOffsetY));
		shadow->SetScale(Vector(1.3f, 1.3f));
	}
	mTargetX = GetPosition().x;
}

void Squash::PlantUpdate()
{
	const float deltaTime = DeltaTime::GetDeltaTime();
	switch (mState) {
	case State::IDLE:
		if (mBoard) {
			if (Zombie* target = mBoard->mEntityRegistry.GetZombie(FindTargetZombieID())) {
				StartLooking(target);
			}
		}
		break;
	case State::LOOKING:
		mStateTimer -= deltaTime;
		if (mStateTimer <= 0.0f) StartPreLaunch();
		break;
	case State::PRELAUNCH:
		mStateTimer -= deltaTime;
		if (mStateTimer <= 0.0f) StartRising();
		break;
	case State::RISING:
		mStateTimer -= deltaTime;
		UpdateRisingPosition();
		if (mStateTimer <= 0.0f) StartFalling();
		break;
	case State::FALLING:
		mStateTimer -= deltaTime;
		UpdateFallingPosition();
		if (!mDamageApplied && mStateTimer <= kDamageTimeLeft) {
			ApplySquashDamage();
		}
		if (mStateTimer <= 0.0f) FinishFalling();
		break;
	case State::LANDED:
		mStateTimer -= deltaTime;
		if (mStateTimer <= 0.0f) Die();
		break;
	}
	UpdateShadowOffset();
}

void Squash::TakeDamage(int damage, DamageSource source)
{
	if (mState == State::IDLE) {
		Plant::TakeDamage(damage, source);
	}
}

bool Squash::CanBeEaten() const
{
	// C# NotOnGround 只把离地后的三个阶段排除；观察和起跳预备仍允许僵尸选中。
	return Plant::CanBeEaten()
		&& mState != State::RISING
		&& mState != State::FALLING
		&& mState != State::LANDED;
}

bool Squash::OccupiesGridSlot() const
{
	return Plant::OccupiesGridSlot()
		&& mState != State::RISING
		&& mState != State::FALLING
		&& mState != State::LANDED;
}

void Squash::ResolveGargantuarSmash()
{
	// C# 的 Plant::Squish 只允许尚未索敌的 Notready 倭瓜进入压扁态。
	if (mState == State::IDLE) {
		Plant::ResolveGargantuarSmash();
	}
}

int Squash::FindTargetZombieID() const
{
	if (!mBoard) return NULL_ZOMBIE_ID;

	if (Zombie* current = mBoard->mEntityRegistry.GetZombie(mTargetZombieID)) {
		float ignoredScore = 0.0f;
		if (IsValidTarget(current, ignoredScore)) return current->mZombieID;
	}

	int closestID = NULL_ZOMBIE_ID;
	float closestScore = FLT_MAX;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		float score = 0.0f;
		if (!IsValidTarget(zombie, score)) return;
		if (score < closestScore) {
			closestScore = score;
			closestID = zombie->mZombieID;
		}
	});
	return closestID;
}

bool Squash::IsValidTarget(Zombie* zombie, float& score) const
{
	if (!zombie || zombie->IsMindControlled() || zombie->IsDying() || !zombie->HasHead()
		|| !zombie->CanBeTargetedByProjectile(false)) {
		return false;
	}
	if (mBoard->MineBlocksSegment(GetPosition(), zombie->GetPosition())) return false;
	auto* collider = zombie->GetColliderComponent();
	if (!collider || !collider->mEnabled || IsTargetedByOtherSquash(zombie->mZombieID)) {
		return false;
	}
	if (auto* polevaulter = dynamic_cast<Polevaulter*>(zombie);
		polevaulter && polevaulter->mVaultState != Polevaulter::VaultState::WALKING) {
		return false;
	}

	const Vector position = GetPosition();
	const SDL_FRect attackRect{
		position.x + kAttackRectLeft,
		position.y - 50.0f,
		kAttackRectWidth,
		80.0f,
	};
	const SDL_FRect zombieRect = collider->GetBoundingBox();
	score = -HorizontalOverlap(attackRect, zombieRect);
	const float maxGap = zombie->IsEating() ? kEatingTriggerGap : kTriggerGap;
	if (score > maxGap) return false;

	float minimumTargetRight = attackRect.x;
	if (dynamic_cast<FootballZombie*>(zombie) || dynamic_cast<Polevaulter*>(zombie)) {
		minimumTargetRight -= kSpecialBehindReach;
	}
	return zombieRect.x + zombieRect.w >= minimumTargetRight;
}

bool Squash::IsTargetedByOtherSquash(int zombieID) const
{
	if (!mBoard || zombieID == NULL_ZOMBIE_ID) return false;
	for (int plantID : mBoard->mEntityRegistry.GetAllPlantIDs()) {
		Plant* plant = mBoard->mEntityRegistry.GetPlant(plantID);
		auto* squash = dynamic_cast<Squash*>(plant);
		if (squash && squash != this && squash->mTargetZombieID == zombieID) {
			return true;
		}
	}
	return false;
}

void Squash::StartLooking(Zombie* target)
{
	mTargetZombieID = target->mZombieID;
	mTargetX = target->GetTargetLeadX(0.0f);
	mState = State::LOOKING;
	mStateTimer = kLookDuration;
	PlayTrackOnce(mTargetX < GetPosition().x ? "anim_lookleft" : "anim_lookright",
		"", kLookAndJumpUpSpeed, 0.1f);

	const bool useAlternateHmm = GameRandom::Range(0, 2) == 2;
	AudioSystem::PlaySound(useAlternateHmm
		? ResourceKeys::Sounds::SOUND_SQUASH_HMM2
		: ResourceKeys::Sounds::SOUND_SQUASH_HMM, 0.45f);
}

void Squash::StartPreLaunch()
{
	mState = State::PRELAUNCH;
	mStateTimer = kPreLaunchDuration;
	PlayTrackOnce("anim_jumpup", "", kLookAndJumpUpSpeed, 0.2f);
}

void Squash::StartRising()
{
	if (mBoard) {
		if (Zombie* target = mBoard->mEntityRegistry.GetZombie(FindTargetZombieID())) {
			mTargetZombieID = target->mZombieID;
			mTargetX = target->GetTargetLeadX(kTargetLeadSeconds);
		}
	}
	mState = State::RISING;
	mStateTimer = kRiseDuration;
	mDamageApplied = false;
	// C# 以 NotOnGround 让起跳倭瓜退出种植判定；显式 Cell 槽必须在同一状态边沿释放。
	ReleaseGridSlot();
	if (mCollider) mCollider->mEnabled = false;
	UpdateRisingPosition();
}

void Squash::StartFalling()
{
	mState = State::FALLING;
	mStateTimer = kFallDuration;
	PlayTrackOnce("anim_jumpdown", "", kJumpDownSpeed);
	UpdateFallingPosition();
}

void Squash::FinishFalling()
{
	SetPosition(Vector(mTargetX, GetLandingY()));
	UpdateShadowOffset();

	if (mBoard && mBoard->IsPoolSquare(mRow, GetLandingColumn()) && GetPosition().x <= 965) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("SquashPoolSplash", GetPosition() + Vector(0.0f, 25.0f));
		}
		const int splat = GameRandom::Range(0, 2);
		const std::string& splatSound = splat == 0
			? ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1
			: (splat == 1 ? ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY2
				: ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY3);
		AudioSystem::PlaySound(splatSound, 0.35f);
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIESPLASH, 0.45f);
		Die();
		return;
	}

	mState = State::LANDED;
	mStateTimer = kLandedDuration;
	if (mBoard) mBoard->ShakeBoard(1.0f, 4.0f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_GARGANTUAR_THUMP, 0.45f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("DustSquash", GetPosition() + Vector(0.0f, 30.0f));
	}
}

void Squash::ApplySquashDamage()
{
	std::vector<int> targets;
	if (mDamageApplied || !mBoard) return;
	mDamageApplied = true;

	const Vector position = GetPosition();
	const SDL_FRect attackRect{
		position.x + kAttackRectLeft,
		position.y - 50.0f,
		kAttackRectWidth,
		80.0f,
	};
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (!zombie || zombie->IsMindControlled() || zombie->IsDying()
			|| !zombie->CanBeTargetedByProjectile(false)) return;
		auto* collider = zombie->GetColliderComponent();
		if (!collider || !collider->mEnabled) return;

		const float overlap = HorizontalOverlap(attackRect, collider->GetBoundingBox());
		const float requiredOverlap = dynamic_cast<FootballZombie*>(zombie)
			? -kFootballDamageGap : 0.0f;
		if (overlap > requiredOverlap) {
			targets.push_back(zombie->mZombieID);
		}
	});
	mBoard->ApplyPlantAshAttack(targets, [](Zombie* zombie) {
			// C# 使用 TakeDamage(1800, 18U)：普通体量会走 bit4 的立即死亡，
			// 只有本体耐久超过 1800 的重型目标才保留并承受伤害；bit1 同时穿透二类护盾。
			if (zombie->mZombieType == ZombieType::ZOMBIE_REINFORCED_DOOR)
			{
				zombie->TakeDamage(kSquashDamage, DamageSource::PLANT_ASH,
					true, false, false, PlantDamageOrigin::Ash());
				return;
			}
			else if (zombie->mZombieType == ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER)
			{
				zombie->TakeDamage(kSquashDamage, DamageSource::PLANT_ASH,
					true, false, false, PlantDamageOrigin::Ash());
				return;
			}
			else if (zombie->mZombieType == ZombieType::ZOMBIE_ADAPTIVE_HELMET)
			{
				zombie->TakeDamage(kSquashDamage, DamageSource::PLANT_ASH,
					true, false, false, PlantDamageOrigin::Ash());
				return;
			}

			if (zombie->mBodyHealth <= kSquashDamage && 
				zombie->mZombieType != ZombieType::ZOMBIE_GARGANTUAR && 
				zombie->mZombieType != ZombieType::ZOMBIE_REDEYE_GARGANTUAR)
			{
				zombie->Die();
			}
			else 
			{
				zombie->TakeDamage(kSquashDamage, DamageSource::PLANT_ASH, true,
					false, false, PlantDamageOrigin::Ash());
			}
	});
}

void Squash::RestoreStatePosition()
{
	if (!OccupiesGridSlot()) ReleaseGridSlot();
	switch (mState) {
	case State::RISING:
		UpdateRisingPosition();
		break;
	case State::FALLING:
		UpdateFallingPosition();
		break;
	case State::LANDED:
		SetPosition(Vector(mTargetX, GetLandingY()));
		break;
	default:
		break;
	}
	if (mCollider) {
		mCollider->mEnabled = mState == State::IDLE
			|| mState == State::LOOKING || mState == State::PRELAUNCH;
	}
	UpdateShadowOffset();
}

void Squash::UpdateRisingPosition()
{
	if (!mBoard) return;
	const Vector start = mBoard->GetCellCenterPosition(mRow, mColumn);
	const Vector end(mTargetX, GetLandingY() - kJumpHeight);
	const float linear = (kRiseDuration - mStateTimer) / kRiseMoveDuration;
	const float eased = SmoothStep(linear);
	SetPosition(start + (end - start) * eased);
}

void Squash::UpdateFallingPosition()
{
	const float progress = std::clamp(1.0f - mStateTimer / kFallDuration, 0.0f, 1.0f);
	SetPosition(Vector(mTargetX, GetLandingY() - kJumpHeight * (1.0f - progress)));
}

void Squash::UpdateShadowOffset()
{
	if (!mBoard) return;
	if (auto* shadow = GetShadow()) {
		const float groundY = mBoard->GetCellCenterPosition(mRow, mColumn).y;
		shadow->SetOffset(Vector(kShadowOffsetX,
			kShadowOffsetY + groundY - GetPosition().y));
	}
}

float Squash::GetLandingY() const
{
	if (!mBoard) return GetPosition().y;
	return mBoard->GetCellCenterPosition(mRow, GetLandingColumn()).y + kLandingOffsetY;
}

int Squash::GetLandingColumn() const
{
	if (!mBoard || mBoard->mColumns <= 0) return mColumn;
	const int column = static_cast<int>(
		(mTargetX - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X);
	return std::clamp(column, 0, mBoard->mColumns - 1);
}

void Squash::SaveExtraData(nlohmann::json& j) const
{
	j["state"] = static_cast<int>(mState);
	j["stateTimer"] = mStateTimer;
	j["targetZombieID"] = mTargetZombieID;
	j["targetX"] = mTargetX;
	j["damageApplied"] = mDamageApplied;
}

void Squash::LoadExtraData(const nlohmann::json& j)
{
	const int state = j.value("state", static_cast<int>(State::IDLE));
	mState = state >= static_cast<int>(State::IDLE) && state <= static_cast<int>(State::LANDED)
		? static_cast<State>(state) : State::IDLE;
	mStateTimer = std::max(0.0f, j.value("stateTimer", 0.0f));
	mTargetZombieID = j.value("targetZombieID", NULL_ZOMBIE_ID);
	mTargetX = j.value("targetX", GetPosition().x);
	mDamageApplied = j.value("damageApplied", false);
	RestoreStatePosition();
}

const char* Squash::GetSquashStateName() const
{
	switch (mState) {
	case State::IDLE: return "IDLE";
	case State::LOOKING: return "LOOKING";
	case State::PRELAUNCH: return "PRELAUNCH";
	case State::RISING: return "RISING";
	case State::FALLING: return "FALLING";
	case State::LANDED: return "LANDED";
	}
	return "UNKNOWN";
}
