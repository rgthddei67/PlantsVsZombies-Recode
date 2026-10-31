#include "WorkshopScene.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <format>

namespace {
constexpr float kPracticeX = 72, kPracticeY = 232, kPracticeCell = 72; // 练习网格位置与格宽，逻辑像素
const glm::vec4 kPracticeInk{53, 50, 35, 255}; // 练习正文颜色
const glm::vec4 kPracticeGreen{53, 104, 48, 255}; // 当前步骤与成功状态颜色
const glm::vec4 kPracticeGold{255, 226, 133, 255}; // 交互控件和目标格高亮颜色
void Text(const std::string& text, float x, float y, int size = 16, glm::vec4 color = kPracticeInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
Vector Center(int index) { return Vector(kPracticeX + (index % Workshop::MaxColumns + .5f) * kPracticeCell, kPracticeY + (index / Workshop::MaxColumns + .5f) * kPracticeCell); }
}

void WorkshopScene::ClosePractice() {
    if (mPracticeDialog) mPracticeDialog->Close();
    mPracticeDialog.reset(); mPractice.reset(); mPracticeControls.clear(); mPracticeControlSizes.clear(); mPracticeTickAt = 0;
}

void WorkshopScene::OpenPractice(int lesson) {
    if (mLoadFailed || lesson < 0 || lesson > 3 || (lesson >= 2 && World().CompletedWaves() < 2)) return;
    if (mPracticeDialog && mPracticeDialog->IsActive()) {
        mPractice->Reset(lesson); mPracticeTickAt = SDL_GetTicks64(); RefreshPractice(); return;
    }
    CancelMapGesture(); ClearFrontPreview(); mTooltipLines.clear();
    mPractice = std::make_unique<Workshop::Practice>(); mPractice->Reset(lesson); mPracticeTickAt = SDL_GetTicks64();
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(1020, 560);
    std::vector<std::string> keys;
    auto button = [&](const std::string& key, const std::string& label, float x, float y, float width, std::function<void()> action, float height = 32) {
        keys.push_back("practice_" + key);
        mPracticeControlSizes["practice_" + key] = Vector(width, height);
        builder.Button(label, Vector(x, y), Vector(width, height), 15, std::move(action), ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    };
    button("close", "跳过 / 返回", 912, 37, 120, [this] { ClosePractice(); });
    for (int i = 0; i < 4; ++i) button("lesson_" + std::to_string(i), Workshop::Practice::Name(i), 72 + i * 242.0f, 88, 232, [this, i] { OpenPractice(i); });
    const std::array<std::string, 6> tools{"inspect", "belt", "feeder", "wire_a", "wire_b", "disconnect"};
    const std::array<const char*, 6> names{"查看", "运输带", "装填口", "接 A口", "接 B口", "断开输入"};
    for (int i = 0; i < 6; ++i) button(tools[i], names[i], 72 + i * 98.0f, 182, 92, [this, key = tools[i]] { PracticeAction(key); });
    button("run", "运行练习", 754, 182, 130, [this] { PracticeAction("run"); });
    button("tick", "推进1秒", 896, 182, 136, [this] { PracticeAction("tick"); });
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 8; ++col) {
        const int index = row * Workshop::MaxColumns + col;
        button("cell_" + std::to_string(index), "", kPracticeX + col * kPracticeCell, kPracticeY + row * kPracticeCell, kPracticeCell - 2,
            [this, index] { PracticeAction("cell", index); }, kPracticeCell - 2);
    }
    button("route", "目标路线", 675, 272, 170, [this] { PracticeAction("route"); });
    button("route2", "方案2路线", 857, 272, 175, [this] { PracticeAction("route2"); });
    button("filter", "弹种筛选", 675, 315, 170, [this] { PracticeAction("filter"); });
    button("setting_minus", "门槛 -1", 857, 315, 82, [this] { PracticeAction("setting", -1); });
    button("setting_plus", "门槛 +1", 951, 315, 81, [this] { PracticeAction("setting", 1); });
    button("power", "加工电源", 675, 358, 170, [this] { PracticeAction("power"); });
    button("enemy_minus", "僵尸 -1", 857, 405, 82, [this] { if (mPractice) PracticeAction("enemy", mPractice->EnemyCount() - 1); });
    button("enemy_plus", "僵尸 +1", 951, 405, 81, [this] { if (mPractice) PracticeAction("enemy", mPractice->EnemyCount() + 1); });
    button("stock_empty", "设为0颗", 857, 405, 82, [this] { PracticeAction("stock", 0); });
    button("stock_full", "设为5颗", 951, 405, 81, [this] { PracticeAction("stock", 5); });
    button("reset", "重做本课", 682, 530, 130, [this] { if (mPractice) OpenPractice(mPractice->Lesson()); });
    button("next", "完成本课后继续", 830, 530, 202, [this] {
        if (!mPractice || !mPractice->Complete()) return;
        if (mPractice->Lesson() == 3 || (mPractice->Lesson() == 1 && World().CompletedWaves() < 2)) ClosePractice();
        else OpenPractice(mPractice->Lesson() + 1);
    });
    mPracticeDialog = builder.Show();
    for (size_t i = 0; i < keys.size(); ++i) {
        auto control = mPracticeDialog->GetButton(i); control->SetTextColor(kPracticeGold);
        control->SetHoverTextColor(glm::vec4(255, 252, 210, 255)); mPracticeControls.emplace(keys[i], control);
    }
    mPracticeDialog->SetCustomDraw([this](Graphics* g) { DrawPractice(g); });
    RefreshPractice(); DeltaTime::SetPaused(true);
}

bool WorkshopScene::PracticeAction(const std::string& action, int value) {
    if (!mPractice || !mPracticeDialog || !mPracticeDialog->IsActive()) return false;
    const bool result = mPractice->Act(action, value); RefreshPractice(); return result;
}

void WorkshopScene::UpdatePractice() {
    if (!mPractice || !mPracticeDialog || !mPracticeDialog->IsActive()) return;
    const auto now = SDL_GetTicks64();
    const float elapsed = std::min(.1f, static_cast<float>(now - mPracticeTickAt) / 1000); mPracticeTickAt = now;
    if (mPractice->Running() && mUIManager.GetTopActiveMessageBox() == mPracticeDialog) mPractice->Advance(elapsed);
    RefreshPractice();
}

void WorkshopScene::RefreshPractice() {
    if (!mPractice || !mPracticeDialog) return;
    if (mPractice->Complete()) mPracticeFinished[mPractice->Lesson()] = true;
    auto set = [&](const std::string& key, const std::string& text, bool enabled = true) {
        auto& control = mPracticeControls.at("practice_" + key); control->SetText(text); control->SetEnabled(enabled);
    };
    const int lesson = mPractice->Lesson(), selected = mPractice->Selected();
    const Workshop::Cell* c = selected >= 0 ? &mPractice->Model().Cells()[selected] : nullptr;
    for (int i = 0; i < 4; ++i) set("lesson_" + std::to_string(i), i >= 2 && World().CompletedWaves() < 2 ? "守住第2波后练习电路" : std::string(mPracticeFinished[i] ? "已练 · " : "") + Workshop::Practice::Name(i), i < 2 || World().CompletedWaves() >= 2);
    set("belt", "运输带", lesson == 0); set("feeder", "装填口", lesson == 0);
    set("wire_a", "接 A口", lesson >= 2); set("wire_b", "接 B口", lesson >= 2);
    mPracticeControls.at("practice_wire_a")->SetTextColor(SignalWireColor(0));
    mPracticeControls.at("practice_wire_b")->SetTextColor(SignalWireColor(1));
    set("disconnect", "断开输入", c && (c->inputs[0] >= 0 || c->inputs[1] >= 0));
    const bool route = c && (c->type == Workshop::Machine::Feeder || c->type == Workshop::Machine::EnemySensor || c->type == Workshop::Machine::StockSensor);
    set("route", route ? std::format("{}：第{}路", c->type == Workshop::Machine::Feeder ? "方案1" : "监视", c->route + 1) : "先选择机器", route);
    set("route2", c && c->type == Workshop::Machine::Feeder ? std::format("方案2：第{}路", c->route2 + 1) : "方案2路线", lesson == 2 && c && c->type == Workshop::Machine::Feeder);
    set("filter", c ? std::string("检测：") + Workshop::AmmoName(c->filter) : "弹种筛选", lesson == 3 && c && c->type == Workshop::Machine::StockSensor);
    const bool setting = c && c->type == Workshop::Machine::StockSensor;
    set("setting_minus", "门槛 -1", setting && c->setting > 1); set("setting_plus", "门槛 +1", setting && c->setting < 10);
    set("power", c ? std::string("手动电源：") + (c->manual ? "开" : "关") : "手动电源", c && Workshop::RatedPower(c->type, c->level) > 0);
    set("enemy_minus", "僵尸 -1", lesson == 2 && mPractice->EnemyCount() > 0); set("enemy_plus", "僵尸 +1", lesson == 2 && mPractice->EnemyCount() < 6);
    set("stock_empty", "设为0颗", lesson == 3); set("stock_full", "设为5颗", lesson == 3);
    set("run", mPractice->Running() ? "暂停练习" : "运行练习");
    set("next", mPractice->Complete() ? (lesson == 3 ? "完成 / 返回" : "下一课") : "完成本课后继续", mPractice->Complete());
}

void WorkshopScene::DrawPractice(Graphics* g) {
    if (!mPractice || !mPracticeDialog || !mPracticeDialog->IsActive()) return;
    const auto& model = mPractice->Model(); const int lesson = mPractice->Lesson();
    g->FillRect(40, 20, 1020, 560, glm::vec4(235, 225, 184, 255)); g->DrawRect(40, 20, 1020, 560, kPracticeGreen);
    Text("戴夫的动手练习", 72, 40, 25, kPracticeGreen);
    Text("独立样例 · 正式基地暂停 · 不消耗您的资源", 340, 49, 15);
    Text(mPractice->Complete() ? "本课完成" : std::format("第 {} / {} 步", mPractice->Step() + 1, mPractice->StepCount()), 72, 136, 18, kPracticeGreen);
    Text(mPractice->Instruction(), 220, 136, 17);
    if (mPractice->SelectedTool() == Workshop::Practice::Tool::WireA || mPractice->SelectedTool() == Workshop::Practice::Tool::WireB)
        Text(mPractice->Source() < 0 ? "接线：先点信号来源" : "接线：来源已选好，现在点目标机器", 72, 162, 13, kPracticeGreen);
    else Text("蓝色实线 A · 橙色虚线 B · 亮线是开（高），暗线是关（低）", 72, 162, 13);
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 8; ++col) {
        const int index = row * Workshop::MaxColumns + col; const auto center = Center(index);
        const auto& cell = model.Cells()[index];
        g->FillRect(center.x - 36, center.y - 36, 70, 70, glm::vec4(150, 170, 100, 255));
        g->DrawRect(center.x - 36, center.y - 36, 70, 70, glm::vec4(100, 123, 65, 255));
        if (cell.type != Workshop::Machine::Empty) {
            auto visual = cell; visual.plan = model.ActivePlan(index); visual.inputs[1] = -1;
            DrawMachineVisual(g, visual, center.x - 28, center.y - 29, 1, model.Enabled(index), true);
        }
        if (index == mPractice->Selected() || index == mPractice->HighlightCell() || index == mPractice->Source())
            for (int edge = 0; edge < 3; ++edge) g->DrawRect(center.x - 35 + edge, center.y - 35 + edge, 68 - edge * 2, 68 - edge * 2, index == mPractice->HighlightCell() ? kPracticeGold : glm::vec4(75, 206, 225, 255));
    }
    for (int i = 0; i < 3 * Workshop::MaxColumns; ++i) if (i % Workshop::MaxColumns < 8) {
        const auto& c = model.Cells()[i];
        for (int port = 0; port < 2; ++port) if (c.inputs[port] >= 0) {
            const auto from = Center(c.inputs[port]), to = Center(i);
            DrawSignalWire(g, Vector(from.x, from.y - 24), Vector(to.x + (port ? 17 : -17), to.y - 24), port, model.Cells()[c.inputs[port]].signal);
        }
    }
    const int selected = mPractice->Selected();
    if (selected >= 0) {
        const auto& c = model.Cells()[selected];
        Text(Workshop::Describe(c.type).name, 675, 234, 20, kPracticeGreen);
        if (Workshop::IsLogic(c.type)) Text(std::string("输出：") + Workshop::SignalName(c.signal), 830, 239, 14);
        if (c.type == Workshop::Machine::StockSensor) Text(std::format("门槛：{}颗", c.setting), 857, 365, 16);
        if (c.type == Workshop::Machine::Feeder) {
            Text(c.inputs[1] < 0 ? "B 未接线，使用手动方案" : std::string("B 方案信号：") + Workshop::SignalName(model.Cells()[c.inputs[1]].signal), 675, 328, 16);
            Text(std::format("当前方案{} → 第{}路", model.ActivePlan(selected) + 1, model.ActiveRoute(selected) + 1), 675, 365, 16, kPracticeGreen);
        }
        if (Workshop::RatedPower(c.type, c.level) > 0) {
            Text(c.inputs[0] < 0 ? "A 未接线，默认允许运行" : std::string("A 启停信号：") + Workshop::SignalName(model.Cells()[c.inputs[0]].signal), 675, 280, 16);
            Text(std::string("实际状态：") + (model.Enabled(selected) ? "允许运行" : "已停机 · 不耗电"), 675, 322, 16, kPracticeGreen);
        }
    } else Text("点亮框里的机器或空格", 675, 239, 18, kPracticeGreen);
    if (lesson == 2) Text(std::format("第3路僵尸：{}只", mPractice->EnemyCount()), 675, 413, 17);
    if (lesson == 3) Text(std::format("第3路库存：{}颗", model.Lanes()[2].ammo), 675, 413, 17);
    const auto power = model.Power();
    Text(std::format("电力 {:.1f}/{:.0f} · 加工 {:.0f}%", power.demand, power.capacity, power.speed * 100), 675, 458, 17, power.speed < .99f ? glm::vec4(177, 62, 28, 255) : kPracticeGreen);
    Text(std::format("弹仓：第2路 {}颗 · 第3路 {}颗", model.Lanes()[1].ammo, model.Lanes()[2].ammo), 675, 488, 15);
    g->PushClipRect(72, 464, 576, 58);
    // 课程提示按中文字符分行；主指令始终独占顶部一行。
    std::string line; float y = 469;
    const auto hint = mPractice->Hint();
    for (size_t p = 0; p < hint.size();) {
        const unsigned char byte = hint[p]; const size_t length = byte < 128 ? 1 : byte < 224 ? 2 : byte < 240 ? 3 : 4;
        const auto next = hint.substr(p, length);
        if (g->MeasureTextWidth(line + next, ResourceKeys::Fonts::FONT_FZCQ, 14) > 565) { Text(line, 72, y, 14); y += 21; line.clear(); }
        line += next; p += length;
    }
    Text(line, 72, y, 14); g->PopClipRect();
    g->PushClipRect(72, 529, 596, 34); Text(mPractice->Notice(), 72, 537, 14, kPracticeGreen); g->PopClipRect();
    const std::string highlighted = "practice_" + mPractice->HighlightControl();
    for (const auto& [key, control] : mPracticeControls) {
        if (key.starts_with("practice_cell_")) continue;
        const auto type = selected < 0 ? Workshop::Machine::Empty : model.Cells()[selected].type;
        if ((key == "practice_belt" || key == "practice_feeder") && lesson != 0) continue;
        if ((key == "practice_wire_a" || key == "practice_wire_b" || key == "practice_disconnect") && lesson < 2) continue;
        if (key == "practice_route" && type != Workshop::Machine::Feeder && type != Workshop::Machine::EnemySensor && type != Workshop::Machine::StockSensor) continue;
        if (key == "practice_route2" && (lesson != 2 || type != Workshop::Machine::Feeder)) continue;
        if ((key == "practice_filter" || key == "practice_setting_minus" || key == "practice_setting_plus") && (lesson != 3 || type != Workshop::Machine::StockSensor)) continue;
        if (key == "practice_power" && (selected < 0 || Workshop::RatedPower(model.Cells()[selected].type, model.Cells()[selected].level) == 0)) continue;
        if ((key == "practice_stock_empty" || key == "practice_stock_full") && lesson != 3) continue;
        if ((key == "practice_enemy_minus" || key == "practice_enemy_plus") && lesson != 2) continue;
        control->Draw(g);
        if (key == highlighted) {
            const auto center = control->GetCenter(); const auto size = mPracticeControlSizes.at(key);
            for (int edge = 0; edge < 2; ++edge) g->DrawRect(center.x - size.x / 2 - edge, center.y - size.y / 2 - edge, size.x + edge * 2, size.y + edge * 2, kPracticeGreen);
        }
    }
}
