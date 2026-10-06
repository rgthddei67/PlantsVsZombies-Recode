#include "PressureShooterZombie.h"
#include "Game/Board/Board.h"
#include "Game/AudioSystem.h"
#include "ResourceKeys.h"
#include <algorithm>
#include <cmath>

/** 继承普通身体事件，挂接独立气压枪头和气罐，注册主人确认的四个发射帧。 */
void PressureShooterZombie::SetupZombie() {
    Zombie::SetupZombie();
    mBodyHealth=mBodyMaxHealth=PressureShooterRules::Health;
    // 仍保留正常掉头阈值和流血，只在 HeadDrop 中省略掉头粒子。
    for (const char* track : {"anim_head1","anim_head2","anim_hair","anim_tongue"})
        mAnimator->SetTrackVisible(track,false);
    mGun=std::make_shared<Animator>(ResourceManager::GetInstance().GetReanimation("PressureShooterHead"));
    mGun->SetLocalPosition(0,0);
    mGun->SetFlipX(true,18);
    mGun->PlayTrack("anim_head_idle",1,0);
    mAnimator->AttachAnimator("anim_head1",mGun);
    for(const int frame:PressureShooterRules::Frames) mGun->AddFrameEvent(frame,[this](){Shoot();},true);
    const auto* tank=ResourceManager::GetInstance().GetTexture("IMAGE_PRESSURE_TANK",false);
    mAnimator->SetTrackFollowerImage("Zombie_body","pressureTank",tank,30,3,.65f,.65f,false,true,true);
    mAnimator->SetTrackFollowerVisible("Zombie_body","pressureTank",true);
    for(const char* track:{"Zombie_body","Zombie_outerarm_upper","Zombie_outerarm_lower","Zombie_innerarm_upper","Zombie_innerarm_lower"})
        if(mAnimator->HasTrack(track)) mAnimator->SetTrackColor(track,SDL_Color{155,195,210,255});
}

/** 返回当前身体头轨对应的枪口世界像素位置，阵营反转只改变横向偏移。 */
Vector PressureShooterZombie::GetMuzzlePosition() const {
    return GetRenderedTrackWorldPosition("anim_head1")
        +Vector(IsMindControlled() ? -PressureShooterRules::MuzzleOffset : PressureShooterRules::MuzzleOffset,14);
}

void PressureShooterZombie::Shoot() {
    if(mIsPreview || !mBoard || !IsActive() || mIsDead || mIsDying || !mHasHead
        || IsImmobilized() || mTangleKelpPlantID!=NULL_PLANT_ID || mBurstShot>=4) return;
    auto* bullet=mBoard->CreateBullet(BulletType::BULLET_PRESSURE,mRow,GetMuzzlePosition());
    if(!bullet) return;
    bullet->SetVelocityX(IsMindControlled() ? PressureShooterRules::ProjectileSpeed : -PressureShooterRules::ProjectileSpeed);
    ++mBurstShot; ++mShotsFired;
    mLastShotBoardFrame=mBoard->mBoardFrame;
    if(mBurstShot==4) mReloadRemaining=PressureShooterRules::Reload;
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT,.25f);
}

/** 按枪头的有效行动倍率加压，冷却完成即开始下一轮，与啃食及索敌无关。 */
void PressureShooterZombie::ZombieUpdate(float) {
    if(mIsPreview || !mGun || !IsActive() || mIsDead || mIsDying || !mHasHead) return;
    if(IsImmobilized() || IsGarlicRedirectPaused() || mTangleKelpPlantID!=NULL_PLANT_ID) return;
    // 四发动画与轮间加压使用同一行动倍率；最后出膛的本帧不重复消费冷却。
    if(mBurstShot>=4 && mLastShotBoardFrame!=mBoard->mBoardFrame) mReloadRemaining=std::max(0.0f,mReloadRemaining-DeltaTime::GetDeltaTime()*mAnimator->GetExtraSpeedMultiplier());
    mGun->SetFlipX(!IsMindControlled(),18);
    if(mBurstShot>=4 && mReloadRemaining<=0) {
        mBurstShot=0;
        mGun->PlayTrackOnce("anim_shooting","anim_head_idle",PressureShooterRules::ClipSpeed,0,1,0);
    }
}

void PressureShooterZombie::HeadDrop() {
    if(mGun) for(const auto& track:*mGun->GetReanimation()->mTracks) mGun->SetTrackVisible(track.mTrackName,false);
    // 不生成普通头或机枪头掉落粒子；基类调用者负责 mHasHead 与流血。
    mBurstShot=4;
}

/** 读档和时间回溯可恢复有头状态；每次公共装备同步都按当前状态重建枪头显隐。 */
void PressureShooterZombie::ZombieItemUpdate() const {
    Zombie::ZombieItemUpdate();
    if(!mAnimator || !mGun) return;
    for(const char* track:{"anim_head1","anim_head2","anim_hair","anim_tongue"}) mAnimator->SetTrackVisible(track,false);
    for(const auto& track:*mGun->GetReanimation()->mTracks) mGun->SetTrackVisible(track.mTrackName,mHasHead);
    mGun->SetFlipX(!IsMindControlled(),18);
}

void PressureShooterZombie::OnTemporalCoreStateRestored() {
    Zombie::OnTemporalCoreStateRestored();
    ZombieItemUpdate(); // 普通骨架复原后重新隐藏原头；不重新发弹或制造掉头粒子。
}

void PressureShooterZombie::SaveExtraData(nlohmann::json& j) const {
    j["reloadRemaining"]=mReloadRemaining; j["burstShot"]=mBurstShot; j["shotsFired"]=mShotsFired;
    if(mGun) { j["gunFrame"]=mGun->GetCurrentFrame(); j["gunTrack"]=mGun->GetCurrentTrackName(); }
}

/** 恢复半轮射击的播放头和已发数量，避免读档补发已经提交的气弹。 */
void PressureShooterZombie::LoadExtraData(const nlohmann::json& j) {
    const float remaining=j.value("reloadRemaining",PressureShooterRules::Reload);
    mReloadRemaining=std::isfinite(remaining) ? std::clamp(remaining,0.0f,PressureShooterRules::Reload) : PressureShooterRules::Reload;
    mBurstShot=std::clamp(j.value("burstShot",4),0,4); mShotsFired=std::max(0,j.value("shotsFired",0));
    if(mGun && j.value("gunTrack",std::string())=="anim_shooting") {
        mGun->PlayTrackOnce("anim_shooting","anim_head_idle",PressureShooterRules::ClipSpeed,0,1,0);
        mGun->SetCurrentFrame(std::clamp(j.value("gunFrame",50.0f),50.0f,88.0f));
    }
    if(!mHasHead) HeadDrop();
}
