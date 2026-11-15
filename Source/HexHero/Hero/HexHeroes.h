// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexCastleConfig.h"
#include "HexHeroConfig.h"
#include "HexUiConfig.h"
#include "HexHeroPath.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexHeroes.generated.h"

class AHexPlanet;
class UHexHeroConfig;
class UHexCastles;
class UHexSites;
class UHexShroud;
class UHexBattles;
class UHexNeutrals;
struct FHexNeutral;
struct FHexSite;
struct FHexBattleSideSetup;
class UHexBattleConfig;
enum class EHexAdventureSpell : uint8;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UStaticMeshComponent;

/**
 *  A hero on the map of the planet: a leader who walks its cells, on the surface or underground. It does not fight itself: its Attack and
 *  Defense are added to those of every unit of its army, its Spell Power is the strength of its spells and its Knowledge its mana, as the
 *  developer described on 2026-11-10; they count in battles (UHexBattles), and nothing levels up yet
 */
/** A secondary skill a hero has, at its level: 1 Basic, 2 Advanced, 3 Expert */
struct FHexHeroSkill
{
	EHexSkill Skill = EHexSkill::Sorcery;
	int32 Level = 1;
	bool operator==(const FHexHeroSkill& Other) const { return Skill == Other.Skill && Level == Other.Level; }
};

/** A level a hero rose to: the attribute it raised and the skills it may choose from, a new one or one a level higher */
struct FHexLevelUp
{
	int32 Level = 2;
	FString Raised;
	TArray<FHexHeroSkill> Offers;
};

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

	/** How it is turned at rest when a game starts, in degrees from where it faces by itself, set in the editor (heroes.json) */
	int32 StartYaw = 0;

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

	/** Its secondary skills, at most MaxSkills of DA_HexHeroes, which it chooses as it rises in level (UHexHeroes::AddExperience) */
	TArray<FHexHeroSkill> Skills;

	/** The spells it knows, by their Ids in DA_HexBattle: those of its class and those it learned in mage guilds (UHexCastles) */
	TArray<FName> Spells;

	/** How many times it cast each spell of the planet today, by its Id, which CastsPerDay of a spell limits */
	TMap<FName, int32> CastsToday;

	/** The levels it rose to whose skill its person has not chosen yet, the first first; the game chooses at once for a player it moves */
	TArray<FHexLevelUp> PendingLevelUps;

	/** The artifacts it wears, by their index in Artifacts of DA_HexHeroes, a slot each of NumWornSlots (GetWornSlotKind), INDEX_NONE for an
	 *  empty one, and those it carries besides; what they give its attributes is in Attack, Defense, SpellPower and Knowledge already
	 *  (UHexHeroes::Wear) */
	TArray<int32> Worn;
	TArray<int32> Backpack;

	/** The boat it sails in, by the Id of its site (EHexSiteType::Boat), 0 on land, and whether it stands over the sea now, on the deck of a boat */
	int32 BoatId = 0;
	bool bOnDeck = false;

	/** The cells it still walks to after the step it takes now, the next first */
	TArray<int32> Route;

	/** True while it walks, the cell it left on the step it takes and how far along that step it is, 0 to 1 */
	bool bWalking = false;
	int32 FromCell = INDEX_NONE;
	float StepAlpha = 0.0f;

	/** The points the step it takes paid as it set out, which the list of heroes shows spent along the step (UHexHeroes::GetShownMovePoints) */
	float StepPaid = 0.0f;

	/** How fast it walks now, in cells a second: it sets out and stops smoothly (WalkStartTime, WalkStopTime) */
	float Speed = 0.0f;

	/** The creatures it leads, up to NumArmySlots stacks of them (UHexCastles): hired in castles, which fight its battles (UHexBattles) */
	TArray<FHexStack> Army;

	/** It has a catapult, bought in a castle (UHexCastles::BuyCatapult), which shoots at the walls of a castle it attacks; lost when it is
	 *  destroyed there */
	bool bCatapult = false;

	/** It starts with a catapult, as its line of heroes.json says */
	bool bStartCatapult = false;

	/** It was sent with Ctrl, as the developer asked on 2026-11-13: onto a hero of its own it swaps places with it, and onto a castle of its own
	 *  it goes before its gate without going in; without Ctrl onto a hero of its own it comes beside it and they exchange their armies */
	bool bAltOrder = false;

	/** The army it starts with, as heroes.json has it, empty for the default of its player (UHexCastles::MakeStartArmy) */
	TArray<FHexStack> StartArmy;

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
	/** The glide out of the mouth on a layer that was not built when the hero went through, as an underground never shown is built only
	 *  when the camera comes to it: its points are found once it is (PlaceHero), as before 2026-11-14 they were found on nothing and the camera
	 *  followed the hero to a place far away on its first way down */
	bool bMouthGlidePending = false;
	int32 GateStepFrom = INDEX_NONE;

	/** For a hero of a player the game moves: seconds until it looks for where to go again, and whether it found nothing more to do today */
	float AiWait = 0.0f;
	bool bAiDone = false;

	/** The site a hero of such a player goes for, so another hero of its player takes another, 0 for none */
	int32 AiSiteId = 0;

	/** What a hero of such a player set out to fight, a castle, a hero or a neutral stack, which it keeps to day after day until it is gone or
	 *  something more pressing comes within a day, as the developer asked on 2026-11-14 ("потрібно герою дати ціль від якої він не відходить,
	 *  поки ціль щось не поміняє критично"): the key of the foe (UHexHeroes::MoveAiToSite), its layer and cell, empty for none */
	FString AiGoal;
	EHexPlanetLayer AiGoalLayer = EHexPlanetLayer::Surface;
	int32 AiGoalCell = INDEX_NONE;

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

/** Neutral creatures that offer to join a hero instead of fighting it (Diplomacy), for nothing or for gold, or a chest a hero opened, which
 *  gives gold or experience: what waits for the person to answer */
struct FHexHeroOffer
{
	int32 HeroId = 0;
	/** The stack that offers to join, by its Id, 0 for a chest */
	int32 NeutralId = 0;
	FHexStack Stack;
	int32 Gold = 0;
	int32 Experience = 0;
	bool bFree = false;
	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
	int32 Cell = INDEX_NONE;
	/** The points of movement the visit took, given back when the battle cannot begin */
	float Cost = 0.0f;
	/** A shipyard that offers to build a boat, by the Id of its site, with what it costs and the water cell it would lie on */
	int32 ShipyardId = 0;
	int32 BoatCell = INDEX_NONE;
	TMap<FName, int32> BoatCost;
	bool IsSet() const { return HeroId != 0; }
	bool IsChest() const { return IsSet() && NeutralId == 0 && ShipyardId == 0; }
	bool IsShipyard() const { return IsSet() && ShipyardId != 0; }
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
/** What the panel at the cursor says while the game is played (UHexHeroes::GetMapTip, SHexMapTip): a title, lines, an army as portraits with
 *  their numbers or ranges (ArmyCounts, one a stack, empty for the numbers), and a rough forecast of a battle there */
/** A stat of a hero in the panel at the cursor: its icon, its value and its name */
struct FHexMapTipStat
{
	EHexStatIcon Icon = EHexStatIcon::Attack;
	FString Value;
	FString Name;
	bool operator==(const FHexMapTipStat& Other) const { return Icon == Other.Icon && Value == Other.Value; }
};

struct FHexMapTip
{
	FString Title;
	FLinearColor TitleColor = FLinearColor::White;
	TArray<FString> Lines;
	/** The attributes of a hero with their icons, and its skills */
	TArray<FHexMapTipStat> Stats;
	TArray<FHexHeroSkill> Skills;
	TArray<FHexStack> Army;
	TArray<FString> ArmyCounts;
	FString Forecast;
	FLinearColor ForecastColor = FLinearColor::White;
	/** The portrait of a hero or the picture of a castle beside the title, framed in TitleColor, as the developer asked on 2026-11-15 */
	class UTexture2D* Picture = nullptr;
	FLinearColor PictureTint = FLinearColor::White;

	bool operator==(const FHexMapTip& Other) const
	{
		if (!(Title == Other.Title && Picture == Other.Picture && Lines == Other.Lines && ArmyCounts == Other.ArmyCounts && Forecast == Other.Forecast && Army.Num() == Other.Army.Num()
			&& Stats == Other.Stats && Skills == Other.Skills))
		{
			return false;
		}
		for (int32 Index = 0; Index < Army.Num(); ++Index)
		{
			if (!Army[Index].IsSameKind(Other.Army[Index]) || Army[Index].Count != Other.Army[Index].Count)
			{
				return false;
			}
		}
		return true;
	}
};

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

	/** The battles, and the neutral stacks of the map, which a hero fights (UHexBattles, UHexNeutrals) */
	UHexBattles* GetBattles() const { return Battles; }
	UHexNeutrals* GetNeutrals() const { return Neutrals; }

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

	/** Gives a hero creatures, joined to a stack of their kind or in a slot of their own, for trying the game (hex.Cheat units). Returns
	 *  false when its army has no room */
	bool AddUnits(int32 Id, const FHexStack& Stack);

	/** Gives a hero a catapult, or takes it, for trying the game (hex.Cheat catapult). Returns false without such a hero */
	bool SetCatapult(int32 Id, bool bHas);

	/** A player has explored the whole of both layers, the passages underground, for trying the game (hex.Cheat reveal). Returns the cells
	 *  that were not explored before */
	int32 RevealAll(int32 Player);

	/** Makes the army of a hero now the one it starts with, which heroes.json keeps (hex.Hero startarmy) */
	bool KeepArmyAsStart(int32 Id);

	/** Whether the game moves the heroes of a player by itself, as the Heroes tab of the palette sets and heroes.json keeps: such a hero gathers
	 *  what its player has seen, takes mines and walks to open the map (MoveAi) */
	bool IsPlayerAi(int32 Player) const { return AiPlayers.Contains(Player) || AutoPlayers.Contains(Player); }

	/** A player of a person the game moves for a while, as the developer asked on 2026-11-14 for a cheat (hex.Cheat autoplay); not saved, so
	 *  heroes.json keeps who a person plays */
	void SetPlayerAuto(int32 Player, bool bAuto);
	bool IsPlayerAuto(int32 Player) const { return AutoPlayers.Contains(Player); }
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

	/** Returns the points of movement a hero has each day, more with Logistics */
	float GetMaxMovePoints(const FHexHero& Hero) const;

	/** The level of a skill of a hero, 0 without it, and its value there (FHexSkillInfo::Values, or SecondValues) */
	int32 GetSkillLevel(const FHexHero& Hero, EHexSkill Skill) const;
	float GetSkillValue(const FHexHero& Hero, EHexSkill Skill, bool bSecond = false) const;

	/** The mana a hero has at most, by its Knowledge */
	int32 GetMaxMana(const FHexHero& Hero) const;

	/** The points of movement a hero has as the list shows them, spent as it walks along its step and not at once as it sets out on it, as the
	 *  developer asked on 2026-11-15 */
	float GetShownMovePoints(const FHexHero& Hero) const
	{
		return Hero.MovePoints + (Hero.bWalking ? Hero.StepPaid * FMath::Clamp(1.0f - Hero.StepAlpha, 0.0f, 1.0f) : 0.0f);
	}

	/** The panel at the cursor of a hero a player would hire of a class: its portrait, attributes, first skill and the army it comes with */
	void GetClassTip(int32 Class, int32 Player, FHexMapTip& OutTip) const;

	/** The portrait of a hero, that of its class (FHexHeroClass::Portrait), or null */
	class UTexture2D* GetHeroPortrait(const FHexHero& Hero) const;

	/** The spells of the planet, as the developer asked on 2026-11-15 (EHexSpellKind::Adventure of DA_HexBattle, HexHeroMagic.cpp): those a
	 *  hero knows, by their index in DA_HexBattle */
	TArray<int32> GetAdventureSpells(const FHexHero& Hero) const;

	/** What a spell of the planet costs a hero, less what the skill of its school takes off, and how far it reaches in cells, more with that
	 *  skill; a view at Expert reaches the whole layer (INT32_MAX) */
	int32 GetAdventureManaCost(const FHexHero& Hero, int32 Spell) const;
	int32 GetAdventureReach(const FHexHero& Hero, int32 Spell) const;

	/** Whether a hero may cast a spell of the planet now: it knows it, it is its turn, it stands, it has the mana, the points of movement and
	 *  the casts of the day it takes, and there is something to cast it on; else why not */
	bool CanCastAdventure(const FHexHero& Hero, int32 Spell, FString* OutWhy = nullptr) const;

	/** Where a hero may step with a Dimension door: cells of its layer within its reach that it may stand on, that its player has explored and
	 *  where nothing lies */
	TArray<int32> GetDoorCells(const FHexHero& Hero, int32 Spell) const;

	/** The castles a Town portal takes a hero into: those of its player that no hero guards, nearest first; from Advanced of its school any of
	 *  them, below that the nearest alone */
	TArray<int32> GetPortalCastles(const FHexHero& Hero, int32 Spell) const;

	/** Casts a spell of the planet: a Dimension door onto TargetCell, a Town portal into the castle TargetCastle (0 for the first it may go
	 *  to); returns false and why not */
	bool CastAdventure(int32 HeroId, int32 Spell, int32 TargetCell, int32 TargetCastle, FString& OutMessage);

	/** While a person aims a spell of the planet (a Dimension door), the outline of the reach shows the cells it may be cast on in place of
	 *  where the hero walks today; no hero (0) gives that back */
	void SetAimCells(int32 HeroId, TConstArrayView<int32> Cells);

	/** A hero cast a spell of the planet that changes where it is, for the camera of the person */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeroTeleported, int32);
	FOnHeroTeleported OnHeroTeleported;

	/** A hero gains experience, as the developer asked on 2026-11-15 for skills gained as heroes rise: each level it reaches raises one of its
	 *  attributes by the chances of its class and offers two skills, a new one and one a level higher, which the game chooses for a player it
	 *  moves and the person chooses in a dialog (PendingLevelUps, ChooseLevelUp) */
	void AddExperience(FHexHero& Hero, int32 Amount);
	void AddExperience(int32 HeroId, int32 Amount);

	/** The person chooses the skill of the first level a hero rose to, by its offer */
	bool ChooseLevelUp(int32 HeroId, int32 Offer, FString& OutMessage);

	/** The hero of the person who plays whose level waits for its skill to be chosen, or null */
	const FHexHero* FindPendingLevelUp() const;

	/** A hero learns spells by their Ids, those it knew already left out; returns how many it learned */
	int32 LearnSpells(FHexHero& Hero, const TArray<FName>& Spells);

	/** What the spells and the skills of magic of a hero give a side of a battle it leads */
	void FillBattleMagic(const FHexHero& Hero, FHexBattleSideSetup& Side, const UHexBattleConfig* Battle) const;

	/** A cost of ground for a hero: what lies over that of plains, less what its Pathfinding takes off */
	float AdjustForPathfinding(const FHexHero* Hero, float Cost) const;

	/** A hero rose to a level, by its Id */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32);
	FOnLevelUp OnLevelUp;

	/** Whether neutral creatures offer to join a hero, as the developer asked on 2026-11-15 for Diplomacy: when its army counts JoinPowerRatio
	 *  times theirs, for nothing by the chance of its Diplomacy and else for their price less what Diplomacy takes off; and the offer */
	bool MakeJoinOffer(const FHexHero& Hero, const FHexNeutral& Neutral, FHexHeroOffer& OutOffer) const;

	/** What waits for the person to answer: creatures that offer to join, or a chest; and the answer, true to take the creatures or the gold,
	 *  false to fight or to take the experience */
	const FHexHeroOffer& GetOffer() const { return PendingOffer; }
	bool AnswerOffer(bool bYes, FString& OutMessage);

	/** An offer came for the person to answer */
	DECLARE_MULTICAST_DELEGATE(FOnOffer);
	FOnOffer OnOffer;

	/** The artifacts of a hero, as the developer asked on 2026-11-15 (HexHeroArtifacts.cpp): an artifact of DA_HexHeroes by its index, or null */
	const FHexArtifact* GetArtifact(int32 Index) const;

	/** What the artifacts a hero wears give together */
	FHexArtifactBonus GetArtifactBonus(const FHexHero& Hero) const;

	/** Gives a hero an artifact: it wears it in a free slot of its kind, else carries it in its backpack. Returns false when the backpack is full */
	bool GiveArtifact(FHexHero& Hero, int32 Artifact);
	bool GiveArtifact(int32 HeroId, int32 Artifact);

	/** A hero puts on the artifact of a place in its backpack, taking off what wears its slot when all of its kind are full, and takes one off
	 *  into the backpack; false and why not */
	bool WearArtifact(int32 HeroId, int32 BackpackIndex, FString& OutMessage);
	bool TakeOffArtifact(int32 HeroId, int32 WornSlot, FString& OutMessage);

	/** A hero gives an artifact it wears (bWorn, by its slot) or carries to another of its player beside it, into its backpack */
	bool PassArtifact(int32 FromHero, bool bWorn, int32 Index, int32 ToHero, FString& OutMessage);

	/** What a beaten hero wore and carried goes to the hero that beat it, as far as its backpack holds; returns how many */
	int32 TakeArtifacts(int32 WinnerId, int32 LoserId);

	/** An artifact of a rank between Low and High, by a hash, or INDEX_NONE without one */
	int32 PickArtifact(uint32 Hash, EHexArtifactRank Low, EHexArtifactRank High) const;

	/** The artifact a site holds: one that lies on the map, or a chest that holds one instead of gold, INDEX_NONE for none */
	int32 GetSiteArtifact(const FHexSite& Site) const;

	/** Whether a cell of a layer is sea, which only a hero in a boat crosses */
	bool IsSea(EHexPlanetLayer Layer, int32 Cell) const;

	/** A cheat (hex.Cheat boat), as the developer asked on 2026-11-15: a boat on the free sea nearest a hero, within a few cells of it, as a
	 *  shipyard builds one. Returns false and why not */
	bool SummonBoat(int32 HeroId, FString& OutMessage, int32 MaxRings = 4);

	/** The free sea nearest a hero within so many rings of cells, where nothing lies and nobody stands, INDEX_NONE for none */
	int32 FindFreeSea(const FHexHero& Hero, int32 MaxRings) const;

	/** A cheat (hex.Cheat teleport), as the developer asked on 2026-11-15: a hero on a cell of a layer at once, where it may stand; and the
	 *  ground nearest it on its layer beside free sea, where a boat may be summoned, INDEX_NONE for none */
	bool CheatTeleport(int32 HeroId, EHexPlanetLayer Layer, int32 Cell, FString& OutMessage);
	int32 FindCoastCell(const FHexHero& Hero) const;

	/** The rules of the sea for a step of a hero, as the developer asked on 2026-11-15 for boats: true when they decide the step, with its cost,
	 *  negative for none. A hero on land steps onto the sea only onto a boat nobody sails, and its way ends there; a hero in a boat sails the sea
	 *  and steps ashore onto ground it may stand on, where its way ends; each costs SeaStepCost, and going aboard or ashore takes the points
	 *  left that day (StartNextStep) */
	bool GetSeaStep(const FHexHero* Hero, const AHexPlanet& Planet, int32 From, int32 To, float& OutCost) const;

	/** What the cheapest step onto a cell beside a hero costs, or -1 where it can step nowhere, so the panel says when the points left pay for
	 *  no step, as the developer found heroes stop with 1 of 10 left on sand on 2026-11-14 */
	float GetCheapestStep(const FHexHero& Hero) const;

	/** The list of heroes changed, so the panels make it again */
	void NoteListChanged() { ++ListRevision; }

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
		/** The hero chosen was clicked again: its army opens (OnArmyOpened) */
		Army,
	};

	/** What the panel at the cursor says of a cell of a layer while the game is played, with the hero whose body is under the cursor: a hero of
	 *  the person, what it leads and has left; a pile or a mine; a neutral stack, a hero of another player or a castle with how many it seems
	 *  to have and a rough forecast of a battle against it, as the developer asked on 2026-11-13. False for nothing to say */
	bool GetMapTip(EHexPlanetLayer Layer, int32 Cell, const FHexHero* Under, FHexMapTip& OutTip, bool bHeroOnly = false) const;

	/** A neutral stack beside the castle a hero guards, which that hero sent onto it fights there as a siege, the hero behind its walls, as the
	 *  developer asked on 2026-11-13; null for none, and the cell of the castle beside it */
	const FHexNeutral* FindSallyNeutral(const FHexHero& Hero, EHexPlanetLayer Layer, int32 Cell, int32& OutCastleCell) const;

	/** Says whether the next order of a hero was given with Ctrl (FHexHero::bAltOrder) */
	void SetAltOrder(int32 Id, bool bAlt);

	/** Returns whether a hero of the player of a hero, not that one and not in a castle, stands on a cell, which it may meet there */
	const FHexHero* FindFriendAt(const FHexHero& Hero, EHexPlanetLayer Layer, int32 Cell) const;

	/** Moves Count creatures (0 for all) of a stack of the army of a hero to a slot of the army of the same hero or of another of its player
	 *  beside it: onto an empty slot (one past the last) they make a stack of their own, onto one of their kind they join it, and the whole
	 *  stack onto another kind swaps places with it, as the developer asked on 2026-11-13 for an army panel that swaps, splits and joins.
	 *  Returns false and why not */
	bool ArrangeArmy(int32 FromHero, int32 FromSlot, int32 ToHero, int32 ToSlot, int32 Count, FString& OutMessage);

	/** Broadcast when a person opens the army of a hero, with the other hero of its player it met, 0 for none, for the panel of the army */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnArmyOpened, int32, int32);
	FOnArmyOpened OnArmyOpened;

	/** Broadcast when the turn of a person without a hero chose its first castle, for the camera to show it */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCastleChosenByTurn, int32);
	FOnCastleChosenByTurn OnCastleChosen;

	/** A left click of the player on a cell of the layer shown, with the hero whose body is under the cursor, if any: a click on another hero
	 *  chooses it; on a cell it points the hero chosen there, so its way shows; a second click on that cell sets it walking; and while it
	 *  walks any click stops it. OutMessage says why nothing came of it */
	EClick Click(EHexPlanetLayer Layer, int32 Cell, const FHexHero* Under, FString& OutMessage);

	/** What a click there, or sending the hero chosen there, would do, for the cursor of the game, as the developer asked on 2026-11-12: choose
	 *  a hero or castle of the person; walk there today or on a later day; nothing, as no way leads there; attack a neutral stack, a hero of
	 *  another player or a castle that is defended; go into a castle or take one nobody defends; visit a mine or mill or take a pile; go
	 *  through an entrance (bMouth: the cell is that of the mouth under the cursor, on the other layer); or swap places with a hero of its own
	 *  beside the castle it guards */
	enum class EOrder : uint8
	{
		None,
		Choose,
		Move,
		MoveLater,
		Blocked,
		Attack,
		Castle,
		Visit,
		Portal,
		Swap,
		/** Walk to a hero of its own it is pointed to and exchange the armies */
		Exchange,
		/** Go aboard a boat, go ashore from one, and go ashore onto a foe to fight it, as the developer asked on 2026-11-15 */
		Board,
		Land,
		LandAttack,
	};
	EOrder GetOrderAt(EHexPlanetLayer Layer, int32 Cell, const FHexHero* Under, bool bMouth, bool bAlt = false);

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

	/** The game ended, as the developer asked on 2026-11-14: a player whose heroes and castles are all gone is out, and the persons lose when
	 *  none of them is left, or win when the players left are theirs alone, whoever got there first (CheckGameEnd). True when a person won,
	 *  with a line about it */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameOver, bool, const FString&);
	FOnGameOver OnGameOver;

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

	/** The castles hire into the armies of the heroes and take from them what an attack costs, and a battle gives them what is left */
	friend class UHexCastles;
	friend class UHexBattles;
	friend class UHexNeutrals;

	UPROPERTY()
	TObjectPtr<UHexShroud> Shroud;

	UPROPERTY()
	TObjectPtr<UHexBattles> Battles;

	UPROPERTY()
	TObjectPtr<UHexNeutrals> Neutrals;

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

	/** The players hex.Cheat autoplay gives the game for a while */
	TSet<int32> AutoPlayers;

	/** The players that had a hero or a castle when the game started, of whom the last left wins */
	TArray<int32> StartPlayers;

	/** Ends the game once one side is left, see OnGameOver */
	void CheckGameEnd();

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
	/** Which layers were built when the way was shown, a bit each, so a layer built since gets its marks */
	int32 ShownBuilt = 0;
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

	/** A panel lies over the map (the panel of a castle the hero went into, its army): the next hero waits until it closes */
	bool bHoldNext = false;

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
	float GetSiteWayCost(const HexHeroPath::FField& Ways, EHexPlanetLayer Layer, int32 SiteCell, int32* OutNode = nullptr, int32 ForPlayer = INDEX_NONE) const;

	/** The cell of the castle on a cell that a hero beside it at From goes in by: the one clicked if it lies beside From, else another of the
	 *  castle that does; any other cell as it is */
	int32 GetCastleEntryCell(EHexPlanetLayer Layer, int32 From, int32 SiteCell) const;

	/** Whether a cell holds what a hero of a player fights rather than walks onto: a neutral stack, or a hero of another player out of a castle */
	bool IsFoeAt(int32 Player, EHexPlanetLayer Layer, int32 Cell) const;

	/** Whether a site on a cell is held by a guard a hero of a player fights before it takes it: a mine or mill of nobody or of another player
	 *  whose guard has creatures and on which no hero stands, or an artifact its guard holds (TakePile), so the cursor and the end of a way
	 *  show an attack there, as the developer asked on 2026-11-15 */
	bool IsGuardedSiteAt(int32 Player, EHexPlanetLayer Layer, int32 Cell) const;

	/** Sends a hero the game moves to the best of the sites its player has seen and wants (UHexSites::GetGoal) within the reach of its kind,
	 *  or to fight a castle, a hero or a neutral stack it beats in battles fought in advance (UHexBattles::Forecast): a neutral stack only for
	 *  losses of AiNeutralLossShare, or of AiLossShare with bBreakThrough, when nothing else is left for it, as a guard of a way to another
	 *  zone. Returns true if it went for one or waits for tomorrow to go on, bOutSent whether it set out */
	bool MoveAiToSite(int32 Id, const HexHeroPath::FField& Ways, bool& bOutSent, bool bBreakThrough = false);

	/** Sends a hero the game moves into a castle of its own within AiReinforceDays to take what is there (UHexCastles::AiTake), when that
	 *  counts AiReinforceShare of its army, or anything at all with bAny. Returns true if it went */
	bool MoveAiHome(int32 Id, const HexHeroPath::FField& Ways, bool bAny);

	/** The spells of the planet for a hero the game moves (HexHeroMagic.cpp): a view of a layer when much of it around lies unexplored, once a
	 *  day; a Dimension door to the furthest cell of the way it was just sent on that it reaches, when that way takes more than today; and
	 *  a Town portal into a castle it would go to but not reach today. Each returns true if it cast */
	bool AiCastViews(int32 Id);
	bool AiDoorAhead(int32 Id, const HexHeroPath::FField& Ways);

	/** A hero the game moves that explores no cell today on foot jumps by a spell that steps (Teleport, Dimension door) over the forest or the
	 *  mountains around it, to the cell it may land on nearest ground its player has not explored (Near, steps by node), which its feet do
	 *  not reach today, as the developer asked on 2026-11-15 that the game leave or enter a closed place so */
	bool AiJumpTo(int32 Id, const HexHeroPath::FField& Ways, const TArray<int8>& Near);
	bool AiPortalInto(int32 Id, int32 CastleId, float WayCost);

	/** The index of the spell of a kind of the planet that a hero knows, INDEX_NONE for none */
	int32 FindAdventureSpell(const FHexHero& Hero, EHexAdventureSpell What) const;

	/** Puts a hero on a cell of a layer at once, out of any castle, its way gone, and lets it see around it, as a spell of the planet does */
	void Teleport(FHexHero& Hero, EHexPlanetLayer Layer, int32 Cell);

public:

	/** The next hero is not chosen while the person looks at a panel over the map, as the panel of a castle a hero that ran out of steps went
	 *  into, which choosing another would close (AHexPlanetPlayerController) */
	void HoldPendingNext(bool bHold) { bHoldNext = bHold; }

	/** The hero of a player the game moves that leads its army: the strongest, which alone takes the creatures of its castles, so the army is
	 *  not spread over its heroes, while the others gather and explore. 0 for none */
	int32 GetAiMainHero(int32 Player) const;

	/** The hero of such a player that brings its leader the armies of its castles, as the developer asked on 2026-11-14 ("щоб герої слабші
	 *  підносили армії для основного - щоб той не вертався постійно"): the first after the leader. 0 for none */
	int32 GetAiCarrier(int32 Player) const;
private:

	/** The carrier takes an army from a castle of its own and brings it to the leader, whose army takes the strongest stacks of both, when it
	 *  is worth AiCarryShare of the army of the leader. Returns true if it went */
	bool MoveAiCarry(int32 Id, const HexHeroPath::FField& Ways);

	/** Battles weighed in advance this turn, by the hero, what it fights and the power of its army, so a hero does not fight them again with
	 *  each step it takes */
	TMap<FString, FVector2f> AiForecasts;

	/** A hero that stands beside the site it was sent to visits it, paying as for a step. Returns false and why not */
	bool VisitSite(FHexHero& Hero, FString& OutMessage);

	/** The next visit costs nothing, as when the guard of a mine was beaten and the hero goes onto it (UHexBattles) */
	bool bNextVisitFree = false;

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

	/** The hero a person aims a spell of the planet for, whose cells the reach shows meanwhile (SetAimCells), 0 for none */
	int32 AimHeroId = 0;
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

	/** What waits for the person to answer, and the stacks that offered to join and were sent away, which offer no more this game */
	FHexHeroOffer PendingOffer;
	TSet<int32> RefusedJoins;

	/** The neutral stack of an offer joins its hero, or is fought */
	bool JoinNeutral(FString& OutMessage);
	bool FightNeutral(FString& OutMessage);

	/** The skills a level offers a hero: one a level higher of those it has and one it lacks, while it has room, by the weights of its class */
	TArray<FHexHeroSkill> MakeLevelOffers(const FHexHero& Hero, FRandomStream& Random) const;

	/** Gives a hero the skill of an offer, a new one at Basic or one of its own a level higher */
	void ApplySkill(FHexHero& Hero, const FHexHeroSkill& Offer) const;

	/** Mana back at the start of a day: ManaPerDay and Mysticism, all of it in a castle of its own with a mage guild */
	void RestoreMana(FHexHero& Hero) const;

	/** Adds what an artifact gives to the attributes of a hero, or takes it off with a Sign of -1, as it puts it on or takes it off */
	void Wear(FHexHero& Hero, int32 Artifact, int32 Sign) const;

	/** A hero goes aboard the boat of a cell, or ashore from its boat, which then lies on the cell of the sea it left */
	void Board(FHexHero& Hero, int32 BoatCell);
	void GoAshore(FHexHero& Hero);

	/** The shipyard of an offer builds its boat, for what it costs */
	bool BuildBoat(const FHexHeroOffer& Offer, FString& OutMessage);

	/** A shipyard beside a hero offers to build a boat: the cell of the sea beside it a boat would lie on, INDEX_NONE for none */
	int32 FindBoatCell(EHexPlanetLayer Layer, int32 ShipyardCell) const;

	/** The boat a hero sails in, drawn where it goes, by its Id */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UInstancedStaticMeshComponent>> BoatBodies;

	/** Draws the boat a hero sails in under it, or hides it */
	void UpdateBoatBody(const FHexHero& Hero, bool bShown);

	/** The rings of foam the boats leave behind them on each layer (WakeSize and the rest of DA_HexHeroes), given out in turn, and when each
	 *  hero's boat laid its last */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Wakes[2];
	int32 WakeNext[2] = {0, 0};
	TMap<int32, double> WakeTimes;

	/** Lays a ring of foam behind the boat of a hero that sails, every WakeInterval */
	void DropWake(const FHexHero& Hero, AHexPlanet& Planet, const FVector3d& Where, const FVector3d& Up, const FVector3d& Facing, double Time);

	/** Returns the name a new hero of a class takes */
	FString MakeName(int32 Class, int32 Id) const;

	/** Puts every hero of a layer on its ground again, as it was built or rebuilt */
	void OnLayerRebuilt(EHexPlanetLayer Layer);

	FLayerMarks& GetMarks(EHexPlanetLayer Layer) { return Marks[int32(Layer)]; }

	/** Returns the point of the ground of a cell, or of the other cell where the first has none there, in a direction from the center, Lift
	 *  above it; the top of the cell where neither has ground there */
	FVector3d FindGroundPoint(const AHexPlanet& Planet, int32 Cell, int32 OtherCell, const FVector3d& Direction, double Lift);

	/** The radius of the water a mark over the sea lies on, the sea of the surface, else 0 */
	static double GetWaterRadius(const AHexPlanet& Planet);

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
