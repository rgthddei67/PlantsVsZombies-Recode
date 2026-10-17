#include "StarFruit.h"
#include "GameApp.h"

#include "Game/Board/Board.h"
#include "../Bullet/Bullet.h"
#include "../ShadowComponent.h"
#include "../Zombie/Zombie.h"

#include <SDL2/SDL.h>
#include <array>
#include <cmath>
#include <limits>

namespace {
	constexpr int kFireFrame = 27;                         // 主人给出的 AddFrameEvent 真实帧号，代码直接使用
	constexpr float kReanimFramesPerSecond = 12.0f;        // Starfruit.reanim 的基础帧率
	constexpr float kShootFramesPerSecond = 28.0f;         // C# PlayBodyReanim(anim_shoot) 的播放帧率
	constexpr float kIdleFramesPerSecondMin = 10.0f;       // C# 普通整株 reanim 待机随机帧率下限
	constexpr float kIdleFramesPerSecondMax = 15.0f;       // C# 普通整株 reanim 待机随机帧率上限
	constexpr float kShootBlendSeconds = 0.2f;             // C# StartBlend(20) 的进入与返回混合时间
	constexpr float kInitialShootInterval = 1.5f;          // C# Starfruit launch rate 150 厘秒
	constexpr float kRepeatShootIntervalMin = 1.36f;       // 150-NextNumber(15) 的最短后续周期
	constexpr float kRepeatShootIntervalMax = 1.5f;        // 150-NextNumber(15) 的最长后续周期
	constexpr float kBulletOffsetX = -15.0f;               // C# mX+25 换算到本项目格子中心后的发射 X
	constexpr float kBulletOffsetY = -25.0f;               // C# mY+25 换算到本项目格子中心后的发射 Y
	constexpr float kStarSpeed = 333.0f;                   // 原版 3.33px/厘秒换算后的星弹速度，单位 px/s
	constexpr float kDiagonalDegrees = 30.0f;              // 两条向右斜线与水平线的夹角
	constexpr float kPi = 3.14159265358979323846f;         // 角度与弧度换算常量
	constexpr int kStarDamage = 20;                        // 原版单颗星弹基础伤害

	/** 发射与索敌共用同一组速度，避免两处独立维护角度或方向。 */
	const std::array<Vector, 5>& StarVelocities()
	{
		static const std::array<Vector, 5> velocities = [] {
			const float radians = kDiagonalDegrees * kPi / 180.0f;
			const float x = std::cos(radians) * kStarSpeed;
			const float y = std::sin(radians) * kStarSpeed;
			return std::array<Vector, 5>{Vector(-kStarSpeed, 0), Vector(0, kStarSpeed),
				Vector(0, -kStarSpeed), Vector(x, y), Vector(x, -y)};
		}();
		return velocities;
	}

	/** 用一维相对运动收紧候选接触时间；零速度轴只检查是否已经重叠。 */
	bool ClipContactTime(float origin, float speed, float low, float high,
		float& begin, float& end)
	{
		if (speed == 0.0f) return origin >= low && origin <= high;
		float a = (low - origin) / speed;
		float b = (high - origin) / speed;
		if (a > b) std::swap(a, b);
		begin = std::max(begin, a);
		end = std::min(end, b);
		return begin <= end;
	}

	/** 有限弹道只查到离开场景或撞屋顶；每轮每个方向计算一次，不按僵尸重复追踪。 */
	float StarFlightDuration(const Board& board, const Vector& origin,
		const Vector& velocity, int sourceRow, float step)
	{
		float begin = 0.0f;
		float end = std::numeric_limits<float>::max();
		if (!ClipContactTime(origin.x, velocity.x, -10.0f, SCENE_WIDTH + 20.0f, begin, end)
			|| !ClipContactTime(origin.y, velocity.y, 0.0f, SCENE_HEIGHT, begin, end)) return 0.0f;
		if (board.IsRoofBackground()) {
			for (int index = 1; index * step <= end; ++index) {
				const float time = index * step;
				const Vector position = origin + velocity * time;
				const int row = Bullet::GetStarRow(board, position, sourceRow, velocity.y);
				if (Bullet::StarHitsRoof(board, position, row)) return time - step;
			}
		}
		return end;
	}

	/** 先用相对运动裁掉必不相交的时间，再按正式逻辑步核对圆形碰撞和所属行。 */
	bool StarCanHit(const Board& board, const Vector& origin, const Vector& velocity,
		int sourceRow, const Zombie& zombie, const ColliderComponent& collider,
		float targetVelocityX, float launchDelay, float duration, float step)
	{
		constexpr float radius = Bullet::DefaultColliderDiameter * 0.5f;
		const SDL_FRect bounds = collider.GetBoundingBox();
		float begin = step;
		float end = duration;
		if (!ClipContactTime(origin.x, velocity.x - targetVelocityX,
			bounds.x + targetVelocityX * launchDelay - radius,
			bounds.x + bounds.w + targetVelocityX * launchDelay + radius, begin, end)) return false;
		// 草地/泳池的 Y 不变；屋顶目标会沿坡移动，留到窄相用 Board 的地形函数重建。
		if (!board.IsRoofBackground() && !ClipContactTime(origin.y, velocity.y,
			bounds.y - radius, bounds.y + bounds.h + radius, begin, end)) return false;
		const float groundY = board.GetRowCenterYAtX(zombie.mRow, zombie.GetPosition().x);
		const int firstStep = std::max(1, static_cast<int>(std::ceil(begin / step)));
		const int lastStep = static_cast<int>(std::floor(end / step));
		for (int index = firstStep; index <= lastStep; ++index) {
			const float time = index * step;
			const Vector position = origin + velocity * time;
			if (!board.IsMineBackground()
				&& Bullet::GetStarRow(board, position, sourceRow, velocity.y) != zombie.mRow) continue;
			const float dx = targetVelocityX * (launchDelay + time);
			const float dy = board.IsRoofBackground()
				? board.GetRowCenterYAtX(zombie.mRow, zombie.GetPosition().x + dx) - groundY : 0.0f;
			const SDL_FRect predicted{bounds.x + dx, bounds.y + dy, bounds.w, bounds.h};
			// 与 CollisionSystem 相同：AABB 预筛之后，圆角不能当成完整扩张矩形命中。
			if (position.x + radius <= predicted.x || position.x - radius >= predicted.x + predicted.w
				|| position.y + radius <= predicted.y || position.y - radius >= predicted.y + predicted.h) continue;
			const Vector nearest = collider.colliderType == ColliderType::CIRCLE
				? collider.GetWorldPosition() + Vector(dx, dy)
				: Vector(std::clamp(position.x, predicted.x, predicted.x + predicted.w),
					std::clamp(position.y, predicted.y, predicted.y + predicted.h));
			const float contactRadius = radius
				+ (collider.colliderType == ColliderType::CIRCLE ? collider.size.x * 0.5f : 0.0f);
			if ((position - nearest).sqrMagnitude() <= contactRadius * contactRadius) {
				return !board.MineBlocksSegment(origin, position);
			}
		}
		return false;
	}

}

void StarFruit::SetupPlant()
{
	Plant::SetupPlant();
	// 原版杨桃资源本体自带贴地明暗，不再叠加通用植物影子。
	RemoveShadow();
	if (!mAnimator) return;

	mAnimator->PlayTrack("anim_idle");
	mAnimator->SetSpeed(GameRandom::Range(
		kIdleFramesPerSecondMin / kReanimFramesPerSecond,
		kIdleFramesPerSecondMax / kReanimFramesPerSecond));

	// 原版首轮 launch counter 在 0..150 厘秒随机；用已累计时间表达同一均匀分布。
	mShootInterval = kInitialShootInterval;
	mShootTimer = GameRandom::Range(0.0f, kInitialShootInterval);

	mAnimator->AddFrameEvent(kFireFrame, [this]() {
		FireStarVolley();
	}, true);
}

void StarFruit::PlantUpdate()
{
	const float attackSpeed = GetAttackSpeedMultiplier();
	mShootTimer += DeltaTime::GetDeltaTime() * attackSpeed;
	if (mShootTimer < mShootInterval) return;

	// C# 到期时无论能否找到目标都会开始下一轮，避免无目标期间每帧重复扫描全场行桶。
	mShootTimer = 0.0f;
	mShootInterval = GameRandom::Range(
		kRepeatShootIntervalMin, kRepeatShootIntervalMax);
	if (!HasStarFruitTarget() || !mAnimator) return;

	const float shootSpeed =
		(kShootFramesPerSecond / kReanimFramesPerSecond) * attackSpeed;
	mAnimator->PlayTrackOnce(
		"anim_shoot", "anim_idle", shootSpeed,
		kShootBlendSeconds, 0.0f, kShootBlendSeconds);
}

bool StarFruit::HasStarFruitTarget() const
{
	if (!mBoard) return false;
	const Vector launchPosition = GetPosition() + Vector(kBulletOffsetX, kBulletOffsetY);
	auto velocities = StarVelocities();
	for (Vector& velocity : velocities) {
		velocity.x = Bullet::GetWindAdjustedVelocityX(mBoard, BulletType::BULLET_STAR, velocity.x);
	}
	const float step = DeltaTime::GetDeltaTime() > 0.0f
		? DeltaTime::GetDeltaTime() : DeltaTime::GetFixedStep();
	const auto shootRange = mAnimator ? mAnimator->GetTrackRange("anim_shoot") : std::pair{-1, -1};
	const float launchDelay = shootRange.first >= 0
		? std::max(0, kFireFrame - shootRange.first)
			/ (kShootFramesPerSecond * GetAttackSpeedMultiplier()) : 0.0f;
	std::array<float, 5> flightDurations;
	flightDurations.fill(-1.0f);
	bool found = false;
	for (int row = 0; row < mBoard->mRows && !found; ++row) {
		mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (found || !zombie || !zombie->IsActive() || zombie->IsDying()
				|| zombie->IsMindControlled() || !zombie->HasHead()
				|| !mBoard->CanPlantAcquireZombie(this, zombie)) return;
			const ColliderComponent* collider = zombie->GetColliderComponent();
			if (!collider || !collider->mEnabled
				|| !(collider->layerMask & CollisionLayer::ZOMBIE)
				|| !(collider->collisionMask & CollisionLayer::BULLET)) return;
			// 目标公开的平均移动预测已处理啃食、冻结与朝向，不按僵尸品种打补丁。
			const float targetVelocityX = zombie->GetTargetLeadX(1.0f) - zombie->GetTargetLeadX(0.0f);
			for (size_t i = 0; i < velocities.size(); ++i) {
				if (flightDurations[i] < 0.0f) flightDurations[i] = StarFlightDuration(
					*mBoard, launchPosition, velocities[i], mRow, step);
				if (StarCanHit(*mBoard, launchPosition, velocities[i], mRow, *zombie, *collider,
					targetVelocityX, launchDelay, flightDurations[i], step)) {
					found = true;
					return;
				}
			}
		});
	}
	return found;
}

void StarFruit::FireStarVolley()
{
	if (!mBoard || mIsPreview) return;

	AudioSystem::PlaySound(GameRandom::Chance()
		? ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT
		: ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2, 0.3f);

	const Vector launchPosition =
		GetPosition() + Vector(kBulletOffsetX, kBulletOffsetY);
	for (const Vector& velocity : StarVelocities()) {
		Bullet* star = mBoard->CreatePlantBullet(
			BulletType::BULLET_STAR, mRow, launchPosition, mPlantType);
		if (!star) continue;
		star->SetBulletDamage(kStarDamage);
		star->SetVelocityX(velocity.x);
		star->SetVelocityY(velocity.y);
	}
}

void StarFruit::SaveExtraData(nlohmann::json& j) const
{
	j["shootTimer"] = mShootTimer;
	j["shootInterval"] = mShootInterval;
}

void StarFruit::LoadExtraData(const nlohmann::json& j)
{
	mShootTimer = std::clamp(
		j.value("shootTimer", 0.0f), 0.0f, kRepeatShootIntervalMax);
	mShootInterval = std::clamp(
		j.value("shootInterval", kInitialShootInterval),
		kRepeatShootIntervalMin, kRepeatShootIntervalMax);
}

void StarFruit::SetShootCycleForTesting(
	float elapsedSeconds, float intervalSeconds)
{
	mShootInterval = std::clamp(
		intervalSeconds, kRepeatShootIntervalMin, kRepeatShootIntervalMax);
	mShootTimer = std::clamp(elapsedSeconds, 0.0f, mShootInterval);
}
