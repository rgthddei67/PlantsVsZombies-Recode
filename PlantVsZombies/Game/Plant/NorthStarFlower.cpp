#include "NorthStarFlower.h"

#include "../../DeltaTime.h"
#include "../../GameApp.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../../ResourceKeys.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr float kChargeDurationSeconds = 12.0f; // 从空能量蓄满一次领域所需的游戏秒
constexpr float kActiveDurationSeconds = 8.0f; // 一次导航领域持续的游戏秒
constexpr float kReadyPulseRate = 0.09f; // 就绪星芒随 Board 帧脉动的频率
constexpr float kNavigationBorderGlowWidth = 8.0f; // 领域边框向内扩散的柔光宽度，单位 px
constexpr float kNavigationBorderCoreWidth = 4.0f; // 领域边框高亮主体宽度，单位 px
constexpr float kCompassDiagonalRatio = 0.58f; // 罗盘星芒斜向尖角相对主方向尖角的长度比例

/** 用机制专属的八向罗盘星代替占位十字，保持缩放后仍有清晰尖角与内切面。 */
void DrawCompassStar(Graphics* g, const Vector& center, float radius, float alpha)
{
	if (!g || radius <= 0.0f || alpha <= 0.0f) return;
	constexpr float kPi = 3.14159265358979323846f;
	std::array<Vector, 16> points{};
	for (size_t i = 0; i < points.size(); ++i) {
		const float angle = -0.5f * kPi + static_cast<float>(i) * kPi / 8.0f;
		const float pointRadius = (i % 4 == 0) ? radius
			: (i % 2 == 0 ? radius * kCompassDiagonalRatio : radius * 0.27f);
		points[i] = center + Vector(std::cos(angle) * pointRadius,
			std::sin(angle) * pointRadius);
	}
	const glm::vec4 glow(74.0f, 207.0f, 255.0f, alpha * 0.42f);
	const glm::vec4 edge(112.0f, 229.0f, 255.0f, alpha);
	for (size_t i = 0; i < points.size(); ++i) {
		const Vector& from = points[i];
		const Vector& to = points[(i + 1) % points.size()];
		g->DrawLine(from.x - 1.0f, from.y, to.x - 1.0f, to.y, glow);
		g->DrawLine(from.x + 1.0f, from.y, to.x + 1.0f, to.y, glow);
		g->DrawLine(from.x, from.y, to.x, to.y, edge);
	}
	const float facet = radius * 0.31f;
	const glm::vec4 facetColor(255.0f, 255.0f, 255.0f, alpha * 0.82f);
	g->DrawLine(center.x, center.y - facet, center.x + facet, center.y,
		facetColor);
	g->DrawLine(center.x + facet, center.y, center.x, center.y + facet,
		facetColor);
	g->DrawLine(center.x, center.y + facet, center.x - facet, center.y,
		facetColor);
	g->DrawLine(center.x - facet, center.y, center.x, center.y - facet,
		facetColor);
}

/** 以柔光带、青色主体和白色内芯叠出在雪地上仍清楚的领域边界。 */
void DrawNavigationBorder(Graphics* g, float x, float y, float width, float height)
{
	if (!g || width <= 0.0f || height <= 0.0f) return;
	const glm::vec4 glow(45.0f, 178.0f, 255.0f, 58.0f);
	const glm::vec4 core(76.0f, 218.0f, 255.0f, 185.0f);
	const glm::vec4 highlight(218.0f, 250.0f, 255.0f, 225.0f);
	g->FillRect(x, y, width, kNavigationBorderGlowWidth, glow);
	g->FillRect(x, y + height - kNavigationBorderGlowWidth,
		width, kNavigationBorderGlowWidth, glow);
	g->FillRect(x, y, kNavigationBorderGlowWidth, height, glow);
	g->FillRect(x + width - kNavigationBorderGlowWidth,
		y, kNavigationBorderGlowWidth, height, glow);
	g->FillRect(x + 2.0f, y + 2.0f,
		width - 4.0f, kNavigationBorderCoreWidth, core);
	g->FillRect(x + 2.0f, y + height - 2.0f - kNavigationBorderCoreWidth,
		width - 4.0f, kNavigationBorderCoreWidth, core);
	g->FillRect(x + 2.0f, y + 2.0f,
		kNavigationBorderCoreWidth, height - 4.0f, core);
	g->FillRect(x + width - 2.0f - kNavigationBorderCoreWidth, y + 2.0f,
		kNavigationBorderCoreWidth, height - 4.0f, core);
	g->DrawRect(x + 4.0f, y + 4.0f, width - 8.0f, height - 8.0f, highlight);
}
}

void NorthStarFlower::SetupPlant()
{
	mPlantHealth = 300;
	mPlantMaxHealth = 300;
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(0.0f, 28.0f));
		shadow->SetScale(Vector(0.72f, 0.55f));
	}
}

/** 导航持续时间使用游戏秒，待机充能受肥沃加速。 */
void NorthStarFlower::PlantUpdate()
{
	if (mIsPreview) return;
	const float deltaTime = DeltaTime::GetDeltaTime();
	if (mActiveRemaining > 0.0f) {
		mActiveRemaining = std::max(0.0f, mActiveRemaining - deltaTime);
		return;
	}
	mChargeSeconds = std::min(kChargeDurationSeconds, mChargeSeconds + deltaTime * GetIndependentActionSpeedMultiplier());
}

bool NorthStarFlower::CoversPolarNavigationCell(int row, int column) const
{
	return IsActive() && !IsSquished() && std::abs(row - mRow) <= 1
		&& std::abs(column - mColumn) <= 1;
}

bool NorthStarFlower::IsPolarNavigationReady() const
{
	return IsActive() && !mIsPreview && !IsSquished() && mActiveRemaining <= 0.0f
		&& mChargeSeconds >= kChargeDurationSeconds;
}

bool NorthStarFlower::ActivatePolarNavigation()
{
	if (!IsPolarNavigationReady()) return false;
	mChargeSeconds = 0.0f;
	mActiveRemaining = kActiveDurationSeconds;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.36f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("NorthStarActivate", GetVisualAnchorPosition());
	}
	return true;
}

void NorthStarFlower::Draw(Graphics* g)
{
	if (g && mBoard && IsPolarNavigationActive()) {
		const int firstRow = std::max(0, mRow - 1);
		const int lastRow = std::min(mBoard->mRows - 1, mRow + 1);
		const int firstColumn = std::max(0, mColumn - 1);
		const int lastColumn = std::min(mBoard->mColumns - 1, mColumn + 1);
		const Vector topLeft = mBoard->GetCellCenterPosition(firstRow, firstColumn);
		const Vector bottomRight = mBoard->GetCellCenterPosition(lastRow, lastColumn);
		const float halfWidth = CELL_COLLIDER_SIZE_X * 0.5f;
		const float halfHeight = mBoard->GetCellHeight() * 0.5f;
		DrawNavigationBorder(g, topLeft.x - halfWidth, topLeft.y - halfHeight,
			bottomRight.x - topLeft.x + CELL_COLLIDER_SIZE_X,
			bottomRight.y - topLeft.y + mBoard->GetCellHeight());
	}
	Plant::Draw(g);
	if (!g) return;
	const Vector center = GetVisualAnchorPosition() + Vector(0.0f, -25.0f);
	const float pulse = 1.0f + 0.12f * std::sin(
		static_cast<float>(mBoard ? mBoard->mBoardFrame : 0) * kReadyPulseRate);
	const float radius = (IsPolarNavigationReady() || IsPolarNavigationActive())
		? 15.0f * pulse : 8.0f + 6.0f * (mChargeSeconds / kChargeDurationSeconds);
	const float chargeRatio = std::clamp(mChargeSeconds / kChargeDurationSeconds,
		0.0f, 1.0f);
	const float visualStrength = (IsPolarNavigationReady() || IsPolarNavigationActive())
		? 1.0f : chargeRatio;
	DrawCompassStar(g, center, radius, 145.0f + 100.0f * visualStrength);
}

void NorthStarFlower::SaveExtraData(nlohmann::json& j) const
{
	j["chargeSeconds"] = mChargeSeconds;
	j["activeRemaining"] = mActiveRemaining;
}

void NorthStarFlower::LoadExtraData(const nlohmann::json& j)
{
	mChargeSeconds = std::clamp(j.value("chargeSeconds", 0.0f),
		0.0f, kChargeDurationSeconds);
	mActiveRemaining = std::clamp(j.value("activeRemaining", 0.0f),
		0.0f, kActiveDurationSeconds);
}
