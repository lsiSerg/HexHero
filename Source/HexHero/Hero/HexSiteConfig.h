// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexSiteConfig.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** A resource of the game: what a player gathers and spends */
USTRUCT(BlueprintType)
struct FHexResourceInfo
{
	GENERATED_BODY()

	/** Name of the resource in the kinds of sites */
	UPROPERTY(EditAnywhere, Category="Resource")
	FName Id;

	UPROPERTY(EditAnywhere, Category="Resource")
	FString DisplayName;

	/** Color of its heaps on the map and of its count on the screen */
	UPROPERTY(EditAnywhere, Category="Resource")
	FLinearColor Color = FLinearColor::White;

	/** How much of it a player has when a game starts */
	UPROPERTY(EditAnywhere, Category="Resource", meta=(ClampMin=0))
	int32 StartAmount = 0;
};

/** What a site is to the game */
UENUM()
enum class EHexSiteType : uint8
{
	/** The player of the hero that visits it owns it, and it gives PerDay of its resource every day */
	Mine,
	/** Gives AmountMin to AmountMax of its resource once, to the hero whose way ends on its cell, and is gone */
	Pile,
	/** Gives MillDays of the daily income of the mine of a resource of its own, once a week, to whoever visits it */
	Mill,
};

/**
 *  A kind of site, as the developer decided on 2026-11-11: a mine or a mill, a building that fills its cell and that a hero visits from a
 *  cell beside it, by a move onto its cell, or a pile, which lies on its cell and is taken by the hero whose way ends there ("ресурс одиночний
 *  - підбирається тільки якщо на нього наступити - не рядом, рядом то тільки для шахт бо вони не прохідні", and not by one that runs over
 *  it on its way further: "тільки як кінцева ціль") (EHexSiteType)
 */
USTRUCT(BlueprintType)
struct FHexSiteKind
{
	GENERATED_BODY()

	/** Name of the kind in sites.json */
	UPROPERTY(EditAnywhere, Category="Site")
	FName Id;

	UPROPERTY(EditAnywhere, Category="Site")
	FString DisplayName;

	UPROPERTY(EditAnywhere, Category="Site")
	EHexSiteType Type = EHexSiteType::Mine;

	/** Id of the resource a mine or a pile gives; a mill has one of its own, any of them, by its site and the game */
	UPROPERTY(EditAnywhere, Category="Site")
	FName Resource;

	/** What a mine gives its owner every day */
	UPROPERTY(EditAnywhere, Category="Site", meta=(ClampMin=0))
	int32 PerDay = 1;

	/** What a pile holds, a number between these by its site and the game */
	UPROPERTY(EditAnywhere, Category="Site", meta=(ClampMin=0))
	int32 AmountMin = 0;

	UPROPERTY(EditAnywhere, Category="Site", meta=(ClampMin=0))
	int32 AmountMax = 0;

	bool IsMine() const { return Type == EHexSiteType::Mine; }

	/** A building fills its cell, so no hero enters it; a pile does not */
	bool FillsCell() const { return Type != EHexSiteType::Pile; }

	/** Its mesh, or none for the mesh of its type in the config; import_planet_sites.py gives a kind without one the mesh named after it,
	 *  SM_Planet<Id>, as each pile has */
	UPROPERTY(EditAnywhere, Category="Site")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/** How many sites of a kind the generation scatters over a planet */
USTRUCT(BlueprintType)
struct FHexSiteShare
{
	GENERATED_BODY()

	/** Id of the kind of site */
	UPROPERTY(EditAnywhere, Category="Generation")
	FName Kind;

	/** Sites of the kind to a thousand cells of the surface a hero can stand on */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=0))
	float PerThousand = 1.0f;

	/** And to a thousand passages of the underground, 0 for a kind that only the surface has */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=0))
	float UndergroundPerThousand = 1.0f;

	/** One of the kind stands near where each player starts, so every player has it within a day or two */
	UPROPERTY(EditAnywhere, Category="Generation")
	bool bNearStart = false;
};

/**
 *  How the sites are scattered over a planet (HexSiteGenerator, Generate of the Mines tab and hex.Site generate), as the developer asked on
 *  2026-11-11; the numbers are Claude's
 */
USTRUCT(BlueprintType)
struct FHexSiteGeneration
{
	GENERATED_BODY()

	/** The kinds the generation places and how many of each; MakeDefaultSiteShares when empty */
	UPROPERTY(EditAnywhere, Category="Generation")
	TArray<FHexSiteShare> Shares;

	/** Factor on the numbers of every kind */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=0, ClampMax=10))
	float Density = 1.0f;

	/** Cells between two sites at least, never under 2, so no two stand side by side */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=2, ClampMax=30))
	float Spacing = 3.0f;

	/** Cells between two mines at least, and between two mines of one kind */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=2, ClampMax=60))
	float MineSpacing = 6.0f;

	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=2, ClampMax=120))
	float SameMineSpacing = 14.0f;

	/** No site stands on ground a hero walks that is smaller than this many cells, such as a ledge between mountains */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=1))
	int32 MinRegionCells = 12;

	/** Rings of cells around an entrance or exit, and around where a hero starts, that stay free */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=0, ClampMax=6))
	int32 EntranceClearRings = 1;

	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=0, ClampMax=6))
	int32 StartClearRings = 2;

	/** The sites near where a player starts stand this many steps of a hero from its first hero */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=1, ClampMax=40))
	int32 StartMinSteps = 3;

	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=1, ClampMax=40))
	int32 StartMaxSteps = 8;

	/** Piles of any kind near where each player starts, besides the mines there */
	UPROPERTY(EditAnywhere, Category="Generation", meta=(ClampMin=0, ClampMax=20))
	int32 StartPiles = 3;

	/** A cell that holds a cover, a decor or an object placed by hand takes no site, so none stands in a meadow in flower or on bones */
	UPROPERTY(EditAnywhere, Category="Generation")
	bool bAvoidObjects = true;
};

/** Returns the resources and the kinds of sites a config has when it holds none, as the developer's document of 2026-11-11 gives them */
TArray<FHexResourceInfo> MakeDefaultResources();
TArray<FHexSiteKind> MakeDefaultSiteKinds();
TArray<FHexSiteShare> MakeDefaultSiteShares();

/**
 *  Every value of the resources and of the mines and piles of the hex planet (UHexSites). DA_HexSites in Content/HexPlanet/Sites, which
 *  import_planet_sites.py creates when it is missing, so tune it by hand; a missing one falls back to the defaults of the class.
 *
 *  As the developer's document "Ресурси та шахти" and the answers in it of 2026-11-11 say: seven resources, gold, the two basic ones, wood
 *  and ore, and the four rare ones, mercury, sulfur, crystals and gems. Each has one kind of mine, which a hero takes for its player and
 *  which then gives its resource every day: the gold mine 1000, the basic ones 2, the rare ones 1. Any resource may lie as a pile, which
 *  gives 5 to 10 of a basic resource or 3 to 6 of a rare one, once (gold 500 to 1000, which the document does not give). A mill has a
 *  resource of its own and gives MillDays (3) days of the income of its mine once a week. A player starts with 2 days of a gold mine, 4 of
 *  the basic ones and 2 of the rare ones (StartAmount). The developer places the sites in the palette, and Generate there scatters them over
 *  both layers (Generation, HexSiteGenerator). Guards wait
 *  for battles, the town hall and the market for castles; chests are not built, and abandoned mines and looted wagons give nothing
 */
UCLASS(BlueprintType)
class UHexSiteConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UHexSiteConfig();

	/** The resources, in the order the screen lists them; MakeDefaultResources when empty */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<FHexResourceInfo> Resources;

	/** The kinds of sites, in the order the palette lists them; MakeDefaultSiteKinds when empty */
	UPROPERTY(EditAnywhere, Category="Rules")
	TArray<FHexSiteKind> Kinds;

	/** Meshes of a mine, of a pile whose kind names none and of a mill, standing on their origin, and their material, which takes the color
	 *  of the resource and of the owner from the custom data of an instance */
	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> MineMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Sites/SM_PlanetMine.SM_PlanetMine")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> PileMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Sites/SM_PlanetPile.SM_PlanetPile")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> MillMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Sites/SM_PlanetMill.SM_PlanetMill")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UMaterialInterface> Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetSite.M_HexPlanetSite")));

	/** The material of a pile that was taken while it dissolves: the same graph, masked */
	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UMaterialInterface> FadeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetSiteFade.M_HexPlanetSiteFade")));

	/** A pile that is taken does not vanish at once but dissolves, as the developer asked on 2026-11-11: over this many seconds it goes in
	 *  grains this large, those about to go glowing this bright, while it lifts this far and its sparkles burst */
	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0, ClampMax=10))
	float PickupFadeTime = 0.9f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0.1))
	float PickupGrain = 3.0f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0))
	float PickupGlow = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0))
	float PickupLift = 10.0f;

	/** A mine that is taken flashes: its flag and its heap glow this much more at first, fading over this many seconds */
	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0))
	float CaptureFlash = 4.0f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0.01, ClampMax=10))
	float CaptureFlashTime = 1.5f;

	/** What a hero gained floats up over the site as text (SHexHeroPanel): for this many seconds, rising this many pixels, of this size,
	 *  starting this far over the foot of the site, each line of several this many seconds after the one before, in the color of its
	 *  resource this far towards white */
	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0.1, ClampMax=10))
	float PopupTime = 2.2f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0))
	float PopupRise = 70.0f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=6, ClampMax=60))
	int32 PopupSize = 17;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0))
	float PopupHeight = 45.0f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0, ClampMax=3))
	float PopupStagger = 0.45f;

	UPROPERTY(EditAnywhere, Category="Look|Taken", meta=(ClampMin=0, ClampMax=1))
	float PopupWhite = 0.45f;

	/** How Generate of the Mines tab scatters the sites over the planet */
	UPROPERTY(EditAnywhere, Category="Generation")
	FHexSiteGeneration Generation;

	/** How many days of the income of the mine of its resource a mill gives, once a week */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	int32 MillDays = 3;

	/** Scale of the meshes */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float MineScale = 1.3f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float PileScale = 1.6f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float MillScale = 1.5f;

	/** Color of the flag of a mine nobody owns */
	UPROPERTY(EditAnywhere, Category="Look")
	FLinearColor NeutralColor = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);

	/** How much the heap of a site and the flag of a mine glow in their colors, so they read in the shade and underground, and how much what
	 *  a pile holds glows in its own */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float SiteGlow = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float PileGlow = 0.22f;

	/** Sparkles twinkle and rise around a pile, as the developer asked on 2026-11-11 for an effect that tells a resource from the decor: stars
	 *  that face the camera, this large at their brightest before the scale of the pile, rising this far while they shine, this many times a
	 *  second each, at its own time */
	UPROPERTY(EditAnywhere, Category="Look|Sparkles", meta=(ClampMin=0))
	float SparkleSize = 5.0f;

	UPROPERTY(EditAnywhere, Category="Look|Sparkles", meta=(ClampMin=0))
	float SparkleRise = 14.0f;

	UPROPERTY(EditAnywhere, Category="Look|Sparkles", meta=(ClampMin=0))
	float SparkleSpeed = 0.45f;

	/** How bright a sparkle is, and how far its color goes from that of the resource to white */
	UPROPERTY(EditAnywhere, Category="Look|Sparkles", meta=(ClampMin=0))
	float SparkleBrightness = 10.0f;

	UPROPERTY(EditAnywhere, Category="Look|Sparkles", meta=(ClampMin=0, ClampMax=1))
	float SparkleWhite = 0.5f;

	/** From this distance of the camera a sparkle grows, up to this many times, so piles show from afar */
	UPROPERTY(EditAnywhere, Category="Look|Sparkles", meta=(ClampMin=1))
	float SparkleFarStart = 1500.0f;

	UPROPERTY(EditAnywhere, Category="Look|Sparkles", meta=(ClampMin=1))
	float SparkleFarScale = 3.0f;

	/** A site sits on the ground under it, not on that of the middle of its cell alone, as the developer found on 2026-11-11 that one on a
	 *  hill hung in the air: the ground is read in the middle and at six points around it, this share of the way to the sides of the cell
	 *  out, the site leans as that ground does, a pile up to PileMaxTilt degrees and a building up to BuildingMaxTilt, and stands between
	 *  the lowest of that ground (SeatLowShare 0) and its mean (1), SiteSink deeper; its mesh goes on under its foot */
	UPROPERTY(EditAnywhere, Category="Look|Seat", meta=(ClampMin=0.05, ClampMax=0.9))
	float SeatReach = 0.42f;

	UPROPERTY(EditAnywhere, Category="Look|Seat", meta=(ClampMin=0, ClampMax=60))
	float PileMaxTilt = 28.0f;

	UPROPERTY(EditAnywhere, Category="Look|Seat", meta=(ClampMin=0, ClampMax=60))
	float BuildingMaxTilt = 10.0f;

	UPROPERTY(EditAnywhere, Category="Look|Seat", meta=(ClampMin=0, ClampMax=1))
	float SeatLowShare = 0.35f;

	UPROPERTY(EditAnywhere, Category="Look|Seat", meta=(ClampMin=0))
	float SiteSink = 1.0f;

	/** Past this distance from the camera the sites are not drawn */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=100))
	float CullDistance = 12000.0f;

	/** Returns the index of the resource of an Id, or INDEX_NONE */
	int32 FindResource(FName Id) const;

	/** Returns the index of the kind of a name, any case, or INDEX_NONE */
	int32 FindKind(const FString& Id) const;

	/** Returns what the mine of a resource gives a day, 0 without one */
	int32 GetMineIncome(int32 Resource) const;

	/** Loads DA_HexSites, or returns the defaults of the class with a warning when it is missing */
	static const UHexSiteConfig* Load();

	virtual void PostLoad() override;
};
