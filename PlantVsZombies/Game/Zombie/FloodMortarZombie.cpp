#include "FloodMortarZombie.h"
#include "Game/Board/Board.h"
#include "Game/Plant/Plant.h"
#include "Game/Bullet/Bullet.h"
#include "ResourceManager.h"
#include <cmath>

/** 复用普通僵尸身体及原有断肢帧，只给躯干挂接储水炮。 */
void FloodMortarZombie::SetupZombie() {
    Zombie::SetupZombie();
    mBodyHealth=mBodyMaxHealth=FloodMortarRules::Health;
    mAnimator->SetTrackFollowerImage("Zombie_body","flood_mortar",
        ResourceManager::GetInstance().GetTexture("IMAGE_FLOOD_MORTAR_PACK",false),-31,8,.72f,.72f,true);
    mAnimator->SetTrackFollowerVisible("Zombie_body","flood_mortar",true);
}

bool FloodMortarZombie::HasTarget() const {
    if(!mBoard || !HasHead() || IsDying()) return false;
    const float sign=IsMindControlled() ? 1.0f : -1.0f;
    auto eligible=[&](int row,float x) {
        const float distance=(x-GetPosition().x)*sign;
        return std::abs(row-mRow)<=1 && distance>=0 && distance<=FloodMortarRules::RangeCells*CELL_COLLIDER_SIZE_X;
    };
    if(IsMindControlled()) {
        for(int id:mBoard->mEntityRegistry.GetAllZombieIDs()) if(auto* z=mBoard->mEntityRegistry.GetZombie(id);
            z && z->IsActive() && !z->IsDying() && !z->IsMindControlled() && eligible(z->mRow,z->GetPosition().x) && !mBoard->MineBlocksSegment(GetPosition(),z->GetPosition())) return true;
    } else {
        for(int id:mBoard->mEntityRegistry.GetAllPlantIDs()) if(auto* p=mBoard->mEntityRegistry.GetPlant(id);
            p && p->IsActive() && !p->IsSquished() && eligible(p->mRow,mBoard->GetCellCenterPosition(p->mRow,p->mColumn).x) && !mBoard->MineBlocksSegment(GetPosition(),p->GetPosition())) return true;
    }
    return false;
}

void FloodMortarZombie::ZombieMove(float dt,Transform* transform) {
    if(HasTarget()) {
        if(mAnimator->GetCurrentTrackName()!="anim_idle") mAnimator->PlayTrack("anim_idle");
    } else {
        if(mAnimator->GetCurrentTrackName()=="anim_idle") PlayWalkAnimation();
        Zombie::ZombieMove(dt,transform);
    }
}

/** 首装与后续装填按有效游戏秒推进；雨势只在发射边沿锁定下一周期及伤害。 */
void FloodMortarZombie::ZombieUpdate(float) {
    if(!mBoard || mIsPreview || !HasHead() || IsDying() || IsImmobilized() || IsTangleKelpTarget()) return;
    const float dt=DeltaTime::GetDeltaTime();
    mReload=std::max(0.0f,mReload-dt);
    mCheckRemaining=std::max(0.0f,mCheckRemaining-dt);
    if(mReload>0 || mCheckRemaining>0 || !HasTarget()) return;
    mCheckRemaining=.25f;
    Vector target;
    mUsedMonteCarlo=false;mRollouts=0;
    if(!mBoard->PickFloodMortarTarget(*this,target,mTargetRow,mTargetColumn,mUsedMonteCarlo,mRollouts)) return;
    const auto rain=static_cast<int>(mBoard->GetRainIntensity());
    const Vector start=GetVisualPosition()+mAnimator->GetTrackPosition("Zombie_body")+Vector(IsMindControlled()?14:-14,20);
    auto* bullet=mBoard->CreateBullet(BulletType::BULLET_FLOOD_MORTAR,mTargetRow,start);
    if(!bullet) return;
    bullet->SetBulletDamage(FloodMortarRules::Damage(rain));
    bullet->SetFloodCharmed(IsMindControlled());
    bullet->RestoreLobbedMotion(start,target,0,FloodMortarRules::FlightSeconds,125,false,false,false);
    mReload=FloodMortarRules::Reload(rain);++mShots;
    if(g_particleSystem) g_particleSystem->EmitEffect("RainBambooMuzzle",start);
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT,.6f);
}

void FloodMortarZombie::SaveExtraData(nlohmann::json& j) const {
    j["reload"]=mReload;j["shots"]=mShots;j["targetRow"]=mTargetRow;j["targetColumn"]=mTargetColumn;
}
/** 装填和落点诊断独立保存；在途水弹由弹丸存档恢复，不重放开炮。 */
void FloodMortarZombie::LoadExtraData(const nlohmann::json& j) {
    const float value=j.value("reload",FloodMortarRules::FirstReload);
    mReload=std::isfinite(value)?std::clamp(value,0.0f,10.0f):FloodMortarRules::FirstReload;
    mShots=std::max(0,j.value("shots",0));mTargetRow=j.value("targetRow",-1);mTargetColumn=j.value("targetColumn",-1);
    mCheckRemaining=0;mUsedMonteCarlo=false;mRollouts=0;
}
