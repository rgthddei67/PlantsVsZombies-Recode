#pragma once

#include "Zombie.h"

/** 经典小鬼：由巨人抛出，完成抛物线和落地演出后恢复普通行走与啃食。 */
class ImpZombie final : public Zombie {
public:
	using Zombie::Zombie;

	enum class Phase {
		WALKING,
		THROWN,
		LANDING,
	};

	/** 配置巨人脱手后的飞行，并在阵营提交后原样继承巨人的剩余减速秒数。 */
	void ConfigureThrown(float throwDistance, bool movingRight,
		float inheritedCooldown, bool inheritMindControl);
	/** 视觉离手位置校准后，沿原抛物线向投手一侧收短至合法矿道落点。 */
	void ConstrainMineLanding();

	void ZombieUpdate(float scaledTime) override;
	void StartEat(ColliderComponent* other) override;
	void HeadDrop() override;
	void ArmDrop() override;
	void ZombieItemUpdate() const override;
	void Charred() override;
	Vector GetVisualPosition() const override;
	bool IsFlying() const override { return mPhase == Phase::THROWN; }
	bool CanBeTargetedByProjectile(bool targetsFlying) const override;
	bool CanTriggerPotatoMine() const override { return mPhase == Phase::WALKING; }
	bool CanBeGrabbedByTangleKelp() const override { return mPhase == Phase::WALKING; }
	bool CanBeChilled() const override;
	bool CanBeFrozen() const override { return mPhase == Phase::WALKING; }
	bool CanBeCharmed() const override { return mPhase == Phase::WALKING; }
	bool CanBeCharred() const override {
		return mPhase == Phase::WALKING && Zombie::CanBeCharred();
	}

	Phase GetPhase() const { return mPhase; }
	float GetThrowAltitude() const { return mAltitude; }
	float GetThrowVerticalVelocity() const { return mVerticalVelocity; }
	float GetThrowHorizontalVelocity() const { return mHorizontalVelocity; }
	bool IsThrowMovingRight() const { return mThrowMovingRight; }
	/** 返回 anim_thrown 当前帧身体原点的实际渲染世界坐标。 */
	Vector GetThrowBodyRenderAnchor() const {
		return GetRenderedTrackWorldPosition("Zombie_imp_body1");
	}
	Vector GetHeadParticleAnchor() const { return GetTrackWorldPosition("anim_head1"); }
	Vector GetArmParticleAnchor() const {
		return GetTrackWorldPosition("Zombie_imp_outerarm_upper");
	}

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void PlayWalkAnimation(float blendTime) override;
	void OnStartEating() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	bool CanUseGroundPoolState() const override { return mPhase == Phase::WALKING; }
	bool ShouldPlayDeathAnimation() const override { return mPhase == Phase::WALKING; }

private:
	float mMineLandingX = 0.0f; // 已锁定矿道落点，世界像素
	bool mHasMineLanding = false;
	/** 飞行高度落至地面时关闭飞行并播放一次落地轨道。 */
	void BeginLanding();
	/** 落地轨结束后恢复地面碰撞、阴影与稳态走路。 */
	void FinishLanding();
	/** 按阶段恢复碰撞、阴影及断肢断头外观，供读档和运行时共用。 */
	void ApplyPhasePresentation() const;

	Phase mPhase = Phase::WALKING;
	float mAltitude = 0.0f;
	float mVerticalVelocity = 0.0f;
	float mHorizontalVelocity = 300.0f;
	bool mThrowMovingRight = false;
};
