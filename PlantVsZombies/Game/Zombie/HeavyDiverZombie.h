#pragma once
#include "Zombie.h"
#include "HeavyDiverRules.h"

/** 两栖重装：水下直射减伤，定期换气；接触目标后按独立计时抡锚。 */
class HeavyDiverZombie final : public Zombie {
public:
    using Zombie::Zombie;
    static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
    void Update() override;
    void ZombieUpdate(float scaledTime) override;
    void Draw(Graphics* g) override;
    Vector GetVisualPosition() const override;
    int ModifyProjectileDamage(int damage, BulletType type) const override;
    float ModifySpikeFrameDamage(float damage, bool bypassShield = false) const override;
    bool ResistsTangleKelpDrowning() const override { return true; }
    // 复用父类死亡事件；旧啃食事件不提交伤害，抡锚由同一游戏计时驱动演出与命中。
    void EatTarget() override {}
    void ZombieItemUpdate() const override;
    void HeadDrop() override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetInterruptibleSpecialActionRemaining() const override;
    bool InterruptUncommittedSpecialAction() override;
    float GetMineSimulationAttackDps() const override { return HeavyDiverRules::SmashDamage / HeavyDiverRules::SmashCycle; }
    bool IsSubmerged() const { return mInPool && !mBreathing && mHasHead && !mIsDying; }
    bool IsBreathing() const { return mInPool && mBreathing; }
    float GetAirRemaining() const { return mAirRemaining; }
    float GetSmashRemaining() const { return mSmashRemaining; }
    int GetSmashCount() const { return mSmashCount; }
protected:
    void SetupZombie() override;
    void OnStartEating() override;
    void OnStopEating() override;
    void OnMindControlled() override;
    float GetAbilityAnimSpeedMultiplier() const override { return mIsEating ? 1.0f : HeavyDiverRules::MoveMultiplier; }
private:
    /** 根据头部生命周期配置独立命名装备槽，不覆盖黄油附件。 */
    void SyncEquipment() const;
    float mAirRemaining = HeavyDiverRules::SubmergedSeconds;
    float mSmashRemaining = HeavyDiverRules::Windup;
    float mSubmergeOffset = 0;
    bool mBreathing = false;
    bool mWasInWater = false;
    int mSmashCount = 0;
};
