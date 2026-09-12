# 词条验证夹具

按当前任务读取命中段落，各专项只在对应机制受影响时适用；流程与审批遵循 [技能入口](../SKILL.md) 和 [AGENTS.md](../../../../AGENTS.md)。

## AutoTest 验证

基础命令：

- `add_perk {"type":"...","count":N}`：UI 前唯一词条注入入口。
- `damage_plant` / `damage_zombie`：走正式受伤链；`source` 可取 `PLANT/ZOMBIE/OTHER`（默认 `OTHER`），用于精确验证阵营增伤隔离。
- `survival_perk_open`：打开轮间选择。
- `survival_perk_pick {"index":0}`：选择候选；`index:-1` 只放弃当前一次机会。
- `survival_perk_refresh`：消耗本轮共享的一次刷新额度，重抽当前全部候选。
- `set_next_survival_perk_rare {"enabled":true|false}`：只覆盖下一次面板稀有判定，随后自动恢复正式随机。
- `apply_toxin_stacks {"row":N,"count":N}`：不经过子弹直伤，直接建立毒层，适合比较基础/百分比毒伤。
- `reserve_plantern_fuel {"value":N}`：直接走 `Plantern::ReserveFuel`，用于锁定实际并发接收上限；不要只断言重复计算出来的投影。若不创建飞行雾火，断言后用 `deliver_plantern_fuel` 清掉测试预留。
- `dump_state.perks`：词条层数与聚合数值。
- `dump_state.perkSelect`：除既有流程字段外，候选暴露 `plantRare/plantCondition/zombieCondition`，聚合暴露稀有植物候选数与条件候选数。steps 是已结算机会数，picks 是实际获得数；刷新不改变二者。

改变选择 UI 或机会/刷新流程时，按受影响分支选用：

1. `smoke_perk_select.json`：两次都选；第 1 次刷新 1 次后第 2 次只剩 2 次，并在第 2 次耗尽，总计恰好 3 次；最终 `completedSteps=2`、`completedPicks=2`。
2. `smoke_perk_select_skip_all.json`：第 1 次放弃后仍 active 且进入第 2 次；再次放弃后 steps=2、picks=0。
3. `smoke_perk_select_skip_then_pick.json`：第 1 次放弃、第 2 次选择，最终 steps=2、picks=1。
4. `smoke_perk_view.json`：查看面板仍能显示和分页。
5. `smoke_perk_select_cob_input_modal.json`：正式轮清会清除已有炮击准星；词条页真实点击炮体与草坪均不得开始瞄准或发射。

纯数值改动跑对应专项；改变共享伤害来源或聚合顺序时，再选用 `smoke_perks_damage_sources.json`、`smoke_perks_balance.json`、`smoke_perks.json` 中能覆盖影响的回归。UI 改动要检查截图，不能只看退出码；同时检查对应 `run.log` 含 `script finished OK`。

地图条件、基础机制强化或稀有质变还必须做成对对照：同一地图、同一实体和同一时长先验证 0 层保持旧值，再开启词条只断言预期差异。当前扩展专项为 `smoke_survival_perk_expansion.json` 与 `smoke_survival_perk_fog_and_toxin.json`；后者同时锁定路灯花无词条/两层燃料价值与接收上限、毒液无词条/腐蚀词条伤害、迷雾出界清控及存档。`smoke_corrosive_toxin_lethal_catapult.json` 另锁定腐蚀毒素致死会同步释放毒素侧车的投篮车生命周期边界。
