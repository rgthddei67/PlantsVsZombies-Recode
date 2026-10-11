#include "CobSparringVisibility.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace CobSparringVisibility {
namespace {
constexpr int kMaximumTexelChecks = 4096; // 每个可见候选四边形的取证采样上限，不影响正式绘制
constexpr double kMaximumWitnessAge = 0.051; // 秒；覆盖渲染帧最多三个未缩放逻辑步，过期即拒绝

/** 裁剪和视口必须是有面积的有限半开矩形。 */
bool ValidRect(const Rect& rect) {
    return std::isfinite(rect.left) && std::isfinite(rect.top)
        && std::isfinite(rect.right) && std::isfinite(rect.bottom)
        && rect.left < rect.right && rect.top < rect.bottom;
}

bool Inside(const Point& point, const Rect& rect) {
    return point.x >= rect.left && point.x < rect.right
        && point.y >= rect.top && point.y < rect.bottom;
}
}

std::optional<Point> FindOpaqueWitness(std::span<const std::uint8_t> alpha,
    int width, int height, const Quad& quad, float submittedAlpha,
    const Rect& viewport, const std::optional<Rect>& clip) {
    if (width <= 0 || height <= 0 || !std::isfinite(submittedAlpha)
        || submittedAlpha <= 0 || !ValidRect(viewport)
        || (clip && !ValidRect(*clip))) return std::nullopt;
    const auto required = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (required != alpha.size()) return std::nullopt;
    const std::array<float, 6> values{quad.a, quad.b, quad.c, quad.d, quad.x, quad.y};
    if (!std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); }))
        return std::nullopt;
    const double determinant = static_cast<double>(quad.a) * quad.d
        - static_cast<double>(quad.b) * quad.c;
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8) return std::nullopt;

    Rect visible = viewport;
    if (clip) {
        visible.left = std::max(visible.left, clip->left);
        visible.top = std::max(visible.top, clip->top);
        visible.right = std::min(visible.right, clip->right);
        visible.bottom = std::min(visible.bottom, clip->bottom);
    }
    // 屏外四边形直接拒绝；逆变换只缩小候选像素范围，最终仍逐点检查真实仿射位置。
    const std::array<Point, 4> corners{{{quad.x, quad.y}, {quad.x + quad.a, quad.y + quad.b},
        {quad.x + quad.c, quad.y + quad.d}, {quad.x + quad.a + quad.c, quad.y + quad.b + quad.d}}};
    float left = corners[0].x, right = left, top = corners[0].y, bottom = top;
    for (const auto& point : corners) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
        left = std::min(left, point.x); right = std::max(right, point.x);
        top = std::min(top, point.y); bottom = std::max(bottom, point.y);
    }
    visible.left = std::max(visible.left, left); visible.right = std::min(visible.right, right);
    visible.top = std::max(visible.top, top); visible.bottom = std::min(visible.bottom, bottom);
    if (!ValidRect(visible)) return std::nullopt;

    double minU = 1, maxU = 0, minV = 1, maxV = 0;
    for (float x : {visible.left, visible.right}) for (float y : {visible.top, visible.bottom}) {
        const double dx = static_cast<double>(x) - quad.x, dy = static_cast<double>(y) - quad.y;
        const double u = (quad.d * dx - quad.c * dy) / determinant;
        const double v = (quad.a * dy - quad.b * dx) / determinant;
        if (!std::isfinite(u) || !std::isfinite(v)) return std::nullopt;
        minU = std::min(minU, u); maxU = std::max(maxU, u);
        minV = std::min(minV, v); maxV = std::max(maxV, v);
    }
    const int firstX = std::clamp(static_cast<int>(std::floor(std::clamp(minU, 0.0, 1.0) * width)), 0, width - 1);
    const int lastX = std::clamp(static_cast<int>(std::ceil(std::clamp(maxU, 0.0, 1.0) * width)), 0, width - 1);
    const int firstY = std::clamp(static_cast<int>(std::floor(std::clamp(minV, 0.0, 1.0) * height)), 0, height - 1);
    const int lastY = std::clamp(static_cast<int>(std::ceil(std::clamp(maxV, 0.0, 1.0) * height)), 0, height - 1);
    const double count = static_cast<double>(lastX - firstX + 1) * (lastY - firstY + 1);
    const int stride = std::max(1, static_cast<int>(std::ceil(std::sqrt(count / kMaximumTexelChecks))));
    int checks = 0;
    for (int y = firstY; y <= lastY && checks < kMaximumTexelChecks; y += stride) {
        for (int x = firstX; x <= lastX && checks < kMaximumTexelChecks; x += stride) {
            ++checks;
            // 即使包围盒覆盖视口，透明边缘也不能为屏外实体生成观察证据。
            if (alpha[static_cast<std::size_t>(y) * width + x] == 0) continue;
            const float u = (static_cast<float>(x) + 0.5f) / width;
            const float v = (static_cast<float>(y) + 0.5f) / height;
            const Point point{quad.a * u + quad.c * v + quad.x,
                quad.b * u + quad.d * v + quad.y};
            if (std::isfinite(point.x) && std::isfinite(point.y) && Inside(point, visible)) return point;
        }
    }
    return std::nullopt;
}

bool IsFresh(double drawUnscaledTime, double nowUnscaledTime) {
    if (!std::isfinite(drawUnscaledTime) || !std::isfinite(nowUnscaledTime) || drawUnscaledTime < 0)
        return false;
    const double age = nowUnscaledTime - drawUnscaledTime;
    return age >= 0 && age <= kMaximumWitnessAge;
}
}
