#pragma once
#include "Zombie.h"
#include "FogFerryRules.h"

/** 雾渡僵尸：留在原位，把本行三格内的地面友军送往前线。 */
class FogFerryZombie final : public Zombie {
public:
    using Zombie::Zombie;
    enum class Phase { WAITING, CHANNELING, DISABLED };
    void Update() override;
    void StartEat(ColliderComponent* other) override;
    void HeadDrop() override;
    void ArmDrop() override;
    void Die() override;
    void ZombieItemUpdate() const override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetInterruptibleSpecialActionRemaining() const override;
    bool InterruptUncommittedSpecialAction() override;
    Phase GetFerryPhase() const { return mPhase; }
    float GetFerryRemaining() const { return mRemaining; }
    int GetFerryUses() const { return mUses; }
    int GetFerryMovedCount() const { return mMovedCount; }
    int GetFerryTargetColumn() const;
    /** 光照只放慢前摇，不消除天气雾区或重置已有进度。 */
    float GetChannelRate() const;
protected:
    void SetupZombie() override;
    void ZombieMove(float scaledDelta, Transform* transform) override;
    void OnMindControlled() override;
private:
    /** 撤销尚未提交的施法，恢复可行走状态；永久停用用于失去能力资格。 */
    void CancelChannel(bool disable, bool feedback);
    /** 同步背包装置，不改动经典时间轴或新增帧事件。 */
    void SyncPresentation() const;
    /** 按当前落点发射短寿命提示，暂停或硬控期间不新增粒子。 */
    void EmitChannelPulse();
    Phase mPhase = Phase::WAITING;
    float mRemaining = FogFerryRules::FirstWait;
    float mPulseRemaining = 0;
    int mUses = 0;
    int mMovedCount = 0;
};
