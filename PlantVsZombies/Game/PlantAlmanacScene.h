#pragma once
#ifndef _PLANTALMANAC_SCENE_H
#define _PLANTALMANAC_SCENE_H

#include "Scene.h"
#include "Card.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <SDL2/SDL_ttf.h>

class Plant;

class PlantAlmanacScene : public Scene {
private:
	PlantType mRewardPlant;
	bool mReadyForNextLevel = false;
	float GetInfoOffsetX() const { return IsReward() ? -357.0f : 0.0f; }
	std::shared_ptr<Button> mBackMenuButton;
	std::shared_ptr<Button> mPreviousPageButton;
	std::shared_ptr<Button> mNextPageButton;
	std::vector<Card*> mCards;
	int mCurrentPage = 0;
	std::weak_ptr<Plant> mPreviewPlant;

	PlantType mCurrentPlantType = PlantType::NUM_PLANT_TYPES;
	std::unordered_map<std::string, std::string> mInfoMap;
	std::string mCurrentPlantName;
	float mPlantNameX = 0.0f;
	std::vector<std::string> mDescriptionLines;
	int   mDescriptionFontSize = 17;     // 自动收缩后的描述字号
	float mDescriptionLineHeight = 22.0f; // 与字号等比的行高

	/** 按拥有顺序创建卡片，页外卡片同时停止更新、绘制和点击。 */
	void CreateAllCards();
	/** 同步当前页卡片和导航按钮，保留右侧已选植物详情。 */
	void RefreshCurrentPage();
	/** 按相对页数翻页；首页和末页不循环。 */
	void ChangePage(int delta);
	void OnCardClicked(PlantType type);
	void CreatePreviewPlant(PlantType type);
	void DestroyPreviewPlant();
	void LoadInfoFile();
	void UpdatePlantInfo(PlantType type);
	std::vector<std::string> WrapText(const std::string& text,
		float startX, float maxX, float wrapX,
		const std::string& fontKey, int fontSize);

public:
	/** 无参数为普通图鉴；指定植物为通关奖励页，共用资料与预览。 */
	explicit PlantAlmanacScene(PlantType reward = PlantType::NUM_PLANT_TYPES)
		: mRewardPlant(reward) {}
	bool IsReward() const { return mRewardPlant != PlantType::NUM_PLANT_TYPES; }
	PlantType GetSelectedPlant() const { return mCurrentPlantType; }
	const std::string& GetPlantName() const { return mCurrentPlantName; }
	size_t GetDescriptionLineCount() const { return mDescriptionLines.size(); }
	bool HasPreviewPlant() const { return !mPreviewPlant.expired(); }
	int GetCurrentPage() const { return mCurrentPage; }
	/** 由实际已创建的卡片数派生页数，空列表保留一个空页。 */
	int GetPageCount() const;
	/** 返回图鉴卡片的非拥有索引，供 AutoTest 检查实际活动状态。 */
	const std::vector<Card*>& GetCards() const { return mCards; }
	std::shared_ptr<Button> GetPreviousPageButton() const { return mPreviousPageButton; }
	std::shared_ptr<Button> GetNextPageButton() const { return mNextPageButton; }
	void OnEnter() override;
	void OnExit() override;
	void Update() override;

	bool mReadyToSwitchAlmanacScene = false;

protected:
	void BuildDrawCommands() override;
};

#endif
