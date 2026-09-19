#include "TestDriver.h"
#include "../../GameApp.h"
#include "../../GameInfoSaver.h"
#include "../../Renderer/VulkanRenderer.h"
#include "../../Renderer/VulkanContext.h"
#include "../../Renderer/OpenGLRenderer.h"
#include "../../DeltaTime.h"
#include "../../Logger.h"
#include "../../ResourceKeys.h"
#include "../../CursorManager.h"
#include "../../UI/Button.h"
#include "../../UI/GameMessageBox.h"
#include "../SceneManager.h"
#include "../GameScene.h"
#include "../CrazyDaveDialog.h"
#include "../GameObjectManager.h"
#include "../AdventureProgression.h"
#include "../AnimatedObject.h"
#include "../GameSelectScene.h"
#include "../MainMenuScene.h"
#include "../ZombieAlmanacScene.h"
#include "../PlantAlmanacScene.h"
#include "../ChooseCardUI.h"
#include "Game/Board/Board.h"
#include "../Ladder.h"
#include "../IceWall.h"
#include "../GroundRift.h"
#include "../Sun.h"
#include "../LawnMower.h"
#include "../AudioSystem.h"
#include "../Card.h"
#include "../CardSlotManager.h"
#include "../ShadowComponent.h"
#include "../Plant/GameDataManager.h"
#include "../Plant/PlantType.h"
#include "../Plant/PlantUpgradeRules.h"
#include "../Plant/Plant.h"
#include "../Plant/Imitater.h"
#include "../Plant/SunFlower.h"
#include "../Plant/Plantern.h"
#include "../Plant/WallNut.h"
#include "../Plant/SnowAnchorNut.h"
#include "../Plant/LilyPad.h"
#include "../Plant/Squash.h"
#include "../Plant/ThreePeater.h"
#include "../Plant/TangleKelp.h"
#include "../Plant/Caltrop.h"
#include "../Plant/Shooter.h"
#include "../Plant/EliteScaredyShroom.h"
#include "../Plant/ScaredyShroom.h"
#include "../Plant/Cactus.h"
#include "../Plant/Blover.h"
#include "../Plant/SplitPea.h"
#include "../Plant/StarFruit.h"
#include "../Plant/PumpkinShell.h"
#include "../Plant/MagnetShroom.h"
#include "../Plant/FlowerPot.h"
#include "../Plant/CabbagePult.h"
#include "../Plant/MeltSnowPult.h"
#include "../Plant/FrostMine.h"
#include "../Plant/AlarmBellFlower.h"
#include "../Plant/FurnaceCoreFlower.h"
#include "../Plant/ListeningGrass.h"
#include "../Plant/IceMint.h"
#include "../Zombie/IceWorkerZombie.h"
#include "../Plant/NorthStarFlower.h"
#include "../Plant/IceMirrorGrass.h"
#include "../Plant/BoundaryFlower.h"
#include "../Plant/PrismFlower.h"
#include "../Zombie/SunThiefZombie.h"
#include "../Zombie/CrystalDrummerZombie.h"
#include "../Plant/DawnLotus.h"
#include "../Plant/KernelPult.h"
#include "../Plant/MelonPult.h"
#include "../Plant/GloomShroom.h"
#include "../Plant/CoffeeBean.h"
#include "../Plant/Garlic.h"
#include "../Plant/CobCannon.h"
#include "../Plant/PlantFootprint.h"
#include "../Bullet/Bullet.h"
#include "../Zombie/ZombieType.h"
#include "../Zombie/Zombie.h"
#include "../Zombie/ZombieCharred.h"
#include "../Zombie/ZamboniCharred.h"
#include "../Zombie/ZamboniZombie.h"
#include "../Zombie/GildedZamboniZombie.h"
#include "../Zombie/BobsledTeamZombie.h"
#include "../Zombie/IceWallEngineerZombie.h"
#include "../Zombie/IceCrackDrillZombie.h"
#include "../Zombie/WeatherJammerZombie.h"
#include "../Zombie/ExcavatorZombie.h"
#include "../Zombie/IceStatueExecutionerZombie.h"
#include "../Zombie/SnowBurrowZombie.h"
#include "../Zombie/AdaptiveHelmetZombie.h"
#include "../Zombie/ThermalSniperZombie.h"
#include "../Zombie/AuroraPriestZombie.h"
#include "../Zombie/PolarClockmakerZombie.h"
#include "../Zombie/EliteDancerZombie.h"
#include "../Zombie/Polevaulter.h"
#include "../Zombie/DolphinRiderZombie.h"
#include "../Zombie/CrystalHornMinerZombie.h"
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
#include "../Zombie/CatapultCharred.h"
#include "../Zombie/CatapultZombie.h"
#include "../Zombie/EliteCatapultZombie.h"
#include "../Zombie/GargantuarCharred.h"
#include "../Zombie/GargantuarZombie.h"
#include "../Zombie/ImpCharred.h"
#include "../Zombie/ImpZombie.h"
#include "../Zombie/PoolNormalZombie.h"
#include "../Zombie/RoofMarshalZombie.h"
#include "../Zombie/InsulatorZombie.h"
#include "../Zombie/HijackerZombie.h"
#include "../Zombie/HealerZombie.h"
#include "../Trophy.h"   // dump_state 输出奖杯坐标
#include "../Crater.h"   // dump_state 输出毁灭菇弹坑
#include "../../Reanimation/Animator.h"   // dump_state 查询轨道可见性（如铁门僵尸手臂）
#include "../../ResourceManager.h"
#include "../../ParticleSystem/ParticleSystem.h"
#include <filesystem>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <SDL2/SDL.h>

namespace {
	const char* ObjectTypeName(ObjectType type)
	{
		switch (type) {
		case ObjectType::OBJECT_UI: return "UI";
		case ObjectType::OBJECT_PLANT: return "PLANT";
		case ObjectType::OBJECT_ZOMBIE: return "ZOMBIE";
		case ObjectType::OBJECT_BULLET: return "BULLET";
		case ObjectType::OBJECT_COIN: return "COIN";
		case ObjectType::OBJECT_LAWNMOWER: return "LAWNMOWER";
		case ObjectType::OBJECT_PARTICLE: return "PARTICLE";
		default: return "NONE";
		}
	}

	const char* EliteJackTargetingModeName(
		EliteJackInTheBoxZombie::PlantTargetingMode mode)
	{
		using Mode = EliteJackInTheBoxZombie::PlantTargetingMode;
		switch (mode) {
		case Mode::FORCED: return "FORCED";
		case Mode::GREEDY: return "GREEDY";
		case Mode::MONTE_CARLO: return "MONTE_CARLO";
		case Mode::CHARMED_RANDOM: return "CHARMED_RANDOM";
		default: return "NONE";
		}
	}

	const char* DiggerPhaseName(DiggerZombie::Phase phase)
	{
		using Phase = DiggerZombie::Phase;
		switch (phase) {
		case Phase::TUNNELING: return "TUNNELING";
		case Phase::RISING: return "RISING";
		case Phase::STUNNED: return "STUNNED";
		case Phase::WALKING_WITH_PICKAXE: return "WALKING_WITH_PICKAXE";
		case Phase::TUNNELING_PAUSE_WITHOUT_PICKAXE:
			return "TUNNELING_PAUSE_WITHOUT_PICKAXE";
		case Phase::RISING_WITHOUT_PICKAXE: return "RISING_WITHOUT_PICKAXE";
		case Phase::WALKING_WITHOUT_PICKAXE: return "WALKING_WITHOUT_PICKAXE";
		default: return "UNKNOWN";
		}
	}

	const char* GargantuarPhaseName(GargantuarZombie::Phase phase)
	{
		switch (phase) {
		case GargantuarZombie::Phase::WALKING: return "WALKING";
		case GargantuarZombie::Phase::SMASHING: return "SMASHING";
		case GargantuarZombie::Phase::THROWING: return "THROWING";
		default: return "UNKNOWN";
		}
	}

	const char* GargantuarWeaponName(GargantuarZombie::WeaponVariant variant)
	{
		switch (variant) {
		case GargantuarZombie::WeaponVariant::TELEPHONE_POLE: return "TELEPHONE_POLE";
		case GargantuarZombie::WeaponVariant::DUCK_SIGN: return "DUCK_SIGN";
		case GargantuarZombie::WeaponVariant::ZOMBIE: return "ZOMBIE";
		default: return "UNKNOWN";
		}
	}

	const char* ImpPhaseName(ImpZombie::Phase phase)
	{
		switch (phase) {
		case ImpZombie::Phase::WALKING: return "WALKING";
		case ImpZombie::Phase::THROWN: return "THROWN";
		case ImpZombie::Phase::LANDING: return "LANDING";
		default: return "UNKNOWN";
		}
	}

	const char* JackPhaseName(JackInTheBoxZombie::Phase phase)
	{
		switch (phase) {
		case JackInTheBoxZombie::Phase::RUNNING: return "RUNNING";
		case JackInTheBoxZombie::Phase::POPPING: return "POPPING";
		case JackInTheBoxZombie::Phase::DISARMED: return "DISARMED";
		default: return "UNKNOWN";
		}
	}

	const char* PogoPhaseName(PogoZombie::Phase phase)
	{
		switch (phase) {
		case PogoZombie::Phase::BOUNCING: return "BOUNCING";
		case PogoZombie::Phase::HIGH_BOUNCE: return "HIGH_BOUNCE";
		case PogoZombie::Phase::FORWARD_BOUNCE: return "FORWARD_BOUNCE";
		case PogoZombie::Phase::WALKING: return "WALKING";
		default: return "UNKNOWN";
		}
	}

	const char* BungeePhaseName(BungeeZombie::Phase phase)
	{
		switch (phase) {
		case BungeeZombie::Phase::DIVING: return "DIVING";
		case BungeeZombie::Phase::AT_BOTTOM: return "AT_BOTTOM";
		case BungeeZombie::Phase::GRABBING: return "GRABBING";
		case BungeeZombie::Phase::RISING: return "RISING";
		default: return "UNKNOWN";
		}
	}

	const char* BungeeTargetModeName(BungeeZombie::TargetMode mode)
	{
		return mode == BungeeZombie::TargetMode::MONTE_CARLO
			? "MONTE_CARLO" : "RANDOM";
	}

	const char* LadderClimbPhaseName(LadderClimbPhase phase)
	{
		switch (phase) {
		case LadderClimbPhase::NONE: return "NONE";
		case LadderClimbPhase::CLIMBING: return "CLIMBING";
		case LadderClimbPhase::FALLING: return "FALLING";
		default: return "UNKNOWN";
		}
	}

	const char* CursorTypeName(CursorType type)
	{
		switch (type) {
		case CursorType::ARROW: return "ARROW";
		case CursorType::HAND: return "HAND";
		case CursorType::IBEAM: return "IBEAM";
		case CursorType::CROSSHAIR: return "CROSSHAIR";
		case CursorType::WAIT: return "WAIT";
		case CursorType::SIZE_ALL: return "SIZE_ALL";
		case CursorType::NO: return "NO";
		case CursorType::CUSTOM: return "CUSTOM";
		}
		return "UNKNOWN";
	}

	const char* CobCannonPhaseName(CobCannon::Phase phase)
	{
		switch (phase) {
		case CobCannon::Phase::ARMING: return "ARMING";
		case CobCannon::Phase::CHARGING: return "CHARGING";
		case CobCannon::Phase::READY: return "READY";
		case CobCannon::Phase::FIRING: return "FIRING";
		default: return "UNKNOWN";
		}
	}

	const char* HealerTreatmentStateName(HealerZombie::TreatmentState state)
	{
		switch (state) {
		case HealerZombie::TreatmentState::IDLE: return "IDLE";
		case HealerZombie::TreatmentState::AREA: return "AREA";
		case HealerZombie::TreatmentState::FOCUSED: return "FOCUSED";
		case HealerZombie::TreatmentState::DISABLED: return "DISABLED";
		default: return "UNKNOWN";
		}
	}

	const char* HealerDecisionModeName(HealerZombie::DecisionMode mode)
	{
		return mode == HealerZombie::DecisionMode::MONTE_CARLO
			? "MONTE_CARLO" : "DETERMINISTIC";
	}

	const char* HealerDecisionActionName(HealerZombie::DecisionAction action)
	{
		switch (action) {
		case HealerZombie::DecisionAction::NONE: return "NONE";
		case HealerZombie::DecisionAction::AREA: return "AREA";
		case HealerZombie::DecisionAction::FOCUSED: return "FOCUSED";
		case HealerZombie::DecisionAction::WAIT: return "WAIT";
		default: return "UNKNOWN";
		}
	}

	const char* ZombieHelmTypeName(HelmType type)
	{
		switch (type) {
		case HelmType::HELMTYPE_NONE: return "HELMTYPE_NONE";
		case HelmType::HELMTYPE_TRAFFIC_CONE: return "HELMTYPE_TRAFFIC_CONE";
		case HelmType::HELMTYPE_BUCKET: return "HELMTYPE_BUCKET";
		case HelmType::HELMTYPE_FOOTBALL: return "HELMTYPE_FOOTBALL";
		case HelmType::HELMTYPE_DIGGER: return "HELMTYPE_DIGGER";
		case HelmType::HELMTYPE_BOBSLED: return "HELMTYPE_BOBSLED";
		case HelmType::HELMTYPE_WALLNUT: return "HELMTYPE_WALLNUT";
		case HelmType::HELMTYPE_TALLNUT: return "HELMTYPE_TALLNUT";
		case HelmType::HELMTYPE_INSULATOR: return "HELMTYPE_INSULATOR";
		case HelmType::HELMTYPE_CRYSTAL_HORN: return "HELMTYPE_CRYSTAL_HORN";
		case HelmType::HELMTYPE_ADAPTIVE: return "HELMTYPE_ADAPTIVE";
		case HelmType::HELMTYPE_AURORA_DEVICE: return "HELMTYPE_AURORA_DEVICE";
		case HelmType::HELMTYPE_CLOCK_DISK: return "HELMTYPE_CLOCK_DISK";
		default: return "UNKNOWN";
		}
	}

	const char* CatapultPhaseName(CatapultZombie::Phase phase)
	{
		switch (phase) {
		case CatapultZombie::Phase::WALKING: return "WALKING";
		case CatapultZombie::Phase::SHOOTING: return "SHOOTING";
		case CatapultZombie::Phase::RELOADING: return "RELOADING";
		case CatapultZombie::Phase::CALTROP_DYING: return "CALTROP_DYING";
		default: return "UNKNOWN";
		}
	}

	const char* PlantBungeeStateName(PlantBungeeState state)
	{
		switch (state) {
		case PlantBungeeState::NONE: return "NONE";
		case PlantBungeeState::GRABBING: return "GRABBING";
		case PlantBungeeState::RISING: return "RISING";
		default: return "UNKNOWN";
		}
	}

	const char* AirborneDefenseStateName(AirborneDefenseState state)
	{
		switch (state) {
		case AirborneDefenseState::INACTIVE: return "INACTIVE";
		case AirborneDefenseState::ACTIVATING: return "ACTIVATING";
		case AirborneDefenseState::REFLECTING: return "REFLECTING";
		default: return "UNKNOWN";
		}
	}

	bool BoundsIntersect(const SDL_FRect& a, const SDL_FRect& b)
	{
		return a.x <= b.x + b.w && a.x + a.w >= b.x
			&& a.y <= b.y + b.h && a.y + a.h >= b.y;
	}

#define PT(n) { #n, PlantType::n }
	const std::unordered_map<std::string, PlantType> kPlantNames = {
		PT(PLANT_PEASHOOTER), PT(PLANT_SUNFLOWER), PT(PLANT_CHERRYBOMB), PT(PLANT_WALLNUT),
		PT(PLANT_POTATOMINE), PT(PLANT_SNOWPEA), PT(PLANT_CHOMPER), PT(PLANT_REPEATER),
		PT(PLANT_PUFFSHROOM), PT(PLANT_SUNSHROOM), PT(PLANT_FUMESHROOM),
		PT(PLANT_HYPNOSHROOM), PT(PLANT_SCAREDYSHROOM), PT(PLANT_ICESHROOM), PT(PLANT_ICEFUMESHROOM),
		PT(PLANT_DOOMSHROOM),
		PT(PLANT_LILYPAD), PT(PLANT_SQUASH), PT(PLANT_THREEPEATER), PT(PLANT_TANGLEKELP),
		PT(PLANT_JALAPENO), PT(PLANT_SPIKEWEED), PT(PLANT_TORCHWOOD), PT(PLANT_TALLNUT),
		PT(PLANT_SEASHROOM), PT(PLANT_PLANTERN), PT(PLANT_CACTUS), PT(PLANT_BLOVER),
		PT(PLANT_SPLITPEA), PT(PLANT_STARFRUIT), PT(PLANT_PUMPKINSHELL), PT(PLANT_MAGNETSHROOM),
		PT(PLANT_CABBAGEPULT), PT(PLANT_FLOWERPOT), PT(PLANT_KERNELPULT), PT(PLANT_INSTANT_COFFEE),
		PT(PLANT_GARLIC), PT(PLANT_UMBRELLA), PT(PLANT_MARIGOLD), PT(PLANT_MELONPULT),
		PT(PLANT_GATLINGPEA), PT(PLANT_TWINSUNFLOWER), PT(PLANT_GLOOMSHROOM), PT(PLANT_CATTAIL),
		PT(PLANT_WINTERMELON), PT(PLANT_GOLD_MAGNET), PT(PLANT_SPIKEROCK), PT(PLANT_COBCANNON),
		PT(PLANT_IMITATER), PT(PLANT_EXPLODE_O_NUT), PT(PLANT_GIANT_WALLNUT), PT(PLANT_SPROUT),
		PT(PLANT_LEFTPEATER), PT(PLANT_ELITE_SCAREDYSHROOM), PT(PLANT_TOXICPEASHOOTER),
		PT(PLANT_GROUNDINGSHROOM), PT(PLANT_LIGHTNINGRODPOT),
		PT(PLANT_SNOWANCHORNUT),
		PT(PLANT_MELTSNOWPULT),
		PT(PLANT_FROSTMINE),
		PT(PLANT_ALARMBELLFLOWER),
		PT(PLANT_FURNACECOREFLOWER),
		PT(PLANT_LISTENINGGRASS),
		PT(PLANT_AURORATORCHWOOD),
		PT(PLANT_NORTHSTARFLOWER), PT(PLANT_ICEMIRRORGRASS),
		PT(PLANT_BOUNDARYFLOWER), PT(PLANT_DAWNLOTUS), PT(PLANT_CARRYVINE), PT(PLANT_ECHOSHROOM), PT(PLANT_PRISMFLOWER), PT(PLANT_AMBERLICHEN), PT(PLANT_ICEMINT),
	};
#undef PT
#define BT(n) { #n, BulletType::n }
	const std::unordered_map<std::string, BulletType> kBulletNames = {
		BT(BULLET_PEA), BT(BULLET_SNOWPEA), BT(BULLET_CABBAGE), BT(BULLET_MELON), BT(BULLET_PUFF), BT(BULLET_WINTERMELON), BT(BULLET_FIREBALL),
		BT(BULLET_SPIKE), BT(BULLET_STAR), BT(BULLET_BASKETBALL), BT(BULLET_KERNEL), BT(BULLET_BUTTER),
		BT(BULLET_TOXICPEA), BT(BULLET_TOXICFIREBALL),
		BT(BULLET_MELT_SNOW), BT(BULLET_SALT_CRYSTAL),
		BT(BULLET_AURORA_PEA),
		BT(BULLET_THERMAL_PULSE),
	};
#undef BT
#define ZT(n) { #n, ZombieType::n }
	const std::unordered_map<std::string, ZombieType> kZombieNames = {
		ZT(ZOMBIE_NORMAL), ZT(ZOMBIE_TRAFFIC_CONE), ZT(ZOMBIE_POLEVAULTER), ZT(ZOMBIE_ELITE_POLEVAULTER), ZT(ZOMBIE_BUCKET),
		ZT(ZOMBIE_FASTBUCKET), ZT(ZOMBIE_NEWSPAPER), ZT(ZOMBIE_FASTPAPER), ZT(ZOMBIE_DOOR),
		ZT(ZOMBIE_FOOTBALL), ZT(ZOMBIE_DANCER), ZT(ZOMBIE_BACKUP_DANCER), ZT(ZOMBIE_ELITE_DANCER), ZT(ZOMBIE_PINK_FOOTBALL),
		ZT(ZOMBIE_REINFORCED_DOOR),
		ZT(ZOMBIE_POOL_NORMAL), ZT(ZOMBIE_POOL_CONE), ZT(ZOMBIE_POOL_BUCKET),
		ZT(ZOMBIE_ZAMBONI), ZT(ZOMBIE_GILDED_ZAMBONI), ZT(ZOMBIE_DOLPHIN_RIDER), ZT(ZOMBIE_ELITE_DOLPHIN_RIDER),
		ZT(ZOMBIE_JACK_IN_THE_BOX), ZT(ZOMBIE_ELITE_JACK_IN_THE_BOX), ZT(ZOMBIE_BALLOON),
		ZT(ZOMBIE_DIGGER), ZT(ZOMBIE_ELITE_DIGGER), ZT(ZOMBIE_POGO), ZT(ZOMBIE_ELITE_POGO),
		ZT(ZOMBIE_BUNGEE), ZT(ZOMBIE_LADDER), ZT(ZOMBIE_ELITE_LADDER), ZT(ZOMBIE_CATAPULT), ZT(ZOMBIE_ELITE_CATAPULT),
		ZT(ZOMBIE_YETI),
		ZT(ZOMBIE_GARGANTUAR), ZT(ZOMBIE_IMP), ZT(ZOMBIE_BOSS), ZT(ZOMBIE_PEA_HEAD),
		ZT(ZOMBIE_WALLNUT_HEAD), ZT(ZOMBIE_JALAPENO_HEAD), ZT(ZOMBIE_GATLING_HEAD),
		ZT(ZOMBIE_SQUASH_HEAD), ZT(ZOMBIE_TALLNUT_HEAD), ZT(ZOMBIE_REDEYE_GARGANTUAR), ZT(ZOMBIE_ROOF_MARSHAL),
		ZT(ZOMBIE_INSULATOR), ZT(ZOMBIE_HIJACKER), ZT(ZOMBIE_HEALER),
		ZT(ZOMBIE_GROUNDING), ZT(ZOMBIE_BOBSLED_TEAM), ZT(ZOMBIE_ICE_WALL_ENGINEER),
		ZT(ZOMBIE_ICE_CRACK_DRILL), ZT(ZOMBIE_WEATHER_JAMMER),
		ZT(ZOMBIE_ICE_STATUE_EXECUTIONER),
		ZT(ZOMBIE_SNOW_BURROW),
		ZT(ZOMBIE_ADAPTIVE_HELMET),
		ZT(ZOMBIE_THERMAL_SNIPER),
		ZT(ZOMBIE_AURORA_PRIEST), ZT(ZOMBIE_POLAR_CLOCKMAKER),
		ZT(ZOMBIE_EXCAVATOR), ZT(ZOMBIE_CRYSTAL_HORN_MINER), ZT(ZOMBIE_SUN_THIEF), ZT(ZOMBIE_CRYSTAL_DRUMMER), ZT(ZOMBIE_ICE_WORKER),
	};
#undef ZT
#define PK(n) { #n, PerkType::n }
	const std::unordered_map<std::string, PerkType> kPerkNames = {
		PK(PLANT_DAMAGE_UP), PK(ZOMBIE_HEALTH_UP), PK(ZOMBIE_DAMAGE_RESIST),
		PK(ZOMBIE_DAMAGE_UP), PK(ZOMBIE_INVULN_HITS), PK(PLANT_REGEN),
		PK(PLANT_ATTACK_SPEED), PK(PLANT_DAMAGE_REDUCTION), PK(PLANT_SUN_BONUS),
		PK(PLANT_CARD_RECHARGE), PK(PLANT_MIST_REFINING),
		PK(PLANT_CORROSIVE_TOXIN), PK(PLANT_UNYIELDING_ROOTS),
		PK(PLANT_DAMAGE_ECHO), PK(ZOMBIE_FOG_BREAKOUT), PK(ZOMBIE_DEATH_RELAY),
		PK(ZOMBIE_DEVOUR_REPAIR), PK(ZOMBIE_ARMOR_BREAK_RUSH),
	};
#undef PK
	const std::unordered_map<std::string, BoardState> kBoardStateNames = {
		{ "CHOOSE_CARD", BoardState::CHOOSE_CARD }, { "GAME", BoardState::GAME },
		{ "LOSE_GAME", BoardState::LOSE_GAME }, { "WIN", BoardState::WIN },
		{ "NONE", BoardState::NONE },
	};
	const std::unordered_map<std::string, DamageSource> kDamageSourceNames = {
		{ "PLANT", DamageSource::PLANT }, { "ZOMBIE", DamageSource::ZOMBIE },
		{ "OTHER", DamageSource::OTHER },
	};
	const std::unordered_map<std::string, RainIntensity> kRainIntensityNames = {
		{ "CLEAR", RainIntensity::CLEAR }, { "LIGHT", RainIntensity::LIGHT },
		{ "MEDIUM", RainIntensity::MEDIUM }, { "HEAVY", RainIntensity::HEAVY },
	};
	const std::unordered_map<std::string, ColdWavePhase> kColdWavePhaseNames = {
		{ "CALM", ColdWavePhase::CALM }, { "COOLING", ColdWavePhase::COOLING },
		{ "COLD", ColdWavePhase::COLD }, { "THAWING", ColdWavePhase::THAWING },
	};
	const std::unordered_map<std::string, ColdWaveStrength> kColdWaveStrengthNames = {
		{ "WEAK", ColdWaveStrength::WEAK }, { "NORMAL", ColdWaveStrength::NORMAL },
		{ "STRONG", ColdWaveStrength::STRONG },
	};
	const std::unordered_map<std::string, FogWeatherIntensity> kFogWeatherIntensityNames = {
		{ "DEFAULT", FogWeatherIntensity::DEFAULT },
		{ "SMALL", FogWeatherIntensity::SMALL },
		{ "NORMAL", FogWeatherIntensity::NORMAL },
		{ "DENSE", FogWeatherIntensity::DENSE },
	};
	const std::unordered_map<std::string, PlanternGear> kPlanternGearNames = {
		{ "OFF", PlanternGear::OFF }, { "0", PlanternGear::OFF },
		{ "LOW", PlanternGear::LOW }, { "I", PlanternGear::LOW },
		{ "MEDIUM", PlanternGear::MEDIUM }, { "II", PlanternGear::MEDIUM },
		{ "HIGH", PlanternGear::HIGH }, { "III", PlanternGear::HIGH },
	};
	const std::unordered_map<std::string, TyphoonStrength> kTyphoonStrengthNames = {
		{ "NONE", TyphoonStrength::NONE }, { "TYPHOON", TyphoonStrength::TYPHOON },
		{ "SEVERE", TyphoonStrength::SEVERE }, { "SUPER", TyphoonStrength::SUPER },
	};
	const std::unordered_map<std::string, WindDirection> kWindDirectionNames = {
		{ "NONE", WindDirection::NONE }, { "HOUSE", WindDirection::TOWARD_HOUSE },
		{ "FRONT", WindDirection::TOWARD_FRONT },
	};
	const std::unordered_map<std::string, RoofRunoffPhase> kRoofRunoffPhaseNames = {
		{ "IDLE", RoofRunoffPhase::IDLE },
		{ "WARNING", RoofRunoffPhase::WARNING },
		{ "FLOWING", RoofRunoffPhase::FLOWING },
	};
	const std::unordered_map<std::string, NightRoofChargePhase> kNightRoofChargePhaseNames = {
		{ "CHARGING", NightRoofChargePhase::CHARGING },
		{ "WARNING", NightRoofChargePhase::WARNING },
		{ "DISCHARGING", NightRoofChargePhase::DISCHARGING },
	};
	const std::unordered_map<std::string, PolarNightPhase> kPolarNightPhaseNames = {
		{ "DORMANT", PolarNightPhase::DORMANT },
		{ "BUILDUP", PolarNightPhase::BUILDUP },
		{ "DANGER_HOLD", PolarNightPhase::DANGER_HOLD },
		{ "RECOVERY", PolarNightPhase::RECOVERY },
		{ "WHITEOUT_RAMP", PolarNightPhase::WHITEOUT_RAMP },
		{ "SNOW_BLIND", PolarNightPhase::SNOW_BLIND },
		{ "FADE", PolarNightPhase::FADE },
	};
	const std::unordered_map<std::string, VerticalWindDirection>
		kVerticalWindDirectionNames = {
			{ "NONE", VerticalWindDirection::NONE },
			{ "UP", VerticalWindDirection::UP },
			{ "DOWN", VerticalWindDirection::DOWN },
		};

	const std::unordered_map<std::string, Uint8> kMouseButtonNames = {
		{ "left", SDL_BUTTON_LEFT }, { "right", SDL_BUTTON_RIGHT },
		{ "middle", SDL_BUTTON_MIDDLE },
	};

	// 按键名 → SDL_Keycode。初始覆盖常用集，按需加行（与植物/僵尸名表同惯例）。
	const std::unordered_map<std::string, SDL_Keycode> kKeyNames = {
		{ "a", SDLK_a }, { "b", SDLK_b }, { "c", SDLK_c }, { "d", SDLK_d },
		{ "e", SDLK_e }, { "f", SDLK_f }, { "g", SDLK_g }, { "h", SDLK_h },
		{ "i", SDLK_i }, { "j", SDLK_j }, { "k", SDLK_k }, { "l", SDLK_l },
		{ "m", SDLK_m }, { "n", SDLK_n }, { "o", SDLK_o }, { "p", SDLK_p },
		{ "q", SDLK_q }, { "r", SDLK_r }, { "s", SDLK_s }, { "t", SDLK_t },
		{ "u", SDLK_u }, { "v", SDLK_v }, { "w", SDLK_w }, { "x", SDLK_x },
		{ "y", SDLK_y }, { "z", SDLK_z },
		{ "0", SDLK_0 }, { "1", SDLK_1 }, { "2", SDLK_2 }, { "3", SDLK_3 },
		{ "4", SDLK_4 }, { "5", SDLK_5 }, { "6", SDLK_6 }, { "7", SDLK_7 },
		{ "8", SDLK_8 }, { "9", SDLK_9 },
		{ "space", SDLK_SPACE }, { "enter", SDLK_RETURN }, { "return", SDLK_RETURN },
		{ "escape", SDLK_ESCAPE }, { "esc", SDLK_ESCAPE }, { "tab", SDLK_TAB },
		{ "backspace", SDLK_BACKSPACE }, { "rshift", SDLK_RSHIFT },
		// 方向键：这里的 "left"/"right" 是键盘方向键，与 kMouseButtonNames 的 "left"/"right"
		// 是各自独立的表（key 命令查此表，click 命令查鼠标表），不冲突。
		{ "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT }, { "right", SDLK_RIGHT },
		{ "f1", SDLK_F1 }, { "f2", SDLK_F2 }, { "f3", SDLK_F3 }, { "f4", SDLK_F4 },
		{ "f5", SDLK_F5 }, { "f6", SDLK_F6 }, { "f7", SDLK_F7 }, { "f8", SDLK_F8 },
		{ "f9", SDLK_F9 }, { "f10", SDLK_F10 }, { "f11", SDLK_F11 }, { "f12", SDLK_F12 },
	};

	std::string PlantTypeName(PlantType t) {
		for (const auto& [k, v] : kPlantNames) if (v == t) return k;
		return "UNKNOWN_PLANT_" + std::to_string(static_cast<int>(t));
	}
	std::string ZombieTypeName(ZombieType t) {
		for (const auto& [k, v] : kZombieNames) if (v == t) return k;
		return "UNKNOWN_ZOMBIE_" + std::to_string(static_cast<int>(t));
	}
	std::string BulletTypeName(BulletType t) {
		for (const auto& [k, v] : kBulletNames) if (v == t) return k;
		return "UNKNOWN_BULLET_" + std::to_string(static_cast<int>(t));
	}
	std::string BoardStateName(BoardState s) {
		for (const auto& [k, v] : kBoardStateNames) if (v == s) return k;
		return "UNKNOWN";
	}
	std::string RainIntensityName(RainIntensity intensity) {
		for (const auto& [k, v] : kRainIntensityNames) if (v == intensity) return k;
		return "UNKNOWN";
	}
	std::string ColdWavePhaseName(ColdWavePhase phase) {
		for (const auto& [k, v] : kColdWavePhaseNames) if (v == phase) return k;
		return "UNKNOWN";
	}
	std::string ColdWaveStrengthName(ColdWaveStrength strength) {
		for (const auto& [k, v] : kColdWaveStrengthNames) if (v == strength) return k;
		return "UNKNOWN";
	}
	std::string FogWeatherIntensityName(FogWeatherIntensity intensity) {
		switch (intensity) {
		case FogWeatherIntensity::DEFAULT: return "DEFAULT";
		case FogWeatherIntensity::SMALL:  return "SMALL";
		case FogWeatherIntensity::NORMAL: return "NORMAL";
		case FogWeatherIntensity::DENSE:  return "DENSE";
		}
		return "UNKNOWN";
	}
	std::string PlanternGearName(PlanternGear gear) {
		switch (gear) {
		case PlanternGear::OFF:    return "OFF";
		case PlanternGear::LOW:    return "LOW";
		case PlanternGear::MEDIUM: return "MEDIUM";
		case PlanternGear::HIGH:   return "HIGH";
		}
		return "UNKNOWN";
	}
	std::string TyphoonStrengthName(TyphoonStrength strength) {
		for (const auto& [k, v] : kTyphoonStrengthNames) if (v == strength) return k;
		return "UNKNOWN";
	}
	std::string WindDirectionName(WindDirection direction) {
		for (const auto& [k, v] : kWindDirectionNames) if (v == direction) return k;
		return "UNKNOWN";
	}
	std::string RoofRunoffPhaseName(RoofRunoffPhase phase) {
		for (const auto& [k, v] : kRoofRunoffPhaseNames) if (v == phase) return k;
		return "UNKNOWN";
	}
	std::string NightRoofChargePhaseName(NightRoofChargePhase phase) {
		for (const auto& [k, v] : kNightRoofChargePhaseNames) if (v == phase) return k;
		return "UNKNOWN";
	}
	std::string PolarNightPhaseName(PolarNightPhase phase) {
		for (const auto& [k, v] : kPolarNightPhaseNames) if (v == phase) return k;
		return "UNKNOWN";
	}
	std::string VerticalWindDirectionName(VerticalWindDirection direction) {
		for (const auto& [k, v] : kVerticalWindDirectionNames) {
			if (v == direction) return k;
		}
		return "UNKNOWN";
	}
	std::string SnowHolePhaseName(SnowHolePhase phase) {
		switch (phase) {
		case SnowHolePhase::NONE: return "NONE";
		case SnowHolePhase::FORMING: return "FORMING";
		case SnowHolePhase::ACTIVE: return "ACTIVE";
		}
		return "UNKNOWN";
	}
	std::string PlayStateName(PlayState s) {
		switch (s) {
		case PlayState::PLAY_NONE:    return "PLAY_NONE";
		case PlayState::PLAY_REPEAT:  return "PLAY_REPEAT";
		case PlayState::PLAY_ONCE:    return "PLAY_ONCE";
		case PlayState::PLAY_ONCE_TO: return "PLAY_ONCE_TO";
		}
		return "UNKNOWN";
	}
	std::string BackgroundName(Background background) {
		switch (background) {
		case Background::GROUND_DAY:       return "GROUND_DAY";
		case Background::GROUND_NIGHT:     return "GROUND_NIGHT";
		case Background::WATER_POOL:       return "WATER_POOL";
		case Background::NIGHT_WATER_POOL: return "NIGHT_WATER_POOL";
		case Background::ROOF:             return "ROOF";
		case Background::NIGHT_ROOF:       return "NIGHT_ROOF";
		case Background::WINTER_GARDEN:    return "WINTER_GARDEN";
		case Background::GLOOMCRYSTAL_MINE: return "GLOOMCRYSTAL_MINE";
		case Background::HOT_COLD_STORAGE: return "HOT_COLD_STORAGE";
		case Background::POLAR_NIGHT_SNOWFIELD: return "POLAR_NIGHT_SNOWFIELD";
		}
		return "UNKNOWN";
	}
	bool IsBackgroundName(const std::string& name) {
		return name == "GROUND_DAY"
			|| name == "GROUND_NIGHT"
			|| name == "WATER_POOL"
			|| name == "NIGHT_WATER_POOL"
			|| name == "ROOF"
			|| name == "NIGHT_ROOF"
			|| name == "WINTER_GARDEN"
			|| name == "POLAR_NIGHT_SNOWFIELD" || name == "GLOOMCRYSTAL_MINE" || name == "HOT_COLD_STORAGE";
	}
	const char* BossSlotName(AdventureProgression::BossSlot slot) {
		switch (slot) {
		case AdventureProgression::BossSlot::NONE:         return "NONE";
		case AdventureProgression::BossSlot::ROOF_MARSHAL: return "ROOF_MARSHAL";
		}
		return "UNKNOWN";
	}
	const char* MowerTypeName(MowerType type) {
		switch (type) {
		case MowerType::LAWN:  return "LAWN";
		case MowerType::WATER: return "WATER";
		case MowerType::ROOF:  return "ROOF";
		}
		return "UNKNOWN";
	}
	const char* MowerHeightName(MowerHeight height) {
		switch (height) {
		case MowerHeight::LAND:     return "LAND";
		case MowerHeight::ENTERING: return "ENTERING";
		case MowerHeight::IN_POOL:  return "IN_POOL";
		case MowerHeight::EXITING:  return "EXITING";
		}
		return "UNKNOWN";
	}

	GameScene* CurrentGameScene() {
		return dynamic_cast<GameScene*>(SceneManager::GetInstance().GetCurrentScene());
	}

	// 只接受不含路径语义的短名，确保快照始终落在 TestDriver 构造的 snapshots 目录。
	bool IsSafeSnapshotName(const std::string& name) {
		if (name.empty()) return false;
		return std::all_of(name.begin(), name.end(), [](unsigned char c) {
			return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9') || c == '_' || c == '-';
			});
	}

	// 用 error_code 完成最终落盘校验，避免文件系统异常越过 AutoTest 的 Fail 契约。
	bool IsNonEmptyRegularFile(const std::filesystem::path& path) {
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
		const auto size = std::filesystem::file_size(path, ec);
		return !ec && size > 0;
	}
}

TestDriver& TestDriver::GetInstance() {
	static TestDriver instance;
	return instance;
}

bool TestDriver::LoadScript(const std::string& path) {
	std::ifstream f(path);
	if (!f) {
		LOG_ERROR("AutoTest") << "无法打开脚本: " << path;
		return false;
	}
	nlohmann::json j;
	try {
		f >> j;
	}
	catch (const std::exception& e) {
		LOG_ERROR("AutoTest") << "脚本 JSON 解析失败: " << e.what();
		return false;
	}
	if (!j.contains("commands") || !j["commands"].is_array() || j["commands"].empty()) {
		LOG_ERROR("AutoTest") << "脚本缺少非空 commands 数组";
		return false;
	}
	for (const auto& c : j["commands"]) mCommands.push_back(c);
	mInteractive = j.value("interactive", false);
	mMineLayoutRevision = j.value("mineLayoutRevision",MineGrid::CurrentLayoutRevision);

	mOutDir = (std::filesystem::path("./autotest/out") /
		std::filesystem::path(path).stem()).string();
	std::error_code ec;
	std::filesystem::create_directories(mOutDir, ec);
	if (ec) {
		LOG_ERROR("AutoTest") << "无法创建输出目录 " << mOutDir << ": " << ec.message();
		return false;
	}
	mRunLog.open(mOutDir + "/run.log", std::ios::trunc);
	if (!mRunLog.is_open()) {
		LOG_ERROR("AutoTest") << "无法创建 " << mOutDir << "/run.log（权威日志，缺失即盲跑）";
		return false;
	}

	mActive = true;
	WriteStatus("running");
	Log("script loaded: " + path + " (" + std::to_string(mCommands.size()) + " commands)");
	if (GameAPP::mAutoTestLoadSave)
		Log("level save loading enabled (read-only): ./saves");
	return true;
}

void TestDriver::Log(const std::string& msg) {
	if (mRunLog.is_open()) {
		mRunLog << "[f" << mFrame << "] " << msg << "\n";
		mRunLog.flush();
	}
	LOG_INFO("AutoTest") << msg;   // Release 编译期裁掉，run.log 才是权威记录
}

void TestDriver::WriteStatus(const char* status, const std::string& detail) {
	nlohmann::json value = {
		{ "status", status ? status : "unknown" },
		{ "exitCode", mExitCode },
		{ "frame", mFrame },
		{ "commandIndex", mIndex },
	};
	if (!detail.empty()) value["detail"] = detail;
	if (mInteractiveReady) {
		value["session"] = mSession;
		value["liveDir"] = mLiveDir;
		value["lastRequestId"] = mRequestId;
	}
	std::ofstream output(mOutDir + "/status.json", std::ios::trunc);
	if (output) output << value.dump(2);
}

void TestDriver::Fail(const std::string& reason) {
	const std::string op = (mIndex < mCommands.size())
		? mCommands[mIndex].value("op", "?") : "?";
	Log("FAIL at cmd#" + std::to_string(mIndex) + " (" + op + "): " + reason);
	LOG_ERROR("AutoTest") << "FAIL at cmd#" << mIndex << " (" << op << "): " << reason;
	mExitCode = 1;
	WriteStatus("failed", reason);
	mActive = false;
	GameAPP::GetInstance().SetRunning(false);
}

void TestDriver::Finish() {
	Log("script finished OK");
	mExitCode = 0;
	WriteStatus("passed");
	mActive = false;
	GameAPP::GetInstance().SetRunning(false);
}

void TestDriver::ResetTestState() {
	DeltaTime::SetTimeScale(1.0f);
	GameAPP::GetInstance().Difficulty = 3;
	GameAPP::mDevNoCooldown = false;
	GameAPP::mDevFreePlant = false;
	GameAPP::mDevSpawnPaused = false;
	GameAPP::GetInstance().mEnableMonteCarloAI = true;
	GameAPP::GetInstance().mAdvancedPauseEnabled = false;
	GameAPP::GetInstance().mHxyModeEnabled = false;
	GameAPP::GetInstance().mOpeningTyphoonProtectionEnabled = true;
	GameAPP::GetInstance().mTyphoonWeatherEnabled = true;
	GameAPP::GetInstance().mCrazyDaveTutorialsSeen.clear();
	GameAPP::GetInstance().mColdStorageHabits.fill(0.0f);
}

void TestDriver::Update() {
	if (!mActive) return;
	if (mInteractiveReady && !mInteractiveBusy) {
		PollInteractive();
		if (!mInteractiveBusy) return;
	}
	++mFrame;
	if (mFrame == 1) {
		// 把实际能力路径写入权威 run.log；Release 构建不会保留普通 Logger 信息，
		// 兼容矩阵仍需能证明每次运行确实走了目标分支。
		GameAPP& app = GameAPP::GetInstance();
		Log(std::string("renderer requested=")
			+ pvz::RendererPreferenceName(GameAPP::mRendererPreference)
			+ " selected=" + pvz::RendererBackendName(app.GetSelectedRenderer())
			+ " noInstance=" + (GameAPP::mDisableInstancePath ? "yes" : "no")
			+ " testVulkanInitFailure=" + (GameAPP::mTestForceVulkanInitFailure ? "yes" : "no"));
		Log(std::string("sdl video driver=")
			+ (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown"));
		if (!app.GetVulkanStartupError().empty()) {
			Log("vulkan fallback error=" + app.GetVulkanStartupError());
		}
		if (auto* context = app.GetVulkanContext()) {
			Log("vulkan api=" + std::to_string(VK_VERSION_MAJOR(context->ApiVersion())) + "."
				+ std::to_string(VK_VERSION_MINOR(context->ApiVersion()))
				+ " dynamicRendering=" + context->DynamicRenderingPathName()
				+ " synchronization=" + context->SynchronizationPathName());
		}
		else if (auto* renderer = app.GetOpenGLRenderer()) {
			Log("opengl vendor=" + renderer->Vendor() + " renderer=" + renderer->RendererName()
				+ " version=" + renderer->Version() + " glsl=" + renderer->ShadingLanguageVersion()
				+ " framebuffer=" + std::to_string(renderer->DrawableWidth()) + "x"
				+ std::to_string(renderer->DrawableHeight())
				+ " vsync=" + (renderer->IsVsyncEnabled() ? "on" : "off")
				+ " batchPath=" + renderer->BatchPathName());
#if defined(_WIN32)
			Log(std::string("opengl vulkanLoaderLoaded=")
				+ (GetModuleHandleW(L"vulkan-1.dll") ? "yes" : "no"));
#endif
		}
	}
	mBreakFrame = false;
	int guard = 0;
	while (mActive && !mBreakFrame && mIndex < mCommands.size()) {
		if (!ExecuteCurrent()) break;          // 等待中，下帧重试
		Log("done cmd#" + std::to_string(mIndex) + " (" +
			mCommands[mIndex].value("op", "?") + ")");
		++mIndex;
		mWaitAccum = 0.0f;
		mFramesLeft = -1;
		mTimeoutAccum = 0.0f;
		mInputPhase = -1;
		mCaptureTicket = 0;
		if (++guard > 64) break;               // 单帧推进上限，防脚本自旋
	}
	if (mActive && mIndex >= mCommands.size()) {
		if (mInteractiveReady) CompleteInteractive();
		else if (mInteractive) BeginInteractive();
		else Finish();
	}
}

bool TestDriver::ExecuteCurrent() {
	const auto& cmd = mCommands[mIndex];
	const std::string op = cmd.value("op", "");
	if (mInteractiveReady && op != "screenshot") return ExecuteInteractive(cmd);

	// 等待型命令的超时看门狗（墙钟语义，不受 timescale 影响）
	mTimeoutAccum += DeltaTime::GetUnscaledDeltaTime();
	const float timeout = cmd.value("timeout", 15.0f);
	if (mTimeoutAccum > timeout) {
		Fail("timeout (" + std::to_string(timeout) + "s)");
		return false;
	}

	if (op == "wait_seconds") {
		mWaitAccum += DeltaTime::GetDeltaTime();
		return mWaitAccum >= cmd.value("value", 0.0f);
	}
	if (op == "wait_value") {
		// 只等待正式状态到达断言边沿，不推进动画、不直接触发能力。
		nlohmann::json state;
		if (!BuildStateJson("wait_value",state)) return false;
		std::string path = "/" + cmd.value("path","");
		std::replace(path.begin(),path.end(),'.','/');
		const nlohmann::json::json_pointer pointer(path);
		return state.contains(pointer) && state.at(pointer) == cmd.at("equals");
	}
	if (op == "wait_frames") {
		if (mFramesLeft < 0) mFramesLeft = cmd.value("value", 0);
		if (mFramesLeft == 0) return true;   // value=0 或已数完：立即完成
		--mFramesLeft;
		return mFramesLeft == 0;
	}
	if (op == "goto_level") {
		if (!cmd.contains("level")) { Fail("goto_level 缺 level 字段"); return false; }
		const int mineRevision = cmd.value("mineLayoutRevision",mMineLayoutRevision);
		if (mineRevision < 0 || mineRevision > MineGrid::CurrentLayoutRevision) { Fail("mineLayoutRevision 超出支持范围"); return false; }
		if (cmd.value("resetTestState", false)) ResetTestState();
		auto& sm = SceneManager::GetInstance();
		const std::string backgroundName = cmd.value("background", "");
		if (!backgroundName.empty() && !IsBackgroundName(backgroundName)) {
			Fail("goto_level.background 必须为合法背景枚举名");
			return false;
		}
		sm.SetGlobalData("AutoTestBackground", backgroundName);
		sm.SetGlobalData("EnterLevel", std::to_string(cmd["level"].get<int>()));
		if (!sm.SwitchTo("GameScene")) { Fail("SwitchTo(GameScene) 失败"); return false; }
		if (auto* gs = CurrentGameScene(); gs && gs->GetBoard() && gs->GetBoard()->IsMineBackground()
			&& mineRevision != gs->GetBoard()->mMineGrid.layoutRevision) {
			auto* board = gs->GetBoard();
			board->mMineGrid.Initialize(board->mMineGrid.layoutGroup,mineRevision);
			board->mMinePlannedWave = -1;
			board->PrepareMineWave();
		}
		return true;
	}
	if (op == "goto_zombie_almanac") {
		if (!SceneManager::GetInstance().SwitchTo("ZombieAlmanacScene")) {
			Fail("SwitchTo(ZombieAlmanacScene) 失败");
			return false;
		}
		return true;
	}
	if (op == "set_timescale") {
		DeltaTime::SetTimeScale(cmd.value("value", 1.0f));
		return true;
	}
	if (op == "set_difficulty") {
		const int difficulty = cmd.value("value", 3);
		if (difficulty < 1 || difficulty > 4) {
			Fail("set_difficulty: value 必须在 1～4");
			return false;
		}
		GameAPP::GetInstance().Difficulty = difficulty;
		return true;
	}
	if (op == "set_monte_carlo_ai") {
		GameAPP::GetInstance().mEnableMonteCarloAI =
			cmd.value("value", true);
		return true;
	}
	if (op == "set_advanced_pause") {
		GameAPP::GetInstance().mAdvancedPauseEnabled =
			cmd.value("value", false);
		return true;
	}
	if (op == "set_opening_typhoon_protection") {
		GameAPP::GetInstance().mOpeningTyphoonProtectionEnabled =
			cmd.value("value", true);
		return true;
	}
	if (op == "set_typhoon_weather_enabled") {
		GameAPP::GetInstance().mTyphoonWeatherEnabled = cmd.value("value", true);
		return true;
	}

	if (op == "set_last_selected_cards") {
		auto& rememberedCards = GameAPP::GetInstance().mLastSelectedCards;
		rememberedCards.clear();
		const auto& cards = cmd.value("cards", nlohmann::json::array());
		for (const auto& value : cards) {
			if (!value.is_string()) {
				Fail("set_last_selected_cards: cards 只能包含字符串");
				return false;
			}
			rememberedCards.push_back(value.get<std::string>());
		}
		return true;
	}
	if (op == "set_all_owned_cards") {
		// 只在 AutoTest 内存中按正式冒险奖励顺序布置完整卡池，不读写真实 PlayerInfo。
		auto& ownedCards = GameAPP::GetInstance().mHaveCards;
		ownedCards.clear();
		ownedCards.push_back(PlantType::PLANT_PEASHOOTER);
		auto& gameData = GameDataManager::GetInstance();
		for (PlantType reward : AdventureProgression::PLANT_REWARD_BY_LEVEL) {
			if (reward == AdventureProgression::NO_PLANT_REWARD
				|| !gameData.HasPlant(reward)
				|| std::find(ownedCards.begin(), ownedCards.end(), reward)
					!= ownedCards.end()) {
				continue;
			}
			ownedCards.push_back(reward);
		}
		// 可截取正式奖励前缀，验证早期卡池的单页边界而不伪造植物。
		const int maxCards = cmd.value("maxCards", static_cast<int>(ownedCards.size()));
		if (maxCards < 1) { Fail("set_all_owned_cards: maxCards 必须为正数"); return false; }
		if (maxCards < static_cast<int>(ownedCards.size())) ownedCards.resize(maxCards);
		return true;
	}
	if (op == "reset_test_state") {
		ResetTestState();
		return true;
	}
	if (op == "set_vsync") {
		if (!GameAPP::GetInstance().ApplyVsync(cmd.value("value", false))) {
			Fail("set_vsync: 后端拒绝 VSync 切换");
			return false;
		}
		return true;
	}
	if (op == "set_fullscreen") {
		if (!GameAPP::GetInstance().SetFullscreen(cmd.value("value", false))) {
			Fail("set_fullscreen: SDL/后端切换失败");
			return false;
		}
		return true;
	}
	if (op == "set_cold_storage" || op == "buy_ice" || op == "queue_ice_zombie" || op == "plan_ice_attack" || op == "player_plant") {
		GameScene* gs = CurrentGameScene();
		Board* board = gs ? gs->GetBoard() : nullptr;
		if (!board || !board->IsColdStorage()) { Fail("cold storage command: unsupported board"); return false; }
		if (op == "set_cold_storage") {
			auto state = board->SaveColdStorage();
			state.update(cmd.value("state", nlohmann::json::object()));
			board->LoadColdStorage(state);
			return true;
		}
		if (op == "plan_ice_attack") { board->PlanColdStorageAttack(); return true; }
		if (op == "player_plant") {
			const std::string result = gs->GetCardSlotManager()->TryPlantFromSlot(cmd.value("slot",0),cmd.value("row",0),cmd.value("col",0));
			if (result != cmd.value("expectedResult", std::string())) { Fail("player_plant: " + result); return false; }
			return true;
		}
		bool success = false;
		if (op == "buy_ice") success = board->BuyColdStorageIce(cmd.value("large",false));
		else {
			auto type = kZombieNames.find(cmd.value("type", ""));
			if (type == kZombieNames.end()) { Fail("queue_ice_zombie: invalid type"); return false; }
			success = board->QueueColdStorageZombie(type->second,cmd.value("row",0),cmd.value("delay",2.0f));
		}
		if (success != cmd.value("expectedSuccess",true)) { Fail("cold storage result mismatch"); return false; }
		return true;
	}
	if (op == "set_spawn_paused") {
		// 只暂停 Board 的自然出波；spawn_zombie / summon_next_wave 等显式测试命令不受影响。
		GameAPP::mDevSpawnPaused = cmd.value("value", true);
		return true;
	}
	if (op == "set_mine_fog") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard() || !gs->GetBoard()->SupportsMineFog()) { Fail("set_mine_fog: unsupported board"); return false; }
		Board* board = gs->GetBoard();
		board->mMineFogElapsed = std::clamp(cmd.value("elapsed",5.0f),-1.0f,Board::kMineFogDuration);
		board->mMineFogNextWave = cmd.value("nextWave",10);
		return true;
	}
	if (op == "choose_cards") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->IsChooseCardReady()) return false;   // 等开场动画铺完卡
		ChooseCardUI* ui = gs->GetChooseCardUI();
		for (const auto& nameJson : cmd.value("cards", nlohmann::json::array())) {
			const std::string name = nameJson.get<std::string>();
			auto it = kPlantNames.find(name);
			if (it == kPlantNames.end()) { Fail("未知植物类型: " + name); return false; }
			Card* card = ui->FindCardByType(it->second);
			if (!card) {                       // 玩家未拥有该卡：AutoTest 直接补一张
				ui->AddCard(it->second);
				card = ui->FindCardByType(it->second);
			}
			if (!card) { Fail("选卡失败（AddCard 后仍找不到）: " + name); return false; }
			if (!ui->IsCardSelected(card) && !ui->ToggleCardSelection(card)) {
				Fail("选卡失败（超出选卡上限？）: " + name);
				return false;
			}
		}
		gs->ChooseCardComplete();
		return true;
	}
	if (op == "wait_state") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) return false;
		auto it = kBoardStateNames.find(cmd.value("state", ""));
		if (it == kBoardStateNames.end()) { Fail("未知 BoardState: " + cmd.value("state", "")); return false; }
		return gs->GetBoard()->mBoardState == it->second;
	}
	if (op == "complete_mine_dig") {
		GameScene* gs = CurrentGameScene();
		Board* board = gs ? gs->GetBoard() : nullptr;
		if (!board || !board->IsMineBackground()) { Fail("complete_mine_dig: no mine board"); return false; }
		const int row = cmd.value("row", -1), col = cmd.value("col", -1);
		// 候选墙对照夹具：沿正式提交入口开一块可挖墙，不推进八秒而扰动同一战斗快照。
		const bool result = board->mMineGrid.CanExcavate(row, col)
			&& board->CompleteMineExcavation(MineGrid::Index(row, col), false);
		if (result != cmd.value("expectedSuccess", true)) { Fail("complete_mine_dig: unexpected result"); return false; }
		return true;
	}
	if (op == "mine_dig" || op == "mine_cancel") {
		GameScene* gs = CurrentGameScene();
		Board* board = gs ? gs->GetBoard() : nullptr;
		if (!board) { Fail("No board"); return false; }
		const bool result = op == "mine_cancel" ? board->CancelMineExcavation()
			: board->BeginMineExcavation(cmd.value("row", -1), cmd.value("col", -1));
		if (result != cmd.value("expectedSuccess", true)) { Fail("Unexpected mine excavation result"); return false; }
		return true;
	}
	if (op == "set_sun") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("set_sun: 不在 GameScene 或 Board 为空"); return false; }
		gs->GetBoard()->mSun = std::min(cmd.value("value", 0), MAX_SUN);
		return true;
	}
	if (op == "set_weather") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("set_weather: 不在 GameScene 或 Board 为空"); return false; }
		auto it = kRainIntensityNames.find(cmd.value("intensity", ""));
		if (it == kRainIntensityNames.end()) {
			Fail("set_weather: intensity 必须是 CLEAR/LIGHT/MEDIUM/HEAVY");
			return false;
		}
		gs->GetBoard()->SetRainForTesting(it->second, cmd.value("duration", 30.0f),
			cmd.value("canIntensify", false));
		if (gs->GetBoard()->GetRainIntensity() != it->second) {
			Fail("set_weather: 当前关卡不支持天气，天气未生效");
			return false;
		}
		return true;
	}
	if (op == "set_cold_wave") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_cold_wave: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto phaseIt = kColdWavePhaseNames.find(cmd.value("phase", ""));
		auto strengthIt = kColdWaveStrengthNames.find(cmd.value("strength", "STRONG"));
		if (phaseIt == kColdWavePhaseNames.end()
			|| strengthIt == kColdWaveStrengthNames.end()
			|| !gs->GetBoard()->SetWinterTemperatureForTesting(
				cmd.value("temperature", 6.0f), phaseIt->second,
				cmd.value("remaining", 30.0f), strengthIt->second,
				cmd.value("targetTemperature", -12.0f),
				cmd.value("coolingDuration", 20.0f),
				cmd.value("holdDuration", 57.5f),
				cmd.value("thawDuration", 32.0f),
				cmd.value("frostVariant", 0))) {
			Fail("set_cold_wave: 仅冬日花园可用，phase/strength 或寒潮计划参数无效");
			return false;
		}
		return true;
	}
	if (op == "disrupt_cold_wave_forecast") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("disrupt_cold_wave_forecast: 不在 GameScene 或 Board 为空");
			return false;
		}
		const bool disrupted = gs->GetBoard()->DisruptColdWaveForecast();
		if (disrupted != cmd.value("expected", true)) {
			Fail("disrupt_cold_wave_forecast: 正式干扰结果与 expected 不符");
			return false;
		}
		return true;
	}
	if (op == "set_fog_weather") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_fog_weather: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto it = kFogWeatherIntensityNames.find(cmd.value("intensity", ""));
		if (it == kFogWeatherIntensityNames.end()
			|| !gs->GetBoard()->SetFogWeatherForTesting(
				it->second, cmd.value("duration", 30.0f))) {
			Fail("set_fog_weather: intensity 必须是 DEFAULT/SMALL/NORMAL/DENSE，且当前必须是迷雾关卡");
			return false;
		}
		return true;
	}
	if (op == "set_fog_forecast") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_fog_forecast: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto forecastIt = kFogWeatherIntensityNames.find(cmd.value("forecast", ""));
		auto actualIt = kFogWeatherIntensityNames.find(cmd.value("actual", ""));
		if (forecastIt == kFogWeatherIntensityNames.end()
			|| actualIt == kFogWeatherIntensityNames.end()
			|| !gs->GetBoard()->SetFogWeatherForecastForTesting(
				forecastIt->second, actualIt->second, cmd.value("revealIn", 1.0f))) {
			Fail("set_fog_forecast: forecast/actual 必须是 DEFAULT/SMALL/NORMAL/DENSE，且当前必须是已初始化的迷雾关卡");
			return false;
		}
		return true;
	}
	if (op == "set_fog_dispersal") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()
			|| !gs->GetBoard()->SetFogDispersalForTesting(cmd.value("value", -1.0f))) {
			Fail("set_fog_dispersal: value 必须是有限数值，且当前必须是已初始化的迷雾关卡");
			return false;
		}
		return true;
	}
	if (op == "set_typhoon") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("set_typhoon: 不在 GameScene 或 Board 为空"); return false; }
		auto strengthIt = kTyphoonStrengthNames.find(cmd.value("strength", ""));
		auto directionIt = kWindDirectionNames.find(cmd.value("direction", "NONE"));
		if (strengthIt == kTyphoonStrengthNames.end() || directionIt == kWindDirectionNames.end()) {
			Fail("set_typhoon: strength 必须是 NONE/TYPHOON/SEVERE/SUPER，direction 必须是 NONE/HOUSE/FRONT");
			return false;
		}
		const bool changed = gs->GetBoard()->SetTyphoonForTesting(strengthIt->second, directionIt->second,
			cmd.value("gustIn", 30.0f), cmd.value("directionIn", 30.0f),
			cmd.value("gustsRemaining", 1), cmd.value("decayIn", 30.0f));
		const bool expectedSuccess = cmd.value("expectedSuccess", true);
		if (changed != expectedSuccess) {
			Fail("set_typhoon: 实际结果与 expectedSuccess 不符；只有支持台风的大雨允许启用，且必须提供 HOUSE/FRONT 风向");
			return false;
		}
		return true;
	}
	if (op == "reroll_typhoon_direction") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("reroll_typhoon_direction: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->GetBoard()->RerollWindDirectionForTesting(cmd.value("directionRoll", 0))) {
			Fail("reroll_typhoon_direction: 当前必须有台风，directionRoll 必须为 1..2");
			return false;
		}
		return true;
	}
	if (op == "trigger_typhoon_gust") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("trigger_typhoon_gust: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->GetBoard()->TriggerTyphoonGustForTesting(cmd.value("plantMoveIn", 0.0f))) {
			Fail("trigger_typhoon_gust: 当前不是带台风的大雨");
			return false;
		}
		return true;
	}
	if (op == "set_roof_runoff") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_roof_runoff: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		int rowMask = 0;
		if (cmd.contains("rows") && cmd["rows"].is_array()) {
			for (const auto& rowJson : cmd["rows"]) {
				if (!rowJson.is_number_integer()) {
					Fail("set_roof_runoff: rows 必须是合法行号数组");
					return false;
				}
				const int row = rowJson.get<int>();
				if (row < 0 || row >= board->mRows) {
					Fail("set_roof_runoff: rows 包含越界行号");
					return false;
				}
				rowMask |= 1 << row;
			}
		}
		else if (cmd.contains("row")) {
			// 兼容现有单行脚本；新脚本应传 rows，使测试表达与正式多行状态一致。
			const int row = cmd.value("row", -1);
			if (row >= 0 && row < board->mRows) rowMask = 1 << row;
		}
		auto phaseIt = kRoofRunoffPhaseNames.find(cmd.value("phase", "IDLE"));
		if (phaseIt == kRoofRunoffPhaseNames.end()
			|| !board->SetRoofRunoffForTesting(
				cmd.value("charge", 0.0f), phaseIt->second,
				rowMask, cmd.value("remaining", 0.0f),
				cmd.value("retainedCharge", 45.0f))) {
			Fail("set_roof_runoff: 仅昼夜屋顶可用；phase 必须为 IDLE/WARNING/FLOWING，活动阶段须提供非空合法 rows");
			return false;
		}
		return true;
	}
	if (op == "set_night_roof_charge") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_night_roof_charge: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto phaseIt = kNightRoofChargePhaseNames.find(
			cmd.value("phase", "CHARGING"));
		if (phaseIt == kNightRoofChargePhaseNames.end()
			|| !gs->GetBoard()->SetNightRoofChargeForTesting(
				cmd.value("charge", 0.0f), phaseIt->second,
				cmd.value("row", -1), cmd.value("remaining", 0.0f),
				cmd.value("overcharge", 0.0f))) {
			Fail("set_night_roof_charge: 仅黑夜屋顶可用；phase 必须为 CHARGING/WARNING/DISCHARGING，活动阶段须提供合法 row");
			return false;
		}
		return true;
	}
	if (op == "set_polar_environment") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_polar_environment: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto phaseIt = kPolarNightPhaseNames.find(
			cmd.value("phase", "DANGER_HOLD"));
		auto directionIt = kVerticalWindDirectionNames.find(
			cmd.value("direction", "NONE"));
		if (phaseIt == kPolarNightPhaseNames.end()
			|| directionIt == kVerticalWindDirectionNames.end()
			|| !gs->GetBoard()->SetPolarNightEnvironmentForTesting(
				cmd.value("temperature", -14.0f),
				cmd.value("humidity", 58.0f),
				cmd.value("wind", 8.0f), directionIt->second, phaseIt->second,
				cmd.value("remaining", -1.0f))) {
			Fail("set_polar_environment: 仅极夜雪原可用；phase 或 direction 无效");
			return false;
		}
		return true;
	}
	if (op == "show_test_message_box") {
		const std::string title = cmd.value("title", "");
		const std::string message = cmd.value("message", "");
		GameMessageBox::Builder builder(Vector(SCENE_WIDTH / 2.0f, SCENE_HEIGHT / 2.0f));
		builder.Scale(cmd.value("scale", 1.0f));
		if (!title.empty()) builder.Title(title);
		if (!message.empty()) builder.Message(message);
		builder.Button(u8"确定", Vector::zero(), Vector(100.0f, 41.6f), 14.0f, []() {});
		builder.Show();
		return true;
	}
	if (op == "activate_dawn_lotus") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("activate_dawn_lotus: 不在 GameScene 或 Board 为空");
			return false;
		}
		// 直接走 Board 的正式格子点击入口，避免测试脚本硬编码极夜地图像素坐标。
		if (!gs->GetBoard()->ActivateDawnLotusAt(
			cmd.value("row", -1), cmd.value("col", -1))) {
			Fail("activate_dawn_lotus: 指定格没有已充满且当前可释放的曙光莲");
			return false;
		}
		return true;
	}
	if (op == "seal_snow_hole") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()
			|| !gs->GetBoard()->SealSnowHole(cmd.value("row", -1))) {
			Fail("seal_snow_hole: 指定行没有形成中或活动雪穴");
			return false;
		}
		return true;
	}
	if (op == "set_snow_hole") {
		GameScene* gs = CurrentGameScene();
		const std::string phaseName = cmd.value("phase", "ACTIVE");
		SnowHolePhase phase = SnowHolePhase::NONE;
		if (phaseName == "FORMING") phase = SnowHolePhase::FORMING;
		else if (phaseName == "ACTIVE") phase = SnowHolePhase::ACTIVE;
		else if (phaseName != "NONE") {
			Fail("set_snow_hole: phase 必须是 NONE/FORMING/ACTIVE");
			return false;
		}
		if (!gs || !gs->GetBoard()
			|| !gs->GetBoard()->SetSnowHoleForTesting(
				cmd.value("row", -1), cmd.value("col", -1), phase,
				cmd.value("remaining", 0.0f))) {
			Fail("set_snow_hole: 仅极夜雪原可用，row/col 必须落在合法雪穴范围");
			return false;
		}
		return true;
	}
	if (op == "roll_typhoon") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("roll_typhoon: 不在 GameScene 或 Board 为空"); return false; }
		auto directionIt = kWindDirectionNames.find(cmd.value("direction", ""));
		if (directionIt == kWindDirectionNames.end()
			|| !gs->GetBoard()->RollTyphoonForTesting(cmd.value("chanceRoll", 0),
				cmd.value("strengthRoll", 0), directionIt->second)) {
			Fail("roll_typhoon: 只允许大雨，chanceRoll 必须为 1..100，strengthRoll 必须落在当前权重总和内，direction 必须为 HOUSE/FRONT");
			return false;
		}
		return true;
	}
	if (op == "set_weather_forecast") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("set_weather_forecast: 不在 GameScene 或 Board 为空"); return false; }
		auto forecastIt = kRainIntensityNames.find(cmd.value("forecast", ""));
		auto actualIt = kRainIntensityNames.find(cmd.value("actual", ""));
		if (forecastIt == kRainIntensityNames.end() || actualIt == kRainIntensityNames.end()) {
			Fail("set_weather_forecast: forecast/actual 必须是 CLEAR/LIGHT/MEDIUM/HEAVY");
			return false;
		}
		if (!gs->GetBoard()->SetWeatherForecastForTesting(forecastIt->second, actualIt->second,
			cmd.value("revealIn", 1.0f))) {
			Fail("set_weather_forecast: 当前关卡不支持天气，或天气尚未初始化");
			return false;
		}
		if (cmd.contains("typhoonStrength")) {
			auto typhoonIt = kTyphoonStrengthNames.find(cmd.value("typhoonStrength", ""));
			if (typhoonIt == kTyphoonStrengthNames.end()
				|| !gs->GetBoard()->SetPendingHeavyTyphoonForTesting(
					typhoonIt->second, cmd.value("promptVariant", -1))) {
				Fail("set_weather_forecast: typhoonStrength 仅允许在公开预报或真实下一天气涉及大雨时使用 NONE/TYPHOON/SEVERE/SUPER，promptVariant 必须为 0..2");
				return false;
			}
		}
		return true;
	}
	if (op == "show_image_prompt") {
		GameScene* gs = CurrentGameScene();
		if (!gs) { Fail("show_image_prompt: 不在 GameScene"); return false; }
		const std::string image = cmd.value("image", "");
		if (image == "HUGE_WAVE") {
			gs->ShowPrompt(ResourceKeys::Textures::IMAGE_HUGE_WAVE_APPROACHING,
				cmd.value("appear", 0.4f), cmd.value("hold", 4.0f), cmd.value("fade", 0.3f));
		}
		else if (image == "FINAL_WAVE") {
			gs->ShowPrompt(ResourceKeys::Textures::IMAGE_FINAL_WAVE,
				cmd.value("appear", 0.3f), cmd.value("hold", 2.0f), cmd.value("fade", 0.4f));
		}
		else {
			Fail("show_image_prompt: image 必须是 HUGE_WAVE 或 FINAL_WAVE");
			return false;
		}
		return true;
	}
	if (op == "roll_weather_forecast") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("roll_weather_forecast: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->GetBoard()->PrepareWeatherForecastForTesting(
			cmd.value("weatherRoll", 0), cmd.value("revealIn", 0.1f))) {
			Fail("roll_weather_forecast: 只允许已初始化的晴天，weatherRoll 必须落在当前权重总和内");
			return false;
		}
		return true;
	}
	if (op == "advance_weather_phase") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("advance_weather_phase: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->GetBoard()->AdvanceRainPhaseForTesting(cmd.value("roll", 1))) {
			Fail("advance_weather_phase: 当前无雨，或 roll 超出当前转档总权重");
			return false;
		}
		return true;
	}
	if (op == "trigger_lightning") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("trigger_lightning: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->GetBoard()->TriggerLightningForTesting()) {
			Fail("trigger_lightning: 只有大雨允许闪电");
			return false;
		}
		return true;
	}
	if (op == "set_adventure_level") {
		const int level = cmd.value("level", 1);
		if (level < 1) { Fail("set_adventure_level: level 必须为正数"); return false; }
		GameAPP::GetInstance().mAdventureLevel = level;
		return true;
	}
	if (op == "force_trophy") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("force_trophy: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		if (board->mBoardState != BoardState::GAME) { Fail("force_trophy: Board 尚未进入 GAME"); return false; }
		board->CreateTrophy(Vector(cmd.value("x", 500.0f), cmd.value("y", 300.0f)));
		return true;
	}
	if (op == "add_crater") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("add_crater: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		const float timeLeft = cmd.value("timeLeft", Crater::CRATER_DURATION);
		if (row < 0 || row >= board->mRows || col < 0 || col >= board->mColumns
			|| timeLeft <= 0.0f) {
			Fail("add_crater: row/col 必须在当前棋盘内，timeLeft 必须大于 0");
			return false;
		}
		if (!board->AddCrater(row, col, timeLeft)) {
			Fail("add_crater: Board::AddCrater 返回空");
			return false;
		}
		return true;
	}
	if (op == "extend_ice_trail") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("extend_ice_trail: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		if (row < 0 || row >= board->mRows) {
			Fail("extend_ice_trail: row 必须在当前棋盘内");
			return false;
		}
		const float frontX = cmd.value("frontX", board->GetRoofSlopeEndX() - 200.0f);
		if (cmd.value("golden", false)) board->ExtendGoldenIceTrail(row, frontX);
		else board->ExtendIceTrail(row, frontX);
		return true;
	}
	if (op == "trigger_mower") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("trigger_mower: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		for (int id : board->mEntityRegistry.GetAllMowerIDs()) {
			Mower* mower = board->mEntityRegistry.GetMower(id);
			if (mower && mower->mRow == row) {
				mower->Trigger();
				return true;
			}
		}
		Fail("trigger_mower: 未找到指定行的小推车");
		return false;
	}
	if (op == "trigger_rain_ground_splash") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("trigger_rain_ground_splash: 不在 GameScene 或 Board 为空");
			return false;
		}
		gs->GetBoard()->TriggerRainGroundSplashForTesting();
		return true;
	}
	if (op == "force_survival_round") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("force_survival_round: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		if (!board->mIsSurvival) { Fail("force_survival_round: 非生存模式关卡"); return false; }
		int round = cmd.value("round", 1);
		if (!board->SetSurvivalRoundForTesting(round)) {
			Fail("force_survival_round: round 必须为正数");
			return false;
		}
		return true;
	}
	if (op == "force_survival_round_clear") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("force_survival_round_clear: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		if (!board->mIsSurvival) { Fail("force_survival_round_clear: 非生存模式关卡"); return false; }
		if (board->mBoardState != BoardState::GAME) { Fail("force_survival_round_clear: Board 尚未进入 GAME"); return false; }
		// 走正式轮清入口，覆盖词条选择、选卡过场及场上对象保留行为。
		board->OnSurvivalRoundClear();
		return true;
	}
	if (op == "relocate_plant_group") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("relocate_plant_group: no Board"); return false; }
		Board* board = gs->GetBoard();
		const int source = board->GetRelocationSourceID(cmd.value("fromRow", -1), cmd.value("fromCol", -1));
		const bool result = board->RelocatePlantGroup(source, cmd.value("row", -1), cmd.value("col", -1));
		if (result != cmd.value("expectedSuccess", true)) {
			Fail("relocate_plant_group: unexpected result"); return false;
		}
		return true;
	}
	if (op == "plant") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("plant: 不在 GameScene 或 Board 为空"); return false; }
		auto it = kPlantNames.find(cmd.value("type", ""));
		if (it == kPlantNames.end()) { Fail("未知植物类型: " + cmd.value("type", "")); return false; }
		Plant* p = nullptr;
		if (it->second == PlantType::PLANT_IMITATER) {
			auto targetIt = kPlantNames.find(cmd.value("imitaterTarget", ""));
			if (targetIt == kPlantNames.end()
				|| targetIt->second == PlantType::PLANT_IMITATER) {
				Fail("plant: 模仿者必须提供合法 imitaterTarget");
				return false;
			}
			p = gs->GetBoard()->CreateImitaterPlant(targetIt->second,
				cmd.value("row", 0), cmd.value("col", 0));
		}
		else {
			p = gs->GetBoard()->CreatePlayerPlant(it->second,
				cmd.value("row", 0), cmd.value("col", 0));
		}
		if ((p != nullptr) != cmd.value("expectedSuccess", true)) {
			Fail("plant: 创建结果与 expectedSuccess 不符");
			return false;
		}
		if (!p) return true;
		if (auto* blover = dynamic_cast<Blover*>(p);
			blover && cmd.contains("bloverDirection")) {
			auto directionIt = kWindDirectionNames.find(
				cmd["bloverDirection"].get<std::string>());
			if (directionIt == kWindDirectionNames.end()
				|| directionIt->second == WindDirection::NONE) {
				Fail("plant: bloverDirection 必须是 HOUSE/FRONT");
				return false;
			}
			blover->SetBlowDirection(directionIt->second);
		}
		if (auto* imitater = dynamic_cast<Imitater*>(p);
			imitater && imitater->GetImitaterTarget() == PlantType::PLANT_BLOVER
			&& cmd.contains("bloverDirection")) {
			auto directionIt = kWindDirectionNames.find(
				cmd["bloverDirection"].get<std::string>());
			if (directionIt == kWindDirectionNames.end()
				|| directionIt->second == WindDirection::NONE) {
				Fail("plant: bloverDirection 必须是 HOUSE/FRONT");
				return false;
			}
			imitater->SetInheritedBloverDirection(directionIt->second);
		}
		return true;
	}
	if (op == "show_crazy_dave_dialog") {
		GameScene* gs = CurrentGameScene();
		if (!gs) { Fail("show_crazy_dave_dialog: 不在 GameScene"); return false; }
		if (!gs->StartCrazyDaveDialogForTesting(cmd.value("force", true))) {
			Fail("show_crazy_dave_dialog: 当前关卡未配置闲聊、已读且 force=false，或资源不完整");
			return false;
		}
		return true;
	}
	if (op == "advance_crazy_dave_dialog") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->AdvanceCrazyDaveDialogForTesting()) {
			Fail("advance_crazy_dave_dialog: 当前没有可推进的戴夫闲聊");
			return false;
		}
		return true;
	}
	if (op == "skip_crazy_dave_dialog") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->SkipCrazyDaveDialogForTesting()) {
			Fail("skip_crazy_dave_dialog: 当前没有可跳过的戴夫闲聊");
			return false;
		}
		return true;
	}
	if (op == "set_cob_cannon_arming") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_cob_cannon_arming: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto* cannon = dynamic_cast<CobCannon*>(gs->GetBoard()->GetNormalPlantAt(
			cmd.value("row", -1), cmd.value("col", -1)));
		const float seconds = cmd.value("value", -1.0f);
		if (!cannon || seconds < 0.0f || seconds > 30.0f) {
			Fail("set_cob_cannon_arming: 两侧格子均可定位炮体，value 必须在 0～30 秒");
			return false;
		}
		// 只压缩正式装填倒计时；充能轨、音效和 READY 边沿仍由游戏逻辑推进。
		cannon->SetArmingTimeForTesting(seconds);
		return true;
	}
	if (op == "begin_cob_cannon_targeting") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()
			|| !gs->GetBoard()->BeginCobCannonTargeting(
				cmd.value("row", -1), cmd.value("col", -1))) {
			Fail("begin_cob_cannon_targeting: 指定格不是已就绪玉米加农炮的任一侧");
			return false;
		}
		return true;
	}
	if (op == "fire_targeted_cob_cannon") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()
			|| !gs->GetBoard()->FireTargetedCobCannonAt(
				Vector(cmd.value("x", -1.0f), cmd.value("y", -1.0f)))) {
			Fail("fire_targeted_cob_cannon: 当前未瞄准、落点位于战场上界外或炮体不再可发射");
			return false;
		}
		return true;
	}
	if (op == "shovel_plant_at") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("shovel_plant_at: 不在 GameScene 或 Board 为空");
			return false;
		}
		Plant* plant = gs->GetBoard()->GetTopPlantAt(
			cmd.value("row", -1), cmd.value("col", -1));
		if (!plant) {
			Fail("shovel_plant_at: 指定格没有可铲植物");
			return false;
		}
		// 走植物统一死亡入口，验证任一占格命中后都会原子释放完整 footprint。
		plant->Die();
		return true;
	}
	if (op == "set_plant_health") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_plant_health: 不在 GameScene 或 Board 为空");
			return false;
		}
		const int row = cmd.value("row", 0);
		const int col = cmd.value("col", 0);
		Plant* plant = gs->GetBoard()->GetTopPlantAt(row, col);
		const int health = cmd.value("value", 0);
		if (!plant || health <= 0 || health > plant->mPlantMaxHealth) {
			Fail("set_plant_health: 顶层植物不存在或生命值越界");
			return false;
		}
		plant->mPlantHealth = health;
		return true;
	}
	if (op == "resolve_winter_ground_impact") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("resolve_winter_ground_impact: 不在 GameScene 或 Board 为空");
			return false;
		}
		Plant* plant = gs->GetBoard()->GetTopPlantAt(
			cmd.value("row", -1), cmd.value("col", -1));
		const std::string kindName = cmd.value("kind", "");
		WinterGroundImpactKind kind;
		if (kindName == "COLLISION") kind = WinterGroundImpactKind::COLLISION;
		else if (kindName == "GROUND_CRACK") kind = WinterGroundImpactKind::GROUND_CRACK;
		else {
			Fail("resolve_winter_ground_impact: kind 必须是 COLLISION 或 GROUND_CRACK");
			return false;
		}
		if (!plant) {
			Fail("resolve_winter_ground_impact: 指定格没有植物");
			return false;
		}
		const WinterGroundImpactResponse response =
			plant->ResolveWinterGroundImpact(kind);
		const int multiplierOn1000 = static_cast<int>(std::lround(
			response.downstreamDamageMultiplierCap * 1000.0f));
		if (response.intercepted != cmd.value("expectedIntercepted", true)
			|| response.containsScatter != cmd.value("expectedContainsScatter", false)
			|| multiplierOn1000 != cmd.value("expectedDownstreamMultiplierOn1000", 1000)) {
			Fail("resolve_winter_ground_impact: 植物响应与预期不符");
			return false;
		}
		return true;
	}
	if (op == "assert_can_plant") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("assert_can_plant: 不在 GameScene 或 Board 为空"); return false; }
		auto it = kPlantNames.find(cmd.value("type", ""));
		if (it == kPlantNames.end()) { Fail("assert_can_plant: 未知植物类型"); return false; }
		const int row = cmd.value("row", 0);
		const int col = cmd.value("col", 0);
		const bool expected = cmd.value("expected", true);
		const bool actual = gs->GetBoard()->CanPlantAt(it->second, row, col);
		if (actual != expected) {
			Fail("assert_can_plant: 判定不符 row=" + std::to_string(row)
				+ " col=" + std::to_string(col));
			return false;
		}
		return true;
	}
	if (op == "set_plantern_gear") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_plantern_gear: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto it = kPlanternGearNames.find(cmd.value("gear", ""));
		if (it == kPlanternGearNames.end() || !gs->GetBoard()->GetActivePlantern()) {
			Fail("set_plantern_gear: gear 必须是 OFF/LOW/MEDIUM/HIGH（或 0/I/II/III），且场上必须有路灯花");
			return false;
		}
		gs->GetBoard()->SetPlanternGear(it->second);
		return true;
	}
	if (op == "set_plantern_fuel") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()
			|| !gs->GetBoard()->SetPlanternFuelForTesting(cmd.value("value", -1.0f))) {
			Fail("set_plantern_fuel: value 必须非负，且场上必须有路灯花");
			return false;
		}
		return true;
	}
	if (op == "award_plantern_fuel") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()
			|| !gs->GetBoard()->AwardPlanternFuelForTesting(cmd.value("value", 0.0f))) {
			Fail("award_plantern_fuel: value 必须为正，且场上必须有路灯花");
			return false;
		}
		return true;
	}
	if (op == "toggle_plantern_menu") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard() || !gs->GetBoard()->GetActivePlantern()) {
			Fail("toggle_plantern_menu: 场上必须有路灯花");
			return false;
		}
		gs->TogglePlanternGearMenu();
		return true;
	}
	if (op == "assert_can_target") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("assert_can_target: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		auto typeIt = kPlantNames.find(cmd.value("plantType", ""));
		if (typeIt == kPlantNames.end()) {
			Fail("assert_can_target: 未知 plantType");
			return false;
		}
		Plant* plant = nullptr;
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			Plant* candidate = board->mEntityRegistry.GetPlant(id);
			if (candidate && candidate->mPlantType == typeIt->second
				&& candidate->mRow == cmd.value("plantRow", -1)
				&& candidate->mColumn == cmd.value("plantCol", -1)) {
				plant = candidate;
				break;
			}
		}
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		Zombie* zombie = nullptr;
		int seen = 0;
		const int zombieRow = cmd.value("zombieRow", -1);
		const int zombieIndex = cmd.value("zombieIndex", 0);
		for (int id : zombieIDs) {
			Zombie* candidate = board->mEntityRegistry.GetZombie(id);
			if (!candidate || !candidate->IsActive()) continue;
			if (zombieRow >= 0 && candidate->mRow != zombieRow) continue;
			if (seen++ == zombieIndex) {
				zombie = candidate;
				break;
			}
		}
		if (!plant || !zombie) {
			Fail("assert_can_target: 未找到指定植物或僵尸");
			return false;
		}
		const bool actual = board->CanPlantAcquireZombie(plant, zombie);
		if (actual != cmd.value("expected", true)) {
			Fail("assert_can_target: 雾中索敌判定与预期不符");
			return false;
		}
		return true;
	}
	if (op == "spawn_bullet") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("spawn_bullet: 不在 GameScene 或 Board 为空"); return false; }
		auto it = kBulletNames.find(cmd.value("type", ""));
		if (it == kBulletNames.end()) {
			Fail("spawn_bullet: 不支持的子弹类型");
			return false;
		}
		const int count = cmd.value("count", 1);
		if (count < 1 || count > 512) {
			Fail("spawn_bullet: count 必须在 1..512 范围内");
			return false;
		}
		const float startX = cmd.value("x", 100.0f);
		const float startY = cmd.value("y", 300.0f);
		const float xStep = cmd.value("xStep", 0.0f);
		const float yStep = cmd.value("yStep", 0.0f);
		for (int i = 0; i < count; ++i) {
			Bullet* bullet = gs->GetBoard()->CreateBullet(it->second, cmd.value("row", 0),
				Vector(startX + xStep * i, startY + yStep * i));
			if (!bullet) { Fail("spawn_bullet: CreateBullet 返回空"); return false; }
			bullet->SetVelocityX(cmd.value("velocityX", 290.0f));
			bullet->SetVelocityY(cmd.value("velocityY", 0.0f));
			bullet->SetBulletDamage(cmd.value("damage", bullet->GetBulletDamage()));
			if (cmd.contains("originPlant")) {
				auto originIt = kPlantNames.find(cmd.value("originPlant", ""));
				if (originIt == kPlantNames.end()) {
					Fail("spawn_bullet: originPlant 不是已注册植物名");
					return false;
				}
				bullet->SetPlantDamageOrigin(
					PlantDamageOrigin::FromPlant(originIt->second));
			}
			bullet->SetTargetsFlying(cmd.value("targetsFlying", false));
			if (cmd.contains("lobTargetX") && cmd.contains("lobTargetY")) {
				bullet->ConfigureLobbedMotion(
					Vector(cmd["lobTargetX"].get<float>(), cmd["lobTargetY"].get<float>()),
					cmd.value("lobDuration", 1.2f), cmd.value("lobApexHeight", 210.0f),
					cmd.value("targetsIceWall", false));
			}
		}
		return true;
	}
	if (op == "set_starfruit_shoot_cycle") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_starfruit_shoot_cycle: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			auto* starFruit = dynamic_cast<StarFruit*>(
				board->mEntityRegistry.GetPlant(id));
			if (!starFruit || (row >= 0 && starFruit->mRow != row)
				|| (col >= 0 && starFruit->mColumn != col)) {
				continue;
			}
			starFruit->SetShootCycleForTesting(
				cmd.value("elapsed", 1.49f), cmd.value("interval", 1.5f));
			return true;
		}
		Fail("set_starfruit_shoot_cycle: 未找到目标杨桃");
		return false;
	}
	if (op == "set_cabbagepult_shoot_cycle") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_cabbagepult_shoot_cycle: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			auto* cabbagePult = dynamic_cast<CabbagePult*>(
				board->mEntityRegistry.GetPlant(id));
			if (!cabbagePult || (row >= 0 && cabbagePult->mRow != row)
				|| (col >= 0 && cabbagePult->mColumn != col)) {
				continue;
			}
			cabbagePult->SetShootCycleForTesting(
				cmd.value("elapsed", 2.99f), cmd.value("interval", 3.0f));
			return true;
		}
		Fail("set_cabbagepult_shoot_cycle: 未找到目标卷心菜投手");
		return false;
	}
	if (op == "set_kernelpult_shoot_cycle") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_kernelpult_shoot_cycle: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		int forcedShot = -1;
		if (cmd.contains("butter")) forcedShot = cmd.value("butter", false) ? 1 : 0;
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			auto* kernelPult = dynamic_cast<KernelPult*>(
				board->mEntityRegistry.GetPlant(id));
			if (!kernelPult || (row >= 0 && kernelPult->mRow != row)
				|| (col >= 0 && kernelPult->mColumn != col)) {
				continue;
			}
			kernelPult->SetShootCycleForTesting(
				cmd.value("elapsed", 2.99f), cmd.value("interval", 3.0f), forcedShot);
			return true;
		}
		Fail("set_kernelpult_shoot_cycle: 未找到目标玉米投手");
		return false;
	}
	if (op == "set_melonpult_shoot_cycle") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_melonpult_shoot_cycle: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			auto* melonPult = dynamic_cast<MelonPult*>(
				board->mEntityRegistry.GetPlant(id));
			// 紫卡升级同帧内旧株仍可能留在实体表；夹具只能布置当前活动的承接株。
			if (!melonPult || !melonPult->IsActive()
				|| (row >= 0 && melonPult->mRow != row)
				|| (col >= 0 && melonPult->mColumn != col)) {
				continue;
			}
			melonPult->SetShootCycleForTesting(
				cmd.value("elapsed", 2.99f), cmd.value("interval", 3.0f));
			return true;
		}
		Fail("set_melonpult_shoot_cycle: 未找到目标西瓜投手");
		return false;
	}
	if (op == "set_melt_snow_pult_shoot_cycle") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_melt_snow_pult_shoot_cycle: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		int forcedShot = -1;
		if (cmd.contains("salt")) forcedShot = cmd.value("salt", false) ? 1 : 0;
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			auto* meltSnowPult = dynamic_cast<MeltSnowPult*>(
				board->mEntityRegistry.GetPlant(id));
			if (!meltSnowPult || (row >= 0 && meltSnowPult->mRow != row)
				|| (col >= 0 && meltSnowPult->mColumn != col)) continue;
			meltSnowPult->SetShootCycleForTesting(
				cmd.value("elapsed", 2.99f), cmd.value("interval", 3.0f), forcedShot);
			return true;
		}
		Fail("set_melt_snow_pult_shoot_cycle: 未找到目标融雪投手");
		return false;
	}
	if (op == "set_melt_snow_pult_salt_state") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_melt_snow_pult_salt_state: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			auto* meltSnowPult = dynamic_cast<MeltSnowPult*>(
				board->mEntityRegistry.GetPlant(id));
			if (!meltSnowPult || (row >= 0 && meltSnowPult->mRow != row)
				|| (col >= 0 && meltSnowPult->mColumn != col)) continue;
			meltSnowPult->SetSaltStateForTesting(
				cmd.value("ammo", 0), cmd.value("pending", false),
				cmd.value("observedForecast", false));
			return true;
		}
		Fail("set_melt_snow_pult_salt_state: 未找到目标融雪投手");
		return false;
	}
	if (op == "set_furnace_core_state") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_furnace_core_state: 不在 GameScene 或 Board 为空");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		auto* furnace = dynamic_cast<FurnaceCoreFlower*>(
			gs->GetBoard()->GetNormalPlantAt(row, col));
		if (!furnace) {
			Fail("set_furnace_core_state: 目标格没有炉芯花");
			return false;
		}
		furnace->SetCoreStateForTesting(cmd.value("cores", 0),
			cmd.value("progress", 0.0f));
		return true;
	}
	if (op == "reserve_plantern_fuel") {
		GameScene* gs = CurrentGameScene();
		const float amount = cmd.value("value", 0.0f);
		Plantern* plantern = gs && gs->GetBoard()
			? gs->GetBoard()->GetActivePlantern() : nullptr;
		if (!plantern || !std::isfinite(amount) || amount <= 0.0f) {
			Fail("reserve_plantern_fuel: value 必须为正，且场上必须有路灯花");
			return false;
		}
		plantern->ReserveFuel(amount);
		return true;
	}
	if (op == "deliver_plantern_fuel") {
		GameScene* gs = CurrentGameScene();
		const float amount = cmd.value("value", 0.0f);
		Plantern* plantern = gs && gs->GetBoard()
			? gs->GetBoard()->GetActivePlantern() : nullptr;
		if (!plantern || !std::isfinite(amount) || amount <= 0.0f) {
			Fail("deliver_plantern_fuel: value 必须为正，且场上必须有路灯花");
			return false;
		}
		plantern->DeliverReservedFuel(amount);
		return true;
	}
	if (op == "set_gloomshroom_shoot_cycle") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_gloomshroom_shoot_cycle: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int col = cmd.value("col", -1);
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			auto* gloomShroom = dynamic_cast<GloomShroom*>(
				board->mEntityRegistry.GetPlant(id));
			if (!gloomShroom || (row >= 0 && gloomShroom->mRow != row)
				|| (col >= 0 && gloomShroom->mColumn != col)) {
				continue;
			}
			gloomShroom->SetShootCycleForTesting(cmd.value("elapsed", 1.99f));
			return true;
		}
		Fail("set_gloomshroom_shoot_cycle: 未找到目标忧郁菇");
		return false;
	}
	if (op == "spawn_zombie") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("spawn_zombie: 不在 GameScene 或 Board 为空"); return false; }
		auto it = kZombieNames.find(cmd.value("type", ""));
		if (it == kZombieNames.end()) { Fail("未知僵尸类型: " + cmd.value("type", "")); return false; }
		Zombie* z = gs->GetBoard()->CreateZombie(it->second,
			cmd.value("row", 0), cmd.value("x", 900.0f));
		if (!z) { Fail("CreateZombie 返回空"); return false; }
		if (cmd.value("stationary", false)) {
			// 测试靶只停基础 Animator；不伪造冻结/减速状态，也不改变受击链。
			z->SetAnimationSpeed(0.0f);
			if (auto* balloon = dynamic_cast<BalloonZombie*>(z)) {
				balloon->SetFlightVelocity(0.0f);
			}
		}
		if (cmd.value("slowed", false)) {
			z->SetCooldown(cmd.value("slowDuration", 20.0f));
		}
		if (cmd.value("frozen", false) && !z->StartFrozen()) {
			Fail("spawn_zombie: frozen=true 但目标不能进入冻结");
			return false;
		}
		if (cmd.value("buttered", false) && !z->ApplyButter()) {
			Fail("spawn_zombie: buttered=true 但目标不能进入黄油定身");
			return false;
		}
		if (cmd.contains("paralyzedFor")) {
			const float duration = cmd.value("paralyzedFor", 0.0f);
			if (!z->ApplyParalysis(duration)) {
				Fail("spawn_zombie: paralyzedFor 无效或目标不能进入麻痹");
				return false;
			}
		}
		return true;
	}
	if (op == "add_ladder") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("add_ladder: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int column = cmd.value("col", -1);
		if (row < 0 || row >= board->mRows
			|| column < 0 || column >= board->mColumns) {
			Fail("add_ladder: row/col 超出当前地图");
			return false;
		}
		if (!board->AddLadder(row, column)) {
			Fail("add_ladder: Board 未能创建扶梯");
			return false;
		}
		return true;
	}
	if (op == "add_ice_wall") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("add_ice_wall: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		if (row < 0 || row >= board->mRows) {
			Fail("add_ice_wall: row 超出当前地图");
			return false;
		}
		if (!board->AddIceWall(row, cmd.value("x", 700.0f),
			cmd.value("health", IceWall::kDefaultHealth),
			cmd.value("maxHealth", IceWall::kDefaultHealth),
			cmd.value("thawDamageRemainder", 0.0f),
			cmd.value("constructionComplete", true),
			cmd.value("builderZombieID", NULL_ZOMBIE_ID))) {
			Fail("add_ice_wall: Board 未能创建唯一冰墙");
			return false;
		}
		return true;
	}
	if (op == "damage_ice_wall") {
		GameScene* gs = CurrentGameScene();
		IceWall* wall = gs && gs->GetBoard() ? gs->GetBoard()->GetIceWall() : nullptr;
		if (!wall) {
			Fail("damage_ice_wall: 未找到活动冰墙");
			return false;
		}
		const int directDamage = cmd.value("damage", 0);
		const int corrosion = cmd.value("corrosion", 0);
		if (directDamage > 0) {
			wall->TakeProjectileDamage(directDamage, cmd.value("fire", false));
		}
		if (wall->IsActive() && corrosion > 0) wall->ApplyWinterCorrosion(corrosion);
		return true;
	}
	if (op == "set_ice_wall_state") {
		GameScene* gs = CurrentGameScene();
		IceWall* wall = gs && gs->GetBoard() ? gs->GetBoard()->GetIceWall() : nullptr;
		if (!wall) {
			Fail("set_ice_wall_state: 未找到活动冰墙");
			return false;
		}
		wall->SetStateForTesting(cmd.value("x", wall->GetCenterX()),
			cmd.value("health", wall->GetHealth()),
			cmd.value("thawDamageRemainder", wall->GetThawDamageRemainder()));
		return true;
	}
	if (op == "set_ice_wall_engineer_state") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_ice_wall_engineer_state: 不在 GameScene 或 Board 为空");
			return false;
		}
		const std::string phaseName = cmd.value("phase", "MOVING");
		IceWallEngineerZombie::ConstructionPhase phase =
			IceWallEngineerZombie::ConstructionPhase::MOVING;
		if (phaseName == "BUILDING") {
			phase = IceWallEngineerZombie::ConstructionPhase::BUILDING;
		}
		else if (phaseName == "COMPLETED") {
			phase = IceWallEngineerZombie::ConstructionPhase::COMPLETED;
		}
		else if (phaseName != "MOVING") {
			Fail("set_ice_wall_engineer_state: phase 必须为 MOVING/BUILDING/COMPLETED");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* engineer = dynamic_cast<IceWallEngineerZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!engineer || !engineer->IsActive()) continue;
			if (row >= 0 && engineer->mRow != row) continue;
			if (seen++ != index) continue;
			engineer->SetConstructionStateForTesting(phase,
				cmd.value("remaining", 4.0f), cmd.value("wallX", 700.0f),
				cmd.value("used", false));
			return true;
		}
		Fail("set_ice_wall_engineer_state: 未找到目标冰墙工程师");
		return false;
	}
	if (op == "set_ice_crack_drill_state") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_ice_crack_drill_state: 不在 GameScene 或 Board 为空");
			return false;
		}
		const std::string phaseName = cmd.value("phase", "MOVING");
		IceCrackDrillZombie::DrillPhase phase = IceCrackDrillZombie::DrillPhase::MOVING;
		if (phaseName == "CHARGING") {
			phase = IceCrackDrillZombie::DrillPhase::CHARGING;
		}
		else if (phaseName == "SPENT") {
			phase = IceCrackDrillZombie::DrillPhase::SPENT;
		}
		else if (phaseName != "MOVING") {
			Fail("set_ice_crack_drill_state: phase 必须为 MOVING/CHARGING/SPENT");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		const bool setAll = cmd.value("all", false);
		int seen = 0;
		int changedCount = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* drill = dynamic_cast<IceCrackDrillZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!drill || !drill->IsActive()) continue;
			if (row >= 0 && drill->mRow != row) continue;
			if (!setAll && seen++ != index) continue;
			drill->SetDrillStateForTesting(phase,
				cmd.value("remaining", 3.5f), cmd.value("used", false));
			++changedCount;
			if (!setAll) return true;
		}
		if (setAll && changedCount > 0) return true;
		Fail("set_ice_crack_drill_state: 未找到目标冰裂钻机");
		return false;
	}
	if (op == "set_ice_executioner_state") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_ice_executioner_state: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		const int targetRow = cmd.value("targetRow", row);
		const int targetCol = cmd.value("targetCol", -1);
		if (targetRow < 0 || targetCol < 0) {
			Fail("set_ice_executioner_state: 必须提供 targetRow/targetCol");
			return false;
		}
		Plant* target = board->GetNormalPlantAt(targetRow, targetCol);
		if (!target) target = board->GetPumpkinAt(targetRow, targetCol);
		if (!target) {
			Fail("set_ice_executioner_state: 目标格没有普通植物或南瓜");
			return false;
		}
		int seen = 0;
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* executioner = dynamic_cast<IceStatueExecutionerZombie*>(
				board->mEntityRegistry.GetZombie(id));
			if (!executioner || !executioner->IsActive()) continue;
			if (row >= 0 && executioner->mRow != row) continue;
			if (seen++ != index) continue;
			if (!executioner->SetExecutionStateForTesting(
				target, cmd.value("progress", 0))) {
				Fail("set_ice_executioner_state: 无法建立冰封关系");
				return false;
			}
			return true;
		}
		Fail("set_ice_executioner_state: 未找到目标冰像处刑者");
		return false;
	}
	if (op == "attempt_ice_execution") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("attempt_ice_execution: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		const int targetRow = cmd.value("targetRow", row);
		const int targetCol = cmd.value("targetCol", -1);
		Plant* target = board->GetNormalPlantAt(targetRow, targetCol);
		if (!target) target = board->GetPumpkinAt(targetRow, targetCol);
		if (!target) {
			Fail("attempt_ice_execution: 目标格没有普通植物或南瓜");
			return false;
		}
		int seen = 0;
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* executioner = dynamic_cast<IceStatueExecutionerZombie*>(
				board->mEntityRegistry.GetZombie(id));
			if (!executioner || !executioner->IsActive()) continue;
			if (row >= 0 && executioner->mRow != row) continue;
			if (seen++ != index) continue;
			if (!executioner->AttemptExecutionForTesting(target)) {
				Fail("attempt_ice_execution: 正式造冰尝试未被处理");
				return false;
			}
			return true;
		}
		Fail("attempt_ice_execution: 未找到 READY 冰像处刑者");
		return false;
	}
	if (op == "interrupt_zombie_special_action"
		|| op == "apply_winter_corrosion_to_zombie"
		|| op == "force_zombie_surface_from_ground_hazard") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail(op + ": 不在 GameScene 或 Board 为空");
			return false;
		}
		ZombieType desiredType = ZombieType::NUM_ZOMBIE_TYPES;
		if (cmd.contains("type")) {
			auto typeIt = kZombieNames.find(cmd.value("type", ""));
			if (typeIt == kZombieNames.end()) {
				Fail(op + ": 未知僵尸类型");
				return false;
			}
			desiredType = typeIt->second;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			Zombie* zombie = gs->GetBoard()->mEntityRegistry.GetZombie(id);
			if (!zombie || !zombie->IsActive()) continue;
			if (row >= 0 && zombie->mRow != row) continue;
			if (desiredType != ZombieType::NUM_ZOMBIE_TYPES
				&& zombie->mZombieType != desiredType) continue;
			if (seen++ != index) continue;
			bool changed = false;
			if (op == "interrupt_zombie_special_action") {
				changed = zombie->InterruptUncommittedSpecialAction();
			}
			else if (op == "force_zombie_surface_from_ground_hazard") {
				changed = zombie->ForceSurfaceFromGroundHazard();
			}
			else {
				changed = zombie->ApplyWinterCorrosion(cmd.value("corrosion", 0));
			}
			if (cmd.contains("expectedChanged")
				&& changed != cmd.value("expectedChanged", false)) {
				Fail(op + ": changed 与 expectedChanged 不符");
				return false;
			}
			return true;
		}
		Fail(op + ": 未找到目标僵尸");
		return false;
	}
	if (op == "set_elite_ladder_scan_countdown") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_elite_ladder_scan_countdown: 不在 GameScene 或 Board 为空");
			return false;
		}
		const float seconds = cmd.value("value", -1.0f);
		if (seconds < 0.0f || seconds > 5.0f) {
			Fail("set_elite_ladder_scan_countdown: value 必须在 0～5 秒");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* elite = dynamic_cast<EliteLadderZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!elite || !elite->IsActive()) continue;
			if (row >= 0 && elite->mRow != row) continue;
			if (seen++ != index) continue;
			elite->SetRowScanTimeRemainingForTesting(seconds);
			return true;
		}
		Fail("set_elite_ladder_scan_countdown: 未找到目标精英扶梯僵尸");
		return false;
	}
	if (op == "make_healer_ready") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("make_healer_ready: 不在 GameScene 或 Board 为空");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		const bool makeAll = cmd.value("all", false);
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* healer = dynamic_cast<HealerZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!healer || !healer->IsActive()) continue;
			if (row >= 0 && healer->mRow != row) continue;
			if (makeAll) {
				// 压力测试必须在同一命令边沿同步放开全部目标，避免逐命令跨逻辑帧稀释尖峰。
				healer->MakeTreatmentReadyForTesting();
				++seen;
				continue;
			}
			if (seen++ != index) continue;
			// 只把正式冷却压到决策边沿；目标选择、前摇、结算和音画仍走正常路径。
			healer->MakeTreatmentReadyForTesting();
			return true;
		}
		if (makeAll && seen > 0) return true;
		Fail("make_healer_ready: 未找到目标急救员僵尸");
		return false;
	}
	if (op == "make_gargantuar_smash_ready") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("make_gargantuar_smash_ready: 不在 GameScene 或 Board 为空");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* gargantuar = dynamic_cast<GargantuarZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!gargantuar || !gargantuar->IsActive()
				|| gargantuar->GetPhase() != GargantuarZombie::Phase::SMASHING
				|| gargantuar->HasAppliedSmash()) {
				continue;
			}
			if (row >= 0 && gargantuar->mRow != row) continue;
			if (seen++ != index) continue;
			// 只推进到既有第 93 帧事件前；目标快照、植物反应和命中音画仍走正式路径。
			gargantuar->SetCurrentFrame(92.0f);
			return true;
		}
		Fail("make_gargantuar_smash_ready: 未找到尚未结算砸击的目标巨人僵尸");
		return false;
	}
	if (op == "make_gargantuar_throw_ready") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("make_gargantuar_throw_ready: 不在 GameScene 或 Board 为空");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* gargantuar = dynamic_cast<GargantuarZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!gargantuar || !gargantuar->IsActive()
				|| gargantuar->GetPhase() != GargantuarZombie::Phase::THROWING
				|| gargantuar->HasReleasedImp()) {
				continue;
			}
			if (row >= 0 && gargantuar->mRow != row) continue;
			if (seen++ != index) continue;
			// 只推进到既有第 131 帧脱手事件前；生成、阵营继承与视觉锚点对齐仍走正式路径。
			gargantuar->SetCurrentFrame(130.99f);
			return true;
		}
		Fail("make_gargantuar_throw_ready: 未找到尚未脱手的目标巨人僵尸");
		return false;
	}
	if (op == "set_jack_pop_countdown") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_jack_pop_countdown: 不在 GameScene 或 Board 为空");
			return false;
		}
		const float seconds = cmd.value("value", -1.0f);
		if (seconds < 0.0f || seconds > 60.0f) {
			Fail("set_jack_pop_countdown: value 必须在 0～60 秒");
			return false;
		}
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		for (const int id : zombieIDs) {
			auto* jack = dynamic_cast<JackInTheBoxZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!jack || !jack->IsActive()
				|| jack->mZombieType != ZombieType::ZOMBIE_JACK_IN_THE_BOX) {
				continue;
			}
			if (row >= 0 && jack->mRow != row) continue;
			if (seen++ != index) continue;
			jack->SetPopCountdownForTesting(seconds);
			return true;
		}
		Fail("set_jack_pop_countdown: 未找到目标小丑僵尸");
		return false;
	}
	if (op == "set_pogo_bounce_remaining") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_pogo_bounce_remaining: 不在 GameScene 或 Board 为空");
			return false;
		}
		const float seconds = cmd.value("value", -1.0f);
		if (seconds < 0.0f || seconds > 80.0f / 60.0f) {
			Fail("set_pogo_bounce_remaining: value 必须在 0～1.333334 秒");
			return false;
		}
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		for (const int id : zombieIDs) {
			auto* pogo = dynamic_cast<PogoZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!pogo || !pogo->IsActive()) continue;
			if (row >= 0 && pogo->mRow != row) continue;
			if (seen++ != index) continue;
			pogo->SetBounceRemainingForTesting(seconds);
			return true;
		}
		Fail("set_pogo_bounce_remaining: 未找到目标跳跳僵尸");
		return false;
	}
	if (op == "set_bungee_altitude" || op == "set_bungee_bottom_countdown") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail(op + ": 不在 GameScene 或 Board 为空");
			return false;
		}
		const int index = cmd.value("index", 0);
		const int row = cmd.value("row", -1);
		const float value = cmd.value("value", -1.0f);
		if (value < 0.0f) {
			Fail(op + ": value 必须大于等于 0");
			return false;
		}
		int seen = 0;
		std::vector<int> zombieIDs =
			gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* bungee = dynamic_cast<BungeeZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!bungee || !bungee->IsActive()) continue;
			if (row >= 0 && bungee->mRow != row) continue;
			if (seen++ != index) continue;
			if (op == "set_bungee_altitude") {
				bungee->SetAltitudeForTesting(value);
			}
			else {
				bungee->SetBottomWaitForTesting(value);
			}
			return true;
		}
		Fail(op + ": 未找到目标蹦极僵尸");
		return false;
	}
	if (op == "digger_lose_pickaxe") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("digger_lose_pickaxe: 不在 GameScene 或 Board 为空");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		ZombieType desiredType = ZombieType::ZOMBIE_DIGGER;
		if (cmd.contains("type")) {
			auto typeIt = kZombieNames.find(cmd.value("type", ""));
			if (typeIt == kZombieNames.end()
				|| (typeIt->second != ZombieType::ZOMBIE_DIGGER
					&& typeIt->second != ZombieType::ZOMBIE_ELITE_DIGGER)) {
				Fail("digger_lose_pickaxe: type 必须为普通或精英矿工");
				return false;
			}
			desiredType = typeIt->second;
		}
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* digger = dynamic_cast<DiggerZombie*>(
				gs->GetBoard()->mEntityRegistry.GetZombie(id));
			if (!digger || !digger->IsActive()
				|| digger->mZombieType != desiredType) continue;
			if (row >= 0 && digger->mRow != row) continue;
			if (seen++ != index) continue;
			digger->LosePickaxe();
			return true;
		}
		Fail("digger_lose_pickaxe: 未找到目标矿工僵尸");
		return false;
	}
	if (op == "set_elite_jack_throw_countdown") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_elite_jack_throw_countdown: 不在 GameScene 或 Board 为空");
			return false;
		}
		const float seconds = cmd.value("value", -1.0f);
		if (seconds < 0.0f || seconds > 60.0f) {
			Fail("set_elite_jack_throw_countdown: value 必须在 0～60 秒");
			return false;
		}
		Board* board = gs->GetBoard();
		const int sourceRow = cmd.value("row", -1);
		const int targetRow = cmd.value("targetRow", -1);
		const int targetColumn = cmd.value("targetColumn", -1);
		if (targetRow >= board->mRows || targetColumn >= board->mColumns) {
			Fail("set_elite_jack_throw_countdown: 固定目标超出当前地图");
			return false;
		}
		const int index = cmd.value("index", 0);
		int seen = 0;
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (const int id : zombieIDs) {
			auto* elite = dynamic_cast<EliteJackInTheBoxZombie*>(
				board->mEntityRegistry.GetZombie(id));
			if (!elite || !elite->IsActive()) continue;
			if (sourceRow >= 0 && elite->mRow != sourceRow) continue;
			if (seen++ != index) continue;
			elite->SetThrowCountdownForTesting(
				seconds, targetRow, targetColumn);
			return true;
		}
		Fail("set_elite_jack_throw_countdown: 未找到目标精英小丑");
		return false;
	}
	if (op == "spawn_wave_zombie") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("spawn_wave_zombie: 不在 GameScene 或 Board 为空"); return false; }
		auto it = kZombieNames.find(cmd.value("type", ""));
		if (it == kZombieNames.end()) { Fail("spawn_wave_zombie: 未知僵尸类型"); return false; }
		const int roll = cmd.value("mutationRoll", 0);
		if (roll < 1 || roll > 100) { Fail("spawn_wave_zombie: mutationRoll 必须为 1..100"); return false; }
		Board* board = gs->GetBoard();
		const ZombieType actual = board->ResolveWaveZombieType(it->second, roll);
		// 与正式 TrySummonZombie 一致：超过每波上限的候选被跳过，不生成回退类型。
		if (actual == ZombieType::NUM_ZOMBIE_TYPES) return true;
		const int row = cmd.value("row", 0);
		if (!board->CanSpawnZombieInRow(actual, row)) {
			Fail("spawn_wave_zombie: 目标行与正式地形规则不兼容");
			return false;
		}
		if (!board->CreateResolvedWaveZombie(actual, row, cmd.value("x", 900.0f))) {
			Fail("spawn_wave_zombie: CreateZombie 返回空");
			return false;
		}
		return true;
	}
	if (op == "assert_zombie_spawn_row") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("assert_zombie_spawn_row: 不在 GameScene 或 Board 为空");
			return false;
		}
		auto it = kZombieNames.find(cmd.value("type", ""));
		if (it == kZombieNames.end()) {
			Fail("assert_zombie_spawn_row: 未知僵尸类型");
			return false;
		}
		if (!cmd.contains("expected")) {
			Fail("assert_zombie_spawn_row: 缺 expected 字段");
			return false;
		}
		const int row = cmd.value("row", -1);
		const bool actual = gs->GetBoard()->CanSpawnZombieInRow(it->second, row);
		const bool expected = cmd["expected"].get<bool>();
		if (actual != expected) {
			Fail("assert_zombie_spawn_row: 行兼容性与预期不符 row="
				+ std::to_string(row));
			return false;
		}
		return true;
	}
	if (op == "summon_next_wave") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("summon_next_wave: 不在 GameScene 或 Board 为空"); return false; }
		const int count = cmd.value("count", 1);
		if (count < 1 || count > 100) { Fail("summon_next_wave: count 必须在 1～100"); return false; }
		for (int i = 0; i < count; ++i) {
			// 波次编排专项可释放上一波实体，仍保留已经承诺的下一波预报与正式累计上限。
			if (cmd.value("clearPrevious",false)) {
				const auto ids = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
				for (int id : ids) if (auto* zombie = gs->GetBoard()->mEntityRegistry.GetZombie(id)) zombie->Die();
			}
			gs->GetBoard()->SummonNextWave();
		}
		return true;
	}
	if (op == "damage_zombie" || op == "ash_damage_zombie") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail(op + ": 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);     // -1 = 不过滤行
		const int index = cmd.value("index", 0);  // 行过滤后按 ID 升序第 index 只
		const int damage = cmd.value("damage", 0);
		if (damage <= 0) { Fail(op + ": damage 必须大于 0"); return false; }
		auto sourceIt = kDamageSourceNames.find(cmd.value("source", "OTHER"));
		if (sourceIt == kDamageSourceNames.end()) { Fail(op + ": source 必须是 PLANT/ZOMBIE/OTHER"); return false; }
		ZombieType desiredType = ZombieType::NUM_ZOMBIE_TYPES;
		if (cmd.contains("type")) {
			auto typeIt = kZombieNames.find(cmd.value("type", ""));
			if (typeIt == kZombieNames.end()) {
				Fail(op + ": type 不是已注册僵尸名");
				return false;
			}
			desiredType = typeIt->second;
		}
		int seen = 0;
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (int id : zombieIDs) {
			Zombie* z = board->mEntityRegistry.GetZombie(id);
			if (!z) continue;
			if (row >= 0 && z->mRow != row) continue;
			if (desiredType != ZombieType::NUM_ZOMBIE_TYPES
				&& z->mZombieType != desiredType) continue;
			if (seen++ == index) {
				// 走正式受伤链（来源词条/护盾/头盔/断肢断头/免伤），用于验证而非直接 Die。
				if (op == "ash_damage_zombie") z->TakePlantAshDamage(damage);
				else {
					PlantDamageOrigin origin;
					if (cmd.contains("plantType")) {
						auto plantIt = kPlantNames.find(cmd.value("plantType", ""));
						if (plantIt == kPlantNames.end()) {
							Fail("damage_zombie: plantType 不是已注册植物名");
							return false;
						}
						origin = PlantDamageOrigin::FromPlant(plantIt->second);
					}
					z->TakeDamage(damage, sourceIt->second,
						cmd.value("penetrateShield", false), false, false, origin);
				}
				return true;
			}
		}
		Fail(op + ": 未找到目标僵尸 (row=" + std::to_string(row)
			+ ", type=" + cmd.value("type", "")
			+ ", index=" + std::to_string(index) + ")");
		return false;
	}
	if (op == "butter_zombie") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("butter_zombie: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		const bool expected = cmd.value("expectedApplied", true);
		int seen = 0;
		for (int id : zombieIDs) {
			Zombie* zombie = board->mEntityRegistry.GetZombie(id);
			if (!zombie || !zombie->IsActive()) continue;
			if (row >= 0 && zombie->mRow != row) continue;
			if (seen++ != index) continue;
			const bool applied = zombie->ApplyButter();
			if (applied != expected) {
				Fail("butter_zombie: ApplyButter 结果与 expectedApplied 不符");
				return false;
			}
			return true;
		}
		Fail("butter_zombie: 未找到目标僵尸");
		return false;
	}
	if (op == "apply_zombie_control") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("apply_zombie_control: 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		const std::string effect = cmd.value("effect", "");
		const float duration = cmd.value("duration", 4.0f);
		const bool expected = cmd.value("expectedApplied", true);
		int seen = 0;
		for (int id : zombieIDs) {
			Zombie* zombie = board->mEntityRegistry.GetZombie(id);
			if (!zombie || !zombie->IsActive()) continue;
			if (row >= 0 && zombie->mRow != row) continue;
			if (seen++ != index) continue;
			bool applied = false;
			if (effect == "SLOW") {
				zombie->SetCooldown(duration);
				applied = zombie->GetCooldownTimer() > 0.0f;
			}
			else if (effect == "FROZEN") applied = zombie->StartFrozen();
			else if (effect == "BUTTER") applied = zombie->ApplyButter();
			else if (effect == "PARALYSIS") applied = zombie->ApplyParalysis(duration);
			else {
				Fail("apply_zombie_control: effect 必须是 SLOW/FROZEN/BUTTER/PARALYSIS");
				return false;
			}
			if (applied != expected) {
				Fail("apply_zombie_control: 控制施加结果与 expectedApplied 不符");
				return false;
			}
			return true;
		}
		Fail("apply_zombie_control: 未找到目标僵尸");
		return false;
	}
	if (op == "set_zombie_mist_fuel_reward" || op == "kill_zombie") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail(op + ": 不在 GameScene 或 Board 为空");
			return false;
		}
		Board* board = gs->GetBoard();
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		int seen = 0;
		for (int id : zombieIDs) {
			Zombie* zombie = board->mEntityRegistry.GetZombie(id);
			if (!zombie || !zombie->IsActive()) continue;
			if (row >= 0 && zombie->mRow != row) continue;
			if (seen++ != index) continue;
			if (op == "set_zombie_mist_fuel_reward") {
				const float reward = cmd.value("value", 0.0f);
				if (reward <= 0.0f) {
					Fail("set_zombie_mist_fuel_reward: value 必须为正");
					return false;
				}
				zombie->SetMistFuelReward(reward);
			}
			else {
				// 走实体正式死亡入口，专门验证雾火结算与无路灯花丢弃契约。
				zombie->Die();
			}
			return true;
		}
		Fail(op + ": 未找到目标僵尸");
		return false;
	}
	if (op == "damage_plant") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("damage_plant: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);     // -1 = 不过滤行
		const int col = cmd.value("col", -1);     // -1 = 不过滤列
		const int index = cmd.value("index", 0);  // 过滤后按 ID 升序第 index 株
		const std::string typeName = cmd.value("type", ""); // 可选：按稳定植物名筛选叠层
		const int damage = cmd.value("damage", 0);
		if (damage <= 0) { Fail("damage_plant: damage 必须大于 0"); return false; }
		auto sourceIt = kDamageSourceNames.find(cmd.value("source", "OTHER"));
		if (sourceIt == kDamageSourceNames.end()) { Fail("damage_plant: source 必须是 PLANT/ZOMBIE/OTHER"); return false; }
		int seen = 0;
		for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
			Plant* p = board->mEntityRegistry.GetPlant(id);
			if (!p) continue;
			if (row >= 0 && p->mRow != row) continue;
			if (col >= 0 && p->mColumn != col) continue;
			if (!typeName.empty() && PlantTypeName(p->mPlantType) != typeName) continue;
			if (seen++ == index) {
				p->TakeDamage(damage, sourceIt->second);
				return true;
			}
		}
		Fail("damage_plant: 未找到目标植物 (row=" + std::to_string(row)
			+ ", col=" + std::to_string(col) + ", type=" + typeName
			+ ", index=" + std::to_string(index) + ")");
		return false;
	}
	if (op == "squish_plant") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("squish_plant: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);     // -1 = 不过滤行
		const int col = cmd.value("col", -1);     // -1 = 不过滤列
		const int index = cmd.value("index", 0);  // 过滤后按 ID 升序第 index 株
		int seen = 0;
		auto plantIDs = board->mEntityRegistry.GetAllPlantIDs();
		std::sort(plantIDs.begin(), plantIDs.end());
		for (int id : plantIDs) {
			Plant* p = board->mEntityRegistry.GetPlant(id);
			if (!p) continue;
			if (row >= 0 && p->mRow != row) continue;
			if (col >= 0 && p->mColumn != col) continue;
			if (seen++ == index) {
				// 直接走正式 Plant 入口；三类未来僵尸只负责决定何时、对哪格调用它。
				p->Squish();
				return true;
			}
		}
		Fail("squish_plant: 未找到目标植物 (row=" + std::to_string(row)
			+ ", col=" + std::to_string(col) + ", index=" + std::to_string(index) + ")");
		return false;
	}
	if (op == "add_perk") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("add_perk: 不在 GameScene 或 Board 为空"); return false; }
		auto it = kPerkNames.find(cmd.value("type", ""));
		if (it == kPerkNames.end()) { Fail("未知词条类型: " + cmd.value("type", "")); return false; }
		int count = cmd.value("count", 1);
		for (int i = 0; i < count; ++i) gs->GetBoard()->GetPerkManager().AddPerk(it->second);
		return true;
	}
	if (op == "survival_perk_open") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("survival_perk_open: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->GetBoard()->mIsSurvival) { Fail("survival_perk_open: 非生存关"); return false; }
		gs->BeginSurvivalPerkSelect();
		return true;
	}
	if (op == "apply_toxin_stacks") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("apply_toxin_stacks: 不在 GameScene 或 Board 为空");
			return false;
		}
		const int row = cmd.value("row", -1);
		const int index = cmd.value("index", 0);
		const int count = cmd.value("count", 1);
		if (count < 1 || count > 20) {
			Fail("apply_toxin_stacks: count 必须在 1～20");
			return false;
		}
		int seen = 0;
		std::vector<int> zombieIDs = gs->GetBoard()->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (int id : zombieIDs) {
			Zombie* zombie = gs->GetBoard()->mEntityRegistry.GetZombie(id);
			if (!zombie || !zombie->IsActive()) continue;
			if (row >= 0 && zombie->mRow != row) continue;
			if (seen++ != index) continue;
			for (int stack = 0; stack < count; ++stack) zombie->ApplyToxinStack();
			return true;
		}
		Fail("apply_toxin_stacks: 未找到目标僵尸");
		return false;
	}
	if (op == "set_next_survival_perk_rare") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) {
			Fail("set_next_survival_perk_rare: 不在 GameScene 或 Board 为空");
			return false;
		}
		gs->SetNextPerkRareRollOverrideForTesting(cmd.value("enabled", true) ? 1 : 0);
		return true;
	}
	if (op == "survival_perk_pick") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("survival_perk_pick: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->IsPerkSelectActive()) { Fail("survival_perk_pick: 当前无词条选择"); return false; }
		int index = cmd.value("index", -1);   // -1 = 放弃当前一次机会
		gs->ApplyPerkSelection(index);
		return true;
	}
	if (op == "survival_perk_refresh") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("survival_perk_refresh: 不在 GameScene 或 Board 为空"); return false; }
		if (!gs->IsPerkSelectActive()) { Fail("survival_perk_refresh: 当前无词条选择"); return false; }
		if (!gs->RefreshSurvivalPerkSelection()) { Fail("survival_perk_refresh: 本轮刷新次数已用完"); return false; }
		return true;
	}
	if (op == "show_zombie_hp") {
		GameAPP::GetInstance().mShowZombieHP = cmd.value("on", true);   // 调试：游戏内绘制僵尸血量
		return true;
	}
	if (op == "show_plant_hp") {
		GameAPP::GetInstance().mShowPlantHP = cmd.value("on", true);    // 调试：游戏内绘制植物血量
		return true;
	}
	if (op == "charm_zombie") {
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard()) { Fail("charm_zombie: 不在 GameScene 或 Board 为空"); return false; }
		Board* board = gs->GetBoard();
		const int row = cmd.value("row", -1);     // -1 = 不过滤行
		const int index = cmd.value("index", 0);  // 行过滤后按 ID 升序第 index 只
		int seen = 0;
		std::vector<int> zombieIDs = board->mEntityRegistry.GetAllZombieIDs();
		std::sort(zombieIDs.begin(), zombieIDs.end());
		for (int id : zombieIDs) {
			Zombie* z = board->mEntityRegistry.GetZombie(id);
			if (!z || !z->IsActive()) continue;
			if (row >= 0 && z->mRow != row) continue;
			if (seen++ == index) {
				z->StartMindControlled();   // 不可魅惑目标是 no-op：脚本用 dump_state 的 mindControlled 断言
				return true;
			}
		}
		Fail("charm_zombie: 未找到目标僵尸 (row=" + std::to_string(row) + ", index=" + std::to_string(index) + ")");
		return false;
	}
	if (op == "save_level_snapshot") {
		const std::string name = cmd.value("name", "");
		if (!IsSafeSnapshotName(name)) {
			Fail("save_level_snapshot: name 只允许 ASCII 字母、数字、_、-，且不能为空");
			return false;
		}
		GameScene* gs = CurrentGameScene();
		if (!gs || !gs->GetBoard() || !gs->GetCardSlotManager()) {
			Fail("save_level_snapshot: GameScene、Board 或 CardSlotManager 无效");
			return false;
		}
		const std::string path = (std::filesystem::path(mOutDir) / "snapshots"
			/ (name + ".json")).string();
		auto& saver = GameAPP::GetInstance().mGameInfoSaver;
		if (!saver.SaveAutoTestLevelSnapshot(
			gs->GetBoard(), gs->GetCardSlotManager(), path)) {
			Fail("save_level_snapshot: 正式序列化或写盘失败");
			return false;
		}
		if (!IsNonEmptyRegularFile(std::filesystem::u8path(path))) {
			Fail("save_level_snapshot: 快照不存在或为空");
			return false;
		}
		Log("snapshot saved: " + path);
		return true;
	}
	if (op == "reload_level_snapshot") {
		const std::string name = cmd.value("name", "");
		if (!IsSafeSnapshotName(name)) {
			Fail("reload_level_snapshot: name 只允许 ASCII 字母、数字、_、-，且不能为空");
			return false;
		}
		GameScene* oldScene = CurrentGameScene();
		if (!oldScene || !oldScene->GetBoard() || !oldScene->GetCardSlotManager()) {
			Fail("reload_level_snapshot: GameScene、Board 或 CardSlotManager 无效");
			return false;
		}
		const std::string path = (std::filesystem::path(mOutDir) / "snapshots"
			/ (name + ".json")).string();
		nlohmann::json snapshot;
		if (!FileManager::LoadJsonFile(path, snapshot)) {
			Fail("reload_level_snapshot: 快照缺失、为空或 JSON 损坏");
			return false;
		}

		const int level = oldScene->GetBoard()->mLevel;
		auto& saver = GameAPP::GetInstance().mGameInfoSaver;
		if (!saver.QueueAutoTestLevelSnapshotLoad(path)) {
			Fail("reload_level_snapshot: 无法登记一次性加载路径");
			return false;
		}
		auto& sm = SceneManager::GetInstance();
		sm.SetGlobalData("EnterLevel", std::to_string(level));
		if (!sm.SwitchTo("GameScene")) {
			saver.CancelAutoTestLevelSnapshotLoad();
			Fail("reload_level_snapshot: SwitchTo(GameScene) 失败");
			return false;
		}
		std::string loadError;
		if (!saver.ConsumeAutoTestLevelSnapshotLoadResult(loadError)) {
			Fail("reload_level_snapshot: " + loadError);
			return false;
		}
		GameScene* newScene = CurrentGameScene();
		if (!newScene || !newScene->GetBoard() || !newScene->GetCardSlotManager()
			|| newScene->GetBoard()->mLevel != level) {
			Fail("reload_level_snapshot: 新 GameScene 未确认加载同一关卡");
			return false;
		}
		Log("snapshot reloaded into fresh GameScene: " + path);
		return true;
	}
	if (op == "glyph_atlas_rebuild_probe") {
		Scene* scene = SceneManager::GetInstance().GetCurrentScene();
		if (!scene) { Fail("glyph_atlas_rebuild_probe: scene 为空"); return false; }
		if (!cmd.value("on", true)) {
			scene->UnregisterDrawCommand("GlyphAtlasRebuildProbe");
			return true;
		}
		// Update 阶段清缓存；下一条 screenshot 必须紧接本命令，捕获首次扩图的同一帧。
		GameAPP::GetInstance().GetGraphics().ClearTextCache();
		scene->RegisterDrawCommand("GlyphAtlasRebuildProbe", [](Graphics* g) {
			// 左列依次引入新数字，右列用完整图集重画同串，供截图逐像素比较。
			const BlendMode savedBlend = g->GetBlendMode();
			g->SetBlendMode(BlendMode::Alpha);
			g->FillRect(100.0f, 140.0f, 900.0f, 300.0f, glm::vec4(48, 48, 48, 255));
			const char* labels[] = { "300/300", "299/300", "187/300", "654/300" };
			for (int column = 0; column < 2; ++column) {
				for (int row = 0; row < 4; ++row) {
					const float x = 140.0f + column * 480.0f;
					const float y = 170.0f + row * 60.0f;
					g->SetBlendMode(BlendMode::Add);
					g->FillRect(x + 270.0f, y, 12.0f, 12.0f, glm::vec4(80, 80, 80, 255));
					g->DrawGlyphRun(labels[row], ResourceKeys::Fonts::FONT_FZCQ, 37,
						glm::vec4(0, 255, 0, 255), x, y);
				}
			}
			g->SetBlendMode(savedBlend);
		}, LAYER_UI + 100);
		return true;
	}
	if (op == "screenshot") {
		const std::string name = cmd.value("name", "shot.png");
		auto* renderer = GameAPP::GetInstance().GetCaptureBackend();
		if (!renderer) { Fail("screenshot: renderer 为空"); return false; }
		if (mCaptureTicket == 0) {
			mCaptureTicket = renderer->RequestCapture(mOutDir + "/" + name);
			if (renderer->GetCaptureStatus(mCaptureTicket) == pvz::CaptureStatus::Failed) {
				Fail("screenshot: " + renderer->GetCaptureError(mCaptureTicket));
				return false;
			}
			// 本帧到此为止并保持当前命令；EndFrame 才能回读、写 PNG 并发布 ticket 完成。
			mBreakFrame = true;
			return false;
		}

		const auto status = renderer->GetCaptureStatus(mCaptureTicket);
		if (status == pvz::CaptureStatus::Pending) return false;
		if (status == pvz::CaptureStatus::Failed || status == pvz::CaptureStatus::Unknown) {
			Fail("screenshot: " + renderer->GetCaptureError(mCaptureTicket));
			return false;
		}
		const auto path = std::filesystem::u8path(mOutDir + "/" + name);
		if (!IsNonEmptyRegularFile(path)) {
			Fail("screenshot: 渲染器报告成功，但 PNG 不存在或为空");
			return false;
		}
		Log("capture ticket " + std::to_string(mCaptureTicket)
			+ " persisted: " + path.u8string());
		if (mInteractiveReady) mInteractiveResults.push_back({
			{"op", "screenshot"}, {"ok", true}, {"reason", ""}, {"path", path.u8string()} });
		return true;
	}
	if (op == "dump_state") {
		nlohmann::json out;
		if (!BuildStateJson("dump_state", out)) return false;
		const std::string name = cmd.value("name", "state.json");
		std::ofstream of(mOutDir + "/" + name, std::ios::trunc);
		if (!of) { Fail("dump_state: 无法写 " + name); return false; }
		try {
			of << out.dump(2);
		}
		catch (const std::exception& e) {
			Fail("dump_state: JSON 序列化失败: " + std::string(e.what()));
			return false;
		}
		return true;
	}
	if (op == "assert_state") {
		// { "op":"assert_state", "path":"perks.stacks.PLANT_DAMAGE_UP", "equals":2 }
		// path 点分嵌套，纯数字段视为数组下标（如 "zombies.0.mindControlled"）；
		// equals 与实际值用 nlohmann::json operator== 严格比对，不匹配 Fail → exit 1。
		nlohmann::json state;
		if (!BuildStateJson("assert_state", state)) return false;
		const std::string path = cmd.value("path", "");
		if (path.empty()) { Fail("assert_state: 缺少 path"); return false; }
		const bool hasEquals = cmd.contains("equals");
		const bool hasAtLeast = cmd.contains("atLeast");
		const bool hasAtMost = cmd.contains("atMost");
		if (!hasEquals && !hasAtLeast && !hasAtMost) {
			Fail("assert_state: 缺少 equals/atLeast/atMost (path=" + path + ")");
			return false;
		}

		const nlohmann::json* cur = &state;
		size_t begin = 0;
		while (begin <= path.size()) {
			const size_t dot = path.find('.', begin);
			const std::string seg = path.substr(begin, dot == std::string::npos ? std::string::npos : dot - begin);
			if (cur->is_array() && !seg.empty()
				&& seg.find_first_not_of("0123456789") == std::string::npos) {
				const size_t idx = static_cast<size_t>(std::stoul(seg));
				if (idx >= cur->size()) {
					Fail("assert_state: 下标越界 \"" + seg + "\" (path=" + path
						+ ", size=" + std::to_string(cur->size()) + ")");
					return false;
				}
				cur = &(*cur)[idx];
			}
			else if (cur->is_object() && cur->contains(seg)) {
				cur = &(*cur)[seg];
			}
			else {
				Fail("assert_state: path 段不存在 \"" + seg + "\" (path=" + path + ")");
				return false;
			}
			if (dot == std::string::npos) break;
			begin = dot + 1;
		}

		if (hasEquals) {
			const nlohmann::json& expected = cmd["equals"];
			if (*cur != expected) {
				Fail("assert_state: 断言失败 path=" + path
					+ " 期望=" + expected.dump() + " 实际=" + cur->dump());
				return false;
			}
			Log("assert_state OK: " + path + " == " + expected.dump());
		}
		if (hasAtLeast || hasAtMost) {
			if (!cur->is_number()
				|| (hasAtLeast && !cmd["atLeast"].is_number())
				|| (hasAtMost && !cmd["atMost"].is_number())) {
				Fail("assert_state: atLeast/atMost 只支持数值 (path=" + path + ")");
				return false;
			}
			const double actual = cur->get<double>();
			if (hasAtLeast && actual < cmd["atLeast"].get<double>()) {
				Fail("assert_state: 断言失败 path=" + path + " 实际=" + cur->dump()
					+ " 小于下限=" + cmd["atLeast"].dump());
				return false;
			}
			if (hasAtMost && actual > cmd["atMost"].get<double>()) {
				Fail("assert_state: 断言失败 path=" + path + " 实际=" + cur->dump()
					+ " 大于上限=" + cmd["atMost"].dump());
				return false;
			}
			Log("assert_state OK: " + path + " in numeric bounds");
		}
		return true;
	}
	if (op == "quit") {
		Log("done cmd#" + std::to_string(mIndex) + " (quit)");
		Finish();
		return false;   // Finish 已停机，不再推进
	}

	if (op == "key") {
		const std::string name = cmd.value("name", "");
		auto it = kKeyNames.find(name);
		if (it == kKeyNames.end()) { Fail("未知按键名: " + name); return false; }
		const SDL_Keycode key = it->second;
		const std::string action = cmd.value("action", "press");

		auto pushKey = [&](Uint32 type) {
			SDL_Event ev{};
			ev.type = type;
			ev.key.keysym.sym = key;
			SDL_PushEvent(&ev);
		};

		if (action == "down") { pushKey(SDL_KEYDOWN); return true; }
		if (action == "up")   { pushKey(SDL_KEYUP);   return true; }
		if (action == "press") {
			// 跨帧：down 帧推 KEYDOWN（下一帧 poll 置 PRESSED，场景读到按下沿），
			// 下一帧推 KEYUP（再下一帧 poll 置 RELEASED）。mInputPhase=0 即"下帧收尾"。
			if (mInputPhase < 0) { pushKey(SDL_KEYDOWN); mInputPhase = 0; return false; }
			pushKey(SDL_KEYUP);
			return true;
		}
		Fail("未知 key action: " + action);
		return false;
	}

	if (op == "move_mouse") {
		if (!cmd.contains("x")) { Fail("move_mouse 缺 x 字段"); return false; }
		if (!cmd.contains("y")) { Fail("move_mouse 缺 y 字段"); return false; }

		SDL_Event move{};
		move.type = SDL_MOUSEMOTION;
		move.motion.x = static_cast<Sint32>(cmd.value("x", 0.0f));
		move.motion.y = static_cast<Sint32>(cmd.value("y", 0.0f));
		SDL_PushEvent(&move);
		return true;
	}

	if (op == "click") {
		float x = 0.0f, y = 0.0f;
		// "target":"trophy"：执行时从 Board 实时解析奖杯坐标（僵尸死亡位置受帧时序影响，
		// 静态写死坐标会漂移脱靶）；仍走下方 SDL_PushEvent 合成输入路径。
		const std::string target = cmd.value("target", "");
		if (target == "trophy") {
			GameScene* gs = CurrentGameScene();
			if (!gs || !gs->GetBoard()) { Fail("click target=trophy: 不在 GameScene 或 Board 为空"); return false; }
			auto trophy = gs->GetBoard()->mTrophy.lock();
			if (!trophy) { Fail("click target=trophy: 场上没有奖杯"); return false; }
			const Vector pos = trophy->GetPosition();
			x = pos.x;
			y = pos.y;
		}
		else if (target == "restore_last_cards") {
			GameScene* gs = CurrentGameScene();
			ChooseCardUI* ui = gs ? gs->GetChooseCardUI() : nullptr;
			auto button = ui ? ui->GetRestoreButton() : nullptr;
			if (!button || !button->IsEnabled()) {
				Fail("click target=restore_last_cards: 上次选卡按钮不存在或不可用");
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "choose_card_page"
			|| target == "imitater_previous_page" || target == "imitater_next_page") {
			GameScene* gs = CurrentGameScene();
			ChooseCardUI* ui = gs ? gs->GetChooseCardUI() : nullptr;
			auto button = ui ? (target == "imitater_previous_page"
				? ui->GetImitaterPreviousPageButton() : target == "imitater_next_page"
				? ui->GetImitaterNextPageButton() : ui->GetPageButton()) : nullptr;
			if (!button || !button->IsEnabled() || button->IsSkipDraw()) {
				Fail("click target=" + target + ": 选卡翻页按钮不存在或不可用");
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "zombie_almanac_previous_page") {
			auto* almanac = dynamic_cast<ZombieAlmanacScene*>(
				SceneManager::GetInstance().GetCurrentScene());
			auto button = almanac ? almanac->GetPreviousPageButton() : nullptr;
			if (!button || !button->IsEnabled() || button->IsSkipDraw()) {
				Fail("click target=zombie_almanac_previous_page: 僵尸图鉴上一页按钮不存在或不可用");
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "zombie_almanac_next_page") {
			auto* almanac = dynamic_cast<ZombieAlmanacScene*>(
				SceneManager::GetInstance().GetCurrentScene());
			auto button = almanac ? almanac->GetNextPageButton() : nullptr;
			if (!button || !button->IsEnabled() || button->IsSkipDraw()) {
				Fail("click target=zombie_almanac_next_page: 僵尸图鉴下一页按钮不存在或不可用");
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "game_select_previous_page") {
			auto* selectScene = dynamic_cast<GameSelectScene*>(
				SceneManager::GetInstance().GetCurrentScene());
			auto button = selectScene ? selectScene->GetPreviousPageButton() : nullptr;
			if (!button || !button->IsEnabled() || button->IsSkipDraw()) {
				Fail("click target=game_select_previous_page: 关卡选择上一页按钮不存在或不可用");
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "game_select_next_page") {
			auto* selectScene = dynamic_cast<GameSelectScene*>(
				SceneManager::GetInstance().GetCurrentScene());
			auto button = selectScene ? selectScene->GetNextPageButton() : nullptr;
			if (!button || !button->IsEnabled() || button->IsSkipDraw()) {
				Fail("click target=game_select_next_page: 关卡选择下一页按钮不存在或不可用");
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "game_select_level") {
			auto* selectScene = dynamic_cast<GameSelectScene*>(
				SceneManager::GetInstance().GetCurrentScene());
			const int level = cmd.value("level", -1);
			auto button = selectScene ? selectScene->GetCardButton(level) : nullptr;
			if (!button || !button->IsEnabled() || button->IsSkipDraw()) {
				Fail("click target=game_select_level: 关卡未解锁、未创建或不在当前页: "
					+ std::to_string(level));
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "main_menu_survival" || target == "main_menu_minigames") {
			auto* mainMenu = dynamic_cast<MainMenuScene*>(
				SceneManager::GetInstance().GetCurrentScene());
			auto button = mainMenu ? (target == "main_menu_minigames"
				? mainMenu->GetMiniGamesButton() : mainMenu->GetSurvivalButton()) : nullptr;
			if (!button || !button->IsEnabled()) {
				Fail("click: 不在主菜单或指定模式入口不可用");
				return false;
			}
			const Vector center = button->GetCenter();
			x = center.x;
			y = center.y;
		}
		else if (target == "choose_card") {
			GameScene* gs = CurrentGameScene();
			ChooseCardUI* ui = gs ? gs->GetChooseCardUI() : nullptr;
			const std::string plantName = cmd.value("plantType", "");
			auto typeIt = kPlantNames.find(plantName);
			Card* card = ui && typeIt != kPlantNames.end()
				? ui->FindCardByType(typeIt->second) : nullptr;
			Transform* transform = card ? card->GetTransform() : nullptr;
			if (!card || !card->IsActive() || !transform) {
				Fail("click target=choose_card: 卡牌不存在、当前页不可见或未激活: "
					+ plantName);
				return false;
			}
			const Vector topLeft = transform->GetPosition();
			x = topLeft.x + CARD_WIDTH * 0.5f;
			y = topLeft.y + CARD_HEIGHT * 0.5f;
		}
		else if (!target.empty()) { Fail("未知 click target: " + target); return false; }
		else {
			// 显式查在：缺字段时不静默回落到 (0,0) 点击，分别精确报错
			if (!cmd.contains("x")) { Fail("click 缺 x 字段"); return false; }
			if (!cmd.contains("y")) { Fail("click 缺 y 字段"); return false; }
			x = cmd.value("x", 0.0f);
			y = cmd.value("y", 0.0f);
		}
		const std::string btnName = cmd.value("button", "left");
		auto bit = kMouseButtonNames.find(btnName);
		if (bit == kMouseButtonNames.end()) { Fail("未知鼠标按钮: " + btnName); return false; }
		const Uint8 button = bit->second;
		const int holdFrames = std::max(1, cmd.value("hold_frames", 1));
		// 可见测试被真实鼠标干扰时，按下后/释放前记录实际命中状态，区分布局错误与输入丢失。
		if (cmd.value("trace", false) && (mInputPhase == holdFrames || mInputPhase == 0)) {
			const auto mouse = GameAPP::GetInstance().GetInputHandler().GetMousePosition();
			Log("click trace phase=" + std::to_string(mInputPhase) + " mouse="
				+ std::to_string(mouse.x) + "," + std::to_string(mouse.y));
			if (auto* scene = SceneManager::GetInstance().GetCurrentScene()) {
				auto& ui = scene->GetUIManager();
				for (size_t i = 0; i < ui.GetButtonCount(); ++i) {
					const auto b = ui.GetButton(i);
					if (b && b->ContainsPoint(Vector(x, y)))
						Log("click trace button=" + std::to_string(i) + " pressed="
							+ std::to_string(b->IsPressed()) + " hovered=" + std::to_string(b->IsHovered()));
				}
			}
		}
		auto pushMouseMotion = [x, y]() {
			SDL_Event move{};
			move.type = SDL_MOUSEMOTION;
			move.motion.x = static_cast<Sint32>(x);
			move.motion.y = static_cast<Sint32>(y);
			SDL_PushEvent(&move);
		};

		// 跨帧状态机：保持与释放帧都重新钉住目标点，避免可见 AutoTest 期间真实鼠标事件
		// 把合成按下与释放拆到不同对象上；整条路径仍经过 SDL、Collider 与 Clickable。
		if (mInputPhase < 0) {
			pushMouseMotion(); // AutoTest 窗口 scale=1，逻辑坐标即屏幕像素

			SDL_Event down{};
			down.type = SDL_MOUSEBUTTONDOWN;
			down.button.button = button;
			down.button.x = static_cast<Sint32>(x);
			down.button.y = static_cast<Sint32>(y);
			SDL_PushEvent(&down);

			mInputPhase = holdFrames;
			return false;
		}
		if (mInputPhase > 0) {
			pushMouseMotion();
			--mInputPhase;
			return false;
		}

		pushMouseMotion();
		SDL_Event up{};
		up.type = SDL_MOUSEBUTTONUP;
		up.button.button = button;
		up.button.x = static_cast<Sint32>(x);
		up.button.y = static_cast<Sint32>(y);
		SDL_PushEvent(&up);
		return true;
	}

	Fail("未知命令 op=\"" + op + "\"");
	return false;
}

bool TestDriver::BuildStateJson(const std::string& opName, nlohmann::json& out)
{
	Scene* currentScene = SceneManager::GetInstance().GetCurrentScene();
	if (!currentScene) { Fail(opName + ": 当前 Scene 为空"); return false; }
	auto& gameApp = GameAPP::GetInstance();
	out["scene"] = currentScene->name;
	out["adventureLevel"] = gameApp.mAdventureLevel;
	// 奖励页和选关页同样需要验证发卡结果，不依赖正在运行的 Board。
	out["haveCardCount"] = static_cast<int>(gameApp.mHaveCards.size());
	out["haveCards"] = nlohmann::json::array();
	for (PlantType type : gameApp.mHaveCards)
		out["haveCards"].push_back(PlantTypeName(type));
	out["difficulty"] = gameApp.Difficulty;
	out["encounteredEliteDancer"] = gameApp.HasEncounteredEliteDancer();
	out["monteCarloAIEnabled"] = gameApp.mEnableMonteCarloAI;
	out["advancedPauseEnabled"] = gameApp.mAdvancedPauseEnabled;
	out["hxyModeEnabled"] = gameApp.mHxyModeEnabled;
	out["cursorType"] = CursorTypeName(
		CursorManager::GetInstance().GetCurrentCursorType());
	out["cursorHoverCount"] = CursorManager::GetInstance().GetHoverCount();
	out["openingTyphoonProtectionEnabled"] =
		gameApp.mOpeningTyphoonProtectionEnabled;
	out["typhoonWeatherEnabled"] = gameApp.mTyphoonWeatherEnabled;
	out["lastSelectedCards"] = gameApp.mLastSelectedCards;
	out["lastSelectedCardCount"] = static_cast<int>(gameApp.mLastSelectedCards.size());
	out["crazyDaveTutorialsSeen"] = gameApp.mCrazyDaveTutorialsSeen;
	out["crazyDaveResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_CRAZY_DAVE) },
		{ "requiredTextureCount", CrazyDaveDialog::GetRequiredTextureCount() },
		{ "loadedTextureCount", CrazyDaveDialog::GetLoadedRequiredTextureCount() },
		{ "requiredVoiceSoundCount", CrazyDaveDialog::GetRequiredVoiceSoundCount() },
		{ "loadedVoiceSoundCount", CrazyDaveDialog::GetLoadedRequiredVoiceSoundCount() },
	};
	out["dialogSkinResources"] = {
		{ "fontLoaded", ResourceManager::GetInstance().GetFont(ResourceKeys::Fonts::FONT_FZJT, 26) != nullptr },
		{ "requiredTextureCount", GameMessageBox::GetStandardSkinRequiredTextureCount() },
		{ "loadedTextureCount", GameMessageBox::GetLoadedStandardSkinTextureCount() },
	};
	out["activeMessageBoxCount"] = currentScene->GetUIManager().GetActiveMessageBoxCount();
	out["gameMessageBox"] = nullptr;
	if (auto messageBox = currentScene->GetUIManager().GetTopActiveMessageBox()) {
		const Vector position = messageBox->GetPosition();
		const Vector size = messageBox->GetSize();
		out["gameMessageBox"] = {
			{ "adaptiveStandardSkin", messageBox->UsesAdaptiveStandardSkin() },
			{ "centerXInt", static_cast<int>(std::lround(position.x)) },
			{ "centerYInt", static_cast<int>(std::lround(position.y)) },
			{ "widthInt", static_cast<int>(std::lround(size.x)) },
			{ "heightInt", static_cast<int>(std::lround(size.y)) },
			{ "wrappedMessageLineCount", messageBox->GetWrappedMessageLineCount() },
		};
	}
	out["adaptiveMusic"] = {
		{ "volumePct", static_cast<int>(std::lround(AudioSystem::GetMusicVolume() * 100.0f)) },
		{ "playing", AudioSystem::IsAdaptiveMusicPlaying() },
		{ "currentTune", AudioSystem::GetAdaptiveMusicCurrentTune() },
		{ "preparedTune", AudioSystem::GetAdaptiveMusicPreparedTune() },
		{ "lastPlayStartedImmediately",
			AudioSystem::DidAdaptiveMusicLastPlayStartImmediately() },
		{ "lastPreparationMilliseconds",
			AudioSystem::GetAdaptiveMusicLastPreparationMilliseconds() },
		{ "lastPlayHandoffMicroseconds",
			AudioSystem::GetAdaptiveMusicLastPlayHandoffMicroseconds() },
	};
	out["dolphinAppearSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_DOLPHIN_APPEARS);
	out["dolphinBeforeJumpSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_DOLPHIN_BEFORE_JUMPING);
	out["plantWaterSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_PLANT_WATER);
	out["zombieEnteringWaterSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_ZOMBIE_ENTERING_WATER);
	out["poolTransitionSoundRequestCount"] =
		out["plantWaterSoundRequestCount"].get<int>()
		+ out["zombieEnteringWaterSoundRequestCount"].get<int>();
	out["poolSplashResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_POOL_SPLASH) },
		{ "soundsLoaded",
			ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_PLANT_WATER) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_ZOMBIE_ENTERING_WATER) != nullptr },
		{ "waterParticlePartsLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_WATERPARTICLE_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_WATERPARTICLE_PART_1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_WATERPARTICLE_PART_2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_WATERPARTICLE_PART_3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_WATERPARTICLE_PART_4, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_WATERPARTICLE_PART_5, false) != nullptr },
	};
	out["bonkSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BONK);
	out["softChewSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT_SOFT);
	out["normalChewSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_ZOMBIE_EAT2);
	out["jackLoopSoundPlaying"] =
		AudioSystem::IsLoopingSoundPlaying(ResourceKeys::Sounds::SOUND_JACKINTHEBOX);
	out["diggerLoopSoundPlaying"] =
		AudioSystem::IsLoopingSoundPlaying(ResourceKeys::Sounds::SOUND_DIGGER_ZOMBIE);
	out["diggerRiseSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_DIRT_RISE);
	out["diggerWakeupSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_WAKEUP);
	out["coffeeSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_COFFEE);
	out["plantWakeupSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_WAKEUP);
	out["sleepIndicatorResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_SLEEPING) },
		{ "textureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_Z, false) != nullptr },
	};
	out["coffeeBeanResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_COFFEEBEAN) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_COFFEEBEAN, false) != nullptr },
		{ "soundLoaded", ResourceManager::GetInstance().GetSound(
			ResourceKeys::Sounds::SOUND_COFFEE) != nullptr },
	};
	out["imitaterResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_IMITATER) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_IMITATER, false) != nullptr },
		{ "chooserFrameLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_SEEDCHOOSER_IMITATERADDON, false) != nullptr },
		{ "particlePartsLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERCLOUDS, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_4, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_5, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_6, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_IMITATERPUFFS_PART_7, false) != nullptr },
	};
	out["garlicYuckSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_YUCK)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_YUCK2);
	out["garlicResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GARLIC) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_GARLIC, false) != nullptr },
		{ "damageBody2Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_GARLIC_BODY2, false) != nullptr },
		{ "damageBody3Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_GARLIC_BODY3, false) != nullptr },
		{ "grossoutFaceLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEAD_GROSSOUT, false) != nullptr },
		{ "yuckSoundsLoaded", ResourceManager::GetInstance().GetSound(
			ResourceKeys::Sounds::SOUND_YUCK) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_YUCK2) != nullptr },
	};
	out["umbrellaSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(
			ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2);
	out["umbrellaBoingSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BOING);
	out["umbrellaSplatSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(
			ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1);
	out["umbrellaResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_UMBRELLALEAF) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_UMBRELLALEAF, false) != nullptr },
		{ "partsLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_UMBRELLALEAF_BLINK1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_UMBRELLALEAF_BLINK2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_BODY, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_LEAF1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_LEAF2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_LEAF3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_LEAF4, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_LEAF5, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_LEAF6, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_UMBRELLALEAF_LEAF7, false) != nullptr },
		{ "soundsLoaded",
			ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_BOING) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_PEABULLET_HIT_BODY1) != nullptr },
		{ "basketballTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_CATAPULT_BASKETBALL,
			false) != nullptr },
	};
	out["marigoldResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_MARIGOLD) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_MARIGOLD, false) != nullptr },
		{ "partsLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_MARIGOLD_BLINK1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_MARIGOLD_BLINK2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MARIGOLD_EYEBROW1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MARIGOLD_EYEBROW2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MARIGOLD_HEAD, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MARIGOLD_MOUTH, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MARIGOLD_PETALS, false) != nullptr },
	};
	out["pogoSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_POGO_ZOMBIE);
	out["bungeeScreamSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM2)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM3);
	out["bungeeGrassstepSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_GRASSSTEP);
	out["magnetSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_MAGNETSHROOM);
	out["magnetResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_MAGNETSHROOM) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_MAGNETSHROOM, false) != nullptr },
		{ "soundLoaded", ResourceManager::GetInstance().GetSound(
			ResourceKeys::Sounds::SOUND_MAGNETSHROOM) != nullptr },
		{ "bucketLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_BUCKET1, false) != nullptr },
		{ "fastBucketLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_FASTZOMBIE_BUCKET1, false) != nullptr },
		{ "footballHelmetLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_FOOTBALL_HELMET, false) != nullptr },
		{ "pinkFootballHelmetLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_PINKFOOTBALL_HELMET, false) != nullptr },
		{ "doorLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_SCREENDOOR1, false) != nullptr },
		{ "reinforcedDoorLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_REINFORCED_SCREENDOOR1, false) != nullptr },
		{ "pogoLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICK, false) != nullptr },
		{ "jackBoxLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_JACKBOX_BOX, false) != nullptr },
		{ "eliteJackBoxLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ELITEJACKBOX_BOX, false) != nullptr },
		{ "diggerPickaxeLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_PICKAXE, false) != nullptr },
	};
	out["goldMagnetResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GOLDMAGNET) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_GOLDMAGNET, false) != nullptr },
		{ "stemLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_GOLDMAGNET_STEM", false) != nullptr },
		{ "headLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_GOLDMAGNET_HEAD1", false) != nullptr },
	};
	out["groundingShroomResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GROUNDINGSHROOM) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_GROUNDINGSHROOM, false) != nullptr },
		{ "idlePoseLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_GROUNDINGSHROOM_IDLE", false) != nullptr },
		{ "shockPoseLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_GROUNDINGSHROOM_SHOCK", false) != nullptr },
	};
	out["bobsledTeamResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_BOBSLED_TEAM_ZOMBIE) },
		{ "insideLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED_INSIDE, false) != nullptr },
		{ "vehicleTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED4, false) != nullptr },
		{ "brokenArmLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED_OUTERARM_UPPER2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_BOBSLED_OUTERARM_HAND, false) != nullptr },
		{ "headParticleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_BOBSLEDHEAD, false) != nullptr },
		{ "impactParticleTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				"PARTICLE_SNOWPEA_SPLATS_PART_0", false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				"PARTICLE_SNOWPEA_PARTICLES_PART_0", false) != nullptr },
	};
	const Vector bobsledOffset = GameDataManager::GetInstance().GetZombieOffset(
		ZombieType::ZOMBIE_BOBSLED_TEAM);
	out["bobsledTeamGameData"] = {
		{ "weight", GameDataManager::GetInstance().GetZombieWeight(
			ZombieType::ZOMBIE_BOBSLED_TEAM) },
		{ "appearWave", GameDataManager::GetInstance().GetZombieAppearWave(
			ZombieType::ZOMBIE_BOBSLED_TEAM) },
		{ "survivalRound", GameDataManager::GetInstance().GetZombieSurvivalRound(
			ZombieType::ZOMBIE_BOBSLED_TEAM) },
		{ "offsetXInt", static_cast<int>(std::lround(bobsledOffset.x)) },
		{ "offsetYInt", static_cast<int>(std::lround(bobsledOffset.y)) },
	};
	out["iceWallEngineerResources"] = {
		{ "coneReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_CONE_ZOMBIE) },
		{ "bodyTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_BODY, false) != nullptr },
		{ "toolStrapTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_TOOLSTRAP, false) != nullptr },
		{ "hatTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_HAT1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_HAT2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICEWALL_ENGINEER_HAT3, false) != nullptr },
		{ "wallTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ICE_WALL, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ICE_WALL_CRACKED1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ICE_WALL_CRACKED2, false) != nullptr },
		{ "particleTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			"PARTICLE_SNOWPEA_PARTICLES_PART_0", false) != nullptr },
	};
	const Vector iceWallEngineerOffset = GameDataManager::GetInstance().GetZombieOffset(
		ZombieType::ZOMBIE_ICE_WALL_ENGINEER);
	out["iceWallEngineerGameData"] = {
		{ "weight", GameDataManager::GetInstance().GetZombieWeight(
			ZombieType::ZOMBIE_ICE_WALL_ENGINEER) },
		{ "appearWave", GameDataManager::GetInstance().GetZombieAppearWave(
			ZombieType::ZOMBIE_ICE_WALL_ENGINEER) },
		{ "survivalRound", GameDataManager::GetInstance().GetZombieSurvivalRound(
			ZombieType::ZOMBIE_ICE_WALL_ENGINEER) },
		{ "offsetXInt", static_cast<int>(std::lround(iceWallEngineerOffset.x)) },
		{ "offsetYInt", static_cast<int>(std::lround(iceWallEngineerOffset.y)) },
	};
	out["iceCrackDrillResources"] = {
		{ "rigReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ICE_CRACK_DRILL_RIG) },
		{ "helmetTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICECRACK_DRILL_HELMET1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICECRACK_DRILL_HELMET2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICECRACK_DRILL_HELMET3, false) != nullptr },
		{ "rigTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				"IMAGE_REANIM_ZOMBIE_ICECRACK_DRILL_RIG1_SPIN0", false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICECRACK_DRILL_RIG3_SPIN0, false) != nullptr },
		{ "riftTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ICECRACKDRILLRIFT, false) != nullptr },
		{ "particleTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				"PARTICLE_ICECRACKDRILLSHARDS_PART_0", false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				"PARTICLE_ICECRACKDRILLSHARDS_PART_3", false) != nullptr },
	};
	const Vector iceCrackDrillOffset = GameDataManager::GetInstance().GetZombieOffset(
		ZombieType::ZOMBIE_ICE_CRACK_DRILL);
	out["iceCrackDrillGameData"] = {
		{ "weight", GameDataManager::GetInstance().GetZombieWeight(
			ZombieType::ZOMBIE_ICE_CRACK_DRILL) },
		{ "appearWave", GameDataManager::GetInstance().GetZombieAppearWave(
			ZombieType::ZOMBIE_ICE_CRACK_DRILL) },
		{ "survivalRound", GameDataManager::GetInstance().GetZombieSurvivalRound(
			ZombieType::ZOMBIE_ICE_CRACK_DRILL) },
		{ "offsetXInt", static_cast<int>(std::lround(iceCrackDrillOffset.x)) },
		{ "offsetYInt", static_cast<int>(std::lround(iceCrackDrillOffset.y)) },
	};
	out["iceCrackDrillChargeSoundRequestCount"] = AudioSystem::GetSoundPlayRequestCount(
		ResourceKeys::Sounds::SOUND_ZAMBONI);
	out["iceCrackDrillRiftSoundRequestCount"] = AudioSystem::GetSoundPlayRequestCount(
		ResourceKeys::Sounds::SOUND_DIRT_RISE);
	out["iceCrackDrillRigDropSoundRequestCount"] = AudioSystem::GetSoundPlayRequestCount(
		ResourceKeys::Sounds::SOUND_ARM_HEAD_DROP);
	out["excavatorResourcesReady"] = ResourceManager::GetInstance().HasReanimation("ExcavatorZombie")
		&& ResourceManager::GetInstance().GetTexture("IMAGE_EXCAVATOR_HAT",false)
		&& ResourceManager::GetInstance().GetTexture("IMAGE_EXCAVATOR_DRILL",false)
		&& ResourceManager::GetInstance().GetTexture("IMAGE_EXCAVATOR_BROKEN",false)
		&& ResourceManager::GetInstance().GetTexture("PARTICLE_EXCAVATORHEAD",false);
	out["weatherJammerResources"] = {
		{ "packReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_WEATHER_JAMMER_PACK) },
		{ "terminalReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_WEATHER_JAMMER_TERMINAL) },
		{ "packTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_PACK_READY,
				false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_PACK_CHANNEL,
				false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_PACK_REBOOT,
				false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_PACK_SPENT,
				false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_DISH,
				false) != nullptr },
		{ "terminalTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_TERMINAL_READY,
				false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_TERMINAL_CHANNEL,
				false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_TERMINAL_REBOOT,
				false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_WEATHER_JAMMER_TERMINAL_SPENT,
				false) != nullptr },
	};
	const Vector weatherJammerOffset = GameDataManager::GetInstance().GetZombieOffset(
		ZombieType::ZOMBIE_WEATHER_JAMMER);
	out["weatherJammerGameData"] = {
		{ "weight", GameDataManager::GetInstance().GetZombieWeight(
			ZombieType::ZOMBIE_WEATHER_JAMMER) },
		{ "appearWave", GameDataManager::GetInstance().GetZombieAppearWave(
			ZombieType::ZOMBIE_WEATHER_JAMMER) },
		{ "survivalRound", GameDataManager::GetInstance().GetZombieSurvivalRound(
			ZombieType::ZOMBIE_WEATHER_JAMMER) },
		{ "offsetXInt", static_cast<int>(std::lround(weatherJammerOffset.x)) },
		{ "offsetYInt", static_cast<int>(std::lround(weatherJammerOffset.y)) },
	};
	out["listeningGrassResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_LISTENINGGRASS) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_LISTENINGGRASS, false) != nullptr },
		{ "partsLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_BODY, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_LEAF1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_LEAF2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_LEAF3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_LEAF4, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_LEAF5, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_LEAF6, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_LISTENINGGRASS_LEAF7, false) != nullptr },
	};
	out["iceStatueExecutionerResources"] = {
		{ "maulTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ICE_EXECUTIONER_MAUL, false) != nullptr },
		{ "iceShellTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ICE_STATUE_SHELL, false) != nullptr },
		{ "executionHelmetTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICE_EXECUTIONER_HELMET, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICE_EXECUTIONER_HELMET2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_ICE_EXECUTIONER_HELMET3, false) != nullptr },
	};
	const Vector iceExecutionerOffset = GameDataManager::GetInstance().GetZombieOffset(
		ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER);
	out["iceStatueExecutionerGameData"] = {
		{ "weight", GameDataManager::GetInstance().GetZombieWeight(
			ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER) },
		{ "appearWave", GameDataManager::GetInstance().GetZombieAppearWave(
			ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER) },
		{ "survivalRound", GameDataManager::GetInstance().GetZombieSurvivalRound(
			ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER) },
		{ "offsetXInt", static_cast<int>(std::lround(iceExecutionerOffset.x)) },
		{ "offsetYInt", static_cast<int>(std::lround(iceExecutionerOffset.y)) },
	};
	out["lightningRodPotResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_LIGHTNINGRODPOT) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_LIGHTNINGRODPOT, false) != nullptr },
		{ "bodyTextureLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_LIGHTNINGRODPOT_BODY", false) != nullptr },
		{ "glowTextureLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_LIGHTNINGRODPOT_GLOW", false) != nullptr },
	};
	out["cobCannonResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_COBCANNON) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_COBCANNON, false) != nullptr },
		{ "cobLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_COBCANNON_COB, false) != nullptr },
		{ "targetLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_COBCANNON_TARGET, false) != nullptr },
		{ "targetShadowLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_COBCANNON_TARGET_SHADOW, false) != nullptr },
		{ "plantShadowLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PLANTSHADOW, false) != nullptr },
		{ "popcornLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_COBCANNON_POPCORN_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_COBCANNON_POPCORN_PART_1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_COBCANNON_POPCORN_PART_2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_COBCANNON_POPCORN_PART_3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_COBCANNON_POPCORN_PART_4, false) != nullptr },
		{ "blastMarkLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_BLASTMARK, false) != nullptr },
		{ "soundsLoaded",
			ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_COBLAUNCH) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_SHOOP) != nullptr },
	};
	out["cobLaunchSoundRequestCount"] = AudioSystem::GetSoundPlayRequestCount(
		ResourceKeys::Sounds::SOUND_COBLAUNCH);
	out["cobChargeSoundRequestCount"] = AudioSystem::GetSoundPlayRequestCount(
		ResourceKeys::Sounds::SOUND_SHOOP);
	// 转换版 reanim 实际引用 13 张分件；目录中的 blink1 未被轨道引用，不计入闭环。
	const std::array<std::string, 13> gloomShroomTextureKeys = {
		"IMAGE_REANIM_GLOOMSHROOM_BASE",
		"IMAGE_REANIM_GLOOMSHROOM_BLINK2",
		"IMAGE_REANIM_GLOOMSHROOM_FACE1",
		"IMAGE_REANIM_GLOOMSHROOM_FACE2",
		"IMAGE_REANIM_GLOOMSHROOM_HEAD",
		"IMAGE_REANIM_GLOOMSHROOM_SHOOTER1",
		"IMAGE_REANIM_GLOOMSHROOM_SHOOTER2",
		"IMAGE_REANIM_GLOOMSHROOM_SHOOTER3",
		"IMAGE_REANIM_GLOOMSHROOM_SHOOTER4",
		"IMAGE_REANIM_GLOOMSHROOM_SHOOTER5",
		"IMAGE_REANIM_GLOOMSHROOM_STEM1",
		"IMAGE_REANIM_GLOOMSHROOM_STEM2",
		"IMAGE_REANIM_GLOOMSHROOM_STEM3",
	};
	const int gloomShroomTexturePartsLoaded = static_cast<int>(std::count_if(
		gloomShroomTextureKeys.begin(), gloomShroomTextureKeys.end(),
		[](const std::string& key) {
			return ResourceManager::GetInstance().GetTexture(key, false) != nullptr;
		}));
	out["gloomShroomResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GLOOMSHROOM) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_GLOOMSHROOM, false) != nullptr },
		{ "upgradeCardBackgroundLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_SEEDPACKETVARIANTS, false) != nullptr },
		{ "referencedTexturePartsLoaded", gloomShroomTexturePartsLoaded },
		{ "particleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_PUFFSHROOM_PUFF1", false) != nullptr },
	};
	const std::array<std::string, 11> twinSunflowerTextureKeys = {
		"IMAGE_REANIM_PEASHOOTER_BACKLEAF",
		"IMAGE_REANIM_PEASHOOTER_BACKLEAF_LEFTTIP",
		"IMAGE_REANIM_PEASHOOTER_BACKLEAF_RIGHTTIP",
		"IMAGE_REANIM_PEASHOOTER_FRONTLEAF",
		"IMAGE_REANIM_PEASHOOTER_FRONTLEAF_LEFTTIP",
		"IMAGE_REANIM_PEASHOOTER_FRONTLEAF_RIGHTTIP",
		"IMAGE_REANIM_SUNFLOWER_DOUBLE_PETALS",
		"IMAGE_REANIM_SUNFLOWER_HEAD",
		ResourceKeys::Textures::IMAGE_REANIM_TWINSUNFLOWER_LEAF,
		ResourceKeys::Textures::IMAGE_REANIM_TWINSUNFLOWER_STEM1,
		ResourceKeys::Textures::IMAGE_REANIM_TWINSUNFLOWER_STEM2,
	};
	const int twinSunflowerTexturePartsLoaded = static_cast<int>(std::count_if(
		twinSunflowerTextureKeys.begin(), twinSunflowerTextureKeys.end(),
		[](const std::string& key) {
			return ResourceManager::GetInstance().GetTexture(key, false) != nullptr;
		}));
	out["twinSunflowerResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_TWINSUNFLOWER) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_TWINSUNFLOWER, false) != nullptr },
		{ "upgradeCardBackgroundLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_SEEDPACKETVARIANTS, false) != nullptr },
		{ "referencedTexturePartsLoaded", twinSunflowerTexturePartsLoaded },
	};
	// 命中配方随机取七个分片；全部加载才能排除“偶尔抽到空纹理”的假绿。
	const std::array<std::string, 7> toxicPeaHitTextureKeys = {
		ResourceKeys::Particles::PARTICLE_TOXICPEA_SPLATS_PART_0,
		ResourceKeys::Particles::PARTICLE_TOXICPEA_SPLATS_PART_1,
		ResourceKeys::Particles::PARTICLE_TOXICPEA_SPLATS_PART_2,
		ResourceKeys::Particles::PARTICLE_TOXICPEA_SPLATS_PART_3,
		ResourceKeys::Particles::PARTICLE_TOXICPEA_PARTICLES_PART_0,
		ResourceKeys::Particles::PARTICLE_TOXICPEA_PARTICLES_PART_1,
		ResourceKeys::Particles::PARTICLE_TOXICPEA_PARTICLES_PART_2,
	};
	const int toxicPeaHitTexturePartsLoaded = static_cast<int>(std::count_if(
		toxicPeaHitTextureKeys.begin(), toxicPeaHitTextureKeys.end(),
		[](const std::string& key) {
			return ResourceManager::GetInstance().GetTexture(key, false) != nullptr;
		}));
	out["toxicPeaResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_TOXICPEASHOOTER) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_TOXICPEASHOOTER, false) != nullptr },
		{ "projectileLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PROJECTILETOXICPEA, false) != nullptr },
		{ "headLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_TOXICPEASHOOTER_HEAD, false) != nullptr },
		{ "tailLeafLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_TOXICPEASHOOTER_SPROUT, false) != nullptr },
		{ "hitTexturePartsLoaded", toxicPeaHitTexturePartsLoaded },
	};
	out["pogoResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_POGO_ZOMBIE) },
		{ "brokenArmLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_OUTERARM_UPPER2, false) != nullptr },
		{ "damagedHandsLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICKHANDS2, false) != nullptr },
		{ "damagedStickLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICKDAMAGE2, false) != nullptr },
		{ "damagedStick2Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_POGO_STICK2DAMAGE2, false) != nullptr },
		{ "headParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_POGOHEAD, false) != nullptr },
		{ "pogoParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIEPOGO_PART_2, false) != nullptr },
	};
	out["elitePogoResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ELITE_POGO_ZOMBIE) },
		{ "brokenArmLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_POGO_OUTERARM_UPPER2, false) != nullptr },
		{ "damagedStickLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_POGO_STICKDAMAGE2, false) != nullptr },
		{ "damagedStick2Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_POGO_STICK2DAMAGE2, false) != nullptr },
		{ "pogoParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIEELITEPOGO_PART_2, false) != nullptr },
	};
	out["bungeeResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_BUNGEE_ZOMBIE) },
		{ "cordLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_BUNGEECORD, false) != nullptr },
		{ "targetLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_BUNGEETARGET, false) != nullptr },
		{ "soundsLoaded",
			ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_GRASSSTEP) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM2) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_BUNGEE_SCREAM3) != nullptr },
	};
	out["ladderSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_LADDER_ZOMBIE);
	out["ladderResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_LADDER_ZOMBIE) },
		{ "baseLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_1, false) != nullptr },
		{ "damage1Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_1_DAMAGE1, false) != nullptr },
		{ "damage2Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_1_DAMAGE2, false) != nullptr },
		{ "placedLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_5, false) != nullptr },
		{ "brokenArmLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_LADDER_OUTERARM_UPPER2, false) != nullptr },
		{ "headParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_LADDERHEAD, false) != nullptr },
		{ "soundLoaded", ResourceManager::GetInstance().GetSound(
			ResourceKeys::Sounds::SOUND_LADDER_ZOMBIE) != nullptr },
	};
	out["eliteLadderResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ELITE_LADDER_ZOMBIE) },
		{ "bodyLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_BODY, false) != nullptr },
		{ "body2Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_BODY2, false) != nullptr },
		{ "baseLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_1, false) != nullptr },
		{ "damage1Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_1_DAMAGE1, false) != nullptr },
		{ "damage2Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_1_DAMAGE2, false) != nullptr },
		{ "placedLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_5, false) != nullptr },
		{ "brokenArmLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_LADDER_OUTERARM_UPPER2, false) != nullptr },
	};
	out["pumpkinResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_PUMPKIN) },
		{ "frontLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PUMPKIN_FRONT, false) != nullptr },
		{ "backLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PUMPKIN_BACK, false) != nullptr },
		{ "damage1Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PUMPKIN_DAMAGE1, false) != nullptr },
		{ "damage3Loaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PUMPKIN_DAMAGE3, false) != nullptr },
		{ "softChewLoaded", ResourceManager::GetInstance().GetSound(
			ResourceKeys::Sounds::SOUND_ZOMBIE_EAT_SOFT) != nullptr },
	};
	out["diggerResources"] = {
		{ "damagedHardhatLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_HARDHAT2, false) != nullptr },
		{ "criticalHardhatLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_HARDHAT3, false) != nullptr },
		{ "brokenArmLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_DIGGER_OUTERARM_UPPER2, false) != nullptr },
	};
	// 精英矿工的受损帽与断臂是运行时换图资源；显式导出加载状态，防止键名或清单遗漏只留下 WARN。
	out["eliteDiggerResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ELITE_DIGGER_ZOMBIE) },
		{ "damagedHardhatLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITEDIGGER_HARDHAT2, false) != nullptr },
		{ "criticalHardhatLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITEDIGGER_HARDHAT3, false) != nullptr },
		{ "armParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_ELITEDIGGERARM, false) != nullptr },
	};
	out["jackBoingSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BOING);
	out["jackSurprise1SoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_JACK_SURPRISE);
	out["jackSurprise2SoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_JACK_SURPRISE2);
	out["jackSurpriseSoundRequestCount"] =
		out["jackSurprise1SoundRequestCount"].get<int>()
		+ out["jackSurprise2SoundRequestCount"].get<int>();
	out["jackExplosionSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_EXPLOSION);
	out["limbsPopSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_LIMBS_POP);
	out["balloonInflateSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BALLOONINFLATE);
	out["balloonPopSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BALLOON_POP);
	out["plantGrowSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_PLANTGROW);
	out["bloverSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BLOVER);
	out["clickFailedSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_CLICKFAILED);
	out["shooterShootSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_SHOOTER_SHOOT2);
	out["healerCastSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BUTTONCLICK);
	out["healerAreaSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_COLLECTSUN);
	out["healerFocusedSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_CHOOSEPLANT1);
	out["healerResources"] = {
		{ "baseReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_NORMAL_ZOMBIE) },
		{ "bodyLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_BODY, false) != nullptr },
		{ "idleGearLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_IDLE, false) != nullptr },
		{ "areaGearLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_AREA, false) != nullptr },
		{ "focusedGearLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_FOCUSED, false) != nullptr },
		{ "disabledGearLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_HEALER_GEAR_DISABLED, false) != nullptr },
		{ "plusParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_HEALERPLUS, false) != nullptr },
		{ "haloParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_HEALERHALO, false) != nullptr },
		{ "soundsLoaded",
			ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_BUTTONCLICK) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_COLLECTSUN) != nullptr
			&& ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_CHOOSEPLANT1) != nullptr },
	};
	const Vector healerOffset = GameDataManager::GetInstance().GetZombieOffset(
		ZombieType::ZOMBIE_HEALER);
	out["healerGameData"] = {
		{ "weight", GameDataManager::GetInstance().GetZombieWeight(
			ZombieType::ZOMBIE_HEALER) },
		{ "appearWave", GameDataManager::GetInstance().GetZombieAppearWave(
			ZombieType::ZOMBIE_HEALER) },
		{ "survivalRound", GameDataManager::GetInstance().GetZombieSurvivalRound(
			ZombieType::ZOMBIE_HEALER) },
		{ "offsetXInt", static_cast<int>(std::lround(healerOffset.x)) },
		{ "offsetYInt", static_cast<int>(std::lround(healerOffset.y)) },
	};
	out["groundingZombieResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GROUNDING_ZOMBIE) },
		{ "coneTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_GROUNDING_CONE1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GROUNDING_CONE2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GROUNDING_CONE3, false) != nullptr },
		{ "reanimConeTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_GROUNDING_CONE1, false) != nullptr },
	};
	const Vector groundingZombieOffset = GameDataManager::GetInstance().GetZombieOffset(
		ZombieType::ZOMBIE_GROUNDING);
	out["groundingZombieGameData"] = {
		{ "weight", GameDataManager::GetInstance().GetZombieWeight(
			ZombieType::ZOMBIE_GROUNDING) },
		{ "appearWave", GameDataManager::GetInstance().GetZombieAppearWave(
			ZombieType::ZOMBIE_GROUNDING) },
		{ "survivalRound", GameDataManager::GetInstance().GetZombieSurvivalRound(
			ZombieType::ZOMBIE_GROUNDING) },
		{ "offsetXInt", static_cast<int>(std::lround(groundingZombieOffset.x)) },
		{ "offsetYInt", static_cast<int>(std::lround(groundingZombieOffset.y)) },
	};

	// 主菜单没有 Board，但仍允许测试读取上方 GameAPP 级设置，覆盖真实按钮切换路径。
	if (auto* mainMenuScene = dynamic_cast<MainMenuScene*>(currentScene)) {
		const std::string tooltipText = mainMenuScene->GetConsoleTooltipText();
		const Vector tooltipPosition = mainMenuScene->GetConsoleTooltipPosition();
		out["consoleTooltipVisible"] = !tooltipText.empty();
		out["consoleTooltipText"] = tooltipText;
		out["consoleTooltipX"] = static_cast<int>(std::lround(tooltipPosition.x));
		out["consoleTooltipY"] = static_cast<int>(std::lround(tooltipPosition.y));
		return true;
	}

	if (auto* selectScene = dynamic_cast<GameSelectScene*>(currentScene)) {
		out["gameSelectMode"] = selectScene->GetSelectMode()
			== GameSelectScene::SelectMode::ADVENTURE ? "ADVENTURE"
			: (selectScene->GetSelectMode() == GameSelectScene::SelectMode::MINIGAMES ? "MINIGAMES" : "SURVIVAL");
		out["gameSelectAvailableLevels"] = selectScene->GetAvailableLevels();
		out["gameSelectAvailableLevelCount"] = static_cast<int>(
			selectScene->GetAvailableLevels().size());
		out["gameSelectPageIndex"] = selectScene->GetCurrentPage();
		out["gameSelectPageNumber"] = selectScene->GetCurrentPage() + 1;
		out["gameSelectPageCount"] = selectScene->GetPageCount();
		out["gameSelectVisibleLevels"] = selectScene->GetCurrentPageLevels();
		out["gameSelectCreatedCardCount"] = static_cast<int>(
			selectScene->GetCurrentPageLevels().size());
		out["gameSelectVisibleCompleted"] = nlohmann::json::array();
		for (int level : selectScene->GetCurrentPageLevels()) {
			out["gameSelectVisibleCompleted"].push_back(
				selectScene->IsAdventureLevelCompleted(level));
		}
		out["gameSelectTrophyTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_MINIGAME_TROPHY, false) != nullptr;
		out["gameSelectSurvivalUnlockAreas"] = nlohmann::json::array();
		for (const auto& definition : SURVIVAL_ENDLESS_DEFINITIONS) {
			out["gameSelectSurvivalUnlockAreas"].push_back(
				definition.requiredAdventureArea);
		}

		auto exportPageButton = [](const std::shared_ptr<Button>& button) {
			nlohmann::json state = nullptr;
			if (!button) return state;
			const Vector center = button->GetCenter();
			state = {
				{ "enabled", button->IsEnabled() },
				{ "visible", !button->IsSkipDraw() },
				{ "rotationDegrees", static_cast<int>(std::lround(
					button->GetImageRotationDegrees())) },
				{ "centerXInt", static_cast<int>(std::lround(center.x)) },
				{ "centerYInt", static_cast<int>(std::lround(center.y)) },
			};
			return state;
		};
		out["gameSelectPreviousPageButton"] = exportPageButton(
			selectScene->GetPreviousPageButton());
		out["gameSelectNextPageButton"] = exportPageButton(
			selectScene->GetNextPageButton());
		out["gameSelectSkipLevelButton"] = exportPageButton(selectScene->GetSkipLevelButton());
		out["gameSelectSkippableLevel"] = selectScene->GetSkippableLevel();
		return true;
	}

	if (auto* almanac = dynamic_cast<ZombieAlmanacScene*>(currentScene)) {
		out["zombieAlmanacEntries"] = nlohmann::json::array();
		for (ZombieType type : almanac->GetDisplayedZombieTypes()) {
			out["zombieAlmanacEntries"].push_back(ZombieTypeName(type));
		}
		out["zombieAlmanacEntryCount"] =
			static_cast<int>(almanac->GetDisplayedZombieTypes().size());
		out["zombieAlmanacPageIndex"] = almanac->GetCurrentPage();
		out["zombieAlmanacPageNumber"] = almanac->GetCurrentPage() + 1;
		out["zombieAlmanacPageCount"] = almanac->GetPageCount();
		out["zombieAlmanacVisibleEntries"] = nlohmann::json::array();
		for (ZombieType type : almanac->GetCurrentPageZombieTypes()) {
			out["zombieAlmanacVisibleEntries"].push_back(ZombieTypeName(type));
		}
		out["zombieAlmanacVisibleEntryCount"] = static_cast<int>(
			out["zombieAlmanacVisibleEntries"].size());
		out["zombieAlmanacGridPreviews"] = nlohmann::json::array();
		for (ZombieType type : almanac->GetCurrentPageZombieTypes()) {
			nlohmann::json gridState = {
				{ "type", ZombieTypeName(type) },
				{ "instanceReady", false },
			};
			if (Zombie* gridPreview = almanac->GetGridZombiePreview(type)) {
				const auto gridAnim = gridPreview->GetAnimatorInternal();
				gridState["instanceReady"] = true;
				gridState["track"] = gridPreview->GetCurrentTrackName();
				gridState["animPlaying"] = gridAnim && gridAnim->IsPlaying();
			}
			out["zombieAlmanacGridPreviews"].push_back(std::move(gridState));
		}
		if (auto button = almanac->GetPreviousPageButton()) {
			const Vector buttonCenter = button->GetCenter();
			out["zombieAlmanacPreviousPageButton"] = {
				{ "enabled", button->IsEnabled() },
				{ "visible", !button->IsSkipDraw() },
				{ "textureLoaded", ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN, false) != nullptr },
				{ "rotationDegrees", static_cast<int>(std::lround(
					button->GetImageRotationDegrees())) },
				{ "centerXInt", static_cast<int>(std::lround(buttonCenter.x)) },
				{ "centerYInt", static_cast<int>(std::lround(buttonCenter.y)) },
			};
		}
		if (auto button = almanac->GetNextPageButton()) {
			const Vector buttonCenter = button->GetCenter();
			out["zombieAlmanacNextPageButton"] = {
				{ "enabled", button->IsEnabled() },
				{ "visible", !button->IsSkipDraw() },
				{ "textureLoaded", ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN, false) != nullptr },
				{ "rotationDegrees", static_cast<int>(std::lround(
					button->GetImageRotationDegrees())) },
				{ "centerXInt", static_cast<int>(std::lround(buttonCenter.x)) },
				{ "centerYInt", static_cast<int>(std::lround(buttonCenter.y)) },
			};
		}
		const ZombieType selected = almanac->GetCurrentZombieType();
		out["zombieAlmanacSelected"] =
			selected == ZombieType::NUM_ZOMBIE_TYPES
			? nlohmann::json(nullptr)
			: nlohmann::json(ZombieTypeName(selected));
		out["zombieAlmanacInfo"] = {
			{ "name", almanac->GetCurrentZombieName() },
			{ "descriptionLineCount", static_cast<int>(
				almanac->GetDescriptionLineCount()) },
		};
		out["zombieAlmanacPreview"] = nullptr;
		if (Zombie* preview = almanac->GetPreviewZombie()) {
			const auto anim = preview->GetAnimatorInternal();
			nlohmann::json previewState = {
				{ "type", ZombieTypeName(preview->mZombieType) },
				{ "track", preview->GetCurrentTrackName() },
				{ "animPlaying", anim && anim->IsPlaying() },
				{ "isPreview", preview->IsPreview() },
			};
			if (auto* pogo = dynamic_cast<PogoZombie*>(preview)) {
				previewState["pogoPreviewBounceActive"] = pogo->IsPreviewBounceActive();
				previewState["pogoAltitudeOn1000"] = static_cast<int>(std::lround(
					pogo->GetPogoAltitude() * 1000.0f));
			}
			if (auto* healer = dynamic_cast<HealerZombie*>(preview)) {
				previewState["healerTreatmentState"] =
					HealerTreatmentStateName(healer->GetTreatmentState());
				previewState["healerCooldownMs"] = static_cast<int>(std::lround(
					healer->GetHealCooldownRemaining() * 1000.0f));
				previewState["healerDisabled"] =
					healer->IsHealingPermanentlyDisabled();
				previewState["healerGearTextureKey"] =
					healer->GetTreatmentGearTextureKey();
				previewState["healerGearVisible"] = healer->IsTreatmentGearVisible();
			}
			if (auto* bobsled = dynamic_cast<BobsledTeamZombie*>(preview)) {
				previewState["bobsledRole"] = bobsled->GetBobsledRole()
					== BobsledTeamZombie::Role::LEADER ? "LEADER" : "FOLLOWER";
				previewState["bobsledPhase"] = bobsled->GetBobsledPhase()
					== BobsledTeamZombie::Phase::RIDING ? "RIDING" : "WALKING";
				previewState["bobsledSlot"] = bobsled->GetBobsledSlot();
				int memberCount = 0;
				int slotMask = 0;
				float minX = std::numeric_limits<float>::max();
				float maxX = std::numeric_limits<float>::lowest();
				for (const auto& weakMember : almanac->GetPreviewZombieMembers()) {
					const auto member = weakMember.lock();
					auto* rider = dynamic_cast<BobsledTeamZombie*>(member.get());
					if (!rider || !rider->IsActive()) continue;
					const float x = rider->GetPosition().x;
					++memberCount;
					slotMask |= 1 << rider->GetBobsledSlot();
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
				}
				previewState["bobsledMemberCount"] = memberCount;
				previewState["bobsledSlotMask"] = slotMask;
				previewState["bobsledXSpanOn1000"] = memberCount > 0
					? static_cast<int>(std::lround((maxX - minX) * 1000.0f)) : 0;
			}
			if (auto* drill = dynamic_cast<IceCrackDrillZombie*>(preview)) {
				previewState["drillRigAnimatorReady"] = drill->HasDrillRigAnimator();
				previewState["drillRigVisible"] = drill->IsDrillRigVisible();
			}
			out["zombieAlmanacPreview"] = std::move(previewState);
		}
		return true;
	}

	if (auto* almanac = dynamic_cast<PlantAlmanacScene*>(currentScene)) {
		out["plantAlmanacReward"] = almanac->IsReward();
		out["plantAlmanacSelected"] = GameDataManager::GetInstance().PlantTypeToEnumName(almanac->GetSelectedPlant());
		out["plantAlmanacName"] = almanac->GetPlantName();
		out["plantAlmanacDescriptionLineCount"] = almanac->GetDescriptionLineCount();
		out["plantAlmanacPreviewReady"] = almanac->HasPreviewPlant();
		out["almanacReturnLevel"] = gameApp.mGameInfoSaver.GetAlmanacReturnLevel();
		return true;
	}
	if (currentScene->name == "AlmanacScene") {
		out["almanacReturnLevel"] = gameApp.mGameInfoSaver.GetAlmanacReturnLevel();
		return true;
	}

	GameScene* gs = dynamic_cast<GameScene*>(currentScene);
	if (!gs || !gs->GetBoard()) {
		Fail(opName + ": 当前 Scene 不支持状态导出 (" + currentScene->name + ")");
		return false;
	}
	Board* board = gs->GetBoard();
	out["crazyDave"] = {
		{ "supported", gs->IsCrazyDaveDialogSupported() },
		{ "active", gs->IsCrazyDaveDialogActive() },
		{ "phase", gs->GetCrazyDaveDialogPhaseName() },
		{ "level", board->mLevel },
		{ "messageIndex", gs->GetCrazyDaveDialogMessageIndex() },
		{ "messageNumber", gs->IsCrazyDaveDialogActive()
			? gs->GetCrazyDaveDialogMessageIndex() + 1 : 0 },
		{ "messageCount", gs->GetCrazyDaveDialogMessageCount() },
		{ "text", gs->GetCrazyDaveDialogText() },
		{ "track", gs->GetCrazyDaveDialogTrackName() },
		{ "voiceSoundKey", gs->GetCrazyDaveDialogVoiceSoundKey() },
		{ "voiceGroup", gs->GetCrazyDaveDialogVoiceGroupName() },
		{ "voicePlayRequestCount", CrazyDaveDialog::GetVoicePlayRequestCount() },
		{ "seen", gameApp.HasSeenCrazyDaveTutorial(board->mLevel) },
		{ "renderQuadCount", gs->GetCrazyDaveDialogRenderedQuadCount() },
		{ "renderedGeometry", gs->HasCrazyDaveDialogRenderedGeometry() },
		{ "usedInstancePath", gs->DidCrazyDaveDialogUseInstancePath() },
	};

	out["boardState"] = BoardStateName(board->mBoardState);
	out["cobCannonTargeting"] = board->IsCobCannonTargeting();
	out["targetingCobCannonID"] = board->GetTargetingCobCannonID();
	out["chooseCardReady"] = gs->IsChooseCardReady();
	out["chooseCardSelectedCards"] = nlohmann::json::array();
	out["chooseCardSelectedCardKeys"] = nlohmann::json::array();
	out["chooseCardSelectedCount"] = 0;
	out["chooseCardSelectedMovingCount"] = 0;
	out["restoreLastSelectionEnabled"] = false;
	out["chooseCardPageIndex"] = 0;
	out["chooseCardPageNumber"] = 0;
	out["chooseCardPageCount"] = 0;
	out["chooseCardVisibleCards"] = nlohmann::json::array();
	out["chooseCardHiddenCards"] = nlohmann::json::array();
	out["chooseCardVisibleCardCount"] = 0;
	out["chooseCardHiddenCardCount"] = 0;
	out["chooseCardImitaterDialogOpen"] = false;
	out["chooseCardImitaterDialogOptions"] = nlohmann::json::array();
	out["chooseCardImitaterDialogOptionCount"] = 0;
	out["chooseCardImitaterDialogHasUpgradeOption"] = false;
	out["chooseCardImitaterSidebarVisible"] = false;
	out["chooseCardImitaterTarget"] = nullptr;
	if (ChooseCardUI* chooseUI = gs->GetChooseCardUI()) {
		for (Card* card : chooseUI->GetSelectedCards()) {
			if (!card) continue;
			out["chooseCardSelectedCards"].push_back(
				PlantTypeName(card->GetPlantType()));
			if (card->IsMoving()) {
				out["chooseCardSelectedMovingCount"] =
					out["chooseCardSelectedMovingCount"].get<int>() + 1;
			}
		}
		out["chooseCardSelectedCount"] = static_cast<int>(
			out["chooseCardSelectedCards"].size());
		for (const std::string& key : chooseUI->GetSelectedCardKeys()) {
			out["chooseCardSelectedCardKeys"].push_back(key);
		}
		out["chooseCardImitaterDialogOpen"] = chooseUI->IsImitaterDialogOpen();
		out["imitaterPagination"] = {
			{ "pageIndex", chooseUI->GetImitaterDialogPage() },
			{ "pageCount", chooseUI->GetImitaterDialogPageCount() },
			{ "visibleOptions", nlohmann::json::array() }
		};
		auto& pagination = out["imitaterPagination"];
		for (PlantType type : chooseUI->GetVisibleImitaterDialogOptionTypes()) {
			pagination["visibleOptions"].push_back(PlantTypeName(type));
		}
		pagination["visibleCount"] = pagination["visibleOptions"].size();
		for (const bool previous : { true, false }) {
			auto button = previous ? chooseUI->GetImitaterPreviousPageButton()
				: chooseUI->GetImitaterNextPageButton();
			if (!button) continue;
			pagination[previous ? "previous" : "next"] = {
				{ "visible", !button->IsSkipDraw() },
				{ "enabled", button->IsEnabled() },
				{ "textureLoaded", ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN, false) != nullptr }
			};
		}
		for (PlantType type : chooseUI->GetImitaterDialogOptionTypes()) {
			out["chooseCardImitaterDialogOptions"].push_back(PlantTypeName(type));
			if (IsUpgradePlantType(type)) {
				out["chooseCardImitaterDialogHasUpgradeOption"] = true;
			}
		}
		out["chooseCardImitaterDialogOptionCount"] = static_cast<int>(
			out["chooseCardImitaterDialogOptions"].size());
		if (Card* imitaterCard = chooseUI->GetImitaterCard()) {
			out["chooseCardImitaterSidebarVisible"] =
				true; // AddOn 是固定背景，目标选定和弹窗期间都不随 Card 消失
			out["chooseCardImitaterEntryCardVisible"] = imitaterCard->IsActive();
			if (imitaterCard->HasImitaterTarget()) {
				out["chooseCardImitaterTarget"] =
					PlantTypeName(imitaterCard->GetImitaterTarget());
			}
			if (Transform* transform = imitaterCard->GetTransform()) {
				out["chooseCardImitaterCard"] = {
					{ "active", imitaterCard->IsActive() },
					{ "xInt", static_cast<int>(std::lround(
						transform->GetPosition().x)) },
					{ "yInt", static_cast<int>(std::lround(
						transform->GetPosition().y)) },
				};
			}
		}
		out["chooseCardPageIndex"] = chooseUI->GetCurrentPage();
		out["chooseCardPageNumber"] = chooseUI->GetCurrentPage() + 1;
		out["chooseCardPageCount"] = chooseUI->GetPageCount();
		for (PlantType type : chooseUI->GetVisibleCardTypes()) {
			out["chooseCardVisibleCards"].push_back(PlantTypeName(type));
		}
		for (PlantType type : chooseUI->GetHiddenCardTypes()) {
			out["chooseCardHiddenCards"].push_back(PlantTypeName(type));
		}
		out["chooseCardVisibleCardCount"] = static_cast<int>(
			out["chooseCardVisibleCards"].size());
		out["chooseCardHiddenCardCount"] = static_cast<int>(
			out["chooseCardHiddenCards"].size());
		if (auto button = chooseUI->GetRestoreButton()) {
			out["restoreLastSelectionEnabled"] = button->IsEnabled();
			const Vector panelPosition = chooseUI->GetPosition();
			const Vector buttonCenter = button->GetCenter();
			out["restoreLastSelectionButton"] = {
				{ "centerXInt", static_cast<int>(std::lround(buttonCenter.x)) },
				{ "centerYInt", static_cast<int>(std::lround(buttonCenter.y)) },
				{ "relativeCenterXInt", static_cast<int>(std::lround(
					buttonCenter.x - panelPosition.x)) },
				{ "relativeCenterYInt", static_cast<int>(std::lround(
					buttonCenter.y - panelPosition.y)) },
			};
		}
		if (auto button = chooseUI->GetPageButton()) {
			const Vector panelPosition = chooseUI->GetPosition();
			const Vector buttonCenter = button->GetCenter();
			out["chooseCardPageButton"] = {
				{ "enabled", button->IsEnabled() },
				{ "visible", !button->IsSkipDraw() },
				{ "textureLoaded", ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZEN_NEXTGARDEN, false) != nullptr },
				{ "rotationDegrees", static_cast<int>(std::lround(
					button->GetImageRotationDegrees())) },
				{ "centerXInt", static_cast<int>(std::lround(buttonCenter.x)) },
				{ "centerYInt", static_cast<int>(std::lround(buttonCenter.y)) },
				{ "relativeCenterXInt", static_cast<int>(std::lround(
					buttonCenter.x - panelPosition.x)) },
				{ "relativeCenterYInt", static_cast<int>(std::lround(
					buttonCenter.y - panelPosition.y)) },
			};
		}
	}
	out["paused"] = DeltaTime::IsPaused();
	out["spacePauseActive"] = gs->IsSpacePauseActiveForTesting();
	out["pauseMenuOpen"] = gs->IsPauseMenuOpenForTesting();
	out["pauseGameplayInputBlocked"] = gs->GetCardSlotManager()
		? !gs->GetCardSlotManager()->CanAcceptGameplayInput() : false;
	out["selectedTimeScaleOn1000"] = static_cast<int>(std::lround(
		DeltaTime::GetSelectedTimeScale() * 1000.0f));
	out["level"] = board->mLevel;
	out["levelName"] = board->mLevelName;
	out["background"] = BackgroundName(board->mBackGround);
	if (board->IsColdStorage()) {
		out["coldStorage"] = board->SaveColdStorage();
		auto& ice = out["coldStorage"];
		ice["pendingCount"] = board->mColdStorage.pending.size();
		ice["hostileCount"] = board->GetColdStorageHostileCount();
		ice["trophySpawned"] = board->mTrophySpawned;
		ice["candidatesEvaluated"] = board->mColdStorage.candidatesEvaluated;
		ice["lastBestScoreOn100"] = static_cast<int>(std::lround(board->mColdStorage.lastBestScore * 100));
		ice["predictedProductionOn100"] = static_cast<int>(std::lround(board->mColdStorage.predictedProduction * 100.0f));
		ice["economyValueOn100"] = static_cast<int>(std::lround(board->mColdStorage.economyValue * 100.0f));
		ice["economyRow"] = board->mColdStorage.economyRow;
		ice["economyRows"] = nlohmann::json::array();
		for (int row = 0; row < board->mRows; ++row) ice["economyRows"].push_back({
			{"netOn100", static_cast<int>(std::lround(board->mColdStorage.economyNetByRow[row] * 100.0f))},
			{"blastLossOn100", static_cast<int>(std::lround(board->mColdStorage.economyBlastLossByRow[row] * 100.0f))},
			{"guardCost", board->mColdStorage.economyGuardCostByRow[row]}});
		ice["unitResourcesReady"] = ResourceManager::GetInstance().HasReanimation("IceMint")
			&& ResourceManager::GetInstance().HasReanimation("IceWorkerZombie")
			&& ResourceManager::GetInstance().GetTexture("IMAGE_ICEMINT", false)
			&& ResourceManager::GetInstance().GetTexture("IMAGE_ICE_MINT_CRYSTAL", false)
			&& ResourceManager::GetInstance().GetTexture("IMAGE_ICE_WORKER_MACHINE", false);
		ice["commanderMode"] = board->mColdStorage.commanderMode;
		ice["commanderBudget"] = board->mColdStorage.commanderBudget;
		ice["commanderSpent"] = board->mColdStorage.commanderSpent;
		ice["commanderReserve"] = board->mColdStorage.commanderReserve;
		ice["commanderFocusRow"] = board->mColdStorage.commanderFocusRow;
		ice["responseWindowMs"] = static_cast<int>(std::lround(board->mColdStorage.responseWindow * 1000));
		ice["resourcesReady"] = ResourceManager::GetInstance().GetTexture("IMAGE_BACKGROUND_HOT_COLD_STORAGE", false)
			&& ResourceManager::GetInstance().GetTexture("IMAGE_COLD_STORAGE_ICE_HEAD", false);
		ice["cellCenters"] = nlohmann::json::array();
		for (int row=0; row<board->mRows; ++row) for (int col=0; col<board->mColumns; ++col) {
			const Vector center=board->GetCellCenterPosition(row,col);
			ice["cellCenters"].push_back({static_cast<int>(center.x),static_cast<int>(center.y)});
		}
		ice["plantCosts"] = nlohmann::json::object();
		for (const auto& entry : kPlantNames) ice["plantCosts"][entry.first]=board->GetPlantIceCost(entry.second);
		ice["unlockRounds"] = nlohmann::json::object();
		for (ZombieType type : board->GetSpawnZombieList()) ice["unlockRounds"][ZombieTypeName(type)] = GameDataManager::GetInstance().GetZombieAppearWave(type);
		ice["zombieCosts"] = nlohmann::json::object();
		for (const auto& entry : kZombieNames) ice["zombieCosts"][entry.first]=board->GetZombieIceCost(entry.second);
	}

	if (board->IsMineBackground()) {
		bool resourcesReady = true;
		for (const char* key : {"IMAGE_BACKGROUND_GLOOMCRYSTAL_MINE", "IMAGE_MINE_ROCK_A", "IMAGE_MINE_ROCK_B",
			"IMAGE_MINE_ROCK_C", "IMAGE_MINE_ROCK_CRACKED", "IMAGE_MINE_RUBBLE", "IMAGE_MINE_CHUNKS", "IMAGE_MINE_PICKAXE", "IMAGE_MINE_ENTRANCE", "IMAGE_MINE_ROUTE_BUTTON"}) {
			resourcesReady = resourcesReady && ResourceManager::GetInstance().GetTexture(key, false) != nullptr;
		}
		out["mine"] = { {"rocks", board->mMineGrid.rock}, {"distances", board->mMineGrid.distance},
			{"rockCount",std::count(board->mMineGrid.rock.begin(),board->mMineGrid.rock.end(),true)},
			{"fogStrength1000",static_cast<int>(std::lround(board->GetMineFogStrength()*1000))},
			{"fogElapsedMs",static_cast<int>(std::lround(board->mMineFogElapsed*1000))},
			{"fogNextWave",board->mMineFogNextWave}, {"fogTutorialSeen",board->mMineFogTutorialSeen},
			{"crystalSpawnedThisWave",board->GetCrystalMinersSpawnedThisWave()},
			{"sunThievesSpawnedThisWave",board->GetSunThievesSpawnedThisWave()},
			{"goldenFog",board->HasGoldenMineFog()}, {"drummersSpawnedThisWave",board->GetCrystalDrummersSpawnedThisWave()},
			{"purpleFog",board->HasPurpleMineFog()}, {"fogFirstColumn",board->GetMineFogFirstColumn()},
			{"layoutRevision", board->mMineGrid.layoutRevision},
			{"connected", board->mMineGrid.connected}, {"entrances", board->mMineGrid.entrance},
			{"pathValid", board->mMineGrid.Validate()}, {"digCell", board->mMineDigCell},
			{"digRemainingMs", static_cast<int>(std::lround(board->mMineDigRemaining * 1000))},
			{"toolActive", board->mMineToolActive},
			{"cursorObjectType", static_cast<int>(board->mCursorObjectManager.GetActiveType())}, {"routesVisible", board->mMineRoutesVisible},
			{"previewCell", board->mMinePreviewCell}, {"resourcesReady", resourcesReady},
			{"forecastWave", board->mMinePlannedWave}, {"forecastMask", board->GetMineForecastEntranceMask()},
			{"wavePlan", board->mMineWavePlan} };
		out["mine"]["wavePlanDetails"] = nlohmann::json::array();
		out["mine"]["forecastMainMask"] = board->GetMineForecastMainEntranceMask();
		for (const auto& entry : board->mMineWavePlan)
			out["mine"]["wavePlanDetails"].push_back({{"type",ZombieTypeName(entry.first)},{"row",entry.second},
				{"role",GameDataManager::GetInstance().GetZombieMineFormationRole(entry.first)}});
	}
	out["echoWaves"] = nlohmann::json::array();
	out["sunTheftLedger"] = nlohmann::json::object();
	for (const auto& [id, record] : board->mSunTheftLedger)
		out["sunTheftLedger"][std::to_string(id)] = {{"stolen",record.stolen},{"carried",record.carried},
			{"escaped",record.escaped},{"disabled",record.disabled}};
	bool thirdPairReady = ResourceManager::GetInstance().HasReanimation("PrismFlower")
		&& ResourceManager::GetInstance().HasReanimation("SunThiefZombie");
	for (const char* key : {"IMAGE_PRISMFLOWER","IMAGE_PRISM_MARK","IMAGE_SUNTHIEF_TANK0",
		"IMAGE_SUNTHIEF_TANK1","IMAGE_SUNTHIEF_TANK2","IMAGE_SUNTHIEF_TANK3",
		"IMAGE_SUNTHIEF_NOZZLE","IMAGE_SUNTHIEF_APRON","IMAGE_SUNTHIEF_HOSE"})
		thirdPairReady = thirdPairReady && ResourceManager::GetInstance().GetTexture(key,false);
	for (int part = 0; part < 7; ++part)
		thirdPairReady = thirdPairReady && ResourceManager::GetInstance().GetTexture("IMAGE_REANIM_PRISMFLOWER_PART"+std::to_string(part),false);
	out["mineThirdResourcesReady"] = thirdPairReady;
	bool fourthReady = ResourceManager::GetInstance().HasReanimation("AmberLichen")
		&& ResourceManager::GetInstance().HasReanimation("CrystalDrummerZombie");
	for (const char* key : {"IMAGE_AMBERLICHEN", "IMAGE_AMBERLICHEN_RESIN", "IMAGE_CRYSTALDRUMMER_DRUM", "IMAGE_CRYSTALDRUMMER_MALLET"})
		fourthReady = fourthReady && ResourceManager::GetInstance().GetTexture(key,false);
	for (int part=0; part<5; ++part)
		fourthReady = fourthReady && ResourceManager::GetInstance().GetTexture("IMAGE_REANIM_AMBERLICHEN_PART"+std::to_string(part),false);
	out["mineFourthResourcesReady"] = fourthReady;
	out["echoWaveCount"] = board->mEchoWaves.size();
	for (const auto& wave : board->mEchoWaves) out["echoWaves"].push_back({
		{"row",wave.row},{"column",wave.column},{"elapsedMs",static_cast<int>(std::lround(wave.elapsed*1000))},
		{"distances",wave.distances},{"hitIDs",wave.hitIDs},{"hitIceWall",wave.hitIceWall}});
	out["minePairResourcesReady"] = ResourceManager::GetInstance().HasReanimation("EchoShroom")
		&& ResourceManager::GetInstance().HasReanimation("CrystalHornMinerZombie")
		&& ResourceManager::GetInstance().GetTexture("IMAGE_ECHOSHROOM",false)
		&& ResourceManager::GetInstance().GetTexture("IMAGE_REANIM_ECHOSHROOM_HEAD",false)
		&& ResourceManager::GetInstance().GetTexture("IMAGE_CRYSTALHORN_INTACT",false)
		&& ResourceManager::GetInstance().GetTexture("IMAGE_CRYSTALHORN_CRACKED",false)
		&& ResourceManager::GetInstance().GetTexture("IMAGE_CRYSTALHORN_BROKEN",false)
		&& ResourceManager::GetInstance().GetTexture("PARTICLE_CRYSTALHORNHEAD",false);
	out["winterGardenBackgroundLoaded"] = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_BACKGROUND_WINTERGARDEN, false) != nullptr;
	out["polarNightBackgroundLoaded"] = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_BACKGROUND_POLAR_NIGHT, false) != nullptr;
	out["snowHoleTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_SNOW_HOLE, false) != nullptr;
	out["winterFrostOverlayLoaded"] = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_WINTER_FROST_OVERLAY_V2, false) != nullptr;
	out["winterFrostFrontierVariant1Loaded"] = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_WINTER_FROST_FRONTIER_VARIANT_1, false) != nullptr;
	out["winterFrostFrontierVariant2Loaded"] = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_WINTER_FROST_FRONTIER_VARIANT_2, false) != nullptr;
	out["winterThermometerLoaded"] = ResourceManager::GetInstance().GetTexture(
		ResourceKeys::Textures::IMAGE_WINTER_THERMOMETER_FRAME_V1, false) != nullptr;
	out["snowAnchorNutResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_SNOWANCHORNUT) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_SNOWANCHORNUT, false) != nullptr },
		{ "bodyTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_SNOWANCHORNUT_BODY, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_SNOWANCHORNUT_CRACKED1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_SNOWANCHORNUT_CRACKED2, false) != nullptr },
		{ "bracedTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_SNOWANCHORNUT_BRACED_BODY, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_SNOWANCHORNUT_BRACED_CRACKED1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_SNOWANCHORNUT_BRACED_CRACKED2, false) != nullptr },
	};
	out["meltSnowPultResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_MELTSNOWPULT) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_MELTSNOWPULT, false) != nullptr },
		{ "snowClodTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_MELTSNOWPULT_SNOWCLOD, false) != nullptr },
		{ "saltCrystalTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_MELTSNOWPULT_SALTCRYSTAL, false) != nullptr },
		{ "hitParticleTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_MELT_SNOW_SPLATS_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_MELT_SNOW_PARTICLES_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_SALT_CRYSTAL_SPLATS_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_SALT_CRYSTAL_PARTICLES_PART_0, false) != nullptr },
	};
	out["gatlingPeaResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GATLINGPEA) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_GATLINGPEA, false) != nullptr },
		{ "barrelLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_GATLINGPEA_BARREL", false) != nullptr },
	};
	out["auroraTorchwoodResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_AURORATORCHWOOD) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_AURORATORCHWOOD, false) != nullptr },
		{ "bodyLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_AURORATORCHWOOD_BODY", false) != nullptr },
		{ "mouthLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_AURORATORCHWOOD_MOUTH", false) != nullptr },
		{ "prismLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_AURORATORCHWOOD_PRISM1", false) != nullptr },
		{ "projectileLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PROJECTILEAURORAPEA, false) != nullptr },
		{ "snowProjectileLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PROJECTILEAURORASNOWPEA, false) != nullptr },
		{ "toxicProjectileLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PROJECTILEAURORATOXICPEA, false) != nullptr },
	};
	out["snowBurrowResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_SNOW_BURROW_ZOMBIE) },
		{ "bodyLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_ZOMBIE_SNOWBURROW_BODY", false) != nullptr },
		{ "shovelLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_ZOMBIE_SNOWBURROW_SHOVEL", false) != nullptr },
		{ "brokenArmLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_SNOWBURROW_OUTERARM_UPPER2,
			false) != nullptr },
		{ "sleeveLoaded", ResourceManager::GetInstance().GetTexture(
			"IMAGE_REANIM_ZOMBIE_SNOWBURROW_OUTERARM_LOWER", false) != nullptr },
		{ "headParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_SNOWBURROWHEAD, false) != nullptr },
		{ "armParticleLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_SNOWBURROWARM, false) != nullptr },
	};
	out["potatoMineResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_POTATOMINE) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_POTATOMINE, false) != nullptr },
		{ "mashedTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_POTATOMINE_MASHED, false) != nullptr },
		{ "particleTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_POTATOMINE_PARTICLES_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_POTATOMINE_PARTICLES_PART_5, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_POTATOMINEFLASH, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_EXPLOSIONSPUDOW, false) != nullptr },
	};
	out["frostMineResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_FROSTMINE) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_FROSTMINE, false) != nullptr },
		{ "stateTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_FROSTMINE_DORMANT, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_FROSTMINE_CALIBRATED, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_FROSTMINE_ARMED, false) != nullptr },
		{ "particleTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_FROSTMINESHARDS_PART_0, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_FROSTMINESHARDS_PART_3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_FROSTMINEPULSE, false) != nullptr },
	};
	out["alarmBellFlowerResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ALARMBELLFLOWER) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ALARMBELLFLOWER, false) != nullptr },
		{ "rigTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_DIRT_BACK, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_DIRT_FRONT, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_STEM1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_STEM2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_PETAL, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_HEAD, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_HEAD2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ALARMBELLFLOWER_CHARM, false) != nullptr },
		{ "particleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ALARMBELLROWPULSE, false) != nullptr },
	};
	out["area8FinaleResources"] = {
		{ "dawnStrikeTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_DAWNFLARE, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_STAR40, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture("PARTICLE_RAIN_CIRCLE", false) != nullptr },
		{ "boundaryReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_BOUNDARYFLOWER) },
		{ "dawnReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_DAWNLOTUS) },
		{ "plantCardsLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_BOUNDARYFLOWER, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_DAWNLOTUS, false) != nullptr },
		{ "plantFollowersLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_BOUNDARYFLOWER_MONUMENT, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_DAWNLOTUS_CROWN, false) != nullptr },
		{ "zombieFollowersLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_AURORA_DEVICE, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_AURORA_PRISM, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_POLAR_CLOCK_DISK, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_POLAR_PENDULUM, false) != nullptr },
		{ "particleTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_AURORARIFT, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_TEMPORALGEAR, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_BOUNDARYSHARD, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_DAWNFLARE, false) != nullptr },
	};
	out["furnaceCoreFlowerResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_FURNACECOREFLOWER) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_FURNACECOREFLOWER, false) != nullptr },
		{ "coreTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_FURNACECOREFLOWER_CORE, false)
				!= nullptr },
		{ "flameTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_TORCHWOOD_FIRE1A, false)
				!= nullptr },
	};
	out["isBossLevel"] = AdventureProgression::IsBossLevel(board->mLevel);
	out["bossSlot"] = BossSlotName(AdventureProgression::GetBossSlot(board->mLevel));
	out["poolEffectCounter"] = gs->GetPoolEffectCounter();
	const RoofMarshalBossHealthBarState bossHealthBar =
		gs->GetRoofMarshalBossHealthBarState();
	out["roofMarshalBossHealthBar"] = {
		{ "visible", bossHealthBar.visible },
		{ "currentHealth", bossHealthBar.currentHealth },
		{ "maxHealth", bossHealthBar.maxHealth },
		{ "fillPermille", static_cast<int>(std::lround(
			bossHealthBar.fillRatio * 1000.0f)) },
		{ "highThreatThreshold", bossHealthBar.highThreatThreshold },
		{ "desperateThreshold", bossHealthBar.desperateThreshold },
		{ "highThreatMarkerPermille", bossHealthBar.maxHealth > 0
			? static_cast<int>(std::lround(1000.0f
				* bossHealthBar.highThreatThreshold / bossHealthBar.maxHealth)) : 0 },
		{ "desperateMarkerPermille", bossHealthBar.maxHealth > 0
			? static_cast<int>(std::lround(1000.0f
				* bossHealthBar.desperateThreshold / bossHealthBar.maxHealth)) : 0 },
		{ "xInt", static_cast<int>(std::lround(bossHealthBar.x)) },
		{ "yInt", static_cast<int>(std::lround(bossHealthBar.y)) },
		{ "widthInt", static_cast<int>(std::lround(bossHealthBar.width)) },
		{ "heightInt", static_cast<int>(std::lround(bossHealthBar.height)) },
	};
	out["rows"] = board->mRows;
	out["columns"] = board->mColumns;
	out["cellHeightInt"] = static_cast<int>(std::lround(board->GetCellHeight()));
	out["roofSlopeEndXInt"] = static_cast<int>(std::lround(board->GetRoofSlopeEndX()));
	out["roofResources"] = {
		{ "dayBackgroundLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_BACKGROUND_ROOF, false) != nullptr },
		{ "rainBackgroundLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_BACKGROUND_ROOF_RAIN, false) != nullptr },
		{ "nightBackgroundLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHTROOF, false) != nullptr },
		{ "nightRainBackgroundLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_BACKGROUND_NIGHTROOF_RAIN, false) != nullptr },
		{ "roofCleanerReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ROOF_CLEANER) },
	};
	out["previewZombies"] = nlohmann::json::array();
	int previewBobsledMemberCount = 0;
	int previewBobsledSlotMask = 0;
	float previewBobsledMinX = std::numeric_limits<float>::max();
	float previewBobsledMaxX = std::numeric_limits<float>::lowest();
	for (Zombie* preview : board->mPreviewZombieList) {
		if (!preview || !preview->IsActive()) continue;
		const Vector pos = preview->GetPosition();
		const float terrainY = preview->mRow >= 0
			? board->GetZombieSpawnY(preview->mRow, pos.x) : pos.y;
		nlohmann::json previewState = {
			{ "type", ZombieTypeName(preview->mZombieType) },
			{ "row", preview->mRow },
			{ "xInt", static_cast<int>(std::lround(pos.x)) },
			{ "yInt", static_cast<int>(std::lround(pos.y)) },
			{ "terrainYOffsetOn1000", static_cast<int>(std::lround(
				(pos.y - terrainY) * 1000.0f)) },
		};
		if (auto* bobsled = dynamic_cast<BobsledTeamZombie*>(preview)) {
			const int slot = bobsled->GetBobsledSlot();
			previewState["bobsledSlot"] = slot;
			++previewBobsledMemberCount;
			previewBobsledSlotMask |= 1 << slot;
			previewBobsledMinX = std::min(previewBobsledMinX, pos.x);
			previewBobsledMaxX = std::max(previewBobsledMaxX, pos.x);
		}
		out["previewZombies"].push_back(std::move(previewState));
	}
	out["previewZombieCount"] = static_cast<int>(out["previewZombies"].size());
	out["previewBobsledMemberCount"] = previewBobsledMemberCount;
	out["previewBobsledSlotMask"] = previewBobsledSlotMask;
	out["previewBobsledXSpanOn1000"] = previewBobsledMemberCount > 0
		? static_cast<int>(std::lround((previewBobsledMaxX - previewBobsledMinX) * 1000.0f))
		: 0;
	out["supportsWeather"] = board->SupportsWeather();
	out["poolRows"] = nlohmann::json::array();
	for (int row = 0; row < board->mRows; ++row) {
		if (board->IsPoolRow(row)) out["poolRows"].push_back(row);
	}
	// 锁定集中禁水名单：当前应为空；未来新增禁水类型时测试与设计需同步显式更新。
	out["poolBlockedZombieTypes"] = nlohmann::json::array();
	for (int i = 0; i < static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES); ++i) {
		const auto type = static_cast<ZombieType>(i);
		if (!board->CanZombieTypeSpawnInPool(type)) {
			out["poolBlockedZombieTypes"].push_back(ZombieTypeName(type));
		}
	}
	out["poolBlockedZombieTypeCount"] =
		static_cast<int>(out["poolBlockedZombieTypes"].size());
	Graphics& graphics = gameApp.GetGraphics();
	auto* vulkanContext = gameApp.GetVulkanContext();
	auto* openGLRenderer = gameApp.GetOpenGLRenderer();
	out["graphics"] = {
		{ "renderer", pvz::RendererBackendName(gameApp.GetSelectedRenderer()) },
		{ "lastFrameDrawCalls", graphics.GetLastFrameDrawCallCount() },
		{ "lastFrameScissorChanges", graphics.GetLastFrameScissorChangeCount() },
		{ "vulkanApiMajor", vulkanContext
			? static_cast<int>(VK_VERSION_MAJOR(vulkanContext->ApiVersion())) : 0 },
		{ "vulkanApiMinor", vulkanContext
			? static_cast<int>(VK_VERSION_MINOR(vulkanContext->ApiVersion())) : 0 },
		{ "dynamicRenderingPath", vulkanContext
			? vulkanContext->DynamicRenderingPathName() : "unavailable" },
		{ "synchronizationPath", vulkanContext
			? vulkanContext->SynchronizationPathName() : "unavailable" },
		{ "openGLQuadCount", openGLRenderer
			? openGLRenderer->LastFrameStats().quadCount : 0 },
		{ "openGLBatchCount", openGLRenderer
			? openGLRenderer->LastFrameStats().batchCount : 0 },
		{ "openGLCpuBatchUploadCount", openGLRenderer
			? openGLRenderer->LastFrameStats().cpuBatchUploadCount : 0 },
		{ "openGLTextureFlushCount", openGLRenderer
			? openGLRenderer->LastFrameStats().textureFlushCount : 0 },
		{ "openGLStateFlushCount", openGLRenderer
			? openGLRenderer->LastFrameStats().stateFlushCount : 0 },
		{ "openGLPeakVboBytes", openGLRenderer
			? openGLRenderer->LastFrameStats().peakVboBytes : 0 },
		{ "openGLPeakIboBytes", openGLRenderer
			? openGLRenderer->LastFrameStats().peakIboBytes : 0 },
		{ "openGLPeakSsboBytes", openGLRenderer
			? openGLRenderer->LastFrameStats().peakSsboBytes : 0 },
		{ "openGLBatchPath", openGLRenderer
			? openGLRenderer->BatchPathName() : "unavailable" },
		{ "openGLContextMajor", openGLRenderer
			? openGLRenderer->ContextMajorVersion() : 0 },
		{ "openGLContextMinor", openGLRenderer
			? openGLRenderer->ContextMinorVersion() : 0 },
		{ "openGLFrameMilliseconds", openGLRenderer
			? openGLRenderer->LastFrameStats().frameMilliseconds : 0.0 },
	};
	out["sun"] = board->mSun;
	out["miniGame"] = MiniGame::IsMiniGame(board->mLevel);
	out["skySunCountdownMs"] =
		static_cast<int>(std::lround(board->mSunCountDown * 1000.0f));
	out["poolSunCountdownMs"] =
		static_cast<int>(std::lround(board->mPoolSunCountDown * 1000.0f));
	int normalSunCount = 0;
	int smallSunCount = 0;
	out["suns"] = nlohmann::json::array();
	for (int id : board->mEntityRegistry.GetAllCoinIDs()) {
		Coin* coin = board->mEntityRegistry.GetCoin(id);
		auto* sun = dynamic_cast<Sun*>(coin);
		if (!sun) continue;
		const bool isSmall = dynamic_cast<SmallSun*>(sun) != nullptr;
		if (isSmall) ++smallSunCount;
		else ++normalSunCount;
		out["suns"].push_back({
			{ "id", id },
			{ "small", isSmall },
			{ "xInt", static_cast<int>(std::lround(sun->GetPosition().x)) },
			{ "yInt", static_cast<int>(std::lround(sun->GetPosition().y)) },
			{ "collected", sun->IsCollected() },
		});
	}
	out["normalSunCount"] = normalSunCount;
	out["smallSunCount"] = smallSunCount;
	out["wave"] = board->mCurrentWave;
	out["currentWaveZombieHP"] = board->mCurrectWaveZombieHP;
	out["nextWaveSpawnZombieHP"] = board->mNextWaveSpawnZombieHP;
	out["maxWave"] = board->mMaxWave;
	out["nextWaveCountdownMs"] = static_cast<int>(std::lround(
		board->mZombieCountDown * 1000.0f));
	out["waveZombiePoints"] = board->GetCurrentWaveZombiePoints();
	out["boardHxyModeEnabled"] = board->mHxyModeEnabled;
	out["zombieNumber"] = board->mZombieNumber;
	out["hostileZombieCountForMusic"] = board->GetHostileZombieCountForMusic();
	out["mowerCount"] = static_cast<int>(board->mEntityRegistry.GetAllMowerIDs().size());
	int movingMowerCount = 0;
	out["mowers"] = nlohmann::json::array();
	for (int id : board->mEntityRegistry.GetAllMowerIDs()) {
		Mower* mower = board->mEntityRegistry.GetMower(id);
		if (!mower) continue;
		const bool moving = mower->mState == MowerState::MOVING;
		if (moving) ++movingMowerCount;
		const Vector mowerPosition = mower->GetPosition();
		const float terrainY = board->GetMowerTerrainY(
			mower->mRow, mowerPosition.x + 40.0f);
		const auto* mowerShadow = mower->GetShadow();
		const Vector mowerShadowOffset = mowerShadow
			? mowerShadow->GetOffset() : Vector::zero();
		out["mowers"].push_back({
			{ "id", id }, { "row", mower->mRow },
			{ "renderOrder", mower->GetRenderOrder() },
			{ "renderLayer", static_cast<int>(mower->GetLayer()) },
			{ "type", MowerTypeName(mower->mMowerType) },
			{ "state", moving ? "MOVING" : "IDLE" },
			{ "height", MowerHeightName(mower->mMowerHeight) },
			{ "track", mower->GetCurrentTrackName() },
			{ "animationType", static_cast<int>(mower->GetAnimationType()) },
			{ "animationPlaying", mower->IsAnimationPlaying() },
			{ "visualY", mower->GetVisualPosition().y },
			{ "shadowOffsetXInt", static_cast<int>(std::lround(mowerShadowOffset.x)) },
			{ "shadowOffsetYInt", static_cast<int>(std::lround(mowerShadowOffset.y)) },
			{ "xInt", static_cast<int>(std::lround(mowerPosition.x)) },
			{ "yInt", static_cast<int>(std::lround(mowerPosition.y)) },
			{ "terrainYOffsetOn1000", static_cast<int>(std::lround(
				(mowerPosition.y - terrainY) * 1000.0f)) },
		});
	}
	out["movingMowerCount"] = movingMowerCount;
	out["devNoCooldown"] = GameAPP::mDevNoCooldown;
	out["devFreePlant"] = GameAPP::mDevFreePlant;
	out["devSpawnPaused"] = GameAPP::mDevSpawnPaused;
	out["devSelectedLevel"] = gameApp.mDeveloperSelectedLevel;
	out["devSelectedZombie"] = gameApp.mDeveloperSelectedZombie;
	out["timeScaleOn1000"] =
		static_cast<int>(std::lround(DeltaTime::GetTimeScale() * 1000.0f));
	out["adventureLevel"] = gameApp.mAdventureLevel;
	out["plantDefinitions"] = nlohmann::json::object();
	for (PlantType type : GameDataManager::GetInstance().GetAllPlantTypes()) {
		const PlantSimulationProfile& simulation =
			GameDataManager::GetInstance().GetPlantSimulationProfile(type);
		out["plantDefinitions"][PlantTypeName(type)] = {
			{ "sunCost", GameDataManager::GetInstance().GetPlantSunCost(type) },
			{ "cooldownMs", static_cast<int>(std::lround(
				GameDataManager::GetInstance().GetPlantCooldown(type) * 1000.0f)) },
			{ "simulationBaseHealth", simulation.baseHealth },
			{ "simulationAttackDpsOn100", static_cast<int>(
				std::lround(simulation.attackDps * 100.0f)) },
			{ "simulationAttackRowRadius", simulation.attackRowRadius },
			{ "simulationSunPerSecondOn100", static_cast<int>(
				std::lround(simulation.sunPerSecond * 100.0f)) },
			{ "simulationFirstSunDelayMs", static_cast<int>(
				std::lround(simulation.firstSunDelay * 1000.0f)) },
			{ "simulationMagneticPulseCooldownMs", static_cast<int>(
				std::lround(simulation.magneticPulseCooldown * 1000.0f)) },
			{ "simulationMagneticPulseRadius", static_cast<int>(
				std::lround(simulation.magneticPulseRadius)) },
			{ "simulationMagneticPulseParalysisMs", static_cast<int>(
				std::lround(simulation.magneticPulseParalysisDuration * 1000.0f)) },
			{ "simulationMagneticSearchRowRadius", simulation.magneticSearchRowRadius },
			{ "simulationMagneticSearchRadiusOn1000", static_cast<int>(
				std::lround(simulation.magneticSearchRadiusInCells * 1000.0f)) },
			{ "simulationMagneticEatingSearchRadiusOn1000", static_cast<int>(
				std::lround(simulation.magneticEatingSearchRadiusInCells * 1000.0f)) },
			{ "simulationCobBlastCooldownMs", static_cast<int>(
				std::lround(simulation.cobBlastCooldown * 1000.0f)) },
			{ "simulationCobBlastDamage", static_cast<int>(
				std::lround(simulation.cobBlastDamage)) },
			{ "simulationCobBlastRadius", static_cast<int>(
				std::lround(simulation.cobBlastRadius)) },
			{ "simulationCobBlastRowRadius", simulation.cobBlastRowRadius },
			{ "simulationDaytimeDormant", simulation.daytimeDormant },
			{ "simulationPersistent", simulation.persistent },
			{ "simulationFuturePlantable", simulation.futurePlantable },
			{ "simulationSupportOnly", simulation.supportOnly },
		};
	}
	out["polarMidgameResources"] = {
		{ "northStarReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_NORTHSTARFLOWER) },
		{ "northStarCardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_NORTHSTARFLOWER, false) != nullptr },
		{ "northStarHeadTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_NORTHSTARFLOWER_HEAD, false) != nullptr },
		{ "iceMirrorReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ICEMIRRORGRASS) },
		{ "iceMirrorCardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ICEMIRRORGRASS, false) != nullptr },
		{ "iceMirrorBodyTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ICEMIRRORGRASS_BODY, false) != nullptr },
		{ "thermalPulseTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_PROJECTILETHERMALPULSE, false) != nullptr },
		{ "thermalLauncherReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_THERMAL_SNIPER_LAUNCHER) },
		{ "thermalGogglesTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_THERMAL_GOGGLES, false) != nullptr },
		{ "thermalPackTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_THERMAL_PACK, false) != nullptr },
		{ "thermalRifleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_THERMAL_RIFLE, false) != nullptr },
	};
	out["flowerPotResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_FLOWERPOT) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_FLOWERPOT, false) != nullptr },
	};
	out["cabbagePultResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_CABBAGEPULT) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_CABBAGEPULT, false) != nullptr },
		{ "projectileTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_CABBAGEPULT_CABBAGE, false) != nullptr },
	};
	out["kernelPultResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_KERNELPULT) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_CORNPULT, false) != nullptr },
		{ "kernelTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_CORNPULT_KERNAL, false) != nullptr },
		{ "butterTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_CORNPULT_BUTTER, false) != nullptr },
		{ "butterSplatTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_CORNPULT_BUTTER_SPLAT, false) != nullptr },
		{ "kernelSoundsLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_KERNELPULT)
			&& ResourceManager::GetInstance().HasSound(
				ResourceKeys::Sounds::SOUND_KERNELPULT2) },
		{ "butterSoundLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_BUTTER) },
	};
	out["melonPultResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_MELONPULT) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_MELONPULT, false) != nullptr },
		{ "projectileTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_MELONPULT_MELON, false) != nullptr },
		{ "bodyTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_MELONPULT_BLINK1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MELONPULT_BLINK2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MELONPULT_BODY, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MELONPULT_EYEBROW, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_MELONPULT_STALK, false) != nullptr },
		{ "particleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_MELONPULT_PARTICLES, false) != nullptr },
		{ "impactSoundsLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_MELONIMPACT)
			&& ResourceManager::GetInstance().HasSound(
				ResourceKeys::Sounds::SOUND_MELONIMPACT2) },
	};
	out["winterMelonResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_WINTERMELON) },
		{ "cardTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_WINTERMELON, false) != nullptr },
		{ "projectileTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_PROJECTILE, false) != nullptr },
		{ "bodyTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_BASKET, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_BASKET_OVERLAY, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_BLINK1, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_BLINK2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_EYEBROW, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_MELON, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_WINTERMELON_STALK, false) != nullptr },
		{ "sharedLeafTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_PEASHOOTER_FRONTLEAF, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_PEASHOOTER_FRONTLEAF_LEFTTIP, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_PEASHOOTER_FRONTLEAF_RIGHTTIP, false) != nullptr },
		{ "particleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_WINTERMELON_PARTICLES, false) != nullptr },
		{ "impactSoundsLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_MELONIMPACT)
			&& ResourceManager::GetInstance().HasSound(
				ResourceKeys::Sounds::SOUND_MELONIMPACT2) },
		{ "chillSoundsLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_SNOW_PEA_SPARKLES)
			&& ResourceManager::GetInstance().HasSound(
				ResourceKeys::Sounds::SOUND_COOLDOWNZOMBIE) },
	};
	out["catapultResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_CATAPULT_ZOMBIE) },
		{ "charredReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_CATAPULT_CHARRED) },
		{ "basketballTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_CATAPULT_BASKETBALL, false) != nullptr },
		{ "sidingDamageTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_CATAPULT_SIDING_DAMAGE, false) != nullptr },
		{ "poleDamageTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_CATAPULT_POLE_DAMAGE, false) != nullptr },
		{ "poleDamageWithBallTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_CATAPULT_POLE_DAMAGE_WITHBALL, false) != nullptr },
		{ "basketballSoundLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_BASKETBALL) },
	};
	out["eliteCatapultResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ELITE_CATAPULT_ZOMBIE) },
		{ "sidingTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ELITE_CATAPULT_SIDING,
			false) != nullptr },
		{ "sidingDamageTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_CATAPULT_SIDING_DAMAGE,
			false) != nullptr },
		{ "manholeTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ELITE_CATAPULT_MANHOLE,
			false) != nullptr },
	};
	out["roofMarshalResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ROOF_MARSHAL_ZOMBIE) },
		{ "assaultFlagReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_ROOF_MARSHAL_ASSAULT_FLAG) },
		{ "assaultFlagTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_FLAG1,
			false) != nullptr },
		{ "bodyTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ROOFMARSHAL_BODY,
			false) != nullptr },
		{ "hatTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ROOFMARSHAL_HAT,
			false) != nullptr },
		{ "tieTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_ROOFMARSHAL_TIE,
			false) != nullptr },
		{ "brokenArmTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_ROOFMARSHAL_OUTERARM_UPPER2,
			false) != nullptr },
		{ "headParticleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_ROOF_MARSHAL_HEAD,
			false) != nullptr },
	};
	out["hijackerResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_HIJACKER_ZOMBIE) },
		{ "bodyTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_BODY1, false) != nullptr },
		{ "receiverTexturesLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_BOX, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_BOX2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_HANDLE, false) != nullptr },
		{ "headTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_HEAD, false) != nullptr },
		{ "brokenArmTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_HIJACKER_OUTERARM_LOWER2,
			false) != nullptr },
		{ "headParticleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_HIJACKER_HEAD, false) != nullptr },
		{ "armParticleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_HIJACKER_ARM, false) != nullptr },
		{ "humSoundLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_HIJACKER_HUM) },
		{ "executeSoundLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_HIJACKER_EXECUTE) },
		{ "executeSoundPlayRequests", AudioSystem::GetSoundPlayRequestCount(
			ResourceKeys::Sounds::SOUND_HIJACKER_EXECUTE) },
	};
	out["gargantuarResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GARGANTUAR_ZOMBIE) },
		{ "charredReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_GARGANTUAR_CHARRED) },
		{ "telephonePoleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_TELEPHONEPOLE,
			false) != nullptr },
		{ "duckSignTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_DUCKXING,
			false) != nullptr },
		{ "zombieWeaponTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_ZOMBIE,
			false) != nullptr },
		{ "damageTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_BODY1_2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_BODY1_3, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_OUTERARM_LOWER2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_FOOT2, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_HEAD2, false) != nullptr },
		{ "redEyeHeadTexturesLoaded",
			ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_HEAD_REDEYE, false) != nullptr
			&& ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_GARGANTUAR_HEAD2_REDEYE,
				false) != nullptr },
		{ "actionSoundsLoaded", ResourceManager::GetInstance().HasSound(
			ResourceKeys::Sounds::SOUND_GARGANTUAR_THUMP)
			&& ResourceManager::GetInstance().HasSound(ResourceKeys::Sounds::SOUND_LOWGROAN)
			&& ResourceManager::GetInstance().HasSound(ResourceKeys::Sounds::SOUND_LOWGROAN2)
			&& ResourceManager::GetInstance().HasSound(ResourceKeys::Sounds::SOUND_SWING)
			&& ResourceManager::GetInstance().HasSound(ResourceKeys::Sounds::SOUND_IMP)
			&& ResourceManager::GetInstance().HasSound(ResourceKeys::Sounds::SOUND_IMP2)
			&& ResourceManager::GetInstance().HasSound(ResourceKeys::Sounds::SOUND_GARGANTUDEATH) },
	};
	out["impResources"] = {
		{ "reanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_IMP_ZOMBIE) },
		{ "charredReanimationLoaded", ResourceManager::GetInstance().HasReanimation(
			ResourceKeys::Reanimations::REANIM_IMP_CHARRED) },
		{ "headParticleTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Particles::PARTICLE_ZOMBIE_IMPHEAD, false) != nullptr },
		{ "armBoneTextureLoaded", ResourceManager::GetInstance().GetTexture(
			ResourceKeys::Textures::IMAGE_REANIM_ZOMBIE_IMP_ARM1_BONE, false) != nullptr },
	};
	out["cards"] = nlohmann::json::array();
	out["carryVine"] = {
		{ "reanimLoaded", ResourceManager::GetInstance().HasReanimation(ResourceKeys::Reanimations::REANIM_CARRYVINE) },
		{ "cardLoaded", ResourceManager::GetInstance().GetTexture(ResourceKeys::Textures::IMAGE_CARRYVINE, false) != nullptr },
		{ "basketLoaded", ResourceManager::GetInstance().GetTexture(ResourceKeys::Textures::IMAGE_CARRYVINE_BASKET, false) != nullptr },
		{ "sourceID", gs->GetCardSlotManager() ? gs->GetCardSlotManager()->GetRelocationSourceID() : NULL_PLANT_ID },
	};
	if (CardSlotManager* cardManager = gs->GetCardSlotManager()) {
		for (Card* card : cardManager->GetCards()) {
			if (!card) continue;
			Transform* transform = card->GetTransform();
			if (!transform) continue;
			nlohmann::json cardState = {
				{ "type", PlantTypeName(card->GetPlantType()) },
				{ "gameplayType", PlantTypeName(card->GetGameplayPlantType()) },
				{ "xInt", static_cast<int>(std::lround(
					transform->GetPosition().x)) },
				{ "yInt", static_cast<int>(std::lround(
					transform->GetPosition().y)) },
				{ "ready", card->IsReady() },
				{ "selected", card->IsSelected() },
				{ "cooldown", card->IsCooldown() },
				{ "sunCost", card->GetSunCost() },
				{ "cooldownRemainingMs", static_cast<int>(std::lround(card->GetCooldownTimer() * 1000.0f)) },
			};
			if (card->GetPlantType() == PlantType::PLANT_IMITATER) {
				cardState["imitaterTarget"] = PlantTypeName(card->GetImitaterTarget());
			}
			if (card->GetGameplayPlantType() == PlantType::PLANT_BLOVER) {
				cardState["bloverDirection"] =
					WindDirectionName(card->GetBloverDirection());
			}
			out["cards"].push_back(std::move(cardState));
		}
	}
	out["cardCount"] = static_cast<int>(out["cards"].size());

	// 奖杯（在场时给坐标，否则 null）——胜利路径冒烟测试的断言抓手
	if (auto trophy = board->mTrophy.lock()) {
		out["trophy"] = { { "x", trophy->GetPosition().x }, { "y", trophy->GetPosition().y } };
	}
	else {
		out["trophy"] = nullptr;
	}

	// 弹坑（毁灭菇）——阻种/消退/地形外观的断言抓手；timeLeftInt 整数投影供 equals，浮点勿断言
	out["craters"] = nlohmann::json::array();
	for (auto& weak : board->mCraters) {
		auto crater = weak.lock();
		if (!crater || !crater->IsActive()) continue;
		const std::string& textureKey = crater->GetTextureKey();
		out["craters"].push_back({
			{ "row", crater->mRow }, { "col", crater->mColumn },
			{ "timeLeftInt", static_cast<int>(crater->mTimeLeft) },
			{ "textureKey", textureKey },
			{ "textureLoaded",
				ResourceManager::GetInstance().GetTexture(textureKey, false) != nullptr },
		});
	}

	// 屏幕抖动：shakeOn 整数投影供 equals（1=抖动中）；offset 浮点仅供人工排查，勿断言
	{
		const Vector shake = board->GetShakeOffset();
		out["shake"] = {
			{ "shakeOn", (shake.x != 0.0f || shake.y != 0.0f) ? 1 : 0 },
			{ "offsetX", shake.x }, { "offsetY", shake.y },
		};
	}

	// 四大关迷雾：逐列 alpha、驱散比例和贴图分份数均给稳定整数抓手。
	{
		static const std::array<std::string, 8> fogTextureKeys = {
			ResourceKeys::Textures::IMAGE_FOG_PART_0,
			ResourceKeys::Textures::IMAGE_FOG_PART_1,
			ResourceKeys::Textures::IMAGE_FOG_PART_2,
			ResourceKeys::Textures::IMAGE_FOG_PART_3,
			ResourceKeys::Textures::IMAGE_FOG_PART_4,
			ResourceKeys::Textures::IMAGE_FOG_PART_5,
			ResourceKeys::Textures::IMAGE_FOG_PART_6,
			ResourceKeys::Textures::IMAGE_FOG_PART_7,
		};
		int loadedTextureParts = 0;
		for (const std::string& key : fogTextureKeys) {
			if (ResourceManager::GetInstance().GetTexture(key, false)) {
				++loadedTextureParts;
			}
		}
		out["fog"] = {
			{ "supported", board->SupportsStageFog() },
			{ "weatherSupported", board->SupportsFogWeather() },
			{ "planternMechanicsSupported", board->SupportsPlanternMechanics() },
			{ "initialized", board->IsFogWeatherInitialized() },
			{ "intensity", FogWeatherIntensityName(board->GetFogWeatherIntensity()) },
			{ "layerCount", board->GetFogLayerCount() },
			{ "baseLeftColumn", board->GetBaseFogLeftColumn() },
			{ "effectiveLeftColumn", board->GetEffectiveFogLeftColumn() },
			{ "drawRows", board->GetFogDrawRowCount() },
			{ "visibleCells", board->GetVisibleFogCellCount() },
			{ "maxAlpha", board->GetMaximumFogAlpha() },
			{ "dispersalPct", static_cast<int>(std::lround(
				board->GetFogDispersal() * 100.0f)) },
			{ "offsetXInt", static_cast<int>(std::lround(board->GetFogVisualOffsetX())) },
			{ "remaining", board->GetFogWeatherTimer() },
			{ "forecastReady", board->HasFogWeatherForecast() },
			{ "forecastDisrupted", board->IsFogWeatherForecastDisrupted() },
			{ "forecastIntensity", FogWeatherIntensityName(
				board->GetForecastFogWeatherIntensity()) },
			{ "lockedActualIntensity", FogWeatherIntensityName(
				board->GetActualForecastFogWeatherIntensity()) },
			{ "denseChancePct", board->GetDenseFogChancePercent() },
			{ "texturePartsLoaded", loadedTextureParts },
			{ "columnMaxAlpha", nlohmann::json::array() },
			{ "cellAlpha", nlohmann::json::array() },
		};
		for (int row = 0; row < board->GetFogDrawRowCount(); ++row) {
			nlohmann::json rowAlpha = nlohmann::json::array();
			for (int col = 0; col < board->mColumns; ++col) {
				rowAlpha.push_back(static_cast<int>(std::lround(
					board->GetFogCellAlpha(row, col))));
			}
			out["fog"]["cellAlpha"].push_back(std::move(rowAlpha));
		}
		for (int col = 0; col < board->mColumns; ++col) {
			float maximum = 0.0f;
			for (int row = 0; row < board->GetFogDrawRowCount(); ++row) {
				maximum = std::max(maximum, board->GetFogCellAlpha(row, col));
			}
			out["fog"]["columnMaxAlpha"].push_back(
				static_cast<int>(std::lround(maximum)));
		}
	}

	// 路灯花核心状态单独导出，避免把玩法控制塞进天气预报 UI 状态。
	{
		Plantern* plantern = board->GetActivePlantern();
		out["plantern"] = {
			{ "supported", board->SupportsPlanternMechanics() },
			{ "active", plantern != nullptr },
			{ "menuOpen", gs->GetCardSlotManager()
				&& gs->GetCardSlotManager()->IsPlanternGearMenuOpen() },
			{ "id", plantern ? plantern->mPlantID : NULL_PLANT_ID },
			{ "fuelTenths", static_cast<int>(std::lround(
				board->GetPlanternFuel() * 10.0f)) },
			{ "pendingFuelTenths", plantern ? static_cast<int>(std::lround(
				plantern->GetPendingFuel() * 10.0f)) : 0 },
			{ "fuelPct", static_cast<int>(std::lround(
				board->GetPlanternFuelRatio() * 100.0f)) },
			{ "capacity", static_cast<int>(Plantern::FUEL_CAPACITY) },
			{ "gear", plantern ? PlanternGearName(plantern->GetGear()) : "NONE" },
			{ "gearValue", board->GetPlanternGearValue() },
			{ "burnRateTenths", plantern ? static_cast<int>(std::lround(
				plantern->GetCurrentBurnRate() * 10.0f)) : 0 },
			{ "lowFuelWarningOn", board->SupportsPlanternMechanics()
				&& plantern && plantern->IsFuelLow() },
			{ "fullHintOn", board->GetPlanternFuelFullHintTimer() > 0.0f },
			{ "dropAccumulatorPct", static_cast<int>(std::lround(
				board->GetMistFuelDropAccumulator() * 100.0f)) },
			{ "assignedThisWave", board->GetMistFuelAssignedThisWave() },
			{ "scarcityPct", static_cast<int>(std::lround(
				board->GetMistFuelScarcityFactor() * 100.0f)) },
			{ "rewardAmount", board->GetMistFuelRewardAmount() },
			{ "waveBudget", board->GetMistFuelWaveBudget() },
			{ "intakeLimit", plantern ? static_cast<int>(std::lround(
				plantern->GetWaveIntakeLimit())) : board->GetMistFuelWaveBudget() },
			{ "baseCarrierChancePct", static_cast<int>(std::lround(
				board->GetMistFuelBaseCarrierChance() * 100.0f)) },
			{ "heavyCarrierBonusPct", static_cast<int>(std::lround(
				board->GetMistFuelHeavyCarrierBonus() * 100.0f)) },
			{ "fuelTextureLoaded", ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_MISTFUEL, false) != nullptr },
			{ "illuminationPct", nlohmann::json::array() },
		};
		for (int row = 0; row < board->mRows; ++row) {
			nlohmann::json rowIllumination = nlohmann::json::array();
			for (int col = 0; col < board->mColumns; ++col) {
				rowIllumination.push_back(static_cast<int>(std::lround(
					board->GetPlanternIllumination(row, col) * 100.0f)));
			}
			out["plantern"]["illuminationPct"].push_back(std::move(rowIllumination));
		}
	}

	// 黑夜天气：倍率与 alpha 另给整数投影，避免 AutoTest 对浮点做严格 equals。
	{
		const float zombieRain = board->GetZombieRainSpeedMultiplier();
		const float hostileWind = board->GetZombieWindMoveMultiplier(false);
		const float charmedWind = board->GetZombieWindMoveMultiplier(true);
		const float gustDrift = board->GetZombieGustDriftVelocity();
		const float plantRain = board->GetPlantRainActionSpeedMultiplier();
		const float weatherPressure = board->GetWeatherPressureFactor();
		const float perkAttack = static_cast<float>(
			board->GetPerkManager().GetPlantAttackSpeedMultiplier());
		out["weather"] = {
			{ "intensity", RainIntensityName(board->GetRainIntensity()) },
			{ "rainEffect", board->GetRainVisualEffectName() },
			{ "stormyNightForecast", board->IsStormyNightForecastActive() },
			{ "stormyNightActive", board->IsStormyNightActive() },
			{ "stormyNightInitialized", board->IsStormyNightInitialized() },
			{ "stormFlashPattern", board->GetStormyNightFlashPattern() },
			{ "stormFlashRemainingMs", static_cast<int>(std::lround(
				board->GetStormyNightFlashTimer() * 1000.0f)) },
			{ "stormFlashOn", board->IsStormyNightFlashOn() },
			{ "stormBlackAlpha", static_cast<int>(std::lround(
				board->GetStormyNightBlackAlpha())) },
			{ "stormWhiteAlpha", static_cast<int>(std::lround(
				board->GetStormyNightWhiteAlpha())) },
			{ "previousIntensity", RainIntensityName(board->GetPreviousRainIntensity()) },
			{ "initialized", board->IsWeatherInitialized() },
			{ "transitionOn", board->IsWeatherTransitionActive() },
			{ "transitionRemaining", board->GetWeatherTransitionTimer() },
			{ "canIntensify", board->CanRainIntensify() },
			{ "canHold", board->CanRainHold() },
			{ "forecastReady", board->HasWeatherForecast() },
			{ "forecastDisrupted", board->IsWeatherForecastDisrupted() },
			{ "disruptibleForecast", board->HasDisruptibleWeatherForecast() },
			{ "panelInterferenceActive", board->IsWeatherPanelInterferenceActive() },
			{ "panelInterferenceRemainingMs", static_cast<int>(std::lround(
				board->GetWeatherPanelInterferenceTimer() * 1000.0f)) },
			{ "panelInterferenceMaximumMs", static_cast<int>(std::lround(
				board->GetMaximumWeatherPanelInterferenceDuration() * 1000.0f)) },
			{ "forecastIntensity", RainIntensityName(board->GetForecastRainIntensity()) },
			{ "lockedActualIntensity", RainIntensityName(board->GetActualForecastRainIntensity()) },
			{ "forecastPlausible", board->IsWeatherForecastPlausible() },
			{ "forecastAccuracyPct", board->GetCurrentWeatherForecastAccuracyPercent() },
			{ "pendingTyphoonPrepared", board->HasPendingHeavyTyphoon() },
			{ "pendingTyphoonOpeningProtected", board->IsPendingHeavyTyphoonOpeningProtected() },
			{ "pendingTyphoonStrength", TyphoonStrengthName(board->GetPendingHeavyTyphoonStrength()) },
			{ "pendingPromptVariant", board->GetPendingHeavyRainPromptVariant() },
			{ "heavyRainPromptShown", board->HasShownHeavyRainPrompt() },
			{ "weakWeatherPhasesSinceHeavy", board->GetWeakWeatherPhasesSinceHeavy() },
			{ "heavyWeatherForced", board->IsHeavyWeatherForced() },
			{ "newClearWeight", board->GetCurrentNewWeatherWeight(RainIntensity::CLEAR) },
			{ "newLightWeight", board->GetCurrentNewWeatherWeight(RainIntensity::LIGHT) },
			{ "newMediumWeight", board->GetCurrentNewWeatherWeight(RainIntensity::MEDIUM) },
			{ "newHeavyWeight", board->GetCurrentNewWeatherWeight(RainIntensity::HEAVY) },
			{ "remaining", board->GetWeatherTimer() },
			{ "lightningRemaining", board->GetLightningTimer() },
			{ "pressurePct", static_cast<int>(std::lround(weatherPressure * 100.0f)) },
			{ "zombieSpeedPct", static_cast<int>(std::lround(zombieRain * 100.0f)) },
			{ "typhoonStrength", TyphoonStrengthName(board->GetTyphoonStrength()) },
			{ "typhoonSupported", board->SupportsTyphoon() },
			{ "openingTyphoonProtectionActive", board->IsOpeningTyphoonProtectionActive() },
			{ "typhoonChancePct", board->GetCurrentTyphoonChancePercent() },
			{ "heavyPhasesWithoutTyphoon", board->GetHeavyPhasesWithoutTyphoon() },
			{ "eliteDancersSpawnedThisWave", board->GetEliteDancersSpawnedThisWave() },
			{ "reinforcedDoorsSpawnedThisWave", board->GetReinforcedDoorsSpawnedThisWave() },
			{ "elitePolevaultersSpawnedThisWave", board->GetElitePolevaultersSpawnedThisWave() },
			{ "gildedZambonisSpawnedThisWave", board->GetGildedZambonisSpawnedThisWave() },
			{ "eliteDolphinRidersSpawnedThisWave", board->GetEliteDolphinRidersSpawnedThisWave() },
			{ "eliteJackInTheBoxesSpawnedThisWave", board->GetEliteJackInTheBoxesSpawnedThisWave() },
			{ "eliteDiggersSpawnedThisWave", board->GetEliteDiggersSpawnedThisWave() },
			{ "elitePogosSpawnedThisWave", board->GetElitePogosSpawnedThisWave() },
			{ "eliteLaddersSpawnedThisWave", board->GetEliteLaddersSpawnedThisWave() },
			{ "eliteCatapultsSpawnedThisWave", board->GetEliteCatapultsSpawnedThisWave() },
			{ "redeyeGargantuarsSpawnedThisWave",
				board->GetRedeyeGargantuarsSpawnedThisWave() },
			{ "insulatorsSpawnedThisWave", board->GetInsulatorsSpawnedThisWave() },
			{ "hijackersSpawnedThisWave", board->GetHijackersSpawnedThisWave() },
			{ "hijackerSpawnCooldownWavesRemaining",
				board->GetHijackerSpawnCooldownWavesRemaining() },
			{ "hijackerSpawnBlockedThisWave", board->IsHijackerSpawnBlockedThisWave() },
			{ "groundingZombiesSpawnedThisWave",
				board->GetGroundingZombiesSpawnedThisWave() },
			{ "bobsledTeamsSpawnedThisWave",
				board->GetBobsledTeamsSpawnedThisWave() },
			{ "iceWallEngineersSpawnedThisWave",
				board->GetIceWallEngineersSpawnedThisWave() },
			{ "iceCrackDrillsSpawnedThisWave",
				board->GetIceCrackDrillsSpawnedThisWave() },
			{ "weatherJammersSpawnedThisWave",
				board->GetWeatherJammersSpawnedThisWave() },
			{ "iceStatueExecutionersSpawnedThisWave",
				board->GetIceStatueExecutionersSpawnedThisWave() },
			{ "snowBurrowsSpawnedThisWave",
				board->GetSnowBurrowsSpawnedThisWave() },
			{ "snowBurrowTutorialHoleSpawnConsumed",
				board->HasConsumedSnowBurrowTutorialHoleSpawn() },
			{ "adaptiveHelmetsSpawnedThisWave",
				board->GetAdaptiveHelmetsSpawnedThisWave() },
			{ "adaptiveHelmetTutorialWaveSpawned",
				board->HasSpawnedAdaptiveHelmetTutorialWave() },
			{ "thermalSnipersSpawnedThisWave",
				board->GetThermalSnipersSpawnedThisWave() },
			{ "thermalSniperTutorialSpawned",
				board->HasSpawnedThermalSniperTutorial() },
			{ "auroraPriestsSpawnedThisWave",
				board->GetAuroraPriestsSpawnedThisWave() },
			{ "clockmakersSpawnedThisWave",
				board->GetClockmakersSpawnedThisWave() },
			{ "auroraPriestGuaranteeConsumed",
				board->HasConsumedAuroraPriestGuarantee() },
			{ "clockmakerGuaranteeConsumed",
				board->HasConsumedClockmakerGuarantee() },
			{ "pendingAuroraRiftCount", board->GetPendingAuroraRiftCount() },
			{ "temporalAnchorCount", board->GetTemporalAnchorCount() },
			{ "temporalAnchorTargetCount", board->GetTemporalAnchorTargetCount() },
			{ "dawnNavigationRemainingMs", static_cast<int>(std::lround(
				board->GetDawnNavigationTimer() * 1000.0f)) },
			{ "typhoonDecayRemaining", board->GetTyphoonStrengthTimer() },
			{ "windDirection", WindDirectionName(board->GetWindDirection()) },
			{ "windDirectionRemaining", board->GetWindDirectionTimer() },
			{ "windGustRemaining", board->GetWindGustTimer() },
			{ "gustsRemaining", board->GetTyphoonGustsRemaining() },
			{ "gustActive", board->IsTyphoonGustActive() },
			{ "activeGustStrength", TyphoonStrengthName(board->GetActiveGustStrength()) },
			{ "activeGustDirection", WindDirectionName(board->GetActiveGustDirection()) },
			{ "activeGustRemainingMs", static_cast<int>(std::lround(
				board->GetActiveGustTimer() * 1000.0f)) },
			{ "activeGustPlantMoveRemainingMs", static_cast<int>(std::lround(
				board->GetActiveGustPlantMoveTimer() * 1000.0f)) },
			{ "activeGustPlantMoved", board->HasActiveGustMovedPlants() },
			{ "zombieGustDriftSpeed", static_cast<int>(std::lround(gustDrift)) },
			{ "gustWarning", board->IsTyphoonGustWarning() },
			{ "lastGustMovedPlants", board->GetLastTyphoonMovedPlants() },
			{ "lastGustLostPlants", board->GetLastTyphoonLostPlants() },
			{ "lastGustBlockedPlantSteps", board->GetLastTyphoonBlockedPlantSteps() },
			{ "hostileWindMovePct", static_cast<int>(std::lround(hostileWind * 100.0f)) },
			{ "charmedWindMovePct", static_cast<int>(std::lround(charmedWind * 100.0f)) },
			{ "hostileCombinedMovePct", static_cast<int>(std::lround(zombieRain * hostileWind * 100.0f)) },
			{ "charmedCombinedMovePct", static_cast<int>(std::lround(zombieRain * charmedWind * 100.0f)) },
			{ "plantActionSpeedPct", static_cast<int>(std::lround(plantRain * 100.0f)) },
			{ "overlayAlpha", static_cast<int>(std::lround(board->GetRainOverlayAlpha())) },
			{ "roofRainBackgroundAlpha", static_cast<int>(std::lround(
				gs->GetRoofRainBackgroundAlpha())) },
			{ "rainSoundPlaying", AudioSystem::IsLoopingSoundPlaying(ResourceKeys::Sounds::SOUND_RAIN) },
			{ "thunderSoundLoaded", ResourceManager::GetInstance().GetSound(
				ResourceKeys::Sounds::SOUND_THUNDER) != nullptr },
			{ "thunderSoundPlayRequests", AudioSystem::GetSoundPlayRequestCount(
				ResourceKeys::Sounds::SOUND_THUNDER) },
			{ "combinedAttackIntervalOn1500",
				static_cast<int>(1500.0f / (plantRain * perkAttack) + 0.5f) },
			{ "screenFlashOn", gs->IsScreenFlashActive() },
			{ "screenFlashPeakAlpha", static_cast<int>(std::lround(gs->GetScreenFlashPeakAlpha())) },
			{ "lightningFlashOn", gs->IsLightningStrikeActive() },
			{ "lightningMainSegments", gs->GetLightningMainSegmentCount() },
			{ "lightningBranchSegments", gs->GetLightningBranchSegmentCount() },
			{ "lightningStrikeX", static_cast<int>(std::lround(gs->GetLightningStrikeX())) },
			{ "panelSlidePct", static_cast<int>(std::lround(gs->GetWeatherPanelSlide() * 100.0f)) },
			{ "panelHeight", static_cast<int>(std::lround(gs->GetWeatherPanelHeight())) },
			{ "forecastDisplayText", gs->GetWeatherForecastPanelText() },
			{ "currentNoticeOn", gs->IsCurrentWeatherNoticeActive() },
			{ "currentNoticeRemainingMs", static_cast<int>(std::lround(
				gs->GetCurrentWeatherNoticeTimer() * 1000.0f)) },
			{ "forecastFailureOn", gs->IsWeatherForecastFailureActive() },
			{ "forecastFailureRemainingMs", static_cast<int>(std::lround(
				gs->GetWeatherForecastFailureTimer() * 1000.0f)) },
			{ "failedForecastIntensity", RainIntensityName(gs->GetFailedForecastRainIntensity()) },
			{ "actualForecastIntensity", RainIntensityName(gs->GetActualForecastRainIntensity()) },
			{ "failedForecastTyphoonStrength",
				TyphoonStrengthName(gs->GetFailedForecastTyphoonStrength()) },
			{ "actualForecastTyphoonStrength",
				TyphoonStrengthName(gs->GetActualForecastTyphoonStrength()) },
		};
		out["weather"]["winter"] = {
			{ "supported", board->SupportsWinterTemperature() },
			{ "initialized", board->IsWinterTemperatureInitialized() },
			{ "openingColdWaveScript",
				AdventureProgression::HasOpeningColdWaveScript(board->mLevel) },
			{ "openingColdWavePlanInitialized",
				board->IsOpeningColdWavePlanInitialized() },
			{ "coldWaveForecastActive", board->HasColdWaveForecast() },
			{ "coldWaveForecastDisrupted", board->IsColdWaveForecastDisrupted() },
			{ "coldWaveDisruptionVisible",
				board->IsColdWaveForecastDisruptionVisible() },
			{ "coldWaveActive", board->IsColdWaveActive() },
			{ "coldWaveDisplayText", gs->GetColdWavePanelText() },
			{ "thermometerVisible", board->mBoardState == BoardState::GAME
				&& board->SupportsWinterTemperature() },
			{ "thermometerMinimumTenths", static_cast<int>(std::lround(
				board->GetWinterMinimumTemperatureC() * 10.0f)) },
			{ "thermometerMaximumTenths", static_cast<int>(std::lround(
				board->GetWinterMaximumTemperatureC() * 10.0f)) },
			{ "thermometerFreezingTenths", static_cast<int>(std::lround(
				board->GetWinterFreezingTemperatureC() * 10.0f)) },
			{ "thermometerFillPct", static_cast<int>(std::lround(
				board->GetWinterTemperatureGaugeRatio() * 100.0f)) },
			{ "temperatureTenths", static_cast<int>(std::lround(
				board->GetAmbientTemperatureC() * 10.0f)) },
			{ "phase", ColdWavePhaseName(board->GetColdWavePhase()) },
			{ "strength", ColdWaveStrengthName(board->GetColdWaveStrength()) },
			{ "targetTemperatureTenths", static_cast<int>(std::lround(
				board->GetColdWaveTargetTemperatureC() * 10.0f)) },
			{ "coolingDurationMs", static_cast<int>(std::lround(
				board->GetColdWaveCoolingDuration() * 1000.0f)) },
			{ "holdDurationMs", static_cast<int>(std::lround(
				board->GetColdWaveHoldDuration() * 1000.0f)) },
			{ "thawDurationMs", static_cast<int>(std::lround(
				board->GetColdWaveThawDuration() * 1000.0f)) },
			{ "frostVariant", board->GetWinterFrostVariant() },
			{ "phaseRemainingMs", static_cast<int>(std::lround(
				board->GetColdWaveTimer() * 1000.0f)) },
			{ "frozenColumns", board->GetFrozenColumnCount() },
			{ "forecastFrozenColumns", board->GetForecastFrozenColumnCount() },
			{ "frostVisualColumnsOn1000", static_cast<int>(std::lround(
				board->GetWinterFrostVisualColumnCount() * 1000.0f)) },
			{ "firstFrozenColumn", board->GetFirstFrozenColumn() },
			{ "snowing", board->IsWinterPrecipitationSnow() },
			{ "typhoonSupported", board->SupportsTyphoon() },
			{ "precipitationEffect", board->GetRainVisualEffectName() },
		};
		nlohmann::json snowHoles = nlohmann::json::array();
		for (int row = 0; row < board->mRows; ++row) {
			snowHoles.push_back({
				{ "row", row },
				{ "column", board->GetSnowHoleColumn(row) },
				{ "phase", SnowHolePhaseName(board->GetSnowHolePhase(row)) },
				{ "remainingMs", static_cast<int>(std::lround(
					board->GetSnowHoleTimer(row) * 1000.0f)) },
			});
		}
		out["weather"]["polarNight"] = {
			{ "supported", board->SupportsPolarNightEnvironment() },
			{ "initialized", board->IsPolarNightInitialized() },
			{ "phase", PolarNightPhaseName(board->GetPolarNightPhase()) },
			{ "temperatureTenths", static_cast<int>(std::lround(
				board->GetPolarTemperatureC() * 10.0f)) },
			{ "humidityTenths", static_cast<int>(std::lround(
				board->GetPolarHumidityPercent() * 10.0f)) },
			{ "windTenths", static_cast<int>(std::lround(
				board->GetPolarWindSpeedMps() * 10.0f)) },
			{ "targetTemperatureTenths", static_cast<int>(std::lround(
				board->GetPolarTargetTemperatureC() * 10.0f)) },
			{ "targetHumidityTenths", static_cast<int>(std::lround(
				board->GetPolarTargetHumidityPercent() * 10.0f)) },
			{ "targetWindTenths", static_cast<int>(std::lround(
				board->GetPolarTargetWindSpeedMps() * 10.0f)) },
			{ "temperatureDanger", board->IsPolarTemperatureDangerous() },
			{ "humidityDanger", board->IsPolarHumidityDangerous() },
			{ "windDanger", board->IsPolarWindDangerous() },
			{ "windVisualPct", static_cast<int>(std::lround(
				board->GetPolarWindVisualStrength() * 100.0f)) },
			{ "verticalWind", VerticalWindDirectionName(
				board->GetPolarVerticalWindDirection()) },
			{ "allDangerRemainingMs", static_cast<int>(std::lround(
				std::max(0.0f, 5.0f - board->GetPolarAllDangerTimer()) * 1000.0f)) },
			{ "whiteoutCommitted", board->IsPolarWhiteoutCommitted() },
			{ "snowBlind", board->IsPolarSnowBlindActive() },
			{ "whiteoutVisualPct", static_cast<int>(std::lround(
				board->GetPolarWhiteoutVisualStrength() * 100.0f)) },
			{ "whiteoutRemainingMs", static_cast<int>(std::lround(
				board->GetPolarWhiteoutTimer() * 1000.0f)) },
			{ "gaugeFluctuationRemainingMs", static_cast<int>(std::lround(
				board->GetPolarFluctuationRemaining() * 1000.0f)) },
			{ "activeHoleCount", board->GetActiveSnowHoleCount() },
			{ "lastHoleBatchCreated", board->GetLastSnowHoleBatchCreated() },
			{ "pendingSpawnCount", board->GetPendingSnowHoleSpawnCount() },
			{ "finalWaveUpgradeApplied", board->IsPolarFinalWaveUpgradeApplied() },
			{ "holes", std::move(snowHoles) },
		};
		nlohmann::json runoffRows = nlohmann::json::array();
		int firstRunoffRow = -1;
		for (int row = 0; row < board->mRows; ++row) {
			if (!board->IsRoofRunoffRowSelected(row)) continue;
			if (firstRunoffRow < 0) firstRunoffRow = row;
			runoffRows.push_back(row);
		}
		out["weather"]["roofRunoff"] = {
			{ "supported", board->SupportsRoofRunoff() },
			{ "chargePct", static_cast<int>(std::lround(
				board->GetRoofRunoffChargeRatio() * 100.0f)) },
			{ "retainedChargePct", static_cast<int>(std::lround(
				board->GetRoofRunoffRetainedCharge())) },
			{ "phase", RoofRunoffPhaseName(board->GetRoofRunoffPhase()) },
			{ "rowMask", board->GetRoofRunoffRowMask() },
			{ "rowCount", board->GetRoofRunoffRowCount() },
			{ "rows", runoffRows },
			{ "guideCandidateRow", board->GetRoofRunoffGuideCandidateRow() },
			{ "guideCandidateSelected", board->IsRoofRunoffRowSelected(
				board->GetRoofRunoffGuideCandidateRow()) },
			{ "phaseRemainingMs", static_cast<int>(std::lround(
				board->GetRoofRunoffPhaseTimer() * 1000.0f)) },
			{ "flowProgressPct", static_cast<int>(std::lround(
				board->GetRoofRunoffFlowProgress() * 100.0f)) },
			{ "zombieDriftSpeed", static_cast<int>(std::lround(
				board->GetRoofRunoffZombieDriftVelocity(
					firstRunoffRow, board->GetRoofSlopeEndX() - 1.0f))) },
		};
		out["weather"]["nightRoofCharge"] = {
			{ "supported", board->SupportsNightRoofCharge() },
			{ "chargePct", static_cast<int>(std::lround(
				board->GetNightRoofChargeRatio() * 100.0f)) },
			{ "overchargePct", static_cast<int>(std::lround(
				board->GetNightRoofOvercharge())) },
			{ "phase", NightRoofChargePhaseName(board->GetNightRoofChargePhase()) },
			{ "row", board->GetNightRoofChargeRow() },
			{ "guided", board->IsNightRoofChargeGuided() },
			{ "guideID", board->GetNightRoofChargeGuideID() },
			{ "guideCandidateCount", board->mEntityRegistry.GetActiveNightRoofChargeGuideCount() },
			{ "routeUsedMonteCarlo", board->DidNightRoofChargeRouteUseMonteCarlo() },
			{ "routeDecisionMicros", board->GetNightRoofChargeRouteDecisionMicros() },
			{ "routeRolloutCount", board->GetNightRoofChargeRouteStats().rolloutCount },
			{ "routeCandidateCount", board->GetNightRoofChargeRouteStats().candidateCount },
			{ "routeSampledZombieCount", board->GetNightRoofChargeRouteStats().sampledZombieCount },
			{ "routeSampledPlantCount", board->GetNightRoofChargeRouteStats().sampledPlantCount },
			{ "routeBestScore", board->GetNightRoofChargeRouteStats().bestScore },
			{ "phaseRemainingMs", static_cast<int>(std::lround(
				board->GetNightRoofChargePhaseTimer() * 1000.0f)) },
			{ "dischargeProgressPct", static_cast<int>(std::lround(
				board->GetNightRoofChargeDischargeProgress() * 100.0f)) },
			{ "hijackerSelectionAttempted", board->HasNightRoofHijackerSelectionAttempted() },
			{ "hijackerID", board->GetNightRoofHijackerID() },
			{ "hijackerCandidateCount", board->mEntityRegistry.GetActiveNightRoofHijackerCount() },
			{ "hijackerRainChargeBonusPerSecondOn1000", static_cast<int>(std::lround(
				board->GetNightRoofHijackerRainChargeBonusPerSecond() * 1000.0f)) },
			{ "hijackerWarningExtended", board->IsNightRoofHijackerWarningExtended() },
			{ "hijackerFinalizing", board->IsNightRoofHijackerFinalizing() },
			{ "executionLine", board->GetNightRoofExecutionLine() },
			{ "executionLineVisible", gs->IsNightRoofExecutionLineVisible() },
			{ "hijackerPulseAlpha", static_cast<int>(std::lround(
				board->GetNightRoofHijackerPulseAlpha())) },
			{ "hijackersSpawnedThisWave", board->GetHijackersSpawnedThisWave() },
		};
	}
	out["prompts"] = {
		{ "activeCount", static_cast<int>(gs->GetPromptsForTesting().size()) },
		{ "entries", nlohmann::json::array() },
	};
	for (const PromptAnimation& prompt : gs->GetPromptsForTesting()) {
		out["prompts"]["entries"].push_back({
			{ "kind", prompt.contentType == PromptContentType::IMAGE ? "IMAGE" : "TEXT" },
			{ "isHeavyRainWarning",
				prompt.purpose == PromptPurpose::HEAVY_RAIN_WARNING },
			{ "content", prompt.content },
			{ "fontSize", prompt.fontSize },
			{ "colorR", static_cast<int>(std::lround(prompt.textColor.r)) },
			{ "colorG", static_cast<int>(std::lround(prompt.textColor.g)) },
			{ "colorB", static_cast<int>(std::lround(prompt.textColor.b)) },
			{ "usesUnscaledTime", prompt.useUnscaledTime },
			{ "appearDurationMs", static_cast<int>(std::lround(
				prompt.appearDuration * 1000.0f)) },
			{ "holdDurationMs", static_cast<int>(std::lround(
				prompt.holdDuration * 1000.0f)) },
			{ "fadeDurationMs", static_cast<int>(std::lround(
				prompt.fadeDuration * 1000.0f)) },
			{ "totalDurationMs", static_cast<int>(std::lround(
				(prompt.appearDuration + prompt.holdDuration
					+ prompt.fadeDuration) * 1000.0f)) },
		});
	}

	out["survivalRound"] = board->mIsSurvival ? board->mSurvivalRound : -1;
	out["loadRestoreActive"] = board->IsLoadRestoreActive();
	out["eliteScaredyShroomsPlanted"] = board->GetEliteScaredyShroomsPlanted();
	// 出怪池不分模式都 dump：冒险关卡验证 spawnlists.json 也要抓手（原先只在生存模式导出）
	out["spawnList"] = nlohmann::json::array();
	bool spawnListHasBobsledTeam = false;
	bool spawnListHasIceWallEngineer = false;
	bool spawnListHasIceCrackDrill = false;
	bool spawnListHasWeatherJammer = false;
	bool spawnListHasIceStatueExecutioner = false;
	bool spawnListHasGildedZamboni = false;
	for (ZombieType t : board->GetSpawnZombieList()) {
		out["spawnList"].push_back(ZombieTypeName(t));
		spawnListHasBobsledTeam = spawnListHasBobsledTeam
			|| t == ZombieType::ZOMBIE_BOBSLED_TEAM;
		spawnListHasIceWallEngineer = spawnListHasIceWallEngineer
			|| t == ZombieType::ZOMBIE_ICE_WALL_ENGINEER;
		spawnListHasIceCrackDrill = spawnListHasIceCrackDrill
			|| t == ZombieType::ZOMBIE_ICE_CRACK_DRILL;
		spawnListHasWeatherJammer = spawnListHasWeatherJammer
			|| t == ZombieType::ZOMBIE_WEATHER_JAMMER;
		spawnListHasIceStatueExecutioner = spawnListHasIceStatueExecutioner
			|| t == ZombieType::ZOMBIE_ICE_STATUE_EXECUTIONER;
		spawnListHasGildedZamboni = spawnListHasGildedZamboni
			|| t == ZombieType::ZOMBIE_GILDED_ZAMBONI;
	}
	out["spawnTypeCount"] = static_cast<int>(board->GetSpawnZombieList().size());
	out["spawnListHasBobsledTeam"] = spawnListHasBobsledTeam;
	out["spawnListHasIceWallEngineer"] = spawnListHasIceWallEngineer;
	out["spawnListHasIceCrackDrill"] = spawnListHasIceCrackDrill;
	out["spawnListHasWeatherJammer"] = spawnListHasWeatherJammer;
	out["spawnListHasIceStatueExecutioner"] = spawnListHasIceStatueExecutioner;
	out["spawnListHasGildedZamboni"] = spawnListHasGildedZamboni;
	out["survivalPoolGildedZamboniEligible"] = board->CanZombieTypeEnterSurvivalPool(
		ZombieType::ZOMBIE_GILDED_ZAMBONI, board->mSurvivalRound);
	out["survivalPoolZamboniEligible"] = board->CanZombieTypeEnterSurvivalPool(
		ZombieType::ZOMBIE_ZAMBONI, board->mSurvivalRound);
	out["survivalPoolSnowBurrowEligible"] = board->CanZombieTypeEnterSurvivalPool(
		ZombieType::ZOMBIE_SNOW_BURROW, board->mSurvivalRound);
	out["survivalPoolAdaptiveHelmetEligible"] = board->CanZombieTypeEnterSurvivalPool(
		ZombieType::ZOMBIE_ADAPTIVE_HELMET, board->mSurvivalRound);
	out["survivalPoolThermalSniperEligible"] = board->CanZombieTypeEnterSurvivalPool(
		ZombieType::ZOMBIE_THERMAL_SNIPER, board->mSurvivalRound);
	out["survivalPoolAuroraPriestEligible"] = board->CanZombieTypeEnterSurvivalPool(
		ZombieType::ZOMBIE_AURORA_PRIEST, board->mSurvivalRound);
	out["survivalPoolPolarClockmakerEligible"] = board->CanZombieTypeEnterSurvivalPool(
		ZombieType::ZOMBIE_POLAR_CLOCKMAKER, board->mSurvivalRound);

	int charredZombieCount = 0;
	int zamboniCharredCount = 0;
	int catapultCharredCount = 0;
	int gargantuarCharredCount = 0;
	int impCharredCount = 0;
	int jalapenoFireCount = 0;
	std::vector<Vector> jalapenoFirePositions;
	int mistFuelVisualCount = 0;
	for (const auto& object : GameObjectManager::GetInstance().GetAllGameObjects()) {
		if (object && object->IsActive() && dynamic_cast<ZombieCharred*>(object.get())) {
			++charredZombieCount;
		}
		if (object && object->IsActive() && dynamic_cast<ZamboniCharred*>(object.get())) {
			++zamboniCharredCount;
		}
		if (object && object->IsActive() && dynamic_cast<CatapultCharred*>(object.get())) {
			++catapultCharredCount;
		}
		if (object && object->IsActive() && dynamic_cast<GargantuarCharred*>(object.get())) {
			++gargantuarCharredCount;
		}
		if (object && object->IsActive() && dynamic_cast<ImpCharred*>(object.get())) {
			++impCharredCount;
		}
		if (object && object->IsActive() && object->GetTag() == "JalapenoFire") {
			++jalapenoFireCount;
			if (auto* fire = dynamic_cast<AnimatedObject*>(object.get())) {
				jalapenoFirePositions.push_back(fire->GetVisualPosition());
			}
		}
		if (object && object->IsActive() && object->GetName() == "MistFuel") {
			++mistFuelVisualCount;
		}
	}
	out["charredZombieCount"] = charredZombieCount;
	out["zamboniCharredCount"] = zamboniCharredCount;
	out["catapultCharredCount"] = catapultCharredCount;
	out["gargantuarCharredCount"] = gargantuarCharredCount;
	out["impCharredCount"] = impCharredCount;
	out["jalapenoFireCount"] = jalapenoFireCount;
	std::sort(jalapenoFirePositions.begin(), jalapenoFirePositions.end(),
		[](const Vector& lhs, const Vector& rhs) { return lhs.x < rhs.x; });
	out["jalapenoFirePath"] = nlohmann::json::array();
	for (const Vector& position : jalapenoFirePositions) {
		out["jalapenoFirePath"].push_back({
			{ "xInt", static_cast<int>(std::lround(position.x)) },
			{ "yInt", static_cast<int>(std::lround(position.y)) },
		});
	}
	out["mistFuelVisualCount"] = mistFuelVisualCount;
	out["zamboniExplosionParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ZamboniExplosion") : 0;
	out["zamboniSmokeParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ZamboniSmoke") : 0;
	out["zamboniTireParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ZamboniTire") : 0;
	out["catapultExplosionParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("CatapultExplosion") : 0;
	out["eliteCatapultExplosionParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("EliteCatapultExplosion") : 0;
	out["cooldownZombieSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_COOLDOWNZOMBIE);
	out["snowPeaSparklesSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_SNOW_PEA_SPARKLES);
	out["caltropTirePopSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BALLOON_POP);
	out["basketballSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BASKETBALL);
	out["gargantuarThumpSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_GARGANTUAR_THUMP);
	out["gargantuarGroanSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_LOWGROAN)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_LOWGROAN2);
	out["gargantuarSwingSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_SWING);
	out["impVoiceSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_IMP)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_IMP2);
	out["gargantuarDeathSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_GARGANTUDEATH);
	out["impHeadParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ImpZombieHeadOff") : 0;
	out["impArmParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ImpZombieArmOff") : 0;
	out["bobsledHeadParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ZombieBobsledHeadOff") : 0;
	out["bobsledArmParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ZombieBobsledArmOff") : 0;
	out["bobsledPlantImpactParticleCount"] = g_particleSystem
		? g_particleSystem->GetEffectActiveParticleCount("ZombieBobsledPlantImpact") : 0;
	out["puffSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_PUFF);
	out["firePeaSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_FIREPEA);
	out["igniteSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_IGNITE);
	out["kernelImpactSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_KERNELPULT)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_KERNELPULT2);
	out["butterImpactSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_BUTTER);
	out["melonImpactSoundRequestCount"] =
		AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_MELONIMPACT)
		+ AudioSystem::GetSoundPlayRequestCount(ResourceKeys::Sounds::SOUND_MELONIMPACT2);
	out["iceTrails"] = nlohmann::json::array();
	out["goldenIceTrails"] = nlohmann::json::array();
	for (int row = 0; row < board->mRows; ++row) {
		int startCol = -1;
		int goldenStartCol = -1;
		for (int col = 0; col < board->mColumns; ++col) {
			if (board->IsIceAt(row, col)) {
				startCol = col;
			}
			if (board->IsGoldenIceAtWorld(row, board->GetCellCenterPosition(row, col).x)) {
				goldenStartCol = col;
			}
			if (startCol >= 0 && goldenStartCol >= 0) {
				break;
			}
		}
		out["iceTrails"].push_back({
			{ "row", row },
			{ "active", board->GetIceTrailTimeRemaining(row) > 0.0f },
			{ "minXInt", static_cast<int>(std::lround(board->GetIceTrailMinX(row))) },
			{ "rightXInt", static_cast<int>(std::lround(board->GetIceTrailRightX())) },
			{ "remainingMs", static_cast<int>(std::lround(
				board->GetIceTrailTimeRemaining(row) * 1000.0f)) },
			{ "startCol", startCol },
			{ "leftTopYInt", static_cast<int>(std::lround(
				board->GetRowCenterYAtX(row, board->GetIceTrailMinX(row))
				- board->GetCellHeight() * 0.5f + 20.0f)) },
			{ "slopeEndTopYInt", static_cast<int>(std::lround(
				board->GetRowCenterYAtX(row, board->GetRoofSlopeEndX())
				- board->GetCellHeight() * 0.5f + 20.0f)) },
		});
		out["goldenIceTrails"].push_back({
			{ "row", row },
			{ "active", board->GetGoldenIceTrailTimeRemaining(row) > 0.0f },
			{ "minXInt", static_cast<int>(std::lround(board->GetGoldenIceTrailMinX(row))) },
			{ "rightXInt", static_cast<int>(std::lround(board->GetIceTrailRightX())) },
			{ "remainingMs", static_cast<int>(std::lround(
				board->GetGoldenIceTrailTimeRemaining(row) * 1000.0f)) },
			{ "startCol", goldenStartCol },
		});
	}

	// 半透明植物预览来自落点幽灵；鼠标跟随预览保持不透明，分开导出便于验证卡片状态传递。
	int cellPlantPreviewCount = 0;
	int plantPreviewCount = 0;
	out["cellPlantPreview"] = nullptr;
	out["plantPreview"] = nullptr;
	// 记录已提交列表中的稳定实体 ID 顺序，用于比较悬停前后已有植物的遮挡关系。
	out["plantDrawOrder"] = nlohmann::json::array();
	for (const auto& object : GameObjectManager::GetInstance().GetAllGameObjects()) {
		auto* preview = object ? dynamic_cast<Plant*>(object.get()) : nullptr;
		if (!preview || !preview->IsActive()) continue;
		if (!preview->IsPreview()) {
			out["plantDrawOrder"].push_back(preview->mPlantID);
			continue;
		}
		const Vector pos = preview->GetPosition();
		nlohmann::json previewState = {
			{ "type", PlantTypeName(preview->mPlantType) },
			{ "imitated", preview->IsImitated() },
			{ "renderOrder", preview->GetRenderOrder() },
			{ "xInt", static_cast<int>(std::lround(pos.x)) },
			{ "yInt", static_cast<int>(std::lround(pos.y)) },
		};
		if (auto* blover = dynamic_cast<Blover*>(preview)) {
			previewState["blowDirection"] =
				WindDirectionName(blover->GetBlowDirection());
			const auto animator = blover->GetAnimatorInternal();
			previewState["flipX"] = animator && animator->GetFlipX();
		}
		if (preview->GetAlpha() < 0.5f) {
			++cellPlantPreviewCount;
			out["cellPlantPreview"] = std::move(previewState);
		}
		else {
			if (CardSlotManager* cardManager = gs->GetCardSlotManager()) {
				previewState["renderProbeReady"] =
					cardManager->IsPreviewRenderProbeReady();
				previewState["renderMouseOffsetXInt"] =
					cardManager->GetPreviewRenderMouseOffsetX();
				previewState["renderMouseOffsetYInt"] =
					cardManager->GetPreviewRenderMouseOffsetY();
			}
			++plantPreviewCount;
			out["plantPreview"] = std::move(previewState);
		}
	}
	out["cellPlantPreviewCount"] = cellPlantPreviewCount;
	out["plantPreviewCount"] = plantPreviewCount;

	out["ladders"] = nlohmann::json::array();
	for (int row = 0; row < board->mRows; ++row) {
		for (int column = 0; column < board->mColumns; ++column) {
			Ladder* ladder = board->GetLadderAt(row, column);
			if (!ladder) continue;
			const Vector center = ladder->GetVisualCenter();
			const Vector gridMoveOffset = ladder->GetGridMoveVisualOffset();
			Plant* host = board->GetTopPlantAt(row, column);
			const Vector hostGridMoveOffset = host
				? host->GetGridMoveVisualOffset() : Vector::zero();
			out["ladders"].push_back({
				{ "row", row },
				{ "col", column },
				{ "style", ladder->GetStyleName() },
				{ "textureKey", ladder->GetTextureKey() },
				{ "visualCenterXInt", static_cast<int>(std::lround(center.x)) },
				{ "visualCenterYInt", static_cast<int>(std::lround(center.y)) },
				{ "attachedToPlant", host != nullptr },
				{ "gridMoveVisualOffsetXOn1000", static_cast<int>(std::lround(
					gridMoveOffset.x * 1000.0f)) },
				{ "gridMoveVisualOffsetYOn1000", static_cast<int>(std::lround(
					gridMoveOffset.y * 1000.0f)) },
				{ "attachedPlantOffsetErrorXOn1000", static_cast<int>(std::lround(
					(gridMoveOffset.x - hostGridMoveOffset.x) * 1000.0f)) },
				{ "attachedPlantOffsetErrorYOn1000", static_cast<int>(std::lround(
					(gridMoveOffset.y - hostGridMoveOffset.y) * 1000.0f)) },
			});
		}
	}
	out["ladderCount"] = static_cast<int>(out["ladders"].size());
	out["iceWall"] = nullptr;
	if (IceWall* wall = board->GetIceWall()) {
		out["iceWall"] = {
			{ "row", wall->GetRow() },
			{ "centerXInt", static_cast<int>(std::lround(wall->GetCenterX())) },
			{ "health", wall->GetHealth() },
			{ "maxHealth", wall->GetMaxHealth() },
			{ "constructionComplete", wall->IsConstructionComplete() },
			{ "builderZombieID", wall->GetBuilderZombieID() },
			{ "thawDamageRemainderOn1000", static_cast<int>(std::lround(
				wall->GetThawDamageRemainder() * 1000.0f)) },
			{ "aimXInt", static_cast<int>(std::lround(
				wall->GetProjectileAimPosition().x)) },
			{ "aimYInt", static_cast<int>(std::lround(
				wall->GetProjectileAimPosition().y)) },
		};
	}

	out["zombies"] = nlohmann::json::array();
	out["zombiesByType"] = nlohmann::json::object();
	out["bobsledTeam"] = nlohmann::json::array();
	out["jack"] = nullptr;
	out["eliteJack"] = nullptr;
	int jackZombieCount = 0;
	int eliteJackZombieCount = 0;
	int poolRowZombieCount = 0;
	int earlyWavePoolZombieCount = 0;
	int zamboniCount = 0;
	int zamboniRowMask = 0;
	int gildedZamboniCount = 0;
	int diggerZombieCount = 0;
	int eliteDiggerZombieCount = 0;
	int snowBurrowZombieCount = 0;
	int normalZombieCount = 0;
	int adaptiveHelmetZombieCount = 0;
	int thermalSniperZombieCount = 0;
	int auroraPriestZombieCount = 0;
	int polarClockmakerZombieCount = 0;
	int adaptedHelmetZombieCount = 0;
	int elitePogoZombieCount = 0;
	int eliteLadderZombieCount = 0;
	int eliteCatapultZombieCount = 0;
	int gargantuarZombieCount = 0;
	int redEyeGargantuarZombieCount = 0;
	int impZombieCount = 0;
	int roofMarshalZombieCount = 0;
	int healerZombieCount = 0;
	int bobsledTeamZombieCount = 0;
	int bobsledRidingCount = 0;
	int bobsledLandingCount = 0;
	int bobsledWalkingCount = 0;
	int bobsledRowMask = 0;
	float bobsledMinX = std::numeric_limits<float>::max();
	float bobsledMaxX = std::numeric_limits<float>::lowest();
	int healerWave3Count = 0;
	int roofMarshalAssaultBoostedZombieCount = 0;
	int roofMarshalAssaultBoostedRowMask = 0;
	int roofMarshalAssaultFlagAnimatorCount = 0;
	int roofMarshalAssaultFlagVisibleCount = 0;
	int roofMarshalAssaultMoveMultiplierPctMax = 100;
	int roofMarshalAssaultBiteMultiplierPctMax = 100;
	int zombieBodyHealthTotal = 0;
	int zombieShieldHealthTotal = 0;
	int slowedZombieCount = 0;
	int toxicZombieCount = 0;
	int fireResistantZombieCount = 0;
	const GargantuarZombie* releasedGargantuar = nullptr;
	const ImpZombie* thrownImp = nullptr;
	for (int id : board->mEntityRegistry.GetAllZombieIDs()) {
		Zombie* z = board->mEntityRegistry.GetZombie(id);
		// Die() 会立即把对象标为 inactive，EntityRegistry 的 weak 引用到下一次清理才过期；
		// 状态快照只导出画面上仍存在的实体，才能验证冰车等“本帧直接消失”的死亡契约。
		if (!z || !z->IsActive()) continue;
		if (board->IsPoolRow(z->mRow)) {
			++poolRowZombieCount;
			if (z->mSpawnWave >= 1 && z->mSpawnWave <= 4) {
				++earlyWavePoolZombieCount;
			}
		}
		if (z->mZombieType == ZombieType::ZOMBIE_ZAMBONI) {
			++zamboniCount;
			if (z->mRow >= 0 && z->mRow < board->mRows) {
				zamboniRowMask |= 1 << z->mRow;
			}
		}
		if (z->mZombieType == ZombieType::ZOMBIE_GILDED_ZAMBONI) {
			++gildedZamboniCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_DIGGER) {
			++diggerZombieCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_ELITE_DIGGER) {
			++eliteDiggerZombieCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_SNOW_BURROW) {
			++snowBurrowZombieCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_NORMAL) ++normalZombieCount;
		if (z->mZombieType == ZombieType::ZOMBIE_ADAPTIVE_HELMET) {
			++adaptiveHelmetZombieCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_THERMAL_SNIPER) {
			++thermalSniperZombieCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_AURORA_PRIEST) ++auroraPriestZombieCount;
		if (z->mZombieType == ZombieType::ZOMBIE_POLAR_CLOCKMAKER) ++polarClockmakerZombieCount;
		if (z->mZombieType == ZombieType::ZOMBIE_ELITE_CATAPULT) {
			++eliteCatapultZombieCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_GARGANTUAR) ++gargantuarZombieCount;
		if (z->mZombieType == ZombieType::ZOMBIE_REDEYE_GARGANTUAR) {
			++redEyeGargantuarZombieCount;
		}
		if (z->mZombieType == ZombieType::ZOMBIE_IMP) ++impZombieCount;
		if (z->mZombieType == ZombieType::ZOMBIE_ROOF_MARSHAL) ++roofMarshalZombieCount;
		if (z->IsRoofMarshalAssaultActive()) {
			++roofMarshalAssaultBoostedZombieCount;
			if (z->mRow >= 0 && z->mRow < board->mRows) {
				roofMarshalAssaultBoostedRowMask |= 1 << z->mRow;
			}
			roofMarshalAssaultMoveMultiplierPctMax = std::max(
				roofMarshalAssaultMoveMultiplierPctMax, static_cast<int>(std::lround(
					z->GetRoofMarshalAssaultMoveMultiplier() * 100.0f)));
			roofMarshalAssaultBiteMultiplierPctMax = std::max(
				roofMarshalAssaultBiteMultiplierPctMax, static_cast<int>(std::lround(
					z->GetRoofMarshalAssaultBiteMultiplier() * 100.0f)));
		}
		if (z->HasRoofMarshalAssaultFlagAnimator()) {
			++roofMarshalAssaultFlagAnimatorCount;
		}
		if (z->IsRoofMarshalAssaultFlagVisible()) {
			++roofMarshalAssaultFlagVisibleCount;
		}
		zombieBodyHealthTotal += z->mBodyHealth;
		zombieShieldHealthTotal += z->mShieldHealth;
		if (z->GetCooldownTimer() > 0.0f) ++slowedZombieCount;
		if (z->GetToxinLayerCount() > 0) ++toxicZombieCount;
		if (z->IsFireResistant()) ++fireResistantZombieCount;
		const Vector pos = z->GetPosition();
		const float terrainY = board->GetZombieSpawnY(z->mRow, pos.x);
		const auto anim = z->GetAnimatorInternal();
		const auto* zombieShadow = z->GetShadow();
		float colliderCenterX = pos.x;
		if (const ColliderComponent* collider = z->GetColliderComponent()) {
			const SDL_FRect bounds = collider->GetBoundingBox();
			colliderCenterX = bounds.x + bounds.w * 0.5f;
		}
		const float horizontalMoveSpeed = z->GetCurrentHorizontalMoveSpeed();
		const float targetLeadDistance = std::fabs(
			z->GetTargetLeadX(1.2f) - colliderCenterX);
		const bool bodyTrackGlowing = anim && anim->GetGlowEffectEnabled();
		// bit0=本体/头盔/飞行额外生命，bit1=二类护盾；逻辑与实际渲染各导出一份。
		const int hitFlashMask = (z->IsBodyHitFlashing() ? 1 : 0)
			| (z->IsShieldHitFlashing() ? 2 : 0);
		const int renderedHitGlowMask = (bodyTrackGlowing ? 1 : 0)
			| (z->IsShieldTrackGlowing() ? 2 : 0);
		nlohmann::json zombieState = {
			{ "id", id },
			{ "type", ZombieTypeName(z->mZombieType) },
			{ "row", z->mRow },
			{ "spawnWave", z->mSpawnWave },
			{ "renderOrder", z->GetRenderOrder() },
			{ "renderLayer", static_cast<int>(z->GetLayer()) },
			{ "x", pos.x }, { "y", pos.y },
			{ "xInt", static_cast<int>(std::lround(pos.x)) },
			{ "yInt", static_cast<int>(std::lround(pos.y)) },
			{ "gridColumnOn1000", static_cast<int>(std::lround(
				(pos.x - CELL_INITALIZE_POS_X) / CELL_COLLIDER_SIZE_X * 1000.0f)) },
			{ "terrainYOffsetOn1000", static_cast<int>(std::lround(
				(pos.y - terrainY) * 1000.0f)) },
			{ "bodyHealth", z->mBodyHealth }, { "bodyMaxHealth", z->mBodyMaxHealth },
			{ "canBeCharred", z->CanBeCharred() },
			{ "canBeCharmed", z->CanBeCharmed() },
			{ "canBeKilledByMower", z->CanBeKilledByMower() },
			{ "resistsTangleKelpDrowning", z->ResistsTangleKelpDrowning() },
			{ "countableExecutionHealth", z->GetCountableExecutionHealth() },
			{ "countableMaxHealth", z->GetCountableMaxHealth() },
			{ "hijackerDoomed", board->IsZombieThreatenedByNightRoofHijacker(z) },
			{ "groundHazardEligible", z->CanBeAffectedByGroundHazards() },
			{ "canBeParalyzed", z->CanBeParalyzed() },
			{ "roofRunoffGuideEligible", z->CanGuideRoofRunoff() },
			{ "roofRunoffDriftMultiplierOn1000", static_cast<int>(std::lround(
				z->GetRoofRunoffDriftMultiplier() * 1000.0f)) },
			{ "roofRunoffDriftVelocity", static_cast<int>(std::lround(
				board->GetRoofRunoffZombieDriftVelocity(z->mRow, pos.x)
				* z->GetRoofRunoffDriftMultiplier())) },
			{ "attackDamage", z->mAttackDamage },
			{ "currentBiteDamage", z->GetCurrentBiteDamage() },
			{ "roofMarshalAssaultTimerMs", static_cast<int>(std::lround(
				z->GetRoofMarshalAssaultTimer() * 1000.0f)) },
			{ "roofMarshalAssaultMoveMultiplierPct", static_cast<int>(std::lround(
				z->GetRoofMarshalAssaultMoveMultiplier() * 100.0f)) },
			{ "roofMarshalAssaultBiteMultiplierPct", static_cast<int>(std::lround(
				z->GetRoofMarshalAssaultBiteMultiplier() * 100.0f)) },
			{ "roofMarshalAssaultFlagAnimator", z->HasRoofMarshalAssaultFlagAnimator() },
			{ "roofMarshalAssaultFlagVisible", z->IsRoofMarshalAssaultFlagVisible() },
			{ "helmType", ZombieHelmTypeName(z->mHelmType) },
			{ "helmHealth", z->mHelmHealth }, { "shieldHealth", z->mShieldHealth },
			{ "helmMaxHealth", z->mHelmMaxHealth }, { "shieldMaxHealth", z->mShieldMaxHealth },
			{ "fireResistant", z->IsFireResistant() },
			{ "mindControlled", z->IsMindControlled() },
			{ "mineTargetCell", z->mMineTargetCell },
			{ "mineTargetReturning", z->mMineTargetReturning },
			{ "mineTargetOffsetXOn1000", z->mMineTargetCell < 0 ? 0 : static_cast<int>(std::lround(
				(board->GetCellCenterPosition(z->mMineTargetCell / board->mColumns,
					z->mMineTargetCell % board->mColumns).x - pos.x) * 1000.0f)) },
			{ "mineTargetOffsetYOn1000", z->mMineTargetCell < 0 ? 0 : static_cast<int>(std::lround(
				(board->GetZombieSpawnY(z->mMineTargetCell / board->mColumns, pos.x) - pos.y) * 1000.0f)) },
			{ "mineRowOffsetOn1000", static_cast<int>(std::lround((z->GetPosition().y
				- board->GetZombieSpawnY(z->mRow, z->GetPosition().x)) * 1000.0f)) },
			{ "mistFuelReward", static_cast<int>(std::lround(z->GetMistFuelReward())) },
			{ "fogObscured", board->IsZombieObscuredByFog(z) },
			{ "inPool", z->IsInPool() },
			{ "isEating", z->IsEating() },
			{ "isDying", z->IsDying() },
			{ "eatPlantID", z->GetEatingPlantID() },
			{ "garlicRedirecting", z->IsGarlicRedirecting() },
			{ "garlicRedirectElapsedMs", static_cast<int>(std::lround(
				z->GetGarlicRedirectElapsed() * 1000.0f)) },
			{ "garlicRowChanged", z->HasGarlicChangedRow() },
			{ "garlicYuckFaceVisible", z->IsGarlicYuckFaceVisible() },
			{ "ladderClimbPhase", LadderClimbPhaseName(z->GetLadderClimbPhase()) },
			{ "ladderAltitudeOn1000", static_cast<int>(std::lround(
				z->GetLadderAltitude() * 1000.0f)) },
			{ "useLadderColumn", z->GetUseLadderColumn() },
			{ "hasHead", z->HasHead() }, { "hasArm", z->HasArm() },
			{ "flying", z->IsFlying() },
			{ "slowCooldown", z->GetCooldownTimer() },
			// slowed 供 assert_state（bool 可 equals）；slowCooldown 浮点仅供肉眼核对勿断言
			{ "slowed", z->GetCooldownTimer() > 0.0f },
			// frozen 供 assert_state（bool 可 equals）；frozenTimer 浮点仅供肉眼核对勿断言
			{ "frozen", z->IsFrozen() },
			{ "frozenTimer", z->GetFrozenTimer() },
			{ "buttered", z->IsButtered() },
			{ "butterTimerMs", static_cast<int>(std::lround(
				z->GetButterTimer() * 1000.0f)) },
			{ "paralyzed", z->IsParalyzed() },
			{ "paralysisTimerMs", static_cast<int>(std::lround(
				z->GetParalysisTimeRemaining() * 1000.0f)) },
			{ "controlImmunityMask", static_cast<std::uint32_t>(
				z->GetActiveControlImmunityMask()) },
			{ "slowImmunityTimerMs", static_cast<int>(std::lround(
				z->GetControlImmunityTimeRemaining(
					ZombieControlEffect::SLOW) * 1000.0f)) },
			{ "frozenImmunityTimerMs", static_cast<int>(std::lround(
				z->GetControlImmunityTimeRemaining(
					ZombieControlEffect::FROZEN) * 1000.0f)) },
			{ "butterImmunityTimerMs", static_cast<int>(std::lround(
				z->GetControlImmunityTimeRemaining(
					ZombieControlEffect::BUTTER) * 1000.0f)) },
			{ "paralysisImmunityTimerMs", static_cast<int>(std::lround(
				z->GetControlImmunityTimeRemaining(
					ZombieControlEffect::PARALYSIS) * 1000.0f)) },
			{ "nightRoofChargeGuideType", z->IsNightRoofChargeGuideType() },
			{ "nightRoofChargeGuideEligible", z->CanGuideNightRoofCharge() },
			{ "butterSplatTrack", z->GetButterSplatTrackName() },
			{ "butterSplatAfterAllTracks", z->ShouldDrawButterSplatAfterAllTracks() },
			{ "butterSplatFollowerConfigured", z->IsButterSplatFollowerConfigured() },
			{ "butterSplatFollowerVisible", z->IsButterSplatFollowerVisible() },
			{ "butterSplatFollowerInheritsOverlay",
				z->DoesButterSplatFollowerInheritOverlayEffect() },
			{ "butterSplatFollowerGlowing", z->IsButterSplatFollowerGlowing() },
			{ "toxic", z->GetToxinLayerCount() > 0 },
			{ "toxinStacks", z->GetToxinLayerCount() },
			{ "toxinMaxRemainingMs", static_cast<int>(std::lround(
				z->GetToxinMaxRemaining() * 1000.0f)) },
			{ "toxinDamageRemainderOn1000", static_cast<int>(std::lround(
				z->GetToxinDamageRemainder() * 1000.0f)) },
			{ "track", z->GetCurrentTrackName() },
			{ "animFrame", anim ? anim->GetCurrentFrame() : -1 },
			{ "flipX", anim && anim->GetFlipX() },
			{ "animPlaying", anim && anim->IsPlaying() },
			{ "animPlayState", PlayStateName(z->GetPlayingState()) },
			{ "animExtraSpeedPct", anim
				? static_cast<int>(std::lround(anim->GetExtraSpeedMultiplier() * 100.0f)) : 0 },
			{ "effectiveAnimSpeed", anim ? anim->EffectiveSpeed() : 0.0f },
			{ "animationScalePct", static_cast<int>(std::lround(
				z->GetAnimationScale() * 100.0f)) },
			{ "shadowScaleXPct", zombieShadow ? static_cast<int>(std::lround(
				zombieShadow->GetScale().x * 100.0f)) : 0 },
			{ "shadowScaleYPct", zombieShadow ? static_cast<int>(std::lround(
				zombieShadow->GetScale().y * 100.0f)) : 0 },
			{ "horizontalMoveSpeedOn1000", static_cast<int>(std::lround(
				horizontalMoveSpeed * 1000.0f)) },
			{ "targetLeadDistance1200On1000", static_cast<int>(std::lround(
				targetLeadDistance * 1000.0f)) },
			{ "goldenIceSpeedActive", z->IsGoldenIceSpeedActive() },
			{ "goldenIceEffectStacks", z->GetGoldenIceEffectStacks() },
			{ "freeHitsRemaining", z->mFreeHitsRemaining },
			{ "fogBreakoutTriggered", z->HasTriggeredFogBreakout() },
			{ "armorBreakRushSpent", z->HasSpentArmorBreakRush() },
			{ "armorBreakRushTimerMs", static_cast<int>(std::lround(
				z->GetArmorBreakRushTimeRemaining() * 1000.0f)) },
			{ "bodyHitFlashing", z->IsBodyHitFlashing() },
			{ "shieldHitFlashing", z->IsShieldHitFlashing() },
			{ "bodyTrackGlowing", bodyTrackGlowing },
			{ "shieldTrackGlowing", z->IsShieldTrackGlowing() },
			{ "hitFlashMask", hitFlashMask },
			{ "renderedHitGlowMask", renderedHitGlowMask },
			{ "tangleKelpTarget", z->IsTangleKelpTarget() },
			{ "hasMagneticItem", z->HasMagneticItem() },
			{ "tangleKelpPlantID", z->GetTangleKelpPlantID() },
			{ "draggedUnderByTangleKelp", z->IsDraggedUnderByTangleKelp() },
			{ "tangleKelpSinkOffsetOn1000", static_cast<int>(std::lround(
				z->GetTangleKelpSinkOffset() * 1000.0f)) },
			{ "tangleKelpGrabFrameOn1000", static_cast<int>(std::lround(
				z->GetTangleKelpGrabFrame() * 1000.0f)) },
			{ "magneticItemAvailable", z->CanBeTargetedByMagnetShroom() },
			{ "colliderEnabled", z->GetColliderComponent()
				&& z->GetColliderComponent()->mEnabled },
			// 时间回溯不仅要恢复 hasHead，还必须撤销 HeadDrop 留在 Animator 上的隐藏覆盖。
			{ "headVisible", anim && anim->GetTrackVisible("anim_head1") },
			// 铁门僵尸常规手臂（藏门后/啃食露出）当前可见性——手臂显隐类 bug 的断言抓手；
			// 无此轨道的僵尸 GetTrackVisible 安全返回 false。
			{ "armVisible", anim && anim->GetTrackVisible("Zombie_outerarm_hand") },
			// 铁门僵尸「持门手臂」（Zombie_*_screendoor*）可见性：死亡/断臂时本应随门消失。
			{ "doorArmVisible", anim && (anim->GetTrackVisible("Zombie_outerarm_screendoor")
				|| anim->GetTrackVisible("Zombie_innerarm_screendoor")
				|| anim->GetTrackVisible("Zombie_innerarm_screendoor_hand")) },
			{ "doorVisible", anim && anim->GetTrackVisible("anim_screendoor") },
			{ "coneVisible", anim && anim->GetTrackVisible("anim_cone") },
			{ "bucketVisible", anim && anim->GetTrackVisible("anim_bucket") },
			{ "footballHelmetVisible", anim && anim->GetTrackVisible("zombie_football_helmet") },
		};
		// 读取品种的正式持久状态，核对回溯后的破损阶段，避免只验血量漏掉出生贴图。
		nlohmann::json equipmentState = nlohmann::json::object();
		z->SaveExtraData(equipmentState);
		zombieState["helmStage"] = equipmentState.value("helmStage", -1);
		zombieState["shieldStage"] = equipmentState.value("shieldStage", -1);
		if (auto* thermal = dynamic_cast<ThermalSniperZombie*>(z)) {
			zombieState["thermalSniperPhase"] = static_cast<int>(thermal->GetSniperPhase());
			zombieState["thermalReloadRemainingMs"] = static_cast<int>(std::lround(
				thermal->GetReloadRemaining() * 1000.0f));
			zombieState["thermalAimRemainingMs"] = static_cast<int>(std::lround(
				thermal->GetAimRemaining() * 1000.0f));
			zombieState["thermalLockedPlantID"] = thermal->GetLockedPlantID();
		}
		if (auto* priest = dynamic_cast<AuroraPriestZombie*>(z)) {
			zombieState["auroraRitualReleaseCount"] = priest->GetRitualReleaseCount();
			zombieState["auroraRitualPhase"] = static_cast<int>(priest->GetRitualPhase());
			zombieState["auroraRitualRemainingMs"] = static_cast<int>(std::lround(
				priest->GetRitualRemaining() * 1000.0f));
			zombieState["auroraOverloaded"] = priest->IsOverloaded();
			zombieState["auroraFollowersConfigured"] = priest->HasFinaleFollowersConfigured();
			zombieState["auroraDeviceTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_AURORA_DEVICE, false) != nullptr;
		}
		if (auto* clock = dynamic_cast<PolarClockmakerZombie*>(z)) {
			zombieState["clockPhase"] = static_cast<int>(clock->GetClockPhase());
			zombieState["clockRemainingMs"] = static_cast<int>(std::lround(
				clock->GetClockRemaining() * 1000.0f));
			zombieState["clockFollowersConfigured"] = clock->HasFinaleFollowersConfigured();
			zombieState["clockDiskTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_ZOMBIE_POLAR_CLOCK_DISK, false) != nullptr;
			zombieState["clockFaceTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_TEMPORALCLOCKFACE, false) != nullptr;
			zombieState["clockHourHandTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_TEMPORALCLOCKHOURHAND, false) != nullptr;
			zombieState["clockMinuteHandTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Particles::PARTICLE_TEMPORALCLOCKMINUTEHAND, false) != nullptr;
		}
		if (auto* elite = dynamic_cast<EliteDancerZombie*>(z)) {
			zombieState["eliteBackupCount"] = elite->GetActiveBackupCount();
			zombieState["eliteSummonRemainingMs"] = static_cast<int>(std::lround(
				elite->GetSummonTimer() * 1000.0f));
			zombieState["eliteTyphoonSpeedPct"] = static_cast<int>(std::lround(
				elite->GetTyphoonAbilitySpeedMultiplier() * 100.0f));
		}
		if (auto* insulator = dynamic_cast<InsulatorZombie*>(z)) {
			zombieState["insulatorWet"] = insulator->IsWet();
			zombieState["insulatorWetTimerMs"] = static_cast<int>(std::lround(
				insulator->GetWetTimeRemaining() * 1000.0f));
			zombieState["insulatorOverloaded"] = insulator->IsOverloaded();
			zombieState["insulatorOverloadTimerMs"] = static_cast<int>(std::lround(
				insulator->GetOverloadTimeRemaining() * 1000.0f));
			zombieState["insulatorArmorStage"] = static_cast<int>(
				insulator->GetArmorStage());
			zombieState["insulatorArmorFollower"] = insulator->HasArmorFollower();
			zombieState["insulatorArmorVisible"] = insulator->IsArmorVisible();
			zombieState["insulatorArmorTexture1Loaded"] =
				ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZOMBIE_INSULATOR_ARMOR1, false) != nullptr;
			zombieState["insulatorArmorTexture2Loaded"] =
				ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZOMBIE_INSULATOR_ARMOR2, false) != nullptr;
			zombieState["insulatorArmorTexture3Loaded"] =
				ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZOMBIE_INSULATOR_ARMOR3, false) != nullptr;
		}
		if (auto* hijacker = dynamic_cast<HijackerZombie*>(z)) {
			zombieState["hijackerPhase"] = static_cast<int>(hijacker->GetPhase());
			zombieState["hijackerCandidate"] =
				hijacker->CanBeNightRoofHijackerCandidate();
			zombieState["hijackerLocked"] =
				board->GetNightRoofHijackerID() == hijacker->mZombieID;
		}
		if (auto* worker = dynamic_cast<IceWorkerZombie*>(z)) {
			zombieState["iceRemainingMs"] = static_cast<int>(std::lround(worker->GetIceRemaining() * 1000.0f));
			zombieState["nextIceYieldOn1000"] = static_cast<int>(std::lround(worker->GetNextIceYield() * 1000.0f));
			zombieState["iceBatches"] = worker->GetIceBatches();
			zombieState["iceMachineVisible"] = worker->HasIceMachine();
		}
		if (auto* healer = dynamic_cast<HealerZombie*>(z)) {
			++healerZombieCount;
			if (healer->mSpawnWave == 3) ++healerWave3Count;
			zombieState["healerTreatmentState"] =
				HealerTreatmentStateName(healer->GetTreatmentState());
			zombieState["healerCooldownMs"] = static_cast<int>(std::lround(
				healer->GetHealCooldownRemaining() * 1000.0f));
			zombieState["healerRetryMs"] = static_cast<int>(std::lround(
				healer->GetRetryRemaining() * 1000.0f));
			zombieState["healerCastRemainingMs"] = static_cast<int>(std::lround(
				healer->GetCastRemaining() * 1000.0f));
			zombieState["healerFocusedTargetID"] = healer->GetFocusedTargetID();
			zombieState["healerLastTargetCount"] = healer->GetLastHealTargetCount();
			zombieState["healerLastTotalAmount"] = healer->GetLastHealTotalAmount();
			zombieState["healerStrategicWaitMs"] = static_cast<int>(std::lround(
				healer->GetStrategicWaitElapsed() * 1000.0f));
			zombieState["healerDecisionMode"] =
				HealerDecisionModeName(healer->GetLastDecisionMode());
			zombieState["healerDecisionAction"] =
				HealerDecisionActionName(healer->GetLastDecisionAction());
			zombieState["healerMonteCarloRolloutCount"] =
				healer->GetLastMonteCarloRolloutCount();
			zombieState["healerMonteCarloCandidateCount"] =
				healer->GetLastMonteCarloCandidateCount();
			zombieState["healerMonteCarloZombieCount"] =
				healer->GetLastMonteCarloZombieCount();
			zombieState["healerMonteCarloCardCount"] =
				healer->GetLastMonteCarloCardCount();
			zombieState["healerMonteCarloBestScoreOn100"] =
				static_cast<int>(std::lround(
					healer->GetLastMonteCarloBestScore() * 100.0f));
			zombieState["healerDisabled"] = healer->IsHealingPermanentlyDisabled();
			zombieState["healerGearFollower"] = healer->HasTreatmentGearFollower();
			zombieState["healerGearVisible"] = healer->IsTreatmentGearVisible();
			zombieState["healerGearTextureKey"] =
				healer->GetTreatmentGearTextureKey();
		}
		if (auto* eliteLadder = dynamic_cast<EliteLadderZombie*>(z)) {
			++eliteLadderZombieCount;
			zombieState["eliteLadderScanComplete"] = eliteLadder->IsRowScanComplete();
			zombieState["eliteLadderScanRemainingMs"] = static_cast<int>(std::lround(
				eliteLadder->GetRowScanTimeRemaining() * 1000.0f));
			zombieState["eliteLadderScannedPlantHealth"] =
				eliteLadder->GetScannedPlantHealth();
			zombieState["eliteLadderScannedPultCount"] =
				eliteLadder->GetScannedPultCount();
			zombieState["eliteLadderScannedShooterCount"] =
				eliteLadder->GetScannedShooterCount();
			zombieState["eliteLadderInfinite"] =
				eliteLadder->HasInfiniteLadderAbility();
			zombieState["eliteLadderDoubleAnim"] =
				eliteLadder->HasDoubledAnimationSpeed();
			zombieState["eliteLadderBodyBonus"] =
				eliteLadder->HasBodyHealthBonus();
			zombieState["eliteLadderShieldDoubled"] =
				eliteLadder->HasDoubledShieldHealth();
		}
		if (auto* ladder = dynamic_cast<LadderZombie*>(z)) {
			zombieState["ladderPhase"] = ladder->GetPhaseName();
			zombieState["ladderShieldStage"] = static_cast<int>(ladder->GetShieldStage());
			zombieState["ladderTextureKey"] = ladder->GetCurrentLadderTextureKey();
			zombieState["ladderPlacementRow"] = ladder->GetPlacementRow();
			zombieState["ladderPlacementColumn"] = ladder->GetPlacementColumn();
			zombieState["ladderTrackVisible"] = anim
				&& anim->GetTrackVisible("Zombie_ladder_1");
		}
		if (auto* polevaulter = dynamic_cast<Polevaulter*>(z)) {
			const char* vaultState = "RUNNING";
			if (polevaulter->mVaultState == Polevaulter::VaultState::JUMPING) {
				vaultState = "JUMPING";
			}
			else if (polevaulter->mVaultState == Polevaulter::VaultState::WALKING) {
				vaultState = "WALKING";
			}
			zombieState["vaultState"] = vaultState;
			zombieState["hasVaulted"] = polevaulter->mHasVaulted;
			zombieState["lastVaultDistanceOn1000"] = static_cast<int>(std::lround(
				polevaulter->GetLastVaultDistance() * 1000.0f));
			zombieState["vaultExtraDistanceAppliedOn1000"] = static_cast<int>(std::lround(
				polevaulter->GetVaultExtraDistanceApplied() * 1000.0f));
			zombieState["vaultProgressOn1000"] = static_cast<int>(std::lround(
				polevaulter->GetVaultProgress() * 1000.0f));
			zombieState["vaultBlockChecked"] = polevaulter->HasCheckedVaultBlock();
			zombieState["vaultHasTarget"] = polevaulter->HasVaultTarget();
		}
		if (auto* dolphin = dynamic_cast<DolphinRiderZombie*>(z)) {
			const char* phase = "APPROACHING";
			switch (dolphin->GetPhase()) {
			case DolphinRiderZombie::Phase::APPROACHING: phase = "APPROACHING"; break;
			case DolphinRiderZombie::Phase::ENTERING_POOL: phase = "ENTERING_POOL"; break;
			case DolphinRiderZombie::Phase::RIDING: phase = "RIDING"; break;
			case DolphinRiderZombie::Phase::JUMPING: phase = "JUMPING"; break;
			case DolphinRiderZombie::Phase::SWIMMING: phase = "SWIMMING"; break;
			case DolphinRiderZombie::Phase::WALKING_WITHOUT_DOLPHIN:
				phase = "WALKING_WITHOUT_DOLPHIN";
				break;
			}
			const Vector visualCompensation = dolphin->GetDolphinVisualCompensation();
			zombieState["dolphinPhase"] = phase;
			zombieState["hasDolphin"] = dolphin->HasDolphin();
			zombieState["dolphinEntrySplashPlayed"] = dolphin->HasPlayedEntrySplash();
			zombieState["dolphinEntryProgressOn1000"] = static_cast<int>(std::lround(
				dolphin->GetEntryProgress() * 1000.0f));
			zombieState["dolphinJumpBlockChecked"] = dolphin->HasCheckedJumpBlocker();
			zombieState["successfulDolphinJumps"] = dolphin->GetSuccessfulDolphinJumps();
			zombieState["dolphinJumpCapacity"] = dolphin->GetDolphinJumpCapacity();
			zombieState["dolphinJumpProgressOn1000"] = static_cast<int>(std::lround(
				dolphin->GetJumpProgress() * 1000.0f));
			zombieState["dolphinVisualCompensationXOn1000"] =
				static_cast<int>(std::lround(visualCompensation.x * 1000.0f));
			zombieState["dolphinVisualCompensationYOn1000"] =
				static_cast<int>(std::lround(visualCompensation.y * 1000.0f));
		}
		if (auto* pogo = dynamic_cast<PogoZombie*>(z)) {
			const Vector visual = pogo->GetVisualPosition();
			zombieState["pogoPhase"] = PogoPhaseName(pogo->GetPhase());
			zombieState["hasPogo"] = pogo->HasPogo();
			zombieState["pogoBounceRemainingMs"] = static_cast<int>(std::lround(
				pogo->GetBounceRemaining() * 1000.0f));
			zombieState["pogoBounceProgressOn1000"] = static_cast<int>(std::lround(
				pogo->GetBounceProgress() * 1000.0f));
			zombieState["pogoAltitudeOn1000"] = static_cast<int>(std::lround(
				pogo->GetPogoAltitude() * 1000.0f));
			zombieState["pogoVisualLiftOn1000"] = static_cast<int>(std::lround(
				(pos.y - visual.y) * 1000.0f));
			zombieState["pogoJumpBlockChecked"] = pogo->HasCheckedJumpBlocker();
			zombieState["pogoForwardDistanceOn1000"] = static_cast<int>(std::lround(
				pogo->GetForwardDistanceTotal() * 1000.0f));
			zombieState["pogoForwardAppliedOn1000"] = static_cast<int>(std::lround(
				pogo->GetForwardDistanceApplied() * 1000.0f));
			zombieState["pogoGlassesVisible"] =
				anim && anim->GetTrackVisible("anim_head_glasses");
			zombieState["pogoStickVisible"] = anim
				&& (anim->GetTrackVisible("Zombie_pogo_stick")
					|| anim->GetTrackVisible("Zombie_pogo_stick2"));
		}
		if (auto* elitePogo = dynamic_cast<ElitePogoZombie*>(z)) {
			++elitePogoZombieCount;
			zombieState["elitePogoImpactBufferAvailable"] = elitePogo->HasImpactBuffer();
		}
		if (auto* bungee = dynamic_cast<BungeeZombie*>(z)) {
			const MonteCarloTargetStats& stats = bungee->GetMonteCarloStats();
			zombieState["bungeePhase"] = BungeePhaseName(bungee->GetPhase());
			zombieState["bungeeTargetMode"] =
				BungeeTargetModeName(bungee->GetTargetMode());
			zombieState["bungeeAltitudeInt"] = static_cast<int>(std::lround(
				bungee->GetAltitude()));
			zombieState["bungeePhaseTimerMs"] = static_cast<int>(std::lround(
				bungee->GetPhaseTimer() * 1000.0f));
			zombieState["bungeeTargetRow"] = bungee->GetTargetRow();
			zombieState["bungeeTargetColumn"] = bungee->GetTargetColumn();
			zombieState["bungeeTargetPlantID"] = bungee->GetTargetPlantID();
			zombieState["bungeeTargetInitialized"] = bungee->HasSelectedTarget();
			zombieState["bungeeTargetableGround"] =
				bungee->CanBeTargetedByProjectile(false);
			zombieState["bungeeMonteCarloRolloutCount"] = stats.rolloutCount;
			zombieState["bungeeMonteCarloCandidateCount"] = stats.candidateCount;
			zombieState["bungeeMonteCarloZombieCount"] = stats.sampledZombieCount;
			zombieState["bungeeMonteCarloPlantCount"] = stats.sampledPlantCount;
			zombieState["bungeeMonteCarloSupportCount"] = stats.supportPlantCount;
			zombieState["bungeeMonteCarloCardCount"] = stats.cardCount;
			zombieState["bungeeMonteCarloBestScoreOn100"] =
				static_cast<int>(std::lround(stats.bestScore * 100.0f));
		}
		if (auto* eliteJack = dynamic_cast<EliteJackInTheBoxZombie*>(z)) {
			++eliteJackZombieCount;
			zombieState["jackPhase"] = JackPhaseName(eliteJack->GetPhase());
			const Vector boxPosition = eliteJack->GetThrownBoxPosition();
			const Vector targetPosition = eliteJack->GetThrowTargetPosition();
			zombieState["eliteJackThrowCountdownMs"] =
				static_cast<int>(std::lround(
					eliteJack->GetThrowCountdown() * 1000.0f));
			zombieState["eliteJackBoxInFlight"] = eliteJack->IsBoxInFlight();
			zombieState["eliteJackBoxFlightProgressOn1000"] =
				static_cast<int>(std::lround(
					eliteJack->GetBoxFlightProgress() * 1000.0f));
			zombieState["eliteJackBoxXInt"] =
				static_cast<int>(std::lround(boxPosition.x));
			zombieState["eliteJackBoxYInt"] =
				static_cast<int>(std::lround(boxPosition.y));
			zombieState["eliteJackTargetRow"] =
				eliteJack->GetThrowTargetRow();
			zombieState["eliteJackTargetXInt"] =
				static_cast<int>(std::lround(targetPosition.x));
			zombieState["eliteJackTargetYInt"] =
				static_cast<int>(std::lround(targetPosition.y));
			zombieState["eliteJackThrowWasMindControlled"] =
				eliteJack->WasThrownByMindControlledZombie();
			zombieState["eliteJackTargetingMode"] =
				EliteJackTargetingModeName(
					eliteJack->GetLastPlantTargetingMode());
			zombieState["eliteJackMonteCarloRolloutCount"] =
				eliteJack->GetLastMonteCarloRolloutCount();
			zombieState["eliteJackMonteCarloCandidateCount"] =
				eliteJack->GetLastMonteCarloCandidateCount();
			zombieState["eliteJackMonteCarloZombieCount"] =
				eliteJack->GetLastMonteCarloZombieCount();
			zombieState["eliteJackMonteCarloCardCount"] =
				eliteJack->GetLastMonteCarloCardCount();
			zombieState["eliteJackMonteCarloCoordinationLossOn100"] =
				static_cast<int>(std::lround(
					eliteJack->GetLastMonteCarloCoordinationLoss() * 100.0f));
			zombieState["eliteJackBoxTrackVisible"] =
				anim && anim->GetTrackVisible("Zombie_jackbox_box");
			zombieState["jackHandleTrackVisible"] =
				anim && anim->GetTrackVisible("Zombie_jackbox_handle");
			zombieState["jackRunVelocityOn1000"] =
				static_cast<int>(std::lround(
					eliteJack->GetRunVelocity() * 1000.0f));
			out["eliteJack"] = zombieState;
		}
		else if (auto* jack = dynamic_cast<JackInTheBoxZombie*>(z)) {
			++jackZombieCount;
			zombieState["jackPhase"] = JackPhaseName(jack->GetPhase());
			zombieState["jackPopCountdownMs"] = static_cast<int>(std::lround(
				jack->GetPopCountdown() * 1000.0f));
			zombieState["jackRunVelocityOn1000"] = static_cast<int>(std::lround(
				jack->GetRunVelocity() * 1000.0f));
			zombieState["jackSurprisePlayed"] = jack->HasPlayedSurprise();
			zombieState["jackExplosionResolved"] = jack->HasResolvedExplosion();
			zombieState["jackHead1Visible"] =
				anim && anim->GetTrackVisible("anim_head1");
			zombieState["jackHead2Visible"] =
				anim && anim->GetTrackVisible("anim_head2");
			zombieState["jackLowerArmVisible"] =
				anim && anim->GetTrackVisible("zombie_jackbox_outerarm_lower");
			zombieState["jackBoxTrackVisible"] =
				anim && anim->GetTrackVisible("Zombie_jackbox_box");
			zombieState["jackHandleTrackVisible"] =
				anim && anim->GetTrackVisible("Zombie_jackbox_handle");
			out["jack"] = zombieState;
		}
		if (auto* balloon = dynamic_cast<BalloonZombie*>(z)) {
			const char* phase = "FLYING";
			switch (balloon->GetPhase()) {
			case BalloonZombie::Phase::FLYING: phase = "FLYING"; break;
			case BalloonZombie::Phase::POPPING: phase = "POPPING"; break;
			case BalloonZombie::Phase::WALKING: phase = "WALKING"; break;
			}
			zombieState["balloonPhase"] = phase;
			zombieState["balloonHealth"] = balloon->GetBalloonHealth();
			zombieState["balloonMaxHealth"] = balloon->GetBalloonMaxHealth();
			zombieState["balloonFlightVelocityOn1000"] =
				static_cast<int>(std::lround(balloon->GetFlightVelocity() * 1000.0f));
			zombieState["balloonBloverBlowing"] = balloon->IsBloverBlowing();
			zombieState["balloonBloverDirection"] =
				WindDirectionName(balloon->GetBloverBlowDirection());
			zombieState["balloonBloverRemainingInt"] =
				static_cast<int>(std::lround(balloon->GetBloverBlowRemaining()));
			zombieState["balloonPropellerFrameOn1000"] =
				static_cast<int>(std::lround(balloon->GetPropellerFrame() * 1000.0f));
			zombieState["balloonPropellerPlaying"] = balloon->IsPropellerPlaying();
			zombieState["balloonHatVisible"] =
				anim && anim->GetTrackVisible("hat");
		}
		if (auto* digger = dynamic_cast<DiggerZombie*>(z)) {
			const auto* shadow = z->GetShadow();
			zombieState["diggerPhase"] = DiggerPhaseName(digger->GetPhase());
			zombieState["diggerHasPickaxe"] = digger->HasPickaxe();
			zombieState["diggerSurpriseShown"] = digger->HasShownSurprise();
			zombieState["diggerMovingRight"] = digger->IsMovingRight();
			zombieState["diggerPickaxeWalkVelocityOn1000"] = static_cast<int>(
				std::lround(digger->GetPickaxeWalkVelocityValue() * 1000.0f));
			zombieState["diggerCanTriggerGameOver"] = digger->CanTriggerGameOver();
			zombieState["diggerTargetableGround"] =
				digger->CanBeTargetedByProjectile(false);
			zombieState["diggerPhaseRemainingMs"] = static_cast<int>(std::lround(
				digger->GetPhaseRemaining() * 1000.0f));
			zombieState["diggerAltitudeOn1000"] = static_cast<int>(std::lround(
				digger->GetAltitude() * 1000.0f));
			zombieState["diggerPickaxeVisible"] =
				anim && anim->GetTrackVisible("Zombie_digger_pickaxe");
			zombieState["diggerDirtVisible"] =
				anim && anim->GetTrackVisible("Zombie_digger_dirt");
			zombieState["diggerHardhatVisible"] =
				anim && anim->GetTrackVisible("Zombie_digger_hardhat");
			zombieState["diggerHeadEyeVisible"] =
				anim && anim->GetTrackVisible("Zombie_digger_head_eye");
			zombieState["diggerShadowVisible"] = shadow && shadow->IsVisible();
			zombieState["diggerGroundTrackVisible"] =
				anim && anim->GetTrackVisible("_ground");
			if (auto* eliteDigger = dynamic_cast<EliteDiggerZombie*>(digger)) {
				zombieState["eliteDiggerBlastResolved"] =
					eliteDigger->HasResolvedBlast();
			}
		}
		if (auto* adaptive = dynamic_cast<AdaptiveHelmetZombie*>(z)) {
			if (adaptive->HasAdapted()) ++adaptedHelmetZombieCount;
			const PlantDamageOrigin origin = adaptive->GetAdaptedOrigin();
			zombieState["adaptiveHasAdapted"] = adaptive->HasAdapted();
			zombieState["adaptiveOriginKind"] = static_cast<int>(origin.kind);
			zombieState["adaptiveOriginLineage"] = static_cast<int>(origin.lineage);
			zombieState["adaptiveHelmetVisible"] =
				adaptive->IsAdaptiveHelmetVisible();
			zombieState["adaptiveBadgeVisible"] =
				adaptive->IsAdaptedBadgeVisible();
			zombieState["adaptiveHelmetTextureLoaded"] =
				ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZOMBIE_ADAPTIVE_HELMET, false) != nullptr;
			zombieState["adaptiveBadgeTextureLoaded"] =
				ResourceManager::GetInstance().GetTexture(
					ResourceKeys::Textures::IMAGE_ZOMBIE_ADAPTIVE_BADGE, false) != nullptr;
		}
		if (auto* snowBurrow = dynamic_cast<SnowBurrowZombie*>(z)) {
			const auto* shadow = z->GetShadow();
			zombieState["snowBurrowPhase"] = snowBurrow->GetPhaseName();
			zombieState["snowBurrowPhaseRemainingMs"] = static_cast<int>(std::lround(
				snowBurrow->GetPhaseRemaining() * 1000.0f));
			zombieState["snowBurrowOriginXInt"] = static_cast<int>(std::lround(
				snowBurrow->GetBurrowOriginX()));
			zombieState["snowBurrowLimitXInt"] = static_cast<int>(std::lround(
				snowBurrow->GetBurrowLimitX()));
			zombieState["snowBurrowTargetXInt"] = static_cast<int>(std::lround(
				snowBurrow->GetBurrowTargetX()));
			zombieState["snowBurrowMaxDistanceInt"] = static_cast<int>(std::lround(
				snowBurrow->GetBurrowOriginX() - snowBurrow->GetBurrowLimitX()));
			zombieState["snowBurrowEmergenceColumn"] =
				snowBurrow->GetEmergenceColumn();
			zombieState["snowBurrowsCommitted"] = snowBurrow->GetBurrowsCommitted();
			zombieState["snowBurrowRetryRemainingMs"] = static_cast<int>(std::lround(
				snowBurrow->GetReburrowRetryRemaining() * 1000.0f));
			zombieState["snowBurrowNaturalImpactPending"] =
				snowBurrow->IsNaturalImpactPending();
			zombieState["snowBurrowTargetableGroundProjectile"] =
				snowBurrow->CanBeTargetedByProjectile(false);
			zombieState["snowBurrowGroundHazardEligible"] =
				snowBurrow->CanBeAffectedByGroundHazards();
			zombieState["snowBurrowInterruptibleRemainingMs"] = static_cast<int>(
				std::lround(snowBurrow->GetInterruptibleSpecialActionRemaining() * 1000.0f));
			zombieState["snowBurrowShadowVisible"] = shadow && shadow->IsVisible();
			zombieState["snowBurrowShovelVisible"] = anim
				&& anim->GetTrackVisible("Zombie_digger_pickaxe");
			zombieState["snowBurrowHatVisible"] = anim
				&& anim->GetTrackVisible("Zombie_digger_hardhat");
		}
		if (auto* zamboni = dynamic_cast<ZamboniZombie*>(z)) {
			zombieState["zamboniDamageStage"] = zamboni->GetDamageStage();
			zombieState["zamboniPuncturedByCaltrop"] = zamboni->IsPuncturedByCaltrop();
			zombieState["zamboniCaltropDeathRemainingMs"] = static_cast<int>(std::lround(
				zamboni->GetCaltropDeathTimer() * 1000.0f));
			zombieState["zamboniDriveSpeedOn1000"] = static_cast<int>(std::lround(
				zamboni->GetDriveSpeed() * 1000.0f));
			zombieState["zamboniDriveCoordinateBaseXInt"] = static_cast<int>(std::lround(
				zamboni->GetDriveCoordinateBaseX()));
			const Vector shakeOffset = zamboni->GetDamageShakeOffset();
			zombieState["zamboniShakeXOn1000"] =
				static_cast<int>(std::lround(shakeOffset.x * 1000.0f));
			zombieState["zamboniShakeYOn1000"] =
				static_cast<int>(std::lround(shakeOffset.y * 1000.0f));
			const Vector visualPosition = zamboni->GetVisualPosition();
			if (const auto* collider = zamboni->GetColliderComponent()) {
				const SDL_FRect bounds = collider->GetBoundingBox();
				// 同一状态下导出相对视觉原点的整数投影，避免倍速与取证帧导致绝对坐标漂移。
				zombieState["zamboniColliderFromVisualXOn1000"] =
					static_cast<int>(std::lround((bounds.x - visualPosition.x) * 1000.0f));
				zombieState["zamboniColliderFromVisualYOn1000"] =
					static_cast<int>(std::lround((bounds.y - visualPosition.y) * 1000.0f));
				zombieState["zamboniColliderWidthInt"] =
					static_cast<int>(std::lround(bounds.w));
				zombieState["zamboniColliderHeightInt"] =
					static_cast<int>(std::lround(bounds.h));
			}
			const float trailMinX = z->mZombieType == ZombieType::ZOMBIE_GILDED_ZAMBONI
				? board->GetGoldenIceTrailMinX(z->mRow)
				: board->GetIceTrailMinX(z->mRow);
			zombieState["zamboniIceFromVisualXOn1000"] = static_cast<int>(std::lround(
				(trailMinX - visualPosition.x) * 1000.0f));
		}
		if (auto* catapult = dynamic_cast<CatapultZombie*>(z)) {
			zombieState["catapultPhase"] = CatapultPhaseName(catapult->GetPhase());
			zombieState["catapultBasketballs"] = catapult->GetBasketballCount();
			zombieState["catapultDamageStage"] = catapult->GetDamageStage();
			zombieState["catapultPuncturedByCaltrop"] = catapult->IsCaltropPunctured();
			zombieState["catapultPhaseRemainingMs"] = static_cast<int>(std::lround(
				catapult->GetPhaseTimer() * 1000.0f));
			zombieState["catapultDriveSpeedOn1000"] = static_cast<int>(std::lround(
				catapult->GetDriveSpeed() * 1000.0f));
			const Vector visualPosition = catapult->GetVisualPosition();
			const Vector butterAnchor = catapult->GetButterSplatAnchor();
			const Vector iceTrapBottomAnchor = catapult->GetIceTrapBottomAnchor();
			zombieState["catapultButterAnchorFromVisualXOn1000"] = static_cast<int>(
				std::lround((butterAnchor.x - visualPosition.x) * 1000.0f));
			zombieState["catapultButterAnchorFromVisualYOn1000"] = static_cast<int>(
				std::lround((butterAnchor.y - visualPosition.y) * 1000.0f));
			zombieState["catapultIceTrapBottomFromVisualXOn1000"] = static_cast<int>(
				std::lround((iceTrapBottomAnchor.x - visualPosition.x) * 1000.0f));
			zombieState["catapultIceTrapBottomFromVisualYOn1000"] = static_cast<int>(
				std::lround((iceTrapBottomAnchor.y - visualPosition.y) * 1000.0f));
			if (const auto* collider = catapult->GetColliderComponent()) {
				const SDL_FRect bounds = collider->GetBoundingBox();
				zombieState["catapultColliderFromVisualXOn1000"] = static_cast<int>(
					std::lround((bounds.x - visualPosition.x) * 1000.0f));
				zombieState["catapultColliderFromVisualYOn1000"] = static_cast<int>(
					std::lround((bounds.y - visualPosition.y) * 1000.0f));
				zombieState["catapultColliderWidthInt"] = static_cast<int>(std::lround(bounds.w));
				zombieState["catapultColliderHeightInt"] = static_cast<int>(std::lround(bounds.h));
			}
		}
		if (auto* gargantuar = dynamic_cast<GargantuarZombie*>(z)) {
			if (!releasedGargantuar && gargantuar->HasReleasedImp()) {
				releasedGargantuar = gargantuar;
			}
			zombieState["gargantuarPhase"] = GargantuarPhaseName(gargantuar->GetPhase());
			zombieState["gargantuarHasImp"] = gargantuar->HasImp();
			zombieState["gargantuarSmashApplied"] = gargantuar->HasAppliedSmash();
			zombieState["gargantuarThrowReleased"] = gargantuar->HasReleasedImp();
			zombieState["gargantuarTargetRow"] = gargantuar->GetActionTargetRow();
			zombieState["gargantuarTargetColumn"] = gargantuar->GetActionTargetColumn();
			zombieState["gargantuarTargetZombieID"] = gargantuar->GetActionTargetZombieID();
			zombieState["gargantuarThrowDistanceInt"] = static_cast<int>(std::lround(
				gargantuar->GetThrowDistance()));
			zombieState["gargantuarWeapon"] = GargantuarWeaponName(
				gargantuar->GetWeaponVariant());
			zombieState["gargantuarDamageStage"] = gargantuar->GetDamageStage();
			zombieState["gargantuarHeadTextureKey"] =
				gargantuar->GetCurrentHeadTextureKey();
			zombieState["gargantuarBodyTextureKey"] =
				gargantuar->GetCurrentBodyTextureKey();
			zombieState["gargantuarOuterArmTextureKey"] =
				gargantuar->GetCurrentOuterArmTextureKey();
			zombieState["gargantuarFootTextureKey"] =
				gargantuar->GetCurrentFootTextureKey();
			zombieState["gargantuarHeldImpVisible"] = anim
				&& anim->GetTrackVisible("Zombie_imp_head")
				&& anim->GetTrackVisible("Zombie_imp_body1")
				&& anim->GetTrackVisible("Zombie_gargantuar_whiterope");
		}
		if (auto* imp = dynamic_cast<ImpZombie*>(z)) {
			if (!thrownImp && imp->GetPhase() == ImpZombie::Phase::THROWN) {
				thrownImp = imp;
			}
			const auto* shadow = imp->GetShadow();
			zombieState["impPhase"] = ImpPhaseName(imp->GetPhase());
			zombieState["impAltitudeOn1000"] = static_cast<int>(std::lround(
				imp->GetThrowAltitude() * 1000.0f));
			zombieState["impVerticalVelocityOn1000"] = static_cast<int>(std::lround(
				imp->GetThrowVerticalVelocity() * 1000.0f));
			zombieState["impHorizontalVelocityOn1000"] = static_cast<int>(std::lround(
				imp->GetThrowHorizontalVelocity() * 1000.0f));
			zombieState["impThrowMovingRight"] = imp->IsThrowMovingRight();
			zombieState["impShadowVisible"] = shadow && shadow->IsVisible();
			const Vector impVisual = imp->GetVisualPosition();
			const Vector impHeadAnchor = imp->GetHeadParticleAnchor();
			const Vector impArmAnchor = imp->GetArmParticleAnchor();
			zombieState["impHeadAnchorFromVisualXOn1000"] = static_cast<int>(
				std::lround((impHeadAnchor.x - impVisual.x) * 1000.0f));
			zombieState["impHeadAnchorFromVisualYOn1000"] = static_cast<int>(
				std::lround((impHeadAnchor.y - impVisual.y) * 1000.0f));
			zombieState["impArmAnchorFromVisualXOn1000"] = static_cast<int>(
				std::lround((impArmAnchor.x - impVisual.x) * 1000.0f));
			zombieState["impArmAnchorFromVisualYOn1000"] = static_cast<int>(
				std::lround((impArmAnchor.y - impVisual.y) * 1000.0f));
		}
		zombieState["drumStacks"] = z->GetDrumInspirationStacks();
		zombieState["drumMove1000"] = static_cast<int>(std::lround(z->GetDrumMoveMultiplier()*1000));
		zombieState["drumBite1000"] = static_cast<int>(std::lround(z->GetDrumBiteMultiplier()*1000));
		zombieState["amberMove1000"] = static_cast<int>(std::lround(z->GetAmberMovementMultiplier()*1000));
		z->SaveDrumInspiration(zombieState);
		if (auto* drummer = dynamic_cast<CrystalDrummerZombie*>(z)) {
			zombieState["drumWindingUp"] = drummer->IsDrumWindingUp();
			zombieState["drumRemainingMs"] = static_cast<int>(std::lround(drummer->GetDrumRemaining()*1000));
			zombieState["drumBeatCount"] = drummer->GetDrumBeatCount();
		}
		if (auto* gilded = dynamic_cast<GildedZamboniZombie*>(z)) {
			zombieState["gildedUndamagedMs"] = static_cast<int>(std::lround(
				gilded->GetUndamagedTime() * 1000.0f));
			zombieState["gildedAccelerationStage"] = gilded->GetAccelerationStage();
			zombieState["gildedAccelerationMultiplierPct"] = static_cast<int>(std::lround(
				gilded->GetAccelerationMultiplier() * 100.0f));
		}
		if (dynamic_cast<PoolNormalZombie*>(z)) {
			zombieState["poolLegsVisible"] = anim && (
				anim->GetTrackVisible("Zombie_innerleg_upper")
				|| anim->GetTrackVisible("Zombie_innerleg_lower")
				|| anim->GetTrackVisible("Zombie_innerleg_foot")
				|| anim->GetTrackVisible("Zombie_outerleg_upper")
				|| anim->GetTrackVisible("Zombie_outerleg_lower")
				|| anim->GetTrackVisible("Zombie_outerleg_foot"));
		}
		if (auto* marshal = dynamic_cast<RoofMarshalZombie*>(z)) {
			zombieState["roofMarshalCommandPhase"] = marshal->GetCommandPhaseName();
			zombieState["roofMarshalSummonTimerMs"] = static_cast<int>(std::lround(
				marshal->GetSummonTimer() * 1000.0f));
			zombieState["roofMarshalCommandPoseTimerMs"] = static_cast<int>(std::lround(
				marshal->GetCommandPoseTimer() * 1000.0f));
			zombieState["roofMarshalCommandCount"] = marshal->GetCommandCount();
			zombieState["roofMarshalLastSummonCount"] = marshal->GetLastSummonCount();
			zombieState["roofMarshalLastSummonRowMask"] = marshal->GetLastSummonRowMask();
			zombieState["roofMarshalLastSummonBossRow"] = marshal->GetLastSummonBossRow();
			zombieState["roofMarshalLastSummonDistinctRowCount"] =
				marshal->GetLastSummonDistinctRowCount();
			zombieState["roofMarshalHighThreatPoolUnlocked"] =
				marshal->IsHighThreatPoolUnlocked();
			zombieState["roofMarshalHighThreatRollPercent"] =
				marshal->GetCurrentHighThreatRollPercent();
			zombieState["roofMarshalCurrentSummonCount"] =
				marshal->GetCurrentSummonCount();
			zombieState["roofMarshalCurrentSummonIntervalMs"] = static_cast<int>(
				std::lround(marshal->GetCurrentSummonInterval() * 1000.0f));
			zombieState["roofMarshalLaneSwitchTimerMs"] = static_cast<int>(std::lround(
				marshal->GetLaneSwitchTimer() * 1000.0f));
			zombieState["roofMarshalLaneTransitionRemainingMs"] = static_cast<int>(
				std::lround(marshal->GetLaneTransitionRemaining() * 1000.0f));
			zombieState["roofMarshalLaneVisualOffsetYOn1000"] = static_cast<int>(
				std::lround(marshal->GetLaneVisualOffsetY() * 1000.0f));
			zombieState["roofMarshalLaneSwitchCount"] = marshal->GetLaneSwitchCount();
			zombieState["roofMarshalAssaultCommandCount"] = marshal->GetAssaultCommandCount();
			zombieState["roofMarshalLastAssaultRow"] = marshal->GetLastAssaultRow();
			zombieState["roofMarshalLastAssaultAffectedCount"] =
				marshal->GetLastAssaultAffectedCount();
			zombieState["roofMarshalAtLastAssaultRow"] =
				marshal->GetLastAssaultRow() >= 0
				&& marshal->mRow == marshal->GetLastAssaultRow();
			zombieState["roofMarshalButterImmunityTimerMs"] = static_cast<int>(
				std::lround(marshal->GetButterImmunityTimer() * 1000.0f));
			zombieState["roofMarshalWalkingPhase"] = marshal->IsWalkingPhase();
			zombieState["roofMarshalLastSummonAvoidedCurrentRow"] =
				marshal->GetLastSummonBossRow() < 0
				|| (marshal->GetLastSummonRowMask()
					& (1 << marshal->GetLastSummonBossRow())) == 0;
			zombieState["roofMarshalLastSummonedTypes"] = nlohmann::json::array();
			bool allAllowed = true;
			int highThreatCount = 0;
			const auto& summonedTypes = marshal->GetLastSummonedTypes();
			for (int i = 0; i < marshal->GetLastSummonCount(); ++i) {
				const ZombieType type = summonedTypes[static_cast<std::size_t>(i)];
				zombieState["roofMarshalLastSummonedTypes"].push_back(ZombieTypeName(type));
				allAllowed = allAllowed && RoofMarshalZombie::IsAllowedSummonType(type);
				if (RoofMarshalZombie::IsHighThreatSummonType(type)) ++highThreatCount;
			}
			zombieState["roofMarshalLastSummonAllAllowed"] = allAllowed;
			zombieState["roofMarshalLastSummonHighThreatCount"] = highThreatCount;
			out["roofMarshal"] = zombieState;
		}
		if (auto* bobsled = dynamic_cast<BobsledTeamZombie*>(z)) {
			++bobsledTeamZombieCount;
			bobsledMinX = std::min(bobsledMinX, pos.x);
			bobsledMaxX = std::max(bobsledMaxX, pos.x);
			if (z->mRow >= 0 && z->mRow < board->mRows) {
				bobsledRowMask |= 1 << z->mRow;
			}
			const char* phase = "RIDING";
			switch (bobsled->GetBobsledPhase()) {
			case BobsledTeamZombie::Phase::RIDING:
				++bobsledRidingCount;
				phase = "RIDING";
				break;
			case BobsledTeamZombie::Phase::LANDING:
				++bobsledLandingCount;
				phase = "LANDING";
				break;
			case BobsledTeamZombie::Phase::WALKING:
				++bobsledWalkingCount;
				phase = "WALKING";
				break;
			}
			zombieState["bobsledRole"] = bobsled->GetBobsledRole()
				== BobsledTeamZombie::Role::LEADER ? "LEADER" : "FOLLOWER";
			zombieState["bobsledPhase"] = phase;
			zombieState["bobsledSlot"] = bobsled->GetBobsledSlot();
			zombieState["bobsledLeaderID"] = bobsled->GetBobsledLeaderID();
			zombieState["bobsledLiveMemberCount"] = bobsled->GetLiveTeamMemberCount();
			zombieState["bobsledScatterContained"] = bobsled->WasScatterContained();
			zombieState["bobsledLandingMs"] = static_cast<int>(std::lround(
				bobsled->GetLandingTimeRemaining() * 1000.0f));
			zombieState["bobsledMemberIDs"] = bobsled->GetBobsledMemberIDs();
			zombieState["bobsledDetachedArmVisible"] = anim
				&& (anim->GetTrackVisible("Zombie_outerarm_lower")
					|| anim->GetTrackVisible("Zombie_outerarm_hand")
					|| anim->GetTrackVisible("Zombie_outerarm_lowereating")
					|| anim->GetTrackVisible("Zombie_outerarm_handeating"));
			zombieState["bobsledShadowOffsetYInt"] = z->GetShadow()
				? static_cast<int>(std::lround(z->GetShadow()->GetOffset().y)) : 0;
			if (const ColliderComponent* collider = z->GetColliderComponent()) {
				const SDL_FRect bounds = collider->GetBoundingBox();
				zombieState["bobsledColliderWidthInt"] =
					static_cast<int>(std::lround(bounds.w));
				zombieState["bobsledColliderHeightInt"] =
					static_cast<int>(std::lround(bounds.h));
			}
			out["bobsledTeam"].push_back(zombieState);
		}
		if (auto* engineer = dynamic_cast<IceWallEngineerZombie*>(z)) {
			const char* phase = "MOVING";
			switch (engineer->GetConstructionPhase()) {
			case IceWallEngineerZombie::ConstructionPhase::MOVING:
				break;
			case IceWallEngineerZombie::ConstructionPhase::BUILDING:
				phase = "BUILDING";
				break;
			case IceWallEngineerZombie::ConstructionPhase::COMPLETED:
				phase = "COMPLETED";
				break;
			}
			zombieState["constructionPhase"] = phase;
			zombieState["constructionRemainingMs"] = static_cast<int>(std::lround(
				engineer->GetConstructionRemaining() * 1000.0f));
			zombieState["buildWallCenterXInt"] = static_cast<int>(std::lround(
				engineer->GetBuildWallCenterX()));
			zombieState["constructionUsed"] = engineer->HasUsedConstruction();
			zombieState["interruptibleSpecialRemainingMs"] = static_cast<int>(std::lround(
				engineer->GetInterruptibleSpecialActionRemaining() * 1000.0f));
			if (const ColliderComponent* collider = engineer->GetColliderComponent();
				collider && board->GetFrozenColumnCount() > 0) {
				const SDL_FRect bounds = collider->GetBoundingBox();
				const float battlefieldRightX = CELL_INITALIZE_POS_X
					+ static_cast<float>(board->mColumns) * CELL_COLLIDER_SIZE_X;
				const float frontierX = board->GetCellCenterPosition(
					engineer->mRow, board->GetFirstFrozenColumn()).x
					- CELL_COLLIDER_SIZE_X * 0.5f;
				zombieState["fullyEnteredBattlefield"] =
					bounds.x + bounds.w <= battlefieldRightX;
				zombieState["frontEdgeToFrostFrontierInt"] =
					static_cast<int>(std::lround(bounds.x - frontierX));
			}
		}
		if (auto* drill = dynamic_cast<IceCrackDrillZombie*>(z)) {
			const char* phase = "MOVING";
			switch (drill->GetDrillPhase()) {
			case IceCrackDrillZombie::DrillPhase::MOVING:
				break;
			case IceCrackDrillZombie::DrillPhase::CHARGING:
				phase = "CHARGING";
				break;
			case IceCrackDrillZombie::DrillPhase::SPENT:
				phase = "SPENT";
				break;
			}
			zombieState["drillPhase"] = phase;
			zombieState["drillChargeRemainingMs"] = static_cast<int>(std::lround(
				drill->GetChargeRemaining() * 1000.0f));
			zombieState["drillUsed"] = drill->HasUsedDrill();
			zombieState["drillRigAnimatorReady"] = drill->HasDrillRigAnimator();
			zombieState["drillRigVisible"] = drill->IsDrillRigVisible();
			zombieState["interruptibleSpecialRemainingMs"] = static_cast<int>(std::lround(
				drill->GetInterruptibleSpecialActionRemaining() * 1000.0f));
		}
		zombieState["mineFogProtection1000"] = static_cast<int>(std::lround(board->GetMineFogProtection(z)*1000));
		zombieState["prismMarkRemainingMs"] = static_cast<int>(std::lround(z->GetPrismMarkRemaining()*1000));
		if (auto* thief = dynamic_cast<SunThiefZombie*>(z)) {
			const char* phases[] = {"READY","WINDUP","COOLDOWN","RETREAT","DISABLED"};
			const auto record = board->GetSunTheftRecord(z->mZombieID);
			zombieState["theftPhase"] = phases[static_cast<int>(thief->GetTheftPhase())];
			zombieState["theftRemainingMs"] = static_cast<int>(std::lround(thief->GetTheftRemaining()*1000));
			zombieState["theftStolen"] = record.stolen;
			zombieState["theftCarried"] = record.carried;
			zombieState["theftDisabled"] = record.disabled;
			zombieState["theftEquipmentVisible"] = anim && anim->GetTrackFollowerVisible("Zombie_body","sun_tank")
				&& anim->GetTrackFollowerVisible("Zombie_body","sun_apron")
				&& anim->GetTrackFollowerVisible("anim_innerarm2","sun_nozzle")
				&& anim->GetTrackFollowerVisible("anim_innerarm2","sun_hose");
		}
		if (auto* miner = dynamic_cast<CrystalHornMinerZombie*>(z)) {
			const char* names[] = {"READY","WINDUP","CHARGING","COOLDOWN"};
			zombieState["chargePhase"] = names[static_cast<int>(miner->GetChargePhase())];
			zombieState["chargeRemainingMs"] = static_cast<int>(std::lround(miner->GetChargeRemaining()*1000));
			zombieState["chargeTravelled1000"] = static_cast<int>(std::lround(miner->GetChargeTravelled()*1000));
			zombieState["crystalHelmetVisible"] = anim && anim->GetTrackFollowerVisible("anim_head1","crystal_horn");
		}
		if (auto* excavator = dynamic_cast<ExcavatorZombie*>(z)) {
			const char* names[] = {"READY","APPROACHING","DRILLING","RETRY","SPENT","DISABLED"};
			zombieState["excavatorPhase"] = names[static_cast<int>(excavator->GetExcavatorPhase())];
			zombieState["excavatorWall"] = excavator->GetWall();
			zombieState["excavatorCandidates"] = excavator->mLastDecision.candidates;
			zombieState["excavatorGain"] = static_cast<int>(std::lround(excavator->mLastDecision.gain));
			zombieState["excavatorBaseline"] = static_cast<int>(std::lround(excavator->mLastDecision.baseline));
			zombieState["excavatorDecisionMicros"] = excavator->mDecisionMicros;
			zombieState["excavatorStand"] = excavator->GetStand();
			zombieState["excavatorToolVisible"] = anim && anim->GetTrackFollowerVisible("anim_innerarm2","excavator_drill");
			zombieState["excavatorHatVisible"] = anim && anim->GetTrackFollowerVisible("anim_head1","excavator_hat");
			zombieState["excavatorWorkMs"] = static_cast<int>(std::lround(excavator->GetWorkRemaining()*1000));
			zombieState["excavatorRetryMs"] = static_cast<int>(std::lround(excavator->GetRetryRemaining()*1000));
		}
		if (auto* jammer = dynamic_cast<WeatherJammerZombie*>(z)) {
			const char* phase = "READY";
			switch (jammer->GetJammerPhase()) {
			case WeatherJammerZombie::JammerPhase::READY:
				break;
			case WeatherJammerZombie::JammerPhase::CHANNELING:
				phase = "CHANNELING";
				break;
			case WeatherJammerZombie::JammerPhase::REBOOTING:
				phase = "REBOOTING";
				break;
			case WeatherJammerZombie::JammerPhase::SPENT:
				phase = "SPENT";
				break;
			}
			zombieState["jammerPhase"] = phase;
			zombieState["jammerChannelRemainingMs"] = static_cast<int>(std::lround(
				jammer->GetChannelRemaining() * 1000.0f));
			zombieState["jammerRebootRemainingMs"] = static_cast<int>(std::lround(
				jammer->GetRebootRemaining() * 1000.0f));
			zombieState["jammerCommittedDisruptionMask"] =
				jammer->GetCommittedDisruptionMask();
			zombieState["jammerPackAnimatorReady"] = jammer->HasPackAnimator();
			zombieState["jammerTerminalAnimatorReady"] = jammer->HasTerminalAnimator();
			zombieState["interruptibleSpecialRemainingMs"] = static_cast<int>(std::lround(
				jammer->GetInterruptibleSpecialActionRemaining() * 1000.0f));
		}
		if (auto* executioner = dynamic_cast<IceStatueExecutionerZombie*>(z)) {
			const char* phase = "READY";
			switch (executioner->GetExecutionPhase()) {
			case IceStatueExecutionerZombie::ExecutionPhase::READY:
				break;
			case IceStatueExecutionerZombie::ExecutionPhase::EXECUTING:
				phase = "EXECUTING";
				break;
			case IceStatueExecutionerZombie::ExecutionPhase::SPENT:
				phase = "SPENT";
				break;
			}
			zombieState["executionPhase"] = phase;
			zombieState["executionTargetPlantID"] =
				executioner->GetExecutionTargetPlantID();
			zombieState["executionProgress"] = executioner->GetExecutionProgress();
			zombieState["executionRequiredStrikes"] =
				executioner->GetCurrentRequiredStrikeCount();
			zombieState["executionUsed"] = executioner->HasUsedExecution();
			const char* targetingMode = "NONE";
			switch (executioner->GetTargetingMode()) {
			case IceStatueExecutionerZombie::TargetingMode::NONE:
				break;
			case IceStatueExecutionerZombie::TargetingMode::MONTE_CARLO:
				targetingMode = "MONTE_CARLO";
				break;
			case IceStatueExecutionerZombie::TargetingMode::STRATEGIC_FALLBACK:
				targetingMode = "STRATEGIC_FALLBACK";
				break;
			}
			zombieState["executionTargetingMode"] = targetingMode;
			zombieState["executionMonteCarloRollouts"] =
				executioner->GetTargetingRolloutCount();
			zombieState["executionMonteCarloCandidates"] =
				executioner->GetTargetingCandidateCount();
			zombieState["executionMonteCarloZombies"] =
				executioner->GetTargetingZombieCount();
			zombieState["executionMonteCarloBestScoreOn100"] = static_cast<int>(
				std::lround(executioner->GetTargetingBestScore() * 100.0f));
			zombieState["executionHelmetStage"] = static_cast<int>(
				executioner->GetHelmetStage());
			zombieState["executionHelmetFollowerVisible"] =
				executioner->HasHelmetFollower();
			zombieState["executionHelmetFollowerInheritsOverlay"] =
				executioner->DoesHelmetFollowerInheritOverlayEffect();
			zombieState["executionHelmetFollowerGlowing"] =
				executioner->IsHelmetFollowerGlowing();
			const float remaining = executioner->GetInterruptibleSpecialActionRemaining();
			zombieState["interruptibleSpecialRemainingMs"] = remaining < 0.0f
				? -1 : (remaining >= 100000.0f ? std::numeric_limits<int>::max()
					: static_cast<int>(std::lround(remaining * 1000.0f)));
		}
		// 专项脚本中的异品种靶子可按语义类型稳定取证；同品种多只时仍使用 zombies 全量数组。
		out["zombiesByType"][ZombieTypeName(z->mZombieType)] = zombieState;
		out["zombies"].push_back(std::move(zombieState));
	}
	if (releasedGargantuar && thrownImp
		&& releasedGargantuar->mRow == thrownImp->mRow) {
		const Vector heldAnchor = releasedGargantuar->GetHeldImpBodyRenderAnchor();
		const Vector thrownAnchor = thrownImp->GetThrowBodyRenderAnchor();
		out["gargantuarImpReleaseBodyAnchorDxOn1000"] = static_cast<int>(std::lround(
			(thrownAnchor.x - heldAnchor.x) * 1000.0f));
		out["gargantuarImpReleaseBodyAnchorDyOn1000"] = static_cast<int>(std::lround(
			(thrownAnchor.y - heldAnchor.y) * 1000.0f));
	}
	out["zombieCount"] = static_cast<int>(out["zombies"].size());
	out["bobsledTeamZombieCount"] = bobsledTeamZombieCount;
	out["bobsledRidingCount"] = bobsledRidingCount;
	out["bobsledLandingCount"] = bobsledLandingCount;
	out["bobsledWalkingCount"] = bobsledWalkingCount;
	out["bobsledRowMask"] = bobsledRowMask;
	out["bobsledXSpanOn1000"] = bobsledTeamZombieCount > 0
		? static_cast<int>(std::lround((bobsledMaxX - bobsledMinX) * 1000.0f)) : 0;
	out["groundRifts"] = nlohmann::json::array();
	for (GroundRift* rift : board->GetGroundRifts()) {
		if (!rift || !rift->IsActive()) continue;
		out["groundRifts"].push_back({
			{ "row", rift->GetRow() },
			{ "frontXInt", static_cast<int>(std::lround(rift->GetFrontX())) },
			{ "nextColumn", rift->GetNextColumn() },
			{ "downstreamDamageMultiplierPct", static_cast<int>(std::lround(
				rift->GetDownstreamDamageMultiplier() * 100.0f)) },
		});
	}
	out["groundRiftCount"] = static_cast<int>(out["groundRifts"].size());
	int bobsledDistinctRowCount = 0;
	for (int mask = bobsledRowMask; mask != 0; mask >>= 1) {
		bobsledDistinctRowCount += mask & 1;
	}
	out["bobsledDistinctRowCount"] = bobsledDistinctRowCount;
	out["healerZombieCount"] = healerZombieCount;
	out["healerWave3Count"] = healerWave3Count;
	int healerRowIndexVisibleCount = 0;
	for (int row = 0; row < board->mRows; ++row) {
		board->mEntityRegistry.ForEachZombieInRow(row, [&](Zombie* zombie) {
			if (zombie && zombie->mZombieType == ZombieType::ZOMBIE_HEALER) {
				++healerRowIndexVisibleCount;
			}
		});
	}
	out["healerRowIndexVisibleCount"] = healerRowIndexVisibleCount;
	out["healers"] = nlohmann::json::array();
	std::vector<int> healerIDs = board->mEntityRegistry.GetAllZombieIDs();
	std::sort(healerIDs.begin(), healerIDs.end());
	for (const int id : healerIDs) {
		auto* healer = dynamic_cast<HealerZombie*>(
			board->mEntityRegistry.GetZombie(id));
		if (!healer || !healer->IsActive()) continue;
		out["healers"].push_back({
			{ "id", id },
			{ "row", healer->mRow },
			{ "spawnWave", healer->mSpawnWave },
			{ "state", HealerTreatmentStateName(healer->GetTreatmentState()) },
			{ "focusedTargetID", healer->GetFocusedTargetID() },
			{ "strategicWaitMs", static_cast<int>(std::lround(
				healer->GetStrategicWaitElapsed() * 1000.0f)) },
			{ "decisionMode", HealerDecisionModeName(healer->GetLastDecisionMode()) },
			{ "decisionAction", HealerDecisionActionName(healer->GetLastDecisionAction()) },
			{ "disabled", healer->IsHealingPermanentlyDisabled() },
			{ "gearTextureKey", healer->GetTreatmentGearTextureKey() },
		});
	}
	out["jackZombieCount"] = jackZombieCount;
	out["eliteJackZombieCount"] = eliteJackZombieCount;
	out["zamboniCount"] = zamboniCount;
	out["zamboniRowMask"] = zamboniRowMask;
	int zamboniDistinctRowCount = 0;
	for (int row = 0; row < board->mRows; ++row) {
		if ((zamboniRowMask & (1 << row)) != 0) ++zamboniDistinctRowCount;
	}
	out["zamboniDistinctRowCount"] = zamboniDistinctRowCount;
	out["gildedZamboniCount"] = gildedZamboniCount;
	out["diggerZombieCount"] = diggerZombieCount;
	out["eliteDiggerZombieCount"] = eliteDiggerZombieCount;
	out["snowBurrowZombieCount"] = snowBurrowZombieCount;
	out["normalZombieCount"] = normalZombieCount;
	out["adaptiveHelmetZombieCount"] = adaptiveHelmetZombieCount;
	out["thermalSniperZombieCount"] = thermalSniperZombieCount;
	out["auroraPriestZombieCount"] = auroraPriestZombieCount;
	out["polarClockmakerZombieCount"] = polarClockmakerZombieCount;
	out["adaptedHelmetZombieCount"] = adaptedHelmetZombieCount;
	out["elitePogoZombieCount"] = elitePogoZombieCount;
	out["eliteLadderZombieCount"] = eliteLadderZombieCount;
	out["eliteCatapultZombieCount"] = eliteCatapultZombieCount;
	out["gargantuarZombieCount"] = gargantuarZombieCount;
	out["redEyeGargantuarZombieCount"] = redEyeGargantuarZombieCount;
	out["redEyeGargantuarWeight"] = GameDataManager::GetInstance().GetZombieWeight(
		ZombieType::ZOMBIE_REDEYE_GARGANTUAR);
	out["impZombieCount"] = impZombieCount;
	out["roofMarshalZombieCount"] = roofMarshalZombieCount;
	out["roofMarshalAssaultBoostedZombieCount"] = roofMarshalAssaultBoostedZombieCount;
	out["roofMarshalAssaultBoostedRowMask"] = roofMarshalAssaultBoostedRowMask;
	out["roofMarshalAssaultFlagAnimatorCount"] = roofMarshalAssaultFlagAnimatorCount;
	out["roofMarshalAssaultFlagVisibleCount"] = roofMarshalAssaultFlagVisibleCount;
	int roofMarshalAssaultBoostedDistinctRowCount = 0;
	for (int mask = roofMarshalAssaultBoostedRowMask; mask != 0; mask >>= 1) {
		roofMarshalAssaultBoostedDistinctRowCount += mask & 1;
	}
	out["roofMarshalAssaultBoostedDistinctRowCount"] =
		roofMarshalAssaultBoostedDistinctRowCount;
	out["roofMarshalAssaultMoveMultiplierPctMax"] = roofMarshalAssaultMoveMultiplierPctMax;
	out["roofMarshalAssaultBiteMultiplierPctMax"] = roofMarshalAssaultBiteMultiplierPctMax;
	out["roofMarshalSummonWhitelist"] = nlohmann::json::array();
	for (int i = 0; i < static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES); ++i) {
		const auto type = static_cast<ZombieType>(i);
		if (RoofMarshalZombie::IsAllowedSummonType(type)) {
			out["roofMarshalSummonWhitelist"].push_back(ZombieTypeName(type));
		}
	}
	out["roofMarshalSummonWhitelistCount"] =
		static_cast<int>(out["roofMarshalSummonWhitelist"].size());
	out["roofMarshalAllowsNormal"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_NORMAL);
	out["roofMarshalAllowsGargantuar"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_GARGANTUAR);
	out["roofMarshalAllowsEliteDancer"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ELITE_DANCER);
	out["roofMarshalAllowsElitePolevaulter"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ELITE_POLEVAULTER);
	out["roofMarshalAllowsEliteJack"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ELITE_JACK_IN_THE_BOX);
	out["roofMarshalAllowsEliteDigger"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ELITE_DIGGER);
	out["roofMarshalAllowsElitePogo"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ELITE_POGO);
	out["roofMarshalAllowsEliteLadder"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ELITE_LADDER);
	out["roofMarshalAllowsEliteCatapult"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ELITE_CATAPULT);
	out["roofMarshalAllowsRedEyeGargantuar"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_REDEYE_GARGANTUAR);
	out["roofMarshalAllowsPoolNormal"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_POOL_NORMAL);
	out["roofMarshalAllowsSelf"] = RoofMarshalZombie::IsAllowedSummonType(
		ZombieType::ZOMBIE_ROOF_MARSHAL);
	out["poolRowZombieCount"] = poolRowZombieCount;
	out["earlyWavePoolZombieCount"] = earlyWavePoolZombieCount;
	out["zombieBodyHealthTotal"] = zombieBodyHealthTotal;
	out["zombieShieldHealthTotal"] = zombieShieldHealthTotal;
	out["slowedZombieCount"] = slowedZombieCount;
	out["toxicZombieCount"] = toxicZombieCount;
	out["fireResistantZombieCount"] = fireResistantZombieCount;

	out["plants"] = nlohmann::json::array();
	out["topPlantsByCell"] = nlohmann::json::object();
	out["underPlantsByCell"] = nlohmann::json::object();
	out["normalPlantsByCell"] = nlohmann::json::object();
	out["pumpkinPlantsByCell"] = nlohmann::json::object();
	out["overlayPlantsByCell"] = nlohmann::json::object();
	out["flowerPotsByCell"] = nlohmann::json::object();
	out["scaredyShroomsByCell"] = nlohmann::json::object();
	out["meltSnowPultsByCell"] = nlohmann::json::object();
	out["frostMinesByCell"] = nlohmann::json::object();
	out["alarmBellFlowersByCell"] = nlohmann::json::object();
	out["furnaceCoreFlowersByCell"] = nlohmann::json::object();
	out["listeningGrassesByCell"] = nlohmann::json::object();
	int repeatingShootingHeadCount = 0;
	for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
		Plant* p = board->mEntityRegistry.GetPlant(id);
		if (!p) continue;
		const Vector plantPosition = p->GetPosition();
		const Vector plantVisualPosition = p->GetVisualPosition();
		const Vector plantVisualAnchor = p->GetVisualAnchorPosition();
		const Vector sleepIndicatorPosition = p->GetSleepIndicatorPosition();
		const Vector cellCenter = board->GetCellCenterPosition(p->mRow, p->mColumn);
		nlohmann::json plantState = {
			{ "id", id },
			{ "type", PlantTypeName(p->mPlantType) },
			{ "placementType", PlantTypeName(p->GetPlacementType()) },
			{ "imitated", p->IsImitated() },
			{ "row", p->mRow }, { "col", p->mColumn },
			{ "renderOrder", p->GetRenderOrder() },
			{ "renderLayer", static_cast<int>(p->GetLayer()) },
			{ "health", p->mPlantHealth }, { "maxHealth", p->mPlantMaxHealth },
			{ "unyieldingRootsSpent", p->HasSpentUnyieldingRoots() },
			{ "unyieldingRootsTimerMs", static_cast<int>(std::lround(
				p->GetUnyieldingRootsTimeRemaining() * 1000.0f)) },
			{ "eaterCount", p->mEaterCount },
			{ "canBeEaten", p->CanBeEaten() },
			{ "canBeRelocated", p->CanBeRelocated() },
			{ "sleeping", p->GetSleepState() },
			{ "sleepIndicatorVisible", p->HasSleepIndicator() },
			{ "sleepIndicatorOffsetXOn1000", static_cast<int>(std::lround(
				(sleepIndicatorPosition.x - plantVisualAnchor.x) * 1000.0f)) },
			{ "sleepIndicatorOffsetYOn1000", static_cast<int>(std::lround(
				(sleepIndicatorPosition.y - plantVisualAnchor.y) * 1000.0f)) },
			{ "wakeUpTimeMs", static_cast<int>(std::lround(
				p->GetWakeUpTimeRemaining() * 1000.0f)) },
			{ "squished", p->IsSquished() },
			{ "squishTimeMs", static_cast<int>(p->GetSquishTimeRemaining() * 1000.0f + 0.5f) },
			{ "track", p->GetCurrentTrackName() },
			{ "animFrame", p->GetCurrentFrame() },
			{ "animPlaying", p->IsAnimationPlaying() },
			{ "animPlayState", PlayStateName(p->GetPlayingState()) },
			{ "animTargetTrack", p->GetTargetTrack() },
			{ "animTargetTrackBlendMs", static_cast<int>(std::lround(
				p->GetTargetTrackBlendTime() * 1000.0f)) },
			{ "alphaPct", static_cast<int>(p->GetAlpha() * 100.0f + 0.5f) },
			{ "renderScaleYPct",
				static_cast<int>(p->GetSquishRenderScaleY() * 100.0f + 0.5f) },
			{ "logicalX", plantPosition.x },
			{ "logicalY", plantPosition.y },
			{ "visualX", plantVisualPosition.x },
			{ "visualY", plantVisualPosition.y },
			{ "visualAnchorX", plantVisualAnchor.x },
			{ "visualAnchorY", plantVisualAnchor.y },
			{ "visualOffsetXOn1000", static_cast<int>(std::lround(
				(plantVisualAnchor.x - plantPosition.x) * 1000.0f)) },
			{ "visualOffsetYOn1000", static_cast<int>(std::lround(
				(plantVisualAnchor.y - plantPosition.y) * 1000.0f)) },
			{ "roofVisualPathErrorOn1000", static_cast<int>(std::lround((
				plantVisualAnchor.x - plantPosition.x
				+ 4.0f * (plantVisualAnchor.y - plantPosition.y)) * 1000.0f)) },
			{ "terrainXOffsetOn1000", static_cast<int>(std::lround(
				(plantPosition.x - cellCenter.x) * 1000.0f)) },
			{ "terrainYOffsetOn1000", static_cast<int>(std::lround(
				(plantPosition.y - cellCenter.y) * 1000.0f)) },
			{ "hasShadow", p->GetShadow() != nullptr },
			{ "sunProductionMultiplierPct", static_cast<int>(std::lround(
				board->GetPlanternSunProductionMultiplier(p) * 100.0f)) },
			{ "roofRunoffPaused", board->IsPlantPausedByRoofRunoff(p) },
			{ "shutdown", p->IsShutdown() },
			{ "shutdownTimerMs", static_cast<int>(std::lround(
				p->GetShutdownTimeRemaining() * 1000.0f)) },
			{ "iceSealed", p->IsIceSealed() },
			{ "iceSealOwnerZombieID", p->GetIceSealOwnerZombieID() },
			{ "colliderEnabled", p->GetColliderComponent()
				&& p->GetColliderComponent()->mEnabled },
			{ "bungeeState", PlantBungeeStateName(p->GetBungeeState()) },
			{ "bungeeOwnerZombieID", p->GetBungeeOwnerZombieID() },
			{ "canBeTargetedByBungee", p->CanBeTargetedByBungee() },
			{ "footprintCellCount", static_cast<int>(
				GetPlantFootprint(p->GetPlacementType()).count) },
			{ "airborneDefenseState",
				AirborneDefenseStateName(p->GetAirborneDefenseState()) },
			{ "airborneDefenseActivationMs", static_cast<int>(std::lround(
				p->GetAirborneDefenseActivationTime() * 1000.0f)) },
			{ "roofSupport", p->IsRoofSupportPlant() },
			{ "nightRoofChargeZombieDamageMultiplierOn1000",
				static_cast<int>(std::lround(
					p->GetNightRoofChargeZombieDamageMultiplier() * 1000.0f)) },
			{ "winterGroundAnchorReady", p->IsWinterGroundAnchorReady() },
			{ "winterGroundAnchorSpent", p->HasSpentWinterGroundAnchor() },
		};
		if (const auto animator = p->GetAnimatorInternal()) {
			const AnimatorRenderProbe& probe = animator->GetLastRenderProbe();
			plantState["renderProbeReady"] = probe.hasGeometry;
			if (probe.hasGeometry) {
				const float centerX = (probe.minX + probe.maxX) * 0.5f;
				const float centerY = (probe.minY + probe.maxY) * 0.5f;
				plantState["renderBoundsCenterFromLogicalXInt"] =
					static_cast<int>(std::lround(centerX - plantPosition.x));
				plantState["renderBoundsCenterFromLogicalYInt"] =
					static_cast<int>(std::lround(centerY - plantPosition.y));
				plantState["renderBoundsBottomFromLogicalYInt"] =
					static_cast<int>(std::lround(probe.maxY - plantPosition.y));
				plantState["renderBoundsWidthInt"] = static_cast<int>(
					std::lround(probe.maxX - probe.minX));
				plantState["renderBoundsHeightInt"] = static_cast<int>(
					std::lround(probe.maxY - probe.minY));
			}
		}
		if (p->IsRoofSupportPlant()) {
			Plant* normal = board->GetNormalPlantAt(p->mRow, p->mColumn);
			plantState["protectsNormalFromNightRoofCharge"] = normal
				&& p->ProtectsSupportedPlantFromNightRoofCharge(normal);
			plantState["protectsNormalFromNightRoofHijacker"] = normal
				&& p->ProtectsSupportedPlantFromNightRoofHijacker(normal);
		}
		if (const auto* shadow = p->GetShadow()) {
			plantState["shadowOffsetXInt"] = static_cast<int>(std::lround(
				shadow->GetOffset().x));
			plantState["shadowOffsetYInt"] = static_cast<int>(std::lround(
				shadow->GetOffset().y));
			const auto animator = p->GetAnimatorInternal();
			const AnimatorRenderProbe* renderProbe =
				animator ? &animator->GetLastRenderProbe() : nullptr;
			// 与同一绘制帧的 Animator 实际 base 对比，避免截图完成后的下一逻辑帧
			// 已推进水面正弦相位，令重新计算的当前锚点产生亚像素时序差。
			const Vector actualCenter = shadow->GetLastDrawCenter();
			const bool probeReady = shadow->IsLastDrawReady()
				&& renderProbe && renderProbe->hasGeometry;
			const Vector expectedCenter = probeReady
				? Vector(renderProbe->baseX, renderProbe->baseY)
					- p->GetStaticVisualOffset() + shadow->GetOffset()
				: Vector::zero();
			plantState["shadowRenderProbeReady"] = probeReady;
			plantState["shadowCenterXOn1000"] =
				static_cast<int>(std::lround(actualCenter.x * 1000.0f));
			plantState["shadowCenterYOn1000"] =
				static_cast<int>(std::lround(actualCenter.y * 1000.0f));
			plantState["shadowFollowsVisualAnchor"] = probeReady
				&& std::abs(actualCenter.x - expectedCenter.x) < 0.01f
				&& std::abs(actualCenter.y - expectedCenter.y) < 0.01f;
		}
		if (auto* shooter = dynamic_cast<Shooter*>(p)) {
			if (const Animator* head = shooter->GetHeadAnimator()) {
				plantState["headTrack"] = head->GetCurrentTrackName();
				plantState["headAnimPlaying"] = head->IsPlaying();
				plantState["headAnimFrame"] = head->GetCurrentFrame();
				plantState["headAnimPlayState"] = PlayStateName(head->GetPlayingState());
				plantState["headAnimTargetTrack"] = head->GetTargetTrack();
				plantState["headAnimTargetTrackBlendMs"] =
					static_cast<int>(std::lround(
						head->GetTargetTrackBlendTime() * 1000.0f));
				plantState["headRenderScaleYPct"] =
					static_cast<int>(head->GetRenderScaleY() * 100.0f + 0.5f);
				if (head->IsPlaying() && head->GetPlayingState() == PlayState::PLAY_REPEAT
					&& head->GetCurrentTrackName() == "anim_shooting") {
					++repeatingShootingHeadCount;
				}
			}
		}
		if (auto* lilyPad = dynamic_cast<LilyPad*>(p)) {
			plantState["biteProtected"] = lilyPad->IsBiteProtected();
		}
		if (auto* flowerPot = dynamic_cast<FlowerPot*>(p)) {
			plantState["biteProtected"] = flowerPot->IsBiteProtected();
			plantState["covered"] = flowerPot->IsCovered();
			const std::string cellKey =
				std::to_string(p->mRow) + "_" + std::to_string(p->mColumn);
			out["flowerPotsByCell"][cellKey] = plantState;
		}
		if (auto* wallNut = dynamic_cast<WallNut*>(p)) {
			plantState["nutDamageStage"] = wallNut->GetDamageStage();
		}
		if (auto* garlic = dynamic_cast<Garlic*>(p)) {
			plantState["garlicDamageStage"] = garlic->GetDamageStage();
		}
		if (auto* pumpkin = dynamic_cast<PumpkinShell*>(p)) {
			plantState["pumpkinDamageStage"] = pumpkin->GetDamageStage();
			plantState["pumpkinBackAnimatorReady"] = pumpkin->HasBackAnimator();
		}
		if (auto* magnet = dynamic_cast<MagnetShroom*>(p)) {
			plantState["magnetPhase"] = magnet->GetPhaseName();
			plantState["magnetRechargeMs"] = static_cast<int>(std::lround(
				magnet->GetRechargeTimeRemaining() * 1000.0f));
			plantState["magnetItemActive"] = magnet->HasCapturedItem();
			plantState["magnetItemTextureKey"] =
				magnet->GetCapturedItemTextureKey();
			plantState["magnetItemDistanceInt"] = static_cast<int>(std::lround(
				magnet->GetCapturedItemDistance()));
			const Vector destinationFromLogical =
				magnet->GetCapturedItemDestinationFromLogical();
			plantState["magnetItemDestinationFromLogicalXInt"] =
				static_cast<int>(std::lround(destinationFromLogical.x));
			plantState["magnetItemDestinationFromLogicalYInt"] =
				static_cast<int>(std::lround(destinationFromLogical.y));
		}
		if (auto* squash = dynamic_cast<Squash*>(p)) {
			plantState["squashState"] = squash->GetSquashStateName();
			plantState["squashTargetZombieID"] = squash->GetTargetZombieID();
			plantState["squashDamageApplied"] = squash->HasAppliedDamage();
		}
		if (auto* tangleKelp = dynamic_cast<TangleKelp*>(p)) {
			plantState["tangleKelpState"] = tangleKelp->GetTangleKelpStateName();
			plantState["tangleKelpTargetZombieID"] = tangleKelp->GetTargetZombieID();
			plantState["tangleKelpGrabRemainingMs"] =
				tangleKelp->GetGrabTimeRemainingMs();
		}
		if (auto* caltrop = dynamic_cast<Caltrop*>(p)) {
			plantState["caltropAttackCooldownMs"] = static_cast<int>(std::lround(
				caltrop->GetAttackCooldown() * 1000.0f));
			plantState["canBeEaten"] = caltrop->CanBeEaten();
		}
		if (auto* cactus = dynamic_cast<Cactus*>(p)) {
			plantState["cactusPhase"] = cactus->GetPhaseName();
			plantState["cactusHasGroundTarget"] = cactus->HasCachedGroundTarget();
			plantState["cactusHasFlyingTarget"] = cactus->HasCachedFlyingTarget();
		}
		if (auto* blover = dynamic_cast<Blover*>(p)) {
			plantState["blowDirection"] =
				WindDirectionName(blover->GetBlowDirection());
			plantState["blowTriggered"] = blover->HasTriggeredBlow();
			const auto animator = blover->GetAnimatorInternal();
			plantState["flipX"] = animator && animator->GetFlipX();
		}
		if (auto* splitPea = dynamic_cast<SplitPea*>(p)) {
			if (const Animator* rearHead = splitPea->GetRearHeadAnimator()) {
				plantState["rearHeadTrack"] = rearHead->GetCurrentTrackName();
				plantState["rearHeadFrame"] = rearHead->GetCurrentFrame();
				plantState["rearHeadAnimPlaying"] = rearHead->IsPlaying();
				plantState["rearHeadAnimPlayState"] =
					PlayStateName(rearHead->GetPlayingState());
				plantState["rearHeadAnimTargetTrack"] = rearHead->GetTargetTrack();
				plantState["rearHeadAnimTargetTrackBlendMs"] =
					static_cast<int>(std::lround(
						rearHead->GetTargetTrackBlendTime() * 1000.0f));
			}
			plantState["rearSecondShotPending"] =
				splitPea->HasPendingRearSecondShot();
			plantState["rearSecondShotInBurst"] =
				splitPea->IsRearSecondShot();
		}
		if (auto* starFruit = dynamic_cast<StarFruit*>(p)) {
			plantState["starfruitShootTimerMs"] = static_cast<int>(std::lround(
				starFruit->GetShootTimer() * 1000.0f));
			plantState["starfruitShootIntervalMs"] = static_cast<int>(std::lround(
				starFruit->GetShootInterval() * 1000.0f));
		}
		if (auto* cabbagePult = dynamic_cast<CabbagePult*>(p)) {
			plantState["cabbageShootTimerMs"] = static_cast<int>(std::lround(
				cabbagePult->GetShootTimer() * 1000.0f));
			plantState["cabbageShootIntervalMs"] = static_cast<int>(std::lround(
				cabbagePult->GetShootInterval() * 1000.0f));
		}
		if (auto* kernelPult = dynamic_cast<KernelPult*>(p)) {
			plantState["kernelShootTimerMs"] = static_cast<int>(std::lround(
				kernelPult->GetShootTimer() * 1000.0f));
			plantState["kernelShootIntervalMs"] = static_cast<int>(std::lround(
				kernelPult->GetShootInterval() * 1000.0f));
			plantState["butterShotPending"] = kernelPult->IsButterShotPending();
			const std::shared_ptr<Animator> animator = kernelPult->GetAnimatorInternal();
			plantState["heldKernelVisible"] = animator
				&& animator->GetTrackVisible("Cornpult_kernal");
			plantState["heldButterVisible"] = animator
				&& animator->GetTrackVisible("Cornpult_butter");
		}
		if (auto* melonPult = dynamic_cast<MelonPult*>(p)) {
			plantState["melonShootTimerMs"] = static_cast<int>(std::lround(
				melonPult->GetShootTimer() * 1000.0f));
			plantState["melonShootIntervalMs"] = static_cast<int>(std::lround(
				melonPult->GetShootInterval() * 1000.0f));
		}
		if (auto* meltSnowPult = dynamic_cast<MeltSnowPult*>(p)) {
			plantState["meltSnowShootTimerMs"] = static_cast<int>(std::lround(
				meltSnowPult->GetShootTimer() * 1000.0f));
			plantState["meltSnowShootIntervalMs"] = static_cast<int>(std::lround(
				meltSnowPult->GetShootInterval() * 1000.0f));
			plantState["saltAmmo"] = meltSnowPult->GetSaltAmmo();
			plantState["saltShotPending"] = meltSnowPult->IsSaltShotPending();
			plantState["observedColdWaveForecast"] =
				meltSnowPult->HasObservedColdWaveForecast();
			const std::string cellKey =
				std::to_string(p->mRow) + "_" + std::to_string(p->mColumn);
			out["meltSnowPultsByCell"][cellKey] = plantState;
		}
		if (auto* frostMine = dynamic_cast<FrostMine*>(p)) {
			plantState["frostMinePhase"] = frostMine->GetPhaseName();
			plantState["frostMineCalibrated"] = frostMine->IsCalibrated();
			plantState["frostMineArmed"] = frostMine->IsArmed();
			const std::string cellKey =
				std::to_string(p->mRow) + "_" + std::to_string(p->mColumn);
			out["frostMinesByCell"][cellKey] = plantState;
		}
		if (auto* alarmBell = dynamic_cast<AlarmBellFlower*>(p)) {
			plantState["alarmBellPulseTriggered"] = alarmBell->HasTriggeredPulse();
			plantState["alarmBellPulseSucceeded"] = alarmBell->DidInterruptAction();
			plantState["alarmBellAfterglowRemainingMs"] = static_cast<int>(std::lround(
				alarmBell->GetAfterglowRemaining() * 1000.0f));
			const std::shared_ptr<Animator> animator = alarmBell->GetAnimatorInternal();
			plantState["alarmBellCharmVisible"] = animator
				&& animator->GetTrackFollowerVisible("Blover_stem1", "alarm_bell_charm");
			const std::string cellKey =
				std::to_string(p->mRow) + "_" + std::to_string(p->mColumn);
			out["alarmBellFlowersByCell"][cellKey] = plantState;
		}
		if (auto* furnace = dynamic_cast<FurnaceCoreFlower*>(p)) {
			plantState["furnaceStoredCores"] = furnace->GetStoredCoreCount();
			plantState["furnaceChargeProgressMs"] = static_cast<int>(std::lround(
				furnace->GetChargeProgress() * 1000.0f));
			plantState["furnaceCharging"] = furnace->IsCharging();
			const std::shared_ptr<Animator> animator = furnace->GetAnimatorInternal();
			plantState["furnaceLeftCoreVisible"] = animator
				&& animator->GetTrackFollowerVisible(
					"anim_idle", "furnace_core_left");
			plantState["furnaceRightCoreVisible"] = animator
				&& animator->GetTrackFollowerVisible(
					"anim_idle", "furnace_core_right");
			const std::string cellKey =
				std::to_string(p->mRow) + "_" + std::to_string(p->mColumn);
			out["furnaceCoreFlowersByCell"][cellKey] = plantState;
		}
		if (auto* mint = dynamic_cast<IceMint*>(p)) {
			plantState["productionRemainingMs"] = static_cast<int>(std::lround(mint->GetProductionRemaining() * 1000.0f));
			plantState["productionBatches"] = mint->GetProductionBatches();
			out["iceMintsByCell"][std::to_string(p->mRow) + "_" + std::to_string(p->mColumn)] = plantState;
		}
		if (auto* listeningGrass = dynamic_cast<ListeningGrass*>(p)) {
			plantState["listenCooldownRemainingMs"] = static_cast<int>(std::lround(
				listeningGrass->GetListenCooldownRemaining() * 1000.0f));
			plantState["listenReady"] = listeningGrass->GetListenCooldownRemaining() <= 0.0f;
			const std::string cellKey =
				std::to_string(p->mRow) + "_" + std::to_string(p->mColumn);
			out["listeningGrassesByCell"][cellKey] = plantState;
		}
		if (auto* northStar = dynamic_cast<NorthStarFlower*>(p)) {
			plantState["northStarChargeMs"] = static_cast<int>(std::lround(
				northStar->GetChargeSeconds() * 1000.0f));
			plantState["northStarActiveRemainingMs"] = static_cast<int>(std::lround(
				northStar->GetActiveRemaining() * 1000.0f));
			plantState["northStarReady"] = northStar->IsPolarNavigationReady();
		}
		if (auto* iceMirror = dynamic_cast<IceMirrorGrass*>(p)) {
			plantState["iceMirrorCount"] = iceMirror->GetMirrorCount();
			plantState["iceMirrorFormationProgressMs"] = static_cast<int>(std::lround(
				iceMirror->GetFormationProgress() * 1000.0f));
		}
		if (auto* imitater = dynamic_cast<Imitater*>(p)) {
			plantState["imitaterTarget"] = PlantTypeName(
				imitater->GetImitaterTarget());
			plantState["imitaterMorphCountdownMs"] = static_cast<int>(std::lround(
				imitater->GetMorphCountdown() * 1000.0f));
			plantState["imitaterMorphing"] = imitater->IsMorphing();
			plantState["imitaterParticleEmitted"] =
				imitater->HasEmittedMorphParticle();
		}
		if (auto* sunFlower = dynamic_cast<SunFlower*>(p)) {
			plantState["produceTimerMs"] = static_cast<int>(std::lround(
				sunFlower->GetProduceTimer() * 1000.0f));
			plantState["produceIntervalMs"] = static_cast<int>(std::lround(
				sunFlower->GetProduceInterval() * 1000.0f));
			plantState["produceSunCount"] = sunFlower->GetProduceSunCount();
			plantState["productionGlowing"] = sunFlower->IsGlowingForProduction();
		}
		if (auto* gloomShroom = dynamic_cast<GloomShroom*>(p)) {
			plantState["gloomShootTimerMs"] = static_cast<int>(std::lround(
				gloomShroom->GetShootTimer() * 1000.0f));
			plantState["gloomAttacking"] = gloomShroom->IsAttacking();
			plantState["gloomAttackElapsedMs"] = static_cast<int>(std::lround(
				gloomShroom->GetAttackElapsed() * 1000.0f));
			plantState["gloomNextCloudIndex"] = gloomShroom->GetNextCloudIndex();
			plantState["gloomNextDamageIndex"] = gloomShroom->GetNextDamageIndex();
		}
		if (auto* threePeater = dynamic_cast<ThreePeater*>(p)) {
			if (const Animator* head1 = threePeater->GetHeadAnimator()) {
				plantState["head1Track"] = head1->GetCurrentTrackName();
				plantState["head1Frame"] = head1->GetCurrentFrame();
			}
			if (const Animator* head2 = threePeater->GetHeadAnimator2()) {
				plantState["head2Track"] = head2->GetCurrentTrackName();
				plantState["head2Frame"] = head2->GetCurrentFrame();
			}
			if (const Animator* head3 = threePeater->GetHeadAnimator3()) {
				plantState["head3Track"] = head3->GetCurrentTrackName();
				plantState["head3Frame"] = head3->GetCurrentFrame();
			}
		}
		if (auto* eliteScaredy = dynamic_cast<EliteScaredyShroom*>(p)) {
			plantState["growthShots"] = eliteScaredy->GetGrowthShotCount();
			plantState["attackSpeedStage"] = eliteScaredy->GetAttackSpeedStage();
			plantState["puffDamage"] = eliteScaredy->GetCurrentPuffDamage();
			plantState["shootIntervalMs"] = eliteScaredy->GetShootIntervalMilliseconds();
			plantState["growthRatePct"] = eliteScaredy->GetGrowthRatePercent();
			plantState["growthProgressTenths"] = eliteScaredy->GetGrowthProgressTenths();
		}
		if (auto* scaredy = dynamic_cast<ScaredyShroom*>(p)) {
			plantState["fearState"] = scaredy->GetFearStateName();
			const std::string cellKey =
				std::to_string(p->mRow) + "_" + std::to_string(p->mColumn);
			out["scaredyShroomsByCell"][cellKey] = plantState;
		}
		if (auto* plantern = dynamic_cast<Plantern*>(p)) {
			plantState["planternFuelTenths"] = static_cast<int>(std::lround(
				plantern->GetFuel() * 10.0f));
			plantState["planternGear"] = PlanternGearName(plantern->GetGear());
			plantState["planternGearValue"] = static_cast<int>(plantern->GetGear());
			plantState["planternLightUsable"] = plantern->HasUsableLight();
		}
		if (auto* coffeeBean = dynamic_cast<CoffeeBean*>(p)) {
			plantState["coffeeBeanPhase"] = coffeeBean->GetPhaseName();
			plantState["coffeeBeanWaitMs"] = coffeeBean->GetWaitTimeRemainingMs();
		}
		if (auto* cannon = dynamic_cast<CobCannon*>(p)) {
			const std::shared_ptr<Animator> animator = cannon->GetAnimatorInternal();
			const SDL_Color cobTrackColor = animator
				? animator->GetTrackColor("CobCannon_cob")
				: SDL_Color{ 255, 255, 255, 255 };
			plantState["cobPhase"] = CobCannonPhaseName(cannon->GetPhase());
			plantState["cobArmingTimeMs"] = static_cast<int>(std::lround(
				cannon->GetArmingTimeRemaining() * 1000.0f));
			plantState["cobShotLaunched"] = cannon->HasLaunchedCurrentShot();
			plantState["cobTargetRow"] = cannon->GetPendingTargetRow();
			plantState["cobTargetXInt"] = static_cast<int>(std::lround(
				cannon->GetPendingTarget().x));
			plantState["cobTargetYInt"] = static_cast<int>(std::lround(
				cannon->GetPendingTarget().y));
			plantState["cobTrackColorR"] = static_cast<int>(cobTrackColor.r);
			plantState["cobTrackColorG"] = static_cast<int>(cobTrackColor.g);
			plantState["cobTrackColorB"] = static_cast<int>(cobTrackColor.b);
			plantState["cobTrackColorA"] = static_cast<int>(cobTrackColor.a);
			plantState["cobTrackColorEqualRGB"] = cobTrackColor.r == cobTrackColor.g
				&& cobTrackColor.g == cobTrackColor.b;
		}
		if (auto* prism = dynamic_cast<PrismFlower*>(p)) {
			plantState["prismCooldownMs"] = static_cast<int>(std::lround(prism->GetMarkCooldown()*1000));
			plantState["prismLastMarkCount"] = prism->GetLastMarkCount();
		}
		if (auto* boundary = dynamic_cast<BoundaryFlower*>(p)) {
			plantState["boundaryShardCount"] = boundary->GetShardCount();
			plantState["boundaryShardChargeMs"] = static_cast<int>(std::lround(
				boundary->GetShardCharge() * 1000.0f));
			plantState["boundaryTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_BOUNDARYFLOWER_MONUMENT, false) != nullptr;
		}
		if (auto* dawn = dynamic_cast<DawnLotus*>(p)) {
			plantState["dawnEnergyOn1000"] = static_cast<int>(std::lround(
				dawn->GetEnergy() * 1000.0f));
			plantState["dawnFullyCharged"] = dawn->IsFullyCharged();
			plantState["dawnCanActivate"] = dawn->IsReadyToActivate();
			plantState["dawnTextureLoaded"] = ResourceManager::GetInstance().GetTexture(
				ResourceKeys::Textures::IMAGE_REANIM_DAWNLOTUS_CROWN, false) != nullptr;
		}
		// 同一实体可占多个格；逐 footprint 查询公共 Board getter，能同时验证所有别名
		// 都返回同一 Plant*，又保持 plants 实体数组只导出一次。
		const PlantFootprint footprint = GetPlantFootprint(p->GetPlacementType());
		for (std::size_t footprintIndex = 0;
			footprintIndex < footprint.count; ++footprintIndex) {
			const int occupiedRow = p->mRow
				+ footprint.cells[footprintIndex].rowOffset;
			const int occupiedColumn = p->mColumn
				+ footprint.cells[footprintIndex].columnOffset;
			const std::string cellKey = std::to_string(occupiedRow)
				+ "_" + std::to_string(occupiedColumn);
			if (board->GetTopPlantAt(occupiedRow, occupiedColumn) == p) {
				out["topPlantsByCell"][cellKey] = plantState;
			}
			if (board->GetOverlayPlantAt(occupiedRow, occupiedColumn) == p) {
				out["overlayPlantsByCell"][cellKey] = plantState;
			}
			if (board->GetUnderPlantAt(occupiedRow, occupiedColumn) == p) {
				out["underPlantsByCell"][cellKey] = plantState;
			}
			if (board->GetNormalPlantAt(occupiedRow, occupiedColumn) == p) {
				out["normalPlantsByCell"][cellKey] = plantState;
			}
			if (board->GetPumpkinAt(occupiedRow, occupiedColumn) == p) {
				out["pumpkinPlantsByCell"][cellKey] = plantState;
			}
		}
		out["plants"].push_back(std::move(plantState));
	}
	out["plantCount"] = static_cast<int>(out["plants"].size());

	// 粒子坐标取证从本项目 DrawTextureRegion 的最终粒子矩形聚合，C# 800x600
	// 只保留行为语义；稳定断言优先使用相对发射原点或最近实体碰撞框的整数投影。
	out["particleEffects"] = nlohmann::json::array();
	out["particleEffectsByName"] = nlohmann::json::object();
	out["particleEffectNameCounts"] = nlohmann::json::object();
	out["particleEffectNameCounts"]["PeaBulletHit"] = 0;
	out["particleEffectNameCounts"]["ToxicPeaBulletHit"] = 0;
	out["particleEffectNameCounts"]["CabbageSplat"] = 0;
	out["particleEffectNameCounts"]["MeltSnowPultHit"] = 0;
	out["particleEffectNameCounts"]["SaltCrystalHit"] = 0;
	out["particleEffectNameCounts"]["MelonSplash"] = 0;
	out["particleEffectNameCounts"]["WinterMelonSplash"] = 0;
	out["particleEffectNameCounts"]["ButterSplat"] = 0;
	out["particleEffectNameCounts"]["UmbrellaReflect"] = 0;
	out["particleEffectNameCounts"]["ZombieArmOff"] = 0;
	out["particleEffectNameCounts"]["ZombieHeadOff"] = 0;
	out["particleEffectNameCounts"]["RoofMarshalHeadOff"] = 0;
	out["particleEffectNameCounts"]["ZombieDolphinRiderHeadOff"] = 0;
	out["particleEffectNameCounts"]["EliteDolphinRiderHeadOff"] = 0;
	out["particleEffectNameCounts"]["JackExplode"] = 0;
	out["particleEffectNameCounts"]["CherryBomb"] = 0;
	out["particleEffectNameCounts"]["CobCannonPopcorn"] = 0;
	out["particleEffectNameCounts"]["CobCannonBlastMark"] = 0;
	out["particleEffectNameCounts"]["ZombieJackboxArmOff"] = 0;
	out["particleEffectNameCounts"]["ZombieEliteJackboxArmOff"] = 0;
	out["particleEffectNameCounts"]["WallnutEatSmall"] = 0;
	out["particleEffectNameCounts"]["WallnutEatLarge"] = 0;
	out["particleEffectNameCounts"]["TallNutBlock"] = 0;
	out["particleEffectNameCounts"]["DiggerTunnel"] = 0;
	out["particleEffectNameCounts"]["DiggerRise"] = 0;
	out["particleEffectNameCounts"]["SnowBurrowTrail"] = 0;
	out["particleEffectNameCounts"]["SnowBurrowRise"] = 0;
	out["particleEffectNameCounts"]["SnowBurrowImpact"] = 0;
	out["particleEffectNameCounts"]["SnowBurrowWindup"] = 0;
	out["particleEffectNameCounts"]["ListeningGrassPulse"] = 0;
	out["particleEffectNameCounts"]["ListeningGrassHoleSeal"] = 0;
	out["particleEffectNameCounts"]["ZombieSnowBurrowArmOff"] = 0;
	out["particleEffectNameCounts"]["ZombieSnowBurrowHeadOff"] = 0;
	out["particleEffectNameCounts"]["ZombieDiggerArmOff"] = 0;
	out["particleEffectNameCounts"]["ZombieDiggerHeadOff"] = 0;
	out["particleEffectNameCounts"]["ZombieHeadLight"] = 0;
	out["particleEffectNameCounts"]["EliteDiggerBlast"] = 0;
	out["particleEffectNameCounts"]["ZombieEliteDiggerArmOff"] = 0;
	out["particleEffectNameCounts"]["ZombieEliteDiggerHeadLight"] = 0;
	out["particleEffectNameCounts"]["ZombiePinkFootballOff"] = 0;
	out["particleEffectNameCounts"]["ZombieFootballOff"] = 0;
	out["particleEffectNameCounts"]["ZombieIceExecutionerHelmetOff"] = 0;
	out["particleEffectNameCounts"]["ZombieElitePogo"] = 0;
	out["particleEffectNameCounts"]["ZombieEliteLadder"] = 0;
	out["particleEffectNameCounts"]["ZombieLadder"] = 0;
	out["particleEffectNameCounts"]["EliteLadderInfiniteBuff"] = 0;
	out["particleEffectNameCounts"]["EliteLadderHasteBuff"] = 0;
	out["particleEffectNameCounts"]["EliteLadderBodyBuff"] = 0;
	out["particleEffectNameCounts"]["EliteLadderShieldBuff"] = 0;
	out["particleEffectNameCounts"]["PlantingPool"] = 0;
	out["particleEffectNameCounts"]["CatapultExplosion"] = 0;
	out["particleEffectNameCounts"]["EliteCatapultExplosion"] = 0;
	out["particleEffectNameCounts"]["GloomCloud"] = 0;
	out["particleEffectNameCounts"]["HealerAreaHeal"] = 0;
	out["particleEffectNameCounts"]["HealerFocusedHeal"] = 0;
	out["particleEffectNameCounts"]["GoldMagnetEMP"] = 0;
	out["particleEffectNameCounts"]["ImitaterMorph"] = 0;
	out["particleEffectNameCounts"]["SnowLight"] = 0;
	out["particleEffectNameCounts"]["SnowMedium"] = 0;
	out["particleEffectNameCounts"]["SnowHeavy"] = 0;
	out["particleEffectNameCounts"]["ZombieBobsledPlantImpact"] = 0;
	out["particleEffectNameCounts"]["IceWallBuild"] = 0;
	out["particleEffectNameCounts"]["IceWallHit"] = 0;
	out["particleEffectNameCounts"]["IceWallBreak"] = 0;
	out["particleEffectNameCounts"]["IceWallEngineerHardhatOff"] = 0;
	out["particleEffectNameCounts"]["IceCrackDrillCharge"] = 0;
	out["particleEffectNameCounts"]["IceCrackDrillRift"] = 0;
	out["particleEffectNameCounts"]["IceCrackDrillRigOff"] = 0;
	out["particleEffectNameCounts"]["FrostMineBurst"] = 0;
	out["particleEffectNameCounts"]["PotatoMine"] = 0;
	out["particleEffectNameCounts"]["AlarmBellRowPulse"] = 0;
	out["particleEffectNameCounts"]["SnowHolePuff"] = 0;
	out["particleEffectNameCounts"]["PolarWindUp"] = 0;
	out["particleEffectNameCounts"]["PolarWindDown"] = 0;
	out["particleEffectNameCounts"]["DawnLotusStrike"] = 0;
	if (g_particleSystem) {
		for (const auto& effect : g_particleSystem->GetEffectsForTesting()) {
			if (!effect) continue;
			const Vector origin = effect->GetPosition();
			const ParticleRenderProbe probe = effect->GetLastRenderProbe();
			nlohmann::json state = {
				{ "name", effect->GetName() },
				{ "originXInt", static_cast<int>(std::lround(origin.x)) },
				{ "originYInt", static_cast<int>(std::lround(origin.y)) },
				{ "renderOrder", effect->GetRenderOrder() },
				{ "emitting", effect->IsEmitting() },
				{ "activeParticleCount", effect->GetActiveParticleCount() },
				{ "renderProbeReady", probe.hasGeometry },
				{ "renderQuadCount", probe.quadCount },
			};
			if (board->IsRoofBackground()) {
				int nearestRow = 0;
				float nearestOffset = std::numeric_limits<float>::max();
				for (int row = 0; row < board->mRows; ++row) {
					const float offset = origin.y - board->GetRowCenterYAtX(row, origin.x);
					if (std::abs(offset) < std::abs(nearestOffset)) {
						nearestOffset = offset;
						nearestRow = row;
					}
				}
				state["roofTerrainRow"] = nearestRow;
				state["roofTerrainLocalYOffsetOn1000"] = static_cast<int>(
					std::lround(nearestOffset * 1000.0f));
				state["roofTerrainInsideRow"] = std::abs(nearestOffset)
					<= board->GetCellHeight() * 0.5f;
			}
			state["clipRightXInt"] = effect->GetClipRightX() >= 0.0f
				? nlohmann::json(static_cast<int>(std::lround(effect->GetClipRightX())))
				: nlohmann::json(nullptr);

			if (probe.hasGeometry) {
				const SDL_FRect bounds = {
					probe.minX,
					probe.minY,
					probe.maxX - probe.minX,
					probe.maxY - probe.minY,
				};
				const float centerX = bounds.x + bounds.w * 0.5f;
				const float centerY = bounds.y + bounds.h * 0.5f;
				state["worldBounds"] = {
					{ "leftInt", static_cast<int>(std::lround(bounds.x)) },
					{ "topInt", static_cast<int>(std::lround(bounds.y)) },
					{ "rightInt", static_cast<int>(std::lround(bounds.x + bounds.w)) },
					{ "bottomInt", static_cast<int>(std::lround(bounds.y + bounds.h)) },
					{ "widthInt", static_cast<int>(std::lround(bounds.w)) },
					{ "heightInt", static_cast<int>(std::lround(bounds.h)) },
					{ "centerXInt", static_cast<int>(std::lround(centerX)) },
					{ "centerYInt", static_cast<int>(std::lround(centerY)) },
				};
				state["originToRenderCenterDxInt"] =
					static_cast<int>(std::lround(centerX - origin.x));
				state["originToRenderCenterDyInt"] =
					static_cast<int>(std::lround(centerY - origin.y));

				float nearestZombieDistanceSq = std::numeric_limits<float>::max();
				nlohmann::json nearestZombie = nullptr;
				for (int id : board->mEntityRegistry.GetAllZombieIDs()) {
					Zombie* zombie = board->mEntityRegistry.GetZombie(id);
					if (!zombie || !zombie->IsActive() || zombie->IsDying()) continue;
					const ColliderComponent* collider = zombie->GetColliderComponent();
					if (!collider) continue;
					const SDL_FRect candidateBounds = collider->GetBoundingBox();
					const float candidateX = candidateBounds.x + candidateBounds.w * 0.5f;
					const float candidateY = candidateBounds.y + candidateBounds.h * 0.5f;
					const float dx = centerX - candidateX;
					const float dy = centerY - candidateY;
					const float distanceSq = dx * dx + dy * dy;
					if (distanceSq >= nearestZombieDistanceSq) continue;
					nearestZombieDistanceSq = distanceSq;
					nearestZombie = {
						{ "id", id },
						{ "row", zombie->mRow },
						{ "type", ZombieTypeName(zombie->mZombieType) },
						{ "centerDxInt", static_cast<int>(std::lround(dx)) },
						{ "centerDyInt", static_cast<int>(std::lround(dy)) },
						{ "boundsIntersect", BoundsIntersect(bounds, candidateBounds) },
					};
				}
				state["nearestZombie"] = std::move(nearestZombie);

				float nearestPlantDistanceSq = std::numeric_limits<float>::max();
				nlohmann::json nearestPlant = nullptr;
				for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
					Plant* plant = board->mEntityRegistry.GetPlant(id);
					if (!plant || !plant->IsActive()) continue;
					const ColliderComponent* collider = plant->GetColliderComponent();
					if (!collider) continue;
					const SDL_FRect candidateBounds = collider->GetBoundingBox();
					const float candidateX = candidateBounds.x + candidateBounds.w * 0.5f;
					const float candidateY = candidateBounds.y + candidateBounds.h * 0.5f;
					const float dx = centerX - candidateX;
					const float dy = centerY - candidateY;
					const float distanceSq = dx * dx + dy * dy;
					if (distanceSq >= nearestPlantDistanceSq) continue;
					nearestPlantDistanceSq = distanceSq;
					nearestPlant = {
						{ "id", id },
						{ "row", plant->mRow },
						{ "col", plant->mColumn },
						{ "type", PlantTypeName(plant->mPlantType) },
						{ "centerDxInt", static_cast<int>(std::lround(dx)) },
						{ "centerDyInt", static_cast<int>(std::lround(dy)) },
						{ "boundsIntersect", BoundsIntersect(bounds, candidateBounds) },
					};
				}
				state["nearestPlant"] = std::move(nearestPlant);
			}
			else {
				state["worldBounds"] = nullptr;
				state["nearestZombie"] = nullptr;
				state["nearestPlant"] = nullptr;
			}

			const std::string name = effect->GetName();
			out["particleEffects"].push_back(state);
			out["particleEffectsByName"][name].push_back(state);
			out["particleEffectNameCounts"][name] =
				out["particleEffectsByName"][name].size();
		}
	}
	out["particleEffectCount"] = static_cast<int>(out["particleEffects"].size());

	// AnimatedObject 语义取证使用 Animator 最近一次实际提交的最终世界四边形，而不是
	// C# 800x600 的绝对坐标或对象逻辑点。默认实例化与 -NoInstance 都写同一份探针。
	out["animatedObjects"] = nlohmann::json::array();
	out["animatedObjectsByTag"] = nlohmann::json::object();
	out["animatedObjectTagCounts"] = nlohmann::json::object();
	out["animatedObjectTagCounts"]["DiggerOneShotVisual"] = 0;
	out["animatedObjectTagCounts"]["PoolSplash"] = 0;
	for (const auto& object : GameObjectManager::GetInstance().GetAllGameObjects()) {
		auto* animated = object && object->IsActive()
			? dynamic_cast<AnimatedObject*>(object.get()) : nullptr;
		if (!animated) continue;
		const auto animator = animated->GetAnimatorInternal();
		if (!animator) continue;

		const Vector logical = animated->GetAnimationPosition();
		const Vector visual = animated->GetVisualPosition();
		const AnimatorRenderProbe& probe = animator->GetLastRenderProbe();
		const std::string tag = animated->GetTag();
		const std::string animation = ResourceManager::GetInstance().AnimationTypeToString(
			animated->GetAnimationType());

		nlohmann::json state = {
			{ "name", animated->GetName() },
			{ "tag", tag },
			{ "objectType", ObjectTypeName(animated->GetObjectType()) },
			{ "animation", animation },
			{ "renderOrder", animated->GetRenderOrder() },
			{ "renderLayer", static_cast<int>(animated->GetLayer()) },
			{ "sortingKey", animated->GetSortingKey() },
			{ "logicalXInt", static_cast<int>(std::lround(logical.x)) },
			{ "logicalYInt", static_cast<int>(std::lround(logical.y)) },
			{ "visualXInt", static_cast<int>(std::lround(visual.x)) },
			{ "visualYInt", static_cast<int>(std::lround(visual.y)) },
			{ "track", animator->GetCurrentTrackName() },
			{ "playing", animator->IsPlaying() },
			{ "renderProbeReady", probe.hasGeometry },
			{ "renderPath", probe.usedInstancePath ? "INSTANCE" : "NO_INSTANCE" },
			{ "renderQuadCount", probe.quadCount },
			{ "renderBaseXInt", static_cast<int>(std::lround(probe.baseX)) },
			{ "renderBaseYInt", static_cast<int>(std::lround(probe.baseY)) },
			{ "objectScaleOn1000", static_cast<int>(std::lround(
				probe.objectScale * 1000.0f)) },
			{ "renderScaleXOn1000", static_cast<int>(std::lround(
				animator->GetRenderScaleX() * 1000.0f)) },
			{ "renderScaleYOn1000", static_cast<int>(std::lround(
				animator->GetRenderScaleY() * 1000.0f)) },
			{ "renderPivotXInt", static_cast<int>(std::lround(
				animator->GetRenderPivotX())) },
			{ "renderPivotYInt", static_cast<int>(std::lround(
				animator->GetRenderPivotY())) },
			{ "renderBaseMatchesVisualPosition",
				std::abs(probe.baseX - visual.x) < 0.5f
				&& std::abs(probe.baseY - visual.y) < 0.5f },
			{ "renderPivotMatchesVisualBase",
				std::abs(animator->GetRenderPivotX() - probe.baseX) < 0.5f
				&& std::abs(animator->GetRenderPivotY() - probe.baseY) < 0.5f },
		};
		if (auto* pogo = dynamic_cast<PogoZombie*>(animated)) {
			state["pogoPreviewBounceActive"] = pogo->IsPreviewBounceActive();
			state["pogoAltitudeOn1000"] = static_cast<int>(std::lround(
				pogo->GetPogoAltitude() * 1000.0f));
		}

		if (probe.hasGeometry) {
			const SDL_FRect bounds = {
				probe.minX,
				probe.minY,
				probe.maxX - probe.minX,
				probe.maxY - probe.minY,
			};
			const float centerX = bounds.x + bounds.w * 0.5f;
			const float centerY = bounds.y + bounds.h * 0.5f;
			state["worldBounds"] = {
				{ "leftInt", static_cast<int>(std::lround(bounds.x)) },
				{ "topInt", static_cast<int>(std::lround(bounds.y)) },
				{ "rightInt", static_cast<int>(std::lround(bounds.x + bounds.w)) },
				{ "bottomInt", static_cast<int>(std::lround(bounds.y + bounds.h)) },
				{ "widthInt", static_cast<int>(std::lround(bounds.w)) },
				{ "heightInt", static_cast<int>(std::lround(bounds.h)) },
				{ "centerXInt", static_cast<int>(std::lround(centerX)) },
				{ "centerYInt", static_cast<int>(std::lround(centerY)) },
			};
			state["visualToRenderCenterDxInt"] =
				static_cast<int>(std::lround(centerX - visual.x));
			state["visualToRenderCenterDyInt"] =
				static_cast<int>(std::lround(centerY - visual.y));

			float nearestZombieDistanceSq = std::numeric_limits<float>::max();
			nlohmann::json nearestZombie = nullptr;
			for (int id : board->mEntityRegistry.GetAllZombieIDs()) {
				Zombie* zombie = board->mEntityRegistry.GetZombie(id);
				if (!zombie || !zombie->IsActive() || zombie->IsDying()) continue;
				const ColliderComponent* collider = zombie->GetColliderComponent();
				if (!collider) continue;
				const SDL_FRect candidateBounds = collider->GetBoundingBox();
				const float candidateX = candidateBounds.x + candidateBounds.w * 0.5f;
				const float candidateY = candidateBounds.y + candidateBounds.h * 0.5f;
				const float dx = centerX - candidateX;
				const float dy = centerY - candidateY;
				const float distanceSq = dx * dx + dy * dy;
				if (distanceSq >= nearestZombieDistanceSq) continue;
				nearestZombieDistanceSq = distanceSq;
				nearestZombie = {
					{ "id", id },
					{ "row", zombie->mRow },
					{ "type", ZombieTypeName(zombie->mZombieType) },
					{ "centerDxInt", static_cast<int>(std::lround(dx)) },
					{ "centerDyInt", static_cast<int>(std::lround(dy)) },
					{ "boundsIntersect", BoundsIntersect(bounds, candidateBounds) },
				};
			}
			state["nearestZombie"] = std::move(nearestZombie);

			float nearestPlantDistanceSq = std::numeric_limits<float>::max();
			nlohmann::json nearestPlant = nullptr;
			for (int id : board->mEntityRegistry.GetAllPlantIDs()) {
				Plant* plant = board->mEntityRegistry.GetPlant(id);
				if (!plant || !plant->IsActive()) continue;
				const ColliderComponent* collider = plant->GetColliderComponent();
				if (!collider) continue;
				const SDL_FRect candidateBounds = collider->GetBoundingBox();
				const float candidateX = candidateBounds.x + candidateBounds.w * 0.5f;
				const float candidateY = candidateBounds.y + candidateBounds.h * 0.5f;
				const float dx = centerX - candidateX;
				const float dy = centerY - candidateY;
				const float distanceSq = dx * dx + dy * dy;
				if (distanceSq >= nearestPlantDistanceSq) continue;
				nearestPlantDistanceSq = distanceSq;
				nearestPlant = {
					{ "id", id },
					{ "row", plant->mRow },
					{ "col", plant->mColumn },
					{ "type", PlantTypeName(plant->mPlantType) },
					{ "centerDxInt", static_cast<int>(std::lround(dx)) },
					{ "centerDyInt", static_cast<int>(std::lround(dy)) },
					{ "boundsIntersect", BoundsIntersect(bounds, candidateBounds) },
				};
			}
			state["nearestPlant"] = std::move(nearestPlant);
		}
		else {
			state["worldBounds"] = nullptr;
			state["nearestZombie"] = nullptr;
			state["nearestPlant"] = nullptr;
		}

		out["animatedObjects"].push_back(state);
		out["animatedObjectsByTag"][tag].push_back(state);
		out["animatedObjectTagCounts"][tag] =
			out["animatedObjectsByTag"][tag].size();
	}
	out["animatedObjectCount"] = static_cast<int>(out["animatedObjects"].size());

	out["cells"] = nlohmann::json::array();
	for (int row = 0; row < board->mRows; ++row) {
		nlohmann::json rowState = nlohmann::json::array();
		for (int col = 0; col < board->mColumns; ++col) {
			Cell* cell = board->GetCell(row, col);
			const Vector center = board->GetCellCenterPosition(row, col);
			Plant* under = cell ? board->mEntityRegistry.GetPlant(cell->GetUnderPlantID()) : nullptr;
			Plant* normal = cell ? board->mEntityRegistry.GetPlant(cell->GetNormalPlantID()) : nullptr;
			Plant* pumpkin = cell ? board->mEntityRegistry.GetPlant(cell->GetPumpkinPlantID()) : nullptr;
			Plant* overlay = cell ? board->mEntityRegistry.GetPlant(cell->GetOverlayPlantID()) : nullptr;
			Plant* top = board->GetTopPlantAt(row, col);
			rowState.push_back({
				{ "centerXInt", static_cast<int>(std::lround(center.x)) },
				{ "centerYInt", static_cast<int>(std::lround(center.y)) },
				{ "under", under ? PlantTypeName(under->mPlantType) : "NONE" },
				{ "underID", under ? under->mPlantID : NULL_PLANT_ID },
				{ "underHealth", under ? under->mPlantHealth : 0 },
				{ "normal", normal ? PlantTypeName(normal->mPlantType) : "NONE" },
				{ "normalID", normal ? normal->mPlantID : NULL_PLANT_ID },
				{ "normalHealth", normal ? normal->mPlantHealth : 0 },
				{ "pumpkin", pumpkin ? PlantTypeName(pumpkin->mPlantType) : "NONE" },
				{ "pumpkinID", pumpkin ? pumpkin->mPlantID : NULL_PLANT_ID },
				{ "pumpkinHealth", pumpkin ? pumpkin->mPlantHealth : 0 },
				{ "overlay", overlay ? PlantTypeName(overlay->mPlantType) : "NONE" },
				{ "overlayID", overlay ? overlay->mPlantID : NULL_PLANT_ID },
				{ "top", top ? PlantTypeName(top->mPlantType) : "NONE" },
				{ "topID", top ? top->mPlantID : NULL_PLANT_ID },
				{ "topHealth", top ? top->mPlantHealth : 0 },
			});
		}
		out["cells"].push_back(std::move(rowState));
	}
	out["bullets"] = nlohmann::json::array();
	bool hasBulletX = false;
	float minBulletX = 0.0f;
	float maxBulletX = 0.0f;
	int peaBulletCount = 0;
	int forwardPeaBulletCount = 0;
	int backwardPeaBulletCount = 0;
	int snowPeaBulletCount = 0;
	int fireballBulletCount = 0;
	int toxicFireballBulletCount = 0;
	int toxicPeaBulletCount = 0;
	int auroraPeaBulletCount = 0;
	int spikeBulletCount = 0;
	int starBulletCount = 0;
	int starLeftBulletCount = 0;
	int starUpBulletCount = 0;
	int starDownBulletCount = 0;
	int starUpRightBulletCount = 0;
	int starDownRightBulletCount = 0;
	int starSpinningBulletCount = 0;
	int cabbageBulletCount = 0;
	int meltSnowBulletCount = 0;
	int saltCrystalBulletCount = 0;
	int melonBulletCount = 0;
	int winterMelonBulletCount = 0;
	int kernelBulletCount = 0;
	int butterBulletCount = 0;
	int basketballBulletCount = 0;
	int lobbedBulletCount = 0;
	int flyingTargetSpikeCount = 0;
	int groundTargetSpikeCount = 0;
	int flyingTargetSpikePiercedZombieCount = 0;
	int groundTargetSpikePiercedZombieCount = 0;
	int torchwoodProtectedPeaCount = 0;
	int thermalPulseBulletCount = 0;
	int animatedBulletCount = 0;
	for (int id : board->mEntityRegistry.GetAllBulletIDs()) {
		Bullet* bullet = board->mEntityRegistry.GetBullet(id);
		if (!bullet) continue;
		const Vector pos = bullet->GetPosition();
		if (!hasBulletX) {
			minBulletX = maxBulletX = pos.x;
			hasBulletX = true;
		}
		else {
			minBulletX = std::min(minBulletX, pos.x);
			maxBulletX = std::max(maxBulletX, pos.x);
		}
		if (bullet->GetHitTorchwoodColumn() >= 0) ++torchwoodProtectedPeaCount;
		if (bullet->mBulletType == BulletType::BULLET_PEA) {
			++peaBulletCount;
			if (bullet->GetVelocityX() > 0.0f) ++forwardPeaBulletCount;
			else if (bullet->GetVelocityX() < 0.0f) ++backwardPeaBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_SNOWPEA) ++snowPeaBulletCount;
		else if (bullet->mBulletType == BulletType::BULLET_FIREBALL) ++fireballBulletCount;
		else if (bullet->mBulletType == BulletType::BULLET_TOXICFIREBALL) {
			++toxicFireballBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_TOXICPEA) ++toxicPeaBulletCount;
		else if (bullet->mBulletType == BulletType::BULLET_AURORA_PEA) ++auroraPeaBulletCount;
		else if (bullet->mBulletType == BulletType::BULLET_STAR) {
			++starBulletCount;
			if (bullet->GetRotationSpeedDegrees() != 0.0f) {
				++starSpinningBulletCount;
			}
			const float vx = bullet->GetVelocityX();
			const float vy = bullet->GetVelocityY();
			if (vx < 0.0f && vy == 0.0f) ++starLeftBulletCount;
			else if (vx == 0.0f && vy < 0.0f) ++starUpBulletCount;
			else if (vx == 0.0f && vy > 0.0f) ++starDownBulletCount;
			else if (vx > 0.0f && vy < 0.0f) ++starUpRightBulletCount;
			else if (vx > 0.0f && vy > 0.0f) ++starDownRightBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_CABBAGE) {
			++cabbageBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_MELT_SNOW) {
			++meltSnowBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_SALT_CRYSTAL) {
			++saltCrystalBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_MELON) {
			++melonBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_WINTERMELON) {
			++winterMelonBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_KERNEL) {
			++kernelBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_BUTTER) {
			++butterBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_BASKETBALL) {
			++basketballBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_THERMAL_PULSE) {
			++thermalPulseBulletCount;
		}
		else if (bullet->mBulletType == BulletType::BULLET_SPIKE) {
			++spikeBulletCount;
			if (bullet->TargetsFlying()) {
				++flyingTargetSpikeCount;
				flyingTargetSpikePiercedZombieCount += bullet->GetPiercedZombieCount();
			}
			else {
				++groundTargetSpikeCount;
				groundTargetSpikePiercedZombieCount += bullet->GetPiercedZombieCount();
			}
		}
		if (bullet->HasAnimatedPresentation()) ++animatedBulletCount;
		if (bullet->IsLobbedMotion()) ++lobbedBulletCount;
		const ShadowComponent* bulletShadow = bullet->GetShadow();
		out["bullets"].push_back({
			{ "id", id },
			{ "type", static_cast<int>(bullet->mBulletType) },
			{ "typeName", BulletTypeName(bullet->mBulletType) },
			{ "row", bullet->mRow },
			{ "renderOrder", bullet->GetRenderOrder() },
			{ "renderLayer", static_cast<int>(bullet->GetLayer()) },
			{ "x", pos.x }, { "y", pos.y },
			{ "terrainShadowYInt", static_cast<int>(std::lround(
				bullet->GetTerrainShadowYForTesting())) },
			{ "terrainClearanceOn1000", static_cast<int>(std::lround(
				(bullet->GetTerrainShadowYForTesting() - pos.y) * 1000.0f)) },
			{ "windAffected", bullet->IsTyphoonWindAffected() },
			{ "baseVelocityX", static_cast<int>(std::lround(bullet->GetVelocityX())) },
			{ "baseVelocityY", static_cast<int>(std::lround(bullet->GetVelocityY())) },
			{ "rotationDegrees", static_cast<int>(std::lround(
				bullet->GetRotationDegrees())) },
			{ "rotationSpeedDegrees", static_cast<int>(std::lround(
				bullet->GetRotationSpeedDegrees())) },
			{ "drawScaleOn1000", static_cast<int>(std::lround(
				bullet->GetDrawScale() * 1000.0f)) },
			{ "windVelocityX", static_cast<int>(std::lround(bullet->GetWindAdjustedVelocityX())) },
			{ "baseDamage", bullet->GetBulletDamage() },
			{ "windDamage", bullet->GetWindAdjustedDamage() },
			{ "winterCorrosionDamage", bullet->GetWinterCorrosionDamage() },
			{ "plantOriginKind", static_cast<int>(bullet->mPlantDamageOrigin.kind) },
			{ "plantOriginLineage", bullet->mPlantDamageOrigin.kind
				== PlantDamageOriginKind::PLANT_LINEAGE
				? PlantTypeName(bullet->mPlantDamageOrigin.lineage) : "NONE" },
			{ "threepeaterMotion", bullet->IsThreepeaterMotion() },
			{ "targetsFlying", bullet->TargetsFlying() },
			{ "hitTorchwoodColumn", bullet->GetHitTorchwoodColumn() },
			{ "hitAuroraTorchwoodColumn", bullet->GetHitAuroraTorchwoodColumn() },
			{ "auroraHitCount", bullet->GetAuroraHitCount() },
			{ "auroraHitZombieIDs", bullet->GetAuroraHitZombieIDs() },
			{ "auroraPlayedHitSound", bullet->HasPlayedAuroraHitSound() },
			{ "piercedZombieCount", bullet->GetPiercedZombieCount() },
			{ "piercedZombieIDs", bullet->GetPiercedZombieIDs() },
			{ "spikeDamageRemainders", bullet->GetSpikeDamageRemainders() },
			{ "animatedPresentation", bullet->HasAnimatedPresentation() },
			{ "toxicFireball", bullet->IsToxicFireball() },
			{ "fromPool", bullet->IsFromPool() },
			{ "poolType", BulletTypeName(bullet->GetPoolType()) },
			{ "colliderEnabled", bullet->GetColliderComponent()
				&& bullet->GetColliderComponent()->mEnabled },
			{ "shadowEnabled", bulletShadow && bulletShadow->IsEnabled() },
			{ "shadowVisible", bulletShadow && bulletShadow->IsVisible() },
			{ "shadowLastDrawReady", bulletShadow && bulletShadow->IsLastDrawReady() },
			{ "shadowScaleXOn1000", bulletShadow ? static_cast<int>(std::lround(
				bulletShadow->GetScale().x * 1000.0f)) : 0 },
			{ "shadowScaleYOn1000", bulletShadow ? static_cast<int>(std::lround(
				bulletShadow->GetScale().y * 1000.0f)) : 0 },
			{ "shadowOffsetXOn1000", bulletShadow ? static_cast<int>(std::lround(
				bulletShadow->GetOffset().x * 1000.0f)) : 0 },
			{ "shadowOffsetYOn1000", bulletShadow ? static_cast<int>(std::lround(
				bulletShadow->GetOffset().y * 1000.0f)) : 0 },
			{ "lobbedMotion", bullet->IsLobbedMotion() },
			{ "polarWindMiss", bullet->IsPolarWindMiss() },
			{ "polarGuided", bullet->IsPolarGuided() },
			{ "thermalEndpointXInt", static_cast<int>(std::lround(
				bullet->GetThermalEndpointX())) },
			{ "thermalEndpointYInt", static_cast<int>(std::lround(
				bullet->GetThermalEndpointY())) },
			{ "thermalOriginalPlantID", bullet->GetThermalOriginalPlantID() },
			{ "targetsIceWall", bullet->TargetsIceWall() },
			{ "lobElapsedMs", static_cast<int>(std::lround(
				bullet->GetLobElapsed() * 1000.0f)) },
			{ "lobDurationMs", static_cast<int>(std::lround(
				bullet->GetLobDuration() * 1000.0f)) },
			{ "lobProgressOn1000", static_cast<int>(std::lround(
				bullet->GetLobProgress() * 1000.0f)) },
			{ "lobArcHeightOn1000", static_cast<int>(std::lround(
				bullet->GetLobArcHeight() * 1000.0f)) },
			{ "lobTargetXInt", static_cast<int>(std::lround(
				bullet->GetLobTarget().x)) },
			{ "lobTargetYInt", static_cast<int>(std::lround(
				bullet->GetLobTarget().y)) },
			{ "cobCannonMotion", bullet->IsCobCannonMotion() },
			{ "cobElapsedMs", static_cast<int>(std::lround(
				bullet->GetCobElapsed() * 1000.0f)) },
			{ "cobDurationMs", static_cast<int>(std::lround(
				bullet->GetCobDuration() * 1000.0f)) },
			{ "cobTargetRow", bullet->GetCobTargetRow() },
			{ "cobTargetXInt", static_cast<int>(std::lround(
				bullet->GetCobTarget().x)) },
			{ "cobTargetYInt", static_cast<int>(std::lround(
				bullet->GetCobTarget().y)) },
			{ "cobShadowScaleOn1000", static_cast<int>(std::lround(
				bullet->GetCobCannonShadowScaleForTesting() * 1000.0f)) },
		});
	}
	out["bulletCount"] = static_cast<int>(out["bullets"].size());
	out["peaBulletCount"] = peaBulletCount;
	out["forwardPeaBulletCount"] = forwardPeaBulletCount;
	out["backwardPeaBulletCount"] = backwardPeaBulletCount;
	out["snowPeaBulletCount"] = snowPeaBulletCount;
	out["fireballBulletCount"] = fireballBulletCount;
	out["toxicFireballBulletCount"] = toxicFireballBulletCount;
	out["toxicPeaBulletCount"] = toxicPeaBulletCount;
	out["auroraPeaBulletCount"] = auroraPeaBulletCount;
	out["spikeBulletCount"] = spikeBulletCount;
	out["starBulletCount"] = starBulletCount;
	out["starLeftBulletCount"] = starLeftBulletCount;
	out["starUpBulletCount"] = starUpBulletCount;
	out["starDownBulletCount"] = starDownBulletCount;
	out["starUpRightBulletCount"] = starUpRightBulletCount;
	out["starDownRightBulletCount"] = starDownRightBulletCount;
	out["starSpinningBulletCount"] = starSpinningBulletCount;
	out["cabbageBulletCount"] = cabbageBulletCount;
	out["meltSnowBulletCount"] = meltSnowBulletCount;
	out["saltCrystalBulletCount"] = saltCrystalBulletCount;
	out["melonBulletCount"] = melonBulletCount;
	out["winterMelonBulletCount"] = winterMelonBulletCount;
	out["kernelBulletCount"] = kernelBulletCount;
	out["butterBulletCount"] = butterBulletCount;
	out["basketballBulletCount"] = basketballBulletCount;
	out["thermalPulseBulletCount"] = thermalPulseBulletCount;
	out["lobbedBulletCount"] = lobbedBulletCount;
	out["flyingTargetSpikeCount"] = flyingTargetSpikeCount;
	out["groundTargetSpikeCount"] = groundTargetSpikeCount;
	out["flyingTargetSpikePiercedZombieCount"] = flyingTargetSpikePiercedZombieCount;
	out["groundTargetSpikePiercedZombieCount"] = groundTargetSpikePiercedZombieCount;
	out["torchwoodProtectedPeaCount"] = torchwoodProtectedPeaCount;
	out["animatedBulletCount"] = animatedBulletCount;
	// 绝对 X 会随测试取证时点变化；整数化相对跨度用于稳定断言同帧同速弹丸。
	out["bulletXSpreadMilli"] = hasBulletX
		? static_cast<int>(std::lround((maxBulletX - minBulletX) * 1000.0f)) : 0;
	if (BulletPool* bulletPool = GameObjectManager::GetInstance().GetBulletPool()) {
		out["bulletPoolStorageCount"] = bulletPool->GetStorageCount();
		out["bulletPoolActiveCount"] = bulletPool->GetActiveCount();
		out["bulletPoolInactiveCount"] = bulletPool->GetInactiveCount();
		out["bulletPoolPeakCount"] = bulletPool->GetPeakCount();
		out["bulletPoolHitCount"] = bulletPool->GetHitCount();
		out["bulletPoolMissCount"] = bulletPool->GetMissCount();
		out["bulletPoolHitRateOn1000"] = static_cast<int>(std::lround(
			bulletPool->GetHitRate() * 1000.0f));
		out["bulletPoolActiveSlotsValid"] =
			bulletPool->HasConsistentActiveSlotsForTesting();
	}
	out["repeatingShootingHeadCount"] = repeatingShootingHeadCount;

	{
		SurvivalPerkManager& pm = board->GetPerkManager();
		nlohmann::json stacks;
		stacks["PLANT_DAMAGE_UP"]      = pm.GetStacks(PerkType::PLANT_DAMAGE_UP);
		stacks["ZOMBIE_HEALTH_UP"]     = pm.GetStacks(PerkType::ZOMBIE_HEALTH_UP);
		stacks["ZOMBIE_DAMAGE_RESIST"] = pm.GetStacks(PerkType::ZOMBIE_DAMAGE_RESIST);
		stacks["ZOMBIE_DAMAGE_UP"]     = pm.GetStacks(PerkType::ZOMBIE_DAMAGE_UP);
		stacks["ZOMBIE_INVULN_HITS"]   = pm.GetStacks(PerkType::ZOMBIE_INVULN_HITS);
		stacks["PLANT_REGEN"]          = pm.GetStacks(PerkType::PLANT_REGEN);
		stacks["PLANT_ATTACK_SPEED"]   = pm.GetStacks(PerkType::PLANT_ATTACK_SPEED);
		stacks["PLANT_DAMAGE_REDUCTION"] = pm.GetStacks(PerkType::PLANT_DAMAGE_REDUCTION);
		stacks["PLANT_SUN_BONUS"]        = pm.GetStacks(PerkType::PLANT_SUN_BONUS);
		stacks["PLANT_CARD_RECHARGE"]    = pm.GetStacks(PerkType::PLANT_CARD_RECHARGE);
		stacks["PLANT_MIST_REFINING"]    = pm.GetStacks(PerkType::PLANT_MIST_REFINING);
		stacks["PLANT_CORROSIVE_TOXIN"]  = pm.GetStacks(PerkType::PLANT_CORROSIVE_TOXIN);
		stacks["PLANT_UNYIELDING_ROOTS"] = pm.GetStacks(PerkType::PLANT_UNYIELDING_ROOTS);
		stacks["PLANT_DAMAGE_ECHO"]      = pm.GetStacks(PerkType::PLANT_DAMAGE_ECHO);
		stacks["ZOMBIE_FOG_BREAKOUT"]    = pm.GetStacks(PerkType::ZOMBIE_FOG_BREAKOUT);
		stacks["ZOMBIE_DEATH_RELAY"]     = pm.GetStacks(PerkType::ZOMBIE_DEATH_RELAY);
		stacks["ZOMBIE_DEVOUR_REPAIR"]   = pm.GetStacks(PerkType::ZOMBIE_DEVOUR_REPAIR);
		stacks["ZOMBIE_ARMOR_BREAK_RUSH"] = pm.GetStacks(PerkType::ZOMBIE_ARMOR_BREAK_RUSH);
		nlohmann::json perks;
		perks["stacks"]              = stacks;
		perks["zombieHealthMult"]    = pm.GetZombieHealthMultiplier();
		perks["zombieHealthOn100"]   = static_cast<int>(100.0 * pm.GetZombieHealthMultiplier() + 0.5);
		perks["plantDamageOn100"]    = pm.ScalePlantDamage(100);
		perks["damageToZombieOn100"] = pm.ScaleDamageToZombie(100);
		perks["damageToPlantOn100"]  = pm.ScaleDamageToPlant(100);
		perks["sunIncomeOn100"]      = pm.ScaleSunIncome(100);
		perks["zombieDamageMult"]    = pm.GetZombieDamageMultiplier();
		perks["zombieDamageOn100"]   = pm.ScaleZombieDamage(100);
		perks["zombieInvulnHits"]    = pm.GetZombieInvulnHits();
		perks["plantRegenPerPulse"]  = pm.GetPlantRegenPerPulse();
		perks["plantRegenCapOn300"]  = pm.GetPlantRegenHpCap(300);
		double asMult = pm.GetPlantAttackSpeedMultiplier();
		perks["plantAttackSpeedMult"] = asMult;                                  // 原始倍率
		perks["shootIntervalOn1500"]  = static_cast<int>(1500.0 / asMult + 0.5); // 整数化：1.5s 间隔被缩到多少 ms
		double rechargeMult = pm.GetPlantCardRechargeMultiplier();
		perks["plantCardRechargeMult"] = rechargeMult;
		perks["cardRechargeOn1000"] = static_cast<int>(1000.0 / rechargeMult + 0.5);
		perks["mistFuelMultiplierPct"] = static_cast<int>(std::lround(
			pm.GetMistFuelMultiplier() * 100.0));
		perks["plantDamageEchoHitCounter"] = board->GetPlantDamageEchoHitCounter();
		PerkOfferContext eligibilityContext;
		eligibilityContext.supportsPlanternMechanics = board->SupportsPlanternMechanics();
		const auto eligiblePlants = pm.AvailablePerks(
			PerkCategory::PLANT_BUFF, eligibilityContext, PerkRarity::COMMON);
		const auto eligibleZombies = pm.AvailablePerks(
			PerkCategory::ZOMBIE_CURSE, eligibilityContext, PerkRarity::COMMON);
		perks["mistRefiningEligible"] = std::find(eligiblePlants.begin(), eligiblePlants.end(),
			PerkType::PLANT_MIST_REFINING) != eligiblePlants.end();
		perks["fogBreakoutEligible"] = std::find(eligibleZombies.begin(), eligibleZombies.end(),
			PerkType::ZOMBIE_FOG_BREAKOUT) != eligibleZombies.end();
		out["perks"] = perks;
	}

	{
		nlohmann::json psel;
		psel["active"] = gs->IsPerkSelectActive();
		psel["offerCount"] = static_cast<int>(gs->GetCurrentPerkOffer().size());
		psel["currentPick"] = gs->GetPerkCurrentPick();
		psel["completedSteps"] = gs->GetPerkStepsCompleted();
		psel["completedPicks"] = gs->GetPerkPicksCompleted();
		psel["maxPicks"] = GameScene::SURVIVAL_PERK_PICKS_PER_ROUND;
		psel["refreshesRemaining"] = gs->GetPerkRefreshesRemaining();
		psel["maxRefreshes"] = GameScene::SURVIVAL_PERK_REFRESHES_PER_ROUND;
		nlohmann::json offers = nlohmann::json::array();
		int rarePlantOfferCount = 0;
		int conditionalOfferCount = 0;
		for (const PerkPairing& pr : gs->GetCurrentPerkOffer()) {
			const PerkInfo& plantInfo = SurvivalPerkManager::GetInfo(pr.plant);
			const PerkInfo& zombieInfo = SurvivalPerkManager::GetInfo(pr.zombie);
			if (plantInfo.rarity == PerkRarity::RARE) ++rarePlantOfferCount;
			if (plantInfo.condition != PerkCondition::NONE
				|| zombieInfo.condition != PerkCondition::NONE) ++conditionalOfferCount;
			offers.push_back({
				{ "plant", plantInfo.key },
				{ "zombie", zombieInfo.key },
				{ "plantRare", plantInfo.rarity == PerkRarity::RARE },
				{ "plantCondition", plantInfo.condition == PerkCondition::PLANTERN_MECHANICS
					? "PLANTERN_MECHANICS" : "NONE" },
				{ "zombieCondition", zombieInfo.condition == PerkCondition::PLANTERN_MECHANICS
					? "PLANTERN_MECHANICS" : "NONE" },
			});
		}
		psel["offers"] = offers;
		psel["rarePlantOfferCount"] = rarePlantOfferCount;
		psel["conditionalOfferCount"] = conditionalOfferCount;
		out["perkSelect"] = psel;
	}

	return true;
}
