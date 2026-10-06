#pragma once
#include "PressureShooterRules.h"
#include "FloodMortarRules.h"

#include "ZombieType.h"
#include "AdaptiveHelmetRules.h"
#include "AuroraPriestRules.h"
#include "BoilerRules.h"
#include "CatapultRules.h"
#include "ColdChainGuardRules.h"
#include "CrystalDrummerRules.h"
#include "DancerRules.h"
#include "DisasterEngineerRules.h"
#include "ImpThrowRules.h"
#include "PolarClockRules.h"
#include "Game/Board/IceProduction.h"
#include <limits>

/** 实体初始化与新购预测共用的出生生命层；不包含召唤、阶段加血或关卡倍率。 */
namespace ZombieBirthVitalsRules {
inline constexpr double HxyArmorHealthMultiplier = .75; // HXY出生防具生命倍率，本体不变
struct Stats {
    int body = 0; // 出生本体生命，未知品种不能冒充默认肉盾
    int helm = 0; // 出生一类防具生命，不能独立替本体承受穿二类盾伤害
    int shield = 0; // 出生二类防具生命，穿透与绕盾需分别结算
    int bite = 50; // 初始单口基础伤害，能力加成仍由实体阶段处理
    bool known = false; // 是否已由正式 SetupZombie 核实生命层
    constexpr long long Total() const { return static_cast<long long>(body) + helm + shield; }
};

/** 创建已核实的出生生命层；动态召唤的每名成员各自读取本品种。 */
constexpr Stats Known(int body, int helm = 0, int shield = 0, int bite = 50)
{
    return {body, helm, shield, bite, true};
}

/** 与实体出生一致的非负生命舍入及饱和；三层分别缩放，不能先合计再四舍五入。 */
inline int ScaleHealth(int value, double factor)
{
    const double scaled=static_cast<double>(value)*factor+.5;
    return scaled>=std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>(scaled);
}

/** 只缩放已核实的出生生命，不缩放基础伤害，也不重复应用到读档活体。 */
inline Stats Scaled(Stats value, double healthMultiplier, double armorMultiplier)
{
    if(!value.known || healthMultiplier<=0 || armorMultiplier<=0) return value;
    value.body=ScaleHealth(value.body,healthMultiplier);
    value.helm=ScaleHealth(value.helm,healthMultiplier*armorMultiplier);
    value.shield=ScaleHealth(value.shield,healthMultiplier*armorMultiplier);
    return value;
}

/** 仅声明出生掉头资格；气球落地等动态转换继续由实体自己的阶段处理。 */
constexpr bool DropsHeadAtBirth(ZombieType type)
{
    using Z=ZombieType;
    switch(type) {
    case Z::ZOMBIE_GARGANTUAR: case Z::ZOMBIE_REDEYE_GARGANTUAR: case Z::ZOMBIE_ROOF_MARSHAL:
    case Z::ZOMBIE_ZAMBONI: case Z::ZOMBIE_GILDED_ZAMBONI:
    case Z::ZOMBIE_CATAPULT: case Z::ZOMBIE_ELITE_CATAPULT:
    case Z::ZOMBIE_BUNGEE: case Z::ZOMBIE_BALLOON: return false;
    default: return true;
    }
}

/** 返回未缩放的出生数值；雪橇只含队长，气球的飞行耐久不属于这三层。 */
constexpr Stats Get(ZombieType type)
{
    using Z = ZombieType;
    switch (type) {
    case Z::ZOMBIE_NORMAL: case Z::ZOMBIE_POOL_NORMAL: return Known(270); // 普通本体生命
    case Z::ZOMBIE_TRAFFIC_CONE: case Z::ZOMBIE_POOL_CONE: return Known(270, 370); // 普通路障的一类防具
    case Z::ZOMBIE_BUCKET: case Z::ZOMBIE_POOL_BUCKET: case Z::ZOMBIE_WEATHER_JAMMER: return Known(270, 1100); // 铁桶及干扰者同源防具
    case Z::ZOMBIE_FASTBUCKET: return Known(270, 600, 0, 75); // 快桶减甲并提高基础单口伤害
    case Z::ZOMBIE_NEWSPAPER: return Known(300, 0, 500); // 报纸属于二类盾，破纸后的伤害翻倍另算
    case Z::ZOMBIE_FASTPAPER: return Known(350, 0, 700, 75); // 加强读报出生生命与基础单口伤害
    case Z::ZOMBIE_DOOR: return Known(270, 0, 1100); // 普通铁门与实体公开常量共用
    case Z::ZOMBIE_REINFORCED_DOOR: return Known(270, 0, 1030); // 加固门每击上限不通过虚增耐久表达
    case Z::ZOMBIE_FOOTBALL: return Known(270, 1400); // 普通橄榄球头盔生命
    case Z::ZOMBIE_PINK_FOOTBALL: return Known(220, 900, 0, 40); // 首击与破帽技能不并入本体或常规啃食
    case Z::ZOMBIE_POLEVAULTER: return Known(500); // 普通撑杆出生本体生命
    case Z::ZOMBIE_ELITE_POLEVAULTER: return Known(450); // 精英撑杆出生本体生命
    case Z::ZOMBIE_POGO: return Known(500); // 普通跳跳出生本体生命
    case Z::ZOMBIE_ELITE_POGO: return Known(850); // 精英跳跳出生本体生命
    case Z::ZOMBIE_DIGGER: return Known(270, 100); // 普通矿工安全帽是一类防具
    case Z::ZOMBIE_ELITE_DIGGER: return Known(250, 100); // 爆破工头出生本体与安全帽
    case Z::ZOMBIE_JACK_IN_THE_BOX: return Known(500); // 普通小丑本体生命，盒子不承担伤害
    case Z::ZOMBIE_ELITE_JACK_IN_THE_BOX: return Known(900, 0, 0, 65); // 精英小丑初始生命与单口伤害
    case Z::ZOMBIE_LADDER: return Known(500, 0, 500); // 扶梯属于二类盾
    case Z::ZOMBIE_ELITE_LADDER: return Known(650, 0, 500); // 五秒后分支加血不提前记为出生生命
    case Z::ZOMBIE_ZAMBONI: return Known(1350); // 普通冰车本体生命
    case Z::ZOMBIE_GILDED_ZAMBONI: return Known(2200); // 鎏金冰车本体生命
    case Z::ZOMBIE_CATAPULT: return Known(CatapultRules::kBodyHealth); // 投篮车沿用自己的共用规则
    case Z::ZOMBIE_ELITE_CATAPULT: return Known(CatapultRules::kEliteBodyHealth); // 导流车不共用普通车生命
    case Z::ZOMBIE_GARGANTUAR: return Known(3000); // 普通巨人本体生命，小鬼独立承伤
    case Z::ZOMBIE_REDEYE_GARGANTUAR: return Known(6000); // 红眼巨人本体生命
    case Z::ZOMBIE_IMP: return Known(ImpThrowRules::Health); // 小鬼不合并进投掷者生命
    case Z::ZOMBIE_DANCER: return Known(DancerRules::BodyHealth); // 普通舞王，伴舞独立预测
    case Z::ZOMBIE_ELITE_DANCER: return Known(DancerRules::EliteBodyHealth); // 精英舞王本体生命
    case Z::ZOMBIE_BACKUP_DANCER: return Known(DancerRules::BackupBodyHealth); // 单只伴舞本体生命
    case Z::ZOMBIE_DOLPHIN_RIDER: return Known(500); // 普通海豚骑士本体生命
    case Z::ZOMBIE_ELITE_DOLPHIN_RIDER: return Known(700); // 精英海豚骑士本体生命
    case Z::ZOMBIE_BUNGEE: return Known(450); // 蹦极全阶段共用本体生命
    case Z::ZOMBIE_BALLOON: return Known(270); // 飞行气球额外耐久由独立字段表达
    case Z::ZOMBIE_BOBSLED_TEAM: return Known(270, 300); // 仅队长的初始本体与雪橇，三名队员另出生
    case Z::ZOMBIE_ROOF_MARSHAL: return Known(15000, 0, 0, 250); // 督军首领数值，不代表已进入指挥官购买池
    case Z::ZOMBIE_INSULATOR: return Known(300, 1200); // 绝缘胸甲是一类防具
    case Z::ZOMBIE_GROUNDING: return Known(270, 1200); // 天线路障继承普通本体生命
    case Z::ZOMBIE_HIJACKER: return Known(1000); // 锁定后加血由阶段触发，不预支
    case Z::ZOMBIE_HEALER: return Known(800); // 急救员初始本体生命
    case Z::ZOMBIE_ICE_WALL_ENGINEER: return Known(800, 370); // 工兵继承路障防具，独立冰墙不并入生命
    case Z::ZOMBIE_ICE_CRACK_DRILL: return Known(650, 900); // 冰制钻机与步行本体分层
    case Z::ZOMBIE_ICE_STATUE_EXECUTIONER: return Known(300, 2700); // 冰像处刑者黑帽与本体分层
    case Z::ZOMBIE_SNOW_BURROW: return Known(700); // 潜雪不增加本体生命
    case Z::ZOMBIE_ADAPTIVE_HELMET: return Known(AdaptiveHelmetRules::BodyHealth, AdaptiveHelmetRules::HelmetHealth); // 自有头盔规则
    case Z::ZOMBIE_THERMAL_SNIPER: return Known(1200); // 热感狙击本体生命，枪不充当防具
    case Z::ZOMBIE_AURORA_PRIEST: return Known(AuroraPriestRules::BodyHealth, AuroraPriestRules::DeviceHealth, 0, AuroraPriestRules::NormalBite); // 祭司仪器为一类防具
    case Z::ZOMBIE_POLAR_CLOCKMAKER: return Known(PolarClockRules::BodyHealth, PolarClockRules::ArmorHealth); // 星盘与本体读已有规则
    case Z::ZOMBIE_EXCAVATOR: return Known(1800); // 开凿者帽子没有防具生命
    case Z::ZOMBIE_CRYSTAL_HORN_MINER: return Known(1500, 2500, 0, 100); // 晶角头盔与冲撞本体分层
    case Z::ZOMBIE_SUN_THIEF: return Known(1500); // 储光罐不提供防具生命
    case Z::ZOMBIE_CRYSTAL_DRUMMER: return Known(CrystalDrummerRules::Health); // 晶鼓不承担伤害
    case Z::ZOMBIE_ICE_WORKER: return Known(IceProduction::WorkerHealth); // 工人生命与生产入口共用
    case Z::ZOMBIE_BOILER: return Known(BoilerRules::kHealth); // 超频不虚增本体生命
    case Z::ZOMBIE_COLD_CHAIN_GUARD: return Known(ColdChainGuardRules::kBodyHealth, ColdChainGuardRules::kShieldHealth); // 冰盾属于一类防具
    case Z::ZOMBIE_FLOOD_MORTAR: return Known(FloodMortarRules::Health);
    case Z::ZOMBIE_PRESSURE_SHOOTER: return Known(PressureShooterRules::Health); // 枪头不是承伤防具
    case Z::ZOMBIE_DISASTER_ENGINEER: return Known(DisasterEngineerRules::Health); // 冷却罐不是承伤防具
    default: return {}; // 未实现或未核实类型显式未知，不按默认生命参与推演
    }
}
}
