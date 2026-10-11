#pragma once

#include <nlohmann/json.hpp>

/** 玉米炮 AutoTest 陪练的纯公开状态灯控；不参与正式指挥官或实体规则。 */
namespace CobSparringLighting {
struct Decision {
    nlohmann::json action = nullptr;
    int sunCost = 0;
    int iceCost = 0;
};

/**
 * 每次观察最多提出一次普通种灯或切挡输入，不修改快照、钱包、冷却或实体。
 * availableSun/availableIce 是调用方扣除本次已预留支出后的余额；仍以公开钱包为上限。
 * allowPlant=false 保留灰烬等已有种植优先级，但不阻止活灯按当前雾势和实际燃料切挡。
 * action 为 null 表示等待下次观察；否则调用方须走 player_plant/player_set_plantern_gear
 * 正式门禁，并在继续规划同次支出前预留返回的两项费用及唯一种植机会。
 */
Decision ChooseAction(const nlohmann::json& state, int availableSun, int availableIce, bool allowPlant = true);
}
