#include "WorkshopScene.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
constexpr float kDragThreshold = 8; // 左键移动超过此逻辑像素距离才进入拖图，过滤点击抖动
constexpr float kKeyboardPanSpeed = 400; // 方向键平移速度，逻辑像素/秒；暂停时仍可操作视图
const glm::vec4 kRemoveColor{164, 44, 37, 255}; // 拆除模式警示底色
const glm::vec4 kWireColor{94, 65, 137, 255}; // 接线模式底色
const glm::vec4 kHoverColor{91, 217, 245, 255}; // 鼠标指向的机器；与已选中金框区分
void Label(const std::string& value, float x, float y, int size = 15, glm::vec4 color = glm::vec4(255, 249, 219, 255)) {
    GameAPP::GetInstance().DrawText(value, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}

void Cross(Graphics* g, Vector center, float radius, glm::vec4 color) {
    for (int stroke = -1; stroke <= 1; ++stroke) {
        g->DrawLine(center.x - radius, center.y - radius + stroke, center.x + radius, center.y + radius + stroke, color);
        g->DrawLine(center.x - radius, center.y + radius + stroke, center.x + radius, center.y - radius + stroke, color);
    }
}
}

bool WorkshopScene::IsFactoryPointer(Vector point) const {
    return point.x >= kGridX && point.x < kGridX + kViewW && point.y >= kGridY && point.y < kGridY + kViewH;
}
int WorkshopScene::HoveredMachine() const {
    if (mFrontView || mUIManager.GetTopActiveMessageBox() || mMapDragging || mLeftPanning) return -1;
    const int index = FactoryCellAt(GameAPP::GetInstance().GetInputHandler().GetMousePosition());
    return index >= 0 && World().Cells()[index].type != Workshop::Machine::Empty ? index : -1;
}
void WorkshopScene::CancelMapGesture() {
    mMapDragging = mLeftPending = mLeftPanning = mSuppressGridUntilRelease = false;
    mPressCell = -1;
}
void WorkshopScene::CancelTool() {
    mTool = Tool::Inspect; mWireSource = -1; mSelected = -1;
    CancelMapGesture(); mHoverKey = -1; mTooltipLines.clear();
    mNotice = "查看模式：指向机器按 R 旋转；左键拖地图，轻点选择。";
}
void WorkshopScene::SelectTool(Tool tool) {
    if (mTool == tool || tool == Tool::Inspect) { CancelTool(); return; }
    CancelTool(); mTool = tool;
    mNotice = tool == Tool::Remove ? "拆除模式：点击拆一台后退出；Shift 连拆，Esc / 右键取消。"
        : "接线模式：先点来源，再点目标；Esc / 右键取消。";
}
const char* WorkshopScene::ToolName() const {
    switch (mTool) {
    case Tool::Build: return "建造";
    case Tool::Remove: return "拆除";
    case Tool::WireA: return "接 A 线";
    case Tool::WireB: return "接 B 线";
    default: return "查看";
    }
}

/** R 永远优先作用于指向的实体；建造时指向空地才转预览，刚放下的机器可直接旋转。 */
void WorkshopScene::RotatePointedMachine() {
    if (mLeftPending || mMapDragging || mUIManager.GetTopActiveMessageBox()) return;
    int target = HoveredMachine();
    if (target < 0 && mTool == Tool::Build) {
        if (Workshop::HasDirectionalOutput(mBuildType)) {
            mDirection = (mDirection + 1) % 4;
            mBuildDirections[static_cast<size_t>(mBuildType)] = mDirection;
        } else mNotice = "此设备没有出料方向，无需旋转。";
        return;
    }
    if (target < 0) target = mSelected;
    if (target < 0 || World().Cells()[target].type == Workshop::Machine::Empty) return;
    mSelected = target;
    if (!Workshop::HasDirectionalOutput(World().Cells()[target].type)) {
        mNotice = "此设备没有出料方向；装填口请设置目标路线。"; return;
    }
    if (Act("rotate", target)) {
        if (mTool == Tool::Build && World().Cells()[target].type == mBuildType) {
            mDirection = World().Cells()[target].direction;
            mBuildDirections[static_cast<size_t>(mBuildType)] = mDirection;
        }
        mNotice = std::string("已旋转：朝") + Workshop::DirectionName(World().Cells()[target].direction) + "出料。";
        mHoverKey = -1; mTooltipLines.clear();
    }
}

/** 左键松开才提交格位操作；一旦识别为拖动，本次手势无论在哪松开都不施工。 */
void WorkshopScene::UpdateFactoryInput() {
    auto& input = GameAPP::GetInstance().GetInputHandler();
    const auto point = input.GetMousePosition();
    if (input.IsMouseButtonPressed(SDL_BUTTON_MIDDLE) && IsFactoryPointer(point)) {
        CancelMapGesture(); mMapDragging = true; mSuppressGridUntilRelease = true; mDragPoint = point;
    }
    if (mMapDragging && input.IsMouseButtonDown(SDL_BUTTON_MIDDLE)) {
        mMapOffset = mMapOffset + point - mDragPoint; mDragPoint = point; ClampFactoryCamera();
    } else mMapDragging = false;
    if (!mSuppressGridUntilRelease && input.IsMouseButtonPressed(SDL_BUTTON_LEFT) && IsFactoryPointer(point)) {
        mLeftPending = true; mLeftPanning = false; mLeftStart = point; mLeftOffset = mMapOffset;
        mPressCell = FactoryCellAt(point); mPressTool = mTool;
    }
    if (mLeftPending && (input.IsMouseButtonDown(SDL_BUTTON_LEFT) || input.IsMouseButtonReleased(SDL_BUTTON_LEFT))) {
        const auto delta = point - mLeftStart;
        if (delta.x * delta.x + delta.y * delta.y >= kDragThreshold * kDragThreshold) mLeftPanning = true;
        if (mLeftPanning) { mMapOffset = mLeftOffset + delta; ClampFactoryCamera(); }
    }
    const auto offsetBefore = mMapOffset;
    const float zoomBefore = mMapZoom;
    if (IsFactoryPointer(point) && input.GetMouseWheel() != 0) ZoomFactory(mMapZoom * std::pow(1.15f, input.GetMouseWheel()), point);
    const float distance = kKeyboardPanSpeed * DeltaTime::GetFixedStep();
    if (input.IsKeyDown(SDLK_LEFT)) mMapOffset.x += distance;
    if (input.IsKeyDown(SDLK_RIGHT)) mMapOffset.x -= distance;
    if (input.IsKeyDown(SDLK_UP)) mMapOffset.y += distance;
    if (input.IsKeyDown(SDLK_DOWN)) mMapOffset.y -= distance;
    ClampFactoryCamera();
    if (mLeftPending && (zoomBefore != mMapZoom || offsetBefore.x != mMapOffset.x || offsetBefore.y != mMapOffset.y)) {
        mLeftPanning = true; mLeftStart = point; mLeftOffset = mMapOffset;
    }
    if (mSuppressGridUntilRelease) {
        if (!input.IsMouseButtonDown(SDL_BUTTON_LEFT) && !input.IsMouseButtonDown(SDL_BUTTON_MIDDLE)) mSuppressGridUntilRelease = false;
        return;
    }
    if (mLeftPending && input.IsMouseButtonReleased(SDL_BUTTON_LEFT)) {
        const int index = FactoryCellAt(point);
        const bool click = !mLeftPanning && mPressTool == mTool && index >= 0 && index == mPressCell;
        mLeftPending = mLeftPanning = false; mPressCell = -1;
        if (click) HandleGridClick(index);
    } else if (mLeftPending && !input.IsMouseButtonDown(SDL_BUTTON_LEFT)) {
        // 焦点丢失导致 InputHandler 重置时取消悬而未决的点击，不在回到窗口后补做。
        mLeftPending = mLeftPanning = false; mPressCell = -1;
    }
}

std::string WorkshopScene::WireHint(int index) const {
    if (index < 0 || World().Cells()[index].type == Workshop::Machine::Empty) return "请选择一台机器，空地不能接线";
    if (mWireSource < 0) {
        if (mTool == Tool::WireB && !Workshop::IsLogic(World().Cells()[index].type)) return "B 口来源需要逻辑信号";
        return "点击设为来源，再点目标机器";
    }
    const int port = mTool == Tool::WireB ? 1 : 0;
    if (World().CanConnect(mWireSource, index, port)) return "点击连接；Shift 保留来源继续接";
    const auto type = World().Cells()[index].type;
    if (port == 1 && type != Workshop::Machine::And && type != Workshop::Machine::Or && type != Workshop::Machine::Latch)
        return "此机器没有 B 口，请换目标";
    return "库存来源只能接库存器 A 口";
}

/** 状态使用文字、工具按钮描边、目标轮廓和光标标签共同表达，不能只依靠底部颜色。 */
void WorkshopScene::DrawInteraction(Graphics* g) {
    if (mFrontView || mLoadFailed) return;
    const bool removing = mTool == Tool::Remove, wiring = mTool == Tool::WireA || mTool == Tool::WireB;
    const auto color = removing ? kRemoveColor : wiring ? kWireColor : glm::vec4(45, 91, 63, 255);
    g->FillRect(kGridX, 128, kViewW, 34, color);
    Label(std::string(ToolName()) + (mTool == Tool::Inspect ? "模式" : "中"), 27, 135, 19);
    const std::string instruction = removing ? "点一台后退出 · Shift 连拆 · Esc / 右键取消"
        : wiring ? (mWireSource < 0 ? "先点来源，再点目标 · Esc / 右键取消" : "已选来源，请点目标 · Shift 连续接线 · Esc 取消")
        : mTool == Tool::Build ? std::string(Workshop::Describe(mBuildType).name) + " · R 旋转指向的机器 / 空地预览 · Esc 结束"
        : "指向机器按 R 旋转 · 轻点选择 · 按住左键拖地图";
    Label(instruction, 132, 138, 14);
    if (mTool == Tool::Inspect || removing || wiring) {
        const auto button = removing ? mRemoveButton : mTool == Tool::WireA ? mWireAButton : mTool == Tool::WireB ? mWireBButton : mInspectButton;
        const float width = wiring ? 90 : 75;
        if (button) {
            const auto center = button->GetCenter();
            for (int edge = 0; edge < 3; ++edge) g->DrawRect(center.x - width / 2 - edge, center.y - 15 - edge, width + edge * 2, 30 + edge * 2, color);
        }
    }
    if (mUIManager.GetTopActiveMessageBox()) return;
    const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
    const bool panning = mMapDragging || mLeftPanning;
    const int hover = HoveredMachine();
    g->PushClipRect(static_cast<int>(kGridX), static_cast<int>(kGridY), static_cast<int>(kViewW), static_cast<int>(kViewH));
    if (removing) for (int edge = 0; edge < 3; ++edge) g->DrawRect(kGridX + edge, kGridY + edge, kViewW - edge * 2, kViewH - edge * 2, kRemoveColor);
    if (!panning && hover >= 0) {
        const auto center = CellScreenCenter(hover / Workshop::MaxColumns, hover % Workshop::MaxColumns);
        const float size = kCellW * mMapZoom;
        const auto outline = removing ? glm::vec4(255, 94, 71, 255) : kHoverColor;
        for (int edge = 0; edge < 2; ++edge) g->DrawRect(center.x - size / 2 + edge, center.y - size / 2 + edge, size - edge * 2, size - edge * 2, outline);
        if (removing) Cross(g, center, size * 0.32f, outline);
        else if (!wiring && Workshop::HasDirectionalOutput(World().Cells()[hover].type)) {
            for (int port = 0; port < (World().Cells()[hover].type == Workshop::Machine::Splitter ? 2 : 1); ++port) {
                const auto output = World().InspectOutput(hover, port == 1);
                const int direction = (World().Cells()[hover].direction + port) % 4;
                const Vector step(direction == 0 ? size : direction == 2 ? -size : 0, direction == 1 ? size : direction == 3 ? -size : 0);
                const auto end = center + step;
                const auto routeColor = output.ready ? glm::vec4(101, 237, 137, 255) : glm::vec4(255, 172, 55, 255);
                for (int stroke = -1; stroke <= 1; ++stroke) g->DrawLine(center.x, center.y + stroke, end.x, end.y + stroke, routeColor);
                g->DrawRect(end.x - size / 2 + 3, end.y - size / 2 + 3, size - 6, size - 6, routeColor);
                if (!output.ready) Cross(g, end, 8, routeColor);
                Label("出料", center.x + step.x * 0.5f - 14, center.y + step.y * 0.5f - 17, 12, routeColor);
            }
        }
    }
    if (wiring && mWireSource >= 0 && !panning) {
        const auto source = CellScreenCenter(mWireSource / Workshop::MaxColumns, mWireSource % Workshop::MaxColumns);
        const bool valid = hover >= 0 && World().CanConnect(mWireSource, hover, mTool == Tool::WireB ? 1 : 0);
        g->DrawLine(source.x, source.y, mouse.x, mouse.y, valid ? glm::vec4(113, 237, 129, 255) : glm::vec4(246, 160, 75, 255));
        g->FillCircle(source.x, source.y, 5, glm::vec4(241, 221, 255, 255));
    }
    g->PopClipRect();
    if (!IsFactoryPointer(mouse)) return;
    std::string title, detail;
    auto badgeColor = color;
    if (panning) { title = "拖动地图"; detail = "松手结束，本次不会施工"; badgeColor = glm::vec4(43, 65, 78, 245); }
    else if (removing) {
        title = hover >= 0 ? std::string("拆除：") + Workshop::Describe(World().Cells()[hover].type).name : "拆除模式";
        detail = hover >= 0 ? std::format("最多退 {} 阳光，丢弃 {} 颗弹药", World().RemoveRefund(hover), World().Cells()[hover].TotalStock()) : "点机器拆除 · Esc 取消";
    } else if (wiring) { title = mWireSource < 0 ? "接线：选择来源" : "接线：选择目标"; detail = WireHint(hover); }
    else if (mTooltipLines.empty() && hover >= 0 && Workshop::HasDirectionalOutput(World().Cells()[hover].type)) {
        title = std::string("R 旋转这台 · 朝") + Workshop::DirectionName(World().Cells()[hover].direction) + "出料";
        detail = World().InspectOutput(hover).message;
    }
    if (title.empty()) return;
    const float x = std::clamp(mouse.x + 18, 18.0f, 805.0f), y = std::clamp(mouse.y + 22, 170.0f, 530.0f);
    g->FillRect(x, y, 277, 53, badgeColor); g->DrawRect(x, y, 277, 53, glm::vec4(255, 232, 178, 255));
    Label(title, x + 9, y + 6, 15); Label(detail, x + 9, y + 30, 13);
}
