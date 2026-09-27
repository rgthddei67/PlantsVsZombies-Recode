#include "CrazyDaveDialog.h"

#include "../DeltaTime.h"
#include "../GameRandom.h"
#include "../GameApp.h"
#include "../Graphics.h"
#include "../Logger.h"
#include "../ResourceKeys.h"
#include "../ResourceManager.h"
#include "AudioSystem.h"
#include "../UI/InputHandler.h"
#include "../Reanimation/Animator.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <array>

namespace {
	struct ScriptMessage {
		std::string text;
		std::string talkTrack;
	};

	constexpr float kDaveBaseX = 230.0f;            // 戴夫动画的屏幕基准 X，单位：逻辑像素
	constexpr float kDaveBaseY = 12.0f;             // 戴夫动画的屏幕基准 Y，单位：逻辑像素
	constexpr float kDaveScale = 0.55f;             // 原版大尺寸戴夫素材缩放到 1100x600 对话构图
	constexpr float kBubbleX = 388.0f;               // 气泡左缘，单位：逻辑像素
	constexpr float kBubbleY = 72.0f;                // 气泡上缘，单位：逻辑像素
	constexpr float kBubbleWidth = 660.0f;           // 气泡宽度，单位：逻辑像素
	constexpr float kBubbleHeight = 258.0f;          // 气泡高度，容纳最多三行正文与操作提示
	constexpr float kBubbleRadius = 18.0f;           // 程序化圆角半径，单位：逻辑像素
	constexpr float kTextX = kBubbleX + 34.0f;       // 正文左缘，单位：逻辑像素
	constexpr float kTextY = kBubbleY + 40.0f;       // 正文首行顶部，单位：逻辑像素
	constexpr float kTextWidth = kBubbleWidth - 68.0f; // 正文最大宽度，单位：逻辑像素
	constexpr float kTextLineHeight = 39.0f;         // 正文行高，单位：逻辑像素
	constexpr int kTextFontSize = 26;                // 戴夫闲聊正文字号
	constexpr int kFooterFontSize = 15;              // 页码与操作提示字号

	struct Conversation {
		int level;
		std::vector<ScriptMessage> messages;
	};

	// 保留戴夫的闲聊口吻；复杂规则分句说明，让玩家能据此完成第一次操作。
	const std::array<Conversation, 16> kConversations = {{
		{ 10, {
			{ u8"欢迎来到夜班！夜班没有加班费，只有天气预报。", "anim_smalltalk" },
			{ u8"从这儿开始，天说变就变——下雨时，院子两边都会跟着换个节奏。", "anim_mediumtalk" },
			{ u8"要是预报里写着暴雨，最好把花盆……不对，把帽子抓牢。哇卜！", "anim_crazy" },
		} },
		{ 28, {
			{ u8"今晚的雾浓得能拿勺子舀！", "anim_smalltalk" },
			{ u8"放心，这一晚它只蒙你的眼，不蒙植物的眼。", "anim_mediumtalk" },
			{ u8"至少——今晚是这样。哇卜！", "anim_crazy" },
		} },
		{ 29, {
			{ u8"昨天我把路灯花借给雾照了照，雾很不高兴。", "anim_smalltalk" },
			{ u8"所以今天它学会躲在雾里，让远处的植物看不见目标。", "anim_mediumtalk" },
			{ u8"路灯花也学会了吃燃料。我的钱包没有学会。", "anim_blahblah" },
		} },
		{ 36, {
			{ u8"天气预报说今晚后半段会有‘一点小风’。", "anim_smalltalk" },
			{ u8"如果大雨、浓雾和风一起来，那绝对只是巧合！", "anim_mediumtalk" },
			{ u8"记得留点余地，也许连僵尸都会赶时间。哇卜！", "anim_crazy" },
		} },
		{ 37, {
			{ u8"欢迎来到屋顶！我已经把花盆放好了。大概放好了。", "anim_smalltalk" },
			{ u8"这里的坡不喜欢直来直去的东西，投出去的反而更懂路。", "anim_mediumtalk" },
			{ u8"还有，下雨时别盯着瓦片看太久——它们会把攒下来的水一起倒掉。", "anim_blahblah" },
		} },
		{ 46, {
			{ u8"同一片屋顶，到了晚上就会开始想念闪电。", "anim_smalltalk" },
			{ u8"雨会让瓦片慢慢攒电；攒满以后，地上的家伙都可能被‘提醒’一下。", "anim_mediumtalk" },
			{ u8"白天那套冲水的毛病还在。屋顶从不只坏一种东西。", "anim_blahblah" },
		} },
		{ 54, {
			{ u8"我带来了一个好消息：雾也爬上屋顶了！", "anim_smalltalk" },
			{ u8"坏消息是，它记得怎么挡住植物的视线。", "anim_mediumtalk" },
			{ u8"路灯花还记得怎么烧燃料。希望你也记得。", "anim_blahblah" },
		} },
		{ 55, {
			{ u8"院子北边那扇门终于开了！我一直以为后面是我的备用冰箱。", "anim_smalltalk" },
			{ u8"寒潮会把花园一格一格冻起来，温度计比我的牙齿更早打颤。", "anim_mediumtalk" },
			{ u8"雨到了这里可能会变成雪，风倒是冻得不想来。", "anim_blahblah" },
			{ u8"别等地都冻硬了才想起准备——冰从来不排队。哇卜！", "anim_crazy" },
		} },
		{ 62, {
			{ u8"今天凉飕飕的，我的冰箱都说自己不够专业。", "anim_smalltalk" },
			{ u8"一开局不久会有一阵强寒潮，先把能种的地想清楚！", "anim_mediumtalk" },
			{ u8"等它回暖，后面的天气又会照老规矩抽。哇卜！", "anim_crazy" },
		} },
		{ 63, {
			{ u8"今天的冷风比昨天更有礼貌——它先敲门，再把门冻上。", "anim_smalltalk" },
			{ u8"一开局不久还是会来一阵强寒潮；最后一关可别把准备拖到结冰后。", "anim_mediumtalk" },
			{ u8"等它缓过气，天气又会继续随机胡闹。我的最爱。哇卜！", "anim_crazy" },
		} },
		{ 64, {
			{ u8"天气预报在这里冻坏了。好消息是，我带了三只不会说话的仪表！", "anim_smalltalk" },
			{ u8"湿度表变红以后，积雪会在雪地下面挖近路。那不是鼹鼠，千万别喂它。", "anim_mediumtalk" },
			{ u8"先记住：洞不会跟着雪一起消失。哇卜！", "anim_crazy" },
		} },
		{ 65, {
			{ u8"风速表变红时，天上飞的东西会横——不对，会竖着跑偏！", "anim_smalltalk" },
			{ u8"每一发东西起飞时就选好了往上还是往下，边界外面可没有备用草坪。", "anim_mediumtalk" },
			{ u8"直着飞和贴地打的倒不在乎。它们比较固执。", "anim_blahblah" },
		} },
		{ 66, {
			{ u8"如果三只表一起变红，白毛风就要来了！", "anim_smalltalk" },
			{ u8"北极星花蓄满以后，会在附近植物真正看不清、或者弹道要被吹偏时自动指路！", "anim_mediumtalk" },
			{ u8"极光只照九格、只维持八秒，别把所有火力摊得太开。哇卜！", "anim_crazy" },
		} },
		{ 68, {
			{ u8"那些红眼睛的家伙能感觉到新种下的植物！", "anim_smalltalk" },
			{ u8"先拿坚果挡住热脉冲，或者骗它开枪后趁装填赶快种！", "anim_crazy" },
		} },
		{ 69, {
			{ u8"冰镜草要先花时间凝出镜子，临时种可来不及！", "anim_smalltalk" },
			{ u8"一株能攒两面，每面只能挡住一发直线敌弹！", "anim_mediumtalk" },
		} },
		{ 82, { // 10-1：先说明资源与操作，再说明敌方经营和清场胜利条件。
			{ u8"欢迎来到灼热冷藏站！这里没有固定的最后一波，要把僵尸打到无力再战。我的冰箱成了战场！", "anim_smalltalk" },
			{ u8"种植物除了阳光，还要付冰块！看好卡牌上的冰价和自己的库存，阳光够了也可能种不下。", "anim_mediumtalk" },
			{ u8"缺冰就点右上方的“冰块商店”，用阳光下单。冰要等配送才到账，一次只能送一单，别等用光才买！", "anim_blahblah" },
			{ u8"消灭敌方花冰块派来的僵尸，也能回收一部分冰块。守住防线，就能边打边补货！", "anim_mediumtalk" },
			{ u8"敌方指挥官也要花冰块出兵，还会看你的阵型调兵。它有定时补给，消灭你的植物也能赚冰！", "anim_mediumtalk" },
			{ u8"制冰工会替僵尸生产冰块，活得越久产得越多。尽快打掉它，也要护住自己的植物，别让对面越打越富！", "anim_blahblah" },
			{ u8"获胜要清掉所有敌人和在途援军，再让敌方买不起兵，或长期低库存、经营不盈利又攻不破防线。", "anim_mediumtalk" },
			{ u8"开局先种产阳光的植物、补好各路火力，再留些阳光提前购冰。守住家门，掏空它的冰箱！哇卜！", "anim_crazy" },
		} },
	}};

	constexpr std::array<const char*, 32> kRequiredTextureKeys = {{
		"IMAGE_REANIM_CRAZYDAVE_BEARD",
		"IMAGE_REANIM_CRAZYDAVE_BODY1",
		"IMAGE_REANIM_CRAZYDAVE_BODY2",
		"IMAGE_REANIM_CRAZYDAVE_EYE",
		"IMAGE_REANIM_CRAZYDAVE_EYEBROW",
		"IMAGE_REANIM_CRAZYDAVE_FOOT1",
		"IMAGE_REANIM_CRAZYDAVE_FOOT2",
		"IMAGE_REANIM_CRAZYDAVE_HANDINGHAND",
		"IMAGE_REANIM_CRAZYDAVE_HANDINGHAND2",
		"IMAGE_REANIM_CRAZYDAVE_HANDINGHAND3",
		"IMAGE_REANIM_CRAZYDAVE_HEAD",
		"IMAGE_REANIM_CRAZYDAVE_INNERARM",
		"IMAGE_REANIM_CRAZYDAVE_INNERFINGER1",
		"IMAGE_REANIM_CRAZYDAVE_INNERFINGER2",
		"IMAGE_REANIM_CRAZYDAVE_INNERFINGER3",
		"IMAGE_REANIM_CRAZYDAVE_INNERFINGER4",
		"IMAGE_REANIM_CRAZYDAVE_INNERHAND",
		"IMAGE_REANIM_CRAZYDAVE_INNERPANTS",
		"IMAGE_REANIM_CRAZYDAVE_MOUTH1",
		"IMAGE_REANIM_CRAZYDAVE_MOUTH2",
		"IMAGE_REANIM_CRAZYDAVE_MOUTH3",
		"IMAGE_REANIM_CRAZYDAVE_MOUTH4",
		"IMAGE_REANIM_CRAZYDAVE_MOUTH5",
		"IMAGE_REANIM_CRAZYDAVE_MOUTH6",
		"IMAGE_REANIM_CRAZYDAVE_OUTERARM",
		"IMAGE_REANIM_CRAZYDAVE_OUTERFINGER1",
		"IMAGE_REANIM_CRAZYDAVE_OUTERFINGER2",
		"IMAGE_REANIM_CRAZYDAVE_OUTERFINGER3",
		"IMAGE_REANIM_CRAZYDAVE_OUTERFINGER4",
		"IMAGE_REANIM_CRAZYDAVE_OUTERHAND",
		"IMAGE_REANIM_CRAZYDAVE_POT",
		"IMAGE_REANIM_CRAZYDAVE_POT_INSIDE",
	}};

	// 原版 Foley 分组：短/长/超长各随机三条，疯狂语气固定一条；两条尖叫留给未来特殊台词。
	const std::array<const std::string*, 12> kRequiredVoiceSoundKeys = {{
		&ResourceKeys::Sounds::SOUND_CRAZYDAVESHORT1,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVESHORT2,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVESHORT3,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVELONG1,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVELONG2,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVELONG3,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVEEXTRALONG1,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVEEXTRALONG2,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVEEXTRALONG3,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVECRAZY,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVESCREAM,
		&ResourceKeys::Sounds::SOUND_CRAZYDAVESCREAM2,
	}};

	const std::string& SelectVoiceSoundKey(const std::string& talkTrack)
	{
		if (talkTrack == "anim_crazy") return *kRequiredVoiceSoundKeys[9];
		const int groupStart = talkTrack == "anim_blahblah" ? 6
			: talkTrack == "anim_mediumtalk" ? 3 : 0;
		return *kRequiredVoiceSoundKeys[groupStart + GameRandom::Range(0, 2)];
	}

	std::string VoiceGroupName(const std::string& talkTrack)
	{
		if (talkTrack == "anim_crazy") return "CRAZY";
		if (talkTrack == "anim_blahblah") return "EXTRALONG";
		if (talkTrack == "anim_mediumtalk") return "LONG";
		return "SHORT";
	}

	const Conversation* FindConversation(int level)
	{
		const auto it = std::find_if(kConversations.begin(), kConversations.end(),
			[level](const Conversation& conversation) { return conversation.level == level; });
		return it == kConversations.end() ? nullptr : &*it;
	}

	/** 以矩形和圆组合出稳定圆角框，避免为一次性气泡引入额外贴图资源。 */
	void FillRoundedRect(Graphics* g, float x, float y, float width, float height,
		float radius, const glm::vec4& color)
	{
		g->FillRect(x + radius, y, width - radius * 2.0f, height, color);
		g->FillRect(x, y + radius, width, height - radius * 2.0f, color);
		g->FillCircle(x + radius, y + radius, radius, color);
		g->FillCircle(x + width - radius, y + radius, radius, color);
		g->FillCircle(x + radius, y + height - radius, radius, color);
		g->FillCircle(x + width - radius, y + height - radius, radius, color);
	}

	/** 逐 UTF-8 字符测宽换行，让中英混排台词与最终字体渲染口径一致。 */
	std::vector<std::string> WrapText(Graphics* g, const std::string& text)
	{
		static const std::string kClosingPunctuation = u8"，。！？；：、）》】”’…";
		std::vector<std::string> lines;
		std::string line;
		std::size_t offset = 0;
		while (offset < text.size()) {
			const unsigned char first = static_cast<unsigned char>(text[offset]);
			std::size_t length = 1;
			if ((first & 0xE0) == 0xC0) length = 2;
			else if ((first & 0xF0) == 0xE0) length = 3;
			else if ((first & 0xF8) == 0xF0) length = 4;
			if (offset + length > text.size()) break;

			const std::string character = text.substr(offset, length);
			const std::string candidate = line + character;
			const bool mayHangPunctuation = kClosingPunctuation.find(character)
				!= std::string::npos;
			if (!line.empty() && !mayHangPunctuation
				&& g->MeasureTextWidth(candidate,
					ResourceKeys::Fonts::FONT_FZCQ, kTextFontSize) > kTextWidth) {
				lines.push_back(line);
				line.clear();
				continue;
			}
			line += character;
			offset += length;
		}
		if (!line.empty()) lines.push_back(std::move(line));
		return lines;
	}

	/** 绘制从气泡指向戴夫的填充尾巴；扫描线做法在全部渲染后端保持一致。 */
	void DrawBubbleTail(Graphics* g, const glm::vec4& border, const glm::vec4& fill)
	{
		constexpr float topY = kBubbleY + 110.0f;    // 尾巴与气泡相接的上端 Y
		constexpr float tipX = kBubbleX - 62.0f;      // 尾巴尖端 X
		for (int row = 0; row <= 70; ++row) {
			const float y = topY + static_cast<float>(row);
			const float left = kBubbleX
				- (kBubbleX - tipX) * static_cast<float>(row) / 70.0f;
			const float right = row <= 52
				? kBubbleX
				: kBubbleX - (kBubbleX - tipX) * static_cast<float>(row - 52) / 18.0f;
			g->DrawLine(left, y, std::max(left, right), y, border);
			if (right - left > 4.0f) {
				g->DrawLine(left + 2.0f, y, right - 2.0f, y, fill);
			}
		}
	}
}

CrazyDaveDialog::~CrazyDaveDialog()
{
	StopCurrentVoice();
}

bool CrazyDaveDialog::SupportsLevel(int level)
{
	return FindConversation(level) != nullptr;
}

int CrazyDaveDialog::GetRequiredTextureCount()
{
	return static_cast<int>(kRequiredTextureKeys.size());
}

int CrazyDaveDialog::GetLoadedRequiredTextureCount()
{
	int loaded = 0;
	auto& resources = ResourceManager::GetInstance();
	for (const char* key : kRequiredTextureKeys) {
		if (resources.GetTexture(key, false)) ++loaded;
	}
	return loaded;
}

int CrazyDaveDialog::GetRequiredVoiceSoundCount()
{
	return static_cast<int>(kRequiredVoiceSoundKeys.size());
}

int CrazyDaveDialog::GetLoadedRequiredVoiceSoundCount()
{
	int loaded = 0;
	auto& resources = ResourceManager::GetInstance();
	for (const std::string* key : kRequiredVoiceSoundKeys) {
		if (resources.HasSound(*key)) ++loaded;
	}
	return loaded;
}

int CrazyDaveDialog::GetVoicePlayRequestCount()
{
	int requests = 0;
	for (const std::string* key : kRequiredVoiceSoundKeys) {
		requests += AudioSystem::GetSoundPlayRequestCount(*key);
	}
	return requests;
}

bool CrazyDaveDialog::Start(int level, CompletionCallback onCompleted)
{
	const Conversation* conversation = FindConversation(level);
	auto& resources = ResourceManager::GetInstance();
	if (!conversation
		|| !resources.HasReanimation(ResourceKeys::Reanimations::REANIM_CRAZY_DAVE)
		|| GetLoadedRequiredTextureCount() != GetRequiredTextureCount()
		|| GetLoadedRequiredVoiceSoundCount() != GetRequiredVoiceSoundCount()) {
		LOG_ERROR("CrazyDave") << "无法开始关卡闲聊：配置或 CrazyDave 资源闭环不完整，level="
			<< level << " parts=" << GetLoadedRequiredTextureCount()
			<< "/" << GetRequiredTextureCount() << " voices="
			<< GetLoadedRequiredVoiceSoundCount() << "/" << GetRequiredVoiceSoundCount();
		return false;
	}

	auto reanimation = resources.GetReanimation(ResourceKeys::Reanimations::REANIM_CRAZY_DAVE);
	if (!reanimation) return false;

	mAnimator = std::make_shared<Animator>(std::move(reanimation));
	if (!mAnimator->PlayTrackOnce("anim_enter", "anim_idle", 1.0f, 0.0f, 1.0f, 0.1f)) {
		mAnimator.reset();
		return false;
	}

	mLevel = level;
	mMessageIndex = 0;
	mMessages.clear();
	mMessages.reserve(conversation->messages.size());
	for (const ScriptMessage& message : conversation->messages) {
		mMessages.push_back({ message.text, message.talkTrack });
	}
	mOnCompleted = std::move(onCompleted);
	mCurrentVoiceSoundKey.clear();
	mCurrentVoiceGroupName.clear();
	mPhase = Phase::ENTERING;
	return true;
}

void CrazyDaveDialog::Update()
{
	if (!IsActive() || !mAnimator) return;
	mAnimator->Update();

	if (mPhase == Phase::ENTERING
		&& mAnimator->GetCurrentTrackName() == "anim_idle") {
		BeginCurrentMessage();
	}
	else if (mPhase == Phase::LEAVING && !mAnimator->IsPlaying()) {
		Finish();
		return;
	}

	auto& input = GameAPP::GetInstance().GetInputHandler();
	if (input.IsKeyPressed(SDLK_ESCAPE)) {
		Skip();
	}
	else if (input.IsMouseButtonPressed(SDL_BUTTON_LEFT)
		|| input.IsKeyPressed(SDLK_RETURN)
		|| input.IsKeyPressed(SDLK_SPACE)) {
		Advance();
	}
}

void CrazyDaveDialog::Draw(Graphics* g)
{
	if (!g || !IsActive() || !mAnimator) return;

	const glm::vec4 dim(20.0f, 22.0f, 18.0f, 118.0f);
	const glm::vec4 shadow(22.0f, 17.0f, 10.0f, 150.0f);
	const glm::vec4 border(79.0f, 57.0f, 28.0f, 255.0f);
	const glm::vec4 cream(255.0f, 247.0f, 211.0f, 255.0f);
	const glm::vec4 ink(55.0f, 43.0f, 25.0f, 255.0f);
	const glm::vec4 hint(114.0f, 88.0f, 50.0f, 255.0f);

	g->FillRect(0.0f, 0.0f, static_cast<float>(SCENE_WIDTH),
		static_cast<float>(SCENE_HEIGHT), dim);
	mAnimator->Draw(g, kDaveBaseX, kDaveBaseY, kDaveScale);

	if (mPhase == Phase::LEAVING) return;
	DrawBubbleTail(g, border, cream);
	FillRoundedRect(g, kBubbleX + 7.0f, kBubbleY + 8.0f,
		kBubbleWidth, kBubbleHeight, kBubbleRadius, shadow);
	FillRoundedRect(g, kBubbleX, kBubbleY,
		kBubbleWidth, kBubbleHeight, kBubbleRadius, border);
	FillRoundedRect(g, kBubbleX + 4.0f, kBubbleY + 4.0f,
		kBubbleWidth - 8.0f, kBubbleHeight - 8.0f,
		kBubbleRadius - 4.0f, cream);

	const auto lines = WrapText(g, GetCurrentText());
	for (std::size_t index = 0; index < lines.size(); ++index) {
		g->DrawText(lines[index], ResourceKeys::Fonts::FONT_FZCQ, kTextFontSize,
			ink, kTextX, kTextY + kTextLineHeight * static_cast<float>(index));
	}

	const std::string footer = u8"点击 / Enter / 空格继续    Esc 跳过    "
		+ std::to_string(mMessageIndex + 1) + "/" + std::to_string(mMessages.size());
	const float footerWidth = g->MeasureTextWidth(footer,
		ResourceKeys::Fonts::FONT_FZCQ, kFooterFontSize);
	g->DrawText(footer, ResourceKeys::Fonts::FONT_FZCQ, kFooterFontSize, hint,
		kBubbleX + kBubbleWidth - footerWidth - 25.0f,
		kBubbleY + kBubbleHeight - 31.0f);
}

bool CrazyDaveDialog::Advance()
{
	if (!IsActive()) return false;
	if (mPhase == Phase::ENTERING) {
		BeginCurrentMessage();
		return true;
	}
	if (mPhase != Phase::TALKING) return false;

	if (mMessageIndex + 1 >= static_cast<int>(mMessages.size())) {
		BeginLeaving();
	}
	else {
		++mMessageIndex;
		BeginCurrentMessage();
	}
	return true;
}

bool CrazyDaveDialog::Skip()
{
	if (!IsActive()) return false;
	Finish();
	return true;
}

const char* CrazyDaveDialog::GetPhaseName() const
{
	switch (mPhase) {
	case Phase::INACTIVE: return "INACTIVE";
	case Phase::ENTERING: return "ENTERING";
	case Phase::TALKING: return "TALKING";
	case Phase::LEAVING: return "LEAVING";
	}
	return "UNKNOWN";
}

const std::string& CrazyDaveDialog::GetCurrentText() const
{
	static const std::string empty;
	if (mMessageIndex < 0 || mMessageIndex >= static_cast<int>(mMessages.size())) return empty;
	return mMessages[mMessageIndex].text;
}

std::string CrazyDaveDialog::GetCurrentTrackName() const
{
	return mAnimator ? mAnimator->GetCurrentTrackName() : std::string();
}

int CrazyDaveDialog::GetRenderedQuadCount() const
{
	return mAnimator ? mAnimator->GetLastRenderProbe().quadCount : 0;
}

bool CrazyDaveDialog::HasRenderedGeometry() const
{
	return mAnimator && mAnimator->GetLastRenderProbe().hasGeometry;
}

bool CrazyDaveDialog::UsedInstanceRenderPath() const
{
	return mAnimator && mAnimator->GetLastRenderProbe().usedInstancePath;
}

void CrazyDaveDialog::BeginCurrentMessage()
{
	if (!mAnimator || mMessages.empty()) {
		Finish();
		return;
	}
	mPhase = Phase::TALKING;
	const std::string& talkTrack = mMessages[mMessageIndex].talkTrack;
	if (!mAnimator->PlayTrackOnce(talkTrack, "anim_idle", 1.0f, 0.08f, 1.0f, 0.12f)) {
		mAnimator->PlayTrack("anim_idle", 1.0f, 0.0f);
	}
	PlayCurrentVoice();
}

void CrazyDaveDialog::BeginLeaving()
{
	StopCurrentVoice();
	mPhase = Phase::LEAVING;
	if (!mAnimator || !mAnimator->PlayTrackOnce("anim_leave", "", 1.0f, 0.08f)) {
		Finish();
	}
}

void CrazyDaveDialog::PlayCurrentVoice()
{
	StopCurrentVoice();
	if (mMessageIndex < 0 || mMessageIndex >= static_cast<int>(mMessages.size())) return;
	const std::string& talkTrack = mMessages[mMessageIndex].talkTrack;
	mCurrentVoiceGroupName = VoiceGroupName(talkTrack);
	mCurrentVoiceSoundKey = SelectVoiceSoundKey(talkTrack);
	AudioSystem::PlaySound(mCurrentVoiceSoundKey);
}

void CrazyDaveDialog::StopCurrentVoice()
{
	if (!mCurrentVoiceSoundKey.empty()) {
		AudioSystem::StopSound(mCurrentVoiceSoundKey);
	}
	mCurrentVoiceSoundKey.clear();
	mCurrentVoiceGroupName.clear();
}

void CrazyDaveDialog::Finish()
{
	if (!IsActive()) return;
	StopCurrentVoice();
	mPhase = Phase::INACTIVE;
	mAnimator.reset();
	auto callback = std::move(mOnCompleted);
	mOnCompleted = nullptr;
	if (callback) callback();
}
