#include "MendingCotton.h"
#include "Game/Board/Board.h"
#include "Graphics.h"
#include <algorithm>
#include <cmath>

void MendingCotton::SetupPlant() {
    mPlantHealth = mPlantMaxHealth = MendingCottonRules::Health;
}

/** 推进一次治疗冷却，按八邻格的生命比例选取并立即治疗一个存活目标。 */
void MendingCotton::PlantUpdate() {
    if (mIsPreview || !mBoard) return;
    const float dt = DeltaTime::GetDeltaTime();
    mThreadRemaining = std::max(0.0f, mThreadRemaining-dt);
    mRemaining = std::max(0.0f, mRemaining-dt);
    if (mRemaining > 0) return;
    Plant* target = nullptr;
    // 逻辑格确定范围；同格三层分别选疗，稳定 ID 打破比例并列，避免注册顺序影响结果。
    for (int row=std::max(0,mRow-1); row<=std::min(mBoard->mRows-1,mRow+1); ++row)
        for (int col=std::max(0,mColumn-1); col<=std::min(mBoard->mColumns-1,mColumn+1); ++col) {
            if (row==mRow && col==mColumn) continue;
            auto* cell=mBoard->GetCell(row,col);
            if (!cell) continue;
            for (int id : {cell->GetNormalPlantID(),cell->GetPumpkinPlantID(),cell->GetUnderPlantID()}) {
                auto* p=mBoard->mEntityRegistry.GetPlant(id);
                if (!p || !p->CanReceiveHealing()) continue;
                const int64_t lhs=target ? int64_t(p->mPlantHealth)*target->mPlantMaxHealth : 0;
                const int64_t rhs=target ? int64_t(target->mPlantHealth)*p->mPlantMaxHealth : 0;
                if (!target || lhs<rhs || (lhs==rhs && p->mPlantID<target->mPlantID)) target=p;
            }
        }
    if (!target || target->RestoreHealth(MendingCottonRules::Amount)<=0) return;
    mRemaining=MendingCottonRules::Interval;
    mLastHealedID=target->mPlantID;
    ++mHealCount;
    mThreadEnd=target->GetVisualAnchorPosition()+Vector(0,-20);
    mThreadRemaining=.45f;
}

/** 绘制本体及连接上次治疗位置的短暂棉线，不参与伤害或回血结算。 */
void MendingCotton::Draw(Graphics* g) {
    Plant::Draw(g);
    if (!g || mThreadRemaining<=0) return;
    const Vector start=GetVisualAnchorPosition()+Vector(0,-23);
    const float alpha=255*std::min(1.0f,mThreadRemaining/.15f);
    // 细棉线只展示已提交的治疗，不持有目标指针，也不延迟真实回血。
    Vector last=start;
    for(int i=1;i<=12;++i) {
        const float t=i/12.0f;
        const Vector next(start.x+(mThreadEnd.x-start.x)*t,
            start.y+(mThreadEnd.y-start.y)*t+std::sin(t*3.14159265f)*13);
        g->DrawLine(last.x,last.y+1,next.x,next.y+1,glm::vec4(93,117,102,alpha));
        g->DrawLine(last.x,last.y,next.x,next.y,glm::vec4(255,248,217,alpha));
        last=next;
    }
    g->DrawCircle(mThreadEnd.x,mThreadEnd.y,7,glm::vec4(237,255,211,alpha),16);
}

void MendingCotton::SaveExtraData(nlohmann::json& j) const {
    j["healRemaining"]=mRemaining; j["healCount"]=mHealCount; j["lastHealedID"]=mLastHealedID;
}

/** 恢复有界冷却及提交计数；已发生的治疗线不在读档时重播。 */
void MendingCotton::LoadExtraData(const nlohmann::json& j) {
    const float remaining=j.value("healRemaining",MendingCottonRules::Interval);
    mRemaining=std::isfinite(remaining) ? std::clamp(remaining,0.0f,MendingCottonRules::Interval) : MendingCottonRules::Interval;
    mHealCount=std::max(0,j.value("healCount",0)); mLastHealedID=j.value("lastHealedID",NULL_PLANT_ID);
    mThreadRemaining=0;
}
