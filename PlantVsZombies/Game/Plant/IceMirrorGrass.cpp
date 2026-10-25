#include "IceMirrorGrass.h"

#include "../../DeltaTime.h"
#include "../../GameApp.h"
#include "../../ResourceKeys.h"
#include "../AudioSystem.h"
#include "../ShadowComponent.h"

#include <algorithm>
#include <array>

namespace {
constexpr int kMaximumMirrors = 2; // 单株同时维持的镜片上限
constexpr float kMirrorFormationSeconds = 12.0f; // 每凝结一面镜片所需的游戏秒
constexpr float kMirrorOffsetX = 22.0f; // 前后镜片相对植物视觉原点的横向距离，单位 px
constexpr float kMirrorOffsetY = -24.0f; // 镜片相对植物视觉原点的纵向距离，单位 px

/** 绘制不规则冰晶镜片：分面轮廓代替占位圆，并用内切面与双高光表达玻璃厚度。 */
void DrawIceMirrorPane(Graphics* g, const Vector& center, float direction)
{
	if (!g) return;
	const std::array<Vector, 7> kPaneShape = {
		Vector(-11.0f, -12.0f), Vector(3.0f, -17.0f), Vector(14.0f, -8.0f),
		Vector(15.0f, 5.0f), Vector(6.0f, 16.0f), Vector(-8.0f, 13.0f),
		Vector(-15.0f, 1.0f),
	};
	std::array<Vector, kPaneShape.size()> outer{};
	std::array<Vector, kPaneShape.size()> inner{};
	for (size_t i = 0; i < kPaneShape.size(); ++i) {
		outer[i] = center + Vector(kPaneShape[i].x * direction, kPaneShape[i].y);
		inner[i] = center + Vector(kPaneShape[i].x * direction * 0.72f,
			kPaneShape[i].y * 0.72f);
	}
	// Graphics 没有通用多边形填充；按水平扫描线填满实际七边轮廓，避免退回圆形底板。
	const glm::vec4 glassFill(72.0f, 190.0f, 232.0f, 72.0f);
	for (int scan = -17; scan <= 15; ++scan) {
		const float scanY = center.y + static_cast<float>(scan) + 0.5f;
		std::array<float, kPaneShape.size()> intersections{};
		size_t intersectionCount = 0;
		for (size_t i = 0; i < outer.size(); ++i) {
			const Vector& from = outer[i];
			const Vector& to = outer[(i + 1) % outer.size()];
			if ((from.y <= scanY && to.y > scanY)
				|| (to.y <= scanY && from.y > scanY)) {
				const float ratio = (scanY - from.y) / (to.y - from.y);
				intersections[intersectionCount++] = from.x + (to.x - from.x) * ratio;
			}
		}
		if (intersectionCount >= 2) {
			std::sort(intersections.begin(), intersections.begin() + intersectionCount);
			g->FillRect(intersections.front(), center.y + static_cast<float>(scan),
				intersections[intersectionCount - 1] - intersections.front(), 1.2f,
				glassFill);
		}
	}
	const glm::vec4 glow(67.0f, 191.0f, 245.0f, 105.0f);
	const glm::vec4 edge(218.0f, 251.0f, 255.0f, 235.0f);
	const glm::vec4 innerEdge(112.0f, 221.0f, 255.0f, 155.0f);
	for (size_t i = 0; i < outer.size(); ++i) {
		const Vector& from = outer[i];
		const Vector& to = outer[(i + 1) % outer.size()];
		g->DrawLine(from.x - direction, from.y, to.x - direction, to.y, glow);
		g->DrawLine(from.x, from.y, to.x, to.y, edge);
		g->DrawLine(inner[i].x, inner[i].y,
			inner[(i + 1) % inner.size()].x, inner[(i + 1) % inner.size()].y,
			innerEdge);
	}
	const Vector facetCenter = center + Vector(-2.0f * direction, -1.0f);
	for (size_t index : { size_t(0), size_t(2), size_t(4), size_t(6) }) {
		g->DrawLine(facetCenter.x, facetCenter.y,
			inner[index].x, inner[index].y, innerEdge);
	}
	const glm::vec4 highlight(255.0f, 255.0f, 255.0f, 205.0f);
	g->DrawLine(center.x - 7.0f * direction, center.y + 8.0f,
		center.x + 7.0f * direction, center.y - 10.0f, highlight);
	g->DrawLine(center.x - 3.0f * direction, center.y + 10.0f,
		center.x + 9.0f * direction, center.y - 5.0f,
		glm::vec4(255.0f, 255.0f, 255.0f, 110.0f));
}
}

void IceMirrorGrass::SetupPlant()
{
	mPlantHealth = 300;
	mPlantMaxHealth = 300;
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(0.0f, 29.0f));
		shadow->SetScale(Vector(0.78f, 0.55f));
	}
}

/** 按土壤倍率形成镜片，达到容量后停止积累。 */
void IceMirrorGrass::PlantUpdate()
{
	if (mIsPreview || mMirrorCount >= kMaximumMirrors) return;
	mFormationProgress += DeltaTime::GetDeltaTime() * GetIndependentActionSpeedMultiplier();
	while (mFormationProgress >= kMirrorFormationSeconds
		&& mMirrorCount < kMaximumMirrors) {
		mFormationProgress -= kMirrorFormationSeconds;
		++mMirrorCount;
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CERAMIC, 0.22f);
	}
	if (mMirrorCount >= kMaximumMirrors) mFormationProgress = 0.0f;
}

bool IceMirrorGrass::TryInterceptHostileStraightProjectile(
	float velocityX, const Vector& impactPosition)
{
	if (mMirrorCount <= 0 || !IsActive() || mIsPreview || IsSquished()) return false;
	--mMirrorCount;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CERAMIC, 0.42f);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("IceMirrorBreak",
			impactPosition + Vector(velocityX < 0.0f ? 12.0f : -12.0f, 0.0f));
	}
	return true;
}

void IceMirrorGrass::Draw(Graphics* g)
{
	Plant::Draw(g);
	if (!g) return;
	const Vector center = GetVisualAnchorPosition()
		+ Vector(0.0f, kMirrorOffsetY);
	for (int i = 0; i < mMirrorCount; ++i) {
		const float direction = i == 0 ? 1.0f : -1.0f;
		const float x = center.x + direction * kMirrorOffsetX;
		DrawIceMirrorPane(g, Vector(x, center.y), direction);
	}
}

void IceMirrorGrass::SaveExtraData(nlohmann::json& j) const
{
	j["mirrorCount"] = mMirrorCount;
	j["formationProgress"] = mFormationProgress;
}

void IceMirrorGrass::LoadExtraData(const nlohmann::json& j)
{
	mMirrorCount = std::clamp(j.value("mirrorCount", 0), 0, kMaximumMirrors);
	mFormationProgress = mMirrorCount >= kMaximumMirrors ? 0.0f
		: std::clamp(j.value("formationProgress", 0.0f),
			0.0f, kMirrorFormationSeconds);
}
