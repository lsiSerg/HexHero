// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexSphereGrid.h"

namespace
{
	/** Vertices of the icosahedron, before they are put on the unit sphere */
	TArray<FVector3d> GetIcosahedronVertices()
	{
		const double T = (1.0 + FMath::Sqrt(5.0)) / 2.0;
		TArray<FVector3d> Vertices = {
			{-1, T, 0}, {1, T, 0}, {-1, -T, 0}, {1, -T, 0},
			{0, -1, T}, {0, 1, T}, {0, -1, -T}, {0, 1, -T},
			{T, 0, -1}, {T, 0, 1}, {-T, 0, -1}, {-T, 0, 1}};

		for (FVector3d& Vertex : Vertices)
		{
			Vertex.Normalize();
		}
		return Vertices;
	}

	/** Faces of the icosahedron, each turned counterclockwise seen from outside */
	TArray<FIntVector3> GetIcosahedronFaces(const TArray<FVector3d>& Vertices)
	{
		TArray<FIntVector3> Faces = {
			{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
			{1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
			{3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
			{4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};

		for (FIntVector3& Face : Faces)
		{
			const FVector3d& A = Vertices[Face.X];
			const FVector3d& B = Vertices[Face.Y];
			const FVector3d& C = Vertices[Face.Z];

			if (FVector3d::DotProduct(FVector3d::CrossProduct(B - A, C - A), A + B + C) < 0.0)
			{
				Swap(Face.Y, Face.Z);
			}
		}
		return Faces;
	}

	/** Finds the lattice points, so that a point on a side shared by two faces is made once and from the same values */
	struct FLatticeBuilder
	{
		const TArray<FVector3d>& IcosahedronVertices;
		int32 Frequency;
		TArray<FVector3d> Points;
		TArray<int32> PointFaces;
		TMap<uint64, int32> PointIds;

		int32 AddPoint(uint64 Key, const FVector3d& Point, int32 Face)
		{
			if (const int32* Existing = PointIds.Find(Key))
			{
				return *Existing;
			}

			const int32 Id = Points.Add(Point.GetSafeNormal());
			PointFaces.Add(Face);
			PointIds.Add(Key, Id);
			return Id;
		}

		/** A point on the side between two vertices of the icosahedron, Step lattice steps from the first */
		int32 AddSidePoint(int32 From, int32 To, int32 Step, int32 Face)
		{
			if (Step == 0)
			{
				return AddVertex(From, Face);
			}
			if (Step == Frequency)
			{
				return AddVertex(To, Face);
			}

			// the lower vertex is always the start, so both faces of the side compute the same point
			if (From > To)
			{
				Swap(From, To);
				Step = Frequency - Step;
			}

			const uint64 Key = (1ull << 60) | (uint64(From) << 40) | (uint64(To) << 20) | uint64(Step);
			const double Alpha = double(Step) / Frequency;
			return AddPoint(Key, Blend(IcosahedronVertices[From], IcosahedronVertices[To], FVector3d::ZeroVector, 1.0 - Alpha, Alpha, 0.0), Face);
		}

		/**
		 *  Blends the vertices of a face by the sines of their weights times the angle of a side, which spaces the points of a side
		 *  at even angles, as a straight lerp crowds them towards the vertices once they are put on the sphere
		 */
		static FVector3d Blend(const FVector3d& A, const FVector3d& B, const FVector3d& C, double WeightA, double WeightB, double WeightC)
		{
			static const double SideAngle = FMath::Acos(1.0 / FMath::Sqrt(5.0));
			return A * FMath::Sin(WeightA * SideAngle) + B * FMath::Sin(WeightB * SideAngle) + C * FMath::Sin(WeightC * SideAngle);
		}

		int32 AddVertex(int32 Vertex, int32 Face)
		{
			return AddPoint(uint64(Vertex), IcosahedronVertices[Vertex], Face);
		}
	};
}

FHexSphereGrid FHexSphereGrid::Build(int32 Frequency, int32 ChunkSteps)
{
	Frequency = FMath::Max(Frequency, 1);
	const int32 Divisions = ChunkSteps > 0 ? FMath::Clamp(FMath::RoundToInt32(double(Frequency) / ChunkSteps), 1, Frequency) : 1;

	const TArray<FVector3d> IcosahedronVertices = GetIcosahedronVertices();
	const TArray<FIntVector3> Faces = GetIcosahedronFaces(IcosahedronVertices);

	FLatticeBuilder Lattice{IcosahedronVertices, Frequency};

	// the vertices come first, so the pentagons are the first cells
	for (int32 Vertex = 0; Vertex < IcosahedronVertices.Num(); ++Vertex)
	{
		Lattice.AddVertex(Vertex, 0);
	}

	TArray<FIntVector3> Triangles;
	Triangles.Reserve(20 * Frequency * Frequency);

	// the chunk of a point is the triangle that holds it when the first face that has it is split into Divisions steps along a side,
	// two per square of steps, numbered in the order they are found
	TArray<int32> PointChunks;
	TMap<int32, int32> ChunkIds;
	const auto SetChunk = [&](int32 Point, int32 FaceIndex, int32 I, int32 J)
	{
		while (PointChunks.Num() <= Point)
		{
			PointChunks.Add(INDEX_NONE);
		}
		if (PointChunks[Point] != INDEX_NONE)
		{
			return;
		}

		const double A = double(I) * Divisions / Frequency;
		const double B = double(J) * Divisions / Frequency;
		const int32 Row = FMath::Clamp(FMath::FloorToInt32(A), 0, Divisions - 1);
		const int32 Column = FMath::Clamp(FMath::FloorToInt32(B), 0, Divisions - 1 - Row);
		const int32 Upper = (A - Row) + (B - Column) > 1.0 && Row + Column < Divisions - 1 ? 1 : 0;
		const int32 Key = ((FaceIndex * Divisions + Row) * Divisions + Column) * 2 + Upper;
		PointChunks[Point] = ChunkIds.FindOrAdd(Key, ChunkIds.Num());
	};

	for (int32 FaceIndex = 0; FaceIndex < Faces.Num(); ++FaceIndex)
	{
		const FIntVector3& Face = Faces[FaceIndex];
		const FVector3d& A = IcosahedronVertices[Face.X];
		const FVector3d& B = IcosahedronVertices[Face.Y];
		const FVector3d& C = IcosahedronVertices[Face.Z];

		// a point I steps towards B and J steps towards C from A
		TArray<TArray<int32>> Grid;
		Grid.SetNum(Frequency + 1);

		for (int32 I = 0; I <= Frequency; ++I)
		{
			Grid[I].SetNum(Frequency + 1 - I);

			for (int32 J = 0; J <= Frequency - I; ++J)
			{
				const int32 K = Frequency - I - J;
				int32 Id;

				if (J == 0)
				{
					Id = Lattice.AddSidePoint(Face.X, Face.Y, I, FaceIndex);
				}
				else if (I == 0)
				{
					Id = Lattice.AddSidePoint(Face.X, Face.Z, J, FaceIndex);
				}
				else if (K == 0)
				{
					Id = Lattice.AddSidePoint(Face.Y, Face.Z, J, FaceIndex);
				}
				else
				{
					const uint64 Key = (2ull << 60) | (uint64(FaceIndex) << 40) | (uint64(I) << 20) | uint64(J);
					Id = Lattice.AddPoint(Key, FLatticeBuilder::Blend(A, B, C, double(K) / Frequency, double(I) / Frequency, double(J) / Frequency), FaceIndex);
				}

				Grid[I][J] = Id;
				SetChunk(Id, FaceIndex, I, J);
			}
		}

		for (int32 I = 0; I < Frequency; ++I)
		{
			for (int32 J = 0; J < Frequency - I; ++J)
			{
				Triangles.Add({Grid[I][J], Grid[I + 1][J], Grid[I][J + 1]});

				if (J < Frequency - I - 1)
				{
					Triangles.Add({Grid[I + 1][J], Grid[I + 1][J + 1], Grid[I][J + 1]});
				}
			}
		}
	}

	const TArray<FVector3d>& Points = Lattice.Points;
	const int32 NumPoints = Points.Num();

	// each triangle of a point gives the neighbor that follows another counterclockwise
	struct FRingStep
	{
		int32 From;
		int32 To;
		int32 Triangle;
	};

	TArray<TArray<FRingStep, TInlineAllocator<6>>> RingSteps;
	RingSteps.SetNum(NumPoints);

	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		const FIntVector3& Triangle = Triangles[Index];
		RingSteps[Triangle.X].Add({Triangle.Y, Triangle.Z, Index});
		RingSteps[Triangle.Y].Add({Triangle.Z, Triangle.X, Index});
		RingSteps[Triangle.Z].Add({Triangle.X, Triangle.Y, Index});
	}

	FHexSphereGrid Result;
	Result.Frequency = Frequency;
	Result.NumChunks = ChunkIds.Num();
	Result.Cells.SetNum(NumPoints);

	for (int32 Point = 0; Point < NumPoints; ++Point)
	{
		const TArray<FRingStep, TInlineAllocator<6>>& Steps = RingSteps[Point];
		FHexSphereCell& Cell = Result.Cells[Point];
		Cell.Face = Lattice.PointFaces[Point];
		Cell.Chunk = PointChunks[Point];

		// corner K is the triangle between neighbor K and neighbor K + 1
		int32 Neighbor = Steps[0].From;

		for (int32 Count = 0; Count < Steps.Num(); ++Count)
		{
			const FRingStep* Step = Steps.FindByPredicate([Neighbor](const FRingStep& Each) { return Each.From == Neighbor; });
			check(Step);
			Cell.Neighbors.Add(Neighbor);
			Cell.Corners.Add(Step->Triangle);
			Neighbor = Step->To;
		}
	}

	for (int32 Point = 0; Point < NumPoints; ++Point)
	{
		Result.Cells[Point].Center = Points[Point];
	}

	Result.Corners.SetNumUninitialized(Triangles.Num());

	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		const FIntVector3& Triangle = Triangles[Index];
		Result.Corners[Index] = (Points[Triangle.X] + Points[Triangle.Y] + Points[Triangle.Z]).GetSafeNormal();
	}

	return Result;
}

int32 FHexSphereGrid::FindCell(const FVector3d& Direction, int32 StartCell) const
{
	if (Cells.IsEmpty())
	{
		return INDEX_NONE;
	}

	const FVector3d Target = Direction.GetSafeNormal();
	int32 Current = StartCell;

	// otherwise start from the nearest of the pentagons, which spread evenly over the sphere
	if (!Cells.IsValidIndex(Current))
	{
		Current = 0;

		for (int32 Pentagon = 1; Pentagon < 12; ++Pentagon)
		{
			if (FVector3d::DotProduct(Cells[Pentagon].Center, Target) > FVector3d::DotProduct(Cells[Current].Center, Target))
			{
				Current = Pentagon;
			}
		}
	}

	// on a lattice of triangles that is nearly Delaunay a walk to nearer neighbors ends at the nearest cell
	while (true)
	{
		int32 Best = Current;
		double BestDot = FVector3d::DotProduct(Cells[Current].Center, Target);

		for (const int32 Neighbor : Cells[Current].Neighbors)
		{
			const double Dot = FVector3d::DotProduct(Cells[Neighbor].Center, Target);

			if (Dot > BestDot)
			{
				Best = Neighbor;
				BestDot = Dot;
			}
		}

		if (Best == Current)
		{
			return Current;
		}
		Current = Best;
	}
}

int32 FHexSphereGrid::FindBlend(const FVector3d& Direction, int32 StartCell, int32 (&OutCells)[3], double (&OutWeights)[3]) const
{
	const int32 Nearest = FindCell(Direction, StartCell);
	const FHexSphereCell& Cell = Cells[Nearest];
	const FVector3d Target = Direction.GetSafeNormal();

	// the triangle between the nearest cell and two neighbors in a row whose weights are least negative holds the direction
	double BestWorst = -TNumericLimits<double>::Max();

	for (int32 Index = 0; Index < Cell.Neighbors.Num(); ++Index)
	{
		const int32 B = Cell.Neighbors[Index];
		const int32 C = Cell.Neighbors[(Index + 1) % Cell.Neighbors.Num()];
		const FVector3d& PointA = Cell.Center;
		const FVector3d& PointB = Cells[B].Center;
		const FVector3d& PointC = Cells[C].Center;

		// each weight is the volume spanned with the opposite side, as on a flat triangle its area
		const double WeightA = FVector3d::DotProduct(Target, FVector3d::CrossProduct(PointB, PointC));
		const double WeightB = FVector3d::DotProduct(Target, FVector3d::CrossProduct(PointC, PointA));
		const double WeightC = FVector3d::DotProduct(Target, FVector3d::CrossProduct(PointA, PointB));
		const double Sum = WeightA + WeightB + WeightC;

		if (Sum <= 0.0)
		{
			continue;
		}

		const double Worst = FMath::Min3(WeightA, WeightB, WeightC) / Sum;

		if (Worst > BestWorst)
		{
			BestWorst = Worst;
			OutCells[0] = Nearest;
			OutCells[1] = B;
			OutCells[2] = C;
			OutWeights[0] = FMath::Max(WeightA / Sum, 0.0);
			OutWeights[1] = FMath::Max(WeightB / Sum, 0.0);
			OutWeights[2] = FMath::Max(WeightC / Sum, 0.0);
		}
	}

	const double Total = OutWeights[0] + OutWeights[1] + OutWeights[2];

	for (double& Weight : OutWeights)
	{
		Weight /= Total;
	}
	return Nearest;
}

double FHexSphereGrid::GetCellArea(int32 Cell) const
{
	const FHexSphereCell& Each = Cells[Cell];
	double Area = 0.0;

	for (int32 Index = 0; Index < Each.Corners.Num(); ++Index)
	{
		const FVector3d& A = Corners[Each.Corners[Index]];
		const FVector3d& B = Corners[Each.Corners[(Index + 1) % Each.Corners.Num()]];
		Area += FVector3d::CrossProduct(A - Each.Center, B - Each.Center).Size() / 2.0;
	}
	return Area;
}

double FHexSphereGrid::GetCellSideRatio(int32 Cell) const
{
	const FHexSphereCell& Each = Cells[Cell];
	double Shortest = TNumericLimits<double>::Max();
	double Longest = 0.0;

	for (int32 Index = 0; Index < Each.Corners.Num(); ++Index)
	{
		const double Side = FVector3d::Distance(Corners[Each.Corners[Index]], Corners[Each.Corners[(Index + 1) % Each.Corners.Num()]]);
		Shortest = FMath::Min(Shortest, Side);
		Longest = FMath::Max(Longest, Side);
	}
	return Longest / Shortest;
}

double FHexSphereGrid::IntersectSphere(const FVector3d& Origin, const FVector3d& Direction, double SphereRadius)
{
	const double B = FVector3d::DotProduct(Origin, Direction);
	const double C = Origin.SizeSquared() - SphereRadius * SphereRadius;
	const double Discriminant = B * B - C;

	if (Discriminant < 0.0)
	{
		return -1.0;
	}

	const double Root = FMath::Sqrt(Discriminant);
	return -B - Root >= 0.0 ? -B - Root : -B + Root;
}
