#include "BobsledTeamZombie.h"

#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../ShadowComponent.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kRiderHealth = 270;                    // 每名雪橇队员的原版本体生命
	constexpr int kSledHealth = 300;                     // 仅队长持有的原版雪橇耐久
	constexpr int kBiteDamage = 50;                      // 每名队员落地后的单次啃食伤害
	constexpr int kCollisionDamage = 1200;               // 撞上植物时由车队结算的僵尸来源伤害
	constexpr float kRideSpeed = 60.0f;                  // 原版 mVelX=0.6 换算后的世界速度，单位 px/s
	constexpr float kLandingDuration = 1.5f;             // 原版 BOBSLED_CRASH_TIME=150cs
	constexpr float kLandingArcHeight = 78.0f;           // 四人跳离雪橇时的最高视觉抬升，单位 px
	constexpr float kJumpClipSpeed = 0.53f;              // 24 帧 anim_jump 约铺满 1.5 秒
	constexpr float kJumpBlendTime = 0.1f;               // 切入 anim_jump 的混合时间，单位秒
	constexpr float kLandingWalkBlendTime = 0.15f;       // 落地完成后切回走路的混合时间，单位秒
	constexpr float kRiderSpacing = 50.0f;               // 车上四个逻辑原点的水平间距，单位 px
	constexpr float kContainedSpacing = 42.0f;           // 雪锚约束后同排落点间距，单位 px
	constexpr float kLandingFrontGap = 50.0f;            // 首名成员落在植物右缘外的距离，单位 px
	constexpr float kSameRowBypass = 100.0f;             // 普通散开时第二名向房屋侧前插距离，单位 px
	constexpr float kAdjacentRowBypass = 65.0f;          // 普通散开时相邻行成员向房屋侧前插距离，单位 px
	constexpr float kSledDrawOffsetX = -76.0f;           // 原版雪橇左上角相对队长视觉原点 X
	constexpr float kSledDrawOffsetY = 15.0f;            // 原版雪橇左上角相对队长视觉原点 Y
	constexpr float kSledColliderInsetX = 10.0f;         // 车辆碰撞框相对雪橇贴图左缘向内收缩量，单位 px
	constexpr float kSledColliderWidth = 275.0f;         // 乘车阶段碰撞框宽度，单位 px
	constexpr float kSledColliderHeight = 115.0f;        // 乘车阶段碰撞框高度，单位 px
	constexpr float kWalkingColliderWidth = 50.0f;       // 步行阶段碰撞框宽度，单位 px
	constexpr float kWalkingColliderHeight = 100.0f;     // 步行阶段碰撞框高度，单位 px
	constexpr float kWalkingColliderOffsetX = -25.0f;    // 步行碰撞框相对逻辑原点 X，单位 px
	constexpr float kWalkingColliderOffsetY = -65.0f;    // 步行碰撞框相对逻辑原点 Y，单位 px
	constexpr float kSledFadeDuration = 0.3f;             // 坠毁末段淡出时间，单位秒
	constexpr float kBobsledShadowOffsetY = 40.0f;         // 乘车/落地影子比普通僵尸再向下 12px
	constexpr float kWalkingShadowOffsetY = 38.0f;         // 步行影子也下移 10px，使其贴近原版雪橇队员脚底
	constexpr float kEatBlendTime = 0.1f;                  // 步行队员切入 anim_eat 的混合时间，单位秒
	constexpr float kEatClipSpeed = 2.1f;                 // anim_eat 的本类播放速度，保持既有啃食节奏
	constexpr float kDismemberSoundVolume = 0.25f;        // 断头/断臂掉落音效音量
	constexpr std::array<float, 4> kRideAltitude{ -10.0f, 9.0f, -7.0f, 9.0f }; // 原版四槽骑乘高度
	constexpr float kPi = 3.14159265358979323846f;        // 落地抛物线视觉弧使用的圆周率

	int ClampPhase(int value)
	{
		return std::clamp(value, static_cast<int>(BobsledTeamZombie::Phase::RIDING),
			static_cast<int>(BobsledTeamZombie::Phase::WALKING));
	}

	int ClampRole(int value)
	{
		return std::clamp(value, static_cast<int>(BobsledTeamZombie::Role::LEADER),
			static_cast<int>(BobsledTeamZombie::Role::FOLLOWER));
	}
}

void BobsledTeamZombie::SetupZombie()
{
	RegisterFrameEvents();
	mBodyMaxHealth = kRiderHealth;
	mBodyHealth = kRiderHealth;
	mAttackDamage = kBiteDamage;
	mHasHead = true;
	mHasArm = true;
	mHasTongue = false;
	mNeedDropHead = true;
	mNeedDropArm = true;
	mHelmType = HelmType::HELMTYPE_BOBSLED;
	mHelmHealth = kSledHealth;
	mHelmMaxHealth = kSledHealth;
	mRole = Role::LEADER;
	mPhase = Phase::RIDING;
	mSlot = 0;
	mLeaderID = NULL_ZOMBIE_ID;
	mMemberIDs.fill(NULL_ZOMBIE_ID);
	mTeamSpawned = false;
	mScatterContained = false;

	ConfigureColliderForPhase();
	PlayTrack("anim_push", 1.0f, 0.0f);
}

/** 注册主人给出的原版死亡终点与两次啃食命中帧。 */
void BobsledTeamZombie::RegisterFrameEvents()
{
	if (!mAnimator) return;
	mAnimator->AddFrameEvent(133, [this]() { Die(); });
	mAnimator->AddFrameEvent(151, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(169, [this]() { EatTarget(); }, true);
}

void BobsledTeamZombie::SpawnFollowers()
{
	if (mRole != Role::LEADER || mTeamSpawned || !mBoard || mIsPreview
		|| mZombieID == NULL_ZOMBIE_ID) {
		return;
	}

	mTeamSpawned = true;
	mLeaderID = mZombieID;
	mMemberIDs[0] = mZombieID;
	const Vector leaderPosition = GetPosition();
	for (int slot = 1; slot < static_cast<int>(mMemberIDs.size()); ++slot) {
		Zombie* created = mBoard->CreateZombie(mZombieType, mRow,
			leaderPosition.x + kRiderSpacing * static_cast<float>(slot));
		auto* follower = dynamic_cast<BobsledTeamZombie*>(created);
		if (!follower) continue;
		follower->ConfigureFollower(mZombieID, slot);
		mMemberIDs[slot] = follower->mZombieID;
	}

	// 所有成员共享同一份稳定 ID 视图；读档与整队死亡不需要扫描类型集合。
	for (int slot = 0; slot < static_cast<int>(mMemberIDs.size()); ++slot) {
		if (BobsledTeamZombie* member = ResolveMember(slot)) {
			member->mMemberIDs = mMemberIDs;
			member->mLeaderID = mZombieID;
			member->mTeamSpawned = true;
		}
	}
}

void BobsledTeamZombie::ConfigureFollower(int leaderID, int slot)
{
	mRole = Role::FOLLOWER;
	mSlot = std::clamp(slot, 1, 3);
	mLeaderID = leaderID;
	mMemberIDs.fill(NULL_ZOMBIE_ID);
	mMemberIDs[0] = leaderID;
	mMemberIDs[mSlot] = mZombieID;
	mTeamSpawned = true;
	mHelmType = HelmType::HELMTYPE_NONE;
	mHelmHealth = 0;
	mHelmMaxHealth = 0;
	ConfigureColliderForPhase();
	PlayTrack("anim_push", 1.0f, 0.0f);
}

void BobsledTeamZombie::ConfigurePreviewTeamMember(int slot)
{
	if (!mIsPreview) return;
	mSlot = std::clamp(slot, 0, 3);
	mRole = mSlot == 0 ? Role::LEADER : Role::FOLLOWER;
	mLeaderID = NULL_ZOMBIE_ID;
	mMemberIDs.fill(NULL_ZOMBIE_ID);
	mTeamSpawned = true;
	if (mRole == Role::FOLLOWER) {
		mHelmType = HelmType::HELMTYPE_NONE;
		mHelmHealth = 0;
		mHelmMaxHealth = 0;
	}
	ConfigureColliderForPhase();
	PlayTrack("anim_push", 1.0f, 0.0f);
}

float BobsledTeamZombie::GetPreviewMemberOffsetX(int slot)
{
	return kRiderSpacing * static_cast<float>(std::clamp(slot, 0, 3));
}

BobsledTeamZombie* BobsledTeamZombie::ResolveLeader() const
{
	if (!mBoard) return nullptr;
	const int leaderID = mRole == Role::LEADER ? mZombieID : mLeaderID;
	auto* leader = dynamic_cast<BobsledTeamZombie*>(
		mBoard->mEntityRegistry.GetZombie(leaderID));
	return leader && leader->mRole == Role::LEADER ? leader : nullptr;
}

BobsledTeamZombie* BobsledTeamZombie::ResolveMember(int slot) const
{
	if (!mBoard || slot < 0 || slot >= static_cast<int>(mMemberIDs.size())) return nullptr;
	auto* member = dynamic_cast<BobsledTeamZombie*>(
		mBoard->mEntityRegistry.GetZombie(mMemberIDs[slot]));
	return member && member->mSlot == slot ? member : nullptr;
}

int BobsledTeamZombie::GetLiveTeamMemberCount() const
{
	const BobsledTeamZombie* leader = mRole == Role::LEADER ? this : ResolveLeader();
	if (!leader) return IsActive() ? 1 : 0;
	int count = 0;
	for (int slot = 0; slot < static_cast<int>(leader->mMemberIDs.size()); ++slot) {
		const BobsledTeamZombie* member = leader->ResolveMember(slot);
		if (member && member->IsActive() && !member->mIsDead) ++count;
	}
	return count;
}

void BobsledTeamZombie::ZombieUpdate(float)
{
	if (mIsPreview || mIsDead || mPhase == Phase::WALKING) return;
	if (mRole == Role::LEADER) {
		SpawnFollowers();
		// 读档缺员或构造失败时不补生成员，整队按当前存活集合安全下车。
		if (mPhase == Phase::RIDING && mTeamSpawned
			&& GetLiveTeamMemberCount() != static_cast<int>(mMemberIDs.size())) {
			BeginTeamLanding(false, GetPosition().x);
		}
		if (mPhase == Phase::RIDING) CheckFrozenFrontier();
	}
	else if (mPhase == Phase::RIDING && !ResolveLeader()) {
		BeginOrphanLanding();
	}
}

void BobsledTeamZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform || scaledDelta <= 0.0f) return;

	if (mPhase == Phase::WALKING) {
		Zombie::ZombieMove(scaledDelta, transform);
		return;
	}
	if (mPhase == Phase::LANDING) {
		mLandingElapsed = std::min(kLandingDuration, mLandingElapsed + scaledDelta);
		const float t = std::clamp(mLandingElapsed / kLandingDuration, 0.0f, 1.0f);
		transform->SetPosition(mLandingStart + (mLandingTarget - mLandingStart) * t);
		if (t >= 1.0f) {
			transform->SetPosition(mLandingTarget);
			mPhase = Phase::WALKING;
			mLandingElapsed = kLandingDuration;
			ConfigureColliderForPhase();
			PlayWalkAnimation(kLandingWalkBlendTime);
		}
		return;
	}

	if (mRole == Role::FOLLOWER) {
		if (BobsledTeamZombie* leader = ResolveLeader();
			leader && leader->mPhase == Phase::RIDING) {
			Vector synced = leader->GetPosition();
			synced.x += kRiderSpacing * static_cast<float>(mSlot);
			transform->SetPosition(synced);
		}
		return;
	}

	float speed = kRideSpeed * GetAmplifiedAbilitySpeedMultiplier();
	if (mBoard) {
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieRainSpeedMultiplier());
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieWindMoveMultiplier(false));
	}
	speed *= GetRoofMarshalAssaultMoveMultiplier();
	transform->Translate(-speed * scaledDelta, 0.0f);
}

/** 乘车与落地仍是一支编队，统一按队长 X 回收，避免后排槽位在出生区误杀整队。 */
bool BobsledTeamZombie::IsOutsideWorldCleanupBounds(const Vector& position) const
{
	if (mPhase == Phase::WALKING) {
		return Zombie::IsOutsideWorldCleanupBounds(position);
	}

	const BobsledTeamZombie* leader = mRole == Role::LEADER ? this : ResolveLeader();
	if (!leader) {
		// 损坏引用先由品种状态机安全下车；步行后再恢复独立回收。
		return false;
	}
	return Zombie::IsOutsideWorldCleanupBounds(leader->GetPosition());
}

void BobsledTeamZombie::CheckFrozenFrontier()
{
	if (!mBoard || mPhase != Phase::RIDING || mRole != Role::LEADER) return;
	const int frozenColumns = mBoard->GetFrozenColumnCount();
	if (frozenColumns <= 0) {
		BeginTeamLanding(false, GetPosition().x);
		return;
	}

	const int firstFrozen = mBoard->GetFirstFrozenColumn();
	const float frontierX = mBoard->GetCellCenterPosition(mRow, firstFrozen).x
		- CELL_COLLIDER_SIZE_X * 0.5f;
	const float sledFrontX = mCollider
		? mCollider->GetBoundingBox().x
		: GetPosition().x;
	if (sledFrontX <= frontierX) {
		BeginTeamLanding(false, GetPosition().x);
	}
}

void BobsledTeamZombie::StartEat(ColliderComponent* other)
{
	if (mPhase == Phase::WALKING) {
		Zombie::StartEat(other);
		return;
	}
	if (mPhase != Phase::RIDING || mRole != Role::LEADER || !other || !mBoard) return;

	auto* plant = dynamic_cast<Plant*>(other->GetGameObject());
	if (!plant) return;
	if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn);
		top && IsPlantValidEatTarget(top)) {
		plant = top;
	}
	if (!IsPlantValidEatTarget(plant) || plant->mRow != mRow) return;

	ColliderComponent* plantCollider = plant->GetColliderComponent();
	if (!plantCollider || !plantCollider->mEnabled) return;
	const SDL_FRect plantBounds = plantCollider->GetBoundingBox();
	const Vector impactAnchor(
		plantBounds.x + plantBounds.w * 0.5f,
		plantBounds.y + plantBounds.h * 0.5f);
	const float landingBaseX = plantBounds.x + plantBounds.w + kLandingFrontGap;

	// 锚定响应必须先于重伤；即便伤害随后击杀植物，本次约束结果也不会回滚或改落点。
	const WinterGroundImpactResponse response = plant->ResolveWinterGroundImpact(
		WinterGroundImpactKind::COLLISION);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieBobsledPlantImpact", impactAnchor);
	}
	plant->TakeDamage(kCollisionDamage, DamageSource::ZOMBIE);
	BeginTeamLanding(response.intercepted && response.containsScatter, landingBaseX);
}

void BobsledTeamZombie::BeginTeamLanding(bool contained, float baseX)
{
	BobsledTeamZombie* leader = mRole == Role::LEADER ? this : ResolveLeader();
	if (!leader || leader->mPhase != Phase::RIDING || !leader->mBoard) return;
	const int sourceRow = leader->mRow;

	for (int slot = 0; slot < static_cast<int>(leader->mMemberIDs.size()); ++slot) {
		BobsledTeamZombie* member = leader->ResolveMember(slot);
		if (!member) continue;

		int targetRow = sourceRow;
		float targetX = baseX;
		if (contained) {
			targetX += kContainedSpacing * static_cast<float>(slot);
		}
		else {
			switch (slot) {
			case 1:
				targetX -= kSameRowBypass;
				break;
			case 2:
				targetRow = sourceRow > 0 ? sourceRow - 1 : sourceRow;
				targetX -= kAdjacentRowBypass;
				break;
			case 3:
				targetRow = sourceRow + 1 < leader->mBoard->mRows
					? sourceRow + 1 : sourceRow;
				targetX -= kAdjacentRowBypass;
				break;
			default:
				break;
			}
		}

		const float targetY = std::max(0.0f,
			leader->mBoard->GetZombieSpawnY(targetRow, targetX));
		member->BeginMemberLanding(contained, targetRow, Vector(targetX, targetY));
	}
}

void BobsledTeamZombie::BeginMemberLanding(
	bool contained, int targetRow, const Vector& targetPosition)
{
	if (mPhase == Phase::WALKING || mIsDead) return;
	mScatterContained = contained;
	mLandingElapsed = 0.0f;
	mLandingStart = GetPosition();
	mLandingTarget = targetPosition;
	mPhase = Phase::LANDING;
	mHelmType = HelmType::HELMTYPE_NONE;
	mHelmHealth = 0;
	mHelmMaxHealth = 0;
	CommitRow(targetRow);
	ConfigureColliderForPhase();
	PlayTrackOnce("anim_jump", "", kJumpClipSpeed, kJumpBlendTime);
}

void BobsledTeamZombie::BeginOrphanLanding()
{
	if (mPhase != Phase::RIDING || !mBoard) return;
	const Vector current = GetPosition();
	const float targetY = std::max(0.0f,
		mBoard->GetZombieSpawnY(mRow, current.x));
	BeginMemberLanding(false, mRow, Vector(current.x, targetY));
}

void BobsledTeamZombie::ConfigureColliderForPhase()
{
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(0.0f, mPhase == Phase::WALKING
			? kWalkingShadowOffsetY : kBobsledShadowOffsetY));
	}
	if (!mCollider) return;
	if (mPhase == Phase::RIDING) {
		if (mRole == Role::LEADER) {
			mCollider->mEnabled = !mIsPreview;
			mCollider->size = Vector(kSledColliderWidth, kSledColliderHeight);
			mCollider->offset = mVisualOffset + Vector(kSledDrawOffsetX + kSledColliderInsetX,
				kSledDrawOffsetY);
		}
		else {
			mCollider->mEnabled = false;
		}
		return;
	}

	mCollider->mEnabled = !mIsPreview && !mIsDying && !mIsDead;
	mCollider->size = Vector(kWalkingColliderWidth, kWalkingColliderHeight);
	mCollider->offset = Vector(kWalkingColliderOffsetX, kWalkingColliderOffsetY);
}

void BobsledTeamZombie::SetCooldown(float timer, bool bypassShield)
{
	if (mPhase != Phase::WALKING) return;
	Zombie::SetCooldown(timer, bypassShield);
}

bool BobsledTeamZombie::CanBeCharmed() const
{
	return mPhase == Phase::WALKING && Zombie::CanBeCharmed();
}

bool BobsledTeamZombie::CanBeChilled() const
{
	return mPhase == Phase::WALKING && Zombie::CanBeChilled();
}

bool BobsledTeamZombie::CanBeFrozen() const
{
	return mPhase == Phase::WALKING;
}

bool BobsledTeamZombie::CanBeParalyzed() const
{
	return mPhase == Phase::WALKING;
}

bool BobsledTeamZombie::CanBeGrabbedByTangleKelp() const
{
	return mPhase == Phase::WALKING;
}

bool BobsledTeamZombie::CanBeTargetedByProjectile(bool targetsFlying) const
{
	if (targetsFlying) return false;
	return mPhase != Phase::RIDING || mRole == Role::LEADER;
}

bool BobsledTeamZombie::CanBeAffectedByGroundHazards() const
{
	return mPhase == Phase::WALKING;
}

bool BobsledTeamZombie::ShouldPlayDeathAnimation() const
{
	return mPhase == Phase::WALKING;
}

float BobsledTeamZombie::GetCurrentHorizontalMoveSpeed() const
{
	if (mIsDead || mIsDying || IsImmobilized()) return 0.0f;
	if (mPhase == Phase::LANDING) return 0.0f;
	if (mPhase == Phase::WALKING) return Zombie::GetCurrentHorizontalMoveSpeed();
	float speed = kRideSpeed * GetAmplifiedAbilitySpeedMultiplier();
	if (mBoard) {
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieRainSpeedMultiplier());
		speed *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieWindMoveMultiplier(false));
	}
	return std::max(0.0f, speed * GetRoofMarshalAssaultMoveMultiplier()
		* AmplifySpeedMultiplierForGoldenIce(GetDrumMoveMultiplier()) * GetAmberMovementMultiplier());
}

Vector BobsledTeamZombie::GetVisualPosition() const
{
	Vector position = Zombie::GetVisualPosition();
	if (mPhase == Phase::RIDING) {
		position.y += kRideAltitude[std::clamp(mSlot, 0, 3)];
	}
	else if (mPhase == Phase::LANDING) {
		const float t = std::clamp(mLandingElapsed / kLandingDuration, 0.0f, 1.0f);
		position.y -= std::sin(kPi * t) * kLandingArcHeight;
	}
	return position;
}

float BobsledTeamZombie::GetLandingTimeRemaining() const
{
	return mPhase == Phase::LANDING
		? std::max(0.0f, kLandingDuration - mLandingElapsed) : 0.0f;
}

void BobsledTeamZombie::TakeBodyDamage(int damage)
{
	if (damage <= 0 || mIsDead) return;
	if (mPhase == Phase::WALKING) {
		Zombie::TakeBodyDamage(damage);
		return;
	}
	mBodyHealth = std::max(0, mBodyHealth - damage);
	if (mBodyHealth <= 0) KillAttachedTeam();
}

void BobsledTeamZombie::HelmDrop()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	Zombie::HelmDrop();
	if (mPhase == Phase::RIDING) BeginTeamLanding(false, GetPosition().x);
}

void BobsledTeamZombie::HeadDrop()
{
	if (!mHasHead) return;
	const Vector anchor = mAnimator && mAnimator->HasTrack("anim_head1")
		? GetTrackWorldPosition("anim_head1") : GetPosition();
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	if (g_particleSystem) g_particleSystem->EmitEffect("ZombieBobsledHeadOff", anchor);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kDismemberSoundVolume);
}

void BobsledTeamZombie::ArmDrop()
{
	if (!mHasArm) return;
	const Vector anchor = mAnimator && mAnimator->HasTrack("Zombie_outerarm_lower")
		? GetTrackWorldPosition("Zombie_outerarm_lower") : GetPosition();
	ApplyDetachedArmVisuals();
	if (g_particleSystem) g_particleSystem->EmitEffect("ZombieBobsledArmOff", anchor);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, kDismemberSoundVolume);
}

void BobsledTeamZombie::ApplyDetachedArmVisuals() const
{
	if (!mAnimator) return;
	// 原版资源为啃食姿态另烘焙了一套小臂和手，断臂必须同时隐藏两套轨道。
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lowereating", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_handeating", false);
	mAnimator->SetTrackImage("Zombie_dolphinrider_outerarm_upper",
		ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED_OUTERARM_UPPER2));
}

void BobsledTeamZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
	}
	if (!mHasArm) {
		ApplyDetachedArmVisuals();
	}
	const_cast<BobsledTeamZombie*>(this)->ConfigureColliderForPhase();
}

void BobsledTeamZombie::KillAttachedTeam()
{
	BobsledTeamZombie* leader = mRole == Role::LEADER ? this : ResolveLeader();
	if (!leader || leader->mTeamTeardown) {
		mTeamTeardown = true;
		Zombie::Die();
		return;
	}

	leader->mTeamTeardown = true;
	std::array<BobsledTeamZombie*, 4> members{};
	// 预览队长和首帧尚未登记队员 ID 的正式队长也必须完成基础死亡，否则 Board 计数会永久多 1。
	members[0] = leader;
	for (int slot = 1; slot < static_cast<int>(members.size()); ++slot) {
		members[slot] = leader->ResolveMember(slot);
	}
	for (BobsledTeamZombie* member : members) {
		if (member) member->mTeamTeardown = true;
	}
	for (BobsledTeamZombie* member : members) {
		if (member && !member->mIsDead) member->Zombie::Die();
	}
}

void BobsledTeamZombie::Die()
{
	if (mIsDead) return;
	if (mPhase != Phase::WALKING && !mTeamTeardown) {
		KillAttachedTeam();
		return;
	}
	Zombie::Die();
}

void BobsledTeamZombie::Charred()
{
	if (mPhase != Phase::WALKING) {
		Die();
		return;
	}
	Zombie::Charred();
}

void BobsledTeamZombie::PlayWalkAnimation(float blendTime)
{
	PlayTrack("anim_walk", 0.0f, blendTime);
	if (!mHasArm) ApplyDetachedArmVisuals();
}

void BobsledTeamZombie::OnStartEating()
{
	PlayTrack("anim_eat", kEatClipSpeed, kEatBlendTime);
	if (!mHasArm) ApplyDetachedArmVisuals();
}

const Texture* BobsledTeamZombie::GetSledFrontTexture() const
{
	const bool previewTeamAnchor = mIsPreview && mTeamSpawned;
	const BobsledTeamZombie* leader = previewTeamAnchor
		? this : (mRole == Role::LEADER ? this : ResolveLeader());
	if (!leader) return nullptr;
	auto& resources = ResourceManager::GetInstance();
	if (previewTeamAnchor) {
		return resources.GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED1, false);
	}
	if (leader->mPhase == Phase::LANDING) {
		return resources.GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED4, false);
	}
	if (leader->mHelmMaxHealth <= 0
		|| leader->mHelmHealth <= leader->mHelmMaxHealth / 3) {
		return resources.GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED3, false);
	}
	if (leader->mHelmHealth <= static_cast<int64_t>(leader->mHelmMaxHealth) * 2 / 3) {
		return resources.GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED2, false);
	}
	return resources.GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED1, false);
}

void BobsledTeamZombie::DrawSledLayer(
	Graphics* g, bool drawInside, bool drawFront) const
{
	if (!g || (!drawInside && !drawFront)) return;
	const bool previewTeamAnchor = mIsPreview && mTeamSpawned;
	const BobsledTeamZombie* leader = previewTeamAnchor
		? this : (mRole == Role::LEADER ? this : ResolveLeader());
	if (!leader) return;
	Vector leaderVisualPosition = leader->Zombie::GetVisualPosition();
	if (previewTeamAnchor) {
		// 展示队员没有注册 ID；由负责分层的槽位反推队长原点，避免伪造玩法引用。
		leaderVisualPosition.x -= GetPreviewMemberOffsetX(mSlot);
	}
	const Vector base = leaderVisualPosition + Vector(kSledDrawOffsetX, kSledDrawOffsetY);
	const float scale = leader->GetTransform()
		? leader->GetTransform()->GetScale() : 1.0f;
	float alpha = 255.0f;
	if (leader->mPhase == Phase::LANDING) {
		alpha = 255.0f * std::clamp(
			(kLandingDuration - leader->mLandingElapsed) / kSledFadeDuration,
			0.0f, 1.0f);
	}
	const glm::vec4 tint(255.0f, 255.0f, 255.0f, alpha);
	auto drawTexture = [g, base, scale, tint](const Texture* texture) {
		if (!texture) return;
		const float width = static_cast<float>(texture->width) * scale;
		const float height = static_cast<float>(texture->height) * scale;
		if (g->IsInstancePathEnabled()) {
			g->DrawTextureInstanced(texture, base.x, base.y, width, height, 0.0f, tint);
		}
		else {
			g->DrawTexture(texture, base.x, base.y, width, height, 0.0f, tint);
		}
	};
	if (drawInside) {
		drawTexture(ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED_INSIDE, false));
	}
	if (drawFront) drawTexture(GetSledFrontTexture());
}

void BobsledTeamZombie::Draw(Graphics* g)
{
	// 图鉴等单体预览仍自行包住一辆雪橇；选卡完整编队沿用正式四槽的单车分层。
	const bool singlePreview = mIsPreview && !mTeamSpawned;
	const float landingT = mPhase == Phase::LANDING
		? std::clamp(mLandingElapsed / kLandingDuration, 0.0f, 1.0f) : 0.0f;
	if (singlePreview) {
		DrawSledLayer(g, true, false);
	}
	else if (mPhase == Phase::RIDING && mSlot == 2) {
		DrawSledLayer(g, true, true);
	}
	else if (mPhase == Phase::LANDING && landingT < 0.5f && mSlot == 2) {
		DrawSledLayer(g, true, true);
	}
	else if (mPhase == Phase::LANDING && landingT >= 0.5f && mSlot == 3) {
		DrawSledLayer(g, true, false);
	}

	Zombie::Draw(g);

	if (singlePreview) {
		DrawSledLayer(g, false, true);
	}
	else if (mPhase == Phase::LANDING && landingT >= 0.5f
		&& mRole == Role::LEADER) {
		DrawSledLayer(g, false, true);
	}
}

void BobsledTeamZombie::SaveExtraData(nlohmann::json& j) const
{
	j["bobsledRole"] = static_cast<int>(mRole);
	j["bobsledPhase"] = static_cast<int>(mPhase);
	j["bobsledSlot"] = mSlot;
	j["bobsledLeaderID"] = mLeaderID;
	j["bobsledMemberIDs"] = mMemberIDs;
	j["bobsledTeamSpawned"] = mTeamSpawned;
	j["bobsledScatterContained"] = mScatterContained;
	j["bobsledLandingElapsed"] = mLandingElapsed;
	j["bobsledLandingStartX"] = mLandingStart.x;
	j["bobsledLandingStartY"] = mLandingStart.y;
	j["bobsledLandingTargetX"] = mLandingTarget.x;
	j["bobsledLandingTargetY"] = mLandingTarget.y;
}

void BobsledTeamZombie::LoadExtraData(const nlohmann::json& j)
{
	mRole = static_cast<Role>(ClampRole(j.value("bobsledRole", 0)));
	mPhase = static_cast<Phase>(ClampPhase(j.value("bobsledPhase", 2)));
	mSlot = mRole == Role::LEADER ? 0
		: std::clamp(j.value("bobsledSlot", 1), 1, 3);
	mLeaderID = j.value("bobsledLeaderID",
		mRole == Role::LEADER ? mZombieID : NULL_ZOMBIE_ID);
	mMemberIDs.fill(NULL_ZOMBIE_ID);
	if (const auto it = j.find("bobsledMemberIDs"); it != j.end() && it->is_array()) {
		const std::size_t count = std::min(it->size(), mMemberIDs.size());
		for (std::size_t index = 0; index < count; ++index) {
			mMemberIDs[index] = (*it)[index].get<int>();
		}
	}
	if (mRole == Role::LEADER) {
		mLeaderID = mZombieID;
		mMemberIDs[0] = mZombieID;
	}
	// 读档实体只能恢复既有成员；即使旧档/损坏档保存了 false，也不能在更新时补生一支新队。
	mTeamSpawned = true;
	mScatterContained = j.value("bobsledScatterContained", false);
	mLandingElapsed = std::clamp(
		j.value("bobsledLandingElapsed", 0.0f), 0.0f, kLandingDuration);
	mLandingStart = Vector(
		j.value("bobsledLandingStartX", GetPosition().x),
		j.value("bobsledLandingStartY", GetPosition().y));
	mLandingTarget = Vector(
		j.value("bobsledLandingTargetX", GetPosition().x),
		j.value("bobsledLandingTargetY", GetPosition().y));
	mTeamTeardown = false;
	ConfigureColliderForPhase();
	ZombieItemUpdate();
}
