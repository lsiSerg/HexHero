// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexArtifactConfig.h"
#include "HexHeroConfig.generated.h"

class UAnimSequence;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;
class UTexture2D;

/** The secondary skills of a hero, as the developer listed them on 2026-11-15: a hero has up to MaxSkills of them, each Basic, Advanced or
 *  Expert, and gains them as it rises in level */
UENUM()
enum class EHexSkill : uint8
{
	/** The damage of the spells that deal damage, Values more */
	Sorcery,
	/** A chance (Values) that neutral creatures join without a fight, and what they ask to join for less by SecondValues */
	Diplomacy,
	/** Points of movement on the map, Values more */
	Logistics,
	/** The spells of a school: Values of their mana less, SecondValues stronger, at Expert on every stack of a side where they may be */
	AirMagic,
	EarthMagic,
	FireMagic,
	WaterMagic,
	/** Mana back a day, Values more */
	Mysticism,
	/** The cost of rough ground over that of plains, Values of it less */
	Pathfinding,
	Num UMETA(Hidden)
};

/** A skill of the game: what it is called, what each of its levels gives and its picture */
USTRUCT(BlueprintType)
struct FHexSkillInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Skill")
	EHexSkill Skill = EHexSkill::Sorcery;

	UPROPERTY(EditAnywhere, Category="Skill")
	FString DisplayName;

	/** What it gives at Basic, Advanced and Expert, with {0} for its value and {1} for its second one, as shares in percents */
	UPROPERTY(EditAnywhere, Category="Skill")
	FString Description;

	/** Its values at Basic, Advanced and Expert */
	UPROPERTY(EditAnywhere, Category="Skill")
	TArray<float> Values;

	UPROPERTY(EditAnywhere, Category="Skill")
	TArray<float> SecondValues;

	/** Its picture, T_Skill<Name> of generate_ui_icons.py */
	UPROPERTY(EditAnywhere, Category="Skill")
	TSoftObjectPtr<UTexture2D> Icon;

	/** The value of a level, 1 to 3, 0 below Basic */
	float GetValue(int32 Level, bool bSecond = false) const
	{
		const TArray<float>& Each = bSecond ? SecondValues : Values;
		return Level > 0 && Each.Num() > 0 ? Each[FMath::Min(Level, Each.Num()) - 1] : 0.0f;
	}
};

/** Returns the nine skills, with numbers of Claude's after those of Heroes III */
TArray<FHexSkillInfo> MakeDefaultSkills();

/** Returns the name of a level of a skill: Basic, Advanced, Expert */
const TCHAR* GetSkillLevelName(int32 Level);

/**
 *  A class of heroes: the attributes its heroes start with and how they tend to grow. Attack and Defense are added to those of every unit of
 *  the army of the hero, Spell Power is the strength and length of its spells and Knowledge its mana (UHexHeroConfig::ManaPerKnowledge each).
 *  Nothing fights or levels up yet, so only the starting attributes show, in the panel of the heroes
 */
USTRUCT(BlueprintType)
struct FHexHeroClass
{
	GENERATED_BODY()

	/** Name of the class in heroes.json */
	UPROPERTY(EditAnywhere, Category="Class")
	FName Id;

	/** Name shown to the player */
	UPROPERTY(EditAnywhere, Category="Class")
	FString DisplayName;

	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	int32 Attack = 1;

	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	int32 Defense = 1;

	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	int32 SpellPower = 1;

	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	int32 Knowledge = 1;

	/** Chances, as weights, that a new level raises Attack, Defense, Spell Power or Knowledge by one; nothing levels up yet */
	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	float AttackChance = 1.0f;

	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	float DefenseChance = 1.0f;

	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	float SpellPowerChance = 1.0f;

	UPROPERTY(EditAnywhere, Category="Class", meta=(ClampMin=0))
	float KnowledgeChance = 1.0f;

	/** Names that new heroes of the class take, the first that no hero of the planet has */
	UPROPERTY(EditAnywhere, Category="Class")
	TArray<FString> Names;

	/** The portrait of its heroes in the list, the tips and the panels, as the developer asked on 2026-11-15 (PostLoad gives a class of the
	 *  default ones without one a render of the figures of the battles: the rider for the knight, the pilgrim for the wizard) */
	UPROPERTY(EditAnywhere, Category="Class")
	TSoftObjectPtr<UTexture2D> Portrait;

	/** The spells its heroes know when they start, by their Ids in DA_HexBattle, as the developer asked on 2026-11-15 that heroes learn the rest
	 *  in the mage guilds; MakeDefaultHeroClasses gives them to a class of that name that names none */
	UPROPERTY(EditAnywhere, Category="Class")
	TArray<FName> StartSpells;

	/** How likely a level offers each skill (EHexSkill), as weights in its order; missing ones weigh 1 */
	UPROPERTY(EditAnywhere, Category="Class")
	TArray<float> SkillWeights;

	/** The skill its heroes start with, at Basic, or Num for none */
	UPROPERTY(EditAnywhere, Category="Class")
	EHexSkill StartSkill = EHexSkill::Num;

	/** Mesh of its heroes on the map, or none for HeroMesh of the config */
	UPROPERTY(EditAnywhere, Category="Class")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/** A player who owns heroes: its name and the color of its banners. How players take turns is not decided yet, so one person moves them all */
USTRUCT(BlueprintType)
struct FHexHeroPlayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Player")
	FString Name;

	UPROPERTY(EditAnywhere, Category="Player")
	FLinearColor Color = FLinearColor::White;
};

/** Returns the classes and players a config has when it holds none */
/**
 *  How a map of zones is made (Zones of FHexPlanetGeneration, which the panel of a new planet sets), as the developer asked on 2026-11-14 ("згенеруй карту на 3 гравці - з
 *  них один ігрок, 1 АІ і один ніхто - пусті замки, шахти, у кожного має бути зона на карті, в яку будуть від 2 до 4 входів у сумі, на
 *  поверхні чи через підземелля, кожний прохід захищається нейтралом", and then for up to 8 zones): the planet split into zones around its
 *  equator by ranges of mountains, a sea at each pole, each zone reached by MinAccesses to MaxAccesses ways in, passes over the ranges and
 *  networks of the underground, each guarded by a neutral stack a hero beats after a week of hiring; a castle of the person, of each player
 *  the game moves and empty ones of nobody, and mines and piles on both layers (HexZoneMap, HexZonePieces). Every number is Claude's
 */
USTRUCT(BlueprintType)
struct FHexZoneMapGeneration
{
	GENERATED_BODY()

	/** The land inside the zones, as FHexPlanetGeneration takes it: mostly land, with ranges of its own that make a maze */
	UPROPERTY(EditAnywhere, Category="Zone map|Terrain", meta=(ClampMin=0, ClampMax=1))
	double LandShare = 0.9;

	UPROPERTY(EditAnywhere, Category="Zone map|Terrain", meta=(ClampMin=0, ClampMax=1))
	double ShallowsShare = 0.5;

	UPROPERTY(EditAnywhere, Category="Zone map|Terrain", meta=(ClampMin=0, ClampMax=1))
	double HillShare = 0.3;

	UPROPERTY(EditAnywhere, Category="Zone map|Terrain", meta=(ClampMin=0, ClampMax=1))
	double MountainShare = 0.16;

	/** Moisture of the land, as FHexPlanetGeneration takes it: land far from the sea is dry, and a map of zones has little sea */
	UPROPERTY(EditAnywhere, Category="Zone map|Terrain", meta=(ClampMin=-0.5, ClampMax=0.5))
	double Moisture = 0.3;

	/** Latitude in degrees from which the poles are sea, moved by a noise up to PolarWobble degrees either way */
	UPROPERTY(EditAnywhere, Category="Zone map|Zones", meta=(ClampMin=30, ClampMax=85))
	float PolarLatitude = 58.0f;

	UPROPERTY(EditAnywhere, Category="Zone map|Zones", meta=(ClampMin=0, ClampMax=20))
	float PolarWobble = 6.0f;

	/** How far a noise moves the borders of the zones from straight meridians, and how fine that noise is */
	UPROPERTY(EditAnywhere, Category="Zone map|Zones", meta=(ClampMin=0, ClampMax=1))
	float ZoneWarp = 0.12f;

	UPROPERTY(EditAnywhere, Category="Zone map|Zones", meta=(ClampMin=0.1, ClampMax=20))
	float ZoneWarpScale = 2.5f;

	/** Turns of the zones around the axis that are tried, the one with the fewest pentagons in a range kept, as a pentagon is never a mountain */
	UPROPERTY(EditAnywhere, Category="Zone map|Zones", meta=(ClampMin=1, ClampMax=360))
	int32 RotationTries = 36;

	/** Share of the cells next to a range that thicken it, by a noise, so a range is two to four cells wide */
	UPROPERTY(EditAnywhere, Category="Zone map|Zones", meta=(ClampMin=0, ClampMax=1))
	float RangeThickShare = 0.5f;

	/** Ways into each zone, passes and entrances of networks that lead to another zone together */
	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=1, ClampMax=8))
	int32 MinAccesses = 2;

	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=1, ClampMax=8))
	int32 MaxAccesses = 4;

	/** Most passes over the range between two zones, and most networks of the underground that join zones */
	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=0, ClampMax=4))
	int32 MaxPasses = 2;

	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=0, ClampMax=3))
	int32 MaxLinkNetworks = 2;

	/** How far a pass may lie from its place along the range, as a share of the range */
	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=0, ClampMax=0.4))
	float PassJitter = 0.12f;

	/** Steps from the castle of its zone an entrance of a network that joins zones lies, and one of a network of the zone alone */
	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=2))
	int32 LinkEntranceMinSteps = 6;

	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=2))
	int32 LinkEntranceMaxSteps = 14;

	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=2))
	int32 OwnEntranceMinSteps = 5;

	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=2))
	int32 OwnEntranceMaxSteps = 18;

	/** Networks of the underground each zone has to itself, two entrances each, for its mines below */
	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=0, ClampMax=2))
	int32 OwnNetworks = 1;

	/** Cells an entrance keeps from a range, and from another entrance or a castle */
	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=1))
	int32 EntranceRangeClear = 3;

	UPROPERTY(EditAnywhere, Category="Zone map|Ways", meta=(ClampMin=1))
	int32 EntranceSpacing = 5;

	/** Five zones or more lie in two rings, this many degrees north and south of the equator; up to four in one ring on it */
	UPROPERTY(EditAnywhere, Category="Zone map|Zones", meta=(ClampMin=0, ClampMax=45))
	float RingLatitude = 26.0f;

	/** Castles of nobody in a zone of nobody, the first in its middle and the others this many degrees north and south of it, as far as
	 *  there is room; a zone of a player has its own castle in its middle */
	UPROPERTY(EditAnywhere, Category="Zone map|Castles", meta=(ClampMin=1, ClampMax=3))
	int32 NobodyCastles = 2;

	/** Empty castles of nobody in the zone of a player besides its own, north and south of it, as the developer asked on 2026-11-14 */
	UPROPERTY(EditAnywhere, Category="Zone map|Castles", meta=(ClampMin=0, ClampMax=2))
	int32 PlayerZoneNobodyCastles = 1;

	UPROPERTY(EditAnywhere, Category="Zone map|Castles", meta=(ClampMin=0, ClampMax=45))
	float NobodyCastleLatitude = 14.0f;

	/** The players and their factions and the class of their heroes, by their Ids */
	UPROPERTY(EditAnywhere, Category="Zone map|Castles")
	int32 PersonPlayer = 0;

	UPROPERTY(EditAnywhere, Category="Zone map|Castles")
	int32 AiPlayer = 1;

	UPROPERTY(EditAnywhere, Category="Zone map|Castles")
	FString PersonFaction = TEXT("Dawn");

	UPROPERTY(EditAnywhere, Category="Zone map|Castles")
	FString AiFaction = TEXT("Decay");

	UPROPERTY(EditAnywhere, Category="Zone map|Castles")
	FString HeroClass = TEXT("Knight");

	/** How many more sites than DA_HexSites places, on both layers, as the developer asked not to spare them ("не шкодуй") */
	UPROPERTY(EditAnywhere, Category="Zone map|Pieces", meta=(ClampMin=0, ClampMax=10))
	float SiteDensity = 2.0f;

	/** Each zone has at least a mine of every resource and this many piles on its land, so a player grows in its zone alone, as the developer
	 *  asked on 2026-11-14 ("в кожній зоні має бути достатньо ресурсів щоб прокачатися"); what the scattering left out is put ZoneSiteMinSteps
	 *  to ZoneSiteMaxSteps from a castle of the zone, and the neutral stacks guard them by how far they lie from the castles of their zone */
	UPROPERTY(EditAnywhere, Category="Zone map|Pieces", meta=(ClampMin=0, ClampMax=40))
	int32 ZoneMinPiles = 8;

	UPROPERTY(EditAnywhere, Category="Zone map|Pieces", meta=(ClampMin=1))
	int32 ZoneSiteMinSteps = 4;

	UPROPERTY(EditAnywhere, Category="Zone map|Pieces", meta=(ClampMin=1))
	int32 ZoneSiteMaxSteps = 16;

	/** The guard of a way in: a stack of a creature of these levels, as many as the army a hero has after GuardWeeks beats with small losses,
	 *  as the developer asked on 2026-11-14 ("щоб за 2 тижні накоплення військ було достатньо для пробиття без суттевої втрати юнітів героя"),
	 *  which battles fought GuardTries times find: the army a hero starts with and GuardWeeks of what the dwellings of the first GuardDwellings
	 *  levels offer, of the weaker of the two factions, wins at least half of them losing at most GuardMaxLoss of its power on average against
	 *  the largest stack, of which GuardShare stands there */
	UPROPERTY(EditAnywhere, Category="Zone map|Guards", meta=(ClampMin=1, ClampMax=7))
	int32 GuardLevelMin = 3;

	UPROPERTY(EditAnywhere, Category="Zone map|Guards", meta=(ClampMin=1, ClampMax=7))
	int32 GuardLevelMax = 4;

	UPROPERTY(EditAnywhere, Category="Zone map|Guards", meta=(ClampMin=0, ClampMax=8))
	float GuardWeeks = 1.0f;

	UPROPERTY(EditAnywhere, Category="Zone map|Guards", meta=(ClampMin=0, ClampMax=1))
	float GuardMaxLoss = 0.15f;

	UPROPERTY(EditAnywhere, Category="Zone map|Guards", meta=(ClampMin=1, ClampMax=7))
	int32 GuardDwellings = 4;

	UPROPERTY(EditAnywhere, Category="Zone map|Guards", meta=(ClampMin=0.1, ClampMax=2))
	float GuardShare = 0.9f;

	UPROPERTY(EditAnywhere, Category="Zone map|Guards", meta=(ClampMin=1, ClampMax=9))
	int32 GuardTries = 3;
};

TArray<FHexHeroClass> MakeDefaultHeroClasses();
TArray<FHexHeroPlayer> MakeDefaultHeroPlayers();

/**
 *  Every value of the heroes of the hex planet (UHexHeroes): the rules of their movement, their classes and players, how they look on the map
 *  and how they are lit. DA_HexHeroes in Content/HexPlanet/Heroes, which import_planet_heroes.py creates when it is missing, so tune it by
 *  hand; a missing one falls back to the defaults of the class.
 *
 *  Movement, as the developer agreed on 2026-11-10: a hero has MovePoints every day, and a step onto a cell costs the factor of its level
 *  (1 on plains and on the passages of the underground, HillCost on hills) times MoveCostMultiplier of its biome, times RiverCrossCost of
 *  DA_HexPlanet across a river without a bridge. The sea, mountains, a biome that blocks heroes, a kind of object that blocks movement and a
 *  cell another hero stands on cannot be entered. A step is taken only while the points left pay for it, so what is left over is lost at the
 *  end of the day; a step dearer than a whole day takes a day
 */
UCLASS(BlueprintType)
class UHexHeroConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UHexHeroConfig();

	/** Points of movement a hero has every day; a step onto plains of grass costs 1 */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=1))
	float MovePoints = 10.0f;

	/** Factor on the cost of a step onto hills */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0.1))
	float HillCost = 1.5f;

	/** How many days of movement a way may take at most, which bounds the search for it */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=1, ClampMax=60))
	int32 PathMaxDays = 7;

	/** Cells a hero walks a second */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0.5))
	float WalkSpeed = 4.5f;

	/** A hero sets out and stops smoothly, as the developer asked on 2026-11-11: seconds in which it reaches WalkSpeed from standing, seconds
	 *  in which it brakes from it to a stop where its way or its points end, and the share of WalkSpeed it never walks slower than, so it
	 *  gets there */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=5))
	float WalkStartTime = 0.45f;

	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=5))
	float WalkStopTime = 0.6f;

	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0.02, ClampMax=1))
	float WalkMinSpeedShare = 0.15f;

	/** How far a hero turns towards where it walks a second, in degrees */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=30))
	float TurnSpeed = 540.0f;

	/** How high a walking hero bobs, in world units, and how many times a cell */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0))
	float WalkBob = 3.0f;

	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0))
	float WalkBobsPerCell = 2.0f;

	/** What going through an entrance to the other layer costs a hero, in points of movement, from the cell before its mouth to the cell
	 *  before the mouth on the other layer: a point, as the developer decided on 2026-11-12 with the entrances built into mountains and
	 *  walls, so a hero needs a point to go in (nothing earlier that day, while a hero stood on the entrance itself), and seconds the hero
	 *  a person plays waits on the other side before it walks on, while the camera comes after it */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0))
	float EntranceCost = 1.0f;

	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=10))
	float EntrancePause = 1.0f;

	/** A hero that goes through an entrance walks from the middle of the cell before it into its mouth over EntranceWalkTime seconds,
	 *  growing smaller down to EntranceShrink of its size, and out of the mouth on the other side as it grows again, as the developer asked
	 *  on 2026-11-12, when it only vanished on its cell; where the person sees it */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=5))
	float EntranceWalkTime = 0.55f;

	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0.02, ClampMax=1))
	float EntranceShrink = 0.75f;

	/** And it dissolves in the glow of the mouth, as the developer asked on 2026-11-12, when at 0.3 of its size it grew too small ("занадто
	 *  зменшується - може вийде розчиняти при проході через світіння"): from EntranceFadeStart of its walk into the mouth on it goes in grains
	 *  HeroFadeGrain across, those about to go glowing HeroFadeGlow in the color of the network, and it comes out of the mouth on the other
	 *  side the same way back (HeroFade of M_HexPlanetHero, HERO_MASK_CODE of create_planet_assets.py) */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=0.95))
	float EntranceFadeStart = 0.1f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float HeroFadeGrain = 4.0f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float HeroFadeGlow = 8.0f;

	/** The hero a person plays walks out of the mouth this long after the screen is no longer black from the trip of the camera */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=3))
	float EntranceOutDelay = 0.2f;

	/** Seconds a hero of a player the game moves waits at most for the camera to come to its layer, where the person who watches would see
	 *  it on the layer that is not shown; the camera goes there first and then it sets out, as the developer asked on 2026-11-11 */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=20))
	float AiViewWaitMax = 4.0f;

	/** The camera glides after the hero chosen while it walks */
	UPROPERTY(EditAnywhere, Category="Movement")
	bool bCameraFollows = true;

	/** Seconds the hero chosen stays chosen after it stopped with no steps left, before the next hero that can still move is chosen, so the
	 *  player sees that it used its movement and where it stopped, as the developer asked on 2026-11-11 */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=10))
	float NextHeroDelay = 1.2f;

	/** The fog of war, as the developer decided on 2026-11-11: each player sees only what its heroes explored, on each layer, and what was
	 *  explored once stays seen, as in Heroes III. A hero explores the cells up to SightRadius steps around the one it stands on, underground
	 *  only the passages among them, as nobody walks the voids */
	UPROPERTY(EditAnywhere, Category="Rules")
	bool bShroud = true;

	/** While the game is played the cells the hero chosen reaches today show on the ground, as the developer asked on 2026-11-12
	 *  (UHexHeroes::UpdateReach; how they look is ReachLine* and the rest of the config of each layer) */
	UPROPERTY(EditAnywhere, Category="Look")
	bool bShowReach = true;

	/** A hero a person moves may be sent over ground its player has not explored, as the developer decided on 2026-11-11, so the fog forbids no
	 *  move: a step onto such a cell counts UnknownCost, that of open plains, whatever lies there. Its way is checked when it reaches the first
	 *  cell of it that lay under the fog, by which it has seen what lies beyond: if the rest of its way is still open and no other way to the
	 *  cell it was sent to is cheaper by more than RerouteStopPoints, it walks on to the next cell of it that is still under the fog, and else
	 *  it stops, so the person decides with the new way shown */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0.1))
	float UnknownCost = 1.0f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float RerouteStopPoints = 0.0f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=1, ClampMax=30))
	int32 SightRadius = 5;

	/** A hero that takes a mine or a mill goes onto its cell, to its place there (FHexSiteKind::HeroPlace), over this many seconds, and
	 *  guards it while it stands there, as the developer asked on 2026-11-11 */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0.05, ClampMax=5))
	float SiteWalkTime = 0.7f;

	/** A castle opens the fog of war for its owner too, this many times as far as a hero, as the developer asked on 2026-11-11 (3 then;
	 *  1.5 since 2026-11-12, as 15 steps opened 18% of Small at the start), rounded to whole steps */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=10))
	float CastleSightFactor = 1.5f;

	/** A player the game moves by itself (the Heroes tab of the palette, kept in heroes.json) sends its heroes to open the map, each to the
	 *  nearest cell from which it sees ground its player has not explored: seconds a hero of such a player stands before it sets out again */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=10))
	float AiThinkTime = 0.5f;

	/** A hero of such a player goes for what its player has seen and can gather before it explores, as the developer asked on 2026-11-11
	 *  ("АІ - має почати збирати їх, гравець може захопити шахти - АІ захоче їх повернути"): a pile or a mill whose way costs at most this
	 *  many days of its movement, a mine of nobody or of another player within AiMineReachDays, and a mine another player took from its own
	 *  within AiLostMineReachDays. Of several it takes the one nearest by the share of its reach, so a mine it lost twice as far as a pile
	 *  still comes first; 0 leaves a kind alone */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=60))
	float AiPileReachDays = 1.5f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=60))
	float AiMineReachDays = 3.0f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=60))
	float AiLostMineReachDays = 7.0f;

	/** A hero of such a player fights a hero, a castle or a neutral stack within its reach when the battle, fought AiForecastTries times
	 *  in advance, is won at least AiWinShare of the times and costs it at most AiLossShare of the power of its army on average, as the
	 *  developer asked on 2026-11-14 that it gathers, builds and hires to break into the other zones and take everything; it weighs at
	 *  most AiForecastMax foes a move, the nearest first */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=1))
	float AiWinShare = 0.65f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=1))
	float AiLossShare = 0.6f;

	/** Losses it bears to fight a neutral stack while it has anything else to do; past that only a stack that stands in its way to the rest of
	 *  the map, when nothing else is left, for AiLossShare */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=1))
	float AiNeutralLossShare = 0.25f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=1, ClampMax=9))
	int32 AiForecastTries = 3;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=1, ClampMax=32))
	int32 AiForecastMax = 6;

	/** Its strongest hero goes back to a castle of its own within this many days to take the creatures there, when they count at least
	 *  AiReinforceShare of its army, or when it has nothing better to do */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=30))
	float AiReinforceDays = 4.0f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=10))
	float AiReinforceShare = 0.35f;

	/** A hero of such a player after its leader carries the armies of its castles to the leader, as the developer asked on 2026-11-14, once they
	 *  would add this share to the army of the leader; the leader then goes home only for a castle within a day */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=10))
	float AiCarryShare = 0.2f;

	/** A hero but the leader flees into a castle of its own from a hero of another player it sees within AiFleeDays whose army counts
	 *  AiFleePower times its own, as the developer asked on 2026-11-14 */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=10))
	float AiFleeDays = 1.5f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=1, ClampMax=100))
	float AiFleePower = 3.0f;

	/** A castle a hero takes counts as this many times nearer than a hero or a neutral stack, and one taken from its player before as nearer
	 *  still, as the developer asked on 2026-11-14 that castles, which give armies, come first, those lost above all */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=1, ClampMax=100))
	float AiCastleRank = 2.0f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=1, ClampMax=100))
	float AiLostCastleRank = 3.0f;

	/** Most heroes such a player hires, one a day in a castle with gold left for AiHeroGoldReserve, as the developer asked on 2026-11-14
	 *  that it buys heroes to take land faster; with none it hires one whatever is left */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=8))
	int32 AiMaxHeroes = 4;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0))
	int32 AiHeroGoldReserve = 500;

	/** Seconds the camera stays where a hero of such a player went into the fog of war, before the game goes on with it or the next, and
	 *  seconds it stays where one is about to come out of the fog, before it does, so the person sees where it went and where it comes from,
	 *  as the developer asked on 2026-11-11; the hero waits meanwhile */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=10))
	float AiFogHoldTime = 0.8f;

	/** And seconds it stays at an entrance or exit such a hero comes out of into what the person sees, as the developer did not see an
	 *  enemy come up out of an underground they had not explored on 2026-11-11, before it took the castle beside the entrance: longer, as
	 *  the camera may come from far and the hero is seen there for the first time */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=10))
	float AiEntranceHoldTime = 2.0f;

	/** Days of a week; nothing happens by the week yet, the panel of the heroes shows it */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=1))
	int32 DaysPerWeek = 7;

	/** Mana a hero has for each point of Knowledge */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	int32 ManaPerKnowledge = 10;

	/** Mana a hero gets back every day, before Mysticism; in a castle of its own with a mage guild it gets all of it back */
	UPROPERTY(EditAnywhere, Category="Rules|Magic", meta=(ClampMin=0))
	int32 ManaPerDay = 1;

	/** The skills, as the developer listed them on 2026-11-15 (MakeDefaultSkills when empty), and how many a hero has at most */
	UPROPERTY(EditAnywhere, Category="Rules|Skills")
	TArray<FHexSkillInfo> Skills;

	UPROPERTY(EditAnywhere, Category="Rules|Skills", meta=(ClampMin=1, ClampMax=12))
	int32 MaxSkills = 6;

	/** The experience a hero needs for its second level, and how much more each level after needs than the one before it, a factor */
	UPROPERTY(EditAnywhere, Category="Rules|Levels", meta=(ClampMin=1))
	int32 LevelExperience = 600;

	UPROPERTY(EditAnywhere, Category="Rules|Levels", meta=(ClampMin=1, ClampMax=3))
	float LevelExperienceGrowth = 1.2f;

	UPROPERTY(EditAnywhere, Category="Rules|Levels", meta=(ClampMin=2, ClampMax=99))
	int32 MaxLevel = 40;

	/** Neutral creatures may offer to join a hero whose army counts this many times theirs (FHexCreatureStats::GetPower): for nothing by the
	 *  chance of Diplomacy, else for their price less what Diplomacy takes off, if the hero has the gold and a slot for them */
	UPROPERTY(EditAnywhere, Category="Rules|Diplomacy", meta=(ClampMin=1))
	float JoinPowerRatio = 2.0f;

	/** A hero without Diplomacy is offered to buy them only with this chance */
	UPROPERTY(EditAnywhere, Category="Rules|Diplomacy", meta=(ClampMin=0, ClampMax=1))
	float JoinBuyChance = 0.0f;

	/** Returns a skill, or null */
	const FHexSkillInfo* FindSkill(EHexSkill Skill) const;

	/** The experience a hero needs for a level, 0 for the first */
	int32 GetLevelExperience(int32 Level) const;

	/** The classes of heroes; MakeDefaultHeroClasses when empty */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<FHexHeroClass> Classes;

	/** The players, by index in heroes.json; MakeDefaultHeroPlayers when empty */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<FHexHeroPlayer> Players;

	/** How a map of zones is made */
	UPROPERTY(EditAnywhere, Category="Zone map")
	FHexZoneMapGeneration ZoneMap;

	/** Each hero lights the land and the underground around itself, as the developer asked on 2026-11-10, as the focus of the camera did before
	 *  there were heroes and still does on a planet without any: the day of the surface (bHeroShade of DA_HexPlanet) and the light, the clear
	 *  fog and the lit dust of the underground (bHeroLight of DA_HexUnderground). The MaxLights heroes nearest the point the camera looks at
	 *  light, at most 8, which the materials take */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=1, ClampMax=8))
	int32 MaxLights = 8;

	/** How many of the lights of the heroes underground cast shadows, those nearest the point the camera looks at; the shadows of one cost
	 *  about 0.2 ms of GPU on the machine it was made on */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0, ClampMax=8))
	int32 ShadowLights = 1;

	/** How far the light of a hero reaches, as a share of the cells of plains it can walk in a day (MovePoints), as the developer decided on
	 *  2026-11-10 that one day it reaches as far as the hero can go in a turn; 0 keeps HeroLightReach of the config of the layer */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float LightMoveShare = 0.0f;

	/** Mesh of a hero on the map, standing on its origin with X forward, and its material, which takes PlayerColor */
	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> HeroMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SM_PlanetHero.SM_PlanetHero")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UMaterialInterface> HeroMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetHero.M_HexPlanetHero")));

	/** The animated hero, as the developer asked on 2026-11-11 ("знайди меші коня та людини, спрости їх і якусь анімацію зроби - мув та
	 *  стоп, початок мува, реакція на вибір"): a skeletal mesh that is drawn in place of HeroMesh when it is there, a rider on a horse
	 *  built from two CC0 models (Tools/HexPlanetHeroes/blender_build_rider.py), RiderScale times its size before HeroScale and turned
	 *  by RiderYaw degrees, so its front looks along X. It stands in IdleAnimation and walks in MoveAnimation, MoveAnimRate fast at
	 *  WalkSpeed; StartAnimation plays once as it sets out and StopAnimation, from StopAnimStart of its length, as it stops, and
	 *  ChosenAnimation once when it is chosen while it stands. The developer found leaps at setting out and stopping and rearing when chosen too much on 2026-11-11, and then a sway of the whole horse too ("злегка", "тільки голова"), so it sets out and stops through a few steps of its walk, and chosen it only turns its head (Look, which blender_build_rider.py makes); each blends into the next over AnimBlendTime seconds. One left empty is
	 *  skipped. */
	UPROPERTY(EditAnywhere, Category="Look|Animation")
	TSoftObjectPtr<USkeletalMesh> RiderMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SK_PlanetRider/SkeletalMeshes/SK_PlanetRider.SK_PlanetRider")));

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0.001))
	float RiderScale = 0.085f;

	UPROPERTY(EditAnywhere, Category="Look|Animation")
	float RiderYaw = -90.0f;

	UPROPERTY(EditAnywhere, Category="Look|Animation")
	TSoftObjectPtr<UAnimSequence> IdleAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SK_PlanetRider/SkeletalMeshes/SK_PlanetRiderIdle.SK_PlanetRiderIdle")));

	UPROPERTY(EditAnywhere, Category="Look|Animation")
	TSoftObjectPtr<UAnimSequence> MoveAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SK_PlanetRider/SkeletalMeshes/SK_PlanetRiderGallop.SK_PlanetRiderGallop")));

	UPROPERTY(EditAnywhere, Category="Look|Animation")
	TSoftObjectPtr<UAnimSequence> StartAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SK_PlanetRider/SkeletalMeshes/SK_PlanetRiderWalk.SK_PlanetRiderWalk")));

	UPROPERTY(EditAnywhere, Category="Look|Animation")
	TSoftObjectPtr<UAnimSequence> StopAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SK_PlanetRider/SkeletalMeshes/SK_PlanetRiderWalk.SK_PlanetRiderWalk")));

	UPROPERTY(EditAnywhere, Category="Look|Animation")
TSoftObjectPtr<UAnimSequence> ChosenAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SK_PlanetRider/SkeletalMeshes/SK_PlanetRiderLook.SK_PlanetRiderLook")));

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0))
	float AnimBlendTime = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0.01))
	float IdleAnimRate = 1.0f;

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0.01))
	float MoveAnimRate = 1.0f;

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0.01))
	float StartAnimRate = 1.4f;

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0.01))
	float StopAnimRate = 1.2f;

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0, ClampMax=0.95))
	float StopAnimStart = 0.4f;

	UPROPERTY(EditAnywhere, Category="Look|Animation", meta=(ClampMin=0.01))
	float ChosenAnimRate = 1.3f;

	/** Scale of the mesh of a hero */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float HeroScale = 2.5f;

	/** A hero, its ring and the marks of its way grow as the camera leaves them, so they still show from afar: at their own size up to FarStart
	 *  from the camera, then larger with the distance, up to FarScale times */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=1))
	float FarStart = 1800.0f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=1))
	float FarScale = 3.0f;

	/** How much of the color of the player the cloak and the banner take, and how much they glow in it, so a hero reads in the dark */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0, ClampMax=1))
	float HeroColorShare = 1.0f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float HeroGlow = 0.25f;

	/** The banner the animated hero carries waves, as the developer asked on 2026-11-11: its far end moves by HeroFlagWave units of
	 *  the mesh, at HeroFlagWaveSpeed */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float HeroFlagWave = 16.0f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float HeroFlagWaveSpeed = 6.0f;

	/** How far from the body of a hero a click still chooses it, in world units, and how tall that body is */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=1))
	float PickRadius = 16.0f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=1))
	float PickHeight = 40.0f;

	/** The ring on the ground under the hero chosen, RingRadius wide, which glows in RingColor and breathes by RingPulse at RingPulseSpeed. It
	 *  is a band of RingSegments pieces laid on the ground under it, so it follows the relief and no hill cuts it, as the developer found a
	 *  flat ring did on 2026-11-11, drawn RingDepthBias nearer the camera */
	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=1))
	float RingRadius = 74.0f;

	/** The color of the ring of what is chosen, a hero or a castle: that of the way of a hero (PathColor), as the developer asked on
	 *  2026-11-12 ("поміняй селекшин на колір як пошук шляху"), after the color of the player until then */
	UPROPERTY(EditAnywhere, Category="Look|Marks")
	FLinearColor RingColor = FLinearColor(1.0f, 0.58f, 0.07f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=8, ClampMax=256))
	int32 RingSegments = 64;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float RingDepthBias = 4.0f;

	/** The marks of the way of the hero chosen to the cell it is pointed to, a flat mesh of radius 1: a dot of PathDotRadius on each cell and a
	 *  larger mark of PathEndRadius on the last, lying on the ground there, PathLift above it, in PathColor as far as the hero gets today,
	 *  PathFarColor beyond, and PathBlockedColor for a last cell it cannot reach */
	UPROPERTY(EditAnywhere, Category="Look|Marks")
	TSoftObjectPtr<UStaticMesh> PathMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SM_PlanetHeroDot.SM_PlanetHeroDot")));

	UPROPERTY(EditAnywhere, Category="Look|Marks")
	TSoftObjectPtr<UStaticMesh> PathEndMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SM_PlanetHeroTarget.SM_PlanetHeroTarget")));

	/** Material of the ring, unlit, in MarkColor, and that of the marks of a way, the same graph, each mark in the color of the custom data of
	 *  its instance. Both test the depth, so a way lies on the ground under the hero, the trees and whatever else stands on it, as the developer
	 *  asked on 2026-11-11 (until then a way was drawn over everything) */
	UPROPERTY(EditAnywhere, Category="Look|Marks")
	TSoftObjectPtr<UMaterialInterface> MarkMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetHeroMark.M_HexPlanetHeroMark")));

	UPROPERTY(EditAnywhere, Category="Look|Marks")
	TSoftObjectPtr<UMaterialInterface> PathMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetHeroPath.M_HexPlanetHeroPath")));

	/** The line that joins the marks of a way, a flat mesh one unit long and wide, PathLineWidth wide, laid from mark to mark in pieces at
	 *  most PathLineStep long, each on the ground under its ends, so the line follows the relief */
	UPROPERTY(EditAnywhere, Category="Look|Marks")
	TSoftObjectPtr<UStaticMesh> PathLineMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SM_PlanetHeroLine.SM_PlanetHeroLine")));

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0.5))
	float PathLineWidth = 7.0f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=2))
	float PathLineStep = 10.0f;

	/** How much the line of a way and its marks cover the ground under them, and how bright they glow; they are see-through, the line more */
	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0, ClampMax=1))
	float PathLineOpacity = 0.6f;

	/** The outline of what a way ends at (AHexPlanet::SetTargetCells): a foe in PathTargetColor, saturated as the developer asked on 2026-11-13
	 *  so the white edge of where the hero gets today does not cover it, a hero or castle of its own in PathColor, PathTargetWidthScale times
	 *  as wide as the outline of the cursor while the game is played and covering PathTargetOpacity of the ground */
	UPROPERTY(EditAnywhere, Category="Look|Marks")
	FLinearColor PathTargetColor = FLinearColor(1.0f, 0.02f, 0.01f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0.1, ClampMax=4))
	float PathTargetWidthScale = 2.0f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0, ClampMax=1))
	float PathTargetOpacity = 1.0f;

	/** The panel at the cursor while the game is played (UHexHeroes::GetMapTip): whether the creatures of another player and the neutral stacks
	 *  show their numbers, which a skill of scouting will give one day, else a range of them ("10-19"), as the developer asked on 2026-11-13 */
	UPROPERTY(EditAnywhere, Category="Look|Tips")
	bool bTipExactCounts = false;

	/** A rough forecast of a battle there, by the power of the army of the hero chosen against what is there (Claude's): above ForecastEasy
	 *  times as strong an easy victory, above ForecastLikely a likely one, above ForecastEven an even fight, above ForecastHard a hard one,
	 *  else deadly; rough on purpose, as the developer asked to leave a battle its surprises */
	UPROPERTY(EditAnywhere, Category="Look|Tips", meta=(ClampMin=0))
	float ForecastEasy = 2.5f;

	UPROPERTY(EditAnywhere, Category="Look|Tips", meta=(ClampMin=0))
	float ForecastLikely = 1.4f;

	UPROPERTY(EditAnywhere, Category="Look|Tips", meta=(ClampMin=0))
	float ForecastEven = 0.8f;

	UPROPERTY(EditAnywhere, Category="Look|Tips", meta=(ClampMin=0))
	float ForecastHard = 0.5f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0, ClampMax=1))
	float PathMarkOpacity = 0.85f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float PathBrightness = 1.0f;

	/** How far the marks of a way are drawn towards the camera, in world units, so bumps of the ground between the points they lie on do not
	 *  cut them, while what stands higher than that on the ground still hides them */
	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float PathDepthBias = 5.0f;

	/** The numbers of the days on a way, drawn on the screen over its marks: the size of their letters and the colors of the plate and the text */
	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=6, ClampMax=40))
	int32 PathLabelSize = 12;

	UPROPERTY(EditAnywhere, Category="Look|Marks")
	FLinearColor PathLabelBack = FLinearColor(0.02f, 0.02f, 0.02f, 0.85f);

	UPROPERTY(EditAnywhere, Category="Look|Marks")
	FLinearColor PathLabelColor = FLinearColor(1.0f, 0.9f, 0.55f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=1))
	float PathDotRadius = 13.0f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=1))
	float PathEndRadius = 34.0f;

	/** A way that goes through an entrance shows it, as the developer asked on 2026-11-12 ("візуалізірувати додатковою точкою та відрізком до
	 *  прохода, може не точка я щось інше що буде символізувати прохід через гору - телепорт"): on the layer it leaves a line from the middle
	 *  of the cell to the mouth and there a hoop with a swirl in it, a flat mesh of radius 1 that stands upright in the mouth, PathPortalRadius
	 *  large with its middle PathPortalLift of that above the ground, and on the layer it comes to the same hoop in the mouth there and a line
	 *  from it to the middle of the cell */
	UPROPERTY(EditAnywhere, Category="Look|Marks")
	TSoftObjectPtr<UStaticMesh> PathPortalMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SM_PlanetHeroPortal.SM_PlanetHeroPortal")));

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=1))
	float PathPortalRadius = 26.0f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float PathPortalLift = 1.05f;

	/** And which way the way goes through it, as the developer asked on 2026-11-12 ("позначити напрям руху"; an arrow, then three hoops of
	 *  three sizes that stood still were tried, which read as a heap to them: "потрібно анімувати з рухом та прозорістю"): PathPortalLayers
	 *  more hoops run from the hoop along the way, one after the other, each over PathPortalTravel in PathPortalPulseTime seconds, growing
	 *  smaller down to PathPortalLayerScale of the hoop and fading in and out on its way: out of the mouth where the way comes out of it,
	 *  and the same mirrored where it goes in, from the hoop on into the passage, drawn over the rock they run into (the custom data of an
	 *  instance, HERO_MARK_OFFSET_CODE). Slower and closer together since the developer asked so the same day (2 hoops in 1.4 s at first) */
	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0, ClampMax=8))
	int32 PathPortalLayers = 4;

	/** Those that run are plain hoops, without the swirl, which in several hoops at once was too much to read */
	UPROPERTY(EditAnywhere, Category="Look|Marks")
	TSoftObjectPtr<UStaticMesh> PathPortalRingMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Heroes/SM_PlanetHeroPortalRing.SM_PlanetHeroPortalRing")));

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float PathPortalTravel = 42.0f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0.05, ClampMax=1))
	float PathPortalLayerScale = 0.4f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0.1))
	float PathPortalPulseTime = 2.8f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float PathLift = 3.0f;

	UPROPERTY(EditAnywhere, Category="Look|Marks")
	FLinearColor PathColor = FLinearColor(1.0f, 0.58f, 0.07f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Marks")
	FLinearColor PathFarColor = FLinearColor(0.6f, 0.3f, 0.07f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Marks")
	FLinearColor PathBlockedColor = FLinearColor(1.0f, 0.2f, 0.15f, 1.0f);

	/** How bright the ring glows, and how much it breathes and how fast */
	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float MarkBrightness = 2.0f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0, ClampMax=1))
	float RingPulse = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=0))
	float RingPulseSpeed = 0.6f;

	/** The artifacts of the game, as the developer asked on 2026-11-15 (FHexArtifact); MakeDefaultArtifacts when empty */
	UPROPERTY(EditAnywhere, Category="Artifacts")
	TArray<FHexArtifact> Artifacts;

	/** Which version of MakeDefaultArtifacts the config holds; PostLoad brings an older one up to date */
	UPROPERTY()
	int32 ArtifactsVersion = 0;

	/** How many artifacts a hero carries in its backpack besides those it wears */
	UPROPERTY(EditAnywhere, Category="Artifacts", meta=(ClampMin=0, ClampMax=64))
	int32 BackpackSize = 24;

	/** What a beaten hero wore and carried goes to the hero that beat it, as far as its backpack holds */
	UPROPERTY(EditAnywhere, Category="Artifacts")
	bool bWinnerTakesArtifacts = true;

	/** Boats, as the developer asked on 2026-11-15 ("додай човники"): a hero boards one that lies at a coast, or one a shipyard builds, and
	 *  sails the sea; a step at sea costs SeaStepCost, as one over plains, and going aboard or ashore takes all the points left that day, as
	 *  in Heroes III ("All points left"). A hero a person plays alone sails, as the game moves its heroes on land */
	UPROPERTY(EditAnywhere, Category="Boats", meta=(ClampMin=0.01))
	float SeaStepCost = 1.0f;

	/** The mesh of a boat a hero sails in, and of one that lies at a coast (the site kind Boat), standing on its origin at the water with its bow along +X */
	UPROPERTY(EditAnywhere, Category="Boats")
	TSoftObjectPtr<UStaticMesh> BoatMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Sites/SM_PlanetBoat.SM_PlanetBoat")));

	UPROPERTY(EditAnywhere, Category="Boats", meta=(ClampMin=0.1))
	float BoatScale = 3.0f;

	/** How high above the water a hero stands in its boat, in units of the mesh of the boat, and how far down the hero is scaled meanwhile */
	UPROPERTY(EditAnywhere, Category="Boats", meta=(ClampMin=0))
	float BoatDeckLift = 4.0f;

	UPROPERTY(EditAnywhere, Category="Boats", meta=(ClampMin=0.1, ClampMax=1.5))
	float BoatHeroScale = 0.75f;

	/** How far towards the bow from the middle of its boat a hero stands, in units of the mesh of the boat, clear of the mast and its sail, as
	 *  the developer found the two through each other on 2026-11-15: right behind the mast, which stands towards the bow since the developer
	 *  asked the same day for the sail at the front and the hero behind it */
	UPROPERTY(EditAnywhere, Category="Boats")
	float BoatHeroForward = -7.0f;

	/** How far a boat rises and falls on the water and how often, and how far it rolls in degrees */
	UPROPERTY(EditAnywhere, Category="Boats", meta=(ClampMin=0))
	float BoatBob = 1.2f;

	UPROPERTY(EditAnywhere, Category="Boats", meta=(ClampMin=0))
	float BoatBobSpeed = 0.7f;

	UPROPERTY(EditAnywhere, Category="Boats", meta=(ClampMin=0))
	float BoatRoll = 3.0f;

	/** The wake of a boat that sails, as the developer asked on 2026-11-15: every WakeInterval seconds three patches of foam WakeSize units
	 *  of the mesh of the boat across are laid on the water WakeStern units of that mesh behind its middle, one churning in the track of the
	 *  boat and fading in the first WakeCoreLife of WakeLife, and one to each side that drifts WakeDrift world units out over WakeLife, so
	 *  the sides draw the arms of a V behind it (until the developer found the rings of the first build odd the same day); each spreads
	 *  WakeSpread times as wide and fades over WakeLife seconds (M_HexPlanetWake, WakeMaterial), WakePool patches at most on a layer, given
	 *  out in turn */
	UPROPERTY(EditAnywhere, Category="Boats|Wake")
	TSoftObjectPtr<UMaterialInterface> WakeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetWake.M_HexPlanetWake")));

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0.02))
	float WakeInterval = 0.04f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0.1))
	float WakeLife = 2.2f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0.1))
	float WakeSize = 4.0f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=1))
	float WakeSpread = 1.8f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0))
	float WakeDrift = 34.0f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0.05, ClampMax=1))
	float WakeCoreLife = 0.45f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake")
	float WakeStern = 17.0f;

	/** How far above the sea the foam lies, in world units */
	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0))
	float WakeLift = 0.8f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=4, ClampMax=1024))
	int32 WakePool = 192;

	/** How the foam looks: how soft the edge of a patch is (0 to 1 of its radius), the size of the noise that breaks it in world units, how
	 *  see-through it is at most, its color and its brightness */
	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0, ClampMax=0.95))
	float WakeSoftness = 0.6f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0.1))
	float WakeNoiseSize = 5.0f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0, ClampMax=1))
	float WakeOpacity = 0.8f;

	UPROPERTY(EditAnywhere, Category="Boats|Wake")
	FLinearColor WakeColor = FLinearColor(0.85f, 0.93f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Boats|Wake", meta=(ClampMin=0))
	float WakeBrightness = 1.1f;

	/** Returns the index of the artifact of an Id, any case, or INDEX_NONE */
	int32 FindArtifact(const FString& Id) const;

	/** Returns the class of an index, the first for one that is none */
	const FHexHeroClass& GetClass(int32 Index) const { return Classes.IsValidIndex(Index) ? Classes[Index] : Classes[0]; }

	/** Returns the index of the class of a name, any case, or INDEX_NONE */
	int32 FindClass(const FString& Id) const;

	/** Returns the player of an index, the first for one that is none */
	const FHexHeroPlayer& GetPlayer(int32 Index) const { return Players.IsValidIndex(Index) ? Players[Index] : Players[0]; }

	/** Loads DA_HexHeroes, or returns the defaults of the class with a warning when it is missing */
	static const UHexHeroConfig* Load();

	virtual void PostLoad() override;
};
