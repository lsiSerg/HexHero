// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexWorldMap.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "Containers/Queue.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HexBiomeConfig.h"
#include "HexHero.h"
#include "HexLastView.h"
#include "HexLevelConfig.h"
#include "HexMapConfig.h"
#include "HexMapData.h"
#include "HexMapSurfaceConfig.h"
#include "HexMapWaterConfig.h"
#include "HexMaterialConfig.h"
#include "HexMapLoader.h"
#include "HexMapSettings.h"
#include "HexMeshBuilder.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Tasks/Task.h"

namespace
{
	const FName GridOpacityParameter(TEXT("GridOpacity"));
	const FName HexRadiusParameter(TEXT("HexRadius"));
	const FName HexDataParameter(TEXT("HexData"));
	const FName FootHeightParameter(TEXT("FootHeight"));
	const FName RimFadeHeightParameter(TEXT("RimFadeHeight"));
	const FName WaterHeightParameter(TEXT("WaterHeight"));
	const FName MeshRimsParameter(TEXT("MeshRims"));
	const FName RimOriginXParameter(TEXT("RimOriginX"));
	const FName RimOriginYParameter(TEXT("RimOriginY"));
	const FName RimStepParameter(TEXT("RimStep"));
	const FName MassifDataParameter(TEXT("MassifData"));
	const FName MassifStepParameter(TEXT("MassifStep"));
	const FName MassifMaxStepsParameter(TEXT("MassifMaxSteps"));
	const FName MassifRiseParameter(TEXT("MassifRise"));
	const FName ShoreFieldParameter(TEXT("ShoreField"));
	const FName ShoreHeightParameter(TEXT("ShoreHeight"));
	const FName ShoreMinXParameter(TEXT("ShoreMinX"));
	const FName ShoreMinYParameter(TEXT("ShoreMinY"));
	const FName ShoreSizeXParameter(TEXT("ShoreSizeX"));
	const FName ShoreSizeYParameter(TEXT("ShoreSizeY"));
	const FName WireHexQParameter(TEXT("WireHexQ"));
	const FName WireHexRParameter(TEXT("WireHexR"));
	const FName WireScopeParameter(TEXT("WireScope"));
	const FName WireOutsideOpacityParameter(TEXT("WireOutsideOpacity"));
	const FName WaterHideHexQParameter(TEXT("WaterHideHexQ"));
	const FName WaterHideHexRParameter(TEXT("WaterHideHexR"));
	const FName WaterHideScopeParameter(TEXT("WaterHideScope"));

	TAutoConsoleVariable<int32> CVarHexWireframe(TEXT("hex.Wireframe"), 0,
		TEXT("Draws the triangles of the terrain and the meshes on it. 0: off, 1: over the normal render, 2: only the triangles."));
	TAutoConsoleVariable<int32> CVarHexWireframeScope(TEXT("hex.WireframeScope"), 1,
		TEXT("Where hex.Wireframe draws the triangles. 0: everywhere, 1: the hex under the mouse cursor, 2: that hex and its neighbors."));
	TAutoConsoleVariable<int32> CVarHexWireframeTarget(TEXT("hex.WireframeTarget"), 0,
		TEXT("What hex.Wireframe draws the triangles of. 0: everything, 1: only the terrain, 2: everything but the terrain, such as mountains and cliffs."));
	TAutoConsoleVariable<int32> CVarHexWater(TEXT("hex.Water"), 0,
		TEXT("Shows the water for debugging. 0: shown, 1: hidden, 2: hidden in the hex under the mouse cursor, 3: hidden in that hex and its neighbors."));

	const TCHAR* const WireframeModeNames[] = { TEXT("off"), TEXT("over render"), TEXT("only") };
	const TCHAR* const WireframeScopeNames[] = { TEXT("all hexes"), TEXT("under cursor"), TEXT("cursor + neighbors") };
	const TCHAR* const WireframeTargetNames[] = { TEXT("everything"), TEXT("terrain"), TEXT("all but terrain") };
	const TCHAR* const WaterViewNames[] = { TEXT("shown"), TEXT("hidden"), TEXT("hidden under cursor"), TEXT("hidden at cursor + neighbors") };

	/** Sections of a terrain chunk that copy the terrain and the cliffs to draw their triangles over them */
	const int32 TerrainWireSection = 2;
	const int32 CliffWireSection = 3;

	/** Hex coordinate given to the materials when no hex is under the cursor */
	const float NoWireHex = 1.0e6f;
}

/** A finished background build */
struct FHexBuiltChunk
{
	FIntPoint Chunk = FIntPoint::ZeroValue;
	FHexMeshData Mesh;
	FHexMeshData CliffMesh;
};

/** Everything a background build reads, which does not change after the map is loaded, and where it leaves its result */
struct FHexTerrainSource
{
	FHexMapData Map;
	FHexMeshParams Params;
	TMap<int8, FHexLevelSurface> LevelSurfaces;
	FHexCliffData Cliffs;
	FHexRiverData Rivers;
	int32 ChunkSize = 16;
	TQueue<FHexBuiltChunk, EQueueMode::Mpsc> BuiltChunks;
};

AHexWorldMap::AHexWorldMap()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AHexWorldMap::SetGridVisible(bool bVisible)
{
	bGridVisible = bVisible;

	if (TerrainMaterial)
	{
		TerrainMaterial->SetScalarParameterValue(GridOpacityParameter, bGridVisible ? 1.0f : 0.0f);
	}

	if (WaterMaterial)
	{
		WaterMaterial->SetScalarParameterValue(GridOpacityParameter, bGridVisible ? 1.0f : 0.0f);
	}

	if (CliffMaterial)
	{
		CliffMaterial->SetScalarParameterValue(GridOpacityParameter, bGridVisible ? 1.0f : 0.0f);
	}

	for (const TPair<int32, TObjectPtr<UMaterialInstanceDynamic>>& FeatureMaterial : FeatureMaterials)
	{
		FeatureMaterial.Value->SetScalarParameterValue(GridOpacityParameter, bGridVisible ? 1.0f : 0.0f);
	}
}

void AHexWorldMap::BeginPlay()
{
	Super::BeginPlay();

	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

	// the player controller opens the planet instead, so no map is loaded for nothing
	if (HexLastView::ShouldOpenPlanet(GetWorld()))
	{
		return;
	}

	MapConfig = GetDefault<UHexMapSettings>()->LoadMapConfig();
	SurfaceConfig = MapConfig->SurfaceConfig.LoadSynchronous();
	WaterLookConfig = MapConfig->WaterLookConfig.LoadSynchronous();
	if (!SurfaceConfig || !WaterLookConfig)
	{
		UE_LOG(LogHexHero, Warning, TEXT("DA_HexMap misses its surface or water config, run Tools/build_hex_materials.py; their defaults are used"));
		SurfaceConfig = SurfaceConfig ? SurfaceConfig.Get() : GetDefault<UHexMapSurfaceConfig>();
		WaterLookConfig = WaterLookConfig ? WaterLookConfig.Get() : GetDefault<UHexMapWaterConfig>();
	}

	// the waves of the water and the drifts of snow are lit from the sun of the level
	SunDirection = WaterLookConfig->SunDirection.GetSafeNormal();
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		SunDirection = -It->GetActorForwardVector();
		break;
	}

	const TArray<const UHexLevelConfig*> LoadedLevels = GetDefault<UHexMapSettings>()->LoadLevels();
	const TArray<const UHexBiomeConfig*> LoadedBiomes = GetDefault<UHexMapSettings>()->LoadBiomes();
	Levels = LoadedLevels;
	Biomes = LoadedBiomes;

	MapFolder = GetWorld()->URL.GetOption(*FString::Printf(TEXT("%s="), HexMapLoader::MapOption), TEXT(""));

	if (MapFolder.IsEmpty())
	{
		const TArray<FHexMapEntry> Maps = HexMapLoader::FindMaps();

		if (Maps.IsEmpty())
		{
			UE_LOG(LogHexHero, Error, TEXT("There are no hex maps"));
			return;
		}

		// the map of the last run, unless it is gone
		MapFolder = HexLastView::FindLastMap(Maps);

		if (MapFolder.IsEmpty())
		{
			MapFolder = Maps[0].Folder;
		}
	}

	FHexMapData Map;
	FString Error;

	if (!HexMapLoader::LoadMap(MapFolder, LoadedLevels, LoadedBiomes, Map, Error))
	{
		UE_LOG(LogHexHero, Error, TEXT("Could not load the hex map %s: %s"), *MapFolder, *Error);
		return;
	}

	UE_LOG(LogHexHero, Log, TEXT("Loaded %dx%d hex map %s"), Map.Width, Map.Height, *MapFolder);

	TerrainSource = MakeShared<FHexTerrainSource>();
	TerrainSource->Map = MoveTemp(Map);

	PrepareTerrain();
	BuildWater();
	BuildRiverWater();
}

void AHexWorldMap::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FIntRect WantedChunks;
	FBox2D ViewArea;

	if (!TerrainSource)
	{
		return;
	}

	UpdateDebugView();

	if (!GetViewArea(ViewArea))
	{
		return;
	}

	UpdateWaterGrid(ViewArea);

	if (!GetChunksInView(WantedChunks))
	{
		return;
	}

	// chunks stay a little longer than they are wanted, so moving back and forth does not rebuild them
	FIntRect ChunksToKeep = WantedChunks;
	ChunksToKeep.InflateRect(1);

	AddBuiltChunks(ChunksToKeep);
	RemoveChunksOutside(ChunksToKeep);
	StartChunkBuilds(WantedChunks);
}

void AHexWorldMap::PrepareTerrain()
{
	TerrainSource->ChunkSize = MapConfig->ChunkSize;
	TerrainSource->Params.HexRadius = MapConfig->HexRadius;
	TerrainSource->Params.LevelHeight = MapConfig->LevelHeight;
	TerrainSource->Params.Subdivisions = MapConfig->HexSubdivisions;
	TerrainSource->Params.BlendPower = MapConfig->LevelBlendPower;
	TerrainSource->Params.WarpDistance = MapConfig->LevelWarpDistance;
	TerrainSource->Params.BottomHeight = MapConfig->MapBottomHeight;
	TerrainSource->Params.FootBlendDistance = MapConfig->JoinedMeshFootBlend * MapConfig->HexRadius;
	TerrainSource->Params.WaterHeight = MapConfig->WaterHeight;
	TerrainSource->Params.CliffSeabedDepth = MapConfig->CliffSeabedDepth;

	TerrainSource->Params.CliffBottomHeight = MapConfig->CliffBottomHeight;
	TerrainSource->Params.CliffMinDifference = MapConfig->CliffMinLevelDifference;
	TerrainSource->Params.RiverWidth = MapConfig->RiverWidth;
	TerrainSource->Params.RiverDepth = MapConfig->RiverDepth;
	TerrainSource->Params.RiverBankHeight = MapConfig->RiverBankHeight;
	TerrainSource->Params.RiverBankLean = MapConfig->RiverBankLean;
	TerrainSource->Params.RiverMeander = MapConfig->RiverMeander;
	TerrainSource->Params.Tuning = MapConfig->Tuning;

	for (const UHexLevelConfig* Definition : Levels)
	{
		FHexLevelSurface Surface;
		Surface.Color = Definition->TerrainColor;
		Surface.BumpHeight = Definition->BumpHeight;
		Surface.RidgeHeight = Definition->RidgeHeight;
		Surface.bHidesTerrain = Definition->bHidesTerrain;
		Surface.CliffTop = !Definition->JoinedMeshes.IsEmpty()
			? Definition->Level * MapConfig->LevelHeight + Definition->JoinedMeshFootHeight * MapConfig->HexRadius / Definition->JoinedMeshRadius
			: FMath::Max(Definition->Level * MapConfig->LevelHeight, MapConfig->WaterHeight + MapConfig->CliffMinRise);
		Surface.bHasJoinedMesh = !Definition->JoinedMeshes.IsEmpty();
		Surface.FootHeight = Definition->Level * MapConfig->LevelHeight + Definition->JoinedMeshFootHeight * MapConfig->HexRadius / Definition->JoinedMeshRadius;
		TerrainSource->LevelSurfaces.Add(static_cast<int8>(Definition->Level), Surface);

		if (UStaticMesh* FeatureMesh = Definition->FeatureMesh.LoadSynchronous())
		{
			FeatureMeshes.Add(Definition->Level, FeatureMesh);
		}
		else if (!Definition->FeatureMesh.IsNull())
		{
			UE_LOG(LogHexHero, Warning, TEXT("Could not load the feature mesh %s of level %d"), *Definition->FeatureMesh.ToString(), Definition->Level);
		}

		for (const TPair<int32, FHexJoinedVariants>& Variants : Definition->JoinedMeshVariants)
		{
			for (const TSoftObjectPtr<UStaticMesh>& Variant : Variants.Value.Meshes)
			{
				if (UStaticMesh* VariantMesh = Variant.LoadSynchronous())
				{
					JoinedVariants.FindOrAdd(FIntPoint(Definition->Level, Variants.Key)).Meshes.Add(VariantMesh);
				}
				else
				{
					UE_LOG(LogHexHero, Warning, TEXT("Could not load the joined mesh %s of level %d"), *Variant.ToString(), Definition->Level);
				}
			}
		}

		if (UStaticMesh* NotchMesh = Definition->JoinedNotchMesh.LoadSynchronous())
		{
			JoinedNotchMeshes.Add(Definition->Level, NotchMesh);
		}

		for (const TPair<int32, TSoftObjectPtr<UStaticMesh>>& Joined : Definition->JoinedMeshes)
		{
			if (UStaticMesh* JoinedMesh = Joined.Value.LoadSynchronous())
			{
				JoinedMeshes.Add(FIntPoint(Definition->Level, Joined.Key), JoinedMesh);
			}
			else
			{
				UE_LOG(LogHexHero, Warning, TEXT("Could not load the joined mesh %s of level %d"), *Joined.Value.ToString(), Definition->Level);
			}
		}
	}

	if (UMaterialInterface* BaseMaterial = MapConfig->TerrainMaterial.LoadSynchronous())
	{
		TerrainMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		ApplyLook(TerrainMaterial);
		TerrainMaterial->SetScalarParameterValue(HexRadiusParameter, MapConfig->HexRadius);
		TerrainMaterial->SetScalarParameterValue(WaterHeightParameter, MapConfig->WaterHeight);
	}

	for (const UHexLevelConfig* Definition : Levels)
	{
		UMaterialInterface* BaseMaterial = Definition->FeatureMaterial.LoadSynchronous();

		// a feature with the terrain material shares its instance
		if (BaseMaterial && BaseMaterial != MapConfig->TerrainMaterial.Get())
		{
			UMaterialInstanceDynamic* FeatureMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			ApplyLook(FeatureMaterial);
			FeatureMaterial->SetScalarParameterValue(HexRadiusParameter, MapConfig->HexRadius);
			FeatureMaterial->SetScalarParameterValue(FootHeightParameter, Definition->Level * MapConfig->LevelHeight + Definition->JoinedMeshFootHeight * MapConfig->HexRadius / Definition->JoinedMeshRadius);
			FeatureMaterial->SetScalarParameterValue(RimFadeHeightParameter, Definition->JoinedMeshRimFadeHeight * MapConfig->HexRadius / Definition->JoinedMeshRadius);
			FeatureMaterial->SetScalarParameterValue(WaterHeightParameter, MapConfig->WaterHeight);
			FeatureMaterial->SetScalarParameterValue(MassifStepParameter, Definition->JoinedMeshMassifStep);
			FeatureMaterial->SetScalarParameterValue(MassifMaxStepsParameter, Definition->JoinedMeshMassifMaxSteps);
			FeatureMaterial->SetScalarParameterValue(MassifRiseParameter, Definition->JoinedMeshMassifRise * MapConfig->HexRadius / Definition->JoinedMeshRadius);
			FeatureMaterials.Add(Definition->Level, FeatureMaterial);
		}

		// a joined mesh is moved onto the terrain by its material, so its triangles must be moved the same way
		UMaterialInterface* JoinedWireBase = MapConfig->JoinedWireframeMaterial.LoadSynchronous();

		if (!Definition->JoinedMeshes.IsEmpty() && JoinedWireBase)
		{
			UMaterialInstanceDynamic* JoinedWire = UMaterialInstanceDynamic::Create(JoinedWireBase, this);
			ApplyLook(JoinedWire);
			JoinedWire->SetScalarParameterValue(FootHeightParameter, Definition->Level * MapConfig->LevelHeight + Definition->JoinedMeshFootHeight * MapConfig->HexRadius / Definition->JoinedMeshRadius);
			JoinedWire->SetScalarParameterValue(RimFadeHeightParameter, Definition->JoinedMeshRimFadeHeight * MapConfig->HexRadius / Definition->JoinedMeshRadius);
			JoinedWire->SetScalarParameterValue(MassifStepParameter, Definition->JoinedMeshMassifStep);
			JoinedWire->SetScalarParameterValue(MassifMaxStepsParameter, Definition->JoinedMeshMassifMaxSteps);
			JoinedWire->SetScalarParameterValue(MassifRiseParameter, Definition->JoinedMeshMassifRise * MapConfig->HexRadius / Definition->JoinedMeshRadius);
			JoinedWireframeMaterials.Add(Definition->Level, JoinedWire);
		}
	}

	if (UMaterialInterface* BaseMaterial = MapConfig->WireframeMaterial.LoadSynchronous())
	{
		WireframeMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		ApplyLook(WireframeMaterial);
	}

	for (UMaterialInstanceDynamic* Wire : GetWireframeMaterials())
	{
		Wire->SetScalarParameterValue(HexRadiusParameter, MapConfig->HexRadius);
	}

	HexMeshBuilder::BuildCliffs(TerrainSource->Map, TerrainSource->Params, TerrainSource->LevelSurfaces, TerrainSource->Cliffs);
	HexMeshBuilder::BuildRivers(TerrainSource->Map, TerrainSource->Params, TerrainSource->LevelSurfaces, &TerrainSource->Cliffs, TerrainSource->Rivers);

	if (UMaterialInterface* BaseMaterial = MapConfig->CliffMaterial.LoadSynchronous())
	{
		CliffMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		ApplyLook(CliffMaterial);
		CliffMaterial->SetScalarParameterValue(HexRadiusParameter, MapConfig->HexRadius);
	}

	SetGridVisible(bGridVisible);
	CreateHexDataTexture();
	CreateMeshRims();

	// the map reaches a full radius past the centers of the hexes at its edge
	const FHexLayout Layout(MapConfig->HexRadius);
	const double HalfHexWidth = FMath::Sqrt(3.0) * 0.5 * MapConfig->HexRadius;
	const double MaxY = Layout.HexToWorld(FHexCoord::FromOffset(TerrainSource->Map.Width - 1, TerrainSource->Map.Height > 1 ? 1 : 0)).Y + HalfHexWidth;
	const double MinX = Layout.HexToWorld(FHexCoord::FromOffset(0, TerrainSource->Map.Height - 1)).X - MapConfig->HexRadius;

	MapBounds = FBox2D(FVector2D(MinX, -HalfHexWidth), FVector2D(MapConfig->HexRadius, MaxY));
}

void AHexWorldMap::CreateHexDataTexture()
{
	const FHexMapData& Map = TerrainSource->Map;
	const TArray<const UHexBiomeConfig*> BiomePointers(Biomes);
	const TArray<FColor> Pixels = HexMapLoader::BuildHexData(Map, BiomePointers);

	HexDataTexture = UTexture2D::CreateTransient(Map.Width, Map.Height, PF_B8G8R8A8);
	HexDataTexture->SRGB = false;
	HexDataTexture->Filter = TF_Nearest;
	HexDataTexture->NeverStream = true;

	FTexture2DMipMap& Mip = HexDataTexture->GetPlatformData()->Mips[0];
	FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
	Mip.BulkData.Unlock();
	HexDataTexture->UpdateResource();

	if (TerrainMaterial)
	{
		TerrainMaterial->SetTextureParameterValue(HexDataParameter, HexDataTexture);
	}

	if (CliffMaterial)
	{
		CliffMaterial->SetTextureParameterValue(HexDataParameter, HexDataTexture);
	}

	for (const TPair<int32, TObjectPtr<UMaterialInstanceDynamic>>& FeatureMaterial : FeatureMaterials)
	{
		FeatureMaterial.Value->SetTextureParameterValue(HexDataParameter, HexDataTexture);
	}
}

bool AHexWorldMap::GetViewArea(FBox2D& OutArea) const
{
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();

	if (!PlayerController)
	{
		return false;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);

	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return false;
	}

	// the view reaches the ground where the rays through the corners of the screen hit it
	OutArea = FBox2D(ForceInit);

	for (const FVector2D& ScreenCorner : { FVector2D(0.0, 0.0), FVector2D(ViewportWidth, 0.0), FVector2D(0.0, ViewportHeight), FVector2D(ViewportWidth, ViewportHeight) })
	{
		FVector RayOrigin;
		FVector RayDirection;

		if (!PlayerController->DeprojectScreenPositionToWorld(ScreenCorner.X, ScreenCorner.Y, RayOrigin, RayDirection))
		{
			return false;
		}

		// a ray at or above the horizon never reaches the ground, so it counts as going past the end of the map
		const bool bHitsGround = RayDirection.Z < -UE_KINDA_SMALL_NUMBER;
		const FVector GroundPoint = RayOrigin + (bHitsGround ? RayDirection * (-RayOrigin.Z / RayDirection.Z) : FVector(RayDirection.X, RayDirection.Y, 0.0) * MapBounds.GetSize().Size());
		OutArea += FVector2D(GroundPoint.X, GroundPoint.Y);
	}

	OutArea = OutArea.ExpandBy(TerrainSource->Params.HexRadius * MapConfig->ViewMarginInHexes);
	return true;
}

bool AHexWorldMap::GetChunksInView(FIntRect& OutChunks) const
{
	FBox2D ViewArea;

	if (!GetViewArea(ViewArea))
	{
		return false;
	}

	const double HexRadius = TerrainSource->Params.HexRadius;

	// the first map row is at X = 0 and later rows go towards -X
	const double ChunkWidth = FMath::Sqrt(3.0) * HexRadius * TerrainSource->ChunkSize;
	const double ChunkHeight = 1.5 * HexRadius * TerrainSource->ChunkSize;
	const int32 NumColumns = FMath::DivideAndRoundUp(TerrainSource->Map.Width, TerrainSource->ChunkSize);
	const int32 NumRows = FMath::DivideAndRoundUp(TerrainSource->Map.Height, TerrainSource->ChunkSize);

	OutChunks.Min.X = FMath::Clamp(FMath::FloorToInt32(ViewArea.Min.Y / ChunkWidth), 0, NumColumns);
	OutChunks.Max.X = FMath::Clamp(FMath::FloorToInt32(ViewArea.Max.Y / ChunkWidth) + 1, 0, NumColumns);
	OutChunks.Min.Y = FMath::Clamp(FMath::FloorToInt32(-ViewArea.Max.X / ChunkHeight), 0, NumRows);
	OutChunks.Max.Y = FMath::Clamp(FMath::FloorToInt32(-ViewArea.Min.X / ChunkHeight) + 1, 0, NumRows);

	return true;
}

void AHexWorldMap::AddBuiltChunks(const FIntRect& ChunksToKeep)
{
	FHexBuiltChunk Built;

	for (int32 Added = 0; Added < MapConfig->MaxChunksAddedPerTick && TerrainSource->BuiltChunks.Dequeue(Built); ++Added)
	{
		BuildingChunks.Remove(Built.Chunk);

		if (!ChunksToKeep.Contains(Built.Chunk))
		{
			continue;
		}

		UProceduralMeshComponent* Component = nullptr;

		if (FreeChunkComponents.Num() > 0)
		{
			Component = FreeChunkComponents.Pop();
		}
		else
		{
			Component = CreateMeshComponent(*FString::Printf(TEXT("TerrainChunk_%d"), TerrainChunks.Num()));
		}

		const FHexMeshData& Mesh = Built.Mesh;
		Component->CreateMeshSection_LinearColor(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, {}, Mesh.Colors, {}, false);

		if (!Built.CliffMesh.Vertices.IsEmpty())
		{
			const FHexMeshData& Cliff = Built.CliffMesh;
			Component->CreateMeshSection_LinearColor(1, Cliff.Vertices, Cliff.Triangles, Cliff.Normals, {}, Cliff.Colors, {}, false);
		}

		// this also gives the sections their materials
		ApplyWireframe(Component);
		TerrainChunks.Add(Built.Chunk, Component);

		AddChunkFeatures(Built.Chunk);
	}
}

void AHexWorldMap::AddChunkFeatures(const FIntPoint& Chunk)
{
	const FHexMapData& Map = TerrainSource->Map;
	const int32 MinColumn = Chunk.X * TerrainSource->ChunkSize;
	const int32 MinRow = Chunk.Y * TerrainSource->ChunkSize;
	const int32 MaxColumn = FMath::Min(MinColumn + TerrainSource->ChunkSize, Map.Width);
	const int32 MaxRow = FMath::Min(MinRow + TerrainSource->ChunkSize, Map.Height);

	for (const UHexLevelConfig* Definition : Levels)
	{
		const TObjectPtr<UStaticMesh>* FeatureMesh = FeatureMeshes.Find(Definition->Level);

		// a chunk has one component per mesh it uses
		TMap<UStaticMesh*, UInstancedStaticMeshComponent*> Components;

		const auto GetComponent = [this, &Components, &Chunk, Definition](UStaticMesh* Mesh)
		{
			UInstancedStaticMeshComponent*& Component = Components.FindOrAdd(Mesh);

			if (!Component)
			{
				Component = NewObject<UInstancedStaticMeshComponent>(this);
				Component->SetupAttachment(RootComponent);
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Component->SetStaticMesh(Mesh);

				// this also gives the component its material
				ApplyWireframe(Component, Definition->Level);

				Component->RegisterComponent();
				FHexChunkFeatures& Features = ChunkFeatures.FindOrAdd(Chunk);
				Features.Components.Add(Component);
				Features.Levels.Add(Definition->Level);
			}

			return Component;
		};

		for (int32 Row = MinRow; Row < MaxRow; ++Row)
		{
			for (int32 Column = MinColumn; Column < MaxColumn; ++Column)
			{
				if (Map.GetLevel(Column, Row) != Definition->Level)
				{
					continue;
				}

				const FHexCoord Hex = FHexCoord::FromOffset(Column, Row);
				const int32 NeighborMask = GetSameLevelNeighbors(Hex);
				const FHexJoinedPiece Piece = HexMeshBuilder::GetJoinedPiece(NeighborMask);
				UStaticMesh* JoinedMesh = FindJoinedMesh(Definition->Level, Piece.MeshMask, Hex);
				UStaticMesh* Mesh = JoinedMesh ? JoinedMesh : FeatureMesh ? FeatureMesh->Get() : nullptr;

				if (!Mesh)
				{
					continue;
				}

				UInstancedStaticMeshComponent* Component = GetComponent(Mesh);

				if (JoinedMesh)
				{
					const double BaseZ = Definition->Level * MapConfig->LevelHeight;
					Component->AddInstance(HexMeshBuilder::GetJoinedTransform(Hex, Piece.Turns + HexMeshBuilder::GetJoinedExtraTurns(NeighborMask, Hex), MapConfig->HexRadius, Definition->JoinedMeshRadius, BaseZ), true);

					if (const TObjectPtr<UStaticMesh>* NotchMesh = JoinedNotchMeshes.Find(Definition->Level))
					{
						for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
						{
							if (HexMeshBuilder::HasJoinedNotch(NeighborMask, Direction))
							{
								GetComponent(*NotchMesh)->AddInstance(HexMeshBuilder::GetJoinedNotchTransform(Hex, Direction, MapConfig->HexRadius, Definition->JoinedMeshRadius, BaseZ), true);
							}
						}
					}
				}
				else
				{
					const double BottomZ = Definition->Level * MapConfig->LevelHeight - Definition->FeatureSink;
					Component->AddInstance(HexMeshBuilder::GetFeatureTransform(Hex, Mesh->GetBoundingBox(),
						MapConfig->HexRadius, Definition->FeatureWidth, Definition->FeatureHeight, BottomZ), true);
				}
			}
		}
	}
}

bool AHexWorldMap::FindHexAlongRay(const FVector& RayOrigin, const FVector& RayDirection, FHexCoord& OutHex) const
{
	if (!IsMapLoaded())
	{
		return false;
	}

	int32 HighestLevel = 0;

	for (const UHexLevelConfig* Definition : Levels)
	{
		HighestLevel = FMath::Max(HighestLevel, Definition->Level);
	}

	return HexMeshBuilder::FindHexAlongRay(TerrainSource->Map, MapConfig->HexRadius, MapConfig->LevelHeight, MapConfig->WaterHeight, HighestLevel * MapConfig->LevelHeight, RayOrigin, RayDirection, OutHex);
}

bool AHexWorldMap::GetHexCenter(const FHexCoord& Hex, FVector& OutCenter) const
{
	if (!IsMapLoaded() || !TerrainSource->Map.Contains(Hex))
	{
		return false;
	}

	const FVector2D Center = FHexLayout(MapConfig->HexRadius).HexToWorld(Hex);
	const float Height = FMath::Max(TerrainSource->Map.GetLevel(Hex) * MapConfig->LevelHeight, MapConfig->WaterHeight);
	OutCenter = FVector(Center.X, Center.Y, Height);
	return true;
}

FString AHexWorldMap::GetHexDebugText(const FHexCoord& Hex) const
{
	const int32 Level = TerrainSource->Map.GetLevel(Hex);
	const UHexBiomeConfig* Biome = Biomes[TerrainSource->Map.GetBiome(Hex)];
	FString Text = FString::Printf(TEXT("Hex %d, %d (axial %d, %d)"), Hex.GetColumn(), Hex.GetRow(), Hex.Q, Hex.R);

	const TObjectPtr<const UHexLevelConfig>* Definition = Levels.FindByPredicate([Level](const UHexLevelConfig* Candidate)
	{
		return Candidate->Level == Level;
	});

	if (Definition)
	{
		const UHexLevelConfig* LevelConfig = *Definition;
		const auto GetYesNo = [](bool bPasses) { return bPasses ? TEXT("yes") : TEXT("no"); };

		Text += FString::Printf(TEXT("\nLevel %d: %s\nBiome: %s\nMove cost %.2f\nPasses: ships %s, heroes %s, units %s"), Level, *LevelConfig->DisplayName, *Biome->DisplayName,
			LevelConfig->MoveCost * Biome->MoveCostMultiplier,
			GetYesNo(LevelConfig->bPassableByShips && !Biome->bBlocksShips),
			GetYesNo(LevelConfig->bPassableByHeroes && !Biome->bBlocksHeroes),
			GetYesNo(LevelConfig->bPassableByUnits && !Biome->bBlocksUnits));
	}

	const FHexRiver River = TerrainSource->Map.GetRiver(Hex);

	if (River.HasRiver())
	{
		static const TCHAR* DirectionNames[FHexCoord::NumDirections] = { TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("SW"), TEXT("W"), TEXT("NW") };
		FString Inflows;

		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			if ((River.InflowMask & (1 << Direction)) != 0)
			{
				Inflows += Inflows.IsEmpty() ? DirectionNames[Direction] : *FString::Printf(TEXT(", %s"), DirectionNames[Direction]);
			}
		}

		const TArray<FHexRiverPath>& Paths = TerrainSource->Map.RiverPaths;
		Text += FString::Printf(TEXT("\n%s"), Paths.IsValidIndex(River.Path) ? *Paths[River.Path].Name : TEXT("River"));
		Text += FString::Printf(TEXT(" %s %s, from %s, size %d"), River.bMouth ? TEXT("flows into water at") : TEXT("flows"), DirectionNames[River.Outflow],
			Inflows.IsEmpty() ? TEXT("its source") : *Inflows, River.Size);
	}

	const FHexJoinedPiece Piece = HexMeshBuilder::GetJoinedPiece(GetSameLevelNeighbors(Hex));

	if (const UStaticMesh* JoinedMesh = FindJoinedMesh(Level, Piece.MeshMask, Hex))
	{
		Text += FString::Printf(TEXT("\nMesh %s, turns %d"), *JoinedMesh->GetName(), Piece.Turns);
	}

	return Text;
}

UStaticMesh* AHexWorldMap::FindJoinedMesh(int32 Level, int32 MeshMask, const FHexCoord& Hex) const
{
	const TObjectPtr<UStaticMesh>* JoinedMesh = JoinedMeshes.Find(FIntPoint(Level, MeshMask));

	if (!JoinedMesh)
	{
		return nullptr;
	}

	const FHexLoadedVariants* Variants = JoinedVariants.Find(FIntPoint(Level, MeshMask));

	if (!Variants)
	{
		return *JoinedMesh;
	}

	// a neighbor in a row gets another version, as the coordinates go through a prime number of steps
	const int32 Choice = FMath::Abs(Hex.Q * 2 + Hex.R * 3 + (Hex.Q ^ Hex.R)) % (Variants->Meshes.Num() + 1);
	return Choice == 0 ? JoinedMesh->Get() : Variants->Meshes[Choice - 1].Get();
}

int32 AHexWorldMap::GetSameLevelNeighbors(const FHexCoord& Hex) const
{
	const FHexMapData& Map = TerrainSource->Map;
	const int8 Level = Map.GetLevel(Hex);
	int32 Mask = 0;

	for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
	{
		const FHexCoord Neighbor = Hex.GetNeighbor(Direction);

		if (Map.Contains(Neighbor) && Map.GetLevel(Neighbor) == Level)
		{
			Mask |= 1 << Direction;
		}
	}

	return Mask;
}

void AHexWorldMap::CycleWireframeMode()
{
	CVarHexWireframe->Set((CVarHexWireframe.GetValueOnGameThread() + 1) % 3, ECVF_SetByConsole);
}

void AHexWorldMap::CycleWireframeScope()
{
	CVarHexWireframeScope->Set((CVarHexWireframeScope.GetValueOnGameThread() + 1) % 3, ECVF_SetByConsole);
}

void AHexWorldMap::CycleWireframeTarget()
{
	CVarHexWireframeTarget->Set((CVarHexWireframeTarget.GetValueOnGameThread() + 1) % 3, ECVF_SetByConsole);
}

void AHexWorldMap::CycleWaterView()
{
	CVarHexWater->Set((CVarHexWater.GetValueOnGameThread() + 1) % 4, ECVF_SetByConsole);
}

bool AHexWorldMap::GetDebugViewStatus(TArray<TPair<FString, FString>>& OutLines)
{
	const int32 Mode = FMath::Clamp(CVarHexWireframe.GetValueOnGameThread(), 0, 2);
	const int32 Scope = FMath::Clamp(CVarHexWireframeScope.GetValueOnGameThread(), 0, 2);
	const int32 Target = FMath::Clamp(CVarHexWireframeTarget.GetValueOnGameThread(), 0, 2);
	const int32 WaterView = FMath::Clamp(CVarHexWater.GetValueOnGameThread(), 0, 3);

	OutLines.Reset();
	OutLines.Emplace(TEXT("Wireframe"), WireframeModeNames[Mode]);
	OutLines.Emplace(TEXT("Hexes"), WireframeScopeNames[Scope]);
	OutLines.Emplace(TEXT("Geometry"), WireframeTargetNames[Target]);
	OutLines.Emplace(TEXT("Water"), WaterViewNames[WaterView]);

	// the scope and target change nothing while the wireframe is off
	return Mode == 0 && WaterView == 0;
}

FHexDebugViewSettings AHexWorldMap::GetDebugViewSettings()
{
	FHexDebugViewSettings Settings;
	Settings.Mode = static_cast<EHexWireframeMode>(FMath::Clamp(CVarHexWireframe.GetValueOnGameThread(), 0, 2));
	Settings.Scope = static_cast<EHexWireframeScope>(FMath::Clamp(CVarHexWireframeScope.GetValueOnGameThread(), 0, 2));
	Settings.Target = static_cast<EHexWireframeTarget>(FMath::Clamp(CVarHexWireframeTarget.GetValueOnGameThread(), 0, 2));
	Settings.Water = static_cast<EHexWaterView>(FMath::Clamp(CVarHexWater.GetValueOnGameThread(), 0, 3));
	return Settings;
}

void AHexWorldMap::UpdateDebugView()
{
	const FHexDebugViewSettings Settings = GetDebugViewSettings();
	const EHexWireframeMode Mode = WireframeMaterial ? Settings.Mode : EHexWireframeMode::Off;
	const EHexWireframeScope Scope = Settings.Scope;
	const EHexWireframeTarget Target = Settings.Target;
	const EHexWaterView WaterView = Settings.Water;

	if (Water)
	{
		Water->SetVisibility(WaterView != EHexWaterView::Hidden);
	}

	if (RiverWater)
	{
		RiverWater->SetVisibility(WaterView != EHexWaterView::Hidden);
	}

	// only the triangles of some hexes leave the rest of the map with its normal render, and the surface hides those hexes by its masked copy
	const bool bCut = Mode == EHexWireframeMode::Only && Scope != EHexWireframeScope::All;

	if (Mode != WireframeMode || Target != WireframeTarget || bCut != bWireframeCut)
	{
		WireframeMode = Mode;
		WireframeTarget = Target;
		bWireframeCut = bCut;

		if (bCut)
		{
			UpdateCutMaterials();
		}

		for (const TPair<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Chunk : TerrainChunks)
		{
			ApplyWireframe(Chunk.Value);
		}

		for (const TPair<FIntPoint, FHexChunkFeatures>& Features : ChunkFeatures)
		{
			for (int32 Index = 0; Index < Features.Value.Components.Num(); ++Index)
			{
				ApplyWireframe(Features.Value.Components[Index], Features.Value.Levels[Index]);
			}
		}
	}

	const bool bHidesWaterAtCursor = WaterView == EHexWaterView::HiddenUnderCursor || WaterView == EHexWaterView::HiddenAroundCursor;

	if (Mode == EHexWireframeMode::Off && !bHidesWaterAtCursor && !bWaterHiddenAtCursor)
	{
		return;
	}

	// the hex under the cursor has its own color in every scope of the wireframe
	FVector2f WireHex(NoWireHex, NoWireHex);
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	FVector RayOrigin;
	FVector RayDirection;
	FHexCoord Hex;

	if (PlayerController && PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection) && FindHexAlongRay(RayOrigin, RayDirection, Hex))
	{
		WireHex = FVector2f(Hex.Q, Hex.R);
	}

	// the parameters are set once more after the water is shown again, to clear them
	if (bHidesWaterAtCursor || bWaterHiddenAtCursor)
	{
		const float HideScope = WaterView == EHexWaterView::HiddenUnderCursor ? 1.0f : WaterView == EHexWaterView::HiddenAroundCursor ? 2.0f : 0.0f;

		for (UMaterialInstanceDynamic* Material : { WaterMaterial.Get(), RiverMaterial.Get() })
		{
			if (Material)
			{
				Material->SetScalarParameterValue(WaterHideHexQParameter, WireHex.X);
				Material->SetScalarParameterValue(WaterHideHexRParameter, WireHex.Y);
				Material->SetScalarParameterValue(WaterHideScopeParameter, HideScope);
			}
		}
	}

	bWaterHiddenAtCursor = bHidesWaterAtCursor;

	if (Mode == EHexWireframeMode::Off)
	{
		return;
	}

	for (UMaterialInstanceDynamic* Wire : GetWireframeMaterials())
	{
		Wire->SetScalarParameterValue(WireHexQParameter, WireHex.X);
		Wire->SetScalarParameterValue(WireHexRParameter, WireHex.Y);
		Wire->SetScalarParameterValue(WireScopeParameter, static_cast<float>(Scope));
		// outside the chosen hexes nothing is drawn, also when only the triangles are
		Wire->SetScalarParameterValue(WireOutsideOpacityParameter, 0.0f);
	}

	for (const TPair<TObjectPtr<UMaterialInstanceDynamic>, TObjectPtr<UMaterialInstanceDynamic>>& Cut : CutMaterials)
	{
		Cut.Value->SetScalarParameterValue(WireHexQParameter, WireHex.X);
		Cut.Value->SetScalarParameterValue(WireHexRParameter, WireHex.Y);
		Cut.Value->SetScalarParameterValue(WireScopeParameter, bCut ? static_cast<float>(Scope) : 0.0f);
	}
}

UMaterialInstanceDynamic* AHexWorldMap::CreateCutMaterial(UMaterialInstanceDynamic* Source, UObject* Outer)
{
	const UMaterialInterface* Parent = Source ? Source->Parent.Get() : nullptr;

	if (!Parent)
	{
		return nullptr;
	}

	// the masked instance lies next to its parent, and only this debug view uses it, so a packaged game may lack it
	const FString CutPath = FString::Printf(TEXT("%s_Cut.%s_Cut"), *Parent->GetOutermost()->GetName(), *Parent->GetName());
	UMaterialInterface* CutParent = LoadObject<UMaterialInterface>(nullptr, *CutPath, nullptr, LOAD_NoWarn);

	if (!CutParent)
	{
		UE_LOG(LogHexHero, Warning, TEXT("No masked material %s, run the script that builds %s"), *CutPath, *Parent->GetName());
		return nullptr;
	}

	UMaterialInstanceDynamic* Cut = UMaterialInstanceDynamic::Create(CutParent, Outer);
	Cut->CopyParameterOverrides(Source);
	return Cut;
}

void AHexWorldMap::UpdateCutMaterials()
{
	TArray<UMaterialInstanceDynamic*> Sources = { TerrainMaterial.Get(), CliffMaterial.Get() };

	for (const TPair<int32, TObjectPtr<UMaterialInstanceDynamic>>& FeatureMaterial : FeatureMaterials)
	{
		Sources.Add(FeatureMaterial.Value);
	}

	for (UMaterialInstanceDynamic* Source : Sources)
	{
		if (!Source)
		{
			continue;
		}

		// the parameters may have changed since the copy was made, such as the grid
		if (const TObjectPtr<UMaterialInstanceDynamic>* Existing = CutMaterials.Find(Source))
		{
			(*Existing)->CopyParameterOverrides(Source);
		}
		else if (UMaterialInstanceDynamic* Cut = CreateCutMaterial(Source, this))
		{
			CutMaterials.Add(Source, Cut);
		}
	}
}

UMaterialInterface* AHexWorldMap::GetCutMaterial(UMaterialInterface* Material) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* Cut = CutMaterials.Find(Cast<UMaterialInstanceDynamic>(Material));
	return Cut ? Cut->Get() : Material;
}

void AHexWorldMap::ApplyWireframe(UProceduralMeshComponent* Component) const
{
	// geometry that is not chosen keeps its normal render
	const bool bTerrainWire = WireframeTarget != EHexWireframeTarget::AllButTerrain;
	const bool bCliffWire = WireframeTarget != EHexWireframeTarget::Terrain;
	const bool bOnly = WireframeMode == EHexWireframeMode::Only && !bWireframeCut;
	const bool bOverlay = WireframeMode == EHexWireframeMode::Overlay || bWireframeCut;
	Component->SetMaterial(0, bOnly && bTerrainWire ? WireframeMaterial.Get() : bWireframeCut && bTerrainWire ? GetCutMaterial(TerrainMaterial) : TerrainMaterial.Get());
	Component->SetMaterial(1, bOnly && bCliffWire ? WireframeMaterial.Get() : bWireframeCut && bCliffWire ? GetCutMaterial(CliffMaterial) : CliffMaterial.Get());

	// a wireframe over the normal render needs the same triangles drawn a second time
	for (const int32 Section : { 0, 1 })
	{
		const int32 WireSection = Section == 0 ? TerrainWireSection : CliffWireSection;
		const FProcMeshSection* Source = Component->GetProcMeshSection(Section);

		if (bOverlay && (Section == 0 ? bTerrainWire : bCliffWire) && Source && !Source->ProcVertexBuffer.IsEmpty())
		{
			// the copy comes first, as adding a section can move the others in memory
			const FProcMeshSection Copy = *Source;
			Component->SetProcMeshSection(WireSection, Copy);
			Component->SetMaterial(WireSection, WireframeMaterial);
		}
		else if (WireSection < Component->GetNumSections())
		{
			Component->ClearMeshSection(WireSection);
		}
	}
}

void AHexWorldMap::ApplyWireframe(UInstancedStaticMeshComponent* Component, int32 Level) const
{
	const TObjectPtr<UMaterialInstanceDynamic>* JoinedWire = JoinedWireframeMaterials.Find(Level);
	UMaterialInterface* Wire = JoinedWire ? JoinedWire->Get() : WireframeMaterial.Get();
	const bool bWire = Wire && WireframeMode != EHexWireframeMode::Off && WireframeTarget != EHexWireframeTarget::Terrain;

	// Nanite draws neither wireframe materials nor overlays, so the meshes fall back to their plain triangles
	Component->SetForceDisableNanite(bWire);
	Component->SetOverlayMaterial(bWire && (WireframeMode == EHexWireframeMode::Overlay || bWireframeCut) ? Wire : nullptr);

	UMaterialInterface* Material = GetFeatureMaterial(Level);
	const int32 NumSlots = Component->GetStaticMesh() ? FMath::Max(Component->GetStaticMesh()->GetStaticMaterials().Num(), 1) : 1;

	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		UMaterialInterface* SlotMaterial = Slot == 0 ? (bWire && bWireframeCut ? GetCutMaterial(Material) : Material) : nullptr;
		Component->SetMaterial(Slot, bWire && WireframeMode == EHexWireframeMode::Only && !bWireframeCut ? Wire : SlotMaterial);
	}
}

TArray<UMaterialInstanceDynamic*> AHexWorldMap::GetWireframeMaterials() const
{
	TArray<UMaterialInstanceDynamic*> Materials;

	if (WireframeMaterial)
	{
		Materials.Add(WireframeMaterial);
	}

	for (const TPair<int32, TObjectPtr<UMaterialInstanceDynamic>>& JoinedWire : JoinedWireframeMaterials)
	{
		Materials.Add(JoinedWire.Value);
	}

	return Materials;
}

UMaterialInterface* AHexWorldMap::GetFeatureMaterial(int32 Level) const
{
	if (const TObjectPtr<UMaterialInstanceDynamic>* FeatureMaterial = FeatureMaterials.Find(Level))
	{
		return *FeatureMaterial;
	}

	const TObjectPtr<const UHexLevelConfig>* Definition = Levels.FindByPredicate([Level](const UHexLevelConfig* Candidate)
	{
		return Candidate->Level == Level;
	});

	// a feature with the terrain material shares its instance, and one without a material keeps that of its mesh
	return Definition && !(*Definition)->FeatureMaterial.IsNull() ? TerrainMaterial.Get() : nullptr;
}

void AHexWorldMap::RemoveChunksOutside(const FIntRect& ChunksToKeep)
{
	for (auto It = TerrainChunks.CreateIterator(); It; ++It)
	{
		if (!ChunksToKeep.Contains(It.Key()))
		{
			It.Value()->ClearAllMeshSections();
			FreeChunkComponents.Add(It.Value());

			if (const FHexChunkFeatures* Features = ChunkFeatures.Find(It.Key()))
			{
				for (UInstancedStaticMeshComponent* Component : Features->Components)
				{
					Component->DestroyComponent();
				}

				ChunkFeatures.Remove(It.Key());
			}

			It.RemoveCurrent();
		}
	}
}

void AHexWorldMap::StartChunkBuilds(const FIntRect& WantedChunks)
{
	if (BuildingChunks.Num() >= MapConfig->MaxChunkBuilds)
	{
		return;
	}

	TArray<FIntPoint> MissingChunks;

	for (int32 Row = WantedChunks.Min.Y; Row < WantedChunks.Max.Y; ++Row)
	{
		for (int32 Column = WantedChunks.Min.X; Column < WantedChunks.Max.X; ++Column)
		{
			const FIntPoint Chunk(Column, Row);

			if (!TerrainChunks.Contains(Chunk) && !BuildingChunks.Contains(Chunk))
			{
				MissingChunks.Add(Chunk);
			}
		}
	}

	const FIntPoint Middle = WantedChunks.Min + WantedChunks.Size() / 2;
	MissingChunks.Sort([&Middle](const FIntPoint& A, const FIntPoint& B)
	{
		return (A - Middle).SizeSquared() < (B - Middle).SizeSquared();
	});

	for (const FIntPoint& Chunk : MissingChunks)
	{
		if (BuildingChunks.Num() >= MapConfig->MaxChunkBuilds)
		{
			break;
		}

		BuildingChunks.Add(Chunk);

		UE::Tasks::Launch(UE_SOURCE_LOCATION, [Source = TerrainSource, Chunk]()
		{
			const double StartTime = FPlatformTime::Seconds();

			const int32 MinColumn = Chunk.X * Source->ChunkSize;
			const int32 MinRow = Chunk.Y * Source->ChunkSize;

			FHexBuiltChunk Built;
			Built.Chunk = Chunk;
			HexMeshBuilder::BuildRange(Source->Map, Source->Params, Source->LevelSurfaces, MinColumn, MinRow,
				FMath::Min(MinColumn + Source->ChunkSize, Source->Map.Width), FMath::Min(MinRow + Source->ChunkSize, Source->Map.Height), Built.Mesh, &Source->Cliffs, &Source->Rivers);
			HexMeshBuilder::AddCliffRange(Source->Cliffs, MinColumn, MinRow, FMath::Min(MinColumn + Source->ChunkSize, Source->Map.Width), FMath::Min(MinRow + Source->ChunkSize, Source->Map.Height), Built.CliffMesh);

			UE_LOG(LogHexHero, Verbose, TEXT("Built chunk (%d, %d) with %d vertices in %.1f ms"), Chunk.X, Chunk.Y, Built.Mesh.Vertices.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0);

			Source->BuiltChunks.Enqueue(MoveTemp(Built));
		});
	}
}

void AHexWorldMap::BuildWater()
{
	// one grid of cells of size one, which the component scales and moves over the view
	const int32 Cells = MapConfig->WaterGridCells;
	const int32 RowSize = Cells + 1;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	Vertices.Reserve(RowSize * RowSize);
	Normals.Init(FVector::UpVector, RowSize * RowSize);
	Triangles.Reserve(Cells * Cells * 6);

	for (int32 Y = 0; Y <= Cells; ++Y)
	{
		for (int32 X = 0; X <= Cells; ++X)
		{
			Vertices.Add(FVector(X - Cells * 0.5, Y - Cells * 0.5, 0.0));
		}
	}

	for (int32 Y = 0; Y < Cells; ++Y)
	{
		for (int32 X = 0; X < Cells; ++X)
		{
			const int32 A = Y * RowSize + X + 1;
			const int32 B = (Y + 1) * RowSize + X + 1;
			const int32 C = (Y + 1) * RowSize + X;
			const int32 D = Y * RowSize + X;

			// (C - A) x (B - A) of both triangles points up
			Triangles.Append({ A, C, B, A, D, C });
		}
	}

	Water = CreateMeshComponent(TEXT("Water"));
	Water->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, {}, {}, {}, false);
	Water->SetCastShadow(false);
	Water->SetWorldLocation(FVector(MapBounds.GetCenter(), MapConfig->WaterHeight));
	Water->SetWorldScale3D(FVector(FMath::Max(MapBounds.GetSize().X, MapBounds.GetSize().Y) / Cells));

	if (UMaterialInterface* BaseMaterial = MapConfig->WaterMaterial.LoadSynchronous())
	{
		WaterMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		ApplyLook(WaterMaterial);
		WaterMaterial->SetScalarParameterValue(HexRadiusParameter, MapConfig->HexRadius);
		WaterMaterial->SetTextureParameterValue(HexDataParameter, HexDataTexture);
		WaterMaterial->SetScalarParameterValue(TEXT("WaterClearDepth"), MapConfig->WaterClearDepth);
		WaterMaterial->SetScalarParameterValue(TEXT("WaterOpaqueDepth"), MapConfig->WaterOpaqueDepth);
		WaterMaterial->SetScalarParameterValue(TEXT("WaterEdgeFoam"), MapConfig->WaterEdgeFoam);
		WaterMaterial->SetScalarParameterValue(TEXT("WaterEdgeFoamWidth"), MapConfig->WaterEdgeFoamWidth);
		CreateShoreField();
		Water->SetMaterial(0, WaterMaterial);
		SetGridVisible(bGridVisible);
	}
}

void AHexWorldMap::BuildRiverWater()
{
	if (TerrainSource->Rivers.Lines.IsEmpty())
	{
		return;
	}

	FHexMeshData Mesh;
	HexMeshBuilder::BuildRiverWater(TerrainSource->Map, TerrainSource->Params, TerrainSource->LevelSurfaces, &TerrainSource->Cliffs, TerrainSource->Rivers, Mesh);

	RiverWater = CreateMeshComponent(TEXT("RiverWater"));
	RiverWater->CreateMeshSection_LinearColor(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs, Mesh.Colors, {}, false);
	RiverWater->SetCastShadow(false);

	if (UMaterialInterface* BaseMaterial = MapConfig->RiverMaterial.LoadSynchronous())
	{
		RiverMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		ApplyLook(RiverMaterial);
		RiverMaterial->SetScalarParameterValue(HexRadiusParameter, MapConfig->HexRadius);
		RiverWater->SetMaterial(0, RiverMaterial);
	}

	UE_LOG(LogHexHero, Log, TEXT("Built %d rivers with %d vertices of water"), TerrainSource->Rivers.Lines.Num(), Mesh.Vertices.Num());
}

void AHexWorldMap::CreateMeshRims()
{
	FHexTerrainHeights Terrain;
	HexMeshBuilder::BuildTerrainHeights(TerrainSource->Map, TerrainSource->Params, TerrainSource->LevelSurfaces, &TerrainSource->Cliffs, Terrain, &TerrainSource->Rivers);

	MeshRimTexture = UTexture2D::CreateTransient(Terrain.Width, Terrain.Height, PF_R32_FLOAT);
	MeshRimTexture->SRGB = false;
	MeshRimTexture->Filter = TF_Nearest;
	MeshRimTexture->NeverStream = true;

	FTexture2DMipMap& Mip = MeshRimTexture->GetPlatformData()->Mips[0];
	FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Terrain.Heights.GetData(), Terrain.Heights.Num() * Terrain.Heights.GetTypeSize());
	Mip.BulkData.Unlock();
	MeshRimTexture->UpdateResource();

	TArray<TObjectPtr<UMaterialInstanceDynamic>> RimMaterials;
	FeatureMaterials.GenerateValueArray(RimMaterials);

	for (const TPair<int32, TObjectPtr<UMaterialInstanceDynamic>>& JoinedWire : JoinedWireframeMaterials)
	{
		RimMaterials.Add(JoinedWire.Value);
	}

	const TArray<uint8> Depths = HexMeshBuilder::BuildMassifDepths(TerrainSource->Map, TerrainSource->LevelSurfaces);
	MassifTexture = UTexture2D::CreateTransient(TerrainSource->Map.Width, TerrainSource->Map.Height, PF_G8);
	MassifTexture->SRGB = false;
	MassifTexture->Filter = TF_Nearest;
	MassifTexture->NeverStream = true;

	FTexture2DMipMap& MassifMip = MassifTexture->GetPlatformData()->Mips[0];
	FMemory::Memcpy(MassifMip.BulkData.Lock(LOCK_READ_WRITE), Depths.GetData(), Depths.Num());
	MassifMip.BulkData.Unlock();
	MassifTexture->UpdateResource();

	for (UMaterialInstanceDynamic* RimMaterial : RimMaterials)
	{
		RimMaterial->SetTextureParameterValue(MassifDataParameter, MassifTexture);
		RimMaterial->SetTextureParameterValue(MeshRimsParameter, MeshRimTexture);
		RimMaterial->SetScalarParameterValue(RimOriginXParameter, Terrain.Origin.X);
		RimMaterial->SetScalarParameterValue(RimOriginYParameter, Terrain.Origin.Y);
		RimMaterial->SetScalarParameterValue(RimStepParameter, static_cast<float>(TerrainSource->Params.HexRadius) / TerrainSource->Params.Subdivisions);
	}
}

void AHexWorldMap::UpdateWaterGrid(const FBox2D& ViewArea)
{
	if (!Water)
	{
		return;
	}

	// only the part on the map needs water, and a ray past the horizon must not stretch the grid
	const FBox2D Area(FVector2D::Max(ViewArea.Min, MapBounds.Min), FVector2D::Min(ViewArea.Max, MapBounds.Max));

	if (!Area.bIsValid || Area.Min.X >= Area.Max.X || Area.Min.Y >= Area.Max.Y)
	{
		return;
	}

	const FVector2D Size = Area.GetSize();

	// the cell doubles at a time and the grid moves by whole cells, so vertices keep their places on the map while the camera moves
	const double Wanted = FMath::Max3(Size.X, Size.Y, 1.0) * 1.1 / MapConfig->WaterGridCells;
	const double Cell = FMath::Pow(2.0, FMath::CeilToDouble(FMath::Log2(FMath::Max(Wanted, MapConfig->WaterGridMinCell))));
	const FVector2D Center = Area.GetCenter();
	const FVector Location(FMath::GridSnap(Center.X, Cell), FMath::GridSnap(Center.Y, Cell), MapConfig->WaterHeight);

	Water->SetWorldScale3D(FVector(Cell, Cell, 1.0));
	Water->SetWorldLocation(Location);
}

void AHexWorldMap::CreateShoreField()
{
	const double StartTime = FPlatformTime::Seconds();

	FHexShoreField Field;
	HexMeshBuilder::BuildShoreField(TerrainSource->Map, TerrainSource->Params, TerrainSource->LevelSurfaces, &TerrainSource->Cliffs, MapBounds, Field, &TerrainSource->Rivers);

	const auto CreateTexture = [&Field]<typename FPixel>(const TArray<FPixel>& Pixels, EPixelFormat Format)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Field.Width, Field.Height, Format);
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	};

	ShoreFieldTexture = CreateTexture(Field.Pixels, PF_B8G8R8A8);
	ShoreHeightTexture = CreateTexture(Field.HeightPixels, PF_G16);

	WaterMaterial->SetTextureParameterValue(ShoreFieldParameter, ShoreFieldTexture);
	WaterMaterial->SetTextureParameterValue(ShoreHeightParameter, ShoreHeightTexture);
	WaterMaterial->SetScalarParameterValue(ShoreMinXParameter, Field.Origin.X);
	WaterMaterial->SetScalarParameterValue(ShoreMinYParameter, Field.Origin.Y);
	WaterMaterial->SetScalarParameterValue(ShoreSizeXParameter, Field.Width * Field.Step);
	WaterMaterial->SetScalarParameterValue(ShoreSizeYParameter, Field.Height * Field.Step);

	UE_LOG(LogHexHero, Log, TEXT("Built a shore field of %dx%d in %.2f s"), Field.Width, Field.Height, FPlatformTime::Seconds() - StartTime);
}

UProceduralMeshComponent* AHexWorldMap::CreateMeshComponent(const FName& Name)
{
	UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(this, Name);
	Component->SetupAttachment(RootComponent);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->RegisterComponent();

	return Component;
}

void AHexWorldMap::ApplyLook(UMaterialInstanceDynamic* Target) const
{
	HexMaterialConfig::Apply(Target, SurfaceConfig);
	HexMaterialConfig::Apply(Target, WaterLookConfig);
	Target->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(SunDirection));
	Target->SetScalarParameterValue(TEXT("WaterHeight"), MapConfig->WaterHeight);

	// the terrain draws the sand past the top of a bank from what HexMeshBuilder wrote by these
	Target->SetScalarParameterValue(TEXT("RiverBankTopColor"), float(MapConfig->Tuning.RiverBankTopColor));
	Target->SetScalarParameterValue(TEXT("RiverBankColorReach"), float(MapConfig->Tuning.RiverBankColorReach));
}
