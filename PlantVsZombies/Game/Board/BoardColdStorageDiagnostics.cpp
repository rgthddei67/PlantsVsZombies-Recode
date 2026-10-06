#include "Game/Board/Board.h"
#include "Game/AI/ColdStoragePolicy.h"
#include "Game/AI/ColdStorageSearch.h"
#include "Game/Plant/GameDataManager.h"
#include "GameApp.h"
#include "DeltaTime.h"
#include "Logger.h"
#include <exception>
#include <nlohmann/json.hpp>

/** 主线程冻结现局与搜索诊断；独立快照只作取证，失败不能影响下一次合法决策。 */
void Board::CaptureColdStorageStall(const ColdStorageSearch::Snapshot& search,
    const ColdStorageSearch::Result& result, std::uint32_t seed)
{
    if (mColdStorage.stallDiagnosticSaved) return;
    // 先消耗本 Board 的一次捕获机会，磁盘失败也不每轮重复阻塞主线程。
    mColdStorage.stallDiagnosticSaved = true;
    try {
        using Json = nlohmann::json;
        const auto& app = GameAPP::GetInstance();
        Json record = {{"schema",1},{"reason","long_empty_field"},{"seed",seed},
            {"level",mLevel},{"levelName",mLevelName},{"wave",mCurrentWave},
            {"boardState",static_cast<int>(mBoardState)},{"background",static_cast<int>(mBackGround)},
            {"coldStorage",SaveColdStorage()}};
        record["runtime"] = {{"autoTest",GameAPP::mAutoTestMode},{"developMode",GameAPP::mDevelopMode},
            {"devSpawnPaused",GameAPP::mDevSpawnPaused},{"devFreePlant",GameAPP::mDevFreePlant},
            {"devNoCooldown",GameAPP::mDevNoCooldown},{"enableMonteCarloAI",app.mEnableMonteCarloAI},
            {"advancedPause",app.mAdvancedPauseEnabled},{"autoCollect",app.mAutoCollected},
            {"paused",DeltaTime::IsPaused()},{"timeScale",DeltaTime::GetTimeScale()}};
        record["planning"] = {{"started",mColdStorage.planningStarted},
            {"applied",mColdStorage.planningApplied},{"discarded",mColdStorage.planningDiscarded},
            {"discardReasons",mColdStorage.planningDiscardReasons},{"lastDiscardMask",mColdStorage.planningLastDiscardMask},
            {"lastAgeMs",mColdStorage.planningLastAgeMs},{"budgetMs",mColdStorage.planningBudgetMs},
            {"workerMs",mColdStorage.planningWorkerMs},{"mainMaxMs",mColdStorage.planningMainMaxMs},
            {"workerThreads",mColdStorage.planningWorkerThreads},{"parallelPlans",mColdStorage.planningParallelPlans},
            {"timeLimited",mColdStorage.planningTimeLimited}};
        record["snapshot"] = {{"budget",search.budget},{"capacity",search.capacity},
            {"deploymentCapital",search.deploymentCapital},{"deploymentOccupied",search.deploymentOccupied},
            {"capitalRiskAllowance",search.capitalRiskAllowance},{"recoveryReserve",search.recoveryReserve},
            {"allowWait",search.allowWait},{"searchVersion",search.searchVersion},
            {"netEconomy",search.netEconomy},{"anticipateEconomy",search.anticipateEconomy},
            {"experiencedFormations",search.experiencedFormations},{"timeLimitedSearch",search.timeLimitedSearch},
            {"resumePortfolio",search.resumePortfolio},{"noProgressSeconds",search.noProgressSeconds},
            {"playerSun",search.playerSun},{"playerIce",search.playerIce},
            {"incomingIce",search.incomingIce},{"incomingIceAt",search.incomingIceAt},
            {"precisionReady",search.precisionReady},{"pendingPrecisionID",search.pendingPrecisionID},
            {"precisionUnlockAfterPurchase",search.precisionUnlockAfterPurchase},
            {"rows",search.rows},{"columns",search.columns},{"gridLeft",search.gridLeft},
            {"cellWidth",search.cellWidth},{"cellHeight",search.cellHeight},{"context",search.context}};
        record["result"] = {{"features",result.features},{"baselineFeatures",result.baselineFeatures},
			{"fallbackMode",result.fallbackMode},
            {"effectiveWeights",result.effectiveWeights},{"stateInputs",result.stateInputs},
            {"score",result.score},{"rawProduction",result.rawProduction},{"productionInputs",result.productionInputs},
            {"baselineBreachSeconds",result.baselineBreachSeconds},{"breachSeconds",result.construction.breachSeconds},
            {"opponentAssets",result.opponentAssets},{"baselineOpponentAssets",result.baselineOpponentAssets},
            {"opponentScore",result.opponentScore},{"preferenceScore",result.preferenceScore},
            {"rawPreferenceScore",result.rawPreferenceScore},{"evaluated",result.evaluated},
            {"capitalRejected",result.capitalRejected},{"largestPlan",result.largestPlan},
            {"widestComposition",result.widestComposition},{"regrouping",result.regrouping},
            {"timeLimited",result.timeLimited},{"expandedForecast",result.expandedForecast},
            {"routeEvaluated",result.routeEvaluated},{"combinationEvaluated",result.combinationEvaluated},
            {"incomeEvaluated",result.incomeEvaluated},{"assaultEvaluated",result.assaultEvaluated},
            {"refinementEvaluated",result.refinementEvaluated},{"pruningEvaluated",result.pruningEvaluated},
            {"proposalEvaluated",result.proposalEvaluated},{"experiencedEvaluated",result.experiencedEvaluated},
            {"experiencedSelected",result.experiencedSelected},{"precisionEvaluated",result.precisionEvaluated},
            {"precisionTargetID",result.precisionTargetID},{"precisionAdditionalTargetIDs",result.precisionAdditionalTargetIDs},
            {"forecastPrecisionTargetID",result.forecastPrecisionTargetID},
            {"forecastPrecisionIce",result.forecastPrecisionIce},{"counterHoldSeconds",result.counterHoldSeconds},
            {"paidCounterCasts",result.construction.paidCounterCasts},{"predictedPlantings",result.construction.planted},
            {"counterSpaceReserved",result.construction.counterSpaceReserved},
            {"paidDefensesRetained",result.construction.paidDefensesRetained},
            {"planternResponseGear",result.construction.planternResponseGear}};
        record["snapshot"]["fallbackProbeBudget"]=search.fallbackProbeBudget;
        record["snapshot"]["fallbackAllIn"]=search.fallbackAllIn;
        record["candidates"] = Json::array();
        const auto& data = GameDataManager::GetInstance();
        for (const auto& candidate : result.candidates) record["candidates"].push_back({
            {"type",candidate.type},{"typeName",data.ZombieTypeToEnumName(static_cast<ZombieType>(candidate.type))},
            {"row",candidate.row},{"evaluated",candidate.evaluated},{"standalone",candidate.standalone},
            {"allowed",candidate.allowed},{"regroupRejected",candidate.regroupRejected},
            {"capitalRejected",candidate.capitalRejected},{"bestDenial",candidate.bestDenial},
            {"bestScore",candidate.bestScore},{"bestBreach",candidate.bestBreach},{"bestCash",candidate.bestCash},
            {"bestProduction",candidate.bestProduction},{"bestCost",candidate.bestCost},
            {"bestBlastLoss",candidate.bestBlastLoss},{"bestAllowedScore",candidate.bestAllowedScore},
            {"bestAllowedBreach",candidate.bestAllowedBreach}});
        record["options"] = Json::array();
        for (const auto& option : search.options) record["options"].push_back({
            {"type",option.type},{"row",option.row},{"cost",option.cost},
            {"device",option.device},{"setting",option.setting},{"preference",option.preference},
            {"health",option.unit.body.health},{"speed",option.unit.body.speed},
            {"economic",option.unit.body.economic},{"shieldHealth",option.unit.shieldHealth},
            {"helmHealth",option.unit.helmHealth}});
        record["actions"] = Json::array();
        for (const auto& action : result.actions) record["actions"].push_back({
            {"option",action.option},{"delay",action.delay}});
        record["snapshotPlants"] = Json::array();
        for (const auto& plant : search.plants) record["snapshotPlants"].push_back({
            {"id",plant.id},{"row",plant.row},{"column",plant.column},{"layer",plant.layer},
            {"x",plant.x},{"health",plant.health},{"dps",plant.dps},{"range",plant.range},
            {"rowRadius",plant.rowRadius},{"multiTarget",plant.multiTarget},{"echo",plant.echo},
            {"hitDamage",plant.hitDamage},{"shutdownUntil",plant.shutdownUntil},{"sunPerSecond",plant.sunPerSecond}});
        record["snapshotUnits"] = Json::array();
        for (const auto& unit : search.current) record["snapshotUnits"].push_back({
            {"id",unit.id},{"row",unit.body.row},{"x",unit.body.x},{"health",unit.body.health},
            {"speed",unit.body.speed},{"spawnAt",unit.body.spawnAt},{"purchaseCost",unit.body.purchaseCost},
            {"economic",unit.body.economic},{"productionRemaining",unit.productionRemaining}});
        record["counters"] = Json::array();
        for (const auto& counter : search.counters) record["counters"].push_back({
            {"source",counter.source},{"plantID",counter.plantID},{"ready",counter.blast.ready},
            {"committed",counter.blast.committed},{"x",counter.blast.x},{"damage",counter.blast.damage},
            {"reach",counter.blast.reach},{"sunCost",counter.sunCost},{"iceCost",counter.iceCost},
            {"row",counter.cellRow},{"column",counter.cellColumn},{"recharge",counter.recharge},
            {"windup",counter.windup},{"stored",counter.stored},{"shovelAllowed",counter.shovelAllowed}});
        // 资源副本说明磁盘版本；实际加载/实验覆盖另保存数值，避免把实验误认成发布策略。
        const auto* weights = ColdStoragePolicy::Get();
        record["activePolicy"] = {{"enabled",weights!=nullptr},{"searchVersion",ColdStoragePolicy::SearchVersion()},
            {"netEconomy",ColdStoragePolicy::NetEconomy()},{"anticipateBuilding",ColdStoragePolicy::AnticipateBuilding()},
            {"anticipateEconomy",ColdStoragePolicy::AnticipateEconomy()},{"allUnits",ColdStoragePolicy::AllUnits()},
            {"opponentWeight",ColdStoragePolicy::OpponentWeight()}};
        record["activePolicy"]["weights"] = weights ? Json(*weights) : Json(nullptr);
        if (const auto* model=search.stateModel) record["activePolicy"]["stateModel"] = model->coefficients;
        record["activePolicy"]["productionCalibration"] = Json::array();
        if (const auto* model=search.productionCalibration) for (const auto& node : model->nodes)
            record["activePolicy"]["productionCalibration"].push_back({{"feature",node.feature},
                {"left",node.left},{"right",node.right},{"threshold",node.threshold},{"value",node.value}});
        if (!GameAPP::GetInstance().mGameInfoSaver.SaveCommanderStallSnapshot(this,mCardSlotManager,
            record,mColdStorage.stallDiagnosticPath)) {
            LOG_ERROR("ColdStorage") << "停滞诊断未完整保存，游戏继续；目录: " << mColdStorage.stallDiagnosticPath;
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("ColdStorage") << "停滞诊断捕获失败，游戏继续: " << e.what();
    }
    catch (...) {
        LOG_ERROR("ColdStorage") << "停滞诊断捕获发生未知异常，游戏继续";
    }
}
