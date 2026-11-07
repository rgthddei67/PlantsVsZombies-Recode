#include "ParticleEmitter.h"
#include "../DeltaTime.h"
#include "../GameApp.h"
#include "../Game/Definit.h"
#include "../GameRandom.h"
#include "../ResourceManager.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace {
	/** 把粒子实际提交的单位四边形矩阵并入本发射器的 AutoTest 包围盒。 */
	void RecordParticleQuad(ParticleRenderProbe& probe, const glm::mat4& matrix)
	{
		const glm::vec4 corners[4] = {
			matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
			matrix * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
			matrix * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
			matrix * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
		};
		if (!probe.hasGeometry) {
			probe.minX = probe.maxX = corners[0].x;
			probe.minY = probe.maxY = corners[0].y;
			probe.hasGeometry = true;
		}
		for (const glm::vec4& corner : corners) {
			probe.minX = std::min(probe.minX, corner.x);
			probe.minY = std::min(probe.minY, corner.y);
			probe.maxX = std::max(probe.maxX, corner.x);
			probe.maxY = std::max(probe.maxY, corner.y);
		}
		++probe.quadCount;
	}
}

ParticleEmitter::ParticleEmitter(Graphics* g)
	: m_graphics(g)
{
}

void ParticleEmitter::Initialize(const EmitterConfig& config, const Vector& pos) {
	xmlConfig = config;
	position = pos;
	active = true;
	spawnTimer = 0.0f;
	particlesEmitted = 0;
	systemTimer = 0.0f;

	spawnRate = config.spawnRate;

	isOneShot = true;
	int minActive = std::max(0, static_cast<int>(config.spawnMinActive.GetRandomValue()));
	int maxLaunched = std::max(0, static_cast<int>(config.spawnMaxLaunched.GetRandomValue()));
	particlesToEmit = maxLaunched;

	maxParticles = std::max(maxLaunched, minActive);
	particles.resize(maxParticles);

	for (int i = 0; i < minActive; i++) {
		EmitSingleParticle();
	}

	activeFields = config.fields;
}

void ParticleEmitter::Update() {
	const float deltaTime = DeltaTime::GetDeltaTime();
	if (deltaTime <= 0.0f) {
		// 暂停期间仍会执行 UI 逻辑步；不能让 Shake 重抽或 Friction 继续改变粒子状态。
		return;
	}

	if (active) {
		systemTimer += deltaTime;

		if (isOneShot && particlesEmitted >= particlesToEmit) {
			spawnRate = 0;
			if (GetActiveParticleCount() == 0) {
				active = false;
			}
		}

		if (spawnRate > 0 && (!isOneShot || particlesEmitted < particlesToEmit)) {
			spawnTimer += deltaTime;
			float spawnInterval = 1.0f / spawnRate;
			if (spawnTimer >= spawnInterval) {
				if (!isOneShot || particlesEmitted < particlesToEmit) {
					EmitSingleParticle();
					spawnTimer = 0;
					particlesEmitted++;
				}
			}
		}
	}

	for (size_t i = 0; i < particles.size(); i++)
	{
		Particle& particle = particles[i];
		if (particle.active)
		{
			float normalizedTime = particle.lifetime / particle.maxLifetime;

			float systemAlphaValue = 1.0f;
			if (xmlConfig.systemDuration > 0.0f) {
				float systemNormalizedTime = systemTimer / xmlConfig.systemDuration;
				systemAlphaValue = xmlConfig.systemAlpha.GetValue(systemNormalizedTime);
			}

			float alpha = xmlConfig.particleAlpha.GetValue(normalizedTime);
			float scale = xmlConfig.particleScale.isRandomRange
				? particle.baseScale
				: xmlConfig.particleScale.GetValue(normalizedTime);
			float stretch = xmlConfig.particleStretch.GetValue(normalizedTime);

			particle.size = scale;
			particle.stretch = stretch;
			particle.color.a = alpha * systemAlphaValue * 255.0f;

			float red = xmlConfig.particleRed.GetValue(normalizedTime);
			float green = xmlConfig.particleGreen.GetValue(normalizedTime);
			float blue = xmlConfig.particleBlue.GetValue(normalizedTime);
			particle.colorMultiplier = glm::vec3(red, green, blue);

			for (const ParticleField& field : activeFields) {
				if (field.type == ParticleFieldType::POSITION) {
					// 逐粒子采样：每颗粒子按自己的 fieldRandom 因子在区间内扩散
					particle.fieldOffset.x = field.xTrack.GetValueRandomized(normalizedTime, particle.fieldRandomX);
					particle.fieldOffset.y = field.yTrack.GetValueRandomized(normalizedTime, particle.fieldRandomY);
					continue;
				}

				float xValue = field.xTrack.GetValue(normalizedTime);
				float yValue = field.yTrack.GetValue(normalizedTime);

				if (field.type == ParticleFieldType::SHAKE) {
					particle.shakeOffset.x = GameRandom::Range(-xValue, xValue);
					particle.shakeOffset.y = GameRandom::Range(-yValue, yValue);
				}
				else if (field.type == ParticleFieldType::FRICTION) {
					particle.velocity.x *= (1.0f - xValue);
					particle.velocity.y *= (1.0f - yValue);
				}
				else if (field.type == ParticleFieldType::ACCELERATION) {
					particle.velocity.x += xValue * deltaTime;
					particle.velocity.y += yValue * deltaTime;
				}
				else if (field.type == ParticleFieldType::CIRCLE
					|| field.type == ParticleFieldType::AWAY) {
					// 原版 Circle/Away 每厘秒直接改一次位置；换算为秒制后以 dt 缩放，
					// 分别沿系统中心的切线/径向推进，不能误当成绝对 X 偏移或世界加速度。
					const Vector fromCenter = particle.position - position;
					const float radius = std::sqrt(fromCenter.x * fromCenter.x
						+ fromCenter.y * fromCenter.y);
					if (radius > 0.0001f) {
						const float radialX = fromCenter.x / radius;
						const float radialY = fromCenter.y / radius;
						const float randomizedX = field.xTrack.GetValueRandomized(
							normalizedTime, particle.fieldRandomX);
						const float randomizedY = field.yTrack.GetValueRandomized(
							normalizedTime, particle.fieldRandomY);
						const float distance = (randomizedX + radius * randomizedY) * deltaTime;
						if (field.type == ParticleFieldType::CIRCLE) {
							particle.position.x += -radialY * distance;
							particle.position.y += radialX * distance;
						}
						else {
							particle.position.x += radialX * distance;
							particle.position.y += radialY * distance;
						}
					}
				}
			}

			particle.Update();
		}
	}
}

void ParticleEmitter::EmitParticles(int count) {
	if (isOneShot) {
		particlesToEmit = count;
		particlesEmitted = 0;
	}

	for (int i = 0; i < count && (!isOneShot || particlesEmitted < particlesToEmit); ++i) {
		EmitSingleParticle();
		if (isOneShot) {
			particlesEmitted++;
		}
	}
}

void ParticleEmitter::EmitSingleParticle() {
	Particle* particle = GetFreeParticle();
	if (!particle) return;

	particle->Reset();  // 注意：Reset 已置 active=false

	// 先解析纹理，并以其为生成前置条件：
	// - 无 <Image> 的发射器（如 CrossFade 的 FadeOut 伴随发射器）不产生可绘制粒子；
	// - 指定了纹理键却未加载成功的同样跳过。
	// 提前返回（粒子保持 inactive），既省掉后续无意义初始化，
	// 也从源头杜绝 Draw 每帧打印"没有图片绘制"。
	if (xmlConfig.imageKeys.empty()) {
		return;
	}
	ResourceManager& resourceManager = ResourceManager::GetInstance();
	int randomIndex = GameRandom::Range(0, static_cast<int>(xmlConfig.imageKeys.size()) - 1);
	const Texture* texture = resourceManager.GetTexture(xmlConfig.imageKeys[randomIndex]);
	if (!texture) {
		return;
	}

	particle->active = true;
	particle->texture = texture;

	particle->position = GetSpawnPosition();

	particle->maxLifetime = xmlConfig.particleDuration.GetRandomValue();

	float speed = xmlConfig.launchSpeed.GetRandomValue();
	float angle = 0.0f;
	if (xmlConfig.randomLaunchSpin) {
		angle = GameRandom::Range(0.0f, 360.0f) * (3.14159f / 180.0f);
	}
	particle->velocity = Vector(cosf(angle) * speed, sinf(angle) * speed);

	// 初始角度与自旋速度分开采样：前者适合斜雨丝等静态朝向，
	// 后者只负责生命周期内持续旋转，避免用自旋伪装朝向时每帧漂移。
	particle->rotation = xmlConfig.particleRotation.GetRandomValue();
	particle->rotationSpeed = xmlConfig.particleSpinSpeed.GetRandomValue();

	particle->gravity = xmlConfig.particleGravity;

	particle->totalFrames = xmlConfig.imageFrames;
	particle->frameRate = xmlConfig.animationRate;
	particle->currentFrame = 0;
	particle->animationTimer = 0.0f;

	particle->brightness = xmlConfig.particleBrightness.GetRandomValue();

	particle->baseScale = xmlConfig.particleScale.SampleConstant();
	particle->size = xmlConfig.particleScale.isRandomRange
		? particle->baseScale
		: xmlConfig.particleScale.GetValue(0.0f);

	// Position 场逐粒子随机因子：每颗粒子各抽一次、整生命周期保持，
	// 使其沿 X/Y 区间内的不同"轨道"扩散，瘴气云才会横向铺开而非堆成一坨。
	particle->fieldRandomX = GameRandom::Range(0.0f, 1.0f);
	particle->fieldRandomY = GameRandom::Range(0.0f, 1.0f);

	particle->color = glm::vec4(255.0f);
	// 仅僵尸离体部件在出生帧消费宿主颜色快照；普通粒子保持原初始化约定。
	if (xmlConfig.initializeDetachedPartTint) {
		particle->colorMultiplier = glm::vec3(xmlConfig.particleRed.GetValue(0),
			xmlConfig.particleGreen.GetValue(0), xmlConfig.particleBlue.GetValue(0));
	}
}

Vector ParticleEmitter::GetSpawnPosition() const {
	Vector spawnPos = position;

	// 发射器基础偏移：瘴气云据此从植物前方 55px 起步（此前被解析却从未应用）。
	// 对所有发射器形状生效，BOX/CIRCLE 的散布在此基础上叠加。
	spawnPos.x += xmlConfig.emitterOffsetX;
	spawnPos.y += xmlConfig.emitterOffsetY;

	switch (xmlConfig.emitterType) {
	case EmitterType::POINT:
		break;

	case EmitterType::BOX:
		spawnPos.x += xmlConfig.emitterBoxX.GetRandomValue();
		spawnPos.y += xmlConfig.emitterBoxY.GetRandomValue();
		break;

	case EmitterType::CIRCLE: {
		float radius = xmlConfig.emitterRadius.GetRandomValue();
		float angle = GameRandom::Range(0.0f, 360.0f) * (3.14159f / 180.0f);
		spawnPos.x += cosf(angle) * radius;
		spawnPos.y += sinf(angle) * radius;
		break;
	}
	}

	return spawnPos;
}

Particle* ParticleEmitter::GetFreeParticle() {
	for (auto& particle : particles) {
		if (!particle.active)
			return &particle;
	}
	return nullptr;
}

void ParticleEmitter::Draw() {
	if (!m_graphics) return;
	if (GameAPP::mAutoTestMode)
		mLastRenderProbe = {};

	for (size_t i = 0; i < particles.size(); i++)
	{
		Particle& particle = particles[i];
		if (particle.active)
		{
			if (!particle.texture) {
				continue;
			}
			// ImageFrames 序列帧：贴图为横向帧条（如毁灭菇爆炸底座 471x85 = 3 帧 157x85），
			// UpdateAnimation 按 AnimationRate 循环推进 currentFrame，这里取对应列。
			// totalFrames<=1 时 frameW 即整图宽，走同一条 DrawTextureRegion 路径。
			int frameCount = std::max(1, particle.totalFrames);
			float srcW = static_cast<float>(particle.texture->width) / frameCount;
			float srcH = static_cast<float>(particle.texture->height);
			float srcX = srcW * (particle.currentFrame % frameCount);
			float destW = srcW * particle.size;
			float destH = srcH * particle.size * particle.stretch;

			float x = particle.position.x + particle.fieldOffset.x + particle.shakeOffset.x - destW * 0.5f;
			float y = particle.position.y + particle.fieldOffset.y + particle.shakeOffset.y - destH * 0.5f;

			glm::vec4 finalColor = particle.color;
			finalColor.r *= particle.brightness * particle.colorMultiplier.r;
			finalColor.g *= particle.brightness * particle.colorMultiplier.g;
			finalColor.b *= particle.brightness * particle.colorMultiplier.b;
			if (GameAPP::mAutoTestMode && !mLastRenderProbe.hasGeometry) mLastRenderProbe.firstColor = finalColor;

			if (xmlConfig.hasParticleRotation) {
				// DrawTextureRegion 的兼容旋转路径会先非等比缩放再旋转，使细长贴图的角度
				// 被长宽比压扁。显式初始朝向改为围绕世界中心先旋转、再绘制拉伸矩形，
				// 让配置角度就是屏幕上实际角度；未使用新标签的旧特效保持原样。
				if (GameAPP::mAutoTestMode) {
					glm::mat4 finalMatrix(1.0f);
					finalMatrix = glm::translate(finalMatrix,
						glm::vec3(x + destW * 0.5f, y + destH * 0.5f, 0.0f));
					finalMatrix = glm::rotate(finalMatrix, glm::radians(particle.rotation),
						glm::vec3(0.0f, 0.0f, 1.0f));
					finalMatrix = glm::translate(finalMatrix,
						glm::vec3(-destW * 0.5f, -destH * 0.5f, 0.0f));
					finalMatrix = glm::scale(finalMatrix, glm::vec3(destW, destH, 1.0f));
					RecordParticleQuad(mLastRenderProbe, finalMatrix);
				}
				m_graphics->PushTransform();
				m_graphics->Translate(x + destW * 0.5f, y + destH * 0.5f);
				m_graphics->Rotate(particle.rotation, 0.0f, 0.0f, 1.0f);
				m_graphics->DrawTextureRegion(
					particle.texture,
					srcX, 0.0f, srcW, srcH,
					-destW * 0.5f, -destH * 0.5f, destW, destH,
					0.0f,
					finalColor
				);
				m_graphics->PopTransform();
			}
			else {
				if (GameAPP::mAutoTestMode) {
					glm::mat4 finalMatrix(1.0f);
					finalMatrix = glm::translate(finalMatrix, glm::vec3(x, y, 0.0f));
					finalMatrix = glm::scale(finalMatrix, glm::vec3(destW, destH, 1.0f));
					if (particle.rotation != 0.0f) {
						finalMatrix = glm::translate(finalMatrix, glm::vec3(0.5f, 0.5f, 0.0f));
						finalMatrix = glm::rotate(finalMatrix, glm::radians(particle.rotation),
							glm::vec3(0.0f, 0.0f, 1.0f));
						finalMatrix = glm::translate(finalMatrix, glm::vec3(-0.5f, -0.5f, 0.0f));
					}
					RecordParticleQuad(mLastRenderProbe, finalMatrix);
				}
				m_graphics->DrawTextureRegion(
					particle.texture,
					srcX, 0.0f, srcW, srcH,
					x, y, destW, destH,
					particle.rotation,
					finalColor
				);
			}
		}
	}
}

void ParticleEmitter::Clear() {
	for (size_t i = 0; i < particles.size(); i++)
	{
		particles[i].active = false;
	}
}

bool ParticleEmitter::ShouldDestroy() const {
	return !active && GetActiveParticleCount() == 0;
}

int ParticleEmitter::GetActiveParticleCount() const {
	int count = 0;
	for (size_t i = 0; i < particles.size(); i++)
	{
		if (particles[i].active)
		{
			count++;
		}
	}
	return count;
}
