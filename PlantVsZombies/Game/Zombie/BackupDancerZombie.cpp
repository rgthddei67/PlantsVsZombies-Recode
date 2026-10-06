#include "BackupDancerZombie.h"
#include "DancerRules.h"
#include "../../GameApp.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../../ParticleSystem/ParticleSystem.h"

namespace {
	constexpr float kRiseDuration = DancerRules::HoldSeconds;    // 出土升起耗时（原版 150cs）与DancerZombie kHoldDuration相同
	constexpr float kRiseDepth = 145.0f;     // 出生下沉深度（原版 altitude -145）
	constexpr float kDanceAnimSpeed = DancerRules::DanceSpeed;  // 全队统一动画速度：覆盖 Start() 的随机 1.1~1.4，否则齐舞散拍
	constexpr float kArmraiseClip = 1.8f;    // 举手段 clip（原版 rate18；按截图手感可微调）
	constexpr int   kGroundClipMargin = 38;   // 地面线裁剪底边 = 逻辑位置 y + 此余量（截图目验微调）
	constexpr float kDancerFlipPivotX = 45.0f; // C# UpdateReanim 翻面时补 90px，等价于绕局部 x=45 翻转
}

void BackupDancerZombie::SetupZombie()
{
	mBodyHealth = DancerRules::BackupBodyHealth;
	mBodyMaxHealth = DancerRules::BackupBodyHealth;
	SetAnimationSpeed(kDanceAnimSpeed);

	if (mIsPreview) { PlayTrack("anim_walk"); return; }

	// 帧号为主人指定：Die@99（anim_death 65~101 内；100 实测播不到——死亡动画停在末帧前，
	// 事件永不触发，僵尸血 0 却不消失，靠 10s 看门狗兜底），EatTarget@46/59（anim_eat 32~64 内）
	mAnimator->AddFrameEvent(99, [this]() { this->Die(); });
	mAnimator->AddFrameEvent(46, [this]() { this->EatTarget(); }, true);
	mAnimator->AddFrameEvent(59, [this]() { this->EatTarget(); }, true);

	// 出生沉入地下；主人要求升起期间完全静止，出土完成后才加入全局舞拍。
	mBaseOffsetY = mVisualOffset.y;
	mVisualOffset.y = mBaseOffsetY + kRiseDepth;
	mPhase = BackupPhase::RISING;
	// 地面线以下不可见：复用逐对象裁剪（图鉴僵尸窗同款），升起完成后 ClearClipRect 恢复。
	// 底边用当前 X 上的“行地面线”而非自身坐标——屋顶出土时也沿斜坡裁剪。
	const float groundY = mBoard
		? mBoard->GetZombieSpawnY(mRow, GetPosition().x)
		: GetPosition().y;
	SetClipRect(0, 0, SCENE_WIDTH,
		static_cast<int>(groundY) + kGroundClipMargin);
	// 人还在土里，地面不该有影子；Zombie::Start 已先创建显式 Shadow 附件，出土完成后再恢复。
	if (auto shadow = GetShadow()) shadow->SetVisible(false);
	// 只暂停 Animator 播放头，不把基础/extra 速度写成 0：若升起时死亡，PlayTrack(anim_death)
	// 会自动把 Animator 重新置为 playing，死亡帧事件仍可正常越过。
	PlayTrack("anim_armraise", kArmraiseClip);
	mAnimator->Pause();
	UpdateDanceFacing();
}

void BackupDancerZombie::ZombieUpdate(float scaledTime)
{
	// 魅惑边沿：脱离领队（领队随后检测到空位会补人）。基类 StartMindControlled 非虚，故在此检测。
	if (mIsMindControlled && !mCharmHandled) {
		mCharmHandled = true;
		mLeaderID = NULL_ZOMBIE_ID;
	}

	if (mPhase == BackupPhase::RISING) {
		mRiseTimer += scaledTime;
		const float t = mRiseTimer / kRiseDuration;
		if (t >= 1.0f) {
			mVisualOffset.y = mBaseOffsetY;
			mPhase = BackupPhase::DANCING;
			ClearClipRect();	// 完全出土，解除地面线裁剪
			if (auto shadow = GetShadow()) shadow->SetVisible(true);
		}
		else {
			mVisualOffset.y = mBaseOffsetY + kRiseDepth * (1.0f - t);
			UpdateDanceFacing();
			return;
		}
	}

	UpdateDanceTrack(0.3f);
	UpdateDanceFacing();
}

void BackupDancerZombie::UpdateDanceTrack(float blendTime)
{
	if (!mBoard || mIsDying || mIsEating) return;
	const int bucket = (mBoard->GetDanceBeatFrame() >= 12) ? 1 : 0;
	if (bucket == mLastBeatBucket) return;
	mLastBeatBucket = bucket;
	if (bucket == 1)
		PlayTrack("anim_armraise", kArmraiseClip, blendTime);
	else
		PlayTrack("anim_walk", 0.0f, blendTime);
}

void BackupDancerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mPhase == BackupPhase::RISING) return;	// 升起中不推进
	Zombie::ZombieMove(scaledDelta, transform);
}

void BackupDancerZombie::PlayWalkAnimation(float blendTime)
{
	// 啃完回走路/读档恢复的统一入口：回到当前节拍对应的舞蹈轨道
	mLastBeatBucket = -1;
	if (mBoard && mBoard->GetDanceBeatFrame() >= 12)
		PlayTrack("anim_armraise", kArmraiseClip, blendTime);
	else
		PlayTrack("anim_walk", 0.0f, blendTime);
}

void BackupDancerZombie::OnStartEating()
{
	UpdateDanceFacing();
}

void BackupDancerZombie::OnStopEating()
{
	UpdateDanceFacing();
}

void BackupDancerZombie::UpdateDanceFacing()
{
	if (!mAnimator || mIsPreview) return;

	// C# GetDancerPhase：13~15、19~21 是两段朝右举手；其余拍朝左。
	// 升起阶段按主人要求保持静态，不跟随节拍翻面。
	bool flip = false;
	if (!mIsEating && mPhase == BackupPhase::DANCING && mBoard) {
		const int beat = mBoard->GetDanceBeatFrame();
		flip = (beat >= 13 && beat <= 15) || (beat >= 19 && beat <= 21);
	}
	if (mIsMindControlled) flip = !flip;
	mAnimator->SetFlipX(flip, kDancerFlipPivotX);
}

void BackupDancerZombie::StartEat(ColliderComponent* other)
{
	if (mPhase == BackupPhase::RISING) return;	// 升起中不啃食
	Zombie::StartEat(other);
}

void BackupDancerZombie::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	mAnimator->SetTrackVisible("anim_earing", false);	// 耳环挂在头上，随头走
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieBackupDancerHeadOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void BackupDancerZombie::ArmDrop()
{
	if (!mHasArm) return;
	// MJ 版 reanim 无残肢轨道：只藏小臂+手，大臂原样保留当残端；无材质替换、无粒子（主人指定）
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void BackupDancerZombie::ZombieItemUpdate() const
{
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_hair", false);
		mAnimator->SetTrackVisible("anim_earing", false);
	}
}

void BackupDancerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["riseTimer"] = mRiseTimer;
	j["leaderID"] = mLeaderID;
	j["charmHandled"] = mCharmHandled;
}

void BackupDancerZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<BackupPhase>(j.value("phase", 0));
	mRiseTimer = j.value("riseTimer", 0.0f);
	mLeaderID = j.value("leaderID", NULL_ZOMBIE_ID);
	mCharmHandled = j.value("charmHandled", false);
	// SetupZombie（读档新建僵尸同样走过）默认按 RISING 预设了下沉+地面线裁剪+影子隐藏；
	// 若存档已是 DANCING，需在此撤销。RISING 存档则无需处理：offset 由 ZombieUpdate 按 mRiseTimer 重算。
	if (mPhase == BackupPhase::DANCING) {
		mVisualOffset.y = mBaseOffsetY;
		ClearClipRect();
		if (auto shadow = GetShadow()) shadow->SetVisible(true);
	}
	else {
		// RestoreAnimState 会恢复播放状态；RISING 存档必须重新暂停，维持出土静态姿势。
		mAnimator->Pause();
	}
	UpdateDanceFacing();
	if (mIsEating) return;
	mLastBeatBucket = -1;	// 下帧按当前节拍重刷轨道（覆盖 RestoreAnimState 的帧位=重新入拍，预期行为）
}

ZombieMovementRules::BirthProfile BackupDancerZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.canBeChilled=false; // 仅升起出生阶段，落地后在能力时间线上恢复
	p.linear=true;
	p.velocityMinimum=p.velocityMaximum=0;
	p.phaseDependent=true;
	return p;
}

float BackupDancerZombie::GetMineSimulationMoveSpeed() const
{
	return mPhase==BackupPhase::RISING ? 0 : Zombie::GetMineSimulationMoveSpeed();
}

ZombieMovementRules::BirthProfile BackupDancerZombie::GetDancingMovementProfile()
{
	auto profile=Zombie::GetBirthMovementProfile();
	profile.alternative=nullptr;
	profile.animationMinimum=profile.animationMaximum=DancerRules::DanceSpeed;
	return profile;
}

DancerRules::Forecast BackupDancerZombie::GetDanceForecast() const
{
	DancerRules::Forecast result;
	result.backup=true;
	result.phase=mPhase==BackupPhase::RISING ? DancerRules::Forecast::Phase::HOLD : DancerRules::Forecast::Phase::DANCE;
	result.remaining=std::max(0.0f,DancerRules::HoldSeconds-mRiseTimer);
	result.walkSpeed=GetSimulationRootMoveSpeed("anim_walk",DancerRules::DanceSpeed);
	return result;
}
