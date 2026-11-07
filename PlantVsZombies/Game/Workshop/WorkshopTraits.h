#pragma once
#include <array>

namespace Workshop {
enum class Trait { Firepower, Vitality, Production, Power, Sunshine, Overload, Industry, Miracle, EternalDay, Count };
inline constexpr int TraitCount = static_cast<int>(Trait::Count);
inline constexpr std::array<int, 3> TraitChances{25, 4, 1}; // 每条困难挑战的绝对百分比，其余70%不出词条，无保底
inline constexpr int CommonTraitLimit = 5; // 普通同类累计层数上限，史诗和传奇每种只领一次
inline constexpr float CommonTraitBonus = .1f; // 普通每层增加10个百分点，各普通加成先相加
inline constexpr float OverloadDamageBonus = .5f; // 史诗火力超载增加50个百分点伤害
inline constexpr float IndustryProductionBonus = .6f, IndustryPowerBonus = .3f; // 史诗工业革命的加工、电容加成
inline constexpr float LegendaryMultiplier = 2; // 传奇在普通与史诗相加后乘算
struct TraitDefinition { const char* name; const char* description; int rarity; };
struct TraitBonuses { float damage = 1, health = 1, production = 1, power = 1, sunshine = 1; };
const TraitDefinition& DescribeTrait(Trait trait);
int TraitLimit(Trait trait);
const char* TraitRarityName(int rarity);
/** 只聚合本基地已领取的层数；待领奖结果不生效，零层必须全部返回1。 */
TraitBonuses AggregateTraits(const std::array<int, TraitCount>& stacks);
}
