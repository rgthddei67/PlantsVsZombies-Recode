#pragma once

#include <array>
#include <cstdint>

/** 幽晶矿场的纯地形数据；反向最短路只包含左/上/下合法边，不把植物计为障碍。 */
class MineGrid {
public:
	static constexpr int Rows = 5;
	static constexpr int Columns = 9;
	static constexpr int Count = Rows * Columns;
	static constexpr int Unreachable = 1000;
	std::array<bool, Count> rock{};
	std::array<int, Count> distance{};
	std::array<int, Count> exitDistance{};
	std::array<bool, Count> connected{};
	std::array<bool, Rows> entrance{};

	static bool Valid(int row, int col) { return row >= 0 && row < Rows && col >= 0 && col < Columns; }
	static int Index(int row, int col) { return row * Columns + col; }
	bool IsRock(int row, int col) const { return Valid(row, col) && rock[Index(row, col)]; }
	/** 前四组每两关共用布局；组4为9-9三入口汇流、双出口分流的收官布局。 */
	void Initialize(int layoutGroup = 0);
	/** 地形提交后重建房屋连通性与有向距离；固定数组队列，无每帧分配。 */
	void Rebuild();
	bool CanExcavate(int row, int col) const;
	/** 返回严格更近的相邻格下标，或-1；并列左优先、上下按稳定偏好。 */
	int Next(int cell, bool preferDown) const;
	/** 魅惑单位沿四邻接矿道返回任一右侧入口；并列优先向右，距离严格下降。 */
	int NextExit(int cell, bool preferDown) const;
	/** 检查路径图所有可达格的下降性质，供存档与自动验收使用。 */
	bool Validate() const;
	/** 在左侧防守区之外沿已连通四邻接矿道求施工距离，防止绕行穿墙或折返。 */
	std::array<int, Count> WorkDistances(int start) const;
	/** 选择净节省格数最多的单墙方案；并列按左上下来向、右向及稳定格序。 */
	bool FindExcavation(int start, const std::array<bool, Count>& excluded, int& wall, int& stand) const;
	/** 返回通往已锁定施工点的严格下降相邻节点，或 -1。 */
	int NextWork(int start, int stand) const;
};
