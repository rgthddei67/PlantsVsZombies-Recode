#pragma once
#include <algorithm>
#include <vector>

/** 冰仓坚果的承伤与付费修复参数；生产和攻击加速不改变这些计时。 */
namespace IceStorageNutRules {
constexpr int kHealth = 8000; // 本体最大生命
constexpr int kCrushDamage = 1000; // 每次巨人砸击或车辆碾压的基础伤害
constexpr float kInvulnerability = 5.0f; // 达到受伤门槛后的无敌游戏秒数
constexpr float kProtectionCooldown = 7.5f; // 无敌结束后开始计算的护体冷却，游戏秒
constexpr float kDamageWindow = 1.5f; // 技能就绪期间滚动统计实际承伤的窗口，游戏秒
constexpr float kDamageThreshold = 1000.0f; // 窗口内实际损血达到此值时触发护体
constexpr float kVehicleRetreatCells = 1.0f; // 成功挡车后向右推退距离，格
constexpr int kRepairHealth = 1000; // 单次恢复生命；自动挡须缺少完整这一量
constexpr int kRepairIce = 20; // 冷藏站每次修复冰费
constexpr int kRepairSun = 100; // 其他地图每次修复阳光费
constexpr float kRepairCooldown = 10.0f; // 修复成功后的冷却游戏秒数

/** 实体和指挥官共用的护体时序；无敌、冷却和就绪统计互斥，不记录停用期伤害。 */
struct Protection {
	struct Hit { float remaining = 0.0f, damage = 0.0f; };
	float invulnerable = 0.0f;
	float cooldown = 0.0f;
	std::vector<Hit> hits;
	/** 推进游戏时间；跨越无敌终点时，只把剩余步长计入随后开始的冷却。 */
	void Advance(float delta) {
		if (invulnerable > 0.0f) {
			const float elapsed = (std::min)(delta, invulnerable);
			invulnerable = (std::max)(0.0f, invulnerable - elapsed);
			delta -= elapsed;
			if (invulnerable > 0.0f) return;
			cooldown = kProtectionCooldown;
		}
		if (cooldown > 0.0f) {
			cooldown = (std::max)(0.0f, cooldown - delta);
			hits.clear(); // 冷却结束从零统计，不能把冷却期间的伤害带入。
			return;
		}
		for (auto& hit : hits) hit.remaining -= delta;
		hits.erase(std::remove_if(hits.begin(), hits.end(), [](const Hit& hit) {
			return hit.remaining < 0.0f;
		}), hits.end());
	}
	/** 当前滚动窗口内实际损失的生命；回血不撤销已经发生的受伤。 */
	float RecentDamage() const {
		float total = 0.0f;
		for (const auto& hit : hits) total += hit.damage;
		return total;
	}
	/** 只由已扣血且仍存活的调用者记录实际损血；开启护体不返还触发一击。 */
	void RecordDamage(float actualDamage) {
		if (actualDamage <= 0.0f || invulnerable > 0.0f || cooldown > 0.0f) return;
		hits.push_back({kDamageWindow, actualDamage});
		if (RecentDamage() >= kDamageThreshold) {
			invulnerable = kInvulnerability;
			hits.clear();
		}
	}
};
}
