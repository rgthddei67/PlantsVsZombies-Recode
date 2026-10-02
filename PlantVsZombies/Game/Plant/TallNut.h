#pragma once

#include "WallNut.h"

/**
 * @brief 经典高坚果：拥有高额生命，阻拦跳跃，并锚定阵风中的植物格。
 */
class TallNut final : public WallNut {
public:
	using WallNut::WallNut;
	bool SupportsLadderPlacement() const override { return true; }

	bool BlocksZombieJump(ZombieJumpType jumpType) const override;
	void OnZombieJumpBlocked(ZombieJumpType jumpType) override;
	bool AnchorsPlantCellAgainstTyphoon() const override { return true; }
	void OnTyphoonPlantImpact(bool showFeedback) override;

protected:
	void SetupPlant() override;
	const std::string& GetBodyTextureKey() const override;
	const std::string& GetCrackedTextureKey(int damageStage) const override;
	const char* GetDamageTrackName() const override { return "anim_idle"; }
	float GetCrackParticleOffsetY() const override { return -72.0f; }

private:
	/** 播放高坚果成功拦截跳跃或阵风撞击时共用的 Bonk 与星光反馈。 */
	void PlayBlockFeedback();
};
