#include "Game/AI/ColdStorageAssaultIntent.h"
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>

namespace {
/** 失败时单独标明观测生命周期契约，不把事件响应写成必须购兵的断言。 */
void CheckIntent(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAILED assault intent: "<<message<<'\n';std::exit(1);}
}

/** 清空时只保留令牌代次，不能留下来源、在途弹、待消费需求或旧付款时间。 */
void CheckCleared(const ColdStorageSearch::AssaultIntentState& intent) {
    CheckIntent(!intent.Active() && !intent.NeedsReplan() && intent.ReplanCount()==0
        && intent.ArmedAt()==0 && !intent.Conditional() && !intent.Eligible() && !intent.ObservedLaunch()
        && intent.Observation()==ColdStorageSearch::AssaultObservation{},
        "invalidated lifetimes drop observations, timestamps, replan allowance use and pending events");
}
}

/** 只响应已发生的离散公开变化；完整战场重评与真实付款仍归调用方。 */
void RunColdStorageAssaultIntentTests() {
    using namespace ColdStorageSearch;
    {
        AssaultIntentState intent;
        const AssaultObservation initial{100,3,3,0};
        CheckCleared(intent);
        CheckIntent(!intent.Observe(10,initial),"an observation alone cannot create an unpaid assault intent");
        CheckIntent(intent.Arm(10,initial) && intent.Active() && !intent.NeedsReplan(),
            "new actual payment establishes a baseline without inventing an immediate cannon event");
        const auto initialRevision=intent.Revision();
        CheckIntent(!intent.Observe(12,initial) && intent.Revision()==initialRevision,
            "countdown and elapsed time alone cannot invent a predicted fire or impact event");
        const AssaultObservation oneFired{101,3,2,1};
        CheckIntent(intent.Observe(13,oneFired) && intent.NeedsReplan()
            && intent.Observation().activeSources==3 && intent.Observation().readySources==2
            && intent.Observation().committedShots==1,
            "one actual shot requests a replan while two other ready cannons and its in-flight shell remain");
        const auto firedRevision=intent.Revision();
        CheckIntent(!intent.Observe(13,oneFired) && !intent.Observe(14,oneFired)
            && intent.Revision()==firedRevision && intent.NeedsReplan(),
            "identical repeated observations have no new edge but preserve unconsumed work while busy");
        CheckIntent(intent.MarkReplanned(firedRevision) && !intent.NeedsReplan() && intent.ReplanCount()==1,
            "a successfully started complete replan consumes precisely the captured event");
        CheckIntent(!intent.MarkReplanned(firedRevision) && !intent.Observe(20,oneFired),
            "the same cannon event cannot repeatedly consume dedicated response slots");
        CheckIntent(intent.ArmedAt()==10,"observations and successful replans do not renew the payment lifetime");
    }
    {
        AssaultIntentState intent;
        intent.Arm(0,{200,1,0,1});
        CheckIntent(intent.Observe(1,{201,0,0,1}) && intent.Observation().committedShots==1,
            "removing the last source is an event but does not erase its already airborne shell");
        const auto removedRevision=intent.Revision();
        CheckIntent(intent.Observe(2,{202,0,0,0}),"the independent shell disappearing is a separate observed impact event");
        const auto landedRevision=intent.Revision();
        CheckIntent(intent.MarkReplanned(removedRevision) && intent.NeedsReplan()
            && intent.Revision()==landedRevision,
            "finishing an older captured source-removal replan cannot consume the newer shell event");
        CheckIntent(!intent.MarkReplanned(removedRevision) && !intent.MarkReplanned(landedRevision+1),
            "duplicate or future revision tokens cannot consume events");
        CheckIntent(intent.MarkReplanned(landedRevision) && !intent.NeedsReplan(),
            "the newer captured shell event remains independently consumable");
        CheckIntent(intent.Active(),"zero sources and shells do not claim tactical success or force a purchase");
    }
    {
        AssaultIntentState intent;
        intent.Arm(3,{300,2,1,1});
        CheckIntent(intent.Observe(4,{301,2,1,1}),
            "a stable source or shell identity/phase replacement is observed even when aggregate counts are equal");
        CheckIntent(intent.MarkReplanned(intent.Revision()),"the identity-only change can be evaluated once");
        CheckIntent(intent.Observe(5,{301,2,0,2}),
            "actual count changes still request reevaluation when a supplied signature remains unchanged");
        CheckIntent(intent.MarkReplanned(intent.Revision()),"the count-only change can be evaluated once");
        const auto revision=intent.Revision();
        CheckIntent(!intent.Observe(25,{301,2,0,2}) && !intent.Observe(47,{301,2,0,2})
            && intent.Active() && intent.Revision()==revision,
            "neither elapsed flight nor imagined reload completion fabricates an unobserved phase transition");
        CheckIntent(!intent.Observe(48,{302,2,2,0}),
            "an event exactly at the payment deadline expires instead of extending the lifetime");
        CheckCleared(intent);
    }
    {
        AssaultIntentState intent;
        intent.Arm(10,{400,1,1,0});
        intent.Observe(11,{401,1,0,1});
        const auto oldRevision=intent.Revision();
        CheckIntent(intent.Arm(40,{402,1,0,1}) && intent.ArmedAt()==40
            && !intent.NeedsReplan() && intent.ReplanCount()==0,
            "only a new confirmed payment can rearm the lifetime and reset its dedicated response allowance");
        CheckIntent(intent.Observe(41,{403,1,0,0}) && !intent.MarkReplanned(oldRevision) && intent.NeedsReplan(),
            "a previous payment's captured token cannot swallow a new payment's observed event");
        CheckIntent(intent.MarkReplanned(intent.Revision()) && !intent.Observe(84,{403,1,0,0}) && intent.Active(),
            "a newly paid lifetime remains valid for its own full duration");
        CheckIntent(!intent.Observe(85,{403,1,0,0}),"the renewed lifetime expires without a new actual payment");
        CheckCleared(intent);
    }
    {
        AssaultIntentState intent;
        intent.Arm(0,{500,1,1,0});
        for(unsigned event=0;event<AssaultIntentState::MaxReplans;++event) {
            const AssaultObservation changed{501+event,1,0,1};
            CheckIntent(intent.Observe(event+1,changed) && intent.MarkReplanned(intent.Revision())
                && !intent.NeedsReplan(),"each different actual event uses at most one dedicated response");
        }
        CheckIntent(intent.ReplanCount()==AssaultIntentState::MaxReplans && intent.Active(),
            "exhausting dedicated wakeups does not end ordinary proposal availability or force execution");
        const AssaultObservation later{600,0,0,1};
        CheckIntent(!intent.Observe(20,later) && !intent.NeedsReplan()
            && !intent.MarkReplanned(intent.Revision()) && intent.Observation()==later,
            "after the finite wake budget, public facts remain current without requesting unbounded dedicated work");
        CheckIntent(intent.Arm(21,later) && intent.ReplanCount()==0 && !intent.NeedsReplan(),
            "a new payment may establish a fresh bounded response budget");
    }
    {
        AssaultIntentState intent;
        const AssaultObservation ready{700,2,2,0};
        intent.Arm(5,ready); intent.Observe(6,{701,2,1,1});
        const auto oldRevision=intent.Revision();
        intent.Clear();
        CheckCleared(intent);
        CheckIntent(intent.Revision()>oldRevision && !intent.MarkReplanned(oldRevision),
            "explicit cancellation invalidates all outstanding tokens");
        CheckIntent(intent.Arm(0,ready),"after explicit clear a restored or new board can start at time zero");
        CheckIntent(!intent.Observe(46,ready),"missing the exact expiry tick still clears at the first later observation");
        CheckCleared(intent);
        intent.Arm(10,ready); intent.Observe(12,ready);
        CheckIntent(!intent.Observe(11,{702,2,1,1}),"time rollback rejects a seemingly useful observation");
        CheckCleared(intent);
        intent.Arm(10,ready); intent.Observe(12,ready);
        CheckIntent(!intent.Arm(11,ready),"payment notification with backwards time clears instead of extending stale intent");
        CheckCleared(intent);
        for(double invalid:{std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),-1.0}) {
            intent.Arm(0,ready);
            CheckIntent(!intent.Observe(invalid,ready),"non-finite or negative observation times cannot keep an intent alive");
            CheckCleared(intent);
            CheckIntent(!intent.Arm(invalid,ready),"non-finite or negative payment times cannot establish a lifetime");
            CheckCleared(intent);
        }
        for(const auto invalid:{AssaultObservation{800,-1,0,0},AssaultObservation{800,1,-1,0},
                AssaultObservation{800,1,2,0},AssaultObservation{800,0,0,-1}}) {
            intent.Arm(0,ready);
            CheckIntent(!intent.Observe(1,invalid),"invalid source or shell counts clear instead of pretending a favorable event");
            CheckCleared(intent);
            CheckIntent(!intent.Arm(0,invalid),"invalid public counts cannot arm an observation lifetime");
            CheckCleared(intent);
        }
        CheckIntent(intent.Arm(0,{900,0,0,2}),"committed shells remain valid independently of any living source");
    }
    {
        AssaultIntentState intent;
        const AssaultObservation baseline{1000,2,1,1,40};
        const AssaultTrigger trigger{6,20,true,true,0};
        CheckIntent(intent.ArmConditional(10,baseline,trigger) && intent.Conditional()
            && intent.ExpiresAt()==30 && !intent.Eligible() && !intent.NeedsReplan(),
            "a conditional anchor copies a bounded deadline without a launch, payment or immediate event");
        const auto anchorRevision=intent.Revision();
        for(int frame=0;frame<30;++frame) CheckIntent(!intent.Observe(10,baseline),
            "wall-clock frames during a paused game cannot advance the earliest-time edge");
        CheckIntent(intent.Revision()==anchorRevision && !intent.Observe(15,baseline),
            "unchanged public observations before the one-shot earliest edge cannot trigger a timer replan");
        CheckIntent(intent.Observe(16,baseline) && !intent.Eligible() && !intent.ObservedLaunch(),
            "the earliest-time edge requests one fresh snapshot but an anchor's old airborne shell is not a new launch");
        const auto timedRevision=intent.Revision();
        CheckIntent(intent.MarkReplanned(timedRevision) && !intent.Observe(16,baseline)
            && !intent.Observe(17,baseline) && intent.Revision()==timedRevision,
            "the same time edge never retriggers or renews its deadline after a reviewed wait");
        CheckIntent(intent.Observe(18,{1001,2,0,2,40}) && !intent.ObservedLaunch() && !intent.Eligible(),
            "FIRING windup and changed committed counts cannot masquerade as a true observed launch");
        CheckIntent(intent.Observe(19,{1002,2,1,1,41}) && intent.ObservedLaunch() && !intent.Eligible(),
            "a new projectile identity proves post-anchor launch but another ready cannon and the shell still block the tail");
        CheckIntent(intent.Observe(20,{1003,1,0,1,41}) && !intent.Eligible(),
            "source death does not satisfy shots-clear while the independent new shell is airborne");
        CheckIntent(intent.Observe(21,{1004,1,1,0,0}) && !intent.Eligible(),
            "the launch witness survives landing but another ready cannon still blocks the tail");
        CheckIntent(intent.Observe(22,{1005,1,0,0,0}) && intent.Eligible(),
            "only all current observed gates together admit the unpaid proposal for fresh evaluation");
        CheckIntent(!intent.Observe(30,{1005,1,0,0,0}),"the copied forecast deadline expires before the generic forty-five-second limit");
        CheckCleared(intent);
    }
    {
        AssaultIntentState intent;
        const AssaultObservation quiet{1100,0,0,0};
        CheckIntent(intent.ArmConditional(3,quiet,{2,300,false,false,-1}) && intent.ExpiresAt()==48,
            "a long forecast expiry is bounded to forty-five game seconds after its true anchor");
        CheckIntent(intent.Observe(5,quiet) && intent.Eligible() && intent.MarkReplanned(intent.Revision()),
            "a pure timed tail does not need an invented cannon source or projectile");
        CheckIntent(!intent.Observe(5,quiet) && intent.Active(),"repeated paused observations preserve the original deadline");
        CheckIntent(!intent.Observe(4,quiet),"backward game time clears even a previously eligible timed tail");
        CheckCleared(intent);
        for(const auto trigger:{AssaultTrigger{45,300},AssaultTrigger{4,4},AssaultTrigger{-1,5},
                AssaultTrigger{0,0},AssaultTrigger{0,std::numeric_limits<double>::infinity()},
                AssaultTrigger{std::numeric_limits<double>::quiet_NaN(),5},AssaultTrigger{0,5,false,false,-2}}) {
            CheckIntent(!intent.ArmConditional(0,quiet,trigger),"invalid or unreachable trigger windows cannot arm unpaid tails");
            CheckCleared(intent);
        }
        CheckIntent(!intent.ArmConditional(0,{1100,0,0,0,-1},{1,5}),"negative projectile identities cannot create launch evidence");
        CheckCleared(intent);
    }
    std::cout<<"Assault intent observes bounded real phase changes and one-shot time edges without payment or lifetime renewal\n";
}
