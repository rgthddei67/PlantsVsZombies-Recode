#pragma once
#include "ConeZombie.h"

/** 淤泥僵尸：可打断喷射、独立在途泥团，以及只修复现存路障的发射收益。 */
class SludgeZombie final : public ConeZombie {
public:
    using ConeZombie::ConeZombie;
    void Update() override;
    void ZombieUpdate(float seconds) override;
    void Draw(Graphics* g) override;
    void StartEat(ColliderComponent* other) override;
    void HeadDrop() override;
    void ZombieItemUpdate() const override;
    float GetInterruptibleSpecialActionRemaining() const override;
    bool InterruptUncommittedSpecialAction() override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetWindup() const { return mWindup; }
    float GetReload() const { return mReload; }
    int GetShots() const { return mShots; }
    int GetRepairedTotal() const { return mRepairedTotal; }
    int GetRepairCount() const { return mRepairCount; }
    Vector GetMuzzlePosition() const;
protected:
    void SetupZombie() override;
    void ZombieMove(float seconds, Transform* transform) override;
    void OnMindControlled() override;
private:
    Plant* FindTarget() const;
    void CancelSpray();
    float GetEquipmentScale() const;
    void SyncEquipment() const;
    void Spray();
    int mTargetID = NULL_PLANT_ID;
    float mWindup = 0.0f;
    float mReload = 0.0f;
    int mShots = 0;
    int mRepairedTotal = 0;
    int mRepairCount = 0;
    float mRepairFeedback = 0.0f;
    int mLastRepair = 0;
};
