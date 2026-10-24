#include "Bullet.h"
#include "Game/Board/Board.h"
#include "Game/Plant/LotusPultRules.h"
#include <algorithm>
#include <cmath>

namespace {
    /** 追踪的是身体碰撞中心，视觉偏移与水下裁剪不改变目标身份。 */
    Vector LotusTargetPoint(const Zombie& zombie)
    {
        const auto bounds = zombie.GetColliderComponent()->GetBoundingBox();
        return {bounds.x + bounds.w * .5f, bounds.y + bounds.h * .35f};
    }
}

void Bullet::ConfigureLotusSeed(bool split)
{
    mLotusState = std::make_unique<LotusState>();
    mLotusState->split = split;
    ConfigurePresentation();
}

void Bullet::ConfigureLotusFragment(int targetID, float arcHeight)
{
    mLotusState = std::make_unique<LotusState>();
    auto& state = *mLotusState;
    state.targetID = targetID;
    state.start = state.target = GetPosition();
    state.arcHeight = arcHeight;
    if (Zombie* target = mBoard->mEntityRegistry.GetZombie(targetID))
        state.target = LotusTargetPoint(*target);
    if (mCollider) mCollider->mEnabled = false;
}

/** 分裂先冻结不同目标；主目标随后即使死亡，也不会丢掉这次已兑现的分裂。 */
void Bullet::SplitLotusSeed(const Zombie* primary)
{
    if (!mBoard || !mLotusState || !mLotusState->split) return;
    mLotusState->split = false;
    const Vector impact = GetPosition();
    std::vector<int> targets;
    for (int row = std::max(0, mRow - LotusPultRules::SplitRowRadius);
        row <= std::min(mBoard->mRows - 1, mRow + LotusPultRules::SplitRowRadius); ++row) {
        mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* z) {
            if (z == primary || !z->CanBeTargetedByProjectile(false) || !z->GetColliderComponent()) return;
            if (std::fabs(LotusTargetPoint(*z).x - impact.x)
                <= CELL_COLLIDER_SIZE_X * LotusPultRules::SplitHalfWidth) targets.push_back(z->mZombieID);
        });
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    if (targets.size() > LotusPultRules::FragmentLimit) targets.resize(LotusPultRules::FragmentLimit);
    for (std::size_t i = 0; i < targets.size(); ++i) {
        Zombie* target = mBoard->mEntityRegistry.GetZombie(targets[i]);
        if (!target || !target->CanBeTargetedByProjectile(false)) continue;
        Bullet* seed = mBoard->CreatePlantBullet(BulletType::BULLET_LOTUS_FRAGMENT,
            target->mRow, impact, mPlantDamageOrigin.lineage);
        if (!seed) continue;
        seed->SetPlantDamageOrigin(mPlantDamageOrigin);
        seed->ConfigureLotusFragment(targets[i], LotusPultRules::FragmentArcHeight + i * 5.0f);
    }
}

/** 短弧线追踪固定目标；失效后飞到最后落点落空，绝不让碰撞回调打中旁人。 */
void Bullet::UpdateLotusFragment(float seconds)
{
    if (!mLotusState || !mBoard) { Die(); return; }
    auto& state = *mLotusState;
    Zombie* target = mBoard->mEntityRegistry.GetZombie(state.targetID);
    if (!target || !target->CanBeTargetedByProjectile(false) || !target->GetColliderComponent()) {
        state.targetID = NULL_ZOMBIE_ID;
        target = nullptr;
    } else state.target = LotusTargetPoint(*target);
    state.elapsed = std::min(LotusPultRules::FragmentFlightSeconds, state.elapsed + seconds);
    const float t = state.elapsed / LotusPultRules::FragmentFlightSeconds;
    const Vector old = GetPosition();
    const Vector position = state.start + (state.target - state.start) * t
        + Vector(0, -4 * state.arcHeight * t * (1 - t));
    GetTransform()->SetPosition(position);
    mVelocityX = seconds > 0 ? (position.x - old.x) / seconds : 0;
    mRotationDegrees += seconds * 180;
    UpdateShadowLayout(position);
    if (t < 1) return;
    mHasHit = true;
    if (target) BulletHitZombie(target);
    Die();
}

nlohmann::json Bullet::SaveLotusState() const
{
    if (!mLotusState) return nullptr;
    const auto& s = *mLotusState;
    return {{"split", s.split}, {"targetID", s.targetID}, {"start", {s.start.x, s.start.y}},
        {"target", {s.target.x, s.target.y}}, {"elapsed", s.elapsed}, {"arcHeight", s.arcHeight}};
}

/** 恢复已锁定的在途状态；不查询目标是否已加载、不重分裂或重抽目标。 */
void Bullet::RestoreLotusState(const nlohmann::json& value)
{
    if (mBulletType != BulletType::BULLET_LOTUS_SEED && mBulletType != BulletType::BULLET_LOTUS_FRAGMENT) return;
    mLotusState = std::make_unique<LotusState>();
    if (value.is_object()) {
        auto& s = *mLotusState;
        s.split = value.value("split", false);
        s.targetID = value.value("targetID", NULL_ZOMBIE_ID);
        const auto start = value.value("start", std::vector<float>{GetPosition().x, GetPosition().y});
        const auto target = value.value("target", start);
        if (start.size() != 2 || target.size() != 2) throw std::runtime_error("invalid lotus trajectory");
        s.start = Vector(start[0], start[1]); s.target = Vector(target[0], target[1]);
        s.elapsed = value.value("elapsed", 0.0f); s.arcHeight = value.value("arcHeight", 0.0f);
        if (!std::isfinite(s.start.x) || !std::isfinite(s.start.y) || !std::isfinite(s.target.x)
            || !std::isfinite(s.target.y) || !std::isfinite(s.elapsed) || !std::isfinite(s.arcHeight))
            throw std::runtime_error("non-finite lotus trajectory");
        s.elapsed = std::clamp(s.elapsed, 0.0f, LotusPultRules::FragmentFlightSeconds);
        s.arcHeight = std::clamp(s.arcHeight, 0.0f, LotusPultRules::FragmentArcHeight + 30.0f);
    }
    if (mBulletType == BulletType::BULLET_LOTUS_FRAGMENT && mCollider) mCollider->mEnabled = false;
    ConfigurePresentation();
}
