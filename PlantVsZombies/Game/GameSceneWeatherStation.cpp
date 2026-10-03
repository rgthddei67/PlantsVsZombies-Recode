#include "GameScene.h"
#include "Game/Board/Board.h"
#include "Game/CardSlotManager.h"
#include "UI/Button.h"
#include "DeltaTime.h"
#include <cmath>

namespace {
constexpr float kPanelX=8, kPanelWidth=158; // 场外控制台位置与宽度，逻辑像素
constexpr float kPanelTop=398, kPanelSpacing=44; // 位于商店面板底边390之下，避免展开时压住设备按钮，逻辑像素
const char* DeviceName(int d) { return d==0 ? u8"雨势" : d==1 ? u8"迷雾" : u8"雷荷"; }
const char* SettingName(int d,int v) {
    static const char* rain[]{u8"晴夜",u8"小雨",u8"中雨",u8"大雨"};
    static const char* fog[]{u8"无雾",u8"原版迷雾",u8"小雾",u8"中雾",u8"大雾"};
    return d==0 ? rain[std::clamp(v,0,3)] : d==1 ? fog[std::clamp(v,0,4)] : v ? u8"开启" : u8"关闭";
}
}

/** 场外实时设备按钮；展开菜单不暂停，提交后自动收起，所有资格在Board复核。 */
void GameScene::CreateWeatherStationControls() {
    if (!mBoard || !mBoard->IsWeatherStation()) return;
    for (int d=0;d<3;++d) {
        auto b=mUIManager.CreateButton(Vector(kPanelX,kPanelTop+d*kPanelSpacing),Vector(kPanelWidth,42));
        b->SetImageKeys(ResourceKeys::Textures::IMAGE_BUTTONSMALL);
        b->SetTextColor(glm::vec4(53,191,61,255));
        b->SetHoverTextColor(glm::vec4(53,240,61,255));
        b->SetText(DeviceName(d),ResourceKeys::Fonts::FONT_FZCQ,13);
        b->SetClickCallBack([this,d](bool) {
            if (DeltaTime::IsPaused()) return;
            mStationOpenDevice=mStationOpenDevice==d ? -1 : d;
            mColdStorageShopOpen=false;
            if (mCardSlotManager) mCardSlotManager->DeselectCard();
        });
        mStationDeviceButtons[d]=b;
    }
    for (int v=0;v<5;++v) {
        auto b=mUIManager.CreateButton(Vector(kPanelX,125.0f+v*43.0f),Vector(kPanelWidth,40));
        b->SetImageKeys(ResourceKeys::Textures::IMAGE_BUTTONSMALL);
        b->SetTextColor(glm::vec4(53,191,61,255));
        b->SetHoverTextColor(glm::vec4(53,240,61,255));
        b->SetClickCallBack([this,v](bool) {
            if (mBoard && mBoard->TryChangeStationControl(mStationOpenDevice,v,true)) mStationOpenDevice=-1;
        });
        mStationSettingButtons[v]=b;
    }
    UpdateWeatherStationControls();
}

/** 黑障遮蔽未来提示，不通过按钮文字或冷却数字侧漏；可操作性仍如实显示。 */
void GameScene::UpdateWeatherStationControls() {
    if (!mBoard || !mBoard->IsWeatherStation()) return;
    if (mColdStorageShopOpen) mStationOpenDevice=-1;
    const bool active=mBoard->mBoardState==BoardState::GAME && !mBoard->mTrophySpawned;
    const bool hidden=mBoard->HidesStationForecasts();
    for (int d=0;d<3;++d) if (auto b=mStationDeviceButtons[d].lock()) {
        const bool unlocked=mBoard->IsStationDeviceUnlocked(d);
        const auto& c=mBoard->mWeatherStation.controls[d];
        std::string label=DeviceName(d);
        if (!unlocked) label+=u8" · 未解锁";
        else if (hidden) { label+=u8" · 信号中断"; if(c.player && c.pending>=0) label+=u8"（已下达）"; }
        // 天气没有自动结束倒计时：前摇表示将切换，保护期只表示暂时不能再次操作。
        else if (c.pending>=0) label+=" · "+std::to_string(static_cast<int>(std::ceil(c.warning)))+u8"秒后切至"+SettingName(d,c.pending);
        else {
            label+=" · "+std::string(SettingName(d,c.value));
            if(c.protection>0) label+=u8" · 锁定"+std::to_string(static_cast<int>(std::ceil(c.protection)))+u8"秒";
            else label+=u8" · 可切换";
        }
        b->SetText(label,ResourceKeys::Fonts::FONT_FZCQ,11);
        b->SetEnabled(active); b->SetSkipDraw(!active); b->SetCanClick(unlocked && !DeltaTime::IsPaused());
    }
    for (int v=0;v<5;++v) if(auto b=mStationSettingButtons[v].lock()) {
        const bool visible=active && mStationOpenDevice>=0 && v<WeatherStationRules::Count(mStationOpenDevice);
        b->SetEnabled(visible); b->SetSkipDraw(!visible);
        if (!visible) continue;
        const auto& c=mBoard->mWeatherStation.controls[mStationOpenDevice];
        std::string label=std::string(SettingName(mStationOpenDevice,v))+" · "+std::to_string(WeatherStationRules::Cost(mStationOpenDevice,v))+u8"冰";
        if(c.pending>=0 || c.protection>0) label+=u8" · 锁定";
        else if(!hidden && c.value==v) label+=u8" · 当前";
        else if(mBoard->mColdStorage.playerIce<WeatherStationRules::Cost(mStationOpenDevice,v)) label+=u8" · 不足";
        b->SetText(label,ResourceKeys::Fonts::FONT_FZCQ,11);
        b->SetCanClick(mBoard->CanChangeStationControl(mStationOpenDevice,v,true));
    }
}

/** 使用场外区域展示设备说明，正式雷击仍由既有战场层绘制。 */
void GameScene::DrawWeatherStationControls(Graphics* g) {
    if (!g || !mBoard || !mBoard->IsWeatherStation() || mBoard->mBoardState!=BoardState::GAME) return;
    const auto font=ResourceKeys::Fonts::FONT_FZCQ;
    // 冰块和补给继续使用右下角 GameProgress，左侧只容纳展开菜单及常驻设备。
    g->FillRect(4,392,166,177,glm::vec4(12,24,34,230));
    const char* notice=DeltaTime::IsPaused() ? u8"已暂停 · 空格继续操作"
        : mBoard->HidesStationForecasts() ? u8"气象信号中断" : u8"天气持续至再次切换";
    g->DrawGlyphRun(notice,font,12,glm::vec4(170,225,240,255),10,535);
    if(mBoard->IsStationDeviceUnlocked(2) && !mBoard->HidesStationForecasts())
        g->DrawGlyphRun(u8"雷荷 "+std::to_string(static_cast<int>(mBoard->GetNightRoofCharge()))+" / 100",font,14,glm::vec4(210,185,255,255),10,553);
    if(mStationOpenDevice>=0) {
        g->FillRect(4,98,166,250,glm::vec4(12,24,34,240));
        g->DrawGlyphRun(u8"选择后付款，8秒后生效",font,12,glm::vec4(190,235,245,255),10,103);
    }
}
