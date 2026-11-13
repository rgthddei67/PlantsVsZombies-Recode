#pragma once

#include "Zombie/ZombieType.h"
#include "Definit.h"
#include "Graphics.h"
#include <memory>
#include <vector>
#include <string>

class Board;
class Graphics;
class Zombie;

/** GameScene 独占的僵尸卡牌视图；付款与冷却始终由 Board 提交。 */
class IZombieCardBar {
public:
    explicit IZombieCardBar(Board* board);
    ~IZombieCardBar();
    void Update();
    void Draw(Graphics* graphics);
    void DrawPlacement(Graphics* graphics);
    int GetSelectedSlot() const { return mSelectedSlot; }
    int GetPage() const { return mPage; }
    /** 卡组恢复或测试夹具改动后只重建展示，不重置 Board 的剩余冷却。 */
    void Refresh();
private:
    Board* mBoard;
    std::vector<ZombieType> mTypes;
    std::vector<std::shared_ptr<Zombie>> mPortraits;
    std::vector<CachedText> mCostLabels;
    std::shared_ptr<Zombie> mPlacement;
    int mSelectedSlot = -1;
    int mPage = 0;
    int mHoveredSlot = -1;
    std::string mHint;
    float mHintRemaining = 0;
    void Select(int slot);
    void Cancel();
    void ClearPortraits();
    /** 与植物卡一样在主线程刷新费用文字，绘制阶段只消费缓存。 */
    void UpdateCostLabels();
    std::pair<int,int> HoveredCell() const;
    /** 只由未被拾取物消费的 Cell 点击提交，资格与付款交给 Board。 */
    void DeployAtHoveredCell();
};
