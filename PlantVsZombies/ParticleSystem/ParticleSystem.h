#pragma once
#ifndef __PARTICLE_SYSTEM_H__
#define __PARTICLE_SYSTEM_H__

#include "ParticleEffect.h"
#include "ParticleConfig.h"
#include "../Game/Definit.h"
#include "../Game/RenderOrder.h"
#include "../Graphics.h"
#include <vector>
#include <memory>

class ParticleSystem {
private:
	std::vector<std::unique_ptr<ParticleEffect>> effects;
	Graphics* m_graphics = nullptr;
	ParticleConfigManager configManager;

public:
	explicit ParticleSystem(Graphics* graphics);
	~ParticleSystem();

	ParticleSystem(const ParticleSystem&) = delete;
	ParticleSystem& operator=(const ParticleSystem&) = delete;

	void UpdateAll();
	// 绘制 renderOrder < order 的特效（战场层；由 GameObjectManager overlay 前 hook 调用）
	void DrawBelow(int order);
	// 绘制 renderOrder >= order 的特效（顶层；由场景命令槽调用）
	void DrawFrom(int order);
	void ClearAll();
	/** Board 销毁前清理引用该地形的短命碎片。 */
	void ClearZombieParts(const Board* board);

	bool LoadXMLConfigs(const std::string& directory);
	/**
	 * 发射粒子；clipRightX>=0 时把该特效裁到世界坐标右边界，适合会被实体阻断的横向喷雾。
	 */
	void EmitEffect(const std::string& effectName, const Vector& position,
		int renderOrder = LAYER_EFFECTS_WORLD, float durationOverride = -1.0f,
		float clipRightX = -1.0f);
	/**
	 * 从角色当前姿态脱落的部件：覆盖固定尺寸与初始角度，沿用 XML 的寿命和弹道。
	 * position 为贴图中心，scaleX/Y 为正的世界像素倍率，rotation 为屏幕角度（度）。
	 * 仅用于单发射器的部件效果；替代 XML 的尺寸、角度与局部 Position 偏移。
	 * textureKey 非空时替换部件贴图，镜像由调用方选择对应贴图；overlay 截取宿主状态染色。
	 */
	void EmitDetachedPart(const std::string& effectName, const Vector& position,
		float scaleX, float scaleY, float rotation, const std::string& textureKey = {},
		SDL_Color overlay = {255, 255, 255, 0}, ZombiePartTerrain terrain = {});
	/** 僵尸原有断肢/护具碎片只截取状态颜色，保留各自 XML 的位置、尺寸与组合发射器。 */
	void EmitZombiePartEffect(const std::string& effectName, const Vector& position, SDL_Color overlay,
		ZombiePartTerrain terrain = {});
	/** 停止全部同名特效继续发射；已生成粒子仍按各自寿命自然收尾。 */
	void StopEffect(const std::string& effectName);
	/** 查询同名特效是否至少有一个发射器仍在工作。 */
	bool IsEffectEmitting(const std::string& effectName) const;
	/** 返回全部同名特效当前存活粒子总数，供 AutoTest 和问题诊断使用。 */
	int GetEffectActiveParticleCount(const std::string& effectName) const;
	/** 只读暴露当前特效实例，供 AutoTest 导出最终绘制语义。 */
	const std::vector<std::unique_ptr<ParticleEffect>>& GetEffectsForTesting() const {
		return effects;
	}

private:
	void CleanupInactiveEffects();
};

extern std::unique_ptr<ParticleSystem> g_particleSystem;

#endif
