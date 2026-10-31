#include "Workshop.h"
#include "WorkshopRoster.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace Workshop {
namespace {
constexpr float kEnergyLimit = 10000.0f; // 全基地储能容量，单位：阳光
constexpr float kInitialEnergy = 600.0f; // 新基地启动能源，足以建立五行基础供弹与补充集光器
constexpr std::array<Definition, static_cast<size_t>(Machine::Count)> kDefinitions{{
    {"empty", "拆除", "拆除返还建造和升级能源；内部弹药丢弃。", 0, 0, 0},
    {"solar", "集光器", "持续采集阳光，存入全基地能源池。", 50, 0, 0},
    {"grower", "育豆机", "消耗阳光生产豌豆，沿箭头输出。", 75, 0, 5},
    {"belt", "运输带", "沿箭头运输；R 旋转，新弹药每步只走一格。", 5, 0, 1},
    {"splitter", "分流器", "A 控制输出，B 选择前方/右侧；可配置普通分流或严格选路。", 30, 1, 4},
    {"store", "弹药仓", "储存豌豆并沿箭头出库，缓冲供需波动。", 40, 1, 60},
    {"feeder", "装填口", "把指定弹种送到前线弹仓；A 控制运行，B 切换两套路线与弹种。", 20, 0, 5},
    {"switch", "开关", "配置切换开关；输出到机器 A 口可控制启停。", 5, 2, 0},
    {"enemy", "敌情器", "选定路线敌人数达到门槛时输出开启信号。", 10, 2, 0},
    {"stock", "库存器", "检测指定弹种库存；A 口未连接时检测选定路线的弹仓。", 10, 2, 0},
    {"and", "与门", "A、B 同时为高时输出高电平。", 5, 2, 0},
    {"or", "或门", "A、B 任意一个为高时输出高电平。", 5, 2, 0},
    {"not", "非门", "把 A 口信号反转；每个逻辑步传播一级。", 5, 2, 0},
    {"timer", "定时器", "按设定周期输出半周期高电平；A 口可控制运行。", 10, 2, 0},
    {"latch", "记忆器", "A 置位、B 复位，复位优先；断开后保留状态。", 10, 2, 0},
    {"cooler", "冷凝机", "A 控制运行，B 切换配方；做完当前一份再切，不清空原料和成品。", 100, ColdUnlockWave, 20},
    {"spore_grower", "孢子培养机", "消耗阳光培养孢子，供精英胆小菇持续射击。", 100, 4, 10},
    {"melon_grower", "瓜果培育机", "消耗阳光生产西瓜，可直接供弹或送去冷凝。", 150, 5, 5},
    {"salvage_port", "回收入口", "从全基地共享的击杀残骸队列取料，沿箭头交给物流。", 30, 1, 5},
    {"recycler", "回收机", "接收残骸，每份加工为5阳光；准备阶段停止回收。", 100, 1, 10},
    {"counter", "计数器", "可数信号、产出或出库；B清零，支持累计达标或每批翻转。", 15, 4, 0},
    {"cannon", "瓜弹炮台", "独立供弹，A许可开火，B选择路线与弹种方案；充能后连射三发。", 350, 6, CannonBufferPerAmmo * 2}
}};

void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
int ReadInt(const nlohmann::json& j, const char* key, int min, int max) {
    const auto& value = j.at(key);
    Require(value.is_number_integer(), "整数类型错误");
    if (value.is_number_unsigned()) Require(value.get<std::uint64_t>() <= static_cast<std::uint64_t>(max), "整数超出允许范围");
    const auto n = value.get<std::int64_t>();
    Require(n >= min && n <= max, "整数超出允许范围");
    return static_cast<int>(n);
}
float ReadFloat(const nlohmann::json& j, const char* key, float min, float max) {
    const auto& value = j.at(key);
    Require(value.is_number(), "数值类型错误");
    const double n = value.get<double>();
    Require(std::isfinite(n) && n >= min && n <= max, "数值超出允许范围");
    return static_cast<float>(n);
}
std::uint64_t ReadLifetimeCount(const nlohmann::json& value, std::uint64_t limit = 1000000000000ULL) {
    Require(value.is_number_integer() && value >= 0 && value <= limit, "累计物料计数无效");
    return value.get<std::uint64_t>();
}
/** schema 3 及更早的电路购买价格；只用于保留旧设备的实际退款，不参与新建报价。 */
int LegacyMachineCost(Machine type) {
    switch (type) {
    case Machine::Switch: case Machine::And: case Machine::Or: case Machine::Not: return 15;
    case Machine::EnemySensor: case Machine::StockSensor: case Machine::Timer: case Machine::Latch: return 25;
    case Machine::Counter: return 40;
    default: return Describe(type).cost;
    }
}
/** 固定长度库存与配额逐项验证整数，防止 JSON 浮点或溢出被隐式截断。 */
template<size_t N> std::array<int, N> ReadCounts(const nlohmann::json& j, const char* key, int maximum) {
    const auto& values = j.at(key); Require(values.is_array() && values.size() == N, "库存或配额长度错误");
    std::array<int, N> result{};
    for (size_t i = 0; i < N; ++i) {
        Require(values[i].is_number_integer() && values[i] >= 0 && values[i] <= maximum, "库存或配额越界");
        result[i] = values[i].get<int>();
    }
    return result;
}
bool ReadBool(const nlohmann::json& j, const char* key) {
    Require(j.at(key).is_boolean(), "布尔类型错误");
    return j.at(key).get<bool>();
}
}

const Definition& Describe(Machine type) { return kDefinitions.at(static_cast<size_t>(type)); }
Machine FindMachine(const std::string& key) {
    for (int i = 0; i < static_cast<int>(Machine::Count); ++i)
        if (key == kDefinitions[i].key) return static_cast<Machine>(i);
    return Machine::Count;
}
bool IsLogic(Machine type) { return (type >= Machine::Switch && type <= Machine::Latch) || type == Machine::Counter; }
bool HasSelectionInput(Machine type) { return type == Machine::Cooler || type == Machine::Feeder || type == Machine::Splitter || type == Machine::Cannon; }
bool HasDirectionalOutput(Machine type) {
    return type == Machine::Grower || type == Machine::Belt || type == Machine::Splitter
        || type == Machine::Store || type == Machine::Cooler || type == Machine::SporeGrower
        || type == Machine::MelonGrower || type == Machine::SalvagePort;
}
const char* DirectionName(int direction) {
    constexpr std::array<const char*, 4> names{"右", "下", "左", "上"};
    return names[direction & 3];
}
bool CanUpgradeMachine(Machine type) { return type == Machine::Solar || type == Machine::Store || ProductionPeriod(type, 1) > 0; }
const char* AmmoName(int filter) {
    constexpr std::array<const char*, ItemCount> names{"普通", "寒冰", "孢子", "西瓜", "冰瓜", "残骸"};
    return filter >= 0 && filter < ItemCount ? names[filter] : "混合";
}
float ProductionPeriod(Machine type, int level) {
    if (type == Machine::Grower || type == Machine::Cooler) return GrowPeriods.at(level - 1);
    if (type == Machine::SporeGrower) return SporePeriods.at(level - 1);
    if (type == Machine::MelonGrower) return MelonPeriods.at(level - 1);
    if (type == Machine::Recycler) return RecyclePeriods.at(level - 1);
    return 0;
}
Ammo ProducedItem(Machine type) {
    if (type == Machine::SporeGrower) return Ammo::Spore;
    if (type == Machine::MelonGrower) return Ammo::Melon;
    if (type == Machine::SalvagePort) return Ammo::Scrap;
    return Ammo::Pea;
}
namespace {
/** 冷凝机只交出成品，原料留在输入缓冲；物流设备可运送全部物料。 */
bool CanOutputItem(const Cell& cell, Ammo item) {
    if (cell.type == Machine::Cooler) return item == Ammo::Ice || item == Ammo::IceMelon;
    if (cell.type == Machine::Grower || cell.type == Machine::SporeGrower || cell.type == Machine::MelonGrower || cell.type == Machine::SalvagePort)
        return item == ProducedItem(cell.type);
    return HasDirectionalOutput(cell.type) || cell.type == Machine::Feeder;
}
int CoolerOutputCount(const Cell& cell) { return cell.iceStock + cell.Inventory(Ammo::IceMelon); }
}
int UpgradeCost(int baseCost, int currentLevel) {
    return currentLevel > 0 && currentLevel < MaxUpgradeLevel ? baseCost * currentLevel : 0;
}
int Capacity(Machine type, int level) {
    return type == Machine::Store ? StoreCapacities.at(level - 1) : Describe(type).capacity;
}
World::World() { Reset(); }

void World::Reset() {
    mCells = {}; mLanes = {}; mEnemyCounts = {};
    mPlantCooldowns.clear();
    mPhase = Phase::Planning; mColumns = 10; mRows = 10; mHealth = 3; mCompletedWaves = 0;
    mEnergy = kInitialEnergy; mTransportClock = 0; mSpawnClock = 0;
    mRemainingSpawns = 0; mSpawned = 0;
    mProduced = 0; mFired = 0; mDelivered = 0; mCheckpoint = nullptr;
    mProcessed = 0; mIceFired = 0; mPendingScrap = 0; mWaveScrap = 0; mRecycled = 0;
    // 两座集光器避免拆线耗尽能源后无法恢复；拆除仍返还原价。
    mCells[0].type = Machine::Solar;
    mCells[MaxColumns].type = Machine::Solar;
    mCells[0].paidCost = mCells[MaxColumns].paidCost = Describe(Machine::Solar).cost;
}
bool World::Valid(int i) const { return i >= 0 && i < CellCount && i / MaxColumns < mRows && i % MaxColumns < mColumns; }
bool World::Unlocked(Machine type) const {
    return type >= Machine::Empty && type < Machine::Count && Describe(type).unlockWave <= mCompletedWaves;
}
bool World::Enabled(int i) const {
    const int source = mCells[i].inputs[0];
    return source < 0 || mCells[source].signal;
}
bool World::CanBuild(int i, Machine type, int direction) const {
    return Valid(i) && mPhase != Phase::Dead && type > Machine::Empty && Unlocked(type)
        && direction >= 0 && direction <= 3 && mCells[i].type == Machine::Empty
        && mEnergy >= Describe(type).cost;
}
bool World::Build(int i, Machine type, int direction) {
    if (!CanBuild(i, type, direction)) return false;
    mEnergy -= Describe(type).cost;
    mCells[i] = {}; mCells[i].type = type; mCells[i].direction = direction;
    mCells[i].paidCost = Describe(type).cost;
    mCells[i].route = (i / MaxColumns) % Rows;
    mCells[i].route2 = (mCells[i].route + 1) % Rows;
    if (type == Machine::Feeder) mCells[i].filter = mCells[i].filter2 = 0;
    if (type == Machine::Cannon) mCells[i].filter = mCells[i].filter2 = static_cast<int>(Ammo::Melon);
    if (type == Machine::Timer) mCells[i].setting = 4;
    if (type == Machine::EnemySensor) mCells[i].setting = 1;
    return true;
}
int World::RemoveRefund(int i) const {
    if (!Valid(i) || mCells[i].type == Machine::Empty) return 0;
    return mCells[i].paidCost;
}
bool World::Remove(int i) {
    if (!Valid(i) || mPhase == Phase::Dead || mCells[i].type == Machine::Empty) return false;
    mEnergy = std::min(kEnergyLimit, mEnergy + RemoveRefund(i));
    mCells[i] = {};
    for (auto& c : mCells) for (auto& input : c.inputs) if (input == i) input = -1;
    return true;
}
bool World::CanBuyUpgrade(int baseCost, int level) const {
    return mPhase != Phase::Dead && baseCost > 0 && level > 0 && level < MaxUpgradeLevel
        && mCompletedWaves >= UpgradeWaves[level] && mEnergy >= UpgradeCost(baseCost, level);
}
bool World::BuyUpgrade(int baseCost, int level) {
    if (!CanBuyUpgrade(baseCost, level)) return false;
    mEnergy -= UpgradeCost(baseCost, level); return true;
}
bool World::Upgrade(int i) {
    if (!Valid(i) || !CanUpgradeMachine(mCells[i].type)) return false;
    auto& c = mCells[i];
    if (!BuyUpgrade(Describe(c.type).cost, c.level)) return false;
    c.paidCost += UpgradeCost(Describe(c.type).cost, c.level);
    if (const float period = ProductionPeriod(c.type, c.level); period > 0) c.clock *= ProductionPeriod(c.type, c.level + 1) / period;
    ++c.level;
    return true;
}
bool World::Rotate(int i) {
    if (!Valid(i) || mPhase == Phase::Dead || !HasDirectionalOutput(mCells[i].type)) return false;
    mCells[i].direction = (mCells[i].direction + 1) % 4; return true;
}
bool World::CanConnect(int source, int target, int input) const {
    if (!Valid(target) || mPhase == Phase::Dead || input < 0 || input > 1
        || mCells[target].type == Machine::Empty) return false;
    if (source != -1 && (!Valid(source) || mCells[source].type == Machine::Empty)) return false;
    const auto targetType = mCells[target].type;
    if (input == 1 && targetType != Machine::And && targetType != Machine::Or && targetType != Machine::Latch && targetType != Machine::Counter && !HasSelectionInput(targetType)) return false;
    if (input == 1 && targetType == Machine::Cooler && mCompletedWaves < 8 && source >= 0) return false;
    if (source >= 0) {
        if (input == 0 && targetType == Machine::Counter && mCells[target].counterMode != CounterMode::Signal)
            return CanCountSource(source, mCells[target].counterMode);
        if (!IsLogic(mCells[source].type) && !(input == 0 && targetType == Machine::StockSensor)) return false;
    }
    return true;
}
bool World::Connect(int source, int target, int input) {
    if (!CanConnect(source, target, input)) return false;
    mCells[target].inputs[input] = source;
    if (input == 0 && mCells[target].type == Machine::Counter) ResetCounterObservation(mCells[target]);
    return true;
}
bool World::Configure(int i) {
    if (!Valid(i) || mPhase == Phase::Dead) return false;
    auto& c = mCells[i];
    if (c.type == Machine::Switch || c.type == Machine::Cannon) c.manual = !c.manual;
    else if (c.type == Machine::Timer) return SetSetting(i, c.setting >= 16 ? 2 : c.setting * 2);
    else if (c.type == Machine::StockSensor || c.type == Machine::Counter) return SetSetting(i, c.setting >= 40 ? 5 : c.setting * 2);
    else if (c.type == Machine::EnemySensor) return SetSetting(i, c.setting >= 16 ? 1 : c.setting * 2);
    else return false;
    return true;
}
bool World::SetRoute(int i, int row, int plan) {
    if (!Valid(i) || row < 0 || row >= Rows || plan < -1 || mPhase == Phase::Dead) return false;
    const auto type = mCells[i].type;
    if (type != Machine::Feeder && type != Machine::Cannon && type != Machine::EnemySensor && type != Machine::StockSensor) return false;
    if (plan < 0) plan = ActivePlan(i);
    if (plan > 1 || (plan == 1 && type != Machine::Feeder && type != Machine::Cannon)) return false;
    (plan == 1 ? mCells[i].route2 : mCells[i].route) = row; return true;
}
bool World::SetFilter(int i, int filter, int plan) {
    if (!Valid(i) || filter < -1 || filter >= ItemCount || plan < -1 || mPhase == Phase::Dead) return false;
    const auto type = mCells[i].type;
    if (type != Machine::Feeder && type != Machine::Cannon && type != Machine::Counter && type != Machine::Store && type != Machine::Splitter && type != Machine::StockSensor) return false;
    if (type == Machine::Feeder && filter == static_cast<int>(Ammo::Scrap)) return false;
    if (type == Machine::Cannon && (filter < static_cast<int>(Ammo::Melon) || filter > static_cast<int>(Ammo::IceMelon)
        || (filter == static_cast<int>(Ammo::IceMelon) && mCompletedWaves < 8))) return false;
    if (plan < 0) plan = ActivePlan(i);
    if (plan > 1 || (plan == 1 && type != Machine::Feeder && type != Machine::Cannon)) return false;
    (plan == 1 ? mCells[i].filter2 : mCells[i].filter) = filter;
    if (type == Machine::Counter) ResetCounterObservation(mCells[i]);
    return true;
}
bool World::SetQuota(int row, int plainQuota) {
    if (row < 0 || row >= Rows || plainQuota < 0 || plainQuota > MagazineCapacity || mPhase == Phase::Dead) return false;
    mLanes[row].plainQuota = plainQuota; mLanes[row].iceQuota = MagazineCapacity - plainQuota;
    mLanes[row].extraQuota = {}; return true;
}
bool World::SetAmmoQuota(int row, Ammo type, int quota) {
    const int item = static_cast<int>(type);
    if (row < 0 || row >= Rows || item < 0 || item >= AmmoCount || quota < 0 || quota > MagazineCapacity || mPhase == Phase::Dead) return false;
    auto& lane = mLanes[row];
    int difference = quota - lane.Quota(type);
    lane.Quota(type) = quota;
    while (difference > 0) {
        int donor = -1;
        for (int a = 0; a < AmmoCount; ++a) if (a != item && (donor < 0 || lane.Quota(static_cast<Ammo>(a)) > lane.Quota(static_cast<Ammo>(donor)))) donor = a;
        --lane.Quota(static_cast<Ammo>(donor)); --difference;
    }
    if (difference < 0) lane.Quota(type == Ammo::Pea ? Ammo::Ice : Ammo::Pea) -= difference;
    return true;
}
void World::EnsureAmmoQuota(int row, Ammo type) {
    if (row >= 0 && row < Rows && static_cast<int>(type) < AmmoCount && mLanes[row].Quota(type) == 0) SetAmmoQuota(row, type, 20);
}
bool World::SetRecipe(int i, int recipe) {
    return Valid(i) && mCells[i].type == Machine::Cooler && SetPlan(i, recipe);
}
bool World::SpendEnergy(float amount) {
    if (mPhase == Phase::Dead || !std::isfinite(amount) || amount < 0 || mEnergy < amount) return false;
    mEnergy -= amount; return true;
}
void World::RefundEnergy(float amount) {
    if (std::isfinite(amount) && amount > 0) mEnergy = std::min(kEnergyLimit, mEnergy + amount);
}
void World::RecordKill() {
    if (mPhase != Phase::Assault || mWaveScrap >= ScrapPerWave) return;
    ++mWaveScrap; ++mPendingScrap;
}
bool World::AddShooter(int row, bool ice) {
    const int cost = ice ? IceShooterCost : ShooterCost;
    if (row < 0 || row >= Rows || mPhase == Phase::Dead || mLanes[row].shooters >= MaxShootersPerLane || mEnergy < cost
        || (ice && mCompletedWaves < ColdUnlockWave)) return false;
    mEnergy -= cost; ++mLanes[row].shooters; return true;
}
bool World::Expand() {
    if (mPhase == Phase::Dead || (mColumns >= MaxColumns && mRows >= MaxRows) || mCompletedWaves < 3 || mEnergy < ExpansionCost) return false;
    mEnergy -= ExpansionCost; mColumns = std::min(MaxColumns, mColumns + 2); mRows = std::min(MaxRows, mRows + 2); return true;
}
bool World::Repair() {
    if (mPhase == Phase::Dead || mHealth >= 3 || mEnergy < RepairCost) return false;
    mEnergy -= RepairCost; ++mHealth; return true;
}
bool World::StartAssault() {
    if (mPhase != Phase::Planning) return false;
    mCheckpoint = SaveState();
    mPhase = Phase::Assault; mSpawned = 0; mWaveScrap = 0;
    mRemainingSpawns = 5 + std::min(100, mCompletedWaves * 2);
    mSpawnClock = 1; return true;
}
bool World::Retry() {
    if (mPhase != Phase::Dead || mCheckpoint.is_null()) return false;
    ReadState(mCheckpoint); mCheckpoint = nullptr; return true;
}
int World::Neighbor(int i, int direction) const {
    const int row = i / MaxColumns + (direction == 1 ? 1 : direction == 3 ? -1 : 0);
    const int col = i % MaxColumns + (direction == 0 ? 1 : direction == 2 ? -1 : 0);
    return row >= 0 && row < mRows && col >= 0 && col < mColumns ? row * MaxColumns + col : -1;
}

/** 只约束入库类型；改变筛选时不销毁已经储存的其他弹药。 */
bool World::Accepts(const Cell& cell, Ammo type) const {
    if (cell.type == Machine::Cooler) return type == Ammo::Pea || (type == Ammo::Melon && mCompletedWaves >= 8);
    if (cell.type == Machine::Cannon) return type == Ammo::Melon || (type == Ammo::IceMelon && mCompletedWaves >= 8);
    if (cell.type == Machine::Recycler) return type == Ammo::Scrap;
    if (cell.type == Machine::Feeder && type == Ammo::Scrap) return false;
    if (cell.type == Machine::Store || cell.type == Machine::Feeder) {
        const int filter = cell.type == Machine::Feeder && SelectedPlan(cell) == 1 ? cell.filter2 : cell.filter;
        return filter < 0 || filter == static_cast<int>(type);
    }
    return cell.type == Machine::Belt || cell.type == Machine::Splitter;
}
int World::ReceivingRoom(const Cell& cell, Ammo type) const {
    if (!Accepts(cell, type)) return 0;
    if (cell.type == Machine::Cooler) return 5 - cell.Inventory(type);
    if (cell.type == Machine::Cannon) return CannonBufferPerAmmo - cell.Inventory(type);
    return Capacity(cell.type, cell.level) - cell.TotalStock();
}

OutputInfo World::InspectOutput(int i, bool side) const {
    OutputInfo result;
    if (!Valid(i) || !HasDirectionalOutput(mCells[i].type)) { result.message = "此设备不通过格边出料"; return result; }
    const auto& cell = mCells[i];
    if (cell.type == Machine::Splitter && cell.strictSplit && (ActivePlan(i) == 1) != side) {
        result.message = "此出口未被选择口选中"; return result;
    }
    const int direction = (cell.direction + (side && cell.type == Machine::Splitter ? 1 : 0)) % 4;
    const std::string prefix = std::string(DirectionName(direction)) + "侧";
    result.target = Neighbor(i, direction);
    if (result.target < 0) { result.message = prefix + "超出地界"; return result; }
    const auto& target = mCells[result.target];
    if (target.type == Machine::Empty) { result.message = prefix + "没有接收设备"; return result; }
    bool accepts = false, hasCargoType = false;
    for (int a = 0; a < ItemCount; ++a) {
        const auto ammo = static_cast<Ammo>(a);
        if (!CanOutputItem(cell, ammo)) continue;
        if (cell.type == Machine::Cooler && CoolerOutputCount(cell) == 0 && ammo != (cell.recipe == 1 ? Ammo::IceMelon : Ammo::Ice)) continue;
        if (cell.type == Machine::Splitter && !cell.strictSplit && cell.filter >= 0 && ((cell.filter == static_cast<int>(ammo)) != side)) continue;
        if ((cell.type == Machine::Cooler ? CoolerOutputCount(cell) : cell.TotalStock()) > 0 && cell.Inventory(ammo) == 0) continue;
        hasCargoType = true;
        if (!Accepts(target, ammo)) continue;
        accepts = true;
        if (ReceivingRoom(target, ammo) > 0) result.ready = true;
    }
    result.message = result.ready ? std::string("朝") + DirectionName(direction) + " → " + Describe(target.type).name
        : !hasCargoType ? "此出口暂无对应弹药"
        : !accepts ? prefix + "目标不接收此弹种" : prefix + "目标缓存已满";
    return result;
}

/** 同拍库存快照与目标预留共同保证混装弹药每拍只走一格，堵塞不复制、不丢货。 */
void World::TransportStep() {
    const auto before = mCells;
    for (int i = 0; i < CellCount; ++i) {
        auto& c = mCells[i];
        const bool a = c.inputs[0] >= 0 && before[c.inputs[0]].signal;
        const bool b = c.inputs[1] >= 0 && before[c.inputs[1]].signal;
        switch (c.type) {
        case Machine::Switch: c.signal = c.manual; break;
        case Machine::EnemySensor:
            c.signal = mEnemyCounts[c.route] >= c.setting; break;
        case Machine::StockSensor: {
            const auto& source = c.inputs[0] >= 0 ? before[c.inputs[0]] : c;
            const auto& lane = mLanes[c.route];
            const int count = c.inputs[0] >= 0 ? (c.filter < 0 ? source.TotalStock() : source.Inventory(static_cast<Ammo>(c.filter)))
                : c.filter < 0 ? lane.TotalStock() : c.filter < AmmoCount ? lane.Inventory(static_cast<Ammo>(c.filter)) : 0;
            c.signal = count >= c.setting; break;
        }
        case Machine::And: c.signal = a && b; break;
        case Machine::Or: c.signal = a || b; break;
        case Machine::Not: c.signal = !a; break;
        case Machine::Latch: if (b) c.signal = false; else if (a) c.signal = true; break;
        case Machine::Counter: {
            std::uint64_t amount = a && !c.previousInput ? 1 : 0;
            if (c.counterMode != CounterMode::Signal) {
                const auto total = CounterSourceTotal(c);
                amount = total >= c.counterObserved ? total - c.counterObserved : 0;
                c.counterObserved = total;
            }
            c.previousInput = a;
            if (b) { c.count = 0; c.signal = false; }
            else if (c.counterOutput == CounterOutput::Toggle) {
                const auto total = c.count + amount;
                if ((total / c.setting) % 2) c.signal = !c.signal;
                c.count = static_cast<int>(total % c.setting);
            } else {
                c.count = static_cast<int>(std::min<std::uint64_t>(1000000, c.count + amount));
                c.signal = c.count >= c.setting;
            }
            break;
        }
        case Machine::Timer:
            if (c.inputs[0] < 0 || a) c.clock = std::fmod(c.clock + TransportPeriod, static_cast<float>(c.setting));
            else c.clock = 0;
            c.signal = (c.inputs[0] < 0 || a) && c.clock >= c.setting * 0.5f; break;
        default: c.signal = false; break;
        }
    }
    std::array<std::array<int, ItemCount>, CellCount> incoming{};
    for (int i = 0; i < CellCount; ++i) {
        auto& c = mCells[i];
        if (!Valid(i) || before[i].TotalStock() <= 0 || !Enabled(i)) continue;
        bool moved = false;
        for (int choice = 0; choice < ItemCount && !moved; ++choice) {
            const auto ammo = static_cast<Ammo>((c.nextItem + choice) % ItemCount);
            if (before[i].Inventory(ammo) <= 0 || !CanOutputItem(c, ammo)) continue;
            if (c.type == Machine::Feeder) {
                const int filter = ActiveFilter(i);
                if (ammo == Ammo::Scrap || (filter >= 0 && filter != static_cast<int>(ammo))) continue;
                auto& lane = mLanes[ActiveRoute(i)];
                if (lane.TotalStock() < MagazineCapacity && lane.Inventory(ammo) < lane.Quota(ammo)) {
                    --c.Inventory(ammo); ++lane.Inventory(ammo); ++mDelivered; moved = true;
                }
            } else {
                const bool split = c.type == Machine::Splitter;
                const bool strict = split && c.strictSplit;
                const bool filtered = split && !strict && c.filter >= 0;
                const bool sideFirst = strict ? ActivePlan(i) == 1 : filtered ? c.filter == static_cast<int>(ammo)
                    : c.inputs[1] >= 0 ? mCells[c.inputs[1]].signal : c.alternate;
                for (int attempt = 0; attempt < (split && !filtered && !strict ? 2 : 1); ++attempt) {
                    const int direction = (c.direction + (split && (sideFirst != (attempt != 0)) ? 1 : 0)) % 4;
                    const int next = Neighbor(i, direction);
                    if (next < 0) continue;
                    const auto& target = before[next];
                    const int reserved = std::accumulate(incoming[next].begin(), incoming[next].end(), 0);
                    if (ReceivingRoom(target, ammo) <= reserved) continue;
                    --c.Inventory(ammo); ++incoming[next][static_cast<int>(ammo)]; c.alternate = !c.alternate; moved = true; break;
                }
            }
            if (moved) { ++c.transferredItems[static_cast<int>(ammo)]; c.nextItem = (static_cast<int>(ammo) + 1) % ItemCount; c.iceNext = ammo == Ammo::Pea; }
        }
    }
    for (int i = 0; i < CellCount; ++i)
        for (int a = 0; a < ItemCount; ++a) mCells[i].Inventory(static_cast<Ammo>(a)) += incoming[i][a];
}

void World::Tick(float seconds) {
    // 准备期只允许施工，不能靠等待囤阳光、弹药或推进电路；保留所有周期余时到开波。
    if (mPhase != Phase::Assault || !std::isfinite(seconds) || seconds <= 0) return;
    // 有限子步避免游戏倍速改变物流/战斗结果，并允许纯逻辑测试推进长时间。
    while (seconds > 0.000001f && mPhase == Phase::Assault) {
        const float dt = std::min(seconds, 1.0f / 60.0f); seconds -= dt;
        for (auto it = mPlantCooldowns.begin(); it != mPlantCooldowns.end();) {
            it->second = std::max(0.0f, it->second - dt);
            if (it->second <= 0) it = mPlantCooldowns.erase(it); else ++it;
        }
        for (int i = 0; i < CellCount; ++i) {
            auto& c = mCells[i];
            // 炮台 A 口只许可开火；待机时仍充能，已开始的一轮也不会被信号变化截断。
            if (c.type == Machine::Cannon) {
                c.clock = std::min(CannonChargeSeconds, c.clock + dt);
                if (c.volleyRemaining > 0) c.volleyClock = std::max(-CannonShotInterval * CannonVolleySize, c.volleyClock - dt);
            }
            if (!Enabled(i)) continue;
            if (c.type == Machine::Solar) mEnergy = std::min(kEnergyLimit, mEnergy + SolarRates[c.level - 1] * dt);
            const bool producer = c.type == Machine::Grower || c.type == Machine::SporeGrower || c.type == Machine::MelonGrower;
            if (producer) {
                const float period = ProductionPeriod(c.type, c.level);
                const auto item = ProducedItem(c.type);
                const float cost = c.type == Machine::SporeGrower ? SporeEnergy : c.type == Machine::MelonGrower ? MelonEnergy : GrowEnergy;
                // 正常生产保留跨周期余时；堵塞最多保留一份准备进度。
                c.clock = std::min(period, c.clock) + dt;
                if (c.clock >= period && c.TotalStock() < Capacity(c.type, c.level) && mEnergy >= cost) {
                    c.clock -= period; mEnergy -= cost; ++c.Inventory(item); ++c.producedItems[static_cast<int>(item)]; ++mProduced;
                } else c.clock = std::min(period, c.clock);
            }
            if (c.type == Machine::Cooler) {
                const int requested = ActivePlan(i);
                if (!c.processing && c.recipe != requested) { c.recipe = requested; c.clock = 0; }
                const auto input = c.recipe == 1 ? Ammo::Melon : Ammo::Pea;
                const auto output = c.recipe == 1 ? Ammo::IceMelon : Ammo::Ice;
                if (c.Inventory(input) <= 0) continue;
                const float period = ProductionPeriod(c.type, c.level), cost = c.recipe == 1 ? IceMelonEnergy : CoolingEnergy;
                // 开工时预留自己的成品空间；输出满时不锁定旧配方，另一条配方仍能接管。
                if (!c.processing) {
                    if (c.Inventory(output) >= 5) continue;
                    c.processing = true;
                }
                c.clock = std::min(period, c.clock) + dt;
                if (c.clock >= period && c.Inventory(output) < 5 && mEnergy >= cost) {
                    c.clock -= period; --c.Inventory(input); ++c.Inventory(output); mEnergy -= cost; ++mProcessed;
                    ++c.producedItems[static_cast<int>(output)]; c.processing = false;
                } else c.clock = std::min(period, c.clock);
            }
            if (c.type == Machine::SalvagePort && mPendingScrap > 0) {
                c.clock = std::min(TransportPeriod, c.clock) + dt;
                if (c.clock >= TransportPeriod && c.TotalStock() < Capacity(c.type, c.level)) {
                    c.clock -= TransportPeriod; --mPendingScrap; ++c.Inventory(Ammo::Scrap); ++c.producedItems[static_cast<int>(Ammo::Scrap)];
                } else c.clock = std::min(TransportPeriod, c.clock);
            }
            if (c.type == Machine::Recycler && c.Inventory(Ammo::Scrap) > 0) {
                const float period = ProductionPeriod(c.type, c.level);
                c.clock = std::min(period, c.clock) + dt;
                if (c.clock >= period && mEnergy < kEnergyLimit) {
                    c.clock -= period; --c.Inventory(Ammo::Scrap); RefundEnergy(ScrapEnergy); ++mRecycled;
                } else c.clock = std::min(period, c.clock);
            }
        }
        mTransportClock += dt;
        if (mTransportClock >= TransportPeriod) { mTransportClock -= TransportPeriod; TransportStep(); }
        if (mPhase == Phase::Assault && mRemainingSpawns > 0) mSpawnClock -= dt;
    }
}

void World::ObserveFrontline(const std::array<int, Rows>& enemies, const std::array<int, Rows>& shooters) {
    mEnemyCounts = enemies;
    for (int row = 0; row < Rows; ++row) mLanes[row].shooters = shooters[row];
}
bool World::ReserveAmmo(int row, Ammo type) {
    if (mPhase != Phase::Assault || row < 0 || row >= Rows || static_cast<int>(type) < 0 || static_cast<int>(type) >= AmmoCount || mLanes[row].Inventory(type) <= 0) return false;
    --mLanes[row].Inventory(type); return true;
}
int World::NextSpawnRow() const {
    return mPhase == Phase::Assault && mRemainingSpawns > 0 && mSpawnClock <= 0 ? mSpawned % Rows : -1;
}
void World::CommitSpawn() {
    if (NextSpawnRow() < 0) return;
    mSpawnClock += std::max(0.8f, 2.5f - mCompletedWaves * 0.08f);
    ++mSpawned; --mRemainingSpawns;
}
void World::CompleteAssault() {
    if (mPhase != Phase::Assault || mRemainingSpawns != 0) return;
    for (const auto& cell : mCells) if (cell.volleyRemaining > 0) return;
    mPhase = Phase::Planning; ++mCompletedWaves;
    mPlantCooldowns.clear();
    mEnergy = std::min(kEnergyLimit, mEnergy + 150 + mCompletedWaves * 20.0f);
    mCheckpoint = nullptr;
}
void World::RecordBreach() {
    if (mPhase != Phase::Assault) return;
    mHealth = std::max(0, mHealth - 1);
    if (mHealth == 0) mPhase = Phase::Dead;
}

std::string World::Status(int i) const {
    if (!Valid(i)) return "完成第 3 波后可扩建";
    const auto& c = mCells[i];
    if (c.type == Machine::Empty) return "空地";
    if (mPhase == Phase::Planning) return "准备停机 · 开波后运行";
    if (mPhase == Phase::Dead) return "基地失守 · 已停机";
    if (IsLogic(c.type)) return c.signal ? "信号：高" : "信号：低";
    if (c.type == Machine::Cannon) {
        if (c.volleyRemaining > 0) return "齐射中 · 本轮路线与弹种已锁定";
        if (c.clock < CannonChargeSeconds) return "炮台充能中";
        if (!c.manual || !Enabled(i)) return "已充能 · 等待开火许可";
        if (c.Inventory(static_cast<Ammo>(ActiveFilter(i))) < CannonVolleySize) return "等待三颗对应瓜弹";
        return "已就绪 · 等待目标";
    }
    if (!Enabled(i)) return "运行口关闭";
    if (c.type == Machine::Solar) return mEnergy >= kEnergyLimit ? "储能已满" : "采集阳光";
    const bool producer = c.type == Machine::Grower || c.type == Machine::SporeGrower || c.type == Machine::MelonGrower;
    const float cost = c.type == Machine::SporeGrower ? SporeEnergy : c.type == Machine::MelonGrower ? MelonEnergy : GrowEnergy;
    if (producer && c.TotalStock() == 0 && mEnergy < cost) return "缺少能源";
    if (c.type == Machine::Recycler) return c.TotalStock() == 0 ? "等待残骸" : mEnergy >= kEnergyLimit ? "储能已满" : "回收残骸中";
    if (c.type == Machine::SalvagePort && c.TotalStock() == 0) return mPendingScrap > 0 ? "提取击杀残骸" : "等待击杀残骸";
    if (c.type == Machine::Cooler) {
        if (c.processing && c.recipe != ActivePlan(i)) return "完成当前一份后切换配方";
        if (c.Inventory(c.recipe == 1 ? Ammo::IceMelon : Ammo::Ice) >= 5) return "当前配方成品缓存已满";
        if (c.Inventory(c.recipe == 1 ? Ammo::Melon : Ammo::Pea) == 0) return c.recipe == 1 ? "等待西瓜" : "等待普通豌豆";
        return mEnergy < (c.recipe == 1 ? IceMelonEnergy : CoolingEnergy) ? "缺少能源" : "冷凝加工中";
    }
    if (c.TotalStock() == 0) return producer ? "生产中" : "等待物料";
    if (c.type == Machine::Feeder) {
        const auto& lane = mLanes[ActiveRoute(i)];
        if (lane.TotalStock() >= MagazineCapacity) return "前线弹仓已满";
        bool hasMatching = false;
        for (int a = 0; a < AmmoCount; ++a) {
            const auto item = static_cast<Ammo>(a);
            if (c.Inventory(item) <= 0 || (ActiveFilter(i) >= 0 && ActiveFilter(i) != a)) continue;
            hasMatching = true;
            if (lane.Inventory(item) < lane.Quota(item)) return "装填中";
        }
        return hasMatching ? "对应弹种配额已满" : "等待指定弹种";
    }
    const auto output = InspectOutput(i);
    if (c.type == Machine::Splitter && c.strictSplit) return InspectOutput(i, ActivePlan(i) == 1).message;
    if (!output.ready && (c.type != Machine::Splitter || !InspectOutput(i, true).ready))
        return c.type == Machine::Splitter ? "两侧出口均无法送出" : output.message;
    return "输送中";
}

nlohmann::json World::SaveState() const {
    nlohmann::json cells = nlohmann::json::array(), lanes = nlohmann::json::array();
    for (const auto& c : mCells) cells.push_back({{"type", Describe(c.type).key}, {"level", c.level}, {"direction", c.direction}, {"stock", c.stock},
        {"iceStock", c.iceStock}, {"extraStock", c.extraStock}, {"nextItem", c.nextItem}, {"recipe", c.recipe},
        {"count", c.count}, {"previousInput", c.previousInput}, {"route", c.route}, {"filter", c.filter}, {"iceNext", c.iceNext},
        {"plan", c.plan}, {"route2", c.route2}, {"filter2", c.filter2}, {"strictSplit", c.strictSplit}, {"processing", c.processing}, {"paidCost", c.paidCost},
        {"counterMode", static_cast<int>(c.counterMode)}, {"counterOutput", static_cast<int>(c.counterOutput)}, {"counterObserved", c.counterObserved},
        {"producedItems", c.producedItems}, {"transferredItems", c.transferredItems},
        {"volleyRemaining", c.volleyRemaining}, {"volleyRoute", c.volleyRoute}, {"volleyAmmo", static_cast<int>(c.volleyAmmo)}, {"volleyClock", c.volleyClock},
        {"inputs", c.inputs}, {"setting", c.setting}, {"clock", c.clock}, {"signal", c.signal}, {"manual", c.manual}, {"alternate", c.alternate}});
    for (const auto& l : mLanes) lanes.push_back({{"ammo", l.ammo}, {"iceAmmo", l.iceAmmo}, {"plainQuota", l.plainQuota}, {"shooters", l.shooters},
        {"iceQuota", l.iceQuota}, {"extraAmmo", l.extraAmmo}, {"extraQuota", l.extraQuota}});
    return {{"phase", static_cast<int>(mPhase)}, {"columns", mColumns}, {"rows", mRows}, {"health", mHealth}, {"completedWaves", mCompletedWaves},
        {"energy", mEnergy}, {"transportClock", mTransportClock}, {"spawnClock", mSpawnClock}, {"remainingSpawns", mRemainingSpawns},
        {"spawned", mSpawned}, {"produced", mProduced}, {"fired", mFired}, {"delivered", mDelivered},
        {"processed", mProcessed}, {"iceFired", mIceFired},
        {"pendingScrap", mPendingScrap}, {"waveScrap", mWaveScrap}, {"recycled", mRecycled},
        {"cells", cells}, {"lanes", lanes}, {"plantCooldowns", mPlantCooldowns}};
}
nlohmann::json World::Save() const { return {{"schemaVersion", 5}, {"state", SaveState()}, {"checkpoint", mCheckpoint}}; }

/** 所有实体与信号索引在临时 World 中验证，避免坏档引入越界或部分覆盖。 */
void World::ReadState(const nlohmann::json& s) {
    mPhase = static_cast<Phase>(ReadInt(s, "phase", 0, 2));
    mColumns = ReadInt(s, "columns", 10, MaxColumns); Require(mColumns % 2 == 0, "无效工厂宽度");
    mRows = ReadInt(s, "rows", 10, MaxRows); Require(mRows % 2 == 0, "无效工厂高度");
    mHealth = ReadInt(s, "health", 0, 3); Require((mHealth == 0) == (mPhase == Phase::Dead), "死亡状态不一致");
    mCompletedWaves = ReadInt(s, "completedWaves", 0, 100000);
    const auto& cooldowns = s.at("plantCooldowns");
    Require(cooldowns.is_object() && cooldowns.size() <= Plants.size(), "种植冷却记录错误");
    mPlantCooldowns.clear();
    for (const auto& entry : cooldowns.items()) {
        Require(FindPlant(std::string_view(entry.key())) != nullptr, "种植冷却引用未知植物");
        const float seconds = ReadFloat(cooldowns, entry.key().c_str(), 0, 600);
        Require(mPhase != Phase::Planning || seconds == 0, "准备阶段不应有种植冷却");
        if (seconds > 0) mPlantCooldowns.emplace(entry.key(), seconds);
    }
    mEnergy = ReadFloat(s, "energy", 0, kEnergyLimit);
    mTransportClock = ReadFloat(s, "transportClock", 0, TransportPeriod);
    mSpawnClock = ReadFloat(s, "spawnClock", -1, 3);
    mRemainingSpawns = ReadInt(s, "remainingSpawns", 0, 105);
    mSpawned = ReadInt(s, "spawned", 0, 105);
    mProduced = ReadInt(s, "produced", 0, 1000000000); mFired = ReadInt(s, "fired", 0, 1000000000); mDelivered = ReadInt(s, "delivered", 0, 1000000000);
    mProcessed = ReadInt(s, "processed", 0, 1000000000); mIceFired = ReadInt(s, "iceFired", 0, 1000000000);
    mPendingScrap = ReadInt(s, "pendingScrap", 0, 1000000000);
    mWaveScrap = ReadInt(s, "waveScrap", 0, ScrapPerWave); mRecycled = ReadInt(s, "recycled", 0, 1000000000);
    const auto& cells = s.at("cells"); Require(cells.is_array() && cells.size() == CellCount, "工厂格数错误");
    for (int i = 0; i < CellCount; ++i) {
        const auto& j = cells[i]; auto& c = mCells[i];
        c.type = FindMachine(j.at("type").get<std::string>()); Require(c.type < Machine::Count, "未知机器");
        Require(Unlocked(c.type) && (Valid(i) || c.type == Machine::Empty), "机器尚未解锁或在地界外");
        c.level = j.contains("level") ? ReadInt(j, "level", 1, MaxUpgradeLevel) : 1;
        Require(c.level == 1 || (CanUpgradeMachine(c.type) && mCompletedWaves >= UpgradeWaves[c.level - 1]), "机器等级尚未解锁或类型不支持升级");
        c.direction = ReadInt(j, "direction", 0, 3); c.stock = ReadInt(j, "stock", 0, Capacity(c.type, c.level));
        c.iceStock = ReadInt(j, "iceStock", 0, Capacity(c.type, c.level));
        c.extraStock = ReadCounts<ItemCount - 2>(j, "extraStock", Capacity(c.type, c.level));
        c.nextItem = ReadInt(j, "nextItem", 0, ItemCount - 1); c.recipe = ReadInt(j, "recipe", 0, 1);
        c.count = ReadInt(j, "count", 0, 1000000); c.previousInput = ReadBool(j, "previousInput");
        Require(c.recipe == 0 || (c.type == Machine::Cooler && mCompletedWaves >= 8), "配方尚未解锁或设备不支持");
        Require(c.TotalStock() <= Capacity(c.type, c.level), "混合库存超过设备容量");
        Require(c.type != Machine::Cooler || (c.stock <= 5 && c.iceStock <= 5 && c.Inventory(Ammo::Melon) <= 5 && c.Inventory(Ammo::IceMelon) <= 5
            && c.Inventory(Ammo::Spore) == 0 && c.Inventory(Ammo::Scrap) == 0), "冷凝机输入输出缓冲越界");
        c.route = ReadInt(j, "route", 0, Rows - 1); c.filter = ReadInt(j, "filter", -1, ItemCount - 1); c.iceNext = ReadBool(j, "iceNext");
        Require(c.type != Machine::Feeder || c.filter != static_cast<int>(Ammo::Scrap), "装填口不接收残骸");
        c.setting = ReadInt(j, "setting", 1, c.type == Machine::Timer ? 120 : 1000000); c.clock = ReadFloat(j, "clock", 0, 120);
        c.signal = ReadBool(j, "signal"); c.manual = ReadBool(j, "manual"); c.alternate = ReadBool(j, "alternate");
        c.plan = ReadInt(j, "plan", 0, 1); c.route2 = ReadInt(j, "route2", 0, Rows - 1); c.filter2 = ReadInt(j, "filter2", -1, ItemCount - 1);
        c.strictSplit = ReadBool(j, "strictSplit"); c.processing = ReadBool(j, "processing");
        c.paidCost = ReadInt(j, "paidCost", 0, 1000000000);
        c.counterMode = static_cast<CounterMode>(ReadInt(j, "counterMode", 0, 2));
        c.counterOutput = static_cast<CounterOutput>(ReadInt(j, "counterOutput", 0, 1));
        c.counterObserved = ReadLifetimeCount(j.at("counterObserved"), 1000000000000ULL * ItemCount);
        for (const char* key : {"producedItems", "transferredItems"}) {
            const auto& values = j.at(key); Require(values.is_array() && values.size() == ItemCount, "累计物料统计长度错误");
            auto& destination = std::string_view(key) == "producedItems" ? c.producedItems : c.transferredItems;
            for (int a = 0; a < ItemCount; ++a) destination[a] = ReadLifetimeCount(values[a]);
        }
        c.volleyRemaining = ReadInt(j, "volleyRemaining", 0, CannonVolleySize);
        c.volleyRoute = ReadInt(j, "volleyRoute", 0, Rows - 1);
        c.volleyAmmo = static_cast<Ammo>(ReadInt(j, "volleyAmmo", static_cast<int>(Ammo::Melon), static_cast<int>(Ammo::IceMelon)));
        c.volleyClock = ReadFloat(j, "volleyClock", -CannonShotInterval * CannonVolleySize, CannonShotInterval);
        Require(c.plan == 0 || HasSelectionInput(c.type), "设备没有选择口");
        Require(c.type != Machine::Cooler || c.plan == 0 || mCompletedWaves >= 8, "冷凝方案尚未解锁");
        Require(!c.strictSplit || c.type == Machine::Splitter, "严格选路设备错误");
        Require(!c.processing || (c.type == Machine::Cooler && c.Inventory(c.recipe == 1 ? Ammo::Melon : Ammo::Pea) > 0
            && c.Inventory(c.recipe == 1 ? Ammo::IceMelon : Ammo::Ice) < 5), "在制品没有原料或成品空间");
        Require(c.type != Machine::Feeder || c.filter2 != static_cast<int>(Ammo::Scrap), "装填方案不接收残骸");
        Require(c.counterMode == CounterMode::Signal || c.type == Machine::Counter, "物料计数设备错误");
        Require(c.counterOutput == CounterOutput::Hold || c.type == Machine::Counter, "批次输出设备错误");
        Require(c.volleyRemaining == 0 || (c.type == Machine::Cannon && mPhase != Phase::Planning), "齐射状态错误");
        if (c.type == Machine::Cannon) {
            Require(c.clock <= CannonChargeSeconds && c.stock == 0 && c.iceStock == 0 && c.Inventory(Ammo::Spore) == 0 && c.Inventory(Ammo::Scrap) == 0
                && c.Inventory(Ammo::Melon) <= CannonBufferPerAmmo && c.Inventory(Ammo::IceMelon) <= CannonBufferPerAmmo, "炮台缓存或充能越界");
            for (int filter : {c.filter, c.filter2}) Require(filter == static_cast<int>(Ammo::Melon)
                || (filter == static_cast<int>(Ammo::IceMelon) && mCompletedWaves >= 8), "炮台弹种尚未解锁");
            Require(c.volleyAmmo != Ammo::IceMelon || mCompletedWaves >= 8, "齐射弹种尚未解锁");
        }
        const auto& inputs = j.at("inputs"); Require(inputs.is_array() && inputs.size() == 2, "信号端口错误");
        for (int p = 0; p < 2; ++p) {
            Require(inputs[p].is_number_integer(), "信号索引类型错误");
            if (inputs[p].is_number_unsigned()) Require(inputs[p].get<std::uint64_t>() < CellCount, "信号索引越界");
            const auto source = inputs[p].get<std::int64_t>(); Require(source >= -1 && source < CellCount, "信号索引越界"); c.inputs[p] = static_cast<int>(source);
        }
    }
    for (int i = 0; i < CellCount; ++i) for (int p = 0; p < 2; ++p) if (mCells[i].inputs[p] >= 0) {
        const int source = mCells[i].inputs[p];
        Require(Valid(source) && mCells[source].type != Machine::Empty && mCells[i].type != Machine::Empty, "信号引用空格");
        const auto& target = mCells[i];
        const bool countedSource = p == 0 && target.type == Machine::Counter && target.counterMode != CounterMode::Signal;
        Require(countedSource ? CanCountSource(source, target.counterMode)
            : IsLogic(mCells[source].type) || (p == 0 && target.type == Machine::StockSensor), "无效信号/计量来源");
        Require(p == 0 || target.type == Machine::And || target.type == Machine::Or || target.type == Machine::Latch || target.type == Machine::Counter || HasSelectionInput(target.type), "不存在的 B 口");
        Require(p != 1 || target.type != Machine::Cooler || mCompletedWaves >= 8, "冷凝选择口尚未解锁");
    }
    const auto& lanes = s.at("lanes"); Require(lanes.is_array() && lanes.size() == Rows, "前线行数错误");
    for (int i = 0; i < Rows; ++i) {
        mLanes[i].ammo = ReadInt(lanes[i], "ammo", 0, MagazineCapacity); mLanes[i].shooters = ReadInt(lanes[i], "shooters", 0, MaxShootersPerLane);
        mLanes[i].iceAmmo = ReadInt(lanes[i], "iceAmmo", 0, MagazineCapacity);
        mLanes[i].plainQuota = ReadInt(lanes[i], "plainQuota", 0, MagazineCapacity);
        mLanes[i].iceQuota = ReadInt(lanes[i], "iceQuota", 0, MagazineCapacity);
        mLanes[i].extraAmmo = ReadCounts<AmmoCount - 2>(lanes[i], "extraAmmo", MagazineCapacity);
        mLanes[i].extraQuota = ReadCounts<AmmoCount - 2>(lanes[i], "extraQuota", MagazineCapacity);
        int quota = 0; for (int a = 0; a < AmmoCount; ++a) quota += mLanes[i].Quota(static_cast<Ammo>(a));
        Require(quota == MagazineCapacity, "前线弹种配额总数错误");
        Require(mLanes[i].TotalStock() <= MagazineCapacity, "前线混合弹仓超出总容量");
    }
    Require(mPhase != Phase::Planning || mRemainingSpawns == 0, "准备阶段存在未完成生成队列");

}
bool World::Load(const nlohmann::json& document, std::string& error) {
    try {
        const int version = ReadInt(document, "schemaVersion", 1, 5);
        // 旧厂房固定 5×12 存储跨度；同时重映射信号引用和袭击检查点，已买到的第 11/12 列保留。
        auto migrate = [version](nlohmann::json state) {
            if (state.is_null()) return state;
            if (version < 5) state["plantCooldowns"] = nlohmann::json::object();
            if (version == 1) {
            const int oldWidth = ReadInt(state, "columns", 8, 12);
            Require(oldWidth % 2 == 0, "旧工厂宽度无效");
            const auto oldCells = state.at("cells");
            Require(oldCells.is_array() && oldCells.size() == 60, "旧工厂格数错误");
            auto cells = World().SaveState().at("cells");
            for (int old = 0; old < 60; ++old) {
                auto cell = oldCells[old];
                Require(old % 12 < oldWidth || cell.at("type") == "empty", "旧机器位于未扩建土地");
                auto& inputs = cell.at("inputs");
                Require(inputs.is_array() && inputs.size() == 2, "旧信号端口错误");
                for (auto& input : inputs) {
                    Require(input.is_number_integer() && input >= -1 && input < 60, "旧信号索引错误");
                    const int source = input.get<int>();
                    if (source >= 0) input = (source / 12) * MaxColumns + source % 12;
                }
                cell["iceStock"] = 0; cell["route"] = old / 12;
                cell["filter"] = cell.at("type") == "feeder" ? 0 : -1; cell["iceNext"] = false;
                cells[(old / 12) * MaxColumns + old % 12] = std::move(cell);
            }
            state["cells"] = std::move(cells); state["columns"] = std::max(10, oldWidth); state["rows"] = 10;
            state["processed"] = 0; state["iceFired"] = 0;
            for (auto& lane : state.at("lanes")) { lane["iceAmmo"] = 0; lane["plainQuota"] = 60; }
            }
            if (version < 3) {
                state["pendingScrap"] = 0; state["waveScrap"] = 0; state["recycled"] = 0;
                for (auto& cell : state.at("cells")) {
                    cell["extraStock"] = std::array<int, ItemCount - 2>{}; cell["recipe"] = 0;
                    cell["count"] = 0; cell["previousInput"] = false;
                    cell["nextItem"] = cell.value("iceNext", false) ? 1 : 0;
                    if (cell.at("type") == "enemy") cell["setting"] = 1;
                }
                for (auto& lane : state.at("lanes")) {
                    lane["iceQuota"] = MagazineCapacity - ReadInt(lane, "plainQuota", 0, MagazineCapacity);
                    lane["extraAmmo"] = std::array<int, AmmoCount - 2>{}; lane["extraQuota"] = std::array<int, AmmoCount - 2>{};
                }
            }
            if (version < 4) {
                for (auto& cell : state.at("cells")) {
                    const auto type = FindMachine(cell.at("type").get<std::string>());
                    Require(type < Machine::Cannon, "旧版不支持该设备");
                    const int level = cell.contains("level") ? ReadInt(cell, "level", 1, MaxUpgradeLevel) : 1;
                    cell["paidCost"] = LegacyMachineCost(type) * (1 + level * (level - 1) / 2);
                    cell["plan"] = cell.at("recipe"); cell["route2"] = cell.at("route"); cell["filter2"] = cell.at("filter");
                    cell["strictSplit"] = false; cell["processing"] = false;
                    if (type == Machine::Cooler) {
                        const int recipe = ReadInt(cell, "recipe", 0, 1);
                        const auto& extras = cell.at("extraStock");
                        const int input = recipe ? extras.at(1).get<int>() : cell.at("stock").get<int>();
                        const int output = recipe ? extras.at(2).get<int>() : cell.at("iceStock").get<int>();
                        cell["processing"] = input > 0 && output < 5 && cell.at("clock").get<float>() > 0;
                    }
                    if (type == Machine::Splitter) {
                        // 旧 A 是优先选路而非启停，移到新的选择口后保留原语义。
                        cell["inputs"][1] = cell.at("inputs").at(0); cell["inputs"][0] = -1;
                    }
                    cell["counterMode"] = 0; cell["counterOutput"] = 0; cell["counterObserved"] = 0;
                    cell["producedItems"] = std::array<std::uint64_t, ItemCount>{}; cell["transferredItems"] = std::array<std::uint64_t, ItemCount>{};
                    cell["volleyRemaining"] = 0; cell["volleyRoute"] = 0; cell["volleyAmmo"] = static_cast<int>(Ammo::Melon); cell["volleyClock"] = 0;
                }
            }
            return state;
        };
        World candidate; candidate.ReadState(migrate(document.at("state")));
        candidate.mCheckpoint = migrate(document.at("checkpoint"));
        if (candidate.mPhase != Phase::Planning) {
            World checkpoint; checkpoint.ReadState(candidate.mCheckpoint);
            Require(checkpoint.mPhase == Phase::Planning && checkpoint.mCompletedWaves == candidate.mCompletedWaves, "袭击检查点不一致");
            candidate.mCheckpoint = checkpoint.SaveState();
        } else Require(candidate.mCheckpoint.is_null(), "准备阶段不应带检查点");
        *this = std::move(candidate); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
nlohmann::json World::Inspect() const {
    auto j = SaveState(); j["phaseName"] = mPhase == Phase::Planning ? "planning" : mPhase == Phase::Assault ? "assault" : "dead";
    for (const auto& plant : Plants) j["plantCooldownRemaining"][plant.key] = PlantCooldown(plant.key);
    j["cannonCount"] = 0; j["cannonShots"] = std::uint64_t{};
    for (int i = 0; i < CellCount; ++i) if (mCells[i].type != Machine::Empty) {
        auto& cell = j["cells"][i];
        cell["activePlan"] = ActivePlan(i); cell["activeRoute"] = ActiveRoute(i); cell["activeFilter"] = ActiveFilter(i);
        cell["refund"] = RemoveRefund(i);
        if (mCells[i].type == Machine::Cannon) {
            j["cannonCount"] = j["cannonCount"].get<int>() + 1;
            j["cannonShots"] = j["cannonShots"].get<std::uint64_t>() + mCells[i].transferredItems[static_cast<int>(Ammo::Melon)] + mCells[i].transferredItems[static_cast<int>(Ammo::IceMelon)];
        }
    }
    j["machineCount"] = std::ranges::count_if(mCells, [](const Cell& c) { return c.type != Machine::Empty; });
    j["unlocked"] = nlohmann::json::array();
    for (int i = 1; i < static_cast<int>(Machine::Count); ++i) if (Unlocked(static_cast<Machine>(i))) j["unlocked"].push_back(Describe(static_cast<Machine>(i)).key);
    return j;
}
}
