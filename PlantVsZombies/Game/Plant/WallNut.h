#pragma once
#ifndef _WALLNUT_H
#define _WALLNUT_H

#include "Plant.h"

class WallNut : public Plant {
private:
	float mUpdateTextureTimer = 0.0f;
	bool mWasBeingEaten = false;
	int mDamageStage = -1;

public:
	using Plant::Plant;
	bool SupportsLadderPlacement() const override { return mPlantType == PlantType::PLANT_WALLNUT; }

	void PlantUpdate() override;
	void OnZombieBite(const Vector& eaterPosition) override;
	int GetDamageStage() const { return mDamageStage; }

	void SaveExtraData(nlohmann::json& j) const override
	{
		j["updateTextureTimer"] = mUpdateTextureTimer;
	}

	void LoadExtraData(const nlohmann::json& j) override
	{
		mUpdateTextureTimer = j.value("updateTextureTimer", 0.0f);
		UpdateTexture(false);
	}

protected:
	void SetupPlant() override;

	/** 按当前生命阶段更新坚果材质，并仅在实战跨入裂纹阶段时喷出一次大碎屑。 */
	void UpdateTexture(bool emitParticle = true);
	virtual const std::string& GetBodyTextureKey() const;
	virtual const std::string& GetCrackedTextureKey(int damageStage) const;
	virtual const char* GetDamageTrackName() const { return "anim_face"; }
	virtual float GetCrackParticleOffsetY() const { return -40.0f; }
	/** 品种外观条件变化时强制下一次 UpdateTexture 重新选择当前阶段材质。 */
	void InvalidateDamageTexture() { mDamageStage = -1; }
};

#endif
