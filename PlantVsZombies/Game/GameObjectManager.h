#pragma once
#ifndef _GAMEOBJECTMANAGER_H
#define _GAMEOBJECTMANAGER_H

#include <vector>
#include <memory>
#include <iostream>
#include <algorithm>
#include <map>
#include <set>
#include <thread>
#include <functional>
#include "GameObject.h"
#include "ThreadPool.h"
#include "ObjectPool/BulletPool.h"
#include "DeferredEvent.h"

const int SUBORDER_PER_KEY = 1000;  // 每个key最多同时存在的顺序数量

class GameObjectManager {
private:
	// 每个图层有自己的回收池（全局）
	std::map<RenderLayer, std::set<int>> mLayerRecycledOrders;
	// 每个图层当前的最大子顺序
	std::map<RenderLayer, int> mLayerMaxSubOrder;

	// 按排序键（如行号）管理的池
	std::map<RenderLayer, std::map<int, std::set<int>>> mLayerKeyRecycledOrders;  // 空闲子顺序（全局值）
	std::map<RenderLayer, std::map<int, int>> mLayerKeyMaxLocalIdx;  // 当前已分配的本地索引（0 ~ SUBORDER_PER_KEY-1）

	std::vector<std::shared_ptr<GameObject>> mGameObjects;       // 已经有的游戏对象
	std::vector<std::shared_ptr<GameObject>> mObjectsToAdd;      // 待添加的游戏对象
	std::vector<std::shared_ptr<GameObject>> mObjectsToRemove;   // 待删除的游戏对象

	std::unique_ptr<ThreadPool> mThreadPool;
	bool mSortDirty = true;
	uint64_t mNextRenderSequence = 1;

	// 主体（< LAYER_UI）绘制完、UI GameObject 绘制前的注入点（主线程串行调用）。
	// 用于世界粒子、天气覆盖层和 Scene UI 贴图的有序合成，避免依赖具体子系统头文件。
	std::function<void()> mPreOverlayHook;

	std::vector<std::vector<DeferredEvent>> mDeferredEventBuffers;  // size = numWorkers，跨帧 capacity 复用

	struct alignas(64) DrawWorkerProfileSample {
		double elapsedMs = 0.0;
		size_t activeObjects = 0;
	};
	// 只在 -Profile 下写入；每个 worker 独占一个槽位，Dispatch 完成后由主线程汇总。
	std::vector<DrawWorkerProfileSample> mDrawWorkerProfileSamples;

	// 对象池
	std::unique_ptr<BulletPool> mBulletPool;

public:
	static GameObjectManager& GetInstance() {
		static GameObjectManager instance;
		return instance;
	}

	GameObjectManager();

	// 创建游戏对象 (塞入mObjectsToAdd，在Update时执行Start)
	// 返回原始指针：所有权在 mObjectsToAdd / mGameObjects 中，调用方仅作非所有 view 使用
	template<typename T, typename... Args>
	T* CreateGameObject(RenderLayer layer, Args&&... args) {
		return CreateGameObjectAsShared<T>(layer, std::forward<Args>(args)...).get();
	}

	// 立即创建游戏对象并启动（立刻调用Start 并且塞入mGameObjects 而不是mObjectsToAdd)
	// 返回原始指针：所有权在 mGameObjects 中
	template<typename T, typename... Args>
	T* CreateGameObjectImmediate(RenderLayer layer, Args&&... args) {
		return CreateGameObjectImmediateAsShared<T>(layer, std::forward<Args>(args)...).get();
	}

	// shared_ptr 版本：供 EntityRegistry 等弱引用登记，或由对象池共享运行时所有权
	template<typename T, typename... Args>
	std::shared_ptr<T> CreateGameObjectAsShared(RenderLayer layer, Args&&... args) {
		static_assert(std::is_base_of<GameObject, T>::value, "T must be a GameObject");
		auto obj = std::make_shared<T>(std::forward<Args>(args)...);
		obj->SetLayer(layer);
		AssignRenderOrder(obj.get(), layer);
		mObjectsToAdd.push_back(obj);
		return obj;
	}

	template<typename T, typename... Args>
	std::shared_ptr<T> CreateGameObjectImmediateAsShared(RenderLayer layer, Args&&... args) {
		static_assert(std::is_base_of<GameObject, T>::value, "T must be a GameObject");
		auto obj = std::make_shared<T>(std::forward<Args>(args)...);
		obj->SetLayer(layer);
		AssignRenderOrder(obj.get(), layer);
		mGameObjects.push_back(obj);
		mSortDirty = true;   // 直接加入 mGameObjects，需要重新排序
		obj->Start();
		return obj;
	}

	// 销毁游戏对象
	void DestroyGameObject(std::shared_ptr<GameObject> obj);

	// 裸指针重载：用于子类内部调用 DestroyGameObject(this)，内部线性扫描还原 shared_ptr
	void DestroyGameObject(GameObject* raw);

	// 销毁全部游戏对象
	void DestroyAllGameObjects();

	// 更新
	void Update();

	// 绘制所有GameObject对象
	void DrawAll(Graphics* g);

	// 设置主体与 UI GameObject 之间的绘制注入点。
	void SetPreOverlayHook(std::function<void()> hook) { mPreOverlayHook = std::move(hook); }

	// 查找在gameObjects中的符合条件游戏对象 (根据tag标签)
	std::vector<std::shared_ptr<GameObject>> FindGameObjectsWithTag(const std::string& tag);

	// 查找在gameObjects中的第一个符合条件游戏对象 (根据tag标签)
	std::shared_ptr<GameObject> FindGameObjectWithTag(const std::string& tag);

	// 获取gameObjects引用
	const std::vector<std::shared_ptr<GameObject>>& GetAllGameObjects() const { return mGameObjects; }

	// 清空所有对象
	void ClearAll();

	// 获取对象池
	BulletPool* GetBulletPool() { return mBulletPool.get(); }

	// 打印对象池统计信息
	void PrintPoolStats() const;

	// 初始化所有图层
	void ResetAllLayers();

	/** @brief 回收对象实际持有的旧号，再按当前层/行分配；未分配对象不会回收默认显示值。 */
	void AssignRenderOrder(GameObject* gameObject, RenderLayer layer);
	void MarkRenderOrderDirty() { mSortDirty = true; }
	/** @brief 同格层次重排时同时交换显示顺序与分配凭据，避免释放仍被另一层使用的号。 */
	void SwapRenderOrders(GameObject* first, GameObject* second);
	/**
	 * @brief 对象的行等排序键变化后，回收旧区间并在新键区间重新分配绘制号。
	 * @param previousKey 变化前的排序键；当前键由 gameObject 重新读取。
	 */
	void RefreshRenderOrderForSortingKey(GameObject* gameObject, int previousKey);

private:
	// 只回收对象持有的分配凭据，并立即作废以防重复回收。
	void ReleaseRenderOrder(GameObject* gameObject);
	void RecycleRenderOrder(int renderOrder, RenderLayer layer, int key);
	// 只按对象当前 layer/key 分配新绘制号；调用方负责先回收旧号。
	void AssignNewRenderOrder(GameObject* gameObject, RenderLayer layer);

	// 获取图层内的下一个可用子顺序
	int GetNextSubOrder(RenderLayer layer);

	// 按key分配下一个可用子顺序
	int GetNextSubOrderForKey(RenderLayer layer, int key);

	void ResetKeyLayer(RenderLayer layer, int key);

	// 重置指定图层
	void ResetLayer(RenderLayer layer);
};

#endif
