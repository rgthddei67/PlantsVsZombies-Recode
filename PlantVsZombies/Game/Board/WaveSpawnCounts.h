#pragma once
#include "Game/Zombie/ZombieType.h"
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <algorithm>
#include <cassert>

/** Board 独占的每波累计台账；直造/读档实体不记账，同时名额另查实体和在途事务。 */
class WaveSpawnCounts {
public:
    int Get(ZombieType type) const { return mCounts[Index(type)]; }
    void Record(ZombieType type) { ++mCounts[Index(type)]; }
    /** 仅撤销本次未成功提交的预留；死亡、魅惑和已提交延迟出生不得返还累计名额。 */
    void CancelReservation(ZombieType type) { auto& count = mCounts[Index(type)]; count = std::max(0, count - 1); }
    void Reset() { mCounts.fill(0); }
    /** 调用方先按当前关卡规则夹紧；用于组合教程状态的既有恢复入口。 */
    void RestoreCount(ZombieType type, int count) { mCounts[Index(type)] = std::max(0, count); }
    /** 沿用各类型既有存档键，集中覆盖全部计数，不改变 schema。 */
    void Save(nlohmann::json& document) const;
    /** 缺失字段归零，非法计数按原关卡/无尽恢复上限夹紧。 */
    void Load(const nlohmann::json& document, int level, bool survival);
private:
    static std::size_t Index(ZombieType type) {
        assert(static_cast<int>(type) >= 0 && type < ZombieType::NUM_ZOMBIE_TYPES);
        return static_cast<std::size_t>(type);
    }
    std::array<int, static_cast<std::size_t>(ZombieType::NUM_ZOMBIE_TYPES)> mCounts{};
};
