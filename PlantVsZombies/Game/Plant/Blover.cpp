#include "Blover.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Zombie/BalloonZombie.h"
#include "../../ResourceManager.h"
#include "../../ResourceKeys.h"

#include <algorithm>
#include <stdexcept>

namespace {
	constexpr int kBlowEventFrame = 44;               // 主人指定的全局吹风结算帧，已是代码口径
	constexpr int kDisappearEventFrame = 61;          // 主人指定的全局消失帧，即 anim_loop 最后一帧
	constexpr float kBloverClipSpeed = 1.6f;          // 主人调整的两段动画统一播放倍率
	constexpr float kFlipPivotX = 40.0f;              // 三叶草局部视觉中线，用于朝屋后时水平翻转
	constexpr float kBloverSoundVolume = 0.5f;        // 原版 blover.ogg 单次播放音量
}

float Blover::GetForecastBlowDelay()
{
	const auto reanim = ResourceManager::GetInstance().GetReanimation(ResourceKeys::Reanimations::REANIM_BLOVER);
	if (!reanim) throw std::runtime_error("missing blover blow reanimation");
	const auto range = reanim->GetTrackFrameRange("anim_blow");
	if (range.first < 0 || range.second <= range.first || kBlowEventFrame < range.first
		|| kBlowEventFrame > range.second || reanim->mFPS <= 0)
		throw std::runtime_error("invalid blover blow frame range");
	return (kBlowEventFrame-range.first)/(reanim->mFPS*kBloverClipSpeed);
}

float Blover::GetForecastBlowRemaining() const
{
	if (mBlowTriggered) return 0;
	// 复用相同资源校验，避免缺失 clip 时静默构造一段虚假的吹风等待。
	const float duration = GetForecastBlowDelay();
	if (!mAnimator || GetCurrentTrackName() != "anim_blow") return 0;
	const auto reanim = mAnimator->GetReanimation();
	if (!reanim) throw std::runtime_error("missing live blover blow reanimation");
	return std::clamp((kBlowEventFrame-mAnimator->GetCurrentFrame())/(reanim->mFPS*kBloverClipSpeed),0.0f,duration);
}

void Blover::SetupPlant()
{
	ApplyDirectionPresentation();
	if (mIsPreview) return;

	// anim_blow 的活跃窗口是 33..51；44 不会被 idle(0..32) 或 loop(52..61) 扫到。
	mAnimator->AddFrameEvent(kBlowEventFrame, [this]() { TriggerBlow(); });
	mAnimator->AddFrameEvent(kDisappearEventFrame, [this]() { Die(); });
	// 原版在相邻包装轨边界直接切换；两段保持同速，返回混合显式为 0。
	PlayTrackOnce("anim_blow", "anim_loop",
		kBloverClipSpeed, 0.0f, kBloverClipSpeed, 0.0f);
}

void Blover::SetBlowDirection(WindDirection direction)
{
	if (direction != WindDirection::TOWARD_HOUSE
		&& direction != WindDirection::TOWARD_FRONT) {
		return;
	}
	mBlowDirection = direction;
	ApplyDirectionPresentation();
}

void Blover::TriggerBlow()
{
	if (mBlowTriggered || !mBoard) return;
	mBlowTriggered = true;

	AudioSystem::PlaySound(
		ResourceKeys::Sounds::SOUND_BLOVER, kBloverSoundVolume);

	// 全场逐行消费既有行桶；只处理仍在 FLYING 的气球，爆裂/落地阶段不受影响。
	for (int row = 0; row < mBoard->mRows; ++row) {
		mBoard->mEntityRegistry.ForEachZombieInRow(row, [this](Zombie* zombie) {
			auto* balloon = dynamic_cast<BalloonZombie*>(zombie);
			if (balloon) balloon->BlowAway(mBlowDirection);
			});
	}

	// 三叶草不直接改雾势或驱散量；只有当前确有台风时才改写风向权威。
	mBoard->RedirectTyphoonFromBlover(mBlowDirection);
}

void Blover::ApplyDirectionPresentation()
{
	if (mAnimator) {
		mAnimator->SetFlipX(
			mBlowDirection == WindDirection::TOWARD_HOUSE, kFlipPivotX);
	}
}

void Blover::SaveExtraData(nlohmann::json& j) const
{
	j["blowDirection"] = static_cast<int>(mBlowDirection);
	j["blowTriggered"] = mBlowTriggered;
}

void Blover::LoadExtraData(const nlohmann::json& j)
{
	const int direction = j.value("blowDirection",
		static_cast<int>(WindDirection::TOWARD_FRONT));
	SetBlowDirection(direction == static_cast<int>(WindDirection::TOWARD_HOUSE)
		? WindDirection::TOWARD_HOUSE : WindDirection::TOWARD_FRONT);
	mBlowTriggered = j.value("blowTriggered", false);
}
