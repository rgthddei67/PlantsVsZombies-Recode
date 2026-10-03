#pragma once

#include <algorithm>
#include <cstdint>

/** 所有指挥官场地共用的同时部署容量；只扩大可选空间，不要求填满或花光资金。 */
namespace ColdStorageDeploymentRules {
inline constexpr int BaseCapacity = 64; // 低资本时敌对在场与已付款队列合计上限
inline constexpr int MaximumCapacity = 192; // 高资本时敌对在场与已付款队列合计上限
inline constexpr int GrowthCapital = 2000; // 总资本超过此冰价后开始增加名额
inline constexpr int FullCapital = 8000; // 总资本达到此冰价时开放全部名额

/** 库存加付费存活/在途兵力原成交价；购买只转移资本，不会缩减本次剩余容量。 */
inline int Capacity(std::int64_t capital) {
    const auto growth = std::clamp<std::int64_t>(capital-GrowthCapital,0,FullCapital-GrowthCapital);
    return BaseCapacity+static_cast<int>(growth*(MaximumCapacity-BaseCapacity)/(FullCapital-GrowthCapital));
}
}
