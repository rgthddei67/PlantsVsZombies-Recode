#pragma once

#include "Zombie/ZombieType.h"
#include "Plant/PlantType.h"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
#include <unordered_map>

enum class Background;

/** 我是僵尸的资源配置；注册表提供卡牌身份，配置只提供价格、冷却及关卡内容。 */
namespace IZombie {
inline constexpr int FirstLevel = 3000; // 解谜关卡独立编号起点
inline constexpr int LastLevel = 3099; // 为同模式后续关卡保留的编号上界
inline constexpr float BrainChewSeconds = 0.7f; // 原版脑子 70 个百分之一秒啃食进度
inline constexpr int SunflowerYield = 8; // 每株普通向日葵整局最多释放的普通阳光颗数

struct CardDefinition {
    ZombieType type;
    std::string name;
    int cost = 0;
    float cooldown = 0; // 购买成功后的基础游戏秒数，默认无冷却
};
struct Placement { PlantType type; int row; int column; };
struct LevelDefinition {
    int level = FirstLevel;
    std::string name;
    Background background{};
    int initialSun = 150;
    int firstSpawnColumn = 4; // 从零起的第一列投放格，蹦极只接受左侧格
    std::vector<ZombieType> cards;
    std::vector<Placement> plants;
};

bool IsLevel(int level);
/** 启动时校验全部注册僵尸的配置；失败中止启动，不能以免费卡掩盖缺失价格。 */
bool LoadDefinitions();
const std::vector<CardDefinition>& Cards();
const CardDefinition& Card(ZombieType type);
const std::vector<LevelDefinition>& Levels();
const LevelDefinition& Level(int level);

/** Board 独占的持久状态；界面不持有第二份冷却或购买账本。 */
struct State {
    bool initialized = false;
    int firstSpawnColumn = 4;
    std::vector<ZombieType> cards;
    std::vector<float> cooldowns;
    std::vector<float> brains;
    std::unordered_map<int, int> sunflowerPaid;
    float defeatDelay = 0; // 无兵且无法继续购买后等待收尾演出的游戏秒数
};
}
