#include "Game/AI/ColdStorageSearch.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include "Game/AI/ColdStoragePlanner.h"
#include <chrono>
#include <thread>
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
    {
        auto alreadyWon=AssaultArena();
        alreadyWon.budget=1000; alreadyWon.precisionReady=true;
        alreadyWon.current={alreadyWon.options[0].unit};
        alreadyWon.current[0].body.x=alreadyWon.houseX-1;
        alreadyWon.current[0].body.speed=0;
        const auto result=Search(alreadyWon,values,3);
        Check(result.features[2]==1 && result.baselineBreachSeconds==0
            && result.actions.empty() && result.precisionTargetID==0,
            "reused precision baseline retains the actual immediate house time and cannot invent a later paid improvement");
    }
    // 未付款提案只携带类型/路线/延迟；变换选项下标仍能正确重算，局势改变不得复用胜利。
    auto continued=AssaultArena();
    Proposal known;
    for(int i:{0,1,2}) known.push_back({continued.options[i].type,0,-1,0,0});
    continued.proposals={known};
    std::reverse(continued.options.begin(),continued.options.end());
    const auto restored=Search(continued,values,6);
    Check(restored.proposalEvaluated>0 && restored.features[2]>0,
        "an unpaid proposal is remapped by legal identity after option indices change and fully rescored");
    continued.options[0].unit.healer.present=false;
    for(auto& option:continued.options) option.unit.healer.present=false;
    Check(Search(continued,values,6).features[2]==0,
        "a previous breakthrough proposal cannot reuse old support or old victory after the ability disappears");
    continued.budget=1;
    Check(Search(continued,values,6).actions.empty(),"cross-round proposals do not retain old purchasing funds");
    const auto awaitResult=[](Planner& planner) {
        std::unique_ptr<Planner::Work> work;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!work && std::chrono::steady_clock::now()<end) {
            work=planner.TakeReady();
            if(!work) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return work;
    };
    Planner planner;
    auto arena=AssaultArena();
    Check(planner.Start(arena,values,1),"a planner can start the first independent frontier search");
    auto first=awaitResult(planner);
    Check(first && !first->failed && !first->result.proposals.empty() && first->result.proposals.size()<=24,
        "only bounded state-free unfinished proposals leave a complete worker result");
    arena.options.clear();
    Check(planner.Start(arena,values,2),"the same planner can resample a changed legal roster");
    auto changed=awaitResult(planner);
    Check(changed && !changed->snapshot.proposals.empty() && changed->result.actions.empty(),
        "retained proposals cannot buy units no longer present in the current legal roster");
    planner.Cancel();
    Check(planner.Start(AssaultArena(),values,3),"cancellation leaves the planner reusable for a new scene");
    auto fresh=awaitResult(planner);
    Check(fresh && fresh->snapshot.proposals.empty(),"cancel clears all cross-scene proposal history");
    std::cout<<"Assault exploration counterfactuals passed\n";
}

/** 五类型提案的保存、重映射与反事实；不将随队类型冒充五个不可缺少的能力。 */
void RunColdStorageDeepCompositionTests()
{
    using namespace ColdStorageSearch;
    auto state=AssaultArena();
    state.budget=62;
    state.capacity=5;
    // 沿用已有真实三方攻城夹具，再加两类无攻击/治疗/生产的付费成员。
    // 本用例隔离超过四类型的搜索起点流转，不声称这两名成员必须购买。
    for(int type:{76004,76005}) {
        Option passive;
        passive.type=type;
        passive.cost=1;
        passive.unit.body.x=650;
        passive.unit.body.health=1;
        passive.unit.body.speed=0;
        passive.unit.maximumBody=1;
        passive.unit.body.purchaseCost=1;
        passive.unit.biteDps=0;
        state.options.push_back(passive);
    }
    const Weights values{0,0,500,0,1,-1,0,0};
    const std::vector<Action> complete{{0,0},{1,0},{2,0},{4,0},{5,0}};
    const auto actual=EvaluateCandidate(state,values,complete);
    Check(actual.features[2]>0 && actual.features[5]==state.budget
        && actual.construction.healerCasts>1,
        "a five-type proposal is completely evaluated with the same real attack, healing and sixty-two ice payment");
    for(const auto& action:complete) {
        const auto& option=state.options[action.option];
        state.proposals.resize(1);
        state.proposals.front().push_back({option.type,option.row,option.device,option.setting,action.delay});
    }
    // 无用合法类型扩大兵池，避免仅有五个选项时完整抽样碰巧等于已知提案。
    for(int index=0;index<8;++index) {
        auto passive=state.options.back();
        passive.type=77000+index;
        state.options.push_back(passive);
    }
    const auto distinct=[](const Proposal& proposal) {
        std::set<int> types;
        for(const auto& member:proposal) types.insert(member.type);
        return types.size();
    };
    int retained=0;
    for(unsigned seed:{2u,6u,11u}) {
        const auto searched=Search(state,values,seed);
        Check(searched.proposalEvaluated>0 && searched.features[2]>0,
            "a legal five-type continuation receives full current-scene scoring instead of trusting its old outcome");
        Check(searched.features[5]<=state.budget && searched.actions.size()<=static_cast<size_t>(state.capacity),
            "deep proposal exploration and final pruning retain the same actual wallet and deployment slots");
        retained+=std::any_of(searched.proposals.begin(),searched.proposals.end(),
            [&](const Proposal& proposal){return distinct(proposal)>=5;});
    }
    Check(retained>0,
        "more-than-four-type attack intermediates can survive the frontier and leave bounded cross-round proposals");
    auto reordered=state;
    std::reverse(reordered.options.begin(),reordered.options.end());
    const auto remapped=Search(reordered,values,6);
    Check(remapped.proposalEvaluated>0 && remapped.features[2]>0 && remapped.features[5]<=reordered.budget,
        "five-type continuation identities remap after every option index changes without carrying old prices or scores");
    auto disabled=reordered;
    for(auto& option:disabled.options) option.unit.healer.present=false;
    const auto noHealing=Search(disabled,values,6);
    Check(noHealing.features[2]==0 && noHealing.actions.empty(),
        "losing the actual required support ability invalidates the old deep proposal and authorizes no historical purchase");
    auto missing=reordered;
    missing.options.erase(std::remove_if(missing.options.begin(),missing.options.end(),
        [](const Option& option){return option.unit.healer.present;}),missing.options.end());
    Check(Search(missing,values,6).features[2]==0,
        "a missing legal member cannot be silently replaced by an old deep-proposal option index");
    auto repriced=reordered;
    for(auto& option:repriced.options) if(option.type==71001) {
        option.cost=1000;
        option.unit.body.purchaseCost=1000;
    }
    const auto expensive=Search(repriced,values,6);
    Check(expensive.features[2]==0 && expensive.features[5]<=repriced.budget,
        "a new unaffordable front price cannot be paid from the price stored in a previous five-type proposal");
    reordered.budget=1;
    Check(Search(reordered,values,6).actions.empty(),
        "deep continuation does not preserve the previous treasury after actual available money disappears");
    Check(state.budget==62 && state.options[2].unit.healer.present
        && state.proposals.front().size()==5,
        "searching, pruning and stale-proposal counterfactuals do not mutate captured resources or source proposal members");
    std::cout<<"Deep composition proposal continuity and current-payment contracts passed\n";
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

/** 订冰不能在留灰烬的完整应对中挪用其阳光；未就绪卡牌不凭空冻结商店预算。 */
void RunColdStorageCounterBudgetTests()
{
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=2; state.houseX=-10000; state.searchVersion=2;
    state.anticipateEconomy=true; state.playerSun=150; state.playerIce=20;
    Unit worker;
    worker.body.row=0; worker.body.x=900; worker.body.health=500;
    worker.body.economic=true; worker.body.purchaseCost=24; worker.body.spawnAt=1;
    worker.productionRemaining=4; worker.nextYield=20;
    state.current={worker};
    Counter ash;
    ash.source=0; ash.cellRow=0; ash.cellColumn=0;
    ash.sunCost=125; ash.iceCost=20; ash.windup=1; ash.recharge=1000;
    ash.blast.x=400; ash.blast.damage=1800; ash.blast.reach.fill(-1); ash.blast.reach[0]=10000;
    auto expensive=ash;
    expensive.source=1; expensive.sunCost=1000; expensive.iceCost=80;
    expensive.cellRow=1; expensive.blast.reach.fill(-1); expensive.blast.reach[1]=10000;
    state.counters={ash,expensive};
    state.shop={{50,40,3},{100,100,3}};
    ConstructionStats ordinary,reserved;
    const auto spent=Evaluate(state,{},&ordinary);
    const auto protectedBudget=Evaluate(state,{},&reserved,0,0,0,true);
    Check(spent[4]>0 && ordinary.orders>0 && ordinary.paidCounterCasts==0,
        "the ordinary purchase posture really spends ash sun before the delayed worker arrives");
    Check(protectedBudget[4]==0 && reserved.orders==0 && reserved.paidCounterCasts==1,
        "one ready ash retains its actual sun and clears the worker before any production");
    Construction future;
    future.source=0; future.ready=1000; future.plant.health=100;
    state.construction={future};
    const auto selected=EvaluateCandidate(state,Weights{0,0,0,0,1,-1,0,0},{});
    Check(selected.rawProduction==0 && selected.construction.counterSpaceReserved,
        "the actual whole-plan comparison retains the legal reserved-wallet response");
    state.playerSun=200;
    Evaluate(state,{},&reserved,0,0,0,true);
    Check(reserved.paidCounterCasts==1 && reserved.orderSun==50,
        "a smaller affordable ice order remains legal without draining the reserved ash sun");
    state.playerSun=150; state.counters[0].blast.ready=1000;
    Evaluate(state,{},&reserved,0,0,0,true);
    Check(reserved.orders>0 && reserved.orderSun==100 && reserved.paidCounterCasts==0,
        "a cooling ash and an unaffordable alternative do not invent a reservation or a free cast");
    std::cout<<"Shared ash sun and paid ice order counterfactuals passed\n";
}

/** 现金边际效用只改变同窗评分，不改变真实钱、产能、退款或自由搜索的可支付边界。 */
void RunColdStorageCapitalUtilityTests()
{
    using namespace ColdStorageSearch;
    const auto near=[](float left,float right) {return std::abs(left-right)<.0001f;};
    // 贫困夹具刚够一次完整高价采购；富裕夹具仍能支付同一支队伍，不修改真实攻防画像。
    auto poor=CapitalArena(100), rich=CapitalArena(10000);
    {
        Snapshot late;
        late.netEconomy=true; late.weatherStation=true;
        late.budget=18387; late.capacity=169; late.recoveryReserve=48;
        Option troop; troop.cost=35; late.options={troop};
        Check(CapitalUtilityScale(late)<.05f,
            "the observed large human-match wallet discounts marginal income while retaining one expensive complete army");
        late.budget=5000;
        Check(near(CapitalUtilityScale(late),1),
            "the same real deployment capacity still preserves full cash value below one complete high-price wave");
    }
    {
        Snapshot opening;
        opening.netEconomy=true; opening.weatherStation=true;
        opening.budget=1875; opening.capacity=64; opening.recoveryReserve=48;
        Option basic; basic.cost=4; opening.options={basic};
        const float cheapOnly=CapitalUtilityScale(opening);
        opening.fundableUnlockTroopCost=35;
        Check(cheapOnly<.05f && near(CapitalUtilityScale(opening),1),
            "an affordable future unlock prevents a cheap opening pool from devaluing startup capital");
        const auto projection=DescribeCapitalUtility(opening);
        Check(projection.highestAffordableTroopCost==4 && projection.fundableUnlockTroopCost==35,
            "future capital reference does not add a locked unit to currently purchasable options");
        opening.fundableUnlockTroopCost=2000;
        Check(near(CapitalUtilityScale(opening),cheapOnly),"unaffordable future prices cannot inflate the cash reference");
        opening.fundableUnlockTroopCost=0; opening.incomingIce=1000000; opening.supplyIce=1000000;
        Check(near(CapitalUtilityScale(opening),cheapOnly),"unearned future income cannot finance an unlock capital reference");
    }
    {
        Snapshot probe;
        probe.budget=20; probe.capacity=1; probe.weatherStation=true;
        Option troop; troop.type=82001; troop.cost=4;
        troop.unit.body.health=100; troop.unit.body.x=1000;
        Option device; device.type=0; device.device=0; device.setting=1; device.cost=20; device.preference[0]=500;
        probe.options={troop,device};
        const Weights values{0,0,0,0,0,-1,0,0};
        const auto normal=Search(probe,values,42);
        Check(!normal.actions.empty() && std::all_of(normal.actions.begin(),normal.actions.end(),[&](const Action& action) {
            return probe.options[action.option].device>=0;
        }),"ordinary search may still choose a useful device-only transaction");
        probe.allowWait=false;
        const auto advancing=Search(probe,values,42);
        Check(std::any_of(advancing.actions.begin(),advancing.actions.end(),[&](const Action& action) {
            return probe.options[action.option].device<0;
        }),"unlock-path search must advance a paid troop wave instead of repeatedly buying only devices");
        Check(advancing.features[5]<=probe.budget,"wave advancement does not borrow future income");
    }
    Check(near(CapitalUtilityScale(poor),1) && near(CapitalUtilityScale(rich),.01f),
        "cash covering one complete legal highest-cost team has decreasing utility only beyond that coverage");
    auto reserve=rich;
    reserve.recoveryReserve=50;
    Check(near(CapitalUtilityScale(reserve),.01f),
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
        Check(poor.budget==100 && rich.budget==10000 && poor.options[1].unit.catapult.ammunition==2,
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
    {
        Snapshot covered;
        covered.searchVersion=2; covered.netEconomy=true;
        covered.budget=10000; covered.capacity=32; covered.houseX=-10000;
        for(int type=0;type<9;++type) {
            Option option;
            option.type=101000+type*137; option.cost=10;
            option.unit.body.x=1000; option.unit.body.health=500; option.unit.body.purchaseCost=10;
            option.unit.biteDps=0;
            covered.options.push_back(option);
        }
        int widest=0;
        for(unsigned seed=1;seed<=4;++seed) {
            covered.timeLimitedSearch=true;
            covered.searchDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(600);
            const auto sampled=Search(covered,Weights{0,0,100,0,1,-1,0,0},seed);
            widest=std::max(widest,sampled.widestComposition);
            Check(sampled.actions.empty(),"wide rich sampling cannot force purchases when every whole plan has no benefit");
        }
        Check(widest>3,"the same affordable arbitrary-type roster is actually compared beyond the old three-type sampling range");
    }
}

/** 长编队的经营与费用共用终点，训练输入保持60秒，不把尚未兑现的冰用于初始付款。 */
void RunColdStorageExtendedIncomeTests()
{
    using namespace ColdStorageSearch;
    Snapshot state;
    state.searchVersion=2; state.netEconomy=true;
    state.houseX=-10000; state.budget=24; state.capacity=1;
    Option worker;
    worker.type=95001; worker.cost=24; worker.unit.body.x=1000;
    worker.unit.body.health=500; worker.unit.body.economic=true; worker.unit.body.purchaseCost=24;
    state.options={worker};
    ConstructionStats trace;
    const auto legacy=Evaluate(state,{{0,60}},&trace);
    Check(legacy[4]==0 && trace.productionAfterWindow>worker.cost,
        "the legacy calibrated window remains 60 seconds while actual later production is separately recorded");
    const Weights income{0,0,0,0,1,-1,0,0};
    const auto payable=EvaluateCandidate(state,income,{{0,60}});
    Check(payable.features[4]==trace.productionAfterWindow && payable.rawProduction==0 && payable.productionInputs[0]==0
        && payable.score>0 && !ShouldConserveCapital(payable,24,0,0),
        "late protected income repays within the same combat window without altering trained 60-second inputs");
    ProductionCalibration calibration;
    calibration.nodes={{-1,-1,-1,0,.25f}}; state.productionCalibration=&calibration;
    Check(EvaluateCandidate(state,income,{{0,60}}).features[4]==trace.productionAfterWindow*.25f,
        "one conservative calibration factor applies to both income windows, rather than adding uncalibrated future money");
    state.productionCalibration=nullptr;
    Counter ash; ash.blast.x=1000; ash.blast.reach.fill(1000); ash.blast.damage=1800;
    ash.blast.committed=true; ash.blast.ready=64; state.counters={ash};
    const auto killed=EvaluateCandidate(state,income,{{0,60}});
    Check(killed.features[4]<=4 && killed.score<0,
        "a worker killed shortly after entry receives only completed batches rather than an assumed full future window");
    state.budget=23;
    Check(Search(state,income,42).actions.empty(),"later production cannot fund an initially unaffordable worker");
    std::cout<<"Extended income and fixed calibration-window contracts passed\n";
}

/** 两株高输出互相兜底时，主动清除允许走出第一步；收益须来自真实受击和有利资产交换。 */
void RunColdStorageSiegePreparationTests()
{
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=1; state.netEconomy=true; state.searchVersion=2;
    state.precisionReady=true; state.budget=100; state.capacity=4;
    state.opponentWeight=1; state.houseX=-10000;
    Plant first;
    first.id=101; first.x=500; first.column=3; first.health=500;
    first.dps=100; first.assetValue=200; first.reward=20;
    Plant second=first; second.id=102; second.x=400; second.column=2;
    state.plants={first,second};
    Option fragile;
    fragile.type=94001; fragile.cost=10;
    fragile.unit.body.x=1000; fragile.unit.body.health=50; fragile.unit.body.purchaseCost=10;
    fragile.unit.body.speed=10; fragile.unit.biteDps=50;
    state.options={fragile};
    const Weights values{1,1,120,0,1,-1,0,0};
    const auto prepared=Search(state,values,42);
    Check(prepared.precisionTargetID>0 && prepared.construction.precisionHits==1
        && prepared.features[2]==0 && prepared.features[0]==20
        && prepared.opponentAssets<prepared.baselineOpponentAssets,
        "proactive fire removal is an actual favorable asset exchange even while another shooter prevents immediate breakthrough");
    for(auto& plant:state.plants) plant.dps=0;
    state.options.clear();
    Check(Search(state,values,42).precisionTargetID==0,
        "valuable harmless plants do not authorize preparing a nonexistent attack through reward farming");
    state.plants[0].dps=100; state.plants[1].dps=100;
    for(auto& plant:state.plants) {plant.assetValue=1;plant.reward=1;}
    Check(Search(state,values,42).precisionTargetID==0,
        "having an attack does not waive the real exchange cost for a low-value target");
    // 超过选靶名额时，周期全场来源不能因只算一发而被逐行射手挤出候选名单。
    Snapshot repeated;
    repeated.rows=5; repeated.netEconomy=true; repeated.searchVersion=2;
    repeated.precisionReady=true; repeated.budget=100; repeated.opponentWeight=1;
    for(int i=0;i<12;++i) {
        Plant shooter; shooter.id=200+i; shooter.row=i%5; shooter.x=500;
        shooter.health=500; shooter.dps=30; shooter.assetValue=500; shooter.reward=20;
        repeated.plants.push_back(shooter);
    }
    Plant source; source.id=1000; source.row=2; source.x=400;
    source.health=300; source.assetValue=2000; source.reward=20;
    repeated.plants.push_back(source);
    repeated.rowStrikes.push_back({source.id,0,20,240,0,0});
    Check(Search(repeated,values,42).precisionTargetID==source.id,
        "a repeatedly charged all-lane strike remains in the bounded target list ahead of weaker sustained shooters");
    std::cout<<"Proactive siege preparation and real exchange contracts passed\n";
}

/** 玉米炮是可重复、可拆除的输出来源；离膛前取消、离膛后保留，不给予灰烬无敌。 */
void RunColdStorageCobForecastTests()
{
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=1; state.searchVersion=2; state.netEconomy=true; state.houseX=-10000;
    state.traceEconomy=true; state.budget=180; state.precisionTargetLimit=3;
    Plant cannon; cannon.id=91001; cannon.x=300; cannon.health=300; cannon.assetValue=600; cannon.reward=40;
    state.plants={cannon};
    Counter shot; shot.plantID=cannon.id; shot.consumesPlant=false; shot.flightSeconds=2;
    shot.windup=4; shot.recharge=10; shot.blast.x=900; shot.blast.reach.fill(-1); shot.blast.reach[0]=115; shot.blast.damage=1800;
    state.counters={shot};
    Unit target; target.body.x=900; target.body.health=100000; target.body.purchaseCost=10000;
    target.body.speed=0; target.biteDps=0; state.current={target};
    ConstructionStats stats;
    Evaluate(state,{},&stats);
    Check(stats.counterTrace.size()>1 && stats.opponentAssets==600,
        "a reusable cannon fires repeatedly without consuming its source or granting it ash immunity");
    auto eaten=state;
    auto biter=target; biter.body.x=cannon.x; biter.biteDps=1000;
    eaten.current.push_back(biter);
    const auto eatenFeatures=Evaluate(eaten,{},&stats);
    Check(stats.counterTrace.empty() && eatenFeatures[0]>0,
        "a cannon remains edible throughout its firing windup instead of inheriting instant-ash invulnerability");
    auto beforeLaunch=state; beforeLaunch.precisionTargetID=cannon.id;
    beforeLaunch.counters[0].windup=ColdStorageSkillRules::StrikeAimDuration+shot.flightSeconds+1;
    Evaluate(beforeLaunch,{},&stats);
    Check(stats.precisionHits==1 && stats.counterTrace.empty(),
        "destroying a cannon before launch cancels its queued shot and all future reloads");
    beforeLaunch.counters[0].windup=ColdStorageSkillRules::StrikeAimDuration+shot.flightSeconds;
    Evaluate(beforeLaunch,{},&stats);
    Check(stats.counterTrace.empty(),"source removal resolved on the launch step cannot be bypassed by floating-point time drift");
    auto afterLaunch=state; afterLaunch.precisionTargetID=cannon.id;
    afterLaunch.counters[0].windup=ColdStorageSkillRules::StrikeAimDuration+shot.flightSeconds-1;
    Evaluate(afterLaunch,{},&stats);
    Check(stats.precisionHits==1 && stats.counterTrace.size()==1,
        "destroying a cannon after launch preserves exactly its independent in-flight shot");
    auto committed=state; committed.counters[0].blast.committed=true; committed.counters[0].blast.ready=4;
    committed.pendingPrecisionID=cannon.id; committed.pendingPrecisionRemaining=1;
    Evaluate(committed,{},&stats);
    Check(stats.counterTrace.empty(),"captured pre-launch shots retain their living source dependency");
    committed.pendingPrecisionRemaining=3;
    Evaluate(committed,{},&stats);
    Check(stats.counterTrace.size()==1,"captured shots detach at launch rather than remaining source-bound until impact");
    committed.counters[0].plantID=0; committed.pendingPrecisionRemaining=1;
    Evaluate(committed,{},&stats);
    Check(stats.counterTrace.size()==1,"already airborne captured shells are independent of a destroyed source");
    auto preparation=state; preparation.current.clear(); preparation.precisionReady=true; preparation.opponentWeight=1;
    for(int i=1;i<4;++i) {
        auto extra=cannon; extra.id+=i; extra.x-=i*30; preparation.plants.push_back(extra);
        auto counter=shot; counter.plantID=extra.id; counter.source=i; preparation.counters.push_back(counter);
    }
    const Weights values{1,1,120,0,1,-1,0,0};
    const auto result=Search(preparation,values,42);
    Check(result.precisionTargetID>0 && result.construction.precisionHits>0 && result.features[2]==0,
        "free search may remove valuable repeatable cannon fire before any army can immediately breach");
    preparation.counters.clear();
    Check(Search(preparation,values,42).precisionTargetID==0,"harmless former cannon assets do not authorize reward farming");
    auto lastChance=state; lastChance.current.clear(); lastChance.rows=5; lastChance.budget=150;
    lastChance.capacity=15; lastChance.fallbackAllIn=true;
    lastChance.counters.clear();
    for(int row=0;row<5;++row) {
        auto counter=shot; counter.recharge=1000; counter.blast.reach.fill(-1);
        for(int r=0;r<5;++r) if(std::abs(r-row)<=1) counter.blast.reach[r]=115;
        lastChance.counters.push_back(counter);
        Option soldier; soldier.type=99001; soldier.row=soldier.unit.body.row=row; soldier.cost=10;
        soldier.unit.body.x=900; soldier.unit.body.health=100; soldier.unit.body.purchaseCost=10;
        soldier.unit.body.speed=0; soldier.unit.biteDps=0; lastChance.options.push_back(soldier);
    }
    const auto dispersed=Search(lastChance,InitialWeights,7);
    std::set<int> routes;
    for(const auto& action:dispersed.actions) routes.insert(lastChance.options[action.option].row);
    const bool delayed=std::any_of(dispersed.actions.begin(),dispersed.actions.end(),[](const Action& a){return a.delay>0;});
    Check(dispersed.fallbackMode==2 && dispersed.features[3]>0 && (routes.size()>1 || delayed),
        "last-chance comparisons preserve survivors through actual spread or timing rather than synchronizing every troop under one shell");
    std::cout<<"Cob source, launch commitment, repeated fire and proactive removal contracts passed\n";
}

/** 多目标同次技能：身份/收费/已付款与自主目标基数，另隔离完整部队共用钱包的突破。 */
void RunColdStorageMultiPrecisionTests()
{
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=3; state.searchVersion=2; state.netEconomy=true;
    state.houseX=-10000; state.budget=180; state.capacity=0;
    state.precisionTargetLimit=3;
    for(int index=0;index<3;++index) {
        Plant plant;
        plant.id=87001+index; plant.row=index; plant.x=500;
        plant.health=300; plant.immuneRemaining=1000; plant.reward=10;
        state.plants.push_back(plant);
    }
    state.precisionTargetID=state.plants[0].id;
    state.precisionAdditionalTargetIDs={state.plants[1].id,state.plants[2].id};
    ConstructionStats stats;
    const auto three=Evaluate(state,{},&stats);
    Check(three[5]==180 && three[0]==30 && stats.precisionHits==3 && stats.abilityIceSpent==180,
        "three cross-row identities resolve once at their shared aim deadline and each charge sixty actual ice");
    auto legacy=state;
    legacy.precisionTargetLimit=1;
    Check(Evaluate(legacy,{},&stats)[5]==60 && stats.precisionHits==1,
        "the default single-target interface preserves old forecast fixtures instead of silently opening three targets");
    auto duplicate=state;
    duplicate.precisionAdditionalTargetIDs={state.precisionTargetID,state.plants[1].id,state.plants[1].id,state.plants[2].id,99999};
    Check(Evaluate(duplicate,{},&stats)[5]==180 && stats.precisionHits==3,
        "duplicate identities cannot create repeated hits and no forecast intent exceeds three distinct identities");
    auto pending=state;
    pending.precisionReady=false; pending.precisionTargetID=0; pending.precisionAdditionalTargetIDs.clear();
    pending.pendingPrecisionID=state.precisionTargetID;
    pending.pendingPrecisionAdditionalIDs=state.precisionAdditionalTargetIDs;
    pending.pendingPrecisionRemaining=2; pending.budget=0; pending.precisionTargetLimit=1;
    const auto paid=Evaluate(pending,{},&stats);
    Check(paid[5]==0 && paid[0]==30 && stats.precisionHits==3 && stats.abilityIceSpent==0,
        "all already-paid targets resolve without new fees even when the caller's new-purchase limit differs");
    pending.pendingPrecisionAdditionalIDs[0]=99999;
    Plant replacement=state.plants[1]; replacement.id=88002;
    pending.plants[1]=replacement;
    Check(Evaluate(pending,{},&stats)[0]==20 && stats.precisionHits==2,
        "a missing secondary identity does not retarget the replacement at its previous row and cell");
    auto died=state;
    for(auto& plant:died.plants) plant.immuneRemaining=0;
    Unit killer;
    killer.body.row=1; killer.body.x=550; killer.body.health=500;
    killer.body.speed=0; killer.biteDps=1000;
    died.current={killer};
    const auto gone=Evaluate(died,{},&stats);
    Check(gone[5]==180 && gone[0]==30 && stats.precisionHits==2,
        "a target killed during aim remains paid but earns no second kill reward when the other two shots resolve");
    Check(state.plants[0].health==300 && state.precisionAdditionalTargetIDs.size()==2,
        "parallel-target forecasting does not consume captured plants, wallet or intent members");

    // 储存灰烬的较晚释放会选中另一完整玩家应对；该结果也必须携带全部已计费的目标。
    auto counterWorld=state;
    counterWorld.playerSun=500; counterWorld.playerIce=50;
    Plant stored;
    stored.id=88010; stored.x=800; stored.health=300; stored.edible=false;
    counterWorld.plants.push_back(stored);
    for(float arrival:{0.0f,12.0f,30.0f}) {
        Unit worker;
        worker.body.x=900; worker.body.health=500; worker.body.economic=true;
        worker.body.purchaseCost=24; worker.body.spawnAt=arrival; worker.biteDps=0;
        counterWorld.current.push_back(worker);
    }
    Counter ash;
    ash.plantID=stored.id; ash.stored=true; ash.sunCost=75; ash.iceCost=5;
    ash.windup=0; ash.recharge=10000; ash.blast.x=900; ash.blast.damage=1800;
    ash.blast.reach.fill(1000); counterWorld.counters={ash};
    const Weights income{0,0,0,0,1,-1,0,0};
    const auto responded=EvaluateCandidate(counterWorld,income,{});
    Check(responded.counterHoldSeconds>0 && responded.precisionTargetID==state.precisionTargetID
        && responded.precisionAdditionalTargetIDs==state.precisionAdditionalTargetIDs
        && responded.features[5]==180 && responded.construction.precisionHits==3,
        "a selected delayed-counter world preserves every target, full skill fee and actual independent hit");

    // 同一合法交易比较1/2/3；不用军队或人为收益迫使一定三连。
    const Weights values{1,1,120,0,1,-1,0,0};
    auto selectable=state;
    selectable.precisionTargetID=0; selectable.precisionAdditionalTargetIDs.clear();
    selectable.precisionReady=true; selectable.opponentWeight=1;
    for(auto& plant:selectable.plants) {plant.dps=100;plant.assetValue=200;plant.reward=20;}
    const auto all=Search(selectable,values,42);
    Check(all.precisionTargetID>0 && all.precisionAdditionalTargetIDs.size()==2
        && all.features[5]==180 && all.construction.precisionHits==3,
        "free bounded search can choose a favorable three-target exchange across rows without troop purchase");
    selectable.plants[2].dps=0; selectable.plants[2].assetValue=0; selectable.plants[2].reward=0;
    const auto two=Search(selectable,values,42);
    Check(two.precisionTargetID>0 && two.precisionAdditionalTargetIDs.size()==1 && two.features[5]==120,
        "an unnecessary third shot loses to the actual favorable two-target transaction");
    selectable.plants[1].dps=0; selectable.plants[1].assetValue=0; selectable.plants[1].reward=0;
    const auto one=Search(selectable,values,42);
    Check(one.precisionTargetID==selectable.plants[0].id && one.precisionAdditionalTargetIDs.empty()
        && one.features[5]==60,
        "one useful source removal beats adding two harmless targets even when all three are affordable");
    selectable.budget=59;
    Check(Search(selectable,values,42).precisionTargetID==0,
        "a multi-target option cannot bypass the price of the first shot when the actual wallet is fifty-nine");

    // 三株互相兜底的火力先清除，再由任意普通可购画像进入；技能和队员没有分离的钱包。
    Snapshot joint;
    joint.rows=1; joint.searchVersion=2; joint.netEconomy=true; joint.precisionReady=true;
    joint.precisionTargetLimit=3; joint.budget=181; joint.capacity=1; joint.houseX=0;
    for(int index=0;index<3;++index) {
        Plant fire;
        fire.id=89001+index; fire.x=400+index*80; fire.health=100000; fire.dps=1000;
        joint.plants.push_back(fire);
    }
    Option troop;
    troop.type=89900; troop.cost=1; troop.unit.body.x=1000;
    troop.unit.body.health=20; troop.unit.body.speed=50; troop.unit.body.purchaseCost=1;
    troop.unit.biteDps=10; joint.options={troop};
    auto known=joint;
    known.precisionTargetID=joint.plants[0].id;
    known.precisionAdditionalTargetIDs={joint.plants[1].id,joint.plants[2].id};
    const float followupDelay=ColdStorageSkillRules::StrikeAimDuration+1;
    const auto complete=Evaluate(known,{{0,followupDelay}},&stats);
    Check(complete[2]>0 && complete[5]==181 && stats.precisionHits==3,
        "three removals and delayed ordinary entry have a real jointly affordable breakthrough");
    known.precisionAdditionalTargetIDs.pop_back();
    Check(Evaluate(known,{{0,followupDelay}})[2]==0,
        "the surviving third firing source prevents the same ordinary followup from claiming breakthrough");
    const auto chosen=Search(joint,values,17);
    Check(chosen.features[2]>0 && chosen.precisionAdditionalTargetIDs.size()==2
        && chosen.actions.size()==1 && chosen.features[5]==181,
        "free search jointly funds all three original targets and its actual followup from one wallet");
    joint.budget=180;
    const auto shortWallet=Search(joint,values,17);
    Check(shortWallet.features[2]==0 && shortWallet.features[5]<=180,
        "paying three shots leaves no borrowed ice for the initially unaffordable followup");
    joint.precisionReady=false; joint.budget=1;
    joint.pendingPrecisionID=joint.plants[0].id;
    joint.pendingPrecisionAdditionalIDs={joint.plants[1].id,joint.plants[2].id};
    joint.pendingPrecisionRemaining=2;
    const auto pendingFollow=Search(joint,values,17);
    Check(pendingFollow.features[2]>0 && pendingFollow.features[5]==1
        && pendingFollow.precisionTargetID==0 && pendingFollow.precisionAdditionalTargetIDs.empty(),
        "an already-paid multi-shot can help a new payable troop without being charged or submitted again");
    std::cout<<"Multi-target precision identities, cardinality and shared-wallet contracts passed\n";
}

/** 一波后的技能只凭有成本、延迟生效的完整反事实竞争；不提前施法、不把等待变成强制采购。 */
void RunColdStoragePrecisionUnlockForecastTests()
{
    using namespace ColdStorageSearch;
    Snapshot future;
    future.rows=1; future.searchVersion=2; future.netEconomy=true;
    future.precisionUnlockAfterPurchase=true; future.precisionUnlockAimStartSeconds=3;
    future.precisionTargetLimit=3; future.budget=181; future.capacity=1; future.houseX=0;
    for(int index=0;index<3;++index) {
        Plant source;
        source.id=91101+index; source.x=400+index*80; source.health=100000; source.dps=1000;
        future.plants.push_back(source);
    }
    Option troop;
    troop.type=91231; troop.cost=1; troop.unit.body.x=1000;
    troop.unit.body.health=20; troop.unit.body.speed=50; troop.unit.body.purchaseCost=1;
    troop.unit.biteDps=10; future.options={troop};
    auto known=future;
    known.precisionTargetID=future.plants[0].id;
    known.precisionAdditionalTargetIDs={future.plants[1].id,future.plants[2].id};
    ConstructionStats stats;
    const float followupDelay=future.precisionUnlockAimStartSeconds+ColdStorageSkillRules::StrikeAimDuration+1;
    const auto delayed=Evaluate(known,{{0,followupDelay}},&stats);
    Check(delayed[2]>0 && delayed[5]==181 && stats.precisionHits==3 && stats.abilityIceSpent==180,
        "a paid purchase followed by next-decision aiming can jointly fund three removals and a real later entry");
    Check(Evaluate(known,{{0,0}},&stats)[2]==0 && stats.precisionHits==3,
        "the troop entering before future aim finishes dies to the still-living sources instead of borrowing immediate clearance");
    Check(Evaluate(known,{},&stats)[0]==0 && stats.precisionHits==0 && stats.abilityIceSpent==0,
        "empty waiting cannot obtain next-wave precision without a real paid troop");
    auto deviceOnly=known;
    deviceOnly.options[0].device=0;
    Evaluate(deviceOnly,{{0,0}},&stats);
    Check(stats.precisionHits==0 && stats.abilityIceSpent==0,
        "a weather-device transaction does not advance the paid troop wave or authorize future aiming");
    // 使用失败候选的全局评分和两个实际训练随机种子；画像/ID独立于正式僵尸，不硬编码突破组合。
    const Weights candidateOne{4.26112446f,1.31421143f,112.454357f,.33995646f,3.49586196f,0,-.27357251f,1.758715f};
    for(unsigned seed:{982268721u,172588331u}) {
        auto locked=future; locked.precisionUnlockAfterPurchase=false;
        Check(Search(locked,candidateOne,seed).actions.empty(),
            "the same legal bodies under the failed candidate weights have no invented breakthrough before skill unlock");
        const auto unlocked=Search(future,candidateOne,seed);
        Check(unlocked.features[2]>0 && unlocked.actions.size()==1 && unlocked.features[5]==181
            && unlocked.precisionTargetID==0 && unlocked.precisionAdditionalTargetIDs.empty()
            && unlocked.forecastPrecisionTargetID>0 && unlocked.forecastPrecisionAdditionalTargetIDs.size()==2
            && unlocked.forecastPrecisionIce==180 && unlocked.forecastPrecisionAimStartSeconds==3,
            "free search can choose a paid next-wave opportunity while exporting no immediately payable strike intent");
    }
    auto realtime=future;
    realtime.timeLimitedSearch=true;
    realtime.searchDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(300);
    const auto boundedRealtime=Search(realtime,candidateOne,172588331u);
    Check(boundedRealtime.forecastPrecisionTargetID>0 && boundedRealtime.features[2]>0
        && boundedRealtime.precisionTargetID==0 && boundedRealtime.features[5]<=realtime.budget,
        "the same real-time deadline keeps next-wave skill comparison inside the normal bounded search budget");
    const auto diagnostic=EvaluateCandidate(known,candidateOne,{{0,followupDelay}});
    Check(diagnostic.precisionTargetID==0 && diagnostic.forecastPrecisionIce==180 && diagnostic.features[5]==181,
        "direct diagnostics keep all predicted fees but cannot turn the future shot into a current Board transaction");
    auto shortWallet=future;
    shortWallet.budget=180; shortWallet.supplyIce=10000; shortWallet.supplyInterval=1;
    const auto poor=Search(shortWallet,candidateOne,172588331u);
    Check(poor.features[2]==0 && poor.forecastPrecisionTargetID==0 && poor.precisionTargetID==0,
        "future supply and kill proceeds cannot prepay the initially unaffordable troop plus three shots");
    auto missing=known;
    missing.precisionAdditionalTargetIDs[0]=999999;
    Evaluate(missing,{{0,followupDelay}},&stats);
    Check(stats.precisionHits==2 && stats.abilityIceSpent==180,
        "a disappeared future identity receives no replacement while the other two original targets remain independent");
    auto diesBeforeAim=known;
    Unit killer;
    killer.body.x=520; killer.body.health=1000000000; killer.body.speed=0; killer.biteDps=1000000;
    diesBeforeAim.current={killer};
    Evaluate(diesBeforeAim,{{0,followupDelay}},&stats);
    Check(stats.precisionHits==2 && stats.abilityIceSpent==180,
        "a source destroyed before the next decision is not hit twice or replaced by a newly chosen forecast identity");
    auto tooLate=known;
    tooLate.precisionUnlockAimStartSeconds=1000;
    Check(Evaluate(tooLate,{{0,followupDelay}},&stats)[2]==0 && stats.precisionHits==0,
        "a future shot outside the forecast window cannot masquerade as an immediately cleared front");
    auto capacity=known;
    capacity.capacity=4; capacity.deploymentCapital=2100; capacity.deploymentOccupied=62; capacity.budget=2100;
    const auto bounded=EvaluateCandidate(capacity,candidateOne,{{0,followupDelay},{0,followupDelay},{0,followupDelay},{0,followupDelay}});
    Check(bounded.actions.size()==2 && bounded.features[5]==182 && bounded.forecastPrecisionIce==180,
        "future skill capital reserves reduce the whole paid team to two slots without paying the prediction now");
    auto harmless=future;
    harmless.houseX=-10000;
    for(auto& plant:harmless.plants) {plant.dps=0; plant.reward=0; plant.assetValue=0;}
    harmless.options[0].unit.body.speed=0; harmless.options[0].unit.biteDps=0;
    const auto wait=Search(harmless,candidateOne,172588331u);
    Check(wait.actions.empty() && wait.forecastPrecisionTargetID==0 && wait.features[5]==0,
        "a one-wave unlock alone gives no purchase reward and leaves true no-return waiting available");
    auto preparation=future;
    preparation.houseX=-10000; preparation.opponentWeight=1;
    preparation.options[0].unit.body.speed=0; preparation.options[0].unit.biteDps=0;
    for(auto& plant:preparation.plants) {plant.dps=100; plant.reward=20; plant.assetValue=200;}
    const auto three=Search(preparation,candidateOne,172588331u);
    Check(three.forecastPrecisionAdditionalTargetIDs.size()==2 && three.forecastPrecisionIce==180,
        "next-wave preparation can freely compare a favorable cross-target three-shot exchange plus its real advancing purchase");
    preparation.plants[2].dps=0; preparation.plants[2].assetValue=0; preparation.plants[2].reward=0;
    const auto two=Search(preparation,candidateOne,172588331u);
    Check(two.forecastPrecisionTargetID>0 && two.forecastPrecisionAdditionalTargetIDs.size()==1
        && two.forecastPrecisionIce==120 && two.features[5]==121,
        "two useful future shots beat paying for a third harmless identity even before actual skill unlock");
    preparation.plants[1].dps=0; preparation.plants[1].assetValue=0; preparation.plants[1].reward=0;
    const auto one=Search(preparation,candidateOne,172588331u);
    Check(one.forecastPrecisionTargetID>0 && one.forecastPrecisionAdditionalTargetIDs.empty()
        && one.forecastPrecisionIce==60 && one.features[5]==61,
        "a single useful future shot retains the cheaper cardinality instead of forcing the three-target limit");
    Check(future.allowWait && !future.precisionReady && future.budget==181 && future.plants[0].health==100000,
        "the lookahead does not mutate live qualification, force purchase, consume the wallet or damage captured targets");
    std::cout<<"Paid next-wave precision opportunity, timing and shared-wallet counterfactuals passed\n";
}

/** 技能/设备花掉资本会缩容量，兵种购买仍转成资产；整案修复须先限制数量再评分。 */
void RunColdStorageDeploymentTransactionTests()
{
    using namespace ColdStorageSearch;
    Snapshot state;
    state.rows=1; state.searchVersion=2; state.netEconomy=true; state.houseX=-10000;
    state.budget=400; state.capacity=4; state.deploymentCapital=2100; state.deploymentOccupied=62;
    state.precisionTargetLimit=3; state.precisionTargetID=91001;
    state.precisionAdditionalTargetIDs={91002,91003};
    for(int index=0;index<3;++index) {
        Plant fire;
        fire.id=91001+index; fire.x=400+index*80; fire.health=100000; fire.dps=1000;
        fire.assetValue=200; fire.edible=false;
        state.plants.push_back(fire);
    }
    Option worker;
    worker.type=91900; worker.cost=24; worker.unit.body.x=1000;
    worker.unit.body.health=500; worker.unit.body.purchaseCost=24;
    worker.unit.body.economic=true; worker.unit.body.speed=0; worker.unit.biteDps=0;
    state.options={worker};
    const Weights profit{0,0,0,0,1,-1,0,0};
    const float followupDelay=ColdStorageSkillRules::StrikeAimDuration+1;
    const std::vector<Action> four(4,{0,followupDelay});
    const auto repaired=EvaluateCandidate(state,profit,four);
    Check(repaired.actions.size()==2 && repaired.features[5]==228
        && repaired.features[4]>repaired.features[5] && repaired.construction.precisionHits==3,
        "capital 2100 and sixty-two occupied slots allow only two new workers after the complete 180-ice skill fee");
    const auto two=EvaluateCandidate(state,profit,{{0,followupDelay},{0,followupDelay}});
    Check(two.actions.size()==2 && two.features==repaired.features,
        "the valid two-member transaction receives the same full evaluation instead of paying a partial four-member plan");
    auto fixed=state;
    fixed.deploymentCapital=-1;
    Check(EvaluateCandidate(fixed,profit,four).actions.size()==4,
        "old pure fixtures keep their explicitly fixed capacity when no authoritative deployment capital is supplied");
    auto capped=state;
    capped.capacity=1;
    Check(EvaluateCandidate(capped,profit,four).actions.size()==1,
        "recomputed capital capacity cannot increase the caller's already-smaller available slot limit");
    auto future=state;
    future.supplyRemaining=0; future.supplyInterval=1; future.supplyIce=10000;
    Check(EvaluateCandidate(future,profit,four).actions.size()==2,
        "future actual supply inside the forecast cannot prepay the initial transaction's deployment capacity");
    auto searched=state;
    searched.precisionTargetID=0; searched.precisionAdditionalTargetIDs.clear(); searched.precisionReady=true;
    const auto discovered=Search(searched,profit,17);
    Check(discovered.precisionAdditionalTargetIDs.size()==2 && discovered.actions.size()==2
        && discovered.features[5]==228 && discovered.features[4]>discovered.features[5],
        "free multi-shot and production exploration respects the skill-reduced live capacity before choosing a payable full plan");

    auto device=state;
    device.precisionTargetID=0; device.precisionAdditionalTargetIDs.clear(); device.plants.clear();
    device.weatherStation=true;
    device.station.controls[WeatherStationRules::FOG].value=1;
    Option clear;
    clear.type=-910; clear.device=WeatherStationRules::FOG; clear.setting=0;
    clear.cost=WeatherStationRules::Cost(clear.device,clear.setting);
    device.options.push_back(clear);
    auto withDevice=four; withDevice.push_back({1,0});
    const auto closed=EvaluateCandidate(device,profit,withDevice);
    const auto troops=[&](const Result& result) {
        return std::count_if(result.actions.begin(),result.actions.end(),[&](const Action& action){return device.options[action.option].device<0;});
    };
    Check(troops(closed)==3 && closed.actions.size()==4 && closed.features[5]==112,
        "a forty-ice device at the end of the cart reduces capital-derived troop slots before any member is purchased");
    auto ahead=withDevice;
    std::rotate(ahead.begin(),ahead.end()-1,ahead.end());
    const auto earlyDevice=EvaluateCandidate(device,profit,ahead);
    Check(troops(earlyDevice)==3 && earlyDevice.features[5]==closed.features[5],
        "reordering the same device payment cannot regain troop slots by temporarily buying the army first");
    withDevice.push_back({1,0});
    const auto duplicated=EvaluateCandidate(device,profit,withDevice);
    Check(troops(duplicated)==3 && duplicated.features[5]==112,
        "a repeated setting is removed and does not charge a second fee or shrink capital twice");
    device.precisionTargetID=state.precisionTargetID;
    device.precisionAdditionalTargetIDs=state.precisionAdditionalTargetIDs;
    device.plants=state.plants;
    const auto combined=EvaluateCandidate(device,profit,withDevice);
    Check(troops(combined)==2 && combined.actions.size()==3 && combined.features[5]==268,
        "all three skill fees and the retained device payment jointly determine the same remaining troop capacity");
    Check(state.deploymentCapital==2100 && state.deploymentOccupied==62 && state.budget==400,
        "capacity admission and complete forecast cannot consume or invent the captured capital, occupied slots or actual wallet");
    std::cout<<"Complete deployment capital, device and skill transaction contracts passed\n";
}
