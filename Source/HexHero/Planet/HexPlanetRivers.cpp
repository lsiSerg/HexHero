// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetRivers.h"
#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HexHero.h"
#include "HexMaterialConfig.h"
#include "HexPlanet.h"
#include "HexPlanetConfig.h"
#include "HexPlanetFile.h"
#include "HexPlanetSurface.h"
#include "HexSphereGrid.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** The two corners of side Side of a cell, as indices of FHexSphereGrid::GetCorners: from corner Side - 1 to corner Side */
	void GetSideCorners(const FHexSphereCell& Cell, int32 Side, int32& OutFrom, int32& OutTo)
	{
		const int32 NumSides = Cell.Corners.Num();
		OutFrom = Cell.Corners[(Side + NumSides - 1) % NumSides];
		OutTo = Cell.Corners[Side];
	}

	/** The cells that meet at each corner */
	TArray<TArray<int32, TInlineAllocator<3>>> FindCornerCells(const FHexSphereGrid& Grid)
	{
		TArray<TArray<int32, TInlineAllocator<3>>> Result;
		Result.SetNum(Grid.GetCorners().Num());
		for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
		{
			for (const int32 Corner : Grid.GetCells()[Cell].Corners)
			{
				Result[Corner].Add(Cell);
			}
		}
		return Result;
	}

	/** The other end of the side between two cells that meet at a corner, or INDEX_NONE if they are no neighbors */
	int32 GetOtherCorner(const FHexSphereGrid& Grid, int32 A, int32 B, int32 Corner)
	{
		const FHexSphereCell& Cell = Grid.GetCells()[A];
		const int32 Side = Cell.Neighbors.IndexOfByKey(B);
		if (Side == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		int32 From, To;
		GetSideCorners(Cell, Side, From, To);
		return From == Corner ? To : To == Corner ? From : INDEX_NONE;
	}

	/** Where a ray from the center of the planet along a direction meets the triangles of a cell, flat in threes */
	bool FindGround(const TArray<FVector3f>& Triangles, const FVector3d& Direction, FVector3d& OutPosition)
	{
		double Best = -1.0;
		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			const FVector3d A(Triangles[Index]);
			const FVector3d B(Triangles[Index + 1]);
			const FVector3d C(Triangles[Index + 2]);
			const FVector3d AB = B - A;
			const FVector3d AC = C - A;
			const FVector3d P = FVector3d::CrossProduct(Direction, AC);
			const double Det = FVector3d::DotProduct(AB, P);
			if (FMath::Abs(Det) < 1.0e-12)
			{
				continue;
			}
			const FVector3d T = -A;
			const double U = FVector3d::DotProduct(T, P) / Det;
			const FVector3d Q = FVector3d::CrossProduct(T, AB);
			const double V = FVector3d::DotProduct(Direction, Q) / Det;
			if (U < -1.0e-6 || V < -1.0e-6 || U + V > 1.0 + 1.0e-6)
			{
				continue;
			}
			const double Distance = FVector3d::DotProduct(AC, Q) / Det;
			if (Distance > 0.0 && Distance > Best)
			{
				Best = Distance;
			}
		}
		if (Best < 0.0)
		{
			return false;
		}
		OutPosition = Direction * Best;
		return true;
	}
}

TArray<uint16> HexPlanetRivers::BuildNet(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, const TSet<uint64>& Edges)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<uint16> Net;
	Net.Init(0, Cells.Num());
	if (Edges.IsEmpty() || Levels.Num() != Cells.Num())
	{
		return Net;
	}

	// the sides the levels allow, as links between their corners
	struct FLink
	{
		int32 Corner;
		int32 Other;
		int32 Cell;
		int32 Side;
	};
	TArray<FLink> Links;
	TMap<int32, TArray<int32, TInlineAllocator<3>>> LinksByCorner;
	for (const uint64 Key : Edges)
	{
		int32 A, B;
		GetEdgeCells(Key, A, B);
		if (!Cells.IsValidIndex(A) || !Cells.IsValidIndex(B) || !IsLevelAllowed(Levels[A]) || !IsLevelAllowed(Levels[B]))
		{
			continue;
		}
		const int32 Side = Cells[A].Neighbors.IndexOfByKey(B);
		if (Side == INDEX_NONE)
		{
			continue;
		}
		int32 From, To;
		GetSideCorners(Cells[A], Side, From, To);
		const int32 Link = Links.Add({From, To, A, Side});
		LinksByCorner.FindOrAdd(From).Add(Link);
		LinksByCorner.FindOrAdd(To).Add(Link);
	}

	// the distance of each corner along the rivers from the nearest mouth, a corner with a cell of the sea; a net with none counts from its
	// lowest corner
	TArray<TArray<int32, TInlineAllocator<3>>> CornerCells = FindCornerCells(Grid);
	TMap<int32, int32> Distances;
	TArray<int32> Queue;
	const auto Spread = [&]()
	{
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			const int32 Corner = Queue[Index];
			for (const int32 Link : LinksByCorner[Corner])
			{
				const int32 Next = Links[Link].Corner == Corner ? Links[Link].Other : Links[Link].Corner;
				if (!Distances.Contains(Next))
				{
					Distances.Add(Next, Distances[Corner] + 1);
					Queue.Add(Next);
				}
			}
		}
		Queue.Reset();
	};

	TArray<int32> Corners;
	LinksByCorner.GetKeys(Corners);
	Corners.Sort();
	for (const int32 Corner : Corners)
	{
		if (CornerCells[Corner].ContainsByPredicate([&](int32 Cell) { return Levels[Cell] < 0; }))
		{
			Distances.Add(Corner, 0);
			Queue.Add(Corner);
		}
	}
	Spread();

	const auto GetHeight = [&](int32 Corner)
	{
		int32 Sum = 0;
		for (const int32 Cell : CornerCells[Corner])
		{
			Sum += Levels[Cell];
		}
		return Sum;
	};
	for (;;)
	{
		int32 Lowest = INDEX_NONE;
		for (const int32 Corner : Corners)
		{
			if (!Distances.Contains(Corner) && (Lowest == INDEX_NONE || GetHeight(Corner) < GetHeight(Lowest)))
			{
				Lowest = Corner;
			}
		}
		if (Lowest == INDEX_NONE)
		{
			break;
		}
		Distances.Add(Lowest, 0);
		Queue.Add(Lowest);
		Spread();
	}

	// each side flows from its corner further from the mouth, and in a loop of equal distances from the corner of the higher index
	for (const FLink& Link : Links)
	{
		const int32 FromDistance = Distances[Link.Corner];
		const int32 ToDistance = Distances[Link.Other];
		const bool bForward = FromDistance != ToDistance ? FromDistance > ToDistance : Link.Corner > Link.Other;
		const int32 Neighbor = Cells[Link.Cell].Neighbors[Link.Side];
		const int32 NeighborSide = Cells[Neighbor].Neighbors.IndexOfByKey(Link.Cell);

		// the neighbor walks the side the other way round
		Net[Link.Cell] |= uint16(1 << Link.Side) | (bForward ? uint16(1 << (Link.Side + FlowShift)) : uint16(0));
		Net[Neighbor] |= uint16(1 << NeighborSide) | (bForward ? uint16(0) : uint16(1 << (NeighborSide + FlowShift)));
	}
	return Net;
}

TArray<int32> HexPlanetRivers::FindChangedCells(TConstArrayView<uint16> Old, TConstArrayView<uint16> New)
{
	TArray<int32> Changed;
	for (int32 Cell = 0; Cell < New.Num(); ++Cell)
	{
		if (!Old.IsValidIndex(Cell) || Old[Cell] != New[Cell])
		{
			Changed.Add(Cell);
		}
	}
	return Changed;
}

namespace
{
	/** The distance from a point to a segment, and where along it the nearest point lies */
	double GetSegmentDistance(const FVector3d& Point, const FVector3d& A, const FVector3d& B, double& OutT)
	{
		const FVector3d AB = B - A;
		const double Length = AB.SizeSquared();
		OutT = Length > 0.0 ? FMath::Clamp(FVector3d::DotProduct(Point - A, AB) / Length, 0.0, 1.0) : 0.0;
		return FVector3d::Distance(Point, A + AB * OutT);
	}

	/** A vector along the surface at a point, a direction from the center */
	FVector3d AlongSurface(const FVector3d& Vector, const FVector3d& Up)
	{
		return (Vector - Up * FVector3d::DotProduct(Vector, Up)).GetSafeNormal();
	}
}

double HexPlanetRivers::GetReach(const UHexPlanetConfig& Settings)
{
	const double Banks = Settings.RiverWidth * 0.5 + Settings.RiverBankWidth + FMath::Max(Settings.RiverLeveeWidth, Settings.RiverRunupGap);
	return FMath::Max(Banks, double(Settings.RiverValleyBottom + Settings.RiverValleySlope)) + Settings.HexRadius * 0.2;
}

TSharedRef<const HexPlanetRivers::FCurve> HexPlanetRivers::BuildCurve(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<uint16> Net,
	const FCurveParams& Params)
{
	TSharedRef<FCurve> Result = MakeShared<FCurve>();
	FCurve& Curve = *Result;
	Curve.Radius = Params.Radius;
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const TArray<FVector3d>& Corners = Grid.GetCorners();
	Curve.CellSegments.SetNum(Cells.Num());
	if (Net.Num() != Cells.Num() || Levels.Num() != Cells.Num() || Params.Radius <= 0.0)
	{
		return Result;
	}

	// the sides of the net, each once, from the corner it flows from to the one it flows to
	struct FFlowEdge
	{
		int32 From;
		int32 To;
		int32 CellA;
		int32 CellB;
	};
	TArray<FFlowEdge> FlowEdges;
	TMap<int32, TArray<int32, TInlineAllocator<3>>> InEdges;
	TMap<int32, TArray<int32, TInlineAllocator<3>>> CornerEdges;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const FHexSphereCell& Each = Cells[Cell];
		for (int32 Side = 0; Side < Each.Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Each.Neighbors[Side];
			if (!(Net[Cell] >> Side & 1) || Neighbor < Cell)
			{
				continue;
			}
			int32 From, To;
			GetSideCorners(Each, Side, From, To);
			const bool bForward = (Net[Cell] >> (Side + FlowShift) & 1) != 0;
			const int32 Index = FlowEdges.Add({bForward ? From : To, bForward ? To : From, Cell, Neighbor});
			InEdges.FindOrAdd(FlowEdges[Index].To).Add(Index);
			CornerEdges.FindOrAdd(From).Add(Index);
			CornerEdges.FindOrAdd(To).Add(Index);
		}
	}
	if (FlowEdges.IsEmpty())
	{
		return Result;
	}

	// how many sides lie upstream of each side, so a line goes on through a corner along the larger river that comes into it
	TArray<int32> Upstream;
	Upstream.Init(0, FlowEdges.Num());
	TArray<uint8> State;
	State.Init(0, FlowEdges.Num());
	for (int32 Start = 0; Start < FlowEdges.Num(); ++Start)
	{
		TArray<int32> Stack{Start};
		while (!Stack.IsEmpty())
		{
			const int32 Edge = Stack.Last();
			if (State[Edge] == 2)
			{
				Stack.Pop();
				continue;
			}
			const TArray<int32, TInlineAllocator<3>>* Ins = InEdges.Find(FlowEdges[Edge].From);
			if (State[Edge] == 0)
			{
				// first the rivers upstream; one already on the stack is a loop, which counts as nothing
				State[Edge] = 1;
				if (Ins)
				{
					for (const int32 In : *Ins)
					{
						if (State[In] == 0)
						{
							Stack.Add(In);
						}
					}
				}
				continue;
			}
			int32 Most = 0;
			if (Ins)
			{
				for (const int32 In : *Ins)
				{
					Most = FMath::Max(Most, State[In] == 2 ? Upstream[In] : 0);
				}
			}
			Upstream[Edge] = Most + 1;
			State[Edge] = 2;
			Stack.Pop();
		}
	}

	// at each corner, the two of its sides a line goes on through: both at a corner of two, whichever way they flow, so a river stays one line
	// where its flow parts or meets; at a corner of three the river flowing out and the largest coming in, else the two largest, and the third
	// ends there, on the line of the others
	TMap<int32, TPair<int32, int32>> Through;
	for (const TPair<int32, TArray<int32, TInlineAllocator<3>>>& Pair : CornerEdges)
	{
		const TArray<int32, TInlineAllocator<3>>& Edges = Pair.Value;
		if (Edges.Num() == 2)
		{
			Through.Add(Pair.Key, {Edges[0], Edges[1]});
			continue;
		}
		if (Edges.Num() < 3)
		{
			continue;
		}
		int32 BestIn = INDEX_NONE;
		int32 Out = INDEX_NONE;
		for (const int32 Edge : Edges)
		{
			if (FlowEdges[Edge].To == Pair.Key)
			{
				BestIn = BestIn == INDEX_NONE || Upstream[Edge] > Upstream[BestIn] ? Edge : BestIn;
			}
			else if (Out == INDEX_NONE)
			{
				Out = Edge;
			}
		}
		if (BestIn != INDEX_NONE && Out != INDEX_NONE)
		{
			Through.Add(Pair.Key, {BestIn, Out});
			continue;
		}
		TArray<int32, TInlineAllocator<3>> Sorted = Edges;
		Sorted.Sort([&Upstream](int32 A, int32 B) { return Upstream[A] != Upstream[B] ? Upstream[A] > Upstream[B] : A < B; });
		Through.Add(Pair.Key, {Sorted[0], Sorted[1]});
	}
	const auto OtherEnd = [&FlowEdges](int32 Edge, int32 Corner) { return FlowEdges[Edge].From == Corner ? FlowEdges[Edge].To : FlowEdges[Edge].From; };
	const auto GoOn = [&Through](int32 Edge, int32 Corner)
	{
		const TPair<int32, int32>* Pair = Through.Find(Corner);
		return !Pair ? INDEX_NONE : Pair->Key == Edge ? Pair->Value : Pair->Value == Edge ? Pair->Key : INDEX_NONE;
	};
	const auto Degree = [&CornerEdges](int32 Corner)
	{
		const TArray<int32, TInlineAllocator<3>>* Edges = CornerEdges.Find(Corner);
		return Edges ? Edges->Num() : 0;
	};

	// the lines, from the corners where they end, then the loops, each turned to run downstream where most of its sides flow
	struct FLine
	{
		TArray<int32> Corners;
		TArray<int32> Edges;
		bool bLoop = false;
	};
	TArray<FLine> Lines;
	TArray<bool> Used;
	Used.Init(false, FlowEdges.Num());
	const auto Walk = [&](int32 First, int32 Start)
	{
		FLine Line;
		Line.Corners.Add(Start);
		int32 Corner = Start;
		for (int32 Edge = First; Edge != INDEX_NONE && !Used[Edge];)
		{
			Used[Edge] = true;
			Line.Edges.Add(Edge);
			Corner = OtherEnd(Edge, Corner);
			Line.Corners.Add(Corner);
			Edge = GoOn(Edge, Corner);
		}
		Line.bLoop = Line.Corners.Num() > 2 && Line.Corners[0] == Line.Corners.Last() && Degree(Start) == 2;
		int32 Along = 0;
		for (int32 Index = 0; Index < Line.Edges.Num(); ++Index)
		{
			Along += FlowEdges[Line.Edges[Index]].From == Line.Corners[Index] ? 1 : -1;
		}
		if (Along < 0)
		{
			Algo::Reverse(Line.Corners);
			Algo::Reverse(Line.Edges);
		}
		Lines.Add(MoveTemp(Line));
	};
	for (int32 Edge = 0; Edge < FlowEdges.Num(); ++Edge)
	{
		for (const int32 End : {FlowEdges[Edge].From, FlowEdges[Edge].To})
		{
			if (!Used[Edge] && GoOn(Edge, End) == INDEX_NONE)
			{
				Walk(Edge, End);
			}
		}
	}
	for (int32 Edge = 0; Edge < FlowEdges.Num(); ++Edge)
	{
		if (!Used[Edge])
		{
			Walk(Edge, FlowEdges[Edge].From);
		}
	}

	const TArray<TArray<int32, TInlineAllocator<3>>> CornerCells = FindCornerCells(Grid);
	const auto IsSeaCorner = [&](int32 Corner) { return CornerCells[Corner].ContainsByPredicate([&](int32 Cell) { return Levels[Cell] < 0; }); };
	// how a line ends: at a spring, a corner of one river away from the sea, where it narrows; at a mouth, a corner of one river on the coast,
	// where it goes on into the sea; or where it joins the line of other rivers going on through that corner
	enum class EEnd : uint8 { Spring, Mouth, Join, None };
	const auto GetEnd = [&](const FLine& Line, bool bLast)
	{
		const int32 Corner = bLast ? Line.Corners.Last() : Line.Corners[0];
		return Line.bLoop ? EEnd::None : Degree(Corner) == 1 ? (IsSeaCorner(Corner) ? EEnd::Mouth : EEnd::Spring) : EEnd::Join;
	};

	// the points of one line: through the corner it starts at, the middles of its sides (each corner between them pulled RiverCornerCut of the
	// way onto the chord between them) and the corner it ends at, on into the sea at a mouth, straightened over RiverRelax passes, kept within
	// MaxOffset of its sides, rounded off and wound by a noise. Starts and Ends, when set, move its ends onto the lines it joins
	struct FBuilt
	{
		TArray<FVector3d> Points;
		TArray<double> Lengths;
		TArray<int8> FlowSigns;
		TArray<FIntPoint> PointCells;
	};
	const double Fade = FMath::Max(Params.MeanderFade * Params.HexRadius, 1.0);
	const auto BuildLine = [&](const FLine& Line, const FVector3d* StartAt, const FVector3d* EndAt)
	{
		const int32 NumEdges = Line.Edges.Num();
		const auto Middle = [&](int32 Index) { return (Corners[FlowEdges[Line.Edges[Index]].From] + Corners[FlowEdges[Line.Edges[Index]].To]).GetSafeNormal(); };
		TArray<FVector3d> Points;
		TArray<double> Sides;
		// the sides as a line, from which the points keep within MaxOffset
		TArray<FVector3d> Reference;
		for (const int32 Corner : Line.Corners)
		{
			Reference.Add(Corners[Corner]);
		}

		Points.Add(StartAt ? *StartAt : Corners[Line.Corners[0]]);
		Sides.Add(0.0);
		for (int32 Index = 0; Index < NumEdges; ++Index)
		{
			if (Index > 0)
			{
				const FVector3d Chord = ((Middle(Index - 1) + Middle(Index)) * 0.5).GetSafeNormal();
				Points.Add(FMath::Lerp(Corners[Line.Corners[Index]], Chord, FMath::Clamp(Params.CornerCut, 0.0, 1.0)).GetSafeNormal());
				Sides.Add(Index);
			}
			Points.Add(Middle(Index));
			Sides.Add(Index + 0.5);
		}
		Points.Add(EndAt ? *EndAt : Corners[Line.Corners.Last()]);
		Sides.Add(NumEdges);

		// on into the sea at a mouth, along its last side
		const auto Extend = [&](int32 End, int32 Before)
		{
			const FVector3d EndPoint = Corners[End];
			return (EndPoint + AlongSurface(EndPoint - Corners[Before], EndPoint) * (Params.MouthLength / Params.Radius)).GetSafeNormal();
		};
		if (GetEnd(Line, true) == EEnd::Mouth)
		{
			Points.Add(Extend(Line.Corners.Last(), Line.Corners.Last(1)));
			Sides.Add(NumEdges + 0.5);
			Reference.Add(Points.Last());
		}
		if (GetEnd(Line, false) == EEnd::Mouth)
		{
			Points.Insert(Extend(Line.Corners[0], Line.Corners[1]), 0);
			Sides.Insert(-0.5, 0);
			Reference.Insert(Points[0], 0);
		}

		// straighter over several sides, its ends kept
		for (int32 Pass = 0; Pass < Params.Relax && Points.Num() >= 3; ++Pass)
		{
			TArray<FVector3d> Relaxed = Points;
			for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
			{
				Relaxed[Index] = (Points[Index] * 0.5 + (Points[Index - 1] + Points[Index + 1]) * 0.25).GetSafeNormal();
			}
			Points = MoveTemp(Relaxed);
		}
		// and never further than MaxOffset from its sides, before it is rounded off, which keeps its shape, and after, which keeps the limit
		const auto Limit = [&]()
		{
			if (Params.MaxOffset <= 0.0)
			{
				return;
			}
			const double Most = Params.MaxOffset / Params.Radius;
			for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
			{
				FVector3d Nearest = Points[Index];
				double Best = TNumericLimits<double>::Max();
				for (int32 Segment = 0; Segment + 1 < Reference.Num(); ++Segment)
				{
					double T;
					const double Distance = GetSegmentDistance(Points[Index], Reference[Segment], Reference[Segment + 1], T);
					if (Distance < Best)
					{
						Best = Distance;
						Nearest = Reference[Segment] + (Reference[Segment + 1] - Reference[Segment]) * T;
					}
				}
				if (Best > Most)
				{
					Points[Index] = (Nearest + (Points[Index] - Nearest) * (Most / Best)).GetSafeNormal();
				}
			}
		};
		Limit();

		// rounded off, its ends kept, the place along its sides carried along
		for (int32 Pass = 0; Pass < Params.Smoothing && Points.Num() >= 3; ++Pass)
		{
			TArray<FVector3d> Rounded{Points[0]};
			TArray<double> RoundedSides{Sides[0]};
			for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
			{
				if (Index > 0)
				{
					Rounded.Add((Points[Index] * 0.75 + Points[Index + 1] * 0.25).GetSafeNormal());
					RoundedSides.Add(Sides[Index] * 0.75 + Sides[Index + 1] * 0.25);
				}
				if (Index + 2 < Points.Num())
				{
					Rounded.Add((Points[Index] * 0.25 + Points[Index + 1] * 0.75).GetSafeNormal());
					RoundedSides.Add(Sides[Index] * 0.25 + Sides[Index + 1] * 0.75);
				}
			}
			Rounded.Add(Points.Last());
			RoundedSides.Add(Sides.Last());
			Points = MoveTemp(Rounded);
			Sides = MoveTemp(RoundedSides);
		}
		Limit();

		FBuilt Built;
		Built.Lengths.Add(0.0);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Built.Lengths.Add(Built.Lengths.Last() + FVector3d::Distance(Points[Index], Points[Index - 1]) * Params.Radius);
		}
		const double Total = Built.Lengths.Last();

		// wound across itself by a noise of the place, fading in from its ends, so lines meet where they join
		Built.Points = Points;
		for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
		{
			const FVector3d Up = Points[Index];
			const FVector3d Tangent = AlongSurface(Points[Index + 1] - Points[Index - 1], Up);
			const FVector3d Left = FVector3d::CrossProduct(Up, Tangent);
			const double Noise = FMath::Clamp(double(FMath::PerlinNoise3D(FVector(Up * (Params.Radius / FMath::Max(Params.MeanderScale, 1.0))))) * 1.5, -1.0, 1.0);
			const double Amount = Params.Meander / Params.Radius * FMath::SmoothStep(0.0, Fade, Built.Lengths[Index]) * FMath::SmoothStep(0.0, Fade, Total - Built.Lengths[Index]);
			Built.Points[Index] = (Up + Left * (Amount * Noise)).GetSafeNormal();
		}

		// which way each segment flows, and the cells of the side it lies along
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const double Place = Index + 1 < Points.Num() ? (Sides[Index] + Sides[Index + 1]) * 0.5 : Sides[Index];
			const int32 Edge = FMath::Clamp(FMath::FloorToInt32(Place), 0, NumEdges - 1);
			Built.FlowSigns.Add(FlowEdges[Line.Edges[Edge]].From == Line.Corners[Edge] ? 1 : -1);
			Built.PointCells.Add(FIntPoint(FlowEdges[Line.Edges[Edge]].CellA, FlowEdges[Line.Edges[Edge]].CellB));
		}
		return Built;
	};

	TArray<FBuilt> Built;
	for (const FLine& Line : Lines)
	{
		Built.Add(BuildLine(Line, nullptr, nullptr));
	}

	// a line that ends where others go on through ends on their line, the point of it nearest that corner, so the waters join
	const auto FindJoin = [&](int32 Self, int32 Corner, FVector3d& OutPoint)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 Other = 0; Other < Lines.Num(); ++Other)
		{
			const TArray<int32>& OtherCorners = Lines[Other].Corners;
			if (Other == Self || !OtherCorners.Contains(Corner) || OtherCorners[0] == Corner || OtherCorners.Last() == Corner)
			{
				continue;
			}
			const TArray<FVector3d>& Points = Built[Other].Points;
			for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
			{
				double T;
				const double Distance = GetSegmentDistance(Corners[Corner], Points[Index], Points[Index + 1], T);
				if (Distance < Best)
				{
					Best = Distance;
					OutPoint = (Points[Index] + (Points[Index + 1] - Points[Index]) * T).GetSafeNormal();
				}
			}
		}
		return Best < TNumericLimits<double>::Max();
	};
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		FVector3d StartAt, EndAt;
		const bool bStart = GetEnd(Lines[Index], false) == EEnd::Join && FindJoin(Index, Lines[Index].Corners[0], StartAt);
		const bool bEnd = GetEnd(Lines[Index], true) == EEnd::Join && FindJoin(Index, Lines[Index].Corners.Last(), EndAt);
		if (bStart || bEnd)
		{
			Built[Index] = BuildLine(Lines[Index], bStart ? &StartAt : nullptr, bEnd ? &EndAt : nullptr);
		}
	}

	const double Reach = Params.Reach / Params.Radius;
	TArray<double> CellRadii;
	CellRadii.Init(-1.0, Cells.Num());
	const auto GetCellRadius = [&](int32 Cell)
	{
		if (CellRadii[Cell] < 0.0)
		{
			double Most = 0.0;
			for (const int32 Corner : Cells[Cell].Corners)
			{
				Most = FMath::Max(Most, FVector3d::Distance(Corners[Corner], Cells[Cell].Center));
			}
			CellRadii[Cell] = Most;
		}
		return CellRadii[Cell];
	};
	// the cells around one, two rings, among which anything of a river near it lies
	TArray<int32, TInlineAllocator<24>> Around;
	const auto GatherAround = [&](int32 Home)
	{
		Around.Reset();
		Around.Add(Home);
		for (int32 Ring = 0, RingStart = 0; Ring < 2; ++Ring)
		{
			const int32 Last = Around.Num();
			for (int32 Each = RingStart; Each < Last; ++Each)
			{
				for (const int32 Neighbor : Cells[Around[Each]].Neighbors)
				{
					Around.AddUnique(Neighbor);
				}
			}
			RingStart = Last;
		}
	};

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FLine& Line = Lines[LineIndex];
		const FBuilt& Each = Built[LineIndex];
		const EEnd StartEnd = GetEnd(Line, false);
		const EEnd LastEnd = GetEnd(Line, true);
		UE_LOG(LogHexHero, Verbose, TEXT("River line of %d sides from corner %d to %d%s%s"), Line.Edges.Num(), Line.Corners[0], Line.Corners.Last(),
			Line.bLoop ? TEXT(", a loop") : TEXT(""), StartEnd == EEnd::Spring || LastEnd == EEnd::Spring ? TEXT(", a spring") : TEXT(""));

		// a mouth, a corner on the coast, with the way the river goes on into the sea and the two cells of its last side
		const auto AddMouth = [&](int32 End, int32 Before, int32 Edge)
		{
			const FVector3d EndPoint = Corners[End];
			Curve.Mouths.Add({EndPoint, AlongSurface(EndPoint - Corners[Before], EndPoint), FlowEdges[Edge].CellA, FlowEdges[Edge].CellB});
		};
		if (LastEnd == EEnd::Mouth)
		{
			AddMouth(Line.Corners.Last(), Line.Corners.Last(1), Line.Edges.Last());
		}
		if (StartEnd == EEnd::Mouth)
		{
			AddMouth(Line.Corners[0], Line.Corners[1], Line.Edges[0]);
		}

		const double Total = Each.Lengths.Last();
		const double Taper = FMath::Max(Params.SourceTaper * Params.HexRadius, 1.0);
		const int32 First = Curve.Points.Num();
		for (int32 Index = 0; Index < Each.Points.Num(); ++Index)
		{
			Curve.Points.Add(Each.Points[Index]);
			// narrower towards a spring at either end
			double Grow = 1.0;
			Grow = StartEnd == EEnd::Spring ? FMath::Min(Grow, FMath::SmoothStep(0.0, 1.0, Each.Lengths[Index] / Taper)) : Grow;
			Grow = LastEnd == EEnd::Spring ? FMath::Min(Grow, FMath::SmoothStep(0.0, 1.0, (Total - Each.Lengths[Index]) / Taper)) : Grow;
			Curve.Narrowing.Add(float(Params.RiverWidth * 0.5 * (1.0 - FMath::Lerp(0.15, 1.0, Grow))));
			Curve.Lengths.Add(float(Each.Lengths[Index]));
			Curve.FlowSigns.Add(Each.FlowSigns[Index]);
			Curve.PointCells.Add(Each.PointCells[Index]);
			Curve.Links.Add(Index + 1 < Each.Points.Num());
		}

		// each segment goes to the cells it may reach, among two rings around the cell of its middle
		int32 Hint = FlowEdges[Line.Edges[0]].CellA;
		for (int32 Index = First; Index + 1 < Curve.Points.Num(); ++Index)
		{
			const FVector3d A = Curve.Points[Index];
			const FVector3d B = Curve.Points[Index + 1];
			const int32 Home = Grid.FindCell((A + B).GetSafeNormal(), Hint);
			if (Home == INDEX_NONE)
			{
				continue;
			}
			Hint = Home;
			GatherAround(Home);
			for (const int32 Cell : Around)
			{
				double T;
				if (GetSegmentDistance(Cells[Cell].Center, A, B, T) <= GetCellRadius(Cell) + Reach)
				{
					Curve.CellSegments[Cell].Add(Index);
				}
			}
		}
	}

	return Result;
}

bool HexPlanetRivers::FindNearest(const FCurve& Curve, int32 Cell, const FVector3d& Direction, FCurveHit& Out)
{
	Out = FCurveHit();
	if (!Curve.CellSegments.IsValidIndex(Cell))
	{
		return false;
	}
	for (const int32 Index : Curve.CellSegments[Cell])
	{
		const FVector3d& A = Curve.Points[Index];
		const FVector3d& B = Curve.Points[Index + 1];
		// T first, as the order of the operands of a sum is not fixed
		double T;
		const double Raw = GetSegmentDistance(Direction, A, B, T) * Curve.Radius;
		const double Distance = Raw + FMath::Lerp(double(Curve.Narrowing[Index]), double(Curve.Narrowing[Index + 1]), T);
		if (Distance < Out.Distance)
		{
			Out.Distance = Distance;
			Out.Point = A + (B - A) * T;
			Out.Segment = Index;
			Out.Along = FMath::Lerp(double(Curve.Lengths[Index]), double(Curve.Lengths[Index + 1]), T);
			// the side is by the way the line runs, the same all along it, and the flow may run the other way on part of it
			Out.LineDirection = (B - A).GetSafeNormal();
			Out.Flow = Out.LineDirection * double(Curve.FlowSigns[Index]);
			Out.bLeft = FVector3d::DotProduct(Direction - Out.Point, FVector3d::CrossProduct(Out.Point, Out.LineDirection)) > 0.0;
		}
	}
	return Out.Distance < TNumericLimits<double>::Max();
}

void HexPlanetRivers::Generate(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, const FGenerateParams& Params, TSet<uint64>& OutEdges, TSet<uint64>& OutBridges)
{
	OutEdges.Reset();
	OutBridges.Reset();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (Levels.Num() != Cells.Num() || Params.Density <= 0.0f)
	{
		return;
	}

	// rings of cells from the sea
	TArray<int32> SeaDistance;
	SeaDistance.Init(MAX_int32, Cells.Num());
	TArray<int32> Queue;
	int32 LandCells = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Levels[Cell] < 0)
		{
			SeaDistance[Cell] = 0;
			Queue.Add(Cell);
		}
		LandCells += IsLevelAllowed(Levels[Cell]) ? 1 : 0;
	}
	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		for (const int32 Neighbor : Cells[Queue[Index]].Neighbors)
		{
			if (SeaDistance[Neighbor] == MAX_int32)
			{
				SeaDistance[Neighbor] = SeaDistance[Queue[Index]] + 1;
				Queue.Add(Neighbor);
			}
		}
	}

	// the nearest cell of the sea of each cell, carried out along the rings and then checked against the neighbors, by which every corner has a
	// distance to the sea that changes smoothly, so a river can run straight towards the coast instead of wandering over rings of equal distance
	TArray<int32> NearestSea;
	NearestSea.Init(INDEX_NONE, Cells.Num());
	for (const int32 Cell : Queue)
	{
		if (NearestSea[Cell] == INDEX_NONE)
		{
			NearestSea[Cell] = Cell;
		}
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (NearestSea[Neighbor] == INDEX_NONE)
			{
				NearestSea[Neighbor] = NearestSea[Cell];
			}
		}
	}
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				const int32 Sea = NearestSea[Neighbor];
				if (Sea != INDEX_NONE && (NearestSea[Cell] == INDEX_NONE
					|| FVector3d::DistSquared(Cells[Cell].Center, Cells[Sea].Center) < FVector3d::DistSquared(Cells[Cell].Center, Cells[NearestSea[Cell]].Center)))
				{
					NearestSea[Cell] = Sea;
				}
			}
		}
	}

	const TArray<FVector3d>& CornerPositions = Grid.GetCorners();
	const TArray<TArray<int32, TInlineAllocator<3>>> CornerCells = FindCornerCells(Grid);
	TArray<double> CornerSea;
	CornerSea.Init(10.0, CornerCells.Num());
	for (int32 Corner = 0; Corner < CornerCells.Num(); ++Corner)
	{
		for (const int32 Cell : CornerCells[Corner])
		{
			for (int32 Index = -1; Index < Cells[Cell].Neighbors.Num(); ++Index)
			{
				const int32 Sea = NearestSea[Index < 0 ? Cell : Cells[Cell].Neighbors[Index]];
				if (Sea != INDEX_NONE)
				{
					CornerSea[Corner] = FMath::Min(CornerSea[Corner], FVector3d::Distance(CornerPositions[Corner], Cells[Sea].Center));
				}
			}
		}
	}
	const auto CornerHeight = [&](int32 Corner)
	{
		int32 Sum = 0;
		for (const int32 Cell : CornerCells[Corner])
		{
			Sum += Levels[Cell];
		}
		return Sum;
	};
	const auto IsMouth = [&](int32 Corner) { return CornerCells[Corner].ContainsByPredicate([&](int32 Cell) { return Levels[Cell] < 0; }); };

	// the sides at a corner a river may take, with the corner at their other end
	const auto GetLinks = [&](int32 Corner, TArray<TPair<int32, uint64>, TInlineAllocator<3>>& OutLinks)
	{
		OutLinks.Reset();
		const TArray<int32, TInlineAllocator<3>>& Around = CornerCells[Corner];
		for (int32 First = 0; First < Around.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < Around.Num(); ++Second)
			{
				const int32 A = Around[First];
				const int32 B = Around[Second];
				// between two cells of one level, as the side between plains and hills lies halfway down the slope between them
				if (!IsLevelAllowed(Levels[A]) || Levels[A] != Levels[B])
				{
					continue;
				}
				const int32 Other = GetOtherCorner(Grid, A, B, Corner);
				if (Other != INDEX_NONE)
				{
					OutLinks.Add({Other, GetEdgeKey(A, B)});
				}
			}
		}
	};

	// sources at the foot of mountains first, then of hills, each group in an order of its own by the seed
	FRandomStream Random(Params.Seed * 7919 + 13);
	TArray<TPair<double, int32>> Sources;
	TArray<TPair<int32, uint64>, TInlineAllocator<3>> Links;
	for (int32 Corner = 0; Corner < CornerCells.Num(); ++Corner)
	{
		const TArray<int32, TInlineAllocator<3>>& Around = CornerCells[Corner];
		bool bMountain = false;
		bool bHill = false;
		int32 Nearest = MAX_int32;
		for (const int32 Cell : Around)
		{
			bMountain |= Levels[Cell] == HexPlanetSurface::MountainLevel;
			bHill |= Levels[Cell] == 1;
			Nearest = FMath::Min(Nearest, SeaDistance[Cell]);
		}
		if ((!bMountain && !bHill) || Nearest < Params.SourceSeaDistance || IsMouth(Corner))
		{
			continue;
		}
		GetLinks(Corner, Links);
		if (!Links.IsEmpty())
		{
			Sources.Add({(bMountain ? 0.0 : 1.0) + Random.FRand(), Corner});
		}
	}
	Sources.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });

	const int32 Wanted = FMath::RoundToInt32(LandCells * Params.Density / 1000.0f);
	// the river of each corner a river runs through, and the corners next to them
	TMap<int32, int32> Taken;
	int32 NumRivers = 0;

	for (const TPair<double, int32>& Source : Sources)
	{
		if (NumRivers >= Wanted)
		{
			break;
		}
		const int32 Start = Source.Value;
		if (Taken.Contains(Start))
		{
			continue;
		}
		// not right next to another river, which would join it at once
		GetLinks(Start, Links);
		if (Links.ContainsByPredicate([&](const TPair<int32, uint64>& Link) { return Taken.Contains(Link.Key); }))
		{
			continue;
		}

		TArray<int32> Path{Start};
		TArray<uint64> Sides;
		int32 Flat = 0;
		// which way the river turned at the corner before, 1 to the left and -1 to the right: at a corner it can only turn one way or the other,
		// so a straight river turns by turns, and two turns the same way in a row take it off to the side
		int32 LastTurn = 0;
		bool bEnded = false;
		int32 Corner = Start;

		while (Sides.Num() < Params.MaxLength)
		{
			GetLinks(Corner, Links);
			int32 Best = INDEX_NONE;
			double BestScore = TNumericLimits<double>::Max();
			bool bBestFlat = false;
			int32 BestTurn = 0;
			for (int32 Index = 0; Index < Links.Num(); ++Index)
			{
				const int32 Next = Links[Index].Key;
				if (Path.Contains(Next) || CornerHeight(Next) > CornerHeight(Corner))
				{
					continue;
				}
				// how much nearer the sea the side takes it, as a share of its length: 1 straight towards it, 0 along the coast
				const FVector3d Step = CornerPositions[Next] - CornerPositions[Corner];
				const double Nearer = (CornerSea[Corner] - CornerSea[Next]) / FMath::Max(Step.Size(), 1.0e-9);
				const bool bFlat = Nearer < 0.25;
				if (Nearer < -0.5 || (bFlat && Flat >= Params.FlatSteps))
				{
					continue;
				}
				int32 Turn = 0;
				if (Path.Num() >= 2)
				{
					const FVector3d In = CornerPositions[Corner] - CornerPositions[Path.Last(1)];
					Turn = FVector3d::DotProduct(FVector3d::CrossProduct(In, Step), CornerPositions[Corner]) > 0.0 ? 1 : -1;
				}
				// the coast and another river end it at once; else the side that takes it nearest the sea, turning the other way than at the
				// corner before, with a little chance of another
				double Score = IsMouth(Next) || Taken.Contains(Next) ? -10.0
					: -Nearer + (Turn != 0 && Turn == LastTurn ? Params.TurnPenalty : 0.0) + Random.FRand() * Params.Wander;
				// a corner next to another river must join it, so rivers do not run side by side
				if (!Taken.Contains(Next) && !IsMouth(Next))
				{
					TArray<TPair<int32, uint64>, TInlineAllocator<3>> NextLinks;
					GetLinks(Next, NextLinks);
					if (NextLinks.ContainsByPredicate([&](const TPair<int32, uint64>& Link) { return Taken.Contains(Link.Key) && !Path.Contains(Link.Key); }))
					{
						Score -= 5.0;
					}
				}
				if (Score < BestScore)
				{
					BestScore = Score;
					Best = Index;
					bBestFlat = bFlat;
					BestTurn = Turn;
				}
			}
			if (Best == INDEX_NONE)
			{
				break;
			}
			Flat = bBestFlat ? Flat + 1 : 0;
			LastTurn = BestTurn;
			Corner = Links[Best].Key;
			Sides.Add(Links[Best].Value);
			Path.Add(Corner);
			if (IsMouth(Corner) || Taken.Contains(Corner))
			{
				bEnded = true;
				break;
			}
		}

		if (!bEnded || Sides.Num() < Params.MinLength)
		{
			continue;
		}

		++NumRivers;
		for (const int32 Each : Path)
		{
			Taken.FindOrAdd(Each, NumRivers);
		}
		for (const uint64 Side : Sides)
		{
			OutEdges.Add(Side);
		}

		// bridges about every BridgeSpacing sides, never at the ends
		if (Params.BridgeSpacing > 0)
		{
			for (int32 Index = 2 + Random.RandRange(0, Params.BridgeSpacing / 2); Index < Sides.Num() - 2;
				Index += FMath::Max(2, Params.BridgeSpacing + Random.RandRange(-Params.BridgeSpacing / 3, Params.BridgeSpacing / 3)))
			{
				OutBridges.Add(Sides[Index]);
			}
		}
	}
}

void UHexPlanetRivers::Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings)
{
	Planet = InPlanet;
	Settings = InSettings ? InSettings : GetDefault<UHexPlanetConfig>();
	Edges.Reset();
	bGenerated = false;

	const FHexSphereGrid& Grid = Planet->GetGrid();
	// from the data folder of the layer of the planet; a layer without rivers, the underground, reads and generates none
	const FString Folder = Planet->GetDataFolder();
	FString Error;
	TArray<FHexRiverEdge> Lines;

	if (Settings->bRivers && !Folder.IsEmpty() && HexPlanetFile::HasRivers(Folder))
	{
		if (HexPlanetFile::LoadRivers(Folder, Lines, Error))
		{
			for (const FHexRiverEdge& Line : Lines)
			{
				if (Grid.GetCells().IsValidIndex(Line.Cell) && Grid.GetCells()[Line.Cell].Neighbors.Contains(Line.Neighbor))
				{
					Edges.Add(HexPlanetRivers::GetEdgeKey(Line.Cell, Line.Neighbor), Line.Bridge ? 2 : 1);
				}
			}
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s, the planet has no rivers"), *Error);
		}
	}
	else if (Settings->bRivers)
	{
		HexPlanetRivers::FGenerateParams Params;
		Params.Seed = Planet->GetManifest().Seed;
		Params.Density = Settings->RiverDensity;
		Params.MinLength = Settings->RiverMinLength;
		Params.MaxLength = Settings->RiverMaxLength;
		Params.FlatSteps = Settings->RiverFlatSteps;
		Params.TurnPenalty = Settings->RiverStraightness;
		Params.Wander = Settings->RiverWander;
		Params.SourceSeaDistance = Settings->RiverSourceSeaDistance;
		Params.BridgeSpacing = Settings->BridgeSpacing;
		TSet<uint64> Generated;
		TSet<uint64> GeneratedBridges;
		const double Start = FPlatformTime::Seconds();
		HexPlanetRivers::Generate(Grid, Planet->GetLevels(), Params, Generated, GeneratedBridges);
		for (const uint64 Key : Generated)
		{
			Edges.Add(Key, GeneratedBridges.Contains(Key) ? 2 : 1);
		}
		bGenerated = true;
		UE_LOG(LogHexHero, Log, TEXT("Planet: generated %d sides of rivers with %d bridges in %.1f ms"), Generated.Num(), GeneratedBridges.Num(),
			(FPlatformTime::Seconds() - Start) * 1000.0);
	}

	ShownNet = BuildNet(Planet->GetLevels());
	UpdateTexture();

	Bridges = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, TEXT("Bridges"));
	Bridges->SetupAttachment(this);
	Bridges->NumCustomDataFloats = 1;
	Bridges->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bridges->SetCanEverAffectNavigation(false);
	Bridges->bAffectDistanceFieldLighting = false;
	if (UStaticMesh* Mesh = Settings->BridgeMesh.LoadSynchronous())
	{
		Bridges->SetStaticMesh(Mesh);
		for (int32 Element = 0; Element < Mesh->GetStaticMaterials().Num(); ++Element)
		{
			if (UMaterialInterface* Parent = Mesh->GetMaterial(Element))
			{
				UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
				HexMaterialConfig::Apply(Material, Settings);
				Bridges->SetMaterial(Element, Material);
			}
		}
	}
	else if (!Settings->BridgeMesh.IsNull())
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: the bridge mesh %s is missing"), *Settings->BridgeMesh.ToString());
	}
	if (Settings->BridgeCullDistance > 0.0f)
	{
		Bridges->SetCullDistances(0, int32(Settings->BridgeCullDistance));
	}
	Bridges->RegisterComponent();
}

TSet<uint64> UHexPlanetRivers::GetRiverKeys() const
{
	TSet<uint64> Keys;
	Keys.Reserve(Edges.Num());
	for (const TPair<uint64, uint8>& Each : Edges)
	{
		Keys.Add(Each.Key);
	}
	return Keys;
}

bool UHexPlanetRivers::Save(const FString& Folder, FString& OutError) const
{
	TArray<FHexRiverEdge> Lines;
	for (const TPair<uint64, uint8>& Each : Edges)
	{
		int32 A, B;
		HexPlanetRivers::GetEdgeCells(Each.Key, A, B);
		if (!IsEdgeAllowed(A, B))
		{
			continue;
		}
		FHexRiverEdge& Line = Lines.AddDefaulted_GetRef();
		Line.Cell = A;
		Line.Neighbor = B;
		Line.Bridge = Each.Value >= 2;
	}
	return HexPlanetFile::SaveRivers(Folder, Lines, OutError);
}

uint8 UHexPlanetRivers::GetEdge(uint64 Key) const
{
	const uint8* Found = Edges.Find(Key);
	return Found ? *Found : 0;
}

void UHexPlanetRivers::SetEdges(const TMap<uint64, uint8>& Changes, bool bByStep)
{
	bool bChanged = false;
	for (const TPair<uint64, uint8>& Each : Changes)
	{
		if (Each.Value == 0)
		{
			bChanged |= Edges.Remove(Each.Key) > 0;
		}
		else if (GetEdge(Each.Key) != Each.Value)
		{
			Edges.Add(Each.Key, FMath::Min<uint8>(Each.Value, 2));
			bChanged = true;
		}
	}
	if (!bChanged)
	{
		return;
	}
	// the water and the valley show once the planet has built them, as both follow the line of the rivers it builds with them
	UpdateBridges();
	Planet->OnRiversChanged(bByStep);
}

bool UHexPlanetRivers::IsEdgeAllowed(int32 A, int32 B) const
{
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	return Cells.IsValidIndex(A) && Cells.IsValidIndex(B) && Cells[A].Neighbors.Contains(B) && HexPlanetRivers::IsLevelAllowed(Planet->GetCellLevel(A))
		&& HexPlanetRivers::IsLevelAllowed(Planet->GetCellLevel(B));
}

bool UHexPlanetRivers::IsRiver(int32 A, int32 B) const
{
	return GetEdge(HexPlanetRivers::GetEdgeKey(A, B)) > 0 && IsEdgeAllowed(A, B);
}

bool UHexPlanetRivers::HasBridge(int32 A, int32 B) const
{
	return GetEdge(HexPlanetRivers::GetEdgeKey(A, B)) >= 2 && IsEdgeAllowed(A, B);
}

float UHexPlanetRivers::GetCrossingFactor(int32 From, int32 To) const
{
	return IsRiver(From, To) && !HasBridge(From, To) ? Settings->RiverCrossCost : 1.0f;
}

TArray<uint16> UHexPlanetRivers::BuildNet(TConstArrayView<int8> Levels) const
{
	return HexPlanetRivers::BuildNet(Planet->GetGrid(), Levels, GetRiverKeys());
}

void UHexPlanetRivers::SetShownNet(TArray<uint16> Net, TSharedPtr<const HexPlanetRivers::FCurve> Curve)
{
	ShownNet = MoveTemp(Net);
	ShownCurve = MoveTemp(Curve);
	UpdateTexture();
	UpdateBridges();
}

HexPlanetRivers::FCurveParams UHexPlanetRivers::GetCurveParams() const
{
	HexPlanetRivers::FCurveParams Params;
	Params.Radius = Planet->GetRadius();
	Params.HexRadius = Settings->HexRadius;
	Params.RiverWidth = Settings->RiverWidth;
	Params.Meander = Settings->RiverMeander;
	Params.MeanderScale = Settings->RiverMeanderScale;
	Params.MeanderFade = Settings->RiverMeanderFade;
	Params.SourceTaper = Settings->RiverSourceTaper;
	Params.MouthLength = Settings->RiverMouthLength;
	Params.Smoothing = Settings->RiverSmoothing;
	Params.CornerCut = ShapeOverride.IsSet() ? ShapeOverride->X : Settings->RiverCornerCut;
	Params.Relax = ShapeOverride.IsSet() ? FMath::RoundToInt32(ShapeOverride->Y) : Settings->RiverRelax;
	Params.MaxOffset = ShapeOverride.IsSet() ? ShapeOverride->Z : Settings->RiverMaxOffset;
	Params.Reach = HexPlanetRivers::GetReach(*Settings);
	return Params;
}

double UHexPlanetRivers::GetRiverDistance(int32 Cell, const FVector3d& Position) const
{
	HexPlanetRivers::FCurveHit Hit;
	return ShownCurve && HexPlanetRivers::FindNearest(*ShownCurve, Cell, Position.GetSafeNormal(), Hit) ? Hit.Distance : TNumericLimits<double>::Max();
}

FString UHexPlanetRivers::GetCellText(int32 Cell) const
{
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	if (!Cells.IsValidIndex(Cell))
	{
		return FString();
	}
	int32 NumRivers = 0;
	int32 NumBridges = 0;
	for (const int32 Neighbor : Cells[Cell].Neighbors)
	{
		NumRivers += IsRiver(Cell, Neighbor) ? 1 : 0;
		NumBridges += HasBridge(Cell, Neighbor) ? 1 : 0;
	}
	if (NumRivers == 0)
	{
		return FString();
	}
	return FString::Printf(TEXT("River on %d side%s, %d bridge%s; crossing without one costs x%.1f"), NumRivers, NumRivers == 1 ? TEXT("") : TEXT("s"),
		NumBridges, NumBridges == 1 ? TEXT("") : TEXT("s"), Settings->RiverCrossCost);
}

void UHexPlanetRivers::UpdateTexture()
{
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const TArray<FVector3d>& GridCorners = Grid.GetCorners();
	if (Cells.IsEmpty())
	{
		return;
	}
	const int32 Rows = FMath::DivideAndRoundUp(Cells.Num(), HexPlanetSurface::MarkColumns);
	const int32 Columns = HexPlanetSurface::MarkColumns * HexPlanetRivers::MaxSectors;
	Pixels.Init(FColor(0, 0, 0, 0), Columns * Rows);
	SegmentTexels.Reset();

	if (ShownCurve && !ShownCurve->Points.IsEmpty())
	{
		const HexPlanetRivers::FCurve& Curve = *ShownCurve;
		const double Radius = FMath::Max(Planet->GetRadius(), 1.0);
		// how far from its line anything of a river shows in M_HexPlanet: its water, its banks and the gap it keeps from the run-up of the sea,
		// with the noise of its edge and how far the middle of a piece may move off its cell (CenterShift of the kit), on the unit sphere
		const double Reach = (Settings->RiverWidth * 0.5 + Settings->RiverBankWidth + FMath::Max(Settings->RiverLeveeWidth, Settings->RiverRunupGap)
			+ Settings->RiverEdgeNoise + 25.0) / Radius;

		// the segments of the line between every other point, which keeps as near it
		struct FShaderSegment
		{
			int32 A;
			int32 B;
		};
		TArray<FShaderSegment> Segments;
		for (int32 Start = 0; Start < Curve.Points.Num();)
		{
			int32 End = Start;
			while (End + 1 < Curve.Points.Num() && Curve.Links[End])
			{
				++End;
			}
			for (int32 Index = Start; Index < End;)
			{
				const int32 Next = FMath::Min(Index + 2, End);
				Segments.Add({Index, Next});
				Index = Next;
			}
			Start = End + 1;
		}

		// each segment goes to the sectors of the cells around it that it may reach: sector K of a cell is the triangle from its center to its
		// corners K - 1 and K, which the fourth UV of the surface gives by its angle
		TMap<int32, TArray<int32>> Lists;
		TArray<int32, TInlineAllocator<24>> Around;
		int32 Hint = INDEX_NONE;
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const FVector3d& A = Curve.Points[Segments[Index].A];
			const FVector3d& B = Curve.Points[Segments[Index].B];
			const int32 Home = Grid.FindCell((A + B).GetSafeNormal(), Hint);
			if (Home == INDEX_NONE)
			{
				continue;
			}
			Hint = Home;
			Around.Reset();
			Around.Add(Home);
			for (int32 Ring = 0, RingStart = 0; Ring < 2; ++Ring)
			{
				const int32 Last = Around.Num();
				for (int32 Each = RingStart; Each < Last; ++Each)
				{
					for (const int32 Neighbor : Cells[Around[Each]].Neighbors)
					{
						Around.AddUnique(Neighbor);
					}
				}
				RingStart = Last;
			}
			for (const int32 Cell : Around)
			{
				const FHexSphereCell& Each = Cells[Cell];
				const int32 NumSides = Each.Corners.Num();
				for (int32 Sector = 0; Sector < NumSides && Sector < HexPlanetRivers::MaxSectors; ++Sector)
				{
					const FVector3d& From = GridCorners[Each.Corners[(Sector + NumSides - 1) % NumSides]];
					const FVector3d& To = GridCorners[Each.Corners[Sector]];
					const FVector3d Middle = (Each.Center + From + To) / 3.0;
					const double Size = FMath::Max3(FVector3d::Distance(Middle, Each.Center), FVector3d::Distance(Middle, From), FVector3d::Distance(Middle, To));
					double T;
					if (GetSegmentDistance(Middle, A, B, T) <= Size + Reach)
					{
						Lists.FindOrAdd(Cell * HexPlanetRivers::MaxSectors + Sector).Add(Index);
					}
				}
			}
		}

		// two texels a segment of a list: its ends as directions from the center, with the narrowing at each, and in the first whether it is
		// frozen (16) and flows from its first end to its second (32)
		Lists.KeySort(TLess<int32>());
		int32 Dropped = 0;
		for (const TPair<int32, TArray<int32>>& Pair : Lists)
		{
			const int32 Cell = Pair.Key / HexPlanetRivers::MaxSectors;
			const int32 Sector = Pair.Key % HexPlanetRivers::MaxSectors;
			const int32 First = SegmentTexels.Num() / 2;
			const int32 Count = FMath::Min(Pair.Value.Num(), 255);
			Dropped += Pair.Value.Num() - Count;
			if (First + Count >= (1 << 24))
			{
				break;
			}
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FShaderSegment& Segment = Segments[Pair.Value[Index]];
				const FIntPoint SideCells = Curve.PointCells[Segment.A];
				const bool bFrozen = Planet->IsRiverFrozenAt(SideCells.X) || Planet->IsRiverFrozenAt(SideCells.Y);
				const bool bForward = Curve.FlowSigns[Segment.A] > 0;
				const FVector3d& A = Curve.Points[Segment.A];
				const FVector3d& B = Curve.Points[Segment.B];
				SegmentTexels.Add(FVector4f(float(A.X), float(A.Y), float(A.Z), Curve.Narrowing[Segment.A] + (bFrozen ? 16.0f : 0.0f) + (bForward ? 32.0f : 0.0f)));
				SegmentTexels.Add(FVector4f(float(B.X), float(B.Y), float(B.Z), Curve.Narrowing[Segment.B]));
			}
			const int32 Pixel = (Cell / HexPlanetSurface::MarkColumns) * Columns + (Cell % HexPlanetSurface::MarkColumns) * HexPlanetRivers::MaxSectors + Sector;
			Pixels[Pixel] = FColor(uint8(First & 255), uint8(First >> 8 & 255), uint8(First >> 16 & 255), uint8(Count));
		}
		if (Dropped > 0)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %d segments of rivers left out of sectors that reach more than 255"), Dropped);
		}
	}

	bool bNewTextures = false;
	if (!Texture)
	{
		// FColor is B, G, R, A in memory, and PF_B8G8R8A8 reads it so, so the material sees R, G, B and A as written
		Texture = UTexture2D::CreateTransient(Columns, Rows, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("PlanetCellRivers")));
		Texture->SRGB = false;
		Texture->Filter = TF_Nearest;
		Texture->NeverStream = true;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		bNewTextures = true;
	}
	else
	{
		uint8* Data = static_cast<uint8*>(FMemory::Malloc(Pixels.Num() * sizeof(FColor)));
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Columns, Rows);
		Texture->UpdateTextureRegions(0, 1, Region, Columns * sizeof(FColor), sizeof(FColor), Data,
			[](uint8* InData, const FUpdateTextureRegion2D* InRegion)
			{
				FMemory::Free(InData);
				delete InRegion;
			});
	}

	// the segments, made again only when they no longer fit
	const int32 SegmentRowsNeeded = FMath::Max(1, FMath::DivideAndRoundUp(SegmentTexels.Num(), HexPlanetRivers::SegmentColumns));
	SegmentTexels.SetNumZeroed(SegmentRowsNeeded * HexPlanetRivers::SegmentColumns);
	if (!SegmentTexture || SegmentRowsNeeded > SegmentRows)
	{
		SegmentRows = FMath::RoundUpToPowerOfTwo(SegmentRowsNeeded);
		TArray<FVector4f> Initial = SegmentTexels;
		Initial.SetNumZeroed(SegmentRows * HexPlanetRivers::SegmentColumns);
		SegmentTexture = UTexture2D::CreateTransient(HexPlanetRivers::SegmentColumns, SegmentRows, PF_A32B32G32R32F,
			MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("PlanetRiverSegments")));
		SegmentTexture->SRGB = false;
		SegmentTexture->Filter = TF_Nearest;
		SegmentTexture->NeverStream = true;
		FTexture2DMipMap& Mip = SegmentTexture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Initial.GetData(), Initial.Num() * Initial.GetTypeSize());
		Mip.BulkData.Unlock();
		SegmentTexture->UpdateResource();
		bNewTextures = true;
	}
	else
	{
		uint8* Data = static_cast<uint8*>(FMemory::Malloc(SegmentTexels.Num() * sizeof(FVector4f)));
		FMemory::Memcpy(Data, SegmentTexels.GetData(), SegmentTexels.Num() * sizeof(FVector4f));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, HexPlanetRivers::SegmentColumns, SegmentRowsNeeded);
		SegmentTexture->UpdateTextureRegions(0, 1, Region, HexPlanetRivers::SegmentColumns * sizeof(FVector4f), sizeof(FVector4f), Data,
			[](uint8* InData, const FUpdateTextureRegion2D* InRegion)
			{
				FMemory::Free(InData);
				delete InRegion;
			});
	}
	if (bNewTextures)
	{
		Planet->ApplyRiverTextures();
	}
}

void UHexPlanetRivers::UpdateBridges()
{
	if (!Bridges || !Bridges->GetStaticMesh())
	{
		return;
	}
	Bridges->ClearInstances();

	const FHexSphereGrid& Grid = Planet->GetGrid();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const FBox MeshBounds = Bridges->GetStaticMesh()->GetBoundingBox();
	const double MeshLength = FMath::Max(MeshBounds.GetSize().X, 1.0);
	const double Scale = Settings->BridgeLength / MeshLength;

	// the sides with a bridge, placed in parallel, as the planet may build the ground of their cells again for it (AHexPlanet::GetCellTriangles)
	TArray<TPair<int32, int32>> BridgeSides;
	for (const TPair<uint64, uint8>& Each : Edges)
	{
		int32 A, B;
		HexPlanetRivers::GetEdgeCells(Each.Key, A, B);
		if (Each.Value >= 2 && IsEdgeAllowed(A, B))
		{
			BridgeSides.Emplace(A, B);
		}
	}
	TArray<FTransform> Transforms;
	Transforms.SetNum(BridgeSides.Num());

	ParallelFor(BridgeSides.Num(), [&](int32 Index)
	{
		const int32 A = BridgeSides[Index].Key;
		const int32 B = BridgeSides[Index].Value;
		TArray<FVector3f> Triangles;
		const int32 Side = Cells[A].Neighbors.IndexOfByKey(B);
		int32 From, To;
		GetSideCorners(Cells[A], Side, From, To);
		const FVector3d Middle = (Grid.GetCorners()[From] + Grid.GetCorners()[To]).GetSafeNormal();

		// where the line of the river passes nearest the middle of the side, across it
		FVector3d Up = Middle;
		FVector3d Flow = AlongSurface(Grid.GetCorners()[To] - Grid.GetCorners()[From], Middle);
		HexPlanetRivers::FCurveHit Hit;
		if (ShownCurve && HexPlanetRivers::FindNearest(*ShownCurve, A, Middle, Hit))
		{
			Up = Hit.Point.GetSafeNormal();
			Flow = AlongSurface(Hit.Flow, Up);
		}

		// on the ground there, which lies on the bottom of the valley
		FVector3d Ground = Up * Planet->GetRadius();
		const int32 Home = Grid.FindCell(Up, A);
		Planet->GetCellTriangles(Home != INDEX_NONE ? Home : A, Triangles);
		if (!FindGround(Triangles, Up, Ground))
		{
			Planet->GetCellTriangles(B, Triangles);
			FindGround(Triangles, Up, Ground);
		}

		// X across the river, from one cell to the other, and Z up
		FVector3d Across = FVector3d::CrossProduct(Flow, Up);
		Across *= FVector3d::DotProduct(Across, Cells[B].Center - Cells[A].Center) < 0.0 ? -1.0 : 1.0;
		const FVector3d Along = FVector3d::CrossProduct(Up, Across);
		const FMatrix Axes(FVector(Across), FVector(Along), FVector(Up), FVector::ZeroVector);
		Transforms[Index] = FTransform(Axes.ToQuat(), FVector(Ground - Up * Settings->BridgeSink), FVector(Scale));
	});

	Bridges->AddInstances(Transforms, false, false, false);
	for (int32 Instance = 0; Instance < Transforms.Num(); ++Instance)
	{
		Bridges->SetCustomDataValue(Instance, 0, float(Settings->BridgeRockLayer), false);
	}
	Bridges->MarkRenderStateDirty();
}
