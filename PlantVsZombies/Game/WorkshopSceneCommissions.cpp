#include "WorkshopScene.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
const glm::vec4 kTaskPaper{240, 230, 191, 255}; // 委托板底色
const glm::vec4 kTaskInk{62, 54, 36, 255}; // 任务正文颜色
const glm::vec4 kTaskGreen{59, 102, 47, 255}; // 达标及标题颜色
void TaskText(const std::string& text, float x, float y, int size = 16, glm::vec4 color = kTaskInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
std::string TimeLabel(float seconds) {
    const int value = static_cast<int>(std::ceil(seconds));
    return std::format("{:02}:{:02}", value / 60, value % 60);
}
}

void WorkshopScene::CloseCommissions() {
    if (mCommissionsDialog) mCommissionsDialog->Close();
    mCommissionsDialog.reset(); mCommissionNotice.clear();
}

/** 使用正式模态输入和暂停；刷新/截止时钟均由 World 的袭击 Tick 推进。 */
void WorkshopScene::OpenCommissions() {
    if (mLoadFailed || (mCommissionsDialog && mCommissionsDialog->IsActive())) return;
    CancelMapGesture(); ClearFrontPreview(); mTooltipLines.clear();
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(1020, 560);
    builder.Button("返回", Vector(934, 39), Vector(100, 32), 16, [this] { CloseCommissions(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    for (int i = 0; i < Workshop::CommissionOfferCount; ++i) {
        const float x = 65 + i % 2 * 318.0f, y = 133 + i / 2 * 126.0f;
        builder.Button("接取", Vector(x + 198, y + 79), Vector(92, 30), 15,
            [this, i] { Act("commission_accept", i); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    for (int i = 0; i < Workshop::CommissionActiveLimit; ++i) {
        const float y = 133 + i * 174.0f;
        builder.Button("领取奖励", Vector(728, y + 124), Vector(128, 30), 15,
            [this, i] { Act("commission_claim", i); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
        builder.Button("放弃", Vector(904, y + 124), Vector(106, 30), 15,
            [this, i] {
                const auto& active = World().Tasks().Active();
                if (i >= static_cast<int>(active.size())) return;
                const auto id = active[i].id;
                mDismissibleDialog = GameMessageBox::Builder(Vector(550, 300)).Title("放弃委托？").Message("进度将被清除，本次刷新内不能重新接取。")
                    .Button("取消", Vector(0, 0), Vector(130, 40), 17, [] {})
                    .Button("确认放弃", Vector(0, 0), Vector(170, 40), 17, [this, id] {
                        const auto& current = World().Tasks().Active();
                        for (size_t at = 0; at < current.size(); ++at) if (current[at].id == id) { Act("commission_abandon", static_cast<int>(at)); break; }
                    }).Show();
            }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    mCommissionsDialog = builder.Show();
    for (int i = 0; i < 1 + Workshop::CommissionOfferCount + 2 * Workshop::CommissionActiveLimit; ++i) {
        mCommissionsDialog->GetButton(i)->SetTextColor(glm::vec4(255, 228, 142, 255));
        mCommissionsDialog->GetButton(i)->SetHoverTextColor(glm::vec4(255, 250, 201, 255));
    }
    mCommissionsDialog->SetCustomDraw([this](Graphics* g) { DrawCommissions(g); });
    RefreshCommissions(); DeltaTime::SetPaused(true);
}

void WorkshopScene::RefreshCommissions() {
    if (!mCommissionsDialog || !mCommissionsDialog->IsActive()) return;
    const auto& tasks = World().Tasks();
    for (int i = 0; i < Workshop::CommissionOfferCount; ++i) {
        const bool available = tasks.Offers()[i].status == Workshop::CommissionStatus::Offered;
        mCommissionsDialog->GetButton(1 + i)->SetText(available ? "接取" : "已接取");
        mCommissionsDialog->GetButton(1 + i)->SetEnabled(available && tasks.ActiveCount() < Workshop::CommissionActiveLimit && World().State() != Workshop::Phase::Dead);
    }
    for (int i = 0; i < Workshop::CommissionActiveLimit; ++i) {
        const bool exists = i < static_cast<int>(tasks.Active().size());
        mCommissionsDialog->GetButton(7 + i * 2)->SetEnabled(exists && tasks.Active()[i].status == Workshop::CommissionStatus::Ready && World().State() != Workshop::Phase::Dead);
        mCommissionsDialog->GetButton(8 + i * 2)->SetEnabled(exists && World().State() != Workshop::Phase::Dead);
    }
}

void WorkshopScene::DrawCommissions(Graphics* g) {
    const auto& tasks = World().Tasks();
    g->FillRect(40, 20, 1020, 560, glm::vec4(93, 68, 40, 255));
    g->FillRect(48, 28, 1004, 544, kTaskPaper);
    TaskText("戴夫委托", 65, 42, 25, kTaskGreen);
    TaskText("下次刷新 " + TimeLabel(tasks.RefreshRemaining()) + " · 只在袭击中计时", 295, 48, 17, kTaskGreen);
    TaskText("主动接取后累计；准备、暂停和离线不计时。完成后计时停止，记得领奖。", 65, 83, 15);
    TaskText(std::format("已接取 {}/{}", tasks.ActiveCount(), Workshop::CommissionActiveLimit), 728, 110, 17, kTaskGreen);
    for (int i = 0; i < Workshop::CommissionOfferCount; ++i) {
        const auto& c = tasks.Offers()[i];
        const float x = 65 + i % 2 * 318.0f, y = 133 + i / 2 * 126.0f;
        g->FillRect(x, y, 304, 116, glm::vec4(225, 216, 175, 255));
        TaskText(std::string(Workshop::CommissionDifficultyName(c.difficulty)) + " · " + Workshop::CommissionGoalName(c.goal), x + 10, y + 8, 18, kTaskGreen);
        TaskText(std::format("目标 {} · 限时 {}", c.target, TimeLabel(c.remaining)), x + 10, y + 36, 15);
        TaskText(std::format("奖励 {} 阳光", c.sunReward), x + 10, y + 59, 15);
        if (c.blueprint != Workshop::Blueprint::Count)
            TaskText(std::string(Workshop::BlueprintName(c.blueprint)) + "蓝图＋1次免费", x + 10, y + 82, 11);
        mCommissionsDialog->GetButton(1 + i)->Draw(g);
    }
    for (int i = 0; i < Workshop::CommissionActiveLimit; ++i) {
        const float y = 133 + i * 174.0f;
        g->FillRect(715, y, 319, 164, glm::vec4(216, 222, 176, 255));
        if (i >= static_cast<int>(tasks.Active().size())) { TaskText("空闲名额 · 从左侧接取委托", 728, y + 35, 17, kTaskGreen); continue; }
        const auto& c = tasks.Active()[i];
        TaskText(std::string(Workshop::CommissionDifficultyName(c.difficulty)) + " · " + Workshop::CommissionGoalName(c.goal), 728, y + 9, 18, kTaskGreen);
        TaskText(std::format("进度 {} / {}", c.progress, c.target), 728, y + 38, 17);
        TaskText(c.status == Workshop::CommissionStatus::Ready ? "已完成 · 可领取奖励"
            : c.status == Workshop::CommissionStatus::Expired ? "已超时 · 本次奖励失效" : "剩余 " + TimeLabel(c.remaining), 728, y + 65, 16);
        TaskText(c.blueprint == Workshop::Blueprint::Count ? std::format("奖励 {} 阳光", c.sunReward)
            : std::string(Workshop::BlueprintName(c.blueprint)) + "蓝图＋1次免费", 728, y + 95, 14);
        mCommissionsDialog->GetButton(7 + i * 2)->Draw(g);
        mCommissionsDialog->GetButton(8 + i * 2)->Draw(g);
    }
    TaskText("蓝图概率：简单 0% · 中等 5% · 困难 25%，无保底；仅抽取阶段已开放的未获得蓝图。", 65, 520, 14);
    TaskText(mCommissionNotice.empty() ? "蓝图仅本基地有效；免费机会可在建造/植物图册使用，已接委托不受刷新影响。" : mCommissionNotice, 65, 545, 13, kTaskGreen);
    mCommissionsDialog->GetButton(0)->Draw(g);
}
