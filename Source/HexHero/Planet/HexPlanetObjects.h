// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexPlanetUnderground.h"
#include "HexPlanetObjects.generated.h"

class AHexPlanet;
class UHexPlanetConfig;
class UHexPlanetObjectSet;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

/**
 *  The forests, bushes, flowers, stones and decor that stand on the cells of a planet, of the kinds of UHexPlanetObjectSet. Each cell holds at most one
 *  cover, at a density of 1 to 3, and one decor, read from objects.json of the planet or generated from its biomes when it has none
 *  (FHexObjectVariant::GenerateShare), and painted with the brush. The objects are instances of their meshes, a hierarchical component per
 *  chunk of the grid and mesh, so a change rebuilds the chunks of its cells alone and what lies behind the planet is culled. They stand on the real
 *  ground, found among the triangles of the surface of their cell, so a rebuild of the surface places them again (RebuildChunks). The first
 *  custom data of an instance is the rock layer of the biome of its cell, the second the snow of its variant. Each layer has its own kinds
 *  (UHexPlanetObjectSet::PlanetLayers, FindLayerSets): the underground generates the cave kinds on its passages in patches, kept off its exits
 *  (HexPlanetUnderground::GenerateObjects), and keeps each mesh off the voids beside a passage (bClipFootprint). The kinds of the rims
 *  (EHexObjectLayer::Rim) are stored nowhere: every side of a passage towards a void takes them as its chunk is placed, rocks and spires on the
 *  lip and stalactites down the cliff below it (HexPlanetUnderground::FindRimSides), so they follow every edit of the levels and the entrances.
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

	/** Writes objects.json into a folder, leaving out what the level of its cell does not allow and what stands on the cells of the entrances that
	 *  the same save keeps (KeptEntranceCells, from HexPlanetUnderground::GetSavedEntrances), where nothing is drawn; an entrance that cannot
	 *  be used is left out of entrances.json, so its cell keeps its objects */
	bool Save(const FString& Folder, const TSet<int32>& KeptEntranceCells, FString& OutError) const;

	/** Returns the lines of objects.json for the objects of the cells, their sets by index plus one into SetIds, with each cover and decor
	 *  that IsKept says a save keeps; a cell keeping neither has no line */
	static TArray<FHexObjectCell> MakeSavedLines(TConstArrayView<FHexCellObjects> Cells, TConstArrayView<FName> SetIds, TFunctionRef<bool(int32 Cell, int32 Set)> IsKept);

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

	/** Gives the pool of light that what stands on a cell casts on its floor (UHexPlanetObjectSet::GlowPool, GlowColor): the stronger of its cover,
	 *  GlowPool times 0.6, 0.8 or 1 by its density, and its decor, GlowPool. Returns false for a cell that does not glow or is clear */
	bool GetCellGlow(int32 Cell, FLinearColor& OutColor, float& OutStrength) const;

	/** Returns how many spores rise from what stands on a cell (UHexPlanetObjectSet::Spores), the more of its cover and decor, with the glow color
	 *  of that kind; 0 for a cell that is clear */
	int32 GetCellSpores(int32 Cell, FLinearColor& OutColor) const;

	/** Keeps the objects off these cells, the entrances to the underground and their exits, whose markers they would hide; what the cells hold
	 *  stays, and shows again once a cell is no entrance; a save leaves it out only for an entrance it keeps (Save). Places the chunks of the
	 *  cells that changed again once the planet is built. Returns true if the set of cleared cells changed */
	bool SetClearCells(TConstArrayView<int32> Cells);

	/** Returns true if no object stands on a cell, as it holds an entrance */
	bool IsClear(int32 Cell) const { return ClearCells.Contains(Cell); }

	/** Shows the objects (hex.PlanetObjects) and, with them, the rims (hex.PlanetRims), or hides them, to see what they cost */
	void SetShown(bool bInObjects, bool bInRims);

	/** Gives what stands on the walls the point under the cursor, around which it dissolves with them (WallFadeCenter of M_HexPlanetCaveWall),
	 *  alpha 0 for none */
	void SetFadeFocus(const FLinearColor& Center);

	/** Returns true if the layer has kinds of rims */
	bool HasRims() const { return !RimSets.IsEmpty(); }

	/** Returns a line for hex.PlanetRimSides: by kind of rim, the sides of a cell over a void that take it, and those it leaves out as the cell or
	 *  its void is an exit or near one, or by RimSideShare; empty when no side of the cell lies over a void */
	FString GetRimText(int32 Cell) const;

	/** Returns the kinds of objects of a layer, those whose PlanetLayers holds it, one per Id, sorted by SortOrder, at most 254: the sets a planet of
	 *  that layer counts FHexCellObjects by, so the palette, the generation and the files count alike before the layer is built */
	static TArray<TObjectPtr<UHexPlanetObjectSet>> FindLayerSets(EHexPlanetLayer Layer);

	/** Returns the indices of the sets that can be painted and generated, the covers and decor */
	static TArray<int32> GetPaintableSets(TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets);

	/** Returns the kinds the generation of the underground places, one per cover and decor set of a layer with the index of its set; a kind that
	 *  blocks movement is left out with a warning, as the generation never cuts a passage */
	static TArray<HexPlanetUnderground::FObjectKind> MakeObjectKinds(TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets);

	/** Generates the objects of the cells of an underground from its levels, the letters of its biomes and its entrances
	 *  (HexPlanetUnderground::GenerateObjects), with the seed of its manifest, and logs what each kind got and the middle of its largest patch */
	static void GenerateUndergroundObjects(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<TCHAR> CellBiomeLetters,
		TConstArrayView<FHexEntrance> Entrances, const FHexPlanetManifest& Manifest, TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets,
		TArray<FHexCellObjects>& OutObjects);

	/** Writes objects.json of objects into a folder, with what the levels of their cells allow, as Save writes those of a planet */
	static bool SaveCells(const FString& Folder, TConstArrayView<FHexCellObjects> Cells, TConstArrayView<int8> Levels,
		TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets, FString& OutError);

	/** On the underground, the objects its generation gives the cells for levels and biomes (indices into the biomes of the planet), as the editor
	 *  asks; false on the surface or before Init */
	bool GenerateCells(TConstArrayView<int8> Levels, TConstArrayView<uint8> Biomes, TArray<FHexCellObjects>& OutObjects) const;

private:

	/** A mesh of a kind of object, drawn by a component per chunk */
	struct FSlot
	{
		int32 Set = 0;
		TObjectPtr<UStaticMesh> Mesh;

		/** Farthest the mesh reaches from its pivot across its up, for UHexPlanetObjectSet::bClipFootprint */
		float FootprintRadius = 0.0f;
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

	/** Materials of the sets that glow, with the values of the set too, by their parent and set (GlowMaterialKeys), kept here */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> GlowMaterials;
	TArray<TPair<const UMaterialInterface*, const UHexPlanetObjectSet*>> GlowMaterialKeys;

	/** The instances of the material of what stands on the walls (AHexPlanet::GetWallObjectMaterial), which SetFadeFocus gives the point the walls
	 *  dissolve around, and the point it gave last, for an instance made after it */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FadeMaterials;
	FLinearColor FadeCenter = FLinearColor::Transparent;

	/** The component of each chunk and slot that has drawn objects, by Chunk * 65536 + Slot */
	UPROPERTY(Transient)
	TMap<int64, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Components;

	/** The objects of each cell */
	TArray<FHexCellObjects> CellObjects;

	/** The cells of each chunk */
	TArray<TArray<int32>> ChunkCells;

	/** The cells that hold an entrance, where nothing is drawn (SetClearCells) */
	TSet<int32> ClearCells;

	/** The kinds of rims, as indices of Sets, and the most rings around an exit any of them keeps clear */
	TArray<int32> RimSets;
	int32 RimClearRings = 0;

	/** How many rings each cell lies from a cleared cell, up to RimClearRings (HexPlanetUnderground::GetRingDistances) */
	TArray<int32> ClearRings;

	bool bGenerated = false;
	bool bObjectsShown = true;
	bool bRimsShown = true;

	/** What PlaceChunk placed besides the objects, for the log */
	struct FPlaceStats
	{
		/** By set, the cells where none of its objects fitted */
		TArray<int32> EmptyCells;

		/** By set, the sides a rim stands on */
		TArray<int32> RimSides;

		/** Tries to place a rim that hangs where no cliff was found below the lip */
		int32 HangMissed = 0;
	};

	/** Finds how far each cell lies from a cleared cell, for the rims that keep clear of exits */
	void UpdateClearRings();

	/** Returns true if the components of a slot are drawn now */
	bool IsSlotShown(int32 Slot) const;

	/** Takes the objects from objects.json of the planet, or generates them */
	void Load();

	/** Gives the land cells their covers and decor from the shares of the variants and a noise of patches; underground the passages, by
	 *  GenerateUndergroundObjects */
	void Generate();

	/** Returns the material a slot draws with, an instance with the values of DA_HexPlanet, and of its set too for a set that glows */
	UMaterialInterface* GetMaterial(UMaterialInterface* Parent, const UHexPlanetObjectSet* Set);

	/** Returns the objects of a chunk, placed on its ground, with its rims, and by set the cells where none of it fitted; reads only what does not
	 *  change while it runs, so chunks run in parallel */
	TArray<FInstance> PlaceChunk(int32 Chunk, FPlaceStats& OutStats) const;

	/** Adds the objects of one kind on a cell to Out */
	void PlaceInCell(int32 Cell, int32 Set, int32 Density, bool bDecor, const TArray<FVector3f>& Triangles, TArray<FInstance>& Out) const;

	/** Adds the rims of a chunk to Out: along every side of its passages towards a void (HexPlanetUnderground::FindRimSides), RimCountMin to the
	 *  first of CountByDensity a side, spread along it, on the lip of the passage, or for a kind that hangs down the face of the cliff of the void
	 *  from the height of the lip; the stream of each side comes from its cell, side and Id, so the rims stand where they stood */
	void PlaceRims(int32 Chunk, TArray<FInstance>& Out, FPlaceStats& Stats) const;

	/** On a layer with walls (AHexPlanet::GetWalls), adds the rims of a chunk that stand on them (EHexRimSeat::WallFoot and WallFace): RimSideShare
	 *  of the samples of its walls, each by a hash of where it lies and the Id, on the rounded foot or out of the inner face */
	void PlaceOnWalls(int32 Chunk, TArray<FInstance>& Out, FPlaceStats& Stats) const;

	/** Gives the components of a chunk its objects, making the components it lacks */
	void ApplyChunk(int32 Chunk, const TArray<FInstance>& Instances);
};
