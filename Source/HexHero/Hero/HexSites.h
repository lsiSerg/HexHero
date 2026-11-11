// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexSites.generated.h"

class AHexPlanet;
class UHexSiteConfig;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;

/** A mine or a pile on a cell of a layer */
struct FHexSite
{
	/** Number of the site on its planet, from 1, which never changes */
	int32 Id = 0;

	/** Index of its kind in DA_HexSites */
	int32 Kind = 0;

	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
	int32 Cell = INDEX_NONE;

	/** The player who owns a mine in the game played, INDEX_NONE for nobody */
	int32 Owner = INDEX_NONE;

	/** A pile that was taken in the game played, and is not drawn */
	bool bTaken = false;

	/** The week a mill last gave, 0 for none yet in the game played */
	int32 VisitedWeek = 0;
};

/**
 *  The mines and piles of a planet and what the players have gathered, an object of UHexHeroes that serves both layers, as the developer's
 *  document "Ресурси та шахти" of 2026-11-11 describes them (see UHexSiteConfig).
 *
 *  A site fills a cell of a layer that a hero could stand on, one site to a cell, and no hero enters that cell. A hero beside it that is
 *  sent onto its cell visits it instead (Visit, from UHexHeroes::VisitSite): a mine becomes that of its player, shown by the color of its
 *  flag, a pile gives what it holds and is gone, and a mill gives once a week. Every new day each mine gives its owner its resource
 *  (EndDay). The game starts with no mine owned, every pile there and what the players start with (Restart).
 *
 *  sites.json of the planet holds the sites (HexPlanetFile::SaveSites), which the Mines tab of the palette and hex.Site edit; who owns what
 *  and what was gathered are not saved yet.
 *
 *  The sites of a layer are instances of the mesh of a mine and of a pile, components of the planet of that layer, placed again when a site
 *  or the ground changed; the custom data of an instance is the color of its resource and of its owner (M_HexPlanetSite).
 */
UCLASS()
class UHexSites : public UObject
{
	GENERATED_BODY()

public:

	/** Reads DA_HexSites and sites.json of the planet, once its surface is built */
	void Init(AHexPlanet* InSurface, int32 NumPlayers);

	/** Places the instances of the layers whose sites or ground changed, every frame */
	void Update();

	const UHexSiteConfig* GetConfig() const { return Config; }

	const TArray<FHexSite>& GetSites() const { return Sites; }

	/** Returns the site on a cell of a layer that is still there, or null */
	const FHexSite* FindSiteAt(EHexPlanetLayer Layer, int32 Cell) const;


	/** Puts a site of a kind on a cell. Returns its Id, or 0 and why not */
	int32 AddSite(EHexPlanetLayer Layer, int32 Cell, int32 Kind, FString& OutProblem);

	/** Takes the site of a cell off the planet. Returns false if there is none */
	bool RemoveSiteAt(EHexPlanetLayer Layer, int32 Cell);

	/** A hero of a player visits the site of a cell, in a week of the game from 1: it takes the mine there for the player, the pile for good,
	 *  or what the mill gives this week. Returns a line for the player, or nothing when there is no site */
	FString Visit(int32 Player, const FString& HeroName, EHexPlanetLayer Layer, int32 Cell, int32 Week);

	/** A new day: every mine gives its owner its resource */
	void EndDay();

	/** A new game: no mine is owned, every pile is there, and the players have what they start with */
	void Restart();

	/** Returns how much of a resource a player has */
	int32 GetAmount(int32 Player, int32 Resource) const;

	/** Returns what a player gets of a resource every day from its mines */
	int32 GetIncome(int32 Player, int32 Resource) const;

	/** Returns a line about the site on a cell for the panel at the cursor, empty without one */
	FString GetCellText(EHexPlanetLayer Layer, int32 Cell) const;

	/** Returns true if the sites that sites.json keeps changed since it was read or written */
	bool IsEdited() const { return bEdited; }

	/** Writes sites.json into the folder of a planet */
	bool Save(const FString& PlanetFolder, FString& OutError);

	/** Returns the lines of sites.json for the sites, and sets the sites from such lines, for Undo of the brush */
	TArray<FHexSiteLine> MakeLines() const;
	void SetLines(const TArray<FHexSiteLine>& Lines);

	/** Says that the ground of a layer changed, so its sites are placed on it again */
	void OnLayerRebuilt(EHexPlanetLayer Layer) { bDirty[int32(Layer)] = true; }

private:

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Surface;

	UPROPERTY(Transient)
	TObjectPtr<const UHexSiteConfig> Config;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	/** The instances of the mines, the piles and the mills of each layer, by EHexSiteType */
	TWeakObjectPtr<UInstancedStaticMeshComponent> Instances[NumPlanetLayers][3];

	TArray<FHexSite> Sites;

	/** What each player has of each resource, by player and resource */
	TArray<TArray<int32>> Amounts;

	/** The lines sites.json holds, by which a step of Undo back to them leaves nothing to save */
	TArray<FHexSiteLine> SavedLines;

	int32 NextId = 1;
	int32 GameNumber = 0;
	bool bEdited = false;
	bool bDirty[NumPlanetLayers] = {true, true};
	bool bLayerBuilt[NumPlanetLayers] = {false, false};

	AHexPlanet* GetPlanet(EHexPlanetLayer Layer) const;

	/** Places the instances of a layer again */
	void Rebuild(EHexPlanetLayer Layer);

	/** Returns what a pile holds in the game played, and the resource of a mill */
	int32 GetPileAmount(const FHexSite& Site) const;
	int32 GetSiteResource(const FHexSite& Site) const;

	/** Returns the colors of a player, from DA_HexHeroes */
	FLinearColor GetPlayerColor(int32 Player) const;
	FString GetPlayerName(int32 Player) const;
};
