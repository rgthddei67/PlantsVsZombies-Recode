#pragma once
#ifndef __PARTICLE_XML_CONFIG_H__
#define __PARTICLE_XML_CONFIG_H__

#include <string>
#include <vector>

// 插值轨迹点
struct InterpolationPoint {
	float value = 0.0f;    // 中点：GetValue 用，保持旧（确定性）行为
	float time = 0.0f;     // 归一化时间 0-1
	float valueLo = 0.0f;  // 关键帧 "[a b]" 随机区间下界（GetValueRandomized 用）
	float valueHi = 0.0f;  // 上界；非区间关键帧时 lo==hi==value

	InterpolationPoint() = default;
	InterpolationPoint(float v, float t) : value(v), time(t), valueLo(v), valueHi(v) {}
	InterpolationPoint(float v, float t, float lo, float hi) : value(v), time(t), valueLo(lo), valueHi(hi) {}

	// 按逐粒子随机因子 f∈[0,1] 在本关键帧区间内取值；非区间时恒返回 value。
	float Sample(float f) const { return valueLo + f * (valueHi - valueLo); }
};

// 插值轨迹（如 "1,90 0" 表示在90%时间值为1，结束时为0）
struct InterpolationTrack {
	std::vector<InterpolationPoint> points;
	bool isConstant = true;
	float constantValue = 1.0f;
	bool isRandomRange = false;   // "[a b]" 语义：每粒子在生成时随机一次，整生命周期保持
	float randomMin = 0.0f;
	float randomMax = 0.0f;

	InterpolationTrack() = default;

	// 根据归一化时间获取插值（确定性：同龄同值，全体粒子一致）
	float GetValue(float normalizedTime) const;

	// 同 GetValue，但每个 "[a b]" 关键帧区间用逐粒子随机因子 f∈[0,1] 取值，
	// 实现"每粒子各自扩散"。Position 场专用——这正是横向铺开的瘴气云所依赖的随机性。
	float GetValueRandomized(float normalizedTime, float randomFactor) const;

	// spawn 时调用：随机范围 → 采样一次；常量/插值 → 起始值
	float SampleConstant() const;
};

// 值范围（如 "[0.3 0.6]" 表示随机范围，或单个值 "1.5"）
struct ValueRange {
	float minValue = 0.0f;
	float maxValue = 0.0f;
	bool isRange = false;

	ValueRange() = default;
	ValueRange(float value) : minValue(value), maxValue(value), isRange(false) {}
	ValueRange(float min, float max) : minValue(min), maxValue(max), isRange(true) {}

	// 获取随机值
	float GetRandomValue() const;
};

// 场效果类型
enum class ParticleFieldType {
	INVALID,          // 未识别的场类型（不参与更新，避免静默误作位置场）
	POSITION,         // 位置场（影响粒子位置）
	SHAKE,            // 抖动场（随机抖动）
	SYSTEM_POSITION,  // 系统位置场（影响发射器位置）
	FRICTION,         // 摩擦力场（使粒子减速）
	ACCELERATION,     // 加速度场（按时间累加到速度）
	CIRCLE,           // 绕系统中心切向运动（原版 Circle field）
	AWAY              // 沿系统中心径向运动（原版 Away field）
};

// 场效果
struct ParticleField {
	ParticleFieldType type = ParticleFieldType::INVALID;
	InterpolationTrack xTrack;
	InterpolationTrack yTrack;

	ParticleField()
	{
		// 场未写某一轴时原版按 0 处理，不能沿用通用轨迹的视觉乘数默认值 1。
		xTrack.constantValue = 0.0f;
		yTrack.constantValue = 0.0f;
	}
};

// 发射器形状类型
enum class EmitterType {
	POINT,   // 点发射器
	BOX,     // 盒子发射器
	CIRCLE   // 圆形发射器
};

// 发射器配置
struct EmitterConfig {
	std::string name;

	// 基础属性
	ValueRange spawnMinActive = ValueRange(1.0f);     // 支持 "[a b]"：每次发射器初始化时随机一个数量
	ValueRange spawnMaxLaunched = ValueRange(1.0f);   // 同上
	int spawnRate = 0;
	ValueRange particleDuration;
	float systemDuration = -1.0f;  // -1表示无限

	// 粒子外观
	InterpolationTrack particleAlpha;
	InterpolationTrack particleScale;
	InterpolationTrack particleStretch;
	ValueRange particleBrightness;
	InterpolationTrack particleRed;
	InterpolationTrack particleGreen;
	InterpolationTrack particleBlue;
	InterpolationTrack systemAlpha;

	// 发射器形状
	EmitterType emitterType = EmitterType::POINT;
	ValueRange emitterBoxX;
	ValueRange emitterBoxY;
	ValueRange emitterRadius;
	float emitterOffsetX = 0.0f;
	float emitterOffsetY = 0.0f;

	// 运动属性
	ValueRange launchSpeed;
	bool randomLaunchSpin = false;
	ValueRange particleRotation;  // 生成时采样的初始角度（度）
	bool hasParticleRotation = false;  // 显式配置时使用不受非等比缩放扭曲的朝向路径
	ValueRange particleSpinSpeed;
	float particleGravity = 100.0f;

	// 动画
	std::vector<std::string> imageKeys;  // 支持逗号分隔的多个纹理
	int imageFrames = 1;
	float animationRate = 12.0f;

	// 场效果
	std::vector<ParticleField> fields;
	std::vector<ParticleField> systemFields;

	// 特殊属性
	bool fullScreen = false;
	bool initializeDetachedPartTint = false; // 仅僵尸离体部件入口设置；不由 XML 开启，不改变普通粒子出生行为

	EmitterConfig();
};

// 粒子特效配置（可包含多个发射器）
struct ParticleEffectConfig {
	std::string name;
	std::vector<EmitterConfig> emitters;

	ParticleEffectConfig() {}
	ParticleEffectConfig(const std::string& n) : name(n) {}
};

#endif
