#include "MagnetShroom.h"
#include "../../GameApp.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../Zombie/Zombie.h"
#include "../../Graphics.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
	constexpr float kReanimationFps = 12.0f;                  // Magnetshroom.reanim 基础帧率
	constexpr float kIdleMinFps = 10.0f;                      // C# 待机随机帧率下界
	constexpr float kIdleMaxFps = 15.0f;                      // C# 待机随机帧率上界
	constexpr float kShootingFps = 12.0f;                     // C# anim_shooting 播放帧率
	constexpr float kChargingFps = 2.0f;                      // C# anim_nonactive_idle2 充能帧率
	constexpr float kBaseRechargeSeconds = MagnetShroom::ForecastRecharge;             // C# mStateCountdown=1500cs，从吸取当帧开始计时
	constexpr int kTargetRowRadius = MagnetShroom::ForecastRowRadius;                       // 原版最多搜索上下各两行
	constexpr float kNormalRadiusInCells = MagnetShroom::ForecastRadiusCells;            // 原版 270px / 80px 格宽
	constexpr float kEatingRadiusInCells = MagnetShroom::ForecastEatingRadiusCells;              // 原版啃食目标 320px / 80px 格宽
	constexpr float kRowDistancePenaltyInCells = MagnetShroom::ForecastRowPenaltyCells;        // 每跨一行额外增加一格宽的选靶代价
	constexpr float kArrivalDistance = 20.0f;                 // 离体物进入磁力菇周围后停止插值的距离
	constexpr float kAttractionPerFixedStep = 0.05f;          // C# 每逻辑步移动剩余距离的 5%
	constexpr float kShadowScale = 0.72f;                     // 磁力菇脚底影子相对默认贴图的缩放
	constexpr float kShadowOffsetY = 32.0f;                   // 影子相对格中心的垂直偏移，单位 px
	constexpr float kMagnetSoundVolume = 0.5f;                // 原版磁吸音效播放音量

	bool CircleOverlapsRect(const Vector& center, float radius, const SDL_FRect& bounds)
	{
		const float nearestX = std::clamp(center.x, bounds.x, bounds.x + bounds.w);
		const float nearestY = std::clamp(center.y, bounds.y, bounds.y + bounds.h);
		const float dx = center.x - nearestX;
		const float dy = center.y - nearestY;
		return dx * dx + dy * dy <= radius * radius;
	}
}

const char* MagnetShroom::GetPhaseName() const
{
	switch (mPhase) {
	case Phase::READY: return "READY";
	case Phase::SUCKING: return "SUCKING";
	case Phase::CHARGING: return "CHARGING";
	}
	return "READY";
}

float MagnetShroom::GetRechargeSeconds() const
{
	return kBaseRechargeSeconds;
}

void MagnetShroom::SetupPlant()
{
	if (auto* shadow = GetShadow()) {
		shadow->SetScale(Vector(kShadowScale, kShadowScale));
		shadow->SetOffset(Vector(0.0f, kShadowOffsetY));
	}

	Shroom::SetupPlant();
	SetAnimationSpeed(GameRandom::Range(
		kIdleMinFps / kReanimationFps, kIdleMaxFps / kReanimationFps));
	mPhase = Phase::READY;
	mRechargeTime = 0.0f;
	mHasCapturedItem = false;
	if (!mIsSleeping) PlayTrack(GetAwakeIdleTrackName());
}

void MagnetShroom::PlantUpdate()
{
	UpdateCapturedItem();
	if (mPhase == Phase::READY) {
		TryStartMagnetizing();
		return;
	}

	// 原版在吸取开始当帧就设置 1500cs，因此射击动画时间也属于总充能时间。
	mRechargeTime = std::max(0.0f, mRechargeTime
		- DeltaTime::GetDeltaTime() * GetSkillSpeedMultiplier());
	if (mPhase == Phase::SUCKING
		&& GetCurrentTrackName() == GetChargingTrackName()) {
		mPhase = Phase::CHARGING;
	}
	if (mPhase == Phase::CHARGING && mRechargeTime <= 0.0f) {
		FinishRecharge();
	}
}

bool MagnetShroom::TryStartMagnetizing()
{
	if (!mBoard) return false;
	Zombie* nearest = nullptr;
	float nearestScore = std::numeric_limits<float>::max();
	const Vector center = GetPosition();
	const int firstRow = std::max(0, mRow - kTargetRowRadius);
	const int lastRow = std::min(mBoard->mRows - 1, mRow + kTargetRowRadius);

	for (int row = firstRow; row <= lastRow; ++row) {
		mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (!zombie || !zombie->CanBeTargetedByMagnetShroom()) return;
			const ColliderComponent* collider = zombie->GetColliderComponent();
			// 地下矿工关闭常规碰撞体门禁，但 C# 仍允许磁力菇吸镐；包围盒只用于量距。
			if (!collider) return;
			const SDL_FRect bounds = collider->GetBoundingBox();
			if (bounds.x > SCENE_WIDTH) return;
			const float radiusCells = zombie->IsEating()
				? kEatingRadiusInCells : kNormalRadiusInCells;
			const float radius = radiusCells * CELL_COLLIDER_SIZE_X;
			if (!CircleOverlapsRect(center, radius, bounds)) return;

			const Vector target(bounds.x + bounds.w * 0.5f,
				bounds.y + bounds.h * 0.5f);
			const float rowPenalty = static_cast<float>(std::abs(row - mRow))
				* CELL_COLLIDER_SIZE_X * kRowDistancePenaltyInCells;
			const float score = (target - center).magnitude() + rowPenalty;
			if (score < nearestScore) {
				nearestScore = score;
				nearest = zombie;
			}
		});
	}

	MagneticItem item;
	if (nearest) {
		const MagneticSimulationLayer extractedLayer = nearest->GetMagneticSimulationLayer();
		const int targetRow = nearest->mRow;
		const ColliderComponent* collider = nearest->GetColliderComponent();
		Vector targetCenter = nearest->GetPosition();
		if (collider) {
			const SDL_FRect targetBounds = collider->GetBoundingBox();
			targetCenter = Vector(
				targetBounds.x + targetBounds.w * 0.5f,
				targetBounds.y + targetBounds.h * 0.5f);
		}
		if (nearest->ExtractMagneticItem(item)) {
			mBoard->MarkTemporalTargetEquipmentExtracted(nearest->mZombieID,
				extractedLayer == MagneticSimulationLayer::SHIELD);
			const int backlashDamage = item.extractorSelfDamage;
			BeginMagnetizing(std::move(item));
			OnZombieMagneticItemExtracted(mCapturedItem, targetCenter, targetRow);
			ApplyExtractionBacklash(backlashDamage);
			return true;
		}
	}

	// C# 仅在附近没有可吸僵尸装备时，按两格 Chebyshev 距离搜索场景扶梯。
	if (mBoard->ExtractNearestLadderForMagnet(mRow, mColumn, item)) {
		BeginMagnetizing(std::move(item));
		return true;
	}
	return false;
}

void MagnetShroom::BeginMagnetizing(MagneticItem item)
{
	mPhase = Phase::SUCKING;
	mRechargeTime = GetRechargeSeconds();
	mHasCapturedItem = true;
	mCapturedItem = std::move(item);
	const float attackSpeed = GetSkillSpeedMultiplier();
	PlayTrackOnce(GetShootingTrackName(), GetChargingTrackName(),
		kShootingFps / kReanimationFps * attackSpeed, 0.0f,
		kChargingFps / kReanimationFps * attackSpeed, 0.0f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_MAGNETSHROOM,
		kMagnetSoundVolume);
}

void MagnetShroom::ApplyExtractionBacklash(int damage)
{
	if (damage <= 0 || !IsActive()) return;
	mPlantHealth = std::max(0, mPlantHealth - damage);
	SetGlowingTimer(0.1f);
	if (mPlantHealth <= 0) Die();
}

void MagnetShroom::UpdateCapturedItem()
{
	if (!mHasCapturedItem) return;
	const Vector delta = GetCapturedItemDestination() - mCapturedItem.worldPosition;
	const float distance = delta.magnitude();
	if (distance < kArrivalDistance) return;

	const float fixedStep = DeltaTime::GetFixedStep();
	const float steps = fixedStep > 0.0f
		? DeltaTime::GetDeltaTime() / fixedStep : 1.0f;
	const float factor = 1.0f - std::pow(
		1.0f - kAttractionPerFixedStep, std::max(0.0f, steps));
	mCapturedItem.worldPosition += delta * factor;
}

void MagnetShroom::FinishRecharge()
{
	mPhase = Phase::READY;
	mRechargeTime = 0.0f;
	mHasCapturedItem = false;
	mCapturedItem = MagneticItem{};
	SetAnimationSpeed(GameRandom::Range(
		kIdleMinFps / kReanimationFps, kIdleMaxFps / kReanimationFps));
	PlayTrack(GetAwakeIdleTrackName(), 0.0f, 0.0f);
}

Vector MagnetShroom::GetCapturedItemDestination() const
{
	Vector destination = GetVisualPosition() + mCapturedItem.destinationOffset;
	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		mCapturedItem.textureKey, false);
	if (!texture) return destination;

	// C# 的 MagnetItem 目标偏移指向未缩放贴图左上角；本项目用中心点绘制，
	// 必须按各装备的实际缩放尺寸转换，不能用统一补偿冒充锚点换算。
	destination.x += static_cast<float>(texture->width)
		* mCapturedItem.drawScale * 0.5f;
	destination.y += static_cast<float>(texture->height)
		* mCapturedItem.drawScale * 0.5f;
	return destination;
}

float MagnetShroom::GetCapturedItemDistance() const
{
	return mHasCapturedItem
		? (GetCapturedItemDestination() - mCapturedItem.worldPosition).magnitude()
		: 0.0f;
}

void MagnetShroom::DrawCapturedItem(Graphics* g) const
{
	if (!g || !mHasCapturedItem || mCapturedItem.textureKey.empty()) return;
	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		mCapturedItem.textureKey, false);
	if (!texture) return;
	const float width = static_cast<float>(texture->width) * mCapturedItem.drawScale;
	const float height = static_cast<float>(texture->height) * mCapturedItem.drawScale;
	g->DrawTexture(texture,
		mCapturedItem.worldPosition.x - width * 0.5f,
		mCapturedItem.worldPosition.y - height * 0.5f,
		width, height);
}

void MagnetShroom::Draw(Graphics* g)
{
	Plant::Draw(g);
	DrawCapturedItem(g);
}

void MagnetShroom::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["rechargeTime"] = mRechargeTime;
	j["hasCapturedItem"] = mHasCapturedItem;
	if (!mHasCapturedItem) return;
	j["capturedTextureKey"] = mCapturedItem.textureKey;
	j["capturedX"] = mCapturedItem.worldPosition.x;
	j["capturedY"] = mCapturedItem.worldPosition.y;
	j["capturedDestX"] = mCapturedItem.destinationOffset.x;
	j["capturedDestY"] = mCapturedItem.destinationOffset.y;
	j["capturedScale"] = mCapturedItem.drawScale;
}

void MagnetShroom::LoadExtraData(const nlohmann::json& j)
{
	const int phase = std::clamp(j.value("phase", 0), 0,
		static_cast<int>(Phase::CHARGING));
	mPhase = static_cast<Phase>(phase);
	mRechargeTime = std::clamp(j.value("rechargeTime", 0.0f),
		0.0f, GetRechargeSeconds());
	mHasCapturedItem = j.value("hasCapturedItem", false);
	if (mHasCapturedItem) {
		mCapturedItem.textureKey = j.value("capturedTextureKey", std::string{});
		mCapturedItem.worldPosition = Vector(
			j.value("capturedX", GetVisualPosition().x),
			j.value("capturedY", GetVisualPosition().y));
		mCapturedItem.destinationOffset = Vector(
			j.value("capturedDestX", 0.0f),
			j.value("capturedDestY", 0.0f));
		mCapturedItem.drawScale = std::clamp(
			j.value("capturedScale", 0.8f), 0.1f, 2.0f);
	}
	if (mPhase == Phase::READY || !mHasCapturedItem) {
		mPhase = Phase::READY;
		mRechargeTime = 0.0f;
		mHasCapturedItem = false;
		mCapturedItem = MagneticItem{};
	}
}
