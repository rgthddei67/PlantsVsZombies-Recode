#pragma once
#include "Zombie.h"
#include "FloodMortarRules.h"

/** 蓄洪僵尸走入射程后停步装填，已发射水弹独立于来源存活。 */
class FloodMortarZombie final : public Zombie {
public:
    using Zombie::Zombie;
    void ZombieUpdate(float scaledTime) override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetReloadRemaining() const { return mReload; }
    int GetShotCount() const { return mShots; }
    int GetTargetRow() const { return mTargetRow; }
    int GetTargetColumn() const { return mTargetColumn; }
    int GetRollouts() const { return mRollouts; }
    bool UsedMonteCarlo() const { return mUsedMonteCarlo; }
protected:
    void SetupZombie() override;
    void ZombieMove(float dt,Transform* transform) override;
private:
    /** 只判断当前合法射程内有无目标，不做昂贵的选点评估。 */
    bool HasTarget() const;
    float mReload=FloodMortarRules::FirstReload;
    float mCheckRemaining=0;
    int mShots=0,mTargetRow=-1,mTargetColumn=-1,mRollouts=0;
    bool mUsedMonteCarlo=false;
};
