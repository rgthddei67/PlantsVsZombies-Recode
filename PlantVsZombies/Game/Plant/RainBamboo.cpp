#include "RainBamboo.h"
#include "GameApp.h"
#include "Game/Board/Board.h"
#include "Game/Bullet/Bullet.h"
#include <cmath>

void RainBamboo::SetupPlant() { mPlantHealth=mPlantMaxHealth=RainBambooRules::Health; }

/** 保留归一化充能进度；雨势替代通用雨天攻击倍率，领域和词条仍正常生效。 */
void RainBamboo::PlantUpdate() {
    if (!mBoard || mIsPreview) return;
    const float weather=GetWeatherActionSpeedMultiplier();
    const float rate=weather>0 ? GetAttackSpeedMultiplier()/weather : 0;
    mCharge=std::min(1.0f,mCharge+DeltaTime::GetDeltaTime()*rate
        /RainBambooRules::Interval(static_cast<int>(mBoard->GetRainIntensity())));
    if (mCharge<1) return;
    bool target=false;
    const Vector muzzle=GetVisualPosition()+mAnimator->GetTrackPosition("head")+Vector(65,26);
    mBoard->mEntityRegistry.ForEachZombieInRow(mRow,[&](Zombie* z) {
        if(z && z->IsActive() && !z->IsDying() && !z->IsMindControlled()
            && CanAcquireZombie(z) && z->GetPosition().x>=muzzle.x
            && z->GetPosition().x<SCENE_WIDTH && !mBoard->MineBlocksSegment(muzzle,z->GetPosition())
            && mBoard->CanPlantAcquireZombie(this,z)) target=true;
    });
    if(!target) return;
    auto* bullet=mBoard->CreatePlantBullet(BulletType::BULLET_RAIN_BAMBOO,mRow,muzzle,mPlantType);
    if(!bullet) return;
    bullet->SetVelocityX(RainBambooRules::Speed);
    mCharge=0; ++mShots;
    if(g_particleSystem) g_particleSystem->EmitEffect("RainBambooMuzzle",muzzle);
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT,.25f);
}

void RainBamboo::SaveExtraData(nlohmann::json& j) const { j["charge"]=mCharge;j["shots"]=mShots; }
/** 只恢复实际充能和计数，不能补发离膛竹矛。 */
void RainBamboo::LoadExtraData(const nlohmann::json& j) {
    const float charge=j.value("charge",0.0f);
    mCharge=std::isfinite(charge) ? std::clamp(charge,0.0f,1.0f) : 0;
    mShots=std::max(0,j.value("shots",0));
}
