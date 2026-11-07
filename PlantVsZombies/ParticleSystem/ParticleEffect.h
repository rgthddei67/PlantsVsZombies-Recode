#pragma once
#ifndef __PARTICLE_EFFECT_H__
#define __PARTICLE_EFFECT_H__

#include "ParticleEmitter.h"
#include "ParticleXMLConfig.h"
#include "../Game/Definit.h"
#include <vector>
#include <memory>
#include <string>
#include <optional>

// 多发射器粒子特效管理器
class ParticleEffect {
private:
	std::vector<std::unique_ptr<ParticleEmitter>> emitters;
	Graphics* graphics = nullptr;
	Vector position;
	float systemTimer = 0.0f;
	float systemDuration = -1.0f;
	bool active = false;
	int renderOrder = 0;
	std::string effectName;
	float clipRightX = -1.0f;
	ZombiePartTerrain partTerrain;

public:
	ParticleEffect() = default;
	~ParticleEffect() = default;

	// 从XML配置初始化
	void InitializeFromConfig(const ParticleEffectConfig& config, Graphics* graphics, const Vector& pos,
		ZombiePartTerrain terrain = {});

	/** 返回本步首次击水的位置，由 ParticleSystem 在更新后发射小水花。 */
	std::optional<Vector> Update();
	const ZombiePartTerrain& GetPartTerrain() const { return partTerrain; }
	void Draw();
	/** 停止继续发射，但保留现有粒子更新到自然消亡。 */
	void Stop();

	bool IsActive() const { return active; }
	/** 特效计时器有效且至少一个发射器仍在持续发射。 */
	bool IsEmitting() const;
	/** 返回所有发射器当前存活粒子数，供状态诊断使用。 */
	int GetActiveParticleCount() const;
	/** 聚合最近一帧各发射器实际提交的粒子世界包围盒，供 AutoTest 坐标取证。 */
	ParticleRenderProbe GetLastRenderProbe() const;
	bool ShouldDestroy() const;

	void SetRenderOrder(int order) { renderOrder = order; }
	int GetRenderOrder() const { return renderOrder; }
	/**
	 * 天气等持续特效可用运行期时长覆盖 XML 默认值；<=0 保留配置值。
	 * 正时长覆盖会让发射器循环复用粒子池，直到特效计时器统一停止。
	 */
	void SetSystemDuration(float duration);

	void SetName(const std::string& name) { effectName = name; }
	const std::string& GetName() const { return effectName; }
	/** 负值关闭裁剪；非负值把本特效绘制限制在世界坐标 x<=clipRightX。 */
	void SetClipRightX(float x) { clipRightX = x; }
	float GetClipRightX() const { return clipRightX; }

	void SetPosition(const Vector& pos);
	Vector GetPosition() const { return position; }
};

#endif
