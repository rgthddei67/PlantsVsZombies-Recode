#include "AdaptiveHelmetZombie.h"

#include "Game/Board/Board.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>

namespace {
	constexpr int kBodyHealth = 800;                 // 僵尸本体生命
	constexpr int kAdaptiveHelmetHealth = 100;       // 首次来源记录头盔生命
	constexpr float kHelmetOffsetX = -9.0f;          // 宽于原头部贴图时向左回中，单位 px
	constexpr float kHelmetOffsetY = -14.0f;         // 高于原头部贴图时向上回中，单位 px
	constexpr float kHelmetScale = 0.82f;            // 头盔 follower 尺寸倍率
	constexpr float kBadgeOffsetX = -3.0f;           // 胸章相对 Zombie_body 的水平偏移，单位 px
	constexpr float kBadgeOffsetY = 3.0f;            // 胸章相对 Zombie_body 的垂直偏移，单位 px
	constexpr float kBadgeScale = 0.72f;             // 胸章 follower 尺寸倍率
	constexpr const char* kHelmetFollowerSlot =
		"adaptive_helmet";                            // anim_head1 上的静态头盔槽
	constexpr const char* kBadgeFollowerSlot =
		"adaptive_badge";                             // Zombie_body 上的适应状态槽
	constexpr int kTemporalOriginNone = 0;            // 时间锚编码：尚未适应
	constexpr int kTemporalOriginAsh = 1;             // 时间锚编码：灰烬类别
	constexpr int kTemporalOriginPlantBase = 2;       // 时间锚编码：植物谱系起始值

	/** 把完整适应来源编码进目标品种独占解释的 phase 字段。 */
	int EncodeTemporalOrigin(PlantDamageOrigin origin)
	{
		if (origin.kind == PlantDamageOriginKind::ASH) return kTemporalOriginAsh;
		if (origin.kind == PlantDamageOriginKind::PLANT_LINEAGE
			&& origin.lineage >= PlantType::PLANT_PEASHOOTER
			&& origin.lineage < PlantType::NUM_PLANT_TYPES) {
			return kTemporalOriginPlantBase + static_cast<int>(origin.lineage);
		}
		return kTemporalOriginNone;
	}

	/** 解码时间锚中的适应来源；损坏或越界编码安全降级为“未适应”。 */
	PlantDamageOrigin DecodeTemporalOrigin(int encoded)
	{
		if (encoded == kTemporalOriginAsh) return PlantDamageOrigin::Ash();
		const int lineage = encoded - kTemporalOriginPlantBase;
		if (lineage >= static_cast<int>(PlantType::PLANT_PEASHOOTER)
			&& lineage < static_cast<int>(PlantType::NUM_PLANT_TYPES)) {
			return {
				PlantDamageOriginKind::PLANT_LINEAGE,
				static_cast<PlantType>(lineage),
			};
		}
		return {};
	}
}

void AdaptiveHelmetZombie::SetupZombie()
{
	Zombie::SetupZombie();
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mHelmType = HelmType::HELMTYPE_ADAPTIVE;
	mHelmHealth = kAdaptiveHelmetHealth;
	mHelmMaxHealth = kAdaptiveHelmetHealth;
	mAdaptedOrigin = {};
	ConfigureFollowers();
	SyncFollowerPresentation();
}

bool AdaptiveHelmetZombie::BlocksPlantDamage(PlantDamageOrigin origin) const
{
	return origin.IsValid() && mAdaptedOrigin.IsValid() && origin == mAdaptedOrigin;
}

bool AdaptiveHelmetZombie::TryAdaptHelmetToPlantDamage(
	int damage, PlantDamageOrigin origin)
{
	if (!origin.IsValid() || mAdaptedOrigin.IsValid()
		|| mHelmType != HelmType::HELMTYPE_ADAPTIVE || mHelmHealth <= 0
		|| damage < mHelmHealth) {
		return false;
	}

	// 击穿沿用词条缩放后的最终伤害；整击只负责提交适应，不允许溢入 800 点本体。
	mAdaptedOrigin = origin;
	mHelmHealth = 0;
	HelmDrop();
	return true;
}

void AdaptiveHelmetZombie::TakePlantAshDamage(int damage)
{
	const PlantDamageOrigin ashOrigin = PlantDamageOrigin::Ash();
	if (BlocksPlantDamage(ashOrigin)) return;

	// 头盔尚在时必须先走统一承伤链，不能被基类的化灰快路径越过。
	if (mHelmType == HelmType::HELMTYPE_ADAPTIVE && mHelmHealth > 0) {
		TakeDamage(damage, DamageSource::PLANT_ASH,
			false, false, false, ashOrigin);
		return;
	}
	Zombie::TakePlantAshDamage(damage);
}

void AdaptiveHelmetZombie::HelmDrop()
{
	Zombie::HelmDrop();
	SyncFollowerPresentation();
}

void AdaptiveHelmetZombie::HeadDrop()
{
	if (mAnimator && mFollowersConfigured) {
		mAnimator->SetTrackFollowerVisible(
			"anim_head1", kHelmetFollowerSlot, false);
	}
	Zombie::HeadDrop();
}

void AdaptiveHelmetZombie::Die()
{
	if (mAnimator && mFollowersConfigured) {
		mAnimator->SetTrackFollowerVisible(
			"anim_head1", kHelmetFollowerSlot, false);
		mAnimator->SetTrackFollowerVisible(
			"Zombie_body", kBadgeFollowerSlot, false);
	}
	Zombie::Die();
}

void AdaptiveHelmetZombie::ConfigureFollowers()
{
	if (mFollowersConfigured || !mAnimator
		|| !mAnimator->HasTrack("anim_head1")
		|| !mAnimator->HasTrack("Zombie_body")) {
		return;
	}
	const Texture* helmet = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_ADAPTIVE_HELMET, false);
	const Texture* badge = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ZOMBIE_ADAPTIVE_BADGE, false);
	if (!helmet || !badge) return;

	mAnimator->SetTrackFollowerImage("anim_head1", kHelmetFollowerSlot, helmet,
		kHelmetOffsetX, kHelmetOffsetY, kHelmetScale, kHelmetScale,
		/*drawAfterAllTracks=*/true, /*inheritOverlayEffect=*/true,
		/*inheritGlowEffect=*/true);
	// 胸章是适应状态标记，不继承冰冻覆盖色或父轨道受击白光。
	mAnimator->SetTrackFollowerImage("Zombie_body", kBadgeFollowerSlot, badge,
		kBadgeOffsetX, kBadgeOffsetY, kBadgeScale, kBadgeScale,
		/*drawAfterAllTracks=*/true, /*inheritOverlayEffect=*/false,
		/*inheritGlowEffect=*/false);
	mFollowersConfigured = true;
}

void AdaptiveHelmetZombie::SyncFollowerPresentation() const
{
	if (!mFollowersConfigured || !mAnimator) return;
	const bool alive = !mIsDead && !mIsDying;
	mAnimator->SetTrackFollowerVisible("anim_head1", kHelmetFollowerSlot,
		alive && mHasHead && mHelmType == HelmType::HELMTYPE_ADAPTIVE
			&& mHelmHealth > 0);
	mAnimator->SetTrackFollowerVisible("Zombie_body", kBadgeFollowerSlot,
		alive && mAdaptedOrigin.IsValid());
}

void AdaptiveHelmetZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mFollowersConfigured) {
		const_cast<AdaptiveHelmetZombie*>(this)->ConfigureFollowers();
	}
	SyncFollowerPresentation();
}

void AdaptiveHelmetZombie::SaveExtraData(nlohmann::json& j) const
{
	j["adaptedOriginKind"] = static_cast<int>(mAdaptedOrigin.kind);
	j["adaptedOriginLineage"] = static_cast<int>(mAdaptedOrigin.lineage);
}

void AdaptiveHelmetZombie::LoadExtraData(const nlohmann::json& j)
{
	PlantDamageOrigin loaded;
	loaded.kind = static_cast<PlantDamageOriginKind>(std::clamp(
		j.value("adaptedOriginKind", 0),
		static_cast<int>(PlantDamageOriginKind::NONE),
		static_cast<int>(PlantDamageOriginKind::ASH)));
	loaded.lineage = static_cast<PlantType>(std::clamp(
		j.value("adaptedOriginLineage", static_cast<int>(PlantType::NUM_PLANT_TYPES)),
		static_cast<int>(PlantType::PLANT_PEASHOOTER),
		static_cast<int>(PlantType::NUM_PLANT_TYPES)));
	ApplyAdaptedOriginState(loaded);
	ConfigureFollowers();
	SyncFollowerPresentation();
}

bool AdaptiveHelmetZombie::CaptureTemporalAbilityState(
	ZombieTemporalAbilityState& state) const
{
	state.phase = EncodeTemporalOrigin(mAdaptedOrigin);
	state.remaining = 0.0f;
	// “尚未适应”也是必须提交的状态，否则锚定后新获得的免疫无法被清除。
	return true;
}

void AdaptiveHelmetZombie::RestoreTemporalAbilityState(
	const ZombieTemporalAbilityState& state)
{
	ApplyAdaptedOriginState(DecodeTemporalOrigin(state.phase));
	ConfigureFollowers();
	SyncFollowerPresentation();
}

void AdaptiveHelmetZombie::ApplyAdaptedOriginState(PlantDamageOrigin origin)
{
	mAdaptedOrigin = origin.IsValid() ? origin : PlantDamageOrigin{};

	// 核心快照先恢复当时的头盔血量；这里只修复适应来源与防具的原子不变量。
	if (mAdaptedOrigin.IsValid()) {
		mHelmHealth = 0;
		mHelmType = HelmType::HELMTYPE_NONE;
		return;
	}

	// 读档或时间锚已恢复缩放后的上限，不能用出生常量覆盖专属模式/生存倍率。
	if (mHelmMaxHealth <= 0) mHelmMaxHealth = kAdaptiveHelmetHealth;
	if (mHelmType == HelmType::HELMTYPE_ADAPTIVE && mHelmHealth > 0) {
		mHelmHealth = std::clamp(mHelmHealth, 1, mHelmMaxHealth);
	}
	else {
		// 损坏旧档或非法时间锚不能留下“无头盔且无免疫”的弱化组合。
		mHelmHealth = mHelmMaxHealth;
		mHelmType = HelmType::HELMTYPE_ADAPTIVE;
	}
}

bool AdaptiveHelmetZombie::IsAdaptiveHelmetVisible() const
{
	return mFollowersConfigured && mAnimator
		&& mAnimator->GetTrackFollowerVisible("anim_head1", kHelmetFollowerSlot);
}

bool AdaptiveHelmetZombie::IsAdaptedBadgeVisible() const
{
	return mFollowersConfigured && mAnimator
		&& mAnimator->GetTrackFollowerVisible("Zombie_body", kBadgeFollowerSlot);
}
