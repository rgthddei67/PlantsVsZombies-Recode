#pragma once
#include <array>
#include <vector>

namespace MineWaveFormation {
enum Role { REGULAR, FRONT, SUPPORT, EXCAVATOR };
struct Unit {
	int type = 0;
	int row = 0;
	int legalRows = 0;
	int role = REGULAR;
};
/** 只重排已经扣预算的队伍，不增减类型/数量；随机行序由 Board 在预报提交时一次提供。 */
void Arrange(std::vector<Unit>& units, const std::array<int,5>& rowOrder, bool concentrate);
/** 前排、普通/工兵、支援的出生纵深，单位格宽；不修改个体后续移动速度。 */
float RankOffset(int role);
}
