// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexSiteGenerator.h"
#include "HexSphereGrid.h"

namespace
{
	/** A site placed, for the distances the next ones keep from it */
	struct FPlaced
	{
		FVector3d Center = FVector3d::ZeroVector;
		int32 Kind = 0;
		bool bMine = false;
	};

	/** A layer while its sites are found */
	struct FSiteLayer
	{
		/** The cells a hero can still stand on, without those of the sites placed */
		TBitArray<> Open;

		/** The cells a site may take */
		TBitArray<> Candidate;

		/** Those cells in a random order, and where the search for the next site goes on */
		TArray<int32> Order;
		int32 Cursor = 0;

		int32 NumCells = 0;
		TArray<FPlaced> Placed;

		/** The sites placed of each kind, by its place in the kinds of the input */
		TArray<int32> Counts;
	};

	void Shuffle(TArray<int32>& Values, FRandomStream& Stream)
	{
		for (int32 Index = Values.Num() - 1; Index > 0; --Index)
		{
			Values.Swap(Index, Stream.RandRange(0, Index));
		}
	}
}

bool HexSiteGenerator::KeepsWays(const FHexSphereGrid& Grid, const TBitArray<>& Open, int32 Cell)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (!Cells.IsValidIndex(Cell))
	{
		return false;
	}
	// the neighbors go around the cell, each beside the next, so those that are open are joined without the cell while they lie in one run
	const auto& Neighbors = Cells[Cell].Neighbors;
	int32 NumOpen = 0;
	int32 Runs = 0;
	for (int32 Index = 0; Index < Neighbors.Num(); ++Index)
	{
		const int32 Next = Neighbors[(Index + 1) % Neighbors.Num()];
		const bool bOpen = Open.IsValidIndex(Neighbors[Index]) && Open[Neighbors[Index]];
		NumOpen += bOpen ? 1 : 0;
		Runs += bOpen && !(Open.IsValidIndex(Next) && Open[Next]) ? 1 : 0;
	}
	return NumOpen > 0 && Runs <= 1;
}

void HexSiteGenerator::Generate(const FHexSphereGrid& Grid, const FInput& Input, TArray<FSite>& OutSites, FReport* OutReport)
{
	OutSites.Reset();
	FReport Report;
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const int32 NumCells = Cells.Num();
	const FHexSiteGeneration& Settings = Input.Settings;
	FRandomStream Stream(Input.Seed);

	// the angle between two cells side by side, by which the distances between sites are counted in cells
	double StepSum = 0.0;
	int32 StepCount = 0;
	for (const FHexSphereCell& Cell : Cells)
	{
		for (const int32 Neighbor : Cell.Neighbors)
		{
			StepSum += FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Cell.Center, Cells[Neighbor].Center), -1.0, 1.0));
			++StepCount;
		}
	}
	const double Step = StepCount > 0 ? StepSum / StepCount : 0.0;

	FSiteLayer Layers[NumPlanetLayers];
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		FSiteLayer& Each = Layers[Layer];
		Each.Counts.Init(0, Input.Kinds.Num());
		const TBitArray<>& Walkable = Input.Walkable[Layer];
		const TBitArray<>& Avoid = Input.Avoid[Layer];
		if (NumCells == 0 || Walkable.Num() != NumCells)
		{
			continue;
		}
		Each.Open = Walkable;
		Each.Candidate.Init(false, NumCells);

		// the pieces of ground a hero walks, and only those large enough take sites
		TArray<int32> Piece;
		Piece.Init(INDEX_NONE, NumCells);
		TArray<int32> Queue;
		for (int32 First = 0; First < NumCells; ++First)
		{
			if (!Walkable[First] || Piece[First] != INDEX_NONE)
			{
				continue;
			}
			Queue.Reset();
			Queue.Add(First);
			Piece[First] = First;
			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				for (const int32 Neighbor : Cells[Queue[Index]].Neighbors)
				{
					if (Walkable[Neighbor] && Piece[Neighbor] == INDEX_NONE)
					{
						Piece[Neighbor] = First;
						Queue.Add(Neighbor);
					}
				}
			}
			if (Queue.Num() < Settings.MinRegionCells)
			{
				continue;
			}
			Each.NumCells += Queue.Num();
			for (const int32 Cell : Queue)
			{
				if (!Cells[Cell].IsPentagon() && !(Avoid.IsValidIndex(Cell) && Avoid[Cell]))
				{
					Each.Candidate[Cell] = true;
					Each.Order.Add(Cell);
				}
			}
		}
		Each.Order.Sort();
		Shuffle(Each.Order, Stream);
		Report.Cells[Layer] = Each.NumCells;
	}

	const float Spacing = FMath::Max(Settings.Spacing, 2.0f);
	// bNearStart for a mine a player gets where it starts, which may stand nearer the same mine of a player that starts beside it
	const auto TryPlace = [&](int32 Layer, int32 Cell, int32 KindIndex, bool bNearStart)
	{
		FSiteLayer& Each = Layers[Layer];
		const FKind& Kind = Input.Kinds[KindIndex];
		if (!Each.Candidate.IsValidIndex(Cell) || !Each.Candidate[Cell] || !Each.Open[Cell] || !KeepsWays(Grid, Each.Open, Cell))
		{
			return false;
		}
		const bool bMine = Kind.Type == EHexSiteType::Mine;
		for (const FPlaced& Other : Each.Placed)
		{
			const float Apart = bMine && Other.bMine ? FMath::Max(Spacing, Other.Kind == Kind.Kind && !bNearStart ? Settings.SameMineSpacing : Settings.MineSpacing) : Spacing;
			// half a cell less, as the cells that many steps away lie a little nearer than that many widths of a cell
			if (FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Cells[Cell].Center, Other.Center), -1.0, 1.0)) < (double(Apart) - 0.5) * Step)
			{
				return false;
			}
		}
		Each.Open[Cell] = false;
		Each.Placed.Add({Cells[Cell].Center, Kind.Kind, bMine});
		++Each.Counts[KindIndex];
		OutSites.Add({Kind.Kind, Layer, Cell});
		return true;
	};

	// near where each player starts: a mine of each kind that every player needs, and some piles
	for (const FStart& Start : Input.Starts)
	{
		if (Start.Layer < 0 || Start.Layer >= NumPlanetLayers || !Layers[Start.Layer].Open.IsValidIndex(Start.Cell) || !Layers[Start.Layer].Open[Start.Cell])
		{
			continue;
		}
		FSiteLayer& Each = Layers[Start.Layer];
		const int32 MaxSteps = FMath::Max(Settings.StartMaxSteps, 1);
		const int32 MinSteps = FMath::Clamp(Settings.StartMinSteps, 1, MaxSteps);
		// the cells that many steps of a hero away, in a random order; found again for each site, as a site placed closes its cell
		const auto FindNear = [&](TArray<int32>& OutNear)
		{
			OutNear.Reset();
			TMap<int32, int32> Steps;
			TArray<int32> Queue;
			Queue.Add(Start.Cell);
			Steps.Add(Start.Cell, 0);
			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				const int32 Cell = Queue[Index];
				const int32 Here = Steps[Cell];
				if (Here >= MinSteps)
				{
					OutNear.Add(Cell);
				}
				if (Here >= MaxSteps)
				{
					continue;
				}
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					if (Each.Open[Neighbor] && !Steps.Contains(Neighbor))
					{
						Steps.Add(Neighbor, Here + 1);
						Queue.Add(Neighbor);
					}
				}
			}
			Shuffle(OutNear, Stream);
		};
		const auto PlaceNear = [&](int32 KindIndex)
		{
			TArray<int32> Near;
			FindNear(Near);
			for (const int32 Cell : Near)
			{
				if (TryPlace(Start.Layer, Cell, KindIndex, true))
				{
					++Report.NearStarts;
					return;
				}
			}
			++Report.NoRoom;
		};
		TArray<int32> Piles;
		float PileSum = 0.0f;
		for (int32 KindIndex = 0; KindIndex < Input.Kinds.Num(); ++KindIndex)
		{
			const FKind& Kind = Input.Kinds[KindIndex];
			if (Kind.PerThousand[Start.Layer] <= 0.0f)
			{
				continue;
			}
			if (Kind.bNearStart)
			{
				PlaceNear(KindIndex);
			}
			else if (Kind.Type == EHexSiteType::Pile)
			{
				Piles.Add(KindIndex);
				PileSum += Kind.PerThousand[Start.Layer];
			}
		}
		// piles of kinds by their numbers on the planet
		for (int32 Pile = 0; Pile < Settings.StartPiles && !Piles.IsEmpty(); ++Pile)
		{
			float Pick = Stream.FRand() * PileSum;
			int32 Chosen = Piles.Last();
			for (const int32 KindIndex : Piles)
			{
				Pick -= Input.Kinds[KindIndex].PerThousand[Start.Layer];
				if (Pick <= 0.0f)
				{
					Chosen = KindIndex;
					break;
				}
			}
			PlaceNear(Chosen);
		}
	}

	// then what each layer still lacks of each kind, the mines first, as they keep the furthest apart, then the mills, then the piles
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		FSiteLayer& Each = Layers[Layer];
		if (Each.Order.IsEmpty())
		{
			continue;
		}
		TArray<int32> Wanted[3];
		for (int32 KindIndex = 0; KindIndex < Input.Kinds.Num(); ++KindIndex)
		{
			const FKind& Kind = Input.Kinds[KindIndex];
			// a number with a fraction is one more by that chance, so a small planet gets some of the rare kinds too
			const float Number = FMath::Max(Kind.PerThousand[Layer], 0.0f) * FMath::Max(Settings.Density, 0.0f) * float(Each.NumCells) / 1000.0f;
			const int32 Whole = FMath::FloorToInt32(Number);
			const int32 Count = Whole + (Stream.FRand() < Number - float(Whole) ? 1 : 0) - Each.Counts[KindIndex];
			const int32 Group = Kind.Type == EHexSiteType::Mine ? 0 : Kind.Type == EHexSiteType::Mill ? 1 : 2;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Wanted[Group].Add(KindIndex);
			}
		}
		for (TArray<int32>& Group : Wanted)
		{
			Shuffle(Group, Stream);
			for (const int32 KindIndex : Group)
			{
				// the next cell of the random order that takes it; not all of a large planet for a kind that finds no room
				const int32 Tries = FMath::Min(Each.Order.Num(), 20000);
				bool bPlaced = false;
				for (int32 Try = 0; Try < Tries && !bPlaced; ++Try)
				{
					const int32 Index = (Each.Cursor + Try) % Each.Order.Num();
					if (TryPlace(Layer, Each.Order[Index], KindIndex, false))
					{
						Each.Cursor = (Index + 1) % Each.Order.Num();
						bPlaced = true;
					}
				}
				Report.NoRoom += bPlaced ? 0 : 1;
			}
		}
	}

	if (OutReport)
	{
		*OutReport = Report;
	}
}
