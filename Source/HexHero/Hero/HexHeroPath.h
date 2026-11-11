// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FHexSphereGrid;

/**
 *  The ways of a hero over the cells of a layer of the hex planet. Pure: it reads the grid and asks the caller what each step costs, so it runs
 *  without a world and the HexHero.Hero tests check it on small grids.
 */
namespace HexHeroPath
{
	/** Returns the cost of the step from a cell across its side to the neighbor there, more than 0, or a negative value if it cannot be taken */
	using FStepCost = TFunctionRef<float(int32 From, int32 Side, int32 To)>;

	/** The cheapest ways from one cell to the cells around it (FindField) */
	struct FField
	{
		int32 Start = INDEX_NONE;

		/** Cost of the cheapest way to each cell, negative for one not reached */
		TArray<float> Costs;

		/** The cell each reached cell is entered from on that way, INDEX_NONE for the start */
		TArray<int32> Previous;

		/** Returns true if a way leads to a cell, the start too */
		bool IsReached(int32 Cell) const { return Costs.IsValidIndex(Cell) && Costs[Cell] >= 0.0f; }

		/** Gives the cells of the way to a cell in the order they are entered, without the start. Returns false if none leads there */
		bool GetPath(int32 Target, TArray<int32>& OutCells) const;
	};

	/** Finds the cheapest ways from a cell to every cell within MaxCost of it, by Dijkstra over the neighbors; ways of one cost keep the cell
	 *  found first, so the result is the same in every run */
	void FindField(const FHexSphereGrid& Grid, int32 Start, float MaxCost, FStepCost GetStepCost, FField& OutField);

	/** How a hero walks a way of steps with the points it has */
	struct FSpend
	{
		/** Steps it takes today, before its points run out */
		int32 StepsToday = 0;

		/** Days the whole way takes, 1 when it ends today, 0 for no steps */
		int32 Days = 0;

		/** Cost of all the steps */
		float Total = 0.0f;
	};

	/** Returns true if a hero with Points left of PointsPerDay can take a step of a cost: the points pay for it, or it has all the points of a
	 *  day and the step is dearer than that, which then takes them all, so no step is out of reach for ever */
	bool CanStep(float Cost, float Points, float PointsPerDay);

	/** Returns how a hero with Points left today and PointsPerDay every day after walks steps of these costs: a step is taken only while
	 *  CanStep allows it, and what is left over is lost with the day */
	FSpend Spend(TConstArrayView<float> StepCosts, float Points, float PointsPerDay);

	/** Gives the day each of these steps is taken on, 1 for today, by the rule of Spend */
	void FindStepDays(TConstArrayView<float> StepCosts, float Points, float PointsPerDay, TArray<int32>& OutDays);
}
