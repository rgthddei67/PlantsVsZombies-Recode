#pragma once
#ifndef _CARD_H
#define _CARD_H

#include "GameObject.h"
#include "./Plant/PlantType.h"
#include "Transform.h"
#include "WeatherTypes.h"
#include "CardPresentation.h"

class CardSlotManager;
class ChooseCardUI;

/** 单张植物卡牌；直接拥有玩法、输入、绘制、缓存和移动状态。 */
class Card : public GameObject {
public:
	Card(PlantType plantType, int sunCost, float cooldown, bool isInChooseCardUI = false);

	void Start() override;
	void Update() override;
	void Draw(Graphics* g) override;

	bool GetIsInChooseCardUI() const { return mIsInChooseCardUI; }
	/** 切换选卡/实战上下文，并在对象已启动时同步点击行为。 */
	void SetIsInChooseCardUI(bool isInChooseCardUI);

	bool IsReady() const { return !mIsCooldown; }
	bool IsCooldown() const { return mIsCooldown; }
	bool IsSelected() const { return mIsSelected; }
	PlantType GetPlantType() const { return mPlantType; }
	/** 返回实战费用、合法性、预览和落种使用的类型；普通卡与身份相同。 */
	PlantType GetGameplayPlantType() const;
	PlantType GetImitaterTarget() const { return mImitaterTarget; }
	bool HasImitaterTarget() const;
	/** 设置模仿目标并同步目标当前 cost/cooldown/卡图；仅模仿者卡接受。 */
	bool SetImitaterTarget(PlantType target);
	/** 取消复制选择并恢复右侧入口的原始模仿者卡面。 */
	void ClearImitaterTarget();
	int GetSunCost() const { return mSunCost; }
	float GetCooldownTimer() const { return mCooldownTimer; }
	float GetCooldownTime() const { return mCooldownTime; }
	float GetCooldownProgress() const;
	WindDirection GetBloverDirection() const { return mBloverDirection; }

	/** 进入完整基础冷却；开发者无冷却模式下保持就绪。 */
	void StartCooldown();
	/** 从关卡存档恢复剩余冷却和基础时长，不改变存档字段语义。 */
	void RestoreCooldown(float timer, float time);
	/** 按当前阳光、次数上限和冷却状态立即同步显示。 */
	void ForceStateUpdate();
	void SetSelected(bool selected);
	/** 设置三叶草卡槽的下一株吹向；非法方向保持原值。 */
	void SetBloverDirection(WindDirection direction);
	/** 仅供三叶草卡槽右键在屋后/前线之间切换。 */
	void ToggleBloverDirection();

	/** 绑定 GameScene 独占的卡槽控制器；Card 不拥有该对象。 */
	void BindCardSlotManager(CardSlotManager* manager);
	/** 绑定选卡界面输入宿主；Card 不拥有该对象。 */
	void BindChooseCardUI(ChooseCardUI* chooseCardUI) { mChooseCardUI = chooseCardUI; }
	/** 只开关选卡碰撞，不影响绘制与移动；模态目标窗用它保留底层卡面。 */
	void SetChooseCardInputEnabled(bool enabled);
	CardSlotManager* GetCardSlotManager() const { return mCardSlotManager; }

	void SetOriginalPosition(const Vector& pos) { mOriginalPos = pos; }
	Vector GetOriginalPosition() const { return mOriginalPos; }
	void SetTargetPosition(const Vector& target);
	/** 立即回到选卡网格原位并结束移动，用于隐藏非当前页的未选卡。 */
	void SnapToOriginalPosition();
	/** 立即移动到模态目标网格，不改主选卡页原位。 */
	void SetPositionImmediate(const Vector& position);
	bool IsMoving() const { return mIsMoving; }

private:
	using VisualState = CardPresentation::State;
	CardSlotManager* mCardSlotManager = nullptr; // GameScene 独占，Card 仅保存非拥有观察指针
	ChooseCardUI* mChooseCardUI = nullptr; // ChooseCardUI 所有期覆盖选卡 Card

	PlantType mPlantType = PlantType::PLANT_PEASHOOTER;
	PlantType mImitaterTarget = PlantType::NUM_PLANT_TYPES;
	int mSunCost = 0;
	float mCooldownTimer = 0.0f;
	float mCooldownTime = 0.0f;
	bool mIsSelected = false;
	bool mIsCooldown = false;
	bool mIsInChooseCardUI = false;
	WindDirection mBloverDirection = WindDirection::TOWARD_FRONT;

	Vector mOriginalPos; // 选卡网格中的固定原位
	Vector mTargetPos; // 选卡飞行动画目标
	bool mIsMoving = false;
	float mMoveSpeed = 600.0f; // 卡牌飞行动画速度，单位：逻辑 px/s

	const Texture* mCardBackground = nullptr;
	const Texture* mCardNormal = nullptr;
	const Texture* mCardVariants = nullptr;
	const Texture* mPlantTexture = nullptr;
	VisualState mVisualState = VisualState::Ready;
	float mMaskFillAmount = 0.0f;
	bool mShowMask = false;
	CachedText mSunTextCache{};
	int mCachedSunValue = -1;

	void SetupComponents();
	void UpdateCooldown();
	void UpdateVisualState();
	void UpdateSunTextCache();
	void UpdateMovement();
	void ConfigureClickHandler();
	void SetCardGameClick();
	void SetCardChooseClick();

	void LoadTextures();
	void TransitionToWaitingSun();
	void TransitionToReady();
	void TransitionToCooling();
	void TransitionToClick();
	glm::vec4 GetCurrentColor() const;
	std::string GetPlantTextureKey() const;
	PlantType GetDisplayPlantType() const;
	void ReloadPlantTexture();
	void DrawCardBackground(Graphics* g, const Vector& position, const glm::vec4& color);
	void DrawPlantImage(Graphics* g, const Vector& position, const glm::vec4& color);
	void DrawCooldownMask(Graphics* g, const Vector& position);
	void DrawSunCost(Graphics* g, const Vector& position);
	void DrawSelectionHighlight(Graphics* g, const Vector& position);
	void DrawPlanternStatus(Graphics* g, const Vector& position);
	void DrawBloverDirection(Graphics* g, const Vector& position);
};

#endif
