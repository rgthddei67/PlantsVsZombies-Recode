#include "PoolNormalZombie.h"

#include "../AudioSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

namespace {
	constexpr int POOL_LAND_DEATH_FRAME = 216;     // 陆地死亡动画的结束帧
	constexpr int POOL_WATER_DEATH_FRAME = 283;    // 水中死亡动画的结束帧
	constexpr int POOL_FIRST_BITE_FRAME = 147;     // 泳池 reanim 第一次啃食命中帧
	constexpr int POOL_SECOND_BITE_FRAME = 168;    // 泳池 reanim 第二次啃食命中帧
}

/** 初始化泳池僵尸，并确保陆地稳态固定使用 walk2。 */
void PoolNormalZombie::SetupZombie()
{
	Zombie::SetupZombie();
	if (!mIsPreview) PlayWalkAnimation();
}

/** 注册泳池僵尸 reanim 的陆地/水中死亡终点和两次啃食命中帧。 */
void PoolNormalZombie::RegisterFrameEvents()
{
	mAnimator->AddFrameEvent(POOL_LAND_DEATH_FRAME, [this]() { Die(); });
	mAnimator->AddFrameEvent(POOL_WATER_DEATH_FRAME, [this]() { Die(); });
	mAnimator->AddFrameEvent(POOL_FIRST_BITE_FRAME, [this]() { EatTarget(); }, true);
	mAnimator->AddFrameEvent(POOL_SECOND_BITE_FRAME, [this]() { EatTarget(); }, true);
}

/** 水中使用 swim，离水后恢复固定的 walk2。 */
void PoolNormalZombie::PlayWalkAnimation(float blendTime)
{
	PlayTrack(mInPool ? "anim_swim" : "anim_walk2", 0.0f, blendTime);
}

/** 洪水可以在啃食/定身期间涨退；只同步泳圈覆盖，不重播动作或改变咬合时机。 */
void PoolNormalZombie::Update()
{
	Zombie::Update();
	SetPoolEatingTubeVisual(mInPool && mIsEating);
}

/** 水中啃食轨道会换回无涟漪泳圈；腿部由基类水面 Clip 统一裁掉。 */
void PoolNormalZombie::OnStartEating()
{
	SetPoolEatingTubeVisual(mInPool);
}

/** 停止啃食后撤销泳圈覆盖，让 walk2/swim 轨道继续按自身帧数据控制。 */
void PoolNormalZombie::OnStopEating()
{
	SetPoolEatingTubeVisual(false);
}

/** 掉头流血致死时按当前介质选择对应死亡轨道。 */
const char* PoolNormalZombie::GetDeathTrackName() const
{
	return mInPool ? "anim_waterdeath" : "anim_death";
}

/** 水中掉头只更新人物轨道并播放音效，不生成悬浮的陆地碎片。 */
void PoolNormalZombie::HeadDrop()
{
	if (!mInPool) {
		Zombie::HeadDrop();
		return;
	}
	if (!mHasHead) return;

	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_tongue", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

/** 水中断臂保留残臂贴图变化，但不生成陆地掉臂粒子。 */
void PoolNormalZombie::ArmDrop()
{
	if (!mInPool) {
		Zombie::ArmDrop();
		return;
	}
	if (!mHasArm) return;

	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_outerarm_upper", ResourceManager::GetInstance().
		GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_OUTERARM_UPPER2));
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

/** 保存泳池介质状态，使读档后死亡轨道和稳态动画保持一致。 */
void PoolNormalZombie::SaveExtraData(nlohmann::json& j) const
{
	j["inPool"] = mInPool;
}

/** 兼容旧档：优先读取旧 extraData 字段；缺少时保留基类保护字段已经恢复的状态。 */
void PoolNormalZombie::LoadExtraData(const nlohmann::json& j)
{
	// 旧版只把 inPool 写在 extraData；新版基类保护字段先恢复，缺旧字段时保留它。
	mInPool = j.value("inPool", mInPool);
}

/** 读档后重建断肢轨道，并同步泳圈贴图。 */
void PoolNormalZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	// 基类最后会按随机舌头标志恢复舌头；无头泳池僵尸必须再次收敛为整组隐藏。
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_tongue", false);
		mAnimator->SetTrackVisible("anim_hair", false);
	}
	SetPoolEatingTubeVisual(mInPool && mIsEating);
}

/** 只在水中啃食期间覆盖泳圈贴图，既保留涟漪又不污染其他动画片段。 */
void PoolNormalZombie::SetPoolEatingTubeVisual(bool active) const
{
	if (!mAnimator) return;
	// 泳圈浮在水面时保留完整下缘；原版水中死亡则重新裁剪，让泳圈随身体沉下去。
	mAnimator->SetTrackIgnoreTopClip("Zombie_duckytube", mInPool && !mIsPreview && !mIsDying);
	const Texture* tubeImage = active
		? ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_DUCKYTUBE_INWATER)
		: nullptr;
	mAnimator->SetTrackImage("Zombie_duckytube", tubeImage);
}
