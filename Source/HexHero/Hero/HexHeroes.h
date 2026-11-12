// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexCastleConfig.h"
#include "HexHeroPath.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexHeroes.generated.h"

class AHexPlanet;
class UHexHeroConfig;
class UHexCastles;
class UHexSites;
class UHexShroud;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UMeshComponent;
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

	/** How fast it walks now, in cells a second: it sets out and stops smoothly (WalkStartTime, WalkStopTime) */
	float Speed = 0.0f;

	/** The creatures it leads, up to NumArmySlots stacks of them (UHexCastles): hired in castles, and until there are battles what decides
	 *  whether it takes a castle that is guarded */
	TArray<FHexStack> Army;

	/** The castle it is in and guards, 0 for none: it stands in the yard of that castle, on the cell of it that it went in by */
	int32 CastleId = 0;

	/** Its walk through a gate of a castle: when it began, in seconds of the world, and the points it goes by, from where it stood through
	 *  the gate to where it stands after (UHexCastles::Visit); and for the step out of a castle the cell of it that it leaves, with the
	 *  yard and the gate in the first two points */
	double GlideStart = -1000.0;
	float GlideTime = 1.0f;
	bool bGliding = false;

	/** Its size while it glides, from the first to the second, and now: smaller in the mouth of an entrance it goes through */
	float GlideScaleFrom = 1.0f;
	float GlideScaleTo = 1.0f;
	float PassScale = 1.0f;

	/** It is walking into the mouth of the entrance it goes through next */
	bool bEnteringMouth = false;

	/** It dissolves while it glides: 1 into the mouth of an entrance, -1 out of one, 0 not; how much of it is gone now, and the color of the
	 *  network it goes through, in which it glows as it goes */
	int8 GlideFade = 0;
	float PassFade = 0.0f;
	FLinearColor PassColor = FLinearColor::White;

	/** The cells of its layer it left since it set out, which stay on the way shown until it stops */
	TArray<int32> Trail;

	/** The cell of its layer whose entrance or exit it came out of since it set out, or INDEX_NONE: the hoop in that mouth and the line from
	 *  it stay on the way shown until it stops, as the developer found on 2026-11-12 that they went as soon as it had come through */
	int32 CameOutCell = INDEX_NONE;
	FVector3d GlidePoints[3] = {FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector};
	int32 GateStepFrom = INDEX_NONE;

	/** For a hero of a player the game moves: seconds until it looks for where to go again, and whether it found nothing more to do today */
	float AiWait = 0.0f;
	bool bAiDone = false;

	/** The site a hero of such a player goes for, so another hero of its player takes another, 0 for none */
	int32 AiSiteId = 0;

	/** Whether the person who watches saw it when the game last looked, on the turn of its player: where that changes the camera stays a
	 *  moment (UHexHeroes::NoteSeen) */
	bool bWasSeen = true;

	/** It came through an entrance or exit since the game last looked whether the person sees it (NoteSeen) */
	bool bCameThrough = false;

	/** The person left it for today (UHexHeroes::Skip): the choice of the next hero passes over it until the next day or its next order */
	bool bSkipped = false;

	/** The first cell of its way that lay under the fog of war of its player when the way was found or last checked, at which the way is
	 *  checked again (UHexHeroes::CheckRoute), or INDEX_NONE for a way over ground it knows */
	int32 CheckCell = INDEX_NONE;
	EHexPlanetLayer CheckLayer = EHexPlanetLayer::Surface;

	/** Seconds it waits before its next step, after it went through an entrance or exit, while the camera comes after it */
	float WalkWait = 0.0f;

	/** It goes through the entrance or exit its way ends on */
	bool bEnterAtEnd = false;

	/** The cell the player pointed it to, of either layer, which it walks to when told and keeps until it is there, or INDEX_NONE; and
	 *  whether its last step failed for want of points. Its way there, Route, is the cells it enters in order, and a cell that comes again
	 *  in it is a step through the entrance or exit there to the other layer */
	int32 TargetCell = INDEX_NONE;
	EHexPlanetLayer TargetLayer = EHexPlanetLayer::Surface;
	bool bOutOfPoints = false;

	/** The cell of the site it visits when its way ends beside it, as a site fills its cell, or INDEX_NONE */
	int32 VisitCell = INDEX_NONE;

	/** The side of a cell with an entrance or exit it stands on, beside the marker: the direction it came from, zero for the south */


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

	/** Returns the mines and piles of the planet and what the players gathered */
	UHexSites* GetSites() const { return Sites; }

	/** The castles of the planet, on both layers */
	UHexCastles* GetCastles() const { return Castles; }

	/** What each player explored (the fog of war) */
	UHexShroud* GetShroud() const { return Shroud; }

	/** Whether the fog of war hides what is not explored: while the game is played, with bShroud of the config, unless hex.PlanetShroud is 0 */
	bool IsShroudOn() const;

	/** The player whose fog of war shows while the game is played: the person whose turn it is, or was last */
	int32 GetShroudPlayer() const;

	/** The players move in turns, one after the other, as the developer decided on 2026-11-11: the player whose turn it is, whether a person
	 *  moves it, and whether a person may choose a hero now, which it may only of its own on its turn */
	int32 GetTurnPlayer() const { return TurnPlayer; }
	bool IsPersonTurn() const { return bPlaying && !IsPlayerAi(TurnPlayer); }
	bool CanChoose(int32 Id) const;

	/** Whether the person who watches sees a hero: on the layer shown, on ground the fog of war does not hide, or on a step to or from it */
	bool IsSeen(const FHexHero& Hero) const;

	/** Stops a hero that walks, at the end of the step it is on, as a click, the key that sends it or the right button do. Returns false if
	 *  it did not walk */
	bool Stop(int32 Id);

	/** Leaves a hero of the person who plays for today, as the developer asked on 2026-11-11: it stops, the next hero that can move is
	 *  chosen, and the choice passes over it until the next day or until the person sends it again */
	bool Skip(int32 Id);

	/** Gives a hero points of movement, or takes them with a negative amount, for trying the game (hex.Hero points) */
	bool AddMovePoints(int32 Id, float Amount);

	/** Whether the game moves the heroes of a player by itself, as the Heroes tab of the palette sets and heroes.json keeps: such a hero gathers
	 *  what its player has seen, takes mines and walks to open the map (MoveAi) */
	bool IsPlayerAi(int32 Player) const { return AiPlayers.Contains(Player); }
	void SetPlayerAi(int32 Player, bool bAi);

	/** Whether the fog of war hides a cell from a player */
	bool IsHidden(int32 Player, EHexPlanetLayer Layer, int32 Cell) const;

	const TArray<FHexHero>& GetHeroes() const { return Heroes; }

	/** Returns the planet of a layer, or null while it is not there */
	const AHexPlanet* GetLayerPlanet(EHexPlanetLayer Layer) const { return GetPlanet(Layer); }

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

	/** Returns the week of the game, from 1 */
	int32 GetWeek() const;

	/** Returns true if a hero could stand on a cell of a layer were no hero and no site there: the ground alone and what blocks on it */
	bool IsOpenGround(EHexPlanetLayer Layer, int32 Cell) const;

	/** Returns the points of movement a hero has each day */
	float GetMaxMovePoints(const FHexHero& Hero) const;

	/** Returns the hero chosen, or null */
	/** Returns the hero chosen, null while a castle is chosen in its place: either a hero or a castle is chosen, as the developer decided on
	 *  2026-11-11 */
	const FHexHero* GetSelected() const
	{
		// with a castle chosen the hero that guards it is chosen with it, and no other
		const FHexHero* Hero = FindHero(SelectedId);
		return SelectedCastleId != 0 && (!Hero || Hero->CastleId != SelectedCastleId) ? nullptr : Hero;
	}
	int32 GetSelectedId() const { const FHexHero* Hero = GetSelected(); return Hero ? Hero->Id : 0; }

	/** Chooses a castle of the person whose turn it is in place of a hero, 0 for the hero that was chosen before; and the castle chosen */
	bool SelectCastle(int32 CastleId);
	int32 GetSelectedCastleId() const { return SelectedCastleId; }

	/** Chooses a hero, 0 for none. Returns false if there is no such hero */
	bool Select(int32 Id);

	/** Chooses the hero after the one chosen, in the order of their Ids. Returns it, or null without heroes */
	const FHexHero* SelectNext();

	/** Returns true while the game is played: only then heroes are chosen and sent, and one is always chosen */
	bool IsPlaying() const { return bPlaying; }

	/** Starts the game where the heroes stand and chooses one. Returns false and why not when the planet has no hero */
	bool StartPlay(FString& OutProblem);

	/** Stops the game: no hero is chosen, and no way shows */
	void StopPlay();

	/** What a click of the player did (Click) */
	enum class EClick : uint8
	{
		None,
		/** Another hero is chosen */
		Chosen,
		/** The hero chosen has a new cell to go to, and its way shows */
		Targeted,
		/** It set out to the cell it was pointed to before */
		Sent,
		/** It stops at the end of the step it is on */
		Stopped,
	};

	/** A left click of the player on a cell of the layer shown, with the hero whose body is under the cursor, if any: a click on another hero
	 *  chooses it; on a cell it points the hero chosen there, so its way shows; a second click on that cell sets it walking; and while it
	 *  walks any click stops it. OutMessage says why nothing came of it */
	EClick Click(EHexPlanetLayer Layer, int32 Cell, const FHexHero* Under, FString& OutMessage);

	/** Points a hero to a cell of a layer, INDEX_NONE for nowhere; its way shows while it is the one chosen */
	bool SetTarget(int32 Id, EHexPlanetLayer Layer, int32 Cell);

	/** Sets a hero walking to the cell it was pointed to. Returns false and why it does not go. One pointed nowhere that stands on an entrance
	 *  or exit that can be used goes through it, as the developer asked on 2026-11-12, so the key takes it to and fro between the layers */
	bool Go(int32 Id, FString& OutMessage);

	/** Returns a line about the way shown: its cost and how many days it takes, or why there is none; empty without one */
	const FString& GetHoverText() const { return HoverText; }

	/** A number to draw at a point of the way shown: the day the hero ends there, or at its last cell the days the whole way takes */
	struct FPathLabel
	{
		FVector3d Point = FVector3d::ZeroVector;
		int32 Days = 0;
		bool bEnd = false;
		/** Drawn in place of the days when set: why the hero has no way to the cell marked */
		FString Text;
	};

	/** Returns the numbers of the way shown and the layer it lies on */
	const TArray<FPathLabel>& GetPathLabels() const { return PathLabels; }
	EHexPlanetLayer GetPathLayer() const { return PathLayer; }

	/** Sets the heroes from lines of heroes.json, for Undo of the brush: a hero whose line is the same stays as it is, another is made
	 *  anew where it starts, and one without a line goes */
	void SetLines(const TArray<FHexHeroLine>& Lines);

	/** Returns a line about the hero on a cell for the panel at the cursor, empty without one */
	FString GetCellText(EHexPlanetLayer Layer, int32 Cell) const;

	/** Logs, for each cell up to Rings around a cell of the layer of the hero chosen, what its way there costs or why it has none, for finding out
	 *  why a click shows no way (hex.Hero ways) */
	void LogWays(int32 Cell, int32 Rings) const;

	/** Sends a hero to a cell of a layer along the cheapest way, as far as its points take it today. The way of a hero a person moves may go
	 *  through the entrances and exits its player has seen, down, along the passages and up again (EntranceCost each), as the developer asked
	 *  on 2026-11-11. A way that ends with a step onto an entrance or exit takes it through, and so does its own cell when it stands on one.
	 *  Returns false and why it does not go. Without a layer the cell is of the layer the hero is on */
	bool Send(int32 Id, EHexPlanetLayer Layer, int32 Cell, FString& OutMessage);
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

	/** Returns true if the heroes that heroes.json keeps, or the sites of sites.json, changed since they were read or written */
	bool IsEdited() const;

	/** Writes heroes.json and sites.json into the folder of a planet */
	bool Save(const FString& PlanetFolder, FString& OutError);

	/** Returns the lines of heroes.json for the heroes */
	TArray<FHexHeroLine> MakeLines() const;

	/** Gives the lights of the heroes of a layer, those nearest a point first, at most MaxLights of the config. Returns false if the planet has
	 *  no heroes at all, when the focus of the camera stands in for one as before there were heroes */
	bool GetLights(EHexPlanetLayer Layer, const FVector3d& Focus, TArray<FHexHeroLight>& OutLights) const;

	/** Returns the direction from the center of the planet to the hero chosen while it walks on a layer, for the camera to follow; false
	 *  while it stands or the config says the camera stays */
	bool GetFollowDirection(EHexPlanetLayer Layer, FVector3d& OutDirection) const;

	/** Says whether the camera is on a trip between the layers: the hero a person sent waits meanwhile, as the screen is black, so the person
	 *  sees it set out and the camera is free to go after it through an entrance */
	void SetViewTravelling(bool bTravelling) { bViewTravelling = bTravelling; }

	/** Says that the levels, biomes, objects or rivers of a layer changed, so the ways are found again */
	static void NoteMapChanged() { ++MapRevision; }

	/** Counts up with every change of the list of heroes, by which the panel of the heroes knows to list them again */
	int32 GetListRevision() const { return ListRevision; }

	/** Broadcast when a hero went through an entrance or exit to the other layer */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeroTravelled, int32);
	FOnHeroTravelled OnHeroTravelled;

	/** Broadcast when the game itself chose another hero, as the one chosen ran out of steps or left the planet, for the camera to show it */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeroChosen, int32);
	FOnHeroChosen OnHeroChosen;

	/** The hero chosen set out on the turn of a person, by a click, a key or the console, or a hero of the player the game moves that the
	 *  person would see is about to move (WaitForView): the camera goes to it where it is on the layer that is not shown */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeroSent, int32);
	FOnHeroSent OnHeroSent;

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
		TWeakObjectPtr<UProceduralMeshComponent> Ring;
		/** Where the ring was laid, at what size, of how many pieces and on which ground, so it is laid again only when one changed */
		FVector3d RingAt = FVector3d::ZeroVector;
		double RingOuter = -1.0;
		int32 RingSegments = 0;
		int32 RingGround = -1;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Dots;
		TWeakObjectPtr<UInstancedStaticMeshComponent> End;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Lines;
		/** The hoops in the mouths of the entrances a way goes through */
		TWeakObjectPtr<UInstancedStaticMeshComponent> Portals;
		/** The plain hoops that run from them along the way */
		TWeakObjectPtr<UInstancedStaticMeshComponent> Rings;
	};

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Surface;

	UPROPERTY(Transient)
	TObjectPtr<const UHexHeroConfig> Config;

	UPROPERTY(Transient)
	TObjectPtr<UHexSites> Sites;

	UPROPERTY(Transient)
	TObjectPtr<UHexCastles> Castles;

	/** The castles hire into the armies of the heroes and take from them what an attack costs */
	friend class UHexCastles;

	UPROPERTY()
	TObjectPtr<UHexShroud> Shroud;

	/** The body of each hero by its Id, a component of the planet of its layer, and its material */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMeshComponent>> Bodies;

	/** What the animated body of each hero last showed: whether it moved and whether it was the hero chosen */
	struct FBodyState
	{
		bool bMoving = false;
		bool bChosen = false;
		/** How much of it its material draws as gone */
		float Fade = 0.0f;
	};
	TMap<int32, FBodyState> BodyStates;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PathMaterial;

	/** The material of the lines of a way, which grow from afar in their width alone */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LineMaterial;

	TArray<FHexHero> Heroes;
	FLayerMarks Marks[NumPlanetLayers];

	int32 SelectedId = 0;

	/** The castle chosen in place of a hero, 0 for none */
	int32 SelectedCastleId = 0;
	int32 Day = 1;
	int32 NextId = 1;
	bool bEdited = false;

	/** Whose turn it is, whose fog of war shows, and the hero of a player the game moves that moves now, 0 for none yet */
	int32 TurnPlayer = 0;
	int32 ViewPlayer = 0;
	int32 AiHeroId = 0;

	/** Seconds the game waits before a player it moves begins, while no person plays at all, so the days do not race */
	float AiTurnWait = 0.0f;

	/** While above 0 the camera stays where a hero of the player the game moves went into the fog of war or is about to come out of it
	 *  (AiFogHoldTime), on that layer, and that hero waits */
	float WatchHold = 0.0f;
	int32 WatchHeroId = 0;
	EHexPlanetLayer WatchLayer = EHexPlanetLayer::Surface;
	FVector3d WatchDirection = FVector3d::ZeroVector;

	/** The players the game moves by itself, sorted, and those heroes.json holds */
	TArray<int32> AiPlayers;
	TArray<int32> SavedAiPlayers;

	/** Counts the changes of the ground of any layer, which the ring lies on */
	int32 GroundRevision = 0;
	bool bPlaying = false;

	/** The lines heroes.json holds, by which a step of Undo back to them leaves nothing to save */
	TArray<FHexHeroLine> SavedLines;
	int32 ListRevision = 0;

	/** Whether each layer was built at the last update, as a layer just built places its heroes */
	bool bLayerBuilt[NumPlanetLayers] = {false, false};
	bool bRebuildBound[NumPlanetLayers] = {false, false};

	/** The cheapest ways of the hero chosen from where it stands, and what they were found for */
	HexHeroPath::FField Field;
	int32 FieldHero = 0;
	EHexPlanetLayer FieldLayer = EHexPlanetLayer::Surface;
	int32 FieldRevision = -1;

	/** The camera is on a trip between the layers (SetViewTravelling) */
	bool bViewTravelling = false;

	/** The hero that waits for the camera to come to its layer, that layer, since when, and whether the camera was asked (WaitForView) */
	int32 ViewWaitHero = 0;
	EHexPlanetLayer ViewWaitLayer = EHexPlanetLayer::Surface;
	double ViewWaitStart = 0.0;
	bool bViewAsked = false;

	/** The cells of a layer not built yet whose objects block movement, read once from its file (IsBlockedUnbuilt) */
	mutable TBitArray<> UnbuiltBlockers[NumPlanetLayers];

	/** What the way shown was shown for, its line of text and its numbers */
	int32 ShownTarget = INDEX_NONE;
	EHexPlanetLayer ShownTargetLayer = EHexPlanetLayer::Surface;
	EHexPlanetLayer ShownLayer = EHexPlanetLayer::Surface;
	int32 ShownHero = 0;
	int32 ShownRevision = -1;
	float ShownPoints = -1.0f;
	int32 ShownRoute = -1;
	FVector3d ShownStart = FVector3d::ZeroVector;
	FString HoverText;
	TArray<FPathLabel> PathLabels;
	EHexPlanetLayer PathLayer = EHexPlanetLayer::Surface;

	/** The ground under the middles of cells by layer, kept between the ways shown, dropped when the ground changes */
	TMap<int32, double> GroundRadii[NumPlanetLayers];

	/** The triangles of the ground of the cells a way was laid over, by layer, so its line follows the relief; dropped when the ground changes
	 *  and when it holds many cells */
	TMap<int32, TArray<FVector3f>> GroundTriangles[NumPlanetLayers];

	/** The hero that stopped with no steps left and the seconds until the next one is chosen in its place (NextHeroDelay), 0 for none */
	int32 PendingNextFrom = 0;
	float PendingNextTime = 0.0f;

	/** Counts the changes of the map of any planet (NoteMapChanged) and of where heroes stand */
	static int32 MapRevision;

	AHexPlanet* GetPlanet(EHexPlanetLayer Layer) const;
	FHexHero* FindHeroMutable(int32 Id);

	/** Returns the factor a cell of a layer puts on a step onto it, or a negative value if no hero enters it; bIgnoreSite for what a visit of
	 *  the site that fills it costs */
	float GetCellCost(const AHexPlanet& Planet, int32 Cell, bool bIgnoreSite = false) const;

	/** Gives the way of a hero to a cell from the ways found for it: the cells it steps onto and, when a site fills the cell, whether the way
	 *  ends beside it with a visit, by the cheapest cell beside it. Returns false if no way leads there */
	bool FindWay(const FHexHero& Hero, const HexHeroPath::FField& Ways, EHexPlanetLayer TargetLayer, int32 Target, TArray<int32>& OutPath, bool& bOutVisit) const;

	/** Returns the node of a cell of a layer in the ways found over both layers (HexHeroPath::FindLayerField) */
	int32 GetNode(EHexPlanetLayer Layer, int32 Cell) const;

	/** Walks a way of a hero from where it stands: for each step the layer it ends on, the cell it leaves, the cell it enters and whether it
	 *  goes through an entrance or exit to the other layer, which a cell that comes again in the way means. Visit returns false to stop */
	void WalkWay(const FHexHero& Hero, TConstArrayView<int32> Path, TFunctionRef<bool(EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)> Visit) const;

	/** Returns the cost of a step of a way as the player of a hero knows it: EntranceCost through an entrance, UnknownCost onto a cell it has
	 *  not explored, else what GetStepCost gives */
	float GetWayStepCost(const FHexHero& Hero, EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough) const;

	/** Returns what the visit of the site of a cell costs a hero that stands on a cell beside it, as a step onto that cell would */
	float GetVisitCost(const AHexPlanet& Planet, int32 From, int32 SiteCell) const;

	/** Returns what the cheapest way to the site of a cell costs with its visit, from the ways found for a hero, and the node of the cell
	 *  beside it that way ends on, which may be where the hero stands; for a pile the way onto its cell. A negative value without a way */
	float GetSiteWayCost(const HexHeroPath::FField& Ways, EHexPlanetLayer Layer, int32 SiteCell, int32* OutNode = nullptr) const;

	/** Sends a hero the game moves to the best of the sites its player has seen and wants (UHexSites::GetGoal) within the reach of its kind.
	 *  Returns true if it went for one or waits for tomorrow to go on, bOutSent whether it set out */
	bool MoveAiToSite(int32 Id, const HexHeroPath::FField& Ways, bool& bOutSent);

	/** A hero that stands beside the site it was sent to visits it, paying as for a step. Returns false and why not */
	bool VisitSite(FHexHero& Hero, FString& OutMessage);

	/** A hero takes the pile that lies on the cell its way ended on, if any; one it only runs over on its way further stays, as the
	 *  developer decided on 2026-11-11 */
	void TakePile(FHexHero& Hero);

	/** Returns true if a cell of a planet holds an entrance or exit that can be used */
	static bool IsUsableEntrance(const AHexPlanet& Planet, int32 Cell);

	/** Finds the cheapest ways of a hero from where it stands, within PathMaxDays of movement */
	/** bThroughUnknown lets the ways cross ground the player of the hero has not explored, each such cell at UnknownCost whatever lies there,
	 *  as a person may send a hero there; without it they keep to what is explored, also on the other side of an entrance or exit they go
	 *  through, as the heroes the game moves do */
	void FindFieldFor(const FHexHero& Hero, HexHeroPath::FField& OutField, bool bThroughUnknown, float MaxCost = -1.0f) const;

	/** Shows on each layer the cells the hero chosen reaches today, with the mines, mills and castles it can visit, while the game is played,
	 *  it is the turn of the person and the hero can take a step. As the developer asked on 2026-11-12 they are found when a hero is chosen
	 *  and when it stops, so while it walks what was shown stays; also when its points or the map changed while it stands */
	void UpdateReach();

	/** What the cells shown were found for: the hero, where it stood, its points and the map */
	int32 ReachHero = 0;
	int32 ReachNode = INDEX_NONE;
	float ReachPoints = -1.0f;
	int32 ReachRevision = -1;
	bool bReachShroud = false;

	/** Whether a way of a hero, or the cell it ends at, lies on ground its player has not explored */
	bool IsWayUnknown(const FHexHero& Hero, TConstArrayView<int32> Path, EHexPlanetLayer TargetLayer, int32 Target) const;

	/** Returns the first cell of a way that lies under the fog of war of the player of a hero, and its layer, or INDEX_NONE */
	int32 FindCheckCell(const FHexHero& Hero, TConstArrayView<int32> Path, EHexPlanetLayer& OutLayer) const;

	/** Checks the way of a hero that reached the first cell of it that lay under the fog, with what its player has seen by now. Returns false
	 *  when the hero stops for the person to decide: the cell it was sent to cannot be reached after all, the rest of its way is blocked, or
	 *  another way is cheaper now */
	bool CheckRoute(FHexHero& Hero);

	/** Whether a cell a player has not explored is a void of the underground beside a passage it did explore: nobody explores the voids, but
	 *  such a one was seen from the passage, as the fog of war shows it (ShroudVoidMargin), so no way is taken to lead over it */
	bool IsSeenVoid(int32 Player, EHexPlanetLayer Layer, int32 Cell) const;

	/** Whether what stands on a cell of a layer that is not built yet keeps heroes out, by its objects.json (UHexPlanetObjects::FindSavedBlockers) */
	bool IsBlockedUnbuilt(const AHexPlanet& Planet, int32 Cell) const;

	/** Adds up what a way of a hero costs as its player knows the ground. Returns false if a step of it cannot be taken */
	bool GetWayCost(const FHexHero& Hero, TConstArrayView<int32> Path, float& OutCost) const;

	/** Whether a way ends with a step over the ground and not one through an entrance or exit: only then does a way that ends on an entrance
	 *  or exit take the hero through it */
	bool EndsWithStep(const FHexHero& Hero, TConstArrayView<int32> Path) const;

	/** Gives the costs of the steps of a way of a hero from where it stands */
	void GetStepCosts(const FHexHero& Hero, TConstArrayView<int32> Path, TArray<float>& OutCosts) const;

	/** Starts the next step of the way of a hero, paying for it. Returns false when the way or its points end, or the step is blocked now */
	bool StartNextStep(FHexHero& Hero);

	/** Stops a hero where it stands, at the end of its way: through the entrance there if it was sent through */
	void Arrive(FHexHero& Hero);

	/** Takes a hero through the entrance or exit it stands on to the other layer, telling the camera unless the caller does. Returns false
	 *  and why it stays */
	bool Travel(FHexHero& Hero, FString& OutMessage, bool bBroadcast = true);

	/** Takes a hero to the same cell of the other layer, through the entrance or exit it stands on, for EntranceCost of its points; its way
	 *  and what it was sent to stay. Returns false and why it stays, with bOutOfPoints of the hero where its points do not pay for it */
	bool PassEntrance(FHexHero& Hero, FString& OutMessage, bool bBroadcast);

	/** Returns true if a hero has the points for a step onto any cell beside it */
	bool CanMove(const FHexHero& Hero) const;

	/** Returns the next hero after one, in the order of their Ids, that stands and can still move today, or null */
	const FHexHero* FindNextAble(int32 After) const;

	/** Chooses another hero when the one chosen is gone, and ends the game when none is left */
	void KeepChosen();

	/** Returns where a hero stands on a cell, as a direction from the center of the planet: its middle, or beside the marker of a cell with
	 *  an entrance or exit that can be used (AHexPlanet::GetEntrancePlace) */
	FVector3d GetStandDirection(const AHexPlanet& Planet, const FHexHero& Hero, int32 Cell) const;

	/** Finds where a hero stands in the world, on the ground of its layer once that is built */
	void PlaceHero(FHexHero& Hero);

	/** Makes or moves the mesh of a hero */
	void UpdateBody(FHexHero& Hero);
	void RemoveBody(int32 Id);

	/** Moves the ring under the hero chosen and hides those of the other layer */
	void UpdateRing();

	/** Lays the ring of a layer on the ground around a hero */
	void BuildRing(const AHexPlanet& Planet, FLayerMarks& Each, const FVector3d& Center, int32 Cell, double Outer, double Width, int32 Segments);

	/** Chooses a hero, and with one that guards a castle that castle */
	void ChooseHero(int32 Id);

	/** The hero on a cell beside the castle a hero guards, onto which that hero is sent from the castle: one of its player swaps places
	 *  with it, another attacks the castle (Sally); null when the cell holds none. OutCastleCell is the cell of the castle beside it */
	const FHexHero* FindSallyTarget(const FHexHero& Hero, EHexPlanetLayer Layer, int32 Cell, int32& OutCastleCell) const;
	bool Sally(FHexHero& Hero, int32 Cell, FString& OutMessage);

	/** Whether the heroes or who moves them differ from what heroes.json holds */
	void UpdateEdited();

	/** Looks whether the person who watches still sees a hero of the player the game moves, or sees it again: where that changed the camera
	 *  stays for AiFogHoldTime, there where it went into the fog or where it is about to come out. Returns true while that hero waits for it */
	bool NoteSeen(FHexHero& Hero);

	/** Whether the hero the game moves now waits for the camera: where the fog of war of the person who watches does not hide it but it is
	 *  on the layer that is not shown, the camera is asked to go there (OnHeroSent), and the hero waits until that layer is shown and the
	 *  trip is over, at most AiViewWaitMax */
	bool WaitForView(const FHexHero& Hero);

	/** Takes the steps of a hero the person who watches does not see at once, until its way ends or a step of it shows */
	void WalkUnseen(FHexHero& Hero);

	/** Returns how many steps of its way a hero can still pay for after the one it takes */
	int32 CountStepsAhead(const FHexHero& Hero) const;

	/** Moves the heroes of a player the game moves on its turn, one after the other, and ends its turn when none has more to do */
	void UpdateAi(float DeltaTime);

	/** The players that have heroes, in the order they move */
	TArray<int32> GetActivePlayers() const;

	/** Begins the turn of a player: a person gets a hero of its own chosen and its fog shown */
	void BeginTurn(int32 Player);

	/** Begins the next day: every hero has its points again and the mines give */
	void BeginDay();

	/** Whether the person who watches sees a cell of a layer, and the step a hero takes */
	bool IsCellSeen(EHexPlanetLayer Layer, int32 Cell) const;
	bool IsStepSeen(const FHexHero& Hero) const;

	/** Sends a hero the game moves to a site its player wants (MoveAiToSite), else to the nearest cell of either layer from which it sees
	 *  ground its player has not explored, else through an entrance whose other side it has not seen. Returns false when it found nowhere to
	 *  go, or waits for tomorrow */
	bool MoveAi(int32 Id);

	/** Shows the way of the hero chosen to the cell it is pointed to, or the rest of the way it walks: a line through a dot on each cell and
	 *  a target on the last, with the numbers of the days; again only when anything it hangs on changed */
	void UpdatePath();
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

	/** Returns the point of the ground of a cell, or of the other cell where the first has none there, in a direction from the center, Lift
	 *  above it; the top of the cell where neither has ground there */
	FVector3d FindGroundPoint(const AHexPlanet& Planet, int32 Cell, int32 OtherCell, const FVector3d& Direction, double Lift);

	/** Returns the up of the ground around the middle of a cell, from three points of it Reach away, so a flat mark lies on a slope */
	FVector3d FindGroundUp(const AHexPlanet& Planet, int32 Cell, double Reach);

	/** Opens to the player of a hero the cells it sees from where it stands (SightRadius), underground only the passages */
	void Explore(const FHexHero& Hero);
	void ExploreAround(int32 Player, EHexPlanetLayer Layer, int32 Cell, int32 Radius);

	/** Every castle opens the fog of war around itself for its owner */
	void ExploreCastles();

	/** Opens what every hero sees from where it stands */
	void ExploreAll();

	/** Chooses the next hero that can move once the pause after the one chosen ran out of steps is over */
	void UpdatePendingNext(float DeltaTime);
	UMaterialInstanceDynamic* GetPathMaterial();
	UMaterialInstanceDynamic* GetLineMaterial();
};
