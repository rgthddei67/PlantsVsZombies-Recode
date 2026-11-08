#pragma once

#include "ZombieType.h"
#include <string>

/** 普通骨架的品种服装；不包含生命、防具或技能状态，资源由确定性资产脚本生成。 */
namespace ZombieIdentityVisuals {

struct Profile {
	const char* stem;
	const char* reanimation;
	bool hasHeadgear;
	float headgearX;
	float headgearY;
};

/** 没有独立服装配置的品种返回 nullptr，继续消费原有外观与断肢实现。 */
const Profile* Find(ZombieType type);
std::string TextureKey(const Profile& profile, const char* part);
std::string EffectName(const Profile& profile, bool head, bool mirrored);
/** 检查整套衣服、残端、头饰、左右离体贴图与独立 reanim 的运行时注册。 */
bool ResourcesReady(const Profile& profile);

}
