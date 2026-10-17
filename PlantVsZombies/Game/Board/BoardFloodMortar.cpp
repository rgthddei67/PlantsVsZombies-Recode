#include "Board.h"
#include "Game/AI/PlantDefenseMonteCarlo.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/FloodMortarRules.h"
#include "Game/Zombie/FloodMortarZombie.h"
#include "Game/AudioSystem.h"
#include "ParticleSystem/ParticleSystem.h"
#include "GameApp.h"
#include "Profiler.h"
#include <algorithm>
#include <set>

namespace {
constexpr int kTargetDecisionSpacingSteps=3; // 两次蓄洪推演间隔的固定逻辑步，覆盖一帧最多三步追帧
constexpr int kTargetScenarioBudget=96; // 每次开炮的候选与等待基线场景总预算，不扩大到其他决策族
constexpr int kTargetRolloutLimit=16; // 少量候选时的每候选抽样上限，避免单格目标反而重复数百次
}

bool Board::TryClaimFloodMortarTargetSlot(const Zombie& source) {
    if(source.IsMindControlled() || !GameAPP::GetInstance().mEnableMonteCarloAI) return true;
    if(mMonteCarloFloodMortarDecisionCooldownSteps>0) return false;
    int first=-1,next=-1;
    for(int id:mEntityRegistry.GetAllZombieIDs()) {
        const auto* gun=dynamic_cast<const FloodMortarZombie*>(mEntityRegistry.GetZombie(id));
        if(!gun || gun->IsMindControlled() || !gun->IsTargetDecisionReady()) continue;
        if(first<0 || id<first) first=id;
        if(id>mMonteCarloFloodMortarLastSourceID && (next<0 || id<next)) next=id;
    }
    // 只在名额空闲时找下一位，不能让已经再次装填完的低ID炮手饿死队尾。
    if(source.mZombieID!=(next>=0 ? next : first)) return false;
    mMonteCarloFloodMortarDecisionCooldownSteps=kTargetDecisionSpacingSteps;
    mMonteCarloFloodMortarLastSourceID=source.mZombieID;
    return true;
}

/** 在同一合法候选集合上选择最大损失；关闭模拟时不构建未来卡牌世界。 */
bool Board::PickFloodMortarTarget(const Zombie& source,Vector& target,int& row,int& column,bool& usedMonteCarlo,int& rollouts) {
    PROFILE_SCOPE("MC.FloodMortar.Total");
    usedMonteCarlo=false;rollouts=0;
    const int damage=FloodMortarRules::Damage(static_cast<int>(GetRainIntensity()));
    const float direction=source.IsMindControlled()?1.0f:-1.0f;
    std::vector<PlantDefenseMonteCarlo::Candidate> candidates;
    int best=-1;float bestValue=-1;
    for(int r=std::max(0,source.mRow-1);r<=std::min(mRows-1,source.mRow+1);++r)
        for(int c=0;c<mColumns;++c) {
            const Vector center=GetCellCenterPosition(r,c);
            const float distance=(center.x-source.GetPosition().x)*direction;
            if(distance<0 || distance>FloodMortarRules::RangeCells*CELL_COLLIDER_SIZE_X || MineBlocksSegment(source.GetPosition(),center)) continue;
            // 与精英小丑一致，瞄准实际有目标的格，不能用空格绕过保护伞。
            if(!source.IsMindControlled()) {
                bool centerOccupied=false;
                ForEachActivePlantInCell(r,c,[&](Plant& p){if(!p.IsSquished())centerOccupied=true;});
                if(!centerOccupied) continue;
            }
            float value=0;bool occupied=false;
            if(source.IsMindControlled()) {
                for(int id:mEntityRegistry.GetAllZombieIDs()) if(auto* z=mEntityRegistry.GetZombie(id);
                    z && z->IsActive() && !z->IsDying() && !z->IsMindControlled() && std::abs(z->mRow-r)<=1
                    && std::abs(z->GetPosition().x-center.x)<=CELL_COLLIDER_SIZE_X*1.5f) {
                    occupied=true;value+=std::min(damage,z->mBodyHealth);
                }
            } else {
                std::set<int> recipients;
                for(int id:mEntityRegistry.GetAllPlantIDs()) if(auto* p=mEntityRegistry.GetPlant(id);
                    p && p->IsActive() && !p->IsSquished() && std::abs(p->mRow-r)<=1 && std::abs(p->mColumn-c)<=1
                    && !MineBlocksSegment(center,p->GetPosition())) {
                    occupied=true;
                    auto* shell=FindPumpkinAreaProtector(*p,0);
                    auto* receiver=shell?shell:p;
                    if(recipients.insert(receiver->mPlantID).second)
                        value+=std::min(receiver->mPlantHealth,damage*(shell?FloodMortarRules::PumpkinMultiplier:1));
                    const auto& profile=GameDataManager::GetInstance().GetPlantSimulationProfile(p->mPlantType);
                    if(!shell) value+=p->GetSimulationAttackDps(profile.attackDps)*p->GetAttackSpeedMultiplier()
                        *(FloodMortarRules::SlowSeconds-p->GetFloodSlowRemaining())*(1-FloodMortarRules::AttackMultiplier);
                }
                // 保护伞拦截整枚炸弹，不把壳体耐久或减速虚构成收益。
                if(FindAirborneThreatProtector(r,c)) value=0;
            }
            if(!occupied) continue;
            candidates.push_back({r,c,center.x,center.y});
            if(IsMineBackground()) for(int id:mEntityRegistry.GetAllPlantIDs())
                if(auto* p=mEntityRegistry.GetPlant(id);p && MineBlocksSegment(center,p->GetPosition())) candidates.back().blockedPlantIds.push_back(id);
            if(value>bestValue) {bestValue=value;best=static_cast<int>(candidates.size())-1;}
        }
    if(best<0) return false;
    if(!source.IsMindControlled() && GameAPP::GetInstance().mEnableMonteCarloAI) {
        PlantDefenseMonteCarlo::Snapshot snapshot;
        bool captured=false;
        {
            PROFILE_SCOPE("MC.FloodMortar.Snapshot");
            captured=BuildMonteCarloCombatSnapshot(snapshot,false,false);
        }
        if(captured) {
            snapshot.candidates=candidates;
            PlantDefenseMonteCarlo::Config config;
            // 每个样本同时推进等待基线，预算包含这一份；保留全部合法落点和原16秒时域。
            const int samples=std::clamp(kTargetScenarioBudget/(1+static_cast<int>(candidates.size())),1,kTargetRolloutLimit);
            ConfigureMonteCarloPlantImpactConfig(config,samples,16,damage,0);
            config.floodMortar=true;config.pumpkinProtectionCellRadius=0;config.impactDelay=FloodMortarRules::FlightSeconds;
            config.pumpkinImpactDamageMultiplier=FloodMortarRules::PumpkinMultiplier;
            PlantDefenseMonteCarlo::Result result;
            {
                PROFILE_SCOPE("MC.FloodMortar.Rollouts");
                result=PlantDefenseMonteCarlo::ChooseTarget(snapshot,config,
                    static_cast<unsigned>(mBoardFrame)^static_cast<unsigned>(source.mZombieID)*16777619u);
            }
            if(result.candidateIndex>=0 && result.candidateIndex<static_cast<int>(candidates.size())) {
                best=result.candidateIndex;usedMonteCarlo=true;rollouts=result.rolloutCount;
            }
        }
    }
    const auto& chosen=candidates[best];row=chosen.row;column=chosen.column;target=Vector(chosen.x,chosen.y);
    return true;
}

/** 在格位上冻结范围与保护壳；同格南瓜同时拦截伤害与减速。 */
void Board::ApplyFloodMortarImpact(const Vector& target,int row,int damage,bool charmed) {
    int column=0;float nearest=1e9f;
    for(int c=0;c<mColumns;++c) {const float d=std::abs(GetCellCenterPosition(row,c).x-target.x);if(d<nearest){nearest=d;column=c;}}
    if(charmed) {
        for(int id:mEntityRegistry.GetAllZombieIDs()) if(auto* z=mEntityRegistry.GetZombie(id);
            z && z->IsActive() && !z->IsDying() && !z->IsMindControlled() && std::abs(z->mRow-row)<=1
            && std::abs(z->GetPosition().x-target.x)<=CELL_COLLIDER_SIZE_X*1.5f && !MineBlocksSegment(target,z->GetPosition()))
            z->TakeDamage(damage,DamageSource::ZOMBIE);
    } else {
        auto inArea=[&](const Plant& p){return std::abs(p.mRow-row)<=1 && std::abs(p.mColumn-column)<=1 && !MineBlocksSegment(target,p.GetPosition());};
        // 在扣壳前冻结保护资格；这炮即使破壳，也不能把减速补给里面的植物。
        for(int id:mEntityRegistry.GetAllPlantIDs()) if(auto* p=mEntityRegistry.GetPlant(id);
            p && p->IsActive() && inArea(*p) && !FindPumpkinAreaProtector(*p,0)) p->ApplyFloodSlow();
        ApplyPumpkinProtectedZombieAreaDamage(damage,FloodMortarRules::PumpkinMultiplier,inArea,0);
    }
    if(g_particleSystem) g_particleSystem->EmitEffect("FloodMortarExplosion",target+Vector(0,-18));
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CHERRYBOMB,.6f);
}
