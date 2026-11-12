// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexCastleConfig.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Seven levels of creatures a faction has, and as many slots an army holds */
constexpr int32 NumCreatureLevels = 7;
constexpr int32 NumArmySlots = 7;

/** What a creature can do in a battle besides walking and striking, as bits of FHexCreatureStats::Abilities (HexBattle), by its role, as the
 *  developer chose on 2026-11-12 the roles of the creatures of the two factions after references of the genre; which creature has which is
 *  Claude's */
UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="false"))
enum class EHexAbility : uint8
{
	/** Strikes twice, and shoots twice */
	DoubleStrike,
	/** Strikes back twice a round */
	RetaliateTwice,
	/** What it strikes does not strike back */
	NoRetaliation,
	/** Strikes in melee as hard as it shoots */
	NoMeleePenalty,
	/** Its shot hits the enemies around the stack it shoots too */
	AreaShot,
	/** Heals and raises its own by the damage it deals, never above how many it began the battle with */
	LifeDrain,
	/** Strikes harder the further it ran to strike, ChargeBonusPerHex of DA_HexBattle a hex */
	Charge,
	/** Its strike goes on into the stack behind the one it strikes, friend or foe */
	Breath,
	/** Has no morale */
	Undead,
	/** Its wounded creature heals at the start of its turn */
	Regenerate,
	/** Each of its turns takes ManaDrainPerTurn of DA_HexBattle from the hero of the other side */
	ManaDrain,
};

/** What a creature is in a battle and what one of it costs; the basic and the upgraded version of a creature have one each */
USTRUCT(BlueprintType)
struct FHexCreatureStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Creature")
	FString DisplayName;

	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=0))
	int32 Attack = 1;

	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=0))
	int32 Defense = 1;

	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=0))
	int32 DamageMin = 1;

	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=0))
	int32 DamageMax = 1;

	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=1))
	int32 Health = 1;

	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=1))
	int32 Speed = 4;

	/** Gold one of it costs to hire */
	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=0))
	int32 Gold = 10;

	/** What it can do in a battle besides walking and striking, bits of EHexAbility */
	UPROPERTY(EditAnywhere, Category="Battle", meta=(Bitmask, BitmaskEnum="/Script/HexHero.EHexAbility"))
	int32 Abilities = 0;

	/** Shots a creature that shoots (FHexCreature::bShoots) has in a battle */
	UPROPERTY(EditAnywhere, Category="Battle", meta=(ClampMin=0))
	int32 Shots = 0;

	/** Its own morale and luck, added to those of its army (HexBattle): a chance of another turn, or of losing one, and of double damage */
	UPROPERTY(EditAnywhere, Category="Battle", meta=(ClampMin=-3, ClampMax=3))
	int32 Morale = 0;

	UPROPERTY(EditAnywhere, Category="Battle", meta=(ClampMin=-3, ClampMax=3))
	int32 Luck = 0;

	bool HasAbility(EHexAbility Ability) const { return (Abilities & (1 << int32(Ability))) != 0; }

	/** How much one of it counts when two armies are weighed against each other: by the AI, which chooses what to go for */
	float GetPower() const;
};

/** A creature of a faction, of one of its seven levels: its basic and its upgraded version, how many come to its dwelling every week */
USTRUCT(BlueprintType)
struct FHexCreature
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Creature")
	FName Id;

	UPROPERTY(EditAnywhere, Category="Creature")
	FHexCreatureStats Base;

	UPROPERTY(EditAnywhere, Category="Creature")
	FHexCreatureStats Upgraded;

	/** How many of it its dwelling offers more every week, before what the fort of the castle adds */
	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=0))
	int32 Growth = 1;

	/** A resource one of it costs besides its gold, by its Id in DA_HexSites, none for most */
	UPROPERTY(EditAnywhere, Category="Creature")
	FName CostResource;

	UPROPERTY(EditAnywhere, Category="Creature", meta=(ClampMin=0))
	int32 CostResourceAmount = 0;

	UPROPERTY(EditAnywhere, Category="Creature")
	bool bShoots = false;

	UPROPERTY(EditAnywhere, Category="Creature")
	bool bFlies = false;

	const FHexCreatureStats& GetStats(bool bUpgraded) const { return bUpgraded ? Upgraded : Base; }
};

/** What a building of a castle is to the game */
UENUM()
enum class EHexBuildingType : uint8
{
	/** Gives its owner gold every day */
	Hall,
	/** Makes the garrison stronger and the dwellings grow faster, and is the walls of the castle seen from outside */
	Fort,
	/** Offers creatures of one level to hire, more every week; its second level offers their upgraded version */
	Dwelling,
};

/** A level of a building: what it is called, what it costs to build and what it gives */
USTRUCT(BlueprintType)
struct FHexBuildingLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Building")
	FString DisplayName;

	/** What it costs, by the Ids of the resources in DA_HexSites, gold among them */
	UPROPERTY(EditAnywhere, Category="Building")
	TMap<FName, int32> Cost;

	/** A hall: gold a day */
	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0))
	int32 GoldPerDay = 0;

	/** A fort: how many times more creatures come to the dwellings every week, and how many times stronger the garrison counts */
	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0))
	float GrowthFactor = 1.0f;

	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0))
	float DefenseFactor = 1.0f;

	/** The level of the hall and of the fort the castle needs before this is built */
	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0))
	int32 NeedsHall = 0;

	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0))
	int32 NeedsFort = 0;

	/** Its mesh seen from outside the castle, standing on its origin; none to keep that of the level before */
	UPROPERTY(EditAnywhere, Category="Building")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/**
 *  A building of a castle, with its levels: level 0 is not built, and each level after is built in turn. It stands at its own place in
 *  the castle, so what is built shows from outside, as the developer asked on 2026-11-11
 */
USTRUCT(BlueprintType)
struct FHexBuilding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Building")
	FName Id;

	UPROPERTY(EditAnywhere, Category="Building")
	EHexBuildingType Type = EHexBuildingType::Dwelling;

	/** A dwelling: the level of its creatures, from 0 */
	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0, ClampMax=6))
	int32 Creature = 0;

	UPROPERTY(EditAnywhere, Category="Building")
	TArray<FHexBuildingLevel> Levels;

	/** The level a new castle has of it */
	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0))
	int32 StartLevel = 0;

	/** A dwelling needs the dwelling of this creature level built first, -1 for none */
	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=-1, ClampMax=6))
	int32 NeedsDwelling = -1;

	/** Where it stands in the castle, from the corner the three cells of the castle share, in radii of a cell: X towards the middle of
	 *  the first of them, Y to its left, and how it is turned, in degrees */
	UPROPERTY(EditAnywhere, Category="Building")
	FVector2D Place = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Building")
	float Yaw = 0.0f;

	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0.1))
	float Scale = 1.0f;

	/** How many times higher than its mesh it stands; its masonry stretches with it, so the meshes are made as high as they should be */
	UPROPERTY(EditAnywhere, Category="Building", meta=(ClampMin=0.1))
	float HeightScale = 1.0f;
};

/** A faction: its castle, its creatures and its buildings */
USTRUCT(BlueprintType)
struct FHexFaction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Faction")
	FName Id;

	UPROPERTY(EditAnywhere, Category="Faction")
	FString DisplayName;

	/** Its color in the palette */
	UPROPERTY(EditAnywhere, Category="Faction")
	FLinearColor Color = FLinearColor::White;

	/** The masonry of the stone of its castle: 0 that of men, dressed blocks of warm grey, 1 that of the undead, dark, cracked and seeping */
	UPROPERTY(EditAnywhere, Category="Faction", meta=(ClampMin=0, ClampMax=1))
	float WallStyle = 0.0f;

	/** Its creatures by level, seven of them */
	UPROPERTY(EditAnywhere, Category="Faction")
	TArray<FHexCreature> Creatures;

	UPROPERTY(EditAnywhere, Category="Faction")
	TArray<FHexBuilding> Buildings;

	/** Returns the index of the building of a type, for a dwelling that of a creature level, or INDEX_NONE */
	int32 FindBuilding(EHexBuildingType Type, int32 Creature = 0) const;
};

/** A stack of creatures of one kind in an army or a garrison */
struct FHexStack
{
	int32 Faction = 0;
	int32 Creature = 0;
	bool bUpgraded = false;
	int32 Count = 0;

	bool IsSameKind(const FHexStack& Other) const { return Faction == Other.Faction && Creature == Other.Creature && bUpgraded == Other.bUpgraded; }
};

/** Returns the factions a config has when it holds none: the Order of Dawn and the Realm of Decay, with numbers of Claude's */
TArray<FHexFaction> MakeDefaultFactions();

/**
 *  Every value of the castles of the hex planet (UHexCastles). DA_HexCastles in Content/HexPlanet/Castles, which import_planet_castles.py
 *  creates when it is missing, so tune it by hand; a missing one falls back to the defaults of the class.
 *
 *  As the developer described castles on 2026-11-11: a castle is taken by a hero, has buildings that are built and upgraded, a dwelling
 *  offers its creatures to hire from the start of every week, creatures stack and cost their price to go into the army of a hero or the
 *  garrison of the castle, and what is built shows on the castle from outside. The two factions are those of the developer's document
 *  "Опис замків" under the working names it gives them, the Order of Dawn and the Realm of Decay; every number is Claude's, in the spirit
 *  of the genre, as the developer chose. A castle takes three cells that share a corner, is nearly round and stands on their ground
 */
UCLASS(BlueprintType)
class UHexCastleConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UHexCastleConfig();

	/** MakeDefaultFactions when empty */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<FHexFaction> Factions;

	/** How many buildings a castle builds or upgrades in a day */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=1))
	int32 BuildsPerDay = 1;

	/** What a catapult costs, which the hero that guards a castle or stands beside it buys there, as the developer asked on 2026-11-12; it
	 *  shoots at the walls of the castles that hero attacks (Siege of DA_HexBattle) */
	UPROPERTY(EditAnywhere, Category="Rules")
	TMap<FName, int32> CatapultCost = {{TEXT("Gold"), 1000}, {TEXT("Wood"), 5}};

	/** What hex.Cheat resources gives the person who plays when it names no amount: this much gold and this much of every other resource */
	UPROPERTY(EditAnywhere, Category="Rules|Cheats", meta=(ClampMin=0))
	int32 CheatGold = 20000;

	UPROPERTY(EditAnywhere, Category="Rules|Cheats", meta=(ClampMin=0))
	int32 CheatResources = 50;

	/** A hero starts with the creatures of the first levels of the faction of its player: this many weeks of what their dwellings offer */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<float> HeroStartWeeks = {1.5f, 0.8f};

	/** A castle nobody owns is guarded by this many weeks of what its dwellings offer, and offers a week of it to who takes it */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float NeutralGarrisonWeeks = 2.0f;

	/** Until there are battles the stronger army wins (UHexCastles::Resolve): each point of Attack of the hero that attacks makes its army
	 *  this much stronger, and the winner loses the share of its creatures the weaker army is of it, to this power, at most LossMax */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float HeroAttackBonus = 0.05f;

	/** A hero that entered a castle of its own guards it, as the developer asked on 2026-11-11: its army counts with the garrison, each
	 *  point of its Defense making it this much stronger */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float HeroDefenseBonus = 0.05f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0.1))
	float LossPower = 1.5f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=1))
	float LossMax = 0.9f;

	/** A hero of a player the game moves goes for a castle it can take whose way costs at most this many days of its movement */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0, ClampMax=60))
	float AiCastleReachDays = 5.0f;

	/** A castle is nearly round and has no ground of its own, as the developer asked on 2026-11-11: it stands on the ground of its cells
	 *  and follows it. Its wall is WallSegments stretches of a mesh along a circle of WallRadius radii of a cell around the corner its
	 *  cells share, counted from GateAngle so every gate has the same gap, each stood on the ground where it lies, but for those within
	 *  GateHalfWidth degrees of a gate, with TowerCounts towers evenly around it. The meshes are by the level of the fort, 0 before one
	 *  is built */
	UPROPERTY(EditAnywhere, Category="Look|Wall")
	TArray<TSoftObjectPtr<UStaticMesh>> WallMeshes;

	UPROPERTY(EditAnywhere, Category="Look|Wall")
	TArray<TSoftObjectPtr<UStaticMesh>> TowerMeshes;

	UPROPERTY(EditAnywhere, Category="Look|Wall")
	TArray<TSoftObjectPtr<UStaticMesh>> GateMeshes;

	UPROPERTY(EditAnywhere, Category="Look|Wall")
	TArray<int32> TowerCounts = {0, 0, 6, 9};

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=6, ClampMax=96))
	int32 WallSegments = 36;

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=0.5, ClampMax=2))
	float WallRadius = 1.35f;

	UPROPERTY(EditAnywhere, Category="Look|Wall")
	float GateAngle = 60.0f;

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=0, ClampMax=60))
	float GateHalfWidth = 12.0f;

	/** A castle has this many gates, evenly around it from GateAngle, as the developer asked on 2026-11-11 for ways in, three being enough.
	 *  A hero goes in and out by the one nearest it, which opens for it: its leaves swing into the yard by GateSwing degrees over
	 *  GateRiseTime seconds, stay open for GateHoldTime and shut again, while the hero walks through over GateWalkTime, setting out
	 *  GateWalkDelay of GateRiseTime after the gate began to open */
	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=1, ClampMax=6))
	int32 NumGates = 3;

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=0, ClampMax=170))
	float GateSwing = 85.0f;

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=0, ClampMax=2))
	float GateWalkDelay = 0.6f;

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=0.01, ClampMax=5))
	float GateRiseTime = 0.5f;

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=0, ClampMax=10))
	float GateHoldTime = 2.0f;

	UPROPERTY(EditAnywhere, Category="Look|Wall", meta=(ClampMin=0.1, ClampMax=10))
	float GateWalkTime = 1.4f;

	/** A castle is alive, as the developer asked on 2026-11-11: its flags and pennants wave this far at their tips at this pace, and its
	 *  fires and lit windows glow this bright and flicker by this share, the flames dancing this high, with embers of this color rising
	 *  from them */
	UPROPERTY(EditAnywhere, Category="Look|Life", meta=(ClampMin=0))
	float FlagWave = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Life", meta=(ClampMin=0))
	float FlagWaveSpeed = 3.2f;

	/** The flags and pennants of a castle fly one way, with a wind that blows east over the ground, as the developer asked on 2026-11-11,
	 *  and veer this many degrees to both sides of it */
	UPROPERTY(EditAnywhere, Category="Look|Life", meta=(ClampMin=0, ClampMax=90))
	float FlagVeer = 16.0f;

	UPROPERTY(EditAnywhere, Category="Look|Life", meta=(ClampMin=0))
	float FireGlow = 4.0f;

	UPROPERTY(EditAnywhere, Category="Look|Life", meta=(ClampMin=0))
	float FlameDance = 5.0f;

	UPROPERTY(EditAnywhere, Category="Look|Life", meta=(ClampMin=0, ClampMax=1))
	float FireFlicker = 0.35f;

	UPROPERTY(EditAnywhere, Category="Look|Life")
	FLinearColor EmberColor = FLinearColor(1.0f, 0.5f, 0.12f, 1.0f);

	/** The flag of the owner inside the gate, the mark beside a dwelling that was upgraded, and the scale of every mesh of a castle */
	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> FlagMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Castles/SM_PlanetCastleFlag.SM_PlanetCastleFlag")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> UpgradeMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Castles/SM_PlanetCastleUpgrade.SM_PlanetCastleUpgrade")));

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float CastleScale = 1.0f;

	/** Where the flag stands and where the hero that guards a castle stands in it, in radii of a cell from the corner its cells share, as
	 *  the place of a building: the hero in the middle of the yard, which the buildings leave open and every gate leads straight to */
	UPROPERTY(EditAnywhere, Category="Look")
	FVector2D FlagPlace = FVector2D(-0.6, -0.92);

	UPROPERTY(EditAnywhere, Category="Look")
	FVector2D GuardPlace = FVector2D(0.0, 0.0);

	/** The place of the hero that guards a castle, which the developer asked to mark on 2026-11-11: a paved round in the yard, at
	 *  GuardPlace, on which the hero stands GuardLift above the ground */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float GuardLift = 7.0f;

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> GuardMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Castles/SM_PlanetCastleYard.SM_PlanetCastleYard")));

	/** A castle that is chosen has a ring on the ground around its walls, as a hero has, in the color of its owner: its outer edge this
	 *  many radii of a cell from the corner its cells share, this wide, of this many pieces */
	UPROPERTY(EditAnywhere, Category="Look|Ring", meta=(ClampMin=0.5, ClampMax=4))
	float RingRadius = 1.8f;

	UPROPERTY(EditAnywhere, Category="Look|Ring", meta=(ClampMin=0.01, ClampMax=1))
	float RingWidth = 0.13f;

	UPROPERTY(EditAnywhere, Category="Look|Ring", meta=(ClampMin=8, ClampMax=256))
	int32 RingSegments = 96;

	/** The material of a castle, which draws its stone as the masonry of the style of its faction (WallStyle), this light against the
	 *  texture and with this much of its relief */
	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UMaterialInterface> Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetCastle.M_HexPlanetCastle")));

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float WallTone = 2.4f;

	/** The roofs are tiles in the color of the owner, as the developer asked on 2026-11-11 in place of a plain color: RoofTone times as
	 *  light as the scan of the tiles is, with RoofTextureShare of the colors of the scan itself and RoofRelief of its normal. They are
	 *  clay tiles in channels, as slates in courses looked like brick, and have no moss, which a first try had and the developer did not
	 *  like */
	UPROPERTY(EditAnywhere, Category="Look|Roofs", meta=(ClampMin=0))
	float RoofTone = 7.0f;

	UPROPERTY(EditAnywhere, Category="Look|Roofs", meta=(ClampMin=0, ClampMax=1))
	float RoofTextureShare = 0.0f;

	UPROPERTY(EditAnywhere, Category="Look|Roofs", meta=(ClampMin=0, ClampMax=2))
	float RoofRelief = 1.0f;

	/** Only the roofs of the hall take the color of the owner, with the flags and the pennants, as the developer found too much of that
	 *  color on a castle on 2026-11-11: the other roofs keep the color of their clay, RoofPlainTone times as light as the scan, tinted by
	 *  the style of the faction, that of the undead darker and greyer */
	UPROPERTY(EditAnywhere, Category="Look|Roofs", meta=(ClampMin=0))
	float RoofPlainTone = 2.4f;

	UPROPERTY(EditAnywhere, Category="Look|Roofs")
	FLinearColor RoofHumanTint = FLinearColor(1.0f, 0.92f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Roofs")
	FLinearColor RoofUndeadTint = FLinearColor(0.36f, 0.42f, 0.5f, 1.0f);

	/** What the masonry of each style is tinted by: that of the undead darker and greener than the stone it was scanned from */
	UPROPERTY(EditAnywhere, Category="Look")
	FLinearColor WallHumanTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look")
	FLinearColor WallUndeadTint = FLinearColor(0.42f, 0.55f, 0.45f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0, ClampMax=1))
	float WallRelief = 1.0f;

	/** The developer found the stone of a castle one tone all over, without shade, and its masonry too much of a pattern on 2026-11-11
	 *  ("в кутах темніше, на відкритих ділянках світліше"). So the scan is drawn WallContrast of the way from its own mean, in patches of
	 *  the place WallPatchSize units across that are lighter or darker by WallPatchTone and warmer or cooler (WallWarmColor,
	 *  WallCoolColor), each piece lighter or darker by WallPieceTone */
	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=1.5))
	float WallContrast = 0.45f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=1))
	float WallPatchSize = 45.0f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=1))
	float WallPatchTone = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=1))
	float WallPieceTone = 0.08f;

	UPROPERTY(EditAnywhere, Category="Look|Shade")
	FLinearColor WallWarmColor = FLinearColor(1.08f, 1.0f, 0.9f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Shade")
	FLinearColor WallCoolColor = FLinearColor(0.92f, 0.97f, 1.06f, 1.0f);

	/** Stone is darker by WallFootShade at the ground, up to WallFootHeight units of its mesh above it */
	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=1))
	float WallFootShade = 0.45f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0.1))
	float WallFootHeight = 22.0f;

	/** And lighter by WallTopLight towards its top, WallTopHeight units above the ground, as more of the sky lights it there */
	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=1))
	float WallTopLight = 0.22f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=1))
	float WallTopHeight = 90.0f;

	/** Streaks the rain left down the stone, as the developer asked on 2026-11-11 for dirt gathered over the years: darker by
	 *  WallStreakShade, WallStreakWidth units of the mesh wide and WallStreakLength long */
	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=1))
	float WallStreakShade = 0.35f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0.1))
	float WallStreakWidth = 3.5f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0.1))
	float WallStreakLength = 40.0f;

	/** A stretch of the wall is darker by WallCornerShade where it meets a tower or a gate, as a corner is: from WallCornerInset degrees
	 *  around the middle of a tower, or the edge of a gate, the shade fades over WallCornerReach degrees along the wall; WallCornerSpan
	 *  is the length of a stretch of the wall in the units of its mesh */
	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=1))
	float WallCornerShade = 0.65f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0.1))
	float WallCornerSpan = 30.0f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0, ClampMax=45))
	float WallCornerInset = 4.0f;

	UPROPERTY(EditAnywhere, Category="Look|Shade", meta=(ClampMin=0.1, ClampMax=90))
	float WallCornerReach = 12.0f;

	/** The fires, embers and lit windows of a castle of the undead style burn in this color */
	UPROPERTY(EditAnywhere, Category="Look")
	FLinearColor UndeadGlowColor = FLinearColor(0.2f, 1.0f, 0.7f, 1.0f);

	/** Past this distance from the camera the castles are not drawn */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=100))
	float CullDistance = 20000.0f;

	/** Width of the panel of a castle and how far from the top of the screen it starts */
	UPROPERTY(EditAnywhere, Category="Panel", meta=(ClampMin=200))
	float PanelWidth = 400.0f;

	UPROPERTY(EditAnywhere, Category="Panel", meta=(ClampMin=0))
	float PanelTop = 56.0f;

	/** When the panel opens the camera comes close to the castle, as the developer asked on 2026-11-11: to this distance, with the castle
	 *  this share of the width of the screen from its left, left of the middle, and the middle of the panel at its own share, right of
	 *  the middle, the two having swapped sides as the developer asked the same day */
	UPROPERTY(EditAnywhere, Category="Panel", meta=(ClampMin=100))
	float PanelCameraDistance = 620.0f;

	UPROPERTY(EditAnywhere, Category="Panel", meta=(ClampMin=0, ClampMax=1))
	float CastleScreenX = 0.3f;

	UPROPERTY(EditAnywhere, Category="Panel", meta=(ClampMin=0, ClampMax=1))
	float PanelCenterX = 0.66f;

	/** Returns the index of the faction of an Id, any case, or INDEX_NONE */
	int32 FindFaction(const FString& Id) const;

	/** Returns the stats of the creature a stack holds, or null */
	const FHexCreatureStats* GetStats(const FHexStack& Stack) const;

	/** Loads DA_HexCastles, or returns the defaults of the class with a warning when it is missing */
	static const UHexCastleConfig* Load();

	virtual void PostLoad() override;
};
