#pragma once

#include "Zombie.h"

/**
 * 泳池普通僵尸：复用基类通用入水与裁剪，只补充泳池 reanim 的
 * swim、泳圈贴图以及独立啃食/死亡帧事件。
 */
class PoolNormalZombie : public Zombie {
public:
	using Zombie::Zombie;

	void Update() override;
	void HeadDrop() override;
	void ArmDrop() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	void ZombieItemUpdate() const override;

protected:
	void SetupZombie() override;
	void RegisterFrameEvents() override;
	const char* GetDeathTrackName() const override;
	void PlayWalkAnimation(float blendTime = 0.0f) override;
	void OnStartEating() override;
	void OnStopEating() override;

private:
	/** 水中啃食轨道使用无涟漪泳圈时，覆盖回带水面接触纹理。 */
	void SetPoolEatingTubeVisual(bool active) const;
};
