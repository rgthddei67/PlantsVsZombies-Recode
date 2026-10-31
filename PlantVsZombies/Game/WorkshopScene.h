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
    std::shared_ptr<Button> GetMachineButton(Workshop::Machine type) const;
    bool IsCatalogOpen() const;
    /** 同一坐标变换供格位绘制、输入与 AutoTest 使用；返回逻辑画布坐标。 */
    Vector CellScreenCenter(int row, int column) const;
    bool IsCellVisible(int row, int column) const { return !mFrontView && FactoryCellAt(CellScreenCenter(row, column)) == row * Workshop::MaxColumns + column; }

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
    float mMapZoom = 1.0f;
    Vector mMapOffset, mDragPoint;
    bool mMapDragging = false;
    bool mPaused = false, mDeathShown = false, mLoadFailed = false, mResourcesReady = false;
    bool mFirstVisit = false, mFrontView = false, mSaveFailed = false;
    float mSaveClock = 0, mVisualTime = 0;
    std::string mSavePath, mNotice, mLoadError;
    int mHoverKey = -1;
    Uint64 mHoverStarted = 0;
    std::vector<std::string> mTooltipLines;
    Vector mTooltipPosition;
    std::vector<std::shared_ptr<Button>> mMachineButtons;
    std::shared_ptr<GameMessageBox> mCatalog;
    std::shared_ptr<Button> mCatalogButton;
    std::vector<std::shared_ptr<Button>> mQuickButtons;
    std::vector<Workshop::Machine> mRecentMachines{Workshop::Machine::Grower, Workshop::Machine::Belt, Workshop::Machine::Feeder, Workshop::Machine::Solar};
    int mCatalogCategory = 0;
    std::string mCatalogNotice;
    std::vector<std::shared_ptr<Button>> mFactoryControls, mFrontControls;
    std::vector<std::shared_ptr<Button>> mFrontUpgradeButtons;
    std::vector<std::shared_ptr<Button>> mQuotaButtons, mIceShooterButtons;
    std::shared_ptr<Button> mFitButton, mZoomInButton, mZoomOutButton, mRouteButton, mFilterButton;
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
    /** 地图实体、建造预览与展板共用机器外观；details 只控制运行标注，不改变造型。 */
    void DrawMachineVisual(Graphics* g, const Workshop::Cell& cell, float x, float y,
        float displayScale, bool enabled, bool details);
    void DrawMachineImage(Graphics* g, Workshop::Machine type, float x, float y, float scale, int direction = 0);
    void OpenCatalog();
    void CloseCatalog();
    void RefreshCatalog();
    void DrawCatalog(Graphics* g);
    void DrawQuickBuild(Graphics* g);
    void RememberMachine(Workshop::Machine type);
    int BuildPreviewIndex() const;
    std::string BuildPreviewReason(int index) const;
    void DrawFront(Graphics* g);
    void SetFrontView(bool front);
    void UpdateTooltip();
    void DrawTooltip(Graphics* g);
    void SelectBuild(Workshop::Machine type);
    Shooter* SelectedShooter() const;
    Shooter* ShooterAt(Vector point) const;
    void UpdateUpgradeControls();
    void FitFactory();
    void ZoomFactory(float zoom, Vector anchor);
    void ClampFactoryCamera();
    Vector FactoryPoint(Vector screen) const;
    int FactoryCellAt(Vector screen) const;
    void UpdateFactoryInput();
    void DrawFactoryMap(Graphics* g);
    void DrawFactoryPanel(Graphics* g);
};
