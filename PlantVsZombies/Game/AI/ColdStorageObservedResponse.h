#pragma once

#include <cstdint>
#include <array>
#include <vector>

/** 局内公开反应证据；只记录真棋盘经历，不接收推演结果或陪练身份。 */
namespace ColdStorageResponse {
enum class AreaKind { Unknown, Cherry, Jalapeno, Doom, Cob };
/** 已公开攻击的正式形状；Unknown不能借逐行宽包络猜测局部删失。 */
struct AreaGeometry {
    AreaKind kind=AreaKind::Unknown;
    int row=0;
    float x=0,y=0;
};
struct TargetGeometry {
    int row=0;
    float x=0,y=0;
    float boundsX=0,boundsY=0,boundsWidth=0,boundsHeight=0;
    bool objectYKnown=false,boundsKnown=false,cobTargetKnown=false,cobTargetable=false;
};
enum class AreaHit { Unknown, Miss, Hit };
/** 学习和预测共用正式灰烬/炮击几何；调用者单独处理矿石遮挡，未知资格不猜。 */
AreaHit ClassifyAreaHit(const AreaGeometry& area,const TargetGeometry& target);
struct Context {
    std::uint64_t readyPaidAreaMask=0; // legal mask：真正当前可付、直接合法且有可见目标；仅此决定连续源年龄
    int exposedCapitalBand=0; // 可见名义兵价/最便宜合法兵价的向上倍增档，不使用返冰或未来收益
    int nearestThreatBand=0; // 0为紧急，较大的值表示离当前可啃防线更远
    int readyFreeAreaCount=0; // 当前真正READY的免费炮数；在途覆盖目标另从学习见证中剔除
    bool economicPresent=false, complexPresent=false;
    std::array<int,64> coveredCapitalBands{}; // 各卡槽最佳当前合法爆点的公开兵价档，不能把分散编队经验套集中人群
    std::uint64_t learnablePaidAreaMask=0; // 另有至少一个未被已承诺攻击覆盖目标的直接合法落点
    int uncoveredCapitalBand=0; // 全部可见目标中未被当前已承诺攻击覆盖的名义资本；不替代完整资本
    std::array<int,64> uncoveredCoveredCapitalBands{}; // 各来源最佳合法爆点可覆盖的未承诺资本
    bool committedCoveragePresent=false; // 部分覆盖与无覆盖是不同的公开条件，不能互相冒充
    bool operator==(const Context&) const = default;
};
struct Observation {
    struct Target {
        int id=0,row=0;
        float x=0,y=0,health=0,nominalCapital=0;
        bool economic=false,bitePressure=false,committedCovered=false;
    };
    Context context;
    bool eligible=false, visibleHostiles=false;
    std::uint64_t targetSignature=0; // 稳定可见实体身份，仅诊断；同一遭遇内换目标不增加独立票数
    std::vector<Target> targets; // 只保留已通过可见性检查的实战目标，供独立机会审计
};
struct Profile {
    Context context;
    float windowSeconds=0, weight=0;
    int encounters=0, nonResponses=0;
    bool allowWeakTransfer=false; // 弱迁移已统一折减整份世界权重，不按候选再挑概率
};
struct Forecast {
    std::vector<Profile> profiles;
    Context currentContext;
    float sampledAt=0, currentExposureSeconds=0;
    std::array<float,64> sourceExposureSeconds{}; // 各真实卡槽连续机会年龄；负值表示当前没有机会，换上下文不重置
    bool currentOpportunity=false;
    std::uint64_t revision=0;
};

/** 纯值局内记录器：每个连续遭遇对同一窗口/上下文至多贡献一次，删失不变成拒用票。 */
class Tracker {
public:
    Tracker() { mOpportunityBegins.fill(-1); }
    /** 只由正常游戏时间采样调用；无合法机会会结束该段，回拨自动丢弃旧证据。 */
    void Observe(float gameSeconds,const Observation& observation);
    /** 真实付款并启动卡槽后调用；立即撤销旧的乐观证据和后台代数。 */
    void PaidAreaCast(float gameSeconds);
    /** 新局、读档或关闭AI时清证据；普通后台Cancel不能调用。 */
    void Reset();
    std::uint64_t Revision() const { return mRevision; }
    const Observation& LastObservation() const { return mLastObservation; }
    /** 不写记录，不推进游戏时间；诊断/probe读取同样的不可变副本。 */
    Forecast BuildForecast(float gameSeconds) const;
private:
    struct Episode {
        Context context;
        std::uint64_t encounter=0;
        float begin=0,end=0;
        bool responded=false;
    };
    struct SeenTarget { int id=0; std::uint64_t encounter=0; float at=0; };
    void Close(float at,bool responded);
    void Expire(float at);
    std::vector<Episode> mEpisodes;
    std::vector<SeenTarget> mSeenTargets;
    Observation mLastObservation;
    std::array<float,64> mOpportunityBegins;
    Episode mActive;
    bool mHasActive=false, mHadVisible=false;
    float mLastAt=-1;
    std::uint64_t mEncounter=0,mNextEncounter=0,mRevision=1;
};

/** 只在同一已观察工具包络内延后意愿；新工具、紧急威胁和更少免费炮保持原反制。 */
bool Supports(const Profile& profile,const Context& current);
/** 主线程和推演共用威胁分箱；不依赖章节或兵种名字。 */
int CapitalBand(float nominalCapital,float cheapestLegalPrice);
int ThreatBand(float distanceToDefenseCells,bool alreadyEating);
/** 新行为采购额外占用的保守额度；既有活体/付费队列全价先占额度，不允许并发反复豁免。 */
float PurchaseRiskCap(float budget,float reserve,float remainingRisk,float outstandingPaidCapital);
}
