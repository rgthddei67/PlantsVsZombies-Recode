#pragma once

#include "Shroom.h"
#include "../Zombie/MagneticItem.h"

#include <string>

/**
 * @brief 经典磁力菇：周期性吸走附近僵尸的一件金属防具或工具。
 */
class MagnetShroom : public Shroom {
public:
	using Shroom::Shroom;
    static constexpr float ForecastRecharge=15; // 普通磁力菇吸取后总充能游戏秒
    static constexpr float ForecastRadiusCells=3.375f, ForecastEatingRadiusCells=4; // 普通/啃食目标搜索格宽
    static constexpr int ForecastRowRadius=2; // 磁吸最多跨越的行数
    static constexpr float ForecastRowPenaltyCells=1; // 每跨一行增加的选靶格宽代价

	enum class Phase {
		READY,
		SUCKING,
		CHARGING,
	};

	Phase GetPhase() const { return mPhase; }
	const char* GetPhaseName() const;
	bool HasCapturedItem() const { return mHasCapturedItem; }
	const std::string& GetCapturedItemTextureKey() const {
		return mCapturedItem.textureKey;
	}
	float GetRechargeTimeRemaining() const { return mRechargeTime; }
	float GetCapturedItemDistance() const;
	Vector GetCapturedItemDestinationFromLogical() const {
		return GetCapturedItemDestination() - GetPosition();
	}

	void Draw(Graphics* g) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;

protected:
	void SetupPlant() override;
	void PlantUpdate() override;
	/** 返回从成功吸取当帧开始计算的总充能秒数。 */
	virtual float GetRechargeSeconds() const;
	virtual const char* GetShootingTrackName() const { return "anim_shooting"; }
	virtual const char* GetChargingTrackName() const { return "anim_nonactive_idle2"; }
	/** 僵尸装备已原子剥离且由植物接管后触发；场景扶梯不调用。 */
	virtual void OnZombieMagneticItemExtracted(
		const MagneticItem&, const Vector&, int) {}

private:
	/** 搜索原版上下两行范围内最近的合法金属目标并原子开始一次吸取。 */
	bool TryStartMagnetizing();
	/** 开始射击轨、品种总充能与离体物飞行；不依赖动画帧事件。 */
	void BeginMagnetizing(MagneticItem item);
	/** 在装备已完成卸除后精确扣除磁力菇本体生命，绕过南瓜与防御词条。 */
	void ApplyExtractionBacklash(int damage);
	/** 按原版每逻辑步靠近 5% 的指数曲线推进离体物。 */
	void UpdateCapturedItem();
	/** 充能完成后清空离体物并回到随机速率待机。 */
	void FinishRecharge();
	/** 返回当前随阵风/水面动态锚点移动的离体物目标位置。 */
	Vector GetCapturedItemDestination() const;
	void DrawCapturedItem(Graphics* g) const;

	Phase mPhase = Phase::READY;
	float mRechargeTime = 0.0f;
	bool mHasCapturedItem = false;
	MagneticItem mCapturedItem;
};
