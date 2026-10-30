#pragma once

/** 水岸工程船的生命、攻击循环与有限护卫；时间均为基础游戏秒。 */
namespace EngineeringShipRules {
inline constexpr int Health = 24000; // 本体基础生命；无独立防具生命层
inline constexpr int AshDamageCap = 1800; // 单次灰烬基础伤害上限，不影响后续词条倍率
inline constexpr int ShellDamageDivisor = 2; // 舱盖关闭时普通伤害除数，整数余量逐击保留
inline constexpr float RelocationSeconds = 2.0f; // 相邻行换位阶段时长
inline constexpr float ChargeSeconds = 4.0f; // 水炮锁格预警时长
inline constexpr float CoolingSeconds = 8.0f; // 常态开舱散热与正常承伤窗口
inline constexpr float EnragedCoolingSeconds = 6.0f; // 低于半血后的开舱散热窗口
inline constexpr int CannonDamage = 300; // 每个命中格正常顶层承受的基础伤害
inline constexpr int CannonCells = 3; // 锁定前线格及其后方格数，包含前线格
inline constexpr float ButterSeconds = 1.25f; // 单次黄油最多定身时间，期间不刷新
inline constexpr float ButterImmunitySeconds = 5.0f; // 黄油解除后的黄油免疫时间
inline constexpr float ShotVisualSeconds = 0.65f; // 已提交水炮的余波表现时长
inline constexpr float SinkingSeconds = 2.0f; // 机械沉没死亡阶段时长，不由普通断头事件回收
inline constexpr float SinkingPixels = 24.0f; // 死亡末段视觉下沉距离，不改变逻辑位置
inline constexpr float ColliderLeft = 10.0f; // 碰撞框相对美术左上角的横向偏移，像素
inline constexpr float ColliderTop = 30.0f; // 碰撞框相对美术左上角的纵向偏移，像素
inline constexpr float ColliderWidth = 150.0f; // 船体受击宽度，像素
inline constexpr float ColliderHeight = 75.0f; // 船体受击高度，像素
inline constexpr float GuardSpacing = 14.0f; // 同批护卫出生纵深间距，像素
inline constexpr int HalfHealthGuardCount = 6; // 半血仅尝试一批四普通、一打捞、一淤泥的总只数
}
