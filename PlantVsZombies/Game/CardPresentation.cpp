#include "CardPresentation.h"
#include "AudioSystem.h"
#include "ResourceKeys.h"
#include <algorithm>

namespace CardPresentation {
glm::vec4 Tint(State state) {
	return state == State::Ready ? glm::vec4(255) : glm::vec4(160,160,160,255);
}
void DrawNormalFrame(Graphics* g, const Texture* texture, float x, float y, const glm::vec4& tint) {
	if (texture) g->DrawTexture(texture,x,y,CARD_WIDTH,CARD_HEIGHT,0,tint);
}
void DrawCooldown(Graphics* g, float x, float y, float remainingFraction) {
	const int height = static_cast<int>(CARD_HEIGHT * std::clamp(remainingFraction,0.0f,1.0f));
	if (height > 0) g->FillRect(x,y,CARD_WIDTH,static_cast<float>(height),{0,0,0,64});
}
void DrawSelection(Graphics* g, float x, float y) {
	g->FillRect(x,y,CARD_WIDTH,CARD_HEIGHT,{0,0,0,64});
}
CachedText AcquireCost(Graphics& g, int cost) {
	return g.AcquireTextTexture(std::to_string(cost),ResourceKeys::Fonts::FONT_FZCQ,CostRasterSize,{0,0,0,255});
}
void DrawCost(Graphics* g, const CachedText& text, float x, float y) {
	g->DrawCachedText(text,x+5,y+51,CostDrawScale);
}
void PlayClick(bool accepted) {
	AudioSystem::PlaySound(accepted ? ResourceKeys::Sounds::SOUND_CLICKSEED : ResourceKeys::Sounds::SOUND_CLICKFAILED,0.5f);
}
}
