// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexHeroPath.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexHeroes.generated.h"

class AHexPlanet;
class UHexHeroConfig;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 *  A hero on the map of the planet: a leader who walks its cells, on the surface or underground. It does not fight itself: its Attack and
 *  Defense are added to those of every unit of its army, its Spell Power is the strength of its spells and its Knowledge its mana, as the
 *  developer described on 2026-11-10; nothing fights, casts or levels up yet, so of these only the movement is played
 */
struct FHexHero
{
	/** Number of the hero on its planet, from 1, which never changes */
	int32 Id = 0;

	FString Name;

	/** Index of its class and of its player in DA_HexHeroes */
	int32 Class = 0;
	int32 Player = 0;

	/** Where it stands when a game starts, which heroes.json keeps */
	EHexPlanetLayer StartLayer = EHexPlanetLayer::Surface;
	int32 StartCell = INDEX_NONE;

	/** Where it stands now; while it walks, the cell it steps onto */
	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
	int32 Cell = INDEX_NONE;

	/** Points of movement it has left today */
	float MovePoints = 0.0f;

	int32 Attack = 0;
	int32 Defense = 0;
	int32 SpellPower = 0;
	int32 Knowledge = 0;
	int32 Mana = 0;
	int32 Level = 1;
	int32 Experience = 0;

	/** The cells it still walks to after the step it takes now, the next first */
	TArray<int32> Route;

	/** True while it walks, the cell it left on the step it takes and how far along that step it is, 0 to 1 */
	bool bWalking = false;
	int32 FromCell = INDEX_NONE;
	float StepAlpha = 0.0f;

	/** It goes through the entrance or exit its way ends on */
	bool bEnterAtEnd = false;

	/** The side of a cell with an entrance or exit it stands on, beside the marker: the direction it came from, zero for the south */
	FVector3d EntranceSide = FVector3d::ZeroVector;

	/** Where it stands in the world and the direction it faces, along the ground; bPlaced once they were found on the built layer */
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d Facing = FVector3d::ZeroVector;
	bool bPlaced = false;

	/** How much larger than its own size it is drawn, as the camera is far from it (FarStart, FarScale of the config) */
	float ViewScale = 1.0f;
};

/** The light of a hero for the planet: where it stands and how far its light reaches on the ground in cells, 0 for HeroLightReach of the layer */
struct FHexHeroLight
{
	FVector3d Point = FVector3d::ZeroVector;
	double ReachCells = 0.0;
};

/** How many heroes light a layer at most, as many as the materials take (HERO_LIGHTS of create_planet_assets.py) */
constexpr int32 MaxHeroLights = 8;

/**
 *  The heroes of a planet and their turns, an object of the surface layer that serves both layers (AHexPlanet::GetHeroes), as the developer
 *  agreed on 2026-11-10: several heroes, a left click chooses one and a right click sends it, and each lights the map around itself.
 *
 *  A hero stands on a cell of a layer, one hero to a cell. Its way to a cell is the cheapest over neighbors (HexHeroPath::FindField, the cost
 *  of a step from GetStepCost, see UHexHeroConfig), found again when the map changes (NoteMapChanged). While a hero is chosen the way to the
 *  cell under the cursor shows as marks on the ground (SetHoverCell): green as far as it gets today, amber beyond, and a red mark on a cell it
 *  cannot reach. Sent there (Send), it walks cell by cell, paying for each step as it starts it, until the way or its points end; a way that
 *  ends on an entrance or exit takes it through to the other layer (OnHeroTravelled, which the camera follows). EndTurn begins the next day
 *  and gives every hero its points back. How players take turns is not decided, so one person moves the heroes of every player.
 *
 *  heroes.json of the planet holds where the heroes stand when a game starts (HexPlanetFile::SaveHeroes), which the Heroes tab of the palette
 *  and hex.Hero edit; where they walked to and the day are not saved yet, so each run starts there, and Restart goes back to it.
 *
 *  Each hero is a mesh component of the planet of its layer, so it is hidden with it; the ring under the hero chosen and the marks of its way
 *  are components of that planet too, drawn with M_HexPlanetHeroMark.
 */
UCLASS()
class UHexHeroes : public UObject
{
	GENERATED_BODY()

public:

	/** Reads DA_HexHeroes and heroes.json of the planet, once its surface is built */
	void Init(AHexPlanet* InSurface);

	/** Walks the heroes and moves their meshes, every frame, whichever layer is shown; ViewPoint is where the camera stands, or null */
	void Update(float DeltaTime, const FVector3d* ViewPoint);

	const UHexHeroConfig* GetConfig() const { return Config; }

	const TArray<FHexHero>& GetHeroes() const { return Heroes; }

	/** Returns a hero by its Id, or null */
	const FHexHero* FindHero(int32 Id) const;

	/** Returns the hero whose name or Id a text gives, or null */
	const FHexHero* FindHeroByText(const FString& Text) const;

	/** Returns the hero that stands on a cell of a layer, or null */
	const FHexHero* FindHeroAt(EHexPlanetLayer Layer, int32 Cell) const;

	/** Returns the hero of a layer whose body a ray passes nearest, within PickRadius of it, or null */
	const FHexHero* FindHeroAlongRay(EHexPlanetLayer Layer, const FVector3d& Origin, const FVector3d& Direction) const;

	/** Returns the day of the game, from 1 */
	int32 GetDay() const { return Day; }

	/** Returns the points of movement a hero has each day */
	float GetMaxMovePoints(const FHexHero& Hero) const;

	/** Returns the hero chosen, or null */
	const FHexHero* GetSelected() const { return FindHero(SelectedId); }
	int32 GetSelectedId() const { return SelectedId; }

	/** Chooses a hero, 0 for none. Returns false if there is no such hero */
	bool Select(int32 Id);

	/** Chooses the hero after the one chosen, in the order of their Ids. Returns it, or null without heroes */
	const FHexHero* SelectNext();

	/** Shows the way of the hero chosen to a cell of a layer as marks on the ground, or none for INDEX_NONE or a cell of another layer */
	void SetHoverCell(EHexPlanetLayer Layer, int32 Cell);

	/** Shows the way to a cell whatever the cursor is over, for hex.Hero hover, which tries the marks without a mouse; INDEX_NONE gives the
	 *  cursor its say back */
	void SetHoverOverride(EHexPlanetLayer Layer, int32 Cell);

	/** Returns a line about the way shown: its cost and how many days it takes, or why there is none; empty without one */
	const FString& GetHoverText() const { return HoverText; }

	/** Returns a line about the hero on a cell for the panel at the cursor, empty without one */
	FString GetCellText(EHexPlanetLayer Layer, int32 Cell) const;

	/** Sends a hero to a cell of its layer along the cheapest way, as far as its points take it today; a way that ends on an entrance or
	 *  exit takes it through, and so does its own cell when it stands on one. Returns false and why it does not go */
	bool Send(int32 Id, int32 Cell, FString& OutMessage);

	/** Begins the next day: every hero gets its points of movement back. Heroes still walking stop where their step ends */
	void EndTurn();

	/** Puts every hero back where it stands when a game starts, on day 1 */
	void Restart();

	/** Returns true if a hero can stand on a cell of a layer, else false and why: land or a passage that no biome, object nor other hero blocks */
	bool CanStand(EHexPlanetLayer Layer, int32 Cell, int32 IgnoreHero, FString* OutProblem = nullptr) const;

	/** Puts a new hero of a class and player on a cell, where it also stands when a game starts. Returns its Id, or 0 and why not */
	int32 AddHero(EHexPlanetLayer Layer, int32 Cell, int32 Class, int32 Player, FString& OutProblem);

	/** Takes a hero off the planet. Returns false if there is none */
	bool RemoveHero(int32 Id);

	/** Returns true if the heroes that heroes.json keeps changed since it was read or written */
	bool IsEdited() const { return bEdited; }

	/** Writes heroes.json into the folder of a planet */
	bool Save(const FString& PlanetFolder, FString& OutError);

	/** Returns the lines of heroes.json for the heroes */
	TArray<FHexHeroLine> MakeLines() const;

	/** Gives the lights of the heroes of a layer, those nearest a point first, at most MaxLights of the config. Returns false if the planet has
	 *  no heroes at all, when the focus of the camera stands in for one as before there were heroes */
	bool GetLights(EHexPlanetLayer Layer, const FVector3d& Focus, TArray<FHexHeroLight>& OutLights) const;

	/** Returns the direction from the center of the planet to the hero chosen while it walks on a layer, for the camera to follow; false
	 *  while it stands or the config says the camera stays */
	bool GetFollowDirection(EHexPlanetLayer Layer, FVector3d& OutDirection) const;

	/** Says that the levels, biomes, objects or rivers of a layer changed, so the ways are found again */
	static void NoteMapChanged() { ++MapRevision; }

	/** Counts up with every change of the list of heroes, by which the panel of the heroes knows to list them again */
	int32 GetListRevision() const { return ListRevision; }

	/** Broadcast when a hero went through an entrance or exit to the other layer */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeroTravelled, int32);
	FOnHeroTravelled OnHeroTravelled;

	/** Broadcast with a line for the player, as why a hero does not go */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessage, const FString&);
	FOnMessage OnMessage;

	/** Returns the cost of a step of a hero from a cell of a layer across a side to the neighbor there, or a negative value if it cannot be
	 *  taken; null for any hero, when no hero blocks */
	float GetStepCost(const AHexPlanet& Planet, const FHexHero* Hero, int32 From, int32 To) const;

private:

	/** The meshes of a layer: the ring under the hero chosen and the marks of the way shown */
	struct FLayerMarks
	{
		TWeakObjectPtr<UStaticMeshComponent> Ring;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Dots;
		TWeakObjectPtr<UInstancedStaticMeshComponent> End;
	};

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Surface;

	UPROPERTY(Transient)
	TObjectPtr<const UHexHeroConfig> Config;

	/** The body of each hero by its Id, a component of the planet of its layer, and its material */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMeshComponent>> Bodies;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PathMaterial;

	TArray<FHexHero> Heroes;
	FLayerMarks Marks[NumPlanetLayers];

	int32 SelectedId = 0;
	int32 Day = 1;
	int32 NextId = 1;
	bool bEdited = false;
	int32 ListRevision = 0;

	/** Whether each layer was built at the last update, as a layer just built places its heroes */
	bool bLayerBuilt[NumPlanetLayers] = {false, false};
	bool bRebuildBound[NumPlanetLayers] = {false, false};

	/** The cheapest ways of the hero chosen from where it stands, and what they were found for */
	HexHeroPath::FField Field;
	int32 FieldHero = 0;
	EHexPlanetLayer FieldLayer = EHexPlanetLayer::Surface;
	int32 FieldRevision = -1;

	/** The cell the way is shown to, and what it was shown for */
	EHexPlanetLayer HoverLayer = EHexPlanetLayer::Surface;
	int32 HoverCell = INDEX_NONE;
	bool bHoverOverride = false;
	int32 ShownHoverCell = INDEX_NONE;
	int32 ShownHoverHero = 0;
	int32 ShownHoverRevision = -1;
	float ShownHoverPoints = -1.0f;
	FString HoverText;

	/** The ground under the middles of cells by layer, kept between the ways shown, dropped when the ground changes */
	TMap<int32, double> GroundRadii[NumPlanetLayers];

	/** Counts the changes of the map of any planet (NoteMapChanged) and of where heroes stand */
	static int32 MapRevision;

	AHexPlanet* GetPlanet(EHexPlanetLayer Layer) const;
	FHexHero* FindHeroMutable(int32 Id);

	/** Returns the factor a cell of a layer puts on a step onto it, or a negative value if no hero enters it */
	float GetCellCost(const AHexPlanet& Planet, int32 Cell) const;

	/** Returns true if a cell of a planet holds an entrance or exit that can be used */
	static bool IsUsableEntrance(const AHexPlanet& Planet, int32 Cell);

	/** Finds the cheapest ways of a hero from where it stands, within PathMaxDays of movement */
	void FindFieldFor(const FHexHero& Hero, HexHeroPath::FField& OutField) const;

	/** Gives the costs of the steps of a way of a hero from where it stands */
	void GetStepCosts(const AHexPlanet& Planet, const FHexHero& Hero, TConstArrayView<int32> Path, TArray<float>& OutCosts) const;

	/** Starts the next step of the way of a hero, paying for it. Returns false when the way or its points end, or the step is blocked now */
	bool StartNextStep(FHexHero& Hero);

	/** Stops a hero where it stands, at the end of its way: through the entrance there if it was sent through */
	void Arrive(FHexHero& Hero);

	/** Takes a hero through the entrance or exit it stands on to the other layer. Returns false and why it stays */
	bool Travel(FHexHero& Hero, FString& OutMessage);

	/** Returns where a hero stands on a cell, as a direction from the center of the planet: its middle, or beside the marker of a cell with
	 *  an entrance or exit that can be used (EntranceStandOff) */
	FVector3d GetStandDirection(const AHexPlanet& Planet, const FHexHero& Hero, int32 Cell) const;

	/** Finds where a hero stands in the world, on the ground of its layer once that is built */
	void PlaceHero(FHexHero& Hero);

	/** Makes or moves the mesh of a hero */
	void UpdateBody(FHexHero& Hero);
	void RemoveBody(int32 Id);

	/** Moves the ring under the hero chosen and hides those of the other layer */
	void UpdateRing();

	/** Shows the marks of the way of the hero chosen to the cell under the cursor again when anything it hangs on changed */
	void UpdateHover();
	void ClearMarks(EHexPlanetLayer Layer);

	/** Returns the ground under the middle of a cell of a layer, as a distance from the center */
	double GetGroundRadius(const AHexPlanet& Planet, int32 Cell);

	/** Gives a hero the attributes its class starts with and the points of a day */
	void ResetHero(FHexHero& Hero) const;

	/** Returns the name a new hero of a class takes */
	FString MakeName(int32 Class, int32 Id) const;

	/** Puts every hero of a layer on its ground again, as it was built or rebuilt */
	void OnLayerRebuilt(EHexPlanetLayer Layer);

	FLayerMarks& GetMarks(EHexPlanetLayer Layer) { return Marks[int32(Layer)]; }
	UMaterialInstanceDynamic* GetPathMaterial();
};
