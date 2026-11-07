// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetSeaField.h"
#include "Async/ParallelFor.h"
#include "HexSphereGrid.h"

FVector3d HexPlanetSeaField::GetTexelDirection(int32 Face, int32 X, int32 Y, int32 Size)
{
	// the faces of a cubemap as the GPU addresses them: +X, -X, +Y, -Y, +Z, -Z, with V going down each face
	const double U = 2.0 * (X + 0.5) / Size - 1.0;
	const double V = 2.0 * (Y + 0.5) / Size - 1.0;

	switch (Face)
	{
	case 0: return FVector3d(1.0, -V, -U).GetSafeNormal();
	case 1: return FVector3d(-1.0, -V, U).GetSafeNormal();
	case 2: return FVector3d(U, 1.0, V).GetSafeNormal();
	case 3: return FVector3d(U, -1.0, -V).GetSafeNormal();
	case 4: return FVector3d(U, -V, 1.0).GetSafeNormal();
	default: return FVector3d(-U, -V, -1.0).GetSafeNormal();
	}
}

TArray<FColor> HexPlanetSeaField::BuildPixels(const FHexSphereGrid& Grid, TConstArrayView<FCellValues> Values, int32 Size, const FShoreParams& Shore,
	TConstArrayView<FVector3f> CoastPoints)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();

	// the land cells within a few rings of each cell, enough for the 400 units alpha holds, so a texel looks only at those of its three cells
	constexpr int32 LandRings = 3;
	TArray<TArray<int32>> NearbyLand;
	NearbyLand.SetNum(Cells.Num());

	ParallelFor(Cells.Num(), [&](int32 Cell)
	{
		TArray<int32, TInlineAllocator<64>> Ring = { Cell };
		TSet<int32, DefaultKeyFuncs<int32>, TInlineSetAllocator<64>> Seen = { Cell };
		for (int32 Step = 0; Step <= LandRings; ++Step)
		{
			TArray<int32, TInlineAllocator<64>> Next;
			for (const int32 Each : Ring)
			{
				if (Values[Each].bLand)
				{
					NearbyLand[Cell].Add(Each);
				}
				if (Step < LandRings)
				{
					for (const int32 Neighbor : Cells[Each].Neighbors)
					{
						bool bAlready = false;
						Seen.Add(Neighbor, &bAlready);
						if (!bAlready)
						{
							Next.Add(Neighbor);
						}
					}
				}
			}
			Ring = MoveTemp(Next);
		}
	});

	// the stretches of the coast: each land cell on the coast starts one, unless a cell before it took it, and with StretchJoin takes one
	// neighbor on the coast that no stretch holds yet, so a stretch is one or two cells; the phase of each comes from a hash of its first cell
	const auto Hash = [](uint32 Value, uint32 Salt)
	{
		uint32 Mixed = Value * 0x9E3779B1u ^ (Salt * 0x85EBCA77u + 0x165667B1u);
		Mixed ^= Mixed >> 15;
		Mixed *= 0x2C1B3C6Du;
		Mixed ^= Mixed >> 12;
		Mixed *= 0x297A2D39u;
		Mixed ^= Mixed >> 15;
		return double(Mixed & 0xFFFFFF) / double(0x1000000);
	};
	const auto IsCoast = [&](int32 Cell)
	{
		return Values[Cell].bLand && Cells[Cell].Neighbors.ContainsByPredicate([&](int32 Neighbor) { return !Values[Neighbor].bLand; });
	};
	TArray<int32> Stretches;
	Stretches.Init(INDEX_NONE, Cells.Num());
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (!Values[Cell].bLand || Stretches[Cell] != INDEX_NONE)
		{
			continue;
		}
		Stretches[Cell] = Cell;
		if (IsCoast(Cell) && Hash(Cell, 1) < Shore.StretchJoin)
		{
			const int32 NumNeighbors = Cells[Cell].Neighbors.Num();
			const int32 First = int32(Hash(Cell, 2) * NumNeighbors);
			for (int32 Index = 0; Index < NumNeighbors; ++Index)
			{
				const int32 Neighbor = Cells[Cell].Neighbors[(First + Index) % NumNeighbors];
				if (Stretches[Neighbor] == INDEX_NONE && IsCoast(Neighbor))
				{
					Stretches[Neighbor] = Cell;
					break;
				}
			}
		}
	}

	// the middle of each stretch and the direction along its coast, so a breaker is no longer than it should be however far its wedge of sea fans
	// out: the mean of its cells, and across the mean direction from them to their neighbors in the sea. A stretch inland has no direction
	TArray<FVector3d> StretchMiddles;
	TArray<FVector3d> StretchAlong;
	StretchMiddles.Init(FVector3d::ZeroVector, Cells.Num());
	StretchAlong.Init(FVector3d::ZeroVector, Cells.Num());
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Stretches[Cell] == INDEX_NONE)
		{
			continue;
		}
		StretchMiddles[Stretches[Cell]] += Cells[Cell].Center;
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (!Values[Neighbor].bLand)
			{
				StretchAlong[Stretches[Cell]] += Cells[Neighbor].Center - Cells[Cell].Center;
			}
		}
	}
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Stretches[Cell] == Cell)
		{
			const FVector3d Middle = StretchMiddles[Cell].GetSafeNormal();
			const FVector3d Seaward = StretchAlong[Cell] - Middle * StretchAlong[Cell].Dot(Middle);
			StretchMiddles[Cell] = Middle;
			StretchAlong[Cell] = Seaward.IsNearlyZero(1e-12) ? FVector3d::ZeroVector : Middle.Cross(Seaward).GetSafeNormal();
		}
	}

	// the distance to the coast by the land cells, each a disc as wide as the middles of its sides: a soft minimum, relative to the nearest so
	// the exponents stay small, which where many cells of a straight coast lie near comes out lower than at a cape, so further out it follows
	// the run of the coast; how fast it grows, the length of its gradient, below 1 close to the shore, where the land cells around pull it
	// different ways, so the breakers divide it out to roll at an even speed; and how much its lines bend around land (its laplacian, the
	// curvature of a line of equal distance, positive around a cape): each disc gives 1 / its radius from the center, weighted as the soft
	// minimum weighs it, less what the spread of their directions takes off, as on a straight coast the discs pull the lines straight
	const auto CellDistance = [&](const FVector3d& Direction, TConstArrayView<int32> Lands, double& OutNearest, int32& OutNearestCell, double& OutSlope,
		double& OutCurvature)
	{
		OutNearest = 400.0;
		OutNearestCell = INDEX_NONE;
		OutSlope = 1.0;
		OutCurvature = 0.0;
		for (const int32 Land : Lands)
		{
			const double Distance = (Direction - Cells[Land].Center).Length() * Shore.Radius - Shore.LandRadius;
			if (Distance < OutNearest)
			{
				OutNearest = Distance;
				OutNearestCell = Land;
			}
		}
		if (Lands.IsEmpty())
		{
			return OutNearest;
		}
		const double Softness = FMath::Max(Shore.Softness, 1.0);
		double Sum = 0.0;
		double Bend = 0.0;
		FVector3d Gradient = FVector3d::ZeroVector;
		for (const int32 Land : Lands)
		{
			const FVector3d Away = Direction - Cells[Land].Center;
			const double Radius = FMath::Max(Away.Length() * Shore.Radius, 1.0);
			const double Weight = FMath::Exp(-(Radius - Shore.LandRadius - OutNearest) / Softness);
			Sum += Weight;
			Bend += Weight / Radius;
			Gradient += (Away - Direction * Away.Dot(Direction)).GetSafeNormal() * Weight;
		}
		Gradient /= Sum;
		OutSlope = Gradient.Length();
		OutCurvature = Bend / Sum - (1.0 - Gradient.SizeSquared()) / Softness;
		return Shore.Softness > 0.0 ? OutNearest - Shore.Softness * FMath::Loge(Sum) : OutNearest;
	};

	// the real coast, where the ground crosses the sea, winds across the cells: land rises out of the shallows and the sea floods parts of land
	// cells. Its points go to their cells, each with the distance the field of the cells has there, which near them is taken off, so the
	// distance is 0 just on the real coast; and each cell lists the cells within CoastRings that hold any, so a texel looks only at those
	constexpr int32 CoastRings = 2;
	TArray<TArray<int32>> CellCoasts;
	CellCoasts.SetNum(Cells.Num());
	TArray<double> CoastValues;
	CoastValues.SetNumZeroed(CoastPoints.Num());
	{
		int32 Hint = INDEX_NONE;
		for (int32 Index = 0; Index < CoastPoints.Num(); ++Index)
		{
			Hint = Grid.FindCell(FVector3d(CoastPoints[Index]), Hint);
			CellCoasts[Hint].Add(Index);
		}
	}
	ParallelFor(Cells.Num(), [&](int32 Cell)
	{
		for (const int32 Index : CellCoasts[Cell])
		{
			double Nearest, Slope, Curvature;
			int32 NearestCell;
			CoastValues[Index] = CellDistance(FVector3d(CoastPoints[Index]).GetSafeNormal(), NearbyLand[Cell], Nearest, NearestCell, Slope, Curvature);
		}
	});
	TArray<TArray<int32>> NearbyCoast;
	NearbyCoast.SetNum(Cells.Num());
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (CellCoasts[Cell].IsEmpty())
		{
			continue;
		}
		TArray<int32, TInlineAllocator<32>> Ring = { Cell };
		TSet<int32, DefaultKeyFuncs<int32>, TInlineSetAllocator<32>> Seen = { Cell };
		for (int32 Step = 0; Step <= CoastRings; ++Step)
		{
			TArray<int32, TInlineAllocator<32>> Next;
			for (const int32 Each : Ring)
			{
				NearbyCoast[Each].Add(Cell);
				if (Step < CoastRings)
				{
					for (const int32 Neighbor : Cells[Each].Neighbors)
					{
						bool bAlready = false;
						Seen.Add(Neighbor, &bAlready);
						if (!bAlready)
						{
							Next.Add(Neighbor);
						}
					}
				}
			}
			Ring = MoveTemp(Next);
		}
	}

	// the sea field, then the shore field
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(2 * 6 * Size * Size);
	const int32 ShoreOffset = 6 * Size * Size;

	ParallelFor(6 * Size, [&](int32 FaceRow)
	{
		const int32 Face = FaceRow / Size;
		const int32 Y = FaceRow % Size;
		int32 Hint = INDEX_NONE;

		for (int32 X = 0; X < Size; ++X)
		{
			int32 BlendCells[3];
			double Weights[3];
			const FVector3d Direction = GetTexelDirection(Face, X, Y, Size);
			Hint = Grid.FindBlend(Direction, Hint, BlendCells, Weights);

			// the nearest land cells come from the lists of the three cells of the blend, which overlap, so each counts once
			TArray<int32, TInlineAllocator<64>> Lands;
			for (const int32 Each : BlendCells)
			{
				for (const int32 Land : NearbyLand[Each])
				{
					Lands.AddUnique(Land);
				}
			}

			double Nearest, Slope, Curvature;
			int32 NearestCell;
			double ShoreDistance = CellDistance(Direction, Lands, Nearest, NearestCell, Slope, Curvature);

			// near the real coast the distance moves to be 0 just on it: it takes off the value the field of the cells has at the points of the
			// real coast nearby, weighted towards the nearest over CoastBlend, fully within a third of CoastFade from it, fading out by CoastFade
			if (Nearest < Shore.CoastFade + Shore.LandRadius * 2.0)
			{
				TArray<int32, TInlineAllocator<32>> CoastCells;
				for (const int32 Each : BlendCells)
				{
					for (const int32 CoastCell : NearbyCoast[Each])
					{
						CoastCells.AddUnique(CoastCell);
					}
				}
				double CoastNearest = Shore.CoastFade;
				for (const int32 CoastCell : CoastCells)
				{
					for (const int32 Index : CellCoasts[CoastCell])
					{
						CoastNearest = FMath::Min(CoastNearest, (Direction - FVector3d(CoastPoints[Index])).Length() * Shore.Radius);
					}
				}
				if (CoastNearest < Shore.CoastFade)
				{
					const double Blend = FMath::Max(Shore.CoastBlend, 1.0);
					double Sum = 0.0;
					double Shift = 0.0;
					for (const int32 CoastCell : CoastCells)
					{
						for (const int32 Index : CellCoasts[CoastCell])
						{
							const double Distance = (Direction - FVector3d(CoastPoints[Index])).Length() * Shore.Radius;
							if (Distance < CoastNearest + Blend * 6.0)
							{
								const double Weight = FMath::Exp(-(Distance - CoastNearest) / Blend);
								Sum += Weight;
								Shift += CoastValues[Index] * Weight;
							}
						}
					}
					ShoreDistance -= Shift / Sum * (1.0 - FMath::SmoothStep(Shore.CoastFade / 3.0, Shore.CoastFade, CoastNearest));
				}
			}

			// the stretch of the nearest land cell, and how far the texel lies from its end: from the bisector towards the nearest land cell of
			// another stretch, (r2^2 - r1^2) / (2 |c2 - c1|) with r the distances to the centers
			// and how far it lies along the coast from the middle of the stretch
			double StretchPhase = 0.0;
			double StretchBorder = 256.0;
			double StretchOffset = 0.0;
			if (NearestCell != INDEX_NONE)
			{
				const int32 Stretch = Stretches[NearestCell];
				StretchPhase = Hash(uint32(Stretch), 3);
				StretchOffset = FMath::Abs((Direction - StretchMiddles[Stretch]).Dot(StretchAlong[Stretch])) * Shore.Radius;
				const double Near = (Direction - Cells[NearestCell].Center).Length();
				for (const int32 Land : Lands)
				{
					if (Stretches[Land] != Stretch)
					{
						const double Far = (Direction - Cells[Land].Center).Length();
						const double Apart = FMath::Max((Cells[Land].Center - Cells[NearestCell].Center).Length(), 1e-9);
						StretchBorder = FMath::Min(StretchBorder, (Far * Far - Near * Near) / (2.0 * Apart) * Shore.Radius);
					}
				}
			}

			double Depth = 0.0;
			double Frozen = 0.0;

			for (int32 Index = 0; Index < 3; ++Index)
			{
				Depth += Values[BlendCells[Index]].Depth * Weights[Index];
				Frozen += Values[BlendCells[Index]].Frozen * Weights[Index];
			}

			const auto ToByte = [](double Value) { return uint8(FMath::Clamp(FMath::RoundToInt(Value * 255.0), 0, 255)); };
			Pixels[FaceRow * Size + X] = FColor(ToByte(Depth / 100.0), ToByte(FMath::Max(Curvature * (Nearest + Shore.LandRadius), 0.0)), ToByte(Frozen), ToByte((ShoreDistance + 50.0) / 450.0));
			Pixels[ShoreOffset + FaceRow * Size + X] = FColor(ToByte(StretchPhase), ToByte(FMath::Max(StretchBorder, 0.0) / 256.0), ToByte(StretchOffset / 256.0), ToByte(Slope));
		}
	});

	return Pixels;
}
