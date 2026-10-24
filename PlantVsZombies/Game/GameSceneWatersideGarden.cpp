#include "GameScene.h"
#include "Board/Board.h"
#include "Graphics.h"
#include "ResourceKeys.h"
#include "ResourceManager.h"
#include <array>
#include <cmath>
#include "Game/Plant/Plant.h"

namespace {
    // 本张 ImageGen 原稿的分界采样（像素）；只变换美术，目标边界由实际 Cell 提供。
    constexpr std::array<float, 12> kSourceX{0,522,620,720,821,924,1028,1139,1246,1354,1462,2025};
    constexpr std::array<float, 9> kSourceY{0,140,236,328,416,508,604,711,776};
    constexpr float kBackgroundWidth = 1880.0f; // 与泳池底图共用的场外背景宽度，像素
    constexpr float kBackgroundHeight = 720.0f; // 与泳池底图共用的背景高度，像素
    constexpr float kWaterInset = 9.0f; // 动态水面距格边的内缩，保留原画石岸，像素
}

/** 在格中心之间插值连续地势；格中心与玩法阈值精确一致，岸线不沿 Cell 方框裁切。 */
void GameScene::DrawWatersideFlood(Graphics* g) {
    if (!mBoard->SupportsWatersideFlood()) return;
    auto& resources = ResourceManager::GetInstance();
    const Texture* base = resources.GetTexture(ResourceKeys::Textures::IMAGE_POOL_BASE,false);
    const Texture* shading = resources.GetTexture(ResourceKeys::Textures::IMAGE_POOL_SHADING,false);
    const Texture* caustic = resources.GetTexture(ResourceKeys::Textures::IMAGE_POOL_CAUSTIC_EFFECT,false);
    const float height = mBoard->GetCellHeight();
    const Vector first = mBoard->GetCellCenterPosition(0,0);
    auto depth = [&](int row,int col) { return mBoard->GetWatersideFloodNodeDepth(row,col); };
    // shader clip 使用屏幕逻辑坐标；选卡/开场镜头平移时不能直接传世界格位。
    const auto clipLeftTop = g->WorldToLogical(mStartX,mBackgroundY);
    const auto clipRightBottom = g->WorldToLogical(mStartX+kBackgroundWidth,mBackgroundY+kBackgroundHeight);
    g->PushClipRect(static_cast<int>(std::floor(clipLeftTop.x)),static_cast<int>(std::floor(clipLeftTop.y)),
        static_cast<int>(std::ceil(clipRightBottom.x)-std::floor(clipLeftTop.x)),
        static_cast<int>(std::ceil(clipRightBottom.y)-std::floor(clipLeftTop.y)));
    const int firstCol=static_cast<int>(std::floor((mStartX-first.x)/CELL_COLLIDER_SIZE_X));
    const int lastCol=static_cast<int>(std::ceil((mStartX+kBackgroundWidth-first.x)/CELL_COLLIDER_SIZE_X));
    const int firstRow=static_cast<int>(std::floor((mBackgroundY-first.y)/height));
    const int lastRow=static_cast<int>(std::ceil((mBackgroundY+kBackgroundHeight-first.y)/height));
    // 用相邻格中心构造双线性 patch，并延伸到四周背景；共享端点保证岸线连续。
    for (int row=firstRow;row<lastRow;++row) {
        for (int col=firstCol;col<lastCol;++col) {
            const glm::vec4 depths(depth(row,col),depth(row,col+1),depth(row+1,col),depth(row+1,col+1));
            if (std::max({depths.x,depths.y,depths.z,depths.w}) < -1.0f) continue;
            const Vector pos=mBoard->GetCellCenterPosition(row,col);
            g->DrawFloodSurface(base,shading,caustic,pos.x,pos.y,CELL_COLLIDER_SIZE_X,height,
                depths,static_cast<float>(mPoolEffectCounter));
        }
    }
    g->PopClipRect();
}

/** 左侧常驻水位表不跟随天气预报滑出；指针直接读取连续的真实水域值。 */
void GameScene::DrawWatersideWaterGauge(Graphics* g) {
    if (!g || !mBoard || !mBoard->SupportsWatersideFlood() || mBoard->GetState() != BoardState::GAME) return;
    constexpr float cx = 105.0f; // 仪表圆心 X，逻辑像素；位于推车左侧
    constexpr float cy = 303.0f; // 仪表圆心 Y，逻辑像素；避开顶部天气预报
    constexpr float radius = 86.0f; // 圆盘半径，逻辑像素
    const glm::vec4 ink(53, 62, 43, 255);
    const glm::vec4 danger(255, 38, 38, 255);
    const float value = mBoard->GetWatersideWater();
    g->DrawWaterGauge(cx-radius,cy-radius,radius*2.0f,value/WatersideFloodRules::MaximumWater);
    auto position = [&](float value, float r) {
        const float angle = (-135.0f + value * 2.7f) * 3.14159265f / 180.0f;
        return Vector(cx + std::sin(angle) * r, cy - std::cos(angle) * r);
    };
    auto text = [&](const std::string& label, int size, float y, const glm::vec4& color) {
        g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, size, color,
            cx - g->MeasureTextWidth(label, ResourceKeys::Fonts::FONT_FZCQ, size) * 0.5f, y);
    };
    for (int tick : {0,50,100}) {
        const Vector pos = position(static_cast<float>(tick),radius-33.0f);
        const std::string label = std::to_string(tick);
        g->DrawText(label,ResourceKeys::Fonts::FONT_FZCQ,12,ink,
            pos.x-g->MeasureTextWidth(label,ResourceKeys::Fonts::FONT_FZCQ,12)*.5f,pos.y-6);
    }
    text("水位表",16,cy-23,ink);
    text(std::to_string(static_cast<int>(std::lround(value))),22,cy+22,ink);
    text(mBoard->GetWatersideWaterRate() > 0 ? "积水中" : "缓慢排水", 13, cy + 47, ink);
    const auto rain = mBoard->GetRainIntensity();
    const char* names[]{"晴天", "小雨", "中雨", "大雨", "暴雨"};
    text(names[static_cast<int>(rain)], 20, cy + radius + 7,
        rain == RainIntensity::TORRENT ? danger : glm::vec4(239, 226, 175, 255));

    for (int id : mBoard->mEntityRegistry.GetAllPlantIDs()) {
        const Plant* plant = mBoard->mEntityRegistry.GetPlant(id);
        if (!plant || !plant->IsActive() || plant->GetWatersideHazardKind() == 0) continue;
        const Vector anchor = plant->GetVisualAnchorPosition();
        const char* label = plant->GetWatersideHazardKind() == 1 ? "补睡莲 "
            : (plant->GetWatersideHazardKind() == 2 ? "补花盆 " : "缺水 ");
        const std::string warning = std::string(label)
            + std::to_string(static_cast<int>(std::ceil(plant->GetWatersideHazardRemaining())));
        g->DrawText(warning, ResourceKeys::Fonts::FONT_FZCQ, 12, glm::vec4(25, 30, 35, 255), anchor.x - 30 + 1, anchor.y - 58 + 1);
        g->DrawText(warning, ResourceKeys::Fonts::FONT_FZCQ, 12, glm::vec4(255, 215, 95, 255), anchor.x - 30, anchor.y - 58);
    }
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

    // 洪水章节只提交统一水面，原池与扩展水格互斥占格，避免原池上再叠一层水。
    if (mBoard->SupportsWatersideFlood()) { DrawWatersideFlood(g); return; }

    // 合并上下同宽的水段，复用原版动态水面。变换只作用于水面，不影响实体与 UI。
    for (int row = 0; row < mBoard->mRows; ++row) {
        for (int col = 0; col < mBoard->mColumns;) {
            if (!mBoard->IsOriginalPoolSquare(row, col)) { ++col; continue; }
            const int first = col;
            while (col < mBoard->mColumns && mBoard->IsOriginalPoolSquare(row, col)) ++col;
            const int last = col;
            auto sameRun = [&](int candidate) {
                if (candidate < 0 || candidate >= mBoard->mRows
                    || mBoard->IsOriginalPoolSquare(candidate, first - 1)
                    || mBoard->IsOriginalPoolSquare(candidate, last)) return false;
                for (int c = first; c < last; ++c)
                    if (!mBoard->IsOriginalPoolSquare(candidate, c)) return false;
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
