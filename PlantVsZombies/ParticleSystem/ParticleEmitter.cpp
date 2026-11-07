#include "ParticleEmitter.h"
#include "../DeltaTime.h"
#include "../GameApp.h"
#include "../Game/Definit.h"
#include "../Game/Board/Board.h"
#include "../GameRandom.h"
#include "../ResourceManager.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace {
	constexpr float kPartGravity = 520.0f;     // 离体碎片最低下落加速度（像素/秒²）
	constexpr float kPartFlightLifetime = 1.35f; // 碎片首触前预留的最长飞行时间（秒）
	constexpr float kPartLandLifetime = .62f;  // 初次触地后留给弹起、滑停和淡出的时间（秒）
	constexpr float kPartWaterLifetime = .42f; // 沉水部件从触水到消失的时长（秒）
	constexpr float kPartFloatLifetime = .62f; // 轻型防具触水后的漂浮和淡出时长（秒）
	constexpr float kPartFadeTime = .19f;      // 离体碎片末段的淡出时长（秒）
	constexpr float kPartGroundDrag = 6.0f;   // 贴地时的水平速度衰减率（1/秒）
	constexpr float kPartWaterDrag = 9.0f;    // 水面以下的水平速度衰减率（1/秒）
	constexpr float kRoundPartImpactSpin = 280.0f; // 头或轻帽弹起时的最小翻转角速度（度/秒）
	constexpr float kFlatPartImpactSpin = 145.0f;  // 手臂与大件弹起时的最小翻转角速度（度/秒）
	constexpr float kRoundPartLaunchDrift = 52.0f; // 圆形碎片离体时的最低水平速度（像素/秒）
	constexpr float kFlatPartLaunchDrift = 34.0f;  // 手臂与大件离体时的最低水平速度（像素/秒）
	constexpr float kArmorContactScale = .88f;     // 重甲贴图透明留边较宽，收紧接触轮廓的倍率

	bool RollsOnContact(ZombiePartMotion motion)
	{
		return motion == ZombiePartMotion::Head || motion == ZombiePartMotion::LightArmor;
	}

	/** 用旋转后的椭圆近似可见轮廓，避免透明的矩形角先接地，看起来像浮空。 */
	float PartBottomRadius(const Particle& particle, ZombiePartMotion motion)
	{
		const float angle = glm::radians(particle.rotation);
		const float width = static_cast<float>(particle.texture->width) / std::max(1, particle.totalFrames);
		const float height = static_cast<float>(particle.texture->height) * particle.stretch;
		const float contactScale = motion == ZombiePartMotion::Armor ? kArmorContactScale : 1.0f;
		return .5f * particle.size * contactScale * std::hypot(width * std::sin(angle), height * std::cos(angle));
	}

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

void ParticleEmitter::Initialize(const EmitterConfig& config, const Vector& pos, ZombiePartTerrain terrain) {
	xmlConfig = config;
	partTerrain = terrain;
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

	activeFields = config.fields;
	for (int i = 0; i < minActive; i++) {
		EmitSingleParticle();
	}
}

std::optional<Vector> ParticleEmitter::Update() {
	const float deltaTime = DeltaTime::GetDeltaTime();
	if (deltaTime <= 0.0f) {
		// 暂停期间仍会执行 UI 逻辑步；不能让 Shake 重抽或 Friction 继续改变粒子状态。
		return {};
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

	std::optional<Vector> splash;
	for (size_t i = 0; i < particles.size(); i++)
	{
		Particle& particle = particles[i];
		if (particle.active)
		{
			const Vector previousCenter = partTerrain.board ? particle.position + particle.fieldOffset : Vector{};
			const bool bouncing = particle.landContacts == 1 && !particle.onGround && particle.waterline < 0.0f;
			const float previousRadius = bouncing ? PartBottomRadius(particle, partTerrain.motion) : 0.0f;
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
			if (partTerrain.board && particle.active) {
				// 弹起时以下缘推进高度，避免贴图旋转变宽后立刻被判成二次触地。
				if (bouncing) particle.position.y += previousRadius - PartBottomRadius(particle, partTerrain.motion);
				const auto impact = ResolvePartContact(particle, previousCenter, deltaTime);
				if (!splash && impact) splash = impact;
				particle.color.a *= std::clamp((particle.maxLifetime - particle.lifetime) / kPartFadeTime, 0.0f, 1.0f);
			}
		}
	}
	return splash;
}

std::optional<Vector> ParticleEmitter::ResolvePartContact(Particle& particle, const Vector& previousCenter, float dt) const
{
	const Board& board = *partTerrain.board;
	const Vector center = particle.position + particle.fieldOffset;
	const float radius = PartBottomRadius(particle, partTerrain.motion);
	const bool water = board.IsPoolWorldPosition(partTerrain.row, center.x);
	const bool floats = partTerrain.motion == ZombiePartMotion::LightArmor;
	if (particle.waterline >= 0.0f) {
		// 已入水的碎片保持裁剪，停止于水岸，避免继续漂到干地上。
		if (!water) particle.position.x = previousCenter.x - particle.fieldOffset.x;
		particle.velocity.x *= std::exp(-kPartWaterDrag * dt);
		particle.rotationSpeed *= std::exp(-kPartWaterDrag * dt);
		if (floats) particle.position.y = particle.waterline - radius * .65f - particle.fieldOffset.y;
		return {};
	}

	const float surfaceY = board.GetZombiePartSurfaceY(partTerrain.row, center.x, water);
	if (!particle.onGround && center.y + radius < surfaceY) return {};
	if (water) {
		particle.waterline = surfaceY;
		particle.onGround = false;
		particle.gravity = 0.0f;
		particle.velocity.x *= floats ? .48f : .24f;
		particle.velocity.y = floats ? 0.0f : 72.0f;
		particle.rotationSpeed *= .45f;
		particle.maxLifetime = particle.lifetime + (floats ? kPartFloatLifetime : kPartWaterLifetime);
		// 水下出生直接下沉。水面上撞击时才把贴图放在接触线并溅起小水花。
		if (floats || previousCenter.y < surfaceY) {
			particle.position.y = surfaceY - radius * (floats ? .65f : 1.0f) - particle.fieldOffset.y;
		}
		if (previousCenter.y < surfaceY) return Vector(center.x, surfaceY);
		return {};
	}

	particle.position.y = surfaceY - radius - particle.fieldOffset.y;
	const bool rolls = RollsOnContact(partTerrain.motion);
	if (particle.onGround) {
		particle.velocity.x *= std::exp(-kPartGroundDrag * dt);
		if (std::abs(particle.velocity.x) < 2.0f) particle.velocity.x = 0.0f;
	}
	else {
		const bool firstContact = ++particle.landContacts == 1;
		if (firstContact) particle.maxLifetime = particle.lifetime + kPartLandLifetime;
		particle.velocity.x *= rolls ? .78f : .58f;
		const float bounceSpeed = std::clamp(particle.velocity.y * (rolls ? .32f : .19f), 0.0f, 75.0f);
		particle.onGround = !firstContact || bounceSpeed < 22.0f;
		particle.velocity.y = particle.onGround ? 0.0f : -bounceSpeed;
		if (particle.onGround) particle.gravity = 0.0f;
	}
	// 水平速度决定翻滚方向。用当前接触半径估算角速度，保持转速和位移一起减弱。
	const float rollSpeed = glm::degrees(particle.velocity.x / std::max(radius, 5.0f));
	const float impactSpin = particle.onGround ? 0.0f : (rolls ? kRoundPartImpactSpin : kFlatPartImpactSpin);
	particle.rotationSpeed = std::copysign(std::clamp(std::abs(rollSpeed), impactSpin, 450.0f), rollSpeed);
	return {};
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
	if (partTerrain.board) {
		particle->stretch = xmlConfig.particleStretch.GetValue(0.0f);
		particle->gravity = std::max(kPartGravity, particle->gravity);
		particle->maxLifetime = std::max(kPartFlightLifetime, particle->maxLifetime);
		particle->velocity.y = std::clamp(particle->velocity.y, -55.0f, 85.0f);
		// 近竖直发射时留一点侧向动量，确保落地时仍能看出短翻或滑停。
		const float launchDrift = RollsOnContact(partTerrain.motion) ? kRoundPartLaunchDrift : kFlatPartLaunchDrift;
		if (std::abs(particle->velocity.x) < launchDrift) {
			const float direction = particle->velocity.x == 0.0f ? particle->rotationSpeed : particle->velocity.x;
			particle->velocity.x = std::copysign(launchDrift, direction);
		}
		for (const ParticleField& field : activeFields) {
			if (field.type != ParticleFieldType::POSITION) continue;
			particle->fieldOffset.x = field.xTrack.GetValueRandomized(0.0f, particle->fieldRandomX);
			particle->fieldOffset.y = field.yTrack.GetValueRandomized(0.0f, particle->fieldRandomY);
		}
		const Vector center = particle->position + particle->fieldOffset;
		const Board& board = *partTerrain.board;
		if (board.IsPoolWorldPosition(partTerrain.row, center.x)
			&& center.y >= board.GetZombiePartSurfaceY(partTerrain.row, center.x, true)) {
			ResolvePartContact(*particle, center, 0.0f);
		}
	}

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

			if (particle.waterline >= 0.0f) m_graphics->PushClipBottom(particle.waterline);
			if (GameAPP::mAutoTestMode && partTerrain.board) {
				mLastRenderProbe.partLandContacts += particle.landContacts;
				mLastRenderProbe.partWaterContacts += particle.waterline >= 0.0f;
			}
			if (xmlConfig.hasParticleRotation || partTerrain.board) {
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
			if (particle.waterline >= 0.0f) m_graphics->PopClipBottom();
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
