#include "WorkshopChallenges.h"
#include <format>

namespace Workshop {
namespace {
constexpr std::array<ChallengeDefinition, static_cast<int>(Challenge::Count)> kChallenges{{
    {"普通袭击", 0, 0, 0},
    {"双倍尸潮", 1, 0, 1, 2},
    {"厚甲来袭", 1, 0, 1, 1, 1.5f},
    {"疾行部队", 1, 0, 1, 1, 1, 1.5f},
    {"精锐集结", 1, 3, 1, 1, 1, 1, 2},
    {"供电吃紧", 1, 0, 1, 1, 1, 1, 1, .85f},
    {"阵地锁定", 1, 0, 1, 1, 1, 1, 1, 1, true},
    {"滴水不漏", 1, 0, 1, 1, 1, 1, 1, 1, false, true},
    {"三倍尸潮", 2, 0, 1, 3},
    {"钢铁军团", 2, 0, 1, 1, 2},
    {"闪电突袭", 2, 0, 1, 1, 1, 2},
    {"重甲尸潮", 2, 5, 2, 2, 1.5f},
    {"疾行尸潮", 2, 5, 2, 2, 1, 1.5f},
    {"装甲急行", 2, 5, 2, 1, 1.5f, 1.5f},
    {"断电精锐", 2, 5, 2, 1, 1, 1, 3, .7f},
    {"固守重围", 2, 5, 2, 1, 1.5f, 1, 1, 1, true},
    {"无漏尸潮", 2, 5, 2, 2, 1, 1, 1, 1, false, true},
    {"重甲闪击", 2, 10, 3, 2, 1.5f, 1.5f},
    {"孤城围攻", 2, 10, 3, 1, 1.5f, 1, 1, .7f, true},
    {"极限防线", 2, 10, 3, 2, 1, 1.5f, 1, 1, false, true}
}}; // 波次为已守住波数；数量保持总出兵时长，血量含全部护甲，power 是本波电网容量比例
}

const ChallengeDefinition& DescribeChallenge(Challenge challenge) {
    return kChallenges.at(static_cast<int>(challenge));
}

std::vector<std::string> ChallengeEffects(Challenge challenge) {
    const auto& c = DescribeChallenge(challenge);
    std::vector<std::string> result;
    if (c.count > 1) result.push_back(std::format("数量×{} · 出兵时长不变", c.count));
    if (c.health > 1) result.push_back(std::format("本体与全部护甲×{:g}", c.health));
    if (c.movement > 1) result.push_back(std::format("移动速度×{:g} · 攻击技能不加速", c.movement));
    if (c.eliteWeight > 1) result.push_back(std::format("已开放特殊僵尸抽取权重×{}", c.eliteWeight));
    if (c.power < 1) result.push_back(std::format("本波电网容量降至{:.0f}%", c.power * 100));
    if (c.lockFormation) result.emplace_back("本波禁止新种与原位改造，仍可修复");
    if (c.noBreach) result.emplace_back("本波不能漏怪，漏怪即失去奖励");
    return result;
}

std::vector<Challenge> ChallengeCandidates(int difficulty, int completedWaves) {
    std::vector<Challenge> result;
    for (int i = 1; i < static_cast<int>(Challenge::Count); ++i)
        if (kChallenges[i].difficulty == difficulty && kChallenges[i].unlockWave <= completedWaves)
            result.push_back(static_cast<Challenge>(i));
    return result;
}
}
