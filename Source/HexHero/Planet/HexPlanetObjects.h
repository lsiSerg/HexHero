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
 *  Besides its cover and decor a cell holds the objects placed on it by hand (FHexPlacedObject, the brush By hand, as the developer asked on
 *  2026-11-10), as many as it takes, one mesh each where it was placed, with its turn, scale and mesh, which may reach over into a neighbor but
 *  never over a void nor into a wall (CanPlace), and block nothing; a mass of the underground that blocks fills its cell and is not placed by hand.
 *  They glow and give spores as a decor.
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
	 *  that IsKept says a save keeps and each object placed by hand that IsPlacedKept does; a cell keeping none has no line */
	static TArray<FHexObjectCell> MakeSavedLines(TConstArrayView<FHexCellObjects> Cells, TConstArrayView<FName> SetIds, TFunctionRef<bool(int32 Cell, int32 Set)> IsKept,
		TFunctionRef<bool(int32 Cell, int32 Set)> IsPlacedKept);

	/** Returns true if an object placed by hand stands on a level: any land of the surface and the passages underground, whatever its kind allows,
	 *  as a hand chooses where it goes */
	static bool IsPlacedLevel(int32 Level) { return Level >= 0; }

	/** Places the objects of the cells of these chunks again on the surface shown now */
	void RebuildChunks(TConstArrayView<int32> Chunks);

	/** Places the objects of these cells again, as their biomes changed, by rebuilding their chunks */
	void RebuildCells(TConstArrayView<int32> Cells);

	/** Sets the objects of cells and places them */
	void SetCells(const TMap<int32, FHexCellObjects>& Changes);

	/** Returns the objects of a cell */
	const FHexCellObjects& GetCell(int32 Cell) const
	{
		static const FHexCellObjects None;
		return CellObjects.IsValidIndex(Cell) ? CellObjects[Cell] : None;
	}

	/** Returns the axes of the frame of a cell, from the direction of its middle, by which an object placed by hand keeps where it stands
	 *  (FHexPlacedObject::Offset); the ground of the cell is flattened onto them to find the triangle under a point */
	static void GetCellAxes(const FVector3d& Center, FVector3d& OutX, FVector3d& OutY);

	/** Returns where an object placed by hand on a cell stands, as a point of the plane of its frame in world units */
	FVector2f GetPlacedFlat(const FHexPlacedObject& Object) const;

	/** Returns where a point stands on a cell as FHexPlacedObject::Offset keeps it, to a thousandth of a cell radius as objects.json does */
	FVector2f GetPlacedOffset(int32 Cell, const FVector3d& Point) const;

	/** Returns about where an object placed by hand on a cell stands, at a distance from the center, for picking it */
	FVector3d GetPlacedPoint(int32 Cell, const FHexPlacedObject& Object, double AtRadius) const;

	/** Makes an object of a kind to place by hand at a point of a cell, its turn, scale and mesh from a hash of where it stands, or returns false
	 *  and why it cannot stand there (CanPlace) */
	bool MakePlaced(int32 Cell, int32 Set, const FVector3d& Point, FHexPlacedObject& OutObject, FString& OutProblem) const;

	/** Returns true if an object placed by hand may stand where it is on a cell, else false and why: on land or a passage (IsPlacedLevel), not a
	 *  mass of the underground that blocks and fills its cell, not on an entrance, on the ground and not under the sea, not in a river, and
	 *  underground its mesh not over a void nor into a wall; what is placed by hand blocks nothing */
	bool CanPlace(int32 Cell, const FHexPlacedObject& Object, FString* OutProblem = nullptr) const;

	/** Finds the object nearest a point, on the cell there or its neighbors, within MaxDistance, whether placed by hand or of the cover or decor
	 *  of a cell: then OutObjects are the objects of its cell with that cover and decor unpacked into objects placed by hand (UnpackCell), else
	 *  as they are, and OutIndex is its index among their Placed; false if there is none */
	bool FindObjectNear(const FVector3d& Point, double MaxDistance, int32& OutCell, int32& OutIndex, FHexCellObjects& OutObjects) const;

	/** Gives the objects of a cell with its cover and decor turned into objects placed by hand where they stand (PlaceInCell, without their
	 *  tilt), in front of those placed by hand before, so each can be moved, turned or taken away on its own; false if it has no cover or decor
	 *  drawn */
	bool UnpackCell(int32 Cell, FHexCellObjects& OutObjects) const;

	/** Returns how many meshes a kind has over all its variants, which the Version tool cycles through */
	int32 GetMeshCount(int32 Cell, int32 Set) const;

	/** Shows an object placed by hand where it would stand on a cell, apart from the chunks, as Move drags it, so no chunk is placed again every
	 *  frame; null hides it */
	void SetDragPreview(int32 Cell, const FHexPlacedObject* Object);

	/** Returns the kinds of objects, by which FHexCellObjects counts: those of the layer, then those of the other layer that only a hand places
	 *  here (FindPlaceableSets) */
	const TArray<TObjectPtr<UHexPlanetObjectSet>>& GetSets() const { return Sets; }

	/** Returns how many of the sets are of the layer itself, first in GetSets: the others are of the other layer, for placing by hand alone */
	int32 GetNumOwnSets() const { return NumOwnSets; }

	/** Returns true if a kind of object may stand on a level */
	bool IsAllowed(int32 Set, int32 Level) const;

	/** Returns true if what stands on a cell, and its level allows, keeps heroes out */
	bool BlocksMovement(int32 Cell) const;

	/** Returns the name of the kind that blocks movement on a cell, empty when none does */
	FString GetBlockerName(int32 Cell) const;

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

	/** Returns the cells of a planet whose cover or decor keeps heroes out, read from its objects.json: for a layer that has its data but
	 *  not its objects yet, as an underground that was never shown, through which a way of a hero is found all the same. No cell without
	 *  the file, which the first show of the layer writes */
	static TBitArray<> FindSavedBlockers(const AHexPlanet& InPlanet);

	/** Returns the cells of such a planet that hold any cover, decor or object placed by hand, read from its objects.json alike */
	static TBitArray<> FindSavedCells(const AHexPlanet& InPlanet);

	/** Returns the sets of a layer (FindLayerSets) and after them the covers and decor of the other layer, as the developer asked on 2026-11-10,
	 *  so a dead tree may stand underground or bones on the surface, placed by hand alone, at most 254; OutOwn is how many are of the layer */
	static TArray<TObjectPtr<UHexPlanetObjectSet>> FindPlaceableSets(EHexPlanetLayer Layer, int32* OutOwn = nullptr);

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

	/** The objects the generation of the layer gives the cells for levels and biomes (indices into the biomes of the planet), as a planet without
	 *  objects.json gets them and as the editor asks; false before Init or without kinds */
	bool GenerateCells(TConstArrayView<int8> Levels, TConstArrayView<uint8> Biomes, TArray<FHexCellObjects>& OutObjects) const;

private:

	/** The objects of the land of the surface: each variant of a kind takes GenerateShare of the free cells of its biomes its levels allow, a
	 *  cover the highest of a noise of patches GeneratePatchCells of the kind across (ObjectNoiseScale of the config without it), densest in
	 *  their middles by GenerateDensityShares, and a decor any of them; the kinds in the order of the layer, so a cell keeps the first */
	void GenerateSurfaceObjects(TConstArrayView<int8> Levels, TConstArrayView<TCHAR> CellBiomeLetters, TArray<FHexCellObjects>& OutObjects) const;

	/** A mesh of a kind of object, drawn by a component per chunk */
	struct FSlot
	{
		int32 Set = 0;
		TObjectPtr<UStaticMesh> Mesh;

		/** Farthest the mesh reaches from its pivot across its up, for UHexPlanetObjectSet::bClipFootprint and what a hand places underground */
		float FootprintRadius = 0.0f;

		/** The scale factor and snow of the first variant that has the mesh, which an object placed by hand takes on a biome whose variant has not */
		float ScaleFactor = 1.0f;
		float Snow = 0.0f;
	};

	/** The ground of a cell flattened onto the plane of its frame, in buckets (HexPlanetObjects.cpp) */
	struct FCellGround;

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

	/** The meshes of every kind, and by kind and variant those of the variant, as indices of Slots, and by kind all of its meshes over its variants,
	 *  which FHexPlacedObject::Mesh counts in */
	TArray<FSlot> Slots;
	TArray<TArray<TArray<int32>>> VariantSlots;
	TArray<TArray<int32>> AllSlots;

	/** How many of Sets are of the layer itself (FindPlaceableSets) */
	int32 NumOwnSets = 0;

	/** The object Move drags, apart from the chunks (SetDragPreview), and the slot it shows */
	UPROPERTY(Transient)
	TObjectPtr<class UInstancedStaticMeshComponent> DragPreview;
	int32 DragPreviewSlot = INDEX_NONE;

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

		/** Objects placed by hand that are not drawn, as their level, biome or ground no longer allows them */
		int32 PlacedLeftOut = 0;
	};

	/** Finds how far each cell lies from a cleared cell, for the rims that keep clear of exits */
	void UpdateClearRings();

	/** Returns true if the components of a slot are drawn now */
	bool IsSlotShown(int32 Slot) const;

	/** The chunks nothing of which shows through the fog of war, a bit per chunk, whose objects are not drawn; empty while every chunk shows */
	TBitArray<> HiddenChunks;

	bool IsChunkShown(int32 Chunk) const { return !HiddenChunks.IsValidIndex(Chunk) || !HiddenChunks[Chunk]; }

public:
	/** Hides the objects of the chunks the fog of war covers whole (AHexPlanet::UpdateShroud), a bit per chunk, or shows all with none */
	void SetHiddenChunks(const TBitArray<>& InHidden);

private:

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

	/** Adds the objects of one kind on a cell to Out, and to OutPlaced, when given, each as an object placed by hand would stand there (UnpackCell) */
	void PlaceInCell(int32 Cell, int32 Set, int32 Density, bool bDecor, const TArray<FVector3f>& Triangles, TArray<FInstance>& Out,
		TArray<FHexPlacedObject>* OutPlaced = nullptr) const;

	/** Adds the objects placed by hand on a cell to Out, each where it stands on the ground of the cell, left out where its mesh would hang over a
	 *  void or stand in a wall since it was placed, as the maze changed; returns how many it left out */
	int32 PlacePlaced(int32 Cell, const TArray<FVector3f>& Triangles, TArray<FInstance>& Out) const;

	/** Makes the instance of an object placed by hand on the ground of its cell; false where no ground lies under it or its kind is gone */
	bool MakePlacedInstance(int32 Cell, const FHexPlacedObject& Object, const FCellGround& Ground, FInstance& Out) const;

	/** Returns the slot of the mesh of an object placed by hand among all the meshes of its kind, or INDEX_NONE */
	int32 GetPlacedSlot(int32 Cell, const FHexPlacedObject& Object) const;

	/** Gives the scale factor and snow a slot takes on a cell: of the variant of its biome when that variant has the mesh, else of the slot */
	void GetSlotLook(int32 Cell, int32 Slot, float& OutScaleFactor, float& OutSnow) const;

	/** Gives a component that draws a slot the materials of its kind, as the chunks have them */
	void SetSlotMaterials(class UStaticMeshComponent* Component, int32 Slot);

	/** Returns false, and why, if the mesh of a slot at a scale on a point of a cell would hang over a void or a cleared cell, or stand in a wall
	 *  or a river, the rules both placing by hand and drawing what was placed keep */
	bool IsFootprintFree(int32 Cell, int32 Slot, float Scale, const FVector3d& Position, bool bRivers, FString* OutProblem) const;

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
