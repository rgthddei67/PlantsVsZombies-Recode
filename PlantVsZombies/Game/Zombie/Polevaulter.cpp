#include "Polevaulter.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../Plant/Plant.h"

#include <algorithm>
#include <limits>

namespace {
	constexpr float kBakedVaultDistance = 150.0f;  // anim_jump 轨道内置的水平视觉位移，单位 px
	constexpr float kVaultBlockProgress = 0.60f;  // C# 原版在 anim_jump 进度 0.6~0.7 检查高坚果
	constexpr float kBlockedPlantGap = 5.0f;  // 受阻落点中僵尸碰撞框与阻拦植物保留的间距，单位 px
}

void Polevaulter::SetupZombie()
{
	if (!mIsPreview) {
		mAnimator->AddFrameEvent(92, [this]() {
			this->EndJump();
			});
		mAnimator->AddFrameEvent(164, [this]() {
			this->Die();
			});
		mAnimator->AddFrameEvent(179, [this]() {
			this->EatTarget();
			}, true);
		mAnimator->AddFrameEvent(191, [this]() {
			this->EatTarget();
			}, true);

		this->SetAnimationSpeed(GameRandom::Range(2.2f, 3.2f));
		PlayTrack("anim_run");
		// 重写碰撞回调：RUNNING状态碰到植物触发跳跃，WALKING状态走基类吃植物逻辑
		auto collider = GetColliderComponent();
		if (collider) {
			collider->SetTriggerEnterCallback([this](ColliderComponent* other) {
				if (mIsPreview || mIsDying) return;

				auto* gameObject = other->GetGameObject();
				if (gameObject->GetObjectType() == ObjectType::OBJECT_ZOMBIE) {
					// 持杆奔跑不停下啃（跳跃语义优先，径直跑过魅惑僵尸）；跳后 WALKING 才互啃
					if (mVaultState == VaultState::WALKING) StartEat(other);
					return;
				}
				if (gameObject->GetObjectType() != ObjectType::OBJECT_PLANT) return;

				auto* plant = dynamic_cast<Plant*>(gameObject);
				if (!plant || plant->mRow != this->mRow) return;
				if (mBoard) {
					if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn)) {
						plant = top;
					}
				}
				// C# 在撑杆起跳之前检查该格扶梯；命中时保留撑杆并直接进入通用攀爬。
				if (TryStartLadderClimb(plant)) return;

				if (mVaultState == VaultState::RUNNING && !mHasVaulted && mHasHead) {
					// 原版先起跳，再在 anim_jump 60% 处检查 Tallnut；接触回调只锁定目标并开播。
					StartJump(plant);
				}
				else if (mVaultState == VaultState::WALKING) {
					// 跳跃后走基类吃植物逻辑
					StartEat(other);
				}
				});
		}
	}
	this->mSpeed = GameRandom::Range(15.0f, 18.0f);

	this->mBodyMaxHealth = 500;
	this->mBodyHealth = 500;

	if (auto shadowComponent = GetShadow()) {
		shadowComponent->SetOffset(Vector(4, 42));
	}
}

void Polevaulter::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	g_particleSystem->EmitEffect("PolevaulterHeadOff",
		GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void Polevaulter::ArmDrop()
{
	if (!mHasArm) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_polevaulter_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_polevaulter_outerarm_upper", ResourceManager::GetInstance().
		GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_POLEVAULTER_OUTERARM_UPPER2));
	g_particleSystem->EmitEffect("PolevaulterArmOff",
		GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void Polevaulter::ZombieItemUpdate() const
{
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
		mAnimator->SetTrackVisible("Zombie_polevaulter_outerarm_lower", false);
		mAnimator->SetTrackImage("Zombie_polevaulter_outerarm_upper", ResourceManager::GetInstance().
			GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_POLEVAULTER_OUTERARM_UPPER2));
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_hair", false);
	}
}

void Polevaulter::StartJump(Plant* target)
{
	// 原版 PolevaulterPreVault 明确要求仍有头；掉头后的流血期不能再开一次必然撞上死亡的跳跃。
	if (!target || mVaultState != VaultState::RUNNING || mHasVaulted || !mHasHead
		|| mIsPreview || mIsDying || mIsDead || !IsActive()) {
		return;
	}
	// 矿道行桶在越过中线时已更新，但起跳必须等身体抵达完整行基线。
	if (mBoard && mBoard->IsMineBackground()
		&& (target->mRow != mRow || std::abs(GetPosition().y
			- mBoard->GetZombieSpawnY(mRow, GetPosition().x)) > 0.01f)) return;
	// 跳跃替代当前水平行进段，落地后应从新位置重新选择矿道节点。
	mMineTargetCell = -1;
	mVaultState = VaultState::JUMPING;
	mLastVaultDistance = 0.0f;
	mVaultExtraDistanceApplied = 0.0f;
	mVaultStartX = GetPosition().x;
	mVaultBlockChecked = false;

	// 组合植物以当前格顶层为跳跃目标，避免先收到睡莲碰撞便跳过上层高坚果。
	if (target && mBoard) {
		if (Plant* topPlant = mBoard->GetTopPlantAt(target->mRow, target->mColumn)) {
			target = topPlant;
		}
	}
	mVaultTargetPlantID = target ? target->mPlantID : NULL_PLANT_ID;

	// 播放跳跃动画
	PlayTrackOnce("anim_jump", "anim_walk", 2.3f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_POLEVAULT_JUMP, 0.4f);

	// 跳跃期间禁用碰撞体，防止触发吃植物
	if (auto collider = GetColliderComponent()) {
		collider->mEnabled = false;
	}
	if (auto shadow = GetShadow()) {
		shadow->SetEnabled(false);
	}
}

void Polevaulter::EndJump()
{
	// 死亡或其他终止边沿优先于落地回调，不能让迟到的帧事件重新切回走路轨道。
	if (mVaultState != VaultState::JUMPING || mIsDying || mIsDead || !IsActive()) return;
	mVaultState = VaultState::WALKING;
	mHasVaulted = true;
	mVaultBlockChecked = true;
	mVaultTargetPlantID = NULL_PLANT_ID;

	const float vaultDistance = GetVaultDistance();
	const float targetExtraDistance = vaultDistance - kBakedVaultDistance;
	const float remainingExtraDistance = targetExtraDistance - mVaultExtraDistanceApplied;

	// 动画自身已经把身体部件向前带了 150 px；这里只提交该根运动以及尚未消费的额外位移。
	JumpMove(kBakedVaultDistance + remainingExtraDistance);
	mVaultExtraDistanceApplied = targetExtraDistance;
	mLastVaultDistance = vaultDistance;
	if (mBoard && mBoard->IsMineBackground()) {
		// 跳跃可以跨过岩壁，但落地及精英召唤必须落在连通空地；从预计落点朝起跳侧回退。
		const float first = mBoard->GetCellCenterPosition(mRow,0).x;
		const float proposed = GetPosition().x;
		int column = std::clamp(static_cast<int>(std::lround((proposed-first)/CELL_COLLIDER_SIZE_X)),0,mBoard->mColumns-1);
		for (; column>=0 && column<mBoard->mColumns; column += IsMovingRight() ? -1 : 1) {
			if (!mBoard->CanPlantOnMineCell(mRow,column)) continue;
			const int originalColumn=static_cast<int>(std::lround((proposed-first)/CELL_COLLIDER_SIZE_X));
			if (column!=originalColumn) SetPosition(Vector(mBoard->GetCellCenterPosition(mRow,column).x,GetPosition().y));
			break;
		}
		mMineTargetCell=-1;
		mLastVaultDistance=std::abs(GetPosition().x-mVaultStartX);
	}

	// 切换为走路动画和普通速度：跳跃后永久降速，写入动画 base（而非临时 clip）
	SetAnimationSpeed(GameRandom::Range(0.9f, 1.7f));
	PlayWalkAnimation(0.0f);
	mSpeed = GameRandom::Range(7.0f, 13.0f);

	// 恢复碰撞体
	if (auto collider = GetColliderComponent()) {
		collider->mEnabled = true;
	}
	if (auto shadow = GetShadow()) {
		shadow->SetEnabled(true);
	}

	// 派生能力必须看到最终落点与已恢复的碰撞状态。
	OnVaultLanded();
}

float Polevaulter::GetVaultProgress() const
{
	if (mVaultState != VaultState::JUMPING || !mAnimator) return 0.0f;

	const auto [jumpBeginFrame, jumpEndFrame] = mAnimator->GetTrackRange("anim_jump");
	const float jumpFrameCount = static_cast<float>(jumpEndFrame - jumpBeginFrame);
	if (jumpFrameCount <= 0.0f) return 0.0f;

	return std::clamp(
		(GetCurrentFrame() - static_cast<float>(jumpBeginFrame)) / jumpFrameCount,
		0.0f, 1.0f);
}

void Polevaulter::JumpMove(float distance)
{
	auto transform = GetTransform();
	if (!transform) return;

	if (mIsMindControlled) {
		transform->Translate(distance, 0);
	}
	else {
		transform->Translate(-distance, 0);
	}
}

float Polevaulter::GetVaultDistance() const
{
	return kBakedVaultDistance;
}

void Polevaulter::ZombieUpdate(float)
{
	if (mVaultState != VaultState::JUMPING
		|| mVaultBlockChecked
		|| GetVaultProgress() < kVaultBlockProgress) {
		return;
	}

	// 与原版 ShouldTriggerTimedEvent 一样，每次跳跃只在中段查询一次。
	mVaultBlockChecked = true;
	Plant* target = FindVaultBlockingPlant();
	if (!target) {
		mVaultTargetPlantID = NULL_PLANT_ID;
		return;
	}

	FinishBlockedVault(*target);
}

Plant* Polevaulter::FindVaultBlockingPlant() const
{
	if (!mBoard || !mCollider) return nullptr;

	// Animator 内置根位移不改变碰撞框；用起跳逻辑 X 和完整跳距构造碰撞框扫掠区，
	// 让精英长跳能发现初始目标后方的高坚果，而非只复查起跳植物 ID。
	const SDL_FRect currentBounds = mCollider->GetBoundingBox();
	const float startShift = mVaultStartX - GetPosition().x;
	const float startLeft = currentBounds.x + startShift;
	const float startRight = startLeft + currentBounds.w;
	const bool movingRight = IsMovingRight();
	const float signedDistance = movingRight ? GetVaultDistance() : -GetVaultDistance();
	const float endLeft = startLeft + signedDistance;
	const float endRight = startRight + signedDistance;
	const float sweepLeft = std::min(startLeft, endLeft);
	const float sweepRight = std::max(startRight, endRight);

	Plant* closest = nullptr;
	float closestEdge = movingRight
		? std::numeric_limits<float>::max()
		: std::numeric_limits<float>::lowest();
	for (int column = 0; column < mBoard->mColumns; ++column) {
		Plant* candidate = mBoard->GetJumpBlockingPlantAt(
			mRow, column, ZombieJumpType::POLEVAULT);
		const ColliderComponent* collider = candidate
			? candidate->GetColliderComponent() : nullptr;
		if (!collider) continue;

		const SDL_FRect bounds = collider->GetBoundingBox();
		const float right = bounds.x + bounds.w;
		if (right < sweepLeft || bounds.x > sweepRight) continue;

		const float encounterEdge = movingRight ? bounds.x : right;
		const bool isCloser = movingRight
			? encounterEdge < closestEdge
			: encounterEdge > closestEdge;
		if (isCloser) {
			closest = candidate;
			closestEdge = encounterEdge;
		}
	}
	return closest;
}

void Polevaulter::FinishBlockedVault(Plant& blockingPlant)
{
	if (mVaultState != VaultState::JUMPING) return;

	// 精英逐帧补过的额外 100px 必须撤回；动画内置位移会在换回走路轨时自然消失。
	if (mVaultExtraDistanceApplied != 0.0f) {
		JumpMove(-mVaultExtraDistanceApplied);
		mVaultExtraDistanceApplied = 0.0f;
	}
	if (mCollider) {
		// 长跳可能在初始普通植物之后才撞上高坚果；落到实际阻拦者迎敌面，保证后续啃食关系有效。
		const ColliderComponent* plantCollider = blockingPlant.GetColliderComponent();
		if (plantCollider) {
			const SDL_FRect plantBounds = plantCollider->GetBoundingBox();
			const SDL_FRect zombieBounds = mCollider->GetBoundingBox();
			Vector position = GetPosition();
			if (IsMovingRight()) {
				const float relativeRight = zombieBounds.x + zombieBounds.w - position.x;
				position.x = plantBounds.x - kBlockedPlantGap - relativeRight;
			}
			else {
				const float relativeLeft = zombieBounds.x - position.x;
				position.x = plantBounds.x + plantBounds.w + kBlockedPlantGap - relativeLeft;
			}
			SetPosition(position);
		}
	}
	mLastVaultDistance = 0.0f;
	mVaultState = VaultState::WALKING;
	mHasVaulted = true;

	SetAnimationSpeed(GameRandom::Range(0.9f, 1.7f));
	mSpeed = GameRandom::Range(7.0f, 13.0f);
	PlayWalkAnimation(0.0f);

	if (mCollider) {
		mCollider->mEnabled = true;
	}
	if (auto* shadow = GetShadow()) {
		shadow->SetEnabled(true);
	}

	blockingPlant.OnZombieJumpBlocked(ZombieJumpType::POLEVAULT);
	if (auto* plantCollider = blockingPlant.GetColliderComponent()) {
		StartEat(plantCollider);
	}
	// 派生效果在通用阻拦状态恢复后结算；精英会先召唤普通撑杆，再伤害阻拦植物。
	OnVaultBlocked(blockingPlant);
	mVaultTargetPlantID = NULL_PLANT_ID;
}

void Polevaulter::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mVaultState == VaultState::JUMPING) {
		const float targetExtraDistance =
			(GetVaultDistance() - kBakedVaultDistance) * GetVaultProgress();

		// 只把超出动画内置 150 px 的部分按实际动画进度逐帧补上，避免落地瞬移。
		JumpMove(targetExtraDistance - mVaultExtraDistanceApplied);
		mVaultExtraDistanceApplied = targetExtraDistance;
		return;
	}
	Zombie::ZombieMove(scaledDelta, transform);
}

void Polevaulter::PlayWalkAnimation(float blendTime)
{
	if (mVaultState == VaultState::JUMPING) {
		// 入水切换只更新介质视觉；跳跃轨道承载落地帧事件，不能被稳态走路动画抢占。
		return;
	}

	PlayTrack("anim_walk", 0.0f, blendTime);
}

void Polevaulter::SaveExtraData(nlohmann::json& j) const
{
	j["vaultState"] = static_cast<int>(mVaultState);
	j["hasVaulted"] = mHasVaulted;
	j["vaultExtraDistanceApplied"] = mVaultExtraDistanceApplied;
	j["vaultStartX"] = mVaultStartX;
	j["vaultTargetPlantID"] = mVaultTargetPlantID;
	j["vaultBlockChecked"] = mVaultBlockChecked;
}

void Polevaulter::LoadExtraData(const nlohmann::json& j)
{
	mVaultState = static_cast<VaultState>(j.value("vaultState", 0));
	mHasVaulted = j.value("hasVaulted", false);
	const float targetExtraDistance = GetVaultDistance() - kBakedVaultDistance;
	mVaultExtraDistanceApplied = std::clamp(
		j.value("vaultExtraDistanceApplied", 0.0f),
		std::min(0.0f, targetExtraDistance),
		std::max(0.0f, targetExtraDistance));
	const float currentX = GetPosition().x;
	const float signedApplied = IsMovingRight()
		? mVaultExtraDistanceApplied : -mVaultExtraDistanceApplied;
	mVaultStartX = j.value("vaultStartX", currentX - signedApplied);
	mVaultTargetPlantID = j.value("vaultTargetPlantID", NULL_PLANT_ID);
	mVaultBlockChecked = j.value("vaultBlockChecked", false);

	// 如果正在啃食，不覆盖已恢复的啃食动画
	if (mIsEating) return;

	// 恢复对应状态的动画
	if (mVaultState == VaultState::WALKING) {
		PlayWalkAnimation(0.0f);
	}
	else if (mVaultState == VaultState::JUMPING) {
		// Animator 已恢复原跳跃帧；继续推进到 60% 阻拦节点，不能读档后直接越过高坚果。
		if (mCollider) mCollider->mEnabled = false;
		if (auto* shadow = GetShadow()) {
			shadow->SetEnabled(false);
		}
	}
}

void Polevaulter::StartEat(ColliderComponent* other)
{
	if (other && other->GetGameObject()->GetObjectType() == ObjectType::OBJECT_PLANT) {
		if (auto* plant = dynamic_cast<Plant*>(other->GetGameObject())) {
			if (mBoard) {
				if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn)) {
					plant = top;
				}
			}
			// C# 在撑杆起跳判定前先检查扶梯；有梯时保留撑杆并直接攀爬。
			if (TryStartLadderClimb(plant)) return;
			// 换行期间拒绝的 enter 不会再次触发；持续接触在到达行基线后重试起跳。
			if (mBoard && mBoard->IsMineBackground() && mVaultState == VaultState::RUNNING) {
				StartJump(plant);
				return;
			}
		}
	}
	// 碰撞对在本帧回调前已经收集完：组合植物的第二个回调即使看到碰撞体已关闭也仍会到达。
	// 状态机入口统一守卫，避免 RUNNING/JUMPING 被任何植物或僵尸碰撞旁路切成啃食动画。
	if (mVaultState != VaultState::WALKING) return;
	Zombie::StartEat(other);
}
