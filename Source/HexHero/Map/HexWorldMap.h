// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexWorldMap.generated.h"

class UHexBiomeConfig;
class UHexLevelConfig;
class UHexMapConfig;
class UHexMapSurfaceConfig;
class UHexMapWaterConfig;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UTexture2D;
class UStaticMesh;
struct FHexCoord;
struct FHexMapData;
struct FHexTerrainSource;

/** Ready-made meshes that stand on one terrain chunk */
USTRUCT()
struct FHexChunkFeatures
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;

	/** Level of each component */
	TArray<int32> Levels;
};

/** How the triangles of the map are drawn for debugging */
enum class EHexWireframeMode : uint8
{
	Off,
	Overlay,
	Only
};

/** Which hexes the triangles are drawn in */
enum class EHexWireframeScope : uint8
{
	All,
	Hover,
	HoverAndNeighbors
};

/** Which geometry the triangles are drawn on. Cliffs are not terrain here, as they are a mesh of their own */
enum class EHexWireframeTarget : uint8
{
	All,
	Terrain,
	AllButTerrain
};

/** How the water is shown for debugging */
enum class EHexWaterView : uint8
{
	Shown,
	Hidden,
	HiddenUnderCursor,
	HiddenAroundCursor
};

/** Debug view settings from the console variables hex.Wireframe, hex.WireframeScope, hex.WireframeTarget and hex.Water */
struct FHexDebugViewSettings
{
	EHexWireframeMode Mode = EHexWireframeMode::Off;
	EHexWireframeScope Scope = EHexWireframeScope::Hover;
	EHexWireframeTarget Target = EHexWireframeTarget::All;
	EHexWaterView Water = EHexWaterView::Shown;
};

/** Loaded other versions of one joined mesh */
USTRUCT()
struct FHexLoadedVariants
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;
};

/**
 *  Loads the map chosen in the map menu, or else the first one in it, and renders its terrain and water.
 *  Only the terrain chunks near the view exist. They are built on background threads as the camera moves.
 *  The grid material reads world positions, so this actor always stays at the world origin.
 */
UCLASS()
class AHexWorldMap : public AActor
{
	GENERATED_BODY()

	/** Mesh sizes and materials */
	UPROPERTY(Transient)
	TObjectPtr<const UHexMapConfig> MapConfig;

	/** The look of the land and of the water, which every material of the map takes as parameters, see HexMaterialConfig */
	UPROPERTY(Transient)
	TObjectPtr<const UHexMapSurfaceConfig> SurfaceConfig;

	UPROPERTY(Transient)
	TObjectPtr<const UHexMapWaterConfig> WaterLookConfig;

	/** Direction the sun of the level comes from, for the waves of the water and the drifts of snow */
	FVector SunDirection = FVector::UpVector;

	/** Terrain levels, lowest first */
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UHexLevelConfig>> Levels;

	/** Biomes, in the order that the biome indices of the map refer to */
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UHexBiomeConfig>> Biomes;

	/** Terrain parts of the map that have a mesh, by chunk column and row */
	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>> TerrainChunks;

	/** Empty components that wait to be used for another chunk */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> FreeChunkComponents;

	/** Ready-made meshes of the chunks that have any, by chunk column and row */
	UPROPERTY(Transient)
	TMap<FIntPoint, FHexChunkFeatures> ChunkFeatures;

	/** Loaded meshes of the levels that have a ready-made mesh, by level */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMesh>> FeatureMeshes;

	/** Loaded meshes that join across the sides of hexes, by level and mesh mask */
	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UStaticMesh>> JoinedMeshes;

	/** Loaded other versions of joined meshes, by level and mesh mask */
	UPROPERTY(Transient)
	TMap<FIntPoint, FHexLoadedVariants> JoinedVariants;

	/** Loaded notch meshes of the levels with joined meshes, by level */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMesh>> JoinedNotchMeshes;

	/** Instance of the material of cliffs */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CliffMaterial;

	/** Water surface of the map */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Water;

	/** Water material instance, which reads the data of each hex */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaterMaterial;

	/** Water of the rivers, one mesh for the whole map, or none on a map without rivers */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> RiverWater;

	/** River material instance, which hides the water at the cursor with the sea */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RiverMaterial;

	/** Terrain material instance, used to change the grid at runtime */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TerrainMaterial;

	/** Instances of the feature materials that differ from the terrain material, by level. The grid changes on them too */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> FeatureMaterials;

	/** Instance of the material that draws the triangles of the terrain, cliffs and ready-made meshes */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WireframeMaterial;

	/** Instances of the material that draws the triangles of joined meshes, by level */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> JoinedWireframeMaterials;

	/** Masked copies of the surface materials, by the instance they copy, which hide the hexes that only the triangles are drawn in, see CreateCutMaterial */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInstanceDynamic>, TObjectPtr<UMaterialInstanceDynamic>> CutMaterials;

	/** One pixel per hex with what the materials need to know about it */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> HexDataTexture;

	/** Height of the terrain along the sides of hexes, which the edges of joined meshes follow */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MeshRimTexture;

	/** How far each hex lies inside its group of joined meshes, which raises a group into a massif */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MassifTexture;

	/** Distance to the coast and direction away from it for the water, see FHexShoreField */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ShoreFieldTexture;

	/** Height of the ground above the water for the water, which finds its edge from it, see FHexShoreField */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ShoreHeightTexture;

	/** Map data shared with the background chunk builds, which may outlive this actor */
	TSharedPtr<FHexTerrainSource> TerrainSource;

	/** Chunks that are being built on a background thread */
	TSet<FIntPoint> BuildingChunks;

	/** Horizontal area covered by the map */
	FBox2D MapBounds = FBox2D(ForceInit);

	/** Folder in Content/Data/Maps of the loaded map */
	FString MapFolder;

	/** True if the grid is drawn */
	bool bGridVisible = true;

	/** How the triangles are drawn now */
	EHexWireframeMode WireframeMode = EHexWireframeMode::Off;

	/** Which geometry the triangles are drawn on now */
	EHexWireframeTarget WireframeTarget = EHexWireframeTarget::All;

	/** True while only the triangles of the hexes around the cursor are drawn, and the rest of the map keeps its normal render */
	bool bWireframeCut = false;

	/** True if the water material was last told to hide the water around the cursor */
	bool bWaterHiddenAtCursor = false;

public:

	/** Constructor */
	AHexWorldMap();

	/** Keeps the terrain chunks around the view up to date */
	virtual void Tick(float DeltaSeconds) override;

	/** Returns true if a map was loaded */
	bool IsMapLoaded() const { return MapBounds.bIsValid; }

	/** Returns the horizontal area covered by the map */
	const FBox2D& GetMapBounds() const { return MapBounds; }

	/** Returns the folder in Content/Data/Maps of the loaded map */
	const FString& GetMapFolder() const { return MapFolder; }

	/** Shows or hides the grid */
	void SetGridVisible(bool bVisible);

	/** Returns true if the grid is drawn */
	bool IsGridVisible() const { return bGridVisible; }

	/** Finds the first hex of the map that a ray reaches */
	bool FindHexAlongRay(const FVector& RayOrigin, const FVector& RayDirection, FHexCoord& OutHex) const;

	/** Finds the center of a hex at the height of its level or of the water over it. Returns false if the hex is not on the map */
	bool GetHexCenter(const FHexCoord& Hex, FVector& OutCenter) const;

	/** Returns a description of a hex on the map, for debugging */
	FString GetHexDebugText(const FHexCoord& Hex) const;

	/** Switches the console variable hex.Wireframe to the next way of drawing the triangles */
	static void CycleWireframeMode();

	/** Switches the console variable hex.WireframeScope to the next set of hexes */
	static void CycleWireframeScope();

	/** Switches the console variable hex.WireframeTarget to the next set of geometry */
	static void CycleWireframeTarget();

	/** Switches the console variable hex.Water to the next way of showing the water */
	static void CycleWaterView();

	/** Gives the debug view settings as pairs of name and value. Returns true if they all show the map as usual */
	static bool GetDebugViewStatus(TArray<TPair<FString, FString>>& OutLines);

	/** Returns the debug view settings of the console variables, which the planet shares */
	static FHexDebugViewSettings GetDebugViewSettings();

	/**
	 *  Returns a copy of a material instance whose parent is the masked instance <parent>_Cut, with the same parameters, or nothing if there is none.
	 *  Its mask hides the hexes that only the triangles are drawn in, while the rest keeps its normal render
	 */
	static UMaterialInstanceDynamic* CreateCutMaterial(UMaterialInstanceDynamic* Source, UObject* Outer);

protected:

	/** Loads the map */
	virtual void BeginPlay() override;

private:

	/** Prepares everything the terrain chunks need */
	void PrepareTerrain();

	/** Builds the water surface */
	void BuildWater();

	/** Builds the water of the rivers */
	void BuildRiverWater();

	/** Fills the texture with the data of each hex and gives it to a material */
	/** Gives a material of the map the look of the land and the water, the sun, the height of the water and what it needs of the Tuning of DA_HexMap */
	void ApplyLook(UMaterialInstanceDynamic* Target) const;

	void CreateHexDataTexture();

	/** Finds the distance to the coast and gives it to the water material */
	void CreateShoreField();

	/** Finds the height of the terrain along the sides of hexes and gives it to the materials of joined meshes */
	void CreateMeshRims();

	/** Moves the grid of the water over what the camera sees, with cells that fit that area */
	void UpdateWaterGrid(const FBox2D& ViewArea);

	/** Finds the part of the ground that the camera sees, with a margin */
	bool GetViewArea(FBox2D& OutArea) const;

	/** Finds the chunks that the view reaches. The max of the area is not included */
	bool GetChunksInView(FIntRect& OutChunks) const;

	/** Follows the console variables of the wireframe and the water, and the hex under the mouse cursor */
	void UpdateDebugView();

	/** Shows or hides the triangles of a terrain chunk as the wireframe mode says */
	void ApplyWireframe(UProceduralMeshComponent* Component) const;

	/** Shows or hides the triangles of ready-made meshes as the wireframe mode says */
	void ApplyWireframe(UInstancedStaticMeshComponent* Component, int32 Level) const;

	/** Creates the masked copies of the surface materials, or copies the parameters to them again */
	void UpdateCutMaterials();

	/** Returns the masked copy of a surface material, or the material itself if it has none */
	UMaterialInterface* GetCutMaterial(UMaterialInterface* Material) const;

	/** Returns every instance of the wireframe materials */
	TArray<UMaterialInstanceDynamic*> GetWireframeMaterials() const;

	/** Returns the material that the ready-made meshes of a level have, or nothing if they keep their own */
	UMaterialInterface* GetFeatureMaterial(int32 Level) const;

	/** Places the ready-made meshes of a chunk */
	void AddChunkFeatures(const FIntPoint& Chunk);

	/** Returns the joined mesh that a hex has, or nothing if its level has none for that mesh mask */
	UStaticMesh* FindJoinedMesh(int32 Level, int32 MeshMask, const FHexCoord& Hex) const;

	/** Returns a mask with bit N set if the neighbor of a hex in direction N is on the map and has the same level */
	int32 GetSameLevelNeighbors(const FHexCoord& Hex) const;

	/** Gives finished background builds a mesh component */
	void AddBuiltChunks(const FIntRect& ChunksToKeep);

	/** Frees the chunks outside an area */
	void RemoveChunksOutside(const FIntRect& ChunksToKeep);

	/** Starts background builds of the missing chunks, closest to the center of the area first */
	void StartChunkBuilds(const FIntRect& WantedChunks);

	/** Creates an empty mesh component */
	UProceduralMeshComponent* CreateMeshComponent(const FName& Name);
};
