#include "SunShroom.h"
#include "Game/Board/Board.h"
#include "../ShadowComponent.h"

void SunShroom::SetupPlant()
{
	auto shadow = GetShadow();
	shadow->SetScale(Vector(0.62f, 0.62f));
	shadow->SetOffset(Vector(2, 32));
	Shroom::SetupPlant();
}

void SunShroom::PlantUpdate()
{
	const float weatherActionDelta = GetWeatherActionDeltaTime();
	mGrowTimer += weatherActionDelta;
	if (mGrowTimer >= GROW_TIME && !mIsGrown) {
		AudioSystem::PlaySound("SOUND_PLANTGROW", 0.3f);
		mGrowTimer = 0.0f;
		mIsGrown = true;
		PlayTrackOnce("anim_grow", "anim_bigidle", 0.0f, 0.2f, 0.0f);
	}

	if (!mIsGlowingForProduction) {
		// 正常计时生产
		mProduceTimer += GetSunProductionDeltaTime();
		if (mProduceTimer >= PRODUCE_TIME) {
			SetGlowingTimer(0.75f);
			mIsGlowingForProduction = true;
			mProductionGlowStartTimer = mProduceTimer;  // 记录开始发光的时间点
			mGlowProducesGrownSun = mIsGrown;           // 发光启动时锁定大/小阳光，避免发光中途长大导致这一颗错付成熟值
		}
	}
	else {
		// 正在为生产而发光
		if (mProduceTimer >= mProductionGlowStartTimer + 0.55f) {
			// 发光 0.55s 结束，生产阳光
			float offsetX = GameRandom::Range(-30.0f, 35.0f);
			Vector position = GetPosition();
			// 幼年产小阳光(15)，长大后产普通阳光(25)；成熟度按本轮发光"启动时"快照判定
			if (mGlowProducesGrownSun) {
				mBoard->CreateSun(position.x + offsetX, position.y, true);
			}
			else {
				mBoard->CreateSmallSun(position.x + offsetX, position.y, true);
			}

			// 重置状态
			mProduceTimer = 0.0f;
			mIsGlowingForProduction = false;
			mProductionGlowStartTimer = 0.0f;
		}
		else {
			// 继续计时
			mProduceTimer += DeltaTime::GetDeltaTime() * GetFertilitySpeedMultiplier();
		}
	}
}
