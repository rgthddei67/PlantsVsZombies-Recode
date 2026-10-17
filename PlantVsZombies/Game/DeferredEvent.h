#pragma once
#ifndef _DEFERRED_EVENT_H
#define _DEFERRED_EVENT_H

#include <cstddef>
#include <cstdint>

class GameObject;
#include <new>
#include <type_traits>
#include <utility>

/**
 * @brief 只容纳一个指针大小小回调的无堆分配包装。
 * @details 当前帧事件只允许无捕获或仅捕获 this 的可平凡析构回调；复制时在内联存储中重建对象。
 */
class InlineFrameCallback {
private:
	static constexpr std::size_t kStorageSize = sizeof(void*);
	using InvokeFn = void (*)(const void*);
	using CopyFn = void (*)(void*, const void*);

	alignas(void*) unsigned char mStorage[kStorageSize]{};
	InvokeFn mInvoke = nullptr;
	CopyFn mCopy = nullptr;

	template<typename Callback>
	static void Invoke(const void* storage)
	{
		(*std::launder(reinterpret_cast<const Callback*>(storage)))();
	}

	template<typename Callback>
	static void Copy(void* destination, const void* source)
	{
		new (destination) Callback(
			*std::launder(reinterpret_cast<const Callback*>(source)));
	}

	void CopyFrom(const InlineFrameCallback& other) noexcept
	{
		mInvoke = other.mInvoke;
		mCopy = other.mCopy;
		if (mCopy) mCopy(mStorage, other.mStorage);
	}

public:
	InlineFrameCallback() = default;

	template<typename Callback,
		typename Stored = std::decay_t<Callback>,
		std::enable_if_t<!std::is_same_v<Stored, InlineFrameCallback>, int> = 0>
	InlineFrameCallback(Callback&& callback) noexcept
	{
		static_assert(sizeof(Stored) <= kStorageSize,
			"frame callbacks may capture at most one pointer");
		static_assert(alignof(Stored) <= alignof(void*),
			"frame callback alignment exceeds inline storage");
		static_assert(std::is_trivially_destructible_v<Stored>,
			"frame callbacks must be trivially destructible");
		static_assert(std::is_nothrow_copy_constructible_v<Stored>,
			"frame callbacks must be nothrow copy constructible");
		static_assert(std::is_nothrow_constructible_v<Stored, Callback&&>,
			"frame callbacks must be nothrow constructible");
		static_assert(std::is_invocable_r_v<void, const Stored&>,
			"frame callbacks must be const-invocable with no arguments");

		new (mStorage) Stored(std::forward<Callback>(callback));
		mInvoke = &Invoke<Stored>;
		mCopy = &Copy<Stored>;
	}

	InlineFrameCallback(const InlineFrameCallback& other) noexcept
	{
		CopyFrom(other);
	}

	InlineFrameCallback(InlineFrameCallback&& other) noexcept
	{
		CopyFrom(other);
	}

	InlineFrameCallback& operator=(const InlineFrameCallback& other) noexcept
	{
		if (this != &other) CopyFrom(other);
		return *this;
	}

	InlineFrameCallback& operator=(InlineFrameCallback&& other) noexcept
	{
		if (this != &other) CopyFrom(other);
		return *this;
	}

	explicit operator bool() const { return mInvoke != nullptr; }

	void operator()() const
	{
		if (mInvoke) mInvoke(mStorage);
	}
};

static_assert(sizeof(InlineFrameCallback) == sizeof(void*) * 3,
	"InlineFrameCallback should remain three pointers wide");

// 阶段二并行：worker 内推进 Animator 时遇到帧事件，把内联 callback 拷贝入此结构；
// 主线程串行 drain 时依次 invoke cb，全程不触发 std::function 堆分配。
struct DeferredEvent {
	InlineFrameCallback cb;
	const GameObject* owner = nullptr; // GOM 在本地预推进后标记宿主，不拥有对象
	std::uint64_t activationGeneration = 0; // 旧激活周期的事件不能作用于复用后的池槽
};

#endif
