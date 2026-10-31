#pragma once
#include "Scene.h"
#include "Workshop/WorkshopRoster.h"
#include <memory>
#include <unordered_map>

class Board;
class Shooter;
class Plant;

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
    static constexpr float kGridX = 16, kGridY = 168; // 工厂视口起点，逻辑像素；上方保留工具状态条
    static constexpr float kViewW = 780, kViewH = 410; // 工厂视口尺寸，逻辑像素
    static constexpr float kCellW = 64, kCellH = 64; // 工厂世界格尺寸，像素
    enum class Tool { Inspect, Build, Remove, WireA, WireB };
    enum class FrontTool { Inspect, Plant, Remove };
    enum class Pending { None, Back, Reset, Retry };
    std::unique_ptr<Board> mBoard;
    Workshop::World& World() const;
    Workshop::Machine mBuildType = Workshop::Machine::Grower;
    Tool mTool = Tool::Inspect;
    Pending mPending = Pending::None;
    int mSelected = -1, mDirection = 0, mWireSource = -1;
    int mSelectedShooter = -1; // 稳定前线植物 ID，保留旧诊断字段名
    PlantType mFrontBuildType = PlantType::PLANT_PEASHOOTER;
    FrontTool mFrontTool = FrontTool::Inspect, mFrontPressTool = FrontTool::Inspect;
    Plant* mHeldPlantPreview = nullptr;
    Plant* mCellPlantPreview = nullptr;
    int mPlantPreviewCell = -1;
    float mMapZoom = 1.0f;
    Vector mMapOffset, mDragPoint;
    bool mMapDragging = false;
    bool mLeftPending = false, mLeftPanning = false, mSuppressGridUntilRelease = false;
    Vector mLeftStart, mLeftOffset;
    int mPressCell = -1;
    Tool mPressTool = Tool::Inspect;
    int mLastMachineClick = -1;
    Uint64 mLastMachineClickAt = 0;
    Vector mLastMachineClickPoint;
    std::array<std::string, Workshop::CellCount> mMaterialWarnings;
    std::string mMaterialNotice;
    Uint64 mMaterialCheckAt = 0, mMaterialNoticeUntil = 0;
    int mMaterialNoticeCount = 0;
    std::array<int, static_cast<size_t>(Workshop::Machine::Count)> mBuildDirections{};
    bool mPaused = false, mDeathShown = false, mLoadFailed = false, mResourcesReady = false;
    bool mFirstVisit = false, mFrontView = false, mSaveFailed = false;
    float mSaveClock = 0, mVisualTime = 0;
    std::string mSavePath, mNotice, mLoadError;
    int mHoverKey = -1;
    Uint64 mHoverStarted = 0;
    std::vector<std::string> mTooltipLines;
    Vector mTooltipPosition;
    std::vector<std::shared_ptr<Button>> mMachineButtons;
    std::vector<std::shared_ptr<Button>> mMachineGuideButtons;
    std::shared_ptr<GameMessageBox> mCatalog;
    std::weak_ptr<GameMessageBox> mDismissibleDialog; // 仅帮助/重开确认可用 Esc 关闭，失败和坏档提示必须明确处理
    std::shared_ptr<Button> mCatalogButton;
    std::vector<std::shared_ptr<Button>> mQuickButtons;
    std::vector<Workshop::Machine> mRecentMachines{Workshop::Machine::Grower, Workshop::Machine::Belt, Workshop::Machine::Feeder, Workshop::Machine::Solar};
    int mCatalogCategory = 0;
    std::string mCatalogNotice;
    std::vector<std::shared_ptr<Button>> mFactoryControls, mFrontControls;
    std::vector<std::shared_ptr<Button>> mQuotaButtons;
    std::shared_ptr<GameMessageBox> mPlantCatalog, mQuotaDialog;
    std::vector<std::shared_ptr<Button>> mPlantButtons;
    std::vector<std::shared_ptr<Button>> mPlantGuideButtons;
    std::shared_ptr<GameMessageBox> mGuide;
    std::vector<int> mGuideTopics;
    int mGuidePage = 0, mGuideSeenWave = 0, mGuideUnlockWave = -1;
    std::shared_ptr<Button> mPlantCatalogButton, mFrontRemoveButton, mFrontUpgradeButton, mAbilityButton, mAutomaticButton;
    std::string mPlantCatalogNotice;
    std::shared_ptr<GameMessageBox> mMachineConfig;
    std::unordered_map<std::string, std::shared_ptr<Button>> mConfigControls;
    int mConfigMachine = -1;
    std::string mConfigNotice;
    std::shared_ptr<Button> mFitButton, mZoomInButton, mZoomOutButton, mRouteButton, mFilterButton;
    std::shared_ptr<Button> mUpgradeButton;
    std::shared_ptr<Button> mPauseButton, mStartButton;
    std::shared_ptr<Button> mViewButton;
    std::shared_ptr<Slider> mSpeedSlider;
    std::shared_ptr<Button> mInspectButton, mRemoveButton, mWireAButton, mWireBButton;
    std::shared_ptr<Button> mRotateButton, mConfigButton, mDisconnectButton;

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
    void OpenMachineConfig(int index);
    void CloseMachineConfig();
    void RefreshMachineConfig();
    void DrawMachineConfig(Graphics* g);
    void DrawProfileCards(Graphics* g);
    void UpdateMaterialWarnings();
    void DrawToolbarIcons(Graphics* g);
    void DrawUtilityIcon(Graphics* g, const std::string& action, float x, float y, float size);
    void OpenGuide(int topic = 0, bool newUnlocks = false);
    void CloseGuide(bool acknowledge = true);
    void RefreshGuide();
    void DrawGuide(Graphics* g);
    void CheckGuideUnlocks();
    void DrawLockedItem(Graphics* g, float centerX, float top, float size);
    void DrawPlantPicture(Graphics* g, PlantType type, float x, float y, float w, float h);
    void ShowDeath();
    void DrawWorkshop(Graphics* g);
    void DrawMachine(Graphics* g, int index, float x, float y);
    /** 地图实体、建造预览与展板共用机器外观；details 只控制运行标注，不改变造型。 */
    void DrawMachineVisual(Graphics* g, const Workshop::Cell& cell, float x, float y,
        float displayScale, bool enabled, bool details);
    void DrawMachineImage(Graphics* g, Workshop::Machine type, float x, float y, float scale, int direction = 0);
    void OpenCatalog();
    void CloseCatalog(bool cancelTool = true);
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
    Plant* SelectedFrontPlant() const;
    Plant* FrontPlantAt(Vector point) const;
    int FrontCellAt(Vector point) const;
    void CreateFrontControls();
    void UpdateFrontInput();
    /** 绘制前按最终相机定位原生预览；它们不进入基地存档或战斗注册表。 */
    void UpdateFrontPreview(Graphics* g);
    void ClearFrontPreview();
    void SelectFrontPlant(PlantType type);
    void OpenPlantCatalog();
    void ClosePlantCatalog(bool cancel = true);
    void DrawPlantCatalog(Graphics* g);
    void DrawFrontOverlay(Graphics* g);
    void OpenAmmoQuotas(int row);
    void DrawAmmoQuotas(Graphics* g, int row);
    void DrawItemIcon(Graphics* g, Workshop::Ammo item, float x, float y, float size);
    void UpdateUpgradeControls();
    void FitFactory();
    void ZoomFactory(float zoom, Vector anchor);
    void ClampFactoryCamera();
    Vector FactoryPoint(Vector screen) const;
    int FactoryCellAt(Vector screen) const;
    void UpdateFactoryInput();
    bool IsFactoryPointer(Vector point) const;
    int HoveredMachine() const;
    void SelectTool(Tool tool);
    void CancelTool();
    void CancelMapGesture();
    void RotatePointedMachine();
    const char* ToolName() const;
    std::string WireHint(int index) const;
    void DrawInteraction(Graphics* g);
    void DrawFactoryMap(Graphics* g);
    void DrawFactoryPanel(Graphics* g);
};
