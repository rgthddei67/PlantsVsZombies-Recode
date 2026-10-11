#include "ColdStorageAssaultIntent.h"
#include <bit>
#include <algorithm>
#include <limits>

namespace ColdStorageSearch {
namespace {
/** Release 也启用 fast-math；按浮点位型拒绝非有限时间，不能依赖被优化掉的 isfinite。 */
bool FiniteTime(double value) {
    return (std::bit_cast<std::uint64_t>(value)&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL;
}

/** 拒绝坏投影，不能把负数或过量就绪炮修成一次有利的真实战场变化。 */
bool ValidObservation(const AssaultObservation& observation) {
    return observation.activeSources>=0 && observation.readySources>=0
        && observation.readySources<=observation.activeSources && observation.committedShots>=0
        && observation.latestLaunchedCobID>=0;
}
}

bool AssaultIntentState::Arm(double now,const AssaultObservation& observation) {
    return Begin(now,observation,{},false);
}

bool AssaultIntentState::ArmConditional(double now,const AssaultObservation& observation,const AssaultTrigger& trigger) {
    return Begin(now,observation,trigger,true);
}

bool AssaultIntentState::Begin(double now,const AssaultObservation& observation,const AssaultTrigger& trigger,bool conditional) {
    if(!FiniteTime(now) || now<0 || (mActive && now<mLastObservedAt) || !ValidObservation(observation)
        || !FiniteTime(trigger.earliestSeconds) || !FiniteTime(trigger.expirySeconds)
        || trigger.earliestSeconds<0 || trigger.expirySeconds<=0 || trigger.maxReadyCobs< -1
        || trigger.earliestSeconds>=std::min(trigger.expirySeconds,LifetimeSeconds)) {
        Clear();
        return false;
    }
    // 调用方区分真实付款与一次性等待锚；先使上一轮令牌失效。
    Clear();
    if(mRevision==std::numeric_limits<std::uint64_t>::max()) return false;
    mActive=true;
    mArmedAt=mLastObservedAt=now;
    mObservation=observation;
    mConditional=conditional;
    mAnchorLaunchedCobID=observation.latestLaunchedCobID;
    mTrigger=trigger;
    mTrigger.expirySeconds=std::min(mTrigger.expirySeconds,LifetimeSeconds);
    return true;
}

bool AssaultIntentState::Observe(double now,const AssaultObservation& observation) {
    if(!mActive) return false;
    if(!FiniteTime(now) || now<mLastObservedAt || now-mArmedAt>=mTrigger.expirySeconds
        || !ValidObservation(observation)) {
        Clear();
        return false;
    }
    mLastObservedAt=now;
    // 离膛身份必须实际在锚之后出现；前摇、来源移除和锚前已经飞行的弹都不算新发射。
    if(observation.latestLaunchedCobID>mAnchorLaunchedCobID) mObservedLaunch=true;
    const bool timedEdge=mConditional && !mTimeEdgeObserved && now-mArmedAt>=mTrigger.earliestSeconds;
    mTimeEdgeObserved|=timedEdge;
    if(observation==mObservation && !timedEdge) return false;
    // 签名与来源/已发弹计数共同表示实际相位变化；不按余时预报任何开火或落地事件。
    mObservation=observation;
    if(mRevision==std::numeric_limits<std::uint64_t>::max()) {
        Clear();
        return false;
    }
    ++mRevision;
    return NeedsReplan();
}

bool AssaultIntentState::Eligible() const {
    return mActive && (!mConditional || (mLastObservedAt-mArmedAt>=mTrigger.earliestSeconds
        && (!mTrigger.requireObservedCobLaunch || mObservedLaunch)
        && (!mTrigger.requireShotsClear || mObservation.committedShots==0)
        && (mTrigger.maxReadyCobs<0 || mObservation.readySources<=mTrigger.maxReadyCobs)));
}

bool AssaultIntentState::MarkReplanned(std::uint64_t revision) {
    if(!NeedsReplan() || revision<=mReplannedRevision || revision>mRevision) return false;
    mReplannedRevision=revision;
    ++mReplans;
    return true;
}

void AssaultIntentState::Clear() {
    mActive=false;
    mConditional=mTimeEdgeObserved=mObservedLaunch=false;
    mAnchorLaunchedCobID=0;
    mTrigger={};
    mArmedAt=mLastObservedAt=0;
    mObservation={};
    mReplans=0;
    // 不复用旧生命周期的令牌；极限溢出时保持关闭，不能让极旧结果碰巧匹配。
    if(mRevision<std::numeric_limits<std::uint64_t>::max()) ++mRevision;
    mReplannedRevision=mRevision;
}
}
