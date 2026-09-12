#include "DawnLotus.h"

#include "../../DeltaTime.h"
#include "../../Graphics.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"
#include "Game/Board/Board.h"

#include <algorithm>

namespace {
constexpr int kHealth = 500; // 曙光莲本体生命
constexpr float kMaxEnergy = 60.0f; // 一次组合黎明所需能量
constexpr float kBaseEnergyPerSecond = 2.5f; // 每游戏秒固定恢复能量，平稳天气也能预先蓄能
constexpr float kDangerEnergyPerSecond = 1.0f; // 每项红色危险仪表每游戏秒额外提供的能量
constexpr float kReadyBadgeWidth = 78.0f; // 就绪牌宽度，逻辑 px
constexpr float kReadyBadgeHeight = 24.0f; // 就绪牌高度，逻辑 px
constexpr float kReadyBadgeOffsetY = -78.0f; // 就绪牌顶边相对植物视觉锚点的竖直偏移，逻辑 px
constexpr int kReadyBadgeFontSize = 16; // 点击提示字号
constexpr const char* kFollowerTrack = "anim_idle"; // 睡莲单轨稳定浮动时间轴
constexpr const char* kCrownSlot = "dawn_lotus_crown"; // 黎明花冠命名槽
constexpr float kFollowerOffsetX = 2.0f; // 花冠相对睡莲局部水平偏移，动画 px
constexpr float kFollowerOffsetY = -22.0f; // 花冠相对睡莲局部垂直偏移，动画 px
}

void DawnLotus::SetupPlant()
{
	mPlantHealth = mPlantMaxHealth = kHealth;
	ConfigureRig();
	PlayTrack("anim_idle", 1.0f);
	RefreshPresentation();
}

void DawnLotus::PlantUpdate()
{
	if (mIsPreview || !mBoard || IsShutdown() || mEnergy >= kMaxEnergy) return;
	int dangerousGauges = 0;
	if (mBoard->IsPolarTemperatureDangerous()) ++dangerousGauges;
	if (mBoard->IsPolarHumidityDangerous()) ++dangerousGauges;
	if (mBoard->IsPolarWindDangerous()) ++dangerousGauges;
	mEnergy = std::min(kMaxEnergy,
		mEnergy + DeltaTime::GetDeltaTime()
			* (kBaseEnergyPerSecond + kDangerEnergyPerSecond * dangerousGauges));
	if (mEnergy >= kMaxEnergy && g_particleSystem) {
		g_particleSystem->EmitEffect("DawnLotusReady", GetVisualPosition());
	}
	RefreshPresentation();
}

void DawnLotus::Draw(Graphics* g)
{
	Plant::Draw(g);
	if (!g || !IsReadyToActivate()) return;
	// 提示随植物视觉锚点移动；不写入存档，也不依赖充满时的一次性粒子。
	const Vector anchor = GetVisualAnchorPosition();
	const float x = anchor.x - kReadyBadgeWidth * 0.5f;
	// 最上行靠近卡槽栏，提示顶边限制在棋盘内，避免被 HUD 遮住。
	const float boardTop = mBoard->GetCellCenterPosition(0, mColumn).y
		- mBoard->GetCellHeight() * 0.5f;
	const float y = std::max(boardTop, anchor.y + kReadyBadgeOffsetY);
	const glm::vec4 gold(255.0f, 222.0f, 100.0f, 255.0f);
	g->FillRect(x + 2.0f, y + 2.0f, kReadyBadgeWidth, kReadyBadgeHeight,
		glm::vec4(0.0f, 0.0f, 0.0f, 100.0f));
	g->FillRect(x, y, kReadyBadgeWidth, kReadyBadgeHeight,
		glm::vec4(43.0f, 25.0f, 64.0f, 245.0f));
	g->DrawRect(x, y, kReadyBadgeWidth, kReadyBadgeHeight, gold);
	g->DrawLine(anchor.x - 5.0f, y + kReadyBadgeHeight,
		anchor.x, y + kReadyBadgeHeight + 5.0f, gold);
	g->DrawLine(anchor.x, y + kReadyBadgeHeight + 5.0f,
		anchor.x + 5.0f, y + kReadyBadgeHeight, gold);
	const glm::vec2 size = g->MeasureTextSize(u8"点击释放",
		ResourceKeys::Fonts::FONT_FZCQ, kReadyBadgeFontSize);
	g->DrawText(u8"点击释放", ResourceKeys::Fonts::FONT_FZCQ, kReadyBadgeFontSize,
		gold, anchor.x - size.x * 0.5f, y + (kReadyBadgeHeight - size.y) * 0.5f);
}

int DawnLotus::GetDangerMask() const
{
	if (!mBoard) return 0;
	return (mBoard->IsPolarTemperatureDangerous() ? 1 : 0)
		| (mBoard->IsPolarHumidityDangerous() ? 2 : 0)
		| (mBoard->IsPolarWindDangerous() ? 4 : 0);
}

bool DawnLotus::IsReadyToActivate() const
{
	return IsActive() && !mIsPreview && !IsSquished() && !IsBungeeTargeted()
		&& !IsActionPaused() && IsFullyCharged() && GetDangerMask() != 0;
}

bool DawnLotus::TryActivate()
{
	if (!IsReadyToActivate() || !mBoard->ActivateDawnLotus(mPlantID, GetDangerMask())) return false;
	mEnergy = 0.0f;
	RefreshPresentation();
	return true;
}

void DawnLotus::SaveExtraData(nlohmann::json& j) const
{
	j["energy"] = mEnergy;
}

void DawnLotus::LoadExtraData(const nlohmann::json& j)
{
	mEnergy = std::clamp(j.value("energy", 0.0f), 0.0f, kMaxEnergy);
	ConfigureRig();
	RefreshPresentation();
}

void DawnLotus::ConfigureRig()
{
	if (mRigConfigured || !mAnimator || !mAnimator->HasTrack(kFollowerTrack)) return;
	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_REANIM_DAWNLOTUS_CROWN, false);
	if (!texture) return;
	mAnimator->SetTrackFollowerImage(kFollowerTrack, kCrownSlot, texture,
		kFollowerOffsetX, kFollowerOffsetY, 0.86f, 0.86f, false, true, true);
	mRigConfigured = true;
}

void DawnLotus::RefreshPresentation() const
{
	if (!mRigConfigured || !mAnimator) return;
	mAnimator->SetTrackFollowerVisible(kFollowerTrack, kCrownSlot, IsActive());
}
