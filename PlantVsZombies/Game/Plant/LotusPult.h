#pragma once
#include "CabbagePult.h"

/** 两栖投手：地上单体、水中分裂；共用卷心菜的动画、发射事件和周期存档。 */
class LotusPult final : public CabbagePult {
public:
    using CabbagePult::CabbagePult;
    void PlantUpdate() override;
    void Draw(Graphics* g) override;
    bool IsWaterForm() const;
protected:
    void SetupPlant() override;
    void FireCabbage() override;
    float NextShootInterval() const override;
private:
    /** 水陆形态只读取当前地形，切换不改能力计时和生命。 */
    void SyncForm() const;
};
