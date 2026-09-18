#include "Plant.h"
#include "Game/Board/Board.h"
#include "../Zombie/Zombie.h"
#include "../GameObjectManager.h"
#include "../ShadowComponent.h"
#include "GameDataManager.h"
#include "PlantFootprint.h"
#include "../../GameApp.h"	// GameAPP::mShowPlantHP / Graphics / DrawText
#include "../../Logger.h"
#include <cmath>

namespace {
	constexpr float kPoolBobAmplitude = 2.0f;              // 水面植物上下浮动振幅（像素）
	constexpr float kPoolBobRadiansPerFrame = 3.14159265f / 60.0f; // 60Hz 下两秒一个周期
	constexpr float kPoolBobRowPhase = 3.14159265f;        // 相邻行交错半个周期
	constexpr float kPoolBobColumnPhase = 3.14159265f / 4.0f; // 相邻列相差八分之一周期
	constexpr float kSquishScaleY = 0.5f;                  // C# ratioSquished：纵向压到一半，横向保持原宽
	constexpr float kSquishDurationSeconds = 5.0f;         // 主人调短的压扁残影总保留时间，单位：秒
	constexpr float kSquishFadeSeconds = 1.0f;             // 保持 C# 末段占总时长 20% 的线性渐隐比例
	constexpr float kDefaultSquishPivotOffsetY = 100.0f;   // 无 Board 的预防性回退；正式关卡使用当前地图格高
	constexpr float kWakeUpSoundTimeRemaining = 0.6f;      // 原版 WAKE_UP_TIME=100，在剩 60cs 时播放 wakeup
	constexpr float kWakeUpBounceStartTime = 0.7f;         // 原版只在倒计时最后 70cs 做 EaseSinWave 纵向弹性
	constexpr float kWakeUpVisualPivotOffsetY = 80.0f;     // 原版 reanim 局部底边枢轴，单位：px
	constexpr float kMaximumShutdownDuration = 600.0f;     // 通用停机单次/读档允许的最大剩余秒数，防损坏档永久停工
	constexpr float kSleepIndicatorOffsetX = 10.0f;        // C# mX+50 换算到本项目格中心后的通用水平偏移，单位：px
	constexpr float kFumeSleepIndicatorExtraX = 12.0f;     // 大喷菇系原版额外右移量，单位：px
	constexpr float kScaredySleepIndicatorOffsetY = -20.0f; // 胆小菇系原版额外上移量，单位：px
	constexpr float kGloomSleepIndicatorOffsetY = -12.0f;  // 忧郁菇原版额外上移量，单位：px
	constexpr float kSleepIndicatorReanimFps = 12.0f;      // Z.reanim 资源基础帧率，单位：fps
	constexpr float kSleepIndicatorMinFps = 6.0f;          // 原版每个 Z 标识的随机最低播放帧率，单位：fps
	constexpr float kSleepIndicatorMaxFps = 8.0f;          // 原版每个 Z 标识的随机最高播放帧率，单位：fps
	constexpr float kSleepIndicatorMaxStartTime = 0.9f;    // 原版随机起始归一化时间上界，避免同屏完全同步
	constexpr float kIceSealDrawWidth = 112.0f;            // 冰像壳画面宽度，单位：px
	constexpr float kIceSealDrawHeight = 120.0f;           // 冰像壳画面高度，单位：px
	constexpr float kIceSealBottomOffsetY = 43.0f;         // 冰像壳底部相对植物公共锚点的偏移，单位：px

	/**
	 * 把 C# 以 80x80 左上角为基准的睡眠标识特例换算成本项目公共视觉锚点偏移。
	 * 自创冰大喷与精英胆小菇沿用各自原型的可感知位置语义。
	 */
	Vector GetSleepIndicatorOffset(PlantType type)
	{
		Vector offset(kSleepIndicatorOffsetX, 0.0f);
		switch (type) {
		case PlantType::PLANT_FUMESHROOM:
		case PlantType::PLANT_ICEFUMESHROOM:
			offset.x += kFumeSleepIndicatorExtraX;
			break;
		case PlantType::PLANT_SCAREDYSHROOM:
		case PlantType::PLANT_ELITE_SCAREDYSHROOM:
			offset.y = kScaredySleepIndicatorOffsetY;
			break;
		case PlantType::PLANT_GLOOMSHROOM:
			offset.y = kGloomSleepIndicatorOffsetY;
			break;
		default:
			break;
		}
		return offset;
	}
}

Plant::Plant(Board* board, PlantType plantType, int row, int column,
	AnimationType animType, float scale, bool isPreview)
	: AnimatedObject(ObjectType::OBJECT_PLANT, board,
		Vector(0, 0), // 位置会在后面计算
		animType,
		ColliderType::BOX,
		Vector(65, 65),
		Vector(-30, -30),
		scale,
		"Plant",
		false)
{
	mBoard = board;
	mPlantType = plantType;
	mRow = row;
	mColumn = column;
	mIsPreview = isPreview;
	// mIsSleeping / mPlantHealth / mPlantMaxHealth 由头文件就地初始化（false / 300 / 300）

	GameDataManager& plantMgr = GameDataManager::GetInstance();

	mVisualOffset = plantMgr.GetPlantOffset(plantType);
	CreateShadow
		(ResourceManager::GetInstance().GetTexture
		(ResourceKeys::Textures::IMAGE_PLANTSHADOW));

	// 设置植物在格子中的位置
	if (!mIsPreview) {
		if (auto collider = GetColliderComponent()) {
			collider->isStatic = true;
			collider->layerMask = CollisionLayer::PLANT;
			collider->collisionMask = CollisionLayer::ZOMBIE;
		}

		Vector cellCenterPosition = mBoard
			? mBoard->GetCellCenterPosition(row, column)
			: Vector(CELL_INITALIZE_POS_X + column * CELL_COLLIDER_SIZE_X + CELL_COLLIDER_SIZE_X / 2,
				CELL_INITALIZE_POS_Y + row * CELL_COLLIDER_SIZE_Y + CELL_COLLIDER_SIZE_Y / 2);
		SetPosition(cellCenterPosition);  // 逻辑位置
	}
	else {
		SetPosition(Vector(-512, -512));
	}
}

void Plant::SetupPlant()
{
}

void Plant::Start()
{
	GameObject::Start();
	if (this->mIsPreview) {
		RemoveCollider();
	}

	this->PlayTrack("anim_idle");
	this->SetupPlant();
}

/**
 * 通用停机和径流暂停必须在并行动画推进前判断；否则射击帧事件会先入队，随后串行阶段再停工已经太晚。
 * mAdvancedInParallel 仍置位，让串行 AnimatedObject::Update 只完成公共收尾而不补推进一遍动画。
 */
void Plant::UpdateParallel(std::vector<DeferredEvent>& outBuf)
{
	if (IsActionPaused()) {
		mAdvancedInParallel = true;
		return;
	}
	AnimatedObject::UpdateParallel(outBuf);
	if (mSleepIndicatorAnimator) {
		mSleepIndicatorAnimator->UpdateParallelDeferred(outBuf);
	}
}

void Plant::TakeDamage(int damage, DamageSource source) {
	if (!IsActive() || mPlantHealth <= 0 || mIsPreview || mIsSquished || IsBungeeTargeted() || IsIceSealed()) return;
	if (damage <= 0 || mUnyieldingRootsTimer > 0.0f) return;
	// 僵尸增伤只放大僵尸来源；植物韧性则对所有实际承伤生效。两者均在 0 层返回单位元。
	int scaledDamage = damage;
	if (mBoard) {
		if (source == DamageSource::ZOMBIE) {
			scaledDamage = mBoard->GetPerkManager().ScaleZombieDamage(scaledDamage);
		}
		scaledDamage = mBoard->GetPerkManager().ScaleDamageToPlant(scaledDamage);
	}
	if (mBoard && !mUnyieldingRootsSpent
		&& mBoard->GetPerkManager().HasUnyieldingRoots()
		&& scaledDamage >= mPlantHealth && mPlantHealth > 0) {
		mPlantHealth = 1;
		mUnyieldingRootsSpent = true;
		mUnyieldingRootsTimer = SurvivalPerkManager::GetInfo(
			PerkType::PLANT_UNYIELDING_ROOTS).perStack;
		SetGlowingTimer(0.1f);
		return;
	}
	mPlantHealth -= scaledDamage;
	SetGlowingTimer(0.1f);
	if (mPlantHealth <= 0) {
		if (mBoard && source == DamageSource::ZOMBIE) mBoard->RewardColdStoragePlantKill(GetPlacementType());
		Die();
	}
}

void Plant::KillByZombie()
{
	if (!IsActive() || mIsPreview || mIsSquished || IsIceSealed()) return;
	if (mBoard) mBoard->RewardColdStoragePlantKill(GetPlacementType());
	Die();
}

void Plant::Die() {
	// GameObjectManager 在下一次 Update 才真正移除对象；先失活可避免本帧绘制已被
	// StopAnimation 重置到轨道起点的姿态，也让重复死亡调用保持幂等。
	if (!IsActive() || IsIceSealed()) return;
	mShutdownTimer = 0.0f;
	// C# 只有飞行的咖啡豆死亡不影响地面扶梯；其余植物死亡都会拆掉完整占格上的梯子。
	const PlantType placementType = GetPlacementType();
	if (!mIsPreview && mBoard && placementType != PlantType::PLANT_INSTANT_COFFEE) {
		const PlantFootprint footprint = GetPlantFootprint(placementType);
		for (std::size_t i = 0; i < footprint.count; ++i) {
			mBoard->RemoveLadderAt(
				mRow + footprint.cells[i].rowOffset,
				mColumn + footprint.cells[i].columnOffset);
		}
	}
	SetActive(false);
	StopAnimation();

	// 禁用碰撞体
	if (mCollider) {
		mCollider->mEnabled = false;
	}

	ReleaseGridSlot();
	GameObjectManager::GetInstance().DestroyGameObject(this);
}

void Plant::ResetUnyieldingRootsForRound()
{
	mUnyieldingRootsSpent = false;
	mUnyieldingRootsTimer = 0.0f;
}

void Plant::RestoreUnyieldingRootsState(bool spent, float remainingSeconds)
{
	mUnyieldingRootsSpent = spent;
	mUnyieldingRootsTimer = std::isfinite(remainingSeconds)
		? std::clamp(remainingSeconds, 0.0f, 30.0f) : 0.0f;
}

bool Plant::CanAcquireZombie(const Zombie* zombie) const
{
	return zombie && zombie->CanBeTargetedByProjectile(false);
}

void Plant::Update()
{
	if (!mIsPreview && mUnyieldingRootsTimer > 0.0f) {
		mUnyieldingRootsTimer = std::max(
			0.0f, mUnyieldingRootsTimer - DeltaTime::GetDeltaTime());
	}
	// 回暖是真实冻土权威边沿；不依赖来源僵尸更新顺序，当帧先解除再恢复植物动作。
	if (IsIceSealed() && mBoard && !mBoard->IsPlantFootprintFrozen(
		mPlantType, mRow, mColumn)) {
		ReleaseIceSeal(mIceSealOwnerZombieID);
	}
	const bool actionPaused = IsActionPaused();
	// 串行回退路径也直接跳过 Animator 推进；不要 Pause/Play，否则一次性轨道会被公共结束检查误判。
	if (actionPaused) mAdvancedInParallel = true;
	const bool animatedInParallel = mAdvancedInParallel;
	AnimatedObject::Update();   // 非冲刷时待机动画照常推进，让植物在选卡阶段仍"活着"
	if (mSleepIndicatorAnimator && !animatedInParallel) {
		mSleepIndicatorAnimator->Update();
	}
	if (mIsSquished) {
		if (!mIsPreview && mBoard && mBoard->mBoardState == BoardState::GAME) {
			UpdateSquish();
		}
		return;
	}
	UpdateGridMoveVisual();
	if (!mIsPreview && mBoard && mBoard->mBoardState == BoardState::GAME) {
		if (mShutdownTimer > 0.0f) {
			mShutdownTimer = std::max(0.0f,
				mShutdownTimer - DeltaTime::GetDeltaTime());
		}
		UpdateWakeUp();
	}
	// 仅在对战进行中(GAME)才跑行为逻辑：生存轮间选卡(CHOOSE_CARD)时场上保留的植物应冻结，
	// 否则向日葵会继续产阳光、射手继续计时等。WIN/LOSE 同理不再行动。
	if (!actionPaused && !mIsPreview && !mIsSleeping && !IsBungeeTargeted() &&
		mBoard && mBoard->mBoardState == BoardState::GAME) {
		PlantUpdate();
	}
}

bool Plant::ApplyShutdown(float durationSeconds)
{
	if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f
		|| mIsPreview || mIsSquished || !IsActive() || !CanBeShutdown()) {
		return false;
	}
	mShutdownTimer = std::max(mShutdownTimer,
		std::min(durationSeconds, kMaximumShutdownDuration));
	return true;
}

void Plant::RestoreShutdown(float remainingSeconds)
{
	mShutdownTimer = std::isfinite(remainingSeconds)
		? std::clamp(remainingSeconds, 0.0f, kMaximumShutdownDuration)
		: 0.0f;
}

bool Plant::IsShutdown() const
{
	return mShutdownTimer > 0.0f
		|| (mBoard && mBoard->IsPlantPausedByRoofRunoff(this));
}

bool Plant::IsActionPaused() const
{
	return IsShutdown() || IsIceSealed();
}

Vector Plant::GetVisualPosition() const {
	if (mIsSquished) return mSquishVisualPosition;

	return GetVisualAnchorPosition() + mVisualOffset + mBungeeVisualOffset;
}

Vector Plant::GetVisualAnchorPosition() const
{
	Vector visual = GetPosition() + mGridMoveVisualOffset;
	if (!mIsPreview && mBoard && mBoard->IsPoolRow(mRow)) {
		const float phase = static_cast<float>(mBoard->mBoardFrame) * kPoolBobRadiansPerFrame
			+ static_cast<float>(mRow) * kPoolBobRowPhase
			+ static_cast<float>(mColumn) * kPoolBobColumnPhase;
		visual.y += std::sin(phase) * kPoolBobAmplitude;
	}
	if (!mIsPreview && mBoard && !IsRoofSupportPlant()) {
		Plant* support = mBoard->GetUnderPlantAt(mRow, mColumn);
		if (support && support->IsRoofSupportPlant()
			&& !support->IsSquished()) {
			// 只抬升画面锚点；逻辑格、碰撞箱和存档坐标仍保持在同一格中心。
			visual.y += kFlowerPotVisualLiftY;
		}
	}
	return visual;
}

Vector Plant::GetSleepIndicatorPosition() const
{
	return GetVisualAnchorPosition() + mBungeeVisualOffset
		+ GetSleepIndicatorOffset(mPlantType);
}

void Plant::PlantUpdate()
{
}

void Plant::ResolveGargantuarSmash()
{
	Squish();
}

void Plant::Squish()
{
	if (!IsActive() || mIsPreview || mIsSquished || IsIceSealed()) return;
	if (mBoard) mBoard->RewardColdStoragePlantKill(GetPlacementType());
	if (mBoard && GetPlacementType() != PlantType::PLANT_INSTANT_COFFEE) {
		mBoard->RemoveLadderAt(mRow, mColumn);
	}

	// 必须在置位前采样；GetVisualPosition() 在压扁态会直接返回冻结坐标。
	mSquishVisualPosition = GetVisualPosition();
	mIsSquished = true;
	mSquishTimer = kSquishDurationSeconds;
	ApplySquishedPresentation();

	// C# FoleyType.Squish 在 SOUND_CHOMP / SOUND_CHOMP2 中随机选一个。
	AudioSystem::PlaySound(GameRandom::Chance()
		? ResourceKeys::Sounds::SOUND_ZOMBIE_EAT
		: ResourceKeys::Sounds::SOUND_ZOMBIE_EAT2, 0.3f);
}

void Plant::RestoreSquishState(float remainingSeconds, const Vector& visualPosition)
{
	if (mIsPreview) return;
	mIsSquished = true;
	mSquishTimer = std::max(0.0f, remainingSeconds);
	mSquishVisualPosition = visualPosition;
	ApplySquishedPresentation();
	if (mSquishTimer <= 0.0f) {
		Die();
	}
}

void Plant::ApplySquishedPresentation()
{
	// 压扁残影不再表达“仍在睡觉”；立即移除独立 Z，读档恢复也走同一终态。
	SyncSleepIndicator();
	PauseAnimation();
	const float pivotOffsetY = mBoard ? mBoard->GetCellHeight() : kDefaultSquishPivotOffsetY;
	if (mAnimator) {
		// 根暂停已经能阻止子树更新；显式递归暂停还会同步每个子 Animator 的播放状态。
		mAnimator->PauseSubtree();
		mAnimator->SetRenderScale(1.0f, kSquishScaleY,
			mSquishVisualPosition.x, mSquishVisualPosition.y + pivotOffsetY);
	}
	if (mCollider) {
		mCollider->mEnabled = false;
	}
	if (auto* shadow = GetShadow()) {
		shadow->SetVisible(false);
	}
	ReleaseGridSlot();

	const float alpha = kSquishFadeSeconds > 0.0f
		? std::clamp(mSquishTimer / kSquishFadeSeconds, 0.0f, 1.0f)
		: 1.0f;
	SetAlpha(alpha);
}

void Plant::ReleaseGridSlot()
{
	if (!mBoard) return;
	const PlantFootprint footprint = GetPlantFootprint(GetPlacementType());
	for (std::size_t i = 0; i < footprint.count; ++i) {
		const PlantFootprintCell& occupied = footprint.cells[i];
		auto* cell = mBoard->GetCell(
			mRow + occupied.rowOffset, mColumn + occupied.columnOffset);
		if (!cell) continue;
		if (cell->GetUnderPlantID() == mPlantID) cell->ClearUnderPlantID();
		if (cell->GetNormalPlantID() == mPlantID) cell->ClearNormalPlantID();
		if (cell->GetPumpkinPlantID() == mPlantID) cell->ClearPumpkinPlantID();
		if (cell->GetOverlayPlantID() == mPlantID) cell->ClearOverlayPlantID();
	}
}

void Plant::RetireAfterReplacement()
{
	if (!IsActive()) return;
	SetActive(false);
	StopAnimation();
	if (mCollider) mCollider->mEnabled = false;
	GameObjectManager::GetInstance().DestroyGameObject(this);
}

void Plant::SetImitatedAppearance(bool imitated)
{
	mIsImitated = imitated;
	if (!mAnimator) return;
	if (!mIsImitated) {
		mAnimator->EnableWashedOutEffect(false);
		return;
	}
	const bool useLighterWash = mPlantType == PlantType::PLANT_HYPNOSHROOM
		|| mPlantType == PlantType::PLANT_SQUASH
		|| mPlantType == PlantType::PLANT_POTATOMINE
		|| mPlantType == PlantType::PLANT_GARLIC
		|| mPlantType == PlantType::PLANT_LILYPAD;
	mAnimator->EnableWashedOutEffect(true, useLighterWash);
}

bool Plant::BeginWakeUp(float durationSeconds)
{
	if (mIsPreview || !mIsSleeping || mWakeUpTimer > 0.0f
		|| durationSeconds <= 0.0f) {
		return false;
	}
	mWakeUpTimer = durationSeconds;
	ApplyWakeUpPresentation();
	return true;
}

void Plant::SetSleepState(bool sleep)
{
	mIsSleeping = sleep;
	SyncSleepIndicator();
}

void Plant::RestoreSleepState(bool sleep, float wakeUpTimeRemaining)
{
	mIsSleeping = sleep;
	mWakeUpTimer = sleep ? std::max(0.0f, wakeUpTimeRemaining) : 0.0f;
	SyncSleepIndicator();
	ApplyWakeUpPresentation();
}

void Plant::SyncSleepIndicator()
{
	if (!mIsSleeping || mIsPreview || mIsSquished) {
		mSleepIndicatorAnimator.reset();
		return;
	}
	if (mSleepIndicatorAnimator) return;

	auto reanim = ResourceManager::GetInstance().GetReanimation(
		ResourceKeys::Reanimations::REANIM_SLEEPING);
	if (!reanim) {
		LOG_ERROR("Plant") << "cannot create sleeping indicator: missing Z reanimation";
		return;
	}

	mSleepIndicatorAnimator = std::make_shared<Animator>(reanim);
	mSleepIndicatorAnimator->SetFrameRangeToDefault();
	const int totalFrames = reanim->GetTotalFrames();
	if (totalFrames > 1) {
		mSleepIndicatorAnimator->SetCurrentFrame(GameRandom::Range(
			0.0f, kSleepIndicatorMaxStartTime)
			* static_cast<float>(totalFrames - 1));
	}
	mSleepIndicatorAnimator->SetSpeed(GameRandom::Range(
		kSleepIndicatorMinFps / kSleepIndicatorReanimFps,
		kSleepIndicatorMaxFps / kSleepIndicatorReanimFps));
	mSleepIndicatorAnimator->Play(PlayState::PLAY_REPEAT);
}

void Plant::DrawSleepIndicator(Graphics* g)
{
	if (!g || !mIsSleeping || mIsSquished || !mSleepIndicatorAnimator) return;
	const Vector position = GetSleepIndicatorPosition();
	// Z 是独立世界动画，不继承品种 gamedata scale；只复用植物的动态视觉锚点。
	mSleepIndicatorAnimator->Draw(g, position.x, position.y, 1.0f);
}

void Plant::UpdateWakeUp()
{
	if (mWakeUpTimer <= 0.0f) return;
	const float previous = mWakeUpTimer;
	mWakeUpTimer = std::max(0.0f, mWakeUpTimer - DeltaTime::GetDeltaTime());

	if (previous > kWakeUpSoundTimeRemaining
		&& mWakeUpTimer <= kWakeUpSoundTimeRemaining) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_WAKEUP, 0.5f);
	}
	ApplyWakeUpPresentation();
	if (mWakeUpTimer <= 0.0f) {
		SetSleepState(false);
	}
}

void Plant::ApplyWakeUpPresentation()
{
	if (!mAnimator) return;
	float scaleY = 1.0f;
	if (mWakeUpTimer > 0.0f && mWakeUpTimer < kWakeUpBounceStartTime) {
		const float t = std::clamp(
			(kWakeUpBounceStartTime - mWakeUpTimer) / kWakeUpBounceStartTime,
			0.0f, 1.0f);
		const float eased = t * t * (3.0f - 2.0f * t);
		scaleY = 1.0f - 0.2f * std::sin(eased * 2.0f * 3.14159265f);
	}
	const Vector visual = GetVisualPosition();
	mAnimator->SetRenderScale(1.0f, scaleY,
		visual.x, visual.y + kWakeUpVisualPivotOffsetY);
}

void Plant::UpdateSquish()
{
	mSquishTimer = std::max(0.0f, mSquishTimer - DeltaTime::GetDeltaTime());
	if (mSquishTimer <= 0.0f) {
		Die();
		return;
	}
	SetAlpha(std::clamp(mSquishTimer / kSquishFadeSeconds, 0.0f, 1.0f));
}

float Plant::GetWeatherActionSpeedMultiplier() const
{
	if (!mBoard) return 1.0f;
	return mBoard->IsPlantPausedByRoofRunoff(this)
		? 0.0f : mBoard->GetPlantRainActionSpeedMultiplier();
}

float Plant::GetWeatherActionDeltaTime() const
{
	return DeltaTime::GetDeltaTime() * GetWeatherActionSpeedMultiplier();
}

float Plant::GetSunProductionDeltaTime() const
{
	const float planternMultiplier = mBoard
		? mBoard->GetPlanternSunProductionMultiplier(this) : 1.0f;
	return GetWeatherActionDeltaTime() * planternMultiplier;
}

float Plant::GetAttackSpeedMultiplier() const
{
	const float perkMultiplier = mBoard
		? static_cast<float>(mBoard->GetPerkManager().GetPlantAttackSpeedMultiplier())
		: 1.0f;
	return perkMultiplier * GetWeatherActionSpeedMultiplier();
}

Vector Plant::GetPosition() const
{
	return GetTransform()->GetPosition();
}

void Plant::SetPosition(const Vector& position)
{
	this->GetTransform()->SetPosition(position);
}

void Plant::MoveToGridCell(int row, int column, float visualDuration)
{
	if (IsBungeeTargeted() || IsIceSealed()) return;
	// 逻辑格和碰撞箱必须在同一帧落到目标格；旧画面位置只作为瞬态绘制偏移保留。
	const Vector currentVisualBase = GetPosition() + mGridMoveVisualOffset;
	const Vector target = mBoard
		? mBoard->GetCellCenterPosition(row, column)
		: Vector(CELL_INITALIZE_POS_X + column * CELL_COLLIDER_SIZE_X + CELL_COLLIDER_SIZE_X / 2,
			CELL_INITALIZE_POS_Y + row * CELL_COLLIDER_SIZE_Y + CELL_COLLIDER_SIZE_Y / 2);
	const int previousRow = mRow;
	mRow = row;
	mColumn = column;
	GameObjectManager::GetInstance().RefreshRenderOrderForSortingKey(
		this, previousRow);
	SetPosition(target);

	mGridMoveVisualStart = currentVisualBase - target;
	mGridMoveVisualOffset = mGridMoveVisualStart;
	mGridMoveVisualDuration = std::max(0.0f, visualDuration);
	mGridMoveVisualTimer = mGridMoveVisualDuration;
	if (mGridMoveVisualDuration <= 0.0f) {
		mGridMoveVisualStart = Vector(0.0f, 0.0f);
		mGridMoveVisualOffset = Vector(0.0f, 0.0f);
	}
}

void Plant::UpdateGridMoveVisual()
{
	if (mGridMoveVisualTimer <= 0.0f || mGridMoveVisualDuration <= 0.0f) return;
	mGridMoveVisualTimer = std::max(0.0f,
		mGridMoveVisualTimer - DeltaTime::GetDeltaTime());
	const float linear = std::clamp(1.0f
		- mGridMoveVisualTimer / mGridMoveVisualDuration, 0.0f, 1.0f);
	const float eased = linear * linear * (3.0f - 2.0f * linear);
	mGridMoveVisualOffset = mGridMoveVisualStart * (1.0f - eased);
	if (mGridMoveVisualTimer <= 0.0f) {
		mGridMoveVisualStart = Vector(0.0f, 0.0f);
		mGridMoveVisualOffset = Vector(0.0f, 0.0f);
	}
}

void Plant::Draw(Graphics* g)
{
	if (mBungeeState == PlantBungeeState::RISING) return;
	if (!mIsPreview && !mIsSquished && mBoard) {
		Plant* pumpkin = mBoard->GetPumpkinAt(mRow, mColumn);
		Plant* normal = mBoard->GetNormalPlantAt(mRow, mColumn);
		// 原版在普通植物绘制中插入 Pumpkin_back；空壳则由南瓜自己先画背片。
		if (pumpkin && ((normal && normal == this) || (!normal && pumpkin == this))) {
			pumpkin->DrawStackBackground(g);
		}
	}

	if (mIsSquished) {
		// 非等比缩放已烘进 Animator 的快/慢绘制路径；这里只抑制压扁态血量文字。
		AnimatedObject::Draw(g);
		return;
	}

	AnimatedObject::Draw(g);	// 先画本体动画
	DrawSleepIndicator(g);
	DrawIceSeal(g);
	// 劫持者目标提示只做当前格的常数次槽位查询；不为描边另起任何全场逐帧遍历。
	if (g && !mIsPreview && mBoard
		&& mBoard->IsPlantThreatenedByNightRoofHijacker(this) && mCollider) {
		const SDL_FRect bounds = mCollider->GetBoundingBox();
		const Vector visualDelta = GetVisualAnchorPosition() - GetPosition();
		const float alpha = mBoard->GetNightRoofHijackerPulseAlpha();
		g->DrawRect(bounds.x - 3.0f + visualDelta.x,
			bounds.y - 3.0f + visualDelta.y, bounds.w + 6.0f, bounds.h + 6.0f,
			glm::vec4(194.0f, 73.0f, 255.0f, alpha));
	}

	if (!g || mIsPreview || !GameAPP::GetInstance().mShowPlantHP) return;
	// 视口剔除：屏外植物不画血量文字（与 Zombie::Draw 同构，省 batch VBO + CPU）。
	if (!g->IsWorldPointVisible(GetPosition().x, GetPosition().y)) return;

	// 直接用逻辑坐标：DrawText 与 Animator 的 DrawTextureMatrix 共享同一 projView，
	// Animator 画 sprite 时就是用裸逻辑坐标，文字必须同坐标系才能叠在对象上（勿转 World）
	// 血条属于画面反馈，随台风平滑位移；碰撞箱仍使用已经落在目标格的 Transform。
	Vector pos = GetVisualAnchorPosition() + GetHealthTextOffset();

	std::string text = std::to_string(mPlantHealth) + u8"/" + std::to_string(mPlantMaxHealth);
	// 颜色是 0..255 范围（ToSDLColor 直接 static_cast，不乘 255），勿写成 0..1 否则全透明隐形
	const glm::vec4 green(0.0f, 255.0f, 0.0f, 255.0f);
	g->DrawGlyphRun(text, ResourceKeys::Fonts::FONT_FZCQ, 17, green, pos.x, pos.y);
}

bool Plant::BeginBungeeGrab(int zombieID)
{
	if (mIsPreview || mIsSquished || IsIceSealed() || zombieID == NULL_ZOMBIE_ID
		|| (IsBungeeTargeted() && mBungeeOwnerZombieID != zombieID)) {
		return false;
	}
	mBungeeState = PlantBungeeState::GRABBING;
	mBungeeOwnerZombieID = zombieID;
	mBungeeVisualOffset = Vector::zero();
	if (mCollider) mCollider->mEnabled = false;
	if (auto* shadow = GetShadow()) shadow->SetVisible(false);
	return true;
}

bool Plant::BeginIceSeal(int ownerZombieID)
{
	if (ownerZombieID == NULL_ZOMBIE_ID || mIsPreview || mIsSquished
		|| IsBungeeTargeted() || !IsActive()
		|| (IsIceSealed() && mIceSealOwnerZombieID != ownerZombieID)) {
		return false;
	}
	mIceSealOwnerZombieID = ownerZombieID;
	if (mCollider) mCollider->mEnabled = false;
	return true;
}

bool Plant::ReleaseIceSeal(int ownerZombieID)
{
	if (!IsIceSealed() || mIceSealOwnerZombieID != ownerZombieID) return false;
	mIceSealOwnerZombieID = NULL_ZOMBIE_ID;
	if (mCollider && IsActive() && !mIsSquished && !IsBungeeTargeted()) {
		mCollider->mEnabled = true;
	}
	return true;
}

bool Plant::TakeIceExecutionDamage(int ownerZombieID, int damage)
{
	if (!IsActive() || damage <= 0 || mIceSealOwnerZombieID != ownerZombieID) {
		return false;
	}
	// 通用承伤链会拒绝冰封实体；在同一调用栈暂时释放关系，使本击仍享受正式词条缩放。
	mIceSealOwnerZombieID = NULL_ZOMBIE_ID;
	TakeDamage(damage, DamageSource::ZOMBIE);
	if (IsActive()) {
		mIceSealOwnerZombieID = ownerZombieID;
		if (mCollider) mCollider->mEnabled = false;
	}
	return true;
}

bool Plant::ResolveIceExecution(int ownerZombieID)
{
	if (!IsActive() || mIceSealOwnerZombieID != ownerZombieID) return false;
	mIceSealOwnerZombieID = NULL_ZOMBIE_ID;
	Die();
	return !IsActive();
}

void Plant::RestoreIceSeal(int ownerZombieID)
{
	mIceSealOwnerZombieID = ownerZombieID >= 0
		? ownerZombieID : NULL_ZOMBIE_ID;
	if (mCollider && IsIceSealed()) mCollider->mEnabled = false;
}

void Plant::DrawIceSeal(Graphics* g)
{
	if (!g || mIsPreview || mIsSquished || !IsIceSealed()) return;
	const Texture* shell = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_ICE_STATUE_SHELL, false);
	if (!shell) return;
	const Vector anchor = GetVisualAnchorPosition();
	const float drawX = anchor.x - kIceSealDrawWidth * 0.5f;
	const float drawY = anchor.y + kIceSealBottomOffsetY - kIceSealDrawHeight;
	if (g->IsInstancePathEnabled()) {
		g->DrawTextureInstanced(shell, drawX, drawY,
			kIceSealDrawWidth, kIceSealDrawHeight);
	}
	else {
		g->DrawTexture(shell, drawX, drawY,
			kIceSealDrawWidth, kIceSealDrawHeight);
	}
}

bool Plant::BeginBungeeLift(int zombieID)
{
	if (mBungeeState != PlantBungeeState::GRABBING
		|| mBungeeOwnerZombieID != zombieID) {
		return false;
	}
	mBungeeState = PlantBungeeState::RISING;
	return true;
}

void Plant::CancelBungeeGrab(int zombieID)
{
	if (!IsBungeeTargeted() || mBungeeOwnerZombieID != zombieID) return;
	mBungeeState = PlantBungeeState::NONE;
	mBungeeOwnerZombieID = NULL_ZOMBIE_ID;
	mBungeeVisualOffset = Vector::zero();
	if (mCollider) mCollider->mEnabled = true;
	if (auto* shadow = GetShadow()) shadow->SetVisible(true);
}

void Plant::SetBungeeVisualOffset(int zombieID, const Vector& offset)
{
	if (mBungeeState != PlantBungeeState::RISING
		|| mBungeeOwnerZombieID != zombieID) return;
	mBungeeVisualOffset = offset;
}

void Plant::DrawAsBungeeCargo(Graphics* g)
{
	if (mBungeeState == PlantBungeeState::RISING) {
		AnimatedObject::Draw(g);
		DrawSleepIndicator(g);
	}
}
