#include "Zombie.h"
#include "Game/Board/Board.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr float kDuration=6.0f; // 每个独立鼓手来源的效果持续游戏秒
	constexpr float kMoveBonus=0.5f; // 每层鼓舞额外移动倍率，来源之间加算
	constexpr float kBiteBonus=0.75f; // 每层鼓舞额外啃食频率倍率，不提高单次伤害
}

void Zombie::ApplyDrumInspiration(int sourceID)
{
	if (sourceID < 0 || sourceID == mZombieID || IsMindControlled() || !IsActive() || mIsDead || mIsDying) return;
	if (!mDrumInspiration) mDrumInspiration=std::make_unique<DrumInspirationState>();
	auto& layers=mDrumInspiration->layers;
	auto it=std::find_if(layers.begin(),layers.end(),[sourceID](const auto& layer){return layer.first==sourceID;});
	if (it==layers.end()) layers.emplace_back(sourceID,kDuration);
	else it->second=kDuration;
	UpdateAnimSpeed();
}

int Zombie::GetDrumInspirationStacks() const
{
	return mDrumInspiration && IsActive() && !mIsDead && !mIsDying && !IsMindControlled()
		? static_cast<int>(mDrumInspiration->layers.size()) : 0;
}

float Zombie::GetDrumMoveMultiplier() const { return 1.0f+kMoveBonus*GetDrumInspirationStacks(); }
float Zombie::GetDrumBiteMultiplier() const { return 1.0f+kBiteBonus*GetDrumInspirationStacks(); }

void Zombie::UpdateDrumInspiration(float delta)
{
	if (!mDrumInspiration) return;
	if (IsMindControlled() || mIsDead || mIsDying) { mDrumInspiration.reset(); UpdateAnimSpeed(); return; }
	auto& layers=mDrumInspiration->layers;
	for (auto& layer : layers) layer.second-=delta;
	layers.erase(std::remove_if(layers.begin(),layers.end(),[](const auto& layer){return layer.second<=0.0f;}),layers.end());
	if (layers.empty()) mDrumInspiration.reset();
	// 啃食和步行可能在碰撞回调中切换；活动鼓舞每步按当前动作刷新，避免啃食倍率残留到走路。
	UpdateAnimSpeed();
}

void Zombie::SaveDrumInspiration(nlohmann::json& j) const
{
	j["drumInspiration"]=nlohmann::json::array();
	if (mDrumInspiration) for (const auto& layer : mDrumInspiration->layers)
		j["drumInspiration"].push_back({{"source",layer.first},{"remaining",layer.second}});
}

void Zombie::LoadDrumInspiration(const nlohmann::json& j)
{
	mDrumInspiration.reset();
	if (IsMindControlled() || !j.contains("drumInspiration") || !j["drumInspiration"].is_array()) return;
	for (const auto& saved : j["drumInspiration"]) {
		if (!saved.is_object() || !saved.contains("source") || !saved["source"].is_number_integer()
			|| !saved.contains("remaining") || !saved["remaining"].is_number()) continue;
		const int id=saved["source"].get<int>();
		const float duration=saved["remaining"].get<float>();
		if (id<0 || id==mZombieID || !std::isfinite(duration) || duration<=0.0f) continue;
		if (!mDrumInspiration) mDrumInspiration=std::make_unique<DrumInspirationState>();
		auto& layers=mDrumInspiration->layers;
		auto it=std::find_if(layers.begin(),layers.end(),[id](const auto& layer){return layer.first==id;});
		if (it==layers.end()) layers.emplace_back(id,std::min(duration,kDuration));
		else it->second=std::max(it->second,std::min(duration,kDuration));
	}
}

float Zombie::GetAmberMovementMultiplier() const
{
	// 普通冰减速已经由动画和逻辑步一起缩放，强于地衣，不能再相乘。
	return !mBoard || mCooldownTimer>0.0f ? 1.0f
		: AmplifySpeedMultiplierForGoldenIce(mBoard->GetGroundSlowFactor(*this));
}
