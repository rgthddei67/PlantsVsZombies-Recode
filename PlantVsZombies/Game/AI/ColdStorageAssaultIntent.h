#pragma once
#include <cstdint>

namespace ColdStorageSearch {
/** 主线程提供的公开、已发生观测，不保存实体指针或预测结果。
 * signature 只编码稳定来源/已发弹身份及离散相位，须与容器枚举顺序无关；
 * 不可混入逐帧变化的装填余时、坐标、生命或预计落地时刻。
 * 活炮和在途弹独立计数：来源移除不代表它已经发出的炮弹消失。
 */
struct AssaultObservation {
    std::uint64_t signature=0;
    int activeSources=0;
    int readySources=0;
    int committedShots=0;
    int latestLaunchedCobID=0; // 当前可见已离膛玉米弹的最大稳定 ID；炮体 FIRING 不算离膛
    bool operator==(const AssaultObservation&) const = default;
};

/** 未付款后队的公开触发门槛；所有秒数相对真实付款或显式等待锚，不保存预测战果。 */
struct AssaultTrigger {
    double earliestSeconds=0, expirySeconds=45;
    bool requireObservedCobLaunch=false, requireShotsClear=false;
    int maxReadyCobs=-1;
};

/** 未付款后队的有界观测生命周期，只由拥有 Planner 的主线程访问。
 * 不持有后队动作，不读取实体、推断炮击、支付、加分或写存档。
 * 任一炮的变化只请求按完整现况重评，不宣称其他炮或在途弹已经安全。
 * Arm 只能由调用方确认的新实际付款建立；显式等待锚另由 Planner 限制一次，观测和重评不得续期。
 * Cancel、换图和读档须调用 Clear；专门唤醒额度不限制常规 AI 搜索。
 */
class AssaultIntentState {
public:
    static constexpr double LifetimeSeconds=45.0; // 每笔新实际付款后的最长观测期，游戏秒
    static constexpr unsigned MaxReplans=6; // 每个观测期最多消费六次专门重评，不限制常规搜索

    /** 在实际付款后捕获初始观测，建立新的期限和响应额度；初始状态本身不触发事件。
     * 非有限、负数或相对当前生命周期倒退的游戏时间，以及非法计数，均清空并拒绝建立。
     */
    bool Arm(double now,const AssaultObservation& observation);
    /** 建立有界条件锚；expiry 最多45游戏秒，earliest 仅触发一次重评，不预言炮弹安全。 */
    bool ArmConditional(double now,const AssaultObservation& observation,const AssaultTrigger& trigger);
    /** 记录公开离散变化；仅在新事件且尚有专门重评额度时返回一次 true。
     * 重复观测只推进有效期检查；到期、倒退/非法时间或非法观测直接清空，不产生事件。
     * 后台繁忙时可稍后通过 NeedsReplan 读取未消费需求，不能只依赖返回值。
     */
    bool Observe(double now,const AssaultObservation& observation);
    /** 消费调用方确认已经完整处理的捕获代及以前事件；保留其后到来的事件，重复/过期令牌无效。
     * 预算未轮到或结果过期时不得调用；完整比较或明确非法拒绝才消费响应额度。
     */
    bool MarkReplanned(std::uint64_t revision);
    /** 清空瞬态观测、时间和次数并使旧令牌失效；revision 只作单调令牌保留，不入存档。 */
    void Clear();

    bool Active() const {return mActive;}
    bool NeedsReplan() const {return mActive && mReplans<MaxReplans && mReplannedRevision<mRevision;}
    std::uint64_t Revision() const {return mRevision;}
    unsigned ReplanCount() const {return mReplans;}
    double ArmedAt() const {return mArmedAt;}
    double ExpiresAt() const {return mArmedAt+mTrigger.expirySeconds;}
    bool Conditional() const {return mConditional;}
    bool ObservedLaunch() const {return mObservedLaunch;}
    /** 只准许将未付款提案交给新搜索；不授予付款权，其他就绪炮和独立在途弹逐项门禁。 */
    bool Eligible() const;
    const AssaultObservation& Observation() const {return mObservation;}

private:
    /** 共用付款/等待锚初始化；非法时间或条件失效旧令牌，不保留半个生命周期。 */
    bool Begin(double now,const AssaultObservation& observation,const AssaultTrigger& trigger,bool conditional);
    bool mActive=false;
    bool mConditional=false, mTimeEdgeObserved=false, mObservedLaunch=false;
    int mAnchorLaunchedCobID=0;
    AssaultTrigger mTrigger;
    double mArmedAt=0;
    double mLastObservedAt=0;
    AssaultObservation mObservation;
    std::uint64_t mRevision=0;
    std::uint64_t mReplannedRevision=0;
    unsigned mReplans=0;
};
}
