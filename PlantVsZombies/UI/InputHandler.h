#pragma once
#ifndef _INPUTHANDLER_H
#define _INPUTHANDLER_H

#include <SDL2/SDL.h>
#include <map>
#include <vector>
#include <functional>
#include "../Game/Definit.h"

class Graphics;

enum class KeyState
{
	UP,
	DOWN,
	PRESSED,    // 按下
	RELEASED    // 按键松开
};

class InputHandler
{
private:
	Graphics* mGraphics = nullptr;
	bool mSceneMouseBlocked = false;
#if defined(__ANDROID__)
	bool mTouchActive = false;
	SDL_FingerID mPrimaryFinger = 0;
	bool mNextTouchRight = false;
	bool mPrimaryTouchRight = false;
	bool mTouchReleasePending[5] = {};
#endif

	std::map<SDL_Keycode, KeyState> m_keyStates;

	std::map<SDL_Keycode, KeyState> m_prevKeyStates;

	Vector m_mousePosition;

	Vector m_mouseDelta;
	float m_mouseWheel = 0.0f; // 本逻辑步累计滚轮刻度；正值向上

	KeyState m_mouseButtons[5] = { KeyState::UP, KeyState::UP, KeyState::UP, KeyState::UP, KeyState::UP };

	KeyState m_prevMouseButtons[5] = { KeyState::UP, KeyState::UP, KeyState::UP, KeyState::UP, KeyState::UP };

	InputHandler(const InputHandler&) = delete;
	InputHandler& operator=(const InputHandler&) = delete;

public:
	InputHandler(Graphics* graphics);
	~InputHandler() = default;

	void ProcessEvent(SDL_Event* event);

	/** 清除所有按住和边沿状态；失焦或切后台后不得遗留拖动/点击。 */
	void ResetInput();

	void Update();

	KeyState GetKeyState(SDL_Keycode keyCode) const;

	bool IsKeyDown(SDL_Keycode keyCode) const;

	bool IsKeyPressed(SDL_Keycode keyCode) const;

	bool IsKeyReleased(SDL_Keycode keyCode) const;

	Vector GetMousePosition() const;

	Vector GetMouseWorldPosition() const;

	Vector GetMouseDelta() const;
	float GetMouseWheel() const { return mSceneMouseBlocked ? 0.0f : m_mouseWheel; }

	/** Scene 在 UI 处理后屏蔽世界鼠标查询，原始事件状态保留给下一轮 UI。 */
	void SetSceneMouseBlocked(bool blocked) { mSceneMouseBlocked = blocked; }
	KeyState GetMouseButtonState(Uint8 button) const;

	bool IsMouseButtonDown(Uint8 button) const;

	bool IsMouseButtonPressed(Uint8 button) const;

	bool IsMouseButtonReleased(Uint8 button) const;
};

#endif
