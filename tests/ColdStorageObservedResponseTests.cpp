#include "Game/AI/ColdStorageObservedResponse.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

/** 公开历史不重复计票、不把删失当完整容忍，也不能按后台重算无限续期。 */
void RunColdStorageObservedResponseTests() {
    using namespace ColdStorageResponse;
    const auto check=[](bool okay,const char* message) {
        if(!okay) {std::cerr<<"FAILED response observation: "<<message<<'\n';std::exit(1);}
    };
    const auto profile=[](const Forecast& forecast,float window,bool weak=false)->const Profile* {
        for(const auto& p:forecast.profiles) if(p.windowSeconds==window && p.allowWeakTransfer==weak) return &p;
        return nullptr;
    };
    Observation seen;seen.eligible=seen.visibleHostiles=true;
    seen.context={1,2,2,1,false,false};seen.targetSignature=123;
    seen.context.learnablePaidAreaMask=1;seen.context.uncoveredCapitalBand=2;
    seen.targets.push_back({123,0,1000,100,270,4,false,false});
    Tracker tracker;
    check(tracker.BuildForecast(0).profiles.empty(),"no observations do not grant a patient hypothesis");
    for(int n=0;n<=48;++n) tracker.Observe(n*.5f,seen);
    const auto first=tracker.BuildForecast(24);
    const auto* p=profile(first,16);
    check(p && p->encounters==1 && p->nonResponses==1 && std::abs(p->weight-1.0f/3)<.0001f,
        "forty-nine frames are one encounter, not forty-nine votes");
    check(first.currentExposureSeconds==24,"active age is retained for nonrenewing future decision clocks");
    const auto revision=tracker.Revision();tracker.Observe(24,seen);
    check(tracker.Revision()==revision,"repeated paused timestamps cannot add samples");
    const auto second=tracker.BuildForecast(24);
    check(second.revision==first.revision && second.currentExposureSeconds==24,"diagnostic copies do not mutate history");
    auto gap=seen;gap.eligible=gap.visibleHostiles=false;
    tracker.Observe(24.5f,gap);tracker.Observe(25,seen);tracker.PaidAreaCast(26);
    check(tracker.BuildForecast(26).profiles.empty(),"real early cast revokes previously supported patience");
    check(tracker.Revision()>revision,"real reaction invalidates old background model generation");

    Tracker censored;
    for(int n=0;n<=20;++n) censored.Observe(n*.5f,seen);
    auto noOpportunity=seen;noOpportunity.eligible=false;
    censored.Observe(10.5f,noOpportunity);
    const auto stopped=censored.BuildForecast(10.5f);
    check(profile(stopped,8) && !profile(stopped,16),"free-shot censor retains completed eight seconds only");
    noOpportunity.visibleHostiles=false;censored.Observe(11,noOpportunity);
    auto newSeen=seen;newSeen.targets[0].id=456;newSeen.targetSignature=456;
    for(int n=0;n<=20;++n) censored.Observe(12+n*.5f,newSeen);
    const auto two=censored.BuildForecast(22);
    const auto* twoP=profile(two,8);
    check(twoP && twoP->encounters==2 && twoP->weight==.5f,"genuinely new visible targets add one distinct encounter");
    noOpportunity.visibleHostiles=false;censored.Observe(23,noOpportunity);
    censored.Observe(324,noOpportunity);
    check(censored.BuildForecast(324).profiles.empty(),"old observations expire by game time");

    Tracker fog;
    for(int n=0;n<=32;++n) fog.Observe(n*.5f,seen);
    fog.Observe(16.5f,gap);
    for(int n=0;n<=32;++n) fog.Observe(17+n*.5f,seen);
    for(const auto& item:fog.BuildForecast(33).profiles)
        check(item.encounters==1,"same IDs reappearing from fog cannot multiply encounter votes");

    Tracker changing;
    for(int n=0;n<=16;++n) changing.Observe(n*.5f,seen);
    auto different=seen;different.context.readyPaidAreaMask=different.context.learnablePaidAreaMask=2;
    for(int n=0;n<=16;++n) changing.Observe(8.5f+n*.5f,different);
    for(int n=0;n<=16;++n) changing.Observe(17+n*.5f,seen);
    const auto pieces=changing.BuildForecast(25);
    check(!profile(pieces,16),"different tool intervals cannot concatenate into a longer patient window");
    for(const auto& piece:pieces.profiles) check(piece.encounters==1,"same encounter segments never multiply votes");
    changing.Observe(1,seen);
    check(changing.BuildForecast(1).profiles.empty(),"game-clock rollback clears old evidence");

    Tracker aging;
    for(int n=0;n<=16;++n) aging.Observe(n*.5f,seen);
    auto growing=seen;growing.context.economicPresent=true;
    aging.Observe(8.5f,growing);aging.Observe(9,growing);
    check(aging.BuildForecast(9).sourceExposureSeconds[0]==9,
        "context change does not renew the same continuously available source");
    growing.context.readyPaidAreaMask=growing.context.learnablePaidAreaMask=3;aging.Observe(9.5f,growing);
    const auto twoSources=aging.BuildForecast(9.5f);
    check(twoSources.sourceExposureSeconds[0]==9.5f && twoSources.sourceExposureSeconds[1]==.5f,
        "newly ready source has its own age and never resets an old source");

    Tracker coverage;
    auto allCovered=seen;allCovered.context.learnablePaidAreaMask=0;allCovered.eligible=false;
    allCovered.context.committedCoveragePresent=true;allCovered.context.uncoveredCapitalBand=0;
    for(int n=0;n<=20;++n) coverage.Observe(n*.5f,allCovered);
    check(coverage.BuildForecast(10).profiles.empty(),"all covered targets cannot teach nonresponse despite a legal card");
    auto partial=seen;partial.context.committedCoveragePresent=true;partial.context.uncoveredCapitalBand=1;
    for(int n=21;n<=28;++n) coverage.Observe(n*.5f,partial);
    const auto partialHistory=coverage.BuildForecast(14);
    check(profile(partialHistory,2) && partialHistory.sourceExposureSeconds[0]==14,
        "uncovered witness starts a learning segment without renewing legal source age");
    coverage.Observe(14.5f,allCovered);coverage.Observe(15,partial);
    check(coverage.BuildForecast(15).sourceExposureSeconds[0]==15,
        "coverage changes cannot pause or restart the same legal opportunity");

    Tracker boundary;
    for(int n=0;n<=20;++n) boundary.Observe(n*.5f,seen);
    auto boundaryContext=seen;boundaryContext.context.exposedCapitalBand=3;
    for(int n=21;n<=31;++n) boundary.Observe(n*.5f,boundaryContext);
    const auto beforeBoundary=boundary.Revision();boundary.Observe(16,boundaryContext);
    check(boundary.Revision()>beforeBoundary && boundary.BuildForecast(16).sourceExposureSeconds[0]==16,
        "continuous source H invalidates background evidence even when the active context piece crosses no window");

    Tracker merging;
    for(int n=0;n<=16;++n) merging.Observe(n*.5f,seen);
    merging.Observe(8.5f,gap);
    for(int n=0;n<=16;++n) merging.Observe(9+n*.5f,newSeen);
    const auto beforeMerge=merging.Revision();
    auto joined=newSeen;joined.targets.push_back(seen.targets[0]);merging.Observe(17.5f,joined);
    check(merging.Revision()>beforeMerge,"merging previously distinct public identities invalidates old evidence weights");
    for(const auto& item:merging.BuildForecast(17.5f).profiles)
        check(item.encounters==1,"joined public encounter cannot retain duplicate successful votes");
    auto overflow=seen;overflow.targets.clear();
    for(int id=1;id<=4097;++id) {auto target=seen.targets[0];target.id=id;overflow.targets.push_back(target);}
    merging.Observe(18,overflow);
    check(merging.BuildForecast(18).profiles.empty(),"identity overflow discards active success instead of readding it after clearing history");

    Profile strict{seen.context,16,.5f,2,2,false};
    check(Supports(strict,seen.context),"exact public context is supported");
    auto context=seen.context;context.readyPaidAreaMask=3;
    check(!Supports(strict,context),"new paid tool cannot inherit old joint nonresponse");
    context=seen.context;context.readyFreeAreaCount=0;
    check(!Supports(strict,context),"three or one ready free cannons cannot teach the no-cannon case");
    context=seen.context;context.nearestThreatBand=0;
    check(!Supports(strict,context),"emergency defense never suppressed");
    context=seen.context;context.economicPresent=true;
    check(!Supports(strict,context),"new economic capability is not a strict match");
    strict.allowWeakTransfer=true;strict.weight*=.5f;
    check(Supports(strict,context) && strict.weight==.25f,"weak transfer is a separate uniformly lower-weight world");
    context.exposedCapitalBand=4;
    check(!Supports(strict,context),"weak transfer cannot grow an unbounded force");
    context=seen.context;context.complexPresent=true;
    check(!Supports(strict,context),"unknown special capabilities remain unmodeled");
    strict.context.complexPresent=true;
    check(!Supports(strict,context),"two different special capabilities cannot match through a true boolean");
    strict.context=seen.context;strict.allowWeakTransfer=false;
    context=seen.context;context.coveredCapitalBands[0]=2;
    check(!Supports(strict,context),"dispersed target history cannot support a more valuable clustered explosion");
    Profile partialProfile{partial.context,2,.333333f,1,1,false};
    context=partial.context;context.exposedCapitalBand=3;
    check(!Supports(partialProfile,context),"covered capital remains part of full exposure even with identical uncovered capital");
    context=partial.context;context.coveredCapitalBands[0]=1;
    check(!Supports(partialProfile,context),"mixed explosion must match all capital it can kill, including committed targets");
    context=partial.context;context.uncoveredCapitalBand=2;
    check(!Supports(partialProfile,context),"larger residual threat cannot borrow partial coverage tolerance");
    context=partial.context;context.uncoveredCoveredCapitalBands[0]=1;
    check(!Supports(partialProfile,context),"larger residual cluster also fails the source coverage envelope");
    context=partial.context;context.committedCoveragePresent=false;
    check(!Supports(partialProfile,context),"in-flight protection is not transferred into a no-protection context");

    TargetGeometry target{1,900,250,880,190,40,80,true,true,true,true};
    check(ClassifyAreaHit({AreaKind::Cob,1,780,250},target)==AreaHit::Hit,
        "cob classification uses actual collider edges rather than object center");
    target.cobTargetKnown=false;
    check(ClassifyAreaHit({AreaKind::Cob,1,900,250},target)==AreaHit::Unknown,
        "missing explicit cob eligibility stays unknown");
    check(ClassifyAreaHit({AreaKind::Cob,3,900,450},target)==AreaHit::Miss,
        "far logical row is a proven miss without guessing target capabilities");
    target.objectYKnown=false;
    check(ClassifyAreaHit({AreaKind::Doom,1,900,250},target)==AreaHit::Unknown,
        "Doom cannot infer object Y from a known collider rectangle");
    check(CapitalBand(24,4)==3 && CapitalBand(16,4)==2,"public nominal price bands are shared");
    check(ThreatBand(1.9f,false)==0 && ThreatBand(4,true)==0 && ThreatBand(3,false)==1,
        "distance and real biting define emergency context");
    check(PurchaseRiskCap(750,48,225,16)==96.5f,"existing paid bodies consume additional risk capacity");
    check(PurchaseRiskCap(144,48,100,0)==24,"bounded affordable worker-sized trial remains possible");
    check(PurchaseRiskCap(750,48,225,113)==0,"parallel unresolved investment cannot reuse same allowance");
    check(PurchaseRiskCap(40,48,100,0)==0,"required cash reserve cannot fund a trial");
    check(PurchaseRiskCap(std::numeric_limits<float>::quiet_NaN(),48,100,0)==0,"invalid numerical cap fails closed");
}
