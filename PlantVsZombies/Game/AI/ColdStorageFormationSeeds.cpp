#include "Game/Zombie/FogFerryRules.h"
#include "Game/Zombie/CatapultRules.h"
#include "ColdStorageFormationSeeds.h"
#include "ColdStorageSearch.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <utility>

namespace ColdStorageSearch {
namespace {
constexpr float kGuardHealth = 1000; // 旧经营入口的护卫最低完整生命，伤害仍由统一预测结算
constexpr float kWorkerDelay = 4; // 旧版护卫先行、工人跟进的基准间隔，游戏秒
constexpr int kMaxSeeds = 96; // 单次搜索经验候选上限；与自由候选共享原时间预算
constexpr std::array<int,3> kScales{1,2,4}; // 同一编队的小、中、大规模起点，不是必买数量
constexpr std::array<int,31> kShapeOrder{0,27,24,17,28,8,25,20,29,14,1,26,30,21,18,15,22,9,12,23,2,10,13,3,11,19,16,4,5,6,7}; // 经营、快攻、射手与炮手掩护交错，紧预算也有机会比较协同
constexpr float kContactPadding=55; // 旧攻坚先行估计的前墙接触距离，像素；只生成时序，最终由统一预测验证
constexpr float kMaxLeadDelay=60; // 完整编队允许比较的最大快兵跟进延后，游戏秒；小队阶段另由搜索修复限制
constexpr float kShooterCoverCells=.5f; // 前排接敌时希望领先射手的距离，逻辑格；只是提案目标，不承诺能存活
constexpr std::array<float,3> kGuardCatapultGaps{8,16,0}; // 多路投篮护卫队的相邻队出生间隔，游戏秒；含同步对照
constexpr size_t kPressureCoverSeeds=24; // 护卫覆盖候选的有界数量，沿用原搜索预算
constexpr float kPressureCoverSpacing=.6f; // 相邻护卫目标距离，按保护半径的比例估算出生间隔

bool Specialist(const Unit& u) {
    return u.engineer || u.clock.present || u.healer.present || u.drum.enabled || u.pressure || u.floodMortar || u.ferry.present;
}

/** 同一战术角色保留便宜护卫与厚重前锋两种选择，不依赖固定僵尸枚举。 */
struct Roles {
    int guard=-1, heavy=-1, worker=-1, engineer=-1, clock=-1, healer=-1, drum=-1;
    int breaker=-1, access=-1, air=-1, fast=-1, ladder=-1, ranged=-1, shooter=-1, mortar=-1;
    int ferry=-1, ferryVehicle=-1; // 运输者与可运输车辆独立保留，避免巨人挤掉车队候选
    int assaultRunner=-1; // 低于旧护卫血量门槛的独立普通地面突击角色，不替换fast掩护语义
    int ashGuard=-1, deploymentSniper=-1; // 单次灰烬封顶与落种反制是独立能力，不能按总血量混进普通护卫
    int pressureGuard=-1, armoredBiter=-1, dancer=-1; // 保护光环、装甲啃食者和召唤者独立保留，避免血价比掩盖协同
};

/** 先行前排用偏慢移速、快兵用偏快移速，不能假定同批出生会自然排成前后队。 */
float MoveSpeed(const Unit& u,bool fast) {
    const float sampled=fast ? u.upperForecastMoveSpeed : u.lowerForecastMoveSpeed;
    return sampled>0 ? sampled : u.body.speed;
}

/** 只补旧护卫门槛漏掉的轻装啃食者；已投影的停步/变阶段专职能力不争抢这个窄入口。
 * Unit 尚未保存普通撑杆/报纸未来换阶段；出生速度排序不是越障、落地或破盾后的行为证明。
 */
bool AssaultRunner(const Unit& u) {
    return u.body.health<kGuardHealth && u.biteDps>0 && MoveSpeed(u,true)>0 && !u.headless
        && !u.body.economic && !Specialist(u) && !u.balloon.present && !u.digger.present
        && !u.vehicleCrush && !u.instantVehicleCrush && !u.goldenDrive.enabled && !u.catapult.present
        && !u.dance.leader && !u.dance.backup && !u.ritual.present && !u.sniper.enabled
        && !u.jack.present && !u.ladder.present && !u.jammer && !u.hijacker
        && u.burst.range<=0 && u.body.smashSeconds<=0 && u.throwHealth<=0;
}

/** 沿用旧版破墙后快兵再抵达的思路，只估计候选延后，不承诺墙一定已被拆掉。 */
float RushDelay(const Snapshot& s,int row,int opener,int runner) {
    if(opener<0 || runner<0) return 0;
    float front=s.houseX; bool wall=false;
    for(const auto& p:s.plants) if(p.health>0 && p.row==row && p.edible) {front=std::max(front,p.x);wall=true;}
    if(!wall) return 0;
    const auto& a=s.options[opener].unit; const auto& b=s.options[runner].unit;
    const float opening=std::max(0.0f,a.body.x-front-kContactPadding)/std::max(1.0f,MoveSpeed(a,false))
        +a.body.smashSeconds+(a.ladder.present ? a.ladder.placementSeconds : 0);
    const float arrival=std::max(0.0f,b.body.x-front-kContactPadding)/std::max(1.0f,MoveSpeed(b,true));
    return std::clamp(opening-arrival,0.0f,kMaxLeadDelay);
}

/** 前排按慢端、射手按快端估计接敌时差，避免射手在接敌前反超；实际生存与收益仍由推演裁决。 */
float ShooterCoverDelay(const Snapshot& s,int row,int front,int shooter) {
    float contact=s.houseX;
    for(const auto& p:s.plants) if(p.health>0 && p.edible && p.row==row) contact=std::max(contact,p.x+kContactPadding);
    const auto& a=s.options[front].unit; const auto& b=s.options[shooter].unit;
    const float frontArrival=std::max(0.0f,a.body.x-contact)/std::max(1.0f,MoveSpeed(a,false));
    const float rearArrival=std::max(0.0f,b.body.x-contact-kShooterCoverCells*s.cellWidth)/std::max(1.0f,MoveSpeed(b,true));
    return std::clamp(frontArrival-rearArrival,0.0f,kMaxLeadDelay);
}

/** 炮手会在六格射程边缘停步，按该位置的前后间距排队，不照搬直射兵接敌时差。 */
float MortarCoverDelay(const Snapshot& s,int row,int front,int mortar) {
    float target=s.houseX;bool found=false;
    for(const auto& p:s.plants) if(p.health>0 && std::abs(p.row-row)<=1) {target=std::max(target,p.x);found=true;}
    if(!found) return 0;
    const auto& a=s.options[front].unit;const auto& b=s.options[mortar].unit;
    const float firingX=std::min(b.body.x,target+FloodMortarRules::RangeCells*s.cellWidth-b.body.blastAnchorOffset);
    const float frontArrival=std::max(0.0f,a.body.x-firingX+kShooterCoverCells*s.cellWidth)/std::max(1.0f,MoveSpeed(a,false));
    const float mortarArrival=std::max(0.0f,b.body.x-firingX)/std::max(1.0f,MoveSpeed(b,true));
    return std::clamp(frontArrival-mortarArrival,0.0f,kMaxLeadDelay);
}

/** 快兵预计接触本行防线时，炮手的首发已经落地；无本行防线时不人为延迟快兵。 */
float MortarRushDelay(const Snapshot& s,int row,int mortar,int runner) {
    float front=s.houseX,aim=s.houseX;bool ownRow=false;
    for(const auto& p:s.plants) if(p.health>0) {
        if(std::abs(p.row-row)<=1) aim=std::max(aim,p.x);
        if(p.row==row) {front=std::max(front,p.x+kContactPadding);ownRow=true;}
    }
    if(!ownRow) return 0;
    const auto& gun=s.options[mortar].unit;const auto& fast=s.options[runner].unit;
    const float firingX=aim+FloodMortarRules::RangeCells*s.cellWidth-gun.body.blastAnchorOffset;
    const float approach=std::max(0.0f,gun.body.x-firingX)/std::max(1.0f,MoveSpeed(gun,false));
    const float impact=std::max(gun.floodReload,approach)+FloodMortarRules::FlightSeconds;
    const float arrival=std::max(0.0f,fast.body.x-front)/std::max(1.0f,MoveSpeed(fast,true));
    return std::clamp(impact-arrival,0.0f,kMaxLeadDelay);
}

/** 能力与真实价格只排序提案；不能用护卫血池替代穿透/溅射和灰烬的正式推演。 */
Roles FindRoles(const Snapshot& s, int row, int budget) {
    Roles r;
    const auto cheapest=[&](int& choice,int i) {
        if(choice<0 || s.options[i].cost<s.options[choice].cost) choice=i;
    };
    for(size_t i=0;i<s.options.size();++i) {
        const auto& o=s.options[i]; const auto& u=o.unit;
        if(o.row!=row || o.device>=0 || o.cost<=0 || o.cost>budget || u.body.health<=0) continue;
        const int id=static_cast<int>(i);
        if(u.body.economic) { cheapest(r.worker,id); continue; }
        if(u.pressureGuard && u.helmHealth>0) cheapest(r.pressureGuard,id);
        if(u.dance.leader) cheapest(r.dancer,id);
        if(!u.pressureGuard && (u.helmHealth>0 || u.shieldHealth>0) && u.biteDps>0
            && MoveSpeed(u,true)>0 && !u.headless && !Specialist(u) && !u.balloon.present
            && !u.digger.present && !u.vehicleCrush && !u.catapult.present && !u.dance.leader
            && !u.dance.backup && !u.ladder.present && !u.sniper.enabled
            && (r.armoredBiter<0 || u.body.health/o.cost>s.options[r.armoredBiter].unit.body.health/s.options[r.armoredBiter].cost))
            r.armoredBiter=id;
        if(u.sniper.enabled) cheapest(r.deploymentSniper,id);
        if(u.shieldHealth>0 && u.shieldedAshCap>0 && (r.ashGuard<0
            || u.shieldHealth/u.shieldedAshCap/o.cost > s.options[r.ashGuard].unit.shieldHealth
                /s.options[r.ashGuard].unit.shieldedAshCap/s.options[r.ashGuard].cost)) r.ashGuard=id;
        if(u.ferry.present) cheapest(r.ferry,id);
        if(u.vehicleCrush || u.catapult.present) cheapest(r.ferryVehicle,id);
        if(u.engineer) cheapest(r.engineer,id);
        if(u.clock.present) cheapest(r.clock,id);
        if(u.healer.present) cheapest(r.healer,id);
        if(u.drum.enabled) cheapest(r.drum,id);
        if(u.balloon.present) cheapest(r.air,id);
        if(u.ladder.present) cheapest(r.ladder,id);
        if(u.catapult.present) cheapest(r.ranged,id);
        if(u.floodMortar) cheapest(r.mortar,id); // 独立保留炮手，不能被便宜投篮车或气压射手挤掉。
        if(u.pressure) cheapest(r.shooter,id); // 直射与投篮分开保留，不能让较便宜的车永久挤掉新射手。
        if(u.ladder.present || u.digger.present || u.catapult.present || u.jack.present) cheapest(r.access,id);
        if(u.body.smashSeconds>0 || u.vehicleCrush) {
            if(r.breaker<0 || u.body.health/o.cost>s.options[r.breaker].unit.body.health/s.options[r.breaker].cost)
                r.breaker=id;
        }
        if(AssaultRunner(u) && (r.assaultRunner<0
            || MoveSpeed(u,true)>MoveSpeed(s.options[r.assaultRunner].unit,true))) r.assaultRunner=id;
        if(Specialist(u) || u.balloon.present || u.digger.present || u.body.health<kGuardHealth) continue;
        if(!u.vehicleCrush && !u.catapult.present && MoveSpeed(u,true)>0
            && (r.fast<0 || MoveSpeed(u,true)>MoveSpeed(s.options[r.fast].unit,true))) r.fast=id;
        if(r.guard<0 || u.body.health/o.cost>s.options[r.guard].unit.body.health/s.options[r.guard].cost) r.guard=id;
        if(r.heavy<0 || u.body.health>s.options[r.heavy].unit.body.health) r.heavy=id;
    }
    return r;
}

/** 按完整配方检查钱包和名额；不能先截掉后排/保护者，再把残缺队伍当作经营协同。 */
struct Recipe {
    const Snapshot& state;
    int limit, remaining;
    bool valid=true;
    std::vector<Action> actions;

    void Add(int option,int count,float delay) {
        if(option<0 || count<=0) return;
        const int cost=state.options[option].cost*count;
        if(cost>remaining || actions.size()+count>static_cast<size_t>(limit)) {valid=false;return;}
        remaining-=cost;
        for(int i=0;i<count;++i) actions.push_back({option,delay});
    }
};

/** 给保护光环单独生成基础推进、召唤、经营、直射、炮击、越墙及分路混编。
 * 最多十九案，沿用三档规模；只取当前合法角色，保护范围与实际收益仍由统一推演判断。
 */
std::vector<std::vector<Action>> PressureGuardAssaults(const Snapshot& s,int limit,int budget,
    const std::vector<int>& rows,const std::array<Roles,6>& roles) {
    std::vector<std::vector<Action>> seeds;
    for(int scale:kScales) for(int shape=0;shape<6;++shape) for(int row:rows) {
        const auto& r=roles[row];
        const int runner=r.assaultRunner>=0 ? r.assaultRunner : r.fast;
        if(r.pressureGuard<0 || (shape<3 && r.armoredBiter<0)
            || (shape==1 && r.dancer<0) || (shape==2 && r.worker<0)
            || (shape==3 && r.shooter<0) || (shape==4 && r.mortar<0)
            || (shape==5 && (r.ladder<0 || runner<0 || runner==r.pressureGuard || runner==r.ladder))) continue;
        Recipe plan{s,limit,budget};
        plan.Add(r.pressureGuard,scale,0);
        // 快兵延后抵达，降低进场即脱离光环的机会；不是保证全程受保护的简化规则。
        if(shape<3) plan.Add(r.armoredBiter,(shape==2 ? 2 : 3)*scale,ShooterCoverDelay(s,row,r.pressureGuard,r.armoredBiter));
        if(shape==1) plan.Add(r.dancer,scale,ShooterCoverDelay(s,row,r.pressureGuard,r.dancer));
        if(shape==2) plan.Add(r.worker,scale,ShooterCoverDelay(s,row,r.pressureGuard,r.worker));
        if(shape==3) plan.Add(r.shooter,2*scale,ShooterCoverDelay(s,row,r.pressureGuard,r.shooter));
        if(shape==4) plan.Add(r.mortar,2*scale,MortarCoverDelay(s,row,r.pressureGuard,r.mortar));
        if(shape==5) {
            const float ladderDelay=ShooterCoverDelay(s,row,r.pressureGuard,r.ladder);
            plan.Add(r.ladder,scale,ladderDelay);
            plan.Add(runner,3*scale,std::min(kMaxLeadDelay,ladderDelay+RushDelay(s,row,r.ladder,runner)));
        }
        if(plan.valid) {seeds.push_back(std::move(plan.actions));break;}
    }
    // 双路仅保留一份小队，按低火力顺序选择至少隔一行的完整路线，不膨胀为所有行组合。
    for(int row:rows) for(int partner:rows) {
        if(std::abs(partner-row)<2) continue;
        const auto& a=roles[row];const auto& b=roles[partner];
        if(a.pressureGuard<0 || a.armoredBiter<0 || b.pressureGuard<0 || b.armoredBiter<0) continue;
        Recipe plan{s,limit,budget};
        for(int route:{row,partner}) {
            const auto& r=roles[route];
            plan.Add(r.pressureGuard,1,0);
            plan.Add(r.armoredBiter,3,ShooterCoverDelay(s,route,r.pressureGuard,r.armoredBiter));
        }
        if(plan.valid) {seeds.push_back(std::move(plan.actions));return seeds;}
    }
    return seeds;
}

/** 给投篮后排比较多路错峰掩护；每路完整付费，不能截掉护卫后仍当作受保护车队。
 * 最多六案，分别覆盖全部合法行与隔行路线；不保证光环跟随，移动/控制/灰烬仍由推演判断。
 */
std::vector<std::vector<Action>> GuardCatapultAssaults(const Snapshot& s,int limit,int budget,
    const std::array<Roles,6>& roles) {
    std::vector<int> routes;
    for(int row=0;row<s.rows && row<6;++row)
        if(roles[row].pressureGuard>=0 && roles[row].ranged>=0) routes.push_back(row);
    std::vector<std::vector<Action>> seeds;
    for(bool separated:{false,true}) {
        std::vector<int> selected;
        for(int row:routes) if(!separated || selected.empty() || row-selected.back()>=2) selected.push_back(row);
        if(selected.size()<2 || (separated && selected==routes)) continue;
        for(float gap:kGuardCatapultGaps) {
            if(gap*(selected.size()-1)>kMaxLeadDelay) continue;
            Recipe plan{s,limit,budget};
            for(size_t i=0;i<selected.size();++i) {
                const auto& r=roles[selected[i]];
                const float delay=gap*static_cast<float>(i);
                // 同队同步出生保留最初的保护机会；不用近战接敌时间让投篮车错过整个反制空窗。
                plan.Add(r.pressureGuard,1,delay);plan.Add(r.ranged,2,delay);
            }
            if(plan.valid) seeds.push_back(std::move(plan.actions));
        }
    }
    return seeds;
}

/** 最多五份轻快对照：三个纯突击规模，加一份双路突击及其带工人版本。
 * 路线至少隔一行；先比较低火力路线，同压再取较远路线。现金/容量不足整案拒绝。
 */
std::vector<std::vector<Action>> RunnerAssaults(const Snapshot& s,int limit,int budget,
    const std::vector<int>& rows,const std::array<Roles,6>& roles,const std::array<float,6>& pressure) {
    std::vector<std::pair<int,int>> pairs;
    for(int row:rows) for(int partner:rows)
        if(partner-row>=2 && roles[row].guard>=0 && roles[partner].guard>=0
            && roles[row].assaultRunner>=0 && roles[partner].assaultRunner>=0)
            pairs.emplace_back(row,partner);
    std::stable_sort(pairs.begin(),pairs.end(),[&](const auto& a,const auto& b) {
        const float first=pressure[a.first]+pressure[a.second],second=pressure[b.first]+pressure[b.second];
        return first!=second ? first<second : a.second-a.first>b.second-b.first;
    });
    std::vector<std::vector<Action>> seeds;
    for(int scale:kScales) {
        for(int row:rows) {
            const int runner=roles[row].assaultRunner;
            if(runner<0 || runner==roles[row].fast) continue;
            Recipe plan{s,limit,budget};plan.Add(runner,4*scale,0);
            if(plan.valid) {seeds.push_back(std::move(plan.actions));break;}
        }
        if(scale!=1) continue; // 双路只给最小完整配方，不把新增规模扩散成另一套大搜索。
        for(bool economy:{false,true}) for(const auto& [row,partner]:pairs) {
            if(economy && (roles[row].worker<0 || roles[partner].worker<0)) continue;
            Recipe plan{s,limit,budget};
            for(int route:{row,partner}) {
                const auto& r=roles[route];
                const float offset=route==row ? 0 : kWorkerDelay*.5f;
                plan.Add(r.guard,1,offset);
                if(economy) plan.Add(r.worker,2,offset+kWorkerDelay);
                // 只是相对出生提案，不能据此声称护卫挡住穿透/溅射或轻兵一定利用炮击空窗。
                plan.Add(r.assaultRunner,3,offset+kWorkerDelay*1.5f);
            }
            if(plan.valid) {seeds.push_back(std::move(plan.actions));break;}
        }
    }
    return seeds;
}

/** 抗灰配方的轻装对照：保留两路完整攻坚核心，不把钟匠/热狙当作购买前置条件。
 * 最多两类乘三个已有规模，包含后队的全额费用；炮数和预计收益不在这里授予采购资格。
 */
std::vector<std::vector<Action>> LightAshAssaults(const Snapshot& s,int limit,int budget,
    const std::vector<int>& rows,const std::array<Roles,6>& roles,const std::array<float,6>& pressure) {
    std::vector<std::pair<int,int>> pairs;
    for(int row:rows) for(int partner:rows)
        if(row<partner && roles[row].ashGuard>=0 && roles[partner].ashGuard>=0)
            pairs.emplace_back(row,partner);
    // 只排序提案路线；行距不是免疫爆炸的证明，实际射程、就绪炮与在途弹仍由完整推演处理。
    std::stable_sort(pairs.begin(),pairs.end(),[&](const auto& a,const auto& b) {
        const int first=a.second-a.first,second=b.second-b.first;
        return first!=second ? first>second : pressure[a.first]+pressure[a.second]<pressure[b.first]+pressure[b.second];
    });
    std::vector<std::vector<Action>> seeds;
    for(int scale:kScales) for(bool ranged:{false,true}) for(const auto& [row,partner]:pairs) {
        Recipe plan{s,limit,budget};
        for(int route:{row,partner}) {
            const auto& r=roles[route];
            if(ranged) {
                if(r.ranged<0) {plan.valid=false;break;}
                plan.Add(r.ashGuard,scale,0);
                plan.Add(r.ranged,2*scale,ShooterCoverDelay(s,route,r.ashGuard,r.ranged));
            } else {
                if(r.ladder<0 || r.fast<0 || r.fast==r.ladder || r.fast==r.ashGuard) {plan.valid=false;break;}
                plan.Add(r.ashGuard,scale,0);plan.Add(r.ladder,scale,1);
                plan.Add(r.fast,2*scale,std::min(kMaxLeadDelay,1+RushDelay(s,route,r.ladder,r.fast)));
            }
        }
        // 每类/规模只取一份可支付双路案，不为镜像路线或相同炮的多个爆点复制候选。
        if(plan.valid && !plan.actions.empty()) {seeds.push_back(std::move(plan.actions));break;}
    }
    return seeds;
}
}

std::vector<std::vector<Action>> BuildPressureCoverFormations(const Snapshot& s,int actionLimit,int budget) {
    std::vector<std::vector<Action>> plans;
    if(!s.experiencedFormations || !s.netEconomy || actionLimit<2 || budget<=0) return plans;
    for(int route=0; route<s.rows && plans.size()<kPressureCoverSeeds; ++route) {
        // 跨次搜索轮转路线；当前已付/活体仍由完整快照参与后续结算，不承诺新兵永久受保护。
        const int row=(route+s.searchRound)%s.rows;
        const auto roles=FindRoles(s,row,budget);
        if(roles.pressureGuard<0) continue;
        const auto& guard=s.options[roles.pressureGuard].unit;
        const float speed=MoveSpeed(guard,false);
        if(speed<=0) continue;
        const float origin=guard.body.x+guard.body.blastAnchorOffset;
        const float radius=s.cellWidth*PressureGuardRules::RadiusCells;
        const float gap=std::clamp(radius*kPressureCoverSpacing/speed,2.0f,8.0f);
        float front=s.houseX; bool target=false;
        for(const auto& p:s.plants) if(p.health>0 && p.row==row) {front=std::max(front,p.x+kContactPadding);target=true;}
        if(!target) continue;
        for(const int partner:{roles.mortar,roles.worker,roles.ranged}) {
            if(partner<0) continue;
            const auto& rear=s.options[partner].unit;
            float meeting=front;
            if(rear.floodMortar) {
                float aim=s.houseX;
                for(const auto& p:s.plants) if(p.health>0 && std::abs(p.row-row)<=1) aim=std::max(aim,p.x);
                meeting=std::min(origin,aim+FloodMortarRules::RangeCells*s.cellWidth);
            } else if(rear.catapult.present) meeting=s.gridLeft+s.columns*s.cellWidth-CatapultRules::kShootStartInsideBoard;
            const float rearOrigin=rear.body.x+rear.body.blastAnchorOffset;
            const float lead=std::max(0.0f,(origin-meeting)/speed
                -std::max(0.0f,rearOrigin-meeting)/std::max(1.0f,MoveSpeed(rear,true)));
            const bool liveCover=std::any_of(s.current.begin(),s.current.end(),[&](const Unit& u) {
                return u.pressureGuard && u.helmHealth>0 && u.body.health>0 && u.body.spawnAt<=0
                    && !u.headless && u.body.stopped<=0 && std::abs(u.body.row-row)<=1
                    && std::abs(u.body.x+u.body.blastAnchorOffset-meeting)<=radius;
            });
            if(liveCover) {
                Recipe reinforce{s,actionLimit,budget};reinforce.Add(partner,2,0);
                if(reinforce.valid) plans.push_back(std::move(reinforce.actions));
                if(plans.size()>=kPressureCoverSeeds) return plans;
            }
            for(int count:{3,6}) {
                Recipe plan{s,actionLimit,budget};
                for(int i=0;i<count;++i) plan.Add(roles.pressureGuard,1,i*gap);
                // 慢护卫先铺开；后续护卫仍可在后排入场后继续补齐，避免第一批全走离炮手。
                const float tailAt=std::clamp(std::max(lead,gap),0.0f,30.0f);
                plan.Add(partner,count==3 ? 2 : 4,tailAt);
                if(plan.valid) plans.push_back(std::move(plan.actions));
                if(plans.size()>=kPressureCoverSeeds) return plans;
            }
        }
        Recipe screen{s,actionLimit,budget};
        for(int i=0;i<3;++i) screen.Add(roles.pressureGuard,1,i*gap);
        if(screen.valid && plans.size()<kPressureCoverSeeds) plans.push_back(std::move(screen.actions));
    }
    return plans;
}

std::vector<std::vector<Action>> BuildExperiencedFormations(const Snapshot& s,int actionLimit,int budget) {
    std::vector<std::vector<Action>> seeds;
    if(!s.experiencedFormations || !s.netEconomy || actionLimit<2 || budget<=0) return seeds;
    std::vector<int> rows(std::clamp(s.rows,0,6));
    std::iota(rows.begin(),rows.end(),0);
    std::array<float,6> pressure{};
    for(const auto& p:s.plants) if(p.health>0 && p.dps>0)
        for(int row:rows) if(std::abs(p.row-row)<=p.rowRadius) pressure[row]+=p.dps;
    // 先试低火力路线，但其余路线仍有同样配方。经验不授予某一路固定采购资格。
    std::stable_sort(rows.begin(),rows.end(),[&](int a,int b){return pressure[a]<pressure[b];});
    std::array<Roles,6> roles{};
    for(int row:rows) roles[row]=FindRoles(s,row,budget);
    auto lightAssaults=LightAshAssaults(s,actionLimit,budget,rows,roles,pressure);
    auto runnerAssaults=RunnerAssaults(s,actionLimit,budget,rows,roles,pressure);
    auto pressureAssaults=PressureGuardAssaults(s,actionLimit,budget,rows,roles);
    auto catapultAssaults=GuardCatapultAssaults(s,actionLimit,budget,roles);
    auto coverAssaults=BuildPressureCoverFormations(s,actionLimit,budget);
    std::vector<std::vector<Action>> extraAssaults;
    for(size_t i=0;i<std::max({lightAssaults.size(),runnerAssaults.size(),pressureAssaults.size(),catapultAssaults.size(),coverAssaults.size()});++i) {
        if(i<catapultAssaults.size()) extraAssaults.push_back(std::move(catapultAssaults[i]));
        if(i<pressureAssaults.size()) extraAssaults.push_back(std::move(pressureAssaults[i]));
        if(i<coverAssaults.size()) extraAssaults.push_back(std::move(coverAssaults[i]));
        if(i<runnerAssaults.size()) extraAssaults.push_back(std::move(runnerAssaults[i]));
        if(i<lightAssaults.size()) extraAssaults.push_back(std::move(lightAssaults[i]));
    }
    size_t extraIndex=0;
    const auto appendExtra=[&] {
        while(extraIndex<extraAssaults.size()) {
            auto& candidate=extraAssaults[extraIndex++];
            const bool duplicate=std::any_of(seeds.begin(),seeds.end(),[&](const auto& existing) {
                return existing.size()==candidate.size() && std::equal(existing.begin(),existing.end(),candidate.begin(),
                    [](const Action& a,const Action& b){return a.option==b.option && a.delay==b.delay;});
            });
            if(!duplicate) {seeds.push_back(std::move(candidate));break;}
        }
    };
    for(int scale:kScales) for(int row:rows) for(int shape:kShapeOrder) {
        const auto& r=roles[row];
        const int front=scale==1 ? r.guard : r.heavy;
        Recipe plan{s,actionLimit,budget};
        const auto support=[&](float clockDelay) {
            // 经营案的钟匠不能比需要保护的工人先进入准备循环；规模变大可交错两个来源。
            plan.Add(r.clock,1,clockDelay);
            if(scale>1) plan.Add(r.clock,1,clockDelay+kWorkerDelay);
            plan.Add(r.healer,1,2); plan.Add(r.drum,1,2);
        };
        switch(shape) {
        case 27: case 28: case 29: case 30: {
            const int cargo=shape==28 ? r.ferryVehicle : shape==29 ? r.shooter : r.heavy;
            if(r.ferry<0 || cargo<0) continue;
            const auto& ferry=s.options[r.ferry].unit;const auto& passenger=s.options[cargo].unit;
            // 按逻辑原点在最右格会合；慢兵先出发，不能假定同批出生能赶上第一次施法。
            const float rendezvous=s.gridLeft+(s.columns-.25f)*s.cellWidth;
            const float carrierSpeed=std::max(1.0f,MoveSpeed(ferry,false));
            const float cargoSpeed=std::max(1.0f,MoveSpeed(passenger,false));
            const float ready=std::max(FogFerryRules::FirstWait,
                (ferry.body.x+ferry.body.blastAnchorOffset-rendezvous)/carrierSpeed);
            const float cargoArrival=std::max(0.0f,(passenger.body.x+passenger.body.blastAnchorOffset-rendezvous)/cargoSpeed);
            const float ferryDelay=std::max(0.0f,cargoArrival-ready);
            const float cargoDelay=std::max(0.0f,ready-cargoArrival);
            plan.Add(r.ferry,1,ferryDelay);plan.Add(cargo,2*scale,cargoDelay);
            if(shape==30) {
                const bool fog=s.weatherStation ? s.station.controls[1].value>0 : s.fogLeftColumn<s.columns;
                const float cooldown=fog ? FogFerryRules::Cooldown : FogFerryRules::ClearCooldown;
                const float nextX=rendezvous-carrierSpeed*cooldown*(fog ? WeatherStationRules::FogMoveMultiplier : 1);
                const float arrival=std::max(0.0f,(passenger.body.x+passenger.body.blastAnchorOffset-nextX)/cargoSpeed);
                const float nextDelay=ferryDelay+ready+FogFerryRules::Windup+cooldown-arrival;
                if(nextDelay<cargoDelay || nextDelay>kMaxLeadDelay) continue;
                plan.Add(cargo,2*scale,nextDelay);
            }
            break;
        }
        case 24: case 25: case 26: { // 抗灰前队分两路承压，后援仍靠完整预测判断；护盾不能替邻居挡爆炸。
            if(r.ashGuard<0 || rows.size()<2) continue;
            const auto far=std::max_element(rows.begin(),rows.end(),[&](int a,int b) {
                const int da=roles[a].ashGuard<0 ? -1 : std::abs(a-row);
                const int db=roles[b].ashGuard<0 ? -1 : std::abs(b-row);
                return da<db;
            });
            const int partner=*far;
            if(partner==row || roles[partner].ashGuard<0) continue;
            const auto& other=roles[partner];
            const int nearRow=row+(partner>row ? 1 : -1),nearPartner=partner+(partner>row ? -1 : 1);
            if(roles[nearRow].clock<0 || roles[nearPartner].clock<0) continue;
            if(shape==24) {
                if(r.ladder<0 || r.fast<0 || r.drum<0) continue;
                plan.Add(r.ashGuard,4*scale,0);plan.Add(other.ashGuard,2*scale,0);
                plan.Add(roles[nearRow].clock,1,6);plan.Add(roles[nearPartner].clock,1,6);
                plan.Add(r.drum,2,6);plan.Add(r.ladder,2*scale,10);
                plan.Add(r.fast,4*scale,std::min(kMaxLeadDelay,10+RushDelay(s,row,r.ladder,r.fast)));
            } else if(shape==25) {
                if(r.worker<0 || other.worker<0 || r.deploymentSniper<0) continue;
                plan.Add(r.ashGuard,4*scale,0);plan.Add(other.ashGuard,2*scale,0);
                plan.Add(roles[nearRow].clock,1,6);plan.Add(r.deploymentSniper,2*scale,6);
                plan.Add(roles[nearPartner].clock,1,9);plan.Add(r.worker,2*scale,9);
                plan.Add(r.ashGuard,2*scale,12);plan.Add(other.worker,scale,12);
            } else {
                if(r.ranged<0 || other.ranged<0 || r.deploymentSniper<0) continue;
                plan.Add(r.ashGuard,3*scale,0);plan.Add(other.ashGuard,2*scale,0);
                plan.Add(r.ranged,3*scale,6);plan.Add(roles[nearRow].clock,1,6);
                plan.Add(other.ranged,scale,10);plan.Add(roles[nearPartner].clock,1,10);
                plan.Add(r.deploymentSniper,2*scale,10);
            }
            break;
        }
        case 0: // 历史经营：便宜肉盾先行，后排工人稍后跟进。
            if(front<0 || r.worker<0) continue;
            plan.Add(front,scale,0); plan.Add(r.worker,3*scale,kWorkerDelay);
            break;
        case 1: // 灰烬仍可用时，直接比较完整保护组合，而不等随机逐个补出支援。
            if(front<0 || r.worker<0 || (r.engineer<0 && r.clock<0 && r.healer<0)) continue;
            plan.Add(front,scale,0); support(kWorkerDelay);
            plan.Add(r.engineer,scale,kWorkerDelay); plan.Add(r.worker,3*scale,kWorkerDelay);
            break;
        case 2: // 同样保护投入，比较更大产能；统一预测会拒绝保护覆盖不足的工人。
            if(front<0 || r.worker<0 || r.engineer<0) continue;
            plan.Add(front,2*scale,0); support(kWorkerDelay+2);
            plan.Add(r.engineer,2*scale,2); plan.Add(r.worker,4*scale,kWorkerDelay+2);
            break;
        case 3: case 4: // 历史攻坚核心＋支援；有/无后续工人直接竞争，允许纯进攻胜出。
            if(front<0 || (r.breaker<0 && r.access<0)) continue;
            plan.Add(front,4*scale,0);
            plan.Add(r.breaker>=0 ? r.breaker : r.access,2*scale,1);
            support(shape==3 ? kWorkerDelay+4 : 1);
            if(shape==3) {
                if(r.worker<0) continue;
                plan.Add(r.engineer,scale,4); plan.Add(r.worker,2*scale,kWorkerDelay+4);
            }
            break;
        case 5: { // 已有军团继续推进时，不重新买整队才肯补工人。
            if(r.worker<0) continue;
            const bool escort=std::any_of(s.current.begin(),s.current.end(),[&](const Unit& u) {
                return u.body.row==row && u.body.health>0 && !u.body.economic
                    && (u.engineer || u.clock.present || u.body.health>=kGuardHealth);
            });
            if(!escort) continue;
            plan.Add(r.engineer,scale,0); plan.Add(r.worker,2*scale,2);
            break;
        }
        case 6: // 保留灰烬空窗下的大批生产，不因引入编队而取消自由经济机会。
            if(r.worker<0) continue;
            plan.Add(r.worker,std::min(actionLimit,8*scale),0);
            break;
        case 7: // 绕行压力与地面前线/后排经营；飞行单位不被假定为地面肉盾。
            if(r.air<0) continue;
            plan.Add(r.air,4*scale,0); plan.Add(front,2*scale,0);
            plan.Add(r.worker,scale,kWorkerDelay+4);
            break;
        case 8: // 旧版成批快兵突击：保留不带工人或额外支援的纯进攻对照。
            if(r.fast<0) continue;
            plan.Add(r.fast,8*scale,0);
            break;
        case 9: { // Git中的“前两只巨人＋后续橄榄”推广到当前合法破障/快兵画像。
            if(r.breaker<0 || r.fast<0 || r.breaker==r.fast) continue;
            const float delay=RushDelay(s,row,r.breaker,r.fast);
            plan.Add(r.breaker,2*scale,0); plan.Add(r.fast,6*scale,delay);
            support(delay);
            break;
        }
        case 10: // 携梯提供共享通路，快兵随后利用；仍检查未搭成、拆梯与来源死亡的真实结果。
            if(r.ladder<0 || r.fast<0 || r.ladder==r.fast) continue;
            plan.Add(front,scale,0); plan.Add(r.ladder,scale,1);
            plan.Add(r.fast,6*scale,std::min(kMaxLeadDelay,1+RushDelay(s,row,r.ladder,r.fast)));
            support(2);
            break;
        case 11: // 旧支援入口：前线掩护投篮拆后排，并以治疗/钟匠续航，不绑定经济购买。
            if(front<0 || r.ranged<0) continue;
            plan.Add(front,2*scale,0); plan.Add(r.ranged,3*scale,2); support(2);
            break;
        case 12: case 13: { // 真群伤挡不住时，不能只试同批工人；让后续产能在清场后继续入场。
            if(r.worker<0) continue;
            const float gap=IceProduction::Interval*(shape==12 ? 1.0f : 1.5f);
            const int count=std::min({actionLimit,8*scale,static_cast<int>(kMaxLeadDelay/gap)+1});
            for(int i=0;i<count;++i) plan.Add(r.worker,1,i*gap);
            break;
        }
        case 14: // 肉盾先行，持续直射随后；独立于工人、治疗和付费支援的轻量进攻组合。
            if(front<0 || r.shooter<0) continue;
            plan.Add(front,scale,0);
            plan.Add(r.shooter,2*scale,ShooterCoverDelay(s,row,front,r.shooter));
            break;
        case 15: // 快兵接敌、射手跟火；快兵本来更快时可同批出发，不硬等固定秒数。
            if(r.fast<0 || r.shooter<0) continue;
            plan.Add(r.fast,2*scale,0);
            plan.Add(r.shooter,2*scale,ShooterCoverDelay(s,row,r.fast,r.shooter));
            break;
        case 20: { // 肉盾承压、炮击削弱密集阵地、气压射手持续直射；不依赖工人卡位。
            if(front<0 || r.mortar<0 || r.shooter<0) continue;
            plan.Add(front,scale,0);
            plan.Add(r.mortar,scale,MortarCoverDelay(s,row,front,r.mortar));
            plan.Add(r.shooter,scale,ShooterCoverDelay(s,row,front,r.shooter));
            break;
        }
        case 21: // 快兵在首轮水压窗口接敌，不假定战鼓能缩短炮手的固定装填时钟。
            if(r.mortar<0 || r.fast<0) continue;
            plan.Add(r.mortar,scale,0);
            plan.Add(r.fast,3*scale,MortarRushDelay(s,row,r.mortar,r.fast));
            break;
        case 22: { // 两门炮错开半个预计装填周期，比较更均匀的减速覆盖和较少过量伤害。
            if(front<0 || r.mortar<0) continue;
            auto weather=s.station.controls[WeatherStationRules::RAIN];
            WeatherStationRules::Advance(weather,FloodMortarRules::FirstReload+FloodMortarRules::FlightSeconds);
            const float gap=FloodMortarRules::Reload(weather.value)*.5f;
            const float delay=MortarCoverDelay(s,row,front,r.mortar);
            plan.Add(front,2*scale,0);
            plan.Add(r.mortar,scale,delay);plan.Add(r.mortar,scale,delay+gap);
            break;
        }
        case 23: { // 两条相隔一行的合法路线各自带护卫，扩大炮击覆盖而非只堆同一行。
            if(front<0 || r.mortar<0) continue;
            const int partner=row+2<s.rows?row+2:row-2;
            if(partner<0 || partner>=static_cast<int>(roles.size())) continue;
            const auto& other=roles[partner];const int otherFront=scale==1?other.guard:other.heavy;
            if(otherFront<0 || other.mortar<0) continue;
            plan.Add(front,scale,0);plan.Add(r.mortar,scale,MortarCoverDelay(s,row,front,r.mortar));
            plan.Add(otherFront,scale,0);plan.Add(other.mortar,scale,MortarCoverDelay(s,partner,otherFront,other.mortar));
            break;
        }
        case 17: // 最小护卫炮组，现金较少时也能完整比较一盾一炮。
            if(front<0 || r.mortar<0) continue;
            plan.Add(front,scale,0);
            plan.Add(r.mortar,scale,MortarCoverDelay(s,row,front,r.mortar));
            break;
        case 18: { // 厚前排与炮组续航；支援仍各自按真实能力与价格接受完整推演。
            if(front<0 || r.mortar<0 || (r.healer<0 && r.clock<0 && r.drum<0)) continue;
            const float delay=MortarCoverDelay(s,row,front,r.mortar);
            plan.Add(front,2*scale,0);plan.Add(r.mortar,2*scale,delay);support(delay);
            break;
        }
        case 19: { // 已有活体肉盾时补炮，尚未进场或无头残兵不充当现成掩护。
            if(r.mortar<0) continue;
            const float rear=s.options[r.mortar].unit.body.x-kShooterCoverCells*s.cellWidth;
            const bool covered=std::any_of(s.current.begin(),s.current.end(),[&](const Unit& u) {
                return u.body.row==row && u.body.spawnAt<=0 && u.body.x<=rear
                    && u.body.health>=kGuardHealth && !u.body.economic && !Specialist(u)
                    && !u.balloon.present && !u.digger.present
                    && u.body.health-u.helmHealth-u.shieldHealth>u.temporalStopHealth;
            });
            if(!covered) continue;
            plan.Add(r.mortar,2*scale,0);
            break;
        }
        case 16: { // 已有活体前排时直接补火力；付费未到场或掉头残兵不冒充可靠掩护。
            if(r.shooter<0) continue;
            const float rear=s.options[r.shooter].unit.body.x-kShooterCoverCells*s.cellWidth;
            const bool covered=std::any_of(s.current.begin(),s.current.end(),[&](const Unit& u) {
                return u.body.row==row && u.body.spawnAt<=0 && u.body.x<=rear
                    && u.body.health>=kGuardHealth && !u.body.economic && !Specialist(u)
                    && !u.balloon.present && !u.digger.present
                    && u.body.health-u.helmHealth-u.shieldHealth>u.temporalStopHealth;
            });
            if(!covered) continue;
            plan.Add(r.shooter,2*scale,0);
            break;
        }
        }
        if(plan.valid && !plan.actions.empty()) {
            seeds.push_back(std::move(plan.actions));
            // 独立混编家族与旧经验交错，不挤掉旧首案；96案满时末尾旧案可能失去本轮槽位。
            if(seeds.size()<kMaxSeeds) appendExtra();
        }
        if(seeds.size()>=kMaxSeeds) return seeds;
    }
    // 卡池缺少旧经验必需的支援时，独立合法的混编核心仍有机会接受完整评价。
    while(seeds.size()<kMaxSeeds && extraIndex<extraAssaults.size()) appendExtra();
    return seeds;
}
}
