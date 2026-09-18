#include "FastPaperZombie.h"

namespace {
	constexpr float kFastPaperBaseAnimSpeedMultiplier = 1.5f;	// 加强读报相对普通读报能力倍率的固定增幅
}

void FastPaperZombie::SetupZombie()
{
	// 复用读报僵尸的护盾类型、帧事件（131 死亡 / 85,206 啃食 / 144 狂怒换头+吼叫）与初始动画
	PaperZombie::SetupZombie();

	// 报纸贴图：reanim 默认贴的是 Zombie_paper_paper1，换成 FastZombie_paper_paper1（预览态也要换）
	mAnimator->SetTrackImage("Zombie_paper_paper", ResourceManager::GetInstance().
		GetTexture("IMAGE_FASTZOMBIE_PAPER_PAPER1"));

	// 加强版数值：更厚的报纸 + 更肉的本体（破碎阈值仍是 2/3、1/3，逻辑沿用基类 CheckShieldImage）
	this->mBodyHealth = 350;
	this->mBodyMaxHealth = 350;
	this->mShieldHealth = 700;
	this->mShieldMaxHealth = 700;

	if (mIsPreview) return;

	// 攻击更疼；整体动画能力倍率由虚函数在普通读报状态倍率外再乘 1.5。
	// 狂暴后最终能力倍率为 1.5×1.4=2.1，腿部动画与地面位移一起缩放，不脱节。
	this->mAttackDamage = static_cast<int>(this->mAttackDamage * 1.5f);
}

float FastPaperZombie::GetAbilityAnimSpeedMultiplier() const
{
	return kFastPaperBaseAnimSpeedMultiplier * PaperZombie::GetAbilityAnimSpeedMultiplier();
}

void FastPaperZombie::CheckShieldImage()
{
	// 与 PaperZombie::CheckShieldImage 完全同构，只把报纸破碎贴图换成 FastZombie 版本。
	if (mShieldType == ShieldType::SHIELDTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("Zombie_paper_paper", true);
	mShieldStage = mShieldHealth > static_cast<int64_t>(mShieldMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mShieldHealth > mShieldMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	const char* imageKey = mShieldStage == ArmorBrokenState::NO_BROKEN
		? "IMAGE_FASTZOMBIE_PAPER_PAPER1"
		: (mShieldStage == ArmorBrokenState::A_LITTLE_BROKEN
			? "IMAGE_FASTZOMBIE_PAPER_PAPER2" : "IMAGE_FASTZOMBIE_PAPER_PAPER3");
	mAnimator->SetTrackImage("Zombie_paper_paper",
		ResourceManager::GetInstance().GetTexture(imageKey));
}
