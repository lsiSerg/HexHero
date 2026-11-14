// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexCastleConfig.h"
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

	/** How it is turned from where it faces by itself, in degrees, set in the editor; the place of a hero at it turns with it */
	int32 Yaw = 0;

	/** The player who owns a mine in the game played, INDEX_NONE for nobody */
	int32 Owner = INDEX_NONE;

	/** The creatures its owner left to guard a mine or a mill, slot by slot, as the developer asked on 2026-11-13: a hero of another player
	 *  fights them before it takes it (UHexBattles), and they show beside it (UHexSites) */
	TArray<FHexStack> Guard;

	/** A pile that was taken in the game played, and is not drawn */
	bool bTaken = false;

	/** The week a mill last gave, 0 for none yet in the game played */
	int32 VisitedWeek = 0;

	/** The players who owned a mine in the game played and lost it to another, a bit each: a player the game moves wants it back */
	uint32 FormerOwners = 0;

	/** When a mine was last taken or a pile taken, in seconds of the world, by which the mine flashes and the pile dissolves */
	double EventTime = -1000.0;

	/** A pile that was taken and is still drawn, dissolving */
	bool bFading = false;

	/** Where it sits on the ground, found once until the ground changes (UHexSites::FindSeat) */
	bool bSeated = false;
	FTransform Seat;
};

/** A line of text that floats up over a site: what a hero gained there */
struct FHexSitePopup
{
	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
	FVector3d Point = FVector3d::ZeroVector;
	FString Text;
	FLinearColor Color = FLinearColor::White;

	/** When it starts, in the seconds of FPlatformTime, which may lie ahead for a line that follows another */
	double StartTime = 0.0;
};

/** What a site is to a player who looks for something to do (UHexSites::GetGoal) */
enum class EHexSiteGoal : uint8
{
	/** Nothing: its own mine, a mill that gave this week */
	None,
	/** A pile to take or a mill to visit */
	Gather,
	/** A mine of nobody or of another player */
	Mine,
	/** A mine it owned and another player took */
	LostMine,
};

/**
 *  The mines and piles of a planet and what the players have gathered, an object of UHexHeroes that serves both layers, as the developer's
 *  document "Ресурси та шахти" of 2026-11-11 describes them (see UHexSiteConfig).
 *
 *  A site stands on a cell of a layer that a hero could stand on, one site to a cell. A mine or a mill fills its cell (FindBlockerAt), so
 *  no hero enters it, and a hero beside it that is sent onto its cell visits it instead (Visit, from UHexHeroes::VisitSite): a mine becomes
 *  that of its player, shown by the color of its flag, and gives what it gives in a day at once, but once a week whoever takes it, and a
 *  mill gives once a week. A pile lies on its cell (FindPileAt), and the hero whose way ends on it takes what it holds
 *  (UHexHeroes::TakePile). What a hero gained floats up over the site as text (GetPopups, drawn by SHexHeroPanel), a mine that is taken
 *  flashes, and a pile that is taken dissolves before it is gone (bFading, PickupFadeTime). Every new day each mine gives its owner its resource (EndDay). The game starts with no mine owned, every pile
 *  there and what the players start with (Restart).
 *
 *  sites.json of the planet holds the sites (HexPlanetFile::SaveSites), which the Mines tab of the palette and hex.Site edit and Generate
 *  there scatters over both layers (HexSiteGenerator); who owns what and what was gathered are not saved yet.
 *
 *  The sites of a layer are instances of the mesh of each kind, a component of the planet of that layer a mesh, placed again when a site
 *  or the ground changed, each seated on the ground under it (FindSeat); the custom data of an instance is the color of its resource and of
 *  its owner (M_HexPlanetSite).
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

	/** What a pile holds in the game played, and the resource of a site, for the panel at the cursor */
	int32 GetPileAmountOf(const FHexSite& Site) const { return GetPileAmount(Site); }
	int32 GetResourceOf(const FHexSite& Site) const { return GetSiteResource(Site); }

	const TArray<FHexSite>& GetSites() const { return Sites; }

	/** Returns the site on a cell of a layer that is still there, or null */
	const FHexSite* FindSiteAt(EHexPlanetLayer Layer, int32 Cell) const;

	/** The site of an Id, to change its guard */
	const FHexSite* FindSite(int32 Id) const;
	FHexSite* FindSiteMutable(int32 Id);

	/** Shows the guard a player left at a mine or a mill: a figure of its strongest stack beside the building, in the color of the owner, hidden
	 *  under the fog of war of the person who plays, as the developer asked on 2026-11-13; every update of the heroes */
	void UpdateGuards(const class UHexHeroes& Heroes);

	/** Returns the mine or mill that fills a cell, which no hero enters, or the pile that lies on it, which the hero whose way ends there takes */
	const FHexSite* FindBlockerAt(EHexPlanetLayer Layer, int32 Cell) const;
	const FHexSite* FindPileAt(EHexPlanetLayer Layer, int32 Cell) const;

	/** Where the hero that took the mine or the mill of a cell stands on that cell, on the ground (HeroPlace of its kind); false for a cell
	 *  without one, or while it is not seated on the ground yet */
	bool GetHeroPoint(EHexPlanetLayer Layer, int32 Cell, FVector3d& OutPoint) const;


	/** Puts a site of a kind on a cell. Returns its Id, or 0 and why not */
	int32 AddSite(EHexPlanetLayer Layer, int32 Cell, int32 Kind, FString& OutProblem);

	/** Takes the site of a cell off the planet. Returns false if there is none */
	bool RemoveSiteAt(EHexPlanetLayer Layer, int32 Cell);

	/** Scatters sites over both layers in place of all there are (HexSiteGenerator, Generation of DA_HexSites): on the ground a hero walks,
	 *  off the entrances, where heroes start and what stands on cells, some near where each player starts. The seed chooses the layout; without
	 *  one each call gives another, the first that of the seed of the planet. Returns how many sites there are now, and says what it did */
	int32 Generate(FString& OutReport, const int32* Seed = nullptr);

	/** Takes every site off the planet. Returns false if there is none */
	bool Clear();

	/** Returns what a site is to a player in a week of the game: something to gather, a mine to take, one to take back, or nothing */
	EHexSiteGoal GetGoal(const FHexSite& Site, int32 Player, int32 Week) const;

	/** A hero of a player visits the site of a cell, in a week of the game from 1: it takes the mine there for the player, with what it
	 *  gives in a day if nobody took it this week, the pile for good, or what the mill gives this week. Returns a line for the player, or
	 *  nothing when there is no site; bShown lets what was gained float up over the site, for what the person who watches sees */
	FString Visit(int32 Player, const FString& HeroName, EHexPlanetLayer Layer, int32 Cell, int32 Week, bool bShown = true);

	/** Whether a player has what something costs, by the Ids of the resources, and takes it from the player; AddAmount gives */
	bool CanPay(int32 Player, const TMap<FName, int32>& Cost) const;
	void Pay(int32 Player, const TMap<FName, int32>& Cost);
	void AddAmount(int32 Player, FName Resource, int32 Amount);

	/** Returns a cost as "2000 gold, 5 wood" */
	FString DescribeCost(const TMap<FName, int32>& Cost) const;

	/** Lets a line float up over a point of a layer, after those already waiting there: what happened at a castle */
	void AddPopupAt(EHexPlanetLayer Layer, const FVector3d& Point, const FString& Text, const FLinearColor& Color);

	/** Returns the lines that float up over the sites now, and those about to */
	const TArray<FHexSitePopup>& GetPopups() const { return Popups; }

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

	/** Says that the ground of a layer changed, so its sites are seated and placed on it again */
	void OnLayerRebuilt(EHexPlanetLayer Layer);

private:

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Surface;

	UPROPERTY(Transient)
	TObjectPtr<const UHexSiteConfig> Config;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	/** The material of the piles that dissolve */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FadeMaterial;

	/** The instances of the sites of each layer, a component for each mesh, and another for those of it that dissolve, by the path of the
	 *  mesh and a mark */
	TMap<FString, TWeakObjectPtr<UInstancedStaticMeshComponent>> Instances[NumPlanetLayers];

	TArray<FHexSitePopup> Popups;

	TArray<FHexSite> Sites;

	/** The figures of the guards, by the Id of their site, with what they were made for */
	struct FGuardBody
	{
		TWeakObjectPtr<USceneComponent> Figure;
		int32 Faction = INDEX_NONE;
		int32 Creature = INDEX_NONE;
		bool bUpgraded = false;
		int32 Cell = INDEX_NONE;
		bool bShown = true;
	};
	TMap<int32, FGuardBody> GuardBodies;

	/** What each player has of each resource, by player and resource */
	TArray<TArray<int32>> Amounts;

	/** The lines sites.json holds, by which a step of Undo back to them leaves nothing to save */
	TArray<FHexSiteLine> SavedLines;

	int32 NextId = 1;
	int32 GameNumber = 0;

	/** How many layouts Generate gave without a seed, so the next is another */
	int32 GenerateCount = 0;
	bool bEdited = false;
	bool bDirty[NumPlanetLayers] = {true, true};
	bool bLayerBuilt[NumPlanetLayers] = {false, false};

	AHexPlanet* GetPlanet(EHexPlanetLayer Layer) const;

	/** Places the instances of a layer again */
	void Rebuild(EHexPlanetLayer Layer);

	/** Returns where a site of a cell sits: on the ground under it, read in the middle of the cell and around it, leaning as that ground
	 *  does up to MaxTilt degrees, turned by Angle around its up, at a scale */
	FTransform FindSeat(const AHexPlanet& Planet, int32 Cell, double Angle, float MaxTilt, float Scale) const;

	/** Lets a line float up over a site that is seated, after those already waiting there */
	void AddPopup(const FHexSite& Site, const FString& Text, const FLinearColor& Color);

	/** Returns the seconds of the world, by which the materials time what happens to a site */
	double GetWorldTime() const;

	/** Returns what a pile holds in the game played, and the resource of a mill */
	int32 GetPileAmount(const FHexSite& Site) const;
	int32 GetSiteResource(const FHexSite& Site) const;

	/** Returns the colors of a player, from DA_HexHeroes */
	FLinearColor GetPlayerColor(int32 Player) const;
	FString GetPlayerName(int32 Player) const;
};
