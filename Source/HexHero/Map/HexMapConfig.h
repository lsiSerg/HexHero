// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexTerrainTuning.h"
#include "HexMapConfig.generated.h"

class UMaterialInterface;
class UHexMapSurfaceConfig;
class UHexMapWaterConfig;

/**
 *  Mesh sizes and materials of the hex world map.
 */
UCLASS(BlueprintType)
class UHexMapConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Distance from the center of a hex to its corners */
	UPROPERTY(EditAnywhere, Category="Mesh", meta=(ClampMin=1))
	float HexRadius = 100.0f;

	/** Height difference between two neighboring levels */
	UPROPERTY(EditAnywhere, Category="Mesh", meta=(ClampMin=0))
	float LevelHeight = 40.0f;

	/** Parts into which the line from the center of a hex to a corner is divided. A hex has 6 x N x N triangles */
	UPROPERTY(EditAnywhere, Category="Mesh", meta=(ClampMin=1, ClampMax=16))
	int32 HexSubdivisions = 6;

	/** How sharply one level changes into the next. Higher values make hex centers flatter and slopes steeper */
	UPROPERTY(EditAnywhere, Category="Mesh", meta=(ClampMin=1, ClampMax=8))
	float LevelBlendPower = 3.0f;

	/** How far the borders between levels can bend away from the sides of the hexes */
	UPROPERTY(EditAnywhere, Category="Mesh", meta=(ClampMin=0))
	float LevelWarpDistance = 15.0f;

	/** How far the terrain around a joined mesh is pulled to its foot, as a multiple of the hex radius */
	UPROPERTY(EditAnywhere, Category="Mesh", meta=(ClampMin=0.1, ClampMax=1))
	float JoinedMeshFootBlend = 0.6f;

	/** Height of the bottom of the walls along the outer edge of the map */
	UPROPERTY(EditAnywhere, Category="Mesh")
	float MapBottomHeight = -200.0f;

	/** Height of the water surface */
	UPROPERTY(EditAnywhere, Category="Mesh")
	float WaterHeight = -15.0f;

	/** Depth of the seabed down to which the water keeps only the clearness of its biome, so what is under it shows */
	UPROPERTY(EditAnywhere, Category="Water", meta=(ClampMin=0))
	float WaterClearDepth = 6.0f;

	/** Depth of the seabed from which the water is fully opaque. Deep water lies 65 below the surface and shallows 25 */
	UPROPERTY(EditAnywhere, Category="Water", meta=(ClampMin=1))
	float WaterOpaqueDepth = 45.0f;

	/** How strong the soft foam that always washes where the water touches land, a cliff or a mountain is at its clearest, from 0 to 1 */
	UPROPERTY(EditAnywhere, Category="Water", meta=(ClampMin=0, ClampMax=1))
	float WaterEdgeFoam = 0.8f;

	/** How far out from the water's edge that foam washes at its furthest */
	UPROPERTY(EditAnywhere, Category="Water", meta=(ClampMin=1))
	float WaterEdgeFoamWidth = 50.0f;

	/** Smallest difference in levels between land and water that makes a cliff. A smaller one is a beach. Zero means no cliffs */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	int32 CliffMinLevelDifference = 2;

	/** Lowest height of the top of a cliff above the water */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	float CliffMinRise = 33.0f;

	/** Height that the bottom of a cliff reaches, under the terrain below the water */
	UPROPERTY(EditAnywhere, Category="Cliffs")
	float CliffBottomHeight = -60.0f;

	/** How far the terrain under the water lies below the water next to a cliff */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=1))
	float CliffSeabedDepth = 25.0f;

	/** Material of cliffs */
	UPROPERTY(EditAnywhere, Category="Cliffs")
	TSoftObjectPtr<UMaterialInterface> CliffMaterial;

	/** Width of the water of every river, which bridges are made for. Up to half a hex, as only nearby hexes are checked */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=4, ClampMax=90))
	float RiverWidth = 28.0f;

	/** Depth of the floor of a river below its water */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0.5))
	float RiverDepth = 4.0f;

	/** How far the top of the bank, a small cliff along the river, lies above its water */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0.5))
	float RiverBankHeight = 3.0f;

	/** How far the face of the bank leans back from the water at its top */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverBankLean = 3.0f;

	/** How far a river winds away from the straight line through the centers of its hexes */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverMeander = 18.0f;

	/** Finer values of how the terrain, the coast, cliffs and rivers are shaped */
	UPROPERTY(EditAnywhere, Category="Tuning")
	FHexTerrainTuning Tuning;

	/** Width and height in hexes of a separately rendered part of the map */
	UPROPERTY(EditAnywhere, Category="Mesh", meta=(ClampMin=1))
	int32 ChunkSize = 16;

	/** How far past the edges of the view chunks are wanted, in hexes. It covers tall terrain and some camera movement */
	UPROPERTY(EditAnywhere, Category="Streaming", meta=(ClampMin=0))
	float ViewMarginInHexes = 4.0f;

	/** Background threads taken at once by chunk builds */
	UPROPERTY(EditAnywhere, Category="Streaming", meta=(ClampMin=1))
	int32 MaxChunkBuilds = 4;

	/** Chunks sent to the renderer per frame, which limits the hitch */
	UPROPERTY(EditAnywhere, Category="Streaming", meta=(ClampMin=1))
	int32 MaxChunksAddedPerTick = 2;

	/** Cells along each side of the grid of the water, which follows the view, and their smallest size */
	UPROPERTY(EditAnywhere, Category="Water", meta=(ClampMin=8, ClampMax=1024))
	int32 WaterGridCells = 256;

	UPROPERTY(EditAnywhere, Category="Water", meta=(ClampMin=0.5))
	float WaterGridMinCell = 6.0f;

	/** The look of the land: M_HexTerrain, M_HexMountain, M_HexCliff and the grid */
	UPROPERTY(EditAnywhere, Category="Rendering")
	TSoftObjectPtr<UHexMapSurfaceConfig> SurfaceConfig = TSoftObjectPtr<UHexMapSurfaceConfig>(FSoftObjectPath(TEXT("/Game/HexMap/DA_HexMapSurface.DA_HexMapSurface")));

	/** The look of the sea, ice, foam and rivers */
	UPROPERTY(EditAnywhere, Category="Rendering")
	TSoftObjectPtr<UHexMapWaterConfig> WaterLookConfig = TSoftObjectPtr<UHexMapWaterConfig>(FSoftObjectPath(TEXT("/Game/HexMap/DA_HexMapWater.DA_HexMapWater")));

	/** Material of the terrain surface */
	UPROPERTY(EditAnywhere, Category="Rendering")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	/** Material of the water surface */
	UPROPERTY(EditAnywhere, Category="Rendering")
	TSoftObjectPtr<UMaterialInterface> WaterMaterial;

	/** Material of the water of rivers */
	UPROPERTY(EditAnywhere, Category="Rivers")
	TSoftObjectPtr<UMaterialInterface> RiverMaterial;

	/** Material that draws the triangles of the terrain, cliffs and ready-made meshes, for debugging */
	UPROPERTY(EditAnywhere, Category="Debug")
	TSoftObjectPtr<UMaterialInterface> WireframeMaterial;

	/** Material that draws the triangles of joined meshes, moving their edges onto the terrain as their own material does */
	UPROPERTY(EditAnywhere, Category="Debug")
	TSoftObjectPtr<UMaterialInterface> JoinedWireframeMaterial;
};
