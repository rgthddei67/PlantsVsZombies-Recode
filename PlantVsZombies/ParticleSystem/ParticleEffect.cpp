#include "ParticleEffect.h"
#include "../DeltaTime.h"
#include "../GameApp.h"
#include <algorithm>
#include <cmath>

void ParticleEffect::InitializeFromConfig(const ParticleEffectConfig& config, Graphics* graphics, const Vector& pos) {
	this->graphics = graphics;
	position = pos;
	systemTimer = 0.0f;
	active = true;

	// 为每个发射器配置创建发射器
	for (const EmitterConfig& emitterConfig : config.emitters) {
		auto emitter = std::make_unique<ParticleEmitter>(graphics);

		// 计算发射器位置（基础位置 + 偏移）
		Vector emitterPos = position;
		emitterPos.x += emitterConfig.emitterOffsetX;
		emitterPos.y += emitterConfig.emitterOffsetY;

		// 从XML配置初始化发射器
		emitter->Initialize(emitterConfig, emitterPos);

		// 设置系统持续时间（使用第一个发射器的系统持续时间）
		if (emitters.empty() && emitterConfig.systemDuration > 0.0f) {
			systemDuration = emitterConfig.systemDuration;
		}

		emitters.push_back(std::move(emitter));
	}
}

void ParticleEffect::Update() {
	if (active) {
		float deltaTime = DeltaTime::GetDeltaTime();
		systemTimer += deltaTime;

		// 检查系统持续时间
		if (systemDuration > 0.0f && systemTimer >= systemDuration) {
			Stop();
		}
	}

	// 无论是否活跃，都继续更新发射器，让已有粒子自然消亡
	for (auto& emitter : emitters) {
		emitter->Update();
	}
}

void ParticleEffect::Draw() {
	if (graphics && clipRightX >= 0.0f) {
		// 粒子在世界层使用逻辑坐标；通用 shader 裁剪会与父框相交，且不会切断粒子 batch。
		const int right = std::max(0, static_cast<int>(std::ceil(clipRightX)));
		graphics->PushClipRect(0, 0, right, SCENE_HEIGHT);
	}
	for (auto& emitter : emitters) {
		emitter->Draw();
	}
	if (graphics && clipRightX >= 0.0f) {
		graphics->PopClipRect();
	}
}

void ParticleEffect::Stop() {
	if (!active) return;
	active = false;
	for (auto& emitter : emitters) {
		emitter->Stop();
	}
}

void ParticleEffect::SetSystemDuration(float duration) {
	if (duration <= 0.0f) return;
	systemDuration = duration;

	// 有明确运行期时长的天气特效应由总计时器收尾，而不是把 SpawnMaxLaunched
	// 当成整段雨的累计配额；循环模式会在固定池内复用已消亡粒子。
	for (auto& emitter : emitters) {
		emitter->SetOneShot(false);
	}
}

bool ParticleEffect::IsEmitting() const {
	if (!active) return false;
	for (const auto& emitter : emitters) {
		if (emitter->IsActive()) return true;
	}
	return false;
}

int ParticleEffect::GetActiveParticleCount() const {
	int count = 0;
	for (const auto& emitter : emitters) {
		count += emitter->GetActiveParticleCount();
	}
	return count;
}

ParticleRenderProbe ParticleEffect::GetLastRenderProbe() const {
	ParticleRenderProbe combined;
	for (const auto& emitter : emitters) {
		const ParticleRenderProbe& probe = emitter->GetLastRenderProbe();
		if (!probe.hasGeometry) continue;
		if (!combined.hasGeometry) {
			combined.minX = probe.minX;
			combined.minY = probe.minY;
			combined.maxX = probe.maxX;
			combined.maxY = probe.maxY;
			combined.hasGeometry = true;
			combined.firstColor = probe.firstColor;
		}
		else {
			combined.minX = std::min(combined.minX, probe.minX);
			combined.minY = std::min(combined.minY, probe.minY);
			combined.maxX = std::max(combined.maxX, probe.maxX);
			combined.maxY = std::max(combined.maxY, probe.maxY);
		}
		combined.quadCount += probe.quadCount;
	}
	return combined;
}

bool ParticleEffect::ShouldDestroy() const {
	if (!active) {
		// 检查所有发射器是否都应该销毁
		for (const auto& emitter : emitters) {
			if (!emitter->ShouldDestroy()) {
				return false;
			}
		}
		return true;
	}
	return false;
}

void ParticleEffect::SetPosition(const Vector& pos) {
	Vector offset = pos - position;
	position = pos;

	for (auto& emitter : emitters) {
		Vector emitterPos = emitter->GetPosition();
		emitterPos.x += offset.x;
		emitterPos.y += offset.y;
		emitter->SetPosition(emitterPos);
	}
}
