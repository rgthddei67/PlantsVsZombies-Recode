#include "Game/AI/ColdStorageFormationSeeds.h"
#include "Game/AI/ColdStorageSearch.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {
using namespace ColdStorageSearch;

/** 本专项只证明候选覆盖、完整付款及正式反制入口，不把固定搭配当成战果。 */
void CheckLightAsh(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED light ash experience: "<<message<<'\n';std::exit(1);}
}

/** 合法选项已由调用方提供；使用无品种含义的ID，能力与费用独立设置。 */
Snapshot LightAshArena() {
    Snapshot s;s.rows=5;s.netEconomy=true;s.searchVersion=2;s.budget=500;s.capacity=64;
    for(int row=0;row<s.rows;++row) for(int role=0;role<5;++role) {
        Option o;o.type=57000+role;o.row=row;o.cost=12;
        auto& u=o.unit;u.body.row=row;u.body.x=1140;u.body.speed=20;
        u.body.health=1300;u.maximumBody=270;
        if(role==0) {u.body.health=u.maximumBody=6000;o.cost=30;u.body.speed=10;}
        if(role==1) {u.shieldHealth=u.maximumShield=1030;u.shieldedAshCap=320;}
        if(role==2) {u.body.health=1000;u.maximumBody=500;u.shieldHealth=u.maximumShield=500;u.ladder.present=true;u.ladder.placementSeconds=2;}
        if(role==3) {u.body.health=1670;u.maximumBody=270;u.helmHealth=u.maximumHelm=1400;u.body.speed=50;o.cost=16;}
        if(role==4) {u.body.health=u.maximumBody=850;u.body.speed=10;u.catapult.present=true;}
        u.body.value=u.body.purchaseCost=static_cast<float>(o.cost);
        s.options.push_back(o);
    }
    return s;
}

/** 两路均有独立抗灰护卫而没有额外支援，才能识别为本次新增的轻装完整案。 */
bool IsLightAsh(const Snapshot& s,const std::vector<Action>& plan,bool ranged) {
    std::set<int> shieldRows,attackRows;
    for(const auto& a:plan) {
        const auto& o=s.options[a.option];const auto& u=o.unit;
        if(u.clock.present || u.drum.enabled || u.sniper.enabled || u.body.economic) return false;
        if(u.shieldHealth>0 && u.shieldedAshCap>0) shieldRows.insert(o.row);
        if(ranged ? u.catapult.present : u.ladder.present) attackRows.insert(o.row);
    }
    return shieldRows.size()==2 && shieldRows==attackRows;
}

/** 保留动作的原提交次序，使没有炮数采购规则的反事实可以逐项比较。 */
bool SamePlans(const std::vector<std::vector<Action>>& a,const std::vector<std::vector<Action>>& b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) {
        if(a[i].size()!=b[i].size()) return false;
        for(size_t j=0;j<a[i].size();++j)
            if(a[i][j].option!=b[i][j].option || a[i][j].delay!=b[i][j].delay) return false;
    }
    return true;
}

/** 合成编号只便于断言；各能力、价格和出生速度在本夹具逐项设置，不从名称推断。 */
Snapshot RunnerArena() {
    Snapshot s;s.rows=5;s.netEconomy=true;s.budget=5000;s.capacity=64;
    for(int row=0;row<s.rows;++row) for(int role=0;role<15;++role) {
        Option o;o.type=64000+role;o.row=row;o.cost=10;
        auto& u=o.unit;u.body.row=row;u.body.x=1140;u.body.speed=12;u.body.health=1370;
        if(role==1) {u.body.health=2000;u.body.speed=55;o.cost=30;}
        if(role==2) {u.body.health=500;u.body.speed=70;u.upperForecastMoveSpeed=80;o.cost=8;}
        if(role==3) {u.body.health=500;u.body.economic=true;o.cost=24;}
        if(role==4) {u.body.health=1000;u.pressure=true;o.cost=20;}
        if(role==5) {u.body.health=1000;u.ladder.present=true;u.ladder.placementSeconds=2;o.cost=12;}
        if(role==6) {u.body.health=6000;u.body.smashSeconds=3;o.cost=100;}
        if(role==7) {u.shieldHealth=1030;u.shieldedAshCap=320;o.cost=14;}
        if(role==8) {u.body.health=850;u.catapult.present=true;o.cost=12;}
        if(role==9) {u.body.health=1000;u.clock.present=true;o.cost=18;}
        if(role==10) {u.body.health=1000;u.drum.enabled=true;o.cost=18;}
        if(role==11) {u.body.health=1000;u.engineer=true;o.cost=35;}
        if(role==12) {u.body.health=1000;u.healer.present=true;o.cost=20;}
        if(role==13) {u.body.health=1500;u.floodMortar=true;o.cost=30;}
        if(role==14) {u.body.health=500;u.sniper.enabled=true;o.cost=20;}
        u.maximumBody=u.body.health-u.shieldHealth;u.maximumShield=u.shieldHealth;
        u.body.value=u.body.purchaseCost=static_cast<float>(o.cost);
        s.options.push_back(o);
    }
    return s;
}

/** 仅用于测试夹具编号的成员计数，正式选角不读取 type。 */
int RunnerCount(const Snapshot& s,const std::vector<Action>& plan,int role) {
    return static_cast<int>(std::count_if(plan.begin(),plan.end(),[&](const auto& a){return s.options[a.option].type==64000+role;}));
}

/** 纯突击对照必须为同种、同行、同批完整小队。 */
bool PureRunner(const Snapshot& s,const std::vector<Action>& plan,int role,int count) {
    return plan.size()==static_cast<size_t>(count) && RunnerCount(s,plan,role)==count
        && std::all_of(plan.begin(),plan.end(),[&](const auto& a){return a.delay==0 && s.options[a.option].row==s.options[plan[0].option].row;});
}

/** 两路均须有完整护卫/突击核心，经营版另须每路两工人；不能把残缺队伍误记为成功。 */
bool SplitRunner(const Snapshot& s,const std::vector<Action>& plan,bool economy) {
    if(plan.size()!=static_cast<size_t>(economy ? 12 : 8)) return false;
    std::array<std::array<int,3>,6> counts{};
    std::set<int> rows;
    for(const auto& a:plan) {
        const auto& o=s.options[a.option];const int role=o.type-64000;
        if(role!=0 && role!=2 && role!=3) return false;
        ++counts[o.row][role==0 ? 0 : role==2 ? 1 : 2];rows.insert(o.row);
    }
    if(rows.size()!=2 || *rows.rbegin()-*rows.begin()<2) return false;
    for(int row:rows) if(counts[row]!=std::array<int,3>{1,3,economy ? 2 : 0}) return false;
    return true;
}

/** 有界覆盖与真实整案入场契约；不运行游戏、不证明胜率或预测的未来阶段已经对齐。 */
void CheckRunnerCandidates() {
    auto s=RunnerArena();const auto plans=BuildExperiencedFormations(s,64,5000);
    std::set<size_t> pureSizes;size_t firstLight=plans.size(),firstHeavy=plans.size(),newCount=0;
    bool splitAttack=false,splitEconomy=false,guardScreen=false,fastScreen=false,ladderFollow=false,breakerFollow=false,ashFollow=false;
    for(size_t i=0;i<plans.size();++i) {
        const auto& plan=plans[i];int spent=0;
        for(const auto& a:plan) {
            CheckLightAsh(a.option>=0 && a.option<static_cast<int>(s.options.size()) && a.delay>=0 && a.delay<=60,
                "runner proposal uses legal current options and bounded relative timing");
            spent+=s.options[a.option].cost;
        }
        CheckLightAsh(spent<=5000 && plan.size()<=64,"runner and old recipes share the full cash and slot limit");
        if(RunnerCount(s,plan,2)>0) {
            ++newCount;firstLight=std::min(firstLight,i);
            const bool pure=PureRunner(s,plan,2,static_cast<int>(plan.size()));
            CheckLightAsh(pure || SplitRunner(s,plan,false) || SplitRunner(s,plan,true),"new light role cannot leak into old support recipes");
            if(pure) pureSizes.insert(plan.size());
        }
        if(PureRunner(s,plan,1,8)) firstHeavy=std::min(firstHeavy,i);
        splitAttack|=SplitRunner(s,plan,false);splitEconomy|=SplitRunner(s,plan,true);
        guardScreen|=plan.size()==3 && RunnerCount(s,plan,0)==1 && RunnerCount(s,plan,4)==2;
        fastScreen|=plan.size()==4 && RunnerCount(s,plan,1)==2 && RunnerCount(s,plan,4)==2;
        ladderFollow|=RunnerCount(s,plan,5)>0 && RunnerCount(s,plan,1)==6;
        breakerFollow|=RunnerCount(s,plan,6)==2 && RunnerCount(s,plan,1)==6;
        ashFollow|=IsLightAsh(s,plan,false) && RunnerCount(s,plan,1)>=4;
    }
    CheckLightAsh(plans.size()==96 && newCount==5,"full roster stays at 96 with exactly five light comparisons");
    CheckLightAsh(pureSizes==std::set<size_t>{4,8,16} && splitAttack && splitEconomy,"all three pure sizes and both complete two-route variants are represented");
    CheckLightAsh(firstLight==1 && firstHeavy<32,"new light and old durable fast entries both appear early in a saturated roster");
    CheckLightAsh(RunnerCount(s,plans[0],0)==1 && RunnerCount(s,plans[0],3)==3,"the original guard/worker family retains the first entry");
    CheckLightAsh(guardScreen && fastScreen && ladderFollow && breakerFollow && ashFollow,"old guard, shooter, ladder, breaker and ash recipes retain durable fast semantics");
    auto renamed=s;for(size_t i=0;i<renamed.options.size();++i) renamed.options[i].type=970000-static_cast<int>(i)*17;
    CheckLightAsh(SamePlans(plans,BuildExperiencedFormations(renamed,64,5000)),"type renumbering preserves every action, route and delay");

    const auto hasSplit=[](const Snapshot& field,int slots,int budget,bool economy) {
        const auto candidates=BuildExperiencedFormations(field,slots,budget);
        return std::any_of(candidates.begin(),candidates.end(),[&](const auto& p){return SplitRunner(field,p,economy);});
    };
    CheckLightAsh(hasSplit(s,12,164,true) && hasSplit(s,8,68,false),"exact cash and exact slots admit the complete economy and attack variants");
    CheckLightAsh(!hasSplit(s,11,164,true) && !hasSplit(s,12,163,true)
        && !hasSplit(s,7,68,false) && !hasSplit(s,8,67,false),"one missing ice or slot rejects the full recipe without truncation");
    auto noWorker=s;std::erase_if(noWorker.options,[](const auto& o){return o.unit.body.economic;});
    CheckLightAsh(!hasSplit(noWorker,64,5000,true) && hasSplit(noWorker,64,5000,false),"missing workers remove only the economy comparison");
    auto noGuard=s;std::erase_if(noGuard.options,[](const auto& o){return o.unit.body.health>=1000;});
    CheckLightAsh(!hasSplit(noGuard,64,5000,true) && !hasSplit(noGuard,64,5000,false),"missing legal guards cannot synthesize either mixed core");
    auto adjacent=s;adjacent.rows=2;std::erase_if(adjacent.options,[](const auto& o){return o.row>=2;});
    CheckLightAsh(!hasSplit(adjacent,64,5000,true) && !hasSplit(adjacent,64,5000,false),"adjacent-only legal routes do not impersonate separated routes");
    auto pressure=s;
    for(int row:{0,4}) {Plant fire;fire.row=row;fire.health=1000;fire.dps=100;pressure.plants.push_back(fire);}
    const auto routed=BuildExperiencedFormations(pressure,64,5000);
    for(const auto& plan:routed) {
        if(PureRunner(pressure,plan,2,static_cast<int>(plan.size())))
            CheckLightAsh(pressure.options[plan.front().option].row==1,"pure comparisons prefer the low-pressure legal route");
        if(SplitRunner(pressure,plan,false) || SplitRunner(pressure,plan,true))
            for(const auto& a:plan) CheckLightAsh(pressure.options[a.option].row==1 || pressure.options[a.option].row==3,
                "two-route candidates prefer lower combined pressure while retaining separation");
    }

    auto small=s;small.rows=1;std::erase_if(small.options,[](const auto& o){return o.row!=0 || o.type>64003;});
    const auto hasPure=[](const Snapshot& field,int role,int count) {
        const auto candidates=BuildExperiencedFormations(field,64,5000);
        return std::any_of(candidates.begin(),candidates.end(),[&](const auto& p){return PureRunner(field,p,role,count);});
    };
    auto onlyRunner=small;onlyRunner.options={small.options[2]};
    const auto independent=BuildExperiencedFormations(onlyRunner,16,128);
    CheckLightAsh(independent.size()==3 && PureRunner(onlyRunner,independent[0],2,4)
        && PureRunner(onlyRunner,independent[1],2,8) && PureRunner(onlyRunner,independent[2],2,16),
        "legal light-only options still get complete comparisons when every old family is unavailable");
    for(float health:{999.0f,1000.0f,1001.0f}) {
        auto edge=small;edge.options[2].unit.body.health=health;
        CheckLightAsh(hasPure(edge,2,4)==(health<1000),"only units below the unchanged guard boundary receive the new light entry");
        if(health>=1000) CheckLightAsh(hasPure(edge,2,8),"guard-qualified speed still enters the original durable fast recipe");
    }
    const std::vector<void(*)(Unit&)> exclusions{
        [](Unit& u){u.body.economic=true;},[](Unit& u){u.engineer=true;},[](Unit& u){u.clock.present=true;},
        [](Unit& u){u.healer.present=true;},[](Unit& u){u.drum.enabled=true;},[](Unit& u){u.pressure=true;},
        [](Unit& u){u.floodMortar=true;},[](Unit& u){u.balloon.present=true;},[](Unit& u){u.digger.present=true;},
        [](Unit& u){u.vehicleCrush=true;},[](Unit& u){u.instantVehicleCrush=true;},[](Unit& u){u.goldenDrive.enabled=true;},
        [](Unit& u){u.catapult.present=true;},[](Unit& u){u.dance.leader=true;},[](Unit& u){u.dance.backup=true;},
        [](Unit& u){u.ritual.present=true;},[](Unit& u){u.sniper.enabled=true;},[](Unit& u){u.jack.present=true;},
        [](Unit& u){u.ladder.present=true;},[](Unit& u){u.jammer=true;},[](Unit& u){u.hijacker=true;},
        [](Unit& u){u.burst.range=200;},[](Unit& u){u.body.smashSeconds=2;},[](Unit& u){u.throwHealth=100;},
        [](Unit& u){u.headless=true;},[](Unit& u){u.biteDps=0;}
    };
    for(const auto exclude:exclusions) {
        auto specialist=small;auto fast=small.options[2];fast.type=64020;
        fast.unit.body.speed=fast.unit.upperForecastMoveSpeed=1000;exclude(fast.unit);specialist.options.push_back(fast);
        CheckLightAsh(hasPure(specialist,2,4),"faster specialist or non-attacker cannot steal the ordinary light runner role");
    }
    auto passive=small;passive.options[2].unit.grounding=true;passive.options[2].unit.insulator=true;
    passive.options[2].unit.body.canBeChilled=false;passive.options[2].unit.paralysisAllowed=false;
    CheckLightAsh(hasPure(passive,2,4),"passive control resistance alone does not exclude ordinary attackers");
    auto speed=small;auto alternate=small.options[2];alternate.type=64020;alternate.unit.body.speed=200;
    alternate.unit.upperForecastMoveSpeed=75;speed.options.push_back(alternate);
    CheckLightAsh(hasPure(speed,2,4),"upper forecast speed takes precedence over the opposite body-speed ordering");
    speed.options.back().unit.upperForecastMoveSpeed=90;
    CheckLightAsh(hasPure(speed,20,4) && !hasPure(speed,2,4),"only the fastest eligible light profile gets this narrow entry");
    for(int invalid=0;invalid<5;++invalid) {
        auto unavailable=small;auto& o=unavailable.options[2];
        if(invalid==0) o.unit.body.speed=o.unit.upperForecastMoveSpeed=0;
        if(invalid==1) o.cost=5001;
        if(invalid==2) o.device=0;
        if(invalid==3) o.row=1;
        if(invalid==4) o.unit.body.health=0;
        CheckLightAsh(!hasPure(unavailable,2,4),"stationary, unaffordable, device, other-row and dead options cannot fill the new role");
    }
    auto noLight=small;std::erase_if(noLight.options,[](const auto& o){return o.type==64002;});
    CheckLightAsh(hasPure(noLight,1,8) && !hasPure(noLight,1,4),"absence of a distinct light role does not duplicate old fast proposals");
    auto disabled=s;disabled.experiencedFormations=false;
    CheckLightAsh(BuildExperiencedFormations(disabled,64,5000).empty(),"experience ablation covers the new source too");
    std::cout<<"Runner coverage: "<<newCount<<" additions in "<<plans.size()<<" capped candidates, first light="<<firstLight
        <<", first durable="<<firstHeavy<<" (generation order only, not actual evaluation or victory)\n";
}
}

/** 同一小队随真实炮相位/独立在途弹改变存活，生成器本身既不伪造空窗也不拨款。 */
void RunColdStorageLightAshExperienceTests() {
    CheckRunnerCandidates();
    auto s=LightAshArena();
    const auto plans=BuildExperiencedFormations(s,64,500);
    std::set<size_t> accessSizes,rangedSizes;
    for(const auto& plan:plans) {
        int spent=0;
        for(const auto& a:plan) {
            CheckLightAsh(a.option>=0 && a.option<static_cast<int>(s.options.size()) && a.delay>=0 && a.delay<=60,
                "every action uses a legal current option and bounded delay");
            spent+=s.options[a.option].cost;
        }
        CheckLightAsh(spent<=500 && plan.size()<=64,"the entire front and followup share the supplied wallet and slots");
        if(IsLightAsh(s,plan,false)) accessSizes.insert(plan.size());
        if(IsLightAsh(s,plan,true)) rangedSizes.insert(plan.size());
    }
    CheckLightAsh(accessSizes==std::set<size_t>{8,16,32} && rangedSizes==std::set<size_t>{6,12,24},
        "two support-light families keep all three existing scales even without clocks or snipers");
    CheckLightAsh(plans.size()<=96,"light comparisons do not expand the existing seed cap");

    const auto exact=BuildExperiencedFormations(s,64,72);
    CheckLightAsh(std::any_of(exact.begin(),exact.end(),[&](const auto& plan){return IsLightAsh(s,plan,true) && plan.size()==6;}),
        "72 ice pays the complete two-shield four-catapult comparison");
    for(int budget:{71,72}) for(const auto& plan:BuildExperiencedFormations(s,5,budget))
        CheckLightAsh(!IsLightAsh(s,plan,true) && !IsLightAsh(s,plan,false),"missing one slot never trims off a partner or followup");
    for(const auto& plan:BuildExperiencedFormations(s,64,71))
        CheckLightAsh(!IsLightAsh(s,plan,true) && !IsLightAsh(s,plan,false),"missing one ice never spends projected future income");
    auto noCap=s;for(auto& o:noCap.options) o.unit.shieldedAshCap=0;
    for(const auto& plan:BuildExperiencedFormations(noCap,64,500)) {
        std::set<int> formerShieldRows;
        for(const auto& a:plan) if(noCap.options[a.option].type==57001) formerShieldRows.insert(noCap.options[a.option].row);
        CheckLightAsh(formerShieldRows.size()<2,"ordinary shield health cannot impersonate an ash cap");
    }
    auto oneRoute=s;
    oneRoute.options.erase(std::remove_if(oneRoute.options.begin(),oneRoute.options.end(),[](const auto& o){return o.row!=0;}),oneRoute.options.end());
    for(const auto& plan:BuildExperiencedFormations(oneRoute,64,500))
        CheckLightAsh(!IsLightAsh(oneRoute,plan,true) && !IsLightAsh(oneRoute,plan,false),"a missing legal partner is not synthesized");
    auto disabled=s;disabled.experiencedFormations=false;
    CheckLightAsh(BuildExperiencedFormations(disabled,64,500).empty(),"the existing experience ablation covers the added comparisons");

    Plant cannon;cannon.id=57010;cannon.x=300;cannon.health=100000;
    s.plants.push_back(cannon);
    Counter shot;shot.source=1;shot.plantID=cannon.id;shot.consumesPlant=false;
    shot.recharge=1000;shot.windup=4;shot.flightSeconds=2;
    shot.blast.x=900;shot.blast.damage=1800;shot.blast.reach.fill(-1);shot.blast.reach[0]=115;
    s.counters.push_back(shot);
    auto alternate=shot;alternate.blast.reach.fill(-1);alternate.blast.reach[4]=115;s.counters.push_back(alternate);
    auto cooling=s;for(auto& counter:cooling.counters) counter.blast.ready=1000;
    auto airborne=cooling;auto flight=shot;flight.source=2;flight.plantID=0;flight.blast.committed=true;flight.blast.ready=1;
    airborne.counters.push_back(flight);
    CheckLightAsh(SamePlans(BuildExperiencedFormations(s,64,500),BuildExperiencedFormations(cooling,64,500))
        && SamePlans(BuildExperiencedFormations(s,64,500),BuildExperiencedFormations(airborne,64,500)),
        "visible phase changes do not create a hardcoded purchase trigger or duplicate source-aim seeds");

    const auto chosen=std::find_if(exact.begin(),exact.end(),[&](const auto& plan){return IsLightAsh(s,plan,true);});
    CheckLightAsh(chosen!=exact.end(),"the affordable ranged comparison exists before evaluating its actual risk");
    // 固定位置只隔离反制承伤；此处不声称静止夹具证明了实战破墙能力。
    for(auto* world:{&s,&cooling,&airborne}) {
        world->houseX=-10000;
        for(auto& option:world->options) {
            option.unit.body.x=900;option.unit.body.speed=0;option.unit.biteDps=0;
            option.unit.catapult.damage=0;option.unit.catapult.ammunition=0;
        }
    }
    const auto readyResult=EvaluateCandidate(s,InitialWeights,*chosen);
    const auto coolingResult=EvaluateCandidate(cooling,InitialWeights,*chosen);
    const auto airborneResult=EvaluateCandidate(airborne,InitialWeights,*chosen);
    CheckLightAsh(readyResult.features[3]<coolingResult.features[3] && airborneResult.features[3]<coolingResult.features[3],
        "normal complete evaluation keeps ready cannon and independent airborne damage despite the lighter recipe");
    CheckLightAsh(std::abs(readyResult.features[5]-72)<.01f && std::abs(coolingResult.features[5]-72)<.01f
        && std::abs(airborneResult.features[5]-72)<.01f,
        "normal evaluation charges the exact full recipe once in every cannon world");
    CheckLightAsh(s.budget==500 && s.playerSun==0 && s.playerIce==0 && s.counters[0].blast.ready==0
        && cooling.counters[0].blast.ready==1000 && airborne.counters.back().blast.committed,
        "candidate generation and evaluation preserve wallets and public counter snapshots");
    std::cout<<"Light ash experience retains affordable complete cores and ordinary cannon risk evaluation\n";
}
