#pragma once

#include "Plant.h"

/** 曙光莲：持续充能并受极夜红色仪表加速，满能后由玩家点击提交一次组合黎明。 */
class DawnLotus final : public Plant {
public:
	using Plant::Plant;

	/** 按游戏时间累积固定与危险仪表能量，封顶时提示就绪。 */
	void PlantUpdate() override;
	/** 在本体上方持续显示当前可点击释放的标志。 */
	void Draw(Graphics* g) override;
	/** 共享点击与就绪提示的资格，实时检查能量、行动状态和红色模块。 */
	bool IsReadyToActivate() const;
	bool TryActivate();
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetEnergy() const { return mEnergy; }
	bool IsFullyCharged() const { return mEnergy >= 60.0f; }

protected:
	void SetupPlant() override;

private:
	/** 读取本次可提交的极夜红色模块位；不缓存天气派生状态。 */
	int GetDangerMask() const;
	void ConfigureRig();
	void RefreshPresentation() const;

	float mEnergy = 0.0f;
	bool mRigConfigured = false;
};
