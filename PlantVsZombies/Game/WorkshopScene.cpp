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
constexpr float kGridX = 20, kGridY = 132; // 工厂左上角，逻辑像素；与点击/信号绘制共用
constexpr float kCellW = 54, kCellH = 62; // 工厂格尺寸，逻辑像素
constexpr float kFrontY = 88, kFrontRowHeight = 100; // 沿用平地草坪的五行边界，逻辑像素
constexpr float kAutosaveSeconds = 5; // 活跃游玩时自动保存间隔，单位：秒
const glm::vec4 kInk{48, 43, 30, 255};
const glm::vec4 kGold{255, 223, 130, 255};
const glm::vec4 kGreen{66, 107, 51, 255};

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
    mResourcesReady = resources.HasReanimation("NormalZombie") && resources.HasReanimation("PeaShooter");
    for (const char* key : {"IMAGE_BACKGROUND_DAY", "IMAGE_PEASHOOTER", "IMAGE_SUNFLOWER", "IMAGE_PROJECTILEPEA", "IMAGE_BUTTONSMALL"})
        mResourcesReady = resources.GetTexture(key, false) && mResourcesReady;
    if (!mResourcesReady) {
        mLoadFailed = true; mLoadError = "工坊必需的原版资源未加载，请检查资源注册。";
    }
    Scene::OnEnter();
    CreateControls();
    mNotice = "先选育豆机，再铺运输带连接本行装填口；准备好后主动开波。";
}

void WorkshopScene::OnExit() {
    if (!mLoadFailed) SaveBase();
    mMachineButtons.clear(); mFactoryControls.clear(); mFrontControls.clear();
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
    mLoadFailed = false; mDeathShown = false; return true;
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
    MakeButton("查看", 20, 92, 75, [this] { mTool = Tool::Inspect; mWireSource = -1; });
    MakeButton("拆除", 100, 92, 75, [this] { mTool = Tool::Remove; mWireSource = -1; });
    MakeButton("旋转 R", 180, 92, 90, [this] {
        if (mTool == Tool::Build) mDirection = (mDirection + 1) % 4; else Act("rotate", mSelected);
    });
    MakeButton("接 A 口", 275, 92, 90, [this] { mTool = Tool::WireA; mWireSource = -1; });
    MakeButton("接 B 口", 370, 92, 90, [this] { mTool = Tool::WireB; mWireSource = -1; });
    MakeButton("断开输入", 465, 92, 100, [this] { Act("disconnect", mSelected); });
    MakeButton("配置", 570, 92, 90, [this] { Act("configure", mSelected); });
    for (int row = 0; row < Workshop::Rows; ++row)
        MakeButton("+射手", 1005, kGridY + row * kCellH + 35, 73, [this, row] { Act("shooter", row); }, 23, 13);
    for (int i = 1; i < static_cast<int>(Workshop::Machine::Count); ++i) {
        const auto type = static_cast<Workshop::Machine>(i);
        auto button = MakeButton(Workshop::Describe(type).name, 20 + ((i - 1) % 7) * 93.0f,
            490 + ((i - 1) / 7) * 46.0f, 88, [this, type] { SelectBuild(type); }, 40, 16);
        mMachineButtons.push_back(button);
    }
    for (size_t i = firstFactoryControl; i < mUIManager.GetButtonCount(); ++i)
        mFactoryControls.push_back(mUIManager.GetButton(i));
    for (int row = 0; row < Workshop::Rows; ++row) {
        auto button = MakeButton("增援射手 " + std::to_string(Workshop::ShooterCost), 25, kFrontY + row * kFrontRowHeight + 49, 165,
            [this, row] { Act("shooter", row); });
        mFrontControls.push_back(button);
    }
    SetFrontView(World().State() != Workshop::Phase::Planning);
}

void WorkshopScene::SetFrontView(bool front) {
    mFrontView = front;
    for (const auto& button : mFactoryControls) { button->SetEnabled(!front); button->SetSkipDraw(front); }
    for (const auto& button : mFrontControls) { button->SetEnabled(front); button->SetSkipDraw(!front); }
    if (mViewButton) mViewButton->SetText(front ? "返回工厂" : "查看前线");
    // 前线工具栏压成一行，给第一行僵尸头部留出原版草坪的上沿空间。
    constexpr std::array<float, 8> toolbarX{20, 142, 264, 386, 508, 630, 752, 916};
    for (size_t i = 0; i < toolbarX.size(); ++i)
        if (auto button = mUIManager.GetButton(i)) button->SetPosition(Vector(toolbarX[i], front ? 8.0f : 52.0f));
    mWireSource = -1;
}

std::shared_ptr<Button> WorkshopScene::GetControlButton(const std::string& action) const {
    constexpr std::array<const char*, 8> actions{"pause", "start", "expand", "repair", "help", "reset", "back", "view"};
    for (size_t i = 0; i < actions.size(); ++i) if (action == actions[i]) return mUIManager.GetButton(i);
    return nullptr;
}

void WorkshopScene::SelectBuild(Workshop::Machine type) {
    if (!World().Unlocked(type)) {
        mNotice = "守住第 " + std::to_string(Workshop::Describe(type).unlockWave) + " 波后解锁该机器。"; return;
    }
    mBuildType = type; mTool = Tool::Build; mWireSource = -1;
    mNotice = Workshop::Describe(type).description;
}

bool WorkshopScene::Act(const std::string& action, int index, const std::string& type, int value) {
    if (mLoadFailed) return false;
    bool result = false;
    if (action == "build") result = World().Build(index, Workshop::FindMachine(type), value);
    else if (action == "remove") result = World().Remove(index);
    else if (action == "rotate") result = World().Rotate(index);
    else if (action == "configure") result = World().Configure(index);
    else if (action == "wire_a" || action == "wire_b") result = World().Connect(value, index, action == "wire_b" ? 1 : 0);
    else if (action == "disconnect") {
        result = World().Connect(-1, index, 0); World().Connect(-1, index, 1);
    }
    else if (action == "shooter") result = mBoard->AddWorkshopShooter(index);
    else if (action == "expand") result = World().Expand();
    else if (action == "repair") result = World().Repair();
    else if (action == "start") { result = mBoard->BeginWorkshopAssault(); if (result) { mPaused = false; SetFrontView(true); DeltaTime::SetPaused(false); } }
    else if (action == "view_front") { SetFrontView(true); return true; }
    else if (action == "view_factory") { SetFrontView(false); return true; }
    else if (action == "pause") { mPaused = !mPaused; DeltaTime::SetPaused(mPaused); return true; }
    else if (action == "retry") { const auto retry = mBoard->WorkshopRetryDocument(); result = !retry.is_null() && RebuildFrontline(retry); mDeathShown = false; mPaused = false; SetFrontView(false); DeltaTime::SetPaused(false); }
    else if (action == "reset") { result = RebuildFrontline(nullptr); mDeathShown = false; mPaused = false; mSelected = -1; SetFrontView(false); DeltaTime::SetPaused(false); }
    else if (action == "save") return SaveBase();
    else if (action == "reload") return ReloadBase();
    if (result) {
        mNotice = action == "start" ? "袭击开始！库存和生产线继续运行，可随时暂停调整。"
            : action == "retry" ? "已恢复本次袭击开始前的基地；调整后可重新挑战。" : "已保存基地。";
        SaveBase();
    } else {
        mNotice = action == "expand" ? "扩建需守住第 3 波、250 阳光，最多扩至 12 列。"
            : action == "shooter" ? "增援需要 150 阳光；每行同时最多 3 株。"
            : "操作未完成：检查能源、空地、解锁条件或信号端口。";
    }
    return result;
}

void WorkshopScene::HandleGridClick(int index) {
    if (index < 0 || index >= Workshop::CellCount) return;
    mSelected = index;
    if (mTool == Tool::Build) Act("build", index, Workshop::Describe(mBuildType).key, mDirection);
    else if (mTool == Tool::Remove) Act("remove", index);
    else if (mTool == Tool::WireA || mTool == Tool::WireB) {
        if (mWireSource < 0) { mWireSource = index; mNotice = "来源已选：再点击目标机器；右键取消。"; }
        else { Act(mTool == Tool::WireA ? "wire_a" : "wire_b", index, "", mWireSource); mWireSource = -1; }
    } else mNotice = Workshop::Describe(World().Cells()[index].type).description;
}

void WorkshopScene::ConfirmReset() {
    GameMessageBox::Builder(Vector(550, 290)).Title("重新经营一座基地？")
        .Message("将清空工坊的布局、资源、解锁和袭击进度。冒险及其他模式不受影响。")
        .Button("取消", Vector(0, 0), Vector(200, 50), 20, [] {})
        .Button("重开基地", Vector(0, 0), Vector(200, 50), 20, [this] { mPending = Pending::Reset; }).Show();
}
void WorkshopScene::ShowHelp() {
    GameMessageBox::Builder(Vector(550, 290)).Title("戴夫工坊 · 从一条生产线开始")
        .Message("集光器持续供能。育豆机制造豌豆，运输带按箭头送货，装填口供应同一行射手。\n先在每行接通 育豆机 → 运输带 → 装填口，再开始袭击。射手没有弹药就停火。\n选择机器后点击空格建造；R 旋转，右键退出建造。暂停时也能施工。拆除返还建造费。\n第 1 波解锁仓库与分流器，第 2 波解锁控制电路，第 3 波可扩建。\n接线：先选来源，再选目标。A 控制机器启停；与/或/记忆器还有 B 口。库存器检测 A 来源库存，未接线时检测本行弹仓。\n基地与冒险完全独立。死亡可回到本波开战前调整，也可重开整个基地。")
        .Button("开始经营", Vector(0, 0), Vector(200, 50), 20, [] {}).Show();
}
void WorkshopScene::ShowDeath() {
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
    const bool modal = mUIManager.GetTopActiveMessageBox() != nullptr;
    if (!modal) {
        auto& input = GameAPP::GetInstance().GetInputHandler();
        if (input.IsKeyPressed(SDLK_SPACE)) Act("pause");
        if (!mFrontView && input.IsKeyPressed(SDLK_r)) {
            if (mTool == Tool::Build) mDirection = (mDirection + 1) % 4; else Act("rotate", mSelected);
        }
        if (input.IsMouseButtonPressed(SDL_BUTTON_RIGHT)) { mTool = Tool::Inspect; mWireSource = -1; }
        if (!mFrontView && input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
            const auto point = input.GetMousePosition();
            const int col = static_cast<int>(std::floor((point.x - kGridX) / kCellW));
            const int row = static_cast<int>(std::floor((point.y - kGridY) / kCellH));
            if (col >= 0 && col < Workshop::MaxColumns && row >= 0 && row < Workshop::Rows)
                HandleGridClick(row * Workshop::MaxColumns + col);
        }
    }
    if (!mPaused && !modal) {
        const float dt = DeltaTime::GetDeltaTime();
        mVisualTime += dt;
        mSaveClock += dt;
        if (mSaveClock >= kAutosaveSeconds) { mSaveClock = 0; SaveBase(); }
    }
    if (before != World().State()) SaveBase();
    if (waves != World().CompletedWaves()) mNotice = "守住了第 " + std::to_string(World().CompletedWaves()) + " 波！基地保留，准备好再开启下一波。";
    if (World().State() == Workshop::Phase::Dead && !mDeathShown && !modal) ShowDeath();
    DeltaTime::SetPaused(mPaused || mUIManager.GetTopActiveMessageBox() != nullptr || World().State() == Workshop::Phase::Dead);
    mPauseButton->SetText(mPaused ? "继续运行" : "暂停规划");
    mStartButton->SetEnabled(World().State() == Workshop::Phase::Planning);
    for (size_t i = 0; i < mMachineButtons.size(); ++i) {
        const auto type = static_cast<Workshop::Machine>(i + 1); const auto& info = Workshop::Describe(type);
        mMachineButtons[i]->SetText(std::string(info.name) + (World().Unlocked(type) ? " " + std::to_string(info.cost) : " 锁"), ResourceKeys::Fonts::FONT_FZCQ, 14);
    }
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
    RegisterDrawCommand("WorkshopTooltip", [this](Graphics* g) { DrawTooltip(g); }, LAYER_UI + 5000);
}

/** 使用墙钟延迟显示：暂停施工时仍可阅读功能，状态内容从正式模型实时生成。 */
void WorkshopScene::UpdateTooltip() {
    int key = -1, index = -1;
    auto type = Workshop::Machine::Empty;
    const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
    if (!mFrontView && !mUIManager.GetTopActiveMessageBox()) {
        for (size_t i = 0; i < mMachineButtons.size(); ++i) if (mMachineButtons[i]->IsHovered()) {
            type = static_cast<Workshop::Machine>(i + 1); key = 100 + static_cast<int>(i); break;
        }
        if (mouse.x >= kGridX && mouse.x < kGridX + kCellW * World().Columns()
            && mouse.y >= kGridY && mouse.y < kGridY + kCellH * Workshop::Rows) {
            index = static_cast<int>((mouse.y - kGridY) / kCellH) * Workshop::MaxColumns + static_cast<int>((mouse.x - kGridX) / kCellW);
            type = World().Cells()[index].type;
            if (type != Workshop::Machine::Empty) key = index;
        }
    }
    mTooltipLines.clear();
    if (key != mHoverKey) { mHoverKey = key; mHoverStarted = SDL_GetTicks64(); }
    if (key < 0 || SDL_GetTicks64() - mHoverStarted < 300) return;
    const auto& info = Workshop::Describe(type);
    std::vector<std::string> lines{std::string(info.name) + " · 建造 " + std::to_string(info.cost) + " 阳光", info.description};
    if (type == Workshop::Machine::Grower)
        lines.push_back(std::format("产速：{:.1f} 秒/颗；消耗：{:.0f} 阳光/颗", Workshop::GrowPeriod, Workshop::GrowEnergy));
    else if (type == Workshop::Machine::Solar)
        lines.push_back(std::format("产能：{:.0f} 阳光/秒；全基地共享", Workshop::SolarRate));
    else if (info.capacity > 0)
        lines.push_back(std::format("输送：每 {:.2f} 秒至多 1 颗", Workshop::TransportPeriod));
    if (info.capacity > 0) lines.push_back("内部容量：" + std::to_string(info.capacity) + " 颗");
    if (index >= 0) {
        lines.push_back("状态：" + World().Status(index));
        if (info.capacity > 0) lines.push_back("当前库存：" + std::to_string(World().Cells()[index].stock) + " 颗");
    } else if (!World().Unlocked(type)) lines.push_back("解锁：守住第 " + std::to_string(info.unlockWave) + " 波");
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
    if (mTooltipLines.empty() || mUIManager.GetTopActiveMessageBox()) return;
    const float x = mTooltipPosition.x, y = mTooltipPosition.y;
    const float height = 22.0f * static_cast<float>(mTooltipLines.size()) + 20;
    g->FillRect(x + 4, y + 4, 370, height, glm::vec4(0, 0, 0, 105));
    g->FillRect(x, y, 370, height, glm::vec4(38, 43, 32, 245));
    g->DrawRect(x, y, 370, height, kGold);
    for (size_t i = 0; i < mTooltipLines.size(); ++i)
        Text(mTooltipLines[i], x + 10, y + 10 + static_cast<float>(i) * 22, 16, i == 0 ? kGold : glm::vec4(239, 235, 204, 255));
}

void WorkshopScene::DrawMachine(Graphics* g, int index, float x, float y) {
    const auto& cell = World().Cells()[index];
    if (cell.type == Workshop::Machine::Empty) return;
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
        g->FillRect(x + 9, y + 38, 32 * std::min(1.0f, cell.clock / Workshop::GrowPeriod), 3, kGold);
    } else if (cell.type == M::Belt || cell.type == M::Splitter) {
        g->FillRect(x + 8, y + 9, 34, 30, glm::vec4(43, 46, 38, 255));
        for (int n = 0; n < 5; ++n) {
            const float offset = std::fmod(n * 7.0f + mVisualTime * (World().Enabled(index) ? 13 : 0), 34.0f);
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
        Arrow(g, x + 27, y + 38, 0, kGold);
    } else {
        const char* glyph = cell.type == M::And ? "&" : cell.type == M::Or ? "OR" : cell.type == M::Not ? "!"
            : cell.type == M::Latch ? "M" : cell.type == M::Timer ? "T" : cell.type == M::EnemySensor ? "Z"
            : cell.type == M::StockSensor ? "#" : "I/O";
        Text(glyph, x + 13, y + 12, 21, kGold);
        g->FillCircle(x + 40, y + 11, 4, cell.signal ? glm::vec4(131, 255, 93, 255) : glm::vec4(32, 43, 36, 255));
    }
    if (!logic && cell.type != M::Solar) {
        if (cell.stock > 0) Sprite(g, "IMAGE_PROJECTILEPEA", x + 3, y + 34, 12, 12);
        Text(std::to_string(cell.stock), x + 18, y + 37, 12, kGold);
        if (cell.type != M::Feeder) Arrow(g, x + 42, y + 53, cell.direction, kInk);
    }
    if (!logic && !World().Enabled(index) && cell.type != M::Splitter)
        g->FillCircle(x + 41, y + 12, 4, glm::vec4(225, 92, 70, 255));
}

void WorkshopScene::DrawFront(Graphics* g) {
    for (int row = 0; row < Workshop::Rows; ++row) {
        const auto& lane = World().Lanes()[row];
        const int enemies = World().EnemyCounts()[row];
        if (!mFrontView) {
            const float y = kGridY + row * kCellH;
            g->FillRect(690, y, 390, kCellH - 1, glm::vec4(231, 221, 174, 245));
            Text("第 " + std::to_string(row + 1) + " 路", 702, y + 7, 18, kGreen);
            Text("豌豆 " + std::to_string(lane.ammo) + "/" + std::to_string(Workshop::MagazineCapacity), 792, y + 9, 17);
            Text("敌人 " + std::to_string(enemies), 933, y + 9, 17, enemies > 0 ? glm::vec4(160, 50, 35, 255) : kInk);
            g->FillRect(705, y + 38, 190, 9, glm::vec4(89, 100, 59, 255));
            g->FillRect(705, y + 38, 190.0f * lane.ammo / Workshop::MagazineCapacity, 9, glm::vec4(125, 183, 62, 255));
            Text(std::to_string(lane.shooters) + " 株", 917, y + 35, 15);
            continue;
        }
        const float y = kFrontY + row * kFrontRowHeight;
        g->FillRect(12, y + 6, 195, 86, glm::vec4(232, 220, 168, 238));
        Text("第 " + std::to_string(row + 1) + " 路 · 豌豆 " + std::to_string(lane.ammo), 25, y + 15, 18);
        g->FillRect(25, y + 39, 165, 6, glm::vec4(54, 64, 38, 255));
        g->FillRect(25, y + 39, 165.0f * lane.ammo / Workshop::MagazineCapacity, 6, glm::vec4(160, 211, 68, 255));

    }
}

void WorkshopScene::DrawWorkshop(Graphics* g) {
    if (!mResourcesReady) return;
    // 沿用原版平地纹理作庭院背景；工厂与前线几何由上述共享常量定义，不复用 Board 的格位。
    if (!mFrontView) Sprite(g, "IMAGE_BACKGROUND_DAY", -250, -50, 1880, 720);
    g->FillRect(0, 0, 1100, mFrontView ? 44.0f : 124.0f, glm::vec4(232, 220, 168, 245));
    if (!mFrontView) g->FillRect(0, 450, 1100, 150, glm::vec4(232, 220, 168, 250));
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
    Text(mSaveFailed ? "保存失败 · 检查磁盘权限" : mPaused ? "已暂停 · 可以施工" : "独立基地 · 自动保存",
        780, 18, 18, mSaveFailed ? glm::vec4(170, 45, 30, 255) : kGreen);
    Text("前线概况 · 上方可切换完整战场", 710, 99, 18, kGreen);
    g->FillRect(kGridX - 4, kGridY - 4, kCellW * Workshop::MaxColumns + 7, kCellH * Workshop::Rows + 7, glm::vec4(87, 70, 42, 255));
    for (int row = 0; row < Workshop::Rows; ++row) for (int col = 0; col < Workshop::MaxColumns; ++col) {
        const int i = row * Workshop::MaxColumns + col;
        const float x = kGridX + col * kCellW, y = kGridY + row * kCellH;
        const bool locked = col >= World().Columns();
        g->FillRect(x, y, kCellW - 1, kCellH - 1, locked ? glm::vec4(92, 90, 65, 245) : glm::vec4(203, 194, 145, 245));
        if (locked) { if (row == 2) Text("扩建", x + 10, y + 24, 14, kGold); }
        else DrawMachine(g, i, x, y);
        if (i == mSelected || i == mWireSource) {
            g->DrawRect(x + 1, y + 1, kCellW - 3, kCellH - 3, glm::vec4(255, 247, 111, 255));
            g->DrawRect(x + 2, y + 2, kCellW - 5, kCellH - 5, glm::vec4(255, 247, 111, 255));
        }
    }
    if (mTool == Tool::WireA || mTool == Tool::WireB || mTool == Tool::Inspect)
        for (int i = 0; i < Workshop::CellCount; ++i) for (int p = 0; p < 2; ++p) {
            const int source = World().Cells()[i].inputs[p]; if (source < 0) continue;
            const auto color = World().Cells()[source].signal ? glm::vec4(70, 250, 96, 230) : glm::vec4(117, 55, 165, 210);
            const float sx = kGridX + source % Workshop::MaxColumns * kCellW + 26, sy = kGridY + source / Workshop::MaxColumns * kCellH + 9;
            const float tx = kGridX + i % Workshop::MaxColumns * kCellW + 10 + p * 28, ty = kGridY + i / Workshop::MaxColumns * kCellH + 8;
            g->DrawLine(sx, sy, tx, ty, color); g->FillCircle(tx, ty, 3, color);
        }
    DrawFront(g);
    Text(mNotice, 20, 459, 16);
    const char* directions[] = {"右", "下", "左", "上"};
    Text(mTool == Tool::Build ? std::string("建造：") + Workshop::Describe(mBuildType).name + " · 朝" + directions[mDirection]
        : mTool == Tool::Remove ? "拆除工具 · 点击机器返还建造费" : mTool == Tool::WireA || mTool == Tool::WireB ? "信号接线 · 先来源，后目标" : "查看工具 · 点击机器检查状态", 695, 495, 16, kGreen);
    if (mSelected >= 0) {
        const auto& c = World().Cells()[mSelected];
        Text(std::string(Workshop::Describe(c.type).name) + " · " + World().Status(mSelected), 695, 524, 16);
        if (c.type == Workshop::Machine::Timer || c.type == Workshop::Machine::StockSensor)
            Text("当前设定 " + std::to_string(c.setting) + (c.type == Workshop::Machine::Timer ? " 秒" : " 颗") + " · 配置按钮可调整", 695, 551, 15);
        else Text("每行射手最多 3 株 · 增援需 150 阳光", 695, 551, 15);
    } else {
        Text("前线自带每行 1 株豌豆射手", 695, 524, 16);
        Text("Space 暂停 · R 旋转 · 右键取消", 695, 551, 15);
    }
}

nlohmann::json WorkshopScene::Inspect() const {
    auto j = World().Inspect(); j["paused"] = mPaused; j["resourcesReady"] = mResourcesReady;
    j["saveProtected"] = mLoadFailed; j["selected"] = mSelected;
    j["gridX"] = kGridX; j["gridY"] = kGridY; j["cellWidth"] = kCellW; j["cellHeight"] = kCellH;
    j["savePath"] = mSavePath;
    j["frontView"] = mFrontView;
    j["nativeFrontline"] = true;
    j["nativePlants"] = nlohmann::json::array();
    j["nativeZombies"] = nlohmann::json::array();
    j["hasShootingAnimation"] = false;
    j["hasEatingZombie"] = false;
    j["hasDyingZombie"] = false;
    j["hasPeaHitParticles"] = g_particleSystem && g_particleSystem->GetEffectActiveParticleCount("PeaBulletHit") > 0;
    for (int id : mBoard->mEntityRegistry.GetAllPlantIDs()) {
        const auto* p = mBoard->mEntityRegistry.GetPlant(id);
        if (p && p->IsActive()) {
            const auto* shooter = dynamic_cast<const Shooter*>(p);
            const std::string headTrack = shooter && shooter->GetHeadAnimator() ? shooter->GetHeadAnimator()->GetCurrentTrackName() : "";
            if (headTrack == "anim_shooting") j["hasShootingAnimation"] = true;
            j["nativePlants"].push_back({{"id", id}, {"row", p->mRow}, {"hp", p->mPlantHealth}, {"shadow", p->GetShadow() != nullptr}, {"headTrack", headTrack}});
        }
    }
    for (int id : mBoard->mEntityRegistry.GetAllZombieIDs()) {
        const auto* z = mBoard->mEntityRegistry.GetZombie(id);
        if (z && z->IsActive()) {
            if (z->IsEating()) j["hasEatingZombie"] = true;
            if (z->IsDying()) j["hasDyingZombie"] = true;
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
