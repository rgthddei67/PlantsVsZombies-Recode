#pragma once
#include <algorithm>

/** 小丑实体与后台预测共用的事务参数；随机出生用期望值，活体保留实际余时。 */
namespace JackBoxRules {
inline constexpr int PopTicksMin=450, PopTicksMax=749; // 普通开盒倒计时厘秒的随机范围
inline constexpr int FastPopRollMax=19; // 随机整数0..19，取0时倒计时缩为三分之一
inline constexpr float PopClip=28.0f/12.0f; // 开盒原片段的有效播放倍率
inline constexpr int ExplosionFrame=66; // 已获主人批准的普通小丑爆炸帧，不新增动画事件
inline constexpr float PlantRadius=90, ZombieRadius=115; // 正式圆形爆区半径，像素
inline constexpr int ZombieDamage=1800; // 对敌方僵尸的普通小丑爆炸伤害；植物走直接击杀
inline constexpr float ThrowMin=5, ThrowMax=7, Flight=.75f, Retry=.5f; // 精英投掷间隔、飞行与无目标重试，游戏秒
inline constexpr float BoxRadius=100; // 精英盒落地爆区半径，像素
inline constexpr int BoxDamage=50; // 精英盒落地基础伤害
inline constexpr float BacklineValue=1.2f, ProducerValue=300; // 贪心择点后排倍率及产阳光株未来价值

/** 普通新购随机倒计时的期望值；不消费实际随机源，也不冒充活体余时。 */
inline float BirthPopSeconds(float velocity) {
    return (PopTicksMin+PopTicksMax)*.5f/(std::max)(.01f,velocity)*2/100
        *(1-2.0f/(3*(FastPopRollMax+1)));
}
/** 动作状态不含实体/资源指针；已经飞出的盒子由独立预测队列持有。 */
struct Forecast {
    enum class Phase { RUNNING, POPPING, DISARMED };
    bool present=false, elite=false, flightsCancelled=false; // 灰烬直接移除来源时，成员持有的飞行盒同时消失
    Phase phase=Phase::RUNNING;
    float remaining=0, release=0, releaseRemaining=0, animationBase=1, disarmedSpeed=0;
    float stopHealth=0;
};
}
