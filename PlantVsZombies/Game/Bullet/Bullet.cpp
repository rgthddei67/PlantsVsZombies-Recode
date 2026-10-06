#include "Game/Zombie/PressureShooterRules.h"
#include "Game/Zombie/FloodMortarRules.h"
#include "Game/Plant/RainBambooRules.h"
#include "Game/Board/Board.h"
#include "../Zombie/Zombie.h"
#include "../Plant/Plant.h"
#include "Bullet.h"
#include "Game/Plant/ThunderFlowerRules.h"
#include "../GameObjectManager.h"
#include "../ObjectPool/BulletPool.h"
#include "../ShadowComponent.h"
#include "../AnimatedObject.h"
#include "../Cell.h"
#include "../IceWall.h"
#include "../../GameApp.h"
#include "../../Logger.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
	constexpr int kPeaDamage = 20;                    // 普通/寒冰/孢子基础伤害
	constexpr int kToxicPeaDamage = 15;               // 毒豆直击伤害；持续伤害由目标僵尸结算
	constexpr int kFireballDamage = 40;               // 火豌豆基础伤害，原版为普通豌豆两倍
	constexpr int kCabbageDamage = 40;                // 经典卷心菜直击伤害
	constexpr int kMelonDamage = 120;                  // 经典西瓜直击伤害
	constexpr int kWinterMelonDamage = 100;            // 主人确认：冰瓜较当前西瓜少 20 点直击伤害
	constexpr int kButterDamage = 40;                 // 经典黄油直击伤害；玉米粒沿用普通 20 点
	constexpr int kBasketballDamage = 75;             // 原版投篮车篮球单发伤害
	constexpr int kSaltCrystalCorrosion = 200;        // 盐晶弹对目标当前冰制层的独立腐蚀值；不得溢出本体
	constexpr int kAuroraPeaDamage = 50;               // 极光豌豆对每只实际目标的基础伤害
	constexpr int kCobCannonDamage = 1800;            // 原版 CobBig 爆炸伤害
	constexpr int kSpikeFrameDamage = 2;              // 仙人掌尖刺在 1x 下每个逻辑碰撞帧的基础伤害
	constexpr std::size_t kSpikePierceLimit = 4;       // 尖刺接触第四只不同僵尸后消失
	constexpr std::size_t kAuroraPierceLimit = 4;      // 极光豌豆命中第四只不同僵尸后碎裂回收
	constexpr float kFireballSplashWidth = 100.0f;    // 火球命中后沿飞行方向的同排溅射宽度，单位：像素
	constexpr float kToxicFireballSplashWidth = 30.0f; // 毒素火球以更窄范围换取溅射目标同样叠毒，单位：像素
	constexpr int kSplashDamageDivisor = 3;           // 火球次要目标伤害为直击伤害的三分之一
	constexpr float kMelonSplashWidth = 60.0f;        // C# Melon 弹丸溅射碰撞区的水平宽度，单位：像素
	constexpr int kMelonSplashRowRadius = 1;           // 西瓜溅射覆盖命中行与上下相邻行
	constexpr int kMelonSecondaryBudgetMultiplier = 7; // 原版次要目标总伤害上限是直击的七倍
	constexpr float kWinterMelonSlowSeconds = 10.0f;   // C# ApplyChill(false) 的 1000cs 群体减速时长
	constexpr float kFireballForwardOffsetX = -25.0f; // FirePea.reanim 相对向右飞子弹逻辑原点的 X 偏移
	constexpr float kFireballBackwardOffsetX = 55.0f; // FirePea.reanim 相对向左飞子弹逻辑原点的 X 偏移
	constexpr float kFireballOffsetY = -25.0f;        // FirePea.reanim 相对子弹逻辑原点的 Y 偏移
	constexpr float kFireballImpactOffsetX = 38.0f;   // JalapenoFire 沿飞行方向相对子弹的 X 偏移绝对值
	constexpr float kFireballImpactOffsetY = -20.0f;  // 原版 JalapenoFire 命中特效相对子弹的 Y 偏移
	constexpr float kFireballImpactStartFrame = 3.0f; // Fire.reanim 约 25% 处起播，复刻原版 mAnimTime=0.25
	constexpr float kFireballImpactSpeed = 2.0f;      // Fire.reanim 为 12fps，原版命中特效按 24fps 播放
	constexpr float kFireballImpactScaleX = 0.7f;     // 原版火球命中特效横向缩放
	constexpr float kFireballImpactScaleY = 0.4f;     // 原版火球命中特效纵向缩放
	constexpr SDL_Color kToxicFireOverlay{165, 55, 235, 225}; // 毒豆点燃后的紫焰覆盖色，保留原动画明暗
	constexpr float kThreepeaterVerticalSpeed = 300.0f; // C# 在 100px 草地行高下每 10ms 移动 3px，换算为每秒像素
	constexpr float kThreepeaterDampingPerTick = 0.97f; // C# 每个 10ms 更新对纵向速度的衰减
	constexpr float kOriginalTickSeconds = 0.01f;       // 原版 Projectile 更新步长，单位：秒
	constexpr float kStarSpinSpeedMin = 286.0f;         // 原版 0.05rad/厘秒换算后的最小自旋，单位：度/秒
	constexpr float kStarSpinSpeedMax = 573.0f;         // 原版 0.10rad/厘秒换算后的最大自旋，单位：度/秒
	constexpr float kStarShadowExtraOffsetY = 15.0f;    // C# 星弹初始化时额外下移阴影 15px
	constexpr float kCabbageInitialRotation = -50.4f;   // C# -0.8796rad 的初始朝向，单位：度
	constexpr float kMelonInitialRotation = -72.0f;     // C# -1.2566rad 的初始朝向，单位：度
	constexpr float kCabbageSpinSpeedMin = -458.4f;     // C# -0.08rad/厘秒换算后的自旋下限，单位：度/秒
	constexpr float kCabbageSpinSpeedMax = -114.6f;     // C# -0.02rad/厘秒换算后的自旋上限，单位：度/秒
	constexpr float kKernelInitialRotation = 0.0f;       // C# 玉米粒初始朝向，单位：度
	constexpr float kKernelSpinSpeedMin = -1145.9f;      // C# -0.20rad/厘秒换算后的自旋下限，单位：度/秒
	constexpr float kKernelSpinSpeedMax = -458.4f;       // C# -0.08rad/厘秒换算后的自旋上限，单位：度/秒
	constexpr float kKernelDrawScale = 0.95f;            // C# 玉米粒弹丸绘制缩放
	constexpr float kButterDrawScale = 0.8f;             // C# 黄油弹丸绘制缩放
	constexpr float kBasketballDrawScale = 1.1f;         // C# 篮球弹丸绘制缩放
	constexpr float kCobCannonDrawScale = 0.90f;         // 对齐 CobCannon_cob 在发射前第 77 帧的 sx/sy，避免炮膛到弹丸尺寸跳变
	constexpr float kBasketballSpinSpeedMin = 286.0f;    // 原版 0.05rad/厘秒换算后的最小自旋，单位：度/秒
	constexpr float kBasketballSpinSpeedMax = 573.0f;    // 原版 0.10rad/厘秒换算后的最大自旋，单位：度/秒
	constexpr float kKernelImpactVolume = 0.3f;          // 玉米粒命中或落空 Foley 音量
	constexpr float kButterImpactVolume = 0.3f;          // 黄油命中或落空 Foley 音量
	constexpr float kMelonImpactVolume = 0.3f;           // 西瓜命中或落空 Foley 音量
	constexpr float kLobCollisionArcHeight = 35.0f;     // 下降末段距基准轨迹不超过此高度时才允许碰撞，单位：px
	constexpr float kLobLandingGrace = 0.08f;           // 到达预测点后留给碰撞系统的命中宽限，单位：秒
	constexpr float kLobShadowHeightScale = 200.0f;     // 经典投掷物阴影随高度缩小公式的高度尺度，单位：px
	constexpr float kMelonShadowOffsetX = 6.0f;         // 西瓜弹丸地面阴影相对通用投掷物的右移量，单位：px
	constexpr float kCobShadowWidthMultiplier = 3.0f;   // 原版 Cobbig 把普通弹丸阴影横向拉宽到三倍
	constexpr float kCobShadowSourceOffsetX = 57.0f;    // 原版阴影左边相对 Cobbig 未旋转贴图左边的 X 偏移，单位：px
	constexpr float kCobRiseEndProgress = 0.42f;        // 玉米棒升到画面上方所占总飞行进度
	constexpr float kCobTransferEndProgress = 0.58f;    // 画面外横移到目标上方所占总飞行进度
	constexpr float kCobSkyY = -120.0f;                 // 升空与垂降衔接高度，单位：世界 px

	enum class BulletWindResponse {
		NONE,
		LIGHT_PROJECTILE
	};

	struct BulletWindProfile {
		BulletType type;
		BulletWindResponse response;
	};

	// 与 BulletType 枚举保持同序；新增或重排类型而未明确选择风力响应时编译直接失败。
	constexpr BulletWindProfile kBulletWindProfiles[] = {
		{ BulletType::BULLET_PEA,        BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_SNOWPEA,    BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_CABBAGE,    BulletWindResponse::NONE },
		{ BulletType::BULLET_MELON,      BulletWindResponse::NONE },
		{ BulletType::BULLET_PUFF,       BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_WINTERMELON, BulletWindResponse::NONE },
		{ BulletType::BULLET_FIREBALL,   BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_STAR,       BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_SPIKE,      BulletWindResponse::NONE },
		{ BulletType::BULLET_BASKETBALL, BulletWindResponse::NONE },
		{ BulletType::BULLET_KERNEL,     BulletWindResponse::NONE },
		{ BulletType::BULLET_COBBIG,     BulletWindResponse::NONE },
		{ BulletType::BULLET_BUTTER,     BulletWindResponse::NONE },
		{ BulletType::BULLET_ZOMBIE_PEA, BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_TOXICPEA,   BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_TOXICFIREBALL, BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_MELT_SNOW,  BulletWindResponse::NONE },
		{ BulletType::BULLET_SALT_CRYSTAL, BulletWindResponse::NONE },
		{ BulletType::BULLET_AURORA_PEA,  BulletWindResponse::LIGHT_PROJECTILE },
		{ BulletType::BULLET_THERMAL_PULSE, BulletWindResponse::NONE },
		{ BulletType::BULLET_THUNDER_SEED, BulletWindResponse::NONE },
        { BulletType::BULLET_PRESSURE, BulletWindResponse::NONE },
        { BulletType::BULLET_RAIN_BAMBOO, BulletWindResponse::NONE },
        { BulletType::BULLET_FLOOD_MORTAR, BulletWindResponse::NONE },
	};

	constexpr bool BulletWindProfilesCoverEveryType()
	{
		constexpr std::size_t profileCount = sizeof(kBulletWindProfiles) / sizeof(kBulletWindProfiles[0]);
		if (profileCount != static_cast<std::size_t>(BulletType::NUM_BULLETS)) return false;
		for (std::size_t i = 0; i < profileCount; ++i) {
			if (kBulletWindProfiles[i].type != static_cast<BulletType>(i)) return false;
		}
		return true;
	}

	static_assert(BulletWindProfilesCoverEveryType(),
		"Every BulletType must explicitly select a typhoon wind response");

	BulletWindResponse WindResponseForBullet(BulletType type)
	{
		const int index = static_cast<int>(type);
		if (index < 0 || index >= static_cast<int>(BulletType::NUM_BULLETS)) {
			return BulletWindResponse::NONE;
		}
		return kBulletWindProfiles[index].response;
	}

	/** 返回对象池新建/复用时应恢复的类型基础伤害。 */
	int DefaultDamageForBullet(BulletType type)
	{
		if(type==BulletType::BULLET_RAIN_BAMBOO) return RainBambooRules::Damage[0];
        if(type==BulletType::BULLET_FLOOD_MORTAR) return FloodMortarRules::Damage(0);
        if (type == BulletType::BULLET_PRESSURE) return PressureShooterRules::Damage;
		if (type == BulletType::BULLET_THUNDER_SEED) return ThunderFlowerRules::Damage;
		if (type == BulletType::BULLET_FIREBALL
			|| type == BulletType::BULLET_TOXICFIREBALL) return kFireballDamage;
		if (type == BulletType::BULLET_CABBAGE) return kCabbageDamage;
		if (type == BulletType::BULLET_MELON) return kMelonDamage;
		if (type == BulletType::BULLET_WINTERMELON) return kWinterMelonDamage;
		if (type == BulletType::BULLET_BUTTER) return kButterDamage;
		if (type == BulletType::BULLET_BASKETBALL) return kBasketballDamage;
		if (type == BulletType::BULLET_COBBIG) return kCobCannonDamage;
		if (type == BulletType::BULLET_SPIKE) return kSpikeFrameDamage;
		if (type == BulletType::BULLET_TOXICPEA) return kToxicPeaDamage;
		if (type == BulletType::BULLET_AURORA_PEA) return kAuroraPeaDamage;
		return kPeaDamage;
	}

	/** 返回投射物是否使用西瓜家族的三行溅射与解析抛物线。 */
	bool IsMelonBullet(BulletType type)
	{
		return type == BulletType::BULLET_MELON
			|| type == BulletType::BULLET_WINTERMELON;
	}

	/** 经典投手家族共用弹心绘制、解析阴影和上方绕盾语义。 */
	bool IsClassicLobbedBullet(BulletType type)
	{
		return type == BulletType::BULLET_CABBAGE
			|| IsMelonBullet(type)
			|| type == BulletType::BULLET_KERNEL
			|| type == BulletType::BULLET_BUTTER
			|| type == BulletType::BULLET_MELT_SNOW
			|| type == BulletType::BULLET_SALT_CRYSTAL
			|| type == BulletType::BULLET_BASKETBALL;
	}

	/** 播放玉米粒落地或命中的两段随机 Foley。 */
	void PlayKernelImpactSound()
	{
		AudioSystem::PlaySound(GameRandom::Chance()
			? ResourceKeys::Sounds::SOUND_KERNELPULT
			: ResourceKeys::Sounds::SOUND_KERNELPULT2, kKernelImpactVolume);
	}

	/** 播放西瓜命中或落空的两段随机 Foley。 */
	void PlayMelonImpactSound()
	{
		AudioSystem::PlaySound(GameRandom::Chance()
			? ResourceKeys::Sounds::SOUND_MELONIMPACT
			: ResourceKeys::Sounds::SOUND_MELONIMPACT2, kMelonImpactVolume);
	}

	/** 播放火球命中时的小段 JalapenoFire；依靠 PLAY_ONCE 完成态回收，不新增帧事件。 */
	class FireballImpact final : public AnimatedObject {
	public:
		FireballImpact(Board* board, const Vector& position, bool toxicFlame)
			: AnimatedObject(ObjectType::OBJECT_PARTICLE, board, position,
				AnimationType::ANIM_JALAPENO_FIRE, ColliderType::BOX,
				Vector::zero(), Vector::zero(), 1.0f, "FireballImpact", true),
			mToxicFlame(toxicFlame)
		{
		}

		void Start() override
		{
			GameObject::Start();
			if (!mAnimator) {
				GameObjectManager::GetInstance().DestroyGameObject(this);
				return;
			}

			mAnimator->SetFrameRangeToDefault();
			SetLoopType(PlayState::PLAY_ONCE);
			SetAnimationSpeed(kFireballImpactSpeed);
			SetCurrentFrame(kFireballImpactStartFrame);
			const Vector pivot = GetVisualPosition();
			mAnimator->SetRenderScale(
				kFireballImpactScaleX, kFireballImpactScaleY, pivot.x, pivot.y);
			if (mToxicFlame) {
				mAnimator->SetOverlayColor(kToxicFireOverlay.r, kToxicFireOverlay.g,
					kToxicFireOverlay.b, kToxicFireOverlay.a);
				mAnimator->EnableOverlayEffect(true);
			}
		}

	private:
		bool mToxicFlame = false;
	};
}

struct Bullet::SpikeState {
	std::array<int, kSpikePierceLimit> zombieIDs{};
	std::array<float, kSpikePierceLimit> damageRemainders{};
	std::size_t count = 0;
};

struct Bullet::AuroraState {
	std::array<int, kAuroraPierceLimit> zombieIDs{};
	std::size_t count = 0;
	bool playedHitSound = false;
};

Bullet::Bullet(Board* board, BulletType bulletType, int row, const Vector& colliderRadius,
	const Vector& position) : GameObject(ObjectType::OBJECT_BULLET)
{
	this->mBoard = board;
	this->mBulletType = bulletType;
	this->mPoolType = bulletType;
	this->mDamage = DefaultDamageForBullet(bulletType);
	this->mRow = row;
	if (!mBoard) return;

	CreateTransform(position);
	CreateCollider
		(colliderRadius, Vector(0, 0), ColliderType::CIRCLE);

	// C# Projectile.DrawShadow 明确让 Puff 直接返回；其余现有子弹使用豌豆阴影，
	// Snowpea 再按原版放大到 1.3 倍。实际提交由 BulletPool 的地面阴影阶段负责。
	if (mBulletType != BulletType::BULLET_PUFF) {
		CreateShadow(ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PLANTSHADOW));
		UpdateShadowLayout(position);
	}

	ConfigureCollisionTarget();
    if((mBulletType==BulletType::BULLET_PRESSURE || mBulletType==BulletType::BULLET_RAIN_BAMBOO || mBulletType==BulletType::BULLET_FLOOD_MORTAR) && mCollider) mCollider->mEnabled=false;
}

int Bullet::GetBaseDamage(BulletType type) { return DefaultDamageForBullet(type); }

Bullet::~Bullet() = default;

int Bullet::GetPiercedZombieCount() const
{
	return mSpikeState ? static_cast<int>(mSpikeState->count) : 0;
}

std::vector<int> Bullet::GetPiercedZombieIDs() const
{
	if (!mSpikeState) return {};
	return std::vector<int>(mSpikeState->zombieIDs.begin(),
		mSpikeState->zombieIDs.begin() + mSpikeState->count);
}

std::vector<float> Bullet::GetSpikeDamageRemainders() const
{
	if (!mSpikeState) return {};
	return std::vector<float>(mSpikeState->damageRemainders.begin(),
		mSpikeState->damageRemainders.begin() + mSpikeState->count);
}

void Bullet::Reset(Board* board, int row,
	const Vector& colliderRadius, const Vector& position) {
	mBoard = board;
	mBulletType = mPoolType;
	mPlantDamageOrigin = {};
	mRow = row;
	mHasHit = false;
	mCheckPositionTimer = 0.0f;
	mBulletID = NULL_BULLET_ID;
	mFromPool = true;  // 标记为来自对象池
	// 自定义伤害/速度只属于上一位对象池使用者；不复位会污染后续普通豌豆或孢子。
	mDamage = DefaultDamageForBullet(mPoolType);
	mVelocityX = 290.0f;
	mVelocityY = 0.0f;
	mRotationDegrees = 0.0f;
	mRotationSpeedDegrees = 0.0f;
	mThreepeaterMotion = false;
	mTargetsFlying = false;
	mTrajectory = TrajectoryState{};
	mThermalEndpointX = 0.0f;
	mThermalEndpointY = 0.0f;
	mThermalOriginalPlantID = NULL_PLANT_ID;
	mThermalConfigured = false;
	mHitTorchwoodColumn = -1;
	mHitAuroraTorchwoodColumn = -1;
	if (mSpikeState) mSpikeState->count = 0;
	mAuroraState.reset();
    mBambooHitIDs.clear();mWaterTrailRemaining=0;mFloodCharmed=false;
	mAnimatorAdvancedInParallel = false;
	ConfigurePresentation();
	ConfigureCollisionTarget();
	if (auto* shadow = GetShadow()) shadow->SetEnabled(true);

	// Transform 也是对象池状态；位置、缩放和旋转必须一起恢复中性值。
	if (GetTransform()) GetTransform()->Reset(position);
	UpdateShadowLayout(position);

	// 重置 Collider
	if (mCollider) {
		mCollider->mEnabled = mBulletType != BulletType::BULLET_PRESSURE && mBulletType != BulletType::BULLET_RAIN_BAMBOO && mBulletType != BulletType::BULLET_FLOOD_MORTAR;
	}
}

void Bullet::Start()
{
	GameObject::Start();
	ConfigurePresentation();
}

void Bullet::Die()
{
	// 如果来自对象池，回收到池中
	if (mFromPool) {
		BulletPool* bulletPool = GameObjectManager::GetInstance().GetBulletPool();
		if (bulletPool) {
			bulletPool->Release(this);
			return;
		}
	}

	// 否则正常销毁
	GameObjectManager::GetInstance().DestroyGameObject(this);
}

void Bullet::Update()
{
	GameObject::Update();
	if (mProjectileAnimator) {
		if (mAnimatorAdvancedInParallel) {
			mAnimatorAdvancedInParallel = false;
		}
		else {
			mProjectileAnimator->Update();
		}
	}

	auto* transform = GetTransform();
	float deltaTime = DeltaTime::GetDeltaTime();
	if (transform)
	{
		const Vector previousPosition = transform->GetPosition();
        if(mBulletType==BulletType::BULLET_RAIN_BAMBOO) { UpdateRainBamboo(deltaTime); return; }
        if(mBulletType==BulletType::BULLET_FLOOD_MORTAR) { UpdateFloodMortar(deltaTime); return; }
        if(mBulletType==BulletType::BULLET_PRESSURE) { UpdatePressureProjectile(deltaTime); return; }
		if (mBulletType == BulletType::BULLET_THERMAL_PULSE
			&& mThermalConfigured) {
			const float direction = mVelocityX >= 0.0f ? 1.0f : -1.0f;
			const float rawNextX = previousPosition.x + mVelocityX * deltaTime;
			const float nextX = direction > 0.0f
				? std::min(rawNextX, mThermalEndpointX)
				: std::max(rawNextX, mThermalEndpointX);
			const bool reachedEndpoint = nextX == mThermalEndpointX;
			const float nextY = reachedEndpoint
				? mThermalEndpointY
				: previousPosition.y + mVelocityY * deltaTime;
			transform->SetPosition(Vector(nextX, nextY));

			if (mBoard && mRow >= 0 && mRow < mBoard->mRows) {
				for (int step = 0; step < mBoard->mColumns; ++step) {
					const int column = direction > 0.0f
						? step : mBoard->mColumns - 1 - step;
					const Vector center = mBoard->GetCellCenterPosition(mRow, column);
					const float left = center.x - CELL_COLLIDER_SIZE_X * 0.5f;
					const float right = center.x + CELL_COLLIDER_SIZE_X * 0.5f;
					if (std::max(previousPosition.x, nextX) < left
						|| std::min(previousPosition.x, nextX) > right) continue;
					Cell* cell = mBoard->GetCell(mRow, column);
					if (!cell) continue;
					Plant* normal = mBoard->mEntityRegistry.GetPlant(
						cell->GetNormalPlantID());
					const float segmentLength = nextX - previousPosition.x;
					const float impactX = direction > 0.0f ? left : right;
					const float segmentProgress = segmentLength == 0.0f ? 0.0f
						: std::clamp((impactX - previousPosition.x) / segmentLength,
							0.0f, 1.0f);
					const Vector impact(impactX, previousPosition.y
						+ (nextY - previousPosition.y) * segmentProgress);
					if (normal && normal->TryInterceptHostileStraightProjectile(
						mVelocityX, impact)) {
						if (g_particleSystem) g_particleSystem->EmitEffect(
							"ThermalPulseHit", impact);
						Die();
						return;
					}
					for (const int plantID : { cell->GetPumpkinPlantID(),
						cell->GetNormalPlantID(), cell->GetUnderPlantID() }) {
						Plant* plant = mBoard->mEntityRegistry.GetPlant(plantID);
						if (!plant || !plant->IsActive() || plant->IsSquished()) continue;
						if (plant->mPlantID == mThermalOriginalPlantID
							&& !reachedEndpoint) continue;
						if (plant->mPlantID == mThermalOriginalPlantID) {
							plant->TakeDeploymentInterceptionDamage(mDamage, DamageSource::ZOMBIE);
						}
						else {
							plant->TakeDamage(mDamage, DamageSource::ZOMBIE);
						}
						if (g_particleSystem) g_particleSystem->EmitEffect(
							"ThermalPulseHit", impact);
						AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.42f);
						Die();
						return;
					}
				}
			}
			if (reachedEndpoint) {
				if (g_particleSystem) g_particleSystem->EmitEffect(
						"ThermalPulseHit", Vector(nextX, nextY));
				AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.3f);
				Die();
			}
			return;
		}
		mCheckPositionTimer += deltaTime;
		if (mCheckPositionTimer >= 1.0f)
		{
			mCheckPositionTimer = 0.0f;
			Vector position = transform->GetPosition();
			if (position.x > static_cast<float>(SCENE_WIDTH + 20) ||
				position.x < -10.0f)
			{
				this->Die();
				return;
			}
		}
		if (IsLobbedMotion()) {
			if (!UpdateLobbedMotion(deltaTime)) return;
		}
		else if (IsCobCannonMotion()) {
			if (!UpdateCobCannonMotion(deltaTime)) return;
		}
		else {
			transform->Translate(
				GetWindAdjustedVelocityX() * deltaTime, mVelocityY * deltaTime);
		}
		const Vector position = transform->GetPosition();
		if (mBoard && !IsLobbedMotion() && !IsCobCannonMotion()
			&& mBoard->MineBlocksSegment(previousPosition, position)) {
			Die();
			return;
		}
		if (mRotationSpeedDegrees != 0.0f) {
			mRotationDegrees = std::fmod(
				mRotationDegrees + mRotationSpeedDegrees * deltaTime, 360.0f);
		}
		if (mBulletType == BulletType::BULLET_STAR) {
			if (position.y < 0.0f || position.y > static_cast<float>(SCENE_HEIGHT)) {
				Die();
				return;
			}
			UpdateStarRow(position);
		}
		// 平射墙判定必须早于 CollisionSystem 的僵尸回调，保证同帧穿越时墙先承伤。
		if (!IsLobbedMotion() && !IsCobCannonMotion()
			&& HitIceWallIfNeeded(previousPosition.x, position.x)) {
			return;
		}
		UpdateShadowLayout(position);
		if (HitsRoofTerrain(position)) {
			HitRoofTerrain();
			return;
		}
		if (mThreepeaterMotion && !IsLobbedMotion()) {
			// 用指数折算保持不同固定步长下与 C# “每 10ms ×0.97”相同的弧线。
			mVelocityY *= std::pow(
				kThreepeaterDampingPerTick, deltaTime / kOriginalTickSeconds);
		}
	}
}

void Bullet::UpdateParallel(std::vector<DeferredEvent>& outBuf)
{
	if (!mProjectileAnimator) {
		mAnimatorAdvancedInParallel = false;
		return;
	}
	mProjectileAnimator->UpdateParallelDeferred(outBuf);
	mAnimatorAdvancedInParallel = true;
}

void Bullet::Draw(Graphics* g)
{
	if (mBulletType == BulletType::BULLET_THERMAL_PULSE && g) {
		const Vector position = GetPosition();
		g->FillCircle(position.x, position.y, 13.0f,
			glm::vec4(255.0f, 68.0f, 20.0f, 72.0f), 22);
		if (mTexture) {
			const float width = static_cast<float>(mTexture->width) * mScale;
			const float height = static_cast<float>(mTexture->height) * mScale;
			g->DrawTexture(mTexture, position.x - width * 0.5f,
				position.y - height * 0.5f, width, height);
		}
		else {
			g->FillCircle(position.x, position.y, 8.0f,
				glm::vec4(255.0f, 236.0f, 120.0f, 250.0f), 18);
		}
		return;
	}
	if (mProjectileAnimator) {
		const Vector position = GetPosition();
		const float offsetX = mVelocityX < 0.0f
			? kFireballBackwardOffsetX : kFireballForwardOffsetX;
		mProjectileAnimator->Draw(
			g, position.x + offsetX, position.y + kFireballOffsetY, 1.0f);
		return;
	}

    if(mBulletType==BulletType::BULLET_FLOOD_MORTAR && IsLobbedMotion()) {
        const float radius=18+5*std::sin(mTrajectory.elapsed*12);
        g->DrawCircle(mTrajectory.target.x,mTrajectory.target.y,radius,glm::vec4(35,210,255,210),32);
        g->DrawCircle(mTrajectory.target.x,mTrajectory.target.y,radius+6,glm::vec4(220,250,255,150),32);
    }
	if (mTexture) {
		Vector position = GetPosition();
		float drawWidth = static_cast<float>(mTexture->width) * mScale;
		float drawHeight = static_cast<float>(mTexture->height) * mScale;
		if (IsCobCannonMotion()) {
			// DrawTexture 的非方形旋转会先按目标框缩放；90 度炮弹须交换目标宽高，
			// 才能保持炮膛内 CobCannon_cob 的原始长宽比而不是横向拉伸。
			drawWidth = static_cast<float>(mTexture->height) * mScale;
			drawHeight = static_cast<float>(mTexture->width) * mScale;
			position.x -= drawWidth * 0.5f;
			position.y -= drawHeight * 0.5f;
		}
		if (IsClassicLobbedBullet(mBulletType) || mBulletType==BulletType::BULLET_FLOOD_MORTAR || mBulletType==BulletType::BULLET_RAIN_BAMBOO) {
			position.x -= drawWidth * 0.5f;
			position.y -= drawHeight * 0.5f;
		}
        if(mBulletType==BulletType::BULLET_PRESSURE) {
            position.x-=drawWidth*.5f; position.y-=drawHeight*.5f;
            mRotationDegrees=mVelocityX>0 ? 180.0f : 0.0f;
        }
		g->DrawTexture(mTexture, position.x, position.y,
			drawWidth, drawHeight, mRotationDegrees);
	}
}

void Bullet::ConfigureThermalPulse(const Vector& endpoint, int originalPlantID)
{
	mThermalEndpointX = endpoint.x;
	mThermalEndpointY = endpoint.y;
	mThermalOriginalPlantID = originalPlantID;
	mThermalConfigured = mBulletType == BulletType::BULLET_THERMAL_PULSE;
	if (mThermalConfigured && GetTransform() && mVelocityX != 0.0f) {
		const Vector origin = GetTransform()->GetPosition();
		const float travelSeconds = std::abs(
			(mThermalEndpointX - origin.x) / mVelocityX);
		mVelocityY = travelSeconds > 0.0f
			? (mThermalEndpointY - origin.y) / travelSeconds : 0.0f;
	}
	if (mCollider) mCollider->mEnabled = false;
	if (auto* shadow = GetShadow()) shadow->SetEnabled(false);
}

void Bullet::DrawShadow(Graphics* g)
{
	if (auto* shadow = GetShadow()) {
		shadow->Draw(g);
	}
}

void Bullet::UpdateShadowLayout(const Vector& position)
{
	auto* shadow = GetShadow();
	if (!shadow) return;

	// 原分辨率 IMAGE_PEA_SHADOWS 是 42x9 的日/夜两格图，因此单格为 21x9；
	// C# Snowpea 分支把两轴统一放大 1.3 倍。
	constexpr float kPeaShadowWidth = 21.0f;
	constexpr float kPeaShadowHeight = 9.0f;
	float typeScale = 1.0f;
	if (mBulletType == BulletType::BULLET_SNOWPEA) {
		typeScale = 1.3f;
	}
	else if (mBulletType == BulletType::BULLET_FIREBALL
		|| mBulletType == BulletType::BULLET_TOXICFIREBALL) {
		typeScale = 1.4f;
	}
	else if (IsClassicLobbedBullet(mBulletType)) {
		const float height = IsLobbedMotion()
			? GetLobArcHeight()
			: std::max(0.0f, GetTerrainShadowY(position) - position.y);
		typeScale = std::clamp(
			kLobShadowHeightScale / (height + kLobShadowHeightScale),
			0.45f, 1.0f);
	}
	else if (IsCobCannonMotion()) {
		typeScale = GetCobCannonShadowScale();
	}
	const float shadowWidth = kPeaShadowWidth * typeScale
		* (IsCobCannonMotion() ? kCobShadowWidthMultiplier : 1.0f);
	const float shadowHeight = kPeaShadowHeight * typeScale;

	const Texture* shadowTexture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_PLANTSHADOW);
	if (shadowTexture && shadowTexture->width > 0 && shadowTexture->height > 0) {
		shadow->SetScale(Vector(
			shadowWidth / static_cast<float>(shadowTexture->width),
			shadowHeight / static_cast<float>(shadowTexture->height)));
	}

	// 主人校对：Y 与同一行豌豆射手的默认阴影中心一致，即格子中心 + 28。
	// X 偏移沿用 C#：Pea +3，Snowpea -1，Fireball/Spike 为 0；西瓜按主人可见校对右移 6px。
	const float shadowLeftOffset = IsCobCannonMotion()
		? kCobShadowSourceOffsetX
			- (mTexture ? static_cast<float>(mTexture->width) * 0.5f : 0.0f)
		: IsMelonBullet(mBulletType)
		? kMelonShadowOffsetX
		: mBulletType == BulletType::BULLET_SNOWPEA
			? -1.0f
			: (mBulletType == BulletType::BULLET_STAR
				? 7.0f
				: ((mBulletType == BulletType::BULLET_FIREBALL
					|| mBulletType == BulletType::BULLET_TOXICFIREBALL
					|| mBulletType == BulletType::BULLET_SPIKE
					|| IsClassicLobbedBullet(mBulletType)) ? 0.0f : 3.0f));
	const float shadowOffsetY = GetTerrainShadowY(position) - position.y;
	shadow->SetOffset(Vector(
		shadowLeftOffset + shadowWidth * 0.5f,
		shadowOffsetY));
}

float Bullet::GetTerrainShadowY(const Vector& position) const
{
	const float rowCenterY = mBoard
		? mBoard->GetRowCenterYAtX(mRow, position.x)
		: CELL_INITALIZE_POS_Y + static_cast<float>(mRow) * CELL_COLLIDER_SIZE_Y
			+ CELL_COLLIDER_SIZE_Y * 0.5f;
	return rowCenterY + 28.0f
		+ (mBulletType == BulletType::BULLET_STAR ? kStarShadowExtraOffsetY : 0.0f);
}

bool Bullet::HitsRoofTerrain(const Vector& position) const
{
	if (!mBoard || !mBoard->IsRoofBackground() || mRow < 0 || mRow >= mBoard->mRows) {
		return false;
	}

	float minimumClearance = 0.0f;
	switch (mBulletType) {
	case BulletType::BULLET_PEA:
	case BulletType::BULLET_SNOWPEA:
	case BulletType::BULLET_FIREBALL:
	case BulletType::BULLET_SPIKE:
	case BulletType::BULLET_TOXICPEA:
	case BulletType::BULLET_TOXICFIREBALL:
	case BulletType::BULLET_AURORA_PEA:
		minimumClearance = 28.0f;
		break;
	case BulletType::BULLET_PUFF:
		minimumClearance = 0.0f;
		break;
	case BulletType::BULLET_STAR:
		minimumClearance = 23.0f;
		break;
	default:
		// 卷心菜、玉米、篮球等拥有独立抛射高度，不能套用平射弹的屋顶阈值。
		return false;
	}
	return GetTerrainShadowY(position) - position.y < minimumClearance;
}

void Bullet::HitRoofTerrain()
{
	if (mHasHit) return;
	mHasHit = true;
	const Vector position = GetPosition();
	if (mBulletType == BulletType::BULLET_AURORA_PEA) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CERAMIC, 0.3f);
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("AuroraPeaBreak", position);
		}
	}
	else if (mBulletType == BulletType::BULLET_FIREBALL
		|| mBulletType == BulletType::BULLET_TOXICFIREBALL) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_IGNITE, 0.35f);
		const float impactOffsetX = mVelocityX < 0.0f
			? -kFireballImpactOffsetX : kFireballImpactOffsetX;
		GameObjectManager::GetInstance().CreateGameObject<FireballImpact>(
			LAYER_EFFECTS_WORLD, mBoard,
			position + Vector(impactOffsetX, kFireballImpactOffsetY),
			IsToxicFireball());
	}
	else if (g_particleSystem) {
		const char* effectName = nullptr;
		switch (mBulletType) {
		case BulletType::BULLET_SNOWPEA: effectName = "SnowPeaBulletHit"; break;
		case BulletType::BULLET_PUFF: effectName = "PuffShroomHit"; break;
		case BulletType::BULLET_TOXICPEA: effectName = "ToxicPeaBulletHit"; break;
		case BulletType::BULLET_PEA: effectName = "PeaBulletHit"; break;
		case BulletType::BULLET_STAR: effectName = "StarSplat"; break;
		case BulletType::BULLET_CABBAGE: effectName = "CabbageSplat"; break;
		case BulletType::BULLET_BUTTER: effectName = "ButterSplat"; break;
		default: break;
		}
		if (effectName) g_particleSystem->EmitEffect(effectName, position);
	}
	Die();
}

void Bullet::UpdateStarRow(const Vector& position)
{
	if (!mBoard || mBoard->mRows <= 0 || mVelocityY == 0.0f) return;

	// C# PixelToGridYKeepOnBoard 以弹丸左上点所在格为准；由当前首行中心和行高重建同一边界。
	const float rowHeight = mBoard->GetCellHeight();
	if (rowHeight <= 0.0f) return;
	const float boardTop =
		mBoard->GetRowCenterYAtX(0, position.x) - rowHeight * 0.5f;
	const int row = static_cast<int>(std::floor((position.y - boardTop) / rowHeight));
	mRow = std::clamp(row, 0, mBoard->mRows - 1);
}

void Bullet::EnableThreepeaterMotion(int sourceRow)
{
	if (sourceRow == mRow) return;
	mThreepeaterMotion = true;
	// 原版 300px/s 的衰减总位移约等于一格 100px；泳池行高为 85px，必须同比缩放，
	// 否则相邻行豌豆会越过目标行的视觉与碰撞基线。
	const float rowHeight = mBoard ? mBoard->GetCellHeight() : CELL_COLLIDER_SIZE_Y;
	const float verticalSpeed =
		kThreepeaterVerticalSpeed * rowHeight / CELL_COLLIDER_SIZE_Y;
	mVelocityY = mRow < sourceRow ? -verticalSpeed : verticalSpeed;
	if (GetTransform()) {
		UpdateShadowLayout(GetTransform()->GetPosition());
	}
}

void Bullet::BulletHitZombie(Zombie* zombie)
{
	if (!zombie) return;
	if (mBulletType == BulletType::BULLET_THUNDER_SEED) {
		mBoard->CreateThunderImpact(GetPosition(), mRow, mDamage);
		return;
	}
	if (IsMelonBullet(mBulletType)) {
		HitMelonZombie(zombie);
		return;
	}
	// 经典投掷物从上方砸向后层；普通二类护盾不承伤，加固防具由目标否决绕过。
	const bool requestsShieldBypass = IsClassicLobbedBullet(mBulletType);
	const bool bypassShield = zombie->ShouldProjectileBypassShield(
		mVelocityX, requestsShieldBypass);

	if (mBulletType == BulletType::BULLET_FIREBALL
		|| mBulletType == BulletType::BULLET_TOXICFIREBALL) {
		HitFireballZombie(zombie);
		return;
	}
	const bool canBeChilled =
		mBulletType == BulletType::BULLET_SNOWPEA && zombie->CanBeChilled();
	const bool playChillSound = canBeChilled
		&& zombie->GetCooldownTimer() <= 0.0f
		&& zombie->mHelmType == HelmType::HELMTYPE_NONE
		&& (zombie->mShieldType == ShieldType::SHIELDTYPE_NONE || bypassShield);

	if (mBulletType == BulletType::BULLET_KERNEL) {
		PlayKernelImpactSound();
	}
	else if (mBulletType == BulletType::BULLET_BUTTER) {
		AudioSystem::PlaySound(
			ResourceKeys::Sounds::SOUND_BUTTER, kButterImpactVolume);
		// 原版黄油声替代普通肉身 splat，但仍允许不可绕过防具或一类头盔发出材质声。
		PlayStandardImpactSound(zombie, bypassShield, false);
	}
	else {
		PlayStandardImpactSound(zombie, bypassShield);
	}
	// 腐蚀先由目标当前冰层消费；即使冰层不足也绝不把余量换算为本体伤害。
	if (mBulletType == BulletType::BULLET_SALT_CRYSTAL) {
		zombie->ApplyWinterCorrosion(GetWinterCorrosionDamage());
	}
	// 风力先修正本发子弹的基础伤害，生存词条仍在 Zombie::TakeDamage 中统一且只缩放一次。
	zombie->TakeProjectileDamage(
		zombie->ModifyProjectileDamage(GetWindAdjustedDamage(), mBulletType),
		DamageSource::PLANT, mVelocityX,
		/*penetrateShield=*/false, /*discardShieldOverflow=*/false,
		requestsShieldBypass, mPlantDamageOrigin);
	// 直击死亡、魅惑或对象已回收时不得留下延迟伤害；具体门禁由目标集中维护。
	if (mBulletType == BulletType::BULLET_TOXICPEA) {
		zombie->ApplyToxinStack();
	}
	else if (mBulletType == BulletType::BULLET_BUTTER) {
		zombie->ApplyButter();
	}

	if (mBulletType == BulletType::BULLET_SNOWPEA) {
		if (canBeChilled) {
			if (playChillSound) {
				AudioSystem::PlaySound(
					ResourceKeys::Sounds::SOUND_COOLDOWNZOMBIE, 0.22f);
			}
			zombie->SetCooldown(7.5f, bypassShield);
		}
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("SnowPeaBulletHit", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_PUFF) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("PuffShroomHit", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_TOXICPEA) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("ToxicPeaBulletHit", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_PEA) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("PeaBulletHit", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_STAR) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("StarSplat", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_CABBAGE) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("CabbageSplat", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_MELT_SNOW) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("MeltSnowPultHit", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_SALT_CRYSTAL) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("SaltCrystalHit", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_BUTTER) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("ButterSplat", GetPosition());
		}
	}
}

void Bullet::HitMelonZombie(Zombie* zombie)
{
	if (!zombie || !mBoard) return;

	const bool isWinterMelon = mBulletType == BulletType::BULLET_WINTERMELON;
	PlayMelonImpactSound();
	const int directDamage = GetWindAdjustedDamage();
	std::vector<int> secondaryIDs;
	const float impactLeft = GetPosition().x - kMelonSplashWidth * 0.5f;
	const float impactRight = impactLeft + kMelonSplashWidth;
	const int firstRow = std::max(0, mRow - kMelonSplashRowRadius);
	const int lastRow = std::min(mBoard->mRows - 1, mRow + kMelonSplashRowRadius);

	// 行桶已表达垂直溅射范围；只沿 X 比较原版 60px 命中窗口，
	// 避免把 800x600 左上坐标系的绝对 Y 碰撞框搬进当前 Board 网格。
	for (int row = firstRow; row <= lastRow; ++row) {
		mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* candidate) {
			if (!candidate || candidate == zombie || !candidate->IsActive()
				|| mBoard->MineBlocksSegment(GetPosition(), candidate->GetPosition())
				|| candidate->IsDying() || candidate->IsMindControlled()
				|| !candidate->CanBeTargetedByProjectile(false)) {
				return;
			}
			const ColliderComponent* collider = candidate->GetColliderComponent();
			if (!collider) return;
			const SDL_FRect bounds = collider->GetBoundingBox();
			if (bounds.x <= impactRight && bounds.x + bounds.w >= impactLeft) {
				secondaryIDs.push_back(candidate->mZombieID);
			}
		});
	}

	// 原版 splash 会让二类护盾与后方本体同时承受完整伤害。
	zombie->TakeProjectileDamage(
		directDamage, DamageSource::PLANT, mVelocityX,
		/*penetrateShield=*/true, /*discardShieldOverflow=*/false,
		/*requestsShieldBypass=*/false, mPlantDamageOrigin);
	if (isWinterMelon && zombie->IsActive() && !zombie->IsDying()
		&& zombie->CanBeChilled()) {
		const bool wasSlowed = zombie->GetCooldownTimer() > 0.0f;
		// Winter Melon 的 splash 已把伤害穿透到后层，因此这里显式绕过仍存在的二类盾门禁。
		zombie->SetCooldown(kWinterMelonSlowSeconds, /*bypassShield=*/true);
		if (!wasSlowed && zombie->GetCooldownTimer() > 0.0f) {
			AudioSystem::PlaySound(
				ResourceKeys::Sounds::SOUND_COOLDOWNZOMBIE, 0.22f);
		}
	}

	const int nominalSecondaryDamage = std::max(1, directDamage / kSplashDamageDivisor);
	const int secondaryDamage = secondaryIDs.empty()
		? nominalSecondaryDamage
		: std::max(1, std::min(nominalSecondaryDamage,
			directDamage * kMelonSecondaryBudgetMultiplier
				/ static_cast<int>(secondaryIDs.size())));
	for (const int id : secondaryIDs) {
		Zombie* candidate = mBoard->mEntityRegistry.GetZombie(id);
		if (!candidate || !candidate->IsActive() || candidate->IsDying()) continue;
		candidate->TakeProjectileDamage(
			secondaryDamage, DamageSource::PLANT, mVelocityX,
			/*penetrateShield=*/true, /*discardShieldOverflow=*/false,
			/*requestsShieldBypass=*/false, mPlantDamageOrigin);
		if (isWinterMelon && candidate->IsActive() && !candidate->IsDying()
			&& candidate->CanBeChilled()) {
			const bool wasSlowed = candidate->GetCooldownTimer() > 0.0f;
			candidate->SetCooldown(kWinterMelonSlowSeconds, /*bypassShield=*/true);
			if (!wasSlowed && candidate->GetCooldownTimer() > 0.0f) {
				AudioSystem::PlaySound(
					ResourceKeys::Sounds::SOUND_COOLDOWNZOMBIE, 0.22f);
			}
		}
	}

	if (g_particleSystem) {
		g_particleSystem->EmitEffect(
			isWinterMelon ? "WinterMelonSplash" : "MelonSplash", GetPosition());
	}
}

void Bullet::ConfigurePresentation()
{
	mTexture = nullptr;
	mProjectileAnimator.reset();
	mAnimatorAdvancedInParallel = false;
	mScale = 0.9f;

	ResourceManager& resources = ResourceManager::GetInstance();
	switch (mBulletType) {
	case BulletType::BULLET_PEA:
		mTexture = resources.GetTexture(ResourceKeys::Textures::IMAGE_PROJECTILEPEA);
		break;
	case BulletType::BULLET_SNOWPEA:
		mTexture = resources.GetTexture(ResourceKeys::Textures::IMAGE_PROJECTILESNOWPEA);
		break;
	case BulletType::BULLET_TOXICPEA:
		mTexture = resources.GetTexture(ResourceKeys::Textures::IMAGE_PROJECTILETOXICPEA);
		break;
	case BulletType::BULLET_AURORA_PEA:
		if (mPlantDamageOrigin.lineage == PlantType::PLANT_SNOWPEA) {
			mTexture = resources.GetTexture(
				ResourceKeys::Textures::IMAGE_PROJECTILEAURORASNOWPEA);
		}
		else if (mPlantDamageOrigin.lineage == PlantType::PLANT_TOXICPEASHOOTER) {
			mTexture = resources.GetTexture(
				ResourceKeys::Textures::IMAGE_PROJECTILEAURORATOXICPEA);
		}
		else {
			mTexture = resources.GetTexture(
				ResourceKeys::Textures::IMAGE_PROJECTILEAURORAPEA);
		}
		mScale = 1.0f;
		break;
    case BulletType::BULLET_RAIN_BAMBOO:
        mTexture=resources.GetTexture("IMAGE_RAIN_BAMBOO_DART",false);mScale=1;break;
    case BulletType::BULLET_FLOOD_MORTAR:
        mTexture=resources.GetTexture("PARTICLE_WATER_DROPLETS",false);mScale=.45f;break;
	case BulletType::BULLET_PRESSURE:
        mTexture = resources.GetTexture("IMAGE_PRESSURE_PROJECTILE");
        mScale = 1;
        break;
	case BulletType::BULLET_THERMAL_PULSE:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_PROJECTILETHERMALPULSE);
		mScale = 0.92f;
		break;
	case BulletType::BULLET_PUFF:
		mTexture = resources.GetTexture("IMAGE_PUFFSHROOM_PUFF1");
		mScale = 0.68f;
		break;
	case BulletType::BULLET_SPIKE:
		mTexture = resources.GetTexture(ResourceKeys::Textures::IMAGE_PROJECTILECACTUS);
		mScale = 1.0f;
		break;
	case BulletType::BULLET_STAR:
		mTexture = resources.GetTexture(ResourceKeys::Textures::IMAGE_PROJECTILE_STAR);
		mRotationSpeedDegrees = GameRandom::Range(
			kStarSpinSpeedMin, kStarSpinSpeedMax);
		if (GameRandom::Chance()) mRotationSpeedDegrees = -mRotationSpeedDegrees;
		break;
	case BulletType::BULLET_CABBAGE:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_CABBAGEPULT_CABBAGE);
		mScale = 1.0f;
		mRotationDegrees = kCabbageInitialRotation;
		mRotationSpeedDegrees = GameRandom::Range(
			kCabbageSpinSpeedMin, kCabbageSpinSpeedMax);
		break;
	case BulletType::BULLET_MELT_SNOW:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_MELTSNOWPULT_SNOWCLOD);
		mScale = 1.0f;
		mRotationDegrees = kCabbageInitialRotation;
		mRotationSpeedDegrees = GameRandom::Range(
			kCabbageSpinSpeedMin, kCabbageSpinSpeedMax);
		break;
	case BulletType::BULLET_SALT_CRYSTAL:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_MELTSNOWPULT_SALTCRYSTAL);
		mScale = 1.0f;
		mRotationDegrees = kCabbageInitialRotation;
		mRotationSpeedDegrees = GameRandom::Range(
			kCabbageSpinSpeedMin, kCabbageSpinSpeedMax);
		break;
	case BulletType::BULLET_THUNDER_SEED:
		mTexture = resources.GetTexture("IMAGE_THUNDER_SEED", false);
		mScale = 1;
		break;
	case BulletType::BULLET_MELON:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_MELONPULT_MELON);
		mScale = 1.0f;
		mRotationDegrees = kMelonInitialRotation;
		mRotationSpeedDegrees = GameRandom::Range(
			kCabbageSpinSpeedMin, kCabbageSpinSpeedMax);
		break;
	case BulletType::BULLET_WINTERMELON:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_PROJECTILE);
		mScale = 1.0f;
		mRotationDegrees = kMelonInitialRotation;
		mRotationSpeedDegrees = GameRandom::Range(
			kCabbageSpinSpeedMin, kCabbageSpinSpeedMax);
		break;
	case BulletType::BULLET_KERNEL:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_CORNPULT_KERNAL);
		mScale = kKernelDrawScale;
		mRotationDegrees = kKernelInitialRotation;
		mRotationSpeedDegrees = GameRandom::Range(
			kKernelSpinSpeedMin, kKernelSpinSpeedMax);
		break;
	case BulletType::BULLET_BUTTER:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_CORNPULT_BUTTER);
		mScale = kButterDrawScale;
		mRotationDegrees = kCabbageInitialRotation;
		mRotationSpeedDegrees = GameRandom::Range(
			kCabbageSpinSpeedMin, kCabbageSpinSpeedMax);
		break;
	case BulletType::BULLET_BASKETBALL:
		mTexture = resources.GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_CATAPULT_BASKETBALL);
		mScale = kBasketballDrawScale;
		mRotationDegrees = GameRandom::Range(0.0f, 360.0f);
		mRotationSpeedDegrees = GameRandom::Range(
			kBasketballSpinSpeedMin, kBasketballSpinSpeedMax);
		if (GameRandom::Chance()) mRotationSpeedDegrees = -mRotationSpeedDegrees;
		break;
	case BulletType::BULLET_COBBIG:
		mTexture = resources.GetTexture(ResourceKeys::Textures::IMAGE_COBCANNON_COB);
		mScale = kCobCannonDrawScale;
		mRotationDegrees = 90.0f;
		break;
	case BulletType::BULLET_FIREBALL:
	case BulletType::BULLET_TOXICFIREBALL: {
		auto reanim = resources.GetReanimation(
			resources.AnimationTypeToString(AnimationType::ANIM_FIREPEA));
		if (!reanim) {
			LOG_ERROR("Bullet") << "无法加载 FirePea.reanim";
			break;
		}
		mProjectileAnimator = std::make_shared<Animator>(reanim);
		mProjectileAnimator->SetFrameRangeToDefault();
		mProjectileAnimator->SetCurrentFrame(0.0f);
		mProjectileAnimator->SetSpeed(GameRandom::Range(50.0f, 80.0f) / 12.0f);
		mProjectileAnimator->SetFlipX(mVelocityX < 0.0f);
		if (IsToxicFireball()) {
			mProjectileAnimator->SetOverlayColor(kToxicFireOverlay.r, kToxicFireOverlay.g,
				kToxicFireOverlay.b, kToxicFireOverlay.a);
			mProjectileAnimator->EnableOverlayEffect(true);
		}
		mProjectileAnimator->Play(PlayState::PLAY_REPEAT);
		break;
	}
	default:
		break;
	}
}

void Bullet::ConfigureLobbedMotion(
	const Vector& target, float durationSeconds, float apexHeight,
	bool targetsIceWall, bool polarGuided)
{
	if (!GetTransform()) return;
	if (mBoard && mBoard->MineBlocksSegment(GetTransform()->GetPosition(), target)) { Die(); return; }
	Vector adjustedTarget = target;
	const int sourceRow = mRow;
	int landingRow = sourceRow;
	const bool landsOnBoard = !mBoard
		|| mBoard->ApplyPolarLobbedWind(sourceRow, landingRow, adjustedTarget,
			polarGuided);
	mTrajectory.kind = TrajectoryKind::LOBBED;
	mTrajectory.polarGuided = polarGuided;
	mTrajectory.polarWindMiss = !landsOnBoard;
	mTrajectory.targetsIceWall = targetsIceWall && landingRow == sourceRow;
	mTrajectory.start = GetTransform()->GetPosition();
	mTrajectory.target = adjustedTarget;
	mTrajectory.elapsed = 0.0f;
	mTrajectory.duration = std::max(0.01f, durationSeconds);
	mTrajectory.apexHeight = std::max(0.0f, apexHeight);
	if (landsOnBoard) mRow = landingRow;
	mVelocityX = (mTrajectory.target.x - mTrajectory.start.x) / mTrajectory.duration;
	mVelocityY = (mTrajectory.target.y - mTrajectory.start.y) / mTrajectory.duration
		- 4.0f * mTrajectory.apexHeight / mTrajectory.duration;
	if (mCollider) mCollider->mEnabled = false;
	UpdateShadowLayout(mTrajectory.start);
}

void Bullet::ConfigureCobCannonMotion(
	const Vector& target, int targetRow, float durationSeconds, bool polarGuided)
{
	if (!GetTransform()) return;
	if (mBoard && mBoard->MineBlocksSegment(GetPosition(), target)) { Die(); return; }
	Vector adjustedTarget = target;
	int adjustedRow = targetRow;
	const bool landsOnBoard = !mBoard
		|| mBoard->ApplyPolarLobbedWind(targetRow, adjustedRow, adjustedTarget,
			polarGuided);
	mTrajectory.kind = TrajectoryKind::COB_CANNON;
	mTrajectory.polarGuided = polarGuided;
	mTrajectory.start = GetTransform()->GetPosition();
	mTrajectory.target = adjustedTarget;
	mTrajectory.elapsed = 0.0f;
	mTrajectory.duration = std::max(0.1f, durationSeconds);
	mTrajectory.targetRow = adjustedRow;
	mTrajectory.targetsIceWall = false;
	mTrajectory.polarWindMiss = !landsOnBoard;
	mRotationDegrees = -90.0f;
	if (mCollider) mCollider->mEnabled = false;
	if (auto* shadow = GetShadow()) shadow->SetEnabled(true);
	UpdateShadowLayout(mTrajectory.start);
}

void Bullet::RestoreCobCannonMotion(const Vector& start, const Vector& target,
	int targetRow, float elapsedSeconds, float durationSeconds,
	bool polarWindMiss, bool polarGuided)
{
	if (!GetTransform()) return;
	mTrajectory.kind = TrajectoryKind::COB_CANNON;
	mTrajectory.start = start;
	mTrajectory.target = target;
	mTrajectory.targetRow = targetRow;
	mTrajectory.targetsIceWall = false;
	mTrajectory.polarWindMiss = polarWindMiss;
	mTrajectory.polarGuided = polarGuided;
	mTrajectory.duration = std::max(0.1f, durationSeconds);
	mRotationDegrees = -90.0f;
	if (mCollider) mCollider->mEnabled = false;
	if (auto* shadow = GetShadow()) shadow->SetEnabled(true);
	mTrajectory.elapsed = std::clamp(
		elapsedSeconds, 0.0f, mTrajectory.duration);
	// 以零增量重建当前位置，不会跨过爆炸边沿。
	UpdateCobCannonMotion(0.0f);
}

bool Bullet::UpdateCobCannonMotion(float deltaTime)
{
	if (!GetTransform() || mTrajectory.duration <= 0.0f) return true;
	mTrajectory.elapsed = std::min(mTrajectory.duration,
		mTrajectory.elapsed + std::max(0.0f, deltaTime));
	const float progress = std::clamp(
		mTrajectory.elapsed / mTrajectory.duration, 0.0f, 1.0f);
	mRotationDegrees = progress <= kCobTransferEndProgress ? -90.0f : 90.0f;
	Vector position = mTrajectory.start;
	if (progress <= kCobRiseEndProgress) {
		const float t = progress / kCobRiseEndProgress;
		position.y = mTrajectory.start.y
			+ (kCobSkyY - mTrajectory.start.y) * t;
	}
	else if (progress <= kCobTransferEndProgress) {
		const float t = (progress - kCobRiseEndProgress)
			/ (kCobTransferEndProgress - kCobRiseEndProgress);
		position.x = mTrajectory.start.x
			+ (mTrajectory.target.x - mTrajectory.start.x) * t;
		position.y = kCobSkyY;
	}
	else {
		const float t = (progress - kCobTransferEndProgress)
			/ (1.0f - kCobTransferEndProgress);
		position.x = mTrajectory.target.x;
		position.y = kCobSkyY + (mTrajectory.target.y - kCobSkyY) * t;
	}
	GetTransform()->SetPosition(position);
	if (mTrajectory.elapsed < mTrajectory.duration) return true;
	if (!mHasHit) {
		mHasHit = true;
		if (mBoard && !mTrajectory.polarWindMiss) mBoard->CreateCobCannonExplosion(
			mTrajectory.target, mTrajectory.targetRow, mDamage);
	}
	Die();
	return false;
}

float Bullet::GetCobCannonShadowScale() const
{
	if (!IsCobCannonMotion() || mTrajectory.duration <= 0.0f) return 1.0f;
	const float progress = std::clamp(
		mTrajectory.elapsed / mTrajectory.duration, 0.0f, 1.0f);
	float normalizedHeight = 1.0f;
	if (progress <= kCobRiseEndProgress) {
		normalizedHeight = progress / kCobRiseEndProgress;
	}
	else if (progress > kCobTransferEndProgress) {
		normalizedHeight = 1.0f - (progress - kCobTransferEndProgress)
			/ (1.0f - kCobTransferEndProgress);
	}
	// 原版将高度截在 200px，再按 200/(高度+200) 缩放；归一化后即 1/(1+h)。
	return 1.0f / (1.0f + std::clamp(normalizedHeight, 0.0f, 1.0f));
}

void Bullet::RestoreLobbedMotion(const Vector& start, const Vector& target,
	float elapsedSeconds, float durationSeconds, float apexHeight,
	bool targetsIceWall, bool polarWindMiss, bool polarGuided)
{
	mTrajectory.kind = TrajectoryKind::LOBBED;
	mTrajectory.targetsIceWall = targetsIceWall;
	mTrajectory.polarWindMiss = polarWindMiss;
	mTrajectory.polarGuided = polarGuided;
	mTrajectory.start = start;
	mTrajectory.target = target;
	mTrajectory.duration = std::max(0.01f, durationSeconds);
	mTrajectory.elapsed = std::clamp(
		elapsedSeconds, 0.0f, mTrajectory.duration + kLobLandingGrace);
	mTrajectory.apexHeight = std::max(0.0f, apexHeight);
	const float progress = GetLobProgress();
	const Vector position(
		mTrajectory.start.x
			+ (mTrajectory.target.x - mTrajectory.start.x) * progress,
		mTrajectory.start.y
			+ (mTrajectory.target.y - mTrajectory.start.y) * progress
			- GetLobArcHeight());
	if (GetTransform()) GetTransform()->SetPosition(position);
	mVelocityX = (mTrajectory.target.x - mTrajectory.start.x) / mTrajectory.duration;
	mVelocityY = (mTrajectory.target.y - mTrajectory.start.y) / mTrajectory.duration
		- (4.0f * mTrajectory.apexHeight / mTrajectory.duration)
		* (1.0f - 2.0f * progress);
	if (mCollider) {
		mCollider->mEnabled = !mTrajectory.polarWindMiss && progress >= 0.5f
			&& GetLobArcHeight() <= kLobCollisionArcHeight;
	}
	UpdateShadowLayout(position);
}

float Bullet::GetLobProgress() const
{
	if (!IsLobbedMotion() || mTrajectory.duration <= 0.0f) return 0.0f;
	return std::clamp(
		mTrajectory.elapsed / mTrajectory.duration, 0.0f, 1.0f);
}

float Bullet::GetLobArcHeight() const
{
	const float progress = GetLobProgress();
	return 4.0f * mTrajectory.apexHeight * progress * (1.0f - progress);
}

bool Bullet::UpdateLobbedMotion(float deltaTime)
{
	if (!GetTransform() || mTrajectory.duration <= 0.0f) return true;
	mTrajectory.elapsed += deltaTime;
	const float progress = GetLobProgress();
	const float arcHeight = GetLobArcHeight();
	const Vector position(
		mTrajectory.start.x
			+ (mTrajectory.target.x - mTrajectory.start.x) * progress,
		mTrajectory.start.y
			+ (mTrajectory.target.y - mTrajectory.start.y) * progress - arcHeight);
	GetTransform()->SetPosition(position);
	mVelocityX = (mTrajectory.target.x - mTrajectory.start.x) / mTrajectory.duration;
	mVelocityY = (mTrajectory.target.y - mTrajectory.start.y) / mTrajectory.duration
		- (4.0f * mTrajectory.apexHeight / mTrajectory.duration)
		* (1.0f - 2.0f * progress);
	if (mCollider) {
		// 只在下降末段打开碰撞，飞越前排目标时不会在高空误触。
		mCollider->mEnabled = !mTrajectory.polarWindMiss && progress >= 0.5f
			&& arcHeight <= kLobCollisionArcHeight;
	}
	if (mTrajectory.elapsed >= mTrajectory.duration + kLobLandingGrace) {
		HitLobbedGround();
		return false;
	}
	return true;
}

void Bullet::HitLobbedGround()
{
	if (mHasHit) return;
	mHasHit = true;
	if (IceWall* wall = GetTargetedIceWall()) {
		wall->TakeProjectileDamage(GetWindAdjustedDamage(), false);
		if (mBulletType == BulletType::BULLET_SALT_CRYSTAL && wall->IsActive()) {
			wall->ApplyWinterCorrosion(GetWinterCorrosionDamage());
		}
		PlayLobbedImpactFeedback();
		Die();
		return;
	}
	if (mBulletType == BulletType::BULLET_THUNDER_SEED && !mTrajectory.polarWindMiss)
		mBoard->CreateThunderImpact(GetPosition(), mRow, mDamage);
	else PlayLobbedImpactFeedback();
	Die();
}

void Bullet::PlayLobbedImpactFeedback()
{
	if (mBulletType == BulletType::BULLET_KERNEL) {
		PlayKernelImpactSound();
	}
	else if (IsMelonBullet(mBulletType)) {
		PlayMelonImpactSound();
		if (g_particleSystem) {
			g_particleSystem->EmitEffect(
				mBulletType == BulletType::BULLET_WINTERMELON
					? "WinterMelonSplash" : "MelonSplash",
				GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_BUTTER) {
		AudioSystem::PlaySound(
			ResourceKeys::Sounds::SOUND_BUTTER, kButterImpactVolume);
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("ButterSplat", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_BASKETBALL) {
		AudioSystem::PlaySound(
			ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1, 0.2f);
	}
	else if (mBulletType == BulletType::BULLET_MELT_SNOW) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("MeltSnowPultHit", GetPosition());
		}
	}
	else if (mBulletType == BulletType::BULLET_SALT_CRYSTAL) {
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("SaltCrystalHit", GetPosition());
		}
	}
	else if (g_particleSystem) {
		g_particleSystem->EmitEffect("CabbageSplat", GetPosition());
	}
}

bool Bullet::HitIceWallIfNeeded(float fromX, float toX)
{
	if (!mBoard || !IsActive() || mBulletType == BulletType::BULLET_BASKETBALL) {
		return false;
	}
	IceWall* wall = mBoard->GetIceWallInRow(mRow);
	if (!wall || !wall->IntersectsHorizontalSegment(fromX, toX)) return false;
	const bool fireDamage = mBulletType == BulletType::BULLET_FIREBALL
		|| mBulletType == BulletType::BULLET_TOXICFIREBALL;
	wall->TakeProjectileDamage(GetWindAdjustedDamage(), fireDamage);
	if (mBulletType == BulletType::BULLET_AURORA_PEA) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CERAMIC, 0.3f);
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("AuroraPeaBreak", GetPosition());
		}
	}
	mHasHit = true;
	if (mCollider) mCollider->mEnabled = false;
	Die();
	return true;
}

IceWall* Bullet::GetTargetedIceWall() const
{
	if (!mBoard || !TargetsIceWall()) return nullptr;
	IceWall* wall = mBoard->GetIceWallInRow(mRow);
	if (!wall) return nullptr;
	// 墙在 1.2 秒飞行中会缓慢左移；半墙宽外再留移动余量，仍不会误接远处僵尸落点。
	return std::fabs(mTrajectory.target.x - wall->GetCenterX())
		<= IceWall::kBlockHalfWidth + 24.0f ? wall : nullptr;
}

void Bullet::ConfigureCollisionTarget()
{
	if (!mCollider) return;
	mCollider->isTrigger = true;
	mCollider->layerMask = CollisionLayer::BULLET;
	mCollider->SetTriggerStayCallback(nullptr);
	if (mPoolType == BulletType::BULLET_BASKETBALL) {
		mCollider->collisionMask = CollisionLayer::PLANT;
		mCollider->SetTriggerEnterCallback([this](ColliderComponent* other) {
			HandlePlantContact(other);
		});
		mCollider->SetTriggerStayCallback([this](ColliderComponent* other) {
			HandlePlantContact(other);
		});
		return;
	}

	mCollider->collisionMask = CollisionLayer::ZOMBIE;
	mCollider->SetTriggerEnterCallback([this](ColliderComponent* other) {
		HandleZombieContact(other);
	});
	if (mPoolType == BulletType::BULLET_SPIKE) {
		mCollider->SetTriggerStayCallback([this](ColliderComponent* other) {
			HandleZombieContact(other);
		});
	}
}

void Bullet::HandlePlantContact(ColliderComponent* other)
{
	if (mHasHit || !IsActive() || !mBoard || !other
		|| mBulletType != BulletType::BULLET_BASKETBALL) {
		return;
	}
	auto* object = other->GetGameObject();
	if (!object || object->GetObjectType() != ObjectType::OBJECT_PLANT) return;
	auto* collidedPlant = dynamic_cast<Plant*>(object);
	if (!collidedPlant || collidedPlant->mRow != mRow) return;

	Plant* target = mBoard->GetCatapultTargetPlantAt(
		collidedPlant->mRow, collidedPlant->mColumn);
	if (!target) return;

	if (Plant* protector = mBoard->FindAirborneThreatProtector(
		target->mRow, target->mColumn)) {
		const AirborneDefenseState defense = protector->ActivateAirborneDefense();
		if (defense == AirborneDefenseState::ACTIVATING) {
			// 原版给伞叶 0.05 秒展开；Stay 会在篮球仍与目标重叠时继续完成正式反弹。
			return;
		}
		if (defense == AirborneDefenseState::REFLECTING) {
			mHasHit = true;
			AudioSystem::PlaySound(
				ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1, 0.2f);
			if (g_particleSystem) {
				g_particleSystem->EmitEffect("UmbrellaReflect", GetPosition());
			}
			Die();
			return;
		}
	}

	mHasHit = true;
	target->TakeDamage(mDamage, DamageSource::ZOMBIE);
	AudioSystem::PlaySound(
		ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1, 0.2f);
	Die();
}

void Bullet::HandleZombieContact(ColliderComponent* other)
{
    if(mBulletType==BulletType::BULLET_RAIN_BAMBOO || mBulletType==BulletType::BULLET_FLOOD_MORTAR) return;
	if (!IsActive() || !other) return;
	if (TargetsIceWall()) return;
	auto* otherGameObject = other->GetGameObject();
	if (!otherGameObject || otherGameObject->GetObjectType() != ObjectType::OBJECT_ZOMBIE) {
		return;
	}

	auto* zombie = dynamic_cast<Zombie*>(otherGameObject);
	if (!zombie || (zombie->mRow != mRow && (!mBoard || !mBoard->IsMineBackground()))
		|| !zombie->CanBeTargetedByProjectile(mTargetsFlying)) return;
	const bool bypassShield = zombie->ShouldProjectileBypassShield(mVelocityX);

	if (mBulletType == BulletType::BULLET_AURORA_PEA) {
		HandleAuroraContacts();
		return;
	}

	if (mBulletType != BulletType::BULLET_SPIKE) {
		if (mHasHit) return;
		mHasHit = true;
		BulletHitZombie(zombie);
		Die();
		return;
	}

	if (!mSpikeState) mSpikeState = std::make_unique<SpikeState>();
	auto& spike = *mSpikeState;
	auto idIt = std::find(spike.zombieIDs.begin(),
		spike.zombieIDs.begin() + spike.count, zombie->mZombieID);
	const bool isNewZombie = idIt == spike.zombieIDs.begin() + spike.count;
	std::size_t targetIndex = static_cast<std::size_t>(
		std::distance(spike.zombieIDs.begin(), idIt));
	if (isNewZombie) {
		if (spike.count >= kSpikePierceLimit) {
			mHasHit = true;
			Die();
			return;
		}
		targetIndex = spike.count++;
		spike.zombieIDs[targetIndex] = zombie->mZombieID;
		spike.damageRemainders[targetIndex] = 0.0f;
		// 帧伤本身不能每帧重播撞击声；每只不同目标只在首次接触时反馈一次。
		PlayStandardImpactSound(zombie, bypassShield);
	}

	const bool reachedPierceLimit =
		isNewZombie && spike.count >= kSpikePierceLimit;
	const float frameDamage = zombie->ModifySpikeFrameDamage(
		static_cast<float>(mDamage), bypassShield);

	if (reachedPierceLimit) {
		// 达到穿透上限的目标没有后续 Stay 可消费额度，因此固定承受 1x 的完整帧伤后再回收。
		const int finalTargetDamage = std::max(1,
			static_cast<int>(std::lround(frameDamage)));
		for (int i = 0; i < finalTargetDamage && zombie->IsActive(); ++i) {
			zombie->TakeProjectileDamage(1, DamageSource::PLANT, mVelocityX,
				false, false, false, mPlantDamageOrigin);
		}
		mHasHit = true;
		Die();
		return;
	}

	// 固定逻辑步的回调次数不随倍速改变；用缩放逻辑时间累计额度，保证同样游戏时长
	// 在 0.5x/1x/2x 下按当前基础帧伤等比例推进，整数承伤总量保持一致。
	float& damageRemainder = spike.damageRemainders[targetIndex];
	damageRemainder += static_cast<float>(frameDamage)
		* DeltaTime::GetDeltaTime() / DeltaTime::GetFixedStep();
	const int damageToApply = static_cast<int>(std::floor(damageRemainder + 1e-6f));
	if (damageToApply > 0) {
		damageRemainder -= static_cast<float>(damageToApply);
		// 每个整数额度仍是一次独立的 1 点帧伤；合并为 TakeDamage(N) 会让免伤次数、
		// 逐击取整和其他“每次受击”语义在 2x 下少触发。
		for (int i = 0; i < damageToApply && zombie->IsActive(); ++i) {
			zombie->TakeProjectileDamage(1, DamageSource::PLANT, mVelocityX,
				false, false, false, mPlantDamageOrigin);
		}
	}
}

void Bullet::HandleAuroraContacts()
{
	if (!mBoard || !IsActive() || mBulletType != BulletType::BULLET_AURORA_PEA) return;
	const ColliderComponent* bulletCollider = GetColliderComponent();
	if (!bulletCollider) return;
	const SDL_FRect bulletBounds = bulletCollider->GetBoundingBox();

	std::vector<Zombie*> targets;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* candidate) {
		if (!candidate || !candidate->IsActive() || candidate->IsDying()
			|| !candidate->CanBeTargetedByProjectile(mTargetsFlying)) {
			return;
		}
		const ColliderComponent* collider = candidate->GetColliderComponent();
		if (!collider) return;
		const SDL_FRect bounds = collider->GetBoundingBox();
		const bool overlaps = bulletBounds.x < bounds.x + bounds.w
			&& bulletBounds.x + bulletBounds.w > bounds.x
			&& bulletBounds.y < bounds.y + bounds.h
			&& bulletBounds.y + bulletBounds.h > bounds.y;
		if (overlaps) targets.push_back(candidate);
	});

	const bool movingLeft = mVelocityX < 0.0f;
	std::sort(targets.begin(), targets.end(), [movingLeft](const Zombie* lhs,
		const Zombie* rhs) {
		const float lhsX = lhs->GetPosition().x;
		const float rhsX = rhs->GetPosition().x;
		if (lhsX != rhsX) return movingLeft ? lhsX > rhsX : lhsX < rhsX;
		return lhs->mZombieID < rhs->mZombieID;
	});

	for (Zombie* target : targets) {
		if (!IsActive()) break;
		HitAuroraZombie(target);
	}
}

void Bullet::HitAuroraZombie(Zombie* zombie)
{
	if (!zombie || !zombie->IsActive()) return;
	if (!mAuroraState) mAuroraState = std::make_unique<AuroraState>();
	auto& aurora = *mAuroraState;
	if (std::find(aurora.zombieIDs.begin(),
		aurora.zombieIDs.begin() + aurora.count, zombie->mZombieID)
		!= aurora.zombieIDs.begin() + aurora.count) {
		return;
	}
	if (aurora.count >= kAuroraPierceLimit) return;

	aurora.zombieIDs[aurora.count++] = zombie->mZombieID;
	if (!aurora.playedHitSound) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SNOW_PEA_SPARKLES, 0.22f);
		aurora.playedHitSound = true;
	}

	const bool canBeChilled = mPlantDamageOrigin.lineage == PlantType::PLANT_SNOWPEA
		&& zombie->CanBeChilled();
	zombie->TakeProjectileDamage(
		zombie->ModifyProjectileDamage(GetWindAdjustedDamage(), mBulletType),
		DamageSource::PLANT, mVelocityX,
		/*penetrateShield=*/false, /*discardShieldOverflow=*/false,
		/*requestsShieldBypass=*/false, mPlantDamageOrigin);
	// 免疫只否决数值伤害；仍存活的实际目标继续获得原射手附带状态。
	if (zombie->IsActive() && !zombie->IsDying()) {
		if (canBeChilled) zombie->SetCooldown(7.5f, false);
		if (mPlantDamageOrigin.lineage == PlantType::PLANT_TOXICPEASHOOTER) {
			zombie->ApplyToxinStack();
		}
	}

	const bool finalHit = aurora.count >= kAuroraPierceLimit;
	if (g_particleSystem) {
		g_particleSystem->EmitEffect(
			finalHit ? "AuroraPeaBreak" : "AuroraPeaPass", GetPosition());
	}
	if (!finalHit) return;

	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CERAMIC, 0.3f);
	mHasHit = true;
	Die();
}

void Bullet::SetVelocityX(float x)
{
	mVelocityX = x;
	if (mProjectileAnimator) {
		mProjectileAnimator->SetFlipX(mVelocityX < 0.0f);
	}
}

void Bullet::ConvertToFireball(int torchwoodColumn)
{
	if ((mBulletType != BulletType::BULLET_PEA
		&& mBulletType != BulletType::BULLET_TOXICPEA
		&& mBulletType != BulletType::BULLET_AURORA_PEA)
		|| mHitTorchwoodColumn == torchwoodColumn) {
		return;
	}

	mBulletType = (mBulletType == BulletType::BULLET_TOXICPEA
		|| mPlantDamageOrigin.lineage == PlantType::PLANT_TOXICPEASHOOTER)
		? BulletType::BULLET_TOXICFIREBALL
		: BulletType::BULLET_FIREBALL;
	mDamage = kFireballDamage;
	mHitTorchwoodColumn = torchwoodColumn;
	ConfigurePresentation();
	if (GetTransform()) {
		UpdateShadowLayout(GetTransform()->GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FIREPEA, 0.35f);
}

void Bullet::ConvertToAuroraPea(int auroraTorchwoodColumn)
{
	if (mHitAuroraTorchwoodColumn == auroraTorchwoodColumn
		|| mPlantDamageOrigin.kind != PlantDamageOriginKind::PLANT_LINEAGE) {
		return;
	}

	const PlantType lineage = mPlantDamageOrigin.lineage;
	const bool eligibleLineage = lineage == PlantType::PLANT_PEASHOOTER
		|| lineage == PlantType::PLANT_SNOWPEA
		|| lineage == PlantType::PLANT_REPEATER
		|| lineage == PlantType::PLANT_THREEPEATER
		|| lineage == PlantType::PLANT_SPLITPEA
		|| lineage == PlantType::PLANT_TOXICPEASHOOTER;
	const bool eligibleCurrentType = mBulletType == BulletType::BULLET_PEA
		|| mBulletType == BulletType::BULLET_SNOWPEA
		|| mBulletType == BulletType::BULLET_TOXICPEA
		|| mBulletType == BulletType::BULLET_FIREBALL
		|| mBulletType == BulletType::BULLET_TOXICFIREBALL
		|| mBulletType == BulletType::BULLET_AURORA_PEA;
	if (!eligibleLineage || !eligibleCurrentType) return;

	mBulletType = BulletType::BULLET_AURORA_PEA;
	mDamage = kAuroraPeaDamage;
	mHitAuroraTorchwoodColumn = auroraTorchwoodColumn;
	if (!mAuroraState) mAuroraState = std::make_unique<AuroraState>();
	ConfigurePresentation();
	if (GetTransform()) UpdateShadowLayout(GetTransform()->GetPosition());
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SNOW_PEA_SPARKLES, 0.12f);
}

void Bullet::ConvertSnowPeaToPea(int torchwoodColumn)
{
	if (mBulletType != BulletType::BULLET_SNOWPEA
		|| mHitTorchwoodColumn == torchwoodColumn) {
		return;
	}

	mBulletType = BulletType::BULLET_PEA;
	mDamage = kPeaDamage;
	mHitTorchwoodColumn = torchwoodColumn;
	ConfigurePresentation();
	if (GetTransform()) {
		UpdateShadowLayout(GetTransform()->GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT, 0.2f);
}

void Bullet::PlayStandardImpactSound(
	const Zombie* zombie, bool bypassShield, bool includeBodySplat) const
{
	if (!zombie) return;

	if (zombie->mHelmType == HelmType::HELMTYPE_TRAFFIC_CONE
		|| zombie->mHelmType == HelmType::HELMTYPE_FOOTBALL) {
		AudioSystem::PlaySound(
			GameRandom::Range(1, 2) == 1
				? ResourceKeys::Sounds::SOUND_HITCONE
				: ResourceKeys::Sounds::SOUND_HITCONE2,
			0.2f);
		return;
	}

	if (zombie->mHelmType == HelmType::HELMTYPE_BUCKET
		|| (!bypassShield && zombie->mShieldType == ShieldType::SHIELDTYPE_DOOR)
		|| (!bypassShield && zombie->mShieldType == ShieldType::SHIELDTYPE_LADDER)
		|| zombie->mZombieType == ZombieType::ZOMBIE_ZAMBONI
		|| zombie->mZombieType == ZombieType::ZOMBIE_GILDED_ZAMBONI) {
		AudioSystem::PlaySound(
			GameRandom::Range(1, 2) == 1
				? ResourceKeys::Sounds::SOUND_IRONHIT
				: ResourceKeys::Sounds::SOUND_IRONHIT2,
			0.2f);
		return;
	}
	if (!includeBodySplat) return;

	switch (GameRandom::Range(1, 3)) {
	case 1:
		AudioSystem::PlaySound(
			ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1, 0.2f);
		break;
	case 2:
		AudioSystem::PlaySound(
			ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY2, 0.2f);
		break;
	default:
		AudioSystem::PlaySound(
			ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY3, 0.2f);
		break;
	}
}

void Bullet::RestoreSavedPresentationState(BulletType currentType,
	int hitTorchwoodColumn, int hitAuroraTorchwoodColumn)
{
	mBulletType = currentType;
	mHitTorchwoodColumn = hitTorchwoodColumn;
	mHitAuroraTorchwoodColumn = hitAuroraTorchwoodColumn;
	ConfigurePresentation();
	if (GetTransform()) {
		UpdateShadowLayout(GetTransform()->GetPosition());
	}
}

int Bullet::GetAuroraHitCount() const
{
	return mAuroraState ? static_cast<int>(mAuroraState->count) : 0;
}

std::vector<int> Bullet::GetAuroraHitZombieIDs() const
{
	if (!mAuroraState) return {};
	return std::vector<int>(mAuroraState->zombieIDs.begin(),
		mAuroraState->zombieIDs.begin() + mAuroraState->count);
}

bool Bullet::HasPlayedAuroraHitSound() const
{
	return mAuroraState && mAuroraState->playedHitSound;
}

void Bullet::RestoreAuroraState(
	const std::vector<int>& zombieIDs, bool playedHitSound)
{
	mAuroraState.reset();
	if (mBulletType != BulletType::BULLET_AURORA_PEA && zombieIDs.empty()) return;
	mAuroraState = std::make_unique<AuroraState>();
	mAuroraState->playedHitSound = playedHitSound;
	for (const int id : zombieIDs) {
		if (std::find(mAuroraState->zombieIDs.begin(),
			mAuroraState->zombieIDs.begin() + mAuroraState->count, id)
			!= mAuroraState->zombieIDs.begin() + mAuroraState->count) {
			continue;
		}
		mAuroraState->zombieIDs[mAuroraState->count++] = id;
		if (mAuroraState->count + 1 >= kAuroraPierceLimit) break;
	}
}

void Bullet::RestorePiercedZombieState(const std::vector<int>& zombieIDs,
	const std::vector<float>& damageRemainders)
{
	if (mSpikeState) mSpikeState->count = 0;
	if (mBulletType != BulletType::BULLET_SPIKE) return;

	for (std::size_t i = 0; i < zombieIDs.size(); ++i) {
		const int id = zombieIDs[i];
		if (!mSpikeState) mSpikeState = std::make_unique<SpikeState>();
		auto& spike = *mSpikeState;
		if (std::find(spike.zombieIDs.begin(),
			spike.zombieIDs.begin() + spike.count, id)
			!= spike.zombieIDs.begin() + spike.count) {
			continue;
		}
		const std::size_t targetIndex = spike.count++;
		spike.zombieIDs[targetIndex] = id;
		const float remainder = i < damageRemainders.size()
			? damageRemainders[i] : 0.0f;
		spike.damageRemainders[targetIndex] =
			std::clamp(remainder, 0.0f, 0.999999f);
		// 活跃子弹一旦达到穿透上限就已回收，因此合法存档至多包含上限减一只。
		if (spike.count + 1 >= kSpikePierceLimit) break;
	}
}

void Bullet::HitFireballZombie(Zombie* zombie)
{
	const int directDamage = GetWindAdjustedDamage();
	const bool bypassShield = zombie->ShouldProjectileBypassShield(mVelocityX);
	const bool toxicFireball = IsToxicFireball();
	if (zombie->IsFireResistant() && !bypassShield) {
		PlayStandardImpactSound(zombie, false);
		zombie->TakeProjectileDamage(directDamage, DamageSource::PLANT, mVelocityX,
			false, false, false, mPlantDamageOrigin);
		if (toxicFireball) zombie->ApplyToxinStack();
		return;
	}

	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_IGNITE, 0.35f);
	if (!toxicFireball)
	{
		zombie->RemoveColdEffects();
	}

	std::vector<Zombie*> secondaryTargets;
	const float impactX = GetPosition().x;
	const float splashWidth = toxicFireball
		? kToxicFireballSplashWidth : kFireballSplashWidth;
	// 静止测试弹与普通向右火豆保持历史正向口径；反向火豆把同一宽度镜像到命中点左侧。
	const float splashLeft = mVelocityX < 0.0f
		? impactX - splashWidth : impactX;
	const float splashRight = mVelocityX < 0.0f
		? impactX : impactX + splashWidth;
	if (mBoard) {
		mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* candidate) {
			if (!candidate || candidate == zombie || !candidate->IsActive()
				|| mBoard->MineBlocksSegment(GetPosition(), candidate->GetPosition())
				|| candidate->IsDying() || candidate->IsMindControlled()
				|| candidate->IsFireResistant()) {
				return;
			}
			const ColliderComponent* collider = candidate->GetColliderComponent();
			if (!collider) return;
			const SDL_FRect bounds = collider->GetBoundingBox();
			if (bounds.x > splashRight || bounds.x + bounds.w < splashLeft) return;
			secondaryTargets.push_back(candidate);
			});
	}

	int splashDamage = std::max(1, directDamage / kSplashDamageDivisor);
	const int splashTotal = splashDamage * static_cast<int>(secondaryTargets.size());
	if (splashTotal > directDamage) {
		splashDamage = std::max(1,
			directDamage * directDamage
			/ (splashTotal * kSplashDamageDivisor));
	}

	// 原版 splash damage flag 会让二类护盾照常受损，同时把全额伤害继续传给本体。
	zombie->TakeProjectileDamage(
		directDamage, DamageSource::PLANT, mVelocityX, /*penetrateShield=*/true,
		/*discardShieldOverflow=*/false, /*requestsShieldBypass=*/false,
		mPlantDamageOrigin);
	// 紫焰豆以 30px 小范围限制铺毒规模；直击与实际受溅射的目标各叠一层。
	if (toxicFireball) zombie->ApplyToxinStack();
	for (Zombie* target : secondaryTargets) {
		if (target->IsActive() && !target->IsDying()) {
			target->TakeDamage(splashDamage, DamageSource::PLANT, true,
				false, false, mPlantDamageOrigin);
			if (toxicFireball) target->ApplyToxinStack();
		}
	}

	const float impactOffsetX = mVelocityX < 0.0f
		? -kFireballImpactOffsetX : kFireballImpactOffsetX;
	GameObjectManager::GetInstance().CreateGameObject<FireballImpact>(
		LAYER_EFFECTS_WORLD,
		mBoard,
		GetPosition() + Vector(impactOffsetX, kFireballImpactOffsetY),
		toxicFireball);
}

bool Bullet::IsTyphoonWindAffected() const
{
	return WindResponseForBullet(mBulletType) == BulletWindResponse::LIGHT_PROJECTILE;
}

float Bullet::GetWindAdjustedVelocityX() const
{
	if (!IsTyphoonWindAffected() || !mBoard || mVelocityX == 0.0f) return mVelocityX;
	return mVelocityX * mBoard->GetPlantBulletWindSpeedMultiplier(mVelocityX > 0.0f);
}

int Bullet::GetWindAdjustedDamage() const
{
	if (!IsTyphoonWindAffected() || !mBoard || mDamage <= 0 || mVelocityX == 0.0f) return mDamage;
	const float multiplier = mBoard->GetPlantBulletWindDamageMultiplier(mVelocityX > 0.0f);
	return std::max(1, static_cast<int>(std::lround(static_cast<float>(mDamage) * multiplier)));
}

/** 盐晶腐蚀与直击伤害分离，供目标冰层和 AutoTest 共用同一个类型契约。 */
int Bullet::GetWinterCorrosionDamage() const
{
	return mBulletType == BulletType::BULLET_SALT_CRYSTAL
		? kSaltCrystalCorrosion : 0;
}

void Bullet::UpdatePressureProjectile(float deltaTime) {
    const Vector from=GetPosition(), to=from+Vector(mVelocityX*deltaTime,0);
    GetTransform()->SetPosition(to);
    if(mCollider) mCollider->mEnabled=false;
    if(!mBoard) return;
    bool hit=false, hitPlant=false;
    const float low=std::min(from.x,to.x), high=std::max(from.x,to.x);
    Vector impact=to;
    if(mVelocityX<0) {
        // 与其他敌方直射弹一致：先镜片拦截，再南瓜、本体、支撑；每发只消费一次。
        for(int col=mBoard->mColumns-1;col>=0 && !hit;--col) {
            const Vector center=mBoard->GetCellCenterPosition(mRow,col);
            const float left=center.x-CELL_COLLIDER_SIZE_X*.5f,right=center.x+CELL_COLLIDER_SIZE_X*.5f;
            if(high<left || low>right) continue;
            auto* cell=mBoard->GetCell(mRow,col); if(!cell) continue;
            impact=Vector(std::clamp(right,low,high),to.y);
            auto* normal=mBoard->mEntityRegistry.GetPlant(cell->GetNormalPlantID());
            if(normal && normal->TryInterceptHostileStraightProjectile(mVelocityX,impact)) { hit=true; break; }
            for(int id:{cell->GetPumpkinPlantID(),cell->GetNormalPlantID(),cell->GetUnderPlantID()}) {
                auto* p=mBoard->mEntityRegistry.GetPlant(id);
                if(!p || !p->IsActive() || !p->OccupiesGridSlot() || p->IsSquished() || p->mPlantHealth<=0) continue;
                p->TakeDamage(mDamage,DamageSource::ZOMBIE); hit=hitPlant=true; break;
            }
        }
    } else {
        // 魅惑只改变未来出膛方向；在途弹的阵营由其速度确定，不引用原射手。
        Zombie* target=nullptr; float best=high+1;
        mBoard->mEntityRegistry.ForEachZombieInRow(mRow,[&](Zombie* z) {
            if(!z || !z->IsActive() || z->IsDying() || z->IsMindControlled() || !z->CanBeTargetedByProjectile(false)) return;
            const auto* c=z->GetColliderComponent(); if(!c) return;
            const auto b=c->GetBoundingBox();
            if(high<b.x || low>b.x+b.w) return;
            const float x=std::max(low,b.x);
            if(x<best || (x==best && target && z->mZombieID<target->mZombieID)) {target=z;best=x;}
        });
        if(target) { target->TakeProjectileDamage(mDamage,DamageSource::ZOMBIE,mVelocityX); impact=Vector(best,to.y);hit=true; }
    }
    if(hit) {
        if(g_particleSystem) g_particleSystem->EmitEffect("PressureHit",impact);
        if(hitPlant) {
            // 植物实际接弹才随机播放肉身 splat；镜片拦截仍保留冰镜自己的碎裂声。
            const int variant=GameRandom::Range(0,2);
            AudioSystem::PlaySound(variant==0 ? ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1
                : variant==1 ? ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY2
                : ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY3,.2f);
        }
        else AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2,.2f);
        Die(); return;
    }
    if(to.x<-40 || to.x>SCENE_WIDTH+40) Die();
}
