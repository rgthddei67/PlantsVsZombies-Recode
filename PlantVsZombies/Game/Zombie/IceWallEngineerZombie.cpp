#include "IceWallEngineerZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "Game/Board/Board.h"
#include "../IceWall.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>

namespace {
	constexpr int kEngineerBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ICE_WALL_ENGINEER).body;              // 工程师本体生命；威胁主体仍是施工而非纯耐久
	constexpr float kConstructionDuration = 4.0f;         // 从停步到冰墙原子提交的施工游戏秒数
	constexpr float kConstructionWallGap = 58.0f;         // 墙与 collider 前缘错开以露出施工装饰，单位 px
	constexpr float kBuildParticleInterval = 0.45f;       // 施工碎冰反馈间隔，单位游戏秒
}

void IceWallEngineerZombie::SetupZombie()
{
	// 复用路障的完整时间轴和既有帧事件，施工仅由逻辑计时驱动。
	ConeZombie::SetupZombie();
	mBodyHealth = kEngineerBodyHealth;
	mBodyMaxHealth = kEngineerBodyHealth;
	mConstructionPhase = ConstructionPhase::MOVING;
	mConstructionRemaining = 0.0f;
	mBuildWallCenterX = 0.0f;
	mBuildParticleTimer = 0.0f;
	mConstructionUsed = false;
	ApplyEngineerEquipmentTextures();
	if (mIsPreview) PlayTrack("anim_idle");
}

void IceWallEngineerZombie::Update()
{
	ConeZombie::Update();
	// 基类会在冻结、黄油和麻痹时跳过 ZombieUpdate；墙体若在此期间碎裂，
	// 施工者仍须当帧收口，避免存档留下 BUILDING 但无半成品墙的组合。
	if (mConstructionPhase == ConstructionPhase::BUILDING
		&& ShouldAbortConstruction()) {
		CancelConstruction(true);
	}
}

void IceWallEngineerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!transform) return;
	if (mConstructionPhase == ConstructionPhase::BUILDING) {
		if (ShouldAbortConstruction()) CancelConstruction(true);
		else return;
	}
	ConeZombie::ZombieMove(scaledDelta, transform);
}

void IceWallEngineerZombie::StartEat(ColliderComponent* other)
{
	if (mConstructionPhase != ConstructionPhase::BUILDING) ConeZombie::StartEat(other);
}

void IceWallEngineerZombie::ZombieUpdate(float scaledTime)
{
	if (mConstructionPhase == ConstructionPhase::MOVING && !mConstructionUsed && CanBeginConstruction() && mCollider) {
		const SDL_FRect bounds = mCollider->GetBoundingBox();
		const float battlefieldRightX = CELL_INITALIZE_POS_X
			+ static_cast<float>(mBoard->mColumns) * CELL_COLLIDER_SIZE_X;
		if (bounds.x + bounds.w <= battlefieldRightX) {
			if (mBoard->HasIceWall()) {
				// 完全进场时已有全场唯一墙，本工程师不在旧墙破裂后补建。
				mConstructionUsed = true;
				mConstructionPhase = ConstructionPhase::COMPLETED;
			}
			else {
				const float desiredCenterX = bounds.x - kConstructionWallGap
					- IceWall::kBlockHalfWidth;
				const float wallCenterX = std::clamp(desiredCenterX,
					CELL_INITALIZE_POS_X + IceWall::kBlockHalfWidth,
					battlefieldRightX - IceWall::kBlockHalfWidth);
				if (BeginConstruction(wallCenterX)) return;
			}
		}
	}
	if (mConstructionPhase != ConstructionPhase::BUILDING) return;
	if (ShouldAbortConstruction()) {
		CancelConstruction(true);
		return;
	}

	mConstructionRemaining = std::max(0.0f,
		mConstructionRemaining - std::max(0.0f, scaledTime));
	mBuildParticleTimer -= std::max(0.0f, scaledTime);
	if (mBuildParticleTimer <= 0.0f) {
		mBuildParticleTimer = kBuildParticleInterval;
		if (g_particleSystem && mBoard) {
			const Vector anchor(mBuildWallCenterX,
				mBoard->GetZombieCollisionY(mRow, mBuildWallCenterX) - 24.0f);
			g_particleSystem->EmitEffect("IceWallBuild", anchor);
		}
	}
	if (mConstructionRemaining <= 0.0f) CompleteConstruction();
}

float IceWallEngineerZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mConstructionPhase == ConstructionPhase::BUILDING
		? mConstructionRemaining : -1.0f;
}

bool IceWallEngineerZombie::InterruptUncommittedSpecialAction()
{
	if (mConstructionPhase != ConstructionPhase::BUILDING) return false;
	// 外部工具只撤销这次未完成施工；墙体和动作状态在同一入口原子回退，能力可重试。
	CancelConstruction(false);
	return true;
}

bool IceWallEngineerZombie::CanBeginConstruction() const
{
	return mBoard && !mIsPreview && IsActive() && !mIsDying
		&& !IsMindControlled() && HasHead() && mBoard->SupportsWinterTemperature()
		&& mBoard->GetFrozenColumnCount() > 0;
}

bool IceWallEngineerZombie::ShouldAbortConstruction() const
{
	if (!mBoard || !IsActive() || mIsDying || IsMindControlled() || !HasHead()
		|| !mBoard->SupportsWinterTemperature()
		|| mBoard->GetFrozenColumnCount() <= 0) return true;
	const IceWall* wall = mBoard->GetIceWall();
	return !wall || !wall->IsUnderConstructionBy(mZombieID);
}

bool IceWallEngineerZombie::BeginConstruction(float wallCenterX)
{
	if (!mBoard || !mBoard->AddIceWall(mRow, wallCenterX,
		IceWall::kConstructionHealth, IceWall::kDefaultHealth, 0.0f,
		false, mZombieID)) return false;
	CancelEatingForSpecialAction();
	mConstructionPhase = ConstructionPhase::BUILDING;
	mConstructionRemaining = kConstructionDuration;
	mBuildWallCenterX = wallCenterX;
	mBuildParticleTimer = 0.0f;
	PlayTrack("anim_idle", 1.0f, 0.1f);
	return true;
}

void IceWallEngineerZombie::CancelConstruction(bool consumeAbility)
{
	if (mConstructionPhase != ConstructionPhase::BUILDING) return;
	if (mBoard) {
		if (IceWall* wall = mBoard->GetIceWall()) {
			wall->AbortConstruction(mZombieID);
		}
	}
	mConstructionUsed = mConstructionUsed || consumeAbility;
	mConstructionPhase = mConstructionUsed
		? ConstructionPhase::COMPLETED : ConstructionPhase::MOVING;
	mConstructionRemaining = 0.0f;
	mBuildParticleTimer = 0.0f;
	if (!mIsDying && IsActive()) PlayWalkAnimation(0.1f);
}

void IceWallEngineerZombie::CompleteConstruction()
{
	if (ShouldAbortConstruction()) {
		CancelConstruction(true);
		return;
	}
	IceWall* wall = mBoard->GetIceWall();
	if (!wall || !wall->CompleteConstruction(mZombieID)) {
		CancelConstruction(true);
		return;
	}
	mConstructionUsed = true;
	mConstructionPhase = ConstructionPhase::COMPLETED;
	mConstructionRemaining = 0.0f;
	mBuildParticleTimer = 0.0f;
	PlayWalkAnimation(0.1f);
}

void IceWallEngineerZombie::OnMindControlled()
{
	CancelConstruction(true);
}

void IceWallEngineerZombie::ZombieItemUpdate() const
{
	ConeZombie::ZombieItemUpdate();
	ApplyEngineerEquipmentTextures();
}

void IceWallEngineerZombie::SaveExtraData(nlohmann::json& j) const
{
	ConeZombie::SaveExtraData(j);
	j["constructionPhase"] = static_cast<int>(mConstructionPhase);
	j["constructionRemaining"] = mConstructionRemaining;
	j["buildWallCenterX"] = mBuildWallCenterX;
	j["buildParticleTimer"] = mBuildParticleTimer;
	j["constructionUsed"] = mConstructionUsed;
}

void IceWallEngineerZombie::LoadExtraData(const nlohmann::json& j)
{
	ConeZombie::LoadExtraData(j);
	const int phase = std::clamp(j.value("constructionPhase", 0), 0,
		static_cast<int>(ConstructionPhase::COMPLETED));
	mConstructionPhase = static_cast<ConstructionPhase>(phase);
	mConstructionRemaining = std::clamp(
		j.value("constructionRemaining", 0.0f), 0.0f, kConstructionDuration);
	mBuildWallCenterX = j.value("buildWallCenterX", 0.0f);
	mBuildParticleTimer = std::clamp(
		j.value("buildParticleTimer", 0.0f), 0.0f, kBuildParticleInterval);
	mConstructionUsed = j.value("constructionUsed",
		mConstructionPhase == ConstructionPhase::COMPLETED);
	if (mConstructionPhase == ConstructionPhase::BUILDING
		&& (!HasHead() || IsMindControlled() || mConstructionRemaining <= 0.0f)) {
		mConstructionPhase = ConstructionPhase::MOVING;
		mConstructionRemaining = 0.0f;
		mConstructionUsed = !HasHead() || IsMindControlled();
	}
	ApplyEngineerEquipmentTextures();
	if (mConstructionPhase == ConstructionPhase::BUILDING && mAnimator) {
		PlayTrack("anim_idle", 1.0f, 0.0f);
	}
}

void IceWallEngineerZombie::SetConstructionStateForTesting(
	ConstructionPhase phase, float remaining, float wallCenterX, bool used)
{
	mConstructionPhase = phase;
	mConstructionRemaining = std::clamp(remaining, 0.0f, kConstructionDuration);
	mBuildWallCenterX = wallCenterX;
	mBuildParticleTimer = 0.0f;
	mConstructionUsed = used;
	if (mAnimator) {
		if (phase == ConstructionPhase::BUILDING) PlayTrack("anim_idle", 1.0f, 0.0f);
		else PlayWalkAnimation(0.0f);
	}
}

const std::string& IceWallEngineerZombie::GetConeTextureKey(
	ArmorBrokenState stage) const
{
	using namespace ResourceKeys::Textures;
	if (stage == ArmorBrokenState::A_LITTLE_BROKEN) {
		return IMAGE_ZOMBIE_ICEWALL_ENGINEER_HAT2;
	}
	if (stage == ArmorBrokenState::REALLY_BROKEN) {
		return IMAGE_ZOMBIE_ICEWALL_ENGINEER_HAT3;
	}
	return IMAGE_ZOMBIE_ICEWALL_ENGINEER_HAT1;
}

void IceWallEngineerZombie::ApplyEngineerEquipmentTextures() const
{
	if (!mAnimator) return;
	if (mAnimator->HasTrack("anim_cone")
		&& mHelmType != HelmType::HELMTYPE_NONE
		&& mHelmStage != ArmorBrokenState::NONE) {
		if (const Texture* hardhat = ResourceManager::GetInstance().GetTexture(
			GetConeTextureKey(mHelmStage), false)) {
			mAnimator->SetTrackImage("anim_cone", hardhat);
		}
	}
	if (mAnimator->HasTrack("Zombie_body")) {
		if (const Texture* body = ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_BODY, false)) {
			mAnimator->SetTrackImage("Zombie_body", body);
		}
	}
	if (mAnimator->HasTrack("Zombie_tie")) {
		if (const Texture* toolStrap = ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_TOOLSTRAP, false)) {
			mAnimator->SetTrackImage("Zombie_tie", toolStrap);
		}
	}
}

ZombieMovementRules::BirthProfile IceWallEngineerZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.phaseDependent=true;
	return p;
}
