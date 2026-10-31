#pragma once
#include <array>
#include <cstdint>
#include <numeric>
#include <map>
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
inline constexpr int FrontColumns = 6; // 前线可种植的列数，每格一株主植物
inline constexpr int MaxShootersPerLane = FrontColumns; // 旧增援入口也服从同时占格上限
inline constexpr int RepairCost = 100; // 修复一点基地生命的阳光费用
inline constexpr int ExpansionCost = 250; // 工厂宽、高各扩建两格的阳光费用，分别不超过 20 格
inline constexpr float GrowPeriod = 2.5f; // 基础育豆机制造一颗豌豆的间隔，单位：秒
inline constexpr float GrowEnergy = 2.0f; // 每颗豌豆消耗的阳光能源
inline constexpr float SolarRate = 1.2f; // 基础集光器每秒产生的阳光能源
inline constexpr float TransportPeriod = 0.25f; // 基础物流与电路节拍，单位：秒
inline constexpr int MaxUpgradeLevel = 3; // 每台机器或每株射手的等级上限
inline constexpr std::array<float, 3> GrowPeriods{GrowPeriod, 2.0f, 1.6f}; // 各级育豆机与冷凝机生产间隔，单位：秒/颗
inline constexpr std::array<float, 3> SolarRates{SolarRate, 1.8f, 2.4f}; // 各级集光器产能，单位：阳光/秒
inline constexpr std::array<int, 3> StoreCapacities{60, 120, 200}; // 各级弹药仓容量，单位：颗
inline constexpr std::array<float, 3> ShooterCooldowns{1.5f, 1.2f, 0.9f}; // 各级射手基础冷却，单位：秒；索敌与动画同比例提速
inline constexpr std::array<int, 3> UpgradeWaves{0, 2, 5}; // 达到对应等级前必须守住的累计波次
enum class Machine { Empty, Solar, Grower, Belt, Splitter, Store, Feeder,
    Switch, EnemySensor, StockSensor, And, Or, Not, Timer, Latch, Cooler,
    SporeGrower, MelonGrower, SalvagePort, Recycler, Counter, Cannon, Count };
// 前五类进入前线弹仓，残骸只参与工厂物流；追加枚举不改变旧弹种编号。
enum class Ammo { Pea, Ice, Spore, Melon, IceMelon, Scrap, Count };
inline constexpr int AmmoCount = static_cast<int>(Ammo::Scrap);
inline constexpr int ItemCount = static_cast<int>(Ammo::Count);
inline constexpr int ScrapPerWave = 40; // 每波全基地累计残骸掉落上限，召唤物也共用
inline constexpr float ScrapEnergy = 5; // 每份残骸完成回收获得的阳光
inline constexpr std::array<float, 3> SporePeriods{0.6f, 0.45f, 0.35f}; // 孢子培养间隔，秒/份
inline constexpr std::array<float, 3> MelonPeriods{4.0f, 3.2f, 2.5f}; // 瓜果培养间隔，秒/份
inline constexpr std::array<float, 3> RecyclePeriods{1.5f, 1.2f, 1.0f}; // 残骸回收间隔，秒/份
inline constexpr float SporeEnergy = 0.5f; // 每份孢子的阳光成本
inline constexpr float MelonEnergy = 8; // 每颗西瓜的阳光成本
inline constexpr float IceMelonEnergy = 6; // 每颗西瓜冷凝为冰瓜的额外阳光成本
inline constexpr float CannonChargeSeconds = 12; // 炮台从新建或上一轮齐射起充能的游戏秒数
inline constexpr float CannonShotInterval = .18f; // 一轮三发之间的游戏秒数
inline constexpr int CannonVolleySize = 3; // 每轮预留并发射的对应瓜弹数量
inline constexpr int CannonBufferPerAmmo = 6; // 每种瓜弹的缓存容量，不含已预留的齐射弹药
enum class Phase { Planning, Assault, Dead };
enum class CounterMode { Signal, Produced, Transferred };
enum class CounterOutput { Hold, Toggle };

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
bool HasDirectionalOutput(Machine type);
/** 物料设备的 B 口用于选择两套工作方案，A 口仍负责运行许可。 */
bool HasSelectionInput(Machine type);
const char* DirectionName(int direction);
bool CanUpgradeMachine(Machine type);
/** 当前等级升一级的阳光费用；满级或无效等级返回 0。 */
int UpgradeCost(int baseCost, int currentLevel);
int Capacity(Machine type, int level = 1);
const char* AmmoName(int filter);
/** 生产和升级共用的周期，秒；非生产设备返回 0。 */
float ProductionPeriod(Machine type, int level);
Ammo ProducedItem(Machine type);

struct Cell {
    Machine type = Machine::Empty;
    int level = 1;
    int direction = 0;
    int stock = 0;
    int iceStock = 0;
    int route = 0;
    int filter = -1; // -1 混合，0 普通，1 寒冰；筛选分流器把匹配弹种送向右侧
    bool iceNext = false; // 保留旧档兼容投影；物流轮询由 nextItem 持有
    std::array<int, ItemCount - 2> extraStock{};
    int nextItem = 0, recipe = 0; // recipe: 冷凝机 0 冰豌豆，1 冰瓜
    int plan = 0; // 未连接选择口时使用的手动方案，0/1；冷凝机 recipe 保留当前在制品身份
    int route2 = 0, filter2 = -1; // 装填口/炮台的第二套目标路线与弹种
    bool strictSplit = false, processing = false; // 严格选路；冷凝机当前已有一份正在加工
    int paidCost = 0; // 累计实际建造/升级支出，调价后拆除仍按原支付额退款
    CounterMode counterMode = CounterMode::Signal;
    CounterOutput counterOutput = CounterOutput::Hold;
    std::array<std::uint64_t, ItemCount> producedItems{}, transferredItems{};
    std::uint64_t counterObserved = 0; // 已读取的来源累计量，切来源/筛选或复位时重新对齐
    int volleyRemaining = 0, volleyRoute = 0;
    Ammo volleyAmmo = Ammo::Melon; // 一轮齐射锁定路线与弹种，选择口变化留到下一轮
    float volleyClock = 0;
    int count = 0;
    bool previousInput = false;
    std::array<int, 2> inputs{ -1, -1 };
    int setting = 10;
    float clock = 0;
    bool signal = false;
    bool manual = true;
    bool alternate = false;
    int TotalStock() const { return stock + iceStock + std::accumulate(extraStock.begin(), extraStock.end(), 0); }
    int& Inventory(Ammo type) { return type == Ammo::Pea ? stock : type == Ammo::Ice ? iceStock : extraStock.at(static_cast<int>(type) - 2); }
    int Inventory(Ammo type) const { return type == Ammo::Pea ? stock : type == Ammo::Ice ? iceStock : extraStock.at(static_cast<int>(type) - 2); }
};
struct Lane {
    int ammo = 0;
    int iceAmmo = 0;
    int plainQuota = 60; // 总弹仓 80 颗，剩余额度预留给寒冰弹；调额不销毁已有库存
    int shooters = 1;
    int iceQuota = 20;
    std::array<int, AmmoCount - 2> extraAmmo{}, extraQuota{};
    int TotalStock() const { return ammo + iceAmmo + std::accumulate(extraAmmo.begin(), extraAmmo.end(), 0); }
    int& Inventory(Ammo type) { return type == Ammo::Pea ? ammo : type == Ammo::Ice ? iceAmmo : extraAmmo.at(static_cast<int>(type) - 2); }
    int Inventory(Ammo type) const { return type == Ammo::Pea ? ammo : type == Ammo::Ice ? iceAmmo : extraAmmo.at(static_cast<int>(type) - 2); }
    int& Quota(Ammo type) { return type == Ammo::Pea ? plainQuota : type == Ammo::Ice ? iceQuota : extraQuota.at(static_cast<int>(type) - 2); }
    int Quota(Ammo type) const { return type == Ammo::Pea ? plainQuota : type == Ammo::Ice ? iceQuota : extraQuota.at(static_cast<int>(type) - 2); }
};
/** 当前出料口的只读诊断；ready 表示目标此刻可接收，不预留容量或推进物流。 */
struct OutputInfo {
    int target = -1;
    bool ready = false;
    std::string message;
};

class World {
public:
    World();
    /** 建造预览与正式事务共用的只读检查，包含格位、占用、方向、解锁及阳光资格。 */
    bool CanBuild(int index, Machine type, int direction) const;
    /** 玩家建造事务；拒绝时状态和资源不变。 */
    bool Build(int index, Machine type, int direction);
    bool Remove(int index);
    int RemoveRefund(int index) const;
    /** 升级保留库存、线路和生产周期完成比例；失败不扣费。 */
    bool Upgrade(int index);
    bool CanBuyUpgrade(int baseCost, int currentLevel) const;
    /** 共享的升级付款事务；射手自身拥有并保存等级。 */
    bool BuyUpgrade(int baseCost, int currentLevel);
    bool Rotate(int index);
    bool Connect(int source, int target, int input);
    bool CanConnect(int source, int target, int input) const;
    bool Configure(int index);
    /** plan 为 -1 时编辑当前生效方案，0/1 时明确编辑指定方案。 */
    bool SetRoute(int index, int row, int plan = -1);
    bool SetFilter(int index, int filter, int plan = -1);
    bool SetPlan(int index, int plan);
    bool SetStrictSplit(int index, bool strict);
    /** 计量配置改变会清零并对齐来源累计量；不兼容的 A 来源自动断开，不补算旧产出。 */
    bool SetCounterMode(int index, CounterMode mode);
    bool SetCounterOutput(int index, CounterOutput output);
    bool SetSetting(int index, int setting);
    /** 当前逻辑时刻的控制选择；不推进机器或信号。 */
    int ActivePlan(int index) const;
    int ActiveRoute(int index) const;
    int ActiveFilter(int index) const;
    /** Board 先确认合法目标再预留一轮；手动请求只绕过待机/运行口，不绕过冷却和库存。 */
    bool BeginCannonVolley(int index, bool manualRequest = false);
    bool CannonShotReady(int index) const;
    /** 真实弹丸创建成功后消费一个预留名额，同时登记出库计数。 */
    void CommitCannonShot(int index);
    bool SetQuota(int row, int plainQuota);
    /** 修改一种弹药配额，优先从最大其他配额转移；不销毁既有库存，总配额固定为弹仓容量。 */
    bool SetAmmoQuota(int row, Ammo type, int quota);
    /** 首次部署一种弹药消费者时，若当前配额为零，自动从其他配额拨出空间。 */
    void EnsureAmmoQuota(int row, Ammo type);
    bool SetRecipe(int index, int recipe);
    bool SpendEnergy(float amount);
    void RefundEnergy(float amount);
    /** 仅由真实战斗死亡入口调用；越线和场景销毁不得调用。 */
    void RecordKill();
    int PendingScrap() const { return mPendingScrap; }
    int WaveScrap() const { return mWaveScrap; }
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
    /** 同种植物共享补种冷却，单位游戏秒；准备阶段返回零且不启动冷却。 */
    float PlantCooldown(const std::string& key) const;
    void StartPlantCooldown(const std::string& key, float seconds);
    /** 当前库存或真实上游来料与接收配置不符时的只读说明；不丢料或自动改方案。 */
    std::string MaterialWarning(int index) const;
    void Reset();
    /** 独立 schema 文档含当前局面和本次袭击前检查点。 */
    nlohmann::json Save() const;
    /** 完整预检后提交；坏档/未来版本返回 false，保持当前对象不变。 */
    bool Load(const nlohmann::json& document, std::string& error);
    nlohmann::json Inspect() const;
    std::string Status(int index) const;
    /** 准备阶段也可检查线路；side 仅用于分流器相对右侧的出口。 */
    OutputInfo InspectOutput(int index, bool side = false) const;
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
    std::map<std::string, float> mPlantCooldowns;
    Phase mPhase = Phase::Planning;
    int mColumns = 10, mRows = 10, mHealth = 3, mCompletedWaves = 0;
    int mRemainingSpawns = 0, mSpawned = 0;
    int mProduced = 0, mFired = 0, mDelivered = 0;
    int mProcessed = 0, mIceFired = 0;
    int mPendingScrap = 0, mWaveScrap = 0, mRecycled = 0;
    float mEnergy = 300, mTransportClock = 0, mSpawnClock = 0;
    nlohmann::json mCheckpoint;
    bool Valid(int index) const;
    int Neighbor(int index, int direction) const;
    void TransportStep();
    bool Accepts(const Cell& cell, Ammo type) const;
    int ReceivingRoom(const Cell& cell, Ammo type) const;
    bool CanCountSource(int source, CounterMode mode) const;
    int SelectedPlan(const Cell& cell) const;
    std::uint64_t CounterSourceTotal(const Cell& counter) const;
    void ResetCounterObservation(Cell& counter);
    nlohmann::json SaveState() const;
    void ReadState(const nlohmann::json& state);
};
}
