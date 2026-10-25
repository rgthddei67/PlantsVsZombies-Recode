#include "Bullet.h"
#include "Game/Board/Board.h"
#include "Game/GameObjectManager.h"
#include <algorithm>

namespace {
constexpr float ReflectionLifetime = .75f; // 弹飞残影总寿命，游戏秒
constexpr float ReflectionHorizontalSpeed = -125.0f; // 朝植物方向上抛但无碰撞，像素/秒
constexpr float ReflectionInitialVelocityY = -230.0f; // 离开伞面的初始纵速，像素/秒
constexpr float ReflectionGravity = 650.0f; // 弹飞抛物线重力，像素/秒平方
constexpr float ReflectionSpinSpeed = 310.0f; // 弹飞自旋速度，度/秒
constexpr float ReflectionFadeSeconds = .25f; // 收尾淡出时长，游戏秒

/** 已提交拦截的纯表现；不登记Bullet、碰撞或存档，不产生二次命中。 */
class ReflectedLob final : public GameObject {
public:
    ReflectedLob(const Texture* texture, Vector position, float scale, float rotation)
        : mTexture(texture), mScale(scale), mRotation(rotation)
    {
        CreateTransform(position);
        SetLayer(LAYER_GAME_BULLET);
        SetRenderOrder(LAYER_GAME_BULLET + 400);
        SetTag("ReflectedLob");
    }
    void Update() override {
        const float dt = DeltaTime::GetDeltaTime();
        mElapsed += dt;
        if (mElapsed >= ReflectionLifetime) {
            SetActive(false);
            GameObjectManager::GetInstance().DestroyGameObject(this);
            return;
        }
        auto* transform = GetTransform();
        transform->SetPosition(transform->GetPosition()
            + Vector(ReflectionHorizontalSpeed, ReflectionInitialVelocityY + ReflectionGravity * mElapsed) * dt);
        mRotation += dt * ReflectionSpinSpeed;
    }
    void Draw(Graphics* g) override {
        const auto p = GetTransform()->GetPosition();
        const float w = mTexture->width * mScale, h = mTexture->height * mScale;
        const glm::vec4 tint(255, 255, 255, 255 * std::min(1.0f, (ReflectionLifetime - mElapsed) / ReflectionFadeSeconds));
        if (g->IsInstancePathEnabled()) g->DrawTextureInstanced(mTexture, p.x-w/2, p.y-h/2, w, h, mRotation, tint);
        else g->DrawTexture(mTexture, p.x-w/2, p.y-h/2, w, h, mRotation, tint);
    }
private:
    const Texture* mTexture;
    float mScale, mRotation;
    float mElapsed = 0;
};
}

bool Bullet::TryReflectLobbedAt(int row, float x)
{
    // 按攻击语义决定资格；玉米炮走灰烬路径，直射、已发生的溅射与敌方篮球不拦截。
    switch (mBulletType) {
    case BulletType::BULLET_CABBAGE: case BulletType::BULLET_KERNEL: case BulletType::BULLET_BUTTER:
    case BulletType::BULLET_MELON: case BulletType::BULLET_WINTERMELON:
    case BulletType::BULLET_MELT_SNOW: case BulletType::BULLET_SALT_CRYSTAL:
    case BulletType::BULLET_THUNDER_SEED:
    case BulletType::BULLET_LOTUS_SEED: case BulletType::BULLET_LOTUS_FRAGMENT: break;
    default: return false;
    }
    if (!mBoard || !mBoard->TryInterceptPlantLobbedProjectile(row, x, GetWindAdjustedDamage())) return false;
    GameObjectManager::GetInstance().CreateGameObject<ReflectedLob>(LAYER_GAME_BULLET,
        mTexture, GetPosition(), mScale, mRotationDegrees);
    return true;
}
