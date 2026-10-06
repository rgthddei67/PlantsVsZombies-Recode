#include "BulletPool.h"

#include "Game/Board/Board.h"
#include "../GameObjectManager.h"
#include "../../Logger.h"

#include <algorithm>

namespace {
	// 只接受已有完整表现和碰撞合同的弹型；僵尸豌豆仍是尚未接入工厂的预留枚举。
	bool IsSupportedPooledBulletType(BulletType type)
	{
		switch (type) {
		case BulletType::BULLET_PEA:
		case BulletType::BULLET_SNOWPEA:
		case BulletType::BULLET_CABBAGE:
		case BulletType::BULLET_MELON:
		case BulletType::BULLET_PUFF:
		case BulletType::BULLET_WINTERMELON:
		case BulletType::BULLET_FIREBALL:
		case BulletType::BULLET_STAR:
		case BulletType::BULLET_SPIKE:
		case BulletType::BULLET_BASKETBALL:
		case BulletType::BULLET_KERNEL:
		case BulletType::BULLET_COBBIG:
		case BulletType::BULLET_BUTTER:
		case BulletType::BULLET_TOXICPEA:
		case BulletType::BULLET_TOXICFIREBALL:
		case BulletType::BULLET_MELT_SNOW:
		case BulletType::BULLET_SALT_CRYSTAL:
		case BulletType::BULLET_AURORA_PEA:
		case BulletType::BULLET_THUNDER_SEED:
		case BulletType::BULLET_THERMAL_PULSE:
		case BulletType::BULLET_RAIN_BAMBOO:
        case BulletType::BULLET_FLOOD_MORTAR:
        case BulletType::BULLET_PRESSURE:
			return true;
		case BulletType::BULLET_ZOMBIE_PEA:
		case BulletType::NUM_BULLETS:
			return false;
		}
		return false;
	}
}

void BulletPool::Initialize(int initialCapacity, int warningThreshold) {
	mInitialCapacity = initialCapacity;
	mWarningThreshold = warningThreshold;
	mPool.clear();
	mPool.reserve(initialCapacity);
	mFreeByType.assign(static_cast<int>(BulletType::NUM_BULLETS), {});
	mActiveIndices.clear();
	mActiveIndices.reserve(initialCapacity);
	mPeakCount = 0;
	mHitCount = 0;
	mMissCount = 0;
}

void BulletPool::ActivateSlot(int poolIndex) {
	auto& pooled = mPool[poolIndex];
	pooled.active = true;
	pooled.activeListIndex = static_cast<int>(mActiveIndices.size());
	mActiveIndices.push_back(poolIndex);
	mPeakCount = std::max(mPeakCount, GetActiveCount());
}

std::shared_ptr<Bullet> BulletPool::AcquireShared(Board* board, BulletType type, int row,
	const Vector& colliderRadius, const Vector& position) {
	int typeIdx = static_cast<int>(type);
	if (typeIdx < 0 || typeIdx >= static_cast<int>(mFreeByType.size())
		|| !IsSupportedPooledBulletType(type)) {
		LOG_ERROR("BulletPool") << "Acquire 不支持的子弹类型: " << typeIdx;
		return nullptr;
	}

	// 1. 从对应类型的空闲列表直接取槽位，O(1)
	auto& freeList = mFreeByType[typeIdx];
	while (!freeList.empty()) {
		int idx = freeList.back();
		freeList.pop_back();
		auto& pooled = mPool[idx];
		auto bullet = pooled.bullet;
		if (bullet) {
			if (bullet->mBulletID != NULL_BULLET_ID) {
				board->mEntityRegistry.RemoveBullet(bullet->mBulletID);
			}
			bullet->SetActive(true);
			bullet->Reset(board, row, colliderRadius, position);
			ActivateSlot(idx);
			mHitCount++;
			return bullet;
		}
		// 空槽位理论上只可能来自损坏状态；跳过后允许本次 Acquire 自愈新建。
	}

	// 2. 没有空闲对象，创建新对象并添加到池中（自动扩容）
	PooledBullet pooled;
	pooled.type = type;
	mMissCount++;

	// 所有弹型共用同一具体存储类型；差异由 BulletType 与 Reset 合同完整表达。
	std::shared_ptr<Bullet> bullet =
		GameObjectManager::GetInstance().CreateGameObjectImmediateAsShared<Bullet>(
			LAYER_GAME_BULLET,
			board, type, row, colliderRadius, position);

	if (bullet) {
		bullet->SetFromPool(true);
		int idx = static_cast<int>(mPool.size());
		bullet->mPoolSlotIndex = idx;
		pooled.bullet = bullet;
		mPool.push_back(std::move(pooled));
		ActivateSlot(idx);

		// 警告：池大小超过阈值
		if (static_cast<int>(mPool.size()) > mWarningThreshold) {
			LOG_WARN("BulletPool") << "池大小 " << mPool.size()
				<< " 超过阈值 " << mWarningThreshold;
		}

		return bullet;
	}

	return nullptr;
}

Bullet* BulletPool::Acquire(Board* board, BulletType type, int row,
	const Vector& colliderRadius, const Vector& position) {
	return AcquireShared(board, type, row, colliderRadius, position).get();
}

void BulletPool::Release(Bullet* bullet) {
	if (!bullet) return;

	// 槽位随 Bullet 生命周期固定，避免为每颗弹丸维护独立哈希节点与桶数组。
	const int idx = bullet->mPoolSlotIndex;
	if (idx < 0 || idx >= static_cast<int>(mPool.size())
		|| mPool[idx].bullet.get() != bullet) {
		LOG_WARN("BulletPool") << "Release 找不到对应的池对象";
		return;
	}

	auto& pooled = mPool[idx];
	if (!pooled.active) {
		LOG_WARN("BulletPool") << "忽略重复回收的池对象，槽位: " << idx;
		return;
	}

	const int activeListIndex = pooled.activeListIndex;
	if (activeListIndex < 0
		|| activeListIndex >= static_cast<int>(mActiveIndices.size())
		|| mActiveIndices[activeListIndex] != idx) {
		LOG_ERROR("BulletPool") << "活跃槽位索引损坏，拒绝回收槽位: " << idx;
		return;
	}

	// 与末项交换后弹出，避免回收时移动其余活跃槽位。
	const int movedPoolIndex = mActiveIndices.back();
	mActiveIndices[activeListIndex] = movedPoolIndex;
	mPool[movedPoolIndex].activeListIndex = activeListIndex;
	mActiveIndices.pop_back();
	pooled.active = false;
	pooled.activeListIndex = -1;
	bullet->SetActive(false);
	mFreeByType[static_cast<int>(pooled.type)].push_back(idx);
}

void BulletPool::Clear() {
	// 销毁所有池中的对象
	for (auto& pooled : mPool) {
		if (pooled.bullet) {
			pooled.bullet->mPoolSlotIndex = -1;
			GameObjectManager::GetInstance().DestroyGameObject(pooled.bullet);
		}
	}

	mPool.clear();
	for (auto& fl : mFreeByType) fl.clear();
	mActiveIndices.clear();
	mPeakCount = 0;
	mHitCount = 0;
	mMissCount = 0;
}

void BulletPool::DrawShadows(Graphics* g) const {
	for (int poolIndex : mActiveIndices) {
		const auto& pooled = mPool[poolIndex];
		pooled.bullet->DrawShadow(g);
	}
}

bool BulletPool::HasConsistentActiveSlotsForTesting() const {
	if (mActiveIndices.size() > mPool.size()) return false;

	std::vector<bool> seen(mPool.size(), false);
	for (int activeListIndex = 0;
		activeListIndex < static_cast<int>(mActiveIndices.size()); ++activeListIndex) {
		const int poolIndex = mActiveIndices[activeListIndex];
		if (poolIndex < 0 || poolIndex >= static_cast<int>(mPool.size())) return false;
		const auto& pooled = mPool[poolIndex];
		if (seen[poolIndex] || !pooled.active
			|| pooled.activeListIndex != activeListIndex
			|| !pooled.bullet
			|| pooled.bullet->mPoolSlotIndex != poolIndex) {
			return false;
		}
		seen[poolIndex] = true;
	}

	for (int poolIndex = 0; poolIndex < static_cast<int>(mPool.size()); ++poolIndex) {
		const auto& pooled = mPool[poolIndex];
		if (!pooled.bullet || pooled.bullet->mPoolSlotIndex != poolIndex) return false;
		if (pooled.active != seen[poolIndex]) return false;
		if (!pooled.active && pooled.activeListIndex != -1) return false;
	}
	return true;
}

void BulletPool::PrintStats() const {
	LOG_DEBUG("BulletPool") << "=== BulletPool 统计信息 ===";
	LOG_DEBUG("BulletPool") << "池大小: " << mPool.size() << " (初始容量: " << mInitialCapacity
		<< ", 警告阈值: " << mWarningThreshold << ")";
	LOG_DEBUG("BulletPool") << "活跃对象: " << GetActiveCount();
	LOG_DEBUG("BulletPool") << "峰值对象: " << mPeakCount;
	LOG_DEBUG("BulletPool") << "命中次数: " << mHitCount;
	LOG_DEBUG("BulletPool") << "未命中次数: " << mMissCount;
	LOG_DEBUG("BulletPool") << "命中率: " << (GetHitRate() * 100.0f) << "%";
}
