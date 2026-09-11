#include "Game/Board/Board.h"
#include "Game/Zombie/Zombie.h"
#include "GameApp.h"
#include "Graphics.h"
#include "ResourceManager.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr float kFadeSeconds = 5.0f; // 雾潮渐入、渐退各五游戏秒
	constexpr float kFogSeconds = 60.0f; // 单次总时长，游戏秒
	constexpr float kReduction = 0.25f; // 满强度减伤比例
}

float Board::GetMineFogStrength() const
{
	if (!SupportsMineFog() || mMineFogElapsed < 0.0f) return 0.0f;
	return std::clamp(std::min(mMineFogElapsed, kFogSeconds - mMineFogElapsed) / kFadeSeconds, 0.0f, 1.0f);
}

/** 按僵尸逻辑脚底和阵营取得当帧减伤；装饰雾延伸区不扩大玩法范围。 */
float Board::GetMineFogProtection(const Zombie* zombie) const
{
	if (!SupportsMineFog() || !zombie || zombie->IsMindControlled() || GetMineFogStrength() <= 0) return 0.0f;
	const Vector pos = zombie->GetPosition();
	const float left = GetCellCenterPosition(0, 5).x - CELL_COLLIDER_SIZE_X * 0.5f;
	const float right = GetCellCenterPosition(0, mColumns - 1).x + CELL_COLLIDER_SIZE_X * 0.5f;
	return pos.x >= left && pos.x <= right && zombie->mRow >= 0 && zombie->mRow < mRows
		? kReduction * GetMineFogStrength() : 0.0f;
}

int Board::ScaleMineFogDamage(int damage, const Zombie* zombie) const
{
	return SurvivalPerkManager::ScaleNumericDamage(damage, 1.0 - GetMineFogProtection(zombie));
}

/** 推进固定波次雾潮，结束时一次锁定后续波次，读档不重新排期。 */
void Board::UpdateMineFog(float delta)
{
	if (!SupportsMineFog()) return;
	mMineFogNoticeRemaining = std::max(0.0f, mMineFogNoticeRemaining - delta);
	if (mMineFogElapsed >= 0.0f) {
		mMineFogElapsed += delta;
		if (mMineFogElapsed >= kFogSeconds) {
			mMineFogElapsed = -1.0f;
			mMineFogNextWave = mCurrentWave + 5;
		}
	} else if (mCurrentWave >= mMineFogNextWave && mMineFogNextWave <= mMaxWave) {
		mMineFogElapsed = 0.0f;
		if (!mMineFogTutorialSeen) {
			mMineFogTutorialSeen = true;
			mMineFogNoticeRemaining = 8.0f;
		}
	}
}

/** 用普通雾片叠出连续薄雾，只消费 Board 状态，不参与照明和驱散。 */
void Board::DrawMineFog(Graphics* g) const
{
	const float strength = GetMineFogStrength();
	if (!g || strength <= 0.0f) return;
	const Vector first = GetCellCenterPosition(0, 5);
	const float left = first.x - CELL_COLLIDER_SIZE_X * 0.5f;
	const float top = first.y - mCellHeight * 0.5f - 30.0f;
	const float bottom = GetCellCenterPosition(mRows-1,5).y + mCellHeight * 0.5f;
	// 复用普通迷雾原生 210x190 雾片与多层错位；采样网与棋盘格无关，岩壁不截断雾幕。
	// 防护只计算棋盘内的四列；背景雾一直铺到屏幕外，避免在右侧矿洞入口形成裁切线。
	const float right = static_cast<float>(SCENE_WIDTH);
	const int samples = static_cast<int>(std::ceil((right-left)/103.0f)) + 2;
	g->PushClipRect(static_cast<int>(left),static_cast<int>(top),static_cast<int>(right-left),static_cast<int>(bottom-top));
	for (int layer = 0; layer < 3; ++layer) for (int y = -1; y < 8; ++y) for (int x = 0; x < samples; ++x) {
		const int seed = (y+3)*37+(x+3)*19+layer*53;
		const float drift = std::sin(mMineFogElapsed * (0.14f+layer*0.035f) + seed*0.7f)*14.0f;
		const float px = left + 105 + x*103.0f + (y%2)*35.0f + layer*27.0f + drift;
		const float py = top + y*79.0f + layer*23.0f + (seed%17) - 8;
		const float arrival = mMineFogElapsed < kFadeSeconds
			? std::clamp((mMineFogElapsed*(right-left+120.0f)/kFadeSeconds-(right-px-105.0f))/120.0f,0.0f,1.0f) : 1.0f;
		const std::string key = "IMAGE_FOG_PART_" + std::to_string(seed%8);
		if (const Texture* texture = ResourceManager::GetInstance().GetTexture(key,false)) {
			const float pulse = 0.94f + 0.06f*std::sin(mMineFogElapsed*0.45f+seed);
			g->DrawTexture(texture,px-105,py-95,210,190,0,
				glm::vec4(176,206,220,(layer == 0 ? 62.0f : 35.0f)*strength*arrival*pulse));
		}
	}
	g->PopClipRect();
}
