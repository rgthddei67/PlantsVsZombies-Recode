#pragma once
#include "Plant.h"
#include "AnchorLotusRules.h"

/** 水生肉盾：周期性束缚前方最近的单个水中敌人，不禁止目标攻击或施法。 */
class AnchorLotus final : public Plant {
public:
    using Plant::Plant;
    void PlantUpdate() override;
    void Draw(Graphics* g) override;
    void Die() override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetRootCooldown() const { return mCooldown; }
    int GetRootTargetID() const { return mTargetID; }
    int GetRootCount() const { return mRootCount; }
protected:
    void SetupPlant() override;
private:
    float mCooldown = 0;
    int mTargetID = -1024;
    int mRootCount = 0;
};
