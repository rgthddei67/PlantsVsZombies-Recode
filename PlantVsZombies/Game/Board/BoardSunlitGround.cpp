#include "Board.h"
#include "SunlitGround.h"
#include "Game/Plant/Plant.h"
#include "Game/Zombie/Zombie.h"
#include "Graphics.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include <algorithm>
#include <cmath>

bool Board::SupportsSunlitGround() const
{
	return !mIsSurvival && mBackGround == Background::GROUND_DAY
		&& mLevel >= SunlitGround::FirstLevel && mLevel <= SunlitGround::FinalLevel;
}

/** 新局和旧档缺省均从零初始化；恢复入口随后只覆盖匹配格位的进度。 */
void Board::InitializeSunlitGround()
{
	mSunlitPlots = {};
	if (SupportsSunlitGround()) mSunlitPlots = SunlitGround::Layouts[mLevel - SunlitGround::FirstLevel];
}

bool Board::IsSunlitPlotOccupied(const SunlitGround::Plot& plot)
{
	const Cell* cell = GetCell(plot.row, plot.column);
	if (!cell) return false;
	for (int id : cell->GetPlantIDs()) {
		const Plant* plant = mEntityRegistry.GetPlant(id);
		if (plant && plant->IsActive()) return true;
	}
	return false;
}

bool Board::IsSunlitPlotTrampled(const SunlitGround::Plot& plot)
{
	bool trampled = false;
	const Vector center = GetCellCenterPosition(plot.row, plot.column);
	mEntityRegistry.ForEachZombieInRow(plot.row, [&](Zombie* zombie) {
		if (trampled || !zombie->CanTrampleGround()) return;
		const float x = zombie->GetPosition().x;
		trampled = x >= center.x - CELL_COLLIDER_SIZE_X * 0.5f
			&& x < center.x + CELL_COLLIDER_SIZE_X * 0.5f;
	});
	return trampled;
}

/** 仅实战推进生产；踩踏优先于占格，防守植物不能把已被踩中的积累保存在脚下。 */
void Board::UpdateSunlitGround(float deltaTime)
{
	if (!SupportsSunlitGround() || mBoardState != BoardState::GAME || deltaTime <= 0.0f) return;
	for (auto& plot : mSunlitPlots) {
		if (plot.row < 0) continue;
		if (IsSunlitPlotTrampled(plot)) { plot.elapsed = 0.0f; continue; }
		if (IsSunlitPlotOccupied(plot)) continue;
		plot.elapsed += deltaTime;
		if (plot.elapsed >= SunlitGround::ProductionSeconds) {
			plot.elapsed -= SunlitGround::ProductionSeconds;
			// 使用正式普通阳光，收取、过期、倍数与在途存档均沿用统一契约。
			const Vector center = GetCellCenterPosition(plot.row, plot.column);
			CreateSun(center.x, center.y - 20.0f, true);
		}
	}
}

nlohmann::json Board::SaveSunlitGround() const
{
	auto result = nlohmann::json::array();
	if (SupportsSunlitGround()) for (const auto& plot : mSunlitPlots) {
		if (plot.row >= 0) result.push_back({{"row", plot.row}, {"column", plot.column}, {"elapsed", plot.elapsed}});
	}
	return result;
}

void Board::LoadSunlitGround(const nlohmann::json& data)
{
	InitializeSunlitGround();
	if (!SupportsSunlitGround() || !data.is_array()) return;
	for (auto& plot : mSunlitPlots) {
		if (plot.row < 0) continue;
		for (const auto& item : data) {
			if (!item.is_object() || !item.contains("row") || !item["row"].is_number_integer()
				|| !item.contains("column") || !item["column"].is_number_integer()
				|| item["row"] != plot.row || item["column"] != plot.column
				|| !item.contains("elapsed") || !item["elapsed"].is_number()) continue;
			const float elapsed = item["elapsed"].get<float>();
			// 不接受已结算的阈值，损坏进度不能在读档时额外补发一颗阳光。
			if (std::isfinite(elapsed) && elapsed >= 0.0f && elapsed < SunlitGround::ProductionSeconds)
				plot.elapsed = elapsed;
			break;
		}
	}
}

/** 在实体下方画低矮野花；花瓣随正式生产进度张开，暂停与读档直接保持相同形态。 */
void Board::DrawSunlitGround(Graphics* g)
{
	if (!SupportsSunlitGround()) return;
	const auto& resources = ResourceManager::GetInstance();
	const Texture* flowers[] = {
		resources.GetTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_FLOWER1, false),
		resources.GetTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_FLOWER2, false),
		resources.GetTexture(ResourceKeys::Textures::IMAGE_SELECTORSCREEN_FLOWER3, false),
	};
	const std::array<Vector, 7> offsets = {{{-24, -16}, {18, -22}, {0, -5}, {-25, 21}, {25, 18}, {-7, 32}, {13, 38}}};
	for (const auto& plot : mSunlitPlots) {
		if (plot.row < 0) continue;
		const Vector center = GetCellCenterPosition(plot.row, plot.column);
		const bool trampled = IsSunlitPlotTrampled(plot);
		const bool occupied = IsSunlitPlotOccupied(plot);
		const float progress = std::clamp(plot.elapsed / SunlitGround::ProductionSeconds, 0.0f, 1.0f);
		// 低透明度暖光衬出格位，主体仍是带明暗纹理的野花，不覆盖原草坪或占格轮廓。
		g->PushTransform();
		g->Translate(center.x, center.y + 12.0f);
		g->Scale(1.0f, 1.16f);
		for (int radius = 36; radius >= 12; radius -= 4)
			g->FillCircle(0.0f, 0.0f, static_cast<float>(radius), glm::vec4(246, 218, 105, occupied ? 3 : 5));
		g->PopTransform();
		for (std::size_t i = 0; i < offsets.size(); ++i) {
			const float bloom = std::clamp(progress * 1.5f - static_cast<float>(i) * 0.075f, 0.0f, 1.0f);
			const float size = 11.0f + bloom * 12.0f;
			const glm::vec4 tint = trampled ? glm::vec4(154, 129, 85, 190)
				: occupied ? glm::vec4(199, 205, 148, 200) : glm::vec4(225 + 30 * bloom, 235 + 20 * bloom, 170 + 85 * bloom, 255);
			g->DrawTexture(flowers[i % 3], center.x + offsets[i].x - size * 0.5f,
				center.y + offsets[i].y - size * 0.5f, size, size * (trampled ? 0.4f : 1.0f),
				static_cast<float>(i * 47), tint);
		}
	}
}
