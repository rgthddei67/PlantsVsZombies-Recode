#include "Board.h"
#include "Game/Workshop/Workshop.h"
#include "Game/Plant/Plant.h"
#include "Game/Zombie/Zombie.h"
#include "Game/AudioSystem.h"
#include "GameInfoSaver.h"
#include "DeltaTime.h"
#include "ResourceKeys.h"
#include <limits>
#include <stdexcept>

namespace {
constexpr float kWorkshopSpawnX = 1060.0f; // 复用草坪右侧正常出生区域，单位：世界像素
constexpr int kWorkshopFirstShooterColumn = 2; // 第一株站第三列，增援依次补到房屋方向
}

void Board::InitializeWorkshopFrontline()
{
    if (!mWorkshop) return;
    mMaxWave = std::numeric_limits<int>::max(); // 工坊无奖杯终局，清波由独立经营状态结算
    mBoardState = BoardState::GAME;
    for (int row = 0; row < Workshop::Rows; ++row)
        CreatePlant(PlantType::PLANT_PEASHOOTER, row, kWorkshopFirstShooterColumn);
    RefreshWorkshopObservation();
}

/** 只观测真实注册表，不复制生命、动画、位置或战斗状态机。 */
void Board::RefreshWorkshopObservation()
{
    if (!mWorkshop) return;
    std::array<int, Workshop::Rows> enemies{}, shooters{};
    for (int id : mEntityRegistry.GetAllZombieIDs()) {
        const auto* zombie = mEntityRegistry.GetZombie(id);
        if (zombie && zombie->IsActive() && !zombie->IsMindControlled() && !zombie->IsDying()
            && zombie->mRow >= 0 && zombie->mRow < Workshop::Rows) ++enemies[zombie->mRow];
    }
    for (int id : mEntityRegistry.GetAllPlantIDs()) {
        const auto* plant = mEntityRegistry.GetPlant(id);
        if (plant && plant->IsActive() && plant->mPlantHealth > 0 && plant->mRow >= 0 && plant->mRow < Workshop::Rows)
            ++shooters[plant->mRow];
    }
    mWorkshop->ObserveFrontline(enemies, shooters);
}

bool Board::AddWorkshopShooter(int row)
{
    if (!mWorkshop || row < 0 || row >= Workshop::Rows || mWorkshop->State() == Workshop::Phase::Dead) return false;
    RefreshWorkshopObservation();
    if (mWorkshop->Energy() < Workshop::ShooterCost || mWorkshop->Lanes()[row].shooters >= Workshop::MaxShootersPerLane) return false;
    for (int col = kWorkshopFirstShooterColumn; col >= 0; --col) {
        if (CanPlantAt(PlantType::PLANT_PEASHOOTER, row, col)
            && CreatePlant(PlantType::PLANT_PEASHOOTER, row, col)) return mWorkshop->AddShooter(row);
    }
    return false;
}

bool Board::BeginWorkshopAssault()
{
    if (!mWorkshop || mWorkshop->State() != Workshop::Phase::Planning) return false;
    nlohmann::json checkpoint;
    if (!GameInfoSaver::CaptureWorkshopFrontline(this, checkpoint) || !mWorkshop->StartAssault()) return false;
    mWorkshopBattleCheckpoint = std::move(checkpoint);
    mCurrentWave = mWorkshop->CompletedWaves() + 1;
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_FIRSTWAVE, 0.7f);
    return true;
}

bool Board::ReserveWorkshopAmmo(int row)
{
    return !mWorkshop || mWorkshop->ReserveAmmo(row);
}
void Board::RecordWorkshopShot()
{
    if (mWorkshop) mWorkshop->RecordShot();
}

/** 入侵者沿用 Zombie 的房屋判定；每只越线只扣一次基地生命，不干预走路或啃食。 */
void Board::RecordWorkshopBreach(int zombieID)
{
    if (!mWorkshop || mWorkshop->State() != Workshop::Phase::Assault) return;
    auto* zombie = mEntityRegistry.GetZombie(zombieID);
    if (!zombie || !zombie->IsActive()) return;
    zombie->Die();
    mWorkshop->RecordBreach();
    if (mWorkshop->State() == Workshop::Phase::Dead) {
        mBoardState = BoardState::LOSE_GAME;
        AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_LOSTGAME, 0.65f);
    }
}

void Board::UpdateWorkshop()
{
    if (!mWorkshop) return;
    const float dt = DeltaTime::GetDeltaTime();
    RefreshWorkshopObservation();
    mWorkshop->Tick(dt);
    mBoardFrameAccum += dt / DeltaTime::GetFixedStep();
    while (mBoardFrameAccum >= 1) { mBoardFrameAccum -= 1; ++mBoardFrame; }
    const int row = mWorkshop->NextSpawnRow();
    if (row >= 0) {
        const int wave = mWorkshop->CompletedWaves() + 1;
        const int number = mWorkshop->Spawned();
        ZombieType type = ZombieType::ZOMBIE_NORMAL;
        if (wave >= 3 && number % 4 == 3) type = ZombieType::ZOMBIE_TRAFFIC_CONE;
        if (wave >= 5 && number % 6 == 5) type = ZombieType::ZOMBIE_NEWSPAPER;
        if (wave >= 7 && number % 8 == 7) type = ZombieType::ZOMBIE_BUCKET;
        if (CreateZombie(type, row, kWorkshopSpawnX)) mWorkshop->CommitSpawn();
    }
    bool anyZombie = false;
    for (int id : mEntityRegistry.GetAllZombieIDs()) {
        const auto* zombie = mEntityRegistry.GetZombie(id);
        if (zombie && zombie->IsActive() && !zombie->IsMindControlled()) { anyZombie = true; break; }
    }
    if (mWorkshop->State() == Workshop::Phase::Assault && mWorkshop->RemainingSpawns() == 0 && !anyZombie) {
        mWorkshop->CompleteAssault();
        mWorkshopBattleCheckpoint = nullptr;
    }
    UpdateZombieMetrics();
    AudioSystem::UpdateAdaptiveMusic(dt, mHostileZombieCountForMusic);
}

nlohmann::json Board::SaveWorkshopDocument()
{
    if (!mWorkshop) return nullptr;
    RefreshWorkshopObservation();
    auto document = mWorkshop->Save();
    document["frontlineVersion"] = 1;
    if (!GameInfoSaver::CaptureWorkshopFrontline(this, document["battle"])) return nullptr;
    document["battleCheckpoint"] = mWorkshopBattleCheckpoint;
    return document;
}

/** 仅恢复到新建 Board；调用方在失败时停止整个场景并保留磁盘原档。 */
bool Board::RestoreWorkshopDocument(const nlohmann::json& original, std::string& error)
{
    if (!mWorkshop) return false;
    try {
        auto document = original;
        if (!document.contains("battle")) {
            // 开发期简化前线档迁移：保留基地，未完成袭击回到原检查点，不猜造原生实体。
            if (document.at("state").at("phase").get<int>() != 0) {
                document["state"] = document.at("checkpoint"); document["checkpoint"] = nullptr;
            }
            if (!mWorkshop->Load(document, error)) return false;
            InitializeWorkshopFrontline();
            return true;
        }
        if (document.at("frontlineVersion") != 1) throw std::runtime_error("不支持的工坊前线版本");
        if (!GameInfoSaver::ValidateWorkshopFrontline(document.at("battle"), error)) return false;
        if (!document.at("battleCheckpoint").is_null()
            && !GameInfoSaver::ValidateWorkshopFrontline(document.at("battleCheckpoint"), error)) return false;
        if (!mWorkshop->Load(document, error)) return false;
        if ((mWorkshop->State() == Workshop::Phase::Planning) != document.at("battleCheckpoint").is_null())
            throw std::runtime_error("工坊前线检查点与阶段不一致");
        const auto expectedState = mWorkshop->State() == Workshop::Phase::Dead ? BoardState::LOSE_GAME : BoardState::GAME;
        if (document.at("battle").at("boardState") != static_cast<int>(expectedState))
            throw std::runtime_error("工坊阶段与真实前线状态不一致");
        if (!GameInfoSaver::RestoreWorkshopFrontline(this, document.at("battle"))) throw std::runtime_error("原生前线对象恢复失败");
        mWorkshopBattleCheckpoint = document.at("battleCheckpoint");
        mMaxWave = std::numeric_limits<int>::max();
        mHxyModeEnabled = false;
        RefreshWorkshopObservation();
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

nlohmann::json Board::WorkshopRetryDocument()
{
    if (!mWorkshop || mWorkshop->State() != Workshop::Phase::Dead) return nullptr;
    auto document = mWorkshop->Save();
    document["state"] = document.at("checkpoint"); document["checkpoint"] = nullptr;
    document["frontlineVersion"] = 1;
    document["battle"] = mWorkshopBattleCheckpoint; document["battleCheckpoint"] = nullptr;
    return document;
}
