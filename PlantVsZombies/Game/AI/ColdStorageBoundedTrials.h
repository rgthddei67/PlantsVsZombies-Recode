#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/** 有界实战试探的纯值账本；不预测收益、不支付、不改变正式钱包或单位规则。 */
namespace ColdStorageTrials {
inline constexpr std::size_t MaximumRecords=32; // 最多保留的假设历史；满额不淘汰失败记录以重新领取试探资格
inline constexpr std::size_t MaximumPaidWaves=64; // 每个假设永久保留的付费波数；满额停止复用，不淘汰仍可产冰的旧波
inline constexpr int MaximumCounter=1000000; // 全体假设累计成本和生产各自的安全上限，冰；任一到顶停止新试探
inline constexpr int MaximumPurchaseIce=64; // 单笔试探上限，冰；仍受当前钱包、恢复储备和累计风险限制

/** 累计真实收支不得重置；所有已知付费波保留归因，latestPaidWave仅标识最新付款。 */
struct Record {
    std::uint64_t signature=0;
    int latestPaidWave=0;
    int cost=0,production=0;
    bool unresolved=false;
    float startedAt=0; // 最新一次真实付款的游戏秒，不作为自动结算或退款期限
    std::vector<int> paidWaves; // 严格递增且跨记录唯一；v30迁移仅能保留当时已知的latestPaidWave
    bool operator==(const Record&) const = default;
};

enum class Reason {
    Allowed, InvalidInput, InvalidHistory, Unresolved, FailedHypothesis, HistoryFull,
    WaveAlreadyUsed, BeforePreviousTrial, CounterLimit, NoRisk, OverCap, WaveHistoryFull
};

/** 稳定的诊断名称；不从枚举值推导采购权限。 */
const char* ReasonName(Reason reason);

struct Decision {
    Reason reason=Reason::InvalidInput;
    int cap=0;
    bool Allowed() const { return reason==Reason::Allowed; }
};

/** 只在当前数值有限且非负时复用正式风险额度，再向下取整并应用单笔上限。 */
int PurchaseCap(float wallet,float reserve,float remainingRisk,float outstandingPaidCapital);

/** Board拥有的持久试探历史；搜索仅取得值副本，后台取消和时间流逝不能清空成本或失败记录。 */
class State {
public:
    const std::vector<Record>& Records() const { return mRecords; }
    bool Disabled() const { return mDisabled; }
    /** 损坏或缺失的新格式历史禁止继续试探；禁用位也须入档，仅新关卡的新State重新开放。 */
    void Disable() { mDisabled=true; }

    /** 检查有界历史、唯一签名/波次和时间顺序，不修剪或修正已有成本。 */
    bool Valid() const;
    /** 事务式恢复纯数值记录；非法或超量历史不改记录并永久禁用本State，不能空历史重试恢复额度。 */
    bool Restore(std::span<const Record> records);
    /** 同时只允许一次未回本试探；已实际回本可申请，失败、全局计数或波历史满额不重开。 */
    Reason Availability(std::uint64_t signature) const;
    /** 付款前用当前真实账本重算资格和完整标价；返回值不预留资金，也不能作为未来付款凭据。 */
    Decision CheckPurchase(std::uint64_t signature,int bill,float wallet,float reserve,
        float remainingRisk,float outstandingPaidCapital) const;
    /** 仅登记已经成功支付的正数成本；波次必须比全部历史更新，防止重放和重复付款记账。
     * 调用方须在第一笔付款前调用CheckPurchase，并在真实完整事务成功后调用本接口。
     */
    Reason Commit(std::uint64_t signature,int wave,int actualCost,float at);
    /** 每次钱包实际接纳的生产增量只调用一次，旧新付费波均归原假设；回本即释放未决位。
     * 关闭的亏损记录不再开放；全局生产计数封顶，返回实际登记量，不影响调用方真钱。
     */
    int CreditProduction(int wave,int acceptedAmount);
    /** 实收已覆盖累计成本即可释放；亏损须无实际部队、队列或返回事务才关闭，不退款或清账。 */
    bool Settle(bool hasTroopActivity);

private:
    std::vector<Record> mRecords;
    bool mDisabled=false;
};
}
