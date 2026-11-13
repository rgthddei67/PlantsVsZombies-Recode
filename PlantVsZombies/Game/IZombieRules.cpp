#include "IZombieRules.h"
#include "Board/Board.h"
#include "Plant/GameDataManager.h"
#include "FileManager.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace {
std::vector<IZombie::CardDefinition> cards;
std::vector<IZombie::LevelDefinition> levels;

/** 图鉴与卡牌读取同一名称文件，避免每个新僵尸重复登记 UI 名称。 */
std::unordered_map<std::string, std::string> ReadNames()
{
    std::unordered_map<std::string, std::string> names;
    std::istringstream input(FileManager::LoadFileAsString("./resources/info.txt"));
    std::string line, key;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 2 && line.front() == '[' && line.back() == ']') key = line.substr(1, line.size()-2);
        else if (!key.empty() && !line.empty()) { names.try_emplace(key, line); key.clear(); }
    }
    return names;
}
Background ParseBackground(const std::string& name)
{
    const std::pair<const char*, Background> values[] = {
        {"GROUND_DAY", Background::GROUND_DAY}, {"GROUND_NIGHT", Background::GROUND_NIGHT},
        {"WATER_POOL", Background::WATER_POOL}, {"NIGHT_WATER_POOL", Background::NIGHT_WATER_POOL},
        {"ROOF", Background::ROOF}, {"NIGHT_ROOF", Background::NIGHT_ROOF},
        {"WINTER_GARDEN", Background::WINTER_GARDEN}, {"POLAR_NIGHT_SNOWFIELD", Background::POLAR_NIGHT_SNOWFIELD},
        {"GLOOMCRYSTAL_MINE", Background::GLOOMCRYSTAL_MINE}, {"HOT_COLD_STORAGE", Background::HOT_COLD_STORAGE},
        {"WEATHER_STATION", Background::WEATHER_STATION}, {"WATERSIDE_GARDEN", Background::WATERSIDE_GARDEN}
    };
    for (const auto& [key, value] : values) if (name == key) return value;
    throw std::runtime_error("unknown IZombie background: " + name);
}
}

bool IZombie::IsLevel(int level) { return level >= FirstLevel && level <= LastLevel; }
const std::vector<IZombie::CardDefinition>& IZombie::Cards() { return cards; }
const std::vector<IZombie::LevelDefinition>& IZombie::Levels() { return levels; }
const IZombie::CardDefinition& IZombie::Card(ZombieType type)
{
    for (const auto& card : cards) if (card.type == type) return card;
    throw std::runtime_error("unregistered IZombie card");
}
const IZombie::LevelDefinition& IZombie::Level(int level)
{
    for (const auto& definition : levels) if (definition.level == level) return definition;
    throw std::runtime_error("unregistered IZombie level");
}

bool IZombie::LoadDefinitions()
{
    try {
        nlohmann::json document;
        if (!FileManager::LoadJsonFile("./resources/izombie.json", document))
            throw std::runtime_error("missing resources/izombie.json");
        const auto names = ReadNames();
        const auto& data = GameDataManager::GetInstance();
        auto types = data.GetAllZombieTypes();
        std::sort(types.begin(), types.end());
        std::vector<CardDefinition> newCards;
        std::vector<LevelDefinition> newLevels;
        std::unordered_map<std::string, ZombieType> byName;
        const float defaultCooldown = document.at("defaultCooldown").get<float>();
        if (!std::isfinite(defaultCooldown) || defaultCooldown < 0 || defaultCooldown > 3600)
            throw std::runtime_error("invalid default cooldown");
        for (ZombieType type : types) {
            const auto key = data.ZombieTypeToEnumName(type);
            const auto& definition = document.at("cards").at(key);
            const int cost = definition.at("cost").get<int>();
            const float cooldown = definition.value("cooldown", defaultCooldown);
            if (cost <= 0 || cost > MAX_SUN || !std::isfinite(cooldown) || cooldown < 0 || cooldown > 3600)
                throw std::runtime_error("invalid IZombie card: " + key);
            newCards.push_back({type, names.at(key), cost, cooldown});
            byName.emplace(key, type);
        }
        std::unordered_set<int> usedLevels;
        for (const auto& source : document.at("levels")) {
            LevelDefinition level;
            level.level = source.at("level").get<int>();
            level.name = source.at("name").get<std::string>();
            level.background = ParseBackground(source.at("background").get<std::string>());
            level.initialSun = source.at("initialSun").get<int>();
            level.firstSpawnColumn = source.at("firstSpawnColumn").get<int>();
            if (!IsLevel(level.level) || !usedLevels.insert(level.level).second
                || level.initialSun < 0 || level.initialSun > MAX_SUN
                || level.firstSpawnColumn < 1 || level.firstSpawnColumn > 8)
                throw std::runtime_error("invalid IZombie level");
            if (source.at("cards").is_string() && source.at("cards") == "all") level.cards = types;
            else for (const auto& key : source.at("cards")) level.cards.push_back(byName.at(key.get<std::string>()));
            std::unordered_set<int> uniqueCards;
            for (auto type : level.cards) if (!uniqueCards.insert(static_cast<int>(type)).second)
                throw std::runtime_error("duplicate IZombie card slot");
            if (level.cards.empty()) throw std::runtime_error("empty IZombie card bank");
            const int rows = level.background == Background::WATER_POOL
                || level.background == Background::NIGHT_WATER_POOL || level.background == Background::WATERSIDE_GARDEN ? 6 : 5;
            for (const auto& entry : source.at("plants")) {
                const auto key = entry.at("type").get<std::string>();
                const PlantType type = data.StringToPlantType(key);
                const int row = entry.at("row").get<int>(), column = entry.at("column").get<int>();
                if (type == PlantType::NUM_PLANT_TYPES || data.IsSkillCard(type)
                    || row < 0 || row >= rows || column < 0 || column >= level.firstSpawnColumn)
                    throw std::runtime_error("invalid IZombie plant: " + key);
                level.plants.push_back({type, row, column});
            }
            newLevels.push_back(std::move(level));
        }
        if (newLevels.empty()) throw std::runtime_error("empty IZombie level list");
        cards = std::move(newCards);
        levels = std::move(newLevels);
        return true;
    } catch (const std::exception& error) {
        LOG_ERROR("IZombie") << "配置加载失败: " << error.what();
        return false;
    }
}
