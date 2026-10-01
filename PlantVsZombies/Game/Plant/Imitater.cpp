#include "Imitater.h"

#include "GameDataManager.h"
#include "PlantUpgradeRules.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../../DeltaTime.h"
#include "../../ParticleSystem/ParticleSystem.h"

#include <algorithm>

namespace {
	constexpr float kInitialMorphDelaySeconds = 2.0f; // 原版 mStateCountdown=200cs 的等待时间，单位：秒
	constexpr float kImitaterReanimFps = 40.0f; // Imitater.reanim 资源基础帧率，单位：fps
	constexpr float kMorphPlaybackFps = 26.0f; // 原版 anim_explode 播放帧率，单位：fps
	constexpr float kMorphParticleFrame = 74.0f; // anim_explode(50..80) 的 80% 边沿，全局帧
	constexpr float kMorphParticleOffsetY = -4.0f; // 云团相对公共视觉锚点的纵向修正，单位：px
}

void Imitater::SetupPlant()
{
	Plant::SetupPlant();
	mPlantHealth = 300;
	mPlantMaxHealth = 300;
	mMorphCountdown = kInitialMorphDelaySeconds;
	if (auto* shadow = GetShadow()) {
		shadow->SetScale(Vector(0.8f, 0.8f));
		shadow->SetOffset(Vector(0.0f, 28.0f));
	}
}

void Imitater::SetImitaterTarget(PlantType target)
{
	auto& gameData = GameDataManager::GetInstance();
	mImitaterTarget = target != PlantType::PLANT_IMITATER && target != PlantType::PLANT_CARRYVINE
		&& !IsUpgradePlantType(target)
		&& !gameData.IsSkillCard(target)
		&& gameData.HasPlant(target) ? target : PlantType::NUM_PLANT_TYPES;
}

bool Imitater::HasValidTarget() const
{
	return mImitaterTarget != PlantType::PLANT_IMITATER && mImitaterTarget != PlantType::PLANT_CARRYVINE
		&& !IsUpgradePlantType(mImitaterTarget)
		&& !GameDataManager::GetInstance().IsSkillCard(mImitaterTarget)
		&& GameDataManager::GetInstance().HasPlant(mImitaterTarget);
}

void Imitater::SetInheritedBloverDirection(WindDirection direction)
{
	if (direction == WindDirection::TOWARD_HOUSE
		|| direction == WindDirection::TOWARD_FRONT) {
		mInheritedBloverDirection = direction;
	}
}

void Imitater::PlantUpdate()
{
	if (!HasValidTarget() || !mBoard) return;

	if (!mMorphing) {
		mMorphCountdown = std::max(0.0f,
			mMorphCountdown - static_cast<float>(DeltaTime::GetDeltaTime()));
		if (mMorphCountdown <= 0.0f) BeginMorph();
		return;
	}

	if (!mMorphParticleEmitted && GetCurrentFrame() >= kMorphParticleFrame) {
		EmitMorphParticle();
	}
	if (!IsAnimationPlaying()) {
		mBoard->MorphImitater(this);
		AudioSystem::PlaySound(mBoard->IsPoolSquare(this->mRow, this->mColumn)
			? ResourceKeys::Sounds::SOUND_PLANT_ONWATER
			: ResourceKeys::Sounds::SOUND_PLANT, 0.5f);
	}
}

void Imitater::BeginMorph()
{
	if (mMorphing) return;
	mMorphing = PlayTrackOnce("anim_explode", "",
		kMorphPlaybackFps / kImitaterReanimFps, 0.0f);
}

void Imitater::EmitMorphParticle()
{
	mMorphParticleEmitted = true;
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ImitaterMorph",
			GetVisualAnchorPosition() + Vector(0.0f, kMorphParticleOffsetY));
	}
}

void Imitater::Die()
{
	if (mBoard && mImitaterTarget == PlantType::PLANT_PLANTERN) {
		mBoard->NotifyPlanternRemoved(mPlantID);
	}
	Plant::Die();
}

void Imitater::SaveExtraData(nlohmann::json& j) const
{
	j["targetType"] = static_cast<int>(mImitaterTarget);
	j["morphCountdown"] = mMorphCountdown;
	j["morphing"] = mMorphing;
	j["morphParticleEmitted"] = mMorphParticleEmitted;
	j["inheritedBloverDirection"] = static_cast<int>(mInheritedBloverDirection);
}

void Imitater::LoadExtraData(const nlohmann::json& j)
{
	SetImitaterTarget(static_cast<PlantType>(
		j.value("targetType", static_cast<int>(PlantType::NUM_PLANT_TYPES))));
	mMorphCountdown = std::clamp(j.value("morphCountdown", kInitialMorphDelaySeconds),
		0.0f, kInitialMorphDelaySeconds);
	mMorphing = j.value("morphing", false);
	mMorphParticleEmitted = j.value("morphParticleEmitted", false);
	SetInheritedBloverDirection(static_cast<WindDirection>(j.value(
		"inheritedBloverDirection", static_cast<int>(WindDirection::TOWARD_FRONT))));

	// 损坏或过渡期存档不能留下“标记变身但仍循环 idle”的永久占位。
	if (mMorphing && GetCurrentTrackName() != "anim_explode") {
		mMorphing = false;
		BeginMorph();
	}
}
