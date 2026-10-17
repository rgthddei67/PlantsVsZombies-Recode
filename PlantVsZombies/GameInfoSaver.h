#pragma once
#ifndef _GAMEINFOSAVER_H
#define _GAMEINFOSAVER_H
#include "FileManager.h"
#include <string>
#include <unordered_set>
#include <nlohmann/json.hpp>

class Board;
class CardSlotManager;

class GameInfoSaver {
public:
	bool SavePlayerInfo();
	bool LoadPlayerInfo();

	bool SaveLevelData(Board* board, CardSlotManager* manager);
    /** 新场景专用：无档为成功的新局；失败后宿主必须销毁整场景，禁止继续更新或保存半成品。 */
    bool LoadLevelData(Board* board, CardSlotManager* manager);
    /** 仅为刚失败的同关卡消费一次无读档重建；不会删除原档或改变后续正常加载。 */
    void RestartAfterFailedLevelLoad(int level);
    bool DidRestartAfterFailedLevelLoad() const { return mRestartedAfterLoadFailure; }
    const std::string& GetLevelLoadError() const { return mLevelLoadError; }
    bool IsLevelSaveProtected(int level) const { return mProtectedFailedLevels.contains(level); }
	bool DeleteLevelData(Board* board);
	/** 按关卡号清除续局存档，供没有 Board 的选关结算使用；仍遵守 AutoTest 隔离。 */
	bool DeleteLevelData(int level);

	/** 为临时图鉴访问捕获当前局的内存快照，不写磁盘；成功后才允许离开关卡。 */
	bool CaptureAlmanacReturn(Board* board, CardSlotManager* manager);
	int GetAlmanacReturnLevel() const { return mAlmanacReturnLevel; }
	/** 标记一次性恢复；下一次 GameScene 加载优先消费内存快照。 */
	bool QueueAlmanacReturn();
	bool IsAlmanacReturnQueued() const { return mAlmanacReturnQueued; }
	/** 丢弃已消费或已离开关卡的图鉴返回上下文。 */
	void ClearAlmanacReturn();

	/**
	 * @brief 将当前关卡用正式序列化逻辑写入显式 AutoTest 快照路径。
	 * @details 仅在 AutoTest 模式可用，不访问玩家存档根目录。
	 */
	bool SaveAutoTestLevelSnapshot(Board* board, CardSlotManager* manager,
		const std::string& filename);
	/**
	 * @brief 保存正式游戏也可调用的隔离指挥官停滞诊断，不访问或覆盖玩家续局存档。
	 * @details 仅写入 autotest/out/commander_stalls 下新建的唯一目录；调用方不能指定路径。
	 * 主线程同步借用 Board/卡槽，输出完整关卡、诊断与当前策略资源副本；失败记录日志并返回 false。
	 * directory 返回已创建的目录，部分失败时仍保留供检查；不创建目录时为空。
	 */
	bool SaveCommanderStallSnapshot(Board* board, CardSlotManager* manager,
		const nlohmann::json& diagnostics, std::string& directory);
	/**
	 * @brief 为下一次 GameScene 正常加载阶段登记一次性 AutoTest 快照路径。
	 * @details 路径在加载尝试开始前即清除，成功或失败都不会影响后续普通 goto_level。
	 */
	bool QueueAutoTestLevelSnapshotLoad(const std::string& filename);
	/** 取得并清除最近一次快照加载结果；失败原因写入 error。 */
	bool ConsumeAutoTestLevelSnapshotLoadResult(std::string& error);
	void CancelAutoTestLevelSnapshotLoad();

    /** AutoTest 在当前场景注入一个损坏字段，验证正式加载拒绝且完整状态和对象数量不变。 */
    bool ProbeRejectedLevelLoad(Board* board, CardSlotManager* manager,
        const std::string& pointer, const nlohmann::json& value, std::string& error);

private:
	// 实际序列化逻辑（须为成员：Board 仅 friend 本类，自由函数无私有成员访问权）。
	// 上面的公有接口只做一层 try/catch 包裹，详见 .cpp 的“异常安全边界”。
	static bool SavePlayerInfoImpl();
	static bool LoadPlayerInfoImpl();
	static bool SaveLevelDataImpl(Board* board, CardSlotManager* manager);
	bool LoadLevelDataImpl(Board* board, CardSlotManager* manager);
	/** 构建完整正式关卡 JSON，并写入调用方指定的已隔离路径。 */
	static bool SerializeLevelDataToPath(Board* board, CardSlotManager* manager,
		const std::string& filename);
	/** 从指定路径解析 JSON，并用正式反序列化流程应用到新 Board。 */
	static bool DeserializeLevelDataFromPath(Board* board, CardSlotManager* manager,
		const std::string& filename);

	/** 文件存档与临时图鉴快照共用的 JSON 数据契约。 */
	static bool SerializeLevelDocument(Board* board, CardSlotManager* manager, nlohmann::json& document);
	static bool DeserializeLevelDocument(Board* board, CardSlotManager* manager, nlohmann::json document);
    /** 恢复 Board/地形/环境与 ID 计数器，不创建战斗实体。 */
    static void RestoreBoardDocument(Board* board, const nlohmann::json& document);
    /** 依次恢复植物、小推车与僵尸；植物非占格状态先于正常占格状态。 */
    static void RestoreActorDocument(Board* board, const nlohmann::json& document);
    /** 全部 actor 已注册后恢复啃食和已提交锁定，不重新选目标。 */
    static void ResolveRestoredActorReferences(Board* board);
    /** 恢复弹丸、收集物、卡槽与后置场景状态，保留旧加载次序。 */
    static void RestoreRemainingDocument(Board* board, CardSlotManager* manager, const nlohmann::json& document);

	nlohmann::json mAlmanacReturnDocument;
	int mAlmanacReturnLevel = -1;
	bool mAlmanacReturnQueued = false;
    int mFailedLoadLevel = -1;
    int mRestartAfterFailedLoadLevel = -1;
    bool mRestartedAfterLoadFailure = false;
    std::string mLevelLoadError;
    std::string mLevelLoadSourcePath;
    // 备份失败时保存/删除都保持失败关闭，避免新局覆盖唯一可诊断原档。
    std::unordered_set<int> mProtectedFailedLevels;
	std::string mAutoTestSnapshotLoadPath;
	bool mAutoTestSnapshotLoadAttempted = false;
	bool mAutoTestSnapshotLoadSucceeded = false;
	std::string mAutoTestSnapshotLoadError;
};

#endif
