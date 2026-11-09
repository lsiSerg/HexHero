// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetUnderground.h"
#include "Algo/Reverse.h"
#include "HexHero.h"
#include "HexSphereGrid.h"

namespace
{
	using namespace HexPlanetUnderground;

	/** Owner of a void cell */
	constexpr int8 NoOwner = -1;

	/** Distance between the centers of two neighbors on the unit sphere of a grid of frequency 1, which shrinks as 1 / Frequency */
	constexpr double CellSpacing = 1.2;

	/** Calls Visit for each pair of neighbors of a cell that must not both be void, or it is a neck: the opposite sides of a hexagon, and the
	 *  sides of a pentagon that are not next to each other */
	template <typename FVisit>
	void ForEachNeckPair(const FHexSphereCell& Cell, FVisit&& Visit)
	{
		const int32 Count = Cell.Neighbors.Num();
		if (Count == 6)
		{
			for (int32 Side = 0; Side < 3; ++Side)
			{
				Visit(Cell.Neighbors[Side], Cell.Neighbors[Side + 3]);
			}
			return;
		}
		for (int32 Side = 0; Side < Count; ++Side)
		{
			Visit(Cell.Neighbors[Side], Cell.Neighbors[(Side + 2) % Count]);
		}
	}

	/** How far apart two directions are, for comparing: 1 - the dot of them */
	double GetSpread(const FVector3d& A, const FVector3d& B)
	{
		return 1.0 - FVector3d::DotProduct(A, B);
	}

	/** Returns a direction made tangent to the sphere at a point, of length 1, or zero */
	FVector3d MakeTangent(const FVector3d& Direction, const FVector3d& Point)
	{
		return (Direction - Point * FVector3d::DotProduct(Direction, Point)).GetSafeNormal();
	}

	/** A step of the search for the cheapest way, by its cost so far */
	struct FStep
	{
		double Cost = 0.0;
		int32 Cell = INDEX_NONE;
	};

	/** Orders the steps of the search cheapest first, ties by cell, so the way never depends on the heap */
	bool IsCheaper(const FStep& A, const FStep& B)
	{
		return A.Cost != B.Cost ? A.Cost < B.Cost : A.Cell < B.Cell;
	}

	/** The maze while it is carved: which network owns each passage cell, and what a way through each cell costs */
	struct FMaze
	{
		FMaze(const FHexSphereGrid& Grid, const FHexUndergroundGeneration& InGeneration)
			: Cells(Grid.GetCells())
			, Generation(InGeneration)
			, Gap(FMath::Max(InGeneration.NetworkGap, 1))
			, Width(FMath::Max(InGeneration.PassageWidth, 2))
		{
			Owner.Init(NoOwner, Cells.Num());
			Cost.Init(1.0, Cells.Num());
			OtherDistance.Init(MAX_int32, Cells.Num());
		}

		const TArray<FHexSphereCell>& Cells;
		const FHexUndergroundGeneration& Generation;

		/** Void cells kept between two networks */
		int32 Gap = 3;

		/** Width of a passage */
		int32 Width = 2;

		/** Network of each passage cell, NoOwner for the void */
		TArray<int8> Owner;

		/** Cost of a way through each cell */
		TArray<double> Cost;

		/** Steps from each cell to the passages of the other networks than the one carved, up to one past Gap and the rings a wider passage
		 *  grows by, else MAX_int32 (FindOtherDistance) */
		TArray<int32> OtherDistance;

		/** Network being carved */
		int8 Network = 0;

		/** Scratch of the search */
		TArray<double> Distance;
		TArray<int32> Previous;
		TArray<uint8> InTree;

		int32 CountOwnNeighbors(int32 Cell) const
		{
			int32 Count = 0;
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				Count += Owner[Neighbor] == Network ? 1 : 0;
			}
			return Count;
		}

		bool TouchesOther(int32 Cell) const
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (Owner[Neighbor] != NoOwner && Owner[Neighbor] != Network)
				{
					return true;
				}
			}
			return false;
		}

		/** Returns true if the network carved may take a void cell, which never touches another network, as that would join them */
		bool CanCarve(int32 Cell) const
		{
			return Owner[Cell] == NoOwner && OtherDistance[Cell] > 1;
		}

		void FindOtherDistance()
		{
			OtherDistance.Init(MAX_int32, Cells.Num());
			TArray<int32> Front;
			for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
			{
				if (Owner[Cell] != NoOwner && Owner[Cell] != Network)
				{
					OtherDistance[Cell] = 0;
					Front.Add(Cell);
				}
			}
			for (int32 Index = 0; Index < Front.Num(); ++Index)
			{
				const int32 Cell = Front[Index];
				if (OtherDistance[Cell] > Gap + (Width - 1) / 2)
				{
					continue;
				}
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					if (OtherDistance[Neighbor] == MAX_int32)
					{
						OtherDistance[Neighbor] = OtherDistance[Cell] + 1;
						Front.Add(Neighbor);
					}
				}
			}
		}

		/** Carves the hall of HallRings rings around an exit, its outer ring ragged; the exit always, as it must be a passage, even next to another
		 *  network, which the report then shows as joined */
		void CarveHall(int32 Exit, FRandomStream& Random)
		{
			if (Owner[Exit] == NoOwner)
			{
				Owner[Exit] = Network;
			}
			TArray<int32> Seen = {Exit};
			TArray<int32> Front = {Exit};
			const int32 Rings = FMath::Max(Generation.HallRings, 0);
			for (int32 Ring = 1; Ring <= Rings; ++Ring)
			{
				TArray<int32> Next;
				for (const int32 Cell : Front)
				{
					for (const int32 Neighbor : Cells[Cell].Neighbors)
					{
						if (Seen.Contains(Neighbor))
						{
							continue;
						}
						Seen.Add(Neighbor);
						Next.Add(Neighbor);
						const bool bKept = Ring < Rings || Random.FRand() < Generation.HallEdgeShare;
						if (bKept && Owner[Neighbor] == NoOwner && !TouchesOther(Neighbor) && CountOwnNeighbors(Neighbor) > 0)
						{
							Owner[Neighbor] = Network;
						}
					}
				}
				Front = MoveTemp(Next);
			}
		}

		/** Returns the passage of the network carved, not on the tree, that the cheapest way from the tree reaches, with Previous leading back to
		 *  the tree, or INDEX_NONE. A way never enters a cell within Clear of another network, nor next to one, and pays GapPenalty of the generation a cell within
		 *  NetworkGap */
		int32 FindWay(TConstArrayView<int32> Tree, int32 Clear)
		{
			Distance.Init(TNumericLimits<double>::Max(), Cells.Num());
			Previous.Init(INDEX_NONE, Cells.Num());
			TArray<FStep> Heap;
			for (const int32 Cell : Tree)
			{
				Distance[Cell] = 0.0;
				Heap.HeapPush(FStep{0.0, Cell}, IsCheaper);
			}

			while (!Heap.IsEmpty())
			{
				FStep Step;
				Heap.HeapPop(Step, IsCheaper);
				if (Step.Cost > Distance[Step.Cell])
				{
					continue;
				}
				if (Owner[Step.Cell] == Network && !InTree[Step.Cell])
				{
					return Step.Cell;
				}
				for (const int32 Neighbor : Cells[Step.Cell].Neighbors)
				{
					double Add = 0.0;
					if (Owner[Neighbor] != Network)
					{
						if (!CanCarve(Neighbor) || OtherDistance[Neighbor] <= Clear)
						{
							continue;
						}
						Add = Cost[Neighbor] + (OtherDistance[Neighbor] <= Gap ? Generation.GapPenalty : 0.0);
					}
					if (Step.Cost + Add < Distance[Neighbor])
					{
						Distance[Neighbor] = Step.Cost + Add;
						Previous[Neighbor] = Step.Cell;
						Heap.HeapPush(FStep{Step.Cost + Add, Neighbor}, IsCheaper);
					}
				}
			}
			return INDEX_NONE;
		}

		/**
		 *  Joins the exits of the network carved, as Prim does: the tree is the passage its first exit lies in, and the cheapest way from anywhere
		 *  on it to any other passage of the network joins that one too, until every exit is on it. Adds the cells it carved, a line one cell
		 *  wide, to Carved. Returns false if an exit cannot be reached
		 */
		bool Connect(TConstArrayView<int32> Exits, TArray<int32>& Carved)
		{
			while (!Exits.IsEmpty())
			{
				InTree.Init(0, Cells.Num());
				TArray<int32> Tree = {Exits[0]};
				InTree[Exits[0]] = 1;
				for (int32 Index = 0; Index < Tree.Num(); ++Index)
				{
					for (const int32 Neighbor : Cells[Tree[Index]].Neighbors)
					{
						if (Owner[Neighbor] == Network && !InTree[Neighbor])
						{
							InTree[Neighbor] = 1;
							Tree.Add(Neighbor);
						}
					}
				}
				if (!Exits.ContainsByPredicate([this](int32 Exit) { return !InTree[Exit]; }))
				{
					return true;
				}

				// first keeping NetworkGap and the rings a wider passage grows by clear of the other networks, and only if that finds no way
				// through them at a high cost, never next to them
				int32 Reached = FindWay(Tree, Gap + (Width - 1) / 2);
				if (Reached == INDEX_NONE)
				{
					Reached = FindWay(Tree, 0);
				}

				if (Reached == INDEX_NONE)
				{
					return false;
				}
				for (int32 Cell = Previous[Reached]; Cell != INDEX_NONE && !InTree[Cell]; Cell = Previous[Cell])
				{
					if (Owner[Cell] == NoOwner)
					{
						Owner[Cell] = Network;
						Carved.Add(Cell);
					}
				}
			}
			return true;
		}

		/**
		 *  Widens the passages of the network carved around the cells of a queue: a passage wider than 2 first grows by whole rings if asked,
		 *  then every neck takes one of its two void cells, the one with more passage around it, so bends fill in rather than reach out, else
		 *  the cheaper. A cell next to another network is never taken, so a neck there stays and the report shows it
		 */
		void Widen(TArray<int32>& Queue, bool bGrow)
		{
			TArray<int32> Front = Queue;
			for (int32 Ring = 0; bGrow && Ring < (Width - 1) / 2; ++Ring)
			{
				TArray<int32> Next;
				for (const int32 Cell : Front)
				{
					for (const int32 Neighbor : Cells[Cell].Neighbors)
					{
						if (CanCarve(Neighbor))
						{
							Owner[Neighbor] = Network;
							Next.Add(Neighbor);
							Queue.Add(Neighbor);
						}
					}
				}
				Front = MoveTemp(Next);
			}

			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				const int32 Cell = Queue[Index];
				if (Owner[Cell] != Network)
				{
					continue;
				}
				int32 Best = INDEX_NONE;
				int32 BestCount = 0;
				ForEachNeckPair(Cells[Cell], [this, &Best, &BestCount](int32 A, int32 B)
				{
					if (Owner[A] != NoOwner || Owner[B] != NoOwner)
					{
						return;
					}
					for (const int32 Candidate : {A, B})
					{
						if (!CanCarve(Candidate))
						{
							continue;
						}
						const int32 Count = CountOwnNeighbors(Candidate);
						if (Best == INDEX_NONE || Count > BestCount || (Count == BestCount && (Cost[Candidate] < Cost[Best] || (Cost[Candidate] == Cost[Best] && Candidate < Best))))
						{
							Best = Candidate;
							BestCount = Count;
						}
					}
				});
				if (Best != INDEX_NONE)
				{
					// the cell may have another neck, and the new one may be one
					Owner[Best] = Network;
					Queue.Add(Cell);
					Queue.Add(Best);
				}
			}
		}

		/** Adds DeadEnds branches to the network carved: each leaves a wall of its passage and goes on, more or less straight, through void that
		 *  touches no passage but its own end, so it never joins one again, and NetworkGap away from other networks; then it is widened */
		void AddDeadEnds(FRandomStream& Random)
		{
			const int32 MaxLength = FMath::Max(Generation.DeadEndLength, 1);
			TArray<int32> Own;
			for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
			{
				if (Owner[Cell] == Network)
				{
					Own.Add(Cell);
				}
			}
			if (Own.IsEmpty())
			{
				return;
			}

			for (int32 Branch = 0; Branch < Generation.DeadEnds; ++Branch)
			{
				TArray<int32> Carved;
				for (int32 Try = 0; Try < 30 && Carved.IsEmpty(); ++Try)
				{
					const int32 Start = Own[Random.RandHelper(Own.Num())];
					TArray<int32> Options;
					for (const int32 Neighbor : Cells[Start].Neighbors)
					{
						if (Owner[Neighbor] == NoOwner && OtherDistance[Neighbor] > Gap && CountOwnNeighbors(Neighbor) <= 2)
						{
							Options.Add(Neighbor);
						}
					}
					if (Options.IsEmpty())
					{
						continue;
					}

					int32 Cell = Options[Random.RandHelper(Options.Num())];
					FVector3d Heading = MakeTangent(Cells[Cell].Center - Cells[Start].Center, Cells[Cell].Center);
					Owner[Cell] = Network;
					Carved.Add(Cell);
					const int32 Length = Random.RandRange(FMath::Max(MaxLength / 2, 1), MaxLength);

					while (Carved.Num() < Length)
					{
						int32 Next = INDEX_NONE;
						double BestScore = -TNumericLimits<double>::Max();
						for (const int32 Neighbor : Cells[Cell].Neighbors)
						{
							if (Owner[Neighbor] != NoOwner || OtherDistance[Neighbor] <= Gap)
							{
								continue;
							}
							bool bAlone = true;
							for (const int32 Around : Cells[Neighbor].Neighbors)
							{
								bAlone &= Around == Cell || Owner[Around] == NoOwner;
							}
							if (!bAlone)
							{
								continue;
							}
							const double Score = FVector3d::DotProduct((Cells[Neighbor].Center - Cells[Cell].Center).GetSafeNormal(), Heading) + Random.FRand() * Generation.DeadEndWander;
							if (Score > BestScore)
							{
								BestScore = Score;
								Next = Neighbor;
							}
						}
						if (Next == INDEX_NONE)
						{
							break;
						}
						Heading = MakeTangent(Heading + (Cells[Next].Center - Cells[Cell].Center).GetSafeNormal() * Generation.DeadEndTurn, Cells[Next].Center);
						Owner[Next] = Network;
						Carved.Add(Next);
						Cell = Next;
					}
				}
				Own.Append(Carved);
				Widen(Carved, true);
			}
		}
	};

	/** Gives the maze the cost of a way through each cell: lowest along the lines where a noise crosses zero, winding curves about WiggleCells
	 *  cells apart on a planet of any size, so the ways follow them and meander rather than run along great circles */
	void InitCost(FMaze& Maze, const FHexSphereGrid& Grid, const FHexUndergroundGeneration& Generation, FRandomStream& Random)
	{
		const TArray<FHexSphereCell>& Cells = Grid.GetCells();
		const FVector3d Offset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
		const double Scale = Grid.GetFrequency() / (CellSpacing * FMath::Max(Generation.WiggleCells, 0.5));
		const double Wiggle = FMath::Max(Generation.Wiggle, 0.0);
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			const FVector3d Point = Cells[Cell].Center * Scale + Offset;
			const double Noise = FMath::PerlinNoise3D(Point) * 0.75 + FMath::PerlinNoise3D(Point * 2.3 + FVector3d(17.0, -31.0, 5.0)) * 0.25;
			Maze.Cost[Cell] = 1.0 + Wiggle * FMath::Clamp(FMath::Abs(Noise) * Generation.WiggleSharpness, 0.0, 1.0);
		}
	}

	/** Returns the exits of the entrances that can be used, by network, whatever lies under them now */
	void GetUsableExits(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, TConstArrayView<FHexEntrance> Entrances, TArray<int32> (&OutExits)[HexPlanetFile::MaxNetworks])
	{
		for (int32 Index = 0; Index < Entrances.Num(); ++Index)
		{
			if (GetEntranceProblem(Grid, SurfaceLevels, {}, Entrances, Index) == EEntranceProblem::None)
			{
				OutExits[Entrances[Index].Network].Add(Entrances[Index].Cell);
			}
		}
	}

	/**
	 *  Carves the networks of a mask into the maze, which may hold passages already, owned by other networks: the halls around their exits first,
	 *  so the ways of every network keep clear of all of them, then each network, those with the most entrances first, as they have the longest
	 *  ways to find, joined, widened and given dead ends, and a last pass over the necks alone, as a later network may have kept an earlier one
	 *  from widening
	 */
	void CarveNetworks(FMaze& Maze, const TArray<int32> (&Exits)[HexPlanetFile::MaxNetworks], uint32 Mask, FRandomStream& Random)
	{
		for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
		{
			if (Mask & (1u << Network))
			{
				Maze.Network = int8(Network);
				for (const int32 Exit : Exits[Network])
				{
					Maze.CarveHall(Exit, Random);
				}
			}
		}

		TArray<int32> Order;
		for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
		{
			if ((Mask & (1u << Network)) && !Exits[Network].IsEmpty())
			{
				Order.Add(Network);
			}
		}
		Order.StableSort([&Exits](int32 A, int32 B) { return Exits[A].Num() > Exits[B].Num(); });

		const auto OwnCells = [&Maze](TArray<int32>& Out)
		{
			for (int32 Cell = 0; Cell < Maze.Owner.Num(); ++Cell)
			{
				if (Maze.Owner[Cell] == Maze.Network)
				{
					Out.Add(Cell);
				}
			}
		};

		for (const int32 Network : Order)
		{
			Maze.Network = int8(Network);
			Maze.FindOtherDistance();
			TArray<int32> OwnExits;
			for (const int32 Exit : Exits[Network])
			{
				if (Maze.Owner[Exit] == Network)
				{
					OwnExits.Add(Exit);
				}
			}
			TArray<int32> Queue;
			OwnCells(Queue);
			if (!Maze.Connect(OwnExits, Queue))
			{
				UE_LOG(LogHexHero, Warning, TEXT("Underground: the exits of network %c cannot all be joined"), HexPlanetFile::GetNetworkLetter(Network));
			}
			Maze.Widen(Queue, true);
			Maze.AddDeadEnds(Random);
		}

		for (const int32 Network : Order)
		{
			Maze.Network = int8(Network);
			Maze.FindOtherDistance();
			TArray<int32> Queue;
			OwnCells(Queue);
			Maze.Widen(Queue, false);
		}
	}

	/** Gives the levels of the maze carved, its biomes and how it fits together to a result */
	void FinishResult(const FMaze& Maze, const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, int32 Seed, double StartTime, FResult& OutResult)
	{
		OutResult.Levels.SetNumUninitialized(Maze.Owner.Num());
		for (int32 Cell = 0; Cell < Maze.Owner.Num(); ++Cell)
		{
			OutResult.Levels[Cell] = Maze.Owner[Cell] == NoOwner ? VoidLevel : PassageLevel;
		}
		GenerateBiomes(Grid, OutResult.Levels, OutResult.Entrances, Maze.Generation, Seed, OutResult.Biomes);
		OutResult.Report = Check(Grid, SurfaceLevels, OutResult.Levels, OutResult.Entrances);
		OutResult.Seconds = FPlatformTime::Seconds() - StartTime;
	}

	/** Returns the piece of passage of each cell, joined through neighbors, INDEX_NONE for the void, and how many pieces there are */
	int32 FindPieces(const TArray<FHexSphereCell>& Cells, TConstArrayView<int8> Levels, TArray<int32>& OutPiece)
	{
		OutPiece.Init(INDEX_NONE, Cells.Num());
		int32 Count = 0;
		for (int32 First = 0; First < Cells.Num(); ++First)
		{
			if (!IsPassage(Levels[First]) || OutPiece[First] != INDEX_NONE)
			{
				continue;
			}
			TArray<int32> Front = {First};
			OutPiece[First] = Count;
			for (int32 Index = 0; Index < Front.Num(); ++Index)
			{
				for (const int32 Neighbor : Cells[Front[Index]].Neighbors)
				{
					if (IsPassage(Levels[Neighbor]) && OutPiece[Neighbor] == INDEX_NONE)
					{
						OutPiece[Neighbor] = Count;
						Front.Add(Neighbor);
					}
				}
			}
			++Count;
		}
		return Count;
	}
}

const TCHAR* HexPlanetUnderground::GetProblemText(EEntranceProblem Problem)
{
	switch (Problem)
	{
	case EEntranceProblem::None: return TEXT("fine");
	case EEntranceProblem::NoCell: return TEXT("no cell of the planet");
	case EEntranceProblem::NoNetwork: return TEXT("no network from A to H");
	case EEntranceProblem::Duplicate: return TEXT("another entrance on its cell");
	case EEntranceProblem::NotLand: return TEXT("not on plains or hills");
	case EEntranceProblem::Pentagon: return TEXT("on a pentagon");
	case EEntranceProblem::ExitInVoid: return TEXT("its exit is in the void");
	}
	return TEXT("");
}

bool HexPlanetUnderground::CanHoldEntrance(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, int32 Cell)
{
	return Grid.GetCells().IsValidIndex(Cell) && SurfaceLevels.IsValidIndex(Cell) && (SurfaceLevels[Cell] == 0 || SurfaceLevels[Cell] == 1)
		&& !Grid.GetCells()[Cell].IsPentagon();
}

HexPlanetUnderground::EEntranceProblem HexPlanetUnderground::GetEntranceProblem(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels,
	TConstArrayView<int8> UndergroundLevels, TConstArrayView<FHexEntrance> Entrances, int32 Index)
{
	const FHexEntrance& Entrance = Entrances[Index];
	if (!Grid.GetCells().IsValidIndex(Entrance.Cell) || !SurfaceLevels.IsValidIndex(Entrance.Cell))
	{
		return EEntranceProblem::NoCell;
	}
	if (Entrance.Network < 0 || Entrance.Network >= HexPlanetFile::MaxNetworks)
	{
		return EEntranceProblem::NoNetwork;
	}
	for (int32 Other = 0; Other < Index; ++Other)
	{
		if (Entrances[Other].Cell == Entrance.Cell)
		{
			return EEntranceProblem::Duplicate;
		}
	}
	if (SurfaceLevels[Entrance.Cell] != 0 && SurfaceLevels[Entrance.Cell] != 1)
	{
		return EEntranceProblem::NotLand;
	}
	if (Grid.GetCells()[Entrance.Cell].IsPentagon())
	{
		return EEntranceProblem::Pentagon;
	}
	if (UndergroundLevels.IsValidIndex(Entrance.Cell) && !IsPassage(UndergroundLevels[Entrance.Cell]))
	{
		return EEntranceProblem::ExitInVoid;
	}
	return EEntranceProblem::None;
}

TArray<FHexEntrance> HexPlanetUnderground::GetSavedEntrances(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, TConstArrayView<int8> UndergroundLevels,
	TConstArrayView<FHexEntrance> Entrances)
{
	TArray<FHexEntrance> Saved;
	for (int32 Index = 0; Index < Entrances.Num(); ++Index)
	{
		if (GetEntranceProblem(Grid, SurfaceLevels, UndergroundLevels, Entrances, Index) == EEntranceProblem::None)
		{
			Saved.Add(Entrances[Index]);
		}
	}
	Saved.Sort([](const FHexEntrance& A, const FHexEntrance& B) { return A.Cell < B.Cell; });
	return Saved;
}

bool HexPlanetUnderground::IsNeck(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, int32 Cell)
{
	if (!IsPassage(Levels[Cell]))
	{
		return false;
	}
	bool bNeck = false;
	ForEachNeckPair(Grid.GetCells()[Cell], [&Levels, &bNeck](int32 A, int32 B)
	{
		bNeck |= !IsPassage(Levels[A]) && !IsPassage(Levels[B]);
	});
	return bNeck;
}

int32 HexPlanetUnderground::FReport::CountNetworks() const
{
	int32 Count = 0;
	for (const FNetworkReport& Network : Networks)
	{
		Count += Network.Entrances > 0 ? 1 : 0;
	}
	return Count;
}

bool HexPlanetUnderground::FReport::IsGood() const
{
	for (const FNetworkReport& Network : Networks)
	{
		if (Network.Entrances > 0 && Network.Components != 1)
		{
			return false;
		}
	}
	return Merged.IsEmpty() && ExitsInVoid.IsEmpty() && BadEntrances.IsEmpty() && Necks.IsEmpty() && OrphanCells == 0;
}

FString HexPlanetUnderground::FReport::ToString() const
{
	FString Text = FString::Printf(TEXT("%d networks ("), CountNetworks());
	bool bFirst = true;
	for (int32 Index = 0; Index < HexPlanetFile::MaxNetworks; ++Index)
	{
		const FNetworkReport& Network = Networks[Index];
		if (Network.Entrances == 0)
		{
			continue;
		}
		Text += FString::Printf(TEXT("%s%c: %d entrances, %d cells%s"), bFirst ? TEXT("") : TEXT("; "), HexPlanetFile::GetNetworkLetter(Index), Network.Entrances,
			Network.Cells, Network.Components == 1 ? TEXT("") : *FString::Printf(TEXT(", in %d pieces"), Network.Components));
		bFirst = false;
	}
	const int32 CellCount = FMath::Max(CellNetworks.Num(), 1);
	Text += FString::Printf(TEXT("), %d passage cells of %d (%.1f%%), %d joined pairs of networks, %d exits in the void, %d bad entrances, %d necks, %d passage cells no exit reaches"),
		PassageCells, CellNetworks.Num(), 100.0 * PassageCells / CellCount, Merged.Num(), ExitsInVoid.Num(), BadEntrances.Num(), Necks.Num(), OrphanCells);
	return Text;
}

HexPlanetUnderground::FReport HexPlanetUnderground::Check(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, TConstArrayView<int8> Levels,
	TConstArrayView<FHexEntrance> Entrances)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	FReport Report;
	Report.CellNetworks.Init(INDEX_NONE, Cells.Num());
	if (Levels.Num() != Cells.Num())
	{
		return Report;
	}

	// the pieces of passage, each cell joined to its neighbors
	TArray<int32> Component;
	Component.Init(INDEX_NONE, Cells.Num());
	TArray<int32> ComponentSizes;
	for (int32 First = 0; First < Cells.Num(); ++First)
	{
		if (!IsPassage(Levels[First]) || Component[First] != INDEX_NONE)
		{
			continue;
		}
		const int32 Id = ComponentSizes.Add(0);
		TArray<int32> Front = {First};
		Component[First] = Id;
		for (int32 Index = 0; Index < Front.Num(); ++Index)
		{
			for (const int32 Neighbor : Cells[Front[Index]].Neighbors)
			{
				if (IsPassage(Levels[Neighbor]) && Component[Neighbor] == INDEX_NONE)
				{
					Component[Neighbor] = Id;
					Front.Add(Neighbor);
				}
			}
		}
		ComponentSizes[Id] = Front.Num();
		Report.PassageCells += Front.Num();
	}

	// the networks whose exits lie in each piece
	TArray<uint8> ComponentNetworks;
	ComponentNetworks.Init(0, ComponentSizes.Num());
	TArray<int32> NetworkComponents[HexPlanetFile::MaxNetworks];
	for (int32 Index = 0; Index < Entrances.Num(); ++Index)
	{
		const EEntranceProblem Problem = GetEntranceProblem(Grid, SurfaceLevels, Levels, Entrances, Index);
		if (Problem != EEntranceProblem::None && Problem != EEntranceProblem::ExitInVoid)
		{
			Report.BadEntrances.Add(Index);
			continue;
		}
		const FHexEntrance& Entrance = Entrances[Index];
		++Report.Networks[Entrance.Network].Entrances;
		if (Problem == EEntranceProblem::ExitInVoid)
		{
			Report.ExitsInVoid.Add(Entrance.Cell);
			continue;
		}
		const int32 Id = Component[Entrance.Cell];
		ComponentNetworks[Id] |= uint8(1 << Entrance.Network);
		NetworkComponents[Entrance.Network].AddUnique(Id);
	}

	for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
	{
		Report.Networks[Network].Components = NetworkComponents[Network].Num();
		for (const int32 Id : NetworkComponents[Network])
		{
			Report.Networks[Network].Cells += ComponentSizes[Id];
		}
	}

	for (int32 Id = 0; Id < ComponentNetworks.Num(); ++Id)
	{
		for (int32 A = 0; A < HexPlanetFile::MaxNetworks; ++A)
		{
			for (int32 B = A + 1; B < HexPlanetFile::MaxNetworks; ++B)
			{
				if ((ComponentNetworks[Id] & (1 << A)) && (ComponentNetworks[Id] & (1 << B)))
				{
					Report.Merged.AddUnique(FIntPoint(A, B));
				}
			}
		}
	}

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Component[Cell] == INDEX_NONE)
		{
			continue;
		}
		const uint8 Mask = ComponentNetworks[Component[Cell]];
		if (Mask == 0)
		{
			++Report.OrphanCells;
		}
		else if (FMath::IsPowerOfTwo(Mask))
		{
			Report.CellNetworks[Cell] = int8(FMath::FloorLog2(Mask));
		}
		else
		{
			Report.CellNetworks[Cell] = MergedNetwork;
		}
		if (IsNeck(Grid, Levels, Cell))
		{
			Report.Necks.Add(Cell);
		}
	}
	return Report;
}

int32 HexPlanetUnderground::GetSeed(const FHexPlanetManifest& Manifest)
{
	return int32(uint32(Manifest.Seed) * 2654435761u + uint32(Manifest.Underground.SeedOffset) * 40503u + 0x51ED27u);
}

void HexPlanetUnderground::GetEntranceCounts(const FHexUndergroundGeneration& Generation, int32 CellCount, int32 CandidateCount, int32& OutEntrances, int32& OutNetworks)
{
	const int32 MinPer = FMath::Max(Generation.MinEntrancesPerNetwork, 1);
	const int32 MaxPer = FMath::Max(Generation.MaxEntrancesPerNetwork, MinPer);
	const int32 Wanted = Generation.EntranceCount > 0 ? Generation.EntranceCount
		: FMath::Max(FMath::RoundToInt32(double(CellCount) / FMath::Max(Generation.CellsPerEntrance, 1)), 2);
	const int32 Networks = Generation.NetworkCount > 0 ? FMath::Min(Generation.NetworkCount, HexPlanetFile::MaxNetworks)
		: FMath::Clamp(FMath::RoundToInt32(Wanted / (0.5 * (MinPer + MaxPer))), 1, HexPlanetFile::MaxNetworks);
	OutEntrances = FMath::Max(FMath::Min3(Wanted, CandidateCount, Networks * MaxPer), 0);
	OutNetworks = OutEntrances > 0 ? FMath::Clamp(OutEntrances / MinPer, 1, Networks) : 0;
}

TArray<FHexEntrance> HexPlanetUnderground::PlaceEntrances(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexUndergroundGeneration& Generation, int32 Seed)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<int32> Candidates;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (CanHoldEntrance(Grid, SurfaceLevels, Cell))
		{
			Candidates.Add(Cell);
		}
	}

	const int32 MinPer = FMath::Max(Generation.MinEntrancesPerNetwork, 1);
	const int32 MaxPer = FMath::Max(Generation.MaxEntrancesPerNetwork, MinPer);
	int32 Count = 0;
	int32 NetworkCount = 0;
	GetEntranceCounts(Generation, Cells.Num(), Candidates.Num(), Count, NetworkCount);
	if (Count <= 0)
	{
		return {};
	}

	// each next entrance is the candidate furthest from all before it, the first one at random
	FRandomStream Random(Seed);
	TArray<int32> Chosen = {Candidates[Random.RandHelper(Candidates.Num())]};
	TArray<double> Nearest;
	Nearest.Init(TNumericLimits<double>::Max(), Candidates.Num());
	while (Chosen.Num() < Count)
	{
		const FVector3d& Last = Cells[Chosen.Last()].Center;
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Nearest[Index] = FMath::Min(Nearest[Index], GetSpread(Cells[Candidates[Index]].Center, Last));
			if (Best == INDEX_NONE || Nearest[Index] > Nearest[Best])
			{
				Best = Index;
			}
		}
		if (Best == INDEX_NONE || Nearest[Best] <= 0.0)
		{
			break;
		}
		Chosen.Add(Candidates[Best]);
	}

	// the networks start from entrances far apart, and each then takes the nearest entrance left to any of its own while it has room
	TArray<int32> Assigned;
	Assigned.Init(INDEX_NONE, Chosen.Num());
	TArray<int32> Sizes;
	Sizes.Init(0, NetworkCount);
	Assigned[0] = 0;
	Sizes[0] = 1;
	for (int32 Network = 1; Network < NetworkCount; ++Network)
	{
		int32 Best = INDEX_NONE;
		double BestSpread = -1.0;
		for (int32 Index = 0; Index < Chosen.Num(); ++Index)
		{
			if (Assigned[Index] != INDEX_NONE)
			{
				continue;
			}
			double Spread = TNumericLimits<double>::Max();
			for (int32 Other = 0; Other < Chosen.Num(); ++Other)
			{
				if (Assigned[Other] != INDEX_NONE)
				{
					Spread = FMath::Min(Spread, GetSpread(Cells[Chosen[Index]].Center, Cells[Chosen[Other]].Center));
				}
			}
			if (Spread > BestSpread)
			{
				BestSpread = Spread;
				Best = Index;
			}
		}
		if (Best == INDEX_NONE)
		{
			break;
		}
		Assigned[Best] = Network;
		Sizes[Network] = 1;
	}

	const auto SpreadToNetwork = [&](int32 Index, int32 Network)
	{
		double Spread = TNumericLimits<double>::Max();
		for (int32 Other = 0; Other < Chosen.Num(); ++Other)
		{
			if (Other != Index && Assigned[Other] == Network)
			{
				Spread = FMath::Min(Spread, GetSpread(Cells[Chosen[Index]].Center, Cells[Chosen[Other]].Center));
			}
		}
		return Spread;
	};

	for (;;)
	{
		int32 BestIndex = INDEX_NONE;
		int32 BestNetwork = INDEX_NONE;
		double BestSpread = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Chosen.Num(); ++Index)
		{
			if (Assigned[Index] != INDEX_NONE)
			{
				continue;
			}
			for (int32 Network = 0; Network < NetworkCount; ++Network)
			{
				const double Spread = Sizes[Network] < MaxPer ? SpreadToNetwork(Index, Network) : TNumericLimits<double>::Max();
				if (Spread < BestSpread)
				{
					BestSpread = Spread;
					BestIndex = Index;
					BestNetwork = Network;
				}
			}
		}
		if (BestIndex == INDEX_NONE)
		{
			break;
		}
		Assigned[BestIndex] = BestNetwork;
		++Sizes[BestNetwork];
	}

	// a network with too few entrances takes the nearest one of a network that can spare it
	for (int32 Pass = 0; Pass < Chosen.Num(); ++Pass)
	{
		const int32 Short = Sizes.IndexOfByPredicate([MinPer](int32 Size) { return Size < MinPer; });
		if (Short == INDEX_NONE)
		{
			break;
		}
		int32 BestIndex = INDEX_NONE;
		double BestSpread = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Chosen.Num(); ++Index)
		{
			if (Assigned[Index] != INDEX_NONE && Assigned[Index] != Short && Sizes[Assigned[Index]] > MinPer)
			{
				const double Spread = SpreadToNetwork(Index, Short);
				if (Spread < BestSpread)
				{
					BestSpread = Spread;
					BestIndex = Index;
				}
			}
		}
		if (BestIndex == INDEX_NONE)
		{
			break;
		}
		--Sizes[Assigned[BestIndex]];
		Assigned[BestIndex] = Short;
		++Sizes[Short];
	}

	TArray<FHexEntrance> Entrances;
	for (int32 Index = 0; Index < Chosen.Num(); ++Index)
	{
		if (Assigned[Index] != INDEX_NONE)
		{
			Entrances.Add({Chosen[Index], Assigned[Index]});
		}
	}
	Entrances.Sort([](const FHexEntrance& A, const FHexEntrance& B) { return A.Cell < B.Cell; });
	return Entrances;
}

void HexPlanetUnderground::Generate(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexPlanetManifest& Manifest,
	const TArray<FHexEntrance>* Entrances, FResult& OutResult)
{
	const double StartTime = FPlatformTime::Seconds();
	const FHexUndergroundGeneration& Generation = Manifest.Underground;
	const int32 Seed = GetSeed(Manifest);

	OutResult = FResult();
	OutResult.bPlacedEntrances = Entrances == nullptr;
	OutResult.Entrances = Entrances ? *Entrances : PlaceEntrances(Grid, SurfaceLevels, Generation, Seed);

	FMaze Maze(Grid, Generation);
	FRandomStream Random(Seed ^ 0x2545F491);
	InitCost(Maze, Grid, Generation, Random);

	TArray<int32> Exits[HexPlanetFile::MaxNetworks];
	GetUsableExits(Grid, SurfaceLevels, OutResult.Entrances, Exits);
	CarveNetworks(Maze, Exits, ~0u, Random);
	FinishResult(Maze, Grid, SurfaceLevels, Seed, StartTime, OutResult);
}

void HexPlanetUnderground::RegenerateNetwork(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexPlanetManifest& Manifest,
	TConstArrayView<FHexEntrance> Entrances, TConstArrayView<int8> Levels, int32 Network, FResult& OutResult)
{
	const double StartTime = FPlatformTime::Seconds();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	OutResult = FResult();
	OutResult.Entrances = TArray<FHexEntrance>(Entrances);
	if (Levels.Num() != Cells.Num() || Network < 0 || Network >= HexPlanetFile::MaxNetworks)
	{
		OutResult.Levels = TArray<int8>(Levels);
		GenerateBiomes(Grid, OutResult.Levels, OutResult.Entrances, Manifest.Underground, GetSeed(Manifest), OutResult.Biomes);
		OutResult.Report = Check(Grid, SurfaceLevels, OutResult.Levels, OutResult.Entrances);
		return;
	}

	TArray<int32> Exits[HexPlanetFile::MaxNetworks];
	GetUsableExits(Grid, SurfaceLevels, Entrances, Exits);

	// the pieces of passage that hold an exit of the network go, and every network whose exits lie in them is carved again with it, as one piece
	// may join several networks
	TArray<int32> Piece;
	const int32 NumPieces = FindPieces(Cells, Levels, Piece);
	// an exit in the void next to a piece counts as in it, as its exit is always carved and would join it
	TBitArray<> Cleared(false, NumPieces);
	for (const int32 Exit : Exits[Network])
	{
		if (Piece[Exit] != INDEX_NONE)
		{
			Cleared[Piece[Exit]] = true;
			continue;
		}
		for (const int32 Neighbor : Cells[Exit].Neighbors)
		{
			if (Piece[Neighbor] != INDEX_NONE)
			{
				Cleared[Piece[Neighbor]] = true;
			}
		}
	}
	uint32 Mask = 1u << Network;
	TBitArray<> HasExit(false, NumPieces);
	for (int32 Other = 0; Other < HexPlanetFile::MaxNetworks; ++Other)
	{
		for (const int32 Exit : Exits[Other])
		{
			if (Piece[Exit] != INDEX_NONE)
			{
				HasExit[Piece[Exit]] = true;
				Mask |= Cleared[Piece[Exit]] ? 1u << Other : 0u;
			}
		}
	}

	// pieces no exit reaches within NetworkGap of those, and of those again, are what is left of the network cut off from its exits, and go
	// too, so it is carved again whole rather than around its own leftovers
	const int32 Gap = FMath::Max(Manifest.Underground.NetworkGap, 1);
	TArray<int32> Distance;
	for (bool bMore = true; bMore;)
	{
		bMore = false;
		Distance.Init(MAX_int32, Cells.Num());
		TArray<int32> Front;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			if (Piece[Cell] != INDEX_NONE && Cleared[Piece[Cell]])
			{
				Distance[Cell] = 0;
				Front.Add(Cell);
			}
		}
		for (int32 Index = 0; Index < Front.Num(); ++Index)
		{
			const int32 Cell = Front[Index];
			if (Piece[Cell] != INDEX_NONE && !HasExit[Piece[Cell]] && !Cleared[Piece[Cell]])
			{
				Cleared[Piece[Cell]] = true;
				bMore = true;
			}
			if (Distance[Cell] > Gap)
			{
				continue;
			}
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (Distance[Neighbor] == MAX_int32)
				{
					Distance[Neighbor] = Distance[Cell] + 1;
					Front.Add(Neighbor);
				}
			}
		}
	}

	// the passages kept belong to their network, or, a piece of none or of several, to a network of their own, which every network keeps clear of
	const FReport Before = Check(Grid, SurfaceLevels, Levels, Entrances);
	FMaze Maze(Grid, Manifest.Underground);
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Piece[Cell] != INDEX_NONE && !Cleared[Piece[Cell]])
		{
			const int8 Owner = Before.CellNetworks[Cell];
			Maze.Owner[Cell] = Owner >= 0 ? Owner : int8(HexPlanetFile::MaxNetworks);
		}
	}

	// the same field of costs as the whole maze, so the ways wind alike
	FRandomStream Random(GetSeed(Manifest) ^ 0x2545F491);
	InitCost(Maze, Grid, Manifest.Underground, Random);
	CarveNetworks(Maze, Exits, Mask, Random);
	FinishResult(Maze, Grid, SurfaceLevels, GetSeed(Manifest), StartTime, OutResult);
}

TArray<int32> HexPlanetUnderground::FindPath(const FHexSphereGrid& Grid, int32 From, int32 To)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (!Cells.IsValidIndex(From) || !Cells.IsValidIndex(To))
	{
		return {};
	}

	// a search outwards from From over the neighbors, which ends where it reaches To
	TMap<int32, int32> Came;
	Came.Add(From, INDEX_NONE);
	TArray<int32> Front = {From};
	for (int32 Index = 0; Index < Front.Num() && !Came.Contains(To); ++Index)
	{
		for (const int32 Neighbor : Cells[Front[Index]].Neighbors)
		{
			if (!Came.Contains(Neighbor))
			{
				Came.Add(Neighbor, Front[Index]);
				Front.Add(Neighbor);
			}
		}
	}

	TArray<int32> Path;
	for (int32 Cell = To; Cell != INDEX_NONE; Cell = Came[Cell])
	{
		Path.Add(Cell);
	}
	Algo::Reverse(Path);
	return Path;
}

TArray<int32> HexPlanetUnderground::WidenPath(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<int32> Path, int32 Width)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<int8> Work(Levels);
	TArray<int32> Carved;
	TArray<int32> Queue;
	const auto Carve = [&Work, &Carved, &Queue](int32 Cell)
	{
		if (!IsPassage(Work[Cell]))
		{
			Work[Cell] = PassageLevel;
			Carved.Add(Cell);
		}
		Queue.Add(Cell);
	};
	if (Levels.Num() != Cells.Num())
	{
		return Carved;
	}

	for (const int32 Cell : Path)
	{
		if (Cells.IsValidIndex(Cell))
		{
			Carve(Cell);
		}
	}

	// a passage wider than 2 grows by whole rings around the way
	TArray<int32> Front = Queue;
	for (int32 Ring = 0; Ring < (FMath::Max(Width, 2) - 1) / 2; ++Ring)
	{
		TArray<int32> Next;
		for (const int32 Cell : Front)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (!IsPassage(Work[Neighbor]))
				{
					Carve(Neighbor);
					Next.Add(Neighbor);
				}
			}
		}
		Front = MoveTemp(Next);
	}

	// then each neck of these cells takes the one of its two void cells with more passage around it, the lower first on a tie
	const auto CountPassages = [&Cells, &Work](int32 Cell)
	{
		int32 Count = 0;
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			Count += IsPassage(Work[Neighbor]) ? 1 : 0;
		}
		return Count;
	};
	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		const int32 Cell = Queue[Index];
		int32 Best = INDEX_NONE;
		int32 BestCount = 0;
		ForEachNeckPair(Cells[Cell], [&](int32 A, int32 B)
		{
			if (IsPassage(Work[A]) || IsPassage(Work[B]))
			{
				return;
			}
			for (const int32 Candidate : {A, B})
			{
				const int32 Count = CountPassages(Candidate);
				if (Best == INDEX_NONE || Count > BestCount || (Count == BestCount && Candidate < Best))
				{
					Best = Candidate;
					BestCount = Count;
				}
			}
		});
		if (Best != INDEX_NONE)
		{
			// the cell may have another neck, and the new one may be one
			Carve(Best);
			Queue.Add(Cell);
		}
	}
	return Carved;
}

void HexPlanetUnderground::Load(const FString& PlanetFolder, const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexPlanetManifest& Manifest,
	TConstArrayView<TCHAR> BiomeLetters, const FLoadOptions& Options, FLoaded& Out)
{
	Out = FLoaded();
	const int32 CellCount = Grid.GetCells().Num();
	const bool bSaved = !PlanetFolder.IsEmpty();
	const FString Name = bSaved ? PlanetFolder : FString(TEXT("(not saved)"));
	const FString DataFolder = HexPlanetFile::GetDataFolder(PlanetFolder, EHexPlanetLayer::Underground);
	const TConstArrayView<FHexLevelLetter> Letters = HexPlanetFile::GetLevelLetters(EHexPlanetLayer::Underground);
	const bool bNewMaze = Options.bNewMaze || Options.bNewEntrances;
	FString Error;

	const bool bEntrancesFile = bSaved && HexPlanetFile::HasEntrances(PlanetFolder);
	bool bHaveEntrances = false;
	if (bEntrancesFile && !Options.bNewEntrances)
	{
		bHaveEntrances = HexPlanetFile::LoadEntrances(PlanetFolder, Out.Entrances, Error);
		if (!bHaveEntrances)
		{
			UE_LOG(LogHexHero, Error, TEXT("Underground of %s: %s, placing the entrances again"), *Name, *Error);
		}
	}

	const bool bLevelsFile = bSaved && HexPlanetFile::HasLevels(DataFolder);
	bool bHaveLevels = false;
	if (bLevelsFile && !bNewMaze)
	{
		bHaveLevels = HexPlanetFile::LoadLevels(DataFolder, CellCount, Out.Levels, Error, Letters);
		if (!bHaveLevels)
		{
			UE_LOG(LogHexHero, Error, TEXT("Underground of %s: %s, generating the maze"), *Name, *Error);
		}
		else if (!bHaveEntrances)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Underground of %s has a maze but no valid %s, so it has no entrances"), *Name, HexPlanetFile::EntrancesFile);
		}
	}

	TArray<TCHAR> CellLetters;
	if (!bHaveLevels)
	{
		FResult Result;
		Generate(Grid, SurfaceLevels, Manifest, bHaveEntrances ? &Out.Entrances : nullptr, Result);
		Out.Levels = MoveTemp(Result.Levels);
		CellLetters = MoveTemp(Result.Biomes);
		if (Result.bPlacedEntrances)
		{
			Out.Entrances = MoveTemp(Result.Entrances);
			Out.bPlacedEntrances = true;
		}
		Out.bGeneratedLevels = true;
		Out.GenerateSeconds = Result.Seconds;
	}

	const bool bBiomesFile = bSaved && HexPlanetFile::HasBiomes(DataFolder);
	if (BiomeLetters.IsEmpty())
	{
		UE_LOG(LogHexHero, Warning, TEXT("Underground: no biome has an UndergroundLetter, run Tools/create_biome_assets.py"));
	}
	else
	{
		bool bHaveBiomes = false;
		if (bBiomesFile && !Out.bGeneratedLevels && !Options.bNewBiomes)
		{
			bHaveBiomes = HexPlanetFile::LoadBiomes(DataFolder, CellCount, BiomeLetters, Out.Biomes, Error);
			if (!bHaveBiomes)
			{
				UE_LOG(LogHexHero, Error, TEXT("Underground of %s: %s, generating the biomes"), *Name, *Error);
			}
		}
		if (!bHaveBiomes)
		{
			// a maze just generated came with the same letters, which are made again here with the report for the log
			GenerateBiomes(Grid, Out.Levels, Out.Entrances, Manifest.Underground, GetSeed(Manifest), CellLetters, &Out.BiomeReport);
			TSet<TCHAR> Missing;
			Out.Biomes.SetNumUninitialized(CellCount);
			for (int32 Cell = 0; Cell < CellCount; ++Cell)
			{
				const int32 Index = BiomeLetters.Find(CellLetters[Cell]);
				Out.Biomes[Cell] = uint8(FMath::Max(Index, 0));
				if (Index == INDEX_NONE)
				{
					Missing.Add(CellLetters[Cell]);
				}
			}
			for (const TCHAR Letter : Missing)
			{
				UE_LOG(LogHexHero, Warning, TEXT("Underground: no biome has the underground letter %c that the generation places, run Tools/create_biome_assets.py"), Letter);
			}
			Out.bGeneratedBiomes = true;
		}
	}

	Out.Report = Check(Grid, SurfaceLevels, Out.Levels, Out.Entrances);

	// what was generated in place of a missing file is written at once, so the planet keeps it; a file that is there but not valid is left alone
	if (bSaved && Options.bWriteMissing)
	{
		const auto Wrote = [&Out, &Error, &Name](bool bWritten, const FString& Path)
		{
			if (bWritten)
			{
				Out.Written.Add(Path);
			}
			else
			{
				UE_LOG(LogHexHero, Warning, TEXT("Underground of %s: %s"), *Name, *Error);
			}
		};
		if (Out.bPlacedEntrances && (!bEntrancesFile || Options.bNewEntrances))
		{
			Wrote(HexPlanetFile::SaveEntrances(PlanetFolder, GetSavedEntrances(Grid, SurfaceLevels, Out.Levels, Out.Entrances), Error),
				HexPlanetFile::GetFilePath(PlanetFolder, HexPlanetFile::EntrancesFile));
		}
		const bool bWriteLevels = Out.bGeneratedLevels && (!bLevelsFile || bNewMaze);
		if (bWriteLevels)
		{
			Wrote(HexPlanetFile::SaveLevels(DataFolder, Out.Levels, Error, Letters), HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::LevelsFile));
		}
		if (Out.bGeneratedBiomes && (!bBiomesFile || bWriteLevels || Options.bNewBiomes))
		{
			Wrote(HexPlanetFile::SaveBiomes(DataFolder, Out.Biomes, BiomeLetters, Error), HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::BiomesFile));
		}
	}

	UE_LOG(LogHexHero, Log, TEXT("Underground of %s: %s maze%s, %d entrances%s: %s"), *Name,
		Out.bGeneratedLevels ? TEXT("generated") : TEXT("read"),
		Out.bGeneratedLevels ? *FString::Printf(TEXT(" in %.0f ms"), Out.GenerateSeconds * 1000.0) : TEXT(""),
		Out.Entrances.Num(), Out.bPlacedEntrances ? TEXT(" placed") : TEXT(""), *Out.Report.ToString());
	if (Out.bGeneratedBiomes)
	{
		UE_LOG(LogHexHero, Log, TEXT("Underground of %s: biomes generated, %s"), *Name, *Out.BiomeReport.ToString());
	}
	for (const FString& Path : Out.Written)
	{
		UE_LOG(LogHexHero, Log, TEXT("Underground: wrote %s"), *Path);
	}
	if (!Out.Report.IsGood())
	{
		UE_LOG(LogHexHero, Warning, TEXT("Underground of %s has problems: %s"), *Name, *Out.Report.ToString());
	}
}

namespace
{
	/** A noise of patches on the unit sphere, two octaves, as the surface takes for its covers */
	double GetPatchNoise(const FVector3d& Point, double Scale, const FVector3d& Offset)
	{
		return FMath::PerlinNoise3D(Point * Scale + Offset) + 0.5 * FMath::PerlinNoise3D(Point * Scale * 2.1 + Offset * 1.7 + FVector3d(31.0));
	}

	/** Returns the middle cell of the largest piece of cells marked in a mask, joined over neighbors, the cell nearest their mean direction */
	int32 FindLargestPatchCenter(const TArray<FHexSphereCell>& Cells, const TArray<uint8>& Marked)
	{
		TArray<uint8> Seen;
		Seen.Init(0, Cells.Num());
		int32 BestCount = 0;
		int32 BestCenter = INDEX_NONE;
		TArray<int32> Piece;
		for (int32 Start = 0; Start < Cells.Num(); ++Start)
		{
			if (!Marked[Start] || Seen[Start])
			{
				continue;
			}
			Piece.Reset();
			Piece.Add(Start);
			Seen[Start] = 1;
			FVector3d Sum = FVector3d::ZeroVector;
			for (int32 Index = 0; Index < Piece.Num(); ++Index)
			{
				Sum += Cells[Piece[Index]].Center;
				for (const int32 Neighbor : Cells[Piece[Index]].Neighbors)
				{
					if (Marked[Neighbor] && !Seen[Neighbor])
					{
						Seen[Neighbor] = 1;
						Piece.Add(Neighbor);
					}
				}
			}
			if (Piece.Num() <= BestCount)
			{
				continue;
			}
			BestCount = Piece.Num();
			const FVector3d Mean = Sum.GetSafeNormal();
			double Nearest = -2.0;
			for (const int32 Cell : Piece)
			{
				const double Dot = FVector3d::DotProduct(Cells[Cell].Center, Mean);
				if (Dot > Nearest)
				{
					Nearest = Dot;
					BestCenter = Cell;
				}
			}
		}
		return BestCenter;
	}
}

void HexPlanetUnderground::GenerateObjects(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<TCHAR> CellBiomeLetters,
	TConstArrayView<FHexEntrance> Entrances, int32 Seed, TConstArrayView<FObjectKind> Kinds, TArray<FHexCellObjects>& OutObjects, FObjectReport* OutReport)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const int32 NumCells = Cells.Num();
	OutObjects.Init(FHexCellObjects(), NumCells);
	if (OutReport)
	{
		OutReport->Cells.Init(0, Kinds.Num());
		OutReport->PatchCenters.Init(INDEX_NONE, Kinds.Num());
	}
	if (NumCells == 0 || Levels.Num() != NumCells)
	{
		return;
	}

	// rings from the exits, as far as any kind keeps clear of them, by a search over neighbors whatever the level
	int32 MaxRings = 0;
	for (const FObjectKind& Kind : Kinds)
	{
		MaxRings = FMath::Max(MaxRings, Kind.ClearRings);
	}
	TArray<int32> ExitCells;
	for (const FHexEntrance& Entrance : Entrances)
	{
		ExitCells.Add(Entrance.Cell);
	}
	const TArray<int32> ExitRings = GetRingDistances(Grid, ExitCells, MaxRings);

	// the cells of the sphere spread about this far apart on the unit sphere; the noise rises and falls about once over its lattice step, so a
	// step of twice PatchCells cells gives lobes about PatchCells cells across
	const double Spacing = FMath::Sqrt(4.0 * UE_DOUBLE_PI / NumCells);
	TArray<int32> Candidates;
	TArray<double> Keys;
	Keys.SetNumZeroed(NumCells);
	TArray<uint8> Marked;

	// the levels with every cell that a kind which blocks took made void: a cell blocks only where that leaves no passage one cell wide (IsNeck)
	// and its passages around it still meet without it, so the maze keeps every way it had
	TArray<int8> Blocked(Levels.GetData(), Levels.Num());
	TArray<int32> Seen;
	Seen.Init(INDEX_NONE, NumCells);
	TArray<int32> Frontier;
	const auto CanBlock = [&](int32 Cell)
	{
		if (!IsPassage(Blocked[Cell]))
		{
			return false;
		}
		const int8 Level = Blocked[Cell];
		Blocked[Cell] = VoidLevel;
		bool bClear = true;
		int32 First = INDEX_NONE;
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			bClear &= !IsNeck(Grid, Blocked, Neighbor);
			First = First == INDEX_NONE && IsPassage(Blocked[Neighbor]) ? Neighbor : First;
		}
		// the passages around it meet within a few rings without it
		if (bClear && First != INDEX_NONE)
		{
			Frontier.Reset();
			Frontier.Add(First);
			Seen[First] = Cell;
			for (int32 Index = 0; Index < Frontier.Num() && Frontier.Num() < 400; ++Index)
			{
				for (const int32 Next : Cells[Frontier[Index]].Neighbors)
				{
					if (Seen[Next] != Cell && IsPassage(Blocked[Next]) && FVector3d::DistSquared(Cells[Next].Center, Cells[Cell].Center) < FMath::Square(Spacing * 6.0))
					{
						Seen[Next] = Cell;
						Frontier.Add(Next);
					}
				}
			}
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				bClear &= !IsPassage(Blocked[Neighbor]) || Seen[Neighbor] == Cell;
			}
		}
		Blocked[Cell] = Level;
		return bClear;
	};

	for (int32 KindIndex = 0; KindIndex < Kinds.Num(); ++KindIndex)
	{
		const FObjectKind& Kind = Kinds[KindIndex];
		if (Kind.SetIndex < 0 || Kind.SetIndex > 253)
		{
			continue;
		}
		const uint8 Stored = uint8(Kind.SetIndex + 1);
		const uint32 NameHash = FCrc::StrCrc32(*(Kind.Group.IsNone() ? Kind.Id : Kind.Group.ToString()));
		FRandomStream Random(int32(HashCombine(uint32(Seed), NameHash)));
		const FVector3d Offset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
		const double Scale = Kind.PatchCells > 0.0f ? 0.5 / (Kind.PatchCells * Spacing) : 0.0;
		const uint32 IdHash = FCrc::StrCrc32(*Kind.Id);
		int32 Given = 0;
		if (OutReport)
		{
			Marked.Init(0, NumCells);
		}

		for (int32 Variant = 0; Variant < Kind.Variants.Num(); ++Variant)
		{
			const float Share = Kind.Variants[Variant].Value;
			if (Share <= 0.0f)
			{
				continue;
			}

			Candidates.Reset();
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				const FHexCellObjects& Objects = OutObjects[Cell];
				if (Cells[Cell].IsPentagon() || ExitRings[Cell] <= Kind.ClearRings || (Kind.bDecor ? Objects.Decor != 0 : Objects.Cover != 0)
					|| !Kind.Levels.Contains(Levels[Cell]))
				{
					continue;
				}
				// a kind that blocks fills a cell of its own, and only one it can block
				if (Kind.bBlocks && (!Objects.IsEmpty() || !CanBlock(Cell)))
				{
					continue;
				}
				const TCHAR Letter = CellBiomeLetters.IsValidIndex(Cell) ? CellBiomeLetters[Cell] : TEXT('\0');
				if (FindHexObjectVariant(Kind.Variants.Num(), Letter, [&Kind](int32 Each) -> const FString& { return Kind.Variants[Each].Key; }) != Variant)
				{
					continue;
				}
				Candidates.Add(Cell);
				Keys[Cell] = Scale > 0.0 ? GetPatchNoise(Cells[Cell].Center, Scale, Offset) : double(HashCombine(GetTypeHash(Cell), IdHash)) / 4294967296.0;
			}

			// highest first, ties by cell, so the order never depends on the sort
			Candidates.Sort([&Keys](int32 A, int32 B) { return Keys[A] != Keys[B] ? Keys[A] > Keys[B] : A < B; });
			const int32 Count = FMath::RoundToInt32(Share * Candidates.Num());
			// a cell that blocks is checked again, as one taken before may have made it a neck; then the next one takes its place
			int32 Taken = 0;
			for (int32 Rank = 0; Rank < Candidates.Num() && Taken < Count; ++Rank)
			{
				if (Kind.bBlocks)
				{
					if (!CanBlock(Candidates[Rank]))
					{
						continue;
					}
					Blocked[Candidates[Rank]] = VoidLevel;
				}
				const int32 Place = Taken++;
				FHexCellObjects& Objects = OutObjects[Candidates[Rank]];
				if (Kind.bDecor)
				{
					Objects.Decor = Stored;
				}
				else
				{
					// dense in the middle of a patch, sparse at its edge
					Objects.Cover = Stored;
					Objects.Density = Place < Count * Kind.DenseShare ? 3 : Place < Count * Kind.MediumShare ? 2 : 1;
				}
				if (OutReport)
				{
					Marked[Candidates[Rank]] = 1;
				}
			}
			Given += Taken;
		}

		if (OutReport)
		{
			OutReport->Cells[KindIndex] = Given;
			OutReport->PatchCenters[KindIndex] = Given > 0 ? FindLargestPatchCenter(Cells, Marked) : INDEX_NONE;
		}
	}
}

TArray<int32> HexPlanetUnderground::GetRingDistances(const FHexSphereGrid& Grid, TConstArrayView<int32> Sources, int32 MaxRings)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<int32> Rings;
	Rings.Init(MAX_int32, Cells.Num());
	TArray<int32> Frontier;
	for (const int32 Source : Sources)
	{
		if (Cells.IsValidIndex(Source) && Rings[Source] != 0)
		{
			Rings[Source] = 0;
			Frontier.Add(Source);
		}
	}
	for (int32 Index = 0; Index < Frontier.Num(); ++Index)
	{
		const int32 Cell = Frontier[Index];
		if (Rings[Cell] >= MaxRings)
		{
			continue;
		}
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (Rings[Neighbor] == MAX_int32)
			{
				Rings[Neighbor] = Rings[Cell] + 1;
				Frontier.Add(Neighbor);
			}
		}
	}
	return Rings;
}

void HexPlanetUnderground::FindRimSides(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, int32 MinDrop, TFunctionRef<bool(int32)> IsSkipped,
	TConstArrayView<int32> InCells, TArray<FIntPoint>& Out)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (Levels.Num() != Cells.Num())
	{
		return;
	}
	for (const int32 Cell : InCells)
	{
		if (!Cells.IsValidIndex(Cell) || IsSkipped(Cell))
		{
			continue;
		}
		// the higher cell owns the side, so a side between a passage and a void is listed once, by the chunk of its passage
		const FHexSphereCell& Each = Cells[Cell];
		for (int32 Side = 0; Side < Each.Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Each.Neighbors[Side];
			if (int32(Levels[Neighbor]) <= int32(Levels[Cell]) - MinDrop && !IsSkipped(Neighbor))
			{
				Out.Add(FIntPoint(Cell, Side));
			}
		}
	}
}

bool HexPlanetUnderground::IsFootprintClear(FVector2f Point, float Radius, TConstArrayView<FVector2f> FlatCorners, uint8 ClipSides)
{
	const int32 NumCorners = FlatCorners.Num();
	if (ClipSides == 0 || NumCorners < 3)
	{
		return true;
	}

	// the middle of the corners lies inside, by which the inside of every line is known whichever way the corners go round
	FVector2f Middle = FVector2f::ZeroVector;
	for (const FVector2f& Corner : FlatCorners)
	{
		Middle += Corner;
	}
	Middle /= float(NumCorners);

	for (int32 Side = 0; Side < NumCorners; ++Side)
	{
		if ((ClipSides & (1u << Side)) == 0)
		{
			continue;
		}
		const FVector2f& From = FlatCorners[(Side + NumCorners - 1) % NumCorners];
		const FVector2f& To = FlatCorners[Side];
		const FVector2f Along = To - From;
		const float Length = Along.Size();
		if (Length < 1.0e-6f)
		{
			continue;
		}
		// the distance from the line of the side towards the inside, so a disc that crosses the line anywhere, also past a corner, is rejected
		const float Inside = FMath::Sign(FVector2f::CrossProduct(Along, Middle - From));
		const float Distance = FVector2f::CrossProduct(Along, Point - From) / Length * Inside;
		if (Distance < Radius)
		{
			return false;
		}
	}
	return true;
}

namespace
{
	/** Returns how many pieces the cells marked in a mask make, joined over neighbors */
	int32 CountPatches(const TArray<FHexSphereCell>& Cells, const TArray<uint8>& Marked)
	{
		TArray<uint8> Seen;
		Seen.Init(0, Cells.Num());
		int32 Count = 0;
		TArray<int32> Front;
		for (int32 Start = 0; Start < Cells.Num(); ++Start)
		{
			if (!Marked[Start] || Seen[Start])
			{
				continue;
			}
			++Count;
			Front.Reset();
			Front.Add(Start);
			Seen[Start] = 1;
			for (int32 Index = 0; Index < Front.Num(); ++Index)
			{
				for (const int32 Neighbor : Cells[Front[Index]].Neighbors)
				{
					if (Marked[Neighbor] && !Seen[Neighbor])
					{
						Seen[Neighbor] = 1;
						Front.Add(Neighbor);
					}
				}
			}
		}
		return Count;
	}
}

FString HexPlanetUnderground::FBiomeReport::ToString() const
{
	return FString::Printf(TEXT("%d voids could hold lava or a lake: lava %d cells in %d patches (largest around %d), lakes %d cells in %d patches (largest around %d)"),
		Candidates, LavaCells, LavaPatches, LavaCenter, LakeCells, LakePatches, LakeCenter);
}

void HexPlanetUnderground::GenerateBiomes(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<FHexEntrance> Entrances,
	const FHexUndergroundGeneration& Generation, int32 Seed, TArray<TCHAR>& OutBiomes, FBiomeReport* OutReport)
{
	OutBiomes.SetNumUninitialized(Levels.Num());
	for (int32 Cell = 0; Cell < Levels.Num(); ++Cell)
	{
		OutBiomes[Cell] = IsPassage(Levels[Cell]) ? Biome::Floor : Biome::Abyss;
	}
	if (OutReport)
	{
		*OutReport = FBiomeReport();
	}
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (Cells.Num() != Levels.Num() || Cells.IsEmpty())
	{
		return;
	}

	// the voids that can be seen, within PoolRings of a passage, and further than PoolExitRings from every exit
	TArray<int32> Passages;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (IsPassage(Levels[Cell]))
		{
			Passages.Add(Cell);
		}
	}
	TArray<int32> ExitCells;
	for (const FHexEntrance& Entrance : Entrances)
	{
		ExitCells.Add(Entrance.Cell);
	}
	const int32 PoolRings = FMath::Max(Generation.PoolRings, 1);
	const int32 ExitRingsKept = FMath::Max(Generation.PoolExitRings, 0);
	const TArray<int32> PassageRings = GetRingDistances(Grid, Passages, PoolRings);
	const TArray<int32> ExitRings = GetRingDistances(Grid, ExitCells, ExitRingsKept);
	TArray<int32> Candidates;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (!IsPassage(Levels[Cell]) && PassageRings[Cell] <= PoolRings && ExitRings[Cell] > ExitRingsKept)
		{
			Candidates.Add(Cell);
		}
	}
	if (OutReport)
	{
		OutReport->Candidates = Candidates.Num();
	}
	if (Candidates.IsEmpty())
	{
		return;
	}

	// a noise of patches about PoolCells across, as the objects of the underground take theirs, one per style with an offset of its own
	const double Spacing = FMath::Sqrt(4.0 * UE_DOUBLE_PI / Cells.Num());
	const double Scale = 0.5 / (FMath::Max(Generation.PoolCells, 0.5) * Spacing);
	TArray<double> Keys;
	Keys.SetNumZeroed(Cells.Num());
	TArray<uint8> Marked;
	const auto Place = [&](const TCHAR* Name, TCHAR Letter, double Share, int32& OutCells, int32& OutPatches, int32& OutCenter)
	{
		FRandomStream Random(int32(HashCombine(uint32(Seed), FCrc::StrCrc32(Name))));
		const FVector3d Offset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
		TArray<int32> Free;
		for (const int32 Cell : Candidates)
		{
			if (OutBiomes[Cell] == Biome::Abyss)
			{
				Free.Add(Cell);
				Keys[Cell] = GetPatchNoise(Cells[Cell].Center, Scale, Offset);
			}
		}
		// highest first, ties by cell, so the order never depends on the sort
		Free.Sort([&Keys](int32 A, int32 B) { return Keys[A] != Keys[B] ? Keys[A] > Keys[B] : A < B; });
		const int32 Count = FMath::Min(FMath::RoundToInt32(FMath::Clamp(Share, 0.0, 1.0) * Candidates.Num()), Free.Num());
		Marked.Init(0, Cells.Num());
		for (int32 Rank = 0; Rank < Count; ++Rank)
		{
			OutBiomes[Free[Rank]] = Letter;
			Marked[Free[Rank]] = 1;
		}
		OutCells = Count;
		if (Count > 0)
		{
			OutPatches = CountPatches(Cells, Marked);
			OutCenter = FindLargestPatchCenter(Cells, Marked);
		}
	};

	FBiomeReport Report;
	Report.Candidates = Candidates.Num();
	Place(TEXT("Lava"), Biome::Lava, Generation.LavaShare, Report.LavaCells, Report.LavaPatches, Report.LavaCenter);
	Place(TEXT("Lake"), Biome::Lake, Generation.LakeShare, Report.LakeCells, Report.LakePatches, Report.LakeCenter);
	if (OutReport)
	{
		*OutReport = Report;
	}
}
