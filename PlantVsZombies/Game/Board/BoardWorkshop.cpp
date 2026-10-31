#include "Board.h"
#include "Game/Workshop/WorkshopRoster.h"
#include "Game/Plant/MelonPult.h"
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

std::string Board::WorkshopPlantPlacementReason(PlantType type, int row, int column) {
    const auto* info = Workshop::FindPlant(type);
    if (!mWorkshop || !info) return "工坊尚未开放此植物";
    if (mWorkshop->State() == Workshop::Phase::Dead) return "基地已失守";
    if (row < 0 || row >= Workshop::Rows || column < 0 || column >= Workshop::FrontColumns) return "只能种在左侧六列";
    if (mWorkshop->CompletedWaves() < info->unlockWave) return "守住第 " + std::to_string(info->unlockWave) + " 波后解锁";
    if (mWorkshop->Energy() < info->cost) return "阳光不足";
    if (!CanPlantAt(type, row, column)) return type == PlantType::PLANT_WINTERMELON ? "请选一株可升级的西瓜投手" : "此格已有植物或暂时不能种植";
    return {};
}

bool Board::PlaceWorkshopPlant(PlantType type, int row, int column) {
    if (!WorkshopPlantPlacementReason(type, row, column).empty()) return false;
    const auto& info = *Workshop::FindPlant(type);
    int inheritedLevel = 1; float healthRatio = 1;
    if (type == PlantType::PLANT_WINTERMELON) {
        const auto* base = GetNormalPlantAt(row, column);
        inheritedLevel = base->GetWorkshopUpgradeLevel();
        healthRatio = static_cast<float>(base->mPlantHealth) / std::max(1, base->mPlantMaxHealth);
    }
    if (!mWorkshop->SpendEnergy(static_cast<float>(info.cost))) return false;
    auto* plant = CreatePlayerPlant(type, row, column);
    if (!plant) { mWorkshop->RefundEnergy(static_cast<float>(info.cost)); return false; }
    if (type == PlantType::PLANT_WINTERMELON) {
        auto* melon = static_cast<MelonPult*>(plant);
        melon->InheritWorkshopLevel(inheritedLevel);
        melon->mPlantHealth = std::max(1, static_cast<int>(melon->mPlantMaxHealth * healthRatio));
    }
    if (info.ammo != Workshop::Ammo::Count) mWorkshop->EnsureAmmoQuota(row, info.ammo);
    RefreshWorkshopObservation();
    return true;
}

bool Board::RemoveWorkshopPlant(int row, int column) {
    if (!mWorkshop || mWorkshop->State() == Workshop::Phase::Dead || row < 0 || row >= Workshop::Rows || column < 0 || column >= Workshop::FrontColumns) return false;
    auto* plant = GetNormalPlantAt(row, column);
    if (!plant || !plant->IsActive() || plant->mPlantHealth <= 0) return false;
    plant->Die(); RefreshWorkshopObservation(); return true;
}

/** 保留旧增援入口供旧脚本使用；玩家现在可以直接点格种植，数量由左侧六列占格决定。 */
bool Board::AddWorkshopShooter(int row, bool ice) {
    if (!mWorkshop || row < 0 || row >= Workshop::Rows) return false;
    const auto type = ice ? PlantType::PLANT_SNOWPEA : PlantType::PLANT_PEASHOOTER;
    for (int col = 0; col < Workshop::FrontColumns; ++col)
        if (WorkshopPlantPlacementReason(type, row, col).empty()) return PlaceWorkshopPlant(type, row, col);
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

bool Board::ReserveWorkshopAmmo(int row, PlantType type)
{
    if (!mWorkshop) return true;
    const auto* info = Workshop::FindPlant(type);
    return info && info->ammo != Workshop::Ammo::Count && mWorkshop->ReserveAmmo(row, info->ammo);
}
void Board::RecordWorkshopShot(bool ice)
{
    if (mWorkshop) mWorkshop->RecordShot(ice ? Workshop::Ammo::Ice : Workshop::Ammo::Pea);
}

void Board::RecordWorkshopPlantShot(PlantType type) {
    if (mWorkshop) if (const auto* info = Workshop::FindPlant(type); info && info->ammo != Workshop::Ammo::Count) mWorkshop->RecordShot(info->ammo);
}
/** 只收真实死亡的敌方尸骸；直接清理存活对象、越线及时间替换均不领取。 */
void Board::RecordWorkshopKill(const Zombie& zombie) {
    if (mWorkshop && zombie.mZombieID != mWorkshopRetiringBreach && !zombie.IsMindControlled() && (zombie.IsDying() || zombie.mBodyHealth <= 0)) mWorkshop->RecordKill();
}

/** 入侵者沿用 Zombie 的房屋判定；每只越线只扣一次基地生命，不干预走路或啃食。 */
void Board::RecordWorkshopBreach(int zombieID)
{
    if (!mWorkshop || mWorkshop->State() != Workshop::Phase::Assault) return;
    auto* zombie = mEntityRegistry.GetZombie(zombieID);
    if (!zombie || !zombie->IsActive()) return;
    const int previousBreach = mWorkshopRetiringBreach;
    mWorkshopRetiringBreach = zombieID;
    zombie->Die();
    mWorkshopRetiringBreach = previousBreach;
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
        const auto type = Workshop::SpawnType(wave, number);
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
