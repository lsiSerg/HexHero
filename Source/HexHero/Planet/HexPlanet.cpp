// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanet.h"
#include "HexHero.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HexBiomeConfig.h"
#include "HexMapSettings.h"
#include "HexMaterialConfig.h"
#include "HexMeshBuilder.h"
#include "HexPlanetGenerator.h"
#include "HexPlanetKit.h"
#include "HexPlanetCloudConfig.h"
#include "HexPlanetConfig.h"
#include "HexPlanetKitConfig.h"
#include "HexPlanetSeaField.h"
#include "HexPlanetWaterConfig.h"
#include "HexTerrainLayersConfig.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "HAL/IConsoleManager.h"
#include "ProceduralMeshComponent.h"

namespace
{
	TAutoConsoleVariable<float> CVarHexPlanetCoarseDistance(TEXT("hex.PlanetCoarseDistance"), 0.0f,
		TEXT("Distance from the camera beyond which the planet draws the coarse mesh of its chunks, in place of CoarseDistance of DA_HexPlanet. 0 keeps that one."));

	TAutoConsoleVariable<float> CVarHexPlanetCoarseBand(TEXT("hex.PlanetCoarseBand"), 0.0f,
		TEXT("Half the width of the band over which the planet dissolves its chunks into their coarse meshes, in place of CoarseBand of DA_HexPlanet. 0 keeps that one."));

	const TCHAR* LevelNames[] = {TEXT("deep sea"), TEXT("shallows"), TEXT("plains"), TEXT("hills"), TEXT("mountains")};

	/** Cells whose centers the water material compares a pixel with, to find whether it lies in a hidden cell: the cell, its neighbors and theirs */
	const int32 MaxWaterHideCells = 19;

	/** Mesh of one section */
	struct FSectionMesh
	{
		TArray<FVector> Positions;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		/** The cell of each vertex and where it lies in it, for the marks of cells, see HexPlanetSurface::GetCellUV and GetLocalUV */
		TArray<FVector2D> CellUVs;
		TArray<FVector2D> LocalUVs;
		TArray<FLinearColor> Colors;

		int32 AddVertex(const FVector& Position, const FVector& Normal, float U, const FLinearColor& Color)
		{
			Normals.Add(Normal);
			UVs.Add(FVector2D(U, 0.0));
			Colors.Add(Color);
			return Positions.Add(Position);
		}

		/** Adds a triangle that shows on the side Outward points to, where (C - A) x (B - A) points */
		void AddTriangle(int32 A, int32 B, int32 C, const FVector& Outward)
		{
			const FVector Facing = FVector::CrossProduct(Positions[C] - Positions[A], Positions[B] - Positions[A]);

			if (FVector::DotProduct(Facing, Outward) >= 0.0)
			{
				Triangles.Append({A, B, C});
			}
			else
			{
				Triangles.Append({A, C, B});
			}
		}

		void Reset()
		{
			Positions.Reset();
			Triangles.Reset();
			Normals.Reset();
			UVs.Reset();
			CellUVs.Reset();
			LocalUVs.Reset();
			Colors.Reset();
		}
	};

	/** Values of the config that the sea field and the grid lines of the sea need, copied so a rebuild in the background reads no UObject */
	struct FSeaParams
	{
		double Radius = 0.0;
		double ShallowDepth = 0.0;
		double DeepDepth = 0.0;
		double HexRadius = 0.0;
		int32 DepthSmoothing = 0;
		int32 SeaFieldSize = 0;
		double ShoreSoftness = 0.0;
		double ShoreStretchJoin = 0.0;
		double ShoreCoastFade = 0.0;
		double ShoreCoastBlend = 0.0;
		/** Width of the grid lines of the sea as a share of the way to the center */
		double GridStrip = 0.0;

		static FSeaParams Make(const UHexPlanetConfig& Config, double Radius)
		{
			FSeaParams Params;
			Params.Radius = Radius;
			Params.ShallowDepth = Config.ShallowDepth;
			Params.DeepDepth = Config.DeepDepth;
			Params.HexRadius = Config.HexRadius;
			Params.DepthSmoothing = Config.DepthSmoothing;
			Params.SeaFieldSize = Config.SeaFieldSize;
			Params.ShoreSoftness = Config.ShoreSoftness;
			Params.ShoreStretchJoin = Config.ShoreStretchJoin;
			Params.ShoreCoastFade = Config.ShoreCoastFade;
			Params.ShoreCoastBlend = Config.ShoreCoastBlend;
			// the strip is as wide as the widest line, which M_HexPlanet draws up to a pixel of 1.5 / 8 of the way to the center, GridMinPixels wide,
			// or as an outline of a marked cell, MarkWidth wide, which past that is cut to the strip on the water
			Params.GridStrip = FMath::Clamp(FMath::Max3(double(Config.GridWidth), Config.GridMinPixels * 1.5 / 8.0, double(Config.MarkWidth)) * 1.2, 0.01, 0.5);
			return Params;
		}
	};

	/** Builds the pixels of the cubemap of the sea, from the depth of the seabed, the distance to land and the ice of each cell, and the real coast
	 *  by chunk (HexPlanetSurface::FBuild::Coasts) */
	TArray<FColor> BuildSeaPixels(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<uint8> Frozen, const TArray<TArray<FVector3f>>& Coasts,
		const FSeaParams& Params)
	{
		const TArray<FHexSphereCell>& Cells = Grid.GetCells();
		TArray<HexPlanetSeaField::FCellValues> Values;
		Values.SetNum(Cells.Num());

		// the distance to the coast is measured per texel from the land cells (HexPlanetSeaField::BuildPixels)
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			Values[Cell].bLand = Levels[Cell] >= 0;
			Values[Cell].Depth = Levels[Cell] >= 0 ? 0.0f : float(Levels[Cell] == -1 ? Params.ShallowDepth : Params.DeepDepth);
			Values[Cell].Frozen = float(Frozen[Cell]);
		}

		// the water sees a smooth seabed, as the shore field of the flat map blurs it
		for (int32 Pass = 0; Pass < Params.DepthSmoothing; ++Pass)
		{
			TArray<HexPlanetSeaField::FCellValues> Smoothed = Values;

			for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
			{
				float Sum = Values[Cell].Depth;

				for (const int32 Neighbor : Cells[Cell].Neighbors)
				{
					Sum += Values[Neighbor].Depth;
				}
				Smoothed[Cell].Depth = Sum / (Cells[Cell].Neighbors.Num() + 1);
			}
			Values = MoveTemp(Smoothed);
		}

		// a land cell counts as a disc as wide as the middles of its sides, where the coast runs
		HexPlanetSeaField::FShoreParams Shore;
		Shore.Radius = Params.Radius;
		Shore.LandRadius = FMath::Sqrt(3.0) * 0.5 * Params.HexRadius;
		Shore.Softness = Params.ShoreSoftness;
		Shore.StretchJoin = Params.ShoreStretchJoin;
		Shore.CoastFade = Params.ShoreCoastFade;
		Shore.CoastBlend = Params.ShoreCoastBlend;
		TArray<FVector3f> CoastPoints;
		for (const TArray<FVector3f>& Chunk : Coasts)
		{
			CoastPoints.Append(Chunk);
		}
		return HexPlanetSeaField::BuildPixels(Grid, Values, Params.SeaFieldSize, Shore, CoastPoints);
	}

	/** Builds the grid lines of the cells of the sea: a strip inside the sides of each at the height of the sea, whose U goes from 1 at a side
	 *  inwards as on the surface, so M_HexPlanetWaterGrid draws the same line as M_HexPlanet */
	FSectionMesh BuildWaterGridMesh(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, const FSeaParams& Params)
	{
		const double Strip = Params.GridStrip;
		const TArray<FVector3d>& Corners = Grid.GetCorners();
		FSectionMesh Mesh;

		for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
		{
			if (Levels[Cell] >= 0)
			{
				continue;
			}

			const FHexSphereCell& Each = Grid.GetCells()[Cell];
			const int32 NumCorners = Each.Corners.Num();

			for (int32 Index = 0; Index < NumCorners; ++Index)
			{
				const FVector3d& From = Corners[Each.Corners[(Index + NumCorners - 1) % NumCorners]];
				const FVector3d& To = Corners[Each.Corners[Index]];
				const FVector3d FromInside = FMath::Lerp(From, Each.Center, Strip).GetSafeNormal();
				const FVector3d ToInside = FMath::Lerp(To, Each.Center, Strip).GetSafeNormal();

				const int32 A = Mesh.AddVertex(From * Params.Radius, From, 1.0f, FLinearColor::White);
				const int32 B = Mesh.AddVertex(To * Params.Radius, To, 1.0f, FLinearColor::White);
				const int32 C = Mesh.AddVertex(ToInside * Params.Radius, ToInside, float(1.0 - Strip), FLinearColor::White);
				const int32 D = Mesh.AddVertex(FromInside * Params.Radius, FromInside, float(1.0 - Strip), FLinearColor::White);

				// the same marks of cells as on the surface, so a marked side of a cell of the sea shows on the water
				const FVector2D CellUV = HexPlanetSurface::GetCellUV(Cell);
				const FVector2D FromLocal = HexPlanetSurface::GetLocalUV(NumCorners, Index, 1.0, 0.0);
				const FVector2D ToLocal = HexPlanetSurface::GetLocalUV(NumCorners, Index, 0.0, 1.0);
				Mesh.CellUVs.Append({CellUV, CellUV, CellUV, CellUV});
				Mesh.LocalUVs.Append({FromLocal, ToLocal, ToLocal * (1.0 - Strip), FromLocal * (1.0 - Strip)});
				Mesh.AddTriangle(A, B, C, Each.Center);
				Mesh.AddTriangle(A, C, D, Each.Center);
			}
		}
		return Mesh;
	}

	/** Changes cells to a level, and the cells up to some rings around them, or along the arc to another cell and around it */
	void SetLevelCommand(const TArray<FString>& Args, UWorld* World)
	{
		int32 Cell = INDEX_NONE;
		int32 Level = 0;
		int32 Rings = 0;
		int32 ToCell = INDEX_NONE;
		const bool bParsed = Args.Num() >= 2 && LexTryParseString(Cell, *Args[0]) && LexTryParseString(Level, *Args[1])
			&& (Args.Num() < 3 || LexTryParseString(Rings, *Args[2])) && (Args.Num() < 4 || LexTryParseString(ToCell, *Args[3]));

		AHexPlanet* Planet = nullptr;
		for (TActorIterator<AHexPlanet> It(World); It; ++It)
		{
			Planet = *It;
			break;
		}

		if (!bParsed || !Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell) || (Args.Num() >= 4 && !Planet->GetGrid().GetCells().IsValidIndex(ToCell)))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetSetLevel <Cell> <Level> [Rings] [ToCell], on a planet, with a level from -2 (deep sea) to 2 (mountains)"));
			return;
		}

		const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
		TMap<int32, int32> NewLevels;
		NewLevels.Add(Cell, Level);
		TArray<int32> Front = {Cell};

		// a line of cells along the arc to ToCell, as a stroke of the brush walks it
		if (ToCell != INDEX_NONE)
		{
			const FVector3d From = Cells[Cell].Center;
			const FVector3d To = Cells[ToCell].Center;
			const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FMath::Acos(FMath::Clamp(FVector3d::DotProduct(From, To), -1.0, 1.0))
				/ FMath::Max(FMath::Acos(FMath::Clamp(FVector3d::DotProduct(From, Cells[Cells[Cell].Neighbors[0]].Center), -1.0, 1.0)), 1.0e-6) * 3.0));
			int32 Walked = Cell;
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				Walked = Planet->GetGrid().FindCell(FMath::Lerp(From, To, double(Step) / Steps).GetSafeNormal(), Walked);
				if (!NewLevels.Contains(Walked))
				{
					NewLevels.Add(Walked, Level);
					Front.Add(Walked);
				}
			}
		}

		for (int32 Ring = 0; Ring < Rings; ++Ring)
		{
			TArray<int32> Next;
			for (const int32 Each : Front)
			{
				for (const int32 Neighbor : Cells[Each].Neighbors)
				{
					if (!NewLevels.Contains(Neighbor))
					{
						NewLevels.Add(Neighbor, Level);
						Next.Add(Neighbor);
					}
				}
			}
			Front = MoveTemp(Next);
		}
		Planet->SetCellLevels(NewLevels);
	}

	/** Changes cells to a biome, by its planet letter, and the cells up to some rings around them, where their level allows it */
	void SetBiomeCommand(const TArray<FString>& Args, UWorld* World)
	{
		int32 Cell = INDEX_NONE;
		int32 Rings = 0;
		AHexPlanet* Planet = nullptr;
		for (TActorIterator<AHexPlanet> It(World); It; ++It)
		{
			Planet = *It;
			break;
		}

		const int32 Biome = Planet && Args.Num() >= 2 && Args[1].Len() == 1
			? Planet->GetBiomes().IndexOfByPredicate([&Args](const UHexBiomeConfig* Each) { return Each->PlanetLetter.Equals(Args[1], ESearchCase::IgnoreCase); })
			: INDEX_NONE;
		const bool bParsed = Args.Num() >= 2 && LexTryParseString(Cell, *Args[0]) && (Args.Num() < 3 || LexTryParseString(Rings, *Args[2]));

		if (!bParsed || Biome == INDEX_NONE || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetSetBiome <Cell> <Letter> [Rings], on a planet, with the PlanetLetter of a biome"));
			return;
		}

		const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
		TMap<int32, int32> NewBiomes;
		NewBiomes.Add(Cell, Biome);
		TArray<int32> Front = {Cell};

		for (int32 Ring = 0; Ring < Rings; ++Ring)
		{
			TArray<int32> Next;
			for (const int32 Each : Front)
			{
				for (const int32 Neighbor : Cells[Each].Neighbors)
				{
					if (!NewBiomes.Contains(Neighbor))
					{
						NewBiomes.Add(Neighbor, Biome);
						Next.Add(Neighbor);
					}
				}
			}
			Front = MoveTemp(Next);
		}
		Planet->SetCells({}, NewBiomes);
	}

	/** Sets the relief of a cell by hand, as the brush of relief does: its version, turn and mirror, -1 for the hash, and a factor on its height */
	void SetReliefCommand(const TArray<FString>& Args, UWorld* World)
	{
		int32 Cell = INDEX_NONE;
		int32 Version = -1;
		int32 Turn = -1;
		int32 Mirror = -1;
		float Scale = 1.0f;
		const bool bParsed = Args.Num() >= 2 && LexTryParseString(Cell, *Args[0]) && LexTryParseString(Version, *Args[1])
			&& (Args.Num() < 3 || LexTryParseString(Turn, *Args[2])) && (Args.Num() < 4 || LexTryParseString(Mirror, *Args[3]))
			&& (Args.Num() < 5 || LexTryParseString(Scale, *Args[4]));

		AHexPlanet* Planet = nullptr;
		for (TActorIterator<AHexPlanet> It(World); It; ++It)
		{
			Planet = *It;
			break;
		}

		if (!bParsed || !Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetSetRelief <Cell> <Version> [Turn] [Mirror] [Scale], with -1 for the hash and Scale 1 for none"));
			return;
		}

		HexPlanetSurface::FReliefOverride Override;
		Override.Version = int8(FMath::Clamp(Version, -1, 100));
		Override.Turn = int8(FMath::Clamp(Turn, -1, 5));
		Override.Mirror = int8(FMath::Clamp(Mirror, -1, 1));
		Override.Scale = FMath::Clamp(Scale, 0.1f, 5.0f);
		Planet->SetReliefOverrides({{Cell, Override}});
	}

	/** Freezes the open sea next to snow land into shore ice on the planet shown, as the generation does, and saves its biomes with save */
	void ShoreIceCommand(const TArray<FString>& Args, UWorld* World)
	{
		int32 Rings = 1;
		AHexPlanet* Planet = nullptr;
		for (TActorIterator<AHexPlanet> It(World); It; ++It)
		{
			Planet = *It;
			break;
		}

		const bool bSave = Args.Num() >= 2 && Args[1].Equals(TEXT("save"), ESearchCase::IgnoreCase);
		const int32 Ice = Planet ? Planet->GetBiomes().IndexOfByPredicate([](const UHexBiomeConfig* Each)
			{ return Each && Each->PlanetLetter.Len() == 1 && Each->PlanetLetter[0] == HexPlanetGenerator::Biome::Ice; }) : INDEX_NONE;
		if (!Planet || Ice == INDEX_NONE || (Args.Num() >= 1 && !LexTryParseString(Rings, *Args[0])))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetShoreIce [Rings] [save], on a planet with an ice biome"));
			return;
		}

		TArray<TCHAR> Letters;
		for (const uint8 Biome : Planet->GetCellBiomes())
		{
			const UHexBiomeConfig* Config = Planet->GetBiomes().IsValidIndex(Biome) ? Planet->GetBiomes()[Biome].Get() : nullptr;
			Letters.Add(Config && Config->PlanetLetter.Len() == 1 ? Config->PlanetLetter[0] : TEXT(' '));
		}

		TMap<int32, int32> NewBiomes;
		for (const int32 Cell : HexPlanetGenerator::AddShoreIce(Planet->GetGrid(), Planet->GetLevels(), Letters, FMath::Max(Rings, 0)))
		{
			NewBiomes.Add(Cell, Ice);
		}
		UE_LOG(LogHexHero, Log, TEXT("Planet: shore ice froze %d cells"), NewBiomes.Num());
		if (NewBiomes.IsEmpty())
		{
			return;
		}
		Planet->SetCells({}, NewBiomes);
		Planet->MarkLevelsEdited();

		FString Error;
		if (bSave && Planet->IsSaved() && !Planet->SaveLevels(Error))
		{
			UE_LOG(LogHexHero, Error, TEXT("Planet: could not save the shore ice: %s"), *Error);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ShoreIceConsoleCommand(TEXT("hex.PlanetShoreIce"),
		TEXT("hex.PlanetShoreIce [Rings] [save]: freezes the open sea up to Rings cells (1) from snow land into shore ice, as the generation does, and with save writes biomes.txt"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ShoreIceCommand));

	FAutoConsoleCommandWithWorldAndArgs SetReliefConsoleCommand(TEXT("hex.PlanetSetRelief"),
		TEXT("hex.PlanetSetRelief <Cell> <Version> [Turn] [Mirror] [Scale]: sets the relief of a mountain or hill by hand, as the brush of relief does, -1 leaving a choice to the hash"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetReliefCommand));

	FAutoConsoleCommandWithWorldAndArgs SetBiomeConsoleCommand(TEXT("hex.PlanetSetBiome"),
		TEXT("hex.PlanetSetBiome <Cell> <Letter> [Rings]: sets the biome of a cell of the planet by its PlanetLetter, and of the cells up to Rings around it, where their level allows it"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetBiomeCommand));

	FAutoConsoleCommandWithWorldAndArgs SetLevelConsoleCommand(TEXT("hex.PlanetSetLevel"),
		TEXT("hex.PlanetSetLevel <Cell> <Level> [Rings] [ToCell]: sets the level of a cell of the planet, -2 deep sea to 2 mountains, and of the cells up to Rings around it, or of a line of cells to ToCell and Rings around it"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetLevelCommand));
}

/** What a rebuild of changed levels reads, all of it copied when it starts, and what it makes */
struct AHexPlanet::FLevelRebuild
{
	TArray<int8> OldLevels;
	TArray<int32> OldMassifSteps;
	TArray<int8> Levels;
	HexPlanetSurface::FReliefOverrides OldOverrides;
	HexPlanetSurface::FReliefOverrides Overrides;
	TArray<uint8> OldFrozen;
	TArray<uint8> Frozen;
	/** The real coast by chunk as it was, which the task brings up to date with the chunks it builds, for the sea field */
	TArray<TArray<FVector3f>> Coasts;

	TArray<int32> MassifSteps;
	TArray<int32> Chunks;
	HexPlanetSurface::FBuild Surface;
	TArray<FColor> SeaPixels;
	FSectionMesh WaterGrid;
	bool bWaterGridBuilt = false;

	/** Milliseconds of the steps in the background */
	double SurfaceMs = 0.0;
	double SeaFieldMs = 0.0;
	double WaterGridMs = 0.0;
	double StartTime = 0.0;
};

AHexPlanet::AHexPlanet()
{
	PrimaryActorTick.bCanEverTick = true;

	// after the camera has moved, so the water cap is under it in the same frame
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Surface"));
	RootComponent = Surface;

	Water = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Water"));
	Water->SetupAttachment(Surface);
	Water->SetCastShadow(false);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// the grid lines of the sea are drawn after the water, which writes no depth, so they lie on it
	WaterGrid = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterGrid"));
	WaterGrid->SetupAttachment(Surface);
	WaterGrid->SetCastShadow(false);
	WaterGrid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterGrid->SetTranslucentSortPriority(1);

	// the clouds are drawn after the water and its grid, as the water cap has its bounds nearer the camera than the shell, whose center is that of the planet
	Clouds = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Surface);
	Clouds->SetCastShadow(false);
	Clouds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Clouds->SetTranslucentSortPriority(2);

	SkyClouds = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SkyClouds"));
	SkyClouds->SetupAttachment(Surface);
	SkyClouds->SetCastShadow(false);
	SkyClouds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyClouds->SetTranslucentSortPriority(2);

	Wireframe = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Wireframe"));
	Wireframe->SetupAttachment(Surface);
	Wireframe->SetCastShadow(false);
	Wireframe->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// the wireframes are drawn after the water and the clouds, which are translucent too, and the cells around the mouse after the rest,
	// so their colors lie on top
	Wireframe->SetTranslucentSortPriority(3);
	HoverWireframe = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HoverWireframe"));
	HoverWireframe->SetupAttachment(Surface);
	HoverWireframe->SetCastShadow(false);
	HoverWireframe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoverWireframe->SetTranslucentSortPriority(4);
}

void AHexPlanet::BeginPlay()
{
	Super::BeginPlay();

	// the cells find their place from world positions, as on the flat map
	SetActorLocation(FVector::ZeroVector);

	// everything is built from the configs, and the materials of the surface, the water and the clouds take their values as they are made
	const auto LoadConfig = [](auto& Soft, const TCHAR* What)
	{
		using FConfig = typename TRemoveReference<decltype(Soft)>::Type::ElementType;
		FConfig* Loaded = Soft.LoadSynchronous();
		if (!Loaded)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s config %s is missing, run Tools/create_planet_assets.py"), What, *Soft.ToString());
			Loaded = GetMutableDefault<FConfig>();
		}
		return Loaded;
	};
	Settings = LoadConfig(PlanetConfig, TEXT("planet"));
	LoadedWaterConfig = LoadConfig(WaterConfig, TEXT("water"));
	LoadedCloudConfig = LoadConfig(CloudConfig, TEXT("cloud"));

	// a new planet of the menu, which has no folder until it is saved, or the planet asked for in the URL, or the one of the saved view,
	// or the first in Content/Data/Planets
	const bool bNewPlanet = HexPlanetFile::GetNewPlanet(GetWorld(), Manifest);
	PlanetFolder = bNewPlanet ? FString() : HexPlanetFile::FindPlanetToOpen(GetWorld());
	FString ManifestError;

	if (bNewPlanet)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: a new planet, not saved yet"));
	}
	else if (PlanetFolder.IsEmpty())
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: there is no planet in %s, generating one from the defaults"), HexPlanetFile::RootFolder);
	}
	else if (!HexPlanetFile::ReadManifest(PlanetFolder, Manifest, ManifestError))
	{
		UE_LOG(LogHexHero, Error, TEXT("Planet: %s"), *ManifestError);
	}

	const double StartTime = FPlatformTime::Seconds();
	Grid = FHexSphereGrid::Build(Manifest.Frequency, Settings->ChunkSteps);
	const double GridTime = FPlatformTime::Seconds();

	const double HexArea = 1.5 * FMath::Sqrt(3.0) * Settings->HexRadius * Settings->HexRadius;
	Radius = FMath::Sqrt(Grid.GetCells().Num() * HexArea / (4.0 * UE_DOUBLE_PI));

	double TotalArea = 0.0;
	double SmallestArea = TNumericLimits<double>::Max();
	double LargestArea = 0.0;
	double WorstSides = 1.0;

	for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
	{
		const double Area = Grid.GetCellArea(Cell);
		TotalArea += Area;

		if (!Grid.GetCells()[Cell].IsPentagon())
		{
			SmallestArea = FMath::Min(SmallestArea, Area);
			LargestArea = FMath::Max(LargestArea, Area);
			WorstSides = FMath::Max(WorstSides, Grid.GetCellSideRatio(Cell));
		}
	}
	MeanCellArea = TotalArea / Grid.GetCells().Num();

	// what a saved planet lacks is generated and written at once, so the planet stays as it is when the generation changes later
	const bool bHadLevels = IsSaved() && HexPlanetFile::HasLevels(PlanetFolder);
	const bool bHadBiomes = IsSaved() && HexPlanetFile::HasBiomes(PlanetFolder);

	LoadLevels();
	LoadBiomeConfigs();
	LoadBiomes();
	LoadRelief();
	WriteGeneratedFiles(!bHadLevels, !bHadBiomes && !BiomeConfigs.IsEmpty());
	CreateCellMarks();
	CreateBiomeTexture();
	LayerParamsTexture = GetDefault<UHexMapSettings>()->LoadTerrainLayers()->CreateParamsTexture();

	// a component per chunk, so what lies behind the planet or off the screen is culled, and a rebuild of changed levels recreates the render
	// state of its chunks alone
	for (int32 Chunk = 0; Chunk < Grid.GetNumChunks(); ++Chunk)
	{
		UProceduralMeshComponent* ChunkMesh = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("SurfaceChunk%d"), Chunk));
		ChunkMesh->SetupAttachment(Surface);
		ChunkMesh->RegisterComponent();
		SurfaceChunks.Add(ChunkMesh);

		// far away the chunk draws its coarse mesh, which UpdateChunkLod shows and hides
		if (Settings->CoarseDistance > 0.0f)
		{
			UProceduralMeshComponent* CoarseMesh = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("CoarseChunk%d"), Chunk));
			CoarseMesh->SetupAttachment(Surface);
			CoarseMesh->SetVisibility(false);
			CoarseMesh->RegisterComponent();
			CoarseChunks.Add(CoarseMesh);
		}
	}

	BuildSurface();
	const double MeshTime = FPlatformTime::Seconds();
	BuildSeaField();
	BuildWater();
	BuildWaterGrid();
	BuildClouds();
	LoadedParameterCollection = ParameterCollection.LoadSynchronous();
	BuildAuroras();

	if (UMaterialInterface* WireBase = WireframeMaterialAsset.LoadSynchronous())
	{
		WireframeMaterial = UMaterialInstanceDynamic::Create(WireBase, this);
		HoverWireframeMaterial = UMaterialInstanceDynamic::Create(WireBase, this);
		HexMaterialConfig::Apply(WireframeMaterial, Settings);
		HexMaterialConfig::Apply(HoverWireframeMaterial, Settings);
		HoverWireframeMaterial->SetScalarParameterValue(TEXT("UseVertexColor"), 1.0f);
	}
	else
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: wireframe material %s is missing, run Tools/create_planet_assets.py"), *WireframeMaterialAsset.ToString());
	}

	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		Sun = *It;
		break;
	}

	UE_LOG(LogHexHero, Log, TEXT("Planet %s: %d cells, radius %.0f, hex area from %.3f to %.3f of the mean (x%.3f), worst sides x%.3f, grid %.2f s, mesh %.2f s, sea field %.2f s"),
		PlanetFolder.IsEmpty() ? TEXT("(not saved)") : *PlanetFolder, Grid.GetCells().Num(), Radius, SmallestArea / MeanCellArea, LargestArea / MeanCellArea, LargestArea / SmallestArea, WorstSides,
		GridTime - StartTime, MeshTime - GridTime, FPlatformTime::Seconds() - MeshTime);
}

void AHexPlanet::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RebuildTask.IsValid())
	{
		RebuildTask.Wait();
		RebuildTask = UE::Tasks::FTask();
	}
	Rebuild.Reset();
	bLevelsPending = false;
	Super::EndPlay(EndPlayReason);
}

void AHexPlanet::SetCells(const TMap<int32, int32>& NewLevels, const TMap<int32, int32>& NewBiomes)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	int32 NumChanged = 0;
	TArray<int32> LevelCells;
	TArray<int32> BiomeCells;

	for (const TPair<int32, int32>& Each : NewLevels)
	{
		if (!Cells.IsValidIndex(Each.Key))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: there is no cell %d to set the level of"), Each.Key);
			continue;
		}

		// the pentagons wait for set pieces of their own, and the mountain kit has no five-sided pieces
		int32 Level = FMath::Clamp(Each.Value, -2, 2);
		if (Cells[Each.Key].IsPentagon() && Level > 1)
		{
			Level = 1;
		}

		if (Levels[Each.Key] != Level)
		{
			Levels[Each.Key] = int8(Level);
			LevelCells.Add(Each.Key);
			++NumChanged;
		}
	}

	bool bFreezeChanged = false;
	const auto SetBiome = [&](int32 Cell, int32 Biome)
	{
		if (CellBiomes[Cell] != Biome && BiomeConfigs.IsValidIndex(Biome) && BiomeConfigs.IsValidIndex(CellBiomes[Cell]))
		{
			bFreezeChanged |= Levels[Cell] < 0 && BiomeConfigs[CellBiomes[Cell]]->bFrozen != BiomeConfigs[Biome]->bFrozen;
			CellBiomes[Cell] = uint8(Biome);
			BiomeCells.Add(Cell);
		}
	};

	for (const TPair<int32, int32>& Each : NewBiomes)
	{
		if (Cells.IsValidIndex(Each.Key) && IsBiomeAllowed(Each.Value, Levels[Each.Key]))
		{
			SetBiome(Each.Key, Each.Value);
		}
	}

	// a cell that moved to a level its biome does not allow takes one of its neighbors, as when the sea floods a desert
	for (const int32 Cell : LevelCells)
	{
		if (!IsBiomeAllowed(CellBiomes[Cell], Levels[Cell]))
		{
			SetBiome(Cell, FindFittingBiome(Cell));
		}
	}

	// a mountain draws the rock of its biome, so a cell that became one or stopped being one changes its layers too
	for (const int32 Cell : LevelCells)
	{
		BiomeCells.AddUnique(Cell);
	}
	if (!BiomeCells.IsEmpty())
	{
		UpdateBiomePixels(BiomeCells);
	}

	if (NumChanged > 0 || bFreezeChanged)
	{
		bLevelsPending = true;
		StartRebuild();
	}
}

bool AHexPlanet::IsBiomeAllowed(int32 Biome, int32 Level) const
{
	return BiomeConfigs.IsValidIndex(Biome) && BiomeConfigs[Biome]->AllowedLevels.Contains(Level);
}

int32 AHexPlanet::FindFittingBiome(int32 Cell) const
{
	const int32 Level = Levels[Cell];
	TMap<int32, int32> Counts;

	for (const int32 Neighbor : Grid.GetCells()[Cell].Neighbors)
	{
		if (IsBiomeAllowed(CellBiomes[Neighbor], Level))
		{
			++Counts.FindOrAdd(CellBiomes[Neighbor]);
		}
	}

	int32 Best = INDEX_NONE;
	for (const TPair<int32, int32>& Count : Counts)
	{
		if (Best == INDEX_NONE || Count.Value > Counts[Best] || (Count.Value == Counts[Best] && Count.Key < Best))
		{
			Best = Count.Key;
		}
	}

	if (Best != INDEX_NONE)
	{
		return Best;
	}

	for (int32 Biome = 0; Biome < BiomeConfigs.Num(); ++Biome)
	{
		if (BiomeConfigs[Biome]->DefaultForLevels.Contains(Level) && IsBiomeAllowed(Biome, Level))
		{
			return Biome;
		}
	}

	for (int32 Biome = 0; Biome < BiomeConfigs.Num(); ++Biome)
	{
		if (IsBiomeAllowed(Biome, Level))
		{
			return Biome;
		}
	}
	return 0;
}

TArray<uint8> AHexPlanet::GetFrozenCells() const
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<uint8> Frozen;
	Frozen.Init(0, Cells.Num());

	const auto IsFrozenSea = [this](int32 Cell)
	{
		return Levels[Cell] < 0 && BiomeConfigs.IsValidIndex(CellBiomes[Cell]) && BiomeConfigs[CellBiomes[Cell]]->bFrozen;
	};

	// land next to ice counts as ice, so the sea field, which blends between the centers of cells, keeps the ice up to the coast
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		bool bFrozen = IsFrozenSea(Cell);

		for (int32 Index = 0; Index < Cells[Cell].Neighbors.Num() && !bFrozen && Levels[Cell] >= 0; ++Index)
		{
			bFrozen = IsFrozenSea(Cells[Cell].Neighbors[Index]);
		}
		Frozen[Cell] = bFrozen ? 1 : 0;
	}
	return Frozen;
}

void AHexPlanet::StartRebuild()
{
	if (RebuildTask.IsValid() || !bLevelsPending || !SurfaceKit)
	{
		return;
	}
	bLevelsPending = false;

	// the task reads only its copies, the kit, which never changes, and the grid, which never changes after BeginPlay and outlives it,
	// as EndPlay waits for it
	Rebuild = MakeShared<FLevelRebuild>();
	Rebuild->OldLevels = ShownLevels;
	Rebuild->OldMassifSteps = ShownMassifSteps;
	Rebuild->Levels = Levels;
	Rebuild->OldOverrides = ShownReliefOverrides;
	Rebuild->Overrides = ReliefOverrides;
	Rebuild->OldFrozen = ShownFrozen;
	Rebuild->Frozen = GetFrozenCells();
	Rebuild->Coasts = ChunkCoasts;
	Rebuild->StartTime = FPlatformTime::Seconds();

	RebuildTask = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Work = Rebuild, GridPtr = &Grid, Kit = SurfaceKit.ToSharedRef(), Params = SurfaceParams, SeaParams = FSeaParams::Make(*Settings, Radius), FixedPeak = MeanPeak]()
		{
			const double SurfaceStart = FPlatformTime::Seconds();
			Work->MassifSteps = HexPlanetSurface::FindMassifSteps(*GridPtr, *Kit, Work->Levels);
			Work->Chunks = HexPlanetSurface::FindAffectedChunks(*GridPtr, Work->OldLevels, Work->Levels, Work->OldMassifSteps, Work->MassifSteps,
				HexPlanetSurface::FindChangedOverrides(Work->OldOverrides, Work->Overrides));

			if (!Work->Chunks.IsEmpty())
			{
				Work->Surface = HexPlanetSurface::Build(*GridPtr, *Kit, Params, Work->Levels, Work->MassifSteps, Work->Chunks, FixedPeak, &Work->Overrides);
				for (int32 Slot = 0; Slot < Work->Surface.Chunks.Num(); ++Slot)
				{
					Work->Coasts[Work->Surface.Chunks[Slot]] = Work->Surface.Coasts[Slot];
				}
			}
			const double SeaStart = FPlatformTime::Seconds();
			Work->SurfaceMs = (SeaStart - SurfaceStart) * 1000.0;

			// the sea and its grid lines change only when a cell crossed the coast or moved between the shallows and the deep sea, and the sea
			// field also when the sea froze or thawed, which a change of biomes alone does
			bool bSeaChanged = false;
			for (int32 Cell = 0; Cell < Work->Levels.Num() && !bSeaChanged; ++Cell)
			{
				bSeaChanged = Work->Levels[Cell] != Work->OldLevels[Cell] && (Work->Levels[Cell] < 0 || Work->OldLevels[Cell] < 0);
			}
			const bool bIceChanged = Work->Frozen != Work->OldFrozen;

			if (bSeaChanged || bIceChanged)
			{
				Work->SeaPixels = BuildSeaPixels(*GridPtr, Work->Levels, Work->Frozen, Work->Coasts, SeaParams);
			}
			const double GridStart = FPlatformTime::Seconds();
			Work->SeaFieldMs = (GridStart - SeaStart) * 1000.0;

			if (bSeaChanged)
			{
				Work->WaterGrid = BuildWaterGridMesh(*GridPtr, Work->Levels, SeaParams);
				Work->bWaterGridBuilt = true;
				Work->WaterGridMs = (FPlatformTime::Seconds() - GridStart) * 1000.0;
			}
		});
}

void AHexPlanet::FinishRebuild()
{
	const double ApplyStart = FPlatformTime::Seconds();
	const double BackgroundMs = (ApplyStart - Rebuild->StartTime) * 1000.0;
	int32 NumChanged = 0;

	for (int32 Cell = 0; Cell < Rebuild->Levels.Num(); ++Cell)
	{
		NumChanged += Rebuild->Levels[Cell] != Rebuild->OldLevels[Cell] ? 1 : 0;
	}

	if (!Rebuild->Chunks.IsEmpty())
	{
		ApplySurface(Rebuild->Surface);
	}
	if (!Rebuild->SeaPixels.IsEmpty())
	{
		ApplySeaField(Rebuild->SeaPixels);
	}
	if (Rebuild->bWaterGridBuilt)
	{
		WaterGrid->CreateMeshSection_LinearColor(0, Rebuild->WaterGrid.Positions, Rebuild->WaterGrid.Triangles, Rebuild->WaterGrid.Normals, Rebuild->WaterGrid.UVs, {},
			Rebuild->WaterGrid.CellUVs, Rebuild->WaterGrid.LocalUVs, {}, {}, false);
	}

	// the floes move with the ice, and with the coast, as a cell of frozen sea that became land keeps its frozen mark for the sea field
	const bool bFloesChanged = !Rebuild->SeaPixels.IsEmpty();
	ShownLevels = MoveTemp(Rebuild->Levels);
	ShownMassifSteps = MoveTemp(Rebuild->MassifSteps);
	ShownFrozen = MoveTemp(Rebuild->Frozen);
	ShownReliefOverrides = MoveTemp(Rebuild->Overrides);
	if (bFloesChanged)
	{
		UpdateFloes();
	}

	FString ChunkList;
	for (const int32 Chunk : Rebuild->Chunks)
	{
		ChunkList += FString::Printf(TEXT("%s%d"), ChunkList.IsEmpty() ? TEXT("") : TEXT(" "), Chunk);
	}

	UE_LOG(LogHexHero, Log, TEXT("Planet: rebuilt %d changed cells, %d chunks (%s) in %.1f ms in the background (surface %.1f ms, sea field %.1f ms, water grid %.1f ms), shown in %.1f ms"),
		NumChanged, Rebuild->Chunks.Num(), *ChunkList, BackgroundMs, Rebuild->SurfaceMs, Rebuild->SeaFieldMs, Rebuild->WaterGridMs, (FPlatformTime::Seconds() - ApplyStart) * 1000.0);
	Rebuild.Reset();
}

void AHexPlanet::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// a rebuild of changed levels that has ended is shown, and the changes made while it ran start the next
	if (RebuildTask.IsValid() && RebuildTask.IsCompleted())
	{
		RebuildTask = UE::Tasks::FTask();
		FinishRebuild();
		StartRebuild();
		OnLevelsRebuilt.Broadcast();
	}

	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();

	// the dense middle of the cap lies under the camera, where the water is seen closest
	if (const AActor* ViewTarget = PlayerController ? PlayerController->GetViewTarget() : nullptr)
	{
		const FVector Below = ViewTarget->GetActorLocation().GetSafeNormal();

		if (!Below.IsNearlyZero())
		{
			Water->SetWorldRotation(FQuat::FindBetweenNormals(FVector::UpVector, Below));
		}
	}

	// the clouds below the camera hand over to the sky over CloudSkySwitchFade around CloudSkySwitch, as M_HexPlanetClouds fades them,
	// and a shell that is faded out is not drawn
	if (const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr)
	{
		const double CameraHeight = CameraManager->GetCameraLocation().Size() - Radius;
		const double Switch = LoadedCloudConfig->CloudSkySwitch;
		const double Fade = FMath::Max(LoadedCloudConfig->CloudSkySwitchFade, 1.0f);
		Clouds->SetVisibility(CameraHeight > Switch - Fade);
		SkyClouds->SetVisibility(CameraHeight < Switch + Fade);

		UpdateChunkLod(CameraManager->GetCameraLocation());
	}

	if (Sun)
	{
		// the water and the clouds are lit by it, and the shadows of the clouds fall along it
		const FLinearColor SunDirection(-Sun->GetActorForwardVector());

		for (UMaterialInstanceDynamic* Instance : { WaterMaterial.Get(), WaterGridMaterial.Get(), CloudMaterial.Get(), SkyCloudMaterial.Get(), SurfaceMaterial.Get(), SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get() })
		{
			if (Instance)
			{
				Instance->SetVectorParameterValue(TEXT("SunDirection"), SunDirection);
			}
		}

		// and any material, such as those of the auroras
		if (UMaterialParameterCollectionInstance* Parameters = LoadedParameterCollection ? GetWorld()->GetParameterCollectionInstance(LoadedParameterCollection) : nullptr)
		{
			Parameters->SetVectorParameterValue(TEXT("SunDirection"), SunDirection);
			Parameters->SetScalarParameterValue(TEXT("PlanetRadius"), Radius);
		}
	}

	UpdateDebugView();
}

void AHexPlanet::SetGridVisible(bool bVisible)
{
	bGridVisible = bVisible;

	for (UMaterialInstanceDynamic* Instance : { SurfaceMaterial.Get(), SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get(), WaterGridMaterial.Get() })
	{
		if (Instance)
		{
			Instance->SetScalarParameterValue(TEXT("GridOpacity"), bGridVisible ? Settings->GridOpacity : 0.0f);
		}
	}
}

bool AHexPlanet::IsWireTarget(int32 Section, EHexWireframeTarget Target)
{
	return Target == EHexWireframeTarget::All || IsMountainSection(Section) == (Target == EHexWireframeTarget::AllButTerrain);
}

UMaterialInterface* AHexPlanet::GetChunkMaterial(int32 Chunk, int32 Part, bool bCut, EHexWireframeTarget Target) const
{
	if (ChunkLodStates.IsValidIndex(Chunk) && ChunkLodStates[Chunk] == 1 && LodNearMaterial && LodFarMaterial)
	{
		return Part == 2 ? LodFarMaterial.Get() : LodNearMaterial.Get();
	}

	// a coarse mesh counts as terrain
	const bool bChosen = IsWireTarget(Part == 2 ? 0 : Part, Target);
	return bCut && bChosen && SurfaceCutMaterial ? SurfaceCutMaterial.Get() : SurfaceMaterial.Get();
}

void AHexPlanet::UpdateChunkLod(const FVector& CameraLocation)
{
	if (CoarseChunks.IsEmpty() || CoarseChunks.Num() != SurfaceChunks.Num())
	{
		return;
	}

	ChunkLodStates.SetNum(SurfaceChunks.Num());

	// the console can move the band to try other distances while the game runs
	const float OverrideDistance = CVarHexPlanetCoarseDistance.GetValueOnGameThread();
	const float OverrideBand = CVarHexPlanetCoarseBand.GetValueOnGameThread();
	const float CoarseDistance = OverrideDistance > 0.0f ? OverrideDistance : Settings->CoarseDistance;
	const float CoarseBand = OverrideBand > 0.0f ? OverrideBand : Settings->CoarseBand;
	const double Near = CoarseDistance - CoarseBand;
	const double Far = CoarseDistance + CoarseBand;

	for (UMaterialInstanceDynamic* Instance : { LodNearMaterial.Get(), LodFarMaterial.Get() })
	{
		if (Instance)
		{
			Instance->SetVectorParameterValue(TEXT("LodCamera"), FLinearColor(CameraLocation));
			Instance->SetScalarParameterValue(TEXT("CoarseDistance"), CoarseDistance);
			Instance->SetScalarParameterValue(TEXT("CoarseBand"), CoarseBand);
		}
	}

	for (int32 Chunk = 0; Chunk < SurfaceChunks.Num(); ++Chunk)
	{
		// the pieces where all of the chunk is nearer than the band, the coarse mesh where all of it is further, and both where it reaches into it,
		// with the LOD materials, which keep the pixels of each by their own distance across the band
		const FBoxSphereBounds& Bounds = SurfaceChunks[Chunk]->Bounds;
		const double Reach = FMath::Max(Bounds.SphereRadius, CoarseChunks[Chunk]->Bounds.SphereRadius);
		const double Distance = FVector::Distance(CameraLocation, Bounds.Origin);
		const uint8 State = Distance + Reach < Near ? 0 : Distance - Reach > Far ? 2 : 1;

		if (State == ChunkLodStates[Chunk] && SurfaceChunks[Chunk]->IsVisible() == (State != 2))
		{
			continue;
		}
		ChunkLodStates[Chunk] = State;

		SurfaceChunks[Chunk]->SetVisibility(State != 2);
		CoarseChunks[Chunk]->SetVisibility(State != 0);
		if (WireChunks.IsValidIndex(Chunk) && WireCoarseChunks.IsValidIndex(Chunk))
		{
			WireChunks[Chunk]->SetVisibility(State != 2);
			WireCoarseChunks[Chunk]->SetVisibility(State != 0);
		}

		for (int32 Part = 0; Part < 2; ++Part)
		{
			SurfaceChunks[Chunk]->SetMaterial(Part, GetChunkMaterial(Chunk, Part, bAppliedCut, AppliedView.Target));
		}
		CoarseChunks[Chunk]->SetMaterial(0, GetChunkMaterial(Chunk, 2, bAppliedCut, AppliedView.Target));
	}
}

void AHexPlanet::UpdateDebugView()
{
	FHexDebugViewSettings View = AHexWorldMap::GetDebugViewSettings();

	if (!WireframeMaterial || CellMeshes.IsEmpty())
	{
		View.Mode = EHexWireframeMode::Off;
	}

	Water->SetVisibility(View.Water != EHexWaterView::Hidden);
	WaterGrid->SetVisibility(View.Water != EHexWaterView::Hidden);
	UpdateWaterHide(View);

	// only the triangles hide the geometry they are drawn on, and what is not chosen keeps its normal render. Around the cursor the surface
	// stays and its masked copy hides those cells, as the rest keeps its normal render
	const bool bCut = View.Mode == EHexWireframeMode::Only && View.Scope != EHexWireframeScope::All;

	if (bCut && !SurfaceCutMaterial && SurfaceMaterial)
	{
		SurfaceCutMaterial = AHexWorldMap::CreateCutMaterial(SurfaceMaterial, this);
		HexMaterialConfig::Apply(SurfaceCutMaterial, Settings);
		HexMaterialConfig::Apply(SurfaceCutMaterial, LoadedWaterConfig);
		SurfaceCutMaterial->SetTextureParameterValue(TEXT("CellMarks"), CellMarks);
		SetBiomeLayers(SurfaceCutMaterial);
		SetShoreField(SurfaceCutMaterial);
		SetCloudParameters(SurfaceCutMaterial);
		SetGridVisible(bGridVisible);
	}

	// a rebuilt section is visible again and its copy in the wireframe is old
	const bool bSurfaceChangedNow = bSurfaceChanged;
	bSurfaceChanged = false;
	if (bSurfaceChangedNow)
	{
		AppliedWireTarget.Reset();
	}

	if (View.Mode != AppliedView.Mode || View.Target != AppliedView.Target || bCut != bAppliedCut || bSurfaceChangedNow)
	{
		bAppliedCut = bCut;

		// a coarse mesh counts as terrain, with its mountains, as it is what the terrain is from far away
		const bool bCoarseChosen = IsWireTarget(0, View.Target);
		for (int32 Chunk = 0; Chunk < CoarseChunks.Num(); ++Chunk)
		{
			CoarseChunks[Chunk]->SetMeshSectionVisible(0, !(View.Mode == EHexWireframeMode::Only && !bCut && bCoarseChosen));
			CoarseChunks[Chunk]->SetMaterial(0, GetChunkMaterial(Chunk, 2, bCut, View.Target));
		}

		for (int32 Section = 0; Section < SurfaceChunks.Num() * 2; ++Section)
		{
			const bool bChosen = IsWireTarget(Section, View.Target);
			UProceduralMeshComponent* ChunkMesh = SurfaceChunks[Section / 2];
			ChunkMesh->SetMeshSectionVisible(Section % 2, !(View.Mode == EHexWireframeMode::Only && !bCut && bChosen));
			ChunkMesh->SetMaterial(Section % 2, GetChunkMaterial(Section / 2, Section % 2, bCut, View.Target));
		}
	}

	const int32 CutCell = bCut && Grid.GetCells().IsValidIndex(HoveredCell) ? HoveredCell : INDEX_NONE;
	const int32 CutCount = CutCell == INDEX_NONE ? 0 : View.Scope == EHexWireframeScope::Hover ? 1 : 1 + Grid.GetCells()[CutCell].Neighbors.Num();

	if (SurfaceCutMaterial && (CutCell != AppliedCutCell || CutCount != AppliedCutCount))
	{
		AppliedCutCell = CutCell;
		AppliedCutCount = CutCount;

		// the chunks in the band of the LOD keep their masked copies, which hide the same cells
		for (UMaterialInstanceDynamic* Instance : { SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get() })
		{
			if (Instance)
			{
				SetHideCells(Instance, CutCell, CutCount);
			}
		}
	}

	// the triangles of the whole surface, drawn only when the scope is every cell
	TOptional<EHexWireframeTarget> WireTarget;

	if (View.Mode != EHexWireframeMode::Off && View.Scope == EHexWireframeScope::All)
	{
		WireTarget = View.Target;
	}

	if (WireTarget != AppliedWireTarget)
	{
		AppliedWireTarget = WireTarget;

		// a copy per chunk, shown with its source, so the triangles show the coarse mesh where it is drawn, and both in the band
		const auto MakeWire = [this](const TCHAR* Name, int32 Chunk, UPrimitiveComponent* Source)
		{
			UProceduralMeshComponent* Wire = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("%s%d"), Name, Chunk));
			Wire->SetupAttachment(Wireframe);
			Wire->SetCastShadow(false);
			Wire->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Wire->SetTranslucentSortPriority(Wireframe->TranslucencySortPriority);
			Wire->SetVisibility(Source->IsVisible());
			Wire->RegisterComponent();
			return Wire;
		};

		if (WireTarget.IsSet() && WireChunks.IsEmpty())
		{
			for (int32 Chunk = 0; Chunk < SurfaceChunks.Num(); ++Chunk)
			{
				WireChunks.Add(MakeWire(TEXT("WireChunk"), Chunk, SurfaceChunks[Chunk]));
			}
			for (int32 Chunk = 0; Chunk < CoarseChunks.Num(); ++Chunk)
			{
				WireCoarseChunks.Add(MakeWire(TEXT("WireCoarseChunk"), Chunk, CoarseChunks[Chunk]));
			}
		}

		for (int32 Section = 0; Section < WireChunks.Num() * 2; ++Section)
		{
			UProceduralMeshComponent* Wire = WireChunks[Section / 2];
			const FProcMeshSection* Source = GetSurfaceSection(Section);
			Wire->ClearMeshSection(Section % 2);

			if (WireTarget.IsSet() && IsWireTarget(Section, WireTarget.GetValue()) && Source && !Source->ProcVertexBuffer.IsEmpty())
			{
				// the copy keeps the visibility of the source, which only the triangles hide
				Wire->SetProcMeshSection(Section % 2, *Source);
				Wire->SetMeshSectionVisible(Section % 2, true);
				Wire->SetMaterial(Section % 2, WireframeMaterial);
			}
		}

		for (int32 Chunk = 0; Chunk < WireCoarseChunks.Num(); ++Chunk)
		{
			UProceduralMeshComponent* Wire = WireCoarseChunks[Chunk];
			const FProcMeshSection* Source = CoarseChunks[Chunk]->GetProcMeshSection(0);
			Wire->ClearAllMeshSections();

			if (WireTarget.IsSet() && IsWireTarget(0, WireTarget.GetValue()) && Source && !Source->ProcVertexBuffer.IsEmpty())
			{
				Wire->SetProcMeshSection(0, *Source);
				Wire->SetMeshSectionVisible(0, true);
				Wire->SetMaterial(0, WireframeMaterial);
			}
		}
	}

	if (View.Mode != AppliedView.Mode || View.Scope != AppliedView.Scope || View.Target != AppliedView.Target || HoveredCell != AppliedWireCell || bSurfaceChangedNow)
	{
		AppliedWireCell = HoveredCell;
		BuildHoverWireframe(View);
	}

	AppliedView = View;
}

void AHexPlanet::BuildHoverWireframe(const FHexDebugViewSettings& View)
{
	HoverWireframe->ClearAllMeshSections();

	if (View.Mode == EHexWireframeMode::Off || !Grid.GetCells().IsValidIndex(HoveredCell))
	{
		return;
	}

	// the cell under the mouse in every scope, and its neighbors unless the scope is that cell alone
	TArray<TPair<int32, FLinearColor>, TInlineAllocator<7>> Chosen;
	Chosen.Emplace(HoveredCell, Settings->WireHoverColor);

	if (View.Scope != EHexWireframeScope::Hover)
	{
		for (const int32 Neighbor : Grid.GetCells()[HoveredCell].Neighbors)
		{
			Chosen.Emplace(Neighbor, Settings->WireNeighborColor);
		}
	}

	TArray<FVector> Positions;
	TArray<int32> Triangles;
	TArray<FLinearColor> Colors;

	for (const TPair<int32, FLinearColor>& Each : Chosen)
	{
		const FCellMeshRange& Range = CellMeshes[Each.Key];
		const FProcMeshSection* Source = Range.Section != INDEX_NONE ? GetSurfaceSection(Range.Section) : nullptr;

		if (!Source || !IsWireTarget(Range.Section, View.Target))
		{
			continue;
		}

		const int32 First = Positions.Num();

		for (int32 Vertex = 0; Vertex < Range.NumVertices; ++Vertex)
		{
			Positions.Add(FVector(Source->ProcVertexBuffer[Range.FirstVertex + Vertex].Position));
			Colors.Add(Each.Value);
		}

		for (int32 Index = 0; Index < Range.NumIndices; ++Index)
		{
			Triangles.Add(First + int32(Source->ProcIndexBuffer[Range.FirstIndex + Index]) - Range.FirstVertex);
		}
	}

	if (!Triangles.IsEmpty())
	{
		HoverWireframe->CreateMeshSection_LinearColor(0, Positions, Triangles, {}, {}, Colors, {}, false);
		HoverWireframe->SetMaterial(0, HoverWireframeMaterial);
	}
}

void AHexPlanet::UpdateWaterHide(const FHexDebugViewSettings& View)
{
	const int32 Cell = Grid.GetCells().IsValidIndex(HoveredCell) ? HoveredCell : INDEX_NONE;
	int32 Count = 0;

	if (Cell != INDEX_NONE && View.Water == EHexWaterView::HiddenUnderCursor)
	{
		Count = 1;
	}
	else if (Cell != INDEX_NONE && View.Water == EHexWaterView::HiddenAroundCursor)
	{
		Count = 1 + Grid.GetCells()[Cell].Neighbors.Num();
	}

	if (!WaterMaterial || (Count == AppliedWaterHideCount && (Count == 0 || Cell == AppliedWaterHideCell)))
	{
		return;
	}

	AppliedWaterHideCell = Cell;
	AppliedWaterHideCount = Count;
	SetHideCells(WaterMaterial, Cell, Count);

	if (WaterGridMaterial)
	{
		SetHideCells(WaterGridMaterial, Cell, Count);
	}
}

void AHexPlanet::SetHideCells(UMaterialInstanceDynamic* Target, int32 Cell, int32 Count) const
{
	// the material finds the nearest of these centers, so the cells past the hidden ones are there too, and a pixel nearest to them stays
	TArray<int32, TInlineAllocator<MaxWaterHideCells>> Centers;

	if (Cell != INDEX_NONE)
	{
		Centers.Add(Cell);
		Centers.Append(Grid.GetCells()[Cell].Neighbors);

		for (int32 Index = 1; Index < Centers.Num() && Index <= Grid.GetCells()[Cell].Neighbors.Num(); ++Index)
		{
			for (const int32 Outer : Grid.GetCells()[Centers[Index]].Neighbors)
			{
				if (Centers.Num() < MaxWaterHideCells && !Centers.Contains(Outer))
				{
					Centers.Add(Outer);
				}
			}
		}
	}

	for (int32 Index = 0; Index < MaxWaterHideCells; ++Index)
	{
		// a zero vector is never the nearest
		const FVector Center = Centers.IsValidIndex(Index) ? FVector(Grid.GetCells()[Centers[Index]].Center) : FVector::ZeroVector;
		Target->SetVectorParameterValue(*FString::Printf(TEXT("HideCell%d"), Index), FLinearColor(Center));
	}

	Target->SetScalarParameterValue(TEXT("HideCount"), float(Count));
}

const FProcMeshSection* AHexPlanet::GetSurfaceSection(int32 Section) const
{
	return SurfaceChunks.IsValidIndex(Section / 2) ? SurfaceChunks[Section / 2]->GetProcMeshSection(Section % 2) : nullptr;
}

void AHexPlanet::BuildSeaField()
{
	ShownFrozen = GetFrozenCells();
	ApplySeaField(BuildSeaPixels(Grid, Levels, ShownFrozen, ChunkCoasts, FSeaParams::Make(*Settings, Radius)));
	UpdateFloes();
}

void AHexPlanet::ApplySeaField(const TArray<FColor>& Pixels)
{
	// a new texture each time, given to the water at once, as the old one is collected once nothing holds it. Its name must be new too,
	// as a texture made under the name of one still there replaces that object in place, and the water then samples an empty texture
	const int32 FaceTexels = 6 * Settings->SeaFieldSize * Settings->SeaFieldSize;
	const auto MakeCube = [&](const TCHAR* BaseName, int32 First)
	{
		const FName Name = MakeUniqueObjectName(GetTransientPackage(), UTextureCube::StaticClass(), BaseName);
		UTextureCube* Cube = UTextureCube::CreateTransient(Settings->SeaFieldSize, Settings->SeaFieldSize, PF_B8G8R8A8, Name);
		Cube->SRGB = false;
		Cube->Filter = TF_Bilinear;
		Cube->NeverStream = true;

		FTexture2DMipMap& Mip = Cube->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Pixels.GetData() + First, FaceTexels * Pixels.GetTypeSize());
		Mip.BulkData.Unlock();
		Cube->UpdateResource();
		return Cube;
	};
	check(Pixels.Num() == 2 * FaceTexels);
	SeaField = MakeCube(TEXT("PlanetSeaField"), 0);
	ShoreField = MakeCube(TEXT("PlanetShoreField"), FaceTexels);

	if (WaterMaterial)
	{
		WaterMaterial->SetTextureParameterValue(TEXT("SeaField"), SeaField);
	}
	SetShoreField(WaterMaterial);
	SetShoreField(SurfaceMaterial);
	SetShoreField(LodNearMaterial);
	SetShoreField(LodFarMaterial);
	SetShoreField(SurfaceCutMaterial);
}

void AHexPlanet::SetShoreField(UMaterialInstanceDynamic* Target) const
{
	// the surface needs the sea field too, for where the edge of the water lies below the beach
	if (Target && ShoreField && SeaField)
	{
		Target->SetTextureParameterValue(TEXT("ShoreField"), ShoreField);
		Target->SetTextureParameterValue(TEXT("SeaField"), SeaField);
	}
}

void AHexPlanet::SetBiomeLayers(UMaterialInstanceDynamic* Target) const
{
	if (!Target)
	{
		return;
	}
	Target->SetTextureParameterValue(TEXT("CellBiomes"), CellBiomeTexture);
	if (LayerParamsTexture)
	{
		Target->SetTextureParameterValue(TEXT("LayerParams"), LayerParamsTexture);
	}

	// the beach is a property of the layer of the terrain, so it blends between cells as their layers do: the beach of the first biome with
	// that layer, else the layer itself, as for the rock of mountains, so a layer without a beach goes down to the water as it is
	float Beaches[12];
	for (int32 Layer = 0; Layer < UE_ARRAY_COUNT(Beaches); ++Layer)
	{
		Beaches[Layer] = float(Layer);
	}
	TBitArray<> bSet(false, UE_ARRAY_COUNT(Beaches));
	for (const TObjectPtr<UHexBiomeConfig>& Biome : BiomeConfigs)
	{
		if (Biome && Biome->TerrainLayer >= 0 && Biome->TerrainLayer < UE_ARRAY_COUNT(Beaches) && !bSet[Biome->TerrainLayer] && Biome->PlanetBeachLayer >= 0)
		{
			Beaches[Biome->TerrainLayer] = float(Biome->PlanetBeachLayer);
			bSet[Biome->TerrainLayer] = true;
		}
	}
	// the rock on the slopes of a layer, so it blends between cells too: each rock of a mountain is its own, as a mountain draws its rock, and
	// another layer takes the rock of the first biome of the land with it
	float Rocks[UE_ARRAY_COUNT(Beaches)];
	TBitArray<> bRockSet(false, UE_ARRAY_COUNT(Rocks));
	for (int32 Layer = 0; Layer < UE_ARRAY_COUNT(Rocks); ++Layer)
	{
		Rocks[Layer] = float(Layer);
	}
	for (const TObjectPtr<UHexBiomeConfig>& Biome : BiomeConfigs)
	{
		if (Biome && Biome->MountainRockLayer >= 0 && Biome->MountainRockLayer < UE_ARRAY_COUNT(Rocks))
		{
			bRockSet[Biome->MountainRockLayer] = true;
		}
	}
	for (const TObjectPtr<UHexBiomeConfig>& Biome : BiomeConfigs)
	{
		const bool bLand = Biome && Biome->AllowedLevels.ContainsByPredicate([](int32 Level) { return Level >= 0; });
		if (bLand && Biome->TerrainLayer >= 0 && Biome->TerrainLayer < UE_ARRAY_COUNT(Rocks) && !bRockSet[Biome->TerrainLayer])
		{
			Rocks[Biome->TerrainLayer] = float(FMath::Clamp(Biome->MountainRockLayer, 0, 15));
			bRockSet[Biome->TerrainLayer] = true;
		}
	}

	for (int32 Group = 0; Group < 3; ++Group)
	{
		Target->SetVectorParameterValue(*FString::Printf(TEXT("BeachLayers%d"), Group),
			FLinearColor(Beaches[Group * 4], Beaches[Group * 4 + 1], Beaches[Group * 4 + 2], Beaches[Group * 4 + 3]));
		Target->SetVectorParameterValue(*FString::Printf(TEXT("RockLayers%d"), Group),
			FLinearColor(Rocks[Group * 4], Rocks[Group * 4 + 1], Rocks[Group * 4 + 2], Rocks[Group * 4 + 3]));
	}

	// the seabed of a layer under the water at the coast, from the first biome with that layer that has one, else its beach, so snow turns into
	// coastal gravel under the sea
	float Seabeds[UE_ARRAY_COUNT(Beaches)];
	TBitArray<> bSeabedSet(false, UE_ARRAY_COUNT(Seabeds));
	for (int32 Layer = 0; Layer < UE_ARRAY_COUNT(Seabeds); ++Layer)
	{
		Seabeds[Layer] = Beaches[Layer];
	}
	for (const TObjectPtr<UHexBiomeConfig>& Biome : BiomeConfigs)
	{
		if (Biome && Biome->TerrainLayer >= 0 && Biome->TerrainLayer < UE_ARRAY_COUNT(Seabeds) && !bSeabedSet[Biome->TerrainLayer] && Biome->PlanetSeabedLayer >= 0)
		{
			Seabeds[Biome->TerrainLayer] = float(Biome->PlanetSeabedLayer);
			bSeabedSet[Biome->TerrainLayer] = true;
		}
	}
	for (int32 Group = 0; Group < 3; ++Group)
	{
		Target->SetVectorParameterValue(*FString::Printf(TEXT("SeabedLayers%d"), Group),
			FLinearColor(Seabeds[Group * 4], Seabeds[Group * 4 + 1], Seabeds[Group * 4 + 2], Seabeds[Group * 4 + 3]));
	}
}

void AHexPlanet::BuildWater()
{
	// each ring is wider than the one inside it by the same share, so the cap is dense under the camera and reaches a quarter of the way around
	const double Reach = Radius * UE_DOUBLE_HALF_PI;
	double Growth = 1.001;

	for (double Low = 1.0001, High = 2.0, Pass = 0; Pass < 60; ++Pass)
	{
		Growth = (Low + High) * 0.5;
		const double Arc = Settings->WaterInnerStep * (FMath::Pow(Growth, Settings->WaterRings) - 1.0) / (Growth - 1.0);
		(Arc > Reach ? High : Low) = Growth;
	}

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Triangles;

	Positions.Add(FVector(0.0, 0.0, Radius));
	Normals.Add(FVector::UpVector);
	UVs.Add(FVector2D(Settings->WaterInnerStep, 0.0));

	for (int32 Ring = 1; Ring <= Settings->WaterRings; ++Ring)
	{
		const double Angle = Settings->WaterInnerStep * (FMath::Pow(Growth, Ring) - 1.0) / (Growth - 1.0) / Radius;
		// U is the spacing of the vertices there, the wider of the step to the next ring and the step around, by which M_HexPlanetWater
		// fades the waves too short for them to lift
		const double Spacing = FMath::Max(Settings->WaterInnerStep * FMath::Pow(Growth, Ring), UE_DOUBLE_TWO_PI * Radius * FMath::Sin(Angle) / Settings->WaterSegments);

		for (int32 Segment = 0; Segment < Settings->WaterSegments; ++Segment)
		{
			const double Around = UE_DOUBLE_TWO_PI * Segment / Settings->WaterSegments;
			const FVector Direction(FMath::Sin(Angle) * FMath::Cos(Around), FMath::Sin(Angle) * FMath::Sin(Around), FMath::Cos(Angle));
			Positions.Add(Direction * Radius);
			Normals.Add(Direction);
			UVs.Add(FVector2D(Spacing, 0.0));
		}
	}

	// triangles show on the side where (C - A) x (B - A) points, which is away from the center of the planet
	const auto AddTriangle = [&](int32 A, int32 B, int32 C)
	{
		const FVector Facing = FVector::CrossProduct(Positions[C] - Positions[A], Positions[B] - Positions[A]);

		if (FVector::DotProduct(Facing, Positions[A]) >= 0.0)
		{
			Triangles.Append({A, B, C});
		}
		else
		{
			Triangles.Append({A, C, B});
		}
	};

	for (int32 Segment = 0; Segment < Settings->WaterSegments; ++Segment)
	{
		AddTriangle(0, 1 + Segment, 1 + (Segment + 1) % Settings->WaterSegments);
	}

	for (int32 Ring = 1; Ring < Settings->WaterRings; ++Ring)
	{
		const int32 Inner = 1 + (Ring - 1) * Settings->WaterSegments;
		const int32 Outer = 1 + Ring * Settings->WaterSegments;

		for (int32 Segment = 0; Segment < Settings->WaterSegments; ++Segment)
		{
			const int32 Next = (Segment + 1) % Settings->WaterSegments;
			AddTriangle(Inner + Segment, Outer + Segment, Outer + Next);
			AddTriangle(Inner + Segment, Outer + Next, Inner + Next);
		}
	}

	Water->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, UVs, {}, {}, false);

	if (UMaterialInterface* BaseMaterial = WaterMaterialAsset.LoadSynchronous())
	{
		WaterMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		WaterMaterial->SetTextureParameterValue(TEXT("SeaField"), SeaField);
		WaterMaterial->SetScalarParameterValue(TEXT("PlanetRadius"), Radius);
		SetShoreField(WaterMaterial);
		HexMaterialConfig::Apply(WaterMaterial, LoadedWaterConfig);
		SetCloudParameters(WaterMaterial);
		Water->SetMaterial(0, WaterMaterial);
	}
	else
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: water material %s is missing, run Tools/create_planet_assets.py"), *WaterMaterialAsset.ToString());
	}
}

void AHexPlanet::BuildWaterGrid()
{
	// each cell of the sea gets a strip inside its sides at the height of the sea, see BuildWaterGridMesh
	const FSectionMesh Mesh = BuildWaterGridMesh(Grid, Levels, FSeaParams::Make(*Settings, Radius));
	WaterGrid->CreateMeshSection_LinearColor(0, Mesh.Positions, Mesh.Triangles, Mesh.Normals, Mesh.UVs, {}, Mesh.CellUVs, Mesh.LocalUVs, {}, {}, false);

	if (UMaterialInterface* BaseMaterial = WaterGridMaterialAsset.LoadSynchronous())
	{
		WaterGridMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		HexMaterialConfig::Apply(WaterGridMaterial, Settings);
		WaterGridMaterial->SetTextureParameterValue(TEXT("CellMarks"), CellMarks);
		WaterGrid->SetMaterial(0, WaterGridMaterial);
		SetGridVisible(bGridVisible);
	}
	else
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: water grid material %s is missing, run Tools/create_planet_assets.py"), *WaterGridMaterialAsset.ToString());
	}
}

void AHexPlanet::BuildClouds()
{
	// a sphere of rings from pole to pole, drawn twice: below the camera, and lifted by the material to CloudSkyHeight as the sky, whose
	// bounds reach that far. Its material is two sided
	const int32 CloudRings = LoadedCloudConfig->CloudRings;
	const double CloudRadius = Radius + LoadedCloudConfig->CloudHeight;
	const int32 Segments = CloudRings * 2;
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<int32> Triangles;

	for (int32 Ring = 0; Ring <= CloudRings; ++Ring)
	{
		const double Polar = UE_DOUBLE_PI * Ring / CloudRings;

		for (int32 Segment = 0; Segment <= Segments; ++Segment)
		{
			const double Around = UE_DOUBLE_TWO_PI * Segment / Segments;
			const FVector Direction(FMath::Sin(Polar) * FMath::Cos(Around), FMath::Sin(Polar) * FMath::Sin(Around), FMath::Cos(Polar));
			Positions.Add(Direction * CloudRadius);
			Normals.Add(Direction);
		}
	}

	// triangles show on the side where (C - A) x (B - A) points, which must be away from the center of the planet
	for (int32 Ring = 0; Ring < CloudRings; ++Ring)
	{
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 A = Ring * (Segments + 1) + Segment;
			const int32 B = A + Segments + 1;

			if (Ring > 0)
			{
				Triangles.Append({A, A + 1, B});
			}

			if (Ring < CloudRings - 1)
			{
				Triangles.Append({A + 1, B + 1, B});
			}
		}
	}

	Clouds->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, {}, {}, {}, false);
	SkyClouds->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, {}, {}, {}, false);
	Clouds->SetBoundsScale(static_cast<float>((CloudRadius + LoadedCloudConfig->CloudThickness * 0.5) / CloudRadius * 1.05));
	SkyClouds->SetBoundsScale(static_cast<float>((Radius + FMath::Max(LoadedCloudConfig->CloudHeight, LoadedCloudConfig->CloudSkyHeight)) / CloudRadius * 1.05));

	if (UMaterialInterface* BaseMaterial = CloudMaterialAsset.LoadSynchronous())
	{
		CloudMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		SetCloudParameters(CloudMaterial);
		Clouds->SetMaterial(0, CloudMaterial);

		SkyCloudMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		SetCloudParameters(SkyCloudMaterial);
		SkyCloudMaterial->SetScalarParameterValue(TEXT("CloudSkyLayer"), 1.0f);
		SkyClouds->SetMaterial(0, SkyCloudMaterial);
	}
	else
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: cloud material %s is missing, run Tools/create_planet_assets.py"), *CloudMaterialAsset.ToString());
	}
}

void AHexPlanet::BuildAuroras()
{
	for (UNiagaraComponent* Old : Auroras)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	Auroras.Reset();

	UNiagaraSystem* System = AuroraSystem.LoadSynchronous();
	if (!System)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: aurora system %s is missing"), *AuroraSystem.ToString());
		return;
	}

	// at the height of the sea, so the aurora stands as high over any level, with Z away from the center
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	for (int32 Cell = 0; Cell < FMath::Min(12, Cells.Num()); ++Cell)
	{
		const FVector Up = FVector(Cells[Cell].Center.GetSafeNormal());
		UNiagaraComponent* Aurora = NewObject<UNiagaraComponent>(this, *FString::Printf(TEXT("Aurora%d"), Cell));
		Aurora->SetupAttachment(Surface);
		Aurora->SetAsset(System);
		Aurora->SetWorldLocationAndRotation(Up * Radius, FQuat::FindBetweenNormals(FVector::UpVector, Up));
		Aurora->SetCastShadow(false);
		// light added to the scene, which ray tracing has no use for; its ribbons, moved by their material, hung the GPU in ray tracing
		Aurora->SetVisibleInRayTracing(false);
		// the auroras add their light after the clouds, so it shines through them
		Aurora->SetTranslucentSortPriority(3);
		Aurora->RegisterComponent();
		Aurora->Activate(true);
		Auroras.Add(Aurora);
	}
}


void AHexPlanet::SetCloudParameters(UMaterialInstanceDynamic* Target) const
{
	Target->SetScalarParameterValue(TEXT("PlanetRadius"), Radius);
	HexMaterialConfig::Apply(Target, LoadedCloudConfig);

	// the noise of the clouds is of the direction from the center, so on a planet of another size it scales by the frequency, keeping the
	// clouds as large in cells, and the turn of a smaller planet is faster by it, keeping their speed over the ground
	const double SizeRatio = Settings->GetSizeRatio(Manifest.Frequency);
	Target->SetScalarParameterValue(TEXT("CloudScale"), LoadedCloudConfig->CloudScale * SizeRatio);
	Target->SetScalarParameterValue(TEXT("CloudSpeed"), LoadedCloudConfig->CloudSpeed / SizeRatio);
}

void AHexPlanet::BuildLevels()
{
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, *Settings, Levels);
}

bool AHexPlanet::SaveAs(const FString& Name, FString& OutError)
{
	const FString Folder = HexPlanetFile::SanitizeFolderName(Name);

	if (Folder.IsEmpty())
	{
		OutError = TEXT("The name needs a letter or a digit");
		return false;
	}

	FHexPlanetManifest Saved = Manifest;
	Saved.Name = Name.TrimStartAndEnd();

	// a planet saved under its own folder keeps its place in the menu, any other comes after the rest
	if (!Folder.Equals(PlanetFolder, ESearchCase::IgnoreCase))
	{
		Saved.SortOrder = 0;
		for (const FHexPlanetEntry& Planet : HexPlanetFile::FindPlanets())
		{
			if (!Planet.Folder.Equals(Folder, ESearchCase::IgnoreCase))
			{
				Saved.SortOrder = FMath::Max(Saved.SortOrder, Planet.Manifest.SortOrder + 10);
			}
		}
	}

	if (!HexPlanetFile::SaveManifest(Folder, Saved, OutError))
	{
		return false;
	}

	// the levels and biomes are always written, painted or generated, so the planet does not change when the generation does
	if (!HexPlanetFile::SaveLevels(Folder, Levels, OutError) || !HexPlanetFile::SaveBiomes(Folder, CellBiomes, BiomeLetters, OutError)
		|| !SaveRelief(Folder, OutError))
	{
		return false;
	}

	Manifest = Saved;
	PlanetFolder = HexPlanetFile::PlanetExists(Folder) ? Folder : PlanetFolder;
	bLevelsEdited = false;
	return true;
}

void AHexPlanet::WriteGeneratedFiles(bool bLevels, bool bBiomes)
{
	if (!IsSaved())
	{
		return;
	}

	FString Error;
	if (bLevels)
	{
		if (HexPlanetFile::SaveLevels(PlanetFolder, Levels, Error))
		{
			UE_LOG(LogHexHero, Log, TEXT("Planet: wrote the generated levels to %s"), *HexPlanetFile::GetFilePath(PlanetFolder, HexPlanetFile::LevelsFile));
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
	}

	if (bBiomes)
	{
		if (HexPlanetFile::SaveBiomes(PlanetFolder, CellBiomes, BiomeLetters, Error))
		{
			UE_LOG(LogHexHero, Log, TEXT("Planet: wrote the generated biomes to %s"), *HexPlanetFile::GetFilePath(PlanetFolder, HexPlanetFile::BiomesFile));
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
	}
}

void AHexPlanet::LoadRelief()
{
	ReliefOverrides.Reset();
	TArray<FHexReliefCell> Lines;
	FString Error;
	if (!IsSaved() || !HexPlanetFile::LoadRelief(PlanetFolder, Lines, Error))
	{
		if (!Error.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
		return;
	}

	for (const FHexReliefCell& Line : Lines)
	{
		if (!Grid.GetCells().IsValidIndex(Line.Cell))
		{
			continue;
		}
		HexPlanetSurface::FReliefOverride Override;
		Override.Version = int8(FMath::Clamp(Line.Version, -1, 100));
		Override.Turn = int8(FMath::Clamp(Line.Turn, -1, 5));
		Override.Mirror = int8(FMath::Clamp(Line.Mirror, -1, 1));
		Override.Scale = FMath::Clamp(Line.Scale, 0.1f, 5.0f);
		if (!Override.IsEmpty())
		{
			ReliefOverrides.Add(Line.Cell, Override);
		}
	}
}

bool AHexPlanet::SaveRelief(const FString& Folder, FString& OutError) const
{
	TArray<FHexReliefCell> Lines;
	for (const TPair<int32, HexPlanetSurface::FReliefOverride>& Each : ReliefOverrides)
	{
		FHexReliefCell& Line = Lines.AddDefaulted_GetRef();
		Line.Cell = Each.Key;
		Line.Version = Each.Value.Version;
		Line.Turn = Each.Value.Turn;
		Line.Mirror = Each.Value.Mirror;
		Line.Scale = Each.Value.Scale;
	}
	return HexPlanetFile::SaveRelief(Folder, Lines, OutError);
}

void AHexPlanet::SetReliefOverrides(const HexPlanetSurface::FReliefOverrides& Changes)
{
	bool bChanged = false;
	for (const TPair<int32, HexPlanetSurface::FReliefOverride>& Each : Changes)
	{
		if (!Grid.GetCells().IsValidIndex(Each.Key))
		{
			continue;
		}
		if (Each.Value.IsEmpty())
		{
			bChanged |= ReliefOverrides.Remove(Each.Key) > 0;
		}
		else if (const HexPlanetSurface::FReliefOverride* Current = ReliefOverrides.Find(Each.Key); !Current || !(*Current == Each.Value))
		{
			ReliefOverrides.Add(Each.Key, Each.Value);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		MarkLevelsEdited();
		bLevelsPending = true;
		StartRebuild();
	}
}

HexPlanetSurface::FReliefOverride AHexPlanet::GetReliefOverride(int32 Cell) const
{
	const HexPlanetSurface::FReliefOverride* Found = ReliefOverrides.Find(Cell);
	return Found ? *Found : HexPlanetSurface::FReliefOverride();
}

bool AHexPlanet::SaveLevels(FString& OutError)
{
	if (!IsSaved())
	{
		OutError = TEXT("The planet is not saved");
		return false;
	}

	if (!HexPlanetFile::SaveLevels(PlanetFolder, Levels, OutError) || !HexPlanetFile::SaveBiomes(PlanetFolder, CellBiomes, BiomeLetters, OutError)
		|| !SaveRelief(PlanetFolder, OutError))
	{
		return false;
	}

	bLevelsEdited = false;
	return true;
}

void AHexPlanet::LoadBiomeConfigs()
{
	BiomeConfigs.Reset();
	BiomeLetters.Reset();

	for (const UHexBiomeConfig* Biome : GetDefault<UHexMapSettings>()->LoadBiomes())
	{
		if (Biome->PlanetLetter.Len() != 1 || !FChar::IsAlpha(Biome->PlanetLetter[0]))
		{
			continue;
		}

		const TCHAR Letter = FChar::ToUpper(Biome->PlanetLetter[0]);
		if (BiomeLetters.Contains(Letter))
		{
			UE_LOG(LogHexHero, Error, TEXT("Planet: two biomes have the planet letter %c, %s is left out"), Letter, *Biome->GetName());
			continue;
		}
		BiomeConfigs.Add(const_cast<UHexBiomeConfig*>(Biome));
		BiomeLetters.Add(Letter);
	}

	// by letter, so the index of a biome is the same in every run
	TArray<int32> Order;
	for (int32 Index = 0; Index < BiomeLetters.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Order.Sort([this](int32 A, int32 B) { return BiomeLetters[A] < BiomeLetters[B]; });

	TArray<TObjectPtr<UHexBiomeConfig>> SortedConfigs;
	TArray<TCHAR> SortedLetters;
	for (const int32 Index : Order)
	{
		SortedConfigs.Add(BiomeConfigs[Index]);
		SortedLetters.Add(BiomeLetters[Index]);
	}
	BiomeConfigs = MoveTemp(SortedConfigs);
	BiomeLetters = MoveTemp(SortedLetters);

	if (BiomeConfigs.IsEmpty())
	{
		UE_LOG(LogHexHero, Error, TEXT("Planet: no biome has a PlanetLetter, run Tools/create_biome_assets.py"));
	}
}

void AHexPlanet::LoadBiomes()
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	CellBiomes.Init(0, Cells.Num());

	if (BiomeConfigs.IsEmpty())
	{
		return;
	}

	FString Error;
	bool bLoaded = false;

	if (!PlanetFolder.IsEmpty() && HexPlanetFile::HasBiomes(PlanetFolder))
	{
		bLoaded = HexPlanetFile::LoadBiomes(PlanetFolder, Cells.Num(), BiomeLetters, CellBiomes, Error);
		if (!bLoaded)
		{
			UE_LOG(LogHexHero, Error, TEXT("Planet: %s, generating the biomes"), *Error);
		}
	}

	if (!bLoaded)
	{
		// the generation names the biomes by letter, and a letter no biome has is replaced by a fitting one below
		TArray<TCHAR> Letters;
		HexPlanetGenerator::GenerateBiomes(Grid, Manifest, *Settings, Levels, Letters);
		TSet<TCHAR> Missing;

		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			const int32 Index = BiomeLetters.Find(Letters[Cell]);
			CellBiomes[Cell] = uint8(FMath::Max(Index, 0));
			if (Index == INDEX_NONE)
			{
				Missing.Add(Letters[Cell]);
				CellBiomes[Cell] = 255;
			}
		}

		for (const TCHAR Letter : Missing)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: no biome has the planet letter %c that the generation places, run Tools/create_biome_assets.py"), Letter);
		}
	}

	// a biome the level of its cell does not allow, as after levels.txt was edited by hand, takes a fitting one
	int32 Fitted = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (!IsBiomeAllowed(CellBiomes[Cell], Levels[Cell]))
		{
			CellBiomes[Cell] = uint8(FindFittingBiome(Cell));
			++Fitted;
		}
	}

	if (bLoaded && Fitted > 0)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: %d cells in %s have a biome their level does not allow, which a fitting one replaced"), Fitted,
			*HexPlanetFile::GetFilePath(PlanetFolder, HexPlanetFile::BiomesFile));
	}

	TArray<int32> Counts;
	Counts.Init(0, BiomeConfigs.Num());
	for (const uint8 Biome : CellBiomes)
	{
		++Counts[Biome];
	}

	FString CountText;
	for (int32 Biome = 0; Biome < BiomeConfigs.Num(); ++Biome)
	{
		CountText += FString::Printf(TEXT("%s%s %d"), CountText.IsEmpty() ? TEXT("") : TEXT(", "), *BiomeConfigs[Biome]->DisplayName, Counts[Biome]);
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: biomes %s (%s)"), bLoaded ? TEXT("from biomes.txt") : TEXT("generated"), *CountText);
}

void AHexPlanet::CreateBiomeTexture()
{
	const int32 Rows = FMath::DivideAndRoundUp(Grid.GetCells().Num(), HexPlanetSurface::MarkColumns);
	BiomePixels.Init(FColor(0, 0, 255, 0), HexPlanetSurface::MarkColumns * 2 * Rows);

	for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
	{
		WriteBiomePixels(Cell);
	}

	const FName Name = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("PlanetCellBiomes"));
	CellBiomeTexture = UTexture2D::CreateTransient(HexPlanetSurface::MarkColumns * 2, Rows, PF_B8G8R8A8, Name);
	CellBiomeTexture->SRGB = false;
	CellBiomeTexture->Filter = TF_Nearest;
	CellBiomeTexture->NeverStream = true;

	FTexture2DMipMap& Mip = CellBiomeTexture->GetPlatformData()->Mips[0];
	FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), BiomePixels.GetData(), BiomePixels.Num() * BiomePixels.GetTypeSize());
	Mip.BulkData.Unlock();
	CellBiomeTexture->UpdateResource();
}

void AHexPlanet::WriteBiomePixels(int32 Cell)
{
	const auto GetLayers = [this](int32 Each)
	{
		return BiomeConfigs.IsValidIndex(CellBiomes[Each]) ? BiomeConfigs[CellBiomes[Each]].Get() : nullptr;
	};

	// a mountain is the rock of its biome down to its foot and to the water, so the land and the sea next to it blend into the color of the
	// mountain and not into the grass of its biome
	const auto GetTerrain = [this, &GetLayers](int32 Each)
	{
		const UHexBiomeConfig* Biome = GetLayers(Each);
		if (!Biome)
		{
			return 0;
		}
		return FMath::Clamp(Levels.IsValidIndex(Each) && Levels[Each] >= 2 ? Biome->MountainRockLayer : Biome->TerrainLayer, 0, 15);
	};

	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const UHexBiomeConfig* Biome = GetLayers(Cell);
	const int32 Terrain = GetTerrain(Cell);
	const int32 Rock = Biome ? FMath::Clamp(Biome->MountainRockLayer, 0, 15) : 2;

	// neighbor K is the low half of byte K / 2 if K is even, else its high half; a pentagon repeats its own layer for the sixth
	uint8 Near[3] = {};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const int32 Layer = Each.Neighbors.IsValidIndex(Index) ? GetTerrain(Each.Neighbors[Index]) : Terrain;
		Near[Index / 2] |= uint8(Layer << (Index % 2 == 0 ? 0 : 4));
	}

	const int32 Pixel = (Cell / HexPlanetSurface::MarkColumns) * HexPlanetSurface::MarkColumns * 2 + (Cell % HexPlanetSurface::MarkColumns) * 2;
	// blue: bit K is set if neighbor K is a mountain and bit 6 if this cell is, so the rock of a mountain reaches into its neighbors
	const auto IsMountain = [this](int32 Other) { return Levels.IsValidIndex(Other) && Levels[Other] >= 2; };
	uint8 Mountains = IsMountain(Cell) ? uint8(1 << 6) : uint8(0);
	for (int32 Index = 0; Index < Each.Neighbors.Num() && Index < 6; ++Index)
	{
		Mountains |= IsMountain(Each.Neighbors[Index]) ? uint8(1 << Index) : uint8(0);
	}
	// alpha: the same bits for cells under the sea, so the layer of the land reaches over the part of a cell of the sea that its winding coast
	// lifts above the water, and a snowy coast shows no sand of the seabed there
	const auto IsSea = [this](int32 Other) { return Levels.IsValidIndex(Other) && Levels[Other] < 0; };
	uint8 Seas = IsSea(Cell) ? uint8(1 << 6) : uint8(0);
	for (int32 Index = 0; Index < Each.Neighbors.Num() && Index < 6; ++Index)
	{
		Seas |= IsSea(Each.Neighbors[Index]) ? uint8(1 << Index) : uint8(0);
	}
	BiomePixels[Pixel] = FColor(uint8(Terrain), uint8(Rock), Mountains, Seas);
	BiomePixels[Pixel + 1] = FColor(Near[0], Near[1], Near[2], 0);
}

void AHexPlanet::UpdateBiomePixels(TConstArrayView<int32> ChangedCells)
{
	// a cell holds the layers of its neighbors too
	for (const int32 Cell : ChangedCells)
	{
		WriteBiomePixels(Cell);
		for (const int32 Neighbor : Grid.GetCells()[Cell].Neighbors)
		{
			WriteBiomePixels(Neighbor);
		}
	}

	// boulders take the rock of the biome of their mountain
	UpdateBoulders();

	if (!CellBiomeTexture)
	{
		return;
	}

	// the whole texture goes up, about half a megabyte, which the render thread frees once it has it
	const int32 Columns = HexPlanetSurface::MarkColumns * 2;
	const int32 Rows = BiomePixels.Num() / Columns;
	uint8* Data = static_cast<uint8*>(FMemory::Malloc(BiomePixels.Num() * sizeof(FColor)));
	FMemory::Memcpy(Data, BiomePixels.GetData(), BiomePixels.Num() * sizeof(FColor));
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Columns, Rows);
	CellBiomeTexture->UpdateTextureRegions(0, 1, Region, Columns * sizeof(FColor), sizeof(FColor), Data,
		[](uint8* InData, const FUpdateTextureRegion2D* InRegion)
		{
			FMemory::Free(InData);
			delete InRegion;
		});
}

void AHexPlanet::LoadLevels()
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();

	if (PlanetFolder.IsEmpty() || !HexPlanetFile::HasLevels(PlanetFolder))
	{
		BuildLevels();
		return;
	}

	FString Error;
	if (!HexPlanetFile::LoadLevels(PlanetFolder, Cells.Num(), Levels, Error))
	{
		UE_LOG(LogHexHero, Error, TEXT("Planet: %s, generating the levels from the seed"), *Error);
		BuildLevels();
		return;
	}

	// the pentagons wait for set pieces of their own, and the mountain kit has no five-sided pieces
	int32 ClampedPentagons = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Cells[Cell].IsPentagon() && Levels[Cell] > 1)
		{
			Levels[Cell] = 1;
			++ClampedPentagons;
		}
	}

	if (ClampedPentagons > 0)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: %d pentagons in %s are mountains, which they cannot be yet, so they are hills"), ClampedPentagons,
			*HexPlanetFile::GetFilePath(PlanetFolder, HexPlanetFile::LevelsFile));
	}
}

double AHexPlanet::GetCellTopRadius(int32 Cell) const
{
	const int32 Level = GetCellLevel(Cell);
	return Radius + (Level == -2 ? -Settings->DeepDepth : Level == -1 ? -Settings->ShallowDepth : (Level + 1) * Settings->LevelHeight);
}

void AHexPlanet::BuildSurface()
{
	const double KitStart = FPlatformTime::Seconds();
	const UHexPlanetKitConfig* Config = KitConfig.LoadSynchronous();

	if (!Config)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: kit %s is missing, run Tools/HexPlanetKit/import_planet_kit.py"), *KitConfig.ToString());
	}

	// the kit is read once and kept for the rebuilds of changed levels
	SurfaceKit = HexPlanetSurface::LoadKit(Config, Settings->HexRadius);
	CreateBoulderComponents(Config);
	CreateFloeComponents(Config);
	SurfaceParams = HexPlanetSurface::FParams::Make(*Settings, *SurfaceKit, Radius);
	const double BuildStart = FPlatformTime::Seconds();

	ShownLevels = Levels;
	ShownMassifSteps = HexPlanetSurface::FindMassifSteps(Grid, *SurfaceKit, Levels);

	// the mean peak of this build stays while levels are edited, so the fade of the grid lines does not jump
	ShownReliefOverrides = ReliefOverrides;
	HexPlanetSurface::FBuild Build = HexPlanetSurface::Build(Grid, *SurfaceKit, SurfaceParams, Levels, ShownMassifSteps, HexPlanetSurface::GetAllChunks(Grid), 0.0, &ReliefOverrides);
	MeanPeak = Build.MeanPeak;
	const double ApplyStart = FPlatformTime::Seconds();

	UMaterialInterface* LoadedMaterial = Material.LoadSynchronous();

	if (!LoadedMaterial)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: material %s is missing, run Tools/create_planet_assets.py"), *Material.ToString());
	}
	else
	{
		// the grid lines can be hidden, and come back at the opacity of the material
		SurfaceMaterial = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
		HexMaterialConfig::Apply(SurfaceMaterial, Settings);
		// the swash that runs up the beach takes the values of the shore of the water
		HexMaterialConfig::Apply(SurfaceMaterial, LoadedWaterConfig);
		SurfaceMaterial->SetTextureParameterValue(TEXT("CellMarks"), CellMarks);
		SetBiomeLayers(SurfaceMaterial);
		SetShoreField(SurfaceMaterial);
		SetCloudParameters(SurfaceMaterial);

		// the masked copies that dissolve a chunk between its pieces and its coarse mesh, made from M_HexPlanet_Cut as the material is
		const FString CutPath = FString::Printf(TEXT("%s_Cut.%s_Cut"), *LoadedMaterial->GetOutermost()->GetName(), *LoadedMaterial->GetName());
		UMaterialInterface* CutParent = CoarseChunks.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *CutPath, nullptr, LOAD_NoWarn);
		if (CutParent)
		{
			for (int32 Side = 1; Side <= 2; ++Side)
			{
				UMaterialInstanceDynamic* Lod = UMaterialInstanceDynamic::Create(CutParent, this);
				HexMaterialConfig::Apply(Lod, Settings);
				HexMaterialConfig::Apply(Lod, LoadedWaterConfig);
				Lod->SetTextureParameterValue(TEXT("CellMarks"), CellMarks);
				SetBiomeLayers(Lod);
				SetShoreField(Lod);
				SetCloudParameters(Lod);
				Lod->SetScalarParameterValue(TEXT("LodSide"), float(Side));
				(Side == 1 ? LodNearMaterial : LodFarMaterial) = Lod;
			}
		}
		SetGridVisible(bGridVisible);
	}

	int32 NumTriangles = 0;
	int32 NumVertices = 0;
	for (const HexPlanetSurface::FSection& Section : Build.Sections)
	{
		NumTriangles += Section.Triangles.Num() / 3;
		NumVertices += Section.Positions.Num();
	}
	int32 NumCoarseTriangles = 0;
	for (const HexPlanetSurface::FSection& Section : Build.CoarseSections)
	{
		NumCoarseTriangles += Section.Triangles.Num() / 3;
	}

	CellMeshes.Init(FCellMeshRange(), Grid.GetCells().Num());
	CellPieces.Init(INDEX_NONE, Grid.GetCells().Num());
	CellTurns.Init(0, Grid.GetCells().Num());
	CellMirrored.Init(false, Grid.GetCells().Num());
	CellRelief.Init(1.0f, Grid.GetCells().Num());
	CellVersions.Init(FIntPoint::ZeroValue, Grid.GetCells().Num());
	CellTurnChoices.Init(FIntPoint::ZeroValue, Grid.GetCells().Num());
	BouldersByChunk.SetNum(Grid.GetNumChunks());
	ApplySurface(Build);

	int32 LevelCounts[5] = {};
	for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
	{
		++LevelCounts[GetCellLevel(Cell) + 2];
	}

	UE_LOG(LogHexHero, Log, TEXT("Planet: %d pieces from %d kits (%d unread), %d flat tiles, %d triangles, %d vertices, cells by level from -2: %d %d %d %d %d, mean peak %.1f, level height %.1f"),
		SurfaceKit->Pieces.Num(), SurfaceKit->Levels.Num(), SurfaceKit->UnreadMeshes, Build.FlatTiles, NumTriangles, NumVertices, LevelCounts[0], LevelCounts[1], LevelCounts[2],
		LevelCounts[3], LevelCounts[4], MeanPeak, Settings->LevelHeight);
	UE_LOG(LogHexHero, Log, TEXT("Planet: kit read in %.0f ms, surface of %d chunks, %d coarse triangles, built in %.0f ms, shown in %.0f ms"),
		(BuildStart - KitStart) * 1000.0, Grid.GetNumChunks(), NumCoarseTriangles, (ApplyStart - BuildStart) * 1000.0, (FPlatformTime::Seconds() - ApplyStart) * 1000.0);
}

void AHexPlanet::ApplySurface(HexPlanetSurface::FBuild& Build)
{
	// the real coast of these chunks, for the sea field
	ChunkCoasts.SetNum(Grid.GetNumChunks());
	for (int32 Slot = 0; Slot < Build.Chunks.Num() && Slot < Build.Coasts.Num(); ++Slot)
	{
		ChunkCoasts[Build.Chunks[Slot]] = MoveTemp(Build.Coasts[Slot]);
	}

	for (int32 Slot = 0; Slot < Build.Chunks.Num(); ++Slot)
	{
		UProceduralMeshComponent* ChunkMesh = SurfaceChunks[Build.Chunks[Slot]];

		// a whole chunk is replaced, as the counts of its vertices change, which recreates the render state of this chunk alone
		for (int32 Part = 0; Part < 2; ++Part)
		{
			HexPlanetSurface::FSection& Section = Build.Sections[Slot * 2 + Part];
			ChunkMesh->CreateMeshSection_LinearColor(Part, Section.Positions, Section.Triangles, Section.Normals, Section.GridUVs, Section.MeshUVs,
				Section.CellUVs, Section.LocalUVs, Section.Colors, {}, false);
			ChunkMesh->SetMaterial(Part, GetChunkMaterial(Build.Chunks[Slot], Part, bAppliedCut, AppliedView.Target));
		}

		if (CoarseChunks.IsValidIndex(Build.Chunks[Slot]))
		{
			UProceduralMeshComponent* CoarseMesh = CoarseChunks[Build.Chunks[Slot]];
			HexPlanetSurface::FSection& Section = Build.CoarseSections[Slot];
			CoarseMesh->CreateMeshSection_LinearColor(0, Section.Positions, Section.Triangles, Section.Normals, Section.GridUVs, Section.MeshUVs,
				Section.CellUVs, Section.LocalUVs, Section.Colors, {}, false);
			CoarseMesh->SetMaterial(0, GetChunkMaterial(Build.Chunks[Slot], 2, bAppliedCut, AppliedView.Target));
		}
	}

	TBitArray<> bChunks(false, Grid.GetNumChunks());
	for (const int32 Chunk : Build.Chunks)
	{
		bChunks[Chunk] = true;
	}

	for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
	{
		if (bChunks[Grid.GetCells()[Cell].Chunk])
		{
			CellMeshes[Cell] = Build.CellMeshes[Cell];
			CellPieces[Cell] = Build.CellPieces[Cell];
			CellTurns[Cell] = Build.CellTurns[Cell];
			CellMirrored[Cell] = Build.CellMirrored[Cell];
			CellVersions[Cell] = Build.CellVersions[Cell];
			CellTurnChoices[Cell] = Build.CellTurnChoices[Cell];
		}
	}

	if (BouldersByChunk.Num() == Grid.GetNumChunks() && Build.Boulders.Num() == Build.Chunks.Num())
	{
		for (int32 Slot = 0; Slot < Build.Chunks.Num(); ++Slot)
		{
			BouldersByChunk[Build.Chunks[Slot]] = MoveTemp(Build.Boulders[Slot]);
		}
		UpdateBoulders();
	}

	// the factor of the height depends on the place and the level of a cell alone, so a build of some chunks gives it for all
	if (Build.CellRelief.Num() == CellRelief.Num())
	{
		CellRelief = Build.CellRelief;
	}

	// the debug view puts its materials and visibility on the sections again and copies them anew
	bSurfaceChanged = true;
}

int32 AHexPlanet::FindCellAlongRay(const FVector& Origin, const FVector& Direction) const
{
	const FVector RayDirection = Direction.GetSafeNormal();
	double Distance = FHexSphereGrid::IntersectSphere(Origin, RayDirection, Radius + 3.0 * Settings->LevelHeight);

	if (Distance < 0.0)
	{
		return INDEX_NONE;
	}

	int32 Cell = Grid.FindCell(Origin + RayDirection * Distance);

	// the ray goes on to the top of the cell it reaches, which may lie lower and further on
	for (int32 Pass = 0; Pass < 3 && Cell != INDEX_NONE; ++Pass)
	{
		const double Top = FHexSphereGrid::IntersectSphere(Origin, RayDirection, GetCellTopRadius(Cell));

		if (Top < 0.0)
		{
			break;
		}
		Cell = Grid.FindCell(Origin + RayDirection * Top, Cell);
	}
	return Cell;
}

void AHexPlanet::CreateBoulderComponents(const UHexPlanetKitConfig* Config)
{
	if (!Config || !BoulderComponents.IsEmpty())
	{
		return;
	}

	const FHexPlanetBoulders& Boulders = Config->Boulders;
	// the values of DA_HexPlanet that the material takes, such as BoulderTint
	UMaterialInterface* BoulderParent = Boulders.Material.LoadSynchronous();
	UMaterialInstanceDynamic* BoulderMaterial = BoulderParent ? UMaterialInstanceDynamic::Create(BoulderParent, this) : nullptr;
	if (BoulderMaterial)
	{
		HexMaterialConfig::Apply(BoulderMaterial, Settings);
	}
	for (int32 Index = 0; Index < Boulders.Meshes.Num(); ++Index)
	{
		UStaticMesh* Mesh = Boulders.Meshes[Index].LoadSynchronous();
		UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Boulders%d"), Index));
		Component->SetupAttachment(Surface);
		Component->SetStaticMesh(Mesh);
		if (BoulderMaterial)
		{
			Component->SetMaterial(0, BoulderMaterial);
		}
		Component->NumCustomDataFloats = 1;
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(Boulders.bCastShadow);
		Component->bAffectDistanceFieldLighting = false;
		Component->SetCullDistances(0, int32(Boulders.CullDistance));
		Component->RegisterComponent();
		BoulderComponents.Add(Component);
	}
}

void AHexPlanet::UpdateBoulders()
{
	if (BoulderComponents.IsEmpty())
	{
		return;
	}

	// the rock of the biome of the mountain, as the surface draws it on slopes
	const auto GetRock = [this](int32 Cell)
	{
		const int32 Biome = GetCellBiome(Cell);
		return BiomeConfigs.IsValidIndex(Biome) && BiomeConfigs[Biome] ? float(FMath::Clamp(BiomeConfigs[Biome]->MountainRockLayer, 0, 15)) : 2.0f;
	};

	int32 Total = 0;
	for (int32 Mesh = 0; Mesh < BoulderComponents.Num(); ++Mesh)
	{
		TArray<FTransform> Transforms;
		TArray<float> Rocks;
		for (const TArray<HexPlanetSurface::FBoulder>& Chunk : BouldersByChunk)
		{
			for (const HexPlanetSurface::FBoulder& Boulder : Chunk)
			{
				if (Boulder.Mesh == Mesh)
				{
					Transforms.Add(FTransform(FQuat(Boulder.Rotation), FVector(Boulder.Position), FVector(Boulder.Scale)));
					Rocks.Add(GetRock(Boulder.Cell));
				}
			}
		}

		UHierarchicalInstancedStaticMeshComponent* Component = BoulderComponents[Mesh];
		Component->ClearInstances();
		Component->AddInstances(Transforms, false, false, false);
		for (int32 Instance = 0; Instance < Rocks.Num(); ++Instance)
		{
			Component->SetCustomDataValue(Instance, 0, Rocks[Instance], false);
		}
		Component->MarkRenderStateDirty();
		Total += Transforms.Num();
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: %d boulders"), Total);
}

void AHexPlanet::CreateFloeComponents(const UHexPlanetKitConfig* Config)
{
	if (!Config || !FloeComponents.IsEmpty())
	{
		return;
	}

	const FHexPlanetFloes& Floes = Config->Floes;
	UMaterialInterface* FloeParent = Floes.Material.LoadSynchronous();
	UMaterialInstanceDynamic* FloeMaterial = FloeParent ? UMaterialInstanceDynamic::Create(FloeParent, this) : nullptr;
	if (FloeMaterial)
	{
		HexMaterialConfig::Apply(FloeMaterial, Settings);
		FloeMaterial->SetScalarParameterValue(TEXT("PlanetRadius"), float(Radius));
	}

	TArray<TSoftObjectPtr<UStaticMesh>> Meshes = Floes.Meshes;
	NumFloeSlabs = Meshes.Num();
	Meshes.Append(Floes.ShardMeshes);
	NumFloeMeshes = Meshes.Num();
	// each mesh twice: over the shallows, whose seabed shows the shadows through the water, and over the deep sea, whose seabed it hides, so
	// those cast none, as the unlit water takes no shadows
	for (int32 Index = 0; Index < Meshes.Num() * 2; ++Index)
	{
		const bool bDeep = Index >= Meshes.Num();
		UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this,
			*FString::Printf(TEXT("Floes%s%d"), bDeep ? TEXT("Deep") : TEXT("Shallow"), Index % Meshes.Num()));
		Component->SetupAttachment(Surface);
		Component->SetStaticMesh(Meshes[Index % Meshes.Num()].LoadSynchronous());
		if (FloeMaterial)
		{
			Component->SetMaterial(0, FloeMaterial);
		}
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(Settings->bFloeShadows && !bDeep);
		Component->bAffectDistanceFieldLighting = false;
		Component->SetCullDistances(0, int32(Settings->FloeCullDistance));
		// far floes stand still, so their shadows need not be drawn anew every frame
		Component->WorldPositionOffsetDisableDistance = int32(Settings->FloeMotionDistance);
		Component->RegisterComponent();
		FloeComponents.Add(Component);
	}
}

void AHexPlanet::UpdateFloes()
{
	if (FloeComponents.IsEmpty() || ShownLevels.Num() != Grid.GetCells().Num())
	{
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const UHexPlanetConfig& Config = *Settings;

	const auto IsIce = [this](int32 Cell)
	{
		return ShownLevels[Cell] < 0 && BiomeConfigs.IsValidIndex(CellBiomes[Cell]) && BiomeConfigs[CellBiomes[Cell]]->bFrozen;
	};

	// how many cells into the ice each cell of it lies from open sea, so the edge of the pack is loose and its inside dense; ice with no open sea
	// anywhere near counts as inside
	const int32 DenseRings = FMath::Max(1, FMath::RoundToInt32(Config.FloeDenseRings));
	TArray<int32> Rings;
	Rings.Init(0, Cells.Num());
	TArray<int32> Front;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (!IsIce(Cell))
		{
			continue;
		}
		Rings[Cell] = DenseRings;
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (ShownLevels[Neighbor] < 0 && !IsIce(Neighbor))
			{
				Rings[Cell] = 1;
				Front.Add(Cell);
				break;
			}
		}
	}
	for (int32 Ring = 2; Ring < DenseRings && !Front.IsEmpty(); ++Ring)
	{
		TArray<int32> Next;
		for (const int32 Cell : Front)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (IsIce(Neighbor) && Rings[Neighbor] > Ring)
				{
					Rings[Neighbor] = Ring;
					Next.Add(Neighbor);
				}
			}
		}
		Front = MoveTemp(Next);
	}

	struct FFloe
	{
		FVector3d Position;
		double Size;
	};
	TArray<TArray<FFloe>> FloesByCell;
	FloesByCell.SetNum(Cells.Num());
	TArray<TArray<FTransform>> Transforms;
	Transforms.SetNum(FloeComponents.Num());
	const int32 NumShards = NumFloeMeshes - NumFloeSlabs;
	int32 NumShadowed = 0;

	const double HexRadius = Config.HexRadius;
	const double CellArea = 1.5 * FMath::Sqrt(3.0) * HexRadius * HexRadius;
	// a slab covers about this share of the disc of its radius, as its outline is broken
	constexpr double FillShare = 0.8;
	const double NoiseScale = Radius / (HexRadius * FMath::Sqrt(3.0) * FMath::Max(Config.FloeCoverNoiseCells, 0.1f));
	const double SizeMin = Config.FloeSizeMin;
	const double SizeMax = FMath::Max(Config.FloeSizeMax, Config.FloeSizeMin);
	int32 Total = 0;

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (!IsIce(Cell))
		{
			continue;
		}
		const FHexSphereCell& Each = Cells[Cell];
		const double Dense = DenseRings > 1 ? FMath::Clamp(double(Rings[Cell] - 1) / double(DenseRings - 1), 0.0, 1.0) : 1.0;
		const double Noise = FMath::PerlinNoise3D(FVector(Each.Center * NoiseScale));
		const double Cover = FMath::Clamp(FMath::Lerp(double(Config.FloeEdgeCover), double(Config.FloeInnerCover), Dense) + Noise * Config.FloeCoverNoise, 0.0, 0.9);
		if (Cover <= 0.0)
		{
			continue;
		}

		// the plane of the cell, with the neighbors as 2D points on it, so a floe keeps its center in its cell and away from land
		const FVector3d Up = Each.Center.GetSafeNormal();
		const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitX());
		const FVector3d North = FVector3d::CrossProduct(Up, East);
		TArray<FVector2d, TInlineAllocator<6>> Sides;
		for (const int32 Neighbor : Each.Neighbors)
		{
			const FVector3d Offset = (Cells[Neighbor].Center.GetSafeNormal() - Up) * Radius;
			Sides.Add(FVector2d(FVector3d::DotProduct(Offset, East), FVector3d::DotProduct(Offset, North)));
		}

		FRandomStream Random(int32(Cell * 2654435761u ^ 0x5bd1e995u));
		const double CellSizeMax = FMath::Max(SizeMin, FMath::Lerp(FMath::Min(double(Config.FloeEdgeSizeMax), SizeMax), SizeMax, Dense));
		TArray<double> Sizes;
		for (int32 Try = 0; Try < Config.FloeTries; ++Try)
		{
			Sizes.Add(FMath::Lerp(SizeMin, CellSizeMax, FMath::Pow(double(Random.FRand()), double(Config.FloeSizePower))));
		}
		// the large ones first, so the small ones fill the water between them
		Sizes.Sort([](double A, double B) { return A > B; });

		const double Target = Cover * CellArea;
		double Covered = 0.0;
		for (const double Size : Sizes)
		{
			if (Covered >= Target)
			{
				break;
			}
			if (Covered + UE_DOUBLE_PI * Size * Size * FillShare > Target * 1.25)
			{
				continue;
			}
			for (int32 Attempt = 0; Attempt < 3; ++Attempt)
			{
				const double Angle = Random.FRand() * UE_DOUBLE_TWO_PI;
				const double Distance = FMath::Sqrt(double(Random.FRand())) * HexRadius;
				const FVector2d Point(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance);

				bool bFits = true;
				for (int32 Side = 0; Side < Sides.Num() && bFits; ++Side)
				{
					const double Length = Sides[Side].Size();
					const double ToSide = (Length * 0.5 - FVector2d::DotProduct(Point, Sides[Side]) / Length);
					bFits = ToSide >= (ShownLevels[Each.Neighbors[Side]] >= 0 ? Size * Config.FloeShoreGap : 0.0);
				}
				if (!bFits)
				{
					continue;
				}

				const FVector3d Position = (Up * Radius + East * Point.X + North * Point.Y).GetSafeNormal() * Radius;
				const auto Overlaps = [&](int32 Other)
				{
					for (const FFloe& Floe : FloesByCell[Other])
					{
						if (FVector3d::DistSquared(Floe.Position, Position) < FMath::Square(Floe.Size + Size + Config.FloeGap * FMath::Min(Floe.Size, Size)))
						{
							return true;
						}
					}
					return false;
				};
				bFits = !Overlaps(Cell);
				for (int32 Index = 0; Index < Each.Neighbors.Num() && bFits; ++Index)
				{
					bFits = !Overlaps(Each.Neighbors[Index]);
				}
				if (!bFits)
				{
					continue;
				}

				FloesByCell[Cell].Add({Position, Size});
				Covered += UE_DOUBLE_PI * Size * Size * FillShare;

				const bool bShard = Size < Config.FloeShardSize && NumShards > 0;
				const bool bShallow = ShownLevels[Cell] == -1;
				NumShadowed += bShallow ? 1 : 0;
				const int32 Mesh = (bShard || NumFloeSlabs == 0 ? NumFloeSlabs + Random.RandRange(0, FMath::Max(NumShards, 1) - 1) : Random.RandRange(0, NumFloeSlabs - 1))
					+ (bShallow ? 0 : NumFloeMeshes);
				const FVector3d FloeUp = Position.GetSafeNormal();
				const FQuat4d Lean(FVector3d::CrossProduct(FVector3d::UnitZ(), FloeUp).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitX()),
					FMath::Acos(FMath::Clamp(FloeUp.Z, -1.0, 1.0)));
				const FQuat4d Tilt(FVector3d(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0), 0.0).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::UnitX()),
					FMath::DegreesToRadians(Random.FRand() * Config.FloeTilt));
				const FQuat4d Spin(FVector3d::UnitZ(), Random.FRand() * UE_DOUBLE_TWO_PI);
				if (Transforms.IsValidIndex(Mesh))
				{
					Transforms[Mesh].Add(FTransform(FQuat(Lean * Tilt * Spin), FVector(Position), FVector(Size)));
					++Total;
				}
				break;
			}
		}
	}

	for (int32 Mesh = 0; Mesh < FloeComponents.Num(); ++Mesh)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = FloeComponents[Mesh];
		Component->ClearInstances();
		Component->AddInstances(Transforms[Mesh], false, false, false);
		Component->MarkRenderStateDirty();
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: %d floes, %d of them over the shallows with shadows, placed in %.1f ms"), Total, NumShadowed, (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void AHexPlanet::CreateCellMarks()
{
	const int32 Rows = FMath::DivideAndRoundUp(Grid.GetCells().Num(), HexPlanetSurface::MarkColumns);
	MarkPixels.Init(FColor(0, 0, 0, 0), HexPlanetSurface::MarkColumns * Rows);
	MarkedCells.Reset();

	const FName Name = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("PlanetCellMarks"));
	CellMarks = UTexture2D::CreateTransient(HexPlanetSurface::MarkColumns, Rows, PF_B8G8R8A8, Name);
	CellMarks->SRGB = false;
	CellMarks->Filter = TF_Nearest;
	CellMarks->NeverStream = true;

	FTexture2DMipMap& Mip = CellMarks->GetPlatformData()->Mips[0];
	FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), MarkPixels.GetData(), MarkPixels.Num() * MarkPixels.GetTypeSize());
	Mip.BulkData.Unlock();
	CellMarks->UpdateResource();
}

void AHexPlanet::SetCellMarks(const TMap<int32, FColor>& Marks)
{
	bool bChanged = false;

	for (const int32 Cell : MarkedCells)
	{
		if (!Marks.Contains(Cell))
		{
			MarkPixels[Cell] = FColor(0, 0, 0, 0);
			bChanged = true;
		}
	}

	MarkedCells.Reset();
	for (const TPair<int32, FColor>& Mark : Marks)
	{
		if (MarkPixels.IsValidIndex(Mark.Key))
		{
			bChanged |= MarkPixels[Mark.Key] != Mark.Value;
			MarkPixels[Mark.Key] = Mark.Value;
			MarkedCells.Add(Mark.Key);
		}
	}

	if (!bChanged || !CellMarks)
	{
		return;
	}

	// the whole texture goes up, a few hundred kilobytes, which the render thread frees once it has it
	const int32 Rows = MarkPixels.Num() / HexPlanetSurface::MarkColumns;
	uint8* Data = static_cast<uint8*>(FMemory::Malloc(MarkPixels.Num() * sizeof(FColor)));
	FMemory::Memcpy(Data, MarkPixels.GetData(), MarkPixels.Num() * sizeof(FColor));
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, HexPlanetSurface::MarkColumns, Rows);
	CellMarks->UpdateTextureRegions(0, 1, Region, HexPlanetSurface::MarkColumns * sizeof(FColor), sizeof(FColor), Data,
		[](uint8* InData, const FUpdateTextureRegion2D* InRegion)
		{
			FMemory::Free(InData);
			delete InRegion;
		});
}

void AHexPlanet::SetHoveredCell(int32 Cell, int32 BrushSize)
{
	// the neighbors are outlined while the wireframe draws them, which a brush leaves out, as it outlines its own cells
	const FHexDebugViewSettings View = AHexWorldMap::GetDebugViewSettings();
	const bool bNeighbors = BrushSize <= 0 && View.Mode != EHexWireframeMode::Off && View.Scope != EHexWireframeScope::Hover;

	if (Cell == HoveredCell && BrushSize == HighlightBrushSize && bNeighbors == bHighlightNeighbors)
	{
		return;
	}

	HoveredCell = Cell;
	HighlightBrushSize = BrushSize;
	bHighlightNeighbors = bNeighbors;

	// an outline is centered on the edge of a set of cells, so each side of the edge is marked in both cells that share it: red holds the bits
	// of the sides of the first layer, drawn in MarkPrimaryColor, and green those of the second, in NeighborOutlineColor. Side K faces neighbor K
	// (MARK_CODE, which also joins the sides at the bends of the edge)
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TMap<int32, FColor> Marks;

	const auto MarkEdge = [&](TConstArrayView<int32> Set, bool bPrimary)
	{
		const auto MarkSide = [&](int32 Each, int32 Side)
		{
			FColor& Mark = Marks.FindOrAdd(Each, FColor(0, 0, 0, 0));
			(bPrimary ? Mark.R : Mark.G) |= uint8(1 << Side);
		};

		for (const int32 Inside : Set)
		{
			for (int32 Side = 0; Side < Cells[Inside].Neighbors.Num(); ++Side)
			{
				const int32 Outside = Cells[Inside].Neighbors[Side];
				if (!Set.Contains(Outside))
				{
					MarkSide(Inside, Side);
					MarkSide(Outside, Cells[Outside].Neighbors.Find(Inside));
				}
			}
		}
	};
	if (Cells.IsValidIndex(Cell) && BrushSize > 0)
	{
		// the cells of the brush, its rings found through the neighbors
		TArray<int32, TInlineAllocator<64>> Brush{Cell};
		int32 RingStart = 0;

		for (int32 Ring = 1; Ring < BrushSize; ++Ring)
		{
			const int32 RingEnd = Brush.Num();
			for (int32 Index = RingStart; Index < RingEnd; ++Index)
			{
				for (const int32 Neighbor : Cells[Brush[Index]].Neighbors)
				{
					Brush.AddUnique(Neighbor);
				}
			}
			RingStart = RingEnd;
		}
		MarkEdge(Brush, true);
	}
	else if (Cells.IsValidIndex(Cell))
	{
		if (bNeighbors)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				MarkEdge({Neighbor}, false);
			}
		}
		MarkEdge({Cell}, true);
	}

	// the first layer is the brush or the cell under the cursor
	const FLinearColor PrimaryColor = BrushSize > 0 ? Settings->BrushOutlineColor : Settings->OutlineColor;
	for (UMaterialInstanceDynamic* Instance : { SurfaceMaterial.Get(), SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get(), WaterGridMaterial.Get() })
	{
		if (Instance)
		{
			Instance->SetVectorParameterValue(TEXT("MarkPrimaryColor"), PrimaryColor);
		}
	}
	SetCellMarks(Marks);
}

FString AHexPlanet::GetCellDebugText(int32 Cell) const
{
	if (!Grid.GetCells().IsValidIndex(Cell))
	{
		return FString();
	}

	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const double Latitude = FMath::RadiansToDegrees(FMath::Asin(Each.Center.Z));
	const double Longitude = FMath::RadiansToDegrees(FMath::Atan2(Each.Center.Y, Each.Center.X));

	const int32 Piece = CellPieces.IsValidIndex(Cell) ? CellPieces[Cell] : INDEX_NONE;
	const TArray<FString>* PieceNames = SurfaceKit ? &SurfaceKit->PieceNames : nullptr;
	const FString PieceText = PieceNames && PieceNames->IsValidIndex(Piece) ? FString::Printf(TEXT("%s, turned %d%s, height x%.2f\nVersion %d of %d, turn %d of %d%s"), *(*PieceNames)[Piece], CellTurns[Cell],
		CellMirrored[Cell] ? TEXT(", mirrored") : TEXT(""), CellRelief.IsValidIndex(Cell) ? CellRelief[Cell] : 1.0f,
		GetCellVersion(Cell).X + 1, GetCellVersion(Cell).Y, GetCellTurnChoice(Cell).X + 1, GetCellTurnChoice(Cell).Y,
		GetCellTurnChoice(Cell).Y <= 1 ? TEXT(" (its neighbors fix the turn)") : TEXT("")) : FString(TEXT("flat tile"));

	const FString BiomeName = BiomeConfigs.IsValidIndex(GetCellBiome(Cell)) ? BiomeConfigs[GetCellBiome(Cell)]->DisplayName : FString(TEXT("none"));

	// what the brush of relief set by hand
	FString ReliefText;
	if (const HexPlanetSurface::FReliefOverride* Override = ReliefOverrides.Find(Cell))
	{
		TArray<FString> Parts;
		if (Override->Version >= 0) { Parts.Add(TEXT("version")); }
		if (Override->Turn >= 0) { Parts.Add(TEXT("turn")); }
		if (Override->Mirror >= 0) { Parts.Add(TEXT("mirror")); }
		if (!FMath::IsNearlyEqual(Override->Scale, 1.0f)) { Parts.Add(FString::Printf(TEXT("height x%.2f"), Override->Scale)); }
		ReliefText = FString::Printf(TEXT(", set by hand: %s"), *FString::Join(Parts, TEXT(", ")));
	}

	return FString::Printf(TEXT("Cell %d (%s)\nLevel: %s\nBiome: %s\nPiece: %s\nFace: %d, chunk %d\nLatitude %.1f, longitude %.1f\nArea: %.1f%% of the mean\nSides: longest x%.3f of shortest"),
		Cell, Each.IsPentagon() ? TEXT("pentagon") : TEXT("hexagon"), LevelNames[GetCellLevel(Cell) + 2], *BiomeName, *(PieceText + ReliefText), Each.Face, Each.Chunk,
		Latitude, Longitude, Grid.GetCellArea(Cell) / MeanCellArea * 100.0, Grid.GetCellSideRatio(Cell));
}
