#pragma once
#ifndef _ANIMATIONTYPES_H
#define _ANIMATIONTYPES_H

// 新增植物/僵尸时，在此加一个动画类型枚举；完整新增步骤见
// Game/Plant/GameDataManager.cpp 的 InitializeHardcodedData 顶部说明。
enum class AnimationType
{
	ANIM_NONE,
	ANIM_SUN,

	ANIM_SUNFLOWER,
	ANIM_PEASHOOTER,
	ANIM_CHERRYBOMB,
	ANIM_WALLNUT,
	ANIM_POTATOMINE,
	ANIM_SNOWPEASHOOTER,
	ANIM_CHOMPER,
	ANIM_REPEAT,
	ANIM_PUFFSHROOM,
	ANIM_SUNSHROOM,
	ANIM_FUMESHROOM,
	// ANIM_GRAVEBUSTER,
	ANIM_HYPER,
	ANIM_SCAREDYSHROOM,
	ANIM_ICE_SHROOM,
	ANIM_ICEFUMESHROOM,
	ANIM_DOOM_SHROOM,
	ANIM_LILYPAD,

	ANIM_NORMAL_ZOMBIE,
	ANIM_CONE_ZOMBIE,
	ANIM_POLEVAULTER_ZOMBIE,
	ANIM_BUCKET_ZOMBIE,   // 铁通僵尸
	ANIM_PAPER_ZOMBIE,    // 报纸僵尸
	ANIM_DOOR_ZOMBIE,      // 铁门僵尸
	ANIM_FOOTBALL_ZOMBIE,  // 橄榄僵尸
	ANIM_PINK_FOOTBALL_ZOMBIE, // 粉色橄榄球僵尸
	ANIM_DANCE_ZOMBIE,     // 舞王僵尸
	ANIM_DANCERWITH_ZOMBIE, // 伴舞僵尸
	ANIM_ELITE_DANCE_ZOMBIE, // 精英舞王僵尸

	ANIM_ZOMBIE_CHARRED,

	ANIM_LAWNMOWER,
	ANIM_POOL_NORMAL_ZOMBIE,
	ANIM_POOL_CONE_ZOMBIE,
	ANIM_POOL_BUCKET_ZOMBIE,
	ANIM_POOL_CLEANER,
	ANIM_ELITE_SCAREDYSHROOM, // 复用 ScaredyShroom reanim；追加在末尾避免旧动画枚举值错位
	ANIM_SQUASH, // 经典倭瓜；追加在末尾避免旧动画枚举值错位
	ANIM_THREEPEATER, // 经典三线射手；追加在末尾避免旧动画枚举值错位
	ANIM_ELITE_POLEVAULTER_ZOMBIE, // 绿色精英撑杆；追加在末尾避免旧动画枚举值错位
	ANIM_TANGLEKELP, // 经典缠绕水草；追加在末尾避免旧动画枚举值错位
	ANIM_JALAPENO, // 经典火爆辣椒；追加在末尾避免旧动画枚举值错位
	ANIM_JALAPENO_FIRE, // 火爆辣椒整行火焰；非实体瞬时动画
	ANIM_ZAMBONI_ZOMBIE, // 经典冰车僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_ZAMBONI_CHARRED, // 冰车专属灰烬残骸；第 53 帧回收
	ANIM_CALTROP, // 经典地刺；追加在末尾避免旧动画枚举值错位
	ANIM_GILDED_ZAMBONI_ZOMBIE, // 鎏金冰车独立黄色材质；复用普通冰车时间线
	ANIM_TORCHWOOD, // 经典火炬树桩；追加在末尾避免旧动画枚举值错位
	ANIM_FIREPEA, // 火炬树桩点燃后的循环动画子弹；非植物实体
	ANIM_DOLPHIN_RIDER_ZOMBIE, // 经典海豚僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_TALLNUT, // 经典高坚果；追加在末尾避免旧动画枚举值错位
	ANIM_ELITE_DOLPHIN_RIDER_ZOMBIE, // 精英海豚独立粉白海豚与蓝色骑手材质
	ANIM_SEASHROOM, // 经典海蘑菇；追加在末尾避免旧动画枚举值错位
	ANIM_PLANTERN, // 经典路灯花；追加在末尾避免旧动画枚举值错位
	ANIM_JACK_IN_THE_BOX_ZOMBIE, // 经典小丑僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_CACTUS, // 经典仙人掌；追加在末尾避免旧动画枚举值错位
	ANIM_BALLOON_ZOMBIE, // 经典气球僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_BLOVER, // 经典三叶草；追加在末尾避免旧动画枚举值错位
	ANIM_ELITE_JACK_IN_THE_BOX_ZOMBIE, // 精英小丑独立午夜紫礼服与紫金盒子材质
	ANIM_SPLITPEA, // 经典双向射手；追加在末尾避免旧动画枚举值错位
	ANIM_DIGGER_ZOMBIE, // 经典矿工僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_DIGGER_RISING_DIRT, // 矿工出土地块一次性动画
	ANIM_DIGGER_CHARRED, // 矿工专属灰烬动画（含镐/无镐两条轨道）
	ANIM_ZOMBIE_SURPRISE, // 地下丢镐后的问号一次性动画
	ANIM_STARFRUIT, // 经典杨桃；追加在末尾避免旧动画枚举值错位
	ANIM_ELITE_DIGGER_ZOMBIE, // 爆破工头独立工程警戒配色；复用普通矿工时间线
	ANIM_POGO_ZOMBIE, // 经典跳跳僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_PUMPKIN, // 经典南瓜头；追加在末尾避免旧动画枚举值错位
	ANIM_MAGNETSHROOM, // 经典磁力菇；追加在末尾避免旧动画枚举值错位
	ANIM_ELITE_POGO_ZOMBIE, // 碳纤维精英跳跳；复用普通跳跳时间线
	ANIM_TOXICPEASHOOTER, // 毒囊射手独立配色；复用豌豆射手时间线
	ANIM_POOL_SPLASH, // 原版通用入水/出水一次性 Splash 动画
	ANIM_ROOF_CLEANER, // 屋顶专属清洁车；追加在末尾避免旧动画枚举值错位
	ANIM_FLOWERPOT, // 经典花盆；追加在末尾避免旧动画枚举值错位
	ANIM_CABBAGEPULT, // 经典卷心菜投手；追加在末尾避免旧动画枚举值错位
	ANIM_BUNGEE_ZOMBIE, // 经典蹦极僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_KERNELPULT, // 经典玉米投手；追加在末尾避免旧动画枚举值错位
	ANIM_COFFEEBEAN, // 经典咖啡豆；追加在末尾避免旧动画枚举值错位
	ANIM_LADDER_ZOMBIE, // 经典扶梯僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_ELITE_LADDER_ZOMBIE, // 精英扶梯独立深蓝工装与黄黑梯子材质
	ANIM_GARLIC, // 经典大蒜；追加在末尾避免旧动画枚举值错位
	ANIM_CATAPULT_ZOMBIE, // 经典投篮车僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_CATAPULT_CHARRED, // 投篮车专属化灰时间线；主人指定第 29 帧回收
	ANIM_UMBRELLALEAF, // 经典叶子保护伞；追加在末尾避免旧动画枚举值错位
	ANIM_ELITE_CATAPULT_ZOMBIE, // 导流投篮车独立深青蓝材质；复用普通投篮车时间线
	ANIM_GARGANTUAR_ZOMBIE, // 经典巨人僵尸；追加在末尾避免旧动画枚举值错位
	ANIM_IMP_ZOMBIE, // 经典小鬼；追加在末尾避免旧动画枚举值错位
	ANIM_GARGANTUAR_CHARRED, // 巨人专属化灰时间线；主人指定第 42 帧回收
	ANIM_IMP_CHARRED, // 小鬼专属化灰时间线；主人指定第 34 帧回收
	ANIM_MARIGOLD, // 经典金盏花；追加在末尾避免旧动画枚举值错位
	ANIM_MELONPULT, // 经典西瓜投手；追加在末尾避免旧动画枚举值错位
	ANIM_ROOF_MARSHAL_ZOMBIE, // 5-9 屋脊督军独立军官材质；复用普通僵尸时间线
	ANIM_GROUNDINGSHROOM, // 接地菇独立低精度整株时间线；追加在末尾避免旧动画枚举值错位
	ANIM_GLOOMSHROOM, // 经典忧郁菇；追加在末尾避免旧动画枚举值错位
	ANIM_TWINSUNFLOWER, // 经典双子向日葵；追加在末尾避免旧动画枚举值错位
	ANIM_HIJACKER_ZOMBIE, // 劫持者独立电工作业服与充能接收器；复用小丑时间线
	ANIM_LIGHTNINGRODPOT, // 避雷花盆独立陶盆、铜带与避雷针时间线
	ANIM_WINTERMELON, // 经典冰瓜；追加在末尾避免旧动画枚举值错位
	ANIM_COBCANNON, // 经典玉米加农炮；双格单实体，追加在末尾保持旧动画枚举值
	ANIM_GROUNDING_ZOMBIE, // 接地僵尸独立电紫天线路障；复用普通路障时间线
	ANIM_GOLD_MAGNET, // 磁暴菇独立金属紫电时间线；追加在末尾避免旧动画枚举值错位
	ANIM_IMITATER, // 经典模仿者等待与变身时间线；追加在末尾避免旧动画枚举值错位
	ANIM_SNOWANCHORNUT, // 雪锚果独立冰蓝坚果时间线；追加在末尾避免旧动画枚举值错位
	ANIM_BOBSLED_TEAM_ZOMBIE, // 原版雪橇车队推车、下车、走路、啃食与死亡时间线
	ANIM_MELTSNOWPULT, // 融雪投手独立冰蓝投手时间线；追加在末尾避免旧动画枚举值错位
	ANIM_ICE_CRACK_DRILL_ZOMBIE, // 冰裂钻机复用路障骨骼并挂载独立钻机 reanim
	ANIM_FROSTMINE, // 伏霜雷独立三态完整立绘时间线；追加在末尾避免旧动画枚举值错位
	ANIM_ALARMBELLFLOWER, // 警铃草复用 Blover 弯折时间轴并替换分件；追加在末尾避免旧动画枚举值错位
	ANIM_FURNACECOREFLOWER, // 炉芯花复用 SunFlower 时间轴并替换炉芯分件；追加在末尾避免旧动画枚举值错位
	ANIM_SNOW_BURROW_ZOMBIE, // 潜雪僵尸独立雪地材质；复用经典矿工时间线
	ANIM_LISTENINGGRASS, // 听雪草独立覆霜材质；复用经典叶子保护伞时间线
	ANIM_GATLINGPEA, // 经典机枪射手独立时间线；追加在末尾避免旧动画枚举值错位
	ANIM_AURORATORCHWOOD, // 极光树桩复用火炬树桩运动并替换晶化分件
	ANIM_NORTHSTARFLOWER, // 北极星花复用向日葵摇摆时间线并替换极夜分件
	ANIM_ICEMIRRORGRASS, // 冰镜草复用路灯花呼吸时间线并替换冰晶分件
	ANIM_BOUNDARYFLOWER, // 界碑花复用金盏花完整摇摆时间线并附加星石界碑
	ANIM_DAWNLOTUS, // 曙光莲复用睡莲完整浮动时间线并附加多层晨曦花冠
	ANIM_CARRYVINE, // 搬搬藤复用缠绕海草藤臂时间线与独立叶篮
	ANIM_EXCAVATOR_ZOMBIE, // 普通僵尸时间线与矿灯/凿岩机附件
	ANIM_ECHOSHROOM,
	ANIM_CRYSTAL_HORN_MINER,
	ANIM_PRISMFLOWER,
	ANIM_SUN_THIEF,
};

#endif
