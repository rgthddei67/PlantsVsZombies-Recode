#include "RoofMarshalZombie.h"
#include "ZombieBirthVitalsRules.h"
#include "../../GameApp.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "Game/Board/BoardPresentation.h"
#include "../ShadowComponent.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include <algorithm>
#include <array>

namespace {
	constexpr int kBodyHealth = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ROOF_MARSHAL).body;                    // 主人确认的首领本体生命值
	constexpr int kPlantAshDamageCap = 1800;              // 灰烬与土豆雷的单次基础伤害上限
	constexpr float kBossVisualScale = 1.2f;              // 须与 gamedata.json 的督军 scale 同改；影子按此倍率同步放大
	constexpr int kHighThreatHealthThreshold = 11000;      // 低于此本体生命后，高威胁原版池开始参与抽取
	constexpr int kDesperateHealthThreshold = 5400;       // 低于此本体生命后，切换 4 秒四只的最终阶段
	constexpr float kFirstSummonDelay = 1.0f;             // 登场到第一批实际生成的游戏秒数
	constexpr float kNormalSummonInterval = 6.0f;         // 常态与第二阶段的实际召唤间隔，单位：游戏秒
	constexpr float kDesperateSummonInterval = 4.0f;      // 5400 血以下的实际召唤间隔，单位：游戏秒
	constexpr float kCommandPoseDuration = 1.2f;          // 每次召唤后停步播放 anim_idle 的游戏秒数
	constexpr float kCommandBlendTime = 0.2f;             // 指挥姿势与走路轨道切换的混合时长，单位：秒
	constexpr float kLaneSwitchInterval = 6.0f;           // 5400 血以上每次自主换到相邻行的间隔，单位：游戏秒
	constexpr float kLaneTransitionDuration = 0.65f;      // 换行视觉从旧行平滑收敛到新行的时长，单位：游戏秒
	constexpr float kLaneTransitionBlendTime = 0.15f;     // 换行开始/结束时切换走路与待机轨道的混合时长，单位：秒
	constexpr int kNormalSummonCount = 3;                 // 5400 血以上每批生成数量
	constexpr int kDesperateSummonCount = 4;              // 5400 血以下每批生成数量
	constexpr int kHighThreatRollStartPercent = 30;       // 刚低于 11000 血时，每个位置抽取高威胁池的起始概率
	constexpr int kHighThreatRollEndPercent = 100;        // 到 5400 血及以下时，每个位置只从高威胁池抽取
	constexpr float kCommandSpawnX = 910.0f;              // 主人指定的部队召唤 X；直接出现在战场右侧可见区域，单位：像素
	constexpr int kWeatherCommandCadence = 3;             // 每完成多少次实际指挥召唤尝试一次周期改天
	constexpr float kCommandedMediumRainDuration = 20.0f; // 周期天气技能强制中雨的最短游戏秒数
	constexpr float kDesperateHeavyRainDuration = 30.0f;  // 首次进入残血阶段时强制大雨的最短游戏秒数
	constexpr int kAssaultCommandCadence = 2;             // 每完成多少批实际召唤发动一次突击令
	constexpr float kAssaultDuration = 10.0f;              // 突击令目标行强化与红旗显示的统一游戏秒数
	constexpr float kAssaultMoveMultiplier = 1.5f;         // 突击令目标行自主水平推进倍率
	constexpr float kAssaultBiteMultiplier = 1.5f;         // 突击令目标行每口啃食伤害倍率
	constexpr float kButterImmobilizeDuration = 1.25f;     // 督军单次黄油定身时长，单位：游戏秒
	constexpr float kButterImmunityDuration = 5.0f;        // 黄油自然解除后的免疫窗口，单位：游戏秒

	// 显式前五大关白名单是跨版本契约：未来第六大关类型注册到枚举后也不会自动混入。
	constexpr std::array<ZombieType, 14> kStandardSummonPool = {
		ZombieType::ZOMBIE_NORMAL,
		ZombieType::ZOMBIE_TRAFFIC_CONE,
		ZombieType::ZOMBIE_POLEVAULTER,
		ZombieType::ZOMBIE_BUCKET,
		ZombieType::ZOMBIE_FASTBUCKET,
		ZombieType::ZOMBIE_NEWSPAPER,
		ZombieType::ZOMBIE_FASTPAPER,
		ZombieType::ZOMBIE_DOOR,
		ZombieType::ZOMBIE_BACKUP_DANCER,
		ZombieType::ZOMBIE_REINFORCED_DOOR,
		ZombieType::ZOMBIE_POOL_NORMAL,
		ZombieType::ZOMBIE_POOL_CONE,
		ZombieType::ZOMBIE_POOL_BUCKET,
		ZombieType::ZOMBIE_IMP,
	};
	constexpr std::array<ZombieType, 22> kHighThreatSummonPool = {
		ZombieType::ZOMBIE_FOOTBALL,
		ZombieType::ZOMBIE_DANCER,
		ZombieType::ZOMBIE_PINK_FOOTBALL,
		ZombieType::ZOMBIE_ELITE_DANCER,
		ZombieType::ZOMBIE_ELITE_POLEVAULTER,
		ZombieType::ZOMBIE_ZAMBONI,
		ZombieType::ZOMBIE_GILDED_ZAMBONI,
		ZombieType::ZOMBIE_DOLPHIN_RIDER,
		ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER,
		ZombieType::ZOMBIE_JACK_IN_THE_BOX,
		ZombieType::ZOMBIE_BALLOON,
		ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX,
		ZombieType::ZOMBIE_DIGGER,
		ZombieType::ZOMBIE_ELITE_DIGGER,
		ZombieType::ZOMBIE_POGO,
		ZombieType::ZOMBIE_ELITE_POGO,
		ZombieType::ZOMBIE_BUNGEE,
		ZombieType::ZOMBIE_LADDER,
		ZombieType::ZOMBIE_ELITE_LADDER,
		ZombieType::ZOMBIE_CATAPULT,
		ZombieType::ZOMBIE_ELITE_CATAPULT,
		ZombieType::ZOMBIE_GARGANTUAR,
	};

	template <std::size_t N>
	bool ContainsZombieType(const std::array<ZombieType, N>& pool, ZombieType type)
	{
		return std::find(pool.begin(), pool.end(), type) != pool.end();
	}

	template <std::size_t N>
	ZombieType RollCompatibleZombieType(
		const std::array<ZombieType, N>& pool, const Board& board, int row)
	{
		std::array<ZombieType, N> compatible{};
		int compatibleCount = 0;
		for (ZombieType type : pool) {
			if (board.CanSpawnZombieInRow(type, row)) {
				compatible[static_cast<std::size_t>(compatibleCount++)] = type;
			}
		}
		if (compatibleCount == 0) return ZombieType::ZOMBIE_NORMAL;
		return compatible[static_cast<std::size_t>(
			GameRandom::Range(0, compatibleCount - 1))];
	}
}

/**
 * @brief 复用普通僵尸已有的走路、啃食和死亡帧事件，不注册新的动画事件。
 */
void RoofMarshalZombie::SetupZombie()
{
	// 完整复用普通僵尸的帧事件和动作时序，只覆盖首领耐久与啃食伤害。
	Zombie::SetupZombie();
	mBodyHealth = kBodyHealth;
	mBodyMaxHealth = kBodyHealth;
	mAttackDamage = ZombieBirthVitalsRules::Get(ZombieType::ZOMBIE_ROOF_MARSHAL).bite;
	mCommandPhase = CommandPhase::ADVANCING;
	mSummonTimer = kFirstSummonDelay;
	mCommandPoseTimer = 0.0f;
	mLaneSwitchTimer = kLaneSwitchInterval;
	mLaneTransitionRemaining = 0.0f;
	mLaneVisualOffsetY = 0.0f;
	mButterImmunityTimer = 0.0f;
	mCommandCount = 0;
	mLaneSwitchCount = 0;
	mLastSummonCount = 0;
	mLastSummonRowMask = 0;
	mLastSummonBossRow = -1;
	mAssaultCommandCount = 0;
	mLastAssaultRow = -1;
	mLastAssaultAffectedCount = 0;
	mLastSummonedTypes.fill(ZombieType::NUM_ZOMBIE_TYPES);
	if (auto* shadow = GetShadow()) {
		// 普通影子默认 (1.0, 0.75)；保持扁率并同步 1.2 倍本体，脚底中心仍由逻辑 Transform 决定。
		shadow->SetScale(Vector(kBossVisualScale, 0.75f * kBossVisualScale));
	}
	if (!mIsPreview) PlayTrack("anim_idle2", 0.0f, 0.0f);
}

void RoofMarshalZombie::Update()
{
	const bool wasButtered = IsButtered();
	if (!IsParalyzed() && mButterImmunityTimer > 0.0f) {
		mButterImmunityTimer = std::max(0.0f,
			mButterImmunityTimer - DeltaTime::GetDeltaTime());
	}
	const bool wasEating = mIsEating;
	Zombie::Update();
	// 只在自然到期且首领仍可战斗时开启免疫；死亡/掉头清状态不能制造无意义的遗留窗口。
	if (wasButtered && !IsButtered() && mHasHead && !mIsDying && !mIsDead
		&& IsActive()) {
		mButterImmunityTimer = kButterImmunityDuration;
	}
	// 基类会在仍在啃食时跳过 ZombieUpdate；督军只补这一次派生逻辑，保留 anim_eat 与啃食帧事件。
	if (!wasEating || !mIsEating || mIsPreview || mIsDying || mIsDead
		|| !IsActive() || IsImmobilized() || IsGarlicRedirecting()
		|| mTangleKelpPlantID != NULL_PLANT_ID) {
		return;
	}
	const float slowMultiplier = mCooldownTimer > 0.0f ? 0.5f : 1.0f;
	ZombieUpdate(DeltaTime::GetDeltaTime() * slowMultiplier);
}

bool RoofMarshalZombie::ApplyButter()
{
	// 同一次定身不允许刷新，免疫期内黄油仁仍正常造成弹丸伤害但不再停住首领。
	if (IsButtered() || mButterImmunityTimer > 0.0f) return false;
	if (!Zombie::ApplyButter()) return false;
	mButterTimer = kButterImmobilizeDuration;
	return true;
}

void RoofMarshalZombie::ZombieUpdate(float scaledTime)
{
	if (mIsDying || mIsDead || scaledTime <= 0.0f) return;

	UpdateLaneTransition(scaledTime);
	if (mBodyHealth >= kDesperateHealthThreshold) {
		mLaneSwitchTimer -= scaledTime;
		if (mLaneSwitchTimer <= 0.0f
			&& mCommandPhase == CommandPhase::ADVANCING
			&& mLaneTransitionRemaining <= 0.0f) {
			BeginLaneSwitch();
		}
	}
	else {
		mLaneSwitchTimer = 0.0f;
	}

	// 一旦进入最终阶段，尚未开始的长冷却立刻收敛到 4 秒，而不是再等完旧的 6 秒。
	if (mBodyHealth < kDesperateHealthThreshold
		&& mSummonTimer > kDesperateSummonInterval) {
		mSummonTimer = kDesperateSummonInterval;
	}
	mSummonTimer -= scaledTime;

	if (mCommandPhase == CommandPhase::COMMANDING) {
		mCommandPoseTimer -= scaledTime;
		if (mCommandPoseTimer <= 0.0f) {
			mCommandPoseTimer = 0.0f;
			mCommandPhase = CommandPhase::ADVANCING;
			PlayWalkAnimation(kCommandBlendTime);
		}
	}

	if (mSummonTimer > 0.0f) return;
	SummonCommandedZombies();
	mSummonTimer = mBodyHealth < kDesperateHealthThreshold
		? kDesperateSummonInterval
		: kNormalSummonInterval;
	// 啃食期间照常下令，但不抢占 anim_eat；停嘴后的碰撞/状态机会自然恢复稳态轨道。
	if (!mIsEating) BeginCommandPose();
}

Vector RoofMarshalZombie::GetVisualPosition() const
{
	return Zombie::GetVisualPosition() + Vector(0.0f, mLaneVisualOffsetY);
}

void RoofMarshalZombie::StartEat(ColliderComponent* other)
{
	if (mCommandPhase == CommandPhase::COMMANDING) return;
	Zombie::StartEat(other);
}

void RoofMarshalZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	if (mCommandPhase == CommandPhase::COMMANDING
		|| mBodyHealth >= kDesperateHealthThreshold) return;
	Zombie::ZombieMove(scaledDelta, transform);
}

void RoofMarshalZombie::PlayWalkAnimation(float blendTime)
{
	if (mBodyHealth >= kDesperateHealthThreshold
		&& mLaneTransitionRemaining <= 0.0f) {
		PlayTrack("anim_idle2", 0.0f, blendTime);
		return;
	}
	Zombie::PlayWalkAnimation(blendTime);
}

void RoofMarshalZombie::TakeBodyDamage(int damage)
{
	const int healthBeforeHit = mBodyHealth;
	// 临时关闭基类 1/3 掉头，只复用其扣血与断臂逻辑；本函数结束前恢复标志，存档契约不变。
	const bool needDropHead = mNeedDropHead;
	mNeedDropHead = false;
	Zombie::TakeBodyDamage(damage);
	mNeedDropHead = needDropHead;

	if (healthBeforeHit >= kDesperateHealthThreshold
		&& mBodyHealth < kDesperateHealthThreshold) {
		// 最终阶段原子终止尚未完成的自主换行，视觉与逻辑行立即重新对齐并开始亲自推进。
		mLaneSwitchTimer = 0.0f;
		mLaneTransitionRemaining = 0.0f;
		mLaneVisualOffsetY = 0.0f;
		if (mCommandPhase != CommandPhase::COMMANDING && !mIsEating) {
			PlayWalkAnimation(kCommandBlendTime);
		}
		if (mBodyHealth > 0 && mBoard) {
			// 残血阶段只会跨越一次；若已经是大雨，则一次性补足而不会在后续受击中刷新。
			mBoard->TriggerRoofMarshalWeather(
				RainIntensity::HEAVY, kDesperateHeavyRainDuration, true);
		}
	}

	// 督军没有残血掉头阶段；只有致命一击才触发专属掉头，下一帧由基类进入死亡动画。
	if (needDropHead && mHasHead && mBodyHealth == 0) {
		HeadDrop();
		mHasHead = false;
		ClearButter();
	}
}

ZombieType RoofMarshalZombie::RollSummonedZombieType(int row) const
{
	if (!mBoard) return ZombieType::ZOMBIE_NORMAL;
	const int highThreatRollPercent = GetCurrentHighThreatRollPercent();
	if (highThreatRollPercent > 0
		&& GameRandom::Range(1, 100) <= highThreatRollPercent) {
		return RollCompatibleZombieType(kHighThreatSummonPool, *mBoard, row);
	}
	return RollCompatibleZombieType(kStandardSummonPool, *mBoard, row);
}

void RoofMarshalZombie::SummonCommandedZombies()
{
	mLastSummonCount = 0;
	mLastSummonRowMask = 0;
	mLastSummonBossRow = mRow;
	mLastSummonedTypes.fill(ZombieType::NUM_ZOMBIE_TYPES);
	if (!mBoard || mBoard->mRows <= 0) return;

	std::array<int, 6> rows{};
	int availableRows = 0;
	for (int row = 0; row < mBoard->mRows
		&& availableRows < static_cast<int>(rows.size()); ++row) {
		// 召唤部队围绕督军展开，但不与它当前所在行重叠。
		if (row == mRow) continue;
		rows[static_cast<std::size_t>(availableRows++)] = row;
	}

	// 局部 Fisher-Yates：只洗出本批需要的前 N 行，保证同一批绝不重复行。
	const int requestedCount = mBodyHealth < kDesperateHealthThreshold
		? kDesperateSummonCount
		: kNormalSummonCount;
	const int summonCount = std::min(requestedCount, availableRows);
	for (int i = 0; i < summonCount; ++i) {
		const int swapIndex = GameRandom::Range(i, availableRows - 1);
		std::swap(rows[static_cast<std::size_t>(i)], rows[static_cast<std::size_t>(swapIndex)]);

		const int row = rows[static_cast<std::size_t>(i)];
		ZombieType type = RollSummonedZombieType(row);
		// 白名单包含泳池历史形态；目标行地形不兼容时不会抽到，此守卫只防未来单项行为漂移。
		if (!mBoard->CanSpawnZombieInRow(type, row)) type = ZombieType::ZOMBIE_NORMAL;
		Zombie* summoned = mBoard->CreateZombie(type, row, kCommandSpawnX);
		if (!summoned) continue;

		mLastSummonedTypes[static_cast<std::size_t>(mLastSummonCount)] = type;
		mLastSummonRowMask |= 1 << row;
		++mLastSummonCount;
	}
	++mCommandCount;
	if (mCommandCount % kAssaultCommandCadence == 0) {
		IssueAssaultCommand();
	}
	if (mBodyHealth >= kDesperateHealthThreshold
		&& mCommandCount % kWeatherCommandCadence == 0) {
		// 周期命令只把晴/小雨提升到中雨；已有中雨或大雨保持原倒计时，不被永久续期。
		mBoard->TriggerRoofMarshalWeather(
			RainIntensity::MEDIUM, kCommandedMediumRainDuration, false);
	}
}

void RoofMarshalZombie::IssueAssaultCommand()
{
	mLastAssaultRow = -1;
	mLastAssaultAffectedCount = 0;
	if (!mBoard || mBoard->mRows <= 0) return;

	std::array<int, 6> rowCounts{};
	int highestCount = 0;
	for (int row = 0; row < mBoard->mRows
		&& row < static_cast<int>(rowCounts.size()); ++row) {
		mBoard->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (!zombie || zombie == this || zombie->mZombieType == ZombieType::ZOMBIE_ROOF_MARSHAL
				|| zombie->IsMindControlled() || zombie->IsDying()) {
				return;
			}
			++rowCounts[static_cast<std::size_t>(row)];
		});
		highestCount = std::max(highestCount, rowCounts[static_cast<std::size_t>(row)]);
	}
	if (highestCount <= 0) return;

	std::array<int, 6> tiedRows{};
	int tiedRowCount = 0;
	for (int row = 0; row < mBoard->mRows
		&& row < static_cast<int>(rowCounts.size()); ++row) {
		if (rowCounts[static_cast<std::size_t>(row)] == highestCount) {
			tiedRows[static_cast<std::size_t>(tiedRowCount++)] = row;
		}
	}
	mLastAssaultRow = tiedRows[static_cast<std::size_t>(
		GameRandom::Range(0, tiedRowCount - 1))];
	// 先提交目标逻辑行并播放跨行走路，再向该行部队下令；红旗与强化共享 10 秒寿命。
	BeginLaneSwitchTo(mLastAssaultRow);
	mBoard->mEntityRegistry.ForEachZombieInRow(mLastAssaultRow, [&](Zombie* zombie) {
		if (!zombie || zombie == this || zombie->mZombieType == ZombieType::ZOMBIE_ROOF_MARSHAL
			|| zombie->IsMindControlled() || zombie->IsDying()) {
			return;
		}
		zombie->ApplyRoofMarshalAssault(
			kAssaultDuration, kAssaultMoveMultiplier, kAssaultBiteMultiplier);
		++mLastAssaultAffectedCount;
	});
	if (mLastAssaultAffectedCount > 0) {
		++mAssaultCommandCount;
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_HUGEWAVE, 0.25f);
		if (BoardPresentation* presentation = mBoard->GetPresentation()) {
			presentation->ShowRoofMarshalAssaultWarning(
				mLastAssaultRow, kAssaultDuration);
		}
	}
}

void RoofMarshalZombie::BeginCommandPose()
{
	// 突击令刚触发的跨行演出必须保持走路轨道；到达后由换行收尾恢复 idle2。
	if (mLaneTransitionRemaining > 0.0f) {
		mCommandPhase = CommandPhase::ADVANCING;
		mCommandPoseTimer = 0.0f;
		return;
	}
	mCommandPhase = CommandPhase::COMMANDING;
	mCommandPoseTimer = kCommandPoseDuration;
	PlayTrack("anim_idle", 0.0f, kCommandBlendTime);
}

void RoofMarshalZombie::BeginLaneSwitch()
{
	mLaneSwitchTimer = kLaneSwitchInterval;
	if (!mBoard || mRow < 0 || mRow >= mBoard->mRows) return;

	int candidates[2] = { -1, -1 };
	int candidateCount = 0;
	for (const int candidate : { mRow - 1, mRow + 1 }) {
		if (candidate < 0 || candidate >= mBoard->mRows
			|| !mBoard->CanSpawnZombieInRow(mZombieType, candidate)) {
			continue;
		}
		candidates[candidateCount++] = candidate;
	}
	if (candidateCount == 0) return;

	const int destination = candidates[candidateCount == 1
		? 0
		: GameRandom::Range(0, candidateCount - 1)];
	BeginLaneSwitchTo(destination);
}

bool RoofMarshalZombie::BeginLaneSwitchTo(int destination)
{
	if (!mBoard || destination < 0 || destination >= mBoard->mRows
		|| destination == mRow
		|| !mBoard->CanSpawnZombieInRow(mZombieType, destination)) {
		return false;
	}
	Transform* transform = GetTransform();
	if (!transform) return false;
	Vector position = transform->GetPosition();
	const float oldTerrainY = mBoard->GetZombieSpawnY(mRow, position.x);
	const float newTerrainY = mBoard->GetZombieSpawnY(destination, position.x);
	if (oldTerrainY < 0.0f || newTerrainY < 0.0f) return false;

	// 逻辑行与碰撞箱立即提交；独立视觉补偿从旧行高度平滑归零，遵守逻辑网格/美术偏移分离契约。
	CommitRow(destination);
	position.y = newTerrainY;
	transform->SetPosition(position);
	mLaneVisualOffsetY = oldTerrainY - newTerrainY;
	mLaneTransitionRemaining = kLaneTransitionDuration;
	++mLaneSwitchCount;
	PlayWalkAnimation(kLaneTransitionBlendTime);
	return true;
}

void RoofMarshalZombie::UpdateLaneTransition(float scaledTime)
{
	if (mLaneTransitionRemaining <= 0.0f) return;
	const float previousRemaining = mLaneTransitionRemaining;
	mLaneTransitionRemaining = std::max(0.0f,
		mLaneTransitionRemaining - std::max(0.0f, scaledTime));
	if (previousRemaining > 0.0f) {
		mLaneVisualOffsetY *= mLaneTransitionRemaining / previousRemaining;
	}
	if (mLaneTransitionRemaining <= 0.0f) {
		mLaneVisualOffsetY = 0.0f;
		if (mCommandPhase != CommandPhase::COMMANDING) {
			PlayWalkAnimation(kLaneTransitionBlendTime);
		}
	}
}

bool RoofMarshalZombie::IsAllowedSummonType(ZombieType type)
{
	return ContainsZombieType(kStandardSummonPool, type)
		|| ContainsZombieType(kHighThreatSummonPool, type);
}

bool RoofMarshalZombie::IsHighThreatSummonType(ZombieType type)
{
	return ContainsZombieType(kHighThreatSummonPool, type);
}

const char* RoofMarshalZombie::GetCommandPhaseName() const
{
	return mCommandPhase == CommandPhase::COMMANDING ? "COMMANDING" : "ADVANCING";
}

int RoofMarshalZombie::GetLastSummonDistinctRowCount() const
{
	int count = 0;
	for (int mask = mLastSummonRowMask; mask != 0; mask >>= 1) {
		count += mask & 1;
	}
	return count;
}

bool RoofMarshalZombie::IsHighThreatPoolUnlocked() const
{
	return mBodyHealth < kHighThreatHealthThreshold;
}

int RoofMarshalZombie::GetCurrentHighThreatRollPercent() const
{
	if (mBodyHealth >= kHighThreatHealthThreshold) return 0;
	if (mBodyHealth <= kDesperateHealthThreshold) {
		return kHighThreatRollEndPercent;
	}
	// 11000→5400 血把概率从 30% 线性抬到 100%；最终阶段因此不会再混入普通杂兵。
	const int threatPhaseHealthSpan =
		kHighThreatHealthThreshold - kDesperateHealthThreshold;
	const int healthLostInThreatPhase =
		kHighThreatHealthThreshold - mBodyHealth;
	return kHighThreatRollStartPercent
		+ (kHighThreatRollEndPercent - kHighThreatRollStartPercent)
		* healthLostInThreatPhase / threatPhaseHealthSpan;
}

int RoofMarshalZombie::GetHighThreatHealthThreshold() const
{
	return kHighThreatHealthThreshold;
}

int RoofMarshalZombie::GetDesperateHealthThreshold() const
{
	return kDesperateHealthThreshold;
}

int RoofMarshalZombie::GetCurrentSummonCount() const
{
	return mBodyHealth < kDesperateHealthThreshold
		? kDesperateSummonCount
		: kNormalSummonCount;
}

float RoofMarshalZombie::GetCurrentSummonInterval() const
{
	return mBodyHealth < kDesperateHealthThreshold
		? kDesperateSummonInterval
		: kNormalSummonInterval;
}

bool RoofMarshalZombie::IsWalkingPhase() const
{
	return mBodyHealth < kDesperateHealthThreshold;
}

void RoofMarshalZombie::SaveExtraData(nlohmann::json& j) const
{
	j["commandPhase"] = static_cast<int>(mCommandPhase);
	j["summonTimer"] = mSummonTimer;
	j["commandPoseTimer"] = mCommandPoseTimer;
	j["laneSwitchTimer"] = mLaneSwitchTimer;
	j["laneTransitionRemaining"] = mLaneTransitionRemaining;
	j["laneVisualOffsetY"] = mLaneVisualOffsetY;
	j["butterImmunityTimer"] = mButterImmunityTimer;
	j["commandCount"] = mCommandCount;
	j["laneSwitchCount"] = mLaneSwitchCount;
	j["lastSummonCount"] = mLastSummonCount;
	j["lastSummonRowMask"] = mLastSummonRowMask;
	j["lastSummonBossRow"] = mLastSummonBossRow;
	j["assaultCommandCount"] = mAssaultCommandCount;
	j["lastAssaultRow"] = mLastAssaultRow;
	j["lastAssaultAffectedCount"] = mLastAssaultAffectedCount;
	j["lastSummonedTypes"] = nlohmann::json::array();
	for (ZombieType type : mLastSummonedTypes) {
		j["lastSummonedTypes"].push_back(static_cast<int>(type));
	}
}

void RoofMarshalZombie::LoadExtraData(const nlohmann::json& j)
{
	const int rawPhase = j.value("commandPhase", static_cast<int>(CommandPhase::ADVANCING));
	mCommandPhase = rawPhase == static_cast<int>(CommandPhase::COMMANDING)
		? CommandPhase::COMMANDING
		: CommandPhase::ADVANCING;
	mSummonTimer = std::clamp(j.value("summonTimer", kFirstSummonDelay),
		0.0f, kNormalSummonInterval);
	mCommandPoseTimer = std::clamp(j.value("commandPoseTimer", 0.0f),
		0.0f, kCommandPoseDuration);
	mLaneSwitchTimer = std::clamp(j.value("laneSwitchTimer", kLaneSwitchInterval),
		0.0f, kLaneSwitchInterval);
	mLaneTransitionRemaining = std::clamp(j.value("laneTransitionRemaining", 0.0f),
		0.0f, kLaneTransitionDuration);
	mLaneVisualOffsetY = std::clamp(j.value("laneVisualOffsetY", 0.0f),
		-static_cast<float>(SCENE_HEIGHT), static_cast<float>(SCENE_HEIGHT));
	mButterImmunityTimer = std::clamp(j.value("butterImmunityTimer", 0.0f),
		0.0f, kButterImmunityDuration);
	mButterTimer = std::min(mButterTimer, kButterImmobilizeDuration);
	if (mButterTimer > 0.0f) mButterImmunityTimer = 0.0f;
	mCommandCount = std::max(0, j.value("commandCount", 0));
	mLaneSwitchCount = std::max(0, j.value("laneSwitchCount", 0));
	mLastSummonCount = std::clamp(j.value("lastSummonCount", 0), 0,
		static_cast<int>(mLastSummonedTypes.size()));
	const int validRowMask = mBoard && mBoard->mRows > 0
		? (1 << std::min(mBoard->mRows, 30)) - 1
		: 0;
	mLastSummonRowMask = j.value("lastSummonRowMask", 0) & validRowMask;
	mLastSummonBossRow = std::clamp(j.value("lastSummonBossRow", mRow), -1,
		mBoard && mBoard->mRows > 0 ? mBoard->mRows - 1 : -1);
	mAssaultCommandCount = std::max(0, j.value("assaultCommandCount", 0));
	mLastAssaultRow = std::clamp(j.value("lastAssaultRow", -1), -1,
		mBoard && mBoard->mRows > 0 ? mBoard->mRows - 1 : -1);
	mLastAssaultAffectedCount = std::max(0, j.value("lastAssaultAffectedCount", 0));
	mLastSummonedTypes.fill(ZombieType::NUM_ZOMBIE_TYPES);
	if (j.contains("lastSummonedTypes") && j["lastSummonedTypes"].is_array()) {
		const auto& savedTypes = j["lastSummonedTypes"];
		for (int i = 0; i < mLastSummonCount && i < static_cast<int>(savedTypes.size()); ++i) {
			const ZombieType type = static_cast<ZombieType>(savedTypes[i].get<int>());
			if (IsAllowedSummonType(type)) {
				mLastSummonedTypes[static_cast<std::size_t>(i)] = type;
			}
		}
	}

	// COMMANDING 必须有正的姿势剩余时间；濒死/损坏组合则原子恢复为可推进状态。
	if (mCommandPhase == CommandPhase::COMMANDING
		&& mCommandPoseTimer > 0.0f && mHasHead && !mIsDying) {
		if (!mIsEating) PlayTrack("anim_idle", 0.0f, 0.0f);
	}
	else {
		mCommandPhase = CommandPhase::ADVANCING;
		mCommandPoseTimer = 0.0f;
	}

	if (mBodyHealth < kDesperateHealthThreshold) {
		mLaneSwitchTimer = 0.0f;
		mLaneTransitionRemaining = 0.0f;
		mLaneVisualOffsetY = 0.0f;
	}
	else if (mLaneTransitionRemaining <= 0.0f) {
		mLaneVisualOffsetY = 0.0f;
	}
	if (!mIsEating && mCommandPhase != CommandPhase::COMMANDING) {
		PlayWalkAnimation(0.0f);
	}
}

void RoofMarshalZombie::TakePlantAshDamage(int damage)
{
	// 土豆雷传入 INT32_MAX；在统一灰烬入口先压回常规爆炸伤害，再保留植物词条缩放。
	Zombie::TakePlantAshDamage(std::min(damage, kPlantAshDamageCap));
}

bool RoofMarshalZombie::TakePlantInstantKill()
{
	// 首领拒绝被吞食，并使用大嘴花统一的 20 点基础咬伤。
	return false;
}

void RoofMarshalZombie::HeadDrop()
{
	if (!mHasHead || !mAnimator) return;

	// 先读取仍可见的头轨世界锚点，再同步隐藏头、下巴、舌头和军帽轨道。
	const Vector particlePosition = GetTrackWorldPosition("anim_head1");
	mAnimator->SetTrackVisible("anim_head1", false);
	mAnimator->SetTrackVisible("anim_head2", false);
	mAnimator->SetTrackVisible("anim_tongue", false);
	mAnimator->SetTrackVisible("anim_hair", false);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("RoofMarshalHeadOff", particlePosition);
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void RoofMarshalZombie::ArmDrop()
{
	if (!mHasArm || !mAnimator) return;
	mAnimator->SetTrackVisible("Zombie_outerarm_hand", false);
	mAnimator->SetTrackVisible("Zombie_outerarm_lower", false);
	mAnimator->SetTrackImage("Zombie_outerarm_upper", ResourceManager::GetInstance().
		GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_ROOFMARSHAL_OUTERARM_UPPER2));
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("ZombieArmOff", GetPosition());
	}
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP, 0.25f);
}

void RoofMarshalZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	if (!mHasArm && mAnimator) {
		// 基类先恢复残肢显隐，再把普通棕袖替换为督军军服断袖，保证读档与实时掉臂一致。
		mAnimator->SetTrackImage("Zombie_outerarm_upper", ResourceManager::GetInstance().
			GetTexture(ResourceKeys::Textures::IMAGE_ZOMBIE_ROOFMARSHAL_OUTERARM_UPPER2));
	}
}

ZombieMovementRules::BirthProfile RoofMarshalZombie::GetBirthMovementProfile()
{
	auto p=Zombie::GetBirthMovementProfile();
	p.linear=true;
	p.velocityMinimum=p.velocityMaximum=0;
	p.phaseDependent=true;
	return p;
}

float RoofMarshalZombie::GetMineSimulationMoveSpeed() const
{
	return IsWalkingPhase() ? Zombie::GetMineSimulationMoveSpeed() : 0;
}
