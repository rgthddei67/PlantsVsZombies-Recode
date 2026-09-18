#include "ExcavatorZombie.h"
#include "Game/Board/Board.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr int kHealth = 1800; // 本体生命，保证工兵有接近关键石块的耐久；帽子不提供护甲
	constexpr float kWorkSeconds = 4.0f; // 无减速时施工所需游戏秒
	constexpr float kRetrySeconds = 3.0f; // 取消或结束啃食后的重试游戏秒
	constexpr float kApproachMultiplier = 3.0f; // 锁定工地后赶路的动画与根运动倍率，施工和啃食不加速
	constexpr float kHatX = -5.0f, kHatY = -15.0f; // 矿灯帽相对头轨原点，局部像素
	constexpr float kToolX = -48.0f, kToolY = 14.0f; // 凿岩机握持点相对内前臂，局部像素
	constexpr float kVibrationPixels = 1.0f; // 施工机械往复幅度，局部像素
	constexpr float kVibrationFrequency = 60.0f; // 施工往复角速度，弧度/施工秒
	constexpr float kFacingPivot = 48.0f; // 沿用普通魅惑身体中线，局部像素
}

void ExcavatorZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = kHealth;
	SyncEquipment();
}

void ExcavatorZombie::Update()
{
	if (!mIsPreview) {
		if (HasTask() && (!mBoard || mWall < 0 || mWall >= MineGrid::Count
			|| !mBoard->mMineGrid.rock[mWall])) Abort(Phase::RETRY);
		if (mPhase == Phase::RETRY && !mIsEating) {
			mRetry = std::max(0.0f,mRetry - DeltaTime::GetDeltaTime());
			if (mRetry <= 0.0f) mPhase = Phase::READY;
		}
	}
	Zombie::Update();
	if (!mIsPreview && mAnimator && !mIsDying) mAnimator->SetFlipX(IsMovingRight(),kFacingPivot);
	// 基类硬控/垂死早退也不能留下施工占用；保留已耗尽工具的损坏外观。
	if (!mIsPreview && mPhase != Phase::SPENT && mPhase != Phase::DISABLED
		&& (!IsActive() || mIsDying || !HasHead() || IsMindControlled())) Abort(Phase::DISABLED);
	SyncEquipment();
}

int ExcavatorZombie::SelectMineNextCell(int cell)
{
	if (!mBoard || mIsEating || IsMindControlled() || !HasHead() || mIsDying) return -2;
	if (mPhase == Phase::READY) {
		if (!mBoard->ReserveMineExcavation(this,cell,mWall,mStand)) {
			Abort(Phase::RETRY);
			return -2;
		}
		mPhase = Phase::APPROACHING;
		UpdateAnimSpeed();
	}
	if (mPhase != Phase::APPROACHING) return mPhase == Phase::DRILLING ? -1 : -2;
	if (cell == mStand) {
		mPhase = Phase::DRILLING;
		UpdateAnimSpeed();
		mRemaining = kWorkSeconds;
		PlayTrack("anim_idle",1.0f,0.1f);
		return -1;
	}
	const int next = mBoard->mMineGrid.NextWork(cell,mStand);
	if (next < 0) { Abort(Phase::RETRY); return -2; }
	return next;
}

bool ExcavatorZombie::IsMovingRight() const
{
	return IsMindControlled() || (mBoard && mMineTargetCell >= 0
		&& mBoard->GetCellCenterPosition(mMineTargetCell / MineGrid::Columns,
			mMineTargetCell % MineGrid::Columns).x > GetPosition().x + 0.01f);
}

void ExcavatorZombie::ZombieMove(float delta, Transform* transform)
{
	if (mPhase != Phase::DRILLING) Zombie::ZombieMove(delta,transform);
}

float ExcavatorZombie::GetAbilityAnimSpeedMultiplier() const
{
	// 根运动已消费动画倍率，不能在 ZombieMove 再乘三；死亡与取消任务即时恢复。
	return mPhase == Phase::APPROACHING && HasHead() && !IsDying() && !IsMindControlled()
		&& !mIsEating ? kApproachMultiplier : 1.0f;
}

void ExcavatorZombie::ZombieUpdate(float delta)
{
	if (mPhase != Phase::DRILLING) return;
	// 基类只在无硬控且未啃食时进入这里；delta 已包含普通减速倍率。
	mRemaining = std::max(0.0f,mRemaining - delta);
	if (mRemaining > 0.0f) return;
	const bool completed = mBoard && mBoard->CompleteMineExcavation(mWall,true);
	Abort(completed ? Phase::SPENT : Phase::RETRY);
	if (completed) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DIRT_RISE,0.45f);
		if (g_particleSystem) g_particleSystem->EmitEffect("ExcavatorSmoke",
			GetRenderedTrackWorldPosition("anim_innerarm2") + Vector(-10,8));
	}
}

void ExcavatorZombie::Abort(Phase next)
{
	if (mBoard && mWall >= 0 && mWall < MineGrid::Count && mBoard->mMineWallOwners[mWall] == mZombieID)
		mBoard->mMineWallOwners[mWall] = NULL_ZOMBIE_ID;
	const bool wasDrilling = mPhase == Phase::DRILLING;
	mWall = mStand = -1;
	mRemaining = 0.0f;
	mRetry = next == Phase::RETRY ? kRetrySeconds : 0.0f;
	mPhase = next;
	UpdateAnimSpeed();
	if (wasDrilling && !mIsEating && !mIsDying && IsActive()) PlayWalkAnimation(0.1f);
}

void ExcavatorZombie::OnStartEating()
{
	if (HasTask()) Abort(Phase::RETRY);
}

void ExcavatorZombie::OnStopEating()
{
	if (mPhase == Phase::RETRY) mRetry = kRetrySeconds;
}

void ExcavatorZombie::OnMindControlled()
{
	if (mPhase != Phase::SPENT) Abort(Phase::DISABLED);
}

bool ExcavatorZombie::InterruptUncommittedSpecialAction()
{
	if (!HasTask()) return false;
	Abort(Phase::RETRY);
	return true;
}

float ExcavatorZombie::GetInterruptibleSpecialActionRemaining() const
{
	return HasTask() ? (mPhase == Phase::DRILLING ? mRemaining : kWorkSeconds) : -1.0f;
}

float ExcavatorZombie::GetWorkProgress() const
{
	return mPhase == Phase::DRILLING ? 1.0f - mRemaining / kWorkSeconds : 0.0f;
}

float ExcavatorZombie::GetWorkSeconds() { return kWorkSeconds; }
float ExcavatorZombie::GetApproachMultiplier() { return kApproachMultiplier; }

void ExcavatorZombie::HeadDrop()
{
	if (!HasHead()) return;
	const Vector origin = GetRenderedTrackWorldPosition("anim_head1");
	for (const char* track : {"anim_head1","anim_head2","anim_tongue","anim_hair"})
		mAnimator->SetTrackVisible(track,false);
	mAnimator->SetTrackFollowerVisible("anim_head1","excavator_hat",false);
	if (mPhase != Phase::SPENT) Abort(Phase::DISABLED);
	if (g_particleSystem) g_particleSystem->EmitEffect("ExcavatorHeadOff",origin + Vector(25,15));
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP,0.25f);
}

void ExcavatorZombie::Die()
{
	Abort(mPhase == Phase::SPENT ? Phase::SPENT : Phase::DISABLED);
	Zombie::Die();
}

void ExcavatorZombie::SyncEquipment() const
{
	if (!mAnimator) return;
	auto& resources = ResourceManager::GetInstance();
	mAnimator->SetTrackFollowerImage("anim_head1","excavator_hat",
		resources.GetTexture("IMAGE_EXCAVATOR_HAT",false),kHatX,kHatY,1,1,true);
	mAnimator->SetTrackFollowerVisible("anim_head1","excavator_hat",HasHead());
	const float shake = mPhase == Phase::DRILLING
		? std::sin(mRemaining * kVibrationFrequency) * kVibrationPixels : 0.0f;
	mAnimator->SetTrackFollowerImage("anim_innerarm2","excavator_drill",
		resources.GetTexture(mPhase == Phase::SPENT ? "IMAGE_EXCAVATOR_BROKEN" : "IMAGE_EXCAVATOR_DRILL",false),
		kToolX + shake,kToolY,1,1,false);
	mAnimator->SetTrackFollowerVisible("anim_innerarm2","excavator_drill",true);
}

void ExcavatorZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	SyncEquipment();
}

void ExcavatorZombie::SaveExtraData(nlohmann::json& j) const
{
	j["excavatorPhase"] = static_cast<int>(mPhase);
	j["wall"] = mWall; j["stand"] = mStand;
	j["workRemaining"] = mRemaining; j["retryRemaining"] = mRetry;
}

void ExcavatorZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<Phase>(std::clamp(j.value("excavatorPhase",0),0,5));
	mWall = j.value("wall",-1); mStand = j.value("stand",-1);
	mRemaining = std::clamp(j.value("workRemaining",0.0f),0.0f,kWorkSeconds);
	mRetry = std::clamp(j.value("retryRemaining",0.0f),0.0f,kRetrySeconds);
	if (mPhase != Phase::SPENT && (!HasHead() || IsMindControlled() || mIsDying)) Abort(Phase::DISABLED);
	else if (HasTask()) {
		if (!mBoard || !mBoard->IsMineBackground() || mWall < 0 || mWall >= MineGrid::Count
			|| mStand < 0 || mStand >= MineGrid::Count || !mBoard->mMineGrid.rock[mWall]
			|| mStand % MineGrid::Columns < 2
			|| std::abs(mStand / MineGrid::Columns - mWall / MineGrid::Columns)
				+ std::abs(mStand % MineGrid::Columns - mWall % MineGrid::Columns) != 1
			|| mBoard->mMineGrid.rock[mStand] || !mBoard->mMineGrid.connected[mStand]
			|| mIsEating || mBoard->GetMineWallOwner(mWall)) Abort(Phase::RETRY);
		else mBoard->mMineWallOwners[mWall] = mZombieID;
	}
	if (!HasTask()) mWall = mStand = -1;
	SyncEquipment();
}
