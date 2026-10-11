#include "Game/Zombie/WeatherJammerRules.h"
#include "Game/Zombie/PolarClockRules.h"
#include "Game/Zombie/AuroraPriestRules.h"
#include "Game/Zombie/CrystalDrummerRules.h"
#include "ColdStorageFogFerryForecast.h"
#include "ColdStorageSearch.h"
#include "Game/Zombie/FogFerryRules.h"
#include "Game/Zombie/ThermalSniperRules.h"
#include <algorithm>

namespace ColdStorageSearch {
namespace {
/** 对齐 TeleportWithinRow 的目标自有动作中断；已释放效果由独立队列继续持有。 */
void InterruptPassenger(Unit& unit) {
    unit.body.eating=false;
    unit.ferry.transported=true;
    if(unit.jammer && unit.jammerChanneling) {
        unit.jammerChanneling=false;unit.jammerRemaining=WeatherJammerRules::Channel;unit.jammerReboot=WeatherJammerRules::Reboot;
    }
    if(unit.clock.winding) {unit.clock.winding=false;unit.clock.remaining=PolarClockRules::RetryWait;}
    if(unit.ritual.winding) {unit.ritual.winding=false;unit.ritual.remaining=AuroraPriestRules::Retry;}
    if(unit.sniper.aiming) {
        unit.sniper.aiming=false;unit.sniper.remaining=ThermalSniperRules::Reload;
        unit.sniper.targetID=0;unit.sniper.damage=0;
    }
    if(unit.drum.winding) {
        unit.drum.winding=false;unit.drum.remaining=CrystalDrummerRules::BeatInterval-CrystalDrummerRules::Windup;
    }
    if(unit.burst.stage==PaidBurst::Stage::WINDUP) {
        unit.burst.stage=PaidBurst::Stage::READY;unit.burst.remaining=0;unit.burst.retryRemaining=unit.burst.retry;
    }
}
}

void AdvanceFogFerries(const Snapshot& s,float time,float step,int fogLeft,
    const std::array<float,54>& illumination,std::vector<Unit>& units,ConstructionStats& stats) {
    std::vector<size_t> sources;
    for(size_t i=0;i<units.size();++i) {
        auto& f=units[i].ferry;f.blocksMovement=false;f.transported=false;
        if(f.present && units[i].body.health>0 && units[i].body.spawnAt<=time) sources.push_back(i);
    }
    std::stable_sort(sources.begin(),sources.end(),[&](size_t a,size_t b){return units[a].id<units[b].id;});
    for(size_t index:sources) {
        auto& source=units[index];auto& f=source.ferry;
        if(source.headless || source.body.health-source.helmHealth-source.shieldHealth<=f.stopBodyHealth) f.enabled=false;
        if(!f.enabled) {f.winding=false;continue;}
        f.blocksMovement=f.winding;
        const float active=std::max(0.0f,step-source.body.stopped);
        if(active<=0) continue;
        const int column=FogFerryRules::Column(source.body.x+source.body.blastAnchorOffset,s.gridLeft,s.cellWidth);
        const int row=source.body.row;
        const bool lit=row>=0 && row<s.rows && column>=0 && column<s.columns && illumination[row*s.columns+column]>0;
        f.remaining=std::max(0.0f,f.remaining-active*(f.winding && lit ? FogFerryRules::LitRate : 1));
        if(f.remaining>0) continue;
        const int target=FogFerryRules::TargetColumn(column,fogLeft,s.columns);
        const float targetX=FogFerryRules::LandingX(target,s.gridLeft,s.cellWidth);
        std::vector<size_t> passengers;
        if(target>=0) for(size_t i=0;i<units.size();++i) {
            const auto& u=units[i];const auto& b=u.body;
            if(i==index || u.ferry.present || b.health<=0 || b.spawnAt>time || b.row!=row || !u.groundHazard || !u.ferryPassengerEligible
                || !u.balloon.CanTargetProjectile(false) || (u.digger.present && !u.digger.CanTargetProjectile(false))) continue;
            const float x=b.x+b.blastAnchorOffset;
            if(std::abs(FogFerryRules::Column(x,s.gridLeft,s.cellWidth)-column)<=1 && x>targetX+5) passengers.push_back(i);
        }
        if(!f.winding) {
            if(!passengers.empty()) {f.winding=true;f.blocksMovement=true;f.remaining=FogFerryRules::Windup;source.body.eating=false;}
            continue; // 首次等待结束后仍需完整前摇，不能同一步直接传送。
        }
        std::stable_sort(passengers.begin(),passengers.end(),[&](size_t a,size_t b){return units[a].id<units[b].id;});
        int moved=0;
        for(size_t i:passengers) {
            auto& u=units[i];u.body.x=targetX+static_cast<float>(moved%5)-u.body.blastAnchorOffset;
            InterruptPassenger(u);++moved;
        }
        f.winding=false;
        f.remaining=moved>0 ? (fogLeft<s.columns ? FogFerryRules::Cooldown : FogFerryRules::ClearCooldown) : FogFerryRules::Windup;
        if(moved>0) {++stats.ferryTransfers;stats.ferryPassengers+=moved;}
    }
}
}
