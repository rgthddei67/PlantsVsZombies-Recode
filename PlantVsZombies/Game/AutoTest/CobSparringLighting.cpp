#include "CobSparringLighting.h"
#include "Game/Board/WeatherStationRules.h"
#include <algorithm>
#include <array>
#include <utility>

namespace {
using Json = nlohmann::json;
constexpr int kHighGearFuelTenths = 200; // 至少20油才用高挡；少量剩油沿用中挡节流
constexpr int kFirstSupportColumn = 4; // 0至3列保留两组双格炮位，补灯不占用其空缺
constexpr int kPreferredLampColumn = 5; // 前墙后一列，优先给前方可见区域提供照明

/** 只核对本方活体占位；宽炮的右半格也不能被当作空地。 */
bool Occupied(const Json& plants, int row, int column) {
    for (const auto& plant : plants) {
        if (plant.value("health", 0) <= 0 || plant.value("squished", false)
            || plant.at("row") != row) continue;
        const int left = plant.at("col");
        if (left == column || (plant.at("type") == "PLANT_COBCANNON" && left + 1 == column)) return true;
    }
    return false;
}
}

CobSparringLighting::Decision CobSparringLighting::ChooseAction(
    const nlohmann::json& state, int availableSun, int availableIce, bool allowPlant) {
    if (state.value("background", std::string{}) != "WEATHER_STATION"
        || !state.contains("plantern") || !state.at("plantern").value("supported", false)) return {};
    const auto& lamp = state.at("plantern");
    const auto& fog = state.at("weatherStation").at("controls").at(WeatherStationRules::FOG);
    if (lamp.value("active", false)) {
        // 预警可提前补灯，但尚未起雾不空耗燃料；在途油不等于已经到账的可用油。
        const int fuel = lamp.value("fuelTenths", 0);
        const int gear = fog.at("value").get<int>() > 0 && fuel > 0
            ? (fuel >= kHighGearFuelTenths ? 3 : 2) : 0;
        if (lamp.value("gearValue", 0) == gear) return {};
        return {{{"op", "player_set_plantern_gear"}, {"gear", gear}}, 0, 0};
    }
    if (!allowPlant || (fog.at("value").get<int>() <= 0 && fog.value("pending", -1) <= 0)) return {};
    const auto& ice = state.at("coldStorage");
    const auto& costs = ice.at("plantCosts");
    if (!costs.contains("PLANT_PLANTERN")) return {};
    const int iceCost = costs.at("PLANT_PLANTERN");
    const int sun = (std::min)(availableSun, state.at("sun").get<int>());
    const int stock = (std::min)(availableIce, ice.at("playerIce").get<int>());
    if (iceCost < 0 || stock < iceCost) return {};

    const int rows = state.at("rows"), columns = state.at("columns");
    const int middle = rows / 2;
    const std::array<std::pair<int, int>, 4> preferred{{
        {middle, kPreferredLampColumn}, {middle - 1, kPreferredLampColumn},
        {middle + 1, kPreferredLampColumn}, {middle, kFirstSupportColumn}
    }};
    for (const auto& card : state.at("cards")) {
        if (card.at("gameplayType") != "PLANT_PLANTERN" || !card.at("ready").get<bool>()) continue;
        const int sunCost = card.at("sunCost");
        if (sunCost < 0 || sun < sunCost) continue;
        const auto& legal = card.at("legalCells");
        const auto canUse = [&](int row, int column) {
            return row >= 0 && row < rows && column >= kFirstSupportColumn && column < columns
                && !Occupied(state.at("plants"), row, column)
                && std::find(legal.begin(), legal.end(), Json::array({row, column})) != legal.end();
        };
        const auto choose = [&](int row, int column) -> Decision {
            return {{{"op", "player_plant"}, {"slot", card.at("slot")}, {"row", row}, {"col", column}},
                sunCost, iceCost};
        };
        for (const auto& [row, column] : preferred) if (canUse(row, column)) return choose(row, column);
        // 首选被经济株等占用时仍可用真实合法空格；不铲任何活体，也不拆占双格炮位。
        for (const auto& cell : legal) {
            const int row = cell.at(0), column = cell.at(1);
            if (canUse(row, column)) return choose(row, column);
        }
    }
    return {};
}
