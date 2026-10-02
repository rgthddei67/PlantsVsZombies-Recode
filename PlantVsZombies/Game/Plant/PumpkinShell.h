#pragma once

#include "Plant.h"

/** 经典南瓜头：占独立外壳层，优先承伤，并把背片夹在同格植物后方。 */
class PumpkinShell : public Plant {
private:
	std::shared_ptr<Animator> mBackAnimator;
	int mDamageStage = -1;

public:
	using Plant::Plant;
	bool SupportsLadderPlacement() const override { return true; }

	/** 维护生命派生的破损材质与背片受击高亮。 */
	void PlantUpdate() override;
	/** 预览或压扁态自行补画背片；正式叠种由同格普通植物插入背片。 */
	void Draw(Graphics* g) override;
	/** 用同步 Animator 只画 Pumpkin_back，供格子组合分层绘制。 */
	void DrawStackBackground(Graphics* g) override;
	/** 走通用承伤链，并立即同步破损外观和完整外壳高亮。 */
	void TakeDamage(int damage, DamageSource source) override;
	/** 同步模仿者泛白到前脸与独立背片，避免组合绘制出现双色外壳。 */
	void SetImitatedAppearance(bool imitated = true) override;
	int GetDamageStage() const { return mDamageStage; }
	bool HasBackAnimator() const { return mBackAnimator != nullptr; }

	/** 破损阶段不单独入档；按通用生命值恢复终态。 */
	void LoadExtraData(const nlohmann::json&) override;

protected:
	/** 配置经典耐久、宽碰撞框、阴影与前后分层 Animator。 */
	void SetupPlant() override;
	/** 把外壳血量下移一行，避免与同格普通植物的血量重叠。 */
	Vector GetHealthTextOffset() const override;

private:
	/** 按已保存生命值恢复前脸材质；资源缺失时保留上一终态，避免假绿。 */
	void UpdateDamageTexture();
};
