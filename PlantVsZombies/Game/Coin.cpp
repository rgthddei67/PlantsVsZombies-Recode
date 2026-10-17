#include "Coin.h"
#include "Game/Board/Board.h"
#include "../DeltaTime.h"

Coin::Coin(Board* board, AnimationType animType, const Vector& position,
	const Vector& colliderSize, const Vector& colliderOffset, float VanlishTime,
	float scale,
	const std::string& tag, bool needScaleAnimation, bool autoDestroy)
	: AnimatedObject(ObjectType::OBJECT_COIN, board, position, animType, ColliderType::CIRCLE,
		colliderSize, colliderOffset, scale, tag, autoDestroy)
{
	this->mVanlishTime = VanlishTime;
	this->mTargetScale = scale;
	this->mStartScale = scale * 0.1f;
	this->mIsScaling = needScaleAnimation;
	this->mScaleAnimationFinished = !needScaleAnimation;

	if (auto collider = GetColliderComponent()) {
		collider->layerMask = CollisionLayer::NONE;
		collider->collisionMask = CollisionLayer::NONE;
	}
}

void Coin::Start()
{
	AnimatedObject::Start();
	if (mIsScaling) {
		SetScale(mStartScale);
	}
	else {
		SetScale(mTargetScale);
	}
	auto clickableComponent = CreateClickable();

	clickableComponent->ConsumeEvent = true;

	SetOnClickBack(clickableComponent);

	this->PlayTrack("Sun1");
}

void Coin::Update()
{
	if (mIsDestroyed) return;
	UpdateScale();

	AnimatedObject::Update();

	if (isMovingToTarget)
	{
		Vector currentPos = GetPosition();
		Vector direction = targetPos - currentPos;
		float distance = direction.magnitude();

		if (distance < 65.0f)
		{
			OnReachTargetBack();
			return;
		}

		float currentSpeed = (distance > slowDownDistance) ? speedFast : speedSlow;

		if (distance > 0) {
			Vector normalizedDir = direction / distance;
			Vector newPos = currentPos + normalizedDir * currentSpeed * DeltaTime::GetDeltaTime();
			SetPosition(newPos);
		}
	}
	else
	{
		// 缩放动画完成前不开始消失计时
		if (!mScaleAnimationFinished) return;

		mVanlishTimer += DeltaTime::GetDeltaTime();
		if (mVanlishTimer >= mVanlishTime)
		{
			Retire();
		}
	}
}

void Coin::SetOnClickBack(ClickableComponent* clickComponent)
{
	if (clickComponent == nullptr) return;
	clickComponent->onClick = [this]() {
		StartMoveToTarget();
		};
}

void Coin::StartMoveToTarget(const Vector& target, float fastSpeed,
	float slowSpeed, float slowdownDist)
{
	if (mIsDestroyed) return;
	PauseAnimation();
	if (auto clickable = GetClickable()) {
		clickable->IsClickable = false;
	}
	targetPos = target;
	speedFast = fastSpeed;
	speedSlow = slowSpeed;
	slowDownDistance = slowdownDist;
	isMovingToTarget = true;
	StopScaleAnimation();
}

void Coin::StopMove()
{
	isMovingToTarget = false;
}

bool Coin::IsMoving() const
{
	return isMovingToTarget;
}

void Coin::SetTargetPosition(const Vector& target)
{
	targetPos = target;
}

void Coin::OnReachTargetBack()
{
	Retire();
}

void Coin::Retire()
{
	if (mIsDestroyed) return;
	isMovingToTarget = false;
	mIsDestroyed = true;
	// GOM 下一步才移除对象；本步结算后的快照和输入必须立即把它视为已消失。
	SetActive(false);
	GameObjectManager::GetInstance().DestroyGameObject(this);
}

Vector Coin::GetPosition() const
{
	if (GetTransform()) {
		return GetTransform()->GetPosition();
	}
	return Vector::zero();
}

void Coin::SetPosition(const Vector& newPos)
{
	if (GetTransform()) {
		GetTransform()->SetPosition(newPos);
	}
}

void Coin::SetScale(float scale)
{
	if (GetTransform()) {
		GetTransform()->SetScale(scale);
	}
}

void Coin::UpdateScale()
{
	if (!mIsScaling) return;

	mScaleTimer += DeltaTime::GetDeltaTime();
	float t = mScaleTimer / mScaleDuration;

	t = t * t * t; // 立方缓入，开始慢，结束快

	if (t >= 1.0f) {
		t = 1.0f;
		FinishScaleAnimation();
	}

	float currentScale = mStartScale + (mTargetScale - mStartScale) * t;
	SetScale(currentScale);
}

void Coin::StopScaleAnimation()
{
	mIsScaling = false;
	mScaleAnimationFinished = true;
}

void Coin::FinishScaleAnimation()
{
	StopScaleAnimation();
	SetScale(mTargetScale);
}
