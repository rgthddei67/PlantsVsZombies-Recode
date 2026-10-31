#include "Workshop.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
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
    {"splitter", "分流器", "前方与右侧轮流供货；接信号后高电平优先右侧。", 30, 1, 4},
    {"store", "弹药仓", "储存豌豆并沿箭头出库，缓冲供需波动。", 40, 1, 60},
    {"feeder", "装填口", "把指定弹种送到选定路线的前线弹仓。", 20, 0, 5},
    {"switch", "开关", "配置切换开关；输出到机器 A 口可控制启停。", 15, 2, 0},
    {"enemy", "敌情器", "选定路线有存活僵尸时输出高电平。", 25, 2, 0},
    {"stock", "库存器", "检测指定弹种库存；A 口未连接时检测选定路线的弹仓。", 25, 2, 0},
    {"and", "与门", "A、B 同时为高时输出高电平。", 15, 2, 0},
    {"or", "或门", "A、B 任意一个为高时输出高电平。", 15, 2, 0},
    {"not", "非门", "把 A 口信号反转；每个逻辑步传播一级。", 15, 2, 0},
    {"timer", "定时器", "按设定周期输出半周期高电平；A 口可控制运行。", 25, 2, 0},
    {"latch", "记忆器", "A 置位、B 复位，复位优先；断开后保留状态。", 25, 2, 0},
    {"cooler", "冷凝机", "普通豌豆加阳光加工成寒冰弹；输入普通弹，沿箭头输出寒冰弹。", 100, ColdUnlockWave, 10}
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
bool IsLogic(Machine type) { return type >= Machine::Switch && type <= Machine::Latch; }
bool CanUpgradeMachine(Machine type) { return type == Machine::Solar || type == Machine::Grower || type == Machine::Store || type == Machine::Cooler; }
const char* AmmoName(int filter) { return filter == 0 ? "普通" : filter == 1 ? "寒冰" : "混合"; }
int UpgradeCost(int baseCost, int currentLevel) {
    return currentLevel > 0 && currentLevel < MaxUpgradeLevel ? baseCost * currentLevel : 0;
}
int Capacity(Machine type, int level) {
    return type == Machine::Store ? StoreCapacities.at(level - 1) : Describe(type).capacity;
}
World::World() { Reset(); }

void World::Reset() {
    mCells = {}; mLanes = {}; mEnemyCounts = {};
    mPhase = Phase::Planning; mColumns = 10; mRows = 10; mHealth = 3; mCompletedWaves = 0;
    mEnergy = kInitialEnergy; mTransportClock = 0; mSpawnClock = 0;
    mRemainingSpawns = 0; mSpawned = 0;
    mProduced = 0; mFired = 0; mDelivered = 0; mCheckpoint = nullptr;
    mProcessed = 0; mIceFired = 0;
    // 两座集光器避免拆线耗尽能源后无法恢复；拆除仍返还原价。
    mCells[0].type = Machine::Solar;
    mCells[MaxColumns].type = Machine::Solar;
}
bool World::Valid(int i) const { return i >= 0 && i < CellCount && i / MaxColumns < mRows && i % MaxColumns < mColumns; }
bool World::Unlocked(Machine type) const {
    return type >= Machine::Empty && type < Machine::Count && Describe(type).unlockWave <= mCompletedWaves;
}
bool World::Enabled(int i) const {
    const int source = mCells[i].inputs[0];
    return source < 0 || mCells[source].signal;
}
bool World::Build(int i, Machine type, int direction) {
    if (!Valid(i) || mPhase == Phase::Dead || type <= Machine::Empty || !Unlocked(type)
        || direction < 0 || direction > 3 || mCells[i].type != Machine::Empty
        || mEnergy < Describe(type).cost) return false;
    mEnergy -= Describe(type).cost;
    mCells[i] = {}; mCells[i].type = type; mCells[i].direction = direction;
    mCells[i].route = (i / MaxColumns) % Rows;
    if (type == Machine::Feeder) mCells[i].filter = 0;
    if (type == Machine::Timer) mCells[i].setting = 4;
    return true;
}
bool World::Remove(int i) {
    if (!Valid(i) || mPhase == Phase::Dead || mCells[i].type == Machine::Empty) return false;
    const auto& c = mCells[i];
    int refund = Describe(c.type).cost;
    for (int level = 1; level < c.level; ++level) refund += UpgradeCost(Describe(c.type).cost, level);
    mEnergy = std::min(kEnergyLimit, mEnergy + refund);
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
    if (c.type == Machine::Grower || c.type == Machine::Cooler) c.clock *= GrowPeriods[c.level] / GrowPeriods[c.level - 1];
    ++c.level;
    return true;
}
bool World::Rotate(int i) {
    if (!Valid(i) || mPhase == Phase::Dead || mCells[i].type == Machine::Empty) return false;
    mCells[i].direction = (mCells[i].direction + 1) % 4; return true;
}
bool World::Connect(int source, int target, int input) {
    if (!Valid(target) || mPhase == Phase::Dead || input < 0 || input > 1
        || mCells[target].type == Machine::Empty) return false;
    if (source != -1 && (!Valid(source) || mCells[source].type == Machine::Empty)) return false;
    const auto targetType = mCells[target].type;
    if (input == 1 && targetType != Machine::And && targetType != Machine::Or && targetType != Machine::Latch) return false;
    if (source != -1 && !IsLogic(mCells[source].type) && targetType != Machine::StockSensor) return false;
    mCells[target].inputs[input] = source; return true;
}
bool World::Configure(int i) {
    if (!Valid(i) || mPhase == Phase::Dead) return false;
    auto& c = mCells[i];
    if (c.type == Machine::Switch) c.manual = !c.manual;
    else if (c.type == Machine::Timer) { c.setting = c.setting >= 16 ? 2 : c.setting * 2; c.clock = 0; }
    else if (c.type == Machine::StockSensor) c.setting = c.setting >= 40 ? 5 : c.setting * 2;
    else return false;
    return true;
}
bool World::SetRoute(int i, int row) {
    if (!Valid(i) || row < 0 || row >= Rows || mPhase == Phase::Dead) return false;
    const auto type = mCells[i].type;
    if (type != Machine::Feeder && type != Machine::EnemySensor && type != Machine::StockSensor) return false;
    mCells[i].route = row; return true;
}
bool World::SetFilter(int i, int filter) {
    if (!Valid(i) || filter < -1 || filter > 1 || mPhase == Phase::Dead) return false;
    const auto type = mCells[i].type;
    if (type != Machine::Feeder && type != Machine::Store && type != Machine::Splitter && type != Machine::StockSensor) return false;
    mCells[i].filter = filter; return true;
}
bool World::SetQuota(int row, int plainQuota) {
    if (row < 0 || row >= Rows || plainQuota < 0 || plainQuota > MagazineCapacity || mPhase == Phase::Dead) return false;
    mLanes[row].plainQuota = plainQuota; return true;
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
    mPhase = Phase::Assault; mSpawned = 0;
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
    if (cell.type == Machine::Cooler) return type == Ammo::Pea;
    if (cell.type == Machine::Store || cell.type == Machine::Feeder)
        return cell.filter < 0 || cell.filter == static_cast<int>(type);
    return cell.type == Machine::Belt || cell.type == Machine::Splitter;
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
            c.signal = mEnemyCounts[c.route] > 0; break;
        case Machine::StockSensor: {
            const auto& source = c.inputs[0] >= 0 ? before[c.inputs[0]] : c;
            const auto& lane = mLanes[c.route];
            const int plain = c.inputs[0] >= 0 ? source.stock : lane.ammo;
            const int ice = c.inputs[0] >= 0 ? source.iceStock : lane.iceAmmo;
            c.signal = (c.filter == 0 ? plain : c.filter == 1 ? ice : plain + ice) >= c.setting; break;
        }
        case Machine::And: c.signal = a && b; break;
        case Machine::Or: c.signal = a || b; break;
        case Machine::Not: c.signal = !a; break;
        case Machine::Latch: if (b) c.signal = false; else if (a) c.signal = true; break;
        case Machine::Timer:
            if (c.inputs[0] < 0 || a) c.clock = std::fmod(c.clock + TransportPeriod, static_cast<float>(c.setting));
            else c.clock = 0;
            c.signal = (c.inputs[0] < 0 || a) && c.clock >= c.setting * 0.5f; break;
        default: c.signal = false; break;
        }
    }
    std::array<std::array<int, 2>, CellCount> incoming{};
    for (int i = 0; i < CellCount; ++i) {
        auto& c = mCells[i];
        if (!Valid(i) || before[i].TotalStock() <= 0 || (!Enabled(i) && c.type != Machine::Splitter)) continue;
        bool moved = false;
        for (int choice = 0; choice < 2 && !moved; ++choice) {
            const Ammo ammo = (c.iceNext != (choice != 0)) ? Ammo::Ice : Ammo::Pea;
            if (before[i].Inventory(ammo) <= 0 || (c.type == Machine::Cooler && ammo != Ammo::Ice)) continue;
            if (c.type == Machine::Feeder) {
                if (c.filter >= 0 && c.filter != static_cast<int>(ammo)) continue;
                auto& lane = mLanes[c.route];
                if (lane.ammo + lane.iceAmmo < MagazineCapacity && lane.Inventory(ammo) < lane.Quota(ammo)) {
                    --c.Inventory(ammo); ++lane.Inventory(ammo); ++mDelivered; moved = true;
                }
            } else {
                const bool split = c.type == Machine::Splitter;
                const bool filtered = split && c.filter >= 0;
                const bool sideFirst = filtered ? c.filter == static_cast<int>(ammo)
                    : c.inputs[0] >= 0 ? mCells[c.inputs[0]].signal : c.alternate;
                for (int attempt = 0; attempt < (split && !filtered ? 2 : 1); ++attempt) {
                    const int direction = (c.direction + (split && (sideFirst != (attempt != 0)) ? 1 : 0)) % 4;
                    const int next = Neighbor(i, direction);
                    if (next < 0 || !Accepts(before[next], ammo)) continue;
                    const auto& target = before[next];
                    const int count = target.type == Machine::Cooler ? target.stock + incoming[next][0]
                        : target.TotalStock() + incoming[next][0] + incoming[next][1];
                    if (count >= (target.type == Machine::Cooler ? 5 : Capacity(target.type, target.level))) continue;
                    --c.Inventory(ammo); ++incoming[next][static_cast<int>(ammo)]; c.alternate = !c.alternate; moved = true; break;
                }
            }
            if (moved) c.iceNext = ammo == Ammo::Pea;
        }
    }
    for (int i = 0; i < CellCount; ++i) { mCells[i].stock += incoming[i][0]; mCells[i].iceStock += incoming[i][1]; }
}

void World::Tick(float seconds) {
    // 准备期只允许施工，不能靠等待囤阳光、弹药或推进电路；保留所有周期余时到开波。
    if (mPhase != Phase::Assault || !std::isfinite(seconds) || seconds <= 0) return;
    // 有限子步避免游戏倍速改变物流/战斗结果，并允许纯逻辑测试推进长时间。
    while (seconds > 0.000001f && mPhase == Phase::Assault) {
        const float dt = std::min(seconds, 1.0f / 60.0f); seconds -= dt;
        for (int i = 0; i < CellCount; ++i) {
            auto& c = mCells[i];
            if (!Enabled(i)) continue;
            if (c.type == Machine::Solar) mEnergy = std::min(kEnergyLimit, mEnergy + SolarRates[c.level - 1] * dt);
            if (c.type == Machine::Grower) {
                const float period = GrowPeriods[c.level - 1];
                // 正常生产保留跨周期余时，避免逐帧截断让各级产速持续偏慢；堵塞只缓存一颗的准备进度。
                c.clock = std::min(period, c.clock) + dt;
                if (c.clock >= period && c.stock < Capacity(c.type, c.level) && mEnergy >= GrowEnergy) {
                    c.clock -= period; mEnergy -= GrowEnergy; ++c.stock; ++mProduced;
                } else c.clock = std::min(period, c.clock);
            }
            if (c.type == Machine::Cooler && c.stock > 0) {
                const float period = GrowPeriods[c.level - 1];
                c.clock = std::min(period, c.clock) + dt;
                if (c.clock >= period && c.iceStock < 5 && mEnergy >= CoolingEnergy) {
                    c.clock -= period; --c.stock; ++c.iceStock; mEnergy -= CoolingEnergy; ++mProcessed;
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
    if (mPhase != Phase::Assault || row < 0 || row >= Rows || mLanes[row].Inventory(type) <= 0) return false;
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
    mPhase = Phase::Planning; ++mCompletedWaves;
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
    if (!Enabled(i) && c.type != Machine::Splitter) return "信号关闭";
    if (c.type == Machine::Solar) return mEnergy >= kEnergyLimit ? "储能已满" : "采集阳光";
    if (c.type == Machine::Grower && mEnergy < GrowEnergy) return "缺少能源";
    if (c.type == Machine::Cooler) {
        if (c.iceStock >= 5) return "寒冰出口堵塞";
        if (c.stock == 0) return "等待普通豌豆";
        return mEnergy < CoolingEnergy ? "缺少能源" : "冷凝加工中";
    }
    if (c.TotalStock() == 0) return c.type == Machine::Grower ? "生产中" : "等待弹药";
    if (c.type == Machine::Feeder) {
        const auto& lane = mLanes[c.route];
        if (lane.ammo + lane.iceAmmo >= MagazineCapacity) return "前线弹仓已满";
        const bool plain = c.stock > 0 && c.filter != 1, ice = c.iceStock > 0 && c.filter != 0;
        if (!plain && !ice) return "等待指定弹种";
        return (!plain || lane.ammo >= lane.plainQuota) && (!ice || lane.iceAmmo >= MagazineCapacity - lane.plainQuota)
            ? "对应弹种配额已满" : "装填中";
    }
    const int next = Neighbor(i, c.direction);
    if (c.type != Machine::Splitter && (next < 0 || Capacity(mCells[next].type, mCells[next].level) == 0
        || mCells[next].TotalStock() >= Capacity(mCells[next].type, mCells[next].level))) return "出口堵塞";
    return "输送中";
}

nlohmann::json World::SaveState() const {
    nlohmann::json cells = nlohmann::json::array(), lanes = nlohmann::json::array();
    for (const auto& c : mCells) cells.push_back({{"type", Describe(c.type).key}, {"level", c.level}, {"direction", c.direction}, {"stock", c.stock},
        {"iceStock", c.iceStock}, {"route", c.route}, {"filter", c.filter}, {"iceNext", c.iceNext},
        {"inputs", c.inputs}, {"setting", c.setting}, {"clock", c.clock}, {"signal", c.signal}, {"manual", c.manual}, {"alternate", c.alternate}});
    for (const auto& l : mLanes) lanes.push_back({{"ammo", l.ammo}, {"iceAmmo", l.iceAmmo}, {"plainQuota", l.plainQuota}, {"shooters", l.shooters}});
    return {{"phase", static_cast<int>(mPhase)}, {"columns", mColumns}, {"rows", mRows}, {"health", mHealth}, {"completedWaves", mCompletedWaves},
        {"energy", mEnergy}, {"transportClock", mTransportClock}, {"spawnClock", mSpawnClock}, {"remainingSpawns", mRemainingSpawns},
        {"spawned", mSpawned}, {"produced", mProduced}, {"fired", mFired}, {"delivered", mDelivered},
        {"processed", mProcessed}, {"iceFired", mIceFired},
        {"cells", cells}, {"lanes", lanes}};
}
nlohmann::json World::Save() const { return {{"schemaVersion", 2}, {"state", SaveState()}, {"checkpoint", mCheckpoint}}; }

/** 所有实体与信号索引在临时 World 中验证，避免坏档引入越界或部分覆盖。 */
void World::ReadState(const nlohmann::json& s) {
    mPhase = static_cast<Phase>(ReadInt(s, "phase", 0, 2));
    mColumns = ReadInt(s, "columns", 10, MaxColumns); Require(mColumns % 2 == 0, "无效工厂宽度");
    mRows = ReadInt(s, "rows", 10, MaxRows); Require(mRows % 2 == 0, "无效工厂高度");
    mHealth = ReadInt(s, "health", 0, 3); Require((mHealth == 0) == (mPhase == Phase::Dead), "死亡状态不一致");
    mCompletedWaves = ReadInt(s, "completedWaves", 0, 100000);
    mEnergy = ReadFloat(s, "energy", 0, kEnergyLimit);
    mTransportClock = ReadFloat(s, "transportClock", 0, TransportPeriod);
    mSpawnClock = ReadFloat(s, "spawnClock", -1, 3);
    mRemainingSpawns = ReadInt(s, "remainingSpawns", 0, 105);
    mSpawned = ReadInt(s, "spawned", 0, 105);
    mProduced = ReadInt(s, "produced", 0, 1000000000); mFired = ReadInt(s, "fired", 0, 1000000000); mDelivered = ReadInt(s, "delivered", 0, 1000000000);
    mProcessed = ReadInt(s, "processed", 0, 1000000000); mIceFired = ReadInt(s, "iceFired", 0, 1000000000);
    const auto& cells = s.at("cells"); Require(cells.is_array() && cells.size() == CellCount, "工厂格数错误");
    for (int i = 0; i < CellCount; ++i) {
        const auto& j = cells[i]; auto& c = mCells[i];
        c.type = FindMachine(j.at("type").get<std::string>()); Require(c.type < Machine::Count, "未知机器");
        Require(Unlocked(c.type) && (Valid(i) || c.type == Machine::Empty), "机器尚未解锁或在地界外");
        c.level = j.contains("level") ? ReadInt(j, "level", 1, MaxUpgradeLevel) : 1;
        Require(c.level == 1 || (CanUpgradeMachine(c.type) && mCompletedWaves >= UpgradeWaves[c.level - 1]), "机器等级尚未解锁或类型不支持升级");
        c.direction = ReadInt(j, "direction", 0, 3); c.stock = ReadInt(j, "stock", 0, Capacity(c.type, c.level));
        c.iceStock = ReadInt(j, "iceStock", 0, Capacity(c.type, c.level));
        Require(c.TotalStock() <= Capacity(c.type, c.level), "混合库存超过设备容量");
        Require(c.type != Machine::Cooler || (c.stock <= 5 && c.iceStock <= 5), "冷凝机输入输出缓冲越界");
        c.route = ReadInt(j, "route", 0, Rows - 1); c.filter = ReadInt(j, "filter", -1, 1); c.iceNext = ReadBool(j, "iceNext");
        c.setting = ReadInt(j, "setting", 1, 80); c.clock = ReadFloat(j, "clock", 0, 80);
        c.signal = ReadBool(j, "signal"); c.manual = ReadBool(j, "manual"); c.alternate = ReadBool(j, "alternate");
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
        Require(IsLogic(mCells[source].type) || mCells[i].type == Machine::StockSensor, "无效信号来源");
        Require(p == 0 || mCells[i].type == Machine::And || mCells[i].type == Machine::Or || mCells[i].type == Machine::Latch, "不存在的 B 口");
    }
    const auto& lanes = s.at("lanes"); Require(lanes.is_array() && lanes.size() == Rows, "前线行数错误");
    for (int i = 0; i < Rows; ++i) {
        mLanes[i].ammo = ReadInt(lanes[i], "ammo", 0, MagazineCapacity); mLanes[i].shooters = ReadInt(lanes[i], "shooters", 0, MaxShootersPerLane);
        mLanes[i].iceAmmo = ReadInt(lanes[i], "iceAmmo", 0, MagazineCapacity);
        mLanes[i].plainQuota = ReadInt(lanes[i], "plainQuota", 0, MagazineCapacity);
        Require(mLanes[i].ammo + mLanes[i].iceAmmo <= MagazineCapacity, "前线混合弹仓超出总容量");
    }
    Require(mPhase != Phase::Planning || mRemainingSpawns == 0, "准备阶段存在未完成生成队列");

}
bool World::Load(const nlohmann::json& document, std::string& error) {
    try {
        const int version = ReadInt(document, "schemaVersion", 1, 2);
        // 旧厂房固定 5×12 存储跨度；同时重映射信号引用和袭击检查点，已买到的第 11/12 列保留。
        auto migrate = [version](nlohmann::json state) {
            if (version == 2 || state.is_null()) return state;
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
    j["machineCount"] = std::ranges::count_if(mCells, [](const Cell& c) { return c.type != Machine::Empty; });
    j["unlocked"] = nlohmann::json::array();
    for (int i = 1; i < static_cast<int>(Machine::Count); ++i) if (Unlocked(static_cast<Machine>(i))) j["unlocked"].push_back(Describe(static_cast<Machine>(i)).key);
    return j;
}
}
