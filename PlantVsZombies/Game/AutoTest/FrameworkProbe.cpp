#include "FrameworkProbe.h"
#include "Game/GameObjectManager.h"
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <thread>

namespace {
    struct ProbeState {
        std::vector<int> trace;
        std::vector<int> draws;
        std::atomic<int> prepared{0};
        std::array<bool, 2> usedWorker{};
        std::thread::id mainThread = std::this_thread::get_id();
        bool barrierFailed = false;
    };

    // 探针只记录调度行为，不注册 Animator 帧事件，也不参与碰撞或玩法。
    class ProbeObject final : public GameObject {
        std::shared_ptr<ProbeState> mState;
        int mIndex;
    public:
        ProbeObject(std::shared_ptr<ProbeState> state, int index)
            : mState(std::move(state)), mIndex(index) {}
        /** 本地阶段记录实际 worker 命中，副作用留给统一提交阶段。 */
        void UpdateParallel(std::vector<DeferredEvent>& events) override {
            mState->usedWorker[mIndex] = std::this_thread::get_id() != mState->mainThread;
            ++mState->prepared;
            events.push_back({[this] {
                if (mState->prepared.load() != 2) mState->barrierFailed = true;
                mState->trace.push_back(mIndex);
            }});
        }
        /** 两个对象的事件必须先于任何玩法更新；最后一个对象关闭本步记录。 */
        void Update() override {
            mState->trace.push_back(10 + mIndex);
            if (mIndex == 1) mState->prepared = 0;
        }
        void Draw(Graphics*) override { mState->draws.push_back(mIndex); }
    };
    std::shared_ptr<ProbeState> state;
    std::vector<std::shared_ptr<GameObject>> objects;

    struct RecycleState {
        int events = 0, updates = 0;
        bool recycled = false, checked = false, failed = false;
    };
    std::shared_ptr<RecycleState> recycleState;

    class RecycledObject final : public GameObject {
        std::shared_ptr<RecycleState> mState;
    public:
        explicit RecycledObject(std::shared_ptr<RecycleState> state) : mState(std::move(state)) {}
        void UpdateParallel(std::vector<DeferredEvent>& events) override {
            events.push_back({[this] { ++mState->events; }});
        }
        void Update() override {
            if (mState->events == 0) mState->failed = true;
            ++mState->updates;
        }
    };

    class RecyclingObject final : public GameObject {
        std::shared_ptr<RecycleState> mState;
    public:
        GameObject* target = nullptr; // 探针集合共同持有，整个运行期间有效
        explicit RecyclingObject(std::shared_ptr<RecycleState> state) : mState(std::move(state)) {}
        /** 在目标旧回调已经入队之后复用槽位，模拟同一步内的对象池复用。 */
        void UpdateParallel(std::vector<DeferredEvent>& events) override {
            if (!mState->recycled) events.push_back({[this] {
                target->SetActive(false);
                target->SetActive(true);
                mState->recycled = true;
            }});
        }
        /** 旧事件必须跳过；下一步开始后新周期应正常收到事件和更新。 */
        void Update() override {
            if (!mState->checked) {
                mState->failed |= mState->events != 0 || mState->updates != 0;
                mState->checked = true;
            }
        }
    };

    /** 通过正式延迟移除入口清理，允许旧探针的回调存储安全跨过当前调用栈。 */
    void EndProbe() {
        for (auto& object : objects) {
            object->SetActive(false);
            GameObjectManager::GetInstance().DestroyGameObject(object);
        }
        objects.clear();
        state.reset();
        recycleState.reset();
    }
}

bool RunFrameworkProbe(const nlohmann::json& command, std::string& error) {
    const auto action = command.value("action", "check");
    auto& manager = GameObjectManager::GetInstance();
    if (action == "begin_recycle") {
        EndProbe();
        recycleState = std::make_shared<RecycleState>();
        auto first = manager.CreateGameObjectImmediateAsShared<RecyclingObject>(LAYER_GAME_OBJECT, recycleState);
        auto second = manager.CreateGameObjectImmediateAsShared<RecycledObject>(LAYER_GAME_OBJECT, recycleState);
        first->target = second.get();
        objects = {first, second};
        for (int i = 0; i < std::clamp(command.value("padding", 0), 0, 300); ++i)
            objects.push_back(manager.CreateGameObjectImmediateAsShared<GameObject>(LAYER_GAME_OBJECT));
        return true;
    }
    if (action == "check_recycle") {
        if (!recycleState || !recycleState->checked || recycleState->failed || recycleState->events < 1
            || recycleState->events != recycleState->updates) {
            error = "reused object received retired events/updates or missed its next activation";
            return false;
        }
        EndProbe();
        return true;
    }
    if (action == "begin") {
        EndProbe();
        state = std::make_shared<ProbeState>();
        for (int i = 0; i < 2; ++i) {
            auto object = manager.CreateGameObjectImmediateAsShared<ProbeObject>(LAYER_GAME_OBJECT, state, i);
            object->SetRenderOrder(LAYER_GAME_OBJECT + (1 - i));
            objects.push_back(object);
        }
        const int padding = std::clamp(command.value("padding", 0), 0, 300);
        for (int i = 0; i < padding; ++i)
            objects.push_back(manager.CreateGameObjectImmediateAsShared<GameObject>(LAYER_GAME_OBJECT));
        return true;
    }
    if (!state) { error = "framework probe has not started"; return false; }
    if (action == "end") { EndProbe(); return true; }
    if (action == "reverse") {
        objects[0]->SetRenderOrder(LAYER_GAME_OBJECT);
        objects[1]->SetRenderOrder(LAYER_GAME_OBJECT + 1);
        state->trace.clear(); state->draws.clear();
        return true;
    }
    const std::array<int, 4> expected{0, 1, 10, 11};
    if (state->barrierFailed || state->trace.size() < 8 || state->trace.size() % 4 != 0) {
        error = "missing complete update stages or pre-advance barrier"; return false;
    }
    for (std::size_t i = 0; i < state->trace.size(); ++i) {
        if (state->trace[i] != expected[i % 4]) {
            error = "draw sorting or worker threshold changed logic/event order"; return false;
        }
    }
    const bool worker = command.value("worker", false);
    if (state->usedWorker[0] != worker || state->usedWorker[1] != worker) {
        error = "requested serial/worker execution path was not exercised"; return false;
    }
    const bool reversed = command.value("reversed", false);
    if (state->draws.size() < 2 || state->draws.size() % 2 != 0) {
        error = "probe did not cross a complete rendered frame"; return false;
    }
    for (std::size_t i = 0; i < state->draws.size(); ++i) {
        if (state->draws[i] != (reversed ? static_cast<int>(i % 2) : 1 - static_cast<int>(i % 2))) {
            error = "draw view does not follow requested visual order"; return false;
        }
    }
    return true;
}
