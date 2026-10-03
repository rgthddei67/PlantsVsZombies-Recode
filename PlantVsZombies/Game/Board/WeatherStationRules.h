#pragma once
#include <algorithm>
#include <array>

/** 气象站付费设备的共享数值规则；Board与后台预测各持自己的状态副本。 */
namespace WeatherStationRules {
inline constexpr float WarningSeconds = 8; // 付款至切换的游戏秒
inline constexpr float ProtectionSeconds = 30; // 切换后禁止双方重设的游戏秒
inline constexpr float FogMoveMultiplier = .5f; // 雾中敌方独立移速倍率，不经过黄色冰道
inline constexpr std::array<int,9> PlayerIce{250,275,300,325,350,375,400,425,450}; // 九关初始冰块
inline constexpr std::array<int,9> EnemyIce{500,650,800,950,1100,1250,1400,1600,1800}; // 难度1敌方初始冰块
enum Device { RAIN, FOG, CHARGE, COUNT };
struct Control {
    int value = 0, pending = -1;
    float warning = 0, protection = 0;
    bool player = false;
};
struct State {
    std::array<Control,COUNT> controls{};
    unsigned revision = 0;
    unsigned enemyUsedMask = 0; // 本局敌方实际使用过的设备，供首次反制教学
};
inline int Count(int device) { return device == RAIN ? 4 : device == FOG ? 5 : device == CHARGE ? 2 : 0; }
inline int Cost(int device, int value) {
    if (value < 0 || value >= Count(device)) return 0;
    constexpr int rain[]{30,20,40,60}; // 各目标雨势冰价
    constexpr int fog[]{40,20,30,40,60}; // 各目标雾势冰价
    return device == RAIN ? rain[value] : device == FOG ? fog[value] : 40;
}
inline bool CanChange(const Control& control, int device, int value) {
    return Cost(device,value)>0 && control.value!=value && control.pending<0 && control.protection<=0;
}
/** 消费一个时间段；跨越切换边沿时只把剩余时间计入保护期。 */
inline bool Advance(Control& control, float seconds) {
    seconds=(std::max)(0.0f,seconds);
    if (control.pending>=0) {
        const float used=(std::min)(seconds,control.warning);
        control.warning-=used; seconds-=used;
        if (control.warning>0) return false;
        control.value=control.pending; control.pending=-1;
        control.protection=(std::max)(0.0f,ProtectionSeconds-seconds);
        return true;
    }
    control.protection=(std::max)(0.0f,control.protection-seconds);
    return false;
}
}
