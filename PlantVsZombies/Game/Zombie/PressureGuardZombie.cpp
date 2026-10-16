#include "PressureGuardZombie.h"
#include "Game/Board/Board.h"
#include "ResourceManager.h"
#include "DeltaTime.h"
#include "Graphics.h"

using namespace PressureGuardRules;

void PressureGuardZombie::SetupZombie()
{
    Zombie::SetupZombie();
    mBodyHealth = mBodyMaxHealth = BodyHealth;
    mHelmType = HelmType::HELMTYPE_PRESSURE_SHIELD;
    mHelmHealth = mHelmMaxHealth = ShieldHealth;
    mAttackDamage = BiteDamage;
    SyncEquipment();
}

bool PressureGuardZombie::HasPressureShield() const
{
    return mHelmType == HelmType::HELMTYPE_PRESSURE_SHIELD && mHelmHealth > 0;
}

bool PressureGuardZombie::CanProtect() const
{
    return IsActive() && !IsDying() && HasHead() && HasPressureShield()
        && !IsImmobilized() && !IsTangleKelpTarget();
}

int PressureGuardZombie::LimitFinalIncomingDamage(int damage, DamageSource source, PlantDamageOrigin origin)
{
    if (!HasPressureShield() || (source == DamageSource::PLANT_ASH && !origin.IsValid()) || origin == PlantDamageOrigin::Ash()) return damage;
    mLastPressureHit = DeltaTime::GetTotalTime();
    return static_cast<int>(mDamageWindow.Consume(static_cast<float>(damage), mLastPressureHit));
}

void PressureGuardZombie::TakeTransferredDamage(int damage)
{
    if (!HasPressureShield() || !IsActive() || IsDying()) return;
    damage = LimitFinalIncomingDamage(damage, DamageSource::PLANT, {});
    if (damage <= 0) return;
    // 代伤已经是最终值，直接走目标自有防具/本体层，不重进普通伤害入口。
    const int remaining = TakeHelmDamage(damage);
    if (remaining > 0) TakeBodyDamage(remaining);
    SetGlowingTimer(.15f);
}

void PressureGuardZombie::ResolvePlantAshDamage(int damage)
{
    if (damage <= 0 || !mBoard) return;
    const int scaled = ScaleStatusDamage(mBoard->GetPerkManager().ScaleTotalDamageToZombie(damage));
    if (CanBeCharred() && static_cast<int64_t>(mBodyHealth) + std::max(0, mHelmHealth) <= scaled) {
        Charred();
        return;
    }
    TakeDamage(damage, DamageSource::PLANT_ASH, false, false, false, PlantDamageOrigin::Ash());
}

int PressureGuardZombie::GetPressureStage() const
{
    return !HasPressureShield() ? 3 : mHelmHealth <= mHelmMaxHealth / 3 ? 2
        : mHelmHealth <= mHelmMaxHealth * 2 / 3 ? 1 : 0;
}

void PressureGuardZombie::SyncEquipment() const
{
    static constexpr const char* keys[]{"IMAGE_PRESSURE_GUARD_EQUIPMENT", "IMAGE_PRESSURE_GUARD_EQUIPMENT_DAMAGED1",
        "IMAGE_PRESSURE_GUARD_EQUIPMENT_DAMAGED2", "IMAGE_PRESSURE_GUARD_EQUIPMENT_SPENT"};
    mAnimator->SetTrackFollowerImage("Zombie_body", "pressure_guard_equipment",
        ResourceManager::GetInstance().GetTexture(keys[GetPressureStage()], false), -5, -8, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerVisible("Zombie_body", "pressure_guard_equipment", true);
}

void PressureGuardZombie::CheckHelmImage() { SyncEquipment(); }

void PressureGuardZombie::HelmDrop()
{
    Zombie::HelmDrop();
    // 破盾停止限伤，但保留尚未过期的账本；时间锚恢复防具也不能刷新这一秒的额度。
    SyncEquipment();
}

void PressureGuardZombie::ZombieItemUpdate() const
{
    Zombie::ZombieItemUpdate();
    SyncEquipment();
}

void PressureGuardZombie::Draw(Graphics* g)
{
    Zombie::Draw(g);
    // 只有受击瞬间提示范围，使用逻辑原点与本棋盘行高，不跟随身体摆动。
    if (!mIsPreview && mBoard && CanProtect() && DeltaTime::GetTotalTime() - mLastPressureHit < .18) {
        const auto pos = GetPosition();
        const float width = CELL_COLLIDER_SIZE_X;
        const float height = mBoard->GetCellHeight();
        g->DrawRect(pos.x-width, pos.y-height*1.5f, width*2, height*3, {80, 210, 190, 100});
    }
}

PressureGuardRules::DamageWindow PressureGuardZombie::CaptureDamageWindow() const
{
    return mDamageWindow.RelativeTo(DeltaTime::GetTotalTime());
}

float PressureGuardZombie::GetDamageWindowUsed() const
{
    return mDamageWindow.Used(DeltaTime::GetTotalTime());
}

void PressureGuardZombie::SaveExtraData(nlohmann::json& j) const
{
    j["pressureWindow"] = nlohmann::json::array();
    for (const auto& hit : CaptureDamageWindow().hits)
        j["pressureWindow"].push_back({{"remaining", hit.expires}, {"damage", hit.damage}});
}

void PressureGuardZombie::LoadExtraData(const nlohmann::json& j)
{
    mDamageWindow.hits.clear();
    if (j.contains("pressureWindow") && j["pressureWindow"].is_array()) {
        const double now = DeltaTime::GetTotalTime();
        float remainingBudget = static_cast<float>(DamageLimit);
        for (const auto& entry : j["pressureWindow"]) {
            const double remaining = entry.value("remaining", 0.0);
            const float damage = entry.value("damage", 0.0f);
            if (!std::isfinite(remaining) || !std::isfinite(damage) || remaining <= 0 || damage <= 0) continue;
            const float accepted = std::min(damage, remainingBudget);
            if (accepted <= 0) break;
            mDamageWindow.hits.push_back({now + std::min(remaining, WindowSeconds), accepted});
            remainingBudget -= accepted;
        }
    }
    SyncEquipment();
}
