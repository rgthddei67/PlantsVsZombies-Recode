# 子系统历史索引

- [矿场9-3/9-4与新植物僵尸设计](../superpowers/specs/2026-09-11-gloomcrystal-mine-9-3-9-4-design.md) — 2026-09-11 设计未实现：9-3改30波，晶角矿工前移并去路障加普通巨人/钟匠；9-4改40波并加开凿/急救员/普通冰车；保留雾潮25%减伤、回声菇100伤/2秒/六格分流及晶角矿工六格冲撞/500伤；20岩壁矩阵候选、9-4完整池与特殊矿道交互待确认

- [设计问题集中询问](feedback_design_question_batching.md) — 2026-09-11：同一植物、僵尸或机制的全部可预见待定问题一次编号问齐并给建议；已确认不重问，仅新分支补问，当次明确单问优先

- [HXY专属轻松模式](project_pvz_hxy_mode.md) — 2026-09-11 控制台默认关闭，新局锁定难度1的70%预算、额外300阳光与所有防具75%当前/最大生命；关卡独立存档、治疗上限与连续读档防重乘

- [开凿僵尸与9-2](project_pvz_excavator_zombie.md) — 2026-09-05：800本体、8秒永久开墙一次、5秒重试；收益寻路/预留竞争/存档、普通骨架装备与带帽断头，9-2第三波教学和每波一只上限，专项及矿道回归入口齐备

- [搬搬藤与植物整组搬运](project_pvz_carry_vine.md) — 2026-09-05：9-1奖励、75阳光/20秒；全场合法空占地整组瞬移，保留实体与能力状态，扶梯随组移动，解除旧啃食并正常接受落点接触；两份可见专项及快照比较入口已补齐，卡图独立居中

- [Android ARM64 首次移植](../../android/README.md) — 2026-09-05 NDK/Gradle、GLES、触屏工具栏与后台保存；模拟器实测后修复 reanim 路径大小写及图鉴切场景后访问旧对象，1311 张动画纹理恢复；排除 GameMonitor，真机仍待验收

- [第九大关幽晶矿场](project_pvz_gloomcrystal_mine.md) — 2026-09-05 主人逐项批准固定矿道、75阳光/8秒单格开凿、无环最短路径、岩壁射界与精致矿洞美术；已实现9-1与公共地图机制；矿道工具仅开战显示、岩壁不盖选卡、施工进度置于岩壁上方；祭司/钟匠能力跨地图并保留矿道回位、撑杆到行后起跳，clang-release可见专项与独立防环测试通过；9-2与开凿僵尸现已实现，详见独立主题

[返回文档导航](../README.md) · [当前系统入口](../systems/README.md)

> 本页摘要用于定位历史主题。带日期的实现、测试和性能数字需要按当前源码与证据复核；索引中的完成标记不等于今天重新验收通过。

> Codex routing: required always-on rules live in `../../AGENTS.md`; detailed build, AutoTest, architecture, resource, and implementation guidance lives in `../agent-guide/PROJECT_GUIDE.md`. The entries below are historical subsystem context and should be read only when relevant.

- [AutoTest 套件与验证矩阵](project_pvz_autotest_suite.md) — 2026-08-27 验证按实际改动面分流：新植物、新僵尸、新粒子或纯出怪/逻辑本身只跑默认 `clang-release` 可见专项；只有改动渲染后端、后端兼容路径或跨后端提交实现时才加跑 `-NoInstance` 和强制 OpenGL

- [第八大关极夜雪原核心环境](project_pvz_polar_night_snowfield.md) — 2026-09-05 修正五路雪穴延迟出怪后血量采样滞后导致异常连跳波：兑现时同步刷新，本波全部兑现后锁定提前出波阈值；8-1～8-9 与极夜无尽 1007 共用环境，隔离冒险教学/终波。第八大关只是当前登记边界，不是最终区域
- [第八大关潜雪僵尸](project_pvz_snow_burrow_zombie.md) — 2026-09-04 `ZOMBIE_SNOW_BURROW` 为 700 本体/50 啃咬、无防具；出生两格潜雪与半血 0.8 秒可中断前摇后一格潜雪最多各一次，自然出雪只伤战斗顶层 150，地面能力经目标接口强制出雪并取消冲击；开始出土后残血死亡必须播放死亡轨而非直接删除；8-1 每波/同时限一且雪穴保底，8-2 每波/同时限二，阶段、延迟出生与计数均入档；8-1 通关后解锁图鉴
- [第八大关听雪草](project_pvz_listening_grass.md) — 2026-08-28 `PLANT_LISTENINGGRASS` 为 8-1 奖励，75 阳光/20 秒卡冷/300 生命；本行优先按最靠近房屋、稳定 ID 迫出一个敌对地下目标，否则封闭一个形成中或活动雪穴，两种成功响应共享 6 秒内部冷却并入档；运行时完整复用经典叶子保护伞时间轴与哈希锁定染色分件
- [第八大关适应头盔僵尸与植物伤害来源](project_pvz_adaptive_helmet_zombie.md) — 2026-08-30 `ZOMBIE_ADAPTIVE_HELMET` 为 800 本体/100 头盔；击穿整击无溢出并永久免疫原植物谱系或统一灰烬数值伤害，附带状态保留、毒伤归毒囊射手；`PlantDamageOrigin` 锁定原发射者且跨火炬/对象池/存档；钟匠窄快照可恢复 NONE/植物谱系/ASH 并原子对齐头盔胸章；8-3 白毛风后保底，8-3/8-4与后续生存池每波4且无同时或累计上限
- [第八大关机枪射手与极光树桩](project_pvz_gatling_aurora_torchwood.md) — 2026-08-29 奖励已随极夜支援植物重排为8-3极光树桩、8-4机枪射手；两者均为225/30/300紫卡，四连发与50伤四目标极光弹玩法不变
- [第八大关热感狙击僵尸](project_pvz_thermal_sniper_zombie.md) — 2026-08-29 已实现：800本体/0.8倍移速/50啃食；玩家种植时同排全部就绪个体各自0.3秒瞄准并从真实肩炮口发射基础生命等伤害的双向热脉冲，沿途植物逐发拦截、原位置终止、独立3秒装填；8-5每波限3、8-6每波限4，均无同时或累计上限
- [第八大关极光祭司、极夜钟匠与终盘植物](project_pvz_area8_rift_clockwork_finale.md) — 2026-08-30 已实现 8-7/8-8：祭司提交后5秒循环且累计最多释放3次（中断不计，次数存档并随钟匠记录回溯），钟匠提交后10秒循环，两者须进入最右列后才可起施法前摇；2026-09-05解除能力提交/读档的极夜地图限制并适配矿道连续回位；时间锚6秒后恢复核心状态，并精确恢复祭司/钟匠局部阶段及适应头盔的 NONE/植物谱系/ASH，存活目标的断头/断臂逻辑与异形骨架轨道会在原对象静默重建，其余品种不承诺内部技能倒带，外部 Board 事务不回滚且来源不自刷；界碑花改道离散入场，曙光莲结算三红模块；schema 12、v11提交态迁移、独立逆转钟面和复合粒子已闭环
- 2026-08-30 回溯补充契约：局部能力阶段恢复后最终对齐存活对象的啮食关系、品种 eat 表现与植物 `mEaterCount`；已进入 `mIsDying` 的倒地壳用不改变 `mZombieNumber` 的计数所有权移交替身并静默退役，雪橇四人编队同时从新锚与旧档锚结算排除。
- [第八大关北极星花与冰镜草](project_pvz_polar_night_support_plants.md) — 2026-08-29 已实现：奖励为8-2北极星花、8-3极光树桩、8-4机枪、8-5冰镜草、8-6无奖励；北极星花125/30/300，以12秒蓄能换8秒3×3按需导航；冰镜草100/20/300，每12秒逐面凝结且最多同时两面，一镜抵消一枚穿格敌方水平直射弹

- [冬日花园僵尸图鉴资料与预览](project_pvz_zombie_almanac_progression.md) — 2026-08-26 冰墙工程师的图鉴缺失源于 `info.txt` 标题/说明键未登记，而非 spawnlist 解锁；天气干扰僵尸的预览无 Board 必须保持 `anim_idle`，不能误触设备失败收口改成 `anim_walk2`。默认与 `-NoInstance` 图鉴专项均通过

- [大嘴花拒吞伤害契约](project_pvz_chomper_rejected_bite.md) — 2026-08-15 `TakePlantInstantKill()` 只报告是否确实吞下；拒吞默认由大嘴花经正式 `PLANT` 伤害链造成20，巨人/红眼/屋脊督军使用默认值；既有特殊值为加固铁门10、镀金冰车50，持杆跳跳因高度咬不到为0且掉杆后恢复普通吞食
- [经典巨人、红眼巨人与小鬼](project_pvz_gargantuar_zombie.md) — 2026-08-27 普通/红眼巨人按3000/6000本体生命的2/3与1/3派生可逆伤势；红眼以4500权重只投放7-8/7-9，冒险每波最多3只且计数入档、无尽不受限；劫持者处决保留头臂；第93帧逐层派发锤击，第131帧按有符号半场距离唯一投出112px高度小鬼，并按旧轨迹落地时长反算竖直初速，保留视觉抬高但不再因1.3倍时长越过草地预期投掷区；内嵌/独立 `Zombie_imp_body1` 渲染锚点接续与 `anim_thrown` 一次播放合同保持
- [全僵尸黄油头贴跟随与绘制层级](project_pvz_zombie_butter_overlay.md) — 2026-08-27 基类虚语义轨道默认 `anim_head1`，异形/车辆只覆写专属头轨；命名 vector follower 默认继承状态 overlay，承伤装备可显式继承父轨 additive glow，黄油 `butter_splat` 显式退出两者并可与冰像处刑者黑盔同轨共存；base→overlay→glow 在实例、NoInstance 和 OpenGL 可见专项闭环
- [经典叶子保护伞与空中威胁防御](project_pvz_umbrella_leaf.md) — 2026-08-08 `PLANT_UMBRELLA` 100阳光/7.5秒/300生命，以能力接口覆盖自身及周围八格；0.05秒展开后反弹75伤篮球并让蹦极立即空手上升，阶段/计时入档且不重播反馈；无新增动画帧事件，默认/NoInstance专项及蹦极/投篮车回归可见通过
- [经典投篮车与导流投篮车僵尸](project_pvz_catapult_zombie.md) — 2026-08-08 普通型850生命/十二发75伤篮球/第46帧/3秒装填；导流精英1000生命并完整继承弹药状态机，屋顶自然锁行时最近房屋的坡段候选只替换一条随机行，行掩码锁后不因死亡重抽，只有自身径流从-60放大到-100且爆胎回退；正式每波最多一只并入档，5-6在普通投篮车之后首次登场；独立青蓝资源/爆炸与共享篮球键已闭环，专项、上限、出怪表和父回归均可见通过且日志0 ERROR/WARN
- [经典大蒜与僵尸跨行反应](project_pvz_garlic.md) — 2026-08-08 `PLANT_GARLIC` 50阳光/7.5秒/400生命；首口50伤后由 Zombie 独立嫌恶状态在0.7秒停吃、1.7秒同介质相邻行改道、2.7秒结束，逻辑 `mRow` 先切换而 Y 以100px/s追赶；报纸破盾原子取消、魅惑继续，阶段/Y/恶心脸存档；通用 grossout 头按透明像素框上移15px；clang-release、默认/NoInstance各196命令可见专项及报纸父回归通过
- [屋顶地形与僵尸连续坡面](project_pvz_roof_terrain_foundation.md) — 2026-08-10 5-1～5-9 正式使用白天 `ROOF`，6-1～6-9 正式使用 `NIGHT_ROOF`；昼夜屋顶共用 Board 连续1:4坡面、三列后期花盆和全部地形消费者
- [昼夜屋顶坡面径流](project_pvz_roof_runoff.md) — 2026-08-10 `ROOF/NIGHT_ROOF` 按背景共用雨势积累、3秒预警与2.2秒冲刷；一次不重复锁定1～3行（50/35/15），结束兑现预抽并入档的30%～60%残留湿度；普通僵尸以60px/s向屋檐漂移，导流投篮车只放大自身到100px/s
- [黑夜屋顶雷荷与基础放电](project_pvz_night_roof_charge.md) — 2026-08-29 雷荷推进、路线推演、放电事务和恢复入口已迁入 `Board/BoardRoofWeather.cpp`；仅 `NIGHT_ROOF` 启用，晴/小/中/大雨基础为-0.5/1/2/3点每秒，有效劫持者令雨中固定再+4.1且多只不叠加，满100锁行预警4秒再放电0.65秒，活动期正向输入截留最多25%余电
- [第七大关冬日花园、寒潮与冻融线](project_pvz_winter_garden.md) — 2026-08-26 7-1～7-9 接入 `WINTER_GARDEN` 五行平地；左侧卡通温度计统一显示 +6/0/-12°C；每轮预锁弱/普通/强寒潮、最低温 ±1°C、三段随机时长和 0～2 三种稳定霜线轮廓，准确预报与存档共用；7-8/7-9 首次 `GAME` 后固定 12 秒预报、15 秒降温、50 秒极寒、30 秒回暖，之后恢复随机；冻土只按真实温度从僵尸侧推进，低温把同档雨势改画为雪并关闭雨声/水花/普通闪电，地图全路径禁用台风；温室遮挡的前两行不创建且旧档不恢复小推车
- [第七大关组合威胁与寒潮植物](project_pvz_winter_area_content_plan.md) — 2026-08-26 五种重点僵尸与五株寒潮植物均已接入；7-1～7-9 重排为25～45波高压生态，每关以冬季主机制搭配不同高速、绕后、远程、爆破或对空压力；7-8/7-9 以红眼和五种重点僵尸压轴，7-9再由急救员维持耐久目标，避免西瓜+冰瓜成为万能解
- [第七大关雪橇车队僵尸](project_pvz_bobsled_team_zombie.md) — 2026-08-25 `ZOMBIE_BOBSLED_TEAM` 一次正式候选生成四名真实成员；乘车/落地按队长 X 回收，拆队始终直接纳入队长，避免出生区误清队及预览销毁后计数残留；选卡与图鉴详情均为四人共乘完整雪橇，最终波零实体旧档可直接恢复奖杯；雪锚收束、存档与双绘制路径专项闭环
- [第七大关冰墙工程师](project_pvz_ice_wall_engineer.md) — 2026-08-25 `ZOMBIE_ICE_WALL_ENGINEER` 完全进场即部署 600 生命未完成墙，4 秒硬化至 1800 后以 14px/s 推进；自动卷心菜、玉米/黄油、西瓜/冰瓜、融雪团/盐晶均优先锁墙且伤害/状态不穿透，锁定意图入档；手动玉米炮与未锁定测试抛射仍越墙；回暖融化、火焰双倍、盐晶 20＋200及施工/波次合同不变
- [第七大关冰裂钻机](project_pvz_ice_crack_drill_zombie.md) — 2026-08-27 `ZOMBIE_ICE_CRACK_DRILL` 650 本体＋900 钻机层，在冻土蓄力3.5秒后提交180px/s同行地裂；Board 统一快照 overlay/pumpkin/normal/under 四层并各造成177，存活冻土上的雪锚果可反复把后续伤害限制到1/3（取整59），多株不叠乘；50%独立钻机由右手握持且只微颤；7-4首发、7-5/7-8/7-9复习、每波限三
- [第七大关气象干扰僵尸](project_pvz_weather_jammer_zombie.md) — 2026-08-26 `ZOMBIE_WEATHER_JAMMER` 为270本体＋1100可磁吸铁桶，完全进场即抢占啃食并原地施法4秒，每次向整栏黑障余时追加30游戏秒且截获期间新广播；同类可在活动窗口内继续起手，当前余时上限300秒而每波/整局口径独立；栏内只留统一故障文案、独立温度计保留，真实锁定结果不变；外部中断重启5秒，死亡/断肢/魅惑永久消费；2600权重、第五波/生存13轮、每波限二、7-6第三波保底，稳定前臂握持/连续雷达循环、schema v5和双路径可见专项闭环
- [第七大关伏霜雷](project_pvz_frost_mine.md) — 2026-08-25 `PLANT_FROSTMINE` 为7-4奖励、50阳光/30秒/300生命；准确预报预测本格冻结时校准，真实冻结后永久武装；干扰仅清未提交校准；首个稳定ID地面目标触发1000冰制层腐蚀＋600常规伤害并中断未提交动作；三态完整独立立绘、粒子、存档和双路径专项闭环
- [第七大关警铃草](project_pvz_alarm_bell_flower.md) — 2026-08-27 `PLANT_ALARMBELLFLOWER` 为7-6奖励、25阳光/50秒/300生命；按本行最短未提交余时和稳定ID只打断一项动作且零伤害，气象干扰重启、钻机/工程师完整重试，已提交结果不回滚；完整Blover时间轴与分件统一青蓝换色，只在茎秆挂24px小铃标识，存档不重触发
- [第七大关炉芯花](project_pvz_furnace_core_flower.md) — 2026-08-26 `PLANT_FURNACECOREFLOWER` 为175阳光/20秒/300生命，暖于0°C时每10游戏秒充一枚且最多2枚；按稳定ID消费一枚，在冰像封存提交前保护3x3内其他植物并让来源能力直接SPENT；同源卡图/场上木炉芯、存档和双路径可见专项闭环
- [第七大关冰像处刑者](project_pvz_ice_statue_executioner_zombie.md) — 2026-08-27 `ZOMBIE_ICE_STATUE_EXECUTIONER` 用共享48-rollout短视推演选择冻土上的关键持久植物，候选从t=0停机并按真实2.5秒/40伤/植物专属锤数延迟处决；普通植物三锤、任一轮在冻土的玉米炮七锤；黑盔 follower 继承减速覆盖与实际承伤 glow，炉芯阻断、防具/灰烬、存档和每波限五保持
- [第六大关绝缘僵尸](project_pvz_insulator_zombie.md) — 2026-08-13 300本体+单层1200陶瓷胸甲；干燥轻弹/尖刺减半、1.5格同阵营放电承接后15秒2.2倍过载与100啃咬，湿润植物伤甲1.5倍且湿坡放电固定碎甲360无溢出；磁力菇整甲吸取自身扣150；胸甲为Zombie_body末尾前景follower，6-2首次、正式每波限2，专项与父回归可见闭环
- [第六大关劫持者僵尸](project_pvz_hijacker_zombie.md) — 2026-08-23 初始1000本体/首次锁定当前与最大生命各+1000/50啃咬/成本2000/每波限2；75%择最高当前可计生命锁定，处决线文字与面板高度只在有效锁定期间出现；满电7秒预警/最后1秒终局动作，释放以实时处决线同帧快照且生存封顶1200；成功释放后封锁本波余下候选并完整跳过后续2波，取消不冷却、第三波恢复且跨存档/生存换轮保留；6-4第7波教学、6-5组合、生存15轮
- [第六大关急救员僵尸](project_pvz_healer_zombie.md) — 2026-08-22 800本体/50啃咬/5秒共享冷却，群疗/单疗每层100/400；MC开启时以40 rollout/7秒/最多16只比较无上限群疗、全部单疗与0.5秒有界等待；治疗夹紧现存本体/盔/盾并通过虚修复入口重建全部可逆破损外观，巨人身体/外臂/脚/头跨阈值恢复已由默认/NoInstance可见专项锁定；关闭或不适用时回退3伤员群疗/劫持者优先单疗
- [第六大关接地僵尸与通用控制免疫](project_pvz_grounding_zombie.md) — 2026-08-15 270本体+1200电紫天线路障；黑夜屋顶满电时以32 rollout/10秒统一比较五条普通行和全部有效引雷路线，受引导放电只停机植物且不伤僵尸，有效引雷者为130像素内同阵营僵尸清除减速/冻结/黄油并发放30秒同类免疫，麻痹/灰烬/普通伤害仍有效，离域或掉帽后既得免控保留；通用按控制类型计时/永久mask、存档、植物控制画像、有序弱索引、6-8/6-9出怪和每波限2闭环
- [第六大关磁暴菇与条件磁吸推演](project_pvz_gold_magnet.md) — 2026-08-22 `PLANT_GOLD_MAGNET` 为6-7奖励：磁力菇+75阳光升级、50秒卡冷却、300生命、12秒吸取充能；按原版不是夜间植物，拒绝Setup/升级继承/读档睡眠，白天无需咖啡豆即可工作且推演`daytimeDormant=false`；成功剥离装备后100px无伤麻痹2.5秒，扶梯不触发且绝缘150反噬后置；原生1.0倍实盆站位和短紫环闭环
- [第六大关黑夜屋顶完成状态](project_pvz_sixth_area_night_roof_backlog.md) — 2026-08-26 当前定案内容已完成：6-1～6-9黑夜屋顶、坡面径流、独立雷荷、6-9完整迷雾、5-9接地菇、6-2～6-8植物奖励，以及绝缘/劫持者/急救员/接地四种专属僵尸及逐关编排均已闭环；6-1与6-9按设计无植物奖励，6-9不设BOSS槽，旧“僵尸博士放在6-9”只保留为历史提案而非待办
- [第六大关避雷花盆](project_pvz_lightning_rod_pot.md) — 2026-08-15 `PLANT_LIGHTNINGRODPOT` 为6-4奖励：150阳光/50秒/700生命，under层原位升级花盆并保留上层；单格宿主要求同格，多格宿主任一占格下的有效盆都保护整株免普通雷荷停机与劫持者处决；有效承载时同排雷击伤害翻倍且多盆不叠加
- [第六大关冰瓜](project_pvz_winter_melon.md) — 2026-08-25 `PLANT_WINTERMELON` 为6-5奖励：200阳光/50秒/300生命，第44帧投出100直击/33三行溅射并减速10秒；同行冰墙优先且只承受100直击，墙后/邻行不溅射或减速；升级、护盾、资源与双路径专项合同见主题文件
- [经典玉米加农炮与双格植物占用](project_pvz_cob_cannon.md) — 2026-08-28 `PLANT_COBCANNON` 为6-6双格单实体；准星第二次左键释放覆盖全战场无Cell区域，屋顶按连续坡面推导行但保留精确像素爆点、避免原版“上界之风”；词条/选卡等非GAME状态统一阻断玩法输入；冰像七锤、MC炮击、第78帧、2秒飞行、115px三行1800灰烬及双格危险契约保持

- [第六大关接地菇](project_pvz_grounding_shroom.md) — 2026-08-13 `PLANT_GROUNDINGSHROOM` 为5-9奖励：100阳光、20秒冷却、500生命；同排三格免一次雷荷停机，普通/湿坡每次直接反噬100/150且按冻结分配不回滚；范围内绝缘僵尸拒绝承接和过载；独立低精度整株动画约0.8倍、卡图约0.7倍；白天睡眠轨从闭眼切图的第26帧起循环，绝不闪回第25帧睁眼图；震击电弧以紫罗兰暗边和浅紫亮芯抵抗缩放与黑夜暗化
- [选卡界面普通卡 48/1 翻页与模仿者独立入口](project_pvz_choose_card_pagination.md) — 2026-08-23 50 张注册植物中 49 张普通卡按 48 张分页、磁暴菇独占第二页；模仿者使用紧贴面板右下角的固定 AddOn 背景与独立 Card，选择窗新建临时 Card 并排除紫卡
- [经典模仿者与原版褪色滤镜](project_pvz_imitater.md) — 2026-08-23 模仿者独立选卡入口、固定 AddOn、临时目标 Card、紫卡门禁、复合上次选卡身份与同 ID 原地变身闭环；普通/较轻 HSL 褪色贯通 Vulkan batch/instance、NoInstance 和 OpenGL 3.3；变身烟保留 Circle/Away 切向/径向场并用几何探针锁定居中
- [经典忧郁菇与紫卡升级](project_pvz_gloomshroom.md) — 2026-08-13 `PLANT_GLOOMSHROOM` 为6-2奖励：150阳光、50秒冷却、300生命；原子覆盖大喷菇并保留承载层/南瓜及睡眠唤醒进度；每2秒按原版0.64～1.58秒时间线发四轮八向云雾与20点环形伤害；紫卡裁取主人seeds.png第二格；八炮口各5颗按双重Offset规则贴口扩散，Vulkan/OpenGL专项与升级/存档/奖励父回归可见闭环
- [经典双子向日葵与生产型紫卡升级](project_pvz_twin_sunflower.md) — 2026-08-13 `PLANT_TWINSUNFLOWER` 为6-3奖励：150阳光、50秒冷却、300生命；原子覆盖向日葵并保留承载层/南瓜，15秒一轮同时生产2颗普通阳光，发光中途存档不重复；默认与NoInstance专项可见闭环
- [经典花盆与屋顶承载层](project_pvz_flowerpot.md) — 2026-08-14 `PLANT_FLOWERPOT` 25阳光/7.5秒/300生命/1秒无啃食；under+normal+南瓜分层、屋顶门禁、5-1/5-2/后续5/4/3列初始布局、覆盖暂停、5px视觉抬升、通用ShadowComponent 46px可见阴影、双向台风整组与存档；MC 中与睡莲压缩进独立64格支撑层，不占128株详细植物容量
- [上次选卡持久化与一键动画恢复](project_pvz_last_selected_cards.md) — 2026-08-23 普通卡继续保存稳定枚举名；模仿者以 `PLANT_IMITATER:PLANT_TARGET` 保存代理身份与目标，恢复时重新验证目标资格并复用飞入动画
- [疯狂戴夫关卡闲聊与隐性机制提示](project_pvz_crazy_dave_tutorial_dialog.md) — 2026-08-26 2-1、4-1、4-2、4-9、5-1、6-1、6-9、7-1、7-8、7-9 以原版风格闲聊含蓄提示天气、雾、燃料、屋顶、雷荷与寒潮；7-8/7-9 在选卡前提示开局强寒潮并说明回暖后恢复随机，完成/跳过一次记录，玩家 schema v5、原版 JPG 灰度 alpha 遮罩、原版 12 段短/长/超长/疯狂语音及切页停声；1-1、3-1不出现
- [架构边界、存档版本与技能审计门禁](project_pvz_architecture_boundaries.md) — 2026-08-29 全部 `Board*` 文件已归档到 `Game/Board/`；前五批环境拆分分别位于 `BoardWinterClimate.cpp`、`BoardPolarNight.cpp`、`BoardWeather.cpp`、`BoardFogWeather.cpp`、`BoardRoofWeather.cpp`，第六批战术推演位于 `BoardTacticalAI.cpp`；`Board` 公共门面、状态权威、字段布局和存档键不变，核心创建与波次入口继续留在 `Board.cpp`
- [OpenGL 3.3 Core 兼容后端](project_pvz_opengl33_backend.md) — 2026-09-05 CPU Batch 改为整组上传后保序分段 DrawArrays，减少选卡/雾片纹理切换时 VBO/IBO 重复上传；Android/Windows 构建通过，主人自行验机，改后性能待测；4.3 SSBO、Vulkan 与 Pool 专用路径保持原实现
- [空格轻量暂停、可选高级暂停与粒子冻结](project_pvz_space_pause_ui.md) — 2026-08-23 暂停仍保留 UI 零 dt 逻辑步，但 ParticleEmitter 完整冻结上一游戏帧，Shake 不重抽、Friction 不衰减；空格只显示上方中央“游戏暂停”，高级暂停默认关闭且只在主菜单控制台设置，暂停倍速仅待选，泳池相位同样冻结
- [经典咖啡豆、蘑菇睡眠 Z 与唤醒](project_pvz_coffeebean.md) — 2026-08-11 白天沉睡植物以独立 `Z.reanim` 按原版6～8fps随机相位循环，位置适配当前视觉锚点且醒来/压扁/失活即移除；`PLANT_INSTANT_COFFEE` 仍以短时 overlay 等待1秒后碎裂并启动1秒唤醒，资源、存档、台风与默认/NoInstance可见专项闭环
- [植物立即死亡的可见生命周期](project_pvz_plant_die_visibility.md) — 2026-08-04 `Plant::Die()` 在延迟销毁前立即失活，避免 `StopAnimation()` 重置轨道后仍被当帧绘制；咖啡豆、倭瓜等瞬时消耗植物不再闪回起始姿态；`clang-release` 编译通过，按主人要求未跑 AutoTest、待亲自目验
- [经典海蘑菇](project_pvz_seashroom.md) — 2026-07-29 `PLANT_SEASHROOM`：0 阳光、10 秒冷却，只能直接种在空水格且占 normal 层；复用小喷菇 1.5 秒间隔/300px 短程孢子，第 33 帧发射，无陆地阴影并随水面浮动；白天睡眠与双绘制路径可见专项通过
- [最终绘制坐标语义取证](project_pvz_render_coordinate_evidence.md) — 2026-07-27 AutoTest 从当前项目实际渲染路径导出植物/僵尸/动画特效的 Animator 世界包围盒，以及粒子最终矩形；断言使用相对视觉原点、发射点和最近实体 collider 的整数投影，默认实例化与 `-NoInstance` 可做同用例一致性核对，C# 800×600 绝对坐标只作行为语义参考
- [植物与僵尸逐行绘制深度](project_pvz_row_depth_render_order.md) — 2026-08-11 战场主体改为 `row N 植物→row N 僵尸/扶梯→row N+1 植物`，同排僵尸仍在上且下一行可遮挡上一行越界身体；小推车/子弹语义层不变，动态换行刷新绘制号，默认/NoInstance 屋顶专项通过
- [仙人掌与帧伤尖刺](project_pvz_cactus_frame_damage.md) — 2026-08-12 当前源码 `BULLET_SPIKE` 为逐逻辑帧2伤，但既有 smoke_cactus 仍断言历史3并会失败，待主人定案后统一；空/地分层、对象池、跨倍速累计与四目标穿透契约不变，绝缘干甲专项按当前基线验证帧伤减半为1
- [经典三叶草](project_pvz_blover.md) — 2026-07-30 `PLANT_BLOVER`：100 阳光、10 秒冷却，第44帧按卡槽右键方向结算；卡图独立×0.9；气球朝屋后以600px/s累计滑行400px，朝前线滑出屏幕后死亡；台风中同步改持续风和活动阵风，不驱散迷雾；连续版本已编译、主人亲测待完成
- [经典杨桃与五向星弹](project_pvz_starfruit.md) — 2026-07-31 `PLANT_STARFRUIT`：125 阳光、7.5 秒冷却，第27帧同帧发出左/上/下/右上/右下五颗20伤星弹；复刻 C# 跨行预测索敌、随机自旋、动态行碰撞、对象池与存档，命中用 `StarSplat`；原版杨桃不画通用植物影子；`clang-release` 默认/NoInstance 可见专项均通过
- [经典卷心菜投手与解析抛物线](project_pvz_cabbagepult.md) — 2026-08-25 `PLANT_CABBAGEPULT`：100阳光、7.5秒、40伤、第43帧发射；同行冰墙优先且可单独触发攻击，显式锁墙弹跳过墙后僵尸并入档；未锁定抛射、平均根速度预判、护盾、对象池与双路径合同见主题文件
- [经典玉米投手、黄油弹与定身](project_pvz_kernelpult.md) — 2026-08-25 `PLANT_KERNELPULT`：75%玉米粒20伤、25%黄油40伤并定身4秒；同行冰墙优先，墙只受对应直击且墙后僵尸不定身；起手弹型、显式锁墙弹道、护盾、对象池和黄油时间均保持存档/复位合同
- [经典西瓜投手、三行溅射与卡图布局](project_pvz_melonpult.md) — 2026-08-25 当前 `PLANT_MELONPULT`：325阳光/10秒、第44帧发射，120直击/40相邻行溅射；同行冰墙优先且只承受120直击，墙后/邻行不溅射；显式锁墙弹道、护盾、对象池、资源、卡图和解锁合同见主题文件
- [毒囊射手与目标级叠毒](project_pvz_toxic_peashooter.md) — 2026-08-22 `PLANT_TOXICPEASHOOTER` 为3-8奖励：125阳光、15直击、每层6秒/0.2秒1伤、每目标共享上限20层；第二十一发刷新最短层，火炬转独立紫焰毒火豆，30px内直击/溅射目标均附毒，魅惑清毒；二十层状态改为中毒时才分配的紧凑侧车，原定长存档形状、盾牌/倍速/对象池专项保持
- [经典南瓜头与第三植物层](project_pvz_pumpkin_shell.md) — 2026-08-05 `PLANT_PUMPKINSHELL`：100 阳光、30 秒冷却、4000 生命；独立 pumpkin 层可包住普通植物，战斗顶层优先啃食，但非阻拦外壳不会遮蔽内层高坚果的跳跃阻拦能力；铲子按中心/外圈任选内层或南瓜；精英小丑与粉色橄榄球范围伤害由命中植物九宫格内最近南瓜按保护者归并一次并输入 5 倍，爆破工头独立 4 倍，普通小丑仍直接清场；前后片夹层、叠层血量、水池三层、存档、台风与专项闭环
- [经典磁力菇与僵尸装备剥离契约](project_pvz_magnetshroom.md) — 2026-08-12 `PLANT_MAGNETSHROOM`：100 阳光、7.5 秒卡冷却、15 秒吸取充能；目标侧虚接口原子剥离装备并可返回提取者本体反噬，绝缘胸甲一次吸取扣磁力菇150且第二次可致死不回滚；离体贴图、充能与存档完整，当前可见父回归通过
- [经典气球僵尸](project_pvz_balloon_zombie.md) — 2026-07-30 `ZOMBIE_BALLOON` 为20气球层+270本体、空中/爆裂/步行三阶段、独立螺旋桨附件、70/80啃食帧和152死亡帧；水道击破与致死灰烬都直接移除，非致死灰烬仍按额外层和本体扣血；专属掉头/掉臂、4-3与生存出怪均有可见专项
- [经典扶梯僵尸与共享扶梯](project_pvz_ladder_zombie.md) — 2026-08-15 `ZOMBIE_LADDER` 为500本体+500扶梯护盾，85/194啃食、131死亡；携梯/放置/普通三阶段与 Board 共享扶梯闭环；植物死亡/压扁、磁吸、台风及爆炸生命周期统一由 Board 管理，樱桃/玉米炮按爆心格±1、毁灭菇按±3方形范围清梯；三份可见 `clang-release` 专项锁定范围内非宿主格清除和范围外保留
- [精英扶梯僵尸](project_pvz_elite_ladder_zombie.md) — 2026-08-06 `ZOMBIE_ELITE_LADDER` 为650本体+500银灰扶梯、正式波次每波限1；出场5秒一次性扫描同行当前植物生命与投手/射手数量，严格触发无限金梯/2倍动画/+500本体/2倍扶梯；无限换色保留破损档位并贯通放置、磁吸、掉落粒子和存档；断头/垂死时中断 `PLACING` 并自愈旧档坏状态；投手预判按当前活动片段平均根速度，不再被双速步态的单帧位移波动放大。
- [经典跳跳僵尸](project_pvz_pogo_zombie.md) — 2026-08-15 `ZOMBIE_POGO` 为500本体、普通/高跳/前跳/弃杆步行四阶段；86/107啃食、154死亡；高坚果击落跳杆，持杆免冻结/魅惑和地面秒杀，且因高度躲过大嘴花咬击、拒吞伤害为0，掉杆后恢复普通吞食；持杆动画和整段空中运动不受寒冰拖慢、下落按真实游戏时间2.0倍推进，泳池正式出怪仅限陆路；4-7首次教学、4-8复习；选卡与图鉴详情走无副作用原地弹跳，网格缩略图仍暂停
- [精英跳跳僵尸](project_pvz_elite_pogo_zombie.md) — 2026-08-02 `ZOMBIE_ELITE_POGO` 为850本体、1.15倍率、黑金碳纤维杆与紫青运动装；磁力菇免疫，首次撞高坚果造成600伤害并消耗存档缓冲、第二次折杆；每波限1，4-9以30波六类型池首次登场；独立资源、选卡/图鉴预览与可见专项闭环
- [矿工僵尸家族](project_pvz_digger_zombie.md) — 2026-08-03 普通矿工270+100与爆破工头600+250；共用地下/出土/丢镐状态机且只在陆地行正式刷新，精英在3.5秒预警后爆破房屋侧3列×相邻3行，无壳每层150，有南瓜格只让外壳承受750且内层安全，再以0.15px/tick持镐折返；普通4-5、精英4-6接入且精英每波限1；独立资源、存档与可见回归通过
- [经典火炬树桩与动画火豌豆](project_pvz_torchwood_firepea.md) — 2026-07-31 `PLANT_TORCHWOOD` 把同排 Pea 点燃为 40 伤 Fireball、Snowpea 融化为 Pea；FirePea 为完整时间轴 Animator 子弹，运行时换型与对象池槽位类型分离；火焰直击解冻、沿当前速度方向 100px 穿盾溅射，反向命中特效的 38px X 偏移同步镜像；门板/梯子/冰车抗火及正反向可见专项均已覆盖
- [子弹按来向命中二类护盾](project_pvz_projectile_shield_direction.md) — 2026-08-01 所有 `Bullet` 直接伤害统一走 `Zombie::TakeProjectileDamage`，以命中时 `velocityX` 与 `IsMovingRight()` 判正背面；同向追上从背后绕过铁门/报纸/梯子只伤后层，静止弹保持正面，不按子弹类型/运动模式建白名单；双向射手真实后豆及正反向 Pea/Snowpea/Fireball/Star/Puff 可见专项通过
- [经典高坚果与坚果啃食碎屑](project_pvz_tallnut.md) — 2026-08-05 `PLANT_TALLNUT`：9000 生命、125 阳光、30 秒冷却，两档裂纹与快照恢复不重放碎屑；声明式阻拦撑杆/海豚/跳跳，南瓜外壳不遮蔽能力，撑杆长跳按完整扫掠路径识别后方高坚果；双向锚定强/超强台风植物格，只对直接撞击逐格承受800环境伤害且同阵风反馈合并，链条不传压、水路组合不拆层、死亡后剩余步数可放行；海豚受阻后的手动啃食会随阵风吹离、目标死亡或自身死亡正确清理
- [僵尸自身整体动画能力倍率](project_pvz_zombie_ability_anim_speed.md) — 2026-07-27 删除 `Zombie::mExtraSpeed`，自身整体动画倍率统一经 `GetAbilityAnimSpeedMultiplier()`；固定、阶段和实例随机值分别由类型、状态与派生存档提供，旧根字段只为快速铁桶保留只读迁移；天气、寒冰和黄色冰道回归通过
- [海豚僵尸](project_pvz_dolphin_rider_zombie.md) — 2026-07-28 普通海豚 500 HP、一次越障；右岸外 0～20px 开始抛豚，C# 0.56～0.65/0.75～结束分段低位裁剪防海豚沉底且不切骑手；跳跃换轨按 reanim 锚点差提交104px（弃豚）或106px（保豚）消除落地倒退；左岸上岸零 blend 防旧骑乘姿态垂挂；3-7 限水路接入，声音、存档、断肢及高坚果30%阻拦回归通过
- [精英海豚骑士僵尸](project_pvz_elite_dolphin_rider_zombie.md) — 2026-07-28 深蓝骑手+粉白海豚，700 HP、第一次成功越障保豚、第二次弃豚；高坚果在30%节点阻挡后承受500僵尸来源伤害；3-8 五类型20波教学池，正式波次每波最多1只且计数入档；独立换色资源与头粒子由25文件哈希脚本锁定
- [僵尸图鉴随冒险进度解锁](project_pvz_zombie_almanac_progression.md) — 2026-08-30 图鉴按首次遭遇顺序合并已通关关卡的随机池、必然派生单位与固定 BOSS 槽位；5-9 通关后解锁屋脊督军而不污染随机池，潜雪僵尸与督军资料已补齐；当前可解锁僵尸及全部已注册植物的标题/说明键静态审计均存在、非空且唯一
- [经典地刺](project_pvz_caltrop.md) — 2026-08-03 `PLANT_SPIKEWEED` 由 `Caltrop` 实装：免普通啃食，第25帧结算30px窄攻击带20伤害；水路与屋顶均禁种；冰车虚事件触发扁胎、TirePop、碎屑/烟雾、wheelie与2.8秒延时爆炸
- [冰车僵尸与冰道](project_pvz_zamboni_zombie.md) — 2026-09-05 修复普通/黄色场外冰道误封第9列，起始列保留 mColumns 未覆盖哨兵，固定边界及快照专项覆盖；2026-08-09 普通冰车 1350 HP、右侧高速入场后减速、碾压植物、两段破损与二段烟雾；昼/夜屋顶五行已正式解禁，平台减速段在坡顶结束且冰道仍只画X=642以右水平平台，鎏金型保持未解禁；零合法行不再回退第一路；免疫寒冰、稳定视觉原点、低血量抖动、专属死亡/灰烬与地刺爆胎契约保持
- [鎏金冰车僵尸与黄色冰道](project_pvz_gilded_zamboni.md) — 2026-08-27 `ZOMBIE_GILDED_ZAMBONI` 明确不进入任何无尽候选卡池，旧档冻结池也按当前资格过滤；冒险出怪、直造、已存在实体读档和显式召唤不受影响，其他玩法契约保持
- [三线射手](project_pvz_threepeater.md) — 2026-07-24 三头视觉帧29/73/111，但按 C# 集中计数器只在帧73同帧创建三弹；逐头补 `inverse(basePose)`；顶/底越界弹折回本行且360/290px/s差速；斜向初速按地图行高缩放（草地300、泳池255px/s），水路僵尸碰撞框脱离+25px美术下沉；本次按主人要求只编译、不跑AutoTest
- [火爆辣椒](project_pvz_jalapeno.md) — 2026-07-26 使用主人裁剪的0..19帧本体，第19帧引爆；12段火焰从`CELL_INITALIZE_POS_X`横铺750px并在第12帧消失；整行非魅惑目标先解冻/解减速再走1800灰烬伤害，水路保持水中死亡且睡莲不受影响；冰车合入后会把同行冰道剩余寿命压到0.2秒；引爆中受巨人锤击立即复用正式IgniteRow；双Clang预设及可见专项AutoTest通过
- [缠绕水草](project_pvz_tanglekelp.md) — 2026-07-24 仅空水格直种且占普通层，25阳光/30秒冷却；普通目标按 C# 99→51→21→0cs 抓取拖沉；持门加固铁门改为原地保持 `anim_grab` 5秒后获释且仅水草死亡，掉门后恢复普通规则；一对一锁定、抗性扩展点与存档迁移已接入，首版专项 AutoTest 74 条全绿，本次扩展按主人要求仅双 preset 编译
- [植物压扁与复合 Animator 世界缩放](project_pvz_plant_squish.md) — 2026-08-15 `Plant::Squish()` 统一冻结位置/动画、释放占格、纵向 0.5 底边锚定、5 秒残影与末 1 秒渐隐；默认绘制已递归实例化根与任意深度附件，`SetRenderScale` 同时覆盖 `InstanceRecord` 与 `-NoInstance` 矩阵兜底；冰车、投篮车直接调用，巨人第93帧改为逐层派发ResolveGargantuarSmash，默认再压扁且允许植物立即结算或忽略，清醒寒冰菇正式冻结而睡眠态仍压扁
- [Windows 中央存档目录与旧档安全迁移](project_pvz_save_location_migration.md) — 2026-07-21 Windows 正式存档改到 `FOLDERID_SavedGames/PlantsVsZombies/saves`；首次访问旧 `./saves` 时复制、逐字节校验、再删源文件，冲突不覆盖、失败逐文件回退；AutoTest/`-AutoTestLoadSave` 继续隔离在构建目录
- [第三大关泳池基础系统](project_pvz_pool_basics.md) — 2026-08-03 当前范围 3-1～3-9：`WATER_POOL` 六行网格、原版 15×5 三层 GPU 动态水面、睡莲双层占格及上层植物本体/影子共享水面浮动视觉锚点、前4波仅陆路、`Zombie` 通用水线裁剪与 `Splash.reanim + PlantingPool` 进出水反馈、海豚派生节点、普通/路障/铁桶水路版本、水中爆炸无烧焦残影、PoolCleaner 与旧档边界；3-5普通冰车、3-6鎏金冰车、3-7普通海豚、3-8精英海豚、3-9为200初始阳光与10种敌人的30波分阶段综合；日间天降普通阳光14秒，泳池另每13秒生成15点小阳光；水路 Transform +30px美术下沉而碰撞仍回归逻辑行
- [通用 shader ClipRect](project_pvz_shader_clip_rect.md) — 2026-08-27当前契约：BatchVertex保留逐顶点framebuffer裁剪，活动Clip的实例精灵/Animator/字形按原调用位置回退batch；Push/Pop仍不flush、不录worker状态命令或改动态scissor，无裁剪InstanceRecord因而收回48B
- [冒险第二大关起雨势天气与独立迷雾](project_pvz_night_rain_weather.md) — 2026-08-29 基础雨势、导演/预报、台风、4-9 暴风雨夜与天气栏目干扰已迁入 `Board/BoardWeather.cpp`，独立雾势/逐格 alpha/路灯花照明形状已迁入 `Board/BoardFogWeather.cpp`，屋顶径流/雷荷已迁入 `Board/BoardRoofWeather.cpp`；Board 唯一权威、字段布局与存档键不变
- [路灯花与迷雾核心](project_pvz_plantern_fog_core.md) — 2026-08-29 独立雾势推进、逐格 alpha、路灯花照明形状和雾片变体已迁入 `Board/BoardFogWeather.cpp`，燃料经济、产光倍率和通用索敌门面仍留 `Board/Board.cpp`；唯一路灯花继续以25/100雾火维持四挡逐格照明/索敌与产光，CardSlotManager 交互、6-9动态雾和存档合同保持
- [雾夜第四大关4-1至4-9出怪节奏](project_pvz_fog_spawnlist_pacing.md) — 2026-08-03 4-8当前为普通/精英海豚、气球、跳跳三高度池；4-9为12类型暴风雨终局综合池并以普通/精英跳跳收尾；权威资源未改，4-7～4-9有序池与预览专项已同步并可见通过
- [经典小丑僵尸](project_pvz_jack_in_the_box_zombie.md) — 2026-08-02 `ZOMBIE_JACK_IN_THE_BOX`：500 HP、0.66～0.68速度、随机开盒与共享循环声；第45帧啃食、第66帧爆炸、第89帧死亡，爆炸只伤敌对阵营僵尸（未魅惑侧直接清除爆区全部植物层），明确不受南瓜范围拦截影响；专属大范围爆炸、原版普通完整掉头、残肢和存档均有可见回归
- [精英小丑僵尸](project_pvz_elite_jack_in_the_box_zombie.md) — 2026-08-14 午夜紫双臂精英小丑：900 HP、0.61速度、每5～7秒只投1盒；100px内50伤，命中南瓜格时只让外壳承受300且内层安全；普通侧64次、16秒轻量蒙特卡洛最多推进16只并保留可关闭贪心回退；飞行状态/阵营入档、每波最多2只及4-4/图鉴均有专项
- [精英舞王僵尸](project_pvz_elite_dancer_zombie.md) — 2026-07-27 当前为黑夜大雨任意台风 50% 变异、每波最多 2 只；超额成功变异候选源头跳过，实体创建成功后写 PlayerInfo 永久遭遇供图鉴解锁；720 HP、基础1.25、每0.2秒补伴舞至36只，强/超强台风再乘1.45/1.75；变异与图鉴专项可见 AutoTest 通过
- [绿色精英撑杆僵尸](project_pvz_elite_polevaulter_zombie.md) — 2026-08-27 普通/精英撑杆的起跳入口完整要求有头且非终止态；2026-09-05矿道中等待实际到行再跳，防止无头流血阶段新起跳后在落地附近消失；已提交跳跃后的致命伤由 `anim_death` 接管，迟到落地回调不覆盖死亡轨；精英仍为450 HP、动画能力层1.1、250px跳距、落地生成普通撑杆，60%节点按完整扫掠路径处理高坚果，正式波次每波最多2只且计数入档
- [黑夜第二大关出怪节奏](project_pvz_night_spawnlist_pacing.md) — 2026-07-22 冒险 2-1～2-9 单主题节奏：2-6 普通橄榄球、2-7 舞王、2-8 普通铁门+加固铁门（玩家已取得毁灭菇）、2-9 八种重点机制综合并必含加固铁门；双 preset 统一，`smoke_night_spawnlists` 逐关断言并截图
- [倭瓜](project_pvz_squash.md) — 2026-08-27 3-1 奖励植物：C# 0.8观察→0.3预备→0.5上升→0.1下砸；RISING 起立即释放 Cell，原格可重种且在途倭瓜+同格植物可快照往返；离地不可啃、1800伤害、落地粒子与巨人锤击语义保持；clang-release及可见专项通过
- [双向射手](project_pvz_splitpea.md) — 2026-08-01 经典125阳光/7.5秒冷却/1.5秒轮询；前头95帧单发、后头57帧反向两连发且两侧独立索敌；双子 Animator 用 inverse(basePose) 对齐并完整入档；后豆溅射/特效随负速度镜像并可背击绕盾；4-4结算解锁，4-5以矿工+加固铁门立即教学背线与绕门
- [精英胆小菇](project_pvz_elite_scaredyshroom.md) — 2026-09-05 每关累计最多4株，模仿者落种预占；修复轮间选卡读档标记导致玩家种植漏计，旧档计数补齐本体/代理下界，死亡铲除不返还；专项复现旧版失败并覆盖修复与快照
- [土豆地雷出土触发与范围爆炸](project_pvz_potato_mine_trigger_blast.md) — 2026-08-27 武装态持续扫描已重叠/外壳重定向目标，四僵尸啃南瓜后不再吃掉地雷；爆炸后植物立即死亡并由 `PotatoMine` 复合粒子保留原版灰烬碎屑，原格可立刻重种；默认 Vulkan 可见专项覆盖资源、范围、外壳、占格与截图
- [加固铁门僵尸](project_pvz_reinforced_door_zombie.md) — 2026-08-01 当前源码为270本体/1030门；持门正面植物普通伤害最多10、灰烬最多320、仙人掌正面尖刺帧伤1且免化灰/直杀，背击子弹绕门并取消持门上限；4-5加入双向射手即时反制教学，4-6继续综合复习；水草束缚、免魅惑与大喷截断契约保持
- [Bullet 地面阴影与跨对象绘制顺序](project_pvz_bullet_shadow.md) — 2026-07-19 对齐 C#：Pea 单格21×9、Snowpea 1.3×、Puff无影；2026-08-22 改为宿主显式 Shadow 附件且对象池复用仍按row/position重算；阴影由 BulletPool 在 GOM 主体前统一提交，宿主固定绘制阶段不能跨越植物/Bullet对象层；主人校对 Y 与同排豌豆射手影子一致；可见默认/NoInstance `smoke_bullet_shadow.json` 验本体在上、影子在下
- [单一 Bullet 与分型对象池](project_pvz_bullet_pool_architecture.md) — 2026-08-22 所有已接入弹型统一创建 `Bullet final`；池/GOM 共同强所有权、Bullet 内稳定运行时槽位取代指针哈希，稠密活跃表 O(1) 回收；GOM 并行阈值扣除休眠池弹丸，512→0→64 同口径压力下休眠/低活跃 dispatch 热点消失，clang-release 与可见 torchwood/projectile/melon 存档回归通过
- [九关制冒险进度+显式植物奖励表](project_pvz_adventure_progression.md) — 2026-09-05 奖杯与选关跳过共用 AdvanceProgress 推进及去重发卡；实际解锁新卡后展示共用资料的奖励图鉴，FZJT 标题，支持继续下一关选卡或返回首页；无奖励、重打或已有卡跳过，进度仍在奖杯点击时保存；奖励映射以当前 AdventureProgression.h 为准
- [5-9 屋脊督军完整首领](project_pvz_roof_marshal_prototype.md) — 2026-08-30 正式绑定第15波；15000生命、250啃食、1/6/4秒固定36种前五大关白名单，高威胁概率在11000～5400血间由30%线性升至100%、狂暴阶段不出普通杂兵；黄油1.25秒后免疫5秒；每两批跑向兵力最多行并强化10秒，目标带原版红旗且有中央警报；屏幕下方560×18黑金首领血条同步显示实际血量与11000/5400阶段线；5-9 通关后以固定遭遇映射解锁图鉴，不进入随机池
- [非整十波旗帜进度条](project_pvz_flag_meter_non_multiple_waves.md) — 2026-07-18 对齐 C# `DrawProgressMeter`：旗数=`总波数/10` 向下取整，第 k 面旗横向位置=`1-k*10/总波数`；旗子按第10/20/30波顺序存储，实时升旗和读档恢复均直接使用同一索引；可见 AutoTest 已覆盖15/25/35波布局与25波第10波升旗
- [原版 MO3 动态分轨音乐 ✅异步预构建已验证](project_pvz_adaptive_mo3_music.md) — 2026-08-23 选卡期间单 worker 预构建最新地形 Playback，`StartGame` 只接管完成结果，极快提交/读档直入先 OGG 后安全切 MO3；DAY→POOL 可见 Release 专项记录准备335ms、接管3us并通过跨场景/存档 harness。仍沿用 libopenmpt interactive 分轨、原版 order/channel、敌对数 burst 与宽松许可 overlay，MO3 资源不入 Git
- [屏幕抖动与手持植物预览相机同步](project_pvz_screen_shake.md) — 2026-08-23 全屏视觉效果必须走相机 `projView`；逻辑鼠标锚定的世界层预览必须在本帧最终相机确定后、世界对象绘制前只做一次 `LogicalToWorld`，禁止 Update 写逻辑坐标与晚层 UI 补写双路径并存；实际绘制探针应从同帧 Animator 提交基点投回逻辑坐标，覆盖开场横移与震屏中暂停并同步截图
- [毁灭菇+弹坑](project_pvz_doomshroom_crater.md) — 2026-08-15 Crater 按当前陆地/水格及昼夜、寿命阶段选择贴图并随水面浮动；毁灭菇引爆清除同格全部其他植物并按爆心格±3的7×7方形范围清梯，僵尸仍按250px圆形受击；清醒引爆中受巨人锤击立即正式爆炸、睡眠态仍压扁；`smoke_doomshroom` 可见通过并锁定范围外水平第4格扶梯保留、弹坑/伤害/睡眠父回归
- [寒冰大喷菇 ✅commit未push](project_pvz_icefumeshroom.md) — 2026-07-15(a70506b+de5e528削弱) FumeShroom模板方法化+蓝overlay/蓝粒子/染色卡图；**数值主人已裁定中档=150阳光/间隔2.5s/10伤/减速2.0s**；旧Trophy枚举值解锁耦合已由AdventureProgression显式表取代；穿透=护盾照掉血且全额透体；msvc-debug的info.txt曾整体陈旧
- [帧事件帧号口径](feedback_frame_event_numbering.md) — 主人定死：AddFrameEvent真实帧号=预览帧号-1；**主人报的帧号默认已-1过，直接用不许再减**；已写进两个skill
- [寒冰菇+黑夜spawnlists ✅完成](project_pvz_iceshroom_freeze.md) — 2026-08-15 清醒寒冰菇受巨人锤击立即复用正式冻结结算并跳过剩余动画，睡眠态仍压扁；原有 StartFrozen+UpdateAnimSpeed 单点收敛extra层、读档验证与黑夜spawnlists契约保持，foot-gun=视觉勿耦合别的效果、豁免连伤害不吃、resources.xml双preset加音效
- [纹理池mipmap ✅已落地](reference_pvz_texture_minification_no_mipmap.md) — 2026-07-12(a5b3184)建图全量mip链+级联blit,缩小任意倍数不糊(此前>2:1欠采样,卡牌0.46实证/定稿0.5)；前提=预乘alpha勿破坏、再动必开validation；小字号文字锐化=2×光栅化+DrawCachedText scale0.5；resources不在git覆盖前必备份
- [舞王+伴舞僵尸+adding-zombie skill ✅](project_pvz_dancer_zombie.md) — 2026-07-10~11(d14a526..536c424) 主体已push；2026-07-18补舞步翻面(C#入场+右举手拍、魅惑取反；主人定逻辑X仍向房子)+伴舞出土静态(`Animator::Pause`而非速度层置0，死亡PlayTrack可唤醒)+flipX/animPlaying AutoTest抓手；2026-07-19补双preset宝开语图鉴+可见`smoke_dancer_almanac`截图验收；原架构=Board节拍齐舞+十字召唤补位+SetClipRect出土裁剪/隐影，2026-07-23 起 Clip 不再逐对象 flush；foot-gun=帧事件末-1帧须逐reanim实测、魅惑脱队按阵营判槽位、**暂停≠逻辑停**
- [蹦极僵尸](project_pvz_bungee_zombie.md) — 2026-08-30 同波智能选点按3固定步错峰，并以384候选样本总评估量限制单次开销；5只同步压力下GOM峰值78.60→9.83ms；四阶段下降/抓取/快速上升、单株实体移除、关闭时10000:1原版随机、玉米炮遮蔽承载层、双向关系与夹层绘制/存档契约保持
- [粉色橄榄球僵尸 ✅](project_pvz_pink_football_zombie.md) — 2026-08-02 黑夜专属轻装变体：220本体/900头盔、速度1.85/1.95、减速动画系数0.7，首口400后续40；掉盔对半径120圆内无壳植物造成50，南瓜格只让外壳承受300，水路内层与睡莲安全；2-9 出怪、圆内/圆外及水路三层均有可见专项
- [胆小菇+adding-plant skill ✅已push](project_pvz_scaredyshroom_and_adding_plant_skill.md) — 2026-08-05 四态害怕状态机；南瓜免疫须在节流前清空旧 `mScaredCached` 并保持检查到期，否则旧 true 会驱动反复缩头；既有 foot-gun 仍包括帧事件帧号必问主人、站位/影子两套 offset 分居 gamedata 与代码、读档首帧必须真算
- [金盏花最小观赏版本](project_pvz_marigold_minimal.md) — 2026-08-09 `Marigold : Plant` 只播 `anim_idle`，不吐钱；费用 `-100`、冷却 25 秒，正式卡槽实测阳光 `0→100`；blink1/2 只用通用 `IMAGE_*` 键，默认与 `-NoInstance` 可见专项及整数 worldBounds 一致
- [主菜单石碑排版、命中仲裁、控制台与2-1跳关](project_pvz_mainmenu_button_arbitration.md) — 2026-09-05 控制台 Tooltip 按实际字体自动换行并测量背景宽高，保留整行命中、随鼠标移动、边缘换侧及隐藏清理；主菜单模态、2-1跳关和石碑中心最近仲裁契约保持
- [血量字形worker侧instance化与满血整行快路](project_pvz_glyph_run_worker_instancing.md) — 2026-07-07字形worker直写InstanceRecord消除串行replay N×ε；2026-08-27满血本体改裁透明边的单共享整行实例，2万档590071→410071 instances、约134→137-138FPS（只曾单窗140.1，未稳定140+）；动态/防具保留字形路径，未裁整行与双纹理分段候选均因GPU退化否决；真实档AutoTest须`-AutoTestLoadSave`且只比较小推车事件前稳定窗口
- [gamedata.json 数值外置 ✅已push](project_pvz_gamedata_json.md) — 2026-08-14 JSON唯一数值来源+缺任一基础字段即拒启动(-6)+AutoTest不弹窗守卫；植物轻量防线推演 `simulation` 增加 `supportOnly`，普通花盆/睡莲由数据声明压缩、特殊支撑保持完整画像；只改 clang-release 权威资源，其他 preset 用 Junction 共享；foot-gun=文件名GameApp.cpp非GameAPP.cpp、后台PowerShell不继承VS环境
- [幽灵僵尸射手空射修复 ✅已push](project_pvz_ghost_zombie_shooter_fix.md) — 2026-07-06(b1cec54) 行索引过滤IsActive/IsDying+Die()防重入(同帧双Die双扣计数)+DestroyGameObject(raw)静默失败留WARN；再见"计数0仍开火"先查GOM WARN
- [固定步长主循环 ✅已push](project_pvz_fixed_timestep.md) — 2026-07-06(729356e) 逻辑60Hz+封顶3步丢债；AutoTest同Seed全产物逐字节复现；foot-gun=追帧第2/3步前必须补poll否则合成输入按下沿湮灭、wait_frames语义变逻辑步、验证防空对空假阳性

- [Trophy下沉到GameObject ✅已push](project_pvz_trophy_decouple_coin.md) — 2026-07-06 两步Coin→AnimatedObject→GameObject+Board持weak_ptr供存档+click新增target:trophy；foot-gun=静态click坐标会脱靶(-Seed不固定帧时序)、SetScale(float&)吃不了constexpr、PS5.1提交信息英文双引号拆参数

- [AutoTest assert_state 命令 ✅已push](project_pvz_autotest_assert_state_todo.md) — 2026-07-04 dump字段断言(path点分+数字段=数组下标,equals严格==,不匹配exit1)；BuildStateJson抽取两op共用；smoke_develop/smoke_perks已补断言,不带-develop假绿已根治；foot-gun=浮点字段勿equals用整数投影字段
- [AutoTest 同步截图、状态复位与隔离快照](project_pvz_autotest_harness_enhancements.md) — 2026-07-27 截图 ticket 仅在 PNG 成功落盘后完成；显式 `reset_test_state` / `goto_level.resetTestState`；脚本输出目录内复用正式 GameInfoSaver 做新 GameScene 往返，禁止关闭 AutoTest 模式绕过保护；动画子弹存档保留 poolType

- [GameMessageBox Builder 与 UIManager 模态生命周期](project_pvz_messagebox_builder.md) — 2026-09-05 模态绘制移至 Scene 全部命令之后，避免主菜单入口/选关文字遮挡；最后创建的活动框独占 Button/Slider 与世界鼠标输入、底层正常绘制；父子框取消保留菜单，移除 MainMenuScene 输入特判；图鉴通过内存关卡快照返回原局暂停菜单；FZJT 与 480×282 短框保留

- [粒子按RenderOrder分层](project_pvz_particle_render_layer.md) — 世界层粒子走 GameObjectManager pre-overlay hook（非场景槽，因 MessageBox 在 GameObjects 命令内部）；2026-07-21 雨天改为“世界粒子 → 暗幕 → UI”；EmitEffect 默认 LAYER_EFFECTS_WORLD=35000，显式顶层粒子仍走 DrawFrom

- [开发者模式(-develop) ✅](project_pvz_developer_mode.md) — 2026-07-30 PlayerInfo 按普通关卡号+僵尸枚举名持久化面板选择，两个生存按钮单击直达1000/1001且不覆盖保存关卡；原有D键面板/收费点双条件守卫/点草坪最近行召唤/下一波连点保留；foot-gun=僵尸选择禁存表下标或枚举整数、FZCQ方向按钮用ASCII、AutoTest截图无扩展名且不得与状态同名

- [资源加载并行化 ✅达标已push](project_pvz_parallel_resource_loading.md) — 2026-07-03 方案A达标结算；冷启动10s=2600次open固定成本(Defender+NTFS冷缓存)非带宽(资源36MB)；计时WARN行留回归探针；foot-gun=PS5.1 `1>`重定向UTF-16 grep搜不到中文/subagent断流先查git再SendMessage原agent
- [魅惑僵尸+魅惑菇全套 ✅已push](project_pvz_charmed_zombie_feature.md) — 2026-07-02 StartMindControlled/CanBeCharmed(撑杆仅WALKING)/双向互啃/SetFlipX翻身(支点48,须在overlay/glow复制前)/魅惑菇EatTarget判HYPNOSHROOM&&!睡眠→不结算这口(75阳光25sCD)；foot-gun=①行为守卫放虚函数不放lambda(onTriggerStay绕过) ②Edit撞形近行+单僵尸假阴性 ③脚本须{"commands":[]}；含 d83bab0 纸僵卡anim_eat修复起源(抽virtual ResumeWalkAfterEat)→已被 [project_pvz_zombie_eat_walk_state_machine](project_pvz_zombie_eat_walk_state_machine.md) 收口
- [僵尸啃食→走路状态机重构 ✅已push](project_pvz_zombie_eat_walk_state_machine.md) — 2026-07-28 基座为 PlayWalkAnimation + OnStart/OnStopEating + 非虚 ResumeWalkAfterEat；新增运行期目标不变量：手动 StartEat 即使未建立碰撞对，也按生命、顶层身份与 6px 咬合间隙逐帧校验，阵风吹离、目标死亡或啃食者死亡均原子清 ID 和 eaterCount；详见专项 smoke_typhoon_eating_target
- [铁门僵尸手臂显隐状态机+3门臂bug](project_pvz_doorzombie_arm_bugs.md) — "铁门bug贼多"专档：两套手臂(常规ShowArm/持门screendoor)+门本体入口清单;已修3个都是入口漏同步(c0cb799魅惑多臂/52bd86d魅惑啃僵尸无臂/8f9ada6大喷菇穿透残留持门臂);dump抓armVisible+doorArmVisible;详见 [project_pvz_charmed_zombie_feature](project_pvz_charmed_zombie_feature.md)/[project_pvz_fumeshroom_attack](project_pvz_fumeshroom_attack.md)/[project_pvz_zombie_eat_walk_state_machine](project_pvz_zombie_eat_walk_state_machine.md)
- [魅惑僵尸啃普通:碰撞侧seeker/target钩子](project_pvz_charmed_zombie_collision_hook.md) — CHARMED层设计(不改碰撞算法/不引O(k²)):被魅惑改layerMask=CHARMED+collisionMask含ZOMBIE→落mRowOthers二分搜mRowZombies;pair僵尸恒放a;源见 [project_pvz_collision_seeker_target_fix](project_pvz_collision_seeker_target_fix.md) spec §4
- [碰撞O(k²)僵尸×僵尸空转修复 ✅已push](project_pvz_collision_seeker_target_fix.md) — 2026-07-01(7e2ae95..045704b) 20000僵尸Collision5.07→2.17ms(73.8→106.9FPS);根因=每行SAP堆叠退化+僵尸mask不含ZOMBIE 100%空转;修=拆mRowZombies/mRowOthers;**foot-gun=HandleCollisionEnter先a后b,僵尸×植物必僵尸放a否则PotatoMine清理次序错**;探针-Profile四行;更新 [project_pvz_perf_optimization](project_pvz_perf_optimization.md)
- [Animator trig上GPU/LUT建议裁决](project_pvz_trig_lut_gpu_suggestion_verdict.md) — 2026-07-01否决无测量的GPU搬运与粗量化LUT；2026-08-27在2万可见对象压力下改用2048点16KiB线性插值表并通过默认/NoInstance视觉回归，GPU/compute方案仍否；未来仍按同场A/B裁决
- [Gemini性能报告逐条裁决(整份不动)](project_pvz_gemini_perf_report_verdict.md) — 2026-06-30 读3文件未profiler;"GPU带宽瓶颈"被实测present0.14/replay0.03 CPU-bound证伪;pipeline切换❌/batch2×3⚠️/纹理压缩⚠️/multimap⚠️/Early-Z❌;教训=瓶颈归因靠profiler非读代码;再提纹理压缩/Early-Z/pipeline合并引此
- [僵尸血量文字thrash→HUD字形图集 ✅完成](project_pvz_zombie_hp_text_thrash.md) — 2026-07-02主人真机验证尖峰消失(已push,feature已删);GlyphAtlas单行白字形+DrawGlyphRun逐字形quad;**关键bug=BeginParallelRecord预留固定4MB被字形×13撑爆→vbo翻倍2GB(b8ab956比例化max(8MB,remain/4))**;+字形基线double-count修(BuildGlyphAtlas按真实墨迹框裁紧致);坑=overflow warning在stdout非run.log
- [host-visible缓冲grow-on-demand ✅已push](project_pvz_host_visible_buffer_grow_on_demand.md) — 2026-06-26(17c3a1d)修启动890MB:三持久映射逐帧缓冲×2帧=448MB常驻(纹理仅54MB);改grow启动56MB,891→476MB;坑=安全增长点唯一/先建后换防OOM;+code-review修4缺陷(按缓冲独立翻倍/EndFrame一步扩容/空闲回收/OOM sticky)
- [生存刷怪轮次表+随机子集池](project_pvz_survival_spawn_round_table.md) — 2026-08-27 轮次、权重、背景、出生行与明确品种排除统一由候选资格查询收口；鎏金冰车是当前唯一额外禁入全部无尽卡池的非零权重类型，普通冰车与其他既有资格不变；最终最多8种和随机±1~2保持
- [小游戏最后的家底](project_pvz_minigame_last_savings.md) — 2026-09-05 独立 2000 关、主菜单小游戏选关页、3000 阳光七卡守十波；正式扣费/存档与冒险进度隔离，后半程铁门与橄榄球混编，长关卡名避让波次条
- [GameSelectScene 冒险分页选关+八地形无尽](project_pvz_gameselect_scene_night_endless.md) — 2026-09-05 冒险当前关所在页新增跳过按钮，标准模态确认后保存进度并发卡；2026-09-04 生存集中定义表新增 1007 极夜无尽，严格在进度越过 8-9 后解锁，复用极夜环境且隔离冒险教学/特殊终波；第八大关不是最终区域，未来区域仍由 `AdventureProgression` 与集中定义继续扩展
- [大喷菇攻击补全+护盾穿透 ✅已push](project_pvz_fumeshroom_attack.md) — 2026-06-24(e443375)FumeAttack第27帧对本行[0,380]锥形20伤害;**Zombie::TakeDamage加penetrateShield还原穿透二类护盾(铁门/报纸不穿头盔)**;仅FastPaper/FastBucket透传;Gloom升级可复用
- [僵尸分层受击闪烁](project_pvz_zombie_damage_flash.md) — 2026-08-03 本体/头盔/飞行额外生命与二类护盾按实际扣血独立闪白；方向背击与弹丸主动绕盾统一由 `TakeProjectileDamage` 路由，目标可否决主动绕盾；Animator 轨道覆盖的实例化与 NoInstance 路径已有可见专项基线
- [小阳光/SunShroom/存档审查](project_pvz_smallsun_sunshroom_review.md) — 2026-06-23(53657f2..133a9f1)无严重bug;真实发现都是cleanup/注释/极小边界(SunShroom.h缺guard等)
- [Animator播放状态机存读档修复 ✅已push](project_pvz_animator_playstate_save_fix.md) — PlayTrackOnce完整保存播放状态、目标轨道、返回速度与返回混合；2026-07-30新增独立`returnTrackBlendTime`，默认0.5兼容旧调用/旧档，零混合包装轨不再靠品种特判；save/load reconciliation同族见 [project_pvz_zombie_eat_walk_state_machine](project_pvz_zombie_eat_walk_state_machine.md)
- [Shooter头部动画存档+AutoTest只读读档](project_pvz_shooter_head_anim_save_autotest_load.md) — 2026-07-18 修子Animator只存轨道/帧导致射击永久PLAY_REPEAT；完整保存头部播放状态+双发两发间瞬态，旧档shooting按一次性恢复；`-AutoTestLoadSave`仅放行关卡读档、保存/删除仍短路，真实level1001 RED(循环3)→GREEN(循环0/子弹0/哈希不变)
- [大佬GS/compute渲染建议裁决](project_pvz_dalao_geometry_compute_suggestion.md) — 2026-06-17 ②目标早达成但用gl_VertexIndex顶点拉取+实例化非GS/①矩阵已并行写SSBO不值搬compute;再提同类渲染优化引此
- [Gemini Vulkan审查](project_pvz_gemini_vulkan_review.md) — 2026-06-16 6条:4误判2休眠;**①DestroyTexture改帧计数延迟删除队列已push(0b14016)**;教训=GPU生命周期改动必开validation
- [Volk动态 Vulkan loader + 1.2兼容矩阵 ✅](project_pvz_volk_dynamic_loader.md) — SDL2 loader→Volk动态分发；运行时优先1.3核心，并对 dynamic rendering / synchronization2 分别选择1.2 KHR或RenderPass/传统同步回退；Win7回退版已越过非法指令但一台实机的系统loader在首次枚举扩展时返回HOST_MEMORY，尚待同机vulkaninfo/loader/驱动诊断
- [Windows 7 x64 系统 API 兼容层 + 导入门禁 ✅](project_pvz_win7_yy_thunks.md) — 仓库 overlay port 固定 YY-Thunks 1.2.2；LLD 用替代 import libs、MSVC 用官方 Win7 obj；PE subsystem 6.01，所有 EXE 链接后逐项核对 Win7 x64 导出表；另有同优化/LTO的`clang-release-noavx2`排除Win7 `0xC000001D`
- [编译警告清零 ✅](project_pvz_warnings_cleanup.md) — 2026-06-13 clang-release 0warn；2026-08-12 将35处 `GameAPP.h` 引用统一为索引/磁盘的 `GameApp.h`，clang-release 复核无编译器警告；验证须用clang(msvc默认不报)
- [CMake构建配置 ✅统一](project_pvz_cmake_migration.md) — 2026-08-28 四个 preset 统一为 `clang-cl + lld-link`；常规编译、F5、AutoTest 与交付仍默认 `clang-release`，独立 `clang-asan` 仅供 Windows 8.1+ 内存诊断并自动部署 runtime、让出首机会异常给 ASan；非默认 preset 以 NTFS Junction 共享单份 resources/font
- [Build permission](feedback_build_permission_msbuild.md) — 主人解除构建限制:可直接命令行编译,不必F7不必核对时间戳(现用cmake preset)
- [PvZ轻量备份节点](project_pvz_backup_node.md) — 2026-08-13 Git SSH副本与GitHub独立镜像引用；AutoTest证据带提交/状态/逐文件SHA-256离机归档90天；每日健康报告、每周Git fsck；不替代Windows clang-release与可见AutoTest
- [perf optimization](project_pvz_perf_optimization.md) — 最新2026-08-27:2万真实存档须`-AutoTestLoadSave`并只取小推车事件前稳定窗；保留自适应48有序slot、2048点插值trig表、常规Animator连续直写、48B per-instance vertex input和4顶点strip、无裁剪实例免clip分支、IMMEDIATE优先及GPU查询；满血整行后完整Profile稳定约137-138FPS，单窗140.1不算稳定达标
- [Collaboration style](feedback_collaboration_style.md) — measure-first, steady-state numbers, honest framing, user builds in VS, responds in Chinese
- [Phase6 OpenGL cleanup ✅](project_pvz_phase6_opengl_cleanup.md) — 7Task全过;执行期修预存LNK2019(geom-batch死子系统);commits user-driven
- [并行Update phase-1 已REVERT](project_pvz_parallel_update_phase1.md) — Animator帧推进仅占Update12%(plan误判80%),dispatch0.05ms非瓶颈
- [并行Update phase-2 ✅](project_pvz_parallel_update_phase2.md) — 292f68e 整Animator::Update并行+deferred events;-3.44ms/69.3→91FPS
- [phase-3 component-update skipping ✅](project_pvz_phase3_component_update_skipping.md) — c435a57 NeedsUpdate virtual+mUpdatableComponents视图;FPS91→100;PROFILE_SCOPE自污染~4.6ms
- [继承式玩法对象与组件容器收缩 ✅](project_pvz_inheritance_gameplay_architecture.md) — 2026-09-05 补充对象生命周期使用说明与 Start 前取消的静态限制（未运行验证）；Card 专属状态/显示、CardSlotManager、显式 Transform、纯 UI 与 Collider/Shadow/Clickable 显式附件均已完成；手持预览在最终相机后、世界绘制前单次同步；通用 Component 基类、类型表、模板接口和生命周期视图已删除；稳定 ID 注册与查询类已由 EntityManager 语义重命名为 EntityRegistry；Shadow 绘制、Clickable O(可点击对象) 输入仲裁和僵尸行桶 Die/CommitRow 即时失效契约保持
- [高频实体、动画事件与运行时字符串冷热布局](project_pvz_entity_memory_layout.md) — 2026-08-22 不引入 ZombiePool；Collider 回调与 Zombie 稀有状态按需侧车，Animator 帧事件连续化并使用 24B 内联回调，GameObject/轨名共享驻留，Bullet 复用互斥弹道且尖刺固定槽位按需分配；当前 ABI 普通26轨僵尸静态下限约5.24→1.63KiB（-68.9%），只代表布局、不冒充 FPS
- [预计算动画(放弃)](project_pvz_precomputed_animation.md) — 2026-05-23 TrackInfo::mFrames已密集per-frame,关键帧搜索不存在,ROI不足
- [GPU instancing reanim ✅](project_pvz_gpu_instancing_reanim.md) — 2026-05-24(388a845)reanim实例化；glow/双队列Z-order、ShadowComponent、冰晶与缺轨黄油均保序；2026-08-27记录收回48B并改per-instance vertex input+4顶点strip，活动Clip回退batch，独立instance descriptor已删除
- [Clickable 稀疏注册与显式所有权 ✅](project_pvz_clickable_optimization.md) — 2026-05-24 自注册表替换全场扫描，历史 1.22→0.01ms(-122×)；2026-08-22 脱离 Component 容器并保留 O(可点击对象)、渲染顺序/事件消费及 Collider 原子绑定契约；`GetAllGameObjects()` per-frame scan 仍是仓库 foot-gun
- [Dual-queue保序foot-gun](feedback_dual_queue_order_preservation.md) — dual-queue加新队列时serial fallback跨队列保序必审;worker replay有emitUpTo兜底serial没有
- [预乘alpha管线](project_pvz_premultiplied_alpha.md) — 2026-05-30修白边:契约跨三层(UploadPixels rgb*=a/混合srcColor=ONE/frag预乘vColor.a);加纹理/混合模式必守;glslc重编spv拷Debug+Release
- [颜色0..255约定](reference_pvz_color_0_255_convention.md) — 绘制glm::vec4 color是0..255非0..1(ToSDLColor直接cast);写0..1→alpha≈全透明隐身
- [Draw视图+HP文字剔除](project_pvz_draw_view_hp_text_cull.md) — 2026-05-31 GameObject::Draw加mDrawableComponents视图+HP文字IsWorldPointVisible视口剔除修128MB VBO溢出;文本爆降真因=整串→纹理256-LRU thrashing
- [glyph atlas后续](project_pvz_glyph_atlas_text_followup.md) — 旧候选,已由 [project_pvz_zombie_hp_text_thrash](project_pvz_zombie_hp_text_thrash.md) 实现;约束清单(双队列z-order/超采样/worker建纹理/tint/度量)
- [batch/instance z-order](reference_pvz_batch_instance_zorder.md) — sprite=instance/文字=batch两队列,cross-flush压batch到instance下;DrawText=当前层(血量)/DrawTextOnTop=顶层(HUD);内联渲染vboCursor推预留区提前到slot循环前
- [改进backlog](project_pvz_improvement_backlog.md) — 2026-05-31剩#2数值JSON化#3 RAII#5测试#6魔法数字#8 inline误用;#1工厂✅#4日志✅#7 Vector加explicit✅(Vector=位置词汇类型602处不全改vec2)
- [统一日志系统 ✅#4](project_pvz_logging_system.md) — 2026-06-06 Logger.h/.cpp流式带级别宏;Debug五级/Release裁到WARN+ERROR;迁~193 cout+29 fprintf;扩展只改Logger.cpp::Write
- [注册式工厂 ✅#1](project_pvz_factory_registry.md) — 2026-05-31消除两Instantiate switch→GameDataManager数据驱动(函数指针factory字段);函数指针非std::function/集中注册;指引在InitializeHardcodedData顶部
- [僵尸按行与稀有品种索引 ✅](project_pvz_zombie_row_index.md) — 2026-08-12 EntityRegistry加ForEachZombieInRow替GetAllZombieIDs全表扫；通用桶惰性每帧重建承接任意换行，同帧新增会置脏；黄色冰道与屋脊督军血条使用品种专用弱索引，逐帧稀有类型查询不得扫描全体；foot-gun=取全集或全表按类型过滤
- [vcpkg缓存删除代价](feedback_vcpkg_cache_deletion.md) — vcpkg-master整目录不能删(toolchainFile指向);"可再生"≠"删了免费"(重装全量联网);清缓存前确认不reconfigure
- [生存词条系统](project_pvz_perk_system.md) — 2026-08-28 共18词条(10植8僵)；腐蚀毒素当前为每层每秒最大生命1%，批量伤害返回后重查宿主/侧车以兼容投篮车同步死亡；轮间词条模态输入、地图条件、固定1.5%稀有预算与存档链保持
- [资产/worktree/AutoTest坑](reference_pvz_assets_worktree_autotest_gotchas.md) — ①clang-release持有单份resources/font，debug/noavx2用Junction；新worktree只需补一次权威原版资产 ②AutoTest wait字段名是"value"非frames ③状态切换后settle>30帧 ④蘑菇夜测goto 10-18 ⑤产阳光验证看dump sun字段
- [Animator三层速度模型 ✅push](project_pvz_animator_clip_speed.md) — 2026-06-07(e74bc76)EffectiveSpeed=(clip!=0?clip:base)*extra;clip绝对覆盖(非乘数)/0回落base;删mOriginalSpeed两步舞;存档animClipSpeed
- [跨平台phase-1审查 ✅push](project_pvz_xplat_phase1_review.md) — 2026-06-25 FF合master(b3ff1da);load_file×2收编走FileManager;**目录枚举×2留phase-3(SDL/AAssetManager无列举API)**
- [跨平台phase-3资源清单 ✅已push](project_pvz_xplat_phase3_manifest.md) — 2026-06-25(c50db1e)构建期gen_manifest.cmake glob生成manifest.txt+FileManager::ListResourceFiles经SDL_RWops读;**裁决:迁SDL3拿APK列目录是陷阱→清单方案库无关,SDL2栈即可用**
- [blend手翻转+离散帧属性修复 ✅](project_pvz_blend_transform_hand_flip_fix.md) — 2026-06-28(1bf662a)修`>180°`吸附目标tSrc→tDst；2026-07-19收口`GetDeltaTransform` blend取`tDst.f/image`、普通取`tSrc.f/image`，复用已有策略分支+删Animator补写，不反转普通插值参数追求“统一after”；clang-release+契约程序+可见repro_blend_hand通过
