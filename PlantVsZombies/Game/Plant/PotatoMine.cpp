#include "PotatoMine.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"
#include "../Zombie/Zombie.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include <algorithm>
#include <climits>

namespace {
	constexpr float kReadyDurationSeconds = 20.0f;	// 地雷从埋下到开始出土所需的基础游戏时间
	constexpr float kBlastRadius = 60.0f;			// 原版 KillAllZombiesInRadius 使用的爆炸半径
	constexpr float kBlastCenterOffsetX = -20.0f;	// 原版爆心相对当前格子中心的水平偏移
	constexpr float kBlastCenterOffsetY = -10.0f;	// C# 植物高 80，而本项目格子高 100，爆心需上移 10 像素

	bool CircleIntersectsBounds(const Vector& center, float radiusSquared,
		const SDL_FRect& bounds)
	{
		const float nearestX = std::clamp(center.x, bounds.x, bounds.x + bounds.w);
		const float nearestY = std::clamp(center.y, bounds.y, bounds.y + bounds.h);
		const float dx = center.x - nearestX;
		const float dy = center.y - nearestY;
		return dx * dx + dy * dy <= radiusSquared;
	}
}

void PotatoMine::SetupPlant()
{
	if (auto shadow = GetShadow()) {
		shadow->SetOffset(Vector(0, 23));
	}

	if (mIsPreview) return;

	GetColliderComponent()->SetCollisionEnterCallback(
		[this](ColliderComponent* other) {
		if (!mIsRise) return;

		auto* gameObject = other->GetGameObject();
		if (gameObject->GetObjectType() == ObjectType::OBJECT_ZOMBIE)
		{
			if (auto zombie = dynamic_cast<Zombie*>(gameObject))
			{
				if (!this->mIsBoom && !zombie->IsMindControlled() && zombie->HasHead()
					&& !zombie->IsTangleKelpTarget() && zombie->CanTriggerPotatoMine()) {
					Detonate();
				}
			}
		}
			});
}

void PotatoMine::PlantUpdate()
{
	// 雨水只加速准备成长；武装后的目标扫描仍每个正式游戏逻辑步执行。
	mReadyTimer += GetWeatherActionDeltaTime();
	if (mReadyTimer >= kReadyDurationSeconds && !mIsRise) {
		mIsRise = true;
		Ready(false);
	}

	// C# PotatoArmed 每次 Update 都会 FindTargetZombie；不能只等首次碰撞进入。
	if (mIsRise && !mIsBoom && (mEaterCount > 0 || HasTriggeringZombieInBlastRadius())) {
		Detonate();
	}
}

void PotatoMine::OnZombieBite(const Vector&)
{
	if (mIsRise && !mIsBoom) Detonate();
}

void PotatoMine::TakeDamage(int damage, DamageSource source)
{
	if (mIsBoom) return;
	Plant::TakeDamage(damage, source);
}

void PotatoMine::Detonate()
{
	if (mIsBoom || !mBoard) return;

	// 先锁住单次触发，再按 C# DoSpecial 的范围伤害、粒子、震屏、立即死亡顺序提交。
	mIsBoom = true;
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_POTATO_MINE, 0.4f);
	KillZombiesInBlastRadius();
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("PotatoMine", GetPosition());
	}
	mBoard->ShakeBoard(3.0f, -4.0f);
	Die();
}

bool PotatoMine::HasTriggeringZombieInBlastRadius()
{
	if (!mBoard) return false;
	const Vector blastCenter = GetPosition() + Vector(kBlastCenterOffsetX, kBlastCenterOffsetY);
	const float radiusSquared = kBlastRadius * kBlastRadius;
	bool found = false;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (found || zombie->IsMindControlled() || zombie->IsDying() || !zombie->HasHead()
			|| zombie->IsTangleKelpTarget() || !zombie->CanTriggerPotatoMine()) return;
		const auto* collider = zombie->GetColliderComponent();
		if (collider && collider->mEnabled
			&& CircleIntersectsBounds(blastCenter, radiusSquared, collider->GetBoundingBox())) {
			found = true;
		}
	});
	return found;
}

void PotatoMine::KillZombiesInBlastRadius()
{
	std::vector<int> targets;
	const Vector blastCenter = GetPosition() + Vector(kBlastCenterOffsetX, kBlastCenterOffsetY);
	const float radiusSquared = kBlastRadius * kBlastRadius;

	// 镜像原版 KillAllZombiesInRadius：同排、圆形爆区，一次结算全部目标而非只杀碰撞触发者。
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (mBoard->MineBlocksSegment(blastCenter, zombie->GetPosition())
			|| zombie->IsMindControlled() || zombie->IsDying()
			|| !zombie->CanBeTargetedByProjectile(false)) return;
		auto* collider = zombie->GetColliderComponent();
		if (!collider || !collider->mEnabled) return;

		if (CircleIntersectsBounds(blastCenter, radiusSquared, collider->GetBoundingBox())) {
			// 土豆雷仍对普通目标一击化灰；特殊目标可拒绝直杀并承受受限灰烬伤害。
			targets.push_back(zombie->mZombieID);
		}
		});
	mBoard->ApplyPlantAshAttack(targets, [](Zombie* z) { z->TakePlantAshDamage(1800); });
}

void PotatoMine::SaveExtraData(nlohmann::json& j) const
{
	j["readyTimer"] = mReadyTimer;
	j["isRise"] = mIsRise;
	j["isBoom"] = mIsBoom;
}

void PotatoMine::LoadExtraData(const nlohmann::json& j)
{
	mReadyTimer = j.value("readyTimer", 0.0f);
	mIsRise = j.value("isRise", false);
	mIsBoom = j.value("isBoom", false);

	// 旧版允许把两秒 mashed 占格实体写进存档；新版加载时直接清掉，恢复原版可种格。
	if (mIsBoom) {
		Die();
		return;
	}
	if (mIsRise) {
		Ready(true);
	}
}

void PotatoMine::Ready(bool quick)
{
	if (!quick)
		PlayTrackOnce("anim_rise", "anim_armed");
	else
		PlayTrack("anim_armed");
}
