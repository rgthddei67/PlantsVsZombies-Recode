#include "CrystalHornMinerZombie.h"
#include "Game/Board/Board.h"
#include "Game/Plant/Plant.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr int kBodyHealth = 1000; // 本体生命
	constexpr int kHelmetHealth = 1000; // 非磁性一类晶角头盔生命
	constexpr int kImpactDamage = 500; // 首株植物战斗顶层的一次冲撞伤害
	constexpr float kWindupSeconds = 1.2f; // 蓄力游戏秒，受普通减速影响
	constexpr float kCooldownSeconds = 6.0f; // 完整冲撞冷却，游戏秒
	constexpr float kTriggerCells = 5.0f; // 触发距离，当前前进方向格数
	constexpr float kChargeCells = 6.0f; // 冲撞最大行进距离，格数
	constexpr float kWindupHeadX = -4.0f; // 低头时整组头部分件前移，动画像素
	constexpr float kWindupHeadY = 4.0f; // 低头时整组头部分件下移，保持颈部重叠
}

void CrystalHornMinerZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = kBodyHealth;
	mHelmHealth = mHelmMaxHealth = kHelmetHealth;
	mHelmType = HelmType::HELMTYPE_CRYSTAL_HORN;
	mAttackDamage *= 2;
	SyncEquipment();
}

void CrystalHornMinerZombie::Update()
{
	if (!mIsPreview) {
		if (!HasHead() || IsMindControlled() || mIsDying || mHelmHealth <= 0) {
			if (mPhase == Phase::WINDUP || mPhase == Phase::CHARGING) AbortCharge();
		}
		if (mPhase == Phase::COOLDOWN) {
			mRemaining = std::max(0.0f,mRemaining - DeltaTime::GetDeltaTime());
			if (mRemaining == 0.0f) mPhase = Phase::READY;
		}
		if (!IsImmobilized() && !IsTangleKelpTarget() && !IsGarlicRedirectPaused()) TryBeginCharge();
	}
	Zombie::Update();
	SyncEquipment();
}

Plant* CrystalHornMinerZombie::FindChargePlant(float reach) const
{
	if (!mBoard) return nullptr;
	Plant* nearest = nullptr;
	float best = reach + 0.001f;
	const Vector pos = GetPosition();
	for (int r = 0; r < mBoard->mRows; ++r) for (int c = 0; c < mBoard->mColumns; ++c) {
		Plant* plant = mBoard->GetTopPlantAt(r,c);
		if (!plant || !plant->CanBeEaten()) continue;
		const Vector center(mBoard->GetCellCenterPosition(r,c).x,mBoard->GetZombieSpawnY(r,pos.x));
		const Vector diff = center - pos;
		const float along = diff.x * mDirection.x + diff.y * mDirection.y;
		const float across = std::abs(diff.x * mDirection.y - diff.y * mDirection.x);
		const float width = mDirection.y == 0 ? mBoard->GetCellHeight() : CELL_COLLIDER_SIZE_X;
		if (along < -0.45f * CELL_COLLIDER_SIZE_X || along > best || across > width * 0.4f) continue;
		if (mBoard->MineBlocksSegment(pos,center)) continue;
		best = along;
		nearest = plant;
	}
	return nearest;
}

void CrystalHornMinerZombie::TryBeginCharge()
{
	if (mPhase != Phase::READY || !mBoard || !IsActive() || mIsDying
		|| !HasHead() || IsMindControlled() || mHelmHealth <= 0) return;
	const Vector pos = GetPosition();
	mDirection = Vector(-1,0);
	if (mBoard->IsMineBackground() && mMineTargetCell >= 0) {
		const int r = mMineTargetCell / mBoard->mColumns, c = mMineTargetCell % mBoard->mColumns;
		const Vector target(mBoard->GetCellCenterPosition(r,c).x,mBoard->GetZombieSpawnY(r,pos.x));
		const Vector diff = target-pos;
		if (std::abs(diff.y) > std::abs(diff.x)) mDirection = Vector(0,diff.y < 0 ? -1.0f : 1.0f);
	}
	const float cellSize = mDirection.y == 0 ? CELL_COLLIDER_SIZE_X : mBoard->GetCellHeight();
	if (!FindChargePlant(kTriggerCells * cellSize)) return;
	// 从当前位置沿已锁定直线逐格裁剪，保留起步时格内偏移，最多前进六格。
	mEnd = pos;
	for (int step = 1; step <= static_cast<int>(kChargeCells); ++step) {
		const Vector next = pos + mDirection * (step * cellSize);
		const int c = static_cast<int>(std::lround((next.x-mBoard->GetCellCenterPosition(0,0).x)/CELL_COLLIDER_SIZE_X));
		const int r = static_cast<int>(std::lround((next.y-mBoard->GetZombieSpawnY(0,next.x))/mBoard->GetCellHeight()));
		if (c < 0 || c >= mBoard->mColumns || r < 0 || r >= mBoard->mRows) break;
		if (mBoard->IsMineBackground() && (mBoard->mMineGrid.IsRock(r,c)
			|| !mBoard->mMineGrid.connected[MineGrid::Index(r,c)])) break;
		mEnd = next;
	}
	if ((mEnd-pos).sqrMagnitude() < 1.0f) return;
	CancelEatingForSpecialAction();
	PlayWalkAnimation(0.0f);
	mChargeSpeed = GetUncontrolledHorizontalMoveSpeed() * 3.0f;
	mPhase = Phase::WINDUP;
	mRemaining = kWindupSeconds;
	mTravelled = 0.0f;
	PlayTrack("anim_idle",1.0f,0.15f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SWING,0.3f);
}

void CrystalHornMinerZombie::ZombieMove(float delta, Transform* transform)
{
	if (mPhase == Phase::WINDUP) {
		mRemaining = std::max(0.0f,mRemaining-delta);
		if (mRemaining == 0.0f) {
			mPhase = Phase::CHARGING;
			PlayWalkAnimation(0.05f);
		}
		return;
	}
	if (mPhase != Phase::CHARGING) { Zombie::ZombieMove(delta,transform); return; }
	const Vector pos = GetPosition();
	const float remaining = std::max(0.0f,(mEnd-pos).x*mDirection.x+(mEnd-pos).y*mDirection.y);
	const float movement = std::min(remaining,mChargeSpeed*delta);
	// 扫掠前沿防止高速穿透；一次只消费战斗顶层，先退出动作再结算。
	if (Plant* plant = FindChargePlant(movement + CELL_COLLIDER_SIZE_X * 0.45f)) {
		AbortCharge();
		plant->TakeDamage(kImpactDamage,DamageSource::ZOMBIE);
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BONK,0.45f);
		return;
	}
	transform->SetPosition(pos + mDirection * movement);
	mTravelled += movement / (mDirection.y == 0 ? CELL_COLLIDER_SIZE_X : mBoard->GetCellHeight());
	const int row = std::clamp(static_cast<int>(std::lround((GetPosition().y
		-mBoard->GetZombieSpawnY(0,GetPosition().x))/mBoard->GetCellHeight())),0,mBoard->mRows-1);
	if (row != mRow) CommitMineRow(row);
	if (movement >= remaining) AbortCharge();
}

void CrystalHornMinerZombie::AbortCharge()
{
	const bool active = mPhase == Phase::WINDUP || mPhase == Phase::CHARGING;
	mPhase = Phase::COOLDOWN;
	mRemaining = kCooldownSeconds;
	mMineTargetCell = -1;
	if (active && !mIsEating && !mIsDying && IsActive()) PlayWalkAnimation(0.1f);
}

void CrystalHornMinerZombie::StartEat(ColliderComponent* other)
{
	if (mPhase != Phase::WINDUP && mPhase != Phase::CHARGING) Zombie::StartEat(other);
}

bool CrystalHornMinerZombie::InterruptUncommittedSpecialAction()
{
	if (mPhase != Phase::WINDUP) return false;
	AbortCharge();
	return true;
}

float CrystalHornMinerZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mPhase == Phase::WINDUP ? mRemaining : -1.0f;
}

void CrystalHornMinerZombie::OnMindControlled() { AbortCharge(); }

void CrystalHornMinerZombie::HelmDrop()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	Zombie::HelmDrop();
	AbortCharge();
	SyncEquipment();
	if (g_particleSystem) g_particleSystem->EmitEffect("CrystalHornBreak",GetRenderedTrackWorldPosition("anim_head1"));
}

void CrystalHornMinerZombie::TakePlantAshDamage(int damage)
{
	if (!mBoard || damage <= 0) return;
	const int scaled = mBoard->ScaleMineFogDamage(mBoard->GetPerkManager().ScaleTotalDamageToZombie(damage),this);
	// 灰烬表现只有耗尽完整耐久时才替代扣血，不能因本体低血绕过晶角头盔。
	if (CanBeCharred() && static_cast<int64_t>(mBodyHealth) + std::max(0,mHelmHealth) <= scaled) {
		Charred();
		return;
	}
	TakeDamage(damage,DamageSource::PLANT_ASH,false,false,false,PlantDamageOrigin::Ash());
}

void CrystalHornMinerZombie::HeadDrop()
{
	if (!HasHead()) return;
	AbortCharge();
	if (mHelmHealth <= 0) { Zombie::HeadDrop(); return; }
	const Vector origin = GetRenderedTrackWorldPosition("anim_head1");
	for (const char* track : {"anim_head1","anim_head2","anim_tongue","anim_hair"}) mAnimator->SetTrackVisible(track,false);
	mAnimator->SetTrackFollowerVisible("anim_head1","crystal_horn",false);
	if (g_particleSystem) g_particleSystem->EmitEffect("CrystalHornHeadOff",origin + Vector(25,15));
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP,0.25f);
}

void CrystalHornMinerZombie::Die()
{
	AbortCharge();
	Zombie::Die();
}

void CrystalHornMinerZombie::OnTemporalCoreStateRestored()
{
	if (mPhase == Phase::WINDUP || mPhase == Phase::CHARGING) AbortCharge();
	Zombie::OnTemporalCoreStateRestored();
	SyncEquipment();
}

void CrystalHornMinerZombie::OnTemporalRecreated()
{
	// 新替身没有死亡前的动作计时，进入完整冷却，避免复活免费获得一次立即冲撞。
	AbortCharge();
}

void CrystalHornMinerZombie::SyncEquipment() const
{
	if (!mAnimator) return;
	const bool windup = mPhase == Phase::WINDUP;
	mAnimator->SetTrackFollowerImage("anim_head1","crystal_horn",ResourceManager::GetInstance().GetTexture(
		mHelmHealth * 2 <= mHelmMaxHealth ? "IMAGE_CRYSTALHORN_CRACKED" : "IMAGE_CRYSTALHORN_INTACT",false),-12,-10,0.72f,0.72f,true);
	mAnimator->SetTrackFollowerVisible("anim_head1","crystal_horn",HasHead() && mHelmHealth > 0 && !mIsDead);
	// 普通骨架的头、下巴、舌头和头发是独立世界轨；必须一起低头，不能只挪主头图。
	for (const char* track : {"anim_head1","anim_head2","anim_tongue","anim_hair"}) {
		mAnimator->SetTrackOffset(track,windup ? kWindupHeadX : 0.0f,windup ? kWindupHeadY : 0.0f);
		mAnimator->SetTrackColor(track,windup ? SDL_Color{170,240,255,255} : SDL_Color{255,255,255,255});
	}
}

void CrystalHornMinerZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	SyncEquipment();
}

void CrystalHornMinerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["chargePhase"] = static_cast<int>(mPhase); j["chargeRemaining"] = mRemaining;
	j["chargeTravelled"] = mTravelled; j["chargeSpeed"] = mChargeSpeed;
	j["chargeDirection"] = {mDirection.x,mDirection.y}; j["chargeEnd"] = {mEnd.x,mEnd.y};
}

void CrystalHornMinerZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<Phase>(std::clamp(j.value("chargePhase",0),0,3));
	mRemaining = std::clamp(j.value("chargeRemaining",0.0f),0.0f,kCooldownSeconds);
	mTravelled = std::clamp(j.value("chargeTravelled",0.0f),0.0f,kChargeCells);
	mChargeSpeed = std::max(0.0f,j.value("chargeSpeed",0.0f));
	if (j.contains("chargeDirection") && j["chargeDirection"].size() == 2)
		mDirection = Vector(j["chargeDirection"][0].get<float>(),j["chargeDirection"][1].get<float>());
	if (j.contains("chargeEnd") && j["chargeEnd"].size() == 2)
		mEnd = Vector(j["chargeEnd"][0].get<float>(),j["chargeEnd"][1].get<float>());
	if ((mPhase == Phase::WINDUP || mPhase == Phase::CHARGING) && (!HasHead() || mHelmHealth <= 0
		|| IsMindControlled() || mIsDying || !std::isfinite(mRemaining) || !std::isfinite(mEnd.x)
		|| !std::isfinite(mEnd.y) || !std::isfinite(mChargeSpeed) || mChargeSpeed <= 0.0f
		|| !((std::abs(mDirection.x) == 1.0f && mDirection.y == 0.0f)
			|| (mDirection.x == 0.0f && std::abs(mDirection.y) == 1.0f)))) AbortCharge();
	if (mPhase == Phase::WINDUP || mPhase == Phase::CHARGING) CancelEatingForSpecialAction();
	if (!std::isfinite(mRemaining)) mRemaining = kCooldownSeconds;
	if (!std::isfinite(mTravelled)) mTravelled = 0.0f;
	SyncEquipment();
}
