#pragma once
#include <array>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

/** 工坊的独立模拟；不访问 Board、冒险解锁或玩家存档。时间单位为游戏秒。 */
namespace Workshop {
inline constexpr int Level = -100; // 独立 Board 身份，不占用冒险、生存或小游戏编号
inline constexpr int Rows = 5; // 前线行数，与独立工厂的行数无关
inline constexpr int MaxColumns = 20; // 工厂最大列数；同时作为稳定格位索引的行跨度
inline constexpr int MaxRows = 20; // 工厂最大行数
inline constexpr int CellCount = MaxRows * MaxColumns;
inline constexpr int MagazineCapacity = 80; // 每行共用弹仓的豌豆数量上限
inline constexpr int ShooterCost = 150; // 每株新增射手的阳光费用
inline constexpr int IceShooterCost = 200; // 每株寒冰射手的阳光费用
inline constexpr int ColdUnlockWave = 3; // 守住此波后开放寒冰生产线和寒冰射手
inline constexpr float CoolingEnergy = 2.0f; // 每颗普通豌豆加工为寒冰弹额外消耗的阳光
inline constexpr int MaxShootersPerLane = 3; // 每行同时部署的射手上限
inline constexpr int RepairCost = 100; // 修复一点基地生命的阳光费用
inline constexpr int ExpansionCost = 250; // 工厂宽、高各扩建两格的阳光费用，分别不超过 20 格
inline constexpr float GrowPeriod = 2.5f; // 基础育豆机制造一颗豌豆的间隔，单位：秒
inline constexpr float GrowEnergy = 2.0f; // 每颗豌豆消耗的阳光能源
inline constexpr float SolarRate = 1.5f; // 基础集光器每秒产生的阳光能源
inline constexpr float TransportPeriod = 0.25f; // 基础物流与电路节拍，单位：秒
inline constexpr int MaxUpgradeLevel = 3; // 每台机器或每株射手的等级上限
inline constexpr std::array<float, 3> GrowPeriods{GrowPeriod, 2.0f, 1.6f}; // 各级育豆机与冷凝机生产间隔，单位：秒/颗
inline constexpr std::array<float, 3> SolarRates{SolarRate, 2.25f, 3.0f}; // 各级集光器产能，单位：阳光/秒
inline constexpr std::array<int, 3> StoreCapacities{60, 120, 200}; // 各级弹药仓容量，单位：颗
inline constexpr std::array<float, 3> ShooterCooldowns{1.5f, 1.2f, 0.9f}; // 各级射手基础冷却，单位：秒；索敌与动画同比例提速
inline constexpr std::array<int, 3> UpgradeWaves{0, 2, 5}; // 达到对应等级前必须守住的累计波次
enum class Machine { Empty, Solar, Grower, Belt, Splitter, Store, Feeder,
    Switch, EnemySensor, StockSensor, And, Or, Not, Timer, Latch, Cooler, Count };
enum class Ammo { Pea, Ice };
enum class Phase { Planning, Assault, Dead };

struct Definition {
    const char* key;
    const char* name;
    const char* description;
    int cost;
    int unlockWave;
    int capacity;
};
const Definition& Describe(Machine type);
Machine FindMachine(const std::string& key);
bool IsLogic(Machine type);
bool CanUpgradeMachine(Machine type);
/** 当前等级升一级的阳光费用；满级或无效等级返回 0。 */
int UpgradeCost(int baseCost, int currentLevel);
int Capacity(Machine type, int level = 1);
const char* AmmoName(int filter);

struct Cell {
    Machine type = Machine::Empty;
    int level = 1;
    int direction = 0;
    int stock = 0;
    int iceStock = 0;
    int route = 0;
    int filter = -1; // -1 混合，0 普通，1 寒冰；筛选分流器把匹配弹种送向右侧
    bool iceNext = false;
    std::array<int, 2> inputs{ -1, -1 };
    int setting = 10;
    float clock = 0;
    bool signal = false;
    bool manual = true;
    bool alternate = false;
    int TotalStock() const { return stock + iceStock; }
    int& Inventory(Ammo type) { return type == Ammo::Ice ? iceStock : stock; }
    int Inventory(Ammo type) const { return type == Ammo::Ice ? iceStock : stock; }
};
struct Lane {
    int ammo = 0;
    int iceAmmo = 0;
    int plainQuota = 60; // 总弹仓 80 颗，剩余额度预留给寒冰弹；调额不销毁已有库存
    int shooters = 1;
    int& Inventory(Ammo type) { return type == Ammo::Ice ? iceAmmo : ammo; }
    int Inventory(Ammo type) const { return type == Ammo::Ice ? iceAmmo : ammo; }
    int Quota(Ammo type) const { return type == Ammo::Ice ? MagazineCapacity - plainQuota : plainQuota; }
};

class World {
public:
    World();
    /** 建造预览与正式事务共用的只读检查，包含格位、占用、方向、解锁及阳光资格。 */
    bool CanBuild(int index, Machine type, int direction) const;
    /** 玩家建造事务；拒绝时状态和资源不变。 */
    bool Build(int index, Machine type, int direction);
    bool Remove(int index);
    /** 升级保留库存、线路和生产周期完成比例；失败不扣费。 */
    bool Upgrade(int index);
    bool CanBuyUpgrade(int baseCost, int currentLevel) const;
    /** 共享的升级付款事务；射手自身拥有并保存等级。 */
    bool BuyUpgrade(int baseCost, int currentLevel);
    bool Rotate(int index);
    bool Connect(int source, int target, int input);
    bool Configure(int index);
    bool SetRoute(int index, int row);
    bool SetFilter(int index, int filter);
    bool SetQuota(int row, int plainQuota);
    bool AddShooter(int row, bool ice = false);
    bool Expand();
    bool Repair();
    bool StartAssault();
    /** 败局恢复开战前基地；不清空解锁、不重播已经结算的奖励。 */
    bool Retry();
    /** 真实 Board 提交的前线观测，仅用于库存器、敌情器和 UI；不模拟实体。 */
    void ObserveFrontline(const std::array<int, Rows>& enemies, const std::array<int, Rows>& shooters);
    bool ReserveAmmo(int row, Ammo type = Ammo::Pea);
    void RecordShot(Ammo type = Ammo::Pea) { ++mFired; if (type == Ammo::Ice) ++mIceFired; }
    int NextSpawnRow() const;
    void CommitSpawn();
    void CompleteAssault();
    void RecordBreach();
    int RemainingSpawns() const { return mRemainingSpawns; }
    int Spawned() const { return mSpawned; }
    void Tick(float seconds);
    void Reset();
    /** 独立 schema 文档含当前局面和本次袭击前检查点。 */
    nlohmann::json Save() const;
    /** 完整预检后提交；坏档/未来版本返回 false，保持当前对象不变。 */
    bool Load(const nlohmann::json& document, std::string& error);
    nlohmann::json Inspect() const;
    std::string Status(int index) const;
    bool Unlocked(Machine type) const;
    bool Enabled(int index) const;
    const std::array<Cell, CellCount>& Cells() const { return mCells; }
    const std::array<Lane, Rows>& Lanes() const { return mLanes; }
    const std::array<int, Rows>& EnemyCounts() const { return mEnemyCounts; }
    Phase State() const { return mPhase; }
    int CompletedWaves() const { return mCompletedWaves; }
    int Columns() const { return mColumns; }
    int FactoryRows() const { return mRows; }
    int Health() const { return mHealth; }
    float Energy() const { return mEnergy; }
    int Produced() const { return mProduced; }
    int Fired() const { return mFired; }
    int Delivered() const { return mDelivered; }

private:
    std::array<Cell, CellCount> mCells{};
    std::array<Lane, Rows> mLanes{};
    std::array<int, Rows> mEnemyCounts{};
    Phase mPhase = Phase::Planning;
    int mColumns = 10, mRows = 10, mHealth = 3, mCompletedWaves = 0;
    int mRemainingSpawns = 0, mSpawned = 0;
    int mProduced = 0, mFired = 0, mDelivered = 0;
    int mProcessed = 0, mIceFired = 0;
    float mEnergy = 300, mTransportClock = 0, mSpawnClock = 0;
    nlohmann::json mCheckpoint;
    bool Valid(int index) const;
    int Neighbor(int index, int direction) const;
    void TransportStep();
    bool Accepts(const Cell& cell, Ammo type) const;
    nlohmann::json SaveState() const;
    void ReadState(const nlohmann::json& state);
};
}
