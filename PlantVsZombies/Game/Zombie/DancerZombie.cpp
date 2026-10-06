#include "DancerZombie.h"
#include "DancerRules.h"
#include "BackupDancerZombie.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceManager.h"
#include <stdexcept>

namespace {
	constexpr float kDancingInDuration = 2.0f;	// 入场月球漫步基础时长（原版 200cs），另加 0~0.12s 随机
	constexpr float kHoldDuration = DancerRules::HoldSeconds;		// 响指后保持举手姿势的时长——**须同改**：与伴舞 kRiseDuration 一致（举手定格到伴舞出土完成）
	constexpr float kDanceLimitX = DancerRules::RefillLimitX;		// 越过此 x 不再补充召唤（原版 dance limit）
	constexpr float kMoonwalkClip = 2.0f;		// 原版 rate24（12fps 的 2 倍）；截图手感可微调
	constexpr float kPointClip = DancerRules::PointClip;
	constexpr float kArmraiseClip = 1.8f;		// 与伴舞 kArmraiseClip 保持一致，否则举手段散拍
	constexpr float kDanceAnimSpeed = DancerRules::DanceSpeed;		// 与伴舞 kDanceAnimSpeed 保持一致
	constexpr float kSummonFrontMinX = DancerRules::FrontMinimumX;	// 舞王 x<130 时不放前方位（原版防出左屏）
	constexpr float kSummonSideDist = DancerRules::SideDistance;	// 同行前/后伴舞与舞王的 x 距离
	constexpr float kDancerFlipPivotX = 45.0f;	// C# UpdateReanim 翻面时补 90px，等价于绕局部 x=45 翻转
}

void DancerZombie::SetupZombie()
{
	mBodyHealth = DancerRules::BodyHealth;
	mBodyMaxHealth = DancerRules::BodyHealth;
	SetAnimationSpeed(kDanceAnimSpeed);

	if (mIsPreview) { PlayTrack("anim_moonwalk", kMoonwalkClip); return; }

	// 帧号为主人指定：Die@146（anim_death 112~147 内），EatTarget@90/99（anim_eat 78~111 内）
	mAnimator->AddFrameEvent(146, [this]() { this->Die(); });
	mAnimator->AddFrameEvent(90, [this]() { this->EatTarget(); }, true);
	mAnimator->AddFrameEvent(99, [this]() { this->EatTarget(); }, true);

	mPhase = DancerPhase::DANCING_IN;
	mPhaseTimer = kDancingInDuration + GameRandom::Range(0, 12) * 0.01f;
	PlayTrack("anim_moonwalk", kMoonwalkClip);
	UpdateDanceFacing();
}

void DancerZombie::ZombieUpdate(float scaledTime)
{
	// 魅惑边沿：放弃旧伴舞（它们继续以原阵营跳舞）；此后新召唤的伴舞在 SummonBackupDancers 里继承魅惑
	if (mIsMindControlled && !mCharmHandled) {
		mCharmHandled = true;
		for (int i = 0; i < 4; ++i) mFollowerID[i] = NULL_ZOMBIE_ID;
	}

	switch (mPhase) {
	case DancerPhase::DANCING_IN:
        if(mIsEating) break; // 保留入场首口完成后由 EatTarget 发起召唤的契约。
		mPhaseTimer -= scaledTime;
		if (mPhaseTimer <= 0.0f && mHasHead) {
			mPhase = DancerPhase::SNAPPING;
			// 空回切轨 = 原版 PlayOnceAndHold：播完定格在举手末帧，由 HOLD 阶段维持姿势
			mAnimator->PlayTrackOnce("anim_point", "", kPointClip, 0.3f);
		}
		break;
	case DancerPhase::SNAPPING:
		// 用 IsPlaying 轮询收尾（同原版查 mLoopCount）而非末帧帧事件——36 帧=point 段末帧，
		// 定格 clamp 时帧事件不保证触发（末-1 帧陷阱）。被迫啃食时 anim_eat 循环播放，不会误判。
		if (!mAnimator->IsPlaying()) {
			SummonBackupDancers();
			mPhase = DancerPhase::HOLD;
			mPhaseTimer = kHoldDuration;
			mLastBeatBucket = -1;
		}
		break;
	case DancerPhase::HOLD:
		// 保持举手定格（不切舞蹈轨道），直到伴舞出土完成
		mPhaseTimer -= scaledTime;
		if (mPhaseTimer <= 0.0f) {
			mPhase = DancerPhase::DANCING;
		}
		break;
	case DancerPhase::DANCING:
		UpdateDanceTrack(0.3f);
		// 补位：只在节拍 12（walk→armraise 切拍点，原版编排）、有头、未越界、确实缺人时再打响指
		// scaledTime>0：暂停时不触发（此检查不依赖 dt，恰停在节拍 12 会让暂停画面瞬间切举手）
		if (mHasHead && scaledTime > 0.0f && mBoard && mBoard->GetDanceBeatFrame() == 12
			&& GetPosition().x < kDanceLimitX && NeedsMoreBackupDancers()) {
            CancelEatingForSpecialAction(); // 补召抢占身体动作，先释放旧啃食事务。
			mPhase = DancerPhase::SNAPPING;
			mAnimator->PlayTrackOnce("anim_point", "", kPointClip, 0.3f);
		}
		break;
	}
	UpdateDanceFacing();
}

void DancerZombie::UpdateDanceTrack(float blendTime)
{
	if (!mBoard || mIsDying || mIsEating) return;
	const int bucket = (mBoard->GetDanceBeatFrame() >= 12) ? 1 : 0;
	if (bucket == mLastBeatBucket) return;
	mLastBeatBucket = bucket;
	if (bucket == 1)
		PlayTrack("anim_armraise", kArmraiseClip, blendTime);
	else
		PlayTrack("anim_walk", 0.0f, blendTime);
}

void DancerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	// 打响指/定身跳舞阶段不移动；其余阶段始终沿阵营方向推进。
	if (mPhase == DancerPhase::SNAPPING || mPhase == DancerPhase::HOLD) return;
	// C# 的 DancerDancingIn 会让未魅惑舞王实际向右；主人现场确认这里只要视觉月球步反向，
	// 逻辑位置仍应朝房子推进。因此保留 UpdateDanceFacing 的入场翻面，不复制 C# 的 X 反向。
	Zombie::ZombieMove(scaledDelta, transform);
}

void DancerZombie::PlayWalkAnimation(float blendTime)
{
	switch (mPhase) {
	case DancerPhase::DANCING_IN:
		PlayTrack("anim_moonwalk", kMoonwalkClip, blendTime);
		return;
	case DancerPhase::SNAPPING:
		// 啃食打断后重播举手（播完定格，SNAPPING 轮询会再次收尾召唤）
		mAnimator->PlayTrackOnce("anim_point", "", kPointClip, blendTime);
		return;
	default:
		mLastBeatBucket = -1;
		if (mBoard && mBoard->GetDanceBeatFrame() >= 12)
			PlayTrack("anim_armraise", kArmraiseClip, blendTime);
		else
			PlayTrack("anim_walk", 0.0f, blendTime);
	}
}

void DancerZombie::OnStartEating()
{
	UpdateDanceFacing();
}

void DancerZombie::OnStopEating()
{
	UpdateDanceFacing();
}

void DancerZombie::UpdateDanceFacing()
{
	if (!mAnimator || mIsPreview) return;

	// C# GetDancerPhase：13~15、19~21 是两段朝右举手；入场月球步也朝右。
	// 啃食时不使用舞步翻面，而是回到所属阵营的默认朝向。
	bool flip = false;
	if (!mIsEating) {
		if (mPhase == DancerPhase::DANCING_IN) {
			flip = true;
		}
		else if (mPhase == DancerPhase::DANCING && mBoard) {
			const int beat = mBoard->GetDanceBeatFrame();
			flip = (beat >= 13 && beat <= 15) || (beat >= 19 && beat <= 21);
		}
	}
	// 原版舞王/伴舞在魅惑后把整套舞步朝向取反，而非始终固定朝右。
	if (mIsMindControlled) flip = !flip;
	mAnimator->SetFlipX(flip, kDancerFlipPivotX);
}

void DancerZombie::StartEat(ColliderComponent* other)
{
	// 响指/定身期间不开吃——召唤动作要播完；碰撞 onTriggerStay 每帧重试 StartEat，
	// 进入 DANCING 后自然（续）啃。月球漫步中碰到植物则照常开吃，由 EatTarget 覆写
	// 在首口结算后中断转召唤（先啃一口，后召唤，主人指定）。
	if (mHasHead && (mPhase == DancerPhase::SNAPPING || mPhase == DancerPhase::HOLD)) return;
	Zombie::StartEat(other);
}

void DancerZombie::EatTarget()
{
	const bool resolvedPlantBite = mEatPlantID != NULL_PLANT_ID
		&& IsCurrentPlantEatingTargetValid();
	Zombie::EatTarget();
	// 月球漫步中被迫开吃：首口伤害结算完立即中断啃食，转打响指召唤。
	// 强停啃食复刻 StartMindControlled 的规范：平衡 mEaterCount → 清目标 → 模板方法收尾。
	if (!resolvedPlantBite || mPhase != DancerPhase::DANCING_IN || !mHasHead || mIsDying) return;
	if (mIsEating && mEatPlantID != NULL_PLANT_ID && mBoard) {
		if (auto* plant = mBoard->mEntityRegistry.GetPlant(mEatPlantID)) plant->mEaterCount--;
	}
	mEatPlantID = NULL_PLANT_ID;
	mEatZombieID = NULL_ZOMBIE_ID;
	mIsEating = false;
	// 先切阶段再收尾：PlayWalkAnimation 的 SNAPPING 分支播 anim_point（播完定格，SNAPPING 轮询收尾召唤）
	mPhase = DancerPhase::SNAPPING;
	ResumeWalkAfterEat(0.3f);
}

void DancerZombie::SummonBackupDancers()
{
	if (!mBoard || !mHasHead) return;
	const float x = GetPosition().x;
	const struct { int row; float posX; } slots[4] = {
		{ mRow - 1, x },                      // 上一行同列
		{ mRow + 1, x },                      // 下一行同列
		{ mRow,     x - kSummonSideDist },    // 同行前方（太靠左跳过）
		{ mRow,     x + kSummonSideDist },    // 同行后方
	};
	for (int i = 0; i < 4; ++i) {
		// 槽位有效 = 活着且与领队同阵营（伴舞被魅惑后不清领队侧槽位，只能在此按阵营判失效）
		if (Zombie* f = mBoard->mEntityRegistry.GetZombie(mFollowerID[i])) {
			if (f->IsMindControlled() == mIsMindControlled) continue;
		}
		mFollowerID[i] = NULL_ZOMBIE_ID;
		if (slots[i].row < 0 || slots[i].row >= mBoard->mRows) continue;
		if (i == 2 && x < kSummonFrontMinX) continue;
		Zombie* summoned = mBoard->CreateZombie(ZombieType::ZOMBIE_BACKUP_DANCER,
			slots[i].row, slots[i].posX);
		if (!summoned) continue;
		mFollowerID[i] = summoned->mZombieID;
		if (auto* backup = dynamic_cast<BackupDancerZombie*>(summoned)) {
			backup->mLeaderID = mZombieID;
		}
		if (mIsMindControlled) summoned->StartMindControlled();	// 魅惑领队的新伴舞继承阵营
	}
}

bool DancerZombie::NeedsMoreBackupDancers() const
{
	if (!mBoard) return false;
	for (int i = 0; i < 4; ++i) {
		// 永久不可用的位不算缺：行越界（上/下行）与太靠左的前方位
		//（原版对前方位无此豁免，会在 x<130 时无限重打响指——这里补上防死循环）
		if (i == 0 && mRow - 1 < 0) continue;
		if (i == 1 && mRow + 1 >= mBoard->mRows) continue;
		if (i == 2 && GetPosition().x < kSummonFrontMinX) continue;
		Zombie* follower = mBoard->mEntityRegistry.GetZombie(mFollowerID[i]);
		// 死亡或阵营不同（被魅惑脱队）都算缺人
		if (!follower || follower->IsMindControlled() != mIsMindControlled) return true;
	}
	return false;
}

void DancerZombie::HeadDrop()
{
	if (!mHasHead) return;
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieJacksonHeadOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void DancerZombie::ArmDrop()
{
	if (!mHasArm) return;
	// 同伴舞：藏小臂+手、保大臂，无材质替换、无粒子（主人指定；MJ 版 reanim 无残肢轨道）
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void DancerZombie::ZombieItemUpdate() const
{
	if (!mHasArm) {
		mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
		mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	}
	if (!mHasHead) {
		mAnimator->SetTrackVisible("anim_head1", false);
		mAnimator->SetTrackVisible("anim_head2", false);
		mAnimator->SetTrackVisible("anim_hair", false);
	}
}

void DancerZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["phaseTimer"] = mPhaseTimer;
	j["charmHandled"] = mCharmHandled;
	j["followers"] = nlohmann::json::array({
		mFollowerID[0], mFollowerID[1], mFollowerID[2], mFollowerID[3] });
}

void DancerZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<DancerPhase>(j.value("phase", 0));
	mPhaseTimer = j.value("phaseTimer", 0.0f);
	mCharmHandled = j.value("charmHandled", false);
	if (j.contains("followers") && j["followers"].is_array()) {
		const auto& arr = j["followers"];
		for (int i = 0; i < 4 && i < static_cast<int>(arr.size()); ++i) {
			mFollowerID[i] = arr[i].get<int>();
		}
	}
	// 读档僵尸 ID 经 CreateZombieWithID 保值，followers 交叉引用安全（死者 GetZombie 返回 null 即空位）
	UpdateDanceFacing();
	if (mIsEating) return;
	if (mPhase == DancerPhase::DANCING_IN || mPhase == DancerPhase::SNAPPING) return;
	// HOLD/DANCING：下帧按节拍重刷轨道（RestoreAnimState 的帧位被重新入拍覆盖，预期行为）
	mLastBeatBucket = -1;
}


ZombieMovementRules::BirthProfile DancerZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.clip="anim_moonwalk";
	p.alternative=nullptr;
	p.rootMinimum=p.rootMaximum=ZombieMovementRules::BaseRootSpeed;
	p.animationMinimum=p.animationMaximum=kMoonwalkClip;
	p.phaseDependent=true;
	return p;
}

float DancerZombie::GetForecastSnapSeconds()
{
	const auto reanim=ResourceManager::GetInstance().GetReanimation("ZombieJackson");
	if (!reanim) throw std::runtime_error("missing dancer snap reanimation");
	const auto range=reanim->GetTrackFrameRange("anim_point");
	if(range.first<0 || range.second<=range.first || reanim->mFPS<=0)
		throw std::runtime_error("invalid dancer snap frame range");
	return (range.second-range.first)/(reanim->mFPS*DancerRules::PointClip);
}

DancerRules::Forecast DancerZombie::GetDanceForecast() const
{
	DancerRules::Forecast result;
	result.leader=true; result.phase=static_cast<DancerRules::Forecast::Phase>(mPhase);
	result.remaining=mPhaseTimer; result.snapSeconds=GetForecastSnapSeconds();
	result.walkSpeed=GetSimulationRootMoveSpeed("anim_walk",DancerRules::DanceSpeed);
	result.entrySpeed=GetSimulationRootMoveSpeed("anim_moonwalk",kMoonwalkClip);
	result.stopHealth=mBodyMaxHealth/3;
	for(size_t i=0;i<result.followers.size();++i)
		result.followers[i]=mFollowerID[i]==NULL_ZOMBIE_ID ? 0 : mFollowerID[i];
	if(mPhase==DancerPhase::SNAPPING && mAnimator) {
		const auto reanim=mAnimator->GetReanimation();
		const auto range=reanim->GetTrackFrameRange("anim_point");
		result.remaining=std::max(0.0f,range.second-mAnimator->GetCurrentFrame())/(reanim->mFPS*DancerRules::PointClip);
	}
	return result;
}
