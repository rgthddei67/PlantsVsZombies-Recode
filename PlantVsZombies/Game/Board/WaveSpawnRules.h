#pragma once

// 正式波次累计上限；同时名额、教程和跨波冷却由 Board 各自判定。
namespace WaveSpawnRules {
	inline constexpr int kEliteDancerMaxPerWave = 2;             // 每波最多允许生成的精英舞王数量；超额候选直接跳过
	inline constexpr int kReinforcedDoorMaxPerWave = 2;          // 每波最多正式生成的加固铁门数量；超额候选直接跳过
	inline constexpr int kElitePolevaulterMaxPerWave = 2;        // 每波最多正式生成的精英撑杆数量；超额候选直接跳过
	inline constexpr int kGildedZamboniMaxPerWave = 1;           // 每波最多正式生成的鎏金冰车数量；超额候选直接跳过
	inline constexpr int kEliteDolphinRiderMaxPerWave = 1;       // 每波最多正式生成的精英海豚数量；超额候选直接跳过
	inline constexpr int kEliteJackInTheBoxMaxPerWave = 2;       // 每波最多正式生成的精英小丑数量；超额候选直接跳过
	inline constexpr int kEliteDiggerMaxPerWave = 1;             // 每波最多正式生成的爆破工头数量；超额候选直接跳过
	inline constexpr int kElitePogoMaxPerWave = 1;               // 每波最多正式生成的精英跳跳数量；超额候选直接跳过
	inline constexpr int kEliteLadderMaxPerWave = 1;             // 每波最多正式生成的精英扶梯数量；超额候选直接跳过
	inline constexpr int kEliteCatapultMaxPerWave = 1;           // 每波最多正式生成的导流投篮车数量；超额候选直接跳过
	inline constexpr int kRedeyeGargantuarMaxPerAdventureWave = 3; // 冒险每波最多正式生成的红眼巨人数量；无尽不启用此上限
	inline constexpr int kInsulatorMaxPerWave = 2;               // 每个正式波次最多成功生成的绝缘僵尸数量
	inline constexpr int kHijackerMaxPerWave = 2;                // 每个正式波次最多成功生成的劫持者数量
	inline constexpr int kGroundingZombieMaxPerWave = 2;         // 每个正式波次最多成功生成的接地僵尸数量
	inline constexpr int kBobsledTeamMaxPerWave = 3;             // 一次候选四人同乘；正式波次最多接纳三队
	inline constexpr int kIceWallEngineerMaxPerWave = 1;         // 每个正式波次最多成功生成一只冰墙工程师
	inline constexpr int kIceCrackDrillMaxPerWave = 3;           // 每个正式波次最多成功生成三只冰裂钻机
	inline constexpr int kWeatherJammerMaxPerWave = 2;           // 每个正式波次最多成功生成两只气象干扰僵尸
	inline constexpr int kIceStatueExecutionerMaxPerWave = 5;   // 每个正式波次最多成功生成五只冰像处刑者
	inline constexpr int kSnowBurrowTutorialMaxPerWave = 1;      // 8-1 每波与同时最多一只潜雪僵尸
	inline constexpr int kSnowBurrowCompositeMaxPerWave = 2;     // 8-2 起每波与同时最多两只潜雪僵尸
	inline constexpr int kSnowBurrowTutorialLevel = 64;           // 内部 64 即 8-1，雪穴形成后保底一次潜雪出生
	inline constexpr int kAdaptiveHelmetMaxPerWave = 4;          // 8-3 起每波最多四只，不设同时或累计上限
	inline constexpr int kThermalSniperTutorialLevel = 68;       // 内部 68 即 8-5，第三波独立保底后开放自然候选
	inline constexpr int kThermalSniperTutorialMaxPerWave = 3;   // 8-5 每波最多三只
	inline constexpr int kThermalSniperCompositeMaxPerWave = 4;  // 8-6 每波最多四只
	inline constexpr int kAuroraPriestMaxPerWave = 3;             // 极光祭司每波累计上限
}
