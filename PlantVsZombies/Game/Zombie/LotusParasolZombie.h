#pragma once
#include "Zombie.h"
#include "LotusParasolRules.h"

/** 荷伞僵尸：移动九宫格内拦截投掷物，硬控暂停，破伞永久失效。 */
class LotusParasolZombie final : public Zombie {
public:
    using Zombie::Zombie;
    void Update() override;
    void ZombieItemUpdate() const override;
    bool CanInterceptLobbedFor(int row, float x) const override;
    bool InterceptLobbedProjectile(int damage) override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetParasolHealth() const { return mParasolHealth; }
    int GetReflectedCount() const { return mReflectedCount; }
    bool IsParasolActive() const;
protected:
    void SetupZombie() override;
private:
    void SyncParasol() const;
    float mParasolHealth = LotusParasolRules::Durability;
    float mReflectPulse = 0.0f;
    int mReflectedCount = 0;
};
