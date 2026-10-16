#include "ColdStorageFogFerryForecast.h"
#include "Game/Zombie/FogFerryRules.h"
#include "Game/Board/NightRoofChargeRules.h"
#include "ColdStorageSearch.h"
#include "ColdStoragePlanEvaluator.h"
#include "ColdStorageFormationSeeds.h"
#include "ColdStorageAssaultTiming.h"
#include "ColdStoragePhasedPlanning.h"
#include "ColdStorageObservedPlanning.h"
#include "ColdStorageObservedForecast.h"
#include "Game/Zombie/DisasterEngineerRules.h"
#include "Game/Plant/ThunderFlowerRules.h"
#include "Game/Plant/EchoWaveRules.h"
#include "Game/Board/ColdStorageDeploymentRules.h"
#include "Game/Board/IceProduction.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include "Game/Zombie/CrystalDrummerRules.h"
#include "Game/Zombie/ThermalSniperRules.h"
#include "Game/Zombie/PolarClockRules.h"
#include "Game/Zombie/CatapultRules.h"
#include "Game/Zombie/AuroraPriestRules.h"
#include "Game/Plant/BoundaryFlowerRules.h"
#include "Game/Plant/DawnLotusRules.h"
#include "Game/Zombie/ImpThrowRules.h"
#include <algorithm>
#include <bitset>
#include <bit>
#include <cmath>
#include <iterator>
#include <random>
#include <set>
#include <utility>
#include <tuple>
#include "ColdStorageHealerForecast.h"
#include "ColdStorageLadderForecast.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace ColdStorageSearch {
namespace {
struct SearchCancelled {}; // 取消只由拥有任务的 Planner 捕获，不作为可提交结果
void CheckForecastCancellation(const Snapshot& state) {
    if(state.cancellation && state.cancellation->load(std::memory_order_relaxed)) throw SearchCancelled{};
}
/** 铲除、升级或自行消耗不保留此前短暂削血的材料价值；已被攻击击杀的资产损失仍保留。 */
void ConsumeForecastPlant(Plant& plant) {
    if(plant.health>0) plant.attackAssetCredit=0;
    plant.health=0;
}
/** 资产分母使用本世界出生时的完整参考生命，不让缺失普通评分字段放大单次材料损伤。 */
float AttackAssetHealth(const Plant& plant) {
    return std::max({1.0f,plant.initialHealth,plant.attackAssetInitialHealth});
}
}

/** 缓存完整原始积分；评分和自适应响应选择仍由调用方逐次执行，不跨本次不可变快照。 */
class RawWorldCache {
public:
    explicit RawWorldCache(bool enabled):mEnabled(enabled) {}
    void BindRoot(Snapshot& state) {state.rawWorldBinding.mCache=this;}
    /** 只供本文件已审核的时域/技能意图/公开Profile变体使用；其他复制保持断开。 */
    static Snapshot CopyView(const Snapshot& source) {
        Snapshot result=source;
        result.rawWorldBinding.mCache=source.rawWorldBinding.mCache;
        result.rawWorldBinding.mFreshPaidAreaOmitted=source.rawWorldBinding.mFreshPaidAreaOmitted;
        return result;
    }
    /** 只删除指定的新付费面积反制；其余能力、实体和钱包完全保留，并分离缓存域。 */
    static Snapshot WithoutFreshPaidArea(const Snapshot& source) {
        auto result=CopyView(source);
        std::erase_if(result.counters,IsFreshKnownPaidAreaCounter);
        result.rawWorldBinding.mFreshPaidAreaOmitted=true;
        return result;
    }
    static RawWorldCache* For(const Snapshot& state) {return state.rawWorldBinding.mCache;}
    RawWorldCounts Counts() const {
        std::lock_guard lock(mMutex);return {mEvaluations,mHits,static_cast<int>(mEntries.size()),mBytes};
    }
    /** 命中也检查取消；只发布完整返回的不可变结果，锁外完成耗时积分。 */
    Weights Run(const Snapshot& state,const std::vector<Action>& actions,ConstructionStats* construction,
        const RawWorldRequest& response,const FollowupPolicy* followup) {
        CheckForecastCancellation(state);
        auto key=mEnabled ? Key(state,actions,response,followup) : std::vector<std::uint32_t>{};
        std::shared_ptr<const Entry> hit;
        if(mEnabled) {
            std::lock_guard lock(mMutex);
            const auto found=std::find_if(mEntries.rbegin(),mEntries.rend(),[&](const auto& entry) {return entry->key==key;});
            if(found!=mEntries.rend()) {hit=*found;++mHits;}
        }
        if(hit) {
            CheckForecastCancellation(state);
            if(construction) *construction=hit->construction;
            return hit->features;
        }
        auto entry=std::make_shared<Entry>();
        entry->features=Evaluate(state,actions,&entry->construction,response.counterHoldSeconds,
            response.storedHoldSeconds,response.rowStrikeHoldSeconds,response.reserveCounterSpace,response.clearFogWhenReady,
            response.planternResponseGear,response.preserveManualAuras,response.shovelCounterSpace,
            response.preservePaidDefenses,response.interferenceBeforeAsh,followup);
        CheckForecastCancellation(state);
        {
            std::lock_guard lock(mMutex);++mEvaluations;
            if(mEnabled) {
                entry->key=std::move(key);
                // 两个线程偶然同时缺失同键可各自完整积分；只保留一份，不等待半成品或延长截止。
                const auto bytes=PayloadBytes(*entry);
                if(mEntries.size()<RawWorldCacheEntryLimit && bytes<=RawWorldCacheByteLimit-mBytes
                    && std::none_of(mEntries.begin(),mEntries.end(),[&](const auto& old) {return old->key==entry->key;})) {
                    mEntries.push_back(entry);
                    mBytes+=bytes;
                }
            }
        }
        if(construction) *construction=entry->construction;
        return entry->features;
    }
private:
    struct Entry {std::vector<std::uint32_t> key;Weights features{};ConstructionStats construction;};
    /** 轨迹目标也计入载荷；超过上限仍返回刚算完的世界，但不长期保存它。 */
    static size_t PayloadBytes(const Entry& entry) {
        const auto& stats=entry.construction;
        size_t bytes=sizeof(Entry)+entry.key.capacity()*sizeof(std::uint32_t)
            +stats.followup.realizedActions.capacity()*sizeof(Action)
            +stats.workerTrace.capacity()*sizeof(WorkerForecastTrace)+stats.counterTrace.capacity()*sizeof(CounterForecastTrace)
            +stats.combatTrace.capacity()*sizeof(CombatForecastTrace);
        for(const auto& event:stats.combatTrace) bytes+=event.targets.capacity()*sizeof(CombatForecastHit);
        return bytes;
    }
    /** 同源基底中的钱包/实体/几何/年龄不变；显式允许改变的量和全部积分参数逐项入键。 */
    static std::vector<std::uint32_t> Key(const Snapshot& state,const std::vector<Action>& actions,
        const RawWorldRequest& response,const FollowupPolicy* followup) {
        std::vector<std::uint32_t> key;
        key.reserve(32+actions.size()*2+(followup ? followup->actions.size()*2 : 0));
        const auto integer=[&](auto value) {key.push_back(static_cast<std::uint32_t>(value));};
        const auto real=[&](float value) {key.push_back(std::bit_cast<std::uint32_t>(value));};
        const auto list=[&](const std::vector<Action>& plan) {
            integer(plan.size());for(const auto& action:plan) {integer(action.option);real(action.delay);}
        };
        list(actions);integer(followup!=nullptr);
        if(followup) {
            list(followup->actions);real(followup->earliestSeconds);real(followup->expirySeconds);
            integer(followup->requireObservedCobLaunch);integer(followup->requireShotsClear);integer(followup->maxReadyCobs);
        }
        integer(state.searchVersion);integer(state.rawWorldBinding.mFreshPaidAreaOmitted);integer(state.precisionTargetID);
        integer(state.precisionAdditionalTargetIDs.size());for(int id:state.precisionAdditionalTargetIDs) integer(id);
        integer(state.precisionUnlockAfterPurchase);integer(state.diagnosticVisibleCounterWitness);
        real(response.counterHoldSeconds);real(response.storedHoldSeconds);real(response.rowStrikeHoldSeconds);
        integer(response.reserveCounterSpace);integer(response.clearFogWhenReady);integer(response.planternResponseGear);
        integer(response.preserveManualAuras);integer(response.shovelCounterSpace);integer(response.preservePaidDefenses);
        integer(response.interferenceBeforeAsh);integer(state.observedProfile!=nullptr);
        if(state.observedProfile) {
            const auto& profile=*state.observedProfile;const auto& context=profile.context;
            const auto mask=[&](std::uint64_t value) {integer(value);integer(value>>32);};
            mask(context.readyPaidAreaMask);integer(context.exposedCapitalBand);integer(context.nearestThreatBand);
            integer(context.readyFreeAreaCount);integer(context.economicPresent);integer(context.complexPresent);
            for(int value:context.coveredCapitalBands) integer(value);
            mask(context.learnablePaidAreaMask);integer(context.uncoveredCapitalBand);
            for(int value:context.uncoveredCoveredCapitalBands) integer(value);
            integer(context.committedCoveragePresent);real(profile.windowSeconds);real(profile.weight);
            integer(profile.encounters);integer(profile.nonResponses);integer(profile.allowWeakTransfer);
        }
        return key;
    }
    bool mEnabled;
    mutable std::mutex mMutex;
    std::vector<std::shared_ptr<const Entry>> mEntries;
    int mEvaluations=0,mHits=0;
    size_t mBytes=0;
};

namespace {
/** 不接触评分的唯一缓存入口；无明确同源绑定时继续执行原始完整积分。 */
Weights EvaluateRawWorld(const Snapshot& state,const std::vector<Action>& actions,ConstructionStats* construction,
    float counterHold=0,float storedHold=0,float strikeHold=0,bool reserve=false,bool clearFog=false,
    int lamp=-1,bool manual=false,bool shovel=false,bool paid=false,bool interference=false,const FollowupPolicy* followup=nullptr) {
    auto* cache=RawWorldCache::For(state);
    if(!cache) return Evaluate(state,actions,construction,counterHold,storedHold,strikeHold,reserve,clearFog,lamp,
        manual,shovel,paid,interference,followup);
    RawWorldRequest response;
    response.counterHoldSeconds=counterHold;response.storedHoldSeconds=storedHold;response.rowStrikeHoldSeconds=strikeHold;
    response.reserveCounterSpace=reserve;response.clearFogWhenReady=clearFog;response.planternResponseGear=lamp;
    response.preserveManualAuras=manual;response.shovelCounterSpace=shovel;response.preservePaidDefenses=paid;
    response.interferenceBeforeAsh=interference;
    return cache->Run(state,actions,construction,response,followup);
}
/** 只在完整候选之间检查截止，不截断积分或拼接不同时域的局部结果。 */
bool SearchTimeExpired(const Snapshot& s) {
    return s.timeLimitedSearch && std::chrono::steady_clock::now()>=s.searchDeadline;
}
/** 搜索规模看现金与存活/在途付费兵力的原成交价；买兵是资产转移，不应立即缩回小队。 */
float PaidSearchCapital(const Snapshot& s) {
    float capital=static_cast<float>(s.budget);
    for (const auto& unit : s.current) if (unit.body.health>0)
        capital+=std::max(0.0f,unit.body.purchaseCost);
    return capital; // 免费召唤的 purchaseCost 为零；这里只选择搜索范围，不能增加可付款预算。
}
constexpr int kPhasedBudgetPercent=20; // 两阶段完整对照占普通搜索原截止的有界份额，不延长总墙钟预算
constexpr float kHorizon = 60; // 推演覆盖的游戏秒，实际对局评测负责检验更长期收益；长编队评分另计同窗后续产冰
constexpr float kStep = 0.5f; // 仅候选预测的积分步长；真实比赛仍使用正式固定步
static_assert(kStep==HealerForecastStep,"healer and formation forecast must share a step");
constexpr float kResponseHighLightFuel = 20; // 一种玩家应对的III挡储油门槛，雾火；不改变真人操作和植物规则
constexpr int kTrials = 96; // 自由搜索的评估数，之后最多补六次同编队逐行比较
constexpr int kMaxActions = 8; // 小队搜索阶段的单位上限；升级后的完整编队搜索使用正式容量
constexpr float kMaxDelay = 12; // 新队员最迟出生时间，游戏秒
constexpr int kAdaptiveActions = 32; // 新模型可比较的最大编队，能力上限而非最低出兵数
constexpr float kAdaptiveDelay = 30; // 新模型可搜索的分批出生时域，游戏秒
constexpr float kAdaptiveHorizon = 90; // 新模型多观察一个后续交战窗口，游戏秒；产冰校准输入固定前60秒，评分计同窗总产冰
constexpr float kContact = 55; // 接触植物的预测距离，像素
constexpr int kInformationalTrialBudgetPercent=50; // 仅有新颖可付试探案时预留一半原预算，正常有益案始终优先
constexpr int kInformationalTrialCandidateLimit=192; // 只生成有限能力/空间/时序假设，完整积分每轮最多一个
/** 非矿场声波按正式对象位置和网格边界判定；碰撞锚点不能让场外工人提前承伤。 */
bool EchoContains(const Snapshot& s, const Plant& p, const Unit& unit) {
    const float x=unit.body.x+unit.body.blastAnchorOffset;
    const int column=static_cast<int>(std::floor((x-s.gridLeft)/s.cellWidth));
    return unit.body.row==p.row && column>=0 && column<s.columns && x>=p.x
        && x-p.x<=EchoWaveRules::RangeCells*s.cellWidth;
}
constexpr float kAreaCounterStake = 24; // 玩家倾向对至少此冰价的集中兵力支付大范围清场；免费可重复炮另比较小目标开火
constexpr float kTargetCounterStake = 8; // 窄范围反制允许用于较小目标，冰价
constexpr float kSquashTriggerRange = 125; // 倭瓜候选种植点可触发近邻目标的预测距离，像素
constexpr float kSquashImpactRange = 50; // 倭瓜落点的窄范围碰撞近似，像素
constexpr float kSquashFlightSeconds = 0.6f; // Squash 起跳后上升/下落时长的近似，游戏秒；此阶段不再追踪
constexpr float kSquashLeadSeconds = 0.3f; // 对齐 Squash::StartRising 起跳时的目标运动预判，游戏秒
constexpr float kRecoveryReturnFraction = 0.5f; // 低库存增援至少应换回半数冰价的预测收入或有效削血价值
constexpr float kConstructionInterval = 2.0f; // 玩家模型两次建设决策间隔，游戏秒
constexpr int kConstructionPlantLimit = 128; // 单次推演的植物容量，含已毁植物，约束新增对象开销
constexpr float kCobRestorationReadiness = .2f; // 恢复快照既有炮位的空场准备价值，与普通输出建设的空场比例一致
constexpr int kPortfolioActions = ColdStorageDeploymentRules::MaximumCapacity; // 完整编队覆盖正式可用容量，仍受当前预算和剩余名额限制
constexpr int kPortfolioCompositionLimit = 8; // 富余实时完整案的直接采样类型数上限；继续细化可接更多类型，不限制正式卡池
constexpr int kPortfolioTrials = 192; // 第二版固定评估预算，含不同规模、同类/混编和错峰方案
constexpr int kInitialSearchBudgetPercent = 45; // 实时编队阶段先给随机探索的时间比例，余量留给路线和增援
constexpr int kRouteSearchBudgetPercent = 65; // 独立路线比较累计用时占比，不能耗尽后续组合搜索
constexpr int kFormationSearchBudgetPercent = 65; // 技能可用时编队搜索占总剩余预算比例，余量比较清除与跟进
constexpr float kPortfolioDelay = 60; // 第二版允许跨过一轮反制冷却的出生时域，游戏秒
constexpr float kPortfolioHorizon = 120; // 最晚队员也有完整交战窗口，游戏秒；产冰校准输入仍固定前60秒，评分计同窗总产冰
constexpr int kForecastSummonLimit = 64; // 每类召唤的一次推演新增实体上限，不改变正式召唤上限
constexpr float kForecastImpLanding = .5f; // 落地动作阻止攻击/行走的近似时长，游戏秒
constexpr float kUnpricedCounterStake = 1; // 免费召唤的最低反制威胁，仅用于选灰烬落点，不计购买资产/返冰
constexpr float kEconomyClearSeconds = 2; // 返阳光卡铲除腾出周转格的保守预测耗时，游戏秒
constexpr float kCounterShovelSeconds = .5f; // 玩家决定腾位后再铲种的近似操作耗时，游戏秒；不改变真实铲子
constexpr int kRouteTrials = 256; // 单阶段兵种/合法路线覆盖上限，不区分攻击、经济或支援角色
constexpr int kCombinationTrials = 80; // 单阶段任意兵种配对和优案扩展预算，不预设技能搭配
constexpr int kRefinementTrials = 20; // 实时组合预算预留给完整优案的小幅换兵，不增加总候选上限
constexpr int kSpreadRefinementInterval = 3; // 尚未回本时每隔此数量的细化尝试比较整段多路协作，共用原名额
constexpr int kWinnerPruningTrials = 12; // 最终整案删除成员的比较上限，不扩大总墙钟预算
constexpr int kInvestmentDiverseBranches = 12; // 单次搜索同时保留的不同构成中间态上限，只作探索，不代表采购
constexpr int kProtectedCompositionDepths = 4; // 保护2/3/4/至少5类构成的四个深度桶，保留总槽位上限而不截断复杂合作
constexpr int kPrecisionProbeBudgetPercent = 65; // 技能阶段先用于选靶的时间比例，余量留给胜出清除目标的完整编队重搜
constexpr int kWinnerSearchBudgetPercent = 90; // 技能与编队探索只用九成总预算，余量核对胜出成员的边际作用
constexpr int kRefinementMaxReplacements = 4; // 一次优案细化最多替换的成员数，所有兵种统一使用
constexpr int kCombinationSearchBudgetPercent = 80; // 实时配对/成批探索累计用时上限，余量留给优案细化
constexpr int kRefinementSearchBudgetPercent = 95; // 实时优案细化累计用时上限，末段仍可对照合法路线
constexpr size_t kAssaultBranches = 24; // 未付款攻城中间态的有界容量，保留不同路线/构成而不增加评估预算
constexpr size_t kAssaultSmallPlan = 4; // 区分小批与大队探索槽位的成员数，不是采购数量限制
constexpr float kPreferenceIceFraction = .25f; // 净经济模式下单兵偏好最多相当于其冰价四分之一，不能压过明确回报
constexpr int kPrecisionFollowupTrials = 16; // 每个清除目标的通用跟进探测预算，之后少量目标共享余量深化
constexpr size_t kPrecisionFrontierLimit = 2; // 技能粗案尚未盈利也可保留的完整深化目标数，共享原墙钟预算
constexpr int kPrecisionTargets = 12; // 技能候选目标上限，每个比较原案、等待和通用跟进
constexpr int kPrecisionSetsPerSize = 12; // 每种目标基数保留的合法组合数，1/2/3交错且共用原截止
constexpr int kQueueTrials = 40; // 已付队列每次滚动重评的候选预算，不改变单位数或购买预算
constexpr float kPatientCounterSeconds = 8; // 对手等聚团再交灰烬的一种预测习惯，游戏秒；与即时反制共同取保守结果
constexpr float kLargeInvestmentFraction = .5f; // 一次投入超过现有库存一半时，必须证明增量回报能覆盖费用
constexpr float kCapitalUtilityWaves = 1; // 保留一次完整高价大队与恢复储备；富余现金的新增产冰价值递减
constexpr float kCapitalUtilityExponent = 3; // 储备充足后现金边际价值的衰减指数，抑制长期只扩厂不破阵
constexpr float kMinimumCashUtility = .01f; // 极充裕库存下仍保留的现金评分比例，避免把花费变为免费
constexpr float kCapitalLossFraction = .35f; // 本案及累计试错允许损失的本金比例；只计己方现金和付费存活资产
constexpr float kReinforcementDelay = 6; // 通用增援的错峰对照间隔，游戏秒；给先行部队拉开承伤距离
constexpr float kStoredCounterSeconds = 32; // 预存一次性清场可等后续部队聚集的保守对照，游戏秒；不延迟已提交爆炸
constexpr float kUrgentCounterDistance = 240; // 距房屋此像素范围内优先救险，灰烬和主动打击都不继续等聚团

/** 原目标身份去重并按开放名额限长；首目标为零时没有事务，不从附加列表凭空创建技能。 */
std::vector<int> PrecisionIDs(int first,const std::vector<int>& additional,int limit) {
	std::vector<int> ids;
	if(first<=0) return ids;
	ids.push_back(first);
	for(int id:additional) {
		if(ids.size()>=static_cast<size_t>(std::clamp(limit,1,ColdStorageSkillRules::StrikeTargetLimit))) break;
		if(id>0 && std::find(ids.begin(),ids.end(),id)==ids.end()) ids.push_back(id);
	}
	return ids;
}
/** 每株独立收费；已付款列表不属于新采购预算，不重复计费。 */
int PrecisionCost(const Snapshot& s) {
	return static_cast<int>(PrecisionIDs(s.precisionTargetID,s.precisionAdditionalTargetIDs,s.precisionTargetLimit).size())
		*ColdStorageSkillRules::StrikeIceCost;
}
/** 付费波次只能由真实士兵推进；纯设备或纯技能不会实现要求的解锁路径。 */
bool HasPaidTroops(const Snapshot& s,const std::vector<Action>& plan) {
	return std::any_of(plan.begin(),plan.end(),[&](const Action& action) {
		return action.option>=0 && action.option<static_cast<int>(s.options.size())
			&& s.options[action.option].device<0 && s.options[action.option].cost>0;
	});
}
/** 只统计本案真实士兵类型，用于区分大单一队伍与完整混编探索。 */
int TroopCompositionSize(const Snapshot& s,const std::vector<Action>& plan) {
	std::set<int> types;
	for(const auto& action:plan) if(action.option>=0 && action.option<static_cast<int>(s.options.size())
		&& s.options[action.option].device<0) types.insert(s.options[action.option].type);
	return static_cast<int>(types.size());
}
/** 仅标记具有实际抗灰烬护卫、支援和两路兵力的完整案；此诊断不进入评分。 */
bool IsAshSupportedFormation(const Snapshot& s,const std::vector<Action>& plan) {
	bool guard=false,support=false,multipleRows=false;
	int firstRow=-1;
	for(const auto& action:plan) {
		const auto& option=s.options[action.option];
		if(option.device>=0) continue;
		guard|=option.unit.shieldedAshCap>0;
		support|=option.unit.clock.present || option.unit.sniper.enabled;
		if(firstRow<0) firstRow=option.row;
		else multipleRows|=firstRow!=option.row;
	}
	return guard && support && multipleRows;
}
/** 完整世界切换时一起保留所有付费目标，不能只复制首目标却仍计算三株费用/战果。 */
void CopyPrecisionIntent(Result& result,const Snapshot& s) {
	const auto ids=PrecisionIDs(s.precisionTargetID,s.precisionAdditionalTargetIDs,s.precisionTargetLimit);
	result.precisionTargetID=ids.empty() ? 0 : ids.front();
	result.precisionAdditionalTargetIDs=ids.empty() ? std::vector<int>{} : std::vector<int>(ids.begin()+1,ids.end());
}
/** 采购先预留本案完整技能费；所有维修仍与部队、技能共用同一个钱包。 */
int PurchaseBudget(const Snapshot& s) { return s.budget-PrecisionCost(s); }
/** 技能/设备消耗公共资本，买兵只把库存转成原成交价资产；不使用预测收入或未来死亡腾名额。 */
int DeploymentCapacity(const Snapshot& s,std::int64_t deviceCost=0) {
	if(s.deploymentCapital<0) return std::max(0,s.capacity);
	const auto capital=std::max<std::int64_t>(0,s.deploymentCapital-PrecisionCost(s)-std::max<std::int64_t>(0,deviceCost));
	const int remaining=ColdStorageDeploymentRules::Capacity(capital)-std::max(0,s.deploymentOccupied);
	return std::max(0,std::min(s.capacity,remaining));
}
/** 每个合法设备槽只计一次费用；生成阶段允许尚未Repair的重复设置，不把它们当多次付款。 */
std::int64_t DeploymentDeviceCost(const Snapshot& s,const std::vector<Action>& plan) {
	std::array<bool,3> used{};
	std::int64_t cost=0;
	for(const auto& action:plan) {
		if(action.option<0 || action.option>=static_cast<int>(s.options.size())) continue;
		const auto& option=s.options[action.option];
		if(option.device<0 || option.device>=static_cast<int>(used.size()) || option.cost<=0 || used[option.device]) continue;
		used[option.device]=true; cost+=option.cost;
	}
	return cost;
}
/** 下一波技能必须由真实付费兵推进；整队和技能共用当前现金与付费后容量，设备不能代替派兵。 */
bool FuturePrecisionPlanPayable(const Snapshot& s,const std::vector<Action>& plan) {
	std::int64_t total=PrecisionCost(s);
	int troops=0;
	for(const auto& action:plan) {
		if(action.option<0 || action.option>=static_cast<int>(s.options.size()) || !std::isfinite(action.delay)) return false;
		const auto& option=s.options[action.option];
		if(option.cost<=0) return false;
		total+=option.cost;
		if(option.device<0) ++troops;
	}
	return troops>0 && total<=s.budget && troops<=DeploymentCapacity(s,DeploymentDeviceCost(s,plan));
}
/** 未来意图只导出为诊断；Board当下只支付真实购物车，下一轮按真实局面重新选靶和付技能费。 */
void ExportFuturePrecision(const Snapshot& s,Result& result) {
	if(!s.precisionUnlockAfterPurchase || s.precisionReady || result.precisionTargetID<=0) return;
	const auto ids=PrecisionIDs(result.precisionTargetID,result.precisionAdditionalTargetIDs,s.precisionTargetLimit);
	result.forecastPrecisionTargetID=ids.front();
	result.forecastPrecisionAdditionalTargetIDs.assign(ids.begin()+1,ids.end());
	result.forecastPrecisionIce=static_cast<int>(ids.size())*ColdStorageSkillRules::StrikeIceCost;
	result.forecastPrecisionAimStartSeconds=std::max(0.0f,s.precisionUnlockAimStartSeconds);
	result.precisionTargetID=0; result.precisionAdditionalTargetIDs.clear();
}
/** 只延续已经激活的优惠，不假设玩家将来必定续券；整数费用向上取整。 */
float PlayerIceCost(const Snapshot& s, float time, float cost) {
	return time < s.discountRemaining && cost > 0 ? std::ceil(cost/ColdStorageSkillRules::DiscountDivisor) : cost;
}

/** 掉头只检查本体；头盔、门盾和气球均不能遮住本体阈值，零阈值保留不掉头阶段。 */
void UpdateHeadlessState(Unit& unit) {
	const float body=unit.body.health-unit.helmHealth-unit.shieldHealth-(unit.balloon.present ? unit.balloon.health : 0);
	if(unit.temporalStopHealth>0 && body<=unit.temporalStopHealth) unit.headless=true;
}

/** 对齐 Zombie::Update 的无头流血与35生命倒地；不沿护甲伤害链，也不生成灰烬损失得分。 */
void AdvanceHeadlessDecay(Unit& unit) {
	UpdateHeadlessState(unit);
	if(!unit.headless || unit.body.health<=0) return;
	const float body=std::max(0.0f,unit.body.health-unit.helmHealth-unit.shieldHealth
		-(unit.balloon.present ? unit.balloon.health : 0));
	// 正式流血每游戏秒扣本体上限10%，整数扣血保留余量；冰冻、减速和停步不延长寿命。
	const float amount=unit.headlessBleedRemainder+std::max(1.0f,unit.maximumBody*.10f)*kStep;
	const float damage=std::min(body,std::floor(amount));
	unit.headlessBleedRemainder=amount-damage;
	unit.body.health-=damage;
	// 正式层此时已进入禁攻击、禁碰撞的死亡阶段；数值世界不继续计算尸体资产和进屋。
	if(body-damage<=35) unit.body.health=0;
}

/** 维护“总剩余生命 + 其中护盾”的双层投影；穿透同时扣两层，剩余门不能救活已死本体。 */
float ApplyDamage(Unit& unit, float damage, bool penetrate = false, bool bypass = false, bool discardOverflow = false, PlantDamageOrigin origin = {},
    std::vector<Unit>* group = nullptr, const Snapshot* state = nullptr, float time = 0) {
	if(unit.jack.present && !unit.jack.elite && unit.jack.phase==JackBoxRules::Forecast::Phase::POPPING) return 0;
	if (origin.IsValid() && unit.adaptedOrigin.IsValid() && origin == unit.adaptedOrigin) return 0;
    if (origin != PlantDamageOrigin::Ash()) {
        Unit* guard = nullptr;
        if (group && state && unit.pressureGroupPresent) {
            const auto eligible = [&](const Unit& candidate) {
                return candidate.pressureGuard && candidate.helmHealth > 0 && !candidate.headless
                    && candidate.body.health > 0 && candidate.body.spawnAt <= time && candidate.body.stopped <= 0
                    && std::abs(candidate.body.row-unit.body.row) <= 1
                    && std::abs(candidate.body.x-unit.body.x) <= state->cellWidth*PressureGuardRules::RadiusCells;
            };
            for (auto& candidate : *group) if (candidate.id == unit.pressureProtectorID && eligible(candidate)) {
                guard = &candidate; break;
            }
            if (!guard) {
                for (auto& candidate : *group) if (eligible(candidate)
                    && (!guard || candidate.body.x < guard->body.x
                        || (candidate.body.x == guard->body.x && candidate.id < guard->id))) guard=&candidate;
                unit.pressureProtectorID = guard ? guard->id : 0;
            }
            if (guard && guard != &unit) {
                // 无群体上下文的递归只结算承接者自己的生命层，不可能二次转移。
                const float share=unit.pressureGuard && unit.helmHealth>0 ? 1.0f : PressureGuardRules::Share;
                ApplyDamage(*guard, damage*share, false, false, false, {}, nullptr, nullptr, time);
                damage *= 1-share;
            }
        }
        if (unit.pressureGuard && unit.helmHealth > 0) damage=unit.pressureWindow.Consume(damage,time);
    }
	if (unit.adaptiveHelmet > 0 && origin.IsValid() && damage >= unit.adaptiveHelmet) {
		const float lost = unit.adaptiveHelmet;
		unit.body.health -= lost; unit.adaptiveHelmet = 0; unit.adaptedOrigin = origin;
		unit.helmHealth = std::max(0.0f,unit.helmHealth-lost);
		return lost; // 首次击穿整击只提交适应，不允许溢入本体。
	}
	auto& health = unit.body.health;
    const float before = health;
    const auto balloonHit=unit.balloon.AbsorbDamage(damage);
    health-=balloonHit.absorbed; damage=balloonHit.remainder;
    if(balloonHit.drowned) {health=0;return before;}
    if(damage<=0) return before-health;
	const float shield = std::clamp(unit.shieldHealth,0.0f,std::max(0.0f,health));
	const float shieldLoss = bypass ? 0 : std::min(shield,damage);
	const float vitalLoss = bypass || penetrate ? damage : discardOverflow && shield > 0 ? 0 : damage-shieldLoss;
	const float vital = health-shield-vitalLoss;
	if (unit.goldenDrive.enabled && vitalLoss>0) unit.goldenDrive.undamaged=0;
	unit.shieldHealth = shield-shieldLoss;
	// 西瓜和大喷穿的是二类门盾；一类冰盾仍先于本体承受 vitalLoss。
	unit.adaptiveHelmet = std::max(0.0f,unit.adaptiveHelmet-std::max(0.0f,vitalLoss));
	unit.ritual.armor = std::max(0.0f,unit.ritual.armor-std::max(0.0f,vitalLoss));
	unit.repair.health = std::max(0.0f,unit.repair.health-std::max(0.0f,vitalLoss));
	unit.helmHealth = std::max(0.0f,unit.helmHealth-std::max(0.0f,vitalLoss));
	health = vital > 0 ? vital+unit.shieldHealth : 0;
	UpdateHeadlessState(unit);
	return before-health;
}
struct PlantHit { float rate; bool penetrate, bypass, discardOverflow; };
/** 目标资格随气球落地和矿工出土变化；不能把出生时的资格固定到整个时域。 */
bool CanTargetProjectile(const Unit& unit,bool flying) {
	return unit.balloon.CanTargetProjectile(flying) && (!unit.digger.present || unit.digger.CanTargetProjectile(flying));
}
/** 把单击修正换算回持续火力；西瓜溅射先分配伤害预算，再应用目标自身每击上限。 */
PlantHit DescribePlantHit(const Unit& unit, const Plant& plant, float fraction = 1) {
	const bool shielded = unit.shieldHealth > 0;
	const bool blocksFume = shielded && unit.blocksFumePiercing && plant.fume;
	const bool bypass = plant.bypassShield && !(shielded && unit.blocksShieldBypass);
	const float multiplier = plant.fume ? unit.fumeMultiplier : 1;
	const float hit = plant.hitDamage*fraction*multiplier;
	const float cap = shielded && !bypass ? unit.shieldedHitCap : 0;
	const float reduction = cap > 0 && hit > 0 ? std::min(1.0f,cap/hit) : 1;
	return {plant.dps*fraction*multiplier*reduction,plant.melon || (plant.fume && !blocksFume),bypass,blocksFume};
}
/** 使用攻击开始时的群体状态判断承压保护；逻辑对象坐标与实体范围一致。 */
bool PressureAshProtected(const Unit& target, const std::vector<Unit>& units, const Snapshot& state, float time) {
    if(target.pressureGuard) return false;
    return std::any_of(units.begin(),units.end(),[&](const Unit& guard) {
        return guard.pressureGuard && guard.helmHealth>0 && !guard.headless && guard.body.health>0
            && guard.body.spawnAt<=time && guard.body.stopped<=0 && guard.ferryPassengerEligible
            && std::abs(guard.body.row-target.body.row)<=1
            && std::abs(guard.body.x+guard.body.blastAnchorOffset-target.body.x-target.body.blastAnchorOffset)
                <=state.cellWidth*PressureGuardRules::RadiusCells;
    });
}

/** 冻结同次灰烬的承压名单，避免护卫在遍历中先死导致同行队友失去本次保护。 */
std::vector<unsigned char> FreezePressureAsh(const std::vector<Unit>& units, const Snapshot& state, float time) {
    std::vector<unsigned char> result(units.size());
    if(std::none_of(units.begin(),units.end(),[](const Unit& u) {return u.pressureGuard;})) return result;
    for(size_t i=0;i<units.size();++i) result[i]=PressureAshProtected(units[i],units,state,time);
    return result;
}

/** 已提交大招逐击结算；保护先同时降低化灰阈值和伤害，不转移也不占普通伤害限额。 */
float ApplyDiscreteHit(Unit& unit, float damage, bool ash, std::vector<Unit>* group = nullptr, const Snapshot* state = nullptr, float time = 0,
    bool pressureAshProtected = false) {
    const bool protectedAsh=ash && pressureAshProtected && !unit.pressureGuard;
    if(protectedAsh) damage=std::ceil(damage/PressureGuardRules::AshDivisor);
	if(ash && unit.adaptedOrigin==PlantDamageOrigin::Ash()) return 0;
	if(ash && unit.digger.present && unit.digger.AshKillsDirectly(damage)) {
		const float lost=unit.body.health; unit.body.health=0; return lost;
	}
    const float armor = unit.ritual.present ? unit.ritual.armor : unit.helmHealth;
    // 与实体相同：承压保护先扣门盾和头甲，再用余伤判断本体化灰；未保护时仍可越甲斩杀。
    const float bodyAshDamage=protectedAsh ? std::max(0.0f,damage-armor-unit.shieldHealth) : damage;
	if(ash && unit.jack.elite && bodyAshDamage>=unit.body.health-unit.helmHealth-unit.shieldHealth) unit.jack.flightsCancelled=true;
	if(ash && unit.jack.present && !unit.jack.elite && unit.jack.phase==JackBoxRules::Forecast::Phase::POPPING) {
        if(bodyAshDamage<unit.body.health-unit.helmHealth-unit.shieldHealth) return 0;
        const float lost=unit.body.health; unit.body.health=0; return lost;
    }
	const bool bodyThreshold=unit.ashBodyThreshold && !unit.pressureGuard && unit.adaptiveHelmet<=0
		&& !(unit.insulator && unit.helmHealth>0) && !(unit.shieldedAshCap>0 && unit.shieldHealth>0);
	if (ash && (bodyThreshold || unit.ritual.present || unit.clock.present) && bodyAshDamage >= unit.body.health-armor-unit.shieldHealth) {
		const float lost = unit.body.health; unit.body.health = 0; unit.ritual.armor = 0; unit.helmHealth = 0; return lost;
	}
	const float cap = unit.shieldHealth > 0 ? (ash ? unit.shieldedAshCap : unit.shieldedHitCap) : 0;
	return ApplyDamage(unit,cap > 0 ? std::min(damage,cap) : damage,false,false,false,
		ash ? PlantDamageOrigin::Ash() : PlantDamageOrigin::FromPlant(PlantType::PLANT_DAWNLOTUS),group,state,time);
}

/** 返回本次搜索阶段的容量；升级阶段可以比较整队，但不设置最低购买量。 */
int ActionLimit(const Snapshot& s,std::int64_t deviceCost=0) {
	return std::max(0,std::min(DeploymentCapacity(s,deviceCost)+(s.weatherStation ? 3 : 0),s.searchVersion == 2 ? kPortfolioActions+(s.weatherStation ? 3 : 0) : s.stateModel ? kAdaptiveActions : kMaxActions));
}
float DelayLimit(const Snapshot& s) { return s.searchVersion == 2 ? kPortfolioDelay : s.stateModel ? kAdaptiveDelay : kMaxDelay; }
/** 已付长队列需要完整战斗预测，但不因此跳过新购物车的小队搜索阶段。 */
bool FullForecast(const Snapshot& s) { return s.searchVersion == 2 || !s.committed.empty(); }
float Horizon(const Snapshot& s) { return FullForecast(s) ? kPortfolioHorizon : s.stateModel ? kAdaptiveHorizon : kHorizon; }

/** 正式搜索与显式案诊断共用资本驱动的长时域；不能拿小队窗口的乐观案比较整队等待。 */
bool ExpandFundedPortfolio(Snapshot& state) {
	const auto cheapest=std::min_element(state.options.begin(),state.options.end(),[](const Option& a,const Option& b) { return a.cost<b.cost; });
	const bool canExploreLarger=cheapest!=state.options.end() && cheapest->cost>0
		&& std::min(DeploymentCapacity(state),PurchaseBudget(state)/cheapest->cost)>ActionLimit(state);
	const bool armedDefense=std::any_of(state.plants.begin(),state.plants.end(),[](const Plant& p){return p.health>0 && p.dps>0;})
		|| std::any_of(state.counters.begin(),state.counters.end(),[](const Counter& c){return !c.blast.committed && !c.consumesPlant && c.blast.damage>0;})
		|| std::any_of(state.rowStrikes.begin(),state.rowStrikes.end(),[](const RowStrike& strike){return strike.damage>0;});
	const bool expanded=state.searchVersion==1 && canExploreLarger
		&& (PaidSearchCapital(state)>=ColdStorageDeploymentRules::GrowthCapital || state.resumePortfolio
			// 已有阵地时，微量削血不能锁住小队阶段；有钱比较完整攻势才有机会跨过防线。
			|| (armedDefense
				&& PurchaseBudget(state)>=cheapest->cost*ActionLimit(state)*2));
	if(expanded) state.searchVersion=2;
	return expanded;
}

/** 根据同帧网格高度投影爆区Y；屋顶使用采样坡度，纯数值夹具保留行高默认值。 */
float ForecastRowY(const Snapshot& state,int row,float x) {
    if(row<0 || row>=state.rows) return 0;
    const int first=row*state.columns;
    if(first>=0 && first<54 && state.cellY[first]!=0) {
        const float column=std::clamp((x-state.gridLeft)/state.cellWidth-.5f,0.0f,static_cast<float>(state.columns-1));
        const int left=static_cast<int>(column),right=std::min(left+1,state.columns-1);
        return state.cellY[first+left]+(state.cellY[first+right]-state.cellY[first+left])*(column-left);
    }
    return state.rowY[row]!=0 ? state.rowY[row] : (row+.5f)*state.cellHeight;
}

/** 先提交清洁车触发和扫过区间，再判断进屋；未出生部队不能被提前清掉，也不能重复使用同一辆车。 */
void AdvanceMowers(const Snapshot& state,std::vector<Mower>& mowers, std::vector<Unit>& units, float time, float rightEdge) {
	for (auto& mower : mowers) {
		if (!mower.active) continue;
		const float left = mower.x;
		const float movement = mower.moving ? mower.speed*kStep : 0;
		const float right = mower.x+mower.width+movement;
		for (auto& unit : units) {
			auto& u = unit.body;
			if (u.health <= 0 || u.spawnAt > time || u.row != mower.row
				|| (unit.digger.present && !unit.digger.CanMower())
				|| (mower.height>0 && (ForecastRowY(state,u.row,u.x)+unit.boundsY+unit.boundsHeight<mower.y
                    || ForecastRowY(state,u.row,u.x)+unit.boundsY>mower.y+mower.height))
                || u.x+u.boundsOffset+u.boundsWidth < left || u.x+u.boundsOffset > right) continue;
			mower.moving = true;
			if (unit.consumesOtherMowers) for (auto& other : mowers) if (&other != &mower) other.active = false;
			if (!unit.mowerImmune) { u.health = 0; unit.temporalIrreversible = true; }
		}
		mower.x += movement;
		if (mower.x > rightEdge+100) mower.active = false;
	}
}

/** 富余实时预算直接尝试多类型/非等量/有限多路和错峰，不按单位能力指定组合。 */
std::vector<Action> SampleDiversePortfolio(const Snapshot& s,std::mt19937& rng,int trial,int count) {
	std::vector<std::vector<int>> types;
	std::vector<int> rows;
	for(int i=0;i<static_cast<int>(s.options.size());++i) {
		const auto& option=s.options[i];
		if(option.device>=0 || option.cost<=0 || option.cost>PurchaseBudget(s)) continue;
		auto group=std::find_if(types.begin(),types.end(),[&](const auto& entries) {
			const auto& first=s.options[entries.front()]; return first.type==option.type && first.cost==option.cost;
		});
		if(group==types.end()) types.push_back({i}); else group->push_back(i);
		if(std::find(rows.begin(),rows.end(),option.row)==rows.end()) rows.push_back(option.row);
	}
	if(types.empty() || count<=0) return {};
	std::shuffle(types.begin(),types.end(),rng); std::shuffle(rows.begin(),rows.end(),rng);
	const int maxTypes=std::min({count,kPortfolioCompositionLimit,static_cast<int>(types.size())});
	// 首案先比较较宽的完整协同；后续仍覆盖单类安全经营、小队和新构成，不能要求买某个形状。
	const int selected=trial==0 ? std::min(maxTypes,3+static_cast<int>(rng()%4)) : 1+static_cast<int>(rng()%maxTypes);
	const int routes=1+static_cast<int>(rng()%std::min(3,static_cast<int>(rows.size())));
	std::vector<int> amounts(selected,1),weights(selected);
	int totalWeight=0;
	for(int& weight:weights) { weight=rng()%3==0 ? 0 : 1<<static_cast<int>(rng()%6); totalWeight+=weight; }
	if(totalWeight==0) {weights[rng()%selected]=1;totalWeight=1;}
	int assigned=selected;
	for(int group=0;group<selected;++group) {
		amounts[group]+=(count-selected)*weights[group]/totalWeight; assigned+=amounts[group]-1;
	}
	while(assigned<count) { const int group=rng()%selected; if(weights[group]>0) {++amounts[group];++assigned;} }
	const float duration=static_cast<float>(rng()%(static_cast<int>(DelayLimit(s)*2)+1))*.5f;
	const bool gradual=rng()%2==0;
	std::vector<Action> plan;
	for(int group=0;group<selected;++group) {
		const auto& choices=types[group];
		for(int n=0;n<amounts[group];++n) {
			const int row=rows[n%routes];
			auto option=std::find_if(choices.begin(),choices.end(),[&](int index){return s.options[index].row==row;});
			// 水路/陆地等资格仍由真实合法选项决定，不把分路抽样变成越地形部署。
			const int chosen=option==choices.end() ? choices[rng()%choices.size()] : *option;
			const float delay=gradual ? duration*n/std::max(1,amounts[group]-1) : duration*group/std::max(1,selected-1);
			plan.push_back({chosen,delay});
		}
	}
	return plan; // 统一Repair仍复核整案的钱包、设备与同时名额，不在采样器借钱。
}

/** 独立采样完整编队，跨过单只亏损的局部最优；只使用合法选项，不指定任何兵种或必攻路线。 */
std::vector<Action> SamplePortfolio(const Snapshot& s, std::mt19937& rng, int trial) {
	std::vector<Action> plan;
	const int limit = ActionLimit(s);
	if (limit == 0 || s.options.empty()) return plan;
    // 设备是一次性设置，不是可重复购买的士兵。整队抽样从合法兵种中取组，
    // 避免抽到天气后被 Repair 去重成单按钮；后续自由变异仍能加入天气联动。
    std::vector<int> troopOptions;
    for(int i=0;i<static_cast<int>(s.options.size());++i) {
        if(s.options[i].device<0 && s.options[i].cost<=PurchaseBudget(s)) troopOptions.push_back(i);
    }
	const auto sampleOption=[&]() { return troopOptions.empty() ? static_cast<int>(rng()%s.options.size())
        : troopOptions[rng()%troopOptions.size()]; };
	const int sampleLimit=s.deploymentCapital>=0 && !troopOptions.empty() ? std::min(limit,DeploymentCapacity(s)) : limit;
	if(sampleLimit<=0) return plan;
	const int count = trial % 4 == 0 ? sampleLimit : 1 + rng() % sampleLimit;
	if(s.timeLimitedSearch && s.netEconomy && CapitalUtilityScale(s)<1)
		return SampleDiversePortfolio(s,rng,trial,count);
	const int groups = 1 + rng() % 3;
	const bool sameType = rng() % 2 == 0, sameRow = rng() % 2 == 0;
	const int base = sampleOption();
	// 首个整队候选比较同步到场，其余仍自由抽取错峰时间，避免完整兵力被稀释成单只接敌。
	const float duration = trial == 0 ? 0 : static_cast<float>(rng() % (static_cast<int>(DelayLimit(s)*2)+1)) * .5f;
	const bool gradual = rng() % 2 == 0;
	for (int group = 0; group < groups; ++group) {
		const int choice = group == 0 ? base : sampleOption();
		const auto& type = s.options[sameType ? base : choice];
		const int row = s.options[sameRow ? base : choice].row;
		const auto option = std::find_if(s.options.begin(),s.options.end(),[&](const auto& o) {
			return o.type == type.type && o.cost == type.cost && o.row == row;
		});
		if (option == s.options.end()) continue;
		for (int i = group*count/groups; i < (group+1)*count/groups; ++i) {
			const float delay = gradual ? duration*i/std::max(1,count-1) : duration*group/std::max(1,groups-1);
			plan.push_back({static_cast<int>(option-s.options.begin()),delay});
		}
	}
	int totalCost = 0;
	for (const auto& action : plan) totalCost += s.options[action.option].cost;
	if (totalCost > PurchaseBudget(s) && !plan.empty()) {
		// 按总预算缩小整案，不让后续 Repair 总是先买光前一批、再删掉后面的配合兵力。
		const int count = std::max(1,static_cast<int>(plan.size())*PurchaseBudget(s)/totalCost);
		std::vector<Action> affordable;
		for (int i = 0; i < count; ++i) affordable.push_back(plan[i*plan.size()/count]);
		plan = std::move(affordable);
	}
	return plan;
}

/** 分层抽样先给各可支付兵种一个合法落点，避免大兵池把低频经济/协同能力淹没；不按角色加权。 */
std::vector<int> SampleTypeCoverage(const Snapshot& s, std::mt19937& rng, int limit) {
	std::vector<std::vector<int>> types;
	for (int i=0; i<static_cast<int>(s.options.size()); ++i) {
		const auto& option = s.options[i];
		if (option.cost <= 0 || option.cost > PurchaseBudget(s)) continue;
		const auto group = std::find_if(types.begin(),types.end(),[&](const auto& entries) {
			return s.options[entries.front()].type == option.type;
		});
		if (group == types.end()) types.push_back({i});
		else group->push_back(i);
	}
	// 小兵池沿用已验证的随机流和搜索过程；只修复宽于小队容量的兵池覆盖稀释。
	if (types.size() <= kMaxActions) return {};
	std::shuffle(types.begin(),types.end(),rng);
	std::vector<int> result;
	for (const auto& type : types) {
		if (static_cast<int>(result.size()) >= limit) break;
		result.push_back(type[rng()%type.size()]);
	}
	return result;
}

/** 将一个新兵种试入已有优案；先腾出预算和容量，不能让合法候选在 Repair 中总被队尾截掉。 */
std::vector<Action> IntroduceOption(const Snapshot& s, std::vector<Action> plan, int option, std::mt19937& rng) {
	int cost = s.options[option].cost;
	for (const auto& action : plan) cost += s.options[action.option].cost;
	const auto fitsDeployment=[&]() {
		auto full=plan; full.push_back({option,0});
		const auto fees=DeploymentDeviceCost(s,full);
		const int troops=static_cast<int>(std::count_if(full.begin(),full.end(),[&](const Action& action){return s.options[action.option].device<0;}));
		return full.size()<=static_cast<size_t>(ActionLimit(s,fees)) && troops<=DeploymentCapacity(s,fees);
	};
	while (!plan.empty() && (!fitsDeployment() || cost > PurchaseBudget(s))) {
		const auto at = rng()%plan.size();
		cost -= s.options[plan[at].option].cost;
		plan.erase(plan.begin()+at);
	}
	// 无队友时从立即行动起评；已有编队仍自由比较分批时机，生产单位没有固定保护模板。
	const float delay = plan.empty() ? 0 : (rng()%(static_cast<int>(DelayLimit(s)*2)+1))*.5f;
	plan.push_back({option,delay});
	return plan;
}

/** 按兵种/价格聚合法路线，避免合法行较多的类型挤占组合机会；不检查能力或角色。 */
std::vector<std::vector<int>> LegalOptionGroups(const Snapshot& s, std::mt19937& rng) {
	std::vector<std::vector<int>> groups;
	for (int i=0; i<static_cast<int>(s.options.size()); ++i) {
		const auto& option = s.options[i];
		if (option.cost <= 0 || option.cost > PurchaseBudget(s)) continue;
		const auto group = std::find_if(groups.begin(),groups.end(),[&](const auto& entries) {
			const auto& first = s.options[entries.front()];
			return first.type == option.type && first.cost == option.cost;
		});
		if (group == groups.end()) groups.push_back({i});
		else group->push_back(i);
	}
	std::shuffle(groups.begin(),groups.end(),rng);
	for (auto& group : groups) std::shuffle(group.begin(),group.end(),rng);
	return groups;
}

/** 先让可配对类型各参与一次，再遍历打乱的剩余配对；小预算下也不反复抽同一对。 */
std::vector<std::pair<int,int>> CombinationPairs(const Snapshot& s,
	const std::vector<std::vector<int>>& groups, std::mt19937& rng) {
	std::vector<std::pair<int,int>> pairs, ordered;
	if (ActionLimit(s)<2) return pairs;
	for (int a=0;a<static_cast<int>(groups.size());++a) for (int b=a;b<static_cast<int>(groups.size());++b) {
		const auto& x=s.options[groups[a].front()]; const auto& y=s.options[groups[b].front()];
		if (x.cost+y.cost<=PurchaseBudget(s) && !(x.device>=0 && x.device==y.device)) pairs.emplace_back(a,b);
	}
	std::shuffle(pairs.begin(),pairs.end(),rng);
	std::vector<bool> covered(groups.size()), used(pairs.size());
	for (int pass=0;pass<2;++pass) for (size_t i=0;i<pairs.size();++i) {
		const auto [a,b]=pairs[i];
		const bool introduces=pass==0 ? a!=b && !covered[a] && !covered[b] : !covered[a] || !covered[b];
		if (!used[i] && introduces) {
			ordered.push_back(pairs[i]); used[i]=true; covered[a]=covered[b]=true;
		}
	}
	for (size_t i=0;i<pairs.size();++i) if (!used[i]) ordered.push_back(pairs[i]);
	return ordered;
}

/** 对任意类型统一抽数量比例、同路/分路和同步/错峰；容量与费用决定可搜索的规模。 */
std::vector<Action> SampleCombination(const Snapshot& s, const std::vector<std::vector<int>>& groups,
	const std::pair<int,int>& pair, std::mt19937& rng, bool simultaneous, int shape) {
	const auto& first = groups[pair.first]; const auto& second = groups[pair.second];
	const int a = first[rng()%first.size()];
	int b = second[rng()%second.size()];
	const int route=shape/4%3;
	if (route<2) {
		const auto matchingRow = std::find_if(second.begin(),second.end(),[&](int option) {
			return (s.options[option].row == s.options[a].row)==(route==0);
		});
		if (matchingRow != second.end()) b = *matchingRow;
	}
	const std::int64_t deviceFees=(s.options[a].device>=0 ? static_cast<std::int64_t>(s.options[a].cost) : 0)
		+(s.options[b].device>=0 ? static_cast<std::int64_t>(s.options[b].cost) : 0);
	const auto copies=[&](int option,int reservedCost,int reservedSlots,int reservedTroops) {
		const auto& choice=s.options[option];
		return std::min({choice.device>=0 ? 1 : DeploymentCapacity(s,deviceFees)-reservedTroops,
			ActionLimit(s,deviceFees)-reservedSlots,(PurchaseBudget(s)-reservedCost)/choice.cost});
	};
	int countA=1, countB=1;
	if (shape%4==1) countB=copies(b,s.options[a].cost,1,s.options[a].device<0);
	else if (shape%4==2) countA=copies(a,s.options[b].cost,1,s.options[b].device<0);
	else if (shape%4==3) {
		countA=1+rng()%std::max(1,copies(a,s.options[b].cost,1,s.options[b].device<0));
		countB=1+rng()%std::max(1,copies(b,countA*s.options[a].cost,countA,s.options[a].device<0 ? countA : 0));
	}
	float startA=0, startB=0;
	if (!simultaneous) {
		// 既探索短间隔协作，也探索跨冷却分批；两种分布对所有兵种完全相同。
		const float span = rng()%2 == 0 ? std::min(6.0f,DelayLimit(s)) : DelayLimit(s);
		(rng()%2==0 ? startA : startB)=(rng()%(static_cast<int>(span*2)+1))*.5f;
	}
	const float spacing=simultaneous ? 0 : static_cast<float>(rng()%3)*.5f;
	std::vector<Action> plan;
	for (int side=0;side<2;++side) {
		const auto& options=side==0 ? first : second;
		for (int i=0;i<(side==0 ? countA : countB);++i)
			plan.push_back({route==2 ? options[i%options.size()] : side==0 ? a : b,
				std::min(DelayLimit(s),(side==0 ? startA : startB)+i*spacing)});
	}
	return plan;
}

/** 普通细化替换少量成员；经营分支优先追加，满额时可比较削减少数成员或主要批次，不指定能力搭配。 */
std::vector<Action> RefineCohort(const Snapshot& s,const std::vector<Action>& anchor,
	const std::vector<int>& group,int count,float delay,bool keepAnchor=false,bool trimMinority=false,bool bulk=false) {
	if(anchor.empty() || group.empty()) return {};
	std::array<int,6> rows{};
	std::vector<std::pair<std::pair<int,int>,int>> types;
	for(const auto& action:anchor) {
		const auto& option=s.options[action.option]; ++rows[option.row];
		const auto type=std::make_pair(option.type,option.cost);
		auto found=std::find_if(types.begin(),types.end(),[&](const auto& entry){return entry.first==type;});
		if(found==types.end()) types.push_back({type,1}); else ++found->second;
	}
	const int row=static_cast<int>(std::max_element(rows.begin(),rows.end())-rows.begin());
	const auto largest=std::max_element(types.begin(),types.end(),[](const auto& a,const auto& b){return a.second<b.second;});
	const auto dominant=largest->first; int dominantRemaining=largest->second;
	const auto sameRow=std::find_if(group.begin(),group.end(),[&](int option){return s.options[option].row==row;});
	const int option=sameRow==group.end() ? group.front() : *sameRow;
	const auto& choice=s.options[option];
	count=std::min(count,choice.device>=0 ? 1 : bulk ? ActionLimit(s) : kRefinementMaxReplacements);
	auto plan=anchor;
	// 天气设置是独占槽位，换挡应替换已有设置，不能在修复时丢掉新设置。
	if(choice.device>=0) plan.erase(std::remove_if(plan.begin(),plan.end(),[&](const Action& action) {
		return s.options[action.option].device==choice.device;
	}),plan.end());
	if(s.deploymentCapital>=0 && choice.device<0) {
		// 先保留原主要成员，再用实际新名额限制追加批次；不要反复生成付费后必定超额的伙伴案。
		count=std::min(count,std::max(0,DeploymentCapacity(s,DeploymentDeviceCost(s,plan))-(keepAnchor ? 1 : 0)));
		if(count<=0) return {};
	}
	const auto removeMember=[&]() {
		// 经营分支保留一名主要成员，避免试四名搭档时把四名工人全部换掉，丢失后续协同目标。
		if(keepAnchor && dominantRemaining<=1) return false;
		const auto member=std::find_if(plan.rbegin(),plan.rend(),[&](const Action& action) {
			const auto& original=s.options[action.option];
			return std::make_pair(original.type,original.cost)==dominant;
		});
		if(member==plan.rend()) return false;
		plan.erase(std::next(member).base()); --dominantRemaining; return true;
	};
	// 不应在钱包和名额都充足时为了补前排先删工人，导致有利协同被降成少数工人的亏损案。
	if(!keepAnchor) for(int i=0;i<count;++i) if(!removeMember()) break;
	int cost=count*choice.cost, troops=choice.device<0 ? count : 0;
	for(const auto& action:plan) { cost+=s.options[action.option].cost; troops+=s.options[action.option].device<0; }
	std::int64_t deviceFees=DeploymentDeviceCost(s,plan)+(choice.device>=0 ? static_cast<std::int64_t>(count)*choice.cost : 0);
	// 保留引入的成员，先腾出真实费用/名额；不能靠 Repair 又把新搭档从队尾删掉。
	while(cost>PurchaseBudget(s) || static_cast<int>(plan.size())+count>ActionLimit(s,deviceFees) || troops>DeploymentCapacity(s,deviceFees)) {
		// 已生产的多类案缺少一点预算时也比较删便宜的少数成员；不能只删主要队员，
		// 永远留下探索时带进来的冗员。删掉的成员是否必要仍由完整推演检验，不按能力指定。
		if(trimMinority) {
			auto minority=plan.end();
			for(auto member=plan.begin();member!=plan.end();++member) {
				const auto& original=s.options[member->option];
				if(std::make_pair(original.type,original.cost)==dominant) continue;
				if(minority==plan.end() || original.cost<s.options[minority->option].cost) minority=member;
			}
			if(minority!=plan.end()) {
				const auto& original=s.options[minority->option]; cost-=original.cost; troops-=original.device<0;
				plan.erase(minority);
				deviceFees=DeploymentDeviceCost(s,plan)+(choice.device>=0 ? static_cast<std::int64_t>(count)*choice.cost : 0);
				continue;
			}
		}
		if(!removeMember()) return {};
		cost-=dominant.second;
		// 主要队员可能是设备；名额统计仍按实际行动种类区分。
		const auto original=std::find_if(s.options.begin(),s.options.end(),[&](const auto& entry) {
			return std::make_pair(entry.type,entry.cost)==dominant;
		});
		troops-=original->device<0;
		deviceFees=DeploymentDeviceCost(s,plan)+(choice.device>=0 ? static_cast<std::int64_t>(count)*choice.cost : 0);
	}
	for(int i=0;i<count;++i) plan.push_back({option,std::min(DelayLimit(s),delay)});
	return plan;
}

/** 围绕未购买的经营案试任意兵种的先行/同批/后援；已兑现生产时可保留原队时序，并按主要批次对齐伙伴。 */
std::vector<Action> RefineInvestment(const Snapshot& s,const std::vector<Action>& anchor,
	const std::vector<int>& group,int shape,bool rephaseExisting=false,bool preserveTiming=false) {
	auto staged=anchor;
	// 中间态可能靠整批晚出生避开灰烬；接前排时从相对时序重新比较，
	// 不能让前排现在出生、工人几十秒后才到，误把过早死掉的肉盾当成协同无效。
	const float first=preserveTiming || staged.empty() ? 0 : std::min_element(staged.begin(),staged.end(),
		[](const Action& a,const Action& b){return a.delay<b.delay;})->delay;
	for(auto& action:staged) action.delay-=first;
	const int timing=shape/kRefinementMaxReplacements%3;
	// 大队需要足够数量的任意搭档才能跨过持续火力；交错小批与半队规模，不绑定能力标签。
	const bool bulk=anchor.size()>=2*kRefinementMaxReplacements && shape%kRefinementMaxReplacements>=2;
	const int copies=bulk ? std::max(1,std::min(ActionLimit(s)/2,static_cast<int>(anchor.size())))
		: 1+shape%kRefinementMaxReplacements;
	if(preserveTiming) {
		// 已有生产说明相对进场顺序有效；新伙伴与数量最多的队员同步，而非与最早的前排同步。
		// 成功跨过反制冷却的绝对等待同样保留，不在这条分支强制把全队归零。
		std::vector<std::pair<std::pair<int,int>,int>> types;
		for(const auto& action:staged) {
			const auto& option=s.options[action.option]; const auto type=std::make_pair(option.type,option.cost);
			const auto found=std::find_if(types.begin(),types.end(),[&](const auto& entry){return entry.first==type;});
			if(found==types.end()) types.push_back({type,1}); else ++found->second;
		}
		if(types.empty()) return {};
		const auto dominant=std::max_element(types.begin(),types.end(),[](const auto& a,const auto& b){return a.second<b.second;})->first;
		float arrival=DelayLimit(s);
		for(const auto& action:staged) { const auto& option=s.options[action.option];
			if(std::make_pair(option.type,option.cost)==dominant) arrival=std::min(arrival,action.delay);
		}
		arrival+=timing==0 ? -kReinforcementDelay : timing==2 ? kReinforcementDelay : 0;
		if(arrival<0) { for(auto& action:staged) action.delay=std::min(DelayLimit(s),action.delay-arrival); arrival=0; }
		return RefineCohort(s,staged,group,copies,arrival,true,true,bulk);
	}
	if(timing==0) for(auto& action:staged) action.delay=std::min(DelayLimit(s),action.delay+kReinforcementDelay);
	if(rephaseExisting && timing==0 && !group.empty()) {
		const auto& choice=s.options[group.front()]; bool retimed=false;
		// 已经买进探索案的同类也可先行；不能为改时序强加第二只、挤掉工人并增加成本。
		for(auto& action:staged) {
			const auto& existing=s.options[action.option];
			if(existing.type==choice.type && existing.cost==choice.cost) { action.delay=0; retimed=true; }
		}
		if(retimed) return staged;
	}
	return RefineCohort(s,staged,group,copies,
		timing==2 ? kReinforcementDelay : 0,true,false,bulk);
}

/** 同时保留各路优案与构成不同的中间态；两类方案都只作探索，最终仍统一评分与付款。 */
class InvestmentFrontier {
public:
	/** 保护进展只比较相对等待基线的增量，不能拿现有部队的保护替新投入背书。 */
	explicit InvestmentFrontier(float baselineProgress) : mBaselineProgress(baselineProgress) {}
	/** 更新经营分支，不经过采购门禁，使暂时亏损的完整案仍能继续接入保护。 */
	void Observe(const Snapshot& s,const Result& candidate) {
		std::array<int,6> rows{}; int workers=0;
		for(const auto& action:candidate.actions) if(s.options[action.option].unit.body.economic) {
			++workers; ++rows[s.options[action.option].row];
		}
		// 已有工人也能获得新前排/支援；只有实际改善生产时才让无新工人的案进入经营分支。
		if(workers==0) {
			if(candidate.features[4]<=candidate.baselineFeatures[4]+.001f) return;
			for(const auto& unit:s.current) if(unit.body.economic && unit.body.health>unit.productionStopHealth)
				++rows[unit.body.row];
		}
		if(candidate.actions.empty()) return;
		const int row=static_cast<int>(std::max_element(rows.begin(),rows.end())-rows.begin());
		const int index=row*2+(workers>3);
		float purchase=0;
		for(const auto& action:candidate.actions) purchase+=s.options[action.option].cost;
		const float risk=std::clamp((candidate.features[6]-candidate.baselineFeatures[6])/std::max(1.0f,purchase),0.0f,1.0f);
		const float progress=std::max(0.0f,candidate.construction.workerProtectionProgress-mBaselineProgress);
		const auto cash=[](const Result& result) { return result.features[0]+result.features[3]+result.features[4]-result.features[5]; };
		const auto& old=mPlans[index];
		if(!mPresent[index] || candidate.features[4]>old.features[4]+.001f
			|| (std::abs(candidate.features[4]-old.features[4])<=.001f && cash(candidate)>cash(old)+.001f)) {
			mPlans[index]=candidate; mPresent[index]=true;
		}
		// 工人+某个搭档暂时都可能没有收入，不能让廉价裸工人抹掉所有第二层尝试。
		// 构成仅用合法类型/价格标识，不读取工程师、钟匠等能力标签决定搭配。
		std::set<std::pair<int,int>> types;
		for(const auto& action:candidate.actions) {
			const auto& option=s.options[action.option]; types.emplace(option.type,option.cost);
		}
		if(types.size()<2) return;
		const auto found=std::find_if(mDiverse.begin(),mDiverse.end(),[&](const Branch& branch) {
			return branch.index==index && branch.workers==workers && branch.types==types;
		});
		if(found!=mDiverse.end()) {
			if(candidate.features[4]>found->plan.features[4]+.001f
				|| (std::abs(candidate.features[4]-found->plan.features[4])<=.001f
					&& (progress>found->progress+.001f || (std::abs(progress-found->progress)<=.001f && cash(candidate)>cash(found->plan)+.001f)))) {
				found->plan=candidate; found->progress=progress; found->risk=risk;
			}
		} else {
			Branch branch{index,workers,std::move(types),risk,progress,candidate};
			if(mDiverse.size()<kInvestmentDiverseBranches) mDiverse.push_back(std::move(branch));
			else {
				// 每个构成深度保留已兑现保护的优案。只保留全局最多挡灰的一案，会让两类案
				// 反复挤掉刚形成的三/四类协作；这些案都未付款，仍要用完整推演证明回本。
				std::array<size_t,kProtectedCompositionDepths> protectedAt;
				protectedAt.fill(mDiverse.size());
				for(size_t i=0;i<mDiverse.size();++i) {
					const auto& current=mDiverse[i]; if(current.progress<=0) continue;
					const auto depth=std::min(current.types.size()-2,static_cast<size_t>(kProtectedCompositionDepths-1));
					auto& saved=protectedAt[depth];
					if(saved==mDiverse.size() || current.progress>mDiverse[saved].progress
						|| (current.progress==mDiverse[saved].progress && current.plan.score>mDiverse[saved].plan.score)) saved=i;
				}
				size_t victim=mInsertionCursor++%mDiverse.size();
				while(std::find(protectedAt.begin(),protectedAt.end(),victim)!=protectedAt.end())
					victim=mInsertionCursor++%mDiverse.size();
				mDiverse[victim]=std::move(branch);
			}
		}
	}
	/** 轮转实际保存的分支，避免一条路或只买一只的低亏损案吞掉所有经营探索。 */
	const Result* Next(bool broadenIncomplete=false) {
		mDiverseTurn=!mDiverseTurn;
		const auto shield=std::min_element(mDiverse.begin(),mDiverse.end(),[](const Branch& a,const Branch& b) {
			if(std::abs(a.progress-b.progress)>.001f) return a.progress>b.progress;
			if(std::abs(a.risk-b.risk)>.001f) return a.risk<b.risk;
			return a.plan.features[4]>b.plan.features[4];
		});
		for(size_t i=0;i<mPlans.size();++i) {
			const size_t index=mCursor++%mPlans.size();
			if(mPresent[index]) {
				// 尚未回本时也轮转已保存构成；不能被下方最佳挡灰/裸工人分支提前返回，
				// 否则仅改善普通火力生存的第三个搭档永远没有机会接入第四层协作。
				if(broadenIncomplete && mDiverseTurn && !mDiverse.empty()) return &mDiverse[mDiverseCursor++%mDiverse.size()].plan;
				// 只因普通火力先杀光工人而未交灰烬，不是保护进展；须有推演中兑现的挡灰或恢复。
				const auto& current=mPlans[index];
				const float returnGain=current.features[0]-current.baselineFeatures[0]
					+current.features[3]-current.baselineFeatures[3]+current.features[4]-current.baselineFeatures[4]
					-current.features[5]+current.baselineFeatures[5];
				if(current.features[2]<=0 && returnGain<=0) {
					if(shield!=mDiverse.end() && shield->progress>.001f) return &shield->plan;
					// 廉价的一只裸工人可能在首个生产周期前死亡，连支援效果也试不出来。
					// 尚无保护进展时以已有小批分支起步；数量只影响探索，不设置最低购买数。
					for(size_t offset=0;offset<mPlans.size()/2;++offset) {
						const size_t cohort=((index/2+offset)%(mPlans.size()/2))*2+1;
						if(mPresent[cohort]) return &mPlans[cohort];
					}
				}
				if(mDiverseTurn && !mDiverse.empty()) return &mDiverse[mDiverseCursor++%mDiverse.size()].plan;
				return &mPlans[index];
			}
		}
		return nullptr;
	}
	/** 已兑现生产的多类案继续扩展，不被尚未生产但挡灰生命更多的分支反复替换。 */
	const Result* ProducedAnchor(const Snapshot& s) const {
		const Result* best=nullptr;
		const auto consider=[&](const Result& plan) {
			if(plan.features[4]<=plan.baselineFeatures[4]+.001f || (best && plan.score<=best->score)) return;
			std::set<std::pair<int,int>> types;
			for(const auto& action:plan.actions) { const auto& o=s.options[action.option]; types.emplace(o.type,o.cost); }
			// 多路同类仍是一类；至少三个类型才需要下一层协作，简单案沿用原探索。
			if(types.size()<3) return;
			best=&plan;
		};
		for(size_t i=0;i<mPlans.size();++i) if(mPresent[i]) consider(mPlans[i]);
		for(const auto& branch:mDiverse) consider(branch.plan);
		return best;
	}
	/** 轮转确有护工进展的未付款构成，供整段协作的多路反事实；不强制购买或指定兵种。 */
	const Result* ProtectionAnchor(size_t turn) const {
		std::vector<const Result*> plans;
		size_t depth=0;
		for(const auto& branch:mDiverse) if(branch.progress>.001f) depth=std::max(depth,branch.types.size());
		for(const auto& branch:mDiverse) if(branch.progress>.001f && branch.types.size()==depth) plans.push_back(&branch.plan);
		return plans.empty() ? nullptr : plans[turn%plans.size()];
	}
	/** 接近回本的完整中间态可在末段删冗员；仍只是未购买的探索购物车。 */
	std::vector<Action> PruningAnchor() const {
		const Result* best=nullptr;
		const auto consider=[&](const Result& plan) {
			if(plan.actions.empty() || (plan.features[4]<=plan.baselineFeatures[4]
				&& plan.construction.workerProtectionProgress<=mBaselineProgress)) return;
			if(!best || plan.score>best->score) best=&plan;
		};
		for(size_t i=0;i<mPlans.size();++i) if(mPresent[i]) consider(mPlans[i]);
		for(const auto& branch:mDiverse) consider(branch.plan);
		return best ? best->actions : std::vector<Action>{};
	}
	/** 导出未付款动作，下一轮只复用搜索起点，收益须重新推演。 */
	std::vector<std::vector<Action>> Proposals() const {
		std::vector<std::vector<Action>> plans;
		for(const auto& branch:mDiverse) plans.push_back(branch.plan.actions);
		for(size_t i=0;i<mPlans.size();++i) if(mPresent[i]) plans.push_back(mPlans[i].actions);
		return plans;
	}
private:
	struct Branch { int index,workers; std::set<std::pair<int,int>> types; float risk,progress; Result plan; };
	std::vector<Branch> mDiverse;
	size_t mDiverseCursor=0, mInsertionCursor=0;
	bool mDiverseTurn=false;
	std::array<Result,12> mPlans;
	std::array<bool,12> mPresent{};
	size_t mCursor=0;
	float mBaselineProgress;
};

/** 保留尚未回本的多类进攻案供后续协同；仅使用数值副本，不改变评分、钱包或付款门禁。 */
class AssaultFrontier {
public:
	explicit AssaultFrontier(int baselineAccess=0):mBaselineAccess(baselineAccess) {}
	/** 在资本拒绝之前观察完整候选；相对等待的真实削血进展与构成决定探索槽位。 */
	void Observe(const Snapshot& s,const Result& candidate) {
		std::array<int,6> rows{};
		std::set<std::pair<int,int>> types;
		bool attacker=false;
		const bool edibleTarget=std::any_of(s.plants.begin(),s.plants.end(),[](const Plant& p){return p.health>0 && p.edible;});
		for(const auto& action:candidate.actions) {
			const auto& option=s.options[action.option]; types.emplace(option.type,option.cost);
			if(option.device<0) {
				++rows[option.row];
				// 纯护工中间态由经营分支维护；只有真实攻击能力才占攻城探索名额。
				attacker|=!option.unit.body.economic && ((edibleTarget && (option.unit.biteDps>0 || option.unit.body.smashSeconds>0))
					|| option.unit.catapult.present || option.unit.jack.present);
			}
		}
		const bool access=candidate.construction.siegeAccessProgress>mBaselineAccess;
		if((!attacker && !access && candidate.features[1]<=candidate.baselineFeatures[1] && candidate.features[2]<=0)
			|| (types.size()<2 && !access)) return;
		const int row=static_cast<int>(std::max_element(rows.begin(),rows.end())-rows.begin());
		const bool large=candidate.actions.size()>kAssaultSmallPlan;
		// 已实际开路的单类型起点也能接后援；这只是探索进展，不把空桥直接奖励成冰或战果。
		const float progress=std::max(access ? 1.0f : 0.0f,candidate.features[1]-candidate.baselineFeatures[1]);
		const auto better=[&](const Branch& old) {
			const bool wins=candidate.features[2]>0,oldWins=old.plan.features[2]>0;
			if(wins!=oldWins) return wins;
			if(wins && candidate.construction.breachSeconds!=old.plan.construction.breachSeconds)
				return candidate.construction.breachSeconds<old.plan.construction.breachSeconds;
			return progress>old.progress+.001f || (std::abs(progress-old.progress)<=.001f && candidate.score>old.plan.score);
		};
		const auto found=std::find_if(mPlans.begin(),mPlans.end(),[&](const Branch& old) {
			return old.row==row && old.large==large && old.types==types;
		});
		if(found!=mPlans.end()) {
			if(better(*found)) { found->plan=candidate; found->progress=progress; }
			return;
		}
		Branch branch{row,large,std::move(types),progress,candidate};
		if(mPlans.size()<kAssaultBranches) { mPlans.push_back(std::move(branch)); return; }
		// 每个构成深度保留一个有实际进展的起点；其余轮转，避免廉价二类案垄断三/四类搜索。
		std::array<size_t,kProtectedCompositionDepths> protectedAt;
		protectedAt.fill(mPlans.size());
		for(size_t i=0;i<mPlans.size();++i) {
			const auto& plan=mPlans[i]; if(plan.progress<=0) continue;
			const auto depth=std::min(std::max<size_t>(2,plan.types.size())-2,static_cast<size_t>(kProtectedCompositionDepths-1));
			auto& saved=protectedAt[depth];
			if(saved==mPlans.size() || plan.progress>mPlans[saved].progress) saved=i;
		}
		size_t victim=mInsertion++%mPlans.size();
		while(std::find(protectedAt.begin(),protectedAt.end(),victim)!=protectedAt.end()) victim=mInsertion++%mPlans.size();
		mPlans[victim]=std::move(branch);
	}
	bool Empty() const { return mPlans.empty(); }
	/** 轮转构成并间歇深化已伤到防线的较深分支，不绑定任何僵尸类型或固定组合。 */
	const Result* Next() {
		if(mPlans.empty()) return nullptr;
		if(mCursor++%3==2) {
			const Branch* best=nullptr;
			for(const auto& branch:mPlans) if(branch.progress>0 && (!best || branch.types.size()>best->types.size()
				|| (branch.types.size()==best->types.size() && branch.progress>best->progress))) best=&branch;
			if(best) return &best->plan;
		}
		return &mPlans[(mCursor-1)%mPlans.size()].plan;
	}
	/** 先导出本轮已有攻城进展的构成；跨局面仅携带动作，不携带削血分或突破结论。 */
	std::vector<std::vector<Action>> Proposals() const {
		std::vector<std::vector<Action>> plans;
		std::vector<const Branch*> ordered;
		for(const auto& branch:mPlans) ordered.push_back(&branch);
		std::stable_sort(ordered.begin(),ordered.end(),[](const Branch* a,const Branch* b) {
			const bool wins=a->plan.features[2]>0,otherWins=b->plan.features[2]>0;
			if(wins!=otherWins) return wins;
			if(wins && a->plan.construction.breachSeconds!=b->plan.construction.breachSeconds)
				return a->plan.construction.breachSeconds<b->plan.construction.breachSeconds;
			return a->progress>b->progress;
		});
		for(const auto* branch:ordered) plans.push_back(branch->plan.actions);
		return plans;
	}
private:
	struct Branch { int row; bool large; std::set<std::pair<int,int>> types; float progress; Result plan; };
	std::vector<Branch> mPlans;
	int mBaselineAccess=0;
	size_t mCursor=0,mInsertion=0;
};

/** 用当前合法选项重映射旧提案；整类缺席时舍弃整案，不借用旧价格或删掉必要搭档。 */
std::vector<Action> RestoreProposal(const Snapshot& s,const Proposal& proposal,bool preserveTiming=false) {
    std::vector<Action> plan;
    for(const auto& member:proposal) {
        const auto found=std::find_if(s.options.begin(),s.options.end(),[&](const Option& option) {
            return option.type==member.type && option.row==member.row && option.device==member.device && option.setting==member.setting;
        });
        if(found==s.options.end()) return {};
        if(preserveTiming && ((std::bit_cast<std::uint32_t>(member.delay)&0x7f800000u)==0x7f800000u
            || member.delay<0 || member.delay>DelayLimit(s))) return {};
        plan.push_back({static_cast<int>(found-s.options.begin()),preserveTiming ? member.delay : std::clamp(member.delay,0.0f,DelayLimit(s))});
    }
    return plan;
}
/** 跨轮只保存无状态的动作提案；数值预期、实体ID和未兑现收入留在原任务内。 */
Proposal SaveProposal(const Snapshot& s,const std::vector<Action>& plan) {
    Proposal proposal;
    for(const auto& action:plan) {
        const auto& option=s.options[action.option];
        proposal.push_back({option.type,option.row,option.device,option.setting,action.delay});
    }
    return proposal;
}

/** 跨技能世界交错保留构成副本，去重后仍受原提案上限；旧收益和实体目标不进入下一轮。 */
std::vector<Proposal> MergeProposals(const std::vector<Proposal>& first,const std::vector<Proposal>& second) {
	std::vector<Proposal> merged;
	const auto append=[&](const Proposal& proposal) {
		if(proposal.empty() || merged.size()>=kAssaultBranches) return;
		const auto same=[&](const Proposal& old) {
			return old.size()==proposal.size() && std::equal(old.begin(),old.end(),proposal.begin(),[](const auto& a,const auto& b) {
				return a.type==b.type && a.row==b.row && a.device==b.device && a.setting==b.setting && a.delay==b.delay;
			});
		};
		if(std::none_of(merged.begin(),merged.end(),same)) merged.push_back(proposal);
	};
	for(size_t i=0;merged.size()<kAssaultBranches && (i<first.size() || i<second.size());++i) {
		if(i<first.size()) append(first[i]);
		if(i<second.size()) append(second[i]);
	}
	return merged;
}

/** 删除等待、整类或单个成员作同技能反事实；同步到场仍保留原提交次序，不强制接受。 */
std::vector<std::vector<Action>> MemberAblations(const Snapshot& s,const std::vector<Action>& plan) {
	std::vector<std::vector<Action>> probes;
	if(std::any_of(plan.begin(),plan.end(),[](const Action& action){return action.delay>0;})) {
		auto simultaneous=plan;
		for(auto& action:simultaneous) action.delay=0;
		probes.push_back(std::move(simultaneous));
	}
	std::set<std::pair<int,int>> covered;
	for(const auto& member:plan) {
		const auto& option=s.options[member.option]; const auto key=std::make_pair(option.type,option.cost);
		if(!covered.insert(key).second) continue;
		auto reduced=plan;
		reduced.erase(std::remove_if(reduced.begin(),reduced.end(),[&](const Action& action) {
			const auto& choice=s.options[action.option]; return std::make_pair(choice.type,choice.cost)==key;
		}),reduced.end());
		probes.push_back(std::move(reduced));
		if(probes.size()>=kWinnerPruningTrials) break;
	}
	for(size_t i=plan.size();i>0 && probes.size()<kWinnerPruningTrials;--i) {
		auto reduced=plan; reduced.erase(reduced.begin()+i-1); probes.push_back(std::move(reduced));
	}
	return probes;
}

/** 自由变异既能改单兵，也能调整一整类的数量、路线和出场时机，不检查兵种能力。 */
void MutatePlan(const Snapshot& s, std::vector<Action>& plan, std::mt19937& rng) {
	const int mutation=rng()%8;
	if (plan.empty() || mutation==0) {
		plan.push_back({static_cast<int>(rng()%s.options.size()),(rng()%(static_cast<int>(DelayLimit(s)*2)+1))*.5f});
		return;
	}
	const size_t at=rng()%plan.size(); const auto original=plan[at];
	const auto& choice=s.options[original.option];
	if (choice.cost<=0) { plan.erase(plan.begin()+at); return; }
	if (mutation==1) plan.erase(plan.begin()+at);
	else if (mutation==2) plan[at].option=rng()%s.options.size();
	else if (mutation==3) plan[at].delay+=static_cast<int>(rng()%13)-6;
	else if (mutation==4) plan.push_back({original.option,original.delay+2});
	else {
		const auto sameType=[&](const Action& action) {
			const auto& other=s.options[action.option];
			return other.type==choice.type && other.cost==choice.cost;
		};
		if (mutation==5) {
			const int shift=static_cast<int>(rng()%25)-12;
			for (auto& action:plan) if (sameType(action)) action.delay+=shift;
		} else if (mutation==6) {
			std::vector<int> routes;
			for (int i=0;i<static_cast<int>(s.options.size());++i)
				if (s.options[i].type==choice.type && s.options[i].cost==choice.cost) routes.push_back(i);
			const int option=routes[rng()%routes.size()]; const bool group=rng()%2==0;
			for (size_t i=0;i<plan.size();++i) if ((group && sameType(plan[i])) || i==at) plan[i].option=option;
		} else {
			// 原伙伴全部保留；按剩余容量/钱包改这一类的规模，避免等量复制成为唯一成批入口。
			plan.erase(std::remove_if(plan.begin(),plan.end(),sameType),plan.end());
			int remaining=PurchaseBudget(s), troops=0;
			for (const auto& action:plan) { remaining-=s.options[action.option].cost; troops+=s.options[action.option].device<0; }
			const auto fees=DeploymentDeviceCost(s,plan)+(choice.device>=0 ? static_cast<std::int64_t>(choice.cost) : 0);
			const int limit=std::min({ActionLimit(s,fees)-static_cast<int>(plan.size()),
				choice.device>=0 ? 1 : DeploymentCapacity(s,fees)-troops,std::max(0,remaining)/choice.cost});
			if (limit>0) for (int n=1+rng()%limit;n>0;--n) plan.push_back(original);
		}
	}
}

/** 合并另一候选中的整类行动；费用超限时均匀缩小整案，保留不同类型参与的机会。 */
void BlendPlan(const Snapshot& s, std::vector<Action>& plan, const std::vector<Action>& donor, std::mt19937& rng) {
	if (donor.empty()) return;
	const auto& selected=s.options[donor[rng()%donor.size()].option];
	const auto matches=[&](const Action& action) {
		const auto& option=s.options[action.option];
		return option.type==selected.type && option.cost==selected.cost;
	};
	plan.erase(std::remove_if(plan.begin(),plan.end(),matches),plan.end());
	for (const auto& action:donor) if (matches(action)) plan.push_back(action);
	int cost=0;
	for (const auto& action:plan) cost+=s.options[action.option].cost;
	int count=std::min(static_cast<int>(plan.size()),ActionLimit(s));
	if (cost>PurchaseBudget(s)) count=std::min(count,static_cast<int>(plan.size())*PurchaseBudget(s)/cost);
	if (count<static_cast<int>(plan.size())) {
		std::vector<Action> fitted;
		for (int i=0;i<count;++i) fitted.push_back(plan[i*plan.size()/count]);
		plan=std::move(fitted);
	}
}

/** 修复变异后的越界和超预算动作，稳定排序保留同一时刻的提交次序。 */
void Repair(const Snapshot& s, std::vector<Action>& actions) {
	int spent = 0, troops=0;
	std::array<bool,3> deviceUsed{};
	actions.erase(std::remove_if(actions.begin(), actions.end(), [&](Action& a) {
		if (a.option < 0 || a.option >= static_cast<int>(s.options.size())) return true;
		const auto& option=s.options[a.option];
		const int cost = option.cost;
		if (cost <= 0 || spent + cost > PurchaseBudget(s)) return true;
		if (option.device>=0) {
			if(option.device>=static_cast<int>(deviceUsed.size())) return true;
			if(deviceUsed[option.device]) return true;
			deviceUsed[option.device]=true; a.delay=0;
		}
		// 删除的超预算行动没有占用名额；后面的便宜队员仍可保留。
		if(option.device<0 && s.deploymentCapital<0) { if (troops>=s.capacity) return true; ++troops; }
		spent += cost;
		a.delay = std::clamp(a.delay, 0.0f, DelayLimit(s));
		return false;
	}), actions.end());
	// 设备在购物车尾部也会先消耗资本；先确定本案真正保留的设置，再整体限制新兵，
	// 不能逐条付款后才发现先前兵力超过新容量。旧纯夹具保留原固定cap修复次序。
	const auto deviceFees=DeploymentDeviceCost(s,actions);
	if(s.deploymentCapital>=0) {
		const int capacity=DeploymentCapacity(s,deviceFees);
		troops=0;
		actions.erase(std::remove_if(actions.begin(),actions.end(),[&](const Action& action) {
			return s.options[action.option].device<0 && troops++>=capacity;
		}),actions.end());
	}
	const int limit = ActionLimit(s,deviceFees);
	if (actions.size() > static_cast<size_t>(limit)) actions.resize(limit);
	std::stable_sort(actions.begin(), actions.end(), [](auto a, auto b) { return a.delay < b.delay; });
}
float Score(const Weights& f, const Weights& w) {
	float value = 0;
	for (int i = 0; i < FeatureCount; ++i) value += f[i] * w[i];
	return value;
}

/** 先比较是否突破和首次进屋时间，避免拖延胜利赚取更多中间收益。 */
int CompareVictory(const Result& a, const Result& b) {
	const bool aWins = a.features[2] > 0, bWins = b.features[2] > 0;
	if (aWins != bWins) return aWins ? 1 : -1;
	if (aWins && a.construction.breachSeconds != b.construction.breachSeconds)
		return a.construction.breachSeconds < b.construction.breachSeconds ? 1 : -1;
	return 0;
}

/** 胜负和进屋时刻相同才比较净收益；仍允许保护前锋或加快突破的增援。 */
bool BetterOutcome(const Result& a, const Result& b) {
	if (const int victory = CompareVictory(a,b)) return victory > 0;
	return a.score > b.score + .001f;
}

/** 替换最终整案的预测结果，保留本轮确实完成的搜索诊断与条件权重。 */
void AdoptOutcome(Result& incumbent,Result candidate) {
	incumbent.actions=std::move(candidate.actions);
    incumbent.followupPolicy=std::move(candidate.followupPolicy);
    incumbent.followupWorldsEvaluated=candidate.followupWorldsEvaluated;
    incumbent.followupWorldsPurchased=candidate.followupWorldsPurchased;
    incumbent.observedDelayedWorlds=candidate.observedDelayedWorlds;
    incumbent.phasedPlanning=candidate.phasedPlanning;
    incumbent.observedDecision=candidate.observedDecision;
	incumbent.experiencedSelected=candidate.experiencedSelected;
	incumbent.experiencedAshSelected=candidate.experiencedAshSelected;
	incumbent.assaultContinuationSelected=candidate.assaultContinuationSelected;
	incumbent.features=candidate.features; incumbent.baselineFeatures=candidate.baselineFeatures;
	incumbent.precisionTargetID=candidate.precisionTargetID;
	incumbent.precisionAdditionalTargetIDs=std::move(candidate.precisionAdditionalTargetIDs);
	incumbent.score=candidate.score; incumbent.blastLoss=candidate.blastLoss;
	incumbent.preferenceScore=candidate.preferenceScore; incumbent.rawPreferenceScore=candidate.rawPreferenceScore;
	incumbent.opponentAssets=candidate.opponentAssets; incumbent.baselineOpponentAssets=candidate.baselineOpponentAssets;
	incumbent.opponentScore=candidate.opponentScore;
	incumbent.construction=std::move(candidate.construction);
	incumbent.rawProduction=candidate.rawProduction; incumbent.productionInputs=candidate.productionInputs;
	incumbent.counterHoldSeconds=candidate.counterHoldSeconds;
	if(!incumbent.actions.empty()) incumbent.regrouping=false;
}

/** 完整案评分入口；成员删减沿用同一套玩家应对与校准。 */
Result EvaluatePlan(const Snapshot& s,const Weights& weights,std::vector<Action> plan,const Weights& baseline,float baselineOpponentAssets,const Result* initial=nullptr, const FollowupPolicy* followup=nullptr, float responseEnvelope=0);

/** 精准清除须改善同一编队或真实削弱关键输出且抵得过技能费；选靶和删成员共用门禁。 */
bool PrecisionWorthwhile(const Snapshot& s,const Result& candidate,const Result& sameArmy) {
	const auto ids=PrecisionIDs(candidate.precisionTargetID,candidate.precisionAdditionalTargetIDs,s.precisionTargetLimit);
	const float cost=static_cast<float>(ids.size()*ColdStorageSkillRules::StrikeIceCost);
	float reward=0;
	bool armed=!ids.empty();
	for(int id:ids) {
		const auto target=std::find_if(s.plants.begin(),s.plants.end(),[&](const Plant& p){return p.id==id;});
		if(target==s.plants.end()) {armed=false; continue;}
		reward+=target->reward;
		armed&=target->dps>0 || std::any_of(s.rowStrikes.begin(),s.rowStrikes.end(),
			[&](const RowStrike& strike){return strike.plantID==id && strike.damage>0;})
			|| std::any_of(s.counters.begin(),s.counters.end(),[&](const Counter& counter) {
				return counter.plantID==id && !counter.consumesPlant && !counter.blast.committed && counter.blast.damage>0;
			});
	}
	// 能真实清掉输出/控场源并形成有利资产交换时，也允许主动削弱阵地；
    // 不能要求本次单击同时突破，否则两株强输出互相兜底会永久封住第一步。
    const bool preparation=armed && candidate.construction.precisionHits>0
        && candidate.baselineOpponentAssets-candidate.opponentAssets+candidate.features[0]-sameArmy.features[0]>=cost;
    const bool tactical=preparation || candidate.features[2]>sameArmy.features[2] || candidate.features[3]>sameArmy.features[3]+.001f
		|| candidate.features[4]>sameArmy.features[4]+.001f || candidate.features[0]>sameArmy.features[0]+reward+.001f
		|| candidate.features[1]>sameArmy.features[1]+reward+.001f;
	const float gain=candidate.features[0]-sameArmy.features[0]+candidate.features[3]-sameArmy.features[3]
		+candidate.features[4]-sameArmy.features[4]+sameArmy.opponentAssets-candidate.opponentAssets;
	return tactical && BetterOutcome(candidate,sameArmy)
		&& (candidate.features[2]>sameArmy.features[2] || gain>=cost);
}

/** 同技能删成员必须仍优于原案并满足真实钱包/资本门禁，不强制留下任何支援。 */
void PruneWinner(const Snapshot& s,Result& best) {
	int trials=0;
	const auto anchor=best.actions.empty() ? best.investmentPruningActions : best.actions;
	const auto probes=MemberAblations(s,anchor);
	for(auto plan:probes) {
		if(SearchTimeExpired(s)) break;
		// 上一轮已经删掉的队员不能在下一反事实中悄悄加回；只接受当前整案的子序列。
		size_t at=0; bool subset=true;
		const auto& current=best.actions.empty() ? anchor : best.actions;
		for(const auto& member:plan) {
			while(at<current.size() && current[at].option!=member.option) ++at;
			if(at==current.size()) { subset=false; break; }
			++at;
		}
		if(!subset) continue;
		if(plan.size()>current.size() || (!s.allowWait && plan.empty())) continue;
		if(!s.allowWait && !HasPaidTroops(s,plan)) continue;
		if(s.precisionUnlockAfterPurchase && s.precisionTargetID>0 && !FuturePrecisionPlanPayable(s,plan)) continue;
		auto candidate=EvaluatePlan(s,best.effectiveWeights,std::move(plan),best.baselineFeatures,best.baselineOpponentAssets);
		++trials;
		if(s.precisionTargetID>0 && !s.netEconomy)
			candidate.score-=std::max(0.0f,1+best.effectiveWeights[5])*PrecisionCost(s);
		if(!BetterOutcome(candidate,best) || ShouldRegroup(candidate,s.budget,s.recoveryReserve)
			|| (s.netEconomy && ShouldConserveCapital(candidate,s.budget,s.recoveryReserve,
				s.allowWait ? s.capitalRiskAllowance : (std::numeric_limits<float>::max)()))) continue;
		if(s.precisionTargetID>0) {
			auto without=RawWorldCache::CopyView(s); without.precisionTargetID=0; without.precisionAdditionalTargetIDs.clear();
			const auto sameArmy=EvaluatePlan(without,best.effectiveWeights,candidate.actions,best.baselineFeatures,best.baselineOpponentAssets);
			if(!PrecisionWorthwhile(s,candidate,sameArmy)) continue;
		}
		AdoptOutcome(best,std::move(candidate));
	}
	best.pruningEvaluated+=trials; best.evaluated+=trials;
}

/** 记录完整候选的比较结果，同兵种同行多只只计一次；不改变选兵、评分或资金门禁。 */
void RecordCandidate(const Snapshot& s, const Result& candidate, int denial, std::vector<CandidateStats>& stats) {
	for (size_t i=0; i<candidate.actions.size(); ++i) {
		const auto& option = s.options[candidate.actions[i].option];
		bool duplicate = false;
		for (size_t j=0; j<i; ++j) {
			const auto& prior = s.options[candidate.actions[j].option];
			if (prior.type == option.type && prior.row == option.row) { duplicate = true; break; }
		}
		if (duplicate) continue;
		auto entry = std::find_if(stats.begin(),stats.end(),[&](const auto& item) {
			return item.type == option.type && item.row == option.row;
		});
		if (entry == stats.end()) { stats.push_back({}); entry = stats.end()-1; entry->type = option.type; entry->row = option.row; }
		const bool breach = candidate.features[2] > 0;
		if (entry->evaluated == 0 || (breach && !entry->bestBreach)
			|| (breach == entry->bestBreach && candidate.score > entry->bestScore)) {
			entry->bestBreach = breach; entry->bestScore = candidate.score; entry->bestDenial = denial;
			entry->bestCost = candidate.features[5]-candidate.baselineFeatures[5];
			entry->bestProduction = candidate.features[4]-candidate.baselineFeatures[4];
			entry->bestCash = entry->bestProduction+candidate.features[0]-candidate.baselineFeatures[0];
			entry->bestBlastLoss = candidate.features[6]-candidate.baselineFeatures[6];
		}
		++entry->evaluated;
		if (candidate.actions.size() == 1) ++entry->standalone;
		if (denial == 0) {
			if (entry->allowed == 0 || (breach && !entry->bestAllowedBreach)
				|| (breach == entry->bestAllowedBreach && candidate.score > entry->bestAllowedScore)) {
				entry->bestAllowedScore = candidate.score; entry->bestAllowedBreach = breach;
			}
			++entry->allowed;
		}
		else if (denial == 1) ++entry->regroupRejected;
		else ++entry->capitalRejected;
	}
}

/** 汇总工人存活条件，只读当前快照和候选，不读取未来玩家动作。 */
ProductionFeatures ProductionInputs(const Snapshot& s, const std::vector<Action>& plan, float income) {
	ProductionFeatures f{}; f[0] = std::log1p(std::max(0.0f,income)); f[9] = s.budget / 100.0f;
	float count = 0;
	auto worker = [&](const Unit& unit, float entry) {
		++count; f[3] += unit.body.health / IceProduction::WorkerHealth;
		const auto& context = s.context[unit.body.row];
		f[6] += context[5]; f[7] += context[4]; f[8] += context[3];
		for (const auto& guard : s.current)
			if (!guard.body.economic && guard.body.health > 0 && guard.body.row == unit.body.row
				&& guard.body.spawnAt <= entry && (guard.body.x < unit.body.x
					|| (guard.body.spawnAt < entry && guard.body.speed > 0 && guard.body.x <= unit.body.x)))
				f[4] += guard.body.health / 3000.0f;
	};
	for (const auto& unit : s.current) if (unit.body.economic && unit.body.health > 0) { ++f[1]; worker(unit,unit.body.spawnAt); }
	for (const auto& action : plan) {
		const auto& option = s.options[action.option];
		if (option.unit.body.economic) {
			++f[2]; worker(option.unit,action.delay);
			for (const auto& escort : plan) {
				const auto& guard = s.options[escort.option];
				if (!guard.unit.body.economic && guard.row == option.row && escort.delay < action.delay)
					f[5] += guard.cost / 48.0f;
			}
		}
	}
	if (count > 0) for (int i = 3; i <= 8; ++i) f[i] /= count;
	return f;
}

/** 保留未经校准的训练输入，再用已验证的小模型折减经济预期。 */
void Calibrate(const Snapshot& s, Result& result) {
	result.rawProduction = result.features[4];
	auto purchased=result.actions;
    purchased.insert(purchased.end(),result.construction.followup.realizedActions.begin(),result.construction.followup.realizedActions.end());
	result.productionInputs = ProductionInputs(s,purchased,result.rawProduction);
	// 校准输入仍是原60秒窗口；长编队的费用/伤亡观察到120秒，生产回报也须计到同一终点。
    // 晚入场不是无收益，且这里只结算存活/有头/未硬控时实际走完的生产周期。
    result.features[4]=(result.rawProduction+result.construction.productionAfterWindow)
        *(s.productionCalibration ? s.productionCalibration->Predict(result.productionInputs) : 1);
}

/** 状态模型继续读取60秒经济输入；完整方案的长时域回报不会改变已训练的状态特征单位。 */
StateFeatures DescribeResultState(const Snapshot& s,const Result& result) {
    auto features=result.features;
    features[4]=result.rawProduction*(s.productionCalibration ? s.productionCalibration->Predict(result.productionInputs) : 1);
    return DescribeState(s,features);
}

/** 对自由搜索与逐行对照使用同一收益、校准和兵种经验，避免比较时遗漏评分项。 */
Result EvaluatePlan(const Snapshot& s, const Weights& weights, std::vector<Action> plan, const Weights& baseline, float baselineOpponentAssets,const Result* initial,const FollowupPolicy* followup,float responseEnvelope) {
	// 解锁后技能没有已付款承诺；没有真实推进兵或整案现金不足时只能评同兵不施法。
	if(s.precisionUnlockAfterPurchase && s.precisionTargetID>0 && !FuturePrecisionPlanPayable(s,plan)) {
		auto without=RawWorldCache::CopyView(s); without.precisionTargetID=0; without.precisionAdditionalTargetIDs.clear();
		return EvaluatePlan(without,weights,std::move(plan),baseline,baselineOpponentAssets,nullptr,followup,responseEnvelope);
	}
    int worlds=0, purchasedWorlds=0, observedDelayedWorlds=0;
    // 每个既有响应姿态都接收同一未付款条件；只统计完整世界，不能拼接部分战果。
    const auto evaluate=[&](const Snapshot& world,const std::vector<Action>& actions,ConstructionStats* stats,
        float counterHold=0,float storedHold=0,float strikeHold=0,bool reserve=false,bool clearFog=false,
        int lamp=-1,bool manual=false,bool shovel=false,bool paid=false,bool interference=false) {
        auto features=EvaluateRawWorld(world,actions,stats,counterHold,storedHold,strikeHold,reserve,clearFog,lamp,
            manual,shovel,paid,interference,followup);
        if(followup) { ++worlds; purchasedWorlds+=stats->followup.status==FollowupForecast::Status::Purchased; }
        observedDelayedWorlds+=stats->observedDelaySteps>0;
        return features;
    };
	Result candidate;
	candidate.actions = std::move(plan);
	CopyPrecisionIntent(candidate,s);
	// 初始姿态可借用同快照/同动作的已校准结果；其余反制世界仍各自完整推演。
	// 借用只发生在当前同步调用内，不跨技能意图、局势或工作线程任务缓存。
	if(initial && !followup) {
		candidate.features=initial->features; candidate.construction=initial->construction;
		candidate.rawProduction=initial->rawProduction; candidate.productionInputs=initial->productionInputs;
        observedDelayedWorlds+=candidate.construction.observedDelaySteps>0;
	} else {
		candidate.features = evaluate(s, candidate.actions, &candidate.construction);
		Calibrate(s, candidate);
	}
	candidate.baselineFeatures = baseline;
	candidate.score = Score(candidate.features, weights);
	candidate.opponentAssets = candidate.construction.opponentAssets;
	candidate.baselineOpponentAssets = baselineOpponentAssets;
	candidate.opponentScore = s.opponentWeight*(baselineOpponentAssets-candidate.opponentAssets);
	candidate.score += candidate.opponentScore;
	// 不把玩家一定会尽早交牌/释放蓄满技能当成进攻收益。比较完整且各自合法的一次推演，
	// 不能逐项拼接两个世界的最坏损失，也不能让玩家凭空多出冷却或资源。
	float selectedCounterHold=0, selectedStoredHold=0, selectedStrikeHold=0;
	bool selectedManualHold=false;
	float storedHold = std::max(kStoredCounterSeconds,responseEnvelope);
	// 长队列还能分批出生到一分钟之后。等待对照随已知的己方出生计划延长，
	// 避免新兵仅靠晚于固定等待窗口就被误判为已经骗掉了预存毁灭。
	for (const auto& unit : s.current) storedHold = std::max(storedHold,unit.body.spawnAt);
	for (const auto& action : candidate.actions) storedHold = std::max(storedHold,action.delay);
	const bool hasRowStrike = !s.rowStrikes.empty() || std::any_of(s.construction.begin(),s.construction.end(),
		[](const auto& card) { return card.strike.damage > 0; });
	const bool hasFutureCob=std::any_of(s.construction.begin(),s.construction.end(),[](const Construction& card) {return card.cob.damage>0;});
	for (float hold : {kPatientCounterSeconds,storedHold}) {
		if (!hasRowStrike && !hasFutureCob && std::none_of(s.counters.begin(),s.counters.end(),[&](const auto& counter) {
			return !counter.blast.committed && (hold == kPatientCounterSeconds || counter.stored || !counter.consumesPlant);
		})) continue;
		Result patient;
		patient.actions = candidate.actions;
		CopyPrecisionIntent(patient,s);
		patient.features = evaluate(s,patient.actions,&patient.construction,kPatientCounterSeconds,
			hold == kPatientCounterSeconds ? 0 : storedHold,hold);
		Calibrate(s,patient);
		patient.baselineFeatures = baseline;
		patient.opponentAssets = patient.construction.opponentAssets;
		patient.baselineOpponentAssets = baselineOpponentAssets;
		patient.opponentScore = s.opponentWeight*(baselineOpponentAssets-patient.opponentAssets);
		patient.score = Score(patient.features,weights)+patient.opponentScore;
		patient.counterHoldSeconds = hold;
		if (BetterOutcome(candidate,patient)) {
            selectedCounterHold=kPatientCounterSeconds;
            selectedStoredHold=hold == kPatientCounterSeconds ? 0 : storedHold;
            selectedStrikeHold=hold;
            candidate = std::move(patient);
        }
	}
    // 玩家可以保留炸弹落点与资金，而不是机械补满所有空格。每个姿态都是独立合法的
    // 完整世界：同一钱包/冷却/视野和既有阵地，不能拼接不同世界的伤害或虚构反制资源。
    const bool canReserve = !s.construction.empty() && (hasFutureCob || std::any_of(s.counters.begin(),s.counters.end(),
        [](const Counter& c) { return !c.blast.committed && c.plantID==0 && c.cellRow>=0; }));
    const bool hasReusableCounter=hasFutureCob || std::any_of(s.counters.begin(),s.counters.end(),
        [](const Counter& c){return !c.blast.committed && !c.consumesPlant;});
    // 留周转空位不等于必须连免费炮一起长期留火；补齐这份合法联合响应，
    // 否则等待能持续经营，廉价单兵却靠诱使模型机械补满空格虚构“对方损失”。
    // 原有长期留火世界保留，全部分别完整推演，不逐项拼接经营与战斗结果。
    if(canReserve) for(int response=hasReusableCounter ? 0 : 1;response<3;++response) {
        const float hold=response==2 ? kPatientCounterSeconds : 0;
        const float reserveHold=response==0 ? 0 : storedHold;
        Result counterFirst;
        counterFirst.actions=candidate.actions; CopyPrecisionIntent(counterFirst,s);
        counterFirst.features=evaluate(s,counterFirst.actions,&counterFirst.construction,hold,reserveHold,hold,true);
        Calibrate(s,counterFirst);
        counterFirst.baselineFeatures=baseline; counterFirst.baselineOpponentAssets=baselineOpponentAssets;
        counterFirst.opponentAssets=counterFirst.construction.opponentAssets;
        counterFirst.opponentScore=s.opponentWeight*(baselineOpponentAssets-counterFirst.opponentAssets);
        counterFirst.score=Score(counterFirst.features,weights)+counterFirst.opponentScore;
        counterFirst.counterHoldSeconds=hold;
        if(BetterOutcome(candidate,counterFirst)) {
            selectedCounterHold=hold; selectedStoredHold=reserveHold; selectedStrikeHold=hold;
            candidate=std::move(counterFirst);
        }
    }

    // 在已选完整反制姿态上比较有限的照明策略，再比较付费关雾；不展开所有组合。
    // 每个世界独立结算燃料、视野、移速和共享钱包，不拼接多个挡位的最佳效果。
    const bool mayHaveFog=s.station.controls[WeatherStationRules::FOG].value>0
        || s.station.controls[WeatherStationRules::FOG].pending>0
        || std::any_of(candidate.actions.begin(),candidate.actions.end(),[&](const Action& a) {
            return s.options[a.option].device==WeatherStationRules::FOG && s.options[a.option].setting>0;
        });
    if(s.weatherStation && mayHaveFog) {
        const auto compareEnvironment=[&](bool clearFog,int lampGear,bool reserveSpace) {
            Result response;
            response.actions=candidate.actions; CopyPrecisionIntent(response,s);
            response.features=evaluate(s,response.actions,&response.construction,selectedCounterHold,
                selectedStoredHold,selectedStrikeHold,reserveSpace,clearFog,lampGear);
            Calibrate(s,response);
            response.baselineFeatures=baseline; response.baselineOpponentAssets=baselineOpponentAssets;
            response.opponentAssets=response.construction.opponentAssets;
            response.opponentScore=s.opponentWeight*(baselineOpponentAssets-response.opponentAssets);
            response.score=Score(response.features,weights)+response.opponentScore;
            response.counterHoldSeconds=candidate.counterHoldSeconds;
            if(BetterOutcome(candidate,response)) candidate=std::move(response);
        };
        const bool hasLamp=std::any_of(s.plants.begin(),s.plants.end(),[](const Plant& p) {
            return p.plantern && p.health>0;
        }) || std::any_of(s.construction.begin(),s.construction.end(),[](const Construction& c) {
            return c.plant.plantern;
        });
        if(hasLamp) for(int gear=0;gear<=(s.fuelAwarePlantern ? FuelAwarePlanternResponse : 3);++gear)
            compareEnvironment(false,gear,candidate.construction.counterSpaceReserved);
        // 暗场中留空位与补满格子的收益可能暂时相同，前段择优未必选中留位姿态。
        // 额外保留一次“只补灯再反制”的协同，不能因前置单项不占优而漏掉整套合法应对。
        if(canReserve && !candidate.construction.counterSpaceReserved && (hasLamp
            || std::any_of(s.construction.begin(),s.construction.end(),[](const Construction& c) { return c.plant.plantern; }))) {
            // 已有灯也能与留灰烬落点协同；逐项贪心会漏掉“高档照见边路，再用整行灰烬”。
            compareEnvironment(false,3,true);
            if(s.fuelAwarePlantern) compareEnvironment(false,FuelAwarePlanternResponse,true);
        }
        compareEnvironment(true,candidate.construction.planternResponseGear,candidate.construction.counterSpaceReserved);
    }
    // 手动菠萝有目标不等于玩家一定会花冰。再比较保留技能的完整世界，
    // 避免无战果的单兵靠虚构两次加速费用反复赚取消耗分；已经开启和自动模式仍照常结算。
    if(candidate.construction.auraActivations>0 && std::any_of(s.attackAuras.begin(),s.attackAuras.end(),
        [](const AttackAura& aura) { return !aura.automatic; })) {
        Result patient;
        patient.actions=candidate.actions; CopyPrecisionIntent(patient,s);
        patient.features=evaluate(s,patient.actions,&patient.construction,selectedCounterHold,
            selectedStoredHold,selectedStrikeHold,candidate.construction.counterSpaceReserved,
            candidate.construction.stationFogCounters>0,candidate.construction.planternResponseGear,true);
        Calibrate(s,patient);
        patient.baselineFeatures=baseline; patient.baselineOpponentAssets=baselineOpponentAssets;
        patient.opponentAssets=patient.construction.opponentAssets;
        patient.opponentScore=s.opponentWeight*(baselineOpponentAssets-patient.opponentAssets);
        patient.score=Score(patient.features,weights)+patient.opponentScore;
        patient.counterHoldSeconds=candidate.counterHoldSeconds;
        if(BetterOutcome(candidate,patient)) { candidate=std::move(patient); selectedManualHold=true; }
    }

    // 腾位是完整的另一种玩家应对：同时失去被铲植物的输出、经济与资产，灰烬仍共用钱和冷却。
    // 只在整个结果对玩家更有利时采用，不能把铲除世界的清场拼接到保留阵地的世界上。
    if((!s.current.empty() || !candidate.actions.empty() || (followup && !followup->actions.empty()))
        && std::any_of(s.counters.begin(),s.counters.end(),[](const Counter& counter){return counter.shovelAllowed;})
        && std::any_of(s.plants.begin(),s.plants.end(),[](const Plant& plant){return plant.health>0 && plant.layer==1;})) {
        Result response;
        response.actions=candidate.actions; CopyPrecisionIntent(response,s);
        response.features=evaluate(s,response.actions,&response.construction,selectedCounterHold,selectedStoredHold,
            selectedStrikeHold,candidate.construction.counterSpaceReserved,candidate.construction.stationFogCounters>0,
            candidate.construction.planternResponseGear,selectedManualHold,true);
        Calibrate(s,response);
        response.baselineFeatures=baseline; response.baselineOpponentAssets=baselineOpponentAssets;
        response.opponentAssets=response.construction.opponentAssets;
        response.opponentScore=s.opponentWeight*(baselineOpponentAssets-response.opponentAssets);
        response.score=Score(response.features,weights)+response.opponentScore;
        response.counterHoldSeconds=candidate.counterHoldSeconds;
        if(BetterOutcome(candidate,response)) candidate=std::move(response);
    }
    // “值得反制”只是启发式，玩家也可依靠既有火力，不为无战果的诱饵交付费工具。
    // 另一完整世界保留同一阵地、钱包和已提交事务；强攻若确有威胁，仍由正常反制世界胜出。
    const bool paidResponse=candidate.construction.paidCounterCasts>0 || candidate.construction.auraActivations>0
        || candidate.construction.plantRepairs>0 || candidate.construction.interferences>0
        || candidate.construction.stationCounters>0 || candidate.construction.stationFogCounters>0;
    if(s.opponentWeight>0 && paidResponse) {
        Result response;
        response.actions=candidate.actions; CopyPrecisionIntent(response,s);
        response.features=evaluate(s,response.actions,&response.construction,selectedCounterHold,selectedStoredHold,
            selectedStrikeHold,candidate.construction.counterSpaceReserved,false,
            candidate.construction.planternResponseGear,true,false,true);
        Calibrate(s,response);
        response.baselineFeatures=baseline; response.baselineOpponentAssets=baselineOpponentAssets;
        response.opponentAssets=response.construction.opponentAssets;
        response.opponentScore=s.opponentWeight*(baselineOpponentAssets-response.opponentAssets);
        response.score=Score(response.features,weights)+response.opponentScore;
        response.counterHoldSeconds=candidate.counterHoldSeconds;
        if(BetterOutcome(candidate,response)) candidate=std::move(response);
    }

	// 先清锚再交灰烬是独立的完整应对，必须与原择时对照，而不是拼接两种世界的清场收益。
	const bool hasClock=s.interferenceAvailable && (!s.temporalAnchors.empty()
		|| std::any_of(s.current.begin(),s.current.end(),[](const Unit& u){return u.clock.present;})
		|| std::any_of(candidate.actions.begin(),candidate.actions.end(),[&](const Action& a){return s.options[a.option].unit.clock.present;}));
	if(hasClock && !s.counters.empty()) {
		Result response;
		response.actions=candidate.actions; CopyPrecisionIntent(response,s);
		response.features=evaluate(s,response.actions,&response.construction,selectedCounterHold,selectedStoredHold,
			selectedStrikeHold,candidate.construction.counterSpaceReserved,candidate.construction.stationFogCounters>0,
			candidate.construction.planternResponseGear,selectedManualHold,candidate.construction.counterShovels>0,false,true);
		Calibrate(s,response);
		response.baselineFeatures=baseline; response.baselineOpponentAssets=baselineOpponentAssets;
		response.opponentAssets=response.construction.opponentAssets;
		response.opponentScore=s.opponentWeight*(baselineOpponentAssets-response.opponentAssets);
		response.score=Score(response.features,weights)+response.opponentScore;
		response.counterHoldSeconds=candidate.counterHoldSeconds;
		if(BetterOutcome(candidate,response)) candidate=std::move(response);
	}
    // 立即盲打与固定8/32秒留火之间，还有“等屏内目标出现再开炮”的合法短窗口。
    // 该响应按整份快照能力启用，空案基线与有兵案共用菜单；不让新小丑凭固定炮时序赚取虚假存活。
    const bool mayHaveEliteJack=std::any_of(s.current.begin(),s.current.end(),[](const Unit& unit) {
        return unit.jack.present && unit.jack.elite;
    }) || std::any_of(s.options.begin(),s.options.end(),[](const Option& option) {
        return option.device<0 && option.unit.jack.present && option.unit.jack.elite;
    });
    if(s.visibleCounterResponse && !s.diagnosticVisibleCounterWitness && hasReusableCounter && mayHaveEliteJack) {
        auto visible=RawWorldCache::CopyView(s); visible.diagnosticVisibleCounterWitness=true;
        Result response;
        response.actions=candidate.actions; CopyPrecisionIntent(response,visible);
        // 只增加这一份完整世界：灰烬仍可付款，沿用已选空位/环境姿态，并从当前可见目标即时反应。
        response.features=evaluate(visible,response.actions,&response.construction,0,0,0,
            candidate.construction.counterSpaceReserved,candidate.construction.stationFogCounters>0,
            candidate.construction.planternResponseGear,selectedManualHold,candidate.construction.counterShovels>0,false);
        Calibrate(visible,response);
        response.baselineFeatures=baseline; response.baselineOpponentAssets=baselineOpponentAssets;
        response.opponentAssets=response.construction.opponentAssets;
        response.opponentScore=s.opponentWeight*(baselineOpponentAssets-response.opponentAssets);
        response.score=Score(response.features,weights)+response.opponentScore;
        response.counterHoldSeconds=0;
        if(BetterOutcome(candidate,response)) candidate=std::move(response);
    }
    auto valuedActions=candidate.actions;
    valuedActions.insert(valuedActions.end(),candidate.construction.followup.realizedActions.begin(),
        candidate.construction.followup.realizedActions.end());
	for (const auto& action : valuedActions) {
		const auto& option = s.options[action.option];
		auto context = s.context[option.row];
		context[5] *= option.firePreferenceScale; // 盾牌偏好只受它实际能削弱的火力激励，不能把穿盾火力也算成优势。
		context[0] = 1;
		// 先前已付款且会先到场的队友也是协作背景，不能因其还在队列中而漏掉护卫。
		for (const auto& paid : s.committed) {
			const auto& ally = s.current[paid.unit].body;
			if (ally.row == option.row && ally.spawnAt <= action.delay) {
				context[2] += 1.0f / 3;
				if (ally.economic) context[6] += 1;
			}
		}
		// 计划中的同行队友也算协作背景，允许发现尚未出生的支援组合。
		for (const auto& other : valuedActions) if (&action != &other) {
			const auto& ally = s.options[other.option];
			if (ally.row == option.row) { context[2] += 1.0f / 3; if (ally.unit.body.economic) context[6] += 1; }
		}
		float rawPreference=0;
		for (int feature = 0; feature < ContextCount; ++feature)
			rawPreference += option.preference[feature]*std::clamp(context[feature],0.0f,3.0f);
		// 技能与火力已经逐步推演，旧危险偏好只能作为有界先验；保留原配置供重训，不暗改兵种权重。
		const float limit=option.cost*std::abs(weights[5])*kPreferenceIceFraction;
		const float preference=s.netEconomy ? std::clamp(rawPreference,-limit,limit) : rawPreference;
		candidate.rawPreferenceScore+=rawPreference;
		candidate.score+=preference; candidate.preferenceScore+=preference;
	}
    candidate.followupWorldsEvaluated=worlds; candidate.followupWorldsPurchased=purchasedWorlds;
    candidate.observedDelayedWorlds=observedDelayedWorlds;
    if(followup) candidate.followupPolicy=*followup;
	candidate.blastLoss = candidate.features[6];
	return candidate;
}

/** 只更换计划中尚未购买单位的行；缺少同兵种同费用选项时整案无效，不偷偷删兵或换兵。 */
bool Concentrate(const Snapshot& s, std::vector<Action>& plan, int row) {
	for (auto& action : plan) {
		const auto& original = s.options[action.option];
		if (original.row == row) continue;
		const auto match = std::find_if(s.options.begin(), s.options.end(), [&](const auto& option) {
			return option.row == row && option.type == original.type && option.cost == original.cost;
		});
		if (match == s.options.end()) return false;
		action.option = static_cast<int>(match - s.options.begin());
	}
	return true;
}

/** 将搜索自身找到的整段协作复制到不同合法行；设备只付一次，整批受现金/名额约束，不拆散伙伴。 */
std::vector<Action> ExpandRoutes(const Snapshot& s,const std::vector<Action>& anchor,size_t shape) {
	if(s.rows<2) return {};
	std::vector<Action> plan;
	int cost=0, troops=0, routes=0;
	for(const auto& action:anchor) if(s.options[action.option].device>=0) {
		plan.push_back(action); cost+=s.options[action.option].cost;
	}
	// 分路时按真实钱包缩小每路规模，而非只复制付不起的大批案；至少保留每类一员，
	// 不从队尾截断，把后加入的协作伙伴全删掉。删去的重复成员是否必要仍由完整推演检验。
	std::vector<Action> source;
	std::vector<std::pair<std::pair<int,int>,int>> counts;
	int cohortCost=0;
	for(const auto& action:anchor) if(s.options[action.option].device<0) {
		source.push_back(action); const auto& option=s.options[action.option]; cohortCost+=option.cost;
		const auto type=std::make_pair(option.type,option.cost);
		const auto found=std::find_if(counts.begin(),counts.end(),[&](const auto& entry){return entry.first==type;});
		if(found==counts.end()) counts.push_back({type,1}); else ++found->second;
	}
	if(counts.empty()) return {};
	// 同一路数先覆盖每种已有类型的原时序/同步主力对照，再减少路数并错峰；
	// 不能每换一个类型就变一次路数，使某类型永远碰不到足够多路的共享冷却窗口。
	const size_t round=shape/(2*counts.size());
	const int copies=s.rows-static_cast<int>(round%(s.rows-1));
	const float spacing=static_cast<float>(round%3)*kReinforcementDelay/3;
	const int startRow=static_cast<int>(shape%s.rows);
	const bool simultaneous=shape%2==1;
	const int routeBudget=(PurchaseBudget(s)-cost)/copies;
	const auto deviceFees=DeploymentDeviceCost(s,plan);
	const int routeSlots=std::min(DeploymentCapacity(s,deviceFees)/copies,(ActionLimit(s,deviceFees)-static_cast<int>(plan.size()))/copies);
	if(simultaneous && !counts.empty()) {
		// 同步对照还轮换各已有类型的主力规模；不能照搬探索时“支援很多、主体一员”的比例。
		// 每类先留一员，再把本路可支付余量给轮到的类型，身份和能力标签不参与选择。
		std::vector<Action> balanced; int balancedCost=0;
		for(const auto& entry:counts) {
			const auto member=std::find_if(source.begin(),source.end(),[&](const Action& action) {
				const auto& option=s.options[action.option]; return std::make_pair(option.type,option.cost)==entry.first;
			});
			balanced.push_back(*member); balancedCost+=entry.first.second;
		}
		if(balancedCost>routeBudget || static_cast<int>(balanced.size())>routeSlots) return {};
		for(auto& entry:counts) entry.second=1;
		const size_t primaryIndex=(shape/2)%balanced.size();
		const auto primary=balanced[primaryIndex]; const int primaryCost=s.options[primary.option].cost;
		while(primaryCost>0 && balancedCost+primaryCost<=routeBudget && static_cast<int>(balanced.size())<routeSlots) {
			balanced.push_back(primary); balancedCost+=primaryCost; ++counts[primaryIndex].second;
		}
		source=std::move(balanced); cohortCost=balancedCost;
	}
	while(cohortCost>routeBudget || static_cast<int>(source.size())>routeSlots) {
		const auto largest=std::max_element(counts.begin(),counts.end(),[](const auto& a,const auto& b) {
			return a.second!=b.second ? a.second<b.second : a.first.second<b.first.second;
		});
		if(largest==counts.end() || largest->second<=1) return {};
		const auto member=std::find_if(source.rbegin(),source.rend(),[&](const Action& action) {
			const auto& option=s.options[action.option]; return std::make_pair(option.type,option.cost)==largest->first;
		});
		source.erase(std::next(member).base()); cohortCost-=largest->first.second; --largest->second;
	}
	const float first=source.empty() ? 0 : std::min_element(source.begin(),source.end(),
		[](const Action& a,const Action& b){return a.delay<b.delay;})->delay;
	for(int offset=0;offset<s.rows && routes<copies;++offset) {
		const int row=(startRow+offset)%s.rows;
		std::vector<Action> cohort; int addedCost=0;
		for(const auto& action:source) {
			const auto& original=s.options[action.option];
			const auto found=std::find_if(s.options.begin(),s.options.end(),[&](const auto& option) {
				return option.device<0 && option.type==original.type && option.cost==original.cost && option.row==row;
			});
			if(found==s.options.end()) { cohort.clear(); break; }
			cohort.push_back({static_cast<int>(found-s.options.begin()),std::min(DelayLimit(s),(simultaneous ? first : action.delay)+routes*spacing)});
			addedCost+=found->cost;
		}
		if(cohort.empty()) continue;
		if(cost+addedCost>PurchaseBudget(s) || plan.size()+cohort.size()>static_cast<size_t>(ActionLimit(s,deviceFees))
			|| troops+static_cast<int>(cohort.size())>DeploymentCapacity(s,deviceFees)) break;
		plan.insert(plan.end(),cohort.begin(),cohort.end()); cost+=addedCost; troops+=static_cast<int>(cohort.size()); ++routes;
	}
	return routes>=2 ? plan : std::vector<Action>{};
}

struct PendingCounter {
	ColdStorageStrategy::BlastThreat blast;
	float at;
	int target = -1;
	float lastTargetX = 0;
	bool targetLocked = false;
	int plantID = 0;
	float invulnerableAt = 0;
	bool consumesPlant = true;
	float releaseAt = 0;
	bool released = false;
    bool publicLaunchObserved=false; // 已离膛事件独立于可选追踪记录

    int traceID=0; bool traceLaunchRecorded=false; // 仅关联显式预测事件，不改变炮弹生命周期
	bool clearsCell = false;
	float craterSeconds = 0;
	int ladderClearRow=-1,ladderClearRadius=-1; bool clearsLadderRow=false;
};

constexpr size_t kCombatTraceEventLimit=4096; // 单次显式预测最多保留的战斗事件数，超额标记截断

/** 取证关闭时不分配事件容器；上限只截断日志，不能中止战斗积分。 */
bool CanTraceCombat(const Snapshot& state,ConstructionStats& stats) {
    if(!state.traceCombat || stats.combatTraceTruncated) return false;
    if(stats.combatTrace.size()<kCombatTraceEventLimit) return true;
    stats.combatTraceTruncated=true; return false;
}

/** 保存已发生的诊断事件，不增加任何预测收益或战损。 */
void AppendCombatTrace(const Snapshot& state,ConstructionStats& stats,CombatForecastTrace event) {
    if(CanTraceCombat(state,stats)) stats.combatTrace.push_back(std::move(event));
}

/** 冻结数值单位的各生命层；模拟活性按总生命，掉头另列，不能冒充实体IsActive。 */
CombatForecastHit UnitCombatHit(const Snapshot& state,const Unit& unit,float requested) {
    CombatForecastHit hit;
    hit.id=unit.id; hit.row=unit.body.row; hit.x=unit.body.x+unit.body.blastAnchorOffset; hit.y=ForecastRowY(state,unit.body.row,unit.body.x);
    hit.requestedDamage=requested; hit.before=hit.after=unit.body.health;
    hit.beforeHelm=hit.afterHelm=unit.helmHealth; hit.beforeShield=hit.afterShield=unit.shieldHealth;
    hit.beforeBody=hit.afterBody=unit.body.health-unit.helmHealth-unit.shieldHealth-(unit.balloon.present ? unit.balloon.health : 0);
    hit.activeBefore=hit.activeAfter=unit.body.health>0; hit.headlessBefore=hit.headlessAfter=unit.headless;
    return hit;
}

/** 补齐同一次实际积分调用后的生命层，不用名义炮伤推算死亡。 */
void FinishUnitCombatHit(CombatForecastHit& hit,const Unit& unit) {
    hit.after=unit.body.health; hit.afterHelm=unit.helmHealth; hit.afterShield=unit.shieldHealth;
    hit.afterBody=unit.body.health-unit.helmHealth-unit.shieldHealth-(unit.balloon.present ? unit.balloon.health : 0);
    hit.activeAfter=unit.body.health>0; hit.headlessAfter=unit.headless;
}

/** 植物承伤记录对应归并后的真实数值目标，南瓜与宿主不重复归因。 */
CombatForecastHit PlantCombatHit(const Plant& plant,float requested) {
    CombatForecastHit hit; hit.id=plant.id; hit.type=plant.actualType; hit.row=plant.row; hit.column=plant.column;
    hit.x=plant.x; hit.y=plant.y; hit.requestedDamage=requested; hit.before=hit.after=plant.health;
    hit.beforeBody=hit.afterBody=plant.health; hit.activeBefore=hit.activeAfter=plant.health>0;
    return hit;
}

struct PlantingBlock { int row, column; float until; };

/** 弹坑仅阻止原格落种；寿命结束或选择其他格仍可继续反制和建设。 */
bool PlantingBlocked(const std::vector<PlantingBlock>& blocks, int row, int column, float time) {
	return std::any_of(blocks.begin(),blocks.end(),[&](const auto& block) {
		return block.row == row && block.column == column && time < block.until;
	});
}

/** 普通层宽株在每个占格阻止落种；仍只保留一份实体、生命及资产。 */
bool OccupiesPlantCell(const Plant& plant, int row, int column) {
	return plant.row == row && column >= plant.column
		&& column < plant.column + (plant.layer == 1 ? std::max(1,plant.footprintColumns) : 1);
}

/** 一个预测世界从不删除死亡记录，递减身份不会把旧锁定交给同格补种。 */
int NextConstructionPlantID(const std::vector<Plant>& plants) {
	int id = -1000000;
	for (const auto& plant : plants) id = std::min(id,plant.id-1);
	return id;
}

/** 每只就绪狙击手独立锁定这次新种，已有植物不会凭空触发射击。 */
void ReactToDeployment(float time, const Plant& plant, std::vector<Unit>& units) {
	for (auto& unit : units) {
		auto& sniper = unit.sniper;
		if (!sniper.enabled || sniper.aiming || sniper.remaining > 0 || unit.body.spawnAt > time
			|| unit.body.health <= sniper.stopHealth || unit.body.row != plant.row) continue;
		sniper.aiming = true; sniper.remaining = ThermalSniperRules::Aim;
		sniper.targetID = plant.id; sniper.targetX = plant.x; sniper.damage = plant.maximumHealth;
	}
}

/** 推进舞王动作及独立伴舞：定身期间不能形成虚构的高速屏障，邻行召唤也会引来电弧。 */
void AdvanceDances(const Snapshot& state,float time,std::vector<Unit>& units,
	const std::unordered_map<int,size_t>& indices,size_t& nextSlot,size_t slotEnd,
	std::vector<float>& initialHealth,std::vector<float>& initialX,std::vector<bool>& blocked,ConstructionStats& stats) {
	using Phase=DancerRules::Forecast::Phase;
	std::fill(blocked.begin(),blocked.end(),false);
	const auto followerAlive=[&](int id) {
		const auto found=indices.find(id);
		return id!=0 && found!=indices.end() && units[found->second].body.health>0;
	};
	for(size_t i=0;i<units.size();++i) {
		auto& unit=units[i]; auto& dance=unit.dance; auto& body=unit.body;
		if((!dance.leader && !dance.backup) || body.health<=0 || body.spawnAt>time) continue;
		const float active=std::max(0.0f,kStep-body.stopped)*(body.slow>0 ? .5f : 1);
		blocked[i]=dance.phase==Phase::SNAP || dance.phase==Phase::HOLD;
		if(active<=0 || (dance.leader && body.health<=dance.stopHealth)) continue;
		if(dance.phase==Phase::ENTRY) {
			dance.remaining-=active;
			if(dance.remaining<=0) { dance.phase=Phase::SNAP; dance.remaining=dance.snapSeconds; }
		} else if(dance.phase==Phase::SNAP) {
			// 响指由动画推进；寒冰的动画倍率与入场/保持的内部半速计时不同。
			dance.remaining-=std::max(0.0f,kStep-body.stopped)*DancerRules::DanceSpeed
				*(body.slow>0 ? ZombieMovementRules::NormalSlowAnimationFactor : 1);
			if(dance.remaining>0) continue;
			const float x=body.x+body.blastAnchorOffset;
			const int rows[4]={body.row-1,body.row+1,body.row,body.row};
			const float offsets[4]={0,0,-DancerRules::SideDistance,DancerRules::SideDistance};
			for(int slot=0;slot<4 && nextSlot<slotEnd;++slot) {
				if(rows[slot]<0 || rows[slot]>=state.rows
					|| (slot==2 && x<DancerRules::FrontMinimumX) || followerAlive(dance.followers[slot])) continue;
				const size_t index=nextSlot++;
				const int id=units[index].id;
				units[index]=state.dancerBackup;
				auto& child=units[index]; child.id=id;
				child.body.row=rows[slot]; child.body.x=x+offsets[slot]-child.body.blastAnchorOffset;
				child.body.spawnAt=time; child.body.purchaseCost=0; child.playerRefund=0;
				initialHealth[index]=child.body.health; initialX[index]=child.body.x;
				dance.followers[slot]=id; ++stats.dancerSummons;
			}
			dance.phase=Phase::HOLD; dance.remaining=DancerRules::HoldSeconds;
		} else if(dance.phase==Phase::HOLD) {
			dance.remaining-=active;
			if(dance.remaining<=0) { dance.phase=Phase::DANCE; body.speed=dance.walkSpeed; body.canBeChilled=true; }
		} else if(dance.leader && body.x+body.blastAnchorOffset>=DancerRules::RefillLimitX) {
			// 半秒积分可能跨过只有0.2秒的第12拍；按跨拍边沿补召，不凭空每步刷新伴舞。
			const float beat=state.danceBeatSeconds+time-DancerRules::RefillBeatSeconds;
			const bool refillBeat=std::floor((beat+kStep)/DancerRules::BeatSeconds)>std::floor(beat/DancerRules::BeatSeconds)
				|| (std::fmod(beat+DancerRules::BeatSeconds,DancerRules::BeatSeconds)<.2f);
			if(!refillBeat) continue;
			bool missing=false;
			for(int slot=0;slot<4;++slot) {
				if((slot==0 && body.row==0) || (slot==1 && body.row+1>=state.rows)) continue;
				missing=missing || !followerAlive(dance.followers[slot]);
			}
			if(missing) { dance.phase=Phase::SNAP; dance.remaining=dance.snapSeconds; }
		}
	}
}

/** 按共享充能与择时释放模拟逐行打击；前排位置本身不能替工人挡主伤害。 */
void AdvanceRowStrikes(const Snapshot& state, const std::vector<RowStrike>& strikes, float time, const std::vector<Plant>& plants,
	std::vector<Unit>& units, const std::vector<float>& initialHealth, std::vector<float>& ready,
	float holdSeconds, std::vector<float>& holdUntil, Weights& features) {
	// 建设模型可在推演中新增来源；每株只拥有一个等待计时，不能为各行复制充能。
	holdUntil.resize(strikes.size(),-1);
	for (size_t ability = 0; ability < strikes.size(); ++ability) {
		const auto& strike = strikes[ability];
		if (time < ready[ability] || std::none_of(plants.begin(),plants.end(),[&](const auto& plant) {
			return plant.id == strike.plantID && plant.health > 0 && plant.shutdownUntil<=time;
		})) continue;
		if (holdSeconds > 0) {
			bool hasTarget = false, urgent = false;
			for (const auto& unit : units) {
				const auto& body = unit.body;
				if (body.health <= 0 || body.spawnAt > time) continue;
				hasTarget = true;
				urgent = urgent || body.x < state.houseX+kUrgentCounterDistance;
			}
			// 蓄满不等于玩家已承诺释放。等已经出生的目标出现才开始计时，
			// 不把稍晚出生的工人误算成已骗掉充能；来源死亡仍由上方门禁取消。
			if (!hasTarget) continue;
			if (!urgent) {
				if (holdUntil[ability] < 0) holdUntil[ability] = time+holdSeconds;
				if (time < holdUntil[ability]) continue;
			}
		}
		bool used = false;
		for (int row = 0; row < static_cast<int>(state.context.size()); ++row) {
			int target = -1;
			long long best = -1, bestID = 0;
			for (size_t i = 0; i < units.size(); ++i) {
				const auto& u = units[i];
				if (u.body.health <= 0 || u.body.spawnAt > time || u.body.row != row) continue;
				const auto score = DawnLotusRules::ThreatScore(static_cast<long long>(u.body.health),
					u.body.x+u.body.blastAnchorOffset,state.rightEdge);
				const long long id = u.id > 0 ? u.id : 0x100000000LL+static_cast<long long>(i);
				if (target < 0 || score > best || (score == best && id < bestID)) {
					target = static_cast<int>(i); best = score; bestID = id;
				}
			}
			if (target < 0) continue;
			used = true;
			const float center = units[target].body.x + units[target].body.blastAnchorOffset;
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > time || u.row != row) continue;
				const bool primary = static_cast<int>(i) == target;
				if (!primary && std::abs(u.x+u.blastAnchorOffset-center) > strike.radius) continue;
				const float damage = ApplyDiscreteHit(units[i],primary ? strike.damage : strike.splashDamage,false,&units,&state,time);
				const float credit = u.purchaseCost * damage / std::max(1.0f,initialHealth[i]);
				features[6] += credit; units[i].blastCredit += credit;
			}
		}
		// 无目标时保留充能；释放一次覆盖所有行，而不是让每行各自获得独立冷却。
		if (used) { ready[ability] = time + strike.recharge; holdUntil[ability] = -1; }
	}
}

/** 按真实爆区覆盖已经出生的单位；场外不是灰烬免疫区，尚未出生的队列仍不承伤。 */
bool CounterHits(const ColdStorageStrategy::BlastThreat& blast, const Unit& unit, float time) {
	const auto& body = unit.body;
	return body.health > 0 && body.spawnAt <= time && blast.reach[body.row] >= 0
		&& (!blast.requiresGroundTarget || CanTargetProjectile(unit,false))
		&& std::abs(body.x + (blast.usesObjectX ? body.blastAnchorOffset : 0) - blast.x) <= blast.reach[body.row];
}

/** 在已选灰烬之前比较合法时间干扰；只按当前锚目标判断，不预知未来落点或出生。 */
void PreemptInterference(const Snapshot& s,float time,const ColdStorageStrategy::BlastThreat& blast,
	const std::vector<Unit>& units,std::vector<TemporalAnchor>& anchors,float reservedIce,float& ice,
	float& ready,float& until,ConstructionStats& stats) {
	if(!s.interferenceAvailable || time<ready || ice<ColdStorageSkillRules::InterferenceIceCost+reservedIce) return;
	float preventable=0;
	for(const auto& anchor:anchors) for(const auto& target:anchor.targets) {
		if(target.unit<0 || target.unit>=static_cast<int>(units.size())) continue;
		const auto& unit=units[target.unit];
		if(unit.temporalIrreversible || !CounterHits(blast,unit,time)) continue;
		auto hit=unit;
		preventable+=std::min(target.saved.body.health,ApplyDiscreteHit(hit,blast.damage,true,nullptr,nullptr,time,blast.pressureAshReduction && PressureAshProtected(unit,units,s,time)));
	}
	if(preventable<DawnLotusRules::Damage) return;
	// 时间干扰先付款，灰烬仍须支付预留费用；取消死者记录与禁锚均沿用正式规则。
	anchors.clear(); ice-=ColdStorageSkillRules::InterferenceIceCost;
	stats.iceSpent+=ColdStorageSkillRules::InterferenceIceCost; ++stats.interferences;
	ready=time+ColdStorageSkillRules::InterferenceCooldown;
	until=time+ColdStorageSkillRules::InterferenceDuration;
}

/** 多张牌共享真实资源、同卡落点共享冷却；先兑现已提交反制，再选择一次可支付的新动作。 */
void AdvanceCounters(const Snapshot& state, const std::vector<Counter>& counters, float time, std::vector<Plant>& plants, std::vector<Unit>& units,
	const std::vector<float>& initialHealth, std::vector<float>& ready,
	std::vector<PendingCounter>& pending, float& sun, float& ice, Weights& features,
	float holdSeconds, float storedHoldSeconds, std::vector<float>& holdUntil,
	std::vector<PlantingBlock>& blocks, ConstructionStats& stats,
    const std::array<float,54>* fogAlpha, bool allowShovel, std::vector<float>& shovelReady, bool preservePaidDefenses,
	bool interferenceBeforeAsh,std::vector<TemporalAnchor>& anchors,float& interferenceReady,float& interferenceUntil,
	std::vector<LadderRules::Cell>& ladders,ColdStorageObservedForecast::WorldClock* observedClock) {
	for (auto it = pending.begin(); it != pending.end();) {
		if (it->plantID != 0 && (it->consumesPlant || !it->released)) {
			const auto source = std::find_if(plants.begin(),plants.end(),[&](const auto& p) { return p.id == it->plantID; });
			// 咖啡已经放下仍不等于爆炸已脱离宿主；睡眠/唤醒等待阶段被吃掉就不能引爆。
			if (source == plants.end() || source->health <= 0) {
                if(CanTraceCombat(state,stats)) {
                    CombatForecastTrace event; event.event="counter_cancel"; event.reason="source_removed_before_release";
                    event.at=time; event.attackID=it->traceID; event.sourceID=it->plantID;
                    event.x=it->blast.x; event.targetRow=it->ladderClearRow; event.resolveAt=it->at; AppendCombatTrace(state,stats,std::move(event));
                }
                it = pending.erase(it); continue;
            }
			// 离膛当步先复核来源，再冻结独立飞行；浮点步长不能让同一步先落地的狙击被跳过。
			if (!it->consumesPlant && time >= it->releaseAt) it->released=true;
			if (it->consumesPlant && time >= it->invulnerableAt) {
				source->edible = false; source->deploymentInterceptionOnly = true;
			}
		}
        if(!it->consumesPlant && !it->publicLaunchObserved && time>=it->releaseAt) {
            it->publicLaunchObserved=true; ++stats.observedCobLaunches;
        }
        if(!it->consumesPlant && !it->traceLaunchRecorded && time>=it->releaseAt && CanTraceCombat(state,stats)) {
            CombatForecastTrace event; event.event="counter_launch"; event.at=time; event.attackID=it->traceID;
            event.sourceID=it->plantID; event.x=it->blast.x; event.targetRow=it->ladderClearRow; event.resolveAt=it->at;
            AppendCombatTrace(state,stats,std::move(event)); it->traceLaunchRecorded=true;
        }
		// 假想新种倭瓜在起跳前继续追踪；不能把移动目标仍判在最初的观察落点。
		// 正式已提交反制没有 target 索引，保持其快照落点，避免替玩家撤销或重新瞄准。
		if (it->target >= 0 && !it->targetLocked) {
			const auto& target = units[it->target].body;
			if (target.health > 0) {
				const float velocity = (target.x - it->lastTargetX) / kStep;
				it->blast.x = target.x + (it->blast.usesObjectX ? target.blastAnchorOffset : 0);
				it->blast.responseArea={}; // 倭瓜追踪不属于已核对的公开面积形状。
				if (time >= it->at - kSquashFlightSeconds) it->blast.x += velocity * kSquashLeadSeconds;
				it->lastTargetX = target.x;
			}
			if (time >= it->at - kSquashFlightSeconds) it->targetLocked = true;
		}
		if (it->at > time) { ++it; continue; }
		ClearForecastLadders(state,it->blast.x,it->ladderClearRow,it->ladderClearRadius,it->clearsLadderRow,ladders,stats);
		if(interferenceBeforeAsh && !preservePaidDefenses)
			PreemptInterference(state,time,it->blast,units,anchors,0,ice,interferenceReady,interferenceUntil,stats);
		if (state.traceEconomy) {
			const auto source = std::find_if(plants.begin(),plants.end(),[&](const auto& p) { return p.id == it->plantID; });
			stats.counterTrace.push_back({time,it->blast.x,it->blast.damage,
				source == plants.end() ? -1 : source->row,source == plants.end() ? -1 : source->column,it->clearsCell});
		}

        CombatForecastTrace combatEvent;
        const bool recordImpact=CanTraceCombat(state,stats);
        if(recordImpact) {
            combatEvent.event="counter_impact"; combatEvent.at=time; combatEvent.attackID=it->traceID;
            combatEvent.sourceID=it->plantID; combatEvent.x=it->blast.x; combatEvent.targetRow=it->ladderClearRow; combatEvent.resolveAt=it->at;
        }
		const auto pressureProtected=FreezePressureAsh(units,state,time);
		std::vector<unsigned char> protectedWorkers(units.size());
		for (auto& engineer:units) if (engineer.engineer && engineer.canisterFull
			&& engineer.body.spawnAt<=time && engineer.body.health>engineer.engineerStopHealth) {
			std::vector<std::pair<float,size_t>> nearby;
			for (size_t i=0;i<units.size();++i) {
				const auto& u=units[i]; const float distance=std::abs(u.body.x-engineer.body.x);
				if (u.body.economic && u.body.health>u.productionStopHealth && u.body.spawnAt<=time
					&& u.body.row==engineer.body.row && distance<=DisasterEngineerRules::RadiusCells*state.cellWidth)
					nearby.emplace_back(distance,i);
			}
			std::sort(nearby.begin(),nearby.end()); bool used=false;
			for (size_t n=0;n<nearby.size() && n<DisasterEngineerRules::Capacity;++n) {
				const size_t i=nearby[n].second;
				if (!protectedWorkers[i] && CounterHits(it->blast,units[i],time)) {
					protectedWorkers[i]=1; used=true; ++stats.engineerBlocks;
					auto exposed=units[i];
					stats.workerProtectionProgress+=units[i].body.purchaseCost
						*ApplyDiscreteHit(exposed,it->blast.damage,true,nullptr,nullptr,time,it->blast.pressureAshReduction && pressureProtected[i])/std::max(1.0f,initialHealth[i]);
				}
			}
			if (used) { engineer.canisterFull=false; engineer.reloadPaid=false; engineer.reloadRemaining=DisasterEngineerRules::ReloadSeconds; }
		}
		for (size_t i = 0; i < units.size(); ++i) if (!protectedWorkers[i] && CounterHits(it->blast, units[i], time)) {
			auto& body = units[i].body;
            auto hit=recordImpact ? UnitCombatHit(state,units[i],it->blast.damage) : CombatForecastHit{};
			const float damage = ApplyDiscreteHit(units[i],it->blast.damage,true,nullptr,nullptr,time,it->blast.pressureAshReduction && pressureProtected[i]);
            if(recordImpact) {FinishUnitCombatHit(hit,units[i]);combatEvent.targets.push_back(hit);}
			const float credit = body.purchaseCost * damage / std::max(1.0f,initialHealth[i]);
			features[6] += credit; units[i].blastCredit += credit;
		}
        if(recordImpact) AppendCombatTrace(state,stats,std::move(combatEvent));
		if (it->plantID != 0 && it->consumesPlant) {
			const auto source = std::find_if(plants.begin(),plants.end(),[&](const auto& p) { return p.id == it->plantID; });
			if (it->clearsCell) {
				// 来源被提前消灭时已在上方取消，只有真正引爆才产生弹坑。
				if (it->craterSeconds > 0) {
					blocks.push_back({source->row,source->column,time+it->craterSeconds});
					++stats.cratersCreated;
				}
				for (auto& plant : plants) if (OccupiesPlantCell(plant,source->row,source->column)) ConsumeForecastPlant(plant);
			} else ConsumeForecastPlant(*source);
		}
		it = pending.erase(it);
	}
    std::uint64_t deferredSources=0;
    std::vector<unsigned char> deferredCandidates;
    if(observedClock && state.observedProfile) {
        std::vector<ColdStorageObservedForecast::Block> publicBlocks;
        publicBlocks.reserve(blocks.size());
        for(const auto& block:blocks) publicBlocks.push_back({block.row,block.column,block.until});
        std::vector<ColdStorageStrategy::BlastThreat> publicPending;
        publicPending.reserve(pending.size());
        for(const auto& scheduled:pending) publicPending.push_back(scheduled.blast);
        const ColdStorageObservedForecast::View view{state,units,plants,counters,ready,time,sun,ice,publicBlocks,publicPending,fogAlpha};
        auto opportunity=ColdStorageObservedForecast::Inspect(view);
        observedClock->Update(time,opportunity);
        deferredSources=observedClock->DeferredSources(*state.observedProfile,opportunity);
        deferredCandidates=std::move(opportunity.directPaidCounters);
    }
	// 在途爆炸先照常结算。新选点只需本步已出生的活体；大量死亡/召唤占位不能反复乘上每门炮的全部落点。
	// 下个积分步重新构造，保留复活、召唤和延迟出生的原索引/顺序，不缓存跨步的命中集合。
	std::vector<size_t> activeUnits;
	activeUnits.reserve(units.size());
	for(size_t i=0;i<units.size();++i)
		if(units[i].body.health>0 && units[i].body.spawnAt<=time) activeUnits.push_back(i);
	if(activeUnits.empty()) return;
	int selected = -1;
	int selectedTarget = -1;
	int selectedPlant = -1;
	float best = 0;
	// 一个时步内所有候选复用占位表，补入未来落点后不能按候选反复扫描整块阵地。
	std::array<int,54> occupied; occupied.fill(-1);
	for (size_t i=0;i<plants.size();++i) {
		const auto& plant=plants[i];
		if (plant.health<=0 || plant.layer!=1 || plant.row<0 || plant.row>=state.rows) continue;
		for(int column=plant.column;column<plant.column+std::max(1,plant.footprintColumns);++column) {
			const int cell=plant.row*state.columns+column;
			if(column>=0 && column<state.columns && cell>=0 && cell<static_cast<int>(occupied.size())) occupied[cell]=static_cast<int>(i);
		}
	}
	// 玩家能看见僵尸总数，但雾内精确行列不是公开信息。未知单位按遮雾格的均匀先验
    // 估计盲炸覆盖率；选点后仍对真实位置结算，允许炸空，也允许炸中隐蔽的制冰工。
    const auto hidden=[&](const Unit& unit) {
        if(!fogAlpha) return false;
        const int col=std::clamp(static_cast<int>((unit.body.x-state.gridLeft)/state.cellWidth),0,state.columns-1);
        return (*fogAlpha)[unit.body.row*state.columns+col]>96;
    };
    int unknownCount=0, unknownCells=0, pricedOptions=0;
    std::array<unsigned,6> blindCells{};
    float unknownStake=0, unknownHealth=0;
    if(fogAlpha) {
        for(const auto& unit:units) if(unit.body.health>0 && unit.body.spawnAt<=time && hidden(unit)) ++unknownCount;
        for(int cell=0;cell<state.rows*state.columns;++cell) if((*fogAlpha)[cell]>96) ++unknownCells;
        for(const auto& option:state.options) if(option.device<0) {
            unknownStake+=option.cost; unknownHealth+=option.unit.body.health; ++pricedOptions;
        }
        // 没有可购兵画像时只能用无价召唤的保守威胁，不借用隐藏单位的真实类型。
        unknownStake=pricedOptions ? unknownStake/pricedOptions : kUnpricedCounterStake;
        unknownHealth=pricedOptions ? unknownHealth/pricedOptions : 1;
        if(unknownCount>0) for(int r=0;r<state.rows;++r) for(int col=0;col<state.columns;++col) {
            if((*fogAlpha)[r*state.columns+col]<=96) continue;
            const float x=state.gridLeft+(col+.5f)*state.cellWidth;
            if(std::none_of(pending.begin(),pending.end(),[&](const PendingCounter& scheduled) {
                return scheduled.blast.reach[r]>=0 && std::abs(x-scheduled.blast.x)<=scheduled.blast.reach[r];
            })) blindCells[r]|=1u<<col;
        }
    }
	ColdStorageStrategy::BlastThreat impact;
	// 同炮的全部爆点共享一次来源存活查询；新增/已毁来源增多也不按爆点反复扫描植物。
	std::vector<unsigned char> sourceAlive(ready.size());
    // 新反制的边际收益共用一份在途伤害预览；逐次冻结保护，不能让已被前一爆杀死的护卫继续保护。
    auto pendingProjection=units;
    std::vector<const PendingCounter*> orderedPending;
    for(const auto& scheduled:pending) orderedPending.push_back(&scheduled);
    std::stable_sort(orderedPending.begin(),orderedPending.end(),[](const auto* a,const auto* b) {return a->at<b->at;});
    for(const auto* scheduled:orderedPending) {
        const auto covered=FreezePressureAsh(pendingProjection,state,time);
        for(const size_t i:activeUnits) if(CounterHits(scheduled->blast,pendingProjection[i],time))
            ApplyDiscreteHit(pendingProjection[i],scheduled->blast.damage,true,nullptr,nullptr,time,
                scheduled->blast.pressureAshReduction && covered[i]);
    }
    const auto candidateProtection=FreezePressureAsh(pendingProjection,state,time);
	for (size_t c = 0; c < counters.size(); ++c) {
		const auto& counter = counters[c];
		if (PlantingBlocked(blocks,counter.cellRow,counter.cellColumn,time)) continue;
		if (counter.blast.committed || time < ready[counter.source]
			|| (counter.sharedSource >= 0 && time < ready[counter.sharedSource])
			|| counter.sunCost > sun || PlayerIceCost(state,time,counter.iceCost) > ice) continue;
		if(preservePaidDefenses && (counter.sunCost>0 || counter.iceCost>0)) continue;
		if(counter.plantID!=0) {
			auto& alive=sourceAlive[counter.source];
			if(alive==0) alive=std::any_of(plants.begin(),plants.end(),[&](const Plant& p) {
				return p.id==counter.plantID && p.health>0 && p.shutdownUntil<=time;
			}) ? 1 : 2;
			if(alive==2) continue;
		}
		const int cell=counter.cellRow*state.columns+counter.cellColumn;
		const int victim=counter.cellRow>=0 && cell>=0 && cell<static_cast<int>(occupied.size()) ? occupied[cell] : -1;
		// 当前铲种只支持单格普通层；不能从右半格铲掉半门炮。
		if (victim>=0 && (!allowShovel || !counter.shovelAllowed || plants[victim].footprintColumns>1)) continue;
		const float sacrificed=victim<0 ? 0 : plants[victim].assetValue
			* std::clamp(plants[victim].health/std::max(1.0f,plants[victim].initialHealth),0.0f,1.0f);
		auto candidate = counter.blast;
		int candidateTarget = -1;
		if (counter.targeted) {
			float nearest = kSquashTriggerRange;
			int target = -1;
			for (const size_t i : activeUnits) {
				const auto& u = units[i].body;
				const float distance = std::abs(u.x - candidate.x);
				if (!hidden(units[i]) && u.health > 0 && u.spawnAt <= time && u.x <= state.rightEdge
					&& (!candidate.requiresGroundTarget || CanTargetProjectile(units[i],false))
					&& candidate.reach[u.row] >= 0 && distance < nearest) { nearest = distance; target = static_cast<int>(i); }
			}
			if (target < 0) continue;
			candidateTarget = target;
			candidate.x = units[target].body.x + units[target].body.blastAnchorOffset;
			candidate.responseArea={};
			for (float& reach : candidate.reach) if (reach >= 0) reach = kSquashImpactRange;
		}
		float loss = 0;
		bool urgent = false;
        bool visibleCounterWitness=false;
		for (const size_t i : activeUnits) if (!hidden(units[i]) && CounterHits(candidate, units[i], time)) {
			auto projected = pendingProjection[i];
			if (projected.body.health <= 0) continue;
            if(state.diagnosticVisibleCounterWitness) {
                const auto& witness=units[i];
                const long objectX=std::lround(witness.body.x+witness.body.blastAnchorOffset);
                const bool targetable=witness.digger.present ? witness.digger.CanGroundHazard()
                    : witness.groundHazard || witness.balloon.present;
                const float bodyHealth=witness.body.health-witness.helmHealth-witness.shieldHealth
                    -(witness.balloon.present ? witness.balloon.health : 0);
                // 只约束新选点的见证；场外同爆区受害者仍贡献loss，所有已提交爆炸照常结算。
                visibleCounterWitness|=targetable && bodyHealth>0 && objectX>=0 && objectX<=state.rightEdge;
            }
			const float stake = std::max(kUnpricedCounterStake,units[i].body.purchaseCost > 0 ? units[i].body.purchaseCost : units[i].body.value);
			loss += stake * ApplyDiscreteHit(projected,candidate.damage,true,nullptr,nullptr,time,candidate.pressureAshReduction && candidateProtection[i]) / std::max(1.0f, initialHealth[i]);
			urgent = urgent || units[i].body.x < state.houseX+kUrgentCounterDistance;
		}
        if(unknownCount>0 && unknownCells>0 && !counter.targeted) {
            int covered=0;
            for(int r=0;r<state.rows;++r) {
                if(candidate.reach[r]<0 || !blindCells[r]) continue;
                const int first=std::clamp(static_cast<int>(std::ceil((candidate.x-candidate.reach[r]-state.gridLeft)/state.cellWidth-.5f)),0,state.columns);
                const int end=std::clamp(static_cast<int>(std::floor((candidate.x+candidate.reach[r]-state.gridLeft)/state.cellWidth-.5f))+1,0,state.columns);
                if(end>first) covered+=static_cast<int>(std::bitset<32>(blindCells[r]&((1u<<end)-(1u<<first))).count());
            }
            loss+=unknownCount*unknownStake*covered/unknownCells*std::min(1.0f,candidate.damage/std::max(1.0f,unknownHealth));
        }
		const float netLoss=urgent ? loss : loss-sacrificed;
		// 免费、可重复来源不沿用付费灰烬的省钱门槛，否则12/18冰单兵会被误判成能白打整座炮阵。
		// 即时开火与长期留炮是分别完整推演的合法选择，不保证小诱饵一定消耗炮弹。
		const bool freeReusable=!counter.consumesPlant && counter.sunCost==0 && counter.iceCost==0;
        if(state.diagnosticVisibleCounterWitness && freeReusable && !visibleCounterWitness) continue;
		const float minimumStake=urgent ? kUnpricedCounterStake
			: counter.targeted ? kTargetCounterStake : kAreaCounterStake;
		if (freeReusable ? netLoss<=0 : netLoss<minimumStake) continue;
		// 等待从首次存在值得反制的目标开始；同一张牌所有落点共用一次等待。
		// 已提交的爆炸走上方独立结算；接近房屋的救险也不为了聚团继续等。
		// 可重复炮也能长期留火，分段进攻不能只靠晚于固定8秒就假定诱饵已耗掉所有炮。
		// 仍是一份完整合法应对；已离膛弹照常结算，真正接近房屋的威胁仍优先救险。
		const float patience = counter.stored || !counter.consumesPlant ? std::max(holdSeconds,storedHoldSeconds) : holdSeconds;
		if (!urgent && patience > 0) {
			if (holdUntil[counter.source] < 0) holdUntil[counter.source] = time+patience;
			if (time < holdUntil[counter.source]) continue;
		}
        // 只限制已观察的新直接落种灰烬。旧炮、在途弹、预存菇和铲种应对继续原完整策略。
        // 原有patience先更新，两个等待窗口并行，不能把16秒习惯再叠加一次8秒人为留牌。
        const int key=counter.observedSourceKey;
        if(victim<0 && c<deferredCandidates.size() && deferredCandidates[c] && key>=1 && key<=64 && (deferredSources&(std::uint64_t{1}<<(key-1)))
            && counter.consumesPlant && !counter.blast.committed && !counter.stored && !counter.targeted
            && counter.plantID==0 && (counter.sunCost>0 || counter.iceCost>0)) {
            ++stats.observedDelaySteps;continue;
        }
		const float value = netLoss / std::max(1.0f, counter.sunCost * 0.02f + PlayerIceCost(state,time,counter.iceCost) * 0.2f);
		if (value > best) { best = value; selected = static_cast<int>(c); impact = candidate; selectedTarget = candidateTarget; selectedPlant=victim; }
	}
	if (selected >= 0) {
		const auto& counter = counters[selected];
		if (selectedPlant>=0) {
			// 准备期间不扣钱、不铲株；下一步仍重新检查冷却、钱包、占位及威胁，再完成铲种。
			if (shovelReady[selected]<0) shovelReady[selected]=time+kCounterShovelSeconds;
			if (time<shovelReady[selected]) return;
			auto& removed=plants[selectedPlant];
			stats.counterShovelAssets+=removed.assetValue*std::clamp(removed.health/std::max(1.0f,removed.initialHealth),0.0f,1.0f);
			++stats.counterShovels;
			ConsumeForecastPlant(removed); // 主动牺牲不走 DamagePlant，不给僵尸击杀冰或材料价值；外壳/承载层保留。
		}
		shovelReady[selected]=-1;
		if(interferenceBeforeAsh && !preservePaidDefenses)
			PreemptInterference(state,time,impact,units,anchors,PlayerIceCost(state,time,counter.iceCost),
				ice,interferenceReady,interferenceUntil,stats);
		sun -= counter.sunCost; ice -= PlayerIceCost(state,time,counter.iceCost);
		if(counter.sunCost>0 || counter.iceCost>0) ++stats.paidCounterCasts;
		if(counter.plantID<0 && !counter.consumesPlant) ++stats.constructedCobShots;
		ready[counter.source] = time + counter.recharge;
		if (counter.sharedSource >= 0) ready[counter.sharedSource] = time + counter.sharedRecharge;
		holdUntil[counter.source] = -1;
		pending.push_back({impact,time + counter.windup,selectedTarget,
			selectedTarget >= 0 ? units[selectedTarget].body.x : 0});
		pending.back().plantID = counter.plantID;
        if(CanTraceCombat(state,stats)) {
            pending.back().traceID=++stats.nextCombatAttackID;
        }
		pending.back().invulnerableAt = time + counter.vulnerableSeconds;
		pending.back().consumesPlant = counter.consumesPlant;
		pending.back().releaseAt = pending.back().at-std::max(0.0f,counter.flightSeconds);
		pending.back().clearsCell = counter.clearsCell;
		pending.back().craterSeconds = counter.craterSeconds;
		pending.back().ladderClearRow=counter.ladderClearRow;
		pending.back().ladderClearRadius=counter.ladderClearRadius;
		pending.back().clearsLadderRow=counter.clearsLadderRow;
		// 即时灰烬也是真实落种事件。先扣同一张卡的费用/冷却，再建立可被狙击的来源；
		// 已预存毁灭的咖啡触发不算重新种下毁灭，不能给狙击手虚构一个新目标。
		if (counter.plantID == 0 && counter.deploymentHealth > 0 && counter.cellRow >= 0) {
			Plant source;
			source.id = -200000-static_cast<int>(plants.size());
			source.row = counter.cellRow; source.column = counter.cellColumn; source.x = counter.blast.x;
			if (state.weatherStation) {
				source.executionGroup = source.row*state.columns+source.column;
				source.countsExecution = source.diesExecution = true;
			}
			source.health = source.maximumHealth = source.initialHealth = counter.deploymentHealth;
			source.reward = counter.deploymentReward; source.assetValue = counter.deploymentAssetValue;
			source.edible = counter.vulnerableSeconds > 0;
			source.deploymentInterceptionOnly = !source.edible;
			source.counterBlastAt = pending.back().at;
			plants.push_back(source); pending.back().plantID = source.id;
			ReactToDeployment(time,source,units);
		}
        if(CanTraceCombat(state,stats)) {
            const auto& shot=pending.back();
            CombatForecastTrace event;event.event="counter_lock";event.at=time;event.attackID=shot.traceID;
            event.sourceID=shot.plantID;event.targetRow=counter.ladderClearRow;
            for(const auto& source:plants) if(source.id==shot.plantID) {event.sourceRow=source.row;event.sourceColumn=source.column;break;}
            event.x=impact.x;event.resolveAt=shot.at;AppendCombatTrace(state,stats,std::move(event));
        }
	}
}
}

/** 已出膛的热脉冲保留独立飞行段；来源死亡不会取消，原目标消失也不重新瞄准。 */
struct DeploymentPulse {
	int row = 0, targetID = 0;
	float x = 0, endX = 0, damage = 0, launchedAt = 0;
};

/** 现有灯与补种估值共用响应挡位，避免按初始I挡把可切高挡的补灯判成无效。 */
static PlanternGear ResponseLightGear(int response, const Plant& lamp) {
	if (response < 0) return lamp.lightGear;
	return static_cast<PlanternGear>(response == FuelAwarePlanternResponse
		? (lamp.lightFuel >= kResponseHighLightFuel ? 3 : 2) : response);
}

/** 承载层只认采样时的真实身份；支撑死亡后不能借同格其他植物继续施工。 */
static bool ConstructionSupportsAlive(const Construction& card, const std::vector<Plant>& plants) {
	for(size_t cell=0;cell<card.requiredSupportIDs.size();++cell) {
		const int id=card.requiredSupportIDs[cell];
		if(id!=0 && std::none_of(plants.begin(),plants.end(),[&](const Plant& p) {
			return p.id==id && p.health>0 && p.layer==0 && p.row==card.plant.row
				&& p.column==card.plant.column+static_cast<int>(cell);
		})) return false;
	}
	return true;
}

/** 普通建设按完整占格检查空位；南瓜不得套到双格炮上，炮升级另检两株底材。 */
static bool ConstructionCellsFree(const Construction& card, const std::vector<Plant>& plants,
	const std::vector<PlantingBlock>& blocks, float time) {
	if(!ConstructionSupportsAlive(card,plants)) return false;
	const auto& p=card.plant;
	for(int offset=0;offset<std::max(1,p.footprintColumns);++offset) {
		const int column=p.column+offset;
		if(PlantingBlocked(blocks,p.row,column,time)) return false;
		for(const auto& existing:plants) if(existing.health>0 && OccupiesPlantCell(existing,p.row,column)) {
			if(existing.layer==p.layer || (p.layer==2 && existing.layer==1 && existing.footprintColumns>1)) return false;
		}
	}
	return true;
}

struct CobRecipePlan {
	int upgrade=-1, next=-1;
	std::array<int,2> bases{-1,-1};
	float finishAt=0, sun=0, ice=0, value=0;
};

/** 只展开一张炮卡的两个底材步骤，复用真实玉米卡槽及其共享冷却，不预造植物。 */
static CobRecipePlan InspectCobRecipe(const Snapshot& state, int upgrade, float time,
	const std::vector<Plant>& plants, const std::vector<float>& ready, const std::vector<PlantingBlock>& blocks) {
	CobRecipePlan plan;
	const auto& card=state.construction[upgrade]; const auto& p=card.plant;
	if(card.cob.damage<=0 || card.requiredBaseKind<0 || p.footprintColumns!=2
		|| p.row<0 || p.row>=state.rows || p.column<0 || p.column+1>=state.columns
		|| !ConstructionSupportsAlive(card,plants)) return plan;
	plan.upgrade=upgrade; plan.finishAt=time; plan.sun=static_cast<float>(card.sunCost);
	plan.ice=PlayerIceCost(state,time,card.iceCost);
	auto projectedReady=ready;
	for(int offset=0;offset<2;++offset) {
		const int column=p.column+offset;
		if(PlantingBlocked(blocks,p.row,column,time)) return {};
		for(size_t index=0;index<plants.size();++index) {
			const auto& existing=plants[index];
			if(existing.health<=0 || !OccupiesPlantCell(existing,p.row,column)) continue;
			if(existing.layer==2 || existing.layer==3 || existing.layer<0) return {};
			if(existing.layer!=1) continue;
			if(existing.actualType!=card.requiredBaseKind || !existing.cobBaseEligible || existing.footprintColumns!=1) return {};
			plan.bases[offset]=static_cast<int>(index);
		}
		if(plan.bases[offset]>=0) continue;
		int chosen=-1; float earliest=std::numeric_limits<float>::max();
		for(size_t index=0;index<state.construction.size();++index) {
			const auto& base=state.construction[index];
			if(base.requiredBaseKind>=0 || !base.plant.cobBaseEligible || base.plant.actualType!=card.requiredBaseKind
				|| base.plant.row!=p.row || base.plant.column!=column || base.plant.footprintColumns!=1
				|| !ConstructionCellsFree(base,plants,blocks,time)) continue;
			const float at=std::max(plan.finishAt,projectedReady[base.source]);
			if(at<earliest) {earliest=at;chosen=static_cast<int>(index);}
		}
		if(chosen<0) return {};
		const auto& base=state.construction[chosen];
		if(plan.next<0) plan.next=chosen;
		plan.sun+=base.sunCost; plan.ice+=PlayerIceCost(state,earliest,base.iceCost);
		projectedReady[base.source]=earliest+std::max(kConstructionInterval,base.recharge);
		plan.finishAt=earliest+kConstructionInterval;
	}
	if(plan.bases[0]>=0 && plan.bases[1]>=0 && (plan.bases[0]==plan.bases[1]
		|| plants[plan.bases[0]].id==plants[plan.bases[1]].id)) return {};
	plan.finishAt=std::max(plan.finishAt,projectedReady[card.source]);
	if(plan.next<0) plan.next=upgrade;
	return plan;
}

/** 对眼前可见威胁比较有界完整配方；缺钱可等真实生产/在途货，执行仍逐笔查钱包。 */
static CobRecipePlan SelectCobRecipe(const Snapshot& state, float time, float horizon,
	const std::vector<Plant>& plants, const std::vector<Unit>& units, const std::vector<float>& ready,
	const std::vector<PlantingBlock>& blocks, const std::array<float,54>* fogAlpha,
	float sun, float ice, float pendingIce) {
	CobRecipePlan best;
	if(state.cobAimPoints.empty()) return best;
	CobConstruction valuedAbility;
	float cachedShotValue=-1;
	std::array<float,6> visibleThreat{};
	for(const auto& unit:units) if(unit.body.health>0 && unit.body.spawnAt<=time && unit.body.x<=state.rightEdge) {
		const int column=std::clamp(static_cast<int>((unit.body.x-state.gridLeft)/state.cellWidth),0,state.columns-1);
		if(!fogAlpha || (*fogAlpha)[unit.body.row*state.columns+column]<=96) visibleThreat[unit.body.row]+=unit.body.health;
	}
	for(size_t index=0;index<state.construction.size();++index) {
		if(state.construction[index].cob.damage<=0) continue;
		auto plan=InspectCobRecipe(state,static_cast<int>(index),time,plants,ready,blocks);
		if(plan.upgrade<0) continue;
		const auto& card=state.construction[index]; const auto& next=state.construction[plan.next];
		if(std::any_of(units.begin(),units.end(),[&](const Unit& unit) {
			if(unit.body.health<=0 || unit.body.spawnAt>time || unit.body.row!=card.plant.row
				|| unit.body.x>card.plant.x+state.cellWidth+kContact) return false;
			const int column=std::clamp(static_cast<int>((unit.body.x-state.gridLeft)/state.cellWidth),0,state.columns-1);
			return !fogAlpha || (*fogAlpha)[unit.body.row*state.columns+column]<=96;
		})) continue;
		const float firstBlast=plan.finishAt+card.cob.firstReady+card.cob.windup;
		if(firstBlast>=horizon) continue;
		if(sun<plan.sun && std::none_of(plants.begin(),plants.end(),[](const Plant& p) {
			return p.health>0 && p.sunPerSecond>0;
		})) continue;
		if(ice+pendingIce<plan.ice && std::none_of(state.shop.begin(),state.shop.end(),[&](const ShopOrder& order) {
			return order.iceGain>0 && order.sunCost>0 && sun>=next.sunCost+order.sunCost && time+order.delivery<horizon;
		})) continue;
		// 同一炮画像的全部建造锚点共享本步可见爆区估值，不重复执行格位×单位扫描。
		if(cachedShotValue<0 || valuedAbility.damage!=card.cob.damage || valuedAbility.radius!=card.cob.radius
			|| valuedAbility.rowRadius!=card.cob.rowRadius) {
			valuedAbility=card.cob; cachedShotValue=0;
			for(const auto& aim:state.cobAimPoints) {
				float damage=0;
				for(const auto& unit:units) {
					const auto& u=unit.body;
					if(u.health<=0 || u.spawnAt>time || u.x>state.rightEdge || std::abs(u.row-aim.row)>card.cob.rowRadius
						|| std::abs(u.x+u.blastAnchorOffset-aim.x)>card.cob.radius) continue;
					const int column=std::clamp(static_cast<int>((u.x-state.gridLeft)/state.cellWidth),0,state.columns-1);
					if(fogAlpha && (*fogAlpha)[u.row*state.columns+column]>96) continue;
					auto projected=unit; damage+=ApplyDiscreteHit(projected,card.cob.damage,true,nullptr,nullptr,time,PressureAshProtected(unit,units,state,time));
				}
				cachedShotValue=std::max(cachedShotValue,damage);
			}
		}
		// 只恢复快照里确有的原炮位。已知旧阵地可提前准备，不读取将来的敌人数、落点或出生时刻。
		const bool restoring=std::any_of(state.plants.begin(),state.plants.end(),[&](const Plant& original) {
			return original.health>0 && original.footprintColumns==2 && original.actualType==card.plant.actualType
				&& original.row==card.plant.row && original.column==card.plant.column;
		});
		const float shotValue=std::max(cachedShotValue,restoring ? card.cob.damage*kCobRestorationReadiness : 0.0f);
		if(shotValue<=0) continue;
		const float cycles=1+std::max(0.0f,horizon-firstBlast)/std::max(1.0f,card.cob.recharge);
		float lostFire=0;
		for(int base:plan.bases) if(base>=0) lostFire+=plants[base].dps*std::max(0.0f,horizon-plan.finishAt)
			*(.2f+visibleThreat[plants[base].row]/(1000+visibleThreat[plants[base].row]));
		plan.value=std::max(0.0f,shotValue*cycles-lostFire)/(1+.15f*card.plant.column)
			/std::max(25.0f,plan.sun+plan.ice);
		if(plan.value>best.value) best=plan;
	}
	return best;
}

/** 新炮仅注册一次来源；所有合法爆点共用该来源装填，已毁炮身份永不重用。 */
static void RegisterConstructedCob(const Snapshot& state, const Construction& card, const Plant& plant, float time,
	std::vector<Counter>& counters, std::vector<float>& ready, std::vector<float>& holdUntil, std::vector<float>& shovelReady) {
	const int source=static_cast<int>(ready.size());
	ready.push_back(time+card.cob.firstReady); holdUntil.push_back(-1);
	for(const auto& aim:state.cobAimPoints) {
		Counter counter;
		counter.source=source; counter.plantID=plant.id; counter.consumesPlant=false;
		counter.recharge=card.cob.recharge; counter.windup=card.cob.windup; counter.flightSeconds=card.cob.flightSeconds;
		counter.blast.x=aim.x; counter.blast.ready=time+card.cob.firstReady; counter.blast.damage=card.cob.damage;
		counter.blast.responseArea={ColdStorageResponse::AreaKind::Cob,aim.row,aim.x,ForecastRowY(state,aim.row,aim.x)};
		counter.blast.usesObjectX=true; counter.blast.reach.fill(-1);
		for(int row=0;row<state.rows;++row) if(std::abs(row-aim.row)<=card.cob.rowRadius) counter.blast.reach[row]=card.cob.radius;
		counter.ladderClearRow=aim.row; counter.ladderClearRadius=1;
		counters.push_back(counter); shovelReady.push_back(-1);
	}
}

/** 按眼前威胁选择合法补阵；反制照明姿态只补灯，仍与灰烬共用实际资源。 */
static void AdvanceConstruction(const Snapshot& state, float time, float horizon, std::vector<Unit>& units,
	std::vector<Plant>& plants, std::vector<float>& ready, std::vector<int>& uses, std::vector<RowStrike>& strikes,
	std::vector<float>& strikeReady, float& sun, float& ice, ConstructionStats& stats, const std::array<float,54>* fogAlpha,
	const std::vector<PlantingBlock>& blocks, bool lightingOnly, int planternResponseGear, float pendingIce,
	std::vector<Counter>& counters, std::vector<float>& counterReady, std::vector<float>& counterHoldUntil, std::vector<float>& shovelReady) {
	if (plants.size() >= kConstructionPlantLimit) return;
	std::array<float,6> threat{}, nearest{}, fire{};
	std::array<std::uint8_t,54> occupied{};
	int elites=0; bool hasLamp=false;
	nearest.fill(state.rightEdge+100);
	for (const auto& u : units) if (u.body.health > 0 && u.body.spawnAt <= time && u.body.x <= state.rightEdge) {
		threat[u.body.row] += u.body.health;
		nearest[u.body.row] = std::min(nearest[u.body.row],u.body.x);
	}
	// 当前推演的占位/唯一名额只依赖本次存活植物，整理一次供全部格位复用。
	// 不跨逻辑步或候选缓存，铲种、死亡、层次变化仍在下一次建设决策立即生效。
	const auto cellIndex=[&](const Plant& p) {
		const int index=p.row*state.columns+p.column;
		return p.row>=0 && p.row<state.rows && p.column>=0 && p.column<state.columns
			&& p.layer>=0 && p.layer<4 && index>=0 && index<static_cast<int>(occupied.size()) ? index : -1;
	};
	for (const auto& p : plants) if (p.health > 0) {
		fire[p.row] += p.dps; elites+=p.eliteQuota; hasLamp|=p.plantern;
		if(const int index=cellIndex(p); index>=0) for(int offset=0;offset<(p.layer==1 ? std::max(1,p.footprintColumns) : 1);++offset)
			if(p.column+offset<state.columns && index+offset<static_cast<int>(occupied.size())) occupied[index+offset]|=static_cast<std::uint8_t>(1u<<p.layer);
	}
	const bool hasStrike=std::any_of(strikes.begin(),strikes.end(),[&](const auto& strike) {
		return std::any_of(plants.begin(),plants.end(),[&](const auto& p){return p.health>0 && p.id==strike.plantID;});
	});
    // 所有棉花落点共用一次火线投影，避免每个格位重复执行单位×植物×遮挡扫描。
    std::vector<float> healingDemand;
    if(std::any_of(state.construction.begin(),state.construction.end(),[](const Construction& card){return card.plant.cotton;})) {
        healingDemand.resize(plants.size());
        for(size_t i=0;i<plants.size();++i) if(plants[i].health>0)
            healingDemand[i]=std::max(0.0f,plants[i].maximumHealth-plants[i].health);
        for(const auto& u:units) {
            if(u.body.health<=0 || u.body.spawnAt>time) continue;
            int target=-1;
            for(size_t i=0;i<plants.size();++i) {
                const auto& p=plants[i];
                if(p.health<=0 || p.row!=u.body.row || p.x>u.body.x || p.layer>2) continue;
                if(target<0 || p.x>plants[target].x || (p.x==plants[target].x && p.layer>plants[target].layer)) target=static_cast<int>(i);
            }
            if(target<0) continue;
            const float remaining=horizon-time;
            if(u.pressure && u.body.health>u.pressureStopHealth)
                healingDemand[target]+=4*PressureShooterRules::Damage*remaining
                    /(PressureShooterRules::Reload+(PressureShooterRules::Frames.back()-PressureShooterRules::FirstFrame)/PressureShooterRules::FramesPerSecond);
            if(u.body.x-plants[target].x<2*kContact) healingDemand[target]+=u.biteDps*remaining;
        }
    }
	int selected = -1;
	float best = 0;
	for (size_t i = 0; i < state.construction.size(); ++i) {
		const auto& card = state.construction[i]; const auto& p = card.plant;
		if(card.requiredBaseKind>=0 || card.cob.damage>0) continue; // 双底材配方在下方有界比较，不冒充普通空格落种。
		if (lightingOnly && !p.plantern) continue;
		if(!ConstructionSupportsAlive(card,plants)) continue;
		if (PlantingBlocked(blocks,p.row,p.column,time)) continue;
		const int quota = card.quotaGroup >= 0 ? card.quotaGroup : card.source;
		if ((card.remainingUses >= 0 && uses[quota] == 0) || time < ready[card.source]
			|| card.sunCost > sun || PlayerIceCost(state,time,card.iceCost) > ice) continue;
		// 累计剩余次数不是同时在场名额，旧株存活时不能用补种额度扩军。
		if (card.simultaneousLimit >= 0 && elites >= card.simultaneousLimit) continue;
		// 曙光莲正式限制是同时一株；死亡后才可再次建设，不能按卡槽数量复制名额。
		if (card.strike.damage > 0 && hasStrike) continue;
		// 路灯花同样是同时唯一，持有卡牌不等于已经照亮全场。
        if(p.plantern && hasLamp) continue;
        // 当前合法格也可能被上次假想建设占用；南瓜壳与宿主分别占层。
		const int cell=cellIndex(p);
		if(cell>=0 ? (occupied[cell] & (1u<<p.layer))!=0 : std::any_of(plants.begin(),plants.end(),[&](const auto& existing) {
			return existing.health > 0 && existing.row == p.row && existing.column == p.column && existing.layer == p.layer;
		})) continue;
		if(p.layer==2 && std::any_of(plants.begin(),plants.end(),[&](const Plant& existing) {
			return existing.health>0 && existing.layer==1 && existing.footprintColumns>1 && OccupiesPlantCell(existing,p.row,p.column);
		})) continue;
		if (p.x+kContact >= nearest[p.row]) continue;
		const float remaining = horizon-time;
		float value = p.dps*remaining*(0.2f+threat[p.row]/(1000+threat[p.row]));
		value += p.sunPerSecond*std::max(0.0f,remaining-card.firstSunDelay);
        if(p.cotton) {
            // 补建棉花按邻近伤口和实际火线估值，不能把无输出支援一律当作前排肉盾。
            float demand=0;
            for(size_t j=0;j<plants.size();++j) {
                const auto& ally=plants[j];
                if(ally.health<=0 || std::abs(ally.row-p.row)>1 || std::abs(ally.column-p.column)>1
                    || (ally.row==p.row && ally.column==p.column)) continue;
                demand+=healingDemand[j];
            }
            value+=std::min(demand,std::floor(remaining/MendingCottonRules::Interval)*MendingCottonRules::Amount);
        }
		if (card.strike.damage > 0) {
			float targets = 0;
			for (float hp : threat) targets += std::min(hp,card.strike.damage);
			value += targets*std::max(0.0f,remaining-card.strike.ready)/std::max(1.0f,card.strike.recharge);
		}
		if(p.plantern && fogAlpha) {
            const auto gear=ResponseLightGear(planternResponseGear,p);
            const float duration=std::min(remaining,p.lightFuel/std::max(.001f,PlanternRules::BurnRate(gear,PlanternRules::Scarcity(true,state.stationWave,0))));
            // 灯要照亮射击目标所在区域，不是照到射手就视为恢复全行火力；未知敌人不提供精确选点。
            for(const auto& ally:plants) if(ally.health>0 && ally.dps>0) {
                float coverage=0; int cells=0;
                for(int r=0;r<state.rows;++r) if(std::abs(r-ally.row)<=ally.rowRadius)
                    for(int c=0;c<state.columns;++c) {
                        const float x=state.gridLeft+(c+.5f)*state.cellWidth;
                        if(x<ally.x || x>ally.x+ally.range) continue;
                        ++cells;
                        if((*fogAlpha)[r*state.columns+c]>96) coverage+=PlanternRules::Illumination(gear,r-p.row,c-p.column);
                    }
                value+=ally.dps*duration*coverage/std::max(1,cells);
            }
        }
        if (p.dps <= 0 && p.sunPerSecond <= 0 && card.strike.damage <= 0 && !p.plantern && !p.cotton)
			value += std::min(p.health,threat[p.row])*(0.25f+fire[p.row]/50)
				/ (1+std::max(0.0f,nearest[p.row]-p.x)/200);
		else value /= 1+0.15f*p.column; // 输出和生产在后方合法位置有更长的存活机会
		value /= std::max(25,card.sunCost)+PlayerIceCost(state,time,card.iceCost);
		if (value > best) { best = value; selected = static_cast<int>(i); }
	}
	CobRecipePlan recipe;
	if(!lightingOnly) {
		recipe=SelectCobRecipe(state,time,horizon,plants,units,ready,blocks,fogAlpha,sun,ice,pendingIce);
		if(recipe.value>best) {
			selected=recipe.next;
			const auto& next=state.construction[selected];
			// 选择攒下一步的完整建设响应，不先花掉随后需要的同一钱包；绝不预支收入。
			if(time<ready[next.source] || sun<next.sunCost || ice<PlayerIceCost(state,time,next.iceCost)) return;
		}
	}
	if (selected < 0) return;
	const auto& card = state.construction[selected];
	auto plant = card.plant;
	if(card.requiredBaseKind>=0) {
		const auto upgrade=InspectCobRecipe(state,selected,time,plants,ready,blocks);
		if(upgrade.upgrade<0 || upgrade.next!=selected || upgrade.bases[0]<0 || upgrade.bases[1]<0) return;
		float transferred=0;
		for(int index:upgrade.bases) {
			auto& base=plants[index];
			transferred+=base.assetValue*std::clamp(base.health/std::max(1.0f,base.initialHealth),0.0f,1.0f);
			ConsumeForecastPlant(base); // 自愿升级直接消费底材，不能走僵尸伤害/击杀返冰入口。
		}
		plant.assetValue+=transferred; stats.cobTransferredAssets+=transferred; ++stats.cobUpgrades;
	}
	// 假想补种也要加入所在格的处决组，尤其是新南瓜必须和旧宿主合并计血。
	if (state.weatherStation) {
		plant.executionGroup = plant.row*state.columns+plant.column;
		plant.countsExecution = plant.layer == 1 || plant.layer == 2;
		plant.diesExecution = plant.layer > 0;
	}
	plant.id = NextConstructionPlantID(plants);
	plant.initialHealth = plant.health;
    plant.attackAssetCredit=0;plant.attackAssetInitialHealth=plant.health;
	plant.productionAt = time+card.firstSunDelay;
	plants.push_back(plant);
	ReactToDeployment(time,plant,units);
	const int quota = card.quotaGroup >= 0 ? card.quotaGroup : card.source;
	if (card.remainingUses >= 0 && uses[quota] > 0) --uses[quota];
	ready[card.source] = time+std::max(kConstructionInterval,card.recharge);
	sun -= card.sunCost; ice -= PlayerIceCost(state,time,card.iceCost);
	++stats.planted; stats.sunSpent += card.sunCost; stats.iceSpent += PlayerIceCost(state,time,card.iceCost);
	if (card.strike.damage > 0) {
		auto strike = card.strike; strike.plantID = plant.id;
		strikes.push_back(strike); strikeReady.push_back(time+strike.ready);
	}
	if(card.cob.damage>0) RegisterConstructedCob(state,card,plant,time,counters,counterReady,counterHoldUntil,shovelReady);
}

bool ValidWeights(const Weights& weights) {
	return std::all_of(weights.begin(), weights.end(), [](float w) { return std::isfinite(w) && std::abs(w) <= 500; });
}

/** 当前存活领域附近是否有能向已入场敌人开火的植物，不读取未来玩家操作。 */
static bool AuraHasTarget(const Snapshot& state, float time, const Plant& source,
	const std::vector<Plant>& plants, const std::vector<Unit>& units) {
	for (const auto& p : plants) {
		if (p.health <= 0 || p.dps <= 0 || std::abs(p.row-source.row)>1 || std::abs(p.column-source.column)>1) continue;
		for (const auto& u : units) if (u.body.health > 0 && u.body.spawnAt <= time && u.body.x <= state.rightEdge
			&& std::abs(u.body.row-p.row) <= p.rowRadius
			&& (p.around ? std::abs(u.body.x-p.x) <= p.range : u.body.x >= p.x-30 && u.body.x <= p.x+p.range)) return true;
	}
	return false;
}

/** 按真实卡价、独立冷却与空格周转预测玩家续航；订单付款、运输、到货分别结算。 */
static void AdvancePlayerEconomy(const Snapshot& state, const std::vector<Counter>& counters, float time, const std::vector<Plant>& plants,
	std::vector<float>& exchangeReady, std::vector<std::pair<std::array<int,2>,float>>& occupied,
	const std::vector<float>& counterReady, const std::vector<float>& constructionReady,
	const std::vector<int>& constructionUses,
	const std::vector<AttackAura>& auras, const std::vector<Unit>& units,
	float& sun, float& ice, float& pendingIce, float& arrival, ConstructionStats& stats, bool reserveCounterSpace, bool preserveManualAuras,
	std::vector<PlantingBlock>& blocks, bool rebuildCounterLight, bool preservePaidDefenses, const std::array<float,54>* fogAlpha) {
	occupied.erase(std::remove_if(occupied.begin(),occupied.end(),[&](const auto& p) { return p.second <= time; }),occupied.end());
	float neededIce = 0;
	float delivery = 0;
	for (const auto& order : state.shop) delivery = std::max(delivery,order.delivery);
	for (size_t i = 0; i < state.exchanges.size(); ++i) {
		const auto& card = state.exchanges[i];
		if (card.sunGain <= 0 || PlayerIceCost(state,time,card.iceCost) < 0 || card.cells.empty()) continue;
		for (const auto& cell : card.cells) {
			if (PlantingBlocked(blocks,cell[0],cell[1],time)) continue;
			if (std::any_of(plants.begin(),plants.end(),[&](const auto& p) {
				return p.health > 0 && p.layer == 1 && OccupiesPlantCell(p,cell[0],cell[1]);
			}) || std::any_of(occupied.begin(),occupied.end(),[&](const auto& p) { return p.first == cell; })) continue;
			if (exchangeReady[i] <= time+delivery) neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,card.iceCost)));
			if (exchangeReady[i] > time || ice < PlayerIceCost(state,time,card.iceCost) || sun >= state.playerSunLimit) break;
			const float gain = std::min(static_cast<float>(card.sunGain),std::max(0.0f,state.playerSunLimit-sun));
			sun += gain; ice -= PlayerIceCost(state,time,card.iceCost);
			exchangeReady[i] = time+std::max(kEconomyClearSeconds,card.recharge);
			occupied.push_back({cell,time+kEconomyClearSeconds});
			blocks.push_back({cell[0],cell[1],time+kEconomyClearSeconds});
			++stats.exchanges; stats.exchangeSun += gain; stats.exchangeIce += PlayerIceCost(state,time,card.iceCost);
			break;
		}
	}
	for (const auto& card : counters) {
		if(preservePaidDefenses && (card.sunCost>0 || card.iceCost>0)) continue;
		if (PlantingBlocked(blocks,card.cellRow,card.cellColumn,time+delivery)) continue;
		if (card.blast.committed || counterReady[card.source] > time+delivery
			|| (card.sharedSource >= 0 && counterReady[card.sharedSource] > time+delivery)) continue;
		if (card.plantID != 0 && std::none_of(plants.begin(),plants.end(),[&](const auto& p) {
			return p.id == card.plantID && p.health > 0;
		})) continue;
		neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,card.iceCost)));
	}
	for(const auto& wind:state.windCounters) if(!wind.committed && wind.iceCost>0 && wind.ready<=time+delivery
        && std::any_of(units.begin(),units.end(),[&](const Unit& u){return u.body.health>0 && u.body.spawnAt<=time && u.balloon.present && u.balloon.phase==BalloonRules::Phase::FLYING;}))
        neededIce=std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,wind.iceCost)));
    for (const auto& card : state.construction) {
        if (reserveCounterSpace && (!rebuildCounterLight || !card.plant.plantern)) continue;
		if(card.requiredBaseKind>=0 || card.cob.damage>0) continue;
		if (card.plant.plantern && std::any_of(plants.begin(),plants.end(),[](const Plant& p) {
			return p.plantern && p.health>0;
		})) continue;
		if (!ConstructionCellsFree(card,plants,blocks,time+delivery)) continue;
		const int quota = card.quotaGroup >= 0 ? card.quotaGroup : card.source;
		// 名额耗尽后不能继续为不存在的补菇采购冰块，否则会虚构对方资源损耗。
		if (card.remainingUses >= 0 && constructionUses[quota] == 0) continue;
		if (constructionReady[card.source] <= time+delivery)
			neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,card.iceCost)));
	}
	float constructionReservedSun=0;
	// 不建设世界完全不为炮配方订货；尚无底材/合法卡槽的升级也不能凭空制造需求。
	if(!reserveCounterSpace && pendingIce<=0 && std::any_of(state.construction.begin(),state.construction.end(),[&](const Construction& card) {
		return card.cob.damage>0 && ice<PlayerIceCost(state,time,card.iceCost);
	})) {
		const auto recipe=SelectCobRecipe(state,time,Horizon(state),plants,units,constructionReady,blocks,fogAlpha,sun,ice,pendingIce);
		if(recipe.next>=0) {
			const auto& next=state.construction[recipe.next];
			if(constructionReady[next.source]<=time+delivery && sun>=next.sunCost) {
				neededIce=std::max(neededIce,PlayerIceCost(state,time,next.iceCost));
				constructionReservedSun=static_cast<float>(next.sunCost);
			}
		}
	}
	// 已部署的付费能力也能形成订冰需求；来源消失或手动模式没有受益目标时不凭空补货。
	for (const auto& aura : auras) if ((!preserveManualAuras || aura.automatic)
        && aura.active+aura.cooldown <= delivery && aura.blockedUntil <= time+delivery) {
		const auto source = std::find_if(plants.begin(),plants.end(),[&](const Plant& p) { return p.id == aura.plantID && p.health > 0; });
		if (source != plants.end() && (aura.automatic || AuraHasTarget(state,time,*source,plants,units)))
			neededIce = std::max(neededIce,static_cast<float>(PlayerIceCost(state,time,aura.iceCost)));
	}
	for (const auto& p : plants) if (p.health > 0 && p.repairMaximum-p.health >= p.repairAmount
		&& p.repairRemaining <= delivery && p.repairBlockedUntil <= time+delivery)
		neededIce = std::max(neededIce,PlayerIceCost(state,time,p.repairCost));
	if (pendingIce > 0 || ice >= neededIce || ice >= state.playerIceLimit) return;
	// 留灰烬落点的独立应对也要留得住其阳光；不能先为别的高冰价工具订货，
	// 再把原本可支付的灰烬误判成没钱。只保留一张当前就绪且阳光够用的牌，
	// 冷却/来源死亡/落点封锁仍按同一世界检查；使用后下一步自然解除这笔预留。
	float reservedSun=constructionReservedSun, cheapestCounter=std::numeric_limits<float>::max();
	if (reserveCounterSpace && !preservePaidDefenses) for (const auto& card : counters) {
		if (card.blast.committed || card.sunCost<=0 || card.sunCost>sun || counterReady[card.source]>time
			|| (card.sharedSource>=0 && counterReady[card.sharedSource]>time)
			|| PlantingBlocked(blocks,card.cellRow,card.cellColumn,time)) continue;
		if (card.plantID!=0 && std::none_of(plants.begin(),plants.end(),[&](const Plant& p) {
			return p.id==card.plantID && p.health>0 && p.shutdownUntil<=time;
		})) continue;
		if (card.plantID==0 && std::any_of(plants.begin(),plants.end(),[&](const Plant& p) {
			return p.health>0 && p.layer==1 && OccupiesPlantCell(p,card.cellRow,card.cellColumn);
		})) continue;
		const float price=card.sunCost*state.sunIceValue+PlayerIceCost(state,time,card.iceCost);
		if(price<cheapestCounter) { cheapestCounter=price; reservedSun=static_cast<float>(card.sunCost); }
	}
	const ShopOrder* selected = nullptr;
	for (const auto& order : state.shop) {
		if (order.sunCost <= 0 || order.iceGain <= 0 || order.delivery < 0 || sun-reservedSun < order.sunCost
			|| time+order.delivery >= Horizon(state)) continue;
		if (!selected || order.sunCost/static_cast<float>(order.iceGain) < selected->sunCost/static_cast<float>(selected->iceGain)) selected = &order;
	}
	if (!selected) return;
	sun -= selected->sunCost; pendingIce = static_cast<float>(selected->iceGain); arrival = time+selected->delivery;
	++stats.orders; stats.orderSun += selected->sunCost; stats.orderIce += selected->iceGain;
}

bool StateModel::IsValid() const {
	return std::all_of(coefficients.begin(),coefficients.end(),[](const auto& row) { return ValidWeights(row); });
}

StateFeatures DescribeState(const Snapshot& s, const Weights& baseline) {
	StateFeatures f{};
	// 这些仅是输入单位归一化，不对应强制进攻阈值；系数正负和组合由实战训练选择。
	f[0] = std::log1p(std::max(0,s.budget)) / std::log(101.0f);
	f[1] = baseline[4] / 100;
	for (const auto& p : s.plants) if (p.health > 0) { f[2] += p.dps / 300; f[3] += p.sunPerSecond / 10; }
	std::vector<int> sources;
	for (const auto& c : s.counters) {
		if (c.blast.committed || c.blast.ready > kMaxDelay || c.sunCost > s.playerSun || c.iceCost > s.playerIce) continue;
		if (std::find(sources.begin(),sources.end(),c.source) == sources.end()) sources.push_back(c.source);
	}
	f[4] = static_cast<float>(sources.size()) / 3;
	for (const auto& strike : s.rowStrikes) if (strike.ready <= kMaxDelay) f[4] += 1.0f / 3;
	f[5] = s.noProgressSeconds / 90;
	for (auto& value : f) value = std::clamp(value,0.0f,3.0f);
	return f;
}

Weights ConditionWeights(const Weights& base, const StateFeatures& inputs, const StateModel* model) {
	auto result = base;
	if (model) for (int j = 0; j < FeatureCount; ++j) {
		for (int i = 0; i < StateFeatureCount; ++i) result[j] += inputs[i] * model->coefficients[i][j];
		result[j] = std::clamp(result[j],-500.0f,500.0f);
	}
	return result;
}

CapitalUtilityInputs DescribeCapitalUtility(const Snapshot& state) {
	CapitalUtilityInputs inputs;
	inputs.budget=state.budget; inputs.capacity=state.capacity; inputs.recoveryReserve=state.recoveryReserve;
	inputs.weatherStation=state.weatherStation;
	for(const auto& option:state.options) if(option.device<0 && option.cost>0 && option.cost<=PurchaseBudget(state))
		inputs.highestAffordableTroopCost=std::max(inputs.highestAffordableTroopCost,option.cost);
	if(state.fundableUnlockTroopCost>0 && state.fundableUnlockTroopCost<=state.budget)
		inputs.fundableUnlockTroopCost=state.fundableUnlockTroopCost;
	return inputs;
}

float CapitalUtilityScale(const Snapshot& state) {
	if(!state.netEconomy || state.budget<=0) return 1;
	const auto inputs=DescribeCapitalUtility(state);
	const int highestCost=std::max(inputs.highestAffordableTroopCost,inputs.fundableUnlockTroopCost);
	const int fullCapacity=std::max(0,std::min(state.capacity+(state.weatherStation ? 3 : 0),kPortfolioActions+(state.weatherStation ? 3 : 0)));
	if(highestCost<=0 || fullCapacity<=0) return 1;
	// 廉价开局不等于长期资本充裕：已经能支付解锁路径时，后续可投入的兵价也进入储备尺度。
	// 这只改变估值；当前可购名单、付款钱包及解锁条件仍分别校验，不能借尚未兑现的收入。
	// 完整容量不随本轮小队/整队搜索阶段变化，队列重排和新采购因此使用同一现金尺度。
	const float reserve=highestCost*static_cast<float>(fullCapacity)*kCapitalUtilityWaves+std::max(0,state.recoveryReserve);
	return std::clamp(std::pow(std::min(1.0f,reserve/state.budget),kCapitalUtilityExponent),kMinimumCashUtility,1.0f);
}

Weights AccountForIce(const Weights& conditioned,float utilityScale) {
	auto result = conditioned;
	// 同一种货币不能收入按高价、支出按低价；保留可训练的经济重要性和战术收益。
	const float scale=std::isfinite(utilityScale) ? std::clamp(utilityScale,kMinimumCashUtility,1.0f) : 1;
	const float value = std::clamp(std::abs(conditioned[4]),0.01f,500.0f)*scale;
	// 两项输入各自已经有界；合成后不能再次截断，否则击杀兑现的冰会与生产/支出不同价。
	result[0] = conditioned[0] + value; // 击杀既有破阵价值，也兑现冰收入
	result[3] = value * std::clamp(conditioned[3],0.0f,1.0f);
	result[4] = value;
	result[5] = -value;
	result[6] = -std::abs(conditioned[6]); // 爆区损失是风险成本，不能成为替代的采购奖励
	return result;
}

bool ProductionCalibration::IsValid() const {
	if (nodes.empty() || nodes.size() > 63) return false;
	for (size_t i = 0; i < nodes.size(); ++i) {
		const auto& n = nodes[i];
		if (!std::isfinite(n.value) || n.value < 0 || n.value > 1 || !std::isfinite(n.threshold)) return false;
		if (n.feature == -1) { if (n.left != -1 || n.right != -1) return false; }
		else if (n.feature < 0 || n.feature >= ProductionFeatureCount || n.left <= static_cast<int>(i)
			|| n.right <= static_cast<int>(i) || n.left >= static_cast<int>(nodes.size()) || n.right >= static_cast<int>(nodes.size())) return false;
	}
	return true;
}

float ProductionCalibration::Predict(const ProductionFeatures& features) const {
	int at = 0;
	for (size_t step = 0; step < nodes.size(); ++step) {
		const auto& node = nodes[at];
		if (node.feature == -1) return node.value;
		at = features[node.feature] <= node.threshold ? node.left : node.right;
	}
	return 1;
}

float ShieldProtectionFraction(const Unit& unit, const Plant& plant) {
	if (unit.shieldHealth <= 0 || plant.dps <= 0) return 1;
	const auto hit = DescribePlantHit(unit,plant);
	return hit.penetrate || hit.bypass ? std::clamp(1-hit.rate/plant.dps,0.0f,1.0f) : 1;
}

bool ShouldRegroup(const Result& result, int budget, int reserve) {
	// 纯狙击也会消耗本金；空购物车只有没有新增技能支出时才代表免费等待。
	if (budget >= reserve || (result.actions.empty() && result.features[5]<=result.baselineFeatures[5])) return false;
	const auto& plan = result.features;
	const auto& baseline = result.baselineFeatures;
	if (plan[2] > baseline[2]) return false;
	// 只比较买与不买的差异；新策略可计对方资源消耗，但支出偏好、单纯位移都不是回报。
	const float pressure = result.opponentScore > 0 ? result.baselineOpponentAssets-result.opponentAssets : 0;
	const float gain = (plan[0] - baseline[0]) + (plan[1] - baseline[1]) + (plan[4] - baseline[4]) + pressure;
	return gain < std::max(0.0f,plan[5]-baseline[5]) * kRecoveryReturnFraction;
}

float RemainingCapitalRisk(float fundedCapital, float currentCapital) {
	const float funded = std::max(0.0f,fundedCapital);
	return std::max(0.0f,funded*kCapitalLossFraction + currentCapital-funded);
}

bool ShouldConserveCapital(const Result& result, int budget, int reserve, float riskAllowance) {
	// 采购列表不含狙击目标，不能以没有士兵跳过同一钱包的累计亏损检查。
	if (result.actions.empty() && result.features[5]<=result.baselineFeatures[5]) return false;
	const auto& plan = result.features;
	const auto& baseline = result.baselineFeatures;
	if (plan[2] > baseline[2]) return false;
	const float spent = std::max(0.0f,plan[5]-baseline[5]);
	const float cash = plan[0]-baseline[0]+plan[4]-baseline[4];
	const float blastLoss = std::max(0.0f,plan[6]-baseline[6]);
	const float surviving = std::clamp(plan[3]-baseline[3],0.0f,spent);
	const float netLoss = std::max(0.0f,spent-cash-surviving);
	const float pressure = result.opponentScore > 0
		? std::max(0.0f,result.baselineOpponentAssets-result.opponentAssets) : 0;
	// 单轮小额诱饵不能无限重复享受豁免；已兑现净亏损会消耗试错额度，生产/击杀盈利可重新补回。
	// 有效前排按幸存资产保留价值，避免要求每个破阵准备步骤都立即现金盈利。
	// 历史亏损不能永久封死有利交换：小额进攻若让对方比等待时额外损失更多资产，仍值得比较。
	// 这只抵扣本案的试错风险，不增加己方钱包/累计额度；大额投资仍走下方交换回报检查。
	if (std::max(0.0f,netLoss-pressure) > riskAllowance && netLoss > spent*kCapitalLossFraction) return true;
	// 制冰和击杀才是可再投资的现金；已有部队收入、残存兵价与学到的偏好不能冒充新增现金。
	if (spent <= budget*kLargeInvestmentFraction && budget-spent >= reserve) return false;
	// 风险属于新增投资本身；用整个钱包作分母，会让富裕时的大额送死方案逃过交换回报检查。
	// 普通火力打光部队同样会耗尽本金，不能只查灰烬；幸存兵力仍可推进和保护后续生产。
	const float lostCapital = std::max(blastLoss,netLoss);
	// 进攻允许现金亏损：对方实际损失与仍存活的兵力同样是投入回报，不能强迫攻城先赚钱。
	// 这些都来自同窗完整推演；不把短暂削血、旧部队自然战果或探索进展加进资源账本。
	if (lostCapital > spent*kCapitalLossFraction && cash+pressure+surviving < spent) return true;
	return cash+plan[1]-baseline[1]+pressure < spent;
}

/** 推进一次性付费阶段；硬控只暂停预热，已经付款的爆发/恢复仍消耗游戏时间。 */
static std::array<float,2> AdvanceBurst(Unit& unit, bool inRange, float active, float& ice,
	Weights& features, ConstructionStats& stats) {
	auto& b = unit.burst;
	using Stage = PaidBurst::Stage;
	if (b.range <= 0) return {active,active};
	if (unit.body.health <= b.stopHealth) b.stage = Stage::SPENT;
	float wall = kStep, stopped = kStep-active;
	std::array<float,2> seconds{};
	// 至多经过预热、爆发、恢复；失败支付产生正重试间隔，不会在零时间反复扣款。
	while (wall > 0.0001f) {
		if (b.stage == Stage::READY && b.retryRemaining <= 0 && inRange && wall > stopped) {
			b.stage = Stage::WINDUP; b.remaining = b.windup;
		}
		float span = wall;
		if (b.stage == Stage::WINDUP) span = std::min(span,stopped+std::max(0.0f,b.remaining));
		else if (b.stage == Stage::ACTIVE || b.stage == Stage::RECOVERY) span = std::min(span,std::max(0.0f,b.remaining));
		else if (b.stage == Stage::READY && b.retryRemaining > 0) span = std::min(span,b.retryRemaining);
		const float free = std::max(0.0f,span-stopped);
		stopped = std::max(0.0f,stopped-span);
		if (b.stage == Stage::WINDUP) b.remaining -= free;
		else {
			seconds[0] += free*GoldenIceRules::Amplify(b.stage == Stage::ACTIVE ? b.moveMultiplier
				: b.stage == Stage::RECOVERY ? b.recoveryMoveMultiplier : 1,unit.goldenStacks);
			if (b.stage != Stage::RECOVERY) seconds[1] += free*(b.stage == Stage::ACTIVE ? b.biteMultiplier
				* GoldenIceRules::Amplify(b.moveMultiplier,unit.goldenStacks)/std::max(.001f,b.moveMultiplier) : 1);
			if (b.stage == Stage::ACTIVE || b.stage == Stage::RECOVERY) b.remaining -= span;
		}
		b.retryRemaining = std::max(0.0f,b.retryRemaining-span);
		wall -= span;
		if (b.remaining <= 0) {
			if (b.stage == Stage::WINDUP) {
				if (ice >= b.cost) {
					ice -= b.cost; features[5] += b.cost;
					stats.abilityIceSpent += b.cost; ++stats.burstActivations;
					b.stage = Stage::ACTIVE; b.remaining = b.duration;
				} else {
					b.stage = Stage::READY; b.retryRemaining = std::max(kStep,b.retry);
				}
			} else if (b.stage == Stage::ACTIVE) { b.stage = Stage::RECOVERY; b.remaining = b.recovery; }
			else if (b.stage == Stage::RECOVERY) b.stage = Stage::SPENT;
		}
	}
	return seconds;
}

/** 临时领域按来源存活、真实钱包与持续时间结算；手动模式只预测当前确有受益目标的释放。 */
static void AdvanceAttackAuras(const Snapshot& state, float time, std::vector<AttackAura>& auras,
	const std::vector<Plant>& plants, const std::vector<Unit>& units, float& ice,
	std::vector<float>& rates, ConstructionStats& stats, bool preserveManualAuras) {
	rates.assign(plants.size(),1);
	for (auto& aura : auras) {
		const auto source = std::find_if(plants.begin(),plants.end(),[&](const Plant& p) { return p.id == aura.plantID && p.health > 0; });
		if (source == plants.end()) continue;
		const bool threatened = !preserveManualAuras && !aura.automatic && AuraHasTarget(state,time,*source,plants,units);
		if (aura.active <= 0 && aura.cooldown <= 0 && time >= aura.blockedUntil
			&& (aura.automatic || threatened) && ice >= PlayerIceCost(state,time,aura.iceCost)) {
			ice -= PlayerIceCost(state,time,aura.iceCost); stats.iceSpent += PlayerIceCost(state,time,aura.iceCost); ++stats.auraActivations;
			aura.active = aura.duration;
		}
		const float boosted = std::min(kStep,aura.active);
		if (boosted > 0) {
			for (size_t i=0; i<plants.size(); ++i)
				if (std::abs(plants[i].row-source->row)<=1 && std::abs(plants[i].column-source->column)<=1)
					rates[i] += aura.bonus*boosted/kStep;
			aura.active -= boosted;
			if (aura.active <= 0) aura.cooldown = aura.recharge;
		}
		aura.cooldown = std::max(0.0f,aura.cooldown-(kStep-boosted));
	}
}

/** 一类盾只修剩余护甲；硬控暂停计时，满盾/缺钱均消耗本轮机会，不攒免费修复。 */
static void AdvanceArmorRepair(Unit& unit, float active, float& ice, Weights& features, ConstructionStats& stats) {
	auto& r = unit.repair;
	if (active <= 0 || r.interval <= 0 || r.health <= 0 || unit.body.health-r.health <= r.stopBodyHealth) return;
	r.remaining -= active;
	while (r.remaining <= 0) {
		r.remaining += r.interval;
		if (r.health >= r.maximum || ice < r.cost) continue;
		const float restored = std::min(r.amount,r.maximum-r.health);
		ice -= r.cost; features[5] += r.cost;
		r.health += restored; unit.body.health += restored;
		unit.helmHealth += restored;
		stats.abilityIceSpent += r.cost; stats.armorRepairIce += r.cost; ++stats.armorRepairs;
	}
}

/** 统一计入僵尸攻击的实际削血；回血会撤回已恢复的削血分，不能靠反复刷血赚分。 */
static bool DamagePlant(Plant& plant, float damage, bool crush, Weights& features, bool deploymentInterception = false) {
	if (plant.health <= 0 || plant.immuneRemaining > 0 || plant.burstProtection.invulnerable > 0 || damage <= 0
		|| (plant.deploymentInterceptionOnly && !deploymentInterception)) return false;
	if (crush) damage = plant.crushDamage > 0 ? plant.crushDamage : plant.health;
	const float credit = plant.reward*std::min(plant.health,damage)/std::max(1.0f,plant.initialHealth);
	features[1] += credit; plant.damageCredit += credit;
    plant.attackAssetCredit+=plant.assetValue*std::min(plant.health,damage)/AttackAssetHealth(plant);
	const float before = plant.health;
	plant.health = std::max(0.0f,plant.health-damage);
	if (plant.health <= 0) features[0] += plant.reward;
	else if (plant.hasBurstProtection) plant.burstProtection.RecordDamage(before-plant.health);
	return true;
}

/** 与正式投篮层序相同：宿主先于南瓜；已提交篮球按落格重新解析，不锁已消失的实体。 */
int BasketballLayer(int layer) { return layer==3 ? 4 : layer==1 ? 3 : layer==2 ? 2 : layer==0 ? 1 : 0; }

/** 只读一格最高投篮层，死亡/不可食层立即退出，不把南瓜冒充宿主的远程护盾。 */
int BasketballTargetAt(const std::vector<Plant>& plants,int row,int column) {
	int target=-1;
	for(size_t i=0;i<plants.size();++i) {
		const auto& p=plants[i]; if(p.health<=0 || !p.edible || !OccupiesPlantCell(p,row,column)) continue;
		if(target<0 || BasketballLayer(p.layer)>BasketballLayer(plants[target].layer)) target=static_cast<int>(i);
	}
	return target;
}

/** 按左到右格位选择正式目标；地刺占普通层时跳过整个格，不改为攻击其下方承载层。 */
int FindBasketballTarget(const Snapshot& state,const Unit& unit,const std::vector<Plant>& plants) {
	std::array<int,54> cells; cells.fill(-1);
	for(size_t i=0;i<plants.size();++i) {
		const auto& p=plants[i];
		if(p.health<=0 || !p.edible || p.row!=unit.body.row || p.column<0 || p.column>=state.columns
			|| p.column>=static_cast<int>(cells.size())) continue;
		for(int offset=0;offset<(p.layer==1 ? std::max(1,p.footprintColumns) : 1);++offset) {
			if(p.column+offset>=state.columns) break;
			auto& selected=cells[p.column+offset];
			if(selected<0 || BasketballLayer(p.layer)>BasketballLayer(plants[selected].layer)) selected=static_cast<int>(i);
		}
	}
	const float objectX=unit.body.x+unit.body.blastAnchorOffset;
	for(int column=0;column<state.columns && column<static_cast<int>(cells.size());++column) {
		const int target=cells[column];
		if(target>=0 && plants[target].catapultTargetable && objectX>=plants[target].x+CatapultRules::kMinimumTargetLead) return target;
	}
	return -1;
}

/** 已离膛篮球独立到达；逐发重查落格和叶子伞，来源被灰烬杀死也不回滚。 */
void AdvanceBasketballs(float time,std::vector<BasketballFlight>& flights,std::vector<Plant>& plants,
	Weights& features,ConstructionStats& stats) {
	for(auto it=flights.begin();it!=flights.end();) {
		if(it->at>time) { ++it; continue; }
		const int target=BasketballTargetAt(plants,it->row,it->column);
		if(target>=0) {
			const bool protectedCell=std::any_of(plants.begin(),plants.end(),[&](const Plant& p) {
				return p.health>0 && p.airborneDefenseRadius>=0 && std::abs(p.row-it->row)<=p.airborneDefenseRadius
					&& std::abs(p.column-it->column)<=p.airborneDefenseRadius;
			});
			if(protectedCell) ++stats.catapultBlocks;
			else if(it->at<plants[target].counterBlastAt && DamagePlant(plants[target],it->damage,false,features)) ++stats.catapultHits;
		}
		it=flights.erase(it);
	}
}

/** 投篮车停步也能碾压近身植物；位置沿用有限接触近似，类型和挡车承伤/推退仍走正式画像。 */
void AdvanceCatapultCrush(Unit& unit,float active,std::vector<Plant>& plants,Weights& features) {
	if(!unit.catapult.present || active<=0 || unit.catapult.phase==CatapultAttack::Phase::PUNCTURED) return;
	std::vector<size_t> nearby;
	for(size_t i=0;i<plants.size();++i) {
		const auto& p=plants[i];
		if(p.health>0 && p.catapultCrushable && p.row==unit.body.row && p.x<=unit.body.x+30
			&& unit.body.x<=p.x+kContact) nearby.push_back(i);
	}
	std::stable_sort(nearby.begin(),nearby.end(),[&](size_t a,size_t b){return plants[a].x>plants[b].x;});
	for(size_t i:nearby) {
		auto& p=plants[i];
		const bool damaged=DamagePlant(p,p.health,true,features);
		if(p.crushDamage>0 && p.health>0) {
			if(damaged) unit.body.x+=p.vehicleRetreat;
			break; // 活着的挡车植物阻止继续压同格后方，无敌期也不赠送穿格。
		}
	}
}

/** 推进投篮车停步射击/装填，返回本步是否禁止移动啃食；新瞄准只读当前活体目标。 */
bool AdvanceCatapult(const Snapshot& state,float time,Unit& unit,float active,bool slowed,float rainMultiplier,const std::vector<Plant>& plants,
	std::vector<BasketballFlight>& flights,ConstructionStats& stats) {
	auto& attack=unit.catapult;
	if(!attack.present) return false;
	using Phase=CatapultAttack::Phase;
	if(active<=0) return attack.phase!=Phase::WALKING;
	const float logicRate=slowed ? .5f : 1;
	if(attack.phase==Phase::PUNCTURED) {
		attack.remaining-=active*logicRate;
		if(attack.remaining<=0) unit.body.health=0;
		return true;
	}
	const auto beginShot=[&](int target) {
		attack.phase=Phase::SHOOTING; attack.targetColumn=plants[target].column; attack.launched=false;
		attack.releaseRemaining=attack.release; attack.remaining=attack.duration;
	};
	if(attack.phase==Phase::WALKING) {
		if(attack.ammunition<=0 || unit.body.x+unit.body.blastAnchorOffset
			>state.gridLeft+state.columns*state.cellWidth-CatapultRules::kShootStartInsideBoard) return false;
		const int target=FindBasketballTarget(state,unit,plants);
		if(target<0) return false;
		beginShot(target); return true;
	}
	if(attack.phase==Phase::RELOADING) {
		// 内部装填只受 scaledDelta 的减速/硬控影响；雨势和品种动画倍率不缩短这三秒。
		attack.remaining-=active*logicRate;
		if(attack.remaining<=0) {
			const int target=attack.ammunition>0 ? FindBasketballTarget(state,unit,plants) : -1;
			if(target>=0) beginShot(target); else attack.phase=Phase::WALKING;
		}
		return true;
	}
	const float animationRate=GoldenIceRules::Amplify(attack.animationBase,unit.goldenStacks)
		*GoldenIceRules::Amplify(slowed ? ZombieMovementRules::NormalSlowAnimationFactor : 1,unit.goldenStacks)
		*GoldenIceRules::Amplify(rainMultiplier,unit.goldenStacks);
	const float beforeRelease=attack.releaseRemaining;
	attack.releaseRemaining-=active*animationRate; attack.remaining-=active*animationRate;
	if(!attack.launched && attack.ammunition>0 && attack.releaseRemaining<=0 && active>0) {
		const float releaseAt=time+std::clamp(beforeRelease/std::max(.001f,animationRate),0.0f,kStep);
		flights.push_back({unit.body.row,attack.targetColumn,releaseAt+CatapultRules::kLobDuration,attack.damage});
		attack.launched=true; ++stats.catapultShots;
	}
	if(attack.remaining<=0) {
		if(attack.launched) attack.ammunition=std::max(0,attack.ammunition-1);
		attack.phase=attack.ammunition>0 && attack.launched ? Phase::RELOADING : Phase::WALKING;
		attack.remaining=attack.phase==Phase::RELOADING ? CatapultRules::kReloadSeconds : 0;
		attack.launched=false;
	}
	return true;
}

/** 劫持者直接处决并获得击杀冰；绕过普通砸击限伤、灰烬充能无敌和储冰坚果护体。 */
static void ExecutePlant(Plant& plant, Weights& features) {
	const float credit = plant.reward*plant.health/std::max(1.0f,plant.initialHealth);
	features[1] += credit; plant.damageCredit += credit;
    plant.attackAssetCredit+=plant.assetValue*plant.health/AttackAssetHealth(plant);
	features[0] += plant.reward; plant.health = 0;
}

/** 圆形爆区使用实际矩形最近点，不能把相邻行一律当成命中或漏掉边缘层。 */
bool BoxOverlapsPlant(const Plant& plant,float x,float y,float radius) {
    const float dx=x-std::clamp(x,plant.x+plant.boundsX,plant.x+plant.boundsX+plant.boundsWidth);
    const float dy=y-std::clamp(y,plant.y+plant.boundsY,plant.y+plant.boundsY+plant.boundsHeight);
    return dx*dx+dy*dy<=radius*radius;
}

/** 与正式九宫格保护者保持同样的格距和稳定择优；死亡壳不能继续替宿主吸收。 */
int BoxPumpkinProtector(const Snapshot& state,const std::vector<Plant>& plants,size_t target) {
    const auto& p=plants[target];
    int best=-1,distance=100000;
    for(size_t i=0;i<plants.size();++i) {
        const auto& shell=plants[i];
        if(!shell.pumpkin || shell.health<=0 || std::abs(p.row-shell.row)>state.pumpkinProtectionCells
            || std::abs(p.column-shell.column)>state.pumpkinProtectionCells || (p.pumpkin && i!=target)) continue;
        const int dr=p.row-shell.row,dc=p.column-shell.column,d=dr*dr+dc*dc;
        if(best<0 || d<distance || (d==distance && std::tie(shell.row,shell.column,shell.id)
            <std::tie(plants[best].row,plants[best].column,plants[best].id))) {best=static_cast<int>(i);distance=d;}
    }
    return best;
}

/** 先锁定承伤集合再扣血，保证同次范围伤害只扣每个保护壳一次，不溢给宿主。 */
std::vector<int> BoxPlantRecipients(const Snapshot& state,const std::vector<Plant>& plants,float x,float y) {
    std::vector<int> targets;
    for(size_t i=0;i<plants.size();++i) {
        const auto& p=plants[i];
        if(p.health<=0 || !BoxOverlapsPlant(p,x,y,JackBoxRules::BoxRadius)) continue;
        const int shell=BoxPumpkinProtector(state,plants,i);
        const int target=shell>=0 ? shell : static_cast<int>(i);
        if(std::find(targets.begin(),targets.end(),target)==targets.end()) targets.push_back(target);
    }
    return targets;
}

/** 有界贪心选点复用正式后排/产能估值；不在后台嵌套实体自己的Monte Carlo或消费游戏RNG。 */
int FindJackBoxTarget(const Snapshot& state,const Unit& unit,const std::vector<Plant>& plants) {
    std::array<bool,54> seen{};
    float best=-1; int cell=-1;
    for(const auto& p:plants) {
        const int key=p.row*state.columns+p.column;
        if(p.health<=0 || std::abs(p.row-unit.body.row)>1 || key<0 || key>=54 || p.column<0 || p.column>=state.columns || seen[key]) continue;
        seen[key]=true;
        const float x=state.gridLeft+(p.column+.5f)*state.cellWidth;
        const float y=ForecastRowY(state,p.row,x);
        float value=0;
        for(int target:BoxPlantRecipients(state,plants,x,y)) {
            const auto& affected=plants[target];
            value+=affected.targetValue*(affected.column<(state.columns+1)/2 ? JackBoxRules::BacklineValue : 1);
        }
        if(value>best || (value==best && (cell<0 || key<cell))) { best=value; cell=key; }
    }
    return cell;
}

/** 诊断并列采样只读稳定来源/投掷序号/格号；不读取时刻、未来实体ID或正式游戏随机流。 */
std::uint32_t DiagnosticJackTieKey(int ownerID,std::uint32_t ordinal,int cell) {
    auto hash=[](std::uint32_t value) {
        value^=value>>16; value*=0x7feb352du;
        value^=value>>15; value*=0x846ca68bu;
        return value^(value>>16);
    };
    return hash(hash(static_cast<std::uint32_t>(ownerID))
        ^hash(ordinal+0x9e3779b9u)^hash(static_cast<std::uint32_t>(cell)+0x85ebca6bu));
}

/** 显式消融只更换并列采样或使用边际伤害代理；默认路径保留上方原贪心，均非实体MC。 */
int FindDiagnosticJackBoxTarget(const Snapshot& state,const Unit& unit,const std::vector<Plant>& plants,std::uint32_t ordinal) {
    std::array<bool,54> seen{};
    std::array<float,54> values{};
    float best=-1; int cell=-1;
    for(const auto& p:plants) {
        const int key=p.row*state.columns+p.column;
        if(p.health<=0 || std::abs(p.row-unit.body.row)>1 || key<0 || key>=54 || p.column<0 || p.column>=state.columns || seen[key]) continue;
        seen[key]=true;
        const float x=state.gridLeft+(p.column+.5f)*state.cellWidth;
        const float y=ForecastRowY(state,p.row,x);
        float value=0;
        for(int target:BoxPlantRecipients(state,plants,x,y)) {
            const auto& affected=plants[target];
            float contribution=affected.targetValue*(affected.column<(state.columns+1)/2 ? JackBoxRules::BacklineValue : 1);
            if(state.diagnosticJackTargetMode==2) {
                // 只比较本盒的直接资产削减；不赠送补刀奖励、未来集火或MC长期协同。
                const float damage=JackBoxRules::BoxDamage*(affected.pumpkin ? state.pumpkinDamageMultiplier : 1);
                const float maximum=affected.maximumHealth>0 ? affected.maximumHealth : affected.initialHealth;
                contribution*=std::min(affected.health,damage)/std::max(1.0f,maximum);
            }
            value+=contribution;
        }
        values[key]=value;
        if(value>best || (value==best && (cell<0 || key<cell))) { best=value; cell=key; }
    }
    if(cell<0) return cell;
    constexpr float tieEpsilon=.001f; // 与实体贪心同量纲的并列容差；仅诊断模式使用
    int chosen=-1;
    std::uint32_t selectedKey=0;
    // 先求最高分再按格号遍历并列集合，使输入植物顺序不能替诊断样本选点。
    for(int key=0;key<static_cast<int>(seen.size());++key) if(seen[key] && std::abs(values[key]-best)<=tieEpsilon) {
        const auto tieKey=DiagnosticJackTieKey(unit.id,ordinal,key);
        if(chosen<0 || tieKey>selectedKey) {chosen=key;selectedKey=tieKey;}
    }
    return chosen;
}

/** 已离手的盒子独立计时；控场/死亡动画不暂停，灰烬真正移除载体会取消，回溯不复制。 */
void AdvanceJackBoxes(const Snapshot& state,float time,std::vector<JackBoxFlight>& boxes,
    std::vector<Unit>& units,std::vector<Plant>& plants,Weights& features,ConstructionStats& stats) {
    for(auto it=boxes.begin();it!=boxes.end();) {
        if(std::any_of(units.begin(),units.end(),[&](const Unit& owner){return owner.id==it->ownerID && owner.jack.flightsCancelled;})) {
            if(CanTraceCombat(state,stats)) {
                CombatForecastTrace event;event.event="jack_cancel";event.reason="source_ash_removed";
                event.at=time;event.attackID=it->traceID;event.sourceID=it->ownerID;event.x=it->x;event.y=it->y;
                event.resolveAt=it->at;AppendCombatTrace(state,stats,std::move(event));
            }
            it=boxes.erase(it); continue;
        }
        if(it->at>time) {++it;continue;}
        CombatForecastTrace event;
        const bool recordImpact=CanTraceCombat(state,stats);
        if(recordImpact) {
            event.event="jack_impact";event.at=time;event.attackID=it->traceID;event.sourceID=it->ownerID;
            event.x=it->x;event.y=it->y;event.resolveAt=it->at;
        }
        if(it->charmed) {
            for(auto& target:units) {
                const auto& body=target.body;
                if(body.health<=0 || body.spawnAt>time || target.id==it->ownerID) continue;
                const float dy=it->y-std::clamp(it->y,ForecastRowY(state,body.row,body.x)+target.boundsY,
                    ForecastRowY(state,body.row,body.x)+target.boundsY+target.boundsHeight);
                const float dx=it->x-std::clamp(it->x,body.x+body.boundsOffset,body.x+body.boundsOffset+body.boundsWidth);
                if(dx*dx+dy*dy<=JackBoxRules::BoxRadius*JackBoxRules::BoxRadius) {
                    auto hit=recordImpact ? UnitCombatHit(state,target,JackBoxRules::BoxDamage) : CombatForecastHit{};
                    ApplyDamage(target,JackBoxRules::BoxDamage,false,false,false,{},&units,&state,time);
                    if(recordImpact) {FinishUnitCombatHit(hit,target);event.targets.push_back(hit);}
                }
            }
        } else {
            for(int index:BoxPlantRecipients(state,plants,it->x,it->y)) {
                auto& p=plants[index];
                const float damage=JackBoxRules::BoxDamage*(p.pumpkin ? state.pumpkinDamageMultiplier : 1);
                auto hit=recordImpact ? PlantCombatHit(p,damage) : CombatForecastHit{};
                if(it->at<p.counterBlastAt && DamagePlant(p,damage,false,features)) ++stats.jackBoxHits;
                if(recordImpact) {
                    hit.after=hit.afterBody=p.health;hit.activeAfter=p.health>0;event.targets.push_back(hit);
                }
            }
        }
        if(recordImpact) AppendCombatTrace(state,stats,std::move(event));
        for(auto& owner:units) if(owner.id==it->ownerID && owner.body.health>0 && owner.jack.elite && owner.jack.remaining<0)
            owner.jack.remaining=(JackBoxRules::ThrowMin+JackBoxRules::ThrowMax)*.5f;
        it=boxes.erase(it);
    }
}

/** 普通开盒计时按游戏秒而非冰减速；开盒动作按动画推进。返回是否停步禁啃。 */
bool AdvanceJack(const Snapshot& state,float time,Unit& unit,float active,bool slowed,float rain,
    std::vector<Plant>& plants,std::vector<JackBoxFlight>& boxes,Weights& features,ConstructionStats& stats,
    std::uint32_t* diagnosticThrowOrdinal) {
    auto& jack=unit.jack;
    if(!jack.present || jack.phase==JackBoxRules::Forecast::Phase::DISARMED) return false;
    if(jack.elite) {
        if(unit.body.health-unit.helmHealth-unit.shieldHealth<=jack.stopHealth || active<=0 || jack.remaining<0) return false;
        jack.remaining-=active;
        if(jack.remaining>0) return false;
        const int cell=diagnosticThrowOrdinal ? FindDiagnosticJackBoxTarget(state,unit,plants,*diagnosticThrowOrdinal)
            : FindJackBoxTarget(state,unit,plants);
        if(cell<0) { jack.remaining=JackBoxRules::Retry; return false; }
        const float x=state.gridLeft+(cell%state.columns+.5f)*state.cellWidth;
        boxes.push_back({x,ForecastRowY(state,cell/state.columns,x),time+active+JackBoxRules::Flight,false,unit.id});
        if(CanTraceCombat(state,stats)) {
            boxes.back().traceID=++stats.nextCombatAttackID;
            CombatForecastTrace event;event.event="jack_throw";event.at=time+active;event.attackID=boxes.back().traceID;
            event.sourceID=unit.id;event.sourceRow=unit.body.row;event.targetRow=cell/state.columns;event.targetColumn=cell%state.columns;
            event.x=boxes.back().x;event.y=boxes.back().y;event.resolveAt=boxes.back().at;
            AppendCombatTrace(state,stats,std::move(event));
        }
        if(diagnosticThrowOrdinal) ++*diagnosticThrowOrdinal; // 已提交序号不随时间锚倒放，也不因重试改变样本。
        jack.remaining=-1; // 飞行期间不启动下一轮计时；落地后由独立队列恢复持盒。
        ++stats.jackThrows;
        return false;
    }
    using Phase=JackBoxRules::Forecast::Phase;
    if(jack.phase==Phase::RUNNING) {
        if(unit.body.health-unit.helmHealth-unit.shieldHealth<=jack.stopHealth) return false;
        jack.remaining-=active;
        if(jack.remaining>0 || active<=0) return false;
        jack.phase=Phase::POPPING; jack.releaseRemaining=jack.release;
        return true;
    }
    const float rate=GoldenIceRules::Amplify(jack.animationBase,unit.goldenStacks)
        *GoldenIceRules::Amplify(slowed ? ZombieMovementRules::NormalSlowAnimationFactor : 1,unit.goldenStacks)
        *GoldenIceRules::Amplify(rain,unit.goldenStacks);
    jack.releaseRemaining-=active*rate;
    if(jack.releaseRemaining<=0 && active>0) {
        const float x=unit.body.x+unit.body.boundsOffset+unit.body.boundsWidth*.5f;
        const float y=ForecastRowY(state,unit.body.row,unit.body.x)+unit.boundsY+unit.boundsHeight*.5f;
        for(auto& p:plants) if(p.health>0 && p.immuneRemaining<=0 && p.burstProtection.invulnerable<=0
            && !p.deploymentInterceptionOnly && BoxOverlapsPlant(p,x,y,JackBoxRules::PlantRadius)) ExecutePlant(p,features);
        unit.body.health=0; ++stats.jackExplosions;
    }
    return true;
}

/** 磁力按最近合法金属竞争，不凭空优先废除小丑；每株充能和已提交锚的剥离标记独立。 */
void AdvanceMagnets(const Snapshot& state,float time,std::vector<Plant>& plants,std::vector<Unit>& units,
    std::vector<TemporalAnchor>& anchors,ConstructionStats& stats,std::vector<LadderRules::Cell>& ladders) {
    for(auto& p:plants) {
        if(p.health<=0 || p.magnetRadius<=0 || p.shutdownUntil>time) continue;
        p.magnetRemaining=std::max(0.0f,p.magnetRemaining-kStep);
        if(p.magnetRemaining>0) continue;
        int best=-1; float bestScore=(std::numeric_limits<float>::max)();
        for(size_t i=0;i<units.size();++i) {
            const auto& u=units[i]; const auto& b=u.body;
            if(b.health<=0 || b.spawnAt>time || u.magneticLayer==0 || std::abs(b.row-p.row)>p.magnetRows
                || b.health-u.helmHealth-u.shieldHealth-(u.balloon.present ? u.balloon.health : 0)<=u.temporalStopHealth
                || b.x+b.boundsOffset>state.rightEdge || (u.magneticLayer==1 && u.helmHealth<=0)
                || (u.magneticLayer==2 && u.shieldHealth<=0)
                || (u.jack.present && u.jack.phase!=JackBoxRules::Forecast::Phase::RUNNING)
                || (u.digger.present && !u.digger.HasMagneticItem())) continue;
            const float y=ForecastRowY(state,b.row,b.x)+u.boundsY;
            const float dx=p.x-std::clamp(p.x,b.x+b.boundsOffset,b.x+b.boundsOffset+b.boundsWidth);
            const float dy=p.y-std::clamp(p.y,y,y+u.boundsHeight);
            const bool eating=std::any_of(plants.begin(),plants.end(),[&](const Plant& target) {
                return target.health>0 && target.edible && target.row==b.row && std::abs(target.x-b.x)<=kContact;
            });
            const float radius=eating ? p.magnetEatingRadius : p.magnetRadius;
            if(dx*dx+dy*dy>radius*radius) continue;
            const float centerX=b.x+b.boundsOffset+b.boundsWidth*.5f,centerY=y+u.boundsHeight*.5f;
            const float score=std::hypot(centerX-p.x,centerY-p.y)+std::abs(b.row-p.row)*p.magnetRowPenalty;
            if(score<bestScore) {best=static_cast<int>(i);bestScore=score;}
        }
        if(best<0) {
            if(ExtractForecastLadder(p,ladders,stats)) p.magnetRemaining=p.magnetRecharge;
            continue;
        }
        auto& u=units[best]; const int layer=u.magneticLayer;
        if(layer==1) {u.body.health-=u.helmHealth;u.helmHealth=u.maximumHelm=u.adaptiveHelmet=u.ritual.armor=0;}
        if(layer==2) {u.body.health-=u.shieldHealth;u.shieldHealth=u.maximumShield=0;}
        if(u.jack.present && !u.jack.elite) {
            u.jack.phase=JackBoxRules::Forecast::Phase::DISARMED; u.jack.remaining=0;
            u.body.speed=u.jack.disarmedSpeed;
        }
        if(u.digger.present) u.digger.LosePickaxe();
        u.magneticLayer=0; p.magnetRemaining=p.magnetRecharge; ++stats.magneticExtractions;
        for(auto& anchor:anchors) for(auto& target:anchor.targets) if(target.unit==best) {
            if(layer==2) target.restoreShield=false;
            else {target.restoreHelm=false;target.restoreAbility=false;}
        }
        // 绝缘胸甲反噬绕过南瓜及护体；它不是僵尸击杀奖励。
        p.attackAssetCredit+=p.assetValue*std::min(p.health,u.magneticBacklash)/AttackAssetHealth(p);
        p.health=std::max(0.0f,p.health-u.magneticBacklash);
        if(p.magneticPulseRadius>0) for(auto& target:units) {
            if(target.body.health<=0 || target.body.spawnAt>time || !target.paralysisAllowed) continue;
            const float dx=target.body.x-u.body.x,dy=ForecastRowY(state,target.body.row,target.body.x)-ForecastRowY(state,u.body.row,u.body.x);
            if(dx*dx+dy*dy<=p.magneticPulseRadius*p.magneticPulseRadius)
                target.body.stopped=std::max(target.body.stopped,p.magneticPulseParalysis);
        }
    }
}

/** 三叶草沿真实卡槽付款并延迟吹风；既有演出依赖来源，吹飞不触发工程师灰烬保护。 */
void AdvanceWindCounters(const Snapshot& state,float time,std::vector<WindCounter>& winds,
    std::vector<Plant>& plants,std::vector<Unit>& units,float& sun,float& ice,ConstructionStats& stats) {
    for(auto& wind:winds) {
        if(wind.committed || wind.plantID>0) {
            const auto source=std::find_if(plants.begin(),plants.end(),[&](const Plant& p){return p.id==wind.plantID && p.health>0;});
            if(source==plants.end()) {
                if(wind.plantID>0) wind.ready=Horizon(state)+1;
                else {wind.plantID=0;wind.committed=false;wind.ready=std::max(time,wind.nextReady);}
                continue;
            }
            // 停机暂停动画事件，必须保存剩余前摇，不能在恢复当刻补发过期吹风。
            if(source->shutdownUntil>time) {
                wind.ready+=std::min(kStep,source->shutdownUntil-time); continue;
            }
        }
        if(wind.ready>time) continue;
        if(!wind.committed && wind.plantID==0) {
            const bool target=std::any_of(units.begin(),units.end(),[&](const Unit& u){return u.body.health>0 && u.body.spawnAt<=time
                && u.balloon.present && u.balloon.phase==BalloonRules::Phase::FLYING && u.body.x<=state.rightEdge;});
            if(!target || sun<wind.sunCost || ice<PlayerIceCost(state,time,wind.iceCost)
                 ) continue;
            const auto cell=std::find_if(wind.cells.begin(),wind.cells.end(),[&](const auto& cell) {
                return std::none_of(plants.begin(),plants.end(),[&](const Plant& p){return p.health>0 && p.layer==1 && OccupiesPlantCell(p,cell[0],cell[1]);});
            });
            if(cell==wind.cells.end()) continue;
            auto plant=wind.deployment;
            plant.attackAssetCredit=0;plant.attackAssetInitialHealth=plant.health;
            plant.row=(*cell)[0]; plant.column=(*cell)[1]; plant.x=state.gridLeft+(plant.column+.5f)*state.cellWidth;
            plant.y=ForecastRowY(state,plant.row,plant.x); plant.id=-300000-static_cast<int>(plants.size());
            wind.plantID=plant.id; wind.committed=true; plants.push_back(plant);
            sun-=wind.sunCost; const float cost=PlayerIceCost(state,time,wind.iceCost); ice-=cost;
            stats.sunSpent+=wind.sunCost; stats.iceSpent+=cost;
            wind.nextReady=time+wind.recharge; wind.ready=time+wind.windup;
            continue;
        }
        for(auto& u:units) if(u.body.health>0 && u.body.spawnAt<=time) u.balloon.BeginBlow(wind.house);
        for(auto& p:plants) if(p.id==wind.plantID) ConsumeForecastPlant(p);
        if(wind.plantID>0) wind.ready=Horizon(state)+1;
        else {wind.committed=false; wind.plantID=0; wind.ready=std::max(time,wind.nextReady);}
    }
}

/** 推进装填/停步瞄准与已出膛弹道；按沿途最近格及南瓜优先结算，不穿透前墙。 */
static void AdvanceDeploymentSnipers(const Snapshot& state, float time, std::vector<Unit>& units,
	std::vector<Plant>& plants, std::vector<DeploymentPulse>& pulses, std::vector<float>& activity,
	Weights& features, ConstructionStats& stats) {
	activity.assign(units.size(),1);
	for (size_t i=0; i<units.size(); ++i) {
		auto& unit = units[i]; auto& sniper = unit.sniper;
		if (!sniper.enabled || unit.body.spawnAt > time) continue;
		if (unit.body.health <= sniper.stopHealth) { sniper.enabled = false; continue; }
		const float active = std::max(0.0f,kStep-unit.body.stopped);
		const float rate = unit.body.slow > 0 ? .5f : 1;
		if (!sniper.aiming) { sniper.remaining = std::max(0.0f,sniper.remaining-active*rate); continue; }
		const float stopped = std::min(active,sniper.remaining/rate);
		activity[i] = active > 0 ? (active-stopped)/active : 0;
		sniper.remaining -= active*rate;
		if (sniper.remaining > 0) continue;
		pulses.push_back({unit.body.row,sniper.targetID,unit.body.x+sniper.muzzleOffset,
			sniper.targetX,sniper.damage,time+stopped});
		++stats.deploymentShots;
		sniper.aiming = false; sniper.remaining = ThermalSniperRules::Reload;
	}
	for (auto it=pulses.begin(); it!=pulses.end();) {
		if (it->launchedAt > time+kStep) { ++it; continue; }
		const float span = std::min(kStep,time+kStep-it->launchedAt);
		const float direction = it->endX >= it->x ? 1 : -1;
		const float next = it->x+direction*std::min(std::abs(it->endX-it->x),ThermalSniperRules::PulseSpeed*span);
		Plant* hit = nullptr;
		float distance = 0;
		for (auto& plant : plants) {
			if (plant.health <= 0 || plant.row != it->row || plant.layer > 2) continue;
			if (plant.id == it->targetID && next != it->endX) continue;
			const float low = plant.x-state.cellWidth*.5f, high = plant.x+state.cellWidth*.5f;
			if (std::max(it->x,next) < low || std::min(it->x,next) > high) continue;
			const float d = std::max(0.0f,direction > 0 ? low-it->x : it->x-high);
			const float travel = plant.id == it->targetID ? std::abs(it->endX-it->x) : d;
			const float hitAt = std::max(time,it->launchedAt)+travel/ThermalSniperRules::PulseSpeed;
			// 弹道按本步内的实际到达先后裁决，不能因先遍历狙击就撤销更早的爆炸。
			if (hitAt >= plant.counterBlastAt) continue;
			const bool mirrorFirst = hit && plant.hostileMirrors > 0 && hit->hostileMirrors <= 0;
			const bool sameKind = hit && (plant.hostileMirrors > 0) == (hit->hostileMirrors > 0);
			if (!hit || d < distance || (d == distance && (mirrorFirst || (sameKind && plant.layer > hit->layer)))) {
				hit = &plant; distance = d;
			}
		}
		if (hit) {
			if (hit->hostileMirrors > 0) --hit->hostileMirrors;
			else { DamagePlant(*hit,it->damage,false,features,hit->id == it->targetID); ++stats.deploymentHits; }
		}
		it->x = next;
		if (hit || next == it->endX) it = pulses.erase(it);
		else { it->launchedAt = time+kStep; ++it; }
	}
}

/** 棉花按最低生命比例逐株治疗，撤回实际回血对应的削血收益，不产生额外钱包。 */
static void AdvanceCottonHealing(float time,std::vector<Plant>& plants,Weights& features) {
    for(auto& source:plants) {
        if(!source.cotton || source.health<=0 || source.shutdownUntil>time) continue;
        source.cottonRemaining=std::max(0.0f,source.cottonRemaining-kStep);
        if(source.cottonRemaining>0) continue;
        Plant* best=nullptr;
        for(auto& p:plants) {
            if(p.health<=0 || p.health>=p.maximumHealth || p.maximumHealth<=0
                || std::abs(p.row-source.row)>1 || std::abs(p.column-source.column)>1
                || (p.row==source.row && p.column==source.column)) continue;
            if(!best || p.health*best->maximumHealth<best->health*p.maximumHealth
                || (p.health*best->maximumHealth==best->health*p.maximumHealth && p.id<best->id)) best=&p;
        }
        if(!best) continue;
        const float healed=std::min(float(MendingCottonRules::Amount),best->maximumHealth-best->health);
        best->health+=healed;source.cottonRemaining=MendingCottonRules::Interval;
        const float credit=std::min(best->damageCredit,best->reward*healed/std::max(1.0f,best->initialHealth));
        best->damageCredit-=credit;features[1]-=credit;
        best->attackAssetCredit=std::max(0.0f,best->attackAssetCredit
            -best->assetValue*healed/AttackAssetHealth(*best));
    }
}

/** 四发逐次提交，并保留本步内出膛时间；前排/镜片各自消费一颗，来源死亡不撤销。 */
static void AdvancePressureShooters(const Snapshot& s,float time,std::vector<Unit>& units,
    std::vector<Plant>& plants,std::vector<PressureRay>& rays,Weights& features,float rain) {
    for(auto& unit:units) {
        const auto& body=unit.body;
        if(!unit.pressure || body.health<=unit.pressureStopHealth || body.spawnAt>time+kStep) continue;
        const float start=std::max(time,body.spawnAt);
        const float active=std::max(0.0f,time+kStep-start-body.stopped);
        const float rate=GoldenIceRules::Amplify(rain>=0 ? rain : unit.rawRainMultiplier,unit.goldenStacks)
            *GoldenIceRules::Amplify(body.slow>0 ? .6f : 1,unit.goldenStacks);
        if(rate<=0 || active<=0) continue;
        float remaining=active*rate, elapsed=0;
        while(remaining>=unit.pressureRemaining) {
            elapsed+=unit.pressureRemaining;remaining-=unit.pressureRemaining;
            rays.push_back({body.row,body.x+unit.pressureMuzzle,start+(time+kStep-start-active)+elapsed/rate});
            ++unit.pressureShot;
            if(unit.pressureShot==4) {
                unit.pressureShot=0;unit.pressureRemaining=PressureShooterRules::Reload+10/PressureShooterRules::FramesPerSecond;
            } else unit.pressureRemaining=(PressureShooterRules::Frames[unit.pressureShot]-PressureShooterRules::Frames[unit.pressureShot-1])/PressureShooterRules::FramesPerSecond;
        }
        unit.pressureRemaining-=remaining;
    }
    for(auto it=rays.begin();it!=rays.end();) {
        const float duration=std::clamp(time+kStep-it->launchedAt,0.0f,kStep);
        const float next=it->x-PressureShooterRules::ProjectileSpeed*duration;
        Plant* hit=nullptr;float best=1e9f;
        for(auto& p:plants) {
            if(p.health<=0 || p.row!=it->row || p.layer>2) continue;
            const float left=p.x-s.cellWidth*.5f,right=p.x+s.cellWidth*.5f;
            if(it->x<left || next>right) continue;
            const float d=std::max(0.0f,it->x-right);
            if(std::max(time,it->launchedAt)+d/PressureShooterRules::ProjectileSpeed>=p.counterBlastAt) continue;
            const bool mirrorFirst=hit && p.hostileMirrors>0 && hit->hostileMirrors<=0;
            const bool sameKind=hit && (p.hostileMirrors>0)==(hit->hostileMirrors>0);
            if(!hit || d<best || (d==best && (mirrorFirst || (sameKind && p.layer>hit->layer)))) {hit=&p;best=d;}
        }
        if(hit) {
            if(hit->hostileMirrors>0) --hit->hostileMirrors;
            else DamagePlant(*hit,it->damage,false,features);
        }
        it->x=next;it->launchedAt=time+kStep;
        if(hit || next<s.gridLeft-60) it=rays.erase(it);else ++it;
    }
}

#include "ColdStorageRainForecast.h"

/** 推进已部署/预测新建坚果的独立计时与付费修复，不能复活或在无敌期重复触发承伤。 */
static void AdvancePlantRepairs(const Snapshot& state, float time, std::vector<Plant>& plants, const std::vector<Unit>& units,
	float& ice, Weights& features, ConstructionStats& stats, bool automaticPhase) {
	for (auto& p : plants) if (p.health > 0 && p.repairMaximum > 0) {
		if (automaticPhase) {
			p.immuneRemaining = std::max(0.0f,p.immuneRemaining-kStep);
			if (p.hasBurstProtection) p.burstProtection.Advance(kStep);
			p.repairRemaining = std::max(0.0f,p.repairRemaining-kStep);
		}
		if (p.repairAutomatic != automaticPhase) continue;
		const float missing = p.repairMaximum-p.health;
		if (missing <= 0 || p.repairRemaining > 0 || time < p.repairBlockedUntil || ice < PlayerIceCost(state,time,p.repairCost)) continue;
		// 手动修复也能应对后排投篮；仍排在灰烬反制后，不能提前花掉救命钱。
		const bool threatened = !p.repairAutomatic && std::any_of(units.begin(),units.end(),[&](const Unit& u) {
			return u.body.health > 0 && u.body.spawnAt <= time && u.body.row == p.row
				&& (((!u.digger.present || u.digger.CanEat()) && std::abs(u.body.x-p.x) <= 2*kContact)
					|| (u.catapult.present && u.catapult.ammunition>0 && u.body.x>=p.x));
		});
		if (!p.repairAutomatic && !threatened) continue;
		if (missing < p.repairAmount && (p.repairAutomatic || p.health > p.repairAmount)) continue;
		const float restored = std::min(missing,p.repairAmount);
		p.health += restored; ice -= PlayerIceCost(state,time,p.repairCost); p.repairRemaining = p.repairRecharge;
		const float credit = std::min(p.damageCredit,p.reward*restored/std::max(1.0f,p.initialHealth));
		p.damageCredit -= credit; features[1] -= credit;
        p.attackAssetCredit=std::max(0.0f,p.attackAssetCredit-p.assetValue*restored/AttackAssetHealth(p));
		stats.iceSpent += PlayerIceCost(state,time,p.repairCost); stats.plantRepairIce += PlayerIceCost(state,time,p.repairCost); ++stats.plantRepairs;
	}
}

/** 推进铺路/到期并按当前位置重算活车叠层；来源消失后只剩一层持久场，不影响中性速度。 */
static void AdvanceGoldenIce(const Snapshot& s, float time, std::vector<Unit>& units,
	const std::vector<size_t>& sources, std::array<GoldenTrail,6>& trails, ConstructionStats& stats) {
	for (auto& trail:trails) {
		trail.remaining=std::max(0.0f,trail.remaining-kStep);
		if (trail.remaining<=0) trail.left=s.goldenRightX;
	}
	for (size_t index:sources) {
		auto& source=units[index]; const auto& body=source.body;
		if (body.health<=0 || body.spawnAt>time) continue;
		const float front=std::max(s.goldenLeftLimit,body.x+body.blastAnchorOffset+source.goldenDrive.frontOffset);
		source.goldenDrive.trailLeft=std::min(source.goldenDrive.trailLeft,front);
		for (int row=std::max(0,body.row-1);row<=std::min(5,body.row+1);++row) {
			if (!s.goldenAllowedRows[row]) continue;
			trails[row].left=std::min(trails[row].left,front);
			trails[row].remaining=GoldenIceRules::TrailDuration;
		}
	}
	for (auto& target:units) {
		target.goldenStacks=0;
		const auto& body=target.body;
		if (body.health<=0 || body.spawnAt>time || body.row<0 || body.row>=6) continue;
		const float x=body.x+body.blastAnchorOffset;
		if (s.goldenAllowedRows[body.row] && x<=s.goldenRightX) for (size_t index:sources) {
			const auto& source=units[index];
			if (source.body.health<=0 || source.body.spawnAt>time || std::abs(source.body.row-body.row)>1) continue;
			const float left=target.goldenDrive.enabled ? std::min(source.goldenDrive.trailLeft,
				source.body.x+source.body.blastAnchorOffset-GoldenIceRules::BodyPadding) : source.goldenDrive.trailLeft;
			if (x>=left) ++target.goldenStacks;
		}
		const bool residual=target.goldenStacks==0 && trails[body.row].remaining>0
			&& s.goldenAllowedRows[body.row] && x>=trails[body.row].left && x<=s.goldenRightX;
		if (target.goldenStacks==0 && (target.goldenDrive.enabled || residual)) target.goldenStacks=1;
		target.goldenStacks=std::min(target.goldenStacks,GoldenIceRules::MaxStacks);
		stats.goldenMaxStacks=std::max(stats.goldenMaxStacks,target.goldenStacks);
		if (residual) ++stats.goldenResidualSteps;
	}
}

/** 已提交鼓舞按墙钟到期；敲鼓按未硬控且受冰减速的时间推进，前摇不移动或啃食。 */
static void AdvanceDrums(const Snapshot& s, float time, std::vector<Unit>& units,
	ConstructionStats& stats, std::vector<float>& activity) {
	using namespace CrystalDrummerRules;
	activity.assign(units.size(),1);
	for (auto& unit : units) {
		for (auto& layer : unit.inspiration) layer.second -= kStep;
		unit.inspiration.erase(std::remove_if(unit.inspiration.begin(),unit.inspiration.end(),
			[](const auto& layer) { return layer.second <= 0; }),unit.inspiration.end());
	}
	const auto column = [&](const Unit& u) {
		return static_cast<int>(std::floor((u.body.x+u.body.blastAnchorOffset-s.gridLeft)/s.cellWidth));
	};
	for (size_t i=0; i<units.size(); ++i) {
		auto& source = units[i]; auto& drum = source.drum;
		if (!drum.enabled || source.body.health <= drum.stopHealth || source.body.spawnAt > time) continue;
		const float active = std::max(0.0f,kStep-source.body.stopped);
		const float slow = source.body.slow > 0 ? .5f : 1;
		float available = active*slow, walking = 0;
		while (available > 0) {
			const float consumed = std::min(available,std::max(0.0f,drum.remaining));
			if (!drum.winding) walking += consumed;
			available -= consumed; drum.remaining -= consumed;
			if (drum.remaining > 0) break;
			if (!drum.winding) { drum.winding = true; drum.remaining = Windup; continue; }
			drum.winding = false; drum.remaining = BeatInterval-Windup;
			++stats.drumBeats;
			const int sc = column(source);
			if (sc < 0 || sc >= s.columns || source.body.row < 0 || source.body.row >= s.rows) continue;
			for (auto& target : units) {
				if (&target == &source || target.body.health <= 0 || target.body.spawnAt > time) continue;
				const int tc = column(target);
				if (tc < 0 || tc >= s.columns || target.body.row < 0 || target.body.row >= s.rows
					|| std::abs(sc-tc)+std::abs(source.body.row-target.body.row) > Range) continue;
				auto layer = std::find_if(target.inspiration.begin(),target.inspiration.end(),
					[&](const auto& item) { return item.first == source.id; });
				if (layer == target.inspiration.end()) target.inspiration.emplace_back(source.id,Duration);
				else layer->second = Duration;
				++stats.drumRecipients;
			}
		}
		activity[i] = active > 0 ? walking/(active*slow) : 0;
	}
}

/** 已提交裂隙按到场次序消耗界碑碎片；来源是否还活着不影响事务。 */
static void AdvanceRiftArrivals(const Snapshot& s, float time, std::vector<Plant>& plants,
	std::vector<Unit>& units, std::vector<int>& columns, ConstructionStats& stats) {
	for (auto& p : plants) if (p.health > 0 && p.boundaryRecharge > 0 && time >= p.boundaryBlockedUntil
		&& p.boundaryShards < BoundaryFlowerRules::MaxShards) {
		p.boundaryCharge += kStep;
		if (p.boundaryCharge >= p.boundaryRecharge) { ++p.boundaryShards; p.boundaryCharge -= p.boundaryRecharge; }
		if (p.boundaryShards == BoundaryFlowerRules::MaxShards) p.boundaryCharge = 0;
	}
	std::vector<size_t> ready;
	for (size_t i=0; i<units.size(); ++i) if (columns[i] >= 0 && units[i].body.spawnAt <= time) ready.push_back(i);
	std::stable_sort(ready.begin(),ready.end(),[&](size_t a,size_t b) {
		if (units[a].body.spawnAt != units[b].body.spawnAt) return units[a].body.spawnAt < units[b].body.spawnAt;
		return columns[a] < columns[b];
	});
	for (size_t index : ready) {
		const int row = units[index].body.row, column = columns[index];
		Plant* selected = nullptr; int distance = 10000;
		for (auto& p : plants) if (p.health > 0 && p.boundaryShards > 0 && time >= p.boundaryBlockedUntil
			&& std::abs(p.row-row)<=1 && std::abs(p.column-column)<=1) {
			const int d = std::abs(p.row-row)+std::abs(p.column-column);
			if (!selected || d < distance || (d == distance && p.id < selected->id)) { selected = &p; distance = d; }
		}
		if (selected) { --selected->boundaryShards; units[index].body.x = s.rightEdge+40; ++stats.riftRedirects; }
		columns[index] = -1;
	}
}

/** 对齐正式最高层耐久优先/分路约束选格；免费召唤不生成成交资产或死亡返冰。 */
static void CommitForecastRitual(const Snapshot& s, float time, Unit& source, const std::vector<Plant>& plants,
	std::vector<Unit>& units, size_t& nextSlot, size_t slotEnd, std::vector<int>& columns,
	std::vector<float>& initialHealth, std::vector<float>& initialX, ConstructionStats& stats) {
	struct Cell { int row, column; float value; };
	std::vector<Cell> cells;
	for (const auto& cell : s.riftCells) {
		const Plant* top = nullptr;
		for (const auto& p : plants) if (p.health > 0 && p.row == cell[0] && p.column == cell[1]
			&& (!top || p.layer > top->layer)) top = &p;
		cells.push_back({cell[0],cell[1],top ? (top->maximumHealth > 0 ? top->maximumHealth : top->initialHealth)+1000 : 0});
	}
	std::stable_sort(cells.begin(),cells.end(),[](const Cell& a,const Cell& b) {
		if (a.value != b.value) return a.value > b.value;
		if (a.column != b.column) return a.column < b.column;
		return a.row < b.row;
	});
	const int count = s.ritualWhiteout ? AuroraPriestRules::WhiteoutSummons : AuroraPriestRules::Summons;
	std::array<int,6> rowCounts{};
	std::vector<Cell> chosen;
	for (int pass=0; pass<2; ++pass) for (const auto& cell : cells) {
		if (static_cast<int>(chosen.size()) >= count) break;
		if (cell.row < 0 || cell.row >= s.rows || rowCounts[cell.row] >= 2) continue;
		if (pass == 0 && chosen.size() == 1 && cell.row == chosen.front().row) continue;
		if (std::any_of(chosen.begin(),chosen.end(),[&](const Cell& c) { return c.row == cell.row && c.column == cell.column; })) continue;
		chosen.push_back(cell); ++rowCounts[cell.row];
	}
	if (chosen.empty()) return;
	++source.ritual.releases; ++stats.ritualReleases;
	for (size_t i=0; i<chosen.size() && nextSlot<slotEnd; ++i) {
		const auto& cell = chosen[i];
		// 活体来源沿用正式 ID；未出生来源使用稳定候选身份，无法预知未来实体分配顺序。
		const size_t kind = (std::abs(source.id)+i+cell.row+cell.column)%s.ritualSummons.size();
		auto child = s.ritualSummons[kind];
		child.id = -1-static_cast<int>(nextSlot);
		child.body.row = cell.row; child.body.x = s.gridLeft+(cell.column+.5f)*s.cellWidth;
		child.body.spawnAt = time+AuroraPriestRules::Unfold;
		child.body.purchaseCost = 0; child.playerRefund = 0;
		units[nextSlot] = std::move(child); columns[nextSlot] = cell.column;
		initialHealth[nextSlot] = units[nextSlot].body.health; initialX[nextSlot] = units[nextSlot].body.x;
		++nextSlot; ++stats.riftSummons;
	}
}

/** 仪器/头部存活才继续计时，场外只准备不施法；鼓舞不加速仪式。 */
static float AdvanceRitual(const Snapshot& s, float time, Unit& unit, float active, const std::vector<Plant>& plants,
	std::vector<Unit>& units, size_t& nextSlot, size_t slotEnd, std::vector<int>& columns,
	std::vector<float>& initialHealth, std::vector<float>& initialX, ConstructionStats& stats) {
	auto& r = unit.ritual;
	if (!r.enabled) return 1;
	if (r.armor <= 0 || unit.body.health-r.armor <= r.stopHealth || r.releases >= AuroraPriestRules::MaxReleases) {
		r.enabled = false; r.winding = false; return 1;
	}
	r.remaining = std::max(0.0f,r.remaining-active*(unit.body.slow > 0 ? .5f : 1));
	const bool wasWinding = r.winding;
	if (r.remaining <= 0) {
		if (r.winding) {
			CommitForecastRitual(s,time,unit,plants,units,nextSlot,slotEnd,columns,initialHealth,initialX,stats);
			r.winding = false; r.remaining = AuroraPriestRules::Cooldown;
		} else if (unit.body.x+unit.body.blastAnchorOffset <= s.gridLeft+s.columns*s.cellWidth) {
			r.winding = true; r.remaining = AuroraPriestRules::Windup;
		}
	}
	return wasWinding || r.winding ? 0 : 1;
}

/** 独立结算已提交锚；只回溯核心和明确可回溯的阶段，钱包、死亡返款及已提交召唤不倒放。 */
static void ResolveTemporalAnchors(const Snapshot& s, float time, std::vector<TemporalAnchor>& anchors,
	std::vector<Unit>& units, std::vector<Plant>& plants, const std::vector<float>& initialHealth,
	Weights& features, ConstructionStats& stats) {
	for (auto it=anchors.begin(); it!=anchors.end();) {
		if (it->at>time) { ++it; continue; }
		for (const auto& target:it->targets) {
			if (target.unit<0 || target.unit>=static_cast<int>(units.size())) continue;
			auto& unit=units[target.unit]; const auto& saved=target.saved;
			if (unit.temporalIrreversible || !saved.temporalEligible) continue;
			const bool revived=unit.body.health<=0;
			const float before=unit.body.health;
			const int column=std::clamp(static_cast<int>((saved.body.x+saved.body.blastAnchorOffset-s.gridLeft)/s.cellWidth),0,s.columns-1);
			Plant* boundary=nullptr; int distance=10000;
			for (auto& p:plants) if (p.health>0 && p.boundaryShards>0 && time>=p.boundaryBlockedUntil
				&& std::abs(p.row-saved.body.row)<=1 && std::abs(p.column-column)<=1) {
				const int d=std::abs(p.row-saved.body.row)+std::abs(p.column-column);
				if (!boundary || d<distance || (d==distance && p.id<boundary->id)) { boundary=&p; distance=d; }
			}
			if (!boundary || revived) {
				unit.body.row=saved.body.row;
				unit.body.x=boundary ? s.rightEdge+40 : saved.body.x;
			}
			if (boundary) { --boundary->boundaryShards; ++stats.clockRedirects; }
			// 首次死亡的返冰已经兑现，复活不重新登记；缺少能力快照的旧锚沿用出生状态。
            if (revived) {
				if(unit.digger.present) {
					unit.digger.phase=DiggerRules::Phase::TUNNELING;
					unit.digger.remaining=0; unit.digger.hasPickaxe=true;
				}
				// 死亡后重新创建的舞王没有旧伴舞关联；存活回溯则不倒退未纳入正式锚的舞步。
				if(unit.dance.leader) {
					unit.dance.phase=DancerRules::Forecast::Phase::ENTRY;
					unit.dance.remaining=DancerRules::EntrySeconds; unit.dance.followers.fill(0);
					unit.body.speed=unit.dance.entrySpeed;
				} else if(unit.dance.backup) {
					unit.dance.phase=DancerRules::Forecast::Phase::HOLD; unit.dance.remaining=DancerRules::HoldSeconds;
				}
				if(unit.jack.present) {
                    unit.jack.flightsCancelled=false; unit.jack.phase=JackBoxRules::Forecast::Phase::RUNNING;
                    unit.jack.remaining=unit.jack.elite ? (JackBoxRules::ThrowMin+JackBoxRules::ThrowMax)*.5f : JackBoxRules::BirthPopSeconds(.67f);
                    if(!target.restoreAbility && saved.jack.phase==JackBoxRules::Forecast::Phase::DISARMED) unit.jack.phase=JackBoxRules::Forecast::Phase::DISARMED;
                }
                if(unit.balloon.present) {
                    if(unit.balloon.phase==BalloonRules::Phase::WALKING) unit.boundsY-=BalloonRules::ColliderRise;
                    unit.balloon.phase=BalloonRules::Phase::FLYING; unit.balloon.health=unit.balloon.maximumHealth;
                    unit.balloon.blowing=false;unit.balloon.blowRemaining=0;unit.groundHazard=false;unit.temporalStopHealth=0;
                }
                if(unit.ferry.present) unit.ferry={true,true,false,FogFerryRules::FirstWait,unit.ferry.stopBodyHealth};
                if(unit.healer.present) { unit.healer=HealerRules::Forecast{}; unit.healer.present=unit.healer.enabled=true; unit.healer.disableBodyHealth=saved.healer.disableBodyHealth; }
                // 正式复活重新创建投篮车，恢复出生库存；存活回溯不改变当前弹药/射击阶段。
				if(unit.catapult.present) {
					unit.catapult.ammunition=CatapultRules::kInitialBasketballs;
					unit.catapult.phase=CatapultAttack::Phase::WALKING;
					unit.catapult.remaining=0; unit.catapult.launched=false; unit.catapult.targetColumn=-1;
				}
				unit.productionRemaining=IceProduction::Interval; unit.nextYield=IceProduction::InitialYield;
				unit.inspiration.clear();
				if (unit.sniper.enabled || saved.sniper.enabled) { unit.sniper=saved.sniper; unit.sniper.aiming=false; unit.sniper.remaining=ThermalSniperRules::Reload; }
				unit.burst.stage=PaidBurst::Stage::READY; unit.burst.remaining=unit.burst.retryRemaining=0;
				unit.repair.remaining=unit.repair.interval;
				unit.goldenDrive.undamaged=0; unit.goldenDrive.trailLeft=s.rightEdge;
				if (unit.clock.present && !target.restoreAbility)
					unit.clock={true,true,false,PolarClockRules::Preparation,unit.clock.stopBodyHealth};
			}
			const float helm=target.restoreHelm ? saved.helmHealth : revived ? 0 : unit.helmHealth;
			const float shield=target.restoreShield ? saved.shieldHealth : revived ? 0 : unit.shieldHealth;
			unit.body.health=saved.body.health-saved.helmHealth-saved.shieldHealth+helm+shield
                +(unit.balloon.present ? unit.balloon.health-saved.balloon.health : 0);
			unit.headless=saved.headless; // 正式核心回溯恢复hasHead，不受能力快照是否可恢复限制。
			if(revived) { unit.headlessBleedRemainder=0; unit.pressureWindow.hits.clear(); unit.pressureProtectorID=0; } // 重建实体没有旧流血余量；活体回溯沿用其本地计时。
			unit.helmHealth=helm; unit.shieldHealth=shield;
			if(unit.ladder.present) {
				unit.ladder.canPlace=saved.ladder.canPlace; // 核心回溯的头部资格，动作阶段仍不倒放。
				if(shield>0) {unit.maximumShield=saved.maximumShield;unit.magneticLayer=2;}
			}
			if(revived) {
				unit.ladderClimb.phase=LadderRules::Climb::Phase::NONE;
				unit.ladderClimb.altitude=0; unit.ladderClimb.usedColumn=-1;
				if(unit.ladder.present) {
					unit.ladder.phase=shield>0 ? LadderRules::Builder::Phase::CARRYING : LadderRules::Builder::Phase::NORMAL;
					unit.ladder.retain=false; // 新实体尚未再次完成精英扫描，不能继承旧无限搭梯资格。
					unit.body.speed=shield>0 ? unit.ladder.carryingSpeed : unit.ladder.normalSpeed;
					unit.ladder.row=unit.ladder.column=-1; unit.ladder.remaining=0;
				}
			}
			unit.body.slow=saved.body.slow; unit.body.stopped=saved.body.stopped;
			unit.adaptiveHelmet=target.restoreHelm ? saved.adaptiveHelmet : 0;
			unit.repair.health=std::min(unit.repair.maximum,helm); unit.ritual.armor=std::min(saved.ritual.armor,helm);
			if (target.restoreAbility) {
				unit.adaptedOrigin=saved.adaptedOrigin; unit.drum=saved.drum; unit.ritual=saved.ritual; unit.clock=saved.clock;
				unit.ritual.armor=std::min(saved.ritual.armor,helm);
				// 本地成熟度、付费阶段和修盾余时可回溯；已产出的冰与已扣技能费仍保留。
				unit.productionRemaining=saved.productionRemaining; unit.nextYield=saved.nextYield;
				// 正式急救员没有本地能力回溯覆写；活体治疗冷却/永久禁疗不能倒放。
				unit.burst=saved.burst; unit.repair.remaining=saved.repair.remaining;
			}
			if (unit.clock.present && helm<=0) unit.clock.enabled=false;
			const float restored=std::max(0.0f,unit.body.health-before);
			if(unit.body.economic && unit.body.health>unit.productionStopHealth)
				stats.workerProtectionProgress+=unit.body.purchaseCost*restored/std::max(1.0f,initialHealth[target.unit]);
			const float recovered=std::min(std::max(0.0f,unit.blastCredit-saved.blastCredit),
				unit.body.purchaseCost*restored/std::max(1.0f,initialHealth[target.unit]));
			features[6]-=recovered; unit.blastCredit-=recovered;
			++stats.clockRewinds; if (revived) ++stats.clockRevivals;
		}
		it=anchors.erase(it);
	}
}

/** 按独立阶段推进各钟匠并记录邻路最高威胁；已有锚的目标不可被重复记录，前摇暂停移动/啃食。 */
static void AdvanceClocks(const Snapshot& s, float time, std::vector<Unit>& units, const std::vector<size_t>& sources,
	std::vector<TemporalAnchor>& anchors, std::vector<float>& activity, ConstructionStats& stats, float blockedUntil) {
	activity.assign(units.size(),1);
	for (size_t index:sources) {
		auto& source=units[index]; auto& clock=source.clock;
		if (source.body.health<=0 || source.body.spawnAt>time) continue;
		if (source.helmHealth<=0 || source.body.health-source.helmHealth-source.shieldHealth<=clock.stopBodyHealth) clock.enabled=false;
		if (!clock.enabled) continue;
		const float active=std::max(0.0f,kStep-source.body.stopped), rate=source.body.slow>0 ? .5f : 1;
		float available=active*rate, walking=0, consumedTotal=0;
		while (available>0) {
			const float consumed=std::min(available,std::max(0.0f,clock.remaining));
			if (!clock.winding) walking+=consumed;
			available-=consumed; clock.remaining-=consumed; consumedTotal+=consumed;
			if (clock.remaining>0) break;
			if (!clock.winding) { clock.winding=true; clock.remaining=PolarClockRules::Windup; continue; }
			clock.winding=false; clock.remaining=PolarClockRules::Cooldown;
			if (time+consumedTotal/rate < blockedUntil) continue;
			if (std::any_of(anchors.begin(),anchors.end(),[&](const auto& anchor){return anchor.ownerID==source.id;})) continue;
			std::vector<size_t> candidates;
			for (size_t i=0;i<units.size();++i) {
				const auto& u=units[i];
				if (!u.temporalEligible || u.temporalIrreversible || u.body.spawnAt>time || std::abs(u.body.row-source.body.row)>1
					|| u.body.health-u.helmHealth-u.shieldHealth<=u.temporalStopHealth) continue;
				bool recorded=false;
				for (const auto& anchor:anchors) for (const auto& target:anchor.targets) recorded|=target.unit==static_cast<int>(i);
				if (!recorded) candidates.push_back(i);
			}
			std::stable_sort(candidates.begin(),candidates.end(),[&](size_t a,size_t b) {
				const auto score=[&](size_t i){return DawnLotusRules::ThreatScore(static_cast<long long>(units[i].body.health),units[i].body.x+units[i].body.blastAnchorOffset,s.rightEdge);};
				const auto sa=score(a),sb=score(b);
				const auto id=[&](size_t i){return units[i].id>0 ? static_cast<long long>(units[i].id) : 0x100000000LL+static_cast<long long>(i);};
				return sa!=sb ? sa>sb : id(a)<id(b);
			});
			if (candidates.empty()) continue;
			TemporalAnchor anchor; anchor.ownerID=source.id; anchor.at=time+consumedTotal/rate+PolarClockRules::AnchorDuration;
			for (size_t i:candidates) {
				if (anchor.targets.size()>=PolarClockRules::TargetLimit) break;
				anchor.targets.push_back({static_cast<int>(i),units[i],true,true,units[i].id!=source.id});
			}
			++stats.clockAnchors; stats.clockTargets+=static_cast<int>(anchor.targets.size()); anchors.push_back(std::move(anchor));
		}
		activity[index]=active>0 ? walking/(active*rate) : 0;
	}
}

/** 气象站在纯数值副本中推进；设备事务、雾区和雷荷只影响本次候选，不触碰Board。 */
using namespace NightRoofChargeRules;
struct StationProjection {
    WeatherStationRules::State state;
    float charge=0, overcharge=0, warning=0, jammed=0;
    float knownCharge=0, knownRate=0, lastObservation=0;
    int phase=0, row=-1, hijacker=-1, guide=-1;
    bool attempted=false, guided=false;
    std::array<float,54> fogAlpha{}, illumination{};
    explicit StationProjection(const Snapshot& s,const std::vector<Action>& plan):state(s.station),
        charge(s.stationCharge),overcharge(s.stationOvercharge),warning(s.stationWarning),jammed(s.stationJammed),
        knownCharge(s.stationJammed>0 ? 0 : s.stationCharge),phase(s.stationChargePhase),row(s.stationRow),
        attempted(s.stationSelectionAttempted),guided(s.stationGuided),fogAlpha(s.stationFogAlpha) {
        for(size_t i=0;i<s.current.size();++i) {
            if(s.current[i].id==s.stationHijackerID) hijacker=static_cast<int>(i);
            if(s.current[i].id==s.stationGuideID) guide=static_cast<int>(i);
        }
        for(const auto& a:plan) if(s.options[a.option].device>=0) {
            const auto& o=s.options[a.option]; auto& c=state.controls[o.device];
            c.pending=o.setting; c.warning=WeatherStationRules::WarningSeconds; c.player=false;
        }
    }
    int Rain() const { return state.controls[0].value; }
    /** 当前照明与浓度共用一个视野口径；近身感知仅放开索敌，不取消环境减速。 */
    bool Obscured(const Snapshot& s,const Unit& unit) const {
        const int col=std::clamp(static_cast<int>((unit.body.x-s.gridLeft)/s.cellWidth),0,s.columns-1);
        return fogAlpha[unit.body.row*s.columns+col]>96;
    }
    bool CanTarget(const Snapshot& s,const Plant& p,const Unit& unit) const {
        if(std::abs(p.row-unit.body.row)<=1 && std::abs(p.x-unit.body.x)<=100) return true;
        const int col=std::clamp(static_cast<int>((unit.body.x-s.gridLeft)/s.cellWidth),0,s.columns-1);
        if(fogAlpha[unit.body.row*s.columns+col]<=96) return true;
        const int adjacent=col+(unit.body.x>p.x ? -1 : 1);
        return adjacent>=0 && adjacent<s.columns && fogAlpha[unit.body.row*s.columns+adjacent]<=96;
    }
    bool HasPot(const Plant& p,const std::vector<Plant>& plants) const {
        return std::any_of(plants.begin(),plants.end(),[&](const Plant& q) {
            return q.lightningPot && q.health>0 && q.row==p.row && q.column==p.column;
        });
    }
    /** 锁定前比较所有普通行与存活接地候选；已公布的路线在预警期间不重选。 */
    void ChooseRoute(const Snapshot& s,float t,const std::vector<Plant>& plants,const std::vector<Unit>& units) {
        float best=-std::numeric_limits<float>::max(); row=0; guide=-1; guided=false;
        for(int candidate=0;candidate<s.rows+static_cast<int>(units.size());++candidate) {
            const int index=candidate-s.rows;
            if(index>=0 && (!units[index].grounding || units[index].helmHealth<=0 || units[index].body.health<=0 || units[index].body.spawnAt>t)) continue;
            const int candidateRow=index<0 ? candidate : units[index].body.row;
            float score=0;
            for(const auto& p:plants) if(p.health>0 && p.row==candidateRow && !p.support && !HasPot(p,plants)) {
                bool protectedPlant=false;
                for(const auto& q:plants) if(q.health>0 && q.grounding && q.row==p.row && std::abs(q.column-p.column)<=1) protectedPlant=true;
                if(!protectedPlant) score+=(p.dps*8+p.sunPerSecond*8*s.sunIceValue);
            }
            if(index<0) for(const auto& u:units) if(u.body.health>0 && u.body.spawnAt<=t && u.body.row==candidateRow && u.groundHazard
                && (!u.digger.present || u.digger.CanGroundHazard()))
                score-=std::min(static_cast<float>(kNightRoofZombieDamage),u.body.health)*u.body.value/std::max(1.0f,u.body.health);
            if(score>best) { best=score; row=candidateRow; guide=index; guided=index>=0; }
        }
    }
    /** 处决按释放瞬间生命冻结目标组，友伤、工人损失和施法者牺牲一并结算。 */
    void Discharge(const Snapshot& s,float t,std::vector<Plant>& plants,std::vector<Unit>& units,Weights& f) {
        if(hijacker>=0 && units[hijacker].body.health>units[hijacker].temporalStopHealth) {
            const float line=units[hijacker].body.health;
            std::array<float,54> health{}; std::array<bool,54> protectedGroup{};
            for(const auto& p:plants) if(p.health>0 && p.executionGroup>=0 && p.executionGroup<54) {
                if(p.countsExecution) health[p.executionGroup]+=p.health;
                if(HasPot(p,plants)) protectedGroup[p.executionGroup]=true;
            }
            for(auto& p:plants) if(p.health>0 && p.diesExecution && p.executionGroup>=0 && p.executionGroup<54
                && health[p.executionGroup]>0 && health[p.executionGroup]<=line && !protectedGroup[p.executionGroup]) ExecutePlant(p,f);
            for(auto& u:units) if(u.body.health>0 && u.body.spawnAt<=t && u.body.health<=line) { u.body.health=0; u.temporalIrreversible=true; }
            units[hijacker].body.health=0; units[hijacker].temporalIrreversible=true;
        }
        std::vector<size_t> grounding;
        float damageMultiplier=1;
        for(const auto& pot:plants) if(pot.health>0 && pot.lightningPot && pot.row==row)
            for(const auto& host:plants) if(host.health>0 && !host.support && host.row==row && host.column==pot.column) damageMultiplier=2;
        for(auto& p:plants) if(p.health>0 && p.row==row && !p.support && !HasPot(p,plants)) {
            int protector=-1, distance=100;
            for(size_t j=0;j<plants.size();++j) {
                const auto& q=plants[j]; const int delta=std::abs(q.column-p.column);
                if(q.health>0 && q.grounding && q.row==row && delta<=1 && delta<distance) { protector=static_cast<int>(j); distance=delta; }
            }
            if(protector>=0) grounding.push_back(protector);
            else { p.shutdownUntil=std::max(p.shutdownUntil,t+kNightRoofPlantShutdownDuration); p.repairBlockedUntil=std::max(p.repairBlockedUntil,t+kNightRoofPlantShutdownDuration); p.boundaryBlockedUntil=std::max(p.boundaryBlockedUntil,t+kNightRoofPlantShutdownDuration); }
        }
        if(guided && guide>=0 && units[guide].body.health>0 && units[guide].helmHealth>0) {
            for(auto& u:units) if(u.body.health>0 && u.body.spawnAt<=t) {
                const float dx=u.body.x-units[guide].body.x, dy=(u.body.row-units[guide].body.row)*s.cellHeight;
                if(dx*dx+dy*dy<=130*130) {
                    u.body.slow=0;
                    // 聚合停步可能包含不受接地免疫影响的麻痹，不能在这里全部清空。
                    u.body.slowImmunity=std::max(u.body.slowImmunity,t+30);
                    u.chargeControlImmunity=std::max(u.chargeControlImmunity,t+30);
                }
            }
        }
        if(!guided) for(auto& u:units) if(u.body.health>0 && u.body.spawnAt<=t && u.body.row==row && u.groundHazard
            && (!u.digger.present || u.digger.CanGroundHazard())) {
            Unit* protector=nullptr;
            const bool suppressed=std::any_of(plants.begin(),plants.end(),[&](const Plant& p) {
                return p.health>0 && p.grounding && p.row==u.body.row && std::abs(p.x-u.body.x)<=s.cellWidth*1.5f;
            });
            for(auto& other:units) if(!suppressed && other.insulator && other.helmHealth>0 && other.body.health>0 && other.body.spawnAt<=t
                && other.body.row==row && std::abs(other.body.x-u.body.x)<=s.cellWidth*1.5f
                && (!protector || std::abs(other.body.x-u.body.x)<std::abs(protector->body.x-u.body.x))) protector=&other;
            if(protector) { const float absorbed=std::min(protector->helmHealth,kNightRoofZombieDamage*damageMultiplier); protector->helmHealth-=absorbed; protector->body.health-=absorbed; protector->overloadRemaining=15; }
            else {
                ApplyDamage(u,kNightRoofZombieDamage*damageMultiplier,false,false,false,{},&units,&s,t);
                if(u.paralysisAllowed) {
                    u.body.stopped=std::max(u.body.stopped,kNightRoofZombieParalysisDuration);
                    // 电荷麻痹与雷鸣花共享状态；雷鸣花不能延长正在生效的电荷麻痹。
                    u.paralysisRemaining=std::max(u.paralysisRemaining,kNightRoofZombieParalysisDuration);
                }
            }
        }
        std::sort(grounding.begin(),grounding.end()); grounding.erase(std::unique(grounding.begin(),grounding.end()),grounding.end());
        for(const size_t i:grounding) DamagePlant(plants[i],100,true,f);
    }
    /** 只对真实存活单位推进充电/干扰；黑障内对手只能使用先前已观察的电量趋势。 */
    void Step(const Snapshot& s,float t,std::vector<Plant>& plants,std::vector<Unit>& units,Weights& f,float& playerIce,ConstructionStats& stats,int wave,bool clearFogWhenReady,int planternResponseGear,bool preservePaidDefenses) {
        if(!s.weatherStation) return;
        for(auto& c:state.controls) WeatherStationRules::Advance(c,kStep);
        jammed=std::max(0.0f,jammed-kStep);
        bool hasHijacker=false;
        for(auto& unit:units) if(unit.body.health>0 && unit.body.spawnAt<=t) {
            unit.overloadRemaining=std::max(0.0f,unit.overloadRemaining-kStep);
            if(unit.hijacker && unit.body.health>unit.temporalStopHealth) hasHijacker=true;
            if(unit.jammerReboot>0) {unit.jammerReboot=std::max(0.0f,unit.jammerReboot-std::max(0.0f,kStep-unit.body.stopped));continue;}
            if(unit.jammer && unit.body.health>unit.temporalStopHealth && unit.body.stopped<=0
                && unit.body.x+unit.body.boundsOffset+unit.body.boundsWidth<=s.gridLeft+s.columns*s.cellWidth) {
                unit.jammerChanneling=true;unit.jammerRemaining-=kStep; unit.body.stopped=std::max(unit.body.stopped,kStep);
                if(unit.jammerRemaining<=0) { jammed=std::min(300.0f,jammed+30); unit.jammer=false; ++stats.stationJams; }
            }
        }
        // 当前雾势本身可见，黑障只屏蔽预告。等待设备解锁且余额足够后付款，
        // 不读取隐藏预告抢先操作；与炸弹、商店和关雷荷共用同一个冰块钱包。
        auto& fogDevice=state.controls[WeatherStationRules::FOG];
        const int fogCost=WeatherStationRules::Cost(WeatherStationRules::FOG,0);
        if(clearFogWhenReady && playerIce>=fogCost
            && WeatherStationRules::CanChange(fogDevice,WeatherStationRules::FOG,0)) {
            fogDevice.pending=0; fogDevice.warning=WeatherStationRules::WarningSeconds; fogDevice.player=true;
            playerIce-=fogCost; stats.iceSpent+=fogCost; ++stats.stationFogCounters;
        }
        const int fog=state.controls[1].value;
        const int left=fog==0 ? s.columns : s.columns-3-fog;
        // 先投影存活光源，避免每个格子都重扫完整阵容；无路灯花时不做空的逐株查找。
        illumination.fill(0);
        for(auto& p:plants) if(p.plantern && p.health>0) {
            for(auto it=p.lightDeliveries.begin();it!=p.lightDeliveries.end();) {
                if(it->first<=t) { p.lightFuel=std::min(PlanternRules::FuelCapacity,p.lightFuel+it->second); it=p.lightDeliveries.erase(it); }
                else ++it;
            }
            // 只按已经出现的雾开灯，无雾关灯节油；不偷看暗区僵尸坐标或未来出生时间。
            // 除固定挡位外比较低油降挡、补油后升挡的完整响应，不能把整段推演
            // 锁在省油挡，漏掉灰烬回油后重新照见边路工人的风险。
            if(planternResponseGear>=0) {
                const auto gear=fog>0 ? ResponseLightGear(planternResponseGear,p) : PlanternGear::OFF;
                if(p.lightGear!=gear) {
                    p.lightGear=gear;
                    for(int r=0;r<s.rows;++r) for(int c=0;c<s.columns;++c)
                        p.illumination[r*s.columns+c]=PlanternRules::Illumination(gear,r-p.row,c-p.column);
                }
            }
            // 停机暂停植物更新和耗油，但正式照明仍由挡位、燃料与存活状态决定。
            if(p.shutdownUntil<=t) p.lightFuel=std::max(0.0f,p.lightFuel-PlanternRules::BurnRate(p.lightGear,
                PlanternRules::Scarcity(true,wave,0))*kStep);
            if(p.lightFuel>0 && p.lightGear!=PlanternGear::OFF)
                for(int cell=0;cell<s.rows*s.columns;++cell)
                    illumination[cell]=std::max(illumination[cell],p.illumination[cell]);
        }
        for(int r=0;r<s.rows;++r) for(int c=0;c<s.columns;++c) {
            const float light=illumination[r*s.columns+c];
            const float target=(c<left ? 0 : c==left ? (fog==4 ? 225.0f : 200.0f) : 255.0f)*(1-light);
            auto& alpha=fogAlpha[r*s.columns+c]; alpha+=std::clamp(target-alpha,-320*kStep,180*kStep);
        }
        const float rate=state.controls[2].value ? (Rain()==0 ? -kNightRoofChargeClearLeakPerSecond : (Rain()==1 ? kNightRoofChargeLightPerSecond : Rain()==2 ? kNightRoofChargeMediumPerSecond : kNightRoofChargeHeavyPerSecond)+(hasHijacker ? kNightRoofHijackerRainChargeBonusPerSecond : 0)) : 0;
        if(jammed<=0) { knownCharge=charge; knownRate=rate; lastObservation=t; }
        // 对手拥有正常预算和8秒切换前摇；黑障开始前在本次推演见过的信息可外推，
          // 快照已经处于黑障且没有历史观测时，不能把真实隐藏电量当作玩家已知。
        const float expectedCharge=std::clamp(knownCharge+knownRate*(t-lastObservation),0.0f,100.0f);
        auto& device=state.controls[2];
        if(!preservePaidDefenses && phase==0 && expectedCharge>=35 && playerIce>=40 && WeatherStationRules::CanChange(device,2,0)) {
            // 没有可执行的停机反制时，没必要在每个积分步做植物×花盆的保护查询。
            float exposedValue=0;
            for(const auto& p:plants) if(p.health>0 && !HasPot(p,plants)) exposedValue+=p.assetValue;
            if(exposedValue>80) {
                device.pending=0; device.warning=WeatherStationRules::WarningSeconds; device.player=true;
                playerIce-=40; stats.iceSpent+=40; ++stats.stationCounters;
            }
        }
        if(phase==0) {
            charge=std::clamp(charge+rate*kStep,0.0f,100.0f);
            if(charge>=kNightRoofHijackerLockThreshold && !attempted) {
                attempted=true; hijacker=-1;
                for(size_t i=0;i<units.size();++i) if(units[i].hijacker && units[i].body.spawnAt<=t && units[i].body.health>units[i].temporalStopHealth
                    && (hijacker<0 || units[i].body.health>units[hijacker].body.health)) hijacker=static_cast<int>(i);
                if(hijacker>=0 && !units[hijacker].hijackerBoosted) {
                    units[hijacker].body.health+=1000; units[hijacker].maximumBody+=1000; units[hijacker].temporalStopHealth+=1000.0f/3;
                    units[hijacker].hijackerBoosted=true;
                }
            }
            if(charge>=kNightRoofChargeMaximum) { phase=1; warning=hijacker>=0 ? kNightRoofHijackerWarningDuration : kNightRoofChargeWarningDuration; ChooseRoute(s,t,plants,units); }
        } else {
            overcharge=std::min(kNightRoofOverchargeMaximum,overcharge+std::max(0.0f,rate)*kStep);
            warning-=kStep;
            if(hijacker>=0 && units[hijacker].body.health<=units[hijacker].temporalStopHealth) hijacker=-1;
            if(hijacker>=0 && phase==1 && warning<=1) units[hijacker].body.stopped=std::max(units[hijacker].body.stopped,warning);
            if(warning<=0) {
                if(phase==1) { Discharge(s,t,plants,units,f); phase=2; warning=kNightRoofChargeDischargeDuration; ++stats.stationDischarges; }
                else { phase=0; charge=overcharge; overcharge=0; attempted=false; hijacker=-1; }
            }
        }
    }
};

/** Release 开启快速浮点时仍按 IEEE 位型拒绝 NaN/无穷，避免非法时刻越过付款门禁。 */
static bool FinitePhasedValue(float value) {
    return (std::bit_cast<std::uint32_t>(value)&0x7f800000u)!=0x7f800000u;
}

/** 后队只使用已开放、纯数值且无额外来源槽的能力，不猜测未建模复杂能力。 */
bool CanForecastFollowup(const Option& option) {
    const auto& u=option.unit;
    return option.device<0 && option.cost>0 && u.body.health>0 && FinitePhasedValue(u.body.health)
        && !u.engineer && !u.drum.enabled && !u.sniper.enabled && !u.ritual.present
        && !u.goldenDrive.enabled && !u.clock.present && !u.dance.leader && !u.dance.backup
        && u.throwHealth<=0 && !u.healer.present && !u.catapult.present && !u.jack.present
        && !u.balloon.present && !u.digger.present && !u.ladder.present && !u.jammer
        && !u.hijacker && !u.pressure && !u.floodMortar && !u.ferry.present;
}

/** 条件与成员必须全部合法；付款失败只能继续等待，不能先创建付不起的部分成员。 */
static bool ValidFollowup(const Snapshot& s,const FollowupPolicy& followup,std::int64_t& bill) {
    bill=0;
    if(followup.actions.empty() || followup.actions.size()>ColdStorageDeploymentRules::MaximumCapacity
        || !FinitePhasedValue(followup.earliestSeconds) || !FinitePhasedValue(followup.expirySeconds)
        || followup.earliestSeconds<0 || followup.expirySeconds<followup.earliestSeconds
        || followup.expirySeconds>300 || followup.maxReadyCobs< -1) return false;
    for(const auto& action:followup.actions) {
        if(action.option<0 || action.option>=static_cast<int>(s.options.size())
            || !FinitePhasedValue(action.delay) || action.delay<0 || action.delay>300) return false;
        const auto& option=s.options[action.option];
        if(option.row<0 || option.row>=s.rows || option.unit.body.row!=option.row || !CanForecastFollowup(option)) return false;
        bill+=option.cost;
        if(bill>(std::numeric_limits<int>::max)()) return false;
    }
    return true;
}

Weights Evaluate(const Snapshot& s, const std::vector<Action>& plan, ConstructionStats* construction, float counterHoldSeconds, float storedHoldSeconds, float rowStrikeHoldSeconds, bool reserveCounterSpace, bool clearFogWhenReady, int planternResponseGear, bool preserveManualAuras, bool shovelCounterSpace, bool preservePaidDefenses, bool interferenceBeforeAsh, const FollowupPolicy* followup) {
	if(s.precisionUnlockAfterPurchase && s.precisionTargetID>0 && !FuturePrecisionPlanPayable(s,plan)) {
		auto without=RawWorldCache::CopyView(s); without.precisionTargetID=0; without.precisionAdditionalTargetIDs.clear();
		return Evaluate(without,plan,construction,counterHoldSeconds,storedHoldSeconds,rowStrikeHoldSeconds,
			reserveCounterSpace,clearFogWhenReady,planternResponseGear,preserveManualAuras,shovelCounterSpace,
			preservePaidDefenses,interferenceBeforeAsh,followup);
	}
	Weights f{};
	f[5] = static_cast<float>(PrecisionCost(s));
	auto units = s.current;
    std::vector<int> combatActionIndices;
    if(s.traceCombat) combatActionIndices.assign(units.size(),-1);
	StationProjection environment(s,plan);
	std::vector<std::pair<size_t,int>> committedRifts;
	for (const auto& rift : s.rifts) {
		auto unit = rift.unit; unit.body.spawnAt = rift.remaining;
		unit.body.purchaseCost = 0; unit.playerRefund = 0;
		committedRifts.emplace_back(units.size(),rift.column); units.push_back(std::move(unit));
        if(s.traceCombat) combatActionIndices.push_back(-1);
	}
	for (size_t actionIndex=0;actionIndex<plan.size();++actionIndex) {
        const auto& a=plan[actionIndex];
		if (a.option < 0 || a.option >= static_cast<int>(s.options.size())) continue;
		if (s.options[a.option].device>=0) { f[5]+=s.options[a.option].cost; continue; }
		auto unit = s.options[a.option].unit;
		unit.body.spawnAt = a.delay;
		units.push_back(unit);
        if(s.traceCombat) combatActionIndices.push_back(static_cast<int>(actionIndex));
		f[5] += s.options[a.option].cost;
	}
	// 预分配未激活的子单位，后续不能因 vector 扩容使当前战斗引用失效。
	const size_t originalUnits = units.size();
	std::vector<int> thrownChild(originalUnits,-1);
	std::vector<float> throwRemaining(originalUnits,-1);
	if (FullForecast(s)) for (size_t i = 0; i < originalUnits && units.size() < originalUnits+kForecastSummonLimit; ++i) {
		if (units[i].throwHealth <= 0) continue;
		Unit child;
		child.maximumBody=ImpThrowRules::Health; child.temporalStopHealth=ImpThrowRules::Health/3;
		child.ladderClimb.eligible=true;
		child.ladderClimb.horizontalBoost=LadderRules::HorizontalBoost;
		child.body.row = units[i].body.row;
		child.body.spawnAt = Horizon(s)+1;
		child.body.speed = s.impWalkSpeed;
		thrownChild[i] = static_cast<int>(units.size());
		units.push_back(child); // 免费召唤既不增加付款资产，也不向玩家凭空返冰
	}
	// 伴舞与裂隙各自预留有界、永不复用的出生槽；旧时间锚不能误恢复后来补召的新实体。
	size_t nextDanceSlot=units.size();
	const size_t danceSlotEnd=nextDanceSlot+(std::any_of(units.begin(),units.end(),[](const Unit& u){return u.dance.leader;}) ? kForecastSummonLimit : 0);
	units.resize(danceSlotEnd);
	for(size_t i=nextDanceSlot;i<danceSlotEnd;++i) units[i].body.spawnAt=Horizon(s)+1;
	size_t nextRiftSlot = units.size();
	if (std::any_of(units.begin(),units.end(),[](const Unit& u) { return u.ritual.enabled; })) {
		int slots = 0;
		for (const auto& unit : units) if (unit.ritual.enabled)
			slots += std::max(0,AuroraPriestRules::MaxReleases-unit.ritual.releases)
				* (s.ritualWhiteout ? AuroraPriestRules::WhiteoutSummons : AuroraPriestRules::Summons);
		units.resize(units.size()+std::min(kForecastSummonLimit,slots));
		for (size_t i=nextRiftSlot; i<units.size(); ++i) { units[i].body.health = 0; units[i].body.spawnAt = Horizon(s)+1; }
	}
    std::int64_t followupBill=0;
    const bool followupValid=followup && ValidFollowup(s,*followup,followupBill);
    const size_t followupBegin=units.size();
    if(followupValid) {
        // 放在所有既有子槽之后，避免仅开启后队就改变裂隙/小鬼/伴舞的稳定身份。
        units.resize(units.size()+followup->actions.size());
        for(size_t i=followupBegin;i<units.size();++i) units[i].body.spawnAt=Horizon(s)+1;
    }
	std::vector<int> riftColumns(units.size(),-1);
	for (const auto& rift : committedRifts) riftColumns[rift.first] = rift.second;
	float enemyIce = std::max(0.0f,s.budget-f[5]), supplyAt = s.supplyRemaining;
	auto auras = s.attackAuras;
	std::vector<float> attackRates;
	auto plants = s.plants;
	auto counters = s.counters; // 每个完整世界独占未来炮来源，绝不修改共享Snapshot或跨世界复用冷却。
	auto ladders=s.ladders;
	std::vector<unsigned char> ladderLivingPlants;
	for(const auto& plant:plants) ladderLivingPlants.push_back(plant.health>0);
	auto mowers = s.mowers;
	auto goldenTrails=s.goldenTrails;
	std::vector<size_t> goldenSources;
	for (size_t i=0;i<units.size();++i) if (units[i].goldenDrive.enabled) goldenSources.push_back(i);
	for (auto& plant : plants) {
        plant.initialHealth = plant.repairMaximum > 0 ? plant.repairMaximum : plant.health;
        plant.attackAssetCredit=0;plant.attackAssetInitialHealth=plant.initialHealth;
    }
	auto strikes = s.rowStrikes;
	auto temporalAnchors=s.temporalAnchors;
	float interferenceReady=s.interferenceReady, interferenceUntil=s.interferenceRemaining;
	if (interferenceUntil > 0) temporalAnchors.clear();
	std::vector<size_t> clockSources;
	for (size_t i=0;i<units.size();++i) if (units[i].clock.present) clockSources.push_back(i);
	ConstructionStats constructionStats;
    if(followup) constructionStats.followup.status=followupValid ? FollowupForecast::Status::Pending : FollowupForecast::Status::Invalid;
	constructionStats.counterSpaceReserved=reserveCounterSpace;
	constructionStats.paidDefensesRetained=preservePaidDefenses;
    constructionStats.visibleCounterResponse=s.diagnosticVisibleCounterWitness;
	constructionStats.planternResponseGear=planternResponseGear;
	// 留空位放灰烬不等于拒绝补灯。照明响应世界允许只补灯再清场，-1 原姿态仍
	// 保留完全不建设的对照；共享卡槽、费用和冷却，不强制僵尸搭配。
	const bool rebuildCounterLight = s.weatherStation && reserveCounterSpace && planternResponseGear>=0;
	// 同速假设会把先出的前排当成永久掩护。生产案用出生分布的偏快后排/偏慢前排对照，
	// 已出生单位没有随机范围，保持自己的实际速度；控制、停步、鼓舞仍在后续时间线结算。
	const bool economicForecast = std::any_of(units.begin(),units.end(),[](const Unit& unit){return unit.body.economic && unit.body.health>0;});
	if (economicForecast) for (auto& unit : units) {
		if ((!unit.birthMovementKnown && unit.minimumMoveSpeed<=0) || unit.maximumMoveSpeed<unit.minimumMoveSpeed) continue;
		const float lower=unit.lowerForecastMoveSpeed>0 ? unit.lowerForecastMoveSpeed : unit.minimumMoveSpeed;
		const float upper=unit.upperForecastMoveSpeed>0 ? unit.upperForecastMoveSpeed : unit.maximumMoveSpeed;
		unit.body.speed = unit.body.economic ? upper : lower;
		++constructionStats.movementBoundsApplied;
	}
	constructionStats.abilityIceSpent = static_cast<float>(PrecisionCost(s));
	auto precisionIDs=s.precisionTargetID>0
		? PrecisionIDs(s.precisionTargetID,s.precisionAdditionalTargetIDs,s.precisionTargetLimit)
		: PrecisionIDs(s.pendingPrecisionID,s.pendingPrecisionAdditionalIDs,ColdStorageSkillRules::StrikeTargetLimit);
	const float precisionAt = s.precisionTargetID > 0 ? ColdStorageSkillRules::StrikeAimDuration
		+(s.precisionUnlockAfterPurchase ? std::max(0.0f,s.precisionUnlockAimStartSeconds) : 0) : s.pendingPrecisionRemaining;
	for (size_t i=0; i<units.size(); ++i) if (units[i].id <= 0) units[i].id = -1-static_cast<int>(i);
    std::vector<std::uint32_t> diagnosticJackThrowOrdinals;
    if(s.diagnosticJackTargetMode==1 || s.diagnosticJackTargetMode==2) diagnosticJackThrowOrdinals.assign(units.size(),0);
	std::unordered_map<int,size_t> danceIndices;
	if(danceSlotEnd>nextDanceSlot) for(size_t i=0;i<units.size();++i) danceIndices.emplace(units[i].id,i);
	std::vector<bool> danceBlocked(units.size());
	const bool hasDance=!danceIndices.empty() || std::any_of(units.begin(),units.end(),[](const Unit& u){return u.dance.backup;});
	const bool hasHealer=std::any_of(units.begin(),units.end(),[](const Unit& u){return u.healer.present;});
    const bool hasMagnets=std::any_of(s.plants.begin(),s.plants.end(),[](const Plant& p){return p.magnetRadius>0;})
        || std::any_of(s.construction.begin(),s.construction.end(),[](const Construction& c){return c.plant.magnetRadius>0;});
    std::vector<float> drumActivity;
	std::vector<float> constructionReady;
	std::vector<int> constructionUses;
	std::vector<DeploymentPulse> deploymentPulses;
	std::vector<float> sniperActivity;
	std::vector<float> clockActivity;
	for (const auto& card : s.construction) {
		if (constructionReady.size() <= static_cast<size_t>(card.source)) constructionReady.resize(card.source+1);
		constructionReady[card.source] = card.ready;
		if (card.remainingUses >= 0) {
			const int quota = card.quotaGroup >= 0 ? card.quotaGroup : card.source;
			if (constructionUses.size() <= static_cast<size_t>(quota)) constructionUses.resize(quota+1,-1);
			constructionUses[quota] = card.remainingUses;
		}
	}
	float constructionAt = 0;
	std::vector<float> initialHealth, initialX, smash(units.size());
	std::vector<int> smashTarget(units.size(),-1);
	for (const auto& u : units) { initialHealth.push_back(u.body.health); initialX.push_back(u.body.x); }
	std::vector<float> counterReady;
	std::vector<float> shovelReady(counters.size(),-1);
	std::vector<float> rowStrikeReady;
	std::vector<float> rowStrikeHoldUntil;
	for (const auto& strike : s.rowStrikes) rowStrikeReady.push_back(strike.ready);
	std::vector<PendingCounter> pending;
	std::vector<PlantingBlock> plantingBlocks;
	for (const auto& counter : counters) {
		if (counter.blast.committed) {
			pending.push_back({counter.blast,counter.blast.ready});
			pending.back().plantID = counter.plantID;
            if(CanTraceCombat(s,constructionStats)) {
                auto& shot=pending.back();shot.traceID=++constructionStats.nextCombatAttackID;
                shot.traceLaunchRecorded=counter.plantID==0;
                CombatForecastTrace event;event.event=shot.traceLaunchRecorded ? "counter_inflight" : "counter_committed";event.at=0;event.attackID=shot.traceID;
                event.sourceID=counter.plantID;event.x=counter.blast.x;event.targetRow=counter.ladderClearRow;event.resolveAt=counter.blast.ready;
                AppendCombatTrace(s,constructionStats,std::move(event));
            }
			pending.back().invulnerableAt = counter.vulnerableSeconds;
			pending.back().consumesPlant = counter.consumesPlant;
			pending.back().releaseAt = counter.blast.ready-std::max(0.0f,counter.flightSeconds);
            pending.back().publicLaunchObserved=!counter.consumesPlant && counter.plantID==0;
			pending.back().clearsCell = counter.clearsCell;
			pending.back().craterSeconds = counter.craterSeconds;
			pending.back().ladderClearRow=counter.ladderClearRow;
			pending.back().ladderClearRadius=counter.ladderClearRadius;
			pending.back().clearsLadderRow=counter.clearsLadderRow;
			// ID 0 表示没有植物来源，不能把纯数值夹具/未来画像的中性 ID 误标为爆炸宿主。
			if(counter.plantID!=0 && counter.consumesPlant) for (auto& plant : plants) if (plant.id == counter.plantID) plant.counterBlastAt = counter.blast.ready;
		}
		else {
			if (counterReady.size() <= static_cast<size_t>(counter.source)) counterReady.resize(counter.source + 1);
			counterReady[counter.source] = counter.blast.ready;
		}
		if (counter.sharedSource >= 0) {
			if (counterReady.size() <= static_cast<size_t>(counter.sharedSource)) counterReady.resize(counter.sharedSource+1);
			counterReady[counter.sharedSource] = counter.sharedReady;
		}
	}
    int projectedWave=s.stationWave+(s.weatherStation && std::any_of(plan.begin(),plan.end(),[&](const Action& a){return s.options[a.option].device<0;}) ? 1 : 0);
	std::vector<bool> refunded(units.size()), breached(units.size());
	std::vector<float> counterHoldUntil(counterReady.size(),-1);
    ColdStorageObservedForecast::WorldClock observedClock(s.observedResponse);
	std::vector<unsigned char> melonHits(units.size());
	auto pressureRays=s.pressureRays;
	auto bambooRays=s.bambooRays;auto floodShots=s.floodShots;
	auto thunderRays=s.thunderRays; // 已发射平射雷种独立存在，来源死亡不取消。
	auto basketballs=s.basketballs;
	auto jackBoxes=s.jackBoxes;
    std::vector<bool> combatSpawnRecorded;
    if(s.traceCombat) {
        combatSpawnRecorded.resize(units.size());combatActionIndices.resize(units.size(),-1);
        for(auto& box:jackBoxes) {
            box.traceID=++constructionStats.nextCombatAttackID;
            CombatForecastTrace event;event.event="jack_inflight";event.at=0;event.attackID=box.traceID;
            event.sourceID=box.ownerID;event.x=box.x;event.y=box.y;event.resolveAt=box.at;
            AppendCombatTrace(s,constructionStats,std::move(event));
        }
    }
	auto windCounters=s.windCounters;

	float playerSun = static_cast<float>(s.playerSun), playerIce = static_cast<float>(s.playerIce);
	float pendingIce = static_cast<float>(s.incomingIce), arrival = s.incomingIceAt;
	std::vector<float> exchangeReady;
	for (const auto& card : s.exchanges) exchangeReady.push_back(card.ready);
	std::vector<std::pair<std::array<int,2>,float>> exchangeOccupied;
	const auto capPlayerResources = [&] {
		if (FullForecast(s) || s.anticipateEconomy) {
			playerSun = std::min(playerSun,static_cast<float>(s.playerSunLimit));
			playerIce = std::min(playerIce,static_cast<float>(s.playerIceLimit));
		}
	};
	capPlayerResources();
	// 砸击一旦开始，目标被队友先消灭也要走完原有前摇。锁住原格，
	// 避免残留进度永久挡住投掷，或把半次砸击转移给后面另一格植物。
	const auto advanceSmash = [&](size_t i, float active, float speedFactor) {
		smash[i] += active * (speedFactor < 1 ? 0.6f : 1);
		if (smash[i] < units[i].body.smashSeconds) return;
		const int target = smashTarget[i];
		smash[i] = 0; smashTarget[i] = -1;
		const int row = plants[target].row, column = plants[target].column;
		if (FullForecast(s)) {
			// 正式巨人一次砸击结算原格各层；原宿主消失不改变已经锁定的格位。
			for (auto& p : plants) if (p.health > 0 && p.row == row && p.column == column) {
				DamagePlant(p,p.health,true,f);
			}
		} else {
			auto& p = plants[target];
			if (p.health > 0) {
				DamagePlant(p,p.health,true,f);
			}
		}
	};
    int sampledLiving=0;
    float sampledPaidCapital=0;
    for(const auto& unit:s.current) if(unit.body.health>0) {
        ++sampledLiving; sampledPaidCapital+=std::max(0.0f,unit.body.purchaseCost);
    }
    // 未进入数值画像的真实占位仍占名额；未来死亡/收入只在本世界实际发生后计入。
    const int unprojectedOccupied=std::max(0,s.deploymentOccupied-sampledLiving);
    const auto advanceFollowup=[&](float t) {
        auto& forecast=constructionStats.followup;
        if(!followupValid || forecast.status!=FollowupForecast::Status::Pending) return;
        if(t>followup->expirySeconds) { forecast.status=FollowupForecast::Status::Expired; return; }
        if(t<followup->earliestSeconds) return;
        std::set<int> readySources;
        for(const auto& counter:counters) {
            if(counter.blast.committed || counter.consumesPlant || counter.source<0
                || counter.source>=static_cast<int>(counterReady.size()) || counterReady[counter.source]>t) continue;
            if(counter.sharedSource>=0 && (counter.sharedSource>=static_cast<int>(counterReady.size())
                || counterReady[counter.sharedSource]>t)) continue;
            if(counter.plantID!=0 && std::none_of(plants.begin(),plants.end(),[&](const Plant& plant) {
                return plant.id==counter.plantID && plant.health>0;
            })) continue;
            readySources.insert(counter.source);
        }
        forecast.observedCobLaunches=constructionStats.observedCobLaunches;
        forecast.readyCobs=static_cast<int>(readySources.size());
        forecast.committedShots=static_cast<int>(std::count_if(pending.begin(),pending.end(),[](const PendingCounter& shot) {
            return !shot.consumesPlant;
        }));
        if((followup->requireObservedCobLaunch && forecast.observedCobLaunches==0)
            || (followup->requireShotsClear && forecast.committedShots>0)
            || (followup->maxReadyCobs>=0 && forecast.readyCobs>followup->maxReadyCobs)) return;
        if(forecast.observedAt<0) forecast.observedAt=t;
        if(enemyIce<followupBill) return;
        int living=0;
        float paidCapital=0;
        for(const auto& unit:units) if(unit.body.health>0) {
            ++living; paidCapital+=std::max(0.0f,unit.body.purchaseCost);
        }
        const auto capacity=s.deploymentCapital<0 ? s.capacity+sampledLiving
            : ColdStorageDeploymentRules::Capacity(static_cast<std::int64_t>(std::max(0.0,
                static_cast<double>(s.deploymentCapital)+enemyIce-s.budget+paidCapital-sampledPaidCapital)));
        if(living+unprojectedOccupied+static_cast<int>(followup->actions.size())>capacity) return;
        // 所有资格、钱包和名额通过后只扣一次完整费用，再激活整队；不使用未来收益。
        forecast.walletBefore=enemyIce; enemyIce-=static_cast<float>(followupBill);
        forecast.walletAfter=enemyIce; forecast.spent=static_cast<float>(followupBill);
        forecast.purchasedAt=t; forecast.status=FollowupForecast::Status::Purchased;
        f[5]+=forecast.spent;
        if(s.weatherStation) ++projectedWave;
        const bool economicTail=economicForecast || std::any_of(followup->actions.begin(),followup->actions.end(),[&](const Action& a) {
            return s.options[a.option].unit.body.economic;
        });
        for(size_t n=0;n<followup->actions.size();++n) {
            const auto& action=followup->actions[n];
            const auto& option=s.options[action.option];
            const size_t index=followupBegin+n;
            const int stableID=units[index].id;
            units[index]=option.unit; auto& unit=units[index]; unit.id=stableID;
            unit.body.purchaseCost=static_cast<float>(option.cost); unit.body.spawnAt=t+action.delay;
            if(economicTail && (unit.birthMovementKnown || unit.minimumMoveSpeed>0)
                && unit.maximumMoveSpeed>=unit.minimumMoveSpeed) {
                unit.body.speed=unit.body.economic ? (unit.upperForecastMoveSpeed>0 ? unit.upperForecastMoveSpeed : unit.maximumMoveSpeed)
                    : (unit.lowerForecastMoveSpeed>0 ? unit.lowerForecastMoveSpeed : unit.minimumMoveSpeed);
                ++constructionStats.movementBoundsApplied;
            }
            initialHealth[index]=unit.body.health; initialX[index]=unit.body.x;
            refunded[index]=false; breached[index]=false;
            forecast.realizedActions.push_back({action.option,unit.body.spawnAt});
            if(s.traceCombat) {
                combatActionIndices[index]=static_cast<int>(plan.size()+n);
                // 零延迟后队在付款当步已入场；不能等下步追踪而漏掉当步出生/死亡。
                if(unit.body.spawnAt<=t && CanTraceCombat(s,constructionStats)) {
                    combatSpawnRecorded[index]=true;
                    CombatForecastTrace event; event.event="unit_spawn"; event.reason="followup_purchase";
                    event.at=t; event.sourceID=unit.id; event.sourceRow=unit.body.row;
                    event.actionIndex=combatActionIndices[index]; event.resolveAt=unit.body.spawnAt;
                    event.x=unit.body.x+unit.body.blastAnchorOffset; event.y=ForecastRowY(s,unit.body.row,unit.body.x);
                    event.targets.push_back(UnitCombatHit(s,unit,0)); AppendCombatTrace(s,constructionStats,std::move(event));
                }
            }
        }
    };
	bool orderArrived = false;
    // 预测出生ID使用负数，0才是未锁定哨兵；潜在的后续真实增援也可能首次带来护卫。
    const bool pressureGroupPresent=std::any_of(units.begin(),units.end(),[](const Unit& u){return u.pressureGuard;})
        || (followup && std::any_of(followup->actions.begin(),followup->actions.end(),[&](const Action& a) {
            return a.option>=0 && a.option<static_cast<int>(s.options.size()) && s.options[a.option].unit.pressureGuard;
        }));
	for (float t = 0; t < Horizon(s); t += kStep) {
        if (pressureGroupPresent) for (auto& u : units) u.pressureGroupPresent=true;
		if (s.cancellation && s.cancellation->load(std::memory_order_relaxed)) throw SearchCancelled{};
		for (auto& unit:units) {
			unit.thunderResistance=std::max(0.0f,unit.thunderResistance-kStep);
			unit.paralysisRemaining=std::max(0.0f,unit.paralysisRemaining-kStep);
		}
        // 出生关联只覆盖已有/已付/当前购物车与已承诺裂隙；新召唤预留槽由命中事件记录，不冒充完整出生流。
        if(CanTraceCombat(s,constructionStats)) for(size_t i=0;i<units.size();++i) {
            if(i>=originalUnits && i<followupBegin) continue;
            const auto& unit=units[i];
            if(combatSpawnRecorded[i] || unit.body.health<=0 || unit.body.spawnAt>t) continue;
            combatSpawnRecorded[i]=true;
            CombatForecastTrace event;event.event=combatActionIndices[i]>=0 || unit.body.spawnAt>0 ? "unit_spawn" : "unit_present";
            event.at=t;event.sourceID=unit.id;event.sourceRow=unit.body.row;event.actionIndex=combatActionIndices[i];
            event.x=unit.body.x+unit.body.blastAnchorOffset;event.y=ForecastRowY(s,unit.body.row,unit.body.x);event.resolveAt=unit.body.spawnAt;
            event.targets.push_back(UnitCombatHit(s,unit,0));AppendCombatTrace(s,constructionStats,std::move(event));
        }
		const float previousKills = f[0];
		environment.Step(s,t,plants,units,f,playerIce,constructionStats,projectedWave,clearFogWhenReady && !preservePaidDefenses,planternResponseGear,preservePaidDefenses);
		// 只根据当前已受伤/死亡的锚目标择时，保留同一钱包和冷却；不读取真人未来输入。
		if (!preservePaidDefenses && s.interferenceAvailable && t >= interferenceReady && playerIce >= ColdStorageSkillRules::InterferenceIceCost) {
			float restoredHealth=0;
			for (const auto& anchor:temporalAnchors) if (anchor.at <= t+kStep)
				for (const auto& target:anchor.targets) if (target.unit >= 0 && target.unit < static_cast<int>(units.size())
					&& !units[target.unit].temporalIrreversible)
					restoredHealth+=std::max(0.0f,target.saved.body.health-units[target.unit].body.health);
			// 至少阻止一次曙光主击规模的恢复，避免为无损小队空耗昂贵技能。
			if (restoredHealth >= DawnLotusRules::Damage) {
				temporalAnchors.clear(); playerIce-=ColdStorageSkillRules::InterferenceIceCost;
				constructionStats.iceSpent+=ColdStorageSkillRules::InterferenceIceCost;
				++constructionStats.interferences;
				interferenceReady=t+ColdStorageSkillRules::InterferenceCooldown;
				interferenceUntil=t+ColdStorageSkillRules::InterferenceDuration;
			}
		}
		// 灰烬移除来源时盒子事务已结束；先清理，避免本步钟匠复活来源后让旧盒子重新生效。
		jackBoxes.erase(std::remove_if(jackBoxes.begin(),jackBoxes.end(),[&](const JackBoxFlight& flight) {
            const bool cancelled=std::any_of(units.begin(),units.end(),[&](const Unit& owner) {
				return owner.id==flight.ownerID && owner.jack.flightsCancelled;
			});
            if(cancelled && CanTraceCombat(s,constructionStats)) {
                CombatForecastTrace event;event.event="jack_cancel";event.reason="source_ash_removed_before_revival";
                event.at=t;event.attackID=flight.traceID;event.sourceID=flight.ownerID;
                event.x=flight.x;event.y=flight.y;event.resolveAt=flight.at;AppendCombatTrace(s,constructionStats,std::move(event));
            }
            return cancelled;
		}),jackBoxes.end());
		if (!temporalAnchors.empty()) ResolveTemporalAnchors(s,t,temporalAnchors,units,plants,initialHealth,f,constructionStats);
		AdvanceGoldenIce(s,t,units,goldenSources,goldenTrails,constructionStats);
		if (!precisionIDs.empty() && t >= precisionAt) {
			// 同一瞄准终点逐个结束原植物的生命周期；跨行/叠层各自结算，消失不替补。
			for(int id:precisionIDs) for (auto& p : plants) if (p.id == id && p.health > 0) {
				// 单株生命周期结束，不借用群伤/砸击，不受壳层或无敌状态阻挡。
				f[1] += p.reward*p.health/std::max(1.0f,p.initialHealth);
                p.attackAssetCredit+=p.assetValue*p.health/AttackAssetHealth(p);
				f[0] += p.reward; p.health = 0; ++constructionStats.precisionHits;
				break;
			}
			precisionIDs.clear(); // 消失也消费事务，不能命中新种在同格的替代物。
		}
		if (s.supplyInterval > 0) while (t >= supplyAt) { enemyIce += s.supplyIce; supplyAt += s.supplyInterval; }
		if (s.anticipateEconomy && pendingIce > 0 && t >= arrival) {
			playerIce += pendingIce; pendingIce = 0;
		} else if (!s.anticipateEconomy && !orderArrived && t >= s.incomingIceAt) {
			playerIce += s.incomingIce; orderArrived = true;
		}
		for (const auto& p : plants) if (p.health > 0 && p.shutdownUntil<=t && t >= p.productionAt) playerSun += p.sunPerSecond * kStep;
		capPlayerResources(); // 满仓后的溢出不是可被攻击消耗的实际资产
        advanceFollowup(t);
		AdvanceRiftArrivals(s,t,plants,units,riftColumns,constructionStats);
		if(hasDance)
			AdvanceDances(s,t,units,danceIndices,nextDanceSlot,danceSlotEnd,initialHealth,initialX,danceBlocked,constructionStats);
		AdvancePlantRepairs(s,t,plants,units,playerIce,f,constructionStats,true);
		AdvanceBasketballs(t,basketballs,plants,f,constructionStats);
		AdvanceJackBoxes(s,t,jackBoxes,units,plants,f,constructionStats);
        AdvanceWindCounters(s,t,windCounters,plants,units,playerSun,playerIce,constructionStats);
		PruneDeadLadderHosts(plants,ladderLivingPlants,ladders,constructionStats);
        if(hasMagnets) AdvanceMagnets(s,t,plants,units,temporalAnchors,constructionStats,ladders);
		AdvanceRowStrikes(s,strikes,t,plants,units,initialHealth,rowStrikeReady,rowStrikeHoldSeconds,rowStrikeHoldUntil,f);
		AdvanceCounters(s,counters,t,plants,units,initialHealth,counterReady,pending,playerSun,playerIce,f,counterHoldSeconds,storedHoldSeconds,counterHoldUntil,plantingBlocks,constructionStats,
			 s.weatherStation ? &environment.fogAlpha : nullptr,shovelCounterSpace,shovelReady,preservePaidDefenses,
			interferenceBeforeAsh,temporalAnchors,interferenceReady,interferenceUntil,ladders,s.observedProfile ? &observedClock : nullptr);
		// 本步落种先触发瞄准，再推进这半秒弹道，避免给新灰烬额外赠送半秒安全时间。
		AdvanceDeploymentSnipers(s,t,units,plants,deploymentPulses,sniperActivity,f,constructionStats);
        AdvancePressureShooters(s,t,units,plants,pressureRays,f,s.weatherStation ? s.rainZombie[environment.Rain()] : -1);
        AdvanceCottonHealing(t,plants,f);
		if (s.anticipateEconomy) AdvancePlayerEconomy(s,counters,t,plants,exchangeReady,exchangeOccupied,counterReady,
			constructionReady,constructionUses,auras,units,playerSun,playerIce,pendingIce,arrival,constructionStats,reserveCounterSpace,
			preserveManualAuras || preservePaidDefenses,plantingBlocks,rebuildCounterLight,preservePaidDefenses,s.weatherStation ? &environment.fogAlpha : nullptr);
		if ((!reserveCounterSpace || rebuildCounterLight) && !s.construction.empty() && t >= constructionAt) {
			AdvanceConstruction(s,t,Horizon(s),units,plants,constructionReady,constructionUses,
				strikes,rowStrikeReady,playerSun,playerIce,constructionStats,s.weatherStation ? &environment.fogAlpha : nullptr,plantingBlocks,reserveCounterSpace,planternResponseGear,pendingIce,
				counters,counterReady,counterHoldUntil,shovelReady);
			constructionAt = t+kConstructionInterval;
		}
		if(!preservePaidDefenses) AdvancePlantRepairs(s,t,plants,units,playerIce,f,constructionStats,false);
		if (!auras.empty()) AdvanceAttackAuras(s,t,auras,plants,units,playerIce,attackRates,constructionStats,preserveManualAuras || preservePaidDefenses);
		// 每株植物只对当前实际可见前锋开火。邻行没有引火目标时不凭空产生西瓜溅射。

		// 对平射雷种作同一逻辑步内的扫掠碰撞，避免把远处控制提前当成即时生效。
		for (auto ray=thunderRays.begin();ray!=thunderRays.end();) {
			const float toX=ray->x+ThunderFlowerRules::ProjectileSpeed*kStep;
			int target=-1; float impactX=toX;
			for (size_t i=0;i<units.size();++i) {
				const auto& u=units[i].body;
				if (u.health<=0 || u.spawnAt>t || u.row!=ray->row || !CanTargetProjectile(units[i],false)
					|| u.x+u.boundsOffset>toX+10 || u.x+u.boundsOffset+u.boundsWidth<ray->x-10) continue;
				const float hitX=std::max(ray->x,u.x+u.boundsOffset-10);
				if (target<0 || hitX<impactX) {target=static_cast<int>(i);impactX=hitX;}
			}
			if (target<0) {
				ray->x=toX;
				if (toX>s.rightEdge+40) ray=thunderRays.erase(ray); else ++ray;
				continue;
			}
			std::vector<std::pair<float,size_t>> hits;
			const float radius=ThunderFlowerRules::RadiusCells*s.cellWidth;
			for (size_t i=0;i<units.size();++i) {
				const auto& u=units[i].body;
				if (u.health<=0 || u.spawnAt>t || !CanTargetProjectile(units[i],false) || std::abs(u.row-ray->row)>1
					|| u.x+u.boundsOffset>impactX+radius || u.x+u.boundsOffset+u.boundsWidth<impactX-radius) continue;
				const float dx=u.x+u.boundsOffset+u.boundsWidth*.5f-impactX;
				const float dy=(u.row-ray->row)*s.cellHeight;
				hits.emplace_back(dx*dx+dy*dy,i);
			}
			std::sort(hits.begin(),hits.end()); int controlled=0;
			for (const auto& hit:hits) {
				auto& unit=units[hit.second];
				ApplyDamage(unit,ThunderFlowerRules::Damage,false,false,false,ray->origin,&units,&s,t);
				if (controlled<ThunderFlowerRules::ControlLimit && unit.body.health>0 && unit.paralysisAllowed
					&& unit.chargeControlImmunity<=t && unit.paralysisRemaining<=0 && unit.thunderResistance<=0) {
					unit.body.stopped=std::max(unit.body.stopped,ThunderFlowerRules::Paralysis);
					unit.paralysisRemaining=ThunderFlowerRules::Paralysis;
					unit.thunderResistance=ThunderFlowerRules::Paralysis+ThunderFlowerRules::Resistance;
					++controlled; ++constructionStats.thunderStuns;
				}
			}
			ray=thunderRays.erase(ray);
		}
        AdvanceFloodMortars(s,t,environment.Rain(),units,plants,floodShots,f);
        AdvanceBambooRays(s,t,units,bambooRays);
		for (size_t pi=0; pi<plants.size(); ++pi) if (plants[pi].health > 0 && plants[pi].dps > 0 && plants[pi].shutdownUntil<=t) {
			auto& p = plants[pi];
			const float attackRate = (auras.empty() ? 1 : attackRates[pi]) * (s.weatherStation ? s.rainPlant[environment.Rain()]/std::max(.001f,s.sampledRainPlant) : 1) * (p.floodSlowUntil>t ? FloodMortarRules::AttackMultiplier : 1);
			int target = -1;
			for (size_t i = 0; i < units.size(); ++i) {
				const auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > t || u.x > s.rightEdge
					|| (!CanTargetProjectile(units[i],false) && !(p.targetsAir && CanTargetProjectile(units[i],true)))
                    || (p.echo ? !EchoContains(s,p,units[i])
                        : (p.around ? std::abs(u.x - p.x) > p.range : u.x < p.x - 30 || u.x > p.x + p.range))) continue;
				// 雷鸣花先在本行发射，命中后才向邻行放电；邻行单位不能提前触发虚构雷种。
				if (u.row != p.row && (p.melon || p.thunder || std::abs(u.row - p.row) > p.rowRadius)) continue;
				if(s.weatherStation && !environment.CanTarget(s,p,units[i])) continue;
				const bool air=p.targetsAir && CanTargetProjectile(units[i],true);
                const bool selectedAir=target>=0 && p.targetsAir && CanTargetProjectile(units[target],true);
                if(target<0 || (air && !selectedAir) || (air==selectedAir && u.x<units[target].body.x)) target=static_cast<int>(i);
			}
            if(p.rainBamboo) {
                const float rate=p.bambooRate*(auras.empty()?1:attackRates[pi])*(p.floodSlowUntil>t?FloodMortarRules::AttackMultiplier:1);
                p.bambooCharge=std::min(1.0f,p.bambooCharge+kStep*rate/RainBambooRules::Interval(environment.Rain()));
                if(target>=0 && p.bambooCharge>=1) {p.bambooCharge=0;bambooRays.push_back({p.x+32,p.row,p.damageOrigin,{}});}
                continue;
            }
			if (p.thunder) {
				const int shots=p.thunderAttack.Advance(kStep,p.thunderAttack.sampledRate*attackRate,target>=0);
				for(int shot=0;shot<shots;++shot) thunderRays.push_back({p.x+30,p.row,p.damageOrigin});
				continue;
			}
			if (target < 0) continue;
			// 只在存在可攻击目标时成长。领域不仅提高本步伤害，也让后续阶段更快到来；
			// 保守忽略未来受惊重置，不能把尚未成功的近身进攻当成已解除这株高成长火力。
			float damageRate = attackRate;
			if (p.growth.perShot > 0) {
				const float damage = p.growth.Advance(kStep*attackRate*p.growthSpeed);
				p.hitDamage = p.growth.Damage();
				p.dps = p.hitDamage/p.growth.Interval()*p.growthSpeed;
				damageRate = damage/std::max(0.001f,p.dps*kStep);
			}


			const auto impact = units[target].body;
			int secondaryCount = 0;
			if (p.melon) {
				// 与正式弹丸一样，先冻结命中集合再伤害；大编队的次要伤害不能无限线性叠加。
				for (size_t i = 0; i < units.size(); ++i) {
					const auto& u = units[i].body;
					melonHits[i] = static_cast<int>(i) != target && u.health > 0 && u.spawnAt <= t
                        && CanTargetProjectile(units[i],false)
						&& ColdStorageStrategy::MelonSplashContains(impact,u);
					secondaryCount += melonHits[i];
				}
			}
			const float secondaryDps = ColdStorageStrategy::MelonSecondaryDps(p.dps,secondaryCount);
			float fumeEnd = p.x+p.range;
			if (p.fume && !p.around) for (const auto& unit : units) {
				if (unit.body.health > 0 && unit.body.spawnAt <= t && unit.body.row == p.row
					&& unit.body.x >= p.x-30 && unit.shieldHealth > 0 && unit.blocksFumePiercing)
					fumeEnd = std::min(fumeEnd,unit.body.x);
			}
			for (size_t i = 0; i < units.size(); ++i) {
				auto& u = units[i].body;
				if (u.health <= 0 || u.spawnAt > t) continue;
                const bool airShot=p.targetsAir && CanTargetProjectile(units[target],true);
                if(!CanTargetProjectile(units[i],airShot)) continue;
                const bool splash = p.melon && melonHits[i];
				const bool area = !p.melon && p.multiTarget && std::abs(u.row - p.row) <= p.rowRadius
					&& (p.echo ? EchoContains(s,p,units[i])
                        : (p.around ? std::abs(u.x - p.x) <= p.range : u.x >= p.x - 30 && u.x <= p.x + p.range));
				if (static_cast<int>(i) != target && !splash && !area) continue;
				if (p.fume && !p.around && u.x > fumeEnd) continue;
				const auto hit = DescribePlantHit(units[i],p,splash ? secondaryDps/p.dps : 1);
				const bool slowReachesBody = units[i].shieldHealth <= 0 || p.melon || hit.bypass;
				ApplyDamage(units[i],hit.rate*kStep*damageRate,hit.penetrate,hit.bypass,hit.discardOverflow,p.damageOrigin,&units,&s,t);
				if ((units[i].digger.present ? units[i].digger.CanChill() : u.canBeChilled)
					&& u.slowImmunity <= t && p.slowRate > 0 && slowReachesBody)
					u.slow = std::max(u.slow, std::min(p.slowDuration, p.slowRate * p.slowDuration * kStep * 2 * attackRate));
				if(units[i].chargeControlImmunity<=t) u.stopped = std::max(u.stopped, std::min(kStep * 0.9f, p.stopDuty * kStep * attackRate));
			}
		}
		// 统一在本步植物伤害后、僵尸治疗/经济/啃食前结算；尚未出生的付款队列不流血。
		for(auto& unit:units) if(unit.body.health>0 && unit.body.spawnAt<=t) AdvanceHeadlessDecay(unit);
		const float healingCredit=constructionStats.healerRecoveryCredit;
        if(hasHealer) AdvanceHealers(s,t,units,initialHealth,constructionStats);
        f[6]-=constructionStats.healerRecoveryCredit-healingCredit;
        AdvanceDrums(s,t,units,constructionStats,drumActivity);
		if (!clockSources.empty()) AdvanceClocks(s,t,units,clockSources,temporalAnchors,clockActivity,constructionStats,interferenceUntil);
        if(!s.weatherStation) {
            environment.illumination.fill(0);
            for(const auto& p:plants) if(p.plantern && p.health>0 && p.lightFuel>0 && p.lightGear!=PlanternGear::OFF)
                for(int cell=0;cell<s.rows*s.columns;++cell)
                    environment.illumination[cell]=std::max(environment.illumination[cell],p.illumination[cell]);
        }
        const int ferryFog=s.weatherStation ? (environment.state.controls[1].value==0 ? s.columns
            : s.columns-3-environment.state.controls[1].value) : s.fogLeftColumn;
        AdvanceFogFerries(s,t,kStep,ferryFog,environment.illumination,units,constructionStats);
		for (size_t i = 0; i < units.size(); ++i) {
			auto& worker = units[i]; auto& u = worker.body;
			if (u.health <= 0 || u.spawnAt > t) continue;
			float active = std::max(0.0f, kStep - u.stopped)*worker.healer.movementActivity;
			u.stopped = std::max(0.0f, u.stopped - kStep);
			const float ritualActivity = AdvanceRitual(s,t,worker,active,plants,units,nextRiftSlot,followupBegin,riftColumns,initialHealth,initialX,constructionStats);
			const float speedFactor = u.slow > 0 ? u.slowFactor*GoldenIceRules::Amplify(.5f,worker.goldenStacks)/.5f : 1;
			u.slow = std::max(0.0f, u.slow - kStep);
			// 被搬走的巨人必须撤销原格未落锤/未脱手动作，已飞行小鬼独立保留。
            if(worker.ferry.transported) {smash[i]=0;smashTarget[i]=-1;if(i<originalUnits && throwRemaining[i]>0) throwRemaining[i]=-1;}
            if(danceBlocked[i] || worker.ferry.blocksMovement) continue;
            if(worker.floodMortar && worker.body.health>worker.floodStopHealth && FindFloodTarget(s,worker,plants,t,environment.Rain())>=0) continue;
            if(worker.digger.present) {
                // 阶段门槛按实体原点；地下只受位移场和鼓舞，不吃地面风雨倍率。
                float objectX=u.x+u.blastAnchorOffset;
                const float fog=s.weatherStation && environment.Obscured(s,worker) ? WeatherStationRules::FogMoveMultiplier : 1;
                const float drum=GoldenIceRules::Amplify(1+CrystalDrummerRules::MoveBonus*worker.inspiration.size(),worker.goldenStacks);
                const float phaseRate=speedFactor<1 ? .5f : 1;
                active=worker.digger.Advance(active*phaseRate,objectX,fog*drum)/phaseRate;
                u.x=objectX-u.blastAnchorOffset;
                if(active<=0) continue;
            }
            if(worker.balloon.present && worker.balloon.phase!=BalloonRules::Phase::WALKING) {
                auto& balloon=worker.balloon;
                if(balloon.phase==BalloonRules::Phase::POPPING) {
                    const float rain=s.weatherStation ? s.rainZombie[environment.Rain()] : worker.rawRainMultiplier;
                    if(balloon.AdvanceLanding(active*rain*(speedFactor<1 ? ZombieMovementRules::NormalSlowAnimationFactor : 1))) {
                        u.speed=balloon.walkSpeed; u.slowFactor=.3f; worker.boundsY+=BalloonRules::ColliderRise; worker.groundHazard=true;
                        worker.temporalStopHealth=static_cast<int>(worker.maximumBody)/3;
                    }
                } else {
                    const bool blowing=balloon.blowing;
                    const float slow=speedFactor<1 ? .5f : 1;
                    if(blowing) {
                        if(balloon.AdvanceBlow(active*slow,u.x,s.rightEdge)) {u.health=0;}
                    } else {
                        const float fog=s.weatherStation && environment.Obscured(s,worker) ? WeatherStationRules::FogMoveMultiplier : 1;
                        u.x-=balloon.flightSpeed*GoldenIceRules::Amplify(balloon.windMultiplier,worker.goldenStacks)
                            *GoldenIceRules::Amplify(1+CrystalDrummerRules::MoveBonus*worker.inspiration.size(),worker.goldenStacks)*fog*slow*active;
                    }
                }
                continue;
            }
            if(AdvanceJack(s,t,worker,active,speedFactor<1,s.weatherStation ? s.rainZombie[environment.Rain()] : worker.rawRainMultiplier,
                plants,jackBoxes,f,constructionStats,diagnosticJackThrowOrdinals.empty() ? nullptr : &diagnosticJackThrowOrdinals[i])) continue;
			if (smashTarget[i] >= 0) {
				advanceSmash(i,active,speedFactor);
				continue; // 动作完成后下一步才恢复投掷/移动，与既有正常砸击的步进顺序一致。
			}
			if (i < originalUnits && thrownChild[i] >= 0 && u.health <= worker.throwHealth
				&& u.x > worker.throwAnchorX+ImpThrowRules::MinimumDistance) {
				if (throwRemaining[i] < 0) throwRemaining[i] = worker.throwWindup;
				throwRemaining[i] -= active*(speedFactor < 1 ? .5f : 1);
				if (throwRemaining[i] <= 0) {
					const int childIndex = thrownChild[i];
					auto& child = units[childIndex];
					const float flight = ImpThrowRules::FlightSeconds(u.x-worker.throwAnchorX);
					child.body.x = u.x-ImpThrowRules::ReleaseOffsetX-ImpThrowRules::HorizontalSpeed*flight;
					child.body.health = ImpThrowRules::Health;
					child.body.spawnAt = t+flight+kForecastImpLanding;
					child.body.slow = u.slow;
					initialHealth[childIndex] = ImpThrowRules::Health; initialX[childIndex] = child.body.x;
					thrownChild[i] = -1; // 脱手后不随投手死亡回滚，且每个投手仅能提交一次
				}
				continue; // 前摇不移动或砸击；期间被杀便不会走到提交分支
			}
			if (u.economic && !worker.headless && u.health > worker.productionStopHealth) {
				worker.productionRemaining -= active;
				while (worker.productionRemaining <= 0) {
					const int income = static_cast<int>(worker.nextYield);
					enemyIce += income;
					if (t < kHorizon) f[4] += income;
					else constructionStats.productionAfterWindow+=income;
					if (s.traceEconomy) constructionStats.workerTrace.push_back({worker.id,u.row,t,u.x,u.health,static_cast<float>(income)});
					worker.productionRemaining += IceProduction::Interval;
					worker.nextYield = std::min(IceProduction::MaximumYield, worker.nextYield * IceProduction::YieldGrowth);
				}
			}

			if (worker.engineer && !worker.canisterFull && u.health>worker.engineerStopHealth && active>0) {
				if (!worker.reloadPaid) {
					if (enemyIce>=DisasterEngineerRules::ReloadCost) {
						enemyIce-=DisasterEngineerRules::ReloadCost; f[5]+=DisasterEngineerRules::ReloadCost;
						constructionStats.engineerReloadIce+=DisasterEngineerRules::ReloadCost;
						constructionStats.abilityIceSpent+=DisasterEngineerRules::ReloadCost; worker.reloadPaid=true;
					}
				} else {
					worker.reloadRemaining=std::max(0.0f,worker.reloadRemaining-active);
					if (worker.reloadRemaining<=0) {worker.canisterFull=true;worker.reloadPaid=false;}
				}
			}
			AdvanceArmorRepair(worker,active,enemyIce,f,constructionStats);
			AdvanceCatapultCrush(worker,active,plants,f);
			if(AdvanceCatapult(s,t,worker,active,speedFactor<1,s.weatherStation ? s.rainZombie[environment.Rain()] : worker.rawRainMultiplier,
				plants,basketballs,constructionStats)) continue;
			int contact = -1;
			const bool movingRight=worker.digger.present && worker.digger.IsMovingRight();
			for (size_t j = 0; j < plants.size(); ++j) {
				const auto& p = plants[j];
				if (p.health <= 0 || !p.edible || p.row != u.row || (movingRight ? p.x < u.x-30 : p.x > u.x+30)
					|| (worker.instantVehicleCrush && !p.vehicleCrushable)
					|| (worker.catapult.present && !p.catapultCrushable)) continue;
				if (contact < 0 || (movingRight ? p.x < plants[contact].x : p.x > plants[contact].x)
					|| (p.x == plants[contact].x && p.layer > plants[contact].layer)) contact = static_cast<int>(j);
			}
			// 触发距离按实体原点，与用碰撞中心判断接敌的坐标分开。
			const float triggerX = u.x+u.blastAnchorOffset;
			const bool inRange = worker.burst.range > 0 && std::any_of(plants.begin(),plants.end(),[&](const Plant& p) {
				return p.health > 0 && p.edible && p.row == u.row && triggerX >= p.x && triggerX-p.x <= worker.burst.range;
			});
			const auto activity = AdvanceBurst(worker,inRange,active*drumActivity[i]*ritualActivity
				* (worker.sniper.aiming ? 0 : sniperActivity[i]) * (clockSources.empty() ? 1 : clockActivity[i]),enemyIce,f,constructionStats);
			const float ritualSpeed=worker.ritual.armor>0 ? AuroraPriestRules::NormalSpeed : AuroraPriestRules::OverloadSpeed;
			const float ritualMove = !worker.ritual.present ? 1 : GoldenIceRules::Amplify(ritualSpeed
				* (u.x+u.blastAnchorOffset > s.gridLeft+s.columns*s.cellWidth ? AuroraPriestRules::OutsideSpeed : 1),worker.goldenStacks);
			const float ritualBite = !worker.ritual.present ? 1 : GoldenIceRules::Amplify(ritualSpeed,worker.goldenStacks)
				* (worker.ritual.armor>0 ? 1 : AuroraPriestRules::OverloadBite/AuroraPriestRules::NormalBite);
			const float drumMove = GoldenIceRules::Amplify(1+CrystalDrummerRules::MoveBonus*worker.inspiration.size(),worker.goldenStacks);
			const float drumBite = GoldenIceRules::Amplify(1+CrystalDrummerRules::BiteBonus*worker.inspiration.size(),worker.goldenStacks);
			const float acceleration=worker.goldenDrive.enabled ? GoldenIceRules::Acceleration(worker.goldenDrive.undamaged,worker.goldenStacks) : 1;
			if(worker.ladder.present || worker.ladderClimb.eligible || worker.ladderClimb.phase!=LadderRules::Climb::Phase::NONE) {
				// 放梯不吃横向风倍率；共享攀梯只按真实内部半速升降，横移仍用当前完整运动倍率。
				const float rain=s.weatherStation ? s.rainZombie[environment.Rain()] : worker.rawRainMultiplier;
				const float rainRatio=s.weatherStation ? GoldenIceRules::Amplify(rain,worker.goldenStacks)
					/std::max(.001f,GoldenIceRules::Amplify(worker.rawRainMultiplier,worker.goldenStacks)) : 1;
				const float fog=s.weatherStation && environment.Obscured(s,worker) ? WeatherStationRules::FogMoveMultiplier : 1;
				const float overload=(worker.overloadRemaining>0 ? GoldenIceRules::Amplify(2.2f,worker.goldenStacks) : 1)
					/GoldenIceRules::Amplify(worker.sampledOverload,worker.goldenStacks);
				const float curve=worker.movementCurve.Factor(u.x-worker.movementCurveBase)
					/worker.movementCurve.Factor(worker.movementCurveReference-worker.movementCurveBase);
				const float motion=rainRatio*fog*overload*curve*worker.goldenMoveRatios[worker.goldenStacks]
					*acceleration*ritualMove*drumMove*speedFactor;
				const float animation=GoldenIceRules::Amplify(worker.ladder.abilityRate,worker.goldenStacks)
					*GoldenIceRules::Amplify(rain,worker.goldenStacks)
					*(speedFactor<1 ? GoldenIceRules::Amplify(ZombieMovementRules::NormalSlowAnimationFactor,worker.goldenStacks) : 1);
				if(!ladders.empty()) PruneDeadLadderHosts(plants,ladderLivingPlants,ladders,constructionStats);
				if(AdvanceLadderAction(s,t,worker,contact,plants,ladders,activity[0],
					activity[0]*(speedFactor<1 ? .5f : 1),animation,motion,movingRight,kContact,constructionStats)) continue;
			}
			if (acceleration>1) ++constructionStats.goldenAccelerationSteps;
			if (!worker.inspiration.empty() && worker.goldenStacks>0) ++constructionStats.goldenDrumSteps;
			// 投篮车已经统一结算碾压；空弹药步行也不能再次啃食或重复伤害挡车坚果。
			if (contact >= 0 && (movingRight ? u.x>=plants[contact].x-kContact : u.x<=plants[contact].x+kContact) && !worker.catapult.present) {
				auto& p = plants[contact];
				float damage = worker.biteDps * (worker.overloadRemaining>0 ? 2 : 1)
                    / (worker.sampledOverload>1 ? 2 : 1) * ritualBite * drumBite * activity[1] * (speedFactor < 1 ? GoldenIceRules::Amplify(.5f,worker.goldenStacks) : 1);
				if (u.smashSeconds > 0) {
					smashTarget[i] = contact;
					advanceSmash(i,active,speedFactor);
					continue;
				}
				if (worker.vehicleCrush && p.crushDamage > 0) {
					if (active > 0 && DamagePlant(p,p.crushDamage,true,f) && p.health > 0) u.x += p.vehicleRetreat;
				} else if (worker.instantVehicleCrush) {
					// 普通植物按实体 Squish 压扁，不把车辆当成慢速啃食者；挡车坚果仍走上方承伤/推退。
					DamagePlant(p,p.health,true,f);
				} else if(!worker.headless) DamagePlant(p,damage,false,f);
			} else {
				// 车辆会随位置减速；保留采样时已有加速状态，不把当前车速冻结到整个时域。
				const float curve=worker.movementCurve.Factor(u.x-worker.movementCurveBase)
					/ worker.movementCurve.Factor(worker.movementCurveReference-worker.movementCurveBase);
				const float rainRatio=s.weatherStation ? GoldenIceRules::Amplify(s.rainZombie[environment.Rain()],worker.goldenStacks)/std::max(.001f,GoldenIceRules::Amplify(worker.rawRainMultiplier,worker.goldenStacks)) : 1;
                const float fogRatio=s.weatherStation && environment.Obscured(s,worker) ? WeatherStationRules::FogMoveMultiplier : 1;
                const float overload=(worker.overloadRemaining>0 ? GoldenIceRules::Amplify(2.2f,worker.goldenStacks) : 1)/GoldenIceRules::Amplify(worker.sampledOverload,worker.goldenStacks);
                if(worker.digger.present) {
                    const float rain=s.weatherStation ? s.rainZombie[environment.Rain()] : worker.rawRainMultiplier;
                    u.x += (movingRight ? 1 : -1)*worker.digger.WalkingSpeed()
                        *GoldenIceRules::Amplify(worker.digger.abilityMultiplier,worker.goldenStacks)
                        *GoldenIceRules::Amplify(movingRight ? worker.digger.rightWindMultiplier : worker.digger.leftWindMultiplier,worker.goldenStacks)
                        *GoldenIceRules::Amplify(rain,worker.goldenStacks)*fogRatio*drumMove*activity[0]*speedFactor;
                } else u.x -= u.speed * rainRatio * fogRatio * overload * curve * worker.goldenMoveRatios[worker.goldenStacks] * acceleration * ritualMove * drumMove * activity[0] * speedFactor;
				// 高速爆发不能跨步穿墙，车辆也不能越过仍存活的抗碾压坚果。
				if (contact >= 0 && (worker.burst.range > 0 || (worker.vehicleCrush && plants[contact].crushDamage > 0))) u.x = std::max(u.x,plants[contact].x+kContact);
			}
			if (worker.goldenDrive.enabled) worker.goldenDrive.undamaged=std::min(GoldenIceRules::ThirdAcceleration,worker.goldenDrive.undamaged+kStep);
		}
		enemyIce += f[0]-previousKills; // 已兑现击杀在下一逻辑步可付技能费，不能提前借用预测收入
		PruneDeadLadderHosts(plants,ladderLivingPlants,ladders,constructionStats);
		// 两版搜索共享正式清场/胜负语义：先过清洁车，再判断是否真的进屋。
		AdvanceMowers(s,mowers,units,t,s.rightEdge);
		for (size_t i = 0; i < units.size(); ++i) if (units[i].body.health > 0 && units[i].body.spawnAt <= t
			&& units[i].body.x < s.houseX && (!units[i].digger.present || units[i].digger.CanTriggerGameOver())) {
				// 进屋只触发一次胜利；重复穿过同一已失守防线不能按人数制造额外胜利收益。
				f[2] = 1; units[i].body.health = 0; breached[i] = true;
				if (constructionStats.breachSeconds < 0) constructionStats.breachSeconds = t;
			}
		for (size_t i = 0; i < units.size(); ++i) if (!refunded[i] && !breached[i] && units[i].body.health <= 0) {
			playerIce += units[i].playerRefund; refunded[i] = true;
            if(s.weatherStation && units[i].mistFuelReward>0) for(auto& p:plants) if(p.plantern && p.health>0) {
                float pending=0; for(const auto& delivery:p.lightDeliveries) pending+=delivery.second;
                const float accepted=std::max(0.0f,std::min({units[i].mistFuelReward,PlanternRules::FuelCapacity-p.lightFuel-pending,p.lightIntakeLimit-pending}));
                if(accepted>0) p.lightDeliveries.push_back({t+PlanternRules::FuelFlightSeconds,accepted});
                break;
            }
		}
		capPlayerResources();
		if (s.traceEconomy && static_cast<int>(t/kStep)%4 == 0)
			for (const auto& worker : units) if (worker.body.economic && worker.body.spawnAt <= t)
				constructionStats.workerTrace.push_back({worker.id,worker.body.row,t,worker.body.x,worker.body.health,0});
		// 正式胜负已经发生，后续制冰/建造/伤害均不再兑现，不能继续虚构胜利后的收入。
		if (f[2] > 0) break;
	}
	for (size_t i = 0; i < units.size(); ++i) if (units[i].body.health > 0) {
		const auto& u = units[i].body;
		f[3] += u.purchaseCost * std::clamp(u.health / std::max({1.0f, initialHealth[i], units[i].repair.totalMaximum}), 0.0f, 1.0f);
		f[7] += u.purchaseCost * std::clamp((initialX[i] - u.x) / 800, 0.0f, 1.0f);
	}
	for(const auto& path:ladders) if(std::none_of(s.ladders.begin(),s.ladders.end(),[&](const auto& old) {
		return old.row==path.row && old.column==path.column;
	})) ++constructionStats.siegeAccessProgress;
	// 建设只是现金换成植株资产；不能把玩家花钱补阵本身误当成被消耗。
	constructionStats.opponentAssets = playerIce+playerSun*s.sunIceValue;
	if (s.anticipateEconomy) {
		constructionStats.pendingIce = pendingIce;
		constructionStats.opponentAssets += pendingIce; // 已付款在途货物仍是玩家资产，不能伪造资源损失
	}
	for (const auto& plant : plants) if (plant.health > 0)
		constructionStats.plantAssets += plant.assetValue*std::clamp(plant.health/std::max(1.0f,plant.initialHealth),0.0f,1.0f);
    constructionStats.opponentAssets += constructionStats.plantAssets;
    for(const auto& plant:plants) constructionStats.plantAttackAssets+=plant.attackAssetCredit;
	if (construction) *construction = constructionStats;
	return f;
}

QueueRevision ReplanCommitted(Snapshot& s, const Weights& baseWeights, std::uint32_t seed) {
    s.rawWorldBinding={}; // 此入口会原地改已付队列；不得继承任何旧快照积分。
	QueueRevision revision;
	if (s.committed.empty() || (s.searchVersion != 1 && s.searchVersion != 2) || !ValidWeights(baseWeights)
		|| (s.stateModel && !s.stateModel->IsValid()) || !std::isfinite(s.opponentWeight)
		|| s.opponentWeight < 0 || s.opponentWeight > 100) return revision;
	// 重排只消费已验证的队列元数据；不能把误标的在场实体当成可移动的后备兵。
	std::vector<bool> marked(s.current.size());
	for (const auto& paid : s.committed) {
		if (paid.unit < 0 || static_cast<size_t>(paid.unit) >= s.current.size() || marked[paid.unit]) return revision;
		if (s.current[paid.unit].id != 0) return revision;
		const auto& body = s.current[paid.unit].body;
		if (body.row < 0 || body.row >= 6 || !paid.legalRows[body.row]
			|| !std::isfinite(body.spawnAt) || body.spawnAt < 0) return revision;
		marked[paid.unit] = true;
	}
	const auto original = s.current;
	const auto baseline = EvaluatePlan(s,baseWeights,{}, {},0);
	const auto conditioned = ConditionWeights(baseWeights,DescribeResultState(s,baseline),s.stateModel);
	const auto weights = s.netEconomy ? AccountForIce(conditioned,CapitalUtilityScale(s)) : conditioned;
	auto bestOutcome = EvaluatePlan(s,weights,{},baseline.features,baseline.opponentAssets);
	revision.beforeScore = revision.afterScore = bestOutcome.score;
	revision.beforeBreach = revision.afterBreach = bestOutcome.features[2] > 0;
	revision.evaluated = 1;
	auto best = original;
	std::mt19937 random(seed);
	for (int trial = 0; trial < kQueueTrials && !SearchTimeExpired(s); ++trial) {
		s.current = trial < 13 ? original : best;
		if (trial < 13) {
			// 同一已购编队既比较原时序改路，也比较提前；集中只是一组候选，不是强制策略。
			const int row = trial % 6;
			for (const auto& paid : s.committed) {
				auto& body = s.current[paid.unit].body;
				if (trial < 12 && paid.legalRows[row]) body.row = row;
				if (trial >= 6) body.spawnAt = 0;
			}
		} else {
			const auto& paid = s.committed[random()%s.committed.size()];
			auto& body = s.current[paid.unit].body;
			const int row = static_cast<int>(random()%6);
			if (paid.legalRows[row]) body.row = row;
			body.spawnAt = original[paid.unit].body.spawnAt * (random()%101)/100.0f;
		}
		auto outcome = EvaluatePlan(s,weights,{},baseline.features,baseline.opponentAssets);
		++revision.evaluated;
		if (BetterOutcome(outcome,bestOutcome)) {
			best = s.current; revision.afterScore = outcome.score; bestOutcome = std::move(outcome);
		}
	}
	revision.afterBreach = bestOutcome.features[2] > 0;
	s.current = std::move(best);
	for (const auto& paid : s.committed) {
		const auto& before = original[paid.unit].body;
		const auto& after = s.current[paid.unit].body;
		if (before.row != after.row || before.spawnAt != after.spawnAt) ++revision.changed;
	}
	return revision;
}

static Result EvaluateCandidateInSession(const Snapshot& input,const Weights& baseWeights,const std::vector<Action>& plan) {
	auto state=RawWorldCache::CopyView(input);
	const bool expanded=ExpandFundedPortfolio(state);
	Result initial;
	initial.features=EvaluateRawWorld(state,{},&initial.construction); Calibrate(state,initial);
	const auto inputs=DescribeResultState(state,initial);
	const auto conditioned=ConditionWeights(baseWeights,inputs,state.stateModel);
	const auto weights=state.netEconomy ? AccountForIce(conditioned,CapitalUtilityScale(state)) : conditioned;
	const auto baseline=EvaluatePlan(state,weights,{}, {},0,&initial);
	auto payable=plan;
	if(state.deploymentCapital>=0) Repair(state,payable); // 正式动态资本诊断与自由搜索共用整案修复；固定夹具仍直接评分。
	auto result=EvaluatePlan(state,weights,std::move(payable),baseline.features,baseline.opponentAssets);
	result.stateInputs=inputs; result.effectiveWeights=weights;
	result.capitalUtilityInputs=DescribeCapitalUtility(state);
	result.baselineBreachSeconds=baseline.construction.breachSeconds;
	result.expandedForecast=expanded;
	ExportFuturePrecision(state,result);
	return result;
}

Result EvaluateCandidate(const Snapshot& input,const Weights& weights,const std::vector<Action>& plan) {
    RawWorldCache cache(input.reuseRawWorlds);
    auto state=input;cache.BindRoot(state);
    auto result=EvaluateCandidateInSession(state,weights,plan);
    const auto counts=cache.Counts();
    result.rawWorldEvaluations=counts.evaluations;result.rawWorldCacheHits=counts.hits;
    result.rawWorldCacheEntries=counts.entries;result.rawWorldCacheBytes=counts.bytes;
    return result;
}

struct RawWorldSession::Impl {
    RawWorldCache cache;
    Snapshot state;
    explicit Impl(const Snapshot& input):cache(input.reuseRawWorlds),state(input) {cache.BindRoot(state);}
};

RawWorldSession::RawWorldSession(const Snapshot& state):mImpl(std::make_unique<Impl>(state)) {}
RawWorldSession::~RawWorldSession()=default;
const Snapshot& RawWorldSession::State() const {return mImpl->state;}
RawWorldCounts RawWorldSession::Counts() const {
    return mImpl->cache.Counts();
}
RawWorldOutcome RawWorldSession::Evaluate(const RawWorldRequest& request) {
    const auto& state=mImpl->state;
    RawWorldOutcome result;
    CheckForecastCancellation(state);
    if(SearchTimeExpired(state) || request.observedProfile< -1
        || request.observedProfile>=static_cast<int>(state.observedResponse.profiles.size())
        || request.searchVersion<0 || request.searchVersion>2) return result;
    for(const auto& action:request.actions) if(action.option<0 || action.option>=static_cast<int>(state.options.size())
        || !FinitePhasedValue(action.delay) || action.delay<0 || action.delay>300) return result;
    auto world=RawWorldCache::CopyView(state);
    world.observedProfile=request.observedProfile<0 ? nullptr : &state.observedResponse.profiles[request.observedProfile];
    if(request.searchVersion!=0) world.searchVersion=request.searchVersion;
    world.diagnosticVisibleCounterWitness|=request.visibleCounterWitness;
    if(request.precisionTargetID>=0) {
        world.precisionTargetID=request.precisionTargetID;
        world.precisionAdditionalTargetIDs=request.precisionAdditionalTargetIDs;
    }
    result.features=EvaluateRawWorld(world,request.actions,&result.construction,request.counterHoldSeconds,
        request.storedHoldSeconds,request.rowStrikeHoldSeconds,request.reserveCounterSpace,request.clearFogWhenReady,
        request.planternResponseGear,request.preserveManualAuras,request.shovelCounterSpace,
        request.preservePaidDefenses,request.interferenceBeforeAsh,request.enableFollowup ? &request.followup : nullptr);
    CheckForecastCancellation(state);
    result.complete=!SearchTimeExpired(state);
    return result;
}

/** 所有实验臂从同一当前快照校准；首批不修剪，资本基线始终是被动零采购。 */
Result EvaluatePhasedCounterfactual(const Snapshot& input,const Weights& baseWeights,
    const std::vector<Action>& first,const FollowupPolicy& followup,bool enableFollowup,float sharedResponseEnvelope) {
    auto state=RawWorldCache::CopyView(input);
    // 尚未解锁的技能没有付款资格，后队也不借采购推进波次提前开放它。
    if(state.precisionUnlockAfterPurchase) { state.precisionTargetID=0; state.precisionAdditionalTargetIDs.clear(); }
    state.precisionUnlockAfterPurchase=false;
    const bool expanded=ExpandFundedPortfolio(state);
    std::int64_t bill=PrecisionCost(state), futureBill=0;
    int troops=0;
    bool legal=ValidWeights(baseWeights) && FinitePhasedValue(sharedResponseEnvelope)
        && sharedResponseEnvelope>=0 && sharedResponseEnvelope<=300;
    std::set<int> devices;
    for(const auto& action:first) {
        if(action.option<0 || action.option>=static_cast<int>(state.options.size())
            || !FinitePhasedValue(action.delay) || action.delay<0 || action.delay>300) { legal=false; break; }
        const auto& option=state.options[action.option];
        if(option.cost<=0 || option.row<0 || option.row>=state.rows) { legal=false; break; }
        bill+=option.cost;
        if(option.device<0) ++troops;
        else if(!devices.insert(option.device).second) legal=false;
    }
    if(enableFollowup && !ValidFollowup(state,followup,futureBill)) legal=false;
    if(!legal || bill>state.budget || troops>DeploymentCapacity(state,DeploymentDeviceCost(state,first))) {
        Result invalid; invalid.actions=first; invalid.construction.followup.status=FollowupForecast::Status::Invalid; return invalid;
    }
    Result initial;
    initial.features=EvaluateRawWorld(state,{},&initial.construction); Calibrate(state,initial);
    const auto inputs=DescribeResultState(state,initial);
    const auto conditioned=ConditionWeights(baseWeights,inputs,state.stateModel);
    const auto weights=state.netEconomy ? AccountForIce(conditioned,CapitalUtilityScale(state)) : conditioned;
    float envelope=sharedResponseEnvelope;
    for(const auto& action:first) envelope=std::max(envelope,action.delay);
    if(enableFollowup) for(const auto& action:followup.actions) envelope=std::max(envelope,followup.expirySeconds+action.delay);
    const auto baseline=EvaluatePlan(state,weights,{}, {},0,&initial,nullptr,envelope);
    auto result=EvaluatePlan(state,weights,first,baseline.features,baseline.opponentAssets,nullptr,
        enableFollowup ? &followup : nullptr,envelope);
    result.effectiveWeights=weights; result.stateInputs=inputs; result.capitalUtilityInputs=DescribeCapitalUtility(state);
    result.baselineBreachSeconds=baseline.construction.breachSeconds; result.expandedForecast=expanded;
    return result;
}

Result EvaluatePhasedCandidate(const Snapshot& state,const Weights& weights,
    const std::vector<Action>& first,const FollowupPolicy& followup) {
    return EvaluatePhasedCounterfactual(state,weights,first,followup,true,0);
}

PhasedPlanComparison ComparePhasedPlan(const Snapshot& input,const Weights& weights,
    const PhasedPlan& plan,const std::vector<Action>& incumbent) {
    PhasedPlanComparison comparison;
    if(SearchTimeExpired(input)) return comparison;
    auto state=RawWorldCache::CopyView(input);
    if(state.precisionUnlockAfterPurchase) { state.precisionTargetID=0; state.precisionAdditionalTargetIDs.clear(); }
    state.precisionUnlockAfterPurchase=false;
    ExpandFundedPortfolio(state);
    auto deferred=plan.followup;
    deferred.actions.insert(deferred.actions.begin(),plan.first.begin(),plan.first.end());
    float envelope=0;
    for(const auto& action:deferred.actions) envelope=std::max(envelope,deferred.expirySeconds+action.delay);
    for(const auto& action:incumbent) envelope=std::max(envelope,action.delay);
    comparison.full=EvaluatePhasedCounterfactual(state,weights,plan.first,plan.followup,true,envelope);
    if(comparison.full.construction.followup.status==FollowupForecast::Status::Invalid || SearchTimeExpired(input)) return comparison;
    const auto& full=comparison.full;
    const auto evaluate=[&](const std::vector<Action>& actions,const FollowupPolicy* followup) {
        auto result=EvaluatePlan(state,full.effectiveWeights,actions,full.baselineFeatures,full.baselineOpponentAssets,
            nullptr,followup,envelope);
        result.effectiveWeights=full.effectiveWeights; result.stateInputs=full.stateInputs;
        result.capitalUtilityInputs=full.capitalUtilityInputs; result.baselineBreachSeconds=full.baselineBreachSeconds;
        result.expandedForecast=full.expandedForecast;
        return result;
    };
    comparison.prefix=evaluate(plan.first,nullptr);
    if(SearchTimeExpired(input)) return comparison;
    comparison.wait=evaluate({},&plan.followup);
    if(SearchTimeExpired(input)) return comparison;
    comparison.deferred=evaluate({},&deferred);
    if(comparison.deferred.construction.followup.status==FollowupForecast::Status::Invalid || SearchTimeExpired(input)) return comparison;
    // 公平等待也可按同一时刻直接买后队/整队，不要求空场先诱出一个不可能发生的炮击。
    if(plan.followup.requireObservedCobLaunch || plan.followup.requireShotsClear || plan.followup.maxReadyCobs>=0) {
        auto timed=plan.followup; timed.requireObservedCobLaunch=false; timed.requireShotsClear=false; timed.maxReadyCobs=-1;
        auto waiting=evaluate({},&timed);
        if(BetterOutcome(waiting,comparison.wait)) comparison.wait=std::move(waiting);
        if(SearchTimeExpired(input)) return comparison;
        timed.actions=deferred.actions;
        auto whole=evaluate({},&timed);
        if(BetterOutcome(whole,comparison.deferred)) comparison.deferred=std::move(whole);
        if(SearchTimeExpired(input)) return comparison;
    }
    const bool identical=incumbent.size()==plan.first.size() && std::equal(incumbent.begin(),incumbent.end(),plan.first.begin(),
        [](const Action& a,const Action& b) {return a.option==b.option && a.delay==b.delay;});
    comparison.incumbent=identical ? comparison.prefix : evaluate(incumbent,nullptr);
    comparison.complete=!SearchTimeExpired(input);
    return comparison;
}

/** 只检查当前快照中的整案资格；预留搜索时间与真正推演共用，不修剪旧提案或预支未来收入。 */
static bool ObservedPlanAdmissible(const Snapshot& state,const PhasedPlan& plan,bool enableFollowup,
    float allowance,ObservedDecisionDiagnostics* diagnostic=nullptr) {
    if(!FinitePhasedValue(allowance) || allowance<0) return false;
    std::int64_t bill=PrecisionCost(state),futureBill=0;
    int troops=0;
    for(const auto& action:plan.first) {
        if(action.option<0 || action.option>=static_cast<int>(state.options.size()) || !FinitePhasedValue(action.delay)
            || action.delay<0 || action.delay>300) return false;
        const auto& option=state.options[action.option];
        if(option.device>=0 || option.cost<=0 || option.row<0 || option.row>=state.rows
            || option.unit.body.row!=option.row || !FinitePhasedValue(option.unit.body.health)
            || option.unit.body.health<=0) return false;
        bill+=option.cost; ++troops;
        if(diagnostic) diagnostic->candidate.push_back({option.type,option.row,option.cost,action.delay,false});
    }
    if(enableFollowup && !ValidFollowup(state,plan.followup,futureBill)) return false;
    if(enableFollowup && diagnostic) for(const auto& action:plan.followup.actions) {
        const auto& option=state.options[action.option];
        diagnostic->candidate.push_back({option.type,option.row,option.cost,action.delay,true});
    }
    if(diagnostic) diagnostic->plannedBill=static_cast<float>(bill+futureBill);
    if(bill>state.budget || troops>DeploymentCapacity(state)) return false;
    if(static_cast<double>(bill+futureBill)>allowance) {
        if(diagnostic) diagnostic->allowanceRejected=1;
        return false;
    }
    if(enableFollowup) {
        auto deferred=plan.followup;
        deferred.actions.insert(deferred.actions.begin(),plan.first.begin(),plan.first.end());
        std::int64_t wholeBill=0;
        if(!ValidFollowup(state,deferred,wholeBill)) return false;
    }
    return true;
}

/** 两种完整响应包络采用同一组权重、时域和真账本；不把习惯世界的收益加到最坏世界上。 */
ObservedPlanComparison CompareObservedPlan(const Snapshot& input,const Weights& baseWeights,
    const PhasedPlan& plan,const std::vector<Action>& incumbent,const ColdStorageResponse::Profile& profile,
    bool enableFollowup) {
    ObservedPlanComparison comparison;
    auto& diagnostic=comparison.diagnostics;
    diagnostic.profiles=static_cast<int>(input.observedResponse.profiles.size());
    diagnostic.revision=input.observedResponse.revision;
    diagnostic.windowSeconds=profile.windowSeconds; diagnostic.weight=profile.weight;
    diagnostic.riskCap=ObservedPurchaseAllowance(input); diagnostic.phased=enableFollowup;
    if(SearchTimeExpired(input)) {diagnostic.incomplete=1;return comparison;}
    if(!ValidWeights(baseWeights) || !FinitePhasedValue(profile.weight) || profile.weight<=0 || profile.weight>.65f
        || !FinitePhasedValue(profile.windowSeconds) || profile.windowSeconds<=0 || profile.windowSeconds>24)
        return comparison;
    auto state=RawWorldCache::CopyView(input); state.observedProfile=nullptr;
    if(state.precisionUnlockAfterPurchase) {state.precisionTargetID=0;state.precisionAdditionalTargetIDs.clear();}
    state.precisionUnlockAfterPurchase=false;
    const bool expanded=ExpandFundedPortfolio(state);
    if(!ObservedPlanAdmissible(state,plan,enableFollowup,diagnostic.riskCap,&diagnostic)) return comparison;
    auto deferred=plan.followup;
    deferred.actions.insert(deferred.actions.begin(),plan.first.begin(),plan.first.end());
    float envelope=0;
    for(const auto& action:plan.first) envelope=std::max(envelope,action.delay);
    for(const auto& action:incumbent) envelope=std::max(envelope,action.delay);
    if(enableFollowup) for(const auto& action:deferred.actions)
        envelope=std::max(envelope,deferred.expirySeconds+action.delay);
    Result initial; initial.features=EvaluateRawWorld(state,{},&initial.construction); Calibrate(state,initial);
    const auto inputs=DescribeResultState(state,initial);
    const auto conditioned=ConditionWeights(baseWeights,inputs,state.stateModel);
    const auto weights=state.netEconomy ? AccountForIce(conditioned,CapitalUtilityScale(state)) : conditioned;
    const auto same=[](const std::vector<Action>& a,const std::vector<Action>& b) {
        return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const Action& x,const Action& y) {
            return x.option==y.option && x.delay==y.delay;
        });
    };
    const auto evaluateWorld=[&](bool behavior,PhasedPlanComparison& arms,Result& passive,Result& timedWait,Result& timedDeferred) {
        auto world=RawWorldCache::CopyView(state);world.observedProfile=behavior ? &profile : nullptr;
        passive=EvaluatePlan(world,weights,{}, {},0,behavior ? nullptr : &initial,nullptr,envelope);
        // 被动世界的资产是基线本身；EvaluatePlan 的临时零资产参照不能重复记一次负资产。
        passive.score=Score(passive.features,weights);passive.opponentScore=0;
        passive.baselineFeatures=passive.features;passive.baselineOpponentAssets=passive.opponentAssets;
        passive.effectiveWeights=weights;passive.stateInputs=inputs;passive.capitalUtilityInputs=DescribeCapitalUtility(state);
        passive.baselineBreachSeconds=passive.construction.breachSeconds;passive.expandedForecast=expanded;
        if(SearchTimeExpired(input)) return false;
        const auto evaluate=[&](const std::vector<Action>& actions,const FollowupPolicy* followup) {
            auto result=EvaluatePlan(world,weights,actions,passive.features,passive.opponentAssets,nullptr,followup,envelope);
            result.effectiveWeights=weights;result.stateInputs=inputs;result.capitalUtilityInputs=DescribeCapitalUtility(state);
            result.baselineBreachSeconds=passive.construction.breachSeconds;result.expandedForecast=expanded;
            return result;
        };
        arms.full=evaluate(plan.first,enableFollowup ? &plan.followup : nullptr);
        if(SearchTimeExpired(input)) return false;
        arms.prefix=enableFollowup ? evaluate(plan.first,nullptr) : arms.full;
        if(SearchTimeExpired(input)) return false;
        if(enableFollowup) {
            arms.wait=evaluate({},&plan.followup);
            if(SearchTimeExpired(input)) return false;
            arms.deferred=evaluate({},&deferred);
            if(SearchTimeExpired(input)) return false;
            // 原条件等待与定时等待是两个完整策略；两套世界不能分别挑不同条件后再拼接。
            if(plan.followup.requireObservedCobLaunch || plan.followup.requireShotsClear || plan.followup.maxReadyCobs>=0) {
                auto timed=plan.followup;timed.requireObservedCobLaunch=false;timed.requireShotsClear=false;timed.maxReadyCobs=-1;
                timedWait=evaluate({},&timed);
                if(SearchTimeExpired(input)) return false;
                timed.actions=deferred.actions;
                timedDeferred=evaluate({},&timed);
                if(SearchTimeExpired(input)) return false;
            } else {timedWait=arms.wait;timedDeferred=arms.deferred;}
        } else {arms.wait=passive;arms.deferred=passive;timedWait=passive;timedDeferred=passive;}
        arms.incumbent=same(incumbent,plan.first) ? arms.prefix : incumbent.empty() ? passive : evaluate(incumbent,nullptr);
        arms.complete=!SearchTimeExpired(input);
        return arms.complete;
    };
    if(!evaluateWorld(false,comparison.robust,comparison.robustPassive,comparison.robustTimedWait,comparison.robustTimedDeferred)
        || !evaluateWorld(true,comparison.behavior,comparison.behaviorPassive,comparison.behaviorTimedWait,comparison.behaviorTimedDeferred)) {
        diagnostic.incomplete=1;return comparison;
    }
    diagnostic.robustDelayedWorlds=comparison.robust.full.observedDelayedWorlds;
    diagnostic.behaviorDelayedWorlds=comparison.behavior.full.observedDelayedWorlds;
    comparison.complete=true;diagnostic.evaluated=1;
    return comparison;
}

float ObservedPurchaseAllowance(const Snapshot& state) {
    float outstanding=0;
    for(const auto& unit:state.current) if(unit.body.health>0) outstanding+=std::max(0.0f,unit.body.purchaseCost);
    if(state.deploymentCapital>=0) outstanding=std::max(outstanding,
        static_cast<float>(std::max<std::int64_t>(0,state.deploymentCapital-state.budget)));
    return ColdStorageResponse::PurchaseRiskCap(static_cast<float>(state.budget),static_cast<float>(state.recoveryReserve),
        state.capitalRiskAllowance,outstanding);
}

/** 类型、profile、路线与规模采用嵌套轮转，分阶段插槽不能永久遮掉某一合法能力。 */
static std::vector<std::vector<int>> ObservedPurchaseGroups(const Snapshot& state,float allowance) {
    std::vector<std::vector<int>> types;
    if(!FinitePhasedValue(allowance) || allowance<=0) return types;
    for(size_t i=0;i<state.options.size();++i) {
        const auto& option=state.options[i];
        if(!CanForecastFollowup(option) || option.row<0 || option.row>=state.rows || option.unit.body.row!=option.row
            || option.cost>allowance || option.cost>PurchaseBudget(state)) continue;
        auto group=std::find_if(types.begin(),types.end(),[&](const auto& row) {return state.options[row.front()].type==option.type;});
        if(group==types.end()) types.push_back({static_cast<int>(i)}); else group->push_back(static_cast<int>(i));
    }
    return types;
}

std::size_t ObservedPurchaseProfileIndex(const Snapshot& state,float allowance) {
    const size_t types=std::max<size_t>(1,ObservedPurchaseGroups(state,allowance).size());
    const size_t profiles=std::max<size_t>(1,state.observedResponse.profiles.size());
    return (state.searchRound-state.searchRound/4)/types%profiles;
}

std::vector<Action> BuildObservedPurchaseCandidate(const Snapshot& state,float allowance) {
    const auto types=ObservedPurchaseGroups(state,allowance);
    const int capacity=DeploymentCapacity(state);
    if(types.empty() || capacity<=0) return {};
    const size_t directTurn=state.searchRound-state.searchRound/4;
    const size_t profiles=std::max<size_t>(1,state.observedResponse.profiles.size());
    const auto& routes=types[directTurn%types.size()];
    const size_t routeTurn=directTurn/(types.size()*profiles);
    const size_t route=routeTurn%routes.size();
    const int selected=routes[route];
    std::vector<Action> candidate{{selected,0}};
    const size_t sizeTurn=routeTurn/routes.size();
    const size_t count=std::min<size_t>({1+sizeTurn%3,static_cast<size_t>(capacity),
        static_cast<size_t>(allowance/state.options[selected].cost)});
    std::int64_t bill=state.options[selected].cost;
    for(size_t n=1;n<count;++n) {
        const int option=routes[(route+n)%routes.size()];
        if(bill+state.options[option].cost>allowance || bill+state.options[option].cost>PurchaseBudget(state)) break;
        bill+=state.options[option].cost; candidate.push_back({option,0});
    }
    return candidate;
}

/** 在给定技能意图下搜索编队；正式入口另外比较不施法的机会成本。 */
static Result SearchFormation(const Snapshot& s, const Weights& baseWeights, std::uint32_t seed, PlanEvaluator* evaluator) {
	Result best;
    // 小队阶段最多占用剩余预算的一半，为必要的整队长时域重搜留下空间。
    auto begin=std::chrono::steady_clock::now();
    const auto explorationDeadline=s.searchVersion==1 ? begin+(s.searchDeadline-begin)/2 : s.searchDeadline;
    auto phaseDeadline=begin+(explorationDeadline-begin)*kInitialSearchBudgetPercent/100;
    bool timeLimited=false;
    const auto withinBudget=[&]() {
        const bool allowed=!s.timeLimitedSearch || std::chrono::steady_clock::now()<phaseDeadline;
        timeLimited|=!allowed;
        return allowed;
    };
	if ((s.searchVersion != 1 && s.searchVersion != 2) || !ValidWeights(baseWeights) || (s.stateModel && !s.stateModel->IsValid())
		|| !std::isfinite(s.opponentWeight) || s.opponentWeight < 0 || s.opponentWeight > 100) return best;
	best.features = EvaluateRawWorld(s, {}, &best.construction); Calibrate(s,best);
	const auto inputs = DescribeResultState(s,best);
	const auto conditioned = ConditionWeights(baseWeights,inputs,s.stateModel);
	const auto weights = s.netEconomy ? AccountForIce(conditioned,CapitalUtilityScale(s)) : conditioned;
	// 不增援也要承受相同的对手模型，不能拿乐观等待基线去比较保守进攻结果。
	best = EvaluatePlan(s,weights,{}, {},0,&best);
	const float baselineOpponentAssets = best.opponentAssets;
	const float baselineBreachSeconds=best.construction.breachSeconds;
	best.baselineBreachSeconds=baselineBreachSeconds;
	best.baselineOpponentAssets = baselineOpponentAssets; best.opponentScore = 0;
	best.stateInputs = inputs; best.effectiveWeights = weights;
	best.capitalUtilityInputs=DescribeCapitalUtility(s);
	best.baselineFeatures = best.features; best.score = Score(best.features, weights); best.evaluated = 1;
	if (s.options.empty() || (DeploymentCapacity(s) <= 0 && !s.weatherStation) || PurchaseBudget(s) <= 0) {
		best.assaultDiagnostics.continuationUnavailable=!s.assaultContinuation.empty();
		return best;
	}
	const auto cheapest = std::min_element(s.options.begin(),s.options.end(),[](const auto& a,const auto& b) { return a.cost < b.cost; });
	if (cheapest->cost > PurchaseBudget(s)) {
		best.assaultDiagnostics.continuationUnavailable=!s.assaultContinuation.empty();
		return best;
	}
	// 等待基线也消耗真实预算。各探索阶段只分剩余时间，否则密集旧军团会在
	// 基线结束前用完随机/提案阶段，虽有完整大队名额却来不及深化攻城。
	begin=std::chrono::steady_clock::now();
	phaseDeadline=begin+(explorationDeadline-begin)*kInitialSearchBudgetPercent/100;
	const bool prioritizeAssault=s.timeLimitedSearch && s.netEconomy && CapitalUtilityScale(s)<1;
	std::mt19937 rng(seed); // 局部共同随机数使同一快照/参数可重复，搜索次数不改变正式战斗随机流。
	std::vector<Result> elite{best};
	bool hasChoice = s.allowWait; // 正式构建启用 fast-math，不能用无穷大充当尚无候选的哨兵。
	bool deferredInvestment = false;
	int capitalRejected = 0;
	std::vector<CandidateStats> candidateStats;
	const auto rejectInvestment = [&](const Result& candidate) {
		const int denial = ShouldRegroup(candidate,s.budget,s.recoveryReserve) ? 1
			: s.netEconomy && ShouldConserveCapital(candidate,s.budget,s.recoveryReserve,
				s.allowWait ? s.capitalRiskAllowance : (std::numeric_limits<float>::max)()) ? 2 : 0;
		RecordCandidate(s,candidate,denial,candidateStats);
		if (denial == 2) ++capitalRejected;
		return denial != 0;
	};
	const int trials = s.searchVersion == 2 ? kPortfolioTrials : kTrials;
	// 在原有预算内最多拿一半做兵种覆盖，余下仍用于组合变异和完整编队探索。
	const auto coverage = SampleTypeCoverage(s,rng,trials/2);
	const auto initialGroups = LegalOptionGroups(s,rng);
	// 生产画像只用于打开经营目标的探索入口；搭档仍遍历全部合法类型，不指定保护名单或必买数量。
	std::vector<int> incomeSeeds;
	if(s.netEconomy && weights[4]>0 && ActionLimit(s)>=3) for(const auto& group:initialGroups)
		for(int option:group) if(s.options[option].unit.body.economic) incomeSeeds.push_back(option);
	InvestmentFrontier investments(best.construction.workerProtectionProgress);
	AssaultFrontier assaults(best.construction.siegeAccessProgress);
	const auto incomeShape=[&](size_t index) {
		// 先比较同步搭档，下一轮直接试先行，而不是先遍历四轮数量才试时序。
		// 按轮次轮转三种姿态与数量，兵池大小不会把某类型锁在同一形状。
		const size_t round=index/initialGroups.size();
		const int count=s.timeLimitedSearch && (index+round)%3==2 ? 2 : static_cast<int>(round/3)%kRefinementMaxReplacements;
		return static_cast<int>((1+2*round)%3)*kRefinementMaxReplacements+count;
	};
	size_t incomeSeedIndex=0, incomeGroupIndex=0;
	int incomeEvaluated=0;
	int assaultEvaluated=0, proposalEvaluated=0;
	size_t proposalIndex=0;
	size_t assaultGroupIndex=0;
	const auto pairs=CombinationPairs(s,initialGroups,rng);
	auto experienced=BuildExperiencedFormations(s,std::min(ActionLimit(s),DeploymentCapacity(s)),PurchaseBudget(s));
	// 紧预算可能只轮到首案；跨快照轮转已生成的完整经验案，不增加预算或改变不限时测试顺序。
	if(s.timeLimitedSearch && !experienced.empty())
		std::rotate(experienced.begin(),experienced.begin()+s.searchRound%experienced.size(),experienced.end());
	size_t experiencedIndex=0;
	int experiencedEvaluated=0,experiencedAshEvaluated=0;
	size_t pairIndex=0;
	const auto samplePair=[&](bool simultaneous) {
		const size_t index=pairIndex%pairs.size(), round=pairIndex++/pairs.size();
		return SampleCombination(s,initialGroups,pairs[index],rng,simultaneous,static_cast<int>((index+round)%12));
	};
	int duplicatesSkipped=0, unevenMixEvaluated=0, evaluatedPlans=1, widestComposition=0;
	std::set<std::vector<std::pair<int,float>>> seenPlans;
	seenPlans.insert(std::vector<std::pair<int,float>>{}); // 等待基线已经完整算过，空案不再反复积分。
	const auto preparePlan=[&](std::vector<Action>& plan) {
		Repair(s,plan);
		if(!s.allowWait && !HasPaidTroops(s,plan)) return false;
		if (!s.allowWait && !plan.empty()) plan.front().delay=0;
		std::vector<std::pair<int,float>> key;
		for (const auto& action:plan) key.emplace_back(action.option,action.delay);
		// 保留相同时刻的提交顺序：出生序列会影响并列目标选择，不能把不同顺序误合并。
		if (!seenPlans.insert(std::move(key)).second) { ++duplicatesSkipped; return false; }
		++evaluatedPlans;
		widestComposition=std::max(widestComposition,TroopCompositionSize(s,plan));
		std::vector<std::pair<std::pair<int,int>,int>> counts;
		for (const auto& action:plan) {
			const auto& option=s.options[action.option];
			const auto group=std::make_pair(option.type,option.cost);
			auto found=std::find_if(counts.begin(),counts.end(),[&](const auto& entry) { return entry.first==group; });
			if (found==counts.end()) counts.push_back({group,1}); else ++found->second;
		}
		if (counts.size()>1 && std::any_of(counts.begin()+1,counts.end(),[&](const auto& entry) {
			return entry.second!=counts.front().second;
		})) ++unevenMixEvaluated;
		return true;
	};
	int combinationEvaluated = 0, cohortEvaluated = 0, reinforcementEvaluated = 0, refinementEvaluated = 0;
	int spreadCohortEvaluated=0, timingEvaluated=0;
	int continuationEvaluated=0,continuationUnavailable=0,continuationValueRejected=0,continuationCapitalRejected=0;
	std::vector<Result> assaultCandidates;
	size_t timingIndex=0;
	Result cohortAnchor;
	bool hasCohortAnchor=false;
    size_t reinforcementIndex=0;
	int largestPlan = 0;
    size_t coverageIndex = 0;
	// 排名、随机数和候选记账只由协调线程修改；辅助线程仅借用本作用域的只读快照。
	const auto acceptCandidate=[&](Result candidate,bool cohort) {
		if(candidate.experiencedAshSelected) ++experiencedAshEvaluated;
		// 短期意图只保留已完整算过的构成；即使本轮亏损，也没有付款权或旧收益。
		if(candidate.actions.size()>=4 && cohort) {
			assaultCandidates.push_back(candidate);
			std::stable_sort(assaultCandidates.begin(),assaultCandidates.end(),[](const Result& a,const Result& b) {
				if(const int victory=CompareVictory(a,b)) return victory>0;
				return a.score>b.score;
			});
			if(assaultCandidates.size()>2) assaultCandidates.resize(2);
		}
		if(candidate.assaultContinuationSelected) {
			if(!BetterOutcome(candidate,best)) ++continuationValueRejected;
			if(ShouldRegroup(candidate,s.budget,s.recoveryReserve)
				|| (s.netEconomy && ShouldConserveCapital(candidate,s.budget,s.recoveryReserve,s.capitalRiskAllowance)))
				++continuationCapitalRejected;
		}
		investments.Observe(s,candidate);
		assaults.Observe(s,candidate); // 资本拒绝只禁止付款，不能提前截断尚未完成的攻城协同。
		if(cohort && !candidate.actions.empty() && (!hasCohortAnchor || BetterOutcome(candidate,cohortAnchor))) {
			cohortAnchor=candidate; hasCohortAnchor=true;
		}
		if(rejectInvestment(candidate)) { deferredInvestment=true; return; }
		if((s.allowWait || !candidate.actions.empty()) && (!hasChoice || BetterOutcome(candidate,best))) {
			best=candidate; hasChoice=true;
		}
		elite.push_back(std::move(candidate));
		std::stable_sort(elite.begin(),elite.end(),[](const auto& a,const auto& b) {
			if(const int victory=CompareVictory(a,b)) return victory>0;
			return a.score>b.score;
		});
		if(elite.size()>8) elite.resize(8);
	};
	struct PendingEvaluation {
		std::future<Result> result;
		bool cohort=false;
		~PendingEvaluation() { if(result.valid()) result.wait(); } // 异常展开也须先完成借用，再销毁快照/权重。
	} pendingEvaluation;
	const auto collect=[&](bool wait) {
		if(!pendingEvaluation.result.valid()) return;
		if(!wait && pendingEvaluation.result.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
		acceptCandidate(pendingEvaluation.result.get(),pendingEvaluation.cohort);
	};
	const Weights baselineFeatures=best.baselineFeatures;
	const auto evaluatePrepared=[&](std::vector<Action> plan,bool cohort,bool synchronous=false,bool experience=false,bool continuation=false) {
		collect(false);
		const bool ashExperience=experience && IsAshSupportedFormation(s,plan);
		if(evaluator && !synchronous && !pendingEvaluation.result.valid()) {
			pendingEvaluation.cohort=cohort;
			pendingEvaluation.result=evaluator->Submit(std::packaged_task<Result()>(
				[&s,weights,baselineFeatures,baselineOpponentAssets,experience,ashExperience,continuation,plan=std::move(plan)]() mutable {
					auto result=EvaluatePlan(s,weights,std::move(plan),baselineFeatures,baselineOpponentAssets);
					result.experiencedSelected=experience; result.assaultContinuationSelected=continuation;
					result.experiencedAshSelected=ashExperience;
					return result;
				}));
		} else {
			// 辅助线程在算长案时，协调线程继续算其他候选；不等一批全完才更新优案。
			auto result=EvaluatePlan(s,weights,std::move(plan),baselineFeatures,baselineOpponentAssets);
			result.experiencedSelected=experience; result.assaultContinuationSelected=continuation;
			result.experiencedAshSelected=ashExperience;
			acceptCandidate(std::move(result),cohort);
			collect(false);
		}
	};


	// 每次只占原预算的一个早期槽，跨轮轮转类别，避免互相遮挡的同余编号把
	// 未付款提案/时序永远排到第十余案。真实反制事件到来时优先重评增援，
	// 原阵地、在途弹、已付小队和当前钱包都来自本次新快照。
	if(s.searchVersion==2 && withinBudget()) {
		std::vector<Action> early;
		bool continuation=false,timed=false;
		if(!s.assaultContinuation.empty()) {
			early=RestoreProposal(s,s.assaultContinuation[s.searchRound%s.assaultContinuation.size()],s.assaultContinuationRelativeTiming);
			continuation=true;
            if(!s.assaultContinuationRelativeTiming) for(auto& action:early) action.delay=0;
            // 条件后队延迟相对本次真实付款，保留首员的非零延迟；旧观察提案比较立即增援。
			// 未付款整队不足额/缩容后整案放弃，不能偷偷截成单兵并称原协同已重评。
			auto repaired=early; Repair(s,repaired);
			if(repaired.size()!=early.size() || !std::equal(early.begin(),early.end(),repaired.begin(),[](const Action& a,const Action& b) {
				return a.option==b.option && a.delay==b.delay;
			})) early.clear();
			if(early.empty()) ++continuationUnavailable;
		} else if(s.timeLimitedSearch && s.searchRound%3==1 && !s.proposals.empty()) {
			early=RestoreProposal(s,s.proposals.front());
		} else if(s.timeLimitedSearch && s.searchRound%3==2) {
			auto full=SamplePortfolio(s,rng,0);
			Repair(s,full);
			const auto schedules=BuildAssaultSchedules(s,full,DelayLimit(s));
			if(!schedules.empty()) {early=schedules[(s.searchRound/3)%schedules.size()]; timed=true;}
		}
		if(!early.empty() && preparePlan(early)) {
			largestPlan=std::max(largestPlan,static_cast<int>(early.size()));
			if(continuation) ++continuationEvaluated;
			else if(timed) ++timingEvaluated;
			else ++proposalEvaluated;
			evaluatePrepared(std::move(early),true,true,false,continuation);
		}
	}

	for (int trial = 1; trial < trials && withinBudget(); ++trial) {
		auto plan = elite[rng() % elite.size()].actions;
		// 每三个首轮名额最多一个用于经验完整案，剩余继续自由组合、变异与协同深化。
		// 经验案也进入原精英/经营/攻城前沿，后续可删成员、替换兵种及改路线，而非独立固定 AI。
		const bool experiencedTrial=trial%3==2 && experiencedIndex<experienced.size();
		// 独立抽完整队伍，允许跨过“单只亏损、协同才盈利”的谷底，不强制任何兵种模板。
		const bool portfolioTrial = !experiencedTrial && s.searchVersion == 2 && trial % 4 == 1;
		// 工人的补搭档时段必须保留；进攻深化从通用变异时段取预算，不能只剩反复裸工人的入口。
		// 富余钱包先重评自己已发现的构成，再加深攻城；仍轮转经营和新抽样，
		// 不增加总预算，不指定兵种或采购义务。
		const bool proposalTrial=!experiencedTrial && trial%8==(prioritizeAssault ? 2 : 4) && proposalIndex<s.proposals.size();
		const bool assaultTrial=!experiencedTrial && !proposalTrial && ((prioritizeAssault ? trial%4==0 : trial%8==0)
			|| (incomeSeeds.empty() && trial%4==2)) && !assaults.Empty();
		const bool incomeTrial=!experiencedTrial && trial%4==2 && !incomeSeeds.empty() && !assaultTrial;
        const bool cooperationTrial = !experiencedTrial && trial % 4 == 3 && !pairs.empty();
        const bool reinforcementTrial=cooperationTrial && trial%8==3
            && (hasCohortAnchor || !best.actions.empty());
		// 完整案的一部分后续名额轮换时序，仍保留首案和新构成；否则富余阶段所有普通槽都可能已被协作占满。
        // 不挤掉跨轮提案、经营和经验入口，未产生新案时继续原分支。
		const bool timingSlot=portfolioTrial && trial>1 && (trial/4)%2==1;
		if(hasCohortAnchor && (timingSlot || (!experiencedTrial && !proposalTrial && !assaultTrial && !incomeTrial
			&& !cooperationTrial && !portfolioTrial))) {
			const auto schedules=BuildAssaultSchedules(s,cohortAnchor.actions,DelayLimit(s));
			if(!schedules.empty()) {
				auto timed=schedules[timingIndex++%schedules.size()];
				if(preparePlan(timed)) {
					++timingEvaluated;
					largestPlan=std::max(largestPlan,static_cast<int>(timed.size()));
					evaluatePrepared(std::move(timed),true);
					continue;
				}
				// 重复案没有消耗积分预算，继续原分支，不能把被去重的时序案记成覆盖。
			}
		}
        bool refinementTrial=false;
        if(experiencedTrial) {
			plan=experienced[experiencedIndex++];
		}
        else if(proposalTrial) {
			plan=RestoreProposal(s,s.proposals[proposalIndex++]);
		}
        else if(assaultTrial) {
			const auto* anchor=assaults.Next();
			const size_t index=assaultGroupIndex++,round=index/initialGroups.size();
			plan=RefineInvestment(s,anchor->actions,initialGroups[index%initialGroups.size()],
				static_cast<int>((1+2*round)%3)*kRefinementMaxReplacements+(s.timeLimitedSearch && (index+round)%3==2 ? 2 : static_cast<int>(round/3)%kRefinementMaxReplacements),
				false,true);
			refinementTrial=true;
		}
        else if(incomeTrial) {
			const auto* anchor=investments.Next(s.timeLimitedSearch);
			if(!anchor || (trial%8==2 && incomeSeedIndex<incomeSeeds.size()*2)) {
				const size_t index=incomeSeedIndex++;
				const int option=incomeSeeds[index%incomeSeeds.size()];
				const int count=std::min({ActionLimit(s),DeploymentCapacity(s),PurchaseBudget(s)/s.options[option].cost,
					index<incomeSeeds.size() ? 4 : kPortfolioActions});
				plan.assign(count,{option,0});
			} else {
				const size_t index=incomeGroupIndex++;
				plan=RefineInvestment(s,anchor->actions,initialGroups[index%initialGroups.size()],
					incomeShape(index));
				refinementTrial=true;
			}
		}
        else if(reinforcementTrial) {
            // 跟队增援不能等所有独立配对/批次搜完才尝试。保留未购买的大队作探索起点，
            // 即使它单独亏损也可试接任意类型；最终整案仍按同一收益和资本门禁决定。
            const auto& anchor=hasCohortAnchor && (best.actions.empty() || trial%16==3)
                ? cohortAnchor.actions : best.actions;
            const auto& group=initialGroups[reinforcementIndex++%initialGroups.size()];
            std::array<int,6> rowCounts{};
            for(const auto& action:anchor) ++rowCounts[s.options[action.option].row];
            const int row=static_cast<int>(std::max_element(rowCounts.begin(),rowCounts.end())-rowCounts.begin());
            const auto sameRow=std::find_if(group.begin(),group.end(),[&](int option){return s.options[option].row==row;});
            const float delay=trial%16==3 ? std::min(kReinforcementDelay,DelayLimit(s)) : 0;
            if(s.timeLimitedSearch && anchor.size()>=3) {
                refinementTrial=true;
                plan=RefineCohort(s,anchor,group,1+static_cast<int>(reinforcementIndex%kRefinementMaxReplacements),delay);
            } else {
                plan=IntroduceOption(s,anchor,sameRow==group.end() ? group.front() : *sameRow,rng);
                plan.back().delay=delay;
            }
        }
        else if(cooperationTrial) {
            // 协作探索不能排在全部单兵变异之后，否则实时预算先耗尽，经济与护卫永远碰不到一起。
            // 任意两类都能试同步/错峰小批，不限定工人、护卫、路线或必须购买的比例。
            plan=samplePair(trial%8==3);
        }
        else if (portfolioTrial) plan = SamplePortfolio(s,rng,(trial-1)/2);
		else if (trial % 3 == 0) {
			plan.clear();
			const int focus = s.options[rng() % s.options.size()].row;
			const int repeated = s.stateModel ? static_cast<int>(rng() % s.options.size()) : -1;
			const bool mixed = !s.stateModel || rng() % 2 == 0;
			const bool concentrate = !s.stateModel || rng() % 2 == 0;
			for (int count = 1 + rng() % (s.stateModel ? kAdaptiveActions : kMaxActions); count > 0; --count) {
				int option = rng() % s.options.size();
				if (s.stateModel) {
					// 同类/混编、集中/分散均给探索机会；类型取自合法选项，不预设战术名单。
					if (!mixed || rng() % 2 == 0) option = repeated;
					const int row = concentrate && rng() % 4 != 0 ? focus : s.options[rng() % s.options.size()].row;
					const auto& original = s.options[option];
					const auto match = std::find_if(s.options.begin(),s.options.end(),[&](const auto& choice) {
						return choice.type == original.type && choice.cost == original.cost && choice.row == row;
					});
					if (match != s.options.end()) option = static_cast<int>(match-s.options.begin());
				} else if (rng() % 4 != 0) {
					for (int attempt = 0; attempt < 12 && s.options[option].row != focus; ++attempt) option = rng() % s.options.size();
				}
				plan.push_back({option, static_cast<float>(rng() % (s.stateModel ? 61 : 25)) * 0.5f});
			}
		}
		// 首案保留完整抽样，让紧预算至少先比较一次可支付的整队；不强制购买。
        const int mutations = experiencedTrial || proposalTrial || assaultTrial || incomeTrial || cooperationTrial || (portfolioTrial && trial == 1) ? 0 : 1 + rng() % 3;
		for (int n = 0; n < mutations; ++n) {
			MutatePlan(s,plan,rng);
		}
		// 兵种覆盖、整队和协作探索交错；不能把刚抽出的完整协作案覆盖成单兵案。
        if (!experiencedTrial && !proposalTrial && !assaultTrial && !incomeTrial && !portfolioTrial && !cooperationTrial && coverageIndex < coverage.size())
			plan = IntroduceOption(s,best.actions,coverage[coverageIndex++],rng);
        if (!experiencedTrial && !proposalTrial && !assaultTrial && !incomeTrial && !portfolioTrial && !cooperationTrial && trial%8==0 && elite.size()>1) {
			const auto& donor=hasCohortAnchor && trial%16==0 ? cohortAnchor.actions : elite[rng()%elite.size()].actions;
			BlendPlan(s,plan,donor,rng);
		}
		// 给有钱的空场提供一个必定可行的起点；其余候选仍自由比较兵种、路线和队形。
		if (!s.allowWait && trial == 1 && coverage.empty()) plan = {{static_cast<int>(cheapest - s.options.begin()),0}};
		if (!preparePlan(plan)) continue;
		largestPlan = std::max(largestPlan,static_cast<int>(plan.size()));
		if(reinforcementTrial) ++reinforcementEvaluated;
		if(refinementTrial) ++refinementEvaluated;
		if(incomeTrial) ++incomeEvaluated;
		if(assaultTrial) ++assaultEvaluated;
		if(proposalTrial) ++proposalEvaluated;
		if(experiencedTrial) ++experiencedEvaluated;
		if(cooperationTrial) { ++combinationEvaluated; if(plan.size()>2) ++cohortEvaluated; }
		// 首个完整编队先算完作为可细化起点，其余独立候选交给两个计算线程。
		evaluatePrepared(std::move(plan),portfolioTrial || experiencedTrial,trial==1,experiencedTrial);
	}
	collect(true);
	int routeEvaluated = 0;
	const float combinationBaseScore = best.score;
	const bool combinationBaseBreach = best.features[2] > 0;
	const float combinationBaseBreachSeconds = best.construction.breachSeconds;
	const auto groups = LegalOptionGroups(s,rng);
	const auto compare = [&](std::vector<Action> plan, bool cohort=false) {
		if (!preparePlan(plan)) return false;
		largestPlan = std::max(largestPlan,static_cast<int>(plan.size()));
		evaluatePrepared(std::move(plan),cohort);
		return true;
	};
	// 独立增援现有部队，避免把“有收益的单兵”绑定到新购物车中的亏损攻击；已有护卫仍在快照中。
	// 未用完的阶段时间自然留给后续阶段；完整候选不截断，离线固定次数搜索不受分段影响。
	phaseDeadline=begin+(explorationDeadline-begin)*kRouteSearchBudgetPercent/100;
	for (size_t route=0; routeEvaluated<kRouteTrials && withinBudget(); ++route) {
		bool found = false;
		for (const auto& group : groups) {
			if (routeEvaluated >= kRouteTrials || !withinBudget()) break;
			if (route >= group.size()) continue;
			found = true;
			compare({{group[route],0}}); ++routeEvaluated; // 已算过的同路单兵直接复用先前对照，不再积分。
		}
		if (!found) break;
	}
	collect(true);
	// 独立两兵案可以跨过“各自亏损、组合才盈利”的谷底；新能力不需要补搭配白名单。
	phaseDeadline=s.timeLimitedSearch ? begin+(explorationDeadline-begin)*kCombinationSearchBudgetPercent/100 : explorationDeadline;
	const bool deepenInvestment=!incomeSeeds.empty();
	// 实时经营与已产冰的离线中间态，从原组合名额中留出完整搭档扫描；
	// 不提高总候选/墙钟预算，避免每次都在新形成的三类案接入第四类之前结束。
	const int refinementLimit=deepenInvestment && best.actions.empty() && (s.timeLimitedSearch || investments.ProducedAnchor(s))
		? std::max(kRefinementTrials,std::min(kCombinationTrials/2,static_cast<int>(initialGroups.size())*2)) : kRefinementTrials;
	const int explorationCombinationLimit=s.timeLimitedSearch || deepenInvestment ? kCombinationTrials-refinementLimit : kCombinationTrials;
	const int pairTrials = std::min(kCombinationTrials/3,std::max(0,explorationCombinationLimit-combinationEvaluated));
	for (int trial=0; trial<pairTrials && !pairs.empty() && withinBudget(); ++trial) {
		auto plan = samplePair(trial%2==0);
		// 后续把任意配对试入搜索中的优案，可生成三种以上兵种并继续优化出生次序。
		if (trial >= std::min(static_cast<int>(pairs.size()),pairTrials/2) && trial%2 == 0) {
			auto expanded = elite[rng()%elite.size()].actions;
			int cost = 0;
			for (const auto& action : plan) cost += s.options[action.option].cost;
			for (const auto& action : expanded) cost += s.options[action.option].cost;
			// 整对预留预算和容量；逐只插入会在第二次腾位时误删刚插入的协作伙伴。
			while (!expanded.empty() && (expanded.size()+plan.size() > static_cast<size_t>(ActionLimit(s))
				|| cost > PurchaseBudget(s))) {
				const auto at = rng()%expanded.size();
				cost -= s.options[expanded[at].option].cost;
				expanded.erase(expanded.begin()+at);
			}
			expanded.insert(expanded.end(),plan.begin(),plan.end());
			plan = std::move(expanded);
		}
		if (compare(std::move(plan))) ++combinationEvaluated;
	}
	// 从同一组合预算中给各类型试成批规模，跨过“单只或两只被清掉，分路群体仍能回本”的谷底。
	// 所有兵种使用相同集中/分路及错峰形状，既不按生产角色筛选，也不指定护卫组合。
	for (int pass=0;pass<2 && cohortEvaluated<kCombinationTrials/3 && combinationEvaluated<explorationCombinationLimit && withinBudget();++pass) for (const auto& group:groups) {
		if (cohortEvaluated>=kCombinationTrials/3 || combinationEvaluated>=explorationCombinationLimit || !withinBudget()) break;
		const auto& choice=s.options[group.front()];
		const int stageCapacity=s.deploymentCapital>=0 ? (choice.device>=0 ? 1 : DeploymentCapacity(s)) : ActionLimit(s);
		const int count=std::min({ActionLimit(s),stageCapacity,PurchaseBudget(s)/choice.cost,pass==0 ? 4 : 8});
		if (count<2) continue;
		std::vector<Action> plan;
		for (int i=0;i<count;++i) plan.push_back({group[pass==0 ? 0 : i%group.size()],
			pass==0 ? 0 : std::min(DelayLimit(s),static_cast<float>(i)*2)});
		if (compare(std::move(plan),true)) { ++combinationEvaluated; ++cohortEvaluated; }
	}
	// 独立单兵与随机配对不能保证试到“给本案的前排补后续支援”。分出原有组合预算，
	// 轮流给各合法类型试入当前优案；同时/错峰、换兵/增兵都使用相同预测和门禁。
	for (int pass=0; pass<2 && combinationEvaluated<explorationCombinationLimit && withinBudget(); ++pass) {
		for (const auto& group : groups) {
			if (combinationEvaluated >= explorationCombinationLimit || !withinBudget()) break;
			// 有界先验不能靠夸大前排单独收益来打开组合搜索。等待暂优时，也从最好的非空探索案接支援。
			const auto anchor=std::find_if(elite.begin(),elite.end(),[](const Result& candidate){return !candidate.actions.empty();});
			if (best.actions.empty() && anchor==elite.end() && !hasCohortAnchor) break;
			const auto& anchorActions=!best.actions.empty() ? best.actions : hasCohortAnchor ? cohortAnchor.actions : anchor->actions;
			std::array<int,6> rowCounts{};
			for (const auto& action : anchorActions) ++rowCounts[s.options[action.option].row];
			const int row = static_cast<int>(std::max_element(rowCounts.begin(),rowCounts.end())-rowCounts.begin());
			const auto sameRow = std::find_if(group.begin(),group.end(),[&](int option){return s.options[option].row==row;});
			const int option = sameRow==group.end() ? group.front() : *sameRow;
			auto plan = IntroduceOption(s,anchorActions,option,rng);
			plan.back().delay = pass==0 ? std::min(kReinforcementDelay,DelayLimit(s)) : 0;
			// 第二轮也比较成批后援，保留原前排和整批预算；所有类型使用同一规模与错峰形状。
			// 不能只给前排追加一只支援，错过少量牺牲后其余成员能够存活的协同。
			int remaining=PurchaseBudget(s);
			for (const auto& action:anchorActions) remaining-=s.options[action.option].cost;
			const int count=std::min({4,remaining/s.options[option].cost,ActionLimit(s)-static_cast<int>(anchorActions.size())});
			const bool cohort=pass==1 && count>=2;
			if (cohort) {
				plan=anchorActions;
				for (int i=0;i<count;++i) plan.push_back({option,std::min(DelayLimit(s),kReinforcementDelay+i*2)});
			}
			if (compare(std::move(plan),cohort)) {
				++combinationEvaluated; ++reinforcementEvaluated;
				if (cohort) ++cohortEvaluated;
			}
		}
	}
	collect(true);
	// 完整优案和经营中间态都预留原组合预算继续接入其他类型；不以单兵亏损淘汰协同候选。
	if(s.timeLimitedSearch || deepenInvestment) {
		phaseDeadline=begin+(explorationDeadline-begin)*kRefinementSearchBudgetPercent/100;
		int completedRefinements=0;
		std::vector<Action> investmentSweep;
		size_t investmentPartner=0;
		bool producedSweep=false;
		// 去重失败不消耗完整评估名额；有界多扫描一轮类型，让新形成的多类案也能接上先前试过的伙伴。
		const int attempts=refinementLimit+static_cast<int>(initialGroups.size());
		for(int trial=0;trial<attempts && completedRefinements<refinementLimit
			&& combinationEvaluated<kCombinationTrials && !groups.empty() && withinBudget();++trial) {
			collect(false);
			const auto* assault=(prioritizeAssault ? trial%3!=2 : trial%2==1) || !deepenInvestment ? assaults.Next() : nullptr;
			const bool investment=!assault && deepenInvestment && (best.actions.empty() || trial%2==0);
			if(investment && (!s.timeLimitedSearch || investmentPartner%initialGroups.size()==0)) {
				// 固定起点扫一轮搭档，再轮换构成；起点和搭档同时轮转会永久漏掉部分组合。
				// 拷贝动作，Observe 更新 Frontier 时不借用会被替换/移动的元素。
				const auto* produced=best.actions.empty() ? investments.ProducedAnchor(s) : nullptr;
				const auto* next=produced && (!s.timeLimitedSearch || investmentPartner==0) ? produced : investments.Next(s.timeLimitedSearch && investmentPartner>0);
				investmentSweep=next ? next->actions : std::vector<Action>{};
				producedSweep=next && next==produced;
			}
			const auto& anchor=assault ? assault->actions : investment ? investmentSweep : !best.actions.empty() ? best.actions : cohortAnchor.actions;
			// 经营中间态可能只有一两名成员；小案仍可接前排，不能因此中止整个细化段。
			if(anchor.empty()) break;
			if(!assault && !investment && anchor.size()<3) continue;
			// 前段经营探索可能刚在最后一个类型上兑现保护；再从头扫描少量搭档，不能跳过排在它前面的类型。
			const size_t index=investment && s.timeLimitedSearch ? investmentPartner++ : static_cast<size_t>(trial);
			// 预留的细化段从先行姿态开始，让实时预算也有机会验证正常出生后的前排间距。
			// 已生产的多类案先比较同步追加，保留现有成功时序；尚未生产的案仍先试先行掩护。
			const int shape=producedSweep && investment ? kRefinementMaxReplacements : incomeShape(index+initialGroups.size());
			const size_t attackIndex=assault ? assaultGroupIndex++ : 0;
			auto plan=assault ? RefineInvestment(s,anchor,initialGroups[attackIndex%initialGroups.size()],
				static_cast<int>((1+2*(attackIndex/initialGroups.size()))%3)*kRefinementMaxReplacements,false,true)
				: investment ? RefineInvestment(s,anchor,initialGroups[index%initialGroups.size()],shape,true,producedSweep)
				: RefineCohort(s,anchor,groups[trial%groups.size()],1+trial%kRefinementMaxReplacements,
					trial%2==0 ? 0 : kReinforcementDelay);
			bool spreadTrial=false;
			// 一路未回本不代表多路同时经营也无效：复制自己发现的完整构成，共享玩家真实反制冷却。
			// 从既有细化名额中交错比较，不把单个搭档散到别行，也不按僵尸编号设置模板。
			if(!assault && best.actions.empty() && trial%kSpreadRefinementInterval==kSpreadRefinementInterval-1 && s.rows>1) {
				const size_t routeTrial=static_cast<size_t>(trial/kSpreadRefinementInterval);
				const auto* protectedPlan=investments.ProtectionAnchor(routeTrial);
				if(producedSweep || protectedPlan) {
					auto expanded=ExpandRoutes(s,producedSweep ? investmentSweep : protectedPlan->actions,routeTrial);
					if(!expanded.empty()) { plan=std::move(expanded); spreadTrial=true; }
				}
			}
			if(compare(std::move(plan))) {
				++combinationEvaluated; ++refinementEvaluated; ++completedRefinements;
				if(spreadTrial) ++spreadCohortEvaluated;
				if(investment) ++incomeEvaluated;
				if(assault) ++assaultEvaluated;
			}
		}
		collect(true);
	}
	phaseDeadline=explorationDeadline;
	const float combinationBestScore = best.score;
	const bool combinationBestBreach = best.features[2] > 0;
	const float combinationBestBreachSeconds = best.construction.breachSeconds;
	// 固定自由搜索选出的兵种、预算和时序，完整比较各合法行。已有部队仍留在原行参与推演，
	// 因此可以发现继续支援巨人的收益，也能因灰烬、溅射或减速而保留分路方案。
	const auto original = best.actions;
	const float baseScore = best.score;
	std::array<float, 6> rowScores{};
	int tested = 0, rejected = 0, chosenRow = -1;
	if (!original.empty()) for (int row = 0; row < static_cast<int>(s.context.size()) && withinBudget(); ++row) {
		auto plan = original;
		if (!Concentrate(s, plan, row)) continue;
		if(!s.allowWait && !HasPaidTroops(s,plan)) continue;
		// 最终至多六次逐行对照保留完整评分/拒绝诊断；此前的重复提案已去重。
		++evaluatedPlans;
		auto candidate = EvaluatePlan(s, weights, std::move(plan), best.baselineFeatures, baselineOpponentAssets);
		tested |= 1 << row; rowScores[row] = candidate.score;
		if (rejectInvestment(candidate)) { rejected |= 1 << row; continue; }
		if (BetterOutcome(candidate,best)) { best = std::move(candidate); chosenRow = row; }
	}
	best.formationBaseScore = baseScore; best.formationScores = rowScores;
	best.formationTested = tested; best.formationRejected = rejected; best.formationChosenRow = chosenRow;

	best.routeEvaluated = routeEvaluated; best.combinationEvaluated = combinationEvaluated;
	best.cohortEvaluated=cohortEvaluated;
	best.duplicatesSkipped=duplicatesSkipped; best.unevenMixEvaluated=unevenMixEvaluated;
	best.reinforcementEvaluated=reinforcementEvaluated;
	best.refinementEvaluated=refinementEvaluated;
	best.incomeEvaluated=incomeEvaluated;
	best.assaultEvaluated=assaultEvaluated;
	best.proposalEvaluated=proposalEvaluated;
	best.experiencedEvaluated=experiencedEvaluated;
	best.experiencedAshEvaluated=experiencedAshEvaluated;
    // 交错导出经营/攻城的构成；下一轮仍有新抽样，不把历史候选变成固定出兵名单。
    const auto incomeProposals=investments.Proposals(), attackProposals=assaults.Proposals();
    for(size_t i=0;best.proposals.size()<kAssaultBranches && (i<incomeProposals.size() || i<attackProposals.size());++i) {
		if(prioritizeAssault && i<attackProposals.size()) best.proposals.push_back(SaveProposal(s,attackProposals[i]));
        if(i<incomeProposals.size() && best.proposals.size()<kAssaultBranches) best.proposals.push_back(SaveProposal(s,incomeProposals[i]));
        if(!prioritizeAssault && i<attackProposals.size() && best.proposals.size()<kAssaultBranches) best.proposals.push_back(SaveProposal(s,attackProposals[i]));
    }
	best.spreadCohortEvaluated=spreadCohortEvaluated;
	best.assaultDiagnostics.timingEvaluated=timingEvaluated;
	best.assaultDiagnostics.continuationEvaluated=continuationEvaluated;
	best.assaultDiagnostics.continuationUnavailable=continuationUnavailable;
	best.assaultDiagnostics.continuationValueRejected=continuationValueRejected;
	best.assaultDiagnostics.continuationCapitalRejected=continuationCapitalRejected;
	for(const auto& candidate:assaultCandidates) best.assaultProposals.push_back(SaveProposal(s,candidate.actions));
	if(best.actions.empty()) best.investmentPruningActions=investments.PruningAnchor();
	best.combinationBaseScore = combinationBaseScore; best.combinationBestScore = combinationBestScore;
	best.combinationBaseBreach = combinationBaseBreach; best.combinationBestBreach = combinationBestBreach;
	best.combinationBaseBreachSeconds = combinationBaseBreachSeconds; best.combinationBestBreachSeconds = combinationBestBreachSeconds;
	best.evaluated = evaluatedPlans;
	best.capitalRejected = capitalRejected;
	best.largestPlan = largestPlan;
	best.widestComposition=widestComposition;
	best.candidates = std::move(candidateStats);
	best.stateInputs = inputs; best.effectiveWeights = weights;
	best.capitalUtilityInputs=DescribeCapitalUtility(s);
	best.regrouping = best.actions.empty() && deferredInvestment;
	best.baselineBreachSeconds=baselineBreachSeconds;
    best.timeLimited=timeLimited;
	// 小队没有预测到增量击杀、削血或生产时，才升级搜索范围与预测时域。
	// 两次评估不能混加不同时间窗的分数：升级后整案及等待基线一起重算。
	if (s.searchVersion == 1 && s.allowWait && cheapest->cost > 0 && !SearchTimeExpired(s)
		&& std::min(DeploymentCapacity(s),PurchaseBudget(s)/cheapest->cost) > ActionLimit(s)
		&& best.features[2] == 0 && best.features[0] <= best.baselineFeatures[0]
		&& best.features[1] <= best.baselineFeatures[1] && best.features[4] <= best.baselineFeatures[4]) {
		auto expanded = RawWorldCache::CopyView(s);
		expanded.searchVersion = 2;
		auto result = SearchFormation(expanded,baseWeights,seed,evaluator);
        result.timeLimited|=best.timeLimited;
		result.expandedForecast = true;
		result.evaluated += best.evaluated;
		result.capitalRejected += best.capitalRejected;
		result.routeEvaluated += best.routeEvaluated;
		result.combinationEvaluated += best.combinationEvaluated;
		result.cohortEvaluated += best.cohortEvaluated;
		result.duplicatesSkipped += best.duplicatesSkipped; result.unevenMixEvaluated += best.unevenMixEvaluated;
		result.reinforcementEvaluated += best.reinforcementEvaluated;
		result.refinementEvaluated += best.refinementEvaluated;
		result.incomeEvaluated += best.incomeEvaluated;
		result.assaultEvaluated += best.assaultEvaluated;
		result.proposalEvaluated+=best.proposalEvaluated;
		result.experiencedEvaluated+=best.experiencedEvaluated;
		result.experiencedAshEvaluated+=best.experiencedAshEvaluated;
		result.spreadCohortEvaluated+=best.spreadCohortEvaluated;
		result.assaultDiagnostics.timingEvaluated+=best.assaultDiagnostics.timingEvaluated;
		result.assaultDiagnostics.continuationEvaluated+=best.assaultDiagnostics.continuationEvaluated;
		result.assaultDiagnostics.continuationUnavailable+=best.assaultDiagnostics.continuationUnavailable;
		result.assaultDiagnostics.continuationValueRejected+=best.assaultDiagnostics.continuationValueRejected;
		result.assaultDiagnostics.continuationCapitalRejected+=best.assaultDiagnostics.continuationCapitalRejected;
		result.largestPlan = std::max(result.largestPlan,best.largestPlan);
		result.widestComposition=std::max(result.widestComposition,best.widestComposition);
		return result;
	}
	return best;
}
/** 技能与不施法优案同分制比较；有限粗案保留少量目标共享剩余预算深化，最终仍复核整案。 */
static Result SearchOrdinary(const Snapshot& input, const Weights& weights, std::uint32_t seed, PlanEvaluator* evaluator) {
    auto state = RawWorldCache::CopyView(input);
    if(state.timeLimitedSearch) {
		const auto begin=std::chrono::steady_clock::now();
		state.searchDeadline=begin+(input.searchDeadline-begin)*kWinnerSearchBudgetPercent/100;
	}
    const auto explorationDeadline=state.searchDeadline;
    const bool futurePrecision=!input.precisionReady && input.precisionUnlockAfterPurchase
        && std::isfinite(input.precisionUnlockAimStartSeconds) && input.precisionUnlockAimStartSeconds>0
        && std::any_of(input.options.begin(),input.options.end(),[&](const Option& option) {
            return option.device<0 && option.cost>0 && option.cost<=input.budget-ColdStorageSkillRules::StrikeIceCost;
        });
    state.precisionUnlockAfterPurchase=futurePrecision;
    const bool canUsePrecision=(input.precisionReady || futurePrecision) && input.pendingPrecisionID<=0
        && input.budget>=ColdStorageSkillRules::StrikeIceCost;
    if (state.timeLimitedSearch && canUsePrecision) {
        // 同步训练能走到清除/跟进，实时搜索也必须为它留下比较机会，不能等编队耗尽全部预算。
        const auto begin=std::chrono::steady_clock::now();
        state.searchDeadline=begin+(explorationDeadline-begin)*kFormationSearchBudgetPercent/100;
    }
    // 总资本充足时比较大队，付款转成存活/在途兵力不会让搜索退回小队；实际采购仍只用现金。
    // 已经扩展却仍等待时直接延续完整搜索，避免每次重付小队与大队两套基线的成本。
    // 同一次搜索的行动、等待、技能均使用同一长时域；仍允许便宜小队胜出。
	const bool fundedPortfolio=ExpandFundedPortfolio(state);
	auto best = SearchFormation(state,weights,seed,evaluator);
    state.searchDeadline=explorationDeadline;
    best.expandedForecast |= fundedPortfolio;
	if (SearchTimeExpired(input)) { best.timeLimited=true; return best; }
	if ((!input.precisionReady && !futurePrecision) || input.pendingPrecisionID > 0
		|| input.budget < ColdStorageSkillRules::StrikeIceCost || !ValidWeights(weights)) {
		if(best.expandedForecast) state.searchVersion=2;
		state.searchDeadline=input.searchDeadline;
		if(best.fallbackMode==0) PruneWinner(state,best);
		best.timeLimited|=SearchTimeExpired(state);
		return best;
	}
	if (best.expandedForecast) state.searchVersion = 2;
	// 编队阶段已经完整选过同一技能意图、同一时域的等待应对；直接复用其
	// 特征/对手资产/进屋时刻，不能在选靶前再花一整套反制世界的时间。
	Result baseline;
	baseline.features=best.baselineFeatures; baseline.opponentAssets=best.baselineOpponentAssets;
	baseline.construction.breachSeconds=best.baselineBreachSeconds;
	baseline.score=Score(baseline.features,best.effectiveWeights);
	const auto incumbent = best;
	int experiencedEvaluated=incumbent.experiencedEvaluated;
	int experiencedAshEvaluated=incumbent.experiencedAshEvaluated;
	// 这里只压缩选靶预算，不直接给技能收益。火力、主动能力与前锋阻挡仍须通过完整推演兑现。
	std::vector<std::pair<float,int>> targets;
	for (const auto& p : state.plants) if (p.id > 0 && p.health > 0) {
		float priority = p.dps*Horizon(state)+p.assetValue;
		// 周期全场技能按同窗内的释放次数排列选靶，避免只算一次而永远排在实时预算后面。
		for (const auto& strike : state.rowStrikes) if (strike.plantID == p.id)
			priority += strike.damage*state.rows*std::max(1.0f,std::ceil(std::max(0.0f,Horizon(state)-strike.ready)/std::max(.001f,strike.recharge)));
		for (const auto& aura : state.attackAuras) if (aura.plantID == p.id)
			for (const auto& ally : state.plants) if (ally.health > 0 && std::abs(ally.row-p.row)<=1 && std::abs(ally.column-p.column)<=1)
				priority += ally.dps*Horizon(state)*aura.bonus;
		// 同一炮的所有可选爆点共用一次装填；按实际来源/周期计威胁，不能把落点数当炮数。
		std::set<int> countedCounters;
		for (const auto& counter : state.counters) if (counter.plantID == p.id && !counter.blast.committed
			&& countedCounters.insert(counter.source).second) {
			const int rows=static_cast<int>(std::count_if(counter.blast.reach.begin(),counter.blast.reach.end(),[](float reach){return reach>=0;}));
			priority += counter.blast.damage*std::max(1,rows)*std::max(1.0f,
				std::ceil(std::max(0.0f,Horizon(state)-counter.blast.ready)/std::max(.001f,counter.recharge)));
		}
		for (const auto& unit : state.current) if (unit.body.health > 0 && unit.body.row == p.row && unit.body.x > p.x)
			priority += std::min(p.health,unit.body.health)/(1+std::abs(unit.body.x-p.x)/state.cellWidth);
		targets.emplace_back(priority,p.id);
	}
	std::stable_sort(targets.begin(),targets.end(),[](const auto& a,const auto& b) { return a.first > b.first; });
	if (targets.size() > kPrecisionTargets) targets.resize(kPrecisionTargets);
	if(state.timeLimitedSearch) {
		// 选靶不能吃光技能阶段；保留时间实际重搜清除后的完整协作，仍不要求购买跟进部队。
		const auto begin=std::chrono::steady_clock::now();
		state.searchDeadline=begin+(explorationDeadline-begin)*kPrecisionProbeBudgetPercent/100;
	}
		int evaluated = 0;
		auto assaultDiagnostics=incumbent.assaultDiagnostics;
		std::vector<Result> precisionFrontier;
		std::vector<Proposal> jointProposals, jointAssaultProposals;
	int largestPlan = incumbent.largestPlan;
	int widestComposition=incumbent.widestComposition;
	std::mt19937 rng(seed);
	// 合法身份先组成无序组合，再按1/2/3株交错。高优先级组合只是一个起点，
	// 其余从全部跨行组合有界抽样；不会把排在前三的植物变成固定三连义务。
	std::array<std::vector<std::vector<int>>,ColdStorageSkillRules::StrikeTargetLimit> setsBySize;
	const int targetLimit=std::min(std::clamp(input.precisionTargetLimit,1,ColdStorageSkillRules::StrikeTargetLimit),
		input.budget/ColdStorageSkillRules::StrikeIceCost);
	for(size_t i=0;i<targets.size();++i) {
		setsBySize[0].push_back({targets[i].second});
		if(targetLimit<2) continue;
		for(size_t j=i+1;j<targets.size();++j) {
			if(targets[i].second==targets[j].second) continue;
			setsBySize[1].push_back({targets[i].second,targets[j].second});
			if(targetLimit<3) continue;
			for(size_t k=j+1;k<targets.size();++k)
				if(targets[k].second!=targets[i].second && targets[k].second!=targets[j].second)
					setsBySize[2].push_back({targets[i].second,targets[j].second,targets[k].second});
		}
	}
	for(int size=1;size<targetLimit;++size) {
		auto& sets=setsBySize[size];
		if(sets.size()>1) std::shuffle(sets.begin()+1,sets.end(),rng);
		if(sets.size()>kPrecisionSetsPerSize) sets.resize(kPrecisionSetsPerSize);
	}
	std::vector<std::vector<int>> targetSets;
	const bool richFollowup=state.timeLimitedSearch && state.netEconomy && CapitalUtilityScale(state)<1;
	for(size_t index=0;index<kPrecisionSetsPerSize;++index)
		for(int order=0;order<targetLimit;++order) {
			// 富余资本的联合破阵先获得多目标比较机会；每种规模仍交错，
			// 费用和完整回报不占优时仍可选单目标或等待，不强制选满。
			const int size=richFollowup ? targetLimit-1-order : order;
			if(index<setsBySize[size].size())
			targetSets.push_back(std::move(setsBySize[size][index]));
		}
	const auto consider = [&](Result candidate) {
			++evaluated;
			++assaultDiagnostics.jointEvaluated;
			int troops=0; bool afterStrike=true;
			const float strikeAt=ColdStorageSkillRules::StrikeAimDuration
				+(state.precisionUnlockAfterPurchase ? std::max(0.0f,state.precisionUnlockAimStartSeconds) : 0);
			for(const auto& action:candidate.actions) if(state.options[action.option].device<0) {
				++troops; afterStrike&=action.delay>=strikeAt;
			}
			assaultDiagnostics.jointLargestPlan=std::max(assaultDiagnostics.jointLargestPlan,troops);
			if(troops>2 && afterStrike) ++assaultDiagnostics.jointAfterStrike;
		largestPlan=std::max(largestPlan,static_cast<int>(candidate.actions.size()));
		widestComposition=std::max(widestComposition,TroopCompositionSize(state,candidate.actions));
		if(!state.allowWait && !HasPaidTroops(state,candidate.actions)) return;
			candidate.capitalUtilityInputs=incumbent.capitalUtilityInputs;
			if(futurePrecision && (candidate.precisionTargetID<=0 || !FuturePrecisionPlanPayable(state,candidate.actions))) return;
			// 粗案先完整推演再保留少量未完成目标，不能要求已经胜出才有资格寻找完整后援。
			// 这里只分配探索预算；最终仍过同兵不施法、等待和原资本门禁。
			const auto sameTarget=[&](const Result& old) {
				return old.precisionTargetID==candidate.precisionTargetID
					&& old.precisionAdditionalTargetIDs==candidate.precisionAdditionalTargetIDs;
			};
			auto retained=std::find_if(precisionFrontier.begin(),precisionFrontier.end(),sameTarget);
			if(retained==precisionFrontier.end()) precisionFrontier.push_back(candidate);
			else if(BetterOutcome(candidate,*retained)) *retained=candidate;
			std::stable_sort(precisionFrontier.begin(),precisionFrontier.end(),[](const auto& a,const auto& b){
				if(const int victory=CompareVictory(a,b)) return victory>0;
				return a.score>b.score;
			});
			if(precisionFrontier.size()>kPrecisionFrontierLimit) precisionFrontier.resize(kPrecisionFrontierLimit);
		// 即使旧参数没有成本惩罚，也不能免费使用新技能；正式净冰模型已经计费，不重复惩罚。
		if (!state.netEconomy) candidate.score -= std::max(0.0f,1+incumbent.effectiveWeights[5])*PrecisionCost(state);
		auto without = RawWorldCache::CopyView(state); without.precisionTargetID = 0; without.precisionAdditionalTargetIDs.clear();
		// 空编队的“不施法”世界就是本轮已算完的同窗等待基线，不为每个目标重复推演。
		auto sameArmy = candidate.actions.empty() ? baseline
			: EvaluatePlan(without,incumbent.effectiveWeights,candidate.actions,baseline.features,baseline.opponentAssets);
		if (candidate.actions.empty()) {
			// 基线初算以零资产为参考；复用为“不施法”对照时必须归一到自身，不能带入整座植物阵地的负分。
			sameArmy.baselineFeatures=baseline.features; sameArmy.baselineOpponentAssets=baseline.opponentAssets;
			sameArmy.opponentScore=0; sameArmy.score=Score(sameArmy.features,incumbent.effectiveWeights);
		}
		// 删除成员与选靶共用回报门禁，避免把单株返冰或估值误当成破阵收益。
			if (!PrecisionWorthwhile(state,candidate,sameArmy)) {++assaultDiagnostics.jointValueRejected; return;}
			if (ShouldRegroup(candidate,state.budget,state.recoveryReserve)) {++assaultDiagnostics.jointRegroupRejected; return;}
			if (state.netEconomy && ShouldConserveCapital(candidate,state.budget,state.recoveryReserve,
				state.allowWait ? state.capitalRiskAllowance : (std::numeric_limits<float>::max)())) {
				++assaultDiagnostics.jointCapitalRejected; return;
			}
			if (!BetterOutcome(candidate,best)) return;
			best = std::move(candidate);
		};
		std::vector<std::vector<std::vector<int>>> targetGroups(targetSets.size());
		std::vector<std::vector<std::vector<Action>>> targetSchedules(targetSets.size());
	const auto probeTarget = [&](size_t index, int firstTrial, int endTrial, bool compareBase) {
		const auto& target=targetSets[index];
		state.precisionTargetID = target.front();
		state.precisionAdditionalTargetIDs.assign(target.begin()+1,target.end());
		const int baseModes=state.timeLimitedSearch && incumbent.actions.empty() ? 1 : 2;
		for (int mode=0; compareBase && mode<baseModes && !SearchTimeExpired(state); ++mode) {
			auto plan = mode == 0 ? incumbent.actions : std::vector<Action>{};
			Repair(state,plan);
			consider(EvaluatePlan(state,incumbent.effectiveWeights,std::move(plan),baseline.features,baseline.opponentAssets));
		}
		// 不施法时的优案可能攻击另一行，或全体等待；先给清除后的破口比较可支付的跟进，
		// 否则“先清关键输出再跟进任意编队”的联合收益永远进不了最后的重搜。
		const auto plant = std::find_if(state.plants.begin(),state.plants.end(),[&](const Plant& p) { return p.id == target.front(); });
		if (plant == state.plants.end()) return;
		auto& followups=targetGroups[index];
		if (followups.empty()) followups=LegalOptionGroups(state,rng);
		for (int trial=firstTrial; trial<endTrial && !followups.empty() && !SearchTimeExpired(state); ++trial) {
			// 清除所在行只是候选落点；跟进的类型和数量不按经济、肉盾、狙击能力筛选。
			std::vector<Action> plan;
			const int count = std::min(ActionLimit(state),trial%2 ? 2 : 1);
			if (count == 0) break;
			for (int n=0; n<count; ++n) {
				const auto& group = followups[(trial+n)%followups.size()];
				int option = group[rng()%group.size()];
				const int followID=target[static_cast<size_t>(trial+n)%target.size()];
				const auto followPlant=std::find_if(state.plants.begin(),state.plants.end(),[&](const Plant& p){return p.id==followID;});
				const int followRow=followPlant==state.plants.end() ? plant->row : followPlant->row;
				const auto sameRow = std::find_if(group.begin(),group.end(),[&](int i) { return state.options[i].row == followRow; });
				if (sameRow != group.end()) option = *sameRow;
				// 第一只也能等打击落地后再进场；否则薄血部队会在清除生效前死亡，联合收益被漏掉。
				const float span = trial%2 ? std::min(6.0f,DelayLimit(state)) : DelayLimit(state);
				float delay = trial%3 == 0 ? 0 : (rng()%(static_cast<int>(span*2)+1))*.5f;
				// 保留一案在实际瞄准结束后进场，避免延长预警后短错峰抽样漏掉可行跟进。
				if (trial == 1) delay = std::min(DelayLimit(state),ColdStorageSkillRules::StrikeAimDuration
					+(state.precisionUnlockAfterPurchase ? std::max(0.0f,state.precisionUnlockAimStartSeconds) : 0)+.5f);
				plan.push_back({option,delay});
			}
            // 清除后的小股后援仍会被另一株输出消灭；交错完整规模与本轮未完成协作，不绑定任何兵种。
	            if(trial%4==2) {
					auto& schedules=targetSchedules[index];
					if(schedules.empty()) {
						// 规模采样使用独立首案，不能把外层恒为2模4的编号传进去而永远错过完整分支。
						auto full=SamplePortfolio(state,rng,0);
						if(target.size()>1 || Concentrate(state,full,plant->row)) {
							Repair(state,full);
							schedules=BuildAssaultSchedules(state,full,DelayLimit(state));
							if(schedules.empty()) schedules.push_back(std::move(full));
						}
					}
					if(!schedules.empty()) plan=schedules[static_cast<size_t>(trial/4)%schedules.size()];
            } else if(trial%4==3 && !incumbent.proposals.empty()) {
                auto continued=RestoreProposal(state,incumbent.proposals[static_cast<size_t>(trial)/4%incumbent.proposals.size()]);
                if(!continued.empty() && (target.size()>1 || Concentrate(state,continued,plant->row))) plan=std::move(continued);
            }
			Repair(state,plan);
			if (!state.allowWait && !plan.empty()) plan.front().delay = 0;
			consider(EvaluatePlan(state,incumbent.effectiveWeights,std::move(plan),baseline.features,baseline.opponentAssets));
		}
	};
	if (state.timeLimitedSearch) {
		// 每个目标先试少量跟进再加深，不能把实时预算全花在第一个目标的十六种变体上。
		// 先比较任意双兵协作，再比较单兵；名单、落点和最终购买仍由同一评分自由决定。
		for (int pass=0; pass<kPrecisionFollowupTrials && !SearchTimeExpired(state); ++pass)
			for (size_t index=0; index<targetSets.size() && !SearchTimeExpired(state); ++index) {
				// 能容纳完整小队时先把整案与清除合算，避免只试一两只薄兵就耗尽窗口。
					const int trial=ActionLimit(state)>2 && pass<3 ? (pass==0 ? 2 : pass==1 ? 1 : 0)
					: pass<2 ? 1-pass : pass;
				probeTarget(index,trial,trial+1,pass==0);
			}
	} else {
		// 不限时训练保留原来的共同随机数顺序，单独比较实时调度的影响。
		for (size_t index=0; index<targetSets.size(); ++index)
			probeTarget(index,0,kPrecisionFollowupTrials,true);
	}
	state.searchDeadline=explorationDeadline;
		// 胜出目标优先，尚未跨过粗探测谷底的少量完整案也共享余下预算，绝不每案重领预算。
		if(best.precisionTargetID>0) {
			precisionFrontier.erase(std::remove_if(precisionFrontier.begin(),precisionFrontier.end(),[&](const Result& candidate) {
				return candidate.precisionTargetID==best.precisionTargetID && candidate.precisionAdditionalTargetIDs==best.precisionAdditionalTargetIDs;
			}),precisionFrontier.end());
			precisionFrontier.insert(precisionFrontier.begin(),best);
			if(precisionFrontier.size()>kPrecisionFrontierLimit) precisionFrontier.resize(kPrecisionFrontierLimit);
		}
		const auto deepTargets=precisionFrontier; // consider 会更新前沿；固定本轮名单避免迭代器失效及无限扩展。
		for(size_t index=0;index<deepTargets.size() && !SearchTimeExpired(state);++index) {
			state.precisionTargetID = deepTargets[index].precisionTargetID;
			state.precisionAdditionalTargetIDs = deepTargets[index].precisionAdditionalTargetIDs;
			if(state.timeLimitedSearch) {
				const auto now=std::chrono::steady_clock::now();
				state.searchDeadline=now+(explorationDeadline-now)/static_cast<int>(deepTargets.size()-index);
			}
			state.proposals=incumbent.proposals; // 用本轮刚评过的未付款构成继续探索；费用、反制和收益仍按清除后的世界重算。
			if(!deepTargets[index].actions.empty()) state.proposals.insert(state.proposals.begin(),SaveProposal(state,deepTargets[index].actions));
			const auto joint = SearchFormation(state,weights,seed,evaluator);
			experiencedEvaluated+=joint.experiencedEvaluated;
			experiencedAshEvaluated+=joint.experiencedAshEvaluated;
			++assaultDiagnostics.jointDeepSearches;
			assaultDiagnostics.timingEvaluated+=joint.assaultDiagnostics.timingEvaluated;
			jointProposals=MergeProposals(joint.proposals,jointProposals);
			jointAssaultProposals=MergeProposals(joint.assaultProposals,jointAssaultProposals);
			evaluated += joint.evaluated;
		largestPlan=std::max(largestPlan,joint.largestPlan);
		widestComposition=std::max(widestComposition,joint.widestComposition);
			auto candidate=EvaluatePlan(state,incumbent.effectiveWeights,joint.actions,baseline.features,baseline.opponentAssets);
			candidate.experiencedSelected=joint.experiencedSelected;
			candidate.experiencedAshSelected=joint.experiencedAshSelected;
			consider(std::move(candidate));
			state.searchDeadline=explorationDeadline;
	}
	state.precisionTargetID=best.precisionTargetID;
	state.precisionAdditionalTargetIDs=best.precisionAdditionalTargetIDs;
	state.searchDeadline=input.searchDeadline;
	best.effectiveWeights=incumbent.effectiveWeights;
	if(best.fallbackMode==0) PruneWinner(state,best);
	best.precisionEvaluated = evaluated;
	best.evaluated = incumbent.evaluated+evaluated+best.pruningEvaluated;
	best.effectiveWeights = incumbent.effectiveWeights; best.stateInputs = incumbent.stateInputs;
	best.baselineBreachSeconds=incumbent.baselineBreachSeconds;
	best.expandedForecast = incumbent.expandedForecast;
	best.candidates = incumbent.candidates; // 统计窗口只包含精准清除前的编队搜索，不能混合不同技能费用的探测。
	// EvaluatePlan 生成的是单案结果；技能胜出不能把之前确实做过的编队探索统计清零。
	best.largestPlan=largestPlan;
	best.widestComposition=widestComposition;
	best.capitalRejected=incumbent.capitalRejected;
	best.routeEvaluated=incumbent.routeEvaluated; best.combinationEvaluated=incumbent.combinationEvaluated;
	best.cohortEvaluated=incumbent.cohortEvaluated;
	best.duplicatesSkipped=incumbent.duplicatesSkipped; best.unevenMixEvaluated=incumbent.unevenMixEvaluated;
	best.reinforcementEvaluated=incumbent.reinforcementEvaluated;
	best.refinementEvaluated=incumbent.refinementEvaluated;
	best.incomeEvaluated=incumbent.incomeEvaluated;
	best.assaultEvaluated=incumbent.assaultEvaluated;
	best.experiencedEvaluated=experiencedEvaluated;
	best.experiencedAshEvaluated=experiencedAshEvaluated;
		best.proposalEvaluated=incumbent.proposalEvaluated;
		best.proposals=MergeProposals(jointProposals,incumbent.proposals);
		best.assaultProposals=MergeProposals(jointAssaultProposals,incumbent.assaultProposals);
		if(best.assaultProposals.size()>2) best.assaultProposals.resize(2);
		assaultDiagnostics.jointProposalsRetained=static_cast<int>(std::count_if(best.proposals.begin(),best.proposals.end(),[&](const Proposal& kept) {
			return std::any_of(jointProposals.begin(),jointProposals.end(),[&](const Proposal& generated) {
				return kept.size()==generated.size() && std::equal(kept.begin(),kept.end(),generated.begin(),[](const auto& a,const auto& b) {
					return a.type==b.type && a.row==b.row && a.device==b.device && a.setting==b.setting && a.delay==b.delay;
				});
			});
		}));
		best.assaultDiagnostics=assaultDiagnostics;
	best.combinationBaseScore=incumbent.combinationBaseScore; best.combinationBestScore=incumbent.combinationBestScore;
	best.combinationBaseBreach=incumbent.combinationBaseBreach; best.combinationBestBreach=incumbent.combinationBestBreach;
	best.combinationBaseBreachSeconds=incumbent.combinationBaseBreachSeconds;
	best.combinationBestBreachSeconds=incumbent.combinationBestBreachSeconds;
	best.formationBaseScore=incumbent.formationBaseScore; best.formationScores=incumbent.formationScores;
	best.formationTested=incumbent.formationTested; best.formationRejected=incumbent.formationRejected;
	best.formationChosenRow=incumbent.formationChosenRow;
	best.precisionGain = best.precisionTargetID > 0 ? best.score-incumbent.score : 0;
    best.timeLimited|=incumbent.timeLimited || SearchTimeExpired(state);
	ExportFuturePrecision(state,best);
	return best;
}

struct ObservedCandidateAdmission {
    PhasedPlan plan;
    size_t profile=0;
    bool continuation=false,phased=false;
};

/** 有效旧案优先；旧案整批失格时仍允许独立合法菜单参评，不改写或截短待普通搜索重评的意图。 */
static ObservedCandidateAdmission PrepareObservedCandidate(const Snapshot& input,const Result& incumbent) {
    ObservedCandidateAdmission admitted;
    const float allowance=ObservedPurchaseAllowance(input);
    if(input.observedResponse.profiles.empty() || allowance<=0) return admitted;
    auto state=RawWorldCache::CopyView(input);
    if(incumbent.expandedForecast) state.searchVersion=2;
    if(state.precisionUnlockAfterPurchase) {state.precisionTargetID=0;state.precisionAdditionalTargetIDs.clear();}
    state.precisionUnlockAfterPurchase=false;
    ExpandFundedPortfolio(state);
    if(DeploymentCapacity(state)<=0) return admitted;
    const auto accept=[&](PhasedPlan plan,bool continuation,bool phased,size_t index) {
        const size_t selected=index%state.observedResponse.profiles.size();
        const auto& profile=state.observedResponse.profiles[selected];
        if(plan.first.empty() || !FinitePhasedValue(profile.weight) || profile.weight<=0 || profile.weight>.65f
            || !FinitePhasedValue(profile.windowSeconds) || profile.windowSeconds<=0 || profile.windowSeconds>24
            || !ObservedPlanAdmissible(state,plan,phased,allowance)) return false;
        admitted.plan=std::move(plan);admitted.profile=selected;
        admitted.continuation=continuation;admitted.phased=phased;return true;
    };
    if(!state.assaultContinuation.empty()) {
        PhasedPlan continued;
        continued.first=RestoreProposal(state,state.assaultContinuation[state.searchRound%state.assaultContinuation.size()],
            state.assaultContinuationRelativeTiming);
        if(!state.assaultContinuationRelativeTiming) for(auto& action:continued.first) action.delay=0;
        if(accept(std::move(continued),true,false,state.searchRound)) return admitted;
    }
    if(state.searchRound%4==3) {
        auto affordable=state;affordable.budget=std::min(state.budget,static_cast<int>(allowance));
        affordable.capacity=DeploymentCapacity(state);
        const auto menu=BuildPhasedPlanningMenu(affordable,incumbent);
        // 菜单各项都仍独立整案验证；选中项失格不能再次遮住同轮的其他可付构成。
        for(size_t n=0;n<menu.plans.size();++n) {
            const size_t index=(state.searchRound/4+n)%menu.plans.size();
            if(accept(menu.plans[index],false,true,state.searchRound/4)) return admitted;
        }
    }
    PhasedPlan direct;direct.first=BuildObservedPurchaseCandidate(state,allowance);
    accept(std::move(direct),false,false,ObservedPurchaseProfileIndex(state,allowance));
    return admitted;
}

/** 一次有限风险比较仍保留完整最坏世界；只有公开习惯支持的整案增量能改变排序。 */
static Result SearchObservedCandidate(const Snapshot& input,const Weights& weights,Result best) {
    ObservedDecisionDiagnostics diagnostics;
    diagnostics.profiles=static_cast<int>(input.observedResponse.profiles.size());
    diagnostics.revision=input.observedResponse.revision;diagnostics.riskCap=ObservedPurchaseAllowance(input);
    if(input.observedResponse.profiles.empty() || diagnostics.riskCap<=0) {best.observedDecision=diagnostics;return best;}
    auto state=RawWorldCache::CopyView(input);if(best.expandedForecast) state.searchVersion=2;
    const auto admitted=PrepareObservedCandidate(state,best);
    if(admitted.plan.first.empty()) {best.observedDecision=diagnostics;return best;}
    const auto& plan=admitted.plan;
    const bool continuation=admitted.continuation,phased=admitted.phased;
    const auto& profile=input.observedResponse.profiles[admitted.profile];
    diagnostics.windowSeconds=profile.windowSeconds;diagnostics.weight=profile.weight;
    auto comparison=CompareObservedPlan(state,weights,plan,best.actions,profile,phased);
    diagnostics=comparison.diagnostics;
    if(comparison.complete && continuation) ++best.assaultDiagnostics.continuationEvaluated;
    if(!comparison.complete) {best.observedDecision=diagnostics;best.timeLimited|=SearchTimeExpired(input);return best;}
    const auto rank=[&](const Result& robust,const Result& behavior) {
        return (1-profile.weight)*(robust.score-comparison.robustPassive.score)
            +profile.weight*(behavior.score-comparison.behaviorPassive.score);
    };
    auto* selected=&comparison.robust.full;auto* behavior=&comparison.behavior.full;
    float value=rank(*selected,*behavior);
    if(phased) {
        const float wait=rank(comparison.robust.wait,comparison.behavior.wait);
        if(wait>value+.001f) {value=wait;selected=&comparison.robust.wait;behavior=&comparison.behavior.wait;}
        const float deferred=rank(comparison.robust.deferred,comparison.behavior.deferred);
        if(deferred>value+.001f) {value=deferred;selected=&comparison.robust.deferred;behavior=&comparison.behavior.deferred;}
        const float timedWait=rank(comparison.robustTimedWait,comparison.behaviorTimedWait);
        if(timedWait>value+.001f) {value=timedWait;selected=&comparison.robustTimedWait;behavior=&comparison.behaviorTimedWait;}
        const float timedDeferred=rank(comparison.robustTimedDeferred,comparison.behaviorTimedDeferred);
        if(timedDeferred>value+.001f) {value=timedDeferred;selected=&comparison.robustTimedDeferred;behavior=&comparison.behaviorTimedDeferred;}
    }
    diagnostics.robustDelta=selected->score-comparison.robustPassive.score;
    diagnostics.behaviorDelta=behavior->score-comparison.behaviorPassive.score;
    diagnostics.robustDelayedWorlds=selected->observedDelayedWorlds;
    diagnostics.behaviorDelayedWorlds=behavior->observedDelayedWorlds;
    diagnostics.combinedDelta=value;
    diagnostics.incumbentDelta=rank(comparison.robust.incumbent,comparison.behavior.incumbent);
    const bool fullSelected=selected==&comparison.robust.full;
    const bool improves=value>std::max(0.0f,diagnostics.incumbentDelta)+.001f
        && (!phased || !fullSelected || (value>rank(comparison.robust.prefix,comparison.behavior.prefix)+.001f
            && value>rank(comparison.robust.wait,comparison.behavior.wait)+.001f
            && value>rank(comparison.robust.deferred,comparison.behavior.deferred)+.001f
            && value>rank(comparison.robustTimedWait,comparison.behaviorTimedWait)+.001f
            && value>rank(comparison.robustTimedDeferred,comparison.behaviorTimedDeferred)+.001f))
        && CompareVictory(*selected,comparison.robust.incumbent)>=0;
    const bool capital=state.netEconomy && (ShouldRegroup(*selected,state.budget,state.recoveryReserve)
        || ShouldConserveCapital(*selected,state.budget,state.recoveryReserve,state.capitalRiskAllowance));
    const bool future=phased && (behavior->followupWorldsPurchased<=0
        || behavior->construction.followup.status!=FollowupForecast::Status::Purchased);
    diagnostics.valueRejected=!improves || future;diagnostics.capitalRejected=capital;
    if(continuation) {
        best.assaultDiagnostics.continuationValueRejected+=!improves;
        best.assaultDiagnostics.continuationCapitalRejected+=capital;
    }
    diagnostics.behaviorFutureWorldsPurchased=behavior->followupWorldsPurchased;
    diagnostics.behaviorFutureSpent=behavior->construction.followup.spent;
    diagnostics.behaviorFuturePurchasedAt=behavior->construction.followup.purchasedAt;
    if(improves && !capital && !future) {
        diagnostics.selected=true;
        selected->assaultContinuationSelected=continuation;
        if(phased) {
            selected->phasedPlanning.evaluated=1;selected->phasedPlanning.selected=true;
            selected->phasedPlanning.waitSelected=selected->actions.empty();
            selected->phasedPlanning.prefixScore=comparison.robust.prefix.score;
            selected->phasedPlanning.fullScore=comparison.robust.full.score;
            selected->phasedPlanning.waitScore=comparison.robust.wait.score;
            selected->phasedPlanning.deferredScore=comparison.robust.deferred.score;
            selected->phasedPlanning.forecastFutureSpent=selected->construction.followup.spent;
            selected->phasedPlanning.forecastFuturePurchasedAt=selected->construction.followup.purchasedAt;
        }
        AdoptOutcome(best,std::move(*selected));
        best.effectiveWeights=comparison.robust.full.effectiveWeights;best.stateInputs=comparison.robust.full.stateInputs;
        best.capitalUtilityInputs=comparison.robust.full.capitalUtilityInputs;
        best.baselineBreachSeconds=comparison.robust.full.baselineBreachSeconds;
        best.expandedForecast=comparison.robust.full.expandedForecast;best.fallbackMode=0;best.regrouping=false;
    }
    best.observedDecision=diagnostics;best.timeLimited|=SearchTimeExpired(input);return best;
}

/** 普通搜索与两阶段共享原截止；未完成全部公平对照时保留原已完整评估结果。 */
static Result SearchValueInSession(const Snapshot& input,const Weights& weights,std::uint32_t seed,PlanEvaluator* evaluator) {
    auto ordinaryState=RawWorldCache::CopyView(input);
    // 只有存在已核验的可付完整候选才预留观察分支预算；无效旧案不再白占普通搜索的一半时间。
    const bool observedEligible=input.observedRiskEnabled && ValidWeights(weights) && !SearchTimeExpired(input)
        && !PrepareObservedCandidate(input,{}).plan.first.empty();
    const bool eligible=input.capacity>1 && std::any_of(input.options.begin(),input.options.end(),[&](const Option& o) {
        return CanForecastFollowup(o) && o.cost<=input.budget/2;
    });
    if((eligible || observedEligible) && input.timeLimitedSearch) {
        const auto begin=std::chrono::steady_clock::now();
        ordinaryState.searchDeadline=begin+(input.searchDeadline-begin)*(100-(observedEligible ? 50 : kPhasedBudgetPercent))/100;
    }
    auto best=SearchOrdinary(ordinaryState,weights,seed,evaluator);
    best.observedDecision.profiles=static_cast<int>(input.observedResponse.profiles.size());
    best.observedDecision.revision=input.observedResponse.revision;
    best.observedDecision.riskCap=ObservedPurchaseAllowance(input);
    if(observedEligible && best.precisionTargetID==0 && best.forecastPrecisionTargetID==0 && !SearchTimeExpired(input))
        return SearchObservedCandidate(input,weights,std::move(best));
    if(!eligible || best.fallbackMode!=0 || best.precisionTargetID>0 || best.forecastPrecisionTargetID>0 || SearchTimeExpired(input)) return best;
    auto state=RawWorldCache::CopyView(input);
    if(best.expandedForecast) state.searchVersion=2;
    const auto menu=BuildPhasedPlanningMenu(state,best);
    if(menu.plans.empty()) return best;
    const auto& plan=menu.plans[input.searchRound%menu.plans.size()];
    auto comparison=ComparePhasedPlan(state,weights,plan,best.actions);
    PhasedSearchDiagnostics diagnostics;
    if(!comparison.complete) {
        diagnostics.incomplete=1; best.phasedPlanning=diagnostics; best.timeLimited|=SearchTimeExpired(input); return best;
    }
    diagnostics.evaluated=1;
    diagnostics.prefixScore=comparison.prefix.score; diagnostics.fullScore=comparison.full.score;
    diagnostics.waitScore=comparison.wait.score; diagnostics.deferredScore=comparison.deferred.score;
    auto* selected=&comparison.full;
    if(BetterOutcome(comparison.wait,*selected)) selected=&comparison.wait;
    if(BetterOutcome(comparison.deferred,*selected)) selected=&comparison.deferred;
    const bool fullSelected=selected==&comparison.full;
    const bool value=BetterOutcome(*selected,comparison.incumbent)
        && (!fullSelected || (BetterOutcome(*selected,comparison.prefix)
            && BetterOutcome(*selected,comparison.wait) && BetterOutcome(*selected,comparison.deferred)));
    const bool capital=state.netEconomy && (ShouldRegroup(*selected,state.budget,state.recoveryReserve)
        || ShouldConserveCapital(*selected,state.budget,state.recoveryReserve,
            state.allowWait ? state.capitalRiskAllowance : (std::numeric_limits<float>::max)()));
    diagnostics.valueRejected=!value; diagnostics.capitalRejected=capital;
    // 待机分支仍需真实后队付款才能成为有意义的建议，不把过期空案当成条件采购。
    if(value && !capital && selected->followupWorldsPurchased>0
        && selected->construction.followup.status==FollowupForecast::Status::Purchased) {
        diagnostics.selected=true; diagnostics.waitSelected=selected->actions.empty();
        diagnostics.forecastFutureSpent=selected->construction.followup.spent;
        diagnostics.forecastFuturePurchasedAt=selected->construction.followup.purchasedAt;
        AdoptOutcome(best,std::move(*selected));
        best.effectiveWeights=comparison.full.effectiveWeights; best.stateInputs=comparison.full.stateInputs;
        best.capitalUtilityInputs=comparison.full.capitalUtilityInputs;
        best.baselineBreachSeconds=comparison.full.baselineBreachSeconds; best.regrouping=false;
        best.expandedForecast=comparison.full.expandedForecast;
    }
    best.phasedPlanning=diagnostics; best.timeLimited|=SearchTimeExpired(input);
    return best;
}

const char* TrialRejectReasonName(TrialRejectReason reason) {
    switch(reason) {
    case TrialRejectReason::Disabled: return "disabled";
    case TrialRejectReason::NoCandidate: return "no_candidate";
    case TrialRejectReason::NoRisk: return "no_risk";
    case TrialRejectReason::History: return "history";
    case TrialRejectReason::NoPaidReaction: return "no_paid_reaction";
    case TrialRejectReason::Incomplete: return "incomplete";
    case TrialRejectReason::InvalidCandidate: return "invalid_candidate";
    case TrialRejectReason::NoMaterialPotential: return "no_material_potential";
    case TrialRejectReason::NormalPriority: return "normal_priority";
    case TrialRejectReason::Selected: return "selected";
    }
    return "invalid_reason";
}

bool IsFreshKnownPaidAreaCounter(const Counter& counter) {
    return counter.observedSourceKey>=1 && counter.observedSourceKey<=64 && !counter.blast.committed
        && counter.consumesPlant && !counter.stored && !counter.targeted && counter.plantID==0
        && counter.flightSeconds<=0 && (counter.sunCost>0 || counter.iceCost>0)
        && (counter.clearsCell || counter.clearsLadderRow || counter.ladderClearRadius==1);
}

/** 只记录离散能力，兵种编号、价格、实体身份和本轮剩余时间不生成新的试探资格。 */
static std::uint64_t TrialCapabilities(const Unit& unit) {
    std::uint64_t bits=0;
    int at=0;
    const auto flag=[&](bool present) {if(present) bits|=std::uint64_t{1}<<at; ++at;};
    flag(unit.body.economic);flag(unit.helmHealth>0);flag(unit.shieldHealth>0);
    flag(unit.shieldedAshCap>0);flag(unit.body.smashSeconds>0);flag(unit.engineer);
    flag(unit.clock.present);flag(unit.healer.present);flag(unit.drum.enabled);flag(unit.sniper.enabled);
    flag(unit.ritual.present);flag(unit.dance.leader);flag(unit.goldenDrive.enabled);
    flag(unit.jack.present);flag(unit.jack.elite);flag(unit.catapult.present);
    flag(unit.balloon.present);flag(unit.digger.present);flag(unit.ladder.present);
    flag(unit.instantVehicleCrush);flag(unit.vehicleCrush);flag(unit.burst.range>0);
    flag(unit.repair.amount>0);flag(unit.adaptiveHelmet>0);flag(unit.throwHealth>0);
    flag(unit.hijacker);flag(unit.grounding);flag(unit.insulator);flag(unit.jammer);
    flag(unit.pressure);flag(unit.floodMortar);flag(unit.mowerImmune);
    flag(unit.biteDps>0);flag(unit.body.speed>0);flag(unit.blocksShieldBypass);flag(unit.blocksFumePiercing);
    flag(unit.ferry.present);
    return bits;
}

/** 少量固定档位表示可见防线量级；不会因一帧冷却或单点削血制造无穷签名。 */
static int TrialBand(float value) {
    if(value<=0) return 0;
    if(value<=4) return 1;
    if(value<=16) return 2;
    if(value<=64) return 3;
    if(value<=256) return 4;
    if(value<=1024) return 5;
    return 6;
}

std::uint64_t InformationalTrialSignature(const Snapshot& state,const std::vector<Action>& actions) {
    if(actions.empty() || state.rows<=0 || state.rows>6) return 0;
    int firstRow=state.rows;
    for(const auto& action:actions) {
        if(action.option<0 || action.option>=static_cast<int>(state.options.size())
            || !FinitePhasedValue(action.delay) || action.delay<0) return 0;
        const auto& option=state.options[action.option];
        if(option.device>=0 || option.row<0 || option.row>=state.rows || option.unit.body.row!=option.row) return 0;
        firstRow=std::min(firstRow,option.row);
    }
    std::uint64_t hash=1469598103934665603ull;
    const auto add=[&](std::uint64_t value) {
        for(int byte=0;byte<8;++byte) {hash^=(value>>(byte*8))&255; hash*=1099511628211ull;}
    };
    add(1); // 签名结构版本，规则变化时不可静默复用旧假设
    std::array<float,6> health{},fire{},slow{};
    std::array<int,6> plants{},defense{};
    for(const auto& plant:state.plants) if(plant.health>0 && plant.row>=0 && plant.row<state.rows) {
        const int row=plant.row;
        ++plants[row];health[row]+=plant.health;fire[row]+=plant.dps;slow[row]+=plant.slowRate;
        defense[row]|=(plant.multiTarget ? 1 : 0)|(plant.around ? 2 : 0)|(plant.targetsAir ? 4 : 0)
            |(plant.repairAmount>0 ? 8 : 0)|(plant.hasBurstProtection ? 16 : 0);
    }
    using Member=std::tuple<std::uint64_t,int,int,int,int,int,int,int>;
    std::vector<Member> members;
    for(const auto& action:actions) {
        const auto& option=state.options[action.option];const int row=option.row;
        const int timing=action.delay<=0 ? 0 : action.delay<=IceProduction::Interval*2 ? 1 : 2;
        members.emplace_back(TrialCapabilities(option.unit),row-firstRow,timing,
            TrialBand(static_cast<float>(plants[row])),TrialBand(health[row]),TrialBand(fire[row]),
            TrialBand(slow[row]),defense[row]);
    }
    std::sort(members.begin(),members.end());add(members.size());
    for(const auto& member:members) std::apply([&](auto... value) {(add(value),...);},member);
    // 既有前排/支援改变可检验协同；只记能力与相对行，不因实体换号或向前走了一帧重试旧失败。
    std::vector<std::pair<std::uint64_t,int>> existing;
    for(const auto& unit:state.current) if(unit.body.health>0)
        existing.emplace_back(TrialCapabilities(unit),unit.body.row-firstRow);
    std::sort(existing.begin(),existing.end());add(existing.size());
    for(const auto& [capability,row]:existing) {add(capability);add(row);}
    // source键仅去重同一卡槽的多个合法爆点，不把键值或排列顺序放入签名。
    std::array<std::set<std::pair<int,int>>,8> sources;
    for(const auto& counter:state.counters) {
        const int role=IsFreshKnownPaidAreaCounter(counter) ? (counter.clearsLadderRow ? 0 : counter.clearsCell ? 1 : 2)
            : counter.blast.committed ? 3 : counter.stored ? 4 : !counter.consumesPlant ? 5 : 6;
        sources[role].insert({counter.source,counter.plantID});
        if(IsFreshKnownPaidAreaCounter(counter) && counter.sunCost<=state.playerSun && counter.iceCost<=state.playerIce)
            sources[7].insert({counter.source,0});
    }
    for(const auto& group:sources) add(TrialBand(static_cast<float>(group.size())));
    add(TrialBand(static_cast<float>(state.construction.size())));
    add(TrialBand(static_cast<float>(state.rowStrikes.size())));
    add(TrialBand(static_cast<float>(state.windCounters.size())));
    add(std::count_if(state.mowers.begin(),state.mowers.end(),[](const Mower& mower){return mower.active;}));
    add(state.weatherStation);
    return hash ? hash : 1;
}

struct InformationalTrialAdmission {
    std::vector<Action> actions;
    TrialDiagnostics diagnostics;
};

/** 风险只来自实际钱包和原成交资本；未来生产与假设世界不补充资金。 */
static float TrialOutstandingCapital(const Snapshot& state) {
    float outstanding=0;
    for(const auto& unit:state.current) if(unit.body.health>0) outstanding+=std::max(0.0f,unit.body.purchaseCost);
    if(state.deploymentCapital>=0) outstanding=std::max(outstanding,
        static_cast<float>(std::max<std::int64_t>(0,state.deploymentCapital-state.budget)));
    return outstanding;
}

/** 信息试探只承担一次明确购价；持续自动扣冰的能力仍可通过普通价值搜索购买。 */
bool HasInformationalTrialAutomaticFees(const Unit& unit) {
    return unit.burst.cost>0 || unit.repair.cost>0 || unit.engineer;
}

/** 整案必须不经删改地通过当前修案规则；不把便宜截断版冒充已检验的试探。 */
static bool ExactTrialCart(const Snapshot& state,const std::vector<Action>& actions,int& bill) {
    bill=0;
    if(actions.empty() || actions.size()>static_cast<std::size_t>(DeploymentCapacity(state))) return false;
    std::int64_t total=0;
    for(const auto& action:actions) {
        if(action.option<0 || action.option>=static_cast<int>(state.options.size())
            || !FinitePhasedValue(action.delay) || action.delay<0 || action.delay>DelayLimit(state)) return false;
        const auto& option=state.options[action.option];
        if(option.device>=0 || option.cost<=0 || HasInformationalTrialAutomaticFees(option.unit) || option.row<0 || option.row>=state.rows
            || option.unit.body.row!=option.row || !FinitePhasedValue(option.unit.body.health) || option.unit.body.health<=0) return false;
        total+=option.cost;
    }
    if(total>PurchaseBudget(state) || total>ColdStorageTrials::MaximumPurchaseIce) return false;
    auto repaired=actions;Repair(state,repaired);
    if(repaired.size()!=actions.size() || !std::equal(actions.begin(),actions.end(),repaired.begin(),[](const Action& a,const Action& b) {
        return a.option==b.option && a.delay==b.delay;
    })) return false;
    bill=static_cast<int>(total);return true;
}

/** 只生成由合法能力组成的小额新假设；无最低兵数、等待轮数或孤注一掷升级。 */
static InformationalTrialAdmission PrepareInformationalTrial(const Snapshot& input) {
    InformationalTrialAdmission admission;auto& diagnostics=admission.diagnostics;
    if(!input.informationalTrials || !input.allowWait || input.observedProfile
        || input.precisionTargetID>0 || !input.committed.empty()
        || !input.temporalAnchors.empty() || !input.rifts.empty()
        || std::any_of(input.current.begin(),input.current.end(),[](const Unit& unit) {
            return unit.body.health>0 && (unit.body.spawnAt>0 || HasInformationalTrialAutomaticFees(unit));
        })) return admission;
    diagnostics.reason=TrialRejectReason::NoCandidate;
    CheckForecastCancellation(input);
    if(SearchTimeExpired(input)) {diagnostics.reason=TrialRejectReason::Incomplete;return admission;}
    diagnostics.cap=input.trialHistory.PurchaseCap(static_cast<float>(input.budget),static_cast<float>(input.recoveryReserve),
        input.capitalRiskAllowance,TrialOutstandingCapital(input));
    if(diagnostics.cap<=0) {diagnostics.reason=TrialRejectReason::NoRisk;return admission;}
    diagnostics.removedCounters=static_cast<int>(std::count_if(input.counters.begin(),input.counters.end(),IsFreshKnownPaidAreaCounter));
    if(!diagnostics.removedCounters) {diagnostics.reason=TrialRejectReason::NoPaidReaction;return admission;}
    auto state=RawWorldCache::CopyView(input);ExpandFundedPortfolio(state);
    if(DeploymentCapacity(state)<=0) return admission;
    std::vector<std::vector<int>> groups;
    for(std::size_t i=0;i<state.options.size();++i) {
        const auto& option=state.options[i];
        if(option.device>=0 || option.cost<=0 || HasInformationalTrialAutomaticFees(option.unit) || option.cost>diagnostics.cap || option.row<0 || option.row>=state.rows
            || option.unit.body.row!=option.row || !FinitePhasedValue(option.unit.body.health) || option.unit.body.health<=0) continue;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& found){return state.options[found.front()].type==option.type;});
        if(group==groups.end()) groups.push_back({static_cast<int>(i)});else group->push_back(static_cast<int>(i));
    }
    // 经营能力先提供一/两路及便宜前排的独立假设；战斗兵同样可按完整反事实证明潜力。
    std::stable_sort(groups.begin(),groups.end(),[&](const auto& a,const auto& b) {
        return state.options[a.front()].unit.body.economic>state.options[b.front()].unit.body.economic;
    });
    std::vector<std::vector<Action>> menu;
    std::set<std::uint64_t> signatures;
    const auto add=[&](std::vector<Action> actions) {
        if(menu.size()>=kInformationalTrialCandidateLimit) return;
        std::stable_sort(actions.begin(),actions.end(),[](const Action& a,const Action& b){return a.delay<b.delay;});
        int bill=0;if(!ExactTrialCart(state,actions,bill) || bill>diagnostics.cap) return;
        const auto signature=InformationalTrialSignature(state,actions);
        if(signature && signatures.insert(signature).second) menu.push_back(std::move(actions));
    };
    float delay=IceProduction::Interval;
    for(const auto& counter:state.counters) if(IsFreshKnownPaidAreaCounter(counter) && FinitePhasedValue(counter.windup))
        delay=std::max(delay,std::min(counter.windup,IceProduction::Interval*2));
    delay=std::min(DelayLimit(state),std::ceil(delay/kStep)*kStep);
    // 按姿态交错全部类型，避免第一个类型的全部格位永久填满有限菜单。
    for(int shape:{1,2,0,3}) for(const auto& group:groups) for(int selected:group) {
        CheckForecastCancellation(input);
        if(SearchTimeExpired(input)) {diagnostics.reason=TrialRejectReason::Incomplete;return admission;}
        const auto& option=state.options[selected];
        if(shape==0) {add({{selected,0}});continue;}
        const auto far=std::max_element(group.begin(),group.end(),[&](int a,int b) {
            return std::abs(state.options[a].row-option.row)<std::abs(state.options[b].row-option.row);
        });
        if(shape==1) {
            if(far!=group.end() && state.options[*far].row!=option.row) add({{selected,0},{*far,0}});
            else add({{selected,0},{selected,0}});
        } else if(shape==2 && far!=group.end()) add({{selected,0},{*far,delay}});
        else if(shape==3 && option.unit.body.economic) {
            int front=-1;
            for(const auto& candidates:groups) for(int index:candidates) {
                const auto& candidate=state.options[index];
                if(candidate.unit.body.economic || candidate.row!=option.row || candidate.cost+option.cost>diagnostics.cap) continue;
                if(front<0 || candidate.cost<state.options[front].cost
                    || (candidate.cost==state.options[front].cost && candidate.unit.body.health>state.options[front].unit.body.health)) front=index;
            }
            if(front>=0) add({{front,0},{selected,delay}});
        }
    }
    if(menu.empty()) return admission;
    // 有产能时先交错经营假设与其余能力；大量廉价战斗类型不能把两路经营推到无限远。
    std::stable_partition(menu.begin(),menu.end(),[&](const auto& actions) {
        return std::any_of(actions.begin(),actions.end(),[&](const Action& action) {
            return state.options[action.option].unit.body.economic;
        });
    });
    const auto economicCount=static_cast<std::size_t>(std::count_if(menu.begin(),menu.end(),[&](const auto& actions) {
        return std::any_of(actions.begin(),actions.end(),[&](const Action& action) {
            return state.options[action.option].unit.body.economic;
        });
    }));
    const auto combatCount=menu.size()-economicCount;
    const std::size_t first=!economicCount || !combatCount ? input.searchRound%menu.size()
        : input.searchRound%3!=2 ? (input.searchRound-input.searchRound/3)%economicCount
        : economicCount+(input.searchRound/3)%combatCount;
    // 每轮只完整评价一个候选，已亏损签名跳过；新鲜轮次自身不是签名的一部分。
    for(std::size_t n=0;n<menu.size();++n) {
        CheckForecastCancellation(input);
        if(SearchTimeExpired(input)) {diagnostics.reason=TrialRejectReason::Incomplete;return admission;}
        const auto& actions=menu[(first+n)%menu.size()];
        const auto signature=InformationalTrialSignature(state,actions);
        int bill=0;if(!ExactTrialCart(state,actions,bill)) continue;
        const auto check=state.trialHistory.CheckPurchase(signature,bill,static_cast<float>(state.budget),
            static_cast<float>(state.recoveryReserve),state.capitalRiskAllowance,TrialOutstandingCapital(state));
        diagnostics.signature=signature;diagnostics.bill=bill;diagnostics.cap=check.cap;diagnostics.historyReason=check.reason;
        if(!check.Allowed()) {diagnostics.reason=TrialRejectReason::History;continue;}
        admission.actions=actions;diagnostics.reason=TrialRejectReason::Incomplete;return admission;
    }
    return admission;
}

/** 净收入扣除未来实际能力费；植物值只记攻击造成且未恢复的资产差，不奖励未建植物或对手花钱。 */
static void DescribeTrialMaterial(const Result& cart,const Result& wait,int bill,float& cash,float& plants,bool& breach) {
    cash=cart.features[0]-wait.features[0]+cart.rawProduction+cart.construction.productionAfterWindow
        -wait.rawProduction-wait.construction.productionAfterWindow
        -std::max(0.0f,cart.features[5]-wait.features[5]-bill);
    plants=cart.construction.plantAttackAssets-wait.construction.plantAttackAssets;
    breach=cart.features[2]>wait.features[2];
}

/** 每个响应域只构建一次完整等待基线；即使原缓存已满，也不重复积分四案的等待部分。 */
static bool EvaluateTrialPair(const Snapshot& state,const Weights& baseWeights,const std::vector<Action>& actions,
    Result& cart,Result& wait,const Weights* sharedWeights=nullptr) {
    CheckForecastCancellation(state);
    if(SearchTimeExpired(state)) return false;
    Result initial;initial.features=EvaluateRawWorld(state,{},&initial.construction);Calibrate(state,initial);
    if(SearchTimeExpired(state)) return false;
    const auto inputs=DescribeResultState(state,initial);
    const auto conditioned=ConditionWeights(baseWeights,inputs,state.stateModel);
    const auto weights=sharedWeights ? *sharedWeights : state.netEconomy
        ? AccountForIce(conditioned,CapitalUtilityScale(state)) : conditioned;
    float envelope=0;for(const auto& action:actions) envelope=std::max(envelope,action.delay);
    wait=EvaluatePlan(state,weights,{}, {},0,&initial,nullptr,envelope);
    if(SearchTimeExpired(state)) return false;
    wait.baselineFeatures=wait.features;wait.baselineOpponentAssets=wait.opponentAssets;
    wait.score=Score(wait.features,weights);wait.opponentScore=0;
    cart=EvaluatePlan(state,weights,actions,wait.features,wait.opponentAssets,nullptr,nullptr,envelope);
    if(SearchTimeExpired(state)) return false;
    CheckForecastCancellation(state);
    cart.effectiveWeights=weights;cart.stateInputs=inputs;cart.capitalUtilityInputs=DescribeCapitalUtility(state);
    cart.baselineBreachSeconds=wait.construction.breachSeconds;
    wait.effectiveWeights=weights;wait.stateInputs=inputs;wait.capitalUtilityInputs=cart.capitalUtilityInputs;
    wait.baselineBreachSeconds=wait.construction.breachSeconds;
    return true;
}

/** 诊断与生产试探共用时域及未付款意图边界；已提交事务仍在原快照数值中。 */
static Snapshot PreparePaidAreaResponseState(const Snapshot& input,bool fullForecast,bool& expanded) {
    auto state=RawWorldCache::CopyView(input);
    state.observedProfile=nullptr;state.precisionTargetID=0;state.precisionAdditionalTargetIDs.clear();
    state.precisionUnlockAfterPurchase=false;
    if(fullForecast) state.searchVersion=2;
    expanded=ExpandFundedPortfolio(state) || fullForecast;
    return state;
}

/** 对照实际删除前后的能力清单，独立核对免费、已提交和其他付费反制仍在。 */
static PaidAreaCounterInventory DescribePaidAreaCounters(const Snapshot& state) {
    PaidAreaCounterInventory inventory;
    for(const auto& counter:state.counters) {
        ++inventory.total;
        const bool fresh=IsFreshKnownPaidAreaCounter(counter);
        inventory.freshPaidArea+=fresh;inventory.other+=!fresh;
        inventory.free+=counter.sunCost<=0 && counter.iceCost<=0;
        inventory.committed+=counter.blast.committed;inventory.stored+=counter.stored;
        inventory.reusable+=!counter.consumesPlant;
    }
    return inventory;
}

/** 唯一的四案实现；每个域的等待都带整车延时包络，潜力域沿用保守域实际条件权重。 */
static PaidAreaResponseComparison ComparePaidAreaResponseInSession(const Snapshot& state,const Weights& weights,
    const std::vector<Action>& actions,int requestedVersion,bool expanded) {
    PaidAreaResponseComparison result;
    result.requestedSearchVersion=requestedVersion;result.effectiveSearchVersion=state.searchVersion;
    result.expandedForecast=expanded;result.horizonSeconds=Horizon(state);result.productionWindowSeconds=kHorizon;
    result.patientResponseHoldSeconds=kPatientCounterSeconds;
    result.storedResponseEnvelopeSeconds=kStoredCounterSeconds;
    for(const auto& unit:state.current)
        result.storedResponseEnvelopeSeconds=std::max(result.storedResponseEnvelopeSeconds,unit.body.spawnAt);
    for(const auto& action:actions) {
        if(action.option<0 || action.option>=static_cast<int>(state.options.size()) || !FinitePhasedValue(action.delay)
            || action.delay<0 || state.options[action.option].cost<0
            || state.options[action.option].cost>(std::numeric_limits<int>::max)()-result.bill) return result;
        result.bill+=state.options[action.option].cost;
        result.responseEnvelopeSeconds=std::max(result.responseEnvelopeSeconds,action.delay);
    }
    result.storedResponseEnvelopeSeconds=std::max(result.storedResponseEnvelopeSeconds,result.responseEnvelopeSeconds);
    for(const auto& counter:state.counters) if(IsFreshKnownPaidAreaCounter(counter)) {
        ++result.removedCounterCount;
        result.removedSourceKeys.push_back(counter.observedSourceKey);
    }
    std::sort(result.removedSourceKeys.begin(),result.removedSourceKeys.end());
    result.removedSourceKeys.erase(std::unique(result.removedSourceKeys.begin(),result.removedSourceKeys.end()),
        result.removedSourceKeys.end());
    auto& robust=result.robust;
    if(!EvaluateTrialPair(state,weights,actions,robust.cart,robust.wait)) return result;
    robust.complete=true;robust.cart.expandedForecast=robust.wait.expandedForecast=expanded;
    result.matchesRequested=robust.cart.actions.size()==actions.size()
        && std::equal(robust.cart.actions.begin(),robust.cart.actions.end(),actions.begin(),
            [](const Action& a,const Action& b){return a.option==b.option && a.delay==b.delay;});
    if(!result.matchesRequested) return result;
    DescribeTrialMaterial(robust.cart,robust.wait,result.bill,robust.cash,robust.plantAttackAssets,robust.breach);
    // 只撤未提交的新付费范围反制；两域仍使用相同实际时域、响应选择器和同一份权重。
    const auto contrast=RawWorldCache::WithoutFreshPaidArea(state);
    result.robustCounters=DescribePaidAreaCounters(state);result.potentialCounters=DescribePaidAreaCounters(contrast);
    auto& potential=result.potential;
    if(!EvaluateTrialPair(contrast,weights,actions,potential.cart,potential.wait,&robust.cart.effectiveWeights)) return result;
    CheckForecastCancellation(state);
    potential.complete=true;potential.cart.expandedForecast=potential.wait.expandedForecast=expanded;
    DescribeTrialMaterial(potential.cart,potential.wait,result.bill,potential.cash,potential.plantAttackAssets,potential.breach);
    result.complete=true;
    return result;
}

PaidAreaResponseComparison ComparePaidAreaResponse(const Snapshot& input,const Weights& weights,
    const std::vector<Action>& actions,bool expandToFullForecast) {
    RawWorldCache cache(input.reuseRawWorlds);
    auto root=input;cache.BindRoot(root);
    bool expanded=false;
    const auto state=PreparePaidAreaResponseState(root,expandToFullForecast,expanded);
    auto result=ComparePaidAreaResponseInSession(state,weights,actions,input.searchVersion,expanded);
    result.rawWorldCounts=cache.Counts();
    return result;
}

/** 完成四个同窗整案后才授予信息试探；假设潜力不写回真实保守评分。 */
static Result SearchInformationalTrial(const Snapshot& input,const Weights& weights,Result best,
    const InformationalTrialAdmission& admission) {
    auto diagnostics=admission.diagnostics;
    const auto finish=[&]() {best.trialDiagnostics=diagnostics;best.timeLimited|=SearchTimeExpired(input);return best;};
    if(admission.actions.empty()) return finish();
    if(!best.actions.empty() || best.precisionTargetID>0 || best.forecastPrecisionTargetID>0
        || !best.followupPolicy.actions.empty()) {diagnostics.reason=TrialRejectReason::NormalPriority;return finish();}
    CheckForecastCancellation(input);
    if(SearchTimeExpired(input)) return finish();
    bool expanded=false;
    const auto state=PreparePaidAreaResponseState(input,best.expandedForecast,expanded);
    int bill=0;
    if(!ExactTrialCart(state,admission.actions,bill) || bill!=diagnostics.bill
        || InformationalTrialSignature(state,admission.actions)!=diagnostics.signature) {
        diagnostics.reason=TrialRejectReason::InvalidCandidate;return finish();
    }
    auto comparison=ComparePaidAreaResponseInSession(state,weights,admission.actions,input.searchVersion,expanded);
    if(comparison.robust.complete) {
        if(!comparison.matchesRequested) {diagnostics.reason=TrialRejectReason::InvalidCandidate;return finish();}
        diagnostics.robustScore=comparison.robust.cart.score;
        diagnostics.robustCash=comparison.robust.cash;diagnostics.robustPlantAssets=comparison.robust.plantAttackAssets;
        diagnostics.robustBreach=comparison.robust.breach;
    }
    if(!comparison.complete) return finish();
    diagnostics.complete=true;
    best.evaluated+=4;
    diagnostics.potentialCash=comparison.potential.cash;diagnostics.potentialPlantAssets=comparison.potential.plantAttackAssets;
    diagnostics.potentialBreach=comparison.potential.breach;
    if(!FinitePhasedValue(diagnostics.potentialCash) || !FinitePhasedValue(diagnostics.potentialPlantAssets)
        || (!diagnostics.potentialBreach && diagnostics.potentialCash+diagnostics.potentialPlantAssets+0.001f<bill)) {
        diagnostics.reason=TrialRejectReason::NoMaterialPotential;return finish();
    }
    const auto check=state.trialHistory.CheckPurchase(diagnostics.signature,bill,static_cast<float>(state.budget),
        static_cast<float>(state.recoveryReserve),state.capitalRiskAllowance,TrialOutstandingCapital(state));
    diagnostics.historyReason=check.reason;
    if(!check.Allowed()) {diagnostics.reason=TrialRejectReason::History;return finish();}
    if(SearchTimeExpired(input)) {diagnostics.complete=false;return finish();}
    diagnostics.selected=true;diagnostics.reason=TrialRejectReason::Selected;
    auto& robust=comparison.robust.cart;
    const auto effective=robust.effectiveWeights;const auto inputs=robust.stateInputs;const auto capital=robust.capitalUtilityInputs;
    const auto baselineBreach=robust.baselineBreachSeconds;
    AdoptOutcome(best,std::move(robust));best.fallbackMode=1;
    best.forecastPrecisionTargetID=0;best.forecastPrecisionAdditionalTargetIDs.clear();
    best.forecastPrecisionIce=0;best.forecastPrecisionAimStartSeconds=0;
    best.effectiveWeights=effective;best.stateInputs=inputs;best.capitalUtilityInputs=capital;
    best.baselineBreachSeconds=baselineBreach;best.expandedForecast=expanded;best.regrouping=false;
    return finish();
}

/** 完整复查的旧案仅作起点；用当前钱包、目标、完整反制和无技能等待重新验证。 */
static bool ReconsiderStallProposal(const Snapshot& input,const Weights& baseWeights,Result& result) {
    if(input.stallProposal.empty() && input.stallPrecisionTargets.empty()) return false;
    auto state=RawWorldCache::CopyView(input); state.searchVersion=2;
    state.precisionUnlockAfterPurchase=false;
    state.precisionTargetID=0; state.precisionAdditionalTargetIDs.clear();
    const auto actions=RestoreProposal(state,input.stallProposal,true);
    if(actions.size()!=input.stallProposal.size()) return false;
    auto baseline=EvaluateCandidateInSession(state,baseWeights,{});
    baseline.score=Score(baseline.features,baseline.effectiveWeights); baseline.opponentScore=0;
    if(!input.stallPrecisionTargets.empty()) {
        if(!state.precisionReady || input.stallPrecisionTargets.size()>static_cast<size_t>(state.precisionTargetLimit)) return false;
        for(int id:input.stallPrecisionTargets)
            if(std::none_of(state.plants.begin(),state.plants.end(),[&](const Plant& plant){return plant.id==id && plant.health>0;})) return false;
        state.precisionTargetID=input.stallPrecisionTargets.front();
        state.precisionAdditionalTargetIDs.assign(input.stallPrecisionTargets.begin()+1,input.stallPrecisionTargets.end());
    }
    auto payable=actions; Repair(state,payable);
    long long bill=PrecisionCost(state);
    for(const auto& action:actions) bill+=state.options[action.option].cost;
    if(bill>state.budget) return false;
    if(payable.size()!=actions.size() || !std::equal(payable.begin(),payable.end(),actions.begin(),
        [](const Action& a,const Action& b){return a.option==b.option && a.delay==b.delay;})) return false;
    auto candidate=EvaluatePlan(state,baseline.effectiveWeights,actions,baseline.features,baseline.opponentAssets);
    if(!state.netEconomy) candidate.score-=std::max(0.0f,1+baseline.effectiveWeights[5])*PrecisionCost(state);
    if(state.precisionTargetID>0) {
        auto without=RawWorldCache::CopyView(state); without.precisionTargetID=0; without.precisionAdditionalTargetIDs.clear();
        auto sameArmy=actions.empty() ? baseline : EvaluatePlan(without,baseline.effectiveWeights,actions,baseline.features,baseline.opponentAssets);
        if(!PrecisionWorthwhile(state,candidate,sameArmy)) return false;
    }
    if(SearchTimeExpired(state) || !BetterOutcome(candidate,baseline)
        || ShouldRegroup(candidate,state.budget,state.recoveryReserve)
        || (state.netEconomy && ShouldConserveCapital(candidate,state.budget,state.recoveryReserve,state.capitalRiskAllowance))) return false;
    candidate.effectiveWeights=baseline.effectiveWeights; candidate.stateInputs=baseline.stateInputs;
    candidate.capitalUtilityInputs=baseline.capitalUtilityInputs; candidate.expandedForecast=true;
    candidate.baselineFeatures=baseline.features; candidate.baselineOpponentAssets=baseline.opponentAssets;
    candidate.baselineBreachSeconds=baseline.construction.breachSeconds; candidate.evaluated=1;
    result=std::move(candidate); return true;
}

/** 保留原普通/公开行为/两阶段价值路径；完整复查的旧案先重新验证，新颖试探仍只用有限预算。 */
static Result SearchInSession(const Snapshot& input,const Weights& weights,std::uint32_t seed,PlanEvaluator* evaluator) {
    const bool valid=(input.searchVersion==1 || input.searchVersion==2) && ValidWeights(weights)
        && (!input.stateModel || input.stateModel->IsValid()) && FinitePhasedValue(input.opponentWeight)
        && input.opponentWeight>=0 && input.opponentWeight<=100;
    Result reconsidered;
    if(valid && ReconsiderStallProposal(input,weights,reconsidered)) return reconsidered;
    const auto admission=valid ? PrepareInformationalTrial(input) : InformationalTrialAdmission{};
    auto ordinary=RawWorldCache::CopyView(input);
    if(!admission.actions.empty() && input.timeLimitedSearch && !SearchTimeExpired(input)) {
        const auto begin=std::chrono::steady_clock::now();
        ordinary.searchDeadline=begin+(input.searchDeadline-begin)*(100-kInformationalTrialBudgetPercent)/100;
    }
    auto best=SearchValueInSession(ordinary,weights,seed,evaluator);
    return SearchInformationalTrial(input,weights,std::move(best),admission);
}

Result Search(const Snapshot& input,const Weights& weights,std::uint32_t seed,PlanEvaluator* evaluator) {
    RawWorldCache cache(input.reuseRawWorlds);
    auto state=input;cache.BindRoot(state);
    auto result=SearchInSession(state,weights,seed,evaluator);
    CheckForecastCancellation(state);
    const auto counts=cache.Counts();
    result.rawWorldEvaluations=counts.evaluations;result.rawWorldCacheHits=counts.hits;
    result.rawWorldCacheEntries=counts.entries;result.rawWorldCacheBytes=counts.bytes;
    return result;
}

}
