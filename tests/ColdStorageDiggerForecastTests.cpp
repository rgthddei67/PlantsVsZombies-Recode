#include "Game/Zombie/DiggerRules.h"
#include "Game/Zombie/GoldenIceRules.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void RequireDigger(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool NearDigger(float a,float b) { return std::abs(a-b)<.0001f; }
DiggerRules::Forecast BirthDigger() {
    DiggerRules::Forecast digger;
    digger.present=true;
    digger.surfaceX=272;
    digger.tunnelSpeed=67;
    return digger;
}
}

/** 验证地下穿墙、出土与转向边沿、磁吸反制，以及不同数值步长下的同一阶段结果。 */
void RunColdStorageDiggerForecastTests() {
    using DiggerRules::Phase;
    auto neutral=BirthDigger();
    RequireDigger(neutral.abilityMultiplier==1 && neutral.rightWindMultiplier==1 && neutral.leftWindMultiplier==1,
        "pure digger rules must use neutral default multipliers");
    // 与实体出生画像的抵消顺序一致：12/能力保存为根速，再按未来场地组合能力。
    neutral.abilityMultiplier=18.0f/(16.0f*1.3f);
    neutral.rightWalkSpeed/=neutral.abilityMultiplier;
    neutral.leftWalkSpeed/=neutral.abilityMultiplier;
    RequireDigger(NearDigger(neutral.rightWalkSpeed*GoldenIceRules::Amplify(neutral.abilityMultiplier,0),12)
        && NearDigger(neutral.leftWalkSpeed*GoldenIceRules::Amplify(neutral.abilityMultiplier,0),30),
        "reapplying unamplified ability must recover ordinary birth walking speeds");
    RequireDigger(NearDigger(neutral.rightWalkSpeed*GoldenIceRules::Amplify(neutral.abilityMultiplier,2),3),
        "future golden stacks must amplify ability exactly once instead of rescaling baked walking speed");
    neutral.rightWindMultiplier=1.25f;
    neutral.leftWindMultiplier=.8f;
    RequireDigger(NearDigger(neutral.rightWalkSpeed*neutral.abilityMultiplier*neutral.rightWindMultiplier,15)
        && NearDigger(neutral.leftWalkSpeed*neutral.abilityMultiplier*neutral.leftWindMultiplier,24)
        && NearDigger(neutral.tunnelSpeed,67),
        "directional wind factors belong to ground walking and must never alter tunneling speed");
    auto digger=BirthDigger();
    float x=406;
    RequireDigger(digger.Advance(1,x)==0 && NearDigger(x,339) && digger.phase==Phase::TUNNELING,
        "tunneling digger should move through ordinary walls without entering walking");
    RequireDigger(digger.Advance(1,x)==0 && NearDigger(x,272) && digger.phase==Phase::TUNNELING,
        "exact surface line does not satisfy strict emergence threshold");
    RequireDigger(!digger.CanTargetProjectile() && !digger.CanFreeze() && !digger.CanMower()
        && !digger.CanGroundHazard() && !digger.CanEat() && !digger.CanTriggerGameOver(),
        "underground digger must be excluded from ground fire, control, contact and house victory");
    RequireDigger(digger.AshKillsDirectly(1) && !digger.AshKillsDirectly(0),
        "positive ash damage directly removes underground digger");
    digger.Advance(.5f,x);
    RequireDigger(digger.phase==Phase::RISING && NearDigger(digger.remaining,.8f)
        && !digger.HasMagneticItem() && !digger.CanTargetProjectile() && digger.AshKillsDirectly(1),
        "rise is still noninteractive and cannot have its pickaxe magnetized");
    digger.Advance(.8f,x);
    RequireDigger(digger.phase==Phase::STUNNED && NearDigger(digger.remaining,3.5f),
        "normal rise must enter 3.5 second dizzy warning");
    RequireDigger(digger.CanTargetProjectile() && !digger.CanTargetProjectile(true) && digger.CanFreeze()
        && digger.CanChill() && digger.CanGroundHazard() && digger.CanMower() && !digger.CanEat()
        && !digger.AshKillsDirectly(1),"dizzy digger can be attacked and controlled but cannot bite");
    const float walking=digger.Advance(4,x);
    RequireDigger(NearDigger(walking,.5f) && digger.phase==Phase::WALKING_WITH_PICKAXE
        && digger.IsMovingRight() && NearDigger(digger.WalkingSpeed(),12)
        && !digger.CanTriggerGameOver(),"warning completion must walk back toward the plant rear");
    RequireDigger(digger.LosePickaxe() && !digger.LosePickaxe() && digger.IsMovingRight()
        && !digger.CanTriggerGameOver(),"ordinary walking pickaxe removal does not invent an immediate turn");

    auto magnet=BirthDigger();
    x=700;
    RequireDigger(magnet.LosePickaxe() && magnet.phase==Phase::TUNNELING_PAUSE_WITHOUT_PICKAXE,
        "underground pickaxe removal begins a two second pause in place");
    magnet.Advance(2,x);
    RequireDigger(magnet.phase==Phase::RISING_WITHOUT_PICKAXE && NearDigger(magnet.remaining,1.3f)
        && NearDigger(x,700),"magnetized digger rises at its current position after pause");
    const float leftTime=magnet.Advance(1.8f,x);
    RequireDigger(NearDigger(leftTime,.5f) && magnet.phase==Phase::WALKING_WITHOUT_PICKAXE
        && !magnet.IsMovingRight() && magnet.CanTriggerGameOver() && NearDigger(magnet.WalkingSpeed(),30),
        "magnetized digger skips normal dizzy phase and may later enter the house");

    auto normalWarning=BirthDigger();
    normalWarning.phase=Phase::STUNNED;
    normalWarning.remaining=2;
    RequireDigger(normalWarning.LosePickaxe() && normalWarning.phase==Phase::STUNNED
        && NearDigger(normalWarning.remaining,2),"ordinary digger retains warning timer after magnetizing");
    normalWarning.Advance(2,x);
    RequireDigger(normalWarning.phase==Phase::WALKING_WITHOUT_PICKAXE,
        "ordinary dizzy digger chooses leftward walking after losing its pickaxe");
    auto eliteWarning=BirthDigger();
    eliteWarning.phase=Phase::STUNNED;
    eliteWarning.remaining=2;
    eliteWarning.losePickaxeImmediatelyWhenStunned=true;
    RequireDigger(eliteWarning.LosePickaxe() && eliteWarning.phase==Phase::WALKING_WITHOUT_PICKAXE
        && eliteWarning.remaining==0,"elite warning pickaxe removal immediately begins normal walking");

    auto whole=BirthDigger(),split=whole;
    float wholeX=540,splitX=540;
    const float wholeWalk=whole.Advance(10,wholeX);
    float splitWalk=0;
    for (int i=0;i<100;++i) splitWalk+=split.Advance(.1f,splitX);
    RequireDigger(whole.phase==split.phase && NearDigger(wholeX,splitX)
        && NearDigger(wholeWalk,splitWalk),"phase timeline must not depend on numerical step partition");

    auto paused=BirthDigger();
    x=500;
    paused.Advance(0,x);
    paused.Advance(.1f,x,0);
    paused.Advance(.1f,x,(std::numeric_limits<float>::infinity)());
    RequireDigger(paused.phase==Phase::TUNNELING && NearDigger(x,500),
        "zero action time, stopped movement and invalid multipliers must not change tunnel phase");
}
