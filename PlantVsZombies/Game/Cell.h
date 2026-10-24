#pragma once
#ifndef _CELL_H
#define _CELL_H
#include "Definit.h"
#include "ClickableComponent.h"
#include "ColliderComponent.h"
#include "Transform.h"
#include "GameObject.h"
#include "GameObjectManager.h"
#include "./Plant/PlantType.h"
#include <array>

constexpr float CELL_COLLIDER_SIZE_X = 80.0f;
constexpr float CELL_COLLIDER_SIZE_Y = 100.0f;
constexpr float CELL_INITALIZE_POS_X = 242.0f;
constexpr float CELL_INITALIZE_POS_Y = 88.0f;

class Cell : public GameObject {
private:
	std::function<void(int, int)> OnCellClicked;
	Vector mColliderSize = Vector(CELL_COLLIDER_SIZE_X, CELL_COLLIDER_SIZE_Y);
	int mUnderPlantID = NULL_PLANT_ID;
	int mNormalPlantID = NULL_PLANT_ID;
	int mPumpkinPlantID = NULL_PLANT_ID;
	int mOverlayPlantID = NULL_PLANT_ID;
	int mFoundationPlantID = NULL_PLANT_ID;

public:
	int mRow = 0;		// 行
	int mColumn = 0;	// 列

	Cell(int row, int column, const Vector& position,
		const Vector& colliderSize = Vector(CELL_COLLIDER_SIZE_X, CELL_COLLIDER_SIZE_Y),
		const std::string& tag = "Cell")
		: mColliderSize(colliderSize), mRow(row), mColumn(column)
	{
		this->mObjectType = ObjectType::OBJECT_NONE;
		SetTag(tag);
		SetName("Cell_" + std::to_string(row) + "_" + std::to_string(column));

		this->CreateTransform(position);
		auto* collider = CreateCollider(mColliderSize);
		collider->isTrigger = true;
		collider->isStatic = true;
		collider->layerMask = CollisionLayer::NONE;
		collider->collisionMask = CollisionLayer::NONE;
		auto* clickable = this->CreateClickable();
		if (!clickable) return;
		clickable->ChangeCursorOnHover = false;
		clickable->onClick = [this]() {
			if (OnCellClicked) {
				OnCellClicked(mRow, mColumn);
			}
			};
	}

	// 获取格子世界位置
	Vector GetWorldPosition() const
	{
		if (GetTransform())
		{
			return GetTransform()->GetPosition();
		}

		float x = CELL_INITALIZE_POS_X + mColumn * CELL_COLLIDER_SIZE_X;
		float y = CELL_INITALIZE_POS_Y + mRow * CELL_COLLIDER_SIZE_Y;
		return Vector(x, y);
	}

	// 获取格子中心位置
	Vector GetCenterPosition() const
	{
		Vector worldPos = GetWorldPosition();
		return Vector(
			worldPos.x + mColliderSize.x / 2,
			worldPos.y + mColliderSize.y / 2
		);
	}

	// 设置植物ID
	/** 水面承载层、普通层、南瓜外壳层与短时飞行覆盖层分开存储，避免组合植物互相覆盖。 */
	void SetUnderPlantID(int plantID) { mUnderPlantID = plantID; }
	void SetNormalPlantID(int plantID) { mNormalPlantID = plantID; }
	void SetPumpkinPlantID(int plantID) { mPumpkinPlantID = plantID; }
	void SetOverlayPlantID(int plantID) { mOverlayPlantID = plantID; }
	void SetFoundationPlantID(int plantID) { mFoundationPlantID = plantID; }

	// 获取植物ID
	int GetUnderPlantID() const { return mUnderPlantID; }
	int GetNormalPlantID() const { return mNormalPlantID; }
	int GetPumpkinPlantID() const { return mPumpkinPlantID; }
	int GetOverlayPlantID() const { return mOverlayPlantID; }
	int GetFoundationPlantID() const { return mFoundationPlantID; }
	/** 全层快照；前四层顺序保持既有约定，末层为水岸花盆基座。 */
	std::array<int, 5> GetPlantIDs() const {
		return {mUnderPlantID, mNormalPlantID, mPumpkinPlantID, mOverlayPlantID, mFoundationPlantID};
	}
	int GetTopPlantID() const {
		if (mPumpkinPlantID != NULL_PLANT_ID) return mPumpkinPlantID;
		if (mNormalPlantID != NULL_PLANT_ID) return mNormalPlantID;
		return mUnderPlantID != NULL_PLANT_ID ? mUnderPlantID : mFoundationPlantID;
	}

	// 清除植物ID
	void ClearUnderPlantID() { mUnderPlantID = NULL_PLANT_ID; }
	void ClearNormalPlantID() { mNormalPlantID = NULL_PLANT_ID; }
	void ClearPumpkinPlantID() { mPumpkinPlantID = NULL_PLANT_ID; }
	void ClearOverlayPlantID() { mOverlayPlantID = NULL_PLANT_ID; }
	void ClearFoundationPlantID() { mFoundationPlantID = NULL_PLANT_ID; }

	// 检查格子是否为空
	bool IsEmpty() const
	{
		return mUnderPlantID == NULL_PLANT_ID
			&& mNormalPlantID == NULL_PLANT_ID
			&& mPumpkinPlantID == NULL_PLANT_ID
			&& mOverlayPlantID == NULL_PLANT_ID
			&& mFoundationPlantID == NULL_PLANT_ID;
	}

	// 检查点是否在格子内
	bool ContainsPoint(const Vector& point) const
	{
		if (const auto* collider = GetCollider())
		{
			return collider->ContainsPoint(point);
		}
		else
		{
			return false;
		}
	}

	// 设置点击回调
	void SetClickCallback(std::function<void(int, int)> callback)
	{
		OnCellClicked = callback;
	}
};
#endif
