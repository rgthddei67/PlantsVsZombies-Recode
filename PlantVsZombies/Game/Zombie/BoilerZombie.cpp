#include "BoilerZombie.h"
#include "Game/Board/Board.h"
#include "Game/Plant/Plant.h"
#include "DeltaTime.h"
#include "Graphics.h"
#include "GameApp.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include <algorithm>
#include <cmath>

using namespace BoilerRules;

void BoilerZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = mBodyMaxHealth = kHealth;
	ConfigureBoiler();
}

void BoilerZombie::ConfigureBoiler()
{
	mAnimator->SetTrackFollowerImage("Zombie_body", "boiler_pack",
		ResourceManager::GetInstance().GetTexture("IMAGE_BOILER_PACK", false),
		25.0f, -23.0f, 0.85f, 0.85f, true);
	mAnimator->SetTrackFollowerVisible("Zombie_body", "boiler_pack", true);
}

bool BoilerZombie::HasPlantInRange() const
{
	if (!mBoard || IsMindControlled()) return false;
	for (int col = 0; col < mBoard->mColumns; ++col) {
		// 暂时不可啃食的上层不能挡住同格仍合法的普通层或承载层。
		for (const auto* plant : {mBoard->GetPumpkinAt(mRow, col), mBoard->GetNormalPlantAt(mRow, col), mBoard->GetUnderPlantAt(mRow, col)}) {
			if (!plant || !plant->IsActive() || !plant->CanBeEaten()) continue;
			const float distance = GetPosition().x - plant->GetPosition().x;
			if (distance >= 0.0f && distance <= kTriggerCells * CELL_COLLIDER_SIZE_X) return true;
		}
	}
	return false;
}

void BoilerZombie::ChangePhase(Phase phase, float duration)
{
	if (phase == Phase::PREHEATING || phase == Phase::VENTING) CancelEatingForSpecialAction();
	mPhase = phase;
	mRemaining = duration;
	if (IsActive() && !IsDying()) {
		if (phase == Phase::PREHEATING) PlayTrack("anim_idle", 1.0f, 0.1f);
		else if (!mIsEating) PlayWalkAnimation(0.1f);
		UpdateAnimSpeed();
	}
}

void BoilerZombie::Update()
{
	if (!mIsPreview && mBoard && mBoard->mBoardState == BoardState::GAME
		&& !DeltaTime::IsPaused() && IsActive() && !IsDying()) {
		const float delta = DeltaTime::GetDeltaTime();
		mRetryRemaining = std::max(0.0f, mRetryRemaining - delta);
		if (!HasHead()) {
			if (mPhase != Phase::SPENT) ChangePhase(Phase::SPENT, 0);
		} else if (mPhase == Phase::READY && !mSpent && mRetryRemaining <= 0
			&& !IsImmobilized() && !IsDraggedUnderByTangleKelp() && HasPlantInRange()) {
			ChangePhase(Phase::PREHEATING, kPreheat);
		} else if (mPhase == Phase::PREHEATING) {
			if (!IsImmobilized() && !IsDraggedUnderByTangleKelp()) mRemaining -= delta;
			if (mRemaining <= 0) {
				// 扣费与消耗唯一机会在同一主线程边沿提交；读档只恢复阶段。
				if (mBoard->TrySpendZombieAbilityIce(kOverdriveCost)) {
					mSpent = true;
					ChangePhase(Phase::OVERDRIVE, kOverdrive);
				} else {
					ChangePhase(Phase::READY, 0);
					mRetryRemaining = kRetry;
				}
			}
		} else if (mPhase == Phase::OVERDRIVE || mPhase == Phase::VENTING) {
			mRemaining -= delta;
			if (mRemaining <= 0) ChangePhase(mPhase == Phase::OVERDRIVE ? Phase::VENTING : Phase::SPENT,
				mPhase == Phase::OVERDRIVE ? kVenting : 0.0f);
		}
	}
	Zombie::Update();
}

float BoilerZombie::GetAbilityAnimSpeedMultiplier() const
{
	if (IsDying() || !HasHead()) return 1.0f;
	return mPhase == Phase::OVERDRIVE ? kOverdriveSpeed : mPhase == Phase::VENTING ? kVentingSpeed : 1.0f;
}

float BoilerZombie::GetAbilityBiteDamageMultiplier() const
{
	return mPhase == Phase::OVERDRIVE && HasHead() && !IsDying() ? kOverdriveDamage : 1.0f;
}

void BoilerZombie::ZombieMove(float delta, Transform* transform)
{
	if (mPhase == Phase::PREHEATING) return;
	if (mPhase == Phase::VENTING && mBoard && mCollider && !IsMindControlled()) {
		const auto bounds = mCollider->GetBoundingBox();
		// 泄压不能啃食，也不能借此穿过防线；碰到植物后等待泄压结束再正常接敌。
		for (int col = 0; col < mBoard->mColumns; ++col) {
			for (auto* plant : {mBoard->GetPumpkinAt(mRow, col), mBoard->GetNormalPlantAt(mRow, col), mBoard->GetUnderPlantAt(mRow, col)}) {
				if (!plant || !plant->IsActive() || !plant->CanBeEaten() || !plant->GetCollider()) continue;
				const auto target = plant->GetCollider()->GetBoundingBox();
				if (bounds.x <= target.x + target.w + 1.0f && bounds.x + bounds.w >= target.x) return;
			}
		}
	}
	Zombie::ZombieMove(delta, transform);
}

void BoilerZombie::StartEat(ColliderComponent* other)
{
	if (mPhase != Phase::PREHEATING && mPhase != Phase::VENTING) Zombie::StartEat(other);
}

float BoilerZombie::GetInterruptibleSpecialActionRemaining() const
{
	return mPhase == Phase::PREHEATING ? std::max(0.0f, mRemaining) : -1.0f;
}

bool BoilerZombie::InterruptUncommittedSpecialAction()
{
	if (mPhase != Phase::PREHEATING) return false;
	ChangePhase(Phase::READY, 0);
	mRetryRemaining = kRetry;
	return true;
}

void BoilerZombie::OnMindControlled()
{
	// 魅惑取消尚未付款的前摇；已经提交的超频跟随实体，不返还敌方费用。
	if (mPhase == Phase::PREHEATING) ChangePhase(Phase::READY, 0);
}

void BoilerZombie::HeadDrop()
{
	ChangePhase(Phase::SPENT, 0);
	Zombie::HeadDrop();
}

void BoilerZombie::Die()
{
	ChangePhase(Phase::SPENT, 0);
	Zombie::Die();
}

void BoilerZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (mAnimator) mAnimator->SetTrackFollowerVisible("Zombie_body", "boiler_pack", IsActive());
}

void BoilerZombie::Draw(Graphics* g)
{
	Zombie::Draw(g);
	if (!g || mIsPreview || IsDying() || mPhase == Phase::READY || mPhase == Phase::SPENT) return;
	const auto p = GetPosition();
	const char* label = mPhase == Phase::PREHEATING ? u8"锅炉预热" : mPhase == Phase::OVERDRIVE ? u8"超频！" : u8"泄压";
	const glm::vec4 color = mPhase == Phase::OVERDRIVE ? glm::vec4(255, 110, 50, 255) : glm::vec4(160, 235, 255, 255);
	// 提示位于脚边；首行不会钻进卡槽，末行限制在可见画布内。
	const float y = std::clamp(p.y + 38.0f, 88.0f, SCENE_HEIGHT - 27.0f);
	g->FillRect(p.x - 34, y, 74, 21, glm::vec4(40, 27, 20, 220));
	g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, 14, color, p.x - 30, y + 1);
	const float maximum = mPhase == Phase::PREHEATING ? kPreheat : mPhase == Phase::OVERDRIVE ? kOverdrive : kVenting;
	g->FillRect(p.x - 32, y + 23, 68 * std::clamp(mRemaining / maximum, 0.0f, 1.0f), 4, color);
}

void BoilerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["boilerPhase"] = static_cast<int>(mPhase);
	j["boilerRemaining"] = mRemaining;
	j["boilerRetryRemaining"] = mRetryRemaining;
	j["boilerSpent"] = mSpent;
}

void BoilerZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<Phase>(std::clamp(j.value("boilerPhase", 0), 0, 4));
	mSpent = j.value("boilerSpent", false) || mPhase == Phase::OVERDRIVE || mPhase == Phase::VENTING || mPhase == Phase::SPENT;
	const float remaining = j.value("boilerRemaining", 0.0f), retry = j.value("boilerRetryRemaining", 0.0f);
	const float maximum = mPhase == Phase::PREHEATING ? kPreheat : mPhase == Phase::OVERDRIVE ? kOverdrive : mPhase == Phase::VENTING ? kVenting : 0.0f;
	mRemaining = std::isfinite(remaining) ? std::clamp(remaining, 0.0f, maximum) : 0.0f;
	mRetryRemaining = std::isfinite(retry) ? std::clamp(retry, 0.0f, kRetry) : 0.0f;
	if (!HasHead() || IsDying() || (mSpent && (mPhase == Phase::READY || mPhase == Phase::PREHEATING))) {
		mPhase = Phase::SPENT;
		mRemaining = 0;
	} else if (IsMindControlled() && mPhase == Phase::PREHEATING) {
		mPhase = Phase::READY;
		mRemaining = 0;
	}
	ConfigureBoiler();
	if (mPhase == Phase::PREHEATING || mPhase == Phase::VENTING) CancelEatingForSpecialAction();
	UpdateAnimSpeed();
}

ZombieMovementRules::BirthProfile BoilerZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.phaseDependent=true;
	return p;
}
