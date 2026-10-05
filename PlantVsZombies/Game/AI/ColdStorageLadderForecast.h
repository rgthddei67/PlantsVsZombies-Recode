#pragma once
#include "Game/Zombie/LadderRules.h"
#include <vector>

namespace ColdStorageSearch {
struct Snapshot; struct Unit; struct Plant; struct ConstructionStats;
/** 更新实际植物死亡造成的拆梯；活动梯独立于建造者，未完成动作不在世界集合里。 */
void PruneDeadLadderHosts(const std::vector<Plant>& plants,std::vector<unsigned char>& living,
    std::vector<LadderRules::Cell>& ladders,ConstructionStats& stats);
/** 正式范围灰烬的格范围/整行拆梯；与伤害是否打到僵尸无关。 */
void ClearForecastLadders(const Snapshot& snapshot,float x,int row,int radius,bool entireRow,
    std::vector<LadderRules::Cell>& ladders,ConstructionStats& stats);
/** 只有附近没有可吸僵尸装备时才调用；消费同一磁力菇充能周期。 */
bool ExtractForecastLadder(const Plant& magnet,std::vector<LadderRules::Cell>& ladders,ConstructionStats& stats);
/** 接敌后推进放梯或共享攀爬；返回true时本步已处理横移并禁止通用啃食。
 * active为未受硬控占用的游戏秒，climbSeconds另使用正式内部半速，animationRate不含风。
 * 垂直高度用于动作延迟，不重新构造三维碰撞；横移始终有界按本步实际速度推进。
 */
bool AdvanceLadderAction(const Snapshot& snapshot,float time,Unit& unit,int& contact,
    const std::vector<Plant>& plants,std::vector<LadderRules::Cell>& ladders,
    float active,float climbSeconds,float animationRate,float motionRate,bool movingRight,float contactDistance,
    ConstructionStats& stats);
}
