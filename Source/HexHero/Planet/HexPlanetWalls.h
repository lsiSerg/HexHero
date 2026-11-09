// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FHexSphereGrid;
class UHexPlanetConfig;

/**
 *  The walls on the lips of the passages of the underground (UHexPlanetConfig::bWalls). Their line follows the sides of the passages towards a
 *  cell MinDrop levels lower, keeping the tips of the hexes, and cuts across each corner of a void between two passages by CornerCut of its sides,
 *  as if the floor went on into that void, so the walls still show the hexes they stand on; every corner is then rounded by CornerRound, the
 *  line sampled every Step world units, each loop closed. The wall stands on the void side of the line: its inner face rises from a rounded foot
 *  that reaches FootReach onto the floor and a flat floor where the line cut a corner (FSample::Gap), as the passage has no floor there, leans back
 *  and bulges out at its top into an overhang from which stalactites hang, ribs of a cellular pattern of the place that end at heights of their
 *  own, some reaching the floor as columns, with scooped flutes between them; its crest rises and falls by the stalactites, and its outer face
 *  goes down OuterDepth into the chasm from the line smoothed over OuterSmooth, so a thick wall does not pinch at a corner. Bulged by 3D noises of
 *  the place, so no stretch repeats from any distance. Pure: it reads no UObject, so any thread may build it, and every chunk builds the same rings
 *  where two of them meet
 */
namespace HexPlanetWalls
{
	struct FParams
	{
		int32 MinDrop = 2;
		/** Radius of the floor of the passages, the sphere the line lies on */
		double FloorRadius = 0.0;
		double Step = 6.0;
		double CornerCut = 0.5;
		double CornerRound = 30.0;
		double OuterSmooth = 90.0;
		double Height = 170.0;
		double HeightNoise = 0.25;
		double HeightNoiseScale = 320.0;
		double Thickness = 75.0;
		double FootReach = 12.0;
		double Fillet = 30.0;
		double Lean = 0.05;
		double Overhang = 12.0;
		double OuterDepth = 160.0;
		double OuterFlare = 25.0;
		double Bulge = 6.0;
		double BulgeScale = 55.0;
		double Detail = 2.0;
		double DetailScale = 15.0;
		double CrestNoise = 0.1;
		double CrestJag = 0.12;
		double CrestNotch = 0.08;
		double RibSize = 50.0;
		double RibDepth = 12.0;
		double RibTipMin = 0.25;
		double RibTipMax = 0.7;
		double ColumnShare = 0.12;
		double FluteDepth = 7.0;
		double FluteWidth = 0.3;
		/** Moves the noises, so another planet has other walls */
		int32 Seed = 0;

		/** The values of a config for a floor radius and a seed */
		static FParams FromConfig(const UHexPlanetConfig& Config, double FloorRadius, int32 Seed);
	};

	/** A sample of a line of walls, on the floor */
	struct FSample
	{
		FVector3d Position = FVector3d::ZeroVector;
		FVector3d Up = FVector3d::UnitZ();
		FVector3d Along = FVector3d::UnitX();
		/** Across the line onto the passage */
		FVector3d Inward = FVector3d::UnitY();
		/** On the line smoothed over OuterSmooth, and onto the passage from there, from which the outer face and the crest stand */
		FVector3d OuterPosition = FVector3d::ZeroVector;
		FVector3d OuterInward = FVector3d::UnitY();
		/** World units along its line from its start */
		double S = 0.0;
		/** The passage of the side it comes from */
		int32 Cell = INDEX_NONE;
		/** How far onto the passage from the line its floor begins: 0 where the line follows a side or runs over the floor, more where it cuts
		 *  across a corner of a void, which the foot of the wall covers with floor */
		double Gap = 0.0;

		/** How far the wall may reach from each line onto the passage and towards the chasm here, about the radius of the turn of that line on
		 *  that side, so a wall does not fold over itself inside a tight turn */
		double InnerReach = TNumericLimits<double>::Max();
		double OuterReach = TNumericLimits<double>::Max();
		double SmoothInnerReach = TNumericLimits<double>::Max();
		double SmoothOuterReach = TNumericLimits<double>::Max();
	};

	struct FWalls
	{
		/** Closed loops of samples */
		TArray<TArray<FSample>> Lines;

		/** (line, sample) of each chunk, by the chunk of the cell of the sample; a chunk builds the stretch from each of its samples to the next */
		TArray<TArray<FIntPoint>> ChunkSamples;

		/** (line, sample) by the cell of the sample and its neighbors, to keep objects off the foot of the walls */
		TArray<TArray<FIntPoint>> CellSamples;

		const FSample& Get(const FIntPoint& At) const { return Lines[At.X][At.Y]; }
		int32 Next(const FIntPoint& At) const { return (At.Y + 1) % Lines[At.X].Num(); }
	};

	/** Builds the lines of the walls along every side of a cell that CanHold towards a neighbor at least MinDrop levels lower */
	void Build(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TFunctionRef<bool(int32)> CanHold, const FParams& Params, FWalls& Out);

	/** Returns the height of a wall at a sample, without the rise and fall of its crest by the stalactites */
	double GetHeight(const FSample& Sample, const FParams& Params);

	/** Returns how far the foot of a wall reaches onto the floor of a passage from its line, where the floor is clear again */
	double GetFootReach(const FParams& Params);

	/** Gives a point of the inner face of a wall at a sample, Height above the floor, and the way out of the face there */
	void GetInnerFace(const FWalls& Walls, const FIntPoint& At, double Height, const FParams& Params, FVector3d& OutPosition, FVector3d& OutNormal);

	/** Returns the distance across the floor from a point to the nearest sample listed for a cell (CellSamples), 0 for a point beyond the line,
	 *  towards the chasm, and a large number without samples */
	double GetDistanceToLine(const FWalls& Walls, int32 Cell, const FVector3d& Point);

	/** The mesh of the walls of a chunk, in world space: positions, normals, in the first UV how deep in a hollow a point lies (0 to 1, flutes,
	 *  under the overhang and between the stalactites) and its height above the floor as a share of the wall, in the second how much it is floor
	 *  (the foot and the floor before it, 0 to 1) and how much of a stalactite stands out there (0 to 1), and three triangles each */
	struct FMesh
	{
		TArray<FVector3f> Positions;
		TArray<FVector3f> Normals;
		TArray<FVector2f> UVs;
		TArray<FVector2f> UV1s;
		TArray<int32> Triangles;
	};
	void BuildChunk(const FWalls& Walls, int32 Chunk, const FParams& Params, FMesh& Out);

	/** Returns a hash of the samples of a chunk and of the next sample of each, by which a rebuild finds the chunks whose walls changed */
	uint32 HashChunk(const FWalls& Walls, int32 Chunk);
}
