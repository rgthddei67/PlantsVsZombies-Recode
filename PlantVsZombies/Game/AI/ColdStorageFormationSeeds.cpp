#include "ColdStorageFormationSeeds.h"
#include "ColdStorageSearch.h"
#include <algorithm>
#include <array>
#include <numeric>

namespace ColdStorageSearch {
namespace {
constexpr float kGuardHealth = 1000; // 旧经营入口的护卫最低完整生命，伤害仍由统一预测结算
constexpr float kWorkerDelay = 4; // 旧版护卫先行、工人跟进的基准间隔，游戏秒
constexpr int kMaxSeeds = 96; // 单次搜索经验候选上限；与自由候选共享原时间预算
constexpr std::array<int,3> kScales{1,2,4}; // 同一编队的小、中、大规模起点，不是必买数量
constexpr std::array<int,17> kShapeOrder{0,8,14,1,15,9,12,2,10,13,3,11,16,4,5,6,7}; // 经营、快攻、射手掩护交错，紧预算也有机会比较协同
constexpr float kContactPadding=55; // 旧攻坚先行估计的前墙接触距离，像素；只生成时序，最终由统一预测验证
constexpr float kMaxLeadDelay=60; // 完整编队允许比较的最大快兵跟进延后，游戏秒；小队阶段另由搜索修复限制
constexpr float kShooterCoverCells=.5f; // 前排接敌时希望领先射手的距离，逻辑格；只是提案目标，不承诺能存活

bool Specialist(const Unit& u) {
    return u.engineer || u.clock.present || u.healer.present || u.drum.enabled || u.pressure;
}

/** 同一战术角色保留便宜护卫与厚重前锋两种选择，不依赖固定僵尸枚举。 */
struct Roles {
    int guard=-1, heavy=-1, worker=-1, engineer=-1, clock=-1, healer=-1, drum=-1;
    int breaker=-1, access=-1, air=-1, fast=-1, ladder=-1, ranged=-1, shooter=-1;
};

/** 先行前排用偏慢移速、快兵用偏快移速，不能假定同批出生会自然排成前后队。 */
float MoveSpeed(const Unit& u,bool fast) {
    const float sampled=fast ? u.upperForecastMoveSpeed : u.lowerForecastMoveSpeed;
    return sampled>0 ? sampled : u.body.speed;
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
        if(u.engineer) cheapest(r.engineer,id);
        if(u.clock.present) cheapest(r.clock,id);
        if(u.healer.present) cheapest(r.healer,id);
        if(u.drum.enabled) cheapest(r.drum,id);
        if(u.balloon.present) cheapest(r.air,id);
        if(u.ladder.present) cheapest(r.ladder,id);
        if(u.catapult.present) cheapest(r.ranged,id);
        if(u.pressure) cheapest(r.shooter,id); // 直射与投篮分开保留，不能让较便宜的车永久挤掉新射手。
        if(u.ladder.present || u.digger.present || u.catapult.present || u.jack.present) cheapest(r.access,id);
        if(u.body.smashSeconds>0 || u.vehicleCrush) {
            if(r.breaker<0 || u.body.health/o.cost>s.options[r.breaker].unit.body.health/s.options[r.breaker].cost)
                r.breaker=id;
        }
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
        if(plan.valid && !plan.actions.empty()) seeds.push_back(std::move(plan.actions));
        if(seeds.size()>=kMaxSeeds) return seeds;
    }
    return seeds;
}
}
