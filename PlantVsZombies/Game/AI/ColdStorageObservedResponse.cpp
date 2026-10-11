#include "ColdStorageObservedResponse.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>

namespace ColdStorageResponse {
namespace {
constexpr float kEvidenceLifetime=300; // 公开习惯证据最多保留300游戏秒，不跨局继承
constexpr float kMaximumWeight=.65f; // 证据再多也保留至少35%的完整原响应风险权重
constexpr float kWeakTransfer=.5f; // 更贵一档/新经济角色迁移时，整份假设的权重减半
constexpr std::array<float,5> kWindows{2,4,8,16,24}; // 只查询已经实际经历的有限游戏秒窗口
constexpr size_t kMaximumEpisodes=64; // 有界局内历史段数；同遭遇的多段仍只算一票
constexpr size_t kMaximumProfiles=8; // 有界搜索姿态数，不增加每轮实时预算
constexpr float kCherryReach=130; // 正式樱桃水平范围，像素
constexpr float kDoomRadius=250; // 正式毁灭圆形半径，像素
constexpr float kCobRadius=115; // 正式玉米炮圆形半径，像素
bool Finite(float value) { return (std::bit_cast<std::uint32_t>(value)&0x7f800000u)!=0x7f800000u; }
}

AreaHit ClassifyAreaHit(const AreaGeometry& area,const TargetGeometry& target) {
    if(area.kind==AreaKind::Unknown || !Finite(area.x) || !Finite(target.x)) return AreaHit::Unknown;
    if(area.kind==AreaKind::Jalapeno) return area.row==target.row ? AreaHit::Hit : AreaHit::Miss;
    if(area.kind==AreaKind::Cherry) return std::abs(area.row-target.row)<=1
        && std::abs(area.x-target.x)<=kCherryReach ? AreaHit::Hit : AreaHit::Miss;
    if(area.kind==AreaKind::Cob && std::abs(area.row-target.row)>1) return AreaHit::Miss;
    float left=0,top=0,width=0,height=0,radius=0;
    if(area.kind==AreaKind::Doom) {
        if(!target.objectYKnown || !Finite(target.y)) return AreaHit::Unknown;
        left=target.x-25;top=target.y-65;width=50;height=100;radius=kDoomRadius;
    } else if(area.kind==AreaKind::Cob) {
        if(!target.cobTargetKnown) return AreaHit::Unknown;
        if(!target.cobTargetable) return AreaHit::Miss;
        if(!target.boundsKnown) return AreaHit::Unknown;
        left=target.boundsX;top=target.boundsY;width=target.boundsWidth;height=target.boundsHeight;radius=kCobRadius;
    } else return AreaHit::Unknown;
    if(!Finite(area.y) || !Finite(left) || !Finite(top) || !Finite(width) || !Finite(height)
        || width<0 || height<0) return AreaHit::Unknown;
    const float dx=area.x-std::clamp(area.x,left,left+width);
    const float dy=area.y-std::clamp(area.y,top,top+height);
    return dx*dx+dy*dy<=radius*radius ? AreaHit::Hit : AreaHit::Miss;
}

void Tracker::Reset() {
    mEpisodes.clear();mSeenTargets.clear();mHasActive=false;mHadVisible=false;mLastAt=-1;
    mEncounter=mNextEncounter=0;mLastObservation={};
    mOpportunityBegins.fill(-1);++mRevision;
}

void Tracker::Close(float at,bool responded) {
    if(!mHasActive) return;
    mActive.end=std::max(mActive.begin,at);mActive.responded=responded;
    mEpisodes.push_back(mActive);mHasActive=false;++mRevision;
    if(mEpisodes.size()>kMaximumEpisodes) mEpisodes.erase(mEpisodes.begin());
}

void Tracker::Expire(float at) {
    const auto before=mEpisodes.size();
    std::erase_if(mEpisodes,[&](const Episode& episode){return at-episode.end>kEvidenceLifetime;});
    std::erase_if(mSeenTargets,[&](const SeenTarget& target){return at-target.at>kEvidenceLifetime;});
    if(before!=mEpisodes.size()) ++mRevision;
}

void Tracker::Observe(float at,const Observation& observation) {
    if(!Finite(at) || at<0) return;
    if(mLastAt>=0 && at<mLastAt) Reset();
    if(at==mLastAt) return; // 暂停/重复读取不能制造证据或独立遭遇。
    Expire(at);
    // 学习样本可因威胁分箱变化而切段，但仍然合法的同一张牌不能因此重新获得耐心时间。
    for(size_t slot=0;slot<mOpportunityBegins.size();++slot) {
        const bool available=observation.visibleHostiles && (observation.context.readyPaidAreaMask&(1ULL<<slot));
        if(!available) mOpportunityBegins[slot]=-1;
        else if(mOpportunityBegins[slot]<0) mOpportunityBegins[slot]=mLastAt<0 ? at : mLastAt;
        else if(mLastAt>=0) for(float window:kWindows)
            if(mLastAt-mOpportunityBegins[slot]<window && at-mOpportunityBegins[slot]>=window) ++mRevision;
    }
    if(observation.visibleHostiles) {
        if(!mHadVisible) mEncounter=++mNextEncounter;
        // 同一实体从雾中重现不构成新遭遇；只使用曾经公开见到的ID，不查询隐藏实体。
        for(const auto& target:observation.targets) {
            const auto found=std::find_if(mSeenTargets.begin(),mSeenTargets.end(),[&](const SeenTarget& old){return old.id==target.id;});
            if(found==mSeenTargets.end()) mSeenTargets.push_back({target.id,mEncounter,at});
            else {
                const auto older=std::min(mEncounter,found->encounter),newer=std::max(mEncounter,found->encounter);
                if(older!=newer) {
                    for(auto& episode:mEpisodes) if(episode.encounter==newer) episode.encounter=older;
                    for(auto& known:mSeenTargets) if(known.encounter==newer) known.encounter=older;
                    if(mHasActive && mActive.encounter==newer) mActive.encounter=older;
                    mEncounter=older;
                    ++mRevision; // 合并独立票后立即使旧后台权重失效。
                }
                found->at=at;
            }
        }
        // 超出历史容量时保守清学习结果，不能遗忘身份后仍保留可能重复的成功票。
        if(mSeenTargets.size()>4096) {mEpisodes.clear();mSeenTargets.clear();mHasActive=false;++mRevision;}
    }
    if(!observation.visibleHostiles || !observation.eligible || !observation.context.learnablePaidAreaMask
        || observation.context.nearestThreatBand<=0) Close(mLastAt<0 ? at : mLastAt,false);
    else if(!mHasActive || !(mActive.context==observation.context)) {
        Close(mLastAt<0 ? at : mLastAt,false);
        mActive={observation.context,mEncounter,at,at,false};mHasActive=true;++mRevision;
    } else {
        // 跨过已声明窗口才使后台证据代数变化；每帧不能造成无尽取消。
        for(float window:kWindows) if(mActive.end-mActive.begin<window && at-mActive.begin>=window) ++mRevision;
        mActive.end=at;
    }
    mLastAt=at;mHadVisible=observation.visibleHostiles;mLastObservation=observation;
}

void Tracker::PaidAreaCast(float at) {
    if(!Finite(at) || at<0) return;
    if(mLastAt>=0 && at<mLastAt) Reset();
    // 一次真实提前反制足以否定当前耐心假设，不能靠旧的大量未出手记录压住变化。
    mEpisodes.clear();Close(at,true);++mRevision;
    mLastAt=at;
}

Forecast Tracker::BuildForecast(float at) const {
    Forecast result;result.sampledAt=at;result.revision=mRevision;result.sourceExposureSeconds.fill(-1);
    if(!Finite(at) || at<0 || (mLastAt>=0 && at<mLastAt)) return result;
    if(mHasActive) {
        result.currentOpportunity=true;result.currentContext=mActive.context;
        result.currentExposureSeconds=std::max(0.0f,mActive.end-mActive.begin);
    }
    for(size_t slot=0;slot<mOpportunityBegins.size();++slot) if(mOpportunityBegins[slot]>=0)
        result.sourceExposureSeconds[slot]=std::max(0.0f,at-mOpportunityBegins[slot]);
    auto episodes=mEpisodes;if(mHasActive) episodes.push_back(mActive);
    std::vector<Context> contexts;
    for(const auto& episode:episodes) if(at-episode.end<=kEvidenceLifetime
        && std::find(contexts.begin(),contexts.end(),episode.context)==contexts.end()) contexts.push_back(episode.context);
    for(const auto& context:contexts) for(float window:kWindows) {
        if(context.complexPresent) continue; // 一个bool不能区分各种特殊能力；首轮保持未知，不交叉迁移。
        // 同一遭遇的多个来源/相同上下文片段不能被拼成更长的未响应时间。
        // 同遭遇若有早反制，其失败票优先于其他片段的成功票。
        std::map<std::uint64_t,int> votes;
        for(const auto& episode:episodes) {
            if(!(episode.context==context) || at-episode.end>kEvidenceLifetime) continue;
            const float duration=episode.end-episode.begin;
            if(episode.responded && duration<=window) votes[episode.encounter]=-1;
            else if(duration>=window && votes[episode.encounter]!=-1) votes[episode.encounter]=1;
        }
        int total=0,held=0;for(const auto& [id,vote]:votes) if(vote) {++total;held+=vote>0;}
        if(!held) continue;
        const float weight=std::min(kMaximumWeight,static_cast<float>(held)/(total+2.0f));
        result.profiles.push_back({context,window,weight,total,held,false});
    }
    std::stable_sort(result.profiles.begin(),result.profiles.end(),[](const Profile& a,const Profile& b){
        const float av=a.weight*a.windowSeconds,bv=b.weight*b.windowSeconds;
        return av>bv;
    });
    if(result.profiles.size()>kMaximumProfiles/2) result.profiles.resize(kMaximumProfiles/2);
    const auto strict=result.profiles;
    for(auto weak:strict) {weak.allowWeakTransfer=true;weak.weight*=kWeakTransfer;result.profiles.push_back(weak);}
    return result;
}

bool Supports(const Profile& profile,const Context& current) {
    const auto& known=profile.context;
    if(!current.readyPaidAreaMask || !current.learnablePaidAreaMask
        || (current.readyPaidAreaMask&~known.readyPaidAreaMask)
        || (current.learnablePaidAreaMask&~known.learnablePaidAreaMask)
        || current.committedCoveragePresent!=known.committedCoveragePresent
        || current.nearestThreatBand<=0 || current.nearestThreatBand<known.nearestThreatBand
        || current.readyFreeAreaCount<known.readyFreeAreaCount || current.complexPresent || known.complexPresent) return false;
    bool weak=current.exposedCapitalBand>known.exposedCapitalBand
        || current.uncoveredCapitalBand>known.uncoveredCapitalBand
        || (current.economicPresent && !known.economicPresent);
    if(current.exposedCapitalBand>known.exposedCapitalBand+(profile.allowWeakTransfer ? 1 : 0)) return false;
    if(current.uncoveredCapitalBand>known.uncoveredCapitalBand+(profile.allowWeakTransfer ? 1 : 0)) return false;
    for(size_t slot=0;slot<current.coveredCapitalBands.size();++slot) if(current.readyPaidAreaMask&(1ULL<<slot)) {
        const int difference=current.coveredCapitalBands[slot]-known.coveredCapitalBands[slot];
        if(difference>(profile.allowWeakTransfer ? 1 : 0)) return false;
        weak|=difference>0;
        const int uncoveredDifference=current.uncoveredCoveredCapitalBands[slot]-known.uncoveredCoveredCapitalBands[slot];
        if(uncoveredDifference>(profile.allowWeakTransfer ? 1 : 0)) return false;
        weak|=uncoveredDifference>0;
    }
    return !weak || profile.allowWeakTransfer;
}

int CapitalBand(float capital,float cheapest) {
    if(!Finite(capital)||!Finite(cheapest)||capital<=0||cheapest<=0) return 0;
    int band=0;float ceiling=cheapest;
    while(capital>ceiling && band<20) {ceiling*=2;++band;}
    return band;
}

int ThreatBand(float distance,bool eating) {
    if(!Finite(distance)||eating||distance<2) return 0;
    return distance<4 ? 1 : 2;
}

float PurchaseRiskCap(float budget,float reserve,float remainingRisk,float outstandingPaidCapital) {
    if(!Finite(budget)||!Finite(reserve)||!Finite(remainingRisk)||!Finite(outstandingPaidCapital)) return 0;
    return std::max(0.0f,std::min(.25f*std::max(0.0f,budget-reserve),
        .5f*std::max(0.0f,remainingRisk)-std::max(0.0f,outstandingPaidCapital)));
}
}
