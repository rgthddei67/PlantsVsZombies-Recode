#pragma once

#include "Game/Board/MineGrid.h"
#include <vector>

/** 矿道选墙的确定性数值副本；不访问实体，不生成未来兵力，不消费游戏随机数。 */
namespace MineExcavationTactics {
enum class AttackShape { LINE = 0, LOCAL = 2, ECHO = 3 };
struct Plant {
	int cell = 0;
	int layer = 1;
	float health = 0;
	float value = 0;
	float dps = 0;
	float range = 9;
	int rowRadius = 0;
	AttackShape shape = AttackShape::LINE;
	bool multiTarget = false;
	float slowUptime = 0;
	float shutdown = 0;
};
struct Zombie {
	int id = 0;
	float row = 0, column = 0;
	int target = -1; // 保留已承诺的路段，开墙不能把在途单位瞬移到新路
	float health = 0;
	float speed = 0; // 格/游戏秒
	float dps = 0;
	float smashSeconds = 0; // >0 时按目标剩余耐久折算一次锤击周期
	float stun = 0, slow = 0, prism = 0;
};
struct Snapshot {
	MineGrid grid;
	std::vector<Plant> plants;
	std::vector<Zombie> zombies; // 工兵固定在首位；Board 采集附近局部攻势，所有防守植物均保留
	std::array<bool, MineGrid::Count> excluded{};
	int start = 0;
	float approachMultiplier = 3;
	float workSeconds = 4;
	float fogReduction = 0;
	float fogRemaining = 0;
	int fogFirstColumn = 9;
};
struct Result {
	int wall = -1, stand = -1;
	int candidates = 0;
	float baseline = 0, gain = 0;
};
constexpr int MaxZombies = 32;
constexpr int MaxPlants = MineGrid::Count * 4;
/** 比较真实施工后的短期攻势与不施工基线；无正收益时返回 wall=-1，允许稍后重试。 */
Result Choose(const Snapshot& snapshot);
}
