#include "WeatherJammerZombie.h"

#include "Game/Board/Board.h"
#include "../../DeltaTime.h"
#include "../../Reanimation/Animator.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>

namespace {
	constexpr float kChannelDuration = 4.0f;              // 停步至全栏目原子提交的游戏秒数
	constexpr float kRebootDuration = 5.0f;               // 外部打断后允许边走边啃的设备重启时长，单位游戏秒
	constexpr float kPanelInterferenceDuration = 30.0f;   // 每次成功提交向整个气象栏目追加的黑障游戏秒数
	constexpr float kPackAttachOffsetX = 27.0f;            // 背包相对身体稳定锚点的后侧偏移，单位局部 px
	constexpr float kPackAttachOffsetY = 3.0f;             // 背包相对身体稳定锚点的垂直偏移，单位局部 px
	constexpr float kTerminalAttachOffsetX = -22.0f;       // 终端相对外前臂稳定锚点、与低垂手掌形成重叠的局部 X，单位 px
	constexpr float kTerminalAttachOffsetY = 27.0f;        // 终端相对外前臂稳定锚点、与低垂手掌形成重叠的局部 Y，单位 px
	constexpr const char* kPackAttachTrack = "Zombie_body"; // 身体之后、外臂之前绘制，形成背负层级
	constexpr const char* kTerminalAttachTrack = "Zombie_outerarm_lower"; // 用稳定前臂位置限制上下摆幅，并让终端覆盖握持连接处
}

void WeatherJammerZombie::SetupZombie()
{
	// 铁桶时间线提供已验证的死亡、断肢、啃食和铁桶掉落事件；本品种不注册新帧号。
	BucketZombie::SetupZombie();
	mJammerPhase = JammerPhase::READY;
	mChannelRemaining = 0.0f;
	mRebootRemaining = 0.0f;
	mCommittedDisruptionMask = 0;
	ConfigureDeviceAnimators();
	SyncDevicePresentation(true);
	if (mIsPreview) PlayTrack("anim_idle");
}

void WeatherJammerZombie::Update()
{
	if (mIsPreview) {
		// 图鉴/选卡预览没有 Board；不能把该展示环境误判为设备失效，
		// 否则 SpendDevice 会恢复实战走路轨道，覆盖 SetupZombie 选定的 anim_idle。
		BucketZombie::Update();
		return;
	}

	if (mJammerPhase == JammerPhase::REBOOTING) {
		mRebootRemaining = std::max(
			0.0f, mRebootRemaining - DeltaTime::GetDeltaTime());
		if (mRebootRemaining <= 0.0f && !HasTerminalAbort()) {
			mJammerPhase = JammerPhase::READY;
		}
	}
	if (mJammerPhase == JammerPhase::READY && CanBeginChannel()) {
		BeginChannel();
	}

	BucketZombie::Update();
	// 基类在硬控时不会调用 ZombieUpdate；死亡、断头、掉臂和魅惑仍须当帧永久收口。
	if (mJammerPhase != JammerPhase::SPENT && HasTerminalAbort()) SpendDevice();
	ConfigureDeviceAnimators();
	SyncDevicePresentation();
}

void WeatherJammerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform || mJammerPhase == JammerPhase::CHANNELING) return;
	BucketZombie::ZombieMove(scaledDelta, transform);
}

void WeatherJammerZombie::ZombieUpdate(float scaledTime)
{
	if (mJammerPhase != JammerPhase::CHANNELING) return;
	if (HasTerminalAbort()) {
		SpendDevice();
		return;
	}
	if (!mBoard || !mBoard->CanBeginWeatherPanelInterference()) {
		// 并行提交把累计余时填满上限时不浪费设备；有余量后重新尝试。
		CancelChannelForRetry(false);
		return;
	}
	mChannelRemaining = std::max(0.0f,
		mChannelRemaining - std::max(0.0f, scaledTime));
	if (mChannelRemaining <= 0.0f) CommitInterference();
}

void WeatherJammerZombie::StartEat(ColliderComponent* other)
{
	if (mJammerPhase == JammerPhase::CHANNELING) return;
	BucketZombie::StartEat(other);
}

bool WeatherJammerZombie::CanBeginChannel() const
{
	if (!mBoard || !mCollider || mBoard->mColumns <= 0) return false;
	const SDL_FRect bounds = mCollider->GetBoundingBox();
	const float battlefieldRightX = CELL_INITALIZE_POS_X
		+ static_cast<float>(mBoard->mColumns) * CELL_COLLIDER_SIZE_X;
	return bounds.x + bounds.w <= battlefieldRightX
		&& !mIsPreview && IsActive() && !mIsDying
		&& !IsMindControlled() && HasHead() && HasArm()
		&& mJammerPhase == JammerPhase::READY
		&& mBoard->CanBeginWeatherPanelInterference();
}

bool WeatherJammerZombie::HasTerminalAbort() const
{
	return !mBoard || !IsActive() || mIsDying || IsMindControlled()
		|| !HasHead() || !HasArm();
}

void WeatherJammerZombie::BeginChannel()
{
	if (!CanBeginChannel()) return;
	// 施法抢占已有啃食：先平衡目标 eaterCount，再进入原地 idle，碰撞回调也被 StartEat 门禁。
	CancelEatingForSpecialAction();
	mJammerPhase = JammerPhase::CHANNELING;
	mChannelRemaining = kChannelDuration;
	mRebootRemaining = 0.0f;
	PlayTrack("anim_idle", 1.0f, 0.1f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.32f);
	SyncDevicePresentation(true);
}

void WeatherJammerZombie::CancelChannelForRetry(bool reboot)
{
	if (mJammerPhase != JammerPhase::CHANNELING) return;
	mJammerPhase = reboot ? JammerPhase::REBOOTING : JammerPhase::READY;
	mChannelRemaining = 0.0f;
	mRebootRemaining = reboot ? kRebootDuration : 0.0f;
	if (!mIsDying && IsActive()) PlayWalkAnimation(0.1f);
	if (reboot) AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.22f);
	SyncDevicePresentation(true);
}

void WeatherJammerZombie::SpendDevice()
{
	if (mJammerPhase == JammerPhase::SPENT) return;
	mJammerPhase = JammerPhase::SPENT;
	mChannelRemaining = 0.0f;
	mRebootRemaining = 0.0f;
	if (!mIsDying && IsActive() && mIsEating == false) PlayWalkAnimation(0.1f);
	SyncDevicePresentation(true);
}

void WeatherJammerZombie::CommitInterference()
{
	if (mJammerPhase != JammerPhase::CHANNELING || HasTerminalAbort()) {
		SpendDevice();
		return;
	}
	const int disrupted = mBoard->BeginWeatherPanelInterference(
		kPanelInterferenceDuration);
	if (disrupted == 0) {
		CancelChannelForRetry(false);
		return;
	}
	mCommittedDisruptionMask = disrupted;
	mJammerPhase = JammerPhase::SPENT;
	mChannelRemaining = 0.0f;
	mRebootRemaining = 0.0f;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.48f);
	PlayWalkAnimation(0.1f);
	SyncDevicePresentation(true);
}

float WeatherJammerZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mJammerPhase == JammerPhase::CHANNELING ? mChannelRemaining : -1.0f;
}

bool WeatherJammerZombie::InterruptUncommittedSpecialAction()
{
	if (mJammerPhase != JammerPhase::CHANNELING) return false;
	CancelChannelForRetry(true);
	return true;
}

void WeatherJammerZombie::Die()
{
	SpendDevice();
	BucketZombie::Die();
	SyncDevicePresentation();
}

void WeatherJammerZombie::OnMindControlled()
{
	SpendDevice();
}

void WeatherJammerZombie::ArmDrop()
{
	if (!mHasArm) return;
	BucketZombie::ArmDrop();
	SpendDevice();
	SyncDevicePresentation(true);
}

void WeatherJammerZombie::HeadDrop()
{
	if (!mHasHead) return;
	BucketZombie::HeadDrop();
	SpendDevice();
	SyncDevicePresentation(true);
}

void WeatherJammerZombie::SaveExtraData(nlohmann::json& j) const
{
	BucketZombie::SaveExtraData(j);
	j["jammerPhase"] = static_cast<int>(mJammerPhase);
	j["jammerChannelRemaining"] = mChannelRemaining;
	j["jammerRebootRemaining"] = mRebootRemaining;
	j["jammerCommittedDisruptionMask"] = mCommittedDisruptionMask;
}

void WeatherJammerZombie::LoadExtraData(const nlohmann::json& j)
{
	BucketZombie::LoadExtraData(j);
	const int phase = std::clamp(j.value("jammerPhase", 0), 0,
		static_cast<int>(JammerPhase::SPENT));
	mJammerPhase = static_cast<JammerPhase>(phase);
	mChannelRemaining = std::clamp(
		j.value("jammerChannelRemaining", 0.0f), 0.0f, kChannelDuration);
	mRebootRemaining = std::clamp(
		j.value("jammerRebootRemaining", 0.0f), 0.0f, kRebootDuration);
	mCommittedDisruptionMask = std::clamp(
		j.value("jammerCommittedDisruptionMask", 0), 0, 15);
	if (mJammerPhase != JammerPhase::SPENT
		&& (!HasHead() || !HasArm() || IsMindControlled() || mIsDying
			|| mCommittedDisruptionMask != 0
			|| (mJammerPhase == JammerPhase::CHANNELING
				&& mChannelRemaining <= 0.0f))) {
		// 异常或旧版快照不能回到 READY，否则终止态会凭空恢复一次干扰能力。
		mJammerPhase = JammerPhase::SPENT;
		mChannelRemaining = 0.0f;
	}
	if (mJammerPhase == JammerPhase::REBOOTING && mRebootRemaining <= 0.0f) {
		mJammerPhase = JammerPhase::READY;
	}
	if (mJammerPhase != JammerPhase::CHANNELING) mChannelRemaining = 0.0f;
	if (mJammerPhase != JammerPhase::REBOOTING) mRebootRemaining = 0.0f;
	mPresentedPhase = JammerPhase::SPENT;
	SyncDevicePresentation(true);
	if (mJammerPhase == JammerPhase::CHANNELING && mAnimator) {
		PlayTrack("anim_idle", 1.0f, 0.0f);
	}
}

void WeatherJammerZombie::ZombieItemUpdate() const
{
	BucketZombie::ZombieItemUpdate();
	SyncDevicePresentation();
}

void WeatherJammerZombie::ConfigureDeviceAnimators()
{
	if (!mAnimator) return;
	if (!mPackAnimator && mAnimator->HasTrack(kPackAttachTrack)) {
		auto reanimation = ResourceManager::GetInstance().GetReanimation(
			ResourceKeys::Reanimations::REANIM_WEATHER_JAMMER_PACK);
		if (reanimation) {
			auto pack = std::make_shared<Animator>(reanimation);
			pack->PlayTrack("anim_ready", 0.55f, 0.0f);
			pack->SetLocalPosition(kPackAttachOffsetX, kPackAttachOffsetY);
			if (mAnimator->AttachAnimator(kPackAttachTrack, pack)) {
				mPackAnimator = std::move(pack);
			}
		}
	}
	if (!mTerminalAnimator && HasArm() && mAnimator->HasTrack(kTerminalAttachTrack)) {
		auto reanimation = ResourceManager::GetInstance().GetReanimation(
			ResourceKeys::Reanimations::REANIM_WEATHER_JAMMER_TERMINAL);
		if (reanimation) {
			auto terminal = std::make_shared<Animator>(reanimation);
			terminal->PlayTrack("anim_ready", 0.55f, 0.0f);
			terminal->SetLocalPosition(kTerminalAttachOffsetX, kTerminalAttachOffsetY);
			if (mAnimator->AttachAnimator(kTerminalAttachTrack, terminal)) {
				mTerminalAnimator = std::move(terminal);
			}
		}
	}
}

const char* WeatherJammerZombie::GetDeviceTrackName() const
{
	switch (mJammerPhase) {
	case JammerPhase::CHANNELING: return "anim_channel";
	case JammerPhase::REBOOTING:  return "anim_reboot";
	case JammerPhase::SPENT:      return "anim_spent";
	case JammerPhase::READY:      return "anim_ready";
	}
	return "anim_spent";
}

void WeatherJammerZombie::SyncDevicePresentation(bool restartTracks) const
{
	if (mTerminalAnimator && !HasArm()) {
		if (mAnimator) {
			mAnimator->DetachAnimator(kTerminalAttachTrack, mTerminalAnimator);
		}
		mTerminalAnimator.reset();
	}
	if (!restartTracks && mPresentedPhase == mJammerPhase) return;
	const char* track = GetDeviceTrackName();
	const float speed = mJammerPhase == JammerPhase::CHANNELING ? 1.0f
		: (mJammerPhase == JammerPhase::REBOOTING ? 0.85f : 0.55f);
	if (mPackAnimator) mPackAnimator->PlayTrack(track, speed, 0.0f);
	if (mTerminalAnimator) mTerminalAnimator->PlayTrack(track, speed, 0.0f);
	mPresentedPhase = mJammerPhase;
}

ZombieMovementRules::BirthProfile WeatherJammerZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.phaseDependent=true;
	return p;
}
