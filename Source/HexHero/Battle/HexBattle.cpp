// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexBattle.h"
#include "HexBattleConfig.h"

namespace
{
	/** The rows the stacks of a side stand on: spread over the field as evenly as they fit */
	TArray<int32> GetStartRows(int32 Count, int32 Rows)
	{
		TArray<int32> Found;
		if (Count <= 0)
		{
			return Found;
		}
		// seven on eleven rows as 0 2 4 5 6 8 10, so a full army fills the edge with gaps
		if (Count == 7 && Rows == 11)
		{
			return {0, 2, 4, 5, 6, 8, 10};
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			int32 Row = FMath::Clamp(FMath::RoundToInt32((Index + 0.5) * Rows / double(Count) - 0.5), 0, Rows - 1);
			while (Found.Contains(Row) && Row < Rows - 1)
			{
				++Row;
			}
			Found.Add(Row);
		}
		return Found;
	}
}

bool FHexBattle::IsOnField(const FHexCoord& Hex) const
{
	return Hex.GetRow() >= 0 && Hex.GetRow() < Rows && Hex.GetColumn() >= 0 && Hex.GetColumn() < Columns;
}

int32 FHexBattle::FindUnitAt(const FHexCoord& Hex) const
{
	return Units.IndexOfByPredicate([&Hex](const FHexBattleUnit& Each) { return Each.IsAlive() && Each.Hex == Hex; });
}

const FHexSpell* FHexBattle::GetSpell(int32 Spell) const
{
	return Config && Config->Spells.IsValidIndex(Spell) ? &Config->Spells[Spell] : nullptr;
}

void FHexBattle::AddEvent(FHexBattleEvent::EType Type, int32 Unit, int32 Target, int32 Amount, int32 Killed)
{
	FHexBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Type = Type;
	Event.Unit = Unit;
	Event.Target = Target;
	Event.Amount = Amount;
	Event.Killed = Killed;
	if (Units.IsValidIndex(Unit))
	{
		Event.Hex = Units[Unit].Hex;
		Event.Side = Units[Unit].Side;
	}
}

void FHexBattle::Start(const FHexBattleSetup& InSetup, const UHexCastleConfig* InCastles, const UHexBattleConfig* InConfig)
{
	Castles = InCastles;
	Config = InConfig;
	Setup = InSetup;
	Random.Initialize(int32(Setup.Seed & 0x7fffffff));
	Columns = Config ? Config->Columns : 15;
	Rows = Config ? Config->Rows : 11;
	Units.Reset();
	Events.Reset();
	Obstacles.Reset();
	Walls.Reset();
	bGateJammed = false;
	Round = 0;
	Active = INDEX_NONE;
	Winner = INDEX_NONE;
	Retreated = INDEX_NONE;

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FHexBattleSideSetup& Each = Setup.Sides[Side];
		Mana[Side] = Each.HeroId != 0 ? FMath::Max(Each.Mana, 0) : 0;
		bCast[Side] = false;
		// the morale of an army: one of a single faction is steadier, one of many less so
		TSet<int32> Factions;
		for (const FHexStack& Stack : Each.Stacks)
		{
			const FHexCreatureStats* Stats = Castles ? Castles->GetStats(Stack) : nullptr;
			if (Stack.Count > 0 && Stats && !Stats->HasAbility(EHexAbility::Undead))
			{
				Factions.Add(Stack.Faction);
			}
		}
		const int32 ArmyMorale = Factions.Num() <= 1 ? (Config ? Config->OneFactionMorale : 1) : (Config ? Config->MixedFactionMorale : -1) * (Factions.Num() - 1);

		int32 Placed = 0;
		for (const FHexStack& Stack : Each.Stacks)
		{
			Placed += Stack.Count > 0 && Castles && Castles->GetStats(Stack) ? 1 : 0;
		}
		// past as many stacks as there are rows, a second column
		const int32 PerColumn = FMath::Min(Placed, Rows);
		const TArray<int32> StartRows = GetStartRows(PerColumn, Rows);
		const TArray<int32> MoreRows = GetStartRows(Placed - PerColumn, Rows);
		int32 Placing = 0;
		for (int32 Slot = 0; Slot < Each.Stacks.Num(); ++Slot)
		{
			const FHexStack& Stack = Each.Stacks[Slot];
			const FHexCreatureStats* Stats = Castles ? Castles->GetStats(Stack) : nullptr;
			if (Stack.Count <= 0 || !Stats || Placing >= Rows * 2)
			{
				continue;
			}
			const FHexCreature& Creature = Castles->Factions[Stack.Faction].Creatures[Stack.Creature];
			FHexBattleUnit& Unit = Units.AddDefaulted_GetRef();
			Unit.Side = Side;
			Unit.Slot = Slot;
			Unit.Kind = Stack;
			Unit.CreatureId = Creature.Id;
			Unit.Level = Stack.Creature;
			Unit.Stats = *Stats;
			Unit.Stats.Health = FMath::Max(Unit.Stats.Health, 1);
			Unit.Stats.DamageMax = FMath::Max(Unit.Stats.DamageMax, Unit.Stats.DamageMin);
			Unit.bShoots = Creature.bShoots;
			Unit.bFlies = Creature.bFlies;
			Unit.Count = Unit.StartCount = Stack.Count;
			Unit.TopHealth = Unit.Stats.Health;
			Unit.ShotsLeft = Unit.bShoots ? FMath::Max(Stats->Shots, 1) : 0;
			Unit.Morale = Unit.Has(EHexAbility::Undead) ? 0 : FMath::Clamp(ArmyMorale + Stats->Morale, -3, 3);
			Unit.Luck = FMath::Clamp(Stats->Luck, -3, 3);
			const bool bSecond = Placing >= PerColumn;
			const int32 Row = bSecond ? MoreRows[Placing - PerColumn] : StartRows[Placing];
			const int32 Column = Side == 0 ? (bSecond ? 1 : 0) : (bSecond ? Columns - 2 : Columns - 1);
			Unit.Hex = FHexCoord::FromOffset(Column, Row);
			++Placing;
		}
	}
	if (Setup.Siege.bWalls || Setup.Siege.bCatapult)
	{
		PlaceSiege();
	}
	if (Setup.bObstacles)
	{
		PlaceObstacles();
	}
	BeginRound();
}

void FHexBattle::PlaceSiege()
{
	const FHexBattleSiege& Siege = Setup.Siege;
	if (Siege.bWalls)
	{
		// across every row, the gate in the middle one and the towers on their rows
		const int32 Column = FMath::Clamp(Columns - 1 - (Config ? Config->SiegeWallFromEnd : 4), 2, Columns - 2);
		TSet<int32> TowerRows;
		for (int32 Index = 0; Config && Index < FMath::Min(Siege.Towers, Config->TowerRows.Num()); ++Index)
		{
			TowerRows.Add(FMath::Clamp(Config->TowerRows[Index], 0, Rows - 1));
		}
		const FHexCreatureStats* TowerStats = Castles && Siege.TowerStack.Count > 0 ? Castles->GetStats(Siege.TowerStack) : nullptr;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			FHexBattleWall& Wall = Walls.AddDefaulted_GetRef();
			Wall.Hex = FHexCoord::FromOffset(Column, Row);
			Wall.Part = Row == Rows / 2 ? EHexWallPart::Gate : TowerRows.Contains(Row) ? EHexWallPart::Tower : EHexWallPart::Wall;
			Wall.Health = Wall.MaxHealth = FMath::Max(Wall.Part == EHexWallPart::Gate ? Siege.GateHealth : Wall.Part == EHexWallPart::Tower ? Siege.TowerHealth : Siege.WallHealth, 1);
			// the shooters of a tower stand in it, part of the castle, with shots that never run out
			if (Wall.Part == EHexWallPart::Tower && TowerStats)
			{
				const FHexCreature& Creature = Castles->Factions[Siege.TowerStack.Faction].Creatures[Siege.TowerStack.Creature];
				FHexBattleUnit& Unit = Units.AddDefaulted_GetRef();
				Unit.Side = 1;
				Unit.Slot = INDEX_NONE;
				Unit.Kind = Siege.TowerStack;
				Unit.CreatureId = Creature.Id;
				Unit.Level = Siege.TowerStack.Creature;
				Unit.Stats = *TowerStats;
				Unit.Stats.Health = FMath::Max(Unit.Stats.Health, 1);
				Unit.Stats.DamageMax = FMath::Max(Unit.Stats.DamageMax, Unit.Stats.DamageMin);
				Unit.bShoots = true;
				Unit.Count = Unit.StartCount = Siege.TowerStack.Count;
				Unit.TopHealth = Unit.Stats.Health;
				Unit.ShotsLeft = 999;
				Unit.bTower = true;
				Unit.RetaliationsLeft = 0;
				Unit.Hex = Wall.Hex;
				Wall.Unit = Units.Num() - 1;
			}
		}
	}
	if (Siege.bCatapult)
	{
		// in a corner behind the attacker, on the first free hex of the first two columns from the bottom
		FHexCoord Place = FHexCoord::FromOffset(0, Rows - 1);
		for (int32 Each = 0; Each < Rows * 2; ++Each)
		{
			const FHexCoord Hex = FHexCoord::FromOffset(Each / Rows, Rows - 1 - Each % Rows);
			if (FindUnitAt(Hex) == INDEX_NONE)
			{
				Place = Hex;
				break;
			}
		}
		FHexBattleUnit& Unit = Units.AddDefaulted_GetRef();
		Unit.Side = 0;
		Unit.Slot = INDEX_NONE;
		Unit.CreatureId = TEXT("Catapult");
		Unit.Stats.DisplayName = TEXT("Catapult");
		Unit.Stats.Health = Config ? Config->CatapultHealth : 400;
		Unit.Stats.Speed = Config ? Config->CatapultSpeed : 6;
		Unit.Stats.DamageMin = Config ? Config->CatapultDamageMin : 60;
		Unit.Stats.DamageMax = Config ? Config->CatapultDamageMax : 110;
		Unit.Count = Unit.StartCount = 1;
		Unit.TopHealth = Unit.Stats.Health;
		Unit.bCatapult = true;
		Unit.RetaliationsLeft = 0;
		Unit.Hex = Place;
	}
}

const FHexBattleWall* FHexBattle::FindWall(const FHexCoord& Hex) const
{
	return Walls.FindByPredicate([&Hex](const FHexBattleWall& Each) { return Each.Hex == Hex; });
}

bool FHexBattle::IsPassable(const FHexCoord& Hex, int32 Side) const
{
	if (!IsOnField(Hex) || Obstacles.Contains(Hex) || FindUnitAt(Hex) != INDEX_NONE)
	{
		return false;
	}
	const FHexBattleWall* Wall = FindWall(Hex);
	if (!Wall || !Wall->IsStanding())
	{
		return true;
	}
	// the gate opens for the defender, and for anyone once it is jammed
	return Wall->Part == EHexWallPart::Gate && (Side == 1 || bGateJammed);
}

bool FHexBattle::CanStrikeWall(int32 UnitIndex, const FHexCoord& WallHex, const FHexCoord& From) const
{
	const FHexBattleWall* Wall = FindWall(WallHex);
	if (IsOver() || UnitIndex != Active || !Units.IsValidIndex(UnitIndex) || !Wall || !Wall->IsStanding() || From.DistanceTo(WallHex) != 1)
	{
		return false;
	}
	const FHexBattleUnit& Unit = Units[UnitIndex];
	if (Unit.Side != 0 || Unit.IsMachine() || Unit.bShoots)
	{
		return false;
	}
	TMap<FHexCoord, int32> Steps;
	FindReach(UnitIndex, Steps);
	return Steps.Contains(From);
}

void FHexBattle::PreviewWallStrike(int32 UnitIndex, int32& OutMin, int32& OutMax) const
{
	OutMin = OutMax = 0;
	if (!Units.IsValidIndex(UnitIndex))
	{
		return;
	}
	const FHexBattleUnit& Unit = Units[UnitIndex];
	const float Share = Config ? Config->WallStrikeShare : 0.35f;
	const int32 Strikes = Unit.Has(EHexAbility::DoubleStrike) ? 2 : 1;
	OutMin = FMath::Max(FMath::FloorToInt32(Unit.Stats.DamageMin * Unit.Count * Share), 1) * Strikes;
	OutMax = FMath::Max(FMath::FloorToInt32(Unit.Stats.DamageMax * Unit.Count * Share), 1) * Strikes;
}

void FHexBattle::DamageWall(int32 Attacker, int32 WallIndex, int32 Damage)
{
	FHexBattleWall& Wall = Walls[WallIndex];
	const bool bWasStanding = Wall.IsStanding();
	Wall.Health = FMath::Max(Wall.Health - Damage, 0);
	AddEvent(FHexBattleEvent::EType::WallHit, Attacker, INDEX_NONE, Damage, bWasStanding && !Wall.IsStanding() ? 1 : 0);
	Events.Last().Hex = Wall.Hex;
	if (bWasStanding && !Wall.IsStanding())
	{
		// a gate that fell stays open, and the shooters of a tower die with it
		if (Wall.Part == EHexWallPart::Gate)
		{
			bGateJammed = true;
		}
		if (Units.IsValidIndex(Wall.Unit) && Units[Wall.Unit].IsAlive())
		{
			FHexBattleUnit& Shooters = Units[Wall.Unit];
			const int32 Killed = Shooters.Count;
			const int32 Health = Shooters.GetTotalHealth();
			Shooters.Count = 0;
			Shooters.TopHealth = 0;
			AddEvent(FHexBattleEvent::EType::Damage, Attacker, Wall.Unit, Health, Killed);
			Events.Last().CountAfter = 0;
			AddEvent(FHexBattleEvent::EType::Death, Wall.Unit);
		}
	}
}

bool FHexBattle::StrikeWall(const FHexCoord& WallHex, const FHexCoord& From)
{
	if (!CanStrikeWall(Active, WallHex, From))
	{
		return false;
	}
	const int32 Attacker = Active;
	DoMove(From);
	const int32 WallIndex = Walls.IndexOfByPredicate([&WallHex](const FHexBattleWall& Each) { return Each.Hex == WallHex; });
	const int32 Strikes = Units[Attacker].Has(EHexAbility::DoubleStrike) ? 2 : 1;
	for (int32 Strike = 0; Strike < Strikes && Walls[WallIndex].IsStanding(); ++Strike)
	{
		const FHexBattleUnit& Unit = Units[Attacker];
		AddEvent(FHexBattleEvent::EType::Strike, Attacker);
		Events.Last().Hex = WallHex;
		const int32 Rolls = FMath::Clamp(Unit.Count, 1, 10);
		double Sum = 0.0;
		for (int32 Roll = 0; Roll < Rolls; ++Roll)
		{
			Sum += Random.RandRange(Unit.Stats.DamageMin, Unit.Stats.DamageMax);
		}
		DamageWall(Attacker, WallIndex, FMath::Max(FMath::FloorToInt32(Sum * Unit.Count / Rolls * (Config ? Config->WallStrikeShare : 0.35f)), 1));
	}
	EndTurn();
	return true;
}

bool FHexBattle::CanFireCatapult(const FHexCoord& WallHex) const
{
	const FHexBattleWall* Wall = FindWall(WallHex);
	return !IsOver() && Units.IsValidIndex(Active) && Units[Active].bCatapult && Wall && Wall->IsStanding();
}

bool FHexBattle::FireCatapult(const FHexCoord& WallHex)
{
	if (!CanFireCatapult(WallHex))
	{
		return false;
	}
	const int32 Catapult = Active;
	const bool bHit = Random.FRand() < (Config ? Config->CatapultHitChance : 0.8f);
	const int32 Damage = bHit ? Random.RandRange(Units[Catapult].Stats.DamageMin, Units[Catapult].Stats.DamageMax) : 0;
	AddEvent(FHexBattleEvent::EType::CatapultShot, Catapult, INDEX_NONE, Damage);
	Events.Last().Hex = WallHex;
	if (bHit)
	{
		DamageWall(Catapult, Walls.IndexOfByPredicate([&WallHex](const FHexBattleWall& Each) { return Each.Hex == WallHex; }), Damage);
	}
	EndTurn();
	return true;
}

bool FHexBattle::ChooseCatapultTarget(FHexCoord& OutHex) const
{
	// the gate first, then the towers, then the sections nearest the middle
	int32 Best = INDEX_NONE;
	int32 BestRank = TNumericLimits<int32>::Max();
	for (int32 Index = 0; Index < Walls.Num(); ++Index)
	{
		const FHexBattleWall& Wall = Walls[Index];
		if (!Wall.IsStanding())
		{
			continue;
		}
		const int32 Rank = (Wall.Part == EHexWallPart::Gate ? 0 : Wall.Part == EHexWallPart::Tower ? 1000 : 2000) + FMath::Abs(Wall.Hex.GetRow() - Rows / 2) * 10;
		if (Rank < BestRank)
		{
			BestRank = Rank;
			Best = Index;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutHex = Walls[Best].Hex;
	return true;
}

bool FHexBattle::ChooseWallStrike(int32 UnitIndex, const TMap<FHexCoord, int32>& Steps, FHexCoord& OutWall, FHexCoord& OutFrom) const
{
	// the gate before anything else, then the section of wall it reaches with the least running
	int32 BestRank = TNumericLimits<int32>::Max();
	for (const FHexBattleWall& Wall : Walls)
	{
		if (!Wall.IsStanding())
		{
			continue;
		}
		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			const FHexCoord From = Wall.Hex.GetNeighbor(Direction);
			const int32* Run = Steps.Find(From);
			if (!Run || !CanStrikeWall(UnitIndex, Wall.Hex, From))
			{
				continue;
			}
			const int32 Rank = (Wall.Part == EHexWallPart::Gate ? 0 : 100) + *Run;
			if (Rank < BestRank)
			{
				BestRank = Rank;
				OutWall = Wall.Hex;
				OutFrom = From;
			}
		}
	}
	return BestRank != TNumericLimits<int32>::Max();
}

void FHexBattle::PlaceObstacles()
{
	const int32 Min = Config ? FMath::Max(Config->ObstaclesMin, 0) : 3;
	const int32 Max = FMath::Max(Config ? Config->ObstaclesMax : 8, Min);
	const int32 Count = Random.RandRange(Min, Max);
	// a way from the first column to the last, around the obstacles alone, so no side is shut in
	const auto IsOpen = [this]()
	{
		TSet<FHexCoord> Seen;
		TArray<FHexCoord> Front;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const FHexCoord Hex = FHexCoord::FromOffset(0, Row);
			if (!Obstacles.Contains(Hex))
			{
				Seen.Add(Hex);
				Front.Add(Hex);
			}
		}
		for (int32 Index = 0; Index < Front.Num(); ++Index)
		{
			if (Front[Index].GetColumn() == Columns - 1)
			{
				return true;
			}
			for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
			{
				const FHexCoord Next = Front[Index].GetNeighbor(Direction);
				if (IsOnField(Next) && !Obstacles.Contains(Next) && !Seen.Contains(Next))
				{
					Seen.Add(Next);
					Front.Add(Next);
				}
			}
		}
		return false;
	};
	for (int32 Try = 0; Try < Count * 6 && Obstacles.Num() < Count; ++Try)
	{
		const FHexCoord Hex = FHexCoord::FromOffset(Random.RandRange(2, FMath::Max(Columns - 3, 2)), Random.RandRange(0, Rows - 1));
		// none at the wall of a castle or behind it
		if (Obstacles.Contains(Hex) || FindUnitAt(Hex) != INDEX_NONE || (!Walls.IsEmpty() && Hex.GetColumn() >= Walls[0].Hex.GetColumn() - 1))
		{
			continue;
		}
		Obstacles.Add(Hex);
		if (!IsOpen())
		{
			Obstacles.Remove(Hex);
		}
	}
}

int32 FHexBattle::GetSpeed(const FHexBattleUnit& Unit) const
{
	int32 Add = 0;
	float Share = 1.0f;
	for (const FHexBattleEffect& Effect : Unit.Effects)
	{
		if (const FHexSpell* Spell = GetSpell(Effect.Spell))
		{
			Add += Spell->SpeedAdd;
			Share *= Spell->SpeedShare;
		}
	}
	return FMath::Max(FMath::RoundToInt32((Unit.Stats.Speed + Add) * Share), 1);
}

int32 FHexBattle::GetAttack(const FHexBattleUnit& Unit) const
{
	int32 Add = Setup.Sides[Unit.Side].Attack;
	for (const FHexBattleEffect& Effect : Unit.Effects)
	{
		if (const FHexSpell* Spell = GetSpell(Effect.Spell))
		{
			Add += Spell->AttackAdd;
		}
	}
	return FMath::Max(Unit.Stats.Attack + Add, 0);
}

int32 FHexBattle::GetDefense(const FHexBattleUnit& Unit) const
{
	int32 Add = Setup.Sides[Unit.Side].Defense;
	for (const FHexBattleEffect& Effect : Unit.Effects)
	{
		if (const FHexSpell* Spell = GetSpell(Effect.Spell))
		{
			Add += Spell->DefenseAdd;
		}
	}
	float Defense = float(FMath::Max(Unit.Stats.Defense + Add, 0)) * FMath::Max(Setup.Sides[Unit.Side].DefenseFactor, 0.0f);
	if (Unit.bDefending)
	{
		Defense *= 1.0f + (Config ? Config->DefendShare : 0.3f);
	}
	return FMath::RoundToInt32(Defense);
}

TArray<int32> FHexBattle::GetRoundOrder(bool bNewRound) const
{
	TArray<int32> First;
	TArray<int32> Waited;
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		const FHexBattleUnit& Unit = Units[Index];
		if (!Unit.IsAlive() || (!bNewRound && (Unit.bDone || Index == Active)))
		{
			continue;
		}
		(Unit.bWaited && !bNewRound ? Waited : First).Add(Index);
	}
	// the fastest first, the attacker first at equal speed, then by their places in the armies
	First.Sort([this](int32 A, int32 B)
	{
		const int32 SpeedA = GetSpeed(Units[A]);
		const int32 SpeedB = GetSpeed(Units[B]);
		return SpeedA != SpeedB ? SpeedA > SpeedB : (Units[A].Side != Units[B].Side ? Units[A].Side < Units[B].Side : Units[A].Slot < Units[B].Slot);
	});
	// those that waited after them, the slowest first
	Waited.Sort([this](int32 A, int32 B)
	{
		const int32 SpeedA = GetSpeed(Units[A]);
		const int32 SpeedB = GetSpeed(Units[B]);
		return SpeedA != SpeedB ? SpeedA < SpeedB : (Units[A].Side != Units[B].Side ? Units[A].Side < Units[B].Side : Units[A].Slot < Units[B].Slot);
	});
	First.Append(Waited);
	return First;
}

TArray<int32> FHexBattle::GetQueue(int32 Max) const
{
	TArray<int32> Queue;
	if (IsOver())
	{
		return Queue;
	}
	if (Active != INDEX_NONE)
	{
		Queue.Add(Active);
	}
	Queue.Append(GetRoundOrder(false));
	while (Queue.Num() < Max)
	{
		const TArray<int32> Next = GetRoundOrder(true);
		if (Next.IsEmpty())
		{
			break;
		}
		Queue.Append(Next);
	}
	if (Queue.Num() > Max)
	{
		Queue.SetNum(Max);
	}
	return Queue;
}

void FHexBattle::BeginRound()
{
	++Round;
	for (FHexBattleUnit& Unit : Units)
	{
		Unit.bWaited = false;
		Unit.bDone = false;
		Unit.bMoraleUsed = false;
		Unit.RetaliationsLeft = Unit.Has(EHexAbility::RetaliateTwice) ? 2 : 1;
		// the spells of a round run out
		for (int32 Index = Unit.Effects.Num() - 1; Index >= 0 && Round > 1; --Index)
		{
			if (--Unit.Effects[Index].RoundsLeft <= 0)
			{
				Unit.Effects.RemoveAt(Index);
			}
		}
	}
	bCast[0] = bCast[1] = false;
	FHexBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Type = FHexBattleEvent::EType::RoundBegins;
	Event.Amount = Round;
	if (Config && Round > Config->MaxRounds)
	{
		// the defender keeps the field
		Winner = 1;
		Active = INDEX_NONE;
		AddEvent(FHexBattleEvent::EType::End, INDEX_NONE, INDEX_NONE, Winner);
		return;
	}
	const TArray<int32> Order = GetRoundOrder(true);
	if (Order.IsEmpty())
	{
		CheckEnd();
		return;
	}
	BeginTurn(Order[0]);
}

void FHexBattle::BeginTurn(int32 UnitIndex)
{
	Active = UnitIndex;
	FHexBattleUnit& Unit = Units[UnitIndex];
	Unit.bDefending = false;
	AddEvent(FHexBattleEvent::EType::TurnBegins, UnitIndex);
	if (Unit.Has(EHexAbility::Regenerate) && Unit.TopHealth < Unit.Stats.Health)
	{
		const int32 Healed = Unit.Stats.Health - Unit.TopHealth;
		Unit.TopHealth = Unit.Stats.Health;
		AddEvent(FHexBattleEvent::EType::Regenerate, UnitIndex, INDEX_NONE, Healed);
	}
	if (Unit.Has(EHexAbility::ManaDrain) && Config)
	{
		const int32 Other = 1 - Unit.Side;
		const int32 Drained = FMath::Min(Mana[Other], Config->ManaDrainPerTurn);
		if (Drained > 0)
		{
			Mana[Other] -= Drained;
			AddEvent(FHexBattleEvent::EType::ManaDrain, UnitIndex, INDEX_NONE, Drained);
		}
	}
	// bad morale takes the turn
	if (Unit.Morale < 0 && Config && Random.FRand() < -Unit.Morale * Config->BadMoraleChancePerPoint)
	{
		AddEvent(FHexBattleEvent::EType::BadMorale, UnitIndex);
		Unit.bMoraleUsed = true;
		EndTurn();
	}
}

bool FHexBattle::CheckEnd()
{
	if (IsOver())
	{
		return true;
	}
	// the machines of a siege count for nobody
	bool bAlive[2] = {false, false};
	for (const FHexBattleUnit& Unit : Units)
	{
		bAlive[Unit.Side] |= Unit.IsAlive() && !Unit.IsMachine();
	}
	if (bAlive[0] && bAlive[1])
	{
		return false;
	}
	// a side with none left loses; with none on either side the defender keeps the field
	Winner = bAlive[0] ? 0 : 1;
	Active = INDEX_NONE;
	AddEvent(FHexBattleEvent::EType::End, INDEX_NONE, INDEX_NONE, Winner);
	return true;
}

void FHexBattle::EndTurn()
{
	if (CheckEnd())
	{
		return;
	}
	if (Units.IsValidIndex(Active))
	{
		FHexBattleUnit& Unit = Units[Active];
		// good morale: another turn, once a round
		if (Unit.IsAlive() && !Unit.bMoraleUsed && !Unit.bWaited && Unit.Morale > 0 && Config && Random.FRand() < Unit.Morale * Config->MoraleChancePerPoint)
		{
			Unit.bMoraleUsed = true;
			AddEvent(FHexBattleEvent::EType::Morale, Active);
			BeginTurn(Active);
			return;
		}
		Unit.bDone = true;
	}
	const TArray<int32> Order = GetRoundOrder(false);
	if (Order.IsEmpty())
	{
		Active = INDEX_NONE;
		BeginRound();
		return;
	}
	BeginTurn(Order[0]);
}

void FHexBattle::FindReach(int32 UnitIndex, TMap<FHexCoord, int32>& OutSteps, TMap<FHexCoord, FHexCoord>* OutFrom) const
{
	OutSteps.Reset();
	if (OutFrom)
	{
		OutFrom->Reset();
	}
	if (!Units.IsValidIndex(UnitIndex) || !Units[UnitIndex].IsAlive())
	{
		return;
	}
	const FHexBattleUnit& Unit = Units[UnitIndex];
	const int32 Speed = GetSpeed(Unit);
	OutSteps.Add(Unit.Hex, 0);
	// the machines of a siege stand where they are
	if (Unit.IsMachine())
	{
		return;
	}
	const auto IsFree = [&](const FHexCoord& Hex) { return IsPassable(Hex, Unit.Side); };
	if (Unit.bFlies)
	{
		// over anything, onto a free hex within its speed
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const FHexCoord Hex = FHexCoord::FromOffset(Column, Row);
				const int32 Distance = Unit.Hex.DistanceTo(Hex);
				if (Distance > 0 && Distance <= Speed && IsFree(Hex))
				{
					OutSteps.Add(Hex, Distance);
					if (OutFrom)
					{
						OutFrom->Add(Hex, Unit.Hex);
					}
				}
			}
		}
		return;
	}
	TArray<FHexCoord> Front = {Unit.Hex};
	for (int32 Index = 0; Index < Front.Num(); ++Index)
	{
		const FHexCoord Hex = Front[Index];
		const int32 Steps = OutSteps[Hex];
		if (Steps >= Speed)
		{
			continue;
		}
		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			const FHexCoord Next = Hex.GetNeighbor(Direction);
			if (!OutSteps.Contains(Next) && IsFree(Next))
			{
				OutSteps.Add(Next, Steps + 1);
				if (OutFrom)
				{
					OutFrom->Add(Next, Hex);
				}
				Front.Add(Next);
			}
		}
	}
}

TArray<FHexCoord> FHexBattle::FindPath(int32 UnitIndex, const FHexCoord& To) const
{
	TArray<FHexCoord> Path;
	TMap<FHexCoord, int32> Steps;
	TMap<FHexCoord, FHexCoord> From;
	FindReach(UnitIndex, Steps, &From);
	if (!Steps.Contains(To))
	{
		return Path;
	}
	const FHexCoord Start = Units[UnitIndex].Hex;
	FHexCoord At = To;
	Path.Add(At);
	while (At != Start)
	{
		At = From[At];
		Path.Insert(At, 0);
	}
	return Path;
}

bool FHexBattle::HasEnemyBeside(int32 UnitIndex) const
{
	const FHexBattleUnit& Unit = Units[UnitIndex];
	for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
	{
		const int32 Other = FindUnitAt(Unit.Hex.GetNeighbor(Direction));
		if (Other != INDEX_NONE && Units[Other].Side != Unit.Side)
		{
			return true;
		}
	}
	return false;
}

int32 FHexBattle::GetHexDirection(const FHexCoord& From, const FHexCoord& To) const
{
	for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
	{
		if (From.GetNeighbor(Direction) == To)
		{
			return Direction;
		}
	}
	return INDEX_NONE;
}

bool FHexBattle::CanShoot(int32 UnitIndex, int32 Target) const
{
	// the shooters of a tower only a catapult harms, and they shoot whoever stands beside their wall
	return !IsOver() && UnitIndex == Active && Units.IsValidIndex(Target) && Units[Target].IsAlive() && Units[Target].Side != Units[UnitIndex].Side && !Units[Target].bTower
		&& Units[UnitIndex].bShoots && Units[UnitIndex].ShotsLeft > 0 && (Units[UnitIndex].bTower || !HasEnemyBeside(UnitIndex));
}

bool FHexBattle::CanStrike(int32 UnitIndex, int32 Target, const FHexCoord& From) const
{
	if (IsOver() || UnitIndex != Active || !Units.IsValidIndex(Target) || !Units[Target].IsAlive() || Units[Target].Side == Units[UnitIndex].Side
		|| From.DistanceTo(Units[Target].Hex) != 1 || Units[Target].bTower || Units[UnitIndex].IsMachine())
	{
		return false;
	}
	TMap<FHexCoord, int32> Steps;
	FindReach(UnitIndex, Steps);
	return Steps.Contains(From);
}

float FHexBattle::GetDamageFactor(const FHexBattleUnit& Attacker, const FHexBattleUnit& Target, bool bShot, int32 HexesRun) const
{
	const float BonusPerPoint = Config ? Config->AttackBonusPerPoint : 0.05f;
	const float CutPerPoint = Config ? Config->DefenseCutPerPoint : 0.025f;
	const int32 Difference = GetAttack(Attacker) - GetDefense(Target);
	float Factor = Difference >= 0 ? 1.0f + FMath::Min(Difference * BonusPerPoint, Config ? Config->MaxAttackBonus : 3.0f)
		: 1.0f - FMath::Min(-Difference * CutPerPoint, Config ? Config->MaxDefenseCut : 0.7f);
	if (bShot && Config && Attacker.Hex.DistanceTo(Target.Hex) > Config->ShotFarHexes)
	{
		Factor *= Config->ShotFarShare;
	}
	if (!bShot && Attacker.bShoots && !Attacker.Has(EHexAbility::NoMeleePenalty))
	{
		Factor *= Config ? Config->MeleePenaltyShare : 0.5f;
	}
	if (!bShot && Attacker.Has(EHexAbility::Charge) && HexesRun > 0)
	{
		Factor *= 1.0f + HexesRun * (Config ? Config->ChargeBonusPerHex : 0.05f);
	}
	for (const FHexBattleEffect& Effect : Target.Effects)
	{
		if (const FHexSpell* Spell = GetSpell(Effect.Spell))
		{
			Factor *= 1.0f - Spell->DamageTakenCut;
		}
	}
	return Factor;
}

int32 FHexBattle::RollDamage(const FHexBattleUnit& Attacker, const FHexBattleUnit& Target, bool bShot, int32 HexesRun, bool bRetaliation, bool& bOutLucky)
{
	bool bBlessed = false;
	for (const FHexBattleEffect& Effect : Attacker.Effects)
	{
		const FHexSpell* Spell = GetSpell(Effect.Spell);
		bBlessed |= Spell && Spell->bMaxDamage;
	}
	// a roll for each creature, ten at most, scaled to their number
	const int32 Rolls = FMath::Clamp(Attacker.Count, 1, 10);
	double Sum = 0.0;
	for (int32 Roll = 0; Roll < Rolls; ++Roll)
	{
		Sum += bBlessed ? Attacker.Stats.DamageMax : Random.RandRange(Attacker.Stats.DamageMin, Attacker.Stats.DamageMax);
	}
	double Damage = Sum * Attacker.Count / Rolls * GetDamageFactor(Attacker, Target, bShot, HexesRun);
	bOutLucky = Attacker.Luck > 0 && Config && Random.FRand() < Attacker.Luck * Config->LuckChancePerPoint;
	if (bOutLucky)
	{
		Damage *= 2.0;
	}
	(void)bRetaliation;
	return FMath::Max(FMath::FloorToInt32(Damage), 1);
}

int32 FHexBattle::DealDamage(int32 Attacker, int32 Target, int32 Damage)
{
	FHexBattleUnit& Unit = Units[Target];
	const int32 Before = Unit.Count;
	const int32 Total = FMath::Max(Unit.GetTotalHealth() - Damage, 0);
	Unit.Count = Total > 0 ? (Total + Unit.Stats.Health - 1) / Unit.Stats.Health : 0;
	Unit.TopHealth = Unit.Count > 0 ? Total - (Unit.Count - 1) * Unit.Stats.Health : 0;
	const int32 Killed = Before - Unit.Count;
	AddEvent(FHexBattleEvent::EType::Damage, Attacker, Target, Damage, Killed);
	Events.Last().Hex = Unit.Hex;
	Events.Last().CountAfter = Unit.Count;
	if (!Unit.IsAlive())
	{
		AddEvent(FHexBattleEvent::EType::Death, Target);
		// one killed in the gate keeps it open for good, as the developer asked on 2026-11-12
		if (const FHexBattleWall* Wall = FindWall(Unit.Hex); Wall && Wall->Part == EHexWallPart::Gate && Wall->IsStanding())
		{
			bGateJammed = true;
		}
	}
	return Killed;
}

void FHexBattle::DoStrike(int32 Attacker, int32 Target, int32 HexesRun, bool bRetaliation)
{
	if (!Units[Attacker].IsAlive() || !Units[Target].IsAlive())
	{
		return;
	}
	AddEvent(bRetaliation ? FHexBattleEvent::EType::Retaliate : FHexBattleEvent::EType::Strike, Attacker, Target);
	Events.Last().Hex = Units[Target].Hex;
	bool bLucky = false;
	const int32 Damage = RollDamage(Units[Attacker], Units[Target], false, HexesRun, bRetaliation, bLucky);
	if (bLucky)
	{
		AddEvent(FHexBattleEvent::EType::Luck, Attacker);
	}
	// the breath goes on into the stack behind, whoever it is
	const int32 Direction = GetHexDirection(Units[Attacker].Hex, Units[Target].Hex);
	const int32 Behind = Units[Attacker].Has(EHexAbility::Breath) && Direction != INDEX_NONE ? FindUnitAt(Units[Target].Hex.GetNeighbor(Direction)) : int32(INDEX_NONE);
	DealDamage(Attacker, Target, Damage);
	if (Behind != INDEX_NONE && Behind != Attacker)
	{
		bool bBehindLucky = false;
		DealDamage(Attacker, Behind, RollDamage(Units[Attacker], Units[Behind], false, HexesRun, bRetaliation, bBehindLucky));
	}
	if (Units[Attacker].Has(EHexAbility::LifeDrain))
	{
		FHexBattleUnit& Drinker = Units[Attacker];
		const int32 Most = Drinker.StartCount * Drinker.Stats.Health;
		const int32 Healed = FMath::Min(FMath::RoundToInt32(Damage * (Config ? Config->LifeDrainShare : 0.5f)), Most - Drinker.GetTotalHealth());
		if (Healed > 0)
		{
			const int32 Total = Drinker.GetTotalHealth() + Healed;
			Drinker.Count = (Total + Drinker.Stats.Health - 1) / Drinker.Stats.Health;
			Drinker.TopHealth = Total - (Drinker.Count - 1) * Drinker.Stats.Health;
			AddEvent(FHexBattleEvent::EType::Heal, Attacker, Attacker, Healed);
			Events.Last().CountAfter = Drinker.Count;
		}
	}
}

void FHexBattle::DoShot(int32 Attacker, int32 Target)
{
	FHexBattleUnit& Shooter = Units[Attacker];
	if (!Shooter.IsAlive() || !Units[Target].IsAlive() || Shooter.ShotsLeft <= 0)
	{
		return;
	}
	--Shooter.ShotsLeft;
	AddEvent(FHexBattleEvent::EType::Shoot, Attacker, Target);
	Events.Last().Hex = Units[Target].Hex;
	bool bLucky = false;
	const int32 Damage = RollDamage(Shooter, Units[Target], true, 0, false, bLucky);
	if (bLucky)
	{
		AddEvent(FHexBattleEvent::EType::Luck, Attacker);
	}
	// the shot of a mage bursts over the enemies around the stack it hits too
	TArray<int32> Around;
	if (Shooter.Has(EHexAbility::AreaShot))
	{
		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			const int32 Other = FindUnitAt(Units[Target].Hex.GetNeighbor(Direction));
			if (Other != INDEX_NONE && Units[Other].Side != Shooter.Side)
			{
				Around.Add(Other);
			}
		}
	}
	DealDamage(Attacker, Target, Damage);
	for (const int32 Other : Around)
	{
		bool bOtherLucky = false;
		DealDamage(Attacker, Other, RollDamage(Units[Attacker], Units[Other], true, 0, false, bOtherLucky));
	}
}

int32 FHexBattle::DoMove(const FHexCoord& To)
{
	FHexBattleUnit& Unit = Units[Active];
	if (Unit.Hex == To)
	{
		return 0;
	}
	const TArray<FHexCoord> Path = FindPath(Active, To);
	if (Path.IsEmpty())
	{
		return -1;
	}
	AddEvent(FHexBattleEvent::EType::Move, Active);
	Events.Last().Path = Path;
	const int32 Run = Unit.bFlies ? Unit.Hex.DistanceTo(To) : Path.Num() - 1;
	Unit.Hex = To;
	return Run;
}

bool FHexBattle::Move(const FHexCoord& To)
{
	if (IsOver() || !Units.IsValidIndex(Active) || To == Units[Active].Hex)
	{
		return false;
	}
	if (DoMove(To) < 0)
	{
		return false;
	}
	EndTurn();
	return true;
}

bool FHexBattle::Strike(int32 Target, const FHexCoord& From)
{
	if (!CanStrike(Active, Target, From))
	{
		return false;
	}
	const int32 Attacker = Active;
	const int32 Run = DoMove(From);
	DoStrike(Attacker, Target, FMath::Max(Run, 0), false);
	// it is struck back, unless what strikes allows none
	FHexBattleUnit& Struck = Units[Target];
	if (Struck.IsAlive() && !Struck.IsMachine() && Units[Attacker].IsAlive() && Struck.RetaliationsLeft > 0 && !Units[Attacker].Has(EHexAbility::NoRetaliation))
	{
		--Struck.RetaliationsLeft;
		DoStrike(Target, Attacker, 0, true);
	}
	if (Units[Attacker].Has(EHexAbility::DoubleStrike) && Units[Attacker].IsAlive() && Struck.IsAlive())
	{
		DoStrike(Attacker, Target, 0, false);
		if (Struck.IsAlive() && !Struck.IsMachine() && Units[Attacker].IsAlive() && Struck.RetaliationsLeft > 0 && !Units[Attacker].Has(EHexAbility::NoRetaliation))
		{
			--Struck.RetaliationsLeft;
			DoStrike(Target, Attacker, 0, true);
		}
	}
	EndTurn();
	return true;
}

bool FHexBattle::Shoot(int32 Target)
{
	if (!CanShoot(Active, Target))
	{
		return false;
	}
	const int32 Attacker = Active;
	DoShot(Attacker, Target);
	if (Units[Attacker].Has(EHexAbility::DoubleStrike) && Units[Target].IsAlive())
	{
		DoShot(Attacker, Target);
	}
	EndTurn();
	return true;
}

bool FHexBattle::Wait()
{
	if (IsOver() || !Units.IsValidIndex(Active) || Units[Active].bWaited)
	{
		return false;
	}
	FHexBattleUnit& Unit = Units[Active];
	Unit.bWaited = true;
	AddEvent(FHexBattleEvent::EType::Wait, Active);
	// it acts again after all the others, so its turn this round is not done
	const TArray<int32> Order = GetRoundOrder(false);
	if (Order.IsEmpty() || (Order.Num() == 1 && Order[0] == Active))
	{
		BeginTurn(Active);
		return true;
	}
	BeginTurn(Order[0]);
	return true;
}

bool FHexBattle::Defend()
{
	if (IsOver() || !Units.IsValidIndex(Active))
	{
		return false;
	}
	const int32 Defender = Active;
	AddEvent(FHexBattleEvent::EType::Defend, Defender);
	Units[Defender].bDone = true;
	EndTurn();
	// it defends until its next turn, which BeginTurn takes off
	Units[Defender].bDefending = Active != Defender;
	return true;
}

bool FHexBattle::CanCastAny(int32 Side) const
{
	if (IsOver() || !Units.IsValidIndex(Active) || Units[Active].Side != Side || Setup.Sides[Side].HeroId == 0 || bCast[Side] || !Config)
	{
		return false;
	}
	for (const FHexSpell& Spell : Config->Spells)
	{
		if (Spell.ManaCost <= Mana[Side])
		{
			return true;
		}
	}
	return false;
}

bool FHexBattle::CanCast(int32 Side, int32 Spell, const FHexCoord& Hex) const
{
	const FHexSpell* Each = GetSpell(Spell);
	if (!Each || !CanCastAny(Side) || Each->ManaCost > Mana[Side] || !IsOnField(Hex))
	{
		return false;
	}
	if (Each->Target == EHexSpellTarget::Hex)
	{
		return true;
	}
	const int32 Unit = FindUnitAt(Hex);
	return Unit != INDEX_NONE && !Units[Unit].bTower && (Units[Unit].Side == Side) == (Each->Target == EHexSpellTarget::Friend);
}

bool FHexBattle::Cast(int32 Side, int32 SpellIndex, const FHexCoord& Hex)
{
	if (!CanCast(Side, SpellIndex, Hex))
	{
		return false;
	}
	const FHexSpell& Spell = *GetSpell(SpellIndex);
	Mana[Side] -= Spell.ManaCost;
	bCast[Side] = true;
	const FHexBattleSideSetup& Caster = Setup.Sides[Side];
	const int32 Power = Spell.Power + Spell.PowerPerSpellPower * FMath::Max(Caster.SpellPower, 0);
	FHexBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Type = FHexBattleEvent::EType::Spell;
	Event.Spell = SpellIndex;
	Event.Side = Side;
	Event.Hex = Hex;
	Event.Target = FindUnitAt(Hex);
	Event.Amount = Power;
	switch (Spell.Kind)
	{
	case EHexSpellKind::Damage:
		DealDamage(INDEX_NONE, FindUnitAt(Hex), Power);
		break;
	case EHexSpellKind::AreaDamage:
		{
			TArray<int32> Hit;
			for (int32 Index = 0; Index < Units.Num(); ++Index)
			{
				if (Units[Index].IsAlive() && !Units[Index].bTower && Units[Index].Hex.DistanceTo(Hex) <= Spell.Radius)
				{
					Hit.Add(Index);
				}
			}
			for (const int32 Index : Hit)
			{
				DealDamage(INDEX_NONE, Index, Power);
			}
		}
		break;
	case EHexSpellKind::Heal:
		{
			// the wounded of the stack: never above the creatures it has now
			FHexBattleUnit& Unit = Units[FindUnitAt(Hex)];
			const int32 Healed = FMath::Min(Power, Unit.Stats.Health - Unit.TopHealth);
			Unit.TopHealth += FMath::Max(Healed, 0);
			AddEvent(FHexBattleEvent::EType::Heal, FindUnitAt(Hex), FindUnitAt(Hex), FMath::Max(Healed, 0));
		}
		break;
	case EHexSpellKind::Effect:
		{
			FHexBattleUnit& Unit = Units[FindUnitAt(Hex)];
			const int32 Rounds = FMath::Max(Spell.BaseRounds + Spell.RoundsPerSpellPower * FMath::Max(Caster.SpellPower, 0), 1);
			FHexBattleEffect* Found = Unit.Effects.FindByPredicate([SpellIndex](const FHexBattleEffect& Each) { return Each.Spell == SpellIndex; });
			if (Found)
			{
				Found->RoundsLeft = FMath::Max(Found->RoundsLeft, Rounds);
			}
			else
			{
				Unit.Effects.Add({SpellIndex, Rounds});
			}
		}
		break;
	}
	// a spell may kill the unit whose turn it is, or a side
	if (CheckEnd())
	{
		return true;
	}
	if (Units.IsValidIndex(Active) && !Units[Active].IsAlive())
	{
		EndTurn();
	}
	return true;
}

void FHexBattle::Retreat(int32 Side)
{
	if (IsOver())
	{
		return;
	}
	Retreated = Side;
	Winner = 1 - Side;
	Active = INDEX_NONE;
	FHexBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Type = FHexBattleEvent::EType::Retreat;
	Event.Side = Side;
	AddEvent(FHexBattleEvent::EType::End, INDEX_NONE, INDEX_NONE, Winner);
}

TArray<FHexStack> FHexBattle::GetSurvivors(int32 Side) const
{
	TArray<FHexStack> Left;
	if (Side == Retreated)
	{
		return Left;
	}
	for (int32 Slot = 0; Slot < Setup.Sides[Side].Stacks.Num(); ++Slot)
	{
		FHexStack Stack = Setup.Sides[Side].Stacks[Slot];
		const FHexBattleUnit* Unit = Units.FindByPredicate([Side, Slot](const FHexBattleUnit& Each) { return Each.Side == Side && Each.Slot == Slot; });
		if (Unit)
		{
			Stack.Count = Unit->Count;
		}
		if (Stack.Count > 0)
		{
			Left.Add(Stack);
		}
	}
	return Left;
}

int32 FHexBattle::GetSlotCount(int32 Side, int32 Slot) const
{
	if (Side == Retreated || !Setup.Sides[Side].Stacks.IsValidIndex(Slot))
	{
		return 0;
	}
	const FHexBattleUnit* Unit = Units.FindByPredicate([Side, Slot](const FHexBattleUnit& Each) { return Each.Side == Side && Each.Slot == Slot; });
	return Unit ? Unit->Count : FMath::Max(Setup.Sides[Side].Stacks[Slot].Count, 0);
}

int32 FHexBattle::GetKilledHealth(int32 Side) const
{
	int32 Health = 0;
	for (const FHexBattleUnit& Unit : Units)
	{
		if (Unit.Side != Side && !Unit.IsMachine())
		{
			Health += (Unit.StartCount - Unit.Count) * Unit.Stats.Health;
		}
	}
	return Health;
}

float FHexBattle::GetWorth(const FHexBattleUnit& Unit, int32 Creatures) const
{
	return Unit.Stats.GetPower() * float(FMath::Max(Creatures, 0));
}

float FHexBattle::GetSidePower(int32 Side) const
{
	float Power = 0.0f;
	for (const FHexBattleUnit& Unit : Units)
	{
		if (Unit.Side == Side && Unit.IsAlive())
		{
			Power += GetWorth(Unit, Unit.Count);
		}
	}
	return Power;
}

FHexStrikePreview FHexBattle::PreviewStrike(int32 UnitIndex, int32 Target, bool bShot, int32 HexesRun) const
{
	FHexStrikePreview Preview;
	if (!Units.IsValidIndex(UnitIndex) || !Units.IsValidIndex(Target))
	{
		return Preview;
	}
	const FHexBattleUnit& Attacker = Units[UnitIndex];
	const FHexBattleUnit& Struck = Units[Target];
	const float Factor = GetDamageFactor(Attacker, Struck, bShot, HexesRun);
	bool bBlessed = false;
	for (const FHexBattleEffect& Effect : Attacker.Effects)
	{
		const FHexSpell* Spell = GetSpell(Effect.Spell);
		bBlessed |= Spell && Spell->bMaxDamage;
	}
	const int32 Strikes = Attacker.Has(EHexAbility::DoubleStrike) ? 2 : 1;
	Preview.DamageMin = FMath::Max(FMath::FloorToInt32((bBlessed ? Attacker.Stats.DamageMax : Attacker.Stats.DamageMin) * Attacker.Count * Factor), 1) * Strikes;
	Preview.DamageMax = FMath::Max(FMath::FloorToInt32(Attacker.Stats.DamageMax * Attacker.Count * Factor), 1) * Strikes;
	const auto Kills = [&Struck](int32 Damage)
	{
		const int32 Total = FMath::Max(Struck.GetTotalHealth() - Damage, 0);
		return Struck.Count - (Total > 0 ? (Total + Struck.Stats.Health - 1) / Struck.Stats.Health : 0);
	};
	Preview.KillsMin = Kills(Preview.DamageMin);
	Preview.KillsMax = Kills(Preview.DamageMax);
	Preview.bFar = bShot && Config && Attacker.Hex.DistanceTo(Struck.Hex) > Config->ShotFarHexes;
	Preview.bMeleePenalty = !bShot && Attacker.bShoots && !Attacker.Has(EHexAbility::NoMeleePenalty);
	// what is left of the one struck strikes back once: the most of it after the least damage, the least after the most
	if (!bShot && Struck.RetaliationsLeft > 0 && !Attacker.Has(EHexAbility::NoRetaliation) && Preview.KillsMin < Struck.Count)
	{
		Preview.bStrikesBack = true;
		const float BackFactor = GetDamageFactor(Struck, Attacker, false, 0);
		const auto BackDamage = [&](int32 Left, int32 Each) { return FMath::Max(FMath::FloorToInt32(Each * Left * BackFactor), 1); };
		const auto BackKills = [&Attacker](int32 Damage)
		{
			const int32 Total = FMath::Max(Attacker.GetTotalHealth() - Damage, 0);
			return Attacker.Count - (Total > 0 ? (Total + Attacker.Stats.Health - 1) / Attacker.Stats.Health : 0);
		};
		Preview.BackDamageMin = Preview.KillsMax < Struck.Count ? BackDamage(Struck.Count - Preview.KillsMax, Struck.Stats.DamageMin) : 0;
		Preview.BackDamageMax = BackDamage(Struck.Count - Preview.KillsMin, Struck.Stats.DamageMax);
		Preview.BackKillsMin = BackKills(Preview.BackDamageMin);
		Preview.BackKillsMax = BackKills(Preview.BackDamageMax);
	}
	return Preview;
}

FHexSpellPreview FHexBattle::PreviewSpell(int32 Side, int32 SpellIndex, const FHexCoord& Hex) const
{
	FHexSpellPreview Preview;
	const FHexSpell* Spell = GetSpell(SpellIndex);
	if (!Spell || Side < 0 || Side > 1)
	{
		return Preview;
	}
	const int32 Power = Spell->Power + Spell->PowerPerSpellPower * FMath::Max(Setup.Sides[Side].SpellPower, 0);
	const auto Hit = [&](const FHexBattleUnit& Unit)
	{
		const int32 Total = FMath::Max(Unit.GetTotalHealth() - Power, 0);
		Preview.Kills += Unit.Count - (Total > 0 ? (Total + Unit.Stats.Health - 1) / Unit.Stats.Health : 0);
		++Preview.Units;
	};
	const int32 At = FindUnitAt(Hex);
	switch (Spell->Kind)
	{
	case EHexSpellKind::Damage:
		Preview.Damage = Power;
		if (At != INDEX_NONE)
		{
			Hit(Units[At]);
		}
		break;
	case EHexSpellKind::AreaDamage:
		Preview.Damage = Power;
		for (const FHexBattleUnit& Unit : Units)
		{
			if (Unit.IsAlive() && Unit.Hex.DistanceTo(Hex) <= Spell->Radius)
			{
				Hit(Unit);
			}
		}
		break;
	case EHexSpellKind::Heal:
		Preview.Heal = At != INDEX_NONE ? FMath::Max(FMath::Min(Power, Units[At].Stats.Health - Units[At].TopHealth), 0) : 0;
		Preview.Units = At != INDEX_NONE ? 1 : 0;
		break;
	case EHexSpellKind::Effect:
		Preview.Units = At != INDEX_NONE ? 1 : 0;
		break;
	}
	return Preview;
}

float FHexBattle::RateSpell(int32 Side, int32 SpellIndex, const FHexCoord& Hex) const
{
	const FHexSpell* Spell = GetSpell(SpellIndex);
	if (!Spell)
	{
		return 0.0f;
	}
	const int32 Power = Spell->Power + Spell->PowerPerSpellPower * FMath::Max(Setup.Sides[Side].SpellPower, 0);
	const auto KilledWorth = [&](const FHexBattleUnit& Unit, int32 Damage)
	{
		const int32 Total = FMath::Max(Unit.GetTotalHealth() - Damage, 0);
		const int32 Left = Total > 0 ? (Total + Unit.Stats.Health - 1) / Unit.Stats.Health : 0;
		// a share of a creature counts too, so a strike that kills none is still worth a little
		return GetWorth(Unit, Unit.Count - Left) + GetWorth(Unit, 1) * FMath::Min(float(Damage) / Unit.Stats.Health, 1.0f) * 0.25f;
	};
	const int32 UnitIndex = FindUnitAt(Hex);
	switch (Spell->Kind)
	{
	case EHexSpellKind::Damage:
		return UnitIndex != INDEX_NONE ? KilledWorth(Units[UnitIndex], Power) : 0.0f;
	case EHexSpellKind::AreaDamage:
		{
			float Worth = 0.0f;
			for (const FHexBattleUnit& Unit : Units)
			{
				if (Unit.IsAlive() && Unit.Hex.DistanceTo(Hex) <= Spell->Radius)
				{
					Worth += (Unit.Side == Side ? -1.5f : 1.0f) * KilledWorth(Unit, Power);
				}
			}
			return Worth;
		}
	case EHexSpellKind::Heal:
		return UnitIndex != INDEX_NONE ? GetWorth(Units[UnitIndex], 1) * FMath::Min(Power, Units[UnitIndex].Stats.Health - Units[UnitIndex].TopHealth) / float(Units[UnitIndex].Stats.Health) : 0.0f;
	case EHexSpellKind::Effect:
		{
			if (UnitIndex == INDEX_NONE || Units[UnitIndex].Effects.ContainsByPredicate([SpellIndex](const FHexBattleEffect& Each) { return Each.Spell == SpellIndex; }))
			{
				return 0.0f;
			}
			// a share of what the stack is worth, by how much the spell changes it
			const FHexBattleUnit& Unit = Units[UnitIndex];
			float Share = 0.0f;
			Share += FMath::Abs(Spell->SpeedAdd) * 0.04f + FMath::Abs(1.0f - Spell->SpeedShare) * 0.3f;
			Share += FMath::Abs(Spell->AttackAdd + Spell->DefenseAdd) * 0.03f;
			Share += Spell->bMaxDamage ? 0.15f : 0.0f;
			Share += Spell->DamageTakenCut * 0.5f;
			// a shooter gains nothing from speed
			if (Unit.bShoots && (Spell->SpeedAdd != 0 || Spell->SpeedShare != 1.0f))
			{
				Share *= 0.2f;
			}
			return GetWorth(Unit, Unit.Count) * Share;
		}
	}
	return 0.0f;
}

bool FHexBattle::ChooseSpell(int32 Side, int32& OutSpell, FHexCoord& OutHex) const
{
	if (!CanCastAny(Side))
	{
		return false;
	}
	float Best = 0.0f;
	for (int32 Spell = 0; Spell < Config->Spells.Num(); ++Spell)
	{
		const FHexSpell& Each = Config->Spells[Spell];
		if (Each.ManaCost > Mana[Side])
		{
			continue;
		}
		for (const FHexBattleUnit& Unit : Units)
		{
			if (!Unit.IsAlive() || !CanCast(Side, Spell, Unit.Hex))
			{
				continue;
			}
			const float Worth = RateSpell(Side, Spell, Unit.Hex);
			// worth its mana: so much of the power of the stacks for each point of it
			if (Worth > Best && Worth >= Config->AiSpellWorth * Each.ManaCost)
			{
				Best = Worth;
				OutSpell = Spell;
				OutHex = Unit.Hex;
			}
		}
	}
	return Best > 0.0f;
}

void FHexBattle::PlayAi()
{
	if (IsOver() || !Units.IsValidIndex(Active))
	{
		return;
	}
	const int32 Side = Units[Active].Side;
	int32 Spell = INDEX_NONE;
	FHexCoord SpellHex;
	if (ChooseSpell(Side, Spell, SpellHex))
	{
		Cast(Side, Spell, SpellHex);
		if (IsOver() || !Units.IsValidIndex(Active) || Units[Active].Side != Side)
		{
			return;
		}
	}
	const int32 Me = Active;
	const FHexBattleUnit& Unit = Units[Me];
	// the catapult shoots at the wall, and with none left waits
	if (Unit.bCatapult)
	{
		FHexCoord WallHex;
		if (!(ChooseCatapultTarget(WallHex) && FireCatapult(WallHex)))
		{
			Defend();
		}
		return;
	}
	// a shooter shoots what it kills most of
	if (Unit.bShoots && Unit.ShotsLeft > 0 && !HasEnemyBeside(Me))
	{
		int32 Target = INDEX_NONE;
		float Best = -1.0f;
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			if (CanShoot(Me, Index))
			{
				const FHexStrikePreview Preview = PreviewStrike(Me, Index, true, 0);
				const float Worth = GetWorth(Units[Index], (Preview.KillsMin + Preview.KillsMax) / 2) + GetWorth(Units[Index], 1) * 0.1f + (Units[Index].bShoots ? GetWorth(Units[Index], 1) * 0.5f : 0.0f);
				if (Worth > Best)
				{
					Best = Worth;
					Target = Index;
				}
			}
		}
		if (Target != INDEX_NONE && Shoot(Target))
		{
			return;
		}
	}
	// else the best strike it reaches, weighing what is struck back
	TMap<FHexCoord, int32> Steps;
	FindReach(Me, Steps);
	int32 BestTarget = INDEX_NONE;
	FHexCoord BestFrom;
	float BestWorth = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		if (!Units[Index].IsAlive() || Units[Index].Side == Side)
		{
			continue;
		}
		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			const FHexCoord From = Units[Index].Hex.GetNeighbor(Direction);
			const int32* Run = Steps.Find(From);
			if (!Run)
			{
				continue;
			}
			const FHexStrikePreview Preview = PreviewStrike(Me, Index, false, *Run);
			float Worth = GetWorth(Units[Index], (Preview.KillsMin + Preview.KillsMax) / 2) + GetWorth(Units[Index], 1) * 0.1f;
			if (Units[Index].RetaliationsLeft > 0 && !Unit.Has(EHexAbility::NoRetaliation))
			{
				Worth -= 0.4f * GetWorth(Unit, FMath::Min(Unit.Count, FMath::Max(Units[Index].Count * Units[Index].Stats.DamageMax / FMath::Max(Unit.Stats.Health, 1) / 2, 0)));
			}
			// less running is a little better, so a stack does not dance around
			Worth -= *Run * 0.01f;
			if (Worth > BestWorth)
			{
				BestWorth = Worth;
				BestTarget = Index;
				BestFrom = From;
			}
		}
	}
	if (BestTarget != INDEX_NONE && Strike(BestTarget, BestFrom))
	{
		return;
	}
	// the shooters of a tower that see no one wait; a stack of the attacker at the wall strikes it, the gate first
	if (Unit.bTower)
	{
		Defend();
		return;
	}
	if (FHexCoord WallHex, From; Unit.Side == 0 && !Walls.IsEmpty() && ChooseWallStrike(Me, Steps, WallHex, From) && StrikeWall(WallHex, From))
	{
		return;
	}
	// a shooter that cannot shoot nor strike stays; the rest, and one out of shots, go towards the nearest enemy
	if (!Unit.bShoots || Unit.ShotsLeft <= 0)
	{
		FHexCoord Goal = Unit.Hex;
		int32 Nearest = TNumericLimits<int32>::Max();
		for (const TPair<FHexCoord, int32>& Each : Steps)
		{
			for (const FHexBattleUnit& Enemy : Units)
			{
				if (Enemy.IsAlive() && Enemy.Side != Side)
				{
					const int32 Distance = Each.Key.DistanceTo(Enemy.Hex) * 100 - Each.Value;
					if (Distance < Nearest)
					{
						Nearest = Distance;
						Goal = Each.Key;
					}
				}
			}
		}
		if (Goal != Unit.Hex && Move(Goal))
		{
			return;
		}
	}
	Defend();
}

void FHexBattle::PlayToEnd()
{
	// a guard against a battle that would never end, past the rounds that end it anyway
	for (int32 Step = 0; Step < 20000 && !IsOver(); ++Step)
	{
		PlayAi();
	}
	if (!IsOver())
	{
		Winner = 1;
		Active = INDEX_NONE;
	}
}
