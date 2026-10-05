#include "GroundingZombie.h"
#include "ZombieBirthVitalsRules.h"

#include "../../ResourceKeys.h"

namespace {
	constexpr int kGroundingConeHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_GROUNDING).helm;      // 天线路障的一类防具生命
	constexpr float kAntennaAnchorOffsetX = 20.0f; // 天线端点相对 anim_cone 贴图左上原点的缩放后水平像素
	constexpr float kAntennaAnchorOffsetY = 1.0f;  // 天线端点相对 anim_cone 贴图左上原点的缩放后垂直像素
}

void GroundingZombie::SetupZombie()
{
	ConeZombie::SetupZombie();
	mHelmHealth = kGroundingConeHealth;
	mHelmMaxHealth = kGroundingConeHealth;
}

bool GroundingZombie::CanGuideNightRoofCharge() const
{
	return !mIsPreview && IsActive() && !mIsDead && !mIsDying
		&& mBodyHealth > 0 && mHelmType == HelmType::HELMTYPE_TRAFFIC_CONE
		&& mHelmHealth > 0;
}

bool GroundingZombie::TryGetNightRoofChargeGuideAnchor(Vector& anchor) const
{
	if (!CanGuideNightRoofCharge() || !mAnimator
		|| !mAnimator->HasTrack("anim_cone")) return false;
	const float scale = GetTransform()
		? GetTransform()->GetScale() : 1.0f;
	anchor = GetTrackWorldPosition("anim_cone")
		+ Vector(kAntennaAnchorOffsetX * scale, kAntennaAnchorOffsetY * scale);
	return true;
}

const std::string& GroundingZombie::GetConeTextureKey(
	ArmorBrokenState stage) const
{
	using namespace ResourceKeys::Textures;
	if (stage == ArmorBrokenState::A_LITTLE_BROKEN) {
		return IMAGE_ZOMBIE_GROUNDING_CONE2;
	}
	if (stage == ArmorBrokenState::REALLY_BROKEN) {
		return IMAGE_ZOMBIE_GROUNDING_CONE3;
	}
	return IMAGE_ZOMBIE_GROUNDING_CONE1;
}
