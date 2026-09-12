#pragma once

#include "Plant.h"

/** 曙光莲：持续充能并受极夜红色仪表加速，满能后由玩家点击提交一次组合黎明。 */
class DawnLotus final : public Plant {
public:
	using Plant::Plant;

	/** 按游戏时间累积固定与危险仪表能量，封顶时提示就绪。 */
	void PlantUpdate() override;
	bool TryActivate();
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetEnergy() const { return mEnergy; }
	bool IsFullyCharged() const { return mEnergy >= 60.0f; }

protected:
	void SetupPlant() override;

private:
	void ConfigureRig();
	void RefreshPresentation() const;

	float mEnergy = 0.0f;
	bool mRigConfigured = false;
};
