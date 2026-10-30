#include "EntityRegistry.h"
#include "Plant/Plant.h"
#include "Zombie/Zombie.h"
#include "Zombie/GildedZamboniZombie.h"
#include "Zombie/RoofMarshalZombie.h"
#include "Zombie/EngineeringShipZombie.h"
#include "Zombie/HijackerZombie.h"
#include "Zombie/HealerZombie.h"
#include "../GameRandom.h"
#include "Bullet/Bullet.h"
#include "Coin.h"
#include "LawnMower.h"

int EntityRegistry::AddPlant(std::shared_ptr<Plant> plant) {
	int id = mNextPlantID++;
	mPlants[id] = plant;
	plant->mPlantID = id;
	return id;
}

Plant* EntityRegistry::GetPlant(int id) const {
	auto it = mPlants.find(id);
	if (it != mPlants.end())
		return it->second.lock().get();
	return nullptr;
}

std::vector<int> EntityRegistry::GetAllPlantIDs() const {
	std::vector<int> ids;
	for (const auto& pair : mPlants) {
		if (pair.second.lock())
			ids.push_back(pair.first);
	}
	return ids;
}

int EntityRegistry::AddZombie(std::shared_ptr<Zombie> zombie) {
	int id = mNextZombieID++;
	mZombies[id] = zombie;
	zombie->mZombieID = id;
	mRowIndexDirty = true; // 同帧生成后，火球溅射等行查询必须立即看见新僵尸
	TrackGoldenIceSource(id, zombie);
	TrackRoofMarshal(id, zombie);
	TrackEngineeringShip(id, zombie);
	TrackHijacker(id, zombie);
	TrackNightRoofChargeGuide(id, zombie);
	TrackHealer(id, zombie);
	if (zombie->mZombieType == ZombieType::ZOMBIE_PRESSURE_GUARD) mPressureGuards[id] = zombie;
	else mPressureGuards.erase(id);
	return id;
}

Zombie* EntityRegistry::GetZombie(int id) const {
	auto it = mZombies.find(id);
	if (it != mZombies.end())
		return it->second.lock().get();
	return nullptr;
}

std::vector<int> EntityRegistry::GetAllZombieIDs() const {
	std::vector<int> ids;
	for (const auto& pair : mZombies) {
		if (pair.second.lock())
			ids.push_back(pair.first);
	}
	return ids;
}

std::shared_ptr<RoofMarshalZombie> EntityRegistry::GetFirstActiveRoofMarshal() const {
	// std::map 已按实体 ID 排序；通常首个候选就是唯一督军，不再随全场僵尸数增长。
	for (const auto& pair : mRoofMarshals) {
		auto marshal = pair.second.lock();
		if (!marshal || marshal->IsPreview() || !marshal->IsActive()
			|| marshal->IsDying() || marshal->mBodyHealth <= 0) {
			continue;
		}
		return marshal;
	}
	return nullptr;
}

std::shared_ptr<EngineeringShipZombie> EntityRegistry::GetFirstActiveEngineeringShip() const {
    for (const auto& [id, weak] : mEngineeringShips) {
        const auto ship = weak.lock();
        if (ship && ship->IsActive() && !ship->IsPreview() && !ship->IsDying() && ship->mBodyHealth > 0)
            return ship;
    }
    return nullptr;
}

std::shared_ptr<HijackerZombie> EntityRegistry::SelectNightRoofHijacker() const
{
	int highestHealth = -1;
	std::vector<std::shared_ptr<HijackerZombie>> tied;
	for (const auto& pair : mHijackers) {
		auto hijacker = pair.second.lock();
		if (!hijacker || !hijacker->CanBeNightRoofHijackerCandidate()) continue;
		const int health = hijacker->GetCountableExecutionHealth();
		if (health > highestHealth) {
			highestHealth = health;
			tied.clear();
			tied.push_back(std::move(hijacker));
		}
		else if (health == highestHealth) {
			tied.push_back(std::move(hijacker));
		}
	}
	if (tied.empty()) return nullptr;
	// mHijackers 是按 ID 有序的 map，tied 继承同一稳定顺序；随机只在锁定边沿消费一次。
	return tied[GameRandom::Range(0, static_cast<int>(tied.size()) - 1)];
}

int EntityRegistry::GetActiveNightRoofHijackerCount() const
{
	int count = 0;
	for (const auto& pair : mHijackers) {
		if (auto hijacker = pair.second.lock();
			hijacker && hijacker->CanBeNightRoofHijackerCandidate()) {
			++count;
		}
	}
	return count;
}

bool EntityRegistry::HasActiveNightRoofHijacker() const
{
	for (const auto& pair : mHijackers) {
		if (auto hijacker = pair.second.lock();
			hijacker && hijacker->CanBeNightRoofHijackerCandidate()) return true;
	}
	return false;
}

std::vector<std::shared_ptr<Zombie>>
EntityRegistry::GetNightRoofChargeGuideCandidates() const
{
	std::vector<std::shared_ptr<Zombie>> candidates;
	for (const auto& pair : mNightRoofChargeGuides) {
		auto zombie = pair.second.lock();
		if (zombie && zombie->CanGuideNightRoofCharge()) {
			candidates.push_back(std::move(zombie));
		}
	}
	return candidates;
}

int EntityRegistry::GetActiveNightRoofChargeGuideCount() const
{
	int count = 0;
	for (const auto& pair : mNightRoofChargeGuides) {
		if (auto zombie = pair.second.lock();
			zombie && zombie->CanGuideNightRoofCharge()) ++count;
	}
	return count;
}

bool EntityRegistry::IsHealerFocusedTargetReserved(
	int zombieID, int exceptHealerID) const
{
	if (zombieID == NULL_ZOMBIE_ID) return false;
	for (const auto& pair : mHealers) {
		if (pair.first == exceptHealerID) continue;
		if (auto healer = pair.second.lock();
			healer && healer->IsFocusedOnTarget(zombieID)) {
			return true;
		}
	}
	return false;
}

bool EntityRegistry::HasReadyHealerBefore(int healerID) const
{
	for (const auto& pair : mHealers) {
		if (pair.first >= healerID) break;
		if (auto healer = pair.second.lock();
			healer && healer->IsReadyForTreatmentChoice()) {
			return true;
		}
	}
	return false;
}

int EntityRegistry::AddBullet(std::shared_ptr<Bullet> bullet) {
	int id = mNextBulletID++;
	mBullets[id] = bullet;
	bullet->mBulletID = id;
	return id;
}

Bullet* EntityRegistry::GetBullet(int id) const {
	auto it = mBullets.find(id);
	if (it != mBullets.end())
		return it->second.lock().get();
	return nullptr;
}

std::vector<int> EntityRegistry::GetAllBulletIDs() const {
	std::vector<int> ids;
	for (const auto& pair : mBullets) {
		auto bullet = pair.second.lock();
		if (bullet && bullet->IsActive())
			ids.push_back(pair.first);
	}
	return ids;
}

void EntityRegistry::RemoveBullet(int id) {
	mBullets.erase(id);
}

int EntityRegistry::AddCoin(std::shared_ptr<Coin> coin) {
	int id = mNextCoinID++;
	mCoins[id] = coin;
	coin->mCoinID = id;
	return id;
}

Coin* EntityRegistry::GetCoin(int id) const {
	auto it = mCoins.find(id);
	if (it != mCoins.end())
		return it->second.lock().get();
	return nullptr;
}

std::vector<int> EntityRegistry::GetAllCoinIDs() const {
	std::vector<int> ids;
	for (const auto& pair : mCoins) {
		if (pair.second.lock())
			ids.push_back(pair.first);
	}
	return ids;
}

void EntityRegistry::EnsureZombieRowIndex() {
	if (!mRowIndexDirty) return;
	// clear() 保留各桶 capacity，跨帧复用，避免反复堆分配。
	for (auto& bucket : mZombiesByRow) bucket.clear();
	for (const auto& pair : mZombies) {
		if (auto z = pair.second.lock()) {
			// 只收"可作为目标"的僵尸：已 Die() 失活（待移除/泄漏）或垂死播死亡动画的都排除，
			// 否则射手/大嘴花等索敌方会朝隐形尸体或尸体持续开火（原版也不索敌垂死僵尸）。
			if (!z->IsActive() || z->IsDying()) continue;
			int row = z->mRow;  // 唯一真相源：换行只改这里，下一帧重建即归位
			if (row >= 0 && row < kMaxRows)
				mZombiesByRow[row].push_back(z.get());
		}
	}
	mRowIndexDirty = false;
}

bool EntityRegistry::IsZombieTargetable(const Zombie* zombie)
{
	return zombie && zombie->IsActive() && !zombie->IsDying();
}

void EntityRegistry::TrackGoldenIceSource(
	int id, const std::shared_ptr<Zombie>& zombie)
{
	if (auto gilded = std::dynamic_pointer_cast<GildedZamboniZombie>(zombie)) {
		mGoldenIceSources[id] = gilded;
		mGoldenIceSourceSnapshotDirty = true;
	}
	else if (mGoldenIceSources.erase(id) > 0) {
		mGoldenIceSourceSnapshotDirty = true;
	}
}

void EntityRegistry::EnsureGoldenIceSourceSnapshot()
{
	if (!mGoldenIceSourceSnapshotDirty) return;

	mGoldenIceSourceSnapshot.clear();
	mGoldenIceSourceSnapshot.reserve(mGoldenIceSources.size());
	for (const auto& pair : mGoldenIceSources) {
		if (auto source = pair.second.lock()) {
			// 与行索引保持相同候选契约；回调还会复核同帧晚些时候发生的状态切换。
			if (source->IsActive() && !source->IsDying()) {
				mGoldenIceSourceSnapshot.push_back(std::move(source));
			}
		}
	}
	mGoldenIceSourceSnapshotDirty = false;
}

void EntityRegistry::TrackRoofMarshal(
	int id, const std::shared_ptr<Zombie>& zombie)
{
	if (auto marshal = std::dynamic_pointer_cast<RoofMarshalZombie>(zombie)) {
		// 专用表只是附加 weak_ptr 索引；主表 mZombies 仍是所有僵尸的唯一全集。
		mRoofMarshals[id] = marshal;
	}
	else {
		mRoofMarshals.erase(id);
	}
}

void EntityRegistry::TrackEngineeringShip(int id, const std::shared_ptr<Zombie>& zombie)
{
    if (auto ship = std::dynamic_pointer_cast<EngineeringShipZombie>(zombie)) mEngineeringShips[id] = ship;
    else mEngineeringShips.erase(id);
}

void EntityRegistry::TrackHijacker(
	int id, const std::shared_ptr<Zombie>& zombie)
{
	if (auto hijacker = std::dynamic_pointer_cast<HijackerZombie>(zombie)) {
		mHijackers[id] = hijacker;
	}
	else {
		mHijackers.erase(id);
	}
}

void EntityRegistry::TrackNightRoofChargeGuide(
	int id, const std::shared_ptr<Zombie>& zombie)
{
	if (zombie && zombie->IsNightRoofChargeGuideType()) {
		mNightRoofChargeGuides[id] = zombie;
	}
	else {
		mNightRoofChargeGuides.erase(id);
	}
}

void EntityRegistry::TrackHealer(
	int id, const std::shared_ptr<Zombie>& zombie)
{
	if (auto healer = std::dynamic_pointer_cast<HealerZombie>(zombie)) {
		mHealers[id] = healer;
	}
	else {
		mHealers.erase(id);
	}
}

std::vector<int> EntityRegistry::CleanupExpired() {
	std::vector<int> removedPlants;

	// 每帧兜底标脏；Die/CommitRow 还会在生命周期边沿立即标脏，避免 GOM 在本函数运行前
	// 已释放僵尸，而此前构建的按行裸指针桶仍被植物索敌复用。
	mRowIndexDirty = true;
	// 释放上一帧的强引用并让首个黄色冰道查询从专用弱索引重建安全快照。
	mGoldenIceSourceSnapshot.clear();
	mGoldenIceSourceSnapshotDirty = true;

	// 清理植物：每帧扫，返回值用于 Board cell 同步（需准实时）
	for (auto it = mPlants.begin(); it != mPlants.end(); ) {
		if (it->second.expired()) {
			removedPlants.push_back(it->first);
			it = mPlants.erase(it);
		}
		else {
			++it;
		}
	}

	// 僵尸/coin/mower：纯内存 hygiene，攒到 CLEANUP_FULL_INTERVAL 帧一次性扫。
	// 间隔期间过期 weak_ptr 会滞留在 map 里，但 GetZombie/GetCoin 等都通过 weak_ptr.lock()
	// 取值，过期返回 nullptr，调用方语义不受影响。
	if (++mCleanupTick >= CLEANUP_FULL_INTERVAL) {
		mCleanupTick = 0;

		for (auto it = mZombies.begin(); it != mZombies.end(); ) {
			if (it->second.expired()) {
				it = mZombies.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = mGoldenIceSources.begin(); it != mGoldenIceSources.end(); ) {
			if (it->second.expired()) {
				it = mGoldenIceSources.erase(it);
			}
			else {
				++it;
			}
		}

		std::erase_if(mEngineeringShips, [](const auto& entry) { return entry.second.expired(); });

		for (auto it = mRoofMarshals.begin(); it != mRoofMarshals.end(); ) {
			if (it->second.expired()) {
				it = mRoofMarshals.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = mHijackers.begin(); it != mHijackers.end(); ) {
			if (it->second.expired()) {
				it = mHijackers.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = mNightRoofChargeGuides.begin();
			it != mNightRoofChargeGuides.end(); ) {
			if (it->second.expired()) it = mNightRoofChargeGuides.erase(it);
			else ++it;
		}

		std::erase_if(mPressureGuards, [](const auto& entry) { return entry.second.expired(); });
		for (auto it = mHealers.begin(); it != mHealers.end(); ) {
			if (it->second.expired()) {
				it = mHealers.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = mCoins.begin(); it != mCoins.end(); ) {
			if (it->second.expired()) {
				it = mCoins.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = mMowers.begin(); it != mMowers.end(); ) {
			if (it->second.expired()) {
				it = mMowers.erase(it);
			}
			else {
				++it;
			}
		}
	}

	return removedPlants;
}

int EntityRegistry::AddPlantWithID(std::shared_ptr<Plant> plant, int id) {
	mPlants[id] = plant;
	plant->mPlantID = id;
	if (id >= mNextPlantID) {
		mNextPlantID = id + 1;
	}
	return id;
}

int EntityRegistry::AddZombieWithID(std::shared_ptr<Zombie> zombie, int id) {
	mZombies[id] = zombie;
	zombie->mZombieID = id;
	mRowIndexDirty = true;
	TrackGoldenIceSource(id, zombie);
	TrackRoofMarshal(id, zombie);
	TrackEngineeringShip(id, zombie);
	TrackHijacker(id, zombie);
	TrackNightRoofChargeGuide(id, zombie);
	TrackHealer(id, zombie);
	if (zombie->mZombieType == ZombieType::ZOMBIE_PRESSURE_GUARD) mPressureGuards[id] = zombie;
	else mPressureGuards.erase(id);
	if (id >= mNextZombieID) {
		mNextZombieID = id + 1;
	}
	return id;
}

int EntityRegistry::AddBulletWithID(std::shared_ptr<Bullet> bullet, int id) {
	mBullets[id] = bullet;
	bullet->mBulletID = id;
	if (id >= mNextBulletID) {
		mNextBulletID = id + 1;
	}
	return id;
}

int EntityRegistry::AddCoinWithID(std::shared_ptr<Coin> coin, int id) {
	mCoins[id] = coin;
	coin->mCoinID = id;
	if (id >= mNextCoinID) {
		mNextCoinID = id + 1;
	}
	return id;
}

int EntityRegistry::AddMower(std::shared_ptr<Mower> mower) {
	int id = mNextMowerID++;
	mMowers[id] = mower;
	mower->mMowerID = id;
	return id;
}

Mower* EntityRegistry::GetMower(int id) const {
	auto it = mMowers.find(id);
	if (it != mMowers.end())
		return it->second.lock().get();
	return nullptr;
}

std::vector<int> EntityRegistry::GetAllMowerIDs() const {
	std::vector<int> ids;
	for (const auto& pair : mMowers) {
		if (pair.second.lock())
			ids.push_back(pair.first);
	}
	return ids;
}

int EntityRegistry::AddMowerWithID(std::shared_ptr<Mower> mower, int id) {
	mMowers[id] = mower;
	mower->mMowerID = id;
	if (id >= mNextMowerID) {
		mNextMowerID = id + 1;
	}
	return id;
}
