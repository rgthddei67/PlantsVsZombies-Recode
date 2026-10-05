#include "Game/Plant/ThunderFlowerRules.h"
#include <cmath>
#include <stdexcept>
#include <limits>

namespace {
void Require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

/** 验证索敌不吃攻击加速、无目标保留已起播发射，以及分步推演与整段推进相同。 */
void RunColdStorageThunderTimingTests() {
    using ThunderFlowerRules::AttackForecast;
    AttackForecast normal;
    Require(normal.Advance(3.3f,1,true)==0,"thunder must wait for cooldown, polling and head windup");
    Require(normal.Advance(.1f,1,true)==1,"first thunder should release after 2.6 seconds plus head windup");
    Require(normal.Advance(2.5f,1,true)==0,"steady thunder must include polling after every cooldown");
    Require(normal.Advance(.1f,1,true)==1,"steady thunder should release after 2.6 seconds");

    AttackForecast accelerated;
    Require(accelerated.Advance(1.9f,2,true)==0,"attack speed must not shorten 0.6 second target polling");
    Require(accelerated.Advance(.1f,2,true)==1,"accelerated cooldown and head windup should still release");

    AttackForecast noTarget;
    Require(noTarget.Advance(3,1,false)==0,"empty row must not invent thunder shots");
    Require(noTarget.cooldownRemaining==0,"failed polling must retain completed cooldown");
    Require(noTarget.Advance(1,1,true)==1,"target arriving after completed cooldown uses remaining poll time");

    AttackForecast pending;
    pending.pendingRemaining=.2f;
    Require(pending.Advance(.25f,1,false)==1,"already winding head must release even after target disappears");
    Require(pending.Advance(.1f,1,false)==0,"committed head shot must not be released twice");

    AttackForecast boundary;
    boundary.cooldownRemaining=boundary.checkRemaining=boundary.pendingRemaining=0;
    Require(boundary.Advance(.01f,1,true)==1 && boundary.pendingRemaining>0,
        "simultaneous release and polling must commit old shot before starting new windup");
    Require(boundary.Advance(.1f,0,true)==0,"paused attack rate must not enter a zero time loop");
    Require(boundary.Advance(.1f,(std::numeric_limits<float>::infinity)(),true)==0,
        "invalid rate must not enter a zero time loop");

    AttackForecast whole, stepped;
    const int complete = whole.Advance(12.7f,1.3f,true);
    int split=0;
    for (int i=0;i<127;++i) split+=stepped.Advance(.1f,1.3f,true);
    Require(complete==split && std::abs(whole.cooldownRemaining-stepped.cooldownRemaining)<.0001f
        && std::abs(whole.checkRemaining-stepped.checkRemaining)<.0001f,
        "thunder phase timing must be independent of numerical step partition");
}
