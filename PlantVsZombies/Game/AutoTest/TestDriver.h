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
	int  ExitCode() const { return mExitCode; }

	// 每帧调用（GameAPP::Run 中 sceneManager.Update() 之后）。未激活时立即返回。
	void Update();
	/** 交互模式只在明确的 advance 预算内更新场景；普通脚本始终照常运行。 */
	bool ShouldUpdateScene() const { return !mInteractiveReady || mAdvanceSteps > 0; }
	/** 在一次完整场景更新后扣除交互步数预算。 */
	void OnSceneUpdated();
	/** 显式批量评测可每个可见渲染帧推进多个原始固定步；普通/交互模式返回零。 */
	int BatchStepsPerFrame() const { return mActive && !mInteractive ? mBatchSteps : 0; }

	const std::string& OutDir() const { return mOutDir; }

private:
	TestDriver() = default;

	// 执行当前命令。返回 true = 已完成可推进下一条；false = 等待中（下帧重试）。
	bool ExecuteCurrent();

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
	/** 复用现有投影生成完整或精简局面，附带实际卡槽与地形资格。 */
	nlohmann::json BuildInteractiveState();
	/** 先写临时文件再发布唯一序号的响应，避免读取半份状态。 */
	void PublishInteractiveReply();
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
	int mEpisodeTicks = -1;
	nlohmann::json mEpisodeInitial, mEpisodeTrace;

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
