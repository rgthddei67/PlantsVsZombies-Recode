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
#include "Plant/EliteScaredyShroom.h"
#include "Plant/IceStorageNut.h"
#include "Plant/MelonPult.h"
#include "Zombie/Zombie.h"
#include "Bullet/Bullet.h"
#include "../Reanimation/Animator.h"
#include "../UI/GameMessageBox.h"
#include "../Logger.h"
#include "Plant/GameDataManager.h"
#include "Workshop/WorkshopGuide.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace {
constexpr float kMinZoom = 0.3f, kMaxZoom = 2.0f; // 工厂缩放范围，不影响前线或 UI
constexpr float kFrontY = 88, kFrontRowHeight = 100; // 沿用平地草坪的五行边界，逻辑像素
constexpr float kAutosaveSeconds = 5; // 活跃游玩时自动保存间隔，单位：秒
constexpr float kSpeedStep = 0.5f; // 倍速滑条每档增加的运行倍率；1～6 档对应 0.5～3 倍
const glm::vec4 kInk{48, 43, 30, 255}; // 工坊正文颜色
const glm::vec4 kGold{255, 223, 130, 255}; // 选中、等级与提示边框颜色
const glm::vec4 kGreen{66, 107, 51, 255}; // 工坊标题颜色

/** 展示值直接读取模拟参数；升级预览和 tooltip 不维护第二套数值。 */
std::string MachineEffect(Workshop::Machine type, int level) {
    using M = Workshop::Machine;
    if (const float period = Workshop::ProductionPeriod(type, level); period > 0) return std::format("{:.2g} 秒/份", period);
    if (type == M::Solar) return std::format("{:g} 阳光/秒", Workshop::SolarRates[level - 1]);
    if (type == M::Store) return std::to_string(Workshop::Capacity(type, level)) + " 颗容量";
    return {};
}
int FrontPlantCost(const Plant* plant) {
    const auto* info = plant ? Workshop::FindPlant(plant->mPlantType) : nullptr;
    return info ? info->cost : 0;
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
    CloseMachineConfig();
    if (mGridDialog) mGridDialog->Close();
    mGridDialog.reset();
    mMaterialWarnings.fill({}); mMaterialNotice.clear(); mMaterialCheckAt = 0; mMaterialNoticeUntil = 0;
    CloseGuide(false);
    ClearFrontPreview();
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
    // 与 CardSlotManager 一样，在世界对象提交前使用本帧最终相机定位手持预览。
    UpdateFrontPreview(g);
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
    for (const char* key : {"IMAGE_BACKGROUND_DAY", "IMAGE_BACKGROUND_WORKSHOP_GARDEN", "IMAGE_PEASHOOTER", "IMAGE_SUNFLOWER", "IMAGE_PROJECTILEPEA", "IMAGE_PROJECTILESNOWPEA", "IMAGE_BUTTONSMALL", "IMAGE_SHOVEL", "IMAGE_REANIM_SUN1"})
        mResourcesReady = resources.GetTexture(key, false) && mResourcesReady;
    for (const auto& definition : Workshop::Plants) {
        const auto& data = GameDataManager::GetInstance();
        mResourcesReady = resources.HasReanimation(data.PlantTypeToAnimName(definition.type))
            && resources.GetTexture(data.GetPlantTextureKey(definition.type), false) && mResourcesReady;
    }
    mResourcesReady = resources.GetTexture(GameDataManager::GetInstance().GetPlantTextureKey(PlantType::PLANT_COBCANNON), false) && mResourcesReady;
    for (const char* key : {"IMAGE_PUFFSHROOM_PUFF1", "IMAGE_REANIM_MELONPULT_MELON", "IMAGE_REANIM_WINTERMELON_PROJECTILE", "IMAGE_ZOMBIE_HEAD_GROSSOUT"})
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
    CloseMachineConfig();
    CloseGuide(false);
    CloseCatalog(); mQuickButtons.clear(); mCatalogButton.reset();
    mMachineButtons.clear(); mFactoryControls.clear(); mFrontControls.clear();
    ClosePlantCatalog(); mQuotaDialog.reset(); mPlantButtons.clear();
    mPlantCatalogButton.reset(); mFrontRemoveButton.reset(); mFrontUpgradeButton.reset(); mAbilityButton.reset(); mAutomaticButton.reset();
    mQuotaButtons.clear(); mUpgradeButton.reset();
    mFitButton.reset(); mZoomInButton.reset(); mZoomOutButton.reset(); mRouteButton.reset(); mFilterButton.reset();
    mPauseButton.reset(); mStartButton.reset(); mViewButton.reset(); mSpeedSlider.reset();
    Scene::OnExit();
    mBoard.reset();
    DeltaTime::SetPaused(false);
}

bool WorkshopScene::SaveBase() {
    if (mLoadFailed || mSavePath.empty()) return false;
    const auto directory = FileManager::GetDirectory(mSavePath);
    auto document = mBoard->SaveWorkshopDocument();
    if (document.is_object()) {
        document["guideSeenWave"] = std::min(mGuideSeenWave, World().CompletedWaves());
        document["powerGuideSeen"] = mPowerGuideSeen;
    }
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
    mGuideSeenWave = Workshop::ReadGuideSeenWave(document, World().CompletedWaves());
    mPowerGuideSeen = document.contains("powerGuideSeen") && document["powerGuideSeen"].is_boolean()
        ? document["powerGuideSeen"].get<bool>() : document.value("schemaVersion", 0) >= 6;
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
    mPauseButton = MakeButton("　暂停", 20, 52, 115, [this] { Act("pause"); });
    mStartButton = MakeButton("　开波", 142, 52, 115, [this] { Act("start"); });
    MakeButton("　扩建 " + std::to_string(Workshop::ExpansionCost), 264, 52, 115, [this] { Act("expand"); });
    MakeButton("　修复 " + std::to_string(Workshop::RepairCost), 386, 52, 115, [this] { Act("repair"); });
    MakeButton("　图解", 508, 52, 115, [this] { ShowHelp(); });
    MakeButton("　重开", 630, 52, 115, [this] { ConfirmReset(); });
    MakeButton("　保存返回", 752, 52, 150, [this] { if (SaveBase()) mPending = Pending::Back; });
    mViewButton = MakeButton("　前线", 916, 52, 165, [this] { SetFrontView(!mFrontView); });
    const size_t firstFactoryControl = mUIManager.GetButtonCount();
    mGridButton = MakeButton("电网", 780, 10, 300, [this] { OpenPowerGrid(); }, 33, 16);
    mInspectButton = MakeButton("　查看", 20, 92, 75, [this] { SelectTool(Tool::Inspect); });
    mRemoveButton = MakeButton("　拆除", 100, 92, 75, [this] { SelectTool(Tool::Remove); });
    mRotateButton = MakeButton("　旋转 R", 180, 92, 90, [this] { RotatePointedMachine(); });
    mWireAButton = MakeButton("　接 A", 275, 92, 90, [this] { SelectTool(Tool::WireA); });
    mWireBButton = MakeButton("　接 B", 370, 92, 90, [this] { SelectTool(Tool::WireB); });
    mDisconnectButton = MakeButton("　断线", 465, 92, 100, [this] { Act("disconnect", mSelected); });
    mConfigButton = MakeButton("　配置", 570, 92, 90, [this] {
        if (mSelected >= 0 && World().Cells()[mSelected].type == Workshop::Machine::Switch) Act("configure", mSelected);
        else OpenMachineConfig(mSelected);
    });
    mFitButton = MakeButton("　全图", 670, 92, 95, [this] { FitFactory(); });
    mZoomOutButton = MakeButton("－", 775, 92, 45, [this] { ZoomFactory(mMapZoom / 1.2f, Vector(kGridX + kViewW / 2, kGridY + kViewH / 2)); });
    mZoomInButton = MakeButton("＋", 827, 92, 45, [this] { ZoomFactory(mMapZoom * 1.2f, Vector(kGridX + kViewW / 2, kGridY + kViewH / 2)); });
    mCatalogButton = MakeButton("建造图册  [B]", 813, 148, 265, [this] { OpenCatalog(); }, 33, 18);
    for (int slot = 0; slot < 6; ++slot) {
        auto button = MakeButton("", 813 + (slot % 3) * 90.0f, 200 + (slot / 3) * 60.0f, 86, [this, slot] {
            if (slot < static_cast<int>(mRecentMachines.size())) SelectBuild(mRecentMachines[slot]);
        }, 56, 12);
        button->SetSkipDraw(true); mQuickButtons.push_back(button);
    }
    mRouteButton = MakeButton("目标路线", 813, 365, 110, [this] {
        if (mSelected >= 0) Act("route", mSelected, "", (World().ActiveRoute(mSelected) + 1) % Workshop::Rows);
    }, 27, 13);
    mFilterButton = MakeButton("接收", 933, 365, 145, [this] {
        if (mSelected < 0) return;
        if (World().Cells()[mSelected].type == Workshop::Machine::Cannon) OpenMachineConfig(mSelected);
        else Act("filter", mSelected, "", World().ActiveFilter(mSelected) >= (World().Cells()[mSelected].type == Workshop::Machine::Feeder ? Workshop::AmmoCount - 1 : Workshop::ItemCount - 1) ? -1 : World().ActiveFilter(mSelected) + 1);
    }, 27, 13);
    mUpgradeButton = MakeButton("选中机器升级", 813, 397, 265, [this] { Act("upgrade"); }, 27, 14);
    for (int row = 0; row < Workshop::Rows; ++row) {
        auto quota = MakeButton("弹仓配额", 813, 430 + row * 29.0f, 265, [this, row] { OpenAmmoQuotas(row); }, 28, 14);
        mQuotaButtons.push_back(quota);
    }
    for (size_t i = firstFactoryControl; i < mUIManager.GetButtonCount(); ++i)
        mFactoryControls.push_back(mUIManager.GetButton(i));
    CreateFrontControls();
    mSpeedSlider = mUIManager.CreateSlider(Vector(960, 102), Vector(115, 8), 1, 6,
        DeltaTime::GetSelectedTimeScale() / kSpeedStep);
    mSpeedSlider->SetIntegerOnly(true);
    mSpeedSlider->SetChangeCallBack([](float step) { DeltaTime::SetSelectedTimeScale(step * kSpeedStep); });
    DeltaTime::SetSelectedTimeScale(mSpeedSlider->GetValue() * kSpeedStep);
    SetFrontView(World().State() != Workshop::Phase::Planning);
}

void WorkshopScene::SetFrontView(bool front) {
    if (front != mFrontView) CancelTool();
    mFrontView = front;
    if (mSpeedSlider) mSpeedSlider->SetPosition(Vector(960, front ? 62.0f : 102.0f));
    for (const auto& button : mFactoryControls) { button->SetEnabled(!front); button->SetSkipDraw(front); }
    for (const auto& button : mQuickButtons) button->SetSkipDraw(true);
    for (const auto& button : mFrontControls) { button->SetEnabled(front); button->SetSkipDraw(!front); }
    if (mViewButton) mViewButton->SetText(front ? "　工厂" : "　前线");
    // 前线工具栏压成一行，给第一行僵尸头部留出原版草坪的上沿空间。
    constexpr std::array<float, 8> toolbarX{20, 142, 264, 386, 508, 630, 752, 916};
    for (size_t i = 0; i < toolbarX.size(); ++i)
        if (auto button = mUIManager.GetButton(i)) button->SetPosition(Vector(toolbarX[i], front ? 8.0f : 52.0f));
    mWireSource = -1;
    UpdateUpgradeControls();
}

std::shared_ptr<Button> WorkshopScene::GetControlButton(const std::string& action) const {
    if (action == "grid") return mGridButton;
    if (action == "grid_upgrade") return mGridDialog && mGridDialog->IsActive() ? mGridDialog->GetButton(1) : nullptr;
    if (action == "grid_guide") return mGridDialog && mGridDialog->IsActive() ? mGridDialog->GetButton(2) : nullptr;
    for (int i = 0; i < 3; ++i) if (action == "power_demo_" + std::to_string(i)) return mGuide && mGuide->IsActive() ? mGuide->GetButton(13 + i) : nullptr;
    if (auto found = mConfigControls.find(action); found != mConfigControls.end()) return found->second;
    for (size_t i = 0; i < mMachineGuideButtons.size(); ++i)
        if (action == "machine_guide_" + std::to_string(i + 1)) return mMachineGuideButtons[i];
    if (action == "plant_catalog") return mPlantCatalogButton;
    if (action == "front_remove") return mFrontRemoveButton;
    if (action == "front_ability") return mAbilityButton;
    if (action == "front_auto") return mAutomaticButton;
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
        return mFrontUpgradeButton;
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

/** 每帧从当前选择、波次和阳光派生按钮，切换视图时也立即撤销旧选择的操作资格。 */
void WorkshopScene::UpdateUpgradeControls() {
    using M = Workshop::Machine;
    if (mGridButton) {
        const auto power = World().Power();
        mGridButton->SetText(World().LegacyAssault() ? "电网 · 旧袭击保护中" : std::format("电网 {:.1f}/{:.0f} · 产速 {:.0f}%", power.demand, power.capacity, power.speed * 100));
        mGridButton->SetTextColor(power.speed < .999f ? glm::vec4(255, 161, 106, 255) : kGold);
    }
    const auto* selected = mSelected >= 0 ? &World().Cells()[mSelected] : nullptr;
    if (mRemoveButton) mRemoveButton->SetText(mTool == Tool::Remove ? "拆除中" : "拆除");
    if (mWireAButton) mWireAButton->SetText(mTool == Tool::WireA ? "接 A 中" : "接 A 口");
    if (mWireBButton) mWireBButton->SetText(mTool == Tool::WireB ? "接 B 中" : "接 B 口");
    if (mRotateButton) mRotateButton->SetEnabled(!mFrontView && (mTool == Tool::Build
        ? Workshop::HasDirectionalOutput(mBuildType) : selected && Workshop::HasDirectionalOutput(selected->type)));
    if (mConfigButton) mConfigButton->SetEnabled(!mFrontView && selected && selected->type != M::Empty);
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
    const auto* plant = SelectedFrontPlant();
    const auto* definition = plant ? Workshop::FindPlant(plant->mPlantType) : nullptr;
    if (mFrontUpgradeButton) {
        const bool supported = definition && definition->upgrades;
        mFrontUpgradeButton->SetText(supported ? UpgradeLabel(World(), definition->cost, plant->GetWorkshopUpgradeLevel()) : "选中植物升级");
        mFrontUpgradeButton->SetEnabled(mFrontView && supported && World().CanBuyUpgrade(definition->cost, plant->GetWorkshopUpgradeLevel()));
    }
    if (mFrontRemoveButton) mFrontRemoveButton->SetText(mFrontTool == FrontTool::Remove ? "铲除中" : "铲除植物");
    const auto* nut = dynamic_cast<const IceStorageNut*>(plant);
    if (mAbilityButton) { mAbilityButton->SetText("　修复"); mAbilityButton->SetEnabled(mFrontView && nut && nut->IsReadyToActivate() && nut->CanAffordActivation()); }
    if (mAutomaticButton) { mAutomaticButton->SetText(nut && nut->IsAutomatic() ? "　自动：开" : "　自动：关"); mAutomaticButton->SetEnabled(mFrontView && nut); }
    if (mConfigButton) mConfigButton->SetText(selected && selected->type == M::Switch ? "　开关" : "　配置");
    if (mRouteButton && mFilterButton) {
        const auto* cell = mSelected >= 0 ? &World().Cells()[mSelected] : nullptr;
        const auto type = cell ? cell->type : Workshop::Machine::Empty;
        using M = Workshop::Machine;
        const bool route = type == M::Feeder || type == M::Cannon || type == M::EnemySensor || type == M::StockSensor;
        const bool filter = type == M::Feeder || type == M::Cannon || type == M::Counter || type == M::Store || type == M::Splitter || type == M::StockSensor;
        mRouteButton->SetSkipDraw(mFrontView || !route); mRouteButton->SetEnabled(!mFrontView && route);
        mFilterButton->SetSkipDraw(mFrontView || !filter); mFilterButton->SetEnabled(!mFrontView && filter);
        if (route) mRouteButton->SetText("目标 第" + std::to_string(World().ActiveRoute(mSelected) + 1) + "路");
        if (filter) mFilterButton->SetText(std::string(type == M::Splitter ? "右侧筛选 " : "接收 ") + Workshop::AmmoName(World().ActiveFilter(mSelected)));
    }
    for (size_t row = 0; row < mQuotaButtons.size(); ++row) {
        mQuotaButtons[row]->SetText(std::format("　{}路弹仓 {}/80 · 调整配额", row + 1, World().Lanes()[row].TotalStock()), ResourceKeys::Fonts::FONT_FZCQ, 14);
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
    if (action == "grid_open") { OpenPowerGrid(); return mGridDialog && mGridDialog->IsActive(); }
    if (action == "config_open") { OpenMachineConfig(index); return mMachineConfig && mMachineConfig->IsActive(); }
    if (action == "guide") { OpenGuide(std::max(0, index)); return mGuide && mGuide->IsActive(); }
    if (action == "guide_close") { CloseGuide(); return true; }
    bool result = false;
    if (action == "build") result = World().Build(index, Workshop::FindMachine(type), value);
    else if (action == "grid_upgrade") result = World().UpgradeGrid();
    else if (action == "machine_running") result = World().SetMachineRunning(index, value != 0);
    else if (action == "select_machine") {
        if (index < 0 || index >= Workshop::CellCount) return false;
        mSelected = index; mTool = Tool::Inspect; UpdateUpgradeControls(); return true;
    }
    else if (action == "select_shooter") {
        auto* plant = mBoard->GetNormalPlantAt(index, value);
        if (!plant) return false;
        mSelectedShooter = plant->mPlantID; UpdateUpgradeControls(); return SelectedFrontPlant() != nullptr;
    }
    else if (action == "upgrade_machine") result = World().Upgrade(index);
    else if (action == "upgrade") {
        if (mFrontView) { if (auto* plant = SelectedFrontPlant()) result = plant->TryUpgradeWorkshop(); }
        else result = World().Upgrade(mSelected);
    }
    else if (action == "remove") result = World().Remove(index);
    else if (action == "rotate") result = World().Rotate(index);
    else if (action == "configure") result = World().Configure(index);
    else if (action == "recipe") result = World().SetRecipe(index, value);
    else if (action == "plan") result = World().SetPlan(index, value);
    else if (action == "strict_split") result = World().SetStrictSplit(index, value != 0);
    else if (action == "setting") result = World().SetSetting(index, value);
    else if (action == "counter_mode") result = World().SetCounterMode(index, static_cast<Workshop::CounterMode>(value));
    else if (action == "counter_output") result = World().SetCounterOutput(index, static_cast<Workshop::CounterOutput>(value));
    else if (action == "counter_reset" && index >= 0 && index < Workshop::CellCount) result = World().SetCounterMode(index, World().Cells()[index].counterMode);
    else if (action == "fire_cannon") result = mBoard->FireWorkshopCannon(index);
    else if (action == "plant") { const auto* info = Workshop::FindPlant(type); result = info && mBoard->PlaceWorkshopPlant(info->type, index, value); }
    else if (action == "remove_plant") result = mBoard->RemoveWorkshopPlant(index, value);
    else if (action == "plant_ability") { if (auto* plant = SelectedFrontPlant()) result = plant->TryActivateManualAbility(); }
    else if (action == "plant_auto") { if (auto* plant = SelectedFrontPlant(); plant && plant->HasManualAbility()) { plant->SetAbilityAutomatic(!plant->IsAbilityAutomatic()); result = true; } }
    else if (action == "ammo_quota") {
        int item = -1; for (int a = 0; a < Workshop::AmmoCount; ++a) if (type == Workshop::AmmoName(a)) item = a;
        result = item >= 0 && World().SetAmmoQuota(index, static_cast<Workshop::Ammo>(item), value);
    }
    else if (action == "route") result = World().SetRoute(index, value);
    else if (action == "filter") result = World().SetFilter(index, value);
    else if (action == "route0" || action == "route1") result = World().SetRoute(index, value, action == "route1" ? 1 : 0);
    else if (action == "filter0" || action == "filter1") result = World().SetFilter(index, value, action == "filter1" ? 1 : 0);
    else if (action == "quota") result = World().SetQuota(index, value);
    else if (action == "wire_a" || action == "wire_b") result = World().Connect(value, index, action == "wire_b" ? 1 : 0);
    else if (action == "disconnect") {
        result = World().Connect(-1, index, 0); World().Connect(-1, index, 1);
    }
    else if (action == "disconnect_a" || action == "disconnect_b") result = World().Connect(-1, index, action == "disconnect_b" ? 1 : 0);
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
    else if (action == "reset") {
        CancelTool(); result = RebuildFrontline(nullptr); mDeathShown = false; mPaused = false;
        mSelected = -1; mSelectedShooter = -1; mGuideSeenWave = 0; mPowerGuideSeen = true; mFirstVisit = true;
        mRecentMachines = {Workshop::Machine::Grower, Workshop::Machine::Belt, Workshop::Machine::Feeder, Workshop::Machine::Solar};
        SetFrontView(false); FitFactory(); DeltaTime::SetPaused(false);
    }
    else if (action == "save") return SaveBase();
    else if (action == "reload") return ReloadBase();
    if (result) {
        mNotice = action == "start" ? "袭击开始！库存和生产线继续运行，可随时暂停调整。"
            : action == "retry" ? "已恢复本次袭击开始前的基地；调整后可重新挑战。"
            : action == "upgrade" || action == "upgrade_machine" ? "升级完成，已保存。" : "已保存基地。";
        SaveBase();
    } else {
        mNotice = action == "expand" ? "扩建需守住第 3 波、250 阳光，宽高各增加 2 格，最多 20×20。"
            : action == "shooter" || action == "ice_shooter" ? "前线左侧六列可种植；检查空格、解锁波次与阳光。"
            : action == "recipe" ? "冰瓜配方需守住第8波；已开工时做完当前一份再切换。"
            : action == "fire_cannon" ? "未能开火：需要开波、充能完成、三颗对应瓜弹及本路合法目标。"
            : "操作未完成：检查能源、空地、解锁条件或信号端口。";
    }
    UpdateUpgradeControls();
    if (mMachineConfig && mMachineConfig->IsActive()) { mConfigNotice = mNotice; RefreshMachineConfig(); }
    return result;
}

void WorkshopScene::HandleGridClick(int index) {
    if (index < 0 || index >= Workshop::CellCount) return;
    const bool occupied = World().Cells()[index].type != Workshop::Machine::Empty;
    auto& input = GameAPP::GetInstance().GetInputHandler();
    const bool continuous = input.IsKeyDown(SDLK_LSHIFT) || input.IsKeyDown(SDLK_RSHIFT);
    if (occupied && (mTool == Tool::Inspect || mTool == Tool::Build)) {
        const Uint64 now = SDL_GetTicks64();
        const auto point = input.GetMousePosition(), delta = point - mLastMachineClickPoint;
        constexpr Uint64 kDoubleClickMilliseconds = 400; // 双击配置的墙钟间隔，暂停/倍速不影响手感
        if (mLastMachineClick == index && now - mLastMachineClickAt <= kDoubleClickMilliseconds
            && delta.x * delta.x + delta.y * delta.y <= 64) {
            CancelTool(); mSelected = index; OpenMachineConfig(index); UpdateUpgradeControls(); return;
        }
        mLastMachineClick = index; mLastMachineClickAt = now; mLastMachineClickPoint = point;
    } else mLastMachineClick = -1;
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
    if (mFrontView) {
        if (const auto* plant = SelectedFrontPlant()) {
            for (size_t i = 0; i < Workshop::Plants.size(); ++i) if (Workshop::Plants[i].type == plant->mPlantType) {
                OpenGuide(Workshop::PlantGuideBase + static_cast<int>(i)); return;
            }
        }
    } else if (mSelected >= 0 && World().Cells()[mSelected].type != Workshop::Machine::Empty) {
        OpenGuide(static_cast<int>(World().Cells()[mSelected].type)); return;
    }
    OpenGuide();
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
        if (mGuide && mGuide->IsActive()) { CloseGuide(); escapeConsumed = true; }
        else if (mMachineConfig && mMachineConfig->IsActive()) { CloseMachineConfig(); escapeConsumed = true; }
        else if (IsCatalogOpen()) { CloseCatalog(); escapeConsumed = true; }
        else if (mPlantCatalog && mPlantCatalog->IsActive()) { ClosePlantCatalog(); escapeConsumed = true; }
        else if (const auto box = mDismissibleDialog.lock(); box && box == mUIManager.GetTopActiveMessageBox()) {
            box->Close(); mDismissibleDialog.reset(); CancelTool(); escapeConsumed = true;
        }
    }
    if (!mUIManager.GetTopActiveMessageBox() && catalogInput.IsKeyPressed(SDLK_b)) { if (mFrontView) OpenPlantCatalog(); else OpenCatalog(); }
    const bool modal = mUIManager.GetTopActiveMessageBox() != nullptr;
    UpdateMaterialWarnings();
    if (modal) { CancelMapGesture(); ClearFrontPreview(); }
    if (!modal) {
        auto& input = GameAPP::GetInstance().GetInputHandler();
        if (input.IsKeyPressed(SDLK_SPACE)) Act("pause");
        if (!escapeConsumed && (input.IsKeyPressed(SDLK_ESCAPE) || input.IsMouseButtonPressed(SDL_BUTTON_RIGHT))) CancelTool();
        if (!mFrontView && input.IsKeyPressed(SDLK_r)) RotatePointedMachine();
        if (!mFrontView) UpdateFactoryInput();
        if (mFrontView) UpdateFrontInput();
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
    CheckGuideUnlocks();
    DeltaTime::SetPaused(mPaused || mUIManager.GetTopActiveMessageBox() != nullptr || World().State() == Workshop::Phase::Dead);
    mPauseButton->SetText(mPaused ? "　继续" : "　暂停");
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
    RegisterDrawCommand("WorkshopInteraction", [this](Graphics* g) { DrawInteraction(g); DrawFrontOverlay(g); DrawToolbarIcons(g); }, LAYER_UI + 4000);
    RegisterDrawCommand("WorkshopTooltip", [this](Graphics* g) { if (!IsCatalogOpen()) DrawTooltip(g); }, LAYER_UI + 5000);
}

/** 使用墙钟延迟显示：暂停施工时仍可阅读功能，状态内容从正式模型实时生成。 */
void WorkshopScene::UpdateTooltip() {
    if ((mGuide && mGuide->IsActive()) || (mMachineConfig && mMachineConfig->IsActive())) { mTooltipLines.clear(); mHoverKey = -1; return; }
    if (!IsCatalogOpen() && (mLeftPending || mMapDragging || mFrontTool != FrontTool::Inspect
        || mTool == Tool::Remove || mTool == Tool::WireA || mTool == Tool::WireB)) {
        mTooltipLines.clear(); mHoverKey = -1; return;
    }
    int key = -1, index = -1;
    auto type = Workshop::Machine::Empty;
    const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
    const auto* hoveredShooter = mFrontView && !mUIManager.GetTopActiveMessageBox() ? FrontPlantAt(mouse) : nullptr;
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
    if (type != Workshop::Machine::Empty && !World().Unlocked(type)) { mHoverKey = -1; return; }
    if (key != mHoverKey) { mHoverKey = key; mHoverStarted = SDL_GetTicks64(); }
    if (key < 0 || SDL_GetTicks64() - mHoverStarted < 300) return;
    const auto& info = Workshop::Describe(type);
    const int level = hoveredShooter ? hoveredShooter->GetWorkshopUpgradeLevel() : index >= 0 ? World().Cells()[index].level : 1;
    const int baseCost = hoveredShooter ? FrontPlantCost(hoveredShooter) : info.cost;
    std::vector<std::string> lines;
    if (hoveredShooter) {
        const auto* definition = Workshop::FindPlant(hoveredShooter->mPlantType);
        lines = {definition ? definition->name : "前线植物", definition ? definition->role : "",
            std::format("生命 {}/{} · 格位 {},{}", hoveredShooter->mPlantHealth, hoveredShooter->mPlantMaxHealth, hoveredShooter->mRow + 1, hoveredShooter->mColumn + 1)};
        if (definition && definition->ammo != Workshop::Ammo::Count)
            lines.push_back(std::string("每次攻击消耗 1 份") + Workshop::AmmoName(static_cast<int>(definition->ammo)) + "；缺弹停火");
        if (definition && definition->upgrades) {
            const auto* melon = dynamic_cast<const MelonPult*>(hoveredShooter);
            const float baseInterval = melon ? melon->GetShootInterval() : Workshop::ShooterCooldowns[0];
            lines.push_back(std::format("Lv.{} · 约 {:.1f} 秒/次 · 点击选中升级", level,
                baseInterval * Workshop::ShooterCooldowns[level - 1] / Workshop::ShooterCooldowns[0]));
            if (level < Workshop::MaxUpgradeLevel) lines.push_back(std::format("下一级：约 {:.1f} 秒/次",
                baseInterval * Workshop::ShooterCooldowns[level] / Workshop::ShooterCooldowns[0]));
        }
        if (const auto* elite = dynamic_cast<const EliteScaredyShroom*>(hoveredShooter)) {
            lines.push_back(std::format("当前伤害 {} · 间隔 {:.2f} 秒", elite->GetCurrentPuffDamage(), elite->GetShootIntervalMilliseconds() / 1000.0f));
            lines.push_back("成长跨波保留；缺弹不清零，近身受惊清零。");
        }
        if (const auto* nut = dynamic_cast<const IceStorageNut*>(hoveredShooter)) {
            lines.push_back(nut->GetManualAbilityDescription()); lines.push_back("开波后可手动或自动修复；准备阶段停止计时。");
        }
    } else {
        lines = {std::string(info.name) + (Workshop::CanUpgradeMachine(type) ? " · Lv." + std::to_string(level) : "")
            + " · 建造 " + std::to_string(info.cost) + " 阳光", info.description};
        if (type == Workshop::Machine::Grower)
            lines.push_back("产速：" + MachineEffect(type, level) + std::format("；消耗：{:.0f} 阳光/颗", Workshop::GrowEnergy));
        else if (type == Workshop::Machine::Cooler) {
            lines.push_back("加工：" + MachineEffect(type, level));
            lines.push_back(World().CompletedWaves() >= 8 ? "普通弹 + 2阳光 → 寒冰弹；西瓜 + 6阳光 → 冰瓜" : "普通弹 + 2阳光 → 寒冰弹");
        } else if (type == Workshop::Machine::SporeGrower || type == Workshop::Machine::MelonGrower)
            lines.push_back("产速：" + MachineEffect(type, level) + std::format("；每份消耗 {:g} 阳光", type == Workshop::Machine::SporeGrower ? Workshop::SporeEnergy : Workshop::MelonEnergy));
        else if (type == Workshop::Machine::Recycler) lines.push_back("加工：" + MachineEffect(type, level) + "；每份残骸获得5阳光");
        else if (type == Workshop::Machine::Solar) lines.push_back("产能：" + MachineEffect(type, level) + "；全基地共享");
        else if (info.capacity > 0) lines.push_back(std::format("输送：每 {:.2f} 秒至多 1 颗", Workshop::TransportPeriod));
        if (info.capacity > 0) lines.push_back("内部容量：" + std::to_string(Workshop::Capacity(type, level)) + " 颗");
        if (const float watts = Workshop::RatedPower(type, level); watts > 0) {
            lines.push_back(std::format("加工用电 {:.1f} · 待机 {:.1f} · 手动/A口关闭为0", watts, watts * Workshop::StandbyPowerRatio));
            if (index >= 0) lines.push_back(std::format("此刻需求 {:.1f} · 全厂加工速度 {:.0f}%", World().MachinePower(index), World().Power().speed * 100));
            else lines.push_back(std::format("新增后满负荷预估：全厂加工速度 {:.0f}%", World().PowerAfterBuild(type).speed * 100));
        }
        if (index >= 0) {
            lines.push_back("状态：" + World().Status(index));
            if (Workshop::HasDirectionalOutput(type)) {
                lines.push_back(std::string("出料方向：") + Workshop::DirectionName(World().Cells()[index].direction));
                lines.push_back(World().InspectOutput(index).message);
                if (type == Workshop::Machine::Splitter) lines.push_back("分支：" + World().InspectOutput(index, true).message);
                lines.push_back("R 直接旋转这台机器，无需退出建造。");
            }
            const auto& cell = World().Cells()[index];
            const auto warning = World().MaterialWarning(index);
            if (!warning.empty()) lines.push_back(warning);
            if (info.capacity > 0) {
                std::string inventory = "库存：";
                for (int a = 0; a < Workshop::ItemCount; ++a) if (cell.Inventory(static_cast<Workshop::Ammo>(a)) > 0)
                    inventory += std::string(Workshop::AmmoName(a)) + " " + std::to_string(cell.Inventory(static_cast<Workshop::Ammo>(a))) + "  ";
                lines.push_back(cell.TotalStock() > 0 ? inventory : "库存：空");
            }
            if (type == Workshop::Machine::Counter) lines.push_back(std::string("计量：")
                + std::array<const char*, 3>{"信号上升沿", "来源机器的实际产出", "来源机器的实际出库/发射"}[static_cast<int>(cell.counterMode)] + "；B 清零");
            if (type == Workshop::Machine::EnemySensor) lines.push_back(std::format("本行敌人达到 {} 只时输出高电平", cell.setting));
            if (type == Workshop::Machine::Cooler) lines.push_back(cell.recipe == 1 ? "当前配方：冰瓜" : "当前配方：寒冰豌豆");
            if (type == Workshop::Machine::SalvagePort || type == Workshop::Machine::Recycler)
                lines.push_back(std::format("待提取残骸 {}；本波掉落 {}/40", World().PendingScrap(), World().WaveScrap()));
            if (type == Workshop::Machine::Feeder || type == Workshop::Machine::Cannon || type == Workshop::Machine::EnemySensor || type == Workshop::Machine::StockSensor)
                lines.push_back("目标路线：第 " + std::to_string(World().ActiveRoute(index) + 1) + " 路");
            if (type == Workshop::Machine::Store || type == Workshop::Machine::Feeder || type == Workshop::Machine::Cannon || type == Workshop::Machine::Counter || type == Workshop::Machine::StockSensor || type == Workshop::Machine::Splitter)
                lines.push_back(std::string(type == Workshop::Machine::Splitter ? "右侧筛选：" : "弹种筛选：") + Workshop::AmmoName(World().ActiveFilter(index)));
            if (Workshop::HasSelectionInput(type)) lines.push_back("A运行 / B选择；当前方案 " + std::to_string(World().ActivePlan(index) + 1) + "，可点配置分别设置。" );
            if (type == Workshop::Machine::Splitter) lines.push_back(cell.strictSplit ? "严格选路：仅送选中出口，堵塞时等待。" : "普通模式：筛选优先；混合时轮流或按B信号优先供货。");
            if (type == Workshop::Machine::Counter) lines.push_back(std::format("当前批次 {} / {}；{}", cell.count, cell.setting, cell.counterOutput == Workshop::CounterOutput::Hold ? "达标保持" : "每批翻转"));
            if (type == Workshop::Machine::Cannon) lines.push_back(std::format("充能 {:.1f}/12秒 · 每轮3发 · 不限台数", cell.clock));
        } else if (!World().Unlocked(type)) lines.push_back("解锁：守住第 " + std::to_string(info.unlockWave) + " 波");
        if (Workshop::CanUpgradeMachine(type) && level < Workshop::MaxUpgradeLevel)
            lines.push_back("下一级：" + MachineEffect(type, level + 1));
    }
    if ((hoveredShooter && Workshop::FindPlant(hoveredShooter->mPlantType) && Workshop::FindPlant(hoveredShooter->mPlantType)->upgrades) || (!hoveredShooter && Workshop::CanUpgradeMachine(type))) {
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
    if (!mMaterialWarnings[index].empty()) {
        g->FillCircle(x + 46, y + 5, 7, glm::vec4(180, 62, 27, 255));
        g->FillRect(x + 45, y + 0, 2, 6, kGold); g->FillCircle(x + 46, y + 9, 1.2f, kGold);
    }
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
    else if (cell.type == M::Grower || cell.type == M::SporeGrower || cell.type == M::MelonGrower) {
        g->FillRect(x + 10, y + 11, 30, 25, glm::vec4(63, 64, 35, 255));
        DrawItemIcon(g, Workshop::ProducedItem(cell.type), x + 15, y + 15, 20);
        g->FillRect(x + 9, y + 38, 32 * std::min(1.0f, cell.clock / Workshop::ProductionPeriod(cell.type, cell.level)), 3, kGold);
    } else if (cell.type == M::Cooler) {
        g->FillRect(x + 8, y + 10, 34, 28, glm::vec4(68, 126, 142, 255));
        g->DrawRect(x + 8, y + 10, 34, 28, glm::vec4(173, 231, 239, 255));
        DrawItemIcon(g, cell.recipe == 1 ? Workshop::Ammo::IceMelon : Workshop::Ammo::Ice, x + 16, y + 13, 19);
        g->FillRect(x + 9, y + 36, 32 * std::min(1.0f, cell.clock / Workshop::ProductionPeriod(cell.type, cell.level)), 3, glm::vec4(150, 234, 255, 255));
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
    } else if (cell.type == M::SalvagePort || cell.type == M::Recycler) {
        g->FillRect(x + 9, y + 10, 32, 27, glm::vec4(92, 72, 56, 255));
        DrawItemIcon(g, Workshop::Ammo::Scrap, x + 14, y + 12, 23);
        if (cell.type == M::Recycler) label("+5", x + 13, y + 28, 11);
        else Arrow(g, x + 37, y + 27, cell.direction, kGold);
    } else if (cell.type == M::Feeder) {
        DrawUtilityIcon(g, "magazine", x + 12, y + 9, 28);
        Arrow(g, x + 9, y + 19, 0, kGold);
        const int plan = cell.inputs[1] >= 0 ? static_cast<int>(World().Cells()[cell.inputs[1]].signal) : cell.plan;
        if (details && displayScale >= 0.85f) label(std::to_string((plan ? cell.route2 : cell.route) + 1) + "路", x + 14, y + 30, 12);
    } else if (cell.type == M::Cannon) {
        const float recoil = cell.volleyRemaining > 0 && cell.volleyClock > .1f ? 2.0f : 0;
        DrawPlantPicture(g, PlantType::PLANT_COBCANNON, x + 6 - recoil, y + 7, 39, 29);
        const int plan = cell.inputs[1] >= 0 ? static_cast<int>(World().Cells()[cell.inputs[1]].signal) : cell.plan;
        DrawItemIcon(g, (plan ? cell.filter2 : cell.filter) == static_cast<int>(Workshop::Ammo::IceMelon) ? Workshop::Ammo::IceMelon : Workshop::Ammo::Melon, x + 32, y + 8, 13);
        g->FillRect(x + 8, y + 43, 34 * cell.clock / Workshop::CannonChargeSeconds, 3, kGold);
    } else {
        const char* glyph = cell.type == M::And ? "&" : cell.type == M::Or ? "OR" : cell.type == M::Not ? "!"
            : cell.type == M::Latch ? "M" : cell.type == M::Timer ? "T" : cell.type == M::EnemySensor ? "Z"
            : cell.type == M::StockSensor ? "#" : cell.type == M::Counter ? "N" : "I/O";
        label(glyph, x + 13, y + 12, 21);
        if (cell.type == M::Counter && details && displayScale >= 0.85f) label(std::to_string(cell.count), x + 13, y + 34, 10);
        g->FillCircle(x + 40, y + 11, 4, cell.signal ? glm::vec4(131, 255, 93, 255) : glm::vec4(32, 43, 36, 255));
    }
    if (!logic && cell.type != M::Solar) {
        int icon = 0;
        for (int a = 0; a < Workshop::ItemCount && icon < 2; ++a) if (cell.Inventory(static_cast<Workshop::Ammo>(a)) > 0)
            DrawItemIcon(g, static_cast<Workshop::Ammo>(a), x + 3 + icon++ * 12, y + 37, 11);
        if (details && displayScale >= 0.85f) label(std::to_string(cell.TotalStock()), x + 28, y + 37, 11);
        if (Workshop::HasDirectionalOutput(cell.type)) {
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
            continue; // 工厂视图由整行弹仓按钮显示库存，扩大点击面积。
        }
        const float y = kFrontY + row * kFrontRowHeight;
        g->FillRect(12, y + 4, 195, 98, glm::vec4(232, 220, 168, 238));
        Text(std::format("第{}路 · 敌{} · 仓{}/80", row + 1, enemies, lane.TotalStock()), 25, y + 8, 13);
        Text(std::format("普{} 冰{} 孢{} 瓜{} 冻{}", lane.ammo, lane.iceAmmo, lane.Inventory(Workshop::Ammo::Spore), lane.Inventory(Workshop::Ammo::Melon), lane.Inventory(Workshop::Ammo::IceMelon)), 25, y + 27, 11, kGreen);
        const auto* selected = SelectedFrontPlant();
        if (selected && selected->mRow == row) {
            const auto* info = Workshop::FindPlant(selected->mPlantType);
            Text(info ? info->name : "植物", 25, y + 42, 13, kGreen);
            Text(std::format("HP {}", selected->mPlantHealth), 123, y + 44, 10);
        } else Text(World().FocusRoute(World().AssaultStage()) == row ? "主攻路线 · 优先补弹" : "点击格子种植 / 选择", 25, y + 43, 12,
            World().FocusRoute(World().AssaultStage()) == row ? glm::vec4(171, 61, 29, 255) : kGreen);
        if (World().FocusRoute(World().AssaultStage()) == row) g->DrawRect(12, y + 4, 195, 98, glm::vec4(197, 76, 32, 255));

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
    Text(std::format("{}×{} · {:.0f}%", World().Columns(), World().FactoryRows(), mMapZoom * 100), 942, 131, 13, kGreen);
    DrawQuickBuild(g);
    if (mSelected >= 0) {
        const auto& cell = World().Cells()[mSelected];
        Text(std::string(Workshop::Describe(cell.type).name) + (Workshop::CanUpgradeMachine(cell.type) ? " Lv." + std::to_string(cell.level) : ""), 815, 320, 16, kGreen);
        const auto warning = World().MaterialWarning(mSelected);
        g->PushClipRect(813, 342, 265, 20);
        Text(warning.empty() ? World().Status(mSelected) : "来料/设置不匹配 · 双击查看", 815, 342, 13, warning.empty() ? kInk : glm::vec4(170,57,27,255));
        g->PopClipRect();
    } else {
        Text("单击选择 · 双击配置", 815, 323, 16, kGreen);
        Text("R 旋转 · 按住左键拖图", 815, 347, 13);
    }
    DrawFront(g);
}

void WorkshopScene::DrawWorkshop(Graphics* g) {
    if (!mResourcesReady) return;
    if (!mFrontView) g->FillRect(0, 0, SCENE_WIDTH, SCENE_HEIGHT, glm::vec4(232, 220, 168, 255));
    g->FillRect(0, 0, 1100, mFrontView ? 44.0f : 124.0f, glm::vec4(232, 220, 168, 245));
    if (mFrontView) {
        g->FillRect(867, 44, 233, 44, glm::vec4(232, 220, 168, 238));
        Text(std::format("倍速 ×{:.1f}", DeltaTime::GetSelectedTimeScale()), 877, 59, 13, kGreen);
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
        + (phase == Workshop::Phase::Assault ? "袭击中" : phase == Workshop::Phase::Dead ? "防线失守" : "准备中"), 475, 7, 17);
    Text(World().FocusRoute(0) < 0 ? "各路均匀来敌" : std::format("主攻 {} → {} → {} 路", World().FocusRoute(0) + 1, World().FocusRoute(1) + 1, World().FocusRoute(2) + 1), 475, 30, 13, kGreen);
    Text(std::format("倍速 ×{:.1f}", DeltaTime::GetSelectedTimeScale()), 877, 99, 13, kGreen);
    for (const auto& threat : Workshop::Threats) if (threat.wave > World().CompletedWaves()) {
        Text(std::to_string(threat.wave) + "波 " + threat.name, 646, 14, 13, kGreen); break;
    }
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
    j["frontTool"] = static_cast<int>(mFrontTool); j["frontBuildType"] = static_cast<int>(mFrontBuildType);
    j["plantCatalogOpen"] = mPlantCatalog && mPlantCatalog->IsActive();
    j["guideOpen"] = mGuide && mGuide->IsActive(); j["guideSeenWave"] = mGuideSeenWave;
    j["gridOpen"] = mGridDialog && mGridDialog->IsActive(); j["powerGuideSeen"] = mPowerGuideSeen;
    j["powerDemo"] = mPowerDemo ? mPowerDemo->Inspect() : nlohmann::json(nullptr);
    j["guideTopics"] = mGuideTopics; j["guidePage"] = mGuidePage; j["guideUnlockWave"] = mGuideUnlockWave;
    j["materialWarnings"] = mMaterialWarnings; j["materialNoticeCount"] = mMaterialNoticeCount;
    j["configOpen"] = mMachineConfig && mMachineConfig->IsActive(); j["configMachine"] = mConfigMachine;
    j["frontColumns"] = Workshop::FrontColumns;
    j["threats"] = nlohmann::json::array();
    for (const auto& threat : Workshop::Threats) j["threats"].push_back({{"wave", threat.wave}, {"name", threat.name}});
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
    j["selectedShooter"] = SelectedFrontPlant() ? mSelectedShooter : -1;
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
            const int level = p->GetWorkshopUpgradeLevel();
            nlohmann::json plant{{"id", id}, {"row", p->mRow}, {"column", p->mColumn}, {"hp", p->mPlantHealth}, {"shadow", p->GetShadow() != nullptr}, {"headTrack", headTrack},
                {"level", level}, {"type", static_cast<int>(p->mPlantType)}, {"ice", p->mPlantType == PlantType::PLANT_SNOWPEA}, {"baseCooldown", Workshop::ShooterCooldowns[level - 1]}};
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
            j["nativeZombies"].push_back({{"id", id}, {"row", z->mRow}, {"hp", z->mBodyHealth}, {"helmetHp", z->mHelmHealth}, {"chilled", z->GetCooldownTimer() > 0}, {"eating", z->IsEating()}, {"dying", z->IsDying()}, {"shadow", z->GetShadow() != nullptr},
                {"track", animator ? animator->GetCurrentTrackName() : ""}, {"tongue", animator && animator->GetTrackVisible("anim_tongue")}});
        }
    }
    j["nativeBulletCount"] = mBoard->mEntityRegistry.GetAllBulletIDs().size();
    j["saveFailed"] = mSaveFailed;
    j["tooltipVisible"] = !mTooltipLines.empty();
    j["tooltipLines"] = mTooltipLines;
    return j;
}
