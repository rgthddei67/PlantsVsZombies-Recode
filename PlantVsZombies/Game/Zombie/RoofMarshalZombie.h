#pragma once

#include "Zombie.h"

#include <array>

/**
 * @brief 5-9 屋脊督军；复用普通僵尸骨架，提供首领抗性、指挥召唤、阶段移动与改天技能。
 */
class RoofMarshalZombie : public Zombie {
public:
	/** 读取本品种当前运动阶段的稳态速度；后续阶段转换仍由能力时间线近似。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class CommandPhase {
		ADVANCING,
		COMMANDING,
	};

	/** @brief 让基类推进通用状态，并在啃食早退后继续执行首领指挥逻辑。 */
	void Update() override;
	/** @brief 推进召唤冷却与指挥姿势；冻结、黄油和水草束缚仍会暂停。 */
	void ZombieUpdate(float scaledTime) override;
	/** @brief 在高血量换行演出期间叠加独立纵向视觉补偿，不污染逻辑行和通用素材偏移。 */
	Vector GetVisualPosition() const override;
	/** @brief 指挥姿势期间拒绝开吃，姿势结束后碰撞系统会自然重试。 */
	void StartEat(ColliderComponent* other) override;
	/** @brief 取消普通僵尸的残血掉头临界值，仅在本体生命归零时触发掉头死亡。 */
	void TakeBodyDamage(int damage) override;

	/** @brief 将所有植物灰烬伤害限制为首领单次伤害上限，土豆雷也不能绕过耐久。 */
	void TakePlantAshDamage(int damage) override;
	/** @brief 大嘴花不能吞下首领；攻击者随后按统一规则结算小额咬伤。 */
	bool TakePlantInstantKill() override;
	/** @brief 首领始终走本体受伤与常规死亡表现，不生成普通僵尸烧焦残影。 */
	bool CanBeCharred() const override { return false; }
	/** @brief 首领不接受魅惑，魅惑菇仍按通用规则被吃掉。 */
	bool CanBeCharmed() const override { return false; }
	/** @brief 黄油只定身 1.25 秒，期间不刷新；解除后进入 5 秒黄油免疫。 */
	bool ApplyButter() override;
	/** @brief 缠绕水草只能限时束缚首领，不能把它拖入水下处决。 */
	bool ResistsTangleKelpDrowning() const override { return true; }
	/** @brief 小推车仍会被触发并消耗，但不能借此跳过首领战。 */
	bool CanBeKilledByMower() const override { return false; }

	/** @brief 保存会影响后续召唤时序与批次诊断的全部派生状态。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** @brief 恢复并钳制指挥状态，损坏档不会造成永久停步或无穷连召。 */
	void LoadExtraData(const nlohmann::json& j) override;

	CommandPhase GetCommandPhase() const { return mCommandPhase; }
	const char* GetCommandPhaseName() const;
	float GetSummonTimer() const { return mSummonTimer; }
	float GetCommandPoseTimer() const { return mCommandPoseTimer; }
	int GetCommandCount() const { return mCommandCount; }
	int GetLastSummonCount() const { return mLastSummonCount; }
	int GetLastSummonRowMask() const { return mLastSummonRowMask; }
	int GetLastSummonDistinctRowCount() const;
	bool IsHighThreatPoolUnlocked() const;
	/** @brief 返回当前生命对应的高威胁池抽取概率，范围 0～100。 */
	int GetCurrentHighThreatRollPercent() const;
	int GetCurrentSummonCount() const;
	float GetCurrentSummonInterval() const;
	float GetLaneSwitchTimer() const { return mLaneSwitchTimer; }
	float GetLaneTransitionRemaining() const { return mLaneTransitionRemaining; }
	float GetLaneVisualOffsetY() const { return mLaneVisualOffsetY; }
	int GetLaneSwitchCount() const { return mLaneSwitchCount; }
	int GetAssaultCommandCount() const { return mAssaultCommandCount; }
	int GetLastAssaultRow() const { return mLastAssaultRow; }
	int GetLastAssaultAffectedCount() const { return mLastAssaultAffectedCount; }
	float GetButterImmunityTimer() const { return mButterImmunityTimer; }
	int GetLastSummonBossRow() const { return mLastSummonBossRow; }
	/** @brief 高威胁援军阶段的绝对生命分界，供首领 UI 与测试共用。 */
	int GetHighThreatHealthThreshold() const;
	/** @brief 狂暴行走阶段的绝对生命分界，供首领 UI 与测试共用。 */
	int GetDesperateHealthThreshold() const;
	bool IsWalkingPhase() const;
	const std::array<ZombieType, 4>& GetLastSummonedTypes() const {
		return mLastSummonedTypes;
	}
	/** @brief 判断类型是否属于不会随未来第六大关扩张的督军固定前五大关白名单。 */
	static bool IsAllowedSummonType(ZombieType type);
	static bool IsHighThreatSummonType(ZombieType type);

protected:
	void SetupZombie() override;
	/** @brief 指挥姿势期间停止自主移动，其余阶段复用普通僵尸移动。 */
	void ZombieMove(float scaledDelta, Transform* transform) override;
	/** @brief 高血量稳态使用 idle2；换行和 4000 以下阶段才播放普通走路。 */
	void PlayWalkAnimation(float blendTime) override;
	/** @brief 隐藏普通头部组并发射军帽与头一体的专属掉落粒子。 */
	void HeadDrop() override;
	/** @brief 换用军服断袖贴图，并复用普通断臂粒子和音效。 */
	void ArmDrop() override;
	/** @brief 读档恢复通用残肢后，重新覆盖督军专属断袖材质。 */
	void ZombieItemUpdate() const override;

private:
	/** @brief 立即生成一批固定白名单僵尸，并记录实际生成结果供存档与验收。 */
	void SummonCommandedZombies();
	/** @brief 根据当前生命阶段与目标行地形选择下一只前五大关僵尸。 */
	ZombieType RollSummonedZombieType(int row) const;
	/** @brief 每两批召唤选择兵力最集中的一行，短时强化其推进与啃食。 */
	void IssueAssaultCommand();
	/** @brief 召唤后进入 1.2 秒指挥姿势，但不延后下一次召唤冷却。 */
	void BeginCommandPose();
	/** @brief 从相邻合法行中选一行并开始独立的纵向换行演出。 */
	void BeginLaneSwitch();
	/** @brief 原子切到指定合法行，并以走路轨道完成纵向视觉过渡。 */
	bool BeginLaneSwitchTo(int destination);
	/** @brief 线性收敛换行视觉补偿；逻辑行在演出开始时已原子提交。 */
	void UpdateLaneTransition(float scaledTime);

	CommandPhase mCommandPhase = CommandPhase::ADVANCING;
	float mSummonTimer = 1.0f;
	float mCommandPoseTimer = 0.0f;
	float mLaneSwitchTimer = 6.0f;
	float mLaneTransitionRemaining = 0.0f;
	float mLaneVisualOffsetY = 0.0f;
	float mButterImmunityTimer = 0.0f;
	int mCommandCount = 0;
	int mLaneSwitchCount = 0;
	int mLastSummonCount = 0;
	int mLastSummonRowMask = 0;
	int mLastSummonBossRow = -1;
	int mAssaultCommandCount = 0;
	int mLastAssaultRow = -1;
	int mLastAssaultAffectedCount = 0;
	std::array<ZombieType, 4> mLastSummonedTypes{
		ZombieType::NUM_ZOMBIE_TYPES,
		ZombieType::NUM_ZOMBIE_TYPES,
		ZombieType::NUM_ZOMBIE_TYPES,
		ZombieType::NUM_ZOMBIE_TYPES,
	};
};
