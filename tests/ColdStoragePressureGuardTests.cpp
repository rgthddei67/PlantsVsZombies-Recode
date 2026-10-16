#include "Game/AI/ColdStorageSearch.h"
#include "Game/AI/ColdStorageHealerForecast.h"
#include "Game/Zombie/PressureGuardRules.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <cstring>

namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << std::endl; throw std::runtime_error(message); }
}
bool Near(float a, float b) { return std::abs(a-b)<.01f; }
/** 同位置静止护卫与后排夹具，隔离价格、移动和随机攻击速度。 */
ColdStorageSearch::Unit Guard(int id, int row, bool ability=true) {
    ColdStorageSearch::Unit u;
    u.id=id; u.body.row=row; u.body.x=700; u.body.health=5100; u.helmHealth=u.maximumHelm=4500;
    u.maximumBody=600; u.body.value=u.body.purchaseCost=35; u.pressureGuard=ability;
    return u;
}
}

/** 检查真正滚动窗口、保存余时以及完整战斗预测中的保护反事实。 */
void RunColdStoragePressureGuardTests()
{
    using namespace ColdStorageSearch;
    PressureGuardRules::DamageWindow window;
    Check(Near(window.Consume(400,0),400) && Near(window.Consume(400,.25),200),"pressure shared budget must cap at 600");
    Check(Near(window.Consume(3000,.99),0),"pressure cap cannot reset at a frame or fixed boundary");
    auto saved=window.RelativeTo(.5);
    Check(Near(saved.Used(0),600) && Near(saved.Consume(600,.5),400),"saved pressure window must keep independent hit expiries");
    Check(Near(saved.Consume(600,.75),200),"rolling pressure window must release only expired damage");

    Snapshot s; s.rows=3; s.columns=9; s.cellWidth=80; s.gridLeft=242;
    s.current={Guard(1,1)};
    Unit worker; worker.id=2; worker.body.row=0; worker.body.x=730; worker.body.health=worker.maximumBody=3000;
    worker.body.economic=true; worker.body.value=worker.body.purchaseCost=20; s.current.push_back(worker);
    Plant p; p.row=0; p.x=300; p.health=100000; p.dps=1000; p.hitDamage=1000; p.range=1000;
    s.plants.push_back(p);
    const auto protectedResult=Evaluate(s,{});
    s.current[0].pressureGuard=false;
    const auto exposedResult=Evaluate(s,{});
    Check(protectedResult[4]>exposedResult[4],"pressure guard must extend the adjacent worker's actual forecast income");
    s.current[0].pressureGuard=true; s.current[0].body.stopped=300;
    const auto frozenResult=Evaluate(s,{});
    Check(frozenResult==exposedResult,"hard-controlled guard must not protect adjacent allies");

    // 用完整爆炸事务检查护卫先死、保护不叠加、下一爆失去保护及低本体穿甲化灰。
    Snapshot ash; ash.rows=3; ash.columns=9; ash.cellWidth=80; ash.houseX=-10000; ash.traceCombat=true;
    auto guard=Guard(10,1); guard.body.health=1600; guard.helmHealth=1000;
    Unit ally; ally.id=11; ally.body.row=0; ally.body.x=730; ally.body.health=ally.maximumBody=1000;
    ash.current={guard,ally};
    Counter blast; blast.source=0; blast.blast.committed=true; blast.blast.ready=1;
    blast.blast.x=700; blast.blast.reach.fill(1000); blast.blast.damage=1800;
    ash.counters={blast};
    const auto impacts=[&](const Snapshot& world, int target) {
        ConstructionStats trace; Evaluate(world,{},&trace);
        std::vector<float> result;
        for(const auto& event:trace.combatTrace) if(std::strcmp(event.event,"counter_impact")==0)
            for(const auto& hit:event.targets) if(hit.id==target) result.push_back(hit.after);
        return result;
    };
    auto protectedHits=impacts(ash,11);
    Check(!protectedHits.empty() && Near(protectedHits[0],640),"same-blast guard death must still reduce ally ash to 360");
    auto guardHits=impacts(ash,10);
    Check(!guardHits.empty() && Near(guardHits[0],0),"guard must take full ash without the ordinary cap");
    auto secondGuard=guard; secondGuard.id=12; ash.current.insert(ash.current.begin(),secondGuard);
    Check(impacts(ash,11)==protectedHits,"overlapping guards must not stack ash protection");
    ash.current={guard,ally}; ash.current[0].body.stopped=300;
    Check(Near(impacts(ash,11)[0],0),"hard control must disable ash protection");
    ash.current[0]=guard; ash.current[0].helmHealth=0;
    Check(Near(impacts(ash,11)[0],0),"broken guard must not protect from ash");
    ash.current[0]=guard; ash.current[1].body.x=781;
    Check(Near(impacts(ash,11)[0],0),"out-of-range allies must take full ash");
    ash.current={guard,ally}; auto later=blast; later.source=1; later.blast.ready=2; ash.counters.push_back(later);
    const auto repeated=impacts(ash,11);
    Check(repeated.size()==2 && Near(repeated[0],640) && Near(repeated[1],0),"a later blast must not reuse dead guard protection");
    ash.counters={blast}; ash.current[1].helmHealth=1100; ash.current[1].body.health=1370; ash.current[1].ashBodyThreshold=true;
    Check(Near(impacts(ash,11)[0],0),"protected 360 ash must still char a 270-body bucket through its helmet");

    // 没有可治本体损伤时，急救员不为不可修复的装置施法。
    std::vector<Unit> units{Guard(1,1)}; units[0].helmHealth=1000; units[0].body.health=1600;
    Unit healer; healer.id=2; healer.body.row=1; healer.body.x=700; healer.body.health=healer.maximumBody=800;
    healer.healer.present=true; healer.healer.cooldown=0; units.push_back(healer);
    ConstructionStats stats; std::vector<float> initial{5100,800};
    AdvanceHealers(s,0,units,initial,stats);
    Check(units[1].healer.phase==HealerRules::Forecast::Phase::IDLE && Near(units[0].helmHealth,1000),
        "healer forecast must not repair pressure armor or spend a cast on it");
}
