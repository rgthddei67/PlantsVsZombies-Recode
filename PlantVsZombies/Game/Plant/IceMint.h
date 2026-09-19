#pragma once

#include "Plant.h"
#include "Game/Board/IceProduction.h"

/** 冰晶薄荷：冷藏站提供小额长期冰收入，其他地图缓慢生产阳光。 */
class IceMint final : public Plant {
public:
	using Plant::Plant;
	/** 按游戏时间结算冰块或普通阳光，公共生命周期负责停机门禁。 */
	void PlantUpdate() override;
	/** 保存剩余生产时间与已完成批次，不把未结算收入提前入账。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复并校验生产状态，不重新生产或重置成熟度。 */
	void LoadExtraData(const nlohmann::json& j) override;
	float GetProductionRemaining() const { return mProductionRemaining; }
	int GetProductionBatches() const { return mProductionBatches; }
protected:
	/** 初始化完整首轮等待并把冰晶挂到现有花心运动轨道。 */
	void SetupPlant() override;
private:
	float mProductionRemaining = IceProduction::MintInterval;
	int mProductionBatches = 0;
};
