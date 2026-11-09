// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HexPlanetObjects.generated.h"

class AHexPlanet;
class UHexPlanetConfig;
class UHexPlanetObjectSet;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** The objects of a cell: its cover and decor as an index into the sets plus one, 0 for none, and the density of the cover, 1 to 3 */
struct FHexCellObjects
{
	uint8 Cover = 0;
	uint8 Density = 0;
	uint8 Decor = 0;

	bool IsEmpty() const { return Cover == 0 && Decor == 0; }
	bool operator==(const FHexCellObjects& Other) const { return Cover == Other.Cover && Density == Other.Density && Decor == Other.Decor; }
};

/**
 *  The forests, bushes, flowers, stones and decor that stand on the cells of a planet, of the kinds of UHexPlanetObjectSet. Each cell holds at most one
 *  cover, at a density of 1 to 3, and one decor, read from objects.json of the planet or generated from its biomes when it has none
 *  (FHexObjectVariant::GenerateShare), and painted with the brush. The objects are instances of their meshes, a hierarchical component per
 *  chunk of the grid and mesh, so a change rebuilds the chunks of its cells alone and what lies behind the planet is culled. They stand on the real
 *  ground, found among the triangles of the surface of their cell, so a rebuild of the surface places them again (RebuildChunks). The first
 *  custom data of an instance is the rock layer of the biome of its cell, the second the snow of its variant.
 */
UCLASS()
class UHexPlanetObjects : public USceneComponent
{
	GENERATED_BODY()

public:

	/** Reads the kinds of objects and their meshes, and takes the objects of the cells from objects.json of the planet, or generates them. Call once
	 *  the planet has its grid, levels and biomes, before its surface is built */
	void Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings);

	/** Returns true if the objects were generated, as the planet had no objects.json */
	bool WasGenerated() const { return bGenerated; }

	/** Writes objects.json into a folder, leaving out what the level of its cell does not allow */
	bool Save(const FString& Folder, FString& OutError) const;

	/** Places the objects of the cells of these chunks again on the surface shown now */
	void RebuildChunks(TConstArrayView<int32> Chunks);

	/** Places the objects of these cells again, as their biomes changed, by rebuilding their chunks */
	void RebuildCells(TConstArrayView<int32> Cells);

	/** Sets the objects of cells and places them */
	void SetCells(const TMap<int32, FHexCellObjects>& Changes);

	/** Returns the objects of a cell */
	FHexCellObjects GetCell(int32 Cell) const { return CellObjects.IsValidIndex(Cell) ? CellObjects[Cell] : FHexCellObjects(); }

	/** Returns the kinds of objects, by which FHexCellObjects counts */
	const TArray<TObjectPtr<UHexPlanetObjectSet>>& GetSets() const { return Sets; }

	/** Returns true if a kind of object may stand on a level */
	bool IsAllowed(int32 Set, int32 Level) const;

	/** Returns true if what stands on a cell, and its level allows, keeps heroes out */
	bool BlocksMovement(int32 Cell) const;

	/** Returns a line for the info at the cursor: what stands on a cell */
	FString GetCellText(int32 Cell) const;

	/** Gives how the cover of a cell tints its ground from far away (UHexPlanetObjectSet::FarCover, FarColor). Returns false if it does not */
	bool GetCellCanopy(int32 Cell, FLinearColor& OutColor, float& OutCover) const;

	/** Keeps the objects off these cells, the entrances to the underground and their exits, whose markers they would hide; what the cells hold
	 *  stays, also in objects.json, and shows again once a cell is no entrance. Places the chunks of the cells that changed again once the
	 *  planet is built */
	void SetClearCells(TConstArrayView<int32> Cells);

	/** Returns true if no object stands on a cell, as it holds an entrance */
	bool IsClear(int32 Cell) const { return ClearCells.Contains(Cell); }

private:

	/** A mesh of a kind of object, drawn by a component per chunk */
	struct FSlot
	{
		int32 Set = 0;
		TObjectPtr<UStaticMesh> Mesh;
	};

	/** An object placed in a chunk */
	struct FInstance
	{
		int32 Slot = 0;
		FTransform Transform;
		float Rock = 0.0f;
		float Snow = 0.0f;
	};

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetConfig> Settings;

	/** The kinds of objects, by SortOrder */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHexPlanetObjectSet>> Sets;

	/** The meshes of every kind, and by kind and variant those of the variant, as indices of Slots */
	TArray<FSlot> Slots;
	TArray<TArray<TArray<int32>>> VariantSlots;

	/** Meshes kept loaded while they are drawn */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> LoadedMeshes;

	/** Materials of the meshes and the sets with the values of DA_HexPlanet, by their parent */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> Materials;

	/** The component of each chunk and slot that has drawn objects, by Chunk * 65536 + Slot */
	UPROPERTY(Transient)
	TMap<int64, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Components;

	/** The objects of each cell */
	TArray<FHexCellObjects> CellObjects;

	/** The cells of each chunk */
	TArray<TArray<int32>> ChunkCells;

	/** The cells that hold an entrance, where nothing is drawn (SetClearCells) */
	TSet<int32> ClearCells;

	bool bGenerated = false;

	/** Takes the objects from objects.json of the planet, or generates them */
	void Load();

	/** Gives the land cells their covers and decor from the shares of the variants and a noise of patches */
	void Generate();

	/** Returns the material a slot draws with, an instance with the values of DA_HexPlanet */
	UMaterialInterface* GetMaterial(UMaterialInterface* Parent);

	/** Returns the objects of a chunk, placed on its ground; reads only what does not change while it runs, so chunks run in parallel */
	TArray<FInstance> PlaceChunk(int32 Chunk) const;

	/** Adds the objects of one kind on a cell to Out */
	void PlaceInCell(int32 Cell, int32 Set, int32 Density, bool bDecor, const TArray<FVector3f>& Triangles, TArray<FInstance>& Out) const;

	/** Gives the components of a chunk its objects, making the components it lacks */
	void ApplyChunk(int32 Chunk, const TArray<FInstance>& Instances);
};
