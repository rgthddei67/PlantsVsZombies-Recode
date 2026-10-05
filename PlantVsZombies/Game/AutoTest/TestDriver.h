#pragma once
#ifndef _TESTDRIVER_H
#define _TESTDRIVER_H
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>

// -AutoTest 脚本自动驾驶：解析 JSON 命令队列，挂在主循环每帧推进。
// 设计文档：docs/superpowers/specs/2026-06-12-autotest-suite-design.md
class TestDriver {
public:
	static TestDriver& GetInstance();

	// 解析脚本并创建输出目录 ./autotest/out/<脚本名>/。失败返回 false（main 直接退出）。
	bool LoadScript(const std::string& path);

	bool IsActive() const { return mActive; }
	/** 真人日志沿用正式后台规划；批量陪练保持同步，避免墙钟速度影响种子对照。 */
	bool BackgroundCommander() const { return mBackgroundCommander; }
	/** 只在显式诊断脚本记录预测工人轨迹和灰烬时点，不改变搜索或交易。 */
	bool CommanderForecastTrace() const { return mCommanderForecastTrace; }
	/** Board 在主线程灰烬事务中记录实际免伤；只为显式逐秒对战取证，不借用实体或改变玩法。 */
	void RecordEngineerAshProtection(float elapsed,int row,int engineerID,const std::vector<int>& workerIDs);
	/** 仅供同一局面消融比较，普通脚本和正式对局保持动态挡位预测。 */
	bool CommanderFuelAwareLamp() const { return mCommanderFuelAwareLamp; }
	/** 同步诊断时保持已付款队列不重排，确保两种预测比较同一编队。 */
	bool CommanderPreservePaidQueue() const { return mCommanderPreservePaidQueue; }
	/** 显式支援专项或真人观察显示正式战前窗口；训练缺省保持无增益。 */
	bool ColdStorageBonusSelection() const { return mColdStorageBonusSelection; }
	int  ExitCode() const { return mExitCode; }

	// 每帧调用（GameAPP::Run 中 sceneManager.Update() 之后）。未激活时立即返回。
	void Update();
	/** 交互步进按 advance 预算推进；真人观察与普通脚本照常更新场景。 */
	bool ShouldUpdateScene() const { return !mInteractiveReady || mHumanObservation || mAdvanceSteps > 0; }
	/** 在完整场景更新后扣除步进预算，或限频记录真人游玩的只读状态。 */
	void OnSceneUpdated();
	/** 显式批量评测可每个可见渲染帧推进多个原始固定步；普通/交互模式返回零。 */
	int BatchStepsPerFrame() const { return mActive && !mInteractive ? mBatchSteps : 0; }
	/** 训练脚本全静音；普通 AutoTest 默认只关闭背景音乐，不写玩家偏好。 */
	bool MuteAudio() const { return mMuteAudio; }

	const std::string& OutDir() const { return mOutDir; }

private:
	bool mCommanderForecastTrace = false;
	bool mCommanderFuelAwareLamp = true;
	bool mCommanderPreservePaidQueue = false;
	TestDriver() = default;

	// 执行当前命令。返回 true = 已完成可推进下一条；false = 等待中（下帧重试）。
	bool ExecuteCurrent();
	/** 导出正式关卡快照到本脚本输出目录；仅取证，不修改当前棋盘或后台规划。 */
	bool SaveLevelSnapshot(const std::string& name);

	void Fail(const std::string& reason);   // 记日志、退出码=1、结束游戏循环
	/** 恢复会跨场景保留的 AutoTest/开发者覆盖状态。 */
	void ResetTestState();

	// 采集当前场景状态（GameScene 导出完整 Board，受支持的 UI 场景导出专属字段）。
	// 当前场景不支持状态导出时 Fail（带 opName 前缀）并返回 false。
	/** 为当前场景构建 dump_state / assert_state 共用的可断言状态。 */
	bool BuildStateJson(const std::string& opName, nlohmann::json& out);
	void Finish();                          // 全部命令跑完，正常收尾
	void Log(const std::string& msg);       // 写 run.log（带帧号）并 flush
	void WriteStatus(const char* status, const std::string& detail = {});
	/** 开局脚本结束后创建独立会话信箱并发布初始局面。 */
	void BeginInteractive();
	/** 限频读取下一序号文件；请求格式错误也返回结果，保持游戏可继续控制。 */
	void PollInteractive();
	/** 完成本批请求，返回冻结局面；显式 quit 才结束进程。 */
	void CompleteInteractive();
	/** 执行允许的玩家操作或安排有限步进；操作拒绝作为结果返回。 */
	bool ExecuteInteractive(const nlohmann::json& command);
	/** 默认生成精简观测；完整投影仅供单次交互响应，不改变后续真人日志或陪练采样。 */
	nlohmann::json BuildInteractiveState(bool fullState = false);
	/** 先写临时文件再发布唯一序号的响应，避免读取半份状态。 */
	void PublishInteractiveReply();
	bool mHumanObservation = false;
	bool mColdStorageBonusSelection = false;
	bool mDefaultColdStorageBonusSelection = false; // 脚本默认值；单场 goto_level 覆盖不泄漏到下一场
	bool mBackgroundCommander = false; // 性能夹具可显式启用；真人观察默认启用，批量训练默认同步
	bool mHumanRecordingFailed = false;
	int mHumanLastDecision = -1, mHumanLastBoardState = -1;
	bool mHumanLastTrophy = false;
	std::chrono::steady_clock::time_point mNextHumanObservation{};
	bool mInteractive = false;
	bool mInteractiveReady = false;
	bool mInteractiveBusy = false;
	bool mInteractiveQuit = false;
	bool mInteractiveFullState = false;
	int mAdvanceSteps = 0;
	uint64_t mSimulationSteps = 0;
	uint64_t mRequestId = 0;
	std::string mSession;
	std::string mLiveDir;
	nlohmann::json mInteractiveResults = nlohmann::json::array();
	std::chrono::steady_clock::time_point mNextInboxPoll{};

	/** 使用正式玩家接口驱动一次有限比赛，输出结果与采样轨迹。 */
	bool ExecuteCommanderEpisode(const nlohmann::json& command);
	int mBatchSteps = 0;
	bool mMuteAudio = false;
	int mEpisodeTicks = -1;
	bool mEpisodeSnapshotSaved=false; // 单段自然对战最多保存一次诊断快照，胜负已结束时不强行保存
	int mEpisodeLastEnemyIce=-1, mEpisodeFogClears=0, mEpisodeBlindDoomCasts=0; // 陪练仅记公开库存及真实成功事务
	float mEpisodeBlindIceUntil=0; // 最近公开上涨信号到期的游戏秒，支持先铲后种
	nlohmann::json mEpisodeInitial, mEpisodeTrace, mEpisodePlantings, mEpisodeDecisions;
	bool mEpisodeTraceProtections=false;
	nlohmann::json mEpisodeEngineerProtections; // 事件当场记录，避免工程师同次死亡后被逐秒活体采样漏掉
	nlohmann::json mEpisodeSunRefills; // 仅显式压力夹具的外部阳光注入记录，不能混入普通训练胜率

	bool mActive = false;
	int  mExitCode = 0;
	size_t mIndex = 0;
	std::vector<nlohmann::json> mCommands;
	std::string mOutDir;
	std::ofstream mRunLog;
	uint64_t mFrame = 0;
	int mMineLayoutRevision = 0; // 专项可显式固定旧矿道夹具；默认始终检验当前冒险布局

	// 等待型命令的逐命令状态（推进到下一条时清零）
	float mWaitAccum = 0.0f;     // wait_seconds 已累计（缩放后游戏时间）
	int   mFramesLeft = -1;      // wait_frames 剩余（-1 = 未初始化）
	float mTimeoutAccum = 0.0f;  // 当前命令已耗时（未缩放，墙钟语义），超 timeout 判失败
	bool  mBreakFrame = false;   // screenshot 等需要"本帧到此为止"的命令置位
	int   mInputPhase = -1;      // click/key(press) 跨帧状态机阶段（-1 = 未初始化）
	std::uint64_t mCaptureTicket = 0; // 当前 screenshot 等待的渲染器 ticket（0 = 未提交）
};

#endif
