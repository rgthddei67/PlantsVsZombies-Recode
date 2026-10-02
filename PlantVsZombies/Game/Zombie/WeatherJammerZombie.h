#pragma once

#include "BucketZombie.h"

#include <memory>

class Animator;

/**
 * @brief 气象干扰僵尸：铁桶本体携带非磁性设备，停步为整栏天气黑障追加三十秒。
 * @details 完全进场即主动施法；黑障可由多只同类补时并截获期间新广播，但不改真实天气，且施法优先于移动和啃食。
 */
class WeatherJammerZombie final : public BucketZombie {
public:
	/** 只读出生运动画像；参数与本品种实际 Setup 共用，不生成实体或消费 RNG。 */
	static ZombieMovementRules::BirthProfile GetBirthMovementProfile();
	using BucketZombie::BucketZombie;

	enum class JammerPhase {
		READY,
		CHANNELING,
		REBOOTING,
		SPENT,
	};

	void Update() override;
	void Die() override;
	void SaveExtraData(nlohmann::json& j) const override;
	void LoadExtraData(const nlohmann::json& j) override;
	void ZombieItemUpdate() const override;
	float GetInterruptibleSpecialActionRemaining() const override;
	bool InterruptUncommittedSpecialAction() override;

	JammerPhase GetJammerPhase() const { return mJammerPhase; }
	float GetChannelRemaining() const { return mChannelRemaining; }
	float GetRebootRemaining() const { return mRebootRemaining; }
	int GetCommittedDisruptionMask() const { return mCommittedDisruptionMask; }
	bool HasPackAnimator() const { return static_cast<bool>(mPackAnimator); }
	bool HasTerminalAnimator() const { return static_cast<bool>(mTerminalAnimator); }

protected:
	void SetupZombie() override;
	void ZombieMove(float scaledDelta, Transform* transform) override;
	void ZombieUpdate(float scaledTime) override;
	void StartEat(ColliderComponent* other) override;
	void OnMindControlled() override;
	void ArmDrop() override;
	void HeadDrop() override;

private:
	bool CanBeginChannel() const;
	bool HasTerminalAbort() const;
	void BeginChannel();
	void CancelChannelForRetry(bool reboot);
	void SpendDevice();
	void CommitInterference();
	void ConfigureDeviceAnimators();
	void SyncDevicePresentation(bool restartTracks = false) const;
	const char* GetDeviceTrackName() const;

	JammerPhase mJammerPhase = JammerPhase::READY;
	float mChannelRemaining = 0.0f;
	float mRebootRemaining = 0.0f;
	int mCommittedDisruptionMask = 0;
	mutable std::shared_ptr<Animator> mPackAnimator;
	mutable std::shared_ptr<Animator> mTerminalAnimator;
	mutable JammerPhase mPresentedPhase = JammerPhase::SPENT;
};
