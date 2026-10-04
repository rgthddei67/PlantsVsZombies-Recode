#pragma once

#include "Plant.h"

/**
 * @brief 经典叶子保护伞：以逻辑九宫格保护植物免受蹦极抓取和篮球伤害。
 */
class UmbrellaLeaf final : public Plant {
public:
	static constexpr int ProtectionCells=1; // 自身与相邻逻辑格；实体资格与未来建设画像共用半径
	using Plant::Plant;

	/** 推进展开倒计时，并在阻挡轨结束后回到待机防御状态。 */
	void PlantUpdate() override;
	bool ProtectsCellFromAirborneThreat(int row, int column) const override;
	AirborneDefenseState ActivateAirborneDefense() override;
	AirborneDefenseState GetAirborneDefenseState() const override { return mDefenseState; }
	float GetAirborneDefenseActivationTime() const override { return mActivationTimer; }
	/** 保存防御阶段与尚未完成的展开时间。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复防御阶段，并按通用 Animator 已恢复的轨道修复旧档组合。 */
	void LoadExtraData(const nlohmann::json& j) override;

protected:
	/** 配置经典生命值和半尺寸阴影。 */
	void SetupPlant() override;

private:
	/** 以当前 Animator 轨道恢复防御终态，不播放音效或重新触发威胁。 */
	void NormalizeLoadedDefenseState();

	AirborneDefenseState mDefenseState = AirborneDefenseState::INACTIVE;
	float mActivationTimer = 0.0f;
};
