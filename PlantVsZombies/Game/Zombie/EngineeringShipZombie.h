#pragma once

#include "Zombie.h"
#include "EngineeringShipRules.h"

/** 12-9 两栖工程船；固定右侧作战，通过锁格水炮与散热弱点交替施压。 */
class EngineeringShipZombie : public Zombie {
public:
    using Zombie::Zombie;
    enum class Phase { RELOCATING, CHARGING, COOLING, SINKING };

    /** 公共控制按游戏时间推进，已提交水炮余波及死亡不受后续硬控暂停。 */
    void Update() override;
    void ZombieUpdate(float scaledTime) override;
    void Draw(Graphics* g) override;
    Vector GetVisualPosition() const override;
    /** 所有伤害入口共用灰烬基础上限，死亡阶段不再承伤或触发增援。 */
    void TakeDamage(int damage, DamageSource source, bool penetrateShield = false,
        bool discardShieldOverflow = false, bool bypassShield = false,
        PlantDamageOrigin plantOrigin = {}) override;
    void TakeBodyDamage(int damage) override;
    int LimitFinalIncomingDamage(int damage, DamageSource source, PlantDamageOrigin origin) override;
    void ResolvePlantAshDamage(int damage) override;
    bool TakePlantInstantKill() override { return false; }
    bool CanBeSquashedInstantly() const override { return false; }
    bool CanBeCharred() const override { return false; }
    void Charred() override;
    bool CanBeCharmed() const override { return false; }
    bool CanBeKilledByMower() const override { return false; }
    bool ResistsTangleKelpDrowning() const override { return true; }
    bool ApplyButter() override;
    void TakeHijackerExecution() override;
    void StartEat(ColliderComponent*) override {}
    void HeadDrop() override {}
    void ArmDrop() override {}
    bool CanTriggerGameOver() const override { return false; }
    float GetCurrentHorizontalMoveSpeed() const override { return 0.0f; }
    float GetMineSimulationMoveSpeed() const override { return 0.0f; }
    float GetMineSimulationAttackDps() const override { return 0.0f; }
    float GetRoofRunoffDriftMultiplier() const override { return 0.0f; }
    float GetInterruptibleSpecialActionRemaining() const override;
    bool InterruptUncommittedSpecialAction() override;
    bool HasCommittedIrreversibleSpecialAction() const override { return mReinforced; }
    void RestoreCommittedIrreversibleSpecialAction(bool committed) override { mReinforced |= committed; }
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    void ZombieItemUpdate() const override;

    Phase GetPhase() const { return mPhase; }
    const char* GetPhaseName() const;
    float GetPhaseRemaining() const { return mPhaseRemaining; }
    float GetCoolingDuration() const;
    int GetTargetRow() const { return mTargetRow; }
    int GetTargetColumn() const { return mTargetColumn; }
    int GetShotCount() const { return mShotCount; }
    int GetLastShotHitCount() const { return mLastShotHitCount; }
    bool HasReinforced() const { return mReinforced; }
    int GetReinforcementCount() const { return mReinforcementCount; }
    int GetLaneSwitchCount() const { return mLaneSwitchCount; }
    float GetButterImmunityRemaining() const { return mButterImmunityRemaining; }
    float GetLaneFromY() const { return mLaneFromY; }
    int GetLaneDestinationRow() const { return mLaneDestinationRow; }
    float GetLaneProgress() const;
    float GetShotVisualRemaining() const { return mShotVisualRemaining; }
    float GetDeathRemaining() const { return mPhase == Phase::SINKING ? mPhaseRemaining : 0.0f; }
    int GetHalfHealthThreshold() const { return mBodyMaxHealth / 2; }
    bool IsEnraged() const { return mBodyHealth < GetHalfHealthThreshold(); }
    int GetShellDamageRemainder() const { return mShellDamageRemainder; }

protected:
    void SetupZombie() override;
    void ZombieMove(float, Transform*) override {}
    void PlayWalkAnimation(float blendTime = 0.0f) override;
    bool CanBeMovedByTyphoonGust() const override { return false; }
    bool ShouldPlayDeathAnimation() const override { return false; }

private:
    /** 只锁定下一相邻行；实际位置连续推进，跨中线时才切换行索引。 */
    void BeginRelocation();
    void UpdateRelocation();
    void CommitNearestRow();
    void BeginCharge();
    void BeginCooling();
    void FireCannon();
    void TryHalfHealthReinforcements();
    void BeginSinking(bool playSound);
    /** 经典泳池的出生下沉只属于美术，船体碰撞始终回到当前行的逻辑基线。 */
    void SyncCollisionToRow();
    /** 只把已保存玩法状态投影到船体分件，实现在专用视觉文件。 */
    void SyncVisualState() const;

    Phase mPhase = Phase::RELOCATING;
    float mPhaseRemaining = EngineeringShipRules::RelocationSeconds;
    float mLaneFromY = 0.0f;
    int mLaneDestinationRow = -1;
    float mButterImmunityRemaining = 0.0f;
    float mShotVisualRemaining = 0.0f;
    int mTargetRow = -1;
    int mTargetColumn = -1;
    int mShotCount = 0;
    int mLastShotHitCount = 0;
    int mLaneSwitchCount = 0;
    int mReinforcementCount = 0;
    int mShellDamageRemainder = 0;
    bool mReinforced = false;
};
