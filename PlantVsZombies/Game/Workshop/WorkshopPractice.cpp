#include "WorkshopPractice.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Workshop {
namespace {
constexpr int kPracticeRoute = 2; // 练习统一监视第 3 路，避免把工厂格行号误当战场路线
constexpr int kPracticeStockGoal = 5; // 库存练习的停止补货门槛，单位：颗普通豌豆
constexpr float kPracticeStepSeconds = 1.0f / 60; // 独立模拟步长，单位：练习秒
}
Practice::Practice() { Reset(0); }
const char* Practice::Name(int lesson) {
    constexpr std::array<const char*, 4> names{"给第3路补弹", "关机释放电力", "有敌时切换路线", "够用停机，缺弹再开"};
    return names.at(lesson);
}
int Practice::StepCount() const { return std::array<int, 4>{4, 3, 5, 7}[mLesson]; }

void Practice::Reset(int lesson) {
    mLesson = std::clamp(lesson, 0, 3); mStep = 0; mSelected = mSource = -1; mTool = Tool::Inspect;
    mRunning = false; mLowStockSet = false; mLessonTime = mPowerClock = 0; mProducedBeforeLow = 0; mEnemies = {}; mNotice.clear();
    mWorld.Reset(); mWorld.Remove(0); mWorld.Remove(MaxColumns);
    // 练习统一开放已用于课程的电路；此副本没有正式基地的布局或奖励进度。
    auto setup = mWorld.Save(); setup["state"]["completedWaves"] = 2; setup["state"]["energy"] = EnergyLimit;
    std::string error;
    if (!mWorld.Load(setup, error)) throw std::logic_error("Invalid workshop practice fixture: " + error);
    mWorld.Build(Grower, Machine::Grower, 0);
    if (mLesson != 0) {
        mWorld.Build(Conveyor, Machine::Belt, 0); mWorld.Build(Feeder, Machine::Feeder, 0);
        mWorld.SetRoute(Feeder, kPracticeRoute);
    }
    if (mLesson == 1) for (int i = 1; i <= 6; ++i) mWorld.Build(i, Machine::Grower, 1);
    if (mLesson == 2) {
        mWorld.Build(Sensor, Machine::EnemySensor, 0);
        mWorld.SetRoute(Feeder, 1, 0); mWorld.SetRoute(Feeder, 1, 1);
    }
    if (mLesson == 3) { mWorld.Build(Sensor, Machine::StockSensor, 0); mWorld.Build(Inverter, Machine::Not, 0); }
    mWorld.StartAssault(); // 没有真实战斗对象，开波仅允许独立产线和电路推进。
}

bool Practice::ClickCell(int index) {
    if (index < 0 || index / MaxColumns >= 3 || index % MaxColumns >= 8) return false;
    const auto type = mWorld.Cells()[index].type;
    if (mTool == Tool::WireA || mTool == Tool::WireB) {
        if (type == Machine::Empty) { mNotice = "接线先点来源机器，再点目标机器；空格不是端口。"; return false; }
        if (mSource < 0) { mSource = index; mNotice = "来源已选好，现在点接收信号的目标机器。"; return true; }
        const int port = mTool == Tool::WireB ? 1 : 0;
        if (!mWorld.Connect(mSource, index, port)) { mNotice = "这个目标不支持所选端口，或来源不能输出控制信号。"; return false; }
        mSource = -1; mSelected = index; mTool = Tool::Inspect; return true;
    }
    if (type != Machine::Empty) { mSelected = index; mTool = Tool::Inspect; return true; }
    const auto build = mTool == Tool::Belt ? Machine::Belt : mTool == Tool::Feeder ? Machine::Feeder : Machine::Empty;
    if (mLesson != 0 || build == Machine::Empty) { mNotice = "先选上方的机器卡片，再点发亮的空格。"; return false; }
    if ((build == Machine::Belt && index != Conveyor) || (build == Machine::Feeder && index != Feeder)) {
        mNotice = "这一课先把机器放在发亮格里，让物料线连续相邻。"; return false;
    }
    if (!mWorld.Build(index, build, 0)) return false;
    mSelected = index; mTool = Tool::Inspect; return true;
}

bool Practice::SetStock(int amount) {
    if (mLesson != 3 || amount < 0 || amount > 10) return false;
    auto setup = mWorld.Save(); setup["state"]["lanes"][kPracticeRoute]["ammo"] = amount;
    std::string error;
    if (!mWorld.Load(setup, error)) return false;
    mWorld.ObserveFrontline(mEnemies, {});
    if (mStep == 6 && amount == 0) { mLowStockSet = true; mProducedBeforeLow = mWorld.Produced(); }
    return true;
}

bool Practice::Act(const std::string& action, int value) {
    mNotice.clear(); bool ok = false;
    if (action == "inspect") { mTool = Tool::Inspect; mSource = -1; return true; }
    if (action == "belt" || action == "feeder") {
        if (mLesson != 0) return false;
        mTool = action == "belt" ? Tool::Belt : Tool::Feeder; mSource = -1; return true;
    }
    if (action == "wire_a" || action == "wire_b") {
        mTool = action == "wire_a" ? Tool::WireA : Tool::WireB; mSource = -1; return true;
    }
    if (action == "run") { mRunning = !mRunning; return true; }
    if (action == "tick") { Advance(1); return true; }
    if (action == "cell") ok = ClickCell(value);
    else if (action == "enemy" && mLesson == 2) { mEnemies[kPracticeRoute] = std::clamp(value, 0, 6); mWorld.ObserveFrontline(mEnemies, {}); ok = true; }
    else if (action == "stock") ok = SetStock(value);
    else if (mSelected >= 0) {
        const auto& c = mWorld.Cells()[mSelected];
        if (action == "route") ok = mWorld.SetRoute(mSelected, (c.route + 1) % Rows, 0);
        else if (action == "route2") ok = mWorld.SetRoute(mSelected, (c.route2 + 1) % Rows, 1);
        else if (action == "filter") ok = mWorld.SetFilter(mSelected, c.filter == -1 ? 0 : -1);
        else if (action == "setting") ok = mWorld.SetSetting(mSelected, std::clamp(c.setting + value, 1, 10));
        else if (action == "power") ok = mWorld.SetMachineRunning(mSelected, !c.manual);
        else if (action == "disconnect") { ok = mWorld.Connect(-1, mSelected, 0); mWorld.Connect(-1, mSelected, 1); }
    }
    if (ok) CheckStep();
    else if (mNotice.empty()) mNotice = "先点选需要配置的机器，再使用对应按钮。";
    return ok;
}

void Practice::CheckStep(bool simulated) {
    if (Complete()) return;
    const auto& cells = mWorld.Cells(); bool done = false;
    if (mLesson == 0) {
        if (mStep == 0) done = cells[Conveyor].type == Machine::Belt;
        if (mStep == 1) done = cells[Feeder].type == Machine::Feeder;
        if (mStep == 2) done = cells[Feeder].route == kPracticeRoute;
        if (mStep == 3) done = mWorld.Lanes()[kPracticeRoute].ammo > 0;
    } else if (mLesson == 1) {
        if (mStep == 0) done = mLessonTime >= .2f && mWorld.Power().speed < .99f;
        if (mStep == 1) { done = !cells[3].manual; if (done) mPowerClock = cells[Grower].clock; }
        if (mStep == 2) done = mWorld.MachinePower(3) == 0 && mWorld.Power().speed == 1 && cells[Grower].clock > mPowerClock;
    } else if (mLesson == 2) {
        if (mStep == 0) done = cells[Sensor].route == kPracticeRoute;
        if (mStep == 1) done = cells[Feeder].route2 == kPracticeRoute;
        if (mStep == 2) done = cells[Feeder].inputs[1] == Sensor && cells[Feeder].inputs[0] == -1;
        if (mStep == 3) done = mEnemies[kPracticeRoute] > 0 && mWorld.ActiveRoute(Feeder) == kPracticeRoute && mWorld.Lanes()[kPracticeRoute].ammo > 0;
        if (mStep == 4) done = mEnemies[kPracticeRoute] == 0 && mWorld.ActiveRoute(Feeder) == 1 && mWorld.Lanes()[1].ammo > 0;
    } else {
        if (mStep == 0) done = cells[Sensor].route == kPracticeRoute;
        if (mStep == 1) done = cells[Sensor].filter == 0;
        if (mStep == 2) done = cells[Sensor].setting == kPracticeStockGoal;
        if (mStep == 3) done = cells[Inverter].inputs[0] == Sensor;
        if (mStep == 4) done = cells[Grower].inputs[0] == Inverter;
        if (mStep == 5) done = mWorld.Lanes()[kPracticeRoute].ammo >= kPracticeStockGoal && cells[Sensor].signal && !cells[Inverter].signal
            && !mWorld.Enabled(Grower) && mWorld.MachinePower(Grower) == 0;
        if (mStep == 6) done = mLowStockSet && mWorld.Enabled(Grower) && mWorld.Produced() > mProducedBeforeLow;
    }
    const bool verification = (mLesson == 0 && mStep == 3) || (mLesson == 1 && mStep != 1)
        || (mLesson == 2 && mStep >= 3) || (mLesson == 3 && mStep >= 5);
    if (done && (!verification || simulated)) { ++mStep; mRunning = false; mSource = -1; mTool = Tool::Inspect; mNotice = Complete() ? "练习完成！可以改条件继续试，也可以重玩。" : "这一步完成，练习已暂停。按下一步提示继续。"; }
}

void Practice::Advance(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    const int startStep = mStep;
    while (seconds > .000001f) {
        const float dt = std::min(seconds, kPracticeStepSeconds); seconds -= dt;
        mLessonTime += dt; mWorld.ObserveFrontline(mEnemies, {}); mWorld.Tick(dt);
        if (mWorld.NextSpawnRow() >= 0) mWorld.CommitSpawn();
        CheckStep(true);
        if (mStep != startStep) break;
    }
}

std::string Practice::Instruction() const {
    if (Complete()) return "已完成。试着改变条件，观察电力、信号和弹药去向。";
    static const std::array<std::vector<const char*>, 4> text{{
        {"点运输带卡片，再点育豆机右边的亮格。", "点装填口卡片，再点运输带右边的亮格。", "点选装填口，把目标路线改成第3路。", "点运行练习，看豌豆走进第3路弹仓。"},
        {"点运行练习，先看电网过载让加工速度下降。", "点选亮框里的育豆机，关闭它的加工电源。", "再运行练习，确认停机释放电力，其他机器恢复全速。"},
        {"点选敌情器，把监视路线改成第3路。", "点选装填口，把方案2的目标改成第3路。", "选接 B口，先点敌情器，再点装填口。", "增加第3路僵尸数量，再运行，观察弹药转送第3路。", "把第3路僵尸数量减到0，再运行，看弹药回到第2路。"},
        {"点选库存器，把检测路线改成第3路。", "把库存器的弹种改成普通豌豆。", "把库存器门槛减到5颗。", "选接 A口，先点库存器，再点非门。", "再选接 A口，先点非门，再点育豆机。", "把第3路库存设成5颗，再运行，看工厂自动断电。", "把库存设成0颗，再运行，看工厂自动开机生产。"}
    }};
    return text[mLesson][mStep];
}
std::string Practice::Hint() const {
    if (mLesson == 0) return "运输带搬物料；装填口决定送哪一路。工厂所在行不等于前线路线。";
    if (mLesson == 1) return "本例7台育豆机需求14，电网容量12。关掉一台，释放2电力。";
    if (mLesson == 2 && mWorld.Cells()[Feeder].inputs[0] >= 0) return "您接到了装填口 A启停。点选装填口断开输入，再改接 B方案。";
    if (mLesson == 2) return "高信号=开：B选方案2；低信号=关：B选方案1。A控制送不送，B控制送哪路。";
    return "库存器：够5颗就开；非门把它反过来。A收到关信号，育豆机停机。";
}
int Practice::HighlightCell() const {
    if (Complete()) return -1;
    if (mLesson == 0) return mStep == 0 ? Conveyor : mStep <= 2 ? Feeder : -1;
    if (mLesson == 1) return mStep == 1 ? 3 : -1;
    if (mLesson == 2) return mStep == 0 ? Sensor : mStep == 1 ? Feeder : mStep == 2 ? (mSource < 0 ? Sensor : Feeder) : -1;
    if (mStep < 3) return Sensor;
    if (mStep == 3) return mSource < 0 ? Sensor : Inverter;
    if (mStep == 4) return mSource < 0 ? Inverter : Grower;
    return -1;
}
std::string Practice::HighlightControl() const {
    if (Complete()) return "next";
    if (mLesson == 0) return mStep == 0 ? "belt" : mStep == 1 ? "feeder" : mStep == 2 ? "route" : "run";
    if (mLesson == 1) return mStep == 1 ? "power" : "run";
    if (mLesson == 2) return mStep == 0 ? "route" : mStep == 1 ? "route2" : mStep == 2 ? "wire_b" : mStep == 3 ? "enemy_plus" : "enemy_minus";
    return mStep == 0 ? "route" : mStep == 1 ? "filter" : mStep == 2 ? "setting_minus" : mStep <= 4 ? "wire_a" : mStep == 5 ? "stock_full" : "stock_empty";
}
nlohmann::json Practice::Inspect() const {
    return {{"lesson", mLesson}, {"step", mStep}, {"complete", Complete()}, {"running", mRunning}, {"selected", mSelected},
        {"source", mSource}, {"tool", static_cast<int>(mTool)}, {"enemies", mEnemies}, {"notice", mNotice}, {"world", mWorld.Inspect()}};
}
}
