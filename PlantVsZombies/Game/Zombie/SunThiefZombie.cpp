#include "SunThiefZombie.h"
#include "DeltaTime.h"
#include "ResourceKeys.h"
#include "Game/Board/Board.h"
#include "Game/AudioSystem.h"
#include "GameApp.h"
#include "ResourceManager.h"
#include "Graphics.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr int kHealth = 1500; // 本体生命，储光罐不提供护甲
	constexpr int kTheftAmount = 125; // 单次抽取上限，阳光
	constexpr int kCapacity = Board::kSunTheftCapacity; // 满载撤退使用 Board 账本上限，仍需三次完整抽取
	constexpr float kWindupSeconds = 1.0f; // 可被打断的抽取前摇，游戏秒
	constexpr float kCooldownSeconds = 1.5f; // 成功、空吸和被打断后的冷却，游戏秒
	constexpr float kRetreatMultiplier = 4.0f; // 撤离步频、啃食动画与位移共用倍率
	constexpr float kFacingPivot = 48.0f; // 复用普通魅惑骨架镜像轴，动画像素
}

void SunThiefZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = kHealth;
	SyncEquipment();
}

int SunThiefZombie::GetCarriedSun() const
{
	return mBoard && !mIsPreview ? mBoard->GetSunTheftRecord(mZombieID).carried : 0;
}

float SunThiefZombie::GetAbilityAnimSpeedMultiplier() const
{
	return mPhase == Phase::RETREAT ? kRetreatMultiplier : 1.0f;
}

void SunThiefZombie::Update()
{
	if (!mIsPreview && mBoard && IsActive() && !mIsDead && !mIsDying) {
		const auto record = mBoard->GetSunTheftRecord(mZombieID);
		if (mPhase != Phase::RETREAT && (!HasHead() || record.disabled || IsMindControlled())) {
			if (mPhase == Phase::WINDUP) FinishWindup(Phase::DISABLED);
			else mPhase = Phase::DISABLED;
		}
		if (!IsImmobilized()) {
			const float delta = DeltaTime::GetDeltaTime() * (GetCooldownTimer() > 0.0f ? 0.5f : 1.0f);
			mFullNotice = std::max(0.0f, mFullNotice - delta);
			if (mPhase == Phase::COOLDOWN || mPhase == Phase::WINDUP) mRemaining = std::max(0.0f, mRemaining - delta);
			if (mPhase == Phase::COOLDOWN && mRemaining <= 0.0f) mPhase = Phase::READY;
			if (mPhase == Phase::WINDUP && mRemaining <= 0.0f) {
				mBoard->CommitSunTheft(mZombieID, kTheftAmount);
				const bool full = mBoard->GetSunTheftRecord(mZombieID).stolen >= kCapacity;
				FinishWindup(full ? Phase::RETREAT : Phase::COOLDOWN);
				if (full) mFullNotice = 1.5f;
			}
			if (mPhase == Phase::READY && HasHead() && mBoard->GetSun() > 0
				&& GetPosition().x <= mBoard->GetCellCenterPosition(mRow,mBoard->mColumns-1).x + CELL_COLLIDER_SIZE_X*0.5f) {
				// 抽取就绪后抢占啃食；统一解除目标关系，避免残留食客计数或迟到咬伤。
				CancelEatingForSpecialAction();
				mPhase = Phase::WINDUP;
				mRemaining = kWindupSeconds;
				PlayTrack("anim_idle", 1.0f, 0.15f);
				AudioSystem::PlaySound("SOUND_BLEEP", 0.2f);
			}
		}
		if (mPhase == Phase::RETREAT) {
			const float edge = mBoard->GetCellCenterPosition(mRow,mBoard->mColumns-1).x + CELL_COLLIDER_SIZE_X*0.5f;
			if (GetPosition().x > edge + 20.0f && (!mBoard->IsMineBackground() || mBoard->mMineGrid.entrance[mRow])) {
				mBoard->CloseSunTheft(mZombieID, true);
				Die();
				return;
			}
		}
		UpdateAnimSpeed();
	}
	Zombie::Update();
	if (!mIsPreview && mAnimator && !mIsDying) mAnimator->SetFlipX(IsMovingRight(), kFacingPivot);
	SyncEquipment();
}

void SunThiefZombie::StartEat(ColliderComponent* other)
{
	if (mPhase != Phase::WINDUP) Zombie::StartEat(other);
}

void SunThiefZombie::ZombieMove(float delta, Transform* transform)
{
	if (mPhase != Phase::WINDUP) Zombie::ZombieMove(delta, transform);
}

void SunThiefZombie::FinishWindup(Phase phase)
{
	mPhase = phase;
	mRemaining = phase == Phase::COOLDOWN ? kCooldownSeconds : 0.0f;
	if (!mIsDying && !mIsDead && !mIsEating) PlayWalkAnimation(0.12f);
	UpdateAnimSpeed();
}

float SunThiefZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mPhase == Phase::WINDUP && !mIsDying && !mIsDead ? mRemaining : -1.0f;
}

bool SunThiefZombie::InterruptUncommittedSpecialAction()
{
	if (GetInterruptibleSpecialActionRemaining() < 0.0f) return false;
	FinishWindup(Phase::COOLDOWN);
	return true;
}

void SunThiefZombie::HeadDrop()
{
	if (mBoard && !mIsPreview) mBoard->DisableSunTheft(mZombieID);
	if (mPhase != Phase::RETREAT) FinishWindup(Phase::DISABLED);
	Zombie::HeadDrop();
}

void SunThiefZombie::Die()
{
	if (mIsDead) return;
	// 钟匠替换濒死外壳时同 ID 继续持款；只有实际清除才结算返款。
	if (mBoard && !mIsPreview && !IsRetiringForTemporalReplacement()) mBoard->CloseSunTheft(mZombieID, false);
	mPhase = Phase::DISABLED;
	Zombie::Die();
}

void SunThiefZombie::OnMindControlled()
{
	if (mBoard) { mBoard->CloseSunTheft(mZombieID, false); mBoard->DisableSunTheft(mZombieID); }
	FinishWindup(Phase::DISABLED);
	SyncEquipment();
}

void SunThiefZombie::OnTemporalRecreated()
{
	// 同 ID 账本保留死亡时的返款结果，重建只能继续尚未消费的累计额度。
	const auto record = mBoard->GetSunTheftRecord(mZombieID);
	FinishWindup(record.stolen >= kCapacity && record.carried > 0 ? Phase::RETREAT
		: record.disabled || record.escaped || record.stolen >= kCapacity ? Phase::DISABLED : Phase::COOLDOWN);
	SyncEquipment();
}

void SunThiefZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	if (mBoard->GetSunTheftRecord(mZombieID).disabled && mPhase != Phase::RETREAT) FinishWindup(Phase::DISABLED);
	SyncEquipment();
}

void SunThiefZombie::SyncEquipment() const
{
	if (!mAnimator) return;
	auto& resources = ResourceManager::GetInstance();
	const int stage = std::clamp((GetCarriedSun() * 3 + kCapacity - 1) / kCapacity, 0, 3);
	mAnimator->SetTrackFollowerImage("Zombie_body", "sun_tank",
		resources.GetTexture("IMAGE_SUNTHIEF_TANK" + std::to_string(stage), false), 24.0f, -18.0f, 1, 1, false);
	mAnimator->SetTrackFollowerVisible("Zombie_body", "sun_tank", true);
	mAnimator->SetTrackFollowerImage("Zombie_body", "sun_apron",
		resources.GetTexture("IMAGE_SUNTHIEF_APRON", false), -5.0f, 2.0f, 1, 1, false);
	mAnimator->SetTrackFollowerVisible("Zombie_body", "sun_apron", true);
	// 手臂与工具一起抬起，附件始终跟随同一根骨骼，断臂仍沿原骨架保留抽取装置。
	const float lift = mPhase == Phase::WINDUP ? -8.0f : 0.0f;
	mAnimator->SetTrackOffset("anim_innerarm2", 0.0f, lift);
	mAnimator->SetTrackGlowOverride("anim_innerarm2", mPhase == Phase::WINDUP || IsBodyHitFlashing());
	mAnimator->SetTrackFollowerImage("anim_innerarm2", "sun_nozzle",
		resources.GetTexture("IMAGE_SUNTHIEF_NOZZLE", false), -39.0f, 8.0f, 1, 1, false, true, true);
	mAnimator->SetTrackFollowerVisible("anim_innerarm2", "sun_nozzle", true);
	mAnimator->SetTrackFollowerImage("anim_innerarm2", "sun_hose",
		resources.GetTexture("IMAGE_SUNTHIEF_HOSE", false), -8.0f, 17.0f, 1, 1, true);
	mAnimator->SetTrackFollowerVisible("anim_innerarm2", "sun_hose", true);
}

void SunThiefZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	SyncEquipment();
}

void SunThiefZombie::Draw(Graphics* g)
{
	Zombie::Draw(g);
	if (!g || mIsPreview || mIsDead || mIsDying) return;
	const Vector p = GetPosition();
	if (GetCarriedSun() > 0) GameAPP::GetInstance().DrawText(std::to_string(GetCarriedSun()),Vector(p.x+24,p.y-76),glm::vec4(255,225,110,255),ResourceKeys::Fonts::FONT_FZCQ,16);
	if (mFullNotice > 0.0f) {
		if (const Texture* t = ResourceManager::GetInstance().GetTexture("IMAGE_PRISM_MARK",false)) {
			if (g->IsInstancePathEnabled()) g->DrawTextureInstanced(t,p.x+18,p.y-105,28,28,0,glm::vec4(255,230,120,255));
			else g->DrawTexture(t,p.x+18,p.y-105,28,28,0,glm::vec4(255,230,120,255));
		}
	}
}

void SunThiefZombie::SaveExtraData(nlohmann::json& j) const
{
	j["theftPhase"] = static_cast<int>(mPhase);
	j["theftRemaining"] = mRemaining;
}

void SunThiefZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<Phase>(std::clamp(j.value("theftPhase",0),0,4));
	mRemaining = std::clamp(j.value("theftRemaining",0.0f),0.0f,mPhase == Phase::WINDUP ? kWindupSeconds : kCooldownSeconds);
	const auto record = mBoard->GetSunTheftRecord(mZombieID);
	if (IsMindControlled() || mIsDying || (mPhase != Phase::RETREAT && (!HasHead() || record.disabled))) mPhase = Phase::DISABLED;
	if (mPhase == Phase::RETREAT && record.carried <= 0) mPhase = Phase::DISABLED;
	if (record.stolen >= kCapacity && mPhase != Phase::RETREAT)
		mPhase = record.carried > 0 && !record.escaped && !IsMindControlled() ? Phase::RETREAT : Phase::DISABLED;
	if (mPhase == Phase::WINDUP && mIsEating) { mPhase = Phase::COOLDOWN; mRemaining = kCooldownSeconds; }
	UpdateAnimSpeed();
	SyncEquipment();
}
