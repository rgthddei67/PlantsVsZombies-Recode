#pragma once
#ifndef _DEFINIT_H
#define _DEFINIT_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <SDL2/SDL.h>
#include <math.h>

struct Vector
{
	float x;
	float y;
	constexpr Vector() : x(0), y(0) {}
	constexpr Vector(float x, float y) : x(x), y(y) {}
	constexpr operator SDL_FPoint() const
	{
		return { x, y };
	}
	Vector(const glm::vec2& vec) : x(vec.x), y(vec.y) {}
	operator glm::vec2() const { return glm::vec2(x, y); }

	// explicit：从 4 维静默降到 2 维(只取 x,y)是 bug 温床——颜色/齐次坐标等 vec4
	// 误赋给 Vector 会无声丢掉 z,w。需要时显式写 Vector(someVec4)。vec2 构造(上一行)
	// 保持隐式,那是有意的 vec2↔Vector 互通。
	explicit Vector(const glm::vec4& vec) : x(vec.x), y(vec.y) {}
	constexpr SDL_FPoint ToSDL_FPoint() const {
		return { x, y };
	}
	constexpr bool operator==(const Vector& other) const {
		return x == other.x && y == other.y;
	}
	constexpr bool operator!=(const Vector& other) const {
		return !(*this == other);
	}
	constexpr bool operator<(const Vector& other) const {
		if (x != other.x) return x < other.x;
		return y < other.y;
	}
	constexpr bool operator>(const Vector& other) const {
		if (x != other.x) return x > other.x;
		return y > other.y;
	}
	constexpr Vector operator+(const Vector& other) const {
		return Vector(x + other.x, y + other.y);
	}
	constexpr Vector operator-(const Vector& other) const {
		return Vector(x - other.x, y - other.y);
	}
	Vector operator+(const glm::vec2& other) const {
		return Vector(x + other.x, y + other.y);
	}
	Vector operator-(const glm::vec2& other) const {
		return Vector(x - other.x, y - other.y);
	}
	constexpr Vector operator*(float scalar) const {
		return Vector(x * scalar, y * scalar);
	}
	constexpr Vector operator/(float scalar) const {
		return Vector(x / scalar, y / scalar);
	}
	constexpr Vector& operator+=(const Vector& other) {
		x += other.x;
		y += other.y;
		return *this;
	}
	constexpr Vector& operator-=(const Vector& other) {
		x -= other.x;
		y -= other.y;
		return *this;
	}
	constexpr Vector& operator*=(float scalar) {
		x *= scalar;
		y *= scalar;
		return *this;
	}
	constexpr Vector& operator/=(float scalar) {
		x /= scalar;
		y /= scalar;
		return *this;
	}
	// 计算向量长度（模）
	float magnitude() const {
		return sqrtf(x * x + y * y);
	}
	// 计算向量长度的平方（避免开方运算，用于比较长度时更高效）
	constexpr float sqrMagnitude() const {
		return x * x + y * y;
	}
	// 归一化向量（使其长度为1）
	Vector normalized() const {
		float mag = magnitude();
		if (mag > 0) {
			return Vector(x / mag, y / mag);
		}
		return Vector(0, 0);
	}
	// 点积（内积）
	constexpr float dot(const Vector& other) const {
		return x * other.x + y * other.y;
	}
	// 计算两点之间的距离
	static float distance(const Vector& a, const Vector& b) {
		return (a - b).magnitude();
	}
	// 计算两点之间距离的平方
	static constexpr float sqrDistance(const Vector& a, const Vector& b) {
		return (a - b).sqrMagnitude();
	}
	// 线性插值
	static constexpr Vector lerp(const Vector& a, const Vector& b, float t) {
		t = (t < 0) ? 0 : (t > 1) ? 1 : t; // 限制t在0-1之间
		return a + (b - a) * t;
	}

	static constexpr Vector zero() { return Vector(0, 0); }
	static constexpr Vector one() { return Vector(1, 1); }
	static constexpr Vector up() { return Vector(0, -1); } // 屏幕坐标系中Y轴向下为正
	static constexpr Vector down() { return Vector(0, 1); }
	static constexpr Vector left() { return Vector(-1, 0); }
	static constexpr Vector right() { return Vector(1, 0); }
};

using Matrix4 = glm::mat4;
using Matrix3 = glm::mat3;

inline Vector TransformPoint(const Vector& point, const glm::mat4& matrix) {
	glm::vec4 result = matrix * glm::vec4(point.x, point.y, 0.0f, 1.0f);
	return Vector(result.x, result.y);
}

#endif
