#include "./GameApp.h"
#include "./Renderer/VulkanContext.h"
#include "./Renderer/VulkanRenderer.h"
#include "./Renderer/VulkanTexturePool.h"
#include "./Renderer/OpenGLRenderer.h"
#include "./Renderer/OpenGLTextureBackend.h"
#include <SDL2/SDL_vulkan.h>
#include "./UI/InputHandler.h"
#include "./ResourceManager.h"
#include "./Game/SceneManager.h"
#include "./Game/GameScene.h"
#include "./Game/MainMenuScene.h"
#include "./Game/GameSelectScene.h"
#include "./Game/PlantAlmanacScene.h"
#include "./Game/AlmanacScene.h"
#include "./Game/ZombieAlmanacScene.h"
#include "./CursorManager.h"
#include "./Game/AudioSystem.h"
#include "./DeltaTime.h"
#include "./ParticleSystem/ParticleSystem.h"
#include "./Game/GameObjectManager.h"
#include "./Game/CollisionSystem.h"
#include "./Game/Plant/GameDataManager.h"
#include "./Game/RenderOrder.h"

#include "./Game/AutoTest/TestDriver.h"

#include "./Game/Zombie/Zombie.h"
#include "./Game/Plant/Plant.h"

#include "Game/Board/Board.h"
#include "./Game/AdventureProgression.h"

#include "./Profiler.h"

#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <algorithm>

GameAPP::GameAPP()
	: mInputHandler(nullptr)
	, mWindow(nullptr)
	, mRunning(false)
	, mInitialized(false)
{
	mHaveCards.reserve(64);
	mHaveCards.push_back(PlantType::PLANT_PEASHOOTER);
}

GameAPP::~GameAPP()
{
}

GameAPP& GameAPP::GetInstance()
{
	static GameAPP instance;
	return instance;
}

void GameAPP::RecordEliteDancerEncounter()
{
	if (mEncounteredEliteDancer) return;

	mEncounteredEliteDancer = true;
	// 首次遭遇是低频永久进度；当场保存可避免玩家在离开关卡前退出而丢失解锁。
	if (!mGameInfoSaver.SavePlayerInfo()) {
		LOG_ERROR("GameApp") << "无法立即保存精英舞王遭遇记录，将在后续存档时重试。";
	}
}

bool GameAPP::HasSeenCrazyDaveTutorial(int level) const
{
	return std::binary_search(mCrazyDaveTutorialsSeen.begin(),
		mCrazyDaveTutorialsSeen.end(), level);
}

void GameAPP::MarkCrazyDaveTutorialSeen(int level)
{
	if (!AdventureProgression::IsAdventureLevel(level)) return;
	const auto it = std::lower_bound(mCrazyDaveTutorialsSeen.begin(),
		mCrazyDaveTutorialsSeen.end(), level);
	if (it != mCrazyDaveTutorialsSeen.end() && *it == level) return;

	mCrazyDaveTutorialsSeen.insert(it, level);
	// 闲聊完成是低频永久进度；当场保存，防止离开关卡前退出导致下次重复。
	if (!mGameInfoSaver.SavePlayerInfo()) {
		LOG_ERROR("GameApp") << "无法立即保存疯狂戴夫闲聊记录，将在后续存档时重试。";
	}
}

bool GameAPP::InitializeSDL()
{
#if defined(__ANDROID__)
	// 单指统一由 InputHandler 消费，关闭 SDL 合成鼠标以免重复种植。
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
	{
		LOG_ERROR("GameApp") << "SDL初始化失败: " << SDL_GetError();
		return false;
	}
	return true;
}

bool GameAPP::InitializeSDL_Image()
{
	int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
	int initializedFlags = IMG_Init(imgFlags);

	if ((initializedFlags & imgFlags) != imgFlags) {
		LOG_ERROR("GameApp") << "SDL_image初始化失败，请求: " << imgFlags
			<< "，实际: " << initializedFlags << " - " << IMG_GetError();
		return false;
	}
	return true;
}

bool GameAPP::InitializeSDL_TTF()
{
	if (TTF_Init() == -1)
	{
		LOG_ERROR("GameApp") << "SDL_ttf初始化失败: " << TTF_GetError();
		return false;
	}
	return true;
}

bool GameAPP::InitializeAudioSystem()
{
	if (!AudioSystem::Initialize())
	{
		LOG_WARN("GameApp") << "音频初始化失败，游戏将继续运行但没有声音";
	}
	return true;
}

void GameAPP::DestroyRenderWindow()
{
	m_openGLTextureBackend.reset();
	m_openGLRenderer.reset();
	m_vulkanTexPool.reset();
	m_vulkanRenderer.reset();
	m_vulkanCtx.reset();
	if (mWindow) {
		SDL_DestroyWindow(mWindow);
		mWindow = nullptr;
	}
}

bool GameAPP::TryCreateVulkanRenderer(std::string& error)
{
	mWindow = SDL_CreateWindow(u8"植物大战僵尸中文版",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		SCENE_WIDTH, SCENE_HEIGHT,
		SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!mWindow) {
		error = std::string("Vulkan window: ") + SDL_GetError();
		return false;
	}
	if (mTestForceVulkanInitFailure) {
		error = "stage=VulkanContext errorCode=TEST_FORCED_VULKAN_INIT_FAILURE "
			"detail=-TestVulkanInitFailure 显式模拟初始化失败";
		return false;
	}

#ifdef _DEBUG
	const bool enableValidation = true;
#else
	const bool enableValidation = false;
#endif
	m_vulkanCtx = std::make_unique<pvz::VulkanContext>();
	if (!m_vulkanCtx->Initialize(mWindow, enableValidation, mVsync,
		mForceVulkan12, mForceLegacyRendering, mForceLegacySync)) {
		error = "VulkanContext: " + (m_vulkanCtx->LastError().empty()
			? std::string("初始化失败; SDL=") + SDL_GetError()
			: m_vulkanCtx->LastError());
		return false;
	}
	m_vulkanRenderer = std::make_unique<pvz::VulkanRenderer>();
	if (!m_vulkanRenderer->Initialize(m_vulkanCtx.get())) {
		error = "VulkanRenderer: 帧资源或交换链渲染器初始化失败";
		return false;
	}
	m_vulkanTexPool = std::make_unique<pvz::VulkanTexturePool>();
	if (!m_vulkanTexPool->Initialize(m_vulkanCtx.get())) {
		error = "VulkanTexturePool: bindless 纹理资源初始化失败";
		return false;
	}
	return true;
}

bool GameAPP::TryCreateOpenGLRenderer(std::string& error)
{
	/** 以独立窗口尝试指定 Core Context，避免失败 Context 的 pixel format 污染后备重试。 */
	auto tryContext = [&](int major, int minor, std::string& attemptError) {
		SDL_GL_ResetAttributes();
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
#if defined(__ANDROID__)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
		mWindow = SDL_CreateWindow(u8"植物大战僵尸中文版",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			SCENE_WIDTH, SCENE_HEIGHT,
			SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
		if (!mWindow) {
			attemptError = "OpenGL " + std::to_string(major) + "." + std::to_string(minor)
				+ " window: " + SDL_GetError();
			return false;
		}
		m_openGLRenderer = std::make_unique<pvz::OpenGLRenderer>();
		if (!m_openGLRenderer->Initialize(mWindow, mVsync, attemptError)) return false;
		m_openGLTextureBackend = std::make_unique<pvz::OpenGLTextureBackend>();
		if (!m_openGLTextureBackend->Initialize(m_openGLRenderer->Api())) {
			attemptError = "OpenGLTextureBackend: GL_MAX_TEXTURE_SIZE 或纹理入口无效";
			return false;
		}
		return true;
	};

#if defined(__ANDROID__)
	return tryContext(3, 0, error);
#else
	std::string fastError;
	if (!mForceOpenGL33 && tryContext(4, 3, fastError)) return true;
	if (!mForceOpenGL33) {
		LOG_WARN("OpenGL") << "OpenGL 4.3/SSBO Context 不可用，回落 3.3 CPU Batch: " << fastError;
		DestroyRenderWindow();
	}
	std::string compatibilityError;
	if (tryContext(3, 3, compatibilityError)) return true;
	error = mForceOpenGL33 ? compatibilityError
		: "OpenGL 4.3: " + fastError + "; OpenGL 3.3: " + compatibilityError;
	return false;
#endif
}

bool GameAPP::CreateWindowAndRenderer()
{
#if defined(__ANDROID__)
	// 首版移动端固定 GLES：不枚举 Vulkan 设备，也不依赖旧驱动的 bindless 能力。
	mRendererPreference = pvz::RendererPreference::OpenGL;
#endif
	LOG_WARN("Startup") << "Renderer requested=" << pvz::RendererPreferenceName(mRendererPreference)
		<< " SDL video driver=" << (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown");

	bool created = false;
	if (mRendererPreference != pvz::RendererPreference::OpenGL) {
		created = TryCreateVulkanRenderer(m_vulkanStartupError);
		if (created) {
			m_selectedRenderer = pvz::RendererBackend::Vulkan;
		}
		else {
			LOG_WARN("Startup") << "Vulkan 初始化失败: " << m_vulkanStartupError;
			DestroyRenderWindow();
			SDL_Vulkan_UnloadLibrary();
			if (mRendererPreference == pvz::RendererPreference::Vulkan) {
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Vulkan 初始化失败",
					m_vulkanStartupError.c_str(), nullptr);
				return false;
			}
			LOG_WARN("Startup") << "Renderer auto: 完整清理 Vulkan 后尝试 OpenGL Core（SSBO 可选）";
		}
	}

	if (!created) {
		created = TryCreateOpenGLRenderer(m_openGLStartupError);
		if (created) {
			m_selectedRenderer = pvz::RendererBackend::OpenGL;
		}
		else {
			LOG_ERROR("Startup") << "OpenGL 初始化失败: " << m_openGLStartupError;
			DestroyRenderWindow();
			const std::string message = "无法初始化渲染器。\nVulkan: "
				+ (m_vulkanStartupError.empty() ? std::string("未请求") : m_vulkanStartupError)
				+ "\nOpenGL: " + m_openGLStartupError;
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "渲染器初始化失败", message.c_str(), nullptr);
			return false;
		}
	}

	m_graphics = std::make_unique<Graphics>();
	if (!m_graphics->Initialize(SCENE_WIDTH, SCENE_HEIGHT)) {
		LOG_ERROR("GameApp") << "Graphics 初始化失败";
		DestroyRenderWindow();
		return false;
	}
	const bool graphicsReady = m_selectedRenderer == pvz::RendererBackend::Vulkan
		? m_graphics->InitializeVulkan(m_vulkanCtx.get(), m_vulkanRenderer.get(), m_vulkanTexPool.get())
		: m_graphics->InitializeOpenGL(m_openGLRenderer.get(), m_openGLTextureBackend.get());
	if (!graphicsReady) {
		LOG_ERROR("GameApp") << "Graphics 后端接入失败: " << pvz::RendererBackendName(m_selectedRenderer);
		m_graphics.reset();
		DestroyRenderWindow();
		return false;
	}

	if (m_selectedRenderer == pvz::RendererBackend::Vulkan) {
		m_graphics->SetInstancePathEnabled(!mDisableInstancePath);
		if (mDisableInstancePath) LOG_WARN("GameApp") << "Vulkan -NoInstance: reanim 使用 CPU slow path";
	}
	else {
		m_graphics->SetInstancePathEnabled(false);
		if (mDisableInstancePath) {
			LOG_WARN("GameApp") << "OpenGL -NoInstance: 兼容后端默认使用 CPU Batch，画面路径不变";
		}
	}

	m_graphics->SetClearColor(0, 0, 0, 255);
	m_graphics->RecomputeLetterbox();
	LOG_WARN("Startup") << "Renderer selected=" << pvz::RendererBackendName(m_selectedRenderer);
#if defined(_WIN32)
	if (m_selectedRenderer == pvz::RendererBackend::OpenGL) {
		LOG_WARN("Startup") << "OpenGL path Vulkan runtime loaded="
			<< (GetModuleHandleW(L"vulkan-1.dll") ? "yes" : "no");
	}
#endif
	return true;
}

bool GameAPP::InitializeResourceManager()
{
	if (!CursorManager::GetInstance().Initialize()) {
		LOG_ERROR("GameApp") << "光标管理器创建失败！";
		return false;
	}

	CursorManager::GetInstance().SetDefaultCursor();

	GameDataManager& plantMgr = GameDataManager::GetInstance();
	if (!plantMgr.Initialize()) {
		LOG_ERROR("GameApp") << "GameDataManager 初始化失败：gamedata.json 缺失或校验未通过";
		return false;
	}

	ResourceManager& resourceManager = ResourceManager::GetInstance();

	// 先注入选中后端的纹理生命周期接口，再读取/上传资源。
	resourceManager.SetTextureBackend(m_selectedRenderer == pvz::RendererBackend::Vulkan
		? static_cast<pvz::TextureBackend*>(m_vulkanTexPool.get())
		: static_cast<pvz::TextureBackend*>(m_openGLTextureBackend.get()));

	if (!resourceManager.Initialize("./resources/resources.xml")) {
		LOG_ERROR("GameApp") << "ResourceManager 初始化失败！";
		return false;
	}

	return true;
}

bool GameAPP::LoadAllResources()
{
	ResourceManager& resourceManager = ResourceManager::GetInstance();
	bool resourcesLoaded = true;

	using Clock = std::chrono::steady_clock;
	auto timedPhase = [](auto&& fn, double& outSec) {
		const auto t0 = Clock::now();
		const bool ok = fn();
		outSec = std::chrono::duration<double>(Clock::now() - t0).count();
		return ok;
	};

	double tImg = 0, tReanimImg = 0, tParticle = 0, tFont = 0, tSound = 0, tMusic = 0, tReanim = 0;
	resourcesLoaded &= timedPhase([&] { return resourceManager.LoadAllGameImages(); }, tImg);
	resourcesLoaded &= timedPhase([&] { return resourceManager.LoadAllImagesFromPath(); }, tReanimImg);
	resourcesLoaded &= timedPhase([&] { return resourceManager.LoadAllParticleTextures(); }, tParticle);
	resourcesLoaded &= timedPhase([&] { return resourceManager.LoadAllFonts(); }, tFont);
	resourcesLoaded &= timedPhase([&] { return resourceManager.LoadAllSounds(); }, tSound);
	resourcesLoaded &= timedPhase([&] { return resourceManager.LoadAllMusic(); }, tMusic);
	resourcesLoaded &= timedPhase([&] { return resourceManager.LoadAllReanimations(); }, tReanim);

	// Release 编译期裁掉 INFO 以下，这行是采集玩家冷启动耗时的唯一通道，故用 WARN。
	const double total = tImg + tReanimImg + tParticle + tFont + tSound + tMusic + tReanim;
	char summary[256];
	std::snprintf(summary, sizeof(summary),
		"资源加载 %.1fs: 图片 %.1f / reanim图 %.1f / 粒子 %.1f / 字体 %.1f / 音效 %.1f / 音乐 %.1f / 动画 %.1f",
		total, tImg, tReanimImg, tParticle, tFont, tSound, tMusic, tReanim);
	LOG_WARN("Startup") << summary;

	if (!resourcesLoaded)
	{
		LOG_ERROR("GameApp") << "资源加载失败！";
		return false;
	}

	// 所有 reanim 与其部件纹理就绪后，构建图集页（消除批渲染的 32 纹理单元抖动）
	resourceManager.BuildReanimAtlases();

	return true;
}

bool GameAPP::Initialize()
{
	if (mInitialized) return true;

	// 设置默认字体路径
	Button::SetDefaultFontPath(ResourceKeys::Fonts::FONT_FZCQ);

	mInitialized = true;
	return true;
}

/** 初始化并驱动固定步长主循环；Android 后台停止提交画面，恢复时丢弃墙钟欠账。 */
int GameAPP::Run()
{
	// 初始化各个系统
	if (!InitializeSDL()) return -1;
	if (!InitializeSDL_Image()) {
		SDL_Quit();
		return -2;
	}
	if (!InitializeSDL_TTF()) {
		IMG_Quit();
		SDL_Quit();
		return -3;
	}
	if (!InitializeAudioSystem()) {
		// 音频失败仍继续
	}

	// 玩家存档需要在创建 swapchain 之前加载，否则 mVsync 还是默认值，present mode 选错。
	if (!mGameInfoSaver.LoadPlayerInfo())
	{
		LOG_WARN("GameApp") << "无法加载玩家存档数据！可能是没有存档!";
	}
	// 自动测试默认静音音乐，覆盖本次进程的加载值；普通游玩偏好与音效音量不变。
	if (mAutoTestMode) AudioSystem::SetMusicVolume(0.0f);

	// 初始化 GameAPP 自身
	if (!Initialize()) {
		CleanupResources();
		TTF_Quit();
		IMG_Quit();
		SDL_Quit();
		return -4;
	}

	// 创建窗口和渲染器
	if (!CreateWindowAndRenderer()) {
		CleanupResources();
		AudioSystem::Shutdown();
		TTF_Quit();
		IMG_Quit();
		SDL_Quit();
		return -5;
	}

	// 初始化资源管理器
	if (!InitializeResourceManager()) {
		CleanupResources();
		AudioSystem::Shutdown();
		m_graphics.reset();
		DestroyRenderWindow();
		TTF_Quit();
		IMG_Quit();
		SDL_Quit();
		return -6;
	}

	// 加载所有资源
	if (!LoadAllResources()) {
		CleanupResources();
		AudioSystem::Shutdown();
		CursorManager::GetInstance().Cleanup();
		m_graphics.reset();
		DestroyRenderWindow();
		TTF_Quit();
		IMG_Quit();
		SDL_Quit();
		return -7;
	}

	mInputHandler = std::make_unique<InputHandler>(m_graphics.get());
	g_particleSystem = std::make_unique<ParticleSystem>(m_graphics.get());

	if (g_particleSystem) {
		g_particleSystem->LoadXMLConfigs("./resources/particles/config");
	}

	// 主体与 UI GameObject 之间依次合成世界粒子、天气覆盖层和 Scene UI 贴图。
	GameObjectManager::GetInstance().SetPreOverlayHook([this] {
		// 世界粒子先参与战场合成，再由天气暗幕统一压暗。
		{
			PROFILE_SCOPE("8a.Draw_worldParticles");
			if (g_particleSystem) {
				g_particleSystem->DrawBelow(LAYER_UI);
			}
		}
		{
			PROFILE_SCOPE("8b.Draw_worldOverlay");
			SceneManager::GetInstance().DrawWorldOverlay(m_graphics.get());
		}
		// Scene 的 UI 坐标贴图（当前为卡槽底板）必须在天气暗幕后、UI GameObject 前绘制。
		{
			PROFILE_SCOPE("8c.Draw_sceneUITextures");
			SceneManager::GetInstance().DrawUITextures(m_graphics.get());
		}
	});

	auto& sceneManager = SceneManager::GetInstance();

	sceneManager.RegisterScene<MainMenuScene>("MainMenuScene");
	sceneManager.RegisterScene<GameSelectScene>("GameSelectScene");
	sceneManager.RegisterScene<AlmanacScene>("AlmanacScene");
	sceneManager.RegisterScene<GameScene>("GameScene");
	sceneManager.RegisterScene<PlantAlmanacScene>("PlantAlmanacScene");
	sceneManager.RegisterScene<ZombieAlmanacScene>("ZombieAlmanacScene");

	sceneManager.SwitchTo("MainMenuScene");

	DeltaTime::Reset();

	// 按存档应用全屏设置（mFullscreen 已在 LoadPlayerInfo 时读入）。
	// 放在主循环前、所有渲染资源就绪后执行（帧外，安全）。
	if (mFullscreen) {
		SetFullscreen(true);
	}

	mRunning = true;
	SDL_Event event;
#if defined(__ANDROID__)
	bool appInBackground = false;
	bool skipResumedFrame = false;
	bool contextLost = false;
#endif

	while (mRunning && !sceneManager.IsEmpty())
	{
		// 固定步长：BeginFrame 折算本渲染帧应执行的逻辑步数（0..3，超出丢债=慢动作退化）
		const int logicSteps = DeltaTime::BeginFrame();

		// 处理事件（每渲染帧至少轮询一次，保证 0 步帧窗口消息也被泵送）
		auto pollEvents = [&]() {
			PROFILE_SCOPE("A.InputPoll");
			while (SDL_PollEvent(&event))
			{
#if defined(__ANDROID__)
				if (event.type == SDL_APP_WILLENTERBACKGROUND) {
					appInBackground = true;
					mInputHandler->ResetInput();
					AudioSystem::PauseMusic();
					Mix_Pause(-1);
					// 在逻辑步之间保存，降低后台被系统回收时的进度损失；不保存胜负结算态。
					mGameInfoSaver.SavePlayerInfo();
					if (auto* game = dynamic_cast<GameScene*>(sceneManager.GetCurrentScene())) {
						if (game->GetBoard() && game->GetCardSlotManager()
							&& game->GetBoard()->mBoardState == BoardState::GAME) {
							mGameInfoSaver.SaveLevelData(game->GetBoard(), game->GetCardSlotManager());
						}
					}
					continue;
				}
				if (event.type == SDL_APP_DIDENTERFOREGROUND) {
					appInBackground = false;
					skipResumedFrame = true;
					DeltaTime::ResetFrameClock();
					mInputHandler->ResetInput();
					if (!DeltaTime::IsPaused()) AudioSystem::ResumeMusic();
					Mix_Resume(-1);
					m_graphics->RecomputeLetterbox();
					continue;
				}
				if (event.type == SDL_RENDER_DEVICE_RESET) {
					// 首版尚不支持丢失 Context 后重建资源；明确退出，不能继续使用失效 GL 对象。
					LOG_ERROR("Android") << "GL Context lost; restart the application";
					contextLost = true;
					mRunning = false;
					continue;
				}
#endif
				if (event.type == SDL_QUIT)
				{
					mRunning = false;
				}
				else if (event.type == SDL_WINDOWEVENT
					&& (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
						|| event.window.event == SDL_WINDOWEVENT_RESIZED)) {
					if (m_graphics && m_selectedRenderer == pvz::RendererBackend::OpenGL) {
						m_graphics->RecomputeLetterbox();
					}
				}
				mInputHandler->ProcessEvent(&event);
			}
		};
		pollEvents();
		if (!mRunning) break;
#if defined(__ANDROID__)
		if (appInBackground || skipResumedFrame) {
			skipResumedFrame = false;
			SDL_Delay(10);
			continue;
		}
#endif

		// 更新：每逻辑步 dt 恒为 1/60 × timeScale（暂停为 0）
		{
			PROFILE_SCOPE("B.SceneUpdate_total");
			for (int i = 0; i < logicSteps && mRunning && !sceneManager.IsEmpty(); ++i)
			{
				// 不变量：每个逻辑步之前都有一次 poll。追帧的第 2/3 步前补一次轮询，
				// 否则上一步内推送的合成输入（TestDriver key/click 跨步状态机）会与
				// 其收尾事件挤进下帧同一批 poll，按下沿未被任何逻辑步观察就被改写湮灭
				if (i > 0) pollEvents();
				if (!mRunning) break;
#if defined(__ANDROID__)
				if (appInBackground || skipResumedFrame) break;
#endif
				auto& testDriver = TestDriver::GetInstance();
				// 信箱等待期间跳过整个场景更新，连按帧计数的技能也冻结；SDL 退出事件与绘制仍正常。
				if (testDriver.ShouldUpdateScene()) {
					DeltaTime::BeginStep();
					CursorManager::GetInstance().ResetHoverCount();
					sceneManager.Update();
					CursorManager::GetInstance().Update();
					testDriver.OnSceneUpdated();
				}
				testDriver.Update();   // 非 AutoTest 模式下首行 !mActive 即返回
				// 边沿衰减（PRESSED→DOWN 等）每逻辑步一次：保证一次点击恰好被一个
				// 逻辑步消费——追帧补 2~3 步时不会把同一次点击种成两棵植物；
				// 本帧 0 步时边沿保留到下一步，点击不会丢
				mInputHandler->Update();
			}
		}

		if (!mRunning) break;
#if defined(__ANDROID__)
		if (appInBackground || skipResumedFrame) {
			skipResumedFrame = false;
			continue;
		}
#endif
		// 渲染
		{
			PROFILE_SCOPE("C.SceneDraw_total");
			Draw();
		}

		if (GameAPP::GetInstance().mDebugMode)
		{
			static int MousePoint = 0;
			if (MousePoint++ % 40 == 0)
			{
				Vector mousePos = mInputHandler->GetMouseWorldPosition();
				LOG_WARN("GameApp") << "Mouse World Position: " << mousePos.x << "，" << mousePos.y;
				LOG_WARN("GameApp") << "Mouse Screen Position: " << mInputHandler->GetMousePosition().x << ", "
					<< mInputHandler->GetMousePosition().y;
			}
		}

		Profiler::Get().EndFrame();
	}

	// 清理
	Shutdown();

#if defined(__ANDROID__)
	if (contextLost) return -8;
#endif
	return 0;
}

void GameAPP::Draw()
{
	// Phase 3b：Graphics 接管帧生命周期。BeginFrame 负责 acquire+begin+barrier+beginRendering，
	// SceneManager::Draw 累积 batch，EndFrame 把 batch 拷到 GPU、issue draw、submit、present。
	m_graphics->Clear();

	bool ok;
	{
		PROFILE_SCOPE("C1.BeginFrame");
		ok = m_graphics->BeginFrame();
	}
	if (!ok) {
		// acquire 报 OUT_OF_DATE 等：BeginFrame 已置 NeedsSwapchainRebuild，下面统一处理。
	}
	else {
		{
			PROFILE_SCOPE("C2.SceneManagerDraw");
			SceneManager::GetInstance().Draw(m_graphics.get());
		}
		{
			PROFILE_SCOPE("C3.EndFrame_Present");
			m_graphics->EndFrame();
		}
	}

	// 帧外消化 swapchain rebuild 请求（OUT_OF_DATE / SUBOPTIMAL）。vsync 主动切换走 ApplyVsync 直接重建，
	// 这里只兜底未来的窗口大小变化、Alt+Tab 全屏切换等情况。
	// 注意：RecreateSwapchain 在窗口最小化/隐藏（extent=0x0）时返回 false 且不销毁旧 swapchain，
	// 此时跳过 OnSwapchainRecreated，保留 rebuild 标志，下一帧继续重试。
	if (m_vulkanRenderer && m_vulkanRenderer->NeedsSwapchainRebuild()) {
		if (m_vulkanCtx->RecreateSwapchain(mVsync)) {
			m_vulkanRenderer->OnSwapchainRecreated();
			m_vulkanRenderer->ClearSwapchainRebuildFlag();
			// 交换链尺寸可能变了（窗口拉伸 / Alt+Tab 全屏切换的兜底路径），重算 letterbox。
			if (m_graphics) m_graphics->RecomputeLetterbox();
		}
	}
}

bool GameAPP::ApplyVsync(bool vsync)
{
	if (m_selectedRenderer == pvz::RendererBackend::OpenGL) {
		if (!m_openGLRenderer) return false;
		std::string error;
		if (!m_openGLRenderer->ApplyVsync(vsync, error)) {
			LOG_ERROR("GameApp") << error;
			return false;
		}
		mVsync = vsync;
		LOG_WARN("OpenGL") << "VSync=" << (mVsync ? "on" : "off");
		return true;
	}
	if (!m_vulkanCtx || !m_vulkanRenderer) return false;
	mVsync = vsync;
	if (!m_vulkanCtx->RecreateSwapchain(mVsync)) return false;
	if (!m_vulkanRenderer->OnSwapchainRecreated()) return false;
	m_vulkanRenderer->ClearSwapchainRebuildFlag();
	if (m_graphics) m_graphics->RecomputeLetterbox();
	return true;
}

bool GameAPP::SetFullscreen(bool fullscreen)
{
	if (!mWindow || !m_graphics) return false;

	// FULLSCREEN_DESKTOP：沿用桌面分辨率、不切显示模式、Alt-Tab 顺滑。0 = 还原窗口。
	Uint32 flag = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
	if (SDL_SetWindowFullscreen(mWindow, flag) != 0) {
		LOG_ERROR("GameApp") << "SDL_SetWindowFullscreen 失败: " << SDL_GetError();
		return false;
	}
	mFullscreen = fullscreen;
	if (m_selectedRenderer == pvz::RendererBackend::OpenGL) {
		SDL_PumpEvents();
		m_graphics->RecomputeLetterbox();
		return true;
	}

	// 交换链尺寸随之改变，需热重建并重算 letterbox（缩放比 + 黑边偏移）。
	if (!m_vulkanCtx || !m_vulkanRenderer) return false;
	if (!m_vulkanCtx->RecreateSwapchain(mVsync)) return false;
	if (!m_vulkanRenderer->OnSwapchainRecreated()) return false;
	m_vulkanRenderer->ClearSwapchainRebuildFlag();
	m_graphics->RecomputeLetterbox();
	return true;
}

void GameAPP::Shutdown()
{
	if (mRunning) return;

	if (!mGameInfoSaver.SavePlayerInfo()) {
		LOG_ERROR("GameApp") << "无法保存玩家数据！ 你的数据将会清空！";
	}

	// 清理粒子系统
	g_particleSystem.reset();

	SceneManager::GetInstance().ClearCurrentScene();

	// 清理游戏对象和碰撞系统
	GameObjectManager::GetInstance().ClearAll();
	CollisionSystem::GetInstance().ClearAll();

	// 资源与文字缓存都必须在当前 TextureBackend/Context 仍存活时释放。
	ResourceManager::ReleaseInstance();

	// 清理文字缓存 (Graphics 内部有缓存)
	if (m_graphics) {
		m_graphics->ClearTextCache();
	}

	// 清理音频系统
	AudioSystem::Shutdown();

	// 清理输入处理器
	mInputHandler.reset();

	// Graphics 先释放白纹理和后端专用 pipeline/buffer。
	m_graphics.reset();

	// GL: texture backend → renderer/context → window；Vulkan: pool → renderer → context → window。
	DestroyRenderWindow();

	// 清理光标管理器
	CursorManager::GetInstance().Cleanup();

	// 清理 SDL 子系统
	TTF_Quit();
	IMG_Quit();
	SDL_Quit();

	mRunning = false;
	mInitialized = false;
}

void GameAPP::CleanupResources()
{
	ResourceManager::GetInstance().CleanupUnusedFontSizes();
}

void GameAPP::DrawText(const std::string& text, const Vector& position,
	const glm::vec4& color,
	const std::string& fontKey, int fontSize)
{
	if (!m_graphics) return;
	m_graphics->DrawText(text, fontKey, fontSize, color, position.x, position.y);
}

Background GameAPP::GetBackgroundID(int level) const
{
	// 生存模式使用独立关卡号，不参与九关制冒险分段。
	if (const auto* definition = FindSurvivalEndlessDefinition(level)) {
		return definition->background;
	}

	const int area = AdventureProgression::GetAreaNumber(level);
	switch (area) {
	case 1:
		return Background::GROUND_DAY;
	case 2:
		return Background::GROUND_NIGHT;
	case 3:
		return Background::WATER_POOL;
	case 4:
		return Background::NIGHT_WATER_POOL;
	case 5:
		// 第五大关完整使用白天屋顶；5-9 的 BOSS 槽位不再通过切夜景表达。
		return Background::ROOF;
	case 6:
		// 第六大关完整使用黑夜屋顶；雷荷与坡面径流均由场景能力自动启用。
		return Background::NIGHT_ROOF;
	case 7:
		// 第七大关使用平地冬日花园；寒潮、冻融线与雨雪表现由 Board 独立管理。
		return Background::WINTER_GARDEN;
	case 9:
		return Background::GLOOMCRYSTAL_MINE;
	case 10:
		return Background::HOT_COLD_STORAGE;
	case 8:
		// 第八大关是极夜开放雪原；本地三仪表与白毛风不复用旧天气状态。
		return Background::POLAR_NIGHT_SNOWFIELD;
	default:
		return Background::GROUND_DAY;
	}
}

bool GameAPP::GetBackgroundIsNight(Background background) const
{
	if (background == Background::GROUND_NIGHT || background == Background::NIGHT_WATER_POOL
		|| background == Background::NIGHT_ROOF
		|| background == Background::POLAR_NIGHT_SNOWFIELD
		|| background == Background::GLOOMCRYSTAL_MINE)
	{
		return true;
	}
	else
	{
		return false;
	}
}

std::shared_ptr<Plant> GameAPP::InstantiatePlant(PlantType plantType, Board* board, int row, int column, bool isPreview)
{
	return GameDataManager::GetInstance().CreatePlant(plantType, board, row, column, isPreview);
}

std::shared_ptr<Zombie> GameAPP::InstantiateZombie(ZombieType zombieType, Board* board, float x, float y, int row, bool isPreview)
{
	return GameDataManager::GetInstance().CreateZombie(zombieType, board, x, y, row, isPreview);
}

std::shared_ptr<Zombie> GameAPP::InstantiateZombieFree(ZombieType zombieType, Board* board, float x, float y)
{
	// row = -1 表示不绑定网格行，直接采用传入的像素 (x, y)；isPreview = true 走纯展示初始化路径。
	return InstantiateZombie(zombieType, board, x, y, -1, true);
}
