// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexTerrainTuning.generated.h"

/**
 *  Finer values of how HexMeshBuilder shapes the terrain, the coast, cliffs and rivers, held by DA_HexMap and copied into FHexMeshParams.
 *  Distances are in units of the world unless they say otherwise.
 */
USTRUCT(BlueprintType)
struct FHexTerrainTuning
{
	GENERATED_BODY()

	/** Octaves of the noise of the relief, each NoiseFrequencyStep times finer and half as strong */
	UPROPERTY(EditAnywhere, Category="Relief", meta=(ClampMin=1, ClampMax=8))
	int32 NoiseOctaves = 3;

	/** Not a whole number, because Perlin noise is zero at every whole coordinate and octaves would line up */
	UPROPERTY(EditAnywhere, Category="Relief", meta=(ClampMin=1.01))
	double NoiseFrequencyStep = 2.1;

	/** Wavelength of the largest features of the relief and of the warp of hex borders, in hex radii */
	UPROPERTY(EditAnywhere, Category="Relief", meta=(ClampMin=0.01))
	double ReliefWavelength = 1.6;

	UPROPERTY(EditAnywhere, Category="Relief", meta=(ClampMin=0.01))
	double WarpWavelength = 2.5;

	/** Part of the hex radius that a notch mesh reaches from its corner towards the center of the lower hex */
	UPROPERTY(EditAnywhere, Category="Relief", meta=(ClampMin=0, ClampMax=1))
	double NotchDepth = 0.25;

	/** How far the coast moves out from the sides of the hexes, at least, and how much noise adds to that */
	UPROPERTY(EditAnywhere, Category="Coast", meta=(ClampMin=0))
	double CoastMinOffset = 15.0;

	UPROPERTY(EditAnywhere, Category="Coast", meta=(ClampMin=0))
	double CoastOffsetNoise = 14.0;

	/** How far long waves in the coast reach further out, which makes headlands and bays */
	UPROPERTY(EditAnywhere, Category="Coast", meta=(ClampMin=0))
	double CoastBayDepth = 34.0;

	/** Wavelength of those headlands and bays */
	UPROPERTY(EditAnywhere, Category="Coast", meta=(ClampMin=1))
	double CoastBayScale = 310.0;

	/** Wavelengths of the large and the fine noise that move the coast out, and the share of the fine one */
	UPROPERTY(EditAnywhere, Category="Coast", meta=(ClampMin=1))
	double CoastNoiseScale = 75.0;

	UPROPERTY(EditAnywhere, Category="Coast", meta=(ClampMin=1))
	double CoastNoiseFineScale = 27.0;

	UPROPERTY(EditAnywhere, Category="Coast", meta=(ClampMin=0))
	double CoastNoiseFine = 0.4;

	/** Distance between cuts of a cliff */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=1))
	double CliffRingSpacing = 8.0;

	/** How far inland from the coast the terrain starts to fall, so that it is behind the cliff all the way down */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	double CliffInlandRamp = 8.0;

	/** How far inland from that the top of a cliff lies over the terrain. A triangle of the terrain can start to fall a whole lattice step before that */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	double CliffLipWidth = 22.0;

	/** How far the terrain stays below the top of a cliff */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	double CliffLipClearance = 5.0;

	/** How far the top of a cliff reaches out over its face, and how far it stands above the land */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	double CliffLipOverhang = 3.0;

	UPROPERTY(EditAnywhere, Category="Cliffs")
	double CliffLipRise = 1.0;

	/** How many cuts along a coast each way can hold a bend that the top of a cliff must not cross */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	int32 CliffLipBendRings = 40;

	/** Distance over which the land behind a cliff returns to its own height */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	double CliffLandBlend = 45.0;

	/** Length of coast over which a cliff grows from nothing where it ends in a beach */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	double CliffEndLength = 50.0;

	/** How far a cliff sinks where it fades out, so the terrain always covers it */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	double CliffFadeSink = 40.0;

	/** Wavelength of the noise that chooses between ledges, a wall and a slope along a coast, and the noise where ledges and slopes start and are full */
	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=1))
	double CliffKindScale = 170.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=0))
	double CliffKindStart = 0.05;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=0))
	double CliffKindEnd = 0.25;

	/** A wall: how far its top stands out, and how far it leans back down to its foot, as a wall straight down would hardly show from above */
	UPROPERTY(EditAnywhere, Category="Cliffs|Shape")
	double CliffWallTop = 6.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape")
	double CliffWallLean = 30.0;

	/** Ledges: how many shelves of rock, and how far each reaches out */
	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=1))
	double CliffLedges = 4.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape")
	double CliffLedgeStep = 15.0;

	/** A slope: how far its top stands out, how far it reaches at its foot and how it curves, where more is steeper at the top */
	UPROPERTY(EditAnywhere, Category="Cliffs|Shape")
	double CliffSlopeTop = 5.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape")
	double CliffSlopeReach = 70.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=0.1))
	double CliffSlopePower = 1.3;

	/** Wavelengths and heights of the bumps of rock on the face, large and fine */
	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=1))
	double CliffRockScale = 13.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=0))
	double CliffRockHeight = 9.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=1))
	double CliffRockFineScale = 5.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=0))
	double CliffRockFineHeight = 3.0;

	/** Wavelength and height of the layers of a slope */
	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=1))
	double CliffStrataScale = 21.0;

	UPROPERTY(EditAnywhere, Category="Cliffs|Shape", meta=(ClampMin=0))
	double CliffStrataHeight = 6.0;

	/** How far inside the side of a joined mesh towards water its slope comes out of the water, roughly, for the shore field */
	UPROPERTY(EditAnywhere, Category="Joined Meshes", meta=(ClampMin=0))
	double JoinedWaterInset = 5.0;

	/** How far inside the side of a joined mesh towards water the shore field counts it as a cliff, for the foam that breaks against it */
	UPROPERTY(EditAnywhere, Category="Joined Meshes", meta=(ClampMin=0))
	double JoinedShoreFoamReach = 30.0;

	/** Depth of the seabed under the water at the side of a joined mesh, and how quickly it falls away from there, where more is steeper near the mesh */
	UPROPERTY(EditAnywhere, Category="Joined Meshes", meta=(ClampMin=0))
	double JoinedShoreDepth = 4.0;

	UPROPERTY(EditAnywhere, Category="Joined Meshes", meta=(ClampMin=0.1))
	double JoinedSeabedCurve = 1.6;

	/** Smallest size of a pixel of the shore field, and its largest number of pixels across, which a large map reaches */
	UPROPERTY(EditAnywhere, Category="Shore Field", meta=(ClampMin=1))
	double ShoreFieldMinStep = 12.5;

	UPROPERTY(EditAnywhere, Category="Shore Field", meta=(ClampMin=16))
	int32 ShoreFieldMaxSize = 2048;

	/** How far the distance is blurred for the fronts of waves, so that they do not show every corner of the coast */
	UPROPERTY(EditAnywhere, Category="Shore Field", meta=(ClampMin=0))
	double ShoreFieldSoftBlur = 30.0;

	/** How far the depth of the seabed is blurred, which leaves no trace of the lattice of the terrain */
	UPROPERTY(EditAnywhere, Category="Shore Field", meta=(ClampMin=0))
	double ShoreFieldDeepBlur = 20.0;

	/** Distance between points of a river, how often its corners are cut, and the wavelength of its windings in hex radii */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=1))
	double RiverPointSpacing = 10.0;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	int32 RiverSmoothPasses = 3;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0.01))
	double RiverMeanderWavelength = 1.3;

	/** How far a river goes on into the water past the side of its mouth, in hex radii */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	double RiverMouthReach = 0.5;

	/** Length over which a river narrows towards its source and where it ends in another, and its width there as a part of its own */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	double RiverTaperLength = 70.0;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=1))
	double RiverTaperWidth = 0.3;

	/** How far the foot of a bank lies inside the edge of the water, under it */
	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=0))
	double RiverBankFoot = 1.5;

	/** Distance past the top of a bank over which the land goes from the height of the bank back to its own */
	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=0))
	double RiverBankSettle = 30.0;

	/**
	 *  How much of the bank color, which M_HexTerrain draws from the alpha of vertex colors, the top of a bank has, and the distance past it over which
	 *  that falls to nothing, from which M_HexTerrain finds the edge of the sand on the land, so its parameters must match
	 */
	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=0, ClampMax=1))
	double RiverBankTopColor = 0.6;

	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=0.01))
	double RiverBankColorReach = 20.0;

	/** Height over which the top and the foot of a bank round off into the land and the floor */
	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=0))
	double RiverBankRounding = 2.0;

	/** How many times a triangle of the lattice can be halved where a river passes through it, as a power of two, so a bank falls sharply */
	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=1, ClampMax=64))
	int32 RiverSplit = 8;

	/**
	 *  How near the top or the foot of a bank, as a part of a step of the fine lattice, a point of a split triangle moves onto it: a little more than
	 *  half, so an edge that crosses the line always has an end near enough to move onto it, and no triangle reaches across it
	 */
	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=0.5, ClampMax=1))
	double RiverBankSnap = 0.52;

	/** Distance over which a point of a split triangle measures its normal, so it takes the rounded slope of the bank right where it lies */
	UPROPERTY(EditAnywhere, Category="Rivers|Banks", meta=(ClampMin=0.001))
	double RiverBankNormalStep = 0.05;

	/** Distance along a river over which the height of the ground is averaged for its water, so bumps of the terrain do not drag it down */
	UPROPERTY(EditAnywhere, Category="Rivers|Water", meta=(ClampMin=0))
	double RiverSurfaceSmoothing = 50.0;

	/** Number of strips across the water of a river, which is flat across, so one is enough */
	UPROPERTY(EditAnywhere, Category="Rivers|Water", meta=(ClampMin=1))
	int32 RiverWaterStrips = 1;

	/** Fall of the water per distance along a river where rapids start and where they are wildest */
	UPROPERTY(EditAnywhere, Category="Rivers|Water", meta=(ClampMin=0))
	double RiverRapidsStart = 0.05;

	UPROPERTY(EditAnywhere, Category="Rivers|Water", meta=(ClampMin=0))
	double RiverRapidsFull = 0.6;

	/** Height above the sea below which a river foams at its mouth */
	UPROPERTY(EditAnywhere, Category="Rivers|Water", meta=(ClampMin=0))
	double RiverMouthFoamHeight = 8.0;

	/** Length over which the water of a river fades in at its source and out where it ends */
	UPROPERTY(EditAnywhere, Category="Rivers|Water", meta=(ClampMin=0))
	double RiverWaterFade = 15.0;

	/** How far the water of a river lies under the sea, where the sea covers it */
	UPROPERTY(EditAnywhere, Category="Rivers|Water", meta=(ClampMin=0))
	double RiverUnderSea = 1.0;
};
