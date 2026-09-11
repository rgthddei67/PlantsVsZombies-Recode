#pragma once
#ifndef _CHOOSECARDUI_H
#define _CHOOSECARDUI_H
#include "GameObject.h"
#include "Card.h"
#include <vector>
#include <memory>

class Button;
class GameScene;
class ImitaterDialogOverlay;

class ChooseCardUI : public GameObject {
public:
	ChooseCardUI(GameScene* gameScene);
	~ChooseCardUI();

	void Update() override;
	/** 绘制选卡面板；小游戏在面板内补充固定卡组的玩法说明。 */
	void Draw(Graphics* g) override;

	// 后续添加卡牌的方法
	void AddCard(PlantType type);

	void RemoveCard(Card* card);

	void RemoveAllCards();

	Vector GetPosition() const {
		return GetTransform()->GetPosition();
	}
	/** 设置选卡面板逻辑坐标，并同步依附于面板右上角的按钮。 */
	void SetPosition(const Vector& position);

	// 切换卡牌选中状态，返回是否选中
	bool ToggleCardSelection(Card* card);
	/** Card 的统一点击入口；模仿者在这里打开独立目标选择层。 */
	bool HandleCardClick(Card* card);
	// 判断卡牌是否已选中
	bool IsCardSelected(Card* card) const;
	// 获取所有选中的卡牌
	const std::vector<Card*>& GetSelectedCards() const { return mSelectedCards; }
	/** 按玩家点击顺序导出当前选择，用于提交后记录上一次卡组。 */
	std::vector<PlantType> GetSelectedCardTypes();
	/** 导出含模仿目标的稳定记忆键；普通卡仍是原枚举名。 */
	std::vector<std::string> GetSelectedCardKeys() const;
	// 获取“一起摇滚吧”按钮
	std::shared_ptr<Button> GetButton() const { return mButton.lock(); }
	// 获取面板右上角的“上次选卡”按钮
	std::shared_ptr<Button> GetRestoreButton() const { return mRestoreButton.lock(); }
	// 获取当前两页之间切换方向的按钮
	std::shared_ptr<Button> GetPageButton() const { return mPageButton.lock(); }
	/** 用当前仍拥有且已注册的卡恢复上一次选择，并复用卡片目标位置动画。 */
	bool RestoreLastSelectedCards();
	// 添加冒险拥有卡；小游戏改用独立七卡池并自动预选
	void AddAllCard();
	// 转换卡牌所有权给卡槽管理器
	void TransferSelectedCardsTo(CardSlotManager* manager);
	// 按植物类型查找选卡界面中的卡牌（AutoTest 程序化选卡用）；找不到返回 nullptr
	Card* FindCardByType(PlantType type);
	/** 返回选卡面板右侧独立模仿者入口；它不参与普通卡池分页。 */
	Card* GetImitaterCard() const { return mImitaterCard; }
	/** 返回当前 0-based 页码与总页数，供 UI 状态检查和 AutoTest 使用。 */
	int GetCurrentPage() const { return mCurrentPage; }
	int GetPageCount() const;
	/** 按拥有顺序导出实际活动/隐藏的卡，验证分页没有留下可点击的叠卡。 */
	std::vector<PlantType> GetVisibleCardTypes() const;
	std::vector<PlantType> GetHiddenCardTypes() const;
	bool IsImitaterDialogOpen() const { return mImitaterDialogOpen; }
	/** 返回当前模态层临时 Card 所代表的目标类型，供 UI 回归验证。 */
	std::vector<PlantType> GetImitaterDialogOptionTypes() const;
	/** 目标弹窗独立分页；仅当前页临时卡可绘制和点击。 */
	int GetImitaterDialogPage() const { return mImitaterDialogPage; }
	int GetImitaterDialogPageCount() const;
	std::vector<PlantType> GetVisibleImitaterDialogOptionTypes() const;
	std::shared_ptr<Button> GetImitaterPreviousPageButton() const { return mImitaterPreviousPageButton.lock(); }
	std::shared_ptr<Button> GetImitaterNextPageButton() const { return mImitaterNextPageButton.lock(); }
	/** 返回满配卡组最后一张卡的右边缘，供卡槽底板按真实容量收口。 */
	static float GetGameSlotRightEdge();

private:
	friend class ImitaterDialogOverlay;
	const Texture* mCardUITexture = nullptr;
	const Texture* mImitaterAddOnTexture = nullptr; // 固定右侧背景，不属于会移动的 Card
	GameScene* mGameScene = nullptr;

	std::weak_ptr<Button> mButton;
	std::weak_ptr<Button> mRestoreButton;
	std::weak_ptr<Button> mPageButton;
	std::weak_ptr<Button> mImitaterCancelButton;
	std::weak_ptr<Button> mImitaterPreviousPageButton;
	std::weak_ptr<Button> mImitaterNextPageButton;

	std::vector<Card*> mCards;  // 存储选卡界面的卡牌（观察者，所有权在 GameObjectManager）
	Card* mImitaterCard = nullptr; // 原版右侧独立入口，不计入普通卡池分页
	ImitaterDialogOverlay* mImitaterDialogOverlay = nullptr; // 位于主卡之上、临时卡之下的模态背景
	std::vector<Card*> mImitaterDialogCards; // 弹窗临时灰卡；不移动主选卡 Card
	std::vector<Card*> mSelectedCards;   // 存储选中的卡牌对象
	int mCurrentPage = 0; // 0-based 当前页；现有完整卡池为两页
	bool mImitaterDialogOpen = false; // 独立目标选择层是否正在接管 Card 输入
	int mImitaterDialogPage = 0; // 弹窗独立的 0-based 页码，每次打开重置
	Card* mPendingImitaterCard = nullptr; // 等待目标的模仿者卡；观察者

	static constexpr int MAX_SELECTED = 11;              // 最大选择数量
	static constexpr float SLOT_START_X = 195;                  // 槽位起始 X 屏幕坐标
	static constexpr float SLOT_START_Y = -1;                    // 槽位起始 Y 屏幕坐标
	static constexpr int SLOT_SPACING = CARD_WIDTH + 3;       // 槽位间距

	static constexpr int MAX_CARDS_PER_ROW = 8;      // 每行最多8张
	static constexpr int MAX_CARD_ROWS_PER_PAGE = 6; // 每页保持现有六行完整可见区域
	static constexpr int CARDS_PER_PAGE =
		MAX_CARDS_PER_ROW * MAX_CARD_ROWS_PER_PAGE; // 每页 48 张
	static constexpr int CARD_HORIZONTAL_SPACING = 3; // 水平间距
	static constexpr int CARD_VERTICAL_SPACING = 4;   // 垂直间距（
	static constexpr float START_X = 210;                 // 第一张卡牌的起始X坐标 屏幕坐标
	static constexpr float START_Y = 115;                // 第一行起始Y坐标  屏幕坐标
	static constexpr int IMITATER_DIALOG_CARDS_PER_ROW = 9; // 目标网格每行9张
	static constexpr int IMITATER_DIALOG_CARDS_PER_PAGE = IMITATER_DIALOG_CARDS_PER_ROW * 5; // 每页45张，底部预留导航
	static constexpr float IMITATER_DIALOG_START_X = 310.0f; // 模态目标网格首列 X，单位：UI px
	static constexpr float IMITATER_DIALOG_START_Y = 90.0f; // 模态目标网格首行 Y，单位：UI px

	// 更新所有卡牌的目标位置（根据选中状态）
	void UpdateTargetPositions(bool playSound = true);
	void SyncRestoreButtonPosition();
	void SyncPageButtonPosition();
	void RefreshRestoreButtonState();
	void RefreshPageButtonState();
	void SyncCardPageVisibility();
	void TogglePage();
	std::vector<Card*> ResolveRestorableCards(bool applyImitaterTargets);
	/** 打开模仿目标模态层，并为每个合法目标创建独立临时 Card。 */
	bool OpenImitaterDialog(Card* imitaterCard);
	/** 关闭目标模态层，销毁临时对象并恢复主选卡输入。 */
	void CloseImitaterDialog();
	/** 统一停用并延迟销毁本轮目标 Card。 */
	void DestroyImitaterDialogCards();
	/** 把临时 Card 的目标提交到主模仿者 Card。 */
	bool SelectImitaterTarget(Card* targetCard);
	/** 按模态状态同步开始、恢复、分页与取消按钮。 */
	void RefreshImitaterDialogControls();
	/** 在有效页范围内切换目标页，同步临时卡输入和导航边界。 */
	void ChangeImitaterDialogPage(int delta);
	/** 在主选卡 Card 之后绘制遮罩、面板和标题。 */
	void DrawImitaterDialog(Graphics* g) const;
	/** 停用并延迟销毁独立模态背景对象。 */
	void DestroyImitaterDialogOverlay();
};

#endif
