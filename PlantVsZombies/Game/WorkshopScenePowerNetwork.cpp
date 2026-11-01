#include "WorkshopScene.h"
#include "../GameApp.h"
#include <format>

namespace {
const glm::vec4 kCable{255, 218, 87, 255}; // 供电线固定黄色，与蓝/橙色信号线区分
const glm::vec4 kNoPower{213, 83, 57, 255}; // 未接电或支路关闭提示
}

bool WorkshopScene::PowerLayerVisible() const {
    return !mFrontView && (mTool == Tool::Power || (mTool == Tool::Inspect && mSelected >= 0
        && (World().Cells()[mSelected].type == Workshop::Machine::PowerBox || Workshop::RatedPower(World().Cells()[mSelected].type) > 0)));
}

void WorkshopScene::SelectPowerStation() {
    CancelTool(); mTool = Tool::Power; mWireSource = Workshop::MainPowerSource;
    mNotice = "总站已选：点击配电箱接入总站，再继续点击机器接电。Esc 结束。";
}

/** 接好箱后保留来源，允许连续点多台机器；点另一箱切换来源，不创建箱间串联。 */
void WorkshopScene::HandlePowerClick(int index) {
    const auto type = World().Cells()[index].type;
    if (type == Workshop::Machine::PowerBox) {
        if (mWireSource == Workshop::MainPowerSource) Act("wire_power", index, "", Workshop::MainPowerSource);
        mWireSource = mSelected = index;
        mNotice = "配电箱已选：继续点击耗电机器，或点另一配电箱换支路。Esc 结束。";
    } else if (World().CanConnectPower(mWireSource, index) && mWireSource >= 0) {
        Act("wire_power", index, "", mWireSource); mSelected = index;
        mNotice = "已接入支路；可继续点击其他机器，Esc 结束接电。";
    } else mNotice = "先点总站接配电箱；再从配电箱给生产机接电。物流和逻辑元件无需供电。";
}

void WorkshopScene::DrawPowerNetwork(Graphics* g) {
    if (mFrontView || mUIManager.GetTopActiveMessageBox()) return;
    const auto network = World().Distribution();
    if (PowerLayerVisible()) {
        g->PushClipRect(kGridX, kGridY, kViewW, kViewH);
        int selectedBox = mWireSource >= 0 ? mWireSource : mSelected;
        if (selectedBox >= 0 && World().Cells()[selectedBox].type != Workshop::Machine::PowerBox)
            selectedBox = World().Cells()[selectedBox].powerSource;
        for (int i = 0; i < Workshop::CellCount; ++i) {
            const auto& c = World().Cells()[i];
            if (c.powerSource == -1) continue;
            const auto end = CellScreenCenter(i / Workshop::MaxColumns, i % Workshop::MaxColumns) + Vector(0, 23 * mMapZoom);
            const auto start = c.powerSource == Workshop::MainPowerSource ? mPowerSourceButton->GetCenter()
                : CellScreenCenter(c.powerSource / Workshop::MaxColumns, c.powerSource % Workshop::MaxColumns) + Vector(0, 23 * mMapZoom);
            const bool focus = mTool == Tool::Power || i == selectedBox || c.powerSource == selectedBox;
            const auto color = focus ? kCable : glm::vec4(166, 146, 77, 125);
            for (int stroke = -1; stroke <= (focus ? 1 : -1); ++stroke)
                g->DrawLine(start.x, start.y + stroke, end.x, end.y + stroke, color);
        }
        g->PopClipRect();
    }
    // 接口固定在下沿中央，信号口仍在上沿；未接电时始终显示醒目标记。
    g->PushClipRect(kGridX, kGridY, kViewW, kViewH);
    for (int i = 0; i < Workshop::CellCount; ++i) {
        const auto& c = World().Cells()[i];
        if (c.type != Workshop::Machine::PowerBox && Workshop::RatedPower(c.type) == 0) continue;
        const bool powered = c.type == Workshop::Machine::PowerBox ? network.branches[i].enabled
            : c.powerSource >= 0 && network.branches[c.powerSource].enabled && network.branches[c.powerSource].speed > 0;
        if (powered && !PowerLayerVisible()) continue;
        const auto p = CellScreenCenter(i / Workshop::MaxColumns, i % Workshop::MaxColumns) + Vector(0, 23 * mMapZoom);
        g->FillRect(p.x - 9, p.y - 8, 18, 16, powered ? glm::vec4(51, 68, 43, 255) : kNoPower);
        g->DrawRect(p.x - 9, p.y - 8, 18, 16, kCable);
        DrawUtilityIcon(g, "power", p.x - 6, p.y - 6, 12);
    }
    g->PopClipRect();
    if (mPowerSourceButton) mPowerSourceButton->Draw(g);
    if (mPowerWireButton) mPowerWireButton->Draw(g);
}
