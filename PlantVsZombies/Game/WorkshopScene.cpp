#include "WorkshopScene.h"
#include "SceneManager.h"
#include "AutoTest/TestDriver.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include "../FileManager.h"
#include "../GameInfoSaver.h"
#include "Board/Board.h"
#include "Plant/Plant.h"
#include "Plant/Shooter.h"
#include "Zombie/Zombie.h"
#include "Bullet/Bullet.h"
#include "../Reanimation/Animator.h"
#include "../UI/GameMessageBox.h"
#include "../Logger.h"
#include "Plant/GameDataManager.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
constexpr float kMinZoom = 0.3f, kMaxZoom = 2.0f; // 工厂缩放范围，不影响前线或 UI
constexpr float kFrontY = 88, kFrontRowHeight = 100; // 沿用平地草坪的五行边界，逻辑像素
constexpr float kAutosaveSeconds = 5; // 活跃游玩时自动保存间隔，单位：秒
const glm::vec4 kInk{48, 43, 30, 255}; // 工坊正文颜色
const glm::vec4 kGold{255, 223, 130, 255}; // 选中、等级与提示边框颜色
const glm::vec4 kGreen{66, 107, 51, 255}; // 工坊标题颜色

/** 展示值直接读取模拟参数；升级预览和 tooltip 不维护第二套数值。 */
std::string MachineEffect(Workshop::Machine type, int level) {
    using M = Workshop::Machine;
    if (type == M::Grower || type == M::Cooler) return std::format("{:.1f} 秒/颗", Workshop::GrowPeriods[level - 1]);
    if (type == M::Solar) return std::format("{:g} 阳光/秒", Workshop::SolarRates[level - 1]);
    if (type == M::Store) return std::to_string(Workshop::Capacity(type, level)) + " 颗容量";
    return {};
}
int ShooterCost(const Shooter* shooter) {
    return shooter && shooter->mPlantType == PlantType::PLANT_SNOWPEA ? Workshop::IceShooterCost : Workshop::ShooterCost;
}

std::string UpgradeLabel(const Workshop::World& world, int baseCost, int level) {
    if (level >= Workshop::MaxUpgradeLevel) return "已满级";
    if (world.CompletedWaves() < Workshop::UpgradeWaves[level])
        return "第 " + std::to_string(Workshop::UpgradeWaves[level]) + " 波解锁";
    return std::string(world.CanBuyUpgrade(baseCost, level) ? "升级 " : "需阳光 ") + std::to_string(Workshop::UpgradeCost(baseCost, level));
}

void Text(const std::string& text, float x, float y, int size = 16, glm::vec4 color = kInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
void Sprite(Graphics* g, const char* key, float x, float y, float w, float h) {
    g->DrawTexture(ResourceManager::GetInstance().GetTexture(key, false), x, y, w, h);
}
/** 图鉴小图含透明留白；机器图标按实际非透明区取样，不再把留白一起缩成小点。 */
void PlantIcon(Graphics* g, bool sunflower, float x, float y, float w, float h) {
    const auto* texture = ResourceManager::GetInstance().GetTexture(sunflower ? "IMAGE_SUNFLOWER" : "IMAGE_PEASHOOTER", false);
    if (sunflower) g->DrawTextureRegion(texture, 27, 25, 65, 73, x, y, w, h);
    else g->DrawTextureRegion(texture, 28, 36, 63, 62, x, y, w, h);
}
void Arrow(Graphics* g, float x, float y, int direction, glm::vec4 color) {
    const float dx = direction == 0 ? 1.0f : direction == 2 ? -1.0f : 0;
    const float dy = direction == 1 ? 1.0f : direction == 3 ? -1.0f : 0;
    for (int offset = -1; offset <= 1; ++offset) {
        g->DrawLine(x - dx * 8 + dy * offset, y - dy * 8 + dx * offset, x + dx * 8 + dy * offset, y + dy * 8 + dx * offset, color);
        g->DrawLine(x + dx * 8, y + dy * 8, x + dx * 2 + dy * 5, y + dy * 2 - dx * 5, color);
        g->DrawLine(x + dx * 8, y + dy * 8, x + dx * 2 - dy * 5, y + dy * 2 + dx * 5, color);
    }
}
}

WorkshopScene::~WorkshopScene() = default;
Workshop::World& WorkshopScene::World() const { return mBoard->GetWorkshop(); }

/** 重建只清理当前场景的正式战斗实体，UI 由 UIManager 独立持有。 */
bool WorkshopScene::RebuildFrontline(const nlohmann::json& document) {
    GameObjectManager::GetInstance().DestroyAllGameObjects();
    if (g_particleSystem) g_particleSystem->ClearAll();
    mBoard.reset();
    mBoard = std::make_unique<Board>(nullptr, Background::GROUND_DAY, Workshop::Level);
    if (document.is_null()) mBoard->StartGame();
    else if (!mBoard->RestoreWorkshopDocument(document, mLoadError)) {
        GameObjectManager::GetInstance().DestroyAllGameObjects();
        mLoadFailed = true; return false;
    }
    mLoadFailed = false;
    return true;
}
void WorkshopScene::UpdateAfterGameObjects() {
    if (mBoard && !mLoadFailed) mBoard->Update();
}
void WorkshopScene::DrawGameObjects(Graphics* g) {
    if (mFrontView && !mLoadFailed) {
        GameObjectManager::GetInstance().SetPreOverlayHook([] {
            if (g_particleSystem) g_particleSystem->DrawBelow(LAYER_UI);
        });
        Scene::DrawGameObjects(g);
        GameObjectManager::GetInstance().SetPreOverlayHook(nullptr);
    }
}

void WorkshopScene::OnEnter() {
    GameAPP::GetInstance().GetGraphics().SetCameraPosition(0, 0);
    DeltaTime::SetPaused(false);
    RebuildFrontline(nullptr);
    // AutoTest 永远只读写本脚本目录；第一次进场覆盖旧测试副本，不读取真人基地。
    if (GameAPP::mAutoTestMode) {
        mSavePath = FileManager::CombinePath(TestDriver::GetInstance().OutDir(), "workshop.json");
        static std::string sessionPath;
        mFirstVisit = sessionPath != mSavePath;
        if (!mFirstVisit && FileManager::FileExists(mSavePath)) ReloadBase();
        sessionPath = mSavePath;
    } else {
        mSavePath = GameInfoSaver::GetWorkshopSavePath();
        mFirstVisit = !FileManager::FileExists(mSavePath);
        if (!mFirstVisit) ReloadBase();
    }
    auto& resources = ResourceManager::GetInstance();
    mResourcesReady = resources.HasReanimation("NormalZombie") && resources.HasReanimation("PeaShooter") && resources.HasReanimation("SnowPeaShooter");
    for (const char* key : {"IMAGE_BACKGROUND_DAY", "IMAGE_BACKGROUND_WORKSHOP_GARDEN", "IMAGE_PEASHOOTER", "IMAGE_SUNFLOWER", "IMAGE_PROJECTILEPEA", "IMAGE_PROJECTILESNOWPEA", "IMAGE_BUTTONSMALL"})
        mResourcesReady = resources.GetTexture(key, false) && mResourcesReady;
    if (!mResourcesReady) {
        mLoadFailed = true; mLoadError = "工坊必需的原版资源未加载，请检查资源注册。";
    }
    Scene::OnEnter();
    CreateControls();
    // 默认以建造尺寸进入；全图按钮承担总览，避免一开始把机器和文字压成小点。
    mMapZoom = 1.25f; mMapOffset = Vector(20, 8); ClampFactoryCamera();
    mNotice = "准备阶段全厂停机，可施工；点击开始袭击后产能恢复。按住左键拖地图，滚轮缩放。";
}

void WorkshopScene::OnExit() {
    if (!mLoadFailed) SaveBase();
    CloseCatalog(); mQuickButtons.clear(); mCatalogButton.reset();
    mMachineButtons.clear(); mFactoryControls.clear(); mFrontControls.clear();
    mFrontUpgradeButtons.clear(); mQuotaButtons.clear(); mIceShooterButtons.clear(); mUpgradeButton.reset();
    mFitButton.reset(); mZoomInButton.reset(); mZoomOutButton.reset(); mRouteButton.reset(); mFilterButton.reset();
    mPauseButton.reset(); mStartButton.reset(); mViewButton.reset();
    Scene::OnExit();
    mBoard.reset();
    DeltaTime::SetPaused(false);
}

bool WorkshopScene::SaveBase() {
    if (mLoadFailed || mSavePath.empty()) return false;
    const auto directory = FileManager::GetDirectory(mSavePath);
    const auto document = mBoard->SaveWorkshopDocument();
    const bool result = document.is_object()
        && (FileManager::IsDirectory(directory) || FileManager::CreateDirectory(directory))
        && FileManager::SaveJsonFile(mSavePath, document);
    mSaveFailed = !result;
    if (!result) { mNotice = "保存失败：请保留游戏窗口，检查磁盘和目录权限。"; LOG_ERROR("Workshop") << "保存失败: " << mSavePath; }
    return result;
}
bool WorkshopScene::ReloadBase() {
    nlohmann::json document;
    if (!FileManager::LoadJsonFile(mSavePath, document) || !RebuildFrontline(document)) {
        mLoadFailed = true;
        GameObjectManager::GetInstance().DestroyAllGameObjects();
        DeltaTime::SetPaused(true);
        if (mLoadError.empty()) mLoadError = "无法读取工坊存档。";
        return false;
    }
    mLoadFailed = false; mDeathShown = false; CancelTool(); ClampFactoryCamera(); return true;
}

std::shared_ptr<Button> WorkshopScene::MakeButton(const std::string& text, float x, float y, float w,
    std::function<void()> callback, float h, int font) {
    auto button = mUIManager.CreateButton(Vector(x, y), Vector(w, h));
    button->SetAsCheckbox(false);
    button->SetImageKeys(ResourceKeys::Textures::IMAGE_BUTTONSMALL);
    button->SetText(text, ResourceKeys::Fonts::FONT_FZCQ, font);
    button->SetTextColor(kGold); button->SetHoverTextColor(glm::vec4(255, 255, 200, 255));
    button->SetClickCallBack([callback = std::move(callback)](bool) { callback(); });
    return button;
}

Vector WorkshopScene::FactoryPoint(Vector screen) const {
    return Vector((screen.x - kGridX - mMapOffset.x) / mMapZoom, (screen.y - kGridY - mMapOffset.y) / mMapZoom);
}
Vector WorkshopScene::CellScreenCenter(int row, int column) const {
    return Vector(kGridX + mMapOffset.x + (column + 0.5f) * kCellW * mMapZoom,
        kGridY + mMapOffset.y + (row + 0.5f) * kCellH * mMapZoom);
}
int WorkshopScene::FactoryCellAt(Vector screen) const {
    if (!IsFactoryPointer(screen)) return -1;
    const auto point = FactoryPoint(screen);
    const int col = static_cast<int>(std::floor(point.x / kCellW)), row = static_cast<int>(std::floor(point.y / kCellH));
    return col >= 0 && col < World().Columns() && row >= 0 && row < World().FactoryRows() ? row * Workshop::MaxColumns + col : -1;
}

/** 视图留出场外装饰余量，限制拖动范围；它不改变格位、线路和真实前线坐标。 */
void WorkshopScene::ClampFactoryCamera() {
    const float visibleX = std::min(128 * mMapZoom, kViewW / 2);
    const float visibleY = std::min(128 * mMapZoom, kViewH / 2);
    // 保留至少两格的可见地面即可；小地图也允许平移，缩放时不强制重新居中。
    mMapOffset.x = std::clamp(mMapOffset.x, visibleX - World().Columns() * kCellW * mMapZoom, kViewW - visibleX);
    mMapOffset.y = std::clamp(mMapOffset.y, visibleY - World().FactoryRows() * kCellH * mMapZoom, kViewH - visibleY);
}
void WorkshopScene::FitFactory() {
    mMapZoom = std::clamp(std::min(kViewW / (World().Columns() * kCellW + 160), kViewH / (World().FactoryRows() * kCellH + 160)), kMinZoom, kMaxZoom);
    mMapOffset = Vector((kViewW - World().Columns() * kCellW * mMapZoom) / 2,
        (kViewH - World().FactoryRows() * kCellH * mMapZoom) / 2);
    mMapDragging = false;
}
/** 保持鼠标下的世界点不变，只在触及场外拖动界限时夹紧。 */
void WorkshopScene::ZoomFactory(float zoom, Vector anchor) {
    const auto world = FactoryPoint(anchor);
    mMapZoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    mMapOffset = Vector(anchor.x - kGridX - world.x * mMapZoom, anchor.y - kGridY - world.y * mMapZoom);
    ClampFactoryCamera();
}

void WorkshopScene::CreateControls() {
    mPauseButton = MakeButton("暂停规划", 20, 52, 115, [this] { Act("pause"); });
    mStartButton = MakeButton("开始袭击", 142, 52, 115, [this] { Act("start"); });
    MakeButton("扩建 " + std::to_string(Workshop::ExpansionCost), 264, 52, 115, [this] { Act("expand"); });
    MakeButton("修复 " + std::to_string(Workshop::RepairCost), 386, 52, 115, [this] { Act("repair"); });
    MakeButton("玩法帮助", 508, 52, 115, [this] { ShowHelp(); });
    MakeButton("重开基地", 630, 52, 115, [this] { ConfirmReset(); });
    MakeButton("保存并返回", 752, 52, 150, [this] { if (SaveBase()) mPending = Pending::Back; });
    mViewButton = MakeButton("查看前线", 916, 52, 165, [this] { SetFrontView(!mFrontView); });
    const size_t firstFactoryControl = mUIManager.GetButtonCount();
    mInspectButton = MakeButton("查看", 20, 92, 75, [this] { SelectTool(Tool::Inspect); });
    mRemoveButton = MakeButton("拆除", 100, 92, 75, [this] { SelectTool(Tool::Remove); });
    mRotateButton = MakeButton("旋转 R", 180, 92, 90, [this] { RotatePointedMachine(); });
    mWireAButton = MakeButton("接 A 口", 275, 92, 90, [this] { SelectTool(Tool::WireA); });
    mWireBButton = MakeButton("接 B 口", 370, 92, 90, [this] { SelectTool(Tool::WireB); });
    mDisconnectButton = MakeButton("断开输入", 465, 92, 100, [this] { Act("disconnect", mSelected); });
    mConfigButton = MakeButton("配置所选", 570, 92, 90, [this] { Act("configure", mSelected); });
    mFitButton = MakeButton("查看全图", 670, 92, 95, [this] { FitFactory(); });
    mZoomOutButton = MakeButton("－", 775, 92, 45, [this] { ZoomFactory(mMapZoom / 1.2f, Vector(kGridX + kViewW / 2, kGridY + kViewH / 2)); });
    mZoomInButton = MakeButton("＋", 827, 92, 45, [this] { ZoomFactory(mMapZoom * 1.2f, Vector(kGridX + kViewW / 2, kGridY + kViewH / 2)); });
    mCatalogButton = MakeButton("建造图册  [B]", 813, 148, 265, [this] { OpenCatalog(); }, 33, 18);
    for (int slot = 0; slot < 6; ++slot) {
        auto button = MakeButton("", 813 + (slot % 3) * 90.0f, 200 + (slot / 3) * 60.0f, 86, [this, slot] {
            if (slot < static_cast<int>(mRecentMachines.size())) SelectBuild(mRecentMachines[slot]);
        }, 56, 12);
        button->SetSkipDraw(true); mQuickButtons.push_back(button);
    }
    mRouteButton = MakeButton("目标路线", 813, 390, 110, [this] {
        if (mSelected >= 0) Act("route", mSelected, "", (World().Cells()[mSelected].route + 1) % Workshop::Rows);
    }, 27, 13);
    mFilterButton = MakeButton("弹种", 933, 390, 145, [this] {
        if (mSelected >= 0) Act("filter", mSelected, "", World().Cells()[mSelected].filter == 1 ? -1 : World().Cells()[mSelected].filter + 1);
    }, 27, 13);
    mUpgradeButton = MakeButton("选中机器升级", 928, 422, 150, [this] { Act("upgrade"); }, 27, 14);
    for (int row = 0; row < Workshop::Rows; ++row) {
        auto quota = MakeButton("60/20", 1003, 477 + row * 22.0f, 75, [this, row] {
            Act("quota", row, "", World().Lanes()[row].plainQuota == 0 ? 80 : std::max(0, World().Lanes()[row].plainQuota - 20));
        }, 21, 12);
        mQuotaButtons.push_back(quota);
    }
    for (size_t i = firstFactoryControl; i < mUIManager.GetButtonCount(); ++i)
        mFactoryControls.push_back(mUIManager.GetButton(i));
    for (int row = 0; row < Workshop::Rows; ++row) {
        auto button = MakeButton("豌豆 " + std::to_string(Workshop::ShooterCost), 25, kFrontY + row * kFrontRowHeight + 55, 78,
            [this, row] { Act("shooter", row); }, 22, 12);
        mFrontControls.push_back(button);
        auto ice = MakeButton("寒冰 200", 109, kFrontY + row * kFrontRowHeight + 55, 86,
            [this, row] { Act("ice_shooter", row); }, 22, 12);
        mFrontControls.push_back(ice); mIceShooterButtons.push_back(ice);
        auto upgrade = MakeButton("选中射手", 25, kFrontY + row * kFrontRowHeight + 80, 170,
            [this] { Act("upgrade"); }, 20, 12);
        mFrontControls.push_back(upgrade); mFrontUpgradeButtons.push_back(upgrade);
    }
    SetFrontView(World().State() != Workshop::Phase::Planning);
}

void WorkshopScene::SetFrontView(bool front) {
    if (front != mFrontView) CancelTool();
    mFrontView = front;
    for (const auto& button : mFactoryControls) { button->SetEnabled(!front); button->SetSkipDraw(front); }
    for (const auto& button : mQuickButtons) button->SetSkipDraw(true);
    for (const auto& button : mFrontControls) { button->SetEnabled(front); button->SetSkipDraw(!front); }
    if (mViewButton) mViewButton->SetText(front ? "返回工厂" : "查看前线");
    // 前线工具栏压成一行，给第一行僵尸头部留出原版草坪的上沿空间。
    constexpr std::array<float, 8> toolbarX{20, 142, 264, 386, 508, 630, 752, 916};
    for (size_t i = 0; i < toolbarX.size(); ++i)
        if (auto button = mUIManager.GetButton(i)) button->SetPosition(Vector(toolbarX[i], front ? 8.0f : 52.0f));
    mWireSource = -1;
    UpdateUpgradeControls();
}

std::shared_ptr<Button> WorkshopScene::GetControlButton(const std::string& action) const {
    if (action == "inspect") return mInspectButton;
    if (action == "remove_tool") return mRemoveButton;
    if (action == "wire_a_tool") return mWireAButton;
    if (action == "wire_b_tool") return mWireBButton;
    if (action == "rotate_tool") return mRotateButton;
    if (action == "configure_tool") return mConfigButton;
    if (action == "disconnect_tool") return mDisconnectButton;
    if (action == "catalog") return mCatalogButton;
    if (action == "catalog_close") return IsCatalogOpen() ? mCatalog->GetButton(0) : nullptr;
    constexpr std::array<const char*, 3> categories{"catalog_production", "catalog_logistics", "catalog_logic"};
    for (size_t i = 0; i < categories.size(); ++i)
        if (action == categories[i]) return IsCatalogOpen() ? mCatalog->GetButton(i + 1) : nullptr;
    for (size_t i = 0; i < mQuickButtons.size(); ++i)
        if (action == "recent_" + std::to_string(i)) return mQuickButtons[i];
    if (action == "fit") return mFitButton;
    if (action == "zoom_in") return mZoomInButton;
    if (action == "zoom_out") return mZoomOutButton;
    if (action == "route") return mRouteButton;
    if (action == "filter") return mFilterButton;
    if (action == "upgrade") {
        if (!mFrontView) return mUpgradeButton;
        if (const auto* shooter = SelectedShooter()) return mFrontUpgradeButtons[shooter->mRow];
        return nullptr;
    }
    constexpr std::array<const char*, 8> actions{"pause", "start", "expand", "repair", "help", "reset", "back", "view"};
    for (size_t i = 0; i < actions.size(); ++i) if (action == actions[i]) return mUIManager.GetButton(i);
    return nullptr;
}

std::shared_ptr<Button> WorkshopScene::GetMachineButton(Workshop::Machine type) const {
    if (type <= Workshop::Machine::Empty || type >= Workshop::Machine::Count) return nullptr;
    if (IsCatalogOpen()) return mMachineButtons[static_cast<int>(type) - 1];
    const auto found = std::find(mRecentMachines.begin(), mRecentMachines.end(), type);
    return found != mRecentMachines.end() ? mQuickButtons[found - mRecentMachines.begin()] : nullptr;
}

Shooter* WorkshopScene::SelectedShooter() const {
    auto* shooter = dynamic_cast<Shooter*>(mBoard->mEntityRegistry.GetPlant(mSelectedShooter));
    return shooter && shooter->IsActive() && shooter->mPlantHealth > 0
        && (shooter->mPlantType == PlantType::PLANT_PEASHOOTER || shooter->mPlantType == PlantType::PLANT_SNOWPEA) ? shooter : nullptr;
}

/** 两种射手共用原生视觉原点进行选中判定，不把镜头或工厂格位带入前线。 */
Shooter* WorkshopScene::ShooterAt(Vector point) const {
    for (int id : mBoard->mEntityRegistry.GetAllPlantIDs()) {
        auto* p = dynamic_cast<Shooter*>(mBoard->mEntityRegistry.GetPlant(id));
        if (!p || !p->IsActive() || p->mPlantHealth <= 0
            || (p->mPlantType != PlantType::PLANT_PEASHOOTER && p->mPlantType != PlantType::PLANT_SNOWPEA)) continue;
        const auto anchor = p->GetVisualAnchorPosition();
        if (std::abs(point.x - anchor.x) < 37 && point.y >= anchor.y - 43 && point.y <= anchor.y + 38) return p;
    }
    return nullptr;
}

/** 每帧从当前选择、波次和阳光派生按钮，切换视图时也立即撤销旧选择的操作资格。 */
void WorkshopScene::UpdateUpgradeControls() {
    using M = Workshop::Machine;
    const auto* selected = mSelected >= 0 ? &World().Cells()[mSelected] : nullptr;
    if (mRemoveButton) mRemoveButton->SetText(mTool == Tool::Remove ? "拆除中" : "拆除");
    if (mWireAButton) mWireAButton->SetText(mTool == Tool::WireA ? "接 A 中" : "接 A 口");
    if (mWireBButton) mWireBButton->SetText(mTool == Tool::WireB ? "接 B 中" : "接 B 口");
    if (mRotateButton) mRotateButton->SetEnabled(!mFrontView && (mTool == Tool::Build
        ? Workshop::HasDirectionalOutput(mBuildType) : selected && Workshop::HasDirectionalOutput(selected->type)));
    if (mConfigButton) mConfigButton->SetEnabled(!mFrontView && selected
        && (selected->type == M::Switch || selected->type == M::Timer || selected->type == M::StockSensor));
    if (mDisconnectButton) mDisconnectButton->SetEnabled(!mFrontView && selected && (selected->inputs[0] >= 0 || selected->inputs[1] >= 0));
    for (size_t i = 0; i < mQuickButtons.size(); ++i)
        mQuickButtons[i]->SetEnabled(!mFrontView && i < mRecentMachines.size() && World().Unlocked(mRecentMachines[i]));
    if (mUpgradeButton) {
        const auto* cell = mSelected >= 0 ? &World().Cells()[mSelected] : nullptr;
        const bool supported = cell && Workshop::CanUpgradeMachine(cell->type);
        mUpgradeButton->SetText(supported ? UpgradeLabel(World(), Workshop::Describe(cell->type).cost, cell->level) : "选中可升级机器");
        mUpgradeButton->SetSkipDraw(mFrontView || !supported);
        mUpgradeButton->SetEnabled(!mFrontView && supported && World().CanBuyUpgrade(Workshop::Describe(cell->type).cost, cell->level));
    }
    const auto* shooter = SelectedShooter();
    for (size_t row = 0; row < mFrontUpgradeButtons.size(); ++row) {
        const bool selected = shooter && shooter->mRow == static_cast<int>(row);
        auto& button = mFrontUpgradeButtons[row];
        button->SetText(selected ? UpgradeLabel(World(), ShooterCost(shooter), shooter->GetWorkshopUpgradeLevel()) : "选中射手升级");
        button->SetEnabled(mFrontView && selected && World().CanBuyUpgrade(ShooterCost(shooter), shooter->GetWorkshopUpgradeLevel()));
    }
    for (const auto& button : mIceShooterButtons) {
        button->SetEnabled(mFrontView && World().CompletedWaves() >= Workshop::ColdUnlockWave);
        button->SetText(World().CompletedWaves() >= Workshop::ColdUnlockWave ? "寒冰 200" : "第3波解锁");
    }
    if (mRouteButton && mFilterButton) {
        const auto* cell = mSelected >= 0 ? &World().Cells()[mSelected] : nullptr;
        const auto type = cell ? cell->type : Workshop::Machine::Empty;
        using M = Workshop::Machine;
        const bool route = type == M::Feeder || type == M::EnemySensor || type == M::StockSensor;
        const bool filter = type == M::Feeder || type == M::Store || type == M::Splitter || type == M::StockSensor;
        mRouteButton->SetSkipDraw(mFrontView || !route); mRouteButton->SetEnabled(!mFrontView && route);
        mFilterButton->SetSkipDraw(mFrontView || !filter); mFilterButton->SetEnabled(!mFrontView && filter);
        if (route) mRouteButton->SetText("目标 第" + std::to_string(cell->route + 1) + "路");
        if (filter) mFilterButton->SetText(std::string(type == M::Splitter ? "右侧筛选 " : "弹种 ") + Workshop::AmmoName(cell->filter));
    }
    for (size_t row = 0; row < mQuotaButtons.size(); ++row) {
        const auto quota = World().Lanes()[row].plainQuota;
        mQuotaButtons[row]->SetText(std::to_string(quota) + "/" + std::to_string(Workshop::MagazineCapacity - quota));
    }
}

void WorkshopScene::SelectBuild(Workshop::Machine type) {
    if (!World().Unlocked(type)) {
        mNotice = "守住第 " + std::to_string(Workshop::Describe(type).unlockWave) + " 波后解锁该机器。"; return;
    }
    mBuildType = type; mTool = Tool::Build; mWireSource = -1; mSelected = -1;
    CancelMapGesture(); mDirection = mBuildDirections[static_cast<size_t>(type)];
    RememberMachine(type);
    mNotice = Workshop::Describe(type).description;
}

bool WorkshopScene::Act(const std::string& action, int index, const std::string& type, int value) {
    if (mLoadFailed) return false;
    bool result = false;
    if (action == "build") result = World().Build(index, Workshop::FindMachine(type), value);
    else if (action == "select_machine") {
        if (index < 0 || index >= Workshop::CellCount) return false;
        mSelected = index; mTool = Tool::Inspect; UpdateUpgradeControls(); return true;
    }
    else if (action == "select_shooter") {
        auto* plant = mBoard->GetNormalPlantAt(index, value);
        if (!plant) return false;
        mSelectedShooter = plant->mPlantID; UpdateUpgradeControls(); return SelectedShooter() != nullptr;
    }
    else if (action == "upgrade_machine") result = World().Upgrade(index);
    else if (action == "upgrade") {
        if (mFrontView) { if (auto* shooter = SelectedShooter()) result = shooter->TryUpgradeWorkshop(); }
        else result = World().Upgrade(mSelected);
    }
    else if (action == "remove") result = World().Remove(index);
    else if (action == "rotate") result = World().Rotate(index);
    else if (action == "configure") result = World().Configure(index);
    else if (action == "route") result = World().SetRoute(index, value);
    else if (action == "filter") result = World().SetFilter(index, value);
    else if (action == "quota") result = World().SetQuota(index, value);
    else if (action == "wire_a" || action == "wire_b") result = World().Connect(value, index, action == "wire_b" ? 1 : 0);
    else if (action == "disconnect") {
        result = World().Connect(-1, index, 0); World().Connect(-1, index, 1);
    }
    else if (action == "shooter") result = mBoard->AddWorkshopShooter(index);
    else if (action == "ice_shooter") result = mBoard->AddWorkshopShooter(index, true);
    else if (action == "expand") { result = World().Expand(); if (result) FitFactory(); }
    else if (action == "fit") { FitFactory(); return true; }
    else if (action == "zoom") { ZoomFactory(static_cast<float>(value) / 100, Vector(kGridX + kViewW / 2, kGridY + kViewH / 2)); return true; }
    else if (action == "repair") result = World().Repair();
    else if (action == "start") { result = mBoard->BeginWorkshopAssault(); if (result) { mPaused = false; SetFrontView(true); DeltaTime::SetPaused(false); } }
    else if (action == "view_front") { SetFrontView(true); return true; }
    else if (action == "view_factory") { SetFrontView(false); return true; }
    else if (action == "pause") { mPaused = !mPaused; DeltaTime::SetPaused(mPaused); return true; }
    else if (action == "retry") { CancelTool(); const auto retry = mBoard->WorkshopRetryDocument(); result = !retry.is_null() && RebuildFrontline(retry); mDeathShown = false; mPaused = false; mSelectedShooter = -1; SetFrontView(false); FitFactory(); DeltaTime::SetPaused(false); }
    else if (action == "reset") { CancelTool(); result = RebuildFrontline(nullptr); mDeathShown = false; mPaused = false; mSelected = -1; mSelectedShooter = -1; SetFrontView(false); FitFactory(); DeltaTime::SetPaused(false); }
    else if (action == "save") return SaveBase();
    else if (action == "reload") return ReloadBase();
    if (result) {
        mNotice = action == "start" ? "袭击开始！库存和生产线继续运行，可随时暂停调整。"
            : action == "retry" ? "已恢复本次袭击开始前的基地；调整后可重新挑战。"
            : action == "upgrade" || action == "upgrade_machine" ? "升级完成，已保存。" : "已保存基地。";
        SaveBase();
    } else {
        mNotice = action == "expand" ? "扩建需守住第 3 波、250 阳光，宽高各增加 2 格，最多 20×20。"
            : action == "shooter" || action == "ice_shooter" ? "每行两种射手合计最多 3 株；寒冰需守住第 3 波和 200 阳光。"
            : "操作未完成：检查能源、空地、解锁条件或信号端口。";
    }
    UpdateUpgradeControls();
    return result;
}

void WorkshopScene::HandleGridClick(int index) {
    if (index < 0 || index >= Workshop::CellCount) return;
    const bool occupied = World().Cells()[index].type != Workshop::Machine::Empty;
    auto& input = GameAPP::GetInstance().GetInputHandler();
    const bool continuous = input.IsKeyDown(SDLK_LSHIFT) || input.IsKeyDown(SDLK_RSHIFT);
    if (mTool == Tool::Build) {
        if (occupied) { mSelected = index; mNotice = "已选中现有机器；R 旋转它，空格继续建造。"; }
        else if (Act("build", index, Workshop::Describe(mBuildType).key, mDirection)) mSelected = index;
    } else if (mTool == Tool::Remove) {
        if (occupied && Act("remove", index)) {
            mSelected = -1;
            if (!continuous) CancelTool();
            mNotice = "已拆除；费用返还，内部弹药已丢弃。";
        }
    } else if (mTool == Tool::WireA || mTool == Tool::WireB) {
        if (mWireSource < 0) {
            if (!occupied || (mTool == Tool::WireB && !Workshop::IsLogic(World().Cells()[index].type))) {
                mNotice = WireHint(index); return;
            }
            mWireSource = index; mSelected = index; mNotice = "来源已选，请点击目标；Esc 取消。";
        } else if (World().CanConnect(mWireSource, index, mTool == Tool::WireB ? 1 : 0)) {
            if (Act(mTool == Tool::WireA ? "wire_a" : "wire_b", index, "", mWireSource)) {
                if (!continuous) CancelTool();
                mNotice = "接线完成；Shift 可保留来源继续连接。";
            }
        } else mNotice = WireHint(index);
    } else {
        mSelected = index;
        mNotice = occupied ? Workshop::Describe(World().Cells()[index].type).description : "空地：打开建造图册选择机器。";
    }
    UpdateUpgradeControls();
}

void WorkshopScene::ConfirmReset() {
    CancelMapGesture();
    mDismissibleDialog = GameMessageBox::Builder(Vector(550, 290)).Title("重新经营一座基地？")
        .Message("将清空工坊的布局、资源、解锁和袭击进度。冒险及其他模式不受影响。")
        .Button("取消", Vector(0, 0), Vector(200, 50), 20, [] {})
        .Button("重开基地", Vector(0, 0), Vector(200, 50), 20, [this] { mPending = Pending::Reset; }).Show();
}
void WorkshopScene::ShowHelp() {
    CancelMapGesture();
    mDismissibleDialog = GameMessageBox::Builder(Vector(550, 290)).Title("戴夫工坊 · 从一条生产线开始")
        .Message("准备阶段阳光、生产、运输与电路全部停机，可自由施工；开波后恢复，已有库存保留。\n育豆机 → 运输带 → 装填口；装填口可指定前线的路线和弹种，无对应弹药就停火。\n守住第 3 波开放冷却机与寒冰射手：普通豌豆 + 2 阳光加工成寒冰弹。分流器筛选弹种，仓库可设收货类型。\n每路弹仓共 80 颗，右侧按钮调整普通/寒冰配额；调额保留已有库存。\n左键或中键拖动地图，滚轮缩放，适应按钮查看全图。R 旋转指向的机器或空地预览，Esc/右键结束操作；暂停可施工。\n选中机器或前线射手后花阳光升级，守住第 2/5 波开放二/三级。\n基地与冒险独立，持续经营；死亡可回到本波开战前调整，或重开整个基地。")
        .Button("开始经营", Vector(0, 0), Vector(200, 50), 20, [] {}).Show();
}
void WorkshopScene::ShowDeath() {
    CancelTool();
    mDeathShown = true;
    GameMessageBox::Builder(Vector(550, 290)).Title("防线被突破了")
        .Message("基地档案仍然保留。继续将恢复到本次袭击开始前，可调整生产线再战；重开会清空本模式的经营进度。")
        .Button("继续经营", Vector(0, 0), Vector(200, 50), 20, [this] { mPending = Pending::Retry; })
        .Button("重开基地", Vector(0, 0), Vector(200, 50), 20, [this] { ConfirmReset(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false)
        .Button("保存返回", Vector(0, 0), Vector(200, 50), 20, [this] { if (SaveBase()) mPending = Pending::Back; }).Show();
}

void WorkshopScene::Update() {
    const auto before = World().State(); const int waves = World().CompletedWaves();
    Scene::Update();
    if (mPending == Pending::Back) { SceneManager::GetInstance().SwitchTo("MainMenuScene"); return; }
    if (mPending == Pending::Reset || mPending == Pending::Retry) {
        const auto pending = mPending; mPending = Pending::None;
        if (auto box = mUIManager.GetTopActiveMessageBox()) box->Close();
        Act(pending == Pending::Reset ? "reset" : "retry");
    }
    if (mLoadFailed) {
        DeltaTime::SetPaused(true);
        if (!mUIManager.GetTopActiveMessageBox())
            GameMessageBox::Builder(Vector(550, 290)).Title("工坊暂时无法载入")
                .Message(mLoadError + "\n原存档保持原样，本次不会覆盖它。")
                .Button("返回主菜单", Vector(0, 0), Vector(200, 50), 20, [this] { mPending = Pending::Back; }).Show();
        return;
    }
    if (mFirstVisit) { mFirstVisit = false; if (!GameAPP::mAutoTestMode) ShowHelp(); }
    auto& catalogInput = GameAPP::GetInstance().GetInputHandler();
    bool escapeConsumed = false;
    if (catalogInput.IsKeyPressed(SDLK_ESCAPE)) {
        if (IsCatalogOpen()) { CloseCatalog(); escapeConsumed = true; }
        else if (const auto box = mDismissibleDialog.lock(); box && box == mUIManager.GetTopActiveMessageBox()) {
            box->Close(); mDismissibleDialog.reset(); CancelTool(); escapeConsumed = true;
        }
    }
    if (!mFrontView && !mUIManager.GetTopActiveMessageBox() && catalogInput.IsKeyPressed(SDLK_b)) OpenCatalog();
    const bool modal = mUIManager.GetTopActiveMessageBox() != nullptr;
    if (modal) CancelMapGesture();
    if (!modal) {
        auto& input = GameAPP::GetInstance().GetInputHandler();
        if (input.IsKeyPressed(SDLK_SPACE)) Act("pause");
        if (!escapeConsumed && (input.IsKeyPressed(SDLK_ESCAPE) || input.IsMouseButtonPressed(SDL_BUTTON_RIGHT))) CancelTool();
        if (!mFrontView && input.IsKeyPressed(SDLK_r)) RotatePointedMachine();
        if (!mFrontView) UpdateFactoryInput();
        if (mFrontView && input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
            if (auto* shooter = ShooterAt(input.GetMousePosition())) mSelectedShooter = shooter->mPlantID;
        }
    }
    if (!mPaused && !modal) {
        const float dt = DeltaTime::GetDeltaTime();
        if (World().State() == Workshop::Phase::Assault) mVisualTime += dt;
        mSaveClock += dt;
        if (mSaveClock >= kAutosaveSeconds) { mSaveClock = 0; SaveBase(); }
    }
    if (before != World().State()) SaveBase();
    if (waves != World().CompletedWaves()) mNotice = "守住了第 " + std::to_string(World().CompletedWaves()) + " 波！基地保留，准备好再开启下一波。";
    if (World().State() == Workshop::Phase::Dead && !mDeathShown && !modal) ShowDeath();
    DeltaTime::SetPaused(mPaused || mUIManager.GetTopActiveMessageBox() != nullptr || World().State() == Workshop::Phase::Dead);
    mPauseButton->SetText(mPaused ? "继续运行" : "暂停规划");
    mStartButton->SetEnabled(World().State() == Workshop::Phase::Planning);
    UpdateUpgradeControls();
    UpdateTooltip();
}

void WorkshopScene::BuildDrawCommands() {
    Scene::BuildDrawCommands();
    RegisterDrawCommand("WorkshopBackground", [this](Graphics* g) {
        if (mFrontView) Sprite(g, "IMAGE_BACKGROUND_DAY", -250, -50, 1880, 720);
    }, LAYER_BACKGROUND - 1);
    RegisterDrawCommand("ParticleSystem", [this](Graphics* g) {
        if (mFrontView && g_particleSystem) g_particleSystem->DrawFrom(LAYER_UI);
    }, LAYER_EFFECTS);
    RegisterDrawCommand("Workshop", [this](Graphics* g) { DrawWorkshop(g); }, LAYER_UI - 1);
    RegisterDrawCommand("WorkshopInteraction", [this](Graphics* g) { DrawInteraction(g); }, LAYER_UI + 4000);
    RegisterDrawCommand("WorkshopTooltip", [this](Graphics* g) { if (!IsCatalogOpen()) DrawTooltip(g); }, LAYER_UI + 5000);
}

/** 使用墙钟延迟显示：暂停施工时仍可阅读功能，状态内容从正式模型实时生成。 */
void WorkshopScene::UpdateTooltip() {
    if (!IsCatalogOpen() && (mLeftPending || mMapDragging || mTool == Tool::Remove || mTool == Tool::WireA || mTool == Tool::WireB)) {
        mTooltipLines.clear(); mHoverKey = -1; return;
    }
    int key = -1, index = -1;
    auto type = Workshop::Machine::Empty;
    const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
    const auto* hoveredShooter = mFrontView && !mUIManager.GetTopActiveMessageBox() ? ShooterAt(mouse) : nullptr;
    if (hoveredShooter) key = 1000 + hoveredShooter->mPlantID;
    if (IsCatalogOpen()) {
        for (size_t i = 0; i < mMachineButtons.size(); ++i) if (mMachineButtons[i]->IsHovered()) {
            type = static_cast<Workshop::Machine>(i + 1); key = 500 + static_cast<int>(i); break;
        }
    }
    if (!mFrontView && !mUIManager.GetTopActiveMessageBox()) {
        for (size_t i = 0; i < mRecentMachines.size(); ++i) if (mQuickButtons[i]->IsHovered()) {
            type = mRecentMachines[i]; key = 500 + static_cast<int>(type); break;
        }
        index = FactoryCellAt(mouse);
        if (index >= 0) {
            type = World().Cells()[index].type;
            if (type != Workshop::Machine::Empty) key = index;
        }
    }
    mTooltipLines.clear();
    if (key != mHoverKey) { mHoverKey = key; mHoverStarted = SDL_GetTicks64(); }
    if (key < 0 || SDL_GetTicks64() - mHoverStarted < 300) return;
    const auto& info = Workshop::Describe(type);
    const int level = hoveredShooter ? hoveredShooter->GetWorkshopUpgradeLevel() : index >= 0 ? World().Cells()[index].level : 1;
    const int baseCost = hoveredShooter ? ShooterCost(hoveredShooter) : info.cost;
    std::vector<std::string> lines;
    if (hoveredShooter) {
        const bool ice = hoveredShooter->mPlantType == PlantType::PLANT_SNOWPEA;
        lines = {std::string(ice ? "寒冰射手 · Lv." : "豌豆射手 · Lv.") + std::to_string(level),
            ice ? "只消耗寒冰弹，沿用原版命中减速；普通弹不能代用。" : "只消耗普通弹；升级提高射速，单颗伤害不变。",
            std::format("基础冷却：{:.1f} 秒；缺弹时等待供给", Workshop::ShooterCooldowns[level - 1]), "点击选中，在本行左侧升级。"};
        if (level < Workshop::MaxUpgradeLevel)
            lines.push_back(std::format("下一级：基础冷却 {:.1f} 秒", Workshop::ShooterCooldowns[level]));
    } else {
        lines = {std::string(info.name) + (Workshop::CanUpgradeMachine(type) ? " · Lv." + std::to_string(level) : "")
            + " · 建造 " + std::to_string(info.cost) + " 阳光", info.description};
        if (type == Workshop::Machine::Grower)
            lines.push_back("产速：" + MachineEffect(type, level) + std::format("；消耗：{:.0f} 阳光/颗", Workshop::GrowEnergy));
        else if (type == Workshop::Machine::Cooler)
            lines.push_back("加工：" + MachineEffect(type, level) + "；1 普通弹 + 2 阳光 → 1 寒冰弹");
        else if (type == Workshop::Machine::Solar) lines.push_back("产能：" + MachineEffect(type, level) + "；全基地共享");
        else if (info.capacity > 0) lines.push_back(std::format("输送：每 {:.2f} 秒至多 1 颗", Workshop::TransportPeriod));
        if (info.capacity > 0) lines.push_back("内部容量：" + std::to_string(Workshop::Capacity(type, level)) + " 颗");
        if (index >= 0) {
            lines.push_back("状态：" + World().Status(index));
            if (Workshop::HasDirectionalOutput(type)) {
                lines.push_back(std::string("出料方向：") + Workshop::DirectionName(World().Cells()[index].direction));
                lines.push_back(World().InspectOutput(index).message);
                if (type == Workshop::Machine::Splitter) lines.push_back("分支：" + World().InspectOutput(index, true).message);
                lines.push_back("R 直接旋转这台机器，无需退出建造。");
            }
            const auto& cell = World().Cells()[index];
            if (info.capacity > 0) lines.push_back(std::format("库存：普通 {}，寒冰 {}", cell.stock, cell.iceStock));
            if (type == Workshop::Machine::Feeder || type == Workshop::Machine::EnemySensor || type == Workshop::Machine::StockSensor)
                lines.push_back("目标路线：第 " + std::to_string(cell.route + 1) + " 路");
            if (type == Workshop::Machine::Store || type == Workshop::Machine::Feeder || type == Workshop::Machine::StockSensor || type == Workshop::Machine::Splitter)
                lines.push_back(std::string(type == Workshop::Machine::Splitter ? "右侧筛选：" : "弹种筛选：") + Workshop::AmmoName(cell.filter));
            if (type == Workshop::Machine::Splitter && cell.filter >= 0) lines.push_back("匹配弹种走右侧，其余走前方；堵塞时等待，不混入另一出口。" );
        } else if (!World().Unlocked(type)) lines.push_back("解锁：守住第 " + std::to_string(info.unlockWave) + " 波");
        if (Workshop::CanUpgradeMachine(type) && level < Workshop::MaxUpgradeLevel)
            lines.push_back("下一级：" + MachineEffect(type, level + 1));
    }
    if (hoveredShooter || Workshop::CanUpgradeMachine(type)) {
        if (level < Workshop::MaxUpgradeLevel)
            lines.push_back(std::format("升级需 {} 阳光，守住第 {} 波开放", Workshop::UpgradeCost(baseCost, level), Workshop::UpgradeWaves[level]));
        else lines.push_back("已满级（3 级）");
    }
    auto& graphics = GameAPP::GetInstance().GetGraphics();
    for (const auto& text : lines) {
        std::string line;
        for (size_t pos = 0; pos < text.size();) {
            const auto first = static_cast<unsigned char>(text[pos]);
            const size_t length = first < 128 ? 1 : first < 224 ? 2 : first < 240 ? 3 : 4;
            const auto next = text.substr(pos, length);
            if (!line.empty() && graphics.MeasureTextWidth(line + next, ResourceKeys::Fonts::FONT_FZCQ, 16) > 350) {
                mTooltipLines.push_back(line); line.clear();
            }
            line += next; pos += length;
        }
        if (!line.empty()) mTooltipLines.push_back(line);
    }
    const float height = 22.0f * static_cast<float>(mTooltipLines.size()) + 20;
    mTooltipPosition.x = mouse.x + 390 < SCENE_WIDTH ? mouse.x + 16 : mouse.x - 386;
    mTooltipPosition.y = mouse.y + height + 20 < SCENE_HEIGHT ? mouse.y + 18 : mouse.y - height - 12;
    mTooltipPosition.x = std::clamp(mTooltipPosition.x, 8.0f, 722.0f);
    mTooltipPosition.y = std::max(8.0f, mTooltipPosition.y);
}

void WorkshopScene::DrawTooltip(Graphics* g) {
    if (mTooltipLines.empty() || (mUIManager.GetTopActiveMessageBox() && !IsCatalogOpen())) return;
    const float x = mTooltipPosition.x, y = mTooltipPosition.y;
    const float height = 22.0f * static_cast<float>(mTooltipLines.size()) + 20;
    g->FillRect(x + 4, y + 4, 370, height, glm::vec4(0, 0, 0, 105));
    g->FillRect(x, y, 370, height, glm::vec4(38, 43, 32, 245));
    g->DrawRect(x, y, 370, height, kGold);
    for (size_t i = 0; i < mTooltipLines.size(); ++i)
        Text(mTooltipLines[i], x + 10, y + 10 + static_cast<float>(i) * 22, 16, i == 0 ? kGold : glm::vec4(239, 235, 204, 255));
}

void WorkshopScene::DrawMachine(Graphics* g, int index, float x, float y) {
    DrawMachineVisual(g, World().Cells()[index], x, y, mMapZoom, World().Enabled(index), true);
}

void WorkshopScene::DrawMachineVisual(Graphics* g, const Workshop::Cell& cell, float x, float y,
    float displayScale, bool enabled, bool details) {
    if (cell.type == Workshop::Machine::Empty) return;
    // 按最终屏幕字号生成字形，避免先生成小字再随地图放大；总览省略难读的细项。
    const auto label = [g, displayScale](const std::string& value, float px, float py, int size) {
        g->PushTransform(); g->Translate(px, py); g->Scale(1 / displayScale, 1 / displayScale);
        Text(value, 0, 0, std::max(10, static_cast<int>(std::lround(size * displayScale))), kGold);
        g->PopTransform();
    };
    const bool logic = Workshop::IsLogic(cell.type);
    const glm::vec4 metal = logic ? glm::vec4(57, 78, 77, 255) : glm::vec4(106, 116, 91, 255);
    g->FillRect(x + 4, y + 5, 47, 48, glm::vec4(34, 43, 30, 200));
    g->FillRect(x + 2, y + 2, 47, 48, metal);
    g->DrawRect(x + 3, y + 3, 45, 46, glm::vec4(179, 188, 141, 255));
    for (float dx : {6.0f, 44.0f}) for (float dy : {6.0f, 45.0f})
        g->FillCircle(x + dx, y + dy, 1.5f, glm::vec4(211, 211, 169, 255));
    using M = Workshop::Machine;
    if (cell.type == M::Solar) PlantIcon(g, true, x + 10, y + 7, 29, 34);
    else if (cell.type == M::Grower) {
        g->FillRect(x + 10, y + 11, 30, 25, glm::vec4(63, 64, 35, 255));
        Sprite(g, "IMAGE_PROJECTILEPEA", x + 15, y + 15, 20, 20);
        g->FillRect(x + 9, y + 38, 32 * std::min(1.0f, cell.clock / Workshop::GrowPeriods[cell.level - 1]), 3, kGold);
    } else if (cell.type == M::Cooler) {
        g->FillRect(x + 8, y + 10, 34, 28, glm::vec4(68, 126, 142, 255));
        g->DrawRect(x + 8, y + 10, 34, 28, glm::vec4(173, 231, 239, 255));
        Sprite(g, "IMAGE_PROJECTILESNOWPEA", x + 16, y + 13, 19, 19);
        g->FillRect(x + 9, y + 36, 32 * std::min(1.0f, cell.clock / Workshop::GrowPeriods[cell.level - 1]), 3, glm::vec4(150, 234, 255, 255));
    } else if (cell.type == M::Belt || cell.type == M::Splitter) {
        g->FillRect(x + 8, y + 9, 34, 30, glm::vec4(43, 46, 38, 255));
        for (int n = 0; n < 5; ++n) {
            const float offset = std::fmod(n * 7.0f + mVisualTime * (enabled ? 13 : 0), 34.0f);
            if (cell.direction % 2 == 0) g->DrawLine(x + 8 + offset, y + 10, x + 8 + offset, y + 37, metal);
            else g->DrawLine(x + 9, y + 9 + offset * 0.85f, x + 40, y + 9 + offset * 0.85f, metal);
        }
        Arrow(g, x + 25, y + 23, cell.direction, kGold);
        if (cell.type == M::Splitter) Arrow(g, x + 35, y + 33, (cell.direction + 1) % 4, kGold);
    } else if (cell.type == M::Store) {
        g->FillRect(x + 10, y + 11, 30, 27, glm::vec4(149, 108, 53, 255));
        for (int n = 0; n < 4; ++n) g->DrawLine(x + 11, y + 12 + n * 8, x + 39, y + 12 + n * 8, kInk);
        g->DrawRect(x + 10, y + 11, 30, 27, kGold);
    } else if (cell.type == M::Feeder) {
        PlantIcon(g, false, x + 11, y + 6, 28, 28);
        if (details && displayScale >= 0.85f) label(std::to_string(cell.route + 1) + "路", x + 14, y + 27, 12);
    } else {
        const char* glyph = cell.type == M::And ? "&" : cell.type == M::Or ? "OR" : cell.type == M::Not ? "!"
            : cell.type == M::Latch ? "M" : cell.type == M::Timer ? "T" : cell.type == M::EnemySensor ? "Z"
            : cell.type == M::StockSensor ? "#" : "I/O";
        label(glyph, x + 13, y + 12, 21);
        g->FillCircle(x + 40, y + 11, 4, cell.signal ? glm::vec4(131, 255, 93, 255) : glm::vec4(32, 43, 36, 255));
    }
    if (!logic && cell.type != M::Solar) {
        if (cell.stock > 0) Sprite(g, "IMAGE_PROJECTILEPEA", x + 3, y + 37, 11, 11);
        if (cell.iceStock > 0) Sprite(g, "IMAGE_PROJECTILESNOWPEA", x + 15, y + 37, 11, 11);
        if (details && displayScale >= 0.85f) label(std::to_string(cell.TotalStock()), x + 28, y + 37, 11);
        if (cell.type != M::Feeder) {
            Arrow(g, x + 42, y + 54, cell.direction, glm::vec4(24, 37, 24, 255));
            Arrow(g, x + 42, y + 52, cell.direction, kGold);
        }
    }
    if (!logic && !enabled && cell.type != M::Splitter)
        g->FillCircle(x + 41, y + 12, 4, glm::vec4(225, 92, 70, 255));
    if (details && Workshop::CanUpgradeMachine(cell.type) && displayScale >= 0.85f) {
        g->FillRect(x + 26, y + 2, 23, 14, glm::vec4(42, 49, 32, 235));
        label("L" + std::to_string(cell.level), x + 29, y + 3, 11);
    }
}

void WorkshopScene::DrawFront(Graphics* g) {
    for (int row = 0; row < Workshop::Rows; ++row) {
        const auto& lane = World().Lanes()[row];
        const int enemies = World().EnemyCounts()[row];
        if (!mFrontView) {
            const float y = 479 + row * 22.0f;
            Text(std::format("{}路 普{} 冰{} 敌{}", row + 1, lane.ammo, lane.iceAmmo, enemies), 813, y, 12, kGreen);
            continue;
        }
        const float y = kFrontY + row * kFrontRowHeight;
        g->FillRect(12, y + 4, 195, 98, glm::vec4(232, 220, 168, 238));
        Text(std::format("{}路 普{} 冰{}", row + 1, lane.ammo, lane.iceAmmo), 25, y + 8, 16);
        Text(std::format("配额 {}/{} · 敌人 {}", lane.plainQuota, Workshop::MagazineCapacity - lane.plainQuota, enemies), 25, y + 27, 11, kGreen);
        const auto* selected = SelectedShooter();
        std::string detail = "点击射手查看升级";
        if (selected && selected->mRow == row) {
            const int level = selected->GetWorkshopUpgradeLevel();
            detail = std::format("Lv.{}  {:.1f}秒", level, Workshop::ShooterCooldowns[level - 1]);
            if (level < Workshop::MaxUpgradeLevel) detail += std::format(" → {:.1f}秒", Workshop::ShooterCooldowns[level]);
        }
        Text(detail, 25, y + 41, 11, kGreen);
    }
    if (mFrontView) for (int id : mBoard->mEntityRegistry.GetAllPlantIDs()) {
        const auto* p = dynamic_cast<const Shooter*>(mBoard->mEntityRegistry.GetPlant(id));
        if (!p || !p->IsActive()) continue;
        const auto anchor = p->GetVisualAnchorPosition();
        if (id == mSelectedShooter) g->DrawRect(anchor.x - 37, anchor.y - 43, 74, 81, kGold);
        g->FillRect(anchor.x - 21, anchor.y + 38, 42, 19, glm::vec4(42, 49, 32, 220));
        Text("Lv." + std::to_string(p->GetWorkshopUpgradeLevel()), anchor.x - 17, anchor.y + 40, 13, kGold);
    }
}

/** 原画中央 x=16..84%、y=20..88% 为净空地面；九片映射保持外围木棚与花盆的比例。 */
void WorkshopScene::DrawFactoryMap(Graphics* g) {
    g->FillRect(kGridX, kGridY, kViewW, kViewH, glm::vec4(90, 110, 57, 255));
    g->PushClipRect(static_cast<int>(kGridX), static_cast<int>(kGridY), static_cast<int>(kViewW), static_cast<int>(kViewH));
    g->PushTransform();
    g->Translate(kGridX + mMapOffset.x, kGridY + mMapOffset.y);
    g->Scale(mMapZoom, mMapZoom);
    const auto* texture = ResourceManager::GetInstance().GetTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_WORKSHOP_GARDEN, false);
    const float width = World().Columns() * kCellW, height = World().FactoryRows() * kCellH;
    const float scale = 640.0f / (texture->width * 0.68f);
    const float sx[]{0, texture->width * 0.16f, texture->width * 0.84f, static_cast<float>(texture->width)};
    const float sy[]{0, texture->height * 0.20f, texture->height * 0.88f, static_cast<float>(texture->height)};
    const float dx[]{-sx[1] * scale, 0, width, width + (sx[3] - sx[2]) * scale};
    const float dy[]{-sy[1] * scale, 0, height, height + (sy[3] - sy[2]) * scale};
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
        g->DrawTextureRegion(texture, sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row],
            dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]);
    const int hover = mMapDragging || mLeftPanning || mUIManager.GetTopActiveMessageBox() ? -1
        : FactoryCellAt(GameAPP::GetInstance().GetInputHandler().GetMousePosition());
    for (int row = 0; row < World().FactoryRows(); ++row) for (int col = 0; col < World().Columns(); ++col) {
        const int index = row * Workshop::MaxColumns + col;
        const float x = col * kCellW, y = row * kCellH;
        if ((row + col) % 2) g->FillRect(x, y, kCellW, kCellH, glm::vec4(40, 55, 24, 18));
        if (index == hover) g->FillRect(x, y, kCellW, kCellH, glm::vec4(255, 242, 153, 65));
        DrawMachine(g, index, x + 7, y + 3);
    }
    for (int row = 0; row <= World().FactoryRows(); ++row)
        g->DrawLine(0, row * kCellH, width, row * kCellH, glm::vec4(40, 55, 24, 170));
    for (int col = 0; col <= World().Columns(); ++col)
        g->DrawLine(col * kCellW, 0, col * kCellW, height, glm::vec4(40, 55, 24, 170));
    if (mTool == Tool::WireA || mTool == Tool::WireB || mTool == Tool::Inspect)
        for (int i = 0; i < Workshop::CellCount; ++i) for (int port = 0; port < 2; ++port) {
            const int source = World().Cells()[i].inputs[port]; if (source < 0) continue;
            const auto color = World().Cells()[source].signal ? glm::vec4(70, 250, 96, 230) : glm::vec4(117, 55, 165, 230);
            const float x = i % Workshop::MaxColumns * kCellW + 18 + port * 28;
            const float y = i / Workshop::MaxColumns * kCellH + 12;
            g->DrawLine(source % Workshop::MaxColumns * kCellW + 32, source / Workshop::MaxColumns * kCellH + 12, x, y, color);
            g->FillCircle(x, y, 3, color);
        }
    for (int index : {mSelected, mWireSource}) if (index >= 0) {
        const float x = index % Workshop::MaxColumns * kCellW, y = index / Workshop::MaxColumns * kCellH;
        g->DrawRect(x + 1 / mMapZoom, y + 1 / mMapZoom, kCellW - 2 / mMapZoom, kCellH - 2 / mMapZoom, kGold);
    }
    const int preview = BuildPreviewIndex();
    if (preview >= 0) {
        const float x = preview % Workshop::MaxColumns * kCellW, y = preview / Workshop::MaxColumns * kCellH;
        const bool valid = World().CanBuild(preview, mBuildType, mDirection);
        if (World().Cells()[preview].type == Workshop::Machine::Empty) {
            Workshop::Cell ghost; ghost.type = mBuildType; ghost.direction = mDirection;
            DrawMachineVisual(g, ghost, x + 7, y + 3, mMapZoom, true, false);
        }
        const auto color = valid ? glm::vec4(106, 225, 98, 255) : glm::vec4(244, 88, 65, 255);
        g->FillRect(x, y, kCellW, kCellH, glm::vec4(color.r, color.g, color.b, 42));
        for (int edge = 0; edge < 3; ++edge)
            g->DrawRect(x + edge / mMapZoom, y + edge / mMapZoom, kCellW - edge * 2 / mMapZoom, kCellH - edge * 2 / mMapZoom, color);
    }
    g->PopTransform(); g->PopClipRect();
}

void WorkshopScene::DrawFactoryPanel(Graphics* g) {
    g->FillRect(803, 128, 284, 450, glm::vec4(232, 220, 168, 255));
    Text("建造与控制", 815, 129, 16, kGreen);
    DrawQuickBuild(g);
    if (mSelected >= 0) {
        const auto& cell = World().Cells()[mSelected];
        Text(std::string(cell.type == Workshop::Machine::Empty ? "空地" : Workshop::Describe(cell.type).name)
            + (Workshop::CanUpgradeMachine(cell.type) ? " Lv." + std::to_string(cell.level) : ""), 815, 320, 17, kGreen);
        Text(World().Status(mSelected), 815, 344, 14);
        const auto effect = MachineEffect(cell.type, cell.level);
        if (!effect.empty()) Text(effect + (cell.level < Workshop::MaxUpgradeLevel ? " → " + MachineEffect(cell.type, cell.level + 1) : " · 满级"), 815, 365, 13);
        else if (cell.type == Workshop::Machine::Timer || cell.type == Workshop::Machine::StockSensor)
            Text("当前设定 " + std::to_string(cell.setting) + (cell.type == Workshop::Machine::Timer ? " 秒" : " 颗"), 815, 365, 13);
        Text(std::format("格位 {},{}", mSelected / Workshop::MaxColumns + 1, mSelected % Workshop::MaxColumns + 1), 815, 428, 12);
    } else {
        Text(mTool == Tool::Build ? std::string("准备建造：") + Workshop::Describe(mBuildType).name : "选中机器查看或升级", 815, 323, 15, kGreen);
        Text(mTool == Tool::Build ? std::to_string(Workshop::Describe(mBuildType).cost) + " 阳光/台 · 放下时扣费"
            : "按住左键拖动 · 滚轮缩放", 815, 352, 13);
        Text("R 旋转 · Esc / 右键取消", 815, 376, 13);
    }
    Text("前线库存", 815, 454, 15, kGreen);
    Text("配额 普/冰", 1003, 455, 12, kGreen);
    DrawFront(g);
}

void WorkshopScene::DrawWorkshop(Graphics* g) {
    if (!mResourcesReady) return;
    if (!mFrontView) g->FillRect(0, 0, SCENE_WIDTH, SCENE_HEIGHT, glm::vec4(232, 220, 168, 255));
    g->FillRect(0, 0, 1100, mFrontView ? 44.0f : 124.0f, glm::vec4(232, 220, 168, 245));
    if (mFrontView) {
        g->FillRect(12, 46, 195, 44, glm::vec4(232, 220, 168, 238));
        Text("阳光 " + std::to_string(static_cast<int>(World().Energy())) + " · 基地 " + std::to_string(World().Health()) + "/3", 23, 50, 14);
        Text("第 " + std::to_string(World().CompletedWaves() + 1) + " 波 · "
            + (mSaveFailed ? "保存失败" : mPaused ? "暂停" : World().State() == Workshop::Phase::Assault ? "袭击中" : "准备中"), 23, 70, 14, kGreen);
        DrawFront(g); return;
    }
    Text("戴夫工坊", 20, 14, 27, kGreen);
    Text("阳光 " + std::to_string(static_cast<int>(World().Energy())), 185, 19, 19);
    Text("基地 " + std::to_string(World().Health()) + "/3", 345, 19, 19);
    const auto phase = World().State();
    Text("第 " + std::to_string(World().CompletedWaves() + 1) + " 波 · "
        + (phase == Workshop::Phase::Assault ? "袭击中" : phase == Workshop::Phase::Dead ? "防线失守" : "准备中"), 475, 19, 19);
    Text(mSaveFailed ? "保存失败 · 检查磁盘权限" : mPaused ? "已暂停 · 可以施工"
        : World().State() == Workshop::Phase::Planning ? "准备停机 · 开波后运行" : "独立基地 · 自动保存", 780, 18, 18, kGreen);
    Text(std::format("{}×{} · {:.0f}%", World().Columns(), World().FactoryRows(), mMapZoom * 100), 891, 99, 16, kGreen);
    DrawFactoryMap(g);
    DrawFactoryPanel(g);
    g->PushClipRect(16, 580, 780, 20);
    const char* directions[]{"右", "下", "左", "上"};
    Text(mTool == Tool::Build ? std::string("建造：") + Workshop::Describe(mBuildType).name + " · 朝" + directions[mDirection] + " · " + BuildPreviewReason(BuildPreviewIndex()) : mNotice, 20, 582, 13);
    g->PopClipRect();
}

nlohmann::json WorkshopScene::Inspect() const {
    auto j = World().Inspect(); j["paused"] = mPaused; j["resourcesReady"] = mResourcesReady;
    j["saveProtected"] = mLoadFailed; j["selected"] = mSelected;
    j["gridX"] = kGridX; j["gridY"] = kGridY; j["cellWidth"] = kCellW; j["cellHeight"] = kCellH;
    const auto probe = CellScreenCenter(4, 4);
    j["camera"] = {{"zoom", mMapZoom}, {"x", mMapOffset.x}, {"y", mMapOffset.y}, {"probeX", probe.x}, {"probeY", probe.y},
        {"probeCell", FactoryCellAt(probe)}, {"viewportX", kGridX}, {"viewportY", kGridY}, {"viewportWidth", kViewW}, {"viewportHeight", kViewH}};
    j["factoryTextureReady"] = ResourceManager::GetInstance().GetTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_WORKSHOP_GARDEN, false) != nullptr;
    j["savePath"] = mSavePath;
    j["frontView"] = mFrontView;
    j["tool"] = static_cast<int>(mTool); j["toolName"] = ToolName();
    j["draggingMap"] = mMapDragging || mLeftPanning; j["leftGesturePending"] = mLeftPending;
    j["hoveredMachine"] = HoveredMachine(); j["wireSource"] = mWireSource;
    if (const int hovered = HoveredMachine(); hovered >= 0) {
        const auto output = World().InspectOutput(hovered);
        j["hoverOutput"] = {{"target", output.target}, {"ready", output.ready}, {"message", output.message}};
    } else j["hoverOutput"] = nullptr;
    j["catalogOpen"] = IsCatalogOpen(); j["catalogCategory"] = mCatalogCategory;
    j["modalOpen"] = mUIManager.GetTopActiveMessageBox() != nullptr;
    j["effectivePaused"] = DeltaTime::IsPaused();
    j["selectedTimeScale"] = DeltaTime::GetSelectedTimeScale();
    j["uiButtonCount"] = mUIManager.GetButtonCount();
    j["buildType"] = mTool == Tool::Build ? Workshop::Describe(mBuildType).key : "empty";
    j["recentMachines"] = nlohmann::json::array();
    for (const auto type : mRecentMachines) j["recentMachines"].push_back(Workshop::Describe(type).key);
    const int preview = BuildPreviewIndex();
    j["buildPreview"] = {{"index", preview}, {"valid", preview >= 0 && World().CanBuild(preview, mBuildType, mDirection)},
        {"direction", mDirection}, {"reason", BuildPreviewReason(preview)}};
    j["nativeFrontline"] = true;
    j["nativePlants"] = nlohmann::json::array();
    j["nativePlantsByCell"] = nlohmann::json::object();
    j["selectedShooter"] = SelectedShooter() ? mSelectedShooter : -1;
    j["upgradeEnabled"] = GetControlButton("upgrade") && GetControlButton("upgrade")->IsEnabled();
    j["nativeZombies"] = nlohmann::json::array();
    j["hasShootingAnimation"] = false;
    j["hasEatingZombie"] = false;
    j["hasDyingZombie"] = false;
    j["hasPeaHitParticles"] = g_particleSystem && g_particleSystem->GetEffectActiveParticleCount("PeaBulletHit") > 0;
    j["hasIceHitParticles"] = g_particleSystem && g_particleSystem->GetEffectActiveParticleCount("SnowPeaBulletHit") > 0;
    j["hasChilledZombie"] = false;
    for (int id : mBoard->mEntityRegistry.GetAllPlantIDs()) {
        const auto* p = mBoard->mEntityRegistry.GetPlant(id);
        if (p && p->IsActive()) {
            const auto* shooter = dynamic_cast<const Shooter*>(p);
            const std::string headTrack = shooter && shooter->GetHeadAnimator() ? shooter->GetHeadAnimator()->GetCurrentTrackName() : "";
            if (headTrack == "anim_shooting") j["hasShootingAnimation"] = true;
            const int level = shooter ? shooter->GetWorkshopUpgradeLevel() : 1;
            nlohmann::json plant{{"id", id}, {"row", p->mRow}, {"column", p->mColumn}, {"hp", p->mPlantHealth}, {"shadow", p->GetShadow() != nullptr}, {"headTrack", headTrack},
                {"level", level}, {"ice", p->mPlantType == PlantType::PLANT_SNOWPEA}, {"baseCooldown", Workshop::ShooterCooldowns[level - 1]}};
            j["nativePlants"].push_back(plant);
            j["nativePlantsByCell"][std::to_string(p->mRow) + "_" + std::to_string(p->mColumn)] = plant;
        }
    }
    for (int id : mBoard->mEntityRegistry.GetAllZombieIDs()) {
        const auto* z = mBoard->mEntityRegistry.GetZombie(id);
        if (z && z->IsActive()) {
            if (z->IsEating()) j["hasEatingZombie"] = true;
            if (z->IsDying()) j["hasDyingZombie"] = true;
            if (z->GetCooldownTimer() > 0) j["hasChilledZombie"] = true;
            const auto animator = z->GetAnimatorInternal();
            j["nativeZombies"].push_back({{"id", id}, {"row", z->mRow}, {"hp", z->mBodyHealth}, {"eating", z->IsEating()}, {"dying", z->IsDying()}, {"shadow", z->GetShadow() != nullptr},
                {"track", animator ? animator->GetCurrentTrackName() : ""}, {"tongue", animator && animator->GetTrackVisible("anim_tongue")}});
        }
    }
    j["nativeBulletCount"] = mBoard->mEntityRegistry.GetAllBulletIDs().size();
    j["saveFailed"] = mSaveFailed;
    j["tooltipVisible"] = !mTooltipLines.empty();
    j["tooltipLines"] = mTooltipLines;
    return j;
}
