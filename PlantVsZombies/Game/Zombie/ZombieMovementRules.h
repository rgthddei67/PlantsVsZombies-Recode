#pragma once

#include <array>
#include <algorithm>

/** 普通僵尸出生随机运动参数；实际生成和只读预测共享，不预读正式 RNG。 */
namespace ZombieMovementRules {
inline constexpr float BaseRootSpeed = 10; // _ground 每帧位移的基础换算倍率
inline constexpr int RootSpeedJitter = 3; // 出生时基础倍率的整数随机浮动，正负范围
inline constexpr float MinimumAnimationSpeed = 1.1f; // 出生基础播放倍率下限
inline constexpr float MaximumAnimationSpeed = 1.4f; // 出生基础播放倍率上限
inline constexpr float FootballRootMultiplier = 1.7f; // 普通橄榄球的独立根运动倍率
inline constexpr float FootballAnimationMultiplier = 1.8f; // 普通橄榄球的常驻动画能力倍率
inline constexpr float PinkFootballRootMultiplier = 1.85f; // 粉色橄榄球的独立根运动倍率
inline constexpr float PinkFootballAnimationMultiplier = 1.95f; // 粉色橄榄球的常驻动画能力倍率

/** 位置驱动车速：相对场地基准的线性段与内场速度，系数相对出生最高速。 */
struct PositionCurve {
	float left=0, right=0, innerAt=0, slow=1, inner=1;
	float Factor(float relativeX) const {
		if (right<=left) return 1;
		if (relativeX<=innerAt) return inner;
		return slow+(1-slow)*std::clamp((relativeX-left)/(right-left),0.0f,1.0f);
	}
};

/** 品种自有的出生运动画像；只读 getter 不创建实体、不读取未来随机数，临时能力另按时间线叠加。 */
struct BirthProfile {
	const char* clip = "anim_walk";
	const char* alternative = "anim_walk2";
	float rootMinimum = BaseRootSpeed-RootSpeedJitter, rootMaximum = BaseRootSpeed+RootSpeedJitter;
	float animationMinimum = MinimumAnimationSpeed, animationMaximum = MaximumAnimationSpeed;
	float abilityMinimum = 1, abilityMaximum = 1;
	bool linear = false;
	float velocityMinimum = 0, velocityMaximum = 0; // linear 时直接使用世界 px/游戏秒，不读取 _ground
	PositionCurve positionCurve; // 独立车速曲线；场地基准由 Board 提供
	bool phaseDependent = false; // 后续换阶段/装备会改变运动；不把出生画像冒充完整行为模拟
};

/** 出生速度的数值快照；valid=false 表示资源/登记缺失，禁止伪装成固定普通移速。 */
struct SpeedRange {
	std::array<float,3> speed{}; // 下限、均值、上限，px/游戏秒
	bool valid=false, phaseDependent=false;
};
}
