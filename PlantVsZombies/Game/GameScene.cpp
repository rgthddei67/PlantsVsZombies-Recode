#include "GameScene.h"
#include "CrazyDaveDialog.h"
#include "CursorObjectManager.h"
#include "SceneManager.h"
#include "PlantAlmanacScene.h"
#include "../ResourceManager.h"
#include "./CardSlotManager.h"
#include "../ResourceKeys.h"
#include "../DeltaTime.h"
#include "./AudioSystem.h"
#include "../GameInfoSaver.h"
#include "GameProgress.h"
#include "ChooseCardUI.h"
#include "../UI/GameMessageBox.h"
#include "Perk/SurvivalPerkManager.h"
#include "../GameApp.h"
#include "../Graphics.h"
#include "../Profiler.h"
#include "./Shovel.h"
#include "ShovelBank.h"
#include "Plant/GameDataManager.h"
#include "Zombie/RoofMarshalZombie.h"
#include "../Logger.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <array>
#include <iterator>

namespace {
	constexpr float kCompactDialogScale = 1.2f; // 重开、退出和失败弹窗整体缩为原 1.5 倍配置的 80%，含文字与按钮
	constexpr float kSeedBankX = 130.0f;                  // 卡槽底板左边缘，单位：逻辑 px
	constexpr float kSeedBankRestY = -10.0f;              // 卡槽停靠后的顶部 Y，单位：逻辑 px
	constexpr float kSeedBankStartY = -100.0f;            // 卡槽滑入前的顶部 Y，单位：逻辑 px
	constexpr float kSeedBankDefaultScaleX = 0.85f;       // 资源不可用时沿用的横向缩放兜底
	constexpr float kSeedBankScaleY = 0.9f;               // 卡槽底板纵向缩放，保持现有高度
	constexpr float kSeedBankRightPadding = 13.0f;        // 最后一张卡右侧到含圆角边框的总留白，单位：逻辑 px
	constexpr float kSunPaperLeftInTexture = 7.0f;        // 阳光纸条文字区左边缘，单位：资源 px
	constexpr float kSunPaperRightInTexture = 69.0f;      // 阳光纸条文字区右边缘，单位：资源 px
	constexpr float kSunPaperTopInTexture = 55.0f;        // 阳光纸条文字区上边缘，单位：资源 px
	constexpr float kSunPaperBottomInTexture = 86.0f;     // 阳光纸条文字区下边缘，单位：资源 px
	constexpr float kSunTextInsetX = 2.0f;                // 数字相对纸条左右边缘的安全内缩，单位：逻辑 px
	constexpr float kSunTextInsetY = 2.0f;                // 数字相对纸条上下边缘的安全内缩，单位：逻辑 px
	constexpr int kSunTextMaxFontSize = 17;               // 短阳光数值允许使用的最大字号
	constexpr int kSunTextMinFontSize = 11;               // 极端长数值仍需保持可读的最小字号

	/** 按 11 个真实卡位的右边缘计算底板横向缩放，消除末端看似可放卡的空段。 */
	float SeedBankScaleX()
	{
		const Texture* texture = ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_SEEDBANK_LONG, false);
		if (!texture || texture->width <= 0) return kSeedBankDefaultScaleX;
		const float targetRight = ChooseCardUI::GetGameSlotRightEdge()
			+ kSeedBankRightPadding;
		return (targetRight - kSeedBankX) / static_cast<float>(texture->width);
	}

	// 右下角关卡名/轮数显示。
	// 冒险模式：沿用原左对齐（左端锚点 x=768，阴影 766），不改动既有观感。
	// 生存/小游戏长名称向左延伸，右边界留在从 870px 开始的波次进度条之前。
	constexpr float kLevelNameRightAnchor = 865.0f; // 长关卡名称右边界，单位：逻辑像素
	constexpr float kWeatherPanelWidth = 350.0f;          // 天气面板宽度；为最长风向与阵风中文实况预留安全边距（逻辑像素）
	constexpr float kWeatherPanelHeight = 72.0f;          // 天气面板高度（逻辑像素）
	constexpr float kWeatherPanelVisibleX = 12.0f;        // 完全滑入后的左边距（逻辑像素）
	constexpr float kWeatherPanelY = 76.0f;               // 面板顶部位置，避开上方种子槽（逻辑像素）
	constexpr float kWeatherPanelSlideDuration = 0.32f;   // 完整滑入或滑出的动画时长（秒，未缩放）
	constexpr float kCurrentWeatherNoticeDuration = 5.0f; // 天气揭晓后继续展示“当前天气”的时长（秒，未缩放）
	constexpr float kWeatherPanelShadowAlpha = 64.0f;     // 天气面板错位阴影 alpha，保留层次且避免叠加后底板过实
	constexpr float kWeatherPanelBackgroundAlpha = 176.0f; // 天气面板主底色 alpha，让战场仍可透出但保证文字对比度
	constexpr int kWeatherCurrentFontSize = 18;           // 第一行“当前天气”字号
	constexpr int kWeatherForecastFontSize = 16;          // 第二行“天气预警”字号
	constexpr int kWeatherWindFontSize = 15;              // 台风期间第三行“风向实况”字号
	constexpr int kPlanternGearMenuRenderOrder = LAYER_UI + 700; // 路灯花菜单盖过天气板/失败提示且低于全屏提示
	constexpr float kWeatherPanelDetailLineHeight = 30.0f; // 雾势预报或风向实况每增加一行的面板高度
	constexpr float kWeatherPanelGaugeLineHeight = 38.0f; // 累计条文字与 8px 进度槽合计占用的面板高度
	constexpr float kWeatherGaugeWidth = 314.0f;          // 天气累计条可用宽度，左右与正文对齐
	constexpr float kWeatherGaugeHeight = 8.0f;           // 天气累计条槽高，避免长期遮挡过多战场
	constexpr float kWinterThermometerFrameX = 0.0f;      // 独立温度计贴图左缘，单位：逻辑像素
	constexpr float kWinterThermometerFrameY = 154.0f;    // 温度计位于天气面板下方且不侵入种子槽，单位：逻辑像素
	constexpr float kWinterThermometerFrameWidth = 100.0f; // 温度计贴图目标宽度，按低分辨率 HUD 可读性确定
	constexpr float kWinterThermometerFrameHeight = 223.0f; // 温度计贴图目标高度，保持原始资源纵横比
	constexpr float kWinterThermometerTubeCenterX = 50.0f; // 透明管腔中心的屏幕 X，单位：逻辑像素
	constexpr float kWinterThermometerTubeTopY = 181.0f;   // +6°C 液柱顶点的屏幕 Y，单位：逻辑像素
	constexpr float kWinterThermometerTubeBottomY = 318.0f; // -12°C 液柱颈部的屏幕 Y，单位：逻辑像素
	constexpr float kWinterThermometerBulbCenterY = 348.0f; // 透明球腔中心的屏幕 Y，单位：逻辑像素
	constexpr float kWinterThermometerLabelX = 90.0f;      // 三条阈值文字的左缘，单位：逻辑像素
	constexpr int kWinterThermometerScaleFontSize = 13;    // 上限、冰点与下限刻度字号
	constexpr int kWinterThermometerCurrentFontSize = 15;  // 温度计下方当前温度字号
	constexpr float kRoofRunoffSlopeStartX = CELL_INITALIZE_POS_X; // 径流世界特效从房屋侧第一列边缘开始
	constexpr float kRoofRunoffSheetSliceWidth = 4.0f;    // 水膜按窄竖片贴合连续坡面，单位像素；片间重叠避免出现轮廓线
	constexpr float kRoofRunoffDropletTravelPerFlow = 240.0f; // 单次冲刷期间零散水珠朝屋檐行进的视觉距离，单位像素
	constexpr int kNightRoofChargeRouteSegments = 18;      // 导电瓦路放电折线的分段数，越高越贴合连续坡面
	constexpr float kNightRoofChargeRouteInsetX = 18.0f;   // 导电瓦路相对左右屋顶网格边缘的视觉内缩，单位像素
	constexpr float kStormyNightColorR = 224.0f;          // “暴风雨”预报与当前天气的紫红强调色 R
	constexpr float kStormyNightColorG = 70.0f;           // “暴风雨”预报与当前天气的紫红强调色 G
	constexpr float kStormyNightColorB = 158.0f;          // “暴风雨”预报与当前天气的紫红强调色 B
	constexpr float kForecastFailureWidth = 350.0f;       // 预报失败提示与加宽后的天气面板对齐（逻辑像素）
	constexpr float kForecastFailureHeight = 58.0f;       // 预报失败提示高度（逻辑像素）
	constexpr float kForecastFailureY = 154.0f;           // 失败提示顶部位置，显示在天气面板下方（逻辑像素）
	constexpr float kForecastFailureDuration = 3.2f;      // 失败提示从出现到完全消失的总时长（秒，未缩放）
	constexpr float kForecastFailureAppearDuration = 0.25f; // 失败提示滑入动画时长（秒，未缩放）
	constexpr float kForecastFailureFadeDuration = 0.45f; // 失败提示末尾淡出时长（秒，未缩放）
	constexpr int kForecastFailureTitleFontSize = 17;     // “天气预报失败”标题字号
	constexpr int kForecastFailureDetailFontSize = 15;    // 预报与实际天气对照行字号
	constexpr float kPromptBandHeight = 96.0f;            // 紧急文字提示的暗色横幅高度（逻辑像素）
	constexpr float kPromptBandHorizontalInset = 58.0f;   // 横幅左右留白，避免贴住画面边缘（逻辑像素）
	constexpr float kPromptTextHoldPulse = 0.015f;        // 文字停留阶段的呼吸缩放幅度
	constexpr float kPromptTextHoldPulseSpeed = 12.0f;    // 文字停留阶段的呼吸频率（弧度/游戏秒）
	constexpr float kSpacePauseLabelY = 72.0f;            // 轻量暂停文字顶部，避开种子槽与右上角按钮
	constexpr int kSpacePauseLabelFontSize = 32;          // 轻量暂停文字字号，保持醒目但不遮挡战场
	constexpr float kHeavyRainPromptAppearDuration = 0.28f; // 大雨警报压入画面的时长（游戏秒）
	constexpr float kHeavyRainPromptHoldDuration = 3.85f; // 大雨警报完全可读的停留时长（游戏秒）
	constexpr float kHeavyRainPromptFadeDuration = 0.55f; // 大雨警报放大淡出的时长（游戏秒）
	constexpr int kHeavyRainPromptFontSize = 42;          // 大雨分级警报字号
	constexpr float kPlanternLowFuelPromptAppearDuration = 0.18f; // 低燃料警报压入画面的未缩放秒数
	constexpr float kPlanternLowFuelPromptHoldDuration = 2.35f; // 低燃料警报完全可读的未缩放秒数
	constexpr float kPlanternLowFuelPromptFadeDuration = 0.47f; // 低燃料警报放大淡出的未缩放秒数，总计约 3 秒
	constexpr int kPlanternLowFuelPromptFontSize = 46;    // 低燃料警报字号，刻意大于天气警报
	constexpr float kRoofMarshalPromptAppearDuration = 0.22f; // 突击令中央警报压入画面的游戏秒数
	constexpr float kRoofMarshalPromptHoldDuration = 2.10f; // 突击令文案完整可读的游戏秒数
	constexpr float kRoofMarshalPromptFadeDuration = 0.48f; // 突击令中央警报放大淡出的游戏秒数
	constexpr int kRoofMarshalPromptFontSize = 44;         // 突击令中央警报字号
	constexpr float kRoofMarshalBossBarWidth = 560.0f;     // 首领血槽宽度，收窄后少遮挡第五路
	constexpr float kRoofMarshalBossBarHeight = 18.0f;     // 首领血槽高度，单位：逻辑像素
	constexpr float kRoofMarshalBossBarY = 556.0f;         // 血槽顶部；让底板覆盖关卡文字而非第五路作战区
	constexpr float kRoofMarshalBossPlatePaddingX = 16.0f; // 金属底板相对血槽的左右扩展，单位：逻辑像素
	constexpr float kRoofMarshalBossPlateTop = 27.0f;      // 金属底板高出血槽的距离，容纳紧凑首领名
	constexpr float kRoofMarshalBossPlateBottom = 14.0f;   // 金属底板低于血槽的距离，容纳黑金阶段铭牌
	constexpr int kRoofMarshalBossTitleFontSize = 20;      // “屋脊督军”标题字号
	constexpr int kRoofMarshalBossHealthFontSize = 14;     // 血槽内实际生命数字号
	constexpr int kRoofMarshalBossPhaseFontSize = 12;      // 8000/4000 技能分界标签字号
	constexpr float kRoofMarshalBossDesperatePulseSpeed = 0.16f; // 狂暴阶段血色脉冲速度，单位：弧度/逻辑帧
	constexpr float kPoolEffectOffsetX = 209.0f;          // 原版水面坐标对齐当前 1880px 泳池背景的世界 X 偏移（像素）
	constexpr float kPoolEffectOffsetY = 12.0f;           // 原版水面坐标对齐当前泳池内框的世界 Y 偏移（像素）
	constexpr float kRoofRainBackgroundAlphaScale = 2.125f; // 通用大雨暗幕 120 映射为雨景背景完全显现 255 的倍率
	constexpr int kLightningMainSegments = 10;           // 主闪电从云层到落点的折线段数
	constexpr int kLightningBranchCount = 3;             // 主干上生成的二段式分叉数量
	constexpr float kFogTileDrawWidth = 210.0f;           // 按原生 210px 宽绘制，使相邻 80px 雾格充分重叠
	constexpr float kFogTileDrawHeight = 190.0f;          // 按原生 190px 高绘制，使相邻 85px 雾行形成连续雾幕
	constexpr float kFogTailTileOffsetX = 120.0f;         // 最右雾格额外收边贴图的水平偏移，覆盖 1100px 场景右缘
	constexpr float kFogOcclusionOffsetX = 19.0f;         // 深雾底层相对主层错位 X，用另一纹理填补透明洞
	constexpr float kFogOcclusionOffsetY = -12.0f;        // 深雾底层相对主层错位 Y，避免重复轮廓形成规则网格
	constexpr float kFogOcclusionAlphaFactor = 0.90f;     // 深雾底层的逐格 alpha 系数；提高遮蔽但保留云纹
	struct FogLayerSpec {
		float offsetX;
		float offsetY;
		float alphaFactor;
	};
	constexpr std::array<FogLayerSpec, 3> kFogLayers = {{
		{ 0.0f, 0.0f, 1.0f },                            // 主雾层：严格对齐原版格位
		{ 37.0f, 26.0f, 0.72f },                         // 小雾/普通迷雾补层：错开主层透明洞
		{ -31.0f, -23.0f, 0.58f },                       // 大雾补层：继续填补前两层剩余缝隙
	}};

	/** 解析进入关卡的背景；AutoTest 可显式覆盖，以继续验证尚未接入冒险流程的地图。 */
	Background ResolveEnterBackground(int level)
	{
		const Background configured = GameAPP::GetInstance().GetBackgroundID(level);
		if (!GameAPP::mAutoTestMode) return configured;

		const std::string overrideName =
			SceneManager::GetInstance().GetGlobalData("AutoTestBackground");
		if (overrideName == "GROUND_DAY") return Background::GROUND_DAY;
		if (overrideName == "GROUND_NIGHT") return Background::GROUND_NIGHT;
		if (overrideName == "WATER_POOL") return Background::WATER_POOL;
		if (overrideName == "NIGHT_WATER_POOL") return Background::NIGHT_WATER_POOL;
		if (overrideName == "ROOF") return Background::ROOF;
		if (overrideName == "NIGHT_ROOF") return Background::NIGHT_ROOF;
		if (overrideName == "WINTER_GARDEN") return Background::WINTER_GARDEN;
		if (overrideName == "GLOOMCRYSTAL_MINE") return Background::GLOOMCRYSTAL_MINE;
	if (overrideName == "POLAR_NIGHT_SNOWFIELD") return Background::POLAR_NIGHT_SNOWFIELD;
		return configured;
	}

	// 用平行线模拟可调粗细，避免为只持续数帧的闪电引入独立纹理或 shader。
	void DrawLightningSegment(Graphics* g, const glm::vec2& from, const glm::vec2& to,
		float glowWidth, float glowAlpha, float coreAlpha)
	{
		const glm::vec2 delta = to - from;
		const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
		if (!g || length <= 0.001f) return;
		const glm::vec2 normal(-delta.y / length, delta.x / length);
		const int glowRadius = std::max(1, static_cast<int>(std::lround(glowWidth)));

		for (int offset = -glowRadius; offset <= glowRadius; ++offset) {
			const float distance = std::abs(static_cast<float>(offset));
			const float falloff = 1.0f - distance / static_cast<float>(glowRadius + 1);
			const glm::vec2 shift = normal * static_cast<float>(offset);
			g->DrawLine(from.x + shift.x, from.y + shift.y,
				to.x + shift.x, to.y + shift.y,
				glm::vec4(118.0f, 154.0f, 255.0f, glowAlpha * falloff));
		}

		for (int offset = -1; offset <= 1; ++offset) {
			const glm::vec2 shift = normal * static_cast<float>(offset);
			g->DrawLine(from.x + shift.x, from.y + shift.y,
				to.x + shift.x, to.y + shift.y,
				glm::vec4(206.0f, 225.0f, 255.0f, coreAlpha));
		}
		g->DrawLine(from.x, from.y, to.x, to.y,
			glm::vec4(255.0f, 255.0f, 255.0f, std::min(255.0f, coreAlpha * 1.35f)));
	}

#define DEVZ(n) { ZombieType::n, #n }
	// 开发者面板循环切换表；显示枚举标识符（简陋版足够）。与 TestDriver kZombieNames 同集合。
	const std::vector<std::pair<ZombieType, const char*>> kDevZombieTable = {
		DEVZ(ZOMBIE_NORMAL), DEVZ(ZOMBIE_TRAFFIC_CONE), DEVZ(ZOMBIE_POLEVAULTER), DEVZ(ZOMBIE_ELITE_POLEVAULTER), DEVZ(ZOMBIE_BUCKET),
		DEVZ(ZOMBIE_FASTBUCKET), DEVZ(ZOMBIE_NEWSPAPER), DEVZ(ZOMBIE_FASTPAPER), DEVZ(ZOMBIE_DOOR),
		DEVZ(ZOMBIE_FOOTBALL), DEVZ(ZOMBIE_DANCER), DEVZ(ZOMBIE_BACKUP_DANCER), DEVZ(ZOMBIE_ELITE_DANCER), DEVZ(ZOMBIE_PINK_FOOTBALL),
		DEVZ(ZOMBIE_REINFORCED_DOOR),
		DEVZ(ZOMBIE_POOL_NORMAL), DEVZ(ZOMBIE_POOL_CONE), DEVZ(ZOMBIE_POOL_BUCKET),
		DEVZ(ZOMBIE_ZAMBONI), DEVZ(ZOMBIE_GILDED_ZAMBONI), DEVZ(ZOMBIE_DOLPHIN_RIDER), DEVZ(ZOMBIE_ELITE_DOLPHIN_RIDER),
		DEVZ(ZOMBIE_JACK_IN_THE_BOX), DEVZ(ZOMBIE_ELITE_JACK_IN_THE_BOX), DEVZ(ZOMBIE_BALLOON),
		DEVZ(ZOMBIE_DIGGER), DEVZ(ZOMBIE_ELITE_DIGGER), DEVZ(ZOMBIE_POGO), DEVZ(ZOMBIE_ELITE_POGO),
		DEVZ(ZOMBIE_BUNGEE), DEVZ(ZOMBIE_LADDER), DEVZ(ZOMBIE_ELITE_LADDER), DEVZ(ZOMBIE_CATAPULT), DEVZ(ZOMBIE_ELITE_CATAPULT),
		DEVZ(ZOMBIE_YETI),
		DEVZ(ZOMBIE_GARGANTUAR), DEVZ(ZOMBIE_IMP), DEVZ(ZOMBIE_BOSS), DEVZ(ZOMBIE_PEA_HEAD),
		DEVZ(ZOMBIE_WALLNUT_HEAD), DEVZ(ZOMBIE_JALAPENO_HEAD), DEVZ(ZOMBIE_GATLING_HEAD),
		DEVZ(ZOMBIE_SQUASH_HEAD), DEVZ(ZOMBIE_TALLNUT_HEAD), DEVZ(ZOMBIE_REDEYE_GARGANTUAR), DEVZ(ZOMBIE_ROOF_MARSHAL),
		DEVZ(ZOMBIE_INSULATOR), DEVZ(ZOMBIE_HIJACKER), DEVZ(ZOMBIE_HEALER),
		DEVZ(ZOMBIE_GROUNDING), DEVZ(ZOMBIE_BOBSLED_TEAM), DEVZ(ZOMBIE_ICE_WALL_ENGINEER),
		DEVZ(ZOMBIE_ICE_CRACK_DRILL), DEVZ(ZOMBIE_WEATHER_JAMMER),
		DEVZ(ZOMBIE_ICE_STATUE_EXECUTIONER),
		DEVZ(ZOMBIE_SNOW_BURROW),
		DEVZ(ZOMBIE_ADAPTIVE_HELMET),
		DEVZ(ZOMBIE_THERMAL_SNIPER),
		DEVZ(ZOMBIE_AURORA_PRIEST), DEVZ(ZOMBIE_POLAR_CLOCKMAKER), DEVZ(ZOMBIE_EXCAVATOR)
	};
#undef DEVZ

	void DrawLevelName(GameAPP& gameApp, const std::string& name, bool rightAligned) {
		if (rightAligned) {
			float drawX = kLevelNameRightAnchor;   // 兜底：取不到字体时退化为右端起点
			if (TTF_Font* font = ResourceManager::GetInstance().GetFont(
				ResourceKeys::Fonts::FONT_FZCQ, 21)) {
				int tw = 0, th = 0;
				TTF_SizeUTF8(font, name.c_str(), &tw, &th);
				drawX = kLevelNameRightAnchor - static_cast<float>(tw);
			}
			// 阴影相对主体偏移沿用原 (766,575)/(768,576)：左上 2px / 下 1px
			gameApp.DrawText(name, Vector(drawX - 2.0f, 575.0f), { 0,0,0,255 },
				ResourceKeys::Fonts::FONT_FZCQ, 21);
			gameApp.DrawText(name, Vector(drawX, 576.0f), { 223,186,98,255 },
				ResourceKeys::Fonts::FONT_FZCQ, 21);
		}
		else {
			gameApp.DrawText(name, Vector(766, 575), { 0,0,0,255 },
				ResourceKeys::Fonts::FONT_FZCQ, 21);
			gameApp.DrawText(name, Vector(768, 576), { 223,186,98,255 },
				ResourceKeys::Fonts::FONT_FZCQ, 21);
		}
	}

	/** 把内部雨势枚举转换为面板使用的简短中文名称。 */
	const char* RainIntensityDisplayName(RainIntensity intensity) {
		switch (intensity) {
		case RainIntensity::CLEAR:  return u8"晴天";
		case RainIntensity::LIGHT:  return u8"小雨";
		case RainIntensity::MEDIUM: return u8"中雨";
		case RainIntensity::HEAVY:  return u8"大雨";
		}
		return u8"未知";
	}

	/** 低温冬日花园沿用同一降水强度，但把玩家可见名称改为雪。 */
	const char* PrecipitationDisplayName(const Board* board, RainIntensity intensity) {
		const bool snow = board && board->SupportsWinterTemperature()
			&& board->GetAmbientTemperatureC()
				<= board->GetWinterFreezingTemperatureC()
			&& intensity != RainIntensity::CLEAR;
		if (!snow) return RainIntensityDisplayName(intensity);
		switch (intensity) {
		case RainIntensity::LIGHT:  return u8"小雪";
		case RainIntensity::MEDIUM: return u8"中雪";
		case RainIntensity::HEAVY:  return u8"大雪";
		case RainIntensity::CLEAR:  return u8"晴天";
		}
		return u8"未知";
	}

	const char* ColdWaveStrengthDisplayName(ColdWaveStrength strength) {
		switch (strength) {
		case ColdWaveStrength::WEAK:   return u8"弱寒潮";
		case ColdWaveStrength::NORMAL: return u8"普通寒潮";
		case ColdWaveStrength::STRONG: return u8"强寒潮";
		}
		return u8"寒潮";
	}

	/** 把独立雾势转换为天气面板名称。 */
	const char* FogWeatherDisplayName(FogWeatherIntensity intensity) {
		switch (intensity) {
		case FogWeatherIntensity::DEFAULT: return u8"原版迷雾";
		case FogWeatherIntensity::SMALL:  return u8"小雾";
		case FogWeatherIntensity::NORMAL: return u8"普通迷雾";
		case FogWeatherIntensity::DENSE:  return u8"大雾";
		}
		return u8"未知雾势";
	}

	/** 把台风强度转换为当前天气行使用的正式等级名称。 */
	const char* TyphoonStrengthDisplayName(TyphoonStrength strength) {
		switch (strength) {
		case TyphoonStrength::NONE:     return u8"";
		case TyphoonStrength::TYPHOON:  return u8"台风";
		case TyphoonStrength::SEVERE:   return u8"强台风";
		case TyphoonStrength::SUPER:    return u8"超强台风";
		}
		return u8"未知台风";
	}

	/** 失败提示使用与天气面板相同的台风后缀，避免大雨续期失准时只显示“大雨 → 大雨”。 */
	std::string ForecastOutcomeDisplayName(
		RainIntensity rain, TyphoonStrength typhoon) {
		std::string name = RainIntensityDisplayName(rain);
		if (rain == RainIntensity::HEAVY && typhoon != TyphoonStrength::NONE) {
			name += std::string(u8"·") + TyphoonStrengthDisplayName(typhoon);
		}
		return name;
	}

	/** 只从公开预报及其已锁定警报等级构建玩家可见文案，隐藏的真实天气不得参与。 */
	std::string WeatherForecastPanelText(const Board* board) {
		if (!board) return u8"天气预报：暂无";
		if (board->IsWeatherPanelInterferenceActive()) {
			return u8"气象信号受干扰";
		}
		if (board->IsStormyNightForecastActive() || board->IsStormyNightActive()) {
			return u8"天气预报：暴风雨";
		}
		if (board->IsWeatherForecastDisrupted()) {
			return u8"天气预报：气象信号受干扰";
		}
		if (!board->HasWeatherForecast()) return u8"天气预报：暂无";

		const int seconds = std::max(0,
			static_cast<int>(std::ceil(board->GetWeatherTimer())));
		const RainIntensity forecast = board->GetForecastRainIntensity();
		std::string line = std::string(u8"天气预报（") + std::to_string(seconds)
			+ u8"秒）：" + PrecipitationDisplayName(board, forecast);
		if (forecast == board->GetRainIntensity()) line += u8"（持续）";
		if (forecast == RainIntensity::HEAVY && board->HasPendingHeavyTyphoon()) {
			const TyphoonStrength strength = board->GetPendingHeavyTyphoonStrength();
			if (strength != TyphoonStrength::NONE) {
				line += std::string(u8"·") + TyphoonStrengthDisplayName(strength);
			}
		}
		return line;
	}

	/** 寒潮预报与实况只读取 Board 已锁定计划，不复用允许误报的雨势预报。 */
	std::string ColdWavePanelText(const Board* board) {
		if (board && board->IsWeatherPanelInterferenceActive()) return {};
		if (board && board->IsColdWaveForecastDisruptionVisible()) {
			return u8"寒潮预报：气象信号受干扰";
		}
		if (!board || (!board->HasColdWaveForecast() && !board->IsColdWaveActive())) {
			return {};
		}
		const int seconds = std::max(0,
			static_cast<int>(std::ceil(board->GetColdWaveTimer())));
		const std::string strength = ColdWaveStrengthDisplayName(
			board->GetColdWaveStrength());
		if (board->HasColdWaveForecast()) {
			const int minimum = static_cast<int>(std::lround(
				board->GetColdWaveTargetTemperatureC()));
			return strength + u8"预报（" + std::to_string(seconds)
				+ u8"秒）：最低 " + std::to_string(minimum) + u8"°C";
		}
		switch (board->GetColdWavePhase()) {
		case ColdWavePhase::COOLING:
			return strength + u8"实况：降温中（"
				+ std::to_string(seconds) + u8"秒）";
		case ColdWavePhase::COLD:
			return strength + u8"实况：低温持续（"
				+ std::to_string(seconds) + u8"秒）";
		case ColdWavePhase::THAWING:
			return strength + u8"实况：回暖中（"
				+ std::to_string(seconds) + u8"秒）";
		case ColdWavePhase::CALM:
			break;
		}
		return {};
	}

	/** 雾势预报保持独立栏目；干扰只遮蔽公开值，不读取或改写已经锁定的真实雾势。 */
	std::string FogForecastPanelText(const Board* board) {
		if (!board) return {};
		if (board->IsWeatherPanelInterferenceActive()) return {};
		if (board->IsFogWeatherForecastDisrupted()) {
			return u8"雾势预报：气象信号受干扰";
		}
		if (!board->HasFogWeatherForecast()) return {};
		const int seconds = std::max(0,
			static_cast<int>(std::ceil(board->GetFogWeatherTimer())));
		std::string line = std::string(u8"雾势预报（") + std::to_string(seconds)
			+ u8"秒）：" + FogWeatherDisplayName(board->GetForecastFogWeatherIntensity());
		if (board->GetForecastFogWeatherIntensity() == board->GetFogWeatherIntensity()) {
			line += u8"（持续）";
		}
		return line;
	}

	/** 处决线只属于当前仍有效的劫持者锁定，不为未锁定状态预留文字行。 */
	bool ShouldDisplayNightRoofExecutionLine(const Board* board) {
		return board && board->GetNightRoofExecutionLine() > 0;
	}

	/** 风向使用“吹向”而不是气象来向，箭头与植物实际位移方向始终一致。 */
	const char* WindDirectionDisplayName(WindDirection direction) {
		switch (direction) {
		case WindDirection::TOWARD_HOUSE: return u8"← 吹向屋后";
		case WindDirection::TOWARD_FRONT: return u8"→ 吹向前线";
		case WindDirection::NONE:         return u8"无";
		}
		return u8"未知";
	}

	/** 把 bitmask 锁定行组格式化为面板文案，例如“第1、3行”。 */
	std::string RoofRunoffRowsDisplayName(const Board* board) {
		if (!board) return u8"未知行";
		std::string result = u8"第";
		bool hasRow = false;
		for (int row = 0; row < board->mRows; ++row) {
			if (!board->IsRoofRunoffRowSelected(row)) continue;
			if (hasRow) result += u8"、";
			result += std::to_string(row + 1);
			hasRow = true;
		}
		return hasRow ? result + u8"行" : std::string(u8"未知行");
	}

	/** 把黑夜屋顶锁定导电瓦路格式化为简短面板文案。 */
	std::string NightRoofChargeRowDisplayName(const Board* board) {
		if (!board || board->GetNightRoofChargeRow() < 0) return u8"未知行";
		return std::string(u8"第")
			+ std::to_string(board->GetNightRoofChargeRow() + 1) + u8"行";
	}

	/** 返回各档天气在面板上的强调色，并保留调用方提供的透明度。 */
	glm::vec4 RainIntensityTextColor(RainIntensity intensity, float alpha) {
		switch (intensity) {
		case RainIntensity::CLEAR:  return glm::vec4(164.0f, 224.0f, 145.0f, alpha);
		case RainIntensity::LIGHT:  return glm::vec4(154.0f, 214.0f, 255.0f, alpha);
		case RainIntensity::MEDIUM: return glm::vec4(92.0f, 169.0f, 255.0f, alpha);
		case RainIntensity::HEAVY:  return glm::vec4(255.0f, 166.0f, 116.0f, alpha);
		}
		return glm::vec4(230.0f, 230.0f, 230.0f, alpha);
	}

	glm::vec4 StormyNightTextColor(float alpha) {
		return glm::vec4(
			kStormyNightColorR, kStormyNightColorG, kStormyNightColorB, alpha);
	}

	/** 绘制统一累计条及可选的右端余量段；玩法只提供文案、比例和专属颜色。 */
	void DrawWeatherAccumulationGauge(Graphics* g, float x, float y,
		const std::string& label, float ratio, const glm::vec4& color,
		float visibility, float reserveRatio, const glm::vec4& reserveColor)
	{
		if (!g) return;
		const float clampedRatio = std::clamp(ratio, 0.0f, 1.0f);
		const float clampedReserveRatio = std::clamp(reserveRatio, 0.0f, 1.0f);
		const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 185.0f * visibility);
		g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			shadow, x + 1.0f, y + 1.0f);
		g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			color, x, y);

		const float barY = y + 20.0f;
		g->FillRect(x, barY, kWeatherGaugeWidth, kWeatherGaugeHeight,
			glm::vec4(5.0f, 13.0f, 24.0f, 150.0f * visibility));
		if (clampedRatio > 0.0f) {
			g->FillRect(x + 1.0f, barY + 1.0f,
				(kWeatherGaugeWidth - 2.0f) * clampedRatio,
				kWeatherGaugeHeight - 2.0f, color);
		}
		if (clampedReserveRatio > 0.0f) {
			const float reserveWidth = (kWeatherGaugeWidth - 2.0f)
				* clampedReserveRatio;
			g->FillRect(x + kWeatherGaugeWidth - 1.0f - reserveWidth,
				barY + 1.0f, reserveWidth, kWeatherGaugeHeight - 2.0f,
				reserveColor);
		}
		g->DrawRect(x, barY, kWeatherGaugeWidth, kWeatherGaugeHeight,
			glm::vec4(155.0f, 220.0f, 237.0f, 155.0f * visibility));
	}

	/** 按独立雾势、屋顶累计条与台风实况计算面板高度，避免内容落出底板。 */
	float WeatherPanelHeight(const Board* board) {
		if (!board) return kWeatherPanelHeight;
		if (board->IsWeatherPanelInterferenceActive()) return kWeatherPanelHeight;
		float height = kWeatherPanelHeight;
		const bool stormyNight = board->IsStormyNightForecastActive()
			|| board->IsStormyNightActive();
		if (!stormyNight && !FogForecastPanelText(board).empty()) {
			height += kWeatherPanelDetailLineHeight;
		}
		if (!ColdWavePanelText(board).empty()) {
			height += kWeatherPanelDetailLineHeight;
		}
		if (board->SupportsRoofRunoff()) height += kWeatherPanelGaugeLineHeight;
		if (board->SupportsNightRoofCharge()) {
			height += kWeatherPanelGaugeLineHeight;
			if (ShouldDisplayNightRoofExecutionLine(board)) {
				height += kWeatherPanelDetailLineHeight;
			}
		}
		if (board->HasTyphoon()) height += kWeatherPanelDetailLineHeight;
		return height;
	}

	/** 按当前字体测量 UI 文字宽度；字体未加载时只作保守半宽兜底。 */
	float MeasureUiTextWidth(const std::string& text, int fontSize)
	{
		int textWidth = static_cast<int>(text.size()) * fontSize / 2;
		if (TTF_Font* font = ResourceManager::GetInstance().GetFont(
			ResourceKeys::Fonts::FONT_FZCQ, fontSize)) {
			int textHeight = 0;
			TTF_SizeUTF8(font, text.c_str(), &textWidth, &textHeight);
		}
		return static_cast<float>(textWidth);
	}

	/** 绘制水平居中文字；阴影由调用方单独提交以保留配色自由度。 */
	void DrawCenteredUiText(Graphics* g, const std::string& text, int fontSize,
		const glm::vec4& color, float centerX, float y)
	{
		if (!g) return;
		const float textWidth = MeasureUiTextWidth(text, fontSize);
		g->DrawText(text, ResourceKeys::Fonts::FONT_FZCQ, fontSize,
			color, centerX - textWidth * 0.5f, y);
	}
}

GameScene::GameScene() {
}

GameScene::~GameScene() {
}

bool GameScene::TryStartCrazyDaveDialog(bool force)
{
	if (!mBoard || mBoard->mIsSurvival
		|| !CrazyDaveDialog::SupportsLevel(mBoard->mLevel)
		|| (mCrazyDaveDialog && mCrazyDaveDialog->IsActive())) {
		return false;
	}

	auto& gameApp = GameAPP::GetInstance();
	const int level = mBoard->mLevel;
	if (!force && gameApp.HasSeenCrazyDaveTutorial(level)) return false;

	// 对话按屏幕坐标构图；直切 GameScene 的测试或上个场景遗留相机都不能把气泡推离画面。
	gameApp.GetGraphics().SetCameraPosition(0.0f, 0.0f);
	mShakeCameraApplied = false;
	auto dialog = std::make_unique<CrazyDaveDialog>();
	if (!dialog->Start(level, [level]() {
		GameAPP::GetInstance().MarkCrazyDaveTutorialSeen(level);
		})) {
		return false;
	}
	mCrazyDaveDialog = std::move(dialog);
	return true;
}

bool GameScene::StartCrazyDaveDialogForTesting(bool force)
{
	return TryStartCrazyDaveDialog(force);
}

bool GameScene::AdvanceCrazyDaveDialogForTesting()
{
	return mCrazyDaveDialog && mCrazyDaveDialog->Advance();
}

bool GameScene::SkipCrazyDaveDialogForTesting()
{
	return mCrazyDaveDialog && mCrazyDaveDialog->Skip();
}

bool GameScene::IsCrazyDaveDialogSupported() const
{
	return mBoard && CrazyDaveDialog::SupportsLevel(mBoard->mLevel);
}

bool GameScene::IsCrazyDaveDialogActive() const
{
	return mCrazyDaveDialog && mCrazyDaveDialog->IsActive();
}

std::string GameScene::GetCrazyDaveDialogPhaseName() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetPhaseName() : "INACTIVE";
}

int GameScene::GetCrazyDaveDialogMessageIndex() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetMessageIndex() : 0;
}

int GameScene::GetCrazyDaveDialogMessageCount() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetMessageCount() : 0;
}

std::string GameScene::GetCrazyDaveDialogText() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetCurrentText() : std::string();
}

std::string GameScene::GetCrazyDaveDialogTrackName() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetCurrentTrackName() : std::string();
}

std::string GameScene::GetCrazyDaveDialogVoiceSoundKey() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetCurrentVoiceSoundKey() : std::string();
}

std::string GameScene::GetCrazyDaveDialogVoiceGroupName() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetCurrentVoiceGroupName() : std::string();
}

int GameScene::GetCrazyDaveDialogRenderedQuadCount() const
{
	return mCrazyDaveDialog ? mCrazyDaveDialog->GetRenderedQuadCount() : 0;
}

bool GameScene::HasCrazyDaveDialogRenderedGeometry() const
{
	return mCrazyDaveDialog && mCrazyDaveDialog->HasRenderedGeometry();
}

bool GameScene::DidCrazyDaveDialogUseInstancePath() const
{
	return mCrazyDaveDialog && mCrazyDaveDialog->UsedInstanceRenderPath();
}

std::string GameScene::GetWeatherForecastPanelText() const
{
	return WeatherForecastPanelText(mBoard.get());
}

std::string GameScene::GetColdWavePanelText() const
{
	return ColdWavePanelText(mBoard.get());
}

bool GameScene::IsNightRoofExecutionLineVisible() const
{
	return ShouldDisplayNightRoofExecutionLine(mBoard.get());
}

float GameScene::GetWeatherPanelHeight() const
{
	return WeatherPanelHeight(mBoard.get());
}

void GameScene::UpdateAfterGameObjects()
{
	if (mBoard) mBoard->UpdateMineInput();
	if (mCardSlotManager && (!mBoard || !mBoard->mMineToolActive)) mCardSlotManager->Update();
}

void GameScene::Draw(Graphics* g)
{
	// 屏幕抖动走相机（m_viewMatrix → projView push constant）而非变换栈：
	// reanim/血量字形的 GPU instancing 快路径不消费变换栈（InstanceRecord 直写世界坐标，
	// 见 Animator::DrawInternalInstanced / AppendReanimInstance），栈方案只能平移 batch
	// 路径的背景与 UI，植物/僵尸留在原位；projView 是全部管线（batch/instance/字形/
	// 延迟文字）的公共出口，相机平移一次覆盖，且视口剔除与 LogicalToWorld 自动一致。
	// 注意：不能每帧无条件写相机——开场动画的背景平移也在用 SetCameraPosition（本文件
	// Update 内 camX 路径）。抖动只发生在 GAME 状态（此时相机基线恒为 0），故只在
	// 抖动中覆写、结束后归零一次。相机设定须在 Scene::Draw 之前完成，保证本帧所有
	// flush（含 mid-frame blend 切换触发的）读到同一个 projView。
	const Vector shake = mBoard ? mBoard->GetShakeOffset() : Vector(0.0f, 0.0f);
	const bool shaking = (shake.x != 0.0f || shake.y != 0.0f);
	if (shaking) {
		g->SetCameraPosition(-shake.x, -shake.y);   // view 平移 = -cameraPos，场景整体移 +shake
		mShakeCameraApplied = true;
	}
	else if (mShakeCameraApplied) {
		g->SetCameraPosition(0.0f, 0.0f);
		mShakeCameraApplied = false;
	}
	// 手持植物属于世界层 GameObject，却以鼠标逻辑坐标为锚点。必须在开场平移/抖动
	// 已确定本帧相机后、GameObjectManager 提交前完成唯一一次逆变换，避免更新/绘制
	// 两阶段用不同坐标反复覆盖而在逻辑帧与渲染帧交错时横跳。
	if (mCardSlotManager) mCardSlotManager->PrepareDraw(g);
	Scene::Draw(g);
	if (GameAPP::GetInstance().mAutoTestMode && mCardSlotManager) {
		mCardSlotManager->CapturePreviewRenderState(g);
	}
}

/** 只在空格键轻量暂停期间绘制紧凑提示，不为其他模态暂停重复叠字。 */
void GameScene::DrawSpacePauseLabel(Graphics* g) const
{
	if (!g || !mSpacePauseActive) return;
	const float centerX = static_cast<float>(SCENE_WIDTH) * 0.5f;
	DrawCenteredUiText(g, u8"游戏暂停", kSpacePauseLabelFontSize,
		glm::vec4(0.0f, 0.0f, 0.0f, 220.0f), centerX + 2.0f, kSpacePauseLabelY + 2.0f);
	DrawCenteredUiText(g, u8"游戏暂停", kSpacePauseLabelFontSize,
		glm::vec4(255.0f, 244.0f, 196.0f, 255.0f), centerX, kSpacePauseLabelY);
}

/** 绘制当前迷雾关卡的逐格雾场；玩法状态完全来自 Board，UI 继续位于雾层之上。 */
void GameScene::DrawFog(Graphics* g) const
{
	if (!g || !mBoard || !mBoard->SupportsStageFog()) return;
	static const std::array<std::string, 8> kFogTextureKeys = {
		ResourceKeys::Textures::IMAGE_FOG_PART_0,
		ResourceKeys::Textures::IMAGE_FOG_PART_1,
		ResourceKeys::Textures::IMAGE_FOG_PART_2,
		ResourceKeys::Textures::IMAGE_FOG_PART_3,
		ResourceKeys::Textures::IMAGE_FOG_PART_4,
		ResourceKeys::Textures::IMAGE_FOG_PART_5,
		ResourceKeys::Textures::IMAGE_FOG_PART_6,
		ResourceKeys::Textures::IMAGE_FOG_PART_7,
	};
	auto& resources = ResourceManager::GetInstance();
	const int drawRows = mBoard->GetFogDrawRowCount();
	const int layerCount = mBoard->GetFogLayerCount();

	for (int row = 0; row < drawRows; ++row) {
		for (int col = 0; col < mBoard->mColumns; ++col) {
			const float alpha = mBoard->GetFogCellAlpha(row, col);
			if (alpha < 1.0f) continue;
			const Vector position = mBoard->GetFogTilePosition(row, col);
			// 原生雾片约四成像素接近全透明；先用错位的冷灰雾片补洞，避免满 alpha 时仍能
			// 清楚辨认底下僵尸。它消费同一逐格 alpha，所以路灯花照明仍同步清除此底层。
			const int occlusionVariant = mBoard->GetFogTileVariant(row + 19, col + 23);
			if (const Texture* occlusionTexture = resources.GetTexture(
				kFogTextureKeys[occlusionVariant], false)) {
				const glm::vec4 occlusionTint(190.0f, 207.0f, 222.0f,
					std::clamp(alpha * kFogOcclusionAlphaFactor, 0.0f, 255.0f));
				g->DrawTexture(occlusionTexture,
					position.x + kFogOcclusionOffsetX,
					position.y + kFogOcclusionOffsetY,
					kFogTileDrawWidth, kFogTileDrawHeight, 0.0f, occlusionTint);
				if (col == mBoard->mColumns - 1) {
					const int tailVariant = mBoard->GetFogTileVariant(
						row + 19, col + mBoard->mColumns + 29);
					if (const Texture* tailTexture = resources.GetTexture(
						kFogTextureKeys[tailVariant], false)) {
						g->DrawTexture(tailTexture,
							position.x + kFogOcclusionOffsetX + kFogTailTileOffsetX,
							position.y + kFogOcclusionOffsetY,
							kFogTileDrawWidth, kFogTileDrawHeight, 0.0f, occlusionTint);
					}
				}
			}
			// 补层先画、主层最后画；所有层共用逐格 alpha，所以台风驱散和回流不会产生残影。
			for (int layerIndex = layerCount - 1; layerIndex >= 0; --layerIndex) {
				const FogLayerSpec& layer = kFogLayers[layerIndex];
				const int variant = mBoard->GetFogTileVariant(
					row + layerIndex * 7, col + layerIndex * 11);
				const Texture* texture = resources.GetTexture(
					kFogTextureKeys[variant], false);
				if (!texture) continue;
				const float pulse = 0.96f + 0.04f * std::sin(
					mBoard->GetFogAnimationTime() * 0.9f
						+ static_cast<float>(row) * 0.7f
						+ static_cast<float>(col) * 0.45f
						+ static_cast<float>(layerIndex) * 1.35f);
				const glm::vec4 tint(225.0f, 233.0f, 242.0f,
					std::clamp(alpha * pulse * layer.alphaFactor, 0.0f, 255.0f));
				g->DrawTexture(texture,
					position.x + layer.offsetX, position.y + layer.offsetY,
					kFogTileDrawWidth, kFogTileDrawHeight, 0.0f, tint);

				if (col == mBoard->mColumns - 1) {
					// 1100px 扩展区使用另一稳定帧收边，避免原版同帧复制造成透明洞重合。
					const int tailVariant = mBoard->GetFogTileVariant(
						row + layerIndex * 7,
						col + mBoard->mColumns + layerIndex * 13);
					const Texture* tailTexture = resources.GetTexture(
						kFogTextureKeys[tailVariant], false);
					if (tailTexture) {
						g->DrawTexture(tailTexture,
							position.x + layer.offsetX + kFogTailTileOffsetX,
							position.y + layer.offsetY,
							kFogTileDrawWidth, kFogTileDrawHeight, 0.0f, tint);
					}
				}
			}
		}
	}
}

void GameScene::DrawWorldOverlay(Graphics* g)
{
	if (!g || !mBoard) return;
	// 岩壁属于战场前景，必须在 GOM 的 UI 尾段之前提交；Scene 命令数值不能插入 GOM 内部层级。
	if (mBoard->IsMineBackground()) {
		PROFILE_SCOPE("8b0.Draw_mineWalls");
		mBoard->DrawMineWalls(g);
	}
	// 雾先遮住战场与世界粒子，随后再统一接受雨天暗幕；闪电最后照亮雾层但仍不覆盖 UI。
	{
		PROFILE_SCOPE("8b1.Draw_fog");
		DrawFog(g);
	}
	{
		PROFILE_SCOPE("8b2.Draw_roofRunoff");
		DrawRoofRunoff(g);
	}
	{
		PROFILE_SCOPE("8b3.Draw_rainOverlay");
		const float alpha = mBoard->GetRainOverlayAlpha();
		if (alpha > 0.0f) {
			// 低成本雨天环境光：只做蓝灰半透明暗幕。该钩子在战场主体与世界粒子之后、UI overlay
			// 之前调用，因而背景、实体和世界特效统一变暗，但卡片、按钮和文字保持清晰。
			g->FillRect(-20.0f, 0.0f,
				static_cast<float>(SCENE_WIDTH + 500), static_cast<float>(SCENE_HEIGHT),
				glm::vec4(36.0f, 52.0f, 78.0f, alpha));		// -20 500的预留空间
		}
	}
	const bool stormyNightActive = mBoard->IsStormyNightActive();
	{
		PROFILE_SCOPE("8b4.Draw_stormyNight");
		if (stormyNightActive) {
			// 与 C# 4-10 相同：战场世界层常态全黑，闪电时降低黑幕并短暂叠加白光；UI 稍后绘制。
			const float blackAlpha = mBoard->GetStormyNightBlackAlpha();
			const float whiteAlpha = mBoard->GetStormyNightWhiteAlpha();
			if (blackAlpha > 0.0f) {
				g->FillRect(-1000.0f, -1000.0f,
					static_cast<float>(SCENE_WIDTH + 2000),
					static_cast<float>(SCENE_HEIGHT + 2000),
					glm::vec4(0.0f, 0.0f, 0.0f, blackAlpha));
			}
			if (whiteAlpha > 0.0f) {
				g->FillRect(-1000.0f, -1000.0f,
					static_cast<float>(SCENE_WIDTH + 2000),
					static_cast<float>(SCENE_HEIGHT + 2000),
					glm::vec4(255.0f, 255.0f, 255.0f, whiteAlpha));
			}
		}
	}
	if (!stormyNightActive) {
		{
			PROFILE_SCOPE("8b5.Draw_nightRoofCharge");
			DrawNightRoofCharge(g);
		}
		{
			PROFILE_SCOPE("8b6.Draw_lightning");
			DrawLightningStrike(g);
		}
	}
	DrawCobCannonTarget(g);
}

void GameScene::DrawCobCannonTarget(Graphics* g) const
{
	if (!g || !mBoard || !mBoard->IsCobCannonTargeting()) return;
	const Vector target = GameAPP::GetInstance().GetInputHandler().GetMouseWorldPosition();
	auto& resources = ResourceManager::GetInstance();
	const Texture* shadow = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_COBCANNON_TARGET_SHADOW, false);
	const Texture* marker = resources.GetTexture(
		ResourceKeys::Textures::IMAGE_COBCANNON_TARGET, false);
	if (shadow) {
		g->DrawTexture(shadow,
			target.x - static_cast<float>(shadow->width) * 0.5f,
			target.y - static_cast<float>(shadow->height) * 0.5f,
			static_cast<float>(shadow->width), static_cast<float>(shadow->height));
	}
	if (marker) {
		g->DrawTexture(marker,
			target.x - static_cast<float>(marker->width) * 0.5f,
			target.y - static_cast<float>(marker->height) * 0.5f,
			static_cast<float>(marker->width), static_cast<float>(marker->height));
	}
}

float GameScene::GetRoofRainBackgroundAlpha() const
{
	if (!mBoard || (mBoard->mBackGround != Background::ROOF
		&& mBoard->mBackGround != Background::NIGHT_ROOF)) return 0.0f;
	return std::clamp(mBoard->GetRainOverlayAlpha() * kRoofRainBackgroundAlphaScale,
		0.0f, 255.0f);
}

void GameScene::DrawRoofRainBackground(Graphics* g)
{
	if (!g) return;
	const float alpha = GetRoofRainBackgroundAlpha();
	if (alpha <= 0.0f) return;

	const std::string& rainBackgroundKey = mBoard->mBackGround == Background::NIGHT_ROOF
		? ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHTROOF_RAIN
		: ResourceKeys::Textures::IMAGE_BACKGROUND_ROOF_RAIN;
	const Texture* rainBackground = ResourceManager::GetInstance().GetTexture(
		rainBackgroundKey, false);
	if (!rainBackground) return;

	// 昼夜雨景都只替换静态背景层；Board 网格、实体位置和后续世界雨幕仍走原有路径。
	g->DrawTexture(rainBackground, mStartX, mBackgroundY,
		static_cast<float>(rainBackground->width),
		static_cast<float>(rainBackground->height), 0.0f,
		glm::vec4(255.0f, 255.0f, 255.0f, alpha));
}

/**
 * 径流必须沿 Board 的连续坡面采样，不能用水平贴图假装水流。预警只勾勒目标行；
 * 正式阶段再叠加低透明水膜、向房屋侧移动的稀疏水珠和屋檐端飞溅。
 */
void GameScene::DrawRoofRunoff(Graphics* g) const
{
	if (!g || !mBoard || !mBoard->SupportsRoofRunoff()) return;
	const RoofRunoffPhase phase = mBoard->GetRoofRunoffPhase();
	if (phase == RoofRunoffPhase::IDLE || mBoard->GetRoofRunoffRowCount() <= 0) return;

	const float slopeEndX = mBoard->GetRoofSlopeEndX();
	const float pulse = 0.68f + 0.32f * std::sin(
		static_cast<float>(mBoard->mBoardFrame) * 0.16f);
	const BlendMode previousBlend = g->GetBlendMode();
	g->SetBlendMode(BlendMode::Alpha);

	if (phase == RoofRunoffPhase::WARNING) {
		// 稀疏湿润光点只负责标出范围；不画贯穿整行的亮直线，避免抢在冲刷前伪装成水柱。
		const float warningOffset = std::fmod(
			static_cast<float>(mBoard->mBoardFrame) * 0.55f, 92.0f);
		for (int row = 0; row < mBoard->mRows; ++row) {
			if (!mBoard->IsRoofRunoffRowSelected(row)) continue;
			for (int edge : { -1, 1 }) {
				const float offsetY = static_cast<float>(edge) * 31.0f;
				for (float x = slopeEndX - warningOffset;
					x >= kRoofRunoffSlopeStartX; x -= 92.0f) {
					g->FillCircle(x, mBoard->GetRowCenterYAtX(row, x) + offsetY,
						2.2f, glm::vec4(93.0f, 196.0f, 222.0f, 54.0f * pulse), 8);
				}
			}
		}
		g->SetBlendMode(previousBlend);
		return;
	}

	for (int row = 0; row < mBoard->mRows; ++row) {
		if (!mBoard->IsRoofRunoffRowSelected(row)) continue;
		// 窄竖片拼成没有描边的半透明水膜；颜色变化是大尺度缓变，不形成规则流线。
		for (float x = kRoofRunoffSlopeStartX; x < slopeEndX;
			x += kRoofRunoffSheetSliceWidth) {
			const float width = std::min(kRoofRunoffSheetSliceWidth + 0.5f, slopeEndX - x);
			const float shimmer = 0.5f + 0.5f * std::sin(x * 0.035f
				+ static_cast<float>(mBoard->mBoardFrame) * 0.045f);
			g->FillRect(x, mBoard->GetRowCenterYAtX(row, x) - 32.0f,
				width, 64.0f, glm::vec4(43.0f, 143.0f, 185.0f, 8.0f + 4.0f * shimmer));
		}

		// 每行只留五颗错位水珠提供动态流向，静帧不再呈现成排短线。
		const float slopeSpan = std::max(1.0f, slopeEndX - kRoofRunoffSlopeStartX);
		for (int droplet = 0; droplet < 5; ++droplet) {
			const float travel = std::fmod(mBoard->GetRoofRunoffFlowProgress()
				* kRoofRunoffDropletTravelPerFlow + static_cast<float>(droplet) * 97.0f,
				slopeSpan);
			const float x = slopeEndX - travel;
			const float offsetY = -24.0f + static_cast<float>((droplet * 13) % 49);
			g->FillCircle(x, mBoard->GetRowCenterYAtX(row, x) + offsetY,
				2.0f + static_cast<float>(droplet % 2),
				glm::vec4(112.0f, 211.0f, 232.0f, 78.0f), 10);
		}

		// 每条被选行各自拥有屋檐端飞溅，和同一行水膜保持一一对应。
		const float eaveY = mBoard->GetRowCenterYAtX(row, kRoofRunoffSlopeStartX);
		for (int splash = 0; splash < 3; ++splash) {
			const float wobble = std::sin(static_cast<float>(mBoard->mBoardFrame) * 0.22f
				+ static_cast<float>(splash) * 2.1f);
			g->FillCircle(kRoofRunoffSlopeStartX - 5.0f - splash * 7.0f,
				eaveY - 14.0f + splash * 13.0f + wobble * 4.0f,
				3.0f + splash, glm::vec4(139.0f, 235.0f, 255.0f, 105.0f), 12);
		}
	}
	g->SetBlendMode(previousBlend);
}

/**
 * 雷荷预警以离散瓦片节点表达锁定路线，避免长期覆盖战场；正式放电才用短暂折线沿平台和坡面
 * 贯穿到屋檐。全部抖动由锁定行、分段编号和 Board 帧派生，不保存第二份玩法状态。
 */
void GameScene::DrawNightRoofCharge(Graphics* g) const
{
	if (!g || !mBoard || !mBoard->SupportsNightRoofCharge()) return;
	const int row = mBoard->GetNightRoofChargeRow();
	if (row < 0 || row >= mBoard->mRows) return;
	if (!mBoard->IsNightRoofChargeWarning()
		&& !mBoard->IsNightRoofChargeDischarging()) return;

	const float leftX = CELL_INITALIZE_POS_X + kNightRoofChargeRouteInsetX;
	const float rightX = CELL_INITALIZE_POS_X
		+ static_cast<float>(mBoard->mColumns) * CELL_COLLIDER_SIZE_X
		- kNightRoofChargeRouteInsetX;
	const BlendMode previousBlend = g->GetBlendMode();
	g->SetBlendMode(BlendMode::Add);
	Vector guideAnchor;
	const bool hasGuideAnchor = mBoard->TryGetNightRoofChargeGuideAnchor(guideAnchor);

	if (mBoard->IsNightRoofChargeWarning()) {
		const float pulse = 0.62f + 0.38f * std::sin(
			static_cast<float>(mBoard->mBoardFrame) * 0.18f);
		for (int column = 0; column < mBoard->mColumns; ++column) {
			const float x = CELL_INITALIZE_POS_X
				+ (static_cast<float>(column) + 0.5f) * CELL_COLLIDER_SIZE_X;
			const float y = mBoard->GetRowCenterYAtX(row, x);
			const float stagger = 0.72f + 0.28f * std::sin(
				static_cast<float>(mBoard->mBoardFrame) * 0.13f
				+ static_cast<float>(column) * 1.7f);
			g->FillCircle(x, y, 7.5f,
				glm::vec4(142.0f, 99.0f, 255.0f, 25.0f * pulse * stagger), 18);
			g->FillCircle(x, y, 2.5f,
				glm::vec4(222.0f, 212.0f, 255.0f, 135.0f * pulse * stagger), 12);
			// 每隔数帧只让少量节点分叉一下，提示通电而不画成长时间常亮直线。
			if ((mBoard->mBoardFrame / 5 + column * 3 + row) % 7 == 0) {
				g->DrawLine(x - 7.0f, y + 1.0f, x - 1.0f, y - 5.0f,
					glm::vec4(183.0f, 163.0f, 255.0f, 118.0f));
				g->DrawLine(x - 1.0f, y - 5.0f, x + 5.0f, y + 3.0f,
					glm::vec4(237.0f, 229.0f, 255.0f, 170.0f));
			}
		}
		if (hasGuideAnchor) {
			const float flicker = 0.72f + 0.28f * std::sin(
				static_cast<float>(mBoard->mBoardFrame) * 0.47f);
			g->FillCircle(guideAnchor.x, guideAnchor.y, 6.0f,
				glm::vec4(169.0f, 92.0f, 255.0f, 78.0f * flicker), 14);
			g->FillCircle(guideAnchor.x, guideAnchor.y, 2.0f,
				glm::vec4(241.0f, 222.0f, 255.0f, 205.0f * flicker), 10);
			for (int branch = 0; branch < 2; ++branch) {
				const float phase = static_cast<float>(mBoard->mBoardFrame) * 0.31f
					+ static_cast<float>(branch) * 2.4f;
				const glm::vec2 middle(guideAnchor.x + std::sin(phase) * 5.0f,
					guideAnchor.y - 8.0f - branch * 3.0f);
				g->DrawLine(guideAnchor.x, guideAnchor.y, middle.x, middle.y,
					glm::vec4(218.0f, 188.0f, 255.0f, 155.0f * flicker));
			}
		}
		g->SetBlendMode(previousBlend);
		return;
	}

	const float progress = mBoard->GetNightRoofChargeDischargeProgress();
	const float mainPulse = std::exp(-progress * 7.5f);
	const float returnPulse = progress >= 0.34f
		? 0.72f * std::exp(-(progress - 0.34f) * 9.0f) : 0.0f;
	const float pulse = std::clamp(std::max(mainPulse, returnPulse), 0.0f, 1.0f);
	if (pulse > 0.005f) {
		glm::vec2 previous(rightX, mBoard->GetRowCenterYAtX(row, rightX));
		for (int segment = 1; segment <= kNightRoofChargeRouteSegments; ++segment) {
			const float routeProgress = static_cast<float>(segment)
				/ static_cast<float>(kNightRoofChargeRouteSegments);
			const float x = rightX + (leftX - rightX) * routeProgress;
			uint32_t hash = static_cast<uint32_t>((row + 1) * 2246822519u)
				^ static_cast<uint32_t>(segment * 3266489917u);
			hash ^= hash >> 15;
			const float unit = static_cast<float>(hash & 0xFFFFu) / 65535.0f;
			const float endpointFactor = std::sin(routeProgress * 3.14159265f);
			const float y = mBoard->GetRowCenterYAtX(row, x)
				+ (unit * 2.0f - 1.0f) * 13.0f * endpointFactor;
			const glm::vec2 current(x, y);
			DrawLightningSegment(g, previous, current,
				4.0f, 26.0f * pulse, 210.0f * pulse);
			previous = current;
		}

		for (int column = 0; column < mBoard->mColumns; ++column) {
			const float x = CELL_INITALIZE_POS_X
				+ (static_cast<float>(column) + 0.5f) * CELL_COLLIDER_SIZE_X;
			g->FillCircle(x, mBoard->GetRowCenterYAtX(row, x),
				5.0f + 3.0f * pulse,
				glm::vec4(190.0f, 166.0f, 255.0f, 92.0f * pulse), 18);
		}
		if (hasGuideAnchor) {
			glm::vec2 previous(guideAnchor.x - 4.0f, guideAnchor.y - 32.0f);
			for (int segment = 1; segment <= 4; ++segment) {
				const float t = static_cast<float>(segment) / 4.0f;
				const float x = guideAnchor.x
					+ std::sin(static_cast<float>(segment) * 4.1f) * 4.0f * (1.0f - t);
				const float y = guideAnchor.y - 32.0f * (1.0f - t);
				const glm::vec2 current(x, y);
				DrawLightningSegment(g, previous, current,
					2.2f, 18.0f * pulse, 185.0f * pulse);
				previous = current;
			}
		}
	}
	g->SetBlendMode(previousBlend);
}

/** 绘制冷白闪电主干、分叉和有限半径的云层/落点散射光，不覆盖 UI。 */
void GameScene::DrawLightningStrike(Graphics* g) const
{
	if (!g || mLightningFlashTimer <= 0.0f || mLightningMainPath.size() < 2) return;

	const float elapsed = mLightningFlashDuration - mLightningFlashTimer;
	// 首次主放电极快衰减，约 0.09 秒后追加较弱回闪，形成自然的双脉冲。
	const float mainPulse = std::exp(-elapsed * 24.0f);
	const float returnPulse = elapsed >= 0.09f
		? 0.68f * std::exp(-(elapsed - 0.09f) * 18.0f) : 0.0f;
	const float pulse = std::clamp(std::max(mainPulse, returnPulse), 0.0f, 1.0f);
	if (pulse <= 0.005f) return;

	const BlendMode previousBlend = g->GetBlendMode();
	g->SetBlendMode(BlendMode::Add);

	// 多层低透明圆模拟有限范围的空气散射，中心更亮但不会把整屏和 UI 一并漂白。
	auto drawRadialGlow = [g, pulse](const glm::vec2& center, float radius,
		const glm::vec3& color, float peakAlpha) {
		constexpr int kGlowLayers = 7;
		for (int layer = kGlowLayers; layer >= 1; --layer) {
			const float scale = static_cast<float>(layer) / static_cast<float>(kGlowLayers);
			const float layerAlpha = peakAlpha * pulse
				* (0.06f + (1.0f - scale) * 0.10f);
			g->FillCircle(center.x, center.y, radius * scale,
				glm::vec4(color, layerAlpha), 40);
		}
	};

	drawRadialGlow(mLightningMainPath.front() + glm::vec2(0.0f, 24.0f),
		190.0f, glm::vec3(132.0f, 164.0f, 255.0f), 34.0f);
	drawRadialGlow(mLightningStrikePoint,
		155.0f, glm::vec3(144.0f, 178.0f, 255.0f), 42.0f);

	for (const auto& branch : mLightningBranches) {
		DrawLightningSegment(g, branch.first, branch.second,
			2.0f, 12.0f * pulse, 112.0f * pulse);
	}
	for (size_t i = 1; i < mLightningMainPath.size(); ++i) {
		DrawLightningSegment(g, mLightningMainPath[i - 1], mLightningMainPath[i],
			4.0f, 18.0f * pulse, 190.0f * pulse);
	}

	// 落点的紧凑高光和贴地侧闪让闪电真正“落在草坪”，而不是悬浮在屏幕上。
	g->FillCircle(mLightningStrikePoint.x, mLightningStrikePoint.y,
		9.0f, glm::vec4(230.0f, 240.0f, 255.0f, 185.0f * pulse), 24);
	g->DrawLine(mLightningStrikePoint.x - 34.0f, mLightningStrikePoint.y + 2.0f,
		mLightningStrikePoint.x + 28.0f, mLightningStrikePoint.y + 2.0f,
		glm::vec4(172.0f, 203.0f, 255.0f, 105.0f * pulse));

	g->SetBlendMode(previousBlend);
}

/** 用未缩放时间推进天气面板与失败提示，使游戏倍速不改变 UI 动画观感。 */
void GameScene::UpdateWeatherUi(float deltaTime)
{
	const bool shouldShow = mBoard && mBoard->mBoardState == BoardState::GAME
		&& mBoard->SupportsWeather();
	const float direction = shouldShow ? 1.0f : -1.0f;
	mWeatherPanelSlide = std::clamp(mWeatherPanelSlide
		+ direction * deltaTime / kWeatherPanelSlideDuration, 0.0f, 1.0f);
	if (mCurrentWeatherNoticeTimer > 0.0f) {
		mCurrentWeatherNoticeTimer = std::max(0.0f,
			mCurrentWeatherNoticeTimer - deltaTime);
	}
	if (mWeatherForecastFailureTimer > 0.0f) {
		mWeatherForecastFailureTimer = std::max(0.0f,
			mWeatherForecastFailureTimer - deltaTime);
	}
	if (mBoard && mBoard->IsWeatherPanelInterferenceActive()) {
		CancelWeatherInformationNotices();
	}
}

/** 在天气栏之外常驻绘制冬日花园温度计，并明确标出三条玩法阈值。 */
void GameScene::DrawWinterThermometer(Graphics* g) const
{
	if (!g || !mBoard || mBoard->mBoardState != BoardState::GAME
		|| !mBoard->SupportsWinterTemperature()) return;
	const Texture* frame = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_WINTER_THERMOMETER_FRAME_V1, false);
	if (!frame) return;

	const float gaugeRatio = mBoard->GetWinterTemperatureGaugeRatio();
	const float fillTopY = kWinterThermometerTubeBottomY
		- (kWinterThermometerTubeBottomY - kWinterThermometerTubeTopY)
			* gaugeRatio;
	const bool frozen = mBoard->GetAmbientTemperatureC()
		<= mBoard->GetWinterFreezingTemperatureC();
	const glm::vec4 liquidColor = frozen
		? glm::vec4(64.0f, 177.0f, 244.0f, 248.0f)
		: glm::vec4(235.0f, 104.0f, 54.0f, 248.0f);
	const glm::vec4 liquidHighlight = frozen
		? glm::vec4(181.0f, 235.0f, 255.0f, 205.0f)
		: glm::vec4(255.0f, 205.0f, 112.0f, 205.0f);

	// 液体先画入贴图的透明管腔；球泡略扩到木框下方，让贴图的平滑内缘裁掉程序圆边。
	g->FillCircle(kWinterThermometerTubeCenterX,
		kWinterThermometerBulbCenterY, 21.0f, liquidColor, 64);
	g->FillRect(kWinterThermometerTubeCenterX - 5.0f, fillTopY, 10.0f,
		kWinterThermometerBulbCenterY - fillTopY, liquidColor);
	g->FillRect(kWinterThermometerTubeCenterX - 2.5f, fillTopY + 2.0f, 2.0f,
		std::max(0.0f, kWinterThermometerBulbCenterY - fillTopY - 8.0f),
		liquidHighlight);
	g->DrawTexture(frame, kWinterThermometerFrameX, kWinterThermometerFrameY,
		kWinterThermometerFrameWidth, kWinterThermometerFrameHeight);

	const float minimum = mBoard->GetWinterMinimumTemperatureC();
	const float maximum = mBoard->GetWinterMaximumTemperatureC();
	const float freezing = mBoard->GetWinterFreezingTemperatureC();
	const float freezingRatio = std::clamp((freezing - minimum)
		/ (maximum - minimum), 0.0f, 1.0f);
	const float freezingY = kWinterThermometerTubeBottomY
		- (kWinterThermometerTubeBottomY - kWinterThermometerTubeTopY)
			* freezingRatio;
	const glm::vec4 tickColor(242.0f, 232.0f, 194.0f, 235.0f);
	const glm::vec4 textColor(242.0f, 241.0f, 223.0f, 255.0f);
	const glm::vec4 shadow(22.0f, 18.0f, 13.0f, 220.0f);
	const auto drawScaleLabel = [g, &tickColor, &textColor, &shadow](
		float y, const std::string& text) {
		g->FillRect(77.0f, y - 1.5f, 10.0f, 3.0f, shadow);
		g->FillRect(77.0f, y - 1.0f, 10.0f, 2.0f, tickColor);
		g->DrawText(text, ResourceKeys::Fonts::FONT_FZCQ,
			kWinterThermometerScaleFontSize, shadow,
			kWinterThermometerLabelX + 1.0f, y - 8.0f + 1.0f);
		g->DrawText(text, ResourceKeys::Fonts::FONT_FZCQ,
			kWinterThermometerScaleFontSize, textColor,
			kWinterThermometerLabelX, y - 8.0f);
	};
	drawScaleLabel(kWinterThermometerTubeTopY, u8"+6° 上限");
	drawScaleLabel(freezingY, u8"0° 结冰");
	drawScaleLabel(kWinterThermometerTubeBottomY, u8"-12° 下限");

	const int current = static_cast<int>(std::lround(mBoard->GetAmbientTemperatureC()));
	const std::string currentText = std::string(u8"当前 ")
		+ (current > 0 ? "+" : "") + std::to_string(current) + u8"°C";
	constexpr float kCurrentPlateX = 3.0f;  // 当前温度底板左缘，单位：逻辑像素
	constexpr float kCurrentPlateY = 381.0f; // 当前温度底板顶缘，单位：逻辑像素
	constexpr float kCurrentPlateWidth = 144.0f; // 当前温度底板宽度，覆盖完整中文数值
	constexpr float kCurrentPlateHeight = 26.0f; // 当前温度底板高度，单位：逻辑像素
	g->FillRect(kCurrentPlateX + 2.0f, kCurrentPlateY + 2.0f,
		kCurrentPlateWidth, kCurrentPlateHeight,
		glm::vec4(0.0f, 0.0f, 0.0f, 92.0f));
	g->FillRect(kCurrentPlateX, kCurrentPlateY,
		kCurrentPlateWidth, kCurrentPlateHeight,
		glm::vec4(69.0f, 50.0f, 30.0f, 205.0f));
	g->DrawRect(kCurrentPlateX, kCurrentPlateY,
		kCurrentPlateWidth, kCurrentPlateHeight,
		glm::vec4(214.0f, 175.0f, 92.0f, 225.0f));
	DrawCenteredUiText(g, currentText, kWinterThermometerCurrentFontSize,
		shadow, kCurrentPlateX + kCurrentPlateWidth * 0.5f + 1.0f,
		kCurrentPlateY + 4.0f + 1.0f);
	DrawCenteredUiText(g, currentText, kWinterThermometerCurrentFontSize,
		textColor, kCurrentPlateX + kCurrentPlateWidth * 0.5f,
		kCurrentPlateY + 4.0f);
}

/** 三只仪表始终读取 Board 当前实数；红色只表示阈值，不泄露隐藏计划。 */
void GameScene::DrawPolarNightInstruments(Graphics* g) const
{
	if (!g || !mBoard || mBoard->mBoardState != BoardState::GAME
		|| !mBoard->SupportsPolarNightEnvironment()) return;
	constexpr float panelX = 7.0f;       // 帐篷侧观测站左缘，逻辑像素
	constexpr float panelY = 405.0f;     // 与底部文字留出余量，保持三只仪表完整可读
	constexpr float panelWidth = 170.0f; // 中号底板兼顾帐篷侧构图与实数可读性
	constexpr float rowHeight = 44.0f;   // 恢复足够的文字、读数和进度条间距
	constexpr float panelHeight = rowHeight * 3.0f + 15.0f; // 完整包住末行进度条并留出底边距
	const glm::vec4 plate(16.0f, 30.0f, 48.0f, 222.0f);
	const glm::vec4 border(142.0f, 183.0f, 211.0f, 235.0f);
	const glm::vec4 safe(167.0f, 219.0f, 237.0f, 245.0f);
	const glm::vec4 danger(255.0f, 82.0f, 72.0f, 255.0f);
	const glm::vec4 shadow(3.0f, 9.0f, 16.0f, 235.0f);
	g->FillRect(panelX + 2.0f, panelY + 2.0f,
		panelWidth, panelHeight,
		glm::vec4(0.0f, 0.0f, 0.0f, 105.0f));
	g->FillRect(panelX, panelY, panelWidth, panelHeight, plate);
	g->DrawRect(panelX, panelY, panelWidth, panelHeight, border);
	g->DrawText(u8"极地观测站", ResourceKeys::Fonts::FONT_FZCQ, 16,
		safe, panelX + 8.0f, panelY + 3.0f);

	auto trendText = [](float current, float target) -> const char* {
		if (target < current - 0.25f) return u8"↓";
		if (target > current + 0.25f) return u8"↑";
		return u8"→";
	};
	auto drawGauge = [&](int index, const std::string& label,
		const std::string& value, float ratio, bool isDanger,
		const char* trend) {
		const float y = panelY + 24.0f + index * rowHeight;
		const glm::vec4 color = isDanger ? danger : safe;
		g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, 14,
			shadow, panelX + 9.0f, y + 1.0f);
		g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, 14,
			color, panelX + 8.0f, y);
		g->DrawText(value + " " + trend, ResourceKeys::Fonts::FONT_FZCQ, 14,
			color, panelX + 88.0f, y);
		g->FillRect(panelX + 8.0f, y + 20.0f, panelWidth - 16.0f, 8.0f,
			glm::vec4(4.0f, 12.0f, 22.0f, 230.0f));
		g->FillRect(panelX + 8.0f, y + 20.0f,
			(panelWidth - 16.0f) * std::clamp(ratio, 0.0f, 1.0f), 8.0f, color);
	};

	const float temperature = mBoard->GetPolarTemperatureC();
	const float humidity = mBoard->GetPolarHumidityPercent();
	const float wind = mBoard->GetPolarWindSpeedMps();
	drawGauge(0, u8"温度", std::to_string(static_cast<int>(std::lround(temperature))) + u8"°C",
		(-2.0f - temperature) / 23.0f, mBoard->IsPolarTemperatureDangerous(),
		trendText(temperature, mBoard->GetPolarTargetTemperatureC()));
	drawGauge(1, u8"湿度", std::to_string(static_cast<int>(std::lround(humidity))) + "%",
		humidity / 100.0f, mBoard->IsPolarHumidityDangerous(),
		trendText(humidity, mBoard->GetPolarTargetHumidityPercent()));
	const VerticalWindDirection direction = mBoard->GetPolarVerticalWindDirection();
	const std::string windDirection = direction == VerticalWindDirection::UP
		? u8"↑" : (direction == VerticalWindDirection::DOWN ? u8"↓" : u8"·");
	drawGauge(2, std::string(u8"风速 ") + windDirection,
		std::to_string(static_cast<int>(std::lround(wind))) + "m/s",
		wind / 30.0f, mBoard->IsPolarWindDangerous(),
		trendText(wind, mBoard->GetPolarTargetWindSpeedMps()));
}

/** 按湿度与强风绘制常态雪层；白毛风再压低世界对比度，但不覆盖顶栏和观测站。 */
void GameScene::DrawPolarNightWhiteout(Graphics* g) const
{
	if (!g || !mBoard || !mBoard->SupportsPolarNightEnvironment()) return;
	const float strength = mBoard->GetPolarWhiteoutVisualStrength();
	const float humidity = std::clamp(
		mBoard->GetPolarHumidityPercent() / 100.0f, 0.0f, 1.0f);
	const float frame = static_cast<float>(mBoard->mBoardFrame);

	// 极夜雪原即使在假信号阶段也保持贴地流雪；这层不承担玩法信息。
	for (int i = 0; i < 11; ++i) {
		const float phase = std::fmod(static_cast<float>(i * 131) + frame * 1.15f,
			1040.0f);
		const float x = 210.0f + phase;
		const float y = 505.0f + static_cast<float>((i * 23) % 82);
		const float length = 24.0f + static_cast<float>((i * 17) % 38);
		g->DrawLine(x, y, x + length, y - 1.5f,
			glm::vec4(225.0f, 241.0f, 251.0f, 24.0f + humidity * 28.0f));
	}

	// 湿度越高，竖直落雪越密；85% 危险线以上保持稳定密度。
	const int fallingSnowCount = 5 + static_cast<int>(humidity * 23.0f);
	for (int i = 0; i < fallingSnowCount; ++i) {
		const float x = 230.0f + static_cast<float>((i * 149 + 37) % 850);
		const float phase = std::fmod(static_cast<float>((i * 71) % 510)
			+ frame * (0.65f + static_cast<float>(i % 3) * 0.12f), 520.0f);
		const float y = 78.0f + phase;
		const float radius = 0.8f + static_cast<float>(i % 3) * 0.45f;
		g->FillCircle(x, y, radius,
			glm::vec4(237.0f, 248.0f, 255.0f, 42.0f + humidity * 88.0f), 8);
	}

	if (strength > 0.0f) {
		g->FillRect(225.0f, 76.0f, 875.0f, 524.0f,
			glm::vec4(199.0f, 220.0f, 236.0f, 42.0f * strength));
	}
	const float windVisualStrength = mBoard->GetPolarWindVisualStrength();
	const VerticalWindDirection visualWindDirection =
		mBoard->GetPolarWindVisualDirection();
	const float direction = visualWindDirection
		== VerticalWindDirection::UP ? -1.0f : 1.0f;
	// 粒子承担主要风雪质感；这里只保留少量清晰斜线传达上下风切方向。
	const int windLineCount = visualWindDirection != VerticalWindDirection::NONE
		&& windVisualStrength > 0.0f
		? 1 + static_cast<int>(2.0f * windVisualStrength) : 0;
	for (int i = 0; i < windLineCount; ++i) {
		const float phase = std::fmod(static_cast<float>(i * 97)
			+ frame * (2.25f + 1.25f * strength), 1040.0f);
		const float x = 185.0f + phase;
		const float y = 90.0f + static_cast<float>((i * 43) % 490);
		const float length = 46.0f + static_cast<float>((i * 19) % 58);
		const glm::vec4 snow(235.0f, 247.0f, 255.0f,
			((38.0f + static_cast<float>((i * 11) % 42))
				+ (54.0f + static_cast<float>((i * 7) % 38)) * strength)
			* windVisualStrength);
		g->DrawLine(x, y, x + length, y + direction * 4.0f, snow);
		g->DrawLine(x, y + 1.0f, x + length, y + direction * 4.0f + 1.0f, snow);
	}
}

/** 在左上角绘制当前天气与已锁定的下一天气预警。 */
void GameScene::DrawWeatherPanel(Graphics* g) const
{
	if (!g || !mBoard || mWeatherPanelSlide <= 0.0f) return;

	const float eased = mWeatherPanelSlide * mWeatherPanelSlide
		* (3.0f - 2.0f * mWeatherPanelSlide);
	const float x = -kWeatherPanelWidth
		+ (kWeatherPanelWidth + kWeatherPanelVisibleX) * eased;
	const float alpha = 255.0f * eased;
	const float panelHeight = WeatherPanelHeight(mBoard.get());
	const bool stormyNightForecast = mBoard->IsStormyNightForecastActive()
		|| mBoard->IsStormyNightActive();
	const glm::vec4 accentColor = mBoard->IsWeatherPanelInterferenceActive()
		? glm::vec4(255.0f, 181.0f, 88.0f, alpha)
		: stormyNightForecast
		? StormyNightTextColor(alpha)
		: RainIntensityTextColor(mBoard->GetRainIntensity(), alpha);

	// 深蓝半透明底板配强度色边条；矩形方案不新增贴图，分辨率和全屏模式都保持锐利。
	g->FillRect(x + 3.0f, kWeatherPanelY + 3.0f,
		kWeatherPanelWidth, panelHeight,
		glm::vec4(0.0f, 0.0f, 0.0f, kWeatherPanelShadowAlpha * eased));
	g->FillRect(x, kWeatherPanelY, kWeatherPanelWidth, panelHeight,
		glm::vec4(18.0f, 28.0f, 48.0f, kWeatherPanelBackgroundAlpha * eased));
	g->DrawRect(x, kWeatherPanelY, kWeatherPanelWidth, panelHeight,
		glm::vec4(111.0f, 151.0f, 196.0f, 180.0f * eased));
	g->FillRect(x, kWeatherPanelY, 5.0f, panelHeight,
		accentColor);
	if (mBoard->IsWeatherPanelInterferenceActive()) {
		const float textX = x + 18.0f;
		const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 185.0f * eased);
		const std::string line = u8"气象信号受干扰";
		g->DrawText(line, ResourceKeys::Fonts::FONT_FZCQ, kWeatherCurrentFontSize,
			shadow, textX + 1.0f, kWeatherPanelY + 26.0f);
		g->DrawText(line, ResourceKeys::Fonts::FONT_FZCQ, kWeatherCurrentFontSize,
			glm::vec4(255.0f, 181.0f, 88.0f, alpha),
			textX, kWeatherPanelY + 25.0f);
		return;
	}

	std::string currentLine = mBoard->IsStormyNightActive()
		? std::string(u8"当前天气：暴风雨")
		: std::string(u8"当前天气：")
			+ PrecipitationDisplayName(mBoard.get(), mBoard->GetRainIntensity());
	if (!mBoard->IsStormyNightActive()) {
		if (mBoard->SupportsStageFog()) {
			currentLine += std::string(u8" · ")
				+ FogWeatherDisplayName(mBoard->GetFogWeatherIntensity());
		}
		if (mBoard->HasTyphoon()) {
			currentLine += std::string(u8" · ")
				+ TyphoonStrengthDisplayName(mBoard->GetTyphoonStrength());
		}
	}
	const std::string forecastLine = WeatherForecastPanelText(mBoard.get());
	glm::vec4 forecastColor(166.0f, 178.0f, 196.0f, alpha);
	if (stormyNightForecast) {
		forecastColor = StormyNightTextColor(alpha);
	}
	else if (mBoard->IsWeatherForecastDisrupted()) {
		forecastColor = glm::vec4(255.0f, 181.0f, 88.0f, alpha);
	}
	else if (mBoard->HasWeatherForecast()) {
		forecastColor = RainIntensityTextColor(mBoard->GetForecastRainIntensity(), alpha);
	}

	const float textX = x + 18.0f;
	const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 185.0f * eased);
	g->DrawText(currentLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherCurrentFontSize,
		shadow, textX + 1.0f, kWeatherPanelY + 10.0f);
	g->DrawText(currentLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherCurrentFontSize,
		mBoard->IsStormyNightActive()
			? StormyNightTextColor(alpha)
			: RainIntensityTextColor(mBoard->GetRainIntensity(), alpha),
		textX, kWeatherPanelY + 9.0f);
	g->DrawText(forecastLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherForecastFontSize,
		shadow, textX + 1.0f, kWeatherPanelY + 42.0f);
	g->DrawText(forecastLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherForecastFontSize,
		forecastColor, textX, kWeatherPanelY + 41.0f);

	float detailLineY = kWeatherPanelY + 69.0f;
	const std::string coldWaveLine = ColdWavePanelText(mBoard.get());
	if (!coldWaveLine.empty()) {
		const glm::vec4 coldWaveColor = mBoard->IsColdWaveForecastDisruptionVisible()
			? glm::vec4(255.0f, 181.0f, 88.0f, alpha)
			: (mBoard->GetColdWavePhase() == ColdWavePhase::THAWING
				? glm::vec4(247.0f, 193.0f, 112.0f, alpha)
				: glm::vec4(143.0f, 220.0f, 255.0f, alpha));
		g->DrawText(coldWaveLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			shadow, textX + 1.0f, detailLineY + 1.0f);
		g->DrawText(coldWaveLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			coldWaveColor, textX, detailLineY);
		detailLineY += kWeatherPanelDetailLineHeight;
	}

	const std::string fogLine = stormyNightForecast
		? std::string() : FogForecastPanelText(mBoard.get());
	if (!fogLine.empty()) {
		glm::vec4 fogColor(151.0f, 181.0f, 199.0f, alpha);
		if (mBoard->IsFogWeatherForecastDisrupted()) {
			fogColor = glm::vec4(255.0f, 181.0f, 88.0f, alpha);
		}
		else if (mBoard->GetForecastFogWeatherIntensity() == FogWeatherIntensity::SMALL) {
			fogColor = glm::vec4(172.0f, 197.0f, 213.0f, alpha);
		}
		else if (mBoard->GetForecastFogWeatherIntensity() == FogWeatherIntensity::NORMAL) {
			fogColor = glm::vec4(190.0f, 208.0f, 222.0f, alpha);
		}
		else if (mBoard->GetForecastFogWeatherIntensity() == FogWeatherIntensity::DENSE) {
			fogColor = glm::vec4(218.0f, 226.0f, 238.0f, alpha);
		}
		g->DrawText(fogLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			shadow, textX + 1.0f, detailLineY + 1.0f);
		g->DrawText(fogLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			fogColor, textX, detailLineY);
		detailLineY += kWeatherPanelDetailLineHeight;
	}

	if (mBoard->SupportsRoofRunoff()) {
		const int chargePercent = static_cast<int>(std::lround(
			mBoard->GetRoofRunoffChargeRatio() * 100.0f));
		std::string runoffLine = std::string(u8"坡面径流：")
			+ std::to_string(chargePercent) + "%";
		if (mBoard->IsRoofRunoffWarning()) {
			const int seconds = std::max(0,
				static_cast<int>(std::ceil(mBoard->GetRoofRunoffPhaseTimer())));
			runoffLine = std::string(u8"坡面径流：")
				+ RoofRunoffRowsDisplayName(mBoard.get())
				+ u8"预警（" + std::to_string(seconds) + u8"秒）";
		}
		else if (mBoard->IsRoofRunoffFlowing()) {
			runoffLine = std::string(u8"坡面径流：")
				+ RoofRunoffRowsDisplayName(mBoard.get()) + u8"冲刷中";
		}
		const float warningPulse = (mBoard->IsRoofRunoffWarning()
			|| mBoard->IsRoofRunoffFlowing())
			? 0.78f + 0.22f * std::sin(static_cast<float>(mBoard->mBoardFrame) * 0.16f)
			: 1.0f;
		DrawWeatherAccumulationGauge(g, textX, detailLineY, runoffLine,
			mBoard->GetRoofRunoffChargeRatio(),
			glm::vec4(76.0f, 218.0f, 255.0f, alpha * warningPulse), eased,
			0.0f, glm::vec4(0.0f));
		detailLineY += kWeatherPanelGaugeLineHeight;
	}

	if (mBoard->SupportsNightRoofCharge()) {
		const int chargePercent = static_cast<int>(std::lround(
			mBoard->GetNightRoofChargeRatio() * 100.0f));
		std::string chargeLine = std::string(u8"屋顶雷荷：")
			+ std::to_string(chargePercent) + "%";
		if (mBoard->IsNightRoofChargeWarning()) {
			const int seconds = std::max(0,
				static_cast<int>(std::ceil(mBoard->GetNightRoofChargePhaseTimer())));
			chargeLine = std::string(u8"屋顶雷荷：")
				+ NightRoofChargeRowDisplayName(mBoard.get())
				+ u8"预警（" + std::to_string(seconds) + u8"秒）";
		}
		else if (mBoard->IsNightRoofChargeDischarging()) {
			chargeLine = std::string(u8"屋顶雷荷：")
				+ NightRoofChargeRowDisplayName(mBoard.get()) + u8"放电中";
		}
		const int overchargePercent = static_cast<int>(std::lround(
			mBoard->GetNightRoofOvercharge()));
		if (overchargePercent > 0) {
			chargeLine += std::string(u8"｜余电+")
				+ std::to_string(overchargePercent) + "%";
		}
		const float warningPulse = (mBoard->IsNightRoofChargeWarning()
			|| mBoard->IsNightRoofChargeDischarging())
			? 0.72f + 0.28f * std::sin(static_cast<float>(mBoard->mBoardFrame) * 0.20f)
			: 1.0f;
		DrawWeatherAccumulationGauge(g, textX, detailLineY, chargeLine,
			mBoard->GetNightRoofChargeRatio(),
			glm::vec4(192.0f, 136.0f, 255.0f, alpha * warningPulse), eased,
			mBoard->GetNightRoofOverchargeRatio(),
			glm::vec4(247.0f, 225.0f, 255.0f, alpha * warningPulse));
		detailLineY += kWeatherPanelGaugeLineHeight;

		const int executionLine = mBoard->GetNightRoofExecutionLine();
		if (executionLine > 0) {
			const std::string executionText = std::string(u8"处决线：")
				+ std::to_string(executionLine);
			const float executionPulse = 0.70f + 0.30f * std::sin(
				static_cast<float>(mBoard->mBoardFrame)
				* (mBoard->IsNightRoofHijackerFinalizing() ? 0.55f : 0.20f));
			const glm::vec4 executionColor(218.0f, 142.0f, 255.0f,
				alpha * executionPulse);
			g->DrawText(executionText, ResourceKeys::Fonts::FONT_FZCQ,
				kWeatherWindFontSize, shadow, textX + 1.0f, detailLineY + 1.0f);
			g->DrawText(executionText, ResourceKeys::Fonts::FONT_FZCQ,
				kWeatherWindFontSize, executionColor, textX, detailLineY);
			detailLineY += kWeatherPanelDetailLineHeight;
		}
	}

	if (mBoard->HasTyphoon()) {
		std::string windLine = std::string(u8"风向实况：")
			+ WindDirectionDisplayName(mBoard->GetWindDirection());
		if (mBoard->GetTyphoonStrength() == TyphoonStrength::TYPHOON) {
			// 普通台风只有持续风和僵尸移速影响，不显示从未发生过的植物位移阵风已经结束。
			windLine += u8"｜持续风（无阵风位移）";
		}
		else if (mBoard->IsTyphoonGustActive()) {
			windLine += u8"｜阵风中";
		}
		else if (mBoard->GetTyphoonGustsRemaining() > 0) {
			const int gustSeconds = std::max(0,
				static_cast<int>(std::ceil(mBoard->GetWindGustTimer())));
			windLine += std::string(u8"｜距阵风 ") + std::to_string(gustSeconds) + u8"秒";
		}
		else {
			windLine += u8"｜本阶段阵风结束";
		}
		const glm::vec4 windColor = (mBoard->IsTyphoonGustActive()
			|| mBoard->IsTyphoonGustWarning())
			? glm::vec4(255.0f, 179.0f, 92.0f, alpha)
			: glm::vec4(190.0f, 223.0f, 255.0f, alpha);
		g->DrawText(windLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			shadow, textX + 1.0f, detailLineY + 1.0f);
		g->DrawText(windLine, ResourceKeys::Fonts::FONT_FZCQ, kWeatherWindFontSize,
			windColor, textX, detailLineY);
	}
}

/** 在天气面板下方绘制错误揭晓提示；它只提示结果，不抢输入也不暂停游戏。 */
void GameScene::DrawWeatherForecastFailure(Graphics* g) const
{
	if (!g || mWeatherForecastFailureTimer <= 0.0f
		|| (mBoard && mBoard->IsWeatherPanelInterferenceActive())) return;
	const float failureY = mBoard
		? kWeatherPanelY + WeatherPanelHeight(mBoard.get()) + 6.0f
		: kForecastFailureY;

	const float elapsed = kForecastFailureDuration - mWeatherForecastFailureTimer;
	const float appear = std::clamp(elapsed / kForecastFailureAppearDuration, 0.0f, 1.0f);
	const float fade = std::clamp(mWeatherForecastFailureTimer
		/ kForecastFailureFadeDuration, 0.0f, 1.0f);
	const float visibility = std::min(appear, fade);
	const float eased = appear * appear * (3.0f - 2.0f * appear);
	const float x = -kForecastFailureWidth
		+ (kForecastFailureWidth + kWeatherPanelVisibleX) * eased;
	const float alpha = 255.0f * visibility;

	// 暖红色与上方天气面板的冷色区分开，玩家无需读完文字也能识别“预报失准”。
	g->FillRect(x + 3.0f, failureY + 3.0f,
		kForecastFailureWidth, kForecastFailureHeight,
		glm::vec4(0.0f, 0.0f, 0.0f, 88.0f * visibility));
	g->FillRect(x, failureY, kForecastFailureWidth, kForecastFailureHeight,
		glm::vec4(58.0f, 25.0f, 29.0f, 224.0f * visibility));
	g->DrawRect(x, failureY, kForecastFailureWidth, kForecastFailureHeight,
		glm::vec4(255.0f, 135.0f, 121.0f, 205.0f * visibility));
	g->FillRect(x, failureY, 5.0f, kForecastFailureHeight,
		glm::vec4(255.0f, 105.0f, 91.0f, alpha));

	const std::string title = u8"天气预报失败！";
	const std::string detail = std::string(u8"预报：")
		+ ForecastOutcomeDisplayName(
			mFailedForecastRainIntensity, mFailedForecastTyphoonStrength)
		+ u8"  →  实际：" + ForecastOutcomeDisplayName(
			mActualForecastRainIntensity, mActualForecastTyphoonStrength);
	const float textX = x + 18.0f;
	const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 185.0f * visibility);
	g->DrawText(title, ResourceKeys::Fonts::FONT_FZCQ, kForecastFailureTitleFontSize,
		shadow, textX + 1.0f, failureY + 7.0f);
	g->DrawText(title, ResourceKeys::Fonts::FONT_FZCQ, kForecastFailureTitleFontSize,
		glm::vec4(255.0f, 181.0f, 169.0f, alpha), textX, failureY + 6.0f);
	g->DrawText(detail, ResourceKeys::Fonts::FONT_FZCQ, kForecastFailureDetailFontSize,
		shadow, textX + 1.0f, failureY + 34.0f);
	g->DrawText(detail, ResourceKeys::Fonts::FONT_FZCQ, kForecastFailureDetailFontSize,
		glm::vec4(242.0f, 229.0f, 220.0f, alpha), textX, failureY + 33.0f);
}

RoofMarshalBossHealthBarState GameScene::GetRoofMarshalBossHealthBarState() const
{
	RoofMarshalBossHealthBarState state;
	state.width = kRoofMarshalBossBarWidth;
	state.height = kRoofMarshalBossBarHeight;
	state.x = (static_cast<float>(SCENE_WIDTH) - state.width) * 0.5f;
	state.y = kRoofMarshalBossBarY;
	if (!mBoard || mBoard->mBoardState != BoardState::GAME) return state;

	const auto marshal = mBoard->mEntityRegistry.GetFirstActiveRoofMarshal();
	if (!marshal || marshal->mBodyHealth <= 0 || marshal->mBodyMaxHealth <= 0) {
		return state;
	}

	state.visible = true;
	state.currentHealth = std::max(0, marshal->mBodyHealth);
	state.maxHealth = marshal->mBodyMaxHealth;
	state.highThreatThreshold = marshal->GetHighThreatHealthThreshold();
	state.desperateThreshold = marshal->GetDesperateHealthThreshold();
	state.fillRatio = std::clamp(
		static_cast<float>(state.currentHealth) / static_cast<float>(state.maxHealth),
		0.0f, 1.0f);
	return state;
}

void GameScene::DrawRoofMarshalBossHealthBar(Graphics* g) const
{
	if (!g) return;
	const RoofMarshalBossHealthBarState state = GetRoofMarshalBossHealthBarState();
	if (!state.visible) return;

	const float plateX = state.x - kRoofMarshalBossPlatePaddingX;
	const float plateY = state.y - kRoofMarshalBossPlateTop;
	const float plateWidth = state.width + kRoofMarshalBossPlatePaddingX * 2.0f;
	const float plateHeight = kRoofMarshalBossPlateTop + state.height
		+ kRoofMarshalBossPlateBottom;
	const float wingY = plateY + 18.0f;
	const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 190.0f);
	const glm::vec4 darkMetal(18.0f, 12.0f, 15.0f, 246.0f);
	const glm::vec4 blackMetal(7.0f, 5.0f, 7.0f, 252.0f);
	const glm::vec4 darkGold(101.0f, 61.0f, 16.0f, 255.0f);
	const glm::vec4 gold(214.0f, 158.0f, 56.0f, 255.0f);
	const glm::vec4 brightGold(255.0f, 221.0f, 119.0f, 255.0f);

	// 两侧阶梯肩甲与双层金边把血条做成独立的首领铭牌，不依赖额外位图资源。
	g->FillRect(plateX - 30.0f, wingY + 5.0f, plateWidth + 60.0f, 20.0f, shadow);
	g->FillRect(plateX - 28.0f, wingY + 3.0f, 28.0f, 25.0f, darkGold);
	g->FillRect(plateX - 38.0f, wingY + 8.0f, 10.0f, 15.0f, gold);
	g->FillRect(plateX + plateWidth, wingY + 3.0f, 28.0f, 25.0f, darkGold);
	g->FillRect(plateX + plateWidth + 28.0f, wingY + 8.0f, 10.0f, 15.0f, gold);
	g->FillRect(plateX + 4.0f, plateY + 5.0f, plateWidth, plateHeight, shadow);
	g->FillRect(plateX, plateY, plateWidth, plateHeight, darkGold);
	g->FillRect(plateX + 2.0f, plateY + 2.0f,
		plateWidth - 4.0f, plateHeight - 4.0f, gold);
	g->FillRect(plateX + 5.0f, plateY + 5.0f,
		plateWidth - 10.0f, plateHeight - 10.0f, darkMetal);
	g->DrawRect(plateX + 7.0f, plateY + 7.0f,
		plateWidth - 14.0f, plateHeight - 14.0f, brightGold);

	const float fillX = state.x + 3.0f;
	const float fillY = state.y + 3.0f;
	const float fillWidth = state.width - 6.0f;
	const float fillHeight = state.height - 6.0f;
	g->FillRect(state.x - 4.0f, state.y - 4.0f,
		state.width + 8.0f, state.height + 8.0f, blackMetal);
	g->FillRect(state.x - 2.0f, state.y - 2.0f,
		state.width + 4.0f, state.height + 4.0f, gold);
	g->FillRect(state.x, state.y, state.width, state.height,
		glm::vec4(25.0f, 3.0f, 6.0f, 255.0f));

	const float desperateRatio = std::clamp(
		static_cast<float>(state.desperateThreshold) / static_cast<float>(state.maxHealth),
		0.0f, 1.0f);
	const float highThreatRatio = std::clamp(
		static_cast<float>(state.highThreatThreshold) / static_cast<float>(state.maxHealth),
		0.0f, 1.0f);
	// 空槽本身也分成三段；血量掉过界后，下一阶段区域仍保持暗红轮廓可读。
	g->FillRect(fillX, fillY, fillWidth * desperateRatio, fillHeight,
		glm::vec4(76.0f, 10.0f, 7.0f, 255.0f));
	g->FillRect(fillX + fillWidth * desperateRatio, fillY,
		fillWidth * std::max(0.0f, highThreatRatio - desperateRatio), fillHeight,
		glm::vec4(54.0f, 7.0f, 9.0f, 255.0f));

	const bool desperate = state.currentHealth < state.desperateThreshold;
	const float pulse = desperate && mBoard
		? 0.5f + 0.5f * std::sin(static_cast<float>(mBoard->mBoardFrame)
			* kRoofMarshalBossDesperatePulseSpeed)
		: 0.0f;
	const float currentFillWidth = fillWidth * state.fillRatio;
	if (currentFillWidth > 0.0f) {
		g->FillRect(fillX, fillY, currentFillWidth, fillHeight,
			glm::vec4(145.0f + 55.0f * pulse, 8.0f, 16.0f, 255.0f));
		g->FillRect(fillX, fillY, currentFillWidth, fillHeight * 0.48f,
			glm::vec4(232.0f + 23.0f * pulse,
				32.0f + 42.0f * pulse, 37.0f, 255.0f));
		g->FillRect(fillX, fillY, currentFillWidth, 2.0f,
			glm::vec4(255.0f, 119.0f + 75.0f * pulse, 104.0f, 230.0f));
	}

	auto drawPhaseMarker = [&](float ratio, const std::string& label) {
		if (ratio <= 0.0f || ratio >= 1.0f) return;
		const float markerX = fillX + fillWidth * ratio;
		const float labelWidth = MeasureUiTextWidth(
			label, kRoofMarshalBossPhaseFontSize);
		const float labelY = state.y + state.height + 1.0f;
		g->FillRect(markerX - 3.0f, state.y - 6.0f,
			6.0f, state.height + 12.0f, blackMetal);
		g->FillRect(markerX - 1.5f, state.y - 7.0f,
			3.0f, state.height + 14.0f, brightGold);
		g->FillRect(markerX - 4.0f, state.y - 9.0f, 8.0f, 4.0f, gold);
		g->FillRect(markerX - labelWidth * 0.5f - 6.0f, labelY - 1.0f,
			labelWidth + 12.0f, 14.0f, blackMetal);
		g->DrawRect(markerX - labelWidth * 0.5f - 6.0f, labelY - 1.0f,
			labelWidth + 12.0f, 14.0f, gold);
		DrawCenteredUiText(g, label, kRoofMarshalBossPhaseFontSize,
			shadow, markerX + 1.0f, labelY + 1.0f);
		DrawCenteredUiText(g, label, kRoofMarshalBossPhaseFontSize,
			brightGold, markerX, labelY);
	};
	drawPhaseMarker(highThreatRatio,
		std::string(u8"精锐 ") + std::to_string(state.highThreatThreshold));
	drawPhaseMarker(desperateRatio,
		std::string(u8"狂暴 ") + std::to_string(state.desperateThreshold));

	const float centerX = state.x + state.width * 0.5f;
	DrawCenteredUiText(g, u8"—  屋脊督军  —", kRoofMarshalBossTitleFontSize,
		shadow, centerX + 2.0f, plateY + 3.0f);
	DrawCenteredUiText(g, u8"—  屋脊督军  —", kRoofMarshalBossTitleFontSize,
		brightGold, centerX, plateY + 1.0f);
	const std::string healthText = std::to_string(state.currentHealth)
		+ " / " + std::to_string(state.maxHealth);
	DrawCenteredUiText(g, healthText, kRoofMarshalBossHealthFontSize,
		shadow, centerX + 1.0f, state.y + 1.0f);
	DrawCenteredUiText(g, healthText, kRoofMarshalBossHealthFontSize,
		glm::vec4(255.0f, 245.0f, 221.0f, 255.0f), centerX, state.y);
}

void GameScene::BuildDrawCommands()
{
	Scene::BuildDrawCommands();

	Background background = ResolveEnterBackground(
		std::stoi(SceneManager::GetInstance().GetGlobalData("EnterLevel")));

	if (background == Background::GROUND_DAY) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_DAY,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::GROUND_NIGHT) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHT,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::WATER_POOL) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_POOL,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::NIGHT_WATER_POOL) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHTPOOL,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::ROOF) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_ROOF,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::NIGHT_ROOF) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHTROOF,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::WINTER_GARDEN) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_WINTERGARDEN,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::GLOOMCRYSTAL_MINE) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_GLOOMCRYSTAL_MINE,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}
	else if (background == Background::POLAR_NIGHT_SNOWFIELD) {
		AddTexture(ResourceKeys::Textures::IMAGE_BACKGROUND_POLAR_NIGHT,
			mStartX, mBackgroundY, 1.0f, 1.0f, LAYER_BACKGROUND, false);
	}

	if ((background == Background::ROOF || background == Background::NIGHT_ROOF) && mBoard) {
		RegisterDrawCommand("RoofRainBackground",
			[this](Graphics* g) { DrawRoofRainBackground(g); },
			LAYER_BACKGROUND + 1);
	}

	if (background == Background::WATER_POOL
		|| background == Background::NIGHT_WATER_POOL) {
		const bool isNight = background == Background::NIGHT_WATER_POOL;
		RegisterDrawCommand("PoolEffect",
			[this, isNight](Graphics* g) {
				auto& resources = ResourceManager::GetInstance();
				const Texture* base = resources.GetTexture(
					isNight ? ResourceKeys::Textures::IMAGE_POOL_BASE_NIGHT
						: ResourceKeys::Textures::IMAGE_POOL_BASE, false);
				const Texture* shading = resources.GetTexture(
					isNight ? ResourceKeys::Textures::IMAGE_POOL_SHADING_NIGHT
						: ResourceKeys::Textures::IMAGE_POOL_SHADING, false);
				const Texture* caustic = resources.GetTexture(
					ResourceKeys::Textures::IMAGE_POOL_CAUSTIC_EFFECT, false);
				g->DrawPoolEffect(base, shading, caustic,
					kPoolEffectOffsetX, kPoolEffectOffsetY,
					mPoolEffectCounter, isNight);
			},
			LAYER_BACKGROUND + 1);
	}

	if (mBoard) {
		if (background == Background::WINTER_GARDEN) {
			RegisterDrawCommand("WinterFrost",
				[this](Graphics* g) { mBoard->DrawWinterFrost(g); },
				LAYER_BACKGROUND + 1);
		}
		if (background == Background::POLAR_NIGHT_SNOWFIELD) {
			RegisterDrawCommand("PolarNightGround",
				[this](Graphics* g) { mBoard->DrawPolarNightGround(g); },
				LAYER_BACKGROUND + 1);
		}
		if (background == Background::GLOOMCRYSTAL_MINE) {
			RegisterDrawCommand("MineGround", [this](Graphics* g) { mBoard->DrawMineGround(g); }, LAYER_BACKGROUND + 3);
			RegisterDrawCommand("MineUI", [this](Graphics* g) { mBoard->DrawMineUI(g); }, LAYER_UI + 60000);
		}
		RegisterDrawCommand("IceTrails",
			[this](Graphics* g) { mBoard->DrawIceTrails(g); },
			LAYER_BACKGROUND + 2);
	}

	// 开发者模式常驻角标（左上角小字；暂停刷怪时附加状态）
	if (GameAPP::mDevelopMode) {
		RegisterDrawCommand("DevModeBadge",
			[](Graphics* g) {
				std::string badge = u8"开发者模式";
				if (GameAPP::mDevSpawnPaused) badge += u8"（刷怪已暂停）";
				auto& gameApp = GameAPP::GetInstance();
				gameApp.DrawText(badge, Vector(4, 4), { 0,0,0,255 },
					ResourceKeys::Fonts::FONT_FZCQ, 14);
				gameApp.DrawText(badge, Vector(5, 5), { 255, 90, 90, 255 },
					ResourceKeys::Fonts::FONT_FZCQ, 14);
			},
			LAYER_UI + 100000);
	}

	if (mBoard) {
		RegisterDrawCommand("PolarNightWhiteout",
			[this](Graphics* g) { DrawPolarNightWhiteout(g); },
			LAYER_UI + 400);
		RegisterDrawCommand("WinterThermometer",
			[this](Graphics* g) { DrawWinterThermometer(g); },
			LAYER_UI + 450);
		RegisterDrawCommand("WeatherPanel",
			[this](Graphics* g) { DrawWeatherPanel(g); },
			LAYER_UI + 500);
		RegisterDrawCommand("WeatherForecastFailure",
			[this](Graphics* g) { DrawWeatherForecastFailure(g); },
			LAYER_UI + 600);
		RegisterDrawCommand("PolarNightInstruments",
			[this](Graphics* g) { DrawPolarNightInstruments(g); },
			LAYER_UI + 700);
		RegisterDrawCommand("PlanternGearMenu",
			[this](Graphics* g) {
				if (mCardSlotManager) {
					mCardSlotManager->DrawPlanternGearMenu(g);
					mCardSlotManager->DrawRelocationHint(g);
				}
			},
			kPlanternGearMenuRenderOrder);
		RegisterDrawCommand("RoofMarshalBossHealthBar",
			[this](Graphics* g) { DrawRoofMarshalBossHealthBar(g); },
			LAYER_UI + 800);

		RegisterDrawCommand("Prompts",
			[this](Graphics* g) { DrawPrompts(g); },
			LAYER_UI + 1000);
		RegisterDrawCommand("SpacePauseLabel",
			[this](Graphics* g) { DrawSpacePauseLabel(g); },
			LAYER_UI + 2000);

		// 全屏白闪（寒冰菇）：盖过场景与 Prompt，但在开发者角标（+100000）之下
		RegisterDrawCommand("ScreenFlash",
			[this](Graphics* g) {
				if (mScreenFlashTimer <= 0.0f) return;
				// 峰值可由调用方控制：寒冰菇沿用 200，大雨闪电使用更柔和的短闪。
				const float t = mScreenFlashTimer / mScreenFlashDuration;
				glm::vec4 color(255.0f, 255.0f, 255.0f, mScreenFlashPeakAlpha * t);
				g->FillRect(0.0f, 0.0f,
					static_cast<float>(SCENE_WIDTH), static_cast<float>(SCENE_HEIGHT), color);
			},
			LAYER_UI + 10000);

		// 读档恢复时，board 已处于 GAME 状态，需在此重新注册 UI 文字命令
		if (mBoard->mBoardState == BoardState::GAME) {
			RegisterDrawCommand("ZombieNumber",
				[this](Graphics* g) {
					auto& gameApp = GameAPP::GetInstance();
					gameApp.DrawText(u8"当前僵尸数量: " + std::to_string(mBoard->mZombieNumber),
						Vector(3, 569), { 0,0,0,255 }, ResourceKeys::Fonts::FONT_FZCQ, 24);
					gameApp.DrawText(u8"当前僵尸数量: " + std::to_string(mBoard->mZombieNumber),
						Vector(5, 570), { 223,186,98,255 }, ResourceKeys::Fonts::FONT_FZCQ, 24);
				},
				LAYER_UI);

			RegisterDrawCommand("LevelName",
				[this](Graphics* g) {
					if (!mBoard || mBoard->mBoardState != BoardState::GAME) return;  // 选卡阶段隐藏
					DrawLevelName(GameAPP::GetInstance(), mBoard->mLevelName, mBoard->mIsSurvival || MiniGame::IsMiniGame(mBoard->mLevel));
				},
				LAYER_UI);

			RegisterDrawCommand("Difficulty",
				[this](Graphics* g) {
					if (!mBoard || mBoard->mBoardState != BoardState::GAME) return;  // 选卡阶段隐藏
					auto& gameApp = GameAPP::GetInstance();
					gameApp.DrawText("难度: " + std::to_string(gameApp.Difficulty),
						Vector(1030, 575), { 0,0,0,255 }, ResourceKeys::Fonts::FONT_FZCQ, 21);
					gameApp.DrawText("难度: " + std::to_string(gameApp.Difficulty),
						Vector(1032, 576), { 223,186,98,255 }, ResourceKeys::Fonts::FONT_FZCQ, 21);
				},
				LAYER_UI);
			ShowSunCount();
		}
	}

	RegisterDrawCommand("MiniGameRules", [this](Graphics*) {
		if (!mBoard || !MiniGame::IsMiniGame(mBoard->mLevel)
			|| mBoard->mBoardState != BoardState::GAME) return;
		const std::string text = mBoard->mCurrentWave == 0
			? u8"布阵剩余 " + std::to_string(static_cast<int>(std::ceil(mBoard->mZombieCountDown))) + u8" 秒 · 本局不再获得阳光"
			: u8"守住十波 · 阳光用完就没有补给了！";
		auto& app = GameAPP::GetInstance();
		app.DrawText(text, Vector(260, 82), {0, 0, 0, 255}, ResourceKeys::Fonts::FONT_FZJZ, 20);
		app.DrawText(text, Vector(259, 81), {255, 235, 160, 255}, ResourceKeys::Fonts::FONT_FZJZ, 20);
	}, LAYER_UI + 100);

	RegisterDrawCommand("CrazyDaveDialog",
		[this](Graphics* g) {
			if (mCrazyDaveDialog) mCrazyDaveDialog->Draw(g);
		},
		LAYER_UI + 90000);
}

void GameScene::OnEnter() {
	Scene::OnEnter();
	RestoreDevPanelSelection();

	int enterLevel = std::stoi(SceneManager::GetInstance().GetGlobalData("EnterLevel"));

	mBoard = std::make_unique<Board>(this, ResolveEnterBackground(enterLevel), enterLevel);
	mCardSlotManager = std::make_unique<CardSlotManager>(mBoard.get());
	mBoard->BindCardSlotManager(mCardSlotManager.get());
	mCardSlotManager->Start();

	mGameProgress = GameObjectManager::GetInstance().CreateGameObjectImmediate<GameProgress>(
		LAYER_UI, mBoard.get());
	mGameProgress->SetActive(false);

	auto button = mUIManager.CreateButton(Vector(990, -5), Vector(125 * 0.9f, 52 * 0.9f));
	mMainMenuButton = button;
	button->SetText("主菜单");
	button->SetAsCheckbox(false);
	button->SetTextColor(glm::vec4{ 53, 191, 61, 255 });
	button->SetHoverTextColor(glm::vec4{ 53, 240, 61, 255 });
	button->SetImageKeys(ResourceKeys::Textures::IMAGE_BUTTONSMALL, ResourceKeys::Textures::IMAGE_BUTTONSMALL,
		ResourceKeys::Textures::IMAGE_BUTTONSMALL, ResourceKeys::Textures::IMAGE_BUTTONSMALL);
	button->SetClickCallBack([this](bool) {
		this->OpenMenu();
		});

	auto button2 = mUIManager.CreateButton(Vector(990, 45), Vector(125 * 0.9f, 52 * 0.9f));
	mSpeedSettingsButton = button2;
	auto formatSpeedText = [](float scale) {
		char buf[16];
		std::snprintf(buf, sizeof(buf), "x%.1f", scale);
		return std::string(buf);
		};
	button2->SetText(formatSpeedText(DeltaTime::GetSelectedTimeScale()));
	button2->SetAsCheckbox(false);
	button2->SetTextColor(glm::vec4{ 53, 191, 61, 255 });
	button2->SetHoverTextColor(glm::vec4{ 53, 240, 61, 255 });
	button2->SetImageKeys(ResourceKeys::Textures::IMAGE_BUTTONSMALL, ResourceKeys::Textures::IMAGE_BUTTONSMALL,
		ResourceKeys::Textures::IMAGE_BUTTONSMALL, ResourceKeys::Textures::IMAGE_BUTTONSMALL);
	button2->SetClickCallBack([this, formatSpeedText](bool) {
		// 暂停时只切换待恢复倍速；实际 timeScale 仍为 0，游戏不会因点击按钮而启动。
		float current = DeltaTime::GetSelectedTimeScale();
		float next = 1.0f;
		if (current == 1.0f)      next = 2.0f;
		else if (current == 2.0f) next = 0.5f;
		else                       next = 1.0f;
		DeltaTime::SetSelectedTimeScale(next);
		if (auto btn = mSpeedSettingsButton.lock()) {
			btn->SetText(formatSpeedText(next));
		}
		});

	// 生存模式专属：右上角第三个按钮「词条」，点开已选词条查看面板（普通关卡不创建）
	if (mBoard->mIsSurvival) {
		auto button3 = mUIManager.CreateButton(Vector(990, 95), Vector(125 * 0.9f, 52 * 0.9f));
		mPerkViewButton = button3;
		button3->SetText(u8"词条");
		button3->SetAsCheckbox(false);
		button3->SetTextColor(glm::vec4{ 53, 191, 61, 255 });
		button3->SetHoverTextColor(glm::vec4{ 53, 240, 61, 255 });
		button3->SetImageKeys(ResourceKeys::Textures::IMAGE_BUTTONSMALL, ResourceKeys::Textures::IMAGE_BUTTONSMALL,
			ResourceKeys::Textures::IMAGE_BUTTONSMALL, ResourceKeys::Textures::IMAGE_BUTTONSMALL);
		button3->SetClickCallBack([this](bool) { this->OpenPerkView(); });
	}

	// 原版在选卡前铺好屋顶初始花盆；只给没有关卡存档的新局生成，避免读档重复叠加。
	mResumeMenuAfterAlmanac = GameAPP::GetInstance().mGameInfoSaver.IsAlmanacReturnQueued();
	GameAPP::GetInstance().mGameInfoSaver.LoadLevelData(
		mBoard.get(), mCardSlotManager.get());
	if (mResumeMenuAfterAlmanac) DeltaTime::SetPaused(true);
	// AutoTest 的无存档路径按契约返回 true，但不会置 mIsLoadSave；以读档生命周期标记判定新局。
	if (!mBoard->IsLoadRestoreActive()) {
		mBoard->InitializeStartingFlowerPots();
	}
	// 地形在 Board 构造与读档后已经确定；选卡/镜头演出期间后台解析完整 MO3 分轨。
	mBoard->PrepareBackgroundMusic();

	if (mBoard->mBoardState == BoardState::GAME) {
		// 跳过选卡和开场动画，直接进入游戏
		GameAPP::GetInstance().GetGraphics().SetCameraPosition(0, 0);
		mCurrentStage = IntroStage::FINISH;
		mCurrectSceneX = mGameStartX;
		mSeedbankAdded = true;

		// 生存第 1 轮的 committed-pan 退出存档（点完"摇滚"但过场途中退出）里没有小推车：
		// 那份 GAME 快照存盘时 StartGame() 从未运行过，小推车尚未生成。清读档标记，
		// 让下面的 StartGame 正常初始化小推车。判别条件无歧义：第 0 波时小推车不可能被用掉
		// （触发小推车需僵尸、僵尸需波次≥1），故"第1轮+第0波+无小推车" 只可能是 committed-pan。
		// 第 2 轮起小推车从第 1 轮保留、已在存档中，不进此分支（mIsLoadSave 维持 true，绝不重建）。
		if (mBoard->mIsSurvival && mBoard->mSurvivalRound == 1
			&& mBoard->mCurrentWave == 0
			&& mBoard->mEntityRegistry.GetAllMowerIDs().empty()) {
			mBoard->CompleteLoadRestore();
		}

		mBoard->StartGame();

		AddTexture(ResourceKeys::Textures::IMAGE_SEEDBANK_LONG,
			kSeedBankX, kSeedBankRestY, SeedBankScaleX(), kSeedBankScaleY,
			LAYER_UI, true);

		mBoard->CompleteLoadRestore();
	}
	else {
		AudioSystem::PlayMusic(ResourceKeys::Music::MUSIC_CHOOSEYOURSEEDS, -1);
		// 生存第 1 轮的轮间存档（首次选卡，StartGame 从未运行过）里没有小推车数据，
		// 需清掉读档标记，让随后选完卡的 StartGame 正常初始化小推车。
		// 第 2 轮起的存档已含小推车（且可能因第 1 轮被用掉而合法地变少甚至为空），
		// 保持 mIsLoadSave=true，由存档恢复，绝不重新生成。
		if (mBoard->mIsSurvival && mBoard->mSurvivalRound == 1) {
			mBoard->CompleteLoadRestore();
		}
	}

	// 戴夫只在新开冒险关的选卡演出之前闲聊；读档、生存模式和常规 AutoTest 均不自动打断。
	if (mBoard->mBoardState == BoardState::CHOOSE_CARD
		&& !mBoard->mIsSurvival && !GameAPP::mAutoTestMode) {
		TryStartCrazyDaveDialog(false);
	}
}

void GameScene::OnExit() {
	// 中途退出不触发完成回调；玩家下次进入仍能看到尚未读完的闲聊。
	mCrazyDaveDialog.reset();
	auto& gameApp = GameAPP::GetInstance();
	// 生存模式：玩家已点"一起摇滚吧"进入回移过场（READY_SET_PLANT）、但 mBoardState 要等过场结束
	// 才翻成 GAME 的这 3 秒窗口里退出——此刻卡已提交、本轮在即，逻辑上已"进入游戏"。
	// 按 GAME 持久化，避免重进被误判为"仍在选卡"而重播选卡（卡槽还原committed卡 → 可重复选卡的错乱）。
	// 仅 survival 分支：普通模式过场期 mBoardState 同为 CHOOSE_CARD，但 mIsSurvival=false 不进此分支，行为零改动。
	if (mBoard->mIsSurvival && mBoard->mBoardState == BoardState::CHOOSE_CARD
		&& mCurrentStage == IntroStage::READY_SET_PLANT) {
		mBoard->mBoardState = BoardState::GAME;
	}
	const bool saveState = (mBoard->mBoardState == BoardState::GAME) ||
		(mBoard->mIsSurvival && mBoard->mBoardState == BoardState::CHOOSE_CARD);
	if (saveState && !mReadyToRestart) {
		gameApp.mGameInfoSaver.SaveLevelData
		(mBoard.get(), mCardSlotManager.get());
	}
	gameApp.mGameInfoSaver.SavePlayerInfo();

	mBoard->BindCardSlotManager(nullptr);
	mCardSlotManager.reset();
	Scene::OnExit();
	mShovelUI = nullptr;
	mBoard.reset();
	mSpeedSettingsButton.reset();
	mMainMenuButton.reset();
	mPerkViewButton.reset();
	mGameProgress = nullptr;
	mChooseCardUI = nullptr;
}

void GameScene::OpenMenu()
{
	if (mOpenMenu) return;
	if (mOpenRestartMenu || mOpenQuitMenu) return;
	if (mSurvivalPerkSelectActive) return;   // 选词条模态期间禁止打开暂停菜单（否则会在框下解除暂停）
	if (mPerkViewActive) return;             // 词条查看面板打开期间禁止叠开暂停菜单
	if (mDevPanelActive) return;             // 开发者面板拥有当前暂停，禁止叠开普通菜单

	// 从轻量暂停切换到完整菜单时保持全程冻结，只把暂停 UI 的所有权交给菜单。
	mSpacePauseActive = false;
	SyncSpacePauseInputPolicy();
	mOpenMenu = true;
	DeltaTime::SetPaused(true);
	auto& gameApp = GameAPP::GetInstance();
	const glm::vec4 labelColor{ 107, 109, 144, 255 };
	mMenu = GameMessageBox::Builder(Vector(SCENE_WIDTH / 2 + 50, SCENE_HEIGHT / 2 - 80.0f))
		.Background(ResourceKeys::Textures::IMAGE_OPTIONS_MENUBACK)
		.ControlFont(ResourceKeys::Fonts::FONT_FZJT)
		.Button(u8"返回游戏", Vector(400, 430), Vector(360, 100), 40, [this]() {
			mOpenMenu = false;
			DeltaTime::SetPaused(false);
		}, ResourceKeys::Textures::IMAGE_OPTIONS_BACKTOGAMEBUTTON0)
		.Button(u8"重新开始", Vector(485, 330), Vector(213 * 0.9f, 50 * 0.9f), 21,
			[this]() { this->OpenRestartMenu(); }, ResourceKeys::Textures::IMAGE_BUTTONBIG, false)
		.Button(MiniGame::IsMiniGame(mBoard->mLevel) ? u8"小游戏选关" : u8"主菜单", Vector(485, 371), Vector(213 * 0.9f, 50 * 0.9f), 21,
			[this]() { this->OpenQuitMenu(); }, ResourceKeys::Textures::IMAGE_BUTTONBIG, false)
		.Button(u8"查看图鉴", Vector(485, 289), Vector(213 * 0.9f, 50 * 0.9f), 21, [this]() {
			this->mLendToAlmanacScene = true;
		}, ResourceKeys::Textures::IMAGE_BUTTONBIG, false)
		.Checkbox(Vector(455, 250), Vector(42, 39), []() {
			auto& app = GameAPP::GetInstance();
			app.mShowPlantHP = !app.mShowPlantHP;
		}, gameApp.mShowPlantHP)
		.Checkbox(Vector(590, 250), Vector(42, 39), []() {
			auto& app = GameAPP::GetInstance();
			app.mShowZombieHP = !app.mShowZombieHP;
		}, gameApp.mShowZombieHP)
		.Slider(Vector(530, 175), Vector(135, 10), 0.0f, 1.0f, AudioSystem::GetMusicVolume(),
			[](float v) { AudioSystem::SetMusicVolume(v); })
		.Slider(Vector(530, 200), Vector(135, 10), 0.0f, 1.0f, AudioSystem::GetSoundVolume(),
			[](float v) { AudioSystem::SetSoundVolume(v); })
		.Slider(Vector(530, 225), Vector(135, 10), 1, 4,
			static_cast<float>(GameAPP::GetInstance().Difficulty),
			[](float v) { GameAPP::GetInstance().Difficulty = static_cast<int>(v); }, true)
		.Text(Vector(480, 165), 22, u8"音乐", labelColor)
		.Text(Vector(480, 190), 22, u8"音效", labelColor)
		.Text(Vector(480, 215), 22, u8"难度", labelColor)
		.Text(Vector(498, 256), 14, u8"植物血量显示", labelColor)
		.Text(Vector(634, 256), 14, u8"僵尸血量显示", labelColor)
		.Show();
}

/** 空格键只拥有轻量暂停；完整菜单和其他暂停模态打开时不会被它意外解除。 */
void GameScene::ToggleSpacePause()
{
	if (!mBoard || mBoard->mBoardState != BoardState::GAME) return;
	if (mOpenMenu || mOpenRestartMenu || mOpenQuitMenu
		|| mSurvivalPerkSelectActive || mPerkViewActive || mDevPanelActive) return;

	if (!mSpacePauseActive)
	{
		AudioSystem::PlaySound("SOUND_PAUSE", 0.6f);
	}

	mSpacePauseActive = !mSpacePauseActive;
	SyncSpacePauseInputPolicy();
	DeltaTime::SetPaused(mSpacePauseActive);
}

/** 普通暂停只保留鼠标预览；开启高级暂停时沿用既有的完整种植交互。 */
void GameScene::SyncSpacePauseInputPolicy()
{
	if (!mCardSlotManager) return;
	const bool blockGameplayInput = mSpacePauseActive
		&& !GameAPP::GetInstance().mAdvancedPauseEnabled;
	mCardSlotManager->SetPauseGameplayInputBlocked(blockGameplayInput);
}

void GameScene::OpenRestartMenu()
{
	if (this->mOpenRestartMenu) return;
	this->mOpenRestartMenu = true;

	GameMessageBox::Builder(Vector(SCENE_WIDTH / 2, SCENE_HEIGHT / 2))
		.Title(u8"重新开始游戏？")
		.Message(u8"你想要重新开始这一关吗？")
		.Scale(kCompactDialogScale)
		.Button(u8"重来", Vector(380, 380), Vector(125 * 0.8f, 52 * 0.8f), 14, [this]() {
			this->mReadyToRestart = true;
			this->mOpenRestartMenu = false;
			this->mOpenMenu = false;
			DeltaTime::SetPaused(false);
		})
		.Button(u8"取消", Vector(560, 380), Vector(125 * 0.8f, 52 * 0.8f), 14, [this]() {
			// 只关闭确认层，父菜单继续拥有暂停。
			this->mOpenRestartMenu = false;
		})
		.Show();
}

void GameScene::OpenQuitMenu()
{
	if (this->mOpenQuitMenu) return;
	this->mOpenQuitMenu = true;

	GameMessageBox::Builder(Vector(SCENE_WIDTH / 2, SCENE_HEIGHT / 2))
		.Title(u8"退出当前游戏？")
		.Message(MiniGame::IsMiniGame(mBoard->mLevel)
			? u8"你想要返回小游戏选关吗？" : u8"你想要返回主菜单吗？")
		.Scale(kCompactDialogScale)
		.Button(u8"退出", Vector(380, 380), Vector(125 * 0.8f, 52 * 0.8f), 14, [this]() {
			this->mReadyToBackMenu = true;
			this->mOpenQuitMenu = false;
			this->mOpenMenu = false;
			DeltaTime::SetPaused(false);
		})
		.Button(u8"取消", Vector(560, 380), Vector(125 * 0.8f, 52 * 0.8f), 14, [this]() {
			// 只关闭确认层，父菜单继续拥有暂停。
			this->mOpenQuitMenu = false;
		})
		.Show();
}

void GameScene::Update() {
	// OnEnter 时新场景尚未挂到 SceneManager；等首次 Update 再注册返回后的暂停菜单。
	if (mResumeMenuAfterAlmanac) {
		mResumeMenuAfterAlmanac = false;
		OpenMenu();
	}
	// 戴夫闲聊独占本帧输入和逻辑更新；结束输入不会穿透到选卡、暂停或战场。
	if (mCrazyDaveDialog && mCrazyDaveDialog->IsActive()) {
		mCrazyDaveDialog->Update();
		return;
	}
	Scene::Update();

	// 水面沿用原版逐 Update 计数器，保持与游戏倍速和天气 DeltaTime 解耦；全局暂停仍冻结画面。
	if (mBoard && mBoard->IsPoolBackground() && !DeltaTime::IsPaused()) {
		++mPoolEffectCounter;
	}

	// 暂停时显示待恢复倍速，非暂停时显示实际倍速；两种状态都可由同一按钮更新。
	if (auto btn = mSpeedSettingsButton.lock()) {
		char buf[16];
		std::snprintf(buf, sizeof(buf), "x%.1f", DeltaTime::GetSelectedTimeScale());
		btn->SetText(buf);
	}

	if (mBoard && !mReadyToRestart && !mReadyToBackMenu)
	{
		{
			PROFILE_SCOPE("2a.Board_Update");
			mBoard->Update();
		}
		UpdateWeatherUi(DeltaTime::GetUnscaledDeltaTime());

		// 轮清后存档：BeginSurvivalCardSelect 置位，在 Board::Update 返回后（已脱离 Die() 调用栈）执行。
		// 触发轮清的那只濒死僵尸此刻仍在 EntityRegistry 中，由 SaveLevelData 内的 IsActive() 过滤排除。
		if (mPendingSurvivalSave) {
			mPendingSurvivalSave = false;
			GameAPP::GetInstance().mGameInfoSaver.SaveLevelData(
				mBoard.get(), mCardSlotManager.get());
		}

		auto& input = GameAPP::GetInstance().GetInputHandler();

		// 开发者模式：RSHIFT 键呼出/关闭面板（放置模式时按 SDLK_RSHIFT 回面板）
		if (GameAPP::mDevelopMode && input.IsKeyPressed(SDLK_RSHIFT)
			&& !mOpenMenu && !mSurvivalPerkSelectActive && !mPerkViewActive) {
			if (mDevSpawnMode)        { mDevSpawnMode = false; OpenDevPanel(); }
			else if (mDevPanelActive) { CloseDevPanel(); }
			else                      { OpenDevPanel(); }
		}

		// 开发者：召唤放置模式——独占 ESC 与左键（须在暂停菜单键处理之前）
		bool devConsumedEsc = false;
		if (mDevSpawnMode && !DeltaTime::IsPaused()) {
			if (input.IsKeyPressed(SDLK_ESCAPE)) {
				mDevSpawnMode = false;
				devConsumedEsc = true;
			}
			else if (input.IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
				const Vector mp = input.GetMouseWorldPosition();
				int bestRow = 0;
				float bestDist = 1e9f;
				for (int r = 0; r < mBoard->mRows; ++r) {
					const float d = std::abs(mp.y - mBoard->GetZombieSpawnY(r, mp.x));
					if (d < bestDist) { bestDist = d; bestRow = r; }
				}
				mBoard->CreateZombie(kDevZombieTable[mDevZombieIndex].first, bestRow, mp.x);
				LOG_DEBUG("DevMode") << "召唤 " << kDevZombieTable[mDevZombieIndex].second
					<< " row=" << bestRow << " x=" << mp.x;
			}
		}

		if (!devConsumedEsc && input.IsKeyPressed(SDLK_SPACE)) {
			ToggleSpacePause();
		}
		else if (mBoard->mBoardState != BoardState::LOSE_GAME
			&& !mOpenRestartMenu && !mOpenQuitMenu && !devConsumedEsc
			&& input.IsKeyPressed(SDLK_ESCAPE)) {
			if (mBoard->IsCobCannonTargeting()) {
				mBoard->mCursorObjectManager.ClearActive();
				devConsumedEsc = true;
			}
			else if (mOpenMenu) {
				mOpenMenu = false;
				DeltaTime::SetPaused(false);
				if (auto menu = mMenu.lock()) menu->Close();
				mMenu.reset();
			}
			else {
				OpenMenu();
			}
		}

		float deltaTime = DeltaTime::GetDeltaTime();

		switch (mCurrentStage) {
		case IntroStage::FINISH:
			// 入场动画已结束，无需处理
			break;
		case IntroStage::BACKGROUND_MOVE:
		{
			if (mBoard->mBoardState != BoardState::CHOOSE_CARD) break;

			if (!mHasEnter) {
				mAnimElapsed += deltaTime;
				if (mAnimElapsed >= mAnimDuration) {
					mAnimElapsed = mAnimDuration;
					mCurrentStage = IntroStage::SEEDBANK_SLIDE;
					mHasEnter = true;
				}
			}

			float t = mAnimElapsed / mAnimDuration;
			float eased = static_cast<float>((1 - cos(t * M_PI)) / 2);

			// 计算背景应有的屏幕坐标
			float screenX = mStartX + (mTargetSceneX - mStartX) * eased;

			// 背景世界坐标固定为 mStartX
			float worldX = mStartX;

			// 摄像机位置 = 世界坐标 - 屏幕坐标
			float camX = worldX - screenX;

			// 移动摄像机（保持 Y 轴不变）
			GameAPP::GetInstance().GetGraphics().SetCameraPosition(camX, 0);

			// 更新 mCurrectSceneX 供后续使用
			mCurrectSceneX = screenX;

			break;
		}
		case IntroStage::SEEDBANK_SLIDE:
		{
			// 首次进入时添加种子槽纹理（初始位置在屏幕外上方）
			if (!mSeedbankAdded) {
				AddTexture(ResourceKeys::Textures::IMAGE_SEEDBANK_LONG,
					kSeedBankX, kSeedBankStartY,
					SeedBankScaleX(), kSeedBankScaleY, LAYER_UI, true);
				mSeedbankAdded = true;
			}
			// 选卡界面：首次进入或生存轮间（上一轮选完已销毁）时按需创建
			if (!mChooseCardUI) {
				mChooseCardUI = GameObjectManager::GetInstance().
					CreateGameObjectImmediate<ChooseCardUI>(LAYER_UI, this);
			}

			// 种子槽滑落动画
			if (mSeedbankAnimElapsed < mSeedbankAnimDuration) {
				mSeedbankAnimElapsed += deltaTime;
				if (mSeedbankAnimElapsed > mSeedbankAnimDuration) mSeedbankAnimElapsed = mSeedbankAnimDuration;
			}

			float t = mSeedbankAnimElapsed / mSeedbankAnimDuration;
			float eased = static_cast<float>((1 - cos(t * M_PI)) / 2);

			float startY = kSeedBankStartY, targetY = kSeedBankRestY;

			float currentY = startY + (targetY - startY) * eased;
			SetTexturePosition(ResourceKeys::Textures::IMAGE_SEEDBANK_LONG,
				kSeedBankX, currentY);

			// --- 选卡界面动画 ---
			if (!mChooseCardUIMoving) {
				mChooseCardUIMoving = true;
				mChooseCardUIAnimElapsed = 0.0f;
			}
			if (mChooseCardUIMoving) {
				mChooseCardUIAnimElapsed += deltaTime;
				if (mChooseCardUIAnimElapsed > mChooseCardUIAnimDuration) {
					mChooseCardUIAnimElapsed = mChooseCardUIAnimDuration;
				}

				float t2 = mChooseCardUIAnimElapsed / mChooseCardUIAnimDuration;
				float eased2 = static_cast<float>((1 - cos(t2 * M_PI)) / 2);
				Vector currentPos = Vector::lerp(mChooseCardUIStartPos, mChooseCardUITargetPos, eased2);
				mChooseCardUI->SetPosition(currentPos);
			}

			// 检查两个动画是否都完成
			bool seedbankDone = (mSeedbankAnimElapsed >= mSeedbankAnimDuration);
			bool chooseUIDone = (mChooseCardUIAnimElapsed >= mChooseCardUIAnimDuration);
			if (seedbankDone && chooseUIDone) {
				// 确保最终位置准确
				mChooseCardUI->SetPosition(mChooseCardUITargetPos);
				mChooseCardUI->AddAllCard();
				// 启用按钮
				if (mChooseCardUI && mChooseCardUI->GetButton()) {
					mChooseCardUI->GetButton()->SetEnabled(true);
				}
				mCurrentStage = IntroStage::COMPLETE;
			}
			break;
		}
		case IntroStage::COMPLETE:
		{
			ShowSunCount();
			break;
		}
		case IntroStage::READY_SET_PLANT:
		{
			if (mReadyAnimElapsed < mReadyAnimDuration) {
				mReadyAnimElapsed += deltaTime;
				if (mReadyAnimElapsed > mReadyAnimDuration)
					mReadyAnimElapsed = mReadyAnimDuration;
			}

			float t = mReadyAnimElapsed / mReadyAnimDuration;
			float eased = static_cast<float>((1 - cos(t * M_PI)) / 2);

			// 计算目标屏幕坐标（从当前屏幕坐标移动到 mGameStartX）
			float startScreenX = mTargetSceneX;   // 当前屏幕坐标（由之前阶段更新）
			float targetScreenX = mGameStartX;     // 目标屏幕坐标（-250）

			float screenX = startScreenX + (targetScreenX - startScreenX) * eased;

			float worldX = mStartX;

			float camX = worldX - screenX;

			GameAPP::GetInstance().GetGraphics().SetCameraPosition(camX, 0);

			mCurrectSceneX = screenX;

			if (mReadyAnimElapsed >= mReadyAnimDuration) {
				if (mSurvivalRoundTransition) {
					// 生存轮间：铲子/小推车/音乐已存在，不重建，只恢复波次推进
					mSurvivalRoundTransition = false;
					if (mBoard) {
						mBoard->DestroyPreviewZombies();   // 清掉选卡阶段的预览僵尸
						mBoard->mBoardState = BoardState::GAME;

						// 切回音乐
						mBoard->PlayBackgroundMusic();
					}
					// 恢复铲子显示
					if (mShovelUI) mShovelUI->SetActive(true);
					if (auto shovel = mBoard->mShovel.lock()) shovel->SetActive(true);

					RegisterSurvivalGameUiOnce();
				}
				else if (mBoard) {
					mBoard->StartGame();
				}
				// 轮间选卡读档也在开战边沿结束恢复，避免标记残留到后续种植。
				if (mBoard && mBoard->IsLoadRestoreActive()) {
					mBoard->CompleteLoadRestore();
				}
				mCurrentStage = IntroStage::FINISH;
			}
			break;
		}
		}
		// 全屏白闪衰减
		if (mScreenFlashTimer > 0.0f)
		{
			mScreenFlashTimer -= DeltaTime::GetDeltaTime();
			if (mScreenFlashTimer < 0.0f) mScreenFlashTimer = 0.0f;
		}
		if (mLightningFlashTimer > 0.0f)
		{
			mLightningFlashTimer -= DeltaTime::GetDeltaTime();
			if (mLightningFlashTimer < 0.0f) mLightningFlashTimer = 0.0f;
		}

		UpdatePrompts(
			DeltaTime::GetDeltaTime(), DeltaTime::GetUnscaledDeltaTime());
	}

	if (mReadyToRestart) {
		auto& gameApp = GameAPP::GetInstance();
		gameApp.GetGraphics().SetCameraPosition(0, 0);
		gameApp.mGameInfoSaver.DeleteLevelData(mBoard.get());
		SceneManager::GetInstance().SwitchTo("GameScene");
		return;	// 避免场景已经弹出，变量错乱，执行后续代码
	}

	if (mDevPendingLevel >= 0) {
		const int lv = mDevPendingLevel;
		mDevPendingLevel = -1;
		GameAPP::GetInstance().GetGraphics().SetCameraPosition(0, 0);
		auto& sm = SceneManager::GetInstance();
		sm.SetGlobalData("EnterLevel", std::to_string(lv));
		sm.SwitchTo("GameScene");   // 重建 GameScene，不检查存档解锁
		return;
	}

	if (mReadyToBackMenu) {
		GameAPP::GetInstance().GetGraphics().SetCameraPosition(0, 0);
		auto& scenes = SceneManager::GetInstance();
		// 奖励已由 Trophy 入账保存；这里只传递一次展示参数，不保留已通关棋盘。
		if (mUnlockedPlant != PlantType::NUM_PLANT_TYPES) {
			const PlantType reward = mUnlockedPlant;
			scenes.RegisterScene<PlantAlmanacScene>("PlantRewardScene", reward);
			scenes.SwitchTo("PlantRewardScene");
		} else if (MiniGame::IsMiniGame(mBoard->mLevel)) {
			scenes.SetGlobalData("GameSelectMode", "minigames");
			scenes.SwitchTo("GameSelectScene");
		} else {
			scenes.SwitchTo("MainMenuScene");
		}
		return;
	}

	if (mLendToAlmanacScene) {
		mLendToAlmanacScene = false;
		auto& app = GameAPP::GetInstance();
		// 先捕获完整正式关卡数据；失败时留在父菜单，不销毁这局游戏。
		if (!app.mGameInfoSaver.CaptureAlmanacReturn(mBoard.get(), mCardSlotManager.get())) return;
		app.GetGraphics().SetCameraPosition(0, 0);
		DeltaTime::SetPaused(false);
		SceneManager::GetInstance().SwitchTo("AlmanacScene");
		return;
	}
}

void GameScene::ShowSunCount()
{
	if (mSunCounterRegistered) return;
	RegisterDrawCommand("SunCounter",
		[this](Graphics* g) {
			if (!g || !mBoard) return;
			const std::string text = std::to_string(mBoard->GetSun());
			const float scaleX = SeedBankScaleX();
			const float centerX = kSeedBankX
				+ (kSunPaperLeftInTexture + kSunPaperRightInTexture) * 0.5f * scaleX;
			const float centerY = kSeedBankRestY
				+ (kSunPaperTopInTexture + kSunPaperBottomInTexture) * 0.5f
				* kSeedBankScaleY;
			const float maxWidth = (kSunPaperRightInTexture - kSunPaperLeftInTexture)
				* scaleX - kSunTextInsetX * 2.0f;
			const float maxHeight = (kSunPaperBottomInTexture - kSunPaperTopInTexture)
				* kSeedBankScaleY - kSunTextInsetY * 2.0f;

			int fontSize = kSunTextMinFontSize;
			glm::vec2 textSize(0.0f);
			for (int candidate = kSunTextMaxFontSize;
				candidate >= kSunTextMinFontSize; --candidate) {
				const glm::vec2 candidateSize = g->MeasureTextSize(text,
					ResourceKeys::Fonts::FONT_FZCQ, candidate);
				if (candidateSize.x <= 0.0f || candidateSize.y <= 0.0f) continue;
				fontSize = candidate;
				textSize = candidateSize;
				if (candidateSize.x <= maxWidth && candidateSize.y <= maxHeight) break;
			}

			const Vector drawPosition = g->LogicalToWorld(
				centerX - textSize.x * 0.5f,
				centerY - textSize.y * 0.5f);
			GameAPP::GetInstance().DrawText(text, drawPosition,
				{ 0,0,0,255 }, ResourceKeys::Fonts::FONT_FZCQ, fontSize);
		},
		LAYER_UI + 100000);
	SortDrawCommands();
	mSunCounterRegistered = true;
}

void GameScene::BeginSurvivalPerkSelect()
{
	if (!mBoard) return;

	// 词条页是轮间模态流程：清掉上一轮尚未提交的手持物或炮击准星，避免其跨过
	// CHOOSE_CARD 状态残留，并由 CardSlotManager 的 BoardState 门禁阻止草坪点击穿透。
	mBoard->mCursorObjectManager.ClearActive();

	// Board 已切到 CHOOSE_CARD，退出场景会立即保存；实际清空卡槽虽在词条结算后执行，
	// 但冷却快照必须现在就独立出来，保证词条界面点 X 后仍能恢复上一轮未完成的冷却。
	mSurvivalCardCooldowns.clear();
	if (mCardSlotManager) {
		for (auto* card : mCardSlotManager->GetCards()) {
			if (!card) continue;
			if (card->IsCooldown()) {
				mSurvivalCardCooldowns[card->GetPlantType()] =
					{ card->GetCooldownTimer(), card->GetCooldownTime() };
			}
		}
	}

	// 重新进入流程前先收掉可能残留的测试/旧 UI，确保一轮只有一个活动选择框。
	CloseSurvivalPerkSelectBox();
	mSurvivalPerkStepsCompleted = 0;
	mSurvivalPerkPicksCompleted = 0;
	mSurvivalPerkRefreshesRemaining = SURVIVAL_PERK_REFRESHES_PER_ROUND;
	mSurvivalPerkSelectActive = true;
	DeltaTime::SetPaused(true);
	RenderSurvivalPerkSelectStep();
}

void GameScene::CloseSurvivalPerkSelectBox()
{
	if (auto box = mPerkSelectBox.lock()) {
		box->SetActive(false);
		box->Close();
	}
	mPerkSelectBox.reset();
}

void GameScene::RenderSurvivalPerkSelectStep()
{
	if (!mBoard) return;

	PerkOfferContext offerContext;
	offerContext.supportsPlanternMechanics = mBoard->SupportsPlanternMechanics();
	offerContext.rarePanelRollOverride = mNextPerkRareRollOverride;
	mNextPerkRareRollOverride = -1;
	mCurrentPerkOffer = RollPerkPairings(
		mBoard->GetPerkManager(), 3, offerContext);

	auto& pm = mBoard->GetPerkManager();

	// 与 DrawText 同字体同字号量出逻辑像素宽（取不到字体时按半宽兜底）
	auto measureW = [](const std::string& s, int fontSize) -> float {
		TTF_Font* f = ResourceManager::GetInstance().GetFont(ResourceKeys::Fonts::FONT_FZCQ, fontSize);
		if (!f) return static_cast<float>(s.size()) * fontSize * 0.5f;
		int w = 0, h = 0;
		TTF_SizeUTF8(f, s.c_str(), &w, &h);
		return static_cast<float>(w);
	};

	// 版面常量——盒子宽高完全由内容自动决定
	const int   titleFont  = 22;
	const int   rowFont    = 16;
	const float padX       = 30.0f;
	const float padY       = 22.0f;
	const float titleLineH = 30.0f;
	const float titleGap   = 16.0f;
	const float lineH      = 22.0f;            // 单行文字行高
	const float rowBlockH  = lineH * 2.0f;     // 每个配对两行（植物/僵尸）
	const float rowGap     = 20.0f;            // 配对之间的间隔
	const float gapTextBtn = 20.0f;            // 文字与「选择」按钮的间隔
	const float actionGap  = 20.0f;            // 配对区与底部操作按钮的间隔
	const float buttonGap  = 20.0f;            // 「刷新」与「放弃本次」之间的间隔
	const Vector selectBtnSize(100.0f, 40.0f);
	const Vector refreshBtnSize(210.0f, 44.0f);
	const Vector skipBtnSize(160.0f, 44.0f);
	const glm::vec4 green{ 53, 191, 61, 255 };
	const glm::vec4 red  { 200, 60, 60, 255 };
	const glm::vec4 titleColor{ 245, 214, 127, 255 };

	const std::string title = std::string(u8"第 ") + std::to_string(mBoard->mSurvivalRound - 1)
		+ u8" 轮 · 选择强化（第 " + std::to_string(mSurvivalPerkStepsCompleted + 1)
		+ u8"/" + std::to_string(SURVIVAL_PERK_PICKS_PER_ROUND) + u8" 次）";

	// 预生成每个配对的两行文字并量宽，求内容最大宽度（descZh 已自带词条名，不再叠加 nameZh）
	struct Row { std::string plant; std::string zombie; };
	std::vector<Row> rows;
	rows.reserve(mCurrentPerkOffer.size());
	float maxRowW = 0.0f;
	for (const PerkPairing& pr : mCurrentPerkOffer) {
		const PerkInfo& bp = SurvivalPerkManager::GetInfo(pr.plant);
		const PerkInfo& cz = SurvivalPerkManager::GetInfo(pr.zombie);
		auto tags = [](const PerkInfo& info) {
			std::string result;
			if (info.rarity == PerkRarity::RARE) result += u8"[稀有]";
			if (info.condition == PerkCondition::PLANTERN_MECHANICS) result += u8"[迷雾]";
			return result;
		};
		Row r;
		r.plant  = std::string(u8"植物：") + tags(bp) + bp.descZh + u8"（当前 " + std::to_string(pm.GetStacks(pr.plant)) + u8" 层）";
		r.zombie = std::string(u8"僵尸：") + tags(cz) + cz.descZh + u8"（当前 " + std::to_string(pm.GetStacks(pr.zombie)) + u8" 层）";
		float wp = measureW(r.plant, rowFont);
		float wz = measureW(r.zombie, rowFont);
		float w = (wp > wz) ? wp : wz;
		if (w > maxRowW) maxRowW = w;
		rows.push_back(r);
	}

	const float titleW = measureW(title, titleFont);
	float contentW = maxRowW + gapTextBtn + selectBtnSize.x;
	if (titleW > contentW)        contentW = titleW;
	const float actionButtonsW = refreshBtnSize.x + buttonGap + skipBtnSize.x;
	if (actionButtonsW > contentW) contentW = actionButtonsW;

	const int   N     = static_cast<int>(rows.size());
	const float rowsH = (N > 0) ? (N * rowBlockH + (N - 1) * rowGap) : 0.0f;
	const float boxW  = contentW + padX * 2.0f;
	const float boxH  = padY + titleLineH + titleGap + rowsH + actionGap + skipBtnSize.y + padY;

	const float cx = static_cast<float>(SCENE_WIDTH)  / 2.0f;   // 550
	const float cy = static_cast<float>(SCENE_HEIGHT) / 2.0f;   // 300
	const float boxLeft  = cx - boxW / 2.0f;
	const float boxTop   = cy - boxH / 2.0f;
	const float boxRight = cx + boxW / 2.0f;

	// 纯色面板：尺寸由内容自动决定，面板矩形与文字坐标严格对齐，
	// 避免墓碑纹理花边内缩导致文字溢出可视边框
	GameMessageBox::Builder builder{ Vector(cx, cy) };
	builder.Panel(boxW, boxH);

	// 标题（顶部居中）
	builder.Text(Vector(cx - titleW / 2.0f, boxTop + padY), static_cast<float>(titleFont), title, titleColor);

	// 配对行：绿=植物增益、红=僵尸增难，右侧「选择」按钮
	const float rowsTop = boxTop + padY + titleLineH + titleGap;
	for (int i = 0; i < N; ++i) {
		const float blockTop = rowsTop + i * (rowBlockH + rowGap);
		builder.Text(Vector(boxLeft + padX, blockTop),         static_cast<float>(rowFont), rows[i].plant,  green);
		builder.Text(Vector(boxLeft + padX, blockTop + lineH), static_cast<float>(rowFont), rows[i].zombie, red);

		const float btnY = blockTop + (rowBlockH - selectBtnSize.y) / 2.0f;
		builder.Button(u8"选择", Vector(boxRight - padX - selectBtnSize.x, btnY), selectBtnSize, 16,
			[this, i]() { this->ApplyPerkSelection(i); }, ResourceKeys::Textures::IMAGE_BUTTONSMALL, false);
	}

	// 两次选择共享同一轮的刷新额度；刷新只重抽当前候选，不结算当前选择机会。
	const bool canRefresh = mSurvivalPerkRefreshesRemaining > 0;
	const std::string refreshText = canRefresh
		? std::string(u8"刷新（剩余 ") + std::to_string(mSurvivalPerkRefreshesRemaining) + u8" 次）"
		: std::string(u8"刷新（已用完）");
	const float actionsLeft = cx - actionButtonsW / 2.0f;
	const float actionsY = boxTop + boxH - padY - skipBtnSize.y;
	builder.Button(refreshText, Vector(actionsLeft, actionsY), refreshBtnSize, 18,
		[this]() { this->RefreshSurvivalPerkSelection(); },
		ResourceKeys::Textures::IMAGE_BUTTONBIG, false, canRefresh);

	// 放弃只消耗当前选择机会；第 1 次放弃后仍会进入第 2 次并保留刷新余额。
	builder.Button(u8"放弃本次", Vector(actionsLeft + refreshBtnSize.x + buttonGap, actionsY),
		skipBtnSize, 20, [this]() { this->ApplyPerkSelection(-1); },
		ResourceKeys::Textures::IMAGE_BUTTONBIG, false);

	mPerkSelectBox = builder.Show();
}

bool GameScene::RefreshSurvivalPerkSelection()
{
	if (!mBoard || !mSurvivalPerkSelectActive || mSurvivalPerkRefreshesRemaining <= 0)
		return false;

	// 先扣额度再重建，保证新框立即展示本轮准确的剩余次数。
	--mSurvivalPerkRefreshesRemaining;
	CloseSurvivalPerkSelectBox();
	RenderSurvivalPerkSelectStep();
	return true;
}

void GameScene::ApplyPerkSelection(int index)
{
	const bool picked = mBoard && index >= 0 && index < static_cast<int>(mCurrentPerkOffer.size());
	if (picked) {
		const PerkPairing& pr = mCurrentPerkOffer[index];
		auto& pm = mBoard->GetPerkManager();
		pm.AddPerk(pr.plant);
		pm.AddPerk(pr.zombie);
		++mSurvivalPerkPicksCompleted;
	}
	// 选择与放弃都会消耗当前机会；步骤进度不能再由实际获得的词条数推导。
	++mSurvivalPerkStepsCompleted;

	// 先失活可避免遍历后清理前与下一步新框重叠一帧；真实按钮与 AutoTest 共用此生命周期。
	CloseSurvivalPerkSelectBox();

	if (mSurvivalPerkStepsCompleted < SURVIVAL_PERK_PICKS_PER_ROUND) {
		RenderSurvivalPerkSelectStep();
		return;
	}

	mSurvivalPerkSelectActive = false;
	mCurrentPerkOffer.clear();
	DeltaTime::SetPaused(false);

	// 两次机会均已结算，最多两对词条已写入 manager；选卡子流程会延后保存最终层数。
	BeginSurvivalCardSelect();
}

void GameScene::OpenPerkView()
{
	if (!mBoard) return;
	if (DeltaTime::IsPaused()) return;
	// 三向守卫：暂停菜单 / 轮间选词条模态 / 自身已开，均不叠开
	if (mOpenMenu || mSurvivalPerkSelectActive || mPerkViewActive) return;

	mPerkViewActive = true;
	DeltaTime::SetPaused(true);
	mPerkViewPage = 0;
	RenderPerkViewPage();
}

void GameScene::RenderPerkViewPage()
{
	if (!mBoard) return;
	auto& pm = mBoard->GetPerkManager();

	// 与 DrawText 同字体量逻辑像素宽（取不到字体时按半宽兜底）
	auto measureW = [](const std::string& s, int fontSize) -> float {
		TTF_Font* f = ResourceManager::GetInstance().GetFont(ResourceKeys::Fonts::FONT_FZCQ, fontSize);
		if (!f) return static_cast<float>(s.size()) * fontSize * 0.5f;
		int w = 0, h = 0;
		TTF_SizeUTF8(f, s.c_str(), &w, &h);
		return static_cast<float>(w);
	};

	const glm::vec4 green{ 53, 191, 61, 255 };
	const glm::vec4 red  { 200, 60, 60, 255 };
	const glm::vec4 titleColor{ 245, 214, 127, 255 };

	// 收集已选词条（stacks>0），按 enum 顺序；descZh 已自带效果描述
	struct Line { std::string text; glm::vec4 color; };
	std::vector<Line> perkLines;
	int distinct = 0, total = 0;
	for (int i = 0; i < static_cast<int>(PerkType::COUNT); ++i) {
		PerkType t = static_cast<PerkType>(i);
		int n = pm.GetStacks(t);
		if (n <= 0) continue;
		const PerkInfo& info = SurvivalPerkManager::GetInfo(t);
		++distinct;
		total += n;
		Line ln;
		std::string tags;
		if (info.rarity == PerkRarity::RARE) tags += u8"[稀有]";
		if (info.condition == PerkCondition::PLANTERN_MECHANICS) tags += u8"[迷雾]";
		ln.text  = std::string(u8"· ") + tags + info.descZh + u8"（已选 " + std::to_string(n) + u8" 次）";
		ln.color = (info.category == PerkCategory::PLANT_BUFF) ? green : red;
		perkLines.push_back(ln);
	}

	// 分页：每页最多 6 个 distinct 词条（distinct == perkLines.size()）
	constexpr int kPerksPerPage = 6;
	const int totalPages = (distinct > 0) ? ((distinct + kPerksPerPage - 1) / kPerksPerPage) : 1;
	if (mPerkViewPage < 0) mPerkViewPage = 0;
	if (mPerkViewPage > totalPages - 1) mPerkViewPage = totalPages - 1;
	const int pageStart = mPerkViewPage * kPerksPerPage;
	const int pageEnd   = std::min(distinct, pageStart + kPerksPerPage);

	std::string title = (distinct > 0)
		? (std::string(u8"已强化：") + std::to_string(distinct) + u8" 种词条 · 累计 " + std::to_string(total) + u8" 层")
		: std::string(u8"尚未选择任何强化词条");
	if (totalPages > 1)
		title += std::string(u8"（第 ") + std::to_string(mPerkViewPage + 1) + u8"/" + std::to_string(totalPages) + u8" 页）";

	// 固定面板（逻辑像素，居中于 550,300）
	const float boxW = 560.0f, boxH = 420.0f;
	const float padX = 30.0f, padY = 26.0f;
	const Vector closeBtnSize(160.0f, 44.0f);
	const float closeGap = 18.0f;
	const float cx = static_cast<float>(SCENE_WIDTH)  / 2.0f;
	const float cy = static_cast<float>(SCENE_HEIGHT) / 2.0f;
	const float boxLeft = cx - boxW / 2.0f;
	const float boxTop  = cy - boxH / 2.0f;
	const float innerW  = boxW - 2.0f * padX;
	const float availH  = boxH - 2.0f * padY - closeGap - closeBtnSize.y;
	const int   N       = pageEnd - pageStart;   // 本页行数（≤6）

	// 字号自动缩放：rowFont 18→10，titleFont=rowFont+4，挑「最大且能塞进固定面板」者；
	// 一路不满足则落到 floor=10（容忍轻微挤压，仍优于溢出可视边框）
	int   rowFont = 10, titleFont = 14;
	float titleLineH = 0.0f, rowLineH = 0.0f, titleGap = 0.0f, rowGap = 0.0f, contentH = 0.0f;
	for (int fnt = 18; fnt >= 10; --fnt) {
		const int   tf  = fnt + 4;
		const float tlh = tf  * 1.4f;
		const float rlh = fnt * 1.4f;
		const float tg  = fnt * 0.9f;
		const float rg  = fnt * 0.5f;
		const float ch  = tlh + (N > 0 ? (tg + N * rlh + (N - 1) * rg) : 0.0f);
		float maxW = measureW(title, tf);
		for (int li = pageStart; li < pageEnd; ++li) {
			float w = measureW(perkLines[li].text, fnt);
			if (w > maxW) maxW = w;
		}
		const bool fits = (ch <= availH) && (maxW <= innerW);
		if (fits || fnt == 10) {
			rowFont = fnt; titleFont = tf;
			titleLineH = tlh; rowLineH = rlh; titleGap = tg; rowGap = rg; contentH = ch;
			if (fits) break;   // 否则 fnt==12 兜底，循环自然结束
		}
	}

	// 纯色面板，同选词条框，规避墓碑花边内缩导致文字溢出
	GameMessageBox::Builder builder{ Vector(cx, cy) };
	builder.Panel(boxW, boxH);

	// 内容块在 availH 区域内垂直居中，词条少时不孤悬顶部
	const float blockTop = boxTop + padY + (availH - contentH) / 2.0f;
	const float titleW   = measureW(title, titleFont);
	builder.Text(Vector(cx - titleW / 2.0f, blockTop), static_cast<float>(titleFont), title, titleColor);

	float y = blockTop + titleLineH + titleGap;
	for (int i = 0; i < N; ++i) {
		const Line& ln = perkLines[pageStart + i];
		builder.Text(Vector(boxLeft + padX, y), static_cast<float>(rowFont), ln.text, ln.color);
		y += rowLineH + rowGap;
	}

	// 底部一行三按钮：上一页（左·仅非首页）· 关闭（中·恒显）· 下一页（右·仅有下一页）
	const float    boxRight   = cx + boxW / 2.0f;
	const Vector   navBtnSize(110.0f, 44.0f);
	const float    btnY       = boxTop + boxH - padY - closeBtnSize.y;

	builder.Button(u8"关闭", Vector(cx - closeBtnSize.x / 2.0f, btnY), closeBtnSize, 20,
		[this]() { this->ClosePerkView(); }, ResourceKeys::Textures::IMAGE_BUTTONBIG);
	if (mPerkViewPage > 0)
		builder.Button(u8"上一页", Vector(boxLeft + padX, btnY), navBtnSize, 18,
			[this]() { --mPerkViewPage; RenderPerkViewPage(); });
	if (mPerkViewPage < totalPages - 1)
		builder.Button(u8"下一页", Vector(boxRight - padX - navBtnSize.x, btnY), navBtnSize, 18,
			[this]() { ++mPerkViewPage; RenderPerkViewPage(); });

	mPerkViewBox = builder.Show();
}

void GameScene::ClosePerkView()
{
	mPerkViewActive = false;
	DeltaTime::SetPaused(false);
	// 查看面板仍由「关闭」按钮 autoClose=true 在帧末安全销毁；词条选择框则由 ApplyPerkSelection 统一关闭。
}

void GameScene::BeginSurvivalCardSelect()
{
	mSurvivalRoundTransition = true;

	// 让上一轮已升起的旗子平滑降回（否则会一直悬着，直到下一轮第 1 波 SetupFlags 才突变重置）。
	// 滑块归位由 GameProgress::Update 依据 mCurrentWave(已被 OnSurvivalRoundClear 归 0)自动完成。
	if (mGameProgress) mGameProgress->LowerAllFlags(1.0f);

	// 冷却快照已在进入词条页时提前完成；现在清空上一轮卡槽，让下一轮从空槽重新选择。
	if (mCardSlotManager)
		mCardSlotManager->ClearAllCards();

	// 轮间选卡：切到选卡背景音乐，进入下一轮时切回战斗音乐（见 READY_SET_PLANT 轮间分支）
	AudioSystem::PlayMusic(ResourceKeys::Music::MUSIC_CHOOSEYOURSEEDS, -1);
	mBoard->PrepareBackgroundMusic();

	// 选卡阶段隐藏铲子（不可用），进入下一轮时再恢复（见 READY_SET_PLANT 轮间分支）
	if (mShovelUI) mShovelUI->SetActive(false);
	if (auto shovel = mBoard->mShovel.lock()) shovel->SetActive(false);

	// 生成"即将打的那轮"的预览僵尸（mSurvivalRound 已自增、mSpawnZombieList 已重建），
	// 散落在右侧生成区，随相机平移露出；进入下一轮时销毁（见 READY_SET_PLANT 轮间分支）
	mBoard->CreatePreviewZombies();

	// 复用整套开场过场动画：相机平移"回到右边"露出僵尸区 → 选卡UI 滑入（种子槽保持停靠，不再滑落）。
	// 重置各阶段动画计时与一次性标记，使 BACKGROUND_MOVE / SEEDBANK_SLIDE 阶段能重新走一遍。
	// 注意：mSeedbankAdded 保持 true（种子槽纹理已存在，不重复添加）；
	//      mChooseCardUI 当前为 nullptr，SEEDBANK_SLIDE 会据此重新创建选卡界面。
	mAnimElapsed = 0.0f;
	mHasEnter = false;
	// 种子槽轮间不重新滑落：上一轮已停靠在 y=-10，这里把滑落动画直接标记为已完成，
	// SEEDBANK_SLIDE 阶段会令 currentY 恒为 targetY(-10)，种子槽保持停靠、无上下抽动。
	// （首次进入游戏时此值为 0，正常播放滑入动画，不受影响。）
	mSeedbankAnimElapsed = mSeedbankAnimDuration;
	mChooseCardUIAnimElapsed = 0.0f;
	mChooseCardUIMoving = false;
	mReadyAnimElapsed = 0.0f;
	mCurrentStage = IntroStage::BACKGROUND_MOVE;

	// 轮间存档点：延后一帧执行（见 mPendingSurvivalSave）。
	// 本函数由最后一只僵尸的 Die() 中途调用，该僵尸此刻仍在 EntityRegistry 中、
	// 尚未被 GameObjectManager 清理；若此处同帧存档会把濒死僵尸误序列化进存档。
	mPendingSurvivalSave = true;
}

void GameScene::ChooseCardComplete()
{
	LOG_INFO("GameScene") << "选卡完成，准备开始游戏";
	if (mCurrentStage != IntroStage::COMPLETE) return;
	// 提交入口也负责注册：AutoTest 可在 COMPLETE 首次可见时立即提交，早于该阶段下一帧 Update。
	ShowSunCount();

	if (mChooseCardUI) {
		auto& gameApp = GameAPP::GetInstance();
		if (!MiniGame::IsMiniGame(mBoard->mLevel)) {
			gameApp.mLastSelectedCards = mChooseCardUI->GetSelectedCardKeys();
			// 选卡提交即落盘；即使玩家随后直接退出，下一局也能恢复这组选择。
			if (!gameApp.mGameInfoSaver.SavePlayerInfo()) {
				LOG_ERROR("GameScene") << "保存上一次选卡失败，将在后续玩家存档点重试。";
			}
		}
		mChooseCardUI->TransferSelectedCardsTo(mCardSlotManager.get());
		mChooseCardUI->RemoveAllCards();
		GameObjectManager::GetInstance().DestroyGameObject(mChooseCardUI);
		mChooseCardUI = nullptr;
	}

	// 还原轮末快照的冷却：对重新选回的同种植物恢复其冷却进度（map 为空则无操作）
	if (!mSurvivalCardCooldowns.empty() && mCardSlotManager) {
		for (auto* card : mCardSlotManager->GetCards()) {
			if (!card) continue;
			auto it = mSurvivalCardCooldowns.find(card->GetPlantType());
			if (it != mSurvivalCardCooldowns.end()) {
				card->RestoreCooldown(it->second.first, it->second.second);
			}
		}
		mSurvivalCardCooldowns.clear();
	}

	// 所有模式统一走相机回移过场（READY_SET_PLANT）。
	// 生存轮间与首次进入的区别仅在该阶段末尾：轮间不重建铲子/小推车/音乐
	// （见 READY_SET_PLANT 对 mSurvivalRoundTransition 的判断）。
	mCurrentStage = IntroStage::READY_SET_PLANT;
	mReadyAnimElapsed = 0.0f;   // 复位，保证回移动画重新播放（轮间为第二次进入此阶段）
	mReadyStartPos = Vector(mCurrectSceneX, 0);

	RegisterSurvivalGameUiOnce();
}

void GameScene::RegisterSurvivalGameUiOnce()
{
	if (mGameUiRegistered) return;
	mGameUiRegistered = true;

	RegisterDrawCommand("ZombieNumber",
		[this](Graphics* g) {
			auto& gameApp = GameAPP::GetInstance();
			gameApp.DrawText(u8"当前僵尸数量: " + std::to_string(mBoard->mZombieNumber),
				Vector(3, 569), { 0,0,0,255 }, ResourceKeys::Fonts::FONT_FZCQ, 24);
			gameApp.DrawText(u8"当前僵尸数量: " + std::to_string(mBoard->mZombieNumber),
				Vector(5, 570), { 223,186 ,98 ,255 }, ResourceKeys::Fonts::FONT_FZCQ, 24);
		},
		LAYER_UI);
	RegisterDrawCommand("LevelName",
		[this](Graphics* g) {
			if (!mBoard || mBoard->mBoardState != BoardState::GAME) return;  // 选卡阶段隐藏
			DrawLevelName(GameAPP::GetInstance(), mBoard->mLevelName, mBoard->mIsSurvival || MiniGame::IsMiniGame(mBoard->mLevel));
		},
		LAYER_UI);
	RegisterDrawCommand("Difficulty",
		[this](Graphics* g) {
			if (!mBoard || mBoard->mBoardState != BoardState::GAME) return;  // 选卡阶段隐藏
			auto& gameApp = GameAPP::GetInstance();
			gameApp.DrawText("难度: " + std::to_string(gameApp.Difficulty),
				Vector(1030, 575), { 0,0,0,255 }, ResourceKeys::Fonts::FONT_FZCQ, 21);
			gameApp.DrawText("难度: " + std::to_string(gameApp.Difficulty),
				Vector(1032, 576), { 223,186,98,255 }, ResourceKeys::Fonts::FONT_FZCQ, 21);
		},
		LAYER_UI);
	SortDrawCommands();
}

void GameScene::ShowShovel()
{
	const Vector shovelBankCenter(mBoard->IsMineBackground() ? 815.0f : 850.0f, 30.0f);

	// 先创建铲子背景（renderOrder 较低，画在下面）
	auto shovelBank = GameObjectManager::GetInstance()
		.CreateGameObjectImmediate<ShovelBank>(LAYER_UI, mBoard.get());
	mShovelUI = shovelBank;

	// 再创建铲子（renderOrder 较高，画在上面）
	auto shovelWeak = mBoard->CreateShovel();
	if (auto shovel = shovelWeak.lock())
		shovel->SetHomePosition(shovelBankCenter);
}

GameProgress* GameScene::GetGameProgress() const
{
	return this->mGameProgress;
}

void GameScene::ActivateWaveProgress()
{
	if (!mGameProgress) return;
	mGameProgress->SetActive(true);
	auto& resources = ResourceManager::GetInstance();
	mGameProgress->SetupFlags(
		resources.GetTexture(ResourceKeys::Textures::IMAGE_FLAGMETER_PART_STICK),
		resources.GetTexture(ResourceKeys::Textures::IMAGE_FLAGMETER_PART_FLAG));
}

void GameScene::RestoreWaveProgress()
{
	ActivateWaveProgress();
	if (!mGameProgress) return;
	mGameProgress->InitializeRaisedFlags(-10.0f);
	mGameProgress->SnapProgressToCurrentWave();
}

void GameScene::TogglePlanternGearMenu()
{
	if (mCardSlotManager) mCardSlotManager->TogglePlanternGearMenu();
}

void GameScene::GameOver()
{
	if (mBoard->mBoardState == BoardState::LOSE_GAME) return;

	mBoard->mCursorObjectManager.ClearActive();
	GameAPP::GetInstance().mGameInfoSaver.DeleteLevelData(mBoard.get());
	mUIManager.RemoveButton(this->mMainMenuButton.lock());
	mMainMenuButton.reset();
	if (mShovelUI)
		GameObjectManager::GetInstance().DestroyGameObject(mShovelUI);

	mShovelUI = nullptr;

	if (auto shovel = mBoard->mShovel.lock())
	{
		shovel->Die();
	}

	GameMessageBox::Builder(Vector(SCENE_WIDTH / 2, SCENE_HEIGHT / 2))
		.Title(u8"游戏结束")
		.Message(u8"僵尸吃掉了你的脑子！")
		.Scale(kCompactDialogScale)
		.Button(MiniGame::IsMiniGame(mBoard->mLevel) ? u8"返回选关" : u8"返回菜单", Vector(380, 380), Vector(125 * 0.8f, 52 * 0.8f), 14, [this]() {
			this->mReadyToBackMenu = true;
			DeltaTime::SetPaused(false);
		})
		.Button(u8"重新开始", Vector(560, 380), Vector(125 * 0.8f, 52 * 0.8f), 14, [this]() {
			this->mReadyToRestart = true;
			DeltaTime::SetPaused(false);
		})
		.Show();
}

// ============ 开发者模式（-develop，D 键面板） ============

void GameScene::OpenDevPanel()
{
	if (!GameAPP::mDevelopMode || !mBoard) return;
	if (DeltaTime::IsPaused()) return;
	// 多向守卫：暂停菜单 / 词条选择 / 词条查看 / 自身已开，均不叠开
	if (mOpenMenu || mSurvivalPerkSelectActive || mPerkViewActive || mDevPanelActive) return;

	mDevPanelActive = true;
	DeltaTime::SetPaused(true);
	RenderDevPanel();
}

void GameScene::CloseDevPanel()
{
	if (auto box = mDevPanelBox.lock()) box->Close();
	mDevPanelBox.reset();
	mDevPanelActive = false;
	DeltaTime::SetPaused(false);
}

void GameScene::RenderDevPanel()
{
	// 状态变化即整体重建：旧盒由被点按钮 autoClose=true 在本帧控件遍历后关闭，这里只管建新盒。
	const float cx = static_cast<float>(SCENE_WIDTH) / 2.0f;    // 550
	const float cy = static_cast<float>(SCENE_HEIGHT) / 2.0f;   // 300
	const Vector boxSize(520.0f, 400.0f);
	const glm::vec4 titleColor{ 245, 214, 127, 255 };
	const glm::vec4 textColor { 230, 230, 230, 255 };

	GameMessageBox::Builder builder{ Vector(cx, cy) };
	builder.Panel(boxSize.x, boxSize.y);
	builder.Text(Vector(cx - 70.0f, 110.0f), 22.0f, u8"开发者面板", titleColor);

	auto toggleText = [](const char* name, bool on) {
		return std::string(name) + (on ? u8"：开" : u8"：关");
	};

	// 作弊开关（点击翻转后重建面板刷新文字）
	builder.Button(toggleText(u8"无冷却种植", GameAPP::mDevNoCooldown),
		Vector(340.0f, 160.0f), Vector(200.0f, 36.0f), 16,
		[this]() { GameAPP::mDevNoCooldown = !GameAPP::mDevNoCooldown; RenderDevPanel(); });
	builder.Button(toggleText(u8"无视阳光", GameAPP::mDevFreePlant),
		Vector(340.0f, 206.0f), Vector(200.0f, 36.0f), 16,
		[this]() { GameAPP::mDevFreePlant = !GameAPP::mDevFreePlant; RenderDevPanel(); });
	builder.Button(toggleText(u8"暂停刷怪", GameAPP::mDevSpawnPaused),
		Vector(580.0f, 160.0f), Vector(200.0f, 36.0f), 16,
		[this]() { GameAPP::mDevSpawnPaused = !GameAPP::mDevSpawnPaused; RenderDevPanel(); });

	// 僵尸类型选择行
	const int zn = static_cast<int>(kDevZombieTable.size());
	builder.Button("<", Vector(340.0f, 252.0f), Vector(40.0f, 36.0f), 16,
		[this, zn]() {
			mDevZombieIndex = (mDevZombieIndex + zn - 1) % zn;
			PersistDevPanelSelection();
			RenderDevPanel();
		});
	builder.Text(Vector(395.0f, 260.0f), 14.0f,
		kDevZombieTable[mDevZombieIndex].second, textColor);
	builder.Button(">", Vector(560.0f, 252.0f), Vector(40.0f, 36.0f), 16,
		[this, zn]() {
			mDevZombieIndex = (mDevZombieIndex + 1) % zn;
			PersistDevPanelSelection();
			RenderDevPanel();
		});
	builder.Button(u8"召唤", Vector(620.0f, 252.0f), Vector(90.0f, 36.0f), 16,
		[this]() { this->BeginDevSpawnMode(); });

	// 关卡选择行
	builder.Button(u8"-", Vector(340.0f, 302.0f), Vector(40.0f, 36.0f), 16,
		[this]() {
			if (mDevLevelSel > 1) --mDevLevelSel;
			PersistDevPanelSelection();
			RenderDevPanel();
		});
	builder.Text(Vector(420.0f, 310.0f), 16.0f,
		std::string(u8"关卡 ") + std::to_string(mDevLevelSel), textColor);
	builder.Button(u8"+", Vector(560.0f, 302.0f), Vector(40.0f, 36.0f), 16,
		[this]() {
			++mDevLevelSel;
			PersistDevPanelSelection();
			RenderDevPanel();
		});
	builder.Button(u8"进入", Vector(620.0f, 302.0f), Vector(90.0f, 36.0f), 16,
		[this]() { this->DevJumpToLevel(mDevLevelSel); });
	builder.Button(u8"进入无尽", Vector(340.0f, 348.0f), Vector(110.0f, 32.0f), 14,
		[this]() { DevJumpToLevel(SURVIVAL_ENDLESS_LEVEL); });
	builder.Button(u8"进入夜无尽", Vector(460.0f, 348.0f), Vector(130.0f, 32.0f), 14,
		[this]() { DevJumpToLevel(SURVIVAL_ENDLESS_NIGHT_LEVEL); });

	// 底部：下一波 / 关闭
	builder.Button(u8"下一波", Vector(360.0f, 420.0f), Vector(120.0f, 40.0f), 18,
		[this]() { this->DevTriggerNextWave(); },
		ResourceKeys::Textures::IMAGE_BUTTONBIG, false);   // 不自动关面板，可连点
	builder.Button(u8"关闭", Vector(600.0f, 420.0f), Vector(120.0f, 40.0f), 18,
		[this]() { mDevPanelActive = false; DeltaTime::SetPaused(false); mDevPanelBox.reset(); },
		ResourceKeys::Textures::IMAGE_BUTTONBIG);

	mDevPanelBox = builder.Show();
}

void GameScene::BeginDevSpawnMode()
{
	mDevPanelActive = false;
	DeltaTime::SetPaused(false);
	mDevPanelBox.reset();          // 盒子由按钮 autoClose 在本帧控件遍历后清理
	mDevSpawnMode = true;
	if (mCardSlotManager) mCardSlotManager->DeselectCard();   // 防手持卡与召唤点击叠加种植

	// 顶部提示（一次注册，靠 mDevSpawnMode 守卫显隐）
	if (!mDevHintRegistered) {
		RegisterDrawCommand("DevSpawnHint",
			[this](Graphics* g) {
				if (!mDevSpawnMode) return;
				const std::string tip = std::string(u8"召唤模式：")
					+ kDevZombieTable[mDevZombieIndex].second + u8"（左键放置，ESC 退出，RSHIFT 回面板）";
				GameAPP::GetInstance().DrawText(tip,
					g->LogicalToWorld(300, 30), { 255, 90, 90, 255 },
					ResourceKeys::Fonts::FONT_FZCQ, 18);
			},
			LAYER_UI + 100000);
		SortDrawCommands();
		mDevHintRegistered = true;
	}
}

/** 从 PlayerInfo 的稳定枚举名恢复面板状态；旧档或失效名称回退为普通僵尸。 */
void GameScene::RestoreDevPanelSelection()
{
	auto& gameApp = GameAPP::GetInstance();
	mDevLevelSel = std::max(1, gameApp.mDeveloperSelectedLevel);

	const auto found = std::find_if(kDevZombieTable.begin(), kDevZombieTable.end(),
		[&gameApp](const auto& entry) {
			return gameApp.mDeveloperSelectedZombie == entry.second;
		});
	mDevZombieIndex = found == kDevZombieTable.end()
		? 0
		: static_cast<int>(std::distance(kDevZombieTable.begin(), found));

	// 回写规范值，确保损坏或已删除的旧名称会在下一次保存时被修正。
	gameApp.mDeveloperSelectedLevel = mDevLevelSel;
	gameApp.mDeveloperSelectedZombie = kDevZombieTable[mDevZombieIndex].second;
}

/** 选择一经改变便持久化；保存名称而非下标/枚举值，避免僵尸表扩展导致旧选择漂移。 */
void GameScene::PersistDevPanelSelection()
{
	auto& gameApp = GameAPP::GetInstance();
	gameApp.mDeveloperSelectedLevel = mDevLevelSel;
	gameApp.mDeveloperSelectedZombie = kDevZombieTable[mDevZombieIndex].second;
	if (!gameApp.mGameInfoSaver.SavePlayerInfo()) {
		LOG_WARN("DevMode") << "开发者面板选择保存失败，将在游戏退出时重试。";
	}
}

void GameScene::DevJumpToLevel(int level)
{
	// 不能在按钮回调（本帧 Update 中段）直接 SwitchTo 销毁自身——
	// 与 mReadyToBackMenu 同理，置 pending 由 Update 尾部统一执行。
	// 生存快捷入口只传目标关卡，不覆盖普通关卡与召唤僵尸的已保存选择。
	mDevPanelActive = false;
	DeltaTime::SetPaused(false);
	mDevPanelBox.reset();
	mDevPendingLevel = level;
}

void GameScene::DevTriggerNextWave()
{
	// 面板不关闭（按钮 autoClose=false）：直接走出波入口，暂停中也立即生成，连点连出多波
	if (mBoard && mBoard->mBoardState == BoardState::GAME
		&& mBoard->mCurrentWave < mBoard->mMaxWave) {
		mBoard->SummonNextWave();
	}
}

void GameScene::ShowScreenFlash(float duration, float peakAlpha)
{
	if (duration <= 0.0f) return;
	mScreenFlashDuration = duration;
	mScreenFlashTimer = duration;
	mScreenFlashPeakAlpha = std::clamp(peakAlpha, 0.0f, 255.0f);
}

/** 为本次闪电生成稳定路径；独立视觉序列保证不会扰动天气、出怪等玩法随机结果。 */
void GameScene::ShowLightningStrike(float duration)
{
	if (duration <= 0.0f) return;

	uint32_t randomState = (++mLightningVisualSequence * 747796405u) + 2891336453u;
	auto nextUnit = [&randomState]() {
		randomState ^= randomState << 13;
		randomState ^= randomState >> 17;
		randomState ^= randomState << 5;
		return static_cast<float>(randomState & 0x00FFFFFFu) / 16777216.0f;
	};
	auto visualRange = [&nextUnit](float minValue, float maxValue) {
		return minValue + (maxValue - minValue) * nextUnit();
	};

	mLightningFlashDuration = duration;
	mLightningFlashTimer = duration;
	const float strikeX = visualRange(360.0f, 1030.0f);
	const float strikeY = visualRange(435.0f, 555.0f);
	mLightningStrikePoint = glm::vec2(strikeX, strikeY);
	const glm::vec2 cloudPoint(
		std::clamp(mLightningStrikePoint.x + visualRange(-190.0f, 190.0f),
			300.0f, 1060.0f),
		72.0f);

	mLightningMainPath.clear();
	mLightningMainPath.reserve(kLightningMainSegments + 1);
	mLightningMainPath.push_back(cloudPoint);
	for (int index = 1; index < kLightningMainSegments; ++index) {
		const float progress = static_cast<float>(index)
			/ static_cast<float>(kLightningMainSegments);
		const glm::vec2 center = cloudPoint
			+ (mLightningStrikePoint - cloudPoint) * progress;
		const float taper = 1.0f - progress * 0.45f;
		const float jitterX = visualRange(-52.0f, 52.0f) * taper;
		const float jitterY = visualRange(-8.0f, 8.0f);
		mLightningMainPath.emplace_back(center.x + jitterX, center.y + jitterY);
	}
	mLightningMainPath.push_back(mLightningStrikePoint);

	mLightningBranches.clear();
	mLightningBranches.reserve(kLightningBranchCount * 2);
	for (int branchIndex = 0; branchIndex < kLightningBranchCount; ++branchIndex) {
		const int mainIndex = 3 + branchIndex * 2;
		const glm::vec2 start = mLightningMainPath[mainIndex];
		const float direction = nextUnit() < 0.5f ? -1.0f : 1.0f;
		const float elbowLengthX = visualRange(38.0f, 72.0f);
		const float elbowLengthY = visualRange(34.0f, 58.0f);
		const float elbowX = std::clamp(start.x + direction * elbowLengthX,
			24.0f, static_cast<float>(SCENE_WIDTH - 24));
		const glm::vec2 elbow(elbowX, start.y + elbowLengthY);
		const float endLengthX = visualRange(24.0f, 58.0f);
		const float endLengthY = visualRange(28.0f, 52.0f);
		const float endX = std::clamp(elbow.x + direction * endLengthX,
			16.0f, static_cast<float>(SCENE_WIDTH - 16));
		const float endY = std::min(elbow.y + endLengthY,
			mLightningStrikePoint.y - 18.0f);
		const glm::vec2 end(endX, endY);
		mLightningBranches.emplace_back(start, elbow);
		mLightningBranches.emplace_back(elbow, end);
	}
}

void GameScene::ShowWeatherForecastFailure(
	RainIntensity forecast, RainIntensity actual,
	TyphoonStrength forecastTyphoon, TyphoonStrength actualTyphoon)
{
	if (mBoard && mBoard->IsWeatherPanelInterferenceActive()) return;
	RestoreWeatherForecastFailure(kForecastFailureDuration,
		forecast, actual, forecastTyphoon, actualTyphoon);
}

void GameScene::RestoreWeatherForecastFailure(float remaining,
	RainIntensity forecast, RainIntensity actual,
	TyphoonStrength forecastTyphoon, TyphoonStrength actualTyphoon)
{
	mWeatherForecastFailureTimer = std::clamp(remaining, 0.0f,
		kForecastFailureDuration);
	if (mWeatherForecastFailureTimer <= 0.0f) {
		mFailedForecastRainIntensity = RainIntensity::CLEAR;
		mActualForecastRainIntensity = RainIntensity::CLEAR;
		mFailedForecastTyphoonStrength = TyphoonStrength::NONE;
		mActualForecastTyphoonStrength = TyphoonStrength::NONE;
		return;
	}
	mFailedForecastRainIntensity = forecast;
	mActualForecastRainIntensity = actual;
	mFailedForecastTyphoonStrength = forecastTyphoon;
	mActualForecastTyphoonStrength = actualTyphoon;
}

void GameScene::ShowCurrentWeatherNotice()
{
	if (mBoard && mBoard->IsWeatherPanelInterferenceActive()) return;
	mCurrentWeatherNoticeTimer = kCurrentWeatherNoticeDuration;
}

void GameScene::RestoreCurrentWeatherNotice(float remaining)
{
	mCurrentWeatherNoticeTimer = std::clamp(remaining, 0.0f,
		kCurrentWeatherNoticeDuration);
}

WeatherPresentationState GameScene::CaptureWeatherPresentationState() const
{
	return WeatherPresentationState{
		mCurrentWeatherNoticeTimer,
		mWeatherForecastFailureTimer,
		mFailedForecastRainIntensity,
		mActualForecastRainIntensity,
		mFailedForecastTyphoonStrength,
		mActualForecastTyphoonStrength
	};
}

void GameScene::RestoreWeatherPresentationState(
	const WeatherPresentationState& state)
{
	if (mBoard && mBoard->IsWeatherPanelInterferenceActive()) {
		CancelWeatherInformationNotices();
		return;
	}
	RestoreCurrentWeatherNotice(state.currentWeatherNoticeTimer);
	RestoreWeatherForecastFailure(state.forecastFailureTimer,
		state.failedForecast, state.actualForecast,
		state.failedForecastTyphoon, state.actualForecastTyphoon);
}

/**
 * 推进所有并存提示的缩放与透明度，并在动画结束后统一移除。
 * 保留 vector 的插入顺序，使后来出现的提示在绘制时自然覆盖较早提示。
 */
void GameScene::UpdatePrompts(float deltaTime, float unscaledDeltaTime)
{
	if ((deltaTime <= 0.0f && unscaledDeltaTime <= 0.0f) || mPrompts.empty()) return;
	for (PromptAnimation& prompt : mPrompts) {
		if (!prompt.active) continue;
		const float step = prompt.useUnscaledTime ? unscaledDeltaTime : deltaTime;
		if (step <= 0.0f) continue;
		prompt.timer += step;
		switch (prompt.stage) {
		case PromptStage::NONE:
			prompt.active = false;
			break;
		case PromptStage::APPEAR:
		{
			const float t = std::min(prompt.timer / prompt.appearDuration, 1.0f);
			prompt.scale = 1.5f - 0.5f * t;
			prompt.alpha = static_cast<Uint8>(255.0f * t);
			if (prompt.timer >= prompt.appearDuration) {
				prompt.stage = PromptStage::HOLD;
				prompt.timer = 0.0f;
				prompt.scale = 1.0f;
				prompt.alpha = 255;
			}
			break;
		}
		case PromptStage::HOLD:
			if (prompt.timer >= prompt.holdDuration) {
				prompt.stage = PromptStage::FADE_OUT;
				prompt.timer = 0.0f;
			}
			break;
		case PromptStage::FADE_OUT:
		{
			const float t = std::min(prompt.timer / prompt.fadeDuration, 1.0f);
			prompt.alpha = static_cast<Uint8>(255.0f * (1.0f - t));
			prompt.scale = 1.0f + 0.2f * t;
			if (prompt.timer >= prompt.fadeDuration) {
				prompt.active = false;
				prompt.stage = PromptStage::NONE;
			}
			break;
		}
		}
	}
	mPrompts.erase(std::remove_if(mPrompts.begin(), mPrompts.end(),
		[](const PromptAnimation& prompt) { return !prompt.active; }),
		mPrompts.end());
}

/** 绘制图片或紧急文字提示；顺序遍历保证 vector 尾部的最新提示处于最上层。 */
void GameScene::DrawPrompts(Graphics* g) const
{
	if (!g) return;
	for (const PromptAnimation& prompt : mPrompts) {
		if (!prompt.active) continue;
		if (prompt.contentType == PromptContentType::IMAGE) {
			auto texture = ResourceManager::GetInstance().GetTexture(prompt.content);
			if (!texture) continue;
			const float w = static_cast<float>(texture->width) * prompt.scale;
			const float h = static_cast<float>(texture->height) * prompt.scale;
			const float drawX = (static_cast<float>(SCENE_WIDTH) - w) * 0.5f;
			const float drawY = (static_cast<float>(SCENE_HEIGHT) - h) * 0.5f;
			g->DrawTexture(texture, drawX, drawY, w, h, 0.0f,
				glm::vec4(255.0f, 255.0f, 255.0f, prompt.alpha));
			continue;
		}

		float textScale = prompt.scale;
		if (prompt.stage == PromptStage::HOLD) {
			textScale *= 1.0f + kPromptTextHoldPulse
				* std::sin(prompt.timer * kPromptTextHoldPulseSpeed);
		}
		int textWidth = static_cast<int>(prompt.content.size()) * prompt.fontSize / 2;
		int textHeight = prompt.fontSize;
		if (TTF_Font* font = ResourceManager::GetInstance().GetFont(
			ResourceKeys::Fonts::FONT_FZCQ, prompt.fontSize)) {
			TTF_SizeUTF8(font, prompt.content.c_str(), &textWidth, &textHeight);
		}
		const float scaledWidth = static_cast<float>(textWidth) * textScale;
		const float scaledHeight = static_cast<float>(textHeight) * textScale;
		const float textX = (static_cast<float>(SCENE_WIDTH) - scaledWidth) * 0.5f;
		const float textY = (static_cast<float>(SCENE_HEIGHT) - scaledHeight) * 0.5f;
		const float visibility = static_cast<float>(prompt.alpha) / 255.0f;
		const float bandY = (static_cast<float>(SCENE_HEIGHT) - kPromptBandHeight) * 0.5f;
		const float bandWidth = static_cast<float>(SCENE_WIDTH)
			- kPromptBandHorizontalInset * 2.0f;
		glm::vec4 accent = prompt.textColor;
		accent.a *= visibility;

		// 暗幕与上下警戒线把文本从复杂战场中分离；双层辉光加强风暴压迫感。
		g->FillRect(kPromptBandHorizontalInset, bandY, bandWidth, kPromptBandHeight,
			glm::vec4(4.0f, 8.0f, 18.0f, 235.0f * visibility));
		g->FillRect(kPromptBandHorizontalInset, bandY, bandWidth, 3.0f,
			glm::vec4(accent.r, accent.g, accent.b, 210.0f * visibility));
		g->FillRect(kPromptBandHorizontalInset, bandY + kPromptBandHeight - 3.0f,
			bandWidth, 3.0f,
			glm::vec4(accent.r, accent.g, accent.b, 210.0f * visibility));
		const glm::vec4 glow(accent.r, accent.g, accent.b, 58.0f * visibility);
		g->DrawText(prompt.content, ResourceKeys::Fonts::FONT_FZCQ, prompt.fontSize,
			glow, textX - 2.0f, textY, textScale);
		g->DrawText(prompt.content, ResourceKeys::Fonts::FONT_FZCQ, prompt.fontSize,
			glow, textX + 2.0f, textY, textScale);
		g->DrawText(prompt.content, ResourceKeys::Fonts::FONT_FZCQ, prompt.fontSize,
			glm::vec4(0.0f, 0.0f, 0.0f, 230.0f * visibility),
			textX + 3.0f, textY + 3.0f, textScale);
		g->DrawText(prompt.content, ResourceKeys::Fonts::FONT_FZCQ, prompt.fontSize,
			accent, textX, textY, textScale);
	}
}

void GameScene::ShowPrompt(const std::string& textureKey,
	float appearDur,
	float holdDur,
	float fadeDur)
{
	PromptAnimation prompt;
	prompt.active = true;
	prompt.stage = PromptStage::APPEAR;
	prompt.contentType = PromptContentType::IMAGE;
	prompt.scale = 1.5f;
	prompt.alpha = 0;
	prompt.content = textureKey;
	prompt.appearDuration = std::max(appearDur, 0.01f);
	prompt.holdDuration = std::max(holdDur, 0.01f);
	prompt.fadeDuration = std::max(fadeDur, 0.01f);
	mPrompts.push_back(std::move(prompt));
}

void GameScene::ShowTextPrompt(const std::string& text, const glm::vec4& color,
	int fontSize, float appearDur, float holdDur, float fadeDur,
	bool useUnscaledTime, PromptPurpose purpose)
{
	if (text.empty()) return;
	PromptAnimation prompt;
	prompt.active = true;
	prompt.stage = PromptStage::APPEAR;
	prompt.contentType = PromptContentType::TEXT;
	prompt.scale = 1.5f;
	prompt.alpha = 0;
	prompt.content = text;
	prompt.textColor = glm::clamp(color, glm::vec4(0.0f), glm::vec4(255.0f));
	prompt.fontSize = std::max(fontSize, 1);
	prompt.appearDuration = std::max(appearDur, 0.01f);
	prompt.holdDuration = std::max(holdDur, 0.01f);
	prompt.fadeDuration = std::max(fadeDur, 0.01f);
	prompt.useUnscaledTime = useUnscaledTime;
	prompt.purpose = purpose;
	mPrompts.push_back(std::move(prompt));
}

void GameScene::ShowRoofMarshalAssaultWarning(int row, float duration)
{
	if (duration <= 0.0f) return;
	ShowTextPrompt(u8"突击令：第" + std::to_string(row + 1) + u8"行全军突击！",
		glm::vec4(255.0f, 62.0f, 42.0f, 255.0f),
		kRoofMarshalPromptFontSize,
		kRoofMarshalPromptAppearDuration,
		kRoofMarshalPromptHoldDuration,
		kRoofMarshalPromptFadeDuration,
		false);
}

void GameScene::ShowPlanternLowFuelWarning()
{
	ShowTextPrompt(u8"路灯花燃料即将耗尽！",
		glm::vec4(255.0f, 58.0f, 48.0f, 255.0f),
		kPlanternLowFuelPromptFontSize,
		kPlanternLowFuelPromptAppearDuration,
		kPlanternLowFuelPromptHoldDuration,
		kPlanternLowFuelPromptFadeDuration,
		true);
}

void GameScene::ShowHeavyRainWarning(TyphoonStrength strength, int variant)
{
	const int selected = std::clamp(variant, 0, 2);
	const char* text = nullptr;
	glm::vec4 color(112.0f, 210.0f, 255.0f, 255.0f);
	if (mBoard && mBoard->SupportsWinterTemperature()
		&& mBoard->GetAmbientTemperatureC() <= 0.0f
		&& strength == TyphoonStrength::NONE) {
		constexpr const char* kSnowLines[] = {
			u8"朔雪压园寒色重，冻云垂野夜无声",
			u8"寒潮过境霜华结，万点飞琼覆故园",
			"THE COLD DESCENDS — THE GARDEN FREEZES",
		};
		text = kSnowLines[selected];
	}
	if (text) {
		ShowTextPrompt(text, color, kHeavyRainPromptFontSize,
			kHeavyRainPromptAppearDuration,
			kHeavyRainPromptHoldDuration,
			kHeavyRainPromptFadeDuration, false,
			PromptPurpose::HEAVY_RAIN_WARNING);
		return;
	}
	switch (strength) {
	case TyphoonStrength::NONE:
	{
		constexpr const char* kLines[] = {
			u8"玄云压城雾不开，银河倒泻雨声来",
			u8"雷隐千峰云覆台，雨倾万壑浪奔来",
			"THE HEAVENS WEEP — THE FLOOD DESCENDS",
		};
		text = kLines[selected];
		break;
	}
	case TyphoonStrength::TYPHOON:
	{
		constexpr const char* kLines[] = {
			u8"长风卷叶穿孤城，疏雨敲窗万木鸣",
			u8"云旗猎猎遮危城，夜雨萧萧动客旌",
			"THE WIND HUNTS — BAR THE GATES",
		};
		text = kLines[selected];
		color = glm::vec4(255.0f, 205.0f, 88.0f, 255.0f);
		break;
	}
	case TyphoonStrength::SEVERE:
	{
		constexpr const char* kLines[] = {
			u8"罡风裂野撼孤城，怒雨翻江万壑鸣",
			u8"狂澜撼岳群山惊，飞石穿云万谷鸣",
			"THE GALE ROARS — KNEEL OR BREAK",
		};
		text = kLines[selected];
		color = glm::vec4(255.0f, 126.0f, 66.0f, 255.0f);
		break;
	}
	case TyphoonStrength::SUPER:
	{
		constexpr const char* kLines[] = {
			u8"天地无光山岳倾，九霄雷坠鬼神惊",
			u8"乾坤倒转星河坠，万里山川一怒摧",
			"THE END DESCENDS — ALL SHALL BREAK",
		};
		text = kLines[selected];
		color = glm::vec4(255.0f, 60.0f, 82.0f, 255.0f);
		break;
	}
	}
	if (!text) return;
	ShowTextPrompt(text, color, kHeavyRainPromptFontSize,
		kHeavyRainPromptAppearDuration,
		kHeavyRainPromptHoldDuration,
		kHeavyRainPromptFadeDuration, false,
		PromptPurpose::HEAVY_RAIN_WARNING);
}

void GameScene::CancelHeavyRainWarning()
{
	mPrompts.erase(std::remove_if(mPrompts.begin(), mPrompts.end(),
		[](const PromptAnimation& prompt) {
			return prompt.purpose == PromptPurpose::HEAVY_RAIN_WARNING;
		}), mPrompts.end());
}

void GameScene::CancelWeatherInformationNotices()
{
	mCurrentWeatherNoticeTimer = 0.0f;
	RestoreWeatherForecastFailure(0.0f,
		RainIntensity::CLEAR, RainIntensity::CLEAR,
		TyphoonStrength::NONE, TyphoonStrength::NONE);
}
