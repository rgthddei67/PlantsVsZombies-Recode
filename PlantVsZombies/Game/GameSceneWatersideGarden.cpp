#include "GameScene.h"
#include "Board/Board.h"
#include "Graphics.h"
#include "ResourceKeys.h"
#include "ResourceManager.h"
#include <array>

namespace {
    // 本张 ImageGen 原稿的分界采样（像素）；只变换美术，目标边界由实际 Cell 提供。
    constexpr std::array<float, 12> kSourceX{0,522,620,720,821,924,1028,1139,1246,1354,1462,2025};
    constexpr std::array<float, 9> kSourceY{0,140,236,328,416,508,604,711,776};
    constexpr float kBackgroundWidth = 1880.0f; // 与泳池底图共用的场外背景宽度，像素
    constexpr float kBackgroundHeight = 720.0f; // 与泳池底图共用的背景高度，像素
    constexpr float kWaterInset = 9.0f; // 动态水面距格边的内缩，保留原画石岸，像素
}

void GameScene::DrawWatersideGarden(Graphics* g) {
    if (!g || !mBoard) return;
    auto& resources = ResourceManager::GetInstance();
    const Texture* texture = resources.GetTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_WATERSIDE_GARDEN, false);
    if (!texture) return; // 资源闭环由专项强断言检查，不借其他地图掩盖缺图。
    std::array<float, 12> x{};
    std::array<float, 9> y{};
    x.front() = mStartX;
    x.back() = mStartX + kBackgroundWidth;
    y.front() = mBackgroundY;
    y.back() = mBackgroundY + kBackgroundHeight;
    for (int col = 0; col <= mBoard->mColumns; ++col)
        x[col + 1] = mBoard->GetCellCenterPosition(0, col).x - CELL_COLLIDER_SIZE_X * 0.5f;
    for (int row = 0; row <= mBoard->mRows; ++row)
        y[row + 1] = mBoard->GetCellCenterPosition(row, 0).y - mBoard->GetCellHeight() * 0.5f;

    // 在原画接缝处分片校准；屋舍及右侧预览区仍完整保留，不移动玩法网格。
    for (std::size_t row = 0; row + 1 < y.size(); ++row)
        for (std::size_t col = 0; col + 1 < x.size(); ++col)
            g->DrawTextureRegion(texture, kSourceX[col], kSourceY[row],
                kSourceX[col + 1] - kSourceX[col], kSourceY[row + 1] - kSourceY[row],
                x[col], y[row], x[col + 1] - x[col], y[row + 1] - y[row]);

    // 合并上下同宽的水段，复用原版动态水面。变换只作用于水面，不影响实体与 UI。
    for (int row = 0; row < mBoard->mRows; ++row) {
        for (int col = 0; col < mBoard->mColumns;) {
            if (!mBoard->IsPoolSquare(row, col)) { ++col; continue; }
            const int first = col;
            while (col < mBoard->mColumns && mBoard->IsPoolSquare(row, col)) ++col;
            const int last = col;
            auto sameRun = [&](int candidate) {
                if (candidate < 0 || candidate >= mBoard->mRows
                    || mBoard->IsPoolSquare(candidate, first - 1)
                    || mBoard->IsPoolSquare(candidate, last)) return false;
                for (int c = first; c < last; ++c)
                    if (!mBoard->IsPoolSquare(candidate, c)) return false;
                return true;
            };
            if (sameRun(row - 1)) continue;
            int bottom = row + 1;
            while (sameRun(bottom)) ++bottom;
            g->PushTransform();
            g->Translate(x[first + 1] + kWaterInset, y[row + 1] + kWaterInset);
            g->Scale((x[last + 1] - x[first + 1] - 2 * kWaterInset) / 720.0f,
                (y[bottom + 1] - y[row + 1] - 2 * kWaterInset) / 159.0f);
            g->DrawPoolEffect(resources.GetTexture(ResourceKeys::Textures::IMAGE_POOL_BASE, false),
                resources.GetTexture(ResourceKeys::Textures::IMAGE_POOL_SHADING, false),
                resources.GetTexture(ResourceKeys::Textures::IMAGE_POOL_CAUSTIC_EFFECT, false),
                -35.0f, -279.0f, mPoolEffectCounter, false);
            g->PopTransform();
        }
    }
}
