#pragma once
#ifndef _PLANT_H
#define _PLANT_H
#include <iostream>
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include "./PlantType.h"
#include "../ColliderComponent.h"
#include "../Transform.h"
#include "../AnimatedObject.h"
#include "../AudioSystem.h"
#include "../../GameRandom.h"
#include "../../DeltaTime.h"
#include "../EntityRegistry.h"
#include "../DamageSource.h"
#include "../Zombie/ZombieJumpType.h"
#include "../Zombie/ZombieType.h"

class Board;
class Zombie;

enum class PlantBungeeState {
	NONE,
	GRABBING,
	RISING,
};

enum class AirborneDefenseState {
	INACTIVE,
	ACTIVATING,
	REFLECTING,
};

/** 会沿冻土传播或撞击植物格的冬季地面威胁类别。 */
enum class WinterGroundImpactKind {
	COLLISION,
	GROUND_CRACK,
};

/** 植物对一次冬季地面冲击的原子响应；伤害和动作时序仍由威胁方拥有。 */
struct WinterGroundImpactResponse {
	bool intercepted = false;
	bool containsScatter = false;
	float downstreamDamageMultiplierCap = 1.0f;
};

class Plant : public AnimatedObject {
public:
	Board* mBoard = nullptr;
	PlantType mPlantType = PlantType::NUM_PLANT_TYPES;

	int mRow = 0;
	int mColumn = 0;
	int mPlantHealth = 300;
	int mPlantMaxHealth = 300;
	int mPlantID = NULL_PLANT_ID;
	int mEaterCount = 0;			// 正在啃食此植物的僵尸数量

protected:
	bool mIsSleeping = false;	// 白天蘑菇睡眠权威状态
	std::shared_ptr<Animator> mSleepIndicatorAnimator; // 原版 Z.reanim 睡眠标识；由睡眠状态派生，不单独入档
	float mWakeUpTimer = 0.0f;	// 咖啡豆唤醒倒计时，单位：秒；大于 0 时仍保持睡眠
	float mShutdownTimer = 0.0f; // 通用停机剩余游戏秒；来源可以是天气、僵尸技能或其他机制
	bool mIsPreview = false;
	bool mIsSquished = false;	// 压扁残影仍参与绘制，但已退出占格、碰撞与植物行为
	float mSquishTimer = 0.0f;	// 压扁残影剩余时间，单位：秒
	Vector mSquishVisualPosition; // 进入压扁态时冻结的画面位置，不再跟随水面浮动或阵风插值
	Vector mVisualOffset;	// 视觉显示偏移
	Vector mGridMoveVisualOffset; // 阵风换格后的瞬态画面偏移；逻辑格与碰撞箱已在目标格
	Vector mGridMoveVisualStart;  // 本次平滑位移的起始偏移，用于无漂移插值
	float mGridMoveVisualTimer = 0.0f;
	float mGridMoveVisualDuration = 0.0f;
	PlantBungeeState mBungeeState = PlantBungeeState::NONE;
	int mBungeeOwnerZombieID = NULL_ZOMBIE_ID;
	Vector mBungeeVisualOffset;
	int mIceSealOwnerZombieID = NULL_ZOMBIE_ID; // 冰像封存的唯一来源；进度仍由来源僵尸拥有
	bool mUnyieldingRootsSpent = false; // 生存词条：本株本轮的致命伤保护是否已消费
	float mUnyieldingRootsTimer = 0.0f; // 生存词条：致命伤保护后的普通伤害免疫剩余秒数

public:
	static constexpr float kFlowerPotVisualLiftY = -5.0f; // 上层植物相对花盆抬升量，单位：px

	Plant(Board* board, PlantType plantType, int row, int column,
		AnimationType animType, float scale = 1.0f, bool isPreview = false);

	~Plant() = default;
	void Start() override;
	/** 并行动画阶段也遵守通用停机与屋顶径流暂停，避免帧事件先于串行行为守卫触发。 */
	void UpdateParallel(std::vector<DeferredEvent>& outBuf) override;
	void Update() override;
	void Draw(Graphics* g) override;	// 重写以叠加血量显示
	Vector GetVisualPosition() const override;

	int GetSortingKey() const override { return this->mRow; }

	virtual void PlantUpdate();		// 子类重写Update用这个
	/** 返回占层、占格与落种契约使用的类型；模仿者占位时返回其目标。 */
	virtual PlantType GetPlacementType() const { return mPlantType; }
	/** 当前阶段是否允许主动搬运；动作中的品种应在自己的类中收紧资格。 */
	virtual bool CanBeRelocated() const {
		return OccupiesGridSlot() && mPlantHealth > 0
			&& !IsBungeeTargeted() && !IsIceSealed();
	}
	/** 绘制需要夹在承载/普通层与本体前层之间的格子背景；默认植物没有这一层。 */
	virtual void DrawStackBackground(Graphics*) {}
	/** 指定格的持续地面移动倍率；来源自行判断位置、活动与范围，默认不减速。 */
	virtual float GetGroundSlowFactorAtCell(int, int) const { return 1.0f; }
	// 统一结算植物承伤；source 必填，使僵尸增伤只作用于僵尸来源。
	virtual void TakeDamage(int damage, DamageSource source);
	/**
	 * 结算热感部署拦截命中原触发实体的数值伤害。
	 * 默认仍走普通承伤；只有拥有部署前短暂无敌的即时植物覆写以放行这一次数值攻击。
	 */
	virtual void TakeDeploymentInterceptionDamage(int damage, DamageSource source) {
		TakeDamage(damage, source);
	}
	/**
	 * 敌方水平直射弹进入本格时请求消耗一层附属拦截。
	 * 返回 true 表示弹体已被完整吸收；普通植物保持 false。
	 */
	virtual bool TryInterceptHostileStraightProjectile(
		float /*velocityX*/, const Vector& /*impactPosition*/) { return false; }
	/** 当前是否为可覆盖指定格的极夜导航提供者。 */
	virtual bool CoversPolarNavigationCell(int, int) const { return false; }
	/** 当前活动领域是否已经能为覆盖格提供导航。 */
	virtual bool IsPolarNavigationActive() const { return false; }
	/** 当前是否已蓄满并可按需开启领域。 */
	virtual bool IsPolarNavigationReady() const { return false; }
	/** 按真实受干扰动作开启领域；返回 false 表示边沿已经失效。 */
	virtual bool ActivatePolarNavigation() { return false; }
	/** 新一轮开始时恢复本株一次不屈根系资格；新种植物默认也尚未消费。 */
	void ResetUnyieldingRootsForRound();
	bool HasSpentUnyieldingRoots() const { return mUnyieldingRootsSpent; }
	float GetUnyieldingRootsTimeRemaining() const { return mUnyieldingRootsTimer; }
	/** 从实体快照恢复不屈根系状态；旧档缺字段时回到中性状态。 */
	void RestoreUnyieldingRootsState(bool spent, float remainingSeconds);
	/** 当前是否能被僵尸选为啃食目标；睡莲用它实现种下后的短暂无咬保护。 */
	virtual bool CanBeEaten() const {
		return !mIsSquished && mBungeeState == PlantBungeeState::NONE
			&& !IsIceSealed();
	}
	/** 是否能被蹦极僵尸选中并抱走；跨格重型植物可覆写为 false。 */
	virtual bool CanBeTargetedByBungee() const { return !IsIceSealed(); }
	/** 僵尸正式啃食命中时的植物侧反馈入口；默认植物不产生专属碎屑。 */
	virtual void OnZombieBite(const Vector&) {}
	/** 是否能在当前跳跃类别的判定节点阻拦僵尸；高坚果等阻拦植物覆写此接口。 */
	virtual bool BlocksZombieJump(ZombieJumpType) const { return false; }
	/** 远程索敌的高度层许可；默认植物只锁定可被地面弹丸命中的僵尸。 */
	virtual bool CanAcquireZombie(const Zombie* zombie) const;
	/** 跳跃确实被本植物阻拦后的音画反馈入口；由跳跃状态机保证每次只调用一次。 */
	virtual void OnZombieJumpBlocked(ZombieJumpType) {}
	/** 是否在强/超强台风的逐格结算中锚定整个植物格；默认植物会随阵风移动。 */
	virtual bool AnchorsPlantCellAgainstTyphoon() const { return false; }
	/** 是否占据屋顶承载层并能托起普通植物；花盆及其承载层升级覆写。 */
	virtual bool IsRoofSupportPlant() const { return false; }
	/** 是否能保护指定逻辑格免受蹦极、篮球等来自上方的威胁。 */
	virtual bool ProtectsCellFromAirborneThreat(int, int) const { return false; }
	/** 请求启动或复用当前空中防御动作；默认植物不响应。 */
	virtual AirborneDefenseState ActivateAirborneDefense() {
		return AirborneDefenseState::INACTIVE;
	}
	/** 返回当前空中防御阶段，供威胁结算与 AutoTest 共用。 */
	virtual AirborneDefenseState GetAirborneDefenseState() const {
		return AirborneDefenseState::INACTIVE;
	}
	/** 返回防御动作展开前的剩余等待时间，单位：秒。 */
	virtual float GetAirborneDefenseActivationTime() const { return 0.0f; }
	/**
	 * 一个相邻植物格被本植物直接挡下一格时的结算入口。
	 * showFeedback 在同一阵风首次撞击时为 true，供品种合并同帧音画而不合并逐格伤害。
	 */
	virtual void OnTyphoonPlantImpact(bool showFeedback) {}
	/** 当前是否能响应冬季地面冲击；默认植物不具备锚定能力。 */
	virtual bool IsWinterGroundAnchorReady() const { return false; }
	/** 是否已耗尽冬季锚定能力；可重复锚定或无此能力的植物保持 false。 */
	virtual bool HasSpentWinterGroundAnchor() const { return false; }
	/**
	 * 原子解析一次冬季地面冲击并返回传播/散射语义。
	 * 调用方继续拥有自身伤害、动作提交、音画和乘员落点。
	 */
	virtual WinterGroundImpactResponse ResolveWinterGroundImpact(
		WinterGroundImpactKind) { return {}; }
	/**
	 * 结算冬季地面冲击伤害；默认复用正式承伤链，悬浮覆盖层可按冲击类别声明窄例外。
	 */
	virtual void TakeWinterGroundImpactDamage(WinterGroundImpactKind,
		int damage, DamageSource source) { TakeDamage(damage, source); }
	/** 本轮寒潮预报被敌方干扰时，清除依赖预报保留的准备状态。 */
	virtual void OnColdWaveForecastDisrupted() {}
	/**
	 * 请求本植物消费自身资源，阻止 target 建立一次冰像封存关系。
	 * 默认无能力；炉芯花只响应目标与自身状态，不得识别处刑者类型。
	 */
	virtual bool TryPreventIceExecutionSealFor(Plant*) { return false; }
	virtual void SaveExtraData(nlohmann::json& j) const {}
	virtual void LoadExtraData(const nlohmann::json& j) {}
	/** 界碑领域查询；普通植物不覆盖任何非连续入场终点。 */
	virtual bool CoversBoundaryEntryCell(int, int) const { return false; }
	/** 原子消费一枚界碑碎片；停机、无碎片或非界碑植物均返回 false。 */
	virtual bool TryConsumeBoundaryShard() { return false; }
	/** 立即退出更新、碰撞与绘制，并登记到下一帧安全销毁。 */
	virtual void Die();
	/**
	 * 结算巨人锤击对本植物的命中；默认进入通用压扁残影。
	 * 正在执行一次性能力的植物可覆写为立即结算或忽略，但同格其他层仍独立接收锤击。
	 */
	virtual void ResolveGargantuarSmash();
	/**
	 * 把植物变为原版压扁残影：冻结当前位置和动画、释放占格，并在渐隐后销毁。
	 * 冰车、投篮车及普通巨人锤击反应通过各自结算入口调用本函数。
	 */
	void Squish();
	bool IsSquished() const { return mIsSquished; }
	/** 当前实体是否仍拥有原始逻辑格；离地攻击体和压扁残影返回 false，供存档恢复排序。 */
	virtual bool OccupiesGridSlot() const { return IsActive() && !mIsSquished; }
	float GetSquishTimeRemaining() const { return mSquishTimer; }
	float GetSquishRenderScaleY() const {
		return mAnimator ? mAnimator->GetRenderScaleY() : 1.0f;
	}
	Vector GetSquishVisualPosition() const { return mSquishVisualPosition; }
	/** 由 GameInfoSaver 在派生类额外数据恢复后重建压扁终态。 */
	void RestoreSquishState(float remainingSeconds, const Vector& visualPosition);
	Vector GetPosition() const;
	/**
	 * 返回不含品种静态 offset 的公共视觉锚点。
	 * 阵风换格与水面浮动都在此收口，供本体、阴影和其他附属视觉保持同步。
	 */
	Vector GetVisualAnchorPosition() const;
	/** 返回阵风换格的瞬态视觉偏移，供随格附件与植物保持完全同步。 */
	Vector GetGridMoveVisualOffset() const { return mGridMoveVisualOffset; }
	/** 返回 gamedata 配置的品种静态视觉偏移，不包含任何逐帧动态量。 */
	Vector GetStaticVisualOffset() const { return mVisualOffset; }
	/** 为模仿者变身后的目标启用原版泛白滤镜；状态进入通用植物存档。 */
	virtual void SetImitatedAppearance(bool imitated = true);
	bool IsImitated() const { return mIsImitated; }
	void SetPosition(const Vector& position);
	/**
	 * 立即把逻辑格与碰撞箱切到目标格，再用纯视觉偏移平滑追赶。
	 * 视觉偏移不入存档；滑动中读档会稳定落在已经结算的目标格。
	 */
	void MoveToGridCell(int row, int column, float visualDuration);
	/** 由蹦极僵尸建立抓取关系；成功后暂停行为并关闭碰撞。 */
	bool BeginBungeeGrab(int zombieID);
	/** 把已抓植物切到升空态；只有当前抓取者能推进关系。 */
	bool BeginBungeeLift(int zombieID);
	/** 抓取者落地阶段死亡时恢复植物行动和碰撞。 */
	void CancelBungeeGrab(int zombieID);
	/** 更新升空视觉偏移；逻辑格在真正偷走前保持不变。 */
	void SetBungeeVisualOffset(int zombieID, const Vector& offset);
	/** 供蹦极僵尸在本体与前臂之间绘制升空植物。 */
	void DrawAsBungeeCargo(Graphics* g);
	PlantBungeeState GetBungeeState() const { return mBungeeState; }
	int GetBungeeOwnerZombieID() const { return mBungeeOwnerZombieID; }
	bool IsBungeeTargeted() const {
		return mBungeeState != PlantBungeeState::NONE;
	}
	/** 建立来源绑定的冰像封存；已有其他来源时拒绝抢占。 */
	bool BeginIceSeal(int ownerZombieID);
	/** 仅允许关系拥有者释放冰像；读取损坏档时可由 Board 传入保存的拥有者。 */
	bool ReleaseIceSeal(int ownerZombieID);
	/** 仅关系拥有者可结算一锤普通伤害；其他伤害入口在封存期间全部无效。 */
	bool TakeIceExecutionDamage(int ownerZombieID, int damage);
	/** 仅关系拥有者可完成处决；成功后实体按通用死亡链移除。 */
	bool ResolveIceExecution(int ownerZombieID);
	bool IsIceSealed() const { return mIceSealOwnerZombieID != NULL_ZOMBIE_ID; }
	int GetIceSealOwnerZombieID() const { return mIceSealOwnerZombieID; }
	/** 存档恢复专用：先重建权威状态，待所有僵尸加载后再由 Board 终检双向关系。 */
	void RestoreIceSeal(int ownerZombieID);

	// 获取睡觉状态
	bool GetSleepState() const { return this->mIsSleeping; }
	bool HasSleepIndicator() const { return mSleepIndicatorAnimator != nullptr; }
	/** 返回 Z.reanim 的世界基点；以公共视觉锚点表达，跟随水面、花盆、台风和蹦极位移。 */
	Vector GetSleepIndicatorPosition() const;
	float GetWakeUpTimeRemaining() const { return mWakeUpTimer; }
	bool IsWakingUp() const { return mWakeUpTimer > 0.0f; }
	/**
	 * @brief 对植物施加通用停机；重复施加只保留更长的剩余时间。
	 *
	 * 停机会冻结 Animator 帧事件和 PlantUpdate，但不改变睡眠、占格或生命状态。
	 * @return 当前实体接受停机时返回 true。
	 */
	bool ApplyShutdown(float durationSeconds);
	/** 返回通用计时停机或当前区域环境停机的合并结果。 */
	bool IsShutdown() const;
	float GetShutdownTimeRemaining() const { return mShutdownTimer; }
	/** 品种可覆写通用停机免疫；默认所有正式存活植物都可停机。 */
	virtual bool CanBeShutdown() const { return true; }
	/** 当前植物是否能替指定植物导走本次黑夜屋顶雷荷；默认植物没有接地能力。 */
	virtual bool CanGroundNightRoofChargeFor(const Plant*) const { return false; }
	/** 承载植物是否让同格指定上层免于本次黑夜屋顶停机；默认承载物不保护。 */
	virtual bool ProtectsSupportedPlantFromNightRoofCharge(const Plant*) const { return false; }
	/** 承载植物是否让同格指定上层免于劫持者处决；默认承载物不保护。 */
	virtual bool ProtectsSupportedPlantFromNightRoofHijacker(const Plant*) const { return false; }
	/** 本次放电对同排普通地面僵尸的伤害倍率；Board 在结算边沿取同排最大值。 */
	virtual float GetNightRoofChargeZombieDamageMultiplier() const { return 1.0f; }
	/** 本次保护或增伤确实生效后的表现入口；不得在动画帧事件里承载玩法结算。 */
	virtual void OnNightRoofChargeProtectionTriggered() {}
	/** 当前植物是否让指定僵尸失去本次雷荷承接/过载能力；默认植物不干预僵尸。 */
	virtual bool SuppressesNightRoofChargeProtectionFor(const Zombie*) const { return false; }
	/** 返回条件能力在轻量推演中的当前剩余冷却；无此类能力的植物保持零。 */
	virtual float GetSimulationAbilityCooldownRemaining() const { return 0.0f; }
	/** 将静态画像换成当前成长阶段的等效火力；调用方另外应用睡眠/停机和攻速。 */
	virtual float GetSimulationAttackDps(float profileDps) const { return profileDps; }
	/** 返回冰像处刑者完成本植物处决所需的已提交锤击数；普通植物默认三锤。 */
	virtual int GetIceExecutionRequiredStrikeCount() const { return 3; }
	/** 完成一次冻结快照保护后的品种反噬入口；onWetSlope 取接地植物自身所在瓦面。 */
	virtual void AbsorbGroundedNightRoofCharge(bool) {}
	/** 存档恢复专用：钳制并还原剩余停机时间，不重放来源效果。 */
	void RestoreShutdown(float remainingSeconds);

	// 是否为预览植物（选卡预览用，不参与对战逻辑）
	bool IsPreview() const { return this->mIsPreview; }

	/** 切换睡眠权威状态并同步原版 Z 标识；蘑菇覆写此入口以切换本体睡眠/清醒动画。 */
	virtual void SetSleepState(bool sleep);
	/** 咖啡豆请求开始原版 1 秒唤醒流程；重复请求或非睡眠植物返回 false。 */
	bool BeginWakeUp(float durationSeconds = 1.0f);
	/** 存档恢复专用：只还原权威状态与表现，不播放唤醒音效或重新触发品种行为。 */
	virtual void RestoreSleepState(bool sleep, float wakeUpTimeRemaining);

protected:
	friend class Board;
	bool mIsImitated = false; // 变身后目标的持久视觉身份；不改变实际 PlantType
	/** 推进阵风换格的纯视觉插值；暂停时 DeltaTime 为 0，逻辑占格不受影响。 */
	void UpdateGridMoveVisual();
	/** 推进压扁残影的保留与渐隐计时，到期后销毁。 */
	void UpdateSquish();
	/** 推进咖啡豆唤醒倒计时、原版纵向弹性表现及两个音效/状态边界。 */
	void UpdateWakeUp();
	/** 返回是否应冻结本帧植物动画与行为；通用停机和环境冲刷共用。 */
	bool IsActionPaused() const;
	/** 按当前唤醒倒计时重建蘑菇纵向弹性表现；读档与逐帧更新共用。 */
	void ApplyWakeUpPresentation();
	/** 按权威睡眠状态创建或移除独立 Z Animator；读档只重建表现，不保存随机相位。 */
	void SyncSleepIndicator();
	/** 在植物本体之后绘制 Z，保留原版 renderOrder+2 的前景语义。 */
	void DrawSleepIndicator(Graphics* g);
	/** 在植物本体前景绘制无碰撞的半透明冰像外壳。 */
	void DrawIceSeal(Graphics* g);
	/** 统一施加压扁态的暂停、碰撞、影子、占格和透明度表现。 */
	void ApplySquishedPresentation();
	/** 仅在格子仍指向自身 ID 时释放所属占格层，避免误清后来种下的植物。 */
	void ReleaseGridSlot();
	/** Board 原子替换实体后回收旧对象；格位和同 ID 注册关系已移交，禁止再次释放。 */
	void RetireAfterReplacement();
	/** 雨势对正向植物行动的倍率；不包含生存攻速词条。 */
	float GetWeatherActionSpeedMultiplier() const;
	/** 仅供攻击/生产/成长/恢复计时使用，禁止替代整个 Plant::Update 的 deltaTime。 */
	float GetWeatherActionDeltaTime() const;
	/** 产光专用计时增量 = 雨势行动倍率 × 路灯花局部照明倍率。 */
	float GetSunProductionDeltaTime() const;
	/** 攻击专用组合倍率 = 生存攻速词条 × 雨势行动倍率。 */
	float GetAttackSpeedMultiplier() const;
	/** 返回血量文字相对公共视觉锚点的偏移；叠层品种可覆写以避免文字重叠。 */
	virtual Vector GetHealthTextOffset() const { return Vector(-21.0f, -11.0f); }

	// 注意： 需要判断mIsPreview，所有植物都执行
	virtual void SetupPlant();
};

#endif
