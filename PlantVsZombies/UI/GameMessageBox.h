#pragma once
#ifndef _H_MESSAGEBOX_H
#define _H_MESSAGEBOX_H

#include "../Graphics.h"
#include "../ResourceKeys.h"
#include "Button.h"
#include "Slider.h"
#include <SDL2/SDL.h>
#include <string>
#include <functional>
#include <memory>
#include <vector>

class UIManager;

// 场景级模态面板。生命周期与控件注册均由创建它的 UIManager 管理，
// 不参与玩法对象的 GameObject 调度。
class GameMessageBox {
public:
	class Builder;
	enum class BackgroundMode {
		STANDARD_DIALOG,
		SOLID_PANEL,
		TEXTURE,
	};

	struct ButtonConfig {
		std::string text;
		Vector pos;
		Vector size;   // 大小，如果是Vector::zero就是按照NormalButton处理
		float fontsize;
		std::function<void()> callback;
		std::string texture;
		bool autoClose = true;             // 是否自动关闭
		bool enabled = true;               // false 时保留按钮外观，但不参与命中与点击
		bool initChecked = false;          // 仅 checkbox 有效：创建时的初始勾选态
		std::string tooltipText;           // 非空时，悬停该按钮显示说明
		Vector hitSize{ 0.0f, 0.0f };      // 非零时，以 pos 为左上角扩展独立命中区域
		std::string font = ResourceKeys::Fonts::FONT_FZCQ;
	};

	struct SliderConfig {
		Vector pos;
		Vector size;
		float min;
		float max;
		float initValue;    // 初始化的值
		std::function<void(float)> callback;
		bool integerOnly = false;   // true=滑块只能停在整数刻度上（省略则为普通连续滑块）
	};

	struct TextConfig {
		Vector pos;
		float size;
		std::string text;
		glm::vec4 color;
		std::string font = ResourceKeys::Fonts::FONT_FZCQ;
	};

	struct TooltipPanelConfig {
		float maxWidth = 0.0f; // 外框最大逻辑宽度；高度由换行后的内容决定
		float fontSize = 17.0f;
		glm::vec4 textColor{ 245, 214, 127, 255 };
	};

	GameMessageBox(UIManager* owner,
		const Vector& pos,
		const std::string& message,
		const std::vector<ButtonConfig>& buttons,
		const std::vector<SliderConfig>& sliders,
		const std::vector<TextConfig>& texts,
		const std::string& title,
		BackgroundMode backgroundMode,
		const std::string& backgroundImageKey,
		float scale,
		const Vector& explicitSize,
		const TooltipPanelConfig& tooltipPanel);   // explicitSize 仅用于 Panel/显式纹理；标准框由内容测量

	~GameMessageBox();

	void Draw(Graphics* g);

	void SetActive(bool active);
	bool IsActive() const { return m_active; }
	void Close();
	/** 返回当前悬停控件的说明；没有可见说明时返回空字符串。 */
	const std::string& GetHoveredTooltipText() const;
	/** 返回随鼠标并经屏幕边缘修正后的说明框左上角；无说明时返回零向量。 */
	Vector GetTooltipDrawPosition() const;
	/** 返回对话框的屏幕逻辑中心与当前自适应尺寸。 */
	Vector GetPosition() const { return m_position; }
	Vector GetSize() const { return m_size; }
	bool UsesAdaptiveStandardSkin() const { return m_backgroundMode == BackgroundMode::STANDARD_DIALOG; }
	size_t GetWrappedMessageLineCount() const { return m_messageLines.size(); }
	/** 按创建顺序返回自有按钮，供自动化在自适应布局后走真实点击路径。 */
	std::shared_ptr<Button> GetButton(size_t index) const {
		return index < m_buttons.size() ? m_buttons[index] : nullptr;
	}
	/** 返回原版标准对话框皮肤的资源完整性，供启动自检与 AutoTest 复用。 */
	static size_t GetStandardSkinRequiredTextureCount();
	static size_t GetLoadedStandardSkinTextureCount();

private:
	friend class UIManager;

	UIManager* m_owner = nullptr;   // 非拥有指针；UIManager::ClearAll 会在销毁前解除关联
	bool m_active = true;
	bool m_closeRequested = false;
	Vector m_position;
	float m_scale = 1.0f;
	Vector m_size;
	Vector m_explicitSize{ 0.0f, 0.0f };   // 非零时覆盖纹理尺寸，背景以 m_position 居中绘制
	std::string m_title;
	std::string m_message;
	BackgroundMode m_backgroundMode = BackgroundMode::STANDARD_DIALOG;
	std::string m_backgroundImageKey;
	std::vector<ButtonConfig> m_buttonConfigs;
	std::vector<SliderConfig> m_sliderConfigs;
	std::vector<TextConfig> m_textConfigs;
	TooltipPanelConfig m_tooltipPanel;
	mutable std::string m_tooltipLayoutText;
	mutable std::vector<std::string> m_tooltipLines;
	mutable Vector m_tooltipSize;
	mutable float m_tooltipLineHeight = 0.0f;
	mutable float m_tooltipRasterScale = 0.0f;

	std::vector<std::shared_ptr<Button>> m_buttons;
	std::vector<std::shared_ptr<Slider>> m_sliders;
	std::vector<std::string> m_messageLines;
	Vector m_titleDrawPosition;
	std::vector<Vector> m_messageLinePositions;

	glm::vec4 m_textColor = { 245, 214, 127, 255 };
	glm::vec4 m_titleColor = { 53, 191, 61, 255 };

	/** 注册本框按钮和滑块，绑定关闭回调与所属皮肤的文字样式。 */
	void InitializeControls();
	void DetachControls();
	bool IsCloseRequested() const { return m_closeRequested; }
	Vector GetBackgroundOriginalSize() const;
	/** 按标题、换行正文和按钮行测量标准框，并把自有按钮改为框内相对排布。 */
	void LayoutStandardDialog();
	/** 用原版上、中、下分件平铺标准对话框，边缘不随尺寸拉伸。 */
	void DrawStandardDialog(Graphics* g) const;
	/** 缓存当前说明的 UTF-8 换行与实测尺寸，供文字、背景和边缘定位共用。 */
	Vector GetTooltipDrawSize(const std::string& text) const;
};

// 流式构建器：把标准自适应框、纯色面板、专用纹理背景和 checkbox 规则
// 显式化为命名方法。终结方法 Show() 创建对象并返回 shared_ptr。
class GameMessageBox::Builder {
public:
	explicit Builder(const Vector& pos) : m_pos(pos) {}

	// —— 背景（不调用 = 原版分件自适应对话框）；后调覆盖先调 ——
	Builder& StandardDialog() {
		m_backgroundMode = BackgroundMode::STANDARD_DIALOG;
		m_bgKey.clear(); m_explicitSize = Vector(0.0f, 0.0f); return *this;
	}
	Builder& Panel(float w, float h) {                    // 纯色面板，尺寸 w×h，以 pos 居中
		m_backgroundMode = BackgroundMode::SOLID_PANEL;
		m_bgKey.clear(); m_explicitSize = Vector(w, h); return *this;
	}
	Builder& Background(const std::string& key) {         // 纹理，原始尺寸×scale
		m_backgroundMode = BackgroundMode::TEXTURE;
		m_bgKey = key; m_explicitSize = Vector(0.0f, 0.0f); return *this;
	}
	Builder& Background(const std::string& key, const Vector& size) {  // 纹理+显式尺寸居中
		m_backgroundMode = BackgroundMode::TEXTURE;
		m_bgKey = key; m_explicitSize = size; return *this;
	}

	Builder& Title(const std::string& t)   { m_title = t;   return *this; }
	Builder& Message(const std::string& m) { m_message = m; return *this; }

	Builder& Text(const Vector& pos, float fontSize, const std::string& text,
		const glm::vec4& color, const std::string& font = ResourceKeys::Fonts::FONT_FZCQ) {
		m_texts.push_back({ pos, fontSize, text, color, font });
		return *this;
	}

	Builder& Button(const std::string& text, const Vector& pos, const Vector& size,
		float fontSize, std::function<void()> cb,
		const std::string& texture = ResourceKeys::Textures::IMAGE_BUTTONSMALL,
		bool autoClose = true, bool enabled = true) {
		m_buttons.push_back({ text, pos, size, fontSize, std::move(cb), texture, autoClose, enabled, false });
		return *this;
	}

	Builder& Checkbox(const Vector& pos, const Vector& size,
		std::function<void()> cb, bool initChecked = false,
		const std::string& tooltipText = "", const Vector& hitSize = Vector(0.0f, 0.0f)) {
		m_buttons.push_back({ "", pos, size, 1.0f, std::move(cb),
			ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX0, false, true, initChecked,
			tooltipText, hitSize });
		return *this;
	}

	/** 配置仅在带说明的控件被悬停时绘制的顶层浮动说明框。 */
	Builder& TooltipPanel(float maxWidth, float fontSize,
		const glm::vec4& textColor = glm::vec4(245, 214, 127, 255)) {
		m_tooltipPanel = { maxWidth, fontSize, textColor };
		return *this;
	}

	Builder& Slider(const Vector& pos, const Vector& size, float minVal, float maxVal,
		float initValue, std::function<void(float)> cb, bool integerOnly = false) {
		m_sliders.push_back({ pos, size, minVal, maxVal, initValue, std::move(cb), integerOnly });
		return *this;
	}

	/** 在 Show 时覆盖本框 Text 标签及非标准皮肤按钮字体；标准皮肤按钮沿用皮肤字体。 */
	Builder& ControlFont(const std::string& font) { m_controlFont = font; return *this; }

	Builder& Scale(float s) { m_scale = s; return *this; }

	/** 应用本框字体配置并向当前场景 UIManager 注册新弹窗。 */
	std::shared_ptr<GameMessageBox> Show();

private:
	Vector m_pos;
	std::string m_title;
	std::string m_message;
	BackgroundMode m_backgroundMode = BackgroundMode::STANDARD_DIALOG;
	std::string m_bgKey;
	std::string m_controlFont;
	float m_scale = 1.0f;
	Vector m_explicitSize{ 0.0f, 0.0f };
	std::vector<GameMessageBox::ButtonConfig> m_buttons;
	std::vector<GameMessageBox::SliderConfig> m_sliders;
	std::vector<GameMessageBox::TextConfig>   m_texts;
	GameMessageBox::TooltipPanelConfig m_tooltipPanel;
};

#endif
