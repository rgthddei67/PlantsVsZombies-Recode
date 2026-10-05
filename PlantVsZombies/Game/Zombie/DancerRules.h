#pragma once
#include <array>

/** 舞王家族实体与新购/付费队列预测共用的出生生命；不包含召唤单位的额外血池。 */
namespace DancerRules {
inline constexpr int BodyHealth = 500; // 普通舞王本体生命；伴舞独立承伤
inline constexpr int EliteBodyHealth = 720; // 精英舞王本体生命
inline constexpr int BackupBodyHealth = 270; // 每只伴舞自己的本体生命
inline constexpr float EntrySeconds = 2.06f; // 月球步随机入场时长的均值，游戏秒；只供预测
inline constexpr float HoldSeconds = 1.2f; // 响指后定身及伴舞升起的游戏秒
inline constexpr float DanceSpeed = 1.2f; // 全队固定基础动画速度
inline constexpr float PointClip = 2.0f; // 响指动作的剪辑播放倍率
inline constexpr float SideDistance = 100; // 同行前后伴舞相对领队的横向距离，像素
inline constexpr float FrontMinimumX = 130; // 领队低于此对象 X 时不生成前伴舞，像素
inline constexpr float RefillLimitX = 700; // 领队低于此对象 X 后不再补召，像素
inline constexpr float BeatSeconds = 4.6f; // Board 23拍、每拍12逻辑步的循环，游戏秒
inline constexpr float RefillBeatSeconds = 2.4f; // 每圈第12拍才补召，游戏秒

/** 后台持有的舞步副本；followers 是已提交实体 ID，数值推演另维护内部槽位。 */
struct Forecast {
    enum class Phase { ENTRY, SNAP, HOLD, DANCE };
    bool leader = false, backup = false;
    Phase phase = Phase::ENTRY;
    float remaining = 0, snapSeconds = 0, walkSpeed = 0, stopHealth = 0, entrySpeed = 0;
    std::array<int,4> followers{};
};
}
