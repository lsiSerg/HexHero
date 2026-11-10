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
	/** A style of the walls (FHexWallStyle): its weight, factors on the values of the walls where it holds, and how much its stone is flowstone and
	 *  how much it shows its joints */
	struct FStyle
	{
		double Weight = 1.0;
		double Height = 1.0;
		double Thickness = 1.0;
		double Lean = 1.0;
		double Overhang = 1.0;
		double Bulge = 1.0;
		double CrestJag = 1.0;
		double CrestNotch = 1.0;
		double RibSize = 1.0;
		double RibDepth = 1.0;
		double RibTip = 1.0;
		double ColumnShare = 1.0;
		double FluteDepth = 1.0;
		double Flowstone = 0.3;
		double Fracture = 0.5;
	};

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
		/** A wall as high as PoolHeight of its height (FSample::Height), as along a void of lava or a lake, is a parapet of PoolThickness of its
		 *  thickness, without an overhang, so they show over it; between that and its full height it blends towards one, and below it narrows to
		 *  its line. The heights ease from one void to the next over HeightBlend world units each way along the line */
		double PoolHeight = 0.3;
		double PoolThickness = 0.5;
		double HeightBlend = 60.0;
		/** The styles, each holding in regions of a noise of the place StyleScale across, blending where they meet over less the higher
		 *  StyleSharpness; none keeps the values as they are */
		TArray<FStyle> Styles;
		double StyleScale = 450.0;
		double StyleSharpness = 5.0;
		/** The rise of the floor of the passages (UHexPlanetConfig::FloorNoise, HexPlanetKit::GetFloorRise), which the walls take, so they stand
		 *  on it: its height, 0 for none, the size of its noise and the share of its finer noise */
		double FloorRise = 0.0;
		double FloorRiseScale = 150.0;
		double FloorRiseDetail = 0.0;

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

		/** How much of the void beyond is lava and how much a lake (0 to 1, smoothed along the line), which light the wall */
		double Lava = 0.0;
		double Lake = 0.0;

		/** The factor on the height of the wall of the void beyond (Build), smoothed along the line: 1 a full wall, PoolHeight a parapet, 0 none */
		double Height = 1.0;

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

	/** Builds the lines of the walls along every side of a cell that CanHold towards a neighbor at least MinDrop levels lower; VoidStyles, a value
	 *  per cell, says which voids are lava (1) or a lake (2), which light the wall, and VoidHeights, a value per cell, the factor on the height of
	 *  the walls along each void (FSample::Height), 1 for all without it */
	void Build(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TFunctionRef<bool(int32)> CanHold, const FParams& Params, FWalls& Out,
		TConstArrayView<uint8> VoidStyles = {}, TConstArrayView<float> VoidHeights = {});

	/** Returns the height of a wall at a sample, without the rise and fall of its crest by the stalactites */
	double GetHeight(const FSample& Sample, const FParams& Params);

	/** Returns how much of its width a wall keeps at a sample: 1 down to a parapet (FParams::PoolHeight), less below it as it narrows to its line,
	 *  0 where it is gone */
	double GetPresence(const FSample& Sample, const FParams& Params);

	/** Returns how far the foot of a wall reaches onto the floor of a passage from its line, where the floor is clear again */
	double GetFootReach(const FParams& Params);

	/** Returns the radius of the floor of the passages in a direction, risen by its noise, on which the walls stand */
	double GetFloorRadius(const FParams& Params, const FVector3d& Direction);

	/** Gives a point of the inner face of a wall at a sample, Height above the floor, and the way out of the face there */
	void GetInnerFace(const FWalls& Walls, const FIntPoint& At, double Height, const FParams& Params, FVector3d& OutPosition, FVector3d& OutNormal);

	/** Returns the distance across the floor from a point to the nearest sample listed for a cell (CellSamples), 0 for a point beyond the line,
	 *  towards the chasm, and a large number without samples */
	double GetDistanceToLine(const FWalls& Walls, int32 Cell, const FVector3d& Point);

	/** The mesh of the walls of a chunk, in world space: positions, normals, in the first UV how deep in a hollow a point lies (0 to 1, flutes,
	 *  under the overhang and between the stalactites) and its height above the floor as a share of the wall, in the second how much it is floor
	 *  (the foot and the floor before it, 0 to 1) or, below 0, how much it is the outer face (to -1, over the crest towards the chasm, from the
	 *  smoothed line), as no row is both, and how much of a stalactite stands out there (0 to 1), in the third how much its stone is
	 *  flowstone and how much it shows its joints (FStyle), in the fourth how much lava and how much lake lies beyond it (FSample::Lava, Lake),
	 *  and three triangles each */
	struct FMesh
	{
		TArray<FVector3f> Positions;
		TArray<FVector3f> Normals;
		TArray<FVector2f> UVs;
		TArray<FVector2f> UV1s;
		TArray<FVector2f> UV2s;
		TArray<FVector2f> UV3s;
		TArray<int32> Triangles;
	};
	/** Builds it whole, or coarse: every third sample and 14 of the rows of the profile, about a fifth of the triangles, which the planet draws
	 *  further than WallLodDistance */
	void BuildChunk(const FWalls& Walls, int32 Chunk, const FParams& Params, FMesh& Out, bool bCoarse = false);

	/** The mesh of the walls along a cell, as BuildChunk builds it: the stretches from the samples listed for it (CellSamples) within Reach of its
	 *  middle on the floor, so a void cell has the walls along its sides too, for the cells under the cursor in the wireframe view */
	void BuildNear(const FWalls& Walls, int32 Cell, const FVector3d& Middle, double Reach, const FParams& Params, FMesh& Out);

	/** Returns a hash of the samples of a chunk and of the next sample of each, by which a rebuild finds the chunks whose walls changed */
	uint32 HashChunk(const FWalls& Walls, int32 Chunk);
}
