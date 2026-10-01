#include "PlantAlmanacScene.h"
#include "SceneManager.h"
#include "AdventureProgression.h"
#include "../GameApp.h"
#include "Plant/Plant.h"
#include "Game/Board/ColdStorageSkillRules.h"
#include "ClickableComponent.h"
#include "./Plant/GameDataManager.h"
#include "GameObjectManager.h"
#include <sstream>
#include <algorithm>
#include "../FileManager.h"

constexpr float CARD_INIT_POSITION_X = 70.0f;
constexpr float CARD_INIT_POSITION_Y = 120.0f;
constexpr int   ALMANAC_MAX_CARDS_PER_ROW = 12;
constexpr int   ALMANAC_CARD_H_SPACING = 1;
constexpr int   ALMANAC_CARD_V_SPACING = 4;

constexpr float PREVIEW_PLANT_X = 900.0f;
constexpr float PREVIEW_PLANT_Y = 198.0f;

// 描述书写区（羊皮纸内沿，屏幕坐标）。卡片图 IMAGE_ALMANAC_PLANTCARD 绘于 (745,110)，
// 324x484，下面这些是把图内书写区映射到屏幕后的边界。
constexpr float DESC_START_X    = 784.0f;        // 第一行起点（首行略缩进，避开装饰）
constexpr float DESC_WRAP_X     = 774.0f;        // 第二行起后续行起点
constexpr float DESC_MAX_X      = 1024.0f;       // 折行右界
constexpr float DESC_START_Y    = 337.0f;        // 第一行顶部
constexpr float DESC_BOTTOM_Y   = 565.0f;        // 书写区下沿（羊皮纸底边内留余量）
constexpr int   DESC_FONT_MAX   = 17;            // 默认/最大字号
constexpr int   DESC_FONT_MIN   = 10;            // 收缩下限（再小不可读）
constexpr float DESC_LINE_RATIO = 22.0f / 17.0f; // 行高随字号等比（原 17 号配 22px）

/** 普通图鉴保留卡片列表；奖励页居中展示详情，并提供下一关和首页入口。 */
void PlantAlmanacScene::BuildDrawCommands()
{
	Scene::BuildDrawCommands();
	AddTexture("IMAGE_ALMANAC_PLANTBACK", -90.0f, -20.0f, 1.0f, 1.0f, -1200, false);
	AddTexture("IMAGE_ALMANAC_PLANTCARD", 745.0f + GetInfoOffsetX(), 110.0f, 1.0f, 1.0f, -1000, false);
	AddTexture("IMAGE_ALMANAC_GROUNDDAY", 808.0f + GetInfoOffsetX(), 128.0f, 1.0f, 1.0f, -1100, false);

	mBackMenuButton = mUIManager.CreateButton(IsReward() ? Vector(90, 535) : Vector(7, 560),
		IsReward() ? Vector(230, 37) : Vector(162, 26));
	mBackMenuButton->SetAsCheckbox(false);
	mBackMenuButton->SetImageKeys(
		"IMAGE_ALMANAC_INDEXBUTTON",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT");

	mBackMenuButton->SetText(IsReward() ? u8"返回主菜单" : u8"返回索引",
		ResourceKeys::Fonts::FONT_FZJZ, IsReward() ? 22 : 18);
	mBackMenuButton->SetTextColor(glm::vec4(52, 51, 93, 255));
	mBackMenuButton->SetHoverTextColor(glm::vec4(52, 51, 93, 255));
	mBackMenuButton->SetClickCallBack([this](bool) {
		this->mReadyToSwitchAlmanacScene = true;
		});
	if (IsReward()) {
		auto nextButton = mUIManager.CreateButton(Vector(780, 535), Vector(230, 37));
		nextButton->SetAsCheckbox(false);
		nextButton->SetImageKeys("IMAGE_ALMANAC_INDEXBUTTON", "IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
			"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT", "IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT");
		nextButton->SetText(u8"继续（下一关）", ResourceKeys::Fonts::FONT_FZJZ, 22);
		nextButton->SetTextColor(glm::vec4(52, 51, 93, 255));
		nextButton->SetHoverTextColor(glm::vec4(52, 51, 93, 255));
		nextButton->SetEnabled(AdventureProgression::IsAdventureLevel(GameAPP::GetInstance().mAdventureLevel));
		nextButton->SetClickCallBack([this](bool) { mReadyForNextLevel = true; });
	}

	RegisterDrawCommand("PlantInfo",
		[this](Graphics* g) {
			auto& app = GameAPP::GetInstance();
			if (IsReward()) {
				app.DrawText(GameDataManager::GetInstance().IsSkillCard(mRewardPlant)
					? u8"获得新技能卡！" : u8"获得新植物！", Vector(445, 65),
					glm::vec4(255, 214, 104, 255), ResourceKeys::Fonts::FONT_FZJT, 32);
			}
			if (mCurrentPlantType == PlantType::PLANT_ICEVOUCHER)
				ColdStorageSkillRules::DrawVoucher(g, PREVIEW_PLANT_X + GetInfoOffsetX() - 75,
					PREVIEW_PLANT_Y - 48, 150, 96);
			if (!mCurrentPlantName.empty())
				app.DrawText(mCurrentPlantName, Vector(mPlantNameX + GetInfoOffsetX(), 290),
					glm::vec4(221, 157, 42, 255), ResourceKeys::Fonts::FONT_FZJZ, 24);

			float y = DESC_START_Y;
			for (size_t i = 0; i < mDescriptionLines.size(); i++) {
				float x = (i == 0) ? DESC_START_X : DESC_WRAP_X;
				app.DrawText(mDescriptionLines[i], Vector(x + GetInfoOffsetX(), y),
					glm::vec4(52, 51, 93, 255),
					ResourceKeys::Fonts::FONT_FZJZ, mDescriptionFontSize);
				y += mDescriptionLineHeight;
			}
		},
		LAYER_UI + 100);

	SortDrawCommands();
}

void PlantAlmanacScene::CreateAllCards()
{
	const auto& haveCards = GameAPP::GetInstance().mHaveCards;
	auto& gameMgr = GameDataManager::GetInstance();

	int cardCount = 0;
	for (const auto& plantType : haveCards) {
		if (!gameMgr.HasPlant(plantType)) continue;
		int row = cardCount / ALMANAC_MAX_CARDS_PER_ROW;
		int col = cardCount % ALMANAC_MAX_CARDS_PER_ROW;

		float posX = CARD_INIT_POSITION_X + col * (CARD_WIDTH + ALMANAC_CARD_H_SPACING);
		float posY = CARD_INIT_POSITION_Y + row * (CARD_HEIGHT + ALMANAC_CARD_V_SPACING);

		auto card = GameObjectManager::GetInstance().CreateGameObjectImmediate<Card>(
			LAYER_UI, plantType,
			gameMgr.GetPlantSunCost(plantType),
			gameMgr.GetPlantCooldown(plantType),
			true);

		if (auto transform = card->GetTransform()) {
			transform->SetPosition(Vector(posX, posY));
		}
		card->SetOriginalPosition(Vector(posX, posY));
		card->mIsUI = true;

		if (auto clickable = card->GetClickable()) {
			clickable->onClick = [this, plantType]() {
				OnCardClicked(plantType);
				};
		}

		mCards.push_back(card);
		cardCount++;
	}
}

void PlantAlmanacScene::OnCardClicked(PlantType type)
{
	DestroyPreviewPlant();
	CreatePreviewPlant(type);
	UpdatePlantInfo(type);
}

/** 图鉴与奖励共用无 Board 的展示植物，不触发战斗能力。 */
void PlantAlmanacScene::CreatePreviewPlant(PlantType type)
{
	if (GameDataManager::GetInstance().IsSkillCard(type)) return;
	auto plant = GameAPP::GetInstance().InstantiatePlant(type, nullptr, -1, -1, true);
	if (!plant) return;
	mPreviewPlant = plant;
	plant->SetPosition(Vector(PREVIEW_PLANT_X + GetInfoOffsetX(), PREVIEW_PLANT_Y));
}

void PlantAlmanacScene::DestroyPreviewPlant()
{
	if (auto plant = mPreviewPlant.lock()) {
		GameObjectManager::GetInstance().DestroyGameObject(plant);
		mPreviewPlant.reset();
	}
}

/** 奖励页可直接进入下一关或返回首页；普通图鉴仍返回索引。 */
void PlantAlmanacScene::Update()
{
	Scene::Update();
	if (mReadyForNextLevel) {
		auto& scenes = SceneManager::GetInstance();
		scenes.SetGlobalData("EnterLevel", std::to_string(GameAPP::GetInstance().mAdventureLevel));
		scenes.SwitchTo("GameScene");
		return;
	}

	if (mReadyToSwitchAlmanacScene) {
		mReadyToSwitchAlmanacScene = false;
		SceneManager::GetInstance().SwitchTo(IsReward() ? "MainMenuScene" : "AlmanacScene");
	}
}

/** 奖励页直接选中刚解锁的植物，普通入口仍展示全部已拥有卡片。 */
void PlantAlmanacScene::OnEnter()
{
	Scene::OnEnter();
	LoadInfoFile();
	if (IsReward()) {
		CreatePreviewPlant(mRewardPlant);
		UpdatePlantInfo(mRewardPlant);
		return;
	}
	CreateAllCards();

	const auto& haveCards = GameAPP::GetInstance().mHaveCards;
	if (!haveCards.empty()) {
		CreatePreviewPlant(haveCards[0]);
		UpdatePlantInfo(haveCards[0]);
	}
}

void PlantAlmanacScene::OnExit()
{
	mCards.clear();
	mPreviewPlant.reset();
	mBackMenuButton.reset();
	mInfoMap.clear();
	mCurrentPlantName.clear();
	mDescriptionLines.clear();
	mCurrentPlantType = PlantType::NUM_PLANT_TYPES;
	Scene::OnExit();
}

void PlantAlmanacScene::LoadInfoFile()
{
	mInfoMap.clear();
	std::string content = FileManager::LoadFileAsString("./resources/info.txt");
	if (content.empty()) return;
	std::istringstream file(content);

	std::string currentKey;
	std::string currentValue;

	auto trimRight = [](std::string& s) {
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
			s.back() == '\r' || s.back() == '\n'))
			s.pop_back();
		};

	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
			if (!currentKey.empty()) {
				trimRight(currentValue);
				mInfoMap[currentKey] = currentValue;
			}
			currentKey = line.substr(1, line.size() - 2);
			currentValue.clear();
		}
		else if (!currentKey.empty()) {
			if (!currentValue.empty())
				currentValue += '\n';
			currentValue += line;
		}
	}
	if (!currentKey.empty()) {
		trimRight(currentValue);
		mInfoMap[currentKey] = currentValue;
	}
}

void PlantAlmanacScene::UpdatePlantInfo(PlantType type)
{
	mCurrentPlantType = type;
	std::string enumName = GameDataManager::GetInstance().PlantTypeToEnumName(type);

	auto nameIt = mInfoMap.find(enumName);
	mCurrentPlantName = (nameIt != mInfoMap.end()) ? nameIt->second : "";

	mPlantNameX = 899.5f;
	if (!mCurrentPlantName.empty()) {
		TTF_Font* font = ResourceManager::GetInstance().GetFont(
			ResourceKeys::Fonts::FONT_FZJZ, 24);
		if (font) {
			int tw = 0, th = 0;
			TTF_SizeUTF8(font, mCurrentPlantName.c_str(), &tw, &th);
			mPlantNameX = (761.0f + 1038.0f - tw) / 2.0f;
		}
	}

	auto descIt = mInfoMap.find(enumName + "_DESCRIPTION");
	std::string description = (descIt != mInfoMap.end()) ? descIt->second : "";

	// 按书写区高度自动收缩字号：从最大字号往下试，直到整段折行后能装进 [START_Y, BOTTOM_Y]。
	// 字号变小 → 行高变小 + 每行容字更多 → 总高单调下降，线性试探即可收敛。最小字号兜底。
	mDescriptionLines.clear();
	mDescriptionFontSize = DESC_FONT_MIN;
	mDescriptionLineHeight = DESC_FONT_MIN * DESC_LINE_RATIO;
	for (int fs = DESC_FONT_MAX; fs >= DESC_FONT_MIN; --fs) {
		auto lines = WrapText(description, DESC_START_X, DESC_MAX_X, DESC_WRAP_X,
			ResourceKeys::Fonts::FONT_FZJZ, fs);
		float lineHeight = fs * DESC_LINE_RATIO;
		mDescriptionLines = std::move(lines);
		mDescriptionFontSize = fs;
		mDescriptionLineHeight = lineHeight;
		if (DESC_START_Y + mDescriptionLines.size() * lineHeight <= DESC_BOTTOM_Y)
			break;
	}
}

std::vector<std::string> PlantAlmanacScene::WrapText(const std::string& text,
	float startX, float maxX, float wrapX,
	const std::string& fontKey, int fontSize)
{
	std::vector<std::string> lines;
	if (text.empty()) return lines;

	TTF_Font* font = ResourceManager::GetInstance().GetFont(fontKey, fontSize);

	std::string currentLine;
	float lineStartX = startX;

	size_t i = 0;
	while (i < text.size()) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		size_t charLen = 1;
		if ((c & 0x80) == 0)      charLen = 1;
		else if ((c & 0xE0) == 0xC0) charLen = 2;
		else if ((c & 0xF0) == 0xE0) charLen = 3;
		else if ((c & 0xF8) == 0xF0) charLen = 4;

		if (i + charLen > text.size()) break;
		std::string ch = text.substr(i, charLen);
		std::string testLine = currentLine + ch;

		int lineWidth = 0, lineHeight = 0;
		if (font)
			TTF_SizeUTF8(font, testLine.c_str(), &lineWidth, &lineHeight);
		else
			lineWidth = static_cast<int>(testLine.size()) * (fontSize / 2);

		if (lineStartX + lineWidth > maxX && !currentLine.empty()) {
			lines.push_back(currentLine);
			currentLine.clear();
			lineStartX = wrapX;
			continue;
		}

		currentLine += ch;
		i += charLen;
	}
	if (!currentLine.empty())
		lines.push_back(currentLine);

	return lines;
}
