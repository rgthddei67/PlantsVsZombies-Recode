#pragma once

#include <glm/vec4.hpp>
class Graphics;

namespace ColdStorageSkillRules {
/** 绘制技能券图标；卡槽与图鉴复用原生图形，不创建植物或 Animator。 */
void DrawVoucher(Graphics* g, float x, float y, float width, float height,
    const glm::vec4& color = glm::vec4(255.0f));
}
