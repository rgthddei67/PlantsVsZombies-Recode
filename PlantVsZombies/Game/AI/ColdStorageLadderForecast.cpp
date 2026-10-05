#include "ColdStorageLadderForecast.h"
#include "ColdStorageSearch.h"
#include <algorithm>
#include <cmath>

namespace ColdStorageSearch {
namespace {
/** 世界梯按格共享；同格南瓜和宿主不能分别收取一次攀爬延迟。 */
bool HasLadder(const std::vector<LadderRules::Cell>& ladders,int row,int column) {
    return std::any_of(ladders.begin(),ladders.end(),[&](const auto& cell){return cell.row==row && cell.column==column;});
}
/** 放置完成重新取同格合法层，不把原南瓜的死亡误当仍存坚果也失效。 */
bool SupportsPlacement(const std::vector<Plant>& plants,int row,int column) {
    return std::any_of(plants.begin(),plants.end(),[&](const Plant& plant) {
        return plant.health>0 && plant.row==row && plant.column==column && plant.ladderTarget;
    });
}
/** 成功卸梯或装备破坏后使用普通稳态速度；已提交世界梯不随来源卸装消失。 */
void Unload(Unit& unit) {
    auto& builder=unit.ladder;
    builder.phase=LadderRules::Builder::Phase::NORMAL;
    builder.row=builder.column=-1; builder.remaining=0;
    unit.body.speed=builder.normalSpeed;
}
/** 单格提交不产生击杀、收入或伤害；后续各来源只共享地形。 */
void BeginClimb(Unit& unit,int column,ConstructionStats& stats) {
    auto& climb=unit.ladderClimb;
    if(climb.phase==LadderRules::Climb::Phase::NONE && climb.usedColumn!=column) {
        climb.phase=LadderRules::Climb::Phase::CLIMBING;
        climb.usedColumn=column;
        ++stats.ladderClimbs;
    }
}
}

void PruneDeadLadderHosts(const std::vector<Plant>& plants,std::vector<unsigned char>& living,
    std::vector<LadderRules::Cell>& ladders,ConstructionStats& stats) {
    if(living.size()<plants.size()) {
        const size_t first=living.size(); living.resize(plants.size());
        for(size_t index=first;index<plants.size();++index) living[index]=plants[index].health>0;
    }
    for(size_t index=0;index<plants.size();++index) if(living[index] && plants[index].health<=0) {
        const auto& plant=plants[index];
        const auto previous=ladders.size();
        ladders.erase(std::remove_if(ladders.begin(),ladders.end(),[&](const auto& cell) {
            return cell.row==plant.row && cell.column==plant.column;
        }),ladders.end());
        stats.ladderRemoved+=static_cast<int>(previous-ladders.size());
        living[index]=0;
    }
}

void ClearForecastLadders(const Snapshot& snapshot,float x,int row,int radius,bool entireRow,
    std::vector<LadderRules::Cell>& ladders,ConstructionStats& stats) {
    if((radius<0 && !entireRow) || row<0 || row>=snapshot.rows || snapshot.columns<=0 || snapshot.cellWidth<=0) return;
    const int column=std::clamp(static_cast<int>(std::floor((x-snapshot.gridLeft)/snapshot.cellWidth)),0,snapshot.columns-1);
    const auto previous=ladders.size();
    ladders.erase(std::remove_if(ladders.begin(),ladders.end(),[&](const auto& cell) {
        return entireRow ? cell.row==row : std::abs(cell.row-row)<=radius && std::abs(cell.column-column)<=radius;
    }),ladders.end());
    stats.ladderRemoved+=static_cast<int>(previous-ladders.size());
}

bool ExtractForecastLadder(const Plant& magnet,std::vector<LadderRules::Cell>& ladders,ConstructionStats& stats) {
    auto nearest=ladders.end(); float score=0;
    for(auto at=ladders.begin();at!=ladders.end();++at) {
        const int row=std::abs(at->row-magnet.row),column=std::abs(at->column-magnet.column);
        const int distance=std::max(row,column);
        const float rank=distance+row*.05f;
        if(distance<=LadderRules::MagnetCells && (nearest==ladders.end() || rank<score)) {nearest=at;score=rank;}
    }
    if(nearest==ladders.end()) return false;
    ladders.erase(nearest); ++stats.ladderRemoved; ++stats.magneticExtractions;
    return true;
}

bool AdvanceLadderAction(const Snapshot&,float time,Unit& unit,int& contact,
    const std::vector<Plant>& plants,std::vector<LadderRules::Cell>& ladders,
    float active,float climbSeconds,float animationRate,float motionRate,bool movingRight,float contactDistance,
    ConstructionStats& stats) {
    using BuildPhase=LadderRules::Builder::Phase;
    using ClimbPhase=LadderRules::Climb::Phase;
    if(unit.body.health<=0 || unit.body.spawnAt>time) return false;
    auto& builder=unit.ladder; auto& climb=unit.ladderClimb;
    const bool mayClimb=climb.eligible && !unit.catapult.present && !unit.instantVehicleCrush
        && !unit.digger.present && (!unit.balloon.present || unit.balloon.phase==BalloonRules::Phase::WALKING);
    // 真正丢装后不能因核心回血自动恢复携梯阶段；放置中掉头只撤销动作，护盾仍按真实层保留。
    if(builder.present && builder.phase!=BuildPhase::NORMAL && unit.shieldHealth<=0) Unload(unit);
    const bool head=builder.canPlace && unit.body.health-unit.helmHealth-unit.shieldHealth>builder.stopHealth;
    if(builder.present && builder.phase==BuildPhase::PLACING) {
        if(!head || !SupportsPlacement(plants,builder.row,builder.column) || HasLadder(ladders,builder.row,builder.column)) {
            builder.phase=unit.shieldHealth>0 ? BuildPhase::CARRYING : BuildPhase::NORMAL;
            builder.row=builder.column=-1; builder.remaining=0;
            return false;
        }
        builder.remaining=std::max(0.0f,builder.remaining-active*animationRate);
        if(builder.remaining>0 || active<=0) return true;
        const int column=builder.column;
        ladders.push_back({builder.row,column}); ++stats.ladderPlaced;
        if(mayClimb) BeginClimb(unit,column,stats);
        if(builder.retain) {
            builder.phase=BuildPhase::CARRYING; builder.row=builder.column=-1;
        } else {
            unit.body.health-=unit.shieldHealth;
            unit.shieldHealth=unit.maximumShield=0;
            unit.magneticLayer=0; Unload(unit);
        }
        return true;
    }
    if(climb.phase!=ClimbPhase::NONE) {
        const bool lostSupport=climb.phase==ClimbPhase::CLIMBING && !HasLadder(ladders,unit.body.row,climb.usedColumn);
        if(lostSupport)
            climb.phase=ClimbPhase::FALLING;
        float remaining=lostSupport ? 0 : std::max(0.0f,climbSeconds),extra=0;
        if(climb.phase==ClimbPhase::CLIMBING) {
            const float ascent=std::min(remaining,std::max(0.0f,LadderRules::Height-climb.altitude)/LadderRules::ClimbSpeed);
            climb.altitude=std::min(LadderRules::Height,climb.altitude+ascent*LadderRules::ClimbSpeed);
            extra=climb.horizontalBoost*ascent; remaining-=ascent;
            if(climb.altitude>=LadderRules::Height) climb.phase=ClimbPhase::FALLING;
        }
        if(climb.phase==ClimbPhase::FALLING) {
            climb.altitude=std::max(0.0f,climb.altitude-remaining*LadderRules::FallSpeed);
            if(climb.altitude<=0) climb.phase=ClimbPhase::NONE;
        }
        unit.body.x+=(movingRight ? 1 : -1)*(unit.body.speed*motionRate*active+extra);
        contact=-1;
        return true;
    }
    if(contact<0 || contact>=static_cast<int>(plants.size()) || active<=0) return false;
    const auto& plant=plants[contact];
    const bool touching=movingRight ? unit.body.x>=plant.x-contactDistance : unit.body.x<=plant.x+contactDistance;
    if(!touching) return false;
    if(mayClimb && HasLadder(ladders,plant.row,plant.column)) {
        BeginClimb(unit,plant.column,stats);
        contact=-1;
        return false; // 本步仍按通常速度接敌；下一步兑现升降及额外横移，不预支攀爬。
    }
    if(builder.present && builder.phase==BuildPhase::CARRYING && head && unit.shieldHealth>0 && plant.ladderTarget) {
        builder.phase=BuildPhase::PLACING; builder.row=plant.row; builder.column=plant.column;
        builder.remaining=builder.placementSeconds;
        return true;
    }
    return false;
}
}
