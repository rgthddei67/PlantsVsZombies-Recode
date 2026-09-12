#pragma once
#ifndef _CARD_SLOT_MANAGER_H
#define _CARD_SLOT_MANAGER_H

#include "Card.h"
#include "./Plant/PlantType.h"
#include "Game/Board/Board.h"
#include "Cell.h"
#include <vector>
#include <memory>
#include <functional>

class GameObject;
class Plant;

/** GameScene 独占的卡槽、手持预览与落种输入控制器。 */
class CardSlotManager {
private:
	std::vector<Card*> cards;  // 卡牌列表（观察者，所有权在 GameObjectManager）
	Card* selectedCard = nullptr;       // 当前选中的卡牌（观察者）
	Plant* plantPreview = nullptr;          // 植物预览（观察者，所有权在 GameObjectManager）
	Plant* cellPlantPreview = nullptr;

	// 常量参数
	Vector firstSlotPosition = Vector(64, -2); // 第一个卡牌槽的位置

	Board* mBoard = nullptr;
	Cell* mHoveredCell = nullptr;     // 当前鼠标悬停的Cell（观察者）
	int mRelocationSourceID = NULL_PLANT_ID; // 未提交来源只保存稳定 ID，取消/读档不自动执行
	bool mPlanternGearMenuOpen = false; // 纯 UI 瞬态；不进入关卡存档
	bool mPauseGameplayInputBlocked = false; // 普通空格暂停的附加门禁；非 GAME 状态始终禁止玩法输入
	bool mSuppressCobTargetRelease = false; // 进入瞄准态的同一次左键释放不得立刻提交炮击
	int mLastSun = 0; // 上次同步卡牌灰态的阳光值，按场景实例隔离
	bool mPreviewRenderProbeReady = false; // AutoTest：最近一帧是否真正提交了手持预览
	int mPreviewRenderMouseOffsetX = 0; // AutoTest：实际提交锚点相对鼠标 X，单位：逻辑 px
	int mPreviewRenderMouseOffsetY = 0; // AutoTest：实际提交锚点相对鼠标 Y，单位：逻辑 px

public:
	CardSlotManager(Board* board);
	~CardSlotManager();

	/** 安装 Cell 点击入口；由 GameScene 在 Board 完成构造后调用一次。 */
	void Start();
	/** 更新卡槽输入与卡牌状态；手持预览位置由 PrepareDraw 按最终相机同步。 */
	void Update();
	/** 在本帧相机确定后、GameObject 绘制前同步手持与落点预览。 */
	void PrepareDraw(Graphics* g);
	/** AutoTest 在绘制结束后记录手持预览实际提交位置与鼠标的逻辑像素差。 */
	void CapturePreviewRenderState(Graphics* g);
	bool IsPreviewRenderProbeReady() const { return mPreviewRenderProbeReady; }
	int GetPreviewRenderMouseOffsetX() const { return mPreviewRenderMouseOffsetX; }
	int GetPreviewRenderMouseOffsetY() const { return mPreviewRenderMouseOffsetY; }
	void UpdateAllCardsState();
	/** 地图射界预览只读取当前正式选卡与悬停格，不创建测试目标或参与索敌。 */
	PlantType GetPlacementPreviewType() const { return selectedCard ? selectedCard->GetGameplayPlantType() : PlantType::NUM_PLANT_TYPES; }
	const Cell* GetPlacementPreviewCell() const { return mHoveredCell; }
	int GetRelocationSourceID() const { return mRelocationSourceID; }
	/** 在场景 UI 阶段提示工具卡当前需要来源或目的地。 */
	void DrawRelocationHint(Graphics* g);

	// 卡牌操作
	void AddCard(Card* card);
	// 清空所有卡槽卡牌（销毁 GameObject）。用于生存模式轮间空槽重选。
	void ClearAllCards();
	void SelectCard(Card* card);
	void DeselectCard();
	/** 设置普通空格暂停的玩法输入门禁；不会销毁进入暂停前已拿起的植物预览。 */
	void SetPauseGameplayInputBlocked(bool blocked) { mPauseGameplayInputBlocked = blocked; }
	/** 只有正式战斗状态且未被普通暂停门禁时，卡槽与草坪才接收玩法输入。 */
	bool CanAcceptGameplayInput() const {
		return mBoard && mBoard->mBoardState == BoardState::GAME
			&& !mPauseGameplayInputBlocked;
	}
	/** 点击路灯花卡片或本体时切换挡位菜单。 */
	void TogglePlanternGearMenu();
	bool IsPlanternGearMenuOpen() const { return mPlanternGearMenuOpen; }
	/** 在场景顶层 UI 阶段绘制挡位菜单，确保它覆盖天气预报板。 */
	void DrawPlanternGearMenu(Graphics* g);

	bool CanAfford(int cost) const;   // 开发者作弊（无视阳光）守卫在 .cpp，避免头文件引 GameApp.h
	/** 同时检查阳光与该植物的本关累计种植次数。 */
	bool CanUsePlant(PlantType type, int cost) const;
	bool SpendSun(int cost);

	// 清理植物预览
	void DestroyPlantPreview();

	// 处理Cell点击
	void HandleCellClick(int row, int col);
	/** 按实际卡槽落种（slot 从 0 起）；成功返回空串，失败返回原因，不绕过费用、冷却或暂停门禁。 */
	std::string TryPlantFromSlot(int slot, int row, int col);

	// 获取当前选中的植物类型
	PlantType GetSelectedPlantType() const;

	// 获取卡牌信息
	Card* GetSelectedCard() const { return selectedCard; }
	int GetCurrentSun() const { return mBoard ? mBoard->GetSun() : 0; }
	Board* GetBoard() const { return mBoard; }
	const std::vector<Card*>& GetCards() const { return cards; }

private:
	void CreatePlantPreview(PlantType plantType);
	void UpdatePlantPreviewPosition(Graphics* g, const Vector& position);
	void UpdatePreviewToMouse(const Vector& mouseWorld);
	/** 将世界坐标解析为唯一格子；重叠边界固定按行列顺序归属。 */
	Cell* FindCellAtWorldPosition(const Vector& position) const;

	// 创建Cell悬停预览（透明）
	void CreateCellPlantPreview(PlantType plantType, Cell* cell);
	// 销毁Cell悬停预览
	void DestroyCellPlantPreview();

	// 检查是否可以在指定Cell放置植物
	bool CanPlaceInCell(Cell* cell) const;

	// 在指定Cell放置植物
	bool PlacePlantInCell(int row, int col);
	Card* FindPlanternCard() const;
	void UpdatePlanternGearMenuInput();
	/** 空手悬停于有效路灯花格时请求手型光标；手持状态保留格子原本的落种或工具语义。 */
	void UpdatePlanternHoverCursor() const;
	/** 路灯花本体交互与悬停共用的格子资格判定。 */
	bool HasInteractablePlanternAt(int row, int col) const;
	/** 空手悬停于已装填加农炮时请求手型光标；资格与正式点击入口保持一致。 */
	void UpdateCobCannonHoverCursor() const;
	/** 在全战场接收炮击落点释放，允许提交到没有 Cell 的草坪外可见区域。 */
	void UpdateCobCannonTargetingInput();
	/** 右键命中三叶草卡槽时切换方向；返回是否消费了本次右键。 */
	bool UpdateBloverDirectionInput();
	void ApplySelectedBloverDirection(Plant* plant) const;
};

#endif
