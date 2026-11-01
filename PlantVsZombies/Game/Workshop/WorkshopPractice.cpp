#include "WorkshopPractice.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Workshop {
namespace {
constexpr int kPracticeRoute = 2; // 练习统一监视第 3 路，避免把工厂格行号误当战场路线
constexpr int kPracticeStockGoal = 5; // 库存练习的停止补货门槛，单位：颗普通豌豆
constexpr float kStopVerificationSeconds = 4, kResumeVerificationSeconds = 6; // 两种库存情形的运行时长，练习秒
constexpr float kPracticeStepSeconds = 1.0f / 60; // 独立模拟步长，单位：练习秒
}
Practice::Practice() { Reset(0); }
const char* Practice::Name(int lesson) {
    constexpr std::array<const char*, LessonCount> names{"接电并给第3路补弹", "优先供电与断电", "有敌时优先供电", "够用停机，缺弹再开", "独立分组验收"};
    return names.at(lesson);
}
int Practice::StepCount() const { return std::array<int, LessonCount>{6, 5, 4, 7, 2}[mLesson]; }
int Practice::UnlockWave(int lesson) {
    if (lesson == 2) return Describe(Machine::EnemySensor).unlockWave;
    if (lesson == 3 || lesson == ChallengeLesson) return std::max(Describe(Machine::StockSensor).unlockWave, Describe(Machine::Not).unlockWave);
    return 0;
}

void Practice::Reset(int lesson) {
    mLesson = std::clamp(lesson, 0, LessonCount - 1); mStep = 0; mSelected = mSource = -1; mTool = Tool::Inspect;
    mRunning = mVerifying = false; mVerificationTime = 0; mLowStockSet = false; mLessonTime = mPowerClock = 0; mProducedBeforeLow = 0; mEnemies = {}; mNotice.clear();
    mWorld.Reset(); mWorld.Remove(0); mWorld.Remove(MaxColumns);
    // 练习统一开放已用于课程的电路；此副本没有正式基地的布局或奖励进度。
    auto setup = mWorld.Save(); setup["state"]["completedWaves"] = 2; setup["state"]["energy"] = EnergyLimit;
    std::string error;
    if (!mWorld.Load(setup, error)) throw std::logic_error("Invalid workshop practice fixture: " + error);
    mWorld.Build(Grower, Machine::Grower, 0);
    if (mLesson == 0) mWorld.ConnectPower(-1, PowerBox);
    else mWorld.ConnectPower(PowerBox, Grower);
    if (mLesson != 0) {
        mWorld.Build(Conveyor, Machine::Belt, 0); mWorld.Build(Feeder, Machine::Feeder, 0);
        mWorld.SetRoute(Feeder, kPracticeRoute);
    }
    if (mLesson == 1 || mLesson == 2) {
        mWorld.Build(BackgroundBox, Machine::PowerBox, 0); mWorld.ConnectPower(MainPowerSource, BackgroundBox);
        for (int i : {0, 1, 2, 3, 4, 6}) { mWorld.Build(i, Machine::Grower, 1); mWorld.ConnectPower(BackgroundBox, i); }
    }
    if (mLesson == 2) {
        mWorld.Build(Sensor, Machine::EnemySensor, 0);

    }
    if (mLesson == 3 || IsChallenge()) { mWorld.Build(Sensor, Machine::StockSensor, 0); mWorld.Build(Inverter, Machine::Not, 0); }
    if (IsChallenge()) {
        mWorld.Build(BackgroundBox, Machine::PowerBox, 0);
        mWorld.Build(BackgroundGrower, Machine::Grower, 0);
        mWorld.ConnectPower(PowerBox, BackgroundGrower);
        mWorld.Build(BackgroundGrower + 1, Machine::Belt, 0);
        mWorld.Build(BackgroundGrower + 2, Machine::Feeder, 0);
        mWorld.SetRoute(BackgroundGrower + 2, 1);
    }
    mWorld.StartAssault(); // 没有真实战斗对象，开波仅允许独立产线和电路推进。
}

bool Practice::ClickCell(int index) {
    if (index < 0 || index / MaxColumns >= 3 || index % MaxColumns >= 8) return false;
    const auto type = mWorld.Cells()[index].type;
    if (mTool == Tool::Power) {
        if (type == Machine::PowerBox) {
            if (mSource == MainPowerSource && !mWorld.ConnectPower(mSource, index)) return false;
            mSource = mSelected = index; return true;
        }
        if (mSource < 0 || !mWorld.ConnectPower(mSource, index)) { mNotice = "先从总站接配电箱，再由配电箱接生产机。"; return false; }
        mSelected = index; return true;
    }
    if (mTool == Tool::WireA || mTool == Tool::WireB) {
        if (type == Machine::Empty) { mNotice = "接线先点来源机器，再点目标机器；空格不是端口。"; return false; }
        if (mSource < 0) { mSource = index; mNotice = "来源已选好，现在点接收信号的目标机器。"; return true; }
        const int port = mTool == Tool::WireB ? 1 : 0;
        if (!mWorld.Connect(mSource, index, port)) { mNotice = "这个目标不支持所选端口，或来源不能输出控制信号。"; return false; }
        mSource = -1; mSelected = index; mTool = Tool::Inspect; return true;
    }
    if (type != Machine::Empty) { mSelected = index; mTool = Tool::Inspect; return true; }
    const auto build = mTool == Tool::Belt ? Machine::Belt : mTool == Tool::Feeder ? Machine::Feeder : Machine::Empty;
    if (mLesson != 0 || build == Machine::Empty) { mNotice = IsChallenge() ? "本课使用现有机器，请调整分组、配置与接线。" : "先选上方的机器卡片，再点发亮的空格。"; return false; }
    if ((build == Machine::Belt && index != Conveyor) || (build == Machine::Feeder && index != Feeder)) {
        mNotice = "这一课先把机器放在发亮格里，让物料线连续相邻。"; return false;
    }
    if (!mWorld.Build(index, build, 0)) return false;
    mSelected = index; mTool = Tool::Inspect; return true;
}

bool Practice::SetStock(int amount) {
    if ((mLesson != 3 && !IsChallenge()) || amount < 0 || amount > 10) return false;
    auto setup = mWorld.Save(); setup["state"]["lanes"][kPracticeRoute]["ammo"] = amount;
    std::string error;
    if (!mWorld.Load(setup, error)) return false;
    mWorld.ObserveFrontline(mEnemies, {});
    if (mStep == 6 && amount == 0) { mLowStockSet = true; mProducedBeforeLow = mWorld.Produced(); }
    return true;
}

bool Practice::Act(const std::string& action, int value) {
    mNotice.clear(); bool ok = false;
    if (mVerifying && action != "run" && action != "tick" && action != "inspect" && action != "cell") {
        mNotice = "正在检验真实运行结果；先等检验结束，或重做本课再修改。"; return false;
    }
    if (action == "verify") return StartVerification();
    if (action == "station") { mTool = Tool::Power; mSource = MainPowerSource; return true; }
    if (action == "wire_power") { mTool = Tool::Power; mSource = -1; return true; }
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
        else if (action == "priority") ok = mWorld.SetPlan(mSelected, 1 - c.plan);
        else if (action == "power") ok = mWorld.SetMachineRunning(mSelected, !c.manual);
        else if (action == "disconnect") { ok = mWorld.Connect(-1, mSelected, 0); mWorld.Connect(-1, mSelected, 1); }
    }
    if (ok) CheckStep();
    else if (mNotice.empty()) mNotice = "先点选需要配置的机器，再使用对应按钮。";
    return ok;
}

void Practice::CheckStep(bool simulated) {
    if (Complete() || IsChallenge()) return;
    const auto& cells = mWorld.Cells(); bool done = false;
    if (mLesson == 0) {
        if (mStep == 0) done = cells[PowerBox].powerSource == MainPowerSource;
        if (mStep == 1) done = cells[Grower].powerSource == PowerBox;
        if (mStep == 2) done = cells[Conveyor].type == Machine::Belt;
        if (mStep == 3) done = cells[Feeder].type == Machine::Feeder;
        if (mStep == 4) done = cells[Feeder].route == kPracticeRoute;
        if (mStep == 5) done = mWorld.Lanes()[kPracticeRoute].ammo > 0;
    } else if (mLesson == 1) {
        if (mStep == 0) done = mLessonTime >= .2f && mWorld.MachineSpeed(Grower) < .99f;
        if (mStep == 1) done = mWorld.ActivePlan(PowerBox) == 1;
        if (mStep == 2) done = mWorld.MachineSpeed(Grower) == 1 && mWorld.MachineSpeed(1) < .99f;
        if (mStep == 3) { done = !cells[BackgroundBox].manual; if (done) mPowerClock = cells[Grower].clock; }
        if (mStep == 4) done = mWorld.MachinePower(1) == 0 && mWorld.Power().speed == 1 && cells[Grower].clock > mPowerClock;
    } else if (mLesson == 2) {
        if (mStep == 0) done = cells[Sensor].route == kPracticeRoute;
        if (mStep == 1) done = cells[PowerBox].inputs[1] == Sensor && cells[PowerBox].inputs[0] == -1;
        if (mStep == 2) done = mEnemies[kPracticeRoute] > 0 && mWorld.ActivePlan(PowerBox) == 1 && mWorld.MachineSpeed(Grower) == 1;
        if (mStep == 3) done = mEnemies[kPracticeRoute] == 0 && mWorld.ActivePlan(PowerBox) == 0 && mWorld.MachineSpeed(Grower) < .99f;
    } else {
        if (mStep == 0) done = cells[Sensor].route == kPracticeRoute;
        if (mStep == 1) done = cells[Sensor].filter == 0;
        if (mStep == 2) done = cells[Sensor].setting == kPracticeStockGoal;
        if (mStep == 3) done = cells[Inverter].inputs[0] == Sensor;
        if (mStep == 4) done = cells[PowerBox].inputs[0] == Inverter;
        if (mStep == 5) done = mWorld.Lanes()[kPracticeRoute].ammo >= kPracticeStockGoal && cells[Sensor].signal && !cells[Inverter].signal
            && !mWorld.Enabled(PowerBox) && mWorld.MachinePower(Grower) == 0;
        if (mStep == 6) done = mLowStockSet && mWorld.Enabled(PowerBox) && mWorld.Produced() > mProducedBeforeLow;
    }
    const bool verification = (mLesson == 0 && mStep == 5) || (mLesson == 1 && mStep % 2 == 0)
        || (mLesson == 2 && mStep >= 2) || (mLesson == 3 && mStep >= 5);
    if (done && (!verification || simulated)) { ++mStep; mRunning = false; mSource = -1; mTool = Tool::Inspect; mNotice = Complete() ? "练习完成！可以改条件继续试，也可以重玩。" : "这一步完成，练习已暂停。按下一步提示继续。"; }
}

bool Practice::StartVerification() {
    if (!IsChallenge() || mVerifying) return false;
    auto setup = mWorld.Save(); auto& state = setup["state"];
    // 清理练习中的待发缓存，确保恢复项必须真的重新生产，而非搬运之前囤下的弹药。
    for (int index : {Grower, Conveyor, Feeder, BackgroundGrower, BackgroundGrower + 1, BackgroundGrower + 2}) {
        state["cells"][index]["stock"] = 0; state["cells"][index]["clock"] = 0;
    }
    state["lanes"][kPracticeRoute]["ammo"] = kPracticeStockGoal;
    state["lanes"][1]["ammo"] = 0; state["energy"] = EnergyLimit;
    std::string error;
    if (!mWorld.Load(setup, error)) { mNotice = "无法准备本次练习检验，请重做本课。"; return false; }
    mStep = 0; mVerificationTime = 0; mVerifying = mRunning = true;
    mSelected = mSource = -1; mTool = Tool::Inspect;
    mVerificationTargetProduced = mWorld.Cells()[Grower].producedItems[0];
    mVerificationBackgroundProduced = mWorld.Cells()[BackgroundGrower].producedItems[0];
    mNotice = "检验中会依次设置5颗、4颗；只改变练习库存，不影响正式基地。";
    return true;
}

void Practice::CheckVerification(float dt) {
    if (!mVerifying) return;
    mVerificationTime += dt;
    if (mVerificationTime < (mStep == 0 ? kStopVerificationSeconds : kResumeVerificationSeconds)) return;
    const auto& target = mWorld.Cells()[Grower]; const auto& background = mWorld.Cells()[BackgroundGrower];
    const bool backgroundRuns = mWorld.MachinePower(BackgroundGrower) > 0
        && background.producedItems[0] > mVerificationBackgroundProduced && mWorld.Lanes()[1].ammo > 0;
    std::string failure;
    if (!backgroundRuns) failure = "第2路没有继续生产供货，不能为了停第3路而一起断供。";
    else if (mStep == 0 && (target.producedItems[0] > mVerificationTargetProduced || target.powerSource < 0 || !target.manual || mWorld.Enabled(target.powerSource)
        || mWorld.MachinePower(Grower) > 0)) failure = "第3路已够5颗，但没有通过支路自动停机。";
    else if (mStep == 1 && (target.producedItems[0] <= mVerificationTargetProduced
        || mWorld.Lanes()[kPracticeRoute].ammo < kPracticeStockGoal)) failure = "第3路降至4颗后，没有重新生产并补到前线。";
    if (!failure.empty()) {
        mNotice = "未通过：" + failure; mVerifying = mRunning = false; mStep = 0; return;
    }
    if (mStep == 0) {
        mVerificationTargetProduced = target.producedItems[0];
        mVerificationBackgroundProduced = background.producedItems[0];
        SetStock(kPracticeStockGoal - 1);
        mStep = 1; mVerificationTime = 0; mNotice = "够弹停机已通过；现在检验低于门槛后的自动恢复。";
    } else {
        mStep = StepCount(); mVerifying = mRunning = false;
        mNotice = "独立验收通过：够弹停机、缺弹重启，而且没有拖停另一条产线。";
    }
}

void Practice::Advance(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    const int startStep = mStep; const bool verifying = mVerifying;
    while (seconds > .000001f) {
        const float dt = std::min(seconds, kPracticeStepSeconds); seconds -= dt;
        mLessonTime += dt; mWorld.ObserveFrontline(mEnemies, {}); mWorld.Tick(dt);
        if (mWorld.NextSpawnRow() >= 0) mWorld.CommitSpawn();
        if (IsChallenge()) CheckVerification(dt);
        else CheckStep(true);
        if (mStep != startStep || (verifying && !mVerifying)) break;
    }
}

std::string Practice::Instruction() const {
    if (IsChallenge()) return Complete() ? "验收通过：目标产线能停能开，另一条产线始终正常供货。"
        : "第3路普通弹够5颗就停、少于5颗再补；第2路必须持续供货。";
    if (Complete()) return mLesson == 0 ? "本路已送通！正式开波前，也要给其余四路安排供弹。"
        : mLesson == 1 ? "已完成。试着开关配电箱，观察各支路加工速度。"
        : "已完成。试着改变条件，观察电力、信号和弹药去向。";
    static const std::array<std::vector<const char*>, 4> text{{
        {"点总站，再点左下亮框里的配电箱。", "点接电，再点配电箱和育豆机。", "点运输带卡片，再点育豆机右边的亮格。", "点装填口卡片，再点运输带右边的亮格。", "点选装填口，把目标路线改成第3路。", "点运行练习，看豌豆走进第3路弹仓。"},
        {"点运行练习，看同级支路缺电时同比减速。", "点左下配电箱，把它改成优先供电。", "推进1秒，确认这条支路全速，其他支路减速。", "点旁边的配电箱，关闭这组背景负载。", "再推进1秒，确认剩余产线恢复全速。"},
        {"点选敌情器，把监视路线改成第3路。", "选接 B口，先点敌情器，再点左下配电箱。", "增加第3路僵尸，再运行，观察本支路优先拿电。", "把僵尸减到0，再运行，看本支路恢复普通供电。"},
        {"点选库存器，把检测路线改成第3路。", "把库存器的物料改成普通豌豆。", "把库存器门槛减到5颗。", "选接 A口，先点库存器，再点非门。", "再选接 A口，先点非门，再点左下配电箱。", "把第3路库存设成5颗，再运行，看整条支路断电。", "把库存设成0颗，再运行，看支路重新开机生产。"}
    }};
    return text[mLesson][mStep];
}
std::string Practice::Hint() const {
    if (IsChallenge()) return mVerifying ? (mStep == 0 ? "检验1：第3路已放5颗。观察目标是否停产，第2路是否仍出货。"
        : "检验2：第3路降至4颗。观察目标是否重新生产，第2路是否仍出货。")
        : "自行分组、配置与接线，无操作高亮。点开始检验，系统只改变练习库存并运行检查；可回前课重看。";
    if (mLesson == 0) return "黄色是供电线，运输带搬物料。配电箱必须接总站，机器必须接箱；装填口不耗电。正式基地准备期停机，开波后才产弹。";
    if (mLesson == 1) return "左箱只供育豆主线，右箱供6台背景机器。优先箱先拿电；同级同比分配，产速为供电比的平方。";
    if (mLesson == 2 && mWorld.Cells()[PowerBox].inputs[0] >= 0) return "您接到了配电箱A通断口；断开输入，改接B优先级口。";
    if (mLesson == 2) return "B开（高）选优先，B关（低）选普通；A控制整组通断。信号线不会搬运电力或弹药。";
    return "库存器够5颗就开，非门反转后让配电箱A关闭。缺弹时重新供电，机器不用再接A线。";
}
int Practice::HighlightCell() const {
    if (Complete() || IsChallenge()) return -1;
    if (mLesson == 0) return mStep == 0 ? PowerBox : mStep == 1 ? (mSource < 0 ? PowerBox : Grower) : mStep == 2 ? Conveyor : mStep <= 4 ? Feeder : -1;
    if (mLesson == 1) return mStep == 1 ? PowerBox : mStep == 3 ? BackgroundBox : -1;
    if (mLesson == 2) return mStep == 0 ? Sensor : mStep == 1 ? (mSource < 0 ? Sensor : PowerBox) : -1;
    if (mStep < 3) return Sensor;
    if (mStep == 3) return mSource < 0 ? Sensor : Inverter;
    if (mStep == 4) return mSource < 0 ? Inverter : PowerBox;
    return -1;
}
std::string Practice::HighlightControl() const {
    if (IsChallenge()) return {};
    if (Complete()) return "next";
    if (mLesson == 0) return std::array<const char*, 6>{"station", "wire_power", "belt", "feeder", "route", "run"}[mStep];
    if (mLesson == 1) return mStep == 1 ? "priority" : mStep == 3 ? "power" : "tick";
    if (mLesson == 2) return mStep == 0 ? "route" : mStep == 1 ? "wire_b" : mStep == 2 ? "enemy_plus" : "enemy_minus";
    return mStep == 0 ? "route" : mStep == 1 ? "filter" : mStep == 2 ? "setting_minus" : mStep <= 4 ? "wire_a" : mStep == 5 ? "stock_full" : "stock_empty";
}
nlohmann::json Practice::Inspect() const {
    return {{"lesson", mLesson}, {"step", mStep}, {"complete", Complete()}, {"running", mRunning}, {"selected", mSelected},
        {"verifying", mVerifying}, {"signalPaused", !mRunning}, {"highlightCell", HighlightCell()}, {"highlightControl", HighlightControl()},
        {"source", mSource}, {"tool", static_cast<int>(mTool)}, {"enemies", mEnemies}, {"notice", mNotice}, {"world", mWorld.Inspect()}};
}
}
