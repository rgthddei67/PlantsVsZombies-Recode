#pragma once
#include "Zombie.h"
#include "PressureGuardRules.h"

/** 承压护卫：不可修复的一类装置替邻近同阵营僵尸承伤，破盾永久失效。 */
class PressureGuardZombie final : public Zombie {
public:
    using Zombie::Zombie;
    bool HasMagneticItem() const override { return false; }
    bool CanRepairHelm() const override { return false; }
    bool HasPressureShield() const;
    bool CanProtect() const;
    /** 接收已经完成来源增伤的代伤；不再次转移、缩放或触发植物攻击副作用。 */
    void TakeTransferredDamage(int damage);
    /** 灰烬按总耐久判断化灰；幸存者继续保留尚存的装置。 */
    void TakePlantAshDamage(int damage) override;
    int LimitFinalIncomingDamage(int damage, DamageSource source, PlantDamageOrigin origin) override;
    void HelmDrop() override;
    void ZombieItemUpdate() const override;
    void Draw(Graphics* g) override;
    /** 按相对余时保存滚动账本，读档不重置当前秒的承伤额度。 */
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    PressureGuardRules::DamageWindow CaptureDamageWindow() const;
    float GetDamageWindowUsed() const;
    int GetPressureStage() const;
protected:
    /** 保留普通僵尸整套动作与事件，仅配置装置、生命和啃食差额。 */
    void SetupZombie() override;
    void CheckHelmImage() override;
private:
    /** 装备跟随身体主轨；破盾仍保留熄灭外壳，便于辨认终态。 */
    void SyncEquipment() const;
    PressureGuardRules::DamageWindow mDamageWindow;
    double mLastPressureHit = -10;
};
