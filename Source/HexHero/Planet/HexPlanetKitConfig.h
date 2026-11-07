// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexLevelConfig.h"
#include "HexPlanetKitConfig.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 *  Pieces of one level of the planet, one per set of neighbors that are not lower, chosen and turned as HexMeshBuilder::GetJoinedPiece does.
 *  A piece is modelled on a regular hex with its top at 0 and its foot at -FootDepth, and HexPlanetKit::PlaceVertex bends it onto its cell
 *  and moves what lies between the foot and FootDepth - FadeHeight onto the ground of its neighbors. Its meshes need CPU access.
 */
USTRUCT(BlueprintType)
struct FHexPlanetKit
{
	GENERATED_BODY()

	/** Level of the cells that use these pieces, as AHexPlanet::GetCellLevel gives it */
	UPROPERTY(EditAnywhere, Category="Kit")
	int32 Level = 0;

	/** Distance from the center of a piece to its corners, in mesh units */
	UPROPERTY(EditAnywhere, Category="Kit", meta=(ClampMin=1))
	float MeshRadius = 100.0f;

	/** How far below its top the foot of a piece lies, in mesh units */
	UPROPERTY(EditAnywhere, Category="Kit", meta=(ClampMin=0.01))
	float FootDepth = 30.0f;

	/** How far above the foot a vertex still moves towards the ground, in mesh units. It must equal FootDepth unless no neighbor can be higher, or sides break */
	UPROPERTY(EditAnywhere, Category="Kit", meta=(ClampMin=0.01))
	float FadeHeight = 30.0f;

	/**
	 *  If above 0, a piece is sampled as a height field on a triangle lattice with this many steps from its center to a corner, which must be even,
	 *  so a detailed mesh costs fewer triangles on the planet. Neighbors still match, as they sample their shared sides at the same places
	 */
	UPROPERTY(EditAnywhere, Category="Kit", meta=(ClampMin=0))
	int32 Subdivisions = 0;

	/** If true, the top bends down to the low corners of the cell, which land needs, as its sides lie at 0. Mountains model their corners themselves */
	UPROPERTY(EditAnywhere, Category="Kit")
	bool bTopFollowsCorners = true;

	/** How much each step inside a group stretches the piece above its foot, as JoinedMeshMassifStep of the flat map */
	UPROPERTY(EditAnywhere, Category="Kit|Massif", meta=(ClampMin=0))
	float MassifStep = 0.0f;

	/** How far each step inside a group raises the piece, in mesh units */
	UPROPERTY(EditAnywhere, Category="Kit|Massif", meta=(ClampMin=0))
	float MassifRise = 0.0f;

	/** Steps inside a group that still count */
	UPROPERTY(EditAnywhere, Category="Kit|Massif", meta=(ClampMin=0))
	int32 MassifMaxSteps = 0;

	/** If true, a cell may also take its piece mirrored, chosen by a hash of the cell, which doubles the shapes. Sides towards the same level must
	 *  then have a profile that is the same both ways along them */
	UPROPERTY(EditAnywhere, Category="Kit|Variety")
	bool bMirror = false;

	/**
	 *  Each cell of this level stretches its piece above ScaleBase by its own factor, blended across its sides as the massif is, so neighbors still
	 *  meet: from a noise of the place, so a chain rises and falls along its length, plus a jitter and now and then a peak. Land fades it out
	 *  towards its sides, which lie on the side lines anyway. 1 everywhere leaves pieces as they are
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0))
	float HeightScaleMin = 1.0f;

	/** Lowest factor a cell ever takes, after the jitter, so a low mountain still reads as one. A factor set by hand may go lower */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0))
	float HeightScaleFloor = 0.0f;

	/**
	 *  A cell with at most LoneNeighbors neighbors on its level stands alone, so it takes at least LoneHeightMin, it has
	 *  its whole height from the foot raised to the power LoneSharpness, below 1, so its low ground rises into a broad dome that fills its cell
	 *  instead of falling to the ground, and its center moves only LoneShift of CenterShift, so it stands in its cell
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0, ClampMax=6))
	int32 LoneNeighbors = -1;

	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0))
	float LoneHeightMin = 1.0f;

	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0.1))
	float LoneSharpness = 1.0f;

	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0, ClampMax=1))
	float LoneShift = 1.0f;

	/**
	 *  How many more versions a cell with no neighbors on its level has besides its pieces, each a cluster of summits of unlike heights over the
	 *  piece lowered to SummitBase of its height: two summits, three in an uneven cluster, and a main peak with a lower shoulder, up to 3.
	 *  The hash and the Version tool of the brush choose among them and the pieces
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0, ClampMax=3))
	int32 LoneSummitStyles = 0;

	/** Share of its height above the side lines that the piece under a cluster of summits keeps */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0, ClampMax=1))
	float SummitBase = 0.55f;

	/** Height of the highest summit of a cluster, as a share of the peak of a piece above its foot, and the width of a summit as a share of the
	 *  radius of a cell, which each style scales */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0))
	float SummitHeight = 1.0f;

	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0.05))
	float SummitWidth = 1.0f;

	/** Height of a dome over the whole of a lone cell, as a share of its peak above the foot, below which its ground never lies */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0, ClampMax=1))
	float LoneBase = 0.0f;

	/**
	 *  A cell within LoneNeighbors that has one neighbor on its level is the end of a chain, a spur: it keeps the dome over its cell (LoneBase) and
	 *  its small shift, but not the tall lifted dome nor LoneHeightMin of a cell with none, which on the piece of a spur, whose peak stands off its
	 *  center, made a leaning tower. Its factor of the height is multiplied by SpurHeightScale, so a chain steps down at its end, and it is never
	 *  sharper than a straight cone
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0.1, ClampMax=2))
	float SpurHeightScale = 1.0f;

	/** Factor where the noise is highest */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0))
	float HeightScaleMax = 1.0f;

	/** Size of the noise of the factor in cells, about the length of a rise and a fall of a chain together */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0.1))
	float HeightNoiseCells = 4.0f;

	/** How much the factor of each cell differs at random from the noise, up and down */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0))
	float HeightJitter = 0.0f;

	/** Share of the cells that are a peak, more so where the noise is high */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0, ClampMax=1))
	float PeakChance = 0.0f;

	/** What a peak adds to the factor */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0))
	float PeakScale = 0.0f;

	/** Height of the piece, in mesh units, above which the factor stretches it, so what lies below keeps its place */
	UPROPERTY(EditAnywhere, Category="Kit|Variety")
	float ScaleBase = 0.0f;

	/** How far the center of each piece moves, at most, in a direction of its own, as a share of the radius of a cell. Its sides stay, and
	 *  what lies between follows linearly, so a peak stands off the middle of its cell and the cells of a chain do not line up */
	UPROPERTY(EditAnywhere, Category="Kit|Variety", meta=(ClampMin=0, ClampMax=0.5))
	float CenterShift = 0.0f;

	/**
	 *  Height of the piece above ScaleBase, in mesh units, that the shape of the inside of a cell measures by: SharpnessMin to SharpnessMax raise
	 *  that share to a power of their own by cell, so one peak is a blunt dome and the next a needle. 0 turns the shape off. Like CragHeight and
	 *  Twist it changes only the inside, fading out before the sides, so neighbors still meet
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0))
	float ShapeHeight = 0.0f;

	/** The power of the bluntest cell, below 1 a dome */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0.1))
	float SharpnessMin = 1.0f;

	/** The power of the sharpest cell, above 1 a needle */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0.1))
	float SharpnessMax = 1.0f;

	/** How far a noise of the place raises and lowers the inside of a cell, in mesh units, most at the top, so peaks split into summits and notches */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0))
	float CragHeight = 0.0f;

	/** Size of that noise, in mesh units */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=1))
	float CragScale = 40.0f;

	/**
	 *  How far the ground rises along the line from the center of a cell to the middle of each side towards the same level, in mesh units, from
	 *  nothing at the center to all of it on the side, so neighboring peaks join by a ridge over a high saddle and a range reads as one chain.
	 *  It depends on the place along the side the same way from both cells, so they still meet
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0))
	float RidgeLift = 0.0f;

	/** Share of the sides inside a massif, between two cells that both have more than two neighbors on their level, that get the whole ridge;
	 *  the rest get RidgeMassifLow of it, so a massif is not a net of ridges. Sides of a narrow chain always get all of it */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0, ClampMax=1))
	float RidgeMassifShare = 0.4f;

	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0, ClampMax=1))
	float RidgeMassifLow = 0.2f;

	/** Half the width of that ridge along a side, as a share of the side, below 0.5 so it ends before the corners */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0.01, ClampMax=0.49))
	float RidgeWidth = 0.3f;

	/** How far the inside of each cell turns at most around its center, in degrees, either way, so its ridges run in directions of their own */
	UPROPERTY(EditAnywhere, Category="Kit|Shape", meta=(ClampMin=0, ClampMax=60))
	float Twist = 0.0f;

	/**
	 *  How far a ridged noise of the place moves the ground up and down, in mesh units, where it lies high: from no ground at FacetHeightStart above
	 *  the sea to all of it at FacetHeightEnd, so big mountains break into sharp faces like split stone and small ones stay smooth. M_HexPlanet also
	 *  shades that ground in flat faces by the same share (the second UV, FacetStrength of DA_HexPlanet)
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Facets", meta=(ClampMin=0))
	float JaggedHeight = 0.0f;

	/** Size of the ridged noise, in mesh units */
	UPROPERTY(EditAnywhere, Category="Kit|Facets", meta=(ClampMin=1))
	float JaggedScale = 30.0f;

	/**
	 *  How deep gullies cut the sides of a cell, in mesh units: channels of a ridged noise of the angle around the center and of the distance
	 *  from it, stretched along that distance, so they run down the fall line with spurs between them. Most on the middle of the slopes, a
	 *  little less towards the foot, and not on the sides, which neighbors share. M_HexPlanet darkens the channels and lets snow lie in them
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Rock", meta=(ClampMin=0))
	float GullyHeight = 0.0f;

	/** How many gullies run down a cell around its center, about */
	UPROPERTY(EditAnywhere, Category="Kit|Rock", meta=(ClampMin=0))
	float GullyCount = 9.0f;

	/** How often a gully changes along its way down, so a high value breaks it into pieces */
	UPROPERTY(EditAnywhere, Category="Kit|Rock", meta=(ClampMin=0))
	float GullyLength = 1.2f;

	/** Height of the ledges that high ground steps in, in mesh units; 0 for none */
	UPROPERTY(EditAnywhere, Category="Kit|Rock", meta=(ClampMin=0))
	float LedgeStep = 0.0f;

	/** How much of a step a ledge takes, from 0 a smooth slope to 1 flat treads and steep risers. It follows the faces (FacetHeightStart) */
	UPROPERTY(EditAnywhere, Category="Kit|Rock", meta=(ClampMin=0, ClampMax=1))
	float LedgeStrength = 0.0f;

	/**
	 *  How far the whole piece rises above its foot, in mesh units, from nothing at the foot to all of it BulkReach of the way up to its peak,
	 *  so its low ground, which the side lines would flatten, stands as a full base out towards the sides and a chain one cell wide looks
	 *  massive. It depends on the height on the piece alone, so cells that share a side agree, and the foot stays on the side lines
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Bulk", meta=(ClampMin=0))
	float BulkLift = 0.0f;

	UPROPERTY(EditAnywhere, Category="Kit|Bulk", meta=(ClampMin=0.01, ClampMax=1))
	float BulkReach = 0.3f;

	/**
	 *  Where a noise along the ranges (SplitNoiseCells cells, above SplitThreshold) says so, the crest of a cell with neighbors on its level
	 *  splits into two blades with a couloir between them: the ground along the line from the center to the middle of each such side sinks by
	 *  SplitDepth of the peak within SplitWidth of a cell radius of it, and rises by SplitBlade at that distance. Only inside, so the crest
	 *  joins again on the sides its neighbors share
	 */
	UPROPERTY(EditAnywhere, Category="Kit|Bulk", meta=(ClampMin=0, ClampMax=1))
	float SplitDepth = 0.0f;

	UPROPERTY(EditAnywhere, Category="Kit|Bulk", meta=(ClampMin=0, ClampMax=1))
	float SplitBlade = 0.0f;

	UPROPERTY(EditAnywhere, Category="Kit|Bulk", meta=(ClampMin=0.05, ClampMax=0.6))
	float SplitWidth = 0.25f;

	UPROPERTY(EditAnywhere, Category="Kit|Bulk", meta=(ClampMin=0.1))
	float SplitNoiseCells = 3.0f;

	UPROPERTY(EditAnywhere, Category="Kit|Bulk", meta=(ClampMin=-1, ClampMax=1))
	float SplitThreshold = 0.0f;

	/** Height above the sea, in world units, where the faces begin */
	UPROPERTY(EditAnywhere, Category="Kit|Facets", meta=(ClampMin=0))
	float FacetHeightStart = 0.0f;

	/** Height above the sea, in world units, where they are full. Equal to the start turns them off */
	UPROPERTY(EditAnywhere, Category="Kit|Facets", meta=(ClampMin=0))
	float FacetHeightEnd = 0.0f;

	/** Color of flat ground */
	UPROPERTY(EditAnywhere, Category="Kit|Color")
	FLinearColor Color = FLinearColor::White;

	/** Color of steep ground */
	UPROPERTY(EditAnywhere, Category="Kit|Color")
	FLinearColor SlopeColor = FLinearColor::Gray;

	/** Pieces by the lowest turn of their mask */
	UPROPERTY(EditAnywhere, Category="Kit")
	TMap<int32, TSoftObjectPtr<UStaticMesh>> Meshes;

	/** Other versions with the same sides */
	UPROPERTY(EditAnywhere, Category="Kit")
	TMap<int32, FHexJoinedVariants> Variants;
};

/**
 *  Boulders that lie at the foot of the mountains on the edge of a range, as instances apart from the surface: on each side of a cell of mountains
 *  towards land or shallows, a few at random along it and a little into the neighbor
 */
USTRUCT(BlueprintType)
struct FHexPlanetBoulders
{
	GENERATED_BODY()

	/** Rocks of radius 1, from Tools/HexPlanetKit/generate_planet_kit.py */
	UPROPERTY(EditAnywhere, Category="Boulders")
	TArray<TSoftObjectPtr<UStaticMesh>> Meshes;

	/** Material of the rocks, which takes the rock layer of the biome of their mountain from the first custom data of an instance */
	UPROPERTY(EditAnywhere, Category="Boulders")
	TSoftObjectPtr<UMaterialInterface> Material;

	/** Most boulders on a side, which takes 0 to this many by a hash */
	UPROPERTY(EditAnywhere, Category="Boulders", meta=(ClampMin=0))
	int32 MaxPerSide = 4;

	/** Lowest level of the neighbor that gets boulders, -1 for the shallows */
	UPROPERTY(EditAnywhere, Category="Boulders")
	int32 MinLevel = -1;

	/** Radius of the smallest and the largest boulder, in world units */
	UPROPERTY(EditAnywhere, Category="Boulders", meta=(ClampMin=0.1))
	float SizeMin = 7.0f;

	UPROPERTY(EditAnywhere, Category="Boulders", meta=(ClampMin=0.1))
	float SizeMax = 20.0f;

	/** How far into the neighbor they lie at most, as a share of the way from its side to its center */
	UPROPERTY(EditAnywhere, Category="Boulders", meta=(ClampMin=0, ClampMax=1))
	float Spread = 0.3f;

	/** How deep each sinks into the ground, as a share of its radius, as it lies on a guess of the ground there */
	UPROPERTY(EditAnywhere, Category="Boulders", meta=(ClampMin=0, ClampMax=1))
	float Sink = 0.15f;

	/** How far the largest leans from the up of the planet, in degrees */
	UPROPERTY(EditAnywhere, Category="Boulders", meta=(ClampMin=0, ClampMax=90))
	float Tilt = 25.0f;

	/** Distance from the camera past which they are not drawn, 0 always */
	UPROPERTY(EditAnywhere, Category="Boulders", meta=(ClampMin=0))
	float CullDistance = 7000.0f;

	/** Whether they cast shadows */
	UPROPERTY(EditAnywhere, Category="Boulders")
	bool bCastShadow = true;
};

/**
 *  Floes of sea ice, which float on the frozen sea as instances (AHexPlanet::UpdateFloes); where they go and how they move is in the Floes of
 *  DA_HexPlanet, which is tuned by hand, as this asset is written by the import
 */
USTRUCT(BlueprintType)
struct FHexPlanetFloes
{
	GENERATED_BODY()

	/** Slabs of radius 1 around their waterline, from Tools/HexPlanetKit/generate_planet_kit.py */
	UPROPERTY(EditAnywhere, Category="Floes")
	TArray<TSoftObjectPtr<UStaticMesh>> Meshes;

	/** Small shards, which take the place of the slabs below FloeShardSize of DA_HexPlanet */
	UPROPERTY(EditAnywhere, Category="Floes")
	TArray<TSoftObjectPtr<UStaticMesh>> ShardMeshes;

	/** Material of the floes, M_HexPlanetFloe, which moves them in its world position offset */
	UPROPERTY(EditAnywhere, Category="Floes")
	TSoftObjectPtr<UMaterialInterface> Material;
};

/** All kits of the planet, written by Tools/HexPlanetKit/import_planet_kit.py */
UCLASS(BlueprintType)
class UHexPlanetKitConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category="Kit")
	TArray<FHexPlanetKit> Kits;

	UPROPERTY(EditAnywhere, Category="Boulders")
	FHexPlanetBoulders Boulders;

	UPROPERTY(EditAnywhere, Category="Floes")
	FHexPlanetFloes Floes;
};
