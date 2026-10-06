#pragma once
#include "Plant.h"
#include "RainBambooRules.h"

/** 穿雨竹按雨势积蓄一发竹矛；就绪后等待可见的同行地面敌人。 */
class RainBamboo final : public Plant {
public:
    using Plant::Plant;
    void PlantUpdate() override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetCharge() const { return mCharge; }
    int GetShotCount() const { return mShots; }
protected:
    void SetupPlant() override;
private:
    float mCharge = 0;
    int mShots = 0;
};
