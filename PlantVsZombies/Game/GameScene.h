#pragma once
#ifndef _GAMESCENE_H
#define _GAMESCENE_H
#include "Scene.h"
#include "Game/Board/BoardPresentation.h"
#include "Game/Board/Board.h"
#include "Perk/PerkType.h"
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

class ChooseCardUI;
class CardSlotManager;
class CrazyDaveDialog;
class GameProgress;
class ShovelBank;

constexpr float mBackgroundY = -50;

// 一大波、最后一波与天气警报共用的提示动画阶段
enum class PromptStage {
	NONE,
	APPEAR,
	HOLD,
	FADE_OUT
};

enum class PromptContentType {
	IMAGE,
	TEXT
};

enum class PromptPurpose {
	GENERIC,
	HEAVY_RAIN_WARNING
};

// 开场动画阶段
enum class IntroStage {
	BACKGROUND_MOVE,   // 背景移动
	SEEDBANK_SLIDE,    // 种子槽滑落
	COMPLETE,           // 完成，显示阳光
	READY_SET_PLANT,    // 准备种植植物，选好卡了
	FINISH              // 最终完成
};

// 提示动画数据；vector 中越晚加入的条目越晚绘制，因而视觉优先级越高。
struct PromptAnimation {
	bool active = false;
	PromptStage stage = PromptStage::NONE;
	PromptContentType contentType = PromptContentType::IMAGE;
	float timer = 0.0f;
	float scale = 1.0f;
	Uint8 alpha = 255;
	std::string content;         // 图片资源键或 UTF-8 文本
	glm::vec4 textColor{ 255.0f, 255.0f, 255.0f, 255.0f };
	int fontSize = 40;
	float appearDuration = 1.0f; // 出现阶段时长
	float holdDuration = 3.0f;    // 停留阶段时长
	float fadeDuration = 1.0f;    // 消失阶段时长
	bool useUnscaledTime = false; // true 时高倍速不压缩时长；暂停时冻结并保留当前画面
	PromptPurpose purpose = PromptPurpose::GENERIC; // 仅用于精确撤下已失效的专用提示
};

// 首领血条完全由当前 Board 实体派生，不复制玩法生命状态；布局字段供 AutoTest 锁定屏幕位置。
struct RoofMarshalBossHealthBarState {
	bool visible = false;
	int currentHealth = 0;
	int maxHealth = 0;
	int highThreatThreshold = 0;
	int desperateThreshold = 0;
	float fillRatio = 0.0f;
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

class GameScene : public Scene, public BoardPresentation {
public:
	GameScene();
	~GameScene() override;

	void OnEnter() override;
	void OnExit() override;
	void Update() override;
	// 覆写以支持屏幕抖动：抖动经相机（projView）作用于全部绘制管线，
	// 含不消费变换栈的 reanim/字形 GPU instancing 快路径（详见 .cpp 实现注释）
	void Draw(Graphics* g) override;
	void DrawWorldOverlay(Graphics* g) override;
	/** 返回白天/夜间屋顶雨景背景的交叉淡化 alpha；非屋顶恒为 0。 */
	float GetRoofRainBackgroundAlpha() const;

	void ChooseCardComplete();  // 选卡完成

	// ---- AutoTest 钩子 ----
	// GetBoard / GetChooseCardUI / IsChooseCardReady 本身只读；
	// AutoTest 经由它们配合 ChooseCardComplete()（与 UI 共用）驱动选卡流程。
	Board* GetBoard() const { return mBoard.get(); }
	ChooseCardUI* GetChooseCardUI() const { return mChooseCardUI; }
	CardSlotManager* GetCardSlotManager() const { return mCardSlotManager.get(); }
	// 选卡界面是否就绪（卡牌已铺开、"一起摇滚吧"可点）
	bool IsChooseCardReady() const {
		return mCurrentStage == IntroStage::COMPLETE && mChooseCardUI != nullptr;
	}
	/** AutoTest 显式打开当前关卡的戴夫闲聊；force=true 时忽略已读状态。 */
	bool StartCrazyDaveDialogForTesting(bool force = true);
	bool AdvanceCrazyDaveDialogForTesting();
	bool SkipCrazyDaveDialogForTesting();
	bool IsCrazyDaveDialogSupported() const;
	bool IsCrazyDaveDialogActive() const;
	std::string GetCrazyDaveDialogPhaseName() const;
	int GetCrazyDaveDialogMessageIndex() const;
	int GetCrazyDaveDialogMessageCount() const;
	std::string GetCrazyDaveDialogText() const;
	std::string GetCrazyDaveDialogTrackName() const;
	std::string GetCrazyDaveDialogVoiceSoundKey() const;
	std::string GetCrazyDaveDialogVoiceGroupName() const;
	int GetCrazyDaveDialogRenderedQuadCount() const;
	bool HasCrazyDaveDialogRenderedGeometry() const;
	bool DidCrazyDaveDialogUseInstancePath() const;

	GameProgress* GetGameProgress() const;

	void GameOver() override;
	void TogglePlanternGearMenu() override;

	// 生存模式：一轮清空后重新进入选卡子流程（同会话轮间，轻量路径）
	void BeginSurvivalCardSelect();

	static constexpr int SURVIVAL_PERK_PICKS_PER_ROUND = 2;
	static constexpr int SURVIVAL_PERK_REFRESHES_PER_ROUND = 3;
	void BeginSurvivalPerkSelect() override; // 轮清后开始最多两次成对词条选择（选卡之前）
	void ApplyPerkSelection(int index);      // 合法 index 应用一对；负数/越界只放弃当前一次机会
	bool RefreshSurvivalPerkSelection();     // 消耗整轮共享的一次刷新额度并重抽当前候选；不可刷新时返回 false
	void OpenPerkView();                      // 生存模式：弹出已选词条查看面板（固定面板+字号自动缩放，打开即暂停）
	void ClosePerkView();                     // 关闭词条查看面板并恢复
	void RenderPerkViewPage();                // 按 mPerkViewPage 重建词条查看面板（分页；OpenPerkView 主体）
	// AutoTest 内省接口（对真实游戏无副作用）
	bool IsPerkSelectActive() const { return mSurvivalPerkSelectActive; }
	int GetPerkCurrentPick() const { return mSurvivalPerkSelectActive ? mSurvivalPerkStepsCompleted + 1 : 0; }
	int GetPerkStepsCompleted() const { return mSurvivalPerkStepsCompleted; }
	int GetPerkPicksCompleted() const { return mSurvivalPerkPicksCompleted; }
	int GetPerkRefreshesRemaining() const { return mSurvivalPerkRefreshesRemaining; }
	const std::vector<PerkPairing>& GetCurrentPerkOffer() const { return mCurrentPerkOffer; }
	/** AutoTest 下一次重抽专用：-1 恢复正式概率，0/1 分别强制普通/稀有面板。 */
	void SetNextPerkRareRollOverrideForTesting(int value) {
		mNextPerkRareRollOverride = value < 0 ? -1 : (value > 0 ? 1 : 0);
	}

	/** 注册玩家阳光 HUD；数字按纸条内框自动选字号并保持水平、垂直居中。 */
	void ShowSunCount();

	/** 添加图片提示；不会覆盖正在播放的其他提示。 */
	void ShowPrompt(const std::string& textureKey,
		float appearDur = 1.0f,
		float holdDur = 3.0f,
		float fadeDur = 1.0f) override;
	/** 添加居中的紧急文字提示；同帧内越晚添加的提示绘制优先级越高。 */
	void ShowTextPrompt(const std::string& text, const glm::vec4& color,
		int fontSize = 40,
		float appearDur = 0.3f,
		float holdDur = 3.8f,
		float fadeDur = 0.6f,
		bool useUnscaledTime = false,
		PromptPurpose purpose = PromptPurpose::GENERIC);
	/** 显示“第 N 行全军突击”短警报；持续红旗负责表达后续强化状态。 */
	void ShowRoofMarshalAssaultWarning(int row, float duration) override;
	/** 显示路灯花低燃料的大号红色中央警报。 */
	void ShowPlanternLowFuelWarning() override;
	/** 根据已锁定的台风等级与同级文案编号显示大雨来临警报。 */
	void ShowHeavyRainWarning(TyphoonStrength strength, int variant) override;
	/** 只撤下大雨/暴雪预警，不影响大波、突击令和低燃料等并存提示。 */
	void CancelHeavyRainWarning() override;
	/** 撤下天气实况牌和预报失败卡；其他 UI 与温度计保持独立。 */
	void CancelWeatherInformationNotices() override;
	const std::vector<PromptAnimation>& GetPromptsForTesting() const { return mPrompts; }

	// 全屏白闪（寒冰菇全场冻结的瞬间反馈）：alpha 从峰值线性衰减到 0
	void ShowScreenFlash(
		float duration = 0.5f, float peakAlpha = 200.0f) override;
	bool IsScreenFlashActive() const { return mScreenFlashTimer > 0.0f; }
	float GetScreenFlashPeakAlpha() const { return mScreenFlashPeakAlpha; }
	/** 生成一次大雨闪电的固定分叉路径，并播放主放电与回闪局部照明。 */
	void ShowLightningStrike(float duration = 0.42f) override;
	bool IsLightningStrikeActive() const { return mLightningFlashTimer > 0.0f; }
	int GetLightningMainSegmentCount() const {
		return mLightningMainPath.empty()
			? 0 : static_cast<int>(mLightningMainPath.size()) - 1;
	}
	int GetLightningBranchSegmentCount() const {
		return static_cast<int>(mLightningBranches.size());
	}
	float GetLightningStrikeX() const { return mLightningStrikePoint.x; }
	/** 天气或台风等级揭晓与公开预报不一致时，显示一个不会暂停战斗的失败提示。 */
	void ShowWeatherForecastFailure(
		RainIntensity forecast, RainIntensity actual,
		TyphoonStrength forecastTyphoon, TyphoonStrength actualTyphoon) override;
	/** 从关卡存档恢复失败提示的剩余时间及预报/实际复合天气。 */
	void RestoreWeatherForecastFailure(float remaining,
		RainIntensity forecast, RainIntensity actual,
		TyphoonStrength forecastTyphoon, TyphoonStrength actualTyphoon);
	/** 天气发生变化后短暂显示当前结果，持续时间由天气 UI 常量统一控制。 */
	void ShowCurrentWeatherNotice() override;
	/** 从关卡存档恢复当前天气展板的剩余显示时间；异常值会限制到 0～5 秒。 */
	void RestoreCurrentWeatherNotice(float remaining);
	float GetWeatherPanelSlide() const { return mWeatherPanelSlide; }
	/** 返回天气面板当前实际绘制的公开预报文字，供展示与 AutoTest 共用口径。 */
	std::string GetWeatherForecastPanelText() const;
	/** 返回寒潮准确预报或当前阶段文字；无需显示时为空。 */
	std::string GetColdWavePanelText() const;
	/** 当前有效劫持者已锁定时才显示处决线。 */
	bool IsNightRoofExecutionLineVisible() const;
	/** 返回按当前可见详情动态计算的天气面板高度。 */
	float GetWeatherPanelHeight() const;
	float GetCurrentWeatherNoticeTimer() const { return mCurrentWeatherNoticeTimer; }
	bool IsCurrentWeatherNoticeActive() const { return mCurrentWeatherNoticeTimer > 0.0f; }
	bool IsWeatherForecastFailureActive() const { return mWeatherForecastFailureTimer > 0.0f; }
	float GetWeatherForecastFailureTimer() const { return mWeatherForecastFailureTimer; }
	RainIntensity GetFailedForecastRainIntensity() const { return mFailedForecastRainIntensity; }
	RainIntensity GetActualForecastRainIntensity() const { return mActualForecastRainIntensity; }
	TyphoonStrength GetFailedForecastTyphoonStrength() const {
		return mFailedForecastTyphoonStrength;
	}
	TyphoonStrength GetActualForecastTyphoonStrength() const {
		return mActualForecastTyphoonStrength;
	}
	int GetPoolEffectCounter() const { return mPoolEffectCounter; }
	bool IsSpacePauseActiveForTesting() const { return mSpacePauseActive; }
	bool IsPauseMenuOpenForTesting() const { return mOpenMenu; }
	/** 返回当前屋脊督军及底部血条的纯派生展示状态。 */
	RoofMarshalBossHealthBarState GetRoofMarshalBossHealthBarState() const;

	/** 延后到安全的场景切换阶段展示新植物，或直接返回首页。 */
	void SetReadyToBackMenu(PlantType unlockedPlant) override {
		mUnlockedPlant = unlockedPlant;
		mReadyToBackMenu = true;
	}

	void ShowShovel() override;

	/** 激活波次进度条并按当前关卡波数建立旗子。 */
	void ActivateWaveProgress() override;
	/** 读档后恢复旗子升起状态并立即对齐当前波次。 */
	void RestoreWaveProgress() override;

	WeatherPresentationState CaptureWeatherPresentationState() const override;
	void RestoreWeatherPresentationState(
		const WeatherPresentationState& state) override;

	// 生存轮间空槽重选的冷却快照存取：词条选择期间退出也会保存，因此必须在进入词条页时
	// 提前捕获；随后旧卡槽清空后，它是仍在冷却卡牌进度的唯一载体。
	const SurvivalCardCooldownMap& GetSurvivalCardCooldowns() const override {
		return mSurvivalCardCooldowns;
	}
	void SetSurvivalCardCooldowns(
		SurvivalCardCooldownMap cooldowns) override {
		mSurvivalCardCooldowns = std::move(cooldowns);
	}

protected:
	/** 保持卡槽控制器位于 GameObject 更新后、点击和碰撞分发前。 */
	void UpdateAfterGameObjects() override;
	void BuildDrawCommands() override;

private:
	/** 在背景与战场实体之间交叉淡入对应昼夜的屋顶雨景，不改变逻辑地形。 */
	void DrawRoofRainBackground(Graphics* g);
	/** 在世界层绘制已锁定径流行的预警边界或顺坡水痕。 */
	void DrawRoofRunoff(Graphics* g) const;
	/** 在世界层绘制黑夜屋顶已锁定导电瓦路的预警节点或基础放电。 */
	void DrawNightRoofCharge(Graphics* g) const;
	/** 在世界 UI 顶层绘制当前玉米加农炮落点标记；黑夜全黑幕也不能遮住它。 */
	void DrawCobCannonTarget(Graphics* g) const;
	void UpdateWeatherUi(float deltaTime);
	void DrawFog(Graphics* g) const;
	/** 在天气面板之外绘制冬日花园常驻温度计与玩法阈值。 */
	void DrawWinterThermometer(Graphics* g) const;
	/** 绘制极夜雪原三只与 Board 实数同源的常驻观测仪。 */
	void DrawPolarNightInstruments(Graphics* g) const;
	/** 绘制不遮挡 UI 的白毛风低对比雪幕。 */
	void DrawPolarNightWhiteout(Graphics* g) const;
	void DrawWeatherPanel(Graphics* g) const;
	void DrawWeatherForecastFailure(Graphics* g) const;
	void DrawLightningStrike(Graphics* g) const;
	/** 绘制屋脊督军的底部金属血条、阶段分界与实际生命。 */
	void DrawRoofMarshalBossHealthBar(Graphics* g) const;
	void UpdatePrompts(float deltaTime, float unscaledDeltaTime);
	void DrawPrompts(Graphics* g) const;
	// 按当前 mSurvivalPerkStepsCompleted 重新 roll 并构建第 N/2 次选择框。
	void RenderSurvivalPerkSelectStep();
	// 立即停用并请求 UIManager 在控件遍历后清理，避免刷新或进入下一步时出现一帧双框。
	void CloseSurvivalPerkSelectBox();
	/** 按当前关卡与玩家已读状态尝试打开戴夫闲聊。 */
	bool TryStartCrazyDaveDialog(bool force);

	std::unique_ptr<Board> mBoard = nullptr;
	std::unique_ptr<CrazyDaveDialog> mCrazyDaveDialog;
	std::weak_ptr<Button> mMainMenuButton;
	bool mColdStorageShopOpen = false;
	std::array<std::weak_ptr<Button>, 3> mColdStorageShopButtons;
	/** 非暂停式冰块采购面板；订单状态唯一保存在 Board。 */
	void CreateColdStorageShop();
	void UpdateColdStorageShop();
	void DrawColdStorageShop(Graphics* g);
	std::weak_ptr<Button> mSpeedSettingsButton;
	ShovelBank* mShovelUI = nullptr;   // 所有权在 GameObjectManager
	std::weak_ptr<GameMessageBox> mMenu;
	std::weak_ptr<GameMessageBox> mPerkSelectBox;
	std::vector<PerkPairing>      mCurrentPerkOffer;        // 本轮展示的配对（AutoTest dump 用）
	bool                          mSurvivalPerkSelectActive = false;
	int                           mSurvivalPerkStepsCompleted = 0; // 本轮已消耗的选择机会数（选择或放弃均 +1）
	int                           mSurvivalPerkPicksCompleted = 0; // 本轮已成功选择的正负配对数（0~2）
	int                           mSurvivalPerkRefreshesRemaining = 0; // 两次选择共享；每轮开始重置为 3
	int                           mNextPerkRareRollOverride = -1; // 仅消费一次的 AutoTest 稀有面板覆盖
	std::weak_ptr<Button>         mPerkViewButton;          // 生存模式右上角「词条」按钮（仅生存关创建）
	std::weak_ptr<GameMessageBox> mPerkViewBox;             // 词条查看面板
	bool                          mPerkViewActive = false;  // 面板打开中（守卫暂停叠态）
	int                           mPerkViewPage   = 0;      // 词条查看面板当前页（0-based）

	// ---- 开发者模式（-develop，D 键面板）----
	std::weak_ptr<GameMessageBox> mDevPanelBox;
	bool mDevPanelActive    = false;  // 面板打开中（守卫暂停叠态）
	bool mDevSpawnMode      = false;  // 召唤放置模式（选好类型后点草坪生成）
	bool mDevHintRegistered = false;  // 放置模式提示绘制命令已注册
	int  mDevZombieIndex    = 0;      // kDevZombieTable 下标
	int  mDevLevelSel       = 1;      // 面板选中的关卡号
	int  mDevPendingLevel   = -1;     // >=0 时 Update 尾部执行跳关（回调内不可 SwitchTo 销毁自身）
	std::unique_ptr<CardSlotManager> mCardSlotManager; // 场景独占的卡槽、手持预览与落种控制器
	ChooseCardUI* mChooseCardUI = nullptr;        // 所有权在 GameObjectManager
	GameProgress* mGameProgress = nullptr;        // 所有权在 GameObjectManager

	bool mOpenMenu = false;
	bool mOpenRestartMenu = false;
	bool mOpenQuitMenu = false;
	bool mSpacePauseActive = false;  // 空格触发的轻量暂停；只显示顶部文字，不创建遮挡战场的菜单

	bool mReadyToBackMenu = false;
	PlantType mUnlockedPlant = PlantType::NUM_PLANT_TYPES;
	bool mReadyToRestart = false;
	bool mSurvivalRoundTransition = false;  // true=正处于同会话轮间转场（ChooseCardComplete 走轻量路径）
	bool mGameUiRegistered = false;         // 防止 ZombieNumber/LevelName/Difficulty 绘制命令重复注册
	bool mPendingSurvivalSave = false;      // 轮清后延后一帧存档（避开濒死僵尸尚未被清理而被误序列化）
	float mWeatherPanelSlide = 0.0f;        // 天气面板滑入进度（0=隐藏，1=完全显示）
	float mCurrentWeatherNoticeTimer = 0.0f; // 天气揭晓后“当前天气”面板的剩余显示时间（秒，未缩放）
	float mWeatherForecastFailureTimer = 0.0f; // 错误预报揭晓提示的剩余显示时间（秒，未缩放）
	RainIntensity mFailedForecastRainIntensity = RainIntensity::CLEAR; // 最近一次错误的公开预报
	RainIntensity mActualForecastRainIntensity = RainIntensity::CLEAR; // 最近一次错误预报对应的真实天气
	TyphoonStrength mFailedForecastTyphoonStrength = TyphoonStrength::NONE; // 最近一次错误公开预报中的台风等级
	TyphoonStrength mActualForecastTyphoonStrength = TyphoonStrength::NONE; // 最近一次失败揭晓时的实际台风等级
	// 轮间空槽重选时，快照冷却中卡牌的 {植物类型 → (已计时, 总时长)}，选完后还原到重选回的同种卡
	SurvivalCardCooldownMap mSurvivalCardCooldowns;
	bool mLendToAlmanacScene = false;
	bool mResumeMenuAfterAlmanac = false;

	IntroStage mCurrentStage = IntroStage::BACKGROUND_MOVE;

	float mStartX = -250.0f;          // BackGround初始X坐标
	float mGameStartX = -250.0f;          // BackGround动画在选完卡后的坐标
	float mCurrectSceneX = -250.0f;     // BackGround当前X坐标
	float mTargetSceneX = -700.0f;         // BackGround要到达的X坐标
	bool mHasEnter = false;             // 是否已经进入场景（用于控制进入动画只播放一次）
	float mAnimDuration = 3.0f;          // 动画总时长（秒）
	float mAnimElapsed = 0.0f;           // 动画已进行时间

	// 种子槽动画参数
	float mSeedbankAnimDuration = 0.8f;   // 滑落时长
	float mSeedbankAnimElapsed = 0.0f;
	bool mSeedbankAdded = false;           // 是否已添加纹理
	bool mSunCounterRegistered = false;    // 是否已注册阳光绘制

	// 选卡界面动画
	float mChooseCardUIAnimDuration = 1.0f;          // 动画时长1秒
	float mChooseCardUIAnimElapsed = 0.0f;
	Vector mChooseCardUIStartPos = Vector(200.0f, 800.0f);   // 起始位置（屏幕下方）
	Vector mChooseCardUITargetPos = Vector(200.0f, 80.0f);   // 目标位置（屏幕内）
	bool mChooseCardUIMoving = false;                // 是否正在移动

	// 背景回移动画
	float mReadyAnimDuration = 3.0f;
	float mReadyAnimElapsed = 0.0f;
	Vector mReadyStartPos;

	std::vector<PromptAnimation> mPrompts;

	// 全屏白闪剩余/总时长（秒）；timer<=0 即不激活、不注册额外状态
	float mScreenFlashTimer = 0.0f;
	float mScreenFlashDuration = 0.5f;
	float mScreenFlashPeakAlpha = 200.0f;

	// 大雨闪电只保存本次瞬态路径；不进入关卡存档，也不消费玩法随机数序列。
	float mLightningFlashTimer = 0.0f;
	float mLightningFlashDuration = 0.42f;
	glm::vec2 mLightningStrikePoint{ 0.0f, 0.0f };
	std::vector<glm::vec2> mLightningMainPath;
	std::vector<std::pair<glm::vec2, glm::vec2>> mLightningBranches;
	uint32_t mLightningVisualSequence = 0;

	// 屏幕抖动上一帧是否占用了相机（true 时抖动归零后须把相机复位一次；
	// 平时不触碰相机，避免与开场动画的 SetCameraPosition(camX,0) 打架）
	bool mShakeCameraApplied = false;
	int mPoolEffectCounter = 0;        // 原版水面按 Update 递增的动画相位；不受游戏倍速影响

	/** 打开暂停父菜单；确认按钮在其上追加子弹窗。 */
	void OpenMenu();
	/** 追加重开确认，取消时保留父菜单与暂停状态。 */
	void OpenRestartMenu();
	/** 追加退出确认，确认后才离开当前关卡。 */
	void OpenQuitMenu();
	/** 切换空格键轻量暂停；其他模态暂停打开时不抢占其所有权。 */
	void ToggleSpacePause();
	/** 按空格暂停所有权与玩家设置同步卡槽/落种输入门禁。 */
	void SyncSpacePauseInputPolicy();
	/** 在画面上方中央绘制轻量暂停文字。 */
	void DrawSpacePauseLabel(Graphics* g) const;

	// ---- 开发者模式 ----
	void OpenDevPanel();
	void CloseDevPanel();
	void RenderDevPanel();        // 状态变化即整体重建（autoClose 在控件遍历后清理旧盒）
	void BeginDevSpawnMode();     // 进入召唤放置模式
	void RestoreDevPanelSelection(); // 从 PlayerInfo 恢复稳定的关卡号与僵尸枚举名
	void PersistDevPanelSelection(); // 选择变化时立即写入 PlayerInfo
	void DevJumpToLevel(int level);  // 置 pending，Update 尾部切关；快捷入口不改已保存选择
	void DevTriggerNextWave();    // 出波倒计时清零
	void RegisterSurvivalGameUiOnce();
};

#endif
