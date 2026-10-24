#include "LawnMower.h"
#include "Game/Board/Board.h"
#include "Zombie/Zombie.h"
#include "GameObjectManager.h"
#include "ShadowComponent.h"
#include "../DeltaTime.h"
#include "../GameApp.h"
#include <algorithm>

namespace {
	constexpr float kPoolCleanerClipSpeed = 35.0f / 12.0f; // 原版 35 FPS；本引擎参数是相对资源 12 FPS 的倍率
	constexpr float kRoofCleanerClipSpeed = 70.0f / 12.0f; // 原版屋顶清洁车启动后按 70 FPS 循环
	constexpr float kRoofCleanerIdleShadowOffsetX = 41.0f; // 待机缓存帧影子对齐轮组中心的水平偏移，单位：像素
	constexpr float kRoofCleanerMovingShadowOffsetX = 31.0f; // 启动 reanim 车身左移 10px，影子同步补偿
	constexpr float kRoofCleanerIdleShadowOffsetY = 22.0f; // 待机缓存帧影子垂直偏移，单位：像素
	constexpr float kRoofCleanerMovingShadowOffsetY = 58.0f; // 启动 reanim 比待机帧下移 36px，影子同步补偿
	constexpr float kPoolCleanerMoveSpeed = 230.0f * 2.5f / 3.33f; // 按原版水车/草车 2.5:3.33 的速度比换算
	constexpr float kPoolTransitionDepth = 28.0f;           // 入水阶段最大视觉下沉距离，单位：像素
	constexpr float kPoolCleanerInWaterLiftY = 13.0f;       // 水中稳态相对原下沉位置向上校正的像素数
	constexpr float kGardenPoolCleanerExtraLiftY = 50.0f;   // 水岸植物园单行水池额外抬高车身，避免压到下方石岸，单位：像素
	constexpr float kPoolTransitionSpeedRatio = 0.8f;       // 原版每横移 2.5 像素下沉/上浮 2 像素
	constexpr float kPoolMowBlendTime = 0.1f;               // 吞噬轨道切换混合时间，单位：秒
	constexpr float kFloodLawnMowerLiftY = 10.0f;           // 洪水托起普通推车的视觉高度，像素；不改变碰撞与触发位置
}

Mower::Mower(Board* board, MowerType type, AnimationType animType, float x, float y, int row, float scale)
	: AnimatedObject(ObjectType::OBJECT_LAWNMOWER, board,
		Vector(x, y),
		animType,
		ColliderType::BOX,
		Vector(60, 50),
		Vector(0, 0),
		scale,
		"LawnMower",
		false)
{
	this->mRow = row;
	this->mMowerType = type;

	auto shadowcomponent = CreateShadow
		(ResourceManager::GetInstance().GetTexture
		(ResourceKeys::Textures::IMAGE_PLANTSHADOW));
	shadowcomponent->SetOffset(Vector(40, 50));
	shadowcomponent->SetScale(Vector(1.0f, 1.0f));

	// 水路清洁车待机时停在陆地轨道首帧，启动后才以原版速率循环。
	if (mMowerType == MowerType::WATER) {
		mSpeed = kPoolCleanerMoveSpeed;
		shadowcomponent->SetOffset(Vector(25, 50));
		PlayTrack("anim_land", kPoolCleanerClipSpeed);
		PauseAnimation();
	}
	else if (mMowerType == MowerType::ROOF) {
		SetAnimationSpeed(kRoofCleanerClipSpeed);
		SetCurrentFrame(0.0f);
		PauseAnimation();
		// RoofCleaner 的轮组中心偏右；影子贴在两轮之间，避免落到车身左下方。
		shadowcomponent->SetOffset(Vector(
			kRoofCleanerIdleShadowOffsetX, kRoofCleanerIdleShadowOffsetY));
		shadowcomponent->SetScale(Vector(1.0f, 1.2f));
	}

	auto collider = GetColliderComponent();
	if (!collider) return;

	collider->isTrigger = true;
	collider->layerMask = CollisionLayer::MOWER;
	collider->collisionMask = CollisionLayer::ZOMBIE;
	collider->SetTriggerStayCallback([this](ColliderComponent* other) {
		auto* go = other->GetGameObject();
		if (!go || go->GetObjectType() != ObjectType::OBJECT_ZOMBIE) return;
		auto* zombie = dynamic_cast<Zombie*>(go);
		if (!zombie || !zombie->CanBeTargetedByProjectile(false)) return;

		// 首次碰撞触发移动
		if (mState == MowerState::IDLE) {
			Trigger();
		}

		// 精英所在行保留正常启动和秒杀；其余行只失去小推车，不播放启动效果。
		if (zombie->ConsumesOtherMowersOnContact() && mBoard) {
			mBoard->RemoveOtherMowersWithoutTrigger(mMowerID);
		}

		if (mMowerType == MowerType::WATER) {
			PlayPoolMowAnimation();
		}
		// 先完成正常启动与消耗；首领可拒绝处决，让清洁车从其身上驶过而不造成伤害。
		if (zombie->CanBeKilledByMower()) {
			if (mBoard) mBoard->MarkTemporalTargetIrreversible(zombie->mZombieID);
			zombie->TakeDamage(INT32_MAX, DamageSource::OTHER);
		}

		});
}

void Mower::Trigger()
{
	if (mState == MowerState::MOVING) return;

	if (mMowerType == MowerType::WATER) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_POOL_CLEANER, 0.4f);
		PlayTrack(mMowerHeight == MowerHeight::IN_POOL ? "anim_water" : "anim_land", kPoolCleanerClipSpeed);
	}
	else if (mMowerType == MowerType::ROOF) {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LAWNMOWER, 0.4f);
		SetAnimationSpeed(kRoofCleanerClipSpeed);
		PlayAnimation();
	}
	else {
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LAWNMOWER, 0.4f);
		PlayTrack("anim_normal");
	}

	mState = MowerState::MOVING;
	if (mBoard) {
		mBoard->SetRowLoseMower(mRow);
	}
}

void Mower::Update()
{
	// 通用动画读档会恢复轨道状态但不保存 Pause；待机清洁车每帧重申首帧冻结契约。
	if (mMowerType == MowerType::WATER && mState == MowerState::IDLE) {
		PauseAnimation();
	}
	if (mMowerType == MowerType::ROOF) {
		if (mState == MowerState::IDLE) PauseAnimation();
		else if (!IsAnimationPlaying()) {
			SetAnimationSpeed(kRoofCleanerClipSpeed);
			PlayAnimation();
		}
		// 待机缓存帧与启动 reanim 的车身 Y 相差 36px；影子必须按状态同步，否则启动后会留在车顶。
		if (auto shadow = GetShadow()) {
			shadow->SetOffset(Vector(
				mState == MowerState::IDLE
					? kRoofCleanerIdleShadowOffsetX : kRoofCleanerMovingShadowOffsetX,
				mState == MowerState::IDLE
					? kRoofCleanerIdleShadowOffsetY : kRoofCleanerMovingShadowOffsetY));
		}
	}
	AnimatedObject::Update();

	Vector pos = GetPosition();
	if (mMowerType == MowerType::ROOF && mBoard) {
		pos.y = mBoard->GetMowerTerrainY(mRow, pos.x + 40.0f);
		SetPosition(pos);
	}
	if (mState != MowerState::MOVING) {
		// 洪水可先淹到停车位；待机也跟随水位，但不启动或消耗这一行防线。
		if (mBoard && mBoard->SupportsWatersideFlood()) UpdatePoolState(DeltaTime::GetDeltaTime());
		return;
	}

	float deltaTime = DeltaTime::GetDeltaTime();
	pos.x += mSpeed * deltaTime;
	if (mBoard && mBoard->MineBlocksSegment(
		Vector(GetPosition().x + 60.0f, mBoard->GetCellCenterPosition(mRow, 0).y),
		Vector(pos.x + 60.0f, mBoard->GetCellCenterPosition(mRow, 0).y))) {
		Die();
		return;
	}
	if (mBoard) {
		const int holeColumn = mBoard->GetSnowHoleColumn(mRow);
		if (holeColumn >= 0) {
			const float holeCenterX = mBoard->GetCellCenterPosition(
				mRow, holeColumn).x;
			if (pos.x + 60.0f >= holeCenterX) mBoard->SealSnowHole(mRow);
		}
	}
	if (mMowerType == MowerType::ROOF && mBoard) {
		// 车辆前缘是与 C# 一致的地面探针，穿过斜坡/平台折角时不会瞬移。
		pos.y = mBoard->GetMowerTerrainY(mRow, pos.x + 40.0f);
	}
	SetPosition(pos);
	if (mMowerType == MowerType::WATER || (mBoard && mBoard->SupportsWatersideFlood())) {
		UpdatePoolState(deltaTime);
	}

	if (pos.x > static_cast<float>(SCENE_WIDTH) + 40.0f) {
		Die();
	}
}

void Mower::Die()
{
	if (mCollider) {
		mCollider->mEnabled = false;
	}
	GameObjectManager::GetInstance().DestroyGameObject(this);
}

Vector Mower::GetPosition() const
{
	if (GetTransform()) {
		return GetTransform()->GetPosition();
	}
	return Vector::zero();
}

void Mower::SetPosition(const Vector& position)
{
	if (GetTransform()) {
		GetTransform()->SetPosition(position);
	}
}

Vector Mower::GetVisualPosition() const
{
	if (mMowerType == MowerType::ROOF) {
		// RoofCleaner 的资源原点与普通草车不同；保留原版待机缓存与启动 reanim 的两套偏移。
		return AnimatedObject::GetVisualPosition()
			+ (mState == MowerState::IDLE
				? Vector(6.0f, -40.0f) : Vector(-4.0f, -4.0f));
	}
	float waterLiftY = 0.0f;
	if (mMowerType == MowerType::LAWN && mBoard && mBoard->SupportsWatersideFlood()) {
		return AnimatedObject::GetVisualPosition() + Vector(0.0f,
			-kFloodLawnMowerLiftY * mPoolVisualOffsetY / kPoolTransitionDepth);
	}
	if (mMowerType == MowerType::WATER && mMowerHeight != MowerHeight::LAND) {
		// 上移校正按当前入水深度渐进，避免 ENTERING/IN_POOL/EXITING 切换时画面跳变；
		// 逻辑位置、碰撞和存档中的原始入水深度均保持不变。
		const float immersion = std::clamp(
			mPoolVisualOffsetY / kPoolTransitionDepth, 0.0f, 1.0f);
		const float gardenLiftY = mBoard && mBoard->IsWatersideGarden()
			? kGardenPoolCleanerExtraLiftY : 0.0f;
		waterLiftY = (kPoolCleanerInWaterLiftY + gardenLiftY) * immersion;
	}
	return AnimatedObject::GetVisualPosition()
		+ Vector(0.0f, mPoolVisualOffsetY - waterLiftY);
}

void Mower::RestorePoolVisualState(MowerHeight height, float visualOffsetY)
{
	mMowerHeight = height;
	mPoolVisualOffsetY = std::clamp(visualOffsetY, 0.0f, kPoolTransitionDepth);
	UpdatePoolShadowVisibility();
}

void Mower::UpdatePoolState(float deltaTime)
{
	if (!mBoard) return;

	const Vector pos = GetPosition();
	const bool overPool = mBoard->IsPoolWorldPosition(mRow, pos.x + 40.0f);
	const float transitionStep = mSpeed * kPoolTransitionSpeedRatio * deltaTime;
	// 涨退水可在过渡中反向，保持当前高度连续，不先完成已经失效的上下岸动作。
	if (overPool && mMowerHeight == MowerHeight::EXITING) mMowerHeight = MowerHeight::ENTERING;
	else if (!overPool && mMowerHeight == MowerHeight::ENTERING) {
		mMowerHeight = MowerHeight::EXITING;
		if (mMowerType == MowerType::WATER) PlayTrack("anim_land", kPoolCleanerClipSpeed);
	}

	switch (mMowerHeight) {
	case MowerHeight::LAND:
		if (overPool) {
			mMowerHeight = MowerHeight::ENTERING;
			mPoolVisualOffsetY = 0.0f;
			UpdatePoolShadowVisibility();
		}
		break;

	case MowerHeight::ENTERING:
		// 逻辑位置和碰撞体沿原行移动；只有绘制位置逐步沉入水面。
		mPoolVisualOffsetY = std::min(
			kPoolTransitionDepth, mPoolVisualOffsetY + transitionStep);
		if (mPoolVisualOffsetY >= kPoolTransitionDepth) {
			mMowerHeight = MowerHeight::IN_POOL;
			mPoolVisualOffsetY = kPoolTransitionDepth;
			if (mMowerType == MowerType::WATER) PlayTrack("anim_water", kPoolCleanerClipSpeed);
			UpdatePoolShadowVisibility();
		}
		break;

	case MowerHeight::IN_POOL:
		if (!overPool) {
			mMowerHeight = MowerHeight::EXITING;
			mPoolVisualOffsetY = kPoolTransitionDepth;
			if (mMowerType == MowerType::WATER) PlayTrack("anim_land", kPoolCleanerClipSpeed);
			UpdatePoolShadowVisibility();
		}
		break;

	case MowerHeight::EXITING:
		mPoolVisualOffsetY = std::max(0.0f, mPoolVisualOffsetY - transitionStep);
		if (mPoolVisualOffsetY <= 0.0f) {
			mMowerHeight = MowerHeight::LAND;
			mPoolVisualOffsetY = 0.0f;
			UpdatePoolShadowVisibility();
		}
		break;
	}
	if (mMowerType == MowerType::WATER && mState == MowerState::IDLE) PauseAnimation();
}

void Mower::PlayPoolMowAnimation()
{
	const bool inPool = mMowerHeight == MowerHeight::IN_POOL;
	const char* mowTrack = inPool ? "anim_suck" : "anim_landsuck";
	const char* returnTrack = inPool ? "anim_water" : "anim_land";
	PlayTrackOnce(mowTrack, returnTrack,
		kPoolCleanerClipSpeed, kPoolMowBlendTime, kPoolCleanerClipSpeed);
}

void Mower::UpdatePoolShadowVisibility()
{
	if (auto* shadow = GetShadow()) {
		shadow->SetVisible(mMowerHeight == MowerHeight::LAND);
	}
}
