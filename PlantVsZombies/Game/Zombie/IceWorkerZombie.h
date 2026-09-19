#pragma once

#include "Zombie.h"
#include "Game/Board/IceProduction.h"

/** 制冰工：生成后持续生产，成长与阵营由本体拥有，库存结算交给 Board。 */
class IceWorkerZombie final : public Zombie {
public:
	using Zombie::Zombie;
	/** 在普通更新之外推进生产，避免啃食和普通减速改变生产周期。 */
	void Update() override;
	/** 保存剩余生产时间与已完成批次，不把未结算收入提前入账。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复并校验生产状态，不重新生产或重置成熟度。 */
	void LoadExtraData(const nlohmann::json& j) override;
	float GetIceRemaining() const { return mIceRemaining; }
	float GetNextIceYield() const { return mNextIceYield; }
	int GetIceBatches() const { return mIceBatches; }
	bool HasIceMachine() const;
protected:
	/** 复用普通僵尸事件与全部身体运动，仅附加制冰机装备。 */
	void SetupZombie() override;
private:
	/** 身体 follower 保留普通骨架的走路、啃食、断肢和死亡运动。 */
	void ConfigureMachine();
	float mIceRemaining = IceProduction::Interval;
	float mNextIceYield = IceProduction::InitialYield;
	int mIceBatches = 0;
};
