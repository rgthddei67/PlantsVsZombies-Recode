#include "ParticleSystem.h"
#include "../Logger.h"
#include <cmath>

std::unique_ptr<ParticleSystem> g_particleSystem = nullptr;

namespace {
	/** 保留 XML 的常量、随机区间及生命周期曲线，只乘入脱落瞬间的状态倍率。 */
	void MultiplyColorTrack(InterpolationTrack& track, float multiplier) {
		track.constantValue *= multiplier;
		track.randomMin *= multiplier;
		track.randomMax *= multiplier;
		for (auto& point : track.points) {
			point.value *= multiplier;
			point.valueLo *= multiplier;
			point.valueHi *= multiplier;
		}
	}

	/** 此开关只在僵尸部件专用入口设置，普通 EmitEffect 不改变出生颜色初始化。 */
	void ApplyDetachedPartTint(EmitterConfig& emitter, SDL_Color overlay) {
		emitter.initializeDetachedPartTint = true;
		// 合成本体 base + Alpha overlay 的不透明像素倍率；离体后固定此状态。
		const float alpha = overlay.a / 255.0f;
		MultiplyColorTrack(emitter.particleRed, 1 - alpha + alpha * overlay.r / 255.0f);
		MultiplyColorTrack(emitter.particleGreen, 1 - alpha + alpha * overlay.g / 255.0f);
		MultiplyColorTrack(emitter.particleBlue, 1 - alpha + alpha * overlay.b / 255.0f);
	}
}

ParticleSystem::ParticleSystem(Graphics* graphics)
	: m_graphics(graphics)
	, configManager(graphics) {
}

ParticleSystem::~ParticleSystem() {
	ClearAll();
}

void ParticleSystem::UpdateAll() {
	CleanupInactiveEffects();

	const size_t existingCount = effects.size();
	for (size_t i = 0; i < existingCount; ++i) {
		const auto splash = effects[i]->Update();
		if (splash) EmitEffect("ZombiePartSplash", *splash);
	}
}

void ParticleSystem::DrawBelow(int order) {
	for (auto& effect : effects) {
		if (effect->GetRenderOrder() < order) {
			effect->Draw();
		}
	}
}

void ParticleSystem::DrawFrom(int order) {
	for (auto& effect : effects) {
		if (effect->GetRenderOrder() >= order) {
			effect->Draw();
		}
	}
}

void ParticleSystem::ClearAll() {
	effects.clear();
}

void ParticleSystem::ClearZombieParts(const Board* board) {
	std::erase_if(effects, [board](const auto& effect) {
		return effect->GetPartTerrain().board == board;
	});
}

bool ParticleSystem::LoadXMLConfigs(const std::string& directory) {
	return configManager.LoadXMLConfigs(directory);
}

void ParticleSystem::EmitEffect(const std::string& effectName, const Vector& position,
	int renderOrder, float durationOverride, float clipRightX) {
	const ParticleEffectConfig* config = configManager.GetEffectConfig(effectName);
	if (!config) {
		LOG_ERROR("Particle") << "找不到粒子特效配置: " << effectName;
		return;
	}

	auto effect = std::make_unique<ParticleEffect>();
	effect->InitializeFromConfig(*config, m_graphics, position);
	effect->SetName(effectName);
	effect->SetRenderOrder(renderOrder);
	effect->SetSystemDuration(durationOverride);
	effect->SetClipRightX(clipRightX);
	effects.push_back(std::move(effect));
}

void ParticleSystem::EmitDetachedPart(const std::string& effectName, const Vector& position,
	float scaleX, float scaleY, float rotation, const std::string& textureKey, SDL_Color overlay,
	ZombiePartTerrain terrain) {
	const auto* source = configManager.GetEffectConfig(effectName);
	if (!source) {
		LOG_ERROR("Particle") << "找不到离体部件配置: " << effectName;
		return;
	}
	if (!(scaleX > 0 && scaleY > 0) || !std::isfinite(scaleX)
		|| !std::isfinite(scaleY) || !std::isfinite(rotation)) return;
	auto config = *source;
	for (auto& emitter : config.emitters) {
		ApplyDetachedPartTint(emitter, overlay);
		// 旧头盔配置的 Position 场要等首个 Update 才生效，暂停截图会先出现在腰部。
		// 这里已传入最终中心，移除旧局部位移，避免首帧和下一帧各使用一套锚点。
		emitter.emitterOffsetX = emitter.emitterOffsetY = 0;
		const auto localPosition = [](const ParticleField& field) {
			return field.type == ParticleFieldType::POSITION || field.type == ParticleFieldType::SYSTEM_POSITION;
		};
		std::erase_if(emitter.fields, localPosition);
		std::erase_if(emitter.systemFields, localPosition);
		if (!textureKey.empty()) emitter.imageKeys = {textureKey};
		emitter.particleScale = InterpolationTrack{};
		emitter.particleScale.constantValue = scaleX;
		emitter.particleStretch = InterpolationTrack{};
		emitter.particleStretch.constantValue = scaleY / scaleX;
		emitter.particleRotation = ValueRange(rotation);
		emitter.hasParticleRotation = true;
	}
	auto effect = std::make_unique<ParticleEffect>();
	effect->InitializeFromConfig(config, m_graphics, position, terrain);
	effect->SetName(effectName);
	effect->SetRenderOrder(LAYER_EFFECTS_WORLD);
	effects.push_back(std::move(effect));
}

void ParticleSystem::EmitZombiePartEffect(const std::string& effectName, const Vector& position,
	SDL_Color overlay, ZombiePartTerrain terrain) {
	const auto* source = configManager.GetEffectConfig(effectName);
	if (!source) {
		LOG_ERROR("Particle") << "找不到僵尸部件配置: " << effectName;
		return;
	}
	auto config = *source;
	for (auto& emitter : config.emitters) ApplyDetachedPartTint(emitter, overlay);
	auto effect = std::make_unique<ParticleEffect>();
	effect->InitializeFromConfig(config, m_graphics, position, terrain);
	effect->SetName(effectName);
	effect->SetRenderOrder(LAYER_EFFECTS_WORLD);
	effects.push_back(std::move(effect));
}

void ParticleSystem::StopEffect(const std::string& effectName) {
	for (auto& effect : effects) {
		if (effect->GetName() == effectName) effect->Stop();
	}
}

bool ParticleSystem::IsEffectEmitting(const std::string& effectName) const {
	for (const auto& effect : effects) {
		if (effect->GetName() == effectName && effect->IsEmitting()) return true;
	}
	return false;
}

int ParticleSystem::GetEffectActiveParticleCount(const std::string& effectName) const {
	int count = 0;
	for (const auto& effect : effects) {
		if (effect->GetName() == effectName) count += effect->GetActiveParticleCount();
	}
	return count;
}

void ParticleSystem::CleanupInactiveEffects() {
	effects.erase(
		std::remove_if(effects.begin(), effects.end(),
			[](const std::unique_ptr<ParticleEffect>& effect) {
				return effect->ShouldDestroy();
			}),
		effects.end()
	);
}
