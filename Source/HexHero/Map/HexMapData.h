// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexCoord.h"

/**
 *  River in a hex. It runs through the center of the hex, in from the sides of its inflows and out through the side of its outflow.
 */
struct FHexRiver
{
	/** Direction of the side the river leaves through, towards the next river hex or, at its mouth, the water. INDEX_NONE without a river */
	int8 Outflow = INDEX_NONE;

	/** Bit 1 << Direction for the side of each river hex that flows into this one */
	uint8 InflowMask = 0;

	/** True if the river flows into water from this hex */
	bool bMouth = false;

	/** Number of river hexes whose water flows through this one, itself included, which makes the river wider downstream */
	int32 Size = 0;

	/** Index of the river path that flows on from this hex */
	int32 Path = INDEX_NONE;

	bool HasRiver() const { return Outflow != INDEX_NONE; }
};

/**
 *  One river as rivers.json describes it: its hexes in the order it flows through them, from its source to the hex it ends in. It ends at the water next to
 *  its last hex, or in another river whose hexes hold its last hex, which makes it a tributary.
 */
struct FHexRiverPath
{
	FString Name;
	TArray<FHexCoord> Hexes;

	/** How much it winds, as a part of the usual winding, and the seed of that noise. Every river has the same width, so bridges fit any of them */
	float Meander = 1.0f;
	int32 Seed = 0;

	/** Index of the river it flows into, or INDEX_NONE if it ends at water */
	int32 Joins = INDEX_NONE;
};

/**
 *  Terrain level, biome and river of every hex on a map, stored row by row in map file order.
 */
struct FHexMapData
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<int8> Levels;

	/** Indices into the biomes that the map was loaded with */
	TArray<uint8> Biomes;

	/** Rivers of every hex, or empty on a map without rivers */
	TArray<FHexRiver> Rivers;

	/** The rivers as their file describes them, from which Rivers comes */
	TArray<FHexRiverPath> RiverPaths;

	/** Returns the river of a hex, which has none if the map has no rivers. The hex must be on the map */
	FHexRiver GetRiver(const FHexCoord& Coord) const
	{
		return Rivers.IsEmpty() ? FHexRiver() : Rivers[Coord.GetRow() * Width + Coord.GetColumn()];
	}

	/** Returns true if a map file column and row are on the map */
	bool Contains(int32 Column, int32 Row) const
	{
		return Column >= 0 && Row >= 0 && Column < Width && Row < Height;
	}

	/** Returns true if a hex is on the map */
	bool Contains(const FHexCoord& Coord) const
	{
		return Contains(Coord.GetColumn(), Coord.GetRow());
	}

	/** Returns the level of a map file column and row. They must be on the map */
	int8 GetLevel(int32 Column, int32 Row) const
	{
		return Levels[Row * Width + Column];
	}

	/** Returns the level of a hex. It must be on the map */
	int8 GetLevel(const FHexCoord& Coord) const
	{
		return GetLevel(Coord.GetColumn(), Coord.GetRow());
	}

	/** Returns the biome index of a hex. It must be on the map */
	uint8 GetBiome(const FHexCoord& Coord) const
	{
		return Biomes[Coord.GetRow() * Width + Coord.GetColumn()];
	}
};
