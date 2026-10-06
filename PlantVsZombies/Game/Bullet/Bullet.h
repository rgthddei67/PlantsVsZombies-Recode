#pragma once
#ifndef _BULLET_H
#define _BULLET_H

#include <SDL2/SDL.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "../../DeltaTime.h"
#include "../GameObject.h"
#include "../../GameRandom.h"
#include "../../ResourceManager.h"
#include "../EntityRegistry.h"
#include "BulletType.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../Transform.h"
#include "../ColliderComponent.h"
#include "../AudioSystem.h"
#include "../Zombie/Zombie.h"
#include "../../Reanimation/Animator.h"
#include "../PlantDamageOrigin.h"

class Board;
class BulletPool;
class ShadowComponent;

class Bullet final : public GameObject
{
private:
	enum class TrajectoryKind : std::uint8_t {
		LINEAR,
		LOBBED,
		COB_CANNON,
	};

	/** 解析抛射与玉米棒轨迹互斥，共用起终点和时间轴以免普通子弹常驻两套状态。 */
	struct TrajectoryState {
		Vector start = Vector::zero();
		Vector target = Vector::zero();
		float elapsed = 0.0f;
		float duration = 0.0f;
		union {
			float apexHeight;
			int targetRow;
		};
		TrajectoryKind kind = TrajectoryKind::LINEAR;
		bool targetsIceWall = false; // 抛射起手时是否已锁定冰墙；在途存档后继续忽略墙后僵尸
		bool polarWindMiss = false; // 发射时已被垂直风切变吹出边界；在途读档不得重判
		bool polarGuided = false; // 发射边沿已由北极星领域锁定；读档后保持原落点

		TrajectoryState() : apexHeight(0.0f) {}
	};

	struct SpikeState;
	struct AuroraState;

public:
	BulletType mBulletType = BulletType::NUM_BULLETS;
	PlantDamageOrigin mPlantDamageOrigin{}; // 原发射植物谱系；火炬转弹不得改写
	float mScale = 0.9f;
	int mRow = -1;
	int mBulletID = NULL_BULLET_ID;
	bool mFromPool = false;  // 标记是否来自对象池

protected:
	Board* mBoard = nullptr;
	const Texture* mTexture = nullptr;
	float mCheckPositionTimer = 0.0f;
	bool mHasHit = false;	// 是否已经击中过僵尸
	int mDamage = 20;			// 子弹伤害
	float mVelocityX = 290.0f;	// 子弹X轴动量
	float mVelocityY = 0.0f;	// 子弹Y轴动量
	float mRotationDegrees = 0.0f; // 星弹等纹理子弹的当前绘制旋转角，单位：度
	float mRotationSpeedDegrees = 0.0f; // 星弹随机自旋速度，单位：度/游戏秒
	bool mThreepeaterMotion = false; // 三线射手斜向豌豆按原版逐步衰减纵向速度
	bool mTargetsFlying = false; // 高姿态仙人掌尖刺为 true；对象池与存档必须显式复位
	TrajectoryState mTrajectory; // 轨迹类型与互斥参数；对象池复用必须整体重置
	BulletType mPoolType = BulletType::NUM_BULLETS; // 对象池槽位的固定类型；火炬树桩只改变当前表现类型
	int mHitTorchwoodColumn = -1; // 最近处理过本子弹的火炬树桩列，防止同列反复转换
	int mHitAuroraTorchwoodColumn = -1; // 最近处理过本子弹的极光树桩列
	std::unique_ptr<SpikeState> mSpikeState; // 仅尖刺首次接触目标时分配，固定四槽且随池槽复用
	std::unique_ptr<AuroraState> mAuroraState; // 首次变为极光弹时分配，保存四目标历史与单次音效状态
	std::shared_ptr<Animator> mProjectileAnimator;
	bool mAnimatorAdvancedInParallel = false;
	float mThermalEndpointX = 0.0f; // 热脉冲发射时锁定的原落种位置，单位世界 px
	float mThermalEndpointY = 0.0f; // 热脉冲从真实炮口斜向收束到同行目标的终点高度，单位世界 px
	int mThermalOriginalPlantID = NULL_PLANT_ID; // 只有该实体命中走部署拦截承伤入口
	bool mThermalConfigured = false; // 对象池复用时防止未配置热脉冲误用零终点

	// 子弹击中僵尸的效果
	void BulletHitZombie(Zombie* zombie);
	/**
	 * 处理一次子弹与僵尸的碰撞帧；普通弹只消费首次 Enter，尖刺在 Enter/Stay 均结算。
	 */
	void HandleZombieContact(ColliderComponent* other);
	/** 对当前重叠的极光弹目标按飞行方向稳定排序，并依次消费未命中过的目标。 */
	void HandleAuroraContacts();
	/** 结算一只极光弹目标的伤害、来源状态与分段反馈。 */
	void HitAuroraZombie(Zombie* zombie);
	/** 处理投篮车篮球与植物的下降末段碰撞，并按格内投篮层级重定向目标。 */
	void HandlePlantContact(ColliderComponent* other);
	/** 按对象池固定弹型恢复碰撞阵营与回调，避免篮球槽位复用后沿用僵尸掩码。 */
	void ConfigureCollisionTarget();

	// 按 C# Projectile.DrawShadow 的类型尺寸与棋盘行位置刷新阴影布局。
	void UpdateShadowLayout(const Vector& position);
	/** 返回当前弹丸在地形上的阴影基线；屋顶碰撞与阴影绘制必须共用同一采样。 */
	float GetTerrainShadowY(const Vector& position) const;
	/** 按经典弹型离地阈值判断平射弹是否撞上屋顶抬高区域。 */
	bool HitsRoofTerrain(const Vector& position) const;
	/** 播放无目标的地形命中特效并回收弹丸。 */
	void HitRoofTerrain();
	// 按当前可变子弹类型重建纹理或 FirePea.reanim 表现。
	void ConfigurePresentation();
	// 星弹纵向飞行时按当前 Board 网格更新碰撞行；其他子弹保持创建行。
	void UpdateStarRow(const Vector& position);
	/** 播放头盔/护盾材质声，并可抑制无防具时的普通本体 splat。 */
	void PlayStandardImpactSound(
		const Zombie* zombie, bool bypassShield = false, bool includeBodySplat = true) const;
	void HitFireballZombie(Zombie* zombie);
    /** 气弹用同帧线段按行检索，避免高速穿透及外观高度影响植物命中。 */
    void UpdatePressureProjectile(float deltaTime);
    void UpdateRainBamboo(float deltaTime);
    void UpdateFloodMortar(float deltaTime);
    std::vector<int> mBambooHitIDs;
    float mWaterTrailRemaining=0;
    bool mFloodCharmed=false;
	/** 结算西瓜直击、相邻行溅射和穿透二类护盾的原版语义。 */
	void HitMelonZombie(Zombie* zombie);
	/** 推进解析抛物线；返回 false 表示本帧已落空并回收。 */
	bool UpdateLobbedMotion(float deltaTime);
	/** 抛射物到达无目标落点后的粒子与回收入口。 */
	void HitLobbedGround();
	/** 播放当前投射物命中地面或冰墙时的品种专属音画反馈。 */
	void PlayLobbedImpactFeedback();
	/** 平射移动后优先结算同行冰墙；返回 true 表示本弹已被墙消费。 */
	bool HitIceWallIfNeeded(float fromX, float toX);
	/** 返回仍位于锁定落点附近的同行冰墙；原墙消失时返回 nullptr。 */
	class IceWall* GetTargetedIceWall() const;
	/** 推进玉米棒升空/换位/垂降三段轨迹；爆炸并回收时返回 false。 */
	bool UpdateCobCannonMotion(float deltaTime);
	/** 按原版高度公式返回玉米棒飞行阴影的纵向尺寸倍率。 */
	float GetCobCannonShadowScale() const;

public:
	Bullet(Board* board, BulletType bulletType, int row, const Vector& colliderRadius,
		const Vector& position);
	~Bullet() override;

	// 重置子弹状态（用于对象池复用）
	void Reset(Board* board, int row,
		const Vector& colliderRadius, const Vector& position);

	// 设置是否来自对象池
	void SetFromPool(bool fromPool) { mFromPool = fromPool; }
	bool IsFromPool() const { return mFromPool; }
	BulletType GetPoolType() const { return mPoolType; }

	// 子弹消失
	void Die();
    const std::vector<int>& GetBambooHitIDs() const { return mBambooHitIDs; }
    /** 恢复不同目标命中序列；不再结算此前已经提交的命中。 */
    void RestoreBambooHitIDs(const std::vector<int>& ids);
    void SetFloodCharmed(bool charmed) { mFloodCharmed=charmed; }
    bool GetFloodCharmed() const { return mFloodCharmed; }

	void Start() override;
	void Update() override;
	void UpdateParallel(std::vector<DeferredEvent>& outBuf) override;
	void Draw(Graphics* g) override;
	// 由 BulletPool 的全局地面阴影阶段调用，保证阴影绘制在植物层之前。
	void DrawShadow(Graphics* g);
	/** 当前类型是否属于会响应台风的轻型植物子弹。 */
	bool IsTyphoonWindAffected() const;
	/** 返回按当前实时风向派生的水平速度；基础速度及存档值保持不变。 */
	float GetWindAdjustedVelocityX() const;
	/** 返回按当前实时风向派生的命中伤害；随后仍由受击入口叠加生存词条。 */
	int GetWindAdjustedDamage() const;
	/** 返回当前弹型对目标冰制层请求的独立腐蚀值；普通弹丸为 0。 */
	int GetWinterCorrosionDamage() const;

	/** 返回出生/对象池复用共用的基础单发伤害，供只读战斗画像使用。 */
	static int GetBaseDamage(BulletType type);
	int GetBulletDamage() const { return mDamage; }
	void SetBulletDamage(int damage) { this->mDamage = damage; }
	void SetPlantDamageOrigin(PlantDamageOrigin origin) { mPlantDamageOrigin = origin; }
	float GetVelocityX() const { return mVelocityX; }
	void SetVelocityX(float x);
	float GetVelocityY() { return mVelocityY; }
	void SetVelocityY(float y) { this->mVelocityY = y; }
	float GetRotationDegrees() const { return mRotationDegrees; }
	float GetRotationSpeedDegrees() const { return mRotationSpeedDegrees; }
	float GetDrawScale() const { return mScale; }
	void SetRotationDegrees(float degrees) { mRotationDegrees = degrees; }
	void SetRotationSpeedDegrees(float degreesPerSecond) {
		mRotationSpeedDegrees = degreesPerSecond;
	}
	/** 普通/毒豆穿过火炬树桩后分别变为普通火豆或紫焰毒火豆。 */
	void ConvertToFireball(int torchwoodColumn);
	/** 寒冰豌豆穿过火炬树桩后退化为普通豌豆；同列不会再被点燃。 */
	void ConvertSnowPeaToPea(int torchwoodColumn);
	/** 把合资格射手谱系的当前豌豆形态改为极光弹，保留原发射来源与命中历史。 */
	void ConvertToAuroraPea(int auroraTorchwoodColumn);
	/**
	 * @brief 按存档恢复可变子弹类型与火炬树桩防重状态，不改变对象池槽位类型。
	 */
	void RestoreSavedPresentationState(BulletType currentType, int hitTorchwoodColumn,
		int hitAuroraTorchwoodColumn = -1);
	int GetHitTorchwoodColumn() const { return mHitTorchwoodColumn; }
	void SetHitTorchwoodColumn(int column) { mHitTorchwoodColumn = column; }
	int GetHitAuroraTorchwoodColumn() const { return mHitAuroraTorchwoodColumn; }
	int GetAuroraHitCount() const;
	std::vector<int> GetAuroraHitZombieIDs() const;
	bool HasPlayedAuroraHitSound() const;
	/** 按存档恢复极光弹的终身目标历史；会去重并截断到四目标上限。 */
	void RestoreAuroraState(const std::vector<int>& zombieIDs, bool playedHitSound);
	/** 返回当前类型是否为独立的紫焰毒火豆。 */
	bool IsToxicFireball() const {
		return mBulletType == BulletType::BULLET_TOXICFIREBALL;
	}
	/** 返回尖刺已接触的不同僵尸数量；其他子弹恒为 0。 */
	int GetPiercedZombieCount() const;
	std::vector<int> GetPiercedZombieIDs() const;
	std::vector<float> GetSpikeDamageRemainders() const;
	/** 按存档恢复尖刺穿透目标和小数伤害额度；会去重并截断到玩法上限。 */
	void RestorePiercedZombieState(const std::vector<int>& zombieIDs,
		const std::vector<float>& damageRemainders);
	bool HasAnimatedPresentation() const { return mProjectileAnimator != nullptr; }
	/**
	 * 启用三线射手斜向轨迹；target row 已由本子弹的 mRow 表示，纵向速度按当前地图行高缩放。
	 * @param sourceRow 发射植物所在行，用于确定初始纵向方向。
	 */
	void EnableThreepeaterMotion(int sourceRow);
	bool IsThreepeaterMotion() const { return mThreepeaterMotion; }
	/** 设定本弹丸仅命中空中层或地面层目标。 */
	void SetTargetsFlying(bool targetsFlying) { mTargetsFlying = targetsFlying; }
	bool TargetsFlying() const { return mTargetsFlying; }
	float GetTerrainShadowYForTesting() const { return GetTerrainShadowY(GetPosition()); }
	/**
	 * 把当前弹心作为起点，按固定飞行时间和拱高配置解析抛物线。
	 * 目标预测由发射植物负责；锁定冰墙时，末段碰撞会跳过墙后僵尸。
	 */
	void ConfigureLobbedMotion(
		const Vector& target, float durationSeconds, float apexHeight,
		bool targetsIceWall = false, bool polarGuided = false);
	/** 按存档恢复在途解析抛物线，并重建速度、位置与末段碰撞门禁。 */
	void RestoreLobbedMotion(const Vector& start, const Vector& target,
		float elapsedSeconds, float durationSeconds, float apexHeight,
		bool targetsIceWall = false, bool polarWindMiss = false,
		bool polarGuided = false);
	bool IsLobbedMotion() const { return mTrajectory.kind == TrajectoryKind::LOBBED; }
	bool TargetsIceWall() const {
		return IsLobbedMotion() && mTrajectory.targetsIceWall;
	}
	Vector GetLobStart() const {
		return IsLobbedMotion() ? mTrajectory.start : Vector::zero();
	}
	Vector GetLobTarget() const {
		return IsLobbedMotion() ? mTrajectory.target : Vector::zero();
	}
	float GetLobElapsed() const { return IsLobbedMotion() ? mTrajectory.elapsed : 0.0f; }
	float GetLobDuration() const { return IsLobbedMotion() ? mTrajectory.duration : 0.0f; }
	float GetLobApexHeight() const { return IsLobbedMotion() ? mTrajectory.apexHeight : 0.0f; }
	bool IsPolarWindMiss() const { return mTrajectory.polarWindMiss; }
	bool IsPolarGuided() const { return mTrajectory.polarGuided; }
	float GetLobProgress() const;
	float GetLobArcHeight() const;
	/** 配置玉米加农炮专属轨迹；目标由玩家点击冻结，不再追踪实体。 */
	void ConfigureCobCannonMotion(const Vector& target, int targetRow,
		float durationSeconds = 1.4f, bool polarGuided = false);
	/** 按存档恢复在途玉米棒；不会重放已经过去的发射音效。 */
	void RestoreCobCannonMotion(const Vector& start, const Vector& target,
		int targetRow, float elapsedSeconds, float durationSeconds,
		bool polarWindMiss = false, bool polarGuided = false);
	bool IsCobCannonMotion() const {
		return mTrajectory.kind == TrajectoryKind::COB_CANNON;
	}
	Vector GetCobStart() const {
		return IsCobCannonMotion() ? mTrajectory.start : Vector::zero();
	}
	Vector GetCobTarget() const {
		return IsCobCannonMotion() ? mTrajectory.target : Vector::zero();
	}
	float GetCobElapsed() const {
		return IsCobCannonMotion() ? mTrajectory.elapsed : 0.0f;
	}
	float GetCobDuration() const {
		return IsCobCannonMotion() ? mTrajectory.duration : 0.0f;
	}
	/** 锁定敌方热脉冲的硬终点和原触发实体，并从真实炮口线性收束到目标；沿途实体仍可提前吸收。 */
	void ConfigureThermalPulse(const Vector& endpoint, int originalPlantID);
	float GetThermalEndpointX() const { return mThermalEndpointX; }
	float GetThermalEndpointY() const { return mThermalEndpointY; }
	int GetThermalOriginalPlantID() const { return mThermalOriginalPlantID; }
	int GetCobTargetRow() const {
		return IsCobCannonMotion() ? mTrajectory.targetRow : -1;
	}
	/** AutoTest 投影玉米棒阴影从高空 0.5 倍逐渐长回落地 1 倍的连续状态。 */
	float GetCobCannonShadowScaleForTesting() const {
		return GetCobCannonShadowScale();
	}

	int GetSortingKey() const override { return this->mRow; }
	Vector GetPosition() const { return GetTransform()->GetPosition(); }
	ColliderComponent* GetColliderComponent() { return GetCollider(); }
	const ColliderComponent* GetColliderComponent() const { return GetCollider(); }

private:
	friend class BulletPool;
	int mPoolSlotIndex = -1; // 运行时稳定池槽位；不进入存档，供 Release 直接定位
};

#endif
