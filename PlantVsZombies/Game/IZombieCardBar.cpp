#include "IZombieCardBar.h"
#include "IZombieRules.h"
#include "CardPresentation.h"
#include "Board/Board.h"
#include "Zombie/Zombie.h"
#include "Plant/GameDataManager.h"
#include "GameObjectManager.h"
#include "GameApp.h"
#include "Graphics.h"
#include "ResourceKeys.h"
#include "ResourceManager.h"
#include "DeltaTime.h"
#include "UI/InputHandler.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr int kPageSize = 11; // 顶部一页僵尸卡数，分页支持完整注册表
constexpr float kCardX = 200; // 卡槽起点，px，保留左侧阳光计数区
constexpr float kCardY = 2; // 卡槽顶部，px
constexpr float kCardWidth = 50; // 卡片宽度，px
constexpr float kCardHeight = 70; // 卡片高度，px
constexpr float kCardPitch = 51; // 相邻卡片间距，px
constexpr float kPageX = 800; // 分页控制起点，px
constexpr float kPlacementOpacity = 0.35f; // 与植物卡落点预览一致，完整角色合成后的透明度

bool Inside(Vector point, float x, float y, float width, float height)
{
    return point.x >= x && point.x < x+width && point.y >= y && point.y < y+height;
}

/** 预览不绑定棋盘、不参与碰撞或自动更新；绘制时直接复用已初始化的 Animator。 */
std::shared_ptr<Zombie> MakePreview(ZombieType type)
{
    auto preview = GameAPP::GetInstance().InstantiateZombieFree(type, nullptr, 0, 0);
    preview->mIsUI = true;
    preview->PauseAnimation();
    preview->RemoveShadow();
    preview->RemoveCollider();
    preview->SetActive(false);
    return preview;
}
void DestroyPreview(std::shared_ptr<Zombie>& preview)
{
    if (preview) GameObjectManager::GetInstance().DestroyGameObject(preview);
    preview.reset();
}
}

IZombieCardBar::IZombieCardBar(Board* board) : mBoard(board)
{
    Refresh();
    // 与植物投放共用 Cell 的点击仲裁，阳光等高层对象会先消费按下和释放。
    for (int row=0;row<mBoard->mRows;++row) for (int col=0;col<mBoard->mColumns;++col)
        if (auto* cell=mBoard->GetCell(row,col))
            cell->SetClickCallback([this](int,int) { DeployAtHoveredCell(); });
}
IZombieCardBar::~IZombieCardBar()
{
    for (int row=0;row<mBoard->mRows;++row) for (int col=0;col<mBoard->mColumns;++col)
        if (auto* cell=mBoard->GetCell(row,col)) cell->SetClickCallback({});
    ClearPortraits();
    Cancel();
}
void IZombieCardBar::ClearPortraits()
{
    for (auto& portrait : mPortraits) DestroyPreview(portrait);
    mPortraits.clear();
    mCostLabels.clear();
}
void IZombieCardBar::Cancel()
{
    mSelectedSlot=-1;
    DestroyPreview(mPlacement);
    // 暂停期间提示计时也冻结；新一次选择或取消不能继续展示上次失败原因。
    mHint.clear();
    mHintRemaining=0;
}
void IZombieCardBar::Select(int slot)
{
    Cancel();
    mSelectedSlot=slot;
    mPlacement=MakePreview(mTypes[slot]);
}
void IZombieCardBar::Refresh()
{
    Cancel();
    ClearPortraits();
    mTypes=mBoard->GetIZombieState()->cards;
    mPage=std::clamp(mPage,0,std::max(0,(static_cast<int>(mTypes.size())-1)/kPageSize));
    const int end=std::min(static_cast<int>(mTypes.size()),(mPage+1)*kPageSize);
    for (int slot=mPage*kPageSize;slot<end;++slot) mPortraits.push_back(MakePreview(mTypes[slot]));
    mCostLabels.resize(mPortraits.size());
    UpdateCostLabels();
}

void IZombieCardBar::UpdateCostLabels()
{
    auto& graphics = GameAPP::GetInstance().GetGraphics();
    for (size_t index = 0; index < mCostLabels.size(); ++index) {
        auto& label = mCostLabels[index];
        if (!graphics.IsCachedTextStale(label) && label.BindingId() != 0) continue;
        label = CardPresentation::AcquireCost(graphics,IZombie::Card(mTypes[mPage*kPageSize+index]).cost);
    }
}

std::pair<int,int> IZombieCardBar::HoveredCell() const
{
    const auto point=GameAPP::GetInstance().GetInputHandler().GetMouseWorldPosition();
    for (int row=0;row<mBoard->mRows;++row) for(int col=0;col<mBoard->mColumns;++col) {
        const auto center=mBoard->GetCellCenterPosition(row,col);
        if (Inside(point,center.x-CELL_COLLIDER_SIZE_X*.5f,center.y-mBoard->GetCellHeight()*.5f,
            CELL_COLLIDER_SIZE_X,mBoard->GetCellHeight())) return {row,col};
    }
    return {-1,-1};
}

void IZombieCardBar::Update()
{
    if (mTypes!=mBoard->GetIZombieState()->cards) Refresh();
    UpdateCostLabels();
    mHintRemaining=std::max(0.0f,mHintRemaining-DeltaTime::GetUnscaledDeltaTime());
    mHoveredSlot=-1;
    if (mBoard->GetState()!=BoardState::GAME || mBoard->mTrophySpawned
        || (DeltaTime::IsPaused() && !GameAPP::GetInstance().mAdvancedPauseEnabled)) return;
    auto& input=GameAPP::GetInstance().GetInputHandler();
    const auto point=input.GetMousePosition();
    for(int index=0;index<static_cast<int>(mPortraits.size());++index)
        if(Inside(point,kCardX+index*kCardPitch,kCardY,kCardWidth,kCardHeight)) mHoveredSlot=mPage*kPageSize+index;
    if(input.IsMouseButtonPressed(SDL_BUTTON_RIGHT)) { Cancel(); return; }
    if(!input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) return;
    if(mTypes.size()>kPageSize && Inside(point,kPageX,4,112,40)) {
        const int pages=(static_cast<int>(mTypes.size())+kPageSize-1)/kPageSize;
        mPage=(mPage+(point.x<kPageX+56 ? pages-1 : 1))%pages;
        Refresh();
        return;
    }
    if(mHoveredSlot>=0) {
        const auto& card=IZombie::Card(mTypes[mHoveredSlot]);
        if(mBoard->GetSun()<card.cost) { mHint="阳光不足"; mHintRemaining=2; CardPresentation::PlayClick(false); }
        else if(mBoard->GetIZombieState()->cooldowns[mHoveredSlot]>0) { mHint="这张卡还在冷却"; mHintRemaining=2; CardPresentation::PlayClick(false); }
        else {
            if(mSelectedSlot==mHoveredSlot) Cancel();
            else Select(mHoveredSlot);
            CardPresentation::PlayClick(true);
        }
        return;
    }
    if(mSelectedSlot>=0 && HoveredCell().first<0) Cancel();
}

void IZombieCardBar::DeployAtHoveredCell()
{
    if(mSelectedSlot<0 || mBoard->GetState()!=BoardState::GAME || mBoard->mTrophySpawned
        || (DeltaTime::IsPaused() && !GameAPP::GetInstance().mAdvancedPauseEnabled)) return;
    // 共边 Cell 都包含边缘，释放时仍用预览的唯一格位解析，避免落点错格。
    const auto [row,column]=HoveredCell();
    if(row<0) return;
    mHint=mBoard->TryDeployIZombie(mSelectedSlot,row,column);
    if(mHint.empty()) Cancel();
    else { mHintRemaining=2.5f; CardPresentation::PlayClick(false); }
}

void IZombieCardBar::Draw(Graphics* graphics)
{
    auto& app=GameAPP::GetInstance();
    const auto* frame=ResourceManager::GetInstance().GetTexture(ResourceKeys::Textures::IMAGE_SEEDPACKETNORMAL);
    const auto* state=mBoard->GetIZombieState();
    for(int index=0;index<static_cast<int>(mPortraits.size());++index) {
        const int slot=mPage*kPageSize+index;
        const auto& card=IZombie::Card(mTypes[slot]);
        const float x=kCardX+index*kCardPitch;
        const auto visualState = slot==mSelectedSlot ? CardPresentation::State::Click
            : state->cooldowns[slot]>0 ? CardPresentation::State::Cooling
            : mBoard->GetSun()<card.cost ? CardPresentation::State::WaitingSun : CardPresentation::State::Ready;
        const auto color = CardPresentation::Tint(visualState);
        CardPresentation::DrawNormalFrame(graphics,frame,x,kCardY,color);
        auto animator=mPortraits[index]->GetAnimatorInternal();
        animator->SetDrawTint({static_cast<Uint8>(color.r),static_cast<Uint8>(color.g),static_cast<Uint8>(color.b),255});
        graphics->PushClipRect(static_cast<int>(x+3),7,static_cast<int>(kCardWidth-6),44);
        // 统一全身取景；大型车辆/巨人按其既有碰撞体尺寸缩放，无需另画卡图。
        const auto offset=GameDataManager::GetInstance().GetZombieOffset(card.type);
        float scale=.38f;
        if(card.type==ZombieType::ZOMBIE_GARGANTUAR || card.type==ZombieType::ZOMBIE_REDEYE_GARGANTUAR) scale=.20f;
        else if(card.type==ZombieType::ZOMBIE_ENGINEERING_SHIP) scale=.22f;
        else if(card.type==ZombieType::ZOMBIE_ZAMBONI || card.type==ZombieType::ZOMBIE_GILDED_ZAMBONI
            || card.type==ZombieType::ZOMBIE_CATAPULT || card.type==ZombieType::ZOMBIE_ELITE_CATAPULT) scale=.27f;
        animator->Draw(graphics,x+kCardWidth*.5f+offset.x*scale,48+offset.y*scale,scale);
        graphics->PopClipRect();
        if(state->cooldowns[slot]>0) {
            const float fraction=std::clamp(state->cooldowns[slot]/std::max(card.cooldown,state->cooldowns[slot]),0.0f,1.0f);
            CardPresentation::DrawCooldown(graphics,x,kCardY,fraction);
        }
        CardPresentation::DrawCost(graphics,mCostLabels[index],x,kCardY);
        if(slot==mSelectedSlot) CardPresentation::DrawSelection(graphics,x,kCardY);
    }
    if(mTypes.size()>kPageSize) {
        app.DrawText("<   >",{kPageX+8,8},{230,240,205,255},ResourceKeys::Fonts::FONT_FZJZ,25);
        app.DrawText(std::to_string(mPage+1)+"/"+std::to_string((mTypes.size()+kPageSize-1)/kPageSize),
            {kPageX+28,42},{235,235,205,255},ResourceKeys::Fonts::FONT_FZJZ,17);
    }
    std::string label="吃掉全部脑子  "+std::to_string(mBoard->mRows-mBoard->GetIZombieBrainsRemaining())+"/"+std::to_string(mBoard->mRows);
    if(mHintRemaining>0) label=mHint;
    app.DrawText(label,{260,568},{255,235,180,255},ResourceKeys::Fonts::FONT_FZJZ,20);
}

/** 与植物卡一样分开绘制格心幽灵和鼠标手持物；共用冻结姿态，不创建战斗实体。 */
void IZombieCardBar::DrawPlacement(Graphics* graphics)
{
    if(!mPlacement || mBoard->GetState()!=BoardState::GAME || mBoard->mTrophySpawned
        || (DeltaTime::IsPaused() && !GameAPP::GetInstance().mAdvancedPauseEnabled)) return;
    const auto [row,column]=HoveredCell();
    const auto mouse=GameAPP::GetInstance().GetInputHandler().GetMouseWorldPosition();
    auto animator=mPlacement->GetAnimatorInternal();
    const auto drawPreview = [&](Vector position, float opacity) {
        mPlacement->SetPosition(position);
        animator->SetCompositeOpacity(opacity);
        const auto visual=mPlacement->GetVisualPosition();
        animator->Draw(graphics,visual.x,visual.y,mPlacement->GetTransform()->GetScale());
    };
    const bool valid=row>=0 && mBoard->CanDeployIZombie(mSelectedSlot,row,column).empty();
    if(valid) {
        auto position=mBoard->GetCellCenterPosition(row,column);
        position.y=mBoard->GetZombieSpawnY(row,position.x);
        drawPreview(position,kPlacementOpacity);
    }
    drawPreview(mouse,1.0f);
}
