#pragma once
#ifndef _H_BULLETTYPE_H
#define _H_BULLETTYPE_H

constexpr int NULL_BULLET_ID = -1024;

enum class BulletType {
	BULLET_PEA,				// 豌豆
	BULLET_SNOWPEA,			// 冰豌豆
	BULLET_CABBAGE,			// 卷心菜
	BULLET_MELON,			// 西瓜
	BULLET_PUFF,			// 孢子
	BULLET_WINTERMELON,		// 冰瓜
	BULLET_FIREBALL,		// 火豌豆
	BULLET_STAR,			// 星星
	BULLET_SPIKE,			// 尖刺
	BULLET_BASKETBALL,		// 篮球
	BULLET_KERNEL,			// 玉米粒
	BULLET_COBBIG,			// 玉米加农炮
	BULLET_BUTTER,			// 黄油
	BULLET_ZOMBIE_PEA,		// 僵尸豌豆
	BULLET_TOXICPEA,		// 毒豆；追加在末尾以保持旧存档子弹整数 ID
	BULLET_TOXICFIREBALL,	// 紫焰毒火豆；继续追加以保持旧存档子弹整数 ID
	BULLET_MELT_SNOW,		// 融雪投手普通雪团；20 点直击伤害
	BULLET_SALT_CRYSTAL,	// 盐晶弹；20 点直击伤害并向冰层请求 200 点腐蚀
	BULLET_AURORA_PEA,		// 极光豌豆；50 点伤害并依次穿透最多四只不同僵尸
	BULLET_THERMAL_PULSE,	// 热感狙击僵尸的敌方水平直射弹；伤害与硬终点由瞄准快照提供
	BULLET_THUNDER_SEED, // 雷鸣花普通伤害雷种；追加保持旧档整数身份
	NUM_BULLETS,
};

#endif
