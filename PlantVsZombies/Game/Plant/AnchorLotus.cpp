#include "AnchorLotus.h"
#include "Game/Board/Board.h"
#include "Game/Zombie/Zombie.h"
#include "Graphics.h"
#include <cmath>

void AnchorLotus::SetupPlant()
{
    mPlantHealth = mPlantMaxHealth = AnchorLotusRules::Health;
    RemoveShadow();
}

/** 消费正式行动时间推进冷却；一次只建立一个按稳定 ID 关联的束缚。 */
void AnchorLotus::PlantUpdate()
{
    if (mIsPreview || !mBoard) return;
    mCooldown = std::max(0.0f, mCooldown - GetWeatherActionDeltaTime());
    Zombie* held = mBoard->mEntityRegistry.GetZombie(mTargetID);
    if (held && held->GetAnchorRootPlantID() == mPlantID) return;
    mTargetID = NULL_ZOMBIE_ID;
    if (mCooldown > 0) return;
    const float startX = GetPosition().x;
    Zombie* target = nullptr;
    mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* z) {
        const float distance = z->GetPosition().x - startX;
        if (distance < 0 || distance > AnchorLotusRules::RangeCells * CELL_COLLIDER_SIZE_X
            || !z->CanBeAnchorRooted()) return;
        if (!target || z->GetPosition().x < target->GetPosition().x
            || (z->GetPosition().x == target->GetPosition().x && z->mZombieID < target->mZombieID)) target = z;
    });
    if (!target || !target->StartAnchorRoot(mPlantID)) return;
    mTargetID = target->mZombieID;
    mCooldown = AnchorLotusRules::Cooldown;
    ++mRootCount;
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FLOOP, .18f);
}

/** 以双方当前视觉锚点画根须；位移、伤害和持续时间均不依赖绘制。 */
void AnchorLotus::Draw(Graphics* g)
{
    Plant::Draw(g);
    if (!g || !mBoard || mIsPreview) return;
    const auto* target = mBoard->mEntityRegistry.GetZombie(mTargetID);
    if (!target || target->GetAnchorRootPlantID() != mPlantID) return;
    const Vector from = GetVisualAnchorPosition() + Vector(0, 18);
    const Vector to = target->GetPosition() + Vector(0, 22);
    Vector previous = from;
    for (int i = 1; i <= 24; ++i) {
        const float t = i / 24.0f;
        const Vector next(from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t + std::sin(t * 18.849556f) * 3);
        for (int offset = -2; offset <= 2; ++offset)
            g->DrawLine(previous.x, previous.y + offset, next.x, next.y + offset,
                offset == 0 ? glm::vec4(218, 192, 114, 230) : glm::vec4(89, 103, 53, 220));
        previous = next;
    }
    g->DrawCircle(to.x, to.y - 5, 15, glm::vec4(150, 174, 82, 230), 20);
}

/** 来源移除立即松绑；目标负责开始共享免疫，避免下一株同帧补锁。 */
void AnchorLotus::Die()
{
    if (mBoard) {
        if (auto* target = mBoard->mEntityRegistry.GetZombie(mTargetID)) target->ReleaseAnchorRoot(mPlantID);
    }
    mTargetID = NULL_ZOMBIE_ID;
    Plant::Die();
}

void AnchorLotus::SaveExtraData(nlohmann::json& j) const
{
    j["rootCooldown"] = mCooldown;
    j["rootTargetID"] = mTargetID;
    j["rootCount"] = mRootCount;
}

/** 恢复提交后的关系和冷却；目标稍后才加载，不能在这里误判其失踪。 */
void AnchorLotus::LoadExtraData(const nlohmann::json& j)
{
    const float remaining = j.value("rootCooldown", 0.0f);
    mCooldown = std::isfinite(remaining) ? std::clamp(remaining, 0.0f, AnchorLotusRules::Cooldown) : 0;
    mTargetID = j.value("rootTargetID", NULL_ZOMBIE_ID);
    mRootCount = std::max(0, j.value("rootCount", 0));
}
