#include "ColdStorageObservedForecast.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <vector>

namespace ColdStorageObservedForecast {
namespace {
constexpr float kFogVisibleAlpha=96; // Board公开索敌浓雾阈值，alpha

bool Finite(float value) { return (std::bit_cast<std::uint32_t>(value)&0x7f800000u)!=0x7f800000u; }

/** 与已有快照相同的逐格连续坡面；对象Y仍由可选显式投影提供，不能从碰撞箱猜Doom锚点。 */
float RowY(const ColdStorageSearch::Snapshot& s,int row,float x)
{
    const int first=row*s.columns;
    if(s.cellY[first]!=0) {
        const float column=std::clamp((x-s.gridLeft)/s.cellWidth-.5f,0.0f,static_cast<float>(s.columns-1));
        const int left=static_cast<int>(column),right=std::min(left+1,s.columns-1);
        return s.cellY[first+left]+(s.cellY[first+right]-s.cellY[first+left])*(column-left);
    }
    return s.rowY[row]!=0 ? s.rowY[row] : (row+.5f)*s.cellHeight;
}

bool Occupies(const ColdStorageSearch::Plant& plant,int row,int column)
{
    return plant.health>0 && plant.row==row && column>=plant.column
        && column<plant.column+(plant.layer==1 ? std::max(1,plant.footprintColumns) : 1);
}

/** 首版不把任意两种复杂机制视作相同响应；能力阶段再弱也不借另一种机制的经验。 */
bool Complex(const ColdStorageSearch::Unit& unit)
{
    return unit.engineer || unit.burst.range>0 || unit.repair.interval>0 || unit.drum.enabled
        || unit.sniper.enabled || unit.ritual.present || unit.goldenDrive.enabled || unit.clock.present
        || unit.dance.leader || unit.dance.backup || unit.throwHealth>0 || unit.body.smashSeconds>0
        || unit.healer.present || unit.catapult.present || unit.jack.present || unit.balloon.present
        || unit.digger.present || unit.ladder.present || unit.jammer || unit.hijacker || unit.insulator
        || unit.pressure || unit.floodMortar || unit.instantVehicleCrush || unit.vehicleCrush
        || unit.blocksShieldBypass || unit.shieldedHitCap>0 || unit.shieldedAshCap>0
        || unit.adaptiveHelmet>0 || unit.mowerImmune || unit.consumesOtherMowers || !unit.groundHazard;
}

/** 只有Board明确标注的新樱桃/辣椒/Doom来源可参与；不推断未知卡或预存/免费能力。 */
bool KnownPaid(const ColdStorageSearch::Counter& counter)
{
    return counter.observedSourceKey>=1 && counter.observedSourceKey<=64 && !counter.blast.committed
        && counter.consumesPlant && !counter.stored && !counter.targeted && counter.plantID==0
        && counter.flightSeconds<=0 && (counter.sunCost>0 || counter.iceCost>0)
        && (counter.clearsCell || counter.clearsLadderRow || counter.ladderClearRadius==1);
}

/** 当前已付优惠只影响本步支付，不提前使用订单/收入或未来优惠。 */
float IceCost(const View& v,const ColdStorageSearch::Counter& counter)
{
    return v.time<v.snapshot.discountRemaining && counter.iceCost>0
        ? std::ceil(static_cast<float>(counter.iceCost)/ColdStorageSkillRules::DiscountDivisor)
        : static_cast<float>(counter.iceCost);
}

bool Ready(const View& v,int source)
{
    return source>=0 && static_cast<size_t>(source)<v.sourceReady.size()
        && Finite(v.sourceReady[source]) && v.sourceReady[source]<=v.time;
}

/** 不铲普通层；未提供完整合法性投影时只沿用采样时已空的合法格和仍存活的承载层。 */
bool DirectPlacement(const View& v,const ColdStorageSearch::Counter& counter,size_t index)
{
    const auto& s=v.snapshot;
    const int row=counter.cellRow,column=counter.cellColumn;
    if(row<0 || row>=s.rows || column<0 || column>=s.columns) return false;
    if(std::any_of(v.blocks.begin(),v.blocks.end(),[&](const Block& block) {
        return block.row==row && block.column==column && v.time<block.until;
    })) return false;
    for(const auto& plant:v.plants) if(Occupies(plant,row,column)) {
        if(plant.layer==1) return false;
        if(counter.sharedSource>=0 && (plant.layer<0 || plant.layer>2)) return false;
    }
    if(!v.directPlacementLegal.empty())
        return index<v.directPlacementLegal.size() && v.directPlacementLegal[index]!=0;
    for(const auto& original:s.plants) if(Occupies(original,row,column)) {
        if(original.layer==1 || (counter.sharedSource>=0 && (original.layer<0 || original.layer>2))) return false;
        if(original.layer==0 && std::none_of(v.plants.begin(),v.plants.end(),[&](const auto& current) {
            return current.id==original.id && current.layer==0 && Occupies(current,row,column);
        })) return false;
    }
    return true;
}

/** 对象坐标与实际碰撞箱分开投影；缺失的出生资格/碰撞箱不由兵种或价格补猜。 */
ColdStorageResponse::TargetGeometry Geometry(const View& v,const ColdStorageSearch::Unit& unit)
{
    const float x=unit.body.x+unit.body.blastAnchorOffset;
    const float rowY=RowY(v.snapshot,unit.body.row,x);
    ColdStorageResponse::TargetGeometry result;
    result.row=unit.body.row;result.x=x;result.y=rowY+unit.responseObjectYOffset;
    result.objectYKnown=unit.responseObjectYKnown && Finite(unit.responseObjectYOffset);
    result.boundsX=unit.body.x+unit.body.boundsOffset;result.boundsY=rowY+unit.boundsY;
    result.boundsWidth=unit.body.boundsWidth;result.boundsHeight=unit.boundsHeight;
    result.boundsKnown=result.objectYKnown && unit.responseCobTargetKnown;
    result.cobTargetKnown=unit.responseCobTargetKnown;result.cobTargetable=unit.responseCobTargetable;
    return result;
}

/** 新付费牌由Board的明确来源和形状能力标记识别；中心来自当前合法格。 */
ColdStorageResponse::AreaGeometry DirectArea(const View& v,const ColdStorageSearch::Counter& counter)
{
    using ColdStorageResponse::AreaKind;
    const auto& s=v.snapshot;
    const float y=s.cellY[counter.cellRow*s.columns+counter.cellColumn]!=0
        ? s.cellY[counter.cellRow*s.columns+counter.cellColumn] : RowY(s,counter.cellRow,counter.blast.x);
    return {counter.clearsLadderRow ? AreaKind::Jalapeno : counter.clearsCell ? AreaKind::Doom : AreaKind::Cherry,
        counter.cellRow,counter.blast.x,y};
}
}

Opportunity Inspect(const View& v)
{
    Opportunity result;
    const auto& s=v.snapshot;
    if(s.rows<=0 || s.rows>6 || s.columns<=0 || s.columns>54/s.rows || !Finite(v.time) || v.time<0
        || !Finite(s.cellWidth) || s.cellWidth<=0 || !Finite(s.rightEdge) || !Finite(v.sceneBottom)
        || !Finite(v.sun) || !Finite(v.ice)) return result;
    result.directPaidCounters.resize(v.counters.size());
    float cheapest=(std::numeric_limits<float>::max)();
    for(const auto& option:s.options) if(option.device<0 && option.cost>0)
        cheapest=std::min(cheapest,static_cast<float>(option.cost));
    const bool knownCheapest=cheapest<(std::numeric_limits<float>::max)();
    std::vector<size_t> visible;
    std::vector<unsigned char> committedCovered(v.units.size());
    std::vector<ColdStorageResponse::TargetGeometry> geometry(v.units.size());
    std::array<float,6> front;
    front.fill(s.gridLeft+s.cellWidth*.5f);
    for(const auto& plant:v.plants) if(plant.health>0 && plant.edible && plant.row>=0 && plant.row<s.rows)
        front[plant.row]=std::max(front[plant.row],plant.x);
    float capital=0,uncoveredCapital=0,nearest=(std::numeric_limits<float>::max)();
    bool eating=false,unknownCapital=false;
    for(size_t i=0;i<v.units.size();++i) {
        const auto& unit=v.units[i];const auto& body=unit.body;
        if(body.row<0 || body.row>=s.rows || !Finite(body.health) || !Finite(body.spawnAt)
            || body.health<=0 || body.spawnAt>v.time || unit.headless
            || (unit.digger.present && !unit.digger.IsInteractive())) continue;
        const float x=body.x+body.blastAnchorOffset;
        if(!Finite(x) || x<0 || x>s.rightEdge) continue;
        const int column=std::clamp(static_cast<int>((x-s.gridLeft)/s.cellWidth),0,s.columns-1);
        const auto* fog=v.fogAlpha ? v.fogAlpha : s.weatherStation ? &s.stationFogAlpha : nullptr;
        if(fog && (!Finite((*fog)[body.row*s.columns+column])
            || (*fog)[body.row*s.columns+column]>kFogVisibleAlpha)) continue;
        if(unit.responseObjectYKnown) {
            const float objectY=RowY(s,body.row,x)+unit.responseObjectYOffset;
            if(!Finite(objectY) || objectY<0 || objectY>v.sceneBottom) continue;
        }
        visible.push_back(i);
        if(!Finite(unit.responseNominalCapital) || unit.responseNominalCapital<=0) unknownCapital=true;
        else capital+=unit.responseNominalCapital;
        const float distance=(x-front[body.row])/s.cellWidth;
        nearest=std::min(nearest,distance);eating|=body.eating>0 || distance<=2;
        result.context.economicPresent|=body.economic;
        result.context.complexPresent|=Complex(unit);
        geometry[i]=Geometry(v,unit);
        for(const auto& pending:v.pending) {
            const auto hit=ColdStorageResponse::ClassifyAreaHit(pending.responseArea,geometry[i]);
            result.unknownGeometry|=hit==ColdStorageResponse::AreaHit::Unknown;
            committedCovered[i]|=hit==ColdStorageResponse::AreaHit::Hit;
        }
        result.censored|=committedCovered[i]!=0;
        if(!committedCovered[i] && Finite(unit.responseNominalCapital) && unit.responseNominalCapital>0)
            uncoveredCapital+=unit.responseNominalCapital;
    }
    result.visibleHostiles=!visible.empty();
    result.context.exposedCapitalBand=ColdStorageResponse::CapitalBand(capital,cheapest);
    result.context.uncoveredCapitalBand=ColdStorageResponse::CapitalBand(uncoveredCapital,cheapest);
    result.context.committedCoveragePresent=result.censored;
    result.context.nearestThreatBand=ColdStorageResponse::ThreatBand(nearest,eating);
    std::vector<int> freeSources;
    for(size_t c=0;c<v.counters.size();++c) {
        const auto& counter=v.counters[c];
        if(counter.blast.committed) continue;
        if(!counter.consumesPlant && counter.plantID!=0 && counter.sunCost==0 && counter.iceCost==0
            && Ready(v,counter.source) && std::find(freeSources.begin(),freeSources.end(),counter.source)==freeSources.end()
            && std::any_of(v.plants.begin(),v.plants.end(),[&](const auto& p) {
                return p.id==counter.plantID && p.health>0 && p.shutdownUntil<=v.time;
            })) freeSources.push_back(counter.source);
        if(!KnownPaid(counter) || !Ready(v,counter.source)
            || (counter.sharedSource>=0 && !Ready(v,counter.sharedSource))
            || counter.sunCost<0 || counter.iceCost<0 || counter.sunCost>v.sun || IceCost(v,counter)>v.ice
            || !DirectPlacement(v,counter,c)) continue;
        bool hits=false,uncoveredHits=false;float covered=0,uncovered=0;
        const auto area=DirectArea(v,counter);
        for(size_t index:visible) {
            const auto hit=ColdStorageResponse::ClassifyAreaHit(area,geometry[index]);
            result.unknownGeometry|=hit==ColdStorageResponse::AreaHit::Unknown;
            // 缺投影不证明原合法机会已经消失；保守保留年龄，且本步完全不授权延期。
            if(hit==ColdStorageResponse::AreaHit::Unknown) hits=true;
            if(hit!=ColdStorageResponse::AreaHit::Hit) continue;
            hits=true;
            const float nominal=v.units[index].responseNominalCapital;
            if(Finite(nominal) && nominal>0) covered+=nominal;
            if(!committedCovered[index]) {
                uncoveredHits=true;
                if(Finite(nominal) && nominal>0) uncovered+=nominal;
            }
        }
        if(!hits) continue;
        // 同一来源另一个落点仅能打已承诺目标时，不得借该来源的远处机会延期。
        result.directPaidCounters[c]=uncoveredHits;
        const size_t slot=static_cast<size_t>(counter.observedSourceKey-1);
        result.context.readyPaidAreaMask|=std::uint64_t{1}<<slot;
        if(uncoveredHits) result.context.learnablePaidAreaMask|=std::uint64_t{1}<<slot;
        result.context.coveredCapitalBands[slot]=std::max(result.context.coveredCapitalBands[slot],
            ColdStorageResponse::CapitalBand(covered,cheapest));
        result.context.uncoveredCoveredCapitalBands[slot]=std::max(result.context.uncoveredCoveredCapitalBands[slot],
            ColdStorageResponse::CapitalBand(uncovered,cheapest));
    }
    result.context.readyFreeAreaCount=static_cast<int>(freeSources.size());
    result.eligible=result.visibleHostiles && knownCheapest && !unknownCapital && !result.unknownGeometry
        && !result.context.complexPresent && result.context.learnablePaidAreaMask && result.context.nearestThreatBand>0;
    return result;
}

WorldClock::WorldClock(const ColdStorageResponse::Forecast& forecast) : mAges(forecast.sourceExposureSeconds)
{
    for(size_t slot=0;slot<mAges.size();++slot) {
        if(Finite(mAges[slot]) && mAges[slot]>=0) mAvailable|=std::uint64_t{1}<<slot;
        else mAges[slot]=-1;
    }
}

void WorldClock::Update(float time,const Opportunity& opportunity)
{
    if(!Finite(time) || time<mLastTime) {mValid=false;return;}
    const float elapsed=time-mLastTime;
    const auto available=opportunity.visibleHostiles ? opportunity.context.readyPaidAreaMask : 0;
    for(size_t slot=0;slot<mAges.size();++slot) {
        const auto bit=std::uint64_t{1}<<slot;
        if(!(available&bit)) mAges[slot]=-1;
        else if(mAvailable&bit) mAges[slot]+=elapsed;
        else mAges[slot]=0;
    }
    mLastTime=time;mAvailable=available;
}

std::uint64_t WorldClock::DeferredSources(const ColdStorageResponse::Profile& profile,const Opportunity& opportunity) const
{
    if(!mValid || !opportunity.eligible || !Finite(profile.windowSeconds) || profile.windowSeconds<=0
        || !ColdStorageResponse::Supports(profile,opportunity.context)) return 0;
    std::uint64_t result=0;
    for(size_t slot=0;slot<mAges.size();++slot) if((mAvailable&opportunity.context.learnablePaidAreaMask&(std::uint64_t{1}<<slot))
        && mAges[slot]>=0 && mAges[slot]<profile.windowSeconds) result|=std::uint64_t{1}<<slot;
    return result;
}

float WorldClock::SourceAge(int sourceKey) const
{
    return sourceKey>=1 && sourceKey<=64 ? mAges[sourceKey-1] : -1;
}
}
