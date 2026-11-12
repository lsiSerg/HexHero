// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexCastleConfig.h"
#include "HexCoord.h"

class UHexBattleConfig;
struct FHexSpell;

/** One side of a battle as it comes onto the field: its player, its hero if it has one, and the stacks it brings */
struct FHexBattleSideSetup
{
	/** The player, or -1 for the neutral creatures of the map */
	int32 Player = -1;

	/** The hero that leads it, 0 for none, and what of it counts in a battle */
	int32 HeroId = 0;
	FString HeroName;
	int32 Attack = 0;
	int32 Defense = 0;
	int32 SpellPower = 0;
	int32 Mana = 0;

	/** The stacks, as the army holds them; an empty stack is left out */
	TArray<FHexStack> Stacks;

	/** The game fights for this side */
	bool bAi = true;

	/** The Defense of its stacks is multiplied by this, by the fort of a castle it defends */
	float DefenseFactor = 1.0f;

	/** What the side is called when it has no hero */
	FString Name;
};

/**
 *  What a castle that is fought for brings onto the field, as the developer described on 2026-11-12: a wall across the field before the
 *  defender, with a gate in its middle and towers by the level of its fort, each section of it WallHealth strong, the gate GateHealth and a
 *  tower TowerHealth; shooters in each tower that are part of the castle; and the catapult of the attacker, if its hero bought one
 */
struct FHexBattleSiege
{
	bool bWalls = false;
	int32 WallHealth = 0;
	int32 GateHealth = 0;
	int32 TowerHealth = 0;
	int32 Towers = 0;
	/** The shooters in each tower: a creature of the faction of the castle and how many */
	FHexStack TowerStack;
	bool bCatapult = false;
};

/** Both sides and the seed of a battle: the attacker first */
struct FHexBattleSetup
{
	FHexBattleSideSetup Sides[2];
	uint32 Seed = 1;
	/** Obstacles are put on the field */
	bool bObstacles = true;
	FHexBattleSiege Siege;
};

/** A part of the wall of a castle on the field: a section of it, its gate or a tower, standing while it has health; one that fell is rubble,
 *  which any unit walks over */
enum class EHexWallPart : uint8
{
	Wall,
	Gate,
	Tower,
};

struct FHexBattleWall
{
	FHexCoord Hex;
	EHexWallPart Part = EHexWallPart::Wall;
	int32 Health = 0;
	int32 MaxHealth = 0;
	/** The shooters of a tower, by index, or INDEX_NONE */
	int32 Unit = INDEX_NONE;
	bool IsStanding() const { return Health > 0; }
};

/** A spell on a stack, for so many rounds more */
struct FHexBattleEffect
{
	int32 Spell = INDEX_NONE;
	int32 RoundsLeft = 0;
};

/** A stack on the field */
struct FHexBattleUnit
{
	int32 Side = 0;
	/** Its place in the army of its side (FHexBattleSideSetup::Stacks), by which what is left of it goes back */
	int32 Slot = 0;
	FHexStack Kind;
	FName CreatureId;
	int32 Level = 0;
	FHexCreatureStats Stats;
	bool bShoots = false;
	bool bFlies = false;

	int32 Count = 0;
	int32 StartCount = 0;
	/** The health of its creature on top, the one struck first */
	int32 TopHealth = 0;
	FHexCoord Hex;
	int32 ShotsLeft = 0;
	int32 RetaliationsLeft = 1;

	/** Its turn this round: it waited, it defends until its next turn, it acted, it took another turn of morale */
	bool bWaited = false;
	bool bDefending = false;
	bool bDone = false;
	bool bMoraleUsed = false;

	int32 Morale = 0;
	int32 Luck = 0;
	TArray<FHexBattleEffect> Effects;

	/** A machine of a siege: the shooters of a tower, which stay in it and only a catapult harms, or the catapult of the attacker, which
	 *  shoots at the walls; neither counts for who wins, nor goes back into an army */
	bool bTower = false;
	bool bCatapult = false;
	bool IsMachine() const { return bTower || bCatapult; }

	bool IsAlive() const { return Count > 0; }
	int32 GetTotalHealth() const { return Count > 0 ? (Count - 1) * Stats.Health + TopHealth : 0; }
	bool Has(EHexAbility Ability) const { return Stats.HasAbility(Ability); }
};

/** What happened in a battle, for the field to show and the log to tell, in order */
struct FHexBattleEvent
{
	enum class EType : uint8
	{
		RoundBegins,
		TurnBegins,
		Move,
		Strike,
		Shoot,
		Retaliate,
		Damage,
		Death,
		Wait,
		Defend,
		Spell,
		Heal,
		Morale,
		BadMorale,
		Luck,
		Regenerate,
		ManaDrain,
		Retreat,
		End,
		/** A part of the wall is struck or shot (Hex, Amount the damage; Killed 1 when it falls), by a unit or the catapult (Unit) */
		WallHit,
		/** The catapult shoots at a part of the wall (Hex), Amount 0 when it misses */
		CatapultShot,
	};

	EType Type = EType::TurnBegins;
	/** The unit that acts and the one it acts on, by index, or INDEX_NONE */
	int32 Unit = INDEX_NONE;
	int32 Target = INDEX_NONE;
	/** The hexes it walks or flies through, from where it stood */
	TArray<FHexCoord> Path;
	FHexCoord Hex;
	int32 Amount = 0;
	int32 Killed = 0;
	/** How many creatures the unit acted on has after it, for the field to show as it plays the events; -1 when it does not change */
	int32 CountAfter = -1;
	int32 Spell = INDEX_NONE;
	int32 Side = INDEX_NONE;
	FString Text;
};

/** What a strike would do, for the person to see before it strikes */
struct FHexStrikePreview
{
	int32 DamageMin = 0;
	int32 DamageMax = 0;
	int32 KillsMin = 0;
	int32 KillsMax = 0;
	/** Whether the one struck strikes back, as it would after the least and the most damage, with what it would deal and kill then */
	bool bStrikesBack = false;
	int32 BackDamageMin = 0;
	int32 BackDamageMax = 0;
	int32 BackKillsMin = 0;
	int32 BackKillsMax = 0;
	/** A shot past ShotFarHexes, at half its damage, and one of a shooter in melee */
	bool bFar = false;
	bool bMeleePenalty = false;
};

/** What a spell of a hero would do at a hex: the damage or healing it deals each unit there and the creatures it kills */
struct FHexSpellPreview
{
	int32 Damage = 0;
	int32 Heal = 0;
	int32 Kills = 0;
	int32 Units = 0;
};

/**
 *  A battle, the rules alone, with no UObject (the HexHero.Battle tests): as the developer decided on 2026-11-12, two sides on a field of
 *  Columns x Rows pointy-top hexes of DA_HexBattle, the attacker on the first column and the defender on the last, with obstacles between.
 *  It is fought in rounds: each round the stacks act from the fastest to the slowest whoever owns them, the attacker first at equal speed,
 *  and those that waited act after all the others, the slowest first. A stack walks up to its speed in hexes around obstacles and other
 *  stacks, or flies over them; strikes a stack beside it, which strikes back once a round; shoots, unless an enemy stands beside it; waits;
 *  or defends. Morale gives a stack another turn after its turn or takes its turn, luck doubles a strike, the abilities of the creatures
 *  (EHexAbility) change all this, and the hero of a side casts one spell a round for its mana. A side whose stacks are all dead, or that
 *  retreats, loses. Every chance comes from the seed, so a battle is the same from the same orders.
 *
 *  The damage of a strike is that of each creature, a roll between its least and its most, times their number (ten rolls at most, scaled),
 *  more by each point of Attack above the Defense of the one struck and less by each point below (AttackBonusPerPoint, DefenseCutPerPoint),
 *  each of the two with the Attack and Defense of its hero. All the numbers are Claude's, in DA_HexBattle
 */
class FHexBattle
{
public:

	/** Puts the stacks of both sides and the obstacles on the field and begins the first round */
	void Start(const FHexBattleSetup& InSetup, const UHexCastleConfig* InCastles, const UHexBattleConfig* InConfig);

	int32 GetColumns() const { return Columns; }
	int32 GetRows() const { return Rows; }

	/** Returns true if a hex lies on the field */
	bool IsOnField(const FHexCoord& Hex) const;

	bool IsObstacle(const FHexCoord& Hex) const { return Obstacles.Contains(Hex); }
	const TSet<FHexCoord>& GetObstacles() const { return Obstacles; }

	/** Returns the living unit on a hex, or INDEX_NONE */
	int32 FindUnitAt(const FHexCoord& Hex) const;

	const TArray<FHexBattleUnit>& GetUnits() const { return Units; }
	const FHexBattleSideSetup& GetSide(int32 Side) const { return Setup.Sides[Side]; }
	int32 GetMana(int32 Side) const { return Mana[Side]; }
	bool HasCast(int32 Side) const { return bCast[Side]; }

	int32 GetRound() const { return Round; }

	/** The unit whose turn it is, or INDEX_NONE once the battle is over */
	int32 GetActive() const { return Active; }

	/** Returns the units that act next, the one whose turn it is first, on into the next round, at most Max */
	TArray<int32> GetQueue(int32 Max) const;

	bool IsOver() const { return Winner != INDEX_NONE; }

	/** The side that won, 0 the attacker and 1 the defender, or INDEX_NONE while it goes on; and the side that retreated, or INDEX_NONE */
	int32 GetWinner() const { return Winner; }
	int32 GetRetreated() const { return Retreated; }

	/** The speed, Attack and Defense of a unit now, with its spells and its hero */
	int32 GetSpeed(const FHexBattleUnit& Unit) const;
	int32 GetAttack(const FHexBattleUnit& Unit) const;
	int32 GetDefense(const FHexBattleUnit& Unit) const;

	/** The hexes a unit can walk or fly to this turn, each with how many hexes it runs to get there, and the way there for one that walks */
	void FindReach(int32 UnitIndex, TMap<FHexCoord, int32>& OutSteps, TMap<FHexCoord, FHexCoord>* OutFrom = nullptr) const;

	/** Returns the hexes a unit goes through to a hex it can reach, from where it stands, or an empty list */
	TArray<FHexCoord> FindPath(int32 UnitIndex, const FHexCoord& To) const;

	/** Returns true if the unit whose turn it is can shoot that unit now */
	bool CanShoot(int32 UnitIndex, int32 Target) const;

	/** Returns true if the unit whose turn it is can strike that unit from that hex, beside it, which it reaches this turn */
	bool CanStrike(int32 UnitIndex, int32 Target, const FHexCoord& From) const;

	/** What a strike or a shot of the unit whose turn it is at a unit would deal, running so many hexes first */
	FHexStrikePreview PreviewStrike(int32 UnitIndex, int32 Target, bool bShot, int32 HexesRun) const;

	/** What a spell of the hero of a side would do at a hex */
	FHexSpellPreview PreviewSpell(int32 Side, int32 Spell, const FHexCoord& Hex) const;

	/** The wall of a castle on the field, empty without one, and the part of it on a hex, or null */
	const TArray<FHexBattleWall>& GetWalls() const { return Walls; }
	const FHexBattleWall* FindWall(const FHexCoord& Hex) const;

	/** Whether the gate stays open whoever comes, as a unit was killed in it or it fell */
	bool IsGateJammed() const { return bGateJammed; }

	/** Whether a unit of a side can go onto a hex: on the field, no obstacle, no unit, and no part of the wall standing there, but the gate for
	 *  the defender, through which its units go out and in, and for anyone once it is jammed */
	bool IsPassable(const FHexCoord& Hex, int32 Side) const;

	/** The unit whose turn it is strikes a standing part of the wall beside a hex it reaches, as the developer asked for units that strike
	 *  the walls; only a unit of the attacker that fights in melee, and none strikes back */
	bool CanStrikeWall(int32 UnitIndex, const FHexCoord& WallHex, const FHexCoord& From) const;
	bool StrikeWall(const FHexCoord& WallHex, const FHexCoord& From);

	/** The catapult, whose turn it is, shoots at a standing part of the wall anywhere on the field */
	bool CanFireCatapult(const FHexCoord& WallHex) const;
	bool FireCatapult(const FHexCoord& WallHex);

	/** The least and the most damage a strike of a unit at the wall deals */
	void PreviewWallStrike(int32 UnitIndex, int32& OutMin, int32& OutMax) const;

	/** The hero of a side can cast a spell at a hex: it has one, the spell, its mana, it has not cast this round, it is the turn of a unit of
	 *  its own, and the hex holds what the spell takes */
	bool CanCast(int32 Side, int32 Spell, const FHexCoord& Hex) const;
	bool CanCastAny(int32 Side) const;

	/** The orders for the unit whose turn it is; each returns false and changes nothing when it cannot be given */
	bool Move(const FHexCoord& To);
	bool Strike(int32 Target, const FHexCoord& From);
	bool Shoot(int32 Target);
	bool Wait();
	bool Defend();

	/** The hero of a side casts a spell; the unit whose turn it is still has its turn */
	bool Cast(int32 Side, int32 Spell, const FHexCoord& Hex);

	/** A side leaves the field and loses, its stacks lost with it */
	void Retreat(int32 Side);

	/** The game gives the order for the unit whose turn it is, and its hero casts first when that is worth it */
	void PlayAi();

	/** Plays the battle to its end, the game giving every order */
	void PlayToEnd();

	/** What happened since the last call, in order */
	TArray<FHexBattleEvent> TakeEvents() { return MoveTemp(Events); }

	/** What is left of the army of a side, by its stacks, the empty ones left out; none for a side that retreated */
	TArray<FHexStack> GetSurvivors(int32 Side) const;

	/** How many are left of a stack of the army of a side, by its place in it; 0 for a side that retreated */
	int32 GetSlotCount(int32 Side, int32 Slot) const;

	/** The health of the creatures a side killed, by their own stats */
	int32 GetKilledHealth(int32 Side) const;

	/** The power of the units of a side alive now (FHexCreatureStats::GetPower), by which the game weighs */
	float GetSidePower(int32 Side) const;

	const UHexBattleConfig* GetConfig() const { return Config; }

private:

	const UHexCastleConfig* Castles = nullptr;
	const UHexBattleConfig* Config = nullptr;
	FHexBattleSetup Setup;
	FRandomStream Random;
	int32 Columns = 15;
	int32 Rows = 11;
	TSet<FHexCoord> Obstacles;
	TArray<FHexBattleWall> Walls;
	bool bGateJammed = false;
	TArray<FHexBattleUnit> Units;
	TArray<FHexBattleEvent> Events;
	int32 Mana[2] = {0, 0};
	bool bCast[2] = {false, false};
	int32 Round = 0;
	int32 Active = INDEX_NONE;
	int32 Winner = INDEX_NONE;
	int32 Retreated = INDEX_NONE;

	const FHexSpell* GetSpell(int32 Spell) const;

	/** Puts the obstacles on the field, keeping a way from the first column to the last */
	void PlaceObstacles();

	/** Puts the wall of a castle, its towers with their shooters and the catapult of the attacker on the field */
	void PlaceSiege();

	/** Deals damage to a part of the wall: one that falls is rubble, and the shooters of a tower die with it */
	void DamageWall(int32 Attacker, int32 WallIndex, int32 Damage);

	/** The game: the part of the wall a catapult shoots at, or one a stack beside it strikes, or false */
	bool ChooseCatapultTarget(FHexCoord& OutHex) const;
	bool ChooseWallStrike(int32 UnitIndex, const TMap<FHexCoord, int32>& Steps, FHexCoord& OutWall, FHexCoord& OutFrom) const;

	/** The units in the order they act in the rest of this round, and in a new one */
	TArray<int32> GetRoundOrder(bool bNewRound) const;

	void BeginRound();

	/** The turn of the unit whose turn it was is over: another of morale, or the next unit, or a new round */
	void EndTurn();

	/** The next unit has its turn: what happens at the start of it, which may take it */
	void BeginTurn(int32 UnitIndex);

	/** Ends the battle when a side has no unit left */
	bool CheckEnd();

	/** Rolls the damage of a strike or a shot of one unit at another */
	int32 RollDamage(const FHexBattleUnit& Attacker, const FHexBattleUnit& Target, bool bShot, int32 HexesRun, bool bRetaliation, bool& bOutLucky);

	/** The share by which the damage of an attacker against a target is changed by their Attack and Defense and the rest */
	float GetDamageFactor(const FHexBattleUnit& Attacker, const FHexBattleUnit& Target, bool bShot, int32 HexesRun) const;

	/** Deals damage to a unit, which may kill some of it, and tells it; returns the creatures killed */
	int32 DealDamage(int32 Attacker, int32 Target, int32 Damage);

	/** One strike of a unit at another beside it, with the breath, the drain and what else goes with it */
	void DoStrike(int32 Attacker, int32 Target, int32 HexesRun, bool bRetaliation);

	/** One shot, with what it hits around */
	void DoShot(int32 Attacker, int32 Target);

	/** Walks or flies the unit whose turn it is to a hex, returning how many hexes it ran */
	int32 DoMove(const FHexCoord& To);

	bool HasEnemyBeside(int32 UnitIndex) const;
	int32 GetHexDirection(const FHexCoord& From, const FHexCoord& To) const;

	/** What a unit is worth to the game: its power for each creature of it */
	float GetWorth(const FHexBattleUnit& Unit, int32 Creatures) const;

	/** The game: the spell worth most for a side now, and its target, or false */
	bool ChooseSpell(int32 Side, int32& OutSpell, FHexCoord& OutHex) const;
	float RateSpell(int32 Side, int32 Spell, const FHexCoord& Hex) const;

	void AddEvent(FHexBattleEvent::EType Type, int32 Unit = INDEX_NONE, int32 Target = INDEX_NONE, int32 Amount = 0, int32 Killed = 0);
};
