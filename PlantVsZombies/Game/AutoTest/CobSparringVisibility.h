#pragma once

#include <cstdint>
#include <optional>
#include <span>

/** AutoTest 的保守像素见证；只处理已提交绘制数据，不接受实体位置或能力。 */
namespace CobSparringVisibility {
struct Point { float x = 0, y = 0; };
struct Rect { float left = 0, top = 0, right = 0, bottom = 0; };
/** 与 Animator 最终单位四边形相同：world=(a*u+c*v+x,b*u+d*v+y)。 */
struct Quad { float a = 0, b = 0, c = 0, d = 0, x = 0, y = 0; };

/**
 * 在视口与可选裁剪框内寻找一个实际非透明源像素中心。
 * 只检查有界数量的候选；缺失遮罩、退化矩阵或采样未命中均返回空，绝不从包围盒推定可见。
 */
std::optional<Point> FindOpaqueWitness(std::span<const std::uint8_t> alpha,
    int width, int height, const Quad& quad, float submittedAlpha,
    const Rect& viewport, const std::optional<Rect>& clip = std::nullopt);

/** 只接受最近最多三个未缩放固定步内的绘制证据；倒退或非有限时钟均拒绝。 */
bool IsFresh(double drawUnscaledTime, double nowUnscaledTime);
}
