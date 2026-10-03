#include "NightRoofChargeRules.h"
#include "Game/Board/Board.h"
#include "Game/AI/PlantDefenseMonteCarlo.h"
#include "Game/AudioSystem.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/PlantFootprint.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/HijackerZombie.h"
#include "GameApp.h"
#include "GameRandom.h"
#include "ParticleSystem/ParticleSystem.h"
#include "Profiler.h"
#include "ResourceKeys.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_set>
#include <vector>

namespace {
	constexpr int kNightRoofRouteMonteCarloRolloutCount = 32; // 满雷路线每个候选共享的短视未来样本数
	constexpr float kNightRoofRouteMonteCarloHorizonSeconds = 10.0f; // 满雷路线从预警起推演的游戏秒数
	constexpr float kGroundingZombieControlImmunityDuration = 30.0f; // 成功引雷后范围免控的游戏秒数
	constexpr float kGroundingZombieControlImmunityRadius = 130.0f; // 成功引雷后减速/冻结/黄油免疫的圆形半径，单位：像素
	constexpr ZombieControlMask kGroundingZombieControlImmunityMask =
		ZombieControlBit(ZombieControlEffect::SLOW)
		| ZombieControlBit(ZombieControlEffect::FROZEN)
		| ZombieControlBit(ZombieControlEffect::BUTTER); // 接地编队免疫三类植物控制，保留麻痹和伤害反制
	constexpr float kRoofRunoffChargeMaximum = 100.0f;    // 坡面径流进入锁行预警所需的满积累值
	constexpr float kRoofRunoffLightChargePerSecond = 1.00f; // 小雨每游戏秒增加的径流积累点数
	constexpr float kRoofRunoffMediumChargePerSecond = 2.00f; // 中雨每游戏秒增加的径流积累点数
	constexpr float kRoofRunoffHeavyChargePerSecond = 3.50f; // 大雨每游戏秒增加的径流积累点数
	constexpr float kRoofRunoffClearDrainPerSecond = 0.30f; // 晴天每游戏秒自然排走的径流积累点数
	constexpr float kRoofRunoffWarningDuration = 3.0f;    // 满积累后锁行预警的持续游戏秒
	constexpr float kRoofRunoffFlowDuration = 2.20f;      // 单次目标行冲刷的持续游戏秒
	constexpr float kRoofRunoffZombieDriftSpeed = -60.0f; // 地面僵尸顺坡冲向屋檐的附加速度，像素/游戏秒
	constexpr int kRoofRunoffRetainedChargeMin = 30;      // 冲刷结束后保留湿度的均匀随机下限（百分比）
	constexpr int kRoofRunoffRetainedChargeMax = 60;      // 冲刷结束后保留湿度的均匀随机上限（百分比）
	constexpr int kRoofRunoffOneRowWeight = 50;           // 满积累事件只冲刷一行的相对权重
	constexpr int kRoofRunoffTwoRowWeight = 35;           // 满积累事件同时冲刷两行的相对权重
	constexpr int kRoofRunoffThreeRowWeight = 15;         // 满积累事件同时冲刷三行的相对权重
	using namespace NightRoofChargeRules;

}

/** 为急救员推演投影当前劫持者处决倒计时和生存模式生命线。 */
void Board::PopulateNightRoofHijackerTreatmentForecast(
	int lockedHijackerID, float& executionSeconds,
	float& survivalExecutionLineCap) const
{
	survivalExecutionLineCap =
		static_cast<float>(kNightRoofHijackerSurvivalLineCap);
	executionSeconds = -1.0f;
	if (lockedHijackerID == NULL_ZOMBIE_ID) return;

	if (mNightRoofChargePhase == NightRoofChargePhase::WARNING) {
		executionSeconds = mNightRoofChargePhaseTimer;
		return;
	}
	if (mNightRoofChargePhase != NightRoofChargePhase::CHARGING) return;

	float chargePerSecond = 0.0f;
	switch (mRainIntensity) {
	case RainIntensity::LIGHT: chargePerSecond = kNightRoofChargeLightPerSecond; break;
	case RainIntensity::MEDIUM: chargePerSecond = kNightRoofChargeMediumPerSecond; break;
	case RainIntensity::HEAVY: chargePerSecond = kNightRoofChargeHeavyPerSecond; break;
	default: break;
	}
	chargePerSecond += GetNightRoofHijackerRainChargeBonusPerSecond();
	if (chargePerSecond > 0.0f) {
		executionSeconds = std::max(
			0.0f, (kNightRoofChargeMaximum - mNightRoofCharge)
				/ chargePerSecond) + kNightRoofHijackerWarningDuration;
	}
}

/**
 * 只暂停锁定行坡段的花盆上层，不碰承载花盆本体、平台植物或其他行。
 * 由格子层 ID 判定 normal/pumpkin，避免临时覆盖层被误当作常规作物。
 */
bool Board::IsPlantPausedByRoofRunoff(const Plant* plant) const
{
	if (!plant || !plant->IsActive() || !IsRoofRunoffFlowing()
		|| !IsRoofRunoffRowSelected(plant->mRow) || plant->mColumn < 0
		|| plant->mColumn >= GetRoofSlopeColumnCount()
		|| plant->IsRoofSupportPlant()) return false;
	if (plant->mRow < 0 || plant->mRow >= mRows
		|| plant->mColumn >= mColumns) return false;

	Cell* cell = mCells[plant->mRow][plant->mColumn];
	if (!cell || (cell->GetNormalPlantID() != plant->mPlantID
		&& cell->GetPumpkinPlantID() != plant->mPlantID)) return false;
	Plant* support = mEntityRegistry.GetPlant(cell->GetUnderPlantID());
	return support && support->IsActive() && !support->IsSquished()
		&& support->IsRoofSupportPlant();
}

/**
 * 昼夜屋顶把当前雨势换算为可保留的全局积累；满值后只随机一次目标行组，
 * 依次经过可读预警和短时冲刷。晴天只缓慢排水，不会凭空触发事件。
 */
void Board::UpdateRoofRunoff(float deltaTime)
{
	if (!SupportsRoofRunoff()) {
		mRoofRunoffCharge = 0.0f;
		mRoofRunoffRetainedCharge = 0.0f;
		mRoofRunoffPhase = RoofRunoffPhase::IDLE;
		mRoofRunoffPhaseTimer = 0.0f;
		mRoofRunoffRowMask = 0;
		return;
	}
	if (deltaTime <= 0.0f) return;

	if (mRoofRunoffPhase == RoofRunoffPhase::WARNING
		|| mRoofRunoffPhase == RoofRunoffPhase::FLOWING) {
		mRoofRunoffPhaseTimer = std::max(0.0f,
			mRoofRunoffPhaseTimer - deltaTime);
		if (mRoofRunoffPhaseTimer > 0.0f) return;

		if (mRoofRunoffPhase == RoofRunoffPhase::WARNING) {
			mRoofRunoffPhase = RoofRunoffPhase::FLOWING;
			mRoofRunoffPhaseTimer = kRoofRunoffFlowDuration;
			return;
		}

		mRoofRunoffCharge = mRoofRunoffRetainedCharge;
		mRoofRunoffRetainedCharge = 0.0f;
		mRoofRunoffPhase = RoofRunoffPhase::IDLE;
		mRoofRunoffRowMask = 0;
		return;
	}

	float chargeDelta = 0.0f;
	switch (mRainIntensity) {
	case RainIntensity::CLEAR:
		chargeDelta = -kRoofRunoffClearDrainPerSecond;
		break;
	case RainIntensity::LIGHT:
		chargeDelta = kRoofRunoffLightChargePerSecond;
		break;
	case RainIntensity::MEDIUM:
		chargeDelta = kRoofRunoffMediumChargePerSecond;
		break;
	case RainIntensity::HEAVY:
		chargeDelta = kRoofRunoffHeavyChargePerSecond;
		break;
	}
	mRoofRunoffCharge = std::clamp(mRoofRunoffCharge
		+ chargeDelta * deltaTime, 0.0f, kRoofRunoffChargeMaximum);
	if (mRoofRunoffCharge < kRoofRunoffChargeMaximum || mRows <= 0) return;

	// 锁行组只抽一次并随档保存；不按敌我密度选行，避免系统暗中追打当前最拥挤防线。
	mRoofRunoffPhase = RoofRunoffPhase::WARNING;
	mRoofRunoffPhaseTimer = kRoofRunoffWarningDuration;
	// 下一轮起点和行组同时预抽并入档，避免活动阶段读档改变后续湿度。
	mRoofRunoffRetainedCharge = static_cast<float>(GameRandom::Range(
		kRoofRunoffRetainedChargeMin, kRoofRunoffRetainedChargeMax));
	const int rowCountRoll = GameRandom::Range(1,
		kRoofRunoffOneRowWeight + kRoofRunoffTwoRowWeight + kRoofRunoffThreeRowWeight);
	int rowCount = rowCountRoll <= kRoofRunoffOneRowWeight ? 1
		: (rowCountRoll <= kRoofRunoffOneRowWeight + kRoofRunoffTwoRowWeight ? 2 : 3);
	rowCount = std::min(rowCount, mRows);
	mRoofRunoffRowMask = 0;
	while (GetRoofRunoffRowCount() < rowCount) {
		mRoofRunoffRowMask |= 1 << GameRandom::Range(0, mRows - 1);
	}

	// 导流车只能替换本次已经抽中的一行，不能扩充冲刷规模；最终掩码仍是唯一入档事实。
	const int guideRow = GetRoofRunoffGuideCandidateRow();
	if (guideRow >= 0 && !IsRoofRunoffRowSelected(guideRow)) {
		const int replacedIndex = GameRandom::Range(0, rowCount - 1);
		int selectedIndex = 0;
		for (int row = 0; row < mRows; ++row) {
			if (!IsRoofRunoffRowSelected(row)) continue;
			if (selectedIndex++ != replacedIndex) continue;
			mRoofRunoffRowMask &= ~(1 << row);
			break;
		}
		mRoofRunoffRowMask |= 1 << guideRow;
	}
}

/**
 * 自然锁行只采样仍在坡段内的活动导流车；最近房屋者优先，ID 只负责同 X 稳定决胜。
 */
int Board::GetRoofRunoffGuideCandidateRow() const
{
	if (!SupportsRoofRunoff()) return -1;
	const float slopeEndX = GetRoofSlopeEndX();
	int bestRow = -1;
	int bestID = NULL_ZOMBIE_ID;
	float bestX = std::numeric_limits<float>::max();
	for (const int id : mEntityRegistry.GetAllZombieIDs()) {
		const Zombie* zombie = mEntityRegistry.GetZombie(id);
		if (!zombie || zombie->mRow < 0 || zombie->mRow >= mRows
			|| !zombie->CanGuideRoofRunoff()) {
			continue;
		}
		const float x = zombie->GetPosition().x;
		if (x > slopeEndX) continue;
		if (x < bestX || (x == bestX && (bestID == NULL_ZOMBIE_ID || id < bestID))) {
			bestX = x;
			bestID = id;
			bestRow = zombie->mRow;
		}
	}
	return bestRow;
}

/** 校验并恢复径流状态；损坏组合和非屋顶存档都回到中性状态。 */
void Board::RestoreRoofRunoffState(float charge, RoofRunoffPhase phase,
	int rowMask, float phaseTimer, float retainedCharge)
{
	mRoofRunoffCharge = 0.0f;
	mRoofRunoffRetainedCharge = 0.0f;
	mRoofRunoffPhase = RoofRunoffPhase::IDLE;
	mRoofRunoffPhaseTimer = 0.0f;
	mRoofRunoffRowMask = 0;
	if (!SupportsRoofRunoff() || !std::isfinite(charge)
		|| !std::isfinite(phaseTimer) || !std::isfinite(retainedCharge)) return;

	mRoofRunoffCharge = std::clamp(charge, 0.0f, kRoofRunoffChargeMaximum);
	if (phase != RoofRunoffPhase::WARNING && phase != RoofRunoffPhase::FLOWING) return;
	const int validRowMask = mRows > 0 ? (1 << mRows) - 1 : 0;
	rowMask &= validRowMask;
	if (rowMask == 0 || phaseTimer < 0.0f) {
		mRoofRunoffCharge = 0.0f;
		return;
	}
	mRoofRunoffCharge = kRoofRunoffChargeMaximum;
	mRoofRunoffPhase = phase;
	mRoofRunoffPhaseTimer = phaseTimer;
	mRoofRunoffRowMask = rowMask;
	mRoofRunoffRetainedCharge = std::clamp(retainedCharge,
		static_cast<float>(kRoofRunoffRetainedChargeMin),
		static_cast<float>(kRoofRunoffRetainedChargeMax));
}

/** 返回仍满足能力门禁的锁定实体；平时只按稳定 ID 查表，不扫描僵尸集合。 */
HijackerZombie* Board::GetValidNightRoofHijacker() const
{
	if (mNightRoofHijackerID == NULL_ZOMBIE_ID) return nullptr;
	auto* hijacker = dynamic_cast<HijackerZombie*>(
		mEntityRegistry.GetZombie(mNightRoofHijackerID));
	return hijacker && hijacker->CanBeNightRoofHijackerCandidate()
		? hijacker : nullptr;
}

/** 75% 边沿只遍历稀有劫持者弱索引；即使没有候选，本轮也不会在之后补选。 */
void Board::TryLockNightRoofHijacker()
{
	if (!SupportsNightRoofCharge() || mNightRoofHijackerSelectionAttempted) return;
	mNightRoofHijackerSelectionAttempted = true;
	auto hijacker = mEntityRegistry.SelectNightRoofHijacker();
	if (!hijacker) return;
	mNightRoofHijackerID = hijacker->mZombieID;
	hijacker->BeginNightRoofLock();
}

/** 满电时统一推演普通行与接地路线；有效劫持者把同一预警窗口延长到七秒。 */
void Board::BeginNightRoofChargeWarning()
{
	if (!SupportsNightRoofCharge()
		|| mNightRoofChargePhase != NightRoofChargePhase::CHARGING
		|| mRows <= 0) return;
	if (!mNightRoofHijackerSelectionAttempted) TryLockNightRoofHijacker();
	HijackerZombie* hijacker = GetValidNightRoofHijacker();
	mNightRoofCharge = kNightRoofChargeMaximum;
	mNightRoofChargePhase = NightRoofChargePhase::WARNING;
	mNightRoofHijackerWarningExtended = hijacker != nullptr;
	mNightRoofHijackerFinalizing = false;
	mNightRoofChargePhaseTimer = hijacker
		? kNightRoofHijackerWarningDuration : kNightRoofChargeWarningDuration;
	ChooseNightRoofChargeRoute(mNightRoofChargePhaseTimer);
	if (hijacker) hijacker->BeginNightRoofWarning();
}

/**
 * 在满电这一低频边沿采集纯数值快照，令所有普通行与每只现存接地僵尸共享同组 rollout。
 * 路线、行和稳定 ID 在这里一次锁定；后续死亡、换行、破甲或新出生都不会触发重选。
 */
void Board::ChooseNightRoofChargeRoute(float warningSeconds)
{
	using namespace PlantDefenseMonteCarlo;
	const auto decisionStarted = std::chrono::steady_clock::now();
	auto recordDecisionTime = [this, decisionStarted]() {
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - decisionStarted).count();
		mNightRoofChargeRouteDecisionMicros = static_cast<int>(std::clamp<int64_t>(
			elapsed, 0, std::numeric_limits<int>::max()));
	};
	mNightRoofChargeGuided = false;
	mNightRoofChargeGuideID = NULL_ZOMBIE_ID;
	mNightRoofChargeRouteUsedMonteCarlo = false;
	mNightRoofChargeRouteStats = {};
	mNightRoofChargeRouteDecisionMicros = 0;
	mNightRoofChargeRow = -1;
	if (mRows <= 0) {
		recordDecisionTime();
		return;
	}

	const std::vector<std::shared_ptr<Zombie>> guides =
		mEntityRegistry.GetNightRoofChargeGuideCandidates();
	std::vector<NightRoofChargeCandidate> candidates;
	candidates.reserve(static_cast<std::size_t>(mRows) + guides.size());
	for (int row = 0; row < mRows; ++row) {
		float damageMultiplier = 1.0f;
		for (int column = 0; column < mColumns; ++column) {
			const Plant* support = GetUnderPlantAt(row, column);
			if (support && support->IsActive() && !support->IsPreview()
				&& support->mPlantHealth > 0 && !support->IsSquished()) {
				damageMultiplier = std::max(damageMultiplier,
					support->GetNightRoofChargeZombieDamageMultiplier());
			}
		}
		NightRoofChargeCandidate candidate;
		candidate.row = row;
		candidate.resolveSeconds = warningSeconds;
		candidate.plantShutdownSeconds = kNightRoofPlantShutdownDuration;
		candidate.wetPlantShutdownSeconds = kNightRoofWetPlantShutdownDuration;
		candidate.wetSlopeColumnCount = GetRoofSlopeColumnCount();
		candidate.zombieDamage = kNightRoofZombieDamage * damageMultiplier;
		candidate.wetZombieDamage = kNightRoofWetZombieDamage * damageMultiplier;
		candidate.paralysisSeconds = kNightRoofZombieParalysisDuration;
		candidate.wetParalysisSeconds = kNightRoofWetZombieParalysisDuration;
		candidate.wetSlopeEndX = GetRoofSlopeEndX();
		candidate.wetRow = IsRoofRunoffFlowing() && IsRoofRunoffRowSelected(row);
		candidates.push_back(candidate);
	}
	for (const std::shared_ptr<Zombie>& guide : guides) {
		if (!guide) continue;
		NightRoofChargeCandidate candidate;
		candidate.row = guide->mRow;
		candidate.guideZombieId = guide->mZombieID;
		candidate.resolveSeconds = warningSeconds;
		candidate.plantShutdownSeconds = kNightRoofPlantShutdownDuration;
		candidate.wetPlantShutdownSeconds = kNightRoofWetPlantShutdownDuration;
		candidate.wetSlopeColumnCount = GetRoofSlopeColumnCount();
		candidate.wetRow = IsRoofRunoffFlowing()
			&& IsRoofRunoffRowSelected(guide->mRow);
		candidate.guided = true;
		candidates.push_back(candidate);
	}

	Snapshot snapshot;
	bool snapshotBuilt = false;
	if (GameAPP::GetInstance().mEnableMonteCarloAI) {
		PROFILE_SCOPE("MC.NightRoofRoute.Snapshot");
		snapshotBuilt = BuildMonteCarloCombatSnapshot(snapshot, false, true);
	}
	if (snapshotBuilt) {
		std::unordered_set<int> guideIDs;
		for (const std::shared_ptr<Zombie>& guide : guides) {
			if (guide) guideIDs.insert(guide->mZombieID);
		}
		for (ZombieSnapshot& zombie : snapshot.zombies) {
			zombie.forcedForDecision = guideIDs.find(zombie.id) != guideIDs.end();
		}

		NightRoofChargeConfig config;
		ConfigureMonteCarloCombatConfig(config.combat,
			kNightRoofRouteMonteCarloRolloutCount,
			kNightRoofRouteMonteCarloHorizonSeconds);
		config.hijackerZombieId = GetNightRoofHijackerID();
		config.hijackerExecutionSeconds = config.hijackerZombieId != NULL_ZOMBIE_ID
			? warningSeconds : -1.0f;
		config.survivalMode = mIsSurvival;
		config.survivalExecutionLineCap =
			static_cast<float>(kNightRoofHijackerSurvivalLineCap);
		config.guideImmunitySeconds = kGroundingZombieControlImmunityDuration;
		config.guideImmunityRadius = kGroundingZombieControlImmunityRadius;

		for (const int plantID : mEntityRegistry.GetAllPlantIDs()) {
			const Plant* plant = mEntityRegistry.GetPlant(plantID);
			if (!plant || !plant->IsActive() || plant->IsPreview()
				|| plant->IsSquished() || plant->GetSleepState()
				|| plant->mPlantType != PlantType::PLANT_ICESHROOM
				|| plant->GetCurrentTrackName() != "anim_idle") continue;
			const float currentFrame = plant->GetCurrentFrame();
			const float speed = plant->GetAnimationSpeed();
			if (currentFrame >= 16.0f || speed <= 0.0f) continue;
			config.pendingControlEvents.push_back({
				plantID,
				std::max(0.0f, (16.0f - currentFrame) / (12.0f * speed)),
				20.0f,
				20.0f,
				4.0f,
				6.0f
			});
		}

		std::uint32_t seed = 2166136261u;
		auto mixSeed = [&seed](std::uint32_t value) {
			seed ^= value;
			seed *= 16777619u;
		};
		mixSeed(static_cast<std::uint32_t>(mBoardFrame));
		mixSeed(static_cast<std::uint32_t>(mCurrentWave));
		mixSeed(static_cast<std::uint32_t>(mNightRoofHijackerID));
		NightRoofChargeResult result;
		{
			PROFILE_SCOPE("MC.NightRoofRoute.Rollouts");
			result = PlantDefenseMonteCarlo::ChooseNightRoofChargeRoute(
				snapshot, candidates, config, seed);
		}
		mNightRoofChargeRouteStats.rolloutCount = result.rolloutCount;
		mNightRoofChargeRouteStats.candidateCount =
			static_cast<int>(candidates.size());
		mNightRoofChargeRouteStats.sampledZombieCount = result.sampledZombieCount;
		mNightRoofChargeRouteStats.sampledPlantCount = result.sampledPlantCount;
		mNightRoofChargeRouteStats.supportPlantCount = result.supportPlantCount;
		mNightRoofChargeRouteStats.cardCount = result.cardCount;
		mNightRoofChargeRouteStats.bestScore = result.score;
		if (result.candidateIndex >= 0
			&& result.candidateIndex < static_cast<int>(candidates.size())) {
			const NightRoofChargeCandidate& chosen = candidates[result.candidateIndex];
			mNightRoofChargeRow = chosen.row;
			mNightRoofChargeGuided = chosen.guided;
			mNightRoofChargeGuideID = chosen.guided
				? chosen.guideZombieId : NULL_ZOMBIE_ID;
			mNightRoofChargeRouteUsedMonteCarlo = true;
			recordDecisionTime();
			return;
		}
	}

	// 推演不可用时才消费正式 RNG：有接地候选就只在稳定 ID 候选集中均匀选，否则随机普通行。
	if (!guides.empty()) {
		const int index = GameRandom::Range(0, static_cast<int>(guides.size()) - 1);
		const std::shared_ptr<Zombie>& guide = guides[static_cast<std::size_t>(index)];
		if (guide) {
			mNightRoofChargeRow = guide->mRow;
			mNightRoofChargeGuided = true;
			mNightRoofChargeGuideID = guide->mZombieID;
		}
	} else {
		mNightRoofChargeRow = GameRandom::Range(0, mRows - 1);
	}
	recordDecisionTime();
}

/**
 * 统一接收雨势与自然闪电增量。积累阶段跨过 100 的同一笔增量保留溢出；
 * 预警和放电演出期间的新增电荷也只进入余电，不改变本次已公开的放电强度。
 */
void Board::AddNightRoofCharge(float amount)
{
	if ((IsWeatherStation() && mWeatherStation.controls[WeatherStationRules::CHARGE].value==0)
		|| !SupportsNightRoofCharge() || amount <= 0.0f
		|| !std::isfinite(amount)) return;
	if (mNightRoofChargePhase == NightRoofChargePhase::CHARGING) {
		const float combinedCharge = mNightRoofCharge + amount;
		mNightRoofCharge = std::clamp(combinedCharge,
			0.0f, kNightRoofChargeMaximum);
		if (!mNightRoofHijackerSelectionAttempted
			&& combinedCharge >= kNightRoofHijackerLockThreshold) {
			TryLockNightRoofHijacker();
		}
		if (combinedCharge >= kNightRoofChargeMaximum) {
			const float overflow = combinedCharge - kNightRoofChargeMaximum;
			BeginNightRoofChargeWarning();
			mNightRoofOvercharge = std::clamp(
				mNightRoofOvercharge + overflow,
				0.0f, kNightRoofOverchargeMaximum);
		}
		return;
	}
	mNightRoofOvercharge = std::clamp(mNightRoofOvercharge + amount,
		0.0f, kNightRoofOverchargeMaximum);
}

int Board::GetNightRoofExecutionLine() const
{
	const HijackerZombie* hijacker = GetValidNightRoofHijacker();
	if (!hijacker) return 0;
	const int currentHealth = hijacker->GetCountableExecutionHealth();
	return mIsSurvival
		? std::min(currentHealth, kNightRoofHijackerSurvivalLineCap)
		: currentHealth;
}

bool Board::IsZombieThreatenedByNightRoofHijacker(const Zombie* zombie) const
{
	const int line = GetNightRoofExecutionLine();
	return line > 0 && zombie && zombie->mZombieID != mNightRoofHijackerID
		&& !zombie->IsPreview() && zombie->IsActive() && !zombie->IsDying()
		&& zombie->GetCountableExecutionHealth() > 0
		&& zombie->GetCountableExecutionHealth() <= line;
}

/** 普通层与南瓜壳合并计血；承载层永远不属于处决组，飞行覆盖层只随宿主一并清除。 */
bool Board::IsPlantThreatenedByNightRoofHijacker(const Plant* plant) const
{
	const int line = GetNightRoofExecutionLine();
	if (line <= 0 || !plant || !plant->IsActive() || plant->IsPreview()
		|| plant->IsSquished() || plant->mRow < 0 || plant->mRow >= mRows
		|| plant->mColumn < 0 || plant->mColumn >= mColumns) return false;
	const Cell* cell = mCells[plant->mRow][plant->mColumn];
	if (!cell || plant->mPlantID == cell->GetUnderPlantID()) return false;
	if (plant->mPlantID != cell->GetNormalPlantID()
		&& plant->mPlantID != cell->GetPumpkinPlantID()
		&& plant->mPlantID != cell->GetOverlayPlantID()) return false;
	if (GetNightRoofHijackerSupportProtector(plant)) return false;

	int64_t groupHealth = 0;
	bool hasHostLayer = false;
	for (const int id : { cell->GetNormalPlantID(), cell->GetPumpkinPlantID() }) {
		const Plant* member = mEntityRegistry.GetPlant(id);
		if (!member || !member->IsActive() || member->IsPreview()
			|| member->IsSquished()) continue;
		hasHostLayer = true;
		groupHealth += std::max(0, member->mPlantHealth);
	}
	return hasHostLayer && groupHealth > 0 && groupHealth <= line;
}

Plant* Board::GetNightRoofChargeSupportProtector(const Plant* target) const
{
	if (!target) return nullptr;
	const PlantFootprint footprint = GetPlantFootprint(target->mPlantType);
	for (std::size_t i = 0; i < footprint.count; ++i) {
		Plant* support = GetUnderPlantAt(
			target->mRow + footprint.cells[i].rowOffset,
			target->mColumn + footprint.cells[i].columnOffset);
		if (support && support->IsActive() && support->mPlantHealth > 0
			&& !support->IsPreview() && !support->IsSquished()
			&& support->ProtectsSupportedPlantFromNightRoofCharge(target)) {
			return support;
		}
	}
	return nullptr;
}

Plant* Board::GetNightRoofHijackerSupportProtector(const Plant* target) const
{
	if (!target) return nullptr;
	const PlantFootprint footprint = GetPlantFootprint(target->mPlantType);
	for (std::size_t i = 0; i < footprint.count; ++i) {
		Plant* support = GetUnderPlantAt(
			target->mRow + footprint.cells[i].rowOffset,
			target->mColumn + footprint.cells[i].columnOffset);
		if (support && support->IsActive() && support->mPlantHealth > 0
			&& !support->IsPreview() && !support->IsSquished()
			&& support->ProtectsSupportedPlantFromNightRoofHijacker(target)) {
			return support;
		}
	}
	return nullptr;
}

float Board::GetNightRoofHijackerPulseAlpha() const
{
	if (!GetValidNightRoofHijacker()) return 0.0f;
	const float wave = 0.5f + 0.5f * std::sin(
		static_cast<float>(mBoardFrame) * (mNightRoofHijackerFinalizing ? 0.55f : 0.20f));
	return mNightRoofHijackerFinalizing
		? 125.0f + 90.0f * wave : 45.0f + 75.0f * wave;
}

void Board::CancelNightRoofHijacker(int zombieID)
{
	if (zombieID == NULL_ZOMBIE_ID || zombieID != mNightRoofHijackerID) return;
	auto* hijacker = dynamic_cast<HijackerZombie*>(mEntityRegistry.GetZombie(zombieID));
	mNightRoofHijackerID = NULL_ZOMBIE_ID;
	mNightRoofHijackerFinalizing = false;
	if (hijacker) hijacker->ClearNightRoofLock();
}

void Board::ResetNightRoofHijackerCycle()
{
	if (auto* hijacker = dynamic_cast<HijackerZombie*>(
		mEntityRegistry.GetZombie(mNightRoofHijackerID))) {
		hijacker->ClearNightRoofLock();
	}
	mNightRoofHijackerSelectionAttempted = false;
	mNightRoofHijackerID = NULL_ZOMBIE_ID;
	mNightRoofHijackerWarningExtended = false;
	mNightRoofHijackerFinalizing = false;
}

/**
 * 目标先按格子和稳定 ID 完整快照，再统一死亡。这样植物槽位释放、施法者掉头和特殊僵尸
 * 死亡回调都不能改变本次集合；气球额外生命不在 GetCountableExecutionHealth 中。
 */
void Board::ResolveNightRoofHijackerExecution()
{
	HijackerZombie* caster = GetValidNightRoofHijacker();
	const int line = GetNightRoofExecutionLine();
	if (!caster || line <= 0) return;

	std::unordered_set<int> plantTargetIDs;
	std::unordered_set<int> protectedSupportIDs;
	std::unordered_set<int> processedNormalPlantIDs;
	for (int row = 0; row < mRows; ++row) {
		for (int column = 0; column < mColumns; ++column) {
			const Cell* cell = mCells[row][column];
			if (!cell) continue;
			const int normalID = cell->GetNormalPlantID();
			if (normalID != NULL_PLANT_ID
				&& !processedNormalPlantIDs.insert(normalID).second) continue;
			int64_t groupHealth = 0;
			std::vector<int> group;
			for (const int id : { cell->GetNormalPlantID(), cell->GetPumpkinPlantID() }) {
				Plant* plant = mEntityRegistry.GetPlant(id);
				if (!plant || !plant->IsActive() || plant->IsPreview()
					|| plant->IsSquished()) continue;
				groupHealth += std::max(0, plant->mPlantHealth);
				group.push_back(id);
			}
			if (group.empty() || groupHealth <= 0 || groupHealth > line) continue;
			if (Plant* protector = GetNightRoofHijackerSupportProtector(
				mEntityRegistry.GetPlant(group.front()))) {
				protectedSupportIDs.insert(protector->mPlantID);
				continue;
			}
			plantTargetIDs.insert(group.begin(), group.end());
			if (Plant* overlay = mEntityRegistry.GetPlant(cell->GetOverlayPlantID());
				overlay && overlay->IsActive() && !overlay->IsPreview()) {
				plantTargetIDs.insert(overlay->mPlantID);
			}
		}
	}
	std::vector<int> plantTargets(plantTargetIDs.begin(), plantTargetIDs.end());
	std::sort(plantTargets.begin(), plantTargets.end());

	std::vector<int> zombieTargets;
	for (const int id : mEntityRegistry.GetAllZombieIDs()) {
		Zombie* zombie = mEntityRegistry.GetZombie(id);
		if (!zombie || id == caster->mZombieID || zombie->IsPreview()
			|| !zombie->IsActive() || zombie->IsDying()) continue;
		const int health = zombie->GetCountableExecutionHealth();
		if (health > 0 && health <= line) zombieTargets.push_back(id);
	}
	std::sort(zombieTargets.begin(), zombieTargets.end());

	// 成功释放从这一提交边沿开始封锁本波余下候选，并跳过后续两个完整波次。
	BeginHijackerSpawnCooldown();
	// 先解除 Board 交叉引用，施法者掉头粒子和死亡回调便不会取消或重入这次批处理。
	mNightRoofHijackerID = NULL_ZOMBIE_ID;
	mNightRoofHijackerFinalizing = false;
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("HijackerElectricFlash", caster->GetVisualPosition());
	}
	// 不按目标数量叠音；只在有效处决快照真正提交时给一次全场听觉落点。
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_HIJACKER_EXECUTE,
		kNightRoofHijackerExecuteVolume);
	caster->TakeHijackerExecution();
	std::vector<int> sortedProtectedSupportIDs(
		protectedSupportIDs.begin(), protectedSupportIDs.end());
	std::sort(sortedProtectedSupportIDs.begin(), sortedProtectedSupportIDs.end());
	for (const int supportID : sortedProtectedSupportIDs) {
		if (Plant* support = mEntityRegistry.GetPlant(supportID)) {
			support->OnNightRoofChargeProtectionTriggered();
		}
	}

	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_EXPLOSION, 0.7f);

	for (const int id : plantTargets) {
		Plant* plant = mEntityRegistry.GetPlant(id);
		if (!plant || !plant->IsActive()) continue;
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("JackExplode", plant->GetVisualPosition());
		}
		plant->Die();
	}
	for (const int id : zombieTargets) {
		Zombie* zombie = mEntityRegistry.GetZombie(id);
		if (!zombie || !zombie->IsActive() || zombie->IsDying()) continue;
		if (g_particleSystem) {
			g_particleSystem->EmitEffect("HijackerElectricFlash", zombie->GetVisualPosition() +
				Vector(23.0f, 27.0f));
		}
		zombie->TakeHijackerExecution();
	}
}

/**
 * 放电只在 WARNING 转入 DISCHARGING 的边沿结算一次。植物先按稳定 ID 冻结
 * 接地保护分配，引导路线再按放电时位置发放编队免控，普通路线中的僵尸随后消费
 * 同一批仍有效的接地范围，最后统一结算植物反噬；
 * 读档恢复 DISCHARGING 不会重复伤害。
 */
void Board::ResolveNightRoofChargeDischarge()
{
	if (!SupportsNightRoofCharge() || mNightRoofChargeRow < 0
		|| mNightRoofChargeRow >= mRows) return;

	const int row = mNightRoofChargeRow;
	const bool wetRow = IsRoofRunoffFlowing() && IsRoofRunoffRowSelected(row);
	std::vector<int> zombieIDs = mEntityRegistry.GetAllZombieIDs();
	std::sort(zombieIDs.begin(), zombieIDs.end());
	if (mNightRoofChargeGuided) {
		Zombie* guide = mEntityRegistry.GetZombie(mNightRoofChargeGuideID);
		if (guide && guide->CanGuideNightRoofCharge()) {
			auto getColliderCenter = [](const Zombie* zombie) {
				Vector center = zombie->GetPosition();
				if (const ColliderComponent* collider = zombie->GetColliderComponent()) {
					const SDL_FRect bounds = collider->GetBoundingBox();
					center = Vector(bounds.x + bounds.w * 0.5f,
						bounds.y + bounds.h * 0.5f);
				}
				return center;
			};
			const Vector guideCenter = getColliderCenter(guide);
			const float radiusSquared = kGroundingZombieControlImmunityRadius
				* kGroundingZombieControlImmunityRadius;
			// 放电边沿冻结稳定 ID 快照；同阵营目标一经获得免控，随后走出范围也保留余时。
			for (const int id : zombieIDs) {
				Zombie* target = mEntityRegistry.GetZombie(id);
				if (!target || !target->IsActive() || target->IsDying()
					|| target->IsPreview()
					|| target->IsMindControlled() != guide->IsMindControlled()) {
					continue;
				}
				const Vector targetCenter = getColliderCenter(target);
				const float dx = targetCenter.x - guideCenter.x;
				const float dy = targetCenter.y - guideCenter.y;
				if (dx * dx + dy * dy > radiusSquared) continue;
				target->GrantControlImmunity(kGroundingZombieControlImmunityMask,
					kGroundingZombieControlImmunityDuration, true);
			}
			Vector anchor;
			if (g_particleSystem && guide->TryGetNightRoofChargeGuideAnchor(anchor)) {
				g_particleSystem->EmitEffect("GroundingZombieLightning", anchor);
			}
		}
	}
	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	std::vector<int> groundingProviderIDs;
	std::unordered_set<int> groundingProviderSet;
	std::unordered_set<int> protectedSupportIDs;
	float zombieDamageMultiplier = 1.0f;
	for (int column = 0; column < mColumns; ++column) {
		Plant* support = GetUnderPlantAt(row, column);
		if (!support || !support->IsActive() || support->IsPreview()
			|| support->mPlantHealth <= 0 || support->IsSquished()) {
			continue;
		}
		const float multiplier = support->GetNightRoofChargeZombieDamageMultiplier();
		zombieDamageMultiplier = std::max(zombieDamageMultiplier, multiplier);
		if (multiplier > 1.0f) protectedSupportIDs.insert(support->mPlantID);
	}
	for (const int id : plantIDs) {
		Plant* plant = mEntityRegistry.GetPlant(id);
		if (!plant || !plant->IsActive() || plant->IsPreview()
			|| plant->IsSquished() || plant->IsBungeeTargeted()
			|| plant->mRow != row || plant->IsRoofSupportPlant()) {
			continue;
		}
		if (Plant* support = GetNightRoofChargeSupportProtector(plant)) {
			protectedSupportIDs.insert(support->mPlantID);
			continue;
		}

		Plant* groundingProvider = nullptr;
		int nearestColumnDistance = std::numeric_limits<int>::max();
		for (const int providerID : plantIDs) {
			Plant* candidate = mEntityRegistry.GetPlant(providerID);
			if (!candidate || !candidate->CanGroundNightRoofChargeFor(plant)) continue;
			const int columnDistance = std::abs(candidate->mColumn - plant->mColumn);
			// plantIDs 已排序；同距时保留先遇到的较小稳定 ID。
			if (columnDistance < nearestColumnDistance) {
				nearestColumnDistance = columnDistance;
				groundingProvider = candidate;
			}
		}
		if (groundingProvider) {
			if (groundingProviderSet.insert(groundingProvider->mPlantID).second) {
				groundingProviderIDs.push_back(groundingProvider->mPlantID);
			}
			continue;
		}

		const bool onWetSlope = wetRow && plant->mColumn >= 0
			&& plant->mColumn < GetRoofSlopeColumnCount();
		plant->ApplyShutdown(onWetSlope
			? kNightRoofWetPlantShutdownDuration
			: kNightRoofPlantShutdownDuration);
	}
	for (const int id : zombieIDs) {
		if (mNightRoofChargeGuided) break;
		Zombie* zombie = mEntityRegistry.GetZombie(id);
		if (!zombie || !zombie->IsActive() || zombie->IsDying()
			|| zombie->mRow != row
			|| !zombie->CanBeAffectedByGroundHazards()) {
			continue;
		}
		const bool onWetSlope = wetRow
			&& zombie->GetPosition().x <= GetRoofSlopeEndX();
		const int baseDamage = static_cast<int>(std::lround((onWetSlope
			? kNightRoofWetZombieDamage : kNightRoofZombieDamage)
			* zombieDamageMultiplier));
		const float paralysisDuration = onWetSlope
			? kNightRoofWetZombieParalysisDuration
			: kNightRoofZombieParalysisDuration;

		// 绝缘者按距离、再按稳定 ID 选出唯一承接者。自身若可承接以零距离优先；
		// 湿润绝缘者会拒绝承接，随后走它自己的湿坡 360 点胸甲结算。
		Zombie* protector = nullptr;
		float nearestDistance = std::numeric_limits<float>::max();
		for (const int providerID : zombieIDs) {
			Zombie* candidate = mEntityRegistry.GetZombie(providerID);
			if (!candidate || !candidate->CanProtectFromNightRoofCharge(zombie)) continue;
			const float distance = std::abs(
				candidate->GetPosition().x - zombie->GetPosition().x);
			if (distance < nearestDistance) {
				nearestDistance = distance;
				protector = candidate;
			}
		}
		if (protector && protector->AbsorbNightRoofChargeFor(zombie, baseDamage)) {
			continue;
		}
		zombie->TakeNightRoofChargeImpact(
			baseDamage, paralysisDuration, onWetSlope);
	}

	std::vector<int> sortedProtectedSupportIDs(
		protectedSupportIDs.begin(), protectedSupportIDs.end());
	std::sort(sortedProtectedSupportIDs.begin(), sortedProtectedSupportIDs.end());
	for (const int supportID : sortedProtectedSupportIDs) {
		if (Plant* support = mEntityRegistry.GetPlant(supportID)) {
			support->OnNightRoofChargeProtectionTriggered();
		}
	}

	// 同次放电的植物与僵尸效果已经全部冻结；现在反噬死亡不会改变本次接地结果。
	std::sort(groundingProviderIDs.begin(), groundingProviderIDs.end());
	for (const int providerID : groundingProviderIDs) {
		Plant* provider = mEntityRegistry.GetPlant(providerID);
		if (!provider) continue;
		const bool onWetSlope = wetRow && provider->mColumn >= 0
			&& provider->mColumn < GetRoofSlopeColumnCount();
		provider->AbsorbGroundedNightRoofCharge(onWetSlope);
	}
}

bool Board::IsNightRoofChargeProtectionSuppressed(const Zombie* zombie) const
{
	if (!zombie) return false;
	std::vector<int> plantIDs = mEntityRegistry.GetAllPlantIDs();
	std::sort(plantIDs.begin(), plantIDs.end());
	for (const int id : plantIDs) {
		const Plant* plant = mEntityRegistry.GetPlant(id);
		if (plant && plant->SuppressesNightRoofChargeProtectionFor(zombie)) {
			return true;
		}
	}
	return false;
}

/**
 * 黑夜屋顶雷荷与径流并行推进。预警转放电时按一次快照结算实体效果；
 * 活动阶段新增电荷只截留为下一轮余电，避免改写已公开的本次强度或重复命中。
 */
void Board::UpdateNightRoofCharge(float deltaTime)
{
	if (!SupportsNightRoofCharge()) {
		ResetNightRoofHijackerCycle();
		mNightRoofCharge = 0.0f;
		mNightRoofOvercharge = 0.0f;
		mNightRoofChargePhase = NightRoofChargePhase::CHARGING;
		mNightRoofChargePhaseTimer = 0.0f;
		mNightRoofChargeRow = -1;
		mNightRoofChargeGuided = false;
		mNightRoofChargeGuideID = NULL_ZOMBIE_ID;
		return;
	}
	if (deltaTime <= 0.0f) return;

	float chargeDelta = 0.0f;
	switch (mRainIntensity) {
	case RainIntensity::CLEAR:
		chargeDelta = -kNightRoofChargeClearLeakPerSecond;
		break;
	case RainIntensity::LIGHT:
		chargeDelta = kNightRoofChargeLightPerSecond;
		break;
	case RainIntensity::MEDIUM:
		chargeDelta = kNightRoofChargeMediumPerSecond;
		break;
	case RainIntensity::HEAVY:
		chargeDelta = kNightRoofChargeHeavyPerSecond;
		break;
	}
	chargeDelta += GetNightRoofHijackerRainChargeBonusPerSecond();
	if (IsWeatherStation() && mWeatherStation.controls[WeatherStationRules::CHARGE].value==0) chargeDelta=0;

	if (mNightRoofChargePhase == NightRoofChargePhase::WARNING
		|| mNightRoofChargePhase == NightRoofChargePhase::DISCHARGING) {
		// 活动阶段只截留正向输入；晴夜泄漏要等余电兑现为下一轮主电荷后才重新生效。
		if (chargeDelta > 0.0f) AddNightRoofCharge(chargeDelta * deltaTime);
		if (mNightRoofChargePhase == NightRoofChargePhase::WARNING
			&& mNightRoofHijackerID != NULL_ZOMBIE_ID
			&& !GetValidNightRoofHijacker()) {
			CancelNightRoofHijacker(mNightRoofHijackerID);
		}
		mNightRoofChargePhaseTimer = std::max(0.0f,
			mNightRoofChargePhaseTimer - deltaTime);
		if (mNightRoofChargePhase == NightRoofChargePhase::WARNING
			&& !mNightRoofHijackerFinalizing
			&& mNightRoofChargePhaseTimer <= kNightRoofHijackerFinalDuration) {
			if (HijackerZombie* hijacker = GetValidNightRoofHijacker()) {
				mNightRoofHijackerFinalizing = true;
				hijacker->BeginNightRoofFinalization();
			}
		}
		if (mNightRoofChargePhaseTimer > 0.0f) return;

		if (mNightRoofChargePhase == NightRoofChargePhase::WARNING) {
			mNightRoofChargePhase = NightRoofChargePhase::DISCHARGING;
			mNightRoofChargePhaseTimer = kNightRoofChargeDischargeDuration;
			ResolveNightRoofHijackerExecution();
			ResolveNightRoofChargeDischarge();
			PlayWeatherThunder();
			return;
		}

		mNightRoofCharge = mNightRoofOvercharge;
		mNightRoofOvercharge = 0.0f;
		mNightRoofChargePhase = NightRoofChargePhase::CHARGING;
		mNightRoofChargePhaseTimer = 0.0f;
		mNightRoofChargeRow = -1;
		mNightRoofChargeGuided = false;
		mNightRoofChargeGuideID = NULL_ZOMBIE_ID;
		ResetNightRoofHijackerCycle();
		return;
	}

	if (chargeDelta > 0.0f) {
		AddNightRoofCharge(chargeDelta * deltaTime);
	}
	else {
		mNightRoofCharge = std::clamp(mNightRoofCharge
			+ chargeDelta * deltaTime, 0.0f, kNightRoofChargeMaximum);
	}
}

/** 校验并恢复黑夜屋顶雷荷；损坏组合和其他背景都回到中性积累状态。 */
void Board::RestoreNightRoofChargeState(float charge, NightRoofChargePhase phase,
	int row, float phaseTimer, float overcharge, bool hijackerSelectionAttempted,
	int hijackerID, bool hijackerWarningExtended, bool hijackerFinalizing,
	bool guided, int guideID)
{
	mNightRoofCharge = 0.0f;
	mNightRoofOvercharge = 0.0f;
	mNightRoofChargePhase = NightRoofChargePhase::CHARGING;
	mNightRoofChargePhaseTimer = 0.0f;
	mNightRoofChargeRow = -1;
	mNightRoofChargeGuided = false;
	mNightRoofChargeGuideID = NULL_ZOMBIE_ID;
	mNightRoofChargeRouteUsedMonteCarlo = false;
	mNightRoofChargeRouteStats = {};
	mNightRoofChargeRouteDecisionMicros = 0;
	mNightRoofHijackerSelectionAttempted = false;
	mNightRoofHijackerID = NULL_ZOMBIE_ID;
	mNightRoofHijackerWarningExtended = false;
	mNightRoofHijackerFinalizing = false;
	if (!SupportsNightRoofCharge() || !std::isfinite(charge)
		|| !std::isfinite(phaseTimer) || !std::isfinite(overcharge)) return;

	mNightRoofCharge = std::clamp(charge, 0.0f, kNightRoofChargeMaximum);
	mNightRoofHijackerSelectionAttempted = hijackerSelectionAttempted;
	mNightRoofHijackerID = hijackerSelectionAttempted
		? hijackerID : NULL_ZOMBIE_ID;
	if (phase == NightRoofChargePhase::CHARGING) return;
	if ((phase != NightRoofChargePhase::WARNING
		&& phase != NightRoofChargePhase::DISCHARGING)
		|| row < 0 || row >= mRows || phaseTimer < 0.0f) {
		mNightRoofCharge = 0.0f;
		ResetNightRoofHijackerCycle();
		return;
	}
	mNightRoofCharge = kNightRoofChargeMaximum;
	mNightRoofChargePhase = phase;
	mNightRoofHijackerWarningExtended = phase == NightRoofChargePhase::WARNING
		&& hijackerWarningExtended;
	mNightRoofHijackerFinalizing = mNightRoofHijackerWarningExtended
		&& hijackerFinalizing && phaseTimer <= kNightRoofHijackerFinalDuration;
	mNightRoofChargePhaseTimer = std::clamp(phaseTimer, 0.0f,
		phase == NightRoofChargePhase::WARNING
			? (mNightRoofHijackerWarningExtended
				? kNightRoofHijackerWarningDuration : kNightRoofChargeWarningDuration)
			: kNightRoofChargeDischargeDuration);
	mNightRoofChargeRow = row;
	mNightRoofChargeGuided = guided;
	mNightRoofChargeGuideID = guided ? guideID : NULL_ZOMBIE_ID;
	mNightRoofOvercharge = std::clamp(overcharge,
		0.0f, kNightRoofOverchargeMaximum);
	if (phase == NightRoofChargePhase::DISCHARGING) {
		mNightRoofHijackerID = NULL_ZOMBIE_ID;
		mNightRoofHijackerFinalizing = false;
	}
}

float Board::GetNightRoofHijackerRainChargeBonusPerSecond() const
{
	if (mRainIntensity == RainIntensity::CLEAR
		|| !mEntityRegistry.HasActiveNightRoofHijacker()) return 0.0f;
	return kNightRoofHijackerRainChargeBonusPerSecond;
}

void Board::FinalizeNightRoofHijackerLoad()
{
	if (mNightRoofHijackerID == NULL_ZOMBIE_ID) return;
	HijackerZombie* hijacker = GetValidNightRoofHijacker();
	if (!hijacker || mNightRoofChargePhase == NightRoofChargePhase::DISCHARGING) {
		CancelNightRoofHijacker(mNightRoofHijackerID);
		return;
	}
	const bool warning = mNightRoofChargePhase == NightRoofChargePhase::WARNING;
	hijacker->RestoreNightRoofPhase(
		true, warning && mNightRoofHijackerFinalizing, warning);
}

bool Board::SetRoofRunoffForTesting(float charge, RoofRunoffPhase phase,
	int rowMask, float phaseTimer, float retainedCharge)
{
	if (!SupportsRoofRunoff() || !std::isfinite(charge)
		|| !std::isfinite(phaseTimer) || !std::isfinite(retainedCharge)) return false;
	if (phase == RoofRunoffPhase::WARNING || phase == RoofRunoffPhase::FLOWING) {
		const int validRowMask = mRows > 0 ? (1 << mRows) - 1 : 0;
		if ((rowMask & validRowMask) == 0 || (rowMask & ~validRowMask) != 0) return false;
		if (phaseTimer <= 0.0f) {
			phaseTimer = phase == RoofRunoffPhase::WARNING
				? kRoofRunoffWarningDuration : kRoofRunoffFlowDuration;
		}
	}
	else {
		rowMask = 0;
		phaseTimer = 0.0f;
		retainedCharge = 0.0f;
	}
	RestoreRoofRunoffState(charge, phase, rowMask, phaseTimer, retainedCharge);
	return mRoofRunoffPhase == phase;
}

bool Board::SetNightRoofChargeForTesting(float charge, NightRoofChargePhase phase,
	int row, float phaseTimer, float overcharge)
{
	if (!SupportsNightRoofCharge() || !std::isfinite(charge)
		|| !std::isfinite(phaseTimer) || !std::isfinite(overcharge)) return false;
	if (phase == NightRoofChargePhase::WARNING
		|| phase == NightRoofChargePhase::DISCHARGING) {
		if (row < 0 || row >= mRows) return false;
		if (phaseTimer <= 0.0f) {
			phaseTimer = phase == NightRoofChargePhase::WARNING
				? kNightRoofChargeWarningDuration
				: kNightRoofChargeDischargeDuration;
		}
	}
	else {
		row = -1;
		phaseTimer = 0.0f;
		overcharge = 0.0f;
	}
	// AutoTest 可能在同一局中重设雷荷；参数校验后才解除旧锁定，失败调用不得改变局面。
	ResetNightRoofHijackerCycle();
	RestoreNightRoofChargeState(charge, phase, row, phaseTimer, overcharge,
		false, NULL_ZOMBIE_ID, false, false, false, NULL_ZOMBIE_ID);
	return mNightRoofChargePhase == phase;
}

bool Board::SupportsRoofRunoff() const
{
	return IsRoofBackground();
}

bool Board::SupportsNightRoofCharge() const
{
	return mBackGround == Background::NIGHT_ROOF || IsStationDeviceUnlocked(WeatherStationRules::CHARGE);
}

float Board::GetRoofRunoffZombieDriftVelocity(int row, float worldX) const
{
	if (!IsRoofRunoffFlowing() || !IsRoofRunoffRowSelected(row)
		|| worldX > GetRoofSlopeEndX()) return 0.0f;
	return kRoofRunoffZombieDriftSpeed;
}

int Board::GetRoofRunoffRowCount() const
{
	int count = 0;
	for (int row = 0; row < mRows; ++row) {
		if (IsRoofRunoffRowSelected(row)) ++count;
	}
	return count;
}

float Board::GetRoofRunoffFlowProgress() const
{
	if (!IsRoofRunoffFlowing()) return 0.0f;
	return std::clamp(1.0f - mRoofRunoffPhaseTimer / kRoofRunoffFlowDuration,
		0.0f, 1.0f);
}

float Board::GetNightRoofChargeDischargeProgress() const
{
	if (!IsNightRoofChargeDischarging()) return 0.0f;
	return std::clamp(1.0f
		- mNightRoofChargePhaseTimer / kNightRoofChargeDischargeDuration,
		0.0f, 1.0f);
}

bool Board::TryGetNightRoofChargeGuideAnchor(Vector& anchor) const
{
	if (!mNightRoofChargeGuided || mNightRoofChargeGuideID == NULL_ZOMBIE_ID) {
		return false;
	}
	const Zombie* guide = mEntityRegistry.GetZombie(mNightRoofChargeGuideID);
	return guide && guide->TryGetNightRoofChargeGuideAnchor(anchor);
}
