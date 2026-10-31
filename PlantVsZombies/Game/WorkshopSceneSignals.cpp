#include "WorkshopScene.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
const glm::vec4 kSignalInk{54, 49, 35, 255}; // 信号解释正文颜色
const glm::vec4 kSignalGreen{52, 104, 49, 255}; // 信号解释标题颜色
constexpr int kReasonLinesPerPage = 14; // 每页运行原因的显示行数，长电路可翻页查看完整诊断
void Text(const std::string& text, float x, float y, int size = 16, glm::vec4 color = kSignalInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
}

glm::vec4 WorkshopScene::SignalWireColor(int port, bool on) {
    const auto bright = port ? glm::vec4(255, 166, 55, 255) : glm::vec4(61, 170, 255, 255);
    return on ? bright : glm::vec4(bright.r * .63f, bright.g * .63f, bright.b * .63f, 210);
}

/** 端口身份使用固定颜色、线型与字母；信号变化只改变亮度，不能把 A/B 都改成紫色。 */
void WorkshopScene::DrawSignalWire(Graphics* g, Vector from, Vector to, int port, bool on) {
    const auto color = SignalWireColor(port, on);
    auto delta = to - from; const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length > 0) {
        const auto step = delta * (1 / length);
        if (port == 0) {
            g->DrawLine(from.x, from.y, to.x, to.y, color);
            g->DrawLine(from.x, from.y + 1, to.x, to.y + 1, color);
        } else for (float d = 0; d < length; d += 11) {
            const auto a = from + step * d, b = from + step * std::min(d + 7, length);
            g->DrawLine(a.x, a.y, b.x, b.y, color); g->DrawLine(a.x, a.y + 1, b.x, b.y + 1, color);
        }
    }
    g->FillRect(to.x - 8, to.y - 8, 16, 16, glm::vec4(35, 43, 35, 245));
    g->DrawRect(to.x - 8, to.y - 8, 16, 16, SignalWireColor(port));
    Text(port ? "B" : "A", to.x - 4.5f, to.y - 7, 12, SignalWireColor(port));
}

void WorkshopScene::OpenMachineReason(int index) {
    if (mLoadFailed || index < 0 || index >= Workshop::CellCount || World().Cells()[index].type == Workshop::Machine::Empty
        || (mReasonDialog && mReasonDialog->IsActive())) return;
    CancelMapGesture(); mTooltipLines.clear(); mReasonPage = 0;
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(930, 530);
    builder.Button("返回", Vector(885, 51), Vector(100, 34), 16, [] {});
    builder.Button("上一页", Vector(765, 517), Vector(100, 32), 14, [this] { --mReasonPage; }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("下一页", Vector(885, 517), Vector(100, 32), 14, [this] { ++mReasonPage; }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    mReasonDialog = builder.Show();
    for (int i = 0; i < 3; ++i) mReasonDialog->GetButton(i)->SetTextColor(glm::vec4(255, 226, 135, 255));
    mReasonDialog->SetCustomDraw([this, index](Graphics* g) { DrawMachineReason(g, index); });
    DeltaTime::SetPaused(true);
}

void WorkshopScene::DrawMachineReason(Graphics* g, int index) {
    if (!mReasonDialog || !mReasonDialog->IsActive()) return;
    const auto& cell = World().Cells()[index];
    g->FillRect(85, 35, 930, 530, glm::vec4(237, 227, 188, 255)); g->DrawRect(85, 35, 930, 530, kSignalGreen);
    Text(std::string(Workshop::Describe(cell.type).name) + " · 为什么这样运行", 120, 59, 24, kSignalGreen);
    g->FillRect(120, 106, 395, 31, glm::vec4(42, 50, 43, 255));
    Text("蓝色实线：A　　橙色虚线：B", 133, 112, 16, glm::vec4(247, 229, 168, 255));
    Text("开（高）=条件成立；关（低）=条件不成立", 535, 113, 15);
    std::vector<std::string> lines;
    for (const auto& explanation : World().Explain(index)) {
        std::string line;
        for (size_t pos = 0; pos < explanation.size();) {
            const unsigned char byte = explanation[pos]; const size_t length = byte < 128 ? 1 : byte < 224 ? 2 : byte < 240 ? 3 : 4;
            const auto next = explanation.substr(pos, length);
            if (g->MeasureTextWidth(line + next, ResourceKeys::Fonts::FONT_FZCQ, 15) > 835) { lines.push_back(line); line.clear(); }
            line += next; pos += length;
        }
        lines.push_back(line);
    }
    const int pages = std::max(1, (static_cast<int>(lines.size()) + kReasonLinesPerPage - 1) / kReasonLinesPerPage);
    mReasonPage = std::clamp(mReasonPage, 0, pages - 1);
    for (int row = 0, at = mReasonPage * kReasonLinesPerPage; row < kReasonLinesPerPage && at < static_cast<int>(lines.size()); ++row, ++at)
        Text(lines[at], 120, 155 + row * 25.0f, 15);
    Text("查看时基地暂停；信号逐拍传播，开波后刷新。", 120, 532, 14, kSignalGreen);
    mReasonDialog->GetButton(1)->SetEnabled(mReasonPage > 0); mReasonDialog->GetButton(2)->SetEnabled(mReasonPage + 1 < pages);
    if (pages > 1) { Text(std::format("{} / {}", mReasonPage + 1, pages), 680, 530, 14); mReasonDialog->GetButton(1)->Draw(g); mReasonDialog->GetButton(2)->Draw(g); }
    mReasonDialog->GetButton(0)->Draw(g);
}
