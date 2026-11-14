// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexZoneMap.h"
#include "HexHero.h"
#include "HexShroud.h"
#include "HexSphereGrid.h"
#include "JsonObjectConverter.h"
#include "Algo/Reverse.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"

namespace
{
	constexpr int8 DeepSea = -2;
	constexpr int8 Shallows = -1;
	constexpr int8 Plains = 0;
	constexpr int8 Hills = 1;
	constexpr int8 Mountain = 2;
	constexpr int32 MaxZones = 8;

	bool IsWalk(int8 Level)
	{
		return Level == Plains || Level == Hills;
	}

	/** What the ways of a map are: passes over the range between two zones beside each other, and networks with an entrance in each of
	 *  two or three zones */
	struct FWays
	{
		TArray<FIntPoint> Pairs;
		TArray<int32> Passes;
		TArray<TArray<int32>> Links;

		int32 Count(int32 Zone) const
		{
			int32 Total = 0;
			for (int32 Pair = 0; Pair < Pairs.Num(); ++Pair)
			{
				Total += Pairs[Pair].X == Zone || Pairs[Pair].Y == Zone ? Passes[Pair] : 0;
			}
			for (const TArray<int32>& Link : Links)
			{
				Total += Link.Contains(Zone) ? 1 : 0;
			}
			return Total;
		}

		bool Joins(int32 NumZones) const
		{
			TArray<int32> Group;
			for (int32 Zone = 0; Zone < NumZones; ++Zone)
			{
				Group.Add(Zone);
			}
			const auto Find = [&Group](int32 Zone) { while (Group[Zone] != Zone) { Zone = Group[Zone]; } return Zone; };
			const auto Join = [&](int32 A, int32 B) { Group[Find(A)] = Find(B); };
			for (int32 Pair = 0; Pair < Pairs.Num(); ++Pair)
			{
				if (Passes[Pair] > 0)
				{
					Join(Pairs[Pair].X, Pairs[Pair].Y);
				}
			}
			for (const TArray<int32>& Link : Links)
			{
				for (int32 Index = 1; Index < Link.Num(); ++Index)
				{
					Join(Link[0], Link[Index]);
				}
			}
			for (int32 Zone = 1; Zone < NumZones; ++Zone)
			{
				if (Find(Zone) != Find(0))
				{
					return false;
				}
			}
			return true;
		}
	};

	/** The work of Generate */
	struct FMaker
	{
		const FHexSphereGrid& Grid;
		const TArray<FHexSphereCell>& Cells;
		const FHexZoneMapGeneration& Settings;
		const int32 NumZones;
		const int32 AiZones;
		FRandomStream Random;
		HexZoneMap::FResult& Out;
		int32 NumCells = 0;
		/** Cells nothing may change any more: the castles and around them, the passes and the pockets of the entrances */
		TBitArray<> Locked;
		/** Cells no hero stands on though their level is walkable: the castles */
		TBitArray<> Closed;
		/** Cells the land of a zone need not reach: inside the pockets of the entrances */
		TBitArray<> Exempt;
		/** Steps from the ranges and the polar sea */
		TArray<int32> RangeSteps;

		FMaker(const FHexSphereGrid& InGrid, const FHexZoneMapGeneration& InSettings, int32 InNumZones, int32 InAiZones, int32 Seed, HexZoneMap::FResult& InOut)
			: Grid(InGrid), Cells(InGrid.GetCells()), Settings(InSettings), NumZones(InNumZones), AiZones(InAiZones), Random(Seed), Out(InOut)
		{
			NumCells = Cells.Num();
			Locked.Init(false, NumCells);
			Closed.Init(false, NumCells);
			Exempt.Init(false, NumCells);
		}

		bool IsInterior(int32 Cell, int32 Zone) const
		{
			return Out.ZoneOf[Cell] == Zone && !Out.Range[Cell];
		}

		/** The middles of the zones for a turn around the axis: one ring on the equator for up to four, two rings beside it for more */
		TArray<FVector3d> GetCenters(double Turn) const
		{
			TArray<FVector3d> Centers;
			const auto Ring = [&](int32 Count, double Latitude, double Shift)
			{
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const double Angle = Turn + Shift + UE_DOUBLE_TWO_PI * Index / Count;
					Centers.Add(FVector3d(FMath::Cos(Angle) * FMath::Cos(Latitude), FMath::Sin(Angle) * FMath::Cos(Latitude), FMath::Sin(Latitude)));
				}
			};
			if (NumZones <= 4)
			{
				Ring(NumZones, 0.0, 0.0);
			}
			else
			{
				const int32 North = NumZones / 2;
				const int32 South = NumZones - North;
				const double Latitude = FMath::DegreesToRadians(double(Settings.RingLatitude));
				Ring(North, Latitude, 0.0);
				Ring(South, -Latitude, UE_DOUBLE_PI / South);
			}
			return Centers;
		}

		/** The zone of a cell for a turn of the zones around the axis, INDEX_NONE for the polar sea */
		void AssignZones(double Turn, TConstArrayView<FVector3d> Offsets, TArray<int8>& OutZones) const
		{
			OutZones.SetNumUninitialized(NumCells);
			const TArray<FVector3d> Centers = GetCenters(Turn);
			const double PolarLatitude = FMath::DegreesToRadians(double(Settings.PolarLatitude));
			const double Wobble = FMath::DegreesToRadians(double(Settings.PolarWobble));
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				const FVector3d& Center = Cells[Cell].Center;
				const double Latitude = FMath::Asin(FMath::Clamp(Center.Z, -1.0, 1.0));
				const double Limit = PolarLatitude + Wobble * double(FMath::PerlinNoise3D(FVector(Center * 3.0 + Offsets[MaxZones])));
				if (FMath::Abs(Latitude) > Limit)
				{
					OutZones[Cell] = INDEX_NONE;
					continue;
				}
				int32 Best = 0;
				double BestScore = -UE_DOUBLE_BIG_NUMBER;
				for (int32 Zone = 0; Zone < NumZones; ++Zone)
				{
					const double Score = FVector3d::DotProduct(Center, Centers[Zone])
						+ double(Settings.ZoneWarp) * double(FMath::PerlinNoise3D(FVector(Center * double(Settings.ZoneWarpScale) + Offsets[Zone])));
					if (Score > BestScore)
					{
						BestScore = Score;
						Best = Zone;
					}
				}
				OutZones[Cell] = int8(Best);
			}
		}

		bool IsBorder(const TArray<int8>& Zones, int32 Cell) const
		{
			if (Zones[Cell] == INDEX_NONE)
			{
				return false;
			}
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (Zones[Neighbor] != INDEX_NONE && Zones[Neighbor] != Zones[Cell])
				{
					return true;
				}
			}
			return false;
		}

		/** A piece of a zone cut off from the rest takes the zone most of its neighbors have, so each zone is one piece */
		void JoinPieces(TArray<int8>& Zones) const
		{
			for (int32 Pass = 0; Pass < 8; ++Pass)
			{
				TArray<int32> Piece;
				Piece.Init(INDEX_NONE, NumCells);
				TArray<int32> Sizes;
				TArray<int32> ZoneOfPiece;
				for (int32 Start = 0; Start < NumCells; ++Start)
				{
					if (Zones[Start] == INDEX_NONE || Piece[Start] != INDEX_NONE)
					{
						continue;
					}
					const int32 Index = Sizes.Add(0);
					ZoneOfPiece.Add(Zones[Start]);
					TArray<int32> Front = {Start};
					Piece[Start] = Index;
					for (int32 At = 0; At < Front.Num(); ++At)
					{
						++Sizes[Index];
						for (const int32 Neighbor : Cells[Front[At]].Neighbors)
						{
							if (Piece[Neighbor] == INDEX_NONE && Zones[Neighbor] == Zones[Start])
							{
								Piece[Neighbor] = Index;
								Front.Add(Neighbor);
							}
						}
					}
				}
				// the largest piece of each zone stays
				TArray<int32> Largest;
				Largest.Init(INDEX_NONE, NumZones);
				for (int32 Index = 0; Index < Sizes.Num(); ++Index)
				{
					const int32 Zone = ZoneOfPiece[Index];
					if (Largest[Zone] == INDEX_NONE || Sizes[Index] > Sizes[Largest[Zone]])
					{
						Largest[Zone] = Index;
					}
				}
				bool bChanged = false;
				for (int32 Cell = 0; Cell < NumCells; ++Cell)
				{
					if (Piece[Cell] == INDEX_NONE || Largest[Zones[Cell]] == Piece[Cell])
					{
						continue;
					}
					int32 Votes[MaxZones] = {};
					for (const int32 Neighbor : Cells[Cell].Neighbors)
					{
						if (Zones[Neighbor] != INDEX_NONE && Zones[Neighbor] != Zones[Cell])
						{
							++Votes[Zones[Neighbor]];
						}
					}
					int32 Best = INDEX_NONE;
					for (int32 Zone = 0; Zone < NumZones; ++Zone)
					{
						Best = Votes[Zone] > 0 && (Best == INDEX_NONE || Votes[Zone] > Votes[Best]) ? Zone : Best;
					}
					if (Best != INDEX_NONE)
					{
						Zones[Cell] = int8(Best);
						bChanged = true;
					}
				}
				if (!bChanged)
				{
					return;
				}
			}
		}

		/** Steps of neighbors from the sources, over the cells the filter lets through; INDEX_NONE where it does not come */
		template <typename FFilter>
		TArray<int32> Flood(TConstArrayView<int32> Sources, FFilter&& Filter, TArray<int32>* OutParents = nullptr) const
		{
			TArray<int32> Steps;
			Steps.Init(INDEX_NONE, NumCells);
			if (OutParents)
			{
				OutParents->Init(INDEX_NONE, NumCells);
			}
			TArray<int32> Front;
			for (const int32 Source : Sources)
			{
				if (Cells.IsValidIndex(Source) && Steps[Source] == INDEX_NONE)
				{
					Steps[Source] = 0;
					Front.Add(Source);
				}
			}
			for (int32 At = 0; At < Front.Num(); ++At)
			{
				const int32 Cell = Front[At];
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					if (Steps[Neighbor] == INDEX_NONE && Filter(Neighbor))
					{
						Steps[Neighbor] = Steps[Cell] + 1;
						if (OutParents)
						{
							(*OutParents)[Neighbor] = Cell;
						}
						Front.Add(Neighbor);
					}
				}
			}
			return Steps;
		}

		/** The cells around a castle, which every way of its zone starts from */
		TArray<int32> GetCastleRing(const TArray<int32>& Castle) const
		{
			TArray<int32> Ring;
			for (const int32 Cell : Castle)
			{
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					if (!Castle.Contains(Neighbor))
					{
						Ring.AddUnique(Neighbor);
					}
				}
			}
			return Ring;
		}

		TArray<int32> GetZoneStarts(int32 Zone) const
		{
			TArray<int32> Starts;
			for (const TArray<int32>& Castle : Out.Zones[Zone].Castles)
			{
				Starts.Append(GetCastleRing(Castle));
			}
			return Starts;
		}

		/** Finds the cell nearest a direction whose castle fits: a hexagon of the zone, two of its neighbors around a corner, and the land
		 *  two rings around them inside the zone, at least Spacing steps from the castles before it */
		bool PlaceCastle(int32 Zone, const FVector3d& Direction, bool bNeeded)
		{
			const int32 Near = Grid.FindCell(Direction.GetSafeNormal());
			const TArray<int32> Steps = Flood({Near}, [](int32) { return true; });
			TArray<int32> Taken;
			for (const TArray<int32>& Castle : Out.Zones[Zone].Castles)
			{
				Taken.Append(Castle);
			}
			const TArray<int32> FromTaken = Flood(Taken, [](int32) { return true; });
			TArray<int32> Order;
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (Steps[Cell] != INDEX_NONE && Steps[Cell] <= 14 && (FromTaken[Cell] == INDEX_NONE || FromTaken[Cell] >= 8))
				{
					Order.Add(Cell);
				}
			}
			Order.Sort([&Steps](int32 A, int32 B) { return Steps[A] != Steps[B] ? Steps[A] < Steps[B] : A < B; });
			TArray<int32> Around;
			for (const int32 Cell : Order)
			{
				const FHexSphereCell& Each = Cells[Cell];
				if (Each.IsPentagon() || !IsInterior(Cell, Zone) || Locked[Cell])
				{
					continue;
				}
				for (int32 K = 0; K < Each.Neighbors.Num(); ++K)
				{
					const TArray<int32> Castle = {Cell, Each.Neighbors[K], Each.Neighbors[(K + 1) % Each.Neighbors.Num()]};
					bool bFits = true;
					for (const int32 Part : Castle)
					{
						HexShroud::FindWithin(Grid, Part, 2, Around);
						for (const int32 Near2 : Around)
						{
							bFits &= IsInterior(Near2, Zone) && !Cells[Near2].IsPentagon() && !Locked[Near2];
						}
					}
					if (!bFits)
					{
						continue;
					}
					// the castle and the ring around it are plains, the land two rings out never sea nor mountains
					for (const int32 Part : Castle)
					{
						HexShroud::FindWithin(Grid, Part, 2, Around);
						for (const int32 Near2 : Around)
						{
							if (!IsWalk(Out.Levels[Near2]))
							{
								Out.Levels[Near2] = Plains;
							}
						}
					}
					for (const int32 Ring : GetCastleRing(Castle))
					{
						Out.Levels[Ring] = Plains;
						Locked[Ring] = true;
						Out.Clear.AddUnique(Ring);
					}
					for (const int32 Part : Castle)
					{
						Out.Levels[Part] = Plains;
						Locked[Part] = true;
						Closed[Part] = true;
					}
					Out.Zones[Zone].Castles.Add(Castle);
					return true;
				}
			}
			if (bNeeded)
			{
				Out.Problems.Add(FString::Printf(TEXT("no room for a castle in zone %d"), Zone));
			}
			return false;
		}

		/** Carves the land of a zone its castles do not reach: from each such piece the cheapest way to what they reach becomes plains, over
		 *  the cells of the zone nothing locks */
		void Repair(int32 Zone)
		{
			const TArray<int32> Starts = GetZoneStarts(Zone);
			const auto Less = [](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; };
			for (int32 Round = 0; Round < 600; ++Round)
			{
				const TArray<int32> Steps = Flood(Starts, [&](int32 Cell) { return IsInterior(Cell, Zone) && IsWalk(Out.Levels[Cell]) && !Closed[Cell]; });
				int32 Lost = INDEX_NONE;
				for (int32 Cell = 0; Cell < NumCells && Lost == INDEX_NONE; ++Cell)
				{
					if (Steps[Cell] == INDEX_NONE && IsInterior(Cell, Zone) && IsWalk(Out.Levels[Cell]) && !Closed[Cell] && !Exempt[Cell])
					{
						Lost = Cell;
					}
				}
				if (Lost == INDEX_NONE)
				{
					return;
				}
				// the cheapest way from it to the land reached: walkable land costs nearly nothing, a mountain 1, the sea more
				TArray<double> Cost;
				Cost.Init(UE_DOUBLE_BIG_NUMBER, NumCells);
				TArray<int32> Parent;
				Parent.Init(INDEX_NONE, NumCells);
				TArray<TPair<double, int32>> Heap;
				Cost[Lost] = 0.0;
				Heap.HeapPush({0.0, Lost}, Less);
				int32 Reached = INDEX_NONE;
				while (!Heap.IsEmpty())
				{
					TPair<double, int32> Top;
					Heap.HeapPop(Top, Less);
					if (Top.Key > Cost[Top.Value])
					{
						continue;
					}
					if (Steps[Top.Value] != INDEX_NONE)
					{
						Reached = Top.Value;
						break;
					}
					for (const int32 Neighbor : Cells[Top.Value].Neighbors)
					{
						if (!IsInterior(Neighbor, Zone) || Closed[Neighbor] || Exempt[Neighbor] || (Locked[Neighbor] && !IsWalk(Out.Levels[Neighbor])))
						{
							continue;
						}
						const int8 Level = Out.Levels[Neighbor];
						const double Step = IsWalk(Level) ? 0.05 : Level == Mountain ? 1.0 : Level == Shallows ? 1.6 : 2.2;
						if (Top.Key + Step < Cost[Neighbor])
						{
							Cost[Neighbor] = Top.Key + Step;
							Parent[Neighbor] = Top.Value;
							Heap.HeapPush({Cost[Neighbor], Neighbor}, Less);
						}
					}
				}
				if (Reached == INDEX_NONE)
				{
					if (Locked[Lost])
					{
						Out.Problems.Add(FString::Printf(TEXT("cell %d of zone %d is out of reach"), Lost, Zone));
						return;
					}
					// nothing joins it: it becomes a mountain, which nobody needs to reach
					Out.Levels[Lost] = Cells[Lost].IsPentagon() ? DeepSea : Mountain;
					continue;
				}
				for (int32 Cell = Reached; Cell != INDEX_NONE; Cell = Parent[Cell])
				{
					if (!IsWalk(Out.Levels[Cell]))
					{
						Out.Levels[Cell] = Plains;
					}
				}
			}
			Out.Problems.Add(FString::Printf(TEXT("the land of zone %d could not be joined"), Zone));
		}

		/** Whether a hero from one cell reaches another over walkable land with some cells closed */
		bool Reaches(int32 From, int32 To, const TBitArray<>& Blocked) const
		{
			const TArray<int32> Steps = Flood({From}, [&](int32 Cell) { return IsWalk(Out.Levels[Cell]) && !Closed[Cell] && !Blocked[Cell]; });
			return Steps[To] != INDEX_NONE;
		}

		TBitArray<> GetGuardCells() const
		{
			TBitArray<> Guards(false, NumCells);
			for (const HexZoneMap::FGuard& Guard : Out.Guards)
			{
				Guards[Guard.Cell] = true;
			}
			return Guards;
		}

		/** The cells of the zone From beside the zone To, along their range from south to north, or by the angle around their border */
		TArray<int32> FindBorder(int32 From, int32 To) const
		{
			TArray<int32> Border;
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (Out.ZoneOf[Cell] != From || Cells[Cell].IsPentagon())
				{
					continue;
				}
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					if (Out.ZoneOf[Neighbor] == To)
					{
						Border.Add(Cell);
						break;
					}
				}
			}
			if (Border.Num() < 2)
			{
				return Border;
			}
			// along the line from one end of the border to the other: the two cells furthest apart
			FVector3d Middle = FVector3d::ZeroVector;
			for (const int32 Cell : Border)
			{
				Middle += Cells[Cell].Center;
			}
			Middle = Middle.GetSafeNormal();
			int32 End = Border[0];
			for (const int32 Cell : Border)
			{
				End = FVector3d::DotProduct(Cells[Cell].Center, Middle) < FVector3d::DotProduct(Cells[End].Center, Middle) ? Cell : End;
			}
			const FVector3d Along = (Cells[End].Center - Middle).GetSafeNormal();
			Border.Sort([&](int32 A, int32 B) { return FVector3d::DotProduct(Cells[A].Center, Along) < FVector3d::DotProduct(Cells[B].Center, Along); });
			return Border;
		}

		/** A pass over the range between two zones: from a cell of the range near a share of its length the shortest way to the land of each
		 *  zone, one cell wide, its guard on a cell of the range that closes it */
		bool PlacePass(int32 From, int32 To, double Share)
		{
			const TArray<int32> Border = FindBorder(From, To);
			if (Border.Num() < 4)
			{
				Out.Problems.Add(FString::Printf(TEXT("zones %d and %d share too short a range for a pass"), From, To));
				return false;
			}
			const int32 Middle = FMath::Clamp(FMath::RoundToInt32(Share * (Border.Num() - 1)), 0, Border.Num() - 1);
			const TBitArray<> Guards = GetGuardCells();
			for (int32 Try = 0; Try < 24; ++Try)
			{
				const int32 Index = Middle + (Try % 2 == 0 ? Try / 2 : -(Try + 1) / 2);
				if (!Border.IsValidIndex(Index) || Locked[Border[Index]])
				{
					continue;
				}
				const int32 Start = Border[Index];
				// it keeps clear of the castles and of the passes and pockets before it
				const auto Open = [&](int32 Cell) { return Out.ZoneOf[Cell] != INDEX_NONE && !Cells[Cell].IsPentagon() && !Locked[Cell]; };
				TArray<int32> Parents;
				const TArray<int32> Steps = Flood({Start}, Open, &Parents);
				int32 Ends[2] = {INDEX_NONE, INDEX_NONE};
				const int32 Zones[2] = {From, To};
				for (int32 Side = 0; Side < 2; ++Side)
				{
					for (int32 Cell = 0; Cell < NumCells; ++Cell)
					{
						if (Steps[Cell] != INDEX_NONE && IsInterior(Cell, Zones[Side]) && (Ends[Side] == INDEX_NONE || Steps[Cell] < Steps[Ends[Side]]))
						{
							Ends[Side] = Cell;
						}
					}
				}
				if (Ends[0] == INDEX_NONE || Ends[1] == INDEX_NONE)
				{
					continue;
				}
				// the way from the land of From over the range to the land of To
				TArray<int32> Way;
				for (int32 Cell = Ends[0]; Cell != INDEX_NONE; Cell = Parents[Cell])
				{
					Way.Add(Cell);
				}
				TArray<int32> Second;
				for (int32 Cell = Ends[1]; Cell != Start && Cell != INDEX_NONE; Cell = Parents[Cell])
				{
					Second.Add(Cell);
				}
				Algo::Reverse(Second);
				Way.Append(Second);
				if (Way.Num() < 3)
				{
					continue;
				}
				TArray<int8> Before;
				for (const int32 Cell : Way)
				{
					Before.Add(Out.Levels[Cell]);
					if (!IsWalk(Out.Levels[Cell]) || Out.Range[Cell])
					{
						Out.Levels[Cell] = Plains;
					}
				}
				// the guard stands on a cell of the range that closes the way, the one nearest its middle
				int32 Guard = INDEX_NONE;
				const int32 Half = Way.Num() / 2;
				for (int32 Step = 0; Step < Way.Num() && Guard == INDEX_NONE; ++Step)
				{
					const int32 At = Half + (Step % 2 == 0 ? Step / 2 : -(Step + 1) / 2);
					if (At <= 0 || At >= Way.Num() - 1 || !Out.Range[Way[At]])
					{
						continue;
					}
					TBitArray<> Blocked = Guards;
					Blocked[Way[At]] = true;
					if (!Reaches(Way[0], Way.Last(), Blocked))
					{
						Guard = Way[At];
					}
				}
				if (Guard == INDEX_NONE)
				{
					for (int32 Index2 = 0; Index2 < Way.Num(); ++Index2)
					{
						Out.Levels[Way[Index2]] = Before[Index2];
					}
					continue;
				}
				for (const int32 Cell : Way)
				{
					Locked[Cell] = true;
					Out.Clear.AddUnique(Cell);
				}
				Out.Guards.Add({Guard, From, To, INDEX_NONE});
				return true;
			}
			Out.Problems.Add(FString::Printf(TEXT("no pass between zones %d and %d"), From, To));
			return false;
		}

		/** An entrance in a zone, Min to Max steps from its castles, far enough from the ranges, the castles and the other entrances: in a pocket
		 *  of mountains open only to the cell of its guard for a network that leads to another zone, else with a mountain behind it */
		bool PlaceEntrance(int32 Zone, int32 Network, bool bPocket, int32 MinSteps, int32 MaxSteps)
		{
			const TArray<int32> Starts = GetZoneStarts(Zone);
			const TArray<int32> Steps = Flood(Starts, [&](int32 Cell) { return IsInterior(Cell, Zone) && IsWalk(Out.Levels[Cell]) && !Closed[Cell]; });
			TArray<int32> Taken;
			for (const FHexEntrance& Entrance : Out.Entrances)
			{
				Taken.Add(Entrance.Cell);
			}
			for (const TArray<int32>& Castle : Out.Zones[Zone].Castles)
			{
				Taken.Append(Castle);
			}
			const TArray<int32> FromTaken = Flood(Taken, [](int32) { return true; });
			// a smaller zone may have no cell so far: the steps widen until one fits
			for (int32 Widen = 0; Widen < 4; ++Widen)
			{
				const int32 Least = FMath::Max(MinSteps - Widen * 2, 2);
				const int32 Most = MaxSteps + Widen * 4;
				const int32 RangeClear = FMath::Max(Settings.EntranceRangeClear - Widen / 2, 2);
				const int32 Spacing = FMath::Max(Settings.EntranceSpacing - Widen, 3);
				TArray<int32> Candidates;
				for (int32 Cell = 0; Cell < NumCells; ++Cell)
				{
					if (Steps[Cell] == INDEX_NONE || Steps[Cell] < Least || Steps[Cell] > Most || Locked[Cell] || Cells[Cell].IsPentagon()
						|| RangeSteps[Cell] < RangeClear || (FromTaken[Cell] != INDEX_NONE && FromTaken[Cell] < Spacing))
					{
						continue;
					}
					bool bFits = true;
					for (const int32 Neighbor : Cells[Cell].Neighbors)
					{
						bFits &= IsInterior(Neighbor, Zone) && !Cells[Neighbor].IsPentagon() && !Locked[Neighbor];
					}
					if (bFits)
					{
						Candidates.Add(Cell);
					}
				}
				if (!Candidates.IsEmpty())
				{
					AddEntrance(Zone, Network, bPocket, Candidates[Random.RandHelper(Candidates.Num())], Steps);
					return true;
				}
			}
			Out.Problems.Add(FString::Printf(TEXT("no room for an entrance of network %c in zone %d"), TCHAR('A' + Network), Zone));
			return false;
		}

		void AddEntrance(int32 Zone, int32 Network, bool bPocket, int32 Cell, const TArray<int32>& Steps)
		{
			const FHexSphereCell& Each = Cells[Cell];
			// the side towards the castles opens, the one away from them is behind it
			const auto StepsOf = [&Steps](int32 Neighbor) { return Steps[Neighbor] == INDEX_NONE ? 100000 : Steps[Neighbor]; };
			int32 Front = 0;
			int32 Back = 0;
			for (int32 K = 1; K < Each.Neighbors.Num(); ++K)
			{
				Front = StepsOf(Each.Neighbors[K]) < StepsOf(Each.Neighbors[Front]) ? K : Front;
				Back = StepsOf(Each.Neighbors[K]) > StepsOf(Each.Neighbors[Back]) ? K : Back;
			}
			FHexEntrance& Entrance = Out.Entrances.AddDefaulted_GetRef();
			Entrance.Cell = Cell;
			Entrance.Network = Network;
			Out.Levels[Cell] = Plains;
			Locked[Cell] = true;
			Out.Clear.AddUnique(Cell);
			if (bPocket)
			{
				Entrance.Side = Front;
				// the exit below opens the same way, so the maze keeps the void behind it for its wall
				Entrance.ExitSide = Front;
				for (int32 K = 0; K < Each.Neighbors.Num(); ++K)
				{
					Out.Levels[Each.Neighbors[K]] = K == Front ? Plains : Mountain;
					Locked[Each.Neighbors[K]] = true;
				}
				Exempt[Cell] = true;
				Out.Clear.AddUnique(Each.Neighbors[Front]);
				Out.Guards.Add({Each.Neighbors[Front], Zone, INDEX_NONE, Network});
			}
			else
			{
				// built into the mountain behind it, so it opens the other way
				Entrance.Side = (Back + 3) % Each.Neighbors.Num();
				Entrance.ExitSide = Entrance.Side;
				Out.Levels[Each.Neighbors[Back]] = Mountain;
				Locked[Each.Neighbors[Back]] = true;
			}
		}

		/** The zones beside each other, by the cells of their border */
		TArray<FIntPoint> FindPairs() const
		{
			TArray<FIntPoint> Pairs;
			for (int32 A = 0; A < NumZones; ++A)
			{
				for (int32 B = A + 1; B < NumZones; ++B)
				{
					if (FindBorder(A, B).Num() >= 6)
					{
						Pairs.Add(FIntPoint(A, B));
					}
				}
			}
			return Pairs;
		}

		/** The ways into the zones, by the seed: a tree of ways joins every zone, passes between zones beside each other or a network between
		 *  any two, then more ways go to the zones that have fewer than MinAccesses, never past MaxAccesses, and at least one network where the
		 *  settings allow it */
		FWays ChooseWays()
		{
			FWays Best;
			for (int32 Try = 0; Try < 200; ++Try)
			{
				FWays Ways;
				Ways.Pairs = FindPairs();
				Ways.Passes.Init(0, Ways.Pairs.Num());
				const int32 MaxLinks = FMath::Clamp(Settings.MaxLinkNetworks, 0, HexPlanetFile::MaxNetworks);
				const auto CanAdd = [&](TConstArrayView<int32> Zones)
				{
					for (const int32 Zone : Zones)
					{
						if (Ways.Count(Zone) >= Settings.MaxAccesses)
						{
							return false;
						}
					}
					return true;
				};
				const auto AddPass = [&](int32 Pair)
				{
					if (Ways.Passes[Pair] < Settings.MaxPasses && CanAdd({Ways.Pairs[Pair].X, Ways.Pairs[Pair].Y}))
					{
						++Ways.Passes[Pair];
						return true;
					}
					return false;
				};
				const auto AddLink = [&](TArray<int32> Zones)
				{
					if (Ways.Links.Num() < MaxLinks && CanAdd(Zones))
					{
						Ways.Links.Add(MoveTemp(Zones));
						return true;
					}
					return false;
				};
				// a tree: each zone not joined yet joins one that is, by a pass where they lie beside each other, else by a network
				TArray<int32> Joined = {Random.RandHelper(NumZones)};
				TArray<int32> Left;
				for (int32 Zone = 0; Zone < NumZones; ++Zone)
				{
					if (Zone != Joined[0])
					{
						Left.Add(Zone);
					}
				}
				bool bFailed = false;
				while (!Left.IsEmpty() && !bFailed)
				{
					// a network now and then, as long as one may be added
					const bool bLink = MaxLinks > 0 && Random.FRand() < 0.3f;
					TArray<int32> Choices;
					for (int32 Pair = 0; Pair < Ways.Pairs.Num(); ++Pair)
					{
						const bool bA = Joined.Contains(Ways.Pairs[Pair].X) && Left.Contains(Ways.Pairs[Pair].Y);
						const bool bB = Joined.Contains(Ways.Pairs[Pair].Y) && Left.Contains(Ways.Pairs[Pair].X);
						if ((bA || bB) && Settings.MaxPasses > 0)
						{
							Choices.Add(Pair);
						}
					}
					if (!bLink && !Choices.IsEmpty())
					{
						const int32 Pair = Choices[Random.RandHelper(Choices.Num())];
						if (AddPass(Pair))
						{
							const int32 New = Joined.Contains(Ways.Pairs[Pair].X) ? Ways.Pairs[Pair].Y : Ways.Pairs[Pair].X;
							Joined.Add(New);
							Left.Remove(New);
							continue;
						}
					}
					const int32 New = Left[Random.RandHelper(Left.Num())];
					const int32 Old = Joined[Random.RandHelper(Joined.Num())];
					if (AddLink({Old, New}))
					{
						Joined.Add(New);
						Left.Remove(New);
					}
					else if (Choices.IsEmpty())
					{
						bFailed = true;
					}
				}
				if (bFailed)
				{
					continue;
				}
				// at least one network, which may join a third zone
				if (Ways.Links.IsEmpty() && MaxLinks > 0)
				{
					const int32 A = Random.RandHelper(NumZones);
					const int32 B = (A + 1 + Random.RandHelper(NumZones - 1)) % NumZones;
					AddLink({A, B});
				}
				// more ways to the zones that have too few, and a few at random
				for (int32 Round = 0; Round < 40; ++Round)
				{
					int32 Short = INDEX_NONE;
					for (int32 Zone = 0; Zone < NumZones; ++Zone)
					{
						Short = Ways.Count(Zone) < Settings.MinAccesses && (Short == INDEX_NONE || Ways.Count(Zone) < Ways.Count(Short)) ? Zone : Short;
					}
					if (Short == INDEX_NONE)
					{
						if (Random.FRand() < 0.6f)
						{
							break;
						}
						Short = Random.RandHelper(NumZones);
					}
					TArray<int32> Choices;
					for (int32 Pair = 0; Pair < Ways.Pairs.Num(); ++Pair)
					{
						if (Ways.Pairs[Pair].X == Short || Ways.Pairs[Pair].Y == Short)
						{
							Choices.Add(Pair);
						}
					}
					bool bAdded = false;
					if (!Choices.IsEmpty() && Random.FRand() < 0.7f)
					{
						bAdded = AddPass(Choices[Random.RandHelper(Choices.Num())]);
					}
					if (!bAdded)
					{
						// a network to another zone, or one already of a network of two, which then joins three
						const int32 Other = (Short + 1 + Random.RandHelper(NumZones - 1)) % NumZones;
						int32 Grow = INDEX_NONE;
						for (int32 Link = 0; Link < Ways.Links.Num(); ++Link)
						{
							Grow = Ways.Links[Link].Num() == 2 && !Ways.Links[Link].Contains(Short) && Ways.Count(Short) < Settings.MaxAccesses ? Link : Grow;
						}
						if (Grow != INDEX_NONE && Random.FRand() < 0.5f)
						{
							Ways.Links[Grow].Add(Short);
						}
						else
						{
							AddLink({Short, Other});
						}
					}
				}
				bool bFits = Ways.Joins(NumZones);
				for (int32 Zone = 0; Zone < NumZones; ++Zone)
				{
					bFits &= Ways.Count(Zone) >= Settings.MinAccesses && Ways.Count(Zone) <= Settings.MaxAccesses;
				}
				if (bFits)
				{
					return Ways;
				}
				Best = Ways;
			}
			Out.Problems.Add(TEXT("no ways fit MinAccesses and MaxAccesses"));
			return Best;
		}

		void Make(TConstArrayView<int8> BaseLevels)
		{
			Out.Levels = TArray<int8>(BaseLevels);
			// the zones, turned around the axis so the fewest pentagons lie in a range
			TArray<FVector3d> Offsets;
			for (int32 Index = 0; Index <= MaxZones; ++Index)
			{
				Offsets.Add(FVector3d(Random.FRandRange(-100.0f, 100.0f), Random.FRandRange(-100.0f, 100.0f), Random.FRandRange(-100.0f, 100.0f)));
			}
			const double BaseTurn = Random.FRandRange(0.0f, UE_TWO_PI);
			double BestTurn = BaseTurn;
			int32 BestCount = TNumericLimits<int32>::Max();
			TArray<int8> Zones;
			const int32 Tries = FMath::Max(Settings.RotationTries, 1);
			for (int32 Try = 0; Try < Tries; ++Try)
			{
				const double Turn = BaseTurn + UE_DOUBLE_TWO_PI / NumZones * Try / Tries;
				AssignZones(Turn, Offsets, Zones);
				int32 Count = 0;
				for (int32 Cell = 0; Cell < 12; ++Cell)
				{
					bool bNear = IsBorder(Zones, Cell);
					for (const int32 Neighbor : Cells[Cell].Neighbors)
					{
						bNear |= IsBorder(Zones, Neighbor);
					}
					Count += bNear ? 1 : 0;
				}
				if (Count < BestCount)
				{
					BestCount = Count;
					BestTurn = Turn;
				}
			}
			AssignZones(BestTurn, Offsets, Out.ZoneOf);
			JoinPieces(Out.ZoneOf);

			// the ranges: the cells beside another zone, and by a noise some of the cells beside those
			Out.Range.Init(false, NumCells);
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				Out.Range[Cell] = IsBorder(Out.ZoneOf, Cell);
			}
			const FVector3d ThickOffset(Random.FRandRange(-100.0f, 100.0f), Random.FRandRange(-100.0f, 100.0f), Random.FRandRange(-100.0f, 100.0f));
			TBitArray<> Thick(false, NumCells);
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (Out.Range[Cell] || Out.ZoneOf[Cell] == INDEX_NONE)
				{
					continue;
				}
				bool bBeside = false;
				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					bBeside |= Out.Range[Neighbor];
				}
				const double Noise = 0.5 + 0.5 * double(FMath::PerlinNoise3D(FVector(Cells[Cell].Center * 9.0 + ThickOffset)));
				Thick[Cell] = bBeside && Noise < double(Settings.RangeThickShare);
			}
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				Out.Range[Cell] = Out.Range[Cell] || Thick[Cell];
			}
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (Out.ZoneOf[Cell] == INDEX_NONE)
				{
					// the polar sea, shallow along its coast
					bool bCoast = false;
					for (const int32 Neighbor : Cells[Cell].Neighbors)
					{
						bCoast |= Out.ZoneOf[Neighbor] != INDEX_NONE;
					}
					Out.Levels[Cell] = bCoast ? Shallows : DeepSea;
				}
				else if (Out.Range[Cell])
				{
					Out.Levels[Cell] = Cells[Cell].IsPentagon() ? DeepSea : Mountain;
				}
			}
			TArray<int32> RangeCells;
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (Out.Range[Cell] || Out.ZoneOf[Cell] == INDEX_NONE)
				{
					RangeCells.Add(Cell);
				}
			}
			RangeSteps = Flood(RangeCells, [](int32) { return true; });
			for (int32& Steps : RangeSteps)
			{
				Steps = Steps == INDEX_NONE ? 1000 : Steps;
			}

			// the middles of the zones, and who each is for: zone 0 the person's, the AiZones furthest from it the game's, by turns
			for (int32 Zone = 0; Zone < NumZones; ++Zone)
			{
				HexZoneMap::FZone& Each = Out.Zones.AddDefaulted_GetRef();
				FVector3d Sum = FVector3d::ZeroVector;
				for (int32 Cell = 0; Cell < NumCells; ++Cell)
				{
					Sum += Out.ZoneOf[Cell] == Zone && !Out.Range[Cell] ? Cells[Cell].Center : FVector3d::ZeroVector;
				}
				Each.Center = Sum.GetSafeNormal();
			}
			Out.Zones[0].Role = HexZoneMap::ERole::Person;
			Out.Zones[0].Player = Settings.PersonPlayer;
			TArray<int32> Players = {0};
			for (int32 Index = 0; Index < AiZones; ++Index)
			{
				int32 Far = INDEX_NONE;
				double FarScore = -1.0;
				for (int32 Zone = 1; Zone < NumZones; ++Zone)
				{
					if (Players.Contains(Zone))
					{
						continue;
					}
					double Score = UE_DOUBLE_BIG_NUMBER;
					for (const int32 Other : Players)
					{
						Score = FMath::Min(Score, FVector3d::Distance(Out.Zones[Zone].Center, Out.Zones[Other].Center));
					}
					if (Score > FarScore)
					{
						FarScore = Score;
						Far = Zone;
					}
				}
				if (Far == INDEX_NONE)
				{
					break;
				}
				Players.Add(Far);
				Out.Zones[Far].Role = HexZoneMap::ERole::Ai;
				Out.Zones[Far].Player = HexZoneMap::GetAiPlayer(Settings, Index);
			}

			// the castles: of a player in the middle of its zone with its hero beside it, of nobody in the middle and north and south of it
			for (int32 Zone = 0; Zone < NumZones; ++Zone)
			{
				HexZoneMap::FZone& Each = Out.Zones[Zone];
				if (Each.Role == HexZoneMap::ERole::Nobody)
				{
					const FVector3d North = (FVector3d::UpVector - Each.Center * Each.Center.Z).GetSafeNormal();
					const double Latitude = FMath::DegreesToRadians(double(Settings.NobodyCastleLatitude));
					for (int32 Castle = 0; Castle < FMath::Max(Settings.NobodyCastles, 1); ++Castle)
					{
						const double Sign = Castle == 0 ? 0.0 : Castle % 2 == 1 ? 1.0 : -1.0;
						PlaceCastle(Zone, Each.Center * FMath::Cos(Latitude * Sign) + North * FMath::Sin(Latitude * Sign), Castle == 0);
					}
				}
				else if (PlaceCastle(Zone, Each.Center, true))
				{
					// its hero starts beside it, on its south side, where the camera that looks north sees it
					const TArray<int32> Ring = GetCastleRing(Each.Castles[0]);
					int32 Hero = Ring[0];
					for (const int32 Cell : Ring)
					{
						Hero = Cells[Cell].Center.Z < Cells[Hero].Center.Z ? Cell : Hero;
					}
					Each.HeroCell = Hero;
					// and empty castles of nobody north and south of its own, which only its first castle is
					const FVector3d North = (FVector3d::UpVector - Each.Center * Each.Center.Z).GetSafeNormal();
					const double Latitude = FMath::DegreesToRadians(double(Settings.NobodyCastleLatitude) * 1.4);
					for (int32 Castle = 0; Castle < Settings.PlayerZoneNobodyCastles; ++Castle)
					{
						const double Sign = Castle % 2 == 0 ? 1.0 : -1.0;
						PlaceCastle(Zone, Each.Center * FMath::Cos(Latitude * Sign) + North * FMath::Sin(Latitude * Sign), false);
					}
				}
			}
			if (!Out.Problems.IsEmpty())
			{
				return;
			}
			for (int32 Zone = 0; Zone < NumZones; ++Zone)
			{
				Repair(Zone);
			}

			// the ways
			const FWays Ways = ChooseWays();
			for (int32 Pair = 0; Pair < Ways.Pairs.Num(); ++Pair)
			{
				const int32 Count = Ways.Passes[Pair];
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const double Share = FMath::Clamp((Index + 1.0) / (Count + 1.0) + Random.FRandRange(-Settings.PassJitter, Settings.PassJitter), 0.15, 0.85);
					PlacePass(Ways.Pairs[Pair].X, Ways.Pairs[Pair].Y, Share);
				}
			}
			int32 Network = 0;
			for (const TArray<int32>& Link : Ways.Links)
			{
				for (const int32 Zone : Link)
				{
					PlaceEntrance(Zone, Network, true, Settings.LinkEntranceMinSteps, Settings.LinkEntranceMaxSteps);
				}
				++Network;
			}
			// networks of their own for the zones, the players' first, as far as the networks go
			TArray<int32> Own = Players;
			for (int32 Zone = 0; Zone < NumZones; ++Zone)
			{
				Own.AddUnique(Zone);
			}
			for (int32 Round = 0; Round < Settings.OwnNetworks; ++Round)
			{
				for (const int32 Zone : Own)
				{
					if (Network >= HexPlanetFile::MaxNetworks)
					{
						break;
					}
					for (int32 Entrance = 0; Entrance < 2; ++Entrance)
					{
						PlaceEntrance(Zone, Network, false, Settings.OwnEntranceMinSteps, Settings.OwnEntranceMaxSteps);
					}
					++Network;
				}
			}
			for (int32 Zone = 0; Zone < NumZones; ++Zone)
			{
				Repair(Zone);
				Out.Zones[Zone].Accesses = Ways.Count(Zone);
				if (Out.Zones[Zone].HeroCell != INDEX_NONE)
				{
					Out.Clear.AddUnique(Out.Zones[Zone].HeroCell);
				}
			}
			// the text of the ways
			for (int32 Pair = 0; Pair < Ways.Pairs.Num(); ++Pair)
			{
				if (Ways.Passes[Pair] > 0)
				{
					Out.Ways += FString::Printf(TEXT("%s%d pass%s %d-%d"), Out.Ways.IsEmpty() ? TEXT("") : TEXT(", "), Ways.Passes[Pair],
						Ways.Passes[Pair] > 1 ? TEXT("es") : TEXT(""), Ways.Pairs[Pair].X, Ways.Pairs[Pair].Y);
				}
			}
			for (int32 Link = 0; Link < Ways.Links.Num(); ++Link)
			{
				TArray<FString> Names;
				for (const int32 Zone : Ways.Links[Link])
				{
					Names.Add(FString::FromInt(Zone));
				}
				Out.Ways += FString::Printf(TEXT("%snetwork %c %s"), Out.Ways.IsEmpty() ? TEXT("") : TEXT(", "), TCHAR('A' + Link), *FString::Join(Names, TEXT("-")));
			}
			Out.Clear.Sort();
			Out.Entrances.Sort([](const FHexEntrance& A, const FHexEntrance& B) { return A.Cell < B.Cell; });
		}
	};
}

const TCHAR* HexZoneMap::GetRoleName(ERole Role)
{
	return Role == ERole::Person ? TEXT("Person") : Role == ERole::Ai ? TEXT("Ai") : TEXT("Nobody");
}

int32 HexZoneMap::GetAiPlayer(const FHexZoneMapGeneration& Settings, int32 Index)
{
	int32 Player = Settings.AiPlayer;
	for (int32 Step = 0; Step < Index; ++Step)
	{
		Player = (Player + 1) % MaxZones;
		Player += Player == Settings.PersonPlayer ? 1 : 0;
	}
	return Player == Settings.PersonPlayer ? (Player + 1) % MaxZones : Player;
}

void HexZoneMap::Generate(const FHexSphereGrid& Grid, TConstArrayView<int8> BaseLevels, const FHexZoneMapGeneration& Settings, int32 NumZones, int32 AiZones,
	int32 Seed, FResult& Out)
{
	Out = FResult();
	NumZones = FMath::Clamp(NumZones, 2, MaxZones);
	AiZones = FMath::Clamp(AiZones, 0, NumZones - 1);
	if (BaseLevels.Num() != Grid.GetCells().Num())
	{
		Out.Problems.Add(TEXT("the levels do not fit the grid"));
		return;
	}
	FMaker Maker(Grid, Settings, NumZones, AiZones, Seed, Out);
	Maker.Make(BaseLevels);
	if (Out.IsGood())
	{
		Check(Grid, Settings, Out);
	}
}

void HexZoneMap::GenerateGood(const FHexSphereGrid& Grid, TConstArrayView<int8> BaseLevels, const FHexZoneMapGeneration& Settings, int32 NumZones, int32 AiZones,
	int32 Seed, FResult& Out, int32* OutSeed)
{
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const int32 Each = Seed + Try * 7919;
		Generate(Grid, BaseLevels, Settings, NumZones, AiZones, Each, Out);
		if (OutSeed)
		{
			*OutSeed = Each;
		}
		if (Out.IsGood())
		{
			return;
		}
		UE_LOG(LogHexHero, Log, TEXT("Zones: the seed %d gives a map with problems (%s), trying another"), Each, *FString::Join(Out.Problems, TEXT("; ")));
	}
}

void HexZoneMap::Check(const FHexSphereGrid& Grid, const FHexZoneMapGeneration& Settings, FResult& InOut)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const int32 NumCells = Cells.Num();
	TBitArray<> Closed(false, NumCells);
	for (const FZone& Zone : InOut.Zones)
	{
		for (const TArray<int32>& Castle : Zone.Castles)
		{
			for (const int32 Cell : Castle)
			{
				Closed[Cell] = true;
			}
		}
	}
	TBitArray<> Guards(false, NumCells);
	for (const FGuard& Guard : InOut.Guards)
	{
		Guards[Guard.Cell] = true;
	}
	// the entrances of a pocket lie behind their guards
	TBitArray<> Pockets(false, NumCells);
	for (const FGuard& Guard : InOut.Guards)
	{
		if (Guard.Network != INDEX_NONE)
		{
			for (const FHexEntrance& Entrance : InOut.Entrances)
			{
				Pockets[Entrance.Cell] = Pockets[Entrance.Cell] || (Entrance.Network == Guard.Network && Cells[Entrance.Cell].Neighbors.Contains(Guard.Cell));
			}
		}
	}
	const auto Walk = [&](int32 Cell) { return (InOut.Levels[Cell] == 0 || InOut.Levels[Cell] == 1) && !Closed[Cell] && !Guards[Cell]; };
	TArray<TBitArray<>> Reached;
	for (int32 Zone = 0; Zone < InOut.Zones.Num(); ++Zone)
	{
		TArray<int32> Front;
		TBitArray<> Seen(false, NumCells);
		for (const TArray<int32>& Castle : InOut.Zones[Zone].Castles)
		{
			for (const int32 Part : Castle)
			{
				for (const int32 Neighbor : Cells[Part].Neighbors)
				{
					if (Walk(Neighbor) && !Seen[Neighbor])
					{
						Seen[Neighbor] = true;
						Front.Add(Neighbor);
					}
				}
			}
		}
		for (int32 At = 0; At < Front.Num(); ++At)
		{
			for (const int32 Neighbor : Cells[Front[At]].Neighbors)
			{
				if (!Seen[Neighbor] && Walk(Neighbor))
				{
					Seen[Neighbor] = true;
					Front.Add(Neighbor);
				}
			}
		}
		// all the walkable land of the zone, but inside the pockets, and nothing of another zone
		int32 Lost = 0;
		int32 Foreign = INDEX_NONE;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (InOut.ZoneOf[Cell] == Zone && !InOut.Range[Cell] && Walk(Cell) && !Seen[Cell] && !Pockets[Cell])
			{
				++Lost;
			}
			if (Seen[Cell] && InOut.ZoneOf[Cell] != Zone && InOut.ZoneOf[Cell] != INDEX_NONE && !InOut.Range[Cell] && Foreign == INDEX_NONE)
			{
				Foreign = Cell;
			}
		}
		if (Foreign != INDEX_NONE)
		{
			InOut.Problems.Add(FString::Printf(TEXT("zone %d reaches cell %d of zone %d past the guards"), Zone, Foreign, InOut.ZoneOf[Foreign]));
		}
		if (Lost > 0)
		{
			InOut.Problems.Add(FString::Printf(TEXT("zone %d does not reach %d cells of its land"), Zone, Lost));
		}
		Reached.Add(MoveTemp(Seen));
	}
	// every guard is fought from each side it closes: a pass from both zones, the guard of a pocket from its zone and from its entrance
	for (const FGuard& Guard : InOut.Guards)
	{
		const auto Beside = [&](const TBitArray<>& Seen)
		{
			for (const int32 Neighbor : Cells[Guard.Cell].Neighbors)
			{
				if (Seen[Neighbor])
				{
					return true;
				}
			}
			return false;
		};
		if (!Reached.IsValidIndex(Guard.From) || !Beside(Reached[Guard.From]) || (Guard.To != INDEX_NONE && !Beside(Reached[Guard.To])))
		{
			InOut.Problems.Add(FString::Printf(TEXT("the guard on cell %d is out of reach"), Guard.Cell));
		}
	}
	for (int32 Zone = 0; Zone < InOut.Zones.Num(); ++Zone)
	{
		const int32 Accesses = InOut.Zones[Zone].Accesses;
		if (Accesses < Settings.MinAccesses || Accesses > Settings.MaxAccesses)
		{
			InOut.Problems.Add(FString::Printf(TEXT("zone %d has %d ways in"), Zone, Accesses));
		}
		for (const FHexEntrance& Entrance : InOut.Entrances)
		{
			if (InOut.ZoneOf[Entrance.Cell] == Zone && !Pockets[Entrance.Cell] && !Reached[Zone][Entrance.Cell])
			{
				InOut.Problems.Add(FString::Printf(TEXT("zone %d does not reach its entrance on cell %d"), Zone, Entrance.Cell));
			}
		}
	}
	// the entrances of a network that joins zones lie in that many zones
	int32 Expected = 0;
	for (const FZone& Zone : InOut.Zones)
	{
		Expected += Zone.Accesses;
	}
	int32 Found = InOut.Guards.Num();
	for (const FGuard& Guard : InOut.Guards)
	{
		Found += Guard.To != INDEX_NONE ? 1 : 0;
	}
	if (Found != Expected)
	{
		InOut.Problems.Add(FString::Printf(TEXT("%d ways were placed of %d"), Found, Expected));
	}
}

FHexZonesFile HexZoneMap::MakeFile(const FResult& Result, int32 Seed)
{
	FHexZonesFile File;
	File.Seed = Seed;
	File.Ways = Result.Ways;
	for (int32 Zone = 0; Zone < Result.Zones.Num(); ++Zone)
	{
		FHexZoneLine& Line = File.Zones.AddDefaulted_GetRef();
		Line.Index = Zone;
		Line.Role = GetRoleName(Result.Zones[Zone].Role);
		Line.Player = Result.Zones[Zone].Player;
		for (const TArray<int32>& Castle : Result.Zones[Zone].Castles)
		{
			Line.Castles.Append(Castle);
		}
		Line.Hero = Result.Zones[Zone].HeroCell;
		Line.Accesses = Result.Zones[Zone].Accesses;
	}
	for (const FGuard& Guard : Result.Guards)
	{
		FHexZoneGuardLine& Line = File.Guards.AddDefaulted_GetRef();
		Line.Cell = Guard.Cell;
		Line.Way = Guard.Network == INDEX_NONE ? TEXT("pass") : TEXT("network");
		Line.From = Guard.From;
		Line.To = Guard.To;
		Line.Network = Guard.Network == INDEX_NONE ? FString() : FString::Chr(TCHAR('A' + Guard.Network));
	}
	File.Clear = Result.Clear;
	return File;
}

bool HexZoneMap::LoadFile(const FString& PlanetFolder, FHexZonesFile& OutFile, FString& OutError)
{
	const FString Path = HexPlanetFile::GetFilePath(PlanetFolder, ZonesFile);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("%s is missing"), *Path);
		return false;
	}
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Text, &OutFile))
	{
		OutError = FString::Printf(TEXT("%s is not valid JSON"), *Path);
		return false;
	}
	return true;
}
