#pragma once
#include "Game/Board/WeatherStationRules.h"
#include "Game/Zombie/ZombieMovementRules.h"
#include "Game/Zombie/GoldenIceRules.h"
#include "Game/Zombie/DancerRules.h"
#include "Game/Zombie/JackBoxRules.h"
#include "Game/Zombie/HealerRules.h"
#include "Game/Zombie/BalloonRules.h"
#include "Game/Zombie/DiggerRules.h"
#include "Game/Zombie/LadderRules.h"
#include "Game/Plant/ThunderFlowerRules.h"
#include "Game/Plant/MendingCottonRules.h"
#include "Game/Plant/RainBambooRules.h"
#include "Game/Zombie/FloodMortarRules.h"
#include "Game/Zombie/PressureShooterRules.h"
#include "Game/Zombie/DisasterEngineerRules.h"

#include "ColdStorageStrategy.h"
#include "ColdStorageDiagnostics.h"
#include "Game/PlantDamageOrigin.h"
#include "Game/Board/IceProduction.h"
#include "Game/Plant/AttackGrowth.h"
#include "Game/Plant/PlanternRules.h"
#include "Game/Plant/IceStorageNutRules.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>

/** 可训练的编队搜索。仅消费数值快照；预测既不扣款也不使用正式游戏随机数。 */
namespace ColdStorageSearch {
inline constexpr int FeatureCount = 8;
inline constexpr int FuelAwarePlanternResponse = 4; // 燃料充足用III挡、低油用II挡的合法玩家应对；不是实际挡位
using Weights = std::array<float, FeatureCount>;
inline constexpr int ContextCount = 8; // 偏置、友军伤势、友军密度、前墙、减速、火力、工人数、后排保护
using ContextWeights = std::array<float, ContextCount>;
inline constexpr int StateFeatureCount = 6; // 库存、预计生产、植物火力、植物经济、可用反制、无击杀时长
using StateFeatures = std::array<float, StateFeatureCount>;
/** 随局势调整收益评分的可训练线性层；零系数不指定任何攒钱门槛或兵种编队。 */
struct StateModel {
	std::array<Weights, StateFeatureCount> coefficients{};
	/** 每项系数须有限且在与基础评分相同的数值域内。 */
	bool IsValid() const;
};
inline constexpr int ProductionFeatureCount = 10; // 产冰预测、现有/新工人、生命、护卫、护卫投入、火力、减速、墙、库存
using ProductionFeatures = std::array<float, ProductionFeatureCount>;
/** 小型实战校准树，只折减简化推演的产冰预期；不生成资源或改变单位。 */
struct ProductionCalibration {
	struct Node { int feature = -1, left = -1, right = -1; float threshold = 0, value = 1; };
	std::vector<Node> nodes;
	/** 加载时检查有界、有限值和无环拓扑。 */
	bool IsValid() const;
	/** 对已通过 IsValid 的只读树推断倍率；不得传入尚未验证的资源。 */
	float Predict(const ProductionFeatures& features) const;
};
// 击杀返冰、削血、突破、存活投资、生产收入、支出、爆区损失、推进；只作训练起点。
inline constexpr Weights InitialWeights{3, 1, 120, 0.4f, 0.25f, -1, -2, 0.5f};

/** 数值化的一次性付费爆发；零范围禁用，阶段不含具体僵尸类型或实体引用。 */
struct PaidBurst {
	enum class Stage { READY, WINDUP, ACTIVE, RECOVERY, SPENT };
	Stage stage = Stage::READY;
	float range = 0, cost = 0, stopHealth = 0;
	float windup = 0, duration = 0, recovery = 0, retry = 0;
	float remaining = 0, retryRemaining = 0;
	float moveMultiplier = 1, biteMultiplier = 1, recoveryMoveMultiplier = 1;
};
/** 可付费修复的一类防具；health 属于总生命，不能沿用二类盾的绕盾/穿透语义。 */
struct ArmorRepair {
	float health = 0, maximum = 0, totalMaximum = 0, stopBodyHealth = 0;
	float remaining = 0, interval = 0, amount = 0, cost = 0;
};
/** 鼓手动作与目标的已提交层分别计时，来源死亡不撤销目标效果。 */
struct Drum {
	bool enabled = false, winding = false;
	float remaining = 0, stopHealth = 0;
};
/** 同行落种反应：装填与前摇独立计时，出膛后的脉冲不再依赖来源存活。 */
struct DeploymentSniper {
	bool enabled = false, aiming = false;
	float remaining = 0, stopHealth = 0;
	int targetID = 0;
	float targetX = 0, damage = 0;
	float muzzleOffset = 0; // 碰撞参考 X 到实际视觉枪口的偏移，像素；Board 转换后提供
};
/** 仪器破坏取消未提交仪式并切换过载；已提交的裂隙由独立队列持有。 */
struct Ritual {
	bool present = false, enabled = false, winding = false;
	float armor = 0, stopHealth = 0, remaining = 0;
	int releases = 0;
};
/** 活车拥有独立铺路来源及无伤计时；来源死亡后只保留 Snapshot 中的非叠加冰道。 */
struct GoldenDrive {
	bool enabled = false;
	float undamaged = 0, trailLeft = (std::numeric_limits<float>::max)(), frontOffset = 0;
};
struct GoldenTrail { float left = 0, remaining = 0; };
/** 钟匠本地准备/前摇/冷却；提交后时间锚独立持有，来源死亡不撤销。 */
struct Clock {
	bool present = false, enabled = false, winding = false;
	float remaining = 0, stopBodyHealth = 0;
};
/** 投篮车停步投射的数值副本；存活回溯不恢复弹药，重新创建的复活单位用出生库存。 */
struct CatapultAttack {
	enum class Phase { WALKING, SHOOTING, RELOADING, PUNCTURED };
	bool present=false, launched=false;
	Phase phase=Phase::WALKING;
	int ammunition=0, targetColumn=-1;
	float release=0, duration=0, releaseRemaining=0, remaining=0, damage=0;
	float animationBase=1; // 品种/词条固有动画倍率；天气、减速、黄金叠层在推演中独立派生
};
struct Unit {
	ColdStorageStrategy::SplashUnit body;
	float minimumMoveSpeed = 0, maximumMoveSpeed = 0; // 未出生的品种移速范围，px/游戏秒；已有实体为零，沿用实测速度
	float lowerForecastMoveSpeed=0, upperForecastMoveSpeed=0; // 偏慢/偏快的出生分布分位数，不是数学极值
	bool birthMovementKnown = false; // 零移速也可能是合法出生阶段，不能把静止品种当成普通行走
	ZombieMovementRules::PositionCurve movementCurve;
	float movementCurveBase=0, movementCurveReference=0; // 世界基准与采样位置；保持已采样速度倍率，按推进位置更新车速
	bool engineer = false, canisterFull = true, reloadPaid = false;
	float engineerStopHealth = DisasterEngineerRules::Health/3; // 工程师掉头后的停用阈值，实例/新购按缩放后本体最大生命采样
	float reloadRemaining = 0, thunderResistance = 0, paralysisRemaining = 0;
	PaidBurst burst;
	ArmorRepair repair;
	Drum drum;
	DeploymentSniper sniper;
    bool pressure=false;
    bool floodMortar=false;
    float floodReload=FloodMortarRules::FirstReload;
    float floodStopHealth=FloodMortarRules::Health/3;
    int pressureShot=0; // 下一发在四连发中的序号
    float pressureRemaining=PressureShooterRules::Reload+10/PressureShooterRules::FramesPerSecond;
    float pressureStopHealth=PressureShooterRules::Health/3, pressureMuzzle=-20; // 掉头阈值和碰撞参考点至枪口偏移

	Ritual ritual;
	GoldenDrive goldenDrive;
	Clock clock;
	DancerRules::Forecast dance;
	float helmHealth = 0, temporalStopHealth = 0; // 一类防具和可锚定的本体掉头阈值；已有实体由 Board 精确采样
	bool temporalEligible = true, temporalIrreversible = false; // 复合编队/首领及清洁车等不可逆清除不参与回溯
	float blastCredit = 0; // 本单位已计入爆区损失的冰价，回溯恢复生命时撤回对应部分
	std::array<float,9> goldenMoveRatios{1,1,1,1,1,1,1,1,1}; // 各层相对采样速度的常驻能力/天气倍率，鼓舞和减速独立推进
	int goldenStacks = 0; // 每步由活车与残留冰道重算，不永久保留采样覆盖
	float adaptiveHelmet = 0;
	PlantDamageOrigin adaptedOrigin;
	std::vector<std::pair<int,float>> inspiration; // 来源身份与剩余游戏秒，同源刷新
	bool instantVehicleCrush=false; // 冰车压扁普通植物；投篮车射击/装填停步独立预测
	bool vehicleCrush = false; // 碰到抗碾压植物时使用承伤/推退契约，其他车战斗仍沿用原近似
	float productionRemaining = IceProduction::Interval, nextYield = IceProduction::InitialYield, biteDps = 50;
	float mistFuelReward = 0; // 活体已分配雾火；未来随机掉落不冒充确定收入
	float playerRefund = 0; // 只有正式付费单位死亡才返给植物方，免费召唤不计
	float shieldHealth = 0; // body.health 中的二类防具份额；本体/头盔归零时，剩余护盾不能维持存活
	float shieldedHitCap = 0, shieldedAshCap = 0; // 持盾时的单次伤害上限；零表示不封顶
	float fumeMultiplier = 1; // 目标自有的大喷家族倍率，破盾后仍适用
	bool blocksShieldBypass = false, blocksFumePiercing = false; // 只在护盾尚存时生效
	int id = 0; // 已有实体稳定 ID；新增候选以出生序列打破威胁并列
	float productionStopHealth = IceProduction::WorkerHealth / 3; // 对齐 Zombie::TakeBodyDamage 掉头阈值；掉头后不再生产
	float throwHealth = 0, throwAnchorX = 0, throwWindup = 1; // 尚持小鬼的投手：触发生命、半场锚点与预计前摇；零生命阈值禁用
	bool mowerImmune = false, consumesOtherMowers = false; // 单位自身的清洁车交互能力，不从购买价格猜测
	bool hijackerBoosted=false;
	float chargeControlImmunity=0, sampledOverload=1; // 引雷免控截止秒、快照已烘焙过载倍率
	bool hijacker=false, grounding=false, insulator=false, groundHazard=true, paralysisAllowed=true;
	bool jammer=false; float jammerRemaining=0, overloadRemaining=0;
	float rawRainMultiplier=1; // 快照中烘焙的雨势，环境推演逐步替换
	CatapultAttack catapult;
	JackBoxRules::Forecast jack;
	HealerRules::Forecast healer;
	BalloonRules::Forecast balloon;
	DiggerRules::Forecast digger; // 地下、出土与折返独立于通用移动；速度由主线程去掉临时叠层后提供
	LadderRules::Builder ladder; // 携梯/放置/卸梯独立状态；活体余帧只在主线程读取
	LadderRules::Climb ladderClimb; // 共享攀梯的资格、阶段和已使用列，不复制建造者收益
	float maximumBody=0, maximumHelm=0, maximumShield=0; // 正式层生命上限；装备被永久移除后其层不再治疗

	float magneticBacklash=0; // 目标卸甲反噬磁力菇本体，生命点
	int magneticLayer=0; // 可吸取的当前层：0无、1头甲、2门盾、3工具；小丑失盒另推进
	float boundsY=0, boundsHeight=100; // 碰撞框相对本行中心的垂直偏移与高度，像素


};
struct Plant {
	float y=0, boundsX=-40, boundsY=-50, boundsWidth=80, boundsHeight=100; // 实体逻辑位置与实际爆区判定矩形相对量
    float targetValue=0; // 精英盒贪心选点的阳光及产能价值，不计入僵尸资源账本
    bool pumpkin=false, targetsAir=false;
    float magneticPulseRadius=0, magneticPulseParalysis=0; // 金磁消耗装备后的独立脉冲范围及麻痹秒数
    float magnetRemaining=0, magnetRecharge=0, magnetRadius=0, magnetEatingRadius=0, magnetRowPenalty=0;
    int magnetRows=0; // 零半径禁用；充能与搜索均采样正式能力参数
    bool catapultTargetable=true; // 选靶跳过地刺，格内仍按正式 overlay/宿主/南瓜/承载层顺序
	bool catapultCrushable=true; // 投篮车自有类型/睡眠资格，不把冰车的目标名单套到投篮车
	int airborneDefenseRadius=-1; // 非负时按自有逻辑格半径拦截篮球；生命/格位由本候选维护
	bool cotton = false;
    bool rainBamboo=false;
    float bambooCharge=0,bambooRate=1,floodSlowUntil=0;
    float cottonRemaining=MendingCottonRules::Interval; // 活体采样当前治疗冷却，未来株从完整周期开始
	bool thunder = false;
	ThunderFlowerRules::AttackForecast thunderAttack; // 射击冷却、索敌等待与已起播头部吐弹分开推进
	float shutdownUntil=0;
	bool grounding=false, lightningPot=false, support=false, plantern=false;
	int executionGroup=-1; bool countsExecution=false, diesExecution=false;
	std::array<float,54> illumination{}; // 当前挡位逐格照明；死亡或燃料耗尽即失效
	PlanternGear lightGear=PlanternGear::OFF;
	float lightFuel=0, lightIntakeLimit=PlanternRules::FuelCapacity;
	std::vector<std::pair<float,float>> lightDeliveries; // 在途雾火的到账秒与预留量，不提前兑现

	bool eliteQuota = false; // 本体和补种画像共享精英同时在场计数
	PlantDamageOrigin damageOrigin;
	float maximumHealth = 0; // 裂隙按最高层原上限选择落点，不随当前残血重排
	int boundaryShards = 0;
	int hostileMirrors = 0; // 冰镜草已成型镜面，逐发拦截敌方直射弹
	float boundaryRecharge = 0, boundaryCharge = 0, boundaryBlockedUntil = 0;
	AttackGrowth growth; // perShot > 0 时按实际射击成长；DPS 不再冻结在采样时刻
	float growthSpeed = 1; // 不含菠萝领域的基础行动倍率，领域在时间线中独立推进
	int row = 0, column = 0, layer = 1;
	float x = 0, health = 0, dps = 0, reward = 0;
	float slowRate = 0, slowDuration = 0, stopDuty = 0;
	float sunPerSecond = 0;
	int rowRadius = 0;
	float range = 10000;
	bool vehicleCrushable=true; // 活体与未来株均由冰车自身的目标资格采样
	bool multiTarget = false, around = false;
	bool echo = false; // 非矿场声波只攻击棋盘内、发射格前方的对象逻辑位置；伤害仍用连续 DPS 近似
	bool ladderTarget=false; // 植物自有SupportsLadderPlacement；放梯完成按当前同格层重取目标
	bool melon = false, edible = true;
	bool deploymentInterceptionOnly = false; // 灰烬充能无敌只放行命中原触发实体的狙击脉冲，不放行普通误伤
	float counterBlastAt = (std::numeric_limits<float>::max)(); // 一次性来源的引爆时刻，游戏秒；晚到弹体不能撤销已发生爆炸
	float hitDamage = 20; // 等效单发伤害，用于将每击上限换算为 DPS；常规小弹丸默认 20
	bool bypassShield = false, fume = false; // 抛物绕盾/大喷穿盾由 Board 解析；西瓜使用 melon 的双层受伤语义
	int id = 0; // 主动能力的来源，推演中被消灭后不能继续释放
	float initialHealth = 0, productionAt = 0; // 削血分母与新生产株首次产出的时刻，游戏秒
	float repairMaximum = 0, repairAmount = 0, repairCost = 0, repairRecharge = 0, repairRemaining = 0;
	float repairBlockedUntil = 0, damageCredit = 0; // 暂停新修复的期限，以及可被回血撤回的削血得分
	bool repairAutomatic = false;
	float crushDamage = 0, immuneRemaining = 0, vehicleRetreat = 0;
	bool hasBurstProtection = false;
	IceStorageNutRules::Protection burstProtection; // 自有护体快照；无敌结束才启动冷却，就绪才统计伤害
	float assetValue = 0; // 当前生命对应的卡价折冰估值；终点再按后续剩余生命折价，不直接返给僵尸
};
/** 一次释放同时打击各行最高威胁目标；各行共用来源的一个充能周期。 */
struct RowStrike {
	int plantID = 0;
	float ready = 0, recharge = 20, damage = 0, splashDamage = 0, radius = 0;
};
/** 当前卡槽的合法建设落点；同 source 共用冷却，不能把每个格位当成独立牌。 */
struct Construction {
	Plant plant;
	RowStrike strike;
	int source = 0, sunCost = 0, iceCost = 0;
	float ready = 0, recharge = 1, firstSunDelay = 0;
	int remainingUses = -1; // 同 source 共用累计剩余次数，-1 表示不限；死亡不返还
	int simultaneousLimit = -1; // 非负时仅在存活精英数低于上限时允许补种
	int quotaGroup = -1; // 非负时跨卡槽共享名额，如本卡与模仿者；缺省按 source
};
/** 一张可循环铲种的返阳光卡；候选格共享卡槽冷却，收益来自真实负阳光价格。 */
struct SunExchange {
	int sunGain = 0, iceCost = 0;
	float ready = 0, recharge = 1;
	std::vector<std::array<int,2>> cells;
};
/** 已在场的临时攻击领域；只有攻击/控制频率受益，生产、技能冷却不加速。 */
struct AttackAura {
	int plantID = 0, row = 0, column = 0;
	float active = 0, cooldown = 0, duration = 0, recharge = 0, bonus = 0;
	int iceCost = 0;
	float blockedUntil = 0; // 当前停机结束前不能新激活，既有领域仍计时
	bool automatic = false;
};
struct ShopOrder { int sunCost = 0, iceGain = 0; float delivery = 0; };
struct ConstructionStats {
	float productionAfterWindow=0; // 60秒校准窗口之后、同一战斗时域内真实兑现的预测产冰，不预支采购

	int catapultShots=0, catapultHits=0, catapultBlocks=0;
	std::vector<WorkerForecastTrace> workerTrace;
	std::vector<CounterForecastTrace> counterTrace;
	float breachSeconds = -1; // 首次有效进屋的预测游戏秒；-1 表示尚未突破
	int cratersCreated = 0; // 实际完成爆炸后生成的预测弹坑，不含被提前消灭的灰烬
	int planternResponseGear = -1; // -1沿用当前挡位；0..3固定应对；4随燃料切II/III挡，无雾关灯
	bool counterSpaceReserved = false; // 对手保留空位/资金优先反制，暂不追加建设的独立推演
	int counterShovels = 0; // 为灰烬腾位而主动牺牲的普通层植物数，不计僵尸击杀奖励
	float counterShovelAssets = 0; // 主动腾位损失的植物资产，冰价；用于诊断回本与反制代价
	int paidCounterCasts = 0; // 玩家尚未提交的付费灰烬实际使用次数，已提交爆炸不重复计费
	bool paidDefensesRetained = false; // 本完整玩家应对保留付费手动反制；既有事务和自动能力仍生效
	int stationDischarges=0, stationJams=0, stationCounters=0, stationFogCounters=0;
	int movementBoundsApplied = 0; // 为经济生存推演采用出生移速边界的单位数，不额外增加候选或推演次数
	int goldenAccelerationSteps = 0, goldenDrumSteps = 0, goldenResidualSteps = 0, goldenMaxStacks = 0; // 实际生效的无伤/鼓舞/残留冰道预测步数及最大来源层数
	int planted = 0, exchanges = 0, orders = 0;
	float abilityIceSpent = 0; // 僵尸未来实际可付的技能费，计入支出，不提高成交返冰价
	int burstActivations = 0, auraActivations = 0, armorRepairs = 0, plantRepairs = 0;
	float armorRepairIce = 0, plantRepairIce = 0;
	int drumBeats = 0, drumRecipients = 0, precisionHits = 0;
	int engineerBlocks = 0, thunderStuns = 0;
	int ladderPlaced=0,ladderClimbs=0,ladderRemoved=0; // 已提交共享梯、真实启动攀爬和实际拆梯次数
	int siegeAccessProgress=0; // 终点仍存在的新共享通路数，仅深化未付款攻城案，不计资源或最终评分
	int healerCasts=0, healerRecipients=0; // 已兑现治疗及受益单位数
	float healerAmount=0, healerRecoveryCredit=0; // 真实恢复生命与撤回的爆区损失折冰
	int jackExplosions=0, jackThrows=0, jackBoxHits=0, magneticExtractions=0; // 已兑现小丑及磁吸事务次数
	int dancerSummons = 0; // 推演中实际提交的免费伴舞，不计采购资产和玩家死亡返冰
	float workerProtectionProgress = 0; // 实际挡灰/回溯/治疗恢复的工人生命折冰值，仅供探索中间态，不计收入或最终评分
	float engineerReloadIce = 0;
	int deploymentShots = 0, deploymentHits = 0;
	int ritualReleases = 0, riftSummons = 0, riftRedirects = 0;
	int interferences = 0; // 玩家实际可支付的时间干扰次数
	int clockAnchors = 0, clockTargets = 0, clockRewinds = 0, clockRevivals = 0, clockRedirects = 0;
	float sunSpent = 0, iceSpent = 0, opponentAssets = 0;
	float exchangeSun = 0, exchangeIce = 0, orderSun = 0, orderIce = 0, pendingIce = 0;
};
struct Mower { int row = 0; float x = 0, width = 60, speed = 230; bool moving = false, active = true; float y=0,height=0; };
struct Option { int type = 0, row = 0, cost = 0; Unit unit; ContextWeights preference{}; float firePreferenceScale = 1; int device=-1, setting=0; };
struct Action { int option = 0; float delay = 0; };
/** 未付款的跨轮探索提案；仅保存兵种、路线、时序，不携带旧实体、评分、价格或收益。 */
struct ProposalMember { int type=0, row=0, device=-1, setting=0; float delay=0; };
using Proposal = std::vector<ProposalMember>;
/** 已付款但未出生的 current 下标与合法行；只允许改路或提前，不换兵、不退冰。 */
struct CommittedUnit { int unit = 0; std::array<bool, 6> legalRows{}; };
struct QueueRevision {
	int evaluated = 0, changed = 0; float beforeScore = 0, afterScore = 0;
	bool beforeBreach = false, afterBreach = false; // 已购队列也采用突破优先，评分仅用于相同结果
};
/** 同一 source 的格位是同一次反制的备选落点，共用冷却和资源；已提交动作不可改点。 */
struct Counter {
	ColdStorageStrategy::BlastThreat blast;
	int source = 0, sunCost = 0, iceCost = 0;
	float recharge = 10000, windup = 1;
	bool targeted = false; // 倭瓜先在种植格附近索敌，再在目标附近结算窄范围伤害
	int cellRow = -1, cellColumn = -1; // 新种灰烬的原落点；-1 表示无需空格的已有能力
	bool shovelAllowed = false; // Board 已验证可腾出普通层；推演仍受当前占位、资源与反制择时约束
	int plantID = 0; // 已有反制能力的稳定来源；死亡停止后续释放，不按格位替换目标
	bool consumesPlant = true; // 一次性灰烬爆炸后消耗宿主；玉米炮保持可受击并继续装填
	float flightSeconds = 0; // 爆炸前独立飞行时间，游戏秒；离膛后不再依赖来源，零保持宿主绑定
	int sharedSource = -1; // 额外共享的触发卡冷却，如多株预存毁灭共用咖啡；-1 表示无需第二张牌
	float sharedReady = 0, sharedRecharge = 0;
	float vulnerableSeconds = 0; // 从提交到清醒无敌的等待，游戏秒；已经清醒时为零
	bool stored = false; // 预存反制额外比较长期蓄爆，不假设小股诱饵一定能骗掉它
	float deploymentHealth = 0, deploymentReward = 0, deploymentAssetValue = 0; // 新种灰烬的实体画像；零生命保持无落种事件的能力
	bool clearsCell = false; // 毁灭引爆会清除同格各层；樱桃/辣椒只消耗自身
	int ladderClearRow=-1,ladderClearRadius=-1; bool clearsLadderRow=false; // 正式灰烬额外拆梯形状；不依赖命中僵尸
	float craterSeconds = 0; // 爆炸后禁止该格新种植的游戏秒数，由正式弹坑寿命提供
};
/** 已提交的裂隙，即使来源死亡也必须进入预测。 */
struct Rift { Unit unit; int column = 0; float remaining = 0; };
/** 只拥有数值副本与稳定 current 下标；资源、返款资格和不可逆提交不随核心状态倒放。 */
struct TemporalTarget {
	int unit = -1;
	Unit saved;
	bool restoreHelm = true, restoreShield = true, restoreAbility = true;
};
struct TemporalAnchor {
	int ownerID = 0;
	float at = 0;
	std::vector<TemporalTarget> targets;
};
/** 已发射雷种只有数值位置与来源；不借用弹丸或植物对象。 */
struct ThunderRay { float x = 0; int row = 0; PlantDamageOrigin origin; };
/** 已离膛篮球只保存落格与到达秒，来源死亡或下一轮重排不取消。 */
struct BasketballFlight { int row=0, column=-1; float at=0, damage=0; };
/** 已离手精英盒冻结落点和阵营；死亡动画仍飞行，但载体被灰烬直接移除会取消，回溯不复制。 */
struct JackBoxFlight { float x=0, y=0, at=0; bool charmed=false; int ownerID=0; };
/** 三叶草卡共享一份真实冷却，已有演出锁定来源；吹飞不属于灰烬。 */
struct WindCounter { Plant deployment; std::vector<std::array<int,2>> cells; bool committed=false; int source=0, plantID=0, row=-1, column=-1, sunCost=0, iceCost=0; float ready=0,recharge=0,windup=0,nextReady=0; bool house=false; };
/** 已出膛气弹独立于来源存活；入场新兵和活体共用弹道。 */
struct BambooRay { float x=0;int row=0;PlantDamageOrigin origin;std::vector<int> hitIDs; };
struct FloodShot { int row=0,column=0;float at=0,damage=0; };
struct PressureRay { int row=0; float x=0, launchedAt=0, damage=PressureShooterRules::Damage; };
struct Snapshot {
    std::vector<PressureRay> pressureRays;

	std::vector<LadderRules::Cell> ladders; // Board已存在的共享梯，与来源死亡及未付款方案无关
	std::vector<Proposal> proposals; // 同一Planner的未完成探索，必须按当前资格/钱包重新映射并完整评价

    std::vector<WindCounter> windCounters;
	std::vector<JackBoxFlight> jackBoxes;
	std::array<float,6> rowY{}; // Board采样的各出生行Y；纯数值夹具可用行高兜底
	std::array<float,54> cellY{}; // 当前网格中心Y，含屋顶坡度

	Unit dancerBackup; // 已采样的普通伴舞出生画像；无实体或资源引用
	float danceBeatSeconds = 0; // Board 当前全局舞拍在一圈内的位置，游戏秒
	std::vector<BasketballFlight> basketballs;
	std::vector<ThunderRay> thunderRays;
    std::vector<BambooRay> bambooRays;
    std::vector<FloodShot> floodShots;
	bool traceEconomy = false; // 仅显式诊断采集预测轨迹；正式对局与批量训练默认不分配轨迹
	bool fuelAwarePlantern = true; // 仅诊断消融可关闭动态挡位应对，正式搜索始终启用
    bool timeLimitedSearch=false; // 仅实时后台按墙钟截止；同步训练维持完整、可重复的搜索次数
    std::chrono::steady_clock::time_point searchDeadline{};
	bool weatherStation=false;
	WeatherStationRules::State station;
	std::array<float,54> stationFogAlpha{};
	std::array<float,4> rainZombie{1,1,1,1}, rainPlant{1,1,1,1};
	float sampledRainPlant=1, sampledRainZombie=1, stationCharge=0, stationOvercharge=0, stationWarning=0, stationJammed=0;
	int stationChargePhase=0, stationRow=-1, stationHijackerID=-1, stationGuideID=-1;
	bool stationSelectionAttempted=false, stationGuided=false;

	const std::atomic<bool>* cancellation = nullptr; // 仅后台任务自有的取消令牌；同步训练缺省为空，不改变评估结果
	int searchVersion = 1; // 1 小队无预测增量收益时升级到 2；2 直接使用整队搜索与长时域预测
	bool netEconomy = false; // 新策略按统一冰价评价收入、残存投资和支出；旧配置保持原评分
	bool experiencedFormations = true; // 混合候选默认开启；纯数值/只读诊断可消融，不改变正式卡池或付款规则
	bool anticipateEconomy = false; // 显式考虑玩家循环经济卡与后续付费订冰，旧策略缺省关闭
	float opponentWeight = 0, sunIceValue = 0; // 对方终点资产差的可训练价值、商店阳光折冰率；权重零保持旧评分
	const ProductionCalibration* productionCalibration = nullptr;
	const StateModel* stateModel = nullptr;
	float noProgressSeconds = 0; // Board 已记录的连续无植物击杀时间，仅作模型输入
	int budget = 0, capacity = 0;
	std::int64_t deploymentCapital = -1; // 实际库存+活体/在途原成交价；-1表示纯夹具使用固定capacity
	int deploymentOccupied = 0; // 实际敌对活体和已付款队列数；技能/设备扣资本后重新核对剩余名额
	int recoveryReserve = 0; // 正式 Board 指定的低库存重组门槛，零表示不启用
	int fundableUnlockTroopCost = 0; // 当前钱包可支付解锁路径的后续兵种最高单价，只参与现金估值，不开放采购
	float capitalRiskAllowance = (std::numeric_limits<float>::max)(); // 累计净亏损后的剩余风险额度，冰；未提供实际账本的夹具不启用
	bool allowWait = true; // 默认允许等待；Board 仅为可支付的后续兵种解锁路径请求出兵
	int fallbackProbeBudget=0; // 长期空场的独立有限实战试攻额度，冰；零保持纯评分等待
	bool fallbackAllIn=false; // 有限试攻无实际进展后，允许从真实评估的完整进攻案孤注一掷
	int playerSun = 0, playerIce = 0, incomingIce = 0;
	int playerSunLimit = (std::numeric_limits<int>::max)(), playerIceLimit = (std::numeric_limits<int>::max)(); // Board 提供正式容量；纯数值夹具可不设上限
	float incomingIceAt = 0;
	float supplyRemaining = 0, supplyInterval = 0, supplyIce = 0; // 技能钱包的真实补给时序；不作为经营得分
	int pumpkinProtectionCells=1; // 正式范围爆炸的南瓜保护格半径
	float pumpkinDamageMultiplier=5; // 南瓜拦截范围伤害的基础伤害倍率
	float houseX = 160, rightEdge = 1100;
	float goldenRightX = 1100, goldenLeftLimit = GoldenIceRules::LeftLimit;
	std::array<bool,6> goldenAllowedRows{true,true,true,true,true,true}; // Board 排除水路，屋顶左缘由当前几何采样
	std::array<GoldenTrail,6> goldenTrails{};
	float gridLeft = 160, cellWidth = 80, cellHeight = 100;
	int rows = 5, columns = 9;
	bool resumePortfolio = false; // 上次已扩展到完整编队且仍在等待，下一次直接继续同类搜索
	int stationWave = 0; // 第30波采购新兵将进入第31波，候选与等待采用各自真实耗油阶段
	float discountRemaining = 0; // 已激活优惠的真实余时，届满后恢复原价
	bool interferenceAvailable = false; // 商店资格，不假定玩家已按按钮
	float interferenceRemaining = 0, interferenceReady = 0; // 已生效禁锚余时和独立冷却余时，秒
	bool precisionReady = false;
	bool precisionUnlockAfterPurchase = false; // Board确认距技能解锁一波；只预测付费出兵后下一决策的技能，不提前提交
	float precisionUnlockAimStartSeconds = 0; // 下一正式决策与后台计算等待后的预计瞄准开始时刻，游戏秒；不含 StrikeAimDuration 瞄准时长
	int precisionTargetID = 0; // 本候选立即购买的技能；零表示保留资金
	std::vector<int> precisionAdditionalTargetIDs; // 同次付费的其余独立目标，最多两株，与首目标共用瞄准/冷却
	int precisionTargetLimit = 1; // 调用方开放的目标数，1..3；旧数值夹具默认保持单株行为
	int pendingPrecisionID = 0; // 已支付技能只结算原目标，不再次收费
	std::vector<int> pendingPrecisionAdditionalIDs; // 已付款的其余目标，按原身份同时结算，不另找替补
	float pendingPrecisionRemaining = 0;
	float impWalkSpeed=20; // Board 从小鬼实际出生画像采样，纯数值夹具保留缺省值
	std::array<Unit,5> ritualSummons{};
	std::vector<Rift> rifts;
	std::vector<TemporalAnchor> temporalAnchors;
	std::vector<std::array<int,2>> riftCells; // 主线程给出的合法落点，不在后台查询 Board
	bool ritualWhiteout = false;
	std::vector<Unit> current;
	std::vector<CommittedUnit> committed;
	std::vector<Plant> plants;
	std::vector<Option> options;
	std::vector<Counter> counters;
	std::vector<RowStrike> rowStrikes;
	std::vector<AttackAura> attackAuras;
	std::vector<Construction> construction;
	std::vector<SunExchange> exchanges;
	std::vector<ShopOrder> shop;
	std::vector<Mower> mowers;
	std::array<ContextWeights, 6> context{};
};
struct Result {
	int fallbackMode=0; // 0正常评分、1有限试攻、2孤注一掷；保留原预测分数，不能伪称盈利
	int experiencedEvaluated=0; // 经验编队在统一预测中实际积分的数量，不含被去重或资金不足的配方
	bool experiencedSelected=false; // 最终直接选中经验原案；自由变异后的方案仍按自由搜索记录
	CapitalUtilityInputs capitalUtilityInputs; // 实际评分使用的现金快照输入，仅诊断，不保存或预支资金
	std::vector<Proposal> proposals; // 有界未付款中间态；可供下一轮探索，不属于可执行购物车
	int proposalEvaluated=0; // 跨轮提案在当前局面真正重新评估的数量

	std::vector<Action> investmentPruningActions; // 未购买的经营中间态，只供同预算删冗员对照，不提交或存档
    bool timeLimited=false; // 搜索停止继续扩展，已返回的候选仍经过完整时间线和反制对照
	std::vector<CandidateStats> candidates; // 最终编队搜索阶段的候选统计；不含精准清除探测
	int precisionTargetID = 0, precisionEvaluated = 0;
	std::vector<int> precisionAdditionalTargetIDs; // 必须与首目标、完整付款及同编队反事实一起提交
	float precisionGain = 0; // 相对保留技能资金的增量评分；突破优先时可为负，以 features[2] 判断胜利
	int forecastPrecisionTargetID = 0, forecastPrecisionIce = 0; // 未付款的解锁后预测；当下提交目标必须为零，费用只留在预测账本
	std::vector<int> forecastPrecisionAdditionalTargetIDs;
	float forecastPrecisionAimStartSeconds = 0; // 仅供预测诊断，下一轮仍重新选靶，不保存承诺
	bool expandedForecast = false; // 小队无增量收益后是否采用完整 v2 预测；避免混比两个时域的分数
	std::vector<Action> actions;
	Weights features{}, baselineFeatures{};
	float baselineBreachSeconds=-1; // 同一完整等待世界的首次进屋秒，仅供本次搜索复用，不跨快照或存档
	Weights effectiveWeights{};
	StateFeatures stateInputs{};
	float score = 0, blastLoss = 0, preferenceScore = 0;
	float rawPreferenceScore = 0; // 有界处理前的兵种先验，仅诊断
	float opponentAssets = 0, baselineOpponentAssets = 0, opponentScore = 0; // 与不增援基线比较，避免奖励本来就会发生的消耗
	int evaluated = 0;
	int capitalRejected = 0; // 资金充足时因大额亏损/清场风险被排除的候选数
	int routeEvaluated = 0, combinationEvaluated = 0; // 路线覆盖数含重复案复用；组合数仅计实际积分，升级时合计两阶段
	int cohortEvaluated = 0; // combinationEvaluated 中的通用成批规模对照数，不增加该阶段预算
	int unevenMixEvaluated = 0, duplicatesSkipped = 0; // 实际积分的非等量混编数、同次搜索复用的重复案数
	int reinforcementEvaluated = 0; // 组合比较中向已有候选编队加入任意类型的次数，不代表实际购买
	int refinementEvaluated = 0; // 围绕完整优案替换少量成员的实际比较数，不按能力限定兵种
	int incomeEvaluated = 0, pruningEvaluated = 0; // 经营分支和最终成员/等待删除对照数，仅诊断，不增加采购或总时间预算
	int assaultEvaluated = 0; // 未付款攻城中间态的实际深化数，与经济分支共享原预算，不授予购买偏好
	int spreadCohortEvaluated = 0; // 完整协作复制到多路后实际积分的次数，不包含被去重/预算拒绝的提案
	float combinationBaseScore = 0, combinationBestScore = 0; // 最终阶段的组合探索前后评分
	bool combinationBaseBreach = false, combinationBestBreach = false; // 突破优先，因此胜出案评分可能下降
	float combinationBaseBreachSeconds = -1, combinationBestBreachSeconds = -1; // 同为突破时先比较首次进屋游戏秒，-1 表示未突破
	int largestPlan = 0; // 实际评估过的最大付费编队，不是强制出兵数量
	int widestComposition=0; // 实际评估的新购案最大兵种数，不包含设备或既有部队
	bool regrouping = false; // 没有可接受的低库存增援；继续积累恢复资本
	ConstructionStats construction;
	float rawProduction = 0; // 前60秒未校准的产冰预期，供实际回报拟合；不含评分用的后续兑现
	float counterHoldSeconds = 0; // 本次保守评估采用的玩家清场/主动打击等待习惯，游戏秒；不是僵尸出兵间隔
	ProductionFeatures productionInputs{};
	float formationBaseScore = 0; // 逐行对比前的最优自由编队评分
	std::array<float, 6> formationScores{}; // 同兵种/费用/时序整体投向各行的评分，按 tested 位掩码读取
	int formationTested = 0, formationRejected = 0, formationChosenRow = -1; // 拒绝位表示低库存收益不足；-1 保留自由编队
};

/** 验证参数尺寸以外的数值域，非法参数必须回退旧 AI。 */
bool ValidWeights(const Weights& weights);
/** 提取只读局势；baseline 的生产仍使用统一的未来 60 秒窗口。 */
StateFeatures DescribeState(const Snapshot& state, const Weights& baseline);
/** 只读诊断的一次采样；仅在调用线程内使用，模型借用当前策略，不能跨重载保留。 */
struct Probe {
	Snapshot snapshot;
	Weights weights{};
	std::uint32_t seed = 0;
	bool captured = false;
};
/** 在固定数值域内计算当前局势的评分权重；无模型时原样返回基础权重。 */
Weights ConditionWeights(const Weights& base, const StateFeatures& inputs, const StateModel* model);
/** 将经济项换成同一冰价的净收益；utilityScale 同时缩放现金增减及残存投资的价值。
 * 保留击杀的战术成分；支出系数不可独立变异为奖励，残存投资至多按原价计。
 */
Weights AccountForIce(const Weights& conditioned,float utilityScale=1);
/** 投影当前合法兵价及已资金覆盖的解锁兵价；只读现金，不读取预计收入，不改变兵种资格。 */
CapitalUtilityInputs DescribeCapitalUtility(const Snapshot& state);
/** 钱包覆盖一次完整投入和恢复储备后降低现金边际评分；包括已资金覆盖的解锁兵价，不预支收入。 */
float CapitalUtilityScale(const Snapshot& state);
/** 护盾能减少的本体火力比例；Board 用它修正持盾单位的火力偏好上下文，保留无盾单位原语义。 */
float ShieldProtectionFraction(const Unit& unit, const Plant& plant);
/** 低于重组储备且兵力/技能投资没有足够增量收益时暂缓付款；已有部队收益不能为新支出背书。 */
bool ShouldRegroup(const Result& result, int budget, int reserve);
/** 用已注入资本及当前现金/付费兵力资产计算剩余试错额度，不把对方损失当作己方资本。 */
float RemainingCapitalRisk(float fundedCapital, float currentCapital);
/** 兵力与纯技能采购共用资本检查；对方相对等待的额外资产损失可抵扣本案风险，不能补钱包。 */
bool ShouldConserveCapital(const Result& result, int budget, int reserve,
	float riskAllowance = (std::numeric_limits<float>::max)());
/** 有限步位置推演；planternResponseGear=-1沿用当前挡位，0..3固定挡位，4随燃料切挡，无雾关灯。
 * clearFogWhenReady 在共享预算内按真实设备时序尝试关雾；reserveCounterSpace 暂缓未来补阵，
 * 但显式照明响应仍可合法补灯，不增加资源或清空现有植物。hold 只延迟未提交且非救险的反制，
 * storedHoldSeconds 仅适用于预存灰烬，rowStrikeHoldSeconds 适用于主动打击。
 * preserveManualAuras 保留尚未开启的手动攻击领域，不撤销已激活或自动释放。
 * shovelCounterSpace 允许付出单格普通层植物的资产与功能损失后腾位反制，不授予击杀奖励。
 * preservePaidDefenses 保留付费手动灰烬、领域、维修及商店反制，既有事务和自动能力仍生效。
 * interferenceBeforeAsh 在已选灰烬前取消受威胁的锚，仍共用钱包和时间干扰冷却。 */
Weights Evaluate(const Snapshot& state, const std::vector<Action>& plan, ConstructionStats* construction = nullptr,
	float counterHoldSeconds = 0, float storedHoldSeconds = 0, float rowStrikeHoldSeconds = 0, bool reserveCounterSpace = false, bool clearFogWhenReady = false, int planternResponseGear = -1,
	bool preserveManualAuras = false, bool shovelCounterSpace = false, bool preservePaidDefenses = false,
	bool interferenceBeforeAsh = false);
class PlanEvaluator;
/** 只读诊断购物车，复用条件权重、等待基线及玩家应对；动态资本快照按整案费用修案。
 * 解锁后技能仅导出forecast诊断，不生成当下可提交的打击；不消费真实钱包或修改快照。 */
Result EvaluateCandidate(const Snapshot& state, const Weights& weights, const std::vector<Action>& plan);
/** 按合法兵种自由变异、配对及扩展后逐行比较；突破优先，同结果比较净收益，不迁移已有实体。
 * 仅差一波解锁的技能可以比较付费出兵后的未来收益，返回当下购物车和未付款forecast，不提前施法。
 * evaluator 由后台任务独占，函数返回前领完其结果；缺省为空，离线搜索保持同步随机顺序。
 */
Result Search(const Snapshot& state, const Weights& weights, std::uint32_t seed, PlanEvaluator* evaluator = nullptr);
/** 以原队列为保底比较合法重排；仅修改标记的未来单位，出生时间不晚于传入期限。 */
QueueRevision ReplanCommitted(Snapshot& state, const Weights& weights, std::uint32_t seed);
}
