#pragma once
#include "Zombie.h"
#include "PressureShooterRules.h"

/** 气压射手：独立枪头四连发，行走与啃食均不停止射击。 */
class PressureShooterZombie final : public Zombie {
public:
    using Zombie::Zombie;
    void ZombieUpdate(float scaledTime) override;
    void HeadDrop() override;
    void ZombieItemUpdate() const override;
    void OnTemporalCoreStateRestored() override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    int GetShotsFired() const { return mShotsFired; }
    int GetBurstShot() const { return mBurstShot; }
    float GetReloadRemaining() const { return mReloadRemaining; }
    float GetGunFrame() const { return mGun ? mGun->GetCurrentFrame() : 0; }
    Vector GetMuzzlePosition() const;
protected:
    void SetupZombie() override;
private:
    /** 帧事件仅提交一颗气弹；第四发开始加压，不受是否有植物影响。 */
    void Shoot();
    std::shared_ptr<Animator> mGun;
    float mReloadRemaining = PressureShooterRules::Reload;
    int mBurstShot = 4, mShotsFired = 0;
    int mLastShotBoardFrame = -1;
};
