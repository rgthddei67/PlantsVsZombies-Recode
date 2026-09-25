#pragma once

#include "Plant/PlantType.h"
#include <array>
#include <algorithm>

namespace MiniGame {
inline constexpr int LAST_SAVINGS_LEVEL = 2000; // 小游戏独立关卡号，不占用冒险和无尽编号
inline constexpr int BRAWL_LEVEL = 2001; // 复用第十大关冷藏站经济与地图的独立试玩关
inline constexpr int INITIAL_SUN = 3000; // 最后的家底整局唯一阳光预算
inline constexpr int BRAWL_INITIAL_SUN = 3000; // 大混战开局阳光，之后沿用冷藏站正常经济
inline constexpr int BRAWL_ENEMY_ICE = 850; // 大混战敌方固定初始冰块，不按难度放大
inline constexpr int WAVES = 10; // 清理第十波全部敌人后通关
inline constexpr float PREPARATION_SECONDS = 60.0f; // 首波前可自由布阵的游戏秒数
inline constexpr float WAVE_SECONDS = 35.0f; // 后续波次最长间隔，单位：游戏秒
inline constexpr const char* NAME = u8"最后的家底";
inline constexpr const char* BRAWL_NAME = u8"大混战";
inline constexpr std::array<PlantType, 7> CARDS = {
    PlantType::PLANT_PEASHOOTER, PlantType::PLANT_SNOWPEA,
    PlantType::PLANT_REPEATER, PlantType::PLANT_WALLNUT,
    PlantType::PLANT_POTATOMINE, PlantType::PLANT_CHERRYBOMB,
    PlantType::PLANT_SQUASH
};

inline bool IsLastSavings(int level) { return level == LAST_SAVINGS_LEVEL; }
inline bool IsBrawl(int level) { return level == BRAWL_LEVEL; }
inline bool IsMiniGame(int level) { return IsLastSavings(level) || IsBrawl(level); }
inline const char* GetName(int level) { return IsBrawl(level) ? BRAWL_NAME : NAME; }
/** 最后的家底只允许固定七张牌；大混战不限制其临时开放的植物卡池。 */
inline bool AllowsPlant(int level, PlantType type) {
    return !IsLastSavings(level) || std::find(CARDS.begin(), CARDS.end(), type) != CARDS.end();
}
}
