#include "Game/AutoTest/CobSparringLighting.h"
#include "Game/Board/WeatherStationRules.h"
#include <cstdlib>
#include <iostream>

namespace {
using Json = nlohmann::json;
using CobSparringLighting::ChooseAction;

/** 检查陪练输入契约；不把纯决策用例冒充游戏中的照明或交易结果。 */
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED cob sparring lighting: " << message << '\n';
        std::exit(1);
    }
}

/** 仅含公开天气、钱包、卡槽与本方占位的五行测试快照。 */
Json PublicState() {
    return {
        {"background", "WEATHER_STATION"}, {"rows", 5}, {"columns", 9}, {"sun", 100},
        {"coldStorage", {{"playerIce", 20}, {"plantCosts", {{"PLANT_PLANTERN", 5}}}}},
        {"weatherStation", {{"controls", Json::array({
            {{"value", 0}, {"pending", -1}}, {{"value", 2}, {"pending", -1}},
            {{"value", 0}, {"pending", -1}}
        })}}},
        {"plantern", {{"supported", true}, {"active", false}, {"fuelTenths", 0}, {"gearValue", 0}}},
        {"cards", Json::array({{{"gameplayType", "PLANT_PLANTERN"}, {"slot", 7}, {"ready", true},
            {"sunCost", 25}, {"legalCells", Json::array({{0, 4}, {2, 4}, {1, 5}, {2, 5}, {3, 5}, {4, 8}})}}})},
        {"plants", Json::array()}
    };
}

/** 只向公开本方实体表加入一个活体或遗留死者，不自动修改卡槽合法格。 */
Json Plant(const char* type, int row, int column, int health = 300, bool squished = false) {
    return {{"type", type}, {"row", row}, {"col", column}, {"health", health}, {"squished", squished}};
}

/** 断言空决策不预扣钱；失败重试必须等下一次真实观察。 */
void CheckNoAction(const Json& state, int sun = 100, int ice = 20, bool allowPlant = true) {
    const auto decision = ChooseAction(state, sun, ice, allowPlant);
    Check(decision.action.is_null() && decision.sunCost == 0 && decision.iceCost == 0,
        "unavailable operation leaves no action or reservation");
}

/** 活灯切挡只消费公开燃料信息，输出必为正常输入支持的整数挡位。 */
void CheckGear(const Json& state, int gear) {
    const auto decision = ChooseAction(state, 0, 0, false);
    Check(decision.action == Json({{"op", "player_set_plantern_gear"}, {"gear", gear}})
        && decision.sunCost == 0 && decision.iceCost == 0,
        "living lamp changes gear without requiring cash or another planting opportunity");
    Check(decision.action.at("gear").is_number_integer() && gear >= 0 && gear <= 3,
        "gear request remains in the finite legal input range");
}
}

/** 覆盖建灯资格、同次预算、死灯补种与不读取雾中敌方状态的边界。 */
int main() {
    {
        auto state = PublicState();
        const auto before = state;
        const auto decision = ChooseAction(state, 25, 5);
        Check(decision.action == Json({{"op", "player_plant"}, {"slot", 7}, {"row", 2}, {"col", 5}})
            && decision.sunCost == 25 && decision.iceCost == 5,
            "first choice uses the real ready slot and exactly reserves both prices");
        Check(state == before, "planning does not mutate the exported observation");
        CheckNoAction(state, 24, 5);
        CheckNoAction(state, 25, 4);
        CheckNoAction(state, 25, 5, false);
        state["sun"] = 24;
        CheckNoAction(state, 1000, 1000);
        state["sun"] = 100;
        state["coldStorage"]["playerIce"] = 4;
        CheckNoAction(state, 1000, 1000);
    }
    {
        auto state = PublicState();
        state["weatherStation"]["controls"][WeatherStationRules::FOG]["value"] = 0;
        CheckNoAction(state);
        state["weatherStation"]["controls"][WeatherStationRules::FOG]["pending"] = 0;
        CheckNoAction(state);
        state["weatherStation"]["controls"][WeatherStationRules::FOG]["pending"] = 3;
        Check(ChooseAction(state, 100, 20).action.value("op", "") == "player_plant",
            "positive pending public fog permits preparation before fog arrives");
        state["plantern"]["supported"] = false;
        CheckNoAction(state);
        state = PublicState();
        state["background"] = "DAY";
        CheckNoAction(state);
        state = PublicState();
        state.erase("plantern");
        CheckNoAction(state);
    }
    {
        for (int blocked = 0; blocked < 4; ++blocked) {
            auto state = PublicState();
            if (blocked == 0) state["cards"][0]["ready"] = false;
            if (blocked == 1) state["cards"] = Json::array();
            if (blocked == 2) state["cards"][0]["legalCells"] = Json::array();
            if (blocked == 3) state["coldStorage"]["plantCosts"].erase("PLANT_PLANTERN");
            CheckNoAction(state);
        }
        auto state = PublicState();
        state["cards"][0]["ready"] = false;
        auto replacementCard = state["cards"][0];
        replacementCard["ready"] = true;
        replacementCard["slot"] = 9;
        state["cards"].push_back(replacementCard);
        Check(ChooseAction(state, 100, 20).action.at("slot") == 9,
            "a separate ready slot may replace a cooling original card without resetting either");
    }
    {
        auto state = PublicState();
        state["plants"].push_back(Plant("PLANT_SUNSHROOM", 2, 5));
        auto decision = ChooseAction(state, 100, 20);
        Check(decision.action.at("row") == 1 && decision.action.at("col") == 5,
            "occupied central cell is preserved while the next protected legal cell is used");
        state["plants"].push_back(Plant("PLANT_TALLNUT", 1, 5));
        decision = ChooseAction(state, 100, 20);
        Check(decision.action.at("row") == 3 && decision.action.at("col") == 5,
            "the other central row precedes rear support cells");
        state["plants"].push_back(Plant("PLANT_TALLNUT", 3, 5));
        decision = ChooseAction(state, 100, 20);
        Check(decision.action.at("row") == 2 && decision.action.at("col") == 4,
            "rear central support cell is the fourth choice");
        state["plants"].push_back(Plant("PLANT_COBCANNON", 2, 3));
        decision = ChooseAction(state, 100, 20);
        Check(decision.action.at("row") == 0 && decision.action.at("col") == 4,
            "fallback uses a legal empty support cell and preserves a wide cannon's right half");
        state["cards"][0]["legalCells"] = Json::array({{2, 0}, {2, 1}, {2, 2}, {2, 3}});
        CheckNoAction(state);
        state["cards"][0]["legalCells"] = Json::array({{4, 8}});
        decision = ChooseAction(state, 100, 20);
        Check(decision.action.at("row") == 4 && decision.action.at("col") == 8,
            "non-preferred legal cells remain available when all preferred cells are absent");
    }
    {
        auto state = PublicState();
        state["plants"].push_back(Plant("PLANT_PLANTERN", 2, 5, 0));
        state["cards"][0]["ready"] = false;
        CheckNoAction(state);
        state["cards"][0]["ready"] = true;
        Check(ChooseAction(state, 100, 20).action.at("col") == 5,
            "dead lamp is rebuilt only after the real card becomes ready");
        state["plants"][0] = Plant("PLANT_PLANTERN", 2, 5, 300, true);
        Check(ChooseAction(state, 100, 20).action.at("row") == 2,
            "a squished lamp cannot permanently block its legal replacement cell");
        state["plantern"]["active"] = true;
        state["plantern"]["fuelTenths"] = 200;
        state["plantern"]["gearValue"] = 3;
        CheckNoAction(state);
    }
    {
        auto state = PublicState();
        state["plantern"]["active"] = true;
        state["plantern"]["fuelTenths"] = 200;
        CheckGear(state, 3);
        state["plantern"]["fuelTenths"] = 199;
        CheckGear(state, 2);
        state["plantern"]["fuelTenths"] = 1;
        CheckGear(state, 2);
        state["plantern"]["fuelTenths"] = 0;
        state["plantern"]["pendingFuelTenths"] = 1000;
        state["plantern"]["gearValue"] = 3;
        CheckGear(state, 0);
        state["plantern"]["fuelTenths"] = 300;
        state["weatherStation"]["controls"][WeatherStationRules::FOG]["value"] = 0;
        state["weatherStation"]["controls"][WeatherStationRules::FOG]["pending"] = 4;
        CheckGear(state, 0);
        state["plantern"]["gearValue"] = 0;
        CheckNoAction(state);
        state["plantern"]["supported"] = false;
        state["plantern"]["gearValue"] = 3;
        CheckNoAction(state);
    }
    {
        auto publicOnly = PublicState();
        auto withHidden = publicOnly;
        withHidden["zombies"] = Json::array({{{"bodyHealth", 100000}, {"row", 2}, {"xInt", 400},
            {"fogObscured", true}, {"type", "ZOMBIE_ICE_WORKER"}}});
        withHidden["coldStorage"]["searchScore"] = 1000000;
        withHidden["fog"] = {{"lockedActualIntensity", "DENSE"}};
        Check(ChooseAction(publicOnly, 100, 20).action == ChooseAction(withHidden, 100, 20).action,
            "construction is identical with no enemy array or with arbitrary hidden enemies and forecasts");
        for (auto* state : {&publicOnly, &withHidden}) {
            (*state)["plantern"]["active"] = true;
            (*state)["plantern"]["fuelTenths"] = 100;
        }
        Check(ChooseAction(publicOnly, 100, 20).action == ChooseAction(withHidden, 100, 20).action,
            "gear choice cannot depend on hidden enemy coordinates or forecast results");
    }
    std::cout << "Cob sparring lighting preserves public observation, wallets, cards, cells and finite gears\n";
}
