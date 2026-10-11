# 显式采购诊断

`commander_episode.diagnosticPurchases` 是默认关闭的固定采购夹具，支持两种格式：

```json
{"at":63,"type":"ZOMBIE_ICE_WORKER","row":2,"count":1,"delay":0}
```

```json
{"at":63,"members":[
  {"type":"ZOMBIE_NORMAL","row":0,"count":2},
  {"type":"ZOMBIE_ICE_WORKER","row":0,"delay":7},
  {"type":"ZOMBIE_ICE_WORKER","row":4,"count":2,"delay":2}
]}
```

`at` 是本段 episode 开始后的游戏秒；`row` 从0开始；每个成员的 `count` 默认1，`delay` 默认0，表示同一次正式付款后的出生延迟（0–60游戏秒）。成员按数组顺序展开，同类型、同行的重复成员不会合并、重排或归零延迟。一次混编可以跨行、跨类型、具有不同延迟，但只提交一张购物车、整车支付一次、推进一波采购。`members` 不得与根级 `type`、`row`、`count`、`delay` 同时出现，不继承根级默认值；空或非数组 `members`、任一非法成员都拒绝整批。成员数及展开后总人数均不能超过正式部署上限；实际剩余容量仍须另行通过。单段最多64项请求。

到时只尝试一次，不保证购买成功。当前正式池、每个成员在付款前的解锁采购波、合法行、真实单价、钱包和部署容量都必须通过；保留原首攻准备期。非法成员、未知类型、锁定、缺钱、缺位均整笔拒绝，不保留合法前缀、不缩小人数、不借这一车的前半成员解锁后半成员。后台正在计算或已有待领取结果时记 `planner_busy`，不取消、不提前领取、不重试。暂停或终局不采购；未到时便结束的请求记录 `episode_ended` / `episode_timeout`。

空闲主线程调用只读 `PlanColdStorageAttack(false,&probe)`，按当前 options 映射，再用 `EvaluateCandidate` 保留真实预测分数。这个显式诊断评分不限墙钟，`prediction.unlimitedDiagnostic` 和 `evaluationMs` 明确记录，不能视为常规120ms自主搜索；负分也可按脚本合法购买。如果评分修剪、重排成员或修改延迟，整笔记 `candidate_changed`，不会改买别的购物车。随后仅经一次 `ApplyColdStoragePlan` 整车支付并推进采购波，传空 tickets/QueueRevision 保持旧队列原有票据、路线、剩余时间、成本和采购波。没有手工加波、加钱、开兵、减弱对手或延长开局等待。

任一种请求可显式增加布尔字段 `responseContrast:true`（默认false，其他类型整批拒绝）。在普通完整评分之后、付款之前，再从同一原始快照副本分别完整推演“请求购物车”和“空购物车等待”。唯一快照改动是移除公开来源键1–64中尚未提交、非预存、非定点索敌、没有植物宿主/飞行阶段的直接付费樱桃/辣椒/Doom候选，筛选边界与 `ColdStorageObservedForecast::KnownPaid` 相同；已承诺爆炸/炮弹、免费炮、其他付费手动能力、建设和经济保留。所有响应hold参数都沿用默认false/0，不使用会一并保留领域/维修等能力的 `preservePaidDefenses`。这只是“未来直接付费区域反制未使用”的显式假设，不是发生概率，也不会替换原预测评分、选案或真实对手行为。

`responseContrast` 回执记录 `hypothesis`、`removedCounterCount`、去重排序的 `removedSourceKeys`、总/逐世界 `evaluationMs`，以及 `purchase` 和 `wait` 各自的 `rawFeatures`、`rawProduction`、`productionAfterWindow`、`paidCounterCasts`、`cobShots`、`constructedCobShots`、建设/兑换/订单及玩家 `sunSpent`/`iceSpent`。`cobShots` 是该世界观察到的真实预测离膛次数，`constructedCobShots` 是新建炮提交的炮击数，两者含义不同；原先已飞在途的弹仍结算，不重复计作新离膛。原始产冰前60秒和时域尾段分开记录，不校准、不预支。`workerTrace`/`counterTrace` 仅在根级 `commanderForecastTrace:true` 时实际采集。两个世界沿用原始快照版本/时域，可能与普通评分主动扩展后的时域不同，不能把其中一个原始世界和普通评分的基线混用。非法或忙碌请求不会为这个字段强行获得快照。

真实AI和陪练继续运行；诊断不赠送陪练额外观察/种植/开炮次数。结果 `controller=autonomous_with_scripted_purchase`，诊断购物车对应的决策标为 `scripted_purchase`。即使全部请求遭拒，整段仍保留这个诊断来源标记，不能混入自主AI胜率。多个实验臂应分别用固定请求重开同条件对战；某臂被拒绝就是未完成干预，不能当作买到了其原定队伍。

`diagnosticPurchases` 回执保留原请求、尝试时刻、接受人数、整车请求费用与真实费用、前后钱包/累计支出/采购波/决策序号、完整评分及前后付费票据。`members` 保序记录每项 `requested`、`typeID`、`unitCost`、`unlockWave`、`requestedCost`；无法识别或尚未解析的字段为 `null`。`failedMember` 在可定位的成员错误中给出从0开始的下标，批次错误为 `null`。旧格式继续导出根级 `typeID`、`unitCost`、`unlockWave`；混编不以任一成员冒充整批，根级三个字段保持 `null`。`requestedCount`/`requestedCost` 是可完整解析后的总人数/整车费用，`actualCost`/`acceptedCount` 才是实际交易。拒绝回执也导出前后队列；成功另有 `newQueueMatchesRequested`。`typeID` 与新票据可关联真实 `paid_birth` 战斗轨迹，`wave` 可关联工人实际生产归因；预测产冰不是实际到账。

`smoke_commander_diagnostic_purchase.json` 使用自然11-1、难度3、无支援、5×后台和原炮阵陪练，运行90游戏秒，包含旧格式单工人、第64秒异类型跨行且延迟不同的混编，以及非法尾成员、开局和过额拒绝；两笔目标采购均开启响应假设对照和预测轨迹。请求若恰遇后台繁忙会如实拒绝，不能靠反复尝试刷成成功样本。`smoke_commander_diagnostic_purchase_rejections.json` 用相同自然开局运行8游戏秒，包含两种格式的开局拒绝、空/错误类型成员表、旧字段与新格式混用、合法前缀加非法尾成员、总人数越界、错误类型的响应对照开关和未来未到时请求。两者只验证交易契约，不证明自主经营或胜利。

运行后检查：

- `python autotest/verify_commander_diagnostic_purchase.py <输出目录> --require-accepted --require-mixed` 要求至少一笔真实付款且有混编整车付款，核对每个成员的合法解锁、真实费用、原始行和延迟、整笔+1波、旧队列保持、控制来源及全局账本
- `python autotest/verify_commander_diagnostic_purchase.py <全拒绝输出目录> --require-all-rejected` 要求实际尝试过且全部拒绝，任何前缀付款/排队都失败；输出 `purchaseLifecycleObserved=false`，不声称验证过成功付款
- 不带要求成功/拒绝的开关可检查任一真实结果，忙碌/锁定导致零成交会如实报告；旧平面格式的R3回执仍可检查
- `python autotest/test_commander_diagnostic_purchase.py` 运行纯回执验证反例，覆盖混编异价/次序/延迟、旧格式、锁定尾成员、前缀扣款和多推进采购波等；这不是C++入口或游戏实测
