#pragma once
#ifndef _SHOVEL_H
#define _SHOVEL_H

#include "GameObject.h"
#include "Definit.h"
#include "./Plant/Plant.h"

class Board;
struct Texture;

enum class ShovelState {
	IDLE,
	ACTIVE,
};

class Shovel : public GameObject {
public:
	Shovel(Board* board);
	void Update() override;
	void Draw(Graphics* g) override;

	/** 按鼠标在格内的可见区域选择待铲层，并持续高亮同一目标。 */
	void CheckPlant();
	/** 按正式鼠标命中规则铲除目标并归位；交互试玩复用，不绕过分层和冰封限制。 */
	bool TryShovelAtPosition(const Vector& position);
	void Activate();
	ShovelState GetState() const { return mState; }
	void SetHomePosition(const Vector& pos);
	void ReturnHome();

	void Die();

private:
	Board* mBoard = nullptr;
	const Texture* mTexture = nullptr;
	Vector           mPosition;
	Vector           mHomePosition;
	ShovelState      mState = ShovelState::IDLE;
	Plant* mPlant = nullptr;	// 选中的植物
};

#endif
