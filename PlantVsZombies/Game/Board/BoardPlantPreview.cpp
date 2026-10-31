#include "Board.h"
#include "../Cell.h"
#include "../Plant/Plant.h"

Plant* Board::CreatePlantingPreview(PlantType plantType, int row, int column) {
    const bool held = row == -1 && column == -1;
    Cell* anchor = nullptr;
    if (!held) {
        int anchorRow = row, anchorColumn = column;
        if (!ResolvePlantPlacementAnchor(plantType, row, column, anchorRow, anchorColumn)) return nullptr;
        anchor = GetCell(anchorRow, anchorColumn);
        if (!anchor) return nullptr;
    }
    auto* preview = CreatePlant(plantType, 0, 0, true, true);
    if (!preview) return nullptr;
    preview->PauseAnimation();
    preview->RemoveShadow();
    // 预览只表达视觉落点，不参与任何按行列查询的正式植物行为。
    preview->mRow = preview->mColumn = -1;
    if (held) {
        preview->SetRenderOrder(LAYER_EFFECTS + 10000);
    } else {
        auto center = anchor->GetCenterPosition();
        if (!preview->IsRoofSupportPlant() && GetFlowerPotAt(anchor->mRow, anchor->mColumn)
            && !IsPoolSquare(anchor->mRow, anchor->mColumn)) center.y += Plant::kFlowerPotVisualLiftY;
        // 与原版落点一致：盖在承载植物上方，不借用相邻植物的整数层号。
        const auto* top = GetTopPlantAt(anchor->mRow, anchor->mColumn);
        preview->SetRenderOrder(top ? top->GetRenderOrder() : LAYER_GAME_PLANT, 1);
        preview->SetPosition(center);
        preview->SetAlpha(0.35f);
    }
    return preview;
}
