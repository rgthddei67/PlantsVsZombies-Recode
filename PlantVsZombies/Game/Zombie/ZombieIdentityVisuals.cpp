#include "ZombieIdentityVisuals.h"
#include "Zombie.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include "ParticleSystem/ParticleSystem.h"
#include <cmath>
#include <cctype>

namespace {
	constexpr const char* kHeadgearSlot = "identity_headgear";
	constexpr float kHeadCenterX = 28.0f; // 80x90 离体头画布中心相对头轨原点的 X，像素
	constexpr float kHeadCenterY = 15.0f; // 头轨在画布 (12,30)，含随头脱落的头饰，像素
	constexpr float kArmCenterX = 9.0f; // 32x55 离体臂中心相对下臂轨的 X，像素
	constexpr float kArmCenterY = 24.5f; // 下臂在画布 (7,3)，手掌与袖口重叠，像素
	struct DetachedPose {
		Vector center;
		float scaleX, scaleY, rotation;
		bool mirrored;
	};

	/** 使用最终插值矩阵求画布中心与方向，统一处理魅惑和对象整体缩放。 */
	DetachedPose GetDetachedPose(const Zombie& zombie, const std::string& track, const Vector& centerInTrack)
	{
		const auto animator = zombie.GetAnimatorInternal();
		const auto matrix = animator->GetTrackLocalTransform(track);
		const auto local = matrix * glm::vec4(centerInTrack.x, centerInTrack.y, 0, 1);
		const float scale = zombie.GetAnimationScale();
		const Vector world = zombie.GetVisualPosition() + Vector(local.x, local.y) * scale;
		const float renderX = animator->GetRenderScaleX(), renderY = animator->GetRenderScaleY();
		const Vector center(animator->GetRenderPivotX() + (world.x - animator->GetRenderPivotX()) * renderX,
			animator->GetRenderPivotY() + (world.y - animator->GetRenderPivotY()) * renderY);
		float xx = matrix[0][0] * scale * renderX, xy = matrix[0][1] * scale * renderY;
		const float yx = matrix[1][0] * scale * renderX, yy = matrix[1][1] * scale * renderY;
		const bool mirrored = xx * yy - xy * yx < 0;
		if (mirrored) { xx = -xx; xy = -xy; }
		return {center, std::hypot(xx, xy), std::hypot(yx, yy), glm::degrees(std::atan2(xy, xx)), mirrored};
	}

}

namespace ZombieIdentityVisuals {

const Profile* Find(ZombieType type)
{
	// 头饰局部像素偏移与 generate_zombie_identity_assets.ps1 的离体合成共用约定。
	static constexpr Profile worker{"worker", "IceWorkerZombie", true, 0, -18}; // 帽檐压入额头并保留眼部空间，像素
	static constexpr Profile boiler{"boiler", "BoilerZombie", true, -2, -4}; // 额头护目镜安装偏移，像素
	static constexpr Profile jammer{"jammer", "WeatherJammerZombie", false, 0, 0};
	static constexpr Profile healer{"healer", "HealerZombie", true, -8, -24}; // 急救帽安装偏移，像素
	static constexpr Profile sludge{"sludge", "SludgeZombie", false, 0, 0};
	static constexpr Profile guard{"guard", "PressureGuardZombie", false, 0, 0};
	switch (type) {
	case ZombieType::ZOMBIE_ICE_WORKER: return &worker;
	case ZombieType::ZOMBIE_BOILER: return &boiler;
	case ZombieType::ZOMBIE_WEATHER_JAMMER: return &jammer;
	case ZombieType::ZOMBIE_HEALER: return &healer;
	case ZombieType::ZOMBIE_SLUDGE: return &sludge;
	case ZombieType::ZOMBIE_PRESSURE_GUARD: return &guard;
	default: return nullptr;
	}
}

std::string TextureKey(const Profile& profile, const char* part)
{
	std::string key = std::string("IMAGE_IDENTITY_") + profile.stem + "_" + part;
	for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return key;
}

std::string EffectName(const Profile& profile, bool head, bool mirrored)
{
	return std::string("Identity_") + profile.stem + (head ? "_head" : "_arm")
		+ (mirrored ? "_right" : "");
}

bool ResourcesReady(const Profile& profile)
{
	auto& resources = ResourceManager::GetInstance();
	if (!resources.HasReanimation(profile.reanimation)) return false;
	for (const char* part : {"body", "innerarm_upper", "innerarm_lower", "outerarm_upper",
		"outerarm_upper2", "outerarm_lower", "innerleg_upper", "innerleg_lower",
		"outerleg_upper", "outerleg_lower", "tie"}) {
		if (!resources.GetTexture(TextureKey(profile, part), false)) return false;
	}
	if (profile.hasHeadgear && !resources.GetTexture(TextureKey(profile, "headgear"), false)) return false;
	for (bool head : {false, true}) for (bool mirrored : {false, true}) {
		std::string key = "PARTICLE_" + EffectName(profile, head, mirrored);
		for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if (!resources.GetTexture(key, false)) return false;
	}
	return true;
}

}

void Zombie::ConfigureIdentityVisuals() const
{
	const auto* profile = ZombieIdentityVisuals::Find(mZombieType);
	if (!profile) return;
	if (profile->hasHeadgear) {
		mAnimator->SetTrackFollowerImage("anim_head1", kHeadgearSlot,
			ResourceManager::GetInstance().GetTexture(ZombieIdentityVisuals::TextureKey(*profile, "headgear"), false),
			profile->headgearX, profile->headgearY, 1, 1, true, true, true);
		mAnimator->SetTrackFollowerVisible("anim_head1", kHeadgearSlot, mHasHead);
		mAnimator->SetTrackVisible("anim_hair", false);
	}
	// 旧存档中可能保留普通断臂材质；按当前生命状态重建，不能把旧 override 带回来。
	mAnimator->SetTrackImage("Zombie_outerarm_upper", mHasArm ? nullptr : GetArmStumpTexture());
}

const Texture* Zombie::GetArmStumpTexture() const
{
	const auto* profile = ZombieIdentityVisuals::Find(mZombieType);
	return ResourceManager::GetInstance().GetTexture(profile
		? ZombieIdentityVisuals::TextureKey(*profile, "outerarm_upper2")
		: ResourceKeys::Textures::IMAGE_ZOMBIE_OUTERARM_UPPER2, false);
}

bool Zombie::EmitIdentityLimb(bool head) const
{
	const auto* profile = ZombieIdentityVisuals::Find(mZombieType);
	if (!profile) return false;
	const auto pose = GetDetachedPose(*this, head ? "anim_head1" : "Zombie_outerarm_lower",
		Vector(head ? kHeadCenterX : kArmCenterX, head ? kHeadCenterY : kArmCenterY));
	if (g_particleSystem) {
		g_particleSystem->EmitDetachedPart(ZombieIdentityVisuals::EffectName(*profile, head, pose.mirrored),
			pose.center, pose.scaleX, pose.scaleY, pose.rotation, {}, mAnimator->GetActiveOverlayColor());
	}
	return true;
}

void Zombie::EmitArmorDrop(const std::string& effectName, const std::string& trackName,
	const std::string& textureKey) const
{
	const auto* texture = ResourceManager::GetInstance().GetTexture(textureKey);
	if (!texture || !g_particleSystem) return;
	const auto pose = GetDetachedPose(*this, trackName, Vector(texture->width * .5f, texture->height * .5f));
	g_particleSystem->EmitDetachedPart(effectName, pose.center, pose.scaleX, pose.scaleY, pose.rotation,
		textureKey + (pose.mirrored ? "_MIRRORED" : ""), mAnimator->GetActiveOverlayColor());
}
