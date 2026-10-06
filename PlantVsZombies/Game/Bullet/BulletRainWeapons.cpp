#include "Bullet.h"
#include "GameApp.h"
#include "Game/Board/Board.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/RainBambooRules.h"
#include "Game/Zombie/FloodMortarRules.h"
#include <algorithm>

void Bullet::RestoreBambooHitIDs(const std::vector<int>& ids) {
    mBambooHitIDs.clear();
    for(int id:ids) if(id>=0 && std::find(mBambooHitIDs.begin(),mBambooHitIDs.end(),id)==mBambooHitIDs.end()) {
        mBambooHitIDs.push_back(id);if(mBambooHitIDs.size()==5) break;
    }
}

/** 对本步扫掠区间按真实碰撞前沿排序，防止高速漏判或重叠目标打乱伤害衰减。 */
void Bullet::UpdateRainBamboo(float dt) {
    if(!mBoard || dt<=0) return;
    if(mBambooHitIDs.size()>=5) {Die();return;}
    const Vector from=GetPosition();const float next=from.x+mVelocityX*dt;
    if(HitIceWallIfNeeded(from.x,next)) return;
    std::vector<std::pair<float,int>> hits;
    mBoard->mEntityRegistry.ForEachZombieInRow(mRow,[&](Zombie* z) {
        if(!z || !z->IsActive() || z->IsDying() || z->IsMindControlled() || !z->CanBeTargetedByProjectile(false)
            || std::find(mBambooHitIDs.begin(),mBambooHitIDs.end(),z->mZombieID)!=mBambooHitIDs.end()) return;
        const auto* collider=z->GetColliderComponent();if(!collider)return;
        const auto bounds=collider->GetBoundingBox();
        if(bounds.x>next || bounds.x+bounds.w<from.x || mBoard->MineBlocksSegment(from,z->GetPosition())) return;
        hits.emplace_back(std::max(from.x,bounds.x),z->mZombieID);
    });
    std::sort(hits.begin(),hits.end());
    for(const auto& [x,id]:hits) if(auto* z=mBoard->mEntityRegistry.GetZombie(id)) {
        const int damage=RainBambooRules::Damage[mBambooHitIDs.size()];
        mBambooHitIDs.push_back(id);
        z->TakeProjectileDamage(damage,DamageSource::PLANT,mVelocityX,false,false,false,mPlantDamageOrigin);
        if(g_particleSystem) g_particleSystem->EmitEffect(mBambooHitIDs.size()==5?"RainBambooFinish":"RainBambooHit",Vector(x,from.y));
        PlayStandardImpactSound(z);
        if(mBambooHitIDs.size()==5) {Die();return;}
    }
    if(mBoard->MineBlocksSegment(from,Vector(next,from.y))) {Die();return;}
    GetTransform()->SetPosition(Vector(next,from.y));UpdateShadowLayout(GetPosition());
    mWaterTrailRemaining-=dt;
    if(mWaterTrailRemaining<=0) {
        mWaterTrailRemaining=.065f;
        if(g_particleSystem) g_particleSystem->EmitEffect("RainBambooTrail",GetPosition());
    }
    if(next>SCENE_WIDTH+80) Die();
}

/** 水弹保留整段落点预警；落地前询问保护伞，拦截后取消整枚水弹的伤害与减速。 */
void Bullet::UpdateFloodMortar(float dt) {
    if(!mBoard || dt<=0 || mTrajectory.duration<=0) return;
    if(mCollider) mCollider->mEnabled=false;
    mTrajectory.elapsed=std::min(mTrajectory.duration,mTrajectory.elapsed+dt);
    const float t=mTrajectory.elapsed/mTrajectory.duration;
    const Vector pos(mTrajectory.start.x+(mTrajectory.target.x-mTrajectory.start.x)*t,
        mTrajectory.start.y+(mTrajectory.target.y-mTrajectory.start.y)*t-4*mTrajectory.apexHeight*t*(1-t));
    GetTransform()->SetPosition(pos);UpdateShadowLayout(pos);
    int column=0;float nearest=1e9f;
    for(int c=0;c<mBoard->mColumns;++c) {
        const float d=std::abs(mBoard->GetCellCenterPosition(mRow,c).x-mTrajectory.target.x);
        if(d<nearest) {nearest=d;column=c;}
    }
    if(!mFloodCharmed && t>=.8f) if(auto* p=mBoard->FindAirborneThreatProtector(mRow,column)) {
        const auto defense=p->ActivateAirborneDefense();
        if(defense==AirborneDefenseState::REFLECTING) {
            if(g_particleSystem) g_particleSystem->EmitEffect("RainBambooHit",pos);
            Die();return;
        }
        if(defense==AirborneDefenseState::ACTIVATING && t>=1) return;
    }
    mWaterTrailRemaining-=dt;
    if(mWaterTrailRemaining<=0) {
        mWaterTrailRemaining=.07f;
        if(g_particleSystem) g_particleSystem->EmitEffect("RainBambooTrail",pos);
    }
    if(t>=1) {
        mBoard->ApplyFloodMortarImpact(mTrajectory.target,mRow,mDamage,mFloodCharmed);
        Die();
    }
}
