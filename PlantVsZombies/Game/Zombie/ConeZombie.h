#pragma once
#ifndef _CONEZOMBIE_H
#define _CONEZOMBIE_H
#include "Zombie.h"

class ConeZombie : public Zombie {
public:
	using Zombie::Zombie;
	ArmorBrokenState mHelmStage = ArmorBrokenState::NO_BROKEN;
	void HelmDrop() override;

	void SaveExtraData(nlohmann::json& j) const override {
		j["helmStage"] = static_cast<int>(mHelmStage);
	}

	void LoadExtraData(const nlohmann::json& j) override {
		mHelmStage = static_cast<ArmorBrokenState>(
			j.value("helmStage", static_cast<int>(ArmorBrokenState::NO_BROKEN))
			);
	}

	void ZombieItemUpdate() const override;

protected:
	void SetupZombie() override;
	void CheckHelmImage() override;
	/** 返回当前路障阶段的运行时纹理键，供数值变体只替换护具图。 */
	virtual const std::string& GetConeTextureKey(ArmorBrokenState stage) const;
	/** 返回路障脱落粒子配置名，供数值变体保持掉落表现与自身贴图一致。 */
	virtual const char* GetConeDropEffectName() const { return "ZombieConeOff"; }
	/** 头戴护具使用 anim_cone；手持钻机等派生装备自行提供脱落锚点。 */
	virtual void EmitConeDropParticle() const;
};

#endif
