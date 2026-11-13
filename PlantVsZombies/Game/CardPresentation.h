#pragma once

#include "Graphics.h"

constexpr float CARD_SCALE = 0.5f; // 卡牌缩放比例，保留 2× 费用文字采样
constexpr int CARD_WIDTH = static_cast<int>(100 * CARD_SCALE); // 卡片宽度，逻辑 px
constexpr int CARD_HEIGHT = static_cast<int>(140 * CARD_SCALE); // 卡片高度，逻辑 px

/** 植物卡与僵尸卡共用的外观/反馈；不拥有钱包、冷却计时或选择状态。 */
namespace CardPresentation {
enum class State { Cooling, Ready, WaitingSun, Click };
inline constexpr int CostRasterSize = 28; // 费用文字的 2× 超采样字号
inline constexpr float CostDrawScale = 0.5f; // 缩回正式卡片字号
/** 阳光不足只使用均匀灰态；冷却遮罩由真实剩余冷却单独决定。 */
glm::vec4 Tint(State state);
void DrawNormalFrame(Graphics* g, const Texture* texture, float x, float y, const glm::vec4& tint);
void DrawCooldown(Graphics* g, float x, float y, float remainingFraction);
void DrawSelection(Graphics* g, float x, float y);
CachedText AcquireCost(Graphics& g, int cost);
void DrawCost(Graphics* g, const CachedText& text, float x, float y);
void PlayClick(bool accepted);
}
