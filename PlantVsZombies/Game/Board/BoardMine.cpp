#include "Game/Board/Board.h"
#include "Game/Board/BoardPresentation.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/ExcavatorZombie.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/PrismFlower.h"
#include "Game/Shovel.h"
#include "Game/CardSlotManager.h"
#include "GameApp.h"
#include "Graphics.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include "UI/InputHandler.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr int kMineCost = 75; // 每格开凿消耗阳光
	constexpr float kMineSeconds = 8.0f; // 每格施工工期，游戏秒
	constexpr float kRubbleSeconds = 1.0f; // 完工碎岩消散时长，游戏秒
	constexpr float kMineToolX = 850.0f; // 镐子底座左边界，与铲子底座紧邻，逻辑像素
	constexpr float kMineToolY = -6.0f; // 与铲子底座同高，逻辑像素
	constexpr float kMineToolSize = 70.0f; // 复用铲子底座的点击宽度，逻辑像素

	/** 闭区间裁剪：端点或角点相切仍命中，避免对角岩缝漏弹。 */
	bool IntersectsRock(const Vector& a, const Vector& b, float left, float top, float width, float height)
	{
		float lo = 0.0f, hi = 1.0f;
		for (int axis = 0; axis < 2; ++axis) {
			const float start = axis ? a.y : a.x;
			const float delta = axis ? b.y - a.y : b.x - a.x;
			const float minimum = axis ? top : left;
			const float maximum = minimum + (axis ? height : width);
			if (std::abs(delta) < 0.00001f) {
				if (start < minimum || start > maximum) return false;
				continue;
			}
			float t0 = (minimum - start) / delta, t1 = (maximum - start) / delta;
			if (t0 > t1) std::swap(t0, t1);
			lo = std::max(lo, t0); hi = std::min(hi, t1);
			if (lo > hi) return false;
		}
		return true;
	}

	/** 按格中心投影植物原攻击方向与覆盖范围；辅助植物不虚构攻击射界。 */
	bool CoversMinePreview(PlantType type, int rowDelta, float dx, float dy)
	{
		switch (type) {
		case PlantType::PLANT_PEASHOOTER: case PlantType::PLANT_SNOWPEA:
		case PlantType::PLANT_REPEATER: case PlantType::PLANT_GATLINGPEA:
		case PlantType::PLANT_TOXICPEASHOOTER: case PlantType::PLANT_SCAREDYSHROOM:
		case PlantType::PLANT_ELITE_SCAREDYSHROOM: case PlantType::PLANT_CACTUS:
		case PlantType::PLANT_CABBAGEPULT: case PlantType::PLANT_KERNELPULT:
		case PlantType::PLANT_MELONPULT: case PlantType::PLANT_WINTERMELON:
		case PlantType::PLANT_MELTSNOWPULT:
			return rowDelta == 0 && dx >= 0;
		case PlantType::PLANT_THREEPEATER: return std::abs(rowDelta) <= 1 && dx >= 0;
		case PlantType::PLANT_SPLITPEA: case PlantType::PLANT_JALAPENO: return rowDelta == 0;
		case PlantType::PLANT_LEFTPEATER: return rowDelta == 0 && dx <= 0;
		case PlantType::PLANT_PUFFSHROOM: case PlantType::PLANT_SEASHROOM:
			return rowDelta == 0 && dx >= 0 && dx <= 300.0f;
		case PlantType::PLANT_FUMESHROOM: case PlantType::PLANT_ICEFUMESHROOM:
			return rowDelta == 0 && dx >= 0 && dx <= 390.0f;
		case PlantType::PLANT_CHOMPER: return rowDelta == 0 && dx >= 0 && dx <= 161.5f;
		case PlantType::PLANT_GLOOMSHROOM: return std::abs(rowDelta) <= 1 && std::abs(dx) <= 120.0f;
		case PlantType::PLANT_CHERRYBOMB: return std::abs(rowDelta) <= 1 && std::abs(dx) <= 130.0f;
		case PlantType::PLANT_DOOMSHROOM: return dx * dx + dy * dy <= 250.0f * 250.0f;
		case PlantType::PLANT_ICESHROOM: case PlantType::PLANT_CATTAIL: case PlantType::PLANT_COBCANNON: return true;
		case PlantType::PLANT_STARFRUIT:
			return (rowDelta == 0 && dx <= 0) || std::abs(dx) < 1.0f
				|| (dx > 0 && std::abs(std::abs(dy) - dx * 0.57735f) < 50.0f);
		case PlantType::PLANT_POTATOMINE: case PlantType::PLANT_SPIKEWEED: case PlantType::PLANT_SPIKEROCK:
			return rowDelta == 0 && std::abs(dx) <= 60.0f;
		default: return false;
		}
	}

	void DrawAsset(Graphics* g, const std::string& key, float x, float y, float w, float h, float alpha = 255.0f)
	{
		const Texture* texture = ResourceManager::GetInstance().GetTexture(key, false);
		if (texture) g->DrawTexture(texture, x, y, w, h, 0.0f, glm::vec4(255, 255, 255, alpha));
	}
}

bool Board::MineBlocksSegment(const Vector& from, const Vector& to) const
{
	if (!IsMineBackground()) return false;
	for (int cell = 0; cell < MineGrid::Count; ++cell) {
		if (!mMineGrid.rock[cell]) continue;
		const Vector center = GetCellCenterPosition(cell / MineGrid::Columns, cell % MineGrid::Columns);
		if (IntersectsRock(from, to, center.x - CELL_COLLIDER_SIZE_X * 0.5f,
			center.y - mCellHeight * 0.5f, CELL_COLLIDER_SIZE_X, mCellHeight)) return true;
	}
	return false;
}

bool Board::CanPlantOnMineCell(int row, int col) const
{
	return !IsMineBackground() || (MineGrid::Valid(row, col)
		&& mMineGrid.connected[MineGrid::Index(row, col)] && !mMineGrid.IsRock(row, col));
}

bool Board::BeginMineExcavation(int row, int col)
{
	if (!IsMineBackground() || mBoardState != BoardState::GAME || mMineDigCell >= 0
		|| !mMineGrid.CanExcavate(row, col) || mSun < kMineCost) return false;
	mSun -= kMineCost;
	mMineDigCell = MineGrid::Index(row, col);
	mMineDigRemaining = kMineSeconds;
	mMinePreviewCell = -1;
	return true;
}

bool Board::CancelMineExcavation()
{
	if (!IsMineBackground() || mBoardState != BoardState::GAME || mMineDigCell < 0) return false;
	mSun += kMineCost;
	mMineDigCell = -1;
	mMineDigRemaining = 0.0f;
	return true;
}

void Board::UpdateMine(float deltaTime)
{
	if (!IsMineBackground() || mBoardState != BoardState::GAME) return;
	mMineRubbleTimer = std::max(0.0f, mMineRubbleTimer - deltaTime);
	if (mMineDigCell < 0) return;
	mMineDigRemaining = std::max(0.0f, mMineDigRemaining - deltaTime);
	if (mMineDigRemaining > 0.0f) return;
	// 完工只提交一次；岩壁、连通域与路由距离在同一边沿切换。
	CompleteMineExcavation(mMineDigCell, false);
}

bool Board::CompleteMineExcavation(int wall, bool byZombie)
{
	if (!IsMineBackground() || wall < 0 || wall >= MineGrid::Count || !mMineGrid.rock[wall]) return false;
	if (wall == mMineDigCell) {
		if (byZombie) CancelMineExcavation();
		else { mMineDigCell = -1; mMineDigRemaining = 0.0f; }
	}
	mMineGrid.rock[wall] = false;
	mMineGrid.Rebuild();
	mMineLastDugCell = wall;
	mMineRubbleTimer = kRubbleSeconds;
	mMineWallOwners[wall] = NULL_ZOMBIE_ID;
	return true;
}

void Board::AdvanceMineZombie(Zombie* zombie, float movement)
{
	if (!zombie || movement <= 0.0f) return;
	const bool returning = zombie->IsMovingRight();
	Vector position = zombie->GetPosition();
	const float entranceX = GetCellCenterPosition(zombie->mRow, mColumns - 1).x;
	if (returning && position.x >= entranceX && mMineGrid.entrance[zombie->mRow] && zombie->mMineTargetCell < 0) {
		zombie->GetTransform()->Translate(movement, 0.0f);
		return;
	}
	if (!returning && position.x > entranceX && zombie->mMineTargetCell < 0) {
		position.x = std::max(entranceX, position.x - movement);
		zombie->GetTransform()->SetPosition(position);
		return;
	}
	if (!returning && position.x <= GetCellCenterPosition(zombie->mRow, 0).x && zombie->mMineTargetCell < 0) {
		zombie->GetTransform()->Translate(-movement, 0.0f);
		return;
	}
	if (zombie->mMineTargetCell < 0) {
		const int column = std::clamp(static_cast<int>(std::lround(
			(position.x - GetCellCenterPosition(0, 0).x) / CELL_COLLIDER_SIZE_X)), 0, mColumns - 1);
		const int cell = MineGrid::Index(zombie->mRow, column);
		const int special = zombie->SelectMineNextCell(cell);
		zombie->mMineTargetCell = special != -2 ? special : (returning
			? mMineGrid.NextExit(cell, (zombie->mZombieID & 1) != 0)
			: mMineGrid.Next(cell, (zombie->mZombieID & 1) != 0));
	}
	const int target = zombie->mMineTargetCell;
	if (target < 0) return;
	const int row = target / mColumns, column = target % mColumns;
	Vector destination(GetCellCenterPosition(row, column).x,
		GetZombieSpawnY(row, GetCellCenterPosition(row, column).x));
	Vector difference = destination - position;
	const float length = std::sqrt(difference.sqrMagnitude());
	if (length <= movement) { position = destination; zombie->mMineTargetCell = -1; }
	else position = position + difference * (movement / length);
	zombie->GetTransform()->SetPosition(position);
	const int actualRow = std::clamp(static_cast<int>(std::floor(
		(position.y - GetZombieSpawnY(0, position.x) + mCellHeight * 0.5f) / mCellHeight)), 0, mRows - 1);
	if (actualRow != zombie->mRow) zombie->CommitMineRow(actualRow);
}

void Board::UpdateMineInput()
{
	if (!IsMineBackground() || mBoardState != BoardState::GAME) return;
	auto& input = GameAPP::GetInstance().GetInputHandler();
	const Vector mouse = input.GetMousePosition();
	const Vector world = input.GetMouseWorldPosition();
	if (input.IsMouseButtonPressed(SDL_BUTTON_RIGHT) && mMineToolActive) mCursorObjectManager.ClearActive();
	// 与铲子底座一致，在释放边沿提交一次点击；按下期间允许移动指针和查看反馈。
	if (!input.IsMouseButtonReleased(SDL_BUTTON_LEFT)) return;
	if (mouse.x >= 924 && mouse.x <= 986 && mouse.y >= 0 && mouse.y <= 62) {
		mMineRoutesVisible = !mMineRoutesVisible;
		return;
	}
	if (mouse.x >= kMineToolX && mouse.x <= kMineToolX + kMineToolSize
		&& mouse.y >= kMineToolY && mouse.y <= kMineToolY + kMineToolSize) {
		if (mMineToolActive) mCursorObjectManager.ClearActive();
		else {
			mCursorObjectManager.Activate(CursorObjectType::MINE_PICKAXE, [this]() {
				mMineToolActive = false;
				mMinePreviewCell = -1;
			});
			mMineToolActive = true;
		}
		mMinePreviewCell = -1;
		return;
	}
	if (!mMineToolActive || (DeltaTime::IsPaused() && mBoardState == BoardState::GAME)) return;
	const int r = static_cast<int>(std::floor((world.y - mCellInitialY) / mCellHeight));
	const int c = static_cast<int>(std::floor((world.x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X));
	if (!MineGrid::Valid(r, c)) return;
	const int cell = MineGrid::Index(r, c);
	if (cell == mMineDigCell) { if (CancelMineExcavation()) mCursorObjectManager.ClearActive(); return; }
	if (!mMineGrid.CanExcavate(r, c)) return;
	if (mMinePreviewCell == cell) { if (BeginMineExcavation(r, c)) mCursorObjectManager.ClearActive(); return; }
	mMinePreviewCell = cell;
	mMineTutorialSeen = true;
}

void Board::DrawPrismRangePreview(Graphics* g)
{
	if (!g || !mCardSlotManager || mMineToolActive
		|| mCardSlotManager->GetPlacementPreviewType() != PlantType::PLANT_PRISMFLOWER) return;
	const Cell* hovered = mCardSlotManager->GetPlacementPreviewCell();
	if (!hovered) return;
	const Vector origin = GetCellCenterPosition(hovered->mRow, hovered->mColumn);
	for (int r = 0; r < mRows; ++r) for (int c = 0; c < mColumns; ++c) {
		if (!PrismFlower::CoversCell(r-hovered->mRow,c-hovered->mColumn)) continue;
		const Vector p = GetCellCenterPosition(r,c);
		g->FillRect(p.x-39,p.y-mCellHeight*0.5f+1,78,mCellHeight-2,
			MineBlocksSegment(origin,p) ? glm::vec4(30,25,40,100) : glm::vec4(255,219,105,55));
	}
}

void Board::DrawMineGround(Graphics* g)
{
	if (!IsMineBackground()) return;
	for (int r = 0; r < mRows; ++r) {
		for (int c = 0; c < mColumns; ++c) {
			const Vector p = GetCellCenterPosition(r, c);
			// 细地面边界标出真实可操作格；主体矿土仍来自手绘背景。
			g->DrawRect(p.x - 40, p.y - mCellHeight * 0.5f, 80, mCellHeight, glm::vec4(159, 125, 78, 25));
		}
		if (mMineGrid.entrance[r]) {
			const Vector p = GetCellCenterPosition(r, mColumns - 1);
			const bool forecast = (GetMineForecastEntranceMask() & (1 << r)) != 0;
			DrawAsset(g, ResourceKeys::Textures::IMAGE_MINE_ENTRANCE, p.x + 47, p.y - 88, 98, 138, forecast ? 255.0f : 145.0f);
			if (forecast) {
				const Vector label(p.x + 56, p.y + 24);
				GameAPP::GetInstance().DrawText(u8"下波入口",label,{255,214,135,255},ResourceKeys::Fonts::FONT_FZCQ,13);
			}
		}
	}
	if (mCardSlotManager && !mMineToolActive) {
		const Cell* hovered = mCardSlotManager->GetPlacementPreviewCell();
		const PlantType type = mCardSlotManager->GetPlacementPreviewType();
		if (hovered && CanPlantOnMineCell(hovered->mRow, hovered->mColumn)) {
			const Vector origin = GetCellCenterPosition(hovered->mRow, hovered->mColumn);
			const bool echo = type == PlantType::PLANT_ECHOSHROOM;
			const EchoWave wave = echo ? BuildEchoWave(hovered->mRow,hovered->mColumn) : EchoWave{};
			for (int r = 0; r < mRows; ++r) for (int c = 0; c < mColumns; ++c) {
				const Vector p = GetCellCenterPosition(r,c);
				// 回声菇的预览与发射共用通路图，不能套用普通射手的直线遮挡投影。
				if (echo ? wave.distances[r*mColumns+c] < 0
					: !CoversMinePreview(type,r - hovered->mRow,p.x - origin.x,p.y - origin.y)) continue;
				const bool blocked = !echo && type != PlantType::PLANT_ICESHROOM && MineBlocksSegment(origin,p);
				g->FillRect(p.x - 39,p.y - mCellHeight * 0.5f + 1,78,mCellHeight - 2,
					blocked ? glm::vec4(18,21,31,115) : glm::vec4(90,219,213,58));
			}
		}
	}
	if (!mMineRoutesVisible && mMinePreviewCell < 0) return;
	const auto drawRoutes = [&](const MineGrid& grid, const glm::vec4& color, float shift) {
		std::array<bool, MineGrid::Count> reached{};
		for (int r = 0; r < mRows; ++r) if (grid.entrance[r]) reached[MineGrid::Index(r, mColumns - 1)] = true;
		for (int step = 0; step < MineGrid::Count; ++step) {
			for (int cell = 0; cell < MineGrid::Count; ++cell) {
				if (!reached[cell]) continue;
				const int r = cell / mColumns, c = cell % mColumns;
				const Vector p = GetCellCenterPosition(r, c);
				for (const auto delta : { std::array<int,2>{0,-1}, {-1,0}, {1,0} }) {
					const int nr = r + delta[0], nc = c + delta[1];
					if (!MineGrid::Valid(nr, nc) || (delta[0] && c < 2)) continue;
					const int next = MineGrid::Index(nr,nc);
					if (grid.rock[next] || grid.distance[next] + 1 != grid.distance[cell]) continue;
					reached[next] = true;
					if (step != MineGrid::Count - 1) continue;
					const Vector q = GetCellCenterPosition(nr,nc);
					g->DrawLine(p.x + shift, p.y + shift, q.x + shift, q.y + shift, color);
				}
			}
		}
	};
	drawRoutes(mMineGrid, glm::vec4(83,213,255,210), -3.0f);
	if (mMinePreviewCell >= 0) {
		MineGrid preview = mMineGrid;
		preview.rock[mMinePreviewCell] = false;
		preview.Rebuild();
		drawRoutes(preview, glm::vec4(255,195,73,230), 3.0f);
	}
}

void Board::DrawMineWalls(Graphics* g)
{
	if (!IsMineBackground()) return;
	for (int cell = 0; cell < MineGrid::Count; ++cell) {
		if (!mMineGrid.rock[cell]) continue;
		const int row = cell / mColumns, col = cell % mColumns;
		const Vector p = GetCellCenterPosition(row, col);
		float alpha = 255.0f;
		mEntityRegistry.ForEachZombieInRow(row - 1, [&](Zombie* z) {
			if (z && z->IsActive() && std::abs(z->GetPosition().x - p.x) < 65.0f) alpha = 95.0f;
		});
		if (row > 0 && GetTopPlantAt(row - 1, col)) alpha = 95.0f;
		const auto* worker = dynamic_cast<ExcavatorZombie*>(GetMineWallOwner(cell));
		const bool digging = cell == mMineDigCell || worker;
		const std::string* keys[] = { &ResourceKeys::Textures::IMAGE_MINE_ROCK_A,
			&ResourceKeys::Textures::IMAGE_MINE_ROCK_B, &ResourceKeys::Textures::IMAGE_MINE_ROCK_C };
		const bool below = mMineGrid.IsRock(row + 1, col);
		DrawAsset(g, *keys[(row + col) % 3], p.x - 48, p.y - 81, 96, below ? 140 : 132, alpha);
		if (digging) {
			const float progress = std::max(cell == mMineDigCell ? 1.0f - mMineDigRemaining / kMineSeconds : 0.0f,
				worker ? worker->GetWorkProgress() : 0.0f);
			DrawAsset(g, ResourceKeys::Textures::IMAGE_MINE_ROCK_CRACKED, p.x - 48, p.y - 81, 96, 132, alpha * progress);
			DrawAsset(g, ResourceKeys::Textures::IMAGE_MINE_CHUNKS, p.x - 38, p.y + 14 + progress * 18, 76, 35, 220 * progress);
		}
		if (!below) DrawAsset(g, ResourceKeys::Textures::IMAGE_MINE_RUBBLE, p.x - 43, p.y + 29, 86, 24, 255);
		// 已预留目标用完整矿灯/工具徽记标识，裂纹继续从实际施工进度派生。
		if (worker) {
			DrawAsset(g,"IMAGE_EXCAVATOR_HAT",p.x - 20,p.y - 54,40,28,245);
			DrawAsset(g,"IMAGE_EXCAVATOR_DRILL",p.x - 23,p.y - 33,46,27,245);
		}
		if (mMineToolActive && mMineGrid.CanExcavate(row,col)) {
			g->DrawLine(p.x - 32,p.y + 46,p.x + 32,p.y + 46,
				cell == mMinePreviewCell ? glm::vec4(255,217,101,255) : glm::vec4(113,229,233,210));
		}
	}
	if (mMineLastDugCell >= 0 && mMineRubbleTimer > 0.0f) {
		const Vector p = GetCellCenterPosition(mMineLastDugCell / mColumns, mMineLastDugCell % mColumns);
		DrawAsset(g, ResourceKeys::Textures::IMAGE_MINE_CHUNKS, p.x - 47, p.y - 38 + (1 - mMineRubbleTimer) * 45,
			94, 82, 255 * mMineRubbleTimer);
	}
}

void Board::DrawMineUI(Graphics* g)
{
	if (!IsMineBackground() || mBoardState != BoardState::GAME) return;
	if (mMineFogNoticeRemaining > 0.0f) {
		GameAPP::GetInstance().DrawText(HasPurpleMineFog() ? u8"紫晶雾潮：雾中僵尸受到的伤害降低50%，迷雾无法驱散。" : u8"幽晶雾潮：雾中僵尸受到的伤害降低25%，迷雾无法驱散。",
			Vector(g->LogicalToWorld(215, 105)), {185,230,255,255}, ResourceKeys::Fonts::FONT_FZCQ, 19);
	}
	// 施工进度独立在 UI 层绘制，避免被后绘制的前排岩壁和碎石遮挡。
	if (mMineDigCell >= 0) {
		const Vector p = GetCellCenterPosition(mMineDigCell / mColumns, mMineDigCell % mColumns);
		const float progress = 1.0f - mMineDigRemaining / kMineSeconds;
		g->FillRect(p.x - 30, p.y + 37, 60, 6, glm::vec4(27,25,25,240));
		g->FillRect(p.x - 30, p.y + 37, 60 * progress, 6, glm::vec4(255,193,78,255));
	}
	const Vector tool = g->LogicalToWorld(kMineToolX,kMineToolY);
	DrawAsset(g, ResourceKeys::Textures::IMAGE_SHOVELBANK, tool.x,tool.y,70,72);
	const Vector pick = mMineToolActive ? GameAPP::GetInstance().GetInputHandler().GetMouseWorldPosition()
		: Vector(g->LogicalToWorld(kMineToolX + 35.0f,30.0f));
	DrawAsset(g, ResourceKeys::Textures::IMAGE_MINE_PICKAXE, pick.x - 31,pick.y - 33,62,66);
	const Vector route = g->LogicalToWorld(924,0);
	const Vector mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
	const bool routeHovered = mouse.x >= 924 && mouse.x <= 986 && mouse.y >= 0 && mouse.y <= 62;
	const float pressOffset = routeHovered && GameAPP::GetInstance().GetInputHandler().IsMouseButtonDown(SDL_BUTTON_LEFT) ? 2.0f : 0.0f;
	DrawAsset(g, ResourceKeys::Textures::IMAGE_MINE_ROUTE_BUTTON, route.x,route.y + pressOffset,62,62);
	if (mMineRoutesVisible || routeHovered) {
		g->DrawRect(route.x + 2,route.y + 2 + pressOffset,58,58,
			mMineRoutesVisible ? glm::vec4(84,225,236,255) : glm::vec4(255,218,142,230));
	}
	if (routeHovered) {
		const Vector tip = g->LogicalToWorld(872,70);
		g->FillRect(tip.x,tip.y,114,28,glm::vec4(45,30,20,245));
		GameAPP::GetInstance().DrawText(mMineRoutesVisible ? u8"收起矿道路线" : u8"查看矿道路线",
			Vector(tip.x + 8,tip.y + 6),{255,226,170,255},ResourceKeys::Fonts::FONT_FZCQ,14);
	}
	if (!mMineToolActive && mMineDigCell < 0) return;
	const Vector panel = g->LogicalToWorld(7,340);
	g->FillRect(panel.x,panel.y,188,166,glm::vec4(20,25,33,235));
	auto& app = GameAPP::GetInstance();
	app.DrawText(u8"幽晶矿场 · 开凿",Vector(g->LogicalToWorld(17,351)),{251,211,143,255},ResourceKeys::Fonts::FONT_FZCQ,17);
	app.DrawText(u8"75阳光 / 8秒",Vector(g->LogicalToWorld(17,379)),{239,230,204,255},ResourceKeys::Fonts::FONT_FZCQ,16);
	app.DrawText(mMineDigCell >= 0 ? u8"点击施工格：取消退款" : u8"点选岩壁，再点确认",Vector(g->LogicalToWorld(17,409)),{220,228,231,255},ResourceKeys::Fonts::FONT_FZCQ,14);
	app.DrawText(u8"蓝：当前路  金：新路",Vector(g->LogicalToWorld(17,436)),{151,213,230,255},ResourceKeys::Fonts::FONT_FZCQ,14);
	app.DrawText(mBoardState == BoardState::CHOOSE_CARD ? u8"开战后才能施工" : u8"右键退出镐子",Vector(g->LogicalToWorld(17,465)),{218,195,162,255},ResourceKeys::Fonts::FONT_FZCQ,14);
}
