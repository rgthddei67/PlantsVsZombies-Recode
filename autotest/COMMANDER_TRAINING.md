# 冷藏站指挥官训练

从仓库根目录运行已编译的 `clang-release`。全兵种训练入口：

```powershell
python autotest/train_cold_storage_all.py --output build/clang-release/autotest/training/my_run --population 4 --generations 4 --seconds 180
python autotest/verify_commander_training.py build/clang-release/autotest/training/my_run
```

游戏窗口在桌面可见。`batchStepsPerFrame` 只让非交互 AutoTest 在每次绘制间执行多个原始固定步，不改变技能或碰撞的单步时间；普通游戏和 `live.py` 不启用它。Codex 启动仍须从提升权限的 shell 执行。

## 学习与决策

程序进化全局局势权重和每个兵种的局势偏好；特征顺序见 `train_cold_storage_all.py` 的 `CONTEXT` 和生成策略中的 `contextFeatures`。友军伤势、密度、墙体、控制、火力、工人及后排保护用于区分能力发挥的场景。游戏内用有限位置推演搜索兵种、路线、出生间隔、队伍长度，也可以等待，没有预置“橄榄先行”或“巨人先行”模板。

候选推演包括前锋承伤、邻路西瓜溅射、减速、啃食、巨人砸击、制冰和一次爆炸反制。它不是完整游戏副本，不模拟所有特殊能力、未来补阵和多次炸弹。**训练成绩来自正式引擎比赛**，特殊能力在那里真实执行；按兵种学习的偏好用于弥补近似预测的不足，不能称作已准确理解所有技能。

全兵种实验从实际注册表取得独立陆地单位，排除水路专用、仅召唤单位和视觉原型。每个陌生兵种先获得正常付费的出场探针，再由真实战果决定偏好。普通游戏不因此解锁兵种或跳过波次门槛。当前策略仅接入 10-1/10-2；其他地图的地形规则不受影响。

训练混合正式卡池与全兵种场景。保留总体冠军和各场景的优秀候选，并使用宽范围参数重启，避免只沿一个快攻解局做微调。最终先在正式卡池选择迁移策略，再冻结参数进入未参与选优的留出对照。

## 陪练与验收

植物陪练通过正式种植、买冰、收阳光、铲除接口操作，使用金盏花和模仿者金盏花。后排输出植物会补南瓜；全兵种陪练还带对空植物。没有对局中免费种植或重置冷却。

场景包含正常开局、成型阵地 `fortress` 和低库存、完整防线的 `economy`。后两者是预先设置双方资源与阵型的战术片段，不能冒充完整开局胜率。经营压力用于检验持久战，并不保证只有制冰工这一条解法。另做允许/禁止制冰工的对照，记录实际部署和产冰，不能把低库存直接认定为经营失败，也不能把存冰高当成胜利。

评价使用实际破阵和扣除外来补给的净资产变化，包含已经正常付费的存活/待出生单位；毛产冰不直接算成绩，经营塑形封顶，真实胜负优先。升级夹具先等实体清理再记录初始阵地，避免把升级前后两株重复计入火力。

冻结策略后可另测未见过的精英胆小菇阵：四株集中两路或分散四路，正常累计配额、后排南瓜和双发支援，不免费恢复精英配额。此报告不自动挑选或覆盖策略；若之后拿它训练，应换新布局/种子做下一轮验收。

```powershell
python autotest/evaluate_commander_formations.py --policy build/clang-release/autotest/training/my_run/candidate_policy.json --output build/clang-release/autotest/training/unseen_elite --seed 401000
python autotest/verify_commander_training.py build/clang-release/autotest/training/unseen_elite
```

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
