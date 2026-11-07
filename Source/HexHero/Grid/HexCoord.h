// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexCoord.generated.h"

/**
 *  Axial coordinate of a pointy-top hex.
 *  Map files store hexes as "odd-r" offset columns and rows, where odd rows are shifted half a hex to the right.
 *  Directions and corners are numbered clockwise as seen from above, with direction 0 pointing north-east
 *  and corner 0 pointing north. Direction D is the edge between corners D and D + 1.
 */
USTRUCT(BlueprintType)
struct FHexCoord
{
	GENERATED_BODY()

	/** Axial column */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hex")
	int32 Q = 0;

	/** Axial row */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hex")
	int32 R = 0;

	FHexCoord() = default;
	FHexCoord(int32 InQ, int32 InR) : Q(InQ), R(InR) {}

	/** Number of neighbors and corners of a hex */
	static constexpr int32 NumDirections = 6;

	/** Converts a map file column and row to an axial coordinate */
	static FHexCoord FromOffset(int32 Column, int32 Row);

	/** Returns the map file column of this coordinate */
	int32 GetColumn() const;

	/** Returns the map file row of this coordinate */
	int32 GetRow() const { return R; }

	/** Returns the neighbor in the given direction. The direction wraps around */
	FHexCoord GetNeighbor(int32 Direction) const;

	/** Returns the number of steps between two hexes */
	int32 DistanceTo(const FHexCoord& Other) const;

	bool operator==(const FHexCoord& Other) const { return Q == Other.Q && R == Other.R; }
	bool operator!=(const FHexCoord& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FHexCoord& Coord) { return HashCombine(GetTypeHash(Coord.Q), GetTypeHash(Coord.R)); }
};

/**
 *  Converts between hex coordinates and positions on the XY plane.
 *  The first map row is at X = 0 and later rows go towards -X, so north on the map is +X in the world.
 *  Columns go towards +Y.
 */
struct FHexLayout
{
	/** Distance from the center of a hex to its corners */
	float Radius = 100.0f;

	explicit FHexLayout(float InRadius) : Radius(InRadius) {}

	/** Returns the center of a hex */
	FVector2D HexToWorld(const FHexCoord& Coord) const;

	/** Returns the hex that contains a position */
	FHexCoord WorldToHex(const FVector2D& Position) const;

	/** Returns the offset of a corner from the center of a hex with a radius of one. The corner wraps around */
	static FVector2D GetCornerDirection(int32 Corner);
};
