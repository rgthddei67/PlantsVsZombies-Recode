#include "TestDriver.h"
#include "Game/Board/Board.h"
#include "Game/AI/ColdStorageSearch.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/BalloonZombie.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include "Game/Cell.h"
#include "Game/GameScene.h"
#include "Game/SceneManager.h"
#include "GameApp.h"
#include "GameInfoSaver.h"
#include "DeltaTime.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <filesystem>

namespace {
using Json=nlohmann::json;
/** 两个真实时钟并存：经营时钟随存档恢复，总时钟是本进程固定步累计值。 */
Json CombatClock(const Board& board) {
    return {{"at",board.mColdStorage.elapsed},{"gameTime",DeltaTime::GetTotalTime()},
        {"boardFrame",board.mBoardFrame},{"dt",DeltaTime::GetDeltaTime()},{"timeScale",DeltaTime::GetTimeScale()}};
}
}

std::uint64_t TestDriver::CommanderCombatAttackID(bool cob,int sourceID,int projectileID) const noexcept {
    if(!CommanderCombatTrace()) return 0;
    if(projectileID>0) {
        const auto found=mCommanderCombatProjectiles.find(projectileID);
        if(found!=mCommanderCombatProjectiles.end()) return found->second.attackID;
    }
    const auto found=mCommanderCombatSources.find({cob,sourceID});
    return found==mCommanderCombatSources.end() ? 0 : found->second;
}

std::uint64_t TestDriver::RecordCommanderCombatEvent(const Board& board,Json event) noexcept {
    if(!CommanderCombatTrace()) return 0;
    try {
        if(mCommanderCombatRecordCount>=mCommanderCombatLimit) {mCommanderCombatTruncated=true;return 0;}
        const std::string name=event.value("event",std::string{});
        const bool cob=name.starts_with("cob_");
        int source=event.value("sourceID",0);
        const int projectile=event.value("projectileID",0);
        std::uint64_t attack=event.value("attackID",std::uint64_t{0});
        if(name=="jack_throw" || name=="cob_lock") {
            attack=++mCommanderAttackSequence;
            mCommanderCombatSources[{cob,source}]=attack;
        } else if(name=="jalapeno_impact") {
            // 植物 ID 与僵尸 ID 是不同命名空间；一次性辣椒不借用小丑的来源映射。
            attack=++mCommanderAttackSequence;
        } else if(attack==0) attack=CommanderCombatAttackID(cob,source,projectile);
        const bool observedWithoutStart=attack==0;
        if(attack==0 && (name=="jack_impact" || name=="cob_impact" || name=="cob_launch")) {
            // 重载只知道在途状态：给本次实际事件新诊断身份，明确不补猜原锁定/投出时刻。
            attack=++mCommanderAttackSequence;
            if(source>0) mCommanderCombatSources[{cob,source}]=attack;
        }
        if(cob && projectile>0) {
            const auto found=mCommanderCombatProjectiles.find(projectile);
            if(found!=mCommanderCombatProjectiles.end()) {
                source=found->second.sourceID;
                event["sourceRow"]=found->second.row;event["sourceColumn"]=found->second.col;
                event["sourceID"]=source;
            }
        }
        if(name=="cob_launch" && projectile>0)
            mCommanderCombatProjectiles[projectile]={attack,source,event.value("sourceRow",-1),event.value("sourceColumn",-1)};
        if(!event.contains("targetColumn") && event.contains("targetRow") && event.contains("x") && board.mColumns>1) {
            const float first=board.GetCellCenterPosition(0,0).x;
            const float width=board.GetCellCenterPosition(0,1).x-first;
            const int column=width>0 ? static_cast<int>(std::floor((event.at("x").get<float>()-first)/width+.5f)) : -1;
            event["targetColumn"]=column>=0 && column<board.mColumns ? column : -1;
        }
        event.update(CombatClock(board));
        event["seq"]=++mCommanderCombatSequence;event["epoch"]=mCommanderCombatEpoch;
        if(name.starts_with("jack_") || cob || name=="jalapeno_impact") {
            event["attackID"]=attack;event["observedWithoutStart"]=observedWithoutStart;
            event["sourceID"]=source;
            if(!event.contains("sourceRow")) event["sourceRow"]=-1;
            if(!event.contains("sourceColumn")) event["sourceColumn"]=-1;
        }
        if(name=="jack_impact" || name=="cob_impact" || name=="jalapeno_impact") {
            event["targets"]=Json::array();
            if(attack>0) mCommanderImpactEvents[attack]=mCommanderCombatEvents.size();
        }
        mCommanderCombatEvents.push_back(std::move(event));++mCommanderCombatRecordCount;
        if(name=="jack_cancel" || name=="cob_cancel") mCommanderCombatSources.erase({cob,source});
        if(name=="cob_impact" || (name=="cob_cancel" && projectile>0)) mCommanderCombatProjectiles.erase(projectile);
        return attack;
    } catch(...) {mCommanderCombatFailed=true;return 0;}
}

TestDriver::CombatTargetState TestDriver::CommanderCombatPlantState(const Plant& plant) const noexcept {
    CombatTargetState state;
    state.id=plant.mPlantID;state.type=static_cast<int>(plant.mPlantType);state.row=plant.mRow;state.column=plant.mColumn;
    state.x=plant.GetPosition().x;state.y=plant.GetPosition().y;state.health=state.body=plant.mPlantHealth;state.active=plant.IsActive();
    if(plant.mBoard) if(const auto* cell=plant.mBoard->GetCell(plant.mRow,plant.mColumn)) {
        if(cell->GetFoundationPlantID()==plant.mPlantID) state.layer=4;
        else if(cell->GetUnderPlantID()==plant.mPlantID) state.layer=0;
        else if(cell->GetNormalPlantID()==plant.mPlantID) state.layer=1;
        else if(cell->GetPumpkinPlantID()==plant.mPlantID) state.layer=2;
        else if(cell->GetOverlayPlantID()==plant.mPlantID) state.layer=3;
    }
    return state;
}

TestDriver::CombatTargetState TestDriver::CommanderCombatZombieState(const Zombie& zombie) const noexcept {
    CombatTargetState state;
    const auto* balloon=dynamic_cast<const BalloonZombie*>(&zombie);
    state.id=zombie.mZombieID;state.type=static_cast<int>(zombie.mZombieType);state.row=zombie.mRow;
    state.x=zombie.GetPosition().x;state.y=zombie.GetPosition().y;
    state.extra=balloon ? balloon->GetBalloonHealth() : 0;
    state.body=zombie.mBodyHealth;state.helm=zombie.mHelmHealth;state.shield=zombie.mShieldHealth;
    state.health=zombie.GetCountableExecutionHealth()+state.extra;
    state.active=zombie.IsActive();state.dying=zombie.IsDying();state.headless=!zombie.HasHead();
    return state;
}

void TestDriver::RecordCommanderCombatDamage(const Board&,std::uint64_t attackID,const char* kind,
    int requestedDamage,const CombatTargetState& before,const CombatTargetState& after) noexcept {
    if(!CommanderCombatTrace() || attackID==0) return;
    try {
        if(mCommanderCombatRecordCount>=mCommanderCombatLimit) {mCommanderCombatTruncated=true;return;}
        const auto found=mCommanderImpactEvents.find(attackID);
        if(found==mCommanderImpactEvents.end()) return;
        auto hit=Json{{"id",before.id},{"type",before.type},{"kind",kind},{"row",before.row},{"column",before.column},
            {"x",before.x},{"y",before.y},{"requestedDamage",requestedDamage},{"before",before.health},{"after",after.health},
            {"beforeBody",before.body},{"afterBody",after.body},{"beforeHelm",before.helm},{"afterHelm",after.helm},
            {"beforeShield",before.shield},{"afterShield",after.shield},{"beforeExtra",before.extra},{"afterExtra",after.extra},
            {"activeBefore",before.active},{"activeAfter",after.active},{"dyingBefore",before.dying},{"dyingAfter",after.dying},
            {"headlessBefore",before.headless},{"headlessAfter",after.headless},
            {"becameInactive",before.active && !after.active},{"becameDying",!before.dying && after.dying}};
        if(std::string_view(kind)=="plant") {
            constexpr const char* layers[]={"under","normal","pumpkin","overlay"};
            hit["layer"]=before.layer>=0 && before.layer<4 ? layers[before.layer] : "unknown";
            hit["typeName"]=GameDataManager::GetInstance().PlantTypeToEnumName(static_cast<PlantType>(before.type));
        } else hit["typeName"]=GameDataManager::GetInstance().ZombieTypeToEnumName(static_cast<ZombieType>(before.type));
        // 名义伤害和层生命减少分开；化灰可以保持旧 HP 而结束生命周期。
        hit["healthLost"]=std::max(0,before.health)-std::max(0,after.health);
        mCommanderCombatEvents.at(found->second)["targets"].push_back(std::move(hit));
        ++mCommanderCombatRecordCount;
    } catch(...) {mCommanderCombatFailed=true;}
}

Json TestDriver::CommanderCombatForecast(const ColdStorageSearch::ConstructionStats& stats) {
    Json events=Json::array();
    for(const auto& e:stats.combatTrace) {
        Json hits=Json::array();
        for(const auto& h:e.targets) hits.push_back({{"id",h.id},{"type",h.type},{"row",h.row},{"column",h.column},
            {"x",h.x},{"y",h.y},{"requestedDamage",h.requestedDamage},{"before",h.before},{"after",h.after},
            {"beforeBody",h.beforeBody},{"afterBody",h.afterBody},{"beforeHelm",h.beforeHelm},{"afterHelm",h.afterHelm},
            {"beforeShield",h.beforeShield},{"afterShield",h.afterShield},{"activeBefore",h.activeBefore},
            {"activeAfter",h.activeAfter},{"headlessBefore",h.headlessBefore},{"headlessAfter",h.headlessAfter}});
        events.push_back({{"seq",events.size()+1},{"event",e.event ? e.event : ""},{"reason",e.reason ? e.reason : ""},
            {"at",e.at},{"x",e.x},{"y",e.y},{"resolveAt",e.resolveAt},{"attackID",e.attackID},
            {"sourceID",e.sourceID},{"sourceRow",e.sourceRow},{"sourceColumn",e.sourceColumn},
            {"targetRow",e.targetRow},{"targetColumn",e.targetColumn},{"actionIndex",e.actionIndex},{"targets",std::move(hits)}});
    }
    Json workers=Json::array(),counters=Json::array();
    for(const auto& e:stats.workerTrace) workers.push_back({{"id",e.id},{"row",e.row},{"at",e.at},
        {"x",e.x},{"health",e.health},{"income",e.income}});
    for(const auto& e:stats.counterTrace) counters.push_back({{"at",e.at},{"x",e.x},{"damage",e.damage},
        {"row",e.row},{"column",e.column},{"clearsCell",e.clearsCell}});
    Json realized=Json::array();
    for(const auto& a:stats.followup.realizedActions) realized.push_back({{"option",a.option},{"delay",a.delay}});
    const Json future={{"status",static_cast<int>(stats.followup.status)},
        {"observedAt",stats.followup.observedAt},{"purchasedAt",stats.followup.purchasedAt},
        {"spent",stats.followup.spent},{"walletBefore",stats.followup.walletBefore},{"walletAfter",stats.followup.walletAfter},
        {"realizedActions",std::move(realized)}};
    return {{"clock","snapshot_relative_seconds"},{"activeSemantics","forecast_health_positive"},
        {"truncated",stats.combatTraceTruncated},{"events",std::move(events)},
        {"workerTrace",std::move(workers)},{"counterTrace",std::move(counters)},
        {"paidCounterCasts",stats.paidCounterCasts},{"counterSpaceReserved",stats.counterSpaceReserved},
        {"paidDefensesRetained",stats.paidDefensesRetained},{"visibleCounterResponse",stats.visibleCounterResponse},
        {"sunSpent",stats.sunSpent},{"iceSpent",stats.iceSpent},
        {"productionAfterWindow",stats.productionAfterWindow},{"followup",future}};
}

std::uint64_t TestDriver::CaptureCommanderPlanBeforePayment(Board& board,const ColdStorageSearch::Snapshot& search,
    const ColdStorageSearch::Result& result,int requestedVersion,float age,const std::vector<std::uint64_t>& tickets,std::uint32_t searchSeed) noexcept {
    if(!CommanderCombatTrace() || mCommanderCaptureMaximum<=0
        || mCommanderPlanCaptures.size()>=static_cast<std::size_t>(mCommanderCaptureMaximum)) return 0;
    try {
        int jacks=0,economic=0,cost=0;
        for(const auto& a:result.actions) {
            const auto& o=search.options.at(a.option);cost+=o.cost;
            if(o.device<0 && o.type==static_cast<int>(ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX)) ++jacks;
            if(o.device<0 && o.unit.body.economic) ++economic;
        }
        if(jacks<mCommanderCaptureMinimumJacks || economic<mCommanderCaptureMinimumEconomicUnits
            || cost<mCommanderCaptureMinimumCost) return 0;
        const std::uint64_t id=mCommanderPlanCaptures.size()+1;
        const auto& s=board.mColdStorage;
        Json record={{"captureID",id},{"epoch",mCommanderCombatEpoch},{"clock",CombatClock(board)},
            {"status","attempted"},{"semantics","selected_original_numeric_world_and_real_pre_payment_save"},
            {"snapshotReloadCaveat","Reload resets transient planner state; a replay is not an exact historical reproduction"},
            {"runnerSeed",mCommanderDeclaredRunnerSeed},{"searchSeed",searchSeed},{"sampleAt",s.elapsed-age},{"commitAt",s.elapsed},{"age",age},
            {"searchSerial",s.searchSerial+1},{"searchElapsed",s.elapsed},{"previousSearchElapsed",s.searchElapsed},{"requestedVersion",requestedVersion},
            {"expandedForecast",result.expandedForecast},{"budget",search.budget},{"capacity",search.capacity},
            {"eliteJacks",jacks},{"economicUnits",economic},{"selectedTroopAndDeviceCost",cost},
            {"selectedTotalCost",cost+(result.precisionTargetID>0 ? ColdStorageSkillRules::StrikeIceCost*static_cast<int>(1+result.precisionAdditionalTargetIDs.size()) : 0)},{"enemyIceBefore",s.enemyIce},
            {"spentBefore",s.spent},{"lastTicketBefore",s.nextTicket},{"existingTickets",tickets},
            {"selectedActions",Json::array()},{"precisionTargetID",result.precisionTargetID},
            {"precisionAdditionalTargetIDs",result.precisionAdditionalTargetIDs},
            {"forecastPrecisionTargetID",result.forecastPrecisionTargetID},
            {"forecastPrecisionAdditionalTargetIDs",result.forecastPrecisionAdditionalTargetIDs},
            {"forecastPrecisionIce",result.forecastPrecisionIce},{"forecastPrecisionAimStartSeconds",result.forecastPrecisionAimStartSeconds},
            {"features",result.features},{"baselineFeatures",result.baselineFeatures},{"score",result.score},
            {"rawProduction",result.rawProduction},{"productionInputs",result.productionInputs},
            {"preferenceScore",result.preferenceScore},{"opponentScore",result.opponentScore},
            {"fallbackMode",result.fallbackMode},{"counterHoldSeconds",result.counterHoldSeconds},
            {"selectedOriginalForecast",CommanderCombatForecast(result.construction)}};
        for(std::size_t index=0;index<result.actions.size();++index) {
            const auto& a=result.actions[index];const auto& o=search.options.at(a.option);
            Json action={{"actionIndex",index},{"optionIndex",a.option},{"type",o.type},{"row",o.row},
                {"delay",a.delay},{"cost",o.cost},{"device",o.device},{"setting",o.setting}};
            action["birthProfile"]={{"x",o.unit.body.x},{"speed",o.unit.body.speed},
                {"spawnAt",o.unit.body.spawnAt},{"health",o.unit.body.health},{"helm",o.unit.helmHealth},
                {"shield",o.unit.shieldHealth},{"minimumSpeed",o.unit.minimumMoveSpeed},
                {"maximumSpeed",o.unit.maximumMoveSpeed},{"productionRemaining",o.unit.productionRemaining},
                {"nextYield",o.unit.nextYield},{"productionStopHealth",o.unit.productionStopHealth}};
            if(o.device<0) action["typeName"]=GameDataManager::GetInstance().ZombieTypeToEnumName(static_cast<ZombieType>(o.type));
            record["selectedActions"].push_back(std::move(action));
        }
        record["snapshotSearchVersion"]=search.searchVersion;
        record["effectiveWeights"]=result.effectiveWeights;
        record["baselineOpponentAssets"]=result.baselineOpponentAssets;
        record["opponentAssets"]=result.opponentAssets;
        record["opponentWeight"]=search.opponentWeight;
        record["traceEconomyEnabled"]=search.traceEconomy;
        record["traceCombatEnabled"]=search.traceCombat;
        record["playerSun"]=search.playerSun;record["playerIce"]=search.playerIce;
        record["snapshotCounters"]=Json::array();
        for(const auto& c:search.counters) record["snapshotCounters"].push_back({{"source",c.source},
            {"plantID",c.plantID},{"row",c.cellRow},{"column",c.cellColumn},
            {"sunCost",c.sunCost},{"iceCost",c.iceCost},{"recharge",c.recharge},
            {"windup",c.windup},{"sharedSource",c.sharedSource},{"sharedReady",c.sharedReady},
            {"sharedRecharge",c.sharedRecharge},{"stored",c.stored},{"consumesPlant",c.consumesPlant},
            {"flightSeconds",c.flightSeconds},{"shovelAllowed",c.shovelAllowed},
            {"vulnerableSeconds",c.vulnerableSeconds},{"targeted",c.targeted},
            {"ready",c.blast.ready},{"x",c.blast.x},{"reach",c.blast.reach},{"damage",c.blast.damage},
            {"committed",c.blast.committed},{"usesObjectX",c.blast.usesObjectX}});
        record["incomingIce"]=search.incomingIce;record["incomingIceAt"]=search.incomingIceAt;
        record["snapshotExchanges"]=Json::array();
        for(const auto& e:search.exchanges) record["snapshotExchanges"].push_back({{"sunGain",e.sunGain},
            {"iceCost",e.iceCost},{"ready",e.ready},{"recharge",e.recharge},{"cells",e.cells}});
        record["snapshotOrders"]=Json::array();
        for(const auto& o:search.shop) record["snapshotOrders"].push_back({{"sunCost",o.sunCost},
            {"iceGain",o.iceGain},{"delivery",o.delivery}});
        record["snapshotPlants"]=Json::array();
        for(const auto& p:search.plants) record["snapshotPlants"].push_back({{"id",p.id},{"type",p.actualType},
            {"row",p.row},{"column",p.column},{"layer",p.layer},{"health",p.health},
            {"sunPerSecond",p.sunPerSecond},{"productionAt",p.productionAt}});
        record["snapshotUnits"]=Json::array();
        for(const auto& u:search.current) record["snapshotUnits"].push_back({{"id",u.id},{"row",u.body.row},
            {"x",u.body.x},{"speed",u.body.speed},{"spawnAt",u.body.spawnAt},{"health",u.body.health},
            {"helm",u.helmHealth},{"shield",u.shieldHealth},{"economic",u.body.economic},
            {"productionRemaining",u.productionRemaining},{"nextYield",u.nextYield},
            {"productionStopHealth",u.productionStopHealth}});
        // 付款前已有的原数值快照仍由本次任务拥有。只读完整分支避免重载、时龄或阶段改变因果对照。
        // 该诊断会占用墙钟，但不推进世界/不搜索新购物车/不消费正式随机流，不能当强度样本。
        record["originalSnapshotResponses"]=Json::array();
        // 经济单位只导出已选原世界；不为工人额外运行与其无关的小丑消融。
        if(mCommanderCaptureMinimumJacks>0)
        for(int version : {search.searchVersion, 3-search.searchVersion})
        for(int mode=0;mode<=2;++mode) for(bool visible : {false,true})
        for(bool reserve : {false,true}) for(bool paidHold : {false,true}) {
            auto state=search;state.searchVersion=version;state.traceCombat=true;
            state.diagnosticJackTargetMode=mode;state.diagnosticVisibleCounterWitness=visible;
            ColdStorageSearch::ConstructionStats response;
            const auto features=ColdStorageSearch::Evaluate(state,result.actions,&response,0,0,0,reserve,false,-1,paidHold,false,paidHold);
            record["originalSnapshotResponses"].push_back({{"searchVersion",version},{"jackTargetMode",mode},{"visibleWitness",visible},
                {"reserve",reserve},{"paidHold",paidHold},{"rawFeatures",features},{"opponentAssets",response.opponentAssets},
                {"paidCounterCasts",response.paidCounterCasts},{"interferences",response.interferences},
                {"clockAnchors",response.clockAnchors},{"clockRewinds",response.clockRewinds},{"clockRevivals",response.clockRevivals},
                {"jackThrows",response.jackThrows},{"combatTrace",CommanderCombatForecast(response)}});
        }
        const auto name="commander_purchase_"+std::to_string(id);
        const auto path=std::filesystem::path(mOutDir)/"snapshots"/(name+".json");
        auto* scene=dynamic_cast<GameScene*>(SceneManager::GetInstance().GetCurrentScene());
        // 复用正式序列化，失败不调用 TestDriver::Fail，也不终止或重试原付款。
        const bool saved=scene && scene->GetBoard()==&board && GameAPP::GetInstance().mGameInfoSaver.SaveAutoTestLevelSnapshot(
            &board,scene->GetCardSlotManager(),path.string());
        record["snapshotSaved"]=saved;record["snapshotPath"]=(std::filesystem::path("snapshots")/(name+".json")).generic_string();
        mCommanderPlanCaptures.push_back(std::move(record));
        return id;
    } catch(...) {mCommanderCombatFailed=true;return 0;}
}

void TestDriver::CaptureCommanderPlanAfterPayment(const Board& board,std::uint64_t id,bool committed) noexcept {
    if(id==0 || id>mCommanderPlanCaptures.size()) return;
    try {
        auto& record=mCommanderPlanCaptures.at(static_cast<std::size_t>(id-1));
        const auto& s=board.mColdStorage;
        record["status"]=committed ? "incomplete" : "rejected";record["applyCompleted"]=committed;
        record["enemyIceAfter"]=s.enemyIce;record["spentAfter"]=s.spent;record["searchSerialAfter"]=s.searchSerial;
        record["actualPaid"]=record.at("enemyIceBefore").get<int>()-s.enemyIce;
        record["paidTickets"]=Json::array();
        std::vector<const Json*> troopActions;
        for(const auto& action:record.at("selectedActions")) if(action.at("device").get<int>()<0) troopActions.push_back(&action);
        const auto last=record.at("lastTicketBefore").get<std::uint64_t>();
        std::vector<const ColdStorageDeployment*> paid;
        for(const auto& p:s.pending) if(p.ticket>last) paid.push_back(&p);
        bool mappingComplete=paid.size()==troopActions.size();
        if(mappingComplete) for(std::size_t i=0;i<paid.size();++i)
            mappingComplete=mappingComplete && troopActions[i]->at("type")==static_cast<int>(paid[i]->type)
                && troopActions[i]->at("row")==paid[i]->row && troopActions[i]->at("cost")==paid[i]->cost;
        for(std::size_t i=0;i<paid.size();++i) {
            const auto& p=*paid[i];
            Json entry={{"ticket",p.ticket},{"type",static_cast<int>(p.type)},{"row",p.row},
                {"cost",p.cost},{"remaining",p.remaining},{"wave",p.wave}};
            entry["actionIndex"]=mappingComplete ? troopActions[i]->at("actionIndex") : Json(nullptr);
            record["paidTickets"].push_back(std::move(entry));
        }
        record["actionTicketMappingComplete"]=mappingComplete;
        if(committed && mappingComplete && record.at("actualPaid")==record.at("selectedTotalCost")) record["status"]="committed";
        FlushCommanderCombatTrace();
    } catch(...) {mCommanderCombatFailed=true;}
}

void TestDriver::ResetCommanderCombatScope() noexcept {
    if(!mCommanderCombatTrace) return;
    mCommanderCombatSources.clear();mCommanderCombatProjectiles.clear();mCommanderImpactEvents.clear();
    ++mCommanderCombatEpoch;
}

void TestDriver::FlushCommanderCombatTrace() noexcept {
    if(!mCommanderCombatTrace) return;
    try {
        Json report={{"schema",1},{"diagnosticOnly",true},{"clock","actual_game_seconds"},
            {"activeSemantics","entity_IsActive_with_IsDying_separate"},{"runnerSeed",mCommanderDeclaredRunnerSeed},
            {"truncated",mCommanderCombatTruncated},{"recordingFailed",mCommanderCombatFailed},
            {"recordLimit",mCommanderCombatLimit},{"recordCount",mCommanderCombatRecordCount},
            {"events",mCommanderCombatEvents},{"captures",mCommanderPlanCaptures}};
        std::ofstream output(std::filesystem::path(mOutDir)/"commander_combat_trace.json",std::ios::trunc);
        output<<report.dump(2);output.flush();
        if(!output) mCommanderCombatFailed=true;
    } catch(...) {mCommanderCombatFailed=true;}
}
