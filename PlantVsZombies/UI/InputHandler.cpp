#include "InputHandler.h"
#include <iostream>
#include "../Graphics.h"

InputHandler::InputHandler(Graphics* graphics)
{
	mGraphics = graphics;
	// m_mouseButtons/m_prevMouseButtons/m_mousePosition/m_mouseDelta 由头文件就地初始化
}

void InputHandler::ResetInput()
{
	m_keyStates.clear();
	m_prevKeyStates.clear();
	for (int i = 0; i < 5; ++i) {
		m_mouseButtons[i] = m_prevMouseButtons[i] = KeyState::UP;
	}
	m_mouseDelta = Vector(0, 0);
	m_mouseWheel = 0;
#if defined(__ANDROID__)
	mTouchActive = mNextTouchRight = mPrimaryTouchRight = false;
	for (bool& pending : mTouchReleasePending) pending = false;
#endif
}

/** 将 SDL 输入归一化为逻辑画布坐标与单步边沿，Android 工具栏选择下一次触摸的按键。 */
void InputHandler::ProcessEvent(SDL_Event* event)
{
#if defined(__ANDROID__)
	// 工具栏事件走 SDL 键盘队列，按逻辑步消费，不从 Java 线程直接改游戏状态。
	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F9) {
		mNextTouchRight = true;
		return;
	}
	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F10) {
		ResetInput();
		// 放到画布外，取消操作不会误命中三叶草卡牌的右键方向切换。
		m_mousePosition = Vector(-100, -100);
		m_mouseButtons[SDL_BUTTON_RIGHT - 1] = KeyState::PRESSED;
		return;
	}
	if (event->type == SDL_KEYUP && event->key.keysym.sym == SDLK_F10) {
		if (m_mouseButtons[SDL_BUTTON_RIGHT - 1] == KeyState::PRESSED)
			mTouchReleasePending[SDL_BUTTON_RIGHT - 1] = true;
		else m_mouseButtons[SDL_BUTTON_RIGHT - 1] = KeyState::RELEASED;
		return;
	}
	// SDL Android 的返回键以 AC_BACK 报告，复用现有暂停菜单的 Escape 契约。
	if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP)
		&& event->key.keysym.sym == SDLK_AC_BACK) event->key.keysym.sym = SDLK_ESCAPE;
	if (event->type == SDL_FINGERDOWN || event->type == SDL_FINGERMOTION || event->type == SDL_FINGERUP) {
		const auto& touch = event->tfinger;
		if (event->type == SDL_FINGERDOWN && !mTouchActive) {
			mTouchActive = true;
			mPrimaryFinger = touch.fingerId;
			mPrimaryTouchRight = mNextTouchRight;
			mNextTouchRight = false;
		}
		if (mTouchActive && touch.fingerId == mPrimaryFinger) {
			SDL_Window* window = SDL_GetWindowFromID(touch.windowID);
			int width = 0, height = 0;
			if (window) SDL_GL_GetDrawableSize(window, &width, &height);
			if (width > 0 && height > 0) {
				const auto point = mGraphics->ScreenToLogical(touch.x * width, touch.y * height);
				m_mousePosition = Vector(point.x, point.y);
				const int button = mPrimaryTouchRight ? SDL_BUTTON_RIGHT - 1 : 0;
				if (event->type == SDL_FINGERDOWN) m_mouseButtons[button] = KeyState::PRESSED;
				if (event->type == SDL_FINGERUP) {
					// 同一轮 poll 内完成的短触也必须让按下和释放各被一个逻辑步看到。
					if (m_mouseButtons[button] == KeyState::PRESSED) mTouchReleasePending[button] = true;
					else m_mouseButtons[button] = KeyState::RELEASED;
				}
			}
			if (event->type == SDL_FINGERUP) mTouchActive = false;
		}
		return;
	}
#endif
	if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
		const auto point = mGraphics->ScreenToLogical(
			static_cast<float>(event->button.x), static_cast<float>(event->button.y));
		m_mousePosition = Vector(point.x, point.y);
	}
	switch (event->type)
	{
	case SDL_KEYDOWN:
	{
		SDL_Keycode key = event->key.keysym.sym;
		if (m_keyStates[key] == KeyState::UP) {
			m_keyStates[key] = KeyState::PRESSED;
		}
	}
	break;

	case SDL_KEYUP:
	{
		SDL_Keycode key = event->key.keysym.sym;
		if (m_keyStates[key] == KeyState::DOWN || m_keyStates[key] == KeyState::PRESSED) {
			m_keyStates[key] = KeyState::RELEASED;
		}
	}
	break;

	case SDL_MOUSEMOTION:
	{
		// 唯一的坐标咽喉点：SDL 给的是帧缓冲像素，全屏 letterbox 下先逆变换回逻辑坐标，
		// 再存入 m_mousePosition。这样下游所有消费者（Button/Slider 比逻辑坐标、
		// Scene/CardSlotManager 经 LogicalToWorld 转世界坐标）都无需改动，
		// 语义与窗口模式完全一致。窗口模式 scale=1/offset=0 时此换算是恒等。
		glm::vec2 logical = mGraphics->ScreenToLogical(
			static_cast<float>(event->motion.x),
			static_cast<float>(event->motion.y));
		m_mousePosition = Vector(logical.x, logical.y);
		break;
	}

	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button >= SDL_BUTTON_LEFT && event->button.button <= SDL_BUTTON_X2) {
			int index = event->button.button - 1;

			if (m_mouseButtons[index] == KeyState::UP) {
				m_mouseButtons[index] = KeyState::PRESSED;
			}
		}
		break;

	case SDL_MOUSEBUTTONUP:
		if (event->button.button >= SDL_BUTTON_LEFT && event->button.button <= SDL_BUTTON_X2) {
			int index = event->button.button - 1;

			if (m_mouseButtons[index] == KeyState::DOWN || m_mouseButtons[index] == KeyState::PRESSED) {
				m_mouseButtons[index] = KeyState::RELEASED;
			}
		}
		break;

	case SDL_MOUSEWHEEL:
		m_mouseWheel += static_cast<float>(event->wheel.y) * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
		break;
	}
}

void InputHandler::Update()
{
	m_mouseWheel = 0;
	Vector prevMousePos = m_mousePosition;

	for (int i = 0; i < 5; i++)
	{
		m_prevMouseButtons[i] = m_mouseButtons[i];
	}
	m_prevKeyStates = m_keyStates;

	m_mouseDelta = m_mousePosition - prevMousePos;

	// PRESSED -> DOWN, RELEASED -> UP
	for (auto& pair : m_keyStates)
	{
		if (pair.second == KeyState::PRESSED) {
			pair.second = KeyState::DOWN;
		}
		else if (pair.second == KeyState::RELEASED) {
			pair.second = KeyState::UP;
		}
	}

	for (int i = 0; i < 5; i++)
	{
		if (m_mouseButtons[i] == KeyState::PRESSED) {
			m_mouseButtons[i] = KeyState::DOWN;
#if defined(__ANDROID__)
			if (mTouchReleasePending[i]) {
				mTouchReleasePending[i] = false;
				m_mouseButtons[i] = KeyState::RELEASED;
			}
#endif
		}
		else if (m_mouseButtons[i] == KeyState::RELEASED) {
			m_mouseButtons[i] = KeyState::UP;
		}
	}
}

KeyState InputHandler::GetKeyState(SDL_Keycode keyCode) const
{
	auto it = m_keyStates.find(keyCode);
	if (it != m_keyStates.end())
	{
		return it->second;
	}
	return KeyState::UP;
}

bool InputHandler::IsKeyDown(SDL_Keycode keyCode) const
{
	KeyState state = GetKeyState(keyCode);
	return state == KeyState::DOWN || state == KeyState::PRESSED;
}

bool InputHandler::IsKeyPressed(SDL_Keycode keyCode) const
{
	return GetKeyState(keyCode) == KeyState::PRESSED;
}

bool InputHandler::IsKeyReleased(SDL_Keycode keyCode) const
{
	return GetKeyState(keyCode) == KeyState::RELEASED;
}

Vector InputHandler::GetMousePosition() const
{
	// m_mousePosition 已是逻辑坐标（letterbox 逆变换在 ProcessEvent 入口完成）。
	return m_mousePosition;
}

Vector InputHandler::GetMouseWorldPosition() const {
	Vector mousePositon = GetMousePosition();
	// 入参已是逻辑坐标，LogicalToWorld 在此基础上做相机逆变换。
	return mGraphics->LogicalToWorld(mousePositon.x, mousePositon.y);
}

Vector InputHandler::GetMouseDelta() const
{
	return m_mouseDelta;
}

KeyState InputHandler::GetMouseButtonState(Uint8 button) const
{
	if (mSceneMouseBlocked) return KeyState::UP;
	if (button >= SDL_BUTTON_LEFT && button <= SDL_BUTTON_X2)
	{
		int index = button - 1;
		return m_mouseButtons[index];
	}
	return KeyState::UP;
}

bool InputHandler::IsMouseButtonDown(Uint8 button) const
{
	KeyState state = GetMouseButtonState(button);
	return state == KeyState::DOWN || state == KeyState::PRESSED;
}

bool InputHandler::IsMouseButtonPressed(Uint8 button) const
{
	return GetMouseButtonState(button) == KeyState::PRESSED;
}

bool InputHandler::IsMouseButtonReleased(Uint8 button) const
{
	return GetMouseButtonState(button) == KeyState::RELEASED;
}
