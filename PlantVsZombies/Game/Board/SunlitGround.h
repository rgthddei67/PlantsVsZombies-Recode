#pragma once

#include <array>

namespace SunlitGround {
	inline constexpr float ProductionSeconds = 30.0f; // 每格空置一轮所需游戏秒数，首轮同样从零开始
	inline constexpr int FirstLevel = 3; // 向阳空地从 1-3 开始教学
	inline constexpr int FinalLevel = 9; // 仅第一大关固定冒险关启用
	inline constexpr int FrontlineWave = 11; // 1-9 从第 11 波加入撑杆与普通铁桶
	inline constexpr int FinalPressureWave = 21; // 1-9 从第 21 波加入快速铁桶

	struct Plot {
		int row = -1;
		int column = -1;
		float elapsed = 0.0f;
	};

	// 零基格位；固定中前场空地保留左侧基础展开空间，每关位置不随波次或读档重抽。
	inline constexpr std::array<std::array<Plot, 4>, 7> Layouts = {{
		{{{1, 3}, {3, 4}, {}, {}}},
		{{{0, 4}, {4, 3}, {}, {}}},
		{{{0, 3}, {2, 4}, {4, 3}, {}}},
		{{{1, 4}, {2, 3}, {3, 5}, {}}},
		{{{0, 4}, {1, 3}, {3, 4}, {4, 5}}},
		{{{0, 5}, {1, 4}, {3, 3}, {4, 4}}},
		{{{0, 4}, {1, 3}, {3, 5}, {4, 4}}},
	}};
}
