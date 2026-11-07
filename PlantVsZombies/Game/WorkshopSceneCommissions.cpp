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
std::string TaskName(const Workshop::Commission& c) {
    return std::string(Workshop::CommissionDifficultyName(c.difficulty)) + " · "
        + (c.IsBattle() ? Workshop::DescribeChallenge(c.challenge).name : Workshop::CommissionGoalName(c.goal));
}
std::string TaskReward(const Workshop::Commission& c) {
    return std::format("{}阳光", c.sunReward) + (c.blueprint == Workshop::Blueprint::Count ? "" : std::string("＋") + Workshop::BlueprintName(c.blueprint) + "蓝图");
}
bool LegendaryAvailable(const Workshop::Commissions& tasks) {
    for (int i = 0; i < Workshop::TraitCount; ++i) {
        const auto trait = static_cast<Workshop::Trait>(i);
        if (Workshop::DescribeTrait(trait).rarity == 2 && tasks.Traits()[i] < Workshop::TraitLimit(trait)) return true;
    }
    return false;
}
}

void WorkshopScene::CloseCommissions() {
    if (mCommissionsDialog) mCommissionsDialog->Close();
    mCommissionsDialog.reset(); mCommissionNotice.clear();
}

/** 使用正式模态输入和暂停；刷新由守波结算驱动，任务时限由袭击 Tick 推进。 */
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
                mDismissibleDialog = GameMessageBox::Builder(Vector(550, 300)).Title("放弃委托？").Message("进度将清除，本次不能重新接取。\n若挑战已经开波，本波强化仍持续到结束。")
                    .Button("取消", Vector(0, 0), Vector(130, 40), 17, [] {})
                    .Button("确认放弃", Vector(0, 0), Vector(170, 40), 17, [this, id] {
                        const auto& current = World().Tasks().Active();
                        for (size_t at = 0; at < current.size(); ++at) if (current[at].id == id) { Act("commission_abandon", static_cast<int>(at)); break; }
                    }).Show();
            }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    for (int i = 0; i < Workshop::CommissionOfferCount; ++i) {
        const float x = 65 + i % 2 * 318.0f, y = 133 + i / 2 * 126.0f;
        builder.Button("详情", Vector(x + 116, y + 79), Vector(75, 30), 14,
            [this, i] { OpenCommissionDetails(i); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    for (int i = 0; i < Workshop::CommissionActiveLimit; ++i)
        builder.Button("详情", Vector(970, 140 + i * 174.0f), Vector(55, 27), 13,
            [this, i] { OpenCommissionDetails(i, true); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("词条奖池", Vector(780, 39), Vector(132, 32), 15,
        [this] { OpenCommissionDetails(-1); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    mCommissionsDialog = builder.Show();
    for (int i = 0; i < 20; ++i) {
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
        mCommissionsDialog->GetButton(1 + i)->SetEnabled(tasks.CanAccept(i) && World().State() != Workshop::Phase::Dead);
    }
    for (int i = 0; i < Workshop::CommissionActiveLimit; ++i) {
        const bool exists = i < static_cast<int>(tasks.Active().size());
        const bool enough = exists && Workshop::EnergyLimit - World().Energy() >= tasks.Active()[i].sunReward;
        mCommissionsDialog->GetButton(7 + i * 2)->SetText(exists && tasks.Active()[i].status == Workshop::CommissionStatus::Ready && !enough ? "阳光容量不足" : "领取奖励");
        mCommissionsDialog->GetButton(7 + i * 2)->SetEnabled(enough && tasks.Active()[i].status == Workshop::CommissionStatus::Ready && World().State() != Workshop::Phase::Dead);
        mCommissionsDialog->GetButton(8 + i * 2)->SetEnabled(exists && World().State() != Workshop::Phase::Dead);
        mCommissionsDialog->GetButton(17 + i)->SetEnabled(exists);
    }
}

void WorkshopScene::DrawCommissions(Graphics* g) {
    const auto& tasks = World().Tasks();
    g->FillRect(40, 20, 1020, 560, glm::vec4(93, 68, 40, 255));
    g->FillRect(48, 28, 1004, 544, kTaskPaper);
    TaskText("戴夫委托", 65, 42, 25, kTaskGreen);
    TaskText(std::format("距刷新还需守住 {} 波 · 每 {} 波更新", tasks.RefreshWavesRemaining(World().CompletedWaves()), Workshop::CommissionRefreshWaves), 295, 48, 17, kTaskGreen);
    TaskText(std::format("至多1条战斗委托，绑定下一完整波。阳光剩余容量 {:.0f} / {:.0f}，容量不足可稍后领取。", Workshop::EnergyLimit - World().Energy(), Workshop::EnergyLimit), 65, 83, 14);
    TaskText(std::format("已接取 {}/{}", tasks.ActiveCount(), Workshop::CommissionActiveLimit), 728, 110, 17, kTaskGreen);
    for (int i = 0; i < Workshop::CommissionOfferCount; ++i) {
        const auto& c = tasks.Offers()[i];
        const float x = 65 + i % 2 * 318.0f, y = 133 + i / 2 * 126.0f;
        g->FillRect(x, y, 304, 116, glm::vec4(225, 216, 175, 255));
        TaskText(TaskName(c), x + 10, y + 8, 18, kTaskGreen);
        TaskText(c.IsBattle() ? std::format("下一完整波 · {}项机制，详情可预览", Workshop::DescribeChallenge(c.challenge).mechanisms)
            : std::format("目标 {} · 限时 {}", c.target, TimeLabel(c.remaining)), x + 10, y + 36, 13);
        TaskText(TaskReward(c), x + 10, y + 59, 12);
        if (c.IsBattle() && c.difficulty == 2) TaskText(LegendaryAvailable(tasks) ? "可出传奇 · 1%" : "传奇已集齐", x + 10, y + 87, 12, glm::vec4(144, 80, 20, 255));
        mCommissionsDialog->GetButton(1 + i)->Draw(g);
        mCommissionsDialog->GetButton(11 + i)->Draw(g);
    }
    for (int i = 0; i < Workshop::CommissionActiveLimit; ++i) {
        const float y = 133 + i * 174.0f;
        g->FillRect(715, y, 319, 164, glm::vec4(216, 222, 176, 255));
        if (i >= static_cast<int>(tasks.Active().size())) { TaskText("空闲名额 · 从左侧接取委托", 728, y + 35, 17, kTaskGreen); continue; }
        const auto& c = tasks.Active()[i];
        TaskText(TaskName(c), 728, y + 9, 17, kTaskGreen);
        TaskText(c.IsBattle() ? std::format("目标：守住第 {} 波", c.targetWave) : std::format("进度 {} / {}", c.progress, c.target), 728, y + 38, 16);
        TaskText(c.status == Workshop::CommissionStatus::Ready ? "已完成 · 可领取奖励"
            : c.status == Workshop::CommissionStatus::Expired ? "已失败 · 本次奖励失效"
            : c.IsBattle() ? "只限目标波 · 开波后强化不撤销" : "剩余 " + TimeLabel(c.remaining), 728, y + 65, 13);
        TaskText(TaskReward(c), 728, y + 87, 12);
        if (c.trait != Workshop::Trait::Count) {
            const auto& trait = Workshop::DescribeTrait(c.trait);
            TaskText(std::string(Workshop::TraitRarityName(trait.rarity)) + "词条：" + trait.name, 728, y + 105, 12,
                trait.rarity == 2 ? glm::vec4(153, 83, 16, 255) : trait.rarity == 1 ? glm::vec4(115, 58, 133, 255) : kTaskGreen);
        }
        else if (c.IsBattle() && c.difficulty == 2) TaskText(c.status == Workshop::CommissionStatus::Ready ? "本次未抽中词条" : "普通25% · 史诗4% · 传奇1%", 728, y + 105, 12);
        mCommissionsDialog->GetButton(7 + i * 2)->Draw(g);
        mCommissionsDialog->GetButton(8 + i * 2)->Draw(g);
        mCommissionsDialog->GetButton(17 + i)->Draw(g);
    }
    TaskText("后两格各50%出现困难，无保底。蓝图：简单0% · 中等5% · 困难25%；词条与蓝图独立抽取。", 65, 520, 13);
    TaskText(mCommissionNotice.empty() ? "蓝图仅本基地有效；免费机会可在建造/植物图册使用，已接委托不受刷新影响。" : mCommissionNotice, 65, 545, 13, kTaskGreen);
    mCommissionsDialog->GetButton(0)->Draw(g);
    mCommissionsDialog->GetButton(19)->Draw(g);
}

/** 详情在接取前展示整套挑战、实际奖励与完整稀有奖池，显示当前基地层数。 */
void WorkshopScene::OpenCommissionDetails(int index, bool active) {
    const auto& tasks = World().Tasks();
    if (active && (index < 0 || index >= static_cast<int>(tasks.Active().size()))) return;
    if (!active && index >= Workshop::CommissionOfferCount) return;
    const bool hasTask = index >= 0;
    const auto commission = hasTask ? (active ? tasks.Active()[index] : tasks.Offers()[index]) : Workshop::Commission{};
    auto dialog = GameMessageBox::Builder(Vector(550, 300)).Panel(1020, 560)
        .Button("返回", Vector(932, 36), Vector(100, 32), 16, [] {}, ResourceKeys::Textures::IMAGE_BUTTONSMALL).Show();
    mDismissibleDialog = dialog;
    dialog->GetButton(0)->SetTextColor(glm::vec4(255, 228, 142, 255));
    dialog->SetCustomDraw([this, hasTask, commission, weak = std::weak_ptr<GameMessageBox>(dialog)](Graphics* g) {
        g->FillRect(40, 20, 1020, 560, glm::vec4(93, 68, 40, 255));
        g->FillRect(48, 28, 1004, 544, kTaskPaper);
        TaskText(hasTask ? TaskName(commission) : "本基地工坊词条", 68, 43, 23, kTaskGreen);
        std::vector<std::string> lines;
        if (hasTask) {
            lines.push_back(TaskReward(commission));
            if (commission.blueprint != Workshop::Blueprint::Count) lines.emplace_back("蓝图仅本基地有效，并附送1次免费使用。");
            if (commission.IsBattle()) {
                lines.push_back(commission.targetWave ? std::format("目标：守住第{}波", commission.targetWave) : "接取后绑定下一波尚未开打的袭击。");
                const auto effects = Workshop::ChallengeEffects(commission.challenge);
                lines.insert(lines.end(), effects.begin(), effects.end());
                lines.emplace_back("只生效一波，守完即达标，不另设分钟限时。");
                lines.emplace_back("开波后放弃或任务失败，本波强化仍保留。");
                lines.emplace_back("失败重试保留挑战，可在重开波前放弃。");
            } else {
                lines.push_back(std::format("接取后累计{}份，限时{}袭击时间。", commission.target, TimeLabel(commission.timeLimit)));
                lines.emplace_back("准备、暂停、离线停表；超时只失去奖励。");
            }
            if (commission.trait != Workshop::Trait::Count) lines.push_back(std::string("已抽中：") + Workshop::DescribeTrait(commission.trait).name);
            lines.push_back(std::format("阳光剩余容量：{:.0f} / {:.0f}", Workshop::EnergyLimit - World().Energy(), Workshop::EnergyLimit));
        } else {
            const auto b = World().Tasks().Bonuses();
            lines = {std::format("当前火力×{:.2f} · 植物生命×{:.2f}", b.damage, b.health),
                std::format("加工×{:.2f} · 电容×{:.2f} · 产阳×{:.2f}", b.production, b.power, b.sunshine),
                "普通最多5层，每层增加10个百分点。", "史诗与传奇每种只获得一次。", "普通与史诗先相加，传奇再乘算。",
                "词条仅当前基地有效，重建重新获得。"};
        }
        float y = 94;
        for (const auto& line : lines) { TaskText(line, 68, y, 14); y += 29; }
        TaskText("困难挑战奖池 · 完成时锁定，领奖后生效", 560, 92, 17, kTaskGreen);
        TaskText("普通25% · 史诗4% · 传奇1% · 无词条70%", 560, 119, 14);
        for (int i = 0; i < Workshop::TraitCount; ++i) {
            const auto trait = static_cast<Workshop::Trait>(i); const auto& d = Workshop::DescribeTrait(trait);
            const float top = 151 + i * 40.0f;
            const auto color = d.rarity == 2 ? glm::vec4(153, 83, 16, 255) : d.rarity == 1 ? glm::vec4(115, 58, 133, 255) : kTaskGreen;
            TaskText(std::format("{} · {}  {}/{}", Workshop::TraitRarityName(d.rarity), d.name, World().Tasks().Traits()[i], Workshop::TraitLimit(trait)), 560, top, 14, color);
            TaskText(d.description, 560, top + 18, 12);
        }
        TaskText("无保底；已满词条退出该档奖池，该档全满时不发词条。", 560, 522, 12);
        TaskText("蓝图与词条独立，可同时获得；各档内从未满词条等概率抽取。", 560, 545, 11);
        if (auto box = weak.lock()) box->GetButton(0)->Draw(g);
    });
}
