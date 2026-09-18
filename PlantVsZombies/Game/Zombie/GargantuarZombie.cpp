#include "GargantuarZombie.h"
#include "../../GameApp.h"

#include "GargantuarCharred.h"
#include "ImpZombie.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../GameObjectManager.h"
#include "../Plant/Plant.h"
#include "../../GameRandom.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "../ShadowComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
	const std::string kDefaultTrackTextureKey = "DEFAULT";
	constexpr int kBodyHealth = 3000;                         // 原版经典巨人本体生命
	constexpr int kSmashFrame = 93;                           // 主人指定的砸击结算全局帧
	constexpr int kThrowReleaseFrame = 131;                   // 主人确认的小鬼脱手全局帧
	constexpr int kDeathFrame = 196;                          // 主人指定的普通死亡回收全局帧
	constexpr int kDeathSoundFrame = 190;                     // 主人指定的死亡音效触发全局帧

	constexpr float kAnimSpeedMultiplierMin = 0.5f;            // 每只巨人整体动画倍率随机下限
	constexpr float kAnimSpeedMultiplierMax = 0.7f;            // 每只巨人整体动画倍率随机上限
	constexpr float kWalkClipSpeed = 1.0f;                    // 资源 12fps 的巨人稳态行走倍率
	constexpr float kSmashClipSpeed = 16.0f / 12.0f;          // 原版砸击 16fps 相对资源 12fps
	constexpr float kThrowClipSpeed = 44.0f / 12.0f;          // 原版投掷 24fps 相对资源 12fps
	constexpr float kDeathClipSpeed = 16.0f / 12.0f;          // 原版普通死亡 14fps 相对资源 12fps
	constexpr int kSmashZombieDamage = 1800;                  // 魅惑巨人砸击敌方僵尸的单次伤害
	constexpr float kColliderOffsetX = -85.0f;                // 原版碰撞框左缘相对逻辑原点 X，单位 px
	constexpr float kColliderOffsetY = -130.0f;                // 原版碰撞框上缘相对逻辑原点 Y，单位 px
	constexpr float kColliderWidth = 125.0f;                  // 原版巨人碰撞框宽度，单位 px
	constexpr float kColliderHeight = 170.0f;                 // 原版巨人碰撞框高度，单位 px
	constexpr float kThrowAnchorMinDistance = 40.0f;          // 巨人须位于投掷半场线外至少该距离才允许投掷，单位 px
	constexpr float kRoofThrowDistanceReduction = 180.0f;     // 原版屋顶投掷距离缩短量，单位 px
	constexpr float kImpReleaseOffsetX = 133.0f;              // 小鬼逻辑出生点相对巨人原点的水平距离，单位 px
	constexpr float kButterSplatScaleMultiplier = 1.2f;      // 巨人头部黄油相对普通僵尸尺寸的倍率
	constexpr float kIceTrapScaleMultiplier = 1.35f;          // 巨人脚底冰晶相对普通僵尸贴图尺寸的倍率
	constexpr float kOneShotVolume = 0.45f;                   // 巨人低吼、投掷和死亡 Foley 音量

	constexpr std::array<const char*, 14> kHeldImpTracks = {
		"Zombie_imp_body1", "Zombie_imp_body2", "Zombie_imp_head",
		"Zombie_imp_innerarm_lower", "Zombie_imp_innerarm_upper",
		"Zombie_imp_innerleg_foot", "Zombie_imp_innerleg_lower",
		"Zombie_imp_innerleg_upper", "Zombie_imp_jaw",
		"Zombie_imp_outerarm_lower", "Zombie_imp_outerarm_upper",
		"Zombie_imp_outerleg_foot", "Zombie_imp_outerleg_lower",
		"Zombie_imp_outerleg_upper",
	};
}

void GargantuarZombie::SetupZombie()
{
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mNeedDropArm = false;
	mNeedDropHead = false;
	mHasArm = true;
	mHasHead = true;
	mHasTongue = false;
	mHasImp = true;
	mPhase = Phase::WALKING;

	if (mCollider) {
		mCollider->size = Vector(kColliderWidth, kColliderHeight);
		// mZombieRect 是相对逻辑位置的战斗范围；视觉锚点只负责摆图，不能带偏砸击触发区。
		mCollider->offset = Vector(kColliderOffsetX, kColliderOffsetY);
	}

	if (auto* shadow = GetShadow()) {
		shadow->SetScale(Vector(1.34f, 1.34f));
	}

	if (!mAnimator) return;
	RegisterFrameEvents();
	const int roll = GameRandom::Range(0, 99);
	mWeaponVariant = roll < 10 ? WeaponVariant::ZOMBIE
		: roll < 35 ? WeaponVariant::DUCK_SIGN
		: WeaponVariant::TELEPHONE_POLE;
	mAnimSpeedMultiplier = GameRandom::Range(
		kAnimSpeedMultiplierMin, kAnimSpeedMultiplierMax);
	ApplyWeaponPresentation();
	ApplyHeldImpPresentation();
	if (mIsPreview) {
		// 预览对象不走基类 UpdateAnimSpeed；把同一随机倍率直接作为 idle clip 速度。
		PlayTrack("anim_idle");
		return;
	}
	mSpeed += GameRandom::Range(-2, 2);
	PlayWalking();
}

float GargantuarZombie::GetAbilityAnimSpeedMultiplier() const
{
	return mAnimSpeedMultiplier;
}

float GargantuarZombie::GetButterSplatScaleMultiplier() const
{
	return kButterSplatScaleMultiplier;
}

float GargantuarZombie::GetIceTrapScaleMultiplier() const
{
	return kIceTrapScaleMultiplier;
}

/** 注册巨人专属的砸击、脱手与死亡帧事件。 */
void GargantuarZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(kSmashFrame, [this]() { ApplySmashImpact(); }, true);
	mAnimator->AddFrameEvent(kThrowReleaseFrame, [this]() { ReleaseImp(); }, true);
	mAnimator->AddFrameEvent(kDeathFrame, [this]() { Die(); });
	mAnimator->AddFrameEvent(kDeathSoundFrame, []() 
	{ 	
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_GARGANTUAR_THUMP, kOneShotVolume);
	});
}

void GargantuarZombie::Update()
{
	const bool wasDying = mIsDying;
	Zombie::Update();
	if (mIsDead) return;
	if (!wasDying && mIsDying && !mDeathSoundPlayed) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_GARGANTUDEATH,
			kOneShotVolume);
		mDeathSoundPlayed = true;
	}
	if (mIsDying && GetCurrentTrackName() == "anim_death"
		&& std::abs(GetClipSpeed() - kDeathClipSpeed) > 0.001f) {
		SetClipSpeed(kDeathClipSpeed);
	}
}

void GargantuarZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mPhase != Phase::WALKING) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

void GargantuarZombie::ZombieUpdate(float)
{
	// 基类在垂死期仍推进动画计时；品种逻辑必须停住，避免重新开砸或开扔覆盖死亡轨。
	if (mIsDying || mIsDead) return;
	if (mPhase == Phase::WALKING) {
		TryBeginThrow();
		return;
	}
	if (mAnimator && !mAnimator->IsPlaying()) {
		PlayWalking(0.1f);
	}
}

void GargantuarZombie::PlayWalkAnimation(float blendTime)
{
	if (mPhase == Phase::WALKING) {
		PlayTrack("anim_walk", kWalkClipSpeed, blendTime);
	}
}

void GargantuarZombie::PlayWalking(float blendTime)
{
	mPhase = Phase::WALKING;
	mSmashApplied = false;
	UpdateAnimSpeed();
	mThrowReleased = false;
	mTargetRow = -1;
	mTargetColumn = -1;
	mTargetZombieID = NULL_ZOMBIE_ID;
	mThrowDistance = 0.0f;
	PlayWalkAnimation(blendTime);
}

void GargantuarZombie::StartEat(ColliderComponent* other)
{
	if (!other || mPhase != Phase::WALKING || mIsPreview || mIsDying || mIsDead
		|| !mHasHead || IsImmobilized()) {
		return;
	}
	GameObject* object = other->GetGameObject();
	if (!object) return;

	if (object->GetObjectType() == ObjectType::OBJECT_PLANT && !mIsMindControlled) {
		auto* plant = dynamic_cast<Plant*>(object);
		if (!plant || plant->mRow != mRow || !plant->IsActive()) return;
		if (mBoard) {
			if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn)) {
				plant = top;
			}
		}
		BeginSmash(plant->mRow, plant->mColumn, NULL_ZOMBIE_ID);
		return;
	}

	if (object->GetObjectType() == ObjectType::OBJECT_ZOMBIE) {
		auto* target = dynamic_cast<Zombie*>(object);
		if (!target || target == this || target->mRow != mRow || target->IsDying()
			|| target->IsMindControlled() == mIsMindControlled
			|| !target->CanBeTargetedByProjectile(false)) {
			return;
		}
		BeginSmash(-1, -1, target->mZombieID);
	}
}

void GargantuarZombie::BeginSmash(int row, int column, int zombieID)
{
	if (mPhase != Phase::WALKING || !mAnimator) return;
	mPhase = Phase::SMASHING;
	UpdateAnimSpeed();
	mTargetRow = row;
	mTargetColumn = column;
	mTargetZombieID = zombieID;
	mSmashApplied = false;
	PlayTrackOnce("anim_smash", "", kSmashClipSpeed, 0.1f);
	AudioSystem::PlaySound(
		GameRandom::Range(0, 1) == 0
			? ResourceKeys::Sounds::SOUND_LOWGROAN
			: ResourceKeys::Sounds::SOUND_LOWGROAN2,
		kOneShotVolume);
}

void GargantuarZombie::ApplySmashImpact()
{
	if (mPhase != Phase::SMASHING || mSmashApplied || mIsDying || mIsDead
		|| mBodyHealth <= 0 || !mBoard) {
		return;
	}
	mSmashApplied = true;

	if (mTargetZombieID != NULL_ZOMBIE_ID) {
		if (Zombie* target = mBoard->mEntityRegistry.GetZombie(mTargetZombieID);
			target && target->IsActive() && !target->IsDying()
			&& target->IsMindControlled() != mIsMindControlled) {
			target->TakeDamage(kSmashZombieDamage, DamageSource::ZOMBIE);
		}
	}
	else if (mTargetRow >= 0 && mTargetColumn >= 0) {
		const std::array<Plant*, 4> plants = {
			mBoard->GetOverlayPlantAt(mTargetRow, mTargetColumn),
			mBoard->GetPumpkinAt(mTargetRow, mTargetColumn),
			mBoard->GetNormalPlantAt(mTargetRow, mTargetColumn),
			mBoard->GetUnderPlantAt(mTargetRow, mTargetColumn),
		};
		for (Plant* plant : plants) {
			if (plant && plant->IsActive()) plant->ResolveGargantuarSmash();
		}
	}

	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_GARGANTUAR_THUMP,
		kOneShotVolume);
	mBoard->ShakeBoard(0.0f, 3.0f);
}

void GargantuarZombie::TryBeginThrow()
{
	if (!mHasImp || mBodyHealth > mBodyMaxHealth / 2 || !mBoard || !mAnimator) return;
	const int anchorColumn = std::min(5, mBoard->mColumns - 1);
	const float anchorX = mBoard->GetCellCenterPosition(mRow, anchorColumn).x;
	// 原版只允许巨人在房屋对面的半场起手；必须保留方向，越线后不能因绝对距离增大而重新获准投掷。
	float distance = GetPosition().x - anchorX;
	if (distance <= kThrowAnchorMinDistance) return;
	if (mBoard->IsRoofBackground()) distance -= kRoofThrowDistanceReduction;
	mThrowDistance = std::max(kThrowAnchorMinDistance, distance);
	mPhase = Phase::THROWING;
	mThrowReleased = false;
	PlayTrackOnce("anim_throw", "", kThrowClipSpeed, 0.1f);
}

void GargantuarZombie::ReleaseImp()
{
	if (mPhase != Phase::THROWING || mThrowReleased || !mHasImp || !mBoard
		|| mIsDying || mIsDead || mBodyHealth <= 0) {
		return;
	}
	// 第 130 帧是内嵌小鬼最后一个可见姿势；第 131 帧只把轨道隐藏，仍保留该轨道原点。
	const Vector heldImpBodyAnchor = GetHeldImpBodyRenderAnchor();
	const bool movingRight = IsMovingRight();
	const float releaseX = GetPosition().x
		+ (movingRight ? kImpReleaseOffsetX : -kImpReleaseOffsetX);
	Zombie* created = mBoard->CreateZombie(ZombieType::ZOMBIE_IMP, mRow, releaseX);
	auto* imp = dynamic_cast<ImpZombie*>(created);
	if (!imp) return;

	imp->ConfigureThrown(mThrowDistance, movingRight, mCooldownTimer,
		mIsMindControlled);
	if (Transform* impTransform = imp->GetTransform()) {
		// 133px 是原版逻辑出生量；两套 reanim 的视觉原点不同，须把同名身体轨道水平接续。
		const float anchorCorrectionX = heldImpBodyAnchor.x
			- imp->GetThrowBodyRenderAnchor().x;
		impTransform->Translate(anchorCorrectionX, 0.0f);
	}
	imp->ConstrainMineLanding();
	mHasImp = false;
	mThrowReleased = true;
	ApplyHeldImpPresentation();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SWING, kOneShotVolume);
	AudioSystem::PlaySound(
		GameRandom::Range(0, 1) == 0
			? ResourceKeys::Sounds::SOUND_IMP
			: ResourceKeys::Sounds::SOUND_IMP2,
		kOneShotVolume);
}

void GargantuarZombie::TakeBodyDamage(int damage)
{
	Zombie::TakeBodyDamage(damage);
	ApplyDamagePresentation();
	if (mBodyHealth > 0 || mIsDead || mIsDying) return;
	// 巨人不掉头，也没有无头流血临界值；本体真正归零时才进入专属死亡轨。
	AbortAction(false);
	if (mFrozenTimer > 0.0f) ClearFrozen();
	if (mButterTimer > 0.0f) ClearButter();
	if (mParalysisTimer > 0.0f) ClearParalysis();
	PlayTrack("anim_death", kDeathClipSpeed, 0.3f);
	if (mCollider) mCollider->mEnabled = false;
	mIsDying = true;
	if (!mDeathSoundPlayed) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_GARGANTUDEATH,
			kOneShotVolume);
		mDeathSoundPlayed = true;
	}
}

void GargantuarZombie::TakeHijackerExecution()
{
	if (mIsDead || mIsDying || !IsActive()) return;
	mShieldHealth = 0;
	ShieldDrop();
	mHelmHealth = 0;
	HelmDrop();
	// 让巨人自己的本体伤害入口接管死亡轨、碰撞体和动作中断，避免基类普通断肢污染贴图。
	TakeBodyDamage(std::max(1, mBodyHealth));
}

void GargantuarZombie::RefreshEquipmentPresentationAfterRepair()
{
	Zombie::RefreshEquipmentPresentationAfterRepair();
	ApplyDamagePresentation();
}

bool GargantuarZombie::TakePlantInstantKill()
{
	// 红眼巨人继承同一拒吞契约；统一的 20 点基础咬伤由大嘴花结算。
	return false;
}

void GargantuarZombie::OnMindControlled()
{
	AbortAction(true);
}

void GargantuarZombie::AbortAction(bool playWalkingTrack)
{
	mPhase = Phase::WALKING;
	mSmashApplied = false;
	mThrowReleased = false;
	mTargetRow = -1;
	mTargetColumn = -1;
	mTargetZombieID = NULL_ZOMBIE_ID;
	mThrowDistance = 0.0f;
	if (playWalkingTrack && !mIsDying && !mIsDead) {
		PlayWalkAnimation(0.1f);
	}
}

void GargantuarZombie::ApplyHeldImpPresentation() const
{
	if (!mAnimator) return;
	for (const char* track : kHeldImpTracks) {
		mAnimator->SetTrackVisible(track, mHasImp);
	}
	mAnimator->SetTrackVisible("Zombie_gargantuar_whiterope", mHasImp);
}

void GargantuarZombie::ApplyWeaponPresentation() const
{
	if (!mAnimator) return;
	const std::string* key = &ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_TELEPHONEPOLE;
	if (mWeaponVariant == WeaponVariant::DUCK_SIGN) {
		key = &ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_DUCKXING;
	}
	else if (mWeaponVariant == WeaponVariant::ZOMBIE) {
		key = &ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_ZOMBIE;
	}
	mAnimator->SetTrackImage("Zombie_gargantuar_telephonepole",
		ResourceManager::GetInstance().GetTexture(*key));
}

int GargantuarZombie::GetDamageStage() const
{
	if (mBodyHealth < mBodyMaxHealth / 3) return 2;
	if (mBodyHealth < mBodyMaxHealth * 2 / 3) return 1;
	return 0;
}

const std::string& GargantuarZombie::GetCurrentHeadTextureKey() const
{
	return GetHeadTextureKey(GetDamageStage());
}

const std::string& GargantuarZombie::GetCurrentBodyTextureKey() const
{
	const int stage = GetDamageStage();
	if (stage >= 2) return ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_BODY1_3;
	if (stage >= 1) return ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_BODY1_2;
	return kDefaultTrackTextureKey;
}

const std::string& GargantuarZombie::GetCurrentOuterArmTextureKey() const
{
	return GetDamageStage() >= 1
		? ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_OUTERARM_LOWER2
		: kDefaultTrackTextureKey;
}

const std::string& GargantuarZombie::GetCurrentFootTextureKey() const
{
	return GetDamageStage() >= 2
		? ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_FOOT2
		: kDefaultTrackTextureKey;
}

const std::string& GargantuarZombie::GetHeadTextureKey(int damageStage) const
{
	return damageStage >= 2
		? ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_HEAD2
		: ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_GARGANTUAR_HEAD;
}

void GargantuarZombie::ApplyDamagePresentation() const
{
	if (!mAnimator) return;
	const int stage = GetDamageStage();
	auto resolveTexture = [](const std::string& key) {
		return key == kDefaultTrackTextureKey
			? static_cast<const Texture*>(nullptr)
			: ResourceManager::GetInstance().GetTexture(key);
	};
	// nullptr 明确清除 Animator 的轨道换图，治疗跨回阈值时才能恢复资源默认贴图。
	mAnimator->SetTrackImage("Zombie_gargantua_body1",
		resolveTexture(GetCurrentBodyTextureKey()));
	mAnimator->SetTrackImage("Zombie_gargantuar_outerarm_lower",
		resolveTexture(GetCurrentOuterArmTextureKey()));
	mAnimator->SetTrackImage("Zombie_gargantuar_outerleg_foot",
		resolveTexture(GetCurrentFootTextureKey()));
	// 头部选择独立于其他伤势材质，使同时间线换色变体在健康、轻伤、重伤和读档后保持一致。
	mAnimator->SetTrackImage("anim_head1", ResourceManager::GetInstance().GetTexture(
		GetHeadTextureKey(stage)));
}

void GargantuarZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	ApplyHeldImpPresentation();
	ApplyWeaponPresentation();
	ApplyDamagePresentation();
}

void GargantuarZombie::Charred()
{
	if (mIsDead || !mBoard) return;
	GameObjectManager::GetInstance().CreateGameObjectImmediate<GargantuarCharred>(
		LAYER_GAME_ZOMBIE, mBoard, GetVisualPosition(), mHasImp, mRow);
	Die();
}

void GargantuarZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["hasImp"] = mHasImp;
	j["smashApplied"] = mSmashApplied;
	j["throwReleased"] = mThrowReleased;
	j["deathSoundPlayed"] = mDeathSoundPlayed;
	j["targetRow"] = mTargetRow;
	j["targetColumn"] = mTargetColumn;
	j["targetZombieID"] = mTargetZombieID;
	j["throwDistance"] = mThrowDistance;
	j["animSpeedMultiplier"] = mAnimSpeedMultiplier;
	j["weaponVariant"] = static_cast<int>(mWeaponVariant);
}

void GargantuarZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<Phase>(std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::THROWING)));
	mHasImp = j.value("hasImp", true);
	mSmashApplied = j.value("smashApplied", false);
	mThrowReleased = j.value("throwReleased", !mHasImp);
	mDeathSoundPlayed = j.value("deathSoundPlayed", mIsDying);
	mTargetRow = j.value("targetRow", -1);
	mTargetColumn = j.value("targetColumn", -1);
	mTargetZombieID = j.value("targetZombieID", NULL_ZOMBIE_ID);
	mThrowDistance = std::clamp(j.value("throwDistance", 0.0f),
		0.0f, static_cast<float>(SCENE_WIDTH));
	mAnimSpeedMultiplier = std::clamp(
		j.value("animSpeedMultiplier", mAnimSpeedMultiplier),
		kAnimSpeedMultiplierMin, kAnimSpeedMultiplierMax);
	mWeaponVariant = static_cast<WeaponVariant>(std::clamp(
		j.value("weaponVariant", 0), 0, static_cast<int>(WeaponVariant::ZOMBIE)));

	// hasImp 是背部小鬼显示与能否再次投掷的唯一权威；旧档矛盾组合只修动作，不反推持有状态。
	const bool invalidSmash = mPhase == Phase::SMASHING
		&& mTargetZombieID == NULL_ZOMBIE_ID
		&& (mTargetRow < 0 || mTargetColumn < 0);
	const bool invalidThrow = mPhase == Phase::THROWING
		&& ((mHasImp && mThrowReleased) || (!mHasImp && !mThrowReleased)
			|| mThrowDistance <= 0.0f);
	const bool actionCannotContinue = mIsDying || mIsDead || mIsMindControlled
		|| invalidSmash || invalidThrow;
	if (actionCannotContinue && mPhase != Phase::WALKING) {
		AbortAction(!mIsDying && !mIsDead);
	}
	else if (mPhase == Phase::SMASHING && GetCurrentTrackName() != "anim_smash") {
		AbortAction(true);
	}
	else if (mPhase == Phase::THROWING && GetCurrentTrackName() != "anim_throw") {
		AbortAction(true);
	}
	ApplyHeldImpPresentation();
	ApplyWeaponPresentation();
	ApplyDamagePresentation();
	UpdateAnimSpeed();
}
