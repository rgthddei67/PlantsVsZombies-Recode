#include "Board.h"
#include "PolarFinaleVisuals.h"
#include "Graphics.h"
#include "Game/Zombie/AuroraPriestZombie.h"
#include "Game/Zombie/AuroraPriestRules.h"
#include "Game/Zombie/PolarClockmakerZombie.h"
#include "Game/Zombie/PolarClockRules.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kChannelDiameter = 174.0f; // 施法主体的世界像素直径，亮环实际占约七成
constexpr float kMarkDiameter = 82.0f; // 每个时间锚目标的钟印尺寸，世界像素
constexpr float kPortalWidth = 112.0f; // 裂口承载宽度，世界像素，透明区域留给外侧辉光
constexpr float kPortalHeight = 162.0f; // 裂口承载高度，世界像素
constexpr float kPortalCenterY = -25.0f; // 裂口中心相对格心向上偏移，世界像素
constexpr float kArrivalSeconds = .65f; // 召唤成功后的收口余辉，游戏秒
constexpr float kRewindSeconds = .85f; // 回溯结算后的倒流光痕，游戏秒
constexpr float kTrailHeight = 46.0f; // 回溯光痕最大宽度，世界像素
constexpr std::size_t kBurstLimit = 192; // 仅瞬态视觉上限；同帧多时间锚不无限累积提交
}

void Board::AddPolarFinaleBurst(const Vector& from,const Vector& to,bool temporal) {
	if (mPolarFinaleBursts.size()>=kBurstLimit) mPolarFinaleBursts.erase(mPolarFinaleBursts.begin());
	mPolarFinaleBursts.push_back({from,to,0,temporal});
}

/** 短暂结算余辉仅随游戏时间前进；持续特效不另存一份技能倒计时。 */
void Board::UpdatePolarFinaleVisuals(float deltaTime) {
	mPolarFinaleVisualTime=std::fmod(mPolarFinaleVisualTime+deltaTime,3600.0f);
	for (auto& burst:mPolarFinaleBursts) burst.age+=deltaTime;
	std::erase_if(mPolarFinaleBursts,[](const PolarFinaleBurst& burst) {
		return burst.age>=(burst.temporal ? kRewindSeconds : kArrivalSeconds);
	});
}

void Board::DrawPolarFinaleEffects(Graphics* g) const {
	mPolarFinaleRenderCounts.fill(0);
	if (!g || mTrophySpawned || mBoardState!=BoardState::GAME) return;
	using Effect=Graphics::PolarEffect;
	const BlendMode savedBlend=g->GetBlendMode();
	g->SetBlendMode(BlendMode::Alpha);
	const auto draw=[&](Effect effect,const Vector& center,float w,float h,float time,float progress,float opacity=1.0f) {
		if (g->DrawPolarEffect(effect,center.x-w*.5f,center.y-h*.5f,w,h,time,progress,opacity))
			++mPolarFinaleRenderCounts[static_cast<int>(effect)];
	};
	// 实体只负责提供已批准的施法阶段，断头、破甲、中断或魅惑后的撤销立即体现在这一帧。
	for (int id:mEntityRegistry.GetAllZombieIDs()) {
		const Zombie* zombie=mEntityRegistry.GetZombie(id);
		if (!zombie || !zombie->IsActive() || zombie->IsDying() || !zombie->HasHead() || zombie->IsMindControlled()) continue;
		if (zombie->mZombieType!=ZombieType::ZOMBIE_AURORA_PRIEST
			&& zombie->mZombieType!=ZombieType::ZOMBIE_POLAR_CLOCKMAKER) continue;
		Effect effect;
		float progress;
		if (const auto* priest=dynamic_cast<const AuroraPriestZombie*>(zombie);
			priest && priest->GetRitualPhase()==AuroraPriestZombie::RitualPhase::WINDUP) {
			effect=Effect::AuroraChannel;
			progress=1.0f-priest->GetRitualRemaining()/AuroraPriestRules::Windup;
		} else if (const auto* clock=dynamic_cast<const PolarClockmakerZombie*>(zombie);
			clock && clock->GetClockPhase()==PolarClockmakerZombie::ClockPhase::WINDUP) {
			effect=Effect::ClockChannel;
			progress=1.0f-clock->GetClockRemaining()/PolarClockRules::Windup;
		} else continue;
		const float scale=zombie->GetTransform()->GetScale();
		const Vector center=PolarFinaleVisuals::ChannelCenter(*zombie);
		draw(effect,center,kChannelDiameter*scale,kChannelDiameter*scale,mPolarFinaleVisualTime,progress);
	}
	for (const auto& rift:mPendingAuroraRifts) {
		const Vector center=GetCellCenterPosition(rift.row,rift.column)+Vector(0,kPortalCenterY);
		draw(Effect::AuroraRift,center,kPortalWidth,kPortalHeight,mPolarFinaleVisualTime+rift.transactionID*.17f,
			1.0f-rift.timer/AuroraPriestRules::Unfold);
	}
	for (const auto& anchor:mTemporalAnchors) {
		for (const auto& target:anchor.targets) {
			if (target.irreversible) continue;
			const Zombie* zombie=mEntityRegistry.GetZombie(target.zombieID);
			const Vector center=zombie && zombie->IsActive() && !zombie->IsDying() && !zombie->IsMindControlled()
				? PolarFinaleVisuals::ClockCenter(*zombie) : Vector(target.x,GetRowCenterYAtX(target.row,target.x)
					+PolarFinaleVisuals::AbsentFootY-PolarFinaleVisuals::MarkLift);
			const float remaining=anchor.timer/PolarClockRules::AnchorDuration;
			draw(Effect::ClockMark,center,kMarkDiameter,kMarkDiameter,
				PolarClockRules::AnchorDuration-anchor.timer,remaining);
		}
	}
	for (const auto& burst:mPolarFinaleBursts) {
		if (!burst.temporal) {
			draw(Effect::AuroraArrival,burst.to+Vector(0,kPortalCenterY),kPortalWidth,kPortalHeight,
				mPolarFinaleVisualTime,burst.age/kArrivalSeconds);
			continue;
		}
		const float progress=burst.age/kRewindSeconds;
		const Vector to=burst.to, from=burst.from, delta=to-from;
		const float length=std::hypot(delta.x,delta.y);
		if (length>8.0f) {
			g->PushTransform();
			g->Translate(from.x,from.y);
			g->Rotate(std::atan2(delta.y,delta.x)*180.0f/3.14159265f);
			draw(Effect::TemporalTrail,Vector(length*.5f,0),length,kTrailHeight,mPolarFinaleVisualTime,progress);
			g->PopTransform();
		}
		draw(Effect::ClockRewind,to,142,142,burst.age,progress);
	}
	g->SetBlendMode(savedBlend);
}
