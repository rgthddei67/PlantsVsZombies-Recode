#pragma once
#include "ZombieType.h"

/** 已有扶梯动作的纯数值契约；预测不拥有实体、动画或Board共享梯。 */
namespace LadderRules {
inline constexpr float PlacementClipSpeed=2; // 放梯轨以资源12FPS的两倍播放，沿用正式24FPS时序
inline constexpr float ClimbSpeed=80; // 攀升速度，像素/内部行动秒
inline constexpr float FallSpeed=100; // 落地速度，像素/内部行动秒
inline constexpr float Height=90; // 梯顶相对地面的高度，像素
inline constexpr float SlowRootThreshold=16; // 正式mSpeed低于此值时，上升期间额外向前移动
inline constexpr float HorizontalBoost=50; // 慢根运动单位的上升横移，像素/内部行动秒
inline constexpr int MagnetCells=2; // 磁力菇没有可吸装备时，按两格Chebyshev距离拆场景梯

/** 永远不走普通植物接触路径的品种不能凭画像获得共享攀梯。 */
inline bool HasOrdinaryPlantContact(ZombieType type) {
    switch(type) {
    case ZombieType::ZOMBIE_ZAMBONI: case ZombieType::ZOMBIE_GILDED_ZAMBONI:
    case ZombieType::ZOMBIE_CATAPULT: case ZombieType::ZOMBIE_ELITE_CATAPULT:
    case ZombieType::ZOMBIE_GARGANTUAR: case ZombieType::ZOMBIE_REDEYE_GARGANTUAR:
    case ZombieType::ZOMBIE_DIGGER: case ZombieType::ZOMBIE_ELITE_DIGGER:
    case ZombieType::ZOMBIE_SNOW_BURROW: case ZombieType::ZOMBIE_BUNGEE:
    case ZombieType::ZOMBIE_BOSS: return false;
    default: return true;
    }
}

/** 未展开的骑乘/持杆阶段保守不借梯；气球落地由其已有独立阶段资格处理。 */
inline bool CanClimbAtBirth(ZombieType type) {
    if(!HasOrdinaryPlantContact(type)) return false;
    switch(type) {
    case ZombieType::ZOMBIE_POGO: case ZombieType::ZOMBIE_ELITE_POGO:
    case ZombieType::ZOMBIE_BOBSLED_TEAM:
    case ZombieType::ZOMBIE_DOLPHIN_RIDER: case ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER: return false;
    default: return true;
    }
}

struct Builder {
    enum class Phase { CARRYING, PLACING, NORMAL };
    bool present=false, retain=false,canPlace=true;
    Phase phase=Phase::CARRYING;
    int row=-1,column=-1;
    float placementSeconds=0,remaining=0,normalSpeed=0,carryingSpeed=0,stopHealth=0,abilityRate=1;
};
struct Climb {
    enum class Phase { NONE, CLIMBING, FALLING };
    bool eligible=false;
    Phase phase=Phase::NONE;
    float altitude=0,horizontalBoost=0;
    int usedColumn=-1;
};
struct Cell { int row=0,column=0; };
}
