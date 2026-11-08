// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetObjects.h"
#include "HexPlanetSurface.h"
#include "UObject/Object.h"
#include "HexPlanetPainter.generated.h"

class AHexPlanet;
class UHexPlanetConfig;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

/**
 *  Paints the levels or the biomes of the cells of a planet with a brush of one to three rings of cells. While the button is held the cells
 *  painted a level show at once as flat tiles in the colors of their new levels (PaintPreview), and when it is released the stroke goes to
 *  AHexPlanet::SetCells, which rebuilds the surface around them in the background; the tiles go once it is shown. A cell whose new level does
 *  not allow its biome takes a fitting one. Biomes show at once, as the planet only uploads them to a texture, so a biome stroke goes to the
 *  planet while it is painted, and only to the cells whose level allows that biome. Each stroke is one step of Undo and Redo, with the levels
 *  and biomes it changed. The pentagons are never mountains, so a mountain brush paints them as hills. It also puts forests, bushes, stones
 *  and decor on cells and takes them off (UHexPlanetObjects), and with no tool the left button drags the planet while the palette stays.
 */
/** What the left button does while painting is on */
enum class EHexPaintTool : uint8
{
	/** Nothing: it drags the planet, as with painting off, while the palette stays */
	None,
	Level,
	Biome,
	Relief,
	/** Puts the chosen kind of object on the cells (UHexPlanetObjects) */
	Object,
	/** Takes the cover and decor off the cells */
	Eraser,
	/** Lays rivers along the sides of cells, from the corner nearest the cursor to the next (UHexPlanetRivers) */
	River,
	/** Puts bridges on the sides with a river nearest the cursor */
	Bridge,
};

/** What a click of the brush in relief mode does to the mountains and hills under it, once per cell and stroke */
enum class EHexReliefTool : uint8
{
	/** The next version of the piece, among those with the same sides */
	Version,
	/** The next turn, among those that give the same set of neighbors */
	Turn,
	/** Mirrored or not */
	Mirror,
	/** A higher or lower factor on the height */
	Higher,
	Lower,
	/** Back to the hash */
	Reset,
};

UCLASS()
class UHexPlanetPainter : public UObject
{
	GENERATED_BODY()

public:

	/** Lowest and highest level a brush paints: deep sea and mountains */
	static constexpr int32 MinLevel = -2;
	static constexpr int32 MaxLevel = 2;

	/** Largest brush, in rings of cells around the one under the cursor plus one */
	static constexpr int32 MaxBrushSize = 3;

	/** Starts painting a planet, with the tiles of the preview on it */
	void Init(AHexPlanet* InPlanet);

	/** Returns true while the mouse paints instead of dragging the planet */
	bool IsActive() const { return bActive; }

	/** Turns painting on or off, which ends a stroke */
	void SetActive(bool bInActive);

	/** Returns the level the brush paints */
	int32 GetLevel() const { return Level; }

	/** Returns the tool of the left button; one is chosen at a time, and choosing one clears the others */
	EHexPaintTool GetTool() const { return Tool; }

	/** Returns true while the left button paints with a tool, rather than dragging the planet */
	bool HasTool() const { return Tool != EHexPaintTool::None; }

	/** Puts the tool down, so the left button drags the planet while the palette stays */
	void ClearTool();

	/** Returns true while the brush paints levels */
	bool IsPaintingLevels() const { return Tool == EHexPaintTool::Level; }

	/** Sets the level the brush paints, and makes it paint levels */
	void SetLevel(int32 InLevel) { Level = FMath::Clamp(InLevel, MinLevel, MaxLevel); Tool = EHexPaintTool::Level; }

	/** Returns true while the brush paints biomes */
	bool IsPaintingBiomes() const { return Tool == EHexPaintTool::Biome; }

	/** Returns true while the brush sets the relief of mountains and hills */
	bool IsPaintingRelief() const { return Tool == EHexPaintTool::Relief; }

	/** Returns the kinds of objects that can be put on cells, their names, colors and whether they are a cover */
	int32 GetNumObjectSets() const;
	FText GetObjectSetName(int32 Set) const;
	FLinearColor GetObjectSetColor(int32 Set) const;
	bool IsObjectSetCover(int32 Set) const;

	/** Returns the kind of object the brush puts, an index into UHexPlanetObjects::GetSets */
	int32 GetObjectSet() const { return ObjectSet; }

	/** Sets the kind of object the brush puts, and makes it put objects */
	void SetObjectSet(int32 InSet) { ObjectSet = InSet; Tool = EHexPaintTool::Object; }

	/** Returns the density a cover is put at, 1 to 3 */
	int32 GetObjectDensity() const { return ObjectDensity; }
	void SetObjectDensity(int32 InDensity) { ObjectDensity = FMath::Clamp(InDensity, 1, 3); }

	/** Returns true while the brush takes objects off cells */
	bool IsErasing() const { return Tool == EHexPaintTool::Eraser; }
	void SetEraser() { Tool = EHexPaintTool::Eraser; }

	/** Returns true while the brush lays rivers, or puts bridges; Ctrl takes them away */
	bool IsPaintingRivers() const { return Tool == EHexPaintTool::River; }
	void SetRiverTool() { Tool = EHexPaintTool::River; }
	bool IsPlacingBridges() const { return Tool == EHexPaintTool::Bridge; }
	void SetBridgeTool() { Tool = EHexPaintTool::Bridge; }

	/** Sets the point on the planet under the cursor, by which the river brush finds the nearest corner and side */
	void SetCursorPoint(const FVector& Point) { CursorPoint = Point; }

	/** Returns the tool of relief */
	EHexReliefTool GetReliefTool() const { return ReliefTool; }

	/** Sets the tool of relief, and makes the brush set relief */
	void SetReliefTool(EHexReliefTool InTool) { ReliefTool = InTool; Tool = EHexPaintTool::Relief; }

	/** Returns the biome the brush paints, an index into AHexPlanet::GetBiomes */
	int32 GetBiome() const { return Biome; }

	/** Sets the biome the brush paints, and makes it paint biomes */
	void SetBiome(int32 InBiome) { Biome = InBiome; Tool = EHexPaintTool::Biome; }

	/** Returns the number of biomes that can be painted, their names and their colors, the FileColor of their asset */
	int32 GetNumBiomes() const;
	FText GetBiomeName(int32 InBiome) const;
	FLinearColor GetBiomeColor(int32 InBiome) const;

	/** Returns the size of the brush, 1 to MaxBrushSize */
	int32 GetBrushSize() const { return BrushSize; }

	/** Sets the size of the brush */
	void SetBrushSize(int32 InSize) { BrushSize = FMath::Clamp(InSize, 1, MaxBrushSize); }

	/** Starts a stroke at the cell under the cursor */
	void BeginStroke(int32 Cell);

	/** Paints the cells on the way from the last cell of the stroke to this one */
	void ContinueStroke(int32 Cell);

	/** Gives the cells of the stroke their levels on the planet and keeps it as a step of Undo */
	void EndStroke();

	/** Returns true while a stroke is painted */
	bool IsPainting() const { return bPainting; }

	/** Returns true if there is a stroke to undo or redo */
	bool CanUndo() const { return !UndoSteps.IsEmpty(); }
	bool CanRedo() const { return !RedoSteps.IsEmpty(); }

	/** Undoes the last stroke, or redoes the last stroke undone */
	void Undo();
	void Redo();

	/** Returns true while the planet rebuilds the surface of changed levels */
	bool IsRebuilding() const;

	/** Returns the color of a level on the surface, from DA_HexPlanet */
	FLinearColor GetLevelColor(int32 InLevel) const;

	/** Returns the name of a level */
	static FText GetLevelName(int32 InLevel);

private:

	/** Old and new levels and biomes of the cells of one stroke */
	struct FStep
	{
		TMap<int32, int32> OldLevels;
		TMap<int32, int32> NewLevels;
		TMap<int32, int32> OldBiomes;
		TMap<int32, int32> NewBiomes;
		HexPlanetSurface::FReliefOverrides OldRelief;
		HexPlanetSurface::FReliefOverrides NewRelief;
		TMap<int32, FHexCellObjects> OldObjects;
		TMap<int32, FHexCellObjects> NewObjects;
		/** What stood on the sides of rivers before and after, by HexPlanetRivers::GetEdgeKey (UHexPlanetRivers::GetEdge) */
		TMap<uint64, uint8> OldRivers;
		TMap<uint64, uint8> NewRivers;
	};

	/** Planet painted */
	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	/** Values of the planet, with the colors of the levels and the height of the tiles */
	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetConfig> Settings;

	/** Flat tiles of the painted cells until the surface shows them */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> PaintPreview;

	/** Material of the tiles, M_HexPlanet with PreviewTile 1, so it draws them in their vertex colors */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	bool bActive = false;
	bool bPainting = false;
	EHexPaintTool Tool = EHexPaintTool::Level;
	int32 ObjectSet = 0;
	int32 ObjectDensity = 3;
	EHexReliefTool ReliefTool = EHexReliefTool::Version;
	int32 Level = 0;
	int32 Biome = 0;
	int32 BrushSize = 1;

	/** Last cell under the cursor in the stroke */
	int32 LastCell = INDEX_NONE;

	/** Point on the planet under the cursor, and the corner the river brush stood at last in the stroke */
	FVector CursorPoint = FVector::ZeroVector;
	int32 LastCorner = INDEX_NONE;

	/** The cells that meet at each corner of the grid, made the first time the river brush needs them */
	TArray<TArray<int32, TInlineAllocator<3>>> CornerCells;

	/** Stroke painted now */
	FStep Stroke;

	/** Cells given to the planet whose surface is not rebuilt yet, shown as tiles, with the level the surface still shows (X) and the new one (Y) */
	TMap<int32, FIntPoint> PendingLevels;

	TArray<FStep> UndoSteps;
	TArray<FStep> RedoSteps;

	/** Paints the brush around a cell */
	void PaintAround(int32 Cell);

	/** Lays or takes away a river from the last corner to the corner of the cell nearest the cursor, or puts or takes away a bridge */
	void PaintRiver(int32 Cell);

	/** Returns the sides of the shortest way along sides of cells between two corners, by HexPlanetRivers::GetEdgeKey */
	TArray<uint64> FindCornerPath(int32 From, int32 To);

	/** Gives the planet the levels and biomes of a step and shows the levels as tiles until it is rebuilt */
	void Apply(const TMap<int32, int32>& Levels, const TMap<int32, int32>& Biomes, const HexPlanetSurface::FReliefOverrides& Relief = {},
		const TMap<int32, FHexCellObjects>& Objects = {}, const TMap<uint64, uint8>& Rivers = {});

	/** Returns the relief a click of the tool gives a cell, or false if it cannot change it, as it has no piece or its neighbors fix it */
	bool GetToolRelief(int32 Cell, HexPlanetSurface::FReliefOverride& OutRelief) const;

	/** Drops the tiles once the planet shows every level given to it */
	void OnLevelsRebuilt();

	/** Returns the level a brush gives a cell, as a pentagon is never a mountain */
	int32 GetPaintedLevel(int32 Cell, int32 InLevel) const;

	/** Returns the radius at which the tile of a cell lies */
	double GetTileRadius(int32 OldLevel, int32 NewLevel) const;

	/** Builds the tiles of the pending levels and of the stroke */
	void UpdatePreview();
};
