#include "Game/AI/ColdStorageSearch.h"
#include "Game/Zombie/JackBoxRules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <tuple>

namespace {
using namespace ColdStorageSearch;

/** 固定数值事务的断言；失败时保留具体生命周期契约。 */
void CheckCombatTrace(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED combat trace: "<<message<<'\n';std::exit(1);}
}
bool Near(float a,float b) {return std::abs(a-b)<.001f;}
bool Named(const CombatForecastTrace& event,const char* name) {return event.event && std::strcmp(event.event,name)==0;}

/** 逐字段比较所有非战斗日志标量，避免memcmp把padding、vector所有权当玩法差异。 */
auto GameplayStats(const ConstructionStats& s) {
    return std::tie(s.cobUpgrades,s.constructedCobShots,s.cobTransferredAssets,s.productionAfterWindow,
        s.catapultShots,s.catapultHits,s.catapultBlocks,s.breachSeconds,s.cratersCreated,s.planternResponseGear,
        s.visibleCounterResponse,s.counterSpaceReserved,s.counterShovels,s.counterShovelAssets,s.paidCounterCasts,s.paidDefensesRetained,
        s.stationDischarges,s.stationJams,s.stationCounters,s.stationFogCounters,s.movementBoundsApplied,
        s.goldenAccelerationSteps,s.goldenDrumSteps,s.goldenResidualSteps,s.goldenMaxStacks,
        s.planted,s.exchanges,s.orders,s.abilityIceSpent,s.burstActivations,s.auraActivations,s.armorRepairs,
        s.plantRepairs,s.armorRepairIce,s.plantRepairIce,s.drumBeats,s.drumRecipients,s.precisionHits,
        s.engineerBlocks,s.thunderStuns,s.ladderPlaced,s.ladderClimbs,s.ladderRemoved,s.siegeAccessProgress,
        s.healerCasts,s.healerRecipients,s.healerAmount,s.healerRecoveryCredit,s.jackExplosions,s.jackThrows,
        s.jackBoxHits,s.magneticExtractions,s.dancerSummons,s.workerProtectionProgress,s.engineerReloadIce,
        s.deploymentShots,s.deploymentHits,s.ritualReleases,s.riftSummons,s.riftRedirects,s.interferences,
        s.clockAnchors,s.clockTargets,s.clockRewinds,s.clockRevivals,s.clockRedirects,
        s.sunSpent,s.iceSpent,s.opponentAssets,s.exchangeSun,s.exchangeIce,s.orderSun,s.orderIce,s.pendingIce);
}

/** 独立经济日志也须逐条不变，战斗取证不能开关或重排另一种诊断。 */
void CheckSameStats(const ConstructionStats& a,const ConstructionStats& b) {
    CheckCombatTrace(GameplayStats(a)==GameplayStats(b),"combat tracing preserves every gameplay statistic");
    CheckCombatTrace(a.workerTrace.size()==b.workerTrace.size() && a.counterTrace.size()==b.counterTrace.size(),
        "combat tracing preserves economy trace counts");
    for(size_t i=0;i<a.workerTrace.size();++i) {
        const auto& x=a.workerTrace[i];const auto& y=b.workerTrace[i];
        CheckCombatTrace(std::tie(x.id,x.row,x.at,x.x,x.health,x.income)==std::tie(y.id,y.row,y.at,y.x,y.health,y.income),
            "combat tracing preserves each worker economy sample");
    }
    for(size_t i=0;i<a.counterTrace.size();++i) {
        const auto& x=a.counterTrace[i];const auto& y=b.counterTrace[i];
        CheckCombatTrace(std::tie(x.at,x.x,x.damage,x.row,x.column,x.clearsCell)==std::tie(y.at,y.x,y.damage,y.row,y.column,y.clearsCell),
            "combat tracing preserves each economy counter sample");
    }
}

struct Projection {Weights features;ConstructionStats stats;};

/** 每个夹具都先关后开取证，严格相等而非容差掩盖积分分支变化。 */
Projection Paired(Snapshot state,const std::vector<Action>& actions={}) {
    state.traceCombat=false;
    ConstructionStats plain;
    const auto features=Evaluate(state,actions,&plain);
    CheckCombatTrace(plain.combatTrace.empty() && !plain.combatTraceTruncated && plain.nextCombatAttackID==0,
        "ordinary Evaluate does not allocate combat events or assign diagnostic attack IDs");
    state.traceCombat=true;
    Projection traced;
    traced.features=Evaluate(state,actions,&traced.stats);
    CheckCombatTrace(features==traced.features,"all eight feature values are exactly identical with tracing on or off");
    CheckSameStats(plain,traced.stats);
    return traced;
}

/** 保留现有Jack/balloon夹具的几何，静止、不啃食，不从品种名字推断能力。 */
Unit Body(int id,float health=900,float x=900,float cost=24) {
    Unit unit;
    unit.id=id;unit.body.health=unit.maximumBody=health;unit.body.x=x;unit.body.speed=0;
    unit.body.purchaseCost=cost;unit.body.boundsOffset=-25;unit.body.boundsWidth=50;
    unit.boundsY=-50;unit.boundsHeight=100;unit.biteDps=0;
    return unit;
}

/** 精英盒的能力显式来自JackBoxRules，生命与停用边界复用既有反事实夹具。 */
Unit Thrower(int id=1) {
    auto unit=Body(id);
    unit.jack.present=unit.jack.elite=true;unit.jack.remaining=0;unit.jack.stopHealth=300;
    return unit;
}

/** 已知格中心与实体边界；耐久高以保留重复事务供ID核对。 */
Plant Target(int id=10,float x=200,float health=10000,int column=0) {
    Plant plant;
    plant.id=id;plant.x=x;plant.y=100;plant.column=column;
    plant.health=plant.maximumHealth=health;plant.reward=100;plant.targetValue=100;plant.edible=false;
    return plant;
}

/** 固定行高并隔离进屋、额外经济与自由搜索。 */
Snapshot Arena() {
    Snapshot state;state.rows=1;state.houseX=-10000;state.rowY={100,200,300,400,500,600};
    return state;
}

/** 已提交灰烬不依赖虚构卡槽、钱包或玩家未来选择。 */
Counter Ash(float damage,float at=0,float x=900) {
    Counter counter;
    counter.blast.committed=true;counter.blast.ready=at;counter.blast.damage=damage;counter.blast.x=x;
    counter.blast.reach.fill(-1);counter.blast.reach[0]=0;
    return counter;
}

/** 复用Cob forecast的活炮、前摇两秒与独立飞行两秒，单发冷却隔离生命周期。 */
Snapshot CannonArena() {
    auto state=Arena();state.traceEconomy=true;
    state.plants={Target(91001,300,300,1)};
    state.current={Body(71,100000,900,10000)};
    auto shot=Ash(1800,0);
    shot.blast.committed=false;shot.plantID=91001;shot.consumesPlant=false;
    shot.windup=4;shot.flightSeconds=2;shot.recharge=1000;shot.blast.reach[0]=115;shot.ladderClearRow=0;
    state.counters={shot};
    return state;
}

/** 按事件名取引用，调用者只在原stats仍存活且不修改容器时使用。 */
std::vector<const CombatForecastTrace*> Events(const ConstructionStats& stats,const char* name) {
    std::vector<const CombatForecastTrace*> found;
    for(const auto& event:stats.combatTrace) if(Named(event,name)) found.push_back(&event);
    return found;
}

/** 唯一事件用例不能悄悄选中重复日志的第一条。 */
const CombatForecastTrace& Only(const ConstructionStats& stats,const char* name) {
    const auto found=Events(stats,name);
    CheckCombatTrace(found.size()==1,"the expected lifecycle event occurs exactly once");
    return *found.front();
}

/** 起始事件统一编号，所有后继必须指向已经出现的同一攻击，不复用ID。 */
void CheckAttackLinks(const ConstructionStats& stats) {
    std::set<int> attacks;
    for(const auto& event:stats.combatTrace) {
        CheckCombatTrace(event.event && event.reason,"event names and reasons remain readable after Evaluate returns");
        if(Named(event,"unit_present") || Named(event,"unit_spawn")) {
            CheckCombatTrace(event.attackID==0,"spawn records do not consume attack IDs");continue;
        }
        const bool starts=Named(event,"jack_throw") || Named(event,"jack_inflight") || Named(event,"counter_lock")
            || Named(event,"counter_inflight") || Named(event,"counter_committed");
        CheckCombatTrace(event.attackID>0,"every recorded attack has a positive diagnostic identity");
        if(starts) CheckCombatTrace(attacks.insert(event.attackID).second,"each attack starts with a unique ID");
        else CheckCombatTrace(attacks.count(event.attackID)==1,"launch, impact and cancel refer to a recorded attack");
    }
}
}

/** 仅检查取证保真与生命周期；不要求指挥官固定购买任何名字或阵容。 */
void RunColdStorageCombatTraceTests() {
    {
        auto state=Arena();auto worker=Body(11);worker.body.economic=true;worker.productionRemaining=0;
        state.current={worker};state.counters={Ash(1,1)};
        const auto combatOnly=Paired(state);
        CheckCombatTrace(!combatOnly.stats.combatTrace.empty() && combatOnly.stats.workerTrace.empty()
            && combatOnly.stats.counterTrace.empty(),"combat tracing is independent from the economy opt-in");
        state.traceEconomy=true;
        const auto both=Paired(state);
        CheckCombatTrace(!both.stats.workerTrace.empty() && both.stats.counterTrace.size()==1,
            "both explicit trace modes can coexist without suppressing either stream");
        state.budget=100;state.capacity=2;
        Option option;option.unit=Body(0);option.cost=24;state.options={option};
        const auto plain=EvaluateCandidate(state,InitialWeights,{{0,1}});
        state.traceCombat=true;
        const auto traced=EvaluateCandidate(state,InitialWeights,{{0,1}});
        CheckCombatTrace(plain.features==traced.features && plain.baselineFeatures==traced.baselineFeatures
            && plain.score==traced.score && plain.opponentScore==traced.opponentScore
            && plain.effectiveWeights==traced.effectiveWeights && plain.rawProduction==traced.rawProduction,
            "candidate scoring and its same-window baseline are exactly unchanged");
        CheckSameStats(plain.construction,traced.construction);
    }
    {
        auto state=Arena();state.current={Thrower()};state.plants={Target()};
        const auto result=Paired(state);
        const auto thrown=Events(result.stats,"jack_throw"),hits=Events(result.stats,"jack_impact");
        CheckCombatTrace(thrown.size()>1 && hits.size()==thrown.size() && hits.size()==static_cast<size_t>(result.stats.jackBoxHits),
            "each completed repeated throw has exactly one corresponding box impact");
        CheckCombatTrace(Near(thrown[0]->at,.5f) && Near(thrown[0]->resolveAt,.5f+JackBoxRules::Flight)
            && Near(hits[0]->at,1.5f),"scheduled fractional flight time remains distinct from the half-second impact step");
        for(size_t i=0;i<hits.size();++i) {
            CheckCombatTrace(thrown[i]->attackID==hits[i]->attackID && thrown[i]->sourceID==1 && hits[i]->sourceID==1
                && thrown[i]->x==hits[i]->x && thrown[i]->y==hits[i]->y && thrown[i]->resolveAt==hits[i]->resolveAt,
                "a thrown box keeps its owner, frozen landing position and scheduled impact identity");
            CheckCombatTrace(hits[i]->targets.size()==1 && hits[i]->targets[0].id==10
                && Near(hits[i]->targets[0].before-hits[i]->targets[0].after,JackBoxRules::BoxDamage),
                "box traces report actual recipient health rather than guessed nominal kills");
        }
        CheckAttackLinks(result.stats);
        CheckCombatTrace(state.current[0].jack.remaining==0 && state.plants[0].health==10000,
            "tracing does not mutate captured throw timers or plant health");
    }
    {
        auto state=Arena();state.current={Thrower()};state.plants={Target()};state.counters={Ash(1800,.5f)};
        const auto result=Paired(state);
        const auto& thrown=Only(result.stats,"jack_throw");const auto& cancelled=Only(result.stats,"jack_cancel");
        CheckCombatTrace(result.stats.jackThrows==1 && result.stats.jackBoxHits==0 && Events(result.stats,"jack_impact").empty()
            && cancelled.attackID==thrown.attackID && Near(cancelled.at,1) && cancelled.resolveAt==thrown.resolveAt
            && std::strcmp(cancelled.reason,"source_ash_removed_before_revival")==0,
            "ash removal cancels exactly the already-thrown box at cleanup, preserving its original due time");
        CheckAttackLinks(result.stats);
    }
    {
        auto state=Arena();auto owner=Thrower();owner.jack.remaining=1000;owner.jack.stopHealth=900;
        state.current={owner};state.plants={Target()};state.jackBoxes={{200,100,2,false,owner.id}};
        state.counters={Ash(1800,.5f)};
        TemporalAnchor anchor;anchor.at=1;anchor.targets={{0,owner}};state.temporalAnchors={anchor};
        const auto result=Paired(state);
        CheckCombatTrace(result.stats.clockRevivals==1 && result.stats.jackBoxHits==0
            && Events(result.stats,"jack_impact").empty() && Only(result.stats,"jack_cancel").attackID==Only(result.stats,"jack_inflight").attackID,
            "reviving an ash-removed owner cannot resurrect or duplicate its cancelled box");
    }
    {
        for(bool alreadyDead:{false,true}) {
            auto state=Arena();auto owner=Thrower();owner.jack.remaining=1000;
            if(alreadyDead) owner.body.health=0;
            state.current={owner};auto plant=Target();if(!alreadyDead) plant.dps=100000;
            state.plants={plant};state.jackBoxes={{200,100,1,false,owner.id}};
            const auto result=Paired(state);
            const auto& start=Only(result.stats,"jack_inflight");const auto& hit=Only(result.stats,"jack_impact");
            CheckCombatTrace(result.stats.jackThrows==0 && result.stats.jackBoxHits==1 && result.features[3]==0
                && Events(result.stats,"jack_cancel").empty() && hit.attackID==start.attackID && Near(hit.at,1)
                && Near(hit.targets[0].before-hit.targets[0].after,JackBoxRules::BoxDamage),
                "ordinary death before or during a captured flight permits exactly its original impact");
        }
    }
    {
        auto state=Arena();auto under=Target(10,440,500,3);under.layer=0;
        auto host=Target(11,440,500,3);auto shell=Target(12,440,100,3);shell.layer=2;shell.pumpkin=true;
        state.plants={under,host,shell};state.jackBoxes={{440,100,0,false,99}};
        const auto result=Paired(state);const auto& impact=Only(result.stats,"jack_impact");
        CheckCombatTrace(impact.targets.size()==1 && impact.targets[0].id==12 && impact.targets[0].before==100
            && impact.targets[0].after==0 && !impact.targets[0].activeAfter
            && impact.targets[0].requestedDamage==JackBoxRules::BoxDamage*state.pumpkinDamageMultiplier,
            "multiple protected layers produce one broken-pumpkin recipient without spillover trace targets");
    }
    {
        for(float removedAt:{1.0f,2.0f,3.0f}) {
            auto state=CannonArena();state.pendingPrecisionID=91001;state.pendingPrecisionRemaining=removedAt;
            const auto result=Paired(state);const auto& locked=Only(result.stats,"counter_lock");
            CheckCombatTrace(Near(locked.at,0) && Near(locked.resolveAt,4) && locked.sourceID==91001
                && locked.sourceRow==0 && locked.sourceColumn==1,"cannon lock retains real source cell and full impact deadline");
            if(removedAt<=2) {
                const auto& cancelled=Only(result.stats,"counter_cancel");
                CheckCombatTrace(cancelled.attackID==locked.attackID && Near(cancelled.at,removedAt)
                    && Events(result.stats,"counter_launch").empty() && Events(result.stats,"counter_impact").empty()
                    && result.stats.counterTrace.empty(),"source death before or exactly at launch cancels without inventing a shell");
            } else {
                const auto& launched=Only(result.stats,"counter_launch");const auto& impact=Only(result.stats,"counter_impact");
                CheckCombatTrace(launched.attackID==locked.attackID && impact.attackID==locked.attackID
                    && Near(launched.at,2) && Near(impact.at,4) && Events(result.stats,"counter_cancel").empty()
                    && result.stats.counterTrace.size()==1 && impact.targets.size()==1 && impact.targets[0].after==98200,
                    "source death after launch leaves exactly one independent 1800-damage impact");
            }
            CheckAttackLinks(result.stats);
        }
    }
    {
        for(float removedAt:{1.0f,3.0f}) {
            auto state=CannonArena();state.counters[0].blast.committed=true;state.counters[0].blast.ready=4;
            state.pendingPrecisionID=91001;state.pendingPrecisionRemaining=removedAt;
            const auto result=Paired(state);
            const auto& pending=Only(result.stats,"counter_committed");
            CheckCombatTrace(Events(result.stats,"counter_inflight").empty() && Near(pending.resolveAt,4),
                "captured committed pre-launch work is not mislabeled as airborne");
            CheckCombatTrace(removedAt<2 ? Events(result.stats,"counter_impact").empty()
                : Only(result.stats,"counter_impact").attackID==pending.attackID,
                "captured source-bound work detaches only at its actual release");
            CheckAttackLinks(result.stats);
        }
        auto state=CannonArena();state.counters[0].blast.committed=true;state.counters[0].blast.ready=4;
        state.counters[0].plantID=0;state.pendingPrecisionID=91001;state.pendingPrecisionRemaining=1;
        const auto result=Paired(state);
        CheckCombatTrace(Only(result.stats,"counter_inflight").attackID==Only(result.stats,"counter_impact").attackID
            && Events(result.stats,"counter_launch").empty() && Events(result.stats,"counter_cancel").empty(),
            "an already-airborne captured shell neither relaunches nor depends on the destroyed plant");
    }
    {
        auto state=Arena();auto door=Body(71,1300,900,12);door.maximumBody=270;door.temporalStopHealth=90;
        door.maximumShield=door.shieldHealth=1030;door.shieldedAshCap=320;
        state.current={door};state.counters.assign(4,Ash(1800));
        const auto result=Paired(state);const auto impacts=Events(result.stats,"counter_impact");
        CheckCombatTrace(impacts.size()==4 && impacts[0]->targets.size()==1 && impacts[3]->targets.size()==1,
            "four ash impacts keep four separate actual damage records");
        const auto& first=impacts[0]->targets[0];const auto& last=impacts[3]->targets[0];
        CheckCombatTrace(first.requestedDamage==1800 && first.before==1300 && first.after==980
            && first.beforeShield==1030 && first.afterShield==710 && first.beforeBody==270 && first.afterBody==270,
            "nominal ash and shield-capped actual layer losses stay distinct");
        CheckCombatTrace(last.after==20 && last.afterBody==20 && last.afterShield==0 && last.activeAfter
            && last.headlessAfter && !last.headlessBefore && result.features[3]==0,
            "impact records the living headless boundary before later natural bleeding ends its lifetime");
        CheckAttackLinks(result.stats);
    }
    {
        auto state=Arena();state.searchVersion=2;
        auto present=Body(101);present.throwHealth=1;state.current={present,Body(102)};state.current[1].body.spawnAt=2;
        Rift rift;rift.unit=Body(103);rift.remaining=1;state.rifts={rift};
        Option device;device.device=0;device.cost=5;
        Option troop;troop.unit=Thrower(0);troop.cost=24;
        state.options={device,troop};state.plants={Target()};
        const auto result=Paired(state,{{0,0},{1,1},{0,0},{1,3}});
        const auto spawns=Events(result.stats,"unit_spawn");std::set<int> mappedIDs;std::set<int> indices;
        for(const auto* event:spawns) if(event->actionIndex>=0) {
            mappedIDs.insert(event->sourceID);indices.insert(event->actionIndex);
            CheckCombatTrace(event->sourceID<0 && Near(event->at,event->resolveAt),
                "planned births use generated stable IDs and the original action delay");
        }
        CheckCombatTrace(indices==std::set<int>({1,3}) && mappedIDs.size()==2 && spawns.size()==4
            && Events(result.stats,"unit_present").size()==1,
            "current, queued, rift and reserved child slots do not shift indices past skipped device actions");
        for(const auto* event:Events(result.stats,"jack_throw"))
            CheckCombatTrace(mappedIDs.count(event->sourceID)==1,"every planned throw maps back to one recorded action birth");
        CheckAttackLinks(result.stats);
    }
    {
        auto state=Arena();state.playerSun=100;
        state.current={Body(71,1000,900,100)};
        auto card=Ash(300);card.blast.committed=false;card.sunCost=100;card.cellRow=0;card.cellColumn=8;
        card.deploymentHealth=300;card.windup=1;state.counters={card};
        const auto result=Paired(state);const auto& locked=Only(result.stats,"counter_lock");
        const auto& impact=Only(result.stats,"counter_impact");
        CheckCombatTrace(locked.sourceID<0 && locked.sourceID==impact.sourceID && locked.attackID==impact.attackID
            && locked.sourceRow==0 && locked.sourceColumn==8 && result.stats.paidCounterCasts==1,
            "a newly planted ash source receives its actual identity before the lock event is frozen");
    }
    {
        auto state=Arena();state.plants={Target(10,200,1000000)};
        constexpr int boxes=2100; // 4200个在途/命中事件跨越4096条诊断上限，不增加搜索预算
        state.jackBoxes.assign(boxes,JackBoxFlight{200,100,0,false,99});
        const auto result=Paired(state);
        CheckCombatTrace(result.stats.combatTraceTruncated && result.stats.combatTrace.size()==4096
            && result.stats.jackBoxHits==boxes && Near(result.features[1],10.5f),
            "event truncation is explicit and all remaining box damage still contributes to the battle result");
        CheckAttackLinks(result.stats);
    }
    std::cout<<"Combat trace opt-in, equivalence, source lifetimes, attack mapping and truncation contracts passed\n";
}
