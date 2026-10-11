#pragma once
#ifndef _TESTDRIVER_H
#define _TESTDRIVER_H
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <map>
#include <unordered_map>
#include <concepts>
#include <utility>

class Board;
class Plant;
class Zombie;
namespace ColdStorageSearch { struct Snapshot; struct Result; struct ConstructionStats; }
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
    bool CommanderObservedRisk() const { return mCommanderObservedRisk; }
    /** 原始完整世界缓存的显式性能消融；正式游戏默认启用。 */
    bool CommanderRawWorldCache() const { return mCommanderRawWorldCache; }
	/** 显式诊断开关；调用方在构造 JSON 或扫描目标前检查，正式游戏不记录。 */
	bool CommanderCombatTrace() const { return mActive && mCommanderCombatTrace && !mCommanderCombatTruncated && !mCommanderCombatFailed; }
	/** 主线程记录数值事件并补入真实时钟；来源/弹丸关联只属于诊断，异常不得影响玩法。 */
	std::uint64_t RecordCommanderCombatEvent(const Board& board, nlohmann::json event) noexcept;
	/** 工厂在异常屏障内构造 JSON，取证分配失败也不能跳过调用方原伤害/死亡。 */
	template<class Factory> requires std::invocable<Factory>
	std::uint64_t RecordCommanderCombatEvent(const Board& board, Factory&& factory) noexcept {
		if(!CommanderCombatTrace()) return 0;
		try { return RecordCommanderCombatEvent(board,std::forward<Factory>(factory)()); }
		catch(...) {mCommanderCombatFailed=true;return 0;}
	}
	/** 返回正在观察的攻击身份；0 表示记录窗口外的攻击，不能补猜历史离手时刻。 */
	std::uint64_t CommanderCombatAttackID(bool cob, int sourceID, int projectileID = 0) const noexcept;
	/** 伤害前保存真实层生命/活性，伤害后按同一稳定 ID 记录结果，直接死亡不伪造掉血。 */
	struct CombatTargetState {
		int id=0,type=-1,row=-1,column=-1,layer=-1;
		int health=0,body=0,helm=0,shield=0,extra=0;
		float x=0,y=0;
		bool active=false,dying=false,headless=false;
	};
	CombatTargetState CommanderCombatPlantState(const Plant& plant) const noexcept;
	CombatTargetState CommanderCombatZombieState(const Zombie& zombie) const noexcept;
	void RecordCommanderCombatDamage(const Board& board, std::uint64_t attackID, const char* kind,
		int requestedDamage, const CombatTargetState& before, const CombatTargetState& after) noexcept;
	/** 直接导出所选 Result 已有的预测事件，绝不为录制增加搜索或重评。 */
	static nlohmann::json CommanderCombatForecast(const ColdStorageSearch::ConstructionStats& stats);
	/** 原提交校验后、任何付款前，按显式人数/费用阈值保存一次真实关卡；不重新搜索。 */
	std::uint64_t CaptureCommanderPlanBeforePayment(Board& board, const ColdStorageSearch::Snapshot& search,
		const ColdStorageSearch::Result& result, int requestedVersion, float age,
		const std::vector<std::uint64_t>& tickets, std::uint32_t searchSeed) noexcept;
	/** 配对原付款的真实结果与新增票据；记录失败或技能拒绝均不能改变事务。 */
	void CaptureCommanderPlanAfterPayment(const Board& board, std::uint64_t captureID, bool committed) noexcept;
	/** 换场只关闭旧录制身份，保存已有事件；不会给未完成攻击虚构命中或取消。 */
	void ResetCommanderCombatScope() noexcept;
	/** 有界日志只在取证边界/收尾落盘；失败仅标记诊断不完整，不结束对局。 */
	void FlushCommanderCombatTrace() noexcept;
	/** Board 在主线程灰烬事务中记录实际免伤；只为显式逐秒对战取证，不借用实体或改变玩法。 */
	void RecordEngineerAshProtection(float elapsed,int row,int engineerID,const std::vector<int>& workerIDs);
	/** 仅供同一局面消融比较，普通脚本和正式对局保持动态挡位预测。 */
	bool CommanderFuelAwareLamp() const { return mCommanderFuelAwareLamp; }
	/** 同局面实战消融只关闭经验候选，正式游戏始终默认保留自由＋经验混合搜索。 */
	bool CommanderExperiencedFormations() const { return mCommanderExperiencedFormations; }
	/** 同步诊断时保持已付款队列不重排，确保两种预测比较同一编队。 */
	bool CommanderPreservePaidQueue() const { return mCommanderPreservePaidQueue; }
	/** 显式支援专项或真人观察显示正式战前窗口；训练缺省保持无增益。 */
	bool ColdStorageBonusSelection() const { return mColdStorageBonusSelection; }
	/** 显式验收玩家入场提示；普通专项不被一次性提示打断。 */
	bool AdventureEntryNotices() const { return mAdventureEntryNotices; }
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
	bool mCommanderCombatTrace = false, mCommanderCombatTruncated = false, mCommanderCombatFailed = false;
	std::size_t mCommanderCombatLimit = 50000, mCommanderCombatRecordCount = 0;
	std::unordered_map<std::uint64_t,std::size_t> mCommanderImpactEvents;
	std::uint64_t mCommanderCombatSequence = 0, mCommanderAttackSequence = 0, mCommanderCombatEpoch = 0;
	std::map<std::pair<bool,int>,std::uint64_t> mCommanderCombatSources;
	struct CombatProjectile { std::uint64_t attackID=0; int sourceID=0, row=-1, col=-1; };
	std::unordered_map<int,CombatProjectile> mCommanderCombatProjectiles;
	nlohmann::json mCommanderCombatEvents = nlohmann::json::array();
	nlohmann::json mCommanderPlanCaptures = nlohmann::json::array();
	nlohmann::json mCommanderDeclaredRunnerSeed;
	int mCommanderCaptureMinimumJacks = 0, mCommanderCaptureMinimumEconomicUnits = 0;
	int mCommanderCaptureMinimumCost = 0, mCommanderCaptureMaximum = 0;
	bool mCommanderForecastTrace = false;
    bool mCommanderObservedRisk = true;
    bool mCommanderRawWorldCache = true;
	bool mCommanderFuelAwareLamp = true;
	bool mCommanderExperiencedFormations = true;
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
	bool mAdventureEntryNotices = false;
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
	/** 显式脚本采购只尝试一次；空闲主线程采样并走正式整车提交，不改已有付费队列。 */
	void AttemptCommanderDiagnosticPurchase(Board& board, nlohmann::json& record);
	int mBatchSteps = 0;
	bool mMuteAudio = false;
	int mEpisodeTicks = -1;
	std::uint64_t mEpisodeSnapshotCaptureTicket=0; // 仅显式对战快照配图，不暂停或改变陪练动作
    bool mEpisodeSnapshotSaved=false; // 单段自然对战最多保存一次诊断快照，胜负已结束时不强行保存
	int mEpisodeLastEnemyIce=-1, mEpisodeFogClears=0, mEpisodeBlindDoomCasts=0; // 陪练仅记公开库存及真实成功事务
	float mEpisodeBlindIceUntil=0; // 最近公开上涨信号到期的游戏秒，支持先铲后种
	nlohmann::json mEpisodeInitial, mEpisodeTrace, mEpisodePlantings, mEpisodeDecisions;
	bool mEpisodeTraceProtections=false;
	nlohmann::json mEpisodeEngineerProtections; // 事件当场记录，避免工程师同次死亡后被逐秒活体采样漏掉
	nlohmann::json mEpisodeSunRefills; // 仅显式压力夹具的外部阳光注入记录，不能混入普通训练胜率
	nlohmann::json mEpisodeDiagnosticPurchases; // 显式采购请求及逐笔真实回执；缺省为空，不属于自主AI成绩
	float mEpisodeDiagnosticOpeningUntil=0; // 沿用开始观察时尚未结束的正式首攻准备期，不提前买兵

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
