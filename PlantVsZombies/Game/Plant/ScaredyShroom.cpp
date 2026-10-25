#include "ScaredyShroom.h"
#include "../Bullet/Bullet.h"
#include "../ShadowComponent.h"

namespace {
	constexpr float kTargetCheckIntervalSeconds = 0.1f; // 本行索敌缓存刷新间隔（秒），须短于精英最快射击间隔
	constexpr float kFearCheckIntervalSeconds = 0.15f;  // 近身害怕判定缓存刷新间隔（秒）
	constexpr float kBaseShootAnimationSpeed = 1.5f;    // 普通胆小菇既有射击动画速度
	constexpr float kShootWindupAtUnitSpeed = 0.75f;    // anim_shooting 16→25 帧在 12fps、1倍速下的吐弹前摇（秒）
}

void ScaredyShroom::SetupPlant()
{
	auto shadow = GetShadow();
	shadow->SetScale(Vector(0.6f, 0.6f));
	shadow->SetOffset(Vector(-4, 30));

	Shroom::SetupPlant();

	if (mIsPreview) return;

	// 第 25 帧吐孢子（主人按动画预览指定；anim_shooting 活跃区间为全局 16..30 帧）
	mAnimator->AddFrameEvent(25, [this]() {
		mShotPending = false;
		if (!mBoard) return;
		AudioSystem::PlaySound("SOUND_PUFF", 0.28f);
		Vector bulletPosition = GetPosition() + Vector(2, -6);
		Bullet* bullet = mBoard->CreatePlantBullet(
			BulletType::BULLET_PUFF, mRow, bulletPosition, mPlantType);
		if (!bullet) return;
		bullet->SetBulletDamage(GetPuffDamage());
		OnPuffFired();
		}, true);
}

void ScaredyShroom::PlantUpdate()
{
	// 吐弹帧之前不进害怕流程，避免把本发孢子拦腰打断；帧事件已经结算后立即允许受惊。
	// 不能守卫整段 anim_shooting：精英在 0.2s 档会连续重启该轨道，从而永久免疫害怕。
	const std::string curTrack = GetCurrentTrackName();
	if (mShotPending && curTrack != "anim_shooting") {
		// 理论上第 25 帧会先清掉 pending；若动画被外部切轨，则放开下一轮重试，避免永久哑火。
		mShotPending = false;
	}
	if (curTrack != "anim_shooting" || !mShotPending) {
		const bool scared = HasZombieNearby();
		switch (mFearState) {
		case FearState::READY:
			if (scared) {
				OnFearStarted();
				mFearState = FearState::LOWERING;
				PlayTrackOnce("anim_scared", "anim_scaredidle", 0.0f, 0.2f);
			}
			break;
		case FearState::LOWERING:
			// PlayTrackOnce 播完自动接 anim_scaredidle，以轨道名切换为完成信号
			if (curTrack == "anim_scaredidle") mFearState = FearState::SCARED;
			break;
		case FearState::SCARED:
			if (!scared) {
				mFearState = FearState::RAISING;
				PlayTrackOnce("anim_grow", "anim_idle", 0.0f, 0.2f);
			}
			break;
		case FearState::RAISING:
			if (curTrack == "anim_idle") mFearState = FearState::READY;
			break;
		}
	}

	if (mFearState != FearState::READY) {
		mShootTimer = 0.0f;   // 非就绪态压住射击计时（原版 mLaunchCounter=mLaunchRate）
		return;
	}

	// 目标缓存必须独立于射击计时刷新；否则 0.2s 阈值到达后才开始累计索敌节流，
	// 每发会额外平白等待 0.1s，最快射速就无法兑现。
	const bool hasTarget = HasZombieInRow();

	// 生存攻速词条 × 雨势行动倍率；害怕状态机与索敌轮询仍使用真实 deltaTime。
	float mult = GetAttackSpeedMultiplier();
	this->mShootTimer += (DeltaTime::GetDeltaTime() * mult);
	const float shootInterval = GetShootInterval();
	if (!mShotPending && this->mShootTimer >= shootInterval)
	{
		if (hasTarget)
		{
			mShootTimer = 0;
			// pending 保护保证下一轮不能在吐弹帧之前重播；高速阶段允许吐弹后立即从前摇重启。
			mShotPending = PlayTrackOnce("anim_shooting", "anim_idle",
				GetShootAnimationSpeed(mult), 0.2f);
		}
	}
}

bool ScaredyShroom::HasZombieInRow()
{
	if (!mBoard) return false;

	// 肥沃同步缩短攻击前索敌间隔，避免固定节流抵消高频攻击。
	mCheckZombieTimer += DeltaTime::GetDeltaTime() * GetIndependentActionSpeedMultiplier();
	if (mCheckZombieTimer < kTargetCheckIntervalSeconds) return mTargetCached;
	mCheckZombieTimer = 0.0f;

	// 按行索引：只遍历本行僵尸。全行射程，不设 300px 上限（与小喷菇的差异点）。
	const float thisX = GetPosition().x;
	bool found = false;
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [&](Zombie* zombie) {
		if (found) return;
		const float dx = zombie->GetPosition().x - thisX;
		if (!zombie->IsMindControlled() && dx >= 0 && zombie->HasHead()
			&& mBoard->CanPlantAcquireZombie(this, zombie))
			found = true;
	});
	mTargetCached = found;
	return mTargetCached;
}

bool ScaredyShroom::HasZombieNearby()
{
	if (!mBoard) return false;
	if (mBoard->GetPumpkinAt(mRow, mColumn)) {
		// 套壳免疫必须同时覆盖节流缓存；保留旧 true 会让 READY 与受惊过渡态反复切换。
		mScaredCached = false;
		mFearCheckTimer = kFearCheckIntervalSeconds;
		return false;
	}

	// 0.15s 节流：害怕判定要扫 3 行僵尸做圆-矩形相交，逐帧算在海量僵尸场景下白烧 CPU，
	// 150ms 的反应延迟肉眼不可感。缓存上次结果供节流间隙使用。
	mFearCheckTimer += DeltaTime::GetDeltaTime();
	if (mFearCheckTimer < kFearCheckIntervalSeconds) return mScaredCached;
	mFearCheckTimer = 0.0f;

	// 原版圆心 (mX, mY+20) 是格子左上角坐标系；本项目 GetPosition() 是 80x100 格子中心，
	// 换算后圆心 = 中心 + (-40, -30)，半径 120（覆盖本行±1行、含身后僵尸）。
	const Vector center = GetPosition() + Vector(-40.0f, -30.0f);
	constexpr float kFearRadiusSq = 120.0f * 120.0f;

	bool found = false;
	for (int row = mRow - 1; row <= mRow + 1 && !found; ++row) {
		// 行索引已排除失活/濒死僵尸（原版也不吓垂死僵尸）
		mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (found) return;
			if (zombie->IsMindControlled()) return;   // 魅惑僵尸不吓
			auto collider = zombie->GetColliderComponent();
			if (!collider) return;
			const SDL_FRect rect = collider->GetBoundingBox();
			// 圆 vs 矩形：取矩形上离圆心最近的点判距
			const float nx = std::clamp(center.x, rect.x, rect.x + rect.w);
			const float ny = std::clamp(center.y, rect.y, rect.y + rect.h);
			const float dx = center.x - nx;
			const float dy = center.y - ny;
			if (dx * dx + dy * dy <= kFearRadiusSq)
				found = true;
		});
	}
	mScaredCached = found;
	return found;
}

const char* ScaredyShroom::GetFearStateName() const
{
	switch (mFearState) {
	case FearState::READY: return "READY";
	case FearState::LOWERING: return "LOWERING";
	case FearState::SCARED: return "SCARED";
	case FearState::RAISING: return "RAISING";
	}
	return "UNKNOWN";
}

float ScaredyShroom::GetShootAnimationSpeed(float attackSpeedMultiplier) const
{
	// 只把“起播到第 25 帧吐弹”的 0.75s 前摇压进当前射击间隔；不按整段 1.5s 基准
	// 成倍放大，否则后期会快到肉眼看不见，并在重播边界丢失孢子。
	const float interval = std::max(GetShootInterval(), 0.001f);
	const float speedForWindup = kShootWindupAtUnitSpeed / interval;
	return attackSpeedMultiplier * std::max(kBaseShootAnimationSpeed, speedForWindup);
}
