// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  One cell of the hex sphere: a hexagon, or one of the 12 pentagons at the vertices of the icosahedron.
 *  Seen from outside the sphere, its neighbors and corners go counterclockwise in the sense of the math cross product,
 *  and corner K lies between neighbor K and neighbor K + 1, so the side towards neighbor K runs from corner K - 1 to corner K.
 */
struct FHexSphereCell
{
	/** Center on the unit sphere */
	FVector3d Center = FVector3d::ZeroVector;

	/** Neighboring cells, one per side */
	TArray<int32, TInlineAllocator<6>> Neighbors;

	/** Corners as indices into FHexSphereGrid::GetCorners, shared by the three cells that meet there */
	TArray<int32, TInlineAllocator<6>> Corners;

	/** Face of the icosahedron that holds the center, from 0 to 19 */
	int32 Face = 0;

	/** Chunk that holds the center: one of the triangles each face is split into (FHexSphereGrid::Build), so a mesh per chunk can be culled */
	int32 Chunk = 0;

	/** Returns true for the 12 cells with five sides */
	bool IsPentagon() const { return Neighbors.Num() == 5; }
};

/**
 *  Hex grid on a sphere, a Goldberg polyhedron: each face of an icosahedron is split into Frequency steps along its sides,
 *  and the cells are the duals of that triangle lattice, so there are 10 * Frequency^2 + 2 cells, 12 of them pentagons.
 *  The points of a face blend its vertices by sines, which keeps the largest hex within about 1.2 of the smallest, where a straight
 *  projection gives about 1.9. Relaxing the lattice afterwards, by springs or by moving to the middle of the neighbors, made it worse.
 *  The first 12 cells are the pentagons.
 */
class FHexSphereGrid
{
public:

	/**
	 *  Builds the grid. Each face of the icosahedron is also split into chunks, triangles about ChunkSteps lattice steps along a side,
	 *  so a mesh per chunk is small enough to be culled on its own; 0 or less makes each face one chunk
	 */
	static FHexSphereGrid Build(int32 Frequency, int32 ChunkSteps = 0);

	/** Returns how many chunks hold the cells, each at least one */
	int32 GetNumChunks() const { return NumChunks; }

	/** Returns how many cells a grid of this frequency has */
	static int32 CountCells(int32 Frequency) { return 10 * Frequency * Frequency + 2; }

	/** Returns the distance along a ray to where it enters a sphere around the origin, or a negative value if it misses */
	static double IntersectSphere(const FVector3d& Origin, const FVector3d& Direction, double SphereRadius);

	/** Returns the number of steps along each side of the icosahedron */
	int32 GetFrequency() const { return Frequency; }

	/** Returns all cells */
	const TArray<FHexSphereCell>& GetCells() const { return Cells; }

	/** Returns the corners of all cells on the unit sphere, one per triangle of the lattice */
	const TArray<FVector3d>& GetCorners() const { return Corners; }

	/** Returns the cell whose center is nearest to a direction from the center of the sphere. A cell near the answer makes it faster */
	int32 FindCell(const FVector3d& Direction, int32 StartCell = INDEX_NONE) const;

	/**
	 *  Finds the triangle of cell centers that holds a direction, and the weights of its three cells, which add up to 1,
	 *  so a value of the cells blends smoothly over the sphere without showing the hexes. Returns the cell nearest to the direction
	 */
	int32 FindBlend(const FVector3d& Direction, int32 StartCell, int32 (&OutCells)[3], double (&OutWeights)[3]) const;

	/** Returns the area of a cell on the unit sphere, measured on the flat triangles from its center to its corners */
	double GetCellArea(int32 Cell) const;

	/** Returns the longest side of a cell divided by its shortest */
	double GetCellSideRatio(int32 Cell) const;

private:

	/** Number of steps along each side of the icosahedron */
	int32 Frequency = 0;

	/** Number of chunks */
	int32 NumChunks = 0;

	/** All cells */
	TArray<FHexSphereCell> Cells;

	/** Corners of all cells */
	TArray<FVector3d> Corners;
};
