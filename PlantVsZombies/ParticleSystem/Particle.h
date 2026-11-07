#pragma once
#ifndef __PARTICLE_H__
#define __PARTICLE_H__

#include "../Graphics.h"
#include "../Game/Definit.h"
#include <memory>
#include <vector>

struct Particle {
	Vector position;
	Vector velocity;
	glm::vec4 color;
	float lifetime = 0.0f;
	float maxLifetime = 60.0f;
	float size = 1.0f;
	float rotation = 0.0f;
	float rotationSpeed = 0.0f;
	float gravity = 0.0f;
	bool active = false;
	const Texture* texture = nullptr;

	float brightness = 1.0f;
	float stretch = 1.0f;
	float baseScale = 1.0f;   // 随机范围 ParticleScale 的 spawn 采样（每粒子保持）
	float fieldRandomX = 0.5f;  // Position 场 X 的逐粒子随机因子∈[0,1]（spawn 采样，整生命周期不变）
	float fieldRandomY = 0.5f;  // Position 场 Y 同上——这对因子让每粒子各自扩散，云才铺得开
	glm::vec3 colorMultiplier;
	int currentFrame = 0;
	float animationTimer = 0.0f;
	int totalFrames = 1;
	float frameRate = 12.0f;
	Vector fieldOffset;
	Vector shakeOffset;
	int landContacts = 0;     // 离体部件的触地次数；第二次触地后贴地滑停
	bool onGround = false;
	float waterline = -1.0f;  // 离体部件入水后的裁剪线，负值表示尚未入水

	Particle();
	void Reset();
	void Update();
	void UpdateAnimation();
};

#endif
