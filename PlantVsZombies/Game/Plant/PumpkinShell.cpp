#include "PumpkinShell.h"

#include "../ShadowComponent.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <cstdint>

namespace {
	constexpr int kPumpkinHealth = 4000;                  // 经典南瓜头基础生命值
	constexpr float kPumpkinColliderWidth = 100.0f;       // 原版 PlantRect 宽度，单位：px
	constexpr float kPumpkinColliderOffsetX = -40.0f;     // 相对逻辑格中心的碰撞框左偏移，单位：px
	constexpr float kPumpkinShadowOffsetY = 23.0f;        // 大外壳脚底阴影相对逻辑中心的垂直偏移，单位：px
	constexpr float kPumpkinShadowScaleX = 1.4f;          // 原版南瓜阴影横向放大倍率
	constexpr float kPumpkinShadowScaleY = 1.05f;         // 保持通用阴影 0.75 纵向压缩后的等比放大
	constexpr float kPumpkinHealthTextDropY = 20.0f;      // 外壳血量相对通用植物下移距离，单位：px
	constexpr const char* kBackTrack = "Pumpkin_back";    // 外壳背片；在普通植物绘制中插入
	constexpr const char* kFrontTrack = "Pumpkin_front";  // 外壳前脸；由南瓜实体本体绘制
}

void PumpkinShell::SetupPlant()
{
	Plant::SetupPlant();
	mPlantHealth = kPumpkinHealth;
	mPlantMaxHealth = kPumpkinHealth;

	if (mCollider) {
		mCollider->size.x = kPumpkinColliderWidth;
		mCollider->offset.x = kPumpkinColliderOffsetX;
	}
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(0.0f, kPumpkinShadowOffsetY));
		shadow->SetScale(Vector(kPumpkinShadowScaleX, kPumpkinShadowScaleY));
	}

	if (mAnimator) {
		// 根 Animator 只画前脸；背片由独立同步 Animator 交给同格普通植物插入。
		mAnimator->SetTrackVisible(kBackTrack, false);
	}
	auto reanim = ResourceManager::GetInstance().GetReanimation(
		ResourceKeys::Reanimations::REANIM_PUMPKIN);
	if (reanim) {
		mBackAnimator = std::make_shared<Animator>(reanim);
		mBackAnimator->SetTrackVisible(kFrontTrack, false);
		mBackAnimator->PlayTrack("anim_idle");
	}
	UpdateDamageTexture();
}

void PumpkinShell::PlantUpdate()
{
	UpdateDamageTexture();
	if (mBackAnimator) {
		mBackAnimator->EnableGlowEffect(mGlowingTimer > 0.0f);
	}
}

void PumpkinShell::Draw(Graphics* g)
{
	const float opacity = mAnimator ? mAnimator->GetCompositeOpacity() : 1.0f;
	const bool compositePreview = g && IsPreview() && opacity < 1.0f;
	if (compositePreview) {
		// 背片是独立 Animator；预览由完整 Draw 拥有合成边界，根动画不再嵌套开组。
		g->BeginOpacityGroup(opacity);
		mAnimator->SetCompositeOpacity(1.0f);
	}
	// 预览没有 Cell；压扁态已经释放 Cell。两者都由自己补画背片。
	if (IsPreview() || IsSquished()) {
		DrawStackBackground(g);
	}
	Plant::Draw(g);
	if (compositePreview) {
		g->EndOpacityGroup();
		mAnimator->SetCompositeOpacity(opacity);
	}
}

void PumpkinShell::DrawStackBackground(Graphics* g)
{
	if (!g || !mBackAnimator || !mAnimator) return;
	mBackAnimator->SetCurrentFrame(mAnimator->GetCurrentFrame());
	mBackAnimator->SetAlpha(mAnimator->GetAlpha());
	mBackAnimator->SetRenderScale(
		mAnimator->GetRenderScaleX(), mAnimator->GetRenderScaleY(),
		mAnimator->GetRenderPivotX(), mAnimator->GetRenderPivotY());
	const Vector position = GetVisualPosition();
	const float scale = GetTransform() ? GetTransform()->GetScale() : 1.0f;
	mBackAnimator->Draw(g, position.x, position.y, scale);
}

Vector PumpkinShell::GetHealthTextOffset() const
{
	return Plant::GetHealthTextOffset() + Vector(0.0f, kPumpkinHealthTextDropY);
}

void PumpkinShell::TakeDamage(int damage, DamageSource source)
{
	Plant::TakeDamage(damage, source);
	if (mPlantHealth <= 0) return;
	if (mBackAnimator) mBackAnimator->EnableGlowEffect(true);
	UpdateDamageTexture();
}

void PumpkinShell::SetImitatedAppearance(bool imitated)
{
	Plant::SetImitatedAppearance(imitated);
	if (!mBackAnimator) return;
	mBackAnimator->EnableWashedOutEffect(imitated);
}

void PumpkinShell::LoadExtraData(const nlohmann::json&)
{
	// 破损阶段完全由通用存档中的生命值派生；读档只恢复终态，不产生音画反馈。
	mDamageStage = -1;
	UpdateDamageTexture();
}

void PumpkinShell::UpdateDamageTexture()
{
	if (!mAnimator) return;
	const int nextStage = mPlantHealth < mPlantMaxHealth / 3
		? 2
		: (mPlantHealth < static_cast<std::int64_t>(mPlantMaxHealth) * 2 / 3 ? 1 : 0);
	if (nextStage == mDamageStage) return;

	const std::string& textureKey = nextStage == 2
		? ResourceKeys::Textures::IMAGE_PUMPKIN_DAMAGE3
		: (nextStage == 1
			? ResourceKeys::Textures::IMAGE_PUMPKIN_DAMAGE1
			: ResourceKeys::Textures::IMAGE_PUMPKIN_FRONT);
	const Texture* texture = ResourceManager::GetInstance().GetTexture(textureKey, false);
	if (!texture) return;

	mDamageStage = nextStage;
	mAnimator->SetTrackImage(kFrontTrack, texture);
}
