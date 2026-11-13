#pragma once
#ifndef _BUTTON_H
#define _BUTTON_H
#include "../GameApp.h"
#include "../Graphics.h"
#include <functional>
#include <string>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

class ResourceManager;

class Button
{
private:
	Vector position = Vector::zero();
	Vector size = Vector(40, 40);
	bool isHovered = false;
	bool isPressed = false;
	bool isChecked = false;
	bool isCheckbox = false;
	bool canClick = true;

	std::string normalImageKey = ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX0;
	std::string hoverImageKey = ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX0;
	std::string pressedImageKey = ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX0;
	std::string checkedImageKey = ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX1;

	std::string text = "";
	std::string fontName = ResourceKeys::Fonts::FONT_FZCQ;
	int fontSize = 17;
	glm::vec4 textColor = glm::vec4(0.0f, 0.0f, 0.0f, 255.0f);
	glm::vec4 hoverTextColor = glm::vec4(255.0f, 255.0f, 255.0f, 255.0f);

	std::function<void(bool isChecked)> onClickCallback = nullptr;
	static std::string s_defaultFontPath;
	bool m_mousePressedThisFrame = false;
	bool m_mouseReleasedThisFrame = false;
	bool m_wasMouseDown = false;

	bool mEnabled = true;
	bool mModalInputBlocked = false;
	bool m_skipDraw = false;
	float mImageRotationDegrees = 0.0f;
	bool mHasCustomHitBounds = false;
	Vector mHitPosition = Vector::zero();
	Vector mHitSize = Vector::zero();

public:
	Button(Vector createPosition = Vector::zero(), Vector btnSize = Vector(40, 40));
	static void SetDefaultFontPath(const std::string& path);
	static std::string GetDefaultFontPath();

	/** 仅在控件允许输入时采集本帧鼠标按下/释放边沿。 */
	void ProcessMouseEvent(InputHandler* input);
	void ResetFrameState();

	void SetPosition(Vector pos);
	void SetSize(Vector size);
	/** 返回逻辑坐标下的绘制矩形，与自定义命中区域无关。 */
	SDL_FRect GetDrawBounds() const { return {position.x, position.y, size.x, size.y}; }
	/** 设置独立于绘制矩形的命中区域，适合让复选框连同整行标签一起响应。 */
	void SetHitBounds(Vector pos, Vector size);
	void SetText(const std::string& btnText, const std::string& font = ResourceKeys::Fonts::FONT_FZCQ, int size = 17);
	void SetTextColor(const glm::vec4& color);
	void SetHoverTextColor(const glm::vec4& color);
	void SetAsCheckbox(bool checkbox);
	void SetCanClick(bool canClick);
	/** 模态遮挡只阻止输入，不修改控件原有启用状态或外观。 */
	void SetModalInputBlocked(bool blocked) { mModalInputBlocked = blocked; if (blocked) { ForceResetHoverState(); ResetFrameState(); } }
	void SetEnabled(bool enabled) { this->mEnabled = enabled; }
	bool IsEnabled() const { return mEnabled; }
	bool IsCheckBox() const { return this->isCheckbox; }
	// 跳过自己按钮的绘制，让别的玩意去绘制
	void SetSkipDraw(bool skip) { m_skipDraw = skip; }
	bool IsSkipDraw() const { return m_skipDraw; }
	/** 设置按钮图片绕目标矩形中心旋转的角度；命中框保持轴对齐。 */
	void SetImageRotationDegrees(float degrees) { mImageRotationDegrees = degrees; }
	float GetImageRotationDegrees() const { return mImageRotationDegrees; }

	void ForceResetHoverState();

	void SetImageKeys(const std::string& normal, const std::string& hover = "", const std::string& pressed = "", const std::string& checked = "");

	void SetClickCallBack(std::function<void(bool)> callback);

	// hitAllowed=false 表示本帧命中仲裁判给了别的按钮：不 hover、不响应按下/释放
	/** 根据命中仲裁与模态门禁更新悬停、按压并触发点击回调。 */
	void Update(InputHandler* input, bool hitAllowed = true);
	void Draw(Graphics* g) const;

	// 命中仲裁用：可点击且判定框包含该点
	bool CanReceiveHit(Vector point) const { return !mModalInputBlocked && mEnabled && canClick && ContainsPoint(point); }
	Vector GetCenter() const {
		const Vector& hitPos = mHasCustomHitBounds ? mHitPosition : position;
		const Vector& hitSize = mHasCustomHitBounds ? mHitSize : size;
		return Vector(hitPos.x + hitSize.x * 0.5f, hitPos.y + hitSize.y * 0.5f);
	}

	bool IsHovered() const;
	bool IsPressed() const;
	bool IsChecked() const;
	void SetChecked(bool checked);

	bool ContainsPoint(Vector point) const;
};

#endif
