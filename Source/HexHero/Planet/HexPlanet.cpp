// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanet.h"
#include "HexHero.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameViewportClient.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
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
#include "AssetRegistry/AssetRegistryModule.h"
#include "HexPlanetObjects.h"
#include "HexPlanetObjectSet.h"
#include "HexPlanetRivers.h"
#include "HexPlanetSeaField.h"
#include "HexPlanetUnderground.h"
#include "HexPlanetWaterConfig.h"
#include "HexTerrainLayersConfig.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "HAL/IConsoleManager.h"
#include "ProceduralMeshComponent.h"
#include "Misc/ScopeExit.h"
#include "Misc/PackageName.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "HAL/PlatformMemory.h"
#include "RenderTimer.h"
#include "Algo/BinarySearch.h"
#include "Async/ParallelFor.h"
#include "HexPlanetMeshComponent.h"
#include "HexPlanetMotes.h"

namespace
{
	TAutoConsoleVariable<float> CVarHexPlanetCoarseDistance(TEXT("hex.PlanetCoarseDistance"), 0.0f,
		TEXT("Distance from the camera beyond which the planet draws the coarse mesh of its chunks, in place of CoarseDistance of DA_HexPlanet. 0 keeps that one."));

	TAutoConsoleVariable<float> CVarHexPlanetCoarseBand(TEXT("hex.PlanetCoarseBand"), 0.0f,
		TEXT("Half the width of the band over which the planet dissolves its chunks into their coarse meshes, in place of CoarseBand of DA_HexPlanet. 0 keeps that one."));

	TAutoConsoleVariable<bool> CVarHexPlanetObjects(TEXT("hex.PlanetObjects"), true,
		TEXT("Draws the forests, bushes, stones and decor of the planet (1) or hides them (0), to see what they cost."));

	TAutoConsoleVariable<bool> CVarHexPlanetRims(TEXT("hex.PlanetRims"), true,
		TEXT("Draws the rims of the chasms of the underground, the rocks, spires and runes on the lips of the passages and the stalactites below them (1), or hides them (0), to see what they cost."));

	TAutoConsoleVariable<bool> CVarHexPlanetWalls(TEXT("hex.PlanetWalls"), true,
		TEXT("Draws the walls on the lips of the passages of the underground (1), or hides them (0), to see what they cost."));

	TAutoConsoleVariable<int32> CVarHexPlanetWallLod(TEXT("hex.PlanetWallLod"), 0,
		TEXT("Draws each chunk of the walls of the underground by its distance (0), or every chunk whole where its whole walls are built (1), in the band where both dissolve into each other (2) or coarse (3)."));

	TAutoConsoleVariable<bool> CVarHexPlanetMotes(TEXT("hex.PlanetMotes"), true,
		TEXT("Draws the motes drifting over the passages of the underground and the spores rising from its mushrooms (1), or hides them (0), to see what they cost."));

	TAutoConsoleVariable<int32> CVarHexPlanetHeroLight(TEXT("hex.PlanetHeroLight"), 1,
		TEXT("The light around the hero underground, for now over the focus of the camera: 1 as DA_HexUnderground has it, 2 without its shadows, 0 none, to see what it costs and what lies in the dark."));

	TAutoConsoleVariable<int32> CVarHexPlanetForcedLight(TEXT("hex.PlanetForcedLight"), 0,
		TEXT("1 lights both layers of the planet evenly, as before the dark (the key L): the shade of the surface away from the hero goes, and underground the light of the hero and the fog around it, while the sun, the sky light and the light the ground gives by itself take their forced values. 0 is the light of the game."));

	TAutoConsoleVariable<bool> CVarHexPlanetStreamPieces(TEXT("hex.PlanetStreamPieces"), true,
		TEXT("Keeps the pieces of only the chunks of the planet near the camera on the GPU (1), building them again as it comes near, or of every chunk (0), to compare."));

	TAutoConsoleVariable<bool> CVarHexPlanetSpawnUnderground(TEXT("hex.PlanetSpawnUnderground"), true,
		TEXT("Spawns the underground with the planet (1), or the surface alone (0), to measure what the underground costs; read when the planet begins to play."));

	/** Returns the planet shown once it is built, else null, as the commands of -ExecCmds may come before it is */
	AHexPlanet* GetShownPlanet(const UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		return Layers ? Layers->GetPresentedBuilt() : nullptr;
	}

	/** Returns the surface, for the commands about the surface only, such as the sea or the rivers; once it is built if asked */
	AHexPlanet* GetSurfacePlanet(const UWorld* World, bool bBuilt)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		AHexPlanet* Planet = Layers ? Layers->GetSurface() : nullptr;
		return Planet && (!bBuilt || Planet->IsBuilt()) ? Planet : nullptr;
	}

	/** Prepares a section of a build for UHexPlanetMeshComponent, named as the component of its chunk, so the ray tracing geometry shows whose it is */
	FHexPlanetMeshSectionRef PrepareChunkSection(const HexPlanetSurface::FSection& Section, const TCHAR* Kind, int32 Chunk)
	{
		FHexPlanetMeshSource Source;
		Source.Positions = Section.Positions;
		Source.Normals = Section.Normals;
		Source.UVs[0] = Section.GridUVs;
		Source.UVs[1] = Section.MeshUVs;
		Source.UVs[2] = Section.CellUVs;
		Source.UVs[3] = Section.LocalUVs;
		Source.Triangles = Section.Triangles;
		return UHexPlanetMeshComponent::PrepareSection(Source, FName(*FString::Printf(TEXT("%s%d"), Kind, Chunk)));
	}

	/** Returns the bounds of the two sections of pieces of a chunk of a build */
	FBoxSphereBounds GetPieceBounds(const HexPlanetSurface::FBuild& Build, int32 Slot)
	{
		FBox3f Box(ForceInit);
		for (int32 Part = 0; Part < 2; ++Part)
		{
			for (const FVector3f& Position : Build.Sections[Slot * 2 + Part].Positions)
			{
				Box += Position;
			}
		}
		return Box.IsValid ? FBoxSphereBounds(FBox(Box)) : FBoxSphereBounds(ForceInit);
	}

	const TCHAR* LevelNames[] = {TEXT("deep sea"), TEXT("shallows"), TEXT("plains"), TEXT("hills"), TEXT("mountains")};

	/** Cells whose centers the water material compares a pixel with, to find whether it lies in a hidden cell: the cell, its neighbors and theirs */
	const int32 MaxWaterHideCells = 19;

	/** How many mouths of rivers the water takes, as RIVER_MOUTHS of create_planet_assets.py */
	constexpr int32 MaxRiverMouths = 8;

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

		// the layer shown
		AHexPlanet* Planet = GetShownPlanet(World);

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
		// the layer shown, whose biomes have the letters of its files
		AHexPlanet* Planet = GetShownPlanet(World);

		const int32 Biome = Planet && Args.Num() >= 2 && Args[1].Len() == 1 ? Planet->GetBiomeLetters().IndexOfByKey(FChar::ToUpper(Args[1][0])) : INDEX_NONE;
		const bool bParsed = Args.Num() >= 2 && LexTryParseString(Cell, *Args[0]) && (Args.Num() < 3 || LexTryParseString(Rings, *Args[2]));

		if (!bParsed || Biome == INDEX_NONE || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetSetBiome <Cell> <Letter> [Rings], on a planet, with the PlanetLetter of a biome, or its UndergroundLetter underground"));
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

		AHexPlanet* Planet = GetShownPlanet(World);

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
		// the sea is on the surface alone
		AHexPlanet* Planet = GetSurfacePlanet(World, true);

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

	/** Sets how straight the rivers run while the game runs, or back to DA_HexPlanet without arguments */
	void RiverShapeCommand(const TArray<FString>& Args, UWorld* World)
	{
		// the rivers are on the surface alone, which takes the shape before it is built too
		AHexPlanet* Planet = GetSurfacePlanet(World, false);
		double CornerCut = 1.0;
		int32 Relax = 0;
		double MaxOffset = 0.0;
		const bool bParsed = Args.IsEmpty() || (LexTryParseString(CornerCut, *Args[0]) && (Args.Num() < 2 || LexTryParseString(Relax, *Args[1]))
			&& (Args.Num() < 3 || LexTryParseString(MaxOffset, *Args[2])));
		if (!Planet || !bParsed)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetRiverShape [CornerCut 0..1] [Relax passes] [MaxOffset], on a planet; without arguments the values of DA_HexPlanet"));
			return;
		}
		Planet->SetRiverShape(Args.IsEmpty() ? TOptional<FVector3d>() : TOptional<FVector3d>(FVector3d(FMath::Clamp(CornerCut, 0.0, 1.0), FMath::Clamp(Relax, 0, 50),
			FMath::Max(MaxOffset, 0.0))));
	}

	/** Writes a map of an underground for looking at it, in longitude and latitude: the void dark, the passages in a color per network, red
	 *  where networks join, grey where no exit reaches, over the land of the surface faintly, and the entrances as white dots */
	void WriteUndergroundImage(const FString& Path, const FHexSphereGrid& Grid, TConstArrayView<int8> Surface, const HexPlanetUnderground::FLoaded& Underground)
	{
		// only for looking at the maze, so not values to tune
		static const FColor NetworkColors[HexPlanetFile::MaxNetworks] = {FColor(230, 180, 60), FColor(80, 190, 230), FColor(120, 220, 90), FColor(230, 110, 200),
			FColor(240, 140, 70), FColor(150, 130, 240), FColor(60, 220, 190), FColor(220, 220, 120)};
		const int32 Width = 1440;
		const int32 Height = 720;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		TArray<uint8> IsEntrance;
		IsEntrance.Init(0, Grid.GetCells().Num());
		for (const FHexEntrance& Entrance : Underground.Entrances)
		{
			if (IsEntrance.IsValidIndex(Entrance.Cell))
			{
				IsEntrance[Entrance.Cell] = 1;
			}
		}

		int32 Hint = INDEX_NONE;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const double Latitude = UE_DOUBLE_HALF_PI - (Y + 0.5) / Height * UE_DOUBLE_PI;
			for (int32 X = 0; X < Width; ++X)
			{
				const double Longitude = (X + 0.5) / Width * UE_DOUBLE_TWO_PI - UE_DOUBLE_PI;
				const FVector3d Direction(FMath::Cos(Latitude) * FMath::Cos(Longitude), FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
				const int32 Cell = Hint = Grid.FindCell(Direction, Hint);
				const int8 Network = Underground.Report.CellNetworks.IsValidIndex(Cell) ? Underground.Report.CellNetworks[Cell] : INDEX_NONE;
				FColor Color = Surface[Cell] >= 0 ? FColor(34, 40, 36) : FColor(14, 22, 30);
				if (HexPlanetUnderground::IsPassage(Underground.Levels[Cell]))
				{
					Color = Network >= 0 ? NetworkColors[Network] : Network == HexPlanetUnderground::MergedNetwork ? FColor(255, 40, 40) : FColor(150, 150, 150);
				}
				Pixels[Y * Width + X] = IsEntrance[Cell] ? FColor::White : Color;
			}
		}

		const FImageView Image(Pixels.GetData(), Width, Height);
		if (FImageUtils::SaveImageByExtension(*Path, Image))
		{
			UE_LOG(LogHexHero, Log, TEXT("Underground: wrote the map %s"), *Path);
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Underground: could not write %s"), *Path);
		}
	}

	/** Reads the underground of planets from their files, generates what is missing and writes it, as a planet does at its start, or generates
	 *  it again, and logs how its networks fit together. It needs no planet in the world, so it also runs from -ExecCmds of the editor */
	void UndergroundCommand(const TArray<FString>& Args)
	{
		HexPlanetUnderground::FLoadOptions Options;
		bool bImage = false;
		bool bObjects = false;
		bool bKnown = !Args.IsEmpty();
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			const bool bMaze = Args[Index].Equals(TEXT("maze"), ESearchCase::IgnoreCase);
			const bool bEntrances = Args[Index].Equals(TEXT("entrances"), ESearchCase::IgnoreCase);
			const bool bThisObjects = Args[Index].Equals(TEXT("objects"), ESearchCase::IgnoreCase);
			const bool bThisImage = Args[Index].Equals(TEXT("image"), ESearchCase::IgnoreCase);
			const bool bBiomes = Args[Index].Equals(TEXT("biomes"), ESearchCase::IgnoreCase);
			Options.bNewMaze |= bMaze;
			Options.bNewEntrances |= bEntrances;
			Options.bNewBiomes |= bBiomes;
			bObjects |= bThisObjects;
			bImage |= bThisImage;
			bKnown &= bMaze || bEntrances || bThisObjects || bThisImage || bBiomes;
		}
		const bool bAll = !Args.IsEmpty() && Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase);

		if (!bKnown || (!bAll && !HexPlanetFile::PlanetExists(Args[0])))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetUnderground <Folder|all> [maze|entrances|objects|biomes] [image], with the folder of a planet in %s"), HexPlanetFile::RootFolder);
			return;
		}

		// a new maze leaves the old objects on cells that are void now and the new passages bare, so it takes new objects too
		bObjects |= Options.bNewMaze || Options.bNewEntrances;
		// the cave kinds as a planet of the game finds them, once the registry has scanned, as -ExecCmds may run before it did
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().WaitForCompletion();
		const TArray<TObjectPtr<UHexPlanetObjectSet>> CaveSets = UHexPlanetObjects::FindLayerSets(EHexPlanetLayer::Underground);
		const UHexPlanetConfig* UndergroundSettings = GetDefault<AHexPlanet>()->UndergroundConfig.LoadSynchronous();

		TArray<FString> Folders;
		if (bAll)
		{
			// a folder of only planet.json is a planet the game generates whole when it opens; all writes into it nothing, so it stays as it is
			for (const FHexPlanetEntry& Planet : HexPlanetFile::FindPlanets())
			{
				if (!HexPlanetFile::HasLevels(Planet.Folder))
				{
					UE_LOG(LogHexHero, Log, TEXT("Underground: skipped %s: no levels.txt, name it to generate one"), *Planet.Folder);
					continue;
				}
				Folders.Add(Planet.Folder);
			}
		}
		else
		{
			Folders.Add(Args[0]);
		}

		TArray<TObjectPtr<UHexBiomeConfig>> Biomes;
		TArray<TCHAR> Letters;
		AHexPlanet::FindBiomes(EHexPlanetLayer::Underground, Biomes, Letters);
		const UHexPlanetConfig* Config = GetDefault<AHexPlanet>()->PlanetConfig.LoadSynchronous();
		Config = Config ? Config : GetDefault<UHexPlanetConfig>();

		for (const FString& Folder : Folders)
		{
			const double StartTime = FPlatformTime::Seconds();
			FHexPlanetManifest Manifest;
			FString Error;
			if (!HexPlanetFile::ReadManifest(Folder, Manifest, Error))
			{
				UE_LOG(LogHexHero, Error, TEXT("Underground: %s"), *Error);
				continue;
			}

			// the surface as the planet reads it, or as it would generate it
			const FHexSphereGrid Grid = FHexSphereGrid::Build(Manifest.Frequency);
			const TArray<FHexSphereCell>& Cells = Grid.GetCells();
			TArray<int8> Surface;
			if (!HexPlanetFile::HasLevels(Folder) || !HexPlanetFile::LoadLevels(Folder, Cells.Num(), Surface, Error))
			{
				HexPlanetGenerator::GenerateLevels(Grid, Manifest, *Config, Surface);
			}
			for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
			{
				Surface[Cell] = Cells[Cell].IsPentagon() ? int8(FMath::Min<int32>(Surface[Cell], 1)) : Surface[Cell];
			}

			HexPlanetUnderground::FLoaded Loaded;
			HexPlanetUnderground::Load(Folder, Grid, Surface, Manifest, Letters, Options, Loaded);
			UE_LOG(LogHexHero, Log, TEXT("Underground of %s: %d cells, %s in %.0f ms with the grid and the files"), *Folder, Cells.Num(),
				Loaded.Report.IsGood() ? TEXT("good") : TEXT("with problems"), (FPlatformTime::Seconds() - StartTime) * 1000.0);

			// the objects as the first show of the underground generates them, written when asked or missing, as a planet does
			const FString DataFolder = HexPlanetFile::GetDataFolder(Folder, EHexPlanetLayer::Underground);
			const bool bMissing = !HexPlanetFile::HasObjects(DataFolder) && (!UndergroundSettings || UndergroundSettings->bGenerateObjects);
			if (!CaveSets.IsEmpty() && (bObjects || bMissing))
			{
				TArray<TCHAR> CellLetters;
				CellLetters.SetNumUninitialized(Cells.Num());
				for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
				{
					CellLetters[Cell] = Loaded.Biomes.IsValidIndex(Cell) && Letters.IsValidIndex(Loaded.Biomes[Cell]) ? Letters[Loaded.Biomes[Cell]] : TEXT('\0');
				}
				TArray<FHexCellObjects> Objects;
				UHexPlanetObjects::GenerateUndergroundObjects(Grid, Loaded.Levels, CellLetters, Loaded.Entrances, Manifest, CaveSets, Objects);
				if (UHexPlanetObjects::SaveCells(DataFolder, Objects, Loaded.Levels, CaveSets, Error))
				{
					UE_LOG(LogHexHero, Log, TEXT("Underground: wrote %s"), *HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::ObjectsFile));
				}
				else
				{
					UE_LOG(LogHexHero, Warning, TEXT("Underground of %s: %s"), *Folder, *Error);
				}
			}
			// the heights of the walls set by hand were for the voids of the old maze
			if (Options.bNewMaze || Options.bNewEntrances)
			{
				const FString WallsPath = HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::WallsFile);
				const bool bHadWalls = FPaths::FileExists(WallsPath);
				if (!HexPlanetFile::SaveWalls(DataFolder, {}, Error))
				{
					UE_LOG(LogHexHero, Warning, TEXT("Underground of %s: %s"), *Folder, *Error);
				}
				else if (bHadWalls)
				{
					UE_LOG(LogHexHero, Log, TEXT("Underground: deleted %s, the heights of the walls of the old maze"), *WallsPath);
				}
			}
			if (bImage)
			{
				WriteUndergroundImage(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Underground"), Folder + TEXT(".png")), Grid, Surface, Loaded);
			}
		}
	}

	FAutoConsoleCommand UndergroundConsoleCommand(TEXT("hex.PlanetUnderground"),
		TEXT("hex.PlanetUnderground <Folder|all> [maze|entrances|objects|biomes] [image]: reads the underground of a planet folder and writes what it lacks, generated, as a planet does at its start; maze generates the maze again for its entrances, entrances places them again too, objects generates underground/objects.json again (maze and entrances do too, as a new maze needs new objects, and delete underground/walls.json, the heights of its walls set by hand), biomes generates underground/biomes.txt again (the floor, the abyss, lava and lakes) and leaves levels.txt and entrances.json as they are, image writes a map of it to Saved/Underground, and the log tells how the networks fit together"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&UndergroundCommand));

	FAutoConsoleCommandWithWorldAndArgs RiverShapeConsoleCommand(TEXT("hex.PlanetRiverShape"),
		TEXT("hex.PlanetRiverShape [CornerCut] [Relax] [MaxOffset]: how straight the rivers run, as RiverCornerCut (0 along the sides of the cells, 1 through their middles), RiverRelax and RiverMaxOffset of DA_HexPlanet; without arguments back to the asset"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RiverShapeCommand));

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

	/** Returns how many windows Slate draws now, its own windows such as a toast of a screenshot among them, each presented apart */
	int32 CountShownWindows()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 0;
		}
		int32 Count = 0;
		TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetTopLevelWindows();
		for (int32 Index = 0; Index < Windows.Num(); ++Index)
		{
			if (Windows[Index]->IsVisible())
			{
				++Count;
				Windows.Append(Windows[Index]->GetChildWindows());
			}
		}
		return Count;
	}

	/** A measurement of hex.PlanetMemory: it waits until the planet is built and Delay seconds more, times the frames of MemoryProbeSeconds,
	 *  logs them with the memory, and quits QuitAfter seconds later if asked, so a script can read the counters of the process meanwhile */
	struct FMemoryProbe
	{
		TWeakObjectPtr<UWorld> World;
		double Delay = 0.0;
		bool bQuit = false;
		double ReadyTime = -1.0;
		double TimingStart = -1.0;
		double QuitTime = -1.0;
		int32 Frames = 0;
		double GpuMs = 0.0;
		double RenderMs = 0.0;
		double GameMs = 0.0;
		double WorstGpuMs = 0.0;
		/** Frames whose time of the render thread the engine gave as about 0, and the most windows Slate drew in a frame */
		int32 RenderZeroFrames = 0;
		int32 MostWindows = 0;
	};
	constexpr double MemoryProbeSeconds = 5.0;
	/** Below this the time of the render thread is no measurement but the slice between the presents of two windows in one frame */
	constexpr double MemoryProbeMinRenderMs = 0.05;
	constexpr double MemoryProbeQuitAfter = 15.0;
	TOptional<FMemoryProbe> MemoryProbe;

	bool TickMemoryProbe(float)
	{
		if (!MemoryProbe.IsSet())
		{
			return false;
		}
		FMemoryProbe& Probe = MemoryProbe.GetValue();
		UWorld* World = Probe.World.Get();
		if (!World)
		{
			MemoryProbe.Reset();
			return false;
		}

		const double Now = FPlatformTime::Seconds();
		if (Probe.QuitTime >= 0.0)
		{
			if (Now < Probe.QuitTime)
			{
				return true;
			}
			MemoryProbe.Reset();
			GEngine->Exec(World, TEXT("quit"));
			return false;
		}

		AHexPlanet* Planet = GetShownPlanet(World);
		if (!Planet)
		{
			return true;
		}
		if (Probe.ReadyTime < 0.0)
		{
			Probe.ReadyTime = Now;
		}
		if (Now - Probe.ReadyTime < Probe.Delay)
		{
			return true;
		}
		if (Probe.TimingStart < 0.0)
		{
			Probe.TimingStart = Now;
			return true;
		}

		// the times of the frame before, as stat unit shows them
		const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		Probe.GpuMs += GpuMs;
		Probe.WorstGpuMs = FMath::Max(Probe.WorstGpuMs, GpuMs);
		// a frame that presented more than one window, such as the toast of a screenshot, keeps only the slice of the last of them, about 0
		const double RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
		const bool bRenderZero = RenderMs < MemoryProbeMinRenderMs;
		Probe.RenderMs += bRenderZero ? 0.0 : RenderMs;
		Probe.RenderZeroFrames += bRenderZero ? 1 : 0;
		Probe.MostWindows = FMath::Max(Probe.MostWindows, CountShownWindows());
		Probe.GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
		++Probe.Frames;
		if (Now - Probe.TimingStart < MemoryProbeSeconds)
		{
			return true;
		}

		// the engine sets the time of the render thread when Slate presents the window (FSlateRHIRenderer::PresentWindow_RenderThread), so a
		// run that presented nothing gives 0, which is no measurement
		const double Frames = FMath::Max(Probe.Frames, 1);
		const FString Windows = Probe.MostWindows > 1 ? FString::Printf(TEXT(", up to %d windows shown"), Probe.MostWindows) : FString();
		const FString RenderText = Probe.RenderZeroFrames * 2 > Probe.Frames
			? FString::Printf(TEXT("render thread n/a (engine stat 0 in %d of the frames%s)"), Probe.RenderZeroFrames, *Windows)
			: FString::Printf(TEXT("render thread %.2f ms%s"), Probe.RenderMs / FMath::Max(Probe.Frames - Probe.RenderZeroFrames, 1),
				Probe.RenderZeroFrames > 0 ? *FString::Printf(TEXT(" (%d frames of about 0 left out%s)"), Probe.RenderZeroFrames, *Windows) : TEXT(""));
		Planet->LogMemory(FString::Printf(TEXT("%d frames over %.0f s: GPU %.2f ms (worst %.2f), %s, game thread %.2f ms, %.1f fps"),
			Probe.Frames, Now - Probe.TimingStart, Probe.GpuMs / Frames, Probe.WorstGpuMs, *RenderText, Probe.GameMs / Frames,
			Probe.Frames / FMath::Max(Now - Probe.TimingStart, 0.001)));
		// and what the hidden layer keeps, once it was built
		const EHexPlanetLayer Hidden = Planet->GetLayer() == EHexPlanetLayer::Surface ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface;
		if (AHexPlanet* Other = Planet->GetLayerPlanet(Hidden); Other && Other->IsBuilt())
		{
			Other->LogMemory(TEXT("the hidden layer"), false);
		}
		if (Probe.bQuit)
		{
			UE_LOG(LogHexHero, Log, TEXT("Planet memory: quitting in %.0f s"), MemoryProbeQuitAfter);
			Probe.QuitTime = Now + MemoryProbeQuitAfter;
			return true;
		}
		MemoryProbe.Reset();
		return false;
	}

	/** Measures the memory of the surface and the times of frames, now or after a delay once the planet is built, and quits after it if asked */
	void MemoryCommand(const TArray<FString>& Args, UWorld* World)
	{
		double Delay = 0.0;
		if ((!Args.IsEmpty() && !LexTryParseString(Delay, *Args[0])) || !World)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetMemory [Delay] [quit]: logs the memory of the planet Delay seconds after it is built, and quits a little later with quit"));
			return;
		}
		const bool bStart = !MemoryProbe.IsSet();
		MemoryProbe.Emplace();
		MemoryProbe->World = World;
		MemoryProbe->Delay = FMath::Max(Delay, 0.0);
		MemoryProbe->bQuit = Args.Num() >= 2 && Args[1].Equals(TEXT("quit"), ESearchCase::IgnoreCase);
		if (bStart)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickMemoryProbe), 0.0f);
		}
	}

	/** Runs a console command after a delay, for scripted tests of what happens while the game runs, such as a jump of the camera */
	void LaterCommand(const TArray<FString>& Args, UWorld* World)
	{
		double Delay = 0.0;
		if (Args.Num() < 2 || !LexTryParseString(Delay, *Args[0]) || !World)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.Later <Seconds> <Command>: runs a console command after a delay"));
			return;
		}
		const FString Command = FString::Join(TConstArrayView<FString>(Args).RightChop(1), TEXT(" "));
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Command](float)
		{
			if (UWorld* Target = WeakWorld.Get())
			{
				// through the player as the console runs it, so commands of the viewport such as HighResShot work too
				UE_LOG(LogHexHero, Log, TEXT("hex.Later: %s"), *Command);
				if (APlayerController* Player = Target->GetFirstPlayerController())
				{
					Player->ConsoleCommand(Command);
				}
				else
				{
					GEngine->Exec(Target, *Command);
				}
			}
			return false;
		}), float(FMath::Max(Delay, 0.0)));
	}

	FAutoConsoleCommandWithWorldAndArgs LaterConsoleCommand(TEXT("hex.Later"),
		TEXT("hex.Later <Seconds> <Command>: runs a console command after a delay, for scripted tests while the game runs"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LaterCommand));

	/** Logs the times of the render thread the engine gives (RenderTimer.h) each frame for a number of frames, for a look at a trip */
	void RenderTimesCommand(const TArray<FString>& Args)
	{
		int32 Frames = 120;
		if (!Args.IsEmpty() && !LexTryParseString(Frames, *Args[0]))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetRenderTimes [Frames]: logs the times of the render thread of the next frames"));
			return;
		}
		TSharedRef<int32> Left = MakeShared<int32>(FMath::Clamp(Frames, 1, 100000));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Left, Frame = 0](float) mutable
		{
			UE_LOG(LogHexHero, Log, TEXT("Render times, frame %d (%llu): render thread %.3f ms, critical path %.3f ms, waits %.3f ms, swap %.3f ms, game thread %.2f ms, %d windows shown"),
				Frame++, uint64(GFrameCounter), FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTimeCriticalPath),
				FPlatformTime::ToMilliseconds(GRenderThreadWaitTime), FPlatformTime::ToMilliseconds(GSwapBufferTime), FPlatformTime::ToMilliseconds(GGameThreadTime), CountShownWindows());
			return --(*Left) > 0;
		}), 0.0f);
	}

	FAutoConsoleCommand RenderTimesConsoleCommand(TEXT("hex.PlanetRenderTimes"),
		TEXT("hex.PlanetRenderTimes [Frames]: logs the times of the render thread, its critical path, its waits and the swap of buffers that the engine gives, each frame for the next Frames (120)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RenderTimesCommand));

	/** Logs the sides of the cells around a cell that take the rims of the chasms, by kind, on the layer shown */
	void RimSidesCommand(const TArray<FString>& Args, UWorld* World)
	{
		AHexPlanet* Planet = GetShownPlanet(World);
		int32 Cell = INDEX_NONE;
		int32 Rings = 0;
		if (!Planet || Args.IsEmpty() || !LexTryParseString(Cell, *Args[0]) || !Planet->GetGrid().GetCells().IsValidIndex(Cell)
			|| (Args.Num() >= 2 && !LexTryParseString(Rings, *Args[1])))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetRimSides <Cell> [Rings]: logs the sides of the cells within Rings of a cell that take rims, on the layer shown once it is built"));
			return;
		}
		const TArray<int32> Distances = HexPlanetUnderground::GetRingDistances(Planet->GetGrid(), {Cell}, FMath::Clamp(Rings, 0, 40));
		int32 Listed = 0;
		for (int32 Each = 0; Each < Distances.Num(); ++Each)
		{
			if (Distances[Each] == MAX_int32)
			{
				continue;
			}
			const FString Text = Planet->GetObjects()->GetRimText(Each);
			if (!Text.IsEmpty() || Each == Cell)
			{
				const FVector3d Center = Planet->GetGrid().GetCells()[Each].Center;
				UE_LOG(LogHexHero, Log, TEXT("Rims: cell %d, %d rings from %d, at %.4f %.4f: %s"), Each, Distances[Each], Cell, FMath::RadiansToDegrees(FMath::Asin(Center.Z)),
					FMath::RadiansToDegrees(FMath::Atan2(Center.Y, Center.X)), Text.IsEmpty() ? TEXT("no side over a void") : *Text);
				++Listed;
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("Rims: %d cells within %d rings of %d listed on the %s"), Listed, Rings, Cell, GetPlanetLayerName(Planet->GetLayer()));
	}

	FAutoConsoleCommandWithWorldAndArgs RimSidesConsoleCommand(TEXT("hex.PlanetRimSides"),
		TEXT("hex.PlanetRimSides <Cell> [Rings]: logs, for the cells within Rings of a cell, the sides that take each kind of rim of the chasms and why a side takes none"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RimSidesCommand));

	FAutoConsoleCommandWithWorldAndArgs MemoryConsoleCommand(TEXT("hex.PlanetMemory"),
		TEXT("hex.PlanetMemory [Delay] [quit]: logs the memory of the surface of the planet, its triangles, its copies on the CPU and the GPU and its ray tracing geometry, with the times of five seconds of frames, Delay seconds after the planet is built; with quit it quits 15 s later"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MemoryCommand));
}

/** What a rebuild of changed levels reads, all of it copied when it starts, and what it makes */
struct AHexPlanet::FLevelRebuild
{
	TArray<int8> OldLevels;
	TArray<int32> OldMassifSteps;
	TArray<int8> Levels;
	HexPlanetSurface::FReliefOverrides OldOverrides;
	HexPlanetSurface::FReliefOverrides Overrides;
	/** The nets of the rivers shown and to build, HexPlanetRivers::BuildNet, and the line of the new one */
	TArray<uint16> OldRivers;
	TArray<uint16> Rivers;
	TSharedPtr<const HexPlanetRivers::FCurve> RiverCurve;
	TArray<uint8> OldFrozen;
	TArray<uint8> Frozen;
	/** True to build every chunk, as the shape of the rivers changed */
	bool bAll = false;
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

/** The pieces of chunks near the camera that a stream builds in the background, from copies of the state of the surface shown */
struct AHexPlanet::FChunkStream
{
	/** The chunks, ascending, and how many times a build had shown each when the stream started */
	TArray<int32> Chunks;
	TArray<uint32> Generations;

	/** Two sections a chunk, in the order of Chunks, ready for the GPU */
	TArray<FHexPlanetMeshSectionRef> Pieces;

	/** The sections themselves, kept only while the wireframe draws the whole surface, for its copies */
	TArray<HexPlanetSurface::FSection> WireSections;

	double StartTime = 0.0;
	double BuildMs = 0.0;
};

/** What the underground reads or generates in the background once the surface is built: its grid, of the same frequency, and its levels,
 *  biomes and entrances (HexPlanetUnderground::Load) */
struct AHexPlanet::FUndergroundData
{
	FHexSphereGrid Grid;
	HexPlanetUnderground::FLoaded Loaded;
	double StartTime = 0.0;
	double Ms = 0.0;
};

AHexPlanet::AHexPlanet()
{
	PrimaryActorTick.bCanEverTick = true;

	// after the camera has moved, so the water cap is under it in the same frame
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Surface"));
	RootComponent = Surface;

	Objects = CreateDefaultSubobject<UHexPlanetObjects>(TEXT("Objects"));
	Rivers = CreateDefaultSubobject<UHexPlanetRivers>(TEXT("Rivers"));
	Objects->SetupAttachment(Surface);

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

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Surface);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = SLS_SpecifiedCubemap;
	// the planet itself is below every point, so nothing of the sky is cut off by a hemisphere of the world
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->CubemapResolution = 16;

	HazeVolume = CreateDefaultSubobject<UPostProcessComponent>(TEXT("HazeVolume"));
	HazeVolume->SetupAttachment(Surface);
	HazeVolume->bUnbound = true;
}

void AHexPlanet::CreateHaze()
{
	UMaterialInterface* Base = HazeMaterialAsset.LoadSynchronous();
	if (!Base)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: haze material %s is missing, run Tools/create_planet_assets.py"), *HazeMaterialAsset.ToString());
		return;
	}

	HazeMaterial = UMaterialInstanceDynamic::Create(Base, this);
	SetCloudParameters(HazeMaterial);
	SetHeroShadeParameters(HazeMaterial);
	HazeVolume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, HazeMaterial));
}

void AHexPlanet::CreateFog()
{
	UMaterialInterface* Base = FogMaterialAsset.LoadSynchronous();
	if (!Base)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: fog material %s is missing, run Tools/create_planet_assets.py"), *FogMaterialAsset.ToString());
		return;
	}

	// the Fog values of the config, measured from the top of the passages, the level of 0
	HazeMaterial = UMaterialInstanceDynamic::Create(Base, this);
	HexMaterialConfig::Apply(HazeMaterial, Settings);
	HazeMaterial->SetScalarParameterValue(TEXT("PlanetRadius"), float(Radius));
	HazeVolume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, HazeMaterial));
}

void AHexPlanet::CreateSkyLight()
{
	const int32 Size = 4;
	SkyCubemap = UTextureCube::CreateTransient(Size, Size, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTextureCube::StaticClass(), TEXT("PlanetSky")));
	SkyCubemap->SRGB = false;
	SkyCubemap->NeverStream = true;
	FTexture2DMipMap& Mip = SkyCubemap->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Index = 0; Index < 6 * Size * Size; ++Index)
	{
		Pixels[Index] = FColor::White;
	}
	Mip.BulkData.Unlock();
	SkyCubemap->UpdateResource();

	SkyLight->SetCubemap(SkyCubemap);
	SkyLight->SetLightColor(Settings->SkyLightColor);
	ApplySkyLightIntensity();
	SkyLight->RecaptureSky();
}

bool AHexPlanet::IsLightForced()
{
	return CVarHexPlanetForcedLight.GetValueOnGameThread() != 0;
}

void AHexPlanet::SetLightForced(bool bForced)
{
	CVarHexPlanetForcedLight->Set(bForced ? 1 : 0, ECVF_SetByConsole);
}

void AHexPlanet::ApplySkyLightIntensity()
{
	bAppliedForcedLight = IsLightForced();
	// the forced light never dims it, so 0 keeps the light of the layer
	SkyLight->SetIntensity(bAppliedForcedLight ? FMath::Max(Settings->ForcedSkyLightIntensity, Settings->SkyLightIntensity) : Settings->SkyLightIntensity);
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

	// the underground takes its folder and manifest from the surface that spawned it, reads its data once the surface is built and builds its
	// meshes when it is first shown, so it shows no loading screen and never holds up the start of the surface
	if (PlanetLayer == EHexPlanetLayer::Underground)
	{
		Settings = PlanetConfig.LoadSynchronous();
		if (!Settings)
		{
			// without its config the underground would have the sea and the sky of the surface, so it takes a copy of that with them off
			const AHexPlanet* SurfacePlanet = Cast<AHexPlanet>(GetOwner());
			UE_LOG(LogHexHero, Warning, TEXT("Planet: underground config %s is missing, run Tools/create_planet_assets.py; the underground takes the config of the surface without its sea and sky"),
				*PlanetConfig.ToString());
			Settings = DuplicateObject<UHexPlanetConfig>(SurfacePlanet && SurfacePlanet->Settings ? SurfacePlanet->Settings.Get() : GetDefault<UHexPlanetConfig>(), this);
			Settings->bSea = Settings->bClouds = Settings->bHaze = Settings->bAuroras = Settings->bRivers = Settings->bGenerateObjects = false;
		}
		// DA_HexUnderground began as a copy of DA_HexPlanet, and its cells must stay as large, as it shares the kit of the surface, scaled to
		// that size, and cell i lies under cell i; one tuned apart takes the size of the surface in a copy, so the asset stays as it is
		const AHexPlanet* SurfacePlanet = Cast<AHexPlanet>(GetOwner());
		if (SurfacePlanet && SurfacePlanet->Settings && !FMath::IsNearlyEqual(SurfacePlanet->Settings->HexRadius, Settings->HexRadius))
		{
			UE_LOG(LogHexHero, Error, TEXT("Planet: HexRadius of %s is %.2f, but %.2f on the surface, whose kit the underground shares, so it takes that of the surface; set it in the asset"),
				*PlanetConfig.ToString(), Settings->HexRadius, SurfacePlanet->Settings->HexRadius);
			Settings = DuplicateObject<UHexPlanetConfig>(Settings.Get(), this);
			Settings->HexRadius = SurfacePlanet->Settings->HexRadius;
		}
		LoadedWaterConfig = LoadConfig(WaterConfig, TEXT("water"));
		LoadedCloudConfig = LoadConfig(CloudConfig, TEXT("cloud"));
		bPresented = false;
		SetActorHiddenInGame(true);
		LoadBiomeConfigs();
		if (UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld()))
		{
			LayerSet->Register(this);
		}
		return;
	}

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

	if (UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld()))
	{
		LayerSet->Register(this);
	}
	if (CVarHexPlanetSpawnUnderground.GetValueOnGameThread())
	{
		SpawnUnderground();
	}

	ShowLoadingScreen();
}

void AHexPlanet::SpawnUnderground()
{
	UWorld* World = GetWorld();
	if (!World || GetLayerPlanet(EHexPlanetLayer::Underground))
	{
		return;
	}

	// set before it begins to play, as its BeginPlay reads them
	AHexPlanet* Underground = World->SpawnActorDeferred<AHexPlanet>(AHexPlanet::StaticClass(), FTransform::Identity, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Underground)
	{
		UE_LOG(LogHexHero, Error, TEXT("Planet: could not spawn the underground"));
		return;
	}
	Underground->PlanetLayer = EHexPlanetLayer::Underground;
	Underground->PlanetConfig = UndergroundConfig;
	Underground->WaterConfig = WaterConfig;
	Underground->CloudConfig = CloudConfig;
	Underground->KitConfig = KitConfig;
	Underground->WireframeMaterialAsset = WireframeMaterialAsset;
	Underground->FogMaterialAsset = FogMaterialAsset;
	Underground->WallMaterialAsset = WallMaterialAsset;
	Underground->WallObjectMaterialAsset = WallObjectMaterialAsset;
	Underground->ParameterCollection = ParameterCollection;
	// its own material once the script makes it, else the one of the surface, so it shows before the look of the underground exists
	Underground->Material = Material;
	if (!UndergroundMaterial.IsNull() && FPackageName::DoesPackageExist(UndergroundMaterial.ToSoftObjectPath().GetLongPackageName()))
	{
		Underground->Material = UndergroundMaterial;
	}
	else
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: %s is not there yet, the underground draws %s"), *UndergroundMaterial.ToString(), *Material.ToString());
	}
	// the same planet, whose name is the folder
	Underground->PlanetFolder = PlanetFolder;
	Underground->Manifest = Manifest;
	Underground->bPresented = false;
	Underground->SetActorHiddenInGame(true);
	Underground->FinishSpawning(FTransform::Identity);
}

void AHexPlanet::ShowLoadingScreen()
{
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport)
	{
		return;
	}

	const FString Name = !Manifest.Name.IsEmpty() ? Manifest.Name : !PlanetFolder.IsEmpty() ? PlanetFolder : FString(TEXT("New planet"));
	LoadingScreen = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(Settings->LoadingBackgroundColor)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::Format(NSLOCTEXT("HexPlanet", "LoadingPlanet", "Loading {0}..."), FText::FromString(Name)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 28))
				.ColorAndOpacity(Settings->LoadingTextColor)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(NSLOCTEXT("HexPlanet", "LoadingCells", "{0} cells: surface, forests, sea and sky"), FText::AsNumber(FHexSphereGrid::CountCells(Manifest.Frequency))))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
				.ColorAndOpacity(Settings->LoadingTextColor * FLinearColor(1.0f, 1.0f, 1.0f, 0.6f))
			]
		];
	// over the map menu and every other widget of the game
	Viewport->AddViewportWidgetContent(LoadingScreen.ToSharedRef(), 1000);
}

void AHexPlanet::HideLoadingScreen()
{
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (Viewport && LoadingScreen.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(LoadingScreen.ToSharedRef());
	}
	LoadingScreen.Reset();
}

void AHexPlanet::BuildPlanet()
{
	const double StartTime = FPlatformTime::Seconds();
	// the underground built its grid with its data in the background
	const bool bSurface = PlanetLayer == EHexPlanetLayer::Surface;
	if (bSurface)
	{
		Grid = FHexSphereGrid::Build(Manifest.Frequency, Settings->ChunkSteps);
	}
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

	// what a saved planet lacks is generated and written at once, so the planet stays as it is when the generation changes later; the
	// underground read its levels and biomes in the background, and wrote what it generated (HexPlanetUnderground::Load)
	const bool bHadLevels = !bSurface || (IsSaved() && HexPlanetFile::HasLevels(PlanetFolder));
	const bool bHadBiomes = !bSurface || (IsSaved() && HexPlanetFile::HasBiomes(PlanetFolder));

	if (bSurface)
	{
		LoadLevels();
		LoadBiomeConfigs();
		LoadBiomes();
	}
	LoadRelief();
	// the objects are placed on the surface as it is built, so they are read first, and they keep out of the rivers
	// the values of the layers, which the materials of the surface and of the walls of the underground take (UpdateWalls)
	LayerParamsTexture = GetDefault<UHexMapSettings>()->LoadTerrainLayers()->CreateParamsTexture();
	Rivers->Init(this, Settings);
	Objects->Init(this, Settings);
	// nothing stands on an entrance, once the underground has its data, which is after the surface is built
	UpdateClearCells();
	// the objects of an underground keep off its exits, generated from the entrances in memory, so while those differ from entrances.json they wait
	// for the save, which writes both; leaving without it generates them again from the saved entrances
	const bool bObjectsWait = PlanetLayer == EHexPlanetLayer::Underground && bEntrancesEdited && Objects->WasGenerated();
	if (bObjectsWait)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: the generated objects of the underground are written with the next save, as its entrances are not saved"));
	}
	WriteGeneratedFiles(!bHadLevels, !bHadBiomes && !BiomeConfigs.IsEmpty(), Objects->WasGenerated() && !Objects->GetSets().IsEmpty() && !bObjectsWait, Rivers->WasGenerated());
	CreateCellMarks();
	CreateBiomeTexture();
	// before the materials of the surface are made, which take the texture
	UpdateCanopy();

	// a component per chunk, so what lies behind the planet or off the screen is culled, and a rebuild of changed levels recreates the render
	// state of its chunks alone
	for (int32 Chunk = 0; Chunk < Grid.GetNumChunks(); ++Chunk)
	{
		UHexPlanetMeshComponent* ChunkMesh = NewObject<UHexPlanetMeshComponent>(this, *FString::Printf(TEXT("SurfaceChunk%d"), Chunk));
		ChunkMesh->SetupAttachment(Surface);
		ChunkMesh->RegisterComponent();
		SurfaceChunks.Add(ChunkMesh);

		// far away the chunk draws its coarse mesh, which UpdateChunkLod shows and hides
		if (Settings->CoarseDistance > 0.0f)
		{
			UHexPlanetMeshComponent* CoarseMesh = NewObject<UHexPlanetMeshComponent>(this, *FString::Printf(TEXT("CoarseChunk%d"), Chunk));
			CoarseMesh->SetupAttachment(Surface);
			CoarseMesh->SetVisibility(false);
			CoarseMesh->RegisterComponent();
			CoarseChunks.Add(CoarseMesh);
		}
	}
	ChunkPieces.Init(0, Grid.GetNumChunks());
	ChunkBounds.Init(FBoxSphereBounds(ForceInit), Grid.GetNumChunks());
	ChunkGenerations.Init(0, Grid.GetNumChunks());

	BuildSurface();
	const double MeshTime = FPlatformTime::Seconds();
	// a layer without the sea and the sky, the underground, keeps their components empty and hidden, so they are not in the scene
	if (Settings->bSea)
	{
		BuildSeaField();
		BuildWater();
		BuildWaterGrid();
	}
	else
	{
		CreateEmptySeaField();
		Water->SetVisibility(false);
		WaterGrid->SetVisibility(false);
	}
	if (Settings->bClouds)
	{
		BuildClouds();
	}
	else
	{
		Clouds->SetVisibility(false);
		SkyClouds->SetVisibility(false);
	}
	LoadedParameterCollection = ParameterCollection.LoadSynchronous();
	if (Settings->bAuroras)
	{
		BuildAuroras();
	}

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
	CreateSkyLight();
	if (Settings->bHaze)
	{
		CreateHaze();
	}
	else if (Settings->bFog)
	{
		CreateFog();
	}
	if (Settings->bMotes && !Motes)
	{
		Motes = NewObject<UHexPlanetMotes>(this, TEXT("Motes"));
		Motes->SetupAttachment(Surface);
		Motes->RegisterComponent();
		Motes->Init(this, Settings);
	}
	if (Settings->bHeroLight && !HeroLight)
	{
		CreateHeroLight();
	}

	const FString Name = (PlanetFolder.IsEmpty() ? FString(TEXT("(not saved)")) : PlanetFolder) + (bSurface ? TEXT("") : TEXT(" underground"));
	UE_LOG(LogHexHero, Log, TEXT("Planet %s: %d cells, radius %.0f, hex area from %.3f to %.3f of the mean (x%.3f), worst sides x%.3f, grid %.2f s, mesh %.2f s, sea field %.2f s"),
		*Name, Grid.GetCells().Num(), Radius, SmallestArea / MeanCellArea, LargestArea / MeanCellArea, LargestArea / SmallestArea, WorstSides,
		GridTime - StartTime, MeshTime - GridTime, FPlatformTime::Seconds() - MeshTime);
	UE_LOG(LogHexHero, Log, TEXT("Planet %s built in %.2f s %s"), *Name, FPlatformTime::Seconds() - StartTime, bSurface ? TEXT("behind the loading screen") : TEXT("as it is first shown"));

	bBuilt = true;
	// the markers of the entrances, once both layers have their data; the underground places those of the surface when it gets its own
	UpdateMarkers();
	if (bSurface)
	{
		HideLoadingScreen();
	}
}

void AHexPlanet::CreateEmptySeaField()
{
	// a texel of nothing: no depth, no ice and the distance to the coast at its lowest, deep under land, so nothing of the shore shows
	const auto MakeCube = [](const TCHAR* BaseName)
	{
		UTextureCube* Cube = UTextureCube::CreateTransient(1, 1, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTextureCube::StaticClass(), BaseName));
		Cube->SRGB = false;
		Cube->NeverStream = true;
		FTexture2DMipMap& Mip = Cube->GetPlatformData()->Mips[0];
		FMemory::Memzero(Mip.BulkData.Lock(LOCK_READ_WRITE), 6 * sizeof(FColor));
		Mip.BulkData.Unlock();
		Cube->UpdateResource();
		return Cube;
	};
	SeaField = MakeCube(TEXT("PlanetEmptySeaField"));
	ShoreField = MakeCube(TEXT("PlanetEmptyShoreField"));
	SetShoreField(SurfaceMaterial);
	SetShoreField(LodNearMaterial);
	SetShoreField(LodFarMaterial);
	SetShoreField(SurfaceCutMaterial);
}

void AHexPlanet::StartUndergroundData()
{
	const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
	if (PlanetLayer != EHexPlanetLayer::Underground || DataTask.IsValid() || bDataLoaded || !SurfacePlanet || !SurfacePlanet->IsBuilt())
	{
		return;
	}

	// the task reads only its copies, and writes the files it generated in place of those that were missing; a planet not saved keeps them in memory
	UndergroundData = MakeShared<FUndergroundData>();
	UndergroundData->StartTime = FPlatformTime::Seconds();
	DataTask = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Work = UndergroundData, Frequency = Manifest.Frequency, ChunkSteps = Settings->ChunkSteps, Folder = PlanetFolder, SurfaceLevels = SurfacePlanet->GetLevels(),
			PlanetManifest = Manifest, Letters = BiomeLetters]()
		{
			Work->Grid = FHexSphereGrid::Build(Frequency, ChunkSteps);
			HexPlanetUnderground::Load(Folder, Work->Grid, SurfaceLevels, PlanetManifest, Letters, HexPlanetUnderground::FLoadOptions(), Work->Loaded);
			Work->Ms = (FPlatformTime::Seconds() - Work->StartTime) * 1000.0;
		});
}

void AHexPlanet::FinishUndergroundData()
{
	if (bDataLoaded || !DataTask.IsValid())
	{
		return;
	}
	const double WaitStart = FPlatformTime::Seconds();
	DataTask.Wait();
	DataTask = UE::Tasks::FTask();
	const double WaitMs = (FPlatformTime::Seconds() - WaitStart) * 1000.0;

	FUndergroundData& Work = *UndergroundData;
	Grid = MoveTemp(Work.Grid);
	Levels = MoveTemp(Work.Loaded.Levels);
	CellBiomes = MoveTemp(Work.Loaded.Biomes);
	if (CellBiomes.Num() != Levels.Num())
	{
		CellBiomes.Init(0, Levels.Num());
	}
	Entrances = MoveTemp(Work.Loaded.Entrances);
	UndergroundReport = MoveTemp(Work.Loaded.Report);
	const double BackgroundMs = Work.Ms;
	UndergroundData.Reset();
	bDataLoaded = true;
	// read with the data, as a save writes every layer that has it, even one never shown, and would delete relief.json and walls.json without it
	LoadRelief();
	LoadWallHeights();

	// a biome its level does not allow, as after levels.txt was edited by hand, takes a fitting one
	int32 Fitted = 0;
	for (int32 Cell = 0; Cell < Levels.Num() && !BiomeConfigs.IsEmpty(); ++Cell)
	{
		if (!IsBiomeAllowed(CellBiomes[Cell], Levels[Cell]))
		{
			CellBiomes[Cell] = uint8(FindFittingBiome(Cell));
			++Fitted;
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: the underground has its data, %d cells, %d entrances, %s, in %.0f ms in the background%s%s"), Levels.Num(), Entrances.Num(),
		UndergroundReport.IsGood() ? TEXT("good") : TEXT("with problems"), BackgroundMs, WaitMs >= 1.0 ? *FString::Printf(TEXT(", waited %.0f ms for it"), WaitMs) : TEXT(""),
		Fitted > 0 ? *FString::Printf(TEXT(", %d cells took a biome their level allows"), Fitted) : TEXT(""));

	// the entrances are known now, so the surface marks them and takes its objects off them
	if (AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface))
	{
		SurfacePlanet->UpdateMarkers();
	}
	UpdateClearCells();
}

void AHexPlanet::UpdateClearCells()
{
	// every entrance, also one that cannot be used, as its marker shows
	TArray<int32> Cells;
	for (const FHexEntrance& Entrance : GetEntrances())
	{
		Cells.Add(Entrance.Cell);
	}
	for (const EHexPlanetLayer Layer : {EHexPlanetLayer::Surface, EHexPlanetLayer::Underground})
	{
		if (AHexPlanet* Planet = GetLayerPlanet(Layer); Planet && Planet->Objects)
		{
			// and the boulders at the foot of mountains on those cells, placed again only when the cleared cells changed
			if (Planet->Objects->SetClearCells(Cells) && Planet->bBuilt)
			{
				Planet->UpdateBoulders();
			}
		}
	}
}

void AHexPlanet::SetPresented(bool bInPresented)
{
	if (bPresented == bInPresented && (bBuilt || !bInPresented))
	{
		return;
	}
	bPresented = bInPresented;
	const double StartTime = FPlatformTime::Seconds();

	// a hidden actor takes its primitives and its sky light out of the scene, and showing it again puts them back
	SetActorHiddenInGame(!bPresented);

	if (bPresented)
	{
		// the underground builds its meshes the first time it is shown, behind the black screen of the trip
		if (!bBuilt)
		{
			StartUndergroundData();
			FinishUndergroundData();
			if (bDataLoaded)
			{
				BuildPlanet();
			}
		}
		for (UNiagaraComponent* Aurora : Auroras)
		{
			if (Aurora)
			{
				Aurora->Activate(true);
			}
		}
	}
	else
	{
		// what only the camera above it needs goes: the pieces of its chunks, which their coarse meshes stand in for, and the build of the start;
		// a prefetch for a jump on it that a trip cut short goes too, so it streams nothing more while hidden
		PrefetchCamera.Reset();
		DropWallWholes();
		int32 Dropped = 0;
		for (int32 Chunk = 0; Chunk < ChunkPieces.Num(); ++Chunk)
		{
			if (ChunkPieces[Chunk] == 2)
			{
				DropChunkPieces(Chunk);
				++Dropped;
			}
		}
		if (StartupSurface)
		{
			if (TriangleSource == StartupSurface.Get())
			{
				TriangleSource = nullptr;
			}
			StartupSurface.Reset();
			FMemory::Trim(true);
		}
		for (UNiagaraComponent* Aurora : Auroras)
		{
			if (Aurora)
			{
				Aurora->Deactivate();
			}
		}
		if (bBuilt)
		{
			// the outline of the cursor or the brush goes, so the layer does not show it when it comes back; the links and the water keep theirs
			SetHoveredCell(INDEX_NONE, 0);
			UE_LOG(LogHexHero, Log, TEXT("Planet: hid the %s and freed the pieces of %d chunks"), GetPlanetLayerName(PlanetLayer), Dropped);
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: the %s is %s, in %.0f ms"), GetPlanetLayerName(PlanetLayer), bPresented ? TEXT("shown") : TEXT("hidden"),
		(FPlatformTime::Seconds() - StartTime) * 1000.0);
}

AHexPlanet* AHexPlanet::GetLayerPlanet(EHexPlanetLayer InLayer) const
{
	if (InLayer == PlanetLayer)
	{
		return const_cast<AHexPlanet*>(this);
	}
	const UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld());
	return LayerSet ? LayerSet->GetPlanet(InLayer) : nullptr;
}

TArray<AHexPlanet*, TInlineAllocator<2>> AHexPlanet::GetLoadedLayers() const
{
	TArray<AHexPlanet*, TInlineAllocator<2>> Planets{const_cast<AHexPlanet*>(this)};
	const EHexPlanetLayer Other = PlanetLayer == EHexPlanetLayer::Surface ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface;
	if (AHexPlanet* OtherPlanet = GetLayerPlanet(Other); OtherPlanet && OtherPlanet->IsDataLoaded())
	{
		Planets.Add(OtherPlanet);
	}
	return Planets;
}

const TArray<FHexEntrance>& AHexPlanet::GetEntrances() const
{
	static const TArray<FHexEntrance> None;
	const AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground);
	return Underground && Underground->bDataLoaded ? Underground->Entrances : None;
}

int32 AHexPlanet::FindEntrance(int32 Cell) const
{
	return GetEntrances().IndexOfByPredicate([Cell](const FHexEntrance& Each) { return Each.Cell == Cell; });
}

HexPlanetUnderground::EEntranceProblem AHexPlanet::GetEntranceProblem(int32 Index) const
{
	const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
	const AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground);
	if (!SurfacePlanet || !Underground || !Underground->bDataLoaded || !SurfacePlanet->IsDataLoaded())
	{
		return HexPlanetUnderground::EEntranceProblem::NoCell;
	}
	return HexPlanetUnderground::GetEntranceProblem(Underground->Grid, SurfacePlanet->Levels, Underground->Levels, Underground->Entrances, Index);
}

const HexPlanetUnderground::FReport* AHexPlanet::GetUndergroundReport() const
{
	const AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground);
	if (!Underground || !Underground->bDataLoaded)
	{
		return nullptr;
	}

	// checked again once after the levels of either layer changed, when it is first read
	const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
	if (Underground->bUndergroundReportDirty && SurfacePlanet && SurfacePlanet->IsDataLoaded())
	{
		Underground->bUndergroundReportDirty = false;
		Underground->UndergroundReport = HexPlanetUnderground::Check(Underground->Grid, SurfacePlanet->Levels, Underground->Levels, Underground->Entrances);
	}
	return &Underground->UndergroundReport;
}

void AHexPlanet::MarkUndergroundReportDirty()
{
	if (AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground))
	{
		Underground->bUndergroundReportDirty = true;
	}
}

void AHexPlanet::SetUndergroundSeedOffset(int32 SeedOffset)
{
	for (const EHexPlanetLayer Layer : {EHexPlanetLayer::Surface, EHexPlanetLayer::Underground})
	{
		if (AHexPlanet* Planet = GetLayerPlanet(Layer); Planet && Planet->Manifest.Underground.SeedOffset != SeedOffset)
		{
			Planet->Manifest.Underground.SeedOffset = SeedOffset;
			Planet->bManifestEdited = true;
		}
	}
}

void AHexPlanet::SetEntrances(const TArray<FHexEntrance>& NewEntrances, bool bByStep)
{
	AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground);
	if (!Underground || !Underground->bDataLoaded || Underground->Entrances == NewEntrances)
	{
		return;
	}
	Underground->Entrances = NewEntrances;
	Underground->bEntrancesEdited = true;
	Underground->bEditedOutsideSteps |= !bByStep;
	MarkUndergroundReportDirty();

	// an entrance stands on both layers, as its exit is the same cell below
	for (AHexPlanet* Planet : GetLoadedLayers())
	{
		Planet->UpdateMarkers();
		Planet->UpdateLinkMarks();
	}
	UpdateClearCells();
}

void AHexPlanet::EnsureDataLoaded()
{
	if (PlanetLayer == EHexPlanetLayer::Underground && !bDataLoaded)
	{
		StartUndergroundData();
		FinishUndergroundData();
	}
}

void AHexPlanet::SetLinksShown(bool bShown)
{
	if (PlanetLayer != EHexPlanetLayer::Underground || bShown == bLinksShown)
	{
		return;
	}
	bLinksShown = bShown;

	// the networks and the problems take the colors of the surface, as their chips and markers do (UpdateMarkers)
	if (bShown)
	{
		const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
		const UHexPlanetConfig* Colors = SurfacePlanet && SurfacePlanet->Settings ? SurfacePlanet->Settings.Get() : Settings.Get();
		for (UMaterialInstanceDynamic* Instance : {SurfaceMaterial.Get(), SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get()})
		{
			if (!Instance || !Colors)
			{
				continue;
			}
			for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
			{
				Instance->SetVectorParameterValue(*FString::Printf(TEXT("NetworkColor%d"), Network), Colors->GetNetworkColor(Network));
			}
			Instance->SetVectorParameterValue(TEXT("ProblemColor"), Colors->ProblemColor);
			Instance->SetVectorParameterValue(TEXT("LinksOrphanColor"), Colors->LinksOrphanColor);
		}
	}
	UpdateLinkMarks();
}

void AHexPlanet::UpdateLinkMarks()
{
	if (PlanetLayer != EHexPlanetLayer::Underground || MarkPixels.IsEmpty())
	{
		return;
	}

	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<uint8> Marks;
	Marks.Init(0, MarkPixels.Num());
	const HexPlanetUnderground::FReport* Report = bLinksShown ? GetUndergroundReport() : nullptr;
	int32 NumProblems = 0;
	if (Report && Report->CellNetworks.Num() == Cells.Num())
	{
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			const int8 Network = Report->CellNetworks[Cell];
			Marks[Cell] = Network >= 0 ? uint8(Network + 1) : Network == HexPlanetUnderground::MergedNetwork ? LinkProblem
				: HexPlanetUnderground::IsPassage(Levels[Cell]) ? LinkOrphan : 0;
		}

		// a network in more than one piece keeps its color on the largest, and the others are what is wrong
		for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
		{
			if (Report->Networks[Network].Components <= 1)
			{
				continue;
			}
			TArray<TArray<int32>> Pieces;
			TBitArray<> Seen(false, Cells.Num());
			for (int32 First = 0; First < Cells.Num(); ++First)
			{
				if (Report->CellNetworks[First] != Network || Seen[First])
				{
					continue;
				}
				TArray<int32>& Piece = Pieces.AddDefaulted_GetRef();
				Piece.Add(First);
				Seen[First] = true;
				for (int32 Index = 0; Index < Piece.Num(); ++Index)
				{
					for (const int32 Neighbor : Cells[Piece[Index]].Neighbors)
					{
						if (Report->CellNetworks[Neighbor] == Network && !Seen[Neighbor])
						{
							Seen[Neighbor] = true;
							Piece.Add(Neighbor);
						}
					}
				}
			}
			Pieces.StableSort([](const TArray<int32>& A, const TArray<int32>& B) { return A.Num() > B.Num(); });
			for (int32 Index = 1; Index < Pieces.Num(); ++Index)
			{
				for (const int32 Cell : Pieces[Index])
				{
					Marks[Cell] = LinkProblem;
				}
			}
		}
		for (const int32 Cell : Report->Necks)
		{
			Marks[Cell] = LinkProblem;
		}
		for (const int32 Cell : Report->ExitsInVoid)
		{
			if (Cells.IsValidIndex(Cell))
			{
				Marks[Cell] = LinkProblem;
			}
		}
		for (const uint8 Mark : Marks)
		{
			NumProblems += Mark == LinkProblem ? 1 : 0;
		}
	}

	bool bChanged = false;
	for (int32 Cell = 0; Cell < MarkPixels.Num(); ++Cell)
	{
		if (MarkPixels[Cell].B != Marks[Cell])
		{
			MarkPixels[Cell].B = Marks[Cell];
			bChanged = true;
		}
	}
	if (bChanged)
	{
		UploadCellMarks();
	}
	if (Report)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: links of the underground shown, %d cells marked as problems: %s"), NumProblems, *Report->ToString());
	}
}

bool AHexPlanet::HasUnsavedLevels() const
{
	for (const AHexPlanet* Planet : GetLoadedLayers())
	{
		if (Planet->bLevelsEdited || Planet->bEntrancesEdited || Planet->bManifestEdited)
		{
			return true;
		}
	}
	return false;
}

bool AHexPlanet::HasUnsavedCells() const
{
	for (const AHexPlanet* Planet : GetLoadedLayers())
	{
		if (Planet->bLevelsEdited)
		{
			return true;
		}
	}
	return false;
}

void AHexPlanet::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RebuildTask.IsValid())
	{
		RebuildTask.Wait();
		RebuildTask = UE::Tasks::FTask();
	}
	if (StreamTask.IsValid())
	{
		StreamTask.Wait();
		StreamTask = UE::Tasks::FTask();
	}
	if (DataTask.IsValid())
	{
		DataTask.Wait();
		DataTask = UE::Tasks::FTask();
	}
	if (UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld()))
	{
		LayerSet->Unregister(this);
	}
	UndergroundData.Reset();
	Rebuild.Reset();
	Stream.Reset();
	StartupSurface.Reset();
	TriangleSource = nullptr;
	bLevelsPending = false;
	HideLoadingScreen();
	Super::EndPlay(EndPlayReason);
}

void AHexPlanet::SetCells(const TMap<int32, int32>& NewLevels, const TMap<int32, int32>& NewBiomes, const TMap<int32, float>& NewWallHeights)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	int32 NumChanged = 0;

	// the heights of the walls set by hand, a negative one going back to that of the style of the void, never below WallHeightMin, in
	// thousandths, as they are written
	bool bWallHeightsChanged = false;
	for (const TPair<int32, float>& Each : NewWallHeights)
	{
		if (!Cells.IsValidIndex(Each.Key) || !Settings)
		{
			continue;
		}
		if (Each.Value < 0.0f)
		{
			bWallHeightsChanged |= WallHeightOverrides.Remove(Each.Key) > 0;
			continue;
		}
		const float Height = Settings->ClampWallHeight(Each.Value);
		const float* Old = WallHeightOverrides.Find(Each.Key);
		if (!Old || *Old != Height)
		{
			WallHeightOverrides.Add(Each.Key, Height);
			bWallHeightsChanged = true;
		}
	}
	TArray<int32> LevelCells;
	TArray<int32> BiomeCells;

	for (const TPair<int32, int32>& Each : NewLevels)
	{
		if (!Cells.IsValidIndex(Each.Key))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: there is no cell %d to set the level of"), Each.Key);
			continue;
		}

		// the pentagons of the surface wait for set pieces of their own, and the mountain kit has no five-sided pieces
		int32 Level = FMath::Clamp(Each.Value, -2, 2);
		if (Cells[Each.Key].IsPentagon() && Level > 1 && PlanetLayer == EHexPlanetLayer::Surface)
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
	bool bVoidStyleChanged = false;
	const auto SetBiome = [&](int32 Cell, int32 Biome)
	{
		if (CellBiomes[Cell] != Biome && BiomeConfigs.IsValidIndex(Biome) && BiomeConfigs.IsValidIndex(CellBiomes[Cell]))
		{
			bFreezeChanged |= Levels[Cell] < 0 && BiomeConfigs[CellBiomes[Cell]]->bFrozen != BiomeConfigs[Biome]->bFrozen;
			bVoidStyleChanged |= Levels[Cell] < 0 && BiomeConfigs[CellBiomes[Cell]]->VoidStyle != BiomeConfigs[Biome]->VoidStyle;
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
		// a river freezes or thaws with the biomes beside it
		Rivers->RefreshTexture();
		// objects take the variant of their biome, and stand on the surface shown until a rebuild of the levels places them again
		Objects->RebuildCells(BiomeCells);
	}

	// the walls take the height of a void and are lit by its lava, so they follow its style and the height set for it at once; a change of the
	// levels builds them again with its rebuild, as does a rebuild that waits, which reads the heights and biomes of then
	if ((bVoidStyleChanged || bWallHeightsChanged) && NumChanged == 0 && !IsRebuilding() && Settings->bWalls && !Walls->Lines.IsEmpty())
	{
		TArray<int32> WallChunksChanged;
		UpdateWalls(WallChunksChanged);
		Objects->RebuildChunks(WallChunksChanged);
	}

	if (NumChanged > 0 || bFreezeChanged)
	{
		bLevelsPending = true;
		StartRebuild();
	}

	// the passages and entrances of the underground fit together by the levels of both layers
	if (NumChanged > 0)
	{
		MarkUndergroundReportDirty();
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
	Rebuild->OldRivers = TArray<uint16>(Rivers->GetShownNet());
	Rebuild->Rivers = Rivers->BuildNet(Levels);
	Rebuild->OldFrozen = ShownFrozen;
	Rebuild->Frozen = GetFrozenCells();
	Rebuild->Coasts = ChunkCoasts;
	Rebuild->bAll = bRebuildAllPending;
	bRebuildAllPending = false;
	Rebuild->StartTime = FPlatformTime::Seconds();

	RebuildTask = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Work = Rebuild, GridPtr = &Grid, Kit = SurfaceKit.ToSharedRef(), Params = SurfaceParams, SeaParams = FSeaParams::Make(*Settings, Radius), FixedPeak = MeanPeak,
			CurveParams = Rivers->GetCurveParams(), bSea = Settings->bSea]()
		{
			const double SurfaceStart = FPlatformTime::Seconds();
			Work->RiverCurve = HexPlanetRivers::BuildCurve(*GridPtr, Work->Levels, Work->Rivers, CurveParams);
			Work->MassifSteps = HexPlanetSurface::FindMassifSteps(*GridPtr, *Kit, Work->Levels);
			// a relief set by hand and a river change the cells they lie on as a level does
			TArray<int32> ChangedCells = HexPlanetSurface::FindChangedOverrides(Work->OldOverrides, Work->Overrides);
			ChangedCells.Append(HexPlanetRivers::FindChangedCells(Work->OldRivers, Work->Rivers));
			Work->Chunks = Work->bAll ? HexPlanetSurface::GetAllChunks(*GridPtr)
				: HexPlanetSurface::FindAffectedChunks(*GridPtr, Work->OldLevels, Work->Levels, Work->OldMassifSteps, Work->MassifSteps, ChangedCells);

			if (!Work->Chunks.IsEmpty())
			{
				Work->Surface = HexPlanetSurface::Build(*GridPtr, *Kit, Params, Work->Levels, Work->MassifSteps, Work->Chunks, FixedPeak, &Work->Overrides, Work->RiverCurve.Get());
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
			for (int32 Cell = 0; Cell < Work->Levels.Num() && !bSeaChanged && bSea; ++Cell)
			{
				bSeaChanged = Work->Levels[Cell] != Work->OldLevels[Cell] && (Work->Levels[Cell] < 0 || Work->OldLevels[Cell] < 0);
			}
			// a layer without the sea, the underground, has neither field nor grid lines of the sea
			const bool bIceChanged = bSea && Work->Frozen != Work->OldFrozen;

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

	// the objects placed again with the surface keep out of the rivers it shows; the line also changes with the coast at a mouth
	Rivers->SetShownNet(Rebuild->Rivers, Rebuild->RiverCurve);

	// what the surface is built from goes first, as whatever needs the ground of a cell that no build holds builds it again from that
	// (GetCellTriangles), such as the bridges below
	// the floes move with the ice, and with the coast, as a cell of frozen sea that became land keeps its frozen mark for the sea field
	const bool bFloesChanged = !Rebuild->SeaPixels.IsEmpty() || Rebuild->Rivers != Rebuild->OldRivers;
	ShownLevels = MoveTemp(Rebuild->Levels);
	ShownMassifSteps = MoveTemp(Rebuild->MassifSteps);
	ShownFrozen = MoveTemp(Rebuild->Frozen);
	ShownReliefOverrides = MoveTemp(Rebuild->Overrides);

	if (!Rebuild->Chunks.IsEmpty())
	{
		ApplySurface(Rebuild->Surface);
	}
	// the bridges stand on the ground of the valleys just shown
	Rivers->UpdateBridges();
	if (!Rebuild->SeaPixels.IsEmpty())
	{
		ApplySeaField(Rebuild->SeaPixels);
	}
	if (Rebuild->bWaterGridBuilt)
	{
		WaterGrid->CreateMeshSection_LinearColor(0, Rebuild->WaterGrid.Positions, Rebuild->WaterGrid.Triangles, Rebuild->WaterGrid.Normals, Rebuild->WaterGrid.UVs, {},
			Rebuild->WaterGrid.CellUVs, Rebuild->WaterGrid.LocalUVs, {}, {}, false);
	}

	if (bFloesChanged)
	{
		UpdateFloes();
	}

	// an entrance stands on the ground just shown, and a change of the levels of either layer may make it usable or not
	for (AHexPlanet* Planet : GetLoadedLayers())
	{
		Planet->UpdateMarkers();
		Planet->UpdateLinkMarks();
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

	// the build waits until the loading screen has been drawn, as it holds the game thread for a second or two; the underground reads its data
	// in the background once the surface is built, and builds its meshes when it is first shown (SetPresented)
	if (!bBuilt)
	{
		if (PlanetLayer == EHexPlanetLayer::Underground)
		{
			if (!bDataLoaded && !DataTask.IsValid())
			{
				StartUndergroundData();
			}
			else if (DataTask.IsValid() && DataTask.IsCompleted())
			{
				FinishUndergroundData();
			}
		}
		else if (++FramesBeforeBuild >= 3)
		{
			BuildPlanet();
		}
		return;
	}

	// a rebuild of changed levels that has ended is shown, and the changes made while it ran start the next
	if (RebuildTask.IsValid() && RebuildTask.IsCompleted())
	{
		RebuildTask = UE::Tasks::FTask();
		FinishRebuild();
		StartRebuild();
		OnLevelsRebuilt.Broadcast();
	}

	// and the pieces of chunks that came near the camera, after it, so a rebuild that made a chunk anew drops what the stream built of it
	if (StreamTask.IsValid() && StreamTask.IsCompleted())
	{
		StreamTask = UE::Tasks::FTask();
		FinishStream();
	}
	// and the whole walls of chunks near the camera
	if (WallStreamTask.IsValid() && WallStreamTask.IsCompleted())
	{
		WallStreamTask = UE::Tasks::FTask();
		FinishWallStream();
	}

	// a hidden layer streams only the pieces a trip to it asks for, without building any at once on the game thread, and does nothing else
	// of a frame, so a camera below the surface does not build its pieces
	if (!bPresented)
	{
		if (PrefetchCamera.IsSet())
		{
			UpdateChunkLod(PrefetchCamera.GetValue());
		}
		return;
	}

	Objects->SetShown(CVarHexPlanetObjects.GetValueOnGameThread(), CVarHexPlanetRims.GetValueOnGameThread());

	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();

	if (Motes)
	{
		Motes->SetShown(CVarHexPlanetMotes.GetValueOnGameThread());
		Motes->Update(PlayerController, DeltaTime);
	}

	// the light of the hero, for now over the focus of the camera where it stands in this frame (the view target, as the camera manager has the
	// frame before, so the light would trail the camera), and the fog of the underground clears around it
	if (const AActor* ViewTarget = PlayerController ? PlayerController->GetViewTarget() : nullptr)
	{
		HeroFocus = FindViewFocus(ViewTarget->GetActorLocation(), ViewTarget->GetActorForwardVector());
		bHasHeroFocus = true;
	}
	if (bHasHeroFocus && Settings->bHeroLight)
	{
		UpdateHeroLight(HeroFocus);
	}
	// and on the surface the land away from it lies in shade
	if (bHasHeroFocus && Settings->bHeroShade)
	{
		UpdateHeroShade(HeroFocus);
	}
	// the forced light of L, by the key or the console, lights the sky of the layer shown; the sun is the camera's, and the materials take it from
	// ForcedLight of MPC_HexPlanet below
	if (bAppliedForcedLight != IsLightForced())
	{
		ApplySkyLightIntensity();
		UE_LOG(LogHexHero, Log, TEXT("Planet: the light of the %s is %s"), GetPlanetLayerName(PlanetLayer), bAppliedForcedLight ? TEXT("forced") : TEXT("that of the game"));
	}

	// the dense middle of the cap lies under the camera, where the water is seen closest
	if (const AActor* ViewTarget = PlayerController && Settings->bSea ? PlayerController->GetViewTarget() : nullptr)
	{
		const FVector Below = ViewTarget->GetActorLocation().GetSafeNormal();

		if (!Below.IsNearlyZero())
		{
			Water->SetWorldRotation(FQuat::FindBetweenNormals(FVector::UpVector, Below));
			UpdateRiverMouths(FVector3d(Below));
		}
	}

	// the clouds below the camera hand over to the sky over CloudSkySwitchFade around CloudSkySwitch, as M_HexPlanetClouds fades them,
	// and a shell that is faded out is not drawn
	if (const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr)
	{
		const double CameraHeight = CameraManager->GetCameraLocation().Size() - Radius;
		const double Switch = LoadedCloudConfig->CloudSkySwitch;
		const double Fade = FMath::Max(LoadedCloudConfig->CloudSkySwitchFade, 1.0f);
		Clouds->SetVisibility(Settings->bClouds && CameraHeight > Switch - Fade);
		SkyClouds->SetVisibility(Settings->bClouds && CameraHeight < Switch + Fade);

		// the view target stands where the camera is in this frame, the camera manager where it was in the frame before, which after a jump
		// would draw a frame of the new place with the chunks of the old one
		const AActor* ViewTarget = PlayerController->GetViewTarget();
		UpdateChunkLod(ViewTarget ? ViewTarget->GetActorLocation() : CameraManager->GetCameraLocation());
		UpdateWallLod(ViewTarget ? ViewTarget->GetActorLocation() : CameraManager->GetCameraLocation());
	}

	if (Sun)
	{
		// the water and the clouds are lit by it, and the shadows of the clouds fall along it
		const FLinearColor SunDirection(-Sun->GetActorForwardVector());

		for (UMaterialInstanceDynamic* Instance : { WaterMaterial.Get(), WaterGridMaterial.Get(), CloudMaterial.Get(), SkyCloudMaterial.Get(), SurfaceMaterial.Get(), SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get(), HazeMaterial.Get() })
		{
			if (Instance)
			{
				Instance->SetVectorParameterValue(TEXT("SunDirection"), SunDirection);
			}
		}

		// and any material, such as those of the auroras; the light the ground gives by itself rises to ForcedAmbient under the forced light
		if (UMaterialParameterCollectionInstance* Parameters = LoadedParameterCollection ? GetWorld()->GetParameterCollectionInstance(LoadedParameterCollection) : nullptr)
		{
			Parameters->SetVectorParameterValue(TEXT("SunDirection"), SunDirection);
			Parameters->SetScalarParameterValue(TEXT("PlanetRadius"), Radius);
			Parameters->SetScalarParameterValue(TEXT("ForcedLight"), IsLightForced() ? 1.0f : 0.0f);
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
	LodCamera = CameraLocation;
	bHasLodCamera = true;

	// the console can move the band to try other distances while the game runs
	double Near, Far;
	GetLodBand(Near, Far);

	for (UMaterialInstanceDynamic* Instance : { LodNearMaterial.Get(), LodFarMaterial.Get() })
	{
		if (Instance)
		{
			Instance->SetVectorParameterValue(TEXT("LodCamera"), FLinearColor(CameraLocation));
			Instance->SetScalarParameterValue(TEXT("CoarseDistance"), float((Near + Far) * 0.5));
			Instance->SetScalarParameterValue(TEXT("CoarseBand"), float((Far - Near) * 0.5));
		}
	}
	// the underground shades a void as deep as its cliff where its coarse mesh draws a shallow cone (M_HexUnderground, AbyssFarCliff), by the
	// same band, so its other materials take the band of the console too
	if (PlanetLayer == EHexPlanetLayer::Underground)
	{
		for (UMaterialInstanceDynamic* Instance : { SurfaceMaterial.Get(), SurfaceCutMaterial.Get() })
		{
			if (Instance)
			{
				Instance->SetScalarParameterValue(TEXT("CoarseDistance"), float((Near + Far) * 0.5));
				Instance->SetScalarParameterValue(TEXT("CoarseBand"), float((Far - Near) * 0.5));
			}
		}
	}

	// the pieces are built ahead of the camera within PieceStreamAhead past the band and freed past PieceStreamKeep further, as from there
	// on a chunk draws its coarse mesh alone; the distances run to the bounds of its pieces, which stay while it has none
	const bool bStream = CVarHexPlanetStreamPieces.GetValueOnGameThread();
	const double Ahead = Far + Settings->PieceStreamAhead;
	const double Keep = Ahead + Settings->PieceStreamKeep;
	TArray<double> Distances;
	TArray<double> Reaches;
	Distances.SetNum(SurfaceChunks.Num());
	Reaches.SetNum(SurfaceChunks.Num());
	TArray<TPair<double, int32>> Wanted;
	int32 NumPrefetched = 0;

	for (int32 Chunk = 0; Chunk < SurfaceChunks.Num(); ++Chunk)
	{
		const FBoxSphereBounds& Bounds = ChunkBounds[Chunk];
		Reaches[Chunk] = FMath::Max(Bounds.SphereRadius, CoarseChunks[Chunk]->Bounds.SphereRadius);
		Distances[Chunk] = FVector::Distance(CameraLocation, Bounds.Origin);
		const double NearEdge = Distances[Chunk] - Reaches[Chunk];

		// a jump of the camera waits for the chunks that draw their pieces where it goes, which come first, all at once, and keep them
		const double PrefetchEdge = PrefetchCamera.IsSet() ? FVector::Distance(PrefetchCamera.GetValue(), Bounds.Origin) - Reaches[Chunk] : TNumericLimits<double>::Max();
		const bool bPrefetched = PrefetchEdge < Far;
		if (ChunkPieces[Chunk] == 0 && (!bStream || NearEdge < Ahead || bPrefetched))
		{
			Wanted.Emplace(bPrefetched ? PrefetchEdge - 1.0e9 : NearEdge, Chunk);
			NumPrefetched += bPrefetched ? 1 : 0;
		}
		else if (ChunkPieces[Chunk] == 2 && bStream && NearEdge > Keep && PrefetchEdge > Keep && !StartupSurface)
		{
			DropChunkPieces(Chunk);
		}
	}

	if (!Wanted.IsEmpty())
	{
		Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
		if (StartupSurface)
		{
			// in the first frames, while the camera finds its place, the build of the start still holds them, so they go to the GPU at once
			TArray<FHexPlanetMeshSectionRef> Prepared;
			Prepared.SetNum(Wanted.Num() * 2);
			ParallelFor(Prepared.Num(), [&](int32 Index)
			{
				const int32 Chunk = Wanted[Index / 2].Value;
				const int32 Slot = Algo::BinarySearch(StartupSurface->Chunks, Chunk);
				if (Slot != INDEX_NONE)
				{
					Prepared[Index] = PrepareChunkSection(StartupSurface->Sections[Slot * 2 + Index % 2], TEXT("SurfaceChunk"), Chunk);
				}
			});
			for (int32 Index = 0; Index < Wanted.Num(); ++Index)
			{
				SetChunkPieces(Wanted[Index].Value, MoveTemp(Prepared[Index * 2]), MoveTemp(Prepared[Index * 2 + 1]));
			}
		}
		else if (!StreamTask.IsValid())
		{
			const int32 Count = NumPrefetched > 0 ? NumPrefetched : FMath::Max(Settings->PieceStreamChunks, 1);
			TArray<int32> Chunks;
			for (int32 Index = 0; Index < Wanted.Num() && Index < Count; ++Index)
			{
				Chunks.Add(Wanted[Index].Value);
			}
			StartStream(MoveTemp(Chunks));
		}
	}

	// a chunk the camera came near without its pieces, after a jump that did not wait for them or a move faster than the stream, gets them
	// at once, as its coarse mesh next to the pieces of its neighbors would leave gaps; what a stream builds of it meanwhile is dropped. A hidden
	// layer that streams for a trip to it waits for the stream, as nothing of it is drawn
	if (!StartupSurface && bPresented)
	{
		TArray<int32> Urgent;
		for (int32 Chunk = 0; Chunk < SurfaceChunks.Num(); ++Chunk)
		{
			if (ChunkPieces[Chunk] != 2 && Distances[Chunk] - Reaches[Chunk] < Near)
			{
				Urgent.Add(Chunk);
			}
		}
		if (!Urgent.IsEmpty())
		{
			BuildPiecesNow(MoveTemp(Urgent));
		}
	}
	if (StartupSurface && --StartupFramesLeft <= 0)
	{
		if (TriangleSource == StartupSurface.Get())
		{
			TriangleSource = nullptr;
		}
		StartupSurface.Reset();

		// the build of every chunk took about a gigabyte, which the allocator may keep for reuse, so it is asked to give back what it can, as
		// the engine does after loading a map; mimalloc of the editor gives back only what the game thread freed, a packaged game more
		FMemory::Trim(true);
		UE_LOG(LogHexHero, Log, TEXT("Planet: freed the build of the start, %d chunks took their pieces from it"),
			ChunkPieces.FilterByPredicate([](uint8 State) { return State == 2; }).Num());
	}

	for (int32 Chunk = 0; Chunk < SurfaceChunks.Num(); ++Chunk)
	{
		// the pieces where all of the chunk is nearer than the band, the coarse mesh where all of it is further, and both where it reaches into it,
		// with the LOD materials, which keep the pixels of each by their own distance across the band. A chunk whose pieces are not on the GPU
		// yet draws its coarse mesh alone
		const double Reach = Reaches[Chunk];
		const double Distance = Distances[Chunk];
		const uint8 State = ChunkPieces[Chunk] != 2 ? 2 : Distance + Reach < Near ? 0 : Distance - Reach > Far ? 2 : 1;

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

void AHexPlanet::GetLodBand(double& OutNear, double& OutFar) const
{
	const float OverrideDistance = CVarHexPlanetCoarseDistance.GetValueOnGameThread();
	const float OverrideBand = CVarHexPlanetCoarseBand.GetValueOnGameThread();
	const float CoarseDistance = OverrideDistance > 0.0f ? OverrideDistance : Settings->CoarseDistance;
	const float CoarseBand = OverrideBand > 0.0f ? OverrideBand : Settings->CoarseBand;
	OutNear = CoarseDistance - CoarseBand;
	OutFar = CoarseDistance + CoarseBand;
}

bool AHexPlanet::ArePiecesReadyAt(const FVector& CameraLocation) const
{
	// without coarse meshes every chunk keeps its pieces, and without a kit there are none to build; in the first frames the build of the
	// start gives any chunk its pieces in the frame the camera comes, as UpdateChunkLod measures from where the camera is in that frame
	if (CoarseChunks.IsEmpty() || CoarseChunks.Num() != SurfaceChunks.Num() || !SurfaceKit || ChunkBounds.Num() != SurfaceChunks.Num() || StartupSurface)
	{
		return true;
	}
	double Near, Far;
	GetLodBand(Near, Far);
	for (int32 Chunk = 0; Chunk < SurfaceChunks.Num(); ++Chunk)
	{
		const FBoxSphereBounds& Bounds = ChunkBounds[Chunk];
		const double Reach = FMath::Max(Bounds.SphereRadius, CoarseChunks[Chunk]->Bounds.SphereRadius);
		if (ChunkPieces[Chunk] != 2 && FVector::Distance(CameraLocation, Bounds.Origin) - Reach < Far)
		{
			return false;
		}
	}
	return true;
}

void AHexPlanet::BuildPiecesNow(TArray<int32> Chunks)
{
	if (Chunks.IsEmpty() || !SurfaceKit)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	Chunks.Sort();
	HexPlanetSurface::FBuildOptions Options;
	Options.bPiecesOnly = true;
	Options.CornerLow = ShownCornerLow;
	Options.Relief = CellRelief;
	const HexPlanetSurface::FBuild Built = HexPlanetSurface::Build(Grid, *SurfaceKit, SurfaceParams, ShownLevels, ShownMassifSteps, Chunks, MeanPeak,
		&ShownReliefOverrides, Rivers->GetShownCurve().Get(), Options);
	TArray<FHexPlanetMeshSectionRef> Prepared;
	Prepared.SetNum(Built.Sections.Num());
	ParallelFor(Prepared.Num(), [&](int32 Index)
	{
		Prepared[Index] = PrepareChunkSection(Built.Sections[Index], TEXT("SurfaceChunk"), Built.Chunks[Index / 2]);
	});
	for (int32 Slot = 0; Slot < Built.Chunks.Num(); ++Slot)
	{
		const int32 Chunk = Built.Chunks[Slot];
		SetChunkPieces(Chunk, MoveTemp(Prepared[Slot * 2]), MoveTemp(Prepared[Slot * 2 + 1]));
		SetWireSection(Chunk, 0, &Built.Sections[Slot * 2]);
		SetWireSection(Chunk, 1, &Built.Sections[Slot * 2 + 1]);
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: built the pieces of %d chunks at once, as the camera came near them before they streamed in, in %.1f ms"),
		Built.Chunks.Num(), (FPlatformTime::Seconds() - Start) * 1000.0);
}

bool AHexPlanet::WantsPieces(int32 Chunk) const
{
	// without coarse meshes or with the streaming off every chunk keeps its pieces, and so does every chunk until the camera is measured from
	if (CoarseChunks.IsEmpty() || CoarseChunks.Num() != SurfaceChunks.Num() || !ChunkBounds.IsValidIndex(Chunk) || !bHasLodCamera
		|| !CVarHexPlanetStreamPieces.GetValueOnGameThread())
	{
		return true;
	}
	double Near, Far;
	GetLodBand(Near, Far);
	const FBoxSphereBounds& Bounds = ChunkBounds[Chunk];
	const double Reach = FMath::Max(Bounds.SphereRadius, CoarseChunks[Chunk]->Bounds.SphereRadius);
	return FVector::Distance(LodCamera, Bounds.Origin) - Reach < Far + Settings->PieceStreamAhead;
}

void AHexPlanet::SetChunkPieces(int32 Chunk, FHexPlanetMeshSectionRef Land, FHexPlanetMeshSectionRef Mountains)
{
	UHexPlanetMeshComponent* ChunkMesh = SurfaceChunks[Chunk];
	ChunkMesh->SetSection(0, MoveTemp(Land));
	ChunkMesh->SetSection(1, MoveTemp(Mountains));
	for (int32 Part = 0; Part < 2; ++Part)
	{
		ChunkMesh->SetMaterial(Part, GetChunkMaterial(Chunk, Part, bAppliedCut, AppliedView.Target));
	}
	ChunkPieces[Chunk] = 2;
}

void AHexPlanet::DropChunkPieces(int32 Chunk)
{
	SurfaceChunks[Chunk]->ClearAllSections();
	ChunkPieces[Chunk] = 0;
	SetWireSection(Chunk, 0, nullptr);
	SetWireSection(Chunk, 1, nullptr);
}

void AHexPlanet::StartStream(TArray<int32> Chunks)
{
	if (Chunks.IsEmpty() || !SurfaceKit || StreamTask.IsValid())
	{
		return;
	}
	Chunks.Sort();
	Stream = MakeShared<FChunkStream>();
	Stream->Chunks = MoveTemp(Chunks);
	Stream->StartTime = FPlatformTime::Seconds();
	for (const int32 Chunk : Stream->Chunks)
	{
		Stream->Generations.Add(ChunkGenerations[Chunk]);
		ChunkPieces[Chunk] = 1;
	}

	// from copies of what the surface shown was built from, so a rebuild of levels may run meanwhile; the heights of the corners and of the
	// relief are those of that build, so they are not found again for the whole planet
	HexPlanetSurface::FBuildOptions Options;
	Options.bPiecesOnly = true;
	StreamTask = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Work = Stream, GridPtr = &Grid, Kit = SurfaceKit.ToSharedRef(), Params = SurfaceParams, Levels = ShownLevels, MassifSteps = ShownMassifSteps,
			Overrides = ShownReliefOverrides, Curve = Rivers->GetShownCurve(), CornerLow = ShownCornerLow, Relief = CellRelief, FixedPeak = MeanPeak,
			Options, bWire = AppliedWireTarget.IsSet()]() mutable
		{
			Options.CornerLow = CornerLow;
			Options.Relief = Relief;
			HexPlanetSurface::FBuild Built = HexPlanetSurface::Build(*GridPtr, *Kit, Params, Levels, MassifSteps, Work->Chunks, FixedPeak, &Overrides, Curve.Get(), Options);
			Work->Pieces.SetNum(Built.Sections.Num());
			ParallelFor(Built.Sections.Num(), [&](int32 Index)
			{
				Work->Pieces[Index] = PrepareChunkSection(Built.Sections[Index], TEXT("SurfaceChunk"), Work->Chunks[Index / 2]);
			});
			if (bWire)
			{
				Work->WireSections = MoveTemp(Built.Sections);
			}
			Work->BuildMs = (FPlatformTime::Seconds() - Work->StartTime) * 1000.0;
		});
}

void AHexPlanet::FinishStream()
{
	if (!Stream)
	{
		return;
	}
	const double ApplyStart = FPlatformTime::Seconds();
	int32 Shown = 0;
	// a layer hidden while they streamed wants them only for a trip to it
	const bool bWanted = bPresented || PrefetchCamera.IsSet();
	for (int32 Slot = 0; Slot < Stream->Chunks.Num(); ++Slot)
	{
		const int32 Chunk = Stream->Chunks[Slot];
		if (!bWanted && ChunkPieces[Chunk] == 1)
		{
			ChunkPieces[Chunk] = 0;
			continue;
		}
		// a build of levels that showed the chunk meanwhile also gave it its pieces, or freed them
		if (ChunkPieces[Chunk] != 1 || ChunkGenerations[Chunk] != Stream->Generations[Slot] || !Stream->Pieces.IsValidIndex(Slot * 2 + 1))
		{
			continue;
		}
		SetChunkPieces(Chunk, MoveTemp(Stream->Pieces[Slot * 2]), MoveTemp(Stream->Pieces[Slot * 2 + 1]));
		if (Stream->WireSections.IsValidIndex(Slot * 2 + 1))
		{
			SetWireSection(Chunk, 0, &Stream->WireSections[Slot * 2]);
			SetWireSection(Chunk, 1, &Stream->WireSections[Slot * 2 + 1]);
		}
		++Shown;
	}
	UE_LOG(LogHexHero, Verbose, TEXT("Planet: built the pieces of %d chunks near the camera in %.1f ms in the background, shown %d in %.1f ms"),
		Stream->Chunks.Num(), Stream->BuildMs, Shown, (FPlatformTime::Seconds() - ApplyStart) * 1000.0);
	Stream.Reset();
}

void AHexPlanet::SetWireSection(int32 Chunk, int32 Part, const HexPlanetSurface::FSection* Section)
{
	UProceduralMeshComponent* Wire = Part == 2 ? (WireCoarseChunks.IsValidIndex(Chunk) ? WireCoarseChunks[Chunk].Get() : nullptr)
		: (WireChunks.IsValidIndex(Chunk) ? WireChunks[Chunk].Get() : nullptr);
	if (!Wire)
	{
		return;
	}
	const int32 WireSection = Part == 2 ? 0 : Part;
	Wire->ClearMeshSection(WireSection);

	// a coarse mesh counts as terrain
	if (Section && AppliedWireTarget.IsSet() && IsWireTarget(Part == 2 ? 0 : Part, AppliedWireTarget.GetValue()) && !Section->Triangles.IsEmpty())
	{
		TArray<FVector> Positions;
		Positions.Reserve(Section->Positions.Num());
		for (const FVector3f& Position : Section->Positions)
		{
			Positions.Add(FVector(Position));
		}
		Wire->CreateMeshSection_LinearColor(WireSection, Positions, Section->Triangles, {}, {}, {}, {}, false);
		Wire->SetMeshSectionVisible(WireSection, true);
		Wire->SetMaterial(WireSection, WireframeMaterial);
	}
}

void AHexPlanet::BuildWireSections()
{
	for (int32 Chunk = 0; Chunk < WireChunks.Num(); ++Chunk)
	{
		WireChunks[Chunk]->ClearAllMeshSections();
	}
	for (int32 Chunk = 0; Chunk < WireCoarseChunks.Num(); ++Chunk)
	{
		WireCoarseChunks[Chunk]->ClearAllMeshSections();
	}
	if (!AppliedWireTarget.IsSet() || !SurfaceKit)
	{
		return;
	}

	// the surface keeps its meshes on the GPU alone, so the debug view builds them again from what the surface shown was built from: every
	// coarse mesh, and the pieces of the chunks that hold them
	const double Start = FPlatformTime::Seconds();
	HexPlanetSurface::FBuildOptions Options;
	Options.CornerLow = ShownCornerLow;
	Options.Relief = CellRelief;
	const HexPlanetSurface::FBuild Built = HexPlanetSurface::Build(Grid, *SurfaceKit, SurfaceParams, ShownLevels, ShownMassifSteps, HexPlanetSurface::GetAllChunks(Grid),
		MeanPeak, &ShownReliefOverrides, Rivers->GetShownCurve().Get(), Options);
	for (int32 Slot = 0; Slot < Built.Chunks.Num(); ++Slot)
	{
		const int32 Chunk = Built.Chunks[Slot];
		if (ChunkPieces.IsValidIndex(Chunk) && ChunkPieces[Chunk] == 2)
		{
			SetWireSection(Chunk, 0, &Built.Sections[Slot * 2]);
			SetWireSection(Chunk, 1, &Built.Sections[Slot * 2 + 1]);
		}
		if (Built.CoarseSections.IsValidIndex(Slot))
		{
			SetWireSection(Chunk, 2, &Built.CoarseSections[Slot]);
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: built the copies of the wireframe in %.0f ms"), (FPlatformTime::Seconds() - Start) * 1000.0);
}

void AHexPlanet::UpdateDebugView()
{
	FHexDebugViewSettings View = AHexWorldMap::GetDebugViewSettings();

	if (!WireframeMaterial || CellMeshes.IsEmpty())
	{
		View.Mode = EHexWireframeMode::Off;
	}

	Water->SetVisibility(Settings->bSea && View.Water != EHexWaterView::Hidden);
	WaterGrid->SetVisibility(Settings->bSea && View.Water != EHexWaterView::Hidden);
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

	// a rebuilt section takes its materials and visibility again; ApplySurface updated its copy in the wireframe
	const bool bSurfaceChangedNow = bSurfaceChanged;
	bSurfaceChanged = false;

	if (View.Mode != AppliedView.Mode || View.Target != AppliedView.Target || bCut != bAppliedCut || bSurfaceChangedNow)
	{
		bAppliedCut = bCut;

		// a coarse mesh counts as terrain, with its mountains, as it is what the terrain is from far away
		const bool bCoarseChosen = IsWireTarget(0, View.Target);
		for (int32 Chunk = 0; Chunk < CoarseChunks.Num(); ++Chunk)
		{
			CoarseChunks[Chunk]->SetSectionVisible(0, !(View.Mode == EHexWireframeMode::Only && !bCut && bCoarseChosen));
			CoarseChunks[Chunk]->SetMaterial(0, GetChunkMaterial(Chunk, 2, bCut, View.Target));
		}

		for (int32 Section = 0; Section < SurfaceChunks.Num() * 2; ++Section)
		{
			const bool bChosen = IsWireTarget(Section, View.Target);
			UHexPlanetMeshComponent* ChunkMesh = SurfaceChunks[Section / 2];
			ChunkMesh->SetSectionVisible(Section % 2, !(View.Mode == EHexWireframeMode::Only && !bCut && bChosen));
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

		// the copies keep the visibility of their sources, which only the triangles hide
		BuildWireSections();
	}

	// the walls are no terrain: their copies while the triangles of every cell are drawn, and the walls themselves hidden while they are drawn
	// alone; built again with every change of the surface, as the walls follow it
	const bool bWallWires = WireTarget.IsSet() && IsWireTarget(1, WireTarget.GetValue()) && !WallCoarseChunks.IsEmpty();
	bWallsHiddenByWire = bWallWires && View.Mode == EHexWireframeMode::Only;
	if (bWallWires != bAppliedWallWires || (bWallWires && bSurfaceChangedNow))
	{
		bAppliedWallWires = bWallWires;
		BuildWallWires(bWallWires);
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
	TArray<FVector3f> Corners;

	for (const TPair<int32, FLinearColor>& Each : Chosen)
	{
		// the surface keeps its meshes on the GPU alone, so the triangles of the cells come from building them again
		const FCellMeshRange& Range = CellMeshes[Each.Key];
		if (Range.Section == INDEX_NONE || !IsWireTarget(Range.Section, View.Target))
		{
			continue;
		}
		GetCellTriangles(Each.Key, Corners);
		for (const FVector3f& Corner : Corners)
		{
			Triangles.Add(Positions.Add(FVector(Corner)));
			Colors.Add(Each.Value);
		}
	}

	// the walls of these cells, which are no terrain, in the color of their cell
	if (!WallCoarseChunks.IsEmpty() && IsWireTarget(1, View.Target))
	{
		for (const TPair<int32, FLinearColor>& Each : Chosen)
		{
			HexPlanetWalls::FMesh Mesh;
			HexPlanetWalls::BuildNear(*Walls, Each.Key, Grid.GetCells()[Each.Key].Center * WallParams.FloorRadius, Settings->HexRadius * 1.1, WallParams, Mesh);
			const int32 First = Positions.Num();
			for (const FVector3f& Position : Mesh.Positions)
			{
				Positions.Add(FVector(Position));
				Colors.Add(Each.Value);
			}
			for (const int32 Index : Mesh.Triangles)
			{
				Triangles.Add(First + Index);
			}
		}
	}

	if (!Triangles.IsEmpty())
	{
		HoverWireframe->CreateMeshSection_LinearColor(0, Positions, Triangles, {}, {}, Colors, {}, false);
		HoverWireframe->SetMaterial(0, HoverWireframeMaterial);
	}
}

void AHexPlanet::BuildWallWires(bool bShown)
{
	for (UProceduralMeshComponent* Wire : WallWireChunks)
	{
		if (Wire)
		{
			Wire->ClearAllMeshSections();
		}
	}
	if (!bShown || !WireframeMaterial)
	{
		return;
	}

	// the walls keep their meshes on the GPU alone, so the debug view builds them again from their lines
	const double Start = FPlatformTime::Seconds();
	const int32 NumChunks = Walls->ChunkSamples.Num();
	TArray<HexPlanetWalls::FMesh> Meshes;
	Meshes.SetNum(NumChunks);
	ParallelFor(NumChunks, [&](int32 Chunk) { HexPlanetWalls::BuildChunk(*Walls, Chunk, WallParams, Meshes[Chunk]); });
	WallWireChunks.SetNum(NumChunks);
	int64 Total = 0;
	for (int32 Chunk = 0; Chunk < NumChunks; ++Chunk)
	{
		const HexPlanetWalls::FMesh& Mesh = Meshes[Chunk];
		if (Mesh.Triangles.IsEmpty())
		{
			continue;
		}
		UProceduralMeshComponent* Wire = WallWireChunks[Chunk];
		if (!Wire)
		{
			Wire = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("WallWireChunk%d"), Chunk));
			Wire->SetupAttachment(Wireframe);
			Wire->SetCastShadow(false);
			Wire->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Wire->SetTranslucentSortPriority(Wireframe->TranslucencySortPriority);
			Wire->RegisterComponent();
			WallWireChunks[Chunk] = Wire;
		}
		TArray<FVector> Positions;
		Positions.Reserve(Mesh.Positions.Num());
		for (const FVector3f& Position : Mesh.Positions)
		{
			Positions.Add(FVector(Position));
		}
		Wire->CreateMeshSection_LinearColor(0, Positions, Mesh.Triangles, {}, {}, {}, {}, false);
		Wire->SetMaterial(0, WireframeMaterial);
		if (WallCoarseChunks.IsValidIndex(Chunk) && WallCoarseChunks[Chunk])
		{
			Wire->SetCullDistance(WallCoarseChunks[Chunk]->LDMaxDrawDistance);
		}
		Total += Mesh.Triangles.Num() / 3;
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: built the copies of the walls in the wireframe, %lld triangles, in %.0f ms"), Total, (FPlatformTime::Seconds() - Start) * 1000.0);
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

	const bool bAll = View.Water == EHexWaterView::Hidden;

	if (!WaterMaterial || (bAll == bAllWaterHidden && Count == AppliedWaterHideCount && (Count == 0 || Cell == AppliedWaterHideCell)))
	{
		return;
	}

	AppliedWaterHideCell = Cell;
	AppliedWaterHideCount = Count;
	SetHideCells(WaterMaterial, Cell, Count);

	// the surface draws the grid lines and the outline on the seabed of these cells, as their strip of the water grid is hidden too
	TArray<int32, TInlineAllocator<7>> Hidden;
	if (Count > 0)
	{
		Hidden.Add(Cell);
		if (Count > 1)
		{
			Hidden.Append(Grid.GetCells()[Cell].Neighbors);
		}
	}
	SetWaterHiddenCells(Hidden, bAll);

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
	if (CellCanopyTexture)
	{
		Target->SetTextureParameterValue(TEXT("CellCanopy"), CellCanopyTexture);
	}
	if (CellGlowNearTexture)
	{
		Target->SetTextureParameterValue(TEXT("CellGlowNear"), CellGlowNearTexture);
	}
	if (Rivers && Rivers->GetTexture() && Rivers->GetSegmentTexture())
	{
		Target->SetTextureParameterValue(TEXT("CellRivers"), Rivers->GetTexture());
		Target->SetTextureParameterValue(TEXT("RiverSegments"), Rivers->GetSegmentTexture());
	}
	if (LayerParamsTexture)
	{
		Target->SetTextureParameterValue(TEXT("LayerParams"), LayerParamsTexture);
	}
	// where walls stand on the lips, the moss of the rims grows on their foot (M_HexUndergroundWall) and not on the floor beside them
	Target->SetScalarParameterValue(TEXT("WallsShown"), Settings && Settings->bWalls ? 1.0f : 0.0f);
	// the layers of the patches of another ground and of the old flagstones on the floor of the passages, -1 without them, which the foot of the
	// walls takes too (UpdateWalls); a material without them, as the surface's, ignores them
	if (Settings)
	{
		const UHexTerrainLayersConfig* TerrainLayers = GetDefault<UHexMapSettings>()->LoadTerrainLayers();
		Target->SetScalarParameterValue(TEXT("CaveFloorAltLayer"), float(TerrainLayers ? TerrainLayers->FindLayer(Settings->CaveFloorAltLayerName) : INDEX_NONE));
		Target->SetScalarParameterValue(TEXT("CaveSlabLayer"), float(TerrainLayers ? TerrainLayers->FindLayer(Settings->CaveSlabLayerName) : INDEX_NONE));
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
		SetHeroShadeParameters(WaterMaterial);
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
		SetHeroShadeParameters(CloudMaterial);
		Clouds->SetMaterial(0, CloudMaterial);

		SkyCloudMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		SetCloudParameters(SkyCloudMaterial);
		SetHeroShadeParameters(SkyCloudMaterial);
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

	// a layer without clouds, the underground, has no shadows of them either
	if (!Settings->bClouds)
	{
		Target->SetScalarParameterValue(TEXT("CloudShadowStrength"), 0.0f);
	}
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

	// the underground of a new planet goes with it, so its data is waited for if it is still being generated
	if (AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground); Underground && !Underground->IsDataLoaded())
	{
		Underground->StartUndergroundData();
		Underground->FinishUndergroundData();
	}

	// the levels and biomes of both layers are always written, painted or generated, so the planet does not change when the generation does
	const TArray<AHexPlanet*, TInlineAllocator<2>> Planets = GetLoadedLayers();
	const TSet<int32> KeptEntranceCells = GetKeptEntranceCells();
	for (const AHexPlanet* Planet : Planets)
	{
		if (!Planet->SaveLayerFiles(Folder, KeptEntranceCells, OutError))
		{
			return false;
		}
	}

	const FString SavedFolder = HexPlanetFile::PlanetExists(Folder) ? Folder : PlanetFolder;
	for (AHexPlanet* Planet : Planets)
	{
		Planet->Manifest = Saved;
		Planet->PlanetFolder = SavedFolder;
		Planet->bLevelsEdited = false;
		Planet->bEntrancesEdited = false;
		Planet->bManifestEdited = false;
		Planet->bEditedOutsideSteps = false;
	}
	for (AHexPlanet* Planet : Planets)
	{
		Planet->OnSaved.Broadcast();
	}
	ReportLeftOutEntrances();
	return true;
}

void AHexPlanet::ReportLeftOutEntrances() const
{
	const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
	const AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground);
	if (!SurfacePlanet || !Underground || !Underground->bDataLoaded)
	{
		return;
	}

	// the same as SaveLayerFiles wrote, so the count is what the next start will lack
	const int32 Kept = HexPlanetUnderground::GetSavedEntrances(Underground->Grid, SurfacePlanet->Levels, Underground->Levels, Underground->Entrances).Num();
	const int32 LeftOut = Underground->Entrances.Num() - Kept;
	if (LeftOut <= 0)
	{
		return;
	}
	TArray<FString> Problems;
	for (int32 Index = 0; Index < Underground->Entrances.Num(); ++Index)
	{
		const HexPlanetUnderground::EEntranceProblem Problem = GetEntranceProblem(Index);
		if (Problem != HexPlanetUnderground::EEntranceProblem::None)
		{
			Problems.Add(FString::Printf(TEXT("%d (%s)"), Underground->Entrances[Index].Cell, HexPlanetUnderground::GetProblemText(Problem)));
		}
	}
	const FString Text = FString::Printf(TEXT("%d of %d entrances were left out of entrances.json, as they cannot be used, and are gone when the planet opens again: %s"),
		LeftOut, Underground->Entrances.Num(), *FString::Join(Problems, TEXT(", ")));
	UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Text);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.0f, FColor::Yellow, Text);
	}
}

TSet<int32> AHexPlanet::GetKeptEntranceCells() const
{
	// the entrances a save writes, as one that cannot be used is left out of entrances.json and its cell keeps what stands on it; none while the
	// underground has no levels, so nothing is dropped then
	TSet<int32> Cells;
	const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
	const AHexPlanet* Underground = GetLayerPlanet(EHexPlanetLayer::Underground);
	if (!SurfacePlanet || !Underground || !Underground->bDataLoaded || !SurfacePlanet->IsDataLoaded())
	{
		return Cells;
	}
	for (const FHexEntrance& Entrance : HexPlanetUnderground::GetSavedEntrances(Underground->Grid, SurfacePlanet->Levels, Underground->Levels, Underground->Entrances))
	{
		Cells.Add(Entrance.Cell);
	}
	return Cells;
}

bool AHexPlanet::SaveLayerFiles(const FString& Folder, const TSet<int32>& KeptEntranceCells, FString& OutError) const
{
	const FString DataFolder = HexPlanetFile::GetDataFolder(Folder, PlanetLayer);
	if (!HexPlanetFile::SaveLevels(DataFolder, Levels, OutError, HexPlanetFile::GetLevelLetters(PlanetLayer))
		|| !HexPlanetFile::SaveBiomes(DataFolder, CellBiomes, BiomeLetters, OutError) || !SaveRelief(DataFolder, OutError))
	{
		return false;
	}

	// a layer without object sets or rivers, the underground, writes no empty files of them
	if ((PlanetLayer == EHexPlanetLayer::Surface || !Objects->GetSets().IsEmpty()) && !Objects->Save(DataFolder, KeptEntranceCells, OutError))
	{
		return false;
	}
	if (Settings->bWalls && !SaveWallHeights(DataFolder, OutError))
	{
		return false;
	}
	if (Settings->bRivers && !Rivers->Save(DataFolder, OutError))
	{
		return false;
	}

	// the entrances that can be used, at the root of the planet, as the editor keeps those that cannot in memory
	if (PlanetLayer == EHexPlanetLayer::Underground)
	{
		const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
		const TArray<FHexEntrance> Saved = SurfacePlanet ? HexPlanetUnderground::GetSavedEntrances(Grid, SurfacePlanet->Levels, Levels, Entrances) : Entrances;
		if (!HexPlanetFile::SaveEntrances(Folder, Saved, OutError))
		{
			return false;
		}
	}
	return true;
}

void AHexPlanet::WriteGeneratedFiles(bool bLevels, bool bBiomes, bool bObjects, bool bRivers)
{
	if (!IsSaved())
	{
		return;
	}

	// into the data folder of the layer, which for the surface is the folder of the planet
	const FString DataFolder = GetDataFolder();
	FString Error;
	if (bLevels)
	{
		if (HexPlanetFile::SaveLevels(DataFolder, Levels, Error, HexPlanetFile::GetLevelLetters(PlanetLayer)))
		{
			UE_LOG(LogHexHero, Log, TEXT("Planet: wrote the generated levels to %s"), *HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::LevelsFile));
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
	}

	if (bBiomes)
	{
		if (HexPlanetFile::SaveBiomes(DataFolder, CellBiomes, BiomeLetters, Error))
		{
			UE_LOG(LogHexHero, Log, TEXT("Planet: wrote the generated biomes to %s"), *HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::BiomesFile));
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
	}

	if (bObjects)
	{
		// generated before the entrances are known, so nothing is left out for them
		if (Objects->Save(DataFolder, {}, Error))
		{
			UE_LOG(LogHexHero, Log, TEXT("Planet: wrote the generated objects to %s"), *HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::ObjectsFile));
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
	}

	if (bRivers)
	{
		if (Rivers->Save(DataFolder, Error))
		{
			UE_LOG(LogHexHero, Log, TEXT("Planet: wrote the generated rivers to %s"), *HexPlanetFile::GetFilePath(DataFolder, HexPlanetFile::RiversFile));
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
	}
}

int32 AHexPlanet::GetCellRockLayer(int32 Cell) const
{
	const int32 Biome = GetCellBiome(Cell);
	return BiomeConfigs.IsValidIndex(Biome) && BiomeConfigs[Biome] ? FMath::Clamp(BiomeConfigs[Biome]->MountainRockLayer, 0, 255) : 2;
}

void AHexPlanet::GetCellTriangles(int32 Cell, TArray<FVector3f>& OutCorners) const
{
	OutCorners.Reset();
	if (!CellMeshes.IsValidIndex(Cell) || !SurfaceKit)
	{
		return;
	}

	// from the build being shown, which holds the meshes of the cells of its chunks
	if (TriangleSource && TriangleSource->CellMeshes.IsValidIndex(Cell))
	{
		const HexPlanetSurface::FCellMesh& Range = TriangleSource->CellMeshes[Cell];
		const int32 Slot = Range.Section == INDEX_NONE ? INDEX_NONE : Algo::BinarySearch(TriangleSource->Chunks, Range.Section / 2);
		const HexPlanetSurface::FSection* Section = Slot == INDEX_NONE ? nullptr : &TriangleSource->Sections[Slot * 2 + Range.Section % 2];
		if (Section && Range.FirstIndex + Range.NumIndices <= Section->Triangles.Num())
		{
			OutCorners.Reserve(Range.NumIndices);
			for (int32 Index = Range.FirstIndex; Index < Range.FirstIndex + Range.NumIndices; ++Index)
			{
				OutCorners.Add(FVector3f(Section->Positions[Section->Triangles[Index]]));
			}
			return;
		}
	}

	// else built again, as the surface keeps its meshes on the GPU alone, from what the surface shown was built from
	HexPlanetSurface::BuildCellTriangles(Grid, *SurfaceKit, SurfaceParams, ShownLevels, ShownMassifSteps, ShownCornerLow, CellRelief, &ShownReliefOverrides,
		Rivers->GetShownCurve().Get(), Cell, OutCorners);
}

double AHexPlanet::FindGroundRadius(int32 Cell, const FVector3d& Direction) const
{
	double Ground = Grid.GetCells().IsValidIndex(Cell) ? GetCellTopRadius(Cell) : Radius;
	TArray<FVector3f> Triangles;
	GetCellTriangles(Cell, Triangles);
	const FVector3d Up = Direction.GetSafeNormal();
	for (int32 Corner = 0; Corner + 2 < Triangles.Num(); Corner += 3)
	{
		const FVector3d A(Triangles[Corner]);
		const FVector3d B(Triangles[Corner + 1]);
		const FVector3d C(Triangles[Corner + 2]);
		const FVector3d Normal = FVector3d::CrossProduct(B - A, C - A);
		const double Along = FVector3d::DotProduct(Normal, Up);
		if (FMath::Abs(Along) < 1.0e-9)
		{
			continue;
		}
		const double Distance = FVector3d::DotProduct(Normal, A) / Along;
		const FVector3d Point = Up * Distance;
		// the point lies inside the triangle when it is on the inner side of each of its edges
		const auto Inside = [&Normal, &Point](const FVector3d& From, const FVector3d& To) { return FVector3d::DotProduct(FVector3d::CrossProduct(To - From, Point - From), Normal) >= -1.0e-6; };
		if (Distance > 0.0 && Inside(A, B) && Inside(B, C) && Inside(C, A))
		{
			Ground = Distance;
			break;
		}
	}
	return Ground;
}

void AHexPlanet::LoadRelief()
{
	ReliefOverrides.Reset();
	TArray<FHexReliefCell> Lines;
	FString Error;
	if (!IsSaved() || !HexPlanetFile::LoadRelief(GetDataFolder(), Lines, Error))
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

void AHexPlanet::SetReliefOverrides(const HexPlanetSurface::FReliefOverrides& Changes, bool bByStep)
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
		MarkLevelsEdited(bByStep);
		bLevelsPending = true;
		StartRebuild();
	}
}

void AHexPlanet::OnRiversChanged(bool bByStep)
{
	MarkLevelsEdited(bByStep);
	bLevelsPending = true;
	StartRebuild();
}

HexPlanetSurface::FReliefOverride AHexPlanet::GetReliefOverride(int32 Cell) const
{
	const HexPlanetSurface::FReliefOverride* Found = ReliefOverrides.Find(Cell);
	return Found ? *Found : HexPlanetSurface::FReliefOverride();
}

void AHexPlanet::SetWallHeights(const TMap<int32, float>& Changes, bool bByStep)
{
	if (Changes.IsEmpty())
	{
		return;
	}
	SetCells({}, {}, Changes);
	MarkLevelsEdited(bByStep);
}

float AHexPlanet::GetWallHeightOverride(int32 Cell) const
{
	const float* Found = WallHeightOverrides.Find(Cell);
	return Found ? *Found : -1.0f;
}

float AHexPlanet::GetWallHeight(int32 Cell) const
{
	const float* Found = WallHeightOverrides.Find(Cell);
	return Found ? *Found : GetStyleWallHeight(GetCellBiome(Cell));
}

float AHexPlanet::GetStyleWallHeight(int32 Biome) const
{
	const UHexBiomeConfig* Config = BiomeConfigs.IsValidIndex(Biome) ? BiomeConfigs[Biome].Get() : nullptr;
	const bool bPool = Config && (Config->VoidStyle == EHexVoidStyle::Lava || Config->VoidStyle == EHexVoidStyle::Lake);
	return !Settings ? 1.0f : Settings->ClampWallHeight(bPool ? FMath::Clamp(Settings->WallPoolHeight, 0.1f, 1.0f) : 1.0f);
}

int32 AHexPlanet::GetWallStyle(int32 Level, int32 Biome) const
{
	if (Level >= 0)
	{
		return INDEX_NONE;
	}
	const UHexBiomeConfig* Config = BiomeConfigs.IsValidIndex(Biome) ? BiomeConfigs[Biome].Get() : nullptr;
	return Config ? int32(Config->VoidStyle) : 0;
}

void AHexPlanet::LoadWallHeights()
{
	WallHeightOverrides.Reset();
	TArray<FHexWallCell> Lines;
	FString Error;
	if (!Settings || !Settings->bWalls || !IsSaved() || !HexPlanetFile::LoadWalls(GetDataFolder(), Lines, Error))
	{
		if (!Error.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		}
		return;
	}
	for (const FHexWallCell& Line : Lines)
	{
		if (Grid.GetCells().IsValidIndex(Line.Cell))
		{
			WallHeightOverrides.Add(Line.Cell, Settings->ClampWallHeight(Line.Height));
		}
	}
}

bool AHexPlanet::SaveWallHeights(const FString& Folder, FString& OutError) const
{
	// a void that became a passage keeps the height of its walls until the next save, which leaves it out
	TArray<FHexWallCell> Lines;
	for (const TPair<int32, float>& Each : WallHeightOverrides)
	{
		if (Levels.IsValidIndex(Each.Key) && Levels[Each.Key] < 0)
		{
			FHexWallCell& Line = Lines.AddDefaulted_GetRef();
			Line.Cell = Each.Key;
			Line.Height = Each.Value;
		}
	}
	return HexPlanetFile::SaveWalls(Folder, Lines, OutError);
}

bool AHexPlanet::SaveLevels(FString& OutError)
{
	if (!IsSaved())
	{
		OutError = TEXT("The planet is not saved");
		return false;
	}

	// both layers, whichever is shown; an underground whose data is not read yet has not changed its files
	const TArray<AHexPlanet*, TInlineAllocator<2>> Planets = GetLoadedLayers();
	const TSet<int32> KeptEntranceCells = GetKeptEntranceCells();

	// a new layout of the underground changed the seed offset in planet.json, which only then is written again, that number alone
	if (Planets.ContainsByPredicate([](const AHexPlanet* Planet) { return Planet->bManifestEdited; }) && !HexPlanetFile::SaveSeedOffset(PlanetFolder, Manifest, OutError))
	{
		return false;
	}
	for (const AHexPlanet* Planet : Planets)
	{
		if (!Planet->SaveLayerFiles(PlanetFolder, KeptEntranceCells, OutError))
		{
			return false;
		}
	}
	for (AHexPlanet* Planet : Planets)
	{
		Planet->bLevelsEdited = false;
		Planet->bEntrancesEdited = false;
		Planet->bManifestEdited = false;
		Planet->bEditedOutsideSteps = false;
	}
	for (AHexPlanet* Planet : Planets)
	{
		Planet->OnSaved.Broadcast();
	}
	ReportLeftOutEntrances();
	return true;
}

void AHexPlanet::FindBiomes(EHexPlanetLayer Layer, TArray<TObjectPtr<UHexBiomeConfig>>& OutConfigs, TArray<TCHAR>& OutLetters)
{
	TArray<TObjectPtr<UHexBiomeConfig>> Configs;
	TArray<TCHAR> Letters;
	const bool bUnderground = Layer == EHexPlanetLayer::Underground;

	for (const UHexBiomeConfig* Biome : GetDefault<UHexMapSettings>()->LoadBiomes())
	{
		const FString& Text = bUnderground ? Biome->UndergroundLetter : Biome->PlanetLetter;
		if (Text.Len() != 1 || !FChar::IsAlpha(Text[0]))
		{
			continue;
		}

		const TCHAR Letter = FChar::ToUpper(Text[0]);
		if (Letters.Contains(Letter))
		{
			UE_LOG(LogHexHero, Error, TEXT("Planet: two biomes have the %s letter %c, %s is left out"), bUnderground ? TEXT("underground") : TEXT("planet"), Letter, *Biome->GetName());
			continue;
		}
		Configs.Add(const_cast<UHexBiomeConfig*>(Biome));
		Letters.Add(Letter);
	}

	// by letter, so the index of a biome is the same in every run
	TArray<int32> Order;
	for (int32 Index = 0; Index < Letters.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Order.Sort([&Letters](int32 A, int32 B) { return Letters[A] < Letters[B]; });

	OutConfigs.Reset();
	OutLetters.Reset();
	for (const int32 Index : Order)
	{
		OutConfigs.Add(Configs[Index]);
		OutLetters.Add(Letters[Index]);
	}
}

void AHexPlanet::LoadBiomeConfigs()
{
	FindBiomes(PlanetLayer, BiomeConfigs, BiomeLetters);

	if (BiomeConfigs.IsEmpty())
	{
		UE_LOG(LogHexHero, Error, TEXT("Planet: no biome has a%s, run Tools/create_biome_assets.py"),
			PlanetLayer == EHexPlanetLayer::Underground ? TEXT("n UndergroundLetter") : TEXT(" PlanetLetter"));
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

	// underground, how many voids hold each style and how many sides lie between two styles, where M_HexUnderground fades both to plain floor
	if (PlanetLayer == EHexPlanetLayer::Underground)
	{
		int32 Styles[4] = {};
		int32 StyleSides = 0;
		for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
		{
			const FColor& Pixel = BiomePixels[(Cell / HexPlanetSurface::MarkColumns) * HexPlanetSurface::MarkColumns * 2 + (Cell % HexPlanetSurface::MarkColumns) * 2];
			if (!HexPlanetUnderground::IsPassage(Levels[Cell]))
			{
				++Styles[(Pixel.B >> 6) & 3];
				for (int32 Side = 0; Side < 6; ++Side)
				{
					StyleSides += (Pixel.B >> Side) & 1;
				}
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("Planet: the voids of the underground hold mist in %d cells, lava in %d and lakes in %d, with %d sides of a void towards another style"),
			Styles[0], Styles[1], Styles[2], StyleSides);
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
	// underground, which has no mountains, blue holds the style of a void instead (EHexVoidStyle, bits 6 and 7) and in bit K whether neighbor K is
	// a void of another style, where M_HexUnderground fades both to plain floor (VOID_STYLE_CODE); a passage keeps 0
	if (PlanetLayer == EHexPlanetLayer::Underground)
	{
		const auto GetVoidStyle = [this, &GetLayers](int32 Other) -> int32
		{
			if (!Levels.IsValidIndex(Other) || HexPlanetUnderground::IsPassage(Levels[Other]))
			{
				return INDEX_NONE;
			}
			const UHexBiomeConfig* Layers = GetLayers(Other);
			return Layers ? FMath::Clamp(int32(Layers->VoidStyle), 0, 3) : 0;
		};
		Mountains = 0;
		const int32 Own = GetVoidStyle(Cell);
		if (Own != INDEX_NONE)
		{
			Mountains = uint8(Own << 6);
			for (int32 Index = 0; Index < Each.Neighbors.Num() && Index < 6; ++Index)
			{
				const int32 Other = GetVoidStyle(Each.Neighbors[Index]);
				Mountains |= Other != INDEX_NONE && Other != Own ? uint8(1 << Index) : uint8(0);
			}
		}
	}
	// green, the rock layer, which no material reads, holds underground which neighbors lay no puddle (UpdateCanopy), as alpha of the second pixel
	// holds what lies on the cell and its neighbors, both kept as they are here
	const uint8 Green = PlanetLayer == EHexPlanetLayer::Underground ? BiomePixels[Pixel].G : uint8(Rock);
	BiomePixels[Pixel] = FColor(uint8(Terrain), Green, Mountains, Seas);
	BiomePixels[Pixel + 1] = FColor(Near[0], Near[1], Near[2], BiomePixels[Pixel + 1].A);
}

namespace
{
	/** Sends pixels to a texture of that size, as a copy the render thread frees once it has it */
	void UploadPixels(UTexture2D* Texture, const TArray<FColor>& Pixels, int32 Columns)
	{
		const int32 Rows = Pixels.Num() / Columns;
		uint8* Data = static_cast<uint8*>(FMemory::Malloc(Pixels.Num() * sizeof(FColor)));
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Columns, Rows);
		Texture->UpdateTextureRegions(0, 1, Region, Columns * sizeof(FColor), sizeof(FColor), Data,
			[](uint8* InData, const FUpdateTextureRegion2D* InRegion)
			{
				FMemory::Free(InData);
				delete InRegion;
			});
	}
}

void AHexPlanet::UploadBiomePixels()
{
	if (CellBiomeTexture)
	{
		UploadPixels(CellBiomeTexture, BiomePixels, HexPlanetSurface::MarkColumns * 2);
	}
}

void AHexPlanet::UpdateCanopy()
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (Cells.IsEmpty() || BiomePixels.IsEmpty())
	{
		return;
	}

	const int32 Rows = FMath::DivideAndRoundUp(Cells.Num(), HexPlanetSurface::MarkColumns);
	CanopyPixels.Init(FColor(0, 0, 0, 0), HexPlanetSurface::MarkColumns * Rows);
	// the root of the linear color, as eight bits of it are too coarse for dark greens
	const auto Encode = [](float Value) { return uint8(FMath::RoundToInt32(FMath::Sqrt(FMath::Clamp(Value, 0.0f, 1.0f)) * 255.0f)); };

	if (PlanetLayer == EHexPlanetLayer::Underground)
	{
		// the underground has no canopy, so the texture holds the pool of light of each cell (the glow pools of M_HexUnderground): the glow color
		// of what stands on it and its strength (UHexPlanetObjects::GetCellGlow), or of a lava void in the color of its light, or of an exit, and a
		// cell that does not glow takes the mean of the colors of its glowing neighbors by their strengths, whose light falls into it
		const FLinearColor Lava = Settings->LavaLightColor / FMath::Max(Settings->LavaLightColor.GetMax(), 1e-3f);
		TArray<FLinearColor> Glow;
		Glow.Init(FLinearColor::Transparent, Cells.Num());
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			FLinearColor Color;
			float Strength = 0.0f;
			if (!Objects->GetCellGlow(Cell, Color, Strength) && Levels.IsValidIndex(Cell) && !HexPlanetUnderground::IsPassage(Levels[Cell])
				&& BiomeConfigs.IsValidIndex(CellBiomes[Cell]) && BiomeConfigs[CellBiomes[Cell]]->VoidStyle == EHexVoidStyle::Lava)
			{
				Color = Lava;
				Strength = Settings->LavaPool;
			}
			if (Strength > 0.0f)
			{
				Glow[Cell] = FLinearColor(Color.R, Color.G, Color.B, FMath::Clamp(Strength, 1.0f / 255.0f, 1.0f));
			}
		}
		// the exits that can be used are lit as what glows is, so in the dark an exit shows its hall: its cell by ExitPoolStrength and the passages
		// around it by ExitPoolRingShare of that, unless what stands there glows more, a share of the way from ExitPoolColor to the color of its
		// network, which the networks of the surface give, as the markers and the editor take them
		const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
		const UHexPlanetConfig* Colors = SurfacePlanet && SurfacePlanet->Settings ? SurfacePlanet->Settings.Get() : Settings.Get();
		const auto LightExit = [&](int32 Cell, const FLinearColor& Color, float Strength)
		{
			if (Glow.IsValidIndex(Cell) && Levels.IsValidIndex(Cell) && HexPlanetUnderground::IsPassage(Levels[Cell]) && Strength > Glow[Cell].A)
			{
				Glow[Cell] = FLinearColor(Color.R, Color.G, Color.B, FMath::Clamp(Strength, 1.0f / 255.0f, 1.0f));
			}
		};
		const TArray<FHexEntrance>& Exits = GetEntrances();
		for (int32 Index = 0; Index < Exits.Num() && Settings->ExitPoolStrength > 0.0f; ++Index)
		{
			const int32 Cell = Exits[Index].Cell;
			if (!Cells.IsValidIndex(Cell) || GetEntranceProblem(Index) != HexPlanetUnderground::EEntranceProblem::None)
			{
				continue;
			}
			const FLinearColor Color = FMath::Lerp(Settings->ExitPoolColor, Colors->GetNetworkColor(Exits[Index].Network), Settings->ExitPoolNetworkShare);
			LightExit(Cell, Color, Settings->ExitPoolStrength);
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				LightExit(Neighbor, Color, Settings->ExitPoolStrength * Settings->ExitPoolRingShare);
			}
		}
		// the strength and color of a glowing cell are blended with those of its glowing neighbors by GlowPoolBlend, so a patch of covers of several
		// densities or kinds is lit about evenly
		TArray<FLinearColor> Pools = Glow;
		const float Blend = FMath::Clamp(Settings->GlowPoolBlend, 0.0f, 1.0f);
		for (int32 Cell = 0; Cell < Cells.Num() && Blend > 0.0f; ++Cell)
		{
			const FLinearColor& Own = Glow[Cell];
			FLinearColor Sum = FLinearColor::Transparent;
			float Weight = 0.0f;
			int32 Count = 0;
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (Own.A > 0.0f && Glow[Neighbor].A > 0.0f)
				{
					Sum += Glow[Neighbor] * Glow[Neighbor].A;
					Weight += Glow[Neighbor].A;
					++Count;
				}
			}
			if (Count > 0)
			{
				// the mean strength of the cell and those neighbors, in the mean of their colors by strength
				const float Total = Own.A + Weight;
				Pools[Cell] = FLinearColor(FMath::Lerp(Own.R, (Own.R * Own.A + Sum.R) / Total, Blend), FMath::Lerp(Own.G, (Own.G * Own.A + Sum.G) / Total, Blend),
					FMath::Lerp(Own.B, (Own.B * Own.A + Sum.B) / Total, Blend), FMath::Lerp(Own.A, Total / float(1 + Count), Blend));
			}
		}
		// the strength of each glowing cell in fifteenths, at least one, which the cell holds in the alpha of CellCanopy and its neighbors in
		// CellGlowNear, so both cells of a side add up the same light there
		TArray<uint8> Steps;
		Steps.Init(0, Cells.Num());
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			Steps[Cell] = Pools[Cell].A > 0.0f ? uint8(FMath::Clamp(FMath::RoundToInt32(Pools[Cell].A * float(GlowStrengthSteps)), 1, GlowStrengthSteps)) : uint8(0);
		}
		// kept as they are encoded, for the walls along the passages (UpdateWallGlow): a glowing cell its color and strength, one beside glowing
		// cells the mean of their colors by their strengths, whose light the material adds up by their strengths
		CellGlowPools.Init(FLinearColor::Transparent, Cells.Num());
		GlowNearPixels.Init(FColor(0, 0, 0, 0), CanopyPixels.Num());
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			uint32 Near = 0;
			FLinearColor Sum = FLinearColor::Transparent;
			float Weight = 0.0f;
			for (int32 Index = 0; Index < Cells[Cell].Neighbors.Num() && Index < 6; ++Index)
			{
				const int32 Neighbor = Cells[Cell].Neighbors[Index];
				Near |= uint32(Steps[Neighbor]) << (4 * Index);
				Sum += Pools[Neighbor] * (float(Steps[Neighbor]) / GlowStrengthSteps);
				Weight += float(Steps[Neighbor]) / GlowStrengthSteps;
			}
			GlowNearPixels[Cell] = FColor(uint8(Near & 255u), uint8((Near >> 8) & 255u), uint8((Near >> 16) & 255u), 0);
			const FLinearColor& Own = Pools[Cell];
			if (Steps[Cell] > 0)
			{
				CanopyPixels[Cell] = FColor(Encode(Own.R), Encode(Own.G), Encode(Own.B), uint8(Steps[Cell] * (255 / GlowStrengthSteps)));
				CellGlowPools[Cell] = FLinearColor(Own.R, Own.G, Own.B, float(Steps[Cell]) / GlowStrengthSteps);
			}
			else if (Weight > 0.0f)
			{
				CanopyPixels[Cell] = FColor(Encode(Sum.R / Weight), Encode(Sum.G / Weight), Encode(Sum.B / Weight), 0);
				CellGlowPools[Cell] = FLinearColor(Sum.R / Weight, Sum.G / Weight, Sum.B / Weight, 0.0f);
			}
		}
		// in bits 0 to 5 which neighbors glow, and in bit 6 whether the cell holds objects or is an exit, where the material lays no puddle
		// (CavePuddle*), so none lies under a stone or a mushroom nor in the middle of the hall of an exit; green of the first pixel, which the
		// material reads nothing else from underground, holds in bit K whether neighbor K is such a cell, so a puddle fades out before that side
		// as before the rim of a void and is never cut by it
		TBitArray<> Dry(false, Cells.Num());
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			Dry[Cell] = !Objects->GetCell(Cell).IsEmpty() || Objects->IsClear(Cell);
		}
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			uint8 Bits = Dry[Cell] ? uint8(1 << 6) : uint8(0);
			uint8 DryNear = 0;
			for (int32 Index = 0; Index < Cells[Cell].Neighbors.Num() && Index < 6; ++Index)
			{
				const int32 Neighbor = Cells[Cell].Neighbors[Index];
				Bits |= Glow[Neighbor].A > 0.0f ? uint8(1 << Index) : uint8(0);
				DryNear |= Dry[Neighbor] ? uint8(1 << Index) : uint8(0);
			}
			const int32 Pixel = (Cell / HexPlanetSurface::MarkColumns) * HexPlanetSurface::MarkColumns * 2 + (Cell % HexPlanetSurface::MarkColumns) * 2;
			BiomePixels[Pixel + 1].A = Bits;
			BiomePixels[Pixel].G = DryNear;
		}
	}
	else
	{
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			FLinearColor Color;
			float Cover = 0.0f;
			if (Objects->GetCellCanopy(Cell, Color, Cover))
			{
				CanopyPixels[Cell] = FColor(Encode(Color.R), Encode(Color.G), Encode(Color.B), uint8(FMath::RoundToInt32(FMath::Clamp(Cover, 0.0f, 1.0f) * 255.0f)));
			}
		}

		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			uint8 Bits = 0;
			for (int32 Index = 0; Index < Cells[Cell].Neighbors.Num() && Index < 6; ++Index)
			{
				Bits |= CanopyPixels[Cells[Cell].Neighbors[Index]].A > 0 ? uint8(1 << Index) : uint8(0);
			}
			const int32 Pixel = (Cell / HexPlanetSurface::MarkColumns) * HexPlanetSurface::MarkColumns * 2 + (Cell % HexPlanetSurface::MarkColumns) * 2;
			BiomePixels[Pixel + 1].A = Bits;
		}
	}

	if (!CellCanopyTexture)
	{
		CellCanopyTexture = UTexture2D::CreateTransient(HexPlanetSurface::MarkColumns, Rows, PF_B8G8R8A8,
			MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("PlanetCellCanopy")));
		CellCanopyTexture->SRGB = false;
		CellCanopyTexture->Filter = TF_Nearest;
		CellCanopyTexture->NeverStream = true;
		FTexture2DMipMap& Mip = CellCanopyTexture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), CanopyPixels.GetData(), CanopyPixels.Num() * CanopyPixels.GetTypeSize());
		Mip.BulkData.Unlock();
		CellCanopyTexture->UpdateResource();
	}
	else
	{
		UploadPixels(CellCanopyTexture, CanopyPixels, HexPlanetSurface::MarkColumns);
	}
	UploadBiomePixels();
	if (PlanetLayer == EHexPlanetLayer::Underground)
	{
		if (!CellGlowNearTexture)
		{
			CellGlowNearTexture = UTexture2D::CreateTransient(HexPlanetSurface::MarkColumns, Rows, PF_B8G8R8A8,
				MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("PlanetCellGlowNear")));
			CellGlowNearTexture->SRGB = false;
			CellGlowNearTexture->Filter = TF_Nearest;
			CellGlowNearTexture->NeverStream = true;
			FTexture2DMipMap& Mip = CellGlowNearTexture->GetPlatformData()->Mips[0];
			FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), GlowNearPixels.GetData(), GlowNearPixels.Num() * GlowNearPixels.GetTypeSize());
			Mip.BulkData.Unlock();
			CellGlowNearTexture->UpdateResource();
		}
		else
		{
			UploadPixels(CellGlowNearTexture, GlowNearPixels, HexPlanetSurface::MarkColumns);
		}
		// the walls along the passages take the pools of the floor at their foot
		UpdateWallGlow();
	}
}

void AHexPlanet::UpdateWallGlow()
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (!Settings || !Settings->bWalls || CellGlowPools.Num() != Cells.Num())
	{
		return;
	}
	const HexPlanetWalls::FWalls& Shown = *Walls;
	const int32 Rows = FMath::Max(FMath::DivideAndRoundUp(Shown.NumSamples, HexPlanetWalls::SampleColumns), 1);
	WallGlowPixels.Init(FColor(0, 0, 0, 0), HexPlanetWalls::SampleColumns * Rows);

	// the pool of light of the floor of the passage of each sample where it stands, as the glow pools of M_HexUnderground draw it there, without
	// the noise that moves its edge: a smooth bump of GlowPoolReach cell radii around the middle of the cell if it glows and of each glowing
	// neighbor, each times its strength, adding up to at most 1, in the color of the cell, or the mean of its glowing neighbors where it does not
	// glow; the distances on the sphere of the floor in cell radii (HexRadius), as the fourth UV of the floor measures them in a regular cell
	const double Reach = FMath::Max(double(Settings->GlowPoolReach), 0.5) * FMath::Max(double(Settings->HexRadius), 1.0);
	const double FloorRadius = Radius + Settings->LevelHeight;
	const auto Bump = [Reach, FloorRadius](const FVector3d& Point, const FVector3d& Middle)
	{
		const double T = FMath::Min(FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Point, Middle), -1.0, 1.0)) * FloorRadius / Reach, 1.0);
		return float(FMath::Square(1.0 - T * T));
	};
	const auto Encode = [](float Value) { return uint8(FMath::RoundToInt32(FMath::Sqrt(FMath::Clamp(Value, 0.0f, 1.0f)) * 255.0f)); };
	int32 Lit = 0;
	for (int32 Line = 0; Line < Shown.Lines.Num(); ++Line)
	{
		const TArray<HexPlanetWalls::FSample>& Samples = Shown.Lines[Line];
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			const HexPlanetWalls::FSample& Sample = Samples[Index];
			const int32 Cell = Sample.Cell;
			if (!CellGlowPools.IsValidIndex(Cell))
			{
				continue;
			}
			const FLinearColor& Own = CellGlowPools[Cell];
			const bool bGlows = Own.A > 0.0f;
			if (!bGlows && Own.R + Own.G + Own.B <= 0.0f)
			{
				continue;
			}
			const FHexSphereCell& Hex = Cells[Cell];
			const FVector3d Point = Sample.Position.GetSafeNormal();
			float Sum = bGlows ? Own.A * Bump(Point, Hex.Center) : 0.0f;
			for (const int32 Neighbor : Hex.Neighbors)
			{
				const float Strength = CellGlowPools[Neighbor].A;
				Sum += Strength > 0.0f ? Strength * Bump(Point, Cells[Neighbor].Center) : 0.0f;
			}
			const float Shape = FMath::Min(Sum, 1.0f);
			if (Shape <= 0.0f)
			{
				continue;
			}
			const int32 Pixel = Shown.GetIndex(FIntPoint(Line, Index));
			WallGlowPixels[Pixel] = FColor(Encode(Own.R * Shape), Encode(Own.G * Shape), Encode(Own.B * Shape), 255);
			++Lit;
		}
	}

	// a new texture when the walls outgrew it, as a texture keeps its size
	if (!WallGlowTexture || WallGlowTexture->GetSizeY() != Rows)
	{
		WallGlowTexture = UTexture2D::CreateTransient(HexPlanetWalls::SampleColumns, Rows, PF_B8G8R8A8,
			MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("PlanetWallGlow")));
		WallGlowTexture->SRGB = false;
		WallGlowTexture->Filter = TF_Nearest;
		WallGlowTexture->NeverStream = true;
		FTexture2DMipMap& Mip = WallGlowTexture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), WallGlowPixels.GetData(), WallGlowPixels.Num() * WallGlowPixels.GetTypeSize());
		Mip.BulkData.Unlock();
		WallGlowTexture->UpdateResource();
		for (UMaterialInstanceDynamic* Instance : {WallMaterial.Get(), WallNearMaterial.Get(), WallFarMaterial.Get()})
		{
			if (Instance)
			{
				Instance->SetTextureParameterValue(TEXT("WallGlow"), WallGlowTexture);
			}
		}
	}
	else
	{
		UploadPixels(WallGlowTexture, WallGlowPixels, HexPlanetWalls::SampleColumns);
	}
	UE_LOG(LogHexHero, Verbose, TEXT("Planet: %d of %d samples of the walls take a pool of light"), Lit, Shown.NumSamples);
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

	// the whole texture goes up, about half a megabyte
	UploadBiomePixels();
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

	// the kit is read once and kept for the rebuilds of changed levels; the underground shares the one of the surface, whose cells are as large
	const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
	if (PlanetLayer == EHexPlanetLayer::Underground && SurfacePlanet && SurfacePlanet->SurfaceKit)
	{
		ensureMsgf(FMath::IsNearlyEqual(SurfacePlanet->Settings->HexRadius, Settings->HexRadius), TEXT("HexRadius of the underground differs from the surface, whose kit it shares"));
		SurfaceKit = SurfacePlanet->SurfaceKit;
	}
	else
	{
		SurfaceKit = HexPlanetSurface::LoadKit(Config, Settings->HexRadius);
	}
	CreateBoulderComponents(Config);
	if (Settings->bSea)
	{
		CreateFloeComponents(Config);
	}
	SurfaceParams = HexPlanetSurface::FParams::Make(*Settings, *SurfaceKit, Radius);
	const double BuildStart = FPlatformTime::Seconds();

	ShownLevels = Levels;
	ShownMassifSteps = HexPlanetSurface::FindMassifSteps(Grid, *SurfaceKit, Levels);
	TArray<uint16> RiverNet = Rivers->BuildNet(Levels);
	const TSharedRef<const HexPlanetRivers::FCurve> RiverCurve = HexPlanetRivers::BuildCurve(Grid, Levels, RiverNet, Rivers->GetCurveParams());

	// the mean peak of this build stays while levels are edited, so the fade of the grid lines does not jump; the underground, which has no
	// mountains, measures by the peak of the surface, so its grid lines fade alike
	ShownReliefOverrides = ReliefOverrides;
	const double FixedPeak = PlanetLayer == EHexPlanetLayer::Underground && SurfacePlanet && SurfacePlanet->IsBuilt() ? SurfacePlanet->MeanPeak : 0.0;
	HexPlanetSurface::FBuild Build = HexPlanetSurface::Build(Grid, *SurfaceKit, SurfaceParams, Levels, ShownMassifSteps, HexPlanetSurface::GetAllChunks(Grid), FixedPeak,
		&ReliefOverrides, &RiverCurve.Get());
	Rivers->SetShownNet(MoveTemp(RiverNet), RiverCurve);
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

	// the camera finds its place only in the next frames, so the build stays for them, and the chunks it wants then take their pieces from it
	// (UpdateChunkLod), while whatever stands on the ground reads it meanwhile; without coarse meshes every chunk takes them now
	const bool bDeferPieces = !CoarseChunks.IsEmpty();
	const int32 FlatTiles = Build.FlatTiles;
	StartupSurface = MakeUnique<HexPlanetSurface::FBuild>(MoveTemp(Build));
	TriangleSource = StartupSurface.Get();
	StartupFramesLeft = 10;
	ApplySurface(*StartupSurface, bDeferPieces);
	Rivers->UpdateBridges();
	if (!bDeferPieces)
	{
		TriangleSource = nullptr;
		StartupSurface.Reset();
	}

	int32 LevelCounts[5] = {};
	for (int32 Cell = 0; Cell < Grid.GetCells().Num(); ++Cell)
	{
		++LevelCounts[GetCellLevel(Cell) + 2];
	}

	UE_LOG(LogHexHero, Log, TEXT("Planet: %d pieces from %d kits (%d unread), %d flat tiles, %d triangles, %d vertices, cells by level from -2: %d %d %d %d %d, mean peak %.1f, level height %.1f"),
		SurfaceKit->Pieces.Num(), SurfaceKit->Levels.Num(), SurfaceKit->UnreadMeshes, FlatTiles, NumTriangles, NumVertices, LevelCounts[0], LevelCounts[1], LevelCounts[2],
		LevelCounts[3], LevelCounts[4], MeanPeak, Settings->LevelHeight);
	UE_LOG(LogHexHero, Log, TEXT("Planet: kit read in %.0f ms, surface of %d chunks, %d coarse triangles, built in %.0f ms, shown in %.0f ms"),
		(BuildStart - KitStart) * 1000.0, Grid.GetNumChunks(), NumCoarseTriangles, (ApplyStart - BuildStart) * 1000.0, (FPlatformTime::Seconds() - ApplyStart) * 1000.0);
}

void AHexPlanet::ApplySurface(HexPlanetSurface::FBuild& Build, bool bStartup)
{
	// the triangles picking kept are those of the build before
	PickTriangles.Reset();

	// what the cells are built from, by which the triangles of a cell are built again when no build holds them (GetCellTriangles): the factor
	// of the height depends on the place and the level of a cell alone, so a build of some chunks gives it for all, as the heights of the corners
	if (Build.CellRelief.Num() == CellRelief.Num())
	{
		CellRelief = Build.CellRelief;
	}
	if (Build.CornerLow.Num() == Grid.GetCorners().Num())
	{
		ShownCornerLow = Build.CornerLow;
	}

	// the real coast of these chunks, for the sea field
	ChunkCoasts.SetNum(Grid.GetNumChunks());
	for (int32 Slot = 0; Slot < Build.Chunks.Num() && Slot < Build.Coasts.Num(); ++Slot)
	{
		ChunkCoasts[Build.Chunks[Slot]] = MoveTemp(Build.Coasts[Slot]);
	}

	// every chunk gets its coarse mesh and keeps the bounds of its pieces, and those near the camera their pieces, packed for the GPU in
	// parallel; at the start UpdateChunkLod gives them their pieces once the camera has its place
	const int32 NumSlots = Build.Chunks.Num();
	TArray<uint8> WantedSlots;
	WantedSlots.Init(0, NumSlots);
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		const int32 Chunk = Build.Chunks[Slot];
		ChunkBounds[Chunk] = GetPieceBounds(Build, Slot);
		// a hidden layer keeps no pieces, unless a trip to it asks for them
		WantedSlots[Slot] = !bStartup && (bPresented || PrefetchCamera.IsSet()) && (ChunkPieces[Chunk] == 2 || WantsPieces(Chunk)) ? 1 : 0;
	}
	TArray<FHexPlanetMeshSectionRef> Prepared;
	Prepared.SetNum(NumSlots * 3);
	ParallelFor(NumSlots * 3, [&](int32 Index)
	{
		const int32 Slot = Index / 3;
		const int32 Part = Index % 3;
		if (Part == 2 && Build.CoarseSections.IsValidIndex(Slot) && CoarseChunks.IsValidIndex(Build.Chunks[Slot]))
		{
			Prepared[Index] = PrepareChunkSection(Build.CoarseSections[Slot], TEXT("CoarseChunk"), Build.Chunks[Slot]);
		}
		else if (Part < 2 && WantedSlots[Slot])
		{
			Prepared[Index] = PrepareChunkSection(Build.Sections[Slot * 2 + Part], TEXT("SurfaceChunk"), Build.Chunks[Slot]);
		}
	});

	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		// a whole chunk is replaced, as the counts of its vertices change, which recreates the render state of this chunk alone, and pieces
		// that a stream builds of it now are dropped
		const int32 Chunk = Build.Chunks[Slot];
		++ChunkGenerations[Chunk];
		if (WantedSlots[Slot])
		{
			SetChunkPieces(Chunk, MoveTemp(Prepared[Slot * 3]), MoveTemp(Prepared[Slot * 3 + 1]));
			SetWireSection(Chunk, 0, &Build.Sections[Slot * 2]);
			SetWireSection(Chunk, 1, &Build.Sections[Slot * 2 + 1]);
		}
		else
		{
			DropChunkPieces(Chunk);
		}

		if (CoarseChunks.IsValidIndex(Chunk) && Build.CoarseSections.IsValidIndex(Slot))
		{
			UHexPlanetMeshComponent* CoarseMesh = CoarseChunks[Chunk];
			CoarseMesh->SetSection(0, MoveTemp(Prepared[Slot * 3 + 2]));
			CoarseMesh->SetMaterial(0, GetChunkMaterial(Chunk, 2, bAppliedCut, AppliedView.Target));
			SetWireSection(Chunk, 2, &Build.CoarseSections[Slot]);
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

	// the objects stand on the ground of their cells, which these chunks changed, read from this build
	const HexPlanetSurface::FBuild* OldSource = TriangleSource;
	TriangleSource = &Build;
	// the walls follow the levels shown, and the objects of the chunks whose walls changed stand on them too
	TArray<int32> ObjectChunks = Build.Chunks;
	UpdateWalls(ObjectChunks);
	Objects->RebuildChunks(ObjectChunks);
	TriangleSource = OldSource;

	// the debug view puts its materials and visibility on the sections again
	bSurfaceChanged = true;
}

void AHexPlanet::LogMemory(const FString& FrameTimes, bool bProcess)
{
	UE_LOG(LogHexHero, Log, TEXT("Planet memory of the %s%s: %s"), GetPlanetLayerName(PlanetLayer), bPresented ? TEXT(" (shown)") : TEXT(" (hidden)"), *FrameTimes);

	// the pieces by level, as the cells hold them
	int64 LevelTriangles[5] = {};
	for (int32 Cell = 0; Cell < CellMeshes.Num() && Cell < ShownLevels.Num(); ++Cell)
	{
		LevelTriangles[FMath::Clamp(int32(ShownLevels[Cell]) + 2, 0, 4)] += CellMeshes[Cell].NumIndices / 3;
	}

	// the copies of the meshes: the components keep them on the GPU alone, with their vertex and index buffers, and the copies the wireframe
	// draws keep theirs on the CPU too, as UProceduralMeshComponent does
	struct FCopies
	{
		int64 Sections = 0;
		int64 Vertices = 0;
		int64 Indices = 0;
		int64 GpuBytes = 0;
	};
	const auto Measure = [](const TArray<TObjectPtr<UHexPlanetMeshComponent>>& Chunks)
	{
		FCopies Copies;
		for (const UHexPlanetMeshComponent* Chunk : Chunks)
		{
			for (int32 Index = 0; Chunk && Index < Chunk->GetNumSections(); ++Index)
			{
				const UHexPlanetMeshComponent::FSectionStats Stats = Chunk->GetSectionStats(Index);
				if (Stats.Indices > 0)
				{
					++Copies.Sections;
					Copies.Vertices += Stats.Vertices;
					Copies.Indices += Stats.Indices;
					Copies.GpuBytes += Stats.GpuBytes;
				}
			}
		}
		return Copies;
	};
	const FCopies Pieces = Measure(SurfaceChunks);
	const FCopies Coarse = Measure(CoarseChunks);
	int64 WireBytes = 0;
	for (const TArray<TObjectPtr<UProceduralMeshComponent>>* Wires : { &WireChunks, &WireCoarseChunks })
	{
		for (UProceduralMeshComponent* Wire : *Wires)
		{
			for (int32 Index = 0; Wire && Index < Wire->GetNumSections(); ++Index)
			{
				const FProcMeshSection* Section = Wire->GetProcMeshSection(Index);
				WireBytes += Section ? Section->ProcVertexBuffer.GetAllocatedSize() + Section->ProcIndexBuffer.GetAllocatedSize() : 0;
			}
		}
	}

	int32 LodCounts[3] = {};
	for (const uint8 State : ChunkLodStates)
	{
		++LodCounts[FMath::Min<int32>(State, 2)];
	}
	int32 PieceCounts[3] = {};
	for (const uint8 State : ChunkPieces)
	{
		++PieceCounts[FMath::Min<int32>(State, 2)];
	}
	int64 SurfaceTriangles = 0;
	for (const int64 Count : LevelTriangles)
	{
		SurfaceTriangles += Count;
	}

	const double MB = 1024.0 * 1024.0;
	UE_LOG(LogHexHero, Log, TEXT("Planet memory: %d chunks, %d drawing their pieces, %d both in the band, %d their coarse meshes; %d hold their pieces on the GPU, %d are building them, %d have none"),
		SurfaceChunks.Num(), LodCounts[0], LodCounts[1], LodCounts[2], PieceCounts[2], PieceCounts[1], PieceCounts[0]);
	UE_LOG(LogHexHero, Log, TEXT("Planet memory: the surface has %lld triangles, by level from the deep sea %lld %lld %lld %lld %lld; on the GPU pieces in %lld sections, %lld triangles and %lld vertices, coarse meshes in %lld sections, %lld triangles and %lld vertices"),
		SurfaceTriangles, LevelTriangles[0], LevelTriangles[1], LevelTriangles[2], LevelTriangles[3], LevelTriangles[4], Pieces.Sections, Pieces.Indices / 3, Pieces.Vertices,
		Coarse.Sections, Coarse.Indices / 3, Coarse.Vertices);
	UE_LOG(LogHexHero, Log, TEXT("Planet memory: pieces 0.0 MB on the CPU in the components, 0.0 MB on the CPU in their scene proxies, %.1f MB of vertices and indices on the GPU; coarse meshes 0.0, 0.0 and %.1f MB; copies of the wireframe %.1f MB on the CPU"),
		Pieces.GpuBytes / MB, Coarse.GpuBytes / MB, WireBytes / MB);

	// the walls: coarse in every chunk, whole near the camera, about 26 bytes a triangle on the GPU (a vertex of 40 bytes, with the fifth UV, to two
	// triangles, and indices of 16 bits)
	int64 WallWholeChunks = 0;
	int64 WallWholeTotal = 0;
	int64 WallCoarseTotal = 0;
	for (int32 Chunk = 0; Chunk < WallWhole.Num(); ++Chunk)
	{
		WallWholeChunks += WallWhole[Chunk];
		WallWholeTotal += WallWholeTriangles[Chunk];
		WallCoarseTotal += WallCoarseTriangles[Chunk];
	}
	if (WallCoarseTotal > 0)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet memory: walls whole in %lld chunks near the camera, %lld triangles, about %.1f MB on the GPU; coarse in every chunk, %lld triangles, about %.1f MB"),
			WallWholeChunks, WallWholeTotal, WallWholeTotal * 26.0 / MB, WallCoarseTotal, WallCoarseTotal * 26.0 / MB);
	}

	if (!bProcess)
	{
		return;
	}

	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	UE_LOG(LogHexHero, Log, TEXT("Planet memory: process %.0f MB in use (peak %.0f), %.0f MB committed (peak %.0f)"),
		Stats.UsedPhysical / MB, Stats.PeakUsedPhysical / MB, Stats.UsedVirtual / MB, Stats.PeakUsedVirtual / MB);

	// the acceleration structures of ray tracing of the chunks, and all the memory of the RHI
	GEngine->Exec(GetWorld(), TEXT("D3D12.DumpRayTracingGeometries all SurfaceChunk"));
	GEngine->Exec(GetWorld(), TEXT("D3D12.DumpRayTracingGeometries all CoarseChunk"));
	GEngine->Exec(GetWorld(), TEXT("rhi.DumpMemory"));
	UE_LOG(LogHexHero, Log, TEXT("Planet memory: done"));
}

int32 AHexPlanet::FindCellAlongRay(const FVector& Origin, const FVector& Direction, FVector* OutPoint) const
{
	if (Radius <= 0.0 || !Settings)
	{
		return INDEX_NONE;
	}

	const FVector RayDirection = Direction.GetSafeNormal();
	const double Start = FHexSphereGrid::IntersectSphere(Origin, RayDirection, Radius + 3.0 * Settings->LevelHeight);

	if (Start < 0.0)
	{
		return INDEX_NONE;
	}

	// the ray ends on the water, or where it leaves the sphere of the highest tops again when it passes over the sea; without the sea, as
	// underground, on the deepest floor
	const bool bSea = Settings->bSea;
	double End = FHexSphereGrid::IntersectSphere(Origin, RayDirection, bSea ? Radius : Radius - Settings->DeepDepth);
	if (End < 0.0)
	{
		End = 2.0 * FVector::DotProduct(-Origin, RayDirection) - Start;
	}

	// where a higher cell holds its sides, a lower one rises to them inside itself, as a void of the underground carries its cliff, so the
	// tops of the cells alone would put a ray that meets a cliff into the passage behind it
	if (Settings->EdgeRule == EHexEdgeRule::HigherHolds && !bSea)
	{
		return FindHeldCellAlongRay(Origin, RayDirection, Start, End, OutPoint);
	}

	// the sea is seen at its water, whose grid lines and outlines are drawn there, and not at its bed, which a slanted ray would reach cells
	// further on; land at the top of its level, and without the sea every cell at its top
	const auto IsUnderTop = [this, &Origin, &RayDirection, bSea](double Distance, int32& InOutCell)
	{
		const FVector Point = Origin + RayDirection * Distance;
		InOutCell = Grid.FindCell(Point, InOutCell);
		// a little above, as the last step lies on the water, where rounding alone could leave it outside and lose the cell for a frame
		return InOutCell != INDEX_NONE && Point.Size() <= (GetCellLevel(InOutCell) < 0 && bSea ? Radius : GetCellTopRadius(InOutCell)) + 0.01;
	};

	// steps of a quarter of a cell find the first cell whose top the ray goes under, so a high cell in front hides a low one behind it,
	// and halving the last step finds where
	const double Step = FMath::Max(Settings->HexRadius * 0.25, 1.0);
	int32 Cell = INDEX_NONE;
	double Previous = Start;

	for (double Distance = Start; ; Distance = FMath::Min(Distance + Step, End))
	{
		int32 Hit = Cell;

		if (IsUnderTop(Distance, Hit))
		{
			double Low = Previous;
			double High = Distance;

			for (int32 Halving = 0; Halving < 6 && High - Low > 1.0; ++Halving)
			{
				const double Middle = 0.5 * (Low + High);
				int32 MiddleCell = Hit;

				if (IsUnderTop(Middle, MiddleCell))
				{
					High = Middle;
					Hit = MiddleCell;
				}
				else
				{
					Low = Middle;
				}
			}
			if (OutPoint)
			{
				*OutPoint = Origin + RayDirection * High;
			}
			return Hit;
		}

		Cell = Hit;
		Previous = Distance;

		if (Distance >= End)
		{
			return INDEX_NONE;
		}
	}
}

int32 AHexPlanet::FindHeldCellAlongRay(const FVector& Origin, const FVector& RayDirection, double Start, double End, FVector* OutPoint) const
{
	// the highest a cell reaches is the highest top among it and its neighbors, as its sides lie at the higher of two tops, and the floor of the
	// passages rises above its top by up to FloorNoise
	const auto GetHighest = [this](int32 Cell)
	{
		double Highest = GetCellTopRadius(Cell);
		for (const int32 Neighbor : Grid.GetCells()[Cell].Neighbors)
		{
			Highest = FMath::Max(Highest, GetCellTopRadius(Neighbor));
		}
		return Highest + FMath::Max(Settings->FloorNoise, 0.0f);
	};

	// the nearest of the triangles that face the ray, in doubles, as the planet is large against its cells
	const auto Intersect = [&Origin, &RayDirection](const FVector3f& A, const FVector3f& B, const FVector3f& C, double& OutDistance)
	{
		const FVector3d EdgeB = FVector3d(B) - FVector3d(A);
		const FVector3d EdgeC = FVector3d(C) - FVector3d(A);
		const FVector3d Across = FVector3d::CrossProduct(RayDirection, EdgeC);
		const double Determinant = FVector3d::DotProduct(EdgeB, Across);
		if (FMath::Abs(Determinant) < 1.0e-12)
		{
			return false;
		}
		const FVector3d FromA = Origin - FVector3d(A);
		const double U = FVector3d::DotProduct(FromA, Across) / Determinant;
		const FVector3d Up = FVector3d::CrossProduct(FromA, EdgeB);
		const double V = FVector3d::DotProduct(RayDirection, Up) / Determinant;
		// a little past their edges, so a ray between two triangles of a seam never falls through
		if (U < -1.0e-5 || V < -1.0e-5 || U + V > 1.0 + 1.0e-5)
		{
			return false;
		}
		OutDistance = FVector3d::DotProduct(EdgeC, Up) / Determinant;
		return OutDistance > 0.0;
	};

	// a cell lies under the ray around a point, or next to it, while the ray is below the highest it reaches; each is tested once against its
	// triangles, and a step of a quarter of a cell never passes one by, as its neighbors are tested too
	if (PickTriangles.Num() > 512)
	{
		PickTriangles.Reset();
	}
	const double Step = FMath::Max(Settings->HexRadius * 0.25, 1.0);
	double Best = TNumericLimits<double>::Max();
	int32 BestCell = INDEX_NONE;
	TArray<int32, TInlineAllocator<64>> Tested;
	int32 Cell = INDEX_NONE;

	for (double Distance = Start; ; Distance = FMath::Min(Distance + Step, End))
	{
		const FVector Point = Origin + RayDirection * Distance;
		Cell = Grid.FindCell(Point, Cell);
		if (Cell != INDEX_NONE)
		{
			const double PointRadius = Point.Size();
			TArray<int32, TInlineAllocator<7>> Near{Cell};
			Near.Append(Grid.GetCells()[Cell].Neighbors);
			for (const int32 Each : Near)
			{
				if (Tested.Contains(Each) || PointRadius > GetHighest(Each) + 0.01)
				{
					continue;
				}
				Tested.Add(Each);
				TArray<FVector3f>* Triangles = PickTriangles.Find(Each);
				if (!Triangles)
				{
					Triangles = &PickTriangles.Add(Each);
					GetCellTriangles(Each, *Triangles);
				}
				for (int32 Index = 0; Index + 2 < Triangles->Num(); Index += 3)
				{
					double Hit = 0.0;
					if (Intersect((*Triangles)[Index], (*Triangles)[Index + 1], (*Triangles)[Index + 2], Hit) && Hit < Best)
					{
						Best = Hit;
						BestCell = Each;
					}
				}
			}
		}

		// nothing nearer can come once the ray has gone past the nearest hit, as the cell of a hit is tested at the first step past it
		if (Best <= Distance || Distance >= End)
		{
			break;
		}
	}

	if (BestCell != INDEX_NONE && OutPoint)
	{
		*OutPoint = Origin + RayDirection * Best;
	}
	return BestCell;
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
	int32 Skipped = 0;
	TSet<int32> SkippedCells;
	for (int32 Mesh = 0; Mesh < BoulderComponents.Num(); ++Mesh)
	{
		TArray<FTransform> Transforms;
		TArray<float> Rocks;
		for (const TArray<HexPlanetSurface::FBoulder>& Chunk : BouldersByChunk)
		{
			for (const HexPlanetSurface::FBoulder& Boulder : Chunk)
			{
				if (Boulder.Mesh != Mesh)
				{
					continue;
				}
				// the cell of an entrance stays clear, as its objects do, so the marker shows
				if (Objects && Objects->IsClear(Boulder.Ground))
				{
					++Skipped;
					SkippedCells.Add(Boulder.Ground);
					continue;
				}
				Transforms.Add(FTransform(FQuat(Boulder.Rotation), FVector(Boulder.Position), FVector(Boulder.Scale)));
				Rocks.Add(GetRock(Boulder.Cell));
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
	TArray<int32> Cleared = SkippedCells.Array();
	Cleared.Sort();
	UE_LOG(LogHexHero, Log, TEXT("Planet: %d boulders%s"), Total, Skipped > 0 ? *FString::Printf(TEXT(", %d left off the cells of entrances %s"), Skipped,
		*FString::JoinBy(Cleared, TEXT(" "), [](int32 Cell) { return FString::FromInt(Cell); })) : TEXT(""));
}

void AHexPlanet::UpdateMarkers()
{
	if (!bBuilt || !Settings)
	{
		return;
	}

	// a pit on the surface, its rim and its mouth; stairs up a rock and a shaft of daylight over it underground
	const bool bSurface = PlanetLayer == EHexPlanetLayer::Surface;
	const TArray<TSoftObjectPtr<UStaticMesh>> Meshes = bSurface ? TArray<TSoftObjectPtr<UStaticMesh>>{Settings->EntranceMesh, Settings->EntranceHoleMesh}
		: TArray<TSoftObjectPtr<UStaticMesh>>{Settings->ExitMesh, Settings->ShaftMesh};
	if (MarkerComponents.IsEmpty())
	{
		for (int32 Index = 0; Index < Meshes.Num(); ++Index)
		{
			UStaticMesh* Mesh = Meshes[Index].LoadSynchronous();
			if (!Mesh)
			{
				UE_LOG(LogHexHero, Warning, TEXT("Planet: marker mesh %s is missing, run Tools/HexPlanetKit/import_planet_markers.py"), *Meshes[Index].ToString());
				MarkerComponents.Add(nullptr);
				continue;
			}
			UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Markers%d"), Index));
			Component->SetupAttachment(Surface);
			Component->SetStaticMesh(Mesh);
			// the shaft is light alone, and the mouth of a pit draws what lies below it, so neither casts a shadow nor needs anything in ray tracing
			const bool bShaft = !bSurface && Index == 1;
			const bool bHole = bSurface && Index == 1;
			// the values of the config the materials take, such as MarkerGlow, and the radius of the mouth of a pit, in which it draws the shaft
			for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
			{
				if (UMaterialInterface* Parent = Mesh->GetMaterial(Slot))
				{
					UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, this);
					HexMaterialConfig::Apply(Instance, Settings);
					if (bHole)
					{
						Instance->SetScalarParameterValue(TEXT("PitRadius"), float(Mesh->GetBounds().BoxExtent.X * Settings->MarkerSize));
					}
					Component->SetMaterial(Slot, Instance);
				}
			}
			// the rock layer and the color of the network
			Component->NumCustomDataFloats = 4;
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCastShadow(!bShaft && !bHole);
			Component->SetVisibleInRayTracing(!bShaft && !bHole);
			Component->bAffectDistanceFieldLighting = false;
			Component->RegisterComponent();
			MarkerComponents.Add(Component);
		}
	}

	for (UHierarchicalInstancedStaticMeshComponent* Component : MarkerComponents)
	{
		if (Component)
		{
			Component->ClearInstances();
		}
	}

	// the colors of the networks are those of the surface, so both layers and the editor agree
	const AHexPlanet* SurfacePlanet = GetLayerPlanet(EHexPlanetLayer::Surface);
	const UHexPlanetConfig* Colors = SurfacePlanet && SurfacePlanet->Settings ? SurfacePlanet->Settings.Get() : Settings.Get();
	const TArray<FHexEntrance>& All = GetEntrances();
	int32 Placed = 0;
	int32 Unusable = 0;
	// the shaft of daylight falls onto the top of the stairs of an exit
	const UStaticMesh* ExitStairs = !bSurface && MarkerComponents.IsValidIndex(0) && MarkerComponents[0] ? MarkerComponents[0]->GetStaticMesh() : nullptr;
	const double ShaftLift = ExitStairs ? FMath::Max(ExitStairs->GetBoundingBox().Max.Z, 0.0) * Settings->MarkerSize : 0.0;

	for (int32 Index = 0; Index < All.Num(); ++Index)
	{
		// an entrance that cannot be used stands in ProblemColor, so the editor shows it, but one on the same cell as another is not drawn twice
		const int32 Cell = All[Index].Cell;
		const HexPlanetUnderground::EEntranceProblem Problem = GetEntranceProblem(Index);
		if (!Grid.GetCells().IsValidIndex(Cell) || Problem == HexPlanetUnderground::EEntranceProblem::NoCell || Problem == HexPlanetUnderground::EEntranceProblem::Duplicate)
		{
			continue;
		}

		// on the ground under the middle of the cell, from its triangles; an exit in the void stands at the top of the passages around it, as on
		// the floor of the hole it would hardly show the problem it marks
		const FVector3d Up = Grid.GetCells()[Cell].Center;
		const bool bInVoid = !bSurface && !HexPlanetUnderground::IsPassage(int8(GetCellLevel(Cell)));
		const double Ground = bInVoid ? Radius + (HexPlanetUnderground::PassageLevel + 1) * Settings->LevelHeight : FindGroundRadius(Cell, Up);

		// turned by the cell, so neighboring markers do not all face alike
		const FQuat Lean = FQuat::FindBetweenNormals(FVector::UpVector, FVector(Up));
		const FQuat Spin(FVector::UpVector, (Cell * 2654435761u % 6283u) / 1000.0);
		const FLinearColor Color = Problem == HexPlanetUnderground::EEntranceProblem::None ? Colors->GetNetworkColor(All[Index].Network) : Colors->ProblemColor;
		const float Rock = float(GetCellRockLayer(Cell));
		for (int32 Mesh = 0; Mesh < MarkerComponents.Num(); ++Mesh)
		{
			UHierarchicalInstancedStaticMeshComponent* Component = MarkerComponents[Mesh];
			if (!Component)
			{
				continue;
			}
			const bool bShaft = !bSurface && Mesh == 1;
			const FVector Scale(Settings->MarkerSize, Settings->MarkerSize, bShaft ? Settings->ShaftHeight : Settings->MarkerSize);
			const int32 Instance = Component->AddInstance(FTransform(Lean * Spin, FVector(Up * (Ground + (bShaft ? ShaftLift : 0.0))), Scale), false);
			Component->SetCustomDataValue(Instance, 0, Rock, false);
			Component->SetCustomDataValue(Instance, 1, Color.R, false);
			Component->SetCustomDataValue(Instance, 2, Color.G, false);
			Component->SetCustomDataValue(Instance, 3, Color.B, false);
		}
		++Placed;
		Unusable += Problem == HexPlanetUnderground::EEntranceProblem::None ? 0 : 1;
	}

	for (UHierarchicalInstancedStaticMeshComponent* Component : MarkerComponents)
	{
		if (Component)
		{
			Component->MarkRenderStateDirty();
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: %d %s of %d entrances marked on the %s, %d of them cannot be used"), Placed, bSurface ? TEXT("entrances") : TEXT("exits"), All.Num(),
		GetPlanetLayerName(PlanetLayer), Unusable);

	// the shade of the surface lifts around the entrances that can be used
	UpdateLightField();
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

	const auto AddFloe = [&](int32 Cell, const FVector3d& Position, double Size, FRandomStream& Random)
	{
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
		FloesByCell[Cell].Add({Position, Size});
		if (Transforms.IsValidIndex(Mesh))
		{
			Transforms[Mesh].Add(FTransform(FQuat(Lean * Tilt * Spin), FVector(Position), FVector(Size)));
			++Total;
		}
	};

	// where a frozen river reaches the sea its ice goes on as broken floes, smaller and further apart out to RiverMouthFloeReach, before the
	// floes of the pack, which keep away from them
	const TSharedPtr<const HexPlanetRivers::FCurve> RiverCurve = Rivers ? Rivers->GetShownCurve() : nullptr;
	const auto IsFrozenLand = [this](int32 Cell) { return IsRiverFrozenAt(Cell); };
	if (RiverCurve && Config.RiverMouthFloes > 0)
	{
		for (const HexPlanetRivers::FCurve::FMouth& Mouth : RiverCurve->Mouths)
		{
			if (!Cells.IsValidIndex(Mouth.CellA) || !Cells.IsValidIndex(Mouth.CellB) || (!IsFrozenLand(Mouth.CellA) && !IsFrozenLand(Mouth.CellB)))
			{
				continue;
			}
			FRandomStream Random(int32(uint32(Mouth.CellA) * 2654435761u ^ uint32(Mouth.CellB) * 40503u));
			const FVector3d Across = FVector3d::CrossProduct(Mouth.Position, Mouth.Direction);
			const int32 Before = Total;
			ON_SCOPE_EXIT
			{
				UE_LOG(LogHexHero, Verbose, TEXT("Planet: %d floes at the frozen mouth between cells %d and %d"), Total - Before, Mouth.CellA, Mouth.CellB);
			};
			for (int32 Index = 0; Index < Config.RiverMouthFloes; ++Index)
			{
				const double Share = (Index + Random.FRandRange(0.2, 0.8)) / Config.RiverMouthFloes;
				const double Size = Config.RiverMouthFloeSize * FMath::Lerp(1.0, 0.3, Share) * Random.FRandRange(0.75, 1.1);
				const double Out = Share * Config.RiverMouthFloeReach + Size * 0.5;
				const double Side = Random.FRandRange(-1.0, 1.0) * Config.RiverWidth * (0.25 + Share * 0.6);
				const FVector3d Position = (Mouth.Position + (Mouth.Direction * Out + Across * Side) / Radius).GetSafeNormal() * Radius;
				// the first ones lie on the edge of the water, where the ground falls into it, the others out on the sea
				const int32 Cell = Grid.FindCell(Position.GetSafeNormal(), Mouth.CellA);
				if (Cell == INDEX_NONE || (ShownLevels[Cell] >= 0 && Out > Config.RiverWidth))
				{
					continue;
				}
				bool bFree = true;
				for (const FFloe& Floe : FloesByCell[Cell])
				{
					bFree &= FVector3d::DistSquared(Floe.Position, Position) >= FMath::Square((Floe.Size + Size) * 0.9);
				}
				if (bFree)
				{
					AddFloe(Cell, Position, Size, Random);
				}
			}
		}
	}

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

				Covered += UE_DOUBLE_PI * Size * Size * FillShare;
				AddFloe(Cell, Position, Size, Random);
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

bool AHexPlanet::IsRiverFrozenAt(int32 Cell) const
{
	const TCHAR Letter = GetCellBiomeLetter(Cell);
	return Letter != 0 && Settings->RiverFrozenBiomes.Contains(FString::Chr(Letter), ESearchCase::IgnoreCase);
}

void AHexPlanet::UpdateRiverMouths(const FVector3d& Below)
{
	static const TArray<FName> PositionNames = []()
	{
		TArray<FName> Names;
		for (int32 Index = 0; Index < MaxRiverMouths; ++Index)
		{
			Names.Add(FName(*FString::Printf(TEXT("RiverMouth%d"), Index)));
		}
		return Names;
	}();
	static const TArray<FName> DirectionNames = []()
	{
		TArray<FName> Names;
		for (int32 Index = 0; Index < MaxRiverMouths; ++Index)
		{
			Names.Add(FName(*FString::Printf(TEXT("RiverMouthDir%d"), Index)));
		}
		return Names;
	}();
	if (!WaterMaterial || !Rivers)
	{
		return;
	}

	// the open mouths nearest the point under the camera, as the water is seen closest there; a frozen river ends in floes instead
	const TSharedPtr<const HexPlanetRivers::FCurve> Curve = Rivers->GetShownCurve();
	TArray<TPair<double, int32>, TInlineAllocator<64>> Nearest;
	if (Curve)
	{
		for (int32 Index = 0; Index < Curve->Mouths.Num(); ++Index)
		{
			const HexPlanetRivers::FCurve::FMouth& Mouth = Curve->Mouths[Index];
			if (!IsRiverFrozenAt(Mouth.CellA) && !IsRiverFrozenAt(Mouth.CellB))
			{
				Nearest.Add({-FVector3d::DotProduct(Mouth.Position, Below), Index});
			}
		}
	}
	Nearest.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	TArray<int32> Chosen;
	for (int32 Index = 0; Index < Nearest.Num() && Index < MaxRiverMouths; ++Index)
	{
		Chosen.Add(Nearest[Index].Value);
	}
	Chosen.Sort();
	if (Chosen == AppliedRiverMouths && Curve.Get() == AppliedRiverCurve)
	{
		return;
	}
	AppliedRiverMouths = Chosen;
	AppliedRiverCurve = Curve.Get();

	for (int32 Slot = 0; Slot < MaxRiverMouths; ++Slot)
	{
		if (Chosen.IsValidIndex(Slot))
		{
			const HexPlanetRivers::FCurve::FMouth& Mouth = Curve->Mouths[Chosen[Slot]];
			const FVector3d Position = Mouth.Position * Radius;
			WaterMaterial->SetVectorParameterValue(PositionNames[Slot], FLinearColor(float(Position.X), float(Position.Y), float(Position.Z), 1.0f));
			WaterMaterial->SetVectorParameterValue(DirectionNames[Slot], FLinearColor(float(Mouth.Direction.X), float(Mouth.Direction.Y), float(Mouth.Direction.Z), 0.0f));
		}
		else
		{
			WaterMaterial->SetVectorParameterValue(PositionNames[Slot], FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		}
	}
}

void AHexPlanet::ApplyRiverTextures()
{
	for (UMaterialInstanceDynamic* Target : { SurfaceMaterial.Get(), SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get() })
	{
		if (Target && Rivers && Rivers->GetTexture() && Rivers->GetSegmentTexture())
		{
			Target->SetTextureParameterValue(TEXT("CellRivers"), Rivers->GetTexture());
			Target->SetTextureParameterValue(TEXT("RiverSegments"), Rivers->GetSegmentTexture());
		}
	}
}

void AHexPlanet::SetRiverShape(TOptional<FVector3d> Shape)
{
	// before the build, as from -ExecCmds, the build takes it
	Rivers->SetShapeOverride(Shape);
	if (!bBuilt)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: the rivers will be built with the shape %s"), Shape.IsSet() ? *Shape->ToString() : TEXT("of DA_HexPlanet"));
		return;
	}
	const HexPlanetRivers::FCurveParams Params = Rivers->GetCurveParams();
	UE_LOG(LogHexHero, Log, TEXT("Planet: rivers cut corners by %.2f, relax %d times and leave their sides by at most %.0f%s; building the surface again"),
		Params.CornerCut, Params.Relax, Params.MaxOffset, Params.MaxOffset > 0.0 ? TEXT("") : TEXT(" (no limit)"));
	bRebuildAllPending = true;
	bLevelsPending = true;
	StartRebuild();
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
			FColor& Pixel = MarkPixels[Cell];
			bChanged |= Pixel.R != 0 || Pixel.G != 0;
			Pixel.R = Pixel.G = 0;
		}
	}

	MarkedCells.Reset();
	for (const TPair<int32, FColor>& Mark : Marks)
	{
		if (MarkPixels.IsValidIndex(Mark.Key))
		{
			FColor& Pixel = MarkPixels[Mark.Key];
			bChanged |= Pixel.R != Mark.Value.R || Pixel.G != Mark.Value.G;
			Pixel.R = Mark.Value.R;
			Pixel.G = Mark.Value.G;
			MarkedCells.Add(Mark.Key);
		}
	}

	if (bChanged)
	{
		UploadCellMarks();
	}
}

void AHexPlanet::SetWaterHiddenCells(TConstArrayView<int32> Cells, bool bAll)
{
	if (MarkPixels.IsEmpty())
	{
		return;
	}

	if (bAllWaterHidden || bAll)
	{
		for (FColor& Pixel : MarkPixels)
		{
			Pixel.B = bAll ? 255 : 0;
		}
	}
	else
	{
		for (const int32 Cell : WaterHiddenCells)
		{
			MarkPixels[Cell].B = 0;
		}
	}

	for (const int32 Cell : WaterRimCells)
	{
		MarkPixels[Cell].A = 0;
	}
	WaterRimCells.Reset();

	WaterHiddenCells.Reset();
	if (!bAll)
	{
		for (const int32 Cell : Cells)
		{
			if (MarkPixels.IsValidIndex(Cell))
			{
				MarkPixels[Cell].B = 255;
				WaterHiddenCells.Add(Cell);
			}
		}
	}

	// the rim of a hole of more than one cell, in alpha, as the outlines mark the sides of an edge in both cells, of which only the water grid
	// outside the hole draws its half
	const TArray<FHexSphereCell>& GridCells = Grid.GetCells();
	if (WaterHiddenCells.Num() > 1)
	{
		for (const int32 Inside : WaterHiddenCells)
		{
			for (int32 Side = 0; Side < GridCells[Inside].Neighbors.Num(); ++Side)
			{
				const int32 Outside = GridCells[Inside].Neighbors[Side];
				if (!WaterHiddenCells.Contains(Outside))
				{
					MarkPixels[Inside].A |= uint8(1 << Side);
					MarkPixels[Outside].A |= uint8(1 << GridCells[Outside].Neighbors.Find(Inside));
					WaterRimCells.AddUnique(Inside);
					WaterRimCells.AddUnique(Outside);
				}
			}
		}
	}

	bAllWaterHidden = bAll;
	UploadCellMarks();
}

void AHexPlanet::UploadCellMarks()
{
	if (!CellMarks)
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

void AHexPlanet::SetFadeFocus(bool bOnPlanet, const FVector& Point)
{
	const FLinearColor Center = bOnPlanet ? FLinearColor(float(Point.X), float(Point.Y), float(Point.Z), 1.0f) : FLinearColor::Transparent;
	for (UMaterialInstanceDynamic* Instance : {WallMaterial.Get(), WallNearMaterial.Get(), WallFarMaterial.Get()})
	{
		if (Instance)
		{
			Instance->SetVectorParameterValue(TEXT("WallFadeCenter"), Center);
		}
	}
	// and what stands on the walls dissolves with them (M_HexPlanetCaveWall)
	if (Objects)
	{
		Objects->SetFadeFocus(Center);
	}
}

UMaterialInterface* AHexPlanet::GetWallObjectMaterial()
{
	// once the script has made it, so a planet without it draws what stands on the walls in the material of its set
	if (!LoadedWallObjectMaterial && !WallObjectMaterialAsset.IsNull() && FPackageName::DoesPackageExist(WallObjectMaterialAsset.ToSoftObjectPath().GetLongPackageName()))
	{
		LoadedWallObjectMaterial = WallObjectMaterialAsset.LoadSynchronous();
	}
	return LoadedWallObjectMaterial;
}

FVector3d AHexPlanet::FindViewFocus(const FVector3d& CameraLocation, const FVector3d& CameraForward) const
{
	const double ShellRadius = Radius + (Settings ? Settings->LevelHeight : 0.0);
	const double Along = FVector3d::DotProduct(CameraLocation, CameraForward);
	const double Disc = Along * Along - (CameraLocation.SizeSquared() - ShellRadius * ShellRadius);
	if (Disc > 0.0 && -Along - FMath::Sqrt(Disc) > 0.0)
	{
		return CameraLocation + CameraForward * (-Along - FMath::Sqrt(Disc));
	}
	return CameraLocation.GetSafeNormal() * ShellRadius;
}

void AHexPlanet::CreateHeroLight()
{
	HeroLight = NewObject<UPointLightComponent>(this, TEXT("HeroLight"));
	HeroLight->SetupAttachment(Surface);
	HeroLight->SetMobility(EComponentMobility::Movable);
	// a pool of light that fades out at its reach by HeroLightFalloff, as one day it shows how far the hero can go, and not the falloff of a lamp
	// by the square of the distance, bright under it and dim soon after
	HeroLight->bUseInverseSquaredFalloff = false;
	HeroLight->SetLightFalloffExponent(Settings->HeroLightFalloff);
	HeroLight->SetIntensityUnits(ELightUnits::Unitless);
	HeroLight->SetIntensity(Settings->HeroLightIntensity);
	HeroLight->SetLightColor(Settings->HeroLightColor);
	// it fades out at its reach on the floor below it, HeroLightHeight under the light
	const double Reach = Settings->GetHeroLightReach();
	HeroLight->SetAttenuationRadius(float(FMath::Sqrt(Reach * Reach + FMath::Square(double(Settings->HeroLightHeight)))));
	HeroLight->SetSourceRadius(Settings->HeroLightSourceRadius);
	HeroLight->SetCastShadows(Settings->bHeroLightShadows);
	HeroLight->bCastVolumetricShadow = false;
	HeroLight->RegisterComponent();
	UE_LOG(LogHexHero, Log, TEXT("Planet: the light of the hero on the %s reaches %.0f on the floor from %.0f above it, shadows %s"), GetPlanetLayerName(PlanetLayer),
		Reach, Settings->HeroLightHeight, Settings->bHeroLightShadows ? TEXT("on") : TEXT("off"));
}

void AHexPlanet::UpdateHeroLight(const FVector3d& Focus)
{
	const FVector3d Up = Focus.GetSafeNormal();
	const FVector3d Light = Focus + Up * Settings->HeroLightHeight;
	// hex.PlanetHeroLight: 0 none, 2 without shadows; the forced light of L lights everything without it
	const int32 Mode = CVarHexPlanetHeroLight.GetValueOnGameThread();
	const bool bLit = HeroLight && Mode != 0 && !IsLightForced();
	if (HeroLight)
	{
		if (HeroLight->IsVisible() != bLit)
		{
			HeroLight->SetVisibility(bLit);
		}
		const bool bShadows = Mode == 1 && Settings->bHeroLightShadows;
		if (HeroLight->CastShadows != bShadows)
		{
			HeroLight->SetCastShadows(bShadows);
		}
	}
	// a light that stands still keeps the shadows cached
	if (HeroLight && !HeroLight->GetComponentLocation().Equals(FVector(Light), 0.01))
	{
		HeroLight->SetWorldLocation(FVector(Light));
	}
	// the motes show in it as dust does, and the fog clears around it
	if (Motes)
	{
		Motes->SetHeroLight(FLinearColor(float(Light.X), float(Light.Y), float(Light.Z), bLit ? HeroLight->AttenuationRadius : 0.0f));
	}
	if (HazeMaterial && Settings->bFog && !Settings->bHaze)
	{
		HazeMaterial->SetVectorParameterValue(TEXT("FogFocus"), FLinearColor(float(Focus.X), float(Focus.Y), float(Focus.Z), bLit ? float(Settings->GetHeroLightReach()) : 0.0f));
	}
}

void AHexPlanet::UpdateHeroShade(const FVector3d& Focus)
{
	// a reach of 0 draws no shade, under the forced light of L
	const FLinearColor Value(float(Focus.X), float(Focus.Y), float(Focus.Z), IsLightForced() ? 0.0f : float(Settings->GetHeroLightReach()));
	if (Value == AppliedHeroShade)
	{
		return;
	}
	AppliedHeroShade = Value;
	for (UMaterialInstanceDynamic* Instance : { HazeMaterial.Get(), WaterMaterial.Get(), CloudMaterial.Get(), SkyCloudMaterial.Get() })
	{
		if (Instance)
		{
			Instance->SetVectorParameterValue(TEXT("HeroFocus"), Value);
		}
	}
}

void AHexPlanet::SetHeroShadeParameters(UMaterialInstanceDynamic* Target) const
{
	if (!Target || !Settings)
	{
		return;
	}
	// these materials take the configs of the air and the water, so the values of the shade of this one are given by name
	Target->SetScalarParameterValue(TEXT("HeroShadeStart"), Settings->HeroShadeStart);
	Target->SetScalarParameterValue(TEXT("HeroShadeEnd"), Settings->HeroShadeEnd);
	Target->SetScalarParameterValue(TEXT("HeroShadeBrightness"), Settings->HeroShadeBrightness);
	Target->SetScalarParameterValue(TEXT("HeroShadeSaturation"), Settings->HeroShadeSaturation);
	if (LightField)
	{
		Target->SetTextureParameterValue(TEXT("LightField"), LightField);
	}
}

void AHexPlanet::UpdateLightField()
{
	if (!Settings || !Settings->bHeroShade || Radius <= 0.0)
	{
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();

	// each light as its direction from the center, how far it reaches as an angle and how much of the day it brings back at its middle
	struct FLight
	{
		FVector3d Direction;
		double Reach;
		double CosReach;
		float Lift;
	};
	TArray<FLight> Lights;
	const double Step = FMath::Sqrt(3.0) * Settings->HexRadius;
	const auto AddLight = [&](int32 Cell, double ReachCells, float Lift)
	{
		const double Reach = FMath::Min(ReachCells * Step / Radius, UE_DOUBLE_PI * 0.5);
		if (Cells.IsValidIndex(Cell) && Lift > 0.0f)
		{
			Lights.Add({Cells[Cell].Center, Reach, FMath::Cos(Reach), Lift});
		}
	};
	// the entrances that can be used, as the markers stand on them
	const TArray<FHexEntrance>& All = GetEntrances();
	int32 EntranceLights = 0;
	for (int32 Index = 0; Index < All.Num(); ++Index)
	{
		if (GetEntranceProblem(Index) == HexPlanetUnderground::EEntranceProblem::None)
		{
			AddLight(All[Index].Cell, Settings->EntranceLightReach, Settings->EntranceLightLift);
			++EntranceLights;
		}
	}
	// and the auroras over the pentagons, the first cells of the grid
	int32 AuroraLights = 0;
	if (Settings->bAuroras)
	{
		for (int32 Cell = 0; Cell < FMath::Min(12, Cells.Num()); ++Cell)
		{
			AddLight(Cell, Settings->AuroraLightReach, Settings->AuroraLightLift);
			++AuroraLights;
		}
	}

	// a smooth bump over each light, the strongest of them where they meet, as the shade lifts towards the brightest light around
	const int32 Size = FMath::Clamp(Settings->LightFieldSize, 16, 2048);
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(6 * Size * Size);
	ParallelFor(6 * Size, [&](int32 Row)
	{
		const int32 Face = Row / Size;
		const int32 Y = Row % Size;
		for (int32 X = 0; X < Size; ++X)
		{
			const FVector3d Direction = HexPlanetSeaField::GetTexelDirection(Face, X, Y, Size);
			float Lift = 0.0f;
			for (const FLight& Light : Lights)
			{
				const double Cos = FVector3d::DotProduct(Direction, Light.Direction);
				if (Cos > Light.CosReach)
				{
					const double Share = FMath::Acos(FMath::Min(Cos, 1.0)) / Light.Reach;
					Lift = FMath::Max(Lift, float(FMath::Square(1.0 - Share * Share)) * Light.Lift);
				}
			}
			const uint8 Value = uint8(FMath::RoundToInt(FMath::Clamp(Lift, 0.0f, 1.0f) * 255.0f));
			Pixels[Row * Size + X] = FColor(Value, Value, Value, 255);
		}
	});

	// a new texture each time with a new name, as with the sea field
	const FName Name = MakeUniqueObjectName(GetTransientPackage(), UTextureCube::StaticClass(), TEXT("PlanetLightField"));
	UTextureCube* Cube = UTextureCube::CreateTransient(Size, Size, PF_B8G8R8A8, Name);
	Cube->SRGB = false;
	Cube->Filter = TF_Bilinear;
	Cube->NeverStream = true;
	FTexture2DMipMap& Mip = Cube->GetPlatformData()->Mips[0];
	FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
	Mip.BulkData.Unlock();
	Cube->UpdateResource();
	LightField = Cube;

	for (UMaterialInstanceDynamic* Instance : { HazeMaterial.Get(), WaterMaterial.Get(), CloudMaterial.Get(), SkyCloudMaterial.Get() })
	{
		if (Instance)
		{
			Instance->SetTextureParameterValue(TEXT("LightField"), LightField);
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: the light field of the %s holds %d entrances and %d auroras, %d texels a side, in %.1f ms"), GetPlanetLayerName(PlanetLayer),
		EntranceLights, AuroraLights, Size, (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

namespace
{
	/** Packs a mesh of walls for the GPU, on any thread */
	FHexPlanetMeshSectionRef PrepareWallSection(const HexPlanetWalls::FMesh& Mesh, const TCHAR* Name, int32 Chunk)
	{
		FHexPlanetMeshSource Source;
		Source.Positions = Mesh.Positions;
		Source.Normals = Mesh.Normals;
		Source.UVs[0] = Mesh.UVs;
		Source.UVs[1] = Mesh.UV1s;
		Source.UVs[2] = Mesh.UV2s;
		Source.UVs[3] = Mesh.UV3s;
		Source.UVs[4] = Mesh.UV4s;
		Source.Triangles = Mesh.Triangles;
		return UHexPlanetMeshComponent::PrepareSection(Source, FName(*FString::Printf(TEXT("%s%d"), Name, Chunk)));
	}
}

UHexPlanetMeshComponent* AHexPlanet::MakeWallComponent(const TCHAR* Name, int32 Chunk)
{
	UHexPlanetMeshComponent* Mesh = NewObject<UHexPlanetMeshComponent>(this, *FString::Printf(TEXT("%s%d"), Name, Chunk));
	Mesh->SetupAttachment(Surface);
	Mesh->SetCastShadow(true);
	// light added by ray tracing would need its geometry too, a third of the triangles of the underground again
	Mesh->SetVisibleInRayTracing(false);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->RegisterComponent();
	return Mesh;
}

void AHexPlanet::UpdateWalls(TArray<int32>& Chunks)
{
	if (!Settings || !Settings->bWalls || ShownLevels.Num() != Grid.GetCells().Num())
	{
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	if (!WallMaterial)
	{
		if (UMaterialInterface* Base = WallMaterialAsset.LoadSynchronous())
		{
			// the walls whole or coarse, and the two sides of the band where one dissolves into the other (WallLodSide)
			const auto Make = [&](float Side)
			{
				UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, this);
				HexMaterialConfig::Apply(Instance, Settings);
				if (LayerParamsTexture)
				{
					Instance->SetTextureParameterValue(TEXT("LayerParams"), LayerParamsTexture);
				}
				Instance->SetScalarParameterValue(TEXT("FloorRadius"), float(Radius + Settings->LevelHeight));
				Instance->SetScalarParameterValue(TEXT("WallLodSide"), Side);
				// the pools of light at their foot, which UpdateWallGlow gives again when it makes the texture anew
				if (WallGlowTexture)
				{
					Instance->SetTextureParameterValue(TEXT("WallGlow"), WallGlowTexture);
				}
				return Instance;
			};
			WallMaterial = Make(0.0f);
			WallNearMaterial = Make(1.0f);
			WallFarMaterial = Make(2.0f);
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: the material of the walls %s is missing, run Tools/create_planet_assets.py underground"), *WallMaterialAsset.ToString());
		}
	}

	// the walls stand on the lips of the passages, the rock of the cliff below them, which is the rock of the void, and their foot is the floor of
	// the passages, the terrain layer of their biome; in regions they are of another rock, -1 without it
	int32 RockCell = INDEX_NONE;
	int32 FloorCell = INDEX_NONE;
	for (int32 Cell = 0; Cell < ShownLevels.Num() && (RockCell == INDEX_NONE || FloorCell == INDEX_NONE); ++Cell)
	{
		RockCell = RockCell == INDEX_NONE && ShownLevels[Cell] < 0 ? Cell : RockCell;
		FloorCell = FloorCell == INDEX_NONE && ShownLevels[Cell] >= 0 ? Cell : FloorCell;
	}
	const int32 FloorBiome = FloorCell == INDEX_NONE ? INDEX_NONE : GetCellBiome(FloorCell);
	const UHexTerrainLayersConfig* TerrainLayers = GetDefault<UHexMapSettings>()->LoadTerrainLayers();
	for (UMaterialInstanceDynamic* Instance : {WallMaterial.Get(), WallNearMaterial.Get(), WallFarMaterial.Get()})
	{
		if (!Instance)
		{
			continue;
		}
		if (RockCell != INDEX_NONE)
		{
			Instance->SetScalarParameterValue(TEXT("RockLayer"), float(GetCellRockLayer(RockCell)));
		}
		if (BiomeConfigs.IsValidIndex(FloorBiome) && BiomeConfigs[FloorBiome])
		{
			Instance->SetScalarParameterValue(TEXT("FloorLayer"), float(FMath::Max(BiomeConfigs[FloorBiome]->TerrainLayer, 0)));
		}
		Instance->SetScalarParameterValue(TEXT("CaveWallAltLayer"), float(TerrainLayers ? TerrainLayers->FindLayer(Settings->CaveWallAltLayerName) : INDEX_NONE));
		// the floor at its foot as SetBiomeLayers gives the ground
		Instance->SetScalarParameterValue(TEXT("CaveFloorAltLayer"), float(TerrainLayers ? TerrainLayers->FindLayer(Settings->CaveFloorAltLayerName) : INDEX_NONE));
		Instance->SetScalarParameterValue(TEXT("CaveSlabLayer"), float(TerrainLayers ? TerrainLayers->FindLayer(Settings->CaveSlabLayerName) : INDEX_NONE));
	}

	// the voids of lava and lakes, which light the walls, and the height of the walls along each void, set by hand or by its style
	TArray<uint8> VoidStyles;
	VoidStyles.SetNumZeroed(ShownLevels.Num());
	TArray<float> VoidHeights;
	VoidHeights.Init(1.0f, ShownLevels.Num());
	for (int32 Cell = 0; Cell < ShownLevels.Num() && Cell < CellBiomes.Num(); ++Cell)
	{
		const UHexBiomeConfig* Biome = ShownLevels[Cell] < 0 && BiomeConfigs.IsValidIndex(CellBiomes[Cell]) ? BiomeConfigs[CellBiomes[Cell]].Get() : nullptr;
		VoidStyles[Cell] = !Biome ? 0 : Biome->VoidStyle == EHexVoidStyle::Lava ? 1 : Biome->VoidStyle == EHexVoidStyle::Lake ? 2 : 0;
		if (ShownLevels[Cell] < 0)
		{
			VoidHeights[Cell] = GetWallHeight(Cell);
		}
	}

	// new walls, so a stream still building whole walls from the old ones keeps them and its result is dropped
	WallParams = HexPlanetWalls::FParams::FromConfig(*Settings, Radius + Settings->LevelHeight, HexPlanetUnderground::GetSeed(GetManifest()));
	const TSharedRef<HexPlanetWalls::FWalls> NewWalls = MakeShared<HexPlanetWalls::FWalls>();
	HexPlanetWalls::Build(Grid, ShownLevels, [this](int32 Cell) { return ShownLevels[Cell] >= 0; }, WallParams, *NewWalls, VoidStyles, VoidHeights);
	Walls = NewWalls;
	++WallGeneration;
	const double LineTime = FPlatformTime::Seconds();

	// the chunks whose samples changed get their coarse walls again, and those near the camera their whole ones too, at once, as an edit there
	// would show the coarse ones for a moment; the others get their whole walls from the stream when the camera comes near
	const int32 NumChunks = Grid.GetNumChunks();
	WallHashes.SetNum(NumChunks);
	WallChunks.SetNum(NumChunks);
	WallCoarseChunks.SetNum(NumChunks);
	WallWhole.SetNumZeroed(NumChunks);
	WallWholeTriangles.SetNumZeroed(NumChunks);
	WallCoarseTriangles.SetNumZeroed(NumChunks);
	WallLodStates.SetNum(NumChunks);
	const double Ahead = Settings->WallLodDistance + Settings->WallLodBand + Settings->WallStreamAhead;
	TArray<int32> Changed;
	TArray<uint8> bNear;
	for (int32 Chunk = 0; Chunk < NumChunks; ++Chunk)
	{
		const uint32 Hash = HexPlanetWalls::HashChunk(*Walls, Chunk);
		if (Hash != WallHashes[Chunk] || (!WallCoarseChunks[Chunk] && !Walls->ChunkSamples[Chunk].IsEmpty()))
		{
			WallHashes[Chunk] = Hash;
			Changed.Add(Chunk);
			bNear.Add(bHasLodCamera && ChunkBounds.IsValidIndex(Chunk)
				&& FVector::Distance(LodCamera, ChunkBounds[Chunk].Origin) - ChunkBounds[Chunk].SphereRadius < Ahead ? 1 : 0);
		}
	}
	TArray<FHexPlanetMeshSectionRef> Coarse;
	TArray<FHexPlanetMeshSectionRef> Whole;
	Coarse.SetNum(Changed.Num());
	Whole.SetNum(Changed.Num());
	TArray<FIntPoint> Triangles;
	Triangles.SetNumZeroed(Changed.Num());
	ParallelFor(Changed.Num(), [&](int32 Index)
	{
		const int32 Chunk = Changed[Index];
		HexPlanetWalls::FMesh Mesh;
		HexPlanetWalls::BuildChunk(*Walls, Chunk, WallParams, Mesh, true);
		Triangles[Index].Y = Mesh.Triangles.Num() / 3;
		Coarse[Index] = Mesh.Triangles.IsEmpty() ? nullptr : PrepareWallSection(Mesh, TEXT("WallCoarseChunk"), Chunk);
		if (bNear[Index])
		{
			HexPlanetWalls::BuildChunk(*Walls, Chunk, WallParams, Mesh);
			Triangles[Index].X = Mesh.Triangles.Num() / 3;
			Whole[Index] = Mesh.Triangles.IsEmpty() ? nullptr : PrepareWallSection(Mesh, TEXT("WallChunk"), Chunk);
		}
	});

	const auto Show = [this](UHexPlanetMeshComponent* Mesh, FHexPlanetMeshSectionRef Section)
	{
		Mesh->SetSection(0, MoveTemp(Section));
		Mesh->SetMaterial(0, WallMaterial);
		Mesh->UpdateBounds();
		if (Settings->WallCullDistance > 0.0f)
		{
			Mesh->SetCullDistance(Settings->WallCullDistance + Mesh->Bounds.SphereRadius);
		}
	};
	int64 CoarseTotal = 0;
	int64 WholeTotal = 0;
	for (int32 Index = 0; Index < Changed.Num(); ++Index)
	{
		const int32 Chunk = Changed[Index];
		if (Coarse[Index])
		{
			UHexPlanetMeshComponent* Mesh = WallCoarseChunks[Chunk] ? WallCoarseChunks[Chunk].Get() : MakeWallComponent(TEXT("WallCoarseChunk"), Chunk);
			WallCoarseChunks[Chunk] = Mesh;
			Show(Mesh, MoveTemp(Coarse[Index]));
		}
		else if (WallCoarseChunks[Chunk])
		{
			WallCoarseChunks[Chunk]->ClearSection(0);
		}
		WallCoarseTriangles[Chunk] = Triangles[Index].Y;
		CoarseTotal += Triangles[Index].Y;

		if (Whole[Index])
		{
			UHexPlanetMeshComponent* Mesh = WallChunks[Chunk] ? WallChunks[Chunk].Get() : MakeWallComponent(TEXT("WallChunk"), Chunk);
			WallChunks[Chunk] = Mesh;
			Show(Mesh, MoveTemp(Whole[Index]));
			WallWhole[Chunk] = 1;
		}
		else
		{
			if (WallChunks[Chunk])
			{
				WallChunks[Chunk]->ClearSection(0);
			}
			WallWhole[Chunk] = 0;
		}
		WallWholeTriangles[Chunk] = Triangles[Index].X;
		WholeTotal += Triangles[Index].X;
		WallLodStates[Chunk] = 255;
		Chunks.AddUnique(Chunk);
	}
	if (bHasLodCamera)
	{
		UpdateWallLod(LodCamera);
	}

	int32 Samples = 0;
	for (const TArray<HexPlanetWalls::FSample>& Line : Walls->Lines)
	{
		Samples += Line.Num();
	}
	// the pools of light of the floor at the foot of the new samples
	const double GlowStart = FPlatformTime::Seconds();
	UpdateWallGlow();
	UE_LOG(LogHexHero, Log, TEXT("Planet: walls of the %s: %d lines, %d samples, %d chunks built again, %lld coarse triangles and %lld whole ones near the camera, lines in %.1f ms, meshes in %.1f ms, pools of light in %.1f ms"),
		GetPlanetLayerName(PlanetLayer), Walls->Lines.Num(), Samples, Changed.Num(), CoarseTotal, WholeTotal, (LineTime - StartTime) * 1000.0,
		(GlowStart - LineTime) * 1000.0, (FPlatformTime::Seconds() - GlowStart) * 1000.0);
}

void AHexPlanet::UpdateWallLod(const FVector& CameraLocation)
{
	if (WallCoarseChunks.IsEmpty() || !WallMaterial || !Settings)
	{
		return;
	}
	const bool bShown = CVarHexPlanetWalls.GetValueOnGameThread() && !bWallsHiddenByWire;
	const double Near = Settings->WallLodDistance - Settings->WallLodBand;
	const double Far = Settings->WallLodDistance + Settings->WallLodBand;
	const double Ahead = Far + Settings->WallStreamAhead;
	const double Keep = Ahead + Settings->WallStreamKeep;
	TArray<TPair<double, int32>> Wanted;

	for (int32 Chunk = 0; Chunk < WallCoarseChunks.Num(); ++Chunk)
	{
		UHexPlanetMeshComponent* Coarse = WallCoarseChunks[Chunk];
		if (!Coarse || WallCoarseTriangles[Chunk] == 0)
		{
			continue;
		}
		const FBoxSphereBounds& Bounds = Coarse->Bounds;
		const double Distance = FVector::Distance(CameraLocation, Bounds.Origin);
		const double NearEdge = Distance - Bounds.SphereRadius;
		const double FarEdge = Distance + Bounds.SphereRadius;

		// the whole walls of a chunk are freed past Keep and built in the background as it comes within Ahead
		if (WallWhole[Chunk] && NearEdge > Keep)
		{
			WallChunks[Chunk]->ClearSection(0);
			WallWhole[Chunk] = 0;
			WallWholeTriangles[Chunk] = 0;
		}
		else if (!WallWhole[Chunk] && NearEdge < Ahead && !(WallStream && WallStream->Chunks.Contains(Chunk)))
		{
			Wanted.Emplace(NearEdge, Chunk);
		}

		// a chunk whose whole walls are not built yet draws its coarse ones whole
		const int32 ForcedLod = CVarHexPlanetWallLod.GetValueOnGameThread();
		const uint8 Band = ForcedLod >= 1 && ForcedLod <= 3 ? uint8(ForcedLod - 1) : FarEdge < Near ? 0 : NearEdge > Far ? 2 : 1;
		const uint8 State = !bShown ? 4 : !WallWhole[Chunk] ? 3 : Band;
		if (State == WallLodStates[Chunk])
		{
			continue;
		}
		UE_LOG(LogHexHero, Verbose, TEXT("Planet: the walls of chunk %d go from state %d to %d (0 whole, 1 band, 2 coarse, 3 coarse waiting, 4 hidden), %.0f to %.0f from the camera, %d triangles whole and %d coarse"),
			Chunk, WallLodStates[Chunk], State, NearEdge, FarEdge, WallWholeTriangles[Chunk], WallCoarseTriangles[Chunk]);
		WallLodStates[Chunk] = State;
		if (UHexPlanetMeshComponent* Whole = WallChunks[Chunk])
		{
			Whole->SetVisibility(State <= 1);
			Whole->SetMaterial(0, State == 1 ? WallNearMaterial.Get() : WallMaterial.Get());
		}
		Coarse->SetVisibility(State >= 1 && State <= 3);
		Coarse->SetMaterial(0, State == 1 ? WallFarMaterial.Get() : WallMaterial.Get());
		// the shadow pass draws both of the band whole, so the coarse walls, a little in front of the whole ones in places, shaded them in stripes
		// and the floor at their foot, a shadow that went as soon as the chunk left the band; there the whole walls alone cast it
		Coarse->SetCastShadow(State != 1);
	}

	// the nearest of those that want their whole walls, a few at a time, from the walls of now
	if (!Wanted.IsEmpty() && !WallStreamTask.IsValid())
	{
		Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
		const TSharedRef<FWallStream> Batch = MakeShared<FWallStream>();
		Batch->Generation = WallGeneration;
		for (int32 Index = 0; Index < Wanted.Num() && Index < 8; ++Index)
		{
			Batch->Chunks.Add(Wanted[Index].Value);
		}
		Batch->Sections.SetNum(Batch->Chunks.Num());
		Batch->Triangles.SetNumZeroed(Batch->Chunks.Num());
		WallStream = Batch;
		const TSharedRef<const HexPlanetWalls::FWalls> Data = Walls;
		const HexPlanetWalls::FParams Params = WallParams;
		WallStreamTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Batch, Data, Params]()
		{
			ParallelFor(Batch->Chunks.Num(), [&](int32 Index)
			{
				HexPlanetWalls::FMesh Mesh;
				HexPlanetWalls::BuildChunk(*Data, Batch->Chunks[Index], Params, Mesh);
				Batch->Triangles[Index] = Mesh.Triangles.Num() / 3;
				Batch->Sections[Index] = Mesh.Triangles.IsEmpty() ? nullptr : PrepareWallSection(Mesh, TEXT("WallChunk"), Batch->Chunks[Index]);
			});
		});
	}
}

void AHexPlanet::FinishWallStream()
{
	const TSharedPtr<FWallStream> Batch = MoveTemp(WallStream);
	// walls built again meanwhile make their own whole walls near the camera
	if (!Batch || Batch->Generation != WallGeneration)
	{
		return;
	}
	for (int32 Index = 0; Index < Batch->Chunks.Num(); ++Index)
	{
		const int32 Chunk = Batch->Chunks[Index];
		if (!Batch->Sections[Index] || !WallChunks.IsValidIndex(Chunk))
		{
			continue;
		}
		UHexPlanetMeshComponent* Mesh = WallChunks[Chunk] ? WallChunks[Chunk].Get() : MakeWallComponent(TEXT("WallChunk"), Chunk);
		WallChunks[Chunk] = Mesh;
		Mesh->SetSection(0, MoveTemp(Batch->Sections[Index]));
		Mesh->SetMaterial(0, WallMaterial);
		Mesh->UpdateBounds();
		if (Settings->WallCullDistance > 0.0f)
		{
			Mesh->SetCullDistance(Settings->WallCullDistance + Mesh->Bounds.SphereRadius);
		}
		WallWhole[Chunk] = 1;
		WallWholeTriangles[Chunk] = Batch->Triangles[Index];
		WallLodStates[Chunk] = 255;
	}
}

void AHexPlanet::DropWallWholes()
{
	WallStream.Reset();
	for (int32 Chunk = 0; Chunk < WallChunks.Num(); ++Chunk)
	{
		if (WallWhole.IsValidIndex(Chunk) && WallWhole[Chunk] && WallChunks[Chunk])
		{
			WallChunks[Chunk]->ClearSection(0);
			WallWhole[Chunk] = 0;
			WallWholeTriangles[Chunk] = 0;
			WallLodStates[Chunk] = 255;
		}
	}
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
	for (UMaterialInstanceDynamic* Instance : { SurfaceMaterial.Get(), SurfaceCutMaterial.Get(), LodNearMaterial.Get(), LodFarMaterial.Get(), WaterGridMaterial.Get(), PreviewMaterial.Get() })
	{
		if (Instance)
		{
			Instance->SetVectorParameterValue(TEXT("MarkPrimaryColor"), PrimaryColor);
		}
	}
	SetCellMarks(Marks);
}

UMaterialInstanceDynamic* AHexPlanet::GetPreviewMaterial()
{
	if (PreviewMaterial || !Settings)
	{
		return PreviewMaterial;
	}
	UMaterialInterface* Base = Material.LoadSynchronous();
	if (!Base)
	{
		return nullptr;
	}

	// the tiles draw their vertex colors in place of the biomes, and the marks of their cells from their third and fourth UVs, as the surface does
	PreviewMaterial = UMaterialInstanceDynamic::Create(Base, this);
	HexMaterialConfig::Apply(PreviewMaterial, Settings);
	PreviewMaterial->SetScalarParameterValue(TEXT("PreviewTile"), 1.0f);
	if (CellMarks)
	{
		PreviewMaterial->SetTextureParameterValue(TEXT("CellMarks"), CellMarks);
	}
	PreviewMaterial->SetVectorParameterValue(TEXT("MarkPrimaryColor"), HighlightBrushSize > 0 ? Settings->BrushOutlineColor : Settings->OutlineColor);
	return PreviewMaterial;
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

	// the layer, and the way between the layers: the entrance on the surface and its network, or what the cell is underground
	const bool bUnderground = PlanetLayer == EHexPlanetLayer::Underground;
	FString LayerText = FString::Printf(TEXT("Layer: %s"), GetPlanetLayerName(PlanetLayer));
	const int32 EntranceIndex = FindEntrance(Cell);
	const HexPlanetUnderground::FReport* Report = GetUndergroundReport();
	const auto GetNetworkCount = [Report](int32 Network)
	{
		return Report && Network >= 0 && Network < HexPlanetFile::MaxNetworks ? Report->Networks[Network].Entrances : 0;
	};
	if (bUnderground)
	{
		const int8 Network = Report && Report->CellNetworks.IsValidIndex(Cell) ? Report->CellNetworks[Cell] : int8(INDEX_NONE);
		const FString Kind = !HexPlanetUnderground::IsPassage(int8(GetCellLevel(Cell))) ? FString(TEXT("void, the abyss nothing crosses"))
			: Network >= 0 ? FString::Printf(TEXT("passage of network %c"), HexPlanetFile::GetNetworkLetter(Network))
			: Network == HexPlanetUnderground::MergedNetwork ? FString(TEXT("passage where networks join"))
			: FString(TEXT("passage no exit reaches"));
		LayerText += FString::Printf(TEXT(", %s"), *Kind);
		if (Settings->bWalls && !HexPlanetUnderground::IsPassage(int8(GetCellLevel(Cell))))
		{
			LayerText += FString::Printf(TEXT("\nWalls along it: height x%.2f, %s"), GetWallHeight(Cell),
				WallHeightOverrides.Contains(Cell) ? TEXT("set by hand") : TEXT("by its style"));
		}
	}
	if (EntranceIndex != INDEX_NONE)
	{
		const FHexEntrance& Entrance = GetEntrances()[EntranceIndex];
		const HexPlanetUnderground::EEntranceProblem Problem = GetEntranceProblem(EntranceIndex);
		LayerText += FString::Printf(TEXT("\n%s %c, %d in network"), bUnderground ? TEXT("Exit of entrance") : TEXT("Entrance"),
			HexPlanetFile::GetNetworkLetter(Entrance.Network), GetNetworkCount(Entrance.Network));
		if (Problem != HexPlanetUnderground::EEntranceProblem::None)
		{
			LayerText += FString::Printf(TEXT(", not used: %s"), HexPlanetUnderground::GetProblemText(Problem));
		}
	}

	return FString::Printf(TEXT("Cell %d (%s)\n%s\nLevel: %s\nBiome: %s\nObjects: %s\n%s%sPiece: %s\nFace: %d, chunk %d\nLatitude %.1f, longitude %.1f\nArea: %.1f%% of the mean\nSides: longest x%.3f of shortest"),
		Cell, Each.IsPentagon() ? TEXT("pentagon") : TEXT("hexagon"), *LayerText,
		bUnderground ? (HexPlanetUnderground::IsPassage(int8(GetCellLevel(Cell))) ? TEXT("passage") : TEXT("void")) : LevelNames[GetCellLevel(Cell) + 2], *BiomeName, *Objects->GetCellText(Cell), *Rivers->GetCellText(Cell),
		Rivers->GetCellText(Cell).IsEmpty() ? TEXT("") : TEXT("\n"), *(PieceText + ReliefText), Each.Face, Each.Chunk,
		Latitude, Longitude, Grid.GetCellArea(Cell) / MeanCellArea * 100.0, Grid.GetCellSideRatio(Cell));
}
