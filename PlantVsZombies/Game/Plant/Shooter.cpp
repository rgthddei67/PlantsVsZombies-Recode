#include "Shooter.h"
#include "../../GameApp.h"
#include "GameDataManager.h"
#include "Game/Board/Board.h"
#include "Game/Workshop/Workshop.h"
#include "../Zombie/Zombie.h"
#include <stdexcept>

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
	if (mBoard && mBoard->IsWorkshop()) j["workshopUpgradeLevel"] = mWorkshopUpgradeLevel;
	SaveHeadAnimatorState(j, "headAnim", mHeadAnim.get());
}

void Shooter::LoadExtraData(const nlohmann::json& j)
{
	mShootTimer = j.value("shootTimer", 1.0f);
	mWorkshopUpgradeLevel = 1;
	if (mBoard && mBoard->IsWorkshop() && j.contains("workshopUpgradeLevel")) {
		const auto& level = j.at("workshopUpgradeLevel");
		if (!level.is_number_integer() || level < 1 || level > Workshop::MaxUpgradeLevel)
			throw std::runtime_error("无效的工坊射手等级");
		mWorkshopUpgradeLevel = level.get<int>();
		if (mBoard->GetWorkshop().CompletedWaves() < Workshop::UpgradeWaves[mWorkshopUpgradeLevel - 1])
			throw std::runtime_error("工坊射手等级尚未解锁");
	}
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

float Shooter::GetWorkshopAttackSpeedMultiplier() const
{
	return mBoard && mBoard->IsWorkshop()
		? Workshop::ShooterCooldowns[0] / Workshop::ShooterCooldowns[mWorkshopUpgradeLevel - 1] : 1.0f;
}

bool Shooter::TryUpgradeWorkshop()
{
	if (!mBoard || !mBoard->IsWorkshop() || mPlantType != PlantType::PLANT_PEASHOOTER
		|| !IsActive() || mPlantHealth <= 0
		|| !mBoard->GetWorkshop().BuyUpgrade(Workshop::ShooterCost, mWorkshopUpgradeLevel)) return false;
	const float oldSpeed = GetWorkshopAttackSpeedMultiplier();
	++mWorkshopUpgradeLevel;
	// 只改变正在播放的速度，保留当前帧与已消费事件；不因升级重新开始一次射击。
	if (mHeadAnim && mHeadAnim->GetCurrentTrackName() == "anim_shooting")
		mHeadAnim->SetClipSpeed(mHeadAnim->GetClipSpeed() * GetWorkshopAttackSpeedMultiplier() / oldSpeed);
	return true;
}

void Shooter::PlantUpdate()
{
	// 攻击统一合成天气、土壤和既有词条倍率，射击计时与发弹动画使用同一速度。
	float mult = GetAttackSpeedMultiplier() * GetWorkshopAttackSpeedMultiplier();
	this->mShootTimer += (DeltaTime::GetDeltaTime() * mult);
	if (this->mShootTimer >= this->mShootTime)
	{
		// 工坊在射击前摇提交时预留一颗弹药；原有头动画与发弹事件原样续接存档。
		if (HasZombieInRow() && mBoard->ReserveWorkshopAmmo(mRow))
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
		// 肥沃同步缩短攻击前索敌间隔，避免固定节流抵消高频攻击。
		mCheckZombieTimer += DeltaTime::GetDeltaTime() * GetIndependentActionSpeedMultiplier() * GetWorkshopAttackSpeedMultiplier();
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
