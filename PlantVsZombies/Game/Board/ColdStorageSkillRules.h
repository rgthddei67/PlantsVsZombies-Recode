#pragma once

#include <glm/vec4.hpp>

class Graphics;

/** 冷藏站双方技能的共用参数；卡牌费用和冷却仍由 gamedata.json 定义。 */
namespace ColdStorageSkillRules {
inline constexpr float DiscountDuration = 10.0f; // 冰惠券的减费持续时间，游戏秒
inline constexpr int DiscountDivisor = 2; // 植物种植与技能冰费除数；不足整冰向上取整
inline constexpr int StrikeUnlockWave = 15; // 指挥官精准清除从本局第几波开始可提交
inline constexpr int StrikeIceCost = 60; // 每次锁定成功消耗的敌方公共冰块
inline constexpr float StrikeCooldown = 30.0f; // 指挥官全局技能冷却，从锁定成功开始计时，游戏秒
inline constexpr float StrikeAimDuration = 2.0f; // 已付款目标的不可打断瞄准提示，游戏秒

/** 绘制技能券图标；卡槽与图鉴复用原生图形，不创建植物或 Animator。 */
void DrawVoucher(Graphics* g, float x, float y, float width, float height,
    const glm::vec4& color = glm::vec4(255.0f));
}
