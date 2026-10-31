#include "GameMessageBox.h"
#include "../ResourceManager.h"
#include "../GameApp.h"
#include "InputHandler.h"
#include "UIManager.h"
#include "../ResourceKeys.h"
#include "../Game/SceneManager.h"
#include "../Logger.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {
	constexpr Vector DEFAULT_SIZE(SCENE_WIDTH / 2, SCENE_HEIGHT / 2); // 自定义背景缺失时的兼容尺寸，单位：逻辑像素
	const int BASE_TITLE_FONT_SIZE = 26; // 默认标题字号，保持原版“大标题+小说明”层级
	const int BASE_MESSAGE_FONT_SIZE = 18; // 默认正文字号
	constexpr Vector LEGACY_TITLE_OFFSET = Vector(-70, -65); // 专用纹理背景的旧标题偏移，单位：逻辑像素
	constexpr Vector LEGACY_MESSAGE_OFFSET = Vector(-190, -25); // 专用纹理背景的旧正文偏移，单位：逻辑像素
	constexpr float kDialogHeaderOffset = 24.0f; // 原版九宫格主体为顶部骷髅装饰预留的高度，单位：素材像素
	constexpr float kDialogHeaderLift = 16.0f; // 骷髅顶盖相对主体向上越界的距离，单位：素材像素
	constexpr float kDialogMinimumWidth = 400.0f; // 短框宽度，避免骷髅与边框相对内容过大，单位：素材像素
	constexpr float kDialogMinimumHeight = 235.0f; // 短文框的原版基准高度，单位：素材像素
	constexpr float kDialogContentInsetLeft = 40.0f; // 正文左内边距，单位：素材像素
	constexpr float kDialogContentInsetRight = 46.0f; // 正文右内边距，单位：素材像素
	constexpr float kDialogContentTop = 60.0f; // 标题/正文区的最小顶部位置，单位：素材像素
	constexpr float kDialogTitleBodyGap = 4.0f; // 标题与正文间距，单位：素材像素
	constexpr float kDialogMessageLineGap = 4.0f; // 正文换行间距，单位：素材像素
	constexpr float kDialogContentButtonGap = 10.0f; // 文字块与按钮区的最小间距，单位：素材像素
	constexpr float kDialogContentBottomAnchor = 16.0f; // 文字区下边界相对底座顶部的原版锚点，单位：素材像素
	constexpr float kDialogButtonTopInset = 28.0f; // 按钮顶部相对底座分件顶部的偏移，单位：素材像素
	constexpr float kDialogButtonSideInset = 20.0f; // 双按钮行距石板左右边缘的留白，单位：素材像素
	constexpr float kDialogButtonGap = 24.0f; // 同行按钮间距，单位：素材像素
	constexpr float kDialogScreenMargin = 40.0f; // 自适应框与屏幕左右边缘的最小留白，单位：逻辑像素
	const glm::vec4 STANDARD_DIALOG_TEXT_COLOR(255, 230, 116, 255); // 参考中文弹窗的亮黄色字面
	constexpr float kTooltipCursorOffset = 18.0f; // 浮动说明框与鼠标热点的间距，单位：逻辑像素
	constexpr float kTooltipScreenMargin = 8.0f; // 浮动说明框距屏幕边缘的最小留白，单位：逻辑像素
	constexpr float kTooltipHorizontalPadding = 24.0f; // 浮动说明框文字左右内边距总和，单位：逻辑像素
	constexpr float kTooltipMinimumWidth = 120.0f; // 浮动说明框最小宽度，单位：逻辑像素
	constexpr float kTooltipVerticalPadding = 20.0f; // 说明文字上下内边距总和，单位：逻辑像素
	constexpr float kTooltipLineGap = 4.0f; // 多行说明的行间距，单位：逻辑像素

	/** 标准弹窗先画八方向黑边再画字面；复用文字缓存，不修改共享字体状态。 */
	void DrawStandardText(const std::string& text, const Vector& world,
		const glm::vec4& color, const std::string& font, int fontSize)
	{
		const float radius = std::max(1.0f, fontSize / 24.0f);
		for (int y = -1; y <= 1; ++y) {
			for (int x = -1; x <= 1; ++x) {
				if (x || y) GameAPP::GetInstance().DrawText(text,
					world + Vector(x * radius, y * radius), glm::vec4(0, 0, 0, 255), font, fontSize);
			}
		}
		GameAPP::GetInstance().DrawText(text, world, color, font, fontSize);
	}

	/** 返回皮肤分件的固定地址表；只对地址做常量求值，不读取资源字符串内容。 */
	constexpr const std::array<const std::string*, 10>& StandardDialogTextureKeys()
	{
		static constexpr std::array<const std::string*, 10> keys = {
			&ResourceKeys::Textures::IMAGE_DIALOG_TOPLEFT,
			&ResourceKeys::Textures::IMAGE_DIALOG_TOPMIDDLE,
			&ResourceKeys::Textures::IMAGE_DIALOG_TOPRIGHT,
			&ResourceKeys::Textures::IMAGE_DIALOG_CENTERLEFT,
			&ResourceKeys::Textures::IMAGE_DIALOG_CENTERMIDDLE,
			&ResourceKeys::Textures::IMAGE_DIALOG_CENTERRIGHT,
			&ResourceKeys::Textures::IMAGE_DIALOG_BOTTOMLEFT,
			&ResourceKeys::Textures::IMAGE_DIALOG_BOTTOMMIDDLE,
			&ResourceKeys::Textures::IMAGE_DIALOG_BOTTOMRIGHT,
			&ResourceKeys::Textures::IMAGE_DIALOG_HEADER,
		};
		return keys;
	}

	/** 取得一个 UTF-8 码点的字节结束；非法字节按单字节前进以保证布局可继续。 */
	size_t NextUtf8CodepointEnd(const std::string& text, size_t begin)
	{
		const unsigned char lead = static_cast<unsigned char>(text[begin]);
		size_t length = 1;
		if ((lead & 0xE0) == 0xC0) length = 2;
		else if ((lead & 0xF0) == 0xE0) length = 3;
		else if ((lead & 0xF8) == 0xF0) length = 4;
		return std::min(text.size(), begin + length);
	}

	/** 按字形实际宽度贪心换行，保留显式换行并不拆分 UTF-8 码点。 */
	std::vector<std::string> WrapUtf8Text(Graphics& graphics, const std::string& text,
		int fontSize, float maxWidth, const std::string& fontKey = ResourceKeys::Fonts::FONT_FZJT)
	{
		std::vector<std::string> lines;
		std::string current;
		for (size_t i = 0; i < text.size();) {
			if (text[i] == '\r') {
				++i;
				continue;
			}
			if (text[i] == '\n') {
				lines.push_back(current);
				current.clear();
				++i;
				continue;
			}

			const size_t end = NextUtf8CodepointEnd(text, i);
			const std::string codepoint = text.substr(i, end - i);
			const std::string candidate = current + codepoint;
			if (!current.empty() && graphics.MeasureTextWidth(candidate,
				fontKey, fontSize) > maxWidth) {
				lines.push_back(current);
				current = codepoint;
			}
			else {
				current = candidate;
			}
			i = end;
		}
		if (!current.empty() || lines.empty()) lines.push_back(current);
		return lines;
	}

	float MeasureLongestExplicitLine(Graphics& graphics, const std::string& text, int fontSize)
	{
		float width = 0.0f;
		size_t begin = 0;
		while (begin <= text.size()) {
			const size_t end = text.find('\n', begin);
			const std::string line = text.substr(begin,
				end == std::string::npos ? std::string::npos : end - begin);
			width = std::max(width, graphics.MeasureTextWidth(line,
				ResourceKeys::Fonts::FONT_FZJT, fontSize));
			if (end == std::string::npos) break;
			begin = end + 1;
		}
		return width;
	}

	void DrawLogicalTexture(Graphics* g, const Texture* texture,
		float x, float y, float width, float height)
	{
		const Vector world = g->LogicalToWorld(x, y);
		g->DrawTexture(texture, world.x, world.y, width, height);
	}

	/** 按原尺寸平铺一行纹理，末段只取源纹理的左侧区域，不拉伸边缘。 */
	void DrawTiledHorizontally(Graphics* g, const Texture* texture,
		float x, float y, float width, float sourceHeight, float scale)
	{
		float remaining = width;
		float drawX = x;
		while (remaining > 0.01f) {
			const float sourceWidth = std::min(static_cast<float>(texture->width), remaining / scale);
			const float drawWidth = sourceWidth * scale;
			const Vector world = g->LogicalToWorld(drawX, y);
			g->DrawTextureRegion(texture, 0.0f, 0.0f, sourceWidth, sourceHeight,
				world.x, world.y, drawWidth, sourceHeight * scale);
			drawX += drawWidth;
			remaining -= drawWidth;
		}
	}
}

GameMessageBox::GameMessageBox(UIManager* owner,
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
	const TooltipPanelConfig& tooltipPanel)
	: m_owner(owner)
	, m_position(pos)
	, m_scale(scale)
	, m_explicitSize(explicitSize)
	, m_title(title)
	, m_message(message)
	, m_backgroundMode(backgroundMode)
	, m_backgroundImageKey(backgroundImageKey)
	, m_buttonConfigs(buttons)
	, m_sliderConfigs(sliders)
	, m_textConfigs(texts)
	, m_tooltipPanel(tooltipPanel)
{
	if (m_backgroundMode == BackgroundMode::STANDARD_DIALOG) {
		LayoutStandardDialog();
	}
	else if (explicitSize.x > 0.0f && explicitSize.y > 0.0f) {
		m_size = explicitSize;
	}
	else {
		m_size = GetBackgroundOriginalSize() * scale;
	}
	InitializeControls();
}

GameMessageBox::~GameMessageBox() {
	DetachControls();
}

Vector GameMessageBox::GetBackgroundOriginalSize() const
{
	if (!m_backgroundImageKey.empty()) {
		auto& resMgr = ResourceManager::GetInstance();
		const Texture* tex = resMgr.GetTexture(m_backgroundImageKey);
		if (tex) {
			int w, h;
			w = tex->width;
			h = tex->height;
			return Vector(static_cast<float>(w), static_cast<float>(h));
		}
	}
	return DEFAULT_SIZE;
}

size_t GameMessageBox::GetStandardSkinRequiredTextureCount()
{
	return StandardDialogTextureKeys().size();
}

size_t GameMessageBox::GetLoadedStandardSkinTextureCount()
{
	size_t loaded = 0;
	auto& resources = ResourceManager::GetInstance();
	for (const std::string* key : StandardDialogTextureKeys()) {
		if (resources.GetTexture(*key, false)) ++loaded;
	}
	return loaded;
}

void GameMessageBox::LayoutStandardDialog()
{
	Graphics& graphics = GameAPP::GetInstance().GetGraphics();
	const float scale = std::max(0.01f, m_scale);
	const int titleFontSize = std::max(8, static_cast<int>(BASE_TITLE_FONT_SIZE * scale));
	const int messageFontSize = std::max(8, static_cast<int>(BASE_MESSAGE_FONT_SIZE * scale));
	const float horizontalInsets = (kDialogContentInsetLeft + kDialogContentInsetRight) * scale;
	const float maxDialogWidth = SCENE_WIDTH - kDialogScreenMargin * 2.0f;

	const Texture* topLeft = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_DIALOG_TOPLEFT, false);
	const Texture* topMiddle = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_DIALOG_TOPMIDDLE, false);
	const Texture* topRight = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_DIALOG_TOPRIGHT, false);
	const float minimumWidth = std::max(kDialogMinimumWidth * scale, (topLeft && topMiddle && topRight)
		? (topLeft->width + topMiddle->width + topRight->width) * scale
		: 320.0f * scale);

	float buttonRowWidth = 0.0f;
	for (const ButtonConfig& config : m_buttonConfigs) {
		buttonRowWidth += config.size.x * scale;
	}
	if (m_buttonConfigs.size() > 1) {
		buttonRowWidth += (m_buttonConfigs.size() - 1) * kDialogButtonGap * scale;
	}

	const float titleWidth = graphics.MeasureTextWidth(m_title,
		ResourceKeys::Fonts::FONT_FZJT, titleFontSize);
	const float messageWidth = MeasureLongestExplicitLine(graphics, m_message, messageFontSize);
	const float desiredWidth = std::max({ minimumWidth,
		titleWidth + horizontalInsets, messageWidth + horizontalInsets,
		buttonRowWidth + horizontalInsets });
	m_size.x = std::min(maxDialogWidth, desiredWidth);
	if (m_buttonConfigs.size() == 2) {
		// 原版双按钮会把底座可用宽度平分，不沿用旧固定贴图框的窄按钮宽度。
		const float buttonWidth = (m_size.x - 2.0f * kDialogButtonSideInset * scale
			- kDialogButtonGap * scale) / 2.0f;
		for (ButtonConfig& config : m_buttonConfigs) {
			config.size.x = buttonWidth / scale;
		}
		buttonRowWidth = buttonWidth * 2.0f + kDialogButtonGap * scale;
	}

	const float contentWidth = std::max(1.0f, m_size.x - horizontalInsets);
	m_messageLines = m_message.empty()
		? std::vector<std::string>{}
		: WrapUtf8Text(graphics, m_message, messageFontSize, contentWidth);

	const float titleHeight = m_title.empty() ? 0.0f
		: graphics.MeasureTextSize(m_title, ResourceKeys::Fonts::FONT_FZJT,
			titleFontSize).y;
	const float messageLineHeight = m_messageLines.empty() ? 0.0f
		: graphics.MeasureTextSize(m_messageLines.front(), ResourceKeys::Fonts::FONT_FZJT,
			messageFontSize).y;
	const float messageHeight = m_messageLines.empty() ? 0.0f
		: messageLineHeight * m_messageLines.size()
			+ kDialogMessageLineGap * scale * (m_messageLines.size() - 1);
	const float titleGap = (!m_title.empty() && !m_messageLines.empty())
		? kDialogTitleBodyGap * scale : 0.0f;
	const float contentHeight = titleHeight + titleGap + messageHeight;

	const Texture* bottomMiddle = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_DIALOG_BOTTOMMIDDLE, false);
	const float bottomHeight = (bottomMiddle ? bottomMiddle->height : 114.0f) * scale;
	const float requiredHeight = kDialogContentTop * scale + contentHeight
		+ kDialogContentButtonGap * scale + bottomHeight - kDialogContentBottomAnchor * scale;
	m_size.y = std::max(kDialogMinimumHeight * scale, requiredHeight);

	const float top = m_position.y - m_size.y / 2.0f;
	const float buttonY = top + m_size.y - bottomHeight + kDialogButtonTopInset * scale;
	float buttonX = m_position.x - buttonRowWidth / 2.0f;
	for (ButtonConfig& config : m_buttonConfigs) {
		config.size.y = std::max(config.size.y, 50.0f);
		config.fontsize = std::max(config.fontsize, 18.0f);
		config.pos = Vector(buttonX, buttonY);
		buttonX += config.size.x * scale + kDialogButtonGap * scale;
	}

	// 中文原版文字块靠暗槽上沿排列，短文多余空间留在正文下方；不受按钮位置影响。
	float drawY = top + kDialogContentTop * scale;
	if (!m_title.empty()) {
		m_titleDrawPosition = Vector(
			m_position.x - titleWidth / 2.0f, drawY);
		drawY += titleHeight + titleGap;
	}
	m_messageLinePositions.clear();
	for (const std::string& line : m_messageLines) {
		const float lineWidth = graphics.MeasureTextWidth(line,
			ResourceKeys::Fonts::FONT_FZJT, messageFontSize);
		m_messageLinePositions.emplace_back(m_position.x - lineWidth / 2.0f, drawY);
		drawY += messageLineHeight + kDialogMessageLineGap * scale;
	}
}

void GameMessageBox::InitializeControls()
{
	if (!m_owner) return;

	for (const auto& config : m_buttonConfigs) {
		Vector btnSize = config.size * m_scale;

		auto button = m_owner->CreateButton(config.pos, btnSize);

		int fontSize = static_cast<int>(config.fontsize * m_scale);
		if (fontSize < 8) fontSize = 8;

		if (config.texture == ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX0 ||
			config.texture == ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX1) {
			button->SetAsCheckbox(true);
			button->SetImageKeys
			(config.texture, config.texture, config.texture,
				ResourceKeys::Textures::IMAGE_OPTIONS_CHECKBOX1);
			button->SetChecked(config.initChecked);
		}
		else {
			button->SetTextColor(m_titleColor);
			button->SetHoverTextColor(m_titleColor);
			const bool standard = m_backgroundMode == BackgroundMode::STANDARD_DIALOG;
			button->SetText(config.text, standard ? ResourceKeys::Fonts::FONT_FZJT
				: config.font, fontSize);
			if (standard) {
				button->SetTextColor(glm::vec4(0, 235, 0, 255));
				button->SetHoverTextColor(glm::vec4(0, 255, 0, 255));
			}
			button->SetAsCheckbox(false);
			button->SetImageKeys
			(config.texture, config.texture, config.texture, config.texture);
		}

		// UIManager 会在 ButtonManager 完成本帧遍历后处理关闭，回调期间 this 始终有效。
		button->SetClickCallBack([this, config](bool) {
			if (config.callback) config.callback();
			if (config.autoClose) {
				Close();
			}
			});
		button->SetEnabled(config.enabled);
		if (config.hitSize.x > 0.0f && config.hitSize.y > 0.0f) {
			button->SetHitBounds(config.pos, config.hitSize * m_scale);
		}

		m_buttons.push_back(button);
		button->SetSkipDraw(true);
	}

	for (const auto& config : m_sliderConfigs) {
		auto slider = m_owner->CreateSlider
			(config.pos, config.size * m_scale, config.min, config.max, config.initValue);

		slider->SetIntegerOnly(config.integerOnly);

		slider->SetChangeCallBack([config](float value) {
			if (config.callback)
				config.callback(value);
			});

		m_sliders.push_back(slider);
		slider->SetSkipDraw(true);
	}
}

void GameMessageBox::DrawStandardDialog(Graphics* g) const
{
	auto& resources = ResourceManager::GetInstance();
	const Texture* topLeft = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_TOPLEFT, false);
	const Texture* topMiddle = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_TOPMIDDLE, false);
	const Texture* topRight = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_TOPRIGHT, false);
	const Texture* centerLeft = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_CENTERLEFT, false);
	const Texture* centerMiddle = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_CENTERMIDDLE, false);
	const Texture* centerRight = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_CENTERRIGHT, false);
	const Texture* bottomLeft = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_BOTTOMLEFT, false);
	const Texture* bottomMiddle = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_BOTTOMMIDDLE, false);
	const Texture* bottomRight = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_BOTTOMRIGHT, false);
	const Texture* header = resources.GetTexture(ResourceKeys::Textures::IMAGE_DIALOG_HEADER, false);
	if (!topLeft || !topMiddle || !topRight || !centerLeft || !centerMiddle || !centerRight
		|| !bottomLeft || !bottomMiddle || !bottomRight || !header) {
		LOG_ERROR("UI") << "GameMessageBox 原版标准皮肤资源不完整 ("
			<< GetLoadedStandardSkinTextureCount() << "/"
			<< GetStandardSkinRequiredTextureCount() << ")";
		return;
	}

	const float scale = std::max(0.01f, m_scale);
	const float left = m_position.x - m_size.x / 2.0f;
	const float top = m_position.y - m_size.y / 2.0f;
	const float bodyTop = top + kDialogHeaderOffset * scale;

	// 上下中段只平铺 middle 分件，两侧拐角始终保持素材原比例。
	DrawLogicalTexture(g, topLeft, left, bodyTop,
		topLeft->width * scale, topLeft->height * scale);
	DrawTiledHorizontally(g, topMiddle,
		left + topLeft->width * scale, bodyTop,
		m_size.x - (topLeft->width + topRight->width) * scale,
		static_cast<float>(topMiddle->height), scale);
	DrawLogicalTexture(g, topRight,
		left + m_size.x - topRight->width * scale, bodyTop,
		topRight->width * scale, topRight->height * scale);

	const float centerTop = bodyTop + topRight->height * scale;
	const float bottomTop = top + m_size.y - bottomMiddle->height * scale;
	// PC 原版的 centerRight 比上下右角少 3 列纯透明阴影。三段必须按左侧内沿
	// 对齐；若按各自宽度贴右对齐，中心暗槽会在接缝处向右凸出约 3 个素材像素。
	const float rightPieceLeft = left + m_size.x - topRight->width * scale;
	float remainingHeight = std::max(0.0f, bottomTop - centerTop);
	float rowY = centerTop;
	while (remainingHeight > 0.01f) {
		const float sourceHeight = std::min(
			static_cast<float>(centerLeft->height), remainingHeight / scale);
		const float drawHeight = sourceHeight * scale;
		Vector world = g->LogicalToWorld(left, rowY);
		g->DrawTextureRegion(centerLeft, 0.0f, 0.0f,
			static_cast<float>(centerLeft->width), sourceHeight,
			world.x, world.y, centerLeft->width * scale, drawHeight);
		DrawTiledHorizontally(g, centerMiddle,
			left + centerLeft->width * scale, rowY,
			rightPieceLeft - left - centerLeft->width * scale,
			sourceHeight, scale);
		world = g->LogicalToWorld(rightPieceLeft, rowY);
		g->DrawTextureRegion(centerRight, 0.0f, 0.0f,
			static_cast<float>(centerRight->width), sourceHeight,
			world.x, world.y, centerRight->width * scale, drawHeight);
		rowY += drawHeight;
		remainingHeight -= drawHeight;
	}

	DrawLogicalTexture(g, bottomLeft, left, bottomTop,
		bottomLeft->width * scale, bottomLeft->height * scale);
	DrawTiledHorizontally(g, bottomMiddle,
		left + bottomLeft->width * scale, bottomTop,
		m_size.x - (bottomLeft->width + bottomRight->width) * scale,
		static_cast<float>(bottomMiddle->height), scale);
	DrawLogicalTexture(g, bottomRight,
		left + m_size.x - bottomRight->width * scale, bottomTop,
		bottomRight->width * scale, bottomRight->height * scale);

	DrawLogicalTexture(g, header,
		left + (m_size.x - header->width * scale) / 2.0f,
		top - kDialogHeaderLift * scale,
		header->width * scale, header->height * scale);
}

void GameMessageBox::Draw(Graphics* g)
{
	if (!m_active) return;
	if (m_customDraw) { m_customDraw(g); return; }

	const bool autoSized = (m_explicitSize.x > 0.0f && m_explicitSize.y > 0.0f);
	if (m_backgroundMode == BackgroundMode::STANDARD_DIALOG) {
		DrawStandardDialog(g);
	}
	else if (m_backgroundMode == BackgroundMode::SOLID_PANEL && autoSized) {
		// 自动尺寸 + 无纹理 → 画纯色面板：FillRect 与 DrawTexture 同坐标约定，
		// 面板矩形恰为 [m_position±m_size/2]，与按内容算好的文字/按钮坐标严格对齐。
		const float left = m_position.x - m_size.x / 2.0f;
		const float top  = m_position.y - m_size.y / 2.0f;
		const float bw   = 3.0f;   // 边框宽
		Vector outer = g->LogicalToWorld(left, top);
		Vector inner = g->LogicalToWorld(left + bw, top + bw);
		g->FillRect(outer.x, outer.y, m_size.x, m_size.y, glm::vec4(150, 170, 110, 255));                       // 边框（暖石绿）
		g->FillRect(inner.x, inner.y, m_size.x - bw * 2.0f, m_size.y - bw * 2.0f, glm::vec4(40, 42, 34, 236));   // 主体（深色半透明）
	}
	else if (m_backgroundMode == BackgroundMode::TEXTURE && !m_backgroundImageKey.empty()) {
		auto& resMgr = ResourceManager::GetInstance();
		const Texture* tex = resMgr.GetTexture(m_backgroundImageKey);
		// 自动尺寸模式以 m_position 为中心绘制；否则沿用固定 (230,180) 偏移
		Vector topLeft = autoSized
			? Vector(m_position.x - m_size.x / 2.0f, m_position.y - m_size.y / 2.0f)
			: Vector(m_position.x - 230, m_position.y - 180);
		Vector pos = g->LogicalToWorld(topLeft.x, topLeft.y);
		g->DrawTexture(tex, pos.x, pos.y, m_size.x, m_size.y);
	}
	else {
		LOG_WARN("UI") << "GameMessageBox::Draw 没有合适的绘制图片";
	}

	if (m_backgroundMode == BackgroundMode::STANDARD_DIALOG) {
		const int titleFontSize = std::max(8, static_cast<int>(BASE_TITLE_FONT_SIZE * m_scale));
		const int messageFontSize = std::max(8, static_cast<int>(BASE_MESSAGE_FONT_SIZE * m_scale));
		if (!m_title.empty()) {
			const Vector world = g->LogicalToWorld(m_titleDrawPosition.x, m_titleDrawPosition.y);
			DrawStandardText(m_title, world, STANDARD_DIALOG_TEXT_COLOR,
				ResourceKeys::Fonts::FONT_FZJT, titleFontSize);
		}
		for (size_t i = 0; i < m_messageLines.size() && i < m_messageLinePositions.size(); ++i) {
			const Vector world = g->LogicalToWorld(
				m_messageLinePositions[i].x, m_messageLinePositions[i].y);
			DrawStandardText(m_messageLines[i], world,
				STANDARD_DIALOG_TEXT_COLOR, ResourceKeys::Fonts::FONT_FZJT, messageFontSize);
		}
	}
	else if (!m_title.empty()) {
		int fontSize = static_cast<int>(BASE_TITLE_FONT_SIZE * m_scale);
		if (fontSize < 8) fontSize = 8;
		Vector titlePos = m_position + Vector(10 * m_scale + LEGACY_TITLE_OFFSET.x, LEGACY_TITLE_OFFSET.y);
		Vector pos2 = g->LogicalToWorld(titlePos.x, titlePos.y);
		GameAPP::GetInstance().DrawText(m_title, pos2, m_titleColor,
			ResourceKeys::Fonts::FONT_FZCQ, fontSize);
	}

	if (m_backgroundMode != BackgroundMode::STANDARD_DIALOG && !m_message.empty()) {
		int fontSize = static_cast<int>(BASE_MESSAGE_FONT_SIZE * m_scale);
		if (fontSize < 8) fontSize = 8;
		Vector msgPos = m_position + Vector(10 * m_scale + LEGACY_MESSAGE_OFFSET.x, LEGACY_MESSAGE_OFFSET.y);
		Vector pos3 = g->LogicalToWorld(msgPos.x, msgPos.y);
		GameAPP::GetInstance().DrawText(m_message, pos3, m_textColor,
			ResourceKeys::Fonts::FONT_FZCQ, fontSize);
	}

	for (const auto& config : m_textConfigs) {
		int fontSize = static_cast<int>(config.size * m_scale);
		if (fontSize < 8) fontSize = 8;

		Vector pos4 = g->LogicalToWorld(config.pos.x, config.pos.y);
		GameAPP::GetInstance().DrawText(config.text, pos4, config.color,
			config.font, fontSize);
	}

	for (const auto& btn : m_buttons) {
		if (btn) btn->Draw(g);
	}
	for (const auto& slider : m_sliders) {
		if (slider) slider->Draw(g);
	}

	// 浮动说明必须位于面板自有控件之上，避免相邻行的复选框覆盖提示文字。
	const std::string& tooltipText = GetHoveredTooltipText();
	if (!tooltipText.empty() && m_tooltipPanel.maxWidth > 0.0f) {
		const float borderWidth = 2.0f; // 悬停说明框边框宽度，单位：逻辑像素
		const Vector tooltipPos = GetTooltipDrawPosition();
		const Vector tooltipSize = GetTooltipDrawSize(tooltipText);
		Vector outer = g->LogicalToWorld(tooltipPos.x, tooltipPos.y);
		Vector inner = g->LogicalToWorld(
			tooltipPos.x + borderWidth, tooltipPos.y + borderWidth);
		g->FillRect(outer.x, outer.y, tooltipSize.x, tooltipSize.y,
			glm::vec4(150, 170, 110, 230));
		g->FillRect(inner.x, inner.y,
			tooltipSize.x - borderWidth * 2.0f,
			tooltipSize.y - borderWidth * 2.0f,
			glm::vec4(27, 29, 24, 244));
		float textY = tooltipPos.y + kTooltipVerticalPadding / 2.0f;
		for (const std::string& line : m_tooltipLines) {
			const Vector textPos = g->LogicalToWorld(
				tooltipPos.x + kTooltipHorizontalPadding / 2.0f, textY);
			GameAPP::GetInstance().DrawText(line, textPos, m_tooltipPanel.textColor,
				ResourceKeys::Fonts::FONT_FZCQ,
				std::max(8, static_cast<int>(m_tooltipPanel.fontSize * m_scale)));
			textY += m_tooltipLineHeight + kTooltipLineGap;
		}
	}
}

void GameMessageBox::SetActive(bool active)
{
	if (active && m_closeRequested) return;

	m_active = active;
	for (size_t i = 0; i < m_buttons.size(); ++i) {
		if (m_buttons[i]) {
			const bool configuredEnabled = i < m_buttonConfigs.size() && m_buttonConfigs[i].enabled;
			m_buttons[i]->SetEnabled(active && configuredEnabled);
		}
	}
	for (const auto& slider : m_sliders) {
		if (slider) slider->SetDrag(active);
	}
}

const std::string& GameMessageBox::GetHoveredTooltipText() const
{
	static const std::string empty;
	for (size_t i = 0; i < m_buttons.size() && i < m_buttonConfigs.size(); ++i) {
		if (m_buttons[i] && m_buttons[i]->IsHovered() && !m_buttonConfigs[i].tooltipText.empty()) {
			return m_buttonConfigs[i].tooltipText;
		}
	}
	return empty;
}

Vector GameMessageBox::GetTooltipDrawSize(const std::string& text) const
{
	Graphics& graphics = GameAPP::GetInstance().GetGraphics();
	const float rasterScale = graphics.GetLetterboxScale();
	if (m_tooltipLayoutText == text && m_tooltipRasterScale == rasterScale) return m_tooltipSize;
	const int fontSize = std::max(8, static_cast<int>(m_tooltipPanel.fontSize * m_scale));
	const float maxWidth = std::clamp(m_tooltipPanel.maxWidth,
		kTooltipMinimumWidth, SCENE_WIDTH - 2.0f * kTooltipScreenMargin);
	m_tooltipLines = WrapUtf8Text(graphics, text, fontSize,
		maxWidth - kTooltipHorizontalPadding, ResourceKeys::Fonts::FONT_FZCQ);
	float textWidth = 0.0f;
	// 空白行也占据完整行高；测量与绘制使用同一字体和字号。
	m_tooltipLineHeight = graphics.MeasureTextSize("Mg", ResourceKeys::Fonts::FONT_FZCQ, fontSize).y;
	for (const std::string& line : m_tooltipLines) {
		const auto size = graphics.MeasureTextSize(line, ResourceKeys::Fonts::FONT_FZCQ, fontSize);
		textWidth = std::max(textWidth, size.x);
		m_tooltipLineHeight = std::max(m_tooltipLineHeight, size.y);
	}
	m_tooltipSize = Vector(std::max(kTooltipMinimumWidth, textWidth + kTooltipHorizontalPadding),
		m_tooltipLines.size() * (m_tooltipLineHeight + kTooltipLineGap)
		- kTooltipLineGap + kTooltipVerticalPadding);
	m_tooltipLayoutText = text;
	m_tooltipRasterScale = rasterScale;
	return m_tooltipSize;
}

Vector GameMessageBox::GetTooltipDrawPosition() const
{
	const std::string& text = GetHoveredTooltipText();
	if (text.empty() || !GameAPP::GetInstance().IsInputHandlerValid()) return Vector::zero();

	const Vector mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
	const Vector tooltipSize = GetTooltipDrawSize(text);
	float left = mouse.x + kTooltipCursorOffset;
	float top = mouse.y + kTooltipCursorOffset;
	if (left + tooltipSize.x > SCENE_WIDTH - kTooltipScreenMargin) {
		left = mouse.x - tooltipSize.x - kTooltipCursorOffset;
	}
	if (top + tooltipSize.y > SCENE_HEIGHT - kTooltipScreenMargin) {
		top = mouse.y - tooltipSize.y - kTooltipCursorOffset;
	}
	left = std::clamp(left, kTooltipScreenMargin,
		std::max(kTooltipScreenMargin, SCENE_WIDTH - tooltipSize.x - kTooltipScreenMargin));
	top = std::clamp(top, kTooltipScreenMargin,
		std::max(kTooltipScreenMargin, SCENE_HEIGHT - tooltipSize.y - kTooltipScreenMargin));
	return Vector(left, top);
}

void GameMessageBox::Close()
{
	if (m_closeRequested) return;
	SetActive(false);
	m_closeRequested = true;
}

void GameMessageBox::DetachControls()
{
	if (m_owner) {
		for (const auto& button : m_buttons) {
			if (button) m_owner->RemoveButton(button);
		}
		for (const auto& slider : m_sliders) {
			if (slider) m_owner->RemoveSlider(slider);
		}
	}
	m_buttons.clear();
	m_sliders.clear();
	m_owner = nullptr;
}

std::shared_ptr<GameMessageBox> GameMessageBox::Builder::Show()
{
	// 覆盖局限于此 Builder 持有的配置，不修改 Button 或 Graphics 的全局字体。
	if (!m_controlFont.empty()) {
		for (auto& button : m_buttons) button.font = m_controlFont;
		for (auto& text : m_texts) text.font = m_controlFont;
	}
	auto& ui = SceneManager::GetInstance().GetCurrectSceneUIManager();
	auto messageBox = std::make_shared<GameMessageBox>(&ui, m_pos, m_message,
		m_buttons, m_sliders, m_texts, m_title, m_backgroundMode, m_bgKey,
		m_scale, m_explicitSize,
		m_tooltipPanel);
	ui.AddMessageBox(messageBox);
	return messageBox;
}
