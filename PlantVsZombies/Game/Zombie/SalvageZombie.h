#pragma once
#include "Zombie.h"

/** 打捞僵尸：锁定本行第一组植物的支撑层，以可中断拉拽拆除单个基座。 */
class SalvageZombie final : public Zombie {
public:
    using Zombie::Zombie;
    void Update() override;
    void ZombieUpdate(float seconds) override;
    void Draw(Graphics* g) override;
    void StartEat(ColliderComponent* other) override;
    void HeadDrop() override;
    void ZombieItemUpdate() const override;
    bool HasMagneticItem() const override { return mHasHook && mHasHead && !mIsDying; }
    MagneticSimulationLayer GetMagneticSimulationLayer() const override {
        return HasMagneticItem() ? MagneticSimulationLayer::TOOL : MagneticSimulationLayer::NONE;
    }
    bool ExtractMagneticItem(MagneticItem& item) override;
    float GetInterruptibleSpecialActionRemaining() const override;
    bool InterruptUncommittedSpecialAction() override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    int GetSalvageTargetID() const { return mTargetID; }
    float GetSalvageWindup() const { return mWindup; }
    float GetSalvageCooldown() const { return mCooldown; }
    int GetSalvageCount() const { return mSalvageCount; }
    bool HasHook() const { return mHasHook; }
protected:
    void SetupZombie() override;
    void ZombieMove(float seconds, Transform* transform) override;
    void OnMindControlled() override;
private:
    /** 无基座的第一组植物同样遮挡索敌，不能从它下面越过攻击后排。 */
    Plant* FindFoundation() const;
    void CancelSalvage();
    void SyncEquipment() const;
    Vector GetHookPosition() const;
    int mTargetID = NULL_PLANT_ID;
    float mWindup = 0.0f;
    float mCooldown = 0.0f;
    int mSalvageCount = 0;
    bool mHasHook = true;
};
