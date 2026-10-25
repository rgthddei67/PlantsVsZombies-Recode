#pragma once
#include "SunFlower.h"

/** 浮光葵：复用向日葵生产状态，兑现时按真实水陆决定产量。 */
class FloatSunflower final : public SunFlower {
public:
    using SunFlower::SunFlower;
    bool IsWaterForm() const;
    void PlantUpdate() override;
    void LoadExtraData(const nlohmann::json& j) override;
protected:
    void SetupPlant() override;
    float GetProductionInterval() const override;
    int GetProductionSunCount() const override;
    float GetProductionGlowLeadTime() const override { return PRODUCTION_GLOW_TIME; }
private:
    void SyncLeaves() const;
};
