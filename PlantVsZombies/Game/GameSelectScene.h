#pragma once
#ifndef _GAME_SELECT_SCENE_H
#define _GAME_SELECT_SCENE_H

#include "Scene.h"

#include <memory>
#include <vector>

/** 挑战风格的关卡选择界面，可按入口展示冒险、生存或小游戏。 */
class GameSelectScene : public Scene {
public:
	enum class SelectMode {
		SURVIVAL,
		ADVENTURE,
		MINIGAMES,
		PUZZLES,
	};

	void OnEnter() override;
	void OnExit() override;
	void Update() override;

	SelectMode GetSelectMode() const { return mSelectMode; }
	int GetCurrentPage() const { return mCurrentPage; }
	int GetPageCount() const;
	const std::vector<int>& GetAvailableLevels() const { return mAvailableLevels; }
	const std::vector<int>& GetCurrentPageLevels() const { return mCurrentPageLevels; }
	/** 当前冒险进度严格越过该关时返回 true；当前待挑战关不算通关。 */
	bool IsAdventureLevelCompleted(int level) const;
	std::shared_ptr<Button> GetPreviousPageButton() const { return mPreviousPageButton; }
	std::shared_ptr<Button> GetNextPageButton() const { return mNextPageButton; }
	std::shared_ptr<Button> GetSkipLevelButton() const { return mSkipLevelButton; }
	/** 当前页有待通关冒险关时返回其编号，否则返回 -1。 */
	int GetSkippableLevel() const;
	/** 返回当前页指定关卡的按钮；锁定关卡和其他页关卡均返回空。 */
	std::shared_ptr<Button> GetCardButton(int level) const;

protected:
	void BuildDrawCommands() override;

private:
	std::shared_ptr<Button> mBackMenuButton;
	std::shared_ptr<Button> mPreviousPageButton;
	std::shared_ptr<Button> mNextPageButton;
	std::shared_ptr<Button> mSkipLevelButton;
	std::vector<std::shared_ptr<Button>> mCards;
	std::vector<int> mAvailableLevels;
	std::vector<int> mCurrentPageLevels;
	SelectMode mSelectMode = SelectMode::SURVIVAL;
	bool mReadyToSwitchMainMenu = false;
	int mPendingEnterLevel = -1;
	int mCurrentPage = 0;
	int mPendingPageDelta = 0;
	int mPendingSkipLevel = -1;

	/** 打开标准模态确认框，捕获玩家实际确认的关卡号。 */
	void ConfirmSkipLevel();
	/** 在 UI 遍历后结算跳过、保存进度，并刷新选关或进入新植物奖励页。 */
	void CompleteSkippedLevel(int level);

	/** 按当前入口和玩家进度生成实际存在的关卡编号。 */
	void BuildAvailableLevels();
	/** 仅为当前页创建卡片，切页时销毁旧页卡片。 */
	void CreateCurrentPageCards();
	/** 同步翻页按钮的可见性、可点击状态和箭头朝向。 */
	void RefreshPageButtonState();
};

#endif
