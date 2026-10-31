#include "WorkshopScene.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <format>

namespace {
using M = Workshop::Machine;
const glm::vec4 kConfigInk{58, 52, 35, 255}; // 配置正文颜色
const glm::vec4 kConfigGreen{51, 103, 49, 255}; // 当前方案和状态标题颜色
const glm::vec4 kConfigGold{255, 229, 144, 255}; // 按钮文字颜色
void Label(const std::string& text, float x, float y, int size = 16, glm::vec4 color = kConfigInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
const char* PortName(const Workshop::Cell& c, int port) {
    if (port == 1) {
        if (Workshop::HasSelectionInput(c.type)) return "B 方案选择";
        if (c.type == M::Counter) return "B 清零";
        if (c.type == M::Latch) return "B 复位";
        return "B 条件输入";
    }
    if (c.type == M::StockSensor) return "A 检测来源";
    if (c.type == M::Counter) return c.counterMode == Workshop::CounterMode::Signal ? "A 信号来源" : "A 计量来源";
    if (c.type == M::Latch) return "A 置位";
    if (c.type == M::Cannon) return "A 开火许可";
    return Workshop::IsLogic(c.type) && c.type != M::Timer ? "A 条件输入" : "A 运行许可";
}
}

void WorkshopScene::CloseMachineConfig() {
    if (mMachineConfig) mMachineConfig->Close();
    mMachineConfig.reset(); mConfigControls.clear(); mConfigMachine = -1; mConfigNotice.clear();
}

/** 显式配置两套方案与计量方式；打开只暂停模拟，不改接线或工作进度。 */
void WorkshopScene::OpenMachineConfig(int index) {
    if (mLoadFailed || index < 0 || index >= Workshop::CellCount || World().Cells()[index].type == M::Empty
        || mUIManager.GetTopActiveMessageBox()) return;
    mConfigMachine = index; mConfigNotice.clear(); CancelMapGesture(); ClearFrontPreview(); mTooltipLines.clear(); mHoverKey = -1;
    const auto type = World().Cells()[index].type;
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(900, 540);
    std::vector<std::string> keys;
    auto add = [&](const std::string& key, const std::string& text, float x, float y, float width, std::function<void()> callback) {
        keys.push_back(key);
        builder.Button(text, Vector(x, y), Vector(width, 31), 14, std::move(callback), ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    };
    auto action = [this](const std::string& name, int value = 0) { Act(name, mConfigMachine, "", value); };
    add("cfg_close", "返回", 888, 48, 88, [this] { CloseMachineConfig(); });
    add("cfg_guide", "图解", 785, 48, 88, [this, type] { OpenGuide(static_cast<int>(type)); });
    add("cfg_disconnect_a", "断开 A", 699, 124, 120, [action] { action("disconnect_a"); });
    add("cfg_disconnect_b", "断开 B", 835, 124, 120, [action] { action("disconnect_b"); });
    if (Workshop::HasSelectionInput(type)) {
        add("cfg_plan0", "手动方案 1", 140, 172, 170, [action] { action("plan", 0); });
        add("cfg_plan1", "手动方案 2", 325, 172, 170, [action] { action("plan", 1); });
    }
    if (type == M::Feeder || type == M::Cannon) {
        for (int plan = 0; plan < 2; ++plan) {
            const float x = plan ? 548.0f : 140.0f;
            add("cfg_route" + std::to_string(plan), "目标路线", x, 260, 155, [this, action, plan] {
                const auto& c = World().Cells()[mConfigMachine];
                action(plan ? "route1" : "route0", ((plan ? c.route2 : c.route) + 1) % Workshop::Rows);
            });
            add("cfg_filter" + std::to_string(plan), "弹种", x + 168, 260, 170, [this, action, plan, type] {
                const auto& c = World().Cells()[mConfigMachine]; const int current = plan ? c.filter2 : c.filter;
                const int next = type == M::Cannon ? (current == static_cast<int>(Workshop::Ammo::Melon) && World().CompletedWaves() >= 8
                    ? static_cast<int>(Workshop::Ammo::IceMelon) : static_cast<int>(Workshop::Ammo::Melon))
                    : current >= Workshop::AmmoCount - 1 ? -1 : current + 1;
                action(plan ? "filter1" : "filter0", next);
            });
        }
    }
    if (type == M::Splitter) {
        add("cfg_split_normal", "轮流 / 筛选 / 优先", 140, 233, 220, [action] { action("strict_split", 0); });
        add("cfg_split_strict", "严格按方案选路", 378, 233, 220, [action] { action("strict_split", 1); });
    }
    if (type == M::Counter) {
        add("cfg_counter_mode", "计数来源", 140, 176, 260, [this, action] { action("counter_mode", (static_cast<int>(World().Cells()[mConfigMachine].counterMode) + 1) % 3); });
        add("cfg_counter_output", "输出方式", 420, 176, 300, [this, action] { action("counter_output", 1 - static_cast<int>(World().Cells()[mConfigMachine].counterOutput)); });
        add("cfg_counter_reset", "计数清零", 760, 330, 180, [action] { action("counter_reset"); });
    }
    if (type == M::StockSensor || type == M::EnemySensor) {
        add("cfg_route0", "目标路线", 140, 176, 200, [this, action] { action("route0", (World().Cells()[mConfigMachine].route + 1) % Workshop::Rows); });
    }
    if (type == M::Counter || type == M::StockSensor || type == M::Store || type == M::Splitter) {
        add("cfg_filter0", "筛选", 140, type == M::Splitter ? 282.0f : 230.0f, 260, [this, action] {
            const int current = World().Cells()[mConfigMachine].filter;
            action("filter0", current >= Workshop::ItemCount - 1 ? -1 : current + 1);
        });
    }
    if (type == M::Timer || type == M::StockSensor || type == M::EnemySensor || type == M::Counter) {
        for (int i = 0; i < 4; ++i) {
            const int delta = std::array<int, 4>{-5, -1, 1, 5}[i];
            add("cfg_setting" + std::to_string(i), delta < 0 ? std::to_string(delta) : "+" + std::to_string(delta), 140 + i * 125.0f, 330, 110,
                [this, action, delta, type] { action("setting", std::clamp(World().Cells()[mConfigMachine].setting + delta, 1, type == M::Timer ? 120 : 1000000)); });
        }
    }
    if (type == M::Switch || type == M::Cannon)
        add("cfg_manual", "自动 / 待机", type == M::Cannon ? 628.0f : 140.0f, type == M::Cannon ? 405.0f : 190.0f, 150, [action] { action("configure"); });
    if (type == M::Cannon) add("cfg_fire", "手动齐射", 798, 405, 150, [action] { action("fire_cannon"); });
    mMachineConfig = builder.Show(); mConfigControls.clear();
    for (size_t i = 0; i < keys.size(); ++i) {
        auto button = mMachineConfig->GetButton(i);
        button->SetTextColor(kConfigGold); button->SetHoverTextColor(glm::vec4(255, 251, 210, 255));
        mConfigControls.emplace(keys[i], button);
    }
    mMachineConfig->SetCustomDraw([this](Graphics* g) { DrawMachineConfig(g); });
    RefreshMachineConfig(); DeltaTime::SetPaused(true);
}

void WorkshopScene::RefreshMachineConfig() {
    if (!mMachineConfig || mConfigMachine < 0) return;
    const auto& c = World().Cells()[mConfigMachine];
    auto set = [&](const std::string& key, const std::string& text, bool enabled = true) {
        if (auto it = mConfigControls.find(key); it != mConfigControls.end()) { it->second->SetText(text); it->second->SetEnabled(enabled); }
    };
    set("cfg_disconnect_a", "断开 A", c.inputs[0] >= 0);
    set("cfg_disconnect_b", "断开 B", c.inputs[1] >= 0);
    set("cfg_plan0", std::string(c.plan == 0 ? "● " : "") + (c.type == M::Splitter ? "正前方" : "手动方案 1"), c.inputs[1] < 0);
    set("cfg_plan1", std::string(c.plan == 1 ? "● " : "") + (c.type == M::Splitter ? "右侧" : "手动方案 2"), c.inputs[1] < 0 && (c.type != M::Cooler || World().CompletedWaves() >= 8));
    for (int plan = 0; plan < 2; ++plan) {
        set("cfg_route" + std::to_string(plan), "目标：第 " + std::to_string((plan ? c.route2 : c.route) + 1) + " 路");
        const int filter = plan ? c.filter2 : c.filter;
        set("cfg_filter" + std::to_string(plan), std::string("物料：") + Workshop::AmmoName(filter),
            !(c.type == M::Counter && c.counterMode == Workshop::CounterMode::Signal) && !(c.type == M::Splitter && c.strictSplit));
    }
    set("cfg_counter_mode", std::string("计数：") + std::array<const char*, 3>{"信号从关到开", "来源机器产出", "来源机器出库 / 发射"}[static_cast<int>(c.counterMode)]);
    set("cfg_counter_output", c.counterOutput == Workshop::CounterOutput::Hold ? "输出：达标后保持开启" : "输出：每批完成翻转开 / 关");
    set("cfg_manual", c.type == M::Switch ? (c.manual ? "开关：开" : "开关：关") : (c.manual ? "自动：开" : "自动：待机"));
    set("cfg_fire", "手动齐射", c.type == M::Cannon && World().State() == Workshop::Phase::Assault && c.volleyRemaining == 0 && c.clock >= Workshop::CannonChargeSeconds
        && c.Inventory(static_cast<Workshop::Ammo>(World().ActiveFilter(mConfigMachine))) >= Workshop::CannonVolleySize);
}

void WorkshopScene::DrawMachineConfig(Graphics* g) {
    if (!mMachineConfig || !mMachineConfig->IsActive() || mConfigMachine < 0) return;
    const auto& c = World().Cells()[mConfigMachine]; const auto type = c.type;
    g->FillRect(100, 30, 900, 540, glm::vec4(234, 224, 182, 255));
    g->DrawRect(100, 30, 900, 540, kConfigGreen);
    Label(std::string(Workshop::Describe(type).name) + " · 运行配置", 130, 55, 25, kConfigGreen);
    Label("查看时已暂停；配置会保存，恢复运行后信号继续传播。", 130, 96, 15);
    auto connection = [&](int port) {
        const int source = c.inputs[port];
        return std::string(PortName(c, port)) + "：" + (source < 0 ? "未连接" : std::string(Workshop::Describe(World().Cells()[source].type).name)
            + " " + std::to_string(source / Workshop::MaxColumns + 1) + "," + std::to_string(source % Workshop::MaxColumns + 1));
    };
    Label(connection(0), 140, 133, 14);
    if (Workshop::HasSelectionInput(type) || type == M::And || type == M::Or || type == M::Latch || type == M::Counter) Label(connection(1), 415, 133, 14);
    if (Workshop::HasSelectionInput(type)) Label("当前方案 " + std::to_string(World().ActivePlan(mConfigMachine) + 1)
        + (c.inputs[1] >= 0 ? " · B 口低选1，高选2" : " · 未接 B，使用手动选择"), 530, 180, 15, kConfigGreen);
    if (type == M::Feeder || type == M::Cannon || type == M::Cooler) {
        for (int plan = 0; plan < 2; ++plan) {
            const float x = plan ? 530.0f : 122.0f;
            g->FillRect(x, 220, 365, 150, glm::vec4(218, 218, 175, 255));
            g->DrawRect(x, 220, 365, 150, World().ActivePlan(mConfigMachine) == plan ? kConfigGreen : glm::vec4(157, 142, 97, 255));
            Label("方案 " + std::to_string(plan + 1) + (plan ? " · B 高信号" : " · B 低信号"), x + 18, 234, 18, kConfigGreen);
            if (type == M::Cooler) {
                const auto input = plan ? Workshop::Ammo::Melon : Workshop::Ammo::Pea, output = plan ? Workshop::Ammo::IceMelon : Workshop::Ammo::Ice;
                Label(plan && World().CompletedWaves() < 8 ? "守住第 8 波开放" : plan ? "西瓜 → 冰瓜" : "普通豌豆 → 寒冰弹", x + 18, 273, 18);
                Label(std::format("原料 {}/5 · 成品 {}/5", c.Inventory(input), c.Inventory(output)), x + 18, 309, 16);
                Label(std::format("每份消耗 {:g} 阳光", plan ? Workshop::IceMelonEnergy : Workshop::CoolingEnergy), x + 18, 337, 14);
            } else {
                const int filter = plan ? c.filter2 : c.filter;
                Label(type == M::Feeder ? "缓存保留，恢复运行后送向该路弹仓。" : "本轮开火时锁定路线和弹种。", x + 18, 310, 14);
                if (type == M::Cannon) Label(std::format("{}瓜弹 {} / {} · 每轮消耗 3 颗", filter == static_cast<int>(Workshop::Ammo::IceMelon) ? "冰" : "普通",
                    c.Inventory(static_cast<Workshop::Ammo>(filter)), Workshop::CannonBufferPerAmmo), x + 18, 339, 14);
                else Label("配额由该路弹仓控制；这里不增加容量。", x + 18, 339, 14);
            }
        }
        if (type == M::Cooler) {
            Label(std::string("当前加工：") + (c.recipe ? "冰瓜" : "寒冰弹") + (c.processing ? " · 在制品完成后再切换" : " · 等待原料 / 开工"), 140, 400, 17, kConfigGreen);
            Label("两种原料和两种成品各有独立缓存；切换不清料、不丢货。", 140, 436, 15);
        } else if (type == M::Cannon) {
            Label(std::format("充能 {:.1f} / {:.0f} 秒", c.clock, Workshop::CannonChargeSeconds), 140, 400, 17, kConfigGreen);
            g->FillRect(140, 433, 430, 10, glm::vec4(91, 102, 75, 255));
            g->FillRect(140, 433, 430 * c.clock / Workshop::CannonChargeSeconds, 10, glm::vec4(220, 183, 75, 255));
            Label("手动齐射需要合法目标；关闭窗口恢复后发射。", 630, 450, 13);
            Label("A 口控制开火，不影响充能；已开始的三发会完整发完。", 140, 470, 14);
        } else Label("方案可以分别设路线与物料；B 切换后从同一缓存继续供货。", 140, 410, 16);
    } else if (type == M::Splitter) {
        Label(c.strictSplit ? "严格选路：只送选定出口，堵住时等待，不偷偷换路。" : "普通模式：无筛选时轮流/优先；有筛选时按弹种固定分路。", 140, 330, 17, kConfigGreen);
        Label("A 口可停止输出；B 口在严格模式下直接选择前方 / 右侧。", 140, 371, 16);
        Label("切回普通模式会保留原来的筛选配置，旧基地迁移后仍是普通模式。", 140, 410, 14);
    } else if (type == M::Counter || type == M::StockSensor || type == M::EnemySensor || type == M::Timer) {
        Label(std::string(type == M::Timer ? "周期（秒）：" : type == M::Counter ? "每批 / 门槛：" : "触发门槛：") + std::to_string(c.setting), 140, 290, 22, kConfigGreen);
        if (type == M::Counter) {
            Label(std::format("本批 {} · 输出 {}", c.count, c.signal ? "开" : "关"), 140, 383, 20, kConfigGreen);
            Label("产出统计成品，出库统计交付的物料；炮台出库按实际发出的瓜弹计数。", 140, 421, 14);
            Label("改变计数方式、来源、物料或门槛会从新批次开始；B 清零优先。", 140, 453, 14);
        } else if (type == M::StockSensor) Label("A 接机器时检测该机器；A 未接时检测选定路线的弹仓。", 140, 400, 16);
        else if (type == M::EnemySensor) Label("检测该条前线路线的存活僵尸数量，达到门槛输出开启信号。", 140, 400, 16);
        else Label("前半周期关闭，后半周期开启；A 不接时自动计时。", 140, 400, 16);
    } else {
        Label(Workshop::Describe(type).description, 140, 285, 17, kConfigGreen);
        Label("需要接法示例时可点右上角图解；A / B 的作用以本页端口说明为准。", 140, 390, 15);
    }
    g->PushClipRect(125, 509, 846, 46);
    Label(mConfigNotice.empty() ? "Esc 返回 · 准备期仍全部停机，开波后生效" : mConfigNotice, 140, 521, 14);
    g->PopClipRect();
    for (const auto& entry : mConfigControls) entry.second->Draw(g);
}
