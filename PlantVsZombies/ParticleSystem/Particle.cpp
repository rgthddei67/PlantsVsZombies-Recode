#include "Particle.h"
#include "../DeltaTime.h"
#include <cmath>

Particle::Particle() {
	Reset();
}

void Particle::Reset() {
	position = { 0, 0 };
	velocity = { 0, 0 };
	color = glm::vec4(255.0f);
	lifetime = 0.0f;
	maxLifetime = 60.0f;
	size = 1.0f;
	rotation = 0.0f;
	rotationSpeed = 0.0f;
	gravity = 0.0f;
	active = false;
	texture = nullptr;

	brightness = 1.0f;
	stretch = 1.0f;
	baseScale = 1.0f;
	fieldRandomX = 0.5f;  // 默认中点，等价于旧的确定性行为（EmitSingleParticle 会重抽）
	fieldRandomY = 0.5f;
	colorMultiplier = glm::vec3(1.0f);
	currentFrame = 0;
	animationTimer = 0.0f;
	totalFrames = 1;
	frameRate = 12.0f;
	fieldOffset = { 0, 0 };
	shakeOffset = { 0, 0 };
	landContacts = 0;
	onGround = false;
	waterline = -1.0f;
}

void Particle::Update() {
	if (!active) return;

	float deltaTime = DeltaTime::GetDeltaTime();
	lifetime += deltaTime;

	if (lifetime >= maxLifetime) {
		active = false;
		return;
	}

	velocity.y += gravity * deltaTime;
	position.x += velocity.x * deltaTime;
	position.y += velocity.y * deltaTime;
	rotation += rotationSpeed * deltaTime;

	UpdateAnimation();
}

void Particle::UpdateAnimation() {
	if (totalFrames <= 1 || frameRate <= 0.0f) {
		return;
	}

	float deltaTime = DeltaTime::GetDeltaTime();
	animationTimer += deltaTime;

	float frameDuration = 1.0f / frameRate;
	if (animationTimer >= frameDuration) {
		animationTimer -= frameDuration;
		currentFrame = (currentFrame + 1) % totalFrames;
	}
}
