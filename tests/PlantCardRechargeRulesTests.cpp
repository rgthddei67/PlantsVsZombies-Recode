#include "Game/PlantCardRechargeRules.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
using PlantCardRechargeRules::GetMultiplier;
constexpr double kTolerance = 0.000001; // 纯数值倍率/游戏秒断言容差

/** 核对规则边界与玩家可见时间，不依赖图形、实体或预测框架。 */
void Check(bool okay, const char* message)
{
    if (!okay) {
        std::cerr << "FAILED plant card recharge: " << message << '\n';
        std::exit(1);
    }
}

bool Near(double left, double right)
{
    return std::abs(left - right) < kTolerance;
}
}

/** 战前支援的单卡例外必须不外溢到其他卡片，也不吞掉独立生存词条。 */
int main()
{
    for (int value = 0; value < static_cast<int>(PlantType::NUM_PLANT_TYPES); ++value) {
        const auto type = static_cast<PlantType>(value);
        Check(Near(GetMultiplier(type, false, 1.0), 1.0), "support-off preserves every card's normal clock");
        const double supported = GetMultiplier(type, true, 1.0);
        Check(Near(supported, type == PlantType::PLANT_COBCANNON ? 1.0 : 2.2),
            "opening support excludes exactly the cannon gameplay type");
        Check(Near(GetMultiplier(type, false, 2.2), 2.2),
            "ten survival recharge stacks remain effective even on the cannon");
    }
    Check(Near(50.0 / GetMultiplier(PlantType::PLANT_COBCANNON, true, 1.0), 50.0),
        "a successfully used cannon card needs fifty game seconds with opening support");
    Check(Near(7.5 / GetMultiplier(PlantType::PLANT_KERNELPULT, true, 1.0), 3.409090909090909),
        "the base-building kernel card still needs about 3.41 game seconds");
    Check(Near(7.5 / GetMultiplier(PlantType::PLANT_PEASHOOTER, true, 1.0), 3.409090909090909),
        "ordinary cards retain the full support benefit");
    Check(Near(7.5 / GetMultiplier(PlantType::PLANT_KERNELPULT, false, 1.0), 7.5),
        "a normal kernel card keeps its original 7.5 seconds");
    Check(Near(50.0 / GetMultiplier(PlantType::PLANT_COBCANNON, false, 2.2), 22.727272727272727),
        "the independent survival perk still accelerates the cannon card");
    Check(Near(GetMultiplier(PlantType::PLANT_COBCANNON, true, 1.6), 1.6)
        && Near(GetMultiplier(PlantType::PLANT_KERNELPULT, true, 1.6), 3.52),
        "excluding opening support never removes or duplicates a separate survival factor");
    Check(Near(GetMultiplier(PlantType::NUM_PLANT_TYPES, true, 1.0), 2.2),
        "the legacy generic diagnostic keeps its ordinary-card support rate");
    std::cout << "PASS: opening support excludes only cannon cards and preserves all survival factors\n";
}
