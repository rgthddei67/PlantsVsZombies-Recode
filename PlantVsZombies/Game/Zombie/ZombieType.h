#pragma once
#ifndef _ZOMBIE_TYPE_H_
#define _ZOMBIE_TYPE_H_

constexpr int NULL_ZOMBIE_ID = -1024;

enum class HelmType {
	HELMTYPE_NONE,
	HELMTYPE_TRAFFIC_CONE,	//路障
	HELMTYPE_BUCKET,			//铁桶
	HELMTYPE_FOOTBALL,		//橄榄球
	HELMTYPE_DIGGER,		//镐子
	HELMTYPE_BOBSLED,		//雪橇
	HELMTYPE_WALLNUT,		//坚果
	HELMTYPE_TALLNUT,		//高坚果
	HELMTYPE_INSULATOR,		//绝缘僵尸的陶瓷胸甲；一个完整的一类防具生命池
	HELMTYPE_ADAPTIVE,		//适应头盔僵尸的一次性来源记录头盔
	HELMTYPE_AURORA_DEVICE,	//极光祭司的非磁性仪器；破坏后永久取消未提交仪式
	HELMTYPE_CLOCK_DISK,		//极夜钟匠的非磁性星盘；破坏后永久取消未提交时间锚
	HELMTYPE_CRYSTAL_HORN // 非磁性晶角头盔
};

enum class ShieldType {
	SHIELDTYPE_NONE,
	SHIELDTYPE_DOOR,		//铁门
	SHIELDTYPE_NEWSPAPER,	//报纸
	SHIELDTYPE_LADDER		//梯子
};

enum class ArmorBrokenState {
	NONE,
	NO_BROKEN,
	A_LITTLE_BROKEN,
	REALLY_BROKEN,
};

enum class ZombieType {
	ZOMBIE_NORMAL,
	ZOMBIE_TRAFFIC_CONE,
	ZOMBIE_POLEVAULTER,
	ZOMBIE_BUCKET,
	ZOMBIE_FASTBUCKET,
	ZOMBIE_NEWSPAPER,
	ZOMBIE_FASTPAPER,	// 加强版读报僵尸
	ZOMBIE_DOOR,
	ZOMBIE_FOOTBALL,
	ZOMBIE_DANCER,
	ZOMBIE_BACKUP_DANCER,
	ZOMBIE_PINK_FOOTBALL,
	ZOMBIE_ELITE_DANCER,
	ZOMBIE_REINFORCED_DOOR,
	ZOMBIE_POOL_NORMAL,
	ZOMBIE_POOL_CONE,
	ZOMBIE_POOL_BUCKET,
	ZOMBIE_ELITE_POLEVAULTER,
	ZOMBIE_ZAMBONI,		// 冰车
	ZOMBIE_GILDED_ZAMBONI,	// 鎏金冰车：黄色三路冰道与无伤害阶梯加速
	ZOMBIE_DOLPHIN_RIDER,	// 海豚僵尸：泳池水路专属，骑豚跃过第一株植物
	ZOMBIE_ELITE_DOLPHIN_RIDER,	// 精英海豚：第一次越障后保留海豚，第二次才弃豚
	ZOMBIE_JACK_IN_THE_BOX,	// 小丑僵尸：随机倒计时开盒并范围爆炸
	ZOMBIE_BALLOON,			// 气球僵尸：空中阶段仅受对空弹丸命中，气球破裂后落地
	ZOMBIE_ELITE_JACK_IN_THE_BOX,	// 精英小丑：不自爆，持续向相邻三行随机投掷盒子
	ZOMBIE_DIGGER,			// 经典矿工：地下穿行、左侧出土并反向折返
	ZOMBIE_ELITE_DIGGER,		// 爆破工头：出土预警后爆破后排三列并持镐折返
	ZOMBIE_POGO,			// 经典跳跳：反复弹跳越过植物，高坚果可击落跳杆
	ZOMBIE_ELITE_POGO,		// 碳纤维精英跳跳：免疫磁力吸取并抵消首次高坚果阻拦
	ZOMBIE_BUNGEE,			// 经典蹦极：定点下落后抓走一株植物并升空离场
	ZOMBIE_LADDER,			// 经典扶梯：为坚果类放梯并供地面僵尸攀爬
	ZOMBIE_ELITE_LADDER,	// 精英扶梯：五秒后按本行血量与远程植物构成获得分支能力
	ZOMBIE_CATAPULT,		// 经典投篮车：十二发篮球、车辆碾压与专属爆炸死亡
	ZOMBIE_ELITE_CATAPULT,	// 导流投篮车：锁定自身所在径流行，并仅强化自身顺坡漂移
	ZOMBIE_GARGANTUAR,		// 经典巨人：砸扁植物并在半血时投出一只小鬼
	ZOMBIE_IMP,			// 经典小鬼：仅由巨人投出，不进入随机出怪权重池
	ZOMBIE_REDEYE_GARGANTUAR,	// 红眼巨人：复用经典巨人全部行为，6000 生命；第七大关压轴投放，冒险每波限三只
	ZOMBIE_ROOF_MARSHAL,	// 5-9 屋脊督军视觉样机：普通僵尸骨架、独立军官材质，不进入随机出怪池
	ZOMBIE_INSULATOR,		// 绝缘僵尸：第六大关陶瓷胸甲与黑夜屋顶放电交互
	ZOMBIE_HIJACKER,		// 劫持者僵尸：75% 雷荷锁定后以当前生命生成全场处决线
	ZOMBIE_HEALER,			// 急救员僵尸：按伤员密度自动切换群体或单体治疗
	ZOMBIE_GROUNDING,		// 接地僵尸：天线路障为满电推演增加植物专属引导候选
	ZOMBIE_BOBSLED_TEAM,	// 雪橇车队：四人共乘冻土雪橇，碰撞后跨行散开
	ZOMBIE_ICE_WALL_ENGINEER, // 冰墙工程师：在霜线施工全场唯一的移动弹道冰墙
	ZOMBIE_ICE_CRACK_DRILL, // 冰裂钻机：冻土蓄力后提交同行持续地裂
	ZOMBIE_WEATHER_JAMMER, // 气象干扰僵尸：铁桶本体停步干扰当前全部公开天气预报
	ZOMBIE_ICE_STATUE_EXECUTIONER, // 冰像处刑者：封存冻土高价值植物并以三锤推进处决
	ZOMBIE_SNOW_BURROW, // 潜雪僵尸：出生潜雪，并在半血前摇后提交第二次短潜雪
	ZOMBIE_ADAPTIVE_HELMET, // 适应头盔僵尸：头盔被击穿时免疫原植物谱系或全部灰烬
	ZOMBIE_THERMAL_SNIPER, // 热感狙击僵尸：玩家部署植物时向原位置发射热脉冲
	ZOMBIE_AURORA_PRIEST, // 极光祭司僵尸：在植物阵线内部提交三至四个独立出生裂隙
	ZOMBIE_POLAR_CLOCKMAKER, // 极夜钟匠僵尸：记录相邻三行并在六秒后回溯目标

	ZOMBIE_EXCAVATOR, // 开凿僵尸：一次永久开墙与主动施工绕行
	// ↓ 哨兵：置于全部已实现僵尸之后，使 [0,NUM_ZOMBIE_TYPES) 只覆盖已实现类型，
	//   生存模式随机抽取据此绝不会抽到下方未实现僵尸。
	//   注：Board::LoadSpawnListFromJson 亦以此为上界校验 JSON 僵尸ID，效果一致。
	ZOMBIE_CRYSTAL_HORN_MINER, // 晶角矿工，9-3 首次登场
	ZOMBIE_SUN_THIEF, // 盗晶僵尸，窃取阳光后沿矿道撤退
	ZOMBIE_CRYSTAL_DRUMMER, // 震晶鼓手，矿道范围鼓舞
	NUM_ZOMBIE_TYPES,

	ZOMBIE_YETI,
	ZOMBIE_BOSS,
	ZOMBIE_PEA_HEAD,
	ZOMBIE_WALLNUT_HEAD,
	ZOMBIE_JALAPENO_HEAD,
	ZOMBIE_GATLING_HEAD,
	ZOMBIE_SQUASH_HEAD,
	ZOMBIE_TALLNUT_HEAD,
};

#endif
