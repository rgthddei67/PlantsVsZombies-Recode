#pragma once

/** 锅炉实体与候选推演共用的能力参数；阶段和扣款仍由各自宿主推进。 */
namespace BoilerRules {
constexpr int kHealth = 1000; // 本体生命，无独立防具
constexpr int kOverdriveCost = 5; // 超频提交时消耗敌方公共冰块
constexpr float kTriggerCells = 4.0f; // 前方本行可啃食植物的触发距离，格
constexpr float kPreheat = 2.0f; // 预热游戏秒数，硬控暂停前摇
constexpr float kOverdrive = 8.0f; // 超频游戏秒数，硬控不暂停倒计时
constexpr float kVenting = 5.0f; // 泄压游戏秒数，禁止啃食
constexpr float kRetry = 1.0f; // 缺冰或被打断后的重试间隔，游戏秒
constexpr float kOverdriveSpeed = 5.0f; // 超频移动与啃食频率倍率
constexpr float kOverdriveDamage = 2.0f; // 超频每口伤害倍率
constexpr float kVentingSpeed = 0.5f; // 泄压移动倍率
}
