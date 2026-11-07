#include "ThreePeater.h"
#include "../../GameApp.h"

#include "Game/Board/Board.h"
#include "Game/Workshop/Workshop.h"
#include "../Bullet/Bullet.h"
#include "../Zombie/Zombie.h"

namespace {
	constexpr int kSynchronizedVolleyFireFrame = 73; // 上头真实发射帧；作为 C# 三弹同步结算的唯一帧事件
	static_assert(Workshop::HeavyThreepeaterVolley == 3, "三线整轮预留必须与既有三头发弹一致");
	constexpr float kReanimFramesPerSecond = 12.0f;  // ThreePeater.reanim 的基础帧率
	constexpr float kShootFramesPerSecond = 20.0f;   // C# 三个头进入射击态时使用的动画帧率
	constexpr float kIdleFramesPerSecondMin = 15.0f; // C# 三线射手待机随机帧率下限
	constexpr float kIdleFramesPerSecondMax = 20.0f; // C# 三线射手待机随机帧率上限
	constexpr float kShootBlendSeconds = 0.1f;       // C# StartBlend(10) 对应的过渡时长
	constexpr float kBulletOffsetX = 5.0f;           // C# mX+45 换算到本项目格子中心后的水平偏移
	constexpr float kBulletOffsetY = -40.0f;         // C# mY+10 换算到本项目格子中心后的垂直偏移
	constexpr float kBoundaryCompensationBulletSpeed = 360.0f; // 边路补偿弹水平速度，和本行普通弹的 290px/s 拉开
	constexpr float kHead1BasePoseX = 53.0f;         // 根 anim_head1 在 anim_idle 首帧的基准锚点 X
	constexpr float kHead1BasePoseY = 50.8f;         // 根 anim_head1 在 anim_idle 首帧的基准锚点 Y
	constexpr float kHead2BasePoseX = 35.4f;         // 根 anim_head2 在 anim_idle 首帧的基准锚点 X
	constexpr float kHead2BasePoseY = 35.0f;         // 根 anim_head2 在 anim_idle 首帧的基准锚点 Y
	constexpr float kHead3BasePoseX = 21.0f;         // 根 anim_head3 在 anim_idle 首帧的基准锚点 X
	constexpr float kHead3BasePoseY = 22.2f;         // 根 anim_head3 在 anim_idle 首帧的基准锚点 Y
}

void ThreePeater::SetupPlant()
{
	Plant::SetupPlant();

	auto reanim = mAnimator ? mAnimator->GetReanimation() : nullptr;
	if (!reanim) return;

	const float idleSpeed = GameRandom::Range(
		kIdleFramesPerSecondMin / kReanimFramesPerSecond,
		kIdleFramesPerSecondMax / kReanimFramesPerSecond);
	mAnimator->PlayTrack("anim_idle");
	mAnimator->SetSpeed(idleSpeed);

	// C# 将三个头分别附着在根动画的三个头部锚点，身体与每个头可独立循环。
	mHeadAnim = CreateHeadAnimator(
		"anim_head_idle1", "anim_head1",
		kHead1BasePoseX, kHead1BasePoseY);
	mHeadAnim2 = CreateHeadAnimator(
		"anim_head_idle2", "anim_head2",
		kHead2BasePoseX, kHead2BasePoseY);
	mHeadAnim3 = CreateHeadAnimator(
		"anim_head_idle3", "anim_head3",
		kHead3BasePoseX, kHead3BasePoseY);

	// C# 在同一个 mShootingCounter tick 创建三颗弹。上头的轨内第 7 帧与该
	// 约 0.35 秒结算点一致，因此只保留这一处帧事件，避免三个视觉帧错开出弹。
	if (mHeadAnim3) {
		mHeadAnim3->AddFrameEvent(kSynchronizedVolleyFireFrame, [this]() {
			ShootBullet();
		}, true);
	}
}

std::shared_ptr<Animator> ThreePeater::CreateHeadAnimator(
	const char* idleTrack, const char* attachTrack,
	float basePoseX, float basePoseY)
{
	auto reanim = mAnimator ? mAnimator->GetReanimation() : nullptr;
	if (!reanim) return nullptr;

	auto head = std::make_shared<Animator>(reanim);
	head->SetSpeed(mAnimator->GetSpeed());
	head->PlayTrack(idleTrack);
	// C# GetAttachmentOverlayMatrix 使用 current * inverse(basePose)；当前附件接口只乘
	// current，因此在局部坐标先减去 anim_idle 首帧锚点，精确复原三个头各自的基准姿态。
	head->SetLocalPosition(-basePoseX, -basePoseY);
	if (!mAnimator->AttachAnimator(attachTrack, head)) return nullptr;

	return head;
}

void ThreePeater::PlantUpdate()
{
	// 与其他射手一致，计时和射击动画同时乘生存攻速与雨势行动倍率。
	const float mult = GetAttackSpeedMultiplier() * GetWorkshopAttackSpeedMultiplier();
	mShootTimer += DeltaTime::GetDeltaTime() * mult;
	if (mWorkshopVolleyReserved || mShootTimer < mShootTime) return;
	if (mBoard && mBoard->IsWorkshop()) {
		if (!HasTargetInThreeRows() || !mBoard->ReserveWorkshopAmmo(mRow, mPlantType, Workshop::HeavyThreepeaterVolley)) return;
		mWorkshopVolleyReserved = true;
		mShootTimer = 0.0f; StartVolley(mult); return;
	}

	// C# 每次 LaunchCounter 到期都会重置；没有目标时也等到下一轮再检查。
	mShootTimer = 0.0f;
	if (!HasTargetInThreeRows()) return;
	StartVolley(mult);
}

bool ThreePeater::HasTargetInRow(int row) const
{
	if (!mBoard || !IsValidRow(row)) return false;

	const float plantX = GetPosition().x;
	bool found = false;
	mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
		if (found || !zombie) return;
		const float zombieX = zombie->GetPosition().x;
		if (!zombie->IsMindControlled()
			&& zombieX >= plantX && zombieX <= SCENE_WIDTH && zombie->HasHead()
			&& mBoard->CanPlantAcquireZombie(this, zombie)) {
			found = true;
		}
	});
	return found;
}

bool ThreePeater::HasTargetInThreeRows() const
{
	return HasTargetInRow(mRow)
		|| HasTargetInRow(mRow - 1)
		|| HasTargetInRow(mRow + 1);
}

bool ThreePeater::IsValidRow(int row) const
{
	return mBoard && row >= 0 && row < mBoard->mRows;
}

void ThreePeater::StartVolley(float attackSpeedMultiplier)
{
	const float shootSpeed =
		(kShootFramesPerSecond / kReanimFramesPerSecond) * attackSpeedMultiplier;

	// 三个头始终完整开火；边界外的弹道会在 FireRow 中补偿到本行。
	if (mHeadAnim) {
		mHeadAnim->PlayTrackOnce(
			"anim_shooting1", "anim_head_idle1", shootSpeed, kShootBlendSeconds);
	}
	if (mHeadAnim2) {
		mHeadAnim2->PlayTrackOnce(
			"anim_shooting2", "anim_head_idle2", shootSpeed, kShootBlendSeconds);
	}
	if (mHeadAnim3) {
		mHeadAnim3->PlayTrackOnce(
			"anim_shooting3", "anim_head_idle3", shootSpeed, kShootBlendSeconds);
	}
}

void ThreePeater::ShootBullet()
{
	if (mBoard && mBoard->IsWorkshop()) {
		if (!mWorkshopVolleyReserved) return;
		mWorkshopVolleyReserved = false;
	}
	// C# 的 Fire 会为三颗弹各播放一次 Throw；保持相同调用次数并在同一逻辑帧结算。
	for (int rowOffset = 1; rowOffset >= -1; --rowOffset) {
		if (GameRandom::Chance()) {
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT, 0.3f);
		}
		else {
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, 0.3f);
		}
		FireRow(mRow + rowOffset);
	}
}

void ThreePeater::FireRow(int targetRow)
{
	if (!mBoard) return;

	const int sourceRow = mRow;
	const bool isBoundaryCompensation = !IsValidRow(targetRow);
	// 顶、底边缺失的斜路不吞弹，改由本行补发，保证每轮始终有三颗豌豆。
	if (isBoundaryCompensation) {
		targetRow = sourceRow;
	}
	const Vector bulletPosition = GetPosition() + Vector(kBulletOffsetX, kBulletOffsetY);
	Bullet* bullet = mBoard->CreatePlantBullet(
		BulletType::BULLET_PEA, targetRow, bulletPosition, mPlantType);
	if (bullet && mBoard->IsWorkshop()) {
		bullet->SetBulletDamage(bullet->GetBulletDamage() * Workshop::HeavyThreepeaterDamageMultiplier);
		mBoard->RecordWorkshopPlantShot(mPlantType);
	}
	if (bullet && isBoundaryCompensation) {
		bullet->SetVelocityX(kBoundaryCompensationBulletSpeed);
	}
	if (bullet && targetRow != sourceRow) {
		bullet->EnableThreepeaterMotion(sourceRow);
	}
}

void ThreePeater::SaveExtraData(nlohmann::json& j) const
{
	Shooter::SaveExtraData(j);
	if (mBoard && mBoard->IsWorkshop()) j["workshopVolleyReserved"] = mWorkshopVolleyReserved;
	SaveHeadAnimatorState(j, "head2Anim", mHeadAnim2.get());
	SaveHeadAnimatorState(j, "head3Anim", mHeadAnim3.get());
}

void ThreePeater::LoadExtraData(const nlohmann::json& j)
{
	Shooter::LoadExtraData(j);
	mWorkshopVolleyReserved = false;
	if (mBoard && mBoard->IsWorkshop()) {
		const auto reserved = j.value("workshopVolleyReserved", nlohmann::json(false));
		if (!reserved.is_boolean()) throw std::runtime_error("重装三线预留状态错误");
		mWorkshopVolleyReserved = reserved.get<bool>();
	}
	LoadHeadAnimatorState(j, "head2Anim", mHeadAnim2.get());
	LoadHeadAnimatorState(j, "head3Anim", mHeadAnim3.get());
}

bool ThreePeater::TryUpgradeWorkshop()
{
	const float oldSpeed = GetWorkshopAttackSpeedMultiplier();
	if (!Shooter::TryUpgradeWorkshop()) return false;
	const float ratio = GetWorkshopAttackSpeedMultiplier() / oldSpeed;
	for (const auto& head : {mHeadAnim, mHeadAnim2, mHeadAnim3})
		if (head && head->GetCurrentTrackName().starts_with("anim_shooting"))
			head->SetClipSpeed(head->GetClipSpeed() * ratio);
	return true;
}
