// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetConfig.h"

class FHexSphereGrid;
class UStaticMesh;

/**
 *  Bends the pieces of a kit, modelled on a regular hex, onto the irregular cells of the hex sphere.
 *
 *  Each of the six triangles from the center of the regular hex to two of its corners goes onto the triangle from the center of the cell to the
 *  matching corners, by the same weights, so a side of a piece lands exactly on the side of its cell and neighbors share it.
 *  Direction D of a piece, as FHexCoord numbers it, is neighbor D of the cell, and its corner D is corner D - 1 of the cell.
 *
 *  Heights follow two surfaces of the cell. The top is where height 0 of the piece goes and the side line is where its foot goes; a vertex
 *  in between blends them by its height. Two levels share the slope between them: every corner of the grid lies at the mean top of its three
 *  cells, and a side at the height GetSideHeight gives, the mean of the tops of its two cells in its middle, bending linearly to its corners,
 *  which both cells find alike. So the higher cell falls to the line of the side and the lower one rises to it. With EHexEdgeRule::HigherHolds
 *  a corner lies at the highest of the three tops and a side at the higher of the two, so the higher cell stays at its top up to its sides and
 *  the lower one carries the whole slope. Land bends its top onto that
 *  line near a side to the same level (bTopFollowsCorners), so a side that a piece models anywhere between 0 and the foot lands on its
 *  neighbor, and a point on a side towards another level, or at a corner of such a side, is put on the line whatever its height.
 */
namespace HexPlanetKit
{
	/** What a vertex needs to know about its cell */
	struct FCellFrame
	{
		/** Radius of the top of the cell */
		double Top = 0.0;

		/** By side: the height of the middle of that side, the mean of the tops of the cell and that neighbor, or the higher of them with
		 *  EHexEdgeRule::HigherHolds */
		TArray<double, TInlineAllocator<6>> SideMiddles;

		/** By side: true if that neighbor has the same level */
		TArray<bool, TInlineAllocator<6>> SameSides;

		/** Share of the plateau (FPlacement::PlateauLift) in the center, by the middle of each side and by corner, blended as the massif. A cell
		 *  has the share of its sides that face neighbors on its level, where a pentagon, a flat tile, has none and counts for none of its
		 *  neighbors; a side or a corner whose cells are all on the plateau has their mean, any other 0, so the plateau rises where many such
		 *  cells meet, ramps down to the sides towards other levels and leaves a lone cell as it is */
		double PlateauCenter = 0.0;
		TArray<double, TInlineAllocator<6>> PlateauSides;
		TArray<double, TInlineAllocator<6>> PlateauCorners;

		/** By corner of the cell: the mean top of the three cells there, or the highest with EHexEdgeRule::HigherHolds */
		TArray<double, TInlineAllocator<6>> CornerHeights;

		/** Steps inside a group, in the center, by the middle of each side and by corner */
		double MassifCenter = 0.0;
		TArray<double, TInlineAllocator<6>> MassifSides;
		TArray<double, TInlineAllocator<6>> MassifCorners;

		/** Factor of the height above FPlacement::ScaleBase, in the center, by the middle of each side and by corner, blended as the massif */
		double ReliefCenter = 1.0;
		TArray<double, TInlineAllocator<6>> ReliefSides;
		TArray<double, TInlineAllocator<6>> ReliefCorners;

		/** Where the center of the piece moves, added to the directions of the unit sphere by the weight of the center, which is 0 on the sides */
		FVector3d CenterShift = FVector3d::ZeroVector;

		/** The power of the shape of the inside of this cell (FHexPlanetKit::SharpnessMin), and how far it turns in radians, both its own */
		double Sharpness = 1.0;
		/** For a lone cell, the power its whole height from the foot is raised to, below 1 so its low ground rises into a broad base that fills
		 *  the cell instead of falling to the ground; 0 for other cells */
		double LoneDome = 0.0;
		/** For a lone cell, the height of a dome over the whole cell, as a share of its peak above the foot, below which its ground never lies */
		double LoneBase = 0.0;

		/** A summit of a lone cell that takes a style of summits: its direction on the unit sphere, its width on it, and its height as a share of
		 *  the peak of the piece above its foot */
		struct FSummit
		{
			FVector3d Direction = FVector3d::ZeroVector;
			double Width = 0.1;
			double Height = 1.0;
		};
		TArray<FSummit, TInlineAllocator<3>> Summits;

		/** Share of its height above the side line that the piece under the summits keeps */
		double SummitBase = 1.0;

		/** How much the crest of this cell splits into two blades, 0 to 1 (FHexPlanetKit::SplitDepth) */
		double Split = 0.0;
		/** By side: how much of RidgeLift that side takes, the same from both cells; empty for all of it */
		TArray<double, TInlineAllocator<6>> RidgeWeights;
		double Twist = 0.0;
	};

	/** How a kit places its vertices, in world units */
	struct FPlacement
	{
		/** World units per mesh unit */
		double Scale = 1.0;
		double FootDepth = 30.0;
		double FadeHeight = 30.0;
		bool bTopFollowsCorners = true;
		double MassifStep = 0.0;
		double MassifRise = 0.0;
		/** Height above which the relief factor of the frame stretches a piece, in world units. Off for flat tiles */
		bool bRelief = false;
		double ScaleBase = 0.0;
		/** The wobble of the slopes below the plains (UHexPlanetConfig::CoastWobble): its height, the size of its noise, the share of its finer
		 *  noise, and the radii of the tops of the deep sea, the shallows and the plains, between which it acts */
		double Wobble = 0.0;
		double WobbleScale = 100.0;
		double WobbleDetail = 0.0;
		double WobbleTops[3] = {};
		/** The rise of the floor of the passages (UHexPlanetConfig::FloorNoise, GetFloorRise): its height, 0 for none, the size of its noise, the
		 *  share of its finer noise, how far below the top of the floor it fades out, and that top, the radius of the top of level 0 */
		double FloorRise = 0.0;
		double FloorRiseScale = 150.0;
		double FloorRiseDetail = 0.0;
		double FloorRiseFade = 20.0;
		double FloorTop = 0.0;
		/** The shape of the inside of a cell (FHexPlanetKit, Shape), in world units, off with a ShapeHeight of 0 */
		double ShapeHeight = 0.0;
		double CragHeight = 0.0;
		double CragScale = 40.0;
		/** The ridges to the neighbors on the same level (FHexPlanetKit::RidgeLift), in world units, and their half width as a share of a side */
		double RidgeLift = 0.0;
		double RidgeWidth = 0.3;
		/** The faces of high ground (FHexPlanetKit, Facets): the height of the ridged noise and its size in world units, and the radii where it
		 *  begins and is full */
		double JaggedHeight = 0.0;
		double JaggedScale = 30.0;
		double FacetLow = 0.0;
		double FacetHigh = 0.0;
		/** Gullies and ledges (FHexPlanetKit, Rock), in world units */
		double GullyHeight = 0.0;
		double GullyCount = 9.0;
		double GullyLength = 1.2;
		double LedgeStep = 0.0;
		double LedgeStrength = 0.0;
		/** The base and the split crests (FHexPlanetKit, Bulk): the lift in world units and its reach as a share of the peak, and the split as
		 *  shares of the peak and of the radius of a cell */
		double BulkLift = 0.0;
		double BulkReach = 0.3;
		double SplitDepth = 0.0;
		double SplitBlade = 0.0;
		double SplitWidth = 0.25;
		/** The plateau of cells with neighbors on their level (FHexPlanetKit, Plateau): its lift in world units, 0 for none, and how much of the
		 *  bumps above the top it swallows, 0 to 1 */
		double PlateauLift = 0.0;
		double PlateauFill = 0.0;
	};

	/** Share of the faces of high ground at a radius, 0 without them */
	double GetFacetShare(const FPlacement& Placement, double Height);

	/** One vertex of a piece, in the triangle of the hex that holds it */
	struct FPieceVertex
	{
		/** Direction of the side whose triangle holds it */
		int32 Sector = 0;
		/** Weights of the center, corner Sector and corner Sector + 1 of the regular hex. The center weight is negative past the side */
		double WCenter = 1.0;
		double WFrom = 0.0;
		double WTo = 0.0;
		/** Height in world units */
		double Z = 0.0;
		/** How far towards the sides it lies: 0 in the center and 1 on a side. 0 past the sides, which draw no grid line */
		float GridU = 0.0f;
		/** True on a side, where it meets the neighbor */
		bool bOnSide = false;
		/** UV of the mesh */
		FVector2f MeshUV = FVector2f::ZeroVector;
	};

	/** A piece read from its mesh, with vertices that share a position joined */
	struct FPiece
	{
		TArray<FPieceVertex> Vertices;
		TArray<int32> Triangles;
	};

	/** Returns for every corner of the grid the mean top of its three cells, or the highest of them with EHexEdgeRule::HigherHolds */
	TArray<double> FindCornerHeights(const FHexSphereGrid& Grid, TFunctionRef<double(int32)> GetTop, EHexEdgeRule Rule = EHexEdgeRule::Shared,
		const TSet<uint64>* LowSides = nullptr);

	/** The key of the side between two cells in a set of low sides: a side that lies at the top of the lower of its two cells, with both its
	 *  corners, whatever the rule, so the lower cell stays flat up to it and the higher one carries the whole slope. The ground before the
	 *  mouth of an entrance built into a mountain is such a side, as the developer asked on 2026-11-12 for ground that comes up to an entrance
	 *  evenly rather than rising to the mountain (AHexPlanet::FindEntranceSides) */
	inline uint64 GetSideKey(int32 One, int32 Other)
	{
		return (uint64(uint32(FMath::Min(One, Other))) << 32) | uint64(uint32(FMath::Max(One, Other)));
	}

	/** Returns the height of the middle of a side between two cells of these tops by a rule */
	inline double GetSideMiddle(double Top, double OtherTop, EHexEdgeRule Rule)
	{
		return Rule == EHexEdgeRule::HigherHolds ? FMath::Max(Top, OtherTop) : (Top + OtherTop) * 0.5;
	}

	/**
	 *  Builds the frame of a cell. MassifSteps holds for each cell how many steps it lies inside its group, and counts only between
	 *  cells of the same level. Relief holds the factor of the height of each cell (1 without it), which counts whatever the levels. CornerHeights
	 *  must come from FindCornerHeights by the same Rule
	 */
	FCellFrame BuildFrame(const FHexSphereGrid& Grid, int32 Cell, TFunctionRef<double(int32)> GetTop, TFunctionRef<int32(int32)> GetLevel,
		TConstArrayView<double> CornerHeights, TConstArrayView<int32> MassifSteps, TConstArrayView<float> Relief = {}, EHexEdgeRule Rule = EHexEdgeRule::Shared,
		const TSet<uint64>* LowSides = nullptr);

	/** Moves a height on a slope below the plains by the wobble of the coast: a pure function of the place, so cells that share a point agree */
	double ApplyWobble(const FPlacement& Placement, const FVector3d& Direction, double Height);

	/** Returns how far the floor of the passages rises in a direction, 0 to Height, by a noise of the place on the sphere of radius FloorTop
	 *  Scale across with Detail of a finer one; the walls of the underground take it too, so they stand on the floor */
	double GetFloorRise(const FVector3d& Direction, double FloorTop, double Height, double Scale, double Detail);

	/** Raises a height by the rise of the floor (FPlacement::FloorRise): all of it on the top of level 0 and fading out below it */
	double ApplyFloorRise(const FPlacement& Placement, const FVector3d& Direction, double Height);

	/** Height of a side: Middle in its middle, bending linearly to From at S = 0 and To at S = 1 */
	double GetSideHeight(double Middle, double From, double To, double S);

	/** Finds the triangle of the regular hex that holds a point, with a radius of one, and the weights in it */
	void FindSector(const FVector2d& Point, int32& OutSector, double& OutCenter, double& OutFrom, double& OutTo);

	/** Places a vertex of a piece: its triangle of the hex is side Side of the cell, between corners Side - 1 and Side */
	FVector3d PlaceVertex(const FHexSphereGrid& Grid, int32 Cell, const FCellFrame& Frame, const FPlacement& Placement,
		int32 Side, double WCenter, double WFrom, double WTo, double Z, double* OutCrevice = nullptr);

	/**
	 *  Reads a piece from a mesh with CPU access, scaled to world units. With Subdivisions above 0 it samples the mesh as a height field
	 *  on a triangle lattice with that many steps from its center to a corner, moving lattice points just past its outline onto it.
	 *  Returns false if the mesh has no data to read
	 */
	bool ReadPiece(UStaticMesh* Mesh, double Scale, double MeshRadius, int32 Subdivisions, FPiece& OutPiece);
}
