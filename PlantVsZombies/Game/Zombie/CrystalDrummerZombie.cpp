#include "CrystalDrummerZombie.h"
#include "Game/Board/Board.h"
#include "Game/AudioSystem.h"
#include "ResourceManager.h"
#include <algorithm>
#include <cmath>

namespace {
	// 时间锚持久化编码，不能因后续添加阶段而重排。
	enum class DrumSnapshotPhase { WAITING, WINDUP, DISABLED };
	constexpr int kHealth=1600; // 鼓手本体生命，晶鼓不是额外防具
	constexpr int kRange=3; // 鼓舞沿连通矿道的最大格数
	constexpr float kBeatInterval=5.0f; // 两次敲响之间的基础游戏秒，包含前摇
	constexpr float kWindup=1.5f; // 停步敲鼓前摇，游戏秒
	constexpr float kFirstWait=0.5f; // 首拍开始前的等待游戏秒；后续敲鼓与打断仍用完整周期
	constexpr float kPulse=0.45f; // 敲响后晶鼓高亮时间，游戏秒
}

void CrystalDrummerZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth=mBodyMaxHealth=kHealth;
	mRemaining=kFirstWait;
	SyncEquipment();
}

void CrystalDrummerZombie::Update()
{
	if (!mIsPreview && mBoard && IsActive() && !mIsDead && !mIsDying) {
		if (!HasHead() || IsMindControlled()) FinishBeat(true);
		const float delta=DeltaTime::GetDeltaTime();
		mPulseRemaining=std::max(0.0f,mPulseRemaining-delta);
		if (!mDisabled && !IsImmobilized()) {
			// 鼓舞只加速移动与啃食；技能本身按游戏秒推进，普通冰减速沿用既有动作口径。
			mRemaining-=delta*(GetCooldownTimer()>0.0f ? 0.5f : 1.0f);
			if (mRemaining<=0.0f) {
				const float overshoot=-mRemaining;
				if (mWindingUp) {
					EmitInspiration();
					FinishBeat(false);
				} else {
					CancelEatingForSpecialAction();
					mWindingUp=true;
					mRemaining=kWindup;
					PlayTrack("anim_idle",1.0f,0.12f);
				}
				mRemaining=std::max(0.0f,mRemaining-overshoot);
			}
		}
	}
	Zombie::Update();
	SyncEquipment();
}

void CrystalDrummerZombie::EmitInspiration()
{
	const float left=mBoard->GetCellCenterPosition(mRow,0).x-CELL_COLLIDER_SIZE_X*0.5f;
	const int col=static_cast<int>(std::floor((GetPosition().x-left)/CELL_COLLIDER_SIZE_X));
	for (const int id : mBoard->mEntityRegistry.GetAllZombieIDs()) {
		Zombie* target=mBoard->mEntityRegistry.GetZombie(id);
		if (!target || target==this || !target->IsActive() || target->IsDying() || target->IsMindControlled()) continue;
		const int targetCol=static_cast<int>(std::floor((target->GetPosition().x-left)/CELL_COLLIDER_SIZE_X));
		if (mBoard->IsCellWithinConnectedRange(mRow,col,target->mRow,targetCol,kRange)) target->ApplyDrumInspiration(mZombieID);
	}
	++mBeatCount;
	mPulseRemaining=kPulse;
	AudioSystem::PlaySound("SOUND_GARGANTUAR_THUMP",0.2f);
}

void CrystalDrummerZombie::FinishBeat(bool disabled)
{
	const bool wasWinding=mWindingUp;
	mWindingUp=false;
	mDisabled=disabled;
	mRemaining=disabled ? 0.0f : kBeatInterval-kWindup;
	if (wasWinding && !mIsDying && !mIsDead && !mIsEating) PlayWalkAnimation(0.12f);
}

void CrystalDrummerZombie::StartEat(ColliderComponent* other)
{
	if (!mWindingUp) Zombie::StartEat(other);
}

void CrystalDrummerZombie::ZombieMove(float delta, Transform* transform)
{
	if (!mWindingUp) Zombie::ZombieMove(delta,transform);
}

float CrystalDrummerZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mWindingUp && !mIsDead && !mIsDying ? mRemaining : -1.0f;
}

bool CrystalDrummerZombie::InterruptUncommittedSpecialAction()
{
	if (GetInterruptibleSpecialActionRemaining()<0.0f) return false;
	FinishBeat(false);
	return true;
}

void CrystalDrummerZombie::HeadDrop() { FinishBeat(true); Zombie::HeadDrop(); }
void CrystalDrummerZombie::Die() { FinishBeat(true); Zombie::Die(); }
void CrystalDrummerZombie::OnMindControlled() { FinishBeat(true); }
void CrystalDrummerZombie::ZombieItemUpdate() const { Zombie::ZombieItemUpdate(); SyncEquipment(); }
bool CrystalDrummerZombie::CaptureTemporalAbilityState(ZombieTemporalAbilityState& state) const
{
	state.phase = static_cast<int>(mDisabled ? DrumSnapshotPhase::DISABLED
		: mWindingUp ? DrumSnapshotPhase::WINDUP : DrumSnapshotPhase::WAITING);
	state.remaining = mRemaining;
	return true;
}

void CrystalDrummerZombie::RestoreTemporalAbilityState(const ZombieTemporalAbilityState& state)
{
	const auto phase = static_cast<DrumSnapshotPhase>(std::clamp(state.phase,
		static_cast<int>(DrumSnapshotPhase::WAITING), static_cast<int>(DrumSnapshotPhase::DISABLED)));
	mDisabled = phase == DrumSnapshotPhase::DISABLED || !HasHead() || IsMindControlled()
		|| mIsDead || mIsDying;
	mWindingUp = !mDisabled && phase == DrumSnapshotPhase::WINDUP;
	mRemaining = mDisabled ? 0.0f : std::clamp(state.remaining, 0.0f,
		mWindingUp ? kWindup : kBeatInterval-kWindup);
	// 只恢复可撤销的本地进度；已敲次数、受益者增益和提交时音画均不倒放或补发。
	mPulseRemaining = 0.0f;
	if (IsActive() && !mIsDead && !mIsDying) {
		if (mWindingUp) {
			CancelEatingForSpecialAction();
			PlayTrack("anim_idle", 1.0f, 0.12f);
		} else if (!mIsEating) {
			PlayWalkAnimation(0.12f);
		}
	}
	SyncEquipment();
}

void CrystalDrummerZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	// 核心恢复先为没有能力快照的旧锚提供安全起点；新锚随后覆盖精确阶段。
	// 复活后的 OnTemporalRecreated 不再重置，避免覆盖刚恢复的前摇。
	FinishBeat(!HasHead() || IsMindControlled());
}

void CrystalDrummerZombie::SyncEquipment() const
{
	if (!mAnimator) return;
	auto& resources=ResourceManager::GetInstance();
	// 专用锚点逐帧复制躯干姿态，排在领带之后、敲鼓前臂之前，鼓面不会被领带穿过。
	mAnimator->SetTrackFollowerImage("crystal_drum_mount","crystal_drum",resources.GetTexture("IMAGE_CRYSTALDRUMMER_DRUM",false),-30,28,1.3f,1.3f,false,true,true);
	mAnimator->SetTrackFollowerVisible("crystal_drum_mount","crystal_drum",true);
	const float lift=mWindingUp ? -12.0f*std::clamp(1.0f-mRemaining/kWindup,0.0f,1.0f) : 0.0f;
	for (const char* arm : {"anim_innerarm1","anim_innerarm2","anim_innerarm3"}) mAnimator->SetTrackOffset(arm,0,lift);
	mAnimator->SetTrackFollowerImage("anim_innerarm2","crystal_mallet",resources.GetTexture("IMAGE_CRYSTALDRUMMER_MALLET",false),-25,-10,1,1,false);
	mAnimator->SetTrackFollowerVisible("anim_innerarm2","crystal_mallet",true);
	mAnimator->SetTrackGlowOverride("Zombie_body",mWindingUp || mPulseRemaining>0.0f || IsBodyHitFlashing());
	mAnimator->SetTrackGlowOverride("crystal_drum_mount",mWindingUp || mPulseRemaining>0.0f || IsBodyHitFlashing());
}

void CrystalDrummerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["drumWindingUp"]=mWindingUp;
	j["drumDisabled"]=mDisabled;
	j["drumRemaining"]=mRemaining;
	j["drumBeatCount"]=mBeatCount;
}

void CrystalDrummerZombie::LoadExtraData(const nlohmann::json& j)
{
	mDisabled=j.value("drumDisabled",false) || !HasHead() || IsMindControlled() || mIsDead || mIsDying;
	mWindingUp=!mDisabled && j.value("drumWindingUp",false);
	mRemaining=mDisabled ? 0.0f : std::clamp(j.value("drumRemaining",kBeatInterval-kWindup),0.0f,mWindingUp ? kWindup : kBeatInterval-kWindup);
	mBeatCount=std::max(0,j.value("drumBeatCount",0));
	mPulseRemaining=0.0f;
	if (mWindingUp) CancelEatingForSpecialAction();
	SyncEquipment();
}
