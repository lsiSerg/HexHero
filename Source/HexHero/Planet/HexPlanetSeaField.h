// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FHexSphereGrid;

/**
 *  What the water of the planet needs to know about the sea under it, smooth over the sphere and stored in a cubemap,
 *  which M_HexPlanetWater samples by the direction from the center of the planet.
 *  Red is the depth of the seabed / 100, blended between the centers of cells, as the shore field of the flat map has it in its alpha,
 *  and blue is how frozen the sea is, 1 under ice. Alpha is the signed distance to the coast, (distance + 50) / 450, negative on land, a soft minimum over the land cells around,
 *  each taken as a disc around its center (FShoreParams), which grows evenly away from the coast even in the middle of a cell and further
 *  out follows the run of the coast rather than each cape, and green is how much its lines bend around land (their curvature times the
 *  distance to the center of the nearest land cell, 1 around a lone cape and about 0 along a straight coast),
 *  so the breakers of the water follow it and fade where it wraps around a cape. Near the real coast, which winds across the cells, the
 *  distance moves to be 0 just where the ground crosses the sea (FShoreParams::CoastFade).
 *  BuildPixels also writes a second cubemap after it, the shore field: the coast is cut into stretches of one or two land cells, and red is the
 *  phase at which the stretch nearest a texel breaks and green how far the texel lies from the end of that stretch / 256 (from the bisector
 *  towards the nearest land cell of another stretch), so the breakers come one by one along the coast and fade out at the ends of each, and blue
 *  how far it lies along the coast from the middle of the stretch / 256, so a breaker is no longer than it should be where its wedge of sea fans out,
 *  and alpha how fast the distance to the coast grows (the length of its gradient), which the breakers divide out to roll at an even speed.
 */
namespace HexPlanetSeaField
{
	/** Values of one cell, in world units */
	struct FCellValues
	{
		float Depth = 0.0f;
		/** 1 where the sea is frozen */
		float Frozen = 0.0f;
		/** Whether the cell is land, for the distance in alpha */
		bool bLand = false;
	};

	/** How the distance to land in alpha is measured: the radius of the planet and of the disc of a land cell, in world units, and the softness
	 *  of the minimum over the land cells around (UHexPlanetConfig::ShoreSoftness, 0 for the nearest) */
	struct FShoreParams
	{
		double Radius = 1.0;
		double LandRadius = 0.0;
		double Softness = 0.0;
		/** Chance that a land cell on the coast takes a neighbor on the coast into its stretch (UHexPlanetConfig::ShoreStretchJoin) */
		double StretchJoin = 0.0;
		/** How the distance moves onto the real coast near it (UHexPlanetConfig::ShoreCoastFade, ShoreCoastBlend) */
		double CoastFade = 200.0;
		double CoastBlend = 15.0;
	};

	/** Returns the direction from the center of a cubemap to the middle of a texel, as a shader samples it */
	FVector3d GetTexelDirection(int32 Face, int32 X, int32 Y, int32 Size);

	/** Fills the six faces of a cubemap of Size x Size, one after another, from the values of the cells, followed by the six faces of the shore
	 *  field; CoastPoints are where the ground crosses the sea (HexPlanetSurface::FBuild::Coasts), onto which the distance moves near them */
	TArray<FColor> BuildPixels(const FHexSphereGrid& Grid, TConstArrayView<FCellValues> Values, int32 Size, const FShoreParams& Shore,
		TConstArrayView<FVector3f> CoastPoints = {});
}
