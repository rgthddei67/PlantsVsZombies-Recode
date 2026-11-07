#pragma once
#include <array>
#include <string>
#include <vector>

namespace Workshop {
// 编号写入任务与本波存档，只能在 Count 前追加；None 也用于旧基地迁移。
enum class Challenge { None, DoubleHorde, Armor, Runners, Veterans, PowerShortage, FixedFormation, NoBreach,
    TripleHorde, HeavyArmor, Blitz, ArmoredHorde, FastHorde, FastArmor, VeteranBlackout, LockedSiege,
    NoMistakes, ArmoredBlitz, FactorySiege, FlawlessBlitz, Count };

struct ChallengeDefinition {
    const char* name;
    int difficulty, unlockWave, mechanisms;
    int count = 1;
    float health = 1, movement = 1;
    int eliteWeight = 1;
    float power = 1;
    bool lockFormation = false, noBreach = false;
};

/** 委托和波次使用同一份倍率；移动增益不进入攻击动画或技能时钟。 */
const ChallengeDefinition& DescribeChallenge(Challenge challenge);
std::vector<std::string> ChallengeEffects(Challenge challenge);
std::vector<Challenge> ChallengeCandidates(int difficulty, int completedWaves);
}
