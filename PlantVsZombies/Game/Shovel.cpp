#include "Shovel.h"
#include "Game/Board/Board.h"
#include "Cell.h"
#include "../UI/InputHandler.h"
#include "GameObjectManager.h"
#include "../ResourceManager.h"
#include "../ResourceKeys.h"
#include "../GameApp.h"
#include <SDL2/SDL.h>

namespace {
	constexpr float kPumpkinShellHitCenterYOffsetInCellHeights = -0.10f; // 外壳命中环中心相对格子中心的纵向比例
	constexpr float kPumpkinShellHitMinYInCellHeights = -0.25f;         // 外壳可见下半部的最上边界比例
	constexpr float kPumpkinShellHitInnerRadiusInCellWidths = 0.3125f;  // 中空区半径；点击其内选壳中植物
	constexpr float kPumpkinShellHitOuterRadiusInCellWidths = 0.625f;   // 南瓜外壳可点击环的外半径

	/** 判断鼠标是否落在南瓜头可见外圈；全部尺寸按当前棋盘格派生。 */
	bool IsPumpkinShellHit(const Board& board, int row, int column,
		const Vector& position)
	{
		const Vector cellCenter = board.GetCellCenterPosition(row, column);
		const float cellHeight = board.GetCellHeight();
		if (position.y <= cellCenter.y
			+ cellHeight * kPumpkinShellHitMinYInCellHeights) {
			return false;
		}

		const Vector shellCenter(cellCenter.x, cellCenter.y
			+ cellHeight * kPumpkinShellHitCenterYOffsetInCellHeights);
		const float dx = position.x - shellCenter.x;
		const float dy = position.y - shellCenter.y;
		const float distanceSquared = dx * dx + dy * dy;
		const float innerRadius = CELL_COLLIDER_SIZE_X
			* kPumpkinShellHitInnerRadiusInCellWidths;
		const float outerRadius = CELL_COLLIDER_SIZE_X
			* kPumpkinShellHitOuterRadiusInCellWidths;
		return distanceSquared >= innerRadius * innerRadius
			&& distanceSquared <= outerRadius * outerRadius;
	}
}

Shovel::Shovel(Board* board)
	: mBoard(board)
{
	// mPosition/mHomePosition(默认 Vector 即 0,0) 与 mState(IDLE) 均由头文件就地初始化
	mTexture = ResourceManager::GetInstance()
		.GetTexture(ResourceKeys::Textures::IMAGE_SHOVEL);
	this->mIsUI = true;
	this->SetRenderOrder(LAYER_UI + 50000);
}

void Shovel::Activate()
{
	mState = ShovelState::ACTIVE;
}

void Shovel::SetHomePosition(const Vector& pos)
{
	mHomePosition = pos;
	if (mState == ShovelState::IDLE)
		mPosition = mHomePosition;
}

void Shovel::ReturnHome()
{
	mState = ShovelState::IDLE;
	mPosition = mHomePosition;
	mPlant = nullptr;
}

void Shovel::Die()
{
	mState = ShovelState::IDLE;
	mPosition = mHomePosition;
	GameObjectManager::GetInstance().DestroyGameObject(this);
	mBoard->mShovel.reset();
	mPlant = nullptr;
}

void Shovel::Update()
{
	if (mState == ShovelState::IDLE)
		return;

	auto& input = GameAPP::GetInstance().GetInputHandler();
	mPosition = input.GetMouseWorldPosition();

	CheckPlant();

	if (input.IsMouseButtonPressed(SDL_BUTTON_RIGHT)) {
		ReturnHome();
		mBoard->mCursorObjectManager.ClearActive();
		return;
	}

	if (input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
		TryShovelAtPosition(mPosition);
	}
}

bool Shovel::TryShovelAtPosition(const Vector& position)
{
	if (mState != ShovelState::ACTIVE || !mBoard) return false;
	mPosition = position;
	CheckPlant();
	const bool removed = mPlant && !mPlant->IsIceSealed();
	if (removed) {
		mPlant->Die();
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_DELETEPLANT, 0.3f);
	}
	ReturnHome();
	mBoard->mCursorObjectManager.ClearActive();
	return removed;
}

void Shovel::CheckPlant()
{
	for (auto& rowCells : mBoard->mCells) {
		for (auto& cell : rowCells) {
			if (cell && cell->ContainsPoint(mPosition) && !cell->IsEmpty()) {
				Plant* pumpkin = mBoard->GetPumpkinAt(cell->mRow, cell->mColumn);
				Plant* normal = mBoard->GetNormalPlantAt(cell->mRow, cell->mColumn);
				Plant* under = mBoard->GetUnderPlantAt(cell->mRow, cell->mColumn);

				// 有内层普通植物时，中心开口选内层、可见外圈选南瓜；空壳仍整格易于铲除。
				if (pumpkin && normal) {
					mPlant = IsPumpkinShellHit(*mBoard, cell->mRow,
						cell->mColumn, mPosition) ? pumpkin : normal;
				}
				else {
					mPlant = pumpkin ? pumpkin : (normal ? normal : under);
				}
				if (mPlant && !mPlant->IsIceSealed()) {
					mPlant->SetGlowingTimer(0.1f);
					return;
				}
				mPlant = nullptr;
			}
		}
	}
	mPlant = nullptr;
}

void Shovel::Draw(Graphics* g)
{
	if (!mTexture || !g) return;
	g->DrawTexture(mTexture,
		mPosition.x - mTexture->width * 0.5f,
		mPosition.y - mTexture->height * 0.5f,
		static_cast<float>(mTexture->width),
		static_cast<float>(mTexture->height));
}
