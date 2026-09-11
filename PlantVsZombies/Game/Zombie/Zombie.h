#pragma once
#ifndef _ZOMBIE_H
#define _ZOMBIE_H

#include "ZombieType.h"
#include "MagneticItem.h"
#include "../AnimatedObject.h"
#include "../Plant/PlantType.h"
#include "../../DeltaTime.h"
#include "../../GameRandom.h"
#include "../EntityRegistry.h"
#include "../DamageSource.h"
#include "../PlantDamageOrigin.h"
#include "../Bullet/BulletType.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

class Board;
class Plant;
class ShadowComponent;
class Caltrop;

/** 经典扶梯的通用垂直状态；所有可在地面行走的僵尸共享。 */
enum class LadderClimbPhase {
	NONE,
	CLIMBING,
	FALLING,
};

/** 可独立计时或声明永久免疫的僵尸控制类型。 */
enum class ZombieControlEffect : std::uint8_t {
	SLOW,
	FROZEN,
	BUTTER,
	PARALYSIS,
	COUNT,
};

using ZombieControlMask = std::uint32_t;
constexpr std::size_t ZOMBIE_CONTROL_EFFECT_COUNT =
	static_cast<std::size_t>(ZombieControlEffect::COUNT);
constexpr ZombieControlMask ZombieControlBit(ZombieControlEffect effect)
{
	return ZombieControlMask{ 1 } << static_cast<std::uint8_t>(effect);
}
constexpr ZombieControlMask ZOMBIE_CONTROL_HARD_MASK =
	ZombieControlBit(ZombieControlEffect::FROZEN)
	| ZombieControlBit(ZombieControlEffect::BUTTER)
	| ZombieControlBit(ZombieControlEffect::PARALYSIS);

/** 时间锚跨品种保存的窄能力状态；phase 只由原僵尸类型解释。 */
struct ZombieTemporalAbilityState {
	int phase = -1;
	float remaining = 0.0f;
	int releaseCount = 0; // 品种拥有的累计释放次数；随时间锚回溯
};

class Zombie : public AnimatedObject {
private:
	struct ToxinState;
	struct RoofMarshalAssaultState;
	struct TangleKelpState;

public:
	ZombieType mZombieType = ZombieType::NUM_ZOMBIE_TYPES;

	int mRow = -1;

	int mAttackDamage = 50;
	int mFreeHitsRemaining = 0;	// 词条：剩余免伤次数（出生时=4×层数，0=无效）

	bool mNeedDropArm = true;
	bool mNeedDropHead = true;
	int mZombieID = NULL_ZOMBIE_ID;
	int mMineTargetCell = -1; // 矿道当前已锁定行进格；-1表示到达决策点
	/** 矿道决策点的品种路由；-2 沿用普通路线，-1 在节点停步。 */
	virtual int SelectMineNextCell(int) { return -2; }
	/** 矿道跨越行中线时复用正式排序/行索引提交。 */
	void CommitMineRow(int row) { CommitRow(row); }

	int mSpawnWave = -1;	// 多少波刷新的

	int mBodyHealth = 270;
	int mBodyMaxHealth = 270;
	HelmType mHelmType = HelmType::HELMTYPE_NONE;
	int mHelmHealth = 0;
	int mHelmMaxHealth = 0;
	ShieldType mShieldType = ShieldType::SHIELDTYPE_NONE;
	int mShieldHealth = 0;
	int mShieldMaxHealth = 0;

protected:
	Vector mVisualOffset;   // 视觉偏移量
	bool mIsPreview = false;
	float mMistFuelReward = 0.0f; // 正式波次出生时预分配的雾火；召唤物、预览与直造默认 0
	bool mMistFuelRewardClaimed = false; // 死亡结算防重入；随实体存档避免读档重复领取
	bool mFogBreakoutObserved = false; // 是否已建立首次雾区状态，避免出生首帧误触发
	bool mWasObscuredByFog = false; // 上一帧是否处于浓雾索敌阈值内
	bool mFogBreakoutTriggered = false; // 一生一次的雾幕突围是否已消费
	bool mArmorBreakRushSpent = false; // 一生一次的破甲狂潮是否已消费
	float mArmorBreakRushTimer = 0.0f; // 破甲狂潮行动加速剩余游戏秒

	int mGoldenIceEffectStacks = 0;	// 当前黄色冰道速度场层数；由仍存活的铺路者与持久冰道实时派生，不入存档

	float mCooldownTimer = 0.0f;	// 僵尸减速倒计时时间
	float mFrozenTimer = 0.0f;		// 冻结剩余秒数（寒冰菇完全定身），0=未冻结
	float mButterTimer = 0.0f;		// 黄油定身剩余秒数，0=未被黄油固定
	float mParalysisTimer = 0.0f;   // 通用麻痹剩余游戏秒；来源可以是天气、植物或其他机制
	std::array<float, ZOMBIE_CONTROL_EFFECT_COUNT> mControlImmunityTimers{}; // 各控制类型独立的临时免疫游戏秒数
	std::unique_ptr<RoofMarshalAssaultState> mRoofMarshalAssaultState; // 首次受突击令时分配，含计时、倍率和红旗表现
	bool mButterSplatFollowerConfigured = false; // 当前 reanim 是否已绑定语义头部轨道黄油；纯展示派生状态不入档
	std::unique_ptr<ToxinState> mToxinState; // 仅中毒时分配，避免普通僵尸常驻二十层计时器

	bool mIsMindControlled = false;	//有没有被魅惑
	bool mInPool = false;	// 水路介质状态；由基类双探针统一维护，所有僵尸共享

	bool mIsEating = false;
	int mEatPlantID = NULL_PLANT_ID;
	int mEatZombieID = NULL_ZOMBIE_ID;   // 互啃目标（魅惑↔普通）；不持久化——读档后由碰撞下一帧重建
	bool mGarlicRedirectActive = false;   // 大蒜嫌恶反应仍在推进；与啃食目标分离，目标死亡后也必须完成换行
	float mGarlicRedirectElapsed = 0.0f;  // 嫌恶反应已推进的游戏秒数；冻结/黄油期间暂停
	bool mGarlicRowChanged = false;       // 1.7 秒节点是否已经提交相邻行，防止跨帧重复选行

	bool mHasHead = true;
	bool mHasArm = true;
	bool mHasTongue = false;
	bool mIsDying = false;	// 是否播放死亡动画 大概可以这么理解 这个时候不能走路
	bool mIsDead = false;	// Die() 防重入：外部（大嘴花/土豆雷/清场）与帧事件可能同帧重复调 Die，
							// 第二次进入会把 mZombieNumber 多扣一次导致计数提前归零
	bool mTemporalReplacementRetirement = false; // 时间替身接管稳定 ID 时，旧壳只释放生命周期且不结算死亡
	bool mDbgAnomalyLogged = false;	// [DBG] 临时插桩：死亡期间轨道异常只记一次

	float mSpeed = 10.0f;
	int mGroundTrackIndex = -1;
	int mTangleKelpPlantID = NULL_PLANT_ID;	// 正在抓住本僵尸的水草 ID；保证一只僵尸只能被一株水草锁定
	std::unique_ptr<TangleKelpState> mTangleKelpState; // 仅被抓目标分配，含拖沉进度及前后层 anim_grab
	LadderClimbPhase mLadderClimbPhase = LadderClimbPhase::NONE;
	float mLadderAltitude = 0.0f;	// 相对地面的扶梯攀爬高度，单位：像素
	int mUseLadderColumn = -1;	// 最近使用的扶梯列；防止落地后反复攀爬同一架梯

	/** 提交新的逻辑行，并同步刷新植物/僵尸共用的逐行绘制深度。 */
	void CommitRow(int row);

private:
	float mCheckPositionTimer = 0.0f;
	float mSubHealthTimer = 0.0f;	
	float mDyingTimer = 0.0f;	// mIsDying 持续时间，超过 10s 强制 Die 防止卡 BUG
	float mCheckGoldenIceTimer = 0.0f;	// 每秒检查一次黄色冰道速度场层数，避免每帧都查 EntityRegistry
	float mShieldHitGlowTimer = 0.0f;	// 二类护盾独立受击白光剩余秒数；本体白光继续复用 AnimatedObject
	ShieldType mShieldHitGlowType = ShieldType::SHIELDTYPE_NONE;	// 白光计时期间保留已掉落护盾的轨道类型
	CachedText mFullBodyHealthText{};	// 仅缓存满血本体整行，避免动态血量把 pinned 纹理集合无界撑大
	int mCachedFullBodyHealth = -1;

	/** 返回二类护盾对应的独立高亮轨道；未知类型返回 nullptr。 */
	const char* GetShieldGlowTrackName(ShieldType shieldType) const;
	/** 出生后把当前二类护盾轨道从 Animator 整体高亮中分离。 */
	void ConfigureShieldHitGlowTrack();
	/** 在护盾实际扣血后启动或刷新其独立白光。 */
	void StartShieldHitGlow(ShieldType shieldType);
	/** 推进二类护盾白光计时，并在到期时关闭对应轨道高亮。 */
	void UpdateShieldHitGlow();
	/** 在主线程为满血本体血量行取得紧致共享纹理；受伤行保留逐字快路径。 */
	void UpdateFullBodyHealthTextCache();

public:
	Zombie(Board* board, ZombieType zombieType, float x, float y, int row,
		AnimationType animType, float scale = 1.0f, bool isPreview = false);
	~Zombie() override;

	void Start() override;
	void Update() override;
	void Draw(Graphics* g) override;	// 重写以叠加血量显示
	virtual void ZombieUpdate(float scaledTime) {}		// 子类重写Update用这个
	// source 必填，使植物增伤只作用于植物来源。penetrateShield=true：穿透二类护盾（大喷菇喷雾）——护盾照常受损/掉落，
	// 但全额伤害继续透到头盔+本体（还原原版 DoRowAreaDamage(20, 2U) 的位标志语义）。
	// discardShieldOverflow=true：若命中开始时存在二类护盾，则本击止于护盾，破盾溢出也不进入头盔/本体。
	// bypassShield=true：本击从背面等未被护盾覆盖的位置命中，护盾完全不承伤，直接进入头盔/本体。
	virtual void TakeDamage(int damage, DamageSource source, bool penetrateShield = false,
		bool discardShieldOverflow = false, bool bypassShield = false,
		PlantDamageOrigin plantOrigin = {});
	/**
	 * 子弹伤害统一入口：先按命中时的真实水平速度判断正面护盾或背面后层，
	 * 再处理弹丸主动请求的二类护盾绕过；目标可通过 BlocksProjectileShieldBypass 否决后者。
	 */
	void TakeProjectileDamage(int damage, DamageSource source, float velocityX,
		bool penetrateShield = false, bool discardShieldOverflow = false,
		bool requestsShieldBypass = false, PlantDamageOrigin plantOrigin = {});
	/** 植物爆炸的统一入口：默认按原版阈值化灰，否则走带 PLANT_ASH 分类的普通扣血链。 */
	virtual void TakePlantAshDamage(int damage);
	/** 大嘴花等植物直杀的统一入口；只返回是否确实吞掉目标，拒吞后的伤害由攻击者结算。 */
	virtual bool TakePlantInstantKill();
	/** 调整拒绝大嘴花吞食后的基础伤害；默认保持攻击者给出的 20 点，特殊品种可覆写。 */
	virtual int AdjustRejectedChomperBiteDamage(int damage) const { return damage; }
	/** 当前状态是否允许作为已出土地雷的接触触发目标。 */
	virtual bool CanTriggerPotatoMine() const { return true; }
	/** 小推车碰撞是否允许直接处决；首领可拒绝处决，但小推车仍正常启动并驶离。 */
	virtual bool CanBeKilledByMower() const { return true; }
	/** 车辆僵尸接管地刺命中的扩展点；返回 true 表示已处理，地刺不得再结算普通伤害。 */
	virtual bool HandleCaltropHit(Caltrop&) { return false; }
	virtual void SaveExtraData(nlohmann::json& j) const {}	// 保存额外数据
	virtual void LoadExtraData(const nlohmann::json& j) {}	// 加载额外数据
	virtual void ZombieItemUpdate() const; // 处理僵尸读档的时候的手臂、防具等处理
	virtual void Charred();	// 变成灰烬
	/** 水中僵尸不生成陆地烧焦残影；特殊品种仍可进一步收紧。 */
	virtual bool CanBeCharred() const { return !mInPool; }
	/** 是否在当前状态截断大喷菇区域攻击；调用方必须在本次伤害结算前取值。 */
	virtual bool BlocksFumePiercing() const { return false; }
	/** 特殊弹丸主动请求无视二类护盾时，当前目标是否仍强制由护盾承伤。 */
	virtual bool BlocksProjectileShieldBypass() const { return false; }
	/** 按当前装备状态修正指定弹种的基础直击伤害；投掷物与火球默认不变。 */
	virtual int ModifyProjectileDamage(int damage, BulletType) const { return damage; }
	/**
	 * 尝试把盐晶腐蚀结算到当前可腐蚀冰制层；普通目标返回 false。
	 * 品种实现必须在冰层耗尽处截断，剩余腐蚀不得灌入僵尸本体。
	 */
	virtual bool ApplyWinterCorrosion(int) { return false; }
	// TODO(winter-area): 未来冰制护甲品种覆写该入口并导出剩余冰层耐久；独立冰墙走自身结算入口。
	/** 返回尚未提交且可被外部工具打断的特殊动作剩余秒；负数表示当前没有。 */
	virtual float GetInterruptibleSpecialActionRemaining() const { return -1.0f; }
	/** 打断当前尚未提交的特殊动作；已提交或没有动作时返回 false。 */
	virtual bool InterruptUncommittedSpecialAction() { return false; }
	/** 时间锚只持久保留已提交的一次性能力；普通僵尸没有该状态。 */
	virtual bool HasCommittedIrreversibleSpecialAction() const { return false; }
	/** 复活后重建已提交的一次性能力状态，但不重复创建外部对象。 */
	virtual void RestoreCommittedIrreversibleSpecialAction(bool) {}
	/** 把可回溯的一次性能力阶段与剩余时间写入窄快照；普通僵尸不参与。 */
	virtual bool CaptureTemporalAbilityState(ZombieTemporalAbilityState&) const {
		return false;
	}
	/** 用同品种快照覆盖当前本地能力阶段；已经提交的 Board 事务不得在此回滚。 */
	virtual void RestoreTemporalAbilityState(const ZombieTemporalAbilityState&) {}
	/** 时间锚恢复核心数值后，静默重建可逆分件与品种派生状态；不得回滚动作相位或重放掉落效果。 */
	virtual void OnTemporalCoreStateRestored();
	/** 时间锚恢复全部局部阶段后，重建有效啮食表现并原子收尾失效植物目标。 */
	void ReconcileTemporalEatingPresentation();
	/**
	 * 让已进入死亡动画的旧对象把稳定 ID 的计数所有权移交给时间替身。
	 * 该入口同步走品种死亡清理，但不触发死亡奖励、死亡联动、计数扣减或胜利检查。
	 */
	bool RetireForTemporalReplacement();
	/** 当前 Die 调用是否只是在回收已由时间替身接管的旧死亡壳。 */
	bool IsRetiringForTemporalReplacement() const {
		return mTemporalReplacementRetirement;
	}
	/**
	 * 玩家卡片成功部署植物后的同行通知；普通僵尸不响应。
	 * baseMaxHealth 是部署类型的基础最大生命快照，不包含运行期强化。
	 */
	virtual void OnPlayerPlantDeployed(const Plant&, int /*baseMaxHealth*/) {}
	/** 调整大喷菇对本体的基础伤害；返回值随后统一进入词条与防具结算。 */
	virtual int ModifyFumeDamage(int damage) const { return damage; }
	/** 调整仙人掌尖刺每个 1x 碰撞帧的基础伤害；背击绕盾信息在倍速累计前一并传入。 */
	virtual float ModifySpikeFrameDamage(float damage,
		bool /*bypassShield*/ = false) const { return damage; }

	virtual int TakeShieldDamage(int damage);
	virtual int TakeHelmDamage(int damage);
	/**
	 * 按伤害来源结算一类防具，并以原始伤害口径返回进入本体的余量。
	 * 防具专属倍率必须覆写此入口，不能把放大后的破甲溢出灌入本体。
	 */
	virtual int TakeHelmDamageFromSource(int damage, DamageSource source);
	virtual void TakeBodyDamage(int damage);
	/** 词条缩放后的最终伤害修正点；用于按来源、命中面和当前防具状态实施每击上限。 */
	virtual int AdjustIncomingDamage(int damage, DamageSource /*source*/, bool /*penetrateShield*/,
		bool /*bypassShield*/ = false) const {
		return damage;
	}
	/** 返回当前适应状态是否吸收该植物来源的数值伤害；普通僵尸始终返回 false。 */
	virtual bool BlocksPlantDamage(PlantDamageOrigin) const { return false; }
	/** 在头盔承伤前尝试消费整击并记录适应来源；普通僵尸始终返回 false。 */
	virtual bool TryAdaptHelmetToPlantDamage(int, PlantDamageOrigin) { return false; }

	int GetSortingKey() const override { return this->mRow; }

	void CheckWin() const;		// 生成奖杯判断

	virtual void ShieldDrop();		// 二类防具掉落 必须调用Zombie::SheildDrop
	virtual void HelmDrop();	// 一类防具掉落 必须调用Zombie::HelmDrop
	virtual void HeadDrop();	// 头掉落 不用调用Zombie::HeadDrop
	virtual void ArmDrop();		// 手掉落 不用调用Zombie::ArmDrop

	virtual void Die();
	/** 正式新建实体成功后的出生音效钩子；预览和读档恢复不调用。 */
	virtual void PlaySpawnSound() {}
	virtual void StartEat(ColliderComponent* other);
	virtual void StopEat(ColliderComponent* other);
	virtual void EatTarget();	// 吃东西掉血的函数
	/** 返回当前所有品种能力与突击令倍率叠加后的单口伤害。 */
	int GetCurrentBiteDamage() const;
	/** 与小推车接触时是否静默吞掉其他行小推车；当前行仍维持原版碰撞结算。 */
	virtual bool ConsumesOtherMowersOnContact() const { return false; }

	Vector GetVisualPosition() const override;
	/** 返回承载黄油贴图的语义头部轨道；普通僵尸默认使用 anim_head1。 */
	virtual const char* GetButterSplatTrackName() const { return "anim_head1"; }
	/** 是否把黄油延迟到整套 reanim 最后绘制；普通僵尸保持最高层。 */
	virtual bool ShouldDrawButterSplatAfterAllTracks() const { return true; }
	/** 返回黄油语义轨道当前帧的世界锚点，主要供展示诊断与旧路径回退。 */
	Vector GetButterSplatAnchor() const;
	/** 返回黄油贴图相对普通尺寸的倍率；巨型身体可独立放大而不改变锚点。 */
	virtual float GetButterSplatScaleMultiplier() const { return 1.0f; }
	/** 当前 Animator 是否成功配置了轨道内黄油 follower。 */
	bool IsButterSplatFollowerConfigured() const { return mButterSplatFollowerConfigured; }
	/** 当前轨道内黄油是否实际可见；供 AutoTest 核对状态与读档重建。 */
	bool IsButterSplatFollowerVisible() const;
	/** 黄油是状态贴图，不继承僵尸本体的减速/冻结覆盖色。 */
	bool DoesButterSplatFollowerInheritOverlayEffect() const;
	/** 黄油不属于承伤外观，不随父轨道提交 additive glow。 */
	bool IsButterSplatFollowerGlowing() const;
	/** 返回冻结冰晶底边中心的世界锚点；车辆可覆写到整车视觉中央。 */
	virtual Vector GetIceTrapBottomAnchor() const;
	/** 返回冻结冰晶相对普通尺寸的倍率；巨型身体可独立放大而不改变脚底线。 */
	virtual float GetIceTrapScaleMultiplier() const { return 1.0f; }
	Vector GetPosition() const;
	void SetPosition(const Vector& position);
	/**
	 * 时间锚只恢复稳定数值和可选位置，不回滚 Animator 或当前动作相位。
	 * 新复活实体由调用方创建后同样走此入口，再进入安全行走态。
	 */
	void RestoreTemporalCoreState(int row, float x, int bodyHealth,
		HelmType helmType, int helmHealth, ShieldType shieldType, int shieldHealth,
		bool hasHead, bool hasArm,
		float slowTimer, float frozenTimer, float butterTimer,
		float paralysisTimer, bool restorePosition);
	/** 返回当前动画片段的平均根运动速度，并折算减速、冻结、天气和场地状态，单位：像素/游戏秒。 */
	virtual float GetCurrentHorizontalMoveSpeed() const;
	/** 轻量推演使用的未受减速或硬控影响水平速度；品种、天气和场地倍率仍保留。 */
	float GetUncontrolledHorizontalMoveSpeed() const;
	/** 按当前碰撞矩形中心与片段平均行走速度预测水平落点，供投手和倭瓜复刻 ZombieTargetLeadX。 */
	float GetTargetLeadX(float seconds) const;
	/** 当前自主行走是否朝战场前线（世界坐标 +X）；反向品种覆写后由位移、风速与预测共用。 */
	virtual bool IsMovingRight() const { return mIsMindControlled; }
	/**
	 * 判断子弹本次是否绕过二类护盾。背后追击保留物理绕盾语义；特殊弹丸的主动
	 * 绕盾请求还必须通过目标自身的 BlocksProjectileShieldBypass 门禁。
	 */
	bool ShouldProjectileBypassShield(
		float velocityX, bool requestsShieldBypass = false) const;
	/** 当前状态是否允许越过房屋失败线；地下、出土等特殊阶段可关闭。 */
	virtual bool CanTriggerGameOver() const { return !IsMovingRight(); }

	bool IsMindControlled() const { return this->mIsMindControlled; }
	void SetMistFuelReward(float reward) {
		mMistFuelReward = std::max(0.0f, reward);
		mMistFuelRewardClaimed = false;
	}
	float GetMistFuelReward() const {
		return mMistFuelRewardClaimed ? 0.0f : mMistFuelReward;
	}
	float ClaimMistFuelReward() {
		if (mMistFuelRewardClaimed) return 0.0f;
		mMistFuelRewardClaimed = true;
		return mMistFuelReward;
	}
	// 魅惑唯一入口：豁免(CanBeCharmed)/重复/垂死则 no-op。魅惑菇、AutoTest charm_zombie 都走这里。
	void StartMindControlled();
	// 子类豁免点：不可魅惑态（如撑杆 RUNNING/JUMPING）返回 false
	virtual bool CanBeCharmed() const { return true; }
	bool HasHead() const { return this->mHasHead; }
	bool IsDying() const { return this->mIsDying; }
	bool IsPreview() const { return this->mIsPreview; }
	bool IsEating() const { return this->mIsEating; }
	int GetEatingPlantID() const { return mEatPlantID; }
	/** 搬运提交前解除指向指定植物的啃食关系；其他目标及已结算伤害不变。 */
	void ReleaseRelocatedPlant(int plantID);
	/** 是否正在执行由大蒜首口触发的停顿、换行或收尾阶段。 */
	bool IsGarlicRedirecting() const { return mGarlicRedirectActive; }
	/** AutoTest 与存档诊断使用的嫌恶反应进度，单位：游戏秒。 */
	float GetGarlicRedirectElapsed() const { return mGarlicRedirectElapsed; }
	/** 本次嫌恶反应是否已经把逻辑行切到目标相邻行。 */
	bool HasGarlicChangedRow() const { return mGarlicRowChanged; }
	/** 当前是否处于应显示嫌恶脸的时间窗，供状态导出验证外观恢复。 */
	bool IsGarlicYuckFaceVisible() const;
	bool IsInPool() const { return this->mInPool; }
	bool HasArm() const { return this->mHasArm; }
	/** 返回处决机制统一使用的当前生命：本体、一类防具与二类护盾；飞行额外生命不计。 */
	int GetCountableExecutionHealth() const;
	/** 返回本体、头盔与护盾的最大可计生命总和，饱和到 int 上限。 */
	int GetCountableMaxHealth() const;
	/** 以本品种正常死亡表现结算劫持者处决；无死亡轨道的品种沿用自身立即死亡入口。 */
	virtual void TakeHijackerExecution();
	LadderClimbPhase GetLadderClimbPhase() const { return mLadderClimbPhase; }
	float GetLadderAltitude() const { return mLadderAltitude; }
	int GetUseLadderColumn() const { return mUseLadderColumn; }
	/** 当前实体是否满足磁力菇通用目标门禁且仍持有可吸取装备。 */
	bool CanBeTargetedByMagnetShroom() const;
	/** 品种是否仍持有可被磁力菇吸取的装备；派生类只声明装备状态。 */
	virtual bool HasMagneticItem() const { return false; }
	/**
	 * 返回轻量推演在吸取时移除的生命层；特殊品种可覆写默认的盾牌/头盔/工具推断。
	 */
	virtual MagneticSimulationLayer GetMagneticSimulationLayer() const {
		if (!HasMagneticItem()) return MagneticSimulationLayer::NONE;
		if (mShieldHealth > 0) return MagneticSimulationLayer::SHIELD;
		if (mHelmHealth > 0) return MagneticSimulationLayer::HELM;
		return MagneticSimulationLayer::TOOL;
	}
	/**
	 * 立即卸下当前金属装备，并把离体表现交给磁力菇。
	 * 返回 false 表示装备已在同帧由其他路径移除，调用方不得开始充能。
	 */
	virtual bool ExtractMagneticItem(MagneticItem&) { return false; }
	/** 当前实体能否替指定同排目标承接一次黑夜屋顶放电。 */
	virtual bool CanProtectFromNightRoofCharge(const Zombie*) const { return false; }
	/** 原子承接目标本应受到的基础放电伤害；成功后目标不再受伤或麻痹。 */
	virtual bool AbsorbNightRoofChargeFor(Zombie*, int) { return false; }
	/** 结算未被其他实体承接的放电命中；特殊防具可改写分层伤害。 */
	virtual void TakeNightRoofChargeImpact(int damage, float paralysisDuration,
		bool onWetSlope);
	/** 品种是否属于黑夜屋顶可引雷单位；装备损毁后仍保持类型身份供弱索引管理。 */
	virtual bool IsNightRoofChargeGuideType() const { return false; }
	/** 当前实例是否仍能为满电决策提供植物专属引导候选。 */
	virtual bool CanGuideNightRoofCharge() const { return false; }
	/** 返回引雷附件的世界锚点；附件不存在时返回 false。 */
	virtual bool TryGetNightRoofChargeGuideAnchor(Vector&) const { return false; }
	/** 本体、头盔或飞行额外生命层是否正处于受击白光期。 */
	bool IsBodyHitFlashing() const { return mGlowingTimer > 0.0f; }
	/** 二类护盾是否正处于独立受击白光期。 */
	bool IsShieldHitFlashing() const { return mShieldHitGlowTimer > 0.0f; }
	/** 二类护盾轨道合并整体与独立开关后的实际高亮状态。 */
	bool IsShieldTrackGlowing() const;
	/** 是否仍处于空中；默认僵尸始终在地面。 */
	virtual bool IsFlying() const { return false; }
	/** 径流自然锁行时是否允许本实体把自己的行纳入本次行组。 */
	virtual bool CanGuideRoofRunoff() const { return false; }
	/** 当前实例承受屋顶径流附加位移的倍率；默认不改变 Board 基础速度。 */
	virtual float GetRoofRunoffDriftMultiplier() const { return 1.0f; }
	/**
	 * 返回当前阶段能否被指定高度层的弹丸命中。
	 * @param targetsFlying true=对空弹丸，false=地面弹丸。
	 */
	virtual bool CanBeTargetedByProjectile(bool targetsFlying) const {
		return !targetsFlying;
	}
	float GetCooldownTimer() const { return this->mCooldownTimer; }
	bool IsFrozen() const { return this->mFrozenTimer > 0.0f; }
	float GetFrozenTimer() const { return this->mFrozenTimer; }
	bool IsButtered() const { return mButterTimer > 0.0f; }
	float GetButterTimer() const { return mButterTimer; }
	/** 返回指定控制是否正被临时计时或品种永久规则免疫。 */
	bool IsControlImmune(ZombieControlEffect effect) const;
	/** 返回指定控制的临时免疫剩余游戏秒；永久免疫不伪造有限计时。 */
	float GetControlImmunityTimeRemaining(ZombieControlEffect effect) const;
	/** 返回当前临时与永久免疫的控制位集合。 */
	ZombieControlMask GetActiveControlImmunityMask() const;
	/**
	 * 同时发放一组临时控制免疫；重复来源逐类型保留更长时长。
	 * clearExisting 为 true 时，只清除 mask 覆盖的既有控制，不影响减速等未选类型。
	 */
	void GrantControlImmunity(ZombieControlMask mask, float durationSeconds,
		bool clearExisting = true);
	/** @brief 施加或刷新屋脊督军突击令；重复命令只延长并保留较强倍率。 */
	void ApplyRoofMarshalAssault(float duration, float moveMultiplier, float biteMultiplier);
	bool IsRoofMarshalAssaultActive() const;
	float GetRoofMarshalAssaultTimer() const;
	float GetRoofMarshalAssaultMoveMultiplier() const;
	float GetRoofMarshalAssaultBiteMultiplier() const;
	/** 突击令红旗附件是否成功创建；用于资源闭环与 AutoTest。 */
	bool HasRoofMarshalAssaultFlagAnimator() const;
	/** 突击令红旗当前是否随目标僵尸显示。 */
	bool IsRoofMarshalAssaultFlagVisible() const;
	/** 冻结、黄油或通用麻痹任一生效时，统一视为完全定身。 */
	bool IsImmobilized() const { return IsFrozen() || IsButtered() || IsParalyzed(); }
	/**
	 * @brief 对僵尸施加通用麻痹；重复施加只保留更长的剩余时间。
	 * @return 当前品种与阶段接受麻痹时返回 true。
	 */
	bool ApplyParalysis(float durationSeconds);
	bool IsParalyzed() const { return mParalysisTimer > 0.0f; }
	float GetParalysisTimeRemaining() const { return mParalysisTimer; }
	/** 车辆等永久免疫麻痹的品种覆写此接口；伤害资格与它相互独立。 */
	virtual bool CanBeParalyzed() const { return true; }
	/** 品种或阶段是否接受黄油定身；默认沿用冻结阶段门禁。 */
	virtual bool CanBeButtered() const { return CanBeFrozen(); }
	/** 地面区域危害的通用命中资格；飞行单位默认免疫，特殊阶段可进一步收紧。 */
	virtual bool CanBeAffectedByGroundHazards() const { return !IsFlying(); }
	/**
	 * 地面能力命中后请求目标立即结束地下阶段；普通品种无地下阶段，默认不处理。
	 * @return true 表示目标接受请求并开始出地，调用方不得直接改写其私有阶段。
	 */
	virtual bool ForceSurfaceFromGroundHazard() { return false; }
	/** 玉米加农炮爆炸的品种/阶段命中资格；默认沿用地面危害，飞行品种可单独放行。 */
	virtual bool CanBeAffectedByCobCannonExplosion() const {
		return CanBeAffectedByGroundHazards();
	}
	/** 黄油弹命中入口；返回 false 表示当前品种或阶段免疫定身。 */
	virtual bool ApplyButter();
	/** 命中时增加毒层；满二十层则刷新剩余时间最短的一层。 */
	bool ApplyToxinStack();
	/** 清除全部毒层及尚未结算的小数伤害。 */
	void ClearToxin();
	int GetToxinLayerCount() const;
	float GetToxinMaxRemaining() const;
	float GetToxinDamageRemainder() const;
	bool HasTriggeredFogBreakout() const { return mFogBreakoutTriggered; }
	bool HasSpentArmorBreakRush() const { return mArmorBreakRushSpent; }
	float GetArmorBreakRushTimeRemaining() const { return mArmorBreakRushTimer; }
	bool IsGoldenIceSpeedActive() const { return mGoldenIceEffectStacks > 0; }
	int GetGoldenIceEffectStacks() const { return mGoldenIceEffectStacks; }
	/** 同时清除减速与冻结，并恢复当前天气/能力组合后的动画速度。 */
	void RemoveColdEffects();
	/** 原版火球耐性：冰车类及仍持门/梯二类护盾的目标只承受直击，不吃溅射或解冻。 */
	bool IsFireResistant() const;
	/** 当前状态是否满足原版水草的近身锁定条件。 */
	bool CanBeTargetedByTangleKelp() const;
	/** 品种特殊阶段过滤点；撑杆跳跃和伴舞出土等状态可覆写。 */
	virtual bool CanBeGrabbedByTangleKelp() const { return true; }
	/** 持盾等品种状态是否把水草的拖沉改为限时束缚。 */
	virtual bool ResistsTangleKelpDrowning() const { return false; }
	/** 原子建立一对一抓取关系并创建包裹僵尸的前后层 anim_grab。 */
	bool StartTangleKelpGrab(int plantID);
	/** 到达 51cs 节点后停止当前啃食，并让僵尸按原版速率沉入水下。 */
	void DragUnderByTangleKelp(int plantID);
	/** 抗拖沉束缚结束时解除一对一关系与 anim_grab，不伤害僵尸。 */
	void ReleaseTangleKelpGrab(int plantID);
	bool IsTangleKelpTarget() const { return mTangleKelpPlantID != NULL_PLANT_ID; }
	bool IsTangleKelpTargetOf(int plantID) const { return mTangleKelpPlantID == plantID; }
	int GetTangleKelpPlantID() const { return mTangleKelpPlantID; }
	bool IsDraggedUnderByTangleKelp() const;
	float GetTangleKelpSinkOffset() const;
	float GetTangleKelpGrabFrame() const;

	// 冻结唯一入口（寒冰菇）：先上 20s 减速尾巴（SetCooldown，其持盾守卫保留——持盾照冻不吃减速），
	// 再完全定身（首冻 4~6s / 已减速或已冻再冻 3~4s）并结算 20 点固定伤害。
	// 返回 true=进入冻结；豁免（魅惑/濒死/预览/CanBeFrozen 覆写/当前冻结免疫）返回 false；
	// 当前冻结免疫仍保留固定伤害和未被单独免疫的减速尾巴。
	bool StartFrozen(PlantDamageOrigin plantOrigin = {});
	// 行为守卫放虚函数（skill 教训：勿放 lambda）。Chilled=减速+冻结的总闸（魅惑免疫在基类）；
	// Frozen=仅豁免定身、减速尾巴照上（如撑杆跳跃中，原版 CanBeFrozen 语义）。
	virtual bool CanBeChilled() const;
	virtual bool CanBeFrozen() const { return true; }

	/** 设置减速；背击子弹可绕过仍存在但未实际承伤的二类护盾。 */
	virtual void SetCooldown(float timer, bool bypassShield = false);
	// Board 天气切换时调用，统一重算 extra 层；不会覆盖 PlayTrack 的 clip 速度。
	void RefreshAnimSpeedForWeather() { UpdateAnimSpeed(); }

	void SaveProtectedData(nlohmann::json& j) const;

	void LoadProtectedData(const nlohmann::json& j);
	/** 派生读档与装备外观恢复完成后，重建大蒜脸覆盖和动画停走层。 */
	void FinalizeProtectedLoad();

	void ValidateEatingState(EntityRegistry& em);

	/** 出生时缩放各层当前及最大生命；防具与额外保护层另乘 armorMultiplier，本体仅乘 multiplier。
	 * 非正倍率不处理；各层合并倍率后仅四舍五入一次，满血仍保持 current==max。读档不得重乘。
	 */
	void ApplyHealthMultiplier(double multiplier, double armorMultiplier = 1.0);
	/** 治疗等正向耐久变化后，按当前生命重新派生本品种全部可逆破损贴图。 */
	virtual void RefreshEquipmentPresentationAfterRepair() {
		CheckHelmImage();
		CheckShieldImage();
	}

protected:
	/** 把当前 Animator 轨道局部锚点换算为稳定世界坐标，供离体装备取得起点。 */
	Vector GetTrackWorldPosition(const std::string& trackName) const;
	/** 把轨道原点按 Animator 水平镜像和最终渲染缩放换算为画面世界坐标。 */
	Vector GetRenderedTrackWorldPosition(const std::string& trackName) const;
	// 统一重算动画 extra 速度层：冻结/黄油/麻痹(0) > 品种能力 × 减速 × 雨势。
	// 子类自身的整体倍率只从 GetAbilityAnimSpeedMultiplier 返回；运行期状态变化后经此收敛，
	// 禁止直调 SetExtraSpeedMultiplier，否则会把冻结停格顶掉或丢失天气组合。
	void UpdateAnimSpeed();
	/** Board 权威定时动作可返回非负倍率，临时绕过控制、天气和减速的动画速度组合。 */
	virtual float GetForcedAnimSpeedMultiplier() const { return -1.0f; }
	/** 品种固有的永久控制免疫集合；默认没有，未来免控僵尸覆写即可。 */
	virtual ZombieControlMask GetPermanentControlImmunityMask() const { return 0; }
	/** 按未减速游戏时间推进全部临时控制免疫。 */
	void UpdateControlImmunity(float deltaTime);
	/** 清除黄油定身并按剩余状态恢复动画速度。 */
	void ClearButter();
	/** 清除通用麻痹并按剩余状态恢复动画与染色。 */
	void ClearParalysis();
	/** 出生 Setup 完成后把黄油统一绑定到品种声明的语义头部轨道。 */
	void ConfigureButterSplatFollower();
	/** 同步轨道内黄油显隐；未配置的异形资源继续由 Draw 的旧锚点路径兜底。 */
	void SetButterSplatFollowerVisible(bool visible) const;
	/** 首次受突击令时创建红旗子 Animator，并挂到该品种已审计的语义头部轨道。 */
	void ConfigureRoofMarshalAssaultFlag();
	/** 同步突击令红旗显隐；计时仍由通用突击令状态唯一拥有。 */
	void SetRoofMarshalAssaultFlagVisible(bool visible);
	/** 头盔和护盾前的额外生命层；返回继续透入常规防具链的伤害。 */
	virtual int TakeExtraProtectionDamage(int damage, DamageSource) { return damage; }
	/** 生存血量倍率对品种额外生命层的扩展点。 */
	virtual void ApplyExtraHealthMultiplier(double) {}
	// 减速时动画降速因子（快速铁桶 0.8 覆写；位移减半由 Update 的 scaledDelta 承担，与此正交）
	virtual float GetSlowAnimFactor() const { return 0.6f; }
	// 僵尸自身最终提供的整体动画能力倍率；可由固定品种值、运行期状态或已持久化随机结果派生。
	virtual float GetAbilityAnimSpeedMultiplier() const { return 1.0f; }
	/** 僵尸自身状态对每口啃咬伤害的倍率；与突击令倍率相乘。 */
	virtual float GetAbilityBiteDamageMultiplier() const { return 1.0f; }
	/** 读取旧版根字段 extraSpeed；仅仍需实例随机倍率的派生类覆写，兼容完成后不再传播旧字段。 */
	virtual void RestoreLegacyAbilityAnimSpeedMultiplier(float) {}
	/** 返回经过黄色冰道叠层后的能力速度倍率；品种可覆写以施加自身上限。 */
	virtual float GetAmplifiedAbilitySpeedMultiplier() const;
	/** 品种是否无条件承受黄色冰道速度场；铺路者自身不依赖几何覆盖。 */
	virtual bool IsAlwaysAffectedByGoldenIce() const { return false; }
	/** 当前品种是否接受台风的水平漂移；定点悬挂单位可关闭。 */
	virtual bool CanBeMovedByTyphoonGust() const { return true; }
	/** 当前逻辑位置是否已经越过世界回收边界；编队可覆写为共享位置权威。 */
	virtual bool IsOutsideWorldCleanupBounds(const Vector& position) const;
	/** 统计当前点由多少辆仍存活的鎏金冰车覆盖；仅剩持久冰道时至少返回一层。 */
	int ComputeGoldenIceEffectStacks() const;
	/** 每层黄色冰道把加速倍率乘二、减速倍率除二；中性倍率 1.0 保持不变。 */
	float AmplifySpeedMultiplierForGoldenIce(float multiplier) const;
	/** 在跨入、离开、叠层变化或冰道消失的边沿刷新 Animator 速度组合。 */
	void RefreshGoldenIceSpeedState();
	// 解除冻结并恢复动画速度；最终染色由全部剩余状态统一派生。
	void ClearFrozen();
	/** 按未减速的游戏时间推进毒层，并沿普通投射物伤害链结算。 */
	void UpdateToxin(float deltaTime);
	/** 推进迷雾突围与破甲狂潮的生存词条实体状态。 */
	void UpdateSurvivalPerkStates(float deltaTime);
	/** 首次破坏任一防具层时原子触发解控、免控和行动加速。 */
	void TriggerArmorBreakRush();
	/** 亲口吃掉植物后修复仍存在的生命层，不复活已掉落装备。 */
	void RepairExistingHealthLayers();
	/** 统一应用魅惑、寒冷和中毒共享的 Animator overlay 优先级。 */
	void UpdateStatusOverlay();

	/** 叠加活动阵风的物理漂移；不依赖自主行走、啃食、冻结或魅惑方向。 */
	void ApplyTyphoonGustDrift(float deltaTime, Transform* transform);
	/** 叠加昼夜屋顶目标行的顺坡径流；只移动仍处于地面的可移动品种。 */
	void ApplyRoofRunoffDrift(float deltaTime, Transform* transform);
	/** 把所有僵尸品种的 Transform Y 收敛到当前 X 对应的屋顶连续坡面。 */
	void SyncToRoofTerrain(Transform* transform);
	/** 推进大蒜嫌恶反应，并在原版节点停止啃食、选择同介质相邻行和恢复移动。 */
	void UpdateGarlicRedirect(float deltaTime, Transform* transform);
	/** 首口命中大蒜后建立独立嫌恶阶段，并立即冻结当前啃食动画。 */
	void StartGarlicRedirect();
	/** 立即结束大蒜嫌恶反应；报纸狂暴与死亡用它原子清理脸图、啃食和纵向过渡。 */
	void CancelGarlicRedirect(bool stopEating);
	/** 当前大蒜阶段是否应冻结自主位移与 Animator extra 层。 */
	bool IsGarlicRedirectPaused() const;
	/** 当前品种是否有匹配的嫌恶脸贴图；缺专属资源的品种走原版短停顿分支。 */
	bool SupportsGarlicYuckFace() const;
	/** 设置或恢复嫌恶脸以及被它临时隐藏的头部附属轨道。 */
	void SetGarlicYuckFaceVisible(bool visible);
	/** 按屏上僵尸数节流并播放原版 2:1 权重的 Yuck Foley。 */
	void PlayGarlicYuckSound() const;
	/** 从当前行的同介质相邻候选中选定一行并更新逻辑行。 */
	bool ChangeRowForGarlic();
	/** 恢复当前品种原有头图；报纸狂暴态覆写为 madhead。 */
	virtual void RestoreHeadImageAfterGarlic();
	/**
	 * 用前后双探针维护通用入水状态；切换介质时同步视觉并恢复当前稳态走路轨道。
	 * @param playTransitionFeedback 首次生成、读档或品种自管演出结束时传 false，避免伪造跨界反馈。
	 */
	void UpdatePoolState(bool playTransitionFeedback = true);
	/** 取得通用双探针中点对应的稳定池沿水花原点。 */
	Vector GetPoolTransitionSplashOrigin() const;
	/** 在给定水面世界点播放原版 Splash 动画与 PlantingPool 水滴。 */
	void PlayPoolSplashVisual(const Vector& origin) const;
	/** 对齐 C# PoolSplash：播放通用水花，并按入水/出水语义选择 Foley 音效。 */
	void PlayPoolTransitionFeedback(bool entering) const;
	/** 当前阶段是否允许基类按地面探针切换水路状态。 */
	virtual bool CanUseGroundPoolState() const { return true; }
	/** 若目标格有扶梯，则阻止啃食并按原版条件开始攀爬。 */
	bool TryStartLadderClimb(Plant* plant);
	/** 由放梯者在成功放置的同帧直接进入攀爬。 */
	void BeginLadderClimb(int column);
	/** 推进攀爬/下落高度和慢速僵尸的额外前移。 */
	void UpdateLadderClimb(float scaledDelta, Transform* transform);
	/** 按通用入水状态隐藏陆地阴影；水面以下裁剪在 Draw 内与其他 Clip 嵌套。 */
	void UpdatePoolVisualState() const;
	/**
	 * @brief 返回当前绘制是否需要水面裁剪及其世界坐标底线。
	 *
	 * 默认只裁剪已经处于水中的僵尸；特殊入水演出可覆写时间窗而不改变其他品种。
	 */
	virtual bool TryGetDrawClipBottom(float& clipBottom) const;
	virtual void ZombieMove(float scaledDelta, Transform* transform);

	// 这才是设置僵尸
	virtual void SetupZombie();
	/** 注册当前 reanim 时间轴上的死亡与啃食帧事件；帧布局不同的品种在此替换。 */
	virtual void RegisterFrameEvents();
	/** 返回掉头流血结束后应播放的死亡轨道名。 */
	virtual const char* GetDeathTrackName() const { return "anim_death"; }
	/** 当前品种阶段是否播放倒地动画；骑乘载具等阶段可要求直接移除。 */
	virtual bool ShouldPlayDeathAnimation() const { return true; }

	virtual void CheckHelmImage() {}	// 检查是否应该更换一类防具图片
	virtual void CheckShieldImage() {} 	// 检查是否应该更换二类防具图片

	// ═══ 啃食 → 走路 状态机：扩展新僵尸只需覆写下面带 ★ 的 virtual，永不碰 ResumeWalkAfterEat ═══

	// ★关注点A｜"我此刻怎么走路"的唯一权威：播哪条稳态走路轨道 + clip 速度（须确定性，勿随机）。
	//   啃完回走路 / 撑杆落地 / 读档恢复 全经它，所以"改走路动画"永远只改这一处。
	//   何时覆写：reanim 没有 anim_walk2，或走路轨道随状态变。
	//   例（读报僵尸·狂暴撕报后换轨道并带 clip 速度）：
	//     void PlayWalkAnimation(float blend) override {
	//         if (mHasNewspaper) PlayTrack("anim_walk", 0.0f, blend);
	//         else               PlayTrack("anim_walk_nopaper", kNoPaperWalkClip, blend);
	//     }
	virtual void PlayWalkAnimation(float blendTime = 0.0f);

	// ★关注点B｜啃食视觉残留（一对对称钩子，默认空操作，绝大多数僵尸不用管）：
	//   OnStartEating 一开吃触发、OnStopEating 一停吃触发。在 StartEat 里改过的视觉状态，在这对里对称还原。
	//   例（铁门僵尸·啃食露常规手臂、收尾藏回门后，门还在才动）：
	//     void OnStartEating() override { if (mShieldType != ShieldType::SHIELDTYPE_NONE) ShowArm(true);  }
	//     void OnStopEating()  override { if (mShieldType != ShieldType::SHIELDTYPE_NONE) ShowArm(false); }
	virtual void OnStartEating() {}
	virtual void OnStopEating()  {}
	/** 魅惑状态完成提交后的派生回调；一次性动作在此原子撤销，避免旧帧事件抢跑。 */
	virtual void OnMindControlled() {}
	/** 按 C# EatingOrder 选目标：只有仍可啃的上层植物才遮挡下层，并尊重植物自己的离地或短期保护。 */
	bool IsPlantValidEatTarget(Plant* plant) const;
	/** 把同格啃食目标迁移到当前最高有效植物，并保持双方 mEaterCount 平衡。 */
	bool RetargetPlantWithinCell(Plant* plant);
	/** 检查当前植物目标仍存活、仍是当前最高有效目标且处于允许啃食的接触距离。 */
	bool IsCurrentPlantEatingTargetValid();
	/** 目标失效时原子清理目标 ID、eaterCount 与啃食视觉，并恢复当前品种的稳态行走。 */
	void StopEatingInvalidPlantTarget(float blendTime);
	/** 特殊动作抢占啃食时原子清理双方状态；调用者负责立即选择新的动作动画。 */
	bool CancelEatingForSpecialAction();
	/** 创建两份水草抓取动画，并分别隐藏前层或后层，以便包裹僵尸本体。 */
	void CreateTangleKelpGrabAnimators(float savedFrame = 22.0f);
	/** 停止当前啃食并平衡目标计数；抗拖沉目标在抓取开始时调用，普通目标在拖沉节点调用。 */
	void StopEatingForTangleKelp();
	/** 清理失效或已释放的一对一关系与附着视觉。 */
	void ClearOrphanedTangleKelpGrab();

	// 模板方法（非虚，勿覆写）：啃完回走路 = 先收尾、再走路。执行顺序由基类锁死。
	void ResumeWalkAfterEat(float blendTime) { OnStopEating(); PlayWalkAnimation(blendTime); }
	// 魅惑的派生状态（碰撞掩码+视觉）：StartMindControlled 与读档恢复共用
	void ApplyCharmEffects();

private:
	void WinGame() const;	// 植物胜利
};

#endif
