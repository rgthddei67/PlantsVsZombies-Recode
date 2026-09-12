#include "GameObjectManager.h"
#include "../Logger.h"
#include "../Profiler.h"
#include "AnimatedObject.h"
#include <atomic>
#include <cstdio>
#include <unordered_set>

namespace {
	constexpr int kBattlefieldRowStride = SUBORDER_PER_KEY * 2;
	constexpr int kBattlefieldOrderCapacity =
		LAYER_GAME_BULLET - LAYER_GAME_PLANT;
	constexpr int kBattlefieldMaximumRows =
		kBattlefieldOrderCapacity / kBattlefieldRowStride;
	constexpr int kParallelDrawTargetObjectsPerSlot = 420; // 每个有序录制切片期望覆盖的活跃对象数；控制负载均衡与回放开销
	constexpr int kParallelDrawMaxSlotsPerWorker = 4;      // 每个物理 worker 最多领取的切片数；限制低核心机器的命令流碎片

	// 植物与僵尸共用战场深度区间；同排植物在前、僵尸在后，下一排再整体覆盖上一排。
	bool UsesBattlefieldRowDepth(RenderLayer layer, int key)
	{
		return key >= 0 && key < kBattlefieldMaximumRows
			&& (layer == LAYER_GAME_PLANT || layer == LAYER_GAME_ZOMBIE);
	}

	int GetBattlefieldRowBandBase(RenderLayer layer, int key)
	{
		return LAYER_GAME_PLANT + key * kBattlefieldRowStride
			+ (layer == LAYER_GAME_ZOMBIE ? SUBORDER_PER_KEY : 0);
	}
}

GameObjectManager::GameObjectManager() {
	ResetAllLayers();
	int n = static_cast<int>(std::thread::hardware_concurrency());
	if (n < 1) n = 1;
	mThreadPool = std::make_unique<ThreadPool>(n);

	mGameObjects.reserve(2048);
	mObjectsToAdd.reserve(256);
	mObjectsToRemove.reserve(2048);

	// 初始化对象池
	mBulletPool = std::make_unique<BulletPool>();
	mBulletPool->Initialize(300, 600);  // 初始容量 300，警告阈值 600
}

void GameObjectManager::DestroyGameObject(std::shared_ptr<GameObject> obj) {
	if (obj) {
		ReleaseRenderOrder(obj.get());
		mObjectsToRemove.push_back(obj);
	}
}

void GameObjectManager::DestroyGameObject(GameObject* raw) {
	if (!raw) return;
	for (const auto& obj : mGameObjects) {
		if (obj.get() == raw) {
			DestroyGameObject(obj);
			return;
		}
	}
	for (const auto& obj : mObjectsToAdd) {
		if (obj.get() == raw) {
			DestroyGameObject(obj);
			return;
		}
	}
	// 两个列表都没找到：对象不会被销毁，shared_ptr 泄漏 → 失活的隐形对象（如僵尸）
	// 会残留在 EntityRegistry 索引里被持续索敌。静默失败极难排查，必须留痕。
	LOG_WARN("GOM") << "DestroyGameObject(raw) 未找到对象，销毁被跳过（疑似泄漏）: layer="
		<< raw->GetLayer() << " renderOrder=" << raw->GetRenderOrder();
}

void GameObjectManager::DestroyAllGameObjects() {
	mBulletPool->Clear();

	for (auto& obj : mGameObjects) {
		if (obj) {
			obj->DestroyAttachments();
		}
	}
	mGameObjects.clear();

	for (auto& obj : mObjectsToAdd) {
		if (obj) {
			obj->DestroyAttachments();
		}
	}
	mObjectsToAdd.clear();

	for (auto& obj : mObjectsToRemove) {
		if (obj) {
			obj->DestroyAttachments();
		}
	}
	mObjectsToRemove.clear();

	ResetAllLayers();

	LOG_DEBUG("GameObjectManager") << "DestroyAllGameObjects 已销毁所有游戏对象";
}

void GameObjectManager::Update() {
	PROFILE_SCOPE("2.GOM_Update(serial)");
	// 有增删时标记排序脏
	if (!mObjectsToRemove.empty() || !mObjectsToAdd.empty())
		mSortDirty = true;

	// 移除在mObjectsToRemove中的对象
	// 旧实现：逐个 std::remove 全表扫描 → O(n·k)。
	// 现改为：收集成 unordered_set，单趟 remove_if → O(n+k)，并保持 renderOrder 有序。
	if (!mObjectsToRemove.empty()) {
		// 以裸指针建集合，判定走指针哈希，避开 shared_ptr 引用计数开销
		std::unordered_set<GameObject*> toRemove;
		toRemove.reserve(mObjectsToRemove.size() * 2);
		// 下标循环 + 每次重读 size：保持旧实现的 realloc 安全性——
		// 若附件销毁期间未来新增的回调又追加移除项，本帧仍一并处理。
		// （拷贝 shared_ptr 而非引用，避免 vector 重新分配后引用失效。）
		for (size_t i = 0; i < mObjectsToRemove.size(); i++) {
			auto obj = mObjectsToRemove[i];
			if (obj) {
				obj->DestroyAttachments();
				toRemove.insert(obj.get());
			}
		}
		// remove_if 是稳定的：保留剩余元素相对顺序，不破坏按 renderOrder 升序的不变量
		mGameObjects.erase(
			std::remove_if(mGameObjects.begin(), mGameObjects.end(),
				[&toRemove](const std::shared_ptr<GameObject>& o) {
					return toRemove.count(o.get()) != 0;
				}),
			mGameObjects.end()
		);
		mObjectsToRemove.clear();
	}

	// 新对象的操作
	for (size_t i = 0; i < mObjectsToAdd.size(); i++) {
		auto obj = mObjectsToAdd[i];
		mGameObjects.push_back(obj);
		obj->Start();
	}
	mObjectsToAdd.clear();

	// 现有对象
	{
		PROFILE_SCOPE("2b.GOM_objUpdateLoop(serial)");
		const int total = static_cast<int>(mGameObjects.size());
		const int inactivePooledBullets = mBulletPool ? mBulletPool->GetInactiveCount() : 0;
		const int parallelCandidateCount = std::max(0, total - inactivePooledBullets);
		constexpr int kParallelUpdateThreshold = 200; // 实际会更新的对象达到此量级才支付调度成本

		// 休眠弹丸仍留在 GOM 维持稳定所有权，但不应把小场景误判为并行更新场景。
		if (mThreadPool && parallelCandidateCount >= kParallelUpdateThreshold) {
			int numWorkers = static_cast<int>(std::thread::hardware_concurrency());
			if (numWorkers < 1) numWorkers = 1;
			if (numWorkers > total) numWorkers = total;

			if (static_cast<int>(mDeferredEventBuffers.size()) < numWorkers)
				mDeferredEventBuffers.resize(numWorkers);
			for (auto& buf : mDeferredEventBuffers) buf.clear();

			// 阶段 A：并行推进（仅 animator 帧推进 + 事件入队，对象本地）
			mThreadPool->Dispatch(total, [this, total, numWorkers](int start, int end) {
				const int chunkSize = (total + numWorkers - 1) / numWorkers;
				const int slot = (chunkSize > 0) ? (start / chunkSize) : 0;
				auto& outBuf = mDeferredEventBuffers[slot];
				for (int i = start; i < end; ++i) {
					auto* obj = mGameObjects[i].get();
					if (obj->IsActive()) obj->UpdateParallel(outBuf);
				}
				});

			// 阶段 B-1：主线程 drain deferred event buffers
			{
				PROFILE_SCOPE("2c.PhaseB_DrainEvents");
				for (auto& buf : mDeferredEventBuffers) {
					for (auto& evt : buf) evt.cb();
				}
			}

			// 阶段 B-2：串行（mGameObjects 序），其余 Update 全部在此
			{
				PROFILE_SCOPE("2d.PhaseB_serialUpdate");
				for (int i = 0; i < total; ++i) {
					auto* obj = mGameObjects[i].get();
					if (obj->IsActive()) obj->Update();
				}
			}
		}
		else {
			// 串行 fallback：与原代码逐字节一致
			for (size_t i = 0; i < mGameObjects.size(); i++) {
				auto obj = mGameObjects[i].get();
				if (obj->IsActive()) {
					obj->Update();
				}
			}
		}
	}
}

void GameObjectManager::DrawAll(Graphics* g) {
	// 增删或显式改序后重排；同号用创建序号打破并列，预览不能扰动已有对象的先后。
	{
		PROFILE_SCOPE("4.Draw_sort(serial)");
		if (mSortDirty) {
			std::sort(mGameObjects.begin(), mGameObjects.end(),
				[](const std::shared_ptr<GameObject>& a, const std::shared_ptr<GameObject>& b) {
					return a->IsDrawnBefore(*b);
				});
			mSortDirty = false;
		}
	}

	// 子弹阴影是地面投影，不能跟随 Bullet 对象留在 LAYER_GAME_BULLET，否则会压住植物。
	// 先统一绘制；并行路径随后在 BeginParallelRecord 中 Flush，可保持这批阴影严格在主体之前。
	{
		PROFILE_SCOPE("5a.Draw_bulletShadows");
		if (mBulletPool) mBulletPool->DrawShadows(g);
	}

	const int total = static_cast<int>(mGameObjects.size());

	// ---- 绘制阶段：小对象数走原串行；大场景走并行 record + 主线程 replay ----
	constexpr int kParallelDrawThreshold = 200;  // 小于此阈值，并行 dispatch overhead 不划算

	// 单个对象的串行绘制（含 clip 处理），并行 fallback 与 overlay 尾段共用。
	auto drawSerialRange = [&](int begin, int end) {
		for (int i = begin; i < end; ++i) {
			auto* obj = mGameObjects[i].get();
			if (!obj->IsActive()) continue;
			const bool clipped = obj->HasClipRect();
			if (clipped) {
				const auto& cr = obj->GetClipRect();
				g->PushClipRect(cr.x, cr.y, cr.w, cr.h);
			}
			obj->Draw(g);
			if (clipped) {
				g->PopClipRect();
			}
		}
		};

	// ---- 顶层对象（renderOrder ≥ LAYER_UI）必须永远画在游戏主体之上 ----
	// 并行 replay 的 emitUpTo 在每个 blend 段内固定"先 batch 后 instance"（依赖 shadow→reanim
	// 约定）。当某个 worker 切片横跨"低层 reanim instance(植物/僵尸) → 高层 batch quad(进度条/
	// 卡片/铲子)"边界且段内无 blend 切换时，更高层的 batch 顶点会被拉到 reanim instance 之前
	// emit，导致 reanim 反盖 UI（随切片边界漂移而时隐时现）。把 renderOrder ≥ LAYER_UI 的对象
	// 摘出，在 replay 之后主线程串行绘制——与 deferred-text"覆盖层最后画"同构，既保证顶层恒在
	// 最上，又不动主体的并行批处理。
	//
	// 注意阈值取 LAYER_UI(40000)，按数值 *同时* 捞走了更高层：LAYER_GAME_COIN(阳光, 50000,
	// reanim instance) 与 LAYER_EFFECTS(80000)。这无损正确性——split + 两个分区各自升序 = 完整
	// 保留全局 z-order；阳光的 instance 走主线程 AppendReanimInstance 分支，落在 BeginParallelRecord
	// 预留的 POST_PARALLEL_RESERVE_INST(1MB) 余量内。代价仅是这两层不再并行（对象数很少，可忽略）。
	// mGameObjects 已按 renderOrder 升序，二分定位第一个 ≥ LAYER_UI 的对象。
	int splitIdx = total;
	{
		auto it = std::lower_bound(mGameObjects.begin(), mGameObjects.end(), static_cast<int>(LAYER_UI),
			[](const std::shared_ptr<GameObject>& o, int order) {
				return o->GetRenderOrder() < order;
			});
		splitIdx = static_cast<int>(it - mGameObjects.begin());
	}
	const int parallelCount = splitIdx;  // [0, splitIdx) 走并行；[splitIdx, total) overlay 串行
	const int inactivePooledBullets = mBulletPool ? mBulletPool->GetInactiveCount() : 0;
	const int parallelCandidateCount = std::max(0, parallelCount - inactivePooledBullets);
	const int desiredRecordSlotCount = std::max(1,
		(parallelCandidateCount + kParallelDrawTargetObjectsPerSlot - 1) /
		kParallelDrawTargetObjectsPerSlot);
	const int availableWorkers = mThreadPool ? mThreadPool->GetWorkerCount() : 1;
	const int numWorkers = std::min({ availableWorkers, parallelCount, desiredRecordSlotCount });

	// 休眠弹丸仍在线性表内但不会提交绘制；阈值只看真正可能产生命令的对象。对象规模
	// 只够一个切片时也保留串行路径，避免普通关卡为了“并行”额外拆出命令流。
	if (!g->IsParallelDrawEnabled() || parallelCandidateCount < kParallelDrawThreshold || numWorkers < 2) {
		// 串行 fallback：主体 → 世界粒子/天气覆盖/Scene UI 贴图 → UI GameObject。
		PROFILE_SCOPE("6.Draw_submit(serial-fallback)");
		drawSerialRange(0, splitIdx);
		if (mPreOverlayHook) mPreOverlayHook();
		drawSerialRange(splitIdx, total);
		return;
	}

	// 并行 record + replay（只覆盖 [0, parallelCount) 的游戏对象主体）。切片数同时由对象规模、
	// 实际 worker 数与每 worker 上限决定；worker 动态领取连续对象区间，减少复杂动画集中在
	// 某个固定区间造成的长尾。Replay 仍严格按 record slot 递增回放，所以全局 z-order 与
	// 原串行 Draw 等价。
	const int recordSlotCount = std::min({ parallelCount, desiredRecordSlotCount,
		numWorkers * kParallelDrawMaxSlotsPerWorker });
	const bool profileDrawWorkers = g_ProfileEnabled;
	if (profileDrawWorkers) {
		mDrawWorkerProfileSamples.resize(numWorkers);
		std::fill(mDrawWorkerProfileSamples.begin(), mDrawWorkerProfileSamples.end(),
			DrawWorkerProfileSample{});
	}

	{
		PROFILE_SCOPE("6.Draw_submit(par-record)");
		g->BeginParallelRecord(recordSlotCount);
		std::atomic<int> nextRecordSlot{ 0 };

		// totalItems=numWorkers 让 ThreadPool 的每个物理线程只进入一次回调；回调内部再动态领切片。
		mThreadPool->Dispatch(numWorkers,
			[this, g, parallelCount, recordSlotCount, profileDrawWorkers, &nextRecordSlot](int start, int) {
			const int workerSlot = start;
			const auto workerStart = profileDrawWorkers ? Profiler::Clock::now() : Profiler::Clock::time_point{};
			size_t activeObjects = 0;

			for (;;) {
				const int recordSlot = nextRecordSlot.fetch_add(1, std::memory_order_relaxed);
				if (recordSlot >= recordSlotCount) break;
				const int rangeBegin = static_cast<int>(
					static_cast<long long>(parallelCount) * recordSlot / recordSlotCount);
				const int rangeEnd = static_cast<int>(
					static_cast<long long>(parallelCount) * (recordSlot + 1) / recordSlotCount);

				g->SetWorkerSlot(recordSlot);
				for (int i = rangeBegin; i < rangeEnd; ++i) {
					auto* obj = mGameObjects[i].get();
					if (!obj->IsActive()) continue;
					if (profileDrawWorkers) ++activeObjects;
					const bool clipped = obj->HasClipRect();
					if (clipped) {
						const auto& cr = obj->GetClipRect();
						g->PushClipRect(cr.x, cr.y, cr.w, cr.h);
					}
					obj->Draw(g);
					if (clipped) {
						g->PopClipRect();
					}
				}
				g->ClearWorkerSlot();
			}

			if (profileDrawWorkers) {
				auto& sample = mDrawWorkerProfileSamples[workerSlot];
				sample.elapsedMs = std::chrono::duration<double, std::milli>(
					Profiler::Clock::now() - workerStart).count();
				sample.activeObjects = activeObjects;
			}
			});
	}
	if (profileDrawWorkers) {
		size_t activeObjects = 0;
		double workerElapsedSumMs = 0.0;
		double longestWorkerMs = 0.0;
		for (int slot = 0; slot < numWorkers; ++slot) {
			const auto& sample = mDrawWorkerProfileSamples[slot];
			activeObjects += sample.activeObjects;
			workerElapsedSumMs += sample.elapsedMs;
			longestWorkerMs = std::max(longestWorkerMs, sample.elapsedMs);
		}
		Profiler::Get().CountParallelDraw(activeObjects, numWorkers, recordSlotCount,
			workerElapsedSumMs, longestWorkerMs);
	}

	{
		PROFILE_SCOPE("7.Draw_replay(serial)");
		g->ReplayAndEndParallel();
	}

	// 主体 replay 后统一插入世界粒子、天气覆盖与 Scene UI 贴图（主线程串行，保序）。
	if (mPreOverlayHook) mPreOverlayHook();

	// UI 层（renderOrder ≥ LAYER_UI）在接缝之后主线程串行绘制，保证恒在最上层。
	// 串行路径走 m_batchVertices / AppendReanimInstance 的主线程分支，自带 cross-flush 严格保序。
	{
		PROFILE_SCOPE("8.Draw_overlay(serial)");
		drawSerialRange(splitIdx, total);
	}
}

std::vector<std::shared_ptr<GameObject>> GameObjectManager::FindGameObjectsWithTag(const std::string& tag) {
	std::vector<std::shared_ptr<GameObject>> result;
	for (auto& obj : mGameObjects) {
		if (obj->GetTag() == tag) {
			result.push_back(obj);
		}
	}
	return result;
}

std::shared_ptr<GameObject> GameObjectManager::FindGameObjectWithTag(const std::string& tag) {
	auto objects = FindGameObjectsWithTag(tag);
	return objects.empty() ? nullptr : objects[0];
}

void GameObjectManager::ClearAll() {
	// BulletPool 现在共同持有强引用；任何全清入口都必须先解除池所有权与槽位元数据。
	if (mBulletPool) mBulletPool->Clear();

	for (auto& obj : mGameObjects) {
		if (obj) {
			obj->DestroyAttachments();
		}
	}
	mGameObjects.clear();

	for (auto& obj : mObjectsToAdd) {
		if (obj) {
			obj->DestroyAttachments();
		}
	}
	mObjectsToAdd.clear();

	for (auto& obj : mObjectsToRemove) {
		if (obj) {
			obj->DestroyAttachments();
		}
	}
	mObjectsToRemove.clear();

	ResetAllLayers();
}

void GameObjectManager::PrintPoolStats() const {
	if (mBulletPool) {
		mBulletPool->PrintStats();
	}
}

void GameObjectManager::ResetAllLayers() {
	mLayerRecycledOrders.clear();
	mLayerMaxSubOrder.clear();
	mLayerKeyRecycledOrders.clear();
	mLayerKeyMaxLocalIdx.clear();

	// 初始化所有枚举值
	RenderLayer layers[] = {
		LAYER_BACKGROUND,
		LAYER_GAME_OBJECT,
		LAYER_GAME_PLANT,
		LAYER_GAME_ZOMBIE,
		LAYER_GAME_BULLET,
		LAYER_GAME_COIN,
		LAYER_EFFECTS,
		LAYER_UI,
		LAYER_DEBUG
	};

	for (RenderLayer layer : layers) {
		ResetLayer(layer);
	}
}

void GameObjectManager::AssignRenderOrder(GameObject* gameObject, RenderLayer layer) {
	if (!gameObject) return;
	ReleaseRenderOrder(gameObject);
	gameObject->mRenderOrderManager = this;
	if (gameObject->mRenderSequence == 0) gameObject->mRenderSequence = mNextRenderSequence++;
	AssignNewRenderOrder(gameObject, layer);
}

void GameObjectManager::ReleaseRenderOrder(GameObject* gameObject)
{
	auto& allocation = gameObject->mRenderOrderAllocation;
	if (!allocation.allocated) return;
	RecycleRenderOrder(allocation.order, allocation.layer, allocation.key);
	allocation.allocated = false;
}

void GameObjectManager::SwapRenderOrders(GameObject* first, GameObject* second)
{
	if (!first || !second || first == second) return;
	std::swap(first->mRenderOrderAllocation, second->mRenderOrderAllocation);
	std::swap(first->mRenderOrder, second->mRenderOrder);
	std::swap(first->mRenderSubOrder, second->mRenderSubOrder);
	mSortDirty = true;
}

void GameObjectManager::RefreshRenderOrderForSortingKey(
	GameObject* gameObject, int previousKey)
{
	if (!gameObject || previousKey == gameObject->GetSortingKey()) return;
	ReleaseRenderOrder(gameObject);
	AssignNewRenderOrder(gameObject, gameObject->GetLayer());
	mSortDirty = true;
}

void GameObjectManager::AssignNewRenderOrder(GameObject* gameObject, RenderLayer layer) {
	int subOrder;
	int key = gameObject->GetSortingKey();
	if (key >= 0) {
		subOrder = GetNextSubOrderForKey(layer, key);
	}
	else {
		subOrder = GetNextSubOrder(layer);
	}
	int renderOrder = static_cast<int>(layer) + subOrder;
	if (UsesBattlefieldRowDepth(layer, key)) {
		const int localIndex = subOrder - key * SUBORDER_PER_KEY;
		renderOrder = GetBattlefieldRowBandBase(layer, key) + localIndex;
	}
	gameObject->mRenderOrderAllocation = { renderOrder, layer, key, true };
	gameObject->SetRenderOrder(renderOrder);
}

void GameObjectManager::RecycleRenderOrder(int renderOrder, RenderLayer layer, int key) {
	if (UsesBattlefieldRowDepth(layer, key)) {
		const int localIndex = renderOrder - GetBattlefieldRowBandBase(layer, key);
		if (localIndex >= 0 && localIndex < SUBORDER_PER_KEY) {
			mLayerKeyRecycledOrders[layer][key].insert(
				key * SUBORDER_PER_KEY + localIndex);
		}
		return;
	}

	int subOrder = renderOrder - static_cast<int>(layer);
	if (subOrder >= 0 && subOrder < 10000) {  // 范围Χ 0~9999
		if (key >= 0) {
			mLayerKeyRecycledOrders[layer][key].insert(subOrder);
		}
		else {
			mLayerRecycledOrders[layer].insert(subOrder);
		}
	}
}

int GameObjectManager::GetNextSubOrder(RenderLayer layer) {
	// 1. 优先从回收池获取
	if (!mLayerRecycledOrders[layer].empty()) {
		int recycled = *mLayerRecycledOrders[layer].begin();
		mLayerRecycledOrders[layer].erase(mLayerRecycledOrders[layer].begin());
		return recycled;
	}

	// 2. 分配新的子顺序
	int newOrder = mLayerMaxSubOrder[layer]++;

	// 3. 检查是否超过图层容量
	if (newOrder >= 10000) { // 每个图层最多10000个子顺序
		// 尝试从回收池获取
		if (!mLayerRecycledOrders[layer].empty()) {
			newOrder = *mLayerRecycledOrders[layer].begin();
			mLayerRecycledOrders[layer].erase(mLayerRecycledOrders[layer].begin());
		}
		else {
			// 重置该图层
			ResetLayer(layer);
			newOrder = mLayerMaxSubOrder[layer]++; // 从0开始
		}
	}

	return newOrder;
}

int GameObjectManager::GetNextSubOrderForKey(RenderLayer layer, int key) {
	// 1. 优先从该 key 的空闲池中取
	auto& recycled = mLayerKeyRecycledOrders[layer][key];
	if (!recycled.empty()) {
		int sub = *recycled.begin();
		recycled.erase(recycled.begin());
		return sub;
	}

	// 2. 分配新区间内的本地索引
	int localIdx = mLayerKeyMaxLocalIdx[layer][key];  // 默认初始为 0
	if (localIdx < SUBORDER_PER_KEY) {
		int globalSub = key * SUBORDER_PER_KEY + localIdx;
		mLayerKeyMaxLocalIdx[layer][key] = localIdx + 1;
		return globalSub;
	}

	// 3. 超出区间上限，尝试再次检查回收池
	if (!recycled.empty()) {
		int sub = *recycled.begin();
		recycled.erase(recycled.begin());
		return sub;
	}

	// 4. 极端情况：重置该 key 的区间（清空回收池，从 0 重新开始）
	ResetKeyLayer(layer, key);
	int globalSub = key * SUBORDER_PER_KEY;  // 从 0 开始
	mLayerKeyMaxLocalIdx[layer][key] = 1;    // 下一个可用本地索引为 1
	return globalSub;
}

void GameObjectManager::ResetKeyLayer(RenderLayer layer, int key) {
	mLayerKeyMaxLocalIdx[layer][key] = 0;
	mLayerKeyRecycledOrders[layer][key].clear();
}

void GameObjectManager::ResetLayer(RenderLayer layer) {
	mLayerMaxSubOrder[layer] = 0;
	mLayerRecycledOrders[layer].clear();
}
