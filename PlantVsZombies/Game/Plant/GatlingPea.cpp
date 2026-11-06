#include "GatlingPea.h"

#include "Game/Board/Board.h"
#include "../../GameApp.h"
#include "Game/Workshop/Workshop.h"
#include "Game/Bullet/Bullet.h"
#include <algorithm>

namespace {
	static_assert(Workshop::HeavyGatlingVolley == 4, "工坊整轮预留必须与已批准的四个发弹事件一致");
	constexpr float kGatlingShootingClipSpeed = 1.9f; // 保证第 80 帧在约 1.5 秒攻击周期内完成
	constexpr float kBodyIdleBasePoseX = 37.6f;       // anim_idle 首帧锚点 X，用于消除附件重复位移
	constexpr float kBodyIdleBasePoseY = 48.7f;       // anim_idle 首帧锚点 Y，用于消除附件重复位移
}

void GatlingPea::SetupPlant()
{
	Plant::SetupPlant();
	const auto reanim = mAnimator ? mAnimator->GetReanimation() : nullptr;
	if (!reanim) return;

	// 根 Animator 只播 0..24 的身体；独立头 Animator 播 25..89 的待机/射击分件。
	// 这份时间线没有 anim_stem，因此挂在 anim_idle；附件接口只乘 current，
	// 需要预先减去首帧基准位姿，避免头部再次叠加身体锚点而整体偏移。
	mAnimator->PlayTrack("anim_idle");
	mAnimator->SetSpeed(GameRandom::Range(1.1f, 1.3f));
	mHeadAnim = std::make_shared<Animator>(reanim);
	mHeadAnim->SetSpeed(mAnimator->GetSpeed());
	mHeadAnim->PlayTrack("anim_head_idle");
	mHeadAnim->SetLocalPosition(-kBodyIdleBasePoseX, -kBodyIdleBasePoseY);
	if (!mAnimator->AttachAnimator("anim_idle", mHeadAnim)) {
		mHeadAnim.reset();
		return;
	}
	if (mBoard && mBoard->IsWorkshop()) mHeadAnim->SetOverlayColor(255, 204, 92, 90);

	// 主人提供的帧号已是 AddFrameEvent 口径，不再额外减一。
	for (const int frame : {60, 68, 74, 80}) {
		mHeadAnim->AddFrameEvent(frame, [this]() {
			PlayShotSound();
			ShootBullet();
		}, true);
	}
}

void GatlingPea::PlantUpdate()
{
	const float multiplier = GetAttackSpeedMultiplier() * GetWorkshopAttackSpeedMultiplier();
	mShootTimer += DeltaTime::GetDeltaTime() * multiplier;
	if (mWorkshopReservedShots > 0 || mShootTimer < mShootTime || !HasZombieInRow()) return;
	if (mBoard && mBoard->IsWorkshop()) {
		if (!mBoard->ReserveWorkshopAmmo(mRow, mPlantType, Workshop::HeavyGatlingVolley)) return;
		mWorkshopReservedShots = Workshop::HeavyGatlingVolley;
	}

	mShootTimer = 0.0f;
	mHeadAnim->PlayTrackOnce(
		"anim_shooting", "anim_head_idle",
		kGatlingShootingClipSpeed * multiplier, 0.1f);
}

void GatlingPea::ShootBullet()
{
	if (!mBoard) return;
	if (mBoard->IsWorkshop()) {
		if (mWorkshopReservedShots <= 0) return;
		--mWorkshopReservedShots;
	}
	const Vector bulletPosition = GetPosition() + Vector(30.0f, -30.0f);
	auto* bullet = mBoard->CreatePlantBullet(
		BulletType::BULLET_PEA, mRow, bulletPosition, mPlantType);
	if (bullet && mBoard->IsWorkshop()) {
		bullet->EnableWorkshopHeavyPea();
		mBoard->RecordWorkshopPlantShot(mPlantType);
	}
}

void GatlingPea::InheritWorkshopLevel(int level) {
	if (mBoard && mBoard->IsWorkshop()) mWorkshopUpgradeLevel = std::clamp(level, 1, Workshop::MaxUpgradeLevel);
}
void GatlingPea::SaveExtraData(nlohmann::json& j) const {
	Shooter::SaveExtraData(j);
	if (mBoard && mBoard->IsWorkshop()) j["workshopReservedShots"] = mWorkshopReservedShots;
}
void GatlingPea::LoadExtraData(const nlohmann::json& j) {
	Shooter::LoadExtraData(j);
	mWorkshopReservedShots = 0;
	if (mBoard && mBoard->IsWorkshop()) {
		const auto value = j.value("workshopReservedShots", nlohmann::json(0));
		if (!value.is_number_integer() || value < 0 || value > Workshop::HeavyGatlingVolley) throw std::runtime_error("重装机枪预留弹数错误");
		mWorkshopReservedShots = value.get<int>();
	}
}

void GatlingPea::PlayShotSound() const
{
	AudioSystem::PlaySound(GameRandom::Chance()
		? ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT
		: ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, 0.3f);
}
