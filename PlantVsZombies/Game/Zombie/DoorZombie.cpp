#include "DoorZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../Plant/Plant.h"

namespace {
	constexpr float kMagnetDestinationX = 30.0f; // 铁门吸附到磁力菇头部附近的局部 X
	constexpr float kMagnetDestinationY = 0.0f;  // 铁门吸附到磁力菇头部附近的局部 Y
	constexpr float kMagnetDestinationJitter = 10.0f; // 离体装备落点随机扰动，单位 px
}

void DoorZombie::SetupZombie()
{
	this->ShowArm(false);

	if (mIsPreview) return;

	mAnimator->AddFrameEvent(216, [this]() { this->Die(); });
	mAnimator->AddFrameEvent(152, [this]() { this->EatTarget(); }, true);
	mAnimator->AddFrameEvent(171, [this]() { this->EatTarget(); }, true);

	this->mShieldHealth = 1100;
	this->mShieldMaxHealth = 1100;
	this->mShieldType = ShieldType::SHIELDTYPE_DOOR;
	ApplyDoorImage();

	mSpeed += GameRandom::Range(-3, 3);

	if (GameRandom::Range(0, 1) == 0)
		this->PlayTrack("anim_walk");
	else
		this->PlayTrack("anim_walk2");
}

const char* DoorZombie::GetDoorImageKey(ArmorBrokenState stage) const
{
	switch (stage) {
	case ArmorBrokenState::NO_BROKEN:
		return "IMAGE_ZOMBIE_SCREENDOOR1";
	case ArmorBrokenState::A_LITTLE_BROKEN:
		return "IMAGE_ZOMBIE_SCREENDOOR2";
	case ArmorBrokenState::REALLY_BROKEN:
		return "IMAGE_ZOMBIE_SCREENDOOR3";
	default:
		return nullptr;
	}
}

void DoorZombie::ApplyDoorImage() const
{
	// 预览对象没有运行期 shieldType，但仍要按默认 NO_BROKEN 阶段显示门；真正掉门后 stage=NONE。
	if (!mAnimator || mShieldStage == ArmorBrokenState::NONE) return;
	const char* imageKey = GetDoorImageKey(mShieldStage);
	if (!imageKey) return;
	if (const Texture* texture = ResourceManager::GetInstance().GetTexture(imageKey)) {
		mAnimator->SetTrackImage("anim_screendoor", texture);
	}
}

void DoorZombie::OnStartEating()
{
	// 残端造型优先于啃食露臂（与 ZombieItemUpdate 的优先级一致）：穿透伤害可在门未掉时掉头
	// （HeadDrop→ShowBrokenArm，mHasArm 仍为 true），无头僵尸流血期间仍可能开吃，
	// 此处若 ShowArm(true) 会把 hand/lower 重新点亮——断臂复活。
	if (!mHasArm || !mHasHead) return;
	// 啃食露出常规手臂（门还在才露）。与 OnStopEating 对称——修复"啃敌方僵尸时无手臂"(52bd86d)。
	if (mShieldType != ShieldType::SHIELDTYPE_NONE)
		this->ShowArm(true);
}

void DoorZombie::OnStopEating()
{
	// 残端造型优先：断臂/掉头后停吃若 ShowArm(false) 会把残端（innerarm+upper）也藏掉，变成完全无臂。
	if (!mHasArm || !mHasHead) return;
	// 收尾把常规手臂藏回门后（门还在才藏）。与 OnStartEating 对称——修复"被魅惑后多一条手臂"(c0cb799)。
	if (mShieldType != ShieldType::SHIELDTYPE_NONE)
		this->ShowArm(false);
}

void DoorZombie::ShowArm(bool show) const
{
	if (!mAnimator) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", show);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", show);
	mAnimator->SetTrackVisible("Zombie_outerarm_upper", show);
	mAnimator->SetTrackVisible("anim_innerarm1", show);
	mAnimator->SetTrackVisible("anim_innerarm2", show);
	mAnimator->SetTrackVisible("anim_innerarm3", show);
}

void DoorZombie::ShowBrokenArm() const
{
	if (!mAnimator) return;
	// 该亮的：内臂 + 外上臂；该灭的：外手掌 + 外下臂（断臂只剩上臂残端）。
	mAnimator->SetTrackVisible("anim_innerarm1", true);
	mAnimator->SetTrackVisible("anim_innerarm2", true);
	mAnimator->SetTrackVisible("anim_innerarm3", true);
	mAnimator->SetTrackVisible("Zombie_outerarm_upper", true);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_outerarm_upper", ResourceManager::GetInstance().
		GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_OUTERARM_UPPER2));

	// 断臂即抓不住门：把「持门手臂」一并隐藏。正常死亡时门先掉、ShieldDrop 已隐藏它们（此处无害 no-op）；
	// 但大喷菇穿透会在门未掉时打死本体（掉头 HeadDrop 走到这里），若不隐藏就残留一条完好的持门手臂。
	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_screendoor", false);
	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor_hand", false);
}

void DoorZombie::CheckShieldImage()
{
	if (mShieldType == ShieldType::SHIELDTYPE_NONE) return;
	// 回溯可恢复已经掉落的门，同时撤销 ShieldDrop 留下的隐藏覆盖。
	mAnimator->SetTrackVisible("anim_screendoor", true);
	mShieldStage = mShieldHealth > static_cast<int64_t>(mShieldMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mShieldHealth > mShieldMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	ApplyDoorImage();
}

void DoorZombie::ShieldDrop()
{
	Zombie::ShieldDrop();
	mShieldStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_screendoor", false);

	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_screendoor", false);
	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor_hand", false);
	
	this->ShowArm(true);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect(GetDoorDropParticleName(),
			GetPosition());
	}
} 

bool DoorZombie::HasMagneticItem() const
{
	return mShieldType == ShieldType::SHIELDTYPE_DOOR;
}

bool DoorZombie::ExtractMagneticItem(MagneticItem& item)
{
	if (!HasMagneticItem()) return false;
	const char* imageKey = GetDoorImageKey(mShieldStage);
	if (!imageKey) return false;
	item.textureKey = imageKey;
	item.worldPosition = GetTrackWorldPosition("anim_screendoor");
	item.destinationOffset = Vector(
		kMagnetDestinationX + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter),
		kMagnetDestinationY + GameRandom::Range(-kMagnetDestinationJitter, kMagnetDestinationJitter));
	item.drawScale = 0.8f;

	// 磁吸是无掉落粒子的卸装路径；门的后续能力与手臂表现仍复用掉门终态。
	mShieldHealth = 0;
	Zombie::ShieldDrop();
	mShieldStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("anim_screendoor", false);
	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_screendoor", false);
	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor_hand", false);
	ShowArm(true);
	return true;
}

void DoorZombie::TakeBodyDamage(int damage)
{
	mBodyHealth -= damage;
	if (mBodyHealth < 0)
		mBodyHealth = 0;

	if (mNeedDropArm && mHasArm && mShieldType == ShieldType::SHIELDTYPE_NONE 
		&& mBodyHealth <= static_cast<int64_t>(mBodyMaxHealth) * 2 / 3)
	{
		ArmDrop();
		mHasArm = false;
	}

	if (mNeedDropHead && mHasHead && mBodyHealth <= mBodyMaxHealth / 3)
	{
		HeadDrop();
		mHasHead = false;
	}
}

void DoorZombie::HeadDrop()
{
	if (!mHasHead) return;

	// 显示手臂，并且让它为断裂的造型
	if (mHasArm) {
		this->ShowBrokenArm();
	}

	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	g_particleSystem->EmitEffect("ZombieHeadOff",
		GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void DoorZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	if (!mAnimator || !mHasArm || !mHasHead) return;
	mAnimator->SetTrackImage("Zombie_outerarm_upper", nullptr);
	const bool shieldGone = mShieldType == ShieldType::SHIELDTYPE_NONE;
	ShowArm(shieldGone || mIsEating);
	// 持门手臂与恢复后的门成套显隐，既撤销掉落覆盖，也保留无门快照的空手姿势。
	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor", !shieldGone);
	mAnimator->SetTrackVisible("Zombie_outerarm_screendoor", !shieldGone);
	mAnimator->SetTrackVisible("Zombie_innerarm_screendoor_hand", !shieldGone);
}
