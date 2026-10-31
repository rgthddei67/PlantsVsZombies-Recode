#include "WorkshopScene.h"
#include "Workshop/WorkshopGuide.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <format>

namespace {
const glm::vec4 kPowerInk{58, 52, 35, 255}; // 电网正文颜色
const glm::vec4 kPowerGreen{51, 103, 49, 255}; // 供电充足和当前等级颜色
const glm::vec4 kPowerRed{171, 55, 30, 255}; // 过载提示颜色
void Label(const std::string& text, float x, float y, int size = 16, glm::vec4 color = kPowerInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
}

/** 电网容量只有此购买入口；集光器增加阳光，不直接增加可同时运行的加工设备。 */
void WorkshopScene::OpenPowerGrid() {
    if (mLoadFailed || mUIManager.GetTopActiveMessageBox()) return;
    CancelMapGesture(); mTooltipLines.clear();
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(930, 536);
    builder.Button("返回", Vector(889, 49), Vector(95, 34), 16, [] {});
    builder.Button("升级电网", Vector(690, 488), Vector(282, 36), 17, [this] { Act("grid_upgrade"); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("看调度演示", Vector(127, 488), Vector(200, 36), 17, [this] { OpenGuide(Workshop::PowerGuide); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    mGridDialog = builder.Show(); mDismissibleDialog = mGridDialog;
    for (int i = 0; i < 3; ++i) {
        mGridDialog->GetButton(i)->SetTextColor(glm::vec4(255, 228, 142, 255));
        mGridDialog->GetButton(i)->SetHoverTextColor(glm::vec4(255, 250, 201, 255));
    }
    mGridDialog->SetCustomDraw([this](Graphics* g) { DrawPowerGrid(g); }); DeltaTime::SetPaused(true);
}

void WorkshopScene::DrawPowerGrid(Graphics* g) {
    const auto power = World().Power();
    g->FillRect(85, 32, 930, 536, glm::vec4(235, 225, 184, 255)); g->DrawRect(85, 32, 930, 536, kPowerGreen);
    Label("基地电网 · 全厂自动接电", 125, 53, 25, kPowerGreen);
    Label(World().LegacyAssault() ? "旧袭击保护：本波与重试保留原产速、原出怪；结算后启用新规则。"
        : World().State() == Workshop::Phase::Planning ? "准备期全部停机。下方显示开波后的负载预估，可先关机或接线。"
        : "打开面板时已暂停；关掉多余加工设备，可以把电力让给紧缺产线。", 125, 98, 16);
    Label(std::format("需求 {:.1f} / 容量 {:.0f}     加工速度 {:.0f}%", power.demand, power.capacity, power.speed * 100), 125, 137, 23,
        power.speed < .999f ? kPowerRed : kPowerGreen);
    g->FillRect(125, 175, 840, 13, glm::vec4(117, 112, 81, 255));
    g->FillRect(125, 175, 840 * std::min(1.0f, power.demand / power.capacity), 13, power.speed < .999f ? kPowerRed : kPowerGreen);
    for (size_t i = 0; i < Workshop::GridCapacities.size(); ++i) {
        const float x = 125 + i * 142.0f;
        g->FillRect(x, 208, 132, 95, i + 1 == World().GridLevel() ? glm::vec4(205, 221, 160, 255) : glm::vec4(220, 211, 170, 255));
        Label(std::format("Lv.{} · {:.0f} 电力", i + 1, Workshop::GridCapacities[i]), x + 7, 218, 15, kPowerGreen);
        Label(i == 0 ? "初始电网" : std::format("守住 {} 波", Workshop::GridUnlockWaves[i]), x + 7, 249, 14);
        Label(i + 1 <= World().GridLevel() ? "已接入" : std::format("{} 阳光", Workshop::GridCosts[i]), x + 7, 276, 13);
    }
    Label("工作用额定功率；无料 / 满仓 / 缺阳光时待机用电 15%。手动或 A 口关闭为 0。", 125, 323, 16);
    Label("超载时所有加工同比减速，进度保留。运输、电路不耗电，炮台沿用弹药与充能。", 125, 352, 16);
    Label("集光器只采阳光；加工机器升级的产速增长高于耗电增长。", 125, 381, 16);
    const int stage = World().AssaultStage();
    Label(World().FocusRoute(0) < 0 ? "本波各路均匀来敌；第 3 波起分段主攻，其他路线仍有零星敌人。"
        : std::format("本波主攻：第 {} 路 → 第 {} 路 → 第 {} 路；当前第 {} 段，其他路仍需留弹。",
            World().FocusRoute(0) + 1, World().FocusRoute(1) + 1, World().FocusRoute(2) + 1, stage + 1), 125, 419, 16, kPowerGreen);
    const int level = World().GridLevel();
    auto upgrade = mGridDialog->GetButton(1);
    upgrade->SetEnabled(World().State() != Workshop::Phase::Dead && level < Workshop::UnlockedGridLevel(World().CompletedWaves()) && World().Energy() >= Workshop::GridCosts[level]);
    upgrade->SetText(level == static_cast<int>(Workshop::GridCapacities.size()) ? "电网已满级"
        : level >= Workshop::UnlockedGridLevel(World().CompletedWaves()) ? std::format("守住第 {} 波后可升级", Workshop::GridUnlockWaves[level])
        : std::format("升级至 {:.0f} 电力 · {} 阳光", Workshop::GridCapacities[level], Workshop::GridCosts[level]));
    Label("升级按波次开放，阳光再多也不能越过当前阶段上限。", 125, 455, 14);
    for (int i = 0; i < 3; ++i) mGridDialog->GetButton(i)->Draw(g);
}

/** 三种操作从同一隔离样例开始，走正式 World 生产/接线入口；绝不改动玩家基地。 */
void WorkshopScene::ResetPowerDemo(int mode) {
    using M = Workshop::Machine;
    mPowerDemoMode = mode; mPowerDemo = std::make_unique<Workshop::World>();
    auto save = mPowerDemo->Save(); auto& state = save["state"];
    state["completedWaves"] = 2; state["energy"] = 10000;
    state["lanes"][1]["ammo"] = 60;
    std::string error;
    if (!mPowerDemo->Load(save, error)) { mPowerDemo.reset(); return; }
    for (int row = 0; row < 10; ++row) {
        const int i = row * Workshop::MaxColumns + 2;
        mPowerDemo->Build(i, M::Grower, 0); mPowerDemo->Build(i + 1, M::Feeder, 0);
        mPowerDemo->SetRoute(i + 1, row < 4 ? 0 : 1);
        if (mode == 1 && row >= 4) mPowerDemo->SetMachineRunning(i, false);
    }
    // 三案购买相同元件，比较只改变开关或接线，预算与场地完全一致。
    mPowerDemo->Build(5, M::StockSensor, 0); mPowerDemo->SetRoute(5, 1); mPowerDemo->SetSetting(5, 60);
    mPowerDemo->Build(6, M::Not, 0);
    if (mode == 2) {
        mPowerDemo->Connect(5, 6, 0);
        for (int row = 4; row < 10; ++row) mPowerDemo->Connect(6, row * Workshop::MaxColumns + 2, 0);
    }
    mPowerDemo->StartAssault(); mPowerDemo->Tick(8);
}

void WorkshopScene::DrawPowerDemo(Graphics* g) {
    if (!mPowerDemo) return;
    g->FillRect(276, 312, 750, 192, glm::vec4(220, 224, 184, 255));
    Label("同一基地 · 12 电力 · 10 台育豆机 · 模拟 8 秒", 291, 320, 17, kPowerGreen);
    DrawMachineImage(g, Workshop::Machine::Grower, 298, 358, .95f);
    DrawItemIcon(g, Workshop::Ammo::Pea, 534, 371, 25);
    Label("4 台补第 1 路", 362, 365, 16);
    Label(std::format("新增 {} 颗", mPowerDemo->Lanes()[0].ammo), 576, 365, 20, kPowerGreen);
    Label("6 台补第 2 路", 362, 400, 16);
    Label("已有 60 颗，无需继续囤", 576, 400, 16);
    Label(mPowerDemoMode == 0 ? "全开互相争电，重要路线也变慢。" : mPowerDemoMode == 1 ? "关掉后六台，电力留给第 1 路。" : "库存器 → 非门 → 后六台 A 口；缺弹会自动再开。", 291, 441, 15, kPowerGreen);
    Label("只演示供给，不模拟战斗；随时切换对照，不消耗真实基地资源。", 291, 477, 13);
}
