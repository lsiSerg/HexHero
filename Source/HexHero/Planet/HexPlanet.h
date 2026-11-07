// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexPlanetFile.h"
#include "HexPlanetSurface.h"
#include "HexSphereGrid.h"
#include "HexWorldMap.h"
#include "Tasks/Task.h"
#include "HexPlanet.generated.h"

class ADirectionalLight;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UNiagaraComponent;
class UNiagaraSystem;
class UProceduralMeshComponent;
class UTexture2D;
class UTextureCube;
class UHexPlanetKitConfig;
class UHierarchicalInstancedStaticMeshComponent;
class UHexBiomeConfig;
class UHexPlanetCloudConfig;
class UHexPlanetConfig;
class UHexPlanetObjects;
class UHexPlanetWaterConfig;
struct FProcMeshSection;

/**
 *  Prototype of the world map as a planet: a hex sphere whose cells get a level from noise. Each cell is a piece of the kit of its level,
 *  chosen by which neighbors share its level and bent onto the cell (HexPlanetKit), and drawn by M_HexPlanet with the texture layers of its biome.
 *  The 12 pentagons are flat tiles of their level, never mountains, each with an aurora rising into the sky (BuildAuroras), a Niagara system.
 *  One mesh component per chunk of the grid, a triangle of a face of the icosahedron, with two sections, the second with the mountains, so
 *  what lies behind the planet or off the screen is culled and a change of levels rebuilds only the chunks it reaches (SetCellLevels), in the background. The planet stays at the world origin.
 *  The water is a cap around the point under the camera, drawn by M_HexPlanetWater from a cubemap of the sea, see HexPlanetSeaField.
 *  The debug views of the flat map work here too: hex.Wireframe, hex.WireframeScope, hex.WireframeTarget, where mountains are not terrain, and hex.Water.
 */
UCLASS()
class AHexPlanet : public AActor
{
	GENERATED_BODY()

	/** Root of the planet, which holds no mesh itself */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> Surface;

	/** Cells of the planet, one component per chunk of the grid with two sections, the second with the mountains, so a chunk is culled on its
	 *  own and rebuilding it recreates the render state of that chunk alone */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> SurfaceChunks;

	/** The coarse mesh of each chunk, drawn beyond CoarseDistance of DA_HexPlanet in place of its SurfaceChunks, and with them in the band
	 *  around it, where they dissolve into each other (UpdateChunkLod), also in the debug views */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> CoarseChunks;

	/** Boulders at the foot of the mountains on the edge of ranges, one component per mesh of FHexPlanetBoulders, with the rock layer of the
	 *  biome of their mountain as the first custom data of each instance (UpdateBoulders) */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> BoulderComponents;

	/** Forests, bushes, flowers, stones and decor on the cells, drawn as instances on the ground of their cells (objects.json) */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UHexPlanetObjects> Objects;

	/** Floes of sea ice on the frozen sea, one component per mesh of FHexPlanetFloes (UpdateFloes), which M_HexPlanetFloe moves */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> FloeComponents;

	/** Cap of water around the point under the camera, dense close to it, which turns with the camera every frame */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> Water;

	/** Grid lines of the cells of the sea at the height of the sea, drawn over the water, as the grid of the seabed fades out under it */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> WaterGrid;

	/** Shell of clouds at CloudHeight above the sea, below the camera, which hands over to SkyClouds as the camera comes close */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> Clouds;

	/** The same shell, which its material lifts to CloudSkyHeight above the close camera as a sky; each is hidden while it is faded out */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> SkyClouds;

	/** The auroras over the 12 pentagons, one NS_HexPlanetAurora each (BuildAuroras) */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> Auroras;

	/** ParameterCollection, loaded, which Tick gives the sun and the radius of the planet */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> LoadedParameterCollection;

	/** Holds no mesh: the parent of the copies of the chunks that hex.Wireframe draws */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> Wireframe;

	/** Triangles of the whole surface or of the part that hex.WireframeTarget chooses, drawn by hex.Wireframe: a copy of each chunk and of
	 *  its coarse mesh, shown with them, so they show which of them is drawn, made the first time they are needed */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> WireChunks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> WireCoarseChunks;

	/** Triangles of the cell under the mouse and its neighbors, drawn by hex.Wireframe in their own colors over the others */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> HoverWireframe;

	/** Material of the cells, whose grid lines can be hidden */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	/** Masked copy of the material of the cells, which hides the cells around the cursor while only their triangles are drawn */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceCutMaterial;

	/** Masked copies of the material of the cells for the pieces (LodSide 1) and the coarse mesh (LodSide 2) of a chunk in the band around
	 *  CoarseDistance, which keep complementary pixels, so the chunk dissolves from one into the other */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LodNearMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LodFarMaterial;

	/** By chunk: 0 while it draws only its pieces, 1 in the band where it draws both, 2 while it draws only its coarse mesh */
	TArray<uint8> ChunkLodStates;

	/** Material of the wireframe of the whole surface */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WireframeMaterial;

	/** Material of the wireframe of the cells around the mouse, which takes their colors from the vertices */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoverWireframeMaterial;

	/** Water material with the sea field and the sun */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaterMaterial;

	/** Material of the grid lines over the water */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaterGridMaterial;

	/** Material of the clouds, with the sun */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	/** Material of the clouds as a sky, with CloudSkyLayer 1 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkyCloudMaterial;

	/** Loaded configs, or the class defaults of those that are missing */
	UPROPERTY(Transient)
	TObjectPtr<UHexPlanetCloudConfig> LoadedCloudConfig;

	UPROPERTY(Transient)
	TObjectPtr<UHexPlanetConfig> Settings;

	UPROPERTY(Transient)
	TObjectPtr<UHexPlanetWaterConfig> LoadedWaterConfig;

	/** Cubemap of the sea under the water, see HexPlanetSeaField */
	UPROPERTY(Transient)
	TObjectPtr<UTextureCube> SeaField;

	/** Cubemap of the stretches of the coast, at which phase each breaks and how far from its ends, for the breakers of the water and their
	 *  run-up on the beach, see HexPlanetSeaField */
	UPROPERTY(Transient)
	TObjectPtr<UTextureCube> ShoreField;

	/** Where the ground crosses the sea, by chunk (HexPlanetSurface::FBuild::Coasts), for the distance to the real coast of the sea field */
	TArray<TArray<FVector3f>> ChunkCoasts;

	/** Sun of the level, whose direction the water needs */
	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> Sun;

public:

	/** Grid, generation, colors, outlines and the values of M_HexPlanet and M_HexPlanetWireframe */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UHexPlanetConfig> PlanetConfig = TSoftObjectPtr<UHexPlanetConfig>(FSoftObjectPath(TEXT("/Game/HexPlanet/DA_HexPlanet.DA_HexPlanet")));

	/** Values of M_HexPlanetWater */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UHexPlanetWaterConfig> WaterConfig = TSoftObjectPtr<UHexPlanetWaterConfig>(FSoftObjectPath(TEXT("/Game/HexPlanet/DA_HexPlanetWater.DA_HexPlanetWater")));

	/** Material that draws the vertex colors and the grid lines from the first UV */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UMaterialInterface> Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanet.M_HexPlanet")));

	/** Unlit wireframe of the debug view, from Tools/create_planet_assets.py */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UMaterialInterface> WireframeMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetWireframe.M_HexPlanetWireframe")));

	/** Material of the water, from Tools/create_planet_assets.py */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UMaterialInterface> WaterMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetWater.M_HexPlanetWater")));

	/** Material of the grid lines over the water, from Tools/create_planet_assets.py */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UMaterialInterface> WaterGridMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetWaterGrid.M_HexPlanetWaterGrid")));

	/** Pieces that give the cells their relief, from Tools/HexPlanetKit/import_planet_kit.py. A cell without a piece is a flat tile */
	UPROPERTY(EditAnywhere, Category="Planet|Kit")
	TSoftObjectPtr<UHexPlanetKitConfig> KitConfig = TSoftObjectPtr<UHexPlanetKitConfig>(FSoftObjectPath(TEXT("/Game/HexPlanet/Kit/DA_HexPlanetKit.DA_HexPlanetKit")));

	/** Constructor */
	AHexPlanet();

	/** Returns the folder in Content/Data/Planets of the open planet, empty if there was none and it was generated from the defaults */
	const FString& GetPlanetFolder() const { return PlanetFolder; }

	/** Returns planet.json of the open planet */
	const FHexPlanetManifest& GetManifest() const { return Manifest; }

	/** Returns true if the planet has a folder; a new planet of the menu has none until it is saved */
	bool IsSaved() const { return !PlanetFolder.IsEmpty(); }

	/** Writes the planet into the folder of a name, over a planet that is there, and makes it the folder of this one, with levels.txt and
	 *  biomes.txt, painted or generated. Returns false with an error if it could not */
	bool SaveAs(const FString& Name, FString& OutError);

	/** Returns true if leaving the planet now would lose something: it is new and not saved, or its cells were painted since the last save */
	bool HasUnsavedChanges() const { return !IsSaved() || bLevelsEdited; }

	/** Writes levels.txt and biomes.txt of a saved planet. Returns false with an error if it could not */
	bool SaveLevels(FString& OutError);

	/** Records that the levels or biomes were painted, so they differ from the files or the generation */
	void MarkLevelsEdited() { bLevelsEdited = true; }

	/** Sets the relief of cells by hand, an empty override going back to the hash, and rebuilds them and their neighbors */
	void SetReliefOverrides(const HexPlanetSurface::FReliefOverrides& Changes);

	/** Returns the relief set by hand of a cell, or an empty one */
	HexPlanetSurface::FReliefOverride GetReliefOverride(int32 Cell) const;

	/** Returns the choice of version and of turn of the piece of a cell as shown, and of how many (Y), 0 for a cell without a piece */
	FIntPoint GetCellVersion(int32 Cell) const { return CellVersions.IsValidIndex(Cell) ? CellVersions[Cell] : FIntPoint::ZeroValue; }
	FIntPoint GetCellTurnChoice(int32 Cell) const { return CellTurnChoices.IsValidIndex(Cell) ? CellTurnChoices[Cell] : FIntPoint::ZeroValue; }

	/** Returns whether the piece of a cell is mirrored as shown */
	bool IsCellMirrored(int32 Cell) const { return CellMirrored.IsValidIndex(Cell) && CellMirrored[Cell]; }

	/** Returns true if the levels or biomes were painted since they were last saved */
	bool HasUnsavedLevels() const { return bLevelsEdited; }

	/** Returns the grid, empty until the planet is built */
	const FHexSphereGrid& GetGrid() const { return Grid; }

	/** Returns the radius of the sea */
	double GetRadius() const { return Radius; }

	/** Returns the radius of the top of a cell */
	double GetCellTopRadius(int32 Cell) const;

	/** Returns the level of a cell: -2 deep sea, -1 shallows, 0 plains, 1 hills, 2 mountains */
	int32 GetCellLevel(int32 Cell) const { return Levels.IsValidIndex(Cell) ? Levels[Cell] : 0; }

	/** Sets the levels of cells (-2..2), cell -> level. A pentagon given a mountain becomes hills. GetCellLevel returns the new levels at once;
	 *  the surface, the sea field and the water grid catch up when the rebuild started in the background ends. Calls while one runs are merged and run after it */
	void SetCellLevels(const TMap<int32, int32>& NewLevels) { SetCells(NewLevels, {}); }

	/**
	 *  Sets the levels of cells, then their biomes (cell -> index in GetBiomes), where a biome its level does not allow is left out. A cell whose
	 *  level changed and whose biome it does not allow takes the biome most of its neighbors have that it allows, else the default of its level.
	 *  Biomes show at once, as M_HexPlanet reads them from CellBiomeTexture; a cell of the sea that freezes or thaws rebuilds the sea field
	 *  in the background, as a change of levels does
	 */
	void SetCells(const TMap<int32, int32>& NewLevels, const TMap<int32, int32>& NewBiomes);

	/** Returns the biomes a planet can have, those with a PlanetLetter, by their letter; a cell holds an index into it */
	const TArray<TObjectPtr<UHexBiomeConfig>>& GetBiomes() const { return BiomeConfigs; }

	/** Returns the biome of a cell, an index into GetBiomes */
	int32 GetCellBiome(int32 Cell) const { return CellBiomes.IsValidIndex(Cell) ? CellBiomes[Cell] : 0; }

	/** Returns the biome of every cell, in the order of the grid */
	const TArray<uint8>& GetCellBiomes() const { return CellBiomes; }

	/** Returns the PlanetLetter of the biome of a cell, or 0 */
	TCHAR GetCellBiomeLetter(int32 Cell) const { return CellBiomes.IsValidIndex(Cell) && BiomeLetters.IsValidIndex(CellBiomes[Cell]) ? BiomeLetters[CellBiomes[Cell]] : TEXT('\0'); }

	/** Returns the rock layer of the biome of a cell, as the surface draws it on slopes and mountains */
	int32 GetCellRockLayer(int32 Cell) const;

	/** Gives the triangles of the surface of a cell as shown now, three corners each, empty before it is built. Reads only, so any thread may
	 *  call it while the surface does not change */
	void GetCellTriangles(int32 Cell, TArray<FVector3f>& OutCorners) const;

	/** Returns the forests, bushes, stones and decor of the cells */
	UHexPlanetObjects* GetObjects() const { return Objects; }

	/** Returns true if a biome, an index into GetBiomes, may lie on a level */
	bool IsBiomeAllowed(int32 Biome, int32 Level) const;

	/** Returns true while a rebuild of changed levels runs or waits */
	bool IsRebuilding() const { return bLevelsPending || RebuildTask.IsValid(); }

	/** Returns the level of every cell, in the order of the grid */
	const TArray<int8>& GetLevels() const { return Levels; }

	/** Returns the levels that the surface shown now was built from, which differ from GetLevels in the cells whose rebuild has not ended yet */
	const TArray<int8>& GetShownLevels() const { return ShownLevels; }

	/** Broadcast on the game thread after the surface of changed levels is shown. IsRebuilding is still true then if more changes wait */
	FSimpleMulticastDelegate OnLevelsRebuilt;

	/** Finds the cell that a ray from the world hits, or INDEX_NONE if it misses the planet */
	int32 FindCellAlongRay(const FVector& Origin, const FVector& Direction) const;

	/** Outlines a cell, or nothing for INDEX_NONE, and its neighbors while the wireframe draws them too. With a brush of 1 to 3 rings of
	 *  cells around it, outlines the edge of the brush instead */
	void SetHoveredCell(int32 Cell, int32 BrushSize = 0);

	/** Returns the text of the debug panel for a cell */
	FString GetCellDebugText(int32 Cell) const;

	/** Material of the clouds, from Tools/create_planet_assets.py */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UMaterialInterface> CloudMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetClouds.M_HexPlanetClouds")));

	/** The aurora over each pentagon, a Niagara system that an artist tunes by hand, local to the pentagon with Z up from the planet */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UNiagaraSystem> AuroraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/HexPlanet/NS_HexPlanetAurora.NS_HexPlanetAurora")));

	/** Values of the planet that any material may read, such as those of Niagara, which AHexPlanet gives no instance: SunDirection and
	 *  PlanetRadius, from Tools/create_planet_assets.py */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/HexPlanet/MPC_HexPlanet.MPC_HexPlanet")));

	/** Clouds, their shadows and their look, which the materials take as parameters */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UHexPlanetCloudConfig> CloudConfig = TSoftObjectPtr<UHexPlanetCloudConfig>(FSoftObjectPath(TEXT("/Game/HexPlanet/DA_HexPlanetClouds.DA_HexPlanetClouds")));

	/** Shows or hides the grid lines between the cells */
	void SetGridVisible(bool bVisible);

	/** Returns true while the grid lines are shown */
	bool IsGridVisible() const { return bGridVisible; }

	/** Turns the water cap to the point under the camera and gives the water the direction of the sun */
	virtual void Tick(float DeltaTime) override;

	/** Returns true once the planet is built, a few frames after it begins to play; until then the grid is empty and the radius 0 */
	bool IsBuilt() const { return bBuilt; }

protected:

	/** Reads the configs and the manifest and shows the loading screen, so the build that follows does not freeze a blank window */
	virtual void BeginPlay() override;

	/** Waits for a rebuild that still runs, as it reads the grid */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	/** Builds the grid, the surface, the objects, the sea and the sky, which takes a second or two on a large planet */
	void BuildPlanet();

	/** Shows and hides the loading screen over the viewport while the planet is built */
	void ShowLoadingScreen();
	void HideLoadingScreen();

	/** True once BuildPlanet ran */
	bool bBuilt = false;

	/** Frames ticked before the build, so the loading screen has been drawn when the build holds the game thread */
	int32 FramesBeforeBuild = 0;

	TSharedPtr<SWidget> LoadingScreen;

	/** Grid of the cells */
	FHexSphereGrid Grid;

	/** Folder in Content/Data/Planets of the open planet */
	FString PlanetFolder;

	/** planet.json of the open planet */
	FHexPlanetManifest Manifest;

	/** True if the levels were painted since they were last saved */
	bool bLevelsEdited = false;

	/** Level of each cell */
	TArray<int8> Levels;

	/** Biomes a planet can have, by PlanetLetter, and their letters */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHexBiomeConfig>> BiomeConfigs;
	TArray<TCHAR> BiomeLetters;

	/** Biome of each cell, an index into BiomeConfigs */
	TArray<uint8> CellBiomes;

	/**
	 *  Two pixels per cell, at twice the column of CellMarks, of the layers of the biome textures that M_HexPlanet blends: the first holds the
	 *  terrain, rock and beach layers of the biome of the cell (255 for no beach), the second the terrain layers of its neighbors, two to a byte,
	 *  neighbor K in the low half of byte K / 2 if K is even. So M_HexPlanet blends a cell into its neighbors without a rebuild of the mesh
	 */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CellBiomeTexture;
	TArray<FColor> BiomePixels;

	/** The values of each texture layer of UHexTerrainLayersConfig, which M_HexPlanet reads as LayerParams (CreateParamsTexture) */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LayerParamsTexture;

	/** Frozen cells of the sea the sea field shown now was built from, see GetFrozenCells */
	TArray<uint8> ShownFrozen;

	/** Reads the biomes that have a PlanetLetter */
	void LoadBiomeConfigs();

	/** Takes the biomes from biomes.txt of the planet, or generates them when it has none or it is not valid, and fits them to the levels */
	void LoadBiomes();

	/** Writes the generated levels, biomes and objects of a saved planet that had no such files, so it keeps them when the generation changes */
	void WriteGeneratedFiles(bool bLevels, bool bBiomes, bool bObjects);

	/** Returns the biome a cell takes on its level when its own is not allowed there: the one most of its neighbors have that it allows,
	 *  else the default of the level, else the first allowed */
	int32 FindFittingBiome(int32 Cell) const;

	/** Returns 1 for each cell of the sea whose biome is frozen and each cell of land next to one, which the sea field blends into ice */
	TArray<uint8> GetFrozenCells() const;

	/** Creates CellBiomeTexture from the biomes of all cells */
	void CreateBiomeTexture();

	/** Writes the pixels of cells into BiomePixels and uploads the texture */
	void UpdateBiomePixels(TConstArrayView<int32> ChangedCells);

	/** Writes the two pixels of a cell into BiomePixels */
	void WriteBiomePixels(int32 Cell);

	/** Radius of the sea */
	double Radius = 0.0;

	/** Mean area of the cells on the unit sphere */
	double MeanCellArea = 0.0;

	/** Cell outlined now */
	int32 HoveredCell = INDEX_NONE;

	/** Brush and neighbors outlined around it */
	int32 HighlightBrushSize = 0;
	bool bHighlightNeighbors = false;

	/** One pixel per cell, row by row, of the marks M_HexPlanet and M_HexPlanetWaterGrid draw on the grid line of a cell: bits of the sides
	 *  to outline in two layers of color (SetHoveredCell). The surface finds its pixel from its third UV (HexPlanetSurface::GetCellUV) */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CellMarks;

	/** Pixels of CellMarks, and the cells marked in them now */
	TArray<FColor> MarkPixels;
	TArray<int32> MarkedCells;

	/** Creates CellMarks with no marks */
	void CreateCellMarks();

	/** Marks cells, cell -> mark, and clears the marks of the other cells; the blue of the marks is left to SetWaterHiddenCells */
	void SetCellMarks(const TMap<int32, FColor>& Marks);

	/** Sets blue of CellMarks in the cells whose water the debug view hides, every cell when bAll, so the surface draws the grid lines and
	 *  outlines on their seabed, as the strip of the water grid that draws them on the water is hidden with it */
	void SetWaterHiddenCells(TConstArrayView<int32> Cells, bool bAll);

	/** Cells whose blue SetWaterHiddenCells set, and whether it set all */
	TArray<int32> WaterHiddenCells;
	bool bAllWaterHidden = false;

	/** Cells whose alpha of CellMarks holds sides of the rim of the hidden water, which the water grid outlines */
	TArray<int32> WaterRimCells;

	/** Sends MarkPixels to CellMarks */
	void UploadCellMarks();

	/** Pieces of the kits, read once and shared with the rebuilds in the background */
	TSharedPtr<const HexPlanetSurface::FKit> SurfaceKit;

	/** Values of the config that the surface needs, copied for the rebuilds in the background */
	HexPlanetSurface::FParams SurfaceParams;

	/** Mean height of the peaks of mountain cells at the first build, which V of the first UV keeps measuring by, so the grid fade does not jump */
	double MeanPeak = 1.0;

	/** Levels and massif steps the surface shown now was built from */
	TArray<int8> ShownLevels;
	TArray<int32> ShownMassifSteps;

	/** Piece of each cell, or INDEX_NONE for a flat tile, and its turns */
	TArray<int32> CellPieces;
	TArray<int8> CellTurns;
	TArray<bool> CellMirrored;

	/** The version of each piece and of its allowed turns, the choice (X) of how many (Y), see HexPlanetSurface::FBuild */
	TArray<FIntPoint> CellVersions;
	TArray<FIntPoint> CellTurnChoices;

	/** The relief set by hand, by cell (relief.json), and the one the surface shown was built with */
	HexPlanetSurface::FReliefOverrides ReliefOverrides;
	HexPlanetSurface::FReliefOverrides ShownReliefOverrides;

	/** Factor of the height of each cell, see HexPlanetSurface::FindRelief */
	TArray<float> CellRelief;

	/** Where the mesh of a cell lies in the sections of the surface */
	using FCellMeshRange = HexPlanetSurface::FCellMesh;

	/** Mesh of each cell */
	TArray<FCellMeshRange> CellMeshes;

	/** What a rebuild in the background reads and makes */
	struct FLevelRebuild;

	/** Rebuild of changed levels running in the background, and what it makes */
	UE::Tasks::FTask RebuildTask;
	TSharedPtr<FLevelRebuild> Rebuild;

	/** True while levels changed that no rebuild has started for yet */
	bool bLevelsPending = false;

	/** True after the surface changed, so the debug view copies its sections again */
	bool bSurfaceChanged = false;

	/** True while the grid lines are shown */
	bool bGridVisible = true;

	/** Debug view drawn now, to find when it changes */
	FHexDebugViewSettings AppliedView;

	/** Geometry whose triangles the wireframe of the whole surface draws, or unset while it draws none */
	TOptional<EHexWireframeTarget> AppliedWireTarget;

	/** Cell whose wireframe is drawn around the mouse */
	int32 AppliedWireCell = INDEX_NONE;

	/** Cell and number of cells around it whose water is hidden */
	int32 AppliedWaterHideCell = INDEX_NONE;
	int32 AppliedWaterHideCount = 0;

	/** True while the surface hides the cells around the cursor, as only their triangles are drawn */
	bool bAppliedCut = false;

	/** Cell and number of cells around it that the surface hides */
	int32 AppliedCutCell = INDEX_NONE;
	int32 AppliedCutCount = 0;

	/** Returns true if a section of the surface holds mountains, which the wireframe does not count as terrain */
	static bool IsMountainSection(int32 Section) { return Section % 2 == 1; }

	/** Returns a section of the surface, two per chunk, or null before it is built */
	const FProcMeshSection* GetSurfaceSection(int32 Section) const;

	/** Returns true if the wireframe draws a section for a target */
	static bool IsWireTarget(int32 Section, EHexWireframeTarget Target);

	/** Follows the console variables of the debug view: the wireframe and the water */
	void UpdateDebugView();

	/**
	 *  Returns the material of part Part of a chunk, 0 and 1 its sections of pieces and 2 its coarse mesh: a masked LOD copy in the band, else
	 *  the masked copy that hides the cells around the cursor if bCut and the wireframe target chooses that part, else the material of the cells
	 */
	UMaterialInterface* GetChunkMaterial(int32 Chunk, int32 Part, bool bCut, EHexWireframeTarget Target) const;

	/** Shows the pieces or the coarse mesh of each chunk by its distance from the camera, and both with the LOD materials in the band */
	void UpdateChunkLod(const FVector& CameraLocation);

	/** Builds the wireframe of the cell under the mouse and its neighbors */
	void BuildHoverWireframe(const FHexDebugViewSettings& View);

	/** Hides the water of the cell under the mouse, and with the water view that asks for it of its neighbors */
	void UpdateWaterHide(const FHexDebugViewSettings& View);

	/** Gives a material that hides cells the centers of a cell, its neighbors and theirs, of which it hides the first Count */
	void SetHideCells(UMaterialInstanceDynamic* Target, int32 Cell, int32 Count) const;

	/** Generates the levels by the generation settings of the planet, see HexPlanetGenerator */
	void BuildLevels();

	/** Takes the levels from levels.txt of the planet, or generates them when it has none or it is not valid */
	void LoadLevels();

	/** Reads the kit and builds two mesh sections per chunk of the grid, the second of them with the mountains */
	void BuildSurface();

	/** Shows the sections of the faces of a build, and keeps where its cells lie in them */
	void ApplySurface(HexPlanetSurface::FBuild& Build);

	/** Reads relief.json of the planet into ReliefOverrides */
	void LoadRelief();

	/** Writes ReliefOverrides to relief.json of a folder */
	bool SaveRelief(const FString& Folder, FString& OutError) const;

	/** Makes a component per mesh of the boulders of the kit */
	void CreateBoulderComponents(const UHexPlanetKitConfig* Config);

	/** Gives the components of the boulders the instances of BouldersByChunk, after a rebuild or a change of biomes */
	void UpdateBoulders();

	/** Boulders of each chunk, from the build that last built it */
	TArray<TArray<HexPlanetSurface::FBoulder>> BouldersByChunk;

	/** Makes a component per mesh of the floes of the kit, slabs first and then shards */
	void CreateFloeComponents(const UHexPlanetKitConfig* Config);

	/** Places the floes on the frozen sea of ShownLevels and gives them to their components, at the start and whenever the ice changed */
	void UpdateFloes();

	/** How many meshes the floes have, the slabs first and then the shards: FloeComponents holds them all over the shallows, which cast
	 *  shadows, and then all again over the deep sea, which do not */
	int32 NumFloeMeshes = 0;
	int32 NumFloeSlabs = 0;

	/** Builds the cubemap of the sea, from the depth of the seabed, the distance to land and the ice of each cell */
	void BuildSeaField();

	/** Makes the cubemaps of the sea and of the shore from their pixels (HexPlanetSeaField::BuildPixels, one after the other) and gives them to the
	 *  water and the surface */
	void ApplySeaField(const TArray<FColor>& Pixels);

	/** Gives a material of the water or the surface the cubemaps of the shore and the sea, once there are */
	void SetShoreField(UMaterialInstanceDynamic* Target) const;

	/** Gives a material of the surface the layers of the biomes of the cells and the beach of each layer (BeachLayers0..2) */
	void SetBiomeLayers(UMaterialInstanceDynamic* Target) const;

	/** Builds the cap of water and its material */
	void BuildWater();

	/** Builds the grid lines of the cells of the sea over the water, and their material */
	void BuildWaterGrid();

	/** Starts a rebuild of the levels changed since the surface shown now was built, unless one runs */
	void StartRebuild();

	/** Shows what the rebuild in the background made */
	void FinishRebuild();

	/** Builds the shell of clouds and its material */
	void BuildClouds();

	/** Places an aurora (AuroraSystem) at the height of the sea over each pentagon, turned so its Z points away from the center of the planet */
	void BuildAuroras();

	/** Gives a material that draws clouds or their shadows the radius of the planet and every float and color of the cloud config, by their names */
	void SetCloudParameters(UMaterialInstanceDynamic* Target) const;
};
