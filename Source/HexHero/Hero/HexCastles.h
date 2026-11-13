// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexCastleConfig.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexCastles.generated.h"

class AHexPlanet;
class UHexHeroes;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FHexHero;

/** A castle of a planet as a line of castles.json: its faction is the Id of one of DA_HexCastles, its layer Surface or Underground, its
 *  cells the three it takes, and its owner the player who has it when a game starts, -1 for nobody */
USTRUCT()
struct FHexCastleLine
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	FString Faction;

	UPROPERTY()
	FString Layer;

	UPROPERTY()
	TArray<int32> Cells;

	UPROPERTY()
	int32 Owner = -1;

	/** What is built in it when a game starts, the level of each building by its Id, and its garrison then, each written only when set;
	 *  without them a castle has the StartLevel of each building and, of nobody, NeutralGarrisonWeeks of what its dwellings offer */
	UPROPERTY()
	TMap<FString, int32> Built;

	UPROPERTY()
	TArray<FHexStackLine> Garrison;

	/** Whether the line sets the garrison, also to none */
	bool bGarrisonSet = false;

	bool operator==(const FHexCastleLine& Other) const
	{
		return Id == Other.Id && Faction == Other.Faction && Layer == Other.Layer && Cells == Other.Cells && Owner == Other.Owner && Built.OrderIndependentCompareEqual(Other.Built)
			&& Garrison == Other.Garrison;
	}
};

/** castles.json of a planet, at the root of its folder: its castles, a line each, which Git can merge */
USTRUCT()
struct FHexCastlesFile
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TArray<FHexCastleLine> Castles;
};

/** A castle on three cells of a layer that share a corner */
struct FHexCastle
{
	/** Number of the castle on its planet, from 1, which never changes */
	int32 Id = 0;

	/** Index of its faction in DA_HexCastles */
	int32 Faction = 0;

	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;

	/** Its cells: the one with the lowest number first, then the other two counterclockwise seen from outside */
	int32 Cells[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};

	/** The player who has it when a game starts, which castles.json keeps, and who has it in the game played; INDEX_NONE for nobody */
	int32 StartOwner = INDEX_NONE;
	int32 Owner = INDEX_NONE;

	/** The level of each building of its faction, 0 for not built */
	TArray<int32> Levels;

	/** What castles.json says is built in it and guards it when a game starts, by building and as stacks; empty for the defaults */
	TMap<FName, int32> StartLevels;
	TArray<FHexStack> StartGarrison;
	bool bStartGarrison = false;

	/** How many creatures of each level its dwellings offer to hire now */
	TArray<int32> Available;

	/** The creatures that guard it */
	TArray<FHexStack> Garrison;

	/** How many buildings it built or upgraded today */
	int32 BuiltToday = 0;

	/** When it was last taken, in seconds of the world, by which its flag flashes, and when each of its gates last opened */
	double EventTime = -1000.0;
	double GateTimes[6] = {-1000.0, -1000.0, -1000.0, -1000.0, -1000.0, -1000.0};

	/** Where it sits on the ground, found once until the ground changes */
	bool bSeated = false;
	FTransform Seat;

	bool HasCell(int32 Cell) const { return Cells[0] == Cell || Cells[1] == Cell || Cells[2] == Cell; }
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnHexCastleOpened, int32);

/**
 *  The castles of a planet, an object of UHexHeroes that serves both layers, as the developer described them on 2026-11-11 (see
 *  UHexCastleConfig): a castle is taken by a hero, its buildings are built and upgraded, its dwellings offer creatures to hire from the start
 *  of every week, creatures stack and cost their price to go into the army of a hero or the garrison, and what is built shows from outside.
 *
 *  A castle fills three cells that share a corner, so no hero walks over them, and a hero beside it that is sent onto one of them visits it
 *  (Visit, from UHexHeroes::VisitSite): into its own it goes in, when no hero is in it yet, stands in its yard (GetGuardPoint) and guards
 *  it with its army (FindGuard, GetDefense), and the panel of the castle opens (OnCastleOpened); one of nobody or of another player is
 *  taken, at once when nothing guards it, else by weighing the army of the hero against the garrison and the guard behind its walls
 *  (Resolve), as there are no battles yet: the stronger wins, an attacker that is weaker turns back, and one that wins loses a share of
 *  its creatures, goes in, and the hero that guarded the castle is put out beside it without its army.
 *
 *  Its owner gets the gold of its hall every day, builds BuildsPerDay buildings a day (Build), and hires the creatures its dwellings offer
 *  (Hire) into the garrison, or into the army of a hero of its own that stands beside the castle (FindVisitor), which may also swap stacks
 *  with the garrison (MoveStack). A player the game moves builds and hires by itself (RunAi).
 *
 *  castles.json of the planet holds where the castles stand and who has each when a game starts (Save), which the Castles tab of the
 *  palette and hex.Castle edit; what was built and hired is not saved yet. A castle is drawn as the ground it stands on with its rampart
 *  and an instance of the mesh of each building that is built, at the place of that building, in the colors of its faction and its owner
 *  (M_HexPlanetSite)
 */
UCLASS()
class UHexCastles : public UObject
{
	GENERATED_BODY()

public:

	/** Reads DA_HexCastles and castles.json of the planet, once its surface is built */
	void Init(AHexPlanet* InSurface, UHexHeroes* InHeroes);

	/** Places the instances of the layers whose castles or ground changed, every frame */
	void Update();

	const UHexCastleConfig* GetConfig() const { return Config; }
	const TArray<FHexCastle>& GetCastles() const { return Castles; }
	const FHexCastle* FindCastle(int32 Id) const;
	const FHexCastle* FindCastleAt(EHexPlanetLayer Layer, int32 Cell) const;

	/** Counts every change of a castle and of the armies, by which the panel of a castle is made again */
	int32 GetRevision() const { return Revision; }

	/** Gives the three cells of a castle at a cell: that cell and the two that share with it its corner nearest a direction from the center.
	 *  Returns false for a cell that has no such corner */
	bool FindCells(EHexPlanetLayer Layer, int32 Cell, const FVector3d& Direction, int32 OutCells[3]) const;

	/** Puts a castle of a faction on three cells. Returns its Id, or 0 and why not */
	int32 AddCastle(EHexPlanetLayer Layer, const int32 InCells[3], int32 Faction, int32 Owner, FString& OutProblem);

	/** Takes the castle of a cell off the planet. Returns false if there is none */
	bool RemoveCastleAt(EHexPlanetLayer Layer, int32 Cell);

	/** Returns the lines of castles.json for the castles, and sets the castles from such lines, for Undo of the brush */
	TArray<FHexCastleLine> MakeLines() const;
	void SetLines(const TArray<FHexCastleLine>& Lines);

	bool IsEdited() const { return bEdited; }
	bool Save(const FString& PlanetFolder, FString& OutError);

	/** A new game: every castle is its first owner's, with the buildings it starts with, what a week offers and, when nobody owns it, its guard */
	void Restart();

	/** A new day: the halls give gold and the castles may build again, and on the first day of a week the dwellings offer more */
	void BeginDay(int32 Day, int32 DaysPerWeek);

	/** Returns the level of the hall, the fort or a dwelling of a castle, 0 for not built */
	int32 GetLevel(const FHexCastle& Castle, EHexBuildingType Type, int32 Creature = 0) const;

	/** Whether the owner of a castle can build the next level of a building now, and why not */
	bool CanBuild(const FHexCastle& Castle, int32 Building, FString* OutWhy = nullptr) const;
	bool Build(int32 CastleId, int32 Building, FString& OutMessage);

	/** The hero that guards a castle or stands beside it buys a catapult there for CatapultCost, which shoots at the walls of a castle it
	 *  attacks. Returns false and why not */
	bool BuyCatapult(int32 CastleId, int32 HeroId, FString& OutMessage);

	/** Returns how many creatures of a level the owner of a castle can hire now: what its dwelling offers and what it can pay for */
	int32 GetHireMax(const FHexCastle& Castle, int32 Creature) const;

	/** Hires creatures of a level into the garrison, or into the army of a hero of the owner that stands beside the castle */
	bool Hire(int32 CastleId, int32 Creature, int32 Count, int32 HeroId, FString& OutMessage);

	/** Moves a stack between the garrison and the army of a hero of the owner that stands beside the castle */
	bool MoveStack(int32 CastleId, int32 HeroId, bool bToHero, int32 Slot, FString& OutMessage);

	/** A hero visits a castle from a cell beside it, by a move onto a cell of it: its own it enters and guards, another is taken if the
	 *  hero is strong enough. Returns a line for the player; bShown lets what happened float up over the castle */
	FString Visit(FHexHero& Hero, int32 CastleId, int32 Cell, bool bShown);

	/** Returns the hero that is in a castle and guards it, or null, and where it stands there, in the yard */
	const FHexHero* FindGuard(const FHexCastle& Castle) const;

	/** Stacks as lines of a file, by the Ids of their faction and creature, and back; a line that names no creature is left out */
	TArray<FHexStackLine> MakeStackLines(TConstArrayView<FHexStack> Stacks) const;
	TArray<FHexStack> ParseStackLines(TConstArrayView<FHexStackLine> Lines) const;

	/** Makes what is built in a castle and its garrison now what it has when a game starts, which castles.json keeps (hex.Castle start) */
	bool KeepAsStart(int32 CastleId);

	/** Builds every building of a castle to about half of its levels, rounded up, for nothing, for trying the game (hex.Cheat buildhalf) */
	int32 BuildHalf(int32 CastleId);

	/** The DefenseFactor of the level of the fort of a castle, 1 without one */
	float GetFortFactor(const FHexCastle& Castle) const;
	bool GetGuardPoint(const FHexCastle& Castle, FVector3d& OutPoint) const;

	/** Says that an army or who stands where changed, so the panel of a castle is made again */
	void NoteChanged() { ++Revision; }

	/** Opens the gate of a castle nearest a point for a hero that goes in or out by it, and gives where that gate is and where the hero
	 *  that guards the castle stands in the yard, on the ground. Returns false for a castle whose layer is not built */
	bool OpenGate(int32 CastleId, const FVector3d& Towards, FVector3d& OutGate, FVector3d& OutYard);

	/** The gate of a castle nearest a point and the place of its hero in the yard, on the ground, for the way shown into it */
	bool FindGate(const FHexCastle& Castle, const FVector3d& Towards, FVector3d& OutGate, FVector3d& OutYard, int32* OutIndex = nullptr) const;

	/** The cells outside a castle before its gates, and the cell of the castle each gate is in, by which a hero sent into it walks to the
	 *  nearest gate whichever of its cells was clicked, as the developer asked on 2026-11-12 */
	void GetGateCells(const FHexCastle& Castle, TArray<int32>& OutOutside, TArray<int32>& OutInside) const;

	/** How many of a creature its dwelling offers a week */
	int32 GetWeeklyGrowth(const FHexCastle& Castle, int32 Creature) const { return GetGrowth(Castle, Creature); }

	/** A cheat: lets every castle build as much as it can pay for in a day (hex.Cheat build) */
	void SetUnlimitedBuilds(bool bOn) { bUnlimitedBuilds = bOn; ++Revision; }

	/** A cheat: builds every building of a castle to its last level at once, for nothing; gives how many levels it built */
	int32 BuildAll(int32 CastleId);
	bool HasUnlimitedBuilds() const { return bUnlimitedBuilds; }

	/** Returns how much an army counts, and a garrison behind the walls of its castle */
	float GetPower(TConstArrayView<FHexStack> Army) const;
	float GetDefense(const FHexCastle& Castle) const;

	/** Returns how much the army of a hero counts when it attacks */
	float GetAttack(const FHexHero& Hero) const;

	/** Weighs an attack against a defense: returns true if the attacker wins, and the share of its creatures it loses */
	static bool Resolve(float Attack, float Defense, float LossPower, float LossMax, float& OutLossShare);

	/** Takes a share of the creatures of an army away, the same of every stack, leaving one creature at least */
	static void ApplyLosses(TArray<FHexStack>& Army, float Share);

	/** Adds a stack to an army: to a stack of its kind, else to a free slot. Returns false when the army is full */
	static bool AddToArmy(TArray<FHexStack>& Army, const FHexStack& Stack);

	/** Returns the faction of a player, that of its first castle, else one by its number, and the army a hero of it starts with */
	int32 GetPlayerFaction(int32 Player) const;
	TArray<FHexStack> MakeStartArmy(int32 Player) const;

	/** Returns the hero of the owner of a castle that is in it, else one that stands on a cell beside it, the one chosen first, or null */
	const FHexHero* FindVisitor(const FHexCastle& Castle) const;

	/** A player the game moves builds what it can in each of its castles and hires into their garrisons */
	void RunAi(int32 Player);

	/** Returns "12 Sentinel" for a stack, a line for an army, and a line about the castle on a cell for the panel at the cursor */
	FString DescribeStack(const FHexStack& Stack) const;
	FString DescribeArmy(TConstArrayView<FHexStack> Army) const;
	FString GetCellText(EHexPlanetLayer Layer, int32 Cell) const;

	/** Says that the ground of a layer changed, so its castles are seated and placed on it again */
	void OnLayerRebuilt(EHexPlanetLayer Layer);

	/** A person looks into a castle of its own */
	FOnHexCastleOpened OnCastleOpened;

private:

	/** A battle for a castle takes its garrison and gives it to who wins */
	friend class UHexBattles;


	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Surface;

	UPROPERTY(Transient)
	TObjectPtr<UHexHeroes> Heroes;

	UPROPERTY(Transient)
	TObjectPtr<const UHexCastleConfig> Config;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	/** The instances of the castles of each layer, a component for each mesh */
	TMap<FString, TWeakObjectPtr<UInstancedStaticMeshComponent>> Instances[NumPlanetLayers];

	TArray<FHexCastle> Castles;
	TArray<FHexCastleLine> SavedLines;
	int32 NextId = 1;
	int32 Revision = 0;
	bool bEdited = false;
	bool bUnlimitedBuilds = false;
	bool bDirty[NumPlanetLayers] = {true, true};
	bool bLayerBuilt[NumPlanetLayers] = {false, false};

	AHexPlanet* GetPlanet(EHexPlanetLayer Layer) const;
	FHexCastle* FindCastleMutable(int32 Id);

	/** Puts a castle back to how a game starts */
	void ResetCastle(FHexCastle& Castle) const;

	/** Returns what a week adds to what a dwelling of a castle offers */
	int32 GetGrowth(const FHexCastle& Castle, int32 Creature) const;

	/** Orders three cells that share a corner: the lowest first, the others counterclockwise. Returns false if they share none */
	bool OrderCells(EHexPlanetLayer Layer, int32 InOutCells[3]) const;

	/** The ground of a castle while its pieces are stood on it: the triangles of the cells they lie on, read once each */
	struct FGround
	{
		TMap<int32, TArray<FVector3f>> Triangles;
		FVector3d Up = FVector3d::ZeroVector;
		FVector3d Forward = FVector3d::ZeroVector;
		FVector3d Left = FVector3d::ZeroVector;
		/** The radius of the ground at the corner the cells share, and the size of a cell there over the 100 units the meshes take it for */
		double Radius = 0.0;
		double Unit = 1.0;
	};

	/** Finds the frame of a castle on its ground: up at the corner its cells share, forward towards the middle of the first of them */
	void MakeGround(const AHexPlanet& Planet, const FHexCastle& Castle, FGround& OutGround) const;

	/** Returns the radius of the ground in a direction from the center of the planet, near a castle */
	double FindGroundRadius(const AHexPlanet& Planet, const FHexCastle& Castle, FGround& Ground, const FVector3d& Direction) const;

	/** Returns where a piece of a castle stands: on the ground at a place given in radii of a cell from the corner the cells share, upright
	 *  there and turned by Yaw degrees, so each piece follows the ground under it */
	FTransform PlaceOn(const AHexPlanet& Planet, const FHexCastle& Castle, FGround& Ground, const FVector2D& Place, float Yaw, float Scale) const;

	/** Places the instances of a layer again */
	void Rebuild(EHexPlanetLayer Layer);

	/** Marks a castle changed, for the panel and for its instances */
	void Touch(const FHexCastle& Castle);

	FLinearColor GetPlayerColor(int32 Player) const;
	FString GetPlayerName(int32 Player) const;
};
