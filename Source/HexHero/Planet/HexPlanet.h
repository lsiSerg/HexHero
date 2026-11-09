// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexPlanetMeshComponent.h"
#include "HexPlanetSurface.h"
#include "HexPlanetUnderground.h"
#include "HexPlanetWalls.h"
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
class UPostProcessComponent;
class USkyLightComponent;
class UTexture2D;
class UTextureCube;
class UHexPlanetKitConfig;
class UHierarchicalInstancedStaticMeshComponent;
class UHexBiomeConfig;
class UHexPlanetCloudConfig;
class UHexPlanetConfig;
class UHexPlanetMotes;
class UHexPlanetObjects;
class UHexPlanetRivers;
class UHexPlanetWaterConfig;

/**
 *  Prototype of the world map as a planet: a hex sphere whose cells get a level from noise. Each cell is a piece of the kit of its level,
 *  chosen by which neighbors share its level and bent onto the cell (HexPlanetKit), and drawn by M_HexPlanet with the texture layers of its biome.
 *  The 12 pentagons are flat tiles of their level, never mountains, each with an aurora rising into the sky (BuildAuroras), a Niagara system.
 *  One mesh component per chunk of the grid, a triangle of a face of the icosahedron, with two sections, the second with the mountains, so
 *  what lies behind the planet or off the screen is culled and a change of levels rebuilds only the chunks it reaches (SetCellLevels), in the background. The planet stays at the world origin.
 *  The water is a cap around the point under the camera, drawn by M_HexPlanetWater from a cubemap of the sea, see HexPlanetSeaField.
 *  The debug views of the flat map work here too: hex.Wireframe, hex.WireframeScope, hex.WireframeTarget, where mountains are not terrain, and hex.Water.
 *
 *  A planet has two layers, each an AHexPlanet (UHexPlanetLayers): the surface, placed in the level, spawns the underground with
 *  DA_HexUnderground, whose features (sea, clouds, haze, auroras, rivers, generated objects) are off. The underground reads or generates its
 *  data in the background once the surface is built and builds its meshes only when it is first shown. Only the layer shown is drawn and does
 *  the work of a frame (SetPresented).
 */
UCLASS()
class AHexPlanet : public AActor
{
	GENERATED_BODY()

	/** Root of the planet, which holds no mesh itself */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UProceduralMeshComponent> Surface;

	/** Cells of the planet, one component per chunk of the grid with two sections, the second with the mountains, so a chunk is culled on its
	 *  own and rebuilding it recreates the render state of that chunk alone. Only the chunks near the camera hold them (ChunkPieces) */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHexPlanetMeshComponent>> SurfaceChunks;

	/** The coarse mesh of each chunk, drawn beyond CoarseDistance of DA_HexPlanet in place of its SurfaceChunks, and with them in the band
	 *  around it, where they dissolve into each other (UpdateChunkLod), also in the debug views */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHexPlanetMeshComponent>> CoarseChunks;

	/** Boulders at the foot of the mountains on the edge of ranges, one component per mesh of FHexPlanetBoulders, with the rock layer of the
	 *  biome of their mountain as the first custom data of each instance (UpdateBoulders) */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> BoulderComponents;

	/** Forests, bushes, flowers, stones and decor on the cells, drawn as instances on the ground of their cells (objects.json) */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UHexPlanetObjects> Objects;

	/** The rivers along the sides of the cells and the bridges over them */
	UPROPERTY(VisibleAnywhere, Category="Planet")
	TObjectPtr<UHexPlanetRivers> Rivers;

	/** The motes over the passages and the spores of the underground (UHexPlanetConfig::bMotes), made when the layer is built */
	UPROPERTY(Transient)
	TObjectPtr<UHexPlanetMotes> Motes;

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

	/** Light of the sky, the same from every direction, as up differs over the planet and a sky light lights by directions of the world, so
	 *  shadows are not black (SkyLightColor, SkyLightIntensity of DA_HexPlanet); Lumen shades it by what covers each point */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	/** The plain white cubemap the sky light takes */
	UPROPERTY(Transient)
	TObjectPtr<UTextureCube> SkyCubemap;

	/** Lays the haze of the air over every view, unbound, with HazeMaterial */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPostProcessComponent> HazeVolume;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HazeMaterial;

public:

	/** Post process of the haze of the air, from Tools/create_planet_assets.py, with the Haze* values of DA_HexPlanetClouds */
	UPROPERTY(EditAnywhere, Category="Planet")
	TSoftObjectPtr<UMaterialInterface> HazeMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetHaze.M_HexPlanetHaze")));

private:

	/** Material of the cells, whose grid lines can be hidden */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	/** Material of the tiles of painting (GetPreviewMaterial) */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

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

	/**
	 *  By chunk, whether its pieces are on the GPU (2), being built in the background (1) or neither (0). The surface keeps them nowhere else,
	 *  so a chunk that comes within PieceStreamAhead past the band of DA_HexPlanet has them built again (StartStream) and one further than
	 *  PieceStreamKeep past that frees them, as it draws its coarse mesh there (UpdateChunkLod)
	 */
	TArray<uint8> ChunkPieces;

	/** The bounds of the pieces of each chunk, kept while it has none, by which UpdateChunkLod measures how far it is */
	TArray<FBoxSphereBounds> ChunkBounds;

	/** By chunk, how many times a build of every kind showed it, so pieces built in the background before a rebuild of levels made the
	 *  chunk anew are dropped */
	TArray<uint32> ChunkGenerations;

	/** Where UpdateChunkLod last measured from, by which a rebuild of levels gives pieces only to the chunks that want them */
	FVector LodCamera = FVector::ZeroVector;
	bool bHasLodCamera = false;

	/** Where the camera is about to jump to, whose chunks get their pieces first (SetPiecePrefetch) */
	TOptional<FVector> PrefetchCamera;

	/** Builds the pieces of chunks on the game thread, in parallel, for a camera that came near chunks without them, as their coarse meshes
	 *  next to the pieces of their neighbors would leave gaps */
	void BuildPiecesNow(TArray<int32> Chunks);

	/** The build of every chunk at the start, whose pieces go to the GPU for the chunks the camera wants in the first frames, while it finds
	 *  its place, and then is freed */
	TUniquePtr<HexPlanetSurface::FBuild> StartupSurface;
	int32 StartupFramesLeft = 0;

	/** A build that holds the meshes of the cells of its chunks while ApplySurface shows it or StartupSurface lives, from which
	 *  GetCellTriangles reads them instead of building them again */
	const HexPlanetSurface::FBuild* TriangleSource = nullptr;

	/** The pieces of chunks near the camera, built in the background from the state of the surface shown */
	struct FChunkStream;
	UE::Tasks::FTask StreamTask;
	TSharedPtr<FChunkStream> Stream;

	/** Starts building in the background the pieces of chunks that want them */
	void StartStream(TArray<int32> Chunks);

	/** Shows the pieces the stream built, of the chunks that no build showed anew meanwhile */
	void FinishStream();

	/** Sends the two sections of pieces of a chunk to the GPU, or frees them */
	void SetChunkPieces(int32 Chunk, FHexPlanetMeshSectionRef Land, FHexPlanetMeshSectionRef Mountains);
	void DropChunkPieces(int32 Chunk);

	/** Returns the distances around CoarseDistance where the pieces of a chunk dissolve into its coarse mesh, of DA_HexPlanet or the console */
	void GetLodBand(double& OutNear, double& OutFar) const;

	/** Returns true if a chunk wants its pieces on the GPU from where UpdateChunkLod last measured */
	bool WantsPieces(int32 Chunk) const;

	/** The heights of the corners of the cells the surface shown was built with (HexPlanetSurface::FBuild::CornerLow), from which
	 *  GetCellTriangles builds the triangles of a cell again */
	TArray<double> ShownCornerLow;

	/** The triangles of the cells that picking with EHexEdgeRule::HigherHolds tested lately (FindHeldCellAlongRay), so a cursor that stays over a
	 *  place does not build them again every frame; emptied whenever a build is shown */
	mutable TMap<int32, TArray<FVector3f>> PickTriangles;

	/** FindCellAlongRay with EHexEdgeRule::HigherHolds, where a cell lower than a neighbor carries its slope inside itself: the ray is tested
	 *  against the triangles of every cell it passes below the highest top around it, from Start to End, and the nearest hit wins */
	int32 FindHeldCellAlongRay(const FVector& Origin, const FVector& RayDirection, double Start, double End, FVector* OutPoint) const;

	/** The markers of the entrances: a cave mouth on each usable entrance of the surface, a ring of rocks with a shaft of light on each exit
	 *  underground, tinted by the color of its network (UHexPlanetConfig::NetworkColors of the surface) */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> MarkerComponents;

	/** Material of the fog of the underground (UHexPlanetConfig::bFog), in HazeVolume in place of the haze */
	UPROPERTY(EditAnywhere, Category="Planet|Underground")
	TSoftObjectPtr<UMaterialInterface> FogMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexUndergroundFog.M_HexUndergroundFog")));

	void CreateFog();

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

	/** Config of the underground, which the surface gives the underground it spawns, its features off, from Tools/create_planet_assets.py */
	UPROPERTY(EditAnywhere, Category="Planet|Underground")
	TSoftObjectPtr<UHexPlanetConfig> UndergroundConfig = TSoftObjectPtr<UHexPlanetConfig>(FSoftObjectPath(TEXT("/Game/HexPlanet/DA_HexUnderground.DA_HexUnderground")));

	/** Material of the cells of the underground; until Tools/create_planet_assets.py makes it, the underground draws Material */
	UPROPERTY(EditAnywhere, Category="Planet|Underground")
	TSoftObjectPtr<UMaterialInterface> UndergroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexUnderground.M_HexUnderground")));

	/** Material of the walls on the lips of the passages (UHexPlanetConfig::bWalls), from Tools/create_planet_assets.py */
	UPROPERTY(EditAnywhere, Category="Planet|Underground")
	TSoftObjectPtr<UMaterialInterface> WallMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexUndergroundWall.M_HexUndergroundWall")));

	/** Constructor */
	AHexPlanet();

	/** Returns the layer of this planet, the surface or the underground */
	EHexPlanetLayer GetLayer() const { return PlanetLayer; }

	/** Returns the planet of a layer of this planet: this one for its own layer, else through UHexPlanetLayers, or null */
	AHexPlanet* GetLayerPlanet(EHexPlanetLayer InLayer) const;

	/** Returns true while this layer is the one shown */
	bool IsPresented() const { return bPresented; }

	/**
	 *  Shows or hides this layer (UHexPlanetLayers::Present calls it): a hidden layer is hidden in the game, so its primitives and its sky light
	 *  leave the scene, frees the pieces of its chunks, keeping their coarse meshes, and skips the work of a frame (the water cap, the switch of
	 *  the clouds, the LOD and streaming of its chunks, the sun of its materials and MPC_HexPlanet, the debug view), but streams the pieces
	 *  SetPiecePrefetch asks for. Shown the first time, the underground builds its meshes. Showing a layer again recreates its scene proxies
	 */
	void SetPresented(bool bInPresented);

	/** Returns true once the levels and biomes of the cells are there: on the surface once it is built, underground once its data is read or
	 *  generated in the background, before its meshes are built */
	bool IsDataLoaded() const { return bBuilt || bDataLoaded; }

	/** Returns the folder in Content/Data/Planets of the open planet, empty if there was none and it was generated from the defaults; the
	 *  same for both layers, as it is the name of the planet */
	const FString& GetPlanetFolder() const { return PlanetFolder; }

	/** Returns the folder of the files of this layer (HexPlanetFile::GetDataFolder), empty for a planet that is not saved */
	FString GetDataFolder() const { return HexPlanetFile::GetDataFolder(PlanetFolder, PlanetLayer); }

	/** Returns planet.json of the open planet */
	const FHexPlanetManifest& GetManifest() const { return Manifest; }

	/** Returns true if the planet has a folder; a new planet of the menu has none until it is saved */
	bool IsSaved() const { return !PlanetFolder.IsEmpty(); }

	/** Writes the planet into the folder of a name, over a planet that is there, and makes it the folder of both layers, with the levels and
	 *  biomes of both, painted or generated, and entrances.json. Returns false with an error if it could not */
	bool SaveAs(const FString& Name, FString& OutError);

	/** Returns true if leaving the planet now would lose something: it is new and not saved, or the cells of either layer or the entrances
	 *  were edited since the last save */
	bool HasUnsavedChanges() const { return !IsSaved() || HasUnsavedLevels(); }

	/** Writes the files of the levels and biomes of both layers of a saved planet, and entrances.json. Returns false with an error if it could not */
	bool SaveLevels(FString& OutError);

	/** Returns the entrances of the underground, all of entrances.json, those that cannot be used too; empty until the underground has its data */
	const TArray<FHexEntrance>& GetEntrances() const;

	/** Returns the index in GetEntrances of the entrance on a cell, or INDEX_NONE */
	int32 FindEntrance(int32 Cell) const;

	/** Returns what keeps an entrance of GetEntrances from being used, against the levels of both layers */
	HexPlanetUnderground::EEntranceProblem GetEntranceProblem(int32 Index) const;

	/** Returns how the passages and entrances of the underground fit together, or null until it has its data */
	const HexPlanetUnderground::FReport* GetUndergroundReport() const;

	/** Sets the entrances of the underground, on whichever layer it is called, as the editor changes them: marks them as edited, checks the
	 *  report again and places the markers of both layers again. Nothing is carved, so the exit of a new entrance may be in the void. bByStep
	 *  tells that a step of Undo of the brush made the change, which Undo can take back to the saved state (MarkLevelsEdited) */
	void SetEntrances(const TArray<FHexEntrance>& NewEntrances, bool bByStep = false);

	/** Sets SeedOffset of the underground block of planet.json on both layers and marks the manifest as edited, so the next save writes
	 *  planet.json; the maze itself is generated again by the caller (UHexPlanetPainter::NewLayout) */
	void SetUndergroundSeedOffset(int32 SeedOffset);

	/** On the underground, reads or generates its data now if it is not there yet, waiting for the background, as the editor needs it */
	void EnsureDataLoaded();

	/** Shows or hides the links of the underground for the Links tab of the editor: each passage tinted by the color of its network and what is
	 *  wrong in ProblemColor, through the blue of CellMarks, which M_HexUnderground reads; kept up to date while shown (UpdateLinkMarks) */
	void SetLinksShown(bool bShown);
	bool AreLinksShown() const { return bLinksShown; }

	/** Records that the levels or biomes were painted, so they differ from the files or the generation. bByStep tells that a step of Undo of
	 *  the brush made the change, so undoing back to the step of the last save brings the saved state back (ClearEditedBySteps); any other
	 *  change, such as hex.PlanetSetLevel, keeps the planet unsaved until it is saved */
	void MarkLevelsEdited(bool bByStep = false) { bLevelsEdited = true; bEditedOutsideSteps |= !bByStep; }

	/** Returns true if something changed the cells, the entrances or planet.json since the last save that no step of Undo holds */
	bool HasEditedOutsideSteps() const { return bEditedOutsideSteps; }

	/** Clears the marks of the edits made by steps of Undo, as the brush undid or redid back to the state of the last save */
	void ClearEditedBySteps() { bLevelsEdited = false; bEntrancesEdited = false; bManifestEdited = false; }

	/** Broadcast after a save wrote the files of this layer, its marks of edits cleared */
	FSimpleMulticastDelegate OnSaved;

	/** Sets the relief of cells by hand, an empty override going back to the hash, and rebuilds them and their neighbors; bByStep as for
	 *  MarkLevelsEdited */
	void SetReliefOverrides(const HexPlanetSurface::FReliefOverrides& Changes, bool bByStep = false);

	/** Returns the relief set by hand of a cell, or an empty one */
	HexPlanetSurface::FReliefOverride GetReliefOverride(int32 Cell) const;

	/** Returns the choice of version and of turn of the piece of a cell as shown, and of how many (Y), 0 for a cell without a piece */
	FIntPoint GetCellVersion(int32 Cell) const { return CellVersions.IsValidIndex(Cell) ? CellVersions[Cell] : FIntPoint::ZeroValue; }
	FIntPoint GetCellTurnChoice(int32 Cell) const { return CellTurnChoices.IsValidIndex(Cell) ? CellTurnChoices[Cell] : FIntPoint::ZeroValue; }

	/** Returns whether the piece of a cell is mirrored as shown */
	bool IsCellMirrored(int32 Cell) const { return CellMirrored.IsValidIndex(Cell) && CellMirrored[Cell]; }

	/** Returns true if the levels or biomes of either layer or the entrances were edited since they were last saved */
	bool HasUnsavedLevels() const;

	/** Returns true if the levels, biomes, relief, objects or rivers of either layer were edited since they were last saved, the entrances
	 *  left aside, so the warning can say which */
	bool HasUnsavedCells() const;

	/** Returns the grid, empty until the planet is built */
	const FHexSphereGrid& GetGrid() const { return Grid; }

	/** Returns the radius of the sea */
	double GetRadius() const { return Radius; }

	/** Returns the up of this layer at a point, away from the center of the planet; new placement takes its up from here, so a view from inside the
	 *  underground that turns the layer turns what stands on it too */
	FVector3d GetUpAt(const FVector3d& Point) const { return Point.GetSafeNormal(); }

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

	/** Finds the biomes of a layer, those with a PlanetLetter on the surface and with an UndergroundLetter underground, sorted by that letter,
	 *  which a cell of that layer holds the index of, so the index of a biome is the same in every run */
	static void FindBiomes(EHexPlanetLayer Layer, TArray<TObjectPtr<UHexBiomeConfig>>& OutConfigs, TArray<TCHAR>& OutLetters);

	/** Returns the biome of a cell, an index into GetBiomes */
	int32 GetCellBiome(int32 Cell) const { return CellBiomes.IsValidIndex(Cell) ? CellBiomes[Cell] : 0; }

	/** Returns the biome of every cell, in the order of the grid */
	const TArray<uint8>& GetCellBiomes() const { return CellBiomes; }

	/** Returns the letter of the biome of a cell in the files of its layer, its PlanetLetter on the surface and its UndergroundLetter underground, or 0 */
	TCHAR GetCellBiomeLetter(int32 Cell) const { return CellBiomes.IsValidIndex(Cell) && BiomeLetters.IsValidIndex(CellBiomes[Cell]) ? BiomeLetters[CellBiomes[Cell]] : TEXT('\0'); }

	/** Returns the letters of GetBiomes in the files of this layer, by index */
	const TArray<TCHAR>& GetBiomeLetters() const { return BiomeLetters; }

	/** Returns the rock layer of the biome of a cell, as the surface draws it on slopes and mountains */
	int32 GetCellRockLayer(int32 Cell) const;

	/** Gives the triangles of the surface of a cell as shown now, three corners each, empty before it is built: from the build being shown
	 *  when it holds the cell, else built again from what the surface shown was built from, as its meshes are on the GPU alone. Reads only,
	 *  so any thread may call it while the surface does not change */
	void GetCellTriangles(int32 Cell, TArray<FVector3f>& OutCorners) const;

	/** Returns the radius of the ground of a cell in a direction from the center of the planet, from its triangles, or its top when no
	 *  triangle of it lies in that direction */
	double FindGroundRadius(int32 Cell, const FVector3d& Direction) const;

	/** Places the markers of this layer again (MarkerComponents), from the entrances and the levels of both layers; a change of the entrances
	 *  calls it on both layers */
	void UpdateMarkers();

	/** Returns the forests, bushes, stones and decor of the cells */
	UHexPlanetObjects* GetObjects() const { return Objects; }

	/** Returns the walls on the lips of the passages, empty without UHexPlanetConfig::bWalls, and the values they are built with */
	const HexPlanetWalls::FWalls& GetWalls() const { return Walls; }
	const HexPlanetWalls::FParams& GetWallParams() const { return WallParams; }
	bool HasWalls() const { return !Walls.Lines.IsEmpty(); }

	/** Gives the walls the point under the cursor, around which they dissolve where they stand in front of it (WallFadeCenter of
	 *  M_HexUndergroundWall), or none while the cursor is off the planet */
	void SetFadeFocus(bool bOnPlanet, const FVector& Point);

	/** Returns the rivers and bridges */
	UHexPlanetRivers* GetRivers() const { return Rivers; }

	/** Rebuilds the valleys of the rivers, which UHexPlanetRivers changed, and marks the planet as edited; bByStep as for MarkLevelsEdited */
	void OnRiversChanged(bool bByStep = false);

	/** Sets how straight the rivers run for trying it (UHexPlanetRivers::SetShapeOverride), or back to DA_HexPlanet with none, and builds the
	 *  whole surface again in the background (hex.PlanetRiverShape) */
	void SetRiverShape(TOptional<FVector3d> Shape);

	/** Returns true if a river freezes beside a cell, by the letter of its biome (RiverFrozenBiomes of DA_HexPlanet) */
	bool IsRiverFrozenAt(int32 Cell) const;

	/** Gives the materials of the surface the textures of the rivers again, as UHexPlanetRivers made them anew */
	void ApplyRiverTextures();

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
	int32 FindCellAlongRay(const FVector& Origin, const FVector& Direction, FVector* OutPoint = nullptr) const;

	/** Outlines a cell, or nothing for INDEX_NONE, and its neighbors while the wireframe draws them too. With a brush of 1 to 3 rings of
	 *  cells around it, outlines the edge of the brush instead */
	void SetHoveredCell(int32 Cell, int32 BrushSize = 0);

	/** Returns the text of the debug panel for a cell */
	FString GetCellDebugText(int32 Cell) const;

	/** Returns how many cells CellMarks outlines now, in red or green (SetHoveredCell), for hex.PlanetLayerCheck */
	int32 CountOutlinedCells() const { return MarkedCells.Num(); }

	/** Returns the material of the tiles of painting (UHexPlanetPainter), made the first time: the material of the cells with PreviewTile 1, so it
	 *  draws their vertex colors, the values of the config and CellMarks, so a tile draws the outlines of its own cell, such as the edge of the
	 *  brush, in the colors SetHoveredCell gives; null without the material */
	UMaterialInstanceDynamic* GetPreviewMaterial();

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

	/**
	 *  Returns true if every chunk that draws its pieces from a camera at a place has them on the GPU, so the camera may jump there without
	 *  landing on coarse meshes with gaps between them and its pieces; the pieces stream only near the camera
	 */
	bool ArePiecesReadyAt(const FVector& CameraLocation) const;

	/** Builds first, all at once in the background, the pieces of the chunks around a place the camera is about to jump to, and keeps them
	 *  until it has; unset once it jumped (AHexPlanetCameraPawn waits for ArePiecesReadyAt) */
	void SetPiecePrefetch(TOptional<FVector> CameraLocation) { PrefetchCamera = CameraLocation; }

	/**
	 *  Logs what the surface holds in memory, after the times of frames the caller measured: its triangles by level, the copies of the meshes
	 *  of its chunks on the CPU and the GPU, the memory of the process, and the ray tracing geometry of the chunks and the memory of the RHI
	 *  (D3D12.DumpRayTracingGeometries, rhi.DumpMemory), the last two only with bProcess. hex.PlanetMemory calls it for each layer that is built
	 */
	void LogMemory(const FString& FrameTimes, bool bProcess = true);

protected:

	/** Reads the configs and the manifest and shows the loading screen, so the build that follows does not freeze a blank window */
	virtual void BeginPlay() override;

	/** Waits for a rebuild that still runs, as it reads the grid */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	/** Builds the grid, the surface, the objects, the sea and the sky, which takes a second or two on a large planet; underground from the data
	 *  read in the background, with the kit of the surface, and without the features its config turns off */
	void BuildPlanet();

	/** Layer of this planet, which the surface sets on the underground it spawns before it begins to play */
	UPROPERTY(VisibleAnywhere, Category="Planet")
	EHexPlanetLayer PlanetLayer = EHexPlanetLayer::Surface;

	/** True while this layer is shown */
	bool bPresented = true;

	/** Spawns the underground of this surface, hidden, with UndergroundConfig and UndergroundMaterial, which shares its folder and manifest */
	void SpawnUnderground();

	/** What the underground reads or generates in the background: its grid and HexPlanetUnderground::Load */
	struct FUndergroundData;
	UE::Tasks::FTask DataTask;
	TSharedPtr<FUndergroundData> UndergroundData;

	/** True once the underground has its levels, biomes and entrances */
	bool bDataLoaded = false;

	/** Starts reading or generating the data of the underground in the background, from the levels of the surface */
	void StartUndergroundData();

	/** Takes the data the background made, waiting for it if it runs */
	void FinishUndergroundData();

	/** The entrances of the underground, on the underground planet, and how they and the passages fit together, checked again when it is
	 *  read after a rebuild of either layer changed the levels */
	TArray<FHexEntrance> Entrances;
	mutable HexPlanetUnderground::FReport UndergroundReport;
	mutable bool bUndergroundReportDirty = false;

	/** Marks the report of the underground to be checked again, after the levels of a layer changed */
	void MarkUndergroundReportDirty();

	/** True if the entrances changed since they were last saved */
	bool bEntrancesEdited = false;

	/** True if planet.json changed since it was last saved, by a new layout of the underground (SetUndergroundSeedOffset) */
	bool bManifestEdited = false;

	/** True if the cells, the entrances or planet.json were changed since the last save by something no step of Undo holds (MarkLevelsEdited) */
	bool bEditedOutsideSteps = false;

	/** True while the Links tab shows the networks of the passages (SetLinksShown) */
	bool bLinksShown = false;

	/** Writes the blue of CellMarks of the underground from its report while the links are shown, else clears it: 1 + the network of a passage,
	 *  LinkProblem where something is wrong and LinkOrphan for a passage no exit reaches */
	void UpdateLinkMarks();

	/** Values of the blue of CellMarks for the links besides the networks */
	static constexpr uint8 LinkProblem = 9;
	static constexpr uint8 LinkOrphan = 10;

	/** Returns the planets of both layers that have their data, this one first */
	TArray<AHexPlanet*, TInlineAllocator<2>> GetLoadedLayers() const;

	/** Writes the files of this layer into its data folder of the folder of a planet, and the underground also entrances.json; the objects on
	 *  KeptEntranceCells are left out */
	bool SaveLayerFiles(const FString& Folder, const TSet<int32>& KeptEntranceCells, FString& OutError) const;

	/** Returns the cells of the entrances a save keeps (HexPlanetUnderground::GetSavedEntrances), none while the underground has no data */
	TSet<int32> GetKeptEntranceCells() const;

	/** After a save, tells on the screen and in the log how many entrances were left out of entrances.json as they cannot be used, so they are
	 *  gone when the planet opens again although they still show now */
	void ReportLeftOutEntrances() const;

	/** Keeps the objects of both layers off the cells of the entrances (UHexPlanetObjects::SetClearCells) */
	void UpdateClearCells();

	/** Gives the materials of the surface empty cubemaps of the sea and the shore, on a layer without the sea */
	void CreateEmptySeaField();

	/** Gives the sky light its plain cubemap and the color and intensity of DA_HexPlanet */
	void CreateSkyLight();

	/** Puts the haze of the air into HazeVolume, with the values of DA_HexPlanetClouds */
	void CreateHaze();

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

	/** The walls on the lips of the passages (UHexPlanetConfig::bWalls, HexPlanetWalls), built from the levels shown, their mesh per chunk
	 *  (WallChunks, under the empty root Surface), the hash of the samples each was built from, and their material */
	HexPlanetWalls::FWalls Walls;
	HexPlanetWalls::FParams WallParams;
	TArray<uint32> WallHashes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHexPlanetMeshComponent>> WallChunks;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WallMaterial;

	/** Triangles of the walls, drawn by hex.Wireframe when it draws every cell and its target is not the terrain alone (walls are not terrain):
	 *  a copy of each chunk of walls, made the first time, under the empty Wireframe; and whether the view hides the walls themselves, as
	 *  hex.Wireframe 2 draws their triangles alone */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> WallWireChunks;

	bool bWallsHiddenByWire = false;

	/** Builds the walls again from the levels shown, gives the chunks whose walls changed their meshes, and adds those chunks to Chunks, whose
	 *  objects stand on the walls too */
	void UpdateWalls(TArray<int32>& Chunks);

	/** Gives the copies of the walls in the wireframe the triangles of every chunk of walls, or clears them when Shown is false */
	void BuildWallWires(bool bShown);

	/** Frozen cells of the sea the sea field shown now was built from, see GetFrozenCells */
	TArray<uint8> ShownFrozen;

	/** Reads the biomes that have a PlanetLetter */
	void LoadBiomeConfigs();

	/** Takes the biomes from biomes.txt of the planet, or generates them when it has none or it is not valid, and fits them to the levels */
	void LoadBiomes();

	/** Writes the generated levels, biomes and objects of a saved planet that had no such files, so it keeps them when the generation changes */
	void WriteGeneratedFiles(bool bLevels, bool bBiomes, bool bObjects, bool bRivers);

	/** Returns the biome a cell takes on its level when its own is not allowed there: the one most of its neighbors have that it allows,
	 *  else the default of the level, else the first allowed */
	int32 FindFittingBiome(int32 Cell) const;

	/** Returns 1 for each cell of the sea whose biome is frozen and each cell of land next to one, which the sea field blends into ice */
	TArray<uint8> GetFrozenCells() const;

	/** Creates CellBiomeTexture from the biomes of all cells */
	void CreateBiomeTexture();

	/** Writes the pixels of cells into BiomePixels and uploads the texture */
	void UpdateBiomePixels(TConstArrayView<int32> ChangedCells);

	/** Writes the two pixels of a cell into BiomePixels, keeping the alpha of the second, which UpdateCanopy writes */
	void WriteBiomePixels(int32 Cell);

	/** Sends BiomePixels to CellBiomeTexture */
	void UploadBiomePixels();

	/** One pixel per cell in the layout of CellMarks: how the cover of a cell tints its ground from far away, its color as the root of the
	 *  linear color and its share in alpha (UHexPlanetObjects::GetCellCanopy), which M_HexPlanet draws (CANOPY_CODE); the alpha of the second
	 *  pixel of a cell in CellBiomeTexture holds which of its neighbors have it, so a forest goes on across their side */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CellCanopyTexture;
	TArray<FColor> CanopyPixels;

public:

	/** Writes CellCanopyTexture and the bits of its neighbors from the objects of every cell, making the texture the first time */
	void UpdateCanopy();

private:

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

	/** True when the next rebuild builds every chunk, as the shape of the rivers changed (SetRiverShape) */
	bool bRebuildAllPending = false;

	/** The mouths of rivers given to the water (UpdateRiverMouths), by their index in the line of the rivers, and that line */
	TArray<int32> AppliedRiverMouths;
	const void* AppliedRiverCurve = nullptr;

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

	/** Whether the copies of the walls in the wireframe have their triangles (BuildWallWires) */
	bool bAppliedWallWires = false;

	/** Cell and number of cells around it that the surface hides */
	int32 AppliedCutCell = INDEX_NONE;
	int32 AppliedCutCount = 0;

	/** Returns true if a section of the surface holds mountains, which the wireframe does not count as terrain */
	static bool IsMountainSection(int32 Section) { return Section % 2 == 1; }

	/** Returns true if the wireframe draws a section for a target */
	static bool IsWireTarget(int32 Section, EHexWireframeTarget Target);

	/** Sets the copy of a part of a chunk that the wireframe of the whole surface draws, 0 and 1 its pieces and 2 its coarse mesh, from the
	 *  section of a build, or clears it with none or while that wireframe is not drawn */
	void SetWireSection(int32 Chunk, int32 Part, const HexPlanetSurface::FSection* Section);

	/** Builds every copy the wireframe of the whole surface draws, from a build of the surface shown, as the surface keeps its meshes on the GPU alone */
	void BuildWireSections();

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

	/**
	 *  Shows a build: the coarse meshes of its chunks, the pieces of those that want them (WantsPieces), or none at the start, when
	 *  StartupSurface keeps them for UpdateChunkLod, and keeps where its cells lie in its sections
	 */
	void ApplySurface(HexPlanetSurface::FBuild& Build, bool bStartup = false);

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

	/** Gives the water the mouths of open rivers nearest the point under the camera, up to MaxRiverMouths (RiverMouth0.., RiverMouthDir0..),
	 *  from which M_HexPlanetWater draws the water of the river streaming out into the sea instead of the foam of the shore */
	void UpdateRiverMouths(const FVector3d& Below);

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
