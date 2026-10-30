#include "EngineeringShipZombie.h"
#include "GameApp.h"
#include "Game/Board/Board.h"
#include "Game/Plant/Plant.h"
#include "Game/ShadowComponent.h"

#include <array>
#include <cmath>
#include <span>
#include <stdexcept>

using namespace EngineeringShipRules;

/** 专用分件没有普通僵尸的断肢和啃食时间轴，全部动作由已保存的机械阶段驱动。 */
void EngineeringShipZombie::SetupZombie()
{
    mBodyHealth = mBodyMaxHealth = Health;
    mHelmHealth = mHelmMaxHealth = mShieldHealth = mShieldMaxHealth = 0;
    mHelmType = HelmType::HELMTYPE_NONE;
    mShieldType = ShieldType::SHIELDTYPE_NONE;
    mNeedDropArm = mNeedDropHead = false;
    mHasHead = mHasArm = true;
    mHasTongue = false;
    mSpeed = 0.0f;
    mAttackDamage = 0;
    mLaneFromY = GetPosition().y;
    if (mCollider) {
        mCollider->size = Vector(ColliderWidth, ColliderHeight);
        SyncCollisionToRow();
        mCollider->SetTriggerEnterCallback(nullptr);
        mCollider->SetTriggerStayCallback(nullptr);
        mCollider->SetTriggerExitCallback(nullptr);
    }
    SetAnimationSpeed(1.0f);
    if (!PlayTrack("anim_idle", 1.0f))
        throw std::runtime_error("EngineeringShipZombie requires anim_idle");
    SyncVisualState();
}

void EngineeringShipZombie::Update()
{
    const float seconds = DeltaTime::GetDeltaTime();
    // 发射边沿已经完成伤害；余波和沉没按独立游戏时间继续，不能被后来冻结撤回。
    if (!mIsPreview) mShotVisualRemaining = std::max(0.0f, mShotVisualRemaining - seconds);
    if (!mIsPreview && mPhase == Phase::SINKING) {
        AnimatedObject::Update();
        mPhaseRemaining = std::max(0.0f, mPhaseRemaining - seconds);
        SyncVisualState();
        if (mPhaseRemaining <= 0.0f) Die();
        return;
    }
    const bool wasButtered = IsButtered();
    if (!mIsPreview)
        mButterImmunityRemaining = std::max(0.0f, mButterImmunityRemaining - seconds);
    Zombie::Update();
    if (wasButtered && !IsButtered() && IsActive() && !mIsDying && !mIsDead)
        mButterImmunityRemaining = ButterImmunitySeconds;
    if (IsActive()) SyncVisualState();
}

void EngineeringShipZombie::ZombieUpdate(float seconds)
{
    if (mIsPreview || mIsDead || mIsDying || !mBoard || seconds <= 0.0f) return;
    // 基类已处理硬控/海草束缚和减速；雨势不另行缩短公开的机械蓄力窗口。
    mPhaseRemaining = std::max(0.0f, mPhaseRemaining - seconds);
    if (mPhase == Phase::RELOCATING) UpdateRelocation();
    if (mPhaseRemaining > 0.0f) return;
    switch (mPhase) {
    case Phase::RELOCATING:
        mLaneDestinationRow = -1;
        BeginCharge();
        break;
    case Phase::CHARGING:
        // 先提交开舱状态，再调用植物受击；反击导致的沉没不能被末尾换态覆盖。
        mVolleyShotsRemaining = std::max(0, mVolleyShotsRemaining - 1);
        BeginCooling();
        FireCannon();
        if (!mIsDying && !mIsDead && mVolleyShotsRemaining == 0) SummonReinforcements();
        break;
    case Phase::COOLING:
        if (mVolleyShotsRemaining > 0) BeginCharge(true);
        else BeginRelocation();
        break;
    case Phase::SINKING:
        break;
    }
}

void EngineeringShipZombie::BeginRelocation()
{
    mPhase = Phase::RELOCATING;
    mPhaseRemaining = RelocationSeconds;
    mLaneFromY = GetPosition().y;
    mLaneDestinationRow = -1;
    mTargetRow = mTargetColumn = -1;
    if (!mBoard || IsAnchorRooted()) return;
    std::array<int, 2> rows{};
    int count = 0;
    int bestFront = -1;
    for (int destination : {mRow - 1, mRow + 1}) {
        if (destination >= 0 && destination < mBoard->mRows
            && mBoard->CanSpawnZombieInRow(mZombieType, destination)) {
            const int front = FindFrontColumn(destination);
            if (front < bestFront) continue;
            if (front > bestFront) { bestFront = front; count = 0; }
            rows[count++] = destination;
        }
    }
    if (count == 0) return;
    // 相邻行全空时保留本行有效火力；全空才继续巡游寻找阵线。
    if (bestFront < 0 && FindFrontColumn(mRow) >= 0) return;
    const int destination = rows[count == 1 ? 0 : GameRandom::Range(0, count - 1)];
    const float destinationY = mBoard->GetZombieSpawnY(destination, GetPosition().x);
    if (destinationY < 0.0f) return;
    mLaneDestinationRow = destination;
    ++mLaneSwitchCount;
}

/** 位置、碰撞和画面读取同一个Transform；冻结/海草由基类同时暂停此过程。 */
void EngineeringShipZombie::UpdateRelocation()
{
    if (!mBoard || mLaneDestinationRow < 0) return;
    Vector position = GetPosition();
    if (IsAnchorRooted()) {
        // 根须中途命中时收敛到最近行，消费本次换位；剩余阶段仍正常走到蓄力。
        CommitNearestRow();
        position.y = mBoard->GetZombieSpawnY(mRow, position.x);
        GetTransform()->SetPosition(position);
        mLaneFromY = position.y;
        mLaneDestinationRow = -1;
        SyncCollisionToRow();
        return;
    }
    const float targetY = mBoard->GetZombieSpawnY(mLaneDestinationRow, position.x);
    if (targetY < 0.0f) { mLaneDestinationRow = -1; return; }
    position.y = std::lerp(mLaneFromY, targetY, GetLaneProgress());
    GetTransform()->SetPosition(position);
    CommitNearestRow();
    SyncCollisionToRow();
}

void EngineeringShipZombie::CommitNearestRow()
{
    if (!mBoard) return;
    const Vector position = GetPosition();
    int nearest = mRow;
    float bestDistance = std::abs(position.y - mBoard->GetZombieSpawnY(mRow, position.x));
    for (int row = 0; row < mBoard->mRows; ++row) {
        const float rowY = mBoard->GetZombieSpawnY(row, position.x);
        const float distance = std::abs(position.y - rowY);
        if (rowY >= 0.0f && distance < bestDistance) {
            bestDistance = distance;
            nearest = row;
        }
    }
    if (nearest != mRow) CommitRow(nearest);
}

float EngineeringShipZombie::GetLaneProgress() const
{
    return mPhase == Phase::RELOCATING && mLaneDestinationRow >= 0
        ? std::clamp(1.0f - mPhaseRemaining / RelocationSeconds, 0.0f, 1.0f) : 0.0f;
}

void EngineeringShipZombie::SyncCollisionToRow()
{
    if (!mCollider) return;
    mCollider->offset = mVisualOffset + Vector(ColliderLeft, ColliderTop);
    if (!mBoard || mIsPreview) return;
    const float baseline = mBoard->GetZombieCollisionY(mRow, GetPosition().x);
    const float spawnY = mBoard->GetZombieSpawnY(mRow, GetPosition().x);
    // 只抵消地图自身的美术下沉，不抵消真正正在发生的纵向换位。
    if (baseline >= 0.0f && spawnY >= 0.0f) mCollider->offset.y += baseline - spawnY;
}

/** 查询当前可受水炮影响的最前格，供换位与每发重新锁格共用。 */
int EngineeringShipZombie::FindFrontColumn(int row) const
{
    if (!mBoard) return -1;
    for (int column = mBoard->mColumns - 1; column >= 0; --column) {
        Plant* plant = mBoard->GetTopPlantAt(row, column);
        if (plant && plant->IsActive() && !plant->IsSquished()) return column;
    }
    return -1;
}

void EngineeringShipZombie::BeginCharge(bool followup)
{
    mPhase = Phase::CHARGING;
    mPhaseRemaining = ChargeSeconds;
    if (!followup) mVolleyShotsRemaining = IsEnraged() ? EnragedVolleyShots : 1;
    mTargetRow = mRow;
    mTargetColumn = FindFrontColumn(mRow);
}

void EngineeringShipZombie::BeginCooling()
{
    mPhase = Phase::COOLING;
    mPhaseRemaining = GetCoolingDuration();
}

/** 锁格不锁植物身份；先快照每格顶层并去重，防多格植物或破南瓜后同炮继续穿层。 */
void EngineeringShipZombie::FireCannon()
{
    if (!mBoard || mTargetRow < 0 || mTargetColumn < 0) return;
    std::array<int, CannonCells> targets{};
    int count = 0;
    for (int offset = 0; offset < CannonCells; ++offset) {
        const int column = mTargetColumn - offset;
        if (column < 0) break;
        Plant* plant = mBoard->GetTopPlantAt(mTargetRow, column);
        if (!plant || !plant->IsActive() || plant->IsSquished()) continue;
        if (std::find(targets.begin(), targets.begin() + count, plant->mPlantID) == targets.begin() + count)
            targets[count++] = plant->mPlantID;
    }
    ++mShotCount;
    mLastShotHitCount = 0;
    mShotVisualRemaining = ShotVisualSeconds;
    for (int index = 0; index < count; ++index) {
        Plant* plant = mBoard->mEntityRegistry.GetPlant(targets[index]);
        if (!plant || !plant->IsActive()) continue;
        const int previousHealth = plant->mPlantHealth;
        plant->TakeDamage(CannonDamage, DamageSource::ZOMBIE);
        if (!plant->IsActive() || plant->mPlantHealth < previousHealth) ++mLastShotHitCount;
    }
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_PLANT_WATER, .6f);
}

void EngineeringShipZombie::TryHalfHealthReinforcements()
{
    if (mReinforced || !mBoard || mIsPreview || mIsDying || mIsDead || mBodyHealth <= 0) return;
    // 整批尝试先消费；被既有品种限额挡下的护卫不排队、不在名额释放或读档后补生。
    mReinforced = true;
    SummonReinforcements(true);
}

/** 不补偿被限额挡下的历史批次；下一轮重新检查实际空位与正式品种配额。 */
void EngineeringShipZombie::SummonReinforcements(bool halfHealthBurst)
{
    if (!mBoard || mIsPreview || mIsDying || mIsDead || mBodyHealth <= 0) return;
    int alive = 0;
    for (int id : mBoard->mEntityRegistry.GetAllZombieIDs()) {
        const Zombie* zombie = mBoard->mEntityRegistry.GetZombie(id);
        if (zombie && zombie != this && zombie->IsActive() && !zombie->IsDying()
            && !zombie->IsMindControlled()) ++alive;
    }
    constexpr std::array regular = { // 常态每轮及半血突发护卫：两路障、两铁桶、一打捞、一淤泥
        ZombieType::ZOMBIE_TRAFFIC_CONE, ZombieType::ZOMBIE_TRAFFIC_CONE,
        ZombieType::ZOMBIE_BUCKET, ZombieType::ZOMBIE_BUCKET,
        ZombieType::ZOMBIE_SALVAGE, ZombieType::ZOMBIE_SLUDGE,
    };
    constexpr std::array enraged = { // 半血后每轮护卫：两铁桶、两橄榄球、两重潜、一打捞、一荷伞
        ZombieType::ZOMBIE_BUCKET, ZombieType::ZOMBIE_BUCKET,
        ZombieType::ZOMBIE_FOOTBALL, ZombieType::ZOMBIE_FOOTBALL,
        ZombieType::ZOMBIE_HEAVY_DIVER, ZombieType::ZOMBIE_HEAVY_DIVER,
        ZombieType::ZOMBIE_SALVAGE, ZombieType::ZOMBIE_LOTUS_PARASOL,
    };
    const std::span<const ZombieType> types = IsEnraged() && !halfHealthBurst
        ? std::span<const ZombieType>(enraged) : std::span<const ZombieType>(regular);
    for (int index = 0; index < static_cast<int>(types.size()) && alive < ReinforcementLimit; ++index) {
        int row = -1;
        for (int offset = 0; offset < mBoard->mRows; ++offset) {
            const int candidate = (mRow + index + 1 + offset) % mBoard->mRows;
            if (mBoard->CanSpawnZombieInRow(types[index], candidate)) { row = candidate; break; }
        }
        if (row < 0) continue;
        const ZombieType actual = mBoard->ResolveWaveZombieType(types[index]);
        if (actual == ZombieType::NUM_ZOMBIE_TYPES) continue;
        const float x = mBoard->GetCellCenterPosition(row, mBoard->mColumns - 1).x
            + CELL_COLLIDER_SIZE_X + index * GuardSpacing;
        if (mBoard->CreateResolvedWaveZombie(actual, row, x)) {
            ++mReinforcementCount;
            ++alive;
        }
    }
}

void EngineeringShipZombie::TakeDamage(int damage, DamageSource source, bool penetrateShield,
    bool discardShieldOverflow, bool bypassShield, PlantDamageOrigin origin)
{
    if (mIsDying || mIsDead || mPhase == Phase::SINKING) return;
    if (source == DamageSource::PLANT_ASH) damage = std::min(damage, AshDamageCap);
    Zombie::TakeDamage(damage, source, penetrateShield, discardShieldOverflow, bypassShield, origin);
}

int EngineeringShipZombie::LimitFinalIncomingDamage(int damage, DamageSource source, PlantDamageOrigin origin)
{
    // 玉米炮本身走 Board::CreateCobCannonExplosion 的正式灰烬入口；不能按植物谱系
    // 豁免 FromPlant(COBCANNON)，因为该谱系也包含普通玉米投手及黄油仁。
    if (source == DamageSource::PLANT_ASH || origin == PlantDamageOrigin::Ash()
        || mPhase == Phase::COOLING) return damage;
    // 保留半点，两个1点命中准确扣1血，不能把高频尖刺或毒素按每次取整吞成免疫。
    const int64_t total = static_cast<int64_t>(std::max(0, damage)) + mShellDamageRemainder;
    mShellDamageRemainder = static_cast<int>(total % ShellDamageDivisor);
    return static_cast<int>(total / ShellDamageDivisor);
}

void EngineeringShipZombie::ResolvePlantAshDamage(int damage)
{
    TakeDamage(damage, DamageSource::PLANT_ASH, false, false, false, PlantDamageOrigin::Ash());
}

void EngineeringShipZombie::TakeBodyDamage(int damage)
{
    if (mIsDying || mIsDead || damage <= 0) return;
    const int healthBefore = mBodyHealth;
    mBodyHealth = std::max(0, mBodyHealth - damage);
    if (mBodyHealth == 0) { BeginSinking(true); return; }
    if (healthBefore >= GetHalfHealthThreshold() && IsEnraged()) {
        if (mPhase == Phase::COOLING) mPhaseRemaining = std::min(mPhaseRemaining, GetCoolingDuration());
        TryHalfHealthReinforcements();
    }
}

void EngineeringShipZombie::BeginSinking(bool playSound)
{
    mBodyHealth = 0;
    mPhase = Phase::SINKING;
    mPhaseRemaining = SinkingSeconds;
    mIsDying = true;
    mTargetRow = mTargetColumn = -1;
    mLaneDestinationRow = -1;
    mVolleyShotsRemaining = 0;
    mShotVisualRemaining = 0.0f;
    ClearFrozen();
    ClearButter();
    ClearParalysis();
    if (mCollider) mCollider->mEnabled = false;
    if (auto* shadow = GetShadow()) shadow->SetEnabled(false);
    if (playSound) AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_EXPLOSION, .55f);
    SyncVisualState();
}

void EngineeringShipZombie::Charred() { if (!mIsDying && !mIsDead) BeginSinking(true); }
void EngineeringShipZombie::TakeHijackerExecution() { if (!mIsDying && !mIsDead) BeginSinking(true); }

bool EngineeringShipZombie::ApplyButter()
{
    if (IsButtered() || mButterImmunityRemaining > 0.0f || !Zombie::ApplyButter()) return false;
    mButterTimer = ButterSeconds;
    return true;
}

float EngineeringShipZombie::GetInterruptibleSpecialActionRemaining() const
{
    return mPhase == Phase::CHARGING && !mIsDying && !mIsDead && mTargetColumn >= 0
        ? mPhaseRemaining : -1.0f;
}

bool EngineeringShipZombie::InterruptUncommittedSpecialAction()
{
    if (GetInterruptibleSpecialActionRemaining() < 0.0f) return false;
    mVolleyShotsRemaining = 0;
    BeginCooling();
    mTargetRow = mTargetColumn = -1;
    SyncVisualState();
    return true;
}

float EngineeringShipZombie::GetCoolingDuration() const
{
    if (mPhase == Phase::COOLING && mVolleyShotsRemaining > 0) return VolleyCoolingSeconds;
    return IsEnraged() ? EnragedCoolingSeconds : CoolingSeconds;
}

Vector EngineeringShipZombie::GetVisualPosition() const
{
    const float sinking = mPhase == Phase::SINKING
        ? SinkingPixels * (1.0f - std::clamp(mPhaseRemaining / SinkingSeconds, 0.0f, 1.0f)) : 0.0f;
    return Zombie::GetVisualPosition() + Vector(0, sinking);
}

void EngineeringShipZombie::PlayWalkAnimation(float blendTime) { PlayTrack("anim_idle", 1.0f, blendTime); }
void EngineeringShipZombie::ZombieItemUpdate() const { SyncVisualState(); }

const char* EngineeringShipZombie::GetPhaseName() const
{
    switch (mPhase) {
    case Phase::RELOCATING: return "RELOCATING";
    case Phase::CHARGING: return "CHARGING";
    case Phase::COOLING: return "COOLING";
    case Phase::SINKING: return "SINKING";
    }
    return "RELOCATING";
}

void EngineeringShipZombie::SaveExtraData(nlohmann::json& j) const
{
    j["shipPhase"] = static_cast<int>(mPhase);
    j["shipPhaseRemaining"] = mPhaseRemaining;
    j["shipLaneFromY"] = mLaneFromY;
    j["shipLaneDestinationRow"] = mLaneDestinationRow;
    j["shipTargetRow"] = mTargetRow;
    j["shipTargetColumn"] = mTargetColumn;
    j["shipShotCount"] = mShotCount;
    j["shipVolleyShotsRemaining"] = mVolleyShotsRemaining;
    j["shipLastShotHitCount"] = mLastShotHitCount;
    j["shipShotVisualRemaining"] = mShotVisualRemaining;
    j["shipLaneSwitchCount"] = mLaneSwitchCount;
    j["shipButterImmunityRemaining"] = mButterImmunityRemaining;
    j["shipReinforced"] = mReinforced;
    j["shipReinforcementCount"] = mReinforcementCount;
    j["shipShellDamageRemainder"] = mShellDamageRemainder;
}

/** 恢复剩余阶段和已消费边沿；不重新锁格、换行、发炮或发放半血护卫。 */
void EngineeringShipZombie::LoadExtraData(const nlohmann::json& j)
{
    auto bounded = [&](const char* key, float fallback, float low, float high) {
        const float value = j.value(key, fallback);
        if (!std::isfinite(value)) throw std::runtime_error("non-finite engineering ship state");
        return std::clamp(value, low, high);
    };
    mPhase = static_cast<Phase>(std::clamp(j.value("shipPhase", 0), 0, 3));
    mVolleyShotsRemaining = std::clamp(j.value("shipVolleyShotsRemaining", 0), 0, EnragedVolleyShots);
    if (mPhase != Phase::CHARGING && mPhase != Phase::COOLING) mVolleyShotsRemaining = 0;
    const float duration = mPhase == Phase::RELOCATING ? RelocationSeconds
        : mPhase == Phase::CHARGING ? ChargeSeconds
        : mPhase == Phase::SINKING ? SinkingSeconds : GetCoolingDuration();
    mPhaseRemaining = bounded("shipPhaseRemaining", duration, 0.0f, duration);
    mLaneFromY = bounded("shipLaneFromY", GetPosition().y, -SCENE_HEIGHT, SCENE_HEIGHT * 2.0f);
    mLaneDestinationRow = j.value("shipLaneDestinationRow", -1);
    if (mPhase != Phase::RELOCATING || !mBoard || mLaneDestinationRow < 0
        || mLaneDestinationRow >= mBoard->mRows) mLaneDestinationRow = -1;
    // Transform Y由公共存档恢复；只重建最近行索引，不提前推进或重抽正在进行的换位。
    if (mPhase == Phase::RELOCATING) CommitNearestRow();
    mTargetRow = j.value("shipTargetRow", -1);
    mTargetColumn = j.value("shipTargetColumn", -1);
    if (!mBoard || mTargetRow < 0 || mTargetRow >= mBoard->mRows
        || mTargetColumn < 0 || mTargetColumn >= mBoard->mColumns)
        mTargetRow = mTargetColumn = -1;
    mShotCount = std::max(0, j.value("shipShotCount", 0));
    mLastShotHitCount = std::clamp(j.value("shipLastShotHitCount", 0), 0, CannonCells);
    mShotVisualRemaining = bounded("shipShotVisualRemaining", 0.0f, 0.0f, ShotVisualSeconds);
    mLaneSwitchCount = std::max(0, j.value("shipLaneSwitchCount", 0));
    mButterImmunityRemaining = bounded("shipButterImmunityRemaining", 0.0f, 0.0f, ButterImmunitySeconds);
    mButterTimer = std::min(mButterTimer, ButterSeconds);
    if (mButterTimer > 0.0f) mButterImmunityRemaining = 0.0f;
    mReinforced = j.value("shipReinforced", false);
    mReinforcementCount = std::max(0, j.value("shipReinforcementCount", 0));
    mShellDamageRemainder = std::clamp(j.value("shipShellDamageRemainder", 0), 0, ShellDamageDivisor - 1);
    mNeedDropHead = mNeedDropArm = false;
    mHasHead = mHasArm = true;
    mIsEating = mIsMindControlled = false;
    mEatPlantID = NULL_PLANT_ID;
    mEatZombieID = NULL_ZOMBIE_ID;
    SyncCollisionToRow();
    if (mBodyHealth <= 0 || mIsDying || mPhase == Phase::SINKING) {
        const float remaining = mPhase == Phase::SINKING ? mPhaseRemaining : SinkingSeconds;
        BeginSinking(false);
        mPhaseRemaining = remaining;
    }
    SyncVisualState();
}
