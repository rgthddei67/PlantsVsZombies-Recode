#include "EliteJackInTheBoxZombie.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "../Plant/GameDataManager.h"
#include "../Plant/Plant.h"
#include "../../DeltaTime.h"
#include "../../GameApp.h"
#include "../../GameRandom.h"
#include "../../Graphics.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include "../../ResourceKeys.h"
#include "../../ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
	constexpr int kEliteBodyHealth = 900;              // 精英小丑本体基础生命
	constexpr int kEliteBiteDamage = 65;               // 每次啃咬的基础伤害
	constexpr float kEliteRunVelocity = 0.61f;         // C# 小丑速度口径；略慢于普通小丑
	constexpr float kThrowIntervalMin = 5.0f;          // 两次投盒之间的最短游戏秒
	constexpr float kThrowIntervalMax = 7.0f;          // 两次投盒之间的最长游戏秒
	constexpr float kBoxFlightDuration = 0.75f;        // 盒子从手中飞到落点的游戏秒
	constexpr float kBoxArcHeight = 120.0f;            // 抛物线中点相对直线抬升的像素
	constexpr float kBoxDrawScale = 0.72f;             // 飞行盒相对原贴图的战场绘制倍率
	constexpr float kBoxSpinDegrees = 540.0f;          // 单次飞行累计旋转角度
	constexpr int kBoxExplosionDamage = 50;            // 落地爆炸的基础植物/僵尸伤害
	constexpr float kBoxExplosionRadius = 100.0f;       // 落地爆炸半径，单位 px
	constexpr float kExplosionVolume = 0.42f;          // 小型盒子爆炸的一次性音量
	constexpr float kBacklinePlantSunMultiplier = 1.2f; // 靠房屋侧半场植物计入贪心损失分数的倍率
	constexpr float kSunProducerFutureValue = 300.0f;  // 每株产阳光植物计入的预期后续经济损失，单位：阳光分
	constexpr float kNoTargetRetryDelay = 0.5f;         // 倒计时到点却没有合法目标时再次搜索的游戏秒
	constexpr float kTargetScoreTieEpsilon = 0.001f;    // 浮点损失分数判定并列时的容差

	/** 当前会持续提供战斗内阳光经济的植物类型。 */
	bool IsSunProducer(PlantType plantType)
	{
		switch (plantType) {
		case PlantType::PLANT_SUNFLOWER:
		case PlantType::PLANT_SUNSHROOM:
		case PlantType::PLANT_TWINSUNFLOWER:
			return true;
		default:
			return false;
		}
	}

	// 以碰撞框最近点判断范围，避免只按对象逻辑原点漏掉爆区边缘目标。
	bool CircleOverlapsRect(const Vector& center, float radius, const SDL_FRect& bounds)
	{
		const float nearestX = std::clamp(center.x, bounds.x, bounds.x + bounds.w);
		const float nearestY = std::clamp(center.y, bounds.y, bounds.y + bounds.h);
		const float dx = center.x - nearestX;
		const float dy = center.y - nearestY;
		return dx * dx + dy * dy <= radius * radius;
	}
}

/**
 * @brief 复用普通小丑的时间线、断肢和声音，只替换精英数值与持续投盒能力。
 */
void EliteJackInTheBoxZombie::SetupZombie()
{
	JackInTheBoxZombie::SetupZombie();
	mBodyHealth = kEliteBodyHealth;
	mBodyMaxHealth = kEliteBodyHealth;
	mAttackDamage = kEliteBiteDamage;
	SetRunVelocityForVariant(kEliteRunVelocity);

	if (mIsPreview) return;
	mThrowCountdown = RollThrowInterval();
	mBoxInFlight = false;
	SetHeldBoxVisible(true);
}

/** 精英小丑只复用啃食与死亡帧，绝不注册经典小丑的第 66 帧自爆。 */
void EliteJackInTheBoxZombie::RegisterFrameEvents()
{
	RegisterSharedFrameEvents();
}

/** 推进基类僵尸生命周期，并在能力门禁外继续已经离手的盒子。 */
void EliteJackInTheBoxZombie::Update()
{
	// 绕过 JackInTheBoxZombie::Update，避免推进经典小丑的开盒倒计时。
	Zombie::Update();
	if (mIsPreview || mIsDead || !mBoard) return;

	const float deltaTime = DeltaTime::GetDeltaTime();
	// 盒子一旦离手便独立飞行；投掷者冻结、掉头或进入死亡动画都不冻结已发出的攻击。
	if (mBoxInFlight) {
		UpdateThrownBox(deltaTime);
		return;
	}
	if (GetPhase() != Phase::RUNNING) return;
	if (mIsDying || !mHasHead || IsImmobilized()) return;

	mThrowCountdown = std::max(0.0f, mThrowCountdown - deltaTime);
	if (mThrowCountdown <= 0.0f) {
		BeginThrow();
	}
}

/** 在僵尸本体之后绘制单个飞行盒子的旋转抛物线位置。 */
void EliteJackInTheBoxZombie::Draw(Graphics* g)
{
	JackInTheBoxZombie::Draw(g);
	if (!g || !mBoxInFlight) return;

	const Texture* texture = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ELITEJACKBOX_BOX, false);
	if (!texture) return;

	const Vector position = GetThrownBoxPosition();
	const float scale = (GetTransform()
		? GetTransform()->GetScale() : 1.0f) * kBoxDrawScale;
	const float width = static_cast<float>(texture->width) * scale;
	const float height = static_cast<float>(texture->height) * scale;
	const float rotation = GetBoxFlightProgress() * kBoxSpinDegrees;
	g->DrawTexture(texture,
		position.x - width * 0.5f,
		position.y - height * 0.5f,
		width, height, rotation);
}

float EliteJackInTheBoxZombie::RollThrowInterval() const
{
	return GameRandom::Range(kThrowIntervalMin, kThrowIntervalMax);
}

/**
 * @brief 锁定一次投掷的起点、阵营和地图合法落点，并隐藏手中盒子轨道。
 */
bool EliteJackInTheBoxZombie::BeginThrow()
{
	if (GetPhase() != Phase::RUNNING || mBoxInFlight || !mBoard
		|| !mHasHead || mIsDying || mIsDead) {
		return false;
	}
	Vector targetPosition;
	int targetRow = -1;
	if (!PickThrowTarget(targetRow, targetPosition)) {
		// 没有敌对目标时保留盒子，短暂等待后再搜索，避免逐帧扫描空场。
		mThrowCountdown = kNoTargetRetryDelay;
		return false;
	}

	mBoxStartPosition = GetHeldBoxWorldPosition();
	mBoxTargetPosition = targetPosition;
	mThrowTargetRow = targetRow;
	mBoxFlightElapsed = 0.0f;
	mBoxInFlight = true;
	mThrowWasMindControlled = mIsMindControlled;
	mThrowCountdown = 0.0f;
	SetHeldBoxVisible(false);
	mForcedTargetRow = -1;
	mForcedTargetColumn = -1;
	return true;
}

/** 推进单盒飞行计时，并在抵达时只结算一次。 */
void EliteJackInTheBoxZombie::UpdateThrownBox(float deltaTime)
{
	mBoxFlightElapsed = std::min(
		kBoxFlightDuration, mBoxFlightElapsed + std::max(0.0f, deltaTime));
	if (mBoxFlightElapsed >= kBoxFlightDuration) {
		ResolveThrownBox();
	}
}

/**
 * @brief 在锁定落点结算一次爆炸，再恢复手中盒子并开始下一轮随机倒计时。
 */
void EliteJackInTheBoxZombie::ResolveThrownBox()
{
	if (!mBoxInFlight) return;
	mBoxInFlight = false;

	AudioSystem::PlaySound(
		ResourceKeys::Sounds::SOUND_EXPLOSION, kExplosionVolume);
	if (g_particleSystem) {
		g_particleSystem->EmitEffect("JackExplode", mBoxTargetPosition);
	}
	if (mThrowWasMindControlled) {
		DamageEnemyZombiesAtImpact();
	}
	else {
		DamagePlantsAtImpact();
	}

	if (GetPhase() == Phase::RUNNING) {
		mThrowCountdown = RollThrowInterval();
		SetHeldBoxVisible(true);
	}
}

/** 对爆区内的活动植物走统一僵尸来源伤害入口。 */
void EliteJackInTheBoxZombie::DamagePlantsAtImpact() const
{
	if (!mBoard) return;
	mBoard->ApplyPumpkinProtectedZombieAreaDamage(kBoxExplosionDamage,
		[this](const Plant& plant) {
			const ColliderComponent* collider = plant.GetColliderComponent();
			return collider && !mBoard->MineBlocksSegment(mBoxTargetPosition,plant.GetPosition())
				&& CircleOverlapsRect(mBoxTargetPosition,
				kBoxExplosionRadius, collider->GetBoundingBox());
		});
}

/** 对爆区内与投出阵营敌对的活动僵尸造成伤害。 */
void EliteJackInTheBoxZombie::DamageEnemyZombiesAtImpact() const
{
	if (!mBoard) return;
	const std::vector<int> zombieIDs = mBoard->mEntityRegistry.GetAllZombieIDs();
	for (const int zombieID : zombieIDs) {
		Zombie* zombie = mBoard->mEntityRegistry.GetZombie(zombieID);
		if (!zombie || zombie == this || !zombie->IsActive() || zombie->IsDying()) {
			continue;
		}
		// 魅惑盒只伤敌对普通僵尸；同阵营魅惑僵尸与植物保持安全。
		if (zombie->IsMindControlled() == mThrowWasMindControlled) continue;
		const ColliderComponent* collider = zombie->GetColliderComponent();
		if (collider && !mBoard->MineBlocksSegment(mBoxTargetPosition,zombie->GetPosition()) && CircleOverlapsRect(
			mBoxTargetPosition, kBoxExplosionRadius, collider->GetBoundingBox())) {
			zombie->TakeDamage(kBoxExplosionDamage, DamageSource::ZOMBIE);
		}
	}
}

Vector EliteJackInTheBoxZombie::GetHeldBoxWorldPosition() const
{
	const float scale = GetTransform()
		? GetTransform()->GetScale() : 1.0f;
	const Vector local = mAnimator
		? mAnimator->GetTrackPosition("Zombie_jackbox_box") : Vector::zero();
	return GetVisualPosition() + local * scale;
}

/**
 * @brief 按当前阵营选择落点：普通状态贪心攻击植物，魅惑状态随机攻击敌方僵尸。
 */
bool EliteJackInTheBoxZombie::PickThrowTarget(
	int& targetRow, Vector& targetPosition)
{
	if (!mBoard) {
		return false;
	}
	mLastPlantTargetingMode = PlantTargetingMode::NONE;
	mLastMonteCarloRolloutCount = 0;
	mLastMonteCarloCandidateCount = 0;
	mLastMonteCarloZombieCount = 0;
	mLastMonteCarloCardCount = 0;
	mLastMonteCarloCoordinationLoss = 0.0f;

	const int minRow = std::max(0, mRow - 1);
	const int maxRow = std::min(mBoard->mRows - 1, mRow + 1);
	if (mForcedTargetRow >= minRow && mForcedTargetRow <= maxRow
		&& mForcedTargetColumn >= 0
		&& mForcedTargetColumn < mBoard->mColumns) {
		targetRow = mForcedTargetRow;
		targetPosition =
			mBoard->GetCellCenterPosition(targetRow, mForcedTargetColumn);
		mLastPlantTargetingMode = PlantTargetingMode::FORCED;
		return true;
	}

	if (mIsMindControlled) {
		const bool found =
			PickRandomEnemyZombieTarget(targetRow, targetPosition);
		if (found) {
			mLastPlantTargetingMode = PlantTargetingMode::CHARMED_RANDOM;
		}
		return found;
	}
	if (GameAPP::GetInstance().mEnableMonteCarloAI
		&& PickMonteCarloPlantTarget(targetRow, targetPosition)) {
		mLastPlantTargetingMode = PlantTargetingMode::MONTE_CARLO;
		return true;
	}
	const bool found = PickGreedyPlantTarget(targetRow, targetPosition);
	if (found) mLastPlantTargetingMode = PlantTargetingMode::GREEDY;
	return found;
}

/** 调用 Board 级共享推演器；失败时由调用者回退到原贪心算法。 */
bool EliteJackInTheBoxZombie::PickMonteCarloPlantTarget(
	int& targetRow, Vector& targetPosition)
{
	if (!mBoard) return false;
	MonteCarloTargetStats stats;
	const bool found = mBoard->PickMonteCarloPlantBlastTarget(
		std::max(0, mRow - 1),
		std::min(mBoard->mRows - 1, mRow + 1),
		kBoxExplosionDamage,
		kBoxExplosionRadius,
		mZombieID,
		targetRow,
		targetPosition,
		&stats);
	mLastMonteCarloRolloutCount = stats.rolloutCount;
	mLastMonteCarloCandidateCount = stats.candidateCount;
	mLastMonteCarloZombieCount = stats.sampledZombieCount;
	mLastMonteCarloCardCount = stats.cardCount;
	mLastMonteCarloCoordinationLoss = stats.coordinationLoss;
	return found;
}

/**
 * @brief 枚举可投行内的占用格，选择实际爆炸覆盖植物阳光损失分数最高的格子。
 */
bool EliteJackInTheBoxZombie::PickGreedyPlantTarget(
	int& targetRow, Vector& targetPosition) const
{
	if (!mBoard) return false;
	const int minRow = std::max(0, mRow - 1);
	const int maxRow = std::min(mBoard->mRows - 1, mRow + 1);

	std::vector<std::pair<int, int>> candidateCells;
	const std::vector<int> plantIDs = mBoard->mEntityRegistry.GetAllPlantIDs();
	for (const int plantID : plantIDs) {
		const Plant* plant = mBoard->mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->IsActive() || plant->IsSquished()
			|| plant->mRow < minRow || plant->mRow > maxRow
			|| plant->mColumn < 0 || plant->mColumn >= mBoard->mColumns) {
			continue;
		}
		const std::pair<int, int> cell(plant->mRow, plant->mColumn);
		if (std::find(candidateCells.begin(), candidateCells.end(), cell)
			== candidateCells.end()) {
			candidateCells.push_back(cell);
		}
	}
	if (candidateCells.empty()) return false;

	float bestScore = -1.0f;
	std::vector<std::pair<int, int>> bestCells;
	for (const auto& cell : candidateCells) {
		const Vector position =
			mBoard->GetCellCenterPosition(cell.first, cell.second);
		const float score = ScorePlantBlastAt(position);
		if (score > bestScore + kTargetScoreTieEpsilon) {
			bestScore = score;
			bestCells.assign(1, cell);
		}
		else if (std::fabs(score - bestScore) <= kTargetScoreTieEpsilon) {
			bestCells.push_back(cell);
		}
	}

	const auto& chosen = bestCells[GameRandom::Range(
		0, static_cast<int>(bestCells.size()) - 1)];
	targetRow = chosen.first;
	targetPosition = mBoard->GetCellCenterPosition(chosen.first, chosen.second);
	return true;
}

/** 按正式九宫格南瓜拦截后的承伤集合计算候选爆点损失，后排价值再乘 1.2。 */
float EliteJackInTheBoxZombie::ScorePlantBlastAt(
	const Vector& targetPosition) const
{
	if (!mBoard) return 0.0f;
	float score = 0.0f;
	const int backlineColumnCount = (mBoard->mColumns + 1) / 2;
	const auto& gameData = GameDataManager::GetInstance();
	std::vector<int> scoredPumpkinIDs;
	const std::vector<int> plantIDs = mBoard->mEntityRegistry.GetAllPlantIDs();
	for (const int plantID : plantIDs) {
		const Plant* plant = mBoard->mEntityRegistry.GetPlant(plantID);
		if (!plant || !plant->IsActive() || plant->IsSquished()) continue;
		const ColliderComponent* collider = plant->GetColliderComponent();
		if (!collider || mBoard->MineBlocksSegment(targetPosition,plant->GetPosition()) || !CircleOverlapsRect(
			targetPosition, kBoxExplosionRadius, collider->GetBoundingBox())) {
			continue;
		}

		const Plant* scoredPlant = plant;
		if (const Plant* pumpkin = mBoard->FindPumpkinAreaProtector(*plant)) {
			if (std::find(scoredPumpkinIDs.begin(), scoredPumpkinIDs.end(),
				pumpkin->mPlantID) != scoredPumpkinIDs.end()) {
				continue;
			}
			scoredPumpkinIDs.push_back(pumpkin->mPlantID);
			scoredPlant = pumpkin;
		}
		const float positionMultiplier = scoredPlant->mColumn < backlineColumnCount
			? kBacklinePlantSunMultiplier : 1.0f;
		float plantValue = static_cast<float>(
			gameData.GetPlantSunCost(scoredPlant->mPlantType));
		if (IsSunProducer(scoredPlant->mPlantType)) {
			plantValue += kSunProducerFutureValue;
		}
		score += plantValue * positionMultiplier;
	}
	return score;
}

/** 魅惑后不做价值判断，从自身及相邻行的敌方僵尸中等概率选择一个。 */
bool EliteJackInTheBoxZombie::PickRandomEnemyZombieTarget(
	int& targetRow, Vector& targetPosition) const
{
	if (!mBoard) return false;
	const int minRow = std::max(0, mRow - 1);
	const int maxRow = std::min(mBoard->mRows - 1, mRow + 1);
	std::vector<Zombie*> candidates;
	const std::vector<int> zombieIDs =
		mBoard->mEntityRegistry.GetAllZombieIDs();
	for (const int zombieID : zombieIDs) {
		Zombie* zombie = mBoard->mEntityRegistry.GetZombie(zombieID);
		if (!zombie || zombie == this || !zombie->IsActive()
			|| zombie->IsDying()
			|| zombie->IsMindControlled() == mIsMindControlled
			|| zombie->mRow < minRow || zombie->mRow > maxRow) {
			continue;
		}
		candidates.push_back(zombie);
	}
	if (candidates.empty()) return false;

	Zombie* chosen = candidates[GameRandom::Range(
		0, static_cast<int>(candidates.size()) - 1)];
	targetRow = chosen->mRow;
	targetPosition = chosen->GetPosition();
	if (const ColliderComponent* collider = chosen->GetColliderComponent()) {
		const SDL_FRect bounds = collider->GetBoundingBox();
		targetPosition = Vector(
			bounds.x + bounds.w * 0.5f,
			bounds.y + bounds.h * 0.5f);
	}
	return true;
}

float EliteJackInTheBoxZombie::GetBoxFlightProgress() const
{
	if (!mBoxInFlight) return 0.0f;
	return std::clamp(mBoxFlightElapsed / kBoxFlightDuration, 0.0f, 1.0f);
}

Vector EliteJackInTheBoxZombie::GetThrownBoxPosition() const
{
	const float t = GetBoxFlightProgress();
	Vector position = Vector::lerp(mBoxStartPosition, mBoxTargetPosition, t);
	position.y -= 4.0f * kBoxArcHeight * t * (1.0f - t);
	return position;
}

void EliteJackInTheBoxZombie::SetHeldBoxVisible(bool visible) const
{
	if (mAnimator) {
		mAnimator->SetTrackVisible("Zombie_jackbox_box", visible);
	}
}

void EliteJackInTheBoxZombie::SetThrowCountdownForTesting(
	float seconds, int targetRow, int targetColumn)
{
	if (mBoxInFlight) return;
	mThrowCountdown = std::max(0.0f, seconds);
	mForcedTargetRow = targetRow;
	mForcedTargetColumn = targetColumn;
}

const std::string& EliteJackInTheBoxZombie::GetBrokenArmTextureKey() const
{
	return ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ELITEJACKBOX_OUTERARM_LOWER2;
}

bool EliteJackInTheBoxZombie::HasMagneticItem() const
{
	// 精英投盒能力保留完整反制压力，不允许磁力菇永久废除。
	return false;
}

const char* EliteJackInTheBoxZombie::GetArmDropEffectName() const
{
	return "ZombieEliteJackboxArmOff";
}

void EliteJackInTheBoxZombie::ZombieItemUpdate() const
{
	JackInTheBoxZombie::ZombieItemUpdate();
	SetHeldBoxVisible(GetPhase() == Phase::RUNNING && !mBoxInFlight);
}

/** 保存下一投倒计时与已经离手盒子的完整结算状态。 */
void EliteJackInTheBoxZombie::SaveExtraData(nlohmann::json& j) const
{
	JackInTheBoxZombie::SaveExtraData(j);
	j["throwCountdown"] = mThrowCountdown;
	j["boxInFlight"] = mBoxInFlight;
	j["boxFlightElapsed"] = mBoxFlightElapsed;
	j["boxStartX"] = mBoxStartPosition.x;
	j["boxStartY"] = mBoxStartPosition.y;
	j["boxTargetX"] = mBoxTargetPosition.x;
	j["boxTargetY"] = mBoxTargetPosition.y;
	j["throwTargetRow"] = mThrowTargetRow;
	j["throwWasMindControlled"] = mThrowWasMindControlled;
}

/** 恢复投盒状态，并按当前飞行/生命阶段重建轨道和循环声所有权。 */
void EliteJackInTheBoxZombie::LoadExtraData(const nlohmann::json& j)
{
	JackInTheBoxZombie::LoadExtraData(j);
	mThrowCountdown = std::clamp(
		j.value("throwCountdown", mThrowCountdown),
		0.0f, kThrowIntervalMax);
	mBoxInFlight = j.value("boxInFlight", false);
	mBoxFlightElapsed = std::clamp(
		j.value("boxFlightElapsed", 0.0f),
		0.0f, kBoxFlightDuration);
	mBoxStartPosition = Vector(
		j.value("boxStartX", GetHeldBoxWorldPosition().x),
		j.value("boxStartY", GetHeldBoxWorldPosition().y));
	mBoxTargetPosition = Vector(
		j.value("boxTargetX", GetPosition().x),
		j.value("boxTargetY", GetPosition().y));
	mThrowTargetRow = j.value("throwTargetRow", -1);
	mThrowWasMindControlled = j.value(
		"throwWasMindControlled", mIsMindControlled);

	if (mBoxInFlight && (!mBoard || mThrowTargetRow < 0
		|| mThrowTargetRow >= mBoard->mRows)) {
		mBoxInFlight = false;
		mThrowCountdown = RollThrowInterval();
	}
	SetHeldBoxVisible(GetPhase() == Phase::RUNNING && !mBoxInFlight);
	if (GetPhase() == Phase::RUNNING && mHasHead && !mIsDying && !mIsDead) {
		ClaimLoopSound();
	}
}
