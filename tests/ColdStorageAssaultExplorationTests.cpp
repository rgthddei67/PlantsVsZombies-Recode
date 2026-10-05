#include "Game/AI/ColdStorageSearch.h"
#include "Game/Zombie/DiggerRules.h"
#include "Game/Plant/IceStorageNutRules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {
/** 合成合法画像，只通过真实生命、攻击和治疗事务形成三方互补，不关联正式兵种ID。 */
ColdStorageSearch::Snapshot AssaultArena()
{
    using namespace ColdStorageSearch;
    Snapshot snapshot;
    snapshot.rows=1;
    snapshot.searchVersion=2;
    snapshot.netEconomy=true;
    snapshot.budget=60;
    snapshot.capacity=3;
    snapshot.houseX=400;
    snapshot.capitalRiskAllowance=0;
    const auto option=[](int type,int cost,float health,float x,float bite) {
        Option result;
        result.type=type;
        result.cost=cost;
        result.unit.body.x=x;
        result.unit.body.speed=20;
        result.unit.body.health=health;
        result.unit.maximumBody=health;
        result.unit.body.purchaseCost=static_cast<float>(cost);
        result.unit.biteDps=bite;
        return result;
    };
    auto front=option(71001,30,240,600,0);
    auto attacker=option(71002,20,40,610,50);
    auto healer=option(71003,10,80,620,0);
    healer.unit.healer.present=true;
    // 保留正常5秒初始冷却和1秒治疗前摇，不为夹具预支一次回血。
    // 合法且单独可买的经济画像占满钱包；它在第一轮生产前死亡，不能用经济盈利替代攻城证据。
    auto producer=option(71004,60,20,610,0);
    producer.unit.body.economic=true;
    producer.unit.productionStopHealth=0;
    snapshot.options={front,attacker,healer,producer};
    Plant wall;
    wall.id=100;
    wall.x=500;
    wall.health=1000;
    wall.dps=30;
    wall.reward=1;
    snapshot.plants={wall};
    return snapshot;
}

/** 失败时留下真实完整推演数据，便于区分夹具不成立和自由搜索漏掉已有解。 */
void Check(bool condition,const char* message)
{
    if(!condition) {
        std::cerr<<"FAILED assault exploration: "<<message<<'\n';
        std::exit(1);
    }
}
}

/** 三类攻城中间态、合法经济分支竞争及改名反事实；不限定最后购买顺序或正式编队。 */
void RunColdStorageAssaultExplorationTests()
{
    using namespace ColdStorageSearch;
    auto snapshot=AssaultArena();
    const Weights values{0,0,500,0,1,-1,0,0};
    Check(ValidWeights(values),"the counterfactual uses legal search weights rather than an input rejected before exploration");
    for(const auto& pair: {std::vector<Action>{{0,0},{1,0}},
        std::vector<Action>{{0,0},{2,0}},std::vector<Action>{{1,0},{2,0}}}) {
        const auto result=EvaluateCandidate(snapshot,values,pair);
        std::cout<<"assault pair="<<pair[0].option<<','<<pair[1].option
            <<" breach="<<result.features[2]<<" cost="<<result.features[5]
            <<" score="<<result.score<<" gate="
            <<ShouldConserveCapital(result,snapshot.budget,0,snapshot.capitalRiskAllowance)<<" features=";
        for(float feature:result.features) std::cout<<feature<<',';
        std::cout<<'\n';
        Check(result.features[2]==0 && result.score<0,
            "every two-role intermediate really loses before house breach");
        Check(ShouldConserveCapital(result,snapshot.budget,0,snapshot.capitalRiskAllowance),
            "unpaid attack intermediates retain no permission to spend the risk budget");
    }
    const auto complete=EvaluateCandidate(snapshot,values,{{0,0},{1,0},{2,0}});
    std::cout<<"assault complete breach="<<complete.features[2]<<" cost="<<complete.features[5]
        <<" heals="<<complete.construction.healerCasts<<" score="<<complete.score<<'\n';
    Check(complete.features[2]>0 && complete.construction.healerCasts>1
        && !ShouldConserveCapital(complete,snapshot.budget,0,snapshot.capitalRiskAllowance),
        "normal repeated healing makes the complete three-role proposal breach and pass final capital checks");
    Check(EvaluateCandidate(snapshot,values,{{0,0},{1,0}}).features[1]>0,
        "the rejected unfinished attack has actual plant damage for a generic progress frontier");
    // 枚举容量内可支付的零延迟两角色反事实，排除重复前排/攻击者已能解决问题的弱夹具。
    for(int first=0;first<4;++first) for(int second=-1;second<4;++second)
        for(int third=-1;third<4;++third) {
            if(second<0 && third>=0) continue;
            std::vector<Action> plan{{first,0}};
            if(second>=0) plan.push_back({second,0});
            if(third>=0) plan.push_back({third,0});
            int cost=0;
            std::set<int> roles;
            for(const auto& action:plan) { cost+=snapshot.options[action.option].cost; roles.insert(action.option); }
            if(cost>snapshot.budget || roles.size()>2) continue;
            Check(EvaluateCandidate(snapshot,values,plan).features[2]==0,
                "affordable duplicate members of at most two roles cannot replace the missing third ability");
        }
    int discoveries=0,attackExpansions=0;
    for(unsigned seed=1;seed<=8;++seed) {
        const auto found=Search(snapshot,values,seed);
        std::cout<<"assault search seed="<<seed<<" breach="<<found.features[2]
            <<" cost="<<found.features[5]<<" refinements="<<found.refinementEvaluated
            <<" attackExpansions="<<found.assaultEvaluated<<'\n';
        if(found.features[2]>0) ++discoveries;
        attackExpansions+=found.assaultEvaluated;
        Check(found.features[5]<=snapshot.budget && found.actions.size()<=static_cast<size_t>(snapshot.capacity)
            && found.combinationEvaluated<=80,"attack exploration shares the real money, slots and candidate quota");
    }
    Check(discoveries>0,"free search can deepen attack intermediates while a legal income type exists");
    Check(attackExpansions>0,"the attack intermediate frontier receives actual evaluations rather than only storing rejected plans");
    auto renamed=snapshot;
    for(size_t index=0;index<renamed.options.size();++index)
        renamed.options[index].type=93000-static_cast<int>(index)*137;
    int renamedDiscoveries=0;
    for(unsigned seed=1;seed<=8;++seed)
        if(Search(renamed,values,seed).features[2]>0) ++renamedDiscoveries;
    Check(renamedDiscoveries>0,"attack cooperation remains discoverable after changing every arbitrary unit id");
    snapshot.options[2].unit.healer.present=false;
    for(unsigned seed=1;seed<=4;++seed) {
        const auto hopeless=Search(snapshot,values,seed);
        Check(hopeless.features[2]==0,
            "preserving attack seeds cannot invent a breakthrough after its actual support ability is removed");
    }
    auto underfunded=AssaultArena();
    underfunded.budget=50;
    Check(Search(underfunded,values,3).features[2]==0,
        "an exploratory attack frontier cannot buy the complete breakthrough with insufficient real funds");
    std::cout<<"Assault exploration counterfactuals passed\n";
}

namespace {
/** 地下来源采用真实阶段时序；前墙极强但后排暴露，房屋线刻意在出土线右侧检验假突破。 */
ColdStorageSearch::Snapshot DiggerArena()
{
    using namespace ColdStorageSearch;
    Snapshot snapshot;
    snapshot.rows=1;
    snapshot.houseX=250;
    Unit miner;
    miner.id=1;
    miner.body.x=1000;
    miner.body.speed=67;
    miner.body.health=270;
    miner.maximumBody=270;
    miner.body.purchaseCost=12;
    miner.digger.present=true;
    miner.digger.surfaceX=190;
    miner.magneticLayer=3;
    snapshot.current={miner};
    Plant front;
    front.id=10;
    front.x=600;
    front.health=10000;
    front.dps=3000;
    front.reward=50;
    Plant rear;
    rear.id=11;
    rear.x=270;
    rear.health=100;
    rear.reward=20;
    snapshot.plants={front,rear};
    return snapshot;
}

/** 储冰坚果手动修复面对远程投篮；所有车静止且远离接触距离，不借近身压力触发。 */
ColdStorageSearch::Snapshot RemoteRepairArena()
{
    using namespace ColdStorageSearch;
    Snapshot snapshot;
    snapshot.houseX=-10000;
    snapshot.playerIce=IceStorageNutRules::kRepairIce;
    Plant nut;
    nut.id=100;
    nut.x=500;
    nut.health=IceStorageNutRules::kHealth-IceStorageNutRules::kRepairHealth;
    nut.reward=100;
    nut.assetValue=100;
    nut.repairMaximum=IceStorageNutRules::kHealth;
    nut.repairAmount=IceStorageNutRules::kRepairHealth;
    nut.repairCost=IceStorageNutRules::kRepairIce;
    nut.repairRecharge=IceStorageNutRules::kRepairCooldown;
    nut.repairAutomatic=false;
    snapshot.plants={nut};
    Unit car;
    car.id=1;
    car.body.x=700;
    car.body.health=850;
    car.body.speed=0;
    car.biteDps=0;
    car.catapult.present=true;
    car.catapult.ammunition=2;
    car.catapult.release=.5f;
    car.catapult.duration=1;
    car.catapult.damage=75;
    snapshot.current={car};
    return snapshot;
}
}

/** 矿工完整进场和通用自由搜索契约；另验证无近身目标时的手动修复资源边界。 */
void RunColdStorageDiggerIntegrationTests()
{
    using namespace ColdStorageSearch;
    using Phase=DiggerRules::Phase;
    {
        auto snapshot=DiggerArena();
        const auto behind=Evaluate(snapshot,{});
        Check(behind[0]==20 && behind[2]==0,
            "an underground miner bypasses lethal front fire and eats the rear without inventing a house breach");
        auto exposed=snapshot;
        exposed.current[0].digger.present=false;
        Check(Evaluate(exposed,{})[0]==0,"an ordinary exposed body cannot borrow underground projectile immunity");
        auto held=snapshot;
        held.current[0].body.x=190;
        held.current[0].digger.phase=Phase::STUNNED;
        held.current[0].digger.remaining=1000;
        const auto stunned=Evaluate(held,{});
        Check(stunned[0]==0 && stunned[2]==0,"the emergence stun cannot eat a nearby rear plant or trigger game over");
        held.current[0].digger.remaining=DiggerRules::StunnedSeconds;
        Check(Evaluate(held,{})[0]==20,"a completed emergence stun resumes rightward rear attacks");
        DiggerRules::Forecast phase;
        phase.present=true;
        phase.surfaceX=190;
        float x=190;
        Check(phase.Advance(DiggerRules::RiseSeconds+DiggerRules::StunnedSeconds-.1f,x)==0
            && phase.phase==Phase::STUNNED && !phase.CanEat(),
            "rise and stun consume their actual action durations before ground eating becomes available");
        Check(phase.Advance(.2f,x)>0 && phase.CanEat() && phase.IsMovingRight(),
            "only action time after both phase edges is available to rightward ground movement");
        Check(snapshot.current[0].digger.phase==Phase::TUNNELING && snapshot.current[0].body.x==1000,
            "forecasting cannot advance the actual captured miner phase or position");
    }
    {
        Snapshot snapshot;
        snapshot.rows=1;
        snapshot.houseX=-10000;
        Unit miner=DiggerArena().current[0];
        miner.body.x=190;
        miner.body.canBeChilled=false;
        miner.digger.phase=Phase::STUNNED;
        miner.digger.remaining=DiggerRules::StunnedSeconds;
        snapshot.current={miner};
        Plant rear;
        rear.id=40;
        rear.x=600;
        rear.health=100;
        rear.reward=20;
        Plant cold;
        cold.id=41;
        cold.x=190;
        cold.health=10000;
        cold.edible=false;
        cold.around=true;
        cold.range=1000;
        cold.dps=1;
        cold.slowRate=1;
        cold.slowDuration=10;
        snapshot.plants={rear,cold};
        const auto chilled=Evaluate(snapshot,{});
        snapshot.plants[1].slowRate=0;
        const auto ordinary=Evaluate(snapshot,{});
        Check(chilled[0]==0 && ordinary[0]==20,
            "surface miner obeys cold fire after stun despite its underground birth canBeChilled=false portrait");
        Check(chilled[3]>0 && ordinary[3]>0,
            "the cold counterfactual delays a surviving miner rather than killing it with the low-damage source");
    }
    {
        auto snapshot=DiggerArena();
        Counter ash;
        ash.blast.committed=true;
        ash.blast.damage=1;
        ash.blast.x=1000;
        ash.blast.reach.fill(-1);
        ash.blast.reach[0]=100;
        snapshot.counters={ash};
        const auto cleared=Evaluate(snapshot,{});
        Check(cleared[0]==0 && cleared[2]==0 && cleared[6]==12,
            "any positive formal ash kills a covered underground miner instead of merely chipping its body");
        snapshot.counters[0].blast.requiresGroundTarget=true;
        const auto belowGround=Evaluate(snapshot,{});
        Check(belowGround[0]==20 && belowGround[2]==0 && belowGround[6]==0,
            "potato or squash ground-target transactions cannot apply their damage to an underground miner");
    }
    {
        auto snapshot=DiggerArena();
        snapshot.houseX=160;
        snapshot.current[0].body.x=600;
        Plant magnet;
        magnet.id=20;
        magnet.x=500;
        magnet.health=10000;
        magnet.edible=false;
        magnet.magnetRadius=1000;
        magnet.magnetRecharge=1000;
        snapshot.plants={magnet};
        ConstructionStats stats;
        Check(Evaluate(snapshot,{},&stats)[2]>0 && stats.magneticExtractions==1,
            "underground magnetic pickaxe removal pauses and rises before switching to leftward house danger");
        snapshot.plants[0].magnetRadius=0;
        Check(Evaluate(snapshot,{})[2]==0,
            "a miner retaining its pickaxe resurfaces to the right and never borrows the no-pickaxe breach route");
    }
    {
        auto snapshot=DiggerArena();
        snapshot.current[0].body.x=190;
        snapshot.current[0].digger.phase=Phase::STUNNED;
        snapshot.current[0].digger.remaining=1000;
        TemporalAnchor rewind;
        rewind.at=1;
        TemporalTarget target;
        target.unit=0;
        target.saved=DiggerArena().current[0];
        target.restoreAbility=true;
        rewind.targets={target};
        snapshot.temporalAnchors={rewind};
        ConstructionStats stats;
        Check(Evaluate(snapshot,{},&stats)[0]==0 && stats.clockRewinds==1,
            "a living temporal rewind restores ordinary layers without reversing an unsupported miner ability phase");
        snapshot.current[0].body.health=0;
        snapshot.temporalAnchors[0].targets[0].saved.digger.phase=Phase::STUNNED;
        snapshot.temporalAnchors[0].targets[0].saved.digger.remaining=1000;
        Check(Evaluate(snapshot,{},&stats)[0]==20 && stats.clockRevivals==1,
            "a dead miner revived as a new entity starts the real underground birth stage rather than a saved stun");
    }
    {
        auto snapshot=DiggerArena();
        Option miner;
        miner.type=72001;
        miner.cost=12;
        miner.unit=snapshot.current[0];
        miner.unit.id=0;
        auto ordinary=miner;
        ordinary.type=72002;
        ordinary.unit.digger.present=false;
        snapshot.current.clear();
        snapshot.options={ordinary,miner};
        snapshot.budget=12;
        snapshot.capacity=1;
        snapshot.netEconomy=true;
        const Weights gain{1,0,0,0,1,-1,0,0};
        Check(ValidWeights(gain),"digger discovery uses legal bounded weights");
        const auto chosen=Search(snapshot,gain,19);
        Check(chosen.features[0]>=20 && chosen.features[5]<=12,
            "free search can discover a payable rear attack through the actual tunneling ability");
        for(auto& option:snapshot.options) option.type=94000-option.type*3;
        Check(Search(snapshot,gain,19).features[0]>=20,
            "rear attack discovery uses the ability portrait rather than a formal miner type id");
        snapshot.options[1].unit.digger.present=false;
        Check(Search(snapshot,gain,19).actions.empty(),
            "removing tunneling removes the reason to buy the same exposed body");
    }
    {
        auto snapshot=RemoteRepairArena();
        ConstructionStats funded,empty;
        Evaluate(snapshot,{},&funded);
        auto broke=snapshot;
        broke.playerIce=0;
        Evaluate(broke,{},&empty);
        Check(funded.plantRepairs==1 && funded.plantRepairIce==IceStorageNutRules::kRepairIce
            && funded.catapultShots==2 && funded.opponentAssets>empty.opponentAssets,
            "manual ice-storage repair reacts to distant lob attacks and spends the real player wallet once");
        Check(empty.plantRepairs==0,"a remote threat cannot repair a nut without enough player ice");
        broke.playerIce=IceStorageNutRules::kRepairIce-1;
        Evaluate(broke,{},&empty);
        Check(empty.plantRepairs==0,"nineteen actual ice cannot pay a twenty-ice remote repair");
        auto cooling=snapshot;
        cooling.plants[0].repairRemaining=1000;
        Evaluate(cooling,{},&empty);
        Check(empty.plantRepairs==0,"remote manual repair still respects its existing cooldown");
        auto disabled=snapshot;
        disabled.plants[0].repairBlockedUntil=1000;
        Evaluate(disabled,{},&empty);
        Check(empty.plantRepairs==0,"disabled manual repair cannot bypass its shutdown timer for ranged pressure");
        auto unarmed=snapshot;
        unarmed.current[0].catapult.ammunition=0;
        Evaluate(unarmed,{},&empty);
        Check(empty.plantRepairs==0,"an empty stationary catapult creates no ranged or contact repair threat");
        auto dead=snapshot;
        dead.current[0].body.health=0;
        Evaluate(dead,{},&empty);
        Check(empty.plantRepairs==0,"a dead distant catapult cannot keep authorizing new manual repair");
    }
    std::cout<<"Digger integration and remote manual repair counterfactuals passed\n";
}

namespace {
/** 同一真实威胁与可购画像，只改变己方现金；单槽迫使比较盈利生产和实际破坏的机会成本。 */
ColdStorageSearch::Snapshot CapitalArena(int budget)
{
    using namespace ColdStorageSearch;
    Snapshot snapshot;
    snapshot.rows=2;
    snapshot.netEconomy=true;
    snapshot.budget=budget;
    snapshot.capacity=1;
    snapshot.houseX=-10000;
    Option producer;
    producer.type=81001;
    producer.row=1;
    producer.cost=20;
    producer.unit.body.row=1;
    producer.unit.body.x=900;
    producer.unit.body.health=500;
    producer.unit.body.purchaseCost=20;
    producer.unit.body.economic=true;
    producer.unit.biteDps=0;
    Option attacker;
    attacker.type=81002;
    attacker.cost=100;
    attacker.unit.body.x=700;
    attacker.unit.body.health=850;
    attacker.unit.body.purchaseCost=100;
    attacker.unit.biteDps=0;
    attacker.unit.catapult.present=true;
    attacker.unit.catapult.ammunition=2;
    attacker.unit.catapult.release=.5f;
    attacker.unit.catapult.duration=1;
    attacker.unit.catapult.damage=75;
    snapshot.options={producer,attacker};
    Plant target;
    target.id=50;
    target.x=300;
    target.health=150;
    target.reward=20;
    snapshot.plants={target};
    return snapshot;
}
}

/** 现金边际效用只改变同窗评分，不改变真实钱、产能、退款或自由搜索的可支付边界。 */
void RunColdStorageCapitalUtilityTests()
{
    using namespace ColdStorageSearch;
    const auto near=[](float left,float right) {return std::abs(left-right)<.0001f;};
    auto poor=CapitalArena(200), rich=CapitalArena(10000);
    Check(near(CapitalUtilityScale(poor),1) && near(CapitalUtilityScale(rich),.02f),
        "cash covering two complete legal highest-cost teams has decreasing utility only beyond that coverage");
    auto reserve=rich;
    reserve.recoveryReserve=50;
    Check(near(CapitalUtilityScale(reserve),.025f),
        "the real recovery reserve joins full-team coverage before wealth utility decreases");
    auto excessive=rich;
    excessive.budget=1000000;
    Check(near(CapitalUtilityScale(excessive),.01f),"wealth cash utility retains its bounded positive minimum");
    auto legacy=rich;
    legacy.netEconomy=false;
    Check(near(CapitalUtilityScale(legacy),1),"legacy policies preserve their original cash utility");
    auto staged=rich;
    staged.capacity=24;
    staged.searchVersion=1;
    const float smallStage=CapitalUtilityScale(staged);
    staged.searchVersion=2;
    Check(near(smallStage,CapitalUtilityScale(staged)) && smallStage>.01f && smallStage<1,
        "small and complete search stages value the same wealthy wallet by stable full deployable capacity");
    {
        auto devices=rich;
        Option device;
        device.type=81003;
        device.cost=5000;
        device.device=0;
        devices.options.push_back(device);
        auto unavailable=rich;
        auto unaffordable=rich.options[1];
        unaffordable.type=81004;
        unaffordable.cost=20000;
        unavailable.options.push_back(unaffordable);
        Check(near(CapitalUtilityScale(devices),CapitalUtilityScale(rich))
            && near(CapitalUtilityScale(unavailable),CapitalUtilityScale(rich)),
            "devices and unaffordable troops do not inflate a currently legal complete troop-team cash target");
        auto future=rich;
        future.incomingIce=1000000;
        future.supplyIce=1000000;
        future.supplyInterval=1;
        future.current={poor.options[0].unit};
        future.current[0].body.purchaseCost=1000000;
        Check(near(CapitalUtilityScale(future),CapitalUtilityScale(rich)),
            "future supply, anticipated production and paid assets cannot masquerade as actual wallet cash");
    }
    {
        const Weights base{3,7,120,.4f,2,-5,8,.5f};
        Check(ValidWeights(base),"cash utility weights use the legal bounded scoring domain");
        const auto normal=AccountForIce(base), reduced=AccountForIce(base,.02f);
        Check(near(normal[0],5) && near(normal[3],.8f) && near(normal[4],2) && near(normal[5],-2),
            "default cash conversion preserves the preexisting full-value economic contract");
        Check(near(reduced[0],3.04f) && near(reduced[3],.016f)
            && near(reduced[4],.04f) && near(reduced[5],-.04f),
            "wealth reduces only the cash part of kills and all matching cash income, residual investment and purchase costs");
        Check(reduced[1]==normal[1] && reduced[2]==normal[2] && reduced[6]==normal[6]
            && reduced[7]==normal[7],"cash wealth cannot reduce damage, breach, ash risk or movement values");
        const Weights edge{490,0,0,1,100,0,0,0};
        Check(ValidWeights(edge),"large tactical and cash components are individually legal input weights");
        const auto combined=AccountForIce(edge,.5f);
        Check(near(combined[0],540) && near(combined[0]-edge[0],50)
            && near(combined[4],50) && near(combined[5],-50) && near(combined[3],50),
            "combining two legal bounded components cannot clip only kill cash and break equal currency valuation");
    }
    {
        const auto poorFactory=Evaluate(poor,{{0,0}}), richFactory=Evaluate(rich,{{0,0}});
        const auto poorAttack=Evaluate(poor,{{1,0}}), richAttack=Evaluate(rich,{{1,0}});
        Check(poorFactory==richFactory && poorFactory[4]>poorFactory[5],
            "wealth does not create production or alter the real profitable factory transaction");
        Check(poorAttack==richAttack && poorAttack[0]==20 && poorAttack[5]==100,
            "the real attack destroys the same plant and returns the same twenty ice at both wallet levels");
        const Weights values{1,3,120,0,1,-1,0,0};
        Check(ValidWeights(values),"the economic versus assault counterfactual uses valid tactical weights");
        for(unsigned seed=1;seed<=4;++seed) {
            const auto economic=Search(poor,values,seed), destructive=Search(rich,values,seed);
            std::cout<<"capital seed="<<seed<<" poor income="<<economic.features[4]
                <<" kill="<<economic.features[0]<<" cashWeight="<<economic.effectiveWeights[4]
                <<" rich income="<<destructive.features[4]<<" kill="<<destructive.features[0]
                <<" cashWeight="<<destructive.effectiveWeights[4]<<'\n';
            Check(economic.features[4]>economic.features[5] && economic.features[0]==0,
                "a scarce treasury prefers an actually profitable factory over the competing expensive attack");
            Check(destructive.features[0]==20 && destructive.features[4]==0 && destructive.features[5]<=rich.budget,
                "an already wealthy treasury can prefer actual destruction instead of indefinitely maximizing more cash");
        }
        Check(poor.budget==200 && rich.budget==10000 && poor.options[1].unit.catapult.ammunition==2,
            "utility calculation and exploration cannot consume the real captured wallet or ammunition");
        auto renamed=rich;
        for(auto& option:renamed.options) option.type=99000-option.type;
        Check(Search(renamed,values,2).features[0]==20,
            "wealth-aware assault choice follows real candidate outcomes rather than formal zombie identities");
        // 同路远程前排先承受直射并摧毁火力，工人稍后出生赚钱；只验证完整案，不强制搜索购买这组角色。
        auto mixed=rich;
        mixed.capacity=2;
        mixed.capitalRiskAllowance=0;
        mixed.options[0].row=mixed.options[0].unit.body.row=0;
        mixed.options[0].unit.body.x=740;
        mixed.plants[0].dps=15;
        const auto rearAlone=Evaluate(mixed,{{0,2}});
        const auto advanceAndIncome=EvaluateCandidate(mixed,values,{{1,0},{0,2}});
        Check(advanceAndIncome.features[0]==20 && advanceAndIncome.features[4]>rearAlone[4]
            && advanceAndIncome.features[0]+advanceAndIncome.features[4]>advanceAndIncome.features[5],
            "a complete same-lane attack and rear worker can destroy incoming fire and actually repay the shared purchase");
        Check(advanceAndIncome.score>0 && !ShouldConserveCapital(advanceAndIncome,mixed.budget,0,0),
            "wealth utility and exhausted risk allowance do not reject a real cash-profitable combined attack");
    }
    std::cout<<"Capital utility and opportunity-cost counterfactuals passed\n";
}
