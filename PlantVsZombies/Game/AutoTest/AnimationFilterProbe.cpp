#include "AnimationFilterProbe.h"
#include "TestDriver.h"
#include "../../ResourceManager.h"
#include "../../ResourceKeys.h"
#include "../../Reanimation/Animator.h"
#include "../SceneManager.h"
#include "../Scene.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace {
	/** 失败立即交给 AutoTest；不向正式 Animator 增加测试专用状态接口。 */
	void Require(bool condition, const char* message) {
		if (!condition) throw std::runtime_error(message);
	}
}

bool RunAnimationFilterProbe(std::string& error) {
	try {
		auto& resources = ResourceManager::GetInstance();
		// 用原始线性扫描作独立参照，覆盖实际资源中的重复名、顺序和缺失查询。
		for (const auto& [key, path] : resources.GetAnimationPaths()) {
			const auto reanim = resources.GetReanimation(key);
			const auto copy = resources.GetReanimation(key);
			Require(reanim && copy, "animation probe: resource missing");
			for (int i = 0; i < static_cast<int>(reanim->GetTrackCount()); ++i) {
				const auto& name = reanim->GetTrack(i)->mTrackName;
				std::vector<int> expected;
				for (int j = 0; j < static_cast<int>(reanim->GetTrackCount()); ++j)
					if (reanim->GetTrack(j)->mTrackName == name) expected.push_back(j);
				const auto indices = reanim->GetTrackIndices(name);
				Require(std::ranges::equal(indices, expected), "animation probe: duplicate order changed");
				Require(indices.data() == copy->GetTrackIndices(name).data(), "animation probe: index not shared");
				Require(reanim->GetFirstTrackIndex(name) == expected.front(), "animation probe: first match changed");
			}
			Require(reanim->GetTrackIndices("__missing_probe_track__").empty()
				&& reanim->GetFirstTrackIndex("__missing_probe_track__") == -1,
				"animation probe: missing name changed");
		}
		const auto fixture = std::make_shared<Reanimation>();
		Require(fixture->LoadFromFile("../../autotest/fixtures/track_lookup.reanim"), "animation probe: fixture missing");
		Require(std::ranges::equal(fixture->GetTrackIndices("duplicate"), std::array{0, 2, 4})
			&& fixture->GetFirstTrackIndex("Duplicate") == 1 && fixture->GetFirstTrackIndex("") == 3,
			"animation probe: empty/case-sensitive name changed");
		const auto a = std::make_shared<Animator>(fixture);
		const auto b = std::make_shared<Animator>(std::make_shared<Reanimation>(*fixture));
		b->SetCurrentFrame(1.0f);
		Require(a->GetTrackPosition("duplicate").x == 20.0f && b->GetTrackPosition("duplicate").x == 25.0f
			&& b->GetTrackRotation("duplicate") == 5.0f && a->GetCurrentFrame() == 0.0f,
			"animation probe: first valid frame or instance frame changed");
		const Texture* texture = resources.GetTexture(ResourceKeys::Textures::IMAGE_WALLNUT, false);
		Require(texture != nullptr, "animation probe: texture missing");
		a->SetTrackImage("duplicate", texture);
		Require(b->GetTrackImageOverride("duplicate") == nullptr, "animation probe: image leaked");
		b->SetTrackImage("duplicate", texture);
		a->SetTrackColor("duplicate", SDL_Color{128, 200, 255, 128});
		a->SetTrackOffset("duplicate", 5.0f, 3.0f);
		a->SetTrackGlowOverride("duplicate", true);
		Require(b->GetTrackColor("duplicate").r == 255 && !b->GetTrackGlowEffectEnabled("duplicate"),
			"animation probe: color/glow leaked");
		a->SetTrackGlowOverride("duplicate", false);
		Scene* scene = SceneManager::GetInstance().GetCurrentScene();
		Require(scene != nullptr, "animation probe: scene missing");
		// 固定贴图、位置与颜色，不依赖战斗时钟；同一画面可直接比较新旧 shader。
		scene->RegisterDrawCommand("AnimationFilterProbe", [a, b, texture, checked = false](Graphics* g) mutable {
			const auto savedBlend = g->GetBlendMode();
			if (!checked) {
				const auto expectQuads = [g](const auto& anim, int count) {
					anim->Draw(g, 20, 20, .5f);
					if (anim->GetLastRenderProbe().quadCount != count)
						TestDriver::GetInstance().OnRenderFailure("animation probe: expected " + std::to_string(count)
							+ " quads, got " + std::to_string(anim->GetLastRenderProbe().quadCount));
				};
				expectQuads(a, 2);
				a->SetTrackVisible("duplicate", false);
				expectQuads(a, 0);
				expectQuads(b, 2);
				a->SetTrackVisible("duplicate", true);
				a->SetTrackFollowerImage("duplicate", "first", texture, 0, 0, 1, 1, false);
				a->SetTrackFollowerImage("duplicate", "second", texture, 0, 0, 1, 1, false);
				a->SetTrackFollowerVisible("duplicate", "first", true);
				a->SetTrackFollowerVisible("duplicate", "second", true);
				expectQuads(a, 6);
				a->SetTrackFollowerVisible("duplicate", "first", false);
				expectQuads(a, 4);
				a->SetTrackFollowerImage("duplicate", "first", nullptr, 0, 0, 1, 1, false);
				a->SetTrackFollowerImage("duplicate", "second", nullptr, 0, 0, 1, 1, false);
				expectQuads(a, 2);
				a->AttachAnimator("duplicate", b);
				expectQuads(a, 6);
				a->DetachAnimator("duplicate", b);
				expectQuads(a, 2);
				checked = true;
			}
			g->SetBlendMode(BlendMode::Alpha);
			g->FillRect(0, 0, 1100, 600, glm::vec4(24, 32, 48, 255));
			g->FillRect(550, 0, 550, 600, glm::vec4(232, 224, 208, 255));
			// 每列依次为普通、洗白和弱洗白；左半不透明，右半染色并淡出。
			for (int row = 0; row < 3; ++row) {
				const bool worker = row == 2 && g->IsParallelDrawEnabled();
				if (worker) { g->BeginParallelRecord(1); g->SetWorkerSlot(0); }
				for (int col = 0; col < 6; ++col) {
					const BlendMode modes[]{BlendMode::Alpha, BlendMode::WashedOut, BlendMode::LessWashedOut};
					g->SetBlendMode(modes[col % 3]);
					const glm::vec4 tint = col < 3 ? glm::vec4(255) : glm::vec4(128, 200, 255, row == 2 ? 16 : 128);
					const float x = 20.25f + col * 182.0f, y = 15.25f + row * 145.0f;
					if (row != 0 && g->IsInstancePathEnabled()) {
						// 普通 DrawTextureInstanced 固定使用 Alpha；滤镜须走 Animator 使用的显式 blend 入口。
						InstanceRecord record{};
						record.tA = 135; record.tD = 125; record.tx = x; record.ty = y;
						record.u0 = texture->aU0; record.v0 = texture->aV0;
						record.u1 = texture->aU1; record.v1 = texture->aV1;
						record.texSlot = (texture->atlasPage ? texture->atlasPage : texture)->BindingId();
						record.colorRGBA8 = static_cast<uint32_t>(tint.r) | (static_cast<uint32_t>(tint.g) << 8)
							| (static_cast<uint32_t>(tint.b) << 16) | (static_cast<uint32_t>(tint.a) << 24);
						g->AppendReanimInstance(record, modes[col % 3]);
					}
					else g->DrawTexture(texture, x, y, 135, 125, 0, tint);
				}
				if (worker) { g->ClearWorkerSlot(); g->ReplayAndEndParallel(); }
			}
			a->EnableWashedOutEffect(true);
			b->EnableWashedOutEffect(true, true);
			a->Draw(g, 60, 465, .65f);
			b->Draw(g, 610, 465, .65f);
			g->SetBlendMode(savedBlend);
		}, LAYER_UI + 500);
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
}
