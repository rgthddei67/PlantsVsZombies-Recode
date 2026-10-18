#include "Board.h"
#include "WatersideGardenRules.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/PlantFootprint.h"

bool Board::RequiresFlowerPotAt(int row, int column) const {
    return IsRoofBackground() || (IsWatersideGarden()
        && WatersideGardenRules::Terrain(row, column) == '#');
}

bool Board::IsFertileSquare(int row, int column) const {
    return IsWatersideGarden() && WatersideGardenRules::IsFertile(row, column);
}

float Board::GetPlantFertilityMultiplier(const Plant* plant) const {
    if (!plant || plant->IsPreview() || !IsWatersideGarden()) return 1.0f;
    const auto footprint = GetPlantFootprint(plant->GetPlacementType());
    // 每个占地都必须直接接触肥沃土壤；承载层存在即不能借土壤加速。
    for (std::size_t i = 0; i < footprint.count; ++i) {
        const int row = plant->mRow + footprint.cells[i].rowOffset;
        const int col = plant->mColumn + footprint.cells[i].columnOffset;
        if (!IsFertileSquare(row, col)) return 1.0f;
        if (mCells[row][col]->GetUnderPlantID() != NULL_PLANT_ID) return 1.0f;
    }
    return WatersideGardenRules::FertileSpeed;
}

float Board::GetNextPoolRightEdge(int row, float worldX) const {
    // 从右往左找即将进入的连续水段，适用于同一行有多个水段的地图。
    for (int col = mColumns - 1; col >= 0; --col) {
        if (!IsPoolSquare(row, col) || IsPoolSquare(row, col + 1)) continue;
        const float edge = CELL_INITALIZE_POS_X + (col + 1) * CELL_COLLIDER_SIZE_X;
        if (edge <= worldX) return edge;
    }
    return -1.0f;
}
