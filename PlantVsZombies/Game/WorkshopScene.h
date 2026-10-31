#pragma once
#include "Scene.h"
#include "Workshop/Workshop.h"
#include <memory>

class Board;
class Shooter;

/** 独立基地入口。工坊存档、解锁、暂停和败局检查点均不借用冒险状态。 */
class WorkshopScene : public Scene {
public:
    ~WorkshopScene() override;
    void OnEnter() override;
    void OnExit() override;
    void Update() override;
    /** UI 与 AutoTest 共用玩家事务入口；不提供任意设资源或跳进度的生产接口。 */
    bool Act(const std::string& action, int index = -1, const std::string& type = "", int value = 0);
    nlohmann::json Inspect() const;
    bool SaveBase();
    bool ReloadBase();
    Board* GetBoard() const { return mBoard.get(); }
    std::shared_ptr<Button> GetControlButton(const std::string& action) const;

private:
    enum class Tool { Inspect, Build, Remove, WireA, WireB };
    enum class Pending { None, Back, Reset, Retry };
    std::unique_ptr<Board> mBoard;
    Workshop::World& World() const;
    Workshop::Machine mBuildType = Workshop::Machine::Grower;
    Tool mTool = Tool::Inspect;
    Pending mPending = Pending::None;
    int mSelected = -1, mDirection = 0, mWireSource = -1;
    int mSelectedShooter = -1;
    bool mPaused = false, mDeathShown = false, mLoadFailed = false, mResourcesReady = false;
    bool mFirstVisit = false, mFrontView = false, mSaveFailed = false;
    float mSaveClock = 0, mVisualTime = 0;
    std::string mSavePath, mNotice, mLoadError;
    int mHoverKey = -1;
    Uint64 mHoverStarted = 0;
    std::vector<std::string> mTooltipLines;
    Vector mTooltipPosition;
    std::vector<std::shared_ptr<Button>> mMachineButtons;
    std::vector<std::shared_ptr<Button>> mFactoryControls, mFrontControls;
    std::vector<std::shared_ptr<Button>> mFrontUpgradeButtons;
    std::shared_ptr<Button> mUpgradeButton;
    std::shared_ptr<Button> mPauseButton, mStartButton;
    std::shared_ptr<Button> mViewButton;

    void BuildDrawCommands() override;
    void UpdateAfterGameObjects() override;
    void DrawGameObjects(Graphics* g) override;
    bool RebuildFrontline(const nlohmann::json& document);
    void CreateControls();
    std::shared_ptr<Button> MakeButton(const std::string& text, float x, float y, float w,
        std::function<void()> callback, float h = 30, int font = 16);
    void HandleGridClick(int index);
    void ConfirmReset();
    void ShowHelp();
    void ShowDeath();
    void DrawWorkshop(Graphics* g);
    void DrawMachine(Graphics* g, int index, float x, float y);
    void DrawFront(Graphics* g);
    void SetFrontView(bool front);
    void UpdateTooltip();
    void DrawTooltip(Graphics* g);
    void SelectBuild(Workshop::Machine type);
    Shooter* SelectedShooter() const;
    Shooter* ShooterAt(Vector point) const;
    void UpdateUpgradeControls();
};
