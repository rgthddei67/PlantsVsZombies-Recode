#pragma once
#include "Workshop.h"

namespace Workshop {
/** 独立练习只拥有自己的 World。操作复用正式事务，既不引用 Board，也不保存到玩家基地。 */
class Practice {
public:
    enum class Tool { Inspect, Belt, Feeder, WireA, WireB };
    static constexpr int Grower = 21, Conveyor = 22, Feeder = 23, Sensor = 5, Inverter = 7;
    Practice();
    void Reset(int lesson);
    /** 玩家练习控件入口；无任意跳步骤或写正式存档能力。 */
    bool Act(const std::string& action, int value = -1);
    /** 练习秒数独立于被暂停的正式基地；每个验证步骤达成后自动暂停。 */
    void Advance(float seconds);
    const World& Model() const { return mWorld; }
    int Lesson() const { return mLesson; }
    int Step() const { return mStep; }
    int StepCount() const;
    bool Complete() const { return mStep >= StepCount(); }
    bool Running() const { return mRunning; }
    int Selected() const { return mSelected; }
    int Source() const { return mSource; }
    Tool SelectedTool() const { return mTool; }
    int EnemyCount() const { return mEnemies[2]; }
    const std::string& Notice() const { return mNotice; }
    static const char* Name(int lesson);
    std::string Instruction() const;
    std::string Hint() const;
    int HighlightCell() const;
    std::string HighlightControl() const;
    nlohmann::json Inspect() const;
private:
    World mWorld;
    int mLesson = 0, mStep = 0, mSelected = -1, mSource = -1;
    Tool mTool = Tool::Inspect;
    bool mRunning = false, mLowStockSet = false;
    float mLessonTime = 0, mPowerClock = 0;
    int mProducedBeforeLow = 0;
    std::array<int, Rows> mEnemies{};
    std::string mNotice;
    bool ClickCell(int index);
    void CheckStep(bool simulated = false);
    bool SetStock(int amount);
};
}
