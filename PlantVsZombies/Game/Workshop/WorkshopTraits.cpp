#include "WorkshopTraits.h"

namespace Workshop {
namespace {
constexpr std::array<TraitDefinition, TraitCount> kTraits{{
    {"火力强化", "植物与瓜弹炮台伤害＋10%/层", 0},
    {"根系强化", "植物最大生命＋10%/层，保持血量比例", 0},
    {"精密加工", "生产设备加工速度＋10%/层", 0},
    {"扩容电网", "电网容量＋10%/层", 0},
    {"高效采光", "集光器产阳＋10%/层", 0},
    {"火力超载", "植物与瓜弹炮台伤害额外＋50%", 1},
    {"工业革命", "加工速度额外＋60%，电网容量额外＋30%", 1},
    {"戴夫的奇迹", "植物与瓜弹炮台最终伤害×2", 2},
    {"永昼引擎", "集光产阳与电网最终容量×2", 2}
}};
}
const TraitDefinition& DescribeTrait(Trait trait) { return kTraits.at(static_cast<int>(trait)); }
int TraitLimit(Trait trait) { return DescribeTrait(trait).rarity == 0 ? CommonTraitLimit : 1; }
const char* TraitRarityName(int rarity) {
    constexpr std::array<const char*, 3> names{"普通", "史诗", "传奇"};
    return names.at(rarity);
}
TraitBonuses AggregateTraits(const std::array<int, TraitCount>& stacks) {
    auto count = [&](Trait trait) { return stacks[static_cast<int>(trait)]; };
    TraitBonuses result;
    result.damage = (1 + count(Trait::Firepower) * CommonTraitBonus + count(Trait::Overload) * OverloadDamageBonus)
        * (count(Trait::Miracle) ? LegendaryMultiplier : 1);
    result.health = 1 + count(Trait::Vitality) * CommonTraitBonus;
    result.production = 1 + count(Trait::Production) * CommonTraitBonus + count(Trait::Industry) * IndustryProductionBonus;
    result.power = (1 + count(Trait::Power) * CommonTraitBonus + count(Trait::Industry) * IndustryPowerBonus)
        * (count(Trait::EternalDay) ? LegendaryMultiplier : 1);
    result.sunshine = (1 + count(Trait::Sunshine) * CommonTraitBonus) * (count(Trait::EternalDay) ? LegendaryMultiplier : 1);
    return result;
}
}
