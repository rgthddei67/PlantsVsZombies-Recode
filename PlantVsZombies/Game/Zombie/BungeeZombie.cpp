#include "BungeeZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/Plant.h"
#include "../Plant/PlantType.h"
#include "../ShadowComponent.h"
#include "../../GameApp.h"
#include "../../ResourceManager.h"
#include "../../ResourceKeys.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace {
	constexpr int kBungeeBodyHealth = 450;                  // 原版蹦极僵尸本体生命
	constexpr float kInitialAltitudeBase = SCENE_HEIGHT + 180.0f; // 初始离地高度，按当前场景高派生，单位 px
	constexpr float kInitialAltitudeJitter = 150.0f;        // 同批蹦极下落起点随机差，单位 px
	constexpr float kDiveSpeed = 600.0f;                    // 下落速度，单位 px/s
	constexpr float kRiseSpeed = 700.0f;                   // 抓取后的瞬时抽离速度，单位 px/s
	constexpr float kScreamAltitude = SCENE_HEIGHT * 0.75f; // 下降到此高度后播放一次尖叫，单位 px
	constexpr float kBottomWaitSeconds = 5.0f;              // 落地到开始抓取的等待时间，单位游戏秒
	constexpr float kExitAltitude = SCENE_HEIGHT + 80.0f;   // 上升超过此高度后结算离场，单位 px
	constexpr float kDropClipSpeed = 2.0f;                  // 原版 24 FPS 相对 12 FPS 资源的倍率
	constexpr float kGrabClipSpeed = 2.0f;                  // 抓取轨道播放倍率
	constexpr float kRaiseClipSpeed = 3.0f;                 // 原版 36 FPS 相对 12 FPS 资源的倍率
	constexpr float kCargoOffsetX = -10.0f;                 // 被提植物对齐蹦极双手中心的水平视觉偏移，单位 px
	constexpr float kCargoOffsetY = -12.0f;                 // 被提植物相对目标格的垂直视觉偏移，单位 px
	constexpr float kCordOffsetX = 40.0f;                   // 绳索相对僵尸视觉原点的水平偏移，单位 px
	constexpr float kCordEndOffsetY = 10.0f;                // 绳索向下延伸进身体背后的固定点，单位 px
	constexpr float kBungeeSoundVolume = 0.45f;             // 蹦极登场、尖叫与抓取音效音量
	constexpr float kUmbrellaBounceVolume = 0.4f;           // 叶子保护伞弹回蹦极时的 boing 音量
	constexpr float kOccupiedCellWeight = 10000.0f;         // 原版有植物格随机权重
	constexpr float kEmptyCellWeight = 1.0f;                // 原版空格随机权重
	constexpr int kMonteCarloCandidateRolloutBudget = 384;  // 单次选点最多推进的“候选×样本”总评估量

	constexpr std::array<const char*, 4> kFrontArmTracks = {
		"Zombie_bungi_rightarm_lower2",
		"Zombie_bungi_rightarm_hand2",
		"Zombie_bungi_leftarm_lower2",
		"Zombie_bungi_leftarm_hand2",
	};
}

void BungeeZombie::SetupZombie()
{
	mBodyMaxHealth = kBungeeBodyHealth;
	mBodyHealth = kBungeeBodyHealth;
	mSpeed = 0.0f;
	mNeedDropArm = false;
	mNeedDropHead = false;
	mHasArm = true;
	mHasHead = true;
	mPhase = Phase::DIVING;
	mAltitude = kInitialAltitudeBase;

	if (auto* shadow = GetShadow()) {
		shadow->SetVisible(false);
	}
	if (mCollider) {
		mCollider->size = Vector(110.0f, 94.0f);
		mCollider->offset = Vector(-55.0f, -78.0f);
		mCollider->mEnabled = false;
	}

	ConfigureFrontArmLayers();
	if (mIsPreview) {
		mAltitude = 0.0f;
		PlayTrack("anim_idle", kDropClipSpeed);
		return;
	}
	PlayTrack("anim_drop", kDropClipSpeed);
}

void BungeeZombie::PlaySpawnSound()
{
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_GRASSSTEP,
		kBungeeSoundVolume);
}

void BungeeZombie::ZombieItemUpdate() const
{
	Zombie::ZombieItemUpdate();
	// 通用读档恢复会按地面僵尸重新打开阴影；蹦极全阶段都由绳索悬挂，不应留下地面黑影。
	if (auto* shadow = GetShadow()) shadow->SetVisible(false);
}

void BungeeZombie::ZombieUpdate(float scaledTime)
{
	if (!mTargetInitialized) {
		// 同波蹦极会在同一固定步进入这里；逐只领取 Board 名额，把昂贵推演摊到不同渲染帧。
		if (GameAPP::GetInstance().mEnableMonteCarloAI
			&& !mBoard->TryClaimMonteCarloBungeeDecisionSlot()) {
			return;
		}
		if (!SelectTarget()) Die();
		return;
	}

	// 蹦极始终悬在预订格正上方；逻辑位置保持在地面，只有视觉高度变化。
	const Vector center = mBoard->GetCellCenterPosition(mTargetRow, mTargetColumn);
	SetPosition(Vector(center.x,
		mBoard->GetZombieSpawnY(mTargetRow, center.x)));
	CommitRow(mTargetRow);

	switch (mPhase) {
	case Phase::DIVING:
		mAltitude = std::max(0.0f, mAltitude - kDiveSpeed * scaledTime);
		if (!mScreamPlayed && mAltitude <= kScreamAltitude) {
			const int choice = GameRandom::Range(0, 2);
			const std::array<std::string, 3> screams = {
				ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM,
				ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM2,
				ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM3,
			};
			AudioSystem::PlaySound(screams[choice], kBungeeSoundVolume);
			mScreamPlayed = true;
		}
		if (mAltitude <= 0.0f) LandAtTarget();
		break;
	case Phase::AT_BOTTOM:
		mPhaseTimer = std::max(0.0f, mPhaseTimer - scaledTime);
		if (mPhaseTimer <= 0.0f) BeginGrab();
		break;
	case Phase::GRABBING:
		if (GetCurrentTrackName() == "anim_hold") BeginRise();
		break;
	case Phase::RISING:
		mAltitude += kRiseSpeed * scaledTime;
		UpdateCargoOffset();
		if (mAltitude >= kExitAltitude) Die();
		break;
	}
}

bool BungeeZombie::SelectTarget()
{
	if (GameAPP::GetInstance().mEnableMonteCarloAI
		&& SelectMonteCarloTarget()) {
		mTargetMode = TargetMode::MONTE_CARLO;
		return true;
	}
	mTargetMode = TargetMode::RANDOM;
	return SelectOriginalRandomTarget();
}

bool BungeeZombie::SelectMonteCarloTarget()
{
	std::vector<int> eligiblePlantIDs;
	std::unordered_set<int> eligiblePlantIDSet;
	for (int row = 0; row < mBoard->mRows; ++row) {
		for (int column = 0; column < mBoard->mColumns; ++column) {
			if (IsCellReserved(row, column)) continue;
			if (Plant* plant = ResolveBungeePlantAt(row, column)) {
				// footprint 会让同一实体出现在多个 Cell；蒙特卡洛候选必须按实体去重。
				if (eligiblePlantIDSet.insert(plant->mPlantID).second) {
					eligiblePlantIDs.push_back(plant->mPlantID);
				}
			}
		}
	}
	if (eligiblePlantIDs.empty()) return false;

	int plantID = NULL_PLANT_ID;
	if (!mBoard->PickMonteCarloPlantRemovalTarget(
		eligiblePlantIDs, mZombieID, plantID, &mMonteCarloStats,
		0.0f, 0, nullptr, kMonteCarloCandidateRolloutBudget)) {
		return false;
	}
	Plant* plant = mBoard->mEntityRegistry.GetPlant(plantID);
	if (!plant || plant->IsBungeeTargeted()) return false;
	ApplySelectedCell({ plant->mRow, plant->mColumn, plantID });
	return true;
}

bool BungeeZombie::SelectOriginalRandomTarget()
{
	int untargetedSunflowers = 0;
	std::unordered_set<int> sunflowerIDs;
	for (int row = 0; row < mBoard->mRows; ++row) {
		for (int column = 0; column < mBoard->mColumns; ++column) {
			if (IsCellReserved(row, column)) continue;
			Plant* plant = ResolveBungeePlantAt(row, column);
			if (plant && plant->mPlantType == PlantType::PLANT_SUNFLOWER
				&& sunflowerIDs.insert(plant->mPlantID).second) {
				++untargetedSunflowers;
			}
		}
	}

	std::vector<CellCandidate> candidates;
	std::vector<float> weights;
	std::unordered_set<int> candidatePlantIDs;
	for (int row = 0; row < mBoard->mRows; ++row) {
		for (int column = 0; column < mBoard->mColumns; ++column) {
			if (IsCellReserved(row, column)) continue;
			Plant* plant = ResolveBungeePlantAt(row, column);
			if (plant && !candidatePlantIDs.insert(plant->mPlantID).second) continue;
			if (plant && plant->mPlantType == PlantType::PLANT_SUNFLOWER
				&& untargetedSunflowers <= 1) {
				continue;
			}
			candidates.push_back({ row, column,
				plant ? plant->mPlantID : NULL_PLANT_ID });
			weights.push_back(plant ? kOccupiedCellWeight : kEmptyCellWeight);
		}
	}
	if (candidates.empty()) return false;
	ApplySelectedCell(GameRandom::WeightedChoice(candidates, weights));
	return true;
}

void BungeeZombie::ApplySelectedCell(const CellCandidate& candidate)
{
	mTargetRow = candidate.row;
	mTargetColumn = candidate.column;
	mTargetPlantID = candidate.plantID;
	CommitRow(candidate.row);
	mTargetInitialized = true;
	mAltitude = kInitialAltitudeBase
		+ GameRandom::Range(0.0f, kInitialAltitudeJitter);
	const Vector center = mBoard->GetCellCenterPosition(candidate.row, candidate.column);
	SetPosition(Vector(center.x,
		mBoard->GetZombieSpawnY(candidate.row, center.x)));
}

Plant* BungeeZombie::ResolveBungeePlantAt(int row, int column) const
{
	Plant* normalPlant = mBoard->GetNormalPlantAt(row, column);
	// 玉米加农炮等明确拒绝蹦极选中的普通层会遮住下方承载层；否则蹦极会越过炮身抱走花盆。
	if (normalPlant && normalPlant->IsActive() && !normalPlant->IsSquished()
		&& !normalPlant->CanBeTargetedByBungee()) {
		return nullptr;
	}
	const std::array<Plant*, 3> layers = {
		normalPlant,
		mBoard->GetPumpkinAt(row, column),
		mBoard->GetUnderPlantAt(row, column),
	};
	for (Plant* plant : layers) {
		if (plant && plant->IsActive() && !plant->IsSquished()
			&& !plant->IsBungeeTargeted() && plant->CanBeTargetedByBungee()) {
			return plant;
		}
	}
	return nullptr;
}

bool BungeeZombie::IsCellReserved(int row, int column) const
{
	Plant* candidate = ResolveBungeePlantAt(row, column);
	const int candidatePlantID = candidate ? candidate->mPlantID : NULL_PLANT_ID;
	for (const int zombieID : mBoard->mEntityRegistry.GetAllZombieIDs()) {
		auto* other = dynamic_cast<BungeeZombie*>(
			mBoard->mEntityRegistry.GetZombie(zombieID));
		if (!other || other == this || !other->HasSelectedTarget()) continue;
		if ((other->GetTargetRow() == row && other->GetTargetColumn() == column)
			|| (candidatePlantID != NULL_PLANT_ID
				&& other->GetTargetPlantID() == candidatePlantID)) {
			return true;
		}
	}
	return false;
}

void BungeeZombie::LandAtTarget()
{
	mAltitude = 0.0f;
	if (Plant* protector = mBoard->FindAirborneThreatProtector(
		mTargetRow, mTargetColumn)) {
		if (protector->ActivateAirborneDefense()
			!= AirborneDefenseState::INACTIVE) {
			// 原版落地节点立即空手弹回；必须先清除预订植物，避免 RISING 的 Die() 误删目标。
			AudioSystem::PlaySound(
				ResourceKeys::Sounds::SOUND_BOING, kUmbrellaBounceVolume);
			mTargetPlantID = NULL_PLANT_ID;
			mPhase = Phase::RISING;
			mPhaseTimer = 0.0f;
			if (mCollider) mCollider->mEnabled = false;
			return;
		}
	}
	mPhase = Phase::AT_BOTTOM;
	mPhaseTimer = kBottomWaitSeconds;
	PlayTrack("anim_idle", kDropClipSpeed, 0.15f);
	if (mCollider) mCollider->mEnabled = true;
}

void BungeeZombie::BeginGrab()
{
	mPhase = Phase::GRABBING;
	Plant* plant = ResolveBungeePlantAt(mTargetRow, mTargetColumn);
	mTargetPlantID = plant ? plant->mPlantID : NULL_PLANT_ID;
	if (plant) plant->BeginBungeeGrab(mZombieID);
	PlayTrackOnce("anim_grab", "anim_hold", kGrabClipSpeed,
		0.15f, kGrabClipSpeed, 0.0f);
}

void BungeeZombie::BeginRise()
{
	Plant* plant = mBoard->mEntityRegistry.GetPlant(mTargetPlantID);
	if (plant && !plant->BeginBungeeLift(mZombieID)) {
		mTargetPlantID = NULL_PLANT_ID;
		plant = nullptr;
	}
	mPhase = Phase::RISING;
	if (mCollider) mCollider->mEnabled = false;
	PlayTrackOnce("anim_raise", "", kRaiseClipSpeed, 0.0f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FLOOP,
		kBungeeSoundVolume);
	UpdateCargoOffset();
}

void BungeeZombie::UpdateCargoOffset()
{
	Plant* plant = mBoard->mEntityRegistry.GetPlant(mTargetPlantID);
	if (!plant) return;
	plant->SetBungeeVisualOffset(mZombieID,
		Vector(kCargoOffsetX, kCargoOffsetY - mAltitude));
}

void BungeeZombie::TakeDamage(int damage, DamageSource source,
	bool penetrateShield, bool discardShieldOverflow, bool bypassShield,
	PlantDamageOrigin plantOrigin)
{
	if (!IsVulnerable()) return;
	Zombie::TakeDamage(damage, source, penetrateShield,
		discardShieldOverflow, bypassShield, plantOrigin);
}

void BungeeZombie::TakePlantAshDamage(int damage)
{
	if (IsVulnerable()) Zombie::TakePlantAshDamage(damage);
}

bool BungeeZombie::TakePlantInstantKill()
{
	if (!IsVulnerable()) return false;
	Die();
	return true;
}

void BungeeZombie::TakeBodyDamage(int damage)
{
	mBodyHealth = std::max(0, mBodyHealth - std::max(0, damage));
	if (mBodyHealth <= 0) Die();
}

bool BungeeZombie::CanBeTargetedByProjectile(bool targetsFlying) const
{
	return !targetsFlying && IsVulnerable();
}

bool BungeeZombie::CanBeChilled() const
{
	return mPhase == Phase::AT_BOTTOM && Zombie::CanBeChilled();
}

void BungeeZombie::Die()
{
	if (mBoard && mTargetPlantID != NULL_PLANT_ID) {
		if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mTargetPlantID)) {
			if (mPhase == Phase::RISING && !IsRetiringForTemporalReplacement()) {
				plant->KillByZombie();
			}
			else plant->CancelBungeeGrab(mZombieID);
		}
	}
	mTargetPlantID = NULL_PLANT_ID;
	Zombie::Die();
}

Vector BungeeZombie::GetVisualPosition() const
{
	return Zombie::GetVisualPosition() + Vector(0.0f, -mAltitude);
}

void BungeeZombie::ConfigureFrontArmLayers()
{
	if (!mAnimator) return;
	const std::shared_ptr<Reanimation> reanimation = mAnimator->GetReanimation();
	if (!reanimation) return;
	mFrontArmAnimator = std::make_shared<Animator>(reanimation);
	for (std::size_t i = 0; i < reanimation->GetTrackCount(); ++i) {
		if (TrackInfo* track = reanimation->GetTrack(static_cast<int>(i))) {
			mFrontArmAnimator->SetTrackVisible(track->mTrackName, false);
		}
	}
	for (const char* track : kFrontArmTracks) {
		mAnimator->SetTrackVisible(track, false);
		mFrontArmAnimator->SetTrackVisible(track, true);
	}
}

void BungeeZombie::DrawCordAndTarget(Graphics* g) const
{
	if (!g || mIsPreview || !mTargetInitialized) return;
	const ResourceManager& resources = ResourceManager::GetInstance();
	if (const Texture* target = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_BUNGEETARGET, false);
		target && mPhase == Phase::DIVING) {
		const Vector center = mBoard->GetCellCenterPosition(
			mTargetRow, mTargetColumn);
		g->DrawTexture(target,
			center.x - static_cast<float>(target->width) * 0.5f,
			center.y - static_cast<float>(target->height) * 0.5f,
			static_cast<float>(target->width),
			static_cast<float>(target->height));
	}
	if (const Texture* cord = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_BUNGEECORD, false)) {
		const Vector visual = GetVisualPosition();
		const float cordX = visual.x + kCordOffsetX;
		const float endY = visual.y + kCordEndOffsetY;
		const float height = static_cast<float>(cord->height);
		for (float y = -height; y < endY; y += height) {
			g->DrawTexture(cord, cordX, y,
				static_cast<float>(cord->width), height);
		}
	}
}

void BungeeZombie::Draw(Graphics* g)
{
	DrawCordAndTarget(g);
	Zombie::Draw(g);
	if (mBoard && mPhase == Phase::RISING) {
		if (Plant* plant = mBoard->mEntityRegistry.GetPlant(mTargetPlantID)) {
			plant->DrawAsBungeeCargo(g);
		}
	}
	if (g && mFrontArmAnimator) {
		mFrontArmAnimator->SetCurrentFrame(GetCurrentFrame());
		mFrontArmAnimator->SetAlpha(GetAlpha());
		const Vector visual = GetVisualPosition();
		const float scale = GetTransform()
			? GetTransform()->GetScale() : 1.0f;
		mFrontArmAnimator->Draw(g, visual.x, visual.y, scale);
	}
}

void BungeeZombie::SaveExtraData(nlohmann::json& j) const
{
	j["phase"] = static_cast<int>(mPhase);
	j["targetMode"] = static_cast<int>(mTargetMode);
	j["altitude"] = mAltitude;
	j["phaseTimer"] = mPhaseTimer;
	j["targetRow"] = mTargetRow;
	j["targetColumn"] = mTargetColumn;
	j["targetPlantID"] = mTargetPlantID;
	j["targetInitialized"] = mTargetInitialized;
	j["screamPlayed"] = mScreamPlayed;
	j["mcRollouts"] = mMonteCarloStats.rolloutCount;
	j["mcCandidates"] = mMonteCarloStats.candidateCount;
	j["mcSampledZombies"] = mMonteCarloStats.sampledZombieCount;
	j["mcSampledPlants"] = mMonteCarloStats.sampledPlantCount;
	j["mcSupportPlants"] = mMonteCarloStats.supportPlantCount;
	j["mcCards"] = mMonteCarloStats.cardCount;
	j["mcBestScore"] = mMonteCarloStats.bestScore;
	j["mcCoordinationLoss"] = mMonteCarloStats.coordinationLoss;
}

void BungeeZombie::LoadExtraData(const nlohmann::json& j)
{
	mPhase = static_cast<Phase>(std::clamp(
		j.value("phase", static_cast<int>(Phase::DIVING)),
		static_cast<int>(Phase::DIVING), static_cast<int>(Phase::RISING)));
	mTargetMode = static_cast<TargetMode>(std::clamp(
		j.value("targetMode", static_cast<int>(TargetMode::RANDOM)),
		static_cast<int>(TargetMode::RANDOM), static_cast<int>(TargetMode::MONTE_CARLO)));
	mAltitude = std::max(0.0f, j.value("altitude", kInitialAltitudeBase));
	mPhaseTimer = std::max(0.0f, j.value("phaseTimer", 0.0f));
	mTargetRow = j.value("targetRow", -1);
	mTargetColumn = j.value("targetColumn", -1);
	mTargetPlantID = j.value("targetPlantID", NULL_PLANT_ID);
	mTargetInitialized = j.value("targetInitialized", false);
	mScreamPlayed = j.value("screamPlayed", false);
	mMonteCarloStats.rolloutCount = j.value("mcRollouts", 0);
	mMonteCarloStats.candidateCount = j.value("mcCandidates", 0);
	mMonteCarloStats.sampledZombieCount = j.value("mcSampledZombies", 0);
	mMonteCarloStats.sampledPlantCount = j.value("mcSampledPlants", 0);
	mMonteCarloStats.supportPlantCount = j.value("mcSupportPlants", 0);
	mMonteCarloStats.cardCount = j.value("mcCards", 0);
	mMonteCarloStats.bestScore = j.value("mcBestScore", 0.0f);
	mMonteCarloStats.coordinationLoss = j.value("mcCoordinationLoss", 0.0f);
	if (mCollider) mCollider->mEnabled = IsVulnerable();

	Plant* plant = mBoard ? mBoard->mEntityRegistry.GetPlant(mTargetPlantID) : nullptr;
	if (plant && (mPhase == Phase::GRABBING || mPhase == Phase::RISING)) {
		if (plant->BeginBungeeGrab(mZombieID) && mPhase == Phase::RISING) {
			plant->BeginBungeeLift(mZombieID);
			UpdateCargoOffset();
		}
	}
}
