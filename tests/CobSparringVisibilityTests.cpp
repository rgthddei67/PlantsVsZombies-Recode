#include "Game/AutoTest/CobSparringVisibility.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace CobSparringVisibility;
constexpr Rect kViewport{0, 0, 1100, 600}; // 当前正式场景视口；纯测试不启动渲染器

/** 检查像素投影契约，失败即结束；这些用例不替代实际画面的同步取证。 */
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED cob sparring visibility: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float actual, float expected) { return std::abs(actual - expected) < 0.001f; }
}

/** 覆盖透明屏边、真实不透明伸入、裁剪、变换、失效输入和过期绘制证据。 */
int main() {
    const std::array<std::uint8_t, 4> opaque{255, 255, 255, 255};
    const Quad edge{4, 0, 0, 1, 1099, 100};
    {
        const std::array<std::uint8_t, 4> padding{0, 0, 255, 255};
        Check(!FindOpaqueWitness(padding, 4, 1, edge, 1, kViewport),
            "transparent padding crossing the screen cannot expose offscreen opaque pixels");
        const std::array<std::uint8_t, 4> visibleEdge{255, 0, 0, 0};
        const auto point = FindOpaqueWitness(visibleEdge, 4, 1, edge, 1, kViewport);
        Check(point && Near(point->x, 1099.5f) && Near(point->y, 100.5f),
            "real opaque edge texel yields its own in-bounds position");
    }
    {
        Check(!FindOpaqueWitness(opaque, 4, 1, {4, 0, 0, 1, 1100, 100}, 1, kViewport),
            "fully offscreen body is rejected even immediately outside the right border");
        Check(!FindOpaqueWitness(opaque, 4, 1, {4, 0, 0, 1, 1140, 100}, 1, kViewport),
            "being within cannon splash range supplies no visual evidence");
        Check(!FindOpaqueWitness(opaque, 4, 1, {4, 0, 0, 1, -4, 100}, 1, kViewport),
            "fully offscreen left body is rejected");
        Check(!FindOpaqueWitness(opaque, 4, 1, {4, 0, 0, 1, 20, 600}, 1, kViewport),
            "body below the viewport is rejected");
    }
    {
        Check(!FindOpaqueWitness(opaque, 4, 1, edge, 0, kViewport),
            "zero final track or root alpha rejects otherwise opaque body pixels");
        const std::array<std::uint8_t, 4> transparent{0, 0, 0, 0};
        Check(!FindOpaqueWitness(transparent, 4, 1, edge, 1, kViewport),
            "all-transparent texture provides no witness");
        Check(!FindOpaqueWitness({}, 4, 1, edge, 1, kViewport),
            "missing AutoTest mask fails closed");
        Check(!FindOpaqueWitness(opaque, 3, 1, edge, 1, kViewport),
            "mask dimension mismatch fails closed");
    }
    {
        Check(!FindOpaqueWitness(opaque, 4, 1, edge, 1, kViewport, Rect{0, 0, 1099, 600}),
            "active clip can remove the complete visible fragment");
        Check(!FindOpaqueWitness(opaque, 4, 1, edge, 1, kViewport, Rect{0, 0, 1100, 100}),
            "waterline-style bottom clip removes a lower body sample");
        const auto point = FindOpaqueWitness(opaque, 4, 1, edge, 1, kViewport, Rect{1099, 100, 1100, 101});
        Check(point && Near(point->x, 1099.5f), "a sample surviving the full clip intersection is valid");
        Check(!FindOpaqueWitness(opaque, 4, 1, edge, 1, kViewport, Rect{1100, 0, 1100, 600}),
            "empty clip fails closed");
    }
    {
        const std::array<std::uint8_t, 4> leftPixel{255, 0, 0, 0};
        const auto mirrored = FindOpaqueWitness(leftPixel, 4, 1, {-4, 0, 0, 1, 1100, 100}, 1, kViewport);
        Check(mirrored && Near(mirrored->x, 1099.5f), "mirror uses the submitted final affine transform");
        const auto rotated = FindOpaqueWitness(leftPixel, 4, 1, {0, 4, -1, 0, 1100, 100}, 1, kViewport);
        Check(rotated && Near(rotated->x, 1099.5f) && Near(rotated->y, 100.5f),
            "rotated body texel is checked in final world coordinates");
        Check(!FindOpaqueWitness(leftPixel, 4, 1, {0, 0, 0, 1, 100, 100}, 1, kViewport),
            "zero-area quad fails closed");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(!FindOpaqueWitness(leftPixel, 4, 1, {4, 0, 0, 1, nan, 100}, 1, kViewport),
            "nonfinite transform fails closed");
        Check(!FindOpaqueWitness(leftPixel, 4, 1, edge, nan, kViewport),
            "nonfinite submitted alpha fails closed");
    }
    {
        // 大遮罩上的采样也只能返回真实不透明源点；未采到允许拒绝，不能回退到包围盒。
        std::vector<std::uint8_t> sparse(512 * 512, 0);
        Check(!FindOpaqueWitness(sparse, 512, 512, {512, 0, 0, 512, 0, 0}, 1, kViewport),
            "bounded sampling never fabricates an opaque witness");
        sparse[0] = 255;
        const auto point = FindOpaqueWitness(sparse, 512, 512, {512, 0, 0, 512, 0, 0}, 1, kViewport);
        Check(point && Near(point->x, 0.5f) && Near(point->y, 0.5f),
            "bounded sampling retains a known real texel");
    }
    {
        Check(IsFresh(10, 10) && IsFresh(10, 10.05), "same-frame and three-step-old witnesses remain fresh");
        Check(!IsFresh(10, 10.052), "older evidence expires instead of persisting indefinitely");
        Check(!IsFresh(10, 9.99) && !IsFresh(-1, 0), "time reset and uninitialized timestamps fail closed");
        Check(!IsFresh(std::numeric_limits<double>::infinity(), 10)
            && !IsFresh(10, std::numeric_limits<double>::quiet_NaN()), "nonfinite clocks fail closed");
    }
    std::cout << "Cob sparring opaque visibility contract passed\n";
}
