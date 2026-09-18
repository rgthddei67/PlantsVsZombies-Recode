#include "Zombie.h"
#include "ZombieCharred.h"
#include "GildedZamboniZombie.h"
#include "../Plant/Plant.h"
#include "../Plant/HypnoShroom.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../GameObjectManager.h"
#include "../Plant/GameDataManager.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../GameApp.h"
#include "../../ResourceKeys.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace {
	float GAMEOVER_X = 110.0f;	// 僵尸到达此横坐标即触发游戏失败
	constexpr float kPoolFrontProbeX = 75.0f;              // 身体前侧入水探针相对世界 X 偏移，单位：像素
	constexpr float kPoolRearProbeX = 45.0f;               // 身体后侧入水探针相对世界 X 偏移，单位：像素
	constexpr float kPoolTransitionRightShiftX = 70.0f;    // 适配当前泳池图，把进出水边界向右校正的像素数
	constexpr float kPoolTransitionBlend = 0.2f;           // 进出水后恢复稳态走路轨道的混合秒数
	constexpr float kPoolClipBottomOffsetY = 0.0f;         // 水面裁剪底边相对行逻辑 Y 的偏移，单位：像素
	constexpr float kPoolSplashScale = 0.8f;               // C# PoolSplash 对 Splash.reanim 的统一缩放
	constexpr float kPoolSplashAnimOffsetX = -30.0f;       // Splash 轨道视觉中心相对池沿发射点的反向锚定，单位：像素
	constexpr float kPoolSplashAnimOffsetY = -40.0f;       // Splash 轨道水面中心相对池沿发射点的反向锚定，单位：像素
	constexpr float kPoolSplashSoundVolume = 0.4f;         // 通用入水/出水 Foley 音量
	constexpr float kTangleKelpSinkSpeed = 100.0f;         // 原版拖沉速度，单位：像素/秒
	constexpr float kTangleKelpGrabOffsetX = -13.0f;       // 原版通用僵尸 anim_grab 附着点水平偏移，单位：像素
	constexpr float kTangleKelpGrabOffsetY = 15.0f;        // 原版通用僵尸 anim_grab 附着点垂直偏移，单位：像素
	constexpr float kTangleKelpGrabSpeed = 2.0f;           // 资源 12fps 播放为原版 24fps 的速度倍率
	constexpr float kTangleKelpGrabStartFrame = 22.0f;     // anim_grab 轨道在 Tanglekelp.reanim 中的首帧
	constexpr float kTangleKelpGrabEndFrame = 26.0f;       // anim_grab 轨道末帧；读档帧钳位避免损坏值越界
	constexpr int kMaxGoldenIceEffectStacks = 8;           // 独立黄色冰道来源计层的安全上限，防止极端调试生成导致倍率溢出
	constexpr float kEatingTargetRetentionGap = 6.0f;      // 跳跃受阻后允许僵尸隔空啃食的最大碰撞箱间隙，单位：像素
	constexpr float kHitGlowDuration = 0.1f;               // 本体与二类护盾受击白光持续时间，单位：游戏秒
	constexpr float kToxinLayerDuration = 6.0f;            // 单层毒素持续时间，单位：游戏秒
	constexpr float kToxinDamageInterval = 0.2f;           // 每层积满 1 点毒伤的游戏时间间隔，单位：秒
	constexpr float kToxinDamageEpsilon = 0.0001f;         // 浮点取整容差，避免整点伤害因误差延迟一帧
	constexpr std::size_t kMaxToxinLayers = 20;             // 单只僵尸同时承载的最大毒素层数
	constexpr float kFogBreakoutImmunitySeconds = 3.0f;    // 雾幕突围清控后的四类控制免疫时长，单位：游戏秒
	constexpr float kArmorBreakRushSeconds = 5.0f;         // 破甲狂潮的行动加速与四类控制免疫时长，单位：游戏秒
	constexpr float kArmorBreakRushSpeedMultiplier = 1.60f;// 破甲狂潮对动画驱动移动和动作的统一倍率
	constexpr ZombieControlMask kPerkControlMask =
		ZombieControlBit(ZombieControlEffect::SLOW)
		| ZombieControlBit(ZombieControlEffect::FROZEN)
		| ZombieControlBit(ZombieControlEffect::BUTTER)
		| ZombieControlBit(ZombieControlEffect::PARALYSIS);   // 两个生存词条共同覆盖的控制集合
	constexpr float kButterDuration = 4.0f;                // C# mButteredCounter=400 厘秒的黄油定身时长
	constexpr float kMaximumParalysisDuration = 600.0f;    // 通用麻痹单次/读档允许的最大剩余秒数，防损坏档永久停格
	constexpr float kButterSplatOffsetY = -6.0f;           // C# DrawButter 相对头部轨道的贴图纵向偏移，单位：像素
	constexpr float kButterSplatScale = 1.0f;              // 对齐原版头顶覆盖比例，同时保留面部与上身轮廓
	constexpr const char* kButterSplatFollowerSlot =
		"butter_splat";                                    // 头部轨道上的通用黄油命名 follower 槽
	const Vector kButterFallbackHeadOffset(0.0f, -40.0f);   // 缺少 anim_head1 时相对逻辑位置的保底头部锚点
	constexpr float kRoofMarshalFlagOffsetX = 18.0f;       // 突击令红旗左上角移到头部右后侧，避免遮住脸，单位：局部像素
	constexpr float kRoofMarshalFlagOffsetY = -5.0f;       // 最上排也不会被顶部 UI 大面积裁掉的纵向偏移，单位：局部像素
	constexpr float kGarlicShortPauseTime = 0.20f;         // 无匹配嫌恶脸资源的品种只停顿 20 厘秒
	constexpr float kGarlicFacePauseTime = 0.70f;          // 有嫌恶脸品种在 70 厘秒节点停吃并换脸
	constexpr float kGarlicHoldTime = 1.70f;               // 原版 YUCKI_HOLD_TIME：选择相邻行并恢复行走
	constexpr float kGarlicWalkEndTime = 2.70f;            // 原版 YUCKI_WALK_TIME：收回嫌恶脸并结束状态
	constexpr float kGarlicRowMoveSpeed = 100.0f;          // 原版每厘秒移动 1 像素，折合像素/游戏秒
	constexpr float kGarlicRowMoveEpsilon = 0.001f;        // 纵向收敛判定容差，避免浮点残差留在行间
	constexpr float kGarlicYuckFaceOffsetY = -15.0f;       // grossout PNG 顶部透明留白相对普通头图的垂直补偿，单位：像素

	/** 对齐 C# AnimateChewSound：坚硬防御植物使用 ChompSoft，其他植物使用普通 Chomp。 */
	bool UsesSoftChewSound(PlantType type)
	{
		switch (type) {
		case PlantType::PLANT_WALLNUT:
		case PlantType::PLANT_TALLNUT:
		case PlantType::PLANT_PUMPKINSHELL:
			return true;
		default:
			return false;
		}
	}

	/** 播放原版 Splash.reanim 的完整一次性时间轴，并在末帧自动回收。 */
	class PoolSplashVisual final : public AnimatedObject {
	public:
		PoolSplashVisual(Board* board, const Vector& position)
			: AnimatedObject(ObjectType::OBJECT_PARTICLE, board, position,
				AnimationType::ANIM_POOL_SPLASH, ColliderType::BOX,
				Vector::zero(), Vector::zero(), kPoolSplashScale,
				"PoolSplash", true)
		{
		}

		void Start() override
		{
			AnimatedObject::Start();
			if (!mAnimator) {
				GameObjectManager::GetInstance().DestroyGameObject(this);
				return;
			}
			mAnimator->SetFrameRangeToDefault();
			SetAnimationSpeed(1.0f);
			SetLoopType(PlayState::PLAY_ONCE);
		}
	};
}

struct Zombie::ToxinState {
	std::array<float, kMaxToxinLayers> mLayerTimers{};
	float mDamageRemainder = 0.0f;
	std::uint8_t mActiveLayerCount = 0;
};

struct Zombie::RoofMarshalAssaultState {
	float mTimer = 0.0f;
	float mMoveMultiplier = 1.0f;
	float mBiteMultiplier = 1.0f;
	std::shared_ptr<Animator> mFlagAnimator;
};

struct Zombie::TangleKelpState {
	bool mDraggedUnder = false;
	float mSinkOffset = 0.0f;
	std::shared_ptr<Animator> mGrabBack;
	std::shared_ptr<Animator> mGrabFront;
};

Zombie::Zombie(Board* board, ZombieType zombieType, float x, float y, int row,
	AnimationType animType, float scale, bool isPreview)
	: AnimatedObject(ObjectType::OBJECT_ZOMBIE, board,
		Vector(x, y),
		animType,
		ColliderType::BOX,
		Vector(50, 100),
		Vector(-25, -65),
		scale,
		"Zombie",
		false)
{
	mZombieType = zombieType;
	mRow = row;
	mIsPreview = isPreview;

	mVisualOffset = GameDataManager::GetInstance().GetZombieOffset(zombieType);

	mAnimator->SetTrackVisible("_ground", false);

	if (isPreview && mAnimator->HasTrack("anim_idle"))
	{
		this->PlayTrack("anim_idle");
		return;
	}

	if (!mBoard) return;

	auto collider = GetColliderComponent();
	const float collisionY = mBoard->GetZombieCollisionY(row, x);
	if (collisionY >= 0.0f) {
		// 水路僵尸的 Transform 保留美术下沉，碰撞框反向抵消该差值并回到逻辑行基线。
		collider->offset.y += collisionY - y;
	}
	collider->isTrigger = true;
	collider->layerMask = CollisionLayer::ZOMBIE;
	collider->collisionMask = CollisionLayer::PLANT | CollisionLayer::BULLET | CollisionLayer::MOWER;
	collider->SetTriggerEnterCallback([this](ColliderComponent* other) {
		this->StartEat(other);
		});
	collider->SetTriggerStayCallback([this](ColliderComponent* other) {
		this->StartEat(other);
		});
	collider->SetTriggerExitCallback([this](ColliderComponent* other) {
		this->StopEat(other);
		});

	mGroundTrackIndex = mAnimator->GetFirstTrackIndexByName("_ground");
}

void Zombie::SetupZombie()
{
	RegisterFrameEvents();

	mHasTongue = static_cast<bool>(GameRandom::Range(0, 1));

	if (!mAnimator->GetTracksByName("anim_tongue").empty()) {
		mAnimator->SetTrackVisible("anim_tongue", mHasTongue);
	}

	if (mIsPreview) return;

	mSpeed += GameRandom::Range(-3, 3);

	if (GameRandom::Range(0, 1) == 0)
		this->PlayTrack("anim_walk");
	else
		this->PlayTrack("anim_walk2");
}

Zombie::~Zombie() = default;

bool Zombie::IsRoofMarshalAssaultActive() const
{
	return mRoofMarshalAssaultState && mRoofMarshalAssaultState->mTimer > 0.0f;
}

float Zombie::GetRoofMarshalAssaultTimer() const
{
	return mRoofMarshalAssaultState ? mRoofMarshalAssaultState->mTimer : 0.0f;
}

float Zombie::GetRoofMarshalAssaultMoveMultiplier() const
{
	return IsRoofMarshalAssaultActive()
		? mRoofMarshalAssaultState->mMoveMultiplier : 1.0f;
}

float Zombie::GetRoofMarshalAssaultBiteMultiplier() const
{
	return IsRoofMarshalAssaultActive()
		? mRoofMarshalAssaultState->mBiteMultiplier : 1.0f;
}

bool Zombie::HasRoofMarshalAssaultFlagAnimator() const
{
	return mRoofMarshalAssaultState
		&& static_cast<bool>(mRoofMarshalAssaultState->mFlagAnimator);
}

bool Zombie::IsDraggedUnderByTangleKelp() const
{
	return mTangleKelpState && mTangleKelpState->mDraggedUnder;
}

float Zombie::GetTangleKelpSinkOffset() const
{
	return mTangleKelpState ? mTangleKelpState->mSinkOffset : 0.0f;
}

/** 注册普通僵尸 reanim 的死亡终点与两次啃食命中帧。 */
void Zombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(216, [this]() { this->Die(); });
	mAnimator->AddFrameEvent(152, [this]() { this->EatTarget(); }, true);
	mAnimator->AddFrameEvent(171, [this]() { this->EatTarget(); }, true);
}

void Zombie::ApplyHealthMultiplier(double multiplier, double armorMultiplier)
{
	if (multiplier <= 0.0 || armorMultiplier <= 0.0
		|| (multiplier == 1.0 && armorMultiplier == 1.0)) return;
	// 同一原值 × 同一倍率 → 同一舍入结果，故缩放后 current 仍等于 max。血量非负，四舍五入用 +0.5。
	// 用 double 而非 float：float 尾数仅 24 位，血量 > 2^24(≈1677万) 时整数会丢精度；double 尾数 52 位，
	// 整数精确到 ~9e15，远超 int 上限，故缩放链路上 float 是比 int 字段更先暴露的弱点（缩放仅出生时算一次，无热路径开销）。
	auto scale = [](int v, double factor) {
		double scaled = static_cast<double>(v) * factor + 0.5;
		// 防溢出：缩放后血量超过 INT_MAX 时 static_cast<int> 是 UB(实测得 INT_MIN)，钳到 INT_MAX。
		// (double)INT_MAX == 2147483647.0 精确可表示(2^31-1 < 2^53)，故用 >= 比较即可。
		if (scaled >= static_cast<double>(INT_MAX)) return INT_MAX;
		return static_cast<int>(scaled);
		};

	mBodyHealth = scale(mBodyHealth, multiplier);
	mBodyMaxHealth = scale(mBodyMaxHealth, multiplier);
	const double armorFactor = multiplier * armorMultiplier;
	mHelmHealth = scale(mHelmHealth, armorFactor);
	mHelmMaxHealth = scale(mHelmMaxHealth, armorFactor);
	mShieldHealth = scale(mShieldHealth, armorFactor);
	mShieldMaxHealth = scale(mShieldMaxHealth, armorFactor);
	ApplyExtraHealthMultiplier(armorFactor);
}

void Zombie::SaveProtectedData(nlohmann::json& j) const {
	SaveDrumInspiration(j);
	j["prismMarkRemaining"] = GetPrismMarkRemaining();
	j["mineTargetCell"] = mMineTargetCell;
	j["mineTargetReturning"] = mMineTargetReturning;
	j["isMindControlled"] = mIsMindControlled;
	j["freeHitsRemaining"] = mFreeHitsRemaining;
	j["isEating"] = mIsEating;
	j["eatPlantID"] = mEatPlantID;
	j["hasHead"] = mHasHead;
	j["hasArm"] = mHasArm;
	j["hasTongue"] = mHasTongue;
	j["isDying"] = mIsDying;
	j["inPool"] = mInPool;
	j["speed"] = mSpeed;
	j["cooldownTimer"] = mCooldownTimer;
	j["frozenTimer"] = mFrozenTimer;
	j["butterTimer"] = mButterTimer;
	j["paralysisTimer"] = mParalysisTimer;
	j["controlImmunityTimers"] = mControlImmunityTimers;
	j["roofMarshalAssaultTimer"] = GetRoofMarshalAssaultTimer();
	j["roofMarshalAssaultMoveMultiplier"] = GetRoofMarshalAssaultMoveMultiplier();
	j["roofMarshalAssaultBiteMultiplier"] = GetRoofMarshalAssaultBiteMultiplier();
	std::array<float, kMaxToxinLayers> toxinLayerTimers{};
	float toxinDamageRemainder = 0.0f;
	if (mToxinState) {
		std::copy_n(mToxinState->mLayerTimers.begin(), mToxinState->mActiveLayerCount,
			toxinLayerTimers.begin());
		toxinDamageRemainder = mToxinState->mDamageRemainder;
	}
	j["toxinLayerTimers"] = toxinLayerTimers;
	j["toxinDamageRemainder"] = toxinDamageRemainder;
	j["dyingTimer"] = mDyingTimer;
	j["tangleKelpPlantID"] = mTangleKelpPlantID;
	j["draggedUnderByTangleKelp"] = IsDraggedUnderByTangleKelp();
	j["tangleKelpSinkOffset"] = GetTangleKelpSinkOffset();
	j["tangleKelpGrabFrame"] = GetTangleKelpGrabFrame();
	j["mistFuelReward"] = mMistFuelReward;
	j["mistFuelRewardClaimed"] = mMistFuelRewardClaimed;
	j["fogBreakoutObserved"] = mFogBreakoutObserved;
	j["wasObscuredByFog"] = mWasObscuredByFog;
	j["fogBreakoutTriggered"] = mFogBreakoutTriggered;
	j["armorBreakRushSpent"] = mArmorBreakRushSpent;
	j["armorBreakRushTimer"] = mArmorBreakRushTimer;
	j["ladderClimbPhase"] = static_cast<int>(mLadderClimbPhase);
	j["ladderAltitude"] = mLadderAltitude;
	j["useLadderColumn"] = mUseLadderColumn;
	j["garlicRedirectActive"] = mGarlicRedirectActive;
	j["garlicRedirectElapsed"] = mGarlicRedirectElapsed;
	j["garlicRowChanged"] = mGarlicRowChanged;
}

void Zombie::LoadProtectedData(const nlohmann::json& j) {
	mPrismMarkRemaining = std::clamp(j.value("prismMarkRemaining",0.0f),0.0f,6.0f);
	mMineTargetCell = (mBoard && mBoard->IsMineBackground()) ? j.value("mineTargetCell", -1) : -1;
	if (mMineTargetCell < -1 || mMineTargetCell >= MineGrid::Count
		|| (mMineTargetCell >= 0 && mBoard->mMineGrid.rock[mMineTargetCell])) mMineTargetCell = -1;
	mIsMindControlled = j.value("isMindControlled", false);
	LoadDrumInspiration(j);
	// 旧档没有路段意图：敌方旧前进边首次返程时掉头，已魅惑单位继续原返程边。
	mMineTargetReturning = j.value("mineTargetReturning", mIsMindControlled);
	mFreeHitsRemaining = j.value("freeHitsRemaining", 0);   // 旧档缺字段→0
	mIsEating = j.value("isEating", false);
	mEatPlantID = j.value("eatPlantID", NULL_PLANT_ID);
	mHasHead = j.value("hasHead", true);
	mHasArm = j.value("hasArm", true);
	mHasTongue = j.value("hasTongue", false);
	mIsDying = j.value("isDying", false);
	mInPool = j.value("inPool", false);
	mSpeed = j.value("speed", 10.0f);
	mFogBreakoutObserved = j.value("fogBreakoutObserved", false);
	mWasObscuredByFog = j.value("wasObscuredByFog", false);
	mFogBreakoutTriggered = j.value("fogBreakoutTriggered", false);
	mArmorBreakRushSpent = j.value("armorBreakRushSpent", false);
	const float armorBreakRushTimer = j.value("armorBreakRushTimer", 0.0f);
	mArmorBreakRushTimer = std::isfinite(armorBreakRushTimer)
		? std::clamp(armorBreakRushTimer, 0.0f, kArmorBreakRushSeconds) : 0.0f;
	const float roofMarshalAssaultTimer = std::clamp(
		j.value("roofMarshalAssaultTimer", 0.0f), 0.0f, 60.0f);
	const float roofMarshalAssaultMoveMultiplier = std::clamp(
		j.value("roofMarshalAssaultMoveMultiplier", 1.0f), 1.0f, 4.0f);
	const float roofMarshalAssaultBiteMultiplier = std::clamp(
		j.value("roofMarshalAssaultBiteMultiplier", 1.0f), 1.0f, 4.0f);
	mRoofMarshalAssaultState.reset();
	if (roofMarshalAssaultTimer > 0.0f) {
		mRoofMarshalAssaultState = std::make_unique<RoofMarshalAssaultState>();
		mRoofMarshalAssaultState->mTimer = roofMarshalAssaultTimer;
		mRoofMarshalAssaultState->mMoveMultiplier = roofMarshalAssaultMoveMultiplier;
		mRoofMarshalAssaultState->mBiteMultiplier = roofMarshalAssaultBiteMultiplier;
	}

	// 旧档把品种能力倍率放在根字段 extraSpeed；只有仍需实例随机值的派生类消费它。
	// 固定倍率和状态倍率均由虚函数直接派生，新档不再写该字段。
	if (j.contains("extraSpeed")) {
		RestoreLegacyAbilityAnimSpeedMultiplier(j.value("extraSpeed", 1.0f));
	}
	float cooldown = j.value("cooldownTimer", 0.0f);
	if (cooldown > 0.0f) {
		this->SetCooldown(cooldown);
	}
	else if (mAnimator) {
		UpdateAnimSpeed();   // 无减速也要恢复僵尸基准 × 当前雨势倍率
	}

	// 冻结还原：必须在减速恢复之后——UpdateAnimSpeed 里冻结优先，把 extra 覆盖回 0（停格）。
	mFrozenTimer = j.value("frozenTimer", 0.0f);
	if (mFrozenTimer > 0.0f && mAnimator) {
		UpdateAnimSpeed();
	}
	mButterTimer = std::clamp(
		j.value("butterTimer", 0.0f), 0.0f, kButterDuration);
	if (mButterTimer > 0.0f && mAnimator) {
		UpdateAnimSpeed();
	}
	mParalysisTimer = std::clamp(
		j.value("paralysisTimer", 0.0f), 0.0f, kMaximumParalysisDuration);
	if (!CanBeParalyzed()) mParalysisTimer = 0.0f;
	if (mParalysisTimer > 0.0f && mAnimator) {
		UpdateAnimSpeed();
	}
	mControlImmunityTimers.fill(0.0f);
	if (const auto it = j.find("controlImmunityTimers");
		it != j.end() && it->is_array()) {
		const std::size_t count = std::min(
			it->size(), mControlImmunityTimers.size());
		for (std::size_t index = 0; index < count; ++index) {
			mControlImmunityTimers[index] = std::clamp(
				(*it)[index].get<float>(), 0.0f, 3600.0f);
		}
	}
	// 临时免疫是读档后的权威状态；旧档或损坏档不能同时保留被免疫的定身。
	if (IsControlImmune(ZombieControlEffect::SLOW) && mCooldownTimer > 0.0f) {
		mCooldownTimer = 0.0f;
		UpdateAnimSpeed();
		UpdateStatusOverlay();
	}
	if (IsControlImmune(ZombieControlEffect::FROZEN) && mFrozenTimer > 0.0f) {
		ClearFrozen();
	}
	if (IsControlImmune(ZombieControlEffect::BUTTER) && mButterTimer > 0.0f) {
		ClearButter();
	}
	if (IsControlImmune(ZombieControlEffect::PARALYSIS) && mParalysisTimer > 0.0f) {
		ClearParalysis();
	}

	mToxinState.reset();
	if (const auto it = j.find("toxinLayerTimers"); it != j.end() && it->is_array()) {
		const std::size_t count = std::min(it->size(), kMaxToxinLayers);
		for (std::size_t index = 0; index < count; ++index) {
			const float timer = std::clamp(
				(*it)[index].get<float>(), 0.0f, kToxinLayerDuration);
			if (timer <= 0.0f) continue;
			if (!mToxinState) mToxinState = std::make_unique<ToxinState>();
			mToxinState->mLayerTimers[mToxinState->mActiveLayerCount++] = timer;
		}
	}
	const float toxinDamageRemainder = std::clamp(
		j.value("toxinDamageRemainder", 0.0f), 0.0f, 0.9999f);
	if (toxinDamageRemainder > 0.0f) {
		if (!mToxinState) mToxinState = std::make_unique<ToxinState>();
		mToxinState->mDamageRemainder = toxinDamageRemainder;
	}
	// 魅惑状态不允许携带任何敌对植物的延迟伤害；旧档也在这里归一化。
	if (mIsMindControlled) {
		mCooldownTimer = 0.0f;
		mFrozenTimer = 0.0f;
		mButterTimer = 0.0f;
		mToxinState.reset();
		mFogBreakoutTriggered = true;
		mArmorBreakRushTimer = 0.0f;
		UpdateAnimSpeed();
		ApplyCharmEffects();
	}
	// 通用麻痹可以来自中立环境并跨魅惑保留；统一按当前全部状态重建最终覆盖色。
	UpdateStatusOverlay();
	SetButterSplatFollowerVisible(mButterTimer > 0.0f && mHasHead && !mIsPreview);

	// 如果播放死亡动画，禁用碰撞箱（预览僵尸已通过宿主入口移除 Collider）。
	if (mIsDying && mCollider) {
		mCollider->mEnabled = false;
	}

	mDyingTimer = j.value("dyingTimer", 0.0f);
	mTangleKelpPlantID = j.value("tangleKelpPlantID", NULL_PLANT_ID);
	mTangleKelpState.reset();
	if (mTangleKelpPlantID != NULL_PLANT_ID) {
		mTangleKelpState = std::make_unique<TangleKelpState>();
		mTangleKelpState->mDraggedUnder =
			j.value("draggedUnderByTangleKelp", false);
		mTangleKelpState->mSinkOffset =
			std::max(0.0f, j.value("tangleKelpSinkOffset", 0.0f));
	}
	mMistFuelReward = std::max(0.0f, j.value("mistFuelReward", 0.0f));
	mMistFuelRewardClaimed = j.value("mistFuelRewardClaimed", false);
	const int ladderPhase = std::clamp(j.value("ladderClimbPhase", 0), 0,
		static_cast<int>(LadderClimbPhase::FALLING));
	mLadderClimbPhase = static_cast<LadderClimbPhase>(ladderPhase);
	mLadderAltitude = std::clamp(j.value("ladderAltitude", 0.0f), 0.0f, 90.0f);
	mUseLadderColumn = j.value("useLadderColumn", -1);
	mGarlicRedirectActive = j.value("garlicRedirectActive", false);
	mGarlicRedirectElapsed = std::clamp(
		j.value("garlicRedirectElapsed", 0.0f), 0.0f, kGarlicWalkEndTime);
	mGarlicRowChanged = mGarlicRedirectActive && j.value("garlicRowChanged", false);
	// 损坏档或旧版调试档不得把已完成阶段恢复成永久停格。
	if (mGarlicRedirectActive && mGarlicRedirectElapsed >= kGarlicWalkEndTime) {
		mGarlicRedirectActive = false;
		mGarlicRedirectElapsed = 0.0f;
		mGarlicRowChanged = false;
	}
	if (mTangleKelpPlantID != NULL_PLANT_ID) {
		CreateTangleKelpGrabAnimators(
			j.value("tangleKelpGrabFrame", kTangleKelpGrabStartFrame));
	}
	if (mDyingTimer >= 10.0f) {
		this->Die();
	}
}

void Zombie::ZombieItemUpdate() const
{
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackImage("Zombie_outerarm_upper", ResourceManager::GetInstance().
			GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_OUTERARM_UPPER2));
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_tongue", false);
		mAnimator->SetTrackVisible("anim_hair", false);
	}
	mAnimator->SetTrackVisible("anim_tongue", mHasTongue);
	UpdatePoolVisualState();
}

void Zombie::Charred()
{
	GameObjectManager::GetInstance().CreateGameObjectImmediate<ZombieCharred>
		(LAYER_GAME_ZOMBIE, ObjectType::OBJECT_ZOMBIE, mBoard, this->GetVisualPosition(),
			AnimationType::ANIM_ZOMBIE_CHARRED, mRow);
	Die();
}

void Zombie::TakePlantAshDamage(int damage)
{
	if (damage <= 0 || !mBoard) return;

	// 化灰阈值必须与 TakeDamage 的最终词条倍率一致；这里只预测是否走表现，真正扣血仍由
	// TakeDamage 单点缩放，避免植物增伤被重复应用。
	const int scaledDamage = ScaleStatusDamage(
		mBoard->GetPerkManager().ScaleTotalDamageToZombie(damage));
	if (CanBeCharred() && mBodyHealth <= scaledDamage) {
		Charred();
		return;
	}
	TakeDamage(damage, DamageSource::PLANT_ASH, false, false, false,
		PlantDamageOrigin::Ash());
}

bool Zombie::TakePlantInstantKill()
{
	if (mBoard) mBoard->MarkTemporalTargetIrreversible(mZombieID);
	Die();
	return true;
}

void Zombie::Start()
{
	AnimatedObject::Start();
	CreateShadow
		(ResourceManager::GetInstance().GetTexture
		(ResourceKeys::Textures::IMAGE_PLANTSHADOW));
	if (this->mIsPreview) {
		RemoveCollider();
	}
	SetAnimationSpeed(GameRandom::Range(1.1f, 1.4f));
	SetupZombie();
	ConfigureButterSplatFollower();
	ConfigureShieldHitGlowTrack();
	mGoldenIceEffectStacks = ComputeGoldenIceEffectStacks();
	// 子类虚函数提供品种能力倍率；最后统一叠加减速、冻结、雨势和场地效果，且跨 PlayTrack 存活。
	if (!mIsPreview) UpdateAnimSpeed();
	// 直接生成在水域内部时首帧就应进入水中，避免等待移动一帧后才裁剪。
	UpdatePoolState(false);
}

void Zombie::CheckWin() const
{
	if (mBoard && mBoard->mCurrentWave >= mBoard->mMaxWave && mBoard->mZombieNumber <= 0)
	{
		WinGame();
	}
}

void Zombie::WinGame() const
{
	if (mBoard->mIsSurvival)
	{
		mBoard->OnSurvivalRoundClear();   // 生存模式：进入下一轮，不出奖杯
	}	
	else
	{
		Vector trophyPos = GetPosition();
		if (trophyPos.x > SCENE_WIDTH - 10)
		{
			trophyPos.x = SCENE_WIDTH - 10;
		}
		else if (trophyPos.x < 20.0f)
		{
			trophyPos.x = 20.0f;
		}
		mBoard->CreateTrophy(trophyPos);
	}
}

void Zombie::Update()
{
	AnimatedObject::Update();
	UpdateShieldHitGlow();
	UpdateFullBodyHealthTextCache();
	if (mTangleKelpState && mTangleKelpState->mGrabBack) {
		mTangleKelpState->mGrabBack->Update();
	}
	if (mTangleKelpState && mTangleKelpState->mGrabFront) {
		mTangleKelpState->mGrabFront->Update();
	}
	if (!mIsPreview) {
		float deltaTime = DeltaTime::GetDeltaTime();
		auto* transform = this->GetTransform();

		if (!transform || !mBoard) return;

		// 标记与毒素不受减速、冻结、啃食和水草早退影响，先推进目标自身的游戏时间。
		UpdateDrumInspiration(deltaTime);
		mPrismMarkRemaining = std::max(0.0f, GetPrismMarkRemaining() - deltaTime);
		UpdateToxin(deltaTime);
		if (!IsActive()) return;
		UpdateSurvivalPerkStates(deltaTime);

		// 突击令按游戏时间衰减，不因啃食、冻结或品种行为早退而变成永久增益。
		UpdateControlImmunity(deltaTime);
		if (IsRoofMarshalAssaultActive()) {
			mRoofMarshalAssaultState->mTimer = std::max(0.0f,
				mRoofMarshalAssaultState->mTimer - deltaTime);
			if (mRoofMarshalAssaultState->mTimer <= 0.0f) {
				mRoofMarshalAssaultState->mMoveMultiplier = 1.0f;
				mRoofMarshalAssaultState->mBiteMultiplier = 1.0f;
				SetRoofMarshalAssaultFlagVisible(false);
			}
		}

		if (mTangleKelpPlantID != NULL_PLANT_ID
			&& !mBoard->mEntityRegistry.GetPlant(mTangleKelpPlantID)) {
			ClearOrphanedTangleKelpGrab();
		}
		if (IsDraggedUnderByTangleKelp()) {
			mTangleKelpState->mSinkOffset += kTangleKelpSinkSpeed * deltaTime;
		}

		if (mIsDying)
		{
			if (mGarlicRedirectActive) CancelGarlicRedirect(false);
			// 定身兜底解除：任何转入死亡的路径都不得停格——死亡动画靠帧事件 Die()，停格即卡尸
			if (mFrozenTimer > 0.0f) ClearFrozen();
			if (mButterTimer > 0.0f) ClearButter();
			if (mParalysisTimer > 0.0f) ClearParalysis();
			mDyingTimer += deltaTime;
			if (GetCurrentTrackName() != GetDeathTrackName() && !mDbgAnomalyLogged) {
				mDbgAnomalyLogged = true;
			}
			if (mDyingTimer >= 20.0f)
			{
				LOG_WARN("DBG") << "WATCHDOG force-die type=" << static_cast<int>(mZombieType)
					<< " track=" << GetCurrentTrackName()
					<< " frame=" << GetCurrentFrame()
					<< " target=" << GetTargetTrack()
					<< " playState=" << static_cast<int>(GetPlayingState())
					<< " hasHead=" << mHasHead << " isEating=" << mIsEating;
				this->Die();
				return;
			}
		}

		// —— 减速滴答（用真实 deltaTime，保证以真实秒数倒计时） ——
		if (mCooldownTimer > 0.0f)
		{
			mCooldownTimer -= deltaTime;
			if (mCooldownTimer <= 0.0f)
			{
				mCooldownTimer = 0.0f;
				UpdateAnimSpeed();
				UpdateStatusOverlay();
			}
		}

		// —— 冻结滴答（同样用真实 deltaTime：定身时长不被减速自身拖慢） ——
		if (mFrozenTimer > 0.0f)
		{
			mFrozenTimer -= deltaTime;
			if (mFrozenTimer <= 0.0f)
			{
				ClearFrozen();   // 解冻：减速尾巴未尽则回 0.6x（蓝光归尾巴管），否则回常速+褪色
			}
		}

		// —— 减速时 50% 缩放僵尸内部逻辑 deltaTime ——
		const float slowMul = (mCooldownTimer > 0.0f) ? 0.5f : 1.0f;
		const float scaledDelta = deltaTime * slowMul;

		mCheckPositionTimer += deltaTime;
		if (mCheckPositionTimer >= 1.0f)
		{
			mCheckPositionTimer = 0.0f;
			Vector position = transform->GetPosition();
			if (position.x <= GAMEOVER_X && CanTriggerGameOver()
				&& mBoard->mBoardState == BoardState::GAME)
			{
				mBoard->GameOver();
			}
			if (IsOutsideWorldCleanupBounds(position))
			{
				this->Die();
			}
		}

		// —— 黄油定身滴答（游戏 deltaTime，倍速下仍按相同游戏秒数到期） ——
		if (mButterTimer > 0.0f)
		{
			mButterTimer -= deltaTime;
			if (mButterTimer <= 0.0f) ClearButter();
		}

		// 通用麻痹与黄油一样使用游戏时间；倍速只改变现实等待，不改变玩法秒数。
		if (mParalysisTimer > 0.0f)
		{
			mParalysisTimer -= deltaTime;
			if (mParalysisTimer <= 0.0f) ClearParalysis();
		}

		// 阵风是空气施加的独立位移：在冻结/啃食的早退前结算，使碰撞箱随 Transform 同帧移动。
		// 水草关系同时充当水底锚点，束缚期间不允许阵风改变僵尸的位置。
		if (mTangleKelpPlantID == NULL_PLANT_ID) {
			ApplyTyphoonGustDrift(deltaTime, transform);
			ApplyRoofRunoffDrift(deltaTime, transform);
		}
		// 大蒜计时使用未减速游戏时间；冻结/黄油在函数内部暂停，换行后则同时收敛纵向位置。
		UpdateGarlicRedirect(deltaTime, transform);
		// 冻结和啃食会在下方早退，阵风横移后仍必须立刻回到屋顶坡面。
		SyncToRoofTerrain(transform);
		// 海豚/撑杆被高坚果挡下后会在碰撞箱外手动开吃，没有碰撞对可产生 onTriggerExit。
		// 因而每帧都以实体生命、顶层身份和实际间距复核一次，阵风吹离或目标死亡时立即收尾。
		if (!mIsDying && mIsEating && mEatPlantID != NULL_PLANT_ID
			&& !IsCurrentPlantEatingTargetValid()) {
			StopEatingInvalidPlantTarget(0.2f);
		}
		// 入水状态是通用介质状态：冻结或啃食期间也要跟随阵风后的实际位置更新。
		if (!mIsDying) UpdatePoolState();
		// 黄色冰道可能在本帧延伸、消失，或被阵风跨越；在冻结/啃食早退前刷新速度场边沿。

		mCheckGoldenIceTimer += deltaTime;
		if (mCheckGoldenIceTimer >= 0.4f)
		{
			mCheckGoldenIceTimer = 0.0f;
			RefreshGoldenIceSpeedState();
		}
		if (mLadderClimbPhase != LadderClimbPhase::NONE
			&& (mIsDying || (mTangleKelpPlantID == NULL_PLANT_ID && !IsImmobilized()))) {
			UpdateLadderClimb(scaledDelta, transform);
		}

		if (!mHasHead)
		{
			// 掉头后本体血量按真实时间流失直至归零（无头僵尸流血而亡）。
			// 流血速率 = 本体血量上限的 10%/秒（纯百分比扣血）。掉头发生在血量降到 ≈上限/3，再流到
			// 死亡阈值 35，故掉头到倒地耗时 ≈ (1/3)/10% ≈ 3 秒，且基本与僵尸血量大小无关：无论普通僵尸
			// 还是后期被生存高轮次/词条（ApplyHealthMultiplier）放大数十倍的僵尸，流血倒地都≈ 2~3.3 秒。
			// 不设固定 HP/秒地板：固定速率在放大后的血量上会让残血需几十秒才流尽（主人反馈“后期减血
			// 太慢”），对普通僵尸又显得过快；纯百分比两头都自洽。下面 1.0f 仅为 mBodyMaxHealth 极小时
			// 的除零兜底（bleedPerSec 当除数），正常僵尸（上限≥270）永不触发、非行为地板。
			// 用未减速的 deltaTime（非 scaledDelta）：流血而亡是确定性死亡机制，速率必须只与
			// 真实时间挂钩——既与帧率无关（高刷不会掉得更快），也不被冰冻减速拖慢（否则高血量
			// 僵尸被冰住时迟迟不死、继续逼近房子）。
			// 累加器保留亚阈值余量、并在长帧（低帧率 / set_timescale 快进）一次补扣多点，速率才恒定。
			float bleedPerSec = mBodyMaxHealth * 0.10f;
			if (bleedPerSec < 1.0f) bleedPerSec = 1.0f;   // 仅除零兜底，非行为地板

			mSubHealthTimer += deltaTime;
			float bleedAmount = mSubHealthTimer * bleedPerSec;
			if (bleedAmount >= 1.0f)
			{
				// 单次最多扣到当前血量：防一次扣过头，也避免 mBodyMaxHealth 极大 + 长帧时 (int) 转换溢出 UB。
				int dmg = (bleedAmount >= static_cast<float>(mBodyHealth))
					? mBodyHealth
					: static_cast<int>(bleedAmount);
				mSubHealthTimer -= dmg / bleedPerSec;   // 仅扣掉已结算的时间，保留亚阈值余量
				mBodyHealth -= dmg;             // 钳在 0：避免跌成负数污染 Board 总血量统计与刷波阈值
				if (mBodyHealth < 0) mBodyHealth = 0;
			}

			if (mBodyHealth <= 35)
			{
				if (!mIsDying)
				{
					if (!ShouldPlayDeathAnimation()) {
						Die();
						return;
					}
					// 定身中也要能倒：先解停格再播死亡动画（帧事件 Die 依赖动画前进）
					if (mFrozenTimer > 0.0f) ClearFrozen();
					if (mButterTimer > 0.0f) ClearButter();
					if (mParalysisTimer > 0.0f) ClearParalysis();
					if (mLadderClimbPhase == LadderClimbPhase::CLIMBING) {
						mLadderClimbPhase = LadderClimbPhase::FALLING;
					}
					CancelGarlicRedirect(false);
					// 死亡轨道开始前立即结束攻击，避免重复啃食帧事件继续伤害目标，
					// 也避免植物的 eaterCount 一直等到死亡动画末帧才归零。
					if (mIsEating) {
						if (mEatPlantID != NULL_PLANT_ID && mBoard) {
							if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
								plant && plant->mEaterCount > 0) {
								--plant->mEaterCount;
							}
						}
						mIsEating = false;
						mEatPlantID = NULL_PLANT_ID;
						mEatZombieID = NULL_ZOMBIE_ID;
						OnStopEating();
					}
					PlayTrack(GetDeathTrackName(), 1.3f, 0.3f);
					if (mCollider) mCollider->mEnabled = false;
					mIsDying = true;
				}
				return;
			}
		}

		// 水草束缚只停移动、碰撞行为和品种逻辑；上方的动画、状态计时与无头流血仍继续。
		if (mTangleKelpPlantID != NULL_PLANT_ID) return;

		// 冻结/黄油定身：移动、啃食推进与子类逻辑全停；上方状态计时照常推进。
		// 啃食帧事件因动画停格（extra=0）自然不触发，mIsEating 状态保留，解冻续啃。
		if (IsImmobilized()) return;
		if (IsGarlicRedirectPaused()) return;

		if (mIsEating) return;

		// 移动类增益只进入位移阶段；固定车速、飞行与普通根运动共用，技能倒计时不被加速。
		ZombieMove(scaledDelta * AmplifySpeedMultiplierForGoldenIce(GetDrumMoveMultiplier())
			* GetAmberMovementMultiplier(), transform);
		// 品种只负责水平推进；坡面高度统一由基类在同帧收敛。
		SyncToRoofTerrain(transform);
		ZombieUpdate(scaledDelta);
	}
}

/**
 * 只在主线程为满血本体血量行取得 pinned 紧致共享纹理。
 * 受伤后的短命数值仍走字形图集，避免长局中每个历史血量都永久占用纹理。
 */
void Zombie::UpdateFullBodyHealthTextCache()
{
	if (mIsPreview || !GameAPP::GetInstance().mShowZombieHP
		|| mBodyHealth != mBodyMaxHealth) {
		return;
	}

	Graphics& graphics = GameAPP::GetInstance().GetGraphics();
	if (mCachedFullBodyHealth == mBodyMaxHealth && mFullBodyHealthText.BindingId() != 0
		&& !graphics.IsCachedTextStale(mFullBodyHealthText)) {
		return;
	}

	mFullBodyHealthText = graphics.AcquireTextTexture(
		u8"本体: " + std::to_string(mBodyHealth) + u8"/" + std::to_string(mBodyMaxHealth),
		ResourceKeys::Fonts::FONT_FZJZ, 15,
		glm::vec4(150.0f, 200.0f, 255.0f, 255.0f));
	mCachedFullBodyHealth = mBodyMaxHealth;
}

void Zombie::FinalizeProtectedLoad()
{
	if (mGarlicRedirectActive && mAnimator) {
		SetGarlicYuckFaceVisible(
			SupportsGarlicYuckFace()
			&& mGarlicRedirectElapsed >= kGarlicFacePauseTime
			&& mGarlicRedirectElapsed < kGarlicWalkEndTime);
		UpdateAnimSpeed();
	}
	SetRoofMarshalAssaultFlagVisible(IsRoofMarshalAssaultActive());
}

/**
 * 屋顶高度是 Board 拥有的地形数据。所有普通、飞行、地下和特殊移动品种都只提交 X，
 * 基类再把逻辑落脚点贴到连续坡面，避免各品种复制一套斜坡公式。
 */
void Zombie::SyncToRoofTerrain(Transform* transform)
{
	if (!transform || !mBoard || mIsPreview || !mBoard->IsRoofBackground()) return;
	// 大蒜换行期间 Y 正向目标行平滑收敛；坡面同步若同帧硬贴目标行会抹掉这段过渡。
	if (mGarlicRedirectActive && mGarlicRowChanged) return;

	Vector position = transform->GetPosition();
	const float terrainY = mBoard->GetZombieSpawnY(mRow, position.x);
	if (terrainY >= 0.0f) {
		position.y = terrainY;
		transform->SetPosition(position);
	}
}

bool Zombie::IsGarlicRedirectPaused() const
{
	return mGarlicRedirectActive && mGarlicRedirectElapsed < kGarlicHoldTime;
}

bool Zombie::IsGarlicYuckFaceVisible() const
{
	return mGarlicRedirectActive && SupportsGarlicYuckFace()
		&& mGarlicRedirectElapsed >= kGarlicFacePauseTime
		&& mGarlicRedirectElapsed < kGarlicWalkEndTime;
}

bool Zombie::SupportsGarlicYuckFace() const
{
	if (!mAnimator || !mAnimator->HasTrack("anim_head1")
		|| !ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEAD_GROSSOUT, false)) {
		return false;
	}

	// 舞王、伴舞与撑杆原版各用专属 grossout 图；当前资源包未提供时必须走短停顿，
	// 不能把普通僵尸脸硬贴到不同骨架。其余特殊品种原版本来也没有嫌恶脸图。
	switch (mZombieType) {
	case ZombieType::ZOMBIE_NORMAL:
	case ZombieType::ZOMBIE_TRAFFIC_CONE:
	case ZombieType::ZOMBIE_BUCKET:
	case ZombieType::ZOMBIE_FASTBUCKET:
	case ZombieType::ZOMBIE_NEWSPAPER:
	case ZombieType::ZOMBIE_FASTPAPER:
	case ZombieType::ZOMBIE_DOOR:
	case ZombieType::ZOMBIE_REINFORCED_DOOR:
	case ZombieType::ZOMBIE_POOL_NORMAL:
	case ZombieType::ZOMBIE_POOL_CONE:
	case ZombieType::ZOMBIE_POOL_BUCKET:
		return true;
	default:
		return false;
	}
}

void Zombie::RestoreHeadImageAfterGarlic()
{
	if (mAnimator) mAnimator->SetTrackImage("anim_head1", nullptr);
}

void Zombie::SetGarlicYuckFaceVisible(bool visible)
{
	if (!mAnimator) return;
	if (visible) {
		if (!SupportsGarlicYuckFace()) return;
		mAnimator->SetTrackImage("anim_head1", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEAD_GROSSOUT, false));
		mAnimator->SetTrackOffset("anim_head1", 0.0f, kGarlicYuckFaceOffsetY);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_head_jaw", false);
		mAnimator->SetTrackVisible("anim_tongue", false);
		return;
	}

	RestoreHeadImageAfterGarlic();
	mAnimator->SetTrackOffset("anim_head1", 0.0f, 0.0f);
	if (mHasHead) {
		mAnimator->SetTrackVisible("anim_head2", true);
		mAnimator->SetTrackVisible("anim_head_jaw", true);
		mAnimator->SetTrackVisible("anim_tongue", mHasTongue);
	}
}

void Zombie::PlayGarlicYuckSound() const
{
	if (!mBoard || !mHasHead) return;

	// 这是每次大蒜反应至多一次的冷路径；按原版只统计仍有头、未魅惑且可见的活动僵尸。
	int zombiesOnScreen = 0;
	for (int id : mBoard->mEntityRegistry.GetAllZombieIDs()) {
		const Zombie* zombie = mBoard->mEntityRegistry.GetZombie(id);
		if (!zombie || !zombie->IsActive() || !zombie->mHasHead
			|| zombie->mIsDying || zombie->mIsMindControlled) {
			continue;
		}
		const float x = zombie->GetPosition().x;
		if (x >= -120.0f && x <= static_cast<float>(SCENE_WIDTH + 120)) {
			++zombiesOnScreen;
		}
	}
	if (zombiesOnScreen > 10 || (zombiesOnScreen > 5 && !GameRandom::Chance())) return;

	// TodFoley 的 Yuck 权重为 YUCK:YUCK2 = 2:1。
	AudioSystem::PlaySound(
		GameRandom::Range(0, 2) < 2
			? ResourceKeys::Sounds::SOUND_YUCK
			: ResourceKeys::Sounds::SOUND_YUCK2,
		0.3f);
}

bool Zombie::ChangeRowForGarlic()
{
	if (!mBoard || mRow < 0 || mRow >= mBoard->mRows) return false;

	int candidates[2] = { -1, -1 };
	int candidateCount = 0;
	const bool currentRowIsPool = mBoard->IsPoolRow(mRow);
	for (const int candidate : { mRow - 1, mRow + 1 }) {
		if (candidate < 0 || candidate >= mBoard->mRows
			|| mBoard->IsPoolRow(candidate) != currentRowIsPool
			|| !mBoard->CanSpawnZombieInRow(mZombieType, candidate)) {
			continue;
		}
		if (mBoard->IsMineBackground()) {
			const int column = static_cast<int>((GetPosition().x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X);
			if (column < 2 || !MineGrid::Valid(candidate, column)
				|| mBoard->mMineGrid.distance[MineGrid::Index(candidate,column)] >= MineGrid::Unreachable
				|| mBoard->MineBlocksSegment(GetPosition(), Vector(GetPosition().x, mBoard->GetZombieSpawnY(candidate, GetPosition().x)))) continue;
		}
		candidates[candidateCount++] = candidate;
	}
	if (candidateCount == 0) return false;

	const int destination = candidates[candidateCount == 1
		? 0
		: GameRandom::Range(0, candidateCount - 1)];
	CommitRow(destination);
	mMineTargetCell = -1;
	return true;
}

void Zombie::CommitRow(int row)
{
	const int previousRow = mRow;
	mRow = row;
	if (mBoard && previousRow != row) {
		mBoard->mEntityRegistry.InvalidateZombieRowIndex();
	}
	GameObjectManager::GetInstance().RefreshRenderOrderForSortingKey(
		this, previousRow);
}

void Zombie::StartGarlicRedirect()
{
	if (mGarlicRedirectActive || mIsPreview || mIsDying || mIsDead || !mBoard) return;
	mGarlicRedirectActive = true;
	mGarlicRedirectElapsed = 0.0f;
	mGarlicRowChanged = false;
	UpdateAnimSpeed();
}

void Zombie::CancelGarlicRedirect(bool stopEating)
{
	if (!mGarlicRedirectActive) return;
	if (stopEating && mIsEating && mEatPlantID != NULL_PLANT_ID) {
		StopEatingInvalidPlantTarget(0.2f);
	}

	// 当前引擎没有原版每帧通用 Y 收敛；中途打断必须落到已提交行，不能把碰撞箱留在行间。
	if (mGarlicRowChanged && mBoard) {
		if (Transform* transform = GetTransform()) {
			Vector position = transform->GetPosition();
			const float targetY = mBoard->GetZombieSpawnY(mRow, position.x);
			if (targetY >= 0.0f) {
				position.y = targetY;
				transform->SetPosition(position);
			}
		}
	}
	SetGarlicYuckFaceVisible(false);
	mGarlicRedirectActive = false;
	mGarlicRedirectElapsed = 0.0f;
	mGarlicRowChanged = false;
	UpdateAnimSpeed();
}

void Zombie::UpdateGarlicRedirect(float deltaTime, Transform* transform)
{
	if (!mGarlicRedirectActive || !mBoard || !transform || mIsPreview || mIsDying || mIsDead) return;
	// 对齐原版 Animate 的定身早退：冻结和黄油都暂停嫌恶计时，解控后从原节点继续。
	if (IsImmobilized()) return;

	const float previousElapsed = mGarlicRedirectElapsed;
	mGarlicRedirectElapsed = std::min(
		kGarlicWalkEndTime, mGarlicRedirectElapsed + std::max(0.0f, deltaTime));
	const bool hasYuckFace = SupportsGarlicYuckFace();

	if (!hasYuckFace
		&& previousElapsed <= kGarlicShortPauseTime
		&& mGarlicRedirectElapsed > kGarlicShortPauseTime) {
		if (mIsEating && mEatPlantID != NULL_PLANT_ID) StopEatingInvalidPlantTarget(0.2f);
		mGarlicRedirectElapsed = kGarlicHoldTime;
		PlayGarlicYuckSound();
	}
	if (hasYuckFace
		&& previousElapsed < kGarlicFacePauseTime
		&& mGarlicRedirectElapsed >= kGarlicFacePauseTime) {
		if (mIsEating && mEatPlantID != NULL_PLANT_ID) StopEatingInvalidPlantTarget(0.2f);
		SetGarlicYuckFaceVisible(true);
		PlayGarlicYuckSound();
	}

	if (!mGarlicRowChanged && mGarlicRedirectElapsed >= kGarlicHoldTime) {
		if (mIsEating && mEatPlantID != NULL_PLANT_ID) StopEatingInvalidPlantTarget(0.2f);
		if (!ChangeRowForGarlic()) {
			CancelGarlicRedirect(false);
			PlayWalkAnimation(0.2f);
			return;
		}
		mGarlicRowChanged = true;
		PlayWalkAnimation(0.2f);
		UpdateAnimSpeed();
	}

	if (mGarlicRowChanged) {
		Vector position = transform->GetPosition();
		const float targetY = mBoard->GetZombieSpawnY(mRow, position.x);
		if (targetY >= 0.0f) {
			const float remaining = targetY - position.y;
			const float step = kGarlicRowMoveSpeed * std::max(0.0f, deltaTime);
			if (std::fabs(remaining) <= step + kGarlicRowMoveEpsilon) position.y = targetY;
			else position.y += remaining > 0.0f ? step : -step;
			transform->SetPosition(position);
		}
	}

	if (mGarlicRedirectElapsed >= kGarlicWalkEndTime) {
		CancelGarlicRedirect(false);
	}
}

/**
 * 将 Board 给出的有符号阵风速度直接叠加到世界坐标。吹向前线时在出生边界内钳位，
 * 避免阵风把刚生成的僵尸推过现有清理线；吹向房屋则保留其真实危险性。
 */
void Zombie::ApplyTyphoonGustDrift(float deltaTime, Transform* transform)
{
	if (!transform || !mBoard || mIsDying || deltaTime <= 0.0f
		|| !CanBeMovedByTyphoonGust()) return;
	const float velocity = mBoard->GetZombieGustDriftVelocity();
	if (velocity == 0.0f) return;
	float displacement = velocity * deltaTime;
	if (velocity > 0.0f) {
		const float limit = mBoard->GetZombieGustFrontLimit();
		const float x = transform->GetPosition().x;
		if (x >= limit) return;
		displacement = std::min(displacement, limit - x);
	}
	transform->Translate(displacement, 0.0f);
}

/**
 * 坡面径流与自主行走正交，即使僵尸正在啃食或被定身也会被水推走；飞行、地下、
 * 弹跳中及明确拒绝台风物理位移的品种不属于屋面地面单位，因此保持原位。
 */
void Zombie::ApplyRoofRunoffDrift(float deltaTime, Transform* transform)
{
	if (!transform || !mBoard || mIsDying || deltaTime <= 0.0f
		|| !CanBeMovedByTyphoonGust() || !CanUseGroundPoolState() || IsFlying()) return;
	const float velocity = mBoard->GetRoofRunoffZombieDriftVelocity(
		mRow, transform->GetPosition().x) * GetRoofRunoffDriftMultiplier();
	if (velocity == 0.0f) return;
	transform->Translate(velocity * deltaTime, 0.0f);
}

/** 双探针同时落入泳池才切入水中，避免僵尸横跨池沿时来回闪动。 */
void Zombie::UpdatePoolState(bool playTransitionFeedback)
{
	if (!mBoard || mIsPreview || mRow < 0) return;
	if (!CanUseGroundPoolState()) {
		if (mInPool) {
			mInPool = false;
			UpdatePoolVisualState();
		}
		return;
	}

	const float x = GetPosition().x;
	const bool shouldBeInPool =
		mBoard->IsPoolWorldPosition(mRow,
			x + kPoolFrontProbeX - kPoolTransitionRightShiftX) &&
		mBoard->IsPoolWorldPosition(mRow,
			x + kPoolRearProbeX - kPoolTransitionRightShiftX);
	if (shouldBeInPool == mInPool) return;

	mInPool = shouldBeInPool;
	UpdatePoolVisualState();
	if (playTransitionFeedback) {
		PlayPoolTransitionFeedback(mInPool);
	}
	// 啃食轨道结束时会经 ResumeWalkAfterEat 读取最新介质；此处不抢占正在播放的啃食。
	if (!mIsEating && !mIsDying) {
		PlayWalkAnimation(kPoolTransitionBlend);
	}
}

/** 以决定介质切换的同一对探针中点为 X，水面裁剪底线为 Y，避免复用 C# 800×600 绝对坐标。 */
Vector Zombie::GetPoolTransitionSplashOrigin() const
{
	const float probeMidpointX = (kPoolFrontProbeX + kPoolRearProbeX) * 0.5f
		- kPoolTransitionRightShiftX;
	return GetPosition() + Vector(probeMidpointX, kPoolClipBottomOffsetY);
}

/** 同时复刻 C# PoolSplash 的 Splash.reanim 与 PlantingPool 两层视觉。 */
void Zombie::PlayPoolSplashVisual(const Vector& origin) const
{
	if (!mBoard || mIsPreview) return;
	GameObjectManager::GetInstance().CreateGameObjectImmediate<PoolSplashVisual>(
		LAYER_EFFECTS_WORLD, mBoard,
		origin + Vector(kPoolSplashAnimOffsetX, kPoolSplashAnimOffsetY));
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("PlantingPool", origin, LAYER_EFFECTS_WORLD);
	}
}

/** 入水沿用 C# Zombiesplash 的两种随机采样，出水固定使用 PlantWater。 */
void Zombie::PlayPoolTransitionFeedback(bool entering) const
{
	PlayPoolSplashVisual(GetPoolTransitionSplashOrigin());
	const std::string& sound = entering && GameRandom::Range(0, 1) != 0
		? ResourceKeys::Sounds::SOUND_ZOMBIE_ENTERING_WATER
		: ResourceKeys::Sounds::SOUND_PLANT_WATER;
	AudioSystem::PlaySound(sound, kPoolSplashSoundVolume);
}

/** 水中不绘制陆地投影；实际水面裁剪在 Draw 内压入 Graphics 裁剪栈。 */
void Zombie::UpdatePoolVisualState() const
{
	if (auto* shadow = GetShadow()) {
		shadow->SetVisible(!mInPool);
	}
}

bool Zombie::IsOutsideWorldCleanupBounds(const Vector& position) const
{
	return position.x > static_cast<float>(SCENE_WIDTH + 65) || position.x < -20.0f;
}

void Zombie::ZombieMove(float scaledDelta, Transform* transform)
{
	float speed = 0.0f;
	// 尝试从 _ground 轨道获取速度
	if (mAnimator) {
		// GetTrackVelocity 内部已乘 animator mSpeed，减速时 SetSpeed(0.5) 已令其自动减半
		//  但 this->mSpeed 是独立的固定项，需单独在减速时乘 0.5 但是deltaTime又变小了，所以不用

		speed = (mGroundTrackIndex >= 0
			? mAnimator->GetTrackVelocity(mGroundTrackIndex)
			: mAnimator->GetTrackVelocity("_ground")) * mSpeed;
		// 台风只缩放水平位移，不改 Animator extra：雨天、减速和冻结仍由 UpdateAnimSpeed
		// 统一组合，啃食、召唤与其他技能不会被顺风误加速。魅惑僵尸按实际向右移动判定顺逆风。
		if (mBoard) {
			speed *= AmplifySpeedMultiplierForGoldenIce(
				mBoard->GetZombieWindMoveMultiplier(IsMovingRight()));
		}
		speed *= GetRoofMarshalAssaultMoveMultiplier();
		if (mBoard && mBoard->IsMineBackground()) {
			mBoard->AdvanceMineZombie(this, std::max(0.0f, speed * scaledDelta));
			return;
		}
		if (IsMovingRight())
		{
			transform->Translate(speed * scaledDelta, 0);
		}
		else
		{
			transform->Translate(-speed * scaledDelta, 0);
		}
	}
}

void Zombie::SetCooldown(float timer, bool bypassShield)
{
	if (!mAnimator || IsControlImmune(ZombieControlEffect::SLOW)
		|| (mShieldType != ShieldType::SHIELDTYPE_NONE && !bypassShield)) return;

	// 已在减速中则取 max，避免短射缩短减速
	mCooldownTimer = std::max(mCooldownTimer, timer);
	UpdateAnimSpeed();
	UpdateStatusOverlay();
}

void Zombie::BeginLadderClimb(int column)
{
	if (column < 0) return;
	mLadderClimbPhase = LadderClimbPhase::CLIMBING;
	mUseLadderColumn = column;
}

bool Zombie::TryStartLadderClimb(Plant* plant)
{
	if (!plant || !mBoard || mIsMindControlled || !CanUseGroundPoolState()
		|| IsFlying() || !mBoard->HasLadderAt(plant->mRow, plant->mColumn)) {
		return false;
	}

	// 原版只会对本次 FindPlantTarget 找到的梯子格 StopEating；碰撞回调可能同帧还扫到
	// 已经爬过的相邻梯子格，不能因此取消正在啃食的下一格植物并反复重播走路动画。
	if (mIsEating && mEatPlantID != NULL_PLANT_ID) {
		Plant* eatingTarget = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
		if (eatingTarget && eatingTarget->mRow == plant->mRow
			&& eatingTarget->mColumn == plant->mColumn) {
			StopEatingInvalidPlantTarget(0.0f);
		}
	}
	if (mLadderClimbPhase == LadderClimbPhase::NONE
		&& mUseLadderColumn != plant->mColumn) {
		BeginLadderClimb(plant->mColumn);
	}
	return true;
}

void Zombie::UpdateLadderClimb(float scaledDelta, Transform* transform)
{
	if (scaledDelta <= 0.0f || !transform) return;
	constexpr float kLadderClimbSpeed = 80.0f; // C# 每厘秒上升 0.8px，折算为 px/s
	constexpr float kLadderFallSpeed = 100.0f; // C# 每厘秒下落 1px，折算为 px/s
	constexpr float kLadderSlowZombieBoost = 50.0f; // 慢速僵尸攀爬时的额外水平速度，单位 px/s
	constexpr float kLadderTargetAltitude = 90.0f; // 原版扶梯顶端离地高度，单位 px
	constexpr float kSlowZombieSpeedThreshold = 16.0f; // 适配本项目根运动倍率的 mVelX<0.5 分界

	if (mIsDying && mLadderClimbPhase == LadderClimbPhase::CLIMBING) {
		mLadderClimbPhase = LadderClimbPhase::FALLING;
	}
	if (mLadderClimbPhase == LadderClimbPhase::CLIMBING) {
		if (!mBoard || !mBoard->HasLadderAt(mRow, mUseLadderColumn)) {
			mLadderClimbPhase = LadderClimbPhase::FALLING;
			return;
		}
		mLadderAltitude = std::min(kLadderTargetAltitude,
			mLadderAltitude + kLadderClimbSpeed * scaledDelta);
		if (mSpeed < kSlowZombieSpeedThreshold) {
			const float distance = kLadderSlowZombieBoost * scaledDelta;
			transform->Translate(IsMovingRight() ? distance : -distance, 0.0f);
		}
		if (mLadderAltitude >= kLadderTargetAltitude) {
			mLadderClimbPhase = LadderClimbPhase::FALLING;
		}
		return;
	}

	mLadderAltitude = std::max(0.0f, mLadderAltitude - kLadderFallSpeed * scaledDelta);
	if (mLadderAltitude <= 0.0f) {
		mLadderAltitude = 0.0f;
		mLadderClimbPhase = LadderClimbPhase::NONE;
	}
}

void Zombie::UpdateAnimSpeed()
{
	if (!mAnimator) return;
	const float forcedMultiplier = GetForcedAnimSpeedMultiplier();
	if (forcedMultiplier >= 0.0f) {
		mAnimator->SetExtraSpeedMultiplier(forcedMultiplier);
		return;
	}
	if (IsImmobilized())
	{
		mAnimator->SetExtraSpeedMultiplier(0.0f);   // 冻结/黄油/麻痹停格：状态层不改各轨道 base 速度
		return;
	}
	if (IsGarlicRedirectPaused()) {
		mAnimator->SetExtraSpeedMultiplier(0.0f);   // 大蒜停顿不改 clip，1.7 秒恢复时可直接续用稳态走路轨道
		return;
	}
	const float rainMultiplier = mBoard ? mBoard->GetZombieRainSpeedMultiplier() : 1.0f;
	mAnimator->SetExtraSpeedMultiplier(
		GetAmplifiedAbilitySpeedMultiplier()
		* AmplifySpeedMultiplierForGoldenIce(
			mCooldownTimer > 0.0f ? GetSlowAnimFactor() : 1.0f)
		* AmplifySpeedMultiplierForGoldenIce(rainMultiplier)
		* (UsesDrumAttackSpeed() ? AmplifySpeedMultiplierForGoldenIce(GetDrumBiteMultiplier()) : 1.0f));
}

int Zombie::GetCountableExecutionHealth() const
{
	const int64_t total = static_cast<int64_t>(std::max(0, mBodyHealth))
		+ static_cast<int64_t>(std::max(0, mHelmHealth))
		+ static_cast<int64_t>(std::max(0, mShieldHealth));
	return total >= INT_MAX ? INT_MAX : static_cast<int>(total);
}

int Zombie::GetCountableMaxHealth() const
{
	const int64_t total = static_cast<int64_t>(std::max(0, mBodyMaxHealth))
		+ static_cast<int64_t>(std::max(0, mHelmMaxHealth))
		+ static_cast<int64_t>(std::max(0, mShieldMaxHealth));
	return total >= INT_MAX ? INT_MAX : static_cast<int>(total);
}

/** 强制清空可计生命后进入既有死亡轨；特殊品种没有死亡轨时退回自己的 Die。 */
void Zombie::TakeHijackerExecution()
{
	if (mIsDead || mIsDying || !IsActive()) return;

	mShieldHealth = 0;
	ShieldDrop();
	mHelmHealth = 0;
	HelmDrop();
	// 直接清本体，避免气球额外生命、免伤次数或品种伤害上限把处决变成普通伤害。
	mBodyHealth = 0;
	if (mHasArm) {
		ArmDrop();
		mHasArm = false;
	}
	if (mHasHead) {
		HeadDrop();
		mHasHead = false;
	}

	CancelGarlicRedirect(false);
	if (mFrozenTimer > 0.0f) ClearFrozen();
	if (mButterTimer > 0.0f) ClearButter();
	if (mParalysisTimer > 0.0f) ClearParalysis();
	if (mIsEating) {
		if (mEatPlantID != NULL_PLANT_ID && mBoard) {
			if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
				plant && plant->mEaterCount > 0) {
				--plant->mEaterCount;
			}
		}
		mIsEating = false;
		mEatPlantID = NULL_PLANT_ID;
		mEatZombieID = NULL_ZOMBIE_ID;
		OnStopEating();
	}

	if (!ShouldPlayDeathAnimation()
		|| !PlayTrack(GetDeathTrackName(), 1.3f, 0.1f)) {
		Die();
		return;
	}
	if (mCollider) mCollider->mEnabled = false;
	mIsDying = true;
}

bool Zombie::ApplyButter()
{
	if (!mHasHead || mIsPreview || mIsDead || mIsDying || mIsMindControlled
		|| !IsActive() || !CanBeButtered()
		|| IsControlImmune(ZombieControlEffect::BUTTER) || IsFlying()
		|| mTangleKelpPlantID != NULL_PLANT_ID
		|| mZombieType == ZombieType::ZOMBIE_ZAMBONI
		|| mZombieType == ZombieType::ZOMBIE_GILDED_ZAMBONI
		|| mZombieType == ZombieType::ZOMBIE_BOSS) {
		return false;
	}

	mButterTimer = kButterDuration;
	SetButterSplatFollowerVisible(true);
	UpdateAnimSpeed();
	return true;
}

bool Zombie::ApplyParalysis(float durationSeconds)
{
	if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f
		|| mIsPreview || mIsDead || mIsDying || !IsActive()
		|| !CanBeParalyzed()
		|| IsControlImmune(ZombieControlEffect::PARALYSIS)) {
		return false;
	}
	mParalysisTimer = std::max(mParalysisTimer,
		std::min(durationSeconds, kMaximumParalysisDuration));
	UpdateAnimSpeed();
	UpdateStatusOverlay();
	return true;
}

void Zombie::ClearParalysis()
{
	mParalysisTimer = 0.0f;
	UpdateAnimSpeed();
	UpdateStatusOverlay();
}

void Zombie::ClearButter()
{
	if (mButterTimer == 0.0f) {
		SetButterSplatFollowerVisible(false);
		return;
	}
	mButterTimer = 0.0f;
	SetButterSplatFollowerVisible(false);
	UpdateAnimSpeed();
}

float Zombie::GetAmplifiedAbilitySpeedMultiplier() const
{
	const float perkMultiplier = mArmorBreakRushTimer > 0.0f
		? kArmorBreakRushSpeedMultiplier : 1.0f;
	return AmplifySpeedMultiplierForGoldenIce(
		GetAbilityAnimSpeedMultiplier() * perkMultiplier);
}

float Zombie::AmplifySpeedMultiplierForGoldenIce(float multiplier) const
{
	float amplified = std::max(0.0f, multiplier);
	for (int stack = 0; stack < mGoldenIceEffectStacks; ++stack) {
		// 中性倍率不变；每个来源让加速项直接乘二、减速项直接减半。
		if (amplified > 1.0f) amplified *= 2.0f;
		else if (amplified < 1.0f) amplified *= 0.5f;
	}
	return amplified;
}

int Zombie::ComputeGoldenIceEffectStacks() const
{
	if (!mBoard || mIsPreview || mIsDead) return IsAlwaysAffectedByGoldenIce() ? 1 : 0;

	int stacks = 0;
	const bool targetIsGilded = dynamic_cast<const GildedZamboniZombie*>(this) != nullptr;
	const int firstSourceRow = std::max(0, mRow - 1);
	const int lastSourceRow = std::min(mBoard->mRows - 1, mRow + 1);
	mBoard->mEntityRegistry.ForEachGoldenIceSource(
		[&](GildedZamboniZombie* gilded) {
			if (!gilded || gilded->mRow < firstSourceRow
				|| gilded->mRow > lastSourceRow
				|| !gilded->IsActive() || gilded->IsDying()) return;
			if (gilded->ProvidesGoldenIceEffectAt(
				mRow, GetPosition().x, targetIsGilded)) {
				++stacks;
			}
		});

	// 车辆死亡后黄色冰道仍保留 35 秒；失去来源身份后继续作为一层非叠加速度场。
	if (stacks == 0 && (IsAlwaysAffectedByGoldenIce()
		|| mBoard->IsGoldenIceAtWorld(mRow, GetPosition().x))) {
		stacks = 1;
	}
	return std::min(stacks, kMaxGoldenIceEffectStacks);
}

void Zombie::RefreshGoldenIceSpeedState()
{
	if (!mBoard || mIsPreview || mIsDead) return;
	const int stacks = ComputeGoldenIceEffectStacks();
	if (stacks == mGoldenIceEffectStacks) return;
	mGoldenIceEffectStacks = stacks;
	UpdateAnimSpeed();
}

bool Zombie::CanBeChilled() const
{
	// 魅惑免疫（原版 CanBeChilled 排除 mMindControlled）；预览/垂死/已死不结算
	return !mIsPreview && !mIsDead && !mIsDying && !mIsMindControlled;
}

bool Zombie::IsControlImmune(ZombieControlEffect effect) const
{
	const std::size_t index = static_cast<std::size_t>(effect);
	if (index >= mControlImmunityTimers.size()) return false;
	return mControlImmunityTimers[index] > 0.0f
		|| (GetPermanentControlImmunityMask() & ZombieControlBit(effect)) != 0;
}

float Zombie::GetControlImmunityTimeRemaining(ZombieControlEffect effect) const
{
	const std::size_t index = static_cast<std::size_t>(effect);
	return index < mControlImmunityTimers.size()
		? mControlImmunityTimers[index] : 0.0f;
}

ZombieControlMask Zombie::GetActiveControlImmunityMask() const
{
	ZombieControlMask mask = GetPermanentControlImmunityMask();
	for (std::size_t index = 0; index < mControlImmunityTimers.size(); ++index) {
		if (mControlImmunityTimers[index] > 0.0f) {
			mask |= ZombieControlMask{ 1 } << index;
		}
	}
	return mask;
}

void Zombie::GrantControlImmunity(
	ZombieControlMask mask, float durationSeconds, bool clearExisting)
{
	if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f
		|| mIsPreview || mIsDead || mIsDying || !IsActive()) return;
	for (std::size_t index = 0; index < mControlImmunityTimers.size(); ++index) {
		if ((mask & (ZombieControlMask{ 1 } << index)) == 0) continue;
		mControlImmunityTimers[index] = std::max(
			mControlImmunityTimers[index], durationSeconds);
	}
	if (!clearExisting) return;
	if ((mask & ZombieControlBit(ZombieControlEffect::SLOW)) != 0
		&& mCooldownTimer > 0.0f) {
		mCooldownTimer = 0.0f;
		UpdateAnimSpeed();
		UpdateStatusOverlay();
	}
	if ((mask & ZombieControlBit(ZombieControlEffect::FROZEN)) != 0
		&& mFrozenTimer > 0.0f) ClearFrozen();
	if ((mask & ZombieControlBit(ZombieControlEffect::BUTTER)) != 0
		&& mButterTimer > 0.0f) ClearButter();
	if ((mask & ZombieControlBit(ZombieControlEffect::PARALYSIS)) != 0
		&& mParalysisTimer > 0.0f) ClearParalysis();
}

void Zombie::UpdateControlImmunity(float deltaTime)
{
	if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) return;
	for (float& timer : mControlImmunityTimers) {
		timer = std::max(0.0f, timer - deltaTime);
	}
}

bool Zombie::StartFrozen(PlantDamageOrigin plantOrigin)
{
	if (!CanBeChilled()) return false;

	const bool wasSlowedOrFrozen = (mCooldownTimer > 0.0f || mFrozenTimer > 0.0f);
	SetCooldown(20.0f);               // 减速尾巴（原版 ApplyChill 2000cs）；持盾守卫在其内部
	if (!CanBeFrozen()) return false; // 撑杆跳跃中等：只吃减速不定身
	if (IsControlImmune(ZombieControlEffect::FROZEN)) {
		// 免疫只挡冻结定身；寒冰菇的固定伤害和不在 mask 内的减速尾巴仍照常结算。
		TakeDamage(20, DamageSource::PLANT, false, false, false, plantOrigin);
		return false;
	}

	mFrozenTimer = wasSlowedOrFrozen
		? GameRandom::Range(3.0f, 4.0f)    // 已减速/已冻再冻缩短（原版 300~400cs，防连放无限定身）
		: GameRandom::Range(4.0f, 6.0f);   // 首冻（原版 400~600cs）
	UpdateAnimSpeed();
	UpdateStatusOverlay();

	// 附带 20 点伤害（原版 HitIceTrap 固定值，在免疫判定之后——魅惑/跳跃中撑杆不掉血）。
	// 走 TakeDamage 正常链（护盾→头盔→本体）；先停格再结算：报纸狂暴等
	// 连锁里的 UpdateAnimSpeed 看到冻结态，不会把停格顶掉。
	TakeDamage(20, DamageSource::PLANT, false, false, false, plantOrigin);
	return true;
}

void Zombie::ClearFrozen()
{
	mFrozenTimer = 0.0f;
	UpdateAnimSpeed();
	UpdateStatusOverlay();
}

void Zombie::RemoveColdEffects()
{
	if (mCooldownTimer <= 0.0f && mFrozenTimer <= 0.0f) return;

	mCooldownTimer = 0.0f;
	mFrozenTimer = 0.0f;
	UpdateAnimSpeed();
	UpdateStatusOverlay();
}

void Zombie::UpdateSurvivalPerkStates(float deltaTime)
{
	if (!mBoard || mIsPreview || mIsDead || mIsDying || !IsActive()) return;
	if (mArmorBreakRushTimer > 0.0f) {
		const float previous = mArmorBreakRushTimer;
		mArmorBreakRushTimer = std::max(0.0f, mArmorBreakRushTimer - deltaTime);
		if (previous > 0.0f && mArmorBreakRushTimer <= 0.0f) UpdateAnimSpeed();
	}
	if (mIsMindControlled) {
		mFogBreakoutTriggered = true;
		return;
	}
	if (!mBoard->GetPerkManager().HasFogBreakout()
		|| !mBoard->SupportsPlanternMechanics() || mFogBreakoutTriggered) return;
	const bool obscured = mBoard->IsZombieObscuredByFog(this);
	if (!mFogBreakoutObserved) {
		mFogBreakoutObserved = true;
		mWasObscuredByFog = obscured;
		return;
	}
	if (mWasObscuredByFog && !obscured) {
		mFogBreakoutTriggered = true;
		GrantControlImmunity(kPerkControlMask, kFogBreakoutImmunitySeconds, true);
	}
	mWasObscuredByFog = obscured;
}

void Zombie::TriggerArmorBreakRush()
{
	if (!mBoard || mIsPreview || mIsDead || mIsDying || mBodyHealth <= 0 || mIsMindControlled
		|| !IsActive() || mArmorBreakRushSpent
		|| !mBoard->GetPerkManager().HasZombieArmorBreakRush()) return;
	mArmorBreakRushSpent = true;
	mArmorBreakRushTimer = kArmorBreakRushSeconds;
	GrantControlImmunity(kPerkControlMask, kArmorBreakRushSeconds, true);
	UpdateAnimSpeed();
}

void Zombie::RepairExistingHealthLayers()
{
	if (mBodyHealth > 0) mBodyHealth = mBodyMaxHealth;
	if (mHelmType != HelmType::HELMTYPE_NONE && mHelmMaxHealth > 0) {
		mHelmHealth = mHelmMaxHealth;
		CheckHelmImage();
	}
	if (mShieldType != ShieldType::SHIELDTYPE_NONE && mShieldMaxHealth > 0) {
		mShieldHealth = mShieldMaxHealth;
		CheckShieldImage();
	}
}

int Zombie::GetToxinLayerCount() const
{
	return mToxinState ? mToxinState->mActiveLayerCount : 0;
}

float Zombie::GetToxinMaxRemaining() const
{
	if (!mToxinState || mToxinState->mActiveLayerCount == 0) return 0.0f;
	return *std::max_element(mToxinState->mLayerTimers.begin(),
		mToxinState->mLayerTimers.begin() + mToxinState->mActiveLayerCount);
}

float Zombie::GetToxinDamageRemainder() const
{
	return mToxinState ? mToxinState->mDamageRemainder : 0.0f;
}

bool Zombie::ApplyToxinStack()
{
	if (mIsPreview || mIsDead || mIsDying || mIsMindControlled || !IsActive()) return false;

	if (!mToxinState) mToxinState = std::make_unique<ToxinState>();
	float* slot = nullptr;
	if (mToxinState->mActiveLayerCount < kMaxToxinLayers) {
		slot = &mToxinState->mLayerTimers[mToxinState->mActiveLayerCount++];
	}
	else {
		// 满层后只刷新最早到期的一层，所有毒囊射手共同受此目标级上限约束。
		slot = &*std::min_element(mToxinState->mLayerTimers.begin(),
			mToxinState->mLayerTimers.end());
	}
	*slot = kToxinLayerDuration;
	UpdateStatusOverlay();
	return true;
}

void Zombie::ClearToxin()
{
	mToxinState.reset();
	UpdateStatusOverlay();
}

void Zombie::UpdateToxin(float deltaTime)
{
	if (!mToxinState || mToxinState->mActiveLayerCount == 0) return;
	if (mIsDead || mIsDying || mIsMindControlled) {
		ClearToxin();
		return;
	}
	if (deltaTime <= 0.0f) return;

	float activeLayerSeconds = 0.0f;
	std::size_t layerIndex = 0;
	while (layerIndex < mToxinState->mActiveLayerCount) {
		float& timer = mToxinState->mLayerTimers[layerIndex];
		activeLayerSeconds += std::min(timer, deltaTime);
		timer = std::max(0.0f, timer - deltaTime);
		if (timer <= 0.0f) {
			const std::size_t lastIndex = --mToxinState->mActiveLayerCount;
			timer = mToxinState->mLayerTimers[lastIndex];
			mToxinState->mLayerTimers[lastIndex] = 0.0f;
		}
		else {
			++layerIndex;
		}
	}

	const bool corrosive = mBoard && mBoard->GetPerkManager().HasCorrosiveToxin();
	const float damagePerLayerSecond = corrosive
		? std::max(1.0f / kToxinDamageInterval,
			static_cast<float>(GetCountableMaxHealth())
				* SurvivalPerkManager::GetInfo(
					PerkType::PLANT_CORROSIVE_TOXIN).perStack)
		: 1.0f / kToxinDamageInterval;
	mToxinState->mDamageRemainder += activeLayerSeconds * damagePerLayerSecond;
	const int damage = static_cast<int>(
		std::floor(mToxinState->mDamageRemainder + kToxinDamageEpsilon));
	if (damage > 0) {
		mToxinState->mDamageRemainder = std::max(
			0.0f, mToxinState->mDamageRemainder - damage);
		// 速度 0 表示从正面命中当前防护层；毒伤永不以背击规则绕过门板。
		if (corrosive) {
			// 百分比毒伤一次结算本逻辑步，避免高生命目标按单点循环；OTHER 仍经过僵尸免伤，
			// 但不会把已经按最大生命计算的基础量再乘全体植物增伤。
			TakeProjectileDamage(damage, DamageSource::OTHER, 0.0f,
				false, false, false,
				PlantDamageOrigin::FromPlant(PlantType::PLANT_TOXICPEASHOOTER));
		}
		else {
			// 普通毒液保留逐点链，免伤次数与旧版时序不变。
			for (int point = 0; point < damage; ++point) {
				TakeProjectileDamage(1, DamageSource::PLANT, 0.0f,
					false, false, false,
					PlantDamageOrigin::FromPlant(PlantType::PLANT_TOXICPEASHOOTER));
				if (!IsActive()) return;
			}
		}
	}
	// 投篮车等品种会在 TakeBodyDamage() 内立即 Die()；死亡会同步释放毒素侧车，
	// 因此任何批量伤害返回后都必须先重新确认宿主和侧车仍有效。
	if (!IsActive() || !mToxinState) return;
	if (mToxinState->mActiveLayerCount == 0) {
		mToxinState.reset();
	}
	UpdateStatusOverlay();
}

void Zombie::UpdateStatusOverlay()
{
	if (!mAnimator) return;
	if (mParalysisTimer > 0.0f) {
		mAnimator->EnableOverlayEffect(true);
		mAnimator->SetOverlayColor(170, 90, 255, 210);
	}
	else if (mIsMindControlled) {
		mAnimator->EnableOverlayEffect(true);
		mAnimator->SetOverlayColor(255, 64, 64, 160);
	}
	else if (mFrozenTimer > 0.0f || mCooldownTimer > 0.0f) {
		mAnimator->EnableOverlayEffect(true);
		mAnimator->SetOverlayColor(80, 80, 255, 240);
	}
	else if (GetToxinLayerCount() > 0) {
		mAnimator->EnableOverlayEffect(true);
		mAnimator->SetOverlayColor(175, 70, 215, 180);
	}
	else {
		mAnimator->EnableOverlayEffect(false);
	}
}

bool Zombie::IsFireResistant() const
{
	return mZombieType == ZombieType::ZOMBIE_ZAMBONI
		|| mZombieType == ZombieType::ZOMBIE_GILDED_ZAMBONI
		|| mZombieType == ZombieType::ZOMBIE_CATAPULT
		|| mShieldType == ShieldType::SHIELDTYPE_DOOR
		|| mShieldType == ShieldType::SHIELDTYPE_LADDER;
}

bool Zombie::ShouldProjectileBypassShield(
	float velocityX, bool requestsShieldBypass) const
{
	if (mShieldType == ShieldType::SHIELDTYPE_NONE) return false;

	// 僵尸面向与自主移动方向一致；同向子弹是在其身后追上，未经过正面的二类护盾。
	if (velocityX != 0.0f && (velocityX > 0.0f) == IsMovingRight()) return true;

	// 主动绕盾是弹丸能力；是否存在不可绕过的防具由目标自己声明，避免子弹侧维护品种表。
	return requestsShieldBypass && !BlocksProjectileShieldBypass();
}

void Zombie::TakeProjectileDamage(
	int damage, DamageSource source, float velocityX, bool penetrateShield,
	bool discardShieldOverflow, bool requestsShieldBypass,
	PlantDamageOrigin plantOrigin)
{
	TakeDamage(damage, source, penetrateShield, discardShieldOverflow,
		ShouldProjectileBypassShield(velocityX, requestsShieldBypass), plantOrigin);
}

void Zombie::ApplyCharmEffects()
{
	// 碰撞：换 CHARMED 层 → 分桶自动落入 seeker 桶（mRowOthers），二分搜同行僵尸；
	// collisionMask 只含 ZOMBIE：不含 PLANT（不误啃植物）、不含 BULLET（子弹判不中）。
	// 植物/子弹/小推车的精确掩码与 CHARMED 两向都不相交 → 它们自动无视魅惑僵尸（CanCollide 是双向 OR）。
	if (mCollider) {
		mCollider->layerMask = CollisionLayer::CHARMED;
		mCollider->collisionMask = CollisionLayer::ZOMBIE;
	}
	// 视觉：按 UpdateStatusOverlay 的统一优先级重建；中立麻痹持续时覆盖魅惑红光。
	if (mAnimator) {
		UpdateStatusOverlay();
		mAnimator->SetFlipX(true, 48.0f);   // 支点≈身体中线（动画局部坐标），截图目验后微调
	}
}

void Zombie::PlayWalkAnimation(float blendTime)
{
	// 有通用 swim 轨道的僵尸入水后自动使用；其余品种保留各自陆地走路轨道并由水面统一裁剪。
	if (mInPool && mAnimator && mAnimator->HasTrack("anim_swim")) {
		PlayTrack("anim_swim", 0.0f, blendTime);
		return;
	}
	// 基类陆地稳态走路：anim_walk2、clip 清零（回落 base 走速）。无此轨道的子类覆写本函数。
	PlayTrack("anim_walk2", 0.0f, blendTime);
}

void Zombie::StartMindControlled()
{
	if (mIsMindControlled || mIsDying || !CanBeCharmed()) return;
	if (mBoard) mBoard->MarkTemporalTargetIrreversible(mZombieID);

	// 正在啃植物：先解除（平衡 mEaterCount）。掩码换掉后与植物不再成对，onTriggerExit 不会补触发。
	if (mIsEating && mEatPlantID != NULL_PLANT_ID && mBoard) {
		if (auto* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID)) {
			plant->mEaterCount--;
		}
		mIsEating = false;
		mEatPlantID = NULL_PLANT_ID;
		ResumeWalkAfterEat(0.2f);
	}

	// 正在啃僵尸也同样解除：魅惑瞬间双方变同阵营，残留一帧可能误伤（帧事件）
	if (mIsEating && mEatZombieID != NULL_ZOMBIE_ID) {
		mIsEating = false;
		mEatZombieID = NULL_ZOMBIE_ID;
		ResumeWalkAfterEat(0.2f);
	}

	// 清减速、冻结和黄油：魅惑目标不保留敌对植物施加的控制，动画立即恢复。
	if (mCooldownTimer > 0.0f || mFrozenTimer > 0.0f || mButterTimer > 0.0f) {
		mCooldownTimer = 0.0f;
		mFrozenTimer = 0.0f;
		mButterTimer = 0.0f;
		SetButterSplatFollowerVisible(false);
		UpdateAnimSpeed();
	}
	ClearToxin();
	mFogBreakoutTriggered = true;
	mArmorBreakRushTimer = 0.0f;
	UpdateAnimSpeed();

	// 如果是最后一波的最后一个僵尸，魅惑后就不会再有僵尸了，直接死亡
	if (mBoard && mBoard->mCurrentWave == mBoard->mMaxWave && mBoard->mZombieNumber == 1)
	{
		this->Die();
	}

	mIsMindControlled = true;
	mPrismMarkRemaining = 0.0f;
	mDrumInspiration.reset();
	ApplyCharmEffects();
	UpdateAnimSpeed(); // 阵营切换即时撤销鼓舞的啃食倍率，不等下一次状态刷新。
	// 天气等中立来源的麻痹可跨阵营保留；麻痹紫色在持续期间优先于魅惑红色。
	UpdateStatusOverlay();
	if (!mIsDead) OnMindControlled();
}

void Zombie::RestoreTemporalCoreState(int row, float x, int bodyHealth,
	HelmType helmType, int helmHealth, ShieldType shieldType, int shieldHealth,
	bool hasHead, bool hasArm,
	float slowTimer, float frozenTimer, float butterTimer,
	float paralysisTimer, bool restorePosition)
{
	if (restorePosition) {
		CommitRow(row);
		SetPosition(Vector(x, mBoard ? mBoard->GetZombieSpawnY(row, x)
			: GetPosition().y));
	}
	mBodyHealth = std::clamp(bodyHealth, 1, mBodyMaxHealth);
	mHelmType = helmType;
	mHelmHealth = std::clamp(helmHealth, 0, mHelmMaxHealth);
	mShieldType = shieldType;
	mShieldHealth = std::clamp(shieldHealth, 0, mShieldMaxHealth);
	mHasHead = hasHead;
	mHasArm = hasArm;
	mCooldownTimer = std::max(0.0f, slowTimer);
	mFrozenTimer = std::max(0.0f, frozenTimer);
	mButterTimer = std::max(0.0f, butterTimer);
	mParalysisTimer = std::max(0.0f, paralysisTimer);
	if (mHelmHealth <= 0) HelmDrop();
	if (mShieldHealth <= 0) ShieldDrop();
	// 存活者可能保留掉甲后的 NONE 阶段，复建者则仍是满甲出生阶段。
	// 必须先按恢复后的耐久重建装备，再让分件更新消费它，不能等下一次受击才换图。
	RefreshEquipmentPresentationAfterRepair();
	ZombieItemUpdate();
	OnTemporalCoreStateRestored();
	UpdateAnimSpeed();
	UpdateStatusOverlay();
}

void Zombie::OnTemporalCoreStateRestored()
{
	if (!mAnimator) return;

	// HeadDrop/ArmDrop 对 Animator 做的是一次性负向覆盖；回溯只恢复布尔值不会自动撤销它们。
	// 这里静默恢复普通骨架的默认分件，不生成第二份掉落物、粒子或声音。
	if (mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", true);
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", true);
		mAnimator->SetTrackImage("Zombie_outerarm_upper", nullptr);
	}
	if (mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", true);
		mAnimator->SetTrackVisible("anim_head2", true);
		mAnimator->SetTrackVisible("anim_hair", true);
		mAnimator->SetTrackVisible("anim_tongue", mHasTongue);
	}
	SetGarlicYuckFaceVisible(IsGarlicYuckFaceVisible());
}

void Zombie::ReconcileTemporalEatingPresentation()
{
	if (!mIsEating || mIsDying || mIsDead || !IsActive()) return;

	if (mEatPlantID != NULL_PLANT_ID) {
		// 回溯位置可能把僵尸带离原目标；失效关系必须走既有的原子收尾，
		// 否则植物 mEaterCount 会残留。
		if (!IsCurrentPlantEatingTargetValid()) {
			StopEatingInvalidPlantTarget(0.12f);
			return;
		}
	}
	else if (mEatZombieID != NULL_ZOMBIE_ID) {
		Zombie* target = mBoard ? mBoard->mEntityRegistry.GetZombie(mEatZombieID) : nullptr;
		if (!target || !target->IsActive() || target->mIsDying || target->mIsDead) {
			mIsEating = false;
			mEatZombieID = NULL_ZOMBIE_ID;
			ResumeWalkAfterEat(0.12f);
			return;
		}
	}
	else {
		// 无目标的啮食位是旧档或异常帧的非法组合，在此收敛为稳态行走。
		mIsEating = false;
		ResumeWalkAfterEat(0.12f);
		return;
	}

	// 局部能力阶段会为非前摇态选择 walk，但它不拥有啮食关系。
	// 最终关系仍有效时由目标本体重播啮食，再让品种钩子恢复手臂、朝向等表现。
	PlayTrack("anim_eat", 2.1f, 0.12f);
	OnStartEating();
}

int Zombie::TakeShieldDamage(int damage)
{
	if (mShieldHealth <= 0)
	{
		mShieldType = ShieldType::SHIELDTYPE_NONE;
		return damage;
	}
	mShieldHealth -= damage;
	CheckShieldImage();
	if (mShieldHealth <= 0)
	{
		int overflow = -mShieldHealth; // 剩余伤害
		mShieldHealth = 0;
		ShieldDrop();
		return overflow;
	}
	return 0;
}

const char* Zombie::GetShieldGlowTrackName(ShieldType shieldType) const
{
	switch (shieldType) {
	case ShieldType::SHIELDTYPE_DOOR:
		return "anim_screendoor";
	case ShieldType::SHIELDTYPE_NEWSPAPER:
		return "Zombie_paper_paper";
	case ShieldType::SHIELDTYPE_LADDER:
		return "Zombie_ladder_1";
	default:
		return nullptr;
	}
}

void Zombie::ConfigureShieldHitGlowTrack()
{
	if (!mAnimator) return;
	if (const char* trackName = GetShieldGlowTrackName(mShieldType)) {
		// 原版把二类护盾放在独立 RENDER_GROUP_SHIELD；本项目用轨道覆盖保留同一高亮语义。
		mAnimator->SetTrackGlowOverride(trackName, false);
	}
}

void Zombie::StartShieldHitGlow(ShieldType shieldType)
{
	if (shieldType == ShieldType::SHIELDTYPE_NONE) return;
	if (mAnimator && mShieldHitGlowType != ShieldType::SHIELDTYPE_NONE
		&& mShieldHitGlowType != shieldType) {
		if (const char* previousTrack = GetShieldGlowTrackName(mShieldHitGlowType)) {
			mAnimator->SetTrackGlowOverride(previousTrack, false);
		}
	}
	mShieldHitGlowType = shieldType;
	mShieldHitGlowTimer = kHitGlowDuration;
	if (mAnimator) {
		if (const char* trackName = GetShieldGlowTrackName(shieldType)) {
			mAnimator->SetTrackGlowOverride(trackName, true);
		}
	}
}

void Zombie::UpdateShieldHitGlow()
{
	if (mShieldHitGlowTimer <= 0.0f) return;
	mShieldHitGlowTimer -= DeltaTime::GetDeltaTime();
	if (mShieldHitGlowTimer > 0.0f) return;

	mShieldHitGlowTimer = 0.0f;
	if (mAnimator) {
		if (const char* trackName = GetShieldGlowTrackName(mShieldHitGlowType)) {
			mAnimator->SetTrackGlowOverride(trackName, false);
		}
	}
	mShieldHitGlowType = ShieldType::SHIELDTYPE_NONE;
}

bool Zombie::IsShieldTrackGlowing() const
{
	if (!mAnimator) return false;
	const ShieldType glowType = mShieldHitGlowType != ShieldType::SHIELDTYPE_NONE
		? mShieldHitGlowType : mShieldType;
	const char* trackName = GetShieldGlowTrackName(glowType);
	return trackName && mAnimator->GetTrackVisible(trackName)
		&& mAnimator->GetTrackGlowEffectEnabled(trackName);
}

int Zombie::TakeHelmDamage(int damage)
{
	if (mHelmHealth <= 0)
	{
		mHelmType = HelmType::HELMTYPE_NONE;
		return damage;
	}
	mHelmHealth -= damage;
	CheckHelmImage();
	if (mHelmHealth <= 0)
	{
		int overflow = -mHelmHealth;
		mHelmHealth = 0;
		HelmDrop();
		return overflow;
	}
	return 0;
}

int Zombie::TakeHelmDamageFromSource(int damage, DamageSource)
{
	return TakeHelmDamage(damage);
}

void Zombie::TakeBodyDamage(int damage)
{
	mBodyHealth -= damage;
	if (mBodyHealth < 0)
		mBodyHealth = 0;

	// 先乘后除：用 64 位算中间量，避免 mBodyMaxHealth 极大时 *2 在 int 内溢出（约 >10.7 亿即翻负）。
	if (mNeedDropArm && mHasArm && mBodyHealth <= static_cast<int64_t>(mBodyMaxHealth) * 2 / 3)
	{
		ArmDrop();
		mHasArm = false;
	}
	if (mNeedDropHead && mHasHead && mBodyHealth <= mBodyMaxHealth / 3)
	{
		HeadDrop();
		mHasHead = false;
		ClearButter();
	}
}

void Zombie::TakeDamage(
	int damage, DamageSource source, bool penetrateShield, bool discardShieldOverflow,
	bool bypassShield, PlantDamageOrigin plantOrigin)
{
	if (damage <= 0 || !mBoard) return;

	// 词条②：僵尸前 N 次免伤（生存专用）。出生时由词条层数设定 mFreeHitsRemaining。
	// 提前 return：完全吸收且不触发受击白光，0 伤害不应闪。
	if (mFreeHitsRemaining > 0) { --mFreeHitsRemaining; return; }
	const ShieldType shieldTypeBeforeHit = mShieldType;
	const HelmType helmTypeBeforeHit = mHelmType;

	// 植物增伤只放大植物来源（普通/灰烬）；僵尸免伤则对所有实际承伤生效。两者均在 0 层返回单位元。
	if (source == DamageSource::PLANT || source == DamageSource::PLANT_ASH) {
		damage = mBoard->GetPerkManager().ScalePlantDamage(damage);
	}
	damage = mBoard->GetPerkManager().ScaleDamageToZombie(damage);
	damage = ScaleStatusDamage(damage);
	damage = AdjustIncomingDamage(damage, source, penetrateShield, bypassShield);
	if (damage <= 0) return;
	if (source == DamageSource::PLANT && !mIsMindControlled
		&& mBoard->ConsumePlantDamageEchoHit()) {
		damage = damage > INT_MAX / 2 ? INT_MAX : damage * 2;
	}
	if (plantOrigin.IsValid() && BlocksPlantDamage(plantOrigin)) return;

	int remainingDamage = TakeExtraProtectionDamage(damage, source);
	// 原版飞行额外生命与头盔/本体共用 mJustGotShotCounter；只要确实吸收了伤害就闪本体层。
	if (remainingDamage < damage) SetGlowingTimer(kHitGlowDuration);
	if (remainingDamage <= 0 || mIsDead || !IsActive()) return;

	// 1. 优先扣除二类护盾
	if (mShieldType != ShieldType::SHIELDTYPE_NONE && !bypassShield)
	{
		const ShieldType shieldTypeBeforeHit = mShieldType;
		const int shieldHealthBeforeHit = mShieldHealth;
		int overflow = TakeShieldDamage(remainingDamage);
		if (mShieldHealth < shieldHealthBeforeHit) {
			StartShieldHitGlow(shieldTypeBeforeHit);
		}
		// 穿透（大喷菇）：护盾照常受损/掉落（触发报纸狂暴等），但全额伤害继续透到头盔+本体；
		// 阻断型护盾会吸收整次喷雾（包括破盾溢出）；普通非穿透伤害仍把击穿溢出传给后续部位。
		remainingDamage = penetrateShield ? damage : (discardShieldOverflow ? 0 : overflow);
	}

	// 2. 然后扣除头盔（穿透不绕过一类头盔，原版仅穿透二类护盾）
	if (remainingDamage > 0 && mHelmType != HelmType::HELMTYPE_NONE)
	{
		if (plantOrigin.IsValid()
			&& TryAdaptHelmetToPlantDamage(remainingDamage, plantOrigin)) {
			remainingDamage = 0;
		}
		const int helmHealthBeforeHit = mHelmHealth;
		if (remainingDamage > 0) {
			remainingDamage = TakeHelmDamageFromSource(remainingDamage, source);
		}
		if (mHelmHealth < helmHealthBeforeHit) {
			SetGlowingTimer(kHitGlowDuration);
		}
	}

	// 3. 最后扣除本体
	if (remainingDamage > 0)
	{
		const int bodyHealthBeforeHit = mBodyHealth;
		TakeBodyDamage(remainingDamage);
		if (mBodyHealth < bodyHealthBeforeHit) {
			SetGlowingTimer(kHitGlowDuration);
		}
	}
	if ((shieldTypeBeforeHit != ShieldType::SHIELDTYPE_NONE
			&& mShieldType == ShieldType::SHIELDTYPE_NONE)
		|| (helmTypeBeforeHit != HelmType::HELMTYPE_NONE
			&& mHelmType == HelmType::HELMTYPE_NONE)) {
		TriggerArmorBreakRush();
	}
}

void Zombie::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_tongue", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	g_particleSystem->EmitEffect("ZombieHeadOff",
		GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void Zombie::ArmDrop()
{
	if (!mHasArm) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_outerarm_upper", ResourceManager::GetInstance().
		GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_OUTERARM_UPPER2));
	g_particleSystem->EmitEffect("ZombieArmOff",
		GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void Zombie::ShieldDrop()
{
	if (mShieldType == ShieldType::SHIELDTYPE_NONE) return;
	mShieldType = ShieldType::SHIELDTYPE_NONE;
}

void Zombie::HelmDrop()
{
	if (mHelmType == HelmType::HELMTYPE_NONE) return;
	mHelmType = HelmType::HELMTYPE_NONE;
}

void Zombie::TakeNightRoofChargeImpact(
	int damage, float paralysisDuration, bool)
{
	TakeDamage(damage, DamageSource::OTHER);
	if (IsActive() && !IsDying()) ApplyParalysis(paralysisDuration);
}

void Zombie::Die()
{
	// 防重入：同帧内可能被调用两次（如自身死亡动画第 216 帧事件 + 大嘴花咬杀帧同帧命中，
	// 此刻 weak_ptr 尚未过期）。重复执行会把 mZombieNumber 多扣一次，导致计数提前归零。
	if (mIsDead) return;
	mIsDead = true;
	mPrismMarkRemaining = 0.0f;
	mDrumInspiration.reset();
	CancelGarlicRedirect(false);
	mButterTimer = 0.0f;
	mParalysisTimer = 0.0f;
	mControlImmunityTimers.fill(0.0f);
	mArmorBreakRushTimer = 0.0f;
	SetButterSplatFollowerVisible(false);
	SetRoofMarshalAssaultFlagVisible(false);
	mRoofMarshalAssaultState.reset();
	mToxinState.reset();

	// 若死亡时仍在啃食植物，手动清理啃食状态（防止 mEaterCount 无法归零）
	if (mIsEating && mEatPlantID != NULL_PLANT_ID && mBoard) {
		if (auto* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID)) {
			plant->mEaterCount--;
		}
		mIsEating = false;
		mEatPlantID = NULL_PLANT_ID;
	}
	mEatZombieID = NULL_ZOMBIE_ID;

	if (mBoard && !mTemporalReplacementRetirement) {
		mBoard->RelayZombieDeathWard(this);
		mBoard->CollectMistFuelFromZombie(this);
		mBoard->mZombieNumber--;
		CheckWin();
	}
	// 禁用碰撞体
	if (mCollider) {
		mCollider->mEnabled = false;
	}
	this->mActive = false;
	if (mBoard) {
		// GOM 会到下一帧开头才释放对象；先让行索引丢弃可能指向本对象的裸指针桶。
		mBoard->mEntityRegistry.InvalidateZombieRowIndex();
	}
	GameObjectManager::GetInstance().DestroyGameObject(this);
}

bool Zombie::RetireForTemporalReplacement()
{
	// 只有仍占用 Board 计数的死亡动画壳允许移交；普通死亡和存活目标必须走原入口。
	if (!mBoard || !mActive || !mIsDying || mIsDead
		|| mTemporalReplacementRetirement) return false;
	mTemporalReplacementRetirement = true;
	Die();
	mTemporalReplacementRetirement = false;
	return mIsDead && !mActive;
}

Vector Zombie::GetVisualPosition() const {
	return GetTransform()->GetPosition()
		+ mVisualOffset + Vector(0.0f, GetTangleKelpSinkOffset() - mLadderAltitude);
}

Vector Zombie::GetButterSplatAnchor() const
{
	const float scale = GetTransform()
		? GetTransform()->GetScale() : 1.0f;
	const char* trackName = GetButterSplatTrackName();
	return mAnimator && trackName && mAnimator->HasTrack(trackName)
		? GetTrackWorldPosition(trackName)
		: GetPosition() + kButterFallbackHeadOffset * scale;
}

bool Zombie::IsButterSplatFollowerVisible() const
{
	const char* trackName = GetButterSplatTrackName();
	return mButterSplatFollowerConfigured && mAnimator && trackName
		&& mAnimator->GetTrackFollowerVisible(trackName, kButterSplatFollowerSlot);
}

bool Zombie::DoesButterSplatFollowerInheritOverlayEffect() const
{
	const char* trackName = GetButterSplatTrackName();
	return mButterSplatFollowerConfigured && mAnimator && trackName
		&& mAnimator->GetTrackFollowerInheritsOverlayEffect(
			trackName, kButterSplatFollowerSlot);
}

bool Zombie::IsButterSplatFollowerGlowing() const
{
	const char* trackName = GetButterSplatTrackName();
	return mButterSplatFollowerConfigured && mAnimator && trackName
		&& mAnimator->GetTrackFollowerGlowEffectEnabled(
			trackName, kButterSplatFollowerSlot);
}

/**
 * 把所有可识别头部轨道的僵尸统一切到 reanim 内分层黄油；缺轨品种保留旧后绘兜底。
 */
void Zombie::ConfigureButterSplatFollower()
{
	mButterSplatFollowerConfigured = false;
	if (!mAnimator) return;

	const char* trackName = GetButterSplatTrackName();
	if (!trackName || !mAnimator->HasTrack(trackName)) return;

	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_CORNPULT_BUTTER_SPLAT);
	if (!texture) return;

	const float followerScale = kButterSplatScale * GetButterSplatScaleMultiplier();
	mAnimator->SetTrackFollowerImage(trackName, kButterSplatFollowerSlot, texture,
		0.0f, kButterSplatOffsetY, followerScale, followerScale,
		ShouldDrawButterSplatAfterAllTracks(),
		/*inheritOverlayEffect=*/false,
		/*inheritGlowEffect=*/false);
	mButterSplatFollowerConfigured = true;
	SetButterSplatFollowerVisible(mButterTimer > 0.0f && mHasHead && !mIsPreview);
}

void Zombie::SetButterSplatFollowerVisible(bool visible) const
{
	if (!mButterSplatFollowerConfigured || !mAnimator) return;
	const char* trackName = GetButterSplatTrackName();
	if (trackName) {
		mAnimator->SetTrackFollowerVisible(trackName, kButterSplatFollowerSlot,
			visible && mHasHead && !mIsPreview);
	}
}

Vector Zombie::GetIceTrapBottomAnchor() const
{
	return GetPosition() + Vector(0.0f, 35.0f);
}

bool Zombie::CanBeTargetedByMagnetShroom() const
{
	return !mIsPreview && !mIsDead && !mIsDying && !mIsMindControlled
		&& mHasHead && IsActive() && HasMagneticItem();
}

Vector Zombie::GetTrackWorldPosition(const std::string& trackName) const
{
	const float scale = GetTransform()
		? GetTransform()->GetScale() : 1.0f;
	const Vector local = mAnimator
		? mAnimator->GetTrackPosition(trackName) : Vector::zero();
	return GetVisualPosition() + local * scale;
}

Vector Zombie::GetRenderedTrackWorldPosition(const std::string& trackName) const
{
	const float scale = GetTransform()
		? GetTransform()->GetScale() : 1.0f;
	Vector local = mAnimator
		? mAnimator->GetTrackPosition(trackName) : Vector::zero();
	// Animator 的水平翻转只发生在提交渲染时；这里复现同一支点反射，供跨 reanim 接续锚点。
	if (mAnimator && mAnimator->GetFlipX()) {
		local.x = 2.0f * mAnimator->GetFlipPivotX() - local.x;
	}
	Vector world = GetVisualPosition() + local * scale;
	if (mAnimator) {
		world.x = mAnimator->GetRenderPivotX()
			+ (world.x - mAnimator->GetRenderPivotX()) * mAnimator->GetRenderScaleX();
		world.y = mAnimator->GetRenderPivotY()
			+ (world.y - mAnimator->GetRenderPivotY()) * mAnimator->GetRenderScaleY();
	}
	return world;
}

bool Zombie::CanBeTargetedByTangleKelp() const
{
	return !mIsPreview && !mIsDead && !mIsDying && !mIsMindControlled
		&& mHasHead && mInPool && mTangleKelpPlantID == NULL_PLANT_ID
		&& mCollider && mCollider->mEnabled && CanBeGrabbedByTangleKelp();
}

bool Zombie::StartTangleKelpGrab(int plantID)
{
	if (plantID == NULL_PLANT_ID) return false;
	if (mTangleKelpPlantID == plantID) {
		if (!mTangleKelpState || !mTangleKelpState->mGrabBack
			|| !mTangleKelpState->mGrabFront) {
			CreateTangleKelpGrabAnimators();
		}
		if (ResistsTangleKelpDrowning()) {
			StopEatingForTangleKelp();
		}
		return true;
	}
	if (!CanBeTargetedByTangleKelp()) return false;

	mTangleKelpPlantID = plantID;
	mTangleKelpState = std::make_unique<TangleKelpState>();
	CreateTangleKelpGrabAnimators();
	if (ResistsTangleKelpDrowning()) {
		StopEatingForTangleKelp();
	}
	return true;
}

void Zombie::DragUnderByTangleKelp(int plantID)
{
	if (mTangleKelpPlantID != plantID || IsDraggedUnderByTangleKelp()) return;
	if (!mTangleKelpState) mTangleKelpState = std::make_unique<TangleKelpState>();
	mTangleKelpState->mDraggedUnder = true;
	StopEatingForTangleKelp();
}

void Zombie::ReleaseTangleKelpGrab(int plantID)
{
	if (mTangleKelpPlantID != plantID) return;
	ClearOrphanedTangleKelpGrab();
}

void Zombie::StopEatingForTangleKelp()
{
	if (mIsEating) {
		if (mEatPlantID != NULL_PLANT_ID && mBoard) {
			if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
			plant && plant->mEaterCount > 0) {
				--plant->mEaterCount;
			}
		}
		mIsEating = false;
		mEatPlantID = NULL_PLANT_ID;
		mEatZombieID = NULL_ZOMBIE_ID;
		ResumeWalkAfterEat(0.2f);
	}
}

void Zombie::CreateTangleKelpGrabAnimators(float savedFrame)
{
	auto reanim = ResourceManager::GetInstance().GetReanimation(
		ResourceKeys::Reanimations::REANIM_TANGLEKELP);
	if (!reanim) return;

	if (!mTangleKelpState) mTangleKelpState = std::make_unique<TangleKelpState>();
	mTangleKelpState->mGrabBack = std::make_shared<Animator>(reanim);
	mTangleKelpState->mGrabFront = std::make_shared<Animator>(reanim);
	for (const auto& animator : {
		mTangleKelpState->mGrabBack, mTangleKelpState->mGrabFront }) {
		animator->PlayTrackOnce("anim_grab", "", kTangleKelpGrabSpeed);
		animator->SetCurrentFrame(std::clamp(
			savedFrame, kTangleKelpGrabStartFrame, kTangleKelpGrabEndFrame));
	}
	mTangleKelpState->mGrabBack->SetTrackVisible("Layer 32", false);
	mTangleKelpState->mGrabFront->SetTrackVisible("Layer 29", false);
}

void Zombie::ClearOrphanedTangleKelpGrab()
{
	mTangleKelpPlantID = NULL_PLANT_ID;
	mTangleKelpState.reset();
}

float Zombie::GetTangleKelpGrabFrame() const
{
	return mTangleKelpState && mTangleKelpState->mGrabFront
		? mTangleKelpState->mGrabFront->GetCurrentFrame()
		: kTangleKelpGrabStartFrame;
}

void Zombie::ApplyRoofMarshalAssault(
	float duration, float moveMultiplier, float biteMultiplier)
{
	if (duration <= 0.0f || mIsDead || mIsDying) return;
	if (!mRoofMarshalAssaultState) {
		mRoofMarshalAssaultState = std::make_unique<RoofMarshalAssaultState>();
	}
	mRoofMarshalAssaultState->mTimer = std::max(
		mRoofMarshalAssaultState->mTimer, duration);
	mRoofMarshalAssaultState->mMoveMultiplier = std::max(
		mRoofMarshalAssaultState->mMoveMultiplier, std::max(1.0f, moveMultiplier));
	mRoofMarshalAssaultState->mBiteMultiplier = std::max(
		mRoofMarshalAssaultState->mBiteMultiplier, std::max(1.0f, biteMultiplier));
	SetRoofMarshalAssaultFlagVisible(true);
}

void Zombie::ConfigureRoofMarshalAssaultFlag()
{
	if (!mRoofMarshalAssaultState) {
		mRoofMarshalAssaultState = std::make_unique<RoofMarshalAssaultState>();
	}
	if (mRoofMarshalAssaultState->mFlagAnimator || !mAnimator) return;
	const char* trackName = GetButterSplatTrackName();
	if (!trackName || !mAnimator->HasTrack(trackName)) return;

	auto reanim = ResourceManager::GetInstance().GetReanimation(
		ResourceKeys::Reanimations::REANIM_ROOF_MARSHAL_ASSAULT_FLAG);
	if (!reanim) return;
	auto flagAnimator = std::make_shared<Animator>(reanim);
	flagAnimator->PlayTrack("anim_idle");
	flagAnimator->SetLocalPosition(kRoofMarshalFlagOffsetX, kRoofMarshalFlagOffsetY);
	flagAnimator->SetAlpha(0.0f);
	if (!mAnimator->AttachAnimator(trackName, flagAnimator)) return;
	mRoofMarshalAssaultState->mFlagAnimator = std::move(flagAnimator);
}

void Zombie::SetRoofMarshalAssaultFlagVisible(bool visible)
{
	if (visible && !HasRoofMarshalAssaultFlagAnimator()) {
		ConfigureRoofMarshalAssaultFlag();
	}
	if (mRoofMarshalAssaultState && mRoofMarshalAssaultState->mFlagAnimator) {
		mRoofMarshalAssaultState->mFlagAnimator->SetAlpha(
			visible && !mIsPreview && !mIsDead && !mIsDying ? 1.0f : 0.0f);
	}
}

bool Zombie::IsRoofMarshalAssaultFlagVisible() const
{
	return IsRoofMarshalAssaultActive() && HasRoofMarshalAssaultFlagAnimator()
		&& mRoofMarshalAssaultState->mFlagAnimator->GetAlpha() > 0.01f
		&& !mIsPreview && !mIsDead && !mIsDying;
}

int Zombie::GetCurrentBiteDamage() const
{
	return std::max(1, static_cast<int>(std::lround(
		static_cast<double>(mAttackDamage) * GetRoofMarshalAssaultBiteMultiplier()
			* GetAbilityBiteDamageMultiplier())));
}

void Zombie::EatTarget()
{
	if (mIsDying || mIsDead) return;
	const int biteDamage = GetCurrentBiteDamage();

	if (mEatZombieID != NULL_ZOMBIE_ID && mHasHead)
	{
		Zombie* target = mBoard ? mBoard->mEntityRegistry.GetZombie(mEatZombieID) : nullptr;
		if (!target || target->mIsDying) {
			// 目标没了/垂死：正常由 onTriggerExit 收尾，这里兜底（含读档后目标失效）
			mIsEating = false;
			mEatZombieID = NULL_ZOMBIE_ID;
			ResumeWalkAfterEat(0.2f);
			return;
		}
		// 互啃走 TakeDamage 正常链（护盾→头盔→本体）：免伤/减伤词条对啃咬同样生效（语义自洽）；
		// 不过 ScaleZombieDamage——那是僵尸对植物的词条
		target->TakeDamage(biteDamage, DamageSource::ZOMBIE);
		if (GameRandom::Range(0, 1) == 0)
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT, 0.17f);
		else
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT2, 0.17f);
		return;
	}
	// 首口大蒜已经建立独立嫌恶状态；后续啃食帧事件即使因长帧同批抵达也不得再造成伤害。
	if (mGarlicRedirectActive) return;
	if (mEatPlantID != NULL_PLANT_ID && mHasHead)
	{
		if (auto* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID)) {
			// C# 原版在每次啃食伤害前重新 FindPlantTarget；这里在伤害帧兜底检查同格最高有效层，
			// 避免可啃上层刚种下时仍伤到支撑层，同时让已离地的倭瓜不再遮挡花盆。
			if (Plant* topPlant = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn);
				topPlant && topPlant != plant && RetargetPlantWithinCell(topPlant)) {
				plant = topPlant;
			}
			if (!IsCurrentPlantEatingTargetValid()) {
				StopEatingInvalidPlantTarget(0.2f);
				return;
			}
			// 魅惑菇：醒着被咬一口即触发——蘑菇立即消失、啃它的僵尸被魅惑（原版 AnimateChewSound：
			// 不结算这口伤害）。睡着（白天）不触发，走下面普通被啃路径。StartMindControlled 内部
			// 有 CanBeCharmed 守卫，对不可魅惑者自动 no-op（蘑菇照样被吃掉，与原版一致）。
			if (plant->mPlantType == PlantType::PLANT_HYPNOSHROOM && !plant->GetSleepState())
			{
				if (auto hypnoShroom = dynamic_cast<HypnoShroom*>(plant))
				{
					if (hypnoShroom->mIsEaten) return;
					hypnoShroom->mIsEaten = true;
					hypnoShroom->Die();
					AudioSystem::PlaySound("SOUND_FLOOP", 0.25f);
					this->StartMindControlled();
					return;
				}
			}
			// 原始攻击力交给植物受伤入口按来源统一结算；不写回 mAttackDamage，避免污染存档。
			const bool plantWasAlive = plant->mPlantHealth > 0;
			plant->OnZombieBite(GetPosition());
			plant->TakeDamage(biteDamage, DamageSource::ZOMBIE);
			if (plantWasAlive && plant->mPlantHealth <= 0
				&& mBoard->GetPerkManager().HasZombieDevourRepair()) {
				RepairExistingHealthLayers();
			}
			if (plant->mPlantHealth <= 0)
			{
				AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_FINISHEAT, 0.2f);
			}

			const bool isGarlic = plant->mPlantType == PlantType::PLANT_GARLIC;
			if (isGarlic)
			{
				// 原版 Garlic 明确使用普通 Chomp；嫌恶音在停吃节点另播。
				AudioSystem::PlaySound(
					GameRandom::Range(0, 1) == 0
						? ResourceKeys::Sounds::SOUND_ZOMBIE_EAT
						: ResourceKeys::Sounds::SOUND_ZOMBIE_EAT2,
					0.17f);
				StartGarlicRedirect();
			}
			else if (UsesSoftChewSound(plant->mPlantType))
			{
				AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT_SOFT, 0.17f);
			}
			else if (GameRandom::Range(0, 1) == 0)
			{
				AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT, 0.17f);
			}
			else
			{
				AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT2, 0.17f);
			}
			if (plant->mPlantHealth <= 0) {
				StopEatingInvalidPlantTarget(0.2f);
			}
		}
		else {
			StopEatingInvalidPlantTarget(0.2f);
		}
	}
}

void Zombie::StartEat(ColliderComponent* other)
{
	// 冻结中不进入啃食态（碰撞 onTriggerStay 每帧重试，解冻后自然补上）
	if (mIsPreview || mIsDying || IsImmobilized() || mGarlicRedirectActive
		|| mTangleKelpPlantID != NULL_PLANT_ID
		|| mLadderClimbPhase != LadderClimbPhase::NONE) return;
	const bool wasEating = mIsEating;   // 仅"本次真开吃"（false→true）触发 OnStartEating，避免每帧 onTriggerStay 重复触发
	auto* gameObject = other->GetGameObject();
	if (gameObject->GetObjectType() == ObjectType::OBJECT_ZOMBIE)
	{
		auto* target = dynamic_cast<Zombie*>(gameObject);
		if (!target) return;
		if (!target->CanBeTargetedByProjectile(false)) return;
		if (target->IsMindControlled() == mIsMindControlled) return;   // 同阵营不啃（魅惑×魅惑掩码本就不成对，此为语义兜底）
		if (target->mIsDying) return;
		if (target->mRow != this->mRow) return;
		if (mEatPlantID != NULL_PLANT_ID || mEatZombieID != NULL_ZOMBIE_ID) return;   // 一次只啃一个目标

		if (!mIsEating) {
			this->PlayTrack("anim_eat", 2.1f, 0.2f);
		}
		mIsEating = true;
		mEatZombieID = target->mZombieID;
		if (!wasEating && mIsEating) OnStartEating();
		return;
	}
	if (gameObject->GetObjectType() == ObjectType::OBJECT_PLANT)
	{
		if (auto* plant = dynamic_cast<Plant*>(gameObject))
		{
			if (mBoard) {
				if (Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn);
					top && top != plant && IsPlantValidEatTarget(top)) {
					plant = top;
				}
			}
			if (TryStartLadderClimb(plant)) return;
			if (!IsPlantValidEatTarget(plant)) return;
			if (mEatZombieID != NULL_ZOMBIE_ID || plant->mRow != this->mRow) return;
			if (mEatPlantID != NULL_PLANT_ID) {
				RetargetPlantWithinCell(plant);
				return;
			}

			if (!mIsEating) {
				this->PlayTrack("anim_eat", 2.1f, 0.2f);
			}
			mIsEating = true;
			mEatPlantID = plant->mPlantID;
			plant->mEaterCount++;
			if (!wasEating && mIsEating) OnStartEating();
		}
	}
}

bool Zombie::IsPlantValidEatTarget(Plant* plant) const
{
	if (!plant || !plant->IsActive() || plant->mPlantHealth <= 0
		|| !plant->CanBeEaten() || plant->mRow != mRow) {
		return false;
	}
	if (!mBoard) return true;
	if (mBoard->MineBlocksSegment(GetPosition(), plant->GetPosition())) return false;

	// C# CanTargetPlant(Chew) 会递归询问 EatingOrder 顶层；顶层若 NotOnGround，
	// 下层仍可成为目标。不能把 Cell 的物理顶层无条件当成唯一合法目标。
	Plant* top = mBoard->GetTopPlantAt(plant->mRow, plant->mColumn);
	return !top || top == plant || !IsPlantValidEatTarget(top);
}

bool Zombie::RetargetPlantWithinCell(Plant* plant)
{
	if (!mBoard || !mIsEating || mEatPlantID == NULL_PLANT_ID
		|| !IsPlantValidEatTarget(plant)) {
		return false;
	}

	Plant* current = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
	if (!current || current == plant
		|| current->mRow != plant->mRow || current->mColumn != plant->mColumn) {
		return false;
	}

	// 目标切换不重播啃食动画，只迁移引用计数；旧荷叶的 exit 回调因 ID 不匹配也不会误停吃。
	if (current->mEaterCount > 0) --current->mEaterCount;
	++plant->mEaterCount;
	mEatPlantID = plant->mPlantID;
	return true;
}

bool Zombie::IsCurrentPlantEatingTargetValid()
{
	if (!mBoard || !mIsEating || mEatPlantID == NULL_PLANT_ID
		|| !mCollider || !mCollider->mEnabled) {
		return false;
	}

	Plant* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
	if (!plant || !plant->IsActive() || plant->mPlantHealth <= 0) {
		return false;
	}
	if (!IsPlantValidEatTarget(plant)) {
		// 读档或同帧换态时，旧目标可能刚变成 NotOnGround；原地迁移到同格最高有效层，
		// 避免先恢复走路一帧、再靠支撑层碰撞重开啃食动画。
		Plant* layers[] = {
			mBoard->GetPumpkinAt(plant->mRow, plant->mColumn),
			mBoard->GetNormalPlantAt(plant->mRow, plant->mColumn),
			mBoard->GetUnderPlantAt(plant->mRow, plant->mColumn),
		};
		Plant* replacement = nullptr;
		for (Plant* candidate : layers) {
			if (candidate != plant && IsPlantValidEatTarget(candidate)) {
				replacement = candidate;
				break;
			}
		}
		if (!replacement || !RetargetPlantWithinCell(replacement)) return false;
		plant = replacement;
	}
	const ColliderComponent* plantCollider = plant->GetColliderComponent();
	if (!plantCollider || !plantCollider->mEnabled) return false;

	const SDL_FRect zombieBounds = mCollider->GetBoundingBox();
	const SDL_FRect plantBounds = plantCollider->GetBoundingBox();
	const bool rowsOverlap = zombieBounds.y < plantBounds.y + plantBounds.h
		&& zombieBounds.y + zombieBounds.h > plantBounds.y;
	if (!rowsOverlap) return false;

	float horizontalGap = 0.0f;
	if (zombieBounds.x > plantBounds.x + plantBounds.w) {
		horizontalGap = zombieBounds.x - (plantBounds.x + plantBounds.w);
	}
	else if (plantBounds.x > zombieBounds.x + zombieBounds.w) {
		horizontalGap = plantBounds.x - (zombieBounds.x + zombieBounds.w);
	}
	return horizontalGap <= kEatingTargetRetentionGap;
}

void Zombie::ReleaseRelocatedPlant(int plantID)
{
	if (mEatPlantID == plantID) StopEatingInvalidPlantTarget(0.0f);
}

void Zombie::StopEatingInvalidPlantTarget(float blendTime)
{
	if (!mIsEating || mEatPlantID == NULL_PLANT_ID) return;
	if (mBoard) {
		if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
			plant && plant->mEaterCount > 0) {
			--plant->mEaterCount;
		}
	}
	mIsEating = false;
	mEatPlantID = NULL_PLANT_ID;
	ResumeWalkAfterEat(blendTime);
}

bool Zombie::CancelEatingForSpecialAction()
{
	if (!mIsEating) return false;
	if (mEatPlantID != NULL_PLANT_ID && mBoard) {
		if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID);
			plant && plant->mEaterCount > 0) {
			--plant->mEaterCount;
		}
	}
	mIsEating = false;
	mEatPlantID = NULL_PLANT_ID;
	mEatZombieID = NULL_ZOMBIE_ID;
	OnStopEating();
	return true;
}

void Zombie::StopEat(ColliderComponent* other)
{
	if (mIsPreview || mIsDying)	return;
	auto* gameObject = other->GetGameObject();
	if (gameObject->GetObjectType() == ObjectType::OBJECT_ZOMBIE)
	{
		auto* target = dynamic_cast<Zombie*>(gameObject);
		if (!target || target->mZombieID != mEatZombieID) return;

		if (mIsEating) {
			this->ResumeWalkAfterEat(0.2f);   // 回落走路（子类可覆写选轨道+clip）
		}
		mIsEating = false;
		mEatZombieID = NULL_ZOMBIE_ID;
		return;
	}
	if (gameObject->GetObjectType() == ObjectType::OBJECT_PLANT)
	{
		if (auto* plant = dynamic_cast<Plant*>(gameObject))
		{
			if (mEatPlantID != plant->mPlantID || plant->mRow != this->mRow) return;

			if (mIsEating) {
				this->ResumeWalkAfterEat(0.2f);   // 收尾+回走路（经模板方法，子类钩子自动生效）
				plant->mEaterCount--;
			}
			mIsEating = false;
			mEatPlantID = NULL_PLANT_ID;
		}
	}
}

Vector Zombie::GetPosition() const
{
	return GetTransform()->GetPosition();
}

void Zombie::SetPosition(const Vector& position)
{
	this->GetTransform()->SetPosition(position);
}

float Zombie::GetCurrentHorizontalMoveSpeed() const
{
	if (mIsDying || mIsDead || !mHasHead || mTangleKelpPlantID != NULL_PLANT_ID
		|| IsImmobilized() || IsGarlicRedirectPaused()
		|| !mAnimator || !mAnimator->IsPlaying()) {
		return 0.0f;
	}
	const float trackSpeed = mGroundTrackIndex >= 0
		? mAnimator->GetTrackAverageVelocity(mGroundTrackIndex)
		: mAnimator->GetTrackAverageVelocity("_ground");
	float velocity = std::fabs(trackSpeed * mSpeed);
	if (mCooldownTimer > 0.0f) velocity *= 0.5f;
	if (mBoard) {
		// 与 ZombieMove 使用同一场地放大顺序，避免黄色冰道上的预测和实际风速分叉。
		velocity *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieWindMoveMultiplier(IsMovingRight()));
	}
	velocity *= GetRoofMarshalAssaultMoveMultiplier()
		* AmplifySpeedMultiplierForGoldenIce(GetDrumMoveMultiplier()) * GetAmberMovementMultiplier();
	return std::max(0.0f, velocity);
}

float Zombie::GetUncontrolledHorizontalMoveSpeed() const
{
	if (mIsDying || mIsDead || !mHasHead || mTangleKelpPlantID != NULL_PLANT_ID
		|| IsGarlicRedirectPaused() || !mAnimator || !mAnimator->IsPlaying()) {
		return 0.0f;
	}
	const float trackSpeed = mGroundTrackIndex >= 0
		? mAnimator->GetTrackAverageVelocity(mGroundTrackIndex)
		: mAnimator->GetTrackAverageVelocity("_ground");
	float velocity = std::fabs(trackSpeed * mSpeed);
	if (mBoard) {
		velocity *= AmplifySpeedMultiplierForGoldenIce(
			mBoard->GetZombieWindMoveMultiplier(IsMovingRight()));
	}
	velocity *= GetRoofMarshalAssaultMoveMultiplier()
		* AmplifySpeedMultiplierForGoldenIce(GetDrumMoveMultiplier()) * GetAmberMovementMultiplier();
	return std::max(0.0f, velocity);
}

float Zombie::GetMineSimulationAttackDps() const
{
	return static_cast<float>(std::max(0,mAttackDamage)) * GetAbilityBiteDamageMultiplier()
		* GetDrumBiteMultiplier() * GetRoofMarshalAssaultBiteMultiplier();
}

float Zombie::GetMineSimulationMoveSpeed() const
{
	if (!mAnimator || !mAnimator->GetReanimation()) return 0;
	const auto* ground = mAnimator->GetReanimation()->GetTrack("_ground");
	const auto range = mAnimator->GetTrackRange(mAnimator->HasTrack("anim_walk2") ? "anim_walk2" : "anim_walk");
	if (!ground || range.first < 0 || range.second <= range.first
		|| range.second >= static_cast<int>(ground->mFrames.size())) return GetUncontrolledHorizontalMoveSpeed();
	float distance = 0;
	for (int frame = range.first; frame < range.second; ++frame)
		distance += std::abs(ground->mFrames[frame+1].x-ground->mFrames[frame].x);
	const float rain = mBoard ? mBoard->GetZombieRainSpeedMultiplier() : 1;
	const float wind = mBoard ? mBoard->GetZombieWindMoveMultiplier(false) : 1;
	return distance/(range.second-range.first) * mSpeed * mAnimator->GetSpeed()
		* GetAmplifiedAbilitySpeedMultiplier() * AmplifySpeedMultiplierForGoldenIce(rain)
		* AmplifySpeedMultiplierForGoldenIce(wind) * GetRoofMarshalAssaultMoveMultiplier()
		* AmplifySpeedMultiplierForGoldenIce(GetDrumMoveMultiplier()) * GetAmberMovementMultiplier();
}

float Zombie::GetTargetLeadX(float seconds) const
{
	float centerX = GetPosition().x;
	if (mCollider) {
		const SDL_FRect bounds = mCollider->GetBoundingBox();
		centerX = bounds.x + bounds.w * 0.5f;
	}
	if (seconds <= 0.0f || mIsEating || mIsDying || mIsDead || !mHasHead
		|| mTangleKelpPlantID != NULL_PLANT_ID
		|| IsImmobilized() || !mAnimator) {
		return centerX;
	}

	const float velocity = GetCurrentHorizontalMoveSpeed();
	return centerX + (IsMovingRight() ? velocity : -velocity) * seconds;
}

void Zombie::Draw(Graphics* g)
{
	float clipBottom = 0.0f;
	const bool clipAtWaterline = g && TryGetDrawClipBottom(clipBottom);
	if (clipAtWaterline) {
		// 水线复用通用 shader ClipRect，不生成 scissor 状态命令，因此不会把逐僵尸绘制拆成独立 draw。
		g->PushClipBottom(clipBottom);
	}

	const Vector grabPosition = GetVisualPosition()
		+ Vector(kTangleKelpGrabOffsetX, kTangleKelpGrabOffsetY);
	const float scale = GetTransform()
		? GetTransform()->GetScale()
		: 1.0f;
	if (mTangleKelpState && mTangleKelpState->mGrabBack) {
		mTangleKelpState->mGrabBack->Draw(g, grabPosition.x, grabPosition.y, scale);
	}
	AnimatedObject::Draw(g);	// 水草后层之后画僵尸本体
	if (g && !mIsPreview && GetDrumInspirationStacks()>0) {
		const Vector p=GetButterSplatAnchor();
		// 最多绘制三个上行标记仅限制表现密度，不限制真实鼓舞层数。
		for (int layer=0; layer<std::min(3,GetDrumInspirationStacks()); ++layer) {
			const float y=p.y-27.0f-layer*6.0f;
			g->DrawLine(p.x-7,y+4,p.x,y,glm::vec4(255,201,64,225));
			g->DrawLine(p.x,y,p.x+7,y+4,glm::vec4(255,201,64,225));
		}
	}
	if (g && GetPrismMarkRemaining() > 0.0f) {
		const Vector p = GetButterSplatAnchor();
		if (const Texture* t = ResourceManager::GetInstance().GetTexture("IMAGE_PRISM_MARK",false)) {
			if (g->IsInstancePathEnabled()) g->DrawTextureInstanced(t,p.x-12,p.y-30,30,30);
			else g->DrawTexture(t,p.x-12,p.y-30,30,30);
		}
	}
	if (g && mBoard && !mIsPreview && !mIsDying) {
		const float protection = mBoard->GetMineFogProtection(this);
		const bool prismMarked = GetPrismMarkRemaining() > 0.0f;
		if (protection > 0.0f || prismMarked) {
			// 贴身流光消费即时雾区资格，不维护第二份护盾状态；普通受击白光增强亮度。
			const Vector p = GetPosition();
			const float alpha = prismMarked ? 105.0f : std::min(220.0f, protection * (mGlowingTimer > 0.0f ? 900.0f : 460.0f));
			const glm::vec4 color = prismMarked ? glm::vec4(255,221,120,alpha)
				: mBoard->HasGoldenMineFog() ? glm::vec4(255,203,83,alpha) : mBoard->HasPurpleMineFog() ? glm::vec4(213,160,255,alpha) : glm::vec4(165,235,255,alpha);
			for (int side : {-1,1}) for (int segment = 0; segment < 18; ++segment) {
				const float t0 = segment/18.0f, t1 = (segment+1)/18.0f;
				const float phase = mBoard->mMineFogElapsed*1.8f + mZombieID*0.7f;
				const float x0 = p.x + side*(23.0f+5.0f*std::sin(t0*6+phase));
				const float x1 = p.x + side*(23.0f+5.0f*std::sin(t1*6+phase));
				const float y0 = p.y-73+t0*82, y1 = p.y-73+t1*82;
				g->DrawLine(x0-1,y0,x1-1,y1,glm::vec4(color.r,color.g,color.b,alpha*0.4f));
				g->DrawLine(x0,y0,x1,y1,color);
				g->DrawLine(x0+1,y0,x1+1,y1,glm::vec4(color.r,color.g,color.b,alpha*0.4f));
			}
		}
	}
	if (mTangleKelpState && mTangleKelpState->mGrabFront) {
		mTangleKelpState->mGrabFront->Draw(g, grabPosition.x, grabPosition.y, scale);
	}

	// 缺少语义头部轨道的未来异形资源才走锚点后绘；当前常规品种均由 reanim 内分层。
	if (g && mButterTimer > 0.0f && mHasHead && !mIsPreview
		&& !mButterSplatFollowerConfigured) {
		if (const Texture* tex = ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_CORNPULT_BUTTER_SPLAT)) {
			const Vector headAnchor = GetButterSplatAnchor();
			const float butterScale = scale * kButterSplatScale
				* GetButterSplatScaleMultiplier();
			const float drawY = headAnchor.y + kButterSplatOffsetY * scale;
			const float drawW = static_cast<float>(tex->width) * butterScale;
			const float drawH = static_cast<float>(tex->height) * butterScale;
			if (g->IsInstancePathEnabled()) {
				// 兜底黄油同样必须跟本体共享实例流，避免并行 replay 把后绘 batch 压到本体下层。
				g->DrawTextureInstanced(tex, headAnchor.x, drawY, drawW, drawH);
			}
			else {
				g->DrawTexture(tex, headAnchor.x, drawY, drawW, drawH);
			}
		}
	}

	// 冻结冰晶（icetrap.png）：画在本体之后=前景，垫在僵尸脚底。
	// 默认路径必须与 reanim 本体进入同一实例流；否则并行 replay 会把同段 batch
	// 提前到 instance 之前，冰晶便会被本体反盖。-NoInstance 保留普通批次兜底。
	// （原版分前后两张 ICETRAP/ICETRAP2，本项目单图取前层简化）
	if (g && mFrozenTimer > 0.0f && !mIsPreview)
	{
		if (auto* tex = ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ICETRAP))
		{
			const Vector bottomAnchor = GetIceTrapBottomAnchor();
			const float iceTrapScale = GetIceTrapScaleMultiplier();
			const float w = static_cast<float>(tex->width) * iceTrapScale;
			const float h = static_cast<float>(tex->height) * iceTrapScale;
			// 普通僵尸仍压在脚底线；车辆覆写后可保持高度并横移到整车中央。
			const float drawX = bottomAnchor.x - w * 0.5f;
			const float drawY = bottomAnchor.y - h;
			if (g->IsInstancePathEnabled()) {
				g->DrawTextureInstanced(tex, drawX, drawY, w, h);
			}
			else {
				g->DrawTexture(tex, drawX, drawY, w, h);
			}
		}
	}

	// 与植物目标提示一样，只读取 Board 锁定 ID 和本实体生命；不会扫描僵尸或植物集合。
	if (g && !mIsPreview && mBoard
		&& mBoard->IsZombieThreatenedByNightRoofHijacker(this) && mCollider) {
		const SDL_FRect bounds = mCollider->GetBoundingBox();
		const float alpha = mBoard->GetNightRoofHijackerPulseAlpha();
		g->DrawRect(bounds.x - 3.0f, bounds.y - 3.0f,
			bounds.w + 6.0f, bounds.h + 6.0f,
			glm::vec4(194.0f, 73.0f, 255.0f, alpha));
	}

	if (clipAtWaterline) {
		g->PopClipBottom();
	}

	if (!g || mIsPreview || !GameAPP::GetInstance().mShowZombieHP) return;
	// 视口剔除：屏外僵尸不画血量文字。11000 压测下绝大多数僵尸堆在屏幕外，
	// 这些 DrawText 会把 ~40 万文字 quad 砸进 128MB batch VBO 致溢出——剔除后省 VBO + CPU。
	if (!g->IsWorldPointVisible(GetPosition().x, GetPosition().y)) return;

	// 直接用逻辑坐标：DrawText 与 Animator 的 DrawTextureMatrix 共享同一 projView，
	// Animator 画 sprite 时就是用裸逻辑坐标，文字必须同坐标系才能叠在对象上（勿转 World）
	Vector pos = GetPosition() + Vector(-40, -40);

	constexpr int   fontSize = 15;
	constexpr float lineHeight = 18.0f;	// 行距 ≈ 字号，逐行向下、无空行
	// 颜色是 0..255 范围（ToSDLColor 直接 static_cast，不乘 255），勿写成 0..1 否则全透明隐形
	const glm::vec4 lightBlue(150.0f, 200.0f, 255.0f, 255.0f);

	float y = pos.y;
	auto drawLine = [&](const std::string& text) {
		g->DrawGlyphRun(text, ResourceKeys::Fonts::FONT_FZJZ, fontSize, lightBlue, pos.x, y);
		y += lineHeight;
		};

	// 满血大军共享紧致整行纹理，把十个字形压成一个实例。当帧血量已变或缓存失效时
	// 立即回退到原逐字路径，不显示过期数值。
	if (mBodyHealth == mBodyMaxHealth && mCachedFullBodyHealth == mBodyMaxHealth
		&& mFullBodyHealthText.BindingId() != 0
		&& !g->IsCachedTextStale(mFullBodyHealthText)) {
		g->DrawCachedText(mFullBodyHealthText, pos.x, y);
		y += lineHeight;
	}
	else {
		drawLine(u8"本体: " + std::to_string(mBodyHealth) + u8"/" + std::to_string(mBodyMaxHealth));
	}
	// 一类防具（有 mHelmType 才显示）
	if (mHelmType != HelmType::HELMTYPE_NONE)
		drawLine(u8"一类: " + std::to_string(mHelmHealth) + u8"/" + std::to_string(mHelmMaxHealth));
	// 二类防具（有 mShieldType 才显示）
	if (mShieldType != ShieldType::SHIELDTYPE_NONE)
		drawLine(u8"二类: " + std::to_string(mShieldHealth) + u8"/" + std::to_string(mShieldMaxHealth));
}

void Zombie::ValidateEatingState(EntityRegistry& em)
{
	// 旧档可能把“正在啃食”与加固铁门的水草束缚同时保存；关系恢复后必须立即结束啃食。
	if (mTangleKelpPlantID != NULL_PLANT_ID && ResistsTangleKelpDrowning()) {
		StopEatingForTangleKelp();
		return;
	}

	if (mIsEating && mEatPlantID != NULL_PLANT_ID) {
		auto plant = em.GetPlant(mEatPlantID);
		if (!plant) {
			mIsEating = false;
			mEatPlantID = NULL_PLANT_ID;
			ResumeWalkAfterEat(0.3f);   // 收尾+回走路（经模板方法，子类钩子自动生效）
		}
		else {
			plant->mEaterCount++;
		}
	}
	else if (mIsEating) {
		// mEatPlantID 为空却在啃：啃僵尸进行时存的档（mEatZombieID 不持久化）→ 回走路，碰撞下一帧重建互啃
		mIsEating = false;
		ResumeWalkAfterEat(0.3f);
	}
}

bool Zombie::TryGetDrawClipBottom(float& clipBottom) const
{
	if (!mInPool || mIsPreview) return false;
	clipBottom = static_cast<float>(static_cast<int>(
		std::lround(GetPosition().y + kPoolClipBottomOffsetY)));
	return true;
}
