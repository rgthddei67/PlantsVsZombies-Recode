#pragma once
#include "LilyPad.h"

/** 睡莲承载层紫卡；保留上层植物，以三行根刺周期攻击地面目标。 */
class ThornLily final : public LilyPad {
public:
    using LilyPad::LilyPad;
    void PlantUpdate() override;
    void Draw(Graphics* g) override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetAttackRemaining() const { return mAttackRemaining; }
    int GetStrikeCount() const { return mStrikes; }
protected:
    void SetupPlant() override;
    Vector GetHealthTextOffset() const override { return Vector(-21, 25); }
private:
    float mAttackRemaining = 0.0f;
    int mStrikes = 0;
};
