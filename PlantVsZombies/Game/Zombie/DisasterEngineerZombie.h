#pragma once
#include "Zombie.h"

/** 防灾工程师：满罐保护附近工人一次灰烬攻击，空罐后从所属钱包付费装填。 */
class DisasterEngineerZombie final : public Zombie {
public:
	using Zombie::Zombie;
	void Update() override;
	void Draw(Graphics* g) override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	bool HasFullCanister() const { return mFull; }
	float GetReloadRemaining() const { return mReloadRemaining; }
	bool IsReloadPaid() const { return mReloadPaid; }
	int GetProtectionUses() const { return mProtectionUses; }
	/** 只由完整灰烬事务调用一次；死亡前已提交的本次保护不会撤回。 */
	void ConsumeCanister();
	/** 返回按距离及稳定ID选出的同行最近三名工人，供正式事务和保护标志共用。 */
	std::vector<int> GetProtectedWorkerIDs() const;
protected:
	void SetupZombie() override;
private:
	/** 液位由正式罐状态派生，挂在身体轨道并随之运动。 */
	void SyncCanister();
	bool mFull = true, mReloadPaid = false;
	float mReloadRemaining = 0;
	int mProtectionUses = 0;
	int mVisualStage = -1;
};
