#include "EliteDancerZombie.h"
#include "DancerRules.h"
#include "BackupDancerZombie.h"
#include "Game/Board/Board.h"

#include <algorithm>

namespace {
	constexpr int kMaxActiveBackupDancers = 36;              // 同时维持的直属伴舞上限。
	constexpr float kEliteDancerSpeed = 1.25f;              // 精英舞王基础移动倍率。
	constexpr float kBackupSummonInterval = 0.2f;           // 每次补充一只伴舞的游戏时间间隔（秒）。
	constexpr float kSevereTyphoonSpeedMultiplier = 1.45f;   // 超强台风下额外动作与移动速度倍率。
	constexpr float kSuperTyphoonSpeedMultiplier = 1.75f;   // 强台风下额外动作与移动速度倍率。
	constexpr float kSummonSideDistance = 100.0f;           // 伴舞相对舞王的前后横向距离（像素）。
	constexpr float kSummonFrontMinX = 150.0f;              // 低于此横坐标时不在舞王前方生成伴舞（像素）。

	struct FormationOffset {
		int row;
		float x;
	};

	constexpr FormationOffset kFormationOffsets[] = {
		{-1, 0.0f}, {1, 0.0f}, {0, -kSummonSideDistance}, {0, kSummonSideDistance},
		{-1, -kSummonSideDistance}, {-1, kSummonSideDistance},
		{1, -kSummonSideDistance}, {1, kSummonSideDistance},
	};
}

/** 复用普通舞王动画事件与掉落契约，再覆盖精英数值和持续召唤状态。 */
void EliteDancerZombie::SetupZombie()
{
	DancerZombie::SetupZombie();
	mBodyHealth = DancerRules::EliteBodyHealth;
	mBodyMaxHealth = DancerRules::EliteBodyHealth;
	mFollowerIDs.clear();
	mSummonTimer = kBackupSummonInterval;
	mNextFormationSlot = 0;
	mCharmHandled = false;
}

/** 补充一名普通伴舞；冻结时基类不会调用本钩子，减速则自然减慢计时。 */
void EliteDancerZombie::ZombieUpdate(float scaledTime)
{
	if (mIsMindControlled && !mCharmHandled) {
		// 魅惑后放弃旧阵营直属关系；旧伴舞照常存活，新伴舞继承当前阵营。
		mCharmHandled = true;
		mFollowerIDs.clear();
	}

	CleanupFollowers();
	if (static_cast<int>(mFollowerIDs.size()) >= kMaxActiveBackupDancers) {
		mSummonTimer = kBackupSummonInterval;
	}
	else {
		mSummonTimer -= scaledTime;
		while (mSummonTimer <= 0.0f
			&& static_cast<int>(mFollowerIDs.size()) < kMaxActiveBackupDancers) {
			if (!SummonOneBackupDancer()) {
				mSummonTimer = kBackupSummonInterval;
				break;
			}
			mSummonTimer += kBackupSummonInterval;
		}
	}
	UpdateDanceFacing();
}

/** 精英舞王无视植物碰撞，但仍保留敌对阵营僵尸之间的互啃规则。 */
void EliteDancerZombie::StartEat(ColliderComponent* other)
{
	if (!other) return;
	auto* object = other->GetGameObject();
	if (!object || object->GetObjectType() == ObjectType::OBJECT_PLANT) return;
	Zombie::StartEat(other);
}

/** 绕过普通舞王首口后打响指的阶段切换，避免敌对僵尸互啃时停止推进。 */
void EliteDancerZombie::EatTarget()
{
	Zombie::EatTarget();
}

/** 始终调用僵尸基础移动，不进入普通舞王打响指和定身阶段。 */
void EliteDancerZombie::ZombieMove(float scaledDelta, Transform* transform)
{
	Zombie::ZombieMove(scaledDelta, transform);
}

float EliteDancerZombie::GetAbilityAnimSpeedMultiplier() const
{
	if (!mBoard) return kEliteDancerSpeed;

	if (mBoard->GetTyphoonStrength() == TyphoonStrength::SEVERE) 
	{
		return kSevereTyphoonSpeedMultiplier * kEliteDancerSpeed;
	}
	else if (mBoard->GetTyphoonStrength() == TyphoonStrength::SUPER)
	{
		return kSuperTyphoonSpeedMultiplier * kEliteDancerSpeed;
	}
	else {
		return kEliteDancerSpeed;
	}
}

float EliteDancerZombie::GetTyphoonAbilitySpeedMultiplier() const
{
	return GetAbilityAnimSpeedMultiplier();
}

/** 清除已死亡或阵营不再相同的直属伴舞，空位按当前召唤间隔重新补充。 */
void EliteDancerZombie::CleanupFollowers()
{
	if (!mBoard) {
		mFollowerIDs.clear();
		return;
	}
	mFollowerIDs.erase(std::remove_if(mFollowerIDs.begin(), mFollowerIDs.end(),
		[this](int id) {
			Zombie* follower = mBoard->mEntityRegistry.GetZombie(id);
			return !follower || follower->IsMindControlled() != mIsMindControlled;
		}), mFollowerIDs.end());
}

/** 从八方向编队槽循环选择一个有效位置，只生成一只普通伴舞。 */
bool EliteDancerZombie::SummonOneBackupDancer()
{
	if (!mBoard || !mHasHead) return false;
	const float leaderX = GetPosition().x;
	constexpr int slotCount = static_cast<int>(sizeof(kFormationOffsets) / sizeof(kFormationOffsets[0]));
	for (int checked = 0; checked < slotCount; ++checked) {
		const int slot = (mNextFormationSlot + checked) % slotCount;
		const FormationOffset& offset = kFormationOffsets[slot];
		const int row = mRow + offset.row;
		const float x = leaderX + offset.x;
		if (row < 0 || row >= mBoard->mRows) continue;
		if (offset.x < 0.0f && leaderX < kSummonFrontMinX) continue;

		Zombie* summoned = mBoard->CreateZombie(
			ZombieType::ZOMBIE_BACKUP_DANCER, row, x);
		if (!summoned) continue;
		mNextFormationSlot = (slot + 1) % slotCount;
		mFollowerIDs.push_back(summoned->mZombieID);
		if (auto* backup = dynamic_cast<BackupDancerZombie*>(summoned)) {
			backup->mLeaderID = mZombieID;
		}
		if (mIsMindControlled) summoned->StartMindControlled();
		return true;
	}
	return false;
}

int EliteDancerZombie::GetActiveBackupCount() const
{
	if (!mBoard) return 0;
	return static_cast<int>(std::count_if(mFollowerIDs.begin(), mFollowerIDs.end(),
		[this](int id) {
			Zombie* follower = mBoard->mEntityRegistry.GetZombie(id);
			return follower && follower->IsMindControlled() == mIsMindControlled;
		}));
}

/** 保存持续召唤的剩余时间、编队游标和直属伴舞交叉引用。 */
void EliteDancerZombie::SaveExtraData(nlohmann::json& j) const
{
	DancerZombie::SaveExtraData(j);
	j["eliteFollowers"] = mFollowerIDs;
	j["eliteSummonTimer"] = mSummonTimer;
	j["eliteNextFormationSlot"] = mNextFormationSlot;
	j["eliteCharmHandled"] = mCharmHandled;
}

/** 恢复精英私有状态；失效的伴舞 ID 会在下一次更新时安全清理。 */
void EliteDancerZombie::LoadExtraData(const nlohmann::json& j)
{
	DancerZombie::LoadExtraData(j);
	mFollowerIDs = j.value("eliteFollowers", std::vector<int>{});
	mSummonTimer = std::max(0.0f, j.value("eliteSummonTimer", kBackupSummonInterval));
	mNextFormationSlot = std::clamp(j.value("eliteNextFormationSlot", 0), 0, 7);
	mCharmHandled = j.value("eliteCharmHandled", mIsMindControlled);
	CleanupFollowers();
	if (static_cast<int>(mFollowerIDs.size()) > kMaxActiveBackupDancers) {
		mFollowerIDs.resize(kMaxActiveBackupDancers);
	}
}

ZombieMovementRules::BirthProfile EliteDancerZombie::GetBirthMovementProfile()
{
	auto p=DancerZombie::GetBirthMovementProfile();
	p.abilityMinimum=p.abilityMaximum=kEliteDancerSpeed;
	p.slowAnimationFactor=ZombieMovementRules::FastSlowAnimationFactor;
	return p;
}
