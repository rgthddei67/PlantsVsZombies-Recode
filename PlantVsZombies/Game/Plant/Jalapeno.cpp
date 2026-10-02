#include "Jalapeno.h"

#include "../../GameRandom.h"
#include "../../ResourceKeys.h"
#include "../AnimatedObject.h"
#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../GameObjectManager.h"
#include "../Zombie/Zombie.h"
#include "Reanimation/Animator.h"
#include <algorithm>

namespace {
	constexpr int kJalapenoIgniteFrame = 19;        // 主人指定的辣椒本体爆炸全局帧号
	constexpr float kAnimationFps = 12;            // Jalapeno.reanim 的基础帧率
	constexpr int kFireVanishFrame = 12;            // 主人指定的整行火焰消失全局帧号
	constexpr int kJalapenoDamage = 1800;            // 原版整行灰烬伤害
	constexpr int kFireSegmentCount = 12;            // 原版 DoFwoosh 横铺的火焰段数
	constexpr float kFireSpanX = 750.0f;             // 原版首尾火焰之间的水平跨度，单位：像素
	constexpr float kFireTopOffsetY = -10.0f;        // 火焰基点相对当前行格顶的垂直偏移，单位：像素
	constexpr float kFireMinSpeed = 0.7f;            // 原版每段火焰动画速率随机下限
	constexpr float kFireMaxSpeed = 1.3f;            // 原版每段火焰动画速率随机上限
	constexpr float kFireMinScale = 0.9f;            // 原版每段火焰随机缩放下限
	constexpr float kFireMaxScale = 1.1f;            // 原版每段火焰随机缩放上限

	/** 播放 Fire.reanim 的完整时间轴，并在第 12 帧主动回收。 */
	class JalapenoFire final : public AnimatedObject {
	public:
		JalapenoFire(Board* board, const Vector& position, float scale, bool flip)
			: AnimatedObject(ObjectType::OBJECT_PARTICLE, board, position,
				AnimationType::ANIM_JALAPENO_FIRE, ColliderType::BOX,
				Vector::zero(), Vector::zero(), scale, "JalapenoFire", false)
		{
			if (mAnimator) {
				mAnimator->SetFlipX(flip);
			}
		}

		/** 从 anim_flame 连续播到 anim_done 尾帧，保持主人指定的全局帧口径。 */
		void Start() override
		{
			AnimatedObject::Start();
			if (!mAnimator) {
				GameObjectManager::GetInstance().DestroyGameObject(this);
				return;
			}

			mAnimator->SetFrameRangeToDefault();
			mAnimator->AddFrameEvent(kFireVanishFrame, [this]() {
				GameObjectManager::GetInstance().DestroyGameObject(this);
				});
			SetAnimationSpeed(GameRandom::Range(kFireMinSpeed, kFireMaxSpeed));
			mAnimator->Play(PlayState::PLAY_ONCE);
		}
	};
}

float Jalapeno::GetExplosionTimeRemaining() const {
	if (!mAnimator || GetCurrentTrackName() != "anim_explode") return -1;
	return std::max(0.0f,kJalapenoIgniteFrame-mAnimator->GetCurrentFrame())/std::max(.001f,kAnimationFps*mAnimator->EffectiveSpeed());
}

void Jalapeno::SetupPlant()
{
	if (mIsPreview) return;

	// 裁剪版 Jalapeno.reanim 的 anim_explode 为全局 7..19；12fps 下正好蓄力 1 秒。
	PlayTrack("anim_explode", 1.0f);
	mAnimator->AddFrameEvent(kJalapenoIgniteFrame, [this]() {
		IgniteRow();
		});
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_REVERSE_EXPLOSION, 0.45f);
}

void Jalapeno::TakeDamage(int /*damage*/, DamageSource /*source*/)
{
	SetGlowingTimer(0.1f);
}

void Jalapeno::ResolveGargantuarSmash()
{
	if (GetCurrentTrackName() == "anim_explode") {
		IgniteRow();
		return;
	}
	Plant::ResolveGargantuarSmash();
}

void Jalapeno::IgniteRow()
{
	if (!mBoard) {
		Die();
		return;
	}

	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_JALAPENO, 0.55f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_JUICY, 0.45f);
	mBoard->ShakeBoard(3.0f, -4.0f);

	// C# DoFwoosh：十二段等距铺满战场，每段独立随机缩放、速率与水平翻面。
	for (int i = 0; i < kFireSegmentCount; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(kFireSegmentCount - 1);
		// 主人校准：基点从草坪逻辑左缘开始；贴图自身再向左伸约 45px，刚好覆盖边界。
		const float fireX = CELL_INITALIZE_POS_X + kFireSpanX * t;
		// C# 用每段 X+10 单独采样 GetPosYBasedOnRow；屋顶因此沿坡面铺开，平地结果不变。
		const float fireY = mBoard->GetRowCenterYAtX(mRow, fireX + 10.0f)
			- mBoard->GetCellHeight() * 0.5f + kFireTopOffsetY;
		if (mBoard->MineBlocksSegment(GetPosition(), Vector(fireX, GetPosition().y))) continue;
		const Vector position(fireX, fireY);
		GameObjectManager::GetInstance().CreateGameObjectImmediate<JalapenoFire>(
			LAYER_EFFECTS_WORLD, mBoard, position,
			GameRandom::Range(kFireMinScale, kFireMaxScale), GameRandom::Chance());
	}

	// 原版 BurnRow 先解冻/解减速，再按灰烬入口烧毁本行；魅惑僵尸不属于植物武器目标。
	mBoard->mEntityRegistry.ForEachZombieInRow(mRow, [this](Zombie* zombie) {
		if (!zombie || zombie->IsMindControlled()
			|| mBoard->MineBlocksSegment(GetPosition(), zombie->GetPosition())) return;
		zombie->RemoveColdEffects();
		zombie->TakePlantAshDamage(kJalapenoDamage);
		});
	// 原版辣椒把本行冰道计时压到 20cs；本项目统一使用秒。
	mBoard->ShortenIceTrail(mRow, 0.2f);
	mBoard->RemoveLaddersInRow(mRow);
	Die();
}
