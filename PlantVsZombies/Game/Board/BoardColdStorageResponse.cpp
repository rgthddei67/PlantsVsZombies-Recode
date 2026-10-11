#include "Board.h"
#include "Game/AI/ColdStoragePolicy.h"
#include "Game/Card.h"
#include "Game/CardSlotManager.h"
#include "Game/Bullet/Bullet.h"
#include "Game/Plant/CherryBomb.h"
#include "Game/Plant/CobCannon.h"
#include "Game/Plant/DoomShroom.h"
#include "Game/Plant/Jalapeno.h"
#include "Game/Zombie/IceWorkerZombie.h"
#include "Game/Zombie/HealerZombie.h"
#include "Game/Zombie/JackInTheBoxZombie.h"
#include "Game/Zombie/DancerZombie.h"
#include "Game/Zombie/BackupDancerZombie.h"
#include "Game/Zombie/DiggerZombie.h"
#include "Game/Zombie/LadderZombie.h"
#include "Game/Zombie/CatapultZombie.h"
#include "Game/Zombie/ZamboniZombie.h"
#include "Game/Zombie/DisasterEngineerZombie.h"
#include "Game/Zombie/ThermalSniperZombie.h"
#include "Game/Zombie/FloodMortarZombie.h"
#include "Game/Zombie/PressureShooterZombie.h"
#include "Game/Zombie/WeatherJammerZombie.h"
#include "Game/Zombie/HijackerZombie.h"
#include "Game/Zombie/InsulatorZombie.h"
#include "Game/Zombie/BalloonZombie.h"
#include "GameApp.h"
#include "DeltaTime.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kObservationInterval = .5f; // 公开应对机会的采样间隔，游戏秒；不补造漏采帧

/** 仅三种已核对正式几何的付费灰烬参与机会学习；其他工具保持原预测。 */
bool IsObservedPaidArea(PlantType type)
{
    return type == PlantType::PLANT_CHERRYBOMB || type == PlantType::PLANT_JALAPENO
        || type == PlantType::PLANT_DOOMSHROOM;
}

/** 已知复杂能力暂不迁移留牌经验；读取实际实体能力，不把不同技能压成可互换的一类。 */
bool HasComplexResponseAbility(const Zombie& target, bool worker)
{
    ZombieTemporalAbilityState ability;
    if ((!worker && target.CaptureTemporalAbilityState(ability))
        || target.GetMineSimulationSmashSeconds() > 0 || target.BlocksProjectileShieldBypass()
        || !target.CanBeAffectedByGroundHazards() || !target.CanBeKilledByMower()
        || target.ConsumesOtherMowersOnContact()) return true;
    // 这些实体拥有独立治疗/召唤/位移/投射阶段，并不都实现时间锚快照接口。
    return dynamic_cast<const HealerZombie*>(&target) || dynamic_cast<const JackInTheBoxZombie*>(&target)
        || dynamic_cast<const DancerZombie*>(&target) || dynamic_cast<const BackupDancerZombie*>(&target)
        || dynamic_cast<const DiggerZombie*>(&target) || dynamic_cast<const LadderZombie*>(&target)
        || dynamic_cast<const CatapultZombie*>(&target) || dynamic_cast<const ZamboniZombie*>(&target)
        || dynamic_cast<const DisasterEngineerZombie*>(&target) || dynamic_cast<const ThermalSniperZombie*>(&target)
        || dynamic_cast<const FloodMortarZombie*>(&target) || dynamic_cast<const PressureShooterZombie*>(&target)
        || dynamic_cast<const WeatherJammerZombie*>(&target) || dynamic_cast<const HijackerZombie*>(&target)
        || dynamic_cast<const InsulatorZombie*>(&target) || dynamic_cast<const BalloonZombie*>(&target);
}

/** 主线程只从已公开实体取得正式坐标、碰撞箱及炮击资格，不由单位名字推断。 */
ColdStorageResponse::TargetGeometry Geometry(const Zombie& target)
{
    const auto position=target.GetPosition();
    const auto* collider=target.GetColliderComponent();
    const SDL_FRect bounds=collider ? collider->GetBoundingBox()
        : SDL_FRect{position.x-25,position.y-65,50,100};
    return {target.mRow,position.x,position.y,bounds.x,bounds.y,bounds.w,bounds.h,
        true,true,true,target.CanBeAffectedByCobCannonExplosion()};
}

/** 只映射已核对的正式攻击种类；未知形状不会被宽包络冒充。 */
ColdStorageResponse::AreaGeometry Area(PlantType type,int row,const Vector& center)
{
    using ColdStorageResponse::AreaKind;
    const auto kind=type==PlantType::PLANT_CHERRYBOMB ? AreaKind::Cherry
        : type==PlantType::PLANT_JALAPENO ? AreaKind::Jalapeno
        : type==PlantType::PLANT_DOOMSHROOM ? AreaKind::Doom
        : type==PlantType::PLANT_COBCANNON ? AreaKind::Cob : AreaKind::Unknown;
    return {kind,row,center.x,center.y};
}

/** 学习与预测使用同一纯几何；矿石遮挡只在其正式Board接口校验。 */
bool AreaHits(const Board& board,PlantType type,int row,const Vector& center,const Zombie& target)
{
    return !board.MineBlocksSegment(center,target.GetPosition())
        && ColdStorageResponse::ClassifyAreaHit(Area(type,row,center),Geometry(target))==ColdStorageResponse::AreaHit::Hit;
}
}

int Board::GetColdStorageObservedSourceKey(const Card& card) const
{
    if (!mCardSlotManager || card.GetPlantType() == PlantType::PLANT_IMITATER
        || !IsObservedPaidArea(card.GetGameplayPlantType())) return 0;
    const auto& cards = mCardSlotManager->GetCards();
    for (size_t slot = 0; slot < cards.size() && slot < 64; ++slot)
        if (cards[slot] == &card) return static_cast<int>(slot) + 1;
    return 0;
}

void Board::ResetColdStorageResponse()
{
    mColdStorageResponse.Reset();
    mColdStorageResponseNextSample = 0;
    mColdStorageResponseLastTime = -1;
    mColdStorageResponseCardSignature = 0;
    mColdStorage.searchObservedDecision = {};
    mColdStorage.searchObservedResponse = {};
    mColdStorage.searchObservedCommitChecks = mColdStorage.searchObservedCommitRejections = 0;
    mColdStorage.searchObservedCommitRejected = false;
    mColdStorage.searchObservedCommitRiskCap = mColdStorage.searchObservedCommitOutstandingPaidCapital = 0;
}

void Board::NotifyColdStoragePaidAreaCard(const Card& card, const Plant& plant)
{
    if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || mIsLoadSave
        || GameAPP::mDevSpawnPaused
        || !GameAPP::GetInstance().mEnableMonteCarloAI || !ColdStoragePolicy::Get()
        || (GameAPP::mDevelopMode && (GameAPP::mDevFreePlant || GameAPP::mDevNoCooldown))
        || plant.IsPreview() || card.GetPlantType() == PlantType::PLANT_IMITATER) return;
    const auto type = card.GetGameplayPlantType();
    bool committed = IsObservedPaidArea(type);
    // 新建实体可能尚未执行Start；日间规则也必须检查，不能把暂时默认清醒误当施法。
    if (type == PlantType::PLANT_DOOMSHROOM)
        committed = !plant.GetSleepState() && GameAPP::GetInstance().GetBackgroundIsNight(mBackGround);
    if (type == PlantType::PLANT_INSTANT_COFFEE) {
        const auto* doom = dynamic_cast<const DoomShroom*>(GetNormalPlantAt(plant.mRow, plant.mColumn));
        committed = doom && doom->IsActive() && doom->GetSleepState() && !doom->IsWakingUp();
    }
    if (committed) mColdStorageResponse.PaidAreaCast(mColdStorage.elapsed);
}

void Board::UpdateColdStorageResponse()
{
    if (!GameAPP::GetInstance().mEnableMonteCarloAI || !ColdStoragePolicy::Get() || GameAPP::mDevSpawnPaused
        || (GameAPP::mDevelopMode && (GameAPP::mDevFreePlant || GameAPP::mDevNoCooldown))) {
        // 诊断暂停刷怪并非正常对抗；丢弃本局观察，避免恢复时把未观察间隔拼成拒用证据。
        if (mColdStorageResponseLastTime >= 0) ResetColdStorageResponse();
        return;
    }
    if (!IsColdStorage() || mBoardState != BoardState::GAME || mTrophySpawned || mIsLoadSave
        || mRows <= 0 || mColumns <= 0
        || DeltaTime::IsPaused() || !std::isfinite(mColdStorage.elapsed)) return;
    const float now = mColdStorage.elapsed;
    if (now < mColdStorageResponseLastTime) ResetColdStorageResponse();
    mColdStorageResponseLastTime = now;
    if (now < mColdStorageResponseNextSample) return;
    mColdStorageResponseNextSample = now + kObservationInterval;

    // 位号在同一实战卡组内稳定；重新选牌/换模仿目标时不能继承旧来源的观察。
    std::uint64_t cardSignature = 1469598103934665603ULL;
    if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
        const std::uint64_t type = card ? static_cast<unsigned>(card->GetGameplayPlantType()) + 1ULL : 0;
        const std::uint64_t actual = card ? static_cast<unsigned>(card->GetPlantType()) + 1ULL : 0;
        cardSignature = (cardSignature ^ type) * 1099511628211ULL;
        cardSignature = (cardSignature ^ actual) * 1099511628211ULL;
    }
    if (mColdStorageResponseCardSignature && cardSignature != mColdStorageResponseCardSignature)
        mColdStorageResponse.Reset();
    mColdStorageResponseCardSignature = cardSignature;

    ColdStorageResponse::Observation observation;
    std::vector<const Zombie*> visible;
    auto zombieIDs = mEntityRegistry.GetAllZombieIDs();
    std::sort(zombieIDs.begin(), zombieIDs.end());
    float capital = 0, nearestCells = (std::numeric_limits<float>::max)();
    bool eating = false,unknownCapital=false;
    std::vector<float> front(mRows, GetCellCenterPosition(0, 0).x);
    for (int row = 0; row < mRows; ++row) for (int col = 0; col < mColumns; ++col) {
        const Plant* plant = GetTopPlantAt(row, col);
        if (plant && plant->IsActive() && !plant->IsPreview() && plant->CanBeEaten())
            front[row] = std::max(front[row], plant->GetPosition().x);
    }
    observation.targetSignature = 1469598103934665603ULL;
    for (int id : zombieIDs) {
        const Zombie* target = mEntityRegistry.GetZombie(id);
        if (!target || !target->IsActive() || target->IsPreview() || target->IsDying()
            || target->IsMindControlled() || !target->HasHead() || target->mRow < 0 || target->mRow >= mRows
            || IsZombieObscuredByFog(target)
            || (!target->CanBeTargetedByProjectile(false) && !target->CanBeTargetedByProjectile(true))) continue;
        const Vector position = target->GetPosition();
        if (!std::isfinite(position.x) || !std::isfinite(position.y)
            || position.x < 0 || position.x > SCENE_WIDTH || position.y < 0 || position.y > SCENE_HEIGHT) continue;
        // 从这里开始才投影实体身份、位置、生命和能力；雾后/屏外坐标不会进入经验模型。
        visible.push_back(target);
        const auto* worker = dynamic_cast<const IceWorkerZombie*>(target);
        const bool economic = worker && worker->HasIceMachine();
        const bool complex = HasComplexResponseAbility(*target, worker != nullptr);
        const float price = static_cast<float>(GetZombieIceCost(target->mZombieType));
        unknownCapital|=!std::isfinite(price) || price<=0;
        const float distanceCells = (position.x - front[target->mRow]) / CELL_COLLIDER_SIZE_X;
        const bool bitePressure = target->IsEating() || distanceCells <= 2;
        observation.targets.push_back({id, target->mRow, position.x, position.y,
            static_cast<float>(target->GetCountableExecutionHealth()), price, economic, bitePressure});
        observation.targetSignature = (observation.targetSignature ^ static_cast<std::uint64_t>(id)) * 1099511628211ULL;
        capital += price;
        nearestCells = std::min(nearestCells, distanceCells);
        eating |= bitePressure;
        observation.context.economicPresent |= economic;
        observation.context.complexPresent |= complex;
    }
    observation.visibleHostiles = !visible.empty();

    int cheapest = (std::numeric_limits<int>::max)();
    for (ZombieType type : mSpawnZombieList) {
        if (!ColdStoragePolicy::AllUnits() && GetColdStorageUnlockWave(type) > mColdStorage.decisions + 1) continue;
        bool legal = false;
        for (int row = 0; row < mRows; ++row) legal |= IsSpawnRowCompatible(type, row);
        const int price = GetZombieIceCost(type);
        if (legal && price > 0) cheapest = std::min(cheapest, price);
    }
    observation.context.exposedCapitalBand = ColdStorageResponse::CapitalBand(capital, static_cast<float>(cheapest));
    observation.context.nearestThreatBand = ColdStorageResponse::ThreatBand(nearestCells, eating);

    // 逐目标标记当前已承诺覆盖；它们仍计入完整资本，不能冒充被玩家额外容忍的资本。
    const auto markCovered=[&](PlantType type,int row,const Vector& center) {
        for(size_t index=0;index<visible.size();++index)
            observation.targets[index].committedCovered|=AreaHits(*this,type,row,center,*visible[index]);
    };
    for (int id : mEntityRegistry.GetAllPlantIDs()) {
        const Plant* plant = mEntityRegistry.GetPlant(id);
        if (!plant || !plant->IsActive() || plant->IsPreview()) continue;
        if (const auto* cannon = dynamic_cast<const CobCannon*>(plant)) {
            observation.context.readyFreeAreaCount += cannon->IsReady();
            if (cannon->GetPendingSimulationBlastDelay() >= 0)
                markCovered(PlantType::PLANT_COBCANNON,cannon->GetPendingTargetRow(),cannon->GetPendingTarget());
            continue;
        }
        const auto* cherry = dynamic_cast<const CherryBomb*>(plant);
        const auto* jalapeno = dynamic_cast<const Jalapeno*>(plant);
        const auto* doom = dynamic_cast<const DoomShroom*>(plant);
        // 樱桃/辣椒出生即已承诺；其Start尚未起播的同帧也不能制造未响应机会。
        const bool committed = cherry || jalapeno || (doom && (doom->GetExplosionTimeRemaining() >= 0
            || (!doom->GetSleepState() && GameAPP::GetInstance().GetBackgroundIsNight(mBackGround))));
        if (committed) markCovered(plant->GetPlacementType(),plant->mRow,plant->GetPosition());
    }
    for (int id : mEntityRegistry.GetAllBulletIDs()) {
        const Bullet* bullet = mEntityRegistry.GetBullet(id);
        if (!bullet || !bullet->IsActive() || !bullet->IsCobCannonMotion()) continue;
        markCovered(PlantType::PLANT_COBCANNON,bullet->GetCobTargetRow(),bullet->GetCobTarget());
    }
    float uncoveredCapital=0;
    for(const auto& target:observation.targets) {
        observation.context.committedCoveragePresent|=target.committedCovered;
        if(!target.committedCovered) uncoveredCapital+=target.nominalCapital;
    }
    observation.context.uncoveredCapitalBand=ColdStorageResponse::CapitalBand(uncoveredCapital,static_cast<float>(cheapest));

    if (mCardSlotManager) for (const Card* card : mCardSlotManager->GetCards()) {
        if (!card || !card->IsReady()) continue;
        const int sourceKey = GetColdStorageObservedSourceKey(*card);
        if (sourceKey <= 0) continue;
        const auto type = card->GetGameplayPlantType();
        const bool needsCoffee = type == PlantType::PLANT_DOOMSHROOM
            && !GameAPP::GetInstance().GetBackgroundIsNight(mBackGround);
        int sun = card->GetSunCost(), ice = GetPlantIcePaymentCost(type);
        if (sun < 0) continue;
        if (needsCoffee) {
            const Card* coffee = nullptr;
            for (const Card* other : mCardSlotManager->GetCards())
                if (other && other->GetPlantType() == PlantType::PLANT_INSTANT_COFFEE
                    && other->IsReady() && other->GetSunCost() >= 0
                    && (!coffee || other->GetSunCost() < coffee->GetSunCost())) coffee = other;
            if (!coffee || !HasPlantingQuota(PlantType::PLANT_INSTANT_COFFEE)
                || !MiniGame::AllowsPlant(mLevel, PlantType::PLANT_INSTANT_COFFEE)) continue;
            sun += coffee->GetSunCost();
            ice += GetPlantIcePaymentCost(PlantType::PLANT_INSTANT_COFFEE);
        }
        if (mSun < sun || mColdStorage.playerIce < ice) continue;
        float maximumCoveredCapital = 0,maximumUncoveredCapital=0;
        // 保留每张牌所有当前合法落点中的最大可见覆盖，分路经验不能替集中爆区背书。
        for (int row = 0; row < mRows; ++row) for (int col = 0; col < mColumns; ++col) {
            const Cell* cell = GetCell(row, col);
            if (!cell || cell->GetNormalPlantID() != NULL_PLANT_ID || !CanPlantAt(type, row, col)) continue;
            // 当前合法Doom空格已经满足咖啡的地形/冰冻/弹坑条件；预期新株睡眠，唯一额外占位是overlay。
            // 不创建临时实体，也不把需要铲除或未来到账资金的落点当作真机会。
            if (needsCoffee && cell->GetOverlayPlantID() != NULL_PLANT_ID) continue;
            const Vector center = GetCellCenterPosition(row, col);
            float coveredCapital = 0,uncoveredCapital=0;
            for (size_t index = 0; index < visible.size(); ++index)
                if (AreaHits(*this, type, row, center, *visible[index])) {
                    coveredCapital += observation.targets[index].nominalCapital;
                    if(!observation.targets[index].committedCovered) uncoveredCapital+=observation.targets[index].nominalCapital;
                }
            maximumCoveredCapital = std::max(maximumCoveredCapital, coveredCapital);
            maximumUncoveredCapital = std::max(maximumUncoveredCapital,uncoveredCapital);
        }
        if (maximumCoveredCapital > 0) {
            observation.context.readyPaidAreaMask |= std::uint64_t{1} << (sourceKey - 1);
            observation.context.coveredCapitalBands[sourceKey - 1] = ColdStorageResponse::CapitalBand(
                maximumCoveredCapital, static_cast<float>(cheapest));
            if(maximumUncoveredCapital>0) observation.context.learnablePaidAreaMask|=std::uint64_t{1}<<(sourceKey-1);
            observation.context.uncoveredCoveredCapitalBands[sourceKey-1]=ColdStorageResponse::CapitalBand(
                maximumUncoveredCapital,static_cast<float>(cheapest));
        }
    }
    // 已承诺覆盖只删失受覆盖目标的机会，连续源年龄仍由完整合法mask推进。
    // 矿石遮挡尚未进入纯预测快照，该地形不能产生无法重建的乐观证据。
    observation.eligible = observation.visibleHostiles && !unknownCapital && cheapest<(std::numeric_limits<int>::max)()
        && !IsMineBackground() && !observation.context.complexPresent
        && observation.context.learnablePaidAreaMask && observation.context.nearestThreatBand > 0;
    mColdStorageResponse.Observe(now, observation);
}
