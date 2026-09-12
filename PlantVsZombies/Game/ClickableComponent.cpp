#include "ClickableComponent.h"
#include "ColliderComponent.h"
#include "GameObjectManager.h"
#include "../UI/InputHandler.h"
#include "../GameApp.h"
#include "GameObject.h"
#include "../CursorManager.h"
#include <algorithm>

ClickableComponent::ClickableComponent(GameObject* owner)
	: mGameObject(owner) {
	s_allClickables.push_back(this);
}

ClickableComponent::~ClickableComponent() {
	// swap-with-back-and-pop：O(1) 移除，顺序不重要
	auto it = std::find(s_allClickables.begin(), s_allClickables.end(), this);
	if (it != s_allClickables.end()) {
		*it = s_allClickables.back();
		s_allClickables.pop_back();
	}
}

void ClickableComponent::ClearProcessedEvents() {
	s_processedEvents.clear();
}

void ClickableComponent::ProcessMouseEvents() {
	auto& input = GameAPP::GetInstance().GetInputHandler();

	// 获取鼠标屏幕坐标和世界坐标
	Vector mouseScreen = input.GetMousePosition();
	Vector mouseWorld = input.GetMouseWorldPosition();

	// 清空上一帧的处理记录
	ClearProcessedEvents();

	s_hoveringClickable = false;
	// 收集所有鼠标位置下的可点击对象：直接遍历自注册表，避免扫全场 GameObject
	std::vector<std::pair<GameObject*, ClickableComponent*>> clickableObjects;
	clickableObjects.reserve(s_allClickables.size());

	for (auto* clickable : s_allClickables)
	{
		if (!clickable->IsClickable) continue;

		auto* obj = clickable->GetGameObject();
		// 显式附件在宿主构造期已经有 owner；仍保持旧延迟初始化阶段在 Start 前不可命中的语义。
		if (!obj || !obj->HasStarted() || !obj->IsActive()) continue;

		auto* collider = obj->GetCollider();
		if (!collider || !collider->mEnabled) continue;

		Vector testPoint = obj->mIsUI ? mouseScreen : mouseWorld;

		if (collider->ContainsPoint(testPoint)) {
			clickableObjects.emplace_back(obj, clickable);

			if (clickable->ChangeCursorOnHover) {
				s_hoveringClickable = true;
			}
		}
	}

	// 按渲染顺序降序排序（order大的在前）
	std::sort(clickableObjects.begin(), clickableObjects.end(),
		[](const auto& a, const auto& b) {
			return b.first->IsDrawnBefore(*a.first);
		});

	// 更新所有对象的鼠标悬停状态 后标记处理过的对象
	for (size_t i = 0; i < clickableObjects.size(); i++)
	{
		auto pair = clickableObjects[i];
		auto clickable = pair.second;
		bool wasHovered = clickable->mouseOver;
		pair.second->mouseOver = true;

		if (!wasHovered && clickable->ChangeCursorOnHover) {
			CursorManager::GetInstance().IncrementHoverCount();
		}

		if (s_processedEvents.find(clickable) != s_processedEvents.end()) {
			continue;
		}

		bool processed = false;

		if (input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
			clickable->mouseDown = true;
			if (clickable->onMouseDown) clickable->onMouseDown();
			processed = true;
		}

		if (input.IsMouseButtonReleased(SDL_BUTTON_LEFT) && clickable->mouseDown) {
			clickable->mouseDown = false;
			if (clickable->onMouseUp) clickable->onMouseUp();
			if (clickable->onClick) clickable->onClick();
			processed = true;
		}

		if (processed && clickable->ConsumeEvent) {
			s_processedEvents.insert(clickable);
			for (size_t j = 0; j < clickableObjects.size(); j++)
			{
				auto remainingPair = clickableObjects[j];
				if (remainingPair.second != clickable) {
					s_processedEvents.insert(remainingPair.second);
				}
			}
			break;
		}

		if (processed) {
			s_processedEvents.insert(clickable);
		}
		clickableObjects[i].second->Update();
	}
}

void ClickableComponent::Update() {
	// 先重置mouseOver状态（会在ProcessMouseEvents中重新设置）
	bool wasMouseOver = mouseOver;
	mouseOver = false;

	// 处理鼠标进入/离开事件
	if (prevMouseOver && !wasMouseOver && onMouseExit) {
		onMouseExit();
		mouseDown = false;
	}
	prevMouseOver = wasMouseOver;
}

void ClickableComponent::SetClickArea(const Vector& size) {
	if (auto* gameObject = GetGameObject()) {
		if (auto* collider = gameObject->GetCollider()) collider->size = size;
	}
}

void ClickableComponent::SetClickOffset(const Vector& offset) {
	if (auto* gameObject = GetGameObject()) {
		if (auto* collider = gameObject->GetCollider()) collider->offset = offset;
	}
}
