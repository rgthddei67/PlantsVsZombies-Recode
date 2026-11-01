#pragma once
#include "Workshop.h"

namespace Workshop {
/** 独立练习只拥有自己的 World。操作复用正式事务，既不引用 Board，也不保存到玩家基地。 */
class Practice {
public:
    enum class Tool { Inspect, Belt, Feeder, WireA, WireB, Power };
    static constexpr int LessonCount = 5; // 四节引导课和一节无高亮的独立验收
    static constexpr int ChallengeLesson = 4; // 最后一课只给目标，检验实际停产与重启
    static constexpr int BackgroundGrower = 25; // 独立任务里必须保持生产的第二条产线
    static constexpr int Grower = 21, Conveyor = 22, Feeder = 23, Sensor = 5, Inverter = 7;
    static constexpr int PowerBox = StarterPowerBox, BackgroundBox = StarterPowerBox + 1;
    Practice();
    void Reset(int lesson);
    /** 玩家练习控件入口；无任意跳步骤或写正式存档能力。 */
    bool Act(const std::string& action, int value = -1);
    /** 练习秒数独立于被暂停的正式基地；引导步骤达成后暂停，独立验收连续检查两个条件。 */
    void Advance(float seconds);
    const World& Model() const { return mWorld; }
    int Lesson() const { return mLesson; }
    int Step() const { return mStep; }
    int StepCount() const;
    bool Complete() const { return mStep >= StepCount(); }
    bool Running() const { return mRunning; }
    bool IsChallenge() const { return mLesson == ChallengeLesson; }
    bool Verifying() const { return mVerifying; }
    int Selected() const { return mSelected; }
    int Source() const { return mSource; }
    Tool SelectedTool() const { return mTool; }
    int EnemyCount() const { return mEnemies[2]; }
    const std::string& Notice() const { return mNotice; }
    static const char* Name(int lesson);
    /** 课程跟随所需元件的实际解锁进度，基础物流与手动用电从开局提供。 */
    static int UnlockWave(int lesson);
    std::string Instruction() const;
    std::string Hint() const;
    int HighlightCell() const;
    std::string HighlightControl() const;
    nlohmann::json Inspect() const;
private:
    World mWorld;
    int mLesson = 0, mStep = 0, mSelected = -1, mSource = -1;
    Tool mTool = Tool::Inspect;
    bool mRunning = false, mLowStockSet = false, mVerifying = false;
    float mVerificationTime = 0;
    std::uint64_t mVerificationTargetProduced = 0, mVerificationBackgroundProduced = 0;
    float mLessonTime = 0, mPowerClock = 0;
    int mProducedBeforeLow = 0;
    std::array<int, Rows> mEnemies{};
    std::string mNotice;
    bool ClickCell(int index);
    void CheckStep(bool simulated = false);
    bool SetStock(int amount);
    /** 在隔离世界中重置测试物料，依次检验满额停机、低于门槛恢复；验证中禁止改接线。 */
    bool StartVerification();
    void CheckVerification(float dt);
};
}
