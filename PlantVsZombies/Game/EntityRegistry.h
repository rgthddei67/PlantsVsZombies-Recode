#pragma once
#ifndef _ENTITYREGISTRY_H
#define _ENTITYREGISTRY_H
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>

class Plant;
class Zombie;
class GildedZamboniZombie;
class RoofMarshalZombie;
class EngineeringShipZombie;
class HijackerZombie;
class HealerZombie;
class Coin;
class Bullet;
class Mower;
enum class ZombieType;

class EntityRegistry {
public:
	int AddPlant(std::shared_ptr<Plant> plant);
	Plant* GetPlant(int id) const;
	std::vector<int> GetAllPlantIDs() const;

	int AddZombie(std::shared_ptr<Zombie> zombie);
	Zombie* GetZombie(int id) const;
	std::vector<int> GetAllZombieIDs() const;
	/** 返回实体 ID 最小的活跃屋脊督军；查询只访问首领专用索引，不扫描全体僵尸。 */
	std::shared_ptr<RoofMarshalZombie> GetFirstActiveRoofMarshal() const;
	/** 从稀有首领弱索引读取仍存活的工程船，供 HUD 派生显示。 */
	std::shared_ptr<EngineeringShipZombie> GetFirstActiveEngineeringShip() const;
	/** 从劫持者专用索引选择当前可计生命最高的候选；最高值并列时按 ID 有序集合随机一次。 */
	std::shared_ptr<HijackerZombie> SelectNightRoofHijacker() const;
	/** 返回专用弱索引中当前仍满足锁定门禁的候选数，不扫描普通僵尸。 */
	int GetActiveNightRoofHijackerCount() const;
	/** 热路径只检查劫持者专用弱索引并在首个有效候选处返回。 */
	bool HasActiveNightRoofHijacker() const;
	/** 返回按稳定实体 ID 排序的当前有效引雷候选；只遍历稀有弱索引。 */
	std::vector<std::shared_ptr<Zombie>> GetNightRoofChargeGuideCandidates() const;
	/** 返回当前有效引雷候选数，不扫描普通僵尸。 */
	int GetActiveNightRoofChargeGuideCount() const;
	/** 单疗预留查询只遍历急救员稀有索引；exceptHealerID 的自身预留不算冲突。 */
	bool IsHealerFocusedTargetReserved(int zombieID, int exceptHealerID) const;
	/** 多名急救员同时就绪时只允许实体 ID 最小者先尝试领取 Board 的分帧推演预算。 */
	bool HasReadyHealerBefore(int healerID) const;

    /** 承伤热路径只遍历护卫弱索引；回调自行检查同帧生命、阵营和距离。 */
    template<typename Fn> void ForEachPressureGuard(Fn&& fn) {
        for (const auto& entry : mPressureGuards) if (auto guard=entry.second.lock()) fn(guard.get());
    }
	int AddBullet(std::shared_ptr<Bullet> bullet);
	Bullet* GetBullet(int id) const;
	std::vector<int> GetAllBulletIDs() const;
	void RemoveBullet(int id);

	int AddCoin(std::shared_ptr<Coin> coin);
	Coin* GetCoin(int id) const;
	std::vector<int> GetAllCoinIDs() const;

	int AddMower(std::shared_ptr<Mower> mower);
	Mower* GetMower(int id) const;
	std::vector<int> GetAllMowerIDs() const;

	// 带指定 ID 添加实体（用于读档恢复）
	int AddPlantWithID(std::shared_ptr<Plant> plant, int id);
	int AddZombieWithID(std::shared_ptr<Zombie> zombie, int id);
	int AddBulletWithID(std::shared_ptr<Bullet> bullet, int id);
	int AddCoinWithID(std::shared_ptr<Coin> coin, int id);
	int AddMowerWithID(std::shared_ptr<Mower> mower, int id);

	// ID 计数器访问方法（用于存档）
	int GetNextPlantID() const { return mNextPlantID; }
	int GetNextZombieID() const { return mNextZombieID; }
	int GetNextBulletID() const { return mNextBulletID; }
	int GetNextCoinID() const { return mNextCoinID; }
	int GetNextMowerID() const { return mNextMowerID; }

	void SetNextPlantID(int id) { mNextPlantID = id; }
	void SetNextZombieID(int id) { mNextZombieID = id; }
	void SetNextBulletID(int id) { mNextBulletID = id; }
	void SetNextCoinID(int id) { mNextCoinID = id; }
	void SetNextMowerID(int id) { mNextMowerID = id; }

	// 清理过期对象 返回清理的植物ID
	// 植物每帧扫（用于 cell 同步），僵尸/coin/mower 每 CLEANUP_FULL_INTERVAL 帧扫一次（纯内存 hygiene）
	std::vector<int> CleanupExpired();

	/** 僵尸死亡或换行时立即废弃按行裸指针缓存，防止延迟销毁后留下悬空引用。 */
	void InvalidateZombieRowIndex() { mRowIndexDirty = true; }

	// 按行遍历存活僵尸，避免射手/嗅食类植物每次都全表扫描 + 双重 weak_ptr.lock。
	// 索引是惰性重建的（参照 CollisionSystem 的按行分桶）：唯一真相源是僵尸的 mRow。
	// 僵尸死亡或换行入口必须先标脏，确保下次查询从 weak_ptr 主表重建。
	// fn 签名为 void(Zombie*)；只回调本行存活僵尸。
	template<typename Fn>
	void ForEachZombieInRow(int row, Fn&& fn) {
		if (row < 0 || row >= kMaxRows) return;
		EnsureZombieRowIndex();
		for (Zombie* z : mZombiesByRow[row]) {
			// 回调之间仍可能让其他候选进入垂死态；不必为这种同帧状态边沿重建整桶。
			if (IsZombieTargetable(z)) fn(z);
		}
	}

	// 遍历本帧仍可作为黄色冰道来源的鎏金冰车；候选集只含该品种，不再让每只僵尸扫描相邻行全体。
	// fn 签名为 void(GildedZamboniZombie*)；调用方仍须在回调内复核同帧发生的死亡/失活边沿。
	template<typename Fn>
	void ForEachGoldenIceSource(Fn&& fn) {
		EnsureGoldenIceSourceSnapshot();
		for (const auto& source : mGoldenIceSourceSnapshot) fn(source.get());
	}

private:
	// 每 N 帧做一次"重表"全扫；植物表始终每帧扫
	static constexpr int CLEANUP_FULL_INTERVAL = 30;
	int mCleanupTick = 0;

	int mNextPlantID = 1;
	std::map<int, std::weak_ptr<Zombie>> mPressureGuards;
	int mNextZombieID = 1;
	int mNextBulletID = 1;
	int mNextCoinID = 1;
	int mNextMowerID = 1;

	std::unordered_map<int, std::weak_ptr<Plant>> mPlants;
	std::unordered_map<int, std::weak_ptr<Zombie>> mZombies;
	std::unordered_map<int, std::weak_ptr<Bullet>> mBullets;
	std::unordered_map<int, std::weak_ptr<Coin>> mCoins;
	std::unordered_map<int, std::weak_ptr<Mower>> mMowers;

	// ── 僵尸按行空间索引（瞬态、每帧惰性重建）──
	// 与 CollisionSystem::MAX_ROWS 取同值：行号上界。桶用裸指针降低热路径引用计数开销；
	// Zombie 的死亡/换行入口必须立即标脏，保证 GOM 真正释放对象前不再复用旧桶。
	static constexpr int kMaxRows = 8;
	std::array<std::vector<Zombie*>, kMaxRows> mZombiesByRow;
	bool mRowIndexDirty = true;  // 生命周期边沿及 CleanupExpired 置脏；首次行查询时重建
	void EnsureZombieRowIndex();
	static bool IsZombieTargetable(const Zombie* zombie);

	// ── 黄色冰道独立来源索引（瞬态、每帧惰性快照）──
	// 弱索引按实体 ID 覆盖普通生成与读档恢复；快照用 shared_ptr 把回调期间的来源生命周期钉住。
	std::unordered_map<int, std::weak_ptr<GildedZamboniZombie>> mGoldenIceSources;
	std::vector<std::shared_ptr<GildedZamboniZombie>> mGoldenIceSourceSnapshot;
	bool mGoldenIceSourceSnapshotDirty = true;
	/** 登记或覆盖指定 ID 的鎏金冰车来源；非鎏金类型会撤销同 ID 的旧登记。 */
	void TrackGoldenIceSource(int id, const std::shared_ptr<Zombie>& zombie);
	/** 从专用弱索引生成本帧活跃来源快照，不扫描全体僵尸。 */
	void EnsureGoldenIceSourceSnapshot();

	// ── 屋脊督军独立索引（瞬态、按实体 ID 有序）──
	// 首领血条每个渲染帧查询；只遍历极少数督军候选，并保持开发模式多实例时选择最小 ID 的契约。
	std::map<int, std::weak_ptr<RoofMarshalZombie>> mRoofMarshals;
	std::map<int, std::weak_ptr<EngineeringShipZombie>> mEngineeringShips;
	/** 登记或覆盖指定 ID 的屋脊督军；非督军类型会撤销同 ID 的旧登记。 */
	void TrackRoofMarshal(int id, const std::shared_ptr<Zombie>& zombie);
	void TrackEngineeringShip(int id, const std::shared_ptr<Zombie>& zombie);

	// ── 劫持者独立索引（瞬态、按实体 ID 有序）──
	// 仅在每轮雷荷跨过 75% 时遍历；总成本只随稀有候选数增长，不随全场普通僵尸数增长。
	std::map<int, std::weak_ptr<HijackerZombie>> mHijackers;
	/** 登记或覆盖指定 ID 的劫持者；非劫持者覆盖同 ID 时撤销旧登记。 */
	void TrackHijacker(int id, const std::shared_ptr<Zombie>& zombie);

	// ── 黑夜屋顶引雷单位独立索引（瞬态、按实体 ID 有序）──
	// 类型通过 Zombie 虚接口声明，未来新增引雷僵尸无需再为每个品种复制一套索引。
	std::map<int, std::weak_ptr<Zombie>> mNightRoofChargeGuides;
	/** 登记或覆盖指定 ID 的引雷类型；装备损毁只影响候选门禁，不撤销类型索引。 */
	void TrackNightRoofChargeGuide(int id, const std::shared_ptr<Zombie>& zombie);

	// ── 急救员独立索引（瞬态、按实体 ID 有序）──
	// 只在六秒决策边沿协调单疗预留和同帧顺序，常态治疗候选扫描仍是低频事件。
	std::map<int, std::weak_ptr<HealerZombie>> mHealers;
	/** 登记或覆盖指定 ID 的急救员；非急救员覆盖同 ID 时撤销旧登记。 */
	void TrackHealer(int id, const std::shared_ptr<Zombie>& zombie);
};

#endif
