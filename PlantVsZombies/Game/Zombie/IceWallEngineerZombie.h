#pragma once

#include "ConeZombie.h"

/**
 * 冰墙工程师：完全进入冬日花园后就地部署可中断施工墙，完工后墙体独立向房屋推进。
 * 行走、啃食、断肢断头和死亡事件全部复用路障僵尸时间轴。
 */
class IceWallEngineerZombie final : public ConeZombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using ConeZombie::ConeZombie;

	enum class ConstructionPhase {
		MOVING,
		BUILDING,
		COMPLETED,
	};

	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	void ZombieItemUpdate() const override;
	void Update() override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;

	ConstructionPhase GetConstructionPhase() const { return mConstructionPhase; }
	float GetConstructionRemaining() const { return mConstructionRemaining; }
	float GetBuildWallCenterX() const { return mBuildWallCenterX; }
	bool HasUsedConstruction() const { return mConstructionUsed; }
	/** AutoTest 专用：直接进入指定施工状态，不提交冰墙。 */
	void SetConstructionStateForTesting(
		ConstructionPhase phase, float remaining, float wallCenterX, bool used);

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void ZombieUpdate(float scaledTime) override;
	void OnMindControlled() override;
	const std::string& GetConeTextureKey(ArmorBrokenState stage) const override;
	const char* GetConeDropEffectName() const override {
		return "IceWallEngineerHardhatOff";
	}

private:
	bool CanBeginConstruction() const;
	bool ShouldAbortConstruction() const;
	bool BeginConstruction(float wallCenterX);
	void CancelConstruction(bool consumeAbility);
	void CompleteConstruction();
	void ApplyEngineerEquipmentTextures() const;

	ConstructionPhase mConstructionPhase = ConstructionPhase::MOVING;
	float mConstructionRemaining = 0.0f;
	float mBuildWallCenterX = 0.0f;
	float mBuildParticleTimer = 0.0f;
	bool mConstructionUsed = false;
};
