#include "Slider.h"
#include "../ResourceManager.h"
#include "../CursorManager.h"
#include "../Logger.h"
#include <algorithm>
#include <cmath>

Slider::Slider(Vector createPosition, Vector sliderSize,
	float minVal, float maxVal, float initialValue)
	: position(createPosition), size(sliderSize),
	minValue(minVal), maxValue(maxVal), currentValue(initialValue)
{
	// isDragging/dragStartPosition/dragStartValue/SliderSizeX/SliderSizeY 由头文件就地初始化
	currentValue = std::clamp(currentValue, minValue, maxValue);
}

void Slider::SetPosition(Vector pos)
{
	this->position = pos;
}

void Slider::SetSize(Vector size)
{
	this->size = size;
}

void Slider::SetValueRange(float min, float max)
{
	this->minValue = min;
	this->maxValue = max;
	this->currentValue = std::clamp(this->currentValue, minValue, maxValue);
}

void Slider::SetValue(float value)
{
	float oldValue = this->currentValue;
	float newValue = std::clamp(value, minValue, maxValue);
	if (integerOnly)
		newValue = std::round(newValue);   // 吸附到最近的整数刻度
	this->currentValue = newValue;

	if (oldValue != this->currentValue && this->onChangeCallback)
	{
		this->onChangeCallback(this->currentValue);
	}
}

void Slider::SetIntegerOnly(bool enabled)
{
	this->integerOnly = enabled;
	// 立即把当前值吸附到整数刻度（纯配置，不触发回调）
	if (this->integerOnly)
		this->currentValue = std::round(std::clamp(this->currentValue, minValue, maxValue));
}

void Slider::SetDrag(bool canDrag)
{
	this->canDrag = canDrag;
	if (!canDrag) isDragging = false;
}

void Slider::SetImageKeys(const std::string& background, const std::string& knob)
{
	this->backgroundImageKey = background;
	this->knobImageKey = knob;
}

void Slider::SetChangeCallBack(std::function<void(float)> callback)
{
	this->onChangeCallback = callback;
}

void Slider::ProcessMouseEvent(InputHandler* input)
{
	if (!input || !canDrag || mModalInputBlocked) return;

	Vector mousePos = input->GetMousePosition();

	// 只在按下边沿捕获；按住时反复重设起点会吞掉慢速拖动的小位移。
	if (input->IsMouseButtonPressed(SDL_BUTTON_LEFT))
	{
		const bool onKnob = KnobContainsPoint(mousePos);
		if (onKnob || BackgroundContainsPoint(mousePos))
		{
			if (!onKnob) SetValue(CalculateValueFromX(mousePos.x));
			isDragging = true;
			dragStartPosition = mousePos;
			dragStartValue = currentValue;
		}
	}
	if (!input->IsMouseButtonDown(SDL_BUTTON_LEFT))
	{
		isDragging = false;
	}
}

void Slider::Update(InputHandler* input)
{
	if (!input || !canDrag || mModalInputBlocked) return;

	Vector mousePos = input->GetMousePosition();

	if (BackgroundContainsPoint(mousePos) && canDrag)
	{
		CursorManager::GetInstance().IncrementHoverCount();
	}

	if (isDragging)
	{
		float deltaX = mousePos.x - dragStartPosition.x;

		float pixelsPerValue = size.x / (maxValue - minValue);
		float valueDelta = deltaX / pixelsPerValue;

		SetValue(dragStartValue + valueDelta);
	}
}

void Slider::Draw(Graphics* g) const
{
	ResourceManager& resourceManager = ResourceManager::GetInstance();

	if (!backgroundImageKey.empty() && resourceManager.HasTexture(backgroundImageKey))
	{
		const Texture* texture = resourceManager.GetTexture(backgroundImageKey);
		if (texture != nullptr)
		{
			Vector newPosition = g->LogicalToWorld(position.x, position.y);
			g->DrawTexture(texture, newPosition.x, newPosition.y, size.x, size.y);
		}
	}
	else
	{
		Vector newPosition = g->LogicalToWorld(position.x, position.y);
		g->FillRect(newPosition.x, newPosition.y, size.x, size.y);
		g->DrawRect(newPosition.x, newPosition.y, size.x, size.y);
	}

	float knobX = CalculateKnobXFromValue();
	float knobY = position.y + size.y / 2;

	Vector knobSize(static_cast<float>(SliderSizeX), static_cast<float>(SliderSizeY));
	Vector knobPosition(knobX - knobSize.x / 2, knobY - knobSize.y / 2);

	if (!knobImageKey.empty() && resourceManager.HasTexture(knobImageKey))
	{
		const Texture* texture2 = resourceManager.GetTexture(knobImageKey);
		if (texture2 != nullptr)
		{
			Vector newPosition = g->LogicalToWorld(knobPosition.x, knobPosition.y);
			g->DrawTexture(texture2, newPosition.x, newPosition.y, knobSize.x, knobSize.y);
		}
	}
	else
	{
		LOG_WARN("UI") << "Slider: 没有合适的绘制图片";
	}
}

bool Slider::IsDragging() const
{
	return isDragging;
}

float Slider::GetValue() const
{
	return this->currentValue;
}

float Slider::GetNormalizedValue() const
{
	return (currentValue - minValue) / (maxValue - minValue);
}

bool Slider::KnobContainsPoint(Vector point) const
{
	float knobX = CalculateKnobXFromValue();
	float knobY = position.y + size.y / 2;
	Vector knobSize(static_cast<float>(SliderSizeX), static_cast<float>(SliderSizeY));
	Vector knobPosition(knobX - knobSize.x / 2, knobY - knobSize.y / 2);

	return point.x >= knobPosition.x && point.x <= knobPosition.x + knobSize.x &&
		point.y >= knobPosition.y && point.y <= knobPosition.y + knobSize.y;
}

bool Slider::BackgroundContainsPoint(Vector point) const
{
	return point.x >= position.x && point.x <= position.x + size.x &&
		point.y >= position.y && point.y <= position.y + size.y;
}

float Slider::CalculateValueFromX(float x) const
{
	float normalizedX = (x - position.x) / size.x;
	normalizedX = std::clamp(normalizedX, 0.0f, 1.0f);
	return minValue + normalizedX * (maxValue - minValue);
}

void Slider::ForceResetState()
{
	isDragging = false;
}

float Slider::CalculateKnobXFromValue() const {
	float normalizedValue = (currentValue - minValue) / (maxValue - minValue);
	return position.x + normalizedValue * size.x;
}
