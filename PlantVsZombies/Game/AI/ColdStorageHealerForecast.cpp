#include "ColdStorageHealerForecast.h"
#include "ColdStorageSearch.h"
#include "Game/Zombie/HealerRules.h"

#include <algorithm>
#include <cmath>

namespace ColdStorageSearch {
namespace {
/** 分离总生命中真实本体份额；纸盾尚存不能替已死本体继续领取治疗。 */
float BodyHealth(const Unit& unit)
{
    const float balloon=unit.balloon.present ? unit.balloon.health : 0;
    return std::max(0.0f, unit.body.health-unit.helmHealth-unit.shieldHealth-balloon);
}

/** 仅处理已出生且仍有头的活动目标；不会修复未出生或已经脱落的层。 */
bool Alive(const Unit& unit, float time)
{
    return unit.body.spawnAt<=time && unit.body.health>0
        && BodyHealth(unit)>unit.temporalStopHealth;
}

/** 当前仍在的层才能回血；零防具是已经脱落，不能从最大值重造。 */
bool Damaged(float current, float maximum)
{
    return current>0 && current<maximum;
}

/** 查询最低尚存生命比例，与实体的单疗排序保持同一含义。 */
float LowestRatio(const Unit& unit)
{
    float ratio=1;
    const auto check=[&](float current,float maximum) {
        if(current>0 && maximum>0) ratio=std::min(ratio,current/maximum);
    };
    check(BodyHealth(unit),unit.maximumBody);
    check(unit.helmHealth,unit.maximumHelm);
    check(unit.shieldHealth,unit.maximumShield);
    return ratio;
}

/** 用采样行中心及实际碰撞框中心计算距离，不把邻行整体当作圆形治疗范围。 */
float CenterY(const Snapshot& snapshot, const Unit& unit)
{
    const int row=unit.body.row;
    if(row<0 || row>=snapshot.rows || row>=static_cast<int>(snapshot.rowY.size())) return 0;
    float rowY=snapshot.rowY[row]!=0 ? snapshot.rowY[row] : (row+.5f)*snapshot.cellHeight;
    const int first=row*snapshot.columns;
    if(first>=0 && first+snapshot.columns<=static_cast<int>(snapshot.cellY.size())
        && snapshot.columns>0 && snapshot.cellWidth>0 && snapshot.cellY[first]!=0) {
        // 与主推演的行高口径一致；屋顶高度按当前X落在相邻格中心之间插值。
        const float column=std::clamp((unit.body.x-snapshot.gridLeft)/snapshot.cellWidth-.5f,
            0.0f,static_cast<float>(snapshot.columns-1));
        const int left=static_cast<int>(column),right=std::min(left+1,snapshot.columns-1);
        rowY=snapshot.cellY[first+left]+(snapshot.cellY[first+right]-snapshot.cellY[first+left])*(column-left);
    }
    return rowY+unit.boundsY+unit.boundsHeight*.5f;
}

/** 圆形治疗范围包含边界；body.x 的具体参考点用碰撞偏移还原。 */
bool InRange(const Snapshot& snapshot, const Unit& source, const Unit& target, float radius)
{
    const float dx=target.body.x+target.body.boundsOffset+target.body.boundsWidth*.5f
        -(source.body.x+source.body.boundsOffset+source.body.boundsWidth*.5f);
    const float dy=CenterY(snapshot,target)-CenterY(snapshot,source);
    return dx*dx+dy*dy<=radius*radius;
}

/** 仅选择仍有实际可修复层的同阵营目标；此预测集合已经由 Board 排除魅惑侧。 */
bool Eligible(const Snapshot& snapshot, const Unit& source, const Unit& target, float time, float radius)
{
    return Alive(target,time) && InRange(snapshot,source,target,radius)
        && (Damaged(BodyHealth(target),target.maximumBody)
            || Damaged(target.helmHealth,target.maximumHelm)
            || Damaged(target.shieldHealth,target.maximumShield));
}

/** 活来源已经锁定的单疗目标不能被重复预留；失效来源即刻释放其预留。 */
bool Reserved(const std::vector<Unit>& units, const Unit& source, int targetID, float time)
{
    return std::any_of(units.begin(),units.end(),[&](const Unit& other) {
        return &other!=&source && other.healer.present && other.healer.enabled && Alive(other,time)
            && other.healer.phase==HealerRules::Forecast::Phase::FOCUSED
            && other.healer.focusedTargetID==targetID;
    });
}

/** 按实体确定性规则选群疗或最危急单体；稳定 ID 打破生命比例并列。 */
bool SelectTreatment(const Snapshot& snapshot, float time, Unit& source, const std::vector<Unit>& units)
{
    int areaCount=0;
    const Unit* focused=nullptr;
    for(const auto& target:units) {
        if(Eligible(snapshot,source,target,time,HealerRules::AreaRadius)) ++areaCount;
        if(&target==&source || !Eligible(snapshot,source,target,time,HealerRules::FocusedRadius)
            || Reserved(units,source,target.id,time)) continue;
        const bool locked=snapshot.stationHijackerID>0 && target.id==snapshot.stationHijackerID;
        const bool currentLocked=snapshot.stationHijackerID>0 && focused && focused->id==snapshot.stationHijackerID;
        if(!focused || (locked && !currentLocked)
            || (locked==currentLocked && (LowestRatio(target)<LowestRatio(*focused)
                || (LowestRatio(target)==LowestRatio(*focused) && target.id<focused->id)))) focused=&target;
    }
    auto& healer=source.healer;
    if(areaCount>=HealerRules::AreaWoundedThreshold) {
        healer.phase=HealerRules::Forecast::Phase::AREA;
        healer.focusedTargetID=0;
    } else if(focused) {
        healer.phase=HealerRules::Forecast::Phase::FOCUSED;
        healer.focusedTargetID=focused->id;
    } else {
        healer.retry=HealerRules::RetryDelay;
        return false;
    }
    healer.remaining=HealerRules::CastDuration;
    return true;
}

/** 三层独立封顶并同步能力拥有的防具余值；已经碎掉的装备绝不重新生成。 */
float Repair(Unit& target, float amount)
{
    const auto increase=[&](float current,float maximum) {
        return Damaged(current,maximum) ? std::min(amount,maximum-current) : 0;
    };
    const float body=increase(BodyHealth(target),target.maximumBody);
    const float helm=increase(target.helmHealth,target.maximumHelm);
    const float shield=increase(target.shieldHealth,target.maximumShield);
    target.helmHealth+=helm;
    target.shieldHealth+=shield;
    target.body.health+=body+helm+shield;
    if(target.adaptiveHelmet>0) target.adaptiveHelmet+=helm;
    if(target.ritual.armor>0) target.ritual.armor+=helm;
    if(target.repair.health>0) target.repair.health+=helm;
    return body+helm+shield;
}

/** 前摇完成后重查圆形范围与锁定身份；兑现回血不依赖来源下一步是否存活。 */
void Resolve(const Snapshot& snapshot, float time, Unit& source, std::vector<Unit>& units,
    const std::vector<float>& initialHealth, ConstructionStats& stats)
{
    auto& healer=source.healer;
    const bool area=healer.phase==HealerRules::Forecast::Phase::AREA;
    int recipients=0;
    for(size_t index=0;index<units.size();++index) {
        auto& target=units[index];
        if(!area && (&target==&source || target.id!=healer.focusedTargetID)) continue;
        if(!Eligible(snapshot,source,target,time,area ? HealerRules::AreaRadius : HealerRules::FocusedRadius)) continue;
        const float restored=Repair(target,area ? HealerRules::AreaHealAmount : HealerRules::FocusedHealAmount);
        if(restored<=0) continue;
        ++recipients;
        stats.healerAmount+=restored;
        const float maximum=target.maximumBody+target.maximumHelm+target.maximumShield;
        const float denominator=index<initialHealth.size() ? initialHealth[index] : maximum;
        // 真正治回且仍可生产的工人也是协作进展；只供继续探索，不凭回血增加收入或最终得分。
        if(target.body.economic && target.body.health>target.productionStopHealth)
            stats.workerProtectionProgress+=target.body.purchaseCost*restored/std::max(1.0f,denominator);
        const float recovered=std::min(target.blastCredit,
            target.body.purchaseCost*restored/std::max(1.0f,denominator));
        target.blastCredit-=recovered;
        stats.healerRecoveryCredit+=recovered;
    }
    healer.phase=HealerRules::Forecast::Phase::IDLE;
    healer.focusedTargetID=0;
    healer.remaining=0;
    if(recipients>0) {
        ++stats.healerCasts;
        stats.healerRecipients+=recipients;
        healer.cooldown=HealerRules::Cooldown;
        healer.retry=0;
    } else healer.retry=HealerRules::RetryDelay;
}
}

void AdvanceHealers(const Snapshot& snapshot, float time, std::vector<Unit>& units,
    const std::vector<float>& initialHealth, ConstructionStats& stats)
{
    std::vector<size_t> sources;
    for(size_t index=0;index<units.size();++index) {
        units[index].healer.movementActivity=1;
        if(units[index].healer.present) sources.push_back(index);
    }
    std::stable_sort(sources.begin(),sources.end(),[&](size_t left,size_t right) {
        return units[left].id<units[right].id;
    });
    for(const auto index:sources) {
        auto& source=units[index];
        auto& healer=source.healer;
        if(source.body.spawnAt>time) continue;
        if(!Alive(source,time) || BodyHealth(source)<=healer.disableBodyHealth) {
            healer.enabled=false;
            healer.phase=HealerRules::Forecast::Phase::IDLE;
            healer.remaining=0;
            healer.focusedTargetID=0;
        }
        if(!healer.enabled) continue;
        // 硬控只暂停有效行动；治疗自己的停步由独立活动比例表示，不反馈到硬控计时。
        const float active=std::max(0.0f,HealerForecastStep-source.body.stopped);
        const float rate=source.body.slow>0 ? .5f : 1;
        float available=active*rate;
        float walking=0;
        while(available>0) {
            if(healer.phase!=HealerRules::Forecast::Phase::IDLE) {
                const float consumed=std::min(available,std::max(0.0f,healer.remaining));
                available-=consumed;
                healer.remaining-=consumed;
                if(healer.remaining>0) break;
                Resolve(snapshot,time,source,units,initialHealth,stats);
                continue;
            }
            if(healer.cooldown>0 || healer.retry>0) {
                float& remaining=healer.cooldown>0 ? healer.cooldown : healer.retry;
                const float consumed=std::min(available,remaining);
                remaining-=consumed;
                available-=consumed;
                walking+=consumed;
                if(remaining>0) break;
                // 正式冷却归零的同一更新就开始前摇；步尾归零不能再多等一整预测步。
                if(healer.cooldown<=0 && healer.retry<=0) SelectTreatment(snapshot,time,source,units);
                continue;
            }
            if(!SelectTreatment(snapshot,time,source,units)) continue;
        }
        healer.movementActivity=active>0 ? std::clamp(walking/(active*rate),0.0f,1.0f) : 0;
    }
}
}
