#include "Board.h"
#include "WatersideGardenRules.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/PlantFootprint.h"
#include "Game/Plant/PlantUpgradeRules.h"
#include "Game/AudioSystem.h"
#include "GameRandom.h"
#include "ResourceKeys.h"
#include "ParticleSystem/ParticleSystem.h"
#include <algorithm>
#include <cmath>

bool Board::SupportsWatersideFlood() const {
    return IsWatersideGarden() && (mIsSurvival || mLevel >= 101);
}

bool Board::IsOriginalPoolSquare(int row, int col) const {
    return IsWatersideGarden() && WatersideGardenRules::Terrain(row, col) == '~';
}

float Board::GetWatersideWaterRate() const {
    return SupportsWatersideFlood() ? WatersideFlood::Rate(GetRainIntensity()) : 0.0f;
}

float Board::GetWatersideFloodThreshold(int row, int col) const {
    const char terrain = WatersideGardenRules::Terrain(row, col);
    if (!terrain) return WatersideFloodRules::MaximumWater + 1.0f;
    if (terrain == '~') return 0.0f;
    int distance = WatersideGardenRules::Rows + WatersideGardenRules::Columns;
    for (int r = 0; r < WatersideGardenRules::Rows; ++r)
        for (int c = 0; c < WatersideGardenRules::Columns; ++c)
            if (WatersideGardenRules::Terrain(r, c) == '~')
                distance = std::min(distance, std::abs(r - row) + std::abs(c - col));
    return distance * WatersideFloodRules::ShoreStep
        + (terrain == '#' ? WatersideFloodRules::StoneRise : 0.0f);
}

float Board::GetWatersideFloodCoverage(int row, int col) const {
    if (!SupportsWatersideFlood() || IsOriginalPoolSquare(row, col)
        || !WatersideGardenRules::Terrain(row, col)) return 0.0f;
    return std::clamp((GetWatersideWater() - GetWatersideFloodThreshold(row, col))
        / WatersideFloodRules::ShoreBlend + 0.5f, 0.0f, 1.0f);
}

float Board::GetWatersideFloodNodeDepth(int row, int col) const {
    const int innerRow = std::clamp(row, 0, mRows - 1);
    const int innerCol = std::clamp(col, 0, mColumns - 1);
    float ground = IsOriginalPoolSquare(innerRow, innerCol) ? -WatersideFloodRules::ShoreStep
        : GetWatersideFloodThreshold(innerRow, innerCol);
    const int outsideDistance = std::abs(row - innerRow) + std::abs(col - innerCol);
    if (outsideDistance > 0) {
        // 原池在零水位时止于棋盘边；涨水后四周按连续地势漫开，满水则越过背景边缘。
        ground = std::min(WatersideFloodRules::MaximumWater - WatersideFloodRules::ShoreBlend,
            std::max(ground, 0.0f) + outsideDistance * WatersideFloodRules::OuterShoreRise);
    }
    return GetWatersideWater() - ground;
}

float Board::GetWatersideFloodDepthAt(float worldX, float worldY) const {
    const Vector first = GetCellCenterPosition(0, 0);
    const float gridX = (worldX - first.x) / CELL_COLLIDER_SIZE_X;
    const float gridY = (worldY - first.y) / mCellHeight;
    const int col = static_cast<int>(std::floor(gridX));
    const int row = static_cast<int>(std::floor(gridY));
    const auto smooth = [](float value) { return value * value * (3.0f - 2.0f * value); };
    const float u = smooth(gridX - col), v = smooth(gridY - row);
    return std::lerp(std::lerp(GetWatersideFloodNodeDepth(row, col),
        GetWatersideFloodNodeDepth(row, col + 1), u),
        std::lerp(GetWatersideFloodNodeDepth(row + 1, col),
            GetWatersideFloodNodeDepth(row + 1, col + 1), u), v);
}

bool Board::IsFloodedSquare(int row, int col) const {
    return GetWatersideFloodCoverage(row, col) >= 0.5f;
}

bool Board::IsEntireRowFlooded(int row) const {
    if (!SupportsWatersideFlood() || row < 0 || row >= mRows) return false;
    for (int col = 0; col < mColumns; ++col)
        if (!IsPoolSquare(row, col)) return false;
    return true;
}

void Board::UpdateWatersideFlood(float seconds) {
    if (!SupportsWatersideFlood() || GetState() != BoardState::GAME || seconds <= 0.0f) return;
    mWatersideFlood.Advance(seconds, GetRainIntensity());
    if (!mWatersideFlood.GetTargets().empty() && mWatersideFlood.GetWarningRemaining() <= 0.0f) {
        // 已锁定目标按实体 ID 复核：补盆、离水、被铲或被吃掉都不能转嫁给另一片睡莲。
        for (int id : mWatersideFlood.GetTargets()) {
            Plant* lily = mEntityRegistry.GetPlant(id);
            if (!IsWatersideSurgeTarget(lily)) continue;
            if (g_particleSystem) g_particleSystem->EmitEffect("PlantingPool", GetCellCenterPosition(lily->mRow, lily->mColumn));
            lily->Die(); // 只释放睡莲层，保留上层植物与原有15秒救援规则。
        }
        mWatersideFlood.FinishSurge();
        AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_PLANT_WATER, 0.5f);
    }
    if (!mWatersideFlood.CanBeginSurge()) return;
    std::vector<int> candidates;
    for (int id : mEntityRegistry.GetAllPlantIDs())
        if (IsWatersideSurgeTarget(mEntityRegistry.GetPlant(id))) candidates.push_back(id);
    std::sort(candidates.begin(), candidates.end());
    std::vector<int> targets;
    while (!candidates.empty() && targets.size() < WatersideFloodRules::SurgeTargetLimit) {
        const int index = GameRandom::Range(0, static_cast<int>(candidates.size()) - 1);
        targets.push_back(candidates[index]);
        candidates.erase(candidates.begin() + index);
    }
    mWatersideFlood.BeginSurge(std::move(targets));
}

bool Board::UsesFoundationLayer(PlantType type) const {
    return SupportsWatersideFlood() && IsUnderPlantLayerType(type) && type != PlantType::PLANT_LILYPAD;
}

bool Board::IsWatersideStoneSquare(int row, int col) const {
    return SupportsWatersideFlood() && WatersideGardenRules::Terrain(row, col) == '#';
}

bool Board::IsWatersideLilyAnchored(const Plant* lily) const {
    if (!lily || !lily->IsActive() || lily->IsSquished()
        || lily->GetPlacementType() != PlantType::PLANT_LILYPAD
        || !IsWatersideStoneSquare(lily->mRow, lily->mColumn)
        || GetUnderPlantAt(lily->mRow, lily->mColumn) != lily) return false;
    const Plant* pot = GetFlowerPotAt(lily->mRow, lily->mColumn);
    return pot && pot->IsActive() && pot->mPlantHealth > 0 && !pot->IsSquished() && !pot->IsBungeeTargeted();
}

bool Board::IsWatersideSurgeTarget(const Plant* lily) const {
    return lily && lily->IsActive() && lily->mPlantHealth > 0 && !lily->IsSquished()
        && !lily->IsIceSealed() && !lily->IsBungeeTargeted()
        && lily->GetPlacementType() == PlantType::PLANT_LILYPAD
        && IsPoolSquare(lily->mRow, lily->mColumn)
        && GetUnderPlantAt(lily->mRow, lily->mColumn) == lily && !IsWatersideLilyAnchored(lily);
}

void Board::LoadWatersideFlood(const nlohmann::json& value) {
    mWatersideFlood.Load(SupportsWatersideFlood() ? value : nlohmann::json::object());
}

bool Board::SetWatersideWaterForTesting(float value) {
    if (!SupportsWatersideFlood() || !std::isfinite(value)) return false;
    mWatersideFlood.SetWater(value);
    return true;
}

int Board::GetWatersidePlantHazard(const Plant* plant) const {
    if (!SupportsWatersideFlood() || !plant || plant->IsPreview()
        || !plant->IsActive() || plant->IsSquished() || plant->IsBungeeTargeted()) return 0;
    const PlantType type = plant->GetPlacementType();
    if (type == PlantType::PLANT_INSTANT_COFFEE) return 0;
    // 石板上的真实花盆是沉底基座，自身不溺亡；上层陆生植物仍须补睡莲。
    if (plant->IsRoofSupportPlant() && IsWatersideStoneSquare(plant->mRow, plant->mColumn)) return 0;
    const bool aquatic = type == PlantType::PLANT_LILYPAD || type == PlantType::PLANT_TANGLEKELP
        || type == PlantType::PLANT_SEASHROOM || type == PlantType::PLANT_ANCHORLOTUS;
    const auto footprint = GetPlantFootprint(type);
    int hazard = 0;
    for (std::size_t i = 0; i < footprint.count; ++i) {
        const int row = plant->mRow + footprint.cells[i].rowOffset;
        const int col = plant->mColumn + footprint.cells[i].columnOffset;
        const bool water = IsPoolSquare(row, col);
        if (aquatic) {
            if (!water) return 3;
            continue;
        }
        const Plant* under = GetUnderPlantAt(row, col);
        if (water && !plant->CanGrowDirectlyInWater()
            && (!under || under->GetPlacementType() != PlantType::PLANT_LILYPAD)) return 1;
        if (!water && RequiresFlowerPotAt(row, col) && !GetFlowerPotAt(row, col)) hazard = 2;
    }
    return hazard;
}

bool Board::RequiresFlowerPotAt(int row, int column) const {
    return IsRoofBackground() || (IsWatersideGarden()
        && WatersideGardenRules::Terrain(row, column) == '#' && !IsPoolSquare(row, column));
}

bool Board::IsFertileSquare(int row, int column) const {
    return IsWatersideGarden() && WatersideGardenRules::Terrain(row, column) == '.'
        && !IsPoolSquare(row, column)
        && (IsPoolSquare(row - 1, column) || IsPoolSquare(row + 1, column)
            || IsPoolSquare(row, column - 1) || IsPoolSquare(row, column + 1));
}

float Board::GetPlantFertilityMultiplier(const Plant* plant) const {
    if (!plant || plant->IsPreview() || !IsWatersideGarden()) return 1.0f;
    const auto footprint = GetPlantFootprint(plant->GetPlacementType());
    // 每个占地都必须直接接触肥沃土壤；承载层存在即不能借土壤加速。
    for (std::size_t i = 0; i < footprint.count; ++i) {
        const int row = plant->mRow + footprint.cells[i].rowOffset;
        const int col = plant->mColumn + footprint.cells[i].columnOffset;
        if (!IsFertileSquare(row, col)) return 1.0f;
        if (mCells[row][col]->GetUnderPlantID() != NULL_PLANT_ID
            || mCells[row][col]->GetFoundationPlantID() != NULL_PLANT_ID) return 1.0f;
    }
    return WatersideGardenRules::FertileSpeed;
}

float Board::GetNextPoolRightEdge(int row, float worldX) const {
    if (SupportsWatersideFlood()) {
        if (row < 0 || row >= mRows) return -1.0f;
        const float y = GetCellCenterPosition(row, 0).y;
        if (GetWatersideFloodDepthAt(worldX, y) >= 0.0f) return worldX;
        // 连续岸线可以在 Cell 之间或场外；从当前点向左找首个湿节点，再求实际交界。
        const float firstX = GetCellCenterPosition(row, 0).x;
        const int startCol = static_cast<int>(std::floor((worldX - firstX) / CELL_COLLIDER_SIZE_X));
        float dryX = worldX;
        for (int col = startCol; col >= 0; --col) {
            const float nodeX = firstX + col * CELL_COLLIDER_SIZE_X;
            if (GetWatersideFloodNodeDepth(row, col) >= 0.0f) {
                float wetX = nodeX;
                for (int iteration = 0; iteration < 12; ++iteration) {
                    const float middle = (wetX + dryX) * 0.5f;
                    if (GetWatersideFloodDepthAt(middle, y) >= 0.0f) wetX = middle;
                    else dryX = middle;
                }
                return (wetX + dryX) * 0.5f;
            }
            dryX = nodeX;
        }
        return -1.0f;
    }
    // 从右往左找即将进入的连续水段，适用于同一行有多个水段的地图。
    for (int col = mColumns - 1; col >= 0; --col) {
        if (!IsPoolSquare(row, col) || IsPoolSquare(row, col + 1)) continue;
        const float edge = CELL_INITALIZE_POS_X + (col + 1) * CELL_COLLIDER_SIZE_X;
        if (edge <= worldX) return edge;
    }
    return -1.0f;
}
