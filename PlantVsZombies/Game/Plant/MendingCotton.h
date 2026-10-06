#pragma once
#include "Plant.h"
#include "MendingCottonRules.h"

/** 缝补棉花：周围八格最低生命比例单体治疗，南瓜和本体独立参与。 */
class MendingCotton final : public Plant {
public:
    using Plant::Plant;
    void PlantUpdate() override;
    void Draw(Graphics* g) override;
    void SaveExtraData(nlohmann::json& j) const override;
    void LoadExtraData(const nlohmann::json& j) override;
    float GetHealRemaining() const { return mRemaining; }
    int GetHealCount() const { return mHealCount; }
    int GetLastHealedID() const { return mLastHealedID; }
protected:
    void SetupPlant() override;
private:
    float mRemaining = MendingCottonRules::Interval;
    float mThreadRemaining = 0;
    Vector mThreadEnd;
    int mHealCount = 0, mLastHealedID = NULL_PLANT_ID;
};
