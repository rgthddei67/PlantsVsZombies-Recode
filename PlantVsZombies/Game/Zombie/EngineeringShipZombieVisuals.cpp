#include "EngineeringShipZombie.h"
#include "Game/Board/Board.h"
#include "Graphics.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float CoreX = 115.0f; // 炉芯相对完整船体美术原点的横坐标，像素
constexpr float CoreY = 80.0f; // 炉芯相对完整船体美术原点的纵坐标，像素
constexpr float HatchSlide = 15.0f; // 两片舱盖完全开启时各自向外滑动的距离，像素
constexpr float HatchOpenSeconds = .3f; // 散热开始后的舱盖机械开启动画，游戏秒
constexpr float WarningSize = 55.0f; // 每个危险格的瞄准标记直径，像素

/** 普通贴图与 reanim 使用同一有序实例流，兼容关闭实例化的既有绘制路径。 */
void DrawSprite(Graphics* graphics, const Texture* texture, float x, float y, float width,
    float height, const glm::vec4& color = glm::vec4(255), float rotation = 0.0f)
{
    if (graphics->IsInstancePathEnabled())
        graphics->DrawTextureInstanced(texture, x, y, width, height, rotation, color);
    else graphics->DrawTexture(texture, x, y, width, height, rotation, color);
}

/** 先在世界空间旋转，再缩放炮流矩形，保持右端喷口与左端落水点准确连接。 */
void DrawWaterJet(Graphics* graphics, const Texture* texture, const Vector& nozzle,
    const Vector& impact, float width, const glm::vec4& color)
{
    const float dx = nozzle.x - impact.x;
    const float dy = nozzle.y - impact.y;
    const float length = std::max(1.0f, std::hypot(dx, dy));
    const float angle = std::atan2(dy, dx) * 57.2957795f;
    graphics->PushTransform();
    graphics->Translate(nozzle.x, nozzle.y);
    graphics->Rotate(angle);
    DrawSprite(graphics, texture, -length, -width * .55f, length, width, color);
    graphics->PopTransform();
}
}

/** 舱盖、炮管与驾驶员由正式阶段投影；覆盖色、受击白光和水线仍由 Animator 统一管理。 */
void EngineeringShipZombie::SyncVisualState() const
{
    const bool sinking = mPhase == Phase::SINKING;
    // 开门只跟本次炮击余波，半血缩短散热余时不会重新关闭已经打开的舱盖。
    // 被警铃打断或读档恢复时没有炮击余波，直接保持开舱终态。
    const float opened = sinking ? 1.0f : mPhase == Phase::COOLING
        ? (mShotVisualRemaining > 0.0f
            ? std::clamp((EngineeringShipRules::ShotVisualSeconds - mShotVisualRemaining) / HatchOpenSeconds, 0.0f, 1.0f)
            : 1.0f) : 0.0f;
    const float death = sinking ? 1.0f - std::clamp(mPhaseRemaining / EngineeringShipRules::SinkingSeconds, 0.0f, 1.0f) : 0.0f;
    const float charge = mPhase == Phase::CHARGING
        ? 1.0f - std::clamp(mPhaseRemaining / EngineeringShipRules::ChargeSeconds, 0.0f, 1.0f) : 0.0f;
    const float recoil = mShotVisualRemaining > 0.0f
        ? 5.0f * mShotVisualRemaining / EngineeringShipRules::ShotVisualSeconds : 0.0f;
    const float pressureShake = charge * .7f * std::sin(mPhaseRemaining * 36.0f);
    const float bob = mInPool && !sinking ? .8f * std::sin(mAnimator->GetCurrentFrame() * .261799f) : 0.0f;
    mAnimator->SetTrackOffset("Zombie_body", 0, bob);
    mAnimator->SetTrackOffset("anim_head1", death * 5.0f, bob + death * 12.0f);
    mAnimator->SetTrackOffset("ship_cannon", recoil + death * 12.0f, bob + pressureShake + death * 8.0f);
    mAnimator->SetTrackVisible("ship_hatch", false);
    // follower 坐标位于二倍分辨率船壳轨内，统一乘该轨道自身的0.5缩放。
    auto& resources = ResourceManager::GetInstance();
    mAnimator->SetTrackImage("Zombie_body", IsEnraged() || sinking
        ? resources.GetTexture("IMAGE_ENGINEERING_SHIP_BODY_DAMAGED", false) : nullptr);
    mAnimator->SetTrackFollowerImage("Zombie_body", "engineering_hatch_left",
        resources.GetTexture("IMAGE_ENGINEERING_SHIP_HATCH_LEFT", false),
        194.0f - opened * HatchSlide * 2.0f, 38.0f, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerImage("Zombie_body", "engineering_hatch_right",
        resources.GetTexture("IMAGE_ENGINEERING_SHIP_HATCH_RIGHT", false),
        231.0f + opened * HatchSlide * 2.0f, 38.0f, 1, 1, false, true, true);
    mAnimator->SetTrackFollowerVisible("Zombie_body", "engineering_hatch_left", true);
    mAnimator->SetTrackFollowerVisible("Zombie_body", "engineering_hatch_right", true);
    const SDL_Color hull = sinking ? SDL_Color{150, 140, 135, 255}
        : IsEnraged() ? SDL_Color{235, 207, 185, 255} : SDL_Color{255, 255, 255, 255};
    mAnimator->SetTrackColor("Zombie_body", hull);
    mAnimator->SetTrackColor("ship_cannon", hull);
    mAnimator->SetAlpha(sinking ? std::clamp((1.0f - death) * 1.5f, 0.0f, 1.0f) : 1.0f);
}

/** 危险格、炮流与蒸汽读取同一锁格和阶段余时，不额外推进或延迟技能。 */
void EngineeringShipZombie::Draw(Graphics* graphics)
{
    if (!graphics) return;
    Zombie::Draw(graphics);
    if (mIsPreview || !mBoard || mIsDead) return;

    auto& resources = ResourceManager::GetInstance();
    const Texture* spray = resources.GetTexture("IMAGE_ENGINEERING_SHIP_SPLASH", false);
    const Texture* waterjet = resources.GetTexture("IMAGE_ENGINEERING_SHIP_WATERJET", false);
    const Texture* glow = resources.GetTexture(ResourceKeys::Textures::IMAGE_GLOWING, false);
    const float scale = GetAnimationScale();
    const Vector origin = GetVisualPosition();
    const Vector core = origin + Vector(CoreX, CoreY) * scale;
    const Vector nozzle = GetRenderedTrackWorldPosition("ship_cannon") + Vector(2, 22) * scale;

    if (mPhase == Phase::CHARGING && mTargetRow >= 0 && mTargetColumn >= 0) {
        const float progress = 1.0f - std::clamp(mPhaseRemaining / EngineeringShipRules::ChargeSeconds, 0.0f, 1.0f);
        const float pulse = .7f + .3f * std::sin(mPhaseRemaining * (9.0f + progress * 8.0f));
        const Texture* target = resources.GetTexture(ResourceKeys::Textures::IMAGE_COBCANNON_TARGET, false);
        for (int column = mTargetColumn; column >= std::max(0, mTargetColumn - EngineeringShipRules::CannonCells + 1); --column) {
            const Vector center = mBoard->GetCellCenterPosition(mTargetRow, column);
            const float size = WarningSize + 5.0f * progress;
            DrawSprite(graphics, target, center.x - size * .5f, center.y - size * .5f,
                size, size, glm::vec4(255, 210 - 90 * progress, 115 - 80 * progress, 120 + 100 * pulse));
            DrawSprite(graphics, spray, center.x - 25, center.y + 13, 50, 15,
                glm::vec4(100, 205, 255, 35 + 35 * progress));
        }
        const Vector center = mBoard->GetCellCenterPosition(mTargetRow, mTargetColumn);
        graphics->DrawGlyphRun("水炮 " + std::to_string(std::max(1, static_cast<int>(std::ceil(mPhaseRemaining)))),
            ResourceKeys::Fonts::FONT_FZJZ, 15, glm::vec4(255, 229, 128, 255), center.x - 23, center.y - 49);
        const float size = 14 + 11 * progress;
        DrawSprite(graphics, glow, nozzle.x - size * .5f, nozzle.y - size * .5f, size, size,
            glm::vec4(95, 216, 255, 100 + 90 * pulse));
        DrawSprite(graphics, spray, nozzle.x - size, nozzle.y - size * .35f, size, size * .7f,
            glm::vec4(170, 235, 255, 100 + 85 * progress));
    }

    if (mShotVisualRemaining > 0.0f && mTargetRow >= 0 && mTargetColumn >= 0) {
        const float remaining = std::clamp(mShotVisualRemaining / EngineeringShipRules::ShotVisualSeconds, 0.0f, 1.0f);
        const int rear = std::max(0, mTargetColumn - EngineeringShipRules::CannonCells + 1);
        const Vector impact = mBoard->GetCellCenterPosition(mTargetRow, rear) + Vector(-12, -15);
        const float height = 48 + 30 * (1.0f - remaining);
        DrawWaterJet(graphics, waterjet, nozzle, impact, height,
            glm::vec4(235, 255, 255, 255 * std::min(1.0f, remaining * 2)));
        for (int column = rear; column <= mTargetColumn; ++column) {
            const Vector center = mBoard->GetCellCenterPosition(mTargetRow, column);
            const float size = 45 + (1.0f - remaining) * 34;
            DrawSprite(graphics, spray, center.x - size * .5f, center.y - size * .6f,
                size, size * .8f, glm::vec4(210, 246, 255, 215 * remaining));
        }
    }

    if (mPhase == Phase::COOLING) {
        const float pulse = .6f + .4f * std::sin(mPhaseRemaining * 6.0f);
        DrawSprite(graphics, glow, core.x - 21 * scale, core.y - 21 * scale, 42 * scale, 42 * scale,
            glm::vec4(255, 105 + 50 * pulse, 35, 80 + 70 * pulse));
        for (int index = 0; index < 3; ++index) {
            const float travel = std::fmod(GetCoolingDuration() - mPhaseRemaining + index * .62f, 1.8f) / 1.8f;
            const float size = (12 + travel * 22) * scale;
            DrawSprite(graphics, spray, core.x - size * .5f + std::sin(index + travel * 3) * 6 * scale,
                core.y - 12 * scale - travel * 32 * scale, size, size * .7f,
                glm::vec4(210, 240, 244, 115 * (1.0f - travel)));
        }
    }

    if (IsEnraged() && mPhase != Phase::SINKING) {
        const float travel = std::fmod(mAnimator->GetCurrentFrame() / 12.0f, 1.7f) / 1.7f;
        const float size = (13 + travel * 20) * scale;
        DrawSprite(graphics, spray, origin.x + 142 * scale - size * .5f,
            origin.y + (40 - travel * 34) * scale, size, size * .8f,
            glm::vec4(90, 77, 66, 115 * (1 - travel)));
    }

    if (mPhase == Phase::SINKING) {
        const float progress = 1.0f - std::clamp(mPhaseRemaining / EngineeringShipRules::SinkingSeconds, 0.0f, 1.0f);
        const float flash = std::max(0.0f, 1.0f - progress * 4.0f);
        DrawSprite(graphics, glow, core.x - 70, core.y - 70, 140, 140,
            glm::vec4(255, 175, 65, 230 * flash));
        for (int index = 0; index < 4; ++index) {
            const float size = 30 + progress * 65;
            const float x = origin.x + (30 + index * 36) * scale;
            const float y = origin.y + 78 * scale - progress * (15 + index * 7);
            DrawSprite(graphics, spray, x - size * .5f, y - size * .5f, size, size * .8f,
                mInPool ? glm::vec4(220, 249, 255, 185 * (1 - progress))
                    : glm::vec4(170, 135, 90, 180 * (1 - progress)));
        }
    }
}
