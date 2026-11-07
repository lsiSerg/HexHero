// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexCoord.h"
#include "HexTerrainTuning.h"

struct FHexMapData;

/** Vertex data of one part of the terrain. The surface shares its vertices, so it is shaded smoothly */
struct FHexMeshData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;

	/** Texture coordinates, which only some meshes have */
	TArray<FVector2D> UVs;
};

/** Sizes that control the shape of the terrain */
struct FHexMeshParams
{
	float HexRadius = 100.0f;
	float LevelHeight = 40.0f;
	int32 Subdivisions = 6;
	float BlendPower = 3.0f;
	float WarpDistance = 15.0f;
	float BottomHeight = -200.0f;

	/** Height of the water surface */
	float WaterHeight = -15.0f;

	/** Smallest difference in levels between land and water that makes a cliff. Zero means no cliffs */
	int32 CliffMinDifference = 0;

	/** How far the terrain under the water lies below the water next to a cliff */
	float CliffSeabedDepth = 25.0f;

	/** Height that the bottom of a cliff reaches */
	float CliffBottomHeight = -60.0f;

	/** How far the terrain around a hex with a joined mesh is pulled to its foot. Up to HexRadius, as only neighboring hexes are checked */
	float FootBlendDistance = 60.0f;

	/** Width of the water of every river, which bridges are made for */
	float RiverWidth = 28.0f;

	/** Depth of the floor of a river below its water */
	float RiverDepth = 4.0f;

	/** How far the top of the bank lies above the water, which is the ground averaged along the river, and how far its face leans back */
	float RiverBankHeight = 3.0f;
	float RiverBankLean = 3.0f;

	/** How far a river winds away from the straight line between hex centers */
	float RiverMeander = 18.0f;

	/** Finer values of the terrain, the coast, cliffs and rivers, from DA_HexMap */
	FHexTerrainTuning Tuning;
};

/** How the surface of one level looks */
struct FHexLevelSurface
{
	FLinearColor Color = FLinearColor::Black;
	float BumpHeight = 0.0f;
	float RidgeHeight = 0.0f;

	/** If true, a hex of this level with only such neighbors has no terrain */
	bool bHidesTerrain = false;

	/** If true, a joined mesh covers hexes of this level and the terrain meets the edge of that mesh */
	bool bHasJoinedMesh = false;

	/** Height of the edge of the joined mesh in the world */
	float FootHeight = 0.0f;

	/** Height of the top of a cliff at the edge of this level, in the world */
	float CliffTop = 0.0f;
};

/** One cut across a cliff, at one place along the line of the coast */
struct FHexCliffRing
{
	FVector2D Position = FVector2D::ZeroVector;

	/** Horizontal direction towards the water */
	FVector2D Outward = FVector2D::ZeroVector;

	/** Height of the top of the cliff */
	double Top = 0.0;

	/** Height of the land behind the cliff. The coast lies out over hexes of water, and what is inland of it must not fall to their height */
	double Ground = 0.0;

	/** How much of the cliff is here, which falls to zero where it ends in a beach */
	double Strength = 1.0;

	/** Map file column and row of the hex of land that this part of the coast belongs to, which chooses its chunk */
	FIntPoint Cell = FIntPoint::ZeroValue;
};

/** One unbroken line of cliffs along a coast, with its mesh as cuts of RowsPerRing vertices each */
struct FHexCliffChain
{
	TArray<FHexCliffRing> Rings;
	bool bClosed = false;
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;
};

/**
 *  All cliffs of a map. The coast runs along the sides of hexes between land and water, but it is smoothed and moved out to sea with noise,
 *  and the terrain follows it, so it does not show the hexes. Every cut along it blends between ledges, a leaning wall and a gentle slope, which all show from above.
 */
struct FHexCliffData
{
	static constexpr int32 RowsPerRing = 16;

	TArray<FHexCliffChain> Chains;

	/** The chain and ring of each part of the coast near a hex */
	TMap<FHexCoord, TArray<FIntPoint>> Nearby;
};

/** One place along the middle of a river */
struct FHexRiverPoint
{
	FVector2D Position = FVector2D::ZeroVector;

	/** Width of the water */
	double Width = 0.0;

	/** Height of the water, which never rises downstream */
	double Surface = 0.0;
};

/** One unbroken line of a river, from a source to its mouth or to where it flows into a larger river */
struct FHexRiverLine
{
	TArray<FHexRiverPoint> Points;

	/** True if it ends in water and not in another river */
	bool bEndsAtMouth = false;
};

/**
 *  All rivers of a map. A line runs through the centers of the hexes of a river and the middles of their sides, smoothed and winding with noise,
 *  and the terrain is cut along it into a bed with banks, which only ever lowers the land.
 */
struct FHexRiverData
{
	TArray<FHexRiverLine> Lines;

	/** The line and segment of each part of a river near a hex, where a segment starts at its point */
	TMap<FHexCoord, TArray<FIntPoint>> Nearby;
};

/**
 *  What the water knows about the coast, one pixel for a small square of the map: red is the distance to land up to NearRange and green the same distance
 *  blurred, up to FarRange, whose lines are the fronts of waves. Blue is how near a cliff it is, and alpha the blurred depth of the seabed below the water, up to DepthRange.
 *  HeightPixels holds how far the ground lies above the water, unblurred, from -HeightRange at 0 to HeightRange at 65535, so that bilinear filtering
 *  finds the water's edge between pixels, and 16 bits keep its lines smooth over flat ground. M_HexWater repeats the four ranges, so change them together.
 */
struct FHexShoreField
{
	static constexpr double NearRange = 200.0;
	static constexpr double FarRange = 400.0;
	static constexpr double DepthRange = 100.0;
	static constexpr double HeightRange = 24.0;

	int32 Width = 0;
	int32 Height = 0;
	double Step = 0.0;
	FVector2D Origin = FVector2D::ZeroVector;
	TArray<FColor> Pixels;
	TArray<uint16> HeightPixels;
};

/**
 *  The height of the terrain at the points of its lattice, as M_HexMountain reads it to lay joined meshes on the terrain, which repeats GetIndex.
 *  Odd rows of the lattice lean, so each row is shifted to keep the map a rectangle.
 */
struct FHexTerrainHeights
{
	int32 Width = 0;
	int32 Height = 0;
	FIntPoint Origin = FIntPoint::ZeroValue;
	TArray<float> Heights;

	static int32 GetRowShift(int32 Y)
	{
		return (Y - (Y & 1)) / 2;
	}

	int32 GetIndex(const FIntPoint& Point) const
	{
		return (Point.Y - Origin.Y) * Width + Point.X + GetRowShift(Point.Y) - Origin.X;
	}
};

/** Which mesh joins a set of neighbors and how it is turned */
struct FHexJoinedPiece
{
	/** Mask of the mesh before it is turned */
	int32 MeshMask = 0;

	/** Turns of 60 degrees towards the next direction */
	int32 Turns = 0;
};

/**
 *  Builds a continuous terrain surface.
 *  The height and color of the surface depend only on the horizontal position, and every vertex lies on one triangular
 *  lattice that covers the whole map. Separately built parts of the map therefore meet exactly, with the same normals.
 */
namespace HexMeshBuilder
{
	/** Builds the hexes in a range of map file columns and rows. The max values are not included */
	void BuildRange(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces,
		int32 MinColumn, int32 MinRow, int32 MaxColumn, int32 MaxRow, FHexMeshData& OutMesh, const FHexCliffData* Cliffs = nullptr, const FHexRiverData* Rivers = nullptr);

	/** Finds the cliffs of a map and builds their meshes */
	void BuildCliffs(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, FHexCliffData& OutCliffs);

	/** Finds the lines of the rivers of a map and the height of their water from the terrain they cross, which already has its cliffs */
	void BuildRivers(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs, FHexRiverData& OutRivers);

	/**
	 *  Builds the water of all rivers as ribbons along their lines at the height of their water, a little wider than the river, so its edge goes under the face of the bank that the terrain
	 *  adds along it. U goes across from -1 to 1 and V along in world units. The vertex colors are what M_HexRiver draws:
	 *  red is how fast the water falls, green how near the sea it is, and alpha fades it in at a source and out where it meets another river or under the sea.
	 */
	void BuildRiverWater(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs,
		const FHexRiverData& Rivers, FHexMeshData& OutMesh);

	/** Finds how many hexes each hex with a joined mesh lies inside its group: 0 next to anything else or the edge of the map, then 1, 2 and so on */
	TArray<uint8> BuildMassifDepths(const FHexMapData& Map, const TMap<int8, FHexLevelSurface>& LevelSurfaces);

	/** Finds the height of the terrain at its vertices in and around all hexes with joined meshes */
	void BuildTerrainHeights(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs, FHexTerrainHeights& OutHeights, const FHexRiverData* Rivers = nullptr);

	/** Finds the distance from every place on the water to the real edge of the land, which follows the terrain and the cliffs and not the sides of hexes */
	void BuildShoreField(const FHexMapData& Map, const FHexMeshParams& Params, const TMap<int8, FHexLevelSurface>& LevelSurfaces, const FHexCliffData* Cliffs, const FBox2D& Bounds, FHexShoreField& OutField, const FHexRiverData* Rivers = nullptr);

	/** Adds the parts of cliffs that belong to the hexes in a range of map file columns and rows */
	void AddCliffRange(const FHexCliffData& Cliffs, int32 MinColumn, int32 MinRow, int32 MaxColumn, int32 MaxRow, FHexMeshData& OutMesh);

	/**
	 *  Returns where a ready-made mesh stands on a hex. It fills the given width and height whatever its original size and pivot,
	 *  with its middle over the center of the hex and its lowest point at the given height. Each hex has its own fixed turn.
	 */
	FTransform GetFeatureTransform(const FHexCoord& Hex, const FBox& MeshBounds, float HexRadius, float Width, float Height, double BottomZ);

	/**
	 *  Finds the mesh for a set of neighbors that it joins. Bit N of a mask means the neighbor in direction N of FHexCoord.
	 *  Turned copies of one mesh cover all masks that differ only by a turn, so 64 masks need 14 meshes.
	 */
	FHexJoinedPiece GetJoinedPiece(int32 NeighborMask);

	/** Returns turns of 60 degrees that leave the sides of a joined mesh where they are, chosen by the hex, as a mesh with a symmetric set of neighbors looks different turned */
	int32 GetJoinedExtraTurns(int32 NeighborMask, const FHexCoord& Hex);

	/** Returns where a joined mesh stands on a hex. It keeps its pivot on the center of the hex, as neighbors only meet exactly that way */
	FTransform GetJoinedTransform(const FHexCoord& Hex, int32 Turns, float HexRadius, float MeshRadius, double BaseZ);

	/** Returns true if a hex with a joined mesh has a notch mesh after direction N: the neighbor in that direction has the same mesh level and the next neighbor does not */
	bool HasJoinedNotch(int32 NeighborMask, int32 Direction);

	/** Returns where the notch mesh after a direction of a hex stands. Its pivot is on the shared corner and it points to the center of the lower hex */
	FTransform GetJoinedNotchTransform(const FHexCoord& Hex, int32 Direction, float HexRadius, float MeshRadius, double BaseZ);

	/** Returns true if a side between two levels has a cliff: one is land, the other water and they differ by at least the given number of levels */
	bool IsCliffSide(int32 LevelA, int32 LevelB, int32 MinDifference);

	/**
	 *  Finds the first hex of a map that a ray going down reaches. It compares the ray with the height of each level or the water
	 *  above it, not with the real surface, so the top of a mountain counts as the hex behind it. TopHeight is the height of the highest level.
	 */
	bool FindHexAlongRay(const FHexMapData& Map, float HexRadius, float LevelHeight, float WaterHeight, double TopHeight,
		const FVector& RayOrigin, const FVector& RayDirection, FHexCoord& OutHex);
}
