// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexShroud.h"
#include "HexSphereGrid.h"

void HexShroud::FindWithin(const FHexSphereGrid& Grid, int32 Cell, int32 Rings, TArray<int32>& OutCells)
{
	OutCells.Reset();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (!Cells.IsValidIndex(Cell))
	{
		return;
	}
	TSet<int32> Seen = {Cell};
	OutCells.Add(Cell);
	int32 RingStart = 0;
	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
		const int32 RingEnd = OutCells.Num();
		for (int32 Index = RingStart; Index < RingEnd; ++Index)
		{
			for (const int32 Neighbor : Cells[OutCells[Index]].Neighbors)
			{
				bool bAlready = false;
				Seen.Add(Neighbor, &bAlready);
				if (!bAlready)
				{
					OutCells.Add(Neighbor);
				}
			}
		}
		RingStart = RingEnd;
	}
}

FVector2d HexShroud::GetFieldUV(const FVector3d& Direction)
{
	const double Sum = FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) + FMath::Abs(Direction.Z);
	if (Sum <= 0.0)
	{
		return FVector2d(0.5, 0.5);
	}
	const FVector3d Flat = Direction / Sum;
	// the lower half of the octahedron folds out over the corners of the square
	const FVector2d Point = Flat.Z >= 0.0 ? FVector2d(Flat.X, Flat.Y)
		: FVector2d((1.0 - FMath::Abs(Flat.Y)) * (Flat.X >= 0.0 ? 1.0 : -1.0), (1.0 - FMath::Abs(Flat.X)) * (Flat.Y >= 0.0 ? 1.0 : -1.0));
	return Point * 0.5 + FVector2d(0.5, 0.5);
}

FVector3d HexShroud::GetTexelDirection(int32 X, int32 Y, int32 Size)
{
	const double U = 2.0 * (X + 0.5) / Size - 1.0;
	const double V = 2.0 * (Y + 0.5) / Size - 1.0;
	FVector3d Direction(U, V, 1.0 - FMath::Abs(U) - FMath::Abs(V));
	const double Fold = FMath::Max(-Direction.Z, 0.0);
	Direction.X += Direction.X >= 0.0 ? -Fold : Fold;
	Direction.Y += Direction.Y >= 0.0 ? -Fold : Fold;
	return Direction.GetSafeNormal();
}

void UHexShroud::Reset()
{
	Players.Reset();
	++Revision;
}

bool UHexShroud::Explore(int32 Player, EHexPlanetLayer Layer, TConstArrayView<int32> Cells, int32 NumCells)
{
	TBitArray<>& Explored = Players.FindOrAdd(Player).Explored[int32(Layer)];
	if (Explored.Num() != NumCells)
	{
		Explored.Init(false, NumCells);
	}
	bool bNew = false;
	for (const int32 Cell : Cells)
	{
		if (Explored.IsValidIndex(Cell) && !Explored[Cell])
		{
			Explored[Cell] = true;
			bNew = true;
		}
	}
	if (bNew)
	{
		++Revision;
	}
	return bNew;
}

bool UHexShroud::IsExplored(int32 Player, EHexPlanetLayer Layer, int32 Cell) const
{
	const TBitArray<>* Explored = GetExplored(Player, Layer);
	return Explored && Explored->IsValidIndex(Cell) && (*Explored)[Cell];
}

const TBitArray<>* UHexShroud::GetExplored(int32 Player, EHexPlanetLayer Layer) const
{
	const FPlayerCells* Found = Players.Find(Player);
	return Found && Found->Explored[int32(Layer)].Num() > 0 ? &Found->Explored[int32(Layer)] : nullptr;
}

int32 UHexShroud::CountExplored(int32 Player, EHexPlanetLayer Layer) const
{
	const TBitArray<>* Explored = GetExplored(Player, Layer);
	return Explored ? Explored->CountSetBits() : 0;
}
