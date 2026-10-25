#include "LotusParasolZombie.h"
#include "Game/Board/Board.h"
#include <cmath>

namespace {
constexpr float GripX = -85.0f; // 完整伞柄握持点相对内前臂轨的横偏移，像素
constexpr float GripY = -132.0f; // 完整伞柄握持点相对内前臂轨的纵偏移，像素
}

void LotusParasolZombie::SetupZombie()
{
    Zombie::SetupZombie();
    mBodyHealth = mBodyMaxHealth = LotusParasolRules::Health;
    SyncParasol();
}

bool LotusParasolZombie::IsParasolActive() const
{
    return IsActive() && !mIsPreview && !mIsDead && !mIsDying && mHasHead
        && !IsMindControlled() && !IsImmobilized() && mParasolHealth > 0.0f;
}

/** 按当前逻辑格覆盖九宫格；场外中心不夹到边缘格，也不影响移动定身。 */
bool LotusParasolZombie::CanInterceptLobbedFor(int row, float x) const
{
    if (!mBoard || !IsParasolActive()) return false;
    const int col = static_cast<int>(std::floor((GetPosition().x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X));
    const int targetCol = static_cast<int>(std::floor((x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X));
    return col >= 0 && col < mBoard->mColumns && targetCol >= 0 && targetCol < mBoard->mColumns
        && std::abs(mRow - row) <= LotusParasolRules::Radius
        && std::abs(col - targetCol) <= LotusParasolRules::Radius;
}

/** 先承诺整发拦截再扣伞；最后一发完整弹走，不把不足耐久转成目标伤害。 */
bool LotusParasolZombie::InterceptLobbedProjectile(int damage)
{
    if (!IsParasolActive()) return false;
    const int scaled = mBoard->GetPerkManager().ScalePlantDamage(std::max(0, damage));
    mParasolHealth = std::max(0.0f, mParasolHealth - scaled * LotusParasolRules::DamageMultiplier);
    ++mReflectedCount;
    mReflectPulse = LotusParasolRules::ReflectPulse;
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BOING, .35f);
    SyncParasol();
    return true;
}

/** 反馈时钟与能力门禁分离；冻结不能延长已经提交的弹飞演出。 */
void LotusParasolZombie::Update()
{
    if (mBoard && mBoard->GetState() == BoardState::GAME)
        mReflectPulse = std::max(0.0f, mReflectPulse - DeltaTime::GetDeltaTime());
    Zombie::Update();
    // ZombieItemUpdate 仅负责恢复；实时跟随掉头、死亡及已衰减的受击演出。
    SyncParasol();
}

void LotusParasolZombie::SyncParasol() const
{
    const char* key = mParasolHealth > LotusParasolRules::Durability * .66f ? "IMAGE_LOTUSPARASOL_INTACT"
        : mParasolHealth > LotusParasolRules::Durability * .33f ? "IMAGE_LOTUSPARASOL_DAMAGED" : "IMAGE_LOTUSPARASOL_BROKEN";
    const float pulse = std::sin(mReflectPulse / LotusParasolRules::ReflectPulse * 3.14159265f);
    mAnimator->SetTrackFollowerImage("anim_innerarm2", "lotus_parasol",
        ResourceManager::GetInstance().GetTexture(key, false), GripX, GripY - pulse * 4,
        1.0f + pulse * .035f, 1.0f, false, true, true);
    mAnimator->SetTrackFollowerVisible("anim_innerarm2", "lotus_parasol",
        mParasolHealth > 0 && mHasHead && !mIsDying && !mIsDead);
}

void LotusParasolZombie::ZombieItemUpdate() const { Zombie::ZombieItemUpdate(); SyncParasol(); }

void LotusParasolZombie::SaveExtraData(nlohmann::json& j) const
{
    j["parasolHealth"] = mParasolHealth;
    j["reflectPulse"] = mReflectPulse;
    j["reflectedCount"] = mReflectedCount;
}

void LotusParasolZombie::LoadExtraData(const nlohmann::json& j)
{
    mParasolHealth = j.value("parasolHealth", LotusParasolRules::Durability);
    mReflectPulse = j.value("reflectPulse", 0.0f);
    if (!std::isfinite(mParasolHealth) || !std::isfinite(mReflectPulse))
        throw std::runtime_error("non-finite lotus parasol state");
    mParasolHealth = std::clamp(mParasolHealth, 0.0f, LotusParasolRules::Durability);
    mReflectPulse = std::clamp(mReflectPulse, 0.0f, LotusParasolRules::ReflectPulse);
    mReflectedCount = std::max(0, j.value("reflectedCount", 0));
    SyncParasol();
}
