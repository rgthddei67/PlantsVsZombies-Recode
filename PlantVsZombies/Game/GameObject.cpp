#include "GameObject.h"
#include "CollisionSystem.h"
#include "ColliderComponent.h"
#include "ShadowComponent.h"
#include "ClickableComponent.h"
#include "GameObjectManager.h"
#include <tuple>

GameObject::GameObject(ObjectType type)
	: mObjectType(type),
	  mTag(&InternRuntimeString("Untagged")),
	  mName(&InternRuntimeString("GameObject"))
{
}

GameObject::~GameObject()
{
	DestroyAttachments();
}

void GameObject::SetRenderOrder(int order, int subOrder)
{
	if (mRenderOrder == order && mRenderSubOrder == subOrder) return;
	mRenderOrder = order;
	mRenderSubOrder = subOrder;
	if (mRenderOrderManager) mRenderOrderManager->MarkRenderOrderDirty();
}

bool GameObject::IsDrawnBefore(const GameObject& other) const
{
	return std::tie(mRenderOrder, mRenderSubOrder, mRenderSequence)
		< std::tie(other.mRenderOrder, other.mRenderSubOrder, other.mRenderSequence);
}

void GameObject::Start() {
	if (!mStarted) {
		RegisterColliderIfNeeded();
		mStarted = true;
	}
}

void GameObject::Update() {
	if (!mActive || !mStarted) return;

	// 保持 Clickable 在宿主更新后、场景统一命中处理前重置状态的时序。
	if (mClickable) mClickable->Update();
}

void GameObject::Draw(Graphics* g) {
	if (!mActive || !mStarted) return;

	// 阴影固定先于宿主本体提交；Bullet 不调用本入口，改由 BulletPool 的地面阶段显式绘制。
	if (mShadow) mShadow->Draw(g);

	// Collider 由宿主固定提交；其内部仅在 Debug 开关开启时绘制碰撞框。
	if (mCollider) mCollider->Draw(g);
}

void GameObject::DestroyAttachments() {
	RemoveClickable();
	RemoveShadow();
	RemoveCollider();
}

ColliderComponent* GameObject::CreateCollider(
	const Vector& size, const Vector& offset, ColliderType type) {
	// 先完成新对象分配，再替换旧对象；这样已注册 Clickable 不会观察到半初始化 Collider。
	auto replacement = std::unique_ptr<ColliderComponent>(
		new ColliderComponent(this, size, offset, type));
	if (mCollider) CollisionSystem::GetInstance().UnregisterCollider(mCollider.get());
	mCollider = std::move(replacement);
	if (mStarted) RegisterColliderIfNeeded();
	return mCollider.get();
}

bool GameObject::RemoveCollider() {
	// Clickable 的命中几何由 Collider 唯一提供；显式移除几何时同步撤销点击注册。
	RemoveClickable();
	if (!mCollider) return false;
	CollisionSystem::GetInstance().UnregisterCollider(mCollider.get());
	mCollider.reset();
	return true;
}

ClickableComponent* GameObject::CreateClickable() {
	if (!mCollider) CreateCollider(Vector(50, 50));
	mCollider->isTrigger = true;
	RemoveClickable();
	mClickable = std::unique_ptr<ClickableComponent>(new ClickableComponent(this));
	return mClickable.get();
}

bool GameObject::RemoveClickable() {
	if (!mClickable) return false;
	mClickable.reset();
	return true;
}

ShadowComponent* GameObject::CreateShadow(
	const Texture* texture, const Vector& offset, float alpha) {
	mShadow = std::unique_ptr<ShadowComponent>(
		new ShadowComponent(this, texture, offset, alpha));
	return mShadow.get();
}

bool GameObject::RemoveShadow() {
	if (!mShadow) return false;
	mShadow.reset();
	return true;
}

void GameObject::RegisterColliderIfNeeded() {
	if (mCollider) CollisionSystem::GetInstance().RegisterCollider(mCollider.get());
}
