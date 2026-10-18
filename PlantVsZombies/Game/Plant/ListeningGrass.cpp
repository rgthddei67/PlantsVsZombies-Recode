#include "ListeningGrass.h"

#include "../../DeltaTime.h"
#include "../../GameApp.h"
#include "../../ResourceKeys.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../Zombie/Zombie.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr float kListenCooldownSeconds = 6.0f; // 迫出与封穴共享的内部冷却，单位游戏秒
constexpr float kResponseClipSpeed = 22.0f / 12.0f; // 原版 Umbrellaleaf anim_block 播放倍率
constexpr float kIdleClipSpeed = 1.0f; // 响应轨结束后待机轨的原生播放倍率
constexpr float kResponseSoundVolume = 0.28f; // 听雪成功时既有提示音的音量
constexpr float kDistanceTieEpsilon = 0.001f; // 水平位置视为并列的浮点容差，单位 px
constexpr float kShadowOffsetX = 0.0f; // 阴影相对格中心的水平偏移，单位 px
constexpr float kShadowOffsetY = 28.0f; // 阴影相对格中心的垂直偏移，单位 px
constexpr float kShadowScaleX = 0.72f; // 低矮叶丛阴影的水平倍率
constexpr float kShadowScaleY = 0.52f; // 低矮叶丛阴影的垂直倍率

struct SurfaceCandidate {
	float x = 0.0f;
	int zombieID = NULL_ZOMBIE_ID;
};
} // namespace

void ListeningGrass::SetupPlant()
{
	mPlantHealth = 300;
	mPlantMaxHealth = 300;
	if (auto* shadow = GetShadow()) {
		shadow->SetOffset(Vector(kShadowOffsetX, kShadowOffsetY));
		shadow->SetScale(Vector(kShadowScaleX, kShadowScaleY));
	}
}

/** 按肥沃倍率恢复监听能力，再处理可用目标。 */
void ListeningGrass::PlantUpdate()
{
	if (mIsPreview || !mBoard) return;
	if (mListenCooldownRemaining > 0.0f) {
		mListenCooldownRemaining = std::max(0.0f,
			mListenCooldownRemaining - DeltaTime::GetDeltaTime() * GetFertilitySpeedMultiplier());
		return;
	}

	if (TryForceSurfaceOne()) return;
	TrySealSnowHole();
}

void ListeningGrass::SaveExtraData(nlohmann::json& j) const
{
	j["listenCooldownRemaining"] = mListenCooldownRemaining;
}

void ListeningGrass::LoadExtraData(const nlohmann::json& j)
{
	const float saved = j.value("listenCooldownRemaining", 0.0f);
	mListenCooldownRemaining = std::isfinite(saved)
		? std::clamp(saved, 0.0f, kListenCooldownSeconds) : 0.0f;
}

bool ListeningGrass::TryForceSurfaceOne()
{
	std::vector<SurfaceCandidate> candidates;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (!zombie || !zombie->IsActive() || zombie->IsDying()
			|| zombie->IsMindControlled()) return;
		candidates.push_back({ zombie->GetPosition().x, zombie->mZombieID });
	});
	std::sort(candidates.begin(), candidates.end(),
		[](const SurfaceCandidate& lhs, const SurfaceCandidate& rhs) {
			if (std::abs(lhs.x - rhs.x) > kDistanceTieEpsilon) return lhs.x < rhs.x;
			return lhs.zombieID < rhs.zombieID;
		});

	for (const SurfaceCandidate& candidate : candidates) {
		Zombie* zombie = mBoard->mEntityRegistry.GetZombie(candidate.zombieID);
		if (!zombie || !zombie->IsActive() || zombie->IsDying()
			|| zombie->IsMindControlled()) continue;
		if (!zombie->ForceSurfaceFromGroundHazard()) continue;

		mListenCooldownRemaining = kListenCooldownSeconds;
		PlayListeningResponse(false, GetVisualAnchorPosition());
		return true;
	}
	return false;
}

bool ListeningGrass::TrySealSnowHole()
{
	const int column = mBoard->GetSnowHoleColumn(mRow);
	if (column < 0) return false;
	const Vector holeCenter = mBoard->GetCellCenterPosition(mRow, column);
	if (!mBoard->SealSnowHole(mRow)) return false;

	mListenCooldownRemaining = kListenCooldownSeconds;
	PlayListeningResponse(true, holeCenter);
	return true;
}

void ListeningGrass::PlayListeningResponse(bool sealedHole,
	const Vector& effectAnchor)
{
	// 玩法已经原子提交；原版 anim_block 只承担竖叶与回弹反馈，不注册新帧事件。
	PlayTrackOnce("anim_block", "anim_idle", kResponseClipSpeed,
		0.0f, kIdleClipSpeed, 0.0f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP,
		kResponseSoundVolume);
	if (!g_particleSystem) return;
	g_particleSystem->EmitEffect(sealedHole
		? "ListeningGrassHoleSeal" : "ListeningGrassPulse", effectAnchor);
}
