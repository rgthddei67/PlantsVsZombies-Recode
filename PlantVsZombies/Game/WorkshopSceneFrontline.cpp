#include "WorkshopScene.h"
#include "Board/Board.h"
#include "Plant/Plant.h"
#include "Plant/GameDataManager.h"
#include "Workshop/WorkshopGuide.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <format>

namespace {
const glm::vec4 kFrontInk{57, 48, 32, 255}; // 前线说明文字
const glm::vec4 kFrontGreen{52, 102, 49, 255}; // 可种植和选中卡片
const glm::vec4 kFrontRed{172, 47, 38, 255}; // 铲除与不可种植提示
void FrontText(const std::string& text, float x, float y, int size = 15, glm::vec4 color = kFrontInk) {
    GameAPP::GetInstance().DrawText(text, Vector(x, y), color, ResourceKeys::Fonts::FONT_FZCQ, size);
}
Vector PlantCardPosition(size_t index) { return Vector(72 + (index % 4) * 244.0f, 145 + (index / 4) * 190.0f); }
}

void WorkshopScene::DrawPlantPicture(Graphics* g, PlantType type, float x, float y, float w, float h) {
    const auto* texture = ResourceManager::GetInstance().GetTexture(GameDataManager::GetInstance().GetPlantTextureKey(type), false);
    if (!texture || texture->width <= 0 || texture->height <= 0) return;
    const float scale = std::min(w / texture->width, h / texture->height);
    const float width = texture->width * scale, height = texture->height * scale;
    g->DrawTexture(texture, x + (w - width) / 2, y + (h - height) / 2, width, height);
}

void WorkshopScene::CreateFrontControls() {
    const auto first = mUIManager.GetButtonCount();
    mPlantCatalogButton = MakeButton("　植物 [B]", 216, 48, 140, [this] { OpenPlantCatalog(); }, 31, 16);
    mFrontRemoveButton = MakeButton("　铲除", 364, 48, 90, [this] {
        const bool removing = mFrontTool == FrontTool::Remove; CancelTool();
        if (!removing) mFrontTool = FrontTool::Remove;
    }, 31, 14);
    mFrontUpgradeButton = MakeButton("选中植物升级", 462, 48, 145, [this] { Act("upgrade"); }, 31, 14);
    mAbilityButton = MakeButton("　修复", 615, 48, 115, [this] { Act("plant_ability"); }, 31, 14);
    mAutomaticButton = MakeButton("　自动：关", 738, 48, 125, [this] { Act("plant_auto"); }, 31, 13);
    for (int row = 0; row < Workshop::Rows; ++row)
        MakeButton("弹仓 / 配额", 22, 158 + row * 100.0f, 178, [this, row] { OpenAmmoQuotas(row); }, 28, 14);
    for (size_t i = first; i < mUIManager.GetButtonCount(); ++i) mFrontControls.push_back(mUIManager.GetButton(i));
}

Plant* WorkshopScene::SelectedFrontPlant() const {
    auto* plant = mBoard->mEntityRegistry.GetPlant(mSelectedShooter);
    return plant && plant->IsActive() && plant->mPlantHealth > 0 && Workshop::FindPlant(plant->mPlantType) ? plant : nullptr;
}
int WorkshopScene::FrontCellAt(Vector point) const {
    if (point.y < 88 || point.y >= 575) return -1;
    for (int row = 0; row < Workshop::Rows; ++row) for (int col = 0; col < Workshop::FrontColumns; ++col) {
        const auto center = mBoard->GetCellCenterPosition(row, col);
        if (std::abs(point.x - center.x) < 40 && std::abs(point.y - center.y) < 50) return row * Workshop::FrontColumns + col;
    }
    return -1;
}
Plant* WorkshopScene::FrontPlantAt(Vector point) const {
    const int cell = FrontCellAt(point);
    return cell >= 0 ? mBoard->GetNormalPlantAt(cell / Workshop::FrontColumns, cell % Workshop::FrontColumns) : nullptr;
}
void WorkshopScene::SelectFrontPlant(PlantType type) {
    const auto* definition = Workshop::FindPlant(type);
    if (!definition || World().CompletedWaves() < definition->unlockWave) return;
    CancelTool(); mFrontTool = FrontTool::Plant; mFrontBuildType = type;
    mNotice = std::string("选择格子种植") + definition->name + "；Esc / 右键取消。";
}

void WorkshopScene::ClearFrontPreview() {
    if (mHeldPlantPreview) { mHeldPlantPreview->Die(); mHeldPlantPreview = nullptr; }
    if (mCellPlantPreview) { mCellPlantPreview->Die(); mCellPlantPreview = nullptr; }
    mPlantPreviewCell = -1;
}

void WorkshopScene::UpdateFrontPreview(Graphics* g) {
    if (!mFrontView || mLoadFailed || mFrontTool != FrontTool::Plant || mLeftPanning
        || mUIManager.GetTopActiveMessageBox()) { ClearFrontPreview(); return; }
    const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
    // 工具栏和底部说明不遮一株手持植物；移回草坪时仍保留所选工具。
    if (mouse.x < 207 || mouse.x >= SCENE_WIDTH || mouse.y < 88 || mouse.y >= 575) {
        ClearFrontPreview(); return;
    }
    if (!mHeldPlantPreview) mHeldPlantPreview = mBoard->CreatePlantingPreview(mFrontBuildType);
    if (mHeldPlantPreview) mHeldPlantPreview->SetPosition(g->LogicalToWorld(mouse.x, mouse.y));
    int cell = FrontCellAt(mouse);
    if (cell >= 0 && !mBoard->WorkshopPlantPlacementReason(mFrontBuildType,
        cell / Workshop::FrontColumns, cell % Workshop::FrontColumns).empty()) cell = -1;
    if (cell != mPlantPreviewCell) {
        if (mCellPlantPreview) { mCellPlantPreview->Die(); mCellPlantPreview = nullptr; }
        mPlantPreviewCell = cell;
        if (cell >= 0) mCellPlantPreview = mBoard->CreatePlantingPreview(mFrontBuildType,
            cell / Workshop::FrontColumns, cell % Workshop::FrontColumns);
    }
}

/** 前线也在松手后提交点击，拖过多个格子不会误种或误铲。 */
void WorkshopScene::UpdateFrontInput() {
    auto& input = GameAPP::GetInstance().GetInputHandler();
    const auto point = input.GetMousePosition();
    if (input.IsMouseButtonPressed(SDL_BUTTON_LEFT) && FrontCellAt(point) >= 0) {
        mLeftPending = true; mLeftPanning = false; mLeftStart = point;
        mPressCell = FrontCellAt(point); mFrontPressTool = mFrontTool;
    }
    if (!mLeftPending) return;
    const auto delta = point - mLeftStart;
    if (delta.x * delta.x + delta.y * delta.y >= 64) mLeftPanning = true;
    if (input.IsMouseButtonReleased(SDL_BUTTON_LEFT)) {
        const int cell = FrontCellAt(point);
        const bool click = !mLeftPanning && cell == mPressCell && cell >= 0 && mFrontTool == mFrontPressTool;
        CancelMapGesture();
        if (!click) return;
        const int row = cell / Workshop::FrontColumns, col = cell % Workshop::FrontColumns;
        if (mFrontTool == FrontTool::Plant) {
            const auto* definition = Workshop::FindPlant(mFrontBuildType);
            const auto reason = mBoard->WorkshopPlantPlacementReason(mFrontBuildType, row, col);
            if (reason.empty() && Act("plant", row, definition->key, col)) {
                if (auto* plant = mBoard->GetNormalPlantAt(row, col)) mSelectedShooter = plant->mPlantID;
            } else mNotice = reason;
        } else if (mFrontTool == FrontTool::Remove) {
            if (Act("remove_plant", row, "", col)) {
                if (!input.IsKeyDown(SDLK_LSHIFT) && !input.IsKeyDown(SDLK_RSHIFT)) CancelTool();
                mNotice = "植物已铲除，不返还阳光；Shift 可连续铲除。";
            }
        } else if (auto* plant = mBoard->GetNormalPlantAt(row, col)) mSelectedShooter = plant->mPlantID;
        else mSelectedShooter = -1;
        UpdateUpgradeControls();
    } else if (!input.IsMouseButtonDown(SDL_BUTTON_LEFT)) CancelMapGesture();
}

void WorkshopScene::OpenPlantCatalog() {
    if (!mFrontView || mLoadFailed || mUIManager.GetTopActiveMessageBox()) return;
    CancelMapGesture(); mPlantCatalogNotice.clear(); mTooltipLines.clear();
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(1012, 552);
    builder.Button("返回", Vector(973, 47), Vector(64, 34), 16, [this] { ClosePlantCatalog(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    builder.Button("图解指南", Vector(810, 47), Vector(140, 34), 16, [this] { OpenGuide(); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    for (size_t i = 0; i < Workshop::Plants.size(); ++i) {
        const auto type = Workshop::Plants[i].type;
        builder.Button("", PlantCardPosition(i), Vector(228, 143), 12, [this, type] {
            const auto& info = *Workshop::FindPlant(type);
            if (World().CompletedWaves() < info.unlockWave) { mPlantCatalogNotice = "守住第 " + std::to_string(info.unlockWave) + " 波后解锁"; return; }
            SelectFrontPlant(type); ClosePlantCatalog(false);
        }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    for (size_t i = 0; i < Workshop::Plants.size(); ++i) {
        const auto pos = PlantCardPosition(i);
        builder.Button("图解", pos + Vector(167, 145), Vector(51, 25), 13,
            [this, i] { OpenGuide(Workshop::PlantGuideBase + static_cast<int>(i)); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    mPlantCatalog = builder.Show(); mPlantButtons.clear(); mPlantGuideButtons.clear();
    mPlantCatalog->GetButton(1)->SetTextColor(glm::vec4(255, 228, 142, 255));
    mPlantCatalog->GetButton(1)->SetHoverTextColor(glm::vec4(255, 250, 201, 255));
    for (size_t i = 0; i < Workshop::Plants.size(); ++i) {
        auto button = mPlantCatalog->GetButton(i + 2); button->SetSkipDraw(true); mPlantButtons.push_back(button);
        auto guide = mPlantCatalog->GetButton(i + 2 + Workshop::Plants.size());
        guide->SetTextColor(glm::vec4(255, 228, 142, 255)); guide->SetHoverTextColor(glm::vec4(255, 250, 201, 255));
        guide->SetEnabled(World().CompletedWaves() >= Workshop::Plants[i].unlockWave); mPlantGuideButtons.push_back(guide);
    }
    mPlantCatalog->SetCustomDraw([this](Graphics* g) { DrawPlantCatalog(g); });
    DeltaTime::SetPaused(true);
}
void WorkshopScene::ClosePlantCatalog(bool cancel) {
    if (cancel) CancelTool();
    if (mPlantCatalog) mPlantCatalog->Close();
    mPlantCatalog.reset(); mPlantButtons.clear(); mPlantGuideButtons.clear(); mTooltipLines.clear(); mHoverKey = -1;
}
void WorkshopScene::DrawPlantCatalog(Graphics* g) {
    g->FillRect(44, 24, 1012, 552, glm::vec4(232, 220, 168, 255));
    g->DrawRect(44, 24, 1012, 552, kFrontGreen);
    FrontText("前线植物图册", 72, 49, 28, kFrontGreen);
    FrontText("选植物，再点左侧六列种植 · 点图解查看供弹与摆法", 72, 101, 17);
    mPlantCatalog->GetButton(0)->Draw(g);
    mPlantCatalog->GetButton(1)->Draw(g);
    for (size_t i = 0; i < Workshop::Plants.size(); ++i) {
        const auto& info = Workshop::Plants[i]; const auto pos = PlantCardPosition(i);
        const bool unlocked = World().CompletedWaves() >= info.unlockWave, hover = mPlantButtons[i]->IsHovered();
        g->FillRect(pos.x, pos.y, 228, 176, hover ? glm::vec4(252, 239, 190, 255) : glm::vec4(216, 211, 168, 255));
        g->DrawRect(pos.x, pos.y, 228, 176, hover ? kFrontGreen : glm::vec4(145, 127, 78, 255));
        if (!unlocked) {
            DrawLockedItem(g, pos.x + 114, pos.y + 25, 60);
            FrontText("尚未解锁", pos.x + 78, pos.y + 101, 17);
            FrontText("守住第 " + std::to_string(info.unlockWave) + " 波后解锁", pos.x + 32, pos.y + 140, 15);
            continue;
        }
        FrontText(info.name, pos.x + 12, pos.y + 10, 17, unlocked ? kFrontGreen : kFrontInk);
        FrontText(std::to_string(info.cost) + " 阳光", pos.x + 151, pos.y + 13, 13, World().Energy() >= info.cost ? kFrontInk : kFrontRed);
        DrawPlantPicture(g, info.type, pos.x + 54, pos.y + 38, 120, 78);
        FrontText(info.role, pos.x + 9, pos.y + 120, 12);
        const float remaining = World().PlantCooldown(info.key);
        if (remaining > 0) {
            g->FillRect(pos.x + 8, pos.y + 111, 212 * std::clamp(remaining / GameDataManager::GetInstance().GetPlantCooldown(info.type), 0.0f, 1.0f), 5, kFrontRed);
            FrontText(std::format("补种还需 {:.1f} 秒", remaining), pos.x + 12, pos.y + 151, 13, kFrontRed);
        } else FrontText(World().State() == Workshop::Phase::Planning ? "准备期 · 可连续种植" : "点击选择种植", pos.x + 12, pos.y + 151, 13, kFrontGreen);
        mPlantGuideButtons[i]->Draw(g);
    }
    FrontText(mPlantCatalogNotice.empty() ? "战斗中同种植物共享补种冷却；准备期无冷却。已有植物、血量和成长跨波保留。" : mPlantCatalogNotice, 72, 541, 15);
}

/** 五种弹药共用每路八十份总容量；改配额保留已有库存，并明确显示当前分配。 */
void WorkshopScene::OpenAmmoQuotas(int row) {
    if (row < 0 || row >= Workshop::Rows || mUIManager.GetTopActiveMessageBox()) return;
    CancelMapGesture();
    auto builder = GameMessageBox::Builder(Vector(550, 300)).Panel(700, 450);
    builder.Button("返回", Vector(795, 95), Vector(76, 30), 15, [] {});
    for (int a = 0; a < Workshop::AmmoCount; ++a) for (int sign : {-1, 1}) {
        builder.Button(sign < 0 ? "－5" : "＋5", Vector(sign < 0 ? 675 : 770, 166 + a * 62.0f), Vector(80, 34), 16,
            [this, row, a, sign] {
                const auto item = static_cast<Workshop::Ammo>(a);
                if (World().SetAmmoQuota(row, item, std::clamp(World().Lanes()[row].Quota(item) + sign * 5, 0, Workshop::MagazineCapacity))) SaveBase();
            }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
    }
    mQuotaDialog = builder.Show(); mDismissibleDialog = mQuotaDialog;
    mQuotaDialog->SetCustomDraw([this, row](Graphics* g) { DrawAmmoQuotas(g, row); }); DeltaTime::SetPaused(true);
}
void WorkshopScene::DrawAmmoQuotas(Graphics* g, int row) {
    g->FillRect(200, 75, 700, 450, glm::vec4(232, 220, 168, 255)); g->DrawRect(200, 75, 700, 450, kFrontGreen);
    FrontText("第 " + std::to_string(row + 1) + " 路弹仓", 230, 97, 24, kFrontGreen);
    const auto& lane = World().Lanes()[row];
    FrontText(std::format("总库存 {}/80 · 所有弹种配额合计80", lane.TotalStock()), 230, 137, 16);
    for (int a = 0; a < Workshop::AmmoCount; ++a) {
        const auto item = static_cast<Workshop::Ammo>(a); const float y = 170 + a * 62.0f;
        DrawItemIcon(g, item, 230, y, 28);
        FrontText(Workshop::AmmoName(a), 275, y + 4, 17);
        FrontText(std::format("库存 {} / 配额 {}", lane.Inventory(item), lane.Quota(item)), 402, y + 4, 17);
    }
    for (size_t i = 0; i < 1 + Workshop::AmmoCount * 2; ++i) mQuotaDialog->GetButton(i)->Draw(g);
    FrontText("增额从其他弹种腾出空间；减额保留超额库存，暂停继续装填。", 230, 490, 14);
}

void WorkshopScene::DrawFrontOverlay(Graphics* g) {
    if (!mFrontView || mLoadFailed || mUIManager.GetTopActiveMessageBox()) return;
    const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
    const int cell = FrontCellAt(mouse);
    const auto* selected = SelectedFrontPlant();
    if (mFrontTool == FrontTool::Remove) {
        for (int row = 0; row < Workshop::Rows; ++row) for (int col = 0; col < Workshop::FrontColumns; ++col) {
            const auto center = mBoard->GetCellCenterPosition(row, col);
            g->DrawRect(center.x - 40, center.y - 50, 80, 100, glm::vec4(240, 232, 152, 110));
        }
    }
    if (selected && mFrontTool != FrontTool::Plant) {
        const auto center = mBoard->GetCellCenterPosition(selected->mRow, selected->mColumn);
        g->DrawRect(center.x - 39, center.y - 49, 78, 98, glm::vec4(255, 227, 113, 255));
    }
    if (cell >= 0 && !mLeftPanning && mFrontTool != FrontTool::Plant) {
        const int row = cell / Workshop::FrontColumns, col = cell % Workshop::FrontColumns;
        const auto center = mBoard->GetCellCenterPosition(row, col);
        const auto* plant = mBoard->GetNormalPlantAt(row, col);
        const bool remove = mFrontTool == FrontTool::Remove;
        const auto color = remove ? kFrontRed : kFrontGreen;
        g->DrawRect(center.x - 38, center.y - 48, 76, 96, color);
        if (remove && plant) {
            g->DrawLine(center.x - 30, center.y - 35, center.x + 30, center.y + 35, kFrontRed);
            g->DrawLine(center.x - 30, center.y + 35, center.x + 30, center.y - 35, kFrontRed);
        }
        if (remove) {
            const float x = std::clamp(mouse.x + 16, 210.0f, 780.0f), y = std::clamp(mouse.y + 25, 90.0f, 540.0f);
            g->FillRect(x, y, 305, 31, color); FrontText("铲除植物 · 不返还阳光", x + 9, y + 7, 15, glm::vec4(255, 247, 217, 255));
        }
    }
    std::string status = mFrontTool == FrontTool::Remove ? "铲除中 · 点一株后退出，Shift 连铲 · Esc / 右键取消"
        : mFrontTool == FrontTool::Plant ? std::string("种植中：") + Workshop::FindPlant(mFrontBuildType)->name + " · 选格放置 · Esc / 右键取消"
        : "B 打开植物图册 · 点植物查看升级与能力 · 左侧按钮配置本路弹药";
    if (mFrontTool == FrontTool::Plant && cell >= 0) {
        const auto reason = mBoard->WorkshopPlantPlacementReason(mFrontBuildType, cell / Workshop::FrontColumns, cell % Workshop::FrontColumns);
        if (!reason.empty()) status = reason + " · Esc / 右键取消";
    }
    g->FillRect(216, 575, 872, 25, mFrontTool == FrontTool::Remove ? kFrontRed : glm::vec4(232, 220, 168, 245));
    g->PushClipRect(216, 575, 640, 25);
    FrontText(status, 225, 580, 13, mFrontTool == FrontTool::Remove ? glm::vec4(255, 247, 217, 255) : kFrontInk);
    g->PopClipRect();
    for (const auto& threat : Workshop::Threats) if (threat.wave > World().CompletedWaves()) {
        FrontText("第 " + std::to_string(threat.wave) + " 波：" + threat.name, 875, 580, 14, kFrontRed); break;
    }
}

/** 物料在机器、弹仓与预览中使用同一原版资源身份。 */
void WorkshopScene::DrawItemIcon(Graphics* g, Workshop::Ammo item, float x, float y, float size) {
    constexpr std::array<const char*, Workshop::ItemCount> keys{
        "IMAGE_PROJECTILEPEA", "IMAGE_PROJECTILESNOWPEA", "IMAGE_PUFFSHROOM_PUFF1",
        "IMAGE_REANIM_MELONPULT_MELON", "IMAGE_REANIM_WINTERMELON_PROJECTILE", "IMAGE_ZOMBIE_HEAD_GROSSOUT"};
    const auto* texture = ResourceManager::GetInstance().GetTexture(keys.at(static_cast<int>(item)), false);
    if (texture) g->DrawTexture(texture, x, y, size, size);
}
