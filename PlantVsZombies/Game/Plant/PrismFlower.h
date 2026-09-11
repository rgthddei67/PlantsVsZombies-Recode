#pragma once
#include "Plant.h"
#include <array>

/** 棱光花：沿真实视线标记局部高耐久目标；已提交标记由目标独立持有。 */
class PrismFlower final : public Plant {
public:
	using Plant::Plant;
	/** 就绪时按遮挡、未标记资格及当前耐久择优，成功后才消费周期。 */
	void PlantUpdate() override;
	void Draw(Graphics* g) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	float GetMarkCooldown() const { return mMarkCooldown; }
	int GetLastMarkCount() const { return mLastMarkCount; }
	/** 种植预览与正式选敌共用偏右、偏下的格范围。 */
	static bool CoversCell(int dr, int dc) { return dr >= -1 && dr <= 2 && dc >= -1 && dc <= 2; }
protected:
	void SetupPlant() override;
private:
	/** 仅更新晶瓣和花芯表现；不在绘制线程改变 Animator 状态。 */
	void RefreshBloom();
	float mMarkCooldown = 0.0f;
	float mBloomRemaining = 0.0f;
	int mLastMarkCount = 0;
	std::array<Vector, 2> mBeamEnds{};
};
