#pragma once
#ifndef _FUME_SHROOM_H
#define _FUME_SHROOM_H

#include "Shroom.h"

class Zombie;

class FumeShroom : public Shroom {
protected:
	float mCheckZombieTimer = 0.0f;
	float mShootTime = 1.5f;     // 射击间隔时间
	float mShootTimer = 1.0f;    // 射击计时器
	int   mFumeDamage = 20;      // 每次喷射伤害（约一发豌豆，原版 DoRowAreaDamage(20, ...)）
	float mFumeReach = 390.0f;   // 喷雾横向覆盖范围（约 4 格锥形；检测与命中共用同一距离）

	// 变种覆写点（寒冰大喷菇）：孢子云粒子名 / 命中每个僵尸后的追加效果
	virtual const char* FumeParticleName() const { return "FumeCloud"; }
	virtual void OnFumeHit(Zombie* zombie) {}

	virtual bool HasZombieInRow(); // 检测合法目标，变种可沿通路搜索
	/** 共用已批准的第 27 帧事件；变种只替换发射效果。 */
	virtual void FireFume();
	float FumeAttack();			// 按近到远结算喷雾；返回阻断点世界 X，负值表示未被阻断

	void SetupPlant() override;

public:
	using Shroom::Shroom;

	void SaveExtraData(nlohmann::json& j) const override {
		j["shootTimer"] = mShootTimer;
	}

	void LoadExtraData(const nlohmann::json& j) override {
		mShootTimer = j.value("shootTimer", 1.0f);
	}

	void PlantUpdate() override;
};

#endif
