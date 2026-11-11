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
	/** Gives AmountMin to AmountMax of its resource once and is gone */
	Pile,
	/** Gives MillDays of the daily income of the mine of a resource of its own, once a week, to whoever visits it */
	Mill,
};

/**
 *  A kind of site, a thing that fills a cell and that a hero visits from a cell beside it, by a move onto its cell, as the developer decided
 *  on 2026-11-11: a mine, a pile or a mill (EHexSiteType)
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

	/** Its mesh, or none for the mesh of its type in the config */
	UPROPERTY(EditAnywhere, Category="Site")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/** Returns the resources and the kinds of sites a config has when it holds none, as the developer's document of 2026-11-11 gives them */
TArray<FHexResourceInfo> MakeDefaultResources();
TArray<FHexSiteKind> MakeDefaultSiteKinds();

/**
 *  Every value of the resources and of the mines and piles of the hex planet (UHexSites). DA_HexSites in Content/HexPlanet/Sites, which
 *  import_planet_sites.py creates when it is missing, so tune it by hand; a missing one falls back to the defaults of the class.
 *
 *  As the developer's document "Ресурси та шахти" and the answers in it of 2026-11-11 say: seven resources, gold, the two basic ones, wood
 *  and ore, and the four rare ones, mercury, sulfur, crystals and gems. Each has one kind of mine, which a hero takes for its player and
 *  which then gives its resource every day: the gold mine 1000, the basic ones 2, the rare ones 1. Any resource may lie as a pile, which
 *  gives 5 to 10 of a basic resource or 3 to 6 of a rare one, once (gold 500 to 1000, which the document does not give). A mill has a
 *  resource of its own and gives MillDays (3) days of the income of its mine once a week. A player starts with 2 days of a gold mine, 4 of
 *  the basic ones and 2 of the rare ones (StartAmount). The developer places the sites in the palette; nothing generates them. Guards wait
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

	/** Meshes of a mine and of a pile, standing on their origin, and their material, which takes the color of the resource and of the owner
	 *  from the custom data of an instance */
	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> MineMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Sites/SM_PlanetMine.SM_PlanetMine")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> PileMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Sites/SM_PlanetPile.SM_PlanetPile")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UStaticMesh> MillMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Sites/SM_PlanetMill.SM_PlanetMill")));

	UPROPERTY(EditAnywhere, Category="Look")
	TSoftObjectPtr<UMaterialInterface> Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetSite.M_HexPlanetSite")));

	/** How many days of the income of the mine of its resource a mill gives, once a week */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	int32 MillDays = 3;

	/** Scale of the meshes */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float MineScale = 1.3f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float PileScale = 1.3f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.1))
	float MillScale = 1.5f;

	/** Color of the flag of a mine nobody owns */
	UPROPERTY(EditAnywhere, Category="Look")
	FLinearColor NeutralColor = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);

	/** How much the heap of a site and the flag of a mine glow in their colors, so they read in the shade and underground */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float SiteGlow = 0.3f;

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
