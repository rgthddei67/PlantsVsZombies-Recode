#pragma once

class Board;

enum class ZombiePartMotion { Head, Arm, Armor, LightArmor };

/** 保存离体时的逻辑行。每步按部件当前 X 重新查询该行的地表。 */
struct ZombiePartTerrain {
	const Board* board = nullptr;
	int row = -1;
	ZombiePartMotion motion = ZombiePartMotion::Armor;
};
