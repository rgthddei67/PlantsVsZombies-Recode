#include "Game/AI/ColdStorageSearch.h"
#include "Game/AI/ColdStorageFogFerryForecast.h"
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace {
using namespace ColdStorageSearch;
void Check(bool ok,const char* message) {if(!ok) {std::cerr<<message<<std::endl;throw std::runtime_error(message);}}
bool Near(float a,float b) {return std::abs(a-b)<.01f;}
Snapshot Field() {
    Snapshot s;s.rows=3;s.columns=9;s.cellWidth=80;s.gridLeft=242;s.houseX=0;s.rightEdge=1100;
    s.anticipateEconomy=false;return s;
}
Unit Body(int id,int row,float x) {
    Unit u;u.id=id;u.body.row=row;u.body.x=x;u.body.speed=0;u.body.health=u.maximumBody=10000;
    u.body.value=u.body.purchaseCost=100;u.biteDps=0;return u;
}
Plant Target(int id,int row,int col,int layer=1) {
    Plant p;p.id=id;p.row=row;p.column=col;p.x=242+(col+.5f)*80;p.layer=layer;
    p.health=p.maximumHealth=10000;p.reward=100;p.assetValue=100;return p;
}
}

/** 同窗能力消融覆盖真实收益、控制、层序及已付款时序，不要求固定购买组合。 */
void RunColdStorageWatersideForecastTests() {
    using namespace ColdStorageSearch;
    auto s=Field();auto diver=Body(1,1,700);diver.diver.present=true;diver.diver.inWater=true;
    s.waterCells.fill(true);s.current={diver};auto gun=Target(10,1,0);gun.dps=100;gun.diverResisted=true;s.plants={gun};
    const auto water=Evaluate(s,{});s.waterCells.fill(false);const auto land=Evaluate(s,{});
    Check(water[3]>land[3],"diver water cycle must improve survival against direct shots");
    s.waterCells.fill(true);s.plants[0].diverDirectFraction=.25f;const auto poison=Evaluate(s,{});
    Check(poison[3]>land[3] && poison[3]<water[3],"mixed toxin DPS must only reduce its direct projectile fraction");
    s.plants[0].diverDirectFraction=1;s.waterCells.fill(false);
    s.plants[0].diverResisted=false;const auto areaLand=Evaluate(s,{});s.waterCells.fill(true);
    Check(Evaluate(s,{})==areaLand,"water resistance must not reduce splash or non-direct attacks");
    s.plants[0].diverResisted=true;s.current[0].body.stopped=100;
    const auto frozen=Evaluate(s,{});s.current[0].diver.breathing=true;s.current[0].diver.airRemaining=4;
    const auto frozenBreath=Evaluate(s,{});
    Check(std::abs(frozen[3]-frozenBreath[3])<5,"hard control must not freeze the breathing cycle");
    s=Field();s.current={diver};s.current[0].body.x=777;s.plants={Target(10,1,6)};
    ConstructionStats smash;Evaluate(s,{},&smash);
    Check(smash.diverSmashes>0,"diver must smash at contact with independent cadence");
    s.current[0].headless=true;Evaluate(s,{},&smash);Check(smash.diverSmashes==0,"headless diver cannot smash");

    s=Field();auto salvage=Body(1,1,850);salvage.salvage.present=true;s.current={salvage};
    auto host=Target(10,1,6),pot=Target(11,1,6,0);pot.foundationKind=2;pot.salvageable=true;
    s.plants={host,pot};ConstructionStats pulled;auto with=Evaluate(s,{},&pulled);
    Check(pulled.salvages==1 && Near(with[0],pot.reward),"salvage removes exactly the foundation, preserving host");
    s.current[0].salvage.hasHook=false;Check(Evaluate(s,{})[0]==0,"lost magnetic hook must disable salvage");
    s.current[0].salvage.hasHook=true;s.plants.push_back(Target(12,1,7));
    Evaluate(s,{},&pulled);Check(pulled.salvages==0,"foundationless front group must occlude rear foundation");
    s.plants.pop_back();s.current[0].salvage.windup=.1f;s.current[0].salvage.targetID=999;
    s.current[0].body.stopped=100;Evaluate(s,{},&pulled);Check(pulled.salvages==0,"control cancels uncommitted locked salvage");
    s.current[0].body.stopped=0;s.current[0].magneticLayer=3;auto magnet=Target(20,1,7);
    magnet.magnetRadius=magnet.magnetEatingRadius=1000;magnet.magnetRows=1;magnet.magnetRecharge=100;s.plants.push_back(magnet);
    Evaluate(s,{},&pulled);Check(pulled.magneticExtractions==1 && pulled.salvages==0,"magnet must remove tool before pull completes");
    s.plants.pop_back();s.current[0].magneticLayer=0;s.current[0].salvage.windup=0;
    s.waterCells.fill(true);s.potCells.fill(true);s.watersideHazards=true;s.plants[1].foundationKind=1;s.plants[1].aquatic=true;
    auto aquatic=s.plants[1];aquatic.id=13;aquatic.foundationKind=2;aquatic.aquatic=false;s.plants.push_back(aquatic);
    auto delayed=Evaluate(s,{},&pulled);
    Check(pulled.salvages==2 && Near(delayed[0],200),"dual bases are separate pulls; drowning must not award host kill ice");
    s=Field();s.current={salvage};s.current[0].body.x=700;
    auto cannon=Target(10,1,5);cannon.footprintColumns=2;
    auto leftPot=Target(11,1,5,0);leftPot.foundationKind=2;leftPot.salvageable=true;leftPot.reward=1;
    auto rightPot=leftPot;rightPot.id=12;rightPot.column=6;rightPot.x+=80;rightPot.reward=2;
    s.plants={cannon,leftPot,rightPot};
    Check(Near(Evaluate(s,{})[0],1),"multi-cell host must salvage the encountered cell, not its already-passed right base");

    s=Field();auto sludge=Body(1,1,850);sludge.sludge.present=true;sludge.sludge.muzzleOffset=-30;
    sludge.helmHealth=400;sludge.maximumHelm=1000;s.current={sludge};s.plants={Target(10,1,6)};
    ConstructionStats mud;const auto sprayed=Evaluate(s,{},&mud);
    Check(mud.sludgeShots>0 && mud.sludgeHits>0 && Near(mud.sludgeRepaired,600),"sprays must deal real damage and repair only missing cone health");
    s.current[0].helmHealth=0;Evaluate(s,{},&mud);Check(mud.sludgeRepaired==0,"broken cone cannot regenerate");
    s.current.clear();s.sludgeRays={{1,850,0,240,60}};
    auto shell=Target(11,1,6,2);shell.pumpkin=true;shell.health=shell.maximumHealth=100;shell.reward=1;s.plants.push_back(shell);
    const auto shellHit=Evaluate(s,{},&mud);
    Check(mud.sludgeShots==0 && mud.sludgeHits==1 && Near(shellHit[0],1) && Near(shellHit[1],1),
        "committed ray survives shooter death, consumes pumpkin and never overflows into host");
    s.plants.pop_back();s.plants[0].hostileMirrors=1;const auto mirror=Evaluate(s,{},&mud);
    Check(mud.sludgeHits==0 && Near(mirror[1],0),"mirror intercepts the whole mud projectile");
    s.plants[0].hostileMirrors=0;s.sludgeRays[0].remaining=10;Evaluate(s,{},&mud);
    Check(mud.sludgeHits==0,"committed mud must respect sampled travel remaining");
    s=Field();s.sunIceValue=1;auto income=Target(10,1,0);income.sunPerSecond=10;s.plants={income};
    ConstructionStats clean,dirty;Evaluate(s,{},&clean);s.plants[0].sludgeUntil=8;Evaluate(s,{},&dirty);
    Check(dirty.opponentAssets<clean.opponentAssets,"sludge must slow production and expire on game time");

    s=Field();auto parasol=Body(1,1,700);parasol.parasol=true;parasol.parasolHealth=1;
    auto ally=Body(2,0,700);s.current={parasol,ally};auto melon=Target(10,0,0);
    melon.dps=100;melon.hitDamage=100;melon.melon=melon.parasolInterceptable=true;s.plants={melon};
    ConstructionStats covered;auto guarded=Evaluate(s,{},&covered);
    Check(covered.parasolBlocks==1 && Near(covered.parasolAbsorbed,100),"one remaining durability must intercept one complete final shot");
    s.current[0].parasolHealth=0;auto bare=Evaluate(s,{});
    Check(guarded[3]>bare[3],"interception must prevent both primary and splash damage");
    s.current[0].parasolHealth=1500;s.current[0].body.stopped=100;Evaluate(s,{},&covered);
    Check(covered.parasolBlocks==0,"frozen parasol must expose nearby allies");
    s.current[0].body.stopped=0;s.current[0].body.row=2;Evaluate(s,{},&covered);
    Check(covered.parasolBlocks==0,"parasol cannot cover targets two rows away");
    s.current[0].body.row=1;s.current[0].body.spawnAt=100;Evaluate(s,{},&covered);
    Check(covered.parasolBlocks==0,"paid parasol cannot protect before arrival");
    s.current[0].body.spawnAt=10;Evaluate(s,{},&covered);Check(covered.parasolBlocks>0,"paid arrival enables future protection");
    s.current[0].body.spawnAt=0;s.plants[0].parasolInterceptable=false;s.plants[0].melon=false;Evaluate(s,{},&covered);
    Check(covered.parasolBlocks==0,"straight fire must not consume a parasol");
    s=Field();s.budget=100;s.capacity=1;s.plants={Target(10,1,6)};Option option;option.cost=10;option.row=1;option.unit=sludge;
    s.options={option};ConstructionStats birth;Evaluate(s,{{0,0}},&birth);
    Check(birth.sludgeShots>0,"new purchase must use the same ability as current and paid units");
    Check(sprayed[1]>0,"sludge forecast must change combat value rather than only diagnostics");
    s=Field();s.current={diver};s.current[0].body.x=688;s.current[0].diver.eatingTargetID=10;
    s.current[0].diver.smashRemaining=.2f;s.plants={Target(10,1,6)};
    Evaluate(s,{},&smash);Check(smash.diverSmashes>0,"sampled live eating identity must survive approximate collider distance");
    s=Field();s.budget=10;s.capacity=1;s.searchVersion=2;
    s.plants={Target(10,1,6)};s.plants[0].health=s.plants[0].maximumHealth=500;
    option.unit=sludge;option.unit.body.purchaseCost=option.unit.body.value=10;s.options={option};
    const auto chosen=Search(s,Weights{4,1.5f,148,.3f,3.4f,-1,-.27f,1.7f},42);
    Check(!chosen.actions.empty() && chosen.construction.sludgeShots>0,"zero-preference new ability must be discoverable by normal candidate search");
    auto carrier=Body(20,1,920);carrier.ferry={true,true,true,.1f,0};
    auto passenger=sludge;passenger.id=2;passenger.body.x=920;passenger.sludge.windup=1;passenger.sludge.targetID=10;
    std::vector<Unit> convoy{carrier,passenger,salvage,diver};
    convoy[2].id=3;convoy[2].body.x=920;convoy[2].salvage.windup=1;convoy[2].salvage.targetID=10;
    convoy[3].id=4;convoy[3].body.x=920;convoy[3].diver.eatingTargetID=10;
    ConstructionStats moved;std::array<float,54> light{};AdvanceFogFerries(s,0,.5f,2,light,convoy,moved);
    Check(moved.ferryPassengers==3 && convoy[1].sludge.windup==0 && convoy[2].salvage.windup==0
        && convoy[3].diver.eatingTargetID==0,"transport cancels all uncommitted waterside body actions");
}
