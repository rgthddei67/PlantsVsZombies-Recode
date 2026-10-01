#include "Game/Board/ColdStorageSkillRules.h"
#include "Graphics.h"
#include "ResourceKeys.h"

void ColdStorageSkillRules::DrawVoucher(Graphics* g, float x, float y, float width, float height,
    const glm::vec4& color)
{
    if (!g) return;
    const auto tint = [&](float r, float green, float b) {
        return glm::vec4(r * color.r / 255, green * color.g / 255, b * color.b / 255, color.a);
    };
    g->FillRect(x, y, width, height, tint(28, 85, 111));
    g->DrawRect(x, y, width, height, tint(165, 242, 255));
    g->DrawRect(x + width * 0.06f, y + height * 0.09f, width * 0.88f, height * 0.82f, tint(82, 174, 201));
    // 券边齿口与雪晶明确表示折扣工具；比例布局在卡槽小图和图鉴大图中保持一致。
    for (int i = 1; i < 5; ++i) {
        const float edgeY = y + height * i / 5;
        g->FillRect(x, edgeY - height * 0.025f, width * 0.08f, height * 0.05f, tint(165, 242, 255));
        g->FillRect(x + width * 0.92f, edgeY - height * 0.025f, width * 0.08f, height * 0.05f, tint(165, 242, 255));
    }
    const int fontSize = static_cast<int>(height * 0.47f);
    const auto font = ResourceKeys::Fonts::FONT_FZCQ;
    const float textWidth = g->MeasureTextWidth("50%", font, fontSize);
    g->DrawText("50%", font, fontSize, tint(219, 252, 255), x + (width - textWidth) / 2, y + height * 0.13f);
    const float centerX = x + width * 0.5f, crystalY = y + height * 0.75f;
    g->DrawLine(centerX - width * 0.16f, crystalY, centerX + width * 0.16f, crystalY, tint(165, 242, 255));
    g->DrawLine(centerX - width * 0.11f, crystalY - height * 0.10f, centerX + width * 0.11f, crystalY + height * 0.10f, tint(165, 242, 255));
    g->DrawLine(centerX + width * 0.11f, crystalY - height * 0.10f, centerX - width * 0.11f, crystalY + height * 0.10f, tint(165, 242, 255));
}
