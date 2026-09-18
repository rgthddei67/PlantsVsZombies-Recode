#include "PaperZombie.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../Plant/Plant.h"

namespace {
	// 狂暴（掉报纸）后奔跑轨道 anim_walk_nopaper 的 clip 速度。
	// EffectiveSpeed = clip * extra(狂暴后 extra=1.4) → 越大腿越快、地面位移也越快（GetTrackVelocity 按 EffectiveSpeed 缩放）。
	constexpr float kNoPaperWalkClip = 4.5f;
	constexpr float kNoPaperEatClip = 3.5f;   // 狂暴啃食 anim_eat_nopaper 的 clip 速度（StartEat / 读档复用）
	constexpr float kPaperRageAnimSpeedMultiplier = 1.4f;	// 掉报纸后的整体动画能力倍率
}

void PaperZombie::SetupZombie()
{
	this->mBodyHealth = 300;
	this->mBodyMaxHealth = 300;
	this->mShieldType = ShieldType::SHIELDTYPE_NEWSPAPER;
	this->mShieldHealth = 500;
	this->mShieldMaxHealth = 500;

	if (!mIsPreview) {
		PlayTrack("anim_walk");

		mAnimator->AddFrameEvent(131, [this]() {
			this->Die();
			});
		mAnimator->AddFrameEvent(85, [this]() {
			this->EatTarget();
			}, true);
		mAnimator->AddFrameEvent(206, [this]() {
			this->EatTarget();
			}, true);

		mAnimator->AddFrameEvent(144, [this]() {
			this->mIsGasp = false;
			this->mAnimator->SetTrackImage("anim_head1", ResourceManager::GetInstance().
				GetTexture("IMAGE_ZOMBIE_PAPER_MADHEAD"));

			if (GameRandom::Chance()) {
				AudioSystem::PlaySound("SOUND_NEWSPAPER_RARRGH", 0.3f);
			}
			else {
				AudioSystem::PlaySound("SOUND_NEWSPAPER_RARRGH2", 0.3f);
			}

			});
	}
	else {
		PlayTrack("anim_idle");
	}
}

void PaperZombie::CheckShieldImage()
{
	if (mShieldType == ShieldType::SHIELDTYPE_NONE) return;
	// 耐久恢复可能跨越掉甲边沿，换图时一并撤销旧的隐藏覆盖。
	mAnimator->SetTrackVisible("Zombie_paper_paper", true);
	mShieldStage = mShieldHealth > static_cast<int64_t>(mShieldMaxHealth) * 2 / 3
		? ArmorBrokenState::NO_BROKEN
		: (mShieldHealth > mShieldMaxHealth / 3
			? ArmorBrokenState::A_LITTLE_BROKEN : ArmorBrokenState::REALLY_BROKEN);
	const char* imageKey = mShieldStage == ArmorBrokenState::NO_BROKEN
		? "IMAGE_ZOMBIE_PAPER_PAPER1"
		: (mShieldStage == ArmorBrokenState::A_LITTLE_BROKEN
			? "IMAGE_ZOMBIE_PAPER_PAPER2" : "IMAGE_ZOMBIE_PAPER_PAPER3");
	mAnimator->SetTrackImage("Zombie_paper_paper",
		ResourceManager::GetInstance().GetTexture(imageKey));
}

void PaperZombie::ShieldDrop()
{
	// 原版撕报会原子中断 Garlic 嫌恶态；先清脸与旧啃食目标，再进入 gasp 狂暴演出。
	CancelGarlicRedirect(true);
	Zombie::ShieldDrop();
	mShieldStage = ArmorBrokenState::NONE;
	mAnimator->SetTrackVisible("Zombie_paper_paper", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombiePaperOff",
			GetPosition());
	}

	AudioSystem::PlaySound("SOUND_NEWSPAPER_RIP", 0.3f);

	mHasNewspaper = false;
	mIsGasp = true;

	// 掉报纸后进入狂奔：位移固定项 ×1.8；奔跑快慢主要由 anim_walk_nopaper 的 clip 决定（逐轨道控制）。
	mSpeed *= 1.35f;
	mAttackDamage *= 2;

	// 狂暴能力倍率由 GetAbilityAnimSpeedMultiplier 按持报纸状态派生，与逐轨道 clip 正交。
	// 经 UpdateAnimSpeed 收敛：减速因子照算，冻结中狂暴不解除停格（解冻后新倍率自然生效）。
	UpdateAnimSpeed();

	// gasp 播完后的回切轨道：若狂暴前已在啃食则回到啃食，否则狂奔。
	// （修复"愤怒前已接触植物 → 愤怒后站着走路不吃"：StartEat 的 mEatPlantID 守卫会让它再也补不回啃食动画，
	//   所以这里直接把回切目标设成啃食。）第3参=gasp 速度(0→base)，第5参=回切轨道 clip。
	if (mIsEating)
		PlayTrackOnce("anim_gasp", "anim_eat_nopaper", 0.0f, 0.05f, kNoPaperEatClip);
	else
		PlayTrackOnce("anim_gasp", "anim_walk_nopaper", 0.0f, 0.05f, kNoPaperWalkClip);
}

void PaperZombie::OnTemporalCoreStateRestored()
{
	Zombie::OnTemporalCoreStateRestored();
	if (mShieldType != ShieldType::SHIELDTYPE_NEWSPAPER || mHasNewspaper) return;

	// ShieldDrop 的狂暴倍率只撤销一次；保留词条、出生倍率和夹具设置的其余数值。
	mHasNewspaper = true;
	mIsGasp = false;
	mSpeed /= 1.35f;
	mAttackDamage /= 2;
	RestoreHeadImageAfterGarlic();
	if (mIsEating) PlayTrack("anim_eat", 2.1f, 0.0f);
	else PlayWalkAnimation(0.0f);
}

void PaperZombie::LoadExtraData(const nlohmann::json& j)
{
	mShieldStage = static_cast<ArmorBrokenState>(
		j.value("shieldStage", static_cast<int>(ArmorBrokenState::NO_BROKEN)));
	mHasNewspaper = j.value("hasNewspaper", true);
	mIsGasp = j.value("isGasp", false);
	// LoadProtectedData 早于派生状态恢复；持报纸状态就位后重新合成能力、寒冰与雨势倍率。
	UpdateAnimSpeed();

	if (!mAnimator) return;

	// 走路/啃食动画的恢复由 GameInfoSaver 全局负责（animTrack+animFrame+animClipSpeed 原样恢复，
	// 再加一遍全局 ValidateEatingState 兜底“植物没了→走路”）。extra 速度层也已在基类 LoadProtectedData 恢复。
	// 这里只补两件全局兜不住的事：
	if (!mHasNewspaper) {
		// (1) 狂怒头图：144 帧换头事件读档后不会再触发，手动贴。
		mAnimator->SetTrackImage("anim_head1", ResourceManager::GetInstance().
			GetTexture("IMAGE_ZOMBIE_PAPER_MADHEAD"));

		// (2) Bug1：存档卡在 gasp 时 animTrack==anim_gasp 会被当循环轨道恢复而永远卡死。
		//     仅此情形需要接管——跳过 gasp，按是否在啃食直接定格（粗糙处理）。
		if (mIsGasp) {
			mIsGasp = false;
			if (mIsEating)
				PlayTrack("anim_eat_nopaper", kNoPaperEatClip, 0.0f);
			else
				PlayWalkAnimation(0.0f);   // 无报纸态经统一走路权威（等价 anim_walk_nopaper+clip）
		}
	}
}

void PaperZombie::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head_pupils", false);
	mAnimator->SetTrackVisible("anim_head_look", false);
	mAnimator->SetTrackVisible("anim_hairpiece", false);
	mAnimator->SetTrackVisible("anim_head_jaw", false);
	mAnimator->SetTrackVisible("anim_head_glasses", false);
	mAnimator->SetTrackVisible("anim_hair", false);

	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieHeadOff",
			GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void PaperZombie::ArmDrop()
{
	if (!mHasArm) return;
	mAnimator->SetTrackVisible("Zombie_paper_hands", false);
	mAnimator->SetTrackVisible("Zombie_paper_leftarm_lower", false);
	mAnimator->SetTrackImage("Zombie_paper_leftarm_upper", ResourceManager::GetInstance().
		GetTexture("IMAGE_ZOMBIE_PAPER_LEFTARM_UPPER2"));

	if (g_particleSystem) {
		g_particleSystem->EmitEffect("PaperZombieArmOff",
			GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void PaperZombie::StartEat(ColliderComponent* other)
{
	if (mIsPreview || mIsDying || IsGarlicRedirecting())	return;
	if (other->GetGameObject()->GetObjectType() == ObjectType::OBJECT_ZOMBIE) {
		Zombie::StartEat(other);
		return;
	}
	if (mIsGasp) return;   // 狂怒喘气期间不开吃，避免新触发的啃食动画盖掉 gasp
	auto* gameObject = other->GetGameObject();
	if (gameObject->GetObjectType() == ObjectType::OBJECT_PLANT)
	{
		if (auto* plant = dynamic_cast<Plant*>(gameObject))
		{
			if (!IsPlantValidEatTarget(plant)) return;
			if (mEatPlantID != NULL_PLANT_ID || plant->mRow != this->mRow) return;	// 正在吃一个植物，那么不吃别的植物

			if (!mIsEating) {
				if (mHasNewspaper)
					this->PlayTrack("anim_eat", 2.1f, 0.2f);
				else
					this->PlayTrack("anim_eat_nopaper", kNoPaperEatClip, 0.2f);
				OnStartEating();   // 契约：开吃即触发（纸僵尸自身 no-op，保钩子普适；植物分支不调基类故须显式补）
			}
			mIsEating = true;
			mEatPlantID = plant->mPlantID;
			plant->mEaterCount++;
		}
	}
}

void PaperZombie::PlayWalkAnimation(float blendTime)
{
	// PaperZombie.reanim 无 anim_walk2；有报纸走 anim_walk，狂暴无报纸走 anim_walk_nopaper 且带 clip 速度。
	if (mHasNewspaper)
		PlayTrack("anim_walk", 0.0f, blendTime);
	else
		PlayTrack("anim_walk_nopaper", kNoPaperWalkClip, blendTime);
}

void PaperZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (!mIsGasp) {
		Zombie::ZombieMove(scaledDelta, transform);
	}
}

void PaperZombie::RestoreHeadImageAfterGarlic()
{
	if (!mAnimator) return;
	mAnimator->SetTrackImage("anim_head1", mHasNewspaper
		? nullptr
		: ResourceManager::GetInstance().GetTexture("IMAGE_ZOMBIE_PAPER_MADHEAD"));
}

float PaperZombie::GetAbilityAnimSpeedMultiplier() const
{
	return mHasNewspaper ? 1.0f : kPaperRageAnimSpeedMultiplier;
}
