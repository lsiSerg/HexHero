// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexZonePieces.h"
#include "HexBattle.h"
#include "HexBattleConfig.h"
#include "HexBattles.h"
#include "HexCastleConfig.h"
#include "HexCastles.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexNeutrals.h"
#include "HexPlanet.h"
#include "HexPlanetObjects.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "JsonObjectConverter.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"

TArray<FHexStack> HexZonePieces::MakeWeekArmy(const UHexCastleConfig& Castles, const FHexZoneMapGeneration& Settings, int32 Faction)
{
	TArray<FHexStack> Army;
	if (!Castles.Factions.IsValidIndex(Faction))
	{
		return Army;
	}
	const TArray<FHexCreature>& Creatures = Castles.Factions[Faction].Creatures;
	// what a hero starts with, as UHexCastles::MakeStartArmy gives it
	for (int32 Creature = 0; Creature < Castles.HeroStartWeeks.Num() && Creature < Creatures.Num(); ++Creature)
	{
		UHexCastles::AddToArmy(Army, {Faction, Creature, false, FMath::RoundToInt32(Creatures[Creature].Growth * Castles.HeroStartWeeks[Creature])});
	}
	// and a week of what its first dwellings offer
	for (int32 Creature = 0; Creature < FMath::Min(Settings.GuardDwellings, Creatures.Num()); ++Creature)
	{
		UHexCastles::AddToArmy(Army, {Faction, Creature, false, FMath::Max(FMath::RoundToInt32(Creatures[Creature].Growth * Settings.GuardWeeks), 1)});
	}
	return Army;
}

int32 HexZonePieces::FindGuardCount(const UHexCastleConfig& Castles, const UHexBattleConfig& Battle, const UHexHeroConfig& Heroes, int32 Faction, int32 Level, bool bUpgraded)
{
	const FHexZoneMapGeneration& Settings = Heroes.ZoneMap;
	const int32 Class = FMath::Max(Heroes.FindClass(Settings.HeroClass), 0);
	const FHexHeroClass& HeroClass = Heroes.GetClass(Class);
	const auto Power = [&Castles](TConstArrayView<FHexStack> Army)
	{
		float Sum = 0.0f;
		for (const FHexStack& Stack : Army)
		{
			const FHexCreatureStats* Stats = Castles.GetStats(Stack);
			Sum += Stats ? Stats->GetPower() * float(FMath::Max(Stack.Count, 0)) : 0.0f;
		}
		return Sum;
	};
	const auto Wins = [&](int32 Army, int32 Count)
	{
		int32 Won = 0;
		float Lost = 0.0f;
		for (int32 Try = 0; Try < Settings.GuardTries; ++Try)
		{
			FHexBattleSetup Setup;
			Setup.Seed = uint32(HashCombine(GetTypeHash(Count), HashCombine(GetTypeHash(Try), GetTypeHash(Faction * 16 + Level))));
			FHexBattleSideSetup& Hero = Setup.Sides[0];
			Hero.Player = 0;
			Hero.HeroId = 1;
			Hero.HeroName = TEXT("Hero");
			Hero.Attack = HeroClass.Attack;
			Hero.Defense = HeroClass.Defense;
			Hero.SpellPower = HeroClass.SpellPower;
			Hero.Mana = HeroClass.Knowledge * Heroes.ManaPerKnowledge;
			Hero.Stacks = MakeWeekArmy(Castles, Settings, Army);
			FHexBattleSideSetup& Guard = Setup.Sides[1];
			Guard.Player = -1;
			Guard.Stacks = {FHexStack{Faction, Level, bUpgraded, Count}};
			Guard.bBySlots = false;
			FHexBattle Fight;
			Fight.Start(Setup, &Castles, &Battle);
			Fight.PlayToEnd();
			Won += Fight.GetWinner() == 0 ? 1 : 0;
			const float Before = Power(Hero.Stacks);
			Lost += Before > 0.0f ? 1.0f - FMath::Clamp(Power(Fight.GetSurvivors(0)) / Before, 0.0f, 1.0f) : 1.0f;
		}
		return Won * 2 >= Settings.GuardTries && Lost / Settings.GuardTries <= Settings.GuardMaxLoss;
	};
	// the largest stack the weaker army beats, found by halves
	int32 Most = TNumericLimits<int32>::Max();
	for (int32 Army = 0; Army < Castles.Factions.Num(); ++Army)
	{
		// no hero leads the creatures of the sea
		if (Castles.Factions[Army].bSea)
		{
			continue;
		}
		int32 Low = 1;
		int32 High = 4096;
		if (!Wins(Army, Low))
		{
			Most = 1;
			continue;
		}
		while (High - Low > 1)
		{
			const int32 Mid = (Low + High) / 2;
			(Wins(Army, Mid) ? Low : High) = Mid;
		}
		Most = FMath::Min(Most, Low);
	}
	return FMath::Max(FMath::FloorToInt32(float(Most == TNumericLimits<int32>::Max() ? 1 : Most) * Settings.GuardShare), 1);
}

bool HexZonePieces::Place(AHexPlanet& Surface, const FHexZonesFile& File, FString& OutReport)
{
	UHexHeroes* Heroes = Surface.GetHeroes();
	UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
	UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	UHexNeutrals* Neutrals = Heroes ? Heroes->GetNeutrals() : nullptr;
	UHexBattles* Battles = Heroes ? Heroes->GetBattles() : nullptr;
	UHexPlanetObjects* Objects = Surface.GetObjects();
	if (!Heroes || !Castles || !Sites || !Neutrals || !Battles || !Battles->GetConfig() || !Objects || !Surface.IsBuilt())
	{
		OutReport = TEXT("the planet is not built yet");
		return false;
	}
	const double StartTime = FPlatformTime::Seconds();
	const UHexHeroConfig* HeroConfig = Heroes->GetConfig();
	const FHexZoneMapGeneration& Settings = HeroConfig->ZoneMap;
	const UHexCastleConfig* CastleConfig = Castles->GetConfig();
	const TArray<FHexSphereCell>& Cells = Surface.GetGrid().GetCells();
	const int32 NumCells = Cells.Num();
	FRandomStream Random(File.Seed);

	// the castles: a player's of its faction, the game's taking the factions by turns from AiFaction, nobody's by turns and empty
	const int32 PersonFaction = FMath::Max(CastleConfig->FindFaction(Settings.PersonFaction), 0);
	const int32 AiFaction = FMath::Max(CastleConfig->FindFaction(Settings.AiFaction), 0);
	// the factions of the land, which come before the creatures of the sea
	const int32 NumFactions = FMath::Max(CastleConfig->Factions.FilterByPredicate([](const FHexFaction& Each) { return !Each.bSea; }).Num(), 1);
	TArray<FHexCastleLine> CastleLines;
	int32 AiIndex = 0;
	int32 NobodyIndex = 0;
	TMap<int32, int32> PlayerFactions;
	for (const FHexZoneLine& Zone : File.Zones)
	{
		const bool bPerson = Zone.Role == TEXT("Person");
		const bool bAi = Zone.Role == TEXT("Ai");
		for (int32 Index = 0; Index + 2 < Zone.Castles.Num(); Index += 3)
		{
			FHexCastleLine& Line = CastleLines.AddDefaulted_GetRef();
			Line.Id = CastleLines.Num();
			const int32 Faction = (bPerson || bAi) && Index == 0 ? (bPerson ? PersonFaction : (AiFaction + AiIndex) % NumFactions) : (PersonFaction + NobodyIndex++) % NumFactions;
			Line.Faction = CastleConfig->Factions[Faction].Id.ToString();
			Line.Layer = GetPlanetLayerName(EHexPlanetLayer::Surface);
			Line.Cells = {Zone.Castles[Index], Zone.Castles[Index + 1], Zone.Castles[Index + 2]};
			// the first castle of the zone of a player is its own, the others of nobody
			const bool bOwned = (bPerson || bAi) && Index == 0;
			Line.Owner = bOwned ? Zone.Player : -1;
			// a castle of nobody stands empty, as the developer asked ("пусті замки")
			Line.bGarrisonSet = !bOwned;
			if (bOwned)
			{
				PlayerFactions.FindOrAdd(Zone.Player, Faction);
			}
		}
		AiIndex += bAi ? 1 : 0;
	}
	Castles->SetLines(CastleLines);

	// a hero beside the castle of each player
	TArray<FHexHeroLine> HeroLines;
	const int32 Class = FMath::Max(HeroConfig->FindClass(Settings.HeroClass), 0);
	const FHexHeroClass& HeroClass = HeroConfig->GetClass(Class);
	for (const FHexZoneLine& Zone : File.Zones)
	{
		if (Zone.Hero == INDEX_NONE || Zone.Player < 0)
		{
			continue;
		}
		FHexHeroLine& Line = HeroLines.AddDefaulted_GetRef();
		Line.Id = HeroLines.Num();
		Line.Name = HeroClass.Names.IsValidIndex(HeroLines.Num() - 1) ? HeroClass.Names[HeroLines.Num() - 1] : FString::Printf(TEXT("Hero %d"), Line.Id);
		Line.Class = HeroClass.Id.ToString();
		Line.Player = Zone.Player;
		Line.Layer = GetPlanetLayerName(EHexPlanetLayer::Surface);
		Line.Cell = Zone.Hero;
	}
	Heroes->SetLines(HeroLines);
	for (const FHexZoneLine& Zone : File.Zones)
	{
		if (Zone.Player >= 0)
		{
			Heroes->SetPlayerAi(Zone.Player, Zone.Role == TEXT("Ai"));
		}
	}

	// nothing stands on the passes, in the pockets and around the castles, and a forest that leaves open land out of reach of every castle
	// is cut through along the cheapest way
	TSet<int32> Clear(File.Clear);
	for (const FHexZoneGuardLine& Guard : File.Guards)
	{
		Clear.Add(Guard.Cell);
	}
	TMap<int32, FHexCellObjects> Cleared;
	for (const int32 Cell : Clear)
	{
		if (Cells.IsValidIndex(Cell) && !Objects->GetCell(Cell).IsEmpty())
		{
			Cleared.Add(Cell, FHexCellObjects());
		}
	}
	TSet<int32> CastleCells;
	for (const FHexCastleLine& Line : CastleLines)
	{
		CastleCells.Append(Line.Cells);
	}
	// the guards close the ways between the zones, so each zone cuts its forests to its own ways in and not only to what another zone reaches
	TSet<int32> GuardCells;
	for (const FHexZoneGuardLine& Guard : File.Guards)
	{
		GuardCells.Add(Guard.Cell);
	}
	const auto IsLand = [&](int32 Cell)
	{
		const int32 Level = Surface.GetCellLevel(Cell);
		return Level >= 0 && Level <= 1 && !CastleCells.Contains(Cell) && !GuardCells.Contains(Cell);
	};
	const auto IsBlocked = [&](int32 Cell) { return !Cleared.Contains(Cell) && Objects->BlocksMovement(Cell); };
	{
		// a search of the cost of the forests cut, 0 over open land
		TArray<int32> Cost;
		Cost.Init(TNumericLimits<int32>::Max(), NumCells);
		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, NumCells);
		TArray<int32> Front;
		TArray<int32> Next;
		for (const FHexCastleLine& Line : CastleLines)
		{
			for (const int32 Part : Line.Cells)
			{
				for (const int32 Neighbor : Cells[Part].Neighbors)
				{
					if (IsLand(Neighbor) && Cost[Neighbor] != 0)
					{
						Cost[Neighbor] = 0;
						Front.Add(Neighbor);
					}
				}
			}
		}
		// by rounds of equal cost, each round spread over open land first
		while (!Front.IsEmpty())
		{
			for (int32 At = 0; At < Front.Num(); ++At)
			{
				const int32 Cell = Front[At];
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					if (!IsLand(Neighbor))
					{
						continue;
					}
					const int32 Step = IsBlocked(Neighbor) ? 1 : 0;
					if (Cost[Cell] + Step < Cost[Neighbor])
					{
						Cost[Neighbor] = Cost[Cell] + Step;
						Parent[Neighbor] = Cell;
						(Step == 0 ? Front : Next).Add(Neighbor);
					}
				}
			}
			Front = MoveTemp(Next);
			Next.Reset();
		}
		// and every guard is reached from each side, the cells beside it counted as land to reach
		for (const int32 Guard : GuardCells)
		{
			for (const int32 Neighbor : Cells[Guard].Neighbors)
			{
				if (IsLand(Neighbor) && Cost[Neighbor] > 0 && Cost[Neighbor] != TNumericLimits<int32>::Max())
				{
					for (int32 Way = Neighbor; Way != INDEX_NONE && Cost[Way] > 0; Way = Parent[Way])
					{
						Cleared.Add(Way, FHexCellObjects());
					}
				}
			}
		}
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (Cost[Cell] > 0 && Cost[Cell] != TNumericLimits<int32>::Max() && !IsBlocked(Cell))
			{
				for (int32 Way = Cell; Way != INDEX_NONE && Cost[Way] > 0; Way = Parent[Way])
				{
					if (IsBlocked(Way))
					{
						Cleared.Add(Way, FHexCellObjects());
					}
				}
			}
		}
	}
	if (!Cleared.IsEmpty())
	{
		Objects->SetCells(Cleared);
	}

	// the mines and piles, many of them, off the passes and pockets
	TBitArray<> Avoid(false, NumCells);
	for (const int32 Cell : Clear)
	{
		if (Cells.IsValidIndex(Cell))
		{
			Avoid[Cell] = true;
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				Avoid[Neighbor] = Avoid[Neighbor] || File.Guards.ContainsByPredicate([Cell](const FHexZoneGuardLine& Guard) { return Guard.Cell == Cell; });
			}
		}
	}
	FString SiteReport;
	const int32 SiteSeed = File.Seed * 31 + 7;
	Sites->Generate(SiteReport, &SiteSeed, Settings.SiteDensity, &Avoid);

	// each zone has a mine of every resource and some piles on its land, what is missing put near its castles
	int32 Added = 0;
	{
		const UHexSiteConfig* SiteConfig = Sites->GetConfig();
		TBitArray<> GuardBits(false, NumCells);
		for (const FHexZoneGuardLine& Guard : File.Guards)
		{
			GuardBits[Guard.Cell] = true;
		}
		for (const FHexZoneLine& Zone : File.Zones)
		{
			// the land of the zone: what its castles reach with the guards standing
			TArray<int32> Steps;
			Steps.Init(INDEX_NONE, NumCells);
			TArray<int32> Front;
			for (int32 Index = 0; Index < Zone.Castles.Num(); ++Index)
			{
				for (const int32 Neighbor : Cells[Zone.Castles[Index]].Neighbors)
				{
					if (Steps[Neighbor] == INDEX_NONE && !CastleCells.Contains(Neighbor))
					{
						Steps[Neighbor] = 0;
						Front.Add(Neighbor);
					}
				}
			}
			for (int32 At = 0; At < Front.Num(); ++At)
			{
				for (const int32 Neighbor : Cells[Front[At]].Neighbors)
				{
					if (Steps[Neighbor] == INDEX_NONE && !GuardBits[Neighbor] && Heroes->IsOpenGround(EHexPlanetLayer::Surface, Neighbor))
					{
						Steps[Neighbor] = Steps[Front[At]] + 1;
						Front.Add(Neighbor);
					}
				}
			}
			TArray<int32> Mines;
			Mines.Init(0, SiteConfig->Resources.Num());
			int32 Piles = 0;
			TBitArray<> Near(false, NumCells);
			for (const FHexSite& Site : Sites->GetSites())
			{
				if (Site.Layer != EHexPlanetLayer::Surface || !SiteConfig->Kinds.IsValidIndex(Site.Kind))
				{
					continue;
				}
				for (const int32 Neighbor : Cells[Site.Cell].Neighbors)
				{
					Near[Neighbor] = true;
				}
				Near[Site.Cell] = true;
				if (Steps[Site.Cell] == INDEX_NONE)
				{
					bool bBeside = false;
					for (const int32 Neighbor : Cells[Site.Cell].Neighbors)
					{
						bBeside |= Steps[Neighbor] != INDEX_NONE;
					}
					if (!bBeside)
					{
						continue;
					}
				}
				const FHexSiteKind& Kind = SiteConfig->Kinds[Site.Kind];
				const int32 Resource = SiteConfig->FindResource(Kind.Resource);
				if (Kind.Type == EHexSiteType::Mine && Mines.IsValidIndex(Resource))
				{
					++Mines[Resource];
				}
				Piles += Kind.Type == EHexSiteType::Pile ? 1 : 0;
			}
			// open land all around, so a site cuts no way, off the passes and pockets and the other sites
			TArray<int32> Free;
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (Steps[Cell] < Settings.ZoneSiteMinSteps || Steps[Cell] > Settings.ZoneSiteMaxSteps || Avoid[Cell] || Near[Cell] || Cells[Cell].IsPentagon())
				{
					continue;
				}
				bool bOpen = true;
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					bOpen &= Steps[Neighbor] != INDEX_NONE && !Near[Neighbor];
				}
				if (bOpen)
				{
					Free.Add(Cell);
				}
			}
			const auto Put = [&](int32 Kind)
			{
				while (!Free.IsEmpty())
				{
					const int32 Cell = Free[Random.RandHelper(Free.Num())];
					Free.RemoveSwap(Cell);
					FString Problem;
					if (Sites->AddSite(EHexPlanetLayer::Surface, Cell, Kind, Problem) > 0)
					{
						++Added;
						// the next keeps a cell away
						Free.RemoveAll([&](int32 Other) { return Other == Cell || Cells[Cell].Neighbors.Contains(Other)
							|| Cells[Other].Neighbors.ContainsByPredicate([&](int32 Each) { return Cells[Cell].Neighbors.Contains(Each); }); });
						return true;
					}
				}
				return false;
			};
			for (int32 Kind = 0; Kind < SiteConfig->Kinds.Num(); ++Kind)
			{
				const FHexSiteKind& Each = SiteConfig->Kinds[Kind];
				const int32 Resource = SiteConfig->FindResource(Each.Resource);
				if (Each.Type == EHexSiteType::Mine && Mines.IsValidIndex(Resource) && Mines[Resource] == 0 && Put(Kind))
				{
					++Mines[Resource];
				}
			}
			TArray<int32> PileKinds;
			for (int32 Kind = 0; Kind < SiteConfig->Kinds.Num(); ++Kind)
			{
				if (SiteConfig->Kinds[Kind].Type == EHexSiteType::Pile)
				{
					PileKinds.Add(Kind);
				}
			}
			for (; Piles < Settings.ZoneMinPiles && !PileKinds.IsEmpty() && Put(PileKinds[Random.RandHelper(PileKinds.Num())]); ++Piles)
			{
			}
		}
	}

	// the neutral stacks of the map, weaker near the castles of each zone, none on the passes and pockets, and then the guard of each way in
	FString NeutralReport;
	const int32 NeutralSeed = File.Seed * 17 + 3;
	Neutrals->Generate(NeutralReport, &NeutralSeed, true);
	TArray<int32> Off;
	for (const FHexNeutral& Neutral : Neutrals->GetNeutrals())
	{
		if (Neutral.Layer == EHexPlanetLayer::Surface && (Clear.Contains(Neutral.Cell) || Avoid[Neutral.Cell]))
		{
			Off.Add(Neutral.Id);
		}
	}
	for (const int32 Id : Off)
	{
		Neutrals->Remove(Id);
	}
	// vaults, closed places with much in them and one guarded way in, as the developer asked on 2026-11-15
	FString VaultReport;
	Sites->MakeVaults(4, File.Seed * 29 + 7, VaultReport);
	TMap<int32, int32> Counts;
	int32 Guards = 0;
	for (const FHexZoneGuardLine& Guard : File.Guards)
	{
		Sites->RemoveSiteAt(EHexPlanetLayer::Surface, Guard.Cell);
		const int32 Faction = Random.RandHelper(NumFactions);
		const int32 Level = FMath::Clamp(Random.RandRange(Settings.GuardLevelMin, Settings.GuardLevelMax), 1, NumCreatureLevels) - 1;
		const int32 Key = Faction * 16 + Level;
		if (!Counts.Contains(Key))
		{
			Counts.Add(Key, FindGuardCount(*CastleConfig, *Battles->GetConfig(), *HeroConfig, Faction, Level, false));
		}
		FString Problem;
		if (Neutrals->Add(EHexPlanetLayer::Surface, Guard.Cell, FHexStack{Faction, Level, false, Counts[Key]}, Problem) > 0)
		{
			++Guards;
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Zones: no guard on cell %d: %s"), Guard.Cell, *Problem);
		}
	}
	FString Counted;
	for (const TPair<int32, int32>& Each : Counts)
	{
		const FHexStack Stack{Each.Key / 16, Each.Key % 16, false, Each.Value};
		Counted += (Counted.IsEmpty() ? TEXT("") : TEXT(", ")) + Castles->DescribeStack(Stack);
	}

	// an entrance that cannot be used takes a way from its zone, so it is said
	for (int32 Index = 0; Index < Surface.GetEntrances().Num(); ++Index)
	{
		if (Surface.GetEntranceProblem(Index) != HexPlanetUnderground::EEntranceProblem::None)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Zones: the entrance on cell %d of network %c cannot be used (problem %d)"), Surface.GetEntrances()[Index].Cell,
				TCHAR('A' + FMath::Max(Surface.GetEntrances()[Index].Network, 0)), int32(Surface.GetEntranceProblem(Index)));
		}
	}

	// a saved planet writes it all at once, as it does what it generated
	if (Surface.IsSaved())
	{
		FString Error;
		const FString Folder = Surface.GetPlanetFolder();
		FString Json;
		// the levels too, as the walls of the vaults raised mountains after they were written
		if (!Heroes->Save(Folder, Error) || !Objects->Save(Folder, {}, Error) || !Surface.SaveLevels(Error)
			|| !FJsonObjectConverter::UStructToJsonObjectString(File, Json)
			|| !FFileHelper::SaveStringToFile(Json, *HexPlanetFile::GetFilePath(Folder, HexZoneMap::ZonesFile), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Zones: could not write the pieces of %s: %s"), *Folder, *Error);
		}
	}
	OutReport = FString::Printf(TEXT("%d zones (%s), %d castles, %d heroes, %d sites (%d put in zones that lacked them), %d neutral stacks, %d guards (%s), %d cells cleared, in %.0f ms"),
		File.Zones.Num(), *File.Ways, CastleLines.Num(), HeroLines.Num(), Sites->GetSites().Num(), Added, Neutrals->GetNeutrals().Num(), Guards, *Counted, Cleared.Num(),
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
	UE_LOG(LogHexHero, Log, TEXT("Zones: %s; sites: %s; neutrals: %s"), *OutReport, *SiteReport, *NeutralReport);
	return true;
}
