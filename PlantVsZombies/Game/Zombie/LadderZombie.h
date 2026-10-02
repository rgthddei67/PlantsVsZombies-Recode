#pragma once

#include "Zombie.h"
#include "../Ladder.h"

class Plant;

/**
 * @brief 经典扶梯僵尸：携梯高速前进，为坚果类放梯，失去扶梯后按普通僵尸行动。
 */
class LadderZombie : public Zombie {
public:
	/** 按当前装备/运动阶段读取实际稳态步速，吃饭或施法时不误用动作 clip。 */
	float GetMineSimulationMoveSpeed() const override;
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using Zombie::Zombie;

	enum class Phase {
		CARRYING,
		PLACING,
		NORMAL,
	};

	Phase GetPhase() const { return mPhase; }
	const char* GetPhaseName() const;
	ArmorBrokenState GetShieldStage() const { return mShieldStage; }
	/** 返回当前携梯实际使用的贴图键，供状态观测验证动态换色与损坏阶段。 */
	const std::string& GetCurrentLadderTextureKey() const {
		return GetShieldTextureKey(mShieldStage);
	}
	int GetPlacementRow() const { return mPlacementRow; }
	int GetPlacementColumn() const { return mPlacementColumn; }

	void StartEat(ColliderComponent* other) override;
	void ShieldDrop() override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void OnTemporalCoreStateRestored() override;
	bool HasMagneticItem() const override;
	bool ExtractMagneticItem(MagneticItem& item) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieUpdate(float scaledTime) override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void CheckShieldImage() override;
	/** 成功放梯后是否保留携梯护盾与继续放梯能力；经典扶梯只使用一次。 */
	virtual bool RetainsLadderAfterPlacement() const { return false; }
	/** 选择 Board 共享扶梯的外观样式。 */
	virtual LadderStyle GetPlacedLadderStyle() const { return LadderStyle::CLASSIC; }
	/** 选择当前损伤阶段的携梯贴图，供换色派生类保持受伤终态。 */
	virtual const std::string& GetShieldTextureKey(ArmorBrokenState stage) const;
	/** 选择断臂后残留上臂材质。 */
	virtual const std::string& GetBrokenArmTextureKey() const;
	/** 选择扶梯破盾时的掉落粒子效果。 */
	virtual const char* GetLadderDropEffectName() const { return "ZombieLadder"; }

private:
	/** 判断目标是否是当前 C# Ladder 攻击类型允许的坚果/高坚果/南瓜。 */
	bool IsLadderTarget(const Plant* plant) const;
	/** 在保存的格子重新取得当前合法放梯目标。 */
	Plant* ResolvePlacementTarget() const;
	/** 停止移动并开始一次性放梯动画。 */
	void BeginPlacement(Plant& plant);
	/** 中断未完成的放梯动作，释放移动锁并清理保存的目标格。 */
	void AbortPlacement(bool restoreWalkAnimation);
	/** 结束携梯护盾并恢复普通走路/啃食；可选择生成掉梯粒子。 */
	void DetachLadder(bool emitParticle);
	/** 按当前护盾阶段恢复扶梯贴图；供受击与读档共用。 */
	void ApplyShieldImage() const;
	/** 恢复断臂终态，避免读档或换轨后部件复活。 */
	void ApplyBrokenArmPresentation() const;
	/** 把 C# 每 tick 水平速度换算为当前 12 FPS 资源的 clip 倍率。 */
	static float WalkClipFromVelocity(float velocity);

	Phase mPhase = Phase::CARRYING;
	ArmorBrokenState mShieldStage = ArmorBrokenState::NO_BROKEN;
	int mPlacementRow = -1;
	int mPlacementColumn = -1;
	float mWalkVelocity = 0.8f;
};
