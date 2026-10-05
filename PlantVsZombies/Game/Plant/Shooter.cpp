#include "Shooter.h"
#include "../../GameApp.h"
#include "GameDataManager.h"
#include "Game/Board/Board.h"
#include "../Zombie/Zombie.h"

namespace {
	std::string HeadStateKey(const char* prefix, const char* suffix)
	{
		return std::string(prefix) + suffix;
	}
}

void Shooter::SetupPlant() {
	Plant::SetupPlant();

	auto reanim = mAnimator->GetReanimation();
	if (!reanim) return;

	mAnimator->PlayTrack("anim_idle");

	// 1. 创建头部动画器
	mHeadAnim = std::make_shared<Animator>(reanim);
	mHeadAnim->SetSpeed(this->GetAnimationSpeed());   // 同步身体动画速度
	mHeadAnim->PlayTrack("anim_head_idle");
	mHeadAnim->SetLocalPosition(GameDataManager::GetInstance().
		GetPlantOffset(this->mPlantType));

	// 2. 将头部附加到身体轨道
	if (!mAnimator->GetTracksByName("anim_stem").empty()) {
		mAnimator->AttachAnimator("anim_stem", mHeadAnim);
	}

	mAnimator->SetSpeed(GameRandom::Range(1.1f, 1.3f));

	mHeadAnim->AddFrameEvent(64, [this]() {
		if (GameRandom::Chance())
		{
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT, 0.3f);
		}
		else {
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, 0.3f);
		}
		this->ShootBullet();
		}, true);
}

void Shooter::SaveExtraData(nlohmann::json& j) const
{
	j["shootTimer"] = mShootTimer;
	SaveHeadAnimatorState(j, "headAnim", mHeadAnim.get());
}

void Shooter::LoadExtraData(const nlohmann::json& j)
{
	mShootTimer = j.value("shootTimer", 1.0f);
	LoadHeadAnimatorState(j, "headAnim", mHeadAnim.get(),
		"anim_shooting", "anim_head_idle");
}

void Shooter::SaveHeadAnimatorState(
	nlohmann::json& j, const char* prefix, const Animator* animator)
{
	if (!animator) return;

	// 附加头不属于 AnimatedObject 的根 Animator；仅存轨道/帧会让一次性射击读档后循环。
	j[HeadStateKey(prefix, "Track")] = animator->GetCurrentTrackName();
	j[HeadStateKey(prefix, "Frame")] = animator->GetCurrentFrame();
	j[HeadStateKey(prefix, "Speed")] = animator->GetSpeed();
	j[HeadStateKey(prefix, "ClipSpeed")] = animator->GetClipSpeed();
	j[HeadStateKey(prefix, "PlayState")] =
		static_cast<int>(animator->GetPlayingState());
	j[HeadStateKey(prefix, "TargetTrack")] = animator->GetTargetTrack();
	j[HeadStateKey(prefix, "TargetTrackSpeed")] = animator->GetTargetTrackSpeed();
	j[HeadStateKey(prefix, "TargetTrackBlendTime")] =
		animator->GetTargetTrackBlendTime();
	j[HeadStateKey(prefix, "Playing")] = animator->IsPlaying();
}

void Shooter::LoadHeadAnimatorState(
	const nlohmann::json& j, const char* prefix, Animator* animator,
	const char* legacyShootingTrack, const char* legacyIdleTrack)
{
	if (!animator) return;

	const std::string trackKey = HeadStateKey(prefix, "Track");
	if (!j.contains(trackKey)) return;
	const std::string track = j.value(trackKey, std::string{});
	if (track.empty()) return;

	const std::string playStateKey = HeadStateKey(prefix, "PlayState");
	const float clipSpeed = j.value(HeadStateKey(prefix, "ClipSpeed"), 0.0f);
	if (j.contains(playStateKey)) {
		const int rawState = j.value(
			playStateKey, static_cast<int>(PlayState::PLAY_REPEAT));
		const PlayState state = (rawState >= static_cast<int>(PlayState::PLAY_NONE)
			&& rawState <= static_cast<int>(PlayState::PLAY_ONCE_TO))
			? static_cast<PlayState>(rawState) : PlayState::PLAY_REPEAT;

		if (state == PlayState::PLAY_ONCE || state == PlayState::PLAY_ONCE_TO) {
			animator->PlayTrackOnce(
				track,
				j.value(HeadStateKey(prefix, "TargetTrack"), std::string{}),
				clipSpeed,
				0.0f,
				j.value(HeadStateKey(prefix, "TargetTrackSpeed"), 0.0f),
				j.value(HeadStateKey(prefix, "TargetTrackBlendTime"), 0.5f));
		}
		else {
			animator->PlayTrack(track, clipSpeed);
		}
	}
	else if (legacyShootingTrack && legacyIdleTrack
		&& track == legacyShootingTrack) {
		// 旧 Shooter 存档缺少播放状态；射击轨只能续播一次，不能恢复为永久循环。
		animator->PlayTrackOnce(track, legacyIdleTrack, clipSpeed);
	}
	else {
		animator->PlayTrack(track, clipSpeed);
	}

	const std::string speedKey = HeadStateKey(prefix, "Speed");
	if (j.contains(speedKey)) {
		animator->SetSpeed(j.value(speedKey, 1.0f));
	}
	animator->SetCurrentFrame(j.value(HeadStateKey(prefix, "Frame"), 0.0f));
	if (j.contains(HeadStateKey(prefix, "Playing"))
		&& !j.value(HeadStateKey(prefix, "Playing"), true)) {
		animator->Pause();
	}
}

void Shooter::PlantUpdate()
{
	// 生存攻速词条 × 雨势行动倍率；二者都是单位元起步，普通晴天自动 no-op。
	float mult = GetAttackSpeedMultiplier();
	this->mShootTimer += (DeltaTime::GetDeltaTime() * mult);
	if (this->mShootTimer >= this->mShootTime)
	{
		if (HasZombieInRow())
		{
			mShootTimer = 0;
			// 动画同比例加快：吐弹的第 64 帧 frame event 跟上更短间隔
			mHeadAnim->PlayTrackOnce("anim_shooting", "anim_head_idle", 1.5f * mult, 0.2f);
		}
	}
}

bool Shooter::HasZombieInRow()
{
	if (mBoard)
	{
		mCheckZombieTimer += DeltaTime::GetDeltaTime();
		if (mCheckZombieTimer >= ShooterRules::TargetCheckSeconds)
		{
			mCheckZombieTimer = 0.0f;
			// 按行索引：只遍历本行僵尸，mRow 过滤已由桶保证。
			const float thisX = GetPosition().x;
			bool found = false;
			mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
				if (found) return;  // 已命中，跳过本行其余
				float zombieX = zombie->GetPosition().x;
				if (!zombie->IsMindControlled() && zombieX >= thisX
					&& zombieX <= SCENE_WIDTH && zombie->HasHead()
					&& mBoard->CanPlantAcquireZombie(this, zombie))
					found = true;
			});
			return found;
		}
	}
	return false;
}
