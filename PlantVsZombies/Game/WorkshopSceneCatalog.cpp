#include "WorkshopScene.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
constexpr int kRecentLimit = 6; // 侧栏最近选择的机器种类上限，不限制建造数量
constexpr float kCardGap = 14; // 图册卡片间距，逻辑像素
constexpr float kCardsWidth = 956; // 卡片区域总宽度，逻辑像素
const glm::vec4 kCatalogInk{66, 54, 35, 255}; // 图册正文颜色
const glm::vec4 kCatalogGreen{62, 103, 48, 255}; // 分类与可建造提示颜色
const glm::vec4 kCatalogRed{150, 53, 37, 255}; // 锁定及阳光不足提示颜色

int Category(Workshop::Machine type) {
    using M = Workshop::Machine;
    if (type == M::Solar || type == M::Grower || type == M::Cooler || type == M::SporeGrower || type == M::MelonGrower || type == M::Recycler) return 0;
    return Workshop::IsLogic(type) ? 2 : 1;
}
struct CardLayout { Vector pos, size; };
/** 命中框和绘制共用布局；生产/物流六卡两行，九种电路三行，不裁掉新增设备。 */
CardLayout Layout(int category, int slot) {
    const int columns = 3;
    const float width = (kCardsWidth - (columns - 1) * kCardGap) / columns;
    return {Vector(72 + (slot % columns) * (width + kCardGap), 147 + (slot / columns) * (category == 2 ? 123.0f : 190.0f)),
        Vector(width, category == 2 ? 112 : 176)};
}
void Label(const std::string& value, float x, float y, int size = 16, glm::vec4 color = kCatalogInk) {
    GameAPP::GetInstance().DrawText(value, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
std::array<std::string, 2> Summary(Workshop::Machine type, int completedWaves) {
    using M = Workshop::Machine;
    switch (type) {
    case M::Solar: return {"为全基地采集阳光", std::format("每秒 {:g} 阳光", Workshop::SolarRate)};
    case M::Grower: return {std::format("{:g} 阳光 → 1 普通弹", Workshop::GrowEnergy), std::format("每 {:.1f} 秒生产一颗", Workshop::GrowPeriod)};
    case M::Cooler: return {completedWaves >= 8 ? "选择冰豌豆 / 冰瓜配方" : "普通豌豆加工成寒冰弹", "原料 + 阳光 → 冷凝成品"};
    case M::SporeGrower: return {"培养精英菇需要的孢子", std::format("每 {:.1f} 秒一份", Workshop::SporePeriods[0])};
    case M::MelonGrower: return {"培育可投掷的西瓜", std::format("每 {:.1f} 秒一颗", Workshop::MelonPeriods[0])};
    case M::SalvagePort: return {"提取击杀掉落的残骸", "连接运输带送往回收机"};
    case M::Recycler: return {"1 份残骸 → 5 阳光", "准备阶段停止加工"};
    case M::Counter: return {"计信号 / 产出 / 出库", "每批完成可翻转方案"};
    case M::Cannon: return {"充能12秒，连射3发瓜弹", "A许可开火，B切换方案"};
    case M::Belt: return {"沿箭头逐格输送弹药", "连接相邻机器"};
    case M::Splitter: return {"一条输入分成两路", "可严格按B口选择出口"};
    case M::Store: return {"缓存运输中的弹药", "平衡供需波动"};
    case M::Feeder: return {"给前线弹仓补货", "B切换两套路线与弹种"};
    case M::Switch: return {"手动切换开关", "控制所连机器"};
    case M::EnemySensor: return {"检测指定路线敌人数", "有敌人才开工"};
    case M::StockSensor: return {"检测指定库存", "够货时发出开启信号"};
    case M::And: return {"两个条件都满足", "才允许下游开工"};
    case M::Or: return {"任一个条件满足", "就允许下游开工"};
    case M::Not: return {"把开与关反过来", "搭配库存器：够货停产"};
    case M::Timer: return {"开一会，关一会", "周期自动重复"};
    case M::Latch: return {"A 触发一次就记住", "B 清除开启状态"};
    default: return {};
    }
}
}

bool WorkshopScene::IsCatalogOpen() const { return mCatalog && mCatalog->IsActive(); }

/** 图册只是 UI 模态层，不改玩家手动暂停标志，也不创建或扣费任何机器。 */
void WorkshopScene::OpenCatalog() {
    if (mLoadFailed || mFrontView || IsCatalogOpen() || mUIManager.GetTopActiveMessageBox()) return;
    mCatalogNotice.clear(); CancelMapGesture(); mTooltipLines.clear(); mHoverKey = -1;
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(1012, 552);
    builder.Button("返回", Vector(973, 47), Vector(64, 34), 16, [this] { CloseCatalog(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    constexpr std::array<const char*, 3> tabs{"生产", "物流 / 战术", "电路"};
    for (int category = 0; category < 3; ++category)
        builder.Button(tabs[category], Vector(72 + category * 172.0f, 99), Vector(160, 34), 18,
            [this, category] { mCatalogCategory = category; mCatalogNotice.clear(); RefreshCatalog(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("图解指南", Vector(602, 99), Vector(140, 34), 16, [this] { OpenGuide(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    for (int i = 1; i < static_cast<int>(Workshop::Machine::Count); ++i) {
        const auto type = static_cast<Workshop::Machine>(i);
        builder.Button("", Vector(-2000, -2000), Vector(1, 1), 12, [this, type] {
            if (!World().Unlocked(type)) {
                mCatalogNotice = "守住第 " + std::to_string(Workshop::Describe(type).unlockWave) + " 波后解锁，届时会介绍用途与接法。";
                return;
            }
            SelectBuild(type); CloseCatalog(false);
        }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    for (int i = 1; i < static_cast<int>(Workshop::Machine::Count); ++i)
        builder.Button("图解", Vector(-2000, -2000), Vector(64, 23), 13, [this, i] { OpenGuide(i); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    mCatalog = builder.Show();
    mCatalog->SetCustomDraw([this](Graphics* g) { DrawCatalog(g); });
    mCatalog->GetButton(4)->SetTextColor(glm::vec4(255, 228, 142, 255));
    mCatalog->GetButton(4)->SetHoverTextColor(glm::vec4(255, 250, 201, 255));
    mMachineButtons.clear(); mMachineGuideButtons.clear();
    for (int i = 1; i < static_cast<int>(Workshop::Machine::Count); ++i) {
        mMachineButtons.push_back(mCatalog->GetButton(i + 4));
        mMachineGuideButtons.push_back(mCatalog->GetButton(i + 3 + static_cast<int>(Workshop::Machine::Count)));
        mMachineGuideButtons.back()->SetTextColor(glm::vec4(255, 228, 142, 255));
        mMachineGuideButtons.back()->SetHoverTextColor(glm::vec4(255, 250, 201, 255));
    }
    RefreshCatalog();
    DeltaTime::SetPaused(true);
}

void WorkshopScene::CloseCatalog(bool cancelTool) {
    if (cancelTool) CancelTool();
    if (mCatalog) mCatalog->Close();
    mCatalog.reset(); mMachineButtons.clear(); mMachineGuideButtons.clear(); mCatalogNotice.clear();
    mHoverKey = -1; mTooltipLines.clear();
    // Scene::Update 在模态释放帧结束后统一恢复时钟，避免释放事件穿透或覆盖手动暂停。
}

void WorkshopScene::RefreshCatalog() {
    if (!IsCatalogOpen()) return;
    int slot = 0;
    for (size_t i = 0; i < mMachineButtons.size(); ++i) {
        auto& button = mMachineButtons[i];
        const bool visible = Category(static_cast<Workshop::Machine>(i + 1)) == mCatalogCategory;
        auto& guide = mMachineGuideButtons[i];
        button->ForceResetHoverState(); button->ResetFrameState(); button->SetEnabled(visible);
        guide->ForceResetHoverState(); guide->ResetFrameState(); guide->SetEnabled(visible && World().Unlocked(static_cast<Workshop::Machine>(i + 1)));
        if (visible) {
            const auto card = Layout(mCatalogCategory, slot++);
            button->SetPosition(card.pos); button->SetSize(Vector(card.size.x, card.size.y - 31));
            guide->SetPosition(card.pos + Vector(card.size.x - 80, card.size.y - 28));
        } else { button->SetPosition(Vector(-2000, -2000)); guide->SetPosition(Vector(-2000, -2000)); }
    }
    for (int category = 0; category < 3; ++category)
        mCatalog->GetButton(category + 1)->SetTextColor(category == mCatalogCategory ? glm::vec4(255, 225, 135, 255) : glm::vec4(225, 225, 192, 255));
}

void WorkshopScene::RememberMachine(Workshop::Machine type) {
    std::erase(mRecentMachines, type);
    mRecentMachines.insert(mRecentMachines.begin(), type);
    if (mRecentMachines.size() > kRecentLimit) mRecentMachines.resize(kRecentLimit);
}

/** 直接复用机器图形绘制，不生成小尺寸截图再放大；虚拟 Cell 只用于外观，不加入模拟。 */
void WorkshopScene::DrawMachineImage(Graphics* g, Workshop::Machine type, float x, float y, float scale, int direction) {
    Workshop::Cell cell; cell.type = type; cell.direction = direction;
    g->PushTransform(); g->Translate(x, y); g->Scale(scale, scale);
    DrawMachineVisual(g, cell, 0, 0, scale, true, false);
    g->PopTransform();
}

void WorkshopScene::DrawCatalog(Graphics* g) {
    if (!IsCatalogOpen()) return;
    g->FillRect(0, 0, SCENE_WIDTH, SCENE_HEIGHT, glm::vec4(17, 25, 18, 175));
    g->FillRect(50, 30, 1012, 552, glm::vec4(19, 23, 12, 125));
    g->FillRect(44, 24, 1012, 552, glm::vec4(88, 57, 32, 255));
    g->FillRect(51, 31, 998, 538, glm::vec4(148, 105, 53, 255));
    for (int line = 0; line < 8; ++line)
        g->DrawLine(57, 36 + line * 75.0f, 1043, 32 + line * 75.0f, glm::vec4(106, 73, 34, 150));
    g->FillRect(61, 41, 978, 518, glm::vec4(236, 224, 181, 255));
    for (float x : {52.0f, 1048.0f}) for (float y : {32.0f, 568.0f}) {
        g->FillCircle(x, y, 4, glm::vec4(56, 48, 32, 255));
        g->FillCircle(x - 1, y - 1, 2, glm::vec4(198, 183, 133, 255));
    }
    Label("戴夫的施工图册", 75, 49, 27, kCatalogGreen);
    Label("阳光 " + std::to_string(static_cast<int>(World().Energy())), 770, 55, 18);
    Label("先认机器，再把生产线接起来", 380, 59, 16);
    for (int i = 0; i < 5; ++i) mCatalog->GetButton(i)->Draw(g);
    Label("查看时已暂停 · Esc 返回", 785, 107, 14, kCatalogGreen);
    int slot = 0;
    std::string detail = mCatalogNotice;
    for (size_t i = 0; i < mMachineButtons.size(); ++i) {
        const auto type = static_cast<Workshop::Machine>(i + 1);
        if (Category(type) != mCatalogCategory) continue;
        const auto card = Layout(mCatalogCategory, slot++);
        const float x = card.pos.x, y = card.pos.y, w = card.size.x, h = card.size.y;
        const auto& info = Workshop::Describe(type);
        const bool unlocked = World().Unlocked(type), affordable = World().Energy() >= info.cost;
        const bool hover = mMachineButtons[i]->IsHovered();
        g->FillRect(x + 3, y + 4, w, h, glm::vec4(74, 64, 35, 90));
        g->FillRect(x, y, w, h, unlocked ? glm::vec4(252, 243, 211, 255) : glm::vec4(220, 215, 194, 255));
        g->FillRect(x, y, w, 4, hover ? glm::vec4(226, 173, 60, 255) : glm::vec4(108, 136, 69, 255));
        g->DrawRect(x, y, w, h, hover ? glm::vec4(180, 123, 30, 255) : glm::vec4(167, 149, 96, 255));
        if (!unlocked) {
            const bool compact = mCatalogCategory == 2;
            DrawLockedItem(g, x + w / 2, y + (compact ? 10 : 28), compact ? 38 : 60);
            Label("守住第 " + std::to_string(info.unlockWave) + " 波后解锁", x + (w - 172) / 2, y + h - 32, 16);
            if (hover && mCatalogNotice.empty()) detail = "尚未解锁：守住第 " + std::to_string(info.unlockWave) + " 波后会展示用途与示例。";
            continue;
        }
        Label(info.name, x + 14, y + 13, 20, unlocked ? kCatalogGreen : kCatalogInk);
        Label(std::to_string(info.cost) + " 阳光", x + w - 91, y + 17, 15, affordable ? kCatalogInk : kCatalogRed);
        const bool compact = mCatalogCategory == 2;
        const float scale = compact ? 0.86f : 1.5f;
        const float imageX = x + 14;
        const float imageY = y + (compact ? 34 : 45);
        g->FillRect(imageX - 3, imageY - 3, 53 * scale, 57 * scale, glm::vec4(209, 222, 180, 255));
        DrawMachineImage(g, type, imageX, imageY, scale);
        const auto summary = Summary(type, World().CompletedWaves());
        for (int line = 0; line < 2; ++line)
            Label(summary[line], compact ? x + 74 : x + 109, y + (compact ? 38 : 61) + line * 24.0f, compact ? 13 : 14);
        const std::string status = affordable ? "点击图卡选择建造" : "阳光不足 · 可先选中";
        g->FillRect(x + 9, y + h - 29, w - 18, 23, unlocked && affordable ? glm::vec4(221, 232, 190, 255) : glm::vec4(209, 201, 179, 255));
        Label(status, x + 16, y + h - 25, 13, unlocked && affordable ? kCatalogGreen : kCatalogRed);
        mMachineGuideButtons[i]->Draw(g);
        if (hover && mCatalogNotice.empty()) detail = info.description;
    }
    Label(detail.empty() ? "选择后返回地图，可连续放置；R 旋转指向的机器 / 空地预览，Esc 取消。" : detail, 75, 532, 15,
        mCatalogNotice.empty() ? kCatalogInk : kCatalogRed);
    DrawTooltip(g);
}

void WorkshopScene::DrawQuickBuild(Graphics* g) {
    Label("常用 / 最近选择", 815, 185, 12, kCatalogGreen);
    for (size_t i = 0; i < mRecentMachines.size(); ++i) {
        const auto center = mQuickButtons[i]->GetCenter();
        const float x = center.x - 43, y = center.y - 28;
        const auto type = mRecentMachines[i];
        const bool unlocked = World().Unlocked(type), hover = mQuickButtons[i]->IsHovered();
        g->FillRect(x, y, 86, 56, hover ? glm::vec4(253, 242, 199, 255) : glm::vec4(216, 210, 164, 255));
        g->DrawRect(x, y, 86, 56, glm::vec4(139, 126, 77, 255));
        if (!unlocked) {
            DrawLockedItem(g, x + 43, y + 3, 25);
            Label(std::to_string(Workshop::Describe(type).unlockWave) + " 波解锁", x + 15, y + 36, 12); continue;
        }
        DrawMachineImage(g, type, x + 29, y + 2, 0.55f);
        Label(Workshop::Describe(type).name, x + 15, y + 36, 13, unlocked ? kCatalogGreen : kCatalogRed);
    }
}

int WorkshopScene::BuildPreviewIndex() const {
    return !mFrontView && !mMapDragging && !mLeftPanning && !mUIManager.GetTopActiveMessageBox() && mTool == Tool::Build
        ? FactoryCellAt(GameAPP::GetInstance().GetInputHandler().GetMousePosition()) : -1;
}
std::string WorkshopScene::BuildPreviewReason(int index) const {
    if (index < 0) return "把机器移到空格上，点击放置";
    if (World().State() == Workshop::Phase::Dead) return "基地失守，暂时不能施工";
    if (World().Cells()[index].type != Workshop::Machine::Empty) return "此格已有机器 · R 可直接旋转它";
    if (!World().Unlocked(mBuildType)) return "该机器尚未解锁";
    if (World().Energy() < Workshop::Describe(mBuildType).cost) return "阳光不足，放置不会扣费";
    return "可放置 · 点击后扣费 · Esc / 右键取消";
}
