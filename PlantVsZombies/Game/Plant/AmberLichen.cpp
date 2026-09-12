#include "AmberLichen.h"
#include "Game/Board/Board.h"
#include "Graphics.h"
#include "ResourceManager.h"

namespace {
	constexpr int kHealth = 300; // 琥珀地衣本体生命
	constexpr int kRange = 2; // 黏液沿矿道延伸的最大格数，包含自身
	constexpr float kMoveFactor = 0.65f; // 地面移动保留倍率，不影响啃食
}

void AmberLichen::SetupPlant()
{
	mPlantHealth = mPlantMaxHealth = kHealth;
	RemoveShadow();
	PlayTrack("anim_idle", 1.0f);
}

bool AmberLichen::CoversCell(const Board& board, int sr, int sc, int row, int column)
{
	if (row < 0 || row >= board.mRows || column < 0 || column >= board.mColumns) return false;
	if (!board.IsMineBackground()) return row == sr && column >= sc && column <= sc + kRange;
	return board.IsCellWithinConnectedRange(sr, sc, row, column, kRange);
}

float AmberLichen::GetGroundSlowFactorAtCell(int row, int column) const
{
	return mBoard && OccupiesGridSlot() && mPlantHealth > 0 && !GetSleepState()
		&& !IsIceSealed() && !IsBungeeTargeted() && CoversCell(*mBoard, mRow, mColumn, row, column)
		? kMoveFactor : 1.0f;
}

void AmberLichen::Draw(Graphics* g)
{
	if (g && !mIsPreview && mBoard && GetGroundSlowFactorAtCell(mRow, mColumn) < 1.0f) {
		const Texture* resin = ResourceManager::GetInstance().GetTexture("IMAGE_AMBERLICHEN_RESIN", false);
		for (int row = 0; row < mBoard->mRows; ++row) for (int col = 0; col < mBoard->mColumns; ++col) {
			if (!CoversCell(*mBoard, mRow, mColumn, row, col)) continue;
			const Vector p = mBoard->GetCellCenterPosition(row, col);
			g->DrawTexture(resin, p.x - 35.0f, p.y + 12.0f, 70.0f, 22.0f, 0,
				glm::vec4(255, 237, 175, 110));
		}
	}
	Plant::Draw(g);
}
