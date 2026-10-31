#include "WorkshopScene.h"
#include "Workshop/WorkshopGuide.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr int kContentsRows = 8; // 每页目录条目数，避免长目录越出弹窗
const glm::vec4 kPaper{240, 230, 191, 255}; // 图解纸面颜色
const glm::vec4 kInk{62, 54, 36, 255}; // 正文颜色
const glm::vec4 kGreen{59, 102, 47, 255}; // 标题与物料箭头颜色
const glm::vec4 kWire{111, 70, 160, 255}; // 控制线颜色，与物料路径区分
using M = Workshop::Machine;
int Topic(M type) { return static_cast<int>(type); }
int PlantTopic(PlantType type) {
    for (size_t i = 0; i < Workshop::Plants.size(); ++i) if (Workshop::Plants[i].type == type) return Workshop::PlantGuideBase + static_cast<int>(i);
    return 0;
}
std::string TopicName(int topic) {
    if (topic == Workshop::PowerGuide) return "电力与手动调度";
    if (topic == Workshop::DispatchGuide) return "电路自动调度";
    if (topic == Workshop::FocusGuide) return "主攻路线预告";
    if (topic == -1) return "全基地阳光";
    if (topic == -2) return "击杀残骸";
    if (topic == 0) return "第一条生产线";
    if (topic >= Workshop::PlantGuideBase) return Workshop::Plants[topic - Workshop::PlantGuideBase].name;
    return Workshop::Describe(static_cast<M>(topic)).name;
}
void Label(const std::string& text, float x, float y, int size = 16, glm::vec4 color = kInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
struct Node { int topic; Vector pos; std::string label{}; };
struct Edge { int from, to; bool wire = false; std::string label{}; };
struct Lesson {
    std::string purpose;
    std::array<std::string, 3> steps;
    std::string tip, example;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
};
/** 示意图表达连接关系，不伪造经营实体；箭头的含义由实线/虚线及端口文字说明。 */
void Chain(Lesson& lesson, std::initializer_list<int> topics, bool wires = false) {
    const float step = topics.size() > 1 ? 580.0f / (topics.size() - 1) : 0;
    for (int topic : topics) {
        const int i = static_cast<int>(lesson.nodes.size());
        lesson.nodes.push_back({topic, Vector(355 + step * i, 402), {}});
        if (i) lesson.edges.push_back({i - 1, i, wires, wires ? "接 A 口" : ""});
    }
}
void Gate(Lesson& lesson, M gate, M first, M second) {
    lesson.nodes = {{Topic(first), Vector(355, 366), {}}, {Topic(second), Vector(355, 458), {}},
        {Topic(gate), Vector(635, 411), {}}, {Topic(M::Grower), Vector(915, 411), {}}};
    lesson.edges = {{0, 2, true, "A"}, {1, 2, true, "B"}, {2, 3, true, "接 A 口"}};
}
Lesson Explain(int topic) {
    Lesson lesson;
    if (topic == Workshop::PowerGuide) {
        lesson.purpose = "阳光付钱，电网决定同时能加工多少。";
        lesson.steps = {"1. 顶部电网显示需求、容量与加工速度。", "2. 超载时双击闲置产线，关闭加工电源。", "3. 对比下方三种操作，看紧缺路线得到多少弹药。"};
        lesson.tip = "升级电网受波次限制；准备期停机，库存和进度跨波保留。";
        return lesson;
    }
    if (topic == Workshop::DispatchGuide) {
        lesson.purpose = "把手动关闲线的操作，交给电路持续执行。";
        lesson.steps = {"1. 库存器看第1路，门槛20，弹种选普通。", "2. 库存器接非门 A，非门接育豆机 A。", "3. 少于20就开工，达到20就断电，把电让给别路。"};
        lesson.tip = "进阶：敌情器与缺弹信号接与门，控制额外增援线；保留基础补给应付转火。";
        lesson.example = "自动补货：够用就断电，库存下降后再开";
        Chain(lesson, {Topic(M::StockSensor), Topic(M::Not), Topic(M::Grower)}, true);
        return lesson;
    }
    if (topic == Workshop::FocusGuide) {
        lesson.purpose = "每波分三段主攻，火力和产能都要跟着需求变。";
        lesson.steps = {"1. 开波前点电网，看三段主攻路线。", "2. 敌情器接装填口 B，在两套送货路线间切换。", "3. 其他路线仍有骚扰，留少量储备，别全部断供。"};
        lesson.tip = "主攻转移不搬走已有僵尸；敌情器检测实际敌人，能继续照顾未清完的旧路线。";
        lesson.example = "共享产线给两路：有敌切第2路，无敌补第1路";
        Chain(lesson, {Topic(M::EnemySensor), Topic(M::Feeder)}, true); lesson.edges[0].label = "接 B 口";
        return lesson;
    }
    if (topic >= Workshop::PlantGuideBase) {
        const auto& plant = Workshop::Plants[topic - Workshop::PlantGuideBase];
        lesson.purpose = plant.role;
        lesson.example = "供弹示例：装填口送到植物所在路线";
        if (plant.ammo == Workshop::Ammo::Count) {
            lesson.steps = {"1. 从植物图册选择，再点前线左侧六列空格。", "2. 放在输出植物前面，替后排承受啃食。", "3. 防御植物不耗弹药，身后的射手仍需要供弹。"};
            lesson.tip = "摆法示意：左边靠房屋，右边迎敌；同格只放一株主植物。";
            lesson.example = "前线摆法：后排输出，前排阻挡";
            Chain(lesson, {PlantTopic(PlantType::PLANT_PEASHOOTER), topic});
            lesson.edges.clear();
            if (plant.type == PlantType::PLANT_ICESTORAGENUT) {
                lesson.steps[2] = "3. 选中后点阳光修复，也可切换自动修复。";
                lesson.tip = "修复支付工坊阳光，准备阶段停止；护体可抵抗巨人重击。";
            } else if (plant.type == PlantType::PLANT_TALLNUT) lesson.tip = "高耐久阻挡，但仍要给后排留输出空间；不能代替火力。";
        } else {
            lesson.steps = {"1. 建好对应生产线，运输带接入装填口。", "2. 装填口的目标路线设为这株植物所在路。", "3. 左侧弹仓按钮可分配弹种配额；缺弹会停火。"};
            lesson.tip = "同一路植物共用弹仓；增加输出植物时，也要增加产能与配额。";
            if (plant.ammo == Workshop::Ammo::Pea) Chain(lesson, {Topic(M::Grower), Topic(M::Belt), Topic(M::Feeder), topic});
            if (plant.ammo == Workshop::Ammo::Ice) Chain(lesson, {Topic(M::Grower), Topic(M::Cooler), Topic(M::Feeder), topic});
            if (plant.ammo == Workshop::Ammo::Spore) {
                Chain(lesson, {Topic(M::SporeGrower), Topic(M::Belt), Topic(M::Feeder), topic});
                lesson.tip = "精英菇放后排：持续射击会成长，受惊清零；跨波或缺弹保留成长。";
            }
            if (plant.ammo == Workshop::Ammo::Melon) Chain(lesson, {Topic(M::MelonGrower), Topic(M::Belt), Topic(M::Feeder), topic});
            if (plant.ammo == Workshop::Ammo::IceMelon) {
                Chain(lesson, {Topic(M::MelonGrower), Topic(M::Cooler), Topic(M::Feeder), topic});
                lesson.steps[0] = "1. 先种西瓜，再选冰瓜点原株升级。";
                lesson.steps[1] = "2. 冷凝机选方案2，或向 B 口输入高信号。";
                lesson.tip = "冰瓜消耗冰瓜弹药；升级继承西瓜的攻速等级和剩余血量比例。";
            }
            if (!lesson.edges.empty()) lesson.edges.back().label = "本路弹仓";
        }
        return lesson;
    }
    switch (static_cast<M>(topic)) {
    case M::Empty:
        lesson.purpose = "工厂造弹药，弹仓收货，前线植物取弹攻击。";
        lesson.steps = {"1. 育豆机、运输带、装填口相邻放，箭头朝右。", "2. 选中装填口，目标路线设为射手所在路。", "3. 点开始袭击才会生产；空格暂停时仍可施工。"};
        lesson.tip = "工厂第几行不决定送哪路，装填口的目标路线才决定。";
        lesson.example = "最小供弹线：先给一条前线供货，再照着搭其他路";
        Chain(lesson, {Topic(M::Grower), Topic(M::Belt), Topic(M::Feeder), PlantTopic(PlantType::PLANT_PEASHOOTER)});
        lesson.edges.back().label = "第 1 路弹仓";
        break;
    case M::Solar:
        lesson.purpose = "开战后采集阳光，供全基地建造和生产使用。";
        lesson.steps = {"1. 选空格建造，不需要接运输带。", "2. 开始袭击后，阳光自动进入顶部总数。", "3. 阳光不足时增加产能，或暂停多余生产线。"};
        lesson.tip = "集光器只产阳光，不增加电网容量；准备阶段和暂停时不生产。";
        lesson.example = "阳光是全基地共享资源，不用运输带搬运";
        Chain(lesson, {topic, -1, Topic(M::Grower)});
        lesson.edges[0].label = "自动入账"; lesson.edges[1].label = "花阳光生产";
        break;
    case M::Grower:
    case M::SporeGrower:
    case M::MelonGrower: {
        const auto machine = static_cast<M>(topic);
        const auto plant = machine == M::Grower ? PlantType::PLANT_PEASHOOTER : machine == M::SporeGrower ? PlantType::PLANT_ELITE_SCAREDYSHROOM : PlantType::PLANT_MELONPULT;
        lesson.purpose = machine == M::Grower ? "把阳光变成普通豌豆，供豌豆射手使用。" : machine == M::SporeGrower ? "把阳光变成孢子，供精英胆小菇使用。" : "把阳光变成西瓜，供西瓜投手使用。";
        lesson.steps = {"1. 建在运输带旁边，箭头朝向运输带。", "2. 用运输带送到装填口，再指定目标路线。", "3. 开波后消耗阳光生产；堵塞时先检查出口。"};
        lesson.tip = "只有产能还不够：弹种、装填口路线和弹仓配额都要对应。";
        lesson.example = "一台生产机器如何给前线供货";
        Chain(lesson, {topic, Topic(M::Belt), Topic(M::Feeder), PlantTopic(plant)});
        lesson.edges.back().label = "本路弹仓";
        break;
    }
    case M::Belt:
        lesson.purpose = "把物料沿箭头送给相邻格里的接收设备。";
        lesson.steps = {"1. 放在产出机器与接收设备之间，不留空格。", "2. 指向运输带按 R 旋转，箭头就是出料方向。", "3. 转弯也要逐格相连；到前线前需经过装填口。"};
        lesson.tip = "运输带不隔空送货；它和远程控制用的电线是两回事。";
        lesson.example = "物料走实线箭头，相邻设备才能收货";
        Chain(lesson, {Topic(M::Grower), topic, Topic(M::Feeder), PlantTopic(PlantType::PLANT_PEASHOOTER)});
        lesson.edges.back().label = "本路弹仓";
        break;
    case M::Feeder:
        lesson.purpose = "从同一缓存供货，B 信号切换两套路线与弹种。";
        lesson.steps = {"1. 用运输带送货，再打开装填口配置。", "2. 两套方案分别设置路线和弹种筛选。", "3. B 低选方案1，高选方案2；A 可暂停输出。"};
        lesson.tip = "切换不清库存；同一路仍共用弹仓容量，筛选不匹配的物料会留在缓存。";
        lesson.example = "工厂与前线的桥梁：跨区域送入本路弹仓";
        Chain(lesson, {Topic(M::Grower), Topic(M::Belt), topic, PlantTopic(PlantType::PLANT_PEASHOOTER)});
        lesson.edges.back().label = "目标第 1 路";
        break;
    case M::Store:
        lesson.purpose = "先存下暂时用不完的物料，缓冲供需起伏。";
        lesson.steps = {"1. 把弹药仓插在生产机器与装填口之间。", "2. 箭头朝出口，弹种筛选可限定接收的物料。", "3. 上游有剩余时入库，下游有空位时再出库。"};
        lesson.tip = "仓库不会提高生产速度；满仓会堵住上游，内部物料拆除后丢失。";
        lesson.example = "先缓存，再补给前线";
        Chain(lesson, {Topic(M::Grower), topic, Topic(M::Feeder), PlantTopic(PlantType::PLANT_PEASHOOTER)});
        lesson.edges.back().label = "本路弹仓";
        break;
    case M::Splitter:
        lesson.purpose = "把一条物料线分给正前方和右侧两个出口。";
        lesson.steps = {"1. 放在产出机器后面，正前方和右侧都接设备。", "2. 保持混合且不接线：轮流分配，堵住会换另路。", "3. 若筛选弹种：该弹种固定走右侧，其余走前方。"};
        lesson.tip = "配置可选严格模式：B低只走前方，高只走右侧，堵住也不换路；A可停机。";
        lesson.example = "箭头朝右：正前方在右，右侧出口在下；R 旋转后一起转";
        lesson.nodes = {{Topic(M::Grower), Vector(355, 365), {}}, {topic, Vector(625, 365), {}},
            {Topic(M::Feeder), Vector(915, 365), {}}, {Topic(M::Store), Vector(625, 461), {}}};
        lesson.edges = {{0, 1}, {1, 2, false, "正前方"}, {1, 3, false, "右侧"}};
        break;
    case M::SalvagePort:
    case M::Recycler:
        lesson.purpose = topic == Topic(M::SalvagePort) ? "把击杀掉落的残骸提取到工厂物流中。" : "消耗残骸，回收阳光，给工厂补充能源。";
        lesson.steps = {"1. 前线真正击杀僵尸后，残骸进入共享队列。", "2. 回收入口接运输带，再接回收机。", "3. 回收机每加工一份残骸，获得 5 阳光。"};
        lesson.tip = "每波全基地最多掉落 40 份；越线不算击杀，准备时整条回收线停机。";
        lesson.example = "击杀 → 提取 → 运输 → 加工 → 阳光";
        Chain(lesson, {-2, Topic(M::SalvagePort), Topic(M::Belt), Topic(M::Recycler), -1});
        break;
    case M::Cooler:
        lesson.purpose = "给普通弹药降温，做出寒冰射手需要的寒冰弹。";
        lesson.steps = {"1. 育豆机向冷凝机送普通豌豆，选方案1。", "2. 成品送装填口，目标选寒冰射手所在路。", "3. 第8波后方案2开放，B信号可自动切配方。"};
        lesson.tip = "原料和成品各自缓存；切配方先做完在制品，再切换，保留余料和成品。";
        lesson.example = "进普通豌豆，出寒冰弹";
        Chain(lesson, {Topic(M::Grower), topic, Topic(M::Feeder), PlantTopic(PlantType::PLANT_SNOWPEA)});
        lesson.edges.back().label = "本路弹仓";
        break;
    case M::Switch:
        lesson.purpose = "手动发出开或关的指令，远程控制机器。";
        lesson.steps = {"1. 点接 A 口，再点开关，最后点育豆机。", "2. 选中开关，点配置所选来切换开与关。", "3. 信号亮时机器运行，暗时机器停机。"};
        lesson.tip = "虚线只传控制信号，不运输弹药；普通机器没接 A 口时默认运行。";
        lesson.example = "一个开关控制一台育豆机";
        Chain(lesson, {topic, Topic(M::Grower)}, true);
        break;
    case M::EnemySensor:
        lesson.purpose = "看指定路线的僵尸数量，满足条件才发出开启信号。";
        lesson.steps = {"1. 选中敌情器，目标路线设为要监视的路线。", "2. 配置所选调整数量门槛；设 1 就是有敌人。", "3. 接 A 口：先点敌情器，再点育豆机。"};
        lesson.tip = "看的是指定前线路线，不是它在工厂里的行号；多余资源可留给别路。";
        lesson.example = "第 1 路有敌人，就开机生产";
        Chain(lesson, {topic, Topic(M::Grower)}, true);
        break;
    case M::StockSensor:
    case M::Not:
        lesson.purpose = topic == Topic(M::StockSensor) ? "库存达到设定数量时亮起，供其他元件做判断。" : "把开变关、关变开，让条件反过来使用。";
        lesson.steps = {"1. 库存器选第 1 路，配置门槛为 20。", "2. 接 A 口：库存器 → 非门，再非门 → 育豆机。", "3. 库存不足就生产，达到 20 就暂停生产。"};
        lesson.tip = "库存器 A 口不接线时看该路弹仓；接到某台机器时改看那台机器库存。";
        lesson.example = "够了就断电，不够再开：把加工能力留给紧缺产线";
        Chain(lesson, {Topic(M::StockSensor), Topic(M::Not), Topic(M::Grower)}, true);
        break;
    case M::And:
        lesson.purpose = "有敌人而且缺弹时，才把共享产线转供这一路。";
        lesson.steps = {"1. 装填口方案1送第1路，方案2送第3路。", "2. 第3路敌情器接 A；库存器经非门接 B。", "3. 与门输出接装填口 B，满足条件就转供第3路。"};
        lesson.tip = "库存器按对应弹种设补给目标；缺弹请求也可送或门，控制共享生产线开工。";
        lesson.example = "第3路有敌 + 第3路缺弹 → 装填口切换至方案2";
        Gate(lesson, M::And, M::EnemySensor, M::Not);
        lesson.nodes[0].label = "第3路敌情器"; lesson.nodes[1].label = "库存器 → 非门";
        lesson.nodes[3].topic = Topic(M::Feeder); lesson.edges[2].label = "接 B 口";
        break;
    case M::Or:
        lesson.purpose = "两个条件有一个成立，就发出开启信号。";
        lesson.steps = {"1. 两路库存器分别接非门，得到两路缺弹请求。", "2. 两个请求分别接或门的 A 口和 B 口。", "3. 或门接生产线 A，任一路缺弹就开工。"};
        lesson.tip = "装填口 B 决定送谁；生产线 A 决定开不开。冷凝线的两台加工设备都要接 A。";
        lesson.example = "两路共享产能：任一路缺弹就开工，都够用才断电";
        Gate(lesson, M::Or, M::Not, M::Not);
        lesson.nodes[0].label = "第1路库存器 → 非门"; lesson.nodes[1].label = "第3路库存器 → 非门";
        break;
    case M::Timer:
        lesson.purpose = "周期性地关一会、开一会，让机器间歇运行。";
        lesson.steps = {"1. 选中定时器，配置所选设为 4 秒。", "2. 接 A 口：定时器 → 育豆机。", "3. 每轮先关 2 秒，再开 2 秒，然后重复。"};
        lesson.tip = "定时器 A 口可以再接开关；没接线时自动计时，准备阶段冻结。";
        lesson.example = "用节奏控制生产，不必反复手点开关";
        Chain(lesson, {topic, Topic(M::Grower)}, true);
        break;
    case M::Latch:
        lesson.purpose = "收到一次开启指令后保持开启，直到收到关闭指令。";
        lesson.steps = {"1. 库存器门槛10，接非门，再接记忆器 A。", "2. 另一库存器门槛30，直接接记忆器 B。", "3. 输出接机器 A：低于10开工，补到30再停。"};
        lesson.tip = "两个库存器看同一路、同弹种；避免门槛附近反复开关，B 停机优先。";
        lesson.example = "批量补仓：低库存启动，补足后释放电力";
        Gate(lesson, M::Latch, M::Not, M::StockSensor);
        lesson.nodes[0].label = "库存<10（库存器接非门）"; lesson.nodes[1].label = "库存≥30";
        break;
    case M::Counter:
        lesson.purpose = "数实际物料，按批次切换工厂的工作方式。";
        lesson.steps = {"1. 装填口方案1送第1路，方案2送第2路。", "2. 计数器设出库、10份、每批完成翻转。", "3. 装填口接计数器A，计数器接装填口B。"};
        lesson.tip = "图中两端是同一台装填口：每送10份切一路；也可选择原来的信号计数。";
        lesson.example = "按真正送出的份数轮换，缺料时不会空数";
        Chain(lesson, {Topic(M::Feeder), topic, Topic(M::Feeder)}, true);
        lesson.nodes[0].label = "装填口 · 统计出库"; lesson.nodes[2].label = "同一装填口 · 切方案";
        lesson.edges[0].label = "计量 A"; lesson.edges[1].label = "选择 B";
        break;
    case M::Cannon:
        lesson.purpose = "工厂直接为前线提供可调度的三连发瓜弹。";
        lesson.steps = {"1. 用物流送入瓜弹，两方案分别选路线和弹种。", "2. A 许可开火，B低选方案1，高选方案2。", "3. 充能12秒后消耗3瓜连射；无目标不扣弹。"};
        lesson.tip = "不限台数，新建从零充能；第8波后可选冰瓜，已开始的一轮会完整发完。";
        lesson.example = "先生产与备弹，再让电路决定火力投向";
        Chain(lesson, {Topic(M::MelonGrower), Topic(M::Belt), topic});
        break;
    default: break;
    }
    return lesson;
}
}

void WorkshopScene::DrawLockedItem(Graphics* g, float centerX, float top, float size) {
    const auto metal = glm::vec4(112, 112, 99, 255);
    g->FillCircle(centerX, top + size * .31f, size * .27f, metal);
    g->FillCircle(centerX, top + size * .31f, size * .17f, glm::vec4(216, 211, 183, 255));
    g->FillRect(centerX - size * .37f + 2, top + size * .38f + 3, size * .74f, size * .56f, glm::vec4(78, 70, 46, 160));
    g->FillRect(centerX - size * .37f, top + size * .38f, size * .74f, size * .56f, glm::vec4(167, 144, 72, 255));
    g->FillRect(centerX - size * .33f, top + size * .42f, size * .66f, size * .06f, glm::vec4(224, 205, 135, 255));
    g->FillCircle(centerX, top + size * .64f, size * .08f, kInk);
    g->FillRect(centerX - size * .035f, top + size * .66f, size * .07f, size * .14f, kInk);
}

/** 新解锁在准备阶段合并成一本图册；查看时暂停，关闭后保留原手动暂停状态。 */
void WorkshopScene::OpenGuide(int topic, bool newUnlocks) {
    if (mLoadFailed || (mGuide && mGuide->IsActive())) return;
    auto topics = Workshop::GuideTopics(World().CompletedWaves(), newUnlocks ? mGuideSeenWave : -1);
    if (topics.empty()) return;
    if (!newUnlocks && std::find(topics.begin(), topics.end(), topic) == topics.end()) return;
    mGuideTopics = std::move(topics);
    mGuidePage = newUnlocks ? 0 : static_cast<int>(std::find(mGuideTopics.begin(), mGuideTopics.end(), topic) - mGuideTopics.begin());
    mGuideUnlockWave = newUnlocks ? World().CompletedWaves() : -1;
    CancelMapGesture(); ClearFrontPreview(); mTooltipLines.clear(); mHoverKey = -1;
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(1012, 552);
    builder.Button(newUnlocks ? "稍后查看" : "返回", Vector(922, 43), Vector(114, 34), 15, [this] { CloseGuide(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("上一项", Vector(736, 523), Vector(126, 34), 15, [this] { --mGuidePage; RefreshGuide(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("下一项", Vector(878, 523), Vector(140, 34), 15, [this] {
        if (mGuidePage + 1 == static_cast<int>(mGuideTopics.size())) CloseGuide();
        else { ++mGuidePage; RefreshGuide(); }
    }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("上页", Vector(76, 516), Vector(76, 31), 14, [this] { mGuidePage = (mGuidePage / kContentsRows - 1) * kContentsRows; RefreshGuide(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("下页", Vector(163, 516), Vector(76, 31), 14, [this] { mGuidePage = (mGuidePage / kContentsRows + 1) * kContentsRows; RefreshGuide(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    for (int row = 0; row < kContentsRows; ++row)
        builder.Button("", Vector(76, 131 + row * 43.0f), Vector(163, 37), 13, [this, row] {
            mGuidePage = mGuidePage / kContentsRows * kContentsRows + row; RefreshGuide();
        }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    for (int mode = 0; mode < 3; ++mode)
        builder.Button(std::array<const char*, 3>{"1 全部开启", "2 手动关闲线", "3 电路自动关"}[mode], Vector(286 + mode * 224.0f, 264), Vector(214, 34), 15,
            [this, mode] { ResetPowerDemo(mode); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    mGuide = builder.Show();
    for (int i = 0; i < 8 + kContentsRows; ++i) {
        mGuide->GetButton(i)->SetTextColor(glm::vec4(255, 228, 142, 255));
        mGuide->GetButton(i)->SetHoverTextColor(glm::vec4(255, 250, 201, 255));
    }
    mGuide->SetCustomDraw([this](Graphics* g) { DrawGuide(g); });
    RefreshGuide(); DeltaTime::SetPaused(true);
}

void WorkshopScene::CloseGuide(bool acknowledge) {
    if (acknowledge && std::find(mGuideTopics.begin(), mGuideTopics.end(), Workshop::PowerGuide) != mGuideTopics.end()) {
        mPowerGuideSeen = true; SaveBase();
    }
    if (mGuide) mGuide->Close();
    mGuide.reset(); mGuideTopics.clear();
    mPowerDemo.reset();
    // 独立指南返回后退出拆除/接线等工具；嵌套指南只退回原配置或图册。
    if (acknowledge && !mUIManager.GetTopActiveMessageBox()) CancelTool();
    if (acknowledge && mGuideUnlockWave >= 0) {
        mGuideSeenWave = std::max(mGuideSeenWave, mGuideUnlockWave); SaveBase();
    }
    mGuideUnlockWave = -1;
}

void WorkshopScene::RefreshGuide() {
    if (!mGuide || mGuideTopics.empty()) return;
    mGuidePage = std::clamp(mGuidePage, 0, static_cast<int>(mGuideTopics.size()) - 1);
    const bool powerLesson = mGuideTopics[mGuidePage] == Workshop::PowerGuide;
    for (int i = 0; i < 3; ++i) mGuide->GetButton(13 + i)->SetEnabled(powerLesson && (i < 2 || World().CompletedWaves() >= 2));
    mGuide->GetButton(15)->SetText(World().CompletedWaves() >= 2 ? "3 电路自动关" : "守住 2 波后解锁");
    if (powerLesson && !mPowerDemo) ResetPowerDemo(0);
    if (!powerLesson) mPowerDemo.reset();
    mGuide->GetButton(1)->SetEnabled(mGuidePage > 0);
    mGuide->GetButton(2)->SetText(mGuidePage + 1 == static_cast<int>(mGuideTopics.size()) ? "看完了" : "下一项");
    const int first = mGuidePage / kContentsRows * kContentsRows;
    mGuide->GetButton(3)->SetEnabled(first > 0);
    mGuide->GetButton(4)->SetEnabled(first + kContentsRows < static_cast<int>(mGuideTopics.size()));
    for (int row = 0; row < kContentsRows; ++row) {
        const int index = first + row; auto button = mGuide->GetButton(5 + row);
        button->SetEnabled(index < static_cast<int>(mGuideTopics.size()));
        button->SetText(index < static_cast<int>(mGuideTopics.size()) ? TopicName(mGuideTopics[index]) : "");
        button->SetTextColor(index == mGuidePage ? glm::vec4(255, 241, 129, 255) : glm::vec4(219, 218, 189, 255));
    }
}

void WorkshopScene::CheckGuideUnlocks() {
    if (!mLoadFailed && !mPowerGuideSeen && !World().LegacyAssault() && World().State() == Workshop::Phase::Planning && !mUIManager.GetTopActiveMessageBox()) {
        OpenGuide(Workshop::PowerGuide); return;
    }
    if (mLoadFailed || World().State() != Workshop::Phase::Planning || mUIManager.GetTopActiveMessageBox()
        || World().CompletedWaves() <= mGuideSeenWave) return;
    if (Workshop::GuideTopics(World().CompletedWaves(), mGuideSeenWave).empty()) {
        mGuideSeenWave = World().CompletedWaves(); SaveBase();
    } else OpenGuide(0, true);
}

void WorkshopScene::DrawGuide(Graphics* g) {
    if (!mGuide || !mGuide->IsActive() || mGuideTopics.empty()) return;
    g->FillRect(44, 24, 1012, 552, glm::vec4(93, 68, 40, 255));
    g->FillRect(52, 32, 996, 536, kPaper);
    g->FillRect(65, 89, 185, 467, glm::vec4(215, 210, 169, 255));
    Label(mGuideUnlockWave >= 0 ? "守住第 " + std::to_string(mGuideUnlockWave) + " 波 · 新内容已解锁" : "戴夫的图解指南", 76, 49, 24, kGreen);
    Label(mGuideUnlockWave >= 0 ? "这次解锁" : "已解锁目录", 88, 101, 17, kGreen);
    const int topic = mGuideTopics[mGuidePage]; const auto lesson = Explain(topic);
    auto picture = [this, g](int id, float x, float y, float size) {
        if (id == Workshop::PowerGuide || id == Workshop::DispatchGuide || id == Workshop::FocusGuide) {
            g->FillRect(x, y, size, size, glm::vec4(60, 85, 51, 255));
            DrawUtilityIcon(g, "wire_a_tool", x + size * .15f, y + size * .15f, size * .7f); return;
        }
        if (id == 0) id = Topic(M::Grower);
        if (id == -2) DrawItemIcon(g, Workshop::Ammo::Scrap, x, y, size);
        else if (id == -1) g->DrawTexture(ResourceManager::GetInstance().GetTexture("IMAGE_REANIM_SUN1", false), x, y, size, size);
        else if (id >= Workshop::PlantGuideBase) DrawPlantPicture(g, Workshop::Plants[id - Workshop::PlantGuideBase].type, x, y, size, size);
        else DrawMachineImage(g, static_cast<M>(id), x, y, size / 57);
    };
    Label(TopicName(topic), 280, 93, 25, kGreen);
    picture(topic, 288, 144, 100);
    Label(lesson.purpose, 408, 137, 17);
    for (int i = 0; i < 3; ++i) Label(lesson.steps[i], 408, 175 + i * 29.0f, 16);
    if (topic == 0) Label("操作：B 图册 · R 旋转 · Esc 取消 · 长按左键拖图 · 滚轮缩放", 280, 261, 13);
    if (topic != Workshop::PowerGuide) Label(lesson.tip, 280, 282, 14, kGreen);
    g->FillRect(276, 312, 750, 192, glm::vec4(220, 224, 184, 255));
    Label(lesson.example, 291, 319, 16, kGreen);
    for (const auto& edge : lesson.edges) {
        const auto source = lesson.nodes[edge.from].pos, target = lesson.nodes[edge.to].pos;
        auto direction = target - source;
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        direction = direction * (1.0f / length);
        const auto start = source + direction * 34, end = target - direction * 34;
        const auto color = edge.wire ? kWire : kGreen;
        if (edge.wire) {
            for (float d = 0; d < length - 68; d += 14) {
                const auto a = start + direction * d, b = start + direction * std::min(d + 8, length - 68);
                g->DrawLine(a.x, a.y, b.x, b.y, color);
            }
        } else g->DrawLine(start.x, start.y, end.x, end.y, color);
        const Vector normal(-direction.y, direction.x);
        const auto a = end - direction * 8 + normal * 4, b = end - direction * 8 - normal * 4;
        g->DrawLine(a.x, a.y, end.x, end.y, color); g->DrawLine(b.x, b.y, end.x, end.y, color);
        const auto middle = (source + target) * .5f;
        Label(edge.label, middle.x + (std::abs(direction.y) > .8f ? 45 : -34), middle.y - 19, 12, color);
    }
    for (const auto& node : lesson.nodes) {
        picture(node.topic, node.pos.x - 25, node.pos.y - 25, 50);
        const auto name = node.label.empty() ? TopicName(node.topic) : node.label;
        const float width = g->MeasureTextWidth(name, ResourceKeys::Fonts::FONT_FZCQ, 13);
        Label(name, node.pos.x - width / 2, node.pos.y + 27, 13);
    }
    if (topic == Workshop::PowerGuide) {
        DrawPowerDemo(g);
        Label(lesson.tip, 280, 508, 12);
        for (int i = 0; i < 3; ++i) mGuide->GetButton(13 + i)->Draw(g);
    } else Label("实线：物料/资源去向  ·  虚线：信号或计量连接，按标注接 A/B", 280, 508, 12);
    Label(std::to_string(mGuidePage + 1) + " / " + std::to_string(mGuideTopics.size()) + "  ·  查看时暂停，随时可重看", 280, 542, 13);
    for (int i = 0; i < 5; ++i) mGuide->GetButton(i)->Draw(g);
    const int first = mGuidePage / kContentsRows * kContentsRows;
    for (int row = 0; row < kContentsRows && first + row < static_cast<int>(mGuideTopics.size()); ++row) mGuide->GetButton(5 + row)->Draw(g);
}
