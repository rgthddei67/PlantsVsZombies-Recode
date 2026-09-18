#pragma once
#ifndef _GAMEAPP_H
#define _GAMEAPP_H
#ifdef DrawText
#undef DrawText
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept>
#include "ResourceKeys.h"
#include "./Game/Definit.h"
#include "./ParticleSystem/ParticleSystem.h"
#include "GameInfoSaver.h"
#include "./Game/Plant/PlantType.h"
#include "./Game/Zombie/ZombieType.h"
#include "Graphics.h"

constexpr int SCENE_WIDTH = 1100;
constexpr int SCENE_HEIGHT = 600;

class InputHandler;
class Board;
class Plant;
class Zombie;

namespace pvz {
	class VulkanContext;
	class VulkanRenderer;
	class VulkanTexturePool;
	class OpenGLRenderer;
	class OpenGLTextureBackend;
}

enum class Background;

class GameAPP
{
public:
	std::array<float, 4> mColdStorageHabits{}; // 冷藏站近期战术画像，按正式落种衰减更新
	int Difficulty = 3; // 难度系数
	int mAdventureLevel = 1;    // 玩到的冒险模式关卡
	bool mEncounteredEliteDancer = false; // 是否曾由正式波次实际刷出精英舞王
	std::vector<int> mCrazyDaveTutorialsSeen; // 已完整看过或主动跳过的关卡闲聊（稳定冒险关卡号）
	int mDeveloperSelectedLevel = 1; // 开发者面板上次选择的关卡号
	std::string mDeveloperSelectedZombie = "ZOMBIE_NORMAL"; // 开发者面板上次选择的召唤僵尸枚举名
	bool mShowPlantHP = false;  // 植物显示血量
	bool mShowZombieHP = false; // 僵尸显示血量
	bool mAutoCollected = true; // 自动收集
	bool mEnableMonteCarloAI = true; // 轻量蒙特卡洛 AI；关闭后调用者走各自低配回退策略
	bool mAdvancedPauseEnabled = false; // 高级暂停；开启后空格暂停期间仍可操作卡槽并种植
	bool mHxyModeEnabled = false; // HXY专属；新开局采用轻松出怪、额外阳光与防具耐久规则
	bool mOpeningTyphoonProtectionEnabled = true; // 开局台风保护；开启后首轮第 1～5 波不会附加台风
	bool mTyphoonWeatherEnabled = true; // 台风天气总开关；关闭后所有地图都不会生成或保留台风
	bool mVsync = true;    // 是否开启垂直同步
	bool mFullscreen = false;   // 是否全屏（等比 letterbox，无边框桌面全屏）

	std::vector<PlantType> mHaveCards;      // 玩家拥有的卡牌
	std::vector<std::string> mLastSelectedCards; // 上一次已提交选卡的稳定植物枚举名（保持点击顺序）

	GameInfoSaver mGameInfoSaver;

private:
	std::unique_ptr<InputHandler> mInputHandler;
	std::unique_ptr<Graphics> m_graphics;   // 改用 Graphics

	SDL_Window* mWindow;
	// 两套后端互斥拥有自己的窗口渲染资源；Graphics 只持非 owning 接口。
	std::unique_ptr<pvz::VulkanContext>     m_vulkanCtx;
	std::unique_ptr<pvz::VulkanRenderer>    m_vulkanRenderer;
	std::unique_ptr<pvz::VulkanTexturePool> m_vulkanTexPool;
	std::unique_ptr<pvz::OpenGLRenderer> m_openGLRenderer;
	std::unique_ptr<pvz::OpenGLTextureBackend> m_openGLTextureBackend;
	pvz::RendererBackend m_selectedRenderer = pvz::RendererBackend::Vulkan;
	std::string m_vulkanStartupError;
	std::string m_openGLStartupError;

	bool mRunning;
	bool mInitialized;

	GameAPP();
	~GameAPP();

	GameAPP(const GameAPP&) = delete;
	GameAPP& operator=(const GameAPP&) = delete;

	/** 初始化 SDL 平台服务；Android 同时设定横屏和单一触摸事件来源。 */
	bool InitializeSDL();
	bool InitializeSDL_Image();
	bool InitializeSDL_TTF();
	bool InitializeAudioSystem();
	/** 创建当前平台渲染器：Windows 保留自动回退，Android 固定 GLES。 */
	bool CreateWindowAndRenderer();
	bool TryCreateVulkanRenderer(std::string& error);
	/** 创建独立 GL 窗口；桌面尝试 Core 4.3/3.3，Android 请求 ES 3.0。 */
	bool TryCreateOpenGLRenderer(std::string& error);
	void DestroyRenderWindow();
	bool InitializeResourceManager();
	bool LoadAllResources();
	void CleanupResources();
	void Draw();
	void Shutdown();

public:
	inline static bool mDebugMode = false;        // 是否是调试模式
	inline static bool mShowColliders = false;    // 显示碰撞框开关
	inline static bool mDisableInstancePath = false;  // Task 7: -NoInstance 启动参数禁用 GPU instance path
	inline static bool mForceOpenGL33 = false;        // -OpenGL33：跳过 4.3/SSBO 探测，强制原 CPU Batch
	inline static bool mForceVulkan12 = false;        // -Vulkan12：把 instance/device 能力协商限制到 Vulkan 1.2
	inline static bool mForceLegacyRendering = false; // -VulkanLegacyRendering：屏蔽 dynamic rendering 路径
	inline static bool mForceLegacySync = false;      // -VulkanLegacySync：屏蔽 synchronization2 路径
	inline static pvz::RendererPreference mRendererPreference = pvz::RendererPreference::Auto;
	inline static bool mTestForceVulkanInitFailure = false; // 显式测试开关，不影响正常玩家启动
	inline static bool mAutoTestMode = false;         // -AutoTest 自动化测试模式：默认禁存档读写、由 TestDriver 驱动
	inline static bool mAutoTestLoadSave = false;     // -AutoTestLoadSave：仅允许读取当前关卡存档，所有保存/删除仍短路
	inline static bool mDevelopMode = false;          // -develop 开发者模式（RSHIFT 键面板）
	inline static bool mDevNoCooldown = false;        // 开发者作弊：无冷却种植（面板内切换）
	inline static bool mDevFreePlant = false;         // 开发者作弊：无视阳光种植（面板内切换）
	inline static bool mDevSpawnPaused = false;       // 开发者作弊：暂停自然出波（面板内切换；面板「下一波」不受影响）

	static GameAPP& GetInstance();

	int Run();
	bool Initialize();

	// 获取 Graphics 对象
	Graphics& GetGraphics() { return *m_graphics; }

	// AutoTest 与诊断入口
	pvz::CaptureBackend* GetCaptureBackend() const { return m_graphics ? m_graphics->GetCaptureBackend() : nullptr; }
	pvz::VulkanRenderer* GetVulkanRenderer() const { return m_vulkanRenderer.get(); }
	pvz::VulkanContext* GetVulkanContext() const { return m_vulkanCtx.get(); }
	pvz::OpenGLRenderer* GetOpenGLRenderer() const { return m_openGLRenderer.get(); }
	pvz::RendererBackend GetSelectedRenderer() const { return m_selectedRenderer; }
	const std::string& GetVulkanStartupError() const { return m_vulkanStartupError; }

	// 应用新的垂直同步设置：OpenGL 更新 swap interval；Vulkan 热重建 swapchain。
	// 必须在主线程、帧外（不在 BeginFrame..EndFrame 之间）调用。
	bool ApplyVsync(bool vsync);

	// 切换全屏：SDL_SetWindowFullscreen(FULLSCREEN_DESKTOP) + 后端尺寸刷新 + 重算 letterbox。
	// 画面等比居中、UI 逻辑坐标不变、黑边补齐。必须在主线程、帧外调用。
	// 按钮直接调用此函数即可（主人自行接 UI）。
	bool SetFullscreen(bool fullscreen);
	bool IsFullscreen() const { return mFullscreen; }

	// 设置游戏是否运行
	void SetRunning(bool running) { this->mRunning = running; }

	/** 永久记录一次实际刷出的精英舞王，并立即尝试写入 PlayerInfo。 */
	void RecordEliteDancerEncounter();
	bool HasEncounteredEliteDancer() const { return mEncounteredEliteDancer; }
	/** 查询指定冒险关的戴夫闲聊是否已经完成。 */
	bool HasSeenCrazyDaveTutorial(int level) const;
	/** 永久记录指定冒险关的戴夫闲聊已完成，并立即尝试写入 PlayerInfo。 */
	void MarkCrazyDaveTutorialSeen(int level);

	// 世界坐标绘制文本 UTF8编码
	void DrawText(const std::string& text,
		const Vector& position,
		const glm::vec4& color,
		const std::string& fontKey = ResourceKeys::Fonts::FONT_FZCQ,
		int fontSize = 17);

	// 获取当前背景索引(根据关卡)
	Background GetBackgroundID(int level) const;

	// 获取Background是不是夜晚
	bool GetBackgroundIsNight(Background background) const;

	// 获取输入处理器
	InputHandler& GetInputHandler() const {
		if (!mInputHandler) {
			throw std::runtime_error("InputHandler not initialized");
		}
		return *mInputHandler;
	}

	bool IsInputHandlerValid() const { return mInputHandler != nullptr; }

	// 获取窗口 (可能用于其他目的)
	SDL_Window* GetWindow() const { return mWindow; }

	std::shared_ptr<Plant> InstantiatePlant(PlantType plantType, Board* board, int row, int column, bool isPreview = false);
	std::shared_ptr<Zombie> InstantiateZombie(ZombieType zombieType, Board* board, float x, float y, int row, bool isPreview = false);
	// 自由像素摆放：在任意 (x, y) 处生成一只预览/UI 僵尸（row = -1、isPreview = true，不绑定网格行）。
	// 用于卡片选择界面的预览僵尸、图鉴场景等纯展示场合。
	std::shared_ptr<Zombie> InstantiateZombieFree(ZombieType zombieType, Board* board, float x, float y);
};

#endif
