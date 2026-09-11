#include "PrismFlower.h"
#include "DeltaTime.h"
#include "Game/Board/Board.h"
#include "Game/Zombie/Zombie.h"
#include "Game/AudioSystem.h"
#include "ResourceManager.h"
#include "Graphics.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
	constexpr int kHealth = 300; // 植物本体生命
	constexpr float kMarkInterval = 8.0f; // 两次成功发动间隔，基础游戏秒
	constexpr float kBloomSeconds = 0.35f; // 瞬时光束与晶瓣展开演出，游戏秒
}

void PrismFlower::SetupPlant()
{
	mPlantHealth = mPlantMaxHealth = kHealth;
	PlayTrack("anim_idle", 1.0f);
	RefreshBloom();
}

void PrismFlower::PlantUpdate()
{
	const float delta = DeltaTime::GetDeltaTime();
	mBloomRemaining = std::max(0.0f, mBloomRemaining - delta);
	mMarkCooldown = std::max(0.0f, mMarkCooldown - delta * GetAttackSpeedMultiplier());
	if (mMarkCooldown > 0.0f) { RefreshBloom(); return; }
	std::vector<Zombie*> candidates;
	const Vector origin = mBoard->GetCellCenterPosition(mRow, mColumn);
	const float left = mBoard->GetCellCenterPosition(0, 0).x - CELL_COLLIDER_SIZE_X * 0.5f;
	for (int row = std::max(0, mRow - 1); row <= std::min(mBoard->mRows - 1, mRow + 2); ++row) {
		mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (!zombie || !zombie->IsActive() || zombie->IsDying() || zombie->IsMindControlled()
				|| (!zombie->CanBeTargetedByProjectile(false) && !zombie->CanBeTargetedByProjectile(true))) return;
			const Vector p = zombie->GetPosition();
			const int col = static_cast<int>(std::floor((p.x - left) / CELL_COLLIDER_SIZE_X));
			if (col < 0 || col >= mBoard->mColumns || !CoversCell(row - mRow, col - mColumn)
				|| mBoard->MineBlocksSegment(origin, p)) return;
			candidates.push_back(zombie);
		});
	}
	std::sort(candidates.begin(), candidates.end(), [](const Zombie* a, const Zombie* b) {
		const bool markedA = a->GetPrismMarkRemaining() > 0.0f;
		const bool markedB = b->GetPrismMarkRemaining() > 0.0f;
		if (markedA != markedB) return !markedA;
		const long long hpA = static_cast<long long>(a->mBodyHealth) + a->mHelmHealth + a->mShieldHealth;
		const long long hpB = static_cast<long long>(b->mBodyHealth) + b->mHelmHealth + b->mShieldHealth;
		if (hpA != hpB) return hpA > hpB;
		if (a->GetPosition().x != b->GetPosition().x) return a->GetPosition().x < b->GetPosition().x;
		return a->mZombieID < b->mZombieID;
	});
	mLastMarkCount = std::min(2, static_cast<int>(candidates.size()));
	for (int i = 0; i < mLastMarkCount; ++i) {
		candidates[i]->ApplyPrismMark();
		mBeamEnds[i] = candidates[i]->GetButterSplatAnchor();
	}
	if (mLastMarkCount > 0) {
		mMarkCooldown = kMarkInterval;
		mBloomRemaining = kBloomSeconds;
		AudioSystem::PlaySound("SOUND_BLEEP", 0.25f);
	}
	RefreshBloom();
}

void PrismFlower::RefreshBloom()
{
	if (!mAnimator) return;
	const bool ready = mMarkCooldown <= 0.0f || mBloomRemaining > 0.0f;
	mAnimator->SetTrackColor("prism_part6", ready ? SDL_Color{255,255,255,255} : SDL_Color{180,190,200,255});
	const float opening = std::sin(std::clamp(mBloomRemaining / kBloomSeconds, 0.0f, 1.0f) * 3.14159265f);
	for (int part = 0; part < 6; ++part) {
		const float angle = part * 1.04719755f - 1.57079633f;
		mAnimator->SetTrackOffset("prism_part" + std::to_string(part),
			std::cos(angle) * opening * 3.0f, std::sin(angle) * opening * 3.0f);
	}
}

void PrismFlower::Draw(Graphics* g)
{
	Plant::Draw(g);
	if (!g || mIsPreview || mBloomRemaining <= 0.0f) return;
	const Vector source = GetVisualPosition() + Vector(38.0f, 30.0f);
	const float alpha = 220.0f * mBloomRemaining / kBloomSeconds;
	for (int i = 0; i < mLastMarkCount; ++i) {
		for (int edge = -2; edge <= 2; ++edge) {
			g->DrawLine(source.x, source.y + edge, mBeamEnds[i].x, mBeamEnds[i].y + edge,
				glm::vec4(255, edge == 0 ? 250 : 205, edge == 0 ? 215 : 90, alpha / (std::abs(edge) + 1)));
		}
	}
}

void PrismFlower::SaveExtraData(nlohmann::json& j) const
{
	j["markCooldown"] = mMarkCooldown;
}

void PrismFlower::LoadExtraData(const nlohmann::json& j)
{
	mMarkCooldown = std::clamp(j.value("markCooldown", 0.0f), 0.0f, kMarkInterval);
	mBloomRemaining = 0.0f; // 读档不重播已经提交的瞬时光束。
	RefreshBloom();
}
