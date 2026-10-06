#include "AlmanacScene.h"
#include "SceneManager.h"
#include "../GameApp.h"
#include "Plant/Plant.h"

void AlmanacScene::BuildDrawCommands()
{
	Scene::BuildDrawCommands();
	AddTexture("IMAGE_ALMANAC_INDEXBACK", -90.0f, -20.0f, 1.0f, 1.0f, -1000, false);

	mBackMenuButton = mUIManager.CreateButton(Vector(7, 560), Vector(162, 26));
	mBackMenuButton->SetAsCheckbox(false);
	mBackMenuButton->SetImageKeys(
		"IMAGE_ALMANAC_INDEXBUTTON",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT");

	mBackMenuButton->SetText(GameAPP::GetInstance().mGameInfoSaver.GetAlmanacReturnLevel() >= 0
		? "返回游戏" : "返回菜单", ResourceKeys::Fonts::FONT_FZJZ, 18);
	mBackMenuButton->SetTextColor(glm::vec4(52, 51, 93, 255));
	mBackMenuButton->SetHoverTextColor(glm::vec4(52, 51, 93, 255));
	mBackMenuButton->SetClickCallBack([this](bool) {
		this->mReadyToSwitchMainMenu = true;
		});

	mPlantButton = mUIManager.CreateButton(Vector(270, 380), Vector(162, 26));
	mPlantButton->SetAsCheckbox(false);
	mPlantButton->SetImageKeys(
		"IMAGE_ALMANAC_INDEXBUTTON",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT");
	mPlantButton->SetText("植物图鉴", ResourceKeys::Fonts::FONT_FZJZ, 18);
	mPlantButton->SetTextColor(glm::vec4(52, 51, 93, 255));
	mPlantButton->SetHoverTextColor(glm::vec4(52, 51, 93, 255));
	mPlantButton->SetClickCallBack([this](bool) {
		mReadyToSwitchPlantScene = true;
		});

	mZombieButton = mUIManager.CreateButton(Vector(660, 380), Vector(162, 26));
	mZombieButton->SetAsCheckbox(false);
	mZombieButton->SetImageKeys(
		"IMAGE_ALMANAC_INDEXBUTTON",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT",
		"IMAGE_ALMANAC_INDEXBUTTONHIGHLIGHT");
	mZombieButton->SetText("僵尸图鉴", ResourceKeys::Fonts::FONT_FZJZ, 18);
	mZombieButton->SetTextColor(glm::vec4(52, 51, 93, 255));
	mZombieButton->SetHoverTextColor(glm::vec4(52, 51, 93, 255));
	mZombieButton->SetClickCallBack([this](bool) {
		mReadyToSwitchZombieScene = true;
		});

	SortDrawCommands();
}

/** 处理图鉴页导航；索引返回按入口恢复原局或回到首页。 */
void AlmanacScene::Update()
{
	Scene::Update();

	if (mReadyToSwitchMainMenu) {
		mReadyToSwitchMainMenu = false;
		auto& saver = GameAPP::GetInstance().mGameInfoSaver;
		auto& scenes = SceneManager::GetInstance();
		if (saver.QueueAlmanacReturn()) {
			scenes.SetGlobalData("EnterLevel", std::to_string(saver.GetAlmanacReturnLevel()));
			scenes.SwitchTo("GameScene");
		}
		else {
			scenes.SwitchTo("MainMenuScene");
		}
		return;
	}
	if (mReadyToSwitchPlantScene) {
		mReadyToSwitchPlantScene = false;
		SceneManager::GetInstance().SwitchTo("PlantAlmanacScene");
		return;
	}
	if (mReadyToSwitchZombieScene) {
		mReadyToSwitchZombieScene = false;
		SceneManager::GetInstance().SwitchTo("ZombieAlmanacScene");
		return;
	}
}

void AlmanacScene::OnEnter()
{
	Scene::OnEnter();
	AudioSystem::PlayMusic(ResourceKeys::Music::MUSIC_CHOOSEYOURSEEDS, -1);
	auto plant = GameAPP::GetInstance().InstantiatePlant(
		PlantType::PLANT_SUNFLOWER, nullptr, -1, -1, true);
	mPlant = plant;
	plant->SetPosition(Vector(355, 310));

	auto zombie = GameAPP::GetInstance().InstantiateZombieFree(
		ZombieType::ZOMBIE_NORMAL, nullptr, 745.0f, 330.0f);
	mZombie = zombie;
}

void AlmanacScene::OnExit()
{
	mPlantButton.reset();
	mZombieButton.reset();
	mBackMenuButton.reset();
	Scene::OnExit();
}
