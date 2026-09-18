#include "GameDataManager.h"
#include "../../ResourceKeys.h"
#include "../../Logger.h"
#include "../../FileManager.h"
#include "../AutoTest/TestDriver.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <vector>
#include <unordered_set>

#include "../GameObjectManager.h"
#include "../RenderOrder.h"
#include "Game/Board/Board.h"

#include "PeaShooter.h"
#include "ToxicPeaShooter.h"
#include "SunFlower.h"
#include "TwinSunflower.h"
#include "CherryBomb.h"
#include "WallNut.h"
#include "SnowAnchorNut.h"
#include "PotatoMine.h"
#include "SnowPeaShooter.h"
#include "Chomper.h"
#include "Repeater.h"
#include "GatlingPea.h"
#include "PuffShroom.h"
#include "SunShroom.h"
#include "FumeShroom.h"
#include "EchoShroom.h"
#include "PrismFlower.h"
#include "AmberLichen.h"
#include "../Zombie/CrystalDrummerZombie.h"
#include "Game/Zombie/SunThiefZombie.h"
#include "Game/Zombie/CrystalHornMinerZombie.h"
#include "GloomShroom.h"
#include "IceFumeShroom.h"
#include "HypnoShroom.h"
#include "ScaredyShroom.h"
#include "EliteScaredyShroom.h"
#include "IceShroom.h"
#include "DoomShroom.h"
#include "LilyPad.h"
#include "Squash.h"
#include "ThreePeater.h"
#include "TangleKelp.h"
#include "CarryVine.h"
#include "Game/Zombie/ExcavatorZombie.h"
#include "Jalapeno.h"
#include "Caltrop.h"
#include "Torchwood.h"
#include "AuroraTorchwood.h"
#include "TallNut.h"
#include "SeaShroom.h"
#include "Plantern.h"
#include "Cactus.h"
#include "Blover.h"
#include "SplitPea.h"
#include "StarFruit.h"
#include "PumpkinShell.h"
#include "MagnetShroom.h"
#include "GoldMagnet.h"
#include "GroundingShroom.h"
#include "FlowerPot.h"
#include "LightningRodPot.h"
#include "CabbagePult.h"
#include "MeltSnowPult.h"
#include "FrostMine.h"
#include "AlarmBellFlower.h"
#include "BoundaryFlower.h"
#include "DawnLotus.h"
#include "FurnaceCoreFlower.h"
#include "ListeningGrass.h"
#include "NorthStarFlower.h"
#include "IceMirrorGrass.h"
#include "KernelPult.h"
#include "CobCannon.h"
#include "CoffeeBean.h"
#include "Garlic.h"
#include "UmbrellaLeaf.h"
#include "Marigold.h"
#include "MelonPult.h"
#include "WinterMelon.h"
#include "Imitater.h"

#include "../Zombie/Zombie.h"
#include "../Zombie/ConeZombie.h"
#include "../Zombie/Polevaulter.h"
#include "../Zombie/BucketZombie.h"
#include "../Zombie/FastBucketZombie.h"
#include "../Zombie/PaperZombie.h"
#include "../Zombie/FastPaperZombie.h"
#include "../Zombie/DoorZombie.h"
#include "../Zombie/FootballZombie.h"
#include "../Zombie/PinkFootballZombie.h"
#include "../Zombie/DancerZombie.h"
#include "../Zombie/BackupDancerZombie.h"
#include "../Zombie/EliteDancerZombie.h"
#include "../Zombie/ReinforcedDoorZombie.h"
#include "../Zombie/PoolNormalZombie.h"
#include "../Zombie/PoolConeZombie.h"
#include "../Zombie/PoolBucketZombie.h"
#include "../Zombie/ElitePolevaulterZombie.h"
#include "../Zombie/ZamboniZombie.h"
#include "../Zombie/GildedZamboniZombie.h"
#include "../Zombie/DolphinRiderZombie.h"
#include "../Zombie/EliteDolphinRiderZombie.h"
#include "../Zombie/JackInTheBoxZombie.h"
#include "../Zombie/EliteJackInTheBoxZombie.h"
#include "../Zombie/BalloonZombie.h"
#include "../Zombie/DiggerZombie.h"
#include "../Zombie/EliteDiggerZombie.h"
#include "../Zombie/PogoZombie.h"
#include "../Zombie/ElitePogoZombie.h"
#include "../Zombie/BungeeZombie.h"
#include "../Zombie/LadderZombie.h"
#include "../Zombie/EliteLadderZombie.h"
#include "../Zombie/CatapultZombie.h"
#include "../Zombie/EliteCatapultZombie.h"
#include "../Zombie/GargantuarZombie.h"
#include "../Zombie/ImpZombie.h"
#include "../Zombie/RedeyeGargantuarZombie.h"
#include "../Zombie/RoofMarshalZombie.h"
#include "../Zombie/InsulatorZombie.h"
#include "../Zombie/HijackerZombie.h"
#include "../Zombie/HealerZombie.h"
#include "../Zombie/GroundingZombie.h"
#include "../Zombie/BobsledTeamZombie.h"
#include "../Zombie/IceWallEngineerZombie.h"
#include "../Zombie/IceCrackDrillZombie.h"
#include "../Zombie/WeatherJammerZombie.h"
#include "../Zombie/IceStatueExecutionerZombie.h"
#include "../Zombie/SnowBurrowZombie.h"
#include "../Zombie/AdaptiveHelmetZombie.h"
#include "../Zombie/ThermalSniperZombie.h"
#include "../Zombie/AuroraPriestZombie.h"
#include "../Zombie/PolarClockmakerZombie.h"

namespace {
	template<typename T>
	std::shared_ptr<Plant> MakePlant(Board* b, PlantType t, int row, int col,
		AnimationType anim, float scale, bool preview) {
		return GameObjectManager::GetInstance()
			.CreateGameObjectImmediateAsShared<T>(LAYER_GAME_PLANT, b, t, row, col, anim, scale, preview);
	}
	template<typename T>
	std::shared_ptr<Zombie> MakeZombie(Board* b, ZombieType t, float x, float y, int row,
		AnimationType anim, float scale, bool preview) {
		return GameObjectManager::GetInstance()
			.CreateGameObjectImmediateAsShared<T>(LAYER_GAME_ZOMBIE, b, t, x, y, row, anim, scale, preview);
	}
}

GameDataManager::GameDataManager() {}

bool GameDataManager::Initialize() {
	// 清空现有数据
	mPlantInfo.clear();
	mZombieInfo.clear();
	mAnimToString.clear();
	mEnumNameToType.clear();
	mTextureKeyToType.clear();
	mAnimNameToType.clear();

	InitializeHardcodedData();
	if (!LoadNumbersFromJson()) return false;

	LOG_INFO("GameData") << "初始化完成，共注册 "
		<< mPlantInfo.size() << " 种植物，"
		<< mZombieInfo.size() << " 种僵尸（数值来自 gamedata.json）";
	return true;
}

void GameDataManager::InitializeHardcodedData() {
	// ============================================================
	// 本函数只注册【身份数据】：枚举名/纹理键/动画/构造工厂。
	// 全部【数值】（cost/cooldown/weight/appearWave/survivalRound/offset/scale）
	// 住在 resources/gamedata.json（数值唯一来源，改数值不用重编译）。
	// 【新增植物】共 6 步（不再需要改 GameApp 的 switch —— 已用注册式工厂取代）：
	//   1. PlantType.h    加枚举项
	//   2. Game/Plant/    写植物类（纯头文件即可，构造经 using 继承 Plant/Shooter）
	//   3. 本文件          在下方“植物注册”区加一行 RegisterPlant(..., &MakePlant<新类>)
	//      并在本文件顶部 #include "你的植物.h"
	//   4. resources/gamedata.json 的 "plants" 加同枚举名条目（漏了 → 启动时报错退出，
	//      只改 build/clang-release/resources/；其他预设通过目录联接共享同一份资源）
	//   5. resources/info.txt 加图鉴名称与说明（卡片本身由注册表数据驱动，无需单独接线）
	//   6. TestDriver 名称表、资源断言与专项 AutoTest 闭环
	// 【新增僵尸】同理：ZombieType.h 加枚举 → 写僵尸类 → 下方“僵尸注册”区加一行
	//      RegisterZombie(..., &MakeZombie<新类>) + 顶部 #include
	//      + gamedata.json 的 "zombies" 加条目（weight/appearWave/survivalRound 都在 JSON）。
	// 配套常量：动画类型加在 Reanimation/AnimationTypes.h；资源键加在 ResourceKeys.h。
	// ============================================================

	// ==================== 植物注册（仅身份，数值见 gamedata.json） ====================
	RegisterPlant(PlantType::PLANT_SUNFLOWER, "PLANT_SUNFLOWER",
		ResourceKeys::Textures::IMAGE_SUNFLOWER,
		AnimationType::ANIM_SUNFLOWER,
		ResourceKeys::Reanimations::REANIM_SUNFLOWER, &MakePlant<SunFlower>);

	RegisterPlant(PlantType::PLANT_TWINSUNFLOWER, "PLANT_TWINSUNFLOWER",
		ResourceKeys::Textures::IMAGE_TWINSUNFLOWER,
		AnimationType::ANIM_TWINSUNFLOWER,
		ResourceKeys::Reanimations::REANIM_TWINSUNFLOWER,
		&MakePlant<TwinSunflower>);

	RegisterPlant(PlantType::PLANT_PEASHOOTER, "PLANT_PEASHOOTER",
		ResourceKeys::Textures::IMAGE_PEASHOOTER,
		AnimationType::ANIM_PEASHOOTER,
		ResourceKeys::Reanimations::REANIM_PEASHOOTER, &MakePlant<PeaShooter>);

	RegisterPlant(PlantType::PLANT_TOXICPEASHOOTER, "PLANT_TOXICPEASHOOTER",
		ResourceKeys::Textures::IMAGE_TOXICPEASHOOTER,
		AnimationType::ANIM_TOXICPEASHOOTER,
		ResourceKeys::Reanimations::REANIM_TOXICPEASHOOTER, &MakePlant<ToxicPeaShooter>);

	RegisterPlant(PlantType::PLANT_CHERRYBOMB, "PLANT_CHERRYBOMB",
		ResourceKeys::Textures::IMAGE_CHERRYBOMB,
		AnimationType::ANIM_CHERRYBOMB,
		ResourceKeys::Reanimations::REANIM_CHERRYBOMB, &MakePlant<CherryBomb>);

	RegisterPlant(PlantType::PLANT_WALLNUT, "PLANT_WALLNUT",
		ResourceKeys::Textures::IMAGE_WALLNUT,
		AnimationType::ANIM_WALLNUT,
		ResourceKeys::Reanimations::REANIM_WALLNUT, &MakePlant<WallNut>);

	RegisterPlant(PlantType::PLANT_SNOWANCHORNUT, "PLANT_SNOWANCHORNUT",
		ResourceKeys::Textures::IMAGE_SNOWANCHORNUT,
		AnimationType::ANIM_SNOWANCHORNUT,
		ResourceKeys::Reanimations::REANIM_SNOWANCHORNUT,
		&MakePlant<SnowAnchorNut>);

	RegisterPlant(PlantType::PLANT_POTATOMINE, "PLANT_POTATOMINE",
		ResourceKeys::Textures::IMAGE_POTATOMINE,
		AnimationType::ANIM_POTATOMINE,
		ResourceKeys::Reanimations::REANIM_POTATOMINE, &MakePlant<PotatoMine>);

	RegisterPlant(PlantType::PLANT_SNOWPEA, "PLANT_SNOWPEASHOOTER",
		ResourceKeys::Textures::IMAGE_SNOWPEASHOOTER,
		AnimationType::ANIM_SNOWPEASHOOTER,
		ResourceKeys::Reanimations::REANIM_SNOWPEASHOOTER, &MakePlant<SnowPeaShooter>);

	RegisterPlant(PlantType::PLANT_CHOMPER, "PLANT_CHOMPER",
		ResourceKeys::Textures::IMAGE_CHOMPER,
		AnimationType::ANIM_CHOMPER,
		"Chomper", &MakePlant<Chomper>);

	RegisterPlant(PlantType::PLANT_REPEATER, "PLANT_REPEATER",
		ResourceKeys::Textures::IMAGE_REPEATER,
		AnimationType::ANIM_REPEAT,
		"Repeater", &MakePlant<Repeater>);

	RegisterPlant(PlantType::PLANT_GATLINGPEA, "PLANT_GATLINGPEA",
		ResourceKeys::Textures::IMAGE_GATLINGPEA,
		AnimationType::ANIM_GATLINGPEA,
		ResourceKeys::Reanimations::REANIM_GATLINGPEA, &MakePlant<GatlingPea>);

	RegisterPlant(PlantType::PLANT_PUFFSHROOM, "PLANT_PUFFSHROOM",
		ResourceKeys::Textures::IMAGE_PUFFSHROOM,
		AnimationType::ANIM_PUFFSHROOM,
		"PuffShroom", &MakePlant<PuffShroom>);

	RegisterPlant(PlantType::PLANT_SUNSHROOM, "PLANT_SUNSHROOM",
		ResourceKeys::Textures::IMAGE_SUNSHROOM,
		AnimationType::ANIM_SUNSHROOM,
		"SunShroom", &MakePlant<SunShroom>);

	RegisterPlant(PlantType::PLANT_AMBERLICHEN, "PLANT_AMBERLICHEN", "IMAGE_AMBERLICHEN",
		AnimationType::ANIM_AMBERLICHEN, "AmberLichen", &MakePlant<AmberLichen>);
	RegisterZombie(ZombieType::ZOMBIE_CRYSTAL_DRUMMER, "ZOMBIE_CRYSTAL_DRUMMER",
		AnimationType::ANIM_CRYSTAL_DRUMMER, "CrystalDrummerZombie", &MakeZombie<CrystalDrummerZombie>);
	RegisterPlant(PlantType::PLANT_PRISMFLOWER, "PLANT_PRISMFLOWER", "IMAGE_PRISMFLOWER",
		AnimationType::ANIM_PRISMFLOWER, "PrismFlower", &MakePlant<PrismFlower>);
	RegisterZombie(ZombieType::ZOMBIE_SUN_THIEF, "ZOMBIE_SUN_THIEF",
		AnimationType::ANIM_SUN_THIEF, "SunThiefZombie", &MakeZombie<SunThiefZombie>);
	RegisterPlant(PlantType::PLANT_ECHOSHROOM, "PLANT_ECHOSHROOM", "IMAGE_ECHOSHROOM",
		AnimationType::ANIM_ECHOSHROOM, "EchoShroom", &MakePlant<EchoShroom>);
	RegisterZombie(ZombieType::ZOMBIE_CRYSTAL_HORN_MINER, "ZOMBIE_CRYSTAL_HORN_MINER",
		AnimationType::ANIM_CRYSTAL_HORN_MINER, "CrystalHornMinerZombie", &MakeZombie<CrystalHornMinerZombie>);

	RegisterPlant(PlantType::PLANT_FUMESHROOM, "PLANT_FUMESHROOM",
		ResourceKeys::Textures::IMAGE_FUMESHROOM,
		AnimationType::ANIM_FUMESHROOM,
		"FumeShroom", &MakePlant<FumeShroom>);

	RegisterPlant(PlantType::PLANT_GLOOMSHROOM, "PLANT_GLOOMSHROOM",
		ResourceKeys::Textures::IMAGE_GLOOMSHROOM,
		AnimationType::ANIM_GLOOMSHROOM,
		ResourceKeys::Reanimations::REANIM_GLOOMSHROOM, &MakePlant<GloomShroom>);

	RegisterPlant(PlantType::PLANT_HYPNOSHROOM, "PLANT_HYPNOSHROOM",
		ResourceKeys::Textures::IMAGE_HYPNOSHROOM,
		AnimationType::ANIM_HYPER,
		"HypnoShroom", &MakePlant<HypnoShroom>);

	RegisterPlant(PlantType::PLANT_SCAREDYSHROOM, "PLANT_SCAREDYSHROOM",
		ResourceKeys::Textures::IMAGE_SCAREDYSHROOM,
		AnimationType::ANIM_SCAREDYSHROOM,
		"ScaredyShroom", &MakePlant<ScaredyShroom>);

	// 精英胆小菇复用原版动画，紫色身份由实例 overlay 与独立卡图表达。
	RegisterPlant(PlantType::PLANT_ELITE_SCAREDYSHROOM, "PLANT_ELITE_SCAREDYSHROOM",
		ResourceKeys::Textures::IMAGE_ELITESCAREDYSHROOM,
		AnimationType::ANIM_ELITE_SCAREDYSHROOM,
		"ScaredyShroom", &MakePlant<EliteScaredyShroom>);

	RegisterPlant(PlantType::PLANT_ICESHROOM, "PLANT_ICESHROOM",
		ResourceKeys::Textures::IMAGE_ICESHROOM,
		AnimationType::ANIM_ICE_SHROOM,
		"IceShroom", &MakePlant<IceShroom>);

	RegisterPlant(PlantType::PLANT_DOOMSHROOM, "PLANT_DOOMSHROOM",
		ResourceKeys::Textures::IMAGE_DOOMSHROOM,
		AnimationType::ANIM_DOOM_SHROOM,
		"DoomShroom", &MakePlant<DoomShroom>);

	RegisterPlant(PlantType::PLANT_LILYPAD, "PLANT_LILYPAD",
		ResourceKeys::Textures::IMAGE_LILYPAD,
		AnimationType::ANIM_LILYPAD,
		ResourceKeys::Reanimations::REANIM_LILYPAD, &MakePlant<LilyPad>);

	RegisterPlant(PlantType::PLANT_SQUASH, "PLANT_SQUASH",
		ResourceKeys::Textures::IMAGE_SQUASH,
		AnimationType::ANIM_SQUASH,
		ResourceKeys::Reanimations::REANIM_SQUASH, &MakePlant<Squash>);

	RegisterPlant(PlantType::PLANT_THREEPEATER, "PLANT_THREEPEATER",
		ResourceKeys::Textures::IMAGE_THREEPEATER,
		AnimationType::ANIM_THREEPEATER,
		ResourceKeys::Reanimations::REANIM_THREEPEATER, &MakePlant<ThreePeater>);

	RegisterPlant(PlantType::PLANT_TANGLEKELP, "PLANT_TANGLEKELP",
		ResourceKeys::Textures::IMAGE_TANGLEKELP,
		AnimationType::ANIM_TANGLEKELP,
		ResourceKeys::Reanimations::REANIM_TANGLEKELP, &MakePlant<TangleKelp>);

	RegisterPlant(PlantType::PLANT_JALAPENO, "PLANT_JALAPENO",
		ResourceKeys::Textures::IMAGE_JALAPENO,
		AnimationType::ANIM_JALAPENO,
		ResourceKeys::Reanimations::REANIM_JALAPENO, &MakePlant<Jalapeno>);

	RegisterPlant(PlantType::PLANT_SPIKEWEED, "PLANT_SPIKEWEED",
		ResourceKeys::Textures::IMAGE_CALTROP,
		AnimationType::ANIM_CALTROP,
		ResourceKeys::Reanimations::REANIM_CALTROP, &MakePlant<Caltrop>);

	RegisterPlant(PlantType::PLANT_TORCHWOOD, "PLANT_TORCHWOOD",
		ResourceKeys::Textures::IMAGE_TORCHWOOD,
		AnimationType::ANIM_TORCHWOOD,
		ResourceKeys::Reanimations::REANIM_TORCHWOOD, &MakePlant<Torchwood>);

	RegisterPlant(PlantType::PLANT_AURORATORCHWOOD, "PLANT_AURORATORCHWOOD",
		ResourceKeys::Textures::IMAGE_AURORATORCHWOOD,
		AnimationType::ANIM_AURORATORCHWOOD,
		ResourceKeys::Reanimations::REANIM_AURORATORCHWOOD,
		&MakePlant<AuroraTorchwood>);

	RegisterPlant(PlantType::PLANT_TALLNUT, "PLANT_TALLNUT",
		ResourceKeys::Textures::IMAGE_TALLNUT,
		AnimationType::ANIM_TALLNUT,
		ResourceKeys::Reanimations::REANIM_TALLNUT, &MakePlant<TallNut>);

	RegisterPlant(PlantType::PLANT_SEASHROOM, "PLANT_SEASHROOM",
		ResourceKeys::Textures::IMAGE_SEASHROOM,
		AnimationType::ANIM_SEASHROOM,
		ResourceKeys::Reanimations::REANIM_SEASHROOM, &MakePlant<SeaShroom>);

	RegisterPlant(PlantType::PLANT_PLANTERN, "PLANT_PLANTERN",
		ResourceKeys::Textures::IMAGE_PLANTERN,
		AnimationType::ANIM_PLANTERN,
		ResourceKeys::Reanimations::REANIM_PLANTERN, &MakePlant<Plantern>);

	RegisterPlant(PlantType::PLANT_CACTUS, "PLANT_CACTUS",
		ResourceKeys::Textures::IMAGE_CACTUS,
		AnimationType::ANIM_CACTUS,
		ResourceKeys::Reanimations::REANIM_CACTUS, &MakePlant<Cactus>);

	RegisterPlant(PlantType::PLANT_BLOVER, "PLANT_BLOVER",
		ResourceKeys::Textures::IMAGE_BLOVER,
		AnimationType::ANIM_BLOVER,
		ResourceKeys::Reanimations::REANIM_BLOVER, &MakePlant<Blover>);

	RegisterPlant(PlantType::PLANT_SPLITPEA, "PLANT_SPLITPEA",
		ResourceKeys::Textures::IMAGE_SPLITPEA,
		AnimationType::ANIM_SPLITPEA,
		ResourceKeys::Reanimations::REANIM_SPLITPEA, &MakePlant<SplitPea>);

	RegisterPlant(PlantType::PLANT_STARFRUIT, "PLANT_STARFRUIT",
		ResourceKeys::Textures::IMAGE_STARFRUIT,
		AnimationType::ANIM_STARFRUIT,
		ResourceKeys::Reanimations::REANIM_STARFRUIT, &MakePlant<StarFruit>);

	RegisterPlant(PlantType::PLANT_PUMPKINSHELL, "PLANT_PUMPKINSHELL",
		ResourceKeys::Textures::IMAGE_PUMPKIN,
		AnimationType::ANIM_PUMPKIN,
		ResourceKeys::Reanimations::REANIM_PUMPKIN, &MakePlant<PumpkinShell>);

	RegisterPlant(PlantType::PLANT_MAGNETSHROOM, "PLANT_MAGNETSHROOM",
		ResourceKeys::Textures::IMAGE_MAGNETSHROOM,
		AnimationType::ANIM_MAGNETSHROOM,
		ResourceKeys::Reanimations::REANIM_MAGNETSHROOM, &MakePlant<MagnetShroom>);

	RegisterPlant(PlantType::PLANT_GOLD_MAGNET, "PLANT_GOLD_MAGNET",
		ResourceKeys::Textures::IMAGE_GOLDMAGNET,
		AnimationType::ANIM_GOLD_MAGNET,
		ResourceKeys::Reanimations::REANIM_GOLDMAGNET, &MakePlant<GoldMagnet>);

	RegisterPlant(PlantType::PLANT_GROUNDINGSHROOM, "PLANT_GROUNDINGSHROOM",
		ResourceKeys::Textures::IMAGE_GROUNDINGSHROOM,
		AnimationType::ANIM_GROUNDINGSHROOM,
		ResourceKeys::Reanimations::REANIM_GROUNDINGSHROOM,
		&MakePlant<GroundingShroom>);

	RegisterPlant(PlantType::PLANT_FLOWERPOT, "PLANT_FLOWERPOT",
		ResourceKeys::Textures::IMAGE_FLOWERPOT,
		AnimationType::ANIM_FLOWERPOT,
		ResourceKeys::Reanimations::REANIM_FLOWERPOT, &MakePlant<FlowerPot>);

	RegisterPlant(PlantType::PLANT_LIGHTNINGRODPOT, "PLANT_LIGHTNINGRODPOT",
		ResourceKeys::Textures::IMAGE_LIGHTNINGRODPOT,
		AnimationType::ANIM_LIGHTNINGRODPOT,
		ResourceKeys::Reanimations::REANIM_LIGHTNINGRODPOT,
		&MakePlant<LightningRodPot>);

	RegisterPlant(PlantType::PLANT_CABBAGEPULT, "PLANT_CABBAGEPULT",
		ResourceKeys::Textures::IMAGE_CABBAGEPULT,
		AnimationType::ANIM_CABBAGEPULT,
		ResourceKeys::Reanimations::REANIM_CABBAGEPULT, &MakePlant<CabbagePult>);

	RegisterPlant(PlantType::PLANT_MELTSNOWPULT, "PLANT_MELTSNOWPULT",
		ResourceKeys::Textures::IMAGE_MELTSNOWPULT,
		AnimationType::ANIM_MELTSNOWPULT,
		ResourceKeys::Reanimations::REANIM_MELTSNOWPULT,
		&MakePlant<MeltSnowPult>);

	RegisterPlant(PlantType::PLANT_FROSTMINE, "PLANT_FROSTMINE",
		ResourceKeys::Textures::IMAGE_FROSTMINE,
		AnimationType::ANIM_FROSTMINE,
		ResourceKeys::Reanimations::REANIM_FROSTMINE,
		&MakePlant<FrostMine>);

	RegisterPlant(PlantType::PLANT_ALARMBELLFLOWER, "PLANT_ALARMBELLFLOWER",
		ResourceKeys::Textures::IMAGE_ALARMBELLFLOWER,
		AnimationType::ANIM_ALARMBELLFLOWER,
		ResourceKeys::Reanimations::REANIM_ALARMBELLFLOWER,
		&MakePlant<AlarmBellFlower>);

	RegisterPlant(PlantType::PLANT_BOUNDARYFLOWER, "PLANT_BOUNDARYFLOWER",
		ResourceKeys::Textures::IMAGE_BOUNDARYFLOWER,
		AnimationType::ANIM_BOUNDARYFLOWER,
		ResourceKeys::Reanimations::REANIM_BOUNDARYFLOWER,
		&MakePlant<BoundaryFlower>);

	RegisterPlant(PlantType::PLANT_CARRYVINE, "PLANT_CARRYVINE",
		ResourceKeys::Textures::IMAGE_CARRYVINE,
		AnimationType::ANIM_CARRYVINE,
		ResourceKeys::Reanimations::REANIM_CARRYVINE,
		&MakePlant<CarryVine>);

	RegisterPlant(PlantType::PLANT_DAWNLOTUS, "PLANT_DAWNLOTUS",
		ResourceKeys::Textures::IMAGE_DAWNLOTUS,
		AnimationType::ANIM_DAWNLOTUS,
		ResourceKeys::Reanimations::REANIM_DAWNLOTUS,
		&MakePlant<DawnLotus>);

	RegisterPlant(PlantType::PLANT_FURNACECOREFLOWER, "PLANT_FURNACECOREFLOWER",
		ResourceKeys::Textures::IMAGE_FURNACECOREFLOWER,
		AnimationType::ANIM_FURNACECOREFLOWER,
		ResourceKeys::Reanimations::REANIM_FURNACECOREFLOWER,
		&MakePlant<FurnaceCoreFlower>);

	RegisterPlant(PlantType::PLANT_LISTENINGGRASS, "PLANT_LISTENINGGRASS",
		ResourceKeys::Textures::IMAGE_LISTENINGGRASS,
		AnimationType::ANIM_LISTENINGGRASS,
		ResourceKeys::Reanimations::REANIM_LISTENINGGRASS,
		&MakePlant<ListeningGrass>);

	RegisterPlant(PlantType::PLANT_NORTHSTARFLOWER, "PLANT_NORTHSTARFLOWER",
		ResourceKeys::Textures::IMAGE_NORTHSTARFLOWER,
		AnimationType::ANIM_NORTHSTARFLOWER,
		ResourceKeys::Reanimations::REANIM_NORTHSTARFLOWER,
		&MakePlant<NorthStarFlower>);

	RegisterPlant(PlantType::PLANT_ICEMIRRORGRASS, "PLANT_ICEMIRRORGRASS",
		ResourceKeys::Textures::IMAGE_ICEMIRRORGRASS,
		AnimationType::ANIM_ICEMIRRORGRASS,
		ResourceKeys::Reanimations::REANIM_ICEMIRRORGRASS,
		&MakePlant<IceMirrorGrass>);

	RegisterPlant(PlantType::PLANT_KERNELPULT, "PLANT_KERNELPULT",
		ResourceKeys::Textures::IMAGE_CORNPULT,
		AnimationType::ANIM_KERNELPULT,
		ResourceKeys::Reanimations::REANIM_KERNELPULT, &MakePlant<KernelPult>);

	RegisterPlant(PlantType::PLANT_COBCANNON, "PLANT_COBCANNON",
		ResourceKeys::Textures::IMAGE_COBCANNON,
		AnimationType::ANIM_COBCANNON,
		ResourceKeys::Reanimations::REANIM_COBCANNON, &MakePlant<CobCannon>);

	RegisterPlant(PlantType::PLANT_INSTANT_COFFEE, "PLANT_INSTANT_COFFEE",
		ResourceKeys::Textures::IMAGE_COFFEEBEAN,
		AnimationType::ANIM_COFFEEBEAN,
		ResourceKeys::Reanimations::REANIM_COFFEEBEAN, &MakePlant<CoffeeBean>);

	RegisterPlant(PlantType::PLANT_GARLIC, "PLANT_GARLIC",
		ResourceKeys::Textures::IMAGE_GARLIC,
		AnimationType::ANIM_GARLIC,
		ResourceKeys::Reanimations::REANIM_GARLIC, &MakePlant<Garlic>);

	RegisterPlant(PlantType::PLANT_UMBRELLA, "PLANT_UMBRELLA",
		ResourceKeys::Textures::IMAGE_UMBRELLALEAF,
		AnimationType::ANIM_UMBRELLALEAF,
		ResourceKeys::Reanimations::REANIM_UMBRELLALEAF, &MakePlant<UmbrellaLeaf>);

	RegisterPlant(PlantType::PLANT_MARIGOLD, "PLANT_MARIGOLD",
		ResourceKeys::Textures::IMAGE_MARIGOLD,
		AnimationType::ANIM_MARIGOLD,
		ResourceKeys::Reanimations::REANIM_MARIGOLD, &MakePlant<Marigold>);

	RegisterPlant(PlantType::PLANT_MELONPULT, "PLANT_MELONPULT",
		ResourceKeys::Textures::IMAGE_MELONPULT,
		AnimationType::ANIM_MELONPULT,
		ResourceKeys::Reanimations::REANIM_MELONPULT, &MakePlant<MelonPult>);

	RegisterPlant(PlantType::PLANT_WINTERMELON, "PLANT_WINTERMELON",
		ResourceKeys::Textures::IMAGE_WINTERMELON,
		AnimationType::ANIM_WINTERMELON,
		ResourceKeys::Reanimations::REANIM_WINTERMELON, &MakePlant<WinterMelon>);

	RegisterPlant(PlantType::PLANT_IMITATER, "PLANT_IMITATER",
		ResourceKeys::Textures::IMAGE_IMITATER,
		AnimationType::ANIM_IMITATER,
		ResourceKeys::Reanimations::REANIM_IMITATER, &MakePlant<Imitater>);

	// 寒冰大喷菇：复用大喷菇 reanim（蓝色靠 overlay），仅卡图独立
	RegisterPlant(PlantType::PLANT_ICEFUMESHROOM, "PLANT_ICEFUMESHROOM",
		ResourceKeys::Textures::IMAGE_ICEFUMESHROOM,
		AnimationType::ANIM_ICEFUMESHROOM,
		"FumeShroom", &MakePlant<IceFumeShroom>);

	// ==================== 僵尸注册（仅身份，数值见 gamedata.json） ====================
	RegisterZombie(ZombieType::ZOMBIE_EXCAVATOR, "ZOMBIE_EXCAVATOR",
		AnimationType::ANIM_EXCAVATOR_ZOMBIE, "ExcavatorZombie", &MakeZombie<ExcavatorZombie>);
	RegisterZombie(ZombieType::ZOMBIE_NORMAL, "ZOMBIE_NORMAL",
		AnimationType::ANIM_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE, &MakeZombie<Zombie>);

	RegisterZombie(ZombieType::ZOMBIE_TRAFFIC_CONE, "ZOMBIE_TRAFFIC_CONE",
		AnimationType::ANIM_CONE_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_CONE_ZOMBIE, &MakeZombie<ConeZombie>);

	RegisterZombie(ZombieType::ZOMBIE_POLEVAULTER, "ZOMBIE_POLEVAULTER",
		AnimationType::ANIM_POLEVAULTER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_POLEVAULTER_ZOMBIE, &MakeZombie<Polevaulter>);

	RegisterZombie(ZombieType::ZOMBIE_BUCKET, "ZOMBIE_BUCKET",
		AnimationType::ANIM_BUCKET_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_BUCKET_ZOMBIE, &MakeZombie<BucketZombie>);

	RegisterZombie(ZombieType::ZOMBIE_FASTBUCKET, "ZOMBIE_FASTBUCKET",
		AnimationType::ANIM_BUCKET_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_BUCKET_ZOMBIE, &MakeZombie<FastBucketZombie>);

	RegisterZombie(ZombieType::ZOMBIE_NEWSPAPER, "ZOMBIE_NEWSPAPER",
		AnimationType::ANIM_PAPER_ZOMBIE,
		"PaperZombie", &MakeZombie<PaperZombie>);

	// 复用读报僵尸 reanim，仅换报纸贴图
	RegisterZombie(ZombieType::ZOMBIE_FASTPAPER, "ZOMBIE_FASTPAPER",
		AnimationType::ANIM_PAPER_ZOMBIE,
		"PaperZombie", &MakeZombie<FastPaperZombie>);

	RegisterZombie(ZombieType::ZOMBIE_DOOR, "ZOMBIE_DOOR",
		AnimationType::ANIM_DOOR_ZOMBIE,
		"DoorZombie", &MakeZombie<DoorZombie>);

	RegisterZombie(ZombieType::ZOMBIE_FOOTBALL, "ZOMBIE_FOOTBALL",
		AnimationType::ANIM_FOOTBALL_ZOMBIE,
		"FootballZombie", &MakeZombie<FootballZombie>);

	RegisterZombie(ZombieType::ZOMBIE_PINK_FOOTBALL, "ZOMBIE_PINK_FOOTBALL",
		AnimationType::ANIM_PINK_FOOTBALL_ZOMBIE,
		"PinkFootballZombie", &MakeZombie<PinkFootballZombie>);

	// 舞王僵尸（MJ版）：入场后打响指召唤十字 4 伴舞，死伴舞按节拍补位
	RegisterZombie(ZombieType::ZOMBIE_DANCER, "ZOMBIE_DANCER",
		AnimationType::ANIM_DANCE_ZOMBIE,
		"ZombieJackson", &MakeZombie<DancerZombie>);

	// 伴舞僵尸：gamedata weight=0，只能被舞王召唤（或 AutoTest spawn_zombie 直造）
	RegisterZombie(ZombieType::ZOMBIE_BACKUP_DANCER, "ZOMBIE_BACKUP_DANCER",
		AnimationType::ANIM_DANCERWITH_ZOMBIE,
		"ZombieDancer", &MakeZombie<BackupDancerZombie>);

	// 精英舞王：正式波次仅由强台风以上天气把普通舞王变异为该类型。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_DANCER, "ZOMBIE_ELITE_DANCER",
		AnimationType::ANIM_ELITE_DANCE_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ZOMBIE_ELITE_JACKSON,
		&MakeZombie<EliteDancerZombie>);

	// 加固铁门：复用铁门动画，派生类在 SetupZombie 内替换门材质并覆盖耐久规则。
	RegisterZombie(ZombieType::ZOMBIE_REINFORCED_DOOR, "ZOMBIE_REINFORCED_DOOR",
		AnimationType::ANIM_DOOR_ZOMBIE,
		"DoorZombie", &MakeZombie<ReinforcedDoorZombie>);

	// 泳池三种地形变体不独立进权重池，由 Board 在选中水路后把基础类型解析为对应变体。
	RegisterZombie(ZombieType::ZOMBIE_POOL_NORMAL, "ZOMBIE_POOL_NORMAL",
		AnimationType::ANIM_POOL_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_POOL_NORMAL_ZOMBIE, &MakeZombie<PoolNormalZombie>);
	RegisterZombie(ZombieType::ZOMBIE_POOL_CONE, "ZOMBIE_POOL_CONE",
		AnimationType::ANIM_POOL_CONE_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_POOL_CONE_ZOMBIE, &MakeZombie<PoolConeZombie>);
	RegisterZombie(ZombieType::ZOMBIE_POOL_BUCKET, "ZOMBIE_POOL_BUCKET",
		AnimationType::ANIM_POOL_BUCKET_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_POOL_BUCKET_ZOMBIE, &MakeZombie<PoolBucketZombie>);

	// 精英撑杆：复用普通撑杆时间线，独立 reanim 只替换红蓝运动服材质。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_POLEVAULTER, "ZOMBIE_ELITE_POLEVAULTER",
		AnimationType::ANIM_ELITE_POLEVAULTER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ELITE_POLEVAULTER_ZOMBIE,
		&MakeZombie<ElitePolevaulterZombie>);

	// 冰车使用车辆专属状态机，不复用普通僵尸的走路/死亡帧事件。
	RegisterZombie(ZombieType::ZOMBIE_ZAMBONI, "ZOMBIE_ZAMBONI",
		AnimationType::ANIM_ZAMBONI_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ZAMBONI_ZOMBIE,
		&MakeZombie<ZamboniZombie>);

	// 鎏金冰车复用车辆时间线，以独立 reanim 键替换黄色材质并保留普通冰车资源。
	RegisterZombie(ZombieType::ZOMBIE_GILDED_ZAMBONI, "ZOMBIE_GILDED_ZAMBONI",
		AnimationType::ANIM_GILDED_ZAMBONI_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_GILDED_ZAMBONI_ZOMBIE,
		&MakeZombie<GildedZamboniZombie>);

	// 海豚僵尸拥有独立的入水、骑乘、跃豚与弃豚状态机。
	RegisterZombie(ZombieType::ZOMBIE_DOLPHIN_RIDER, "ZOMBIE_DOLPHIN_RIDER",
		AnimationType::ANIM_DOLPHIN_RIDER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_DOLPHIN_RIDER_ZOMBIE,
		&MakeZombie<DolphinRiderZombie>);

	// 精英海豚复用普通时间线，只替换材质并把成功越障容量扩展为两次。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_DOLPHIN_RIDER, "ZOMBIE_ELITE_DOLPHIN_RIDER",
		AnimationType::ANIM_ELITE_DOLPHIN_RIDER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ELITE_DOLPHIN_RIDER_ZOMBIE,
		&MakeZombie<EliteDolphinRiderZombie>);

	// 小丑僵尸拥有随机开盒倒计时、完整音效生命周期与专属范围爆炸。
	RegisterZombie(ZombieType::ZOMBIE_JACK_IN_THE_BOX, "ZOMBIE_JACK_IN_THE_BOX",
		AnimationType::ANIM_JACK_IN_THE_BOX_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_JACK_IN_THE_BOX_ZOMBIE,
		&MakeZombie<JackInTheBoxZombie>);

	// 精英小丑复用普通小丑时间线；派生类不注册自爆帧，改为持续投盒。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX,
		"ZOMBIE_ELITE_JACK_IN_THE_BOX",
		AnimationType::ANIM_ELITE_JACK_IN_THE_BOX_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ELITE_JACK_IN_THE_BOX_ZOMBIE,
		&MakeZombie<EliteJackInTheBoxZombie>);

	// 气球生命层破裂前处于空中；陆地完成爆裂演出后转为普通步行。
	RegisterZombie(ZombieType::ZOMBIE_BALLOON, "ZOMBIE_BALLOON",
		AnimationType::ANIM_BALLOON_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_BALLOON_ZOMBIE,
		&MakeZombie<BalloonZombie>);

	// 矿工拥有地下穿行、两种出土路径及向前线折返的独立状态机。
	RegisterZombie(ZombieType::ZOMBIE_DIGGER, "ZOMBIE_DIGGER",
		AnimationType::ANIM_DIGGER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_DIGGER_ZOMBIE,
		&MakeZombie<DiggerZombie>);

	// 爆破工头复用矿工状态机，在眩晕结束钩子结算固定后排爆破。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_DIGGER, "ZOMBIE_ELITE_DIGGER",
		AnimationType::ANIM_ELITE_DIGGER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ELITE_DIGGER_ZOMBIE,
		&MakeZombie<EliteDiggerZombie>);

	// 跳跳僵尸使用独立弹跳状态机；高坚果阻拦后才回落到普通根运动步行。
	RegisterZombie(ZombieType::ZOMBIE_POGO, "ZOMBIE_POGO",
		AnimationType::ANIM_POGO_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_POGO_ZOMBIE,
		&MakeZombie<PogoZombie>);

	// 精英跳跳复用普通跳跳时序，并由独立配色资源与缓冲器状态扩展能力。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_POGO, "ZOMBIE_ELITE_POGO",
		AnimationType::ANIM_ELITE_POGO_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ELITE_POGO_ZOMBIE,
		&MakeZombie<ElitePogoZombie>);

	// 蹦极僵尸使用独立垂直状态机；普通刷怪不接入 5-5 的固定五只特殊编排。
	RegisterZombie(ZombieType::ZOMBIE_BUNGEE, "ZOMBIE_BUNGEE",
		AnimationType::ANIM_BUNGEE_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_BUNGEE_ZOMBIE,
		&MakeZombie<BungeeZombie>);

	// 扶梯僵尸携带可破坏/可磁吸护盾，并为坚果类创建全地面僵尸共享的攀爬物。
	RegisterZombie(ZombieType::ZOMBIE_LADDER, "ZOMBIE_LADDER",
		AnimationType::ANIM_LADDER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_LADDER_ZOMBIE,
		&MakeZombie<LadderZombie>);

	// 精英扶梯复用经典时间线，在五秒整行扫描后锁定能力分支。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_LADDER, "ZOMBIE_ELITE_LADDER",
		AnimationType::ANIM_ELITE_LADDER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ELITE_LADDER_ZOMBIE,
		&MakeZombie<EliteLadderZombie>);

	// 投篮车拥有独立十二发篮球、碾压、车辆损坏及专属死亡状态机。
	RegisterZombie(ZombieType::ZOMBIE_CATAPULT, "ZOMBIE_CATAPULT",
		AnimationType::ANIM_CATAPULT_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_CATAPULT_ZOMBIE,
		&MakeZombie<CatapultZombie>);

	// 导流投篮车继承普通投篮车全部弹药行为，只增加屋顶锁行与自身径流倍率。
	RegisterZombie(ZombieType::ZOMBIE_ELITE_CATAPULT, "ZOMBIE_ELITE_CATAPULT",
		AnimationType::ANIM_ELITE_CATAPULT_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ELITE_CATAPULT_ZOMBIE,
		&MakeZombie<EliteCatapultZombie>);

	// 巨人拥有独立砸击/投掷状态机；小鬼权重为零，只允许由巨人召唤或测试直造。
	RegisterZombie(ZombieType::ZOMBIE_GARGANTUAR, "ZOMBIE_GARGANTUAR",
		AnimationType::ANIM_GARGANTUAR_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_GARGANTUAR_ZOMBIE,
		&MakeZombie<GargantuarZombie>);
	RegisterZombie(ZombieType::ZOMBIE_IMP, "ZOMBIE_IMP",
		AnimationType::ANIM_IMP_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_IMP_ZOMBIE,
		&MakeZombie<ImpZombie>);
	// 红眼巨人复用经典巨人时间线；7-8/7-9 由数据表投放，冒险波次上限由 Board 统一执行。
	RegisterZombie(ZombieType::ZOMBIE_REDEYE_GARGANTUAR, "ZOMBIE_REDEYE_GARGANTUAR",
		AnimationType::ANIM_GARGANTUAR_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_GARGANTUAR_ZOMBIE,
		&MakeZombie<RedeyeGargantuarZombie>);

	// 5-9 屋脊督军先以视觉样机注册；权重为零，不会污染正式冒险或生存出怪池。
	RegisterZombie(ZombieType::ZOMBIE_ROOF_MARSHAL, "ZOMBIE_ROOF_MARSHAL",
		AnimationType::ANIM_ROOF_MARSHAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_ROOF_MARSHAL_ZOMBIE,
		&MakeZombie<RoofMarshalZombie>);

	// 绝缘僵尸复用普通僵尸骨架；陶瓷胸甲作为 Zombie_body 的末尾前景 follower。
	RegisterZombie(ZombieType::ZOMBIE_INSULATOR, "ZOMBIE_INSULATOR",
		AnimationType::ANIM_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE,
		&MakeZombie<InsulatorZombie>);

	// 劫持者独立复用小丑时间线，75% 锁定与处决计时全部由黑夜屋顶 Board 状态拥有。
	RegisterZombie(ZombieType::ZOMBIE_HIJACKER, "ZOMBIE_HIJACKER",
		AnimationType::ANIM_HIJACKER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_HIJACKER_ZOMBIE,
		&MakeZombie<HijackerZombie>);

	// 急救员复用普通僵尸时间线，以逻辑计时驱动双模式治疗并用身体 follower 区分状态。
	RegisterZombie(ZombieType::ZOMBIE_HEALER, "ZOMBIE_HEALER",
		AnimationType::ANIM_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE,
		&MakeZombie<HealerZombie>);

	// 接地僵尸复用路障时间线，仅把三阶段护具换成带铜天线的独立电紫资源。
	RegisterZombie(ZombieType::ZOMBIE_GROUNDING, "ZOMBIE_GROUNDING",
		AnimationType::ANIM_GROUNDING_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_GROUNDING_ZOMBIE,
		&MakeZombie<GroundingZombie>);

	// 一次正式出怪由队长在首更新补齐三名同类型跟随者；跟随者不重复消费波次预算。
	RegisterZombie(ZombieType::ZOMBIE_BOBSLED_TEAM, "ZOMBIE_BOBSLED_TEAM",
		AnimationType::ANIM_BOBSLED_TEAM_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_BOBSLED_TEAM_ZOMBIE,
		&MakeZombie<BobsledTeamZombie>);

	// 工程师复用路障完整时间轴；逻辑计时施工不增加死亡或啃食帧事件。
	RegisterZombie(ZombieType::ZOMBIE_ICE_WALL_ENGINEER, "ZOMBIE_ICE_WALL_ENGINEER",
		AnimationType::ANIM_CONE_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_CONE_ZOMBIE,
		&MakeZombie<IceWallEngineerZombie>);

	// 钻机复用路障骨骼和帧事件；独立附着 reanim 负责四相钻齿与三档破损轮廓。
	RegisterZombie(ZombieType::ZOMBIE_ICE_CRACK_DRILL, "ZOMBIE_ICE_CRACK_DRILL",
		AnimationType::ANIM_ICE_CRACK_DRILL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_CONE_ZOMBIE,
		&MakeZombie<IceCrackDrillZombie>);

	// 气象设备是非磁性的独立附着层；铁桶本体完整复用标准铁桶防具契约。
	RegisterZombie(ZombieType::ZOMBIE_WEATHER_JAMMER, "ZOMBIE_WEATHER_JAMMER",
		AnimationType::ANIM_BUCKET_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_BUCKET_ZOMBIE,
		&MakeZombie<WeatherJammerZombie>);

	// 处刑者复用扶梯全时间线：隐藏梯子、替换冰锤，并让原版红帽作为 anim_head1 命名 follower。
	RegisterZombie(ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER,
		"ZOMBIE_ICE_STATUE_EXECUTIONER",
		AnimationType::ANIM_LADDER_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_LADDER_ZOMBIE,
		&MakeZombie<IceStatueExecutionerZombie>);

	// 潜雪僵尸独立复用矿工时间线；雪帽与雪铲无耐久，全部潜雪状态由本体拥有。
	RegisterZombie(ZombieType::ZOMBIE_SNOW_BURROW, "ZOMBIE_SNOW_BURROW",
		AnimationType::ANIM_SNOW_BURROW_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_SNOW_BURROW_ZOMBIE,
		&MakeZombie<SnowBurrowZombie>);

	// 适应头盔复用普通僵尸时间线；头盔和胸章是命名 follower，不新增帧事件。
	RegisterZombie(ZombieType::ZOMBIE_ADAPTIVE_HELMET, "ZOMBIE_ADAPTIVE_HELMET",
		AnimationType::ANIM_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE,
		&MakeZombie<AdaptiveHelmetZombie>);

	// 热感狙击手复用普通僵尸完整时间线；装填和出膛由独立逻辑计时，不增加帧事件。
	RegisterZombie(ZombieType::ZOMBIE_THERMAL_SNIPER, "ZOMBIE_THERMAL_SNIPER",
		AnimationType::ANIM_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE,
		&MakeZombie<ThermalSniperZombie>);

	// 两类终章僵尸完整复用普通僵尸时间轴；身份件与提交演出均为命名 follower/粒子。
	RegisterZombie(ZombieType::ZOMBIE_AURORA_PRIEST, "ZOMBIE_AURORA_PRIEST",
		AnimationType::ANIM_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE,
		&MakeZombie<AuroraPriestZombie>);

	RegisterZombie(ZombieType::ZOMBIE_POLAR_CLOCKMAKER, "ZOMBIE_POLAR_CLOCKMAKER",
		AnimationType::ANIM_NORMAL_ZOMBIE,
		ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE,
		&MakeZombie<PolarClockmakerZombie>);

	// ==================== 非植物/僵尸动画映射 ====================
	mAnimToString[AnimationType::ANIM_SUN] = ResourceKeys::Reanimations::REANIM_SUN;

	mAnimToString[AnimationType::ANIM_ZOMBIE_CHARRED] =
		ResourceKeys::Reanimations::REANIM_ZOMBIE_CHARRED;
	mAnimToString[AnimationType::ANIM_ZAMBONI_CHARRED] =
		ResourceKeys::Reanimations::REANIM_ZAMBONI_CHARRED;
	mAnimToString[AnimationType::ANIM_CATAPULT_CHARRED] =
		ResourceKeys::Reanimations::REANIM_CATAPULT_CHARRED;
	mAnimToString[AnimationType::ANIM_GARGANTUAR_CHARRED] =
		ResourceKeys::Reanimations::REANIM_GARGANTUAR_CHARRED;
	mAnimToString[AnimationType::ANIM_IMP_CHARRED] =
		ResourceKeys::Reanimations::REANIM_IMP_CHARRED;
	mAnimToString[AnimationType::ANIM_DIGGER_RISING_DIRT] =
		ResourceKeys::Reanimations::REANIM_DIGGER_RISING_DIRT;
	mAnimToString[AnimationType::ANIM_DIGGER_CHARRED] =
		ResourceKeys::Reanimations::REANIM_DIGGER_CHARRED;
	mAnimToString[AnimationType::ANIM_ZOMBIE_SURPRISE] =
		ResourceKeys::Reanimations::REANIM_ZOMBIE_SURPRISE;

	mAnimToString[AnimationType::ANIM_LAWNMOWER] =
		ResourceKeys::Reanimations::REANIM_LAWNMOWER;
	mAnimToString[AnimationType::ANIM_POOL_CLEANER] =
		ResourceKeys::Reanimations::REANIM_POOL_CLEANER;
	mAnimToString[AnimationType::ANIM_ROOF_CLEANER] =
		ResourceKeys::Reanimations::REANIM_ROOF_CLEANER;
	mAnimToString[AnimationType::ANIM_JALAPENO_FIRE] =
		ResourceKeys::Reanimations::REANIM_JALAPENO_FIRE;
	mAnimToString[AnimationType::ANIM_FIREPEA] =
		ResourceKeys::Reanimations::REANIM_FIREPEA;
	mAnimToString[AnimationType::ANIM_POOL_SPLASH] =
		ResourceKeys::Reanimations::REANIM_POOL_SPLASH;

	mAnimToString[AnimationType::ANIM_NONE] = "Unknown";
}

void GameDataManager::RegisterPlant(PlantType type,
	const std::string& enumName,
	const std::string& textureKey,
	AnimationType animType,
	const std::string& animName,
	PlantFactoryFn factory) {
	PlantInfo info;
	info.type = type;
	info.enumName = enumName;
	info.textureKey = textureKey;
	info.animType = animType;
	info.animName = animName;
	info.factory = factory;
	mPlantInfo[type] = info;

	mEnumNameToType[enumName] = type;
	mTextureKeyToType[textureKey] = type;
	mAnimNameToType[animName] = type;
	mAnimToString[animType] = animName;

	LOG_DEBUG("GameData") << "注册植物身份: " << enumName;
}

void GameDataManager::RegisterZombie(ZombieType type,
	const std::string& enumName,
	AnimationType animType,
	const std::string& animName,
	ZombieFactoryFn factory) {
	ZombieInfo info;
	info.type = type;
	info.enumName = enumName;
	info.animType = animType;
	info.animName = animName;
	info.factory = factory;
	mZombieInfo[type] = info;

	// 记录动画类型->资源名，以便通过 AnimationType 统一查询
	mAnimToString[animType] = animName;

	LOG_DEBUG("GameData") << "注册僵尸身份: " << enumName;
}

bool GameDataManager::LoadNumbersFromJson() {
	std::vector<std::string> errors;
	nlohmann::json data;
	if (!FileManager::LoadJsonFile("./resources/gamedata.json", data)) {
		errors.push_back("resources/gamedata.json 缺失或解析失败");
	}
	else {
		// 单字段读取器：缺失/类型不符记错（不中断，攒齐所有错误一次报告）
		auto readInt = [&errors](const nlohmann::json& e, const char* field,
			const std::string& who, int& out) {
			if (!e.contains(field) || !e[field].is_number_integer()) {
				errors.push_back(who + " 缺字段 \"" + field + "\"（须为整数）");
				return;
			}
			out = e[field].get<int>();
		};
		auto readFloat = [&errors](const nlohmann::json& e, const char* field,
			const std::string& who, float& out) {
			if (!e.contains(field) || !e[field].is_number()) {
				errors.push_back(who + " 缺字段 \"" + field + "\"（须为数字）");
				return;
			}
			out = e[field].get<float>();
		};
		auto readOffset = [&errors](const nlohmann::json& e,
			const std::string& who, Vector& out) {
			if (!e.contains("offset") || !e["offset"].is_array() || e["offset"].size() != 2
				|| !e["offset"][0].is_number() || !e["offset"][1].is_number()) {
				errors.push_back(who + " 缺字段 \"offset\"（须为双元素数字数组）");
				return;
			}
			out = Vector(e["offset"][0].get<float>(), e["offset"][1].get<float>());
		};
		auto readSimulation = [&errors](const nlohmann::json& e,
			const std::string& who, PlantSimulationProfile& out) {
			if (!e.contains("simulation")) return;
			if (!e["simulation"].is_object()) {
				errors.push_back(who + " 的 \"simulation\" 须为对象");
				return;
			}
			const nlohmann::json& simulation = e["simulation"];
			auto readOptionalFloat = [&errors, &simulation, &who](
				const char* field, float& value) {
				if (!simulation.contains(field)) return;
				if (!simulation[field].is_number()) {
					errors.push_back(who + ".simulation 的 \"" + field
						+ "\" 须为数字");
					return;
				}
				value = simulation[field].get<float>();
			};
			auto readOptionalInt = [&errors, &simulation, &who](
				const char* field, int& value) {
				if (!simulation.contains(field)) return;
				if (!simulation[field].is_number_integer()) {
					errors.push_back(who + ".simulation 的 \"" + field
						+ "\" 须为整数");
					return;
				}
				value = simulation[field].get<int>();
			};
			readOptionalInt("baseHealth", out.baseHealth);
			readOptionalFloat("attackDps", out.attackDps);
			readOptionalInt("attackRowRadius", out.attackRowRadius);
			readOptionalInt("mineAttackShape", out.mineAttackShape);
			readOptionalInt("mineAttackRange", out.mineAttackRange);
			if (simulation.contains("mineMultiTarget")) {
				if (simulation["mineMultiTarget"].is_boolean())
					out.mineMultiTarget = simulation["mineMultiTarget"].get<bool>();
				else errors.push_back(who + ".simulation.mineMultiTarget 须为布尔值");
			}
			readOptionalFloat("sunPerSecond", out.sunPerSecond);
			readOptionalFloat("firstSunDelay", out.firstSunDelay);
			readOptionalFloat("slowApplicationsPerSecond", out.slowApplicationsPerSecond);
			readOptionalFloat("slowDuration", out.slowDuration);
			readOptionalFloat("frozenApplicationsPerSecond", out.frozenApplicationsPerSecond);
			readOptionalFloat("frozenDuration", out.frozenDuration);
			readOptionalFloat("butterApplicationsPerSecond", out.butterApplicationsPerSecond);
			readOptionalFloat("butterDuration", out.butterDuration);
			readOptionalFloat("paralysisApplicationsPerSecond", out.paralysisApplicationsPerSecond);
			readOptionalFloat("paralysisDuration", out.paralysisDuration);
			readOptionalFloat("magneticPulseCooldown", out.magneticPulseCooldown);
			readOptionalFloat("magneticPulseRadius", out.magneticPulseRadius);
			readOptionalFloat("magneticPulseParalysisDuration", out.magneticPulseParalysisDuration);
			readOptionalInt("magneticSearchRowRadius", out.magneticSearchRowRadius);
			readOptionalFloat("magneticSearchRadiusInCells", out.magneticSearchRadiusInCells);
			readOptionalFloat("magneticEatingSearchRadiusInCells", out.magneticEatingSearchRadiusInCells);
			readOptionalFloat("cobBlastCooldown", out.cobBlastCooldown);
			readOptionalFloat("cobBlastDamage", out.cobBlastDamage);
			readOptionalFloat("cobBlastRadius", out.cobBlastRadius);
			readOptionalInt("cobBlastRowRadius", out.cobBlastRowRadius);
			if (simulation.contains("daytimeDormant")) {
				if (!simulation["daytimeDormant"].is_boolean()) {
					errors.push_back(who
						+ ".simulation 的 \"daytimeDormant\" 须为布尔值");
				}
				else {
					out.daytimeDormant = simulation["daytimeDormant"].get<bool>();
				}
			}
			if (simulation.contains("persistent")) {
				if (!simulation["persistent"].is_boolean()) {
					errors.push_back(who
						+ ".simulation 的 \"persistent\" 须为布尔值");
				}
				else {
					out.persistent = simulation["persistent"].get<bool>();
				}
			}
			if (simulation.contains("futurePlantable")) {
				if (!simulation["futurePlantable"].is_boolean()) {
					errors.push_back(who
						+ ".simulation 的 \"futurePlantable\" 须为布尔值");
				}
				else {
					out.futurePlantable = simulation["futurePlantable"].get<bool>();
				}
			}
			if (simulation.contains("supportOnly")) {
				if (!simulation["supportOnly"].is_boolean()) {
					errors.push_back(who
						+ ".simulation 的 \"supportOnly\" 须为布尔值");
				}
				else {
					out.supportOnly = simulation["supportOnly"].get<bool>();
				}
			}
			if (out.baseHealth <= 0 || out.attackDps < 0.0f
				|| out.attackRowRadius < 0 || out.sunPerSecond < 0.0f
				|| out.mineAttackShape < 0 || out.mineAttackShape == 1 || out.mineAttackShape > 3
				|| out.mineAttackRange < 0 || out.mineAttackRange > 9
				|| out.firstSunDelay < 0.0f
				|| out.slowApplicationsPerSecond < 0.0f || out.slowDuration < 0.0f
				|| out.frozenApplicationsPerSecond < 0.0f || out.frozenDuration < 0.0f
				|| out.butterApplicationsPerSecond < 0.0f || out.butterDuration < 0.0f
				|| out.paralysisApplicationsPerSecond < 0.0f
				|| out.paralysisDuration < 0.0f
				|| out.magneticPulseCooldown < 0.0f
				|| out.magneticPulseRadius < 0.0f
				|| out.magneticPulseParalysisDuration < 0.0f
				|| out.magneticSearchRowRadius < 0
				|| out.magneticSearchRadiusInCells < 0.0f
				|| out.magneticEatingSearchRadiusInCells < 0.0f
				|| out.cobBlastCooldown < 0.0f
				|| out.cobBlastDamage < 0.0f
				|| out.cobBlastRadius < 0.0f
				|| out.cobBlastRowRadius < 0) {
				errors.push_back(who + ".simulation 含越界负数或零生命");
			}
		};

		const bool hasPlants = data.contains("plants") && data["plants"].is_object();
		const bool hasZombies = data.contains("zombies") && data["zombies"].is_object();
		if (!hasPlants) errors.push_back("缺 \"plants\" 对象");
		if (!hasZombies) errors.push_back("缺 \"zombies\" 对象");

		if (hasPlants) {
			const nlohmann::json& plants = data["plants"];
			for (auto& pair : mPlantInfo) {
				PlantInfo& info = pair.second;
				if (!plants.contains(info.enumName)) {
					errors.push_back("缺 " + info.enumName + " 的条目");
					continue;
				}
				const nlohmann::json& e = plants[info.enumName];
				readInt(e, "cost", info.enumName, info.SunCost);
				readFloat(e, "cooldown", info.enumName, info.Cooldown);
				readOffset(e, info.enumName, info.offset);
				readFloat(e, "scale", info.enumName, info.scale);
				readSimulation(e, info.enumName, info.simulation);
			}
			for (auto& item : plants.items()) {
				if (mEnumNameToType.find(item.key()) == mEnumNameToType.end())
					LOG_WARN("GameData") << "gamedata.json 有未注册的植物键: " << item.key();
			}
		}

		if (hasZombies) {
			const nlohmann::json& zombies = data["zombies"];
			std::unordered_set<std::string> registeredNames;
			for (auto& pair : mZombieInfo) {
				ZombieInfo& info = pair.second;
				registeredNames.insert(info.enumName);
				if (!zombies.contains(info.enumName)) {
					errors.push_back("缺 " + info.enumName + " 的条目");
					continue;
				}
				const nlohmann::json& e = zombies[info.enumName];
				readInt(e, "weight", info.enumName, info.weight);
				readInt(e, "appearWave", info.enumName, info.appearWave);
				readInt(e, "survivalRound", info.enumName, info.survivalRound);
				if (e.contains("mineFormationRole")) readInt(e,"mineFormationRole",info.enumName,info.mineFormationRole);
				if (info.mineFormationRole < 0 || info.mineFormationRole > 3)
					errors.push_back(info.enumName + ".mineFormationRole 须为 0..3");
				readOffset(e, info.enumName, info.offset);
				readFloat(e, "scale", info.enumName, info.scale);
			}
			for (auto& item : zombies.items()) {
				if (registeredNames.find(item.key()) == registeredNames.end())
					LOG_WARN("GameData") << "gamedata.json 有未注册的僵尸键: " << item.key();
			}
		}
	}

	if (errors.empty()) return true;

	std::string all;
	for (const auto& msg : errors) {
		LOG_ERROR("GameData") << "gamedata.json: " << msg;
		all += msg;
		all += "\n";
	}
	// AutoTest/无头运行不弹模态框（否则负向测试卡死拿不到退出码）
	if (!TestDriver::GetInstance().IsActive()) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
			"gamedata.json 配置错误", all.c_str(), nullptr);
	}
	return false;
}

std::shared_ptr<Plant> GameDataManager::CreatePlant(PlantType type, Board* board, int row, int col, bool isPreview) const {
	auto it = mPlantInfo.find(type);
	if (it == mPlantInfo.end() || !it->second.factory) {
		LOG_ERROR("GameData") << "未注册或缺工厂的植物类型: " << static_cast<int>(type);
		return nullptr;
	}
	const PlantInfo& i = it->second;
	return i.factory(board, type, row, col, i.animType, i.scale, isPreview);
}

std::shared_ptr<Zombie> GameDataManager::CreateZombie(ZombieType type, Board* board, float x, float y, int row, bool isPreview) const {
	auto it = mZombieInfo.find(type);
	if (it == mZombieInfo.end() || !it->second.factory) {
		LOG_ERROR("GameData") << "未注册或缺工厂的僵尸类型: " << static_cast<int>(type);
		return nullptr;
	}
	const ZombieInfo& i = it->second;
	return i.factory(board, type, x, y, row, i.animType, i.scale, isPreview);
}

std::string GameDataManager::GetPlantTextureKey(PlantType plantType) const {
	auto it = mPlantInfo.find(plantType);
	if (it != mPlantInfo.end())
		return it->second.textureKey;
	return "IMAGE_PLANT_DEFAULT";
}

AnimationType GameDataManager::GetPlantAnimationType(PlantType plantType) const {
	auto it = mPlantInfo.find(plantType);
	if (it != mPlantInfo.end())
		return it->second.animType;
	return AnimationType::ANIM_NONE;
}

std::string GameDataManager::GetAnimationName(AnimationType animType) const {
	// 优先在植物动画中查找
	for (const auto& pair : mPlantInfo) {
		if (pair.second.animType == animType)
			return pair.second.animName;
	}
	// 然后在僵尸动画中查找
	for (const auto& pair : mZombieInfo) {
		if (pair.second.animType == animType)
			return pair.second.animName;
	}
	// 最后在通用映射中查找
	auto it = mAnimToString.find(animType);
	if (it != mAnimToString.end())
		return it->second;
	return "Unknown";
}

Vector GameDataManager::GetPlantOffset(PlantType plantType) const {
	auto it = mPlantInfo.find(plantType);
	if (it != mPlantInfo.end())
		return it->second.offset;
	return Vector(0, 0);
}

void GameDataManager::SetPlantOffset(PlantType plantType, const Vector& offset) {
	auto it = mPlantInfo.find(plantType);
	if (it != mPlantInfo.end()) {
		it->second.offset = offset;
		LOG_DEBUG("GameData") << "设置植物偏移: " << it->second.animName
			<< " -> (" << offset.x << ", " << offset.y << ")";
	}
}

std::string GameDataManager::PlantTypeToEnumName(PlantType type) const {
	auto it = mPlantInfo.find(type);
	if (it != mPlantInfo.end())
		return it->second.enumName;
	return "PLANT_NONE";
}

std::string GameDataManager::PlantTypeToTextureKey(PlantType type) const {
	return GetPlantTextureKey(type);
}

std::string GameDataManager::PlantTypeToAnimName(PlantType type) const {
	auto it = mPlantInfo.find(type);
	if (it != mPlantInfo.end())
		return it->second.animName;
	return "Unknown";
}

PlantType GameDataManager::StringToPlantType(const std::string& str) const {
	auto enumIt = mEnumNameToType.find(str);
	if (enumIt != mEnumNameToType.end())
		return enumIt->second;

	auto texIt = mTextureKeyToType.find(str);
	if (texIt != mTextureKeyToType.end())
		return texIt->second;

	auto animIt = mAnimNameToType.find(str);
	if (animIt != mAnimNameToType.end())
		return animIt->second;

	return PlantType::NUM_PLANT_TYPES;
}

std::vector<PlantType> GameDataManager::GetAllPlantTypes() const {
	std::vector<PlantType> types;
	for (const auto& pair : mPlantInfo)
		types.push_back(pair.first);
	return types;
}

bool GameDataManager::HasPlant(PlantType type) const {
	return mPlantInfo.find(type) != mPlantInfo.end();
}

int GameDataManager::GetPlantSunCost(PlantType plantType) const {
	auto it = mPlantInfo.find(plantType);
	if (it != mPlantInfo.end())
		return it->second.SunCost;
	return 0;
}

float GameDataManager::GetPlantCooldown(PlantType plantType) const {
	auto it = mPlantInfo.find(plantType);
	if (it != mPlantInfo.end())
		return it->second.Cooldown;
	return 0.0f;
}

const PlantSimulationProfile& GameDataManager::GetPlantSimulationProfile(
	PlantType plantType) const
{
	auto it = mPlantInfo.find(plantType);
	if (it != mPlantInfo.end()) return it->second.simulation;
	static const PlantSimulationProfile kDefaultProfile;
	return kDefaultProfile;
}

AnimationType GameDataManager::GetZombieAnimationType(ZombieType zombieType) const {
	auto it = mZombieInfo.find(zombieType);
	if (it != mZombieInfo.end())
		return it->second.animType;
	return AnimationType::ANIM_NONE;
}

std::string GameDataManager::GetZombieAnimName(ZombieType zombieType) const {
	auto it = mZombieInfo.find(zombieType);
	if (it != mZombieInfo.end())
		return it->second.animName;
	return "Unknown";
}

Vector GameDataManager::GetZombieOffset(ZombieType zombieType) const {
	auto it = mZombieInfo.find(zombieType);
	if (it != mZombieInfo.end())
		return it->second.offset;
	return Vector(0, 0);
}

void GameDataManager::SetZombieOffset(ZombieType zombieType, const Vector& offset) {
	auto it = mZombieInfo.find(zombieType);
	if (it != mZombieInfo.end()) {
		it->second.offset = offset;
		LOG_DEBUG("GameData") << "设置僵尸偏移: " << it->second.animName
			<< " -> (" << offset.x << ", " << offset.y << ")";
	}
}

int GameDataManager::GetZombieMineFormationRole(ZombieType type) const
{
	const auto it = mZombieInfo.find(type);
	return it != mZombieInfo.end() ? it->second.mineFormationRole : 0;
}

int GameDataManager::GetZombieWeight(ZombieType zombieType) const
{
	auto it = mZombieInfo.find(zombieType);
	if (it != mZombieInfo.end())
		return it->second.weight;
	return 0;
}

int GameDataManager::GetZombieAppearWave(ZombieType zombieType) const
{
	auto it = mZombieInfo.find(zombieType);
	if (it != mZombieInfo.end())
		return it->second.appearWave;
	return 0;
}

int GameDataManager::GetZombieSurvivalRound(ZombieType zombieType) const
{
	auto it = mZombieInfo.find(zombieType);
	if (it != mZombieInfo.end())
		return it->second.survivalRound;
	return 0;
}

std::string GameDataManager::ZombieTypeToEnumName(ZombieType type) const {
	auto it = mZombieInfo.find(type);
	if (it != mZombieInfo.end())
		return it->second.enumName;
	return "ZOMBIE_NONE";
}

std::vector<ZombieType> GameDataManager::GetAllZombieTypes() const {
	std::vector<ZombieType> types;
	for (const auto& pair : mZombieInfo)
		types.push_back(pair.first);
	return types;
}

bool GameDataManager::HasZombie(ZombieType type) const {
	return mZombieInfo.find(type) != mZombieInfo.end();
}

void GameDataManager::DebugPrintAll() const {
	LOG_DEBUG("GameData") << "========== GameDataManager 数据（数值来自 gamedata.json） ==========";
	LOG_DEBUG("GameData") << "植物总数: " << mPlantInfo.size();
	for (const auto& pair : mPlantInfo) {
		const PlantInfo& info = pair.second;
		LOG_DEBUG("GameData") << "植物: " << info.animName << " (" << info.enumName << ")";
		LOG_DEBUG("GameData") << "  阳光: " << info.SunCost << "  冷却: " << info.Cooldown
			<< "s  缩放: " << info.scale;
		LOG_DEBUG("GameData") << "  纹理键: " << info.textureKey;
		LOG_DEBUG("GameData") << "  动画类型: " << static_cast<int>(info.animType);
		LOG_DEBUG("GameData") << "  偏移量: (" << info.offset.x << ", " << info.offset.y << ")";
	}

	LOG_DEBUG("GameData") << "僵尸总数: " << mZombieInfo.size();
	for (const auto& pair : mZombieInfo) {
		const ZombieInfo& info = pair.second;
		LOG_DEBUG("GameData") << "僵尸: " << info.animName << " (" << info.enumName << ")";
		LOG_DEBUG("GameData") << "  权重: " << info.weight << "  出现波: " << info.appearWave
			<< "  生存轮: " << info.survivalRound << "  缩放: " << info.scale;
		LOG_DEBUG("GameData") << "  动画类型: " << static_cast<int>(info.animType);
		LOG_DEBUG("GameData") << "  偏移量: (" << info.offset.x << ", " << info.offset.y << ")";
	}
	LOG_DEBUG("GameData") << "=============================================";
}
