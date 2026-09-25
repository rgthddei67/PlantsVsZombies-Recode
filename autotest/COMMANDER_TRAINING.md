# 冷藏站指挥官训练

从仓库根目录运行已编译的 `clang-release`。全兵种训练入口：

```powershell
python autotest/train_cold_storage_all.py --output build/clang-release/autotest/training/my_run --population 4 --generations 4 --seconds 180
python autotest/verify_commander_training.py build/clang-release/autotest/training/my_run
```

游戏窗口在桌面可见，训练默认全静音；启动器不传固定 `-Seed 42`，每局在开局前设置脚本记录的独立种子，同一配对案例的不同策略共用种子；普通 AutoTest 默认有音效、无背景音乐，不改玩家的游玩音量偏好。`batchStepsPerFrame` 只让非交互 AutoTest 在每次绘制间执行多个原始固定步，不改变技能或碰撞的单步时间；普通游戏和 `live.py` 不启用它。Codex 启动仍须从提升权限的 shell 执行。

## 学习与决策

程序进化全局局势权重和每个兵种的局势偏好；特征顺序见 `train_cold_storage_all.py` 的 `CONTEXT` 和生成策略中的 `contextFeatures`。友军伤势、密度、墙体、控制、火力、工人及后排保护用于区分能力发挥的场景。游戏内用有限位置推演搜索兵种、路线、出生间隔、队伍长度，也可以等待，没有预置“橄榄先行”或“巨人先行”模板。

候选推演包括前锋承伤、邻路西瓜溅射、减速、啃食、巨人砸击、制冰及连续清场反制。每张反制牌的落点共享该卡冷却，多张牌共同消耗玩家当前阳光、冰块及已在途冰块，付费僵尸的死亡返冰也会影响后续可用反制；倭瓜先索敌再在目标附近预测砸击。它不是完整游戏副本，特殊能力、伤害层和未来补阵仍有近似。**训练成绩来自正式引擎比赛**，特殊能力在那里真实执行；按兵种学习的偏好用于弥补近似预测的不足，不能称作已准确理解所有技能。

全兵种实验从实际注册表取得独立陆地单位，排除水路专用、仅召唤单位和视觉原型。每个陌生兵种先获得正常付费的出场探针，再由真实战果决定偏好。普通游戏不因此解锁兵种或跳过波次门槛。当前策略仅接入 10-1/10-2；其他地图的地形规则不受影响。

训练混合正式卡池与全兵种场景。保留总体冠军和各场景的优秀候选，并使用宽范围参数重启，避免只沿一个快攻解局做微调。最终先在正式卡池选择迁移策略，再冻结参数进入未参与选优的留出对照。

## 陪练与验收

植物陪练通过正式种植、买冰、收阳光、铲除接口操作，使用金盏花和模仿者金盏花。后排输出植物会补南瓜；全兵种陪练还带对空植物。没有对局中免费种植或重置冷却。

`train_cold_storage_all.py --curriculum counter` 继承已有兵种经验，在正式卡池续训“坚果拖延＋樱桃、辣椒、倭瓜连续反制”。`developing` 战术片段从橄榄刚解锁、坚果防线已存在的阶段开始，补足正常开局中不易稳定重复的决策窗口。逐场 `playerPlantings` 记录真正成功种下的植物，确认陪练确实使用了反制，而不是只带着卡。

反制课程先按真实获胜场数选优，再比较落败场数及有界评分，避免大量超时掩盖缺乏进攻；源策略、已发布策略和训练候选都参加迁移对照。`--generations 0` 跳过变异，用现有候选做当前引擎复测。空场有可支付行动时，搜索受 Board 的最长观望时间约束；低于重组储备且计划缺乏增量收益时允许暂缓，避免强制探路耗尽恢复资本。长期没有消灭植物且最近窗口制冰收入不超过出兵支出、低库存、活动与在途敌军清空的收尾判定由正式 Board 执行，训练和真人对局共用；补给不算经营回报，具体门槛以 `BoardColdStorage.cpp` 和 `ColdStorageState.h` 为准。

场景包含正常开局、成型阵地 `fortress` 和低库存、完整防线的 `economy`。后两者是预先设置双方资源与阵型的战术片段，不能冒充完整开局胜率。经营压力用于检验持久战，并不保证只有制冰工这一条解法。另做允许/禁止制冰工的对照，记录实际部署和产冰，不能把低库存直接认定为经营失败，也不能把存冰高当成胜利。

评价使用实际破阵和扣除外来补给的净资产变化，包含已经正常付费的存活/待出生单位；毛产冰不直接算成绩，经营塑形封顶，真实胜负优先。升级夹具先等实体清理再记录初始阵地，避免把升级前后两株重复计入火力。

冻结策略后可另测未见过的精英胆小菇阵：四株集中两路或分散四路，正常累计配额、后排南瓜和双发支援，不免费恢复精英配额。此报告不自动挑选或覆盖策略；若之后拿它训练，应换新布局/种子做下一轮验收。

```powershell
python autotest/evaluate_commander_formations.py --policy build/clang-release/autotest/training/my_run/candidate_policy.json --output build/clang-release/autotest/training/unseen_elite --seed 401000
python autotest/verify_commander_training.py build/clang-release/autotest/training/unseen_elite
```

加 `--opponent ash` 可测试纯灰烬＋坚果拖延：保留经济植物、坚果/南瓜和三种即时清场，不带持续输出植物；正常开局，无免费预建阵地。这套对手应留在训练结束后检验，不参与本轮参数选优。

全兵种训练器的发布门槛是：至少十二个配对留出场景，比旧 AI 多取得至少两次真实胜利，且不丢掉旧 AI 已获胜的场景。获胜来自 Board 的真实输赢状态；限时未结束记为 timeout。评分塑形有界，不能靠无限存冰压过真实胜负。门槛是有限样本工程检查，不代表对所有玩家更强。

`--publish` 只在通过门槛后写入 `build/clang-release/resources/ai/cold_storage_policy.json`。普通游戏读取有效且标记通过的资源，缺失或非法时回退旧 AI；资源每次进程首次使用时加载，替换后需重启游戏。正式发布资源应纳入 Git 和资源清单。

旧的 `train_cold_storage.py` 保留为只训练全局权重的基础对照，使用独立的较严格评分门槛；正式全兵种流程使用上面的 `_all.py`。

## 数据与继续训练

输出目录包含：

- `identity.json`：引擎、训练程序、gamedata 哈希及实验设置。
- `catalog.json`、`probes.json`：可探索兵种、能力探针或继承来源。
- `checkpoint.json`：每代候选、逐场成绩、冠军及来源。
- `report.json`：冻结策略的留出对照、正式卡池结果及经营对照。
- `candidate_policy.json`：候选参数和验证状态。

游戏证据位于 `build/clang-release/autotest/out/<批次名>/`，包含逐场结果、轨迹、`run.log`、`status.json`。运行脚本同目录保存 stdout/stderr；未捕获 C++ 异常另记录错误内容及匹配 EXE/PDB 的栈地址。

改了引擎或陪练后继承已有经验，并使用新目录、新留出种子重新验证：

```powershell
python autotest/train_cold_storage_all.py --from-checkpoint build/clang-release/autotest/training/my_run/checkpoint.json --skip-probes --output build/clang-release/autotest/training/continued --heldout-seed 201000 --generations 4 --publish
```

`--skip-probes` 用于已经完成兵种探针的检查点。旧记录保留，但旧 EXE/陪练的成绩不能冒充当前验证。完全相同设置可复用已完成批次；改引擎、训练程序或实验设置必须换输出目录。训练不写玩家存档、不改游戏单位数值。每批有墙钟超时，只关闭本程序启动的游戏进程；已完成引擎批次若在 Python 后处理时中断，可用 `recover_commander_checkpoint.py` 验证并恢复。

## 决策校准与混合防守续训

`audit_commander_forecast.py <output>` 对正常付费的一批队伍运行 60 秒，禁止后续增援，记录同一时域的预测产冰/击杀收入与实际回报。它隔离了追加购买的干扰，但植物陪练会真实补阵，因此差距也包含推演尚未表达的后续补阵。`searchFeatures`、`searchBaselineFeatures` 和 `searchPreferenceScore` 分别解释计划、无增援基线和兵种经验加分；逐场 `decisions` 记录搜索编号、付款和预测，包含观望。这些诊断不进玩家存档。

`train_commander_robust.py --from-checkpoint <checkpoint> --output <output>` 使用自适应反制、原反制和发育陪练混合课程。自适应陪练优先救险、避免往已提交爆炸的范围重复交灰烬，并向已突破路线的后方补坚果；仍走正式费用、卡槽和冷却。每代共用一组新种子，兵种偏好的后续变异集中在课程的正式卡池；除逐兵种变异外，还按实际冰价在同一局势特征上做相关变异，让课程有机会学出随火力/墙体等变化的投入倾向。没有参与变异的兵种保留起点中的经验。先在迁移集选出方案，再冻结它跑独立留出集。

本入口的主基线是**当前发布参数**：至少十二个配对留出场景，比现版多赢至少两场，且不丢掉现版已赢的案例；同时不能低于旧规则 AI 的胜负排序。程序只写候选和证据，不自动覆盖正式资源。成绩不合格时保留数据，不能把候选选优、模型诊断通过或单个阵容全胜说成整体升级。
