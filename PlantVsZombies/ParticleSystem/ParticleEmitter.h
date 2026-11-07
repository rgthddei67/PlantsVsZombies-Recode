#pragma once
#ifndef __PARTICLE_EMITTER_H__
#define __PARTICLE_EMITTER_H__

#include "Particle.h"
#include "ParticleXMLConfig.h"
#include "ZombiePartTerrain.h"
#include "../Graphics.h"
#include <optional>
#include <vector>

/**
 * AutoTest 最近一帧实际提交的粒子世界几何包围盒。
 * 只在 AutoTest 模式采集；坐标来自 DrawTextureRegion 使用的最终粒子矩形。
 */
struct ParticleRenderProbe {
	bool hasGeometry = false;
	int quadCount = 0;
	float minX = 0.0f;
	float minY = 0.0f;
	float maxX = 0.0f;
	float maxY = 0.0f;
	glm::vec4 firstColor = glm::vec4(255.0f); ///< 首个提交粒子的 RGBA 倍率，供出生帧状态染色取证
	int partLandContacts = 0;
	int partWaterContacts = 0;
};

class ParticleEmitter {
private:
	Graphics* m_graphics = nullptr;
	std::vector<Particle> particles;
	ParticleRenderProbe mLastRenderProbe;

	Vector position;
	bool active = false;
	float spawnTimer = 0.0f;
	int spawnRate = 0;
	int maxParticles = 0;

	bool isOneShot = false;
	int particlesToEmit = 0;
	int particlesEmitted = 0;

	EmitterConfig xmlConfig;
	std::vector<ParticleField> activeFields;
	float systemTimer = 0.0f;
	ZombiePartTerrain partTerrain;

public:
	ParticleEmitter(Graphics* g = nullptr);
	~ParticleEmitter() = default;

	void SetGraphics(Graphics* g) { m_graphics = g; }

	void Initialize(const EmitterConfig& config, const Vector& pos, ZombiePartTerrain terrain);

	void SetSpawnRate(int rate) { spawnRate = rate; }
	void SetMaxParticles(int max) { maxParticles = max; }
	void SetOneShot(bool oneShot) { isOneShot = oneShot; }

	void EmitParticles(int count);
	void Stop() { active = false; }
	void Clear();

	bool IsActive() const { return active; }
	bool ShouldDestroy() const;
	int GetActiveParticleCount() const;
	void SetPosition(const Vector& pos) { position = pos; }
	Vector GetPosition() const { return position; }
	const ParticleRenderProbe& GetLastRenderProbe() const { return mLastRenderProbe; }

	/**
	 * 按缩放后的游戏时间推进发射、粒子属性和 Field；暂停时完整保留上一游戏帧状态。
	 */
	std::optional<Vector> Update();
	void Draw();

private:
	void EmitSingleParticle();
	Particle* GetFreeParticle();
	Vector GetSpawnPosition() const;
	std::optional<Vector> ResolvePartContact(Particle& particle, const Vector& previousCenter, float dt) const;
};

#endif
