// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHeroPath.h"
#include "HexSphereGrid.h"
#include "Algo/Reverse.h"

namespace
{
	/** A cell waiting in the queue of the search with the cost it was reached at, and the order it was pushed in, which breaks ties */
	struct FQueued
	{
		float Cost = 0.0f;
		int32 Order = 0;
		int32 Cell = INDEX_NONE;

		bool operator<(const FQueued& Other) const { return Cost != Other.Cost ? Cost < Other.Cost : Order < Other.Order; }
	};

	/** Costs are sums of a few floats, so two ways of the same steps may differ by rounding */
	constexpr float CostTolerance = 1.0e-4f;
}

bool HexHeroPath::FField::GetPath(int32 Target, TArray<int32>& OutCells) const
{
	OutCells.Reset();
	if (!IsReached(Target))
	{
		return false;
	}
	for (int32 Cell = Target; Cell != Start && Cell != INDEX_NONE; Cell = Previous[Cell])
	{
		OutCells.Add(Cell);
	}
	Algo::Reverse(OutCells);
	return true;
}

void HexHeroPath::FindField(const FHexSphereGrid& Grid, int32 Start, float MaxCost, FStepCost GetStepCost, FField& OutField)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	OutField.Start = Start;
	OutField.Costs.Init(-1.0f, Cells.Num());
	OutField.Previous.Init(INDEX_NONE, Cells.Num());
	if (!Cells.IsValidIndex(Start))
	{
		return;
	}

	TArray<FQueued> Queue;
	int32 Order = 0;
	OutField.Costs[Start] = 0.0f;
	Queue.HeapPush({0.0f, Order++, Start});

	while (!Queue.IsEmpty())
	{
		FQueued Current;
		Queue.HeapPop(Current, EAllowShrinking::No);
		// an older entry of a cell that was reached cheaper since
		if (Current.Cost > OutField.Costs[Current.Cell] + CostTolerance)
		{
			continue;
		}

		const FHexSphereCell& Cell = Cells[Current.Cell];
		for (int32 Side = 0; Side < Cell.Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Cell.Neighbors[Side];
			const float StepCost = GetStepCost(Current.Cell, Side, Neighbor);
			if (StepCost < 0.0f)
			{
				continue;
			}
			// a step is never free, or the search could circle
			const float Cost = Current.Cost + FMath::Max(StepCost, CostTolerance);
			if (Cost > MaxCost + CostTolerance)
			{
				continue;
			}
			if (OutField.Costs[Neighbor] < 0.0f || Cost < OutField.Costs[Neighbor] - CostTolerance)
			{
				OutField.Costs[Neighbor] = Cost;
				OutField.Previous[Neighbor] = Current.Cell;
				Queue.HeapPush({Cost, Order++, Neighbor});
			}
		}
	}
}

bool HexHeroPath::CanStep(float Cost, float Points, float PointsPerDay)
{
	return Points + CostTolerance >= Cost || (Points + CostTolerance >= PointsPerDay && Cost > PointsPerDay);
}

HexHeroPath::FSpend HexHeroPath::Spend(TConstArrayView<float> StepCosts, float Points, float PointsPerDay)
{
	FSpend Result;
	if (StepCosts.IsEmpty())
	{
		return Result;
	}

	Result.Days = 1;
	bool bToday = true;
	for (const float Cost : StepCosts)
	{
		Result.Total += Cost;
		if (!CanStep(Cost, Points, PointsPerDay))
		{
			// the day ends here and the next begins with all its points, which pay for any step (CanStep)
			++Result.Days;
			bToday = false;
			Points = PointsPerDay;
		}
		Points = FMath::Max(Points - Cost, 0.0f);
		Result.StepsToday += bToday ? 1 : 0;
	}
	return Result;
}
