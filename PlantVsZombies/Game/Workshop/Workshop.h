#pragma once
#include <array>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

/** 工坊的独立模拟；不访问 Board、冒险解锁或玩家存档。时间单位为游戏秒。 */
namespace Workshop {
inline constexpr int Level = -100; // 独立 Board 身份，不占用冒险、生存或小游戏编号
inline constexpr int Rows = 5; // 前线行数，工厂同一行的装填口供应对应防线
inline constexpr int MaxColumns = 12; // 工厂扩建后的最大列数
inline constexpr int CellCount = Rows * MaxColumns;
inline constexpr int MagazineCapacity = 80; // 每行共用弹仓的豌豆数量上限
inline constexpr int ShooterCost = 150; // 每株新增射手的阳光费用
inline constexpr int MaxShootersPerLane = 3; // 每行同时部署的射手上限
inline constexpr int RepairCost = 100; // 修复一点基地生命的阳光费用
inline constexpr int ExpansionCost = 250; // 扩建两列的阳光费用
inline constexpr float GrowPeriod = 2.0f; // 基础育豆机制造一颗豌豆的间隔，单位：秒
inline constexpr float GrowEnergy = 2.0f; // 每颗豌豆消耗的阳光能源
inline constexpr float SolarRate = 1.5f; // 基础集光器每秒产生的阳光能源
inline constexpr float TransportPeriod = 0.25f; // 基础物流与电路节拍，单位：秒
inline constexpr int MaxUpgradeLevel = 3; // 每台机器或每株射手的等级上限
inline constexpr std::array<float, 3> GrowPeriods{GrowPeriod, 1.6f, 1.3f}; // 各级育豆机生产间隔，单位：秒/颗
inline constexpr std::array<float, 3> SolarRates{SolarRate, 2.25f, 3.0f}; // 各级集光器产能，单位：阳光/秒
inline constexpr std::array<int, 3> StoreCapacities{60, 120, 200}; // 各级弹药仓容量，单位：颗
inline constexpr std::array<float, 3> ShooterCooldowns{1.5f, 1.2f, 0.9f}; // 各级射手基础冷却，单位：秒；索敌与动画同比例提速
inline constexpr std::array<int, 3> UpgradeWaves{0, 2, 5}; // 达到对应等级前必须守住的累计波次
enum class Machine { Empty, Solar, Grower, Belt, Splitter, Store, Feeder,
    Switch, EnemySensor, StockSensor, And, Or, Not, Timer, Latch, Count };
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

struct Cell {
    Machine type = Machine::Empty;
    int level = 1;
    int direction = 0;
    int stock = 0;
    std::array<int, 2> inputs{ -1, -1 };
    int setting = 10;
    float clock = 0;
    bool signal = false;
    bool manual = true;
    bool alternate = false;
};
struct Lane {
    int ammo = 0;
    int shooters = 1;
};

class World {
public:
    World();
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
    bool AddShooter(int row);
    bool Expand();
    bool Repair();
    bool StartAssault();
    /** 败局恢复开战前基地；不清空解锁、不重播已经结算的奖励。 */
    bool Retry();
    /** 真实 Board 提交的前线观测，仅用于库存器、敌情器和 UI；不模拟实体。 */
    void ObserveFrontline(const std::array<int, Rows>& enemies, const std::array<int, Rows>& shooters);
    bool ReserveAmmo(int row);
    void RecordShot() { ++mFired; }
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
    int mColumns = 8, mHealth = 3, mCompletedWaves = 0;
    int mRemainingSpawns = 0, mSpawned = 0;
    int mProduced = 0, mFired = 0, mDelivered = 0;
    float mEnergy = 300, mTransportClock = 0, mSpawnClock = 0;
    nlohmann::json mCheckpoint;
    bool Valid(int index) const;
    int Neighbor(int index, int direction) const;
    void TransportStep();
    nlohmann::json SaveState() const;
    void ReadState(const nlohmann::json& state);
};
}
