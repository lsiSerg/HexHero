// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexCastles.h"
#include "HexBattles.h"
#include "HexCheats.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexMaterialConfig.h"
#include "HexPlanet.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexPlanetUnderground.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"

namespace
{
	const TCHAR* CastlesFile = TEXT("castles.json");

	TArray<FHexCastleLine> SortCastleLines(TArray<FHexCastleLine> Lines)
	{
		Lines.Sort([](const FHexCastleLine& A, const FHexCastleLine& B) { return A.Id < B.Id; });
		return Lines;
	}

	/** castles.json: a line per castle, sorted by Id, so Git merges it */
	FString EncodeCastles(TConstArrayView<FHexCastleLine> Lines)
	{
		const TArray<FHexCastleLine> Sorted = SortCastleLines(TArray<FHexCastleLine>(Lines));
		FString Text = TEXT("{\n\t\"version\": 1,\n\t\"castles\": [");
		for (int32 Index = 0; Index < Sorted.Num(); ++Index)
		{
			const FHexCastleLine& Line = Sorted[Index];
			FString Cells;
			for (const int32 Cell : Line.Cells)
			{
				Cells += FString::Printf(TEXT("%s%d"), Cells.IsEmpty() ? TEXT("") : TEXT(", "), Cell);
			}
			// what is built and who guards it when a game starts, only when set
			FString Start;
			if (!Line.Built.IsEmpty())
			{
				TArray<FString> Names;
				Line.Built.GetKeys(Names);
				Names.Sort();
				FString Built;
				for (const FString& Name : Names)
				{
					Built += FString::Printf(TEXT("%s\"%s\": %d"), Built.IsEmpty() ? TEXT("") : TEXT(", "), *Name, Line.Built[Name]);
				}
				Start += FString::Printf(TEXT(", \"built\": {%s}"), *Built);
			}
			if (Line.bGarrisonSet || !Line.Garrison.IsEmpty())
			{
				Start += TEXT(", \"garrison\": ") + HexPlanetFile::EncodeStackLines(Line.Garrison);
			}
			Text += FString::Printf(TEXT("%s\n\t\t{\"id\": %d, \"faction\": \"%s\", \"layer\": \"%s\", \"cells\": [%s], \"owner\": %d%s}"), Index == 0 ? TEXT("") : TEXT(","),
				Line.Id, *Line.Faction, *Line.Layer, *Cells, Line.Owner, *Start);
		}
		Text += Sorted.IsEmpty() ? TEXT("]\n}\n") : TEXT("\n\t]\n}\n");
		return Text;
	}
}

AHexPlanet* UHexCastles::GetPlanet(EHexPlanetLayer Layer) const
{
	return Surface ? Surface->GetLayerPlanet(Layer) : nullptr;
}

FLinearColor UHexCastles::GetPlayerColor(int32 Player) const
{
	const UHexHeroConfig* HeroConfig = UHexHeroConfig::Load();
	return HeroConfig->Players.IsValidIndex(Player) ? HeroConfig->Players[Player].Color : UHexSiteConfig::Load()->NeutralColor;
}

FString UHexCastles::GetPlayerName(int32 Player) const
{
	const UHexHeroConfig* HeroConfig = UHexHeroConfig::Load();
	return HeroConfig->Players.IsValidIndex(Player) ? HeroConfig->Players[Player].Name : FString(TEXT("nobody"));
}

const FHexCastle* UHexCastles::FindCastle(int32 Id) const
{
	return Castles.FindByPredicate([Id](const FHexCastle& Each) { return Each.Id == Id; });
}

FHexCastle* UHexCastles::FindCastleMutable(int32 Id)
{
	return Castles.FindByPredicate([Id](const FHexCastle& Each) { return Each.Id == Id; });
}

const FHexCastle* UHexCastles::FindCastleAt(EHexPlanetLayer Layer, int32 Cell) const
{
	return Castles.FindByPredicate([Layer, Cell](const FHexCastle& Each) { return Each.Layer == Layer && Each.HasCell(Cell); });
}

void UHexCastles::Touch(const FHexCastle& Castle)
{
	++Revision;
	bDirty[int32(Castle.Layer)] = true;
}

void UHexCastles::OnLayerRebuilt(EHexPlanetLayer Layer)
{
	bDirty[int32(Layer)] = true;
	for (FHexCastle& Castle : Castles)
	{
		if (Castle.Layer == Layer)
		{
			Castle.bSeated = false;
		}
	}
}

void UHexCastles::Init(AHexPlanet* InSurface, UHexHeroes* InHeroes)
{
	Surface = InSurface;
	Heroes = InHeroes;
	Config = UHexCastleConfig::Load();
	Castles.Reset();
	SavedLines.Reset();
	NextId = 1;
	bEdited = false;
	const FString FilePath = Surface && Surface->IsSaved() ? HexPlanetFile::GetFilePath(Surface->GetPlanetFolder(), CastlesFile) : FString();
	FString Text;
	if (!FilePath.IsEmpty() && IFileManager::Get().FileExists(*FilePath) && FFileHelper::LoadFileToString(Text, *FilePath))
	{
		FHexCastlesFile File;
		if (FJsonObjectConverter::JsonObjectStringToUStruct(Text, &File))
		{
			SetLines(File.Castles);
			SavedLines = SortCastleLines(File.Castles);
			bEdited = SortCastleLines(MakeLines()) != SavedLines;
			UE_LOG(LogHexHero, Log, TEXT("Castles: %d on the planet"), Castles.Num());
		}
		else
		{
			UE_LOG(LogHexHero, Error, TEXT("Castles: %s is not valid JSON"), *FilePath);
		}
	}
	Restart();
}

bool UHexCastles::Save(const FString& PlanetFolder, FString& OutError)
{
	const FString FilePath = HexPlanetFile::GetFilePath(PlanetFolder, CastlesFile);
	if (PlanetFolder.IsEmpty())
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	const TArray<FHexCastleLine> Lines = MakeLines();
	if (Lines.IsEmpty())
	{
		if (IFileManager::Get().FileExists(*FilePath) && !IFileManager::Get().Delete(*FilePath))
		{
			OutError = FString::Printf(TEXT("could not delete %s"), *FilePath);
			return false;
		}
	}
	else if (!FFileHelper::SaveStringToFile(EncodeCastles(Lines), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	SavedLines = SortCastleLines(Lines);
	bEdited = false;
	return true;
}

TArray<FHexCastleLine> UHexCastles::MakeLines() const
{
	TArray<FHexCastleLine> Lines;
	for (const FHexCastle& Castle : Castles)
	{
		FHexCastleLine& Line = Lines.AddDefaulted_GetRef();
		Line.Id = Castle.Id;
		Line.Faction = Config->Factions[Castle.Faction].Id.ToString();
		Line.Layer = GetPlanetLayerName(Castle.Layer);
		Line.Cells = {Castle.Cells[0], Castle.Cells[1], Castle.Cells[2]};
		Line.Owner = Castle.StartOwner;
		for (const TPair<FName, int32>& Each : Castle.StartLevels)
		{
			Line.Built.Add(Each.Key.ToString(), Each.Value);
		}
		Line.Garrison = MakeStackLines(Castle.StartGarrison);
		Line.bGarrisonSet = Castle.bStartGarrison;
	}
	return Lines;
}

TArray<FHexStackLine> UHexCastles::MakeStackLines(TConstArrayView<FHexStack> Stacks) const
{
	TArray<FHexStackLine> Lines;
	for (const FHexStack& Stack : Stacks)
	{
		if (Stack.Count > 0 && Config->Factions.IsValidIndex(Stack.Faction) && Config->Factions[Stack.Faction].Creatures.IsValidIndex(Stack.Creature))
		{
			Lines.Add({Config->Factions[Stack.Faction].Id.ToString(), Config->Factions[Stack.Faction].Creatures[Stack.Creature].Id.ToString(), Stack.bUpgraded, Stack.Count});
		}
	}
	return Lines;
}

TArray<FHexStack> UHexCastles::ParseStackLines(TConstArrayView<FHexStackLine> Lines) const
{
	TArray<FHexStack> Stacks;
	for (const FHexStackLine& Line : Lines)
	{
		const int32 Faction = Config->FindFaction(Line.Faction);
		const int32 Creature = Faction != INDEX_NONE ? Config->Factions[Faction].Creatures.IndexOfByPredicate([&Line](const FHexCreature& Each) { return Each.Id.ToString() == Line.Creature; })
			: INDEX_NONE;
		if (Creature == INDEX_NONE || Line.Count <= 0)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Castles: a stack of %s %s is left out, as DA_HexCastles has no such creature"), *Line.Faction, *Line.Creature);
			continue;
		}
		AddToArmy(Stacks, {Faction, Creature, Line.Upgraded, Line.Count});
	}
	return Stacks;
}

bool UHexCastles::KeepAsStart(int32 CastleId)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	if (!Castle)
	{
		return false;
	}
	const FHexFaction& Faction = Config->Factions[Castle->Faction];
	Castle->StartLevels.Reset();
	for (int32 Index = 0; Index < Faction.Buildings.Num() && Index < Castle->Levels.Num(); ++Index)
	{
		Castle->StartLevels.Add(Faction.Buildings[Index].Id, Castle->Levels[Index]);
	}
	Castle->StartGarrison = Castle->Garrison;
	Castle->bStartGarrison = true;
	bEdited = SortCastleLines(MakeLines()) != SavedLines;
	++Revision;
	return true;
}

int32 UHexCastles::BuildHalf(int32 CastleId)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	if (!Castle)
	{
		return 0;
	}
	const FHexFaction& Faction = Config->Factions[Castle->Faction];
	int32 Built = 0;
	for (int32 Index = 0; Index < Faction.Buildings.Num() && Index < Castle->Levels.Num(); ++Index)
	{
		const FHexBuilding& Building = Faction.Buildings[Index];
		// the dwellings of the four lower levels whole and the fifth begun; the hall and the fort to the middle of their levels
		const int32 Wanted = Building.Type == EHexBuildingType::Dwelling ? (Building.Creature < 4 ? Building.Levels.Num() : Building.Creature == 4 ? 1 : 0)
			: (Building.Levels.Num() + 1) / 2;
		if (Castle->Levels[Index] < Wanted)
		{
			Built += Wanted - Castle->Levels[Index];
			Castle->Levels[Index] = Wanted;
		}
	}
	for (int32 Creature = 0; Creature < NumCreatureLevels; ++Creature)
	{
		Castle->Available[Creature] = FMath::Max(Castle->Available[Creature], GetGrowth(*Castle, Creature));
	}
	bDirty[int32(Castle->Layer)] = true;
	++Revision;
	return Built;
}

void UHexCastles::SetLines(const TArray<FHexCastleLine>& Lines)
{
	const TArray<FHexCastleLine> Before = MakeLines();
	TArray<FHexCastle> Kept;
	for (const FHexCastleLine& Line : Lines)
	{
		// a castle whose line is the same stays as it is, with what was built in it
		const int32 Same = Before.IndexOfByKey(Line);
		if (Same != INDEX_NONE)
		{
			Kept.Add(Castles[Same]);
			continue;
		}
		FHexCastle Castle;
		Castle.Id = Line.Id;
		Castle.Faction = Config->FindFaction(Line.Faction);
		Castle.StartOwner = Line.Owner >= 0 ? Line.Owner : int32(INDEX_NONE);
		for (const TPair<FString, int32>& Each : Line.Built)
		{
			Castle.StartLevels.Add(FName(*Each.Key), Each.Value);
		}
		Castle.StartGarrison = ParseStackLines(Line.Garrison);
		Castle.bStartGarrison = Line.bGarrisonSet || !Line.Garrison.IsEmpty();
		bool bValid = Castle.Id > 0 && Castle.Faction != INDEX_NONE && Line.Cells.Num() == 3 && ParsePlanetLayer(Line.Layer, Castle.Layer);
		if (bValid)
		{
			Castle.Cells[0] = Line.Cells[0];
			Castle.Cells[1] = Line.Cells[1];
			Castle.Cells[2] = Line.Cells[2];
			bValid = OrderCells(Castle.Layer, Castle.Cells) && !Kept.ContainsByPredicate([&Castle](const FHexCastle& Each)
			{
				return Each.Id == Castle.Id || (Each.Layer == Castle.Layer && (Each.HasCell(Castle.Cells[0]) || Each.HasCell(Castle.Cells[1]) || Each.HasCell(Castle.Cells[2])));
			});
		}
		if (!bValid)
		{
			UE_LOG(LogHexHero, Error, TEXT("Castles: the castle %d (%s) of castles.json has no Id, faction, layer or three cells of its own around a corner, and is left out"),
				Line.Id, *Line.Faction);
			continue;
		}
		ResetCastle(Castle);
		Kept.Add(MoveTemp(Castle));
	}
	Castles = MoveTemp(Kept);
	for (const FHexCastle& Castle : Castles)
	{
		NextId = FMath::Max(NextId, Castle.Id + 1);
	}
	bEdited = SortCastleLines(MakeLines()) != SavedLines;
	bDirty[0] = bDirty[1] = true;
	++Revision;
	UHexHeroes::NoteMapChanged();
	// nothing of the planet is drawn on the cells of a castle
	if (Surface && Surface->IsBuilt())
	{
		Surface->UpdateClearCells();
	}
}

bool UHexCastles::OrderCells(EHexPlanetLayer Layer, int32 InOutCells[3]) const
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	const AHexPlanet* GridPlanet = Planet ? Planet : Surface.Get();
	if (!GridPlanet)
	{
		return false;
	}
	const TArray<FHexSphereCell>& Cells = GridPlanet->GetGrid().GetCells();
	int32 A = InOutCells[0];
	int32 B = InOutCells[1];
	int32 C = InOutCells[2];
	if (!Cells.IsValidIndex(A) || !Cells.IsValidIndex(B) || !Cells.IsValidIndex(C) || A == B || B == C || A == C
		|| !Cells[A].Neighbors.Contains(B) || !Cells[B].Neighbors.Contains(C) || !Cells[C].Neighbors.Contains(A))
	{
		return false;
	}
	// the lowest first, then counterclockwise seen from outside
	if (B < A && B < C)
	{
		Swap(A, B);
	}
	else if (C < A && C < B)
	{
		Swap(A, C);
	}
	const FVector3d Up = (Cells[A].Center + Cells[B].Center + Cells[C].Center).GetSafeNormal();
	if (FVector3d::DotProduct(FVector3d::CrossProduct(Cells[B].Center - Cells[A].Center, Cells[C].Center - Cells[A].Center), Up) < 0.0)
	{
		Swap(B, C);
	}
	InOutCells[0] = A;
	InOutCells[1] = B;
	InOutCells[2] = C;
	return true;
}

bool UHexCastles::FindCells(EHexPlanetLayer Layer, int32 Cell, const FVector3d& Direction, int32 OutCells[3]) const
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return false;
	}
	// corner K of a cell lies between its neighbors K and K + 1
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const FVector3d Towards = Direction.GetSafeNormal();
	int32 Best = INDEX_NONE;
	double BestDot = -2.0;
	for (int32 Corner = 0; Corner < Each.Corners.Num(); ++Corner)
	{
		const double Dot = Towards.IsNearlyZero() ? -double(Corner) : FVector3d::DotProduct(Grid.GetCorners()[Each.Corners[Corner]].GetSafeNormal(), Towards);
		if (Best == INDEX_NONE || Dot > BestDot)
		{
			Best = Corner;
			BestDot = Dot;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutCells[0] = Cell;
	OutCells[1] = Each.Neighbors[Best];
	OutCells[2] = Each.Neighbors[(Best + 1) % Each.Neighbors.Num()];
	return OrderCells(Layer, OutCells);
}

int32 UHexCastles::AddCastle(EHexPlanetLayer Layer, const int32 InCells[3], int32 Faction, int32 Owner, FString& OutProblem)
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	int32 Ordered[3] = {InCells[0], InCells[1], InCells[2]};
	if (!Config || !Config->Factions.IsValidIndex(Faction) || !Planet || !OrderCells(Layer, Ordered))
	{
		OutProblem = TEXT("there is no such faction, or the cells are not three around a corner");
		return 0;
	}
	for (const int32 Cell : Ordered)
	{
		// where a hero could stand, with nothing on it, as a castle fills its cells
		if (Heroes && !Heroes->CanStand(Layer, Cell, 0, &OutProblem))
		{
			return 0;
		}
		if (Planet->FindEntrance(Cell) != INDEX_NONE || (Heroes && Heroes->GetSites() && Heroes->GetSites()->FindSiteAt(Layer, Cell)) || FindCastleAt(Layer, Cell)
			|| Planet->GetGrid().GetCells()[Cell].IsPentagon())
		{
			OutProblem = TEXT("something stands on one of its cells already");
			return 0;
		}
		// underground its wall, which reaches past its cells, stands on the floor of a hall and not over the void, as the developer asked for castles
		// underground on 2026-11-13; that a whole ring of passages is needed is Claude's
		if (Layer == EHexPlanetLayer::Underground)
		{
			for (const int32 Neighbor : Planet->GetGrid().GetCells()[Cell].Neighbors)
			{
				if (Planet->GetCellLevel(Neighbor) != HexPlanetUnderground::PassageLevel)
				{
					OutProblem = TEXT("underground a castle needs passages all around it, or its wall would hang over the void");
					return 0;
				}
			}
		}
	}
	FHexCastle Castle;
	Castle.Id = NextId++;
	Castle.Faction = Faction;
	Castle.Layer = Layer;
	Castle.Cells[0] = Ordered[0];
	Castle.Cells[1] = Ordered[1];
	Castle.Cells[2] = Ordered[2];
	Castle.StartOwner = Owner >= 0 ? Owner : int32(INDEX_NONE);
	ResetCastle(Castle);
	const int32 Id = Castle.Id;
	UE_LOG(LogHexHero, Log, TEXT("Castles: a castle of the %s stands on cells %d, %d and %d of the %s as castle %d, of %s"), *Config->Factions[Faction].DisplayName,
		Ordered[0], Ordered[1], Ordered[2], GetPlanetLayerName(Layer), Id, *GetPlayerName(Castle.StartOwner));
	Castles.Add(MoveTemp(Castle));
	bEdited = SortCastleLines(MakeLines()) != SavedLines;
	bDirty[int32(Layer)] = true;
	++Revision;
	UHexHeroes::NoteMapChanged();
	Surface->UpdateClearCells();
	return Id;
}

bool UHexCastles::RemoveCastleAt(EHexPlanetLayer Layer, int32 Cell)
{
	const int32 Index = Castles.IndexOfByPredicate([Layer, Cell](const FHexCastle& Each) { return Each.Layer == Layer && Each.HasCell(Cell); });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Castles.RemoveAt(Index);
	bEdited = SortCastleLines(MakeLines()) != SavedLines;
	bDirty[int32(Layer)] = true;
	++Revision;
	UHexHeroes::NoteMapChanged();
	Surface->UpdateClearCells();
	return true;
}

int32 UHexCastles::GetLevel(const FHexCastle& Castle, EHexBuildingType Type, int32 Creature) const
{
	const int32 Building = Config->Factions[Castle.Faction].FindBuilding(Type, Creature);
	return Castle.Levels.IsValidIndex(Building) ? Castle.Levels[Building] : 0;
}

int32 UHexCastles::GetGrowth(const FHexCastle& Castle, int32 Creature) const
{
	const FHexFaction& Faction = Config->Factions[Castle.Faction];
	if (!Faction.Creatures.IsValidIndex(Creature) || GetLevel(Castle, EHexBuildingType::Dwelling, Creature) <= 0)
	{
		return 0;
	}
	// the fort makes them come faster
	const int32 Fort = Faction.FindBuilding(EHexBuildingType::Fort);
	const int32 FortLevel = GetLevel(Castle, EHexBuildingType::Fort);
	const float Factor = Faction.Buildings.IsValidIndex(Fort) && Faction.Buildings[Fort].Levels.IsValidIndex(FortLevel - 1) ? Faction.Buildings[Fort].Levels[FortLevel - 1].GrowthFactor : 1.0f;
	return FMath::Max(FMath::RoundToInt32(Faction.Creatures[Creature].Growth * FMath::Max(Factor, 0.0f)), 0);
}

void UHexCastles::ResetCastle(FHexCastle& Castle) const
{
	const FHexFaction& Faction = Config->Factions[Castle.Faction];
	Castle.Owner = Castle.StartOwner;
	Castle.BuiltToday = 0;
	Castle.EventTime = -1000.0;
	Castle.Levels.Reset();
	for (const FHexBuilding& Building : Faction.Buildings)
	{
		// as castles.json has it, else as the building starts
		const int32* Start = Castle.StartLevels.Find(Building.Id);
		Castle.Levels.Add(FMath::Clamp(Start ? *Start : Building.StartLevel, 0, Building.Levels.Num()));
	}
	// a week of what its dwellings offer, and for a castle of nobody a guard of some weeks of it
	Castle.Available.Init(0, NumCreatureLevels);
	Castle.Garrison.Reset();
	for (int32 Creature = 0; Creature < NumCreatureLevels; ++Creature)
	{
		const int32 Growth = GetGrowth(Castle, Creature);
		Castle.Available[Creature] = Growth;
		if (!Castle.bStartGarrison && Castle.Owner == INDEX_NONE && Growth > 0 && Config->NeutralGarrisonWeeks > 0.0f)
		{
			AddToArmy(Castle.Garrison, {Castle.Faction, Creature, false, FMath::Max(FMath::RoundToInt32(Growth * Config->NeutralGarrisonWeeks), 1)});
		}
	}
	// the garrison castles.json gives it
	if (Castle.bStartGarrison)
	{
		Castle.Garrison = Castle.StartGarrison;
	}
}

void UHexCastles::Restart()
{
	for (FHexCastle& Castle : Castles)
	{
		ResetCastle(Castle);
	}
	bDirty[0] = bDirty[1] = true;
	++Revision;
}

void UHexCastles::BeginDay(int32 Day, int32 DaysPerWeek)
{
	UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	const bool bNewWeek = (Day - 1) % FMath::Max(DaysPerWeek, 1) == 0;
	for (FHexCastle& Castle : Castles)
	{
		Castle.BuiltToday = 0;
		if (Castle.Owner == INDEX_NONE)
		{
			continue;
		}
		const FHexFaction& Faction = Config->Factions[Castle.Faction];
		const int32 Hall = Faction.FindBuilding(EHexBuildingType::Hall);
		const int32 HallLevel = GetLevel(Castle, EHexBuildingType::Hall);
		if (Sites && Faction.Buildings.IsValidIndex(Hall) && Faction.Buildings[Hall].Levels.IsValidIndex(HallLevel - 1))
		{
			Sites->AddAmount(Castle.Owner, TEXT("Gold"), Faction.Buildings[Hall].Levels[HallLevel - 1].GoldPerDay);
		}
		if (bNewWeek)
		{
			for (int32 Creature = 0; Creature < NumCreatureLevels; ++Creature)
			{
				Castle.Available[Creature] += GetGrowth(Castle, Creature);
			}
		}
	}
	++Revision;
}

bool UHexCastles::CanBuild(const FHexCastle& Castle, int32 Building, FString* OutWhy) const
{
	const auto Fail = [OutWhy](const FString& Why)
	{
		if (OutWhy)
		{
			*OutWhy = Why;
		}
		return false;
	};
	const FHexFaction& Faction = Config->Factions[Castle.Faction];
	if (!Faction.Buildings.IsValidIndex(Building) || !Castle.Levels.IsValidIndex(Building))
	{
		return Fail(TEXT("there is no such building"));
	}
	const FHexBuilding& Each = Faction.Buildings[Building];
	const int32 Level = Castle.Levels[Building];
	if (!Each.Levels.IsValidIndex(Level))
	{
		return Fail(TEXT("it is built as far as it goes"));
	}
	const FHexBuildingLevel& Next = Each.Levels[Level];
	if (!bUnlimitedBuilds && Castle.BuiltToday >= Config->BuildsPerDay)
	{
		return Fail(TEXT("the castle built today already"));
	}
	const int32 Hall = Faction.FindBuilding(EHexBuildingType::Hall);
	const int32 Fort = Faction.FindBuilding(EHexBuildingType::Fort);
	if (GetLevel(Castle, EHexBuildingType::Hall) < Next.NeedsHall && Faction.Buildings.IsValidIndex(Hall) && Faction.Buildings[Hall].Levels.IsValidIndex(Next.NeedsHall - 1))
	{
		return Fail(FString::Printf(TEXT("needs the %s"), *Faction.Buildings[Hall].Levels[Next.NeedsHall - 1].DisplayName.ToLower()));
	}
	if (GetLevel(Castle, EHexBuildingType::Fort) < Next.NeedsFort && Faction.Buildings.IsValidIndex(Fort) && Faction.Buildings[Fort].Levels.IsValidIndex(Next.NeedsFort - 1))
	{
		return Fail(FString::Printf(TEXT("needs the %s"), *Faction.Buildings[Fort].Levels[Next.NeedsFort - 1].DisplayName.ToLower()));
	}
	if (Each.Type == EHexBuildingType::Dwelling && Level == 0 && Each.NeedsDwelling >= 0 && GetLevel(Castle, EHexBuildingType::Dwelling, Each.NeedsDwelling) <= 0)
	{
		const int32 Needed = Faction.FindBuilding(EHexBuildingType::Dwelling, Each.NeedsDwelling);
		return Fail(FString::Printf(TEXT("needs the %s"), Faction.Buildings.IsValidIndex(Needed) ? *Faction.Buildings[Needed].Levels[0].DisplayName.ToLower() : TEXT("dwelling before it")));
	}
	const UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	if (!Sites || !Sites->CanPay(Castle.Owner, Next.Cost))
	{
		return Fail(TEXT("not enough resources"));
	}
	return true;
}

bool UHexCastles::Build(int32 CastleId, int32 Building, FString& OutMessage)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	FString Why;
	if (!Castle || !CanBuild(*Castle, Building, &Why))
	{
		OutMessage = Castle ? FString::Printf(TEXT("Not built: %s"), *Why) : FString(TEXT("There is no such castle"));
		return false;
	}
	const FHexBuilding& Each = Config->Factions[Castle->Faction].Buildings[Building];
	const FHexBuildingLevel& Next = Each.Levels[Castle->Levels[Building]];
	Heroes->GetSites()->Pay(Castle->Owner, Next.Cost);
	++Castle->Levels[Building];
	++Castle->BuiltToday;
	// a dwelling just built offers its first creatures at once
	if (Each.Type == EHexBuildingType::Dwelling && Castle->Levels[Building] == 1)
	{
		Castle->Available[Each.Creature] += GetGrowth(*Castle, Each.Creature);
	}
	Touch(*Castle);
	OutMessage = FString::Printf(TEXT("%s builds the %s"), *GetPlayerName(Castle->Owner), *Next.DisplayName.ToLower());
	UE_LOG(LogHexHero, Log, TEXT("Castles: castle %d, %s"), CastleId, *OutMessage);
	return true;
}

int32 UHexCastles::BuildAll(int32 CastleId)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	if (!Castle || !Config || !Config->Factions.IsValidIndex(Castle->Faction))
	{
		return 0;
	}
	int32 Built = 0;
	const TArray<FHexBuilding>& Buildings = Config->Factions[Castle->Faction].Buildings;
	for (int32 Building = 0; Building < Buildings.Num() && Castle->Levels.IsValidIndex(Building); ++Building)
	{
		const FHexBuilding& Each = Buildings[Building];
		const bool bNew = Castle->Levels[Building] == 0;
		Built += FMath::Max(Each.Levels.Num() - Castle->Levels[Building], 0);
		Castle->Levels[Building] = FMath::Max(Castle->Levels[Building], Each.Levels.Num());
		// a dwelling just built offers its first creatures at once
		if (bNew && Each.Type == EHexBuildingType::Dwelling && Castle->Available.IsValidIndex(Each.Creature))
		{
			Castle->Available[Each.Creature] += GetGrowth(*Castle, Each.Creature);
		}
	}
	Touch(*Castle);
	UE_LOG(LogHexHero, Log, TEXT("Castles: castle %d, a cheat builds %d levels of its buildings"), CastleId, Built);
	return Built;
}

int32 UHexCastles::GetHireMax(const FHexCastle& Castle, int32 Creature) const
{
	const FHexFaction& Faction = Config->Factions[Castle.Faction];
	const UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	const int32 Level = GetLevel(Castle, EHexBuildingType::Dwelling, Creature);
	if (!Sites || Level <= 0 || !Castle.Available.IsValidIndex(Creature) || !Faction.Creatures.IsValidIndex(Creature))
	{
		return 0;
	}
	const FHexCreature& Each = Faction.Creatures[Creature];
	const int32 Gold = Each.GetStats(Level >= 2).Gold;
	int32 Most = Castle.Available[Creature];
	if (Gold > 0)
	{
		Most = FMath::Min(Most, Sites->GetAmount(Castle.Owner, Sites->GetConfig()->FindResource(TEXT("Gold"))) / Gold);
	}
	if (!Each.CostResource.IsNone() && Each.CostResourceAmount > 0)
	{
		Most = FMath::Min(Most, Sites->GetAmount(Castle.Owner, Sites->GetConfig()->FindResource(Each.CostResource)) / Each.CostResourceAmount);
	}
	return FMath::Max(Most, 0);
}

bool UHexCastles::AddToArmy(TArray<FHexStack>& Army, const FHexStack& Stack)
{
	if (Stack.Count <= 0)
	{
		return true;
	}
	for (FHexStack& Each : Army)
	{
		if (Each.IsSameKind(Stack))
		{
			Each.Count += Stack.Count;
			return true;
		}
	}
	if (Army.Num() >= NumArmySlots)
	{
		return false;
	}
	Army.Add(Stack);
	return true;
}

bool UHexCastles::OpenGate(int32 CastleId, const FVector3d& Towards, FVector3d& OutGate, FVector3d& OutYard)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	int32 Gate = 0;
	if (!Castle || !FindGate(*Castle, Towards, OutGate, OutYard, &Gate))
	{
		return false;
	}
	Castle->GateTimes[Gate] = Surface && Surface->GetWorld() ? Surface->GetWorld()->GetTimeSeconds() : 0.0;
	bDirty[int32(Castle->Layer)] = true;
	return true;
}

bool UHexCastles::FindGate(const FHexCastle& Castle, const FVector3d& Towards, FVector3d& OutGate, FVector3d& OutYard, int32* OutIndex) const
{
	const AHexPlanet* Planet = GetPlanet(Castle.Layer);
	if (!Planet || !Planet->IsBuilt())
	{
		return false;
	}
	FGround Ground;
	MakeGround(*Planet, Castle, Ground);
	// the gate whose direction from the middle of the castle is nearest that of the point
	const FVector3d Offset = Towards.GetSafeNormal() - Ground.Up;
	const double Degrees = FMath::RadiansToDegrees(FMath::Atan2(FVector3d::DotProduct(Offset, Ground.Left), FVector3d::DotProduct(Offset, Ground.Forward)));
	const int32 NumGates = FMath::Clamp(Config->NumGates, 1, 6);
	int32 Best = 0;
	double BestDelta = 360.0;
	for (int32 Gate = 0; Gate < NumGates; ++Gate)
	{
		const double Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(Degrees, double(Config->GateAngle) + 360.0 * Gate / NumGates));
		if (Delta < BestDelta)
		{
			Best = Gate;
			BestDelta = Delta;
		}
	}
	const double GateDegrees = FMath::DegreesToRadians(double(Config->GateAngle) + 360.0 * Best / NumGates);
	OutGate = FVector3d(PlaceOn(*Planet, Castle, Ground, FVector2D(FMath::Cos(GateDegrees), FMath::Sin(GateDegrees)) * double(Config->WallRadius), 0.0f, 1.0f).GetLocation());
	OutYard = FVector3d(PlaceOn(*Planet, Castle, Ground, Config->GuardPlace, 0.0f, 1.0f).GetLocation());
	OutYard += OutYard.GetSafeNormal() * (double(Config->GuardLift) * Ground.Unit);
	if (OutIndex)
	{
		*OutIndex = Best;
	}
	return true;
}

void UHexCastles::GetGateCells(const FHexCastle& Castle, TArray<int32>& OutOutside, TArray<int32>& OutInside) const
{
	OutOutside.Reset();
	OutInside.Reset();
	const AHexPlanet* Planet = GetPlanet(Castle.Layer);
	if (!Planet || !Planet->IsBuilt() || !Config)
	{
		return;
	}
	FGround Ground;
	MakeGround(*Planet, Castle, Ground);
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const auto IsCastle = [&Castle](int32 Cell) { return Cell == Castle.Cells[0] || Cell == Castle.Cells[1] || Cell == Castle.Cells[2]; };
	const int32 NumGates = FMath::Clamp(Config->NumGates, 1, 6);
	for (int32 Gate = 0; Gate < NumGates; ++Gate)
	{
		const double Angle = FMath::DegreesToRadians(double(Config->GateAngle) + 360.0 * Gate / NumGates);
		const FVector2D Along(FMath::Cos(Angle), FMath::Sin(Angle));
		// the cell of the castle the gate is in, and the cell outside before it, a cell radius past the wall
		const FVector3d In = FVector3d(PlaceOn(*Planet, Castle, Ground, Along * (double(Config->WallRadius) * 0.8), 0.0f, 1.0f).GetLocation());
		const FVector3d Out = FVector3d(PlaceOn(*Planet, Castle, Ground, Along * (double(Config->WallRadius) + 1.0), 0.0f, 1.0f).GetLocation());
		int32 Inside = Planet->GetGrid().FindCell(In.GetSafeNormal());
		if (!IsCastle(Inside))
		{
			continue;
		}
		int32 Outside = Planet->GetGrid().FindCell(Out.GetSafeNormal());
		if (IsCastle(Outside) || !Cells.IsValidIndex(Outside) || !Cells[Inside].Neighbors.Contains(Outside))
		{
			// the cell outside beside the gate nearest the point before it
			Outside = INDEX_NONE;
			double Nearest = TNumericLimits<double>::Max();
			for (const int32 Neighbor : Cells[Inside].Neighbors)
			{
				const double Distance = FVector3d::DistSquared(Cells[Neighbor].Center, Out.GetSafeNormal());
				if (!IsCastle(Neighbor) && Distance < Nearest)
				{
					Nearest = Distance;
					Outside = Neighbor;
				}
			}
		}
		if (Outside != INDEX_NONE)
		{
			OutOutside.Add(Outside);
			OutInside.Add(Inside);
		}
	}
}

bool UHexCastles::BuyCatapult(int32 CastleId, int32 HeroId, FString& OutMessage)
{
	const FHexCastle* Castle = FindCastle(CastleId);
	FHexHero* Hero = Heroes ? Heroes->FindHeroMutable(HeroId) : nullptr;
	UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	const FHexHero* Visitor = Castle ? FindVisitor(*Castle) : nullptr;
	if (!Castle || !Hero || !Sites || !Visitor || Visitor->Id != HeroId || Hero->Player != Castle->Owner)
	{
		OutMessage = TEXT("Only a hero of the owner in the castle or beside it buys a catapult there");
		return false;
	}
	if (Hero->bCatapult)
	{
		OutMessage = FString::Printf(TEXT("%s has a catapult already"), *Hero->Name);
		return false;
	}
	if (!Sites->CanPay(Castle->Owner, Config->CatapultCost))
	{
		OutMessage = FString::Printf(TEXT("A catapult costs %s"), *Sites->DescribeCost(Config->CatapultCost));
		return false;
	}
	Sites->Pay(Castle->Owner, Config->CatapultCost);
	Hero->bCatapult = true;
	++Revision;
	OutMessage = FString::Printf(TEXT("%s has a catapult"), *Hero->Name);
	return true;
}

const FHexHero* UHexCastles::FindGuard(const FHexCastle& Castle) const
{
	const int32 Id = Castle.Id;
	return Heroes ? Heroes->GetHeroes().FindByPredicate([Id](const FHexHero& Each) { return Each.CastleId == Id; }) : nullptr;
}

bool UHexCastles::GetGuardPoint(const FHexCastle& Castle, FVector3d& OutPoint) const
{
	const AHexPlanet* Planet = GetPlanet(Castle.Layer);
	if (!Planet || !Planet->IsBuilt())
	{
		return false;
	}
	// on the ground of the yard
	FGround Ground;
	MakeGround(*Planet, Castle, Ground);
	// on the paved round there
	OutPoint = FVector3d(PlaceOn(*Planet, Castle, Ground, Config->GuardPlace, 0.0f, 1.0f).GetLocation());
	OutPoint += OutPoint.GetSafeNormal() * (double(Config->GuardLift) * Ground.Unit);
	return true;
}

const FHexHero* UHexCastles::FindVisitor(const FHexCastle& Castle) const
{
	const AHexPlanet* Planet = GetPlanet(Castle.Layer);
	if (!Planet || !Heroes || Castle.Owner == INDEX_NONE)
	{
		return nullptr;
	}
	if (const FHexHero* Guard = FindGuard(Castle); Guard && Guard->Player == Castle.Owner)
	{
		return Guard;
	}
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const FHexHero* Found = nullptr;
	for (const FHexHero& Hero : Heroes->GetHeroes())
	{
		if (Hero.Player != Castle.Owner || Hero.Layer != Castle.Layer || Hero.bWalking || !Cells.IsValidIndex(Hero.Cell))
		{
			continue;
		}
		const bool bBeside = Cells[Hero.Cell].Neighbors.ContainsByPredicate([&Castle](int32 Neighbor) { return Castle.HasCell(Neighbor); });
		if (bBeside && (!Found || Hero.Id == Heroes->GetSelectedId() || (Found->Id != Heroes->GetSelectedId() && Hero.Id < Found->Id)))
		{
			Found = &Hero;
		}
	}
	return Found;
}

bool UHexCastles::Hire(int32 CastleId, int32 Creature, int32 Count, int32 HeroId, FString& OutMessage)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	if (!Castle || Castle->Owner == INDEX_NONE || Count <= 0)
	{
		OutMessage = TEXT("There is no such castle to hire in");
		return false;
	}
	if (Count > GetHireMax(*Castle, Creature))
	{
		OutMessage = TEXT("Not hired: the dwelling does not offer that many, or they cost more than there is");
		return false;
	}
	const FHexCreature& Each = Config->Factions[Castle->Faction].Creatures[Creature];
	const bool bUpgraded = GetLevel(*Castle, EHexBuildingType::Dwelling, Creature) >= 2;
	const FHexStack Stack = {Castle->Faction, Creature, bUpgraded, Count};
	// into the army of a hero that stands beside the castle, else into the garrison
	const FHexHero* Visitor = HeroId != 0 ? FindVisitor(*Castle) : nullptr;
	FHexHero* Hero = Visitor && Visitor->Id == HeroId ? Heroes->FindHeroMutable(HeroId) : nullptr;
	if (HeroId != 0 && !Hero)
	{
		OutMessage = TEXT("Not hired: that hero does not stand beside the castle");
		return false;
	}
	if (!AddToArmy(Hero ? Hero->Army : Castle->Garrison, Stack))
	{
		OutMessage = FString::Printf(TEXT("Not hired: %s has no free slot"), Hero ? *Hero->Name : TEXT("the garrison"));
		return false;
	}
	TMap<FName, int32> Cost;
	Cost.Add(TEXT("Gold"), Each.GetStats(bUpgraded).Gold * Count);
	if (!Each.CostResource.IsNone() && Each.CostResourceAmount > 0)
	{
		Cost.Add(Each.CostResource, Each.CostResourceAmount * Count);
	}
	Heroes->GetSites()->Pay(Castle->Owner, Cost);
	Castle->Available[Creature] -= Count;
	Touch(*Castle);
	OutMessage = FString::Printf(TEXT("%s hires %s for %s"), *GetPlayerName(Castle->Owner), *DescribeStack(Stack), Hero ? *Hero->Name : TEXT("the garrison"));
	UE_LOG(LogHexHero, Log, TEXT("Castles: castle %d, %s"), CastleId, *OutMessage);
	return true;
}

bool UHexCastles::MoveStack(int32 CastleId, int32 HeroId, bool bToHero, int32 Slot, FString& OutMessage)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	const FHexHero* Visitor = Castle ? FindVisitor(*Castle) : nullptr;
	FHexHero* Hero = Visitor && Visitor->Id == HeroId ? Heroes->FindHeroMutable(HeroId) : nullptr;
	if (!Castle || !Hero)
	{
		OutMessage = TEXT("No hero of the castle stands beside it");
		return false;
	}
	TArray<FHexStack>& From = bToHero ? Castle->Garrison : Hero->Army;
	TArray<FHexStack>& To = bToHero ? Hero->Army : Castle->Garrison;
	if (!From.IsValidIndex(Slot))
	{
		OutMessage = TEXT("There is no such stack");
		return false;
	}
	if (!AddToArmy(To, From[Slot]))
	{
		OutMessage = FString::Printf(TEXT("%s has no free slot"), bToHero ? *Hero->Name : TEXT("The garrison"));
		return false;
	}
	From.RemoveAt(Slot);
	Touch(*Castle);
	return true;
}

float UHexCastles::GetPower(TConstArrayView<FHexStack> Army) const
{
	float Power = 0.0f;
	for (const FHexStack& Stack : Army)
	{
		const FHexCreatureStats* Stats = Config->GetStats(Stack);
		Power += Stats ? Stats->GetPower() * float(FMath::Max(Stack.Count, 0)) : 0.0f;
	}
	return Power;
}

float UHexCastles::GetFortFactor(const FHexCastle& Castle) const
{
	const FHexFaction& Faction = Config->Factions[Castle.Faction];
	const int32 Fort = Faction.FindBuilding(EHexBuildingType::Fort);
	const int32 FortLevel = GetLevel(Castle, EHexBuildingType::Fort);
	return Faction.Buildings.IsValidIndex(Fort) && Faction.Buildings[Fort].Levels.IsValidIndex(FortLevel - 1) ? Faction.Buildings[Fort].Levels[FortLevel - 1].DefenseFactor : 1.0f;
}

float UHexCastles::GetDefense(const FHexCastle& Castle) const
{
	const float Factor = GetFortFactor(Castle);
	// the hero in the castle guards it with its army
	const FHexHero* Guard = FindGuard(Castle);
	const float GuardPower = Guard ? GetPower(Guard->Army) * (1.0f + Config->HeroDefenseBonus * float(FMath::Max(Guard->Defense, 0))) : 0.0f;
	return (GetPower(Castle.Garrison) + GuardPower) * FMath::Max(Factor, 0.0f);
}

float UHexCastles::GetAttack(const FHexHero& Hero) const
{
	return GetPower(Hero.Army) * (1.0f + Config->HeroAttackBonus * float(FMath::Max(Hero.Attack, 0)));
}

bool UHexCastles::Resolve(float Attack, float Defense, float LossPower, float LossMax, float& OutLossShare)
{
	OutLossShare = 0.0f;
	if (Defense <= 0.0f)
	{
		return true;
	}
	if (Attack <= Defense)
	{
		return false;
	}
	OutLossShare = FMath::Clamp(FMath::Pow(Defense / Attack, FMath::Max(LossPower, 0.1f)), 0.0f, FMath::Clamp(LossMax, 0.0f, 1.0f));
	return true;
}

void UHexCastles::ApplyLosses(TArray<FHexStack>& Army, float Share)
{
	int32 Strongest = INDEX_NONE;
	for (int32 Index = 0; Index < Army.Num(); ++Index)
	{
		if (Strongest == INDEX_NONE || Army[Index].Creature > Army[Strongest].Creature)
		{
			Strongest = Index;
		}
		Army[Index].Count = FMath::RoundToInt32(float(Army[Index].Count) * (1.0f - FMath::Clamp(Share, 0.0f, 1.0f)));
	}
	// one creature is left at least, of the highest level
	if (Strongest != INDEX_NONE && !Army.ContainsByPredicate([](const FHexStack& Each) { return Each.Count > 0; }))
	{
		Army[Strongest].Count = 1;
	}
	Army.RemoveAll([](const FHexStack& Each) { return Each.Count <= 0; });
}

FString UHexCastles::Visit(FHexHero& Hero, int32 CastleId, int32 Cell, bool bShown)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	if (!Castle)
	{
		return FString();
	}
	const FString CastleName = FString::Printf(TEXT("castle of the %s"), *Config->Factions[Castle->Faction].DisplayName);
	// it goes in by the cell it was sent onto, stands in the yard and guards the castle
	FVector3d GatePoint = FVector3d::ZeroVector;
	FVector3d YardPoint = FVector3d::ZeroVector;
	const FVector3d Outside = Hero.Position;
	const bool bGate = bShown && !Outside.IsNearlyZero() && OpenGate(CastleId, Outside, GatePoint, YardPoint);
	// it sets out when the leaves have begun to swing
	const double Now = (Surface && Surface->GetWorld() ? Surface->GetWorld()->GetTimeSeconds() : 0.0) + double(Config->GateRiseTime * Config->GateWalkDelay);
	const auto Enter = [&]()
	{
		Hero.CastleId = CastleId;
		Hero.FromCell = Hero.Cell = Castle->HasCell(Cell) ? Cell : Castle->Cells[0];
		Hero.bPlaced = false;
		// it walks in through the gate nearest it, which opens for it
		if (bGate)
		{
			Hero.GlideStart = Now;
			Hero.GlideTime = Config->GateWalkTime;
			Hero.bGliding = true;
			Hero.GlidePoints[0] = Outside;
			Hero.GlidePoints[1] = GatePoint;
			Hero.GlidePoints[2] = YardPoint;
		}
		UHexHeroes::NoteMapChanged();
	};
	if (Castle->Owner == Hero.Player)
	{
		// its own: it goes in, and the hero that guarded the castle comes out to where this one stood, so the two swap places, as the
		// developer asked on 2026-11-11; a person looks into the castle
		const FHexHero* Guard = FindGuard(*Castle);
		FHexHero* Out = Guard && Guard->Id != Hero.Id ? Heroes->FindHeroMutable(Guard->Id) : nullptr;
		const int32 OutsideCell = Hero.Cell;
		Enter();
		if (Out)
		{
			Out->CastleId = 0;
			Out->FromCell = Out->Cell = OutsideCell;
			Out->bPlaced = false;
			// chosen, it is chosen alone now, without the castle it left
			if (Heroes->SelectedId == Out->Id)
			{
				Heroes->SelectedCastleId = 0;
			}
			// and walks out by the same gate, to where the other stood
			if (bGate)
			{
				Out->GlideStart = Now;
				Out->GlideTime = Config->GateWalkTime;
				Out->bGliding = true;
				Out->GlidePoints[0] = YardPoint;
				Out->GlidePoints[1] = GatePoint;
				Out->GlidePoints[2] = Outside;
			}
		}
		++Revision;
		if (!Heroes->IsPlayerAi(Hero.Player))
		{
			OnCastleOpened.Broadcast(CastleId);
		}
		return Out ? FString::Printf(TEXT("%s goes into the %s, and %s comes out"), *Hero.Name, *CastleName, *Out->Name)
			: FString::Printf(TEXT("%s goes into the %s and guards it"), *Hero.Name, *CastleName);
	}
	// until there are battles the stronger wins
	const float Attack = GetAttack(Hero);
	const float Defense = GetDefense(*Castle);
	float LossShare = 0.0f;
	if (!Resolve(Attack, Defense, Config->LossPower, Config->LossMax, LossShare))
	{
		return FString::Printf(TEXT("%s is too weak to take the %s: its army counts %.0f against %.0f behind the walls"), *Hero.Name, *CastleName, Attack, Defense);
	}
	const int32 Before = Castle->Owner;
	int32 CountBefore = 0;
	int32 CountAfter = 0;
	for (const FHexStack& Stack : Hero.Army)
	{
		CountBefore += Stack.Count;
	}
	if (Defense > 0.0f)
	{
		ApplyLosses(Hero.Army, LossShare);
	}
	for (const FHexStack& Stack : Hero.Army)
	{
		CountAfter += Stack.Count;
	}
	// the hero that guarded it is put out beside it without its army; what happens to a hero that is beaten waits for battles
	if (const FHexHero* Guard = FindGuard(*Castle))
	{
		FHexHero* Beaten = Heroes->FindHeroMutable(Guard->Id);
		const AHexPlanet* Planet = GetPlanet(Castle->Layer);
		Beaten->Army.Reset();
		Beaten->CastleId = 0;
		Beaten->bPlaced = false;
		for (int32 Index = 0; Planet && Index < 3 && Castle->HasCell(Beaten->Cell); ++Index)
		{
			for (const int32 Neighbor : Planet->GetGrid().GetCells()[Castle->Cells[Index]].Neighbors)
			{
				if (!Castle->HasCell(Neighbor) && Neighbor != Hero.Cell && Heroes->CanStand(Castle->Layer, Neighbor, 0))
				{
					Beaten->FromCell = Beaten->Cell = Neighbor;
					break;
				}
			}
		}
	}
	Castle->Garrison.Reset();
	Castle->Owner = Hero.Player;
	Castle->EventTime = Surface && Surface->GetWorld() ? Surface->GetWorld()->GetTimeSeconds() : 0.0;
	Enter();
	Touch(*Castle);
	UHexHeroes::NoteMapChanged();
	const FString Losses = CountBefore > CountAfter ? FString::Printf(TEXT(", losing %d of %d creatures"), CountBefore - CountAfter, CountBefore) : FString();
	if (bShown && Castle->bSeated && Heroes->GetSites())
	{
		const FVector3d Foot(Castle->Seat.GetLocation());
		const FVector3d Point = Foot + Foot.GetSafeNormal() * 90.0;
		Heroes->GetSites()->AddPopupAt(Castle->Layer, Point, FString::Printf(TEXT("Castle: %s"), *GetPlayerName(Hero.Player)), GetPlayerColor(Hero.Player));
		if (CountBefore > CountAfter)
		{
			Heroes->GetSites()->AddPopupAt(Castle->Layer, Point, FString::Printf(TEXT("-%d creatures"), CountBefore - CountAfter), FLinearColor(0.9f, 0.2f, 0.15f, 1.0f));
		}
	}
	return Before != INDEX_NONE
		? FString::Printf(TEXT("%s takes the %s of %s for %s%s"), *Hero.Name, *CastleName, *GetPlayerName(Before), *GetPlayerName(Hero.Player), *Losses)
		: FString::Printf(TEXT("%s takes the %s for %s%s"), *Hero.Name, *CastleName, *GetPlayerName(Hero.Player), *Losses);
}

int32 UHexCastles::GetPlayerFaction(int32 Player) const
{
	const FHexCastle* First = nullptr;
	for (const FHexCastle& Castle : Castles)
	{
		if (Castle.StartOwner == Player && (!First || Castle.Id < First->Id))
		{
			First = &Castle;
		}
	}
	return First ? First->Faction : Config->Factions.IsEmpty() ? 0 : FMath::Max(Player, 0) % Config->Factions.Num();
}

TArray<FHexStack> UHexCastles::MakeStartArmy(int32 Player) const
{
	TArray<FHexStack> Army;
	const int32 Faction = GetPlayerFaction(Player);
	if (!Config->Factions.IsValidIndex(Faction))
	{
		return Army;
	}
	for (int32 Creature = 0; Creature < Config->HeroStartWeeks.Num() && Creature < Config->Factions[Faction].Creatures.Num(); ++Creature)
	{
		const int32 Count = FMath::RoundToInt32(Config->Factions[Faction].Creatures[Creature].Growth * Config->HeroStartWeeks[Creature]);
		AddToArmy(Army, {Faction, Creature, false, Count});
	}
	return Army;
}

void UHexCastles::RunAi(int32 Player)
{
	for (int32 Index = 0; Index < Castles.Num(); ++Index)
	{
		if (Castles[Index].Owner != Player)
		{
			continue;
		}
		const int32 Id = Castles[Index].Id;
		const FHexFaction& Faction = Config->Factions[Castles[Index].Faction];
		// the dwellings first, from the lowest, then the fort and the hall, then what can be upgraded
		TArray<int32> Order;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 Building = 0; Building < Faction.Buildings.Num(); ++Building)
			{
				const bool bNewDwelling = Faction.Buildings[Building].Type == EHexBuildingType::Dwelling && Castles[Index].Levels[Building] == 0;
				if (bNewDwelling == (Pass == 0))
				{
					Order.Add(Building);
				}
			}
		}
		FString Message;
		for (const int32 Building : Order)
		{
			if (CanBuild(Castles[Index], Building) && Build(Id, Building, Message))
			{
				break;
			}
		}
		// and every creature it can pay for, the best first, into the garrison
		for (int32 Creature = NumCreatureLevels - 1; Creature >= 0; --Creature)
		{
			const int32 Count = GetHireMax(Castles[Index], Creature);
			if (Count > 0)
			{
				Hire(Id, Creature, Count, 0, Message);
			}
		}
	}
}

FString UHexCastles::DescribeStack(const FHexStack& Stack) const
{
	const FHexCreatureStats* Stats = Config->GetStats(Stack);
	return FString::Printf(TEXT("%d %s"), Stack.Count, Stats ? *Stats->DisplayName : TEXT("creatures"));
}

FString UHexCastles::DescribeArmy(TConstArrayView<FHexStack> Army) const
{
	FString Text;
	for (const FHexStack& Stack : Army)
	{
		Text += (Text.IsEmpty() ? TEXT("") : TEXT(", ")) + DescribeStack(Stack);
	}
	return Text.IsEmpty() ? FString(TEXT("none")) : Text;
}

FString UHexCastles::GetCellText(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexCastle* Castle = FindCastleAt(Layer, Cell);
	if (!Castle)
	{
		return FString();
	}
	return FString::Printf(TEXT("Castle: %s, of %s, guarded by %s"), *Config->Factions[Castle->Faction].DisplayName, *GetPlayerName(Castle->Owner), *DescribeArmy(Castle->Garrison));
}

void UHexCastles::MakeGround(const AHexPlanet& Planet, const FHexCastle& Castle, FGround& OutGround) const
{
	const TArray<FHexSphereCell>& Cells = Planet.GetGrid().GetCells();
	const FVector3d First = Cells[Castle.Cells[0]].Center;
	OutGround.Up = (First + Cells[Castle.Cells[1]].Center + Cells[Castle.Cells[2]].Center).GetSafeNormal();
	OutGround.Forward = (First - OutGround.Up * FVector3d::DotProduct(First, OutGround.Up)).GetSafeNormal();
	OutGround.Left = FVector3d::CrossProduct(OutGround.Up, OutGround.Forward);
	OutGround.Radius = FindGroundRadius(Planet, Castle, OutGround, OutGround.Up);
	// the middle of a cell lies a cell radius from the corner
	const double CellRadius = FVector3d::Distance(First * OutGround.Radius, OutGround.Up * FVector3d::DotProduct(First * OutGround.Radius, OutGround.Up));
	OutGround.Unit = CellRadius / 100.0 * double(Config->CastleScale);
}

double UHexCastles::FindGroundRadius(const AHexPlanet& Planet, const FHexCastle& Castle, FGround& Ground, const FVector3d& Direction) const
{
	// the triangles of the cell there once for all the pieces that stand on it, as AHexPlanet::FindGroundRadius would get them for each
	const int32 Cell = Planet.GetGrid().FindCell(Direction, Castle.Cells[0]);
	TArray<FVector3f>* Triangles = Ground.Triangles.Find(Cell);
	if (!Triangles)
	{
		Triangles = &Ground.Triangles.Add(Cell);
		Planet.GetCellTriangles(Cell, *Triangles);
	}
	for (int32 Corner = 0; Corner + 2 < Triangles->Num(); Corner += 3)
	{
		const FVector3d A((*Triangles)[Corner]);
		const FVector3d B((*Triangles)[Corner + 1]);
		const FVector3d C((*Triangles)[Corner + 2]);
		const FVector3d Normal = FVector3d::CrossProduct(B - A, C - A);
		const double Along = FVector3d::DotProduct(Normal, Direction);
		if (FMath::Abs(Along) < 1.0e-9)
		{
			continue;
		}
		const double Distance = FVector3d::DotProduct(Normal, A) / Along;
		const FVector3d Point = Direction * Distance;
		const auto Inside = [&Normal, &Point](const FVector3d& From, const FVector3d& To) { return FVector3d::DotProduct(FVector3d::CrossProduct(To - From, Point - From), Normal) >= -1.0e-6; };
		if (Distance > 0.0 && Inside(A, B) && Inside(B, C) && Inside(C, A))
		{
			return Distance;
		}
	}
	return Planet.GetCellTopRadius(Cell);
}

FTransform UHexCastles::PlaceOn(const AHexPlanet& Planet, const FHexCastle& Castle, FGround& Ground, const FVector2D& Place, float Yaw, float Scale) const
{
	const double Reach = 100.0 * Ground.Unit;
	const FVector3d Direction = (Ground.Up * Ground.Radius + (Ground.Forward * Place.X + Ground.Left * Place.Y) * Reach).GetSafeNormal();
	const double Turn = FMath::DegreesToRadians(double(Yaw));
	const FVector3d Facing = Ground.Forward * FMath::Cos(Turn) + Ground.Left * FMath::Sin(Turn);
	return FTransform(FRotationMatrix::MakeFromZX(FVector(Direction), FVector(Facing)).ToQuat(), FVector(Direction * FindGroundRadius(Planet, Castle, Ground, Direction)),
		FVector(double(Scale) * Ground.Unit));
}

void UHexCastles::Rebuild(EHexPlanetLayer Layer)
{
	AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet || !Planet->IsBuilt())
	{
		return;
	}
	if (!Material)
	{
		// the graph of the sites, which takes their values, and on the stone the masonry of the faction
		if (UMaterialInterface* Parent = Config->Material.LoadSynchronous())
		{
			Material = UMaterialInstanceDynamic::Create(Parent, this);
			HexMaterialConfig::Apply(Material, UHexSiteConfig::Load());
			HexMaterialConfig::Apply(Material, Config);
		}
	}
	TMap<FString, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Components = Instances[int32(Layer)];
	for (const TPair<FString, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Each : Components)
	{
		if (Each.Value.IsValid() && Each.Value->GetInstanceCount() > 0)
		{
			Each.Value->ClearInstances();
		}
	}
	// when the gate that is placed last opened, long ago for every other piece
	float GateTime = -1000.0f;
	// how near the two ends of the stretch of the wall that is placed are to a tower or a gate, by which its material shades it as a corner
	float CornerLow = 0.0f;
	float CornerHigh = 0.0f;
	const auto AddInstance = [&](const TSoftObjectPtr<UStaticMesh>& MeshAsset, const FTransform& Transform, const FHexCastle& Castle)
	{
		if (MeshAsset.IsNull())
		{
			return;
		}
		TWeakObjectPtr<UInstancedStaticMeshComponent>& Slot = Components.FindOrAdd(MeshAsset.ToSoftObjectPath().ToString());
		if (!Slot.IsValid())
		{
			UStaticMesh* Mesh = MeshAsset.LoadSynchronous();
			if (!Mesh)
			{
				return;
			}
			UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Planet);
			Component->SetupAttachment(Planet->GetRootComponent());
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetStaticMesh(Mesh);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetNumCustomDataFloats(11);
			Component->SetCullDistances(0, int32(Config->CullDistance));
			if (Material)
			{
				Component->SetMaterial(0, Material);
			}
			Component->RegisterComponent();
			Slot = Component;
		}
		// the roofs and the flag in the color of its owner, as the developer asked on 2026-11-11, when it was taken, and the masonry of
		// its faction
		const FLinearColor OwnerColor = GetPlayerColor(Castle.Owner);
		const float Data[11] = {OwnerColor.R, OwnerColor.G, OwnerColor.B, OwnerColor.R, OwnerColor.G, OwnerColor.B, float(Castle.EventTime), Config->Factions[Castle.Faction].WallStyle, GateTime,
			CornerLow, CornerHigh};
		Slot->SetCustomData(Slot->AddInstance(Transform, true), MakeArrayView(Data), true);
	};
	const auto GetMesh = [](const TArray<TSoftObjectPtr<UStaticMesh>>& Meshes, int32 Level) { return Meshes.IsValidIndex(Level) ? Meshes[Level] : TSoftObjectPtr<UStaticMesh>(); };

	for (FHexCastle& Castle : Castles)
	{
		if (Castle.Layer != Layer || !Planet->GetGrid().GetCells().IsValidIndex(Castle.Cells[0]))
		{
			continue;
		}
		// every piece stands on the ground where it lies and follows it: the castle has no ground of its own
		FGround Ground;
		MakeGround(*Planet, Castle, Ground);
		Castle.Seat = FTransform(FRotationMatrix::MakeFromZX(FVector(Ground.Up), FVector(Ground.Forward)).ToQuat(), FVector(Ground.Up * Ground.Radius), FVector(Ground.Unit));
		Castle.bSeated = true;

		// its wall along a circle, by the level of its fort, with a gap for its gate, and its towers evenly around
		const int32 Fort = FMath::Clamp(GetLevel(Castle, EHexBuildingType::Fort), 0, 3);
		const double Rim = double(Config->WallRadius);
		const auto AtRim = [Rim](double Degrees) { return FVector2D(Rim * FMath::Cos(FMath::DegreesToRadians(Degrees)), Rim * FMath::Sin(FMath::DegreesToRadians(Degrees))); };
		const int32 NumGates = FMath::Clamp(Config->NumGates, 1, 6);
		const auto IsGate = [this, NumGates](double Degrees)
		{
			for (int32 Gate = 0; Gate < NumGates; ++Gate)
			{
				if (FMath::Abs(FMath::FindDeltaAngleDegrees(Degrees, double(Config->GateAngle) + 360.0 * Gate / NumGates)) < double(Config->GateHalfWidth))
				{
					return true;
				}
			}
			return false;
		};
		const int32 Segments = FMath::Max(Config->WallSegments, 6);
		const int32 Towers = Config->TowerCounts.IsValidIndex(Fort) ? Config->TowerCounts[Fort] : 0;
		// how much of a corner the wall is at an angle: 1 at a tower or the edge of a gate, fading along the wall
		const auto GetCorner = [this, NumGates, Towers, &IsGate](double Degrees)
		{
			double Nearest = 360.0;
			for (int32 Gate = 0; Gate < NumGates; ++Gate)
			{
				Nearest = FMath::Min(Nearest, FMath::Abs(FMath::FindDeltaAngleDegrees(Degrees, double(Config->GateAngle) + 360.0 * Gate / NumGates)) - double(Config->GateHalfWidth));
			}
			for (int32 Tower = 0; Tower < Towers; ++Tower)
			{
				const double At = double(Config->GateAngle) + 360.0 * (Tower + 0.5) / Towers;
				if (!IsGate(At))
				{
					Nearest = FMath::Min(Nearest, FMath::Abs(FMath::FindDeltaAngleDegrees(Degrees, At)) - double(Config->WallCornerInset));
				}
			}
			return float(FMath::Clamp(1.0 - Nearest / FMath::Max(double(Config->WallCornerReach), 0.1), 0.0, 1.0));
		};
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const double Degrees = double(Config->GateAngle) + 360.0 * Segment / Segments;
			if (!IsGate(Degrees))
			{
				CornerLow = GetCorner(Degrees - 180.0 / Segments);
				CornerHigh = GetCorner(Degrees + 180.0 / Segments);
				AddInstance(GetMesh(Config->WallMeshes, Fort), PlaceOn(*Planet, Castle, Ground, AtRim(Degrees), float(Degrees), 1.0f), Castle);
			}
		}
		CornerLow = 0.0f;
		CornerHigh = 0.0f;
		for (int32 Gate = 0; Gate < NumGates; ++Gate)
		{
			// its leaves swing open when a hero goes through it
			const double Degrees = double(Config->GateAngle) + 360.0 * Gate / NumGates;
			GateTime = float(Castle.GateTimes[Gate]);
			AddInstance(GetMesh(Config->GateMeshes, Fort), PlaceOn(*Planet, Castle, Ground, AtRim(Degrees), float(Degrees), 1.0f), Castle);
			GateTime = -1000.0f;
		}
		for (int32 Tower = 0; Tower < Towers; ++Tower)
		{
			// between the stretches of the wall, and none in a gate
			const double Degrees = double(Config->GateAngle) + 360.0 * (Tower + 0.5) / Towers;
			if (!IsGate(Degrees))
			{
				AddInstance(GetMesh(Config->TowerMeshes, Fort), PlaceOn(*Planet, Castle, Ground, AtRim(Degrees), float(Degrees), 1.0f), Castle);
			}
		}
		AddInstance(Config->FlagMesh, PlaceOn(*Planet, Castle, Ground, Config->FlagPlace, Config->GateAngle, 1.0f), Castle);
		// the place of the hero that guards it
		AddInstance(Config->GuardMesh, PlaceOn(*Planet, Castle, Ground, Config->GuardPlace, Config->GateAngle, 1.0f), Castle);

		// and each building that is built at its place: the mesh of the highest level built that has one
		const FHexFaction& Faction = Config->Factions[Castle.Faction];
		for (int32 Building = 0; Building < Faction.Buildings.Num() && Building < Castle.Levels.Num(); ++Building)
		{
			const FHexBuilding& Each = Faction.Buildings[Building];
			const int32 Level = FMath::Min(Castle.Levels[Building], Each.Levels.Num());
			if (Level <= 0)
			{
				continue;
			}
			// higher than its mesh by its own factor
			FTransform Transform = PlaceOn(*Planet, Castle, Ground, Each.Place, Each.Yaw, Each.Scale);
			Transform.SetScale3D(Transform.GetScale3D() * FVector(1.0, 1.0, double(Each.HeightScale)));
			int32 Shown = INDEX_NONE;
			for (int32 Index = Level - 1; Index >= 0 && Shown == INDEX_NONE; --Index)
			{
				Shown = Each.Levels[Index].Mesh.IsNull() ? int32(INDEX_NONE) : Index;
			}
			if (Shown != INDEX_NONE)
			{
				AddInstance(Each.Levels[Shown].Mesh, Transform, Castle);
			}
			// a level built over the one whose mesh shows is marked
			if (Shown != INDEX_NONE && Shown < Level - 1)
			{
				AddInstance(Config->UpgradeMesh, Transform, Castle);
			}
		}
	}
}

void UHexCastles::Update()
{
	if (!Surface || !Config)
	{
		return;
	}
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		const AHexPlanet* Planet = GetPlanet(EHexPlanetLayer(Layer));
		const bool bBuilt = Planet && Planet->IsBuilt();
		if (bBuilt != bLayerBuilt[Layer])
		{
			bLayerBuilt[Layer] = bBuilt;
			OnLayerRebuilt(EHexPlanetLayer(Layer));
		}
		if (bDirty[Layer] && bBuilt)
		{
			bDirty[Layer] = false;
			Rebuild(EHexPlanetLayer(Layer));
		}
	}
}

namespace
{
	/** hex.Castle: places castles and plays them, for trying them from the console and from -ExecCmds */
	void RunCastleCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		AHexPlanet* Planet = Layers ? Layers->GetPresentedBuilt() : nullptr;
		UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
		UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
		if (!Castles || !Castles->GetConfig() || Args.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Castle <list|add <Cell> <Faction> [Player]|remove <Cell>|build <Castle> <Building>|hire <Castle> <Level 1-7> <Count|max> [Hero]|move <Castle> <Hero> <tohero|togarrison> <Slot>|open <Castle>|give <Player> <Resource> <Amount>>, on a built planet"));
			return;
		}
		const UHexCastleConfig* Config = Castles->GetConfig();
		const EHexPlanetLayer Layer = Planet->GetLayer();
		const auto GetInt = [&Args](int32 Index, int32 Default) { return Args.IsValidIndex(Index) && Args[Index].IsNumeric() ? FCString::Atoi(*Args[Index]) : Default; };
		const FString Action = Args[0].ToLower();
		FString Message;
		if (Action == TEXT("list"))
		{
			UE_LOG(LogHexHero, Log, TEXT("Castles: %d on the planet"), Castles->GetCastles().Num());
			for (const FHexCastle& Castle : Castles->GetCastles())
			{
				const FHexFaction& Faction = Config->Factions[Castle.Faction];
				FString Built;
				for (int32 Building = 0; Building < Faction.Buildings.Num() && Building < Castle.Levels.Num(); ++Building)
				{
					if (Castle.Levels[Building] > 0)
					{
						Built += FString::Printf(TEXT(" %s %d"), *Faction.Buildings[Building].Id.ToString(), Castle.Levels[Building]);
					}
				}
				FString Offered;
				for (int32 Creature = 0; Creature < Castle.Available.Num(); ++Creature)
				{
					Offered += FString::Printf(TEXT(" %d"), Castle.Available[Creature]);
				}
				UE_LOG(LogHexHero, Log, TEXT("  %d %s, cells %d %d %d of the %s, owner %d, built:%s, offers:%s, garrison: %s (%.0f behind the walls)"), Castle.Id, *Faction.DisplayName,
					Castle.Cells[0], Castle.Cells[1], Castle.Cells[2], GetPlanetLayerName(Castle.Layer), Castle.Owner, *Built, *Offered, *Castles->DescribeArmy(Castle.Garrison),
					Castles->GetDefense(Castle));
			}
			for (const FHexHero& Hero : Heroes->GetHeroes())
			{
				UE_LOG(LogHexHero, Log, TEXT("  %s: %s (%.0f)"), *Hero.Name, *Castles->DescribeArmy(Hero.Army), Castles->GetAttack(Hero));
			}
		}
		else if (Action == TEXT("add") && Args.Num() >= 3)
		{
			int32 Cells[3];
			const int32 Faction = Config->FindFaction(Args[2]);
			if (!Castles->FindCells(Layer, GetInt(1, INDEX_NONE), FVector3d::ZeroVector, Cells) || Castles->AddCastle(Layer, Cells, Faction, GetInt(3, INDEX_NONE), Message) == 0)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Castle: no castle of %s at cell %s: %s"), *Args[2], *Args[1], *Message);
			}
		}
		else if (Action == TEXT("remove") && Args.Num() >= 2)
		{
			if (!Castles->RemoveCastleAt(Layer, GetInt(1, INDEX_NONE)))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Castle: no castle on cell %s"), *Args[1]);
			}
		}
		else if (Action == TEXT("build") && Args.Num() >= 3)
		{
			const FHexCastle* Castle = Castles->FindCastle(GetInt(1, 0));
			const int32 Building = Castle ? Config->Factions[Castle->Faction].Buildings.IndexOfByPredicate([&Args](const FHexBuilding& Each) { return Each.Id.ToString().Equals(Args[2], ESearchCase::IgnoreCase); }) : int32(INDEX_NONE);
			Castles->Build(GetInt(1, 0), Building, Message);
			UE_LOG(LogHexHero, Log, TEXT("hex.Castle: %s"), *Message);
		}
		else if (Action == TEXT("hire") && Args.Num() >= 4)
		{
			const FHexCastle* Castle = Castles->FindCastle(GetInt(1, 0));
			const int32 Creature = GetInt(2, 1) - 1;
			const FHexHero* Hero = Args.IsValidIndex(4) ? Heroes->FindHeroByText(Args[4]) : nullptr;
			const int32 Count = Castle && Args[3].Equals(TEXT("max"), ESearchCase::IgnoreCase) ? Castles->GetHireMax(*Castle, Creature) : GetInt(3, 0);
			Castles->Hire(GetInt(1, 0), Creature, Count, Hero ? Hero->Id : 0, Message);
			UE_LOG(LogHexHero, Log, TEXT("hex.Castle: %s"), *Message);
		}
		else if (Action == TEXT("move") && Args.Num() >= 5)
		{
			const FHexHero* Hero = Heroes->FindHeroByText(Args[2]);
			const bool bMoved = Castles->MoveStack(GetInt(1, 0), Hero ? Hero->Id : 0, Args[3].Equals(TEXT("tohero"), ESearchCase::IgnoreCase), GetInt(4, 0), Message);
			UE_LOG(LogHexHero, Log, TEXT("hex.Castle: %s"), bMoved ? TEXT("the stack is moved") : *Message);
		}
		else if (Action == TEXT("start") && Args.Num() >= 2)
		{
			// what is built in a castle and its garrison now become what it has when a game starts, which the save keeps; all for every castle
			int32 Kept = 0;
			TArray<int32> Ids;
			for (const FHexCastle& Castle : Castles->GetCastles())
			{
				if (Args[1].Equals(TEXT("all"), ESearchCase::IgnoreCase) || Castle.Id == FCString::Atoi(*Args[1]))
				{
					Ids.Add(Castle.Id);
				}
			}
			for (const int32 Id : Ids)
			{
				Kept += Castles->KeepAsStart(Id) ? 1 : 0;
			}
			UE_LOG(LogHexHero, Log, TEXT("hex.Castle: %d castles start as they are now"), Kept);
		}
		else if (Action == TEXT("open") && Args.Num() >= 2)
		{
			Castles->OnCastleOpened.Broadcast(GetInt(1, 0));
		}
		else if (Action == TEXT("give") && Args.Num() >= 4)
		{
			Heroes->GetSites()->AddAmount(GetInt(1, 0), FName(*Args[2]), GetInt(3, 0));
			UE_LOG(LogHexHero, Log, TEXT("hex.Castle: player %d has %d more %s"), GetInt(1, 0), GetInt(3, 0), *Args[2]);
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Castle: unknown action %s"), *Args[0]);
		}
	}

	/** hex.Cheat: what the developer asked for on 2026-11-11 to try the castles, more resources and building without the limit of a day */
	void RunCheatCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		AHexPlanet* Planet = Layers ? Layers->GetPresentedBuilt() : nullptr;
		UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
		UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
		UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
		// with nothing after it, or help, it lists what there is, as the developer asked on 2026-11-12
		if (Args.IsEmpty() || Args[0].Equals(TEXT("help"), ESearchCase::IgnoreCase) || Args[0] == TEXT("?"))
		{
			HexCheats::LogHelp();
			return;
		}
		if (!Castles || !Castles->GetConfig() || !Sites || !Sites->GetConfig())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Cheat works on a built planet; hex.Cheat help lists the cheats"));
			return;
		}
		const FString Action = Args[0].ToLower();
		FString Message;
		if (Action == TEXT("reveal"))
		{
			// the whole of both layers explored for the person who plays, or the player named
			const int32 Player = Args.IsValidIndex(1) && Args[1].IsNumeric() ? FCString::Atoi(*Args[1]) : Heroes->GetShroudPlayer();
			const int32 Cells = Heroes->RevealAll(Player);
			Message = FString::Printf(TEXT("Cheat: player %d has explored the whole map, %d cells more"), Player, Cells);
		}
		else if (Action == TEXT("enemies"))
		{
			const bool bOn = Args.IsValidIndex(1) ? Args[1].Equals(TEXT("on"), ESearchCase::IgnoreCase) || Args[1] == TEXT("1") : !HexCheats::AreEnemiesMarked();
			HexCheats::SetEnemiesMarked(bOn);
			Message = bOn ? TEXT("Cheat: the enemies are marked") : TEXT("Cheat off: the enemies are not marked");
		}
		else if (Action == TEXT("resources"))
		{
			// to the person who plays, or to the player named: the amount named of each, or what DA_HexCastles gives
			const bool bAmount = Args.IsValidIndex(1) && Args[1].IsNumeric();
			const int32 Player = Args.IsValidIndex(2) && Args[2].IsNumeric() ? FCString::Atoi(*Args[2]) : Heroes->GetShroudPlayer();
			const TArray<FHexResourceInfo>& Resources = Sites->GetConfig()->Resources;
			for (int32 Resource = 0; Resource < Resources.Num(); ++Resource)
			{
				const int32 Amount = bAmount ? FCString::Atoi(*Args[1]) : Resource == 0 ? Castles->GetConfig()->CheatGold : Castles->GetConfig()->CheatResources;
				Sites->AddAmount(Player, Resources[Resource].Id, Amount);
				Message += FString::Printf(TEXT("%s+%d %s"), Resource == 0 ? TEXT("") : TEXT(", "), Amount, *Resources[Resource].DisplayName);
			}
			Castles->NoteChanged();
			Message = FString::Printf(TEXT("Cheat: player %d gets %s"), Player, *Message);
		}
		else if (Action == TEXT("build"))
		{
			const bool bOn = Args.IsValidIndex(1) ? Args[1].Equals(TEXT("on"), ESearchCase::IgnoreCase) || Args[1] == TEXT("1") : !Castles->HasUnlimitedBuilds();
			Castles->SetUnlimitedBuilds(bOn);
			Message = bOn ? TEXT("Cheat: castles build without a limit") : TEXT("Cheat off: a castle builds once a day");
		}
		else if (Action == TEXT("buildall"))
		{
			// everything in the castle named, in the one chosen, else in every castle of the person who plays, and with all in every castle
			const bool bAll = Args.IsValidIndex(1) && Args[1].Equals(TEXT("all"), ESearchCase::IgnoreCase);
			const int32 Named = Args.IsValidIndex(1) && Args[1].IsNumeric() ? FCString::Atoi(*Args[1]) : Heroes->GetSelectedCastleId();
			int32 Count = 0;
			int32 Built = 0;
			TArray<int32> Ids;
			for (const FHexCastle& Castle : Castles->GetCastles())
			{
				if (bAll || (Named > 0 ? Castle.Id == Named : Castle.Owner == Heroes->GetShroudPlayer()))
				{
					Ids.Add(Castle.Id);
				}
			}
			for (int32 Id : Ids)
			{
				Built += Castles->BuildAll(Id);
				++Count;
			}
			Castles->NoteChanged();
			Message = FString::Printf(TEXT("Cheat: everything built in %d %s, %d levels"), Count, Count == 1 ? TEXT("castle") : TEXT("castles"), Built);
		}
		else if (Action == TEXT("units") && Args.Num() >= 4)
		{
			// creatures for the hero chosen, or the one named: units <Faction> <Level 1-7> <Count> [up] [Hero], as the developer asked on 2026-11-12
			const int32 Faction = Castles->GetConfig()->FindFaction(Args[1]);
			const bool bUpgraded = Args.IsValidIndex(4) && Args[4].Equals(TEXT("up"), ESearchCase::IgnoreCase);
			const FString HeroText = Args.IsValidIndex(bUpgraded ? 5 : 4) ? Args[bUpgraded ? 5 : 4] : FString();
			const FHexHero* Hero = HeroText.IsEmpty() ? Heroes->GetSelected() : Heroes->FindHeroByText(HeroText);
			const FHexStack Stack{Faction, FCString::Atoi(*Args[2]) - 1, bUpgraded, FCString::Atoi(*Args[3])};
			const FHexCreatureStats* Stats = Faction != INDEX_NONE ? Castles->GetConfig()->GetStats(Stack) : nullptr;
			if (!Hero || !Stats)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Cheat units <Faction> <Level 1-7> <Count> [up] [Hero]: no such %s"), Hero ? TEXT("creature") : TEXT("hero, and none is chosen"));
				return;
			}
			if (!Heroes->AddUnits(Hero->Id, Stack))
			{
				Message = FString::Printf(TEXT("Cheat: the army of %s has no room for %s"), *Hero->Name, *Stats->DisplayName);
			}
			else
			{
				Message = FString::Printf(TEXT("Cheat: %s gets %d %s"), *Hero->Name, Stack.Count, *Stats->DisplayName);
			}
		}
		else if (Action == TEXT("passive"))
		{
			// in the battle the person fights now, the side of the game attacks nobody and defends on every turn, for that battle alone
			UHexBattles* Battles = Heroes->GetBattles();
			const int32 Person = Battles && Battles->IsShown() ? Battles->GetPersonSide() : int32(INDEX_NONE);
			if (Person == INDEX_NONE)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Cheat passive [on|off]: only in a battle a person fights"));
				return;
			}
			FHexBattle& Battle = Battles->GetBattle();
			const int32 Enemy = 1 - Person;
			const bool bOn = Args.IsValidIndex(1) ? Args[1].Equals(TEXT("on"), ESearchCase::IgnoreCase) : Battle.GetPassiveSide() != Enemy;
			Battle.SetPassiveSide(bOn ? Enemy : int32(INDEX_NONE));
			Message = bOn ? FString::Printf(TEXT("Cheat: %s attacks nobody and defends in this battle"), *Battles->GetSideName(Enemy))
				: FString::Printf(TEXT("Cheat: %s fights again"), *Battles->GetSideName(Enemy));
		}
		else if (Action == TEXT("catapult"))
		{
			// a catapult for the hero chosen, or the one named, for nothing
			const FHexHero* Hero = Args.IsValidIndex(1) ? Heroes->FindHeroByText(Args[1]) : Heroes->GetSelected();
			if (!Hero || !Heroes->SetCatapult(Hero->Id, true))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Cheat catapult [Hero]: no such hero, and none is chosen"));
				return;
			}
			Message = FString::Printf(TEXT("Cheat: %s has a catapult"), *Hero->Name);
		}
		else if (Action == TEXT("buildhalf"))
		{
			// to the middle of the levels, in the castle named, the one chosen, or with all in every castle
			const bool bAll = Args.IsValidIndex(1) && Args[1].Equals(TEXT("all"), ESearchCase::IgnoreCase);
			const int32 Named = Args.IsValidIndex(1) && Args[1].IsNumeric() ? FCString::Atoi(*Args[1]) : Heroes->GetSelectedCastleId();
			int32 Built = 0;
			int32 Count = 0;
			TArray<int32> Ids;
			for (const FHexCastle& Castle : Castles->GetCastles())
			{
				if (bAll || Castle.Id == Named)
				{
					Ids.Add(Castle.Id);
				}
			}
			for (const int32 Id : Ids)
			{
				Built += Castles->BuildHalf(Id);
				++Count;
			}
			Message = FString::Printf(TEXT("Cheat: %d %s built to the middle, %d levels"), Count, Count == 1 ? TEXT("castle") : TEXT("castles"), Built);
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Cheat: unknown action %s; hex.Cheat help lists the cheats"), *Args[0]);
			return;
		}
		UE_LOG(LogHexHero, Log, TEXT("hex.Cheat: %s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 4.0f, FColor::Yellow, Message);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CheatCommand(TEXT("hex.Cheat"),
		TEXT("Cheats for trying the game; help, or nothing after it, lists them all (also hex.Cheat.Help and Ctrl+H): reveal [Player] explores both layers for the person who plays; enemies [on|off] marks every neutral stack and hero of another player; resources [Amount] [Player] gives the person who plays, or the player named, that much of every resource, or without an amount CheatGold and CheatResources of DA_HexCastles; build [on|off] lets every castle build without the limit of one building a day, or switches that; buildall [Castle|all] builds every building to its last level for nothing, in the castle named, else in the one chosen, else in every castle of the person who plays, and with all in every castle; buildhalf <Castle|all> builds to the middle (the hall and the fort to half their levels, the four lower dwellings upgraded, the fifth built); units <Faction> <Level 1-7> <Count> [up] [Hero] gives the hero chosen, or the one named, that many creatures; passive [on|off], in a battle the person fights, makes the side of the game attack nobody and defend on every turn, for that battle alone."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCheatCommand));

	FAutoConsoleCommandWithWorldAndArgs CastleCommand(TEXT("hex.Castle"),
		TEXT("The castles of the planet: list (with the armies of the heroes), add <Cell> <Faction> [Player] on the layer shown, at the cell and the two around its first corner, remove <Cell>, build <Castle> <Building> (Hall, Fort, Dwelling1 to Dwelling7), hire <Castle> <Level 1-7> <Count|max> [Hero], move <Castle> <Hero> <tohero|togarrison> <Slot>, open <Castle> (its panel), give <Player> <Resource> <Amount>. A faction is its Id in DA_HexCastles, Dawn or Decay."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCastleCommand));
}
