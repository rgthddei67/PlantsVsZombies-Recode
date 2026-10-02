#pragma once
#include <algorithm>

/** 黄色冰道与无伤加速的纯数值契约；正式实体和后台预测共用，不拥有场地状态。 */
namespace GoldenIceRules {
inline constexpr int MaxStacks = 8; // 独立活车来源叠层上限，防止倍率溢出
inline constexpr float TrailDuration = 35; // 铺路刷新后的持久寿命，游戏秒
inline constexpr float LeftLimit = 25; // 非屋顶冰道左缘最小世界 X，px
inline constexpr float BodyPadding = 80; // 活车对其他鎏金冰车的左侧覆盖补量，px
inline constexpr float FirstAcceleration = 6, SecondAcceleration = 10, ThirdAcceleration = 14; // 无伤升级门槛，游戏秒
inline constexpr float AccelerationCap = 8; // 鎏金冰车经冰道放大后的自身加速上限

/** 中性不变；每层将加速项乘二、减速项除二，不把加成差值乘二。 */
inline float Amplify(float multiplier, int stacks) {
    float value = (std::max)(0.0f, multiplier);
    for (int i = 0; i < std::clamp(stacks, 0, MaxStacks); ++i) {
        if (value > 1) value *= 2;
        else if (value < 1) value *= .5f;
    }
    return value;
}
/** 从连续无伤时间取得升级阶段；承伤重置由实际/预测伤害入口分别负责。 */
inline int AccelerationStage(float seconds) {
    return seconds >= ThirdAcceleration ? 3 : seconds >= SecondAcceleration ? 2 : seconds >= FirstAcceleration ? 1 : 0;
}
/** 无伤能力先按冰道来源放大，再施加品种上限。 */
inline float Acceleration(float seconds, int stacks) {
    return (std::min)(AccelerationCap, Amplify(static_cast<float>(1 << AccelerationStage(seconds)), stacks));
}
}
