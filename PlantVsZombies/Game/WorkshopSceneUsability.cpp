#include "WorkshopScene.h"
#include "../GameApp.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
using M = Workshop::Machine;
const glm::vec4 kInk{58, 52, 35, 255}; // 图卡正文
const glm::vec4 kGreen{51, 103, 49, 255}; // 使用中方案
const glm::vec4 kWarning{170, 57, 27, 255}; // 来料/配置警告
const glm::vec4 kGold{255, 229, 144, 255}; // 工具图标
constexpr Uint64 kWarningInterval = 250, kWarningDuration = 6000; // 检查间隔/一次提示时长，墙钟毫秒
void Text(const std::string& text, float x, float y, int size = 15, glm::vec4 color = kInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
int ProfileAt(Vector point) {
    if (point.y < 210 || point.y >= 422) return -1;
    if (point.x >= 125 && point.x < 545) return 0;
    return point.x >= 555 && point.x < 975 ? 1 : -1;
}
}

/** 用物料图标、实际去向和消费者解释方案；手动玩家无需先理解高低电平。 */
void WorkshopScene::DrawProfileCards(Graphics* g) {
    const auto& c = World().Cells()[mConfigMachine]; const int active = World().ActivePlan(mConfigMachine);
    const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
    Text(c.inputs[1] >= 0 ? "自动切换：B 关闭选左边，开启选右边。" : "点击整张方案卡即可启用；接上 B 线后，也能让电路自动切换。", 140, 177, 16, kGreen);
    for (int plan = 0; plan < 2; ++plan) {
        const float x = 125 + plan * 430.0f;
        const bool locked = c.type == M::Cooler && plan == 1 && World().CompletedWaves() < 8;
        const bool hover = ProfileAt(mouse) == plan;
        g->FillRect(x, 210, 420, 212, hover && !locked ? glm::vec4(244, 238, 196, 255) : glm::vec4(218, 218, 175, 255));
        for (int edge = 0; edge < (active == plan ? 3 : 1); ++edge)
            g->DrawRect(x + edge, 210 + edge, 420 - edge * 2, 212 - edge * 2, active == plan ? kGreen : glm::vec4(157, 142, 97, 255));
        Text(std::format("方案 {} · {}", plan + 1, locked ? "第 8 波后开放" : active == plan ? "正在使用" : "点击启用"), x + 18, 225, 20, kGreen);
        if (c.type == M::Cooler) {
            const auto raw = plan ? Workshop::Ammo::Melon : Workshop::Ammo::Pea;
            const auto product = plan ? Workshop::Ammo::IceMelon : Workshop::Ammo::Ice;
            DrawItemIcon(g, raw, x + 42, 265, 36); DrawItemIcon(g, product, x + 252, 265, 36);
            Text("加工 →", x + 138, 274, 19, kGreen);
            Text(plan ? "西瓜" : "普通豌豆", x + 26, 309, 17);
            Text(plan ? "冰瓜" : "寒冰豌豆", x + 235, 309, 17);
            Text(std::format("原料 {}/5", c.Inventory(raw)), x + 26, 343, 14);
            Text(std::format("成品 {}/5 · 每份 {:g} 阳光", c.Inventory(product), plan ? Workshop::IceMelonEnergy : Workshop::CoolingEnergy), x + 210, 343, 14);
        } else {
            const int filter = plan ? c.filter2 : c.filter, route = plan ? c.route2 : c.route;
            if (filter >= 0) DrawItemIcon(g, static_cast<Workshop::Ammo>(filter), x + 22, 268, 35);
            else for (int a = 0; a < Workshop::AmmoCount; ++a) DrawItemIcon(g, static_cast<Workshop::Ammo>(a), x + 12 + a * 14, 273, 18);
            Text("→", x + 88, 274, 22, kGreen);
            DrawUtilityIcon(g, "magazine", x + 129, 265, 34);
            Text(std::format("第 {} 路{}", route + 1, c.type == M::Cannon ? "炮击目标" : "公共弹仓"), x + 180, 273, 21, kGreen);
            if (c.type == M::Feeder) {
                const char* consumers[]{"豌豆射手", "寒冰射手", "精英胆小菇", "西瓜投手", "冰瓜"};
                Text(filter < 0 ? "混合配送；各植物只取自己需要的弹药。" : std::string("供给：") + consumers[filter] + "（不会改变植物弹种）", x + 18, 361, 13);
            } else Text(std::format("库存 {} / {} · 一轮消耗 3 颗", c.Inventory(static_cast<Workshop::Ammo>(filter)), Workshop::CannonBufferPerAmmo), x + 18, 361, 14);
        }
    }
    if (c.type == M::Cooler) Text(c.processing ? "当前一份完成后再切换；两种原料和成品都会保留。" : "原料和成品各自缓存；先检查配方，再接后面的装填口。", 140, 445, 16, kGreen);
    else if (c.type == M::Feeder) {
        Text("只负责送货，不生产弹药，也不是豌豆射手。", 140, 445, 17, kGreen);
        Text("每路所有植物共用弹仓；配额可在右侧整行弹仓按钮中调整。", 140, 477, 14);
    } else {
        Text(std::format("充能 {:.1f} / 12 秒", c.clock), 140, 442, 17, kGreen);
        g->FillRect(140, 471, 430, 10, glm::vec4(91, 102, 75, 255));
        g->FillRect(140, 471, 430 * c.clock / Workshop::CannonChargeSeconds, 10, kGold);
        Text("手动齐射需要目标；关闭窗口后发射。", 630, 495, 13);
    }
}

/** 同一处持续问题只提示一次，修正后再发生才重新提醒；地图上的标记持续保留。 */
void WorkshopScene::UpdateMaterialWarnings() {
    const Uint64 now = SDL_GetTicks64(); if (now < mMaterialCheckAt) return;
    mMaterialCheckAt = now + kWarningInterval;
    int fresh = -1, count = 0;
    for (int i = 0; i < Workshop::CellCount; ++i) {
        auto warning = World().MaterialWarning(i);
        if (!warning.empty()) { ++count; if (warning != mMaterialWarnings[i] && fresh < 0) fresh = i; }
        mMaterialWarnings[i] = std::move(warning);
    }
    if (fresh >= 0) {
        mMaterialNotice = std::format("{}处来料/配置待检查 · ({},{}) {}", count, fresh / Workshop::MaxColumns + 1, fresh % Workshop::MaxColumns + 1, mMaterialWarnings[fresh]);
        mMaterialNoticeUntil = now + kWarningDuration; ++mMaterialNoticeCount;
    }
    if (count == 0) { mMaterialNotice.clear(); mMaterialNoticeUntil = 0; }
}

/** 小工具采用熟悉的形状，拆除复用原版铲子；文字保留作准确说明。 */
void WorkshopScene::DrawUtilityIcon(Graphics* g, const std::string& action, float x, float y, float size) {
    g->PushTransform(); g->Translate(x, y); g->Scale(size / 24, size / 24);
    auto line = [&](float a, float b, float c, float d) { for (int k = 0; k < 2; ++k) g->DrawLine(a, b + k, c, d + k, kGold); };
    if (action == "remove_tool" || action == "front_remove") {
        auto* texture = ResourceManager::GetInstance().GetTexture(ResourceKeys::Textures::IMAGE_SHOVEL, false);
        if (texture) { const float scale = 23.0f / std::max(texture->width, texture->height); g->DrawTexture(texture, (24-texture->width*scale)/2, 0, texture->width*scale, texture->height*scale); }
    } else if (action == "pause") {
        if (mPaused) for (int n = 0; n < 15; ++n) line(6, 4+n, 6+std::min(n,14-n)*1.5f, 4+n);
        else { g->FillRect(5, 4, 5, 16, kGold); g->FillRect(15, 4, 5, 16, kGold); }
    } else if (action == "start" || action == "view") {
        line(5, 3, 5, 22); for (int n=0;n<10;++n) line(6, 4+n, 20-n, 4+n);
    } else if (action == "help") {
        g->DrawRect(2, 4, 20, 16, kGold); line(12, 4, 12, 20); line(5, 8, 9, 8); line(15, 8, 19, 8); line(5, 12, 9, 12); line(15, 12, 19, 12);
    } else if (action == "inspect" || action == "fit") {
        g->DrawCircle(9, 9, 6, kGold, 20); line(14, 14, 22, 22);
        if (action == "fit") { line(6, 9, 12, 9); line(9, 6, 9, 12); }
    } else if (action == "configure_tool" || action == "repair") {
        if (action == "repair") { g->FillRect(9, 3, 6, 19, kGold); g->FillRect(3, 9, 19, 6, kGold); }
        else { g->DrawCircle(12, 12, 6, kGold, 16); for (int n=0;n<8;++n) { float a=n*3.14159265f/4; line(12+std::cos(a)*6,12+std::sin(a)*6,12+std::cos(a)*10,12+std::sin(a)*10); } g->FillCircle(12,12,2,kGold); }
    } else if (action == "wire_a_tool" || action == "wire_b_tool" || action == "disconnect_tool") {
        g->DrawRect(1, 4, 6, 6, kGold); g->DrawRect(17, 15, 6, 6, kGold); line(7,7,12,7); line(12,7,12,18); line(12,18,17,18);
        if (action == "disconnect_tool") { line(6,20,20,3); line(5,19,19,2); }
    } else if (action == "rotate_tool" || action == "reset") {
        g->DrawCircle(12,12,8,kGold,20); g->FillRect(12,0,12,9,glm::vec4(91,90,126,255)); line(12,4,21,4); line(21,4,17,0); line(21,4,17,8);
    } else if (action == "expand") {
        for(int n=0;n<3;++n){line(3,3+n*8,19,3+n*8);line(3+n*8,3,3+n*8,19);} line(16,20,24,20); line(20,16,20,24);
    } else if (action == "back") {
        g->DrawRect(3,2,18,20,kGold); g->FillRect(6,2,12,7,kGold); g->DrawRect(7,14,10,7,kGold);
    } else {
        g->FillRect(2,4,20,17,glm::vec4(143,103,48,255)); g->DrawRect(2,4,20,17,kGold); line(2,9,22,9);line(2,16,22,16);line(6,4,6,21);line(18,4,18,21);
    }
    g->PopTransform();
}

void WorkshopScene::DrawToolbarIcons(Graphics* g) {
    if (mUIManager.GetTopActiveMessageBox()) return;
    const std::pair<const char*, float> controls[]{{"pause",115},{"start",115},{"expand",115},{"repair",115},{"help",115},{"reset",115},{"back",150},{"view",165},
        {"inspect",75},{"remove_tool",75},{"rotate_tool",90},{"wire_a_tool",90},{"wire_b_tool",90},{"disconnect_tool",100},{"configure_tool",90},{"fit",95}};
    for (const auto& [action,width] : controls) {
        auto button = GetControlButton(action); if (!button || button->IsSkipDraw()) continue;
        const auto center = button->GetCenter(); DrawUtilityIcon(g, action, center.x-width/2+7, center.y-10, 20);
    }
    if (mFrontView) {
        const std::pair<const char*, float> front[]{{"plant_catalog",140},{"front_remove",90},{"front_ability",115},{"front_auto",125}};
        for (const auto& [action,width] : front) if (auto button = GetControlButton(action)) {
            const auto center=button->GetCenter();
            DrawUtilityIcon(g, std::string(action)=="plant_catalog" ? "help" : std::string(action)=="front_ability" ? "repair" : std::string(action)=="front_auto" ? "rotate_tool" : action, center.x-width/2+7, center.y-10, 20);
        }
    }
    if (!mFrontView) for (const auto& button : mQuotaButtons) { const auto center=button->GetCenter(); DrawUtilityIcon(g,"magazine",center.x-123,center.y-10,20); }
    if (!mMaterialNotice.empty() && SDL_GetTicks64() < mMaterialNoticeUntil) {
        g->FillRect(216, 545, 860, 30, glm::vec4(251,228,178,250)); g->DrawRect(216,545,860,30,kWarning);
        Text(mMaterialNotice,226,551,14,kWarning);
    }
}
