// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexHeroConfig.generated.h"

class UMaterialInterface;
class UStaticMesh;

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

	/** How far from the middle of a cell with an entrance or exit a hero stands, in radii of a cell, beside the pit or the stairs on the side
	 *  it came from, as the marker fills the middle of the cell */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=0.8))
	float EntranceStandOff = 0.72f;

	/** What going through an entrance or exit to the other layer costs a hero, in points of movement, as the developer set on 2026-11-11, and
	 *  seconds the hero a person plays waits on the other side before it walks on, while the camera comes after it */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0))
	float EntranceCost = 1.0f;

	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0, ClampMax=10))
	float EntrancePause = 1.0f;

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

	/** A castle opens the fog of war for its owner too, this many times as far as a hero, as the developer asked on 2026-11-11 */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=10))
	float CastleSightFactor = 3.0f;

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

	/** Seconds the camera stays where a hero of such a player went into the fog of war, before the game goes on with it or the next, and
	 *  seconds it stays where one is about to come out of the fog, before it does, so the person sees where it went and where it comes from,
	 *  as the developer asked on 2026-11-11; the hero waits meanwhile */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=10))
	float AiFogHoldTime = 0.8f;

	/** Days of a week; nothing happens by the week yet, the panel of the heroes shows it */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=1))
	int32 DaysPerWeek = 7;

	/** Mana a hero has for each point of Knowledge */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	int32 ManaPerKnowledge = 10;

	/** The classes of heroes; MakeDefaultHeroClasses when empty */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<FHexHeroClass> Classes;

	/** The players, by index in heroes.json; MakeDefaultHeroPlayers when empty */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<FHexHeroPlayer> Players;

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

	/** How far from the body of a hero a click still chooses it, in world units, and how tall that body is */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=1))
	float PickRadius = 16.0f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=1))
	float PickHeight = 40.0f;

	/** The ring on the ground under the hero chosen, RingRadius wide, which glows in the color of its player and breathes by RingPulse at
	 *  RingPulseSpeed. It is a band of RingSegments pieces laid on the ground under it, so it follows the relief and no hill cuts it, as the
	 *  developer found a flat ring did on 2026-11-11, drawn RingDepthBias nearer the camera */
	UPROPERTY(EditAnywhere, Category="Look|Marks", meta=(ClampMin=1))
	float RingRadius = 74.0f;

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
