#include "Board.h"
#include "BoardPresentation.h"
#include "Game/Zombie/Zombie.h"
#include "Game/Zombie/BungeeZombie.h"
#include "Game/Plant/Plant.h"
#include "Game/Plant/GameDataManager.h"
#include "Game/Sun.h"
#include "Game/AudioSystem.h"
#include "DeltaTime.h"
#include "GameApp.h"
#include "Graphics.h"
#include "ResourceManager.h"
#include "ResourceKeys.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace {
constexpr float kBrainOffsetCells = 0.85f; // 脑子中心位于首列中心左侧的格宽倍数
constexpr float kBrainReachPixels = 24.0f; // 逻辑僵尸原点进入脑子的啃食距离，px
constexpr float kDefeatDelay = 1.5f; // 等待死亡、爆炸和拾取收尾后再判负，游戏秒
constexpr float kBrainDrawSize = 42.0f; // 脑子贴图实机显示尺寸，px
}

void Board::InitializeIZombie()
{
    if (!mIZombie || mIZombie->initialized) return;
    const auto& level = IZombie::Level(mLevel);
    mIZombie->initialized = true;
    mIZombie->firstSpawnColumn = level.firstSpawnColumn;
    mIZombie->cards = level.cards;
    mIZombie->cooldowns.assign(level.cards.size(), 0);
    mIZombie->brains.assign(mRows, IZombie::BrainChewSeconds);
    mSun = level.initialSun;
    for (const auto& placement : level.plants) {
        if (!CreatePlant(placement.type, placement.row, placement.column))
            throw std::runtime_error("IZombie initial formation could not create a plant");
    }
}

std::string Board::CanDeployIZombie(int slot, int row, int column) const
{
    // 高级暂停允许布置；玩法时钟仍为零，移动、战斗与冷却继续冻结。
    if (!mIZombie || mBoardState != BoardState::GAME || mTrophySpawned
        || (DeltaTime::IsPaused() && !GameAPP::GetInstance().mAdvancedPauseEnabled)) return "当前不能投放";
    if (slot < 0 || slot >= static_cast<int>(mIZombie->cards.size())) return "无效的僵尸卡";
    if (row < 0 || row >= mRows || column < 0 || column >= mColumns) return "请选择草坪格子";
    const auto type = mIZombie->cards[slot];
    if (type == ZombieType::ZOMBIE_BUNGEE) {
        if (column >= mIZombie->firstSpawnColumn) return "蹦极僵尸需要投放在分界线左侧";
    } else if (column < mIZombie->firstSpawnColumn) return "请投放在分界线右侧";
    if (!IsSpawnRowCompatible(type, row)) return "该僵尸不适合这条路线";
    if (IsMineBackground() && mMineGrid.IsRock(row, column)) return "不能投放在岩墙内";
    if (mIZombie->cooldowns[slot] > 0) return "这张卡还在冷却";
    if (mSun < IZombie::Card(type).cost) return "阳光不足";
    return {};
}

std::string Board::TryDeployIZombie(int slot, int row, int column)
{
    if (auto reason = CanDeployIZombie(slot, row, column); !reason.empty()) return reason;
    const auto type = mIZombie->cards[slot];
    const auto& card = IZombie::Card(type);
    const float x = GetCellCenterPosition(row, column).x;
    Zombie* zombie = CreateZombie(type, row, x);
    if (!zombie) return "僵尸创建失败";
    if (auto* bungee = dynamic_cast<BungeeZombie*>(zombie)) {
        if (!bungee->SetPlayerTarget(row, column)) {
            zombie->Die();
            return "这个目标格暂时不能抓取";
        }
    }
    mSun -= card.cost;
    mIZombie->cooldowns[slot] = card.cooldown;
    mIZombie->defeatDelay = 0;
    AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_PLANT, 0.35f);
    return {};
}

Vector Board::GetIZombieBrainPosition(int row) const
{
    auto position = GetCellCenterPosition(row, 0);
    position.x -= CELL_COLLIDER_SIZE_X * kBrainOffsetCells;
    return position;
}

bool Board::CanReachIZombieBrain(const Zombie& zombie) const
{
    if (!mIZombie || mBoardState != BoardState::GAME || mTrophySpawned || zombie.IsPreview()
        || zombie.IsDying() || !zombie.IsActive() || !zombie.HasHead() || zombie.IsMindControlled()
        || zombie.IsMovingRight() || zombie.mRow < 0 || zombie.mRow >= static_cast<int>(mIZombie->brains.size())
        || mIZombie->brains[zombie.mRow] <= 0) return false;
    return zombie.GetPosition().x <= GetIZombieBrainPosition(zombie.mRow).x + kBrainReachPixels;
}

void Board::DamageIZombieBrain(int row, float chewSeconds)
{
    if (!mIZombie || mBoardState != BoardState::GAME || mTrophySpawned || row < 0
        || row >= static_cast<int>(mIZombie->brains.size()) || chewSeconds <= 0) return;
    auto& remaining = mIZombie->brains[row];
    if (remaining <= 0) return;
    remaining = std::max(0.0f, remaining - chewSeconds);
    if (remaining <= 0) AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_ZOMBIE_FINISHEAT, 0.4f);
    if (GetIZombieBrainsRemaining() == 0) CreateTrophy(GetCellCenterPosition(mRows / 2, 4));
}

int Board::GetIZombieBrainsRemaining() const
{
    return mIZombie ? static_cast<int>(std::count_if(mIZombie->brains.begin(), mIZombie->brains.end(),
        [](float remaining) { return remaining > 0; })) : 0;
}

void Board::ReleaseIZombieSun(Plant& plant, bool all)
{
    if (!mIZombie || plant.mIsPreview || plant.mPlantType != PlantType::PLANT_SUNFLOWER) return;
    const int target = all || plant.mPlantHealth <= 0 ? IZombie::SunflowerYield
        : std::clamp(IZombie::SunflowerYield - 1 - plant.mPlantHealth / 40, 0, IZombie::SunflowerYield);
    auto& paid = mIZombie->sunflowerPaid[plant.mPlantID];
    // 先提交账本，再创建拾取物；多个死亡入口或钟匠重建同 ID 都不能再次领取。
    const int count = std::max(0, target - paid);
    paid = std::max(paid, target);
    for (int index = 0; index < count; ++index)
        CreateSun(plant.GetPosition() + Vector((index - (count-1)*0.5f)*12.0f, -12), true);
}

void Board::UpdateIZombie(float delta)
{
    if (!mIZombie || mBoardState != BoardState::GAME || mTrophySpawned || delta <= 0) return;
    for (auto& cooldown : mIZombie->cooldowns) cooldown = std::max(0.0f, cooldown - delta);
    // 所有活体（含死亡动画）、在途召唤都先收尾。只在确实没有后续资源时判负。
    if (mZombieNumber > 0 || !mPendingSnowHoleSpawns.empty() || !mPendingAuroraRifts.empty()) {
        mIZombie->defeatDelay = 0;
        return;
    }
    int available = mSun;
    for (int id : mEntityRegistry.GetAllCoinIDs()) {
        auto* coin = mEntityRegistry.GetCoin(id);
        if (coin && coin->IsActive() && dynamic_cast<Sun*>(coin))
            available += dynamic_cast<SmallSun*>(coin) ? 15 : 25;
    }
    for (int slot = 0; slot < static_cast<int>(mIZombie->cards.size()); ++slot) {
        const auto type = mIZombie->cards[slot];
        if (IZombie::Card(type).cost > available) continue;
        const int begin = type == ZombieType::ZOMBIE_BUNGEE ? 0 : mIZombie->firstSpawnColumn;
        const int end = type == ZombieType::ZOMBIE_BUNGEE ? mIZombie->firstSpawnColumn : mColumns;
        for (int row = 0; row < mRows; ++row) {
            if (!IsSpawnRowCompatible(type, row)) continue;
            for (int column = begin; column < end; ++column) {
                if (IsMineBackground() && mMineGrid.IsRock(row, column)) continue;
                mIZombie->defeatDelay = 0;
                return;
            }
        }
    }
    mIZombie->defeatDelay += delta;
    if (mIZombie->defeatDelay >= kDefeatDelay) GameOver();
}

void Board::DrawIZombie(Graphics* graphics) const
{
    if (!mIZombie || !mIZombie->initialized) return;
    const auto* brain = ResourceManager::GetInstance().GetTexture("IMAGE_IZOMBIE_BRAIN", false);
    const float boundary = GetCellCenterPosition(0, mIZombie->firstSpawnColumn).x - CELL_COLLIDER_SIZE_X*0.5f;
    for (int row = 0; row < mRows; ++row) {
        const auto position = GetIZombieBrainPosition(row);
        graphics->DrawRect(boundary-2, GetCellCenterPosition(row,0).y-GetCellHeight()*0.5f,
            4, GetCellHeight()-5, {230,65,65,210});
        if (brain && mIZombie->brains[row] > 0) {
            const float size = kBrainDrawSize * (0.8f + 0.2f * mIZombie->brains[row] / IZombie::BrainChewSeconds);
            graphics->DrawTexture(brain, position.x-size*0.5f, position.y-size*0.5f, size, size);
        }
    }
}

nlohmann::json Board::SaveIZombie() const
{
    if (!mIZombie) return nullptr;
    nlohmann::json state = {{"initialized", mIZombie->initialized}, {"level",mLevel},
        {"firstSpawnColumn",mIZombie->firstSpawnColumn}, {"brains",mIZombie->brains},
        {"cooldowns",mIZombie->cooldowns}, {"defeatDelay",mIZombie->defeatDelay},
        {"cards",nlohmann::json::array()}, {"sunflowerPaid",nlohmann::json::array()}};
    for (auto type : mIZombie->cards) state["cards"].push_back(GameDataManager::GetInstance().ZombieTypeToEnumName(type));
    for (const auto& [id, paid] : mIZombie->sunflowerPaid) state["sunflowerPaid"].push_back({id,paid});
    return state;
}

void Board::LoadIZombie(const nlohmann::json& source)
{
    if (!mIZombie) {
        if (!source.is_null()) throw std::runtime_error("IZombie save in a different mode");
        return;
    }
    if (!source.is_object() || source.at("level") != mLevel) throw std::runtime_error("IZombie save level mismatch");
    IZombie::State state;
    state.initialized = source.at("initialized").get<bool>();
    state.firstSpawnColumn = source.at("firstSpawnColumn").get<int>();
    state.brains = source.at("brains").get<std::vector<float>>();
    state.cooldowns = source.at("cooldowns").get<std::vector<float>>();
    state.defeatDelay = source.value("defeatDelay",0.0f);
    if (!state.initialized || state.firstSpawnColumn < 1 || state.firstSpawnColumn >= mColumns
        || state.brains.size() != static_cast<size_t>(mRows) || !std::isfinite(state.defeatDelay)
        || state.defeatDelay < 0 || state.defeatDelay > kDefeatDelay)
        throw std::runtime_error("invalid IZombie state");
    std::unordered_set<int> seen;
    for (const auto& name : source.at("cards")) {
        const auto it = std::find_if(IZombie::Cards().begin(), IZombie::Cards().end(), [&](const auto& card) {
            return name == GameDataManager::GetInstance().ZombieTypeToEnumName(card.type);
        });
        if (it == IZombie::Cards().end() || !seen.insert(static_cast<int>(it->type)).second)
            throw std::runtime_error("invalid IZombie saved card");
        state.cards.push_back(it->type);
    }
    if (state.cards.empty() || state.cards.size() != state.cooldowns.size()) throw std::runtime_error("invalid IZombie cooldown bank");
    for (float remaining : state.brains) if (!std::isfinite(remaining) || remaining < 0 || remaining > IZombie::BrainChewSeconds)
        throw std::runtime_error("invalid IZombie brain");
    for (float remaining : state.cooldowns) if (!std::isfinite(remaining) || remaining < 0 || remaining > 3600)
        throw std::runtime_error("invalid IZombie cooldown");
    for (const auto& entry : source.at("sunflowerPaid")) {
        if (!entry.is_array() || entry.size()!=2 || !entry[0].is_number_integer() || !entry[1].is_number_integer())
            throw std::runtime_error("invalid IZombie sunflower ledger");
        const int id=entry[0].get<int>(), paid=entry[1].get<int>();
        if (id<=0 || paid<0 || paid>IZombie::SunflowerYield || !state.sunflowerPaid.emplace(id,paid).second)
            throw std::runtime_error("invalid IZombie sunflower identity");
    }
    *mIZombie = std::move(state);
}

void Board::SetIZombieCardsForTesting(const std::vector<ZombieType>& types)
{
    if (!GameAPP::mAutoTestMode || !mIZombie || types.empty()) return;
    std::unordered_set<int> seen;
    for (auto type : types) {
        IZombie::Card(type);
        if (!seen.insert(static_cast<int>(type)).second) throw std::runtime_error("duplicate test card");
    }
    mIZombie->cards = types;
    mIZombie->cooldowns.assign(types.size(), 0);
}

void Board::SetIZombieCooldownForTesting(int slot, float seconds)
{
    if (GameAPP::mAutoTestMode && mIZombie && slot >= 0 && slot < static_cast<int>(mIZombie->cooldowns.size()))
        mIZombie->cooldowns[slot] = std::clamp(seconds,0.0f,3600.0f);
}
