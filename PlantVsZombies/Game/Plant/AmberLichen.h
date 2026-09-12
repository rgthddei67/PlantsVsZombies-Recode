#pragma once
#include "Plant.h"

/** 琥珀地衣：通过连通矿道提供持续地面减速，范围随搬运和施工即时变化。 */
class AmberLichen final : public Plant {
public:
	using Plant::Plant;
	float GetGroundSlowFactorAtCell(int row, int column) const override;
	void Draw(Graphics* g) override;
	/** 预览、黏液绘制和正式减速共用地形范围。 */
	static bool CoversCell(const Board& board, int sourceRow, int sourceColumn, int row, int column);
protected:
	void SetupPlant() override;
};
