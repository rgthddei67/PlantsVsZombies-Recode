# 矿场第三组：维护入口与易错点

9-5/9-6、棱光花和盗晶已经实现。当前参数与阵容只维护在源码及 `build/clang-release/resources/gamedata.json`、`spawnlists.json`，原设计交接文档已由实现替代。

- 盗晶动作在 `SunThiefZombie`，经济事务在 `BoardSunTheft.cpp`。账本以稳定僵尸 ID 为键，实际死亡返款后仍保留记录。`RetireForTemporalReplacement` 调用虚拟 `Die` 只是替换濒死外壳，此时不能返款；重新创建时应由现存账本决定继续撤退或冷却，不能恢复旧携款。
- 断头禁用与已逃脱状态也属于不可回溯记录。魅惑直接结算余额，不能调用带生产加成的阳光入口。普通读档先恢复 Board 账本，再恢复实体阶段；钟匠核心快照不保存经济记录。
- 棱光技能和选敌位于 `PrismFlower`，提交后的标记由 `Zombie` 独立计时。标记随普通存档恢复，不属于时间锚核心状态：存活回溯保持余时，死亡重建不得携回旧标记。增伤与矿雾缩放共用 `ScaleStatusDamage`，实际扣血与化灰预判必须使用同一入口，目标自己的伤害上限仍在后面执行。
- `MineGrid::Initialize` 选择地形，`BoardMineFog.cpp` 管理矿雾。种植范围预览共用 `PrismFlower::CoversCell`；在途光束只是表现，不重新索敌。既有标记不因后续位置或岩壁变化撤销。
- 精英小丑允许盒子飞越岩壁，但命中集合从爆点重新检查视线。实际伤害、贪心评分、蒙特卡洛候选须共同遵守遮挡，再应用既有南瓜拦截；AI 用候选的 `blockedPlantIds` 冻结遮挡，不访问场景对象。
- 图像生成入口为 `scripts/generate_prism_theft_assets.py`，母图、经典时间轴及产物均锁定哈希。静态装备用命名 follower；新类型即使共用经典时间轴，也需独立注册 reanim 名称。

验证入口：`smoke_mine_third*`、`smoke_prism_edges`、`autotest/verify_mine_third.py`，以及 `MineGridTests`、`PlantDefenseMonteCarloTests`。后续改动按实际影响选择用例，不将历史测试结果当作当前证据。
