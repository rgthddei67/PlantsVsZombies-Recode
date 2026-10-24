#include "Board.h"
#include "Game/Cell.h"
#include "Game/Ladder.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/PlantFootprint.h"
#include "Game/Plant/CarryVine.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Zombie/Zombie.h"
#include <array>
#include <unordered_set>

namespace {
	/** 固定顺序包含 overlay 与水岸 foundation，整组搬运不得遗留底座。 */
	std::array<int, 5> GetStackIDs(const Cell& cell)
	{
		return cell.GetPlantIDs();
	}
}

int Board::GetRelocationSourceID(int row, int col)
{
	Cell* cell = GetCell(row, col);
	if (!cell) return NULL_PLANT_ID;
	Plant* anchor = GetNormalPlantAt(row, col);
	if (!anchor) anchor = GetTopPlantAt(row, col);
	if (!anchor || !anchor->CanBeRelocated()) return NULL_PLANT_ID;
	const auto footprint = GetPlantFootprint(anchor->GetPlacementType());
	for (std::size_t i = 0; i < footprint.count; ++i) {
		Cell* source = GetCell(anchor->mRow + footprint.cells[i].rowOffset,
			anchor->mColumn + footprint.cells[i].columnOffset);
		if (!source) return NULL_PLANT_ID;
		for (int id : GetStackIDs(*source)) {
			if (id == NULL_PLANT_ID) continue;
			Plant* member = mEntityRegistry.GetPlant(id);
			if (!member || !member->CanBeRelocated()) return NULL_PLANT_ID;
		}
	}
	return anchor->mPlantID;
}

bool Board::CanPlantStackOccupyTerrain(int sourceRow, int sourceCol, int targetRow, int targetCol)
{
	Cell* source = GetCell(sourceRow, sourceCol);
	if (!source || !GetCell(targetRow, targetCol)) return false;
	Plant* under = GetUnderPlantAt(sourceRow, sourceCol);
	const bool water = IsPoolSquare(targetRow, targetCol);
	const bool lily = under && under->GetPlacementType() == PlantType::PLANT_LILYPAD;
	const bool pot = GetFlowerPotAt(sourceRow, sourceCol) != nullptr;
	for (int id : GetStackIDs(*source)) {
		Plant* member = mEntityRegistry.GetPlant(id);
		if (!member || !member->IsActive()) continue;
		const PlantType type = member->GetPlacementType();
		if (type == PlantType::PLANT_LILYPAD) {
			if (!water) return false;
		} else if (member->IsRoofSupportPlant()) {
			if (water && !IsWatersideStoneSquare(targetRow, targetCol)) return false;
		} else if (type == PlantType::PLANT_TANGLEKELP || type == PlantType::PLANT_SEASHROOM
            || type == PlantType::PLANT_ANCHORLOTUS) {
			if (!water || under) return false;
		} else {
			if ((type == PlantType::PLANT_SPIKEWEED || type == PlantType::PLANT_SPIKEROCK)
				&& (water || RequiresFlowerPotAt(targetRow, targetCol))) return false;
			if (water && (!lily || type == PlantType::PLANT_POTATOMINE)) return false;
			if (RequiresFlowerPotAt(targetRow, targetCol) && !pot) return false;
		}
	}
	return true;
}

bool Board::CanRelocatePlantGroup(int sourceID, int row, int col)
{
	Plant* anchor = mEntityRegistry.GetPlant(sourceID);
	if (!anchor || GetRelocationSourceID(anchor->mRow, anchor->mColumn) != sourceID)
		return false;
	const auto footprint = GetPlantFootprint(anchor->GetPlacementType());
	for (std::size_t i = 0; i < footprint.count; ++i) {
		const int targetRow = row + footprint.cells[i].rowOffset;
		const int targetCol = col + footprint.cells[i].columnOffset;
		Cell* target = GetCell(targetRow, targetCol);
		// 目的地必须在点击时已完整为空，不能把自身重叠格视为可交换空格。
		if (!target || !target->IsEmpty() || !CanPlantOnMineCell(targetRow, targetCol)
			|| HasCraterAt(targetRow, targetCol) || HasSnowHoleAt(targetRow, targetCol)
			|| IsCellFrozen(targetRow, targetCol) || IsIceAt(targetRow, targetCol)
			|| HasLadderAt(targetRow, targetCol)) return false;
		const int sourceRow = anchor->mRow + footprint.cells[i].rowOffset;
		const int sourceCol = anchor->mColumn + footprint.cells[i].columnOffset;
		if (!CanPlantStackOccupyTerrain(sourceRow, sourceCol, targetRow, targetCol)) return false;
	}
	return true;
}

bool Board::RelocatePlantGroup(int sourceID, int row, int col)
{
	if (!CanRelocatePlantGroup(sourceID, row, col)) return false;
	Plant* anchor = mEntityRegistry.GetPlant(sourceID);
	const int sourceRow = anchor->mRow;
	const int sourceCol = anchor->mColumn;
	const auto footprint = GetPlantFootprint(anchor->GetPlacementType());
	std::unordered_set<int> moved;
	// 在任何格位变更前解除全部旧啃食；原实体持续存在，保留其内部计时和稳定 ID。
	for (std::size_t i = 0; i < footprint.count; ++i) {
		Cell* source = GetCell(sourceRow + footprint.cells[i].rowOffset,
			sourceCol + footprint.cells[i].columnOffset);
		for (int id : GetStackIDs(*source)) if (id != NULL_PLANT_ID) moved.insert(id);
	}
	for (int zombieID : mEntityRegistry.GetAllZombieIDs()) {
		Zombie* zombie = mEntityRegistry.GetZombie(zombieID);
		if (zombie && moved.count(zombie->GetEatingPlantID()))
			zombie->ReleaseRelocatedPlant(zombie->GetEatingPlantID());
	}
	for (std::size_t i = 0; i < footprint.count; ++i) {
		const int dr = footprint.cells[i].rowOffset;
		const int dc = footprint.cells[i].columnOffset;
		Cell* source = GetCell(sourceRow + dr, sourceCol + dc);
		Cell* target = GetCell(row + dr, col + dc);
		const auto ids = GetStackIDs(*source);
		target->SetUnderPlantID(ids[0]);
		target->SetNormalPlantID(ids[1]);
		target->SetPumpkinPlantID(ids[2]);
		target->SetOverlayPlantID(ids[3]);
		target->SetFoundationPlantID(ids[4]);
		source->ClearUnderPlantID();
		source->ClearNormalPlantID();
		source->ClearPumpkinPlantID();
		source->ClearOverlayPlantID();
		source->ClearFoundationPlantID();
		if (Ladder* ladder = GetLadderAt(sourceRow + dr, sourceCol + dc))
			ladder->MoveToGridCell(row + dr, col + dc);
	}
	for (int id : moved) {
		Plant* member = mEntityRegistry.GetPlant(id);
		member->MoveToGridCell(row + member->mRow - sourceRow,
			col + member->mColumn - sourceCol, 0.0f);
	}
	for (std::size_t i = 0; i < footprint.count; ++i)
		RefreshPlantStackRenderOrder(GetCell(row + footprint.cells[i].rowOffset,
			col + footprint.cells[i].columnOffset));
	// 纯展示实体没有占格或注册 ID，读档只恢复已完成搬运，不重放效果。
	for (const Vector& position : { GetCellCenterPosition(sourceRow, sourceCol),
		GetCellCenterPosition(row, col) }) {
		auto effect = GameDataManager::GetInstance().CreatePlant(
			PlantType::PLANT_CARRYVINE, this, 0, 0, true);
		if (auto* vine = dynamic_cast<CarryVine*>(effect.get())) vine->BeginFeedback(position);
	}
	return true;
}
