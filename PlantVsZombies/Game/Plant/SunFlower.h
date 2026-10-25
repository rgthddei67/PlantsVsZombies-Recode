#pragma once
#ifndef _SUNFLOWER_H
#define _SUNFLOWER_H

#include "Plant.h"
#include "Game/Board/Board.h"
#include "../../DeltaTime.h"

class SunFlower : public Plant
{
protected:
	static constexpr float PRODUCE_TIME = 20.0f; // 普通向日葵两轮生产之间的基础游戏秒数
	static constexpr float INITIAL_PRODUCE_TIMER = 15.0f; // 新种普通向日葵的初始进度，保留约 5 秒首轮等待
	static constexpr float PRODUCTION_GLOW_TIME = 0.55f; // 达到生产阈值后的发光结算时长，单位：游戏秒

	float mProduceTimer = INITIAL_PRODUCE_TIMER;
	bool mIsGlowingForProduction = false;  // 标记是否正在为生产发光
	float mProductionGlowStartTimer = 0.0f;  // 发光开始时间

	/** 返回本品种两轮生产之间的基础游戏秒数。 */
	virtual float GetProductionInterval() const { return PRODUCE_TIME; }
	/** 返回一次生产同时生成的普通阳光数量。 */
	virtual int GetProductionSunCount() const { return 1; }
	/** 生产发光提前量；默认保持经典额外等待，新品种可将发光计入完整周期。 */
	virtual float GetProductionGlowLeadTime() const { return 0.0f; }

public:
	using Plant::Plant;

	float GetProduceTimer() const { return mProduceTimer; }
	float GetProduceInterval() const { return GetProductionInterval(); }
	int GetProduceSunCount() const { return GetProductionSunCount(); }
	bool IsGlowingForProduction() const { return mIsGlowingForProduction; }

	void SaveExtraData(nlohmann::json& j) const override {
		j["produceTimer"] = mProduceTimer;
		j["isGlowingForProduction"] = mIsGlowingForProduction;
		j["productionGlowStartTimer"] = mProductionGlowStartTimer;
	}

	void LoadExtraData(const nlohmann::json& j) override {
		mProduceTimer = j.value("produceTimer", INITIAL_PRODUCE_TIMER);
		mIsGlowingForProduction = j.value("isGlowingForProduction", false);
		mProductionGlowStartTimer = j.value("productionGlowStartTimer", 0.0f);
	}

	/** 推进生产进度，并在发光结束时按品种数量生成普通阳光。 */
	void PlantUpdate() override
	{
		if (!mIsGlowingForProduction) {
			// 正常计时生产
			mProduceTimer += GetSunProductionDeltaTime();
			if (mProduceTimer >= GetProductionInterval() - GetProductionGlowLeadTime()) {
				SetGlowingTimer(0.75f);
				mIsGlowingForProduction = true;
				mProductionGlowStartTimer = mProduceTimer;  // 记录开始发光的时间点
			}
		}
		else {
			// 正在为生产而发光
			if (mProduceTimer >= mProductionGlowStartTimer + PRODUCTION_GLOW_TIME) {
				// 每颗阳光独立抽取横向起点，使双子同轮产物不会完全重叠。
				Vector position = GetPosition();
				for (int i = 0; i < GetProductionSunCount(); ++i) {
					const float offsetX = GameRandom::Range(-30.0f, 35.0f);
					mBoard->CreateSun(
						position.x + offsetX,
						position.y,
						true);
				}

				// 重置状态
				mProduceTimer = 0.0f;
				mIsGlowingForProduction = false;
				mProductionGlowStartTimer = 0.0f;
			}
			else {
				// 继续计时
				mProduceTimer += GetProductionGlowLeadTime() > 0 ? GetSunProductionDeltaTime()
					: DeltaTime::GetDeltaTime() * GetIndependentActionSpeedMultiplier();
			}
		}
	}
};

#endif
