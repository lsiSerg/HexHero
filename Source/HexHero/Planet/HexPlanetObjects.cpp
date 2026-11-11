// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetObjects.h"
#include "HexHeroes.h"
#include "HexPlanetRivers.h"
#include "Async/ParallelFor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HexHero.h"
#include "HexMapSettings.h"
#include "HexMaterialConfig.h"
#include "HexPlanet.h"
#include "HexPlanetConfig.h"
#include "HexPlanetFile.h"
#include "HexPlanetObjectSet.h"
#include "HexPlanetUnderground.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"

#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
	const TCHAR* DensityNames[] = {TEXT("sparse"), TEXT("medium"), TEXT("dense")};

	TAutoConsoleVariable<bool> CVarHexPlanetObjectShadowCache(TEXT("hex.PlanetObjectShadowCache"), true,
		TEXT("Keeps the shadows of the objects of the planet in the cache of the virtual shadow maps though their leaves sway (1), or lets every sway draw them again (0); read when the components of a chunk are made, so set it before the planet is built."));

	int64 GetComponentKey(int32 Chunk, int32 Slot)
	{
		return int64(Chunk) * 65536 + Slot;
	}
}

	/** The triangles of the ground of a cell, flattened onto the plane of its center, in buckets, so a point finds its triangle at once */
	struct UHexPlanetObjects::FCellGround
	{
		static constexpr int32 Buckets = 10;

		FVector3f Center;
		FVector3f AxisX;
		FVector3f AxisY;
		const TArray<FVector3f>* Triangles = nullptr;
		TArray<FVector2f> Flat;
		FVector2f Min;
		FVector2f Max;
		TArray<int32> BucketStarts;
		TArray<int32> BucketTriangles;

		FVector2f Flatten(const FVector3f& Point) const
		{
			return FVector2f(FVector3f::DotProduct(Point, AxisX), FVector3f::DotProduct(Point, AxisY));
		}

		FIntPoint GetBucket(const FVector2f& Point) const
		{
			const FVector2f Size = (Max - Min) / Buckets;
			return FIntPoint(FMath::Clamp(int32((Point.X - Min.X) / FMath::Max(Size.X, 1.0e-3f)), 0, Buckets - 1),
				FMath::Clamp(int32((Point.Y - Min.Y) / FMath::Max(Size.Y, 1.0e-3f)), 0, Buckets - 1));
		}

		void Build(const FVector3f& InCenter, const TArray<FVector3f>& InTriangles)
		{
			Center = InCenter;
			AxisX = FVector3f::CrossProduct(FMath::Abs(Center.Z) < 0.9f ? FVector3f::UnitZ() : FVector3f::UnitX(), Center).GetSafeNormal();
			AxisY = FVector3f::CrossProduct(Center, AxisX);
			Triangles = &InTriangles;

			Flat.SetNumUninitialized(InTriangles.Num());
			Min = FVector2f(TNumericLimits<float>::Max());
			Max = FVector2f(-TNumericLimits<float>::Max());
			for (int32 Index = 0; Index < InTriangles.Num(); ++Index)
			{
				Flat[Index] = Flatten(InTriangles[Index]);
				Min = FVector2f::Min(Min, Flat[Index]);
				Max = FVector2f::Max(Max, Flat[Index]);
			}

			// each triangle goes into every bucket its bounds touch, counted first, so the lists lie in one array
			TArray<int32> Counts;
			Counts.Init(0, Buckets * Buckets + 1);
			const auto ForEachBucket = [this](int32 Triangle, auto&& Visit)
			{
				const FVector2f& A = Flat[Triangle * 3];
				const FVector2f& B = Flat[Triangle * 3 + 1];
				const FVector2f& C = Flat[Triangle * 3 + 2];
				const FIntPoint Low = GetBucket(FVector2f::Min(A, FVector2f::Min(B, C)));
				const FIntPoint High = GetBucket(FVector2f::Max(A, FVector2f::Max(B, C)));
				for (int32 Y = Low.Y; Y <= High.Y; ++Y)
				{
					for (int32 X = Low.X; X <= High.X; ++X)
					{
						Visit(Y * Buckets + X);
					}
				}
			};

			const int32 NumTriangles = InTriangles.Num() / 3;
			for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
			{
				ForEachBucket(Triangle, [&Counts](int32 Bucket) { ++Counts[Bucket + 1]; });
			}
			BucketStarts.SetNumUninitialized(Buckets * Buckets + 1);
			BucketStarts[0] = 0;
			for (int32 Bucket = 0; Bucket < Buckets * Buckets; ++Bucket)
			{
				BucketStarts[Bucket + 1] = BucketStarts[Bucket] + Counts[Bucket + 1];
			}
			BucketTriangles.SetNumUninitialized(BucketStarts.Last());
			TArray<int32> Filled;
			Filled.Init(0, Buckets * Buckets);
			for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
			{
				ForEachBucket(Triangle, [&](int32 Bucket) { BucketTriangles[BucketStarts[Bucket] + Filled[Bucket]++] = Triangle; });
			}
		}

		/** Finds the point of the ground over a point of the plane, and the normal of its triangle, facing out */
		bool Find(const FVector2f& Point, FVector3f& OutPosition, FVector3f& OutNormal) const
		{
			if (Flat.IsEmpty())
			{
				return false;
			}
			const FIntPoint Bucket = GetBucket(Point);
			const int32 Index = Bucket.Y * Buckets + Bucket.X;
			for (int32 Slot = BucketStarts[Index]; Slot < BucketStarts[Index + 1]; ++Slot)
			{
				const int32 Triangle = BucketTriangles[Slot];
				const FVector2f& A = Flat[Triangle * 3];
				const FVector2f& B = Flat[Triangle * 3 + 1];
				const FVector2f& C = Flat[Triangle * 3 + 2];
				const float Area = FVector2f::CrossProduct(B - A, C - A);
				if (FMath::Abs(Area) < 1.0e-6f)
				{
					continue;
				}
				const float U = FVector2f::CrossProduct(B - Point, C - Point) / Area;
				const float V = FVector2f::CrossProduct(C - Point, A - Point) / Area;
				const float W = 1.0f - U - V;
				if (U < -1.0e-4f || V < -1.0e-4f || W < -1.0e-4f)
				{
					continue;
				}
				const FVector3f& PA = (*Triangles)[Triangle * 3];
				const FVector3f& PB = (*Triangles)[Triangle * 3 + 1];
				const FVector3f& PC = (*Triangles)[Triangle * 3 + 2];
				OutPosition = PA * U + PB * V + PC * W;
				OutNormal = FVector3f::CrossProduct(PB - PA, PC - PA).GetSafeNormal();
				if (FVector3f::DotProduct(OutNormal, OutPosition) < 0.0f)
				{
					OutNormal = -OutNormal;
				}
				return true;
			}
			return false;
		}
	};

namespace
{
	/** Returns true if a point of the plane lies inside a convex polygon, whose corners go either way round */
	bool IsInside(const FVector2f& Point, const TArray<FVector2f, TInlineAllocator<6>>& Polygon)
	{
		float Sign = 0.0f;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2f& A = Polygon[Index];
			const FVector2f& B = Polygon[(Index + 1) % Polygon.Num()];
			const float Side = FVector2f::CrossProduct(B - A, Point - A);
			if (Sign == 0.0f)
			{
				Sign = FMath::Sign(Side);
			}
			else if (Side * Sign < 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	/** A noise of patches on the unit sphere, two octaves */
	double PatchNoise(const FVector3d& Point, double Scale, const FVector3d& Offset)
	{
		return FMath::PerlinNoise3D(Point * Scale + Offset) + 0.5 * FMath::PerlinNoise3D(Point * Scale * 2.1 + Offset * 1.7 + FVector3d(31.0));
	}

	/** Returns how far a mesh reaches from its pivot across its Z, from its vertices where the CPU has them (import_cave_objects.py gives the cave
	 *  meshes CPU access), else from its bounds, which for a round mesh is up to a third more */
	float GetFootprintRadius(UStaticMesh* Mesh)
	{
#if WITH_EDITOR
		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
		const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (RenderData && !RenderData->LODResources.IsEmpty() && (Mesh->bAllowCPUAccess || GIsEditor))
		{
			const FPositionVertexBuffer& Positions = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
			if (Positions.GetNumVertices() > 0 && Positions.GetVertexData())
			{
				float Farthest = 0.0f;
				for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
				{
					const FVector3f Position = Positions.VertexPosition(Index);
					Farthest = FMath::Max(Farthest, FVector2f(Position.X, Position.Y).Size());
				}
				return Farthest;
			}
		}
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		return float(FVector2d(Bounds.Origin.X, Bounds.Origin.Y).Size() + FVector2d(Bounds.BoxExtent.X, Bounds.BoxExtent.Y).Size());
	}
}

TArray<TObjectPtr<UHexPlanetObjectSet>> UHexPlanetObjects::FindLayerSets(EHexPlanetLayer Layer)
{
	TArray<TObjectPtr<UHexPlanetObjectSet>> Found;
	for (const UHexPlanetObjectSet* Set : GetDefault<UHexMapSettings>()->LoadPlanetObjectSets())
	{
		// the sets of one layer alone, so the underground loads no meshes of forests and the surface none of caves
		if (!Set->IsOnLayer(Layer))
		{
			continue;
		}
		if (Set->Id.IsNone() || Found.ContainsByPredicate([Set](const UHexPlanetObjectSet* Each) { return Each->Id == Set->Id; }))
		{
			UE_LOG(LogHexHero, Error, TEXT("Planet: the object set %s has no Id or one another set has, so it is left out"), *Set->GetName());
			continue;
		}
		Found.Add(const_cast<UHexPlanetObjectSet*>(Set));
	}
	// the palette, the generation and FHexCellObjects count by this order, so it is the same in every run
	Found.StableSort([](const UHexPlanetObjectSet& A, const UHexPlanetObjectSet& B) { return A.SortOrder < B.SortOrder; });
	if (Found.Num() > 254)
	{
		Found.SetNum(254);
	}
	return Found;
}

TArray<TObjectPtr<UHexPlanetObjectSet>> UHexPlanetObjects::FindPlaceableSets(EHexPlanetLayer Layer, int32* OutOwn)
{
	TArray<TObjectPtr<UHexPlanetObjectSet>> Found = FindLayerSets(Layer);
	const int32 Own = Found.Num();
	// after them, so the indices of the sets of the layer stay as the generation and the palette count them, the covers and decor of the other
	// layer that this one has not, which a hand alone places here, but no mass that fills a cell, which is never placed by hand
	const EHexPlanetLayer Other = Layer == EHexPlanetLayer::Surface ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface;
	for (const TObjectPtr<UHexPlanetObjectSet>& Set : FindLayerSets(Other))
	{
		if ((Set->Layer == EHexObjectLayer::Cover || Set->Layer == EHexObjectLayer::Decor) && !Set->IsCellMass() && Found.Num() < 254
			&& !Found.ContainsByPredicate([&Set](const UHexPlanetObjectSet* Each) { return Each->Id == Set->Id; }))
		{
			Found.Add(Set);
		}
	}
	if (OutOwn)
	{
		*OutOwn = Own;
	}
	return Found;
}

TArray<int32> UHexPlanetObjects::GetPaintableSets(TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets)
{
	TArray<int32> Paintable;
	for (int32 Index = 0; Index < LayerSets.Num(); ++Index)
	{
		if (LayerSets[Index] && (LayerSets[Index]->Layer == EHexObjectLayer::Cover || LayerSets[Index]->Layer == EHexObjectLayer::Decor))
		{
			Paintable.Add(Index);
		}
	}
	return Paintable;
}

TArray<HexPlanetUnderground::FObjectKind> UHexPlanetObjects::MakeObjectKinds(TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets)
{
	TArray<HexPlanetUnderground::FObjectKind> Kinds;
	for (const int32 Index : GetPaintableSets(LayerSets))
	{
		const UHexPlanetObjectSet& Set = *LayerSets[Index];
		// a kind that blocks takes only cells it cannot cut a passage at (FObjectKind::bBlocks); painting it anywhere stays the choice of the designer
		HexPlanetUnderground::FObjectKind& Kind = Kinds.AddDefaulted_GetRef();
		Kind.SetIndex = Index;
		Kind.Id = Set.Id.ToString();
		Kind.bDecor = Set.Layer == EHexObjectLayer::Decor;
		Kind.bBlocks = Set.bBlocksMovement;
		for (const int32 Level : Set.AllowedLevels)
		{
			Kind.Levels.Add(int8(FMath::Clamp(Level, -128, 127)));
		}
		for (const FHexObjectVariant& Variant : Set.Variants)
		{
			Kind.Variants.Add({Variant.Biomes, Variant.GenerateShare});
		}
		Kind.PatchCells = Set.GeneratePatchCells;
		// to a millionth, so the shares are the decimals of the asset and the rank at a threshold falls as the literals of before gave it
		const auto Share = [&Set](int32 Index, double Default)
		{
			return Set.GenerateDensityShares.IsValidIndex(Index) ? FMath::RoundToDouble(double(Set.GenerateDensityShares[Index]) * 1e6) / 1e6 : Default;
		};
		Kind.DenseShare = Share(0, 0.4);
		Kind.MediumShare = FMath::Max(Share(1, 0.75), Kind.DenseShare);
		Kind.Group = Set.GenerateGroup;
		Kind.ClearRings = FMath::Clamp(Set.ExitClearRings, 0, 3);
	}
	return Kinds;
}

void UHexPlanetObjects::GenerateUndergroundObjects(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<TCHAR> CellBiomeLetters,
	TConstArrayView<FHexEntrance> Entrances, const FHexPlanetManifest& Manifest, TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets,
	TArray<FHexCellObjects>& OutObjects)
{
	const double StartTime = FPlatformTime::Seconds();
	const TArray<HexPlanetUnderground::FObjectKind> Kinds = MakeObjectKinds(LayerSets);
	HexPlanetUnderground::FObjectReport Report;
	HexPlanetUnderground::GenerateObjects(Grid, Levels, CellBiomeLetters, Entrances, HexPlanetUnderground::GetSeed(Manifest), Kinds, OutObjects, &Report);

	// the middle of the largest patch of each kind, for hex.PlanetFocus <Cell> underground
	TArray<FString> Parts;
	int32 Total = 0;
	for (int32 Index = 0; Index < Kinds.Num(); ++Index)
	{
		Parts.Add(FString::Printf(TEXT("%s %d (patch at %d)"), *Kinds[Index].Id, Report.Cells[Index], Report.PatchCenters[Index]));
	}
	for (const FHexCellObjects& Objects : OutObjects)
	{
		Total += Objects.IsEmpty() ? 0 : 1;
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: generated the objects of the underground on %d cells in %.1f ms: %s"), Total, (FPlatformTime::Seconds() - StartTime) * 1000.0,
		*FString::Join(Parts, TEXT(", ")));
}

bool UHexPlanetObjects::SaveCells(const FString& Folder, TConstArrayView<FHexCellObjects> Cells, TConstArrayView<int8> Levels,
	TConstArrayView<TObjectPtr<UHexPlanetObjectSet>> LayerSets, FString& OutError)
{
	TArray<FName> SetIds;
	for (const UHexPlanetObjectSet* Set : LayerSets)
	{
		SetIds.Add(Set->Id);
	}
	const TArray<FHexObjectCell> Lines = MakeSavedLines(Cells, SetIds, [&LayerSets, &Levels](int32 Cell, int32 Set)
	{
		return LayerSets.IsValidIndex(Set) && Levels.IsValidIndex(Cell) && LayerSets[Set]->AllowedLevels.Contains(int32(Levels[Cell]));
	}, [&Levels](int32 Cell, int32 Set)
	{
		return Levels.IsValidIndex(Cell) && IsPlacedLevel(Levels[Cell]);
	});
	return HexPlanetFile::SaveObjects(Folder, Lines, OutError);
}

bool UHexPlanetObjects::GenerateCells(TConstArrayView<int8> Levels, TConstArrayView<uint8> Biomes, TArray<FHexCellObjects>& OutObjects) const
{
	if (!Planet || Sets.IsEmpty() || Levels.Num() != Planet->GetGrid().GetCells().Num() || Biomes.Num() != Levels.Num())
	{
		return false;
	}
	const TArray<TCHAR>& Letters = Planet->GetBiomeLetters();
	TArray<TCHAR> CellLetters;
	CellLetters.SetNumUninitialized(Biomes.Num());
	for (int32 Cell = 0; Cell < Biomes.Num(); ++Cell)
	{
		CellLetters[Cell] = Letters.IsValidIndex(Biomes[Cell]) ? Letters[Biomes[Cell]] : TEXT('\0');
	}
	// by the kinds of the layer alone, whose indices come first in Sets
	if (Planet->GetLayer() == EHexPlanetLayer::Underground)
	{
		GenerateUndergroundObjects(Planet->GetGrid(), Levels, CellLetters, Planet->GetEntrances(), Planet->GetManifest(), MakeArrayView(Sets.GetData(), NumOwnSets), OutObjects);
	}
	else
	{
		GenerateSurfaceObjects(Levels, CellLetters, OutObjects);
	}
	return true;
}

void UHexPlanetObjects::Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings)
{
	Planet = InPlanet;
	Settings = InSettings;
	// the kinds of the layer, then those of the other layer that a hand places here
	Sets = FindPlaceableSets(Planet->GetLayer(), &NumOwnSets);

	VariantSlots.SetNum(Sets.Num());
	AllSlots.SetNum(Sets.Num());
	for (int32 SetIndex = 0; SetIndex < Sets.Num(); ++SetIndex)
	{
		for (const FHexObjectVariant& Variant : Sets[SetIndex]->Variants)
		{
			TArray<int32>& Found = VariantSlots[SetIndex].AddDefaulted_GetRef();
			for (const TSoftObjectPtr<UStaticMesh>& Soft : Variant.Meshes)
			{
				UStaticMesh* Mesh = Soft.LoadSynchronous();
				if (!Mesh)
				{
					UE_LOG(LogHexHero, Warning, TEXT("Planet: the mesh %s of the object set %s is missing"), *Soft.ToString(), *Sets[SetIndex]->GetName());
					continue;
				}
				int32 Slot = Slots.IndexOfByPredicate([SetIndex, Mesh](const FSlot& Each) { return Each.Set == SetIndex && Each.Mesh == Mesh; });
				if (Slot == INDEX_NONE)
				{
					// a rim keeps off the cell of an exit beside its side by its footprint too, and what a hand places underground off the void
					const bool bFootprint = Sets[SetIndex]->bClipFootprint || Sets[SetIndex]->Layer == EHexObjectLayer::Rim || Planet->GetLayer() == EHexPlanetLayer::Underground;
					Slot = Slots.Add(FSlot{SetIndex, Mesh, bFootprint ? GetFootprintRadius(Mesh) : 0.0f, Variant.ScaleFactor, Variant.Snow});
					LoadedMeshes.AddUnique(Mesh);
					AllSlots[SetIndex].Add(Slot);
				}
				Found.Add(Slot);
			}
		}
		// only a kind of the layer stands on its rims
		if (Sets[SetIndex]->Layer == EHexObjectLayer::Rim && SetIndex < NumOwnSets)
		{
			RimSets.Add(SetIndex);
			RimClearRings = FMath::Max(RimClearRings, FMath::Clamp(Sets[SetIndex]->ExitClearRings, 0, 3));
		}
	}
	UpdateClearRings();

	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	ChunkCells.SetNum(Planet->GetGrid().GetNumChunks());
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		ChunkCells[Cells[Cell].Chunk].Add(Cell);
	}

	Load();
}

void UHexPlanetObjects::Load()
{
	const int32 NumCells = Planet->GetGrid().GetCells().Num();
	CellObjects.Init(FHexCellObjects(), NumCells);

	TArray<FHexObjectCell> Lines;
	FString Error;
	// from the data folder of the layer of the planet; a layer that does not generate them, the underground, has none without the file
	const FString Folder = Planet->GetDataFolder();
	if (!Planet->IsSaved() || !HexPlanetFile::HasObjects(Folder))
	{
		if (Settings->bGenerateObjects)
		{
			Generate();
			bGenerated = true;
		}
		return;
	}
	if (!HexPlanetFile::LoadObjects(Folder, Lines, Error))
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: %s"), *Error);
		return;
	}

	const auto FindSet = [this](FName Id)
	{
		return Id.IsNone() ? INDEX_NONE : Sets.IndexOfByPredicate([Id](const UHexPlanetObjectSet* Each) { return Each->Id == Id; });
	};
	TSet<FName> Unknown;
	for (const FHexObjectCell& Line : Lines)
	{
		if (!CellObjects.IsValidIndex(Line.Cell))
		{
			continue;
		}
		FHexCellObjects& Objects = CellObjects[Line.Cell];
		// what was placed by hand, a cover or decor of the layer each
		for (const FHexPlacedLine& Each : Line.Placed)
		{
			const int32 Set = FindSet(Each.Id);
			if (Set == INDEX_NONE || Sets[Set]->Layer == EHexObjectLayer::Rim)
			{
				Unknown.Add(Each.Id);
				continue;
			}
			FHexPlacedObject& Object = Objects.Placed.AddDefaulted_GetRef();
			Object.Set = uint8(Set + 1);
			Object.Mesh = uint8(FMath::Clamp(Each.Mesh, 0, 255));
			Object.Offset = FVector2f(Each.X, Each.Y);
			Object.Yaw = Each.Yaw;
			Object.Scale = FMath::Max(Each.Scale, 0.01f);
		}
		// a cover and a decor of the layer alone, as only a hand places the kinds of the other layer
		const int32 Cover = FindSet(Line.Cover);
		const int32 Decor = FindSet(Line.Decor);
		if (Cover != INDEX_NONE && Cover < NumOwnSets && Sets[Cover]->Layer == EHexObjectLayer::Cover)
		{
			Objects.Cover = uint8(Cover + 1);
			Objects.Density = uint8(FMath::Clamp(Line.Density, 1, 3));
		}
		else if (!Line.Cover.IsNone())
		{
			Unknown.Add(Line.Cover);
		}
		if (Decor != INDEX_NONE && Decor < NumOwnSets && Sets[Decor]->Layer == EHexObjectLayer::Decor)
		{
			Objects.Decor = uint8(Decor + 1);
		}
		else if (!Line.Decor.IsNone())
		{
			Unknown.Add(Line.Decor);
		}
	}
	for (const FName Id : Unknown)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: objects.json names %s, which no object set of that layer has as its Id, so those cells lack it"), *Id.ToString());
	}
}

void UHexPlanetObjects::Generate()
{
	// the passages of the underground by their own shares and patches, kept off the exits (HexPlanetUnderground::GenerateObjects), and the land
	// of the surface by the shares of the variants
	if (!GenerateCells(Planet->GetLevels(), Planet->GetCellBiomes(), CellObjects))
	{
		CellObjects.Init(FHexCellObjects(), Planet->GetGrid().GetCells().Num());
	}
}

void UHexPlanetObjects::GenerateSurfaceObjects(TConstArrayView<int8> Levels, TConstArrayView<TCHAR> CellBiomeLetters, TArray<FHexCellObjects>& OutObjects) const
{
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	OutObjects.Init(FHexCellObjects(), Cells.Num());
	const FHexPlanetManifest& Manifest = Planet->GetManifest();
	FRandomStream Random(Manifest.Seed * 7919 + 17);
	const FVector3d Offset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
	const double Scale = Settings->ObjectNoiseScale * Settings->GetSizeRatio(Manifest.Frequency);
	// the lattice of the noise of a kind with patches of its own size is twice that size, as the noise rises and falls about once over a step
	const double CellAngle = FMath::Sqrt(4.0 * UE_DOUBLE_PI / FMath::Max(Cells.Num(), 1));
	int32 Placed = 0;

	for (int32 SetIndex = 0; SetIndex < NumOwnSets; ++SetIndex)
	{
		const UHexPlanetObjectSet& Set = *Sets[SetIndex];
		// the rims stand along the sides by themselves and are never stored in a cell
		if (Set.Layer == EHexObjectLayer::Rim)
		{
			continue;
		}
		const bool bDecor = Set.Layer == EHexObjectLayer::Decor;
		const FVector3d SetOffset = Offset + FVector3d(SetIndex * 17.3, SetIndex * -9.1, SetIndex * 5.7);
		const double SetScale = Set.GeneratePatchCells > 0.0f ? 1.0 / (2.0 * Set.GeneratePatchCells * CellAngle) : Scale;
		const double DenseShare = Set.GenerateDensityShares.IsValidIndex(0) ? Set.GenerateDensityShares[0] : 0.4;
		const double MediumShare = FMath::Max(Set.GenerateDensityShares.IsValidIndex(1) ? double(Set.GenerateDensityShares[1]) : 0.75, DenseShare);
		int32 SetPlaced = 0;

		for (const FHexObjectVariant& Variant : Set.Variants)
		{
			if (Variant.GenerateShare <= 0.0f)
			{
				continue;
			}

			// the free land cells of the biomes of this variant, where the level allows it; the pentagons are kept for gameplay
			TArray<int32> Candidates;
			TArray<double> Keys;
			Keys.SetNumZeroed(Cells.Num());
			for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
			{
				const FHexCellObjects& Objects = OutObjects[Cell];
				if (Cells[Cell].IsPentagon() || (bDecor ? Objects.Decor != 0 : Objects.Cover != 0) || !Set.AllowedLevels.Contains(int32(Levels[Cell]))
					|| Set.FindVariant(CellBiomeLetters[Cell]) != &Variant)
				{
					continue;
				}
				Candidates.Add(Cell);
				// a cover lies in patches, a decor anywhere
				Keys[Cell] = bDecor ? Random.FRand() : PatchNoise(Cells[Cell].Center, SetScale, SetOffset);
			}

			Candidates.Sort([&Keys](int32 A, int32 B) { return Keys[A] > Keys[B]; });
			const int32 Count = FMath::RoundToInt32(Variant.GenerateShare * Candidates.Num());
			for (int32 Rank = 0; Rank < Count; ++Rank)
			{
				FHexCellObjects& Objects = OutObjects[Candidates[Rank]];
				if (bDecor)
				{
					Objects.Decor = uint8(SetIndex + 1);
				}
				else
				{
					// dense in the middle of a patch, sparse at its edge
					Objects.Cover = uint8(SetIndex + 1);
					Objects.Density = Rank < Count * DenseShare ? 3 : Rank < Count * MediumShare ? 2 : 1;
				}
				++Placed;
				++SetPlaced;
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("Planet: generated %s on %d cells"), *Set.Id.ToString(), SetPlaced);
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: generated objects on %d cells from %d object sets"), Placed, NumOwnSets);
}

bool UHexPlanetObjects::Save(const FString& Folder, const TSet<int32>& KeptEntranceCells, FString& OutError) const
{
	TArray<FName> SetIds;
	for (const UHexPlanetObjectSet* Set : Sets)
	{
		SetIds.Add(Set->Id);
	}
	const TArray<FHexObjectCell> Lines = MakeSavedLines(CellObjects, SetIds, [this, &KeptEntranceCells](int32 Cell, int32 Set)
	{
		return IsAllowed(Set, Planet->GetCellLevel(Cell)) && !KeptEntranceCells.Contains(Cell);
	}, [this, &KeptEntranceCells](int32 Cell, int32 Set)
	{
		return IsPlacedLevel(Planet->GetCellLevel(Cell)) && !KeptEntranceCells.Contains(Cell);
	});
	return HexPlanetFile::SaveObjects(Folder, Lines, OutError);
}

TArray<FHexObjectCell> UHexPlanetObjects::MakeSavedLines(TConstArrayView<FHexCellObjects> Cells, TConstArrayView<FName> SetIds, TFunctionRef<bool(int32 Cell, int32 Set)> IsKept,
	TFunctionRef<bool(int32 Cell, int32 Set)> IsPlacedKept)
{
	TArray<FHexObjectCell> Lines;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const FHexCellObjects& Objects = Cells[Cell];
		const bool bCover = Objects.Cover != 0 && SetIds.IsValidIndex(Objects.Cover - 1) && IsKept(Cell, Objects.Cover - 1);
		const bool bDecor = Objects.Decor != 0 && SetIds.IsValidIndex(Objects.Decor - 1) && IsKept(Cell, Objects.Decor - 1);
		TArray<FHexPlacedLine> Placed;
		for (const FHexPlacedObject& Object : Objects.Placed)
		{
			if (Object.Set != 0 && SetIds.IsValidIndex(Object.Set - 1) && IsPlacedKept(Cell, Object.Set - 1))
			{
				FHexPlacedLine& Each = Placed.AddDefaulted_GetRef();
				Each.Id = SetIds[Object.Set - 1];
				Each.X = Object.Offset.X;
				Each.Y = Object.Offset.Y;
				Each.Yaw = Object.Yaw;
				Each.Scale = Object.Scale;
				Each.Mesh = Object.Mesh;
			}
		}
		if (!bCover && !bDecor && Placed.IsEmpty())
		{
			continue;
		}
		FHexObjectCell& Line = Lines.AddDefaulted_GetRef();
		Line.Cell = Cell;
		Line.Cover = bCover ? SetIds[Objects.Cover - 1] : NAME_None;
		Line.Density = bCover ? Objects.Density : 0;
		Line.Decor = bDecor ? SetIds[Objects.Decor - 1] : NAME_None;
		Line.Placed = MoveTemp(Placed);
	}
	return Lines;
}

bool UHexPlanetObjects::IsAllowed(int32 Set, int32 Level) const
{
	return Sets.IsValidIndex(Set) && Sets[Set]->AllowedLevels.Contains(Level);
}

bool UHexPlanetObjects::GetCellCanopy(int32 Cell, FLinearColor& OutColor, float& OutCover) const
{
	const FHexCellObjects& Objects = GetCell(Cell);
	const int32 Set = int32(Objects.Cover) - 1;
	if (Set < 0 && !Objects.Placed.IsEmpty() && !IsClear(Cell) && IsPlacedLevel(Planet->GetCellLevel(Cell)))
	{
		// a cell whose cover was unpacked, or trees placed by hand, tints its ground by how many of a kind of cover stand on it against a dense cover
		TMap<int32, int32> Counts;
		for (const FHexPlacedObject& Object : Objects.Placed)
		{
			const int32 Kind = int32(Object.Set) - 1;
			if (Kind >= 0 && Kind < NumOwnSets && Sets[Kind]->Layer == EHexObjectLayer::Cover && Sets[Kind]->FarCover > 0.0f && !Sets[Kind]->CountByDensity.IsEmpty())
			{
				++Counts.FindOrAdd(Kind);
			}
		}
		int32 Most = INDEX_NONE;
		for (const TPair<int32, int32>& Each : Counts)
		{
			Most = Most == INDEX_NONE || Each.Value > Counts[Most] ? Each.Key : Most;
		}
		if (Most == INDEX_NONE)
		{
			return false;
		}
		const UHexPlanetObjectSet& Kind = *Sets[Most];
		const FHexObjectVariant* Variant = Kind.FindVariant(Planet->GetCellBiomeLetter(Cell));
		OutCover = Kind.FarCover * FMath::Clamp(float(Counts[Most]) / float(FMath::Max(Kind.CountByDensity.Last(), 1)), 0.0f, 1.0f);
		OutColor = FMath::Lerp(Kind.FarColor, FLinearColor::White, FMath::Clamp(Variant ? Variant->Snow : 0.0f, 0.0f, 1.0f) * Kind.FarSnowShare);
		return OutCover > 0.0f;
	}
	if (Set < 0 || IsClear(Cell) || !IsAllowed(Set, Planet->GetCellLevel(Cell)) || Sets[Set]->FarCover <= 0.0f)
	{
		return false;
	}

	const UHexPlanetObjectSet& Kind = *Sets[Set];
	const FHexObjectVariant* Variant = Kind.FindVariant(Planet->GetCellBiomeLetter(Cell));
	if (!Variant)
	{
		return false;
	}

	// a sparse cover a little lighter than a dense one, as the cells of a forest differ in density and much more drew a mosaic of hexes
	const int32 MaxDensity = FMath::Max(Kind.CountByDensity.Num(), 1);
	const float Dense = MaxDensity > 1 ? float(FMath::Clamp(int32(Objects.Density), 1, MaxDensity) - 1) / float(MaxDensity - 1) : 1.0f;
	OutCover = Kind.FarCover * FMath::Lerp(0.8f, 1.0f, Dense);
	OutColor = FMath::Lerp(Kind.FarColor, FLinearColor::White, FMath::Clamp(Variant->Snow, 0.0f, 1.0f) * Kind.FarSnowShare);
	return true;
}

bool UHexPlanetObjects::GetCellGlow(int32 Cell, FLinearColor& OutColor, float& OutStrength) const
{
	OutStrength = 0.0f;
	if (IsClear(Cell))
	{
		return false;
	}
	const FHexCellObjects& Objects = GetCell(Cell);
	const int32 Level = Planet->GetCellLevel(Cell);
	const TCHAR Letter = Planet->GetCellBiomeLetter(Cell);
	const auto Consider = [&](int32 Stored, float Factor)
	{
		const int32 Set = Stored - 1;
		if (Set < 0 || !IsAllowed(Set, Level))
		{
			return;
		}
		const UHexPlanetObjectSet& Kind = *Sets[Set];
		const float Strength = Kind.GlowPool * Factor;
		if (Kind.GlowStrength > 0.0f && Strength > OutStrength && Kind.FindVariant(Letter))
		{
			OutStrength = Strength;
			OutColor = Kind.GlowColor;
		}
	};
	// a sparse cover lights less of its floor than a dense one (GlowPoolByDensity)
	const int32 Cover = int32(Objects.Cover) - 1;
	const TArray<float>* ByDensity = Sets.IsValidIndex(Cover) ? &Sets[Cover]->GlowPoolByDensity : nullptr;
	const int32 Density = FMath::Clamp(int32(Objects.Density), 1, 3) - 1;
	Consider(Objects.Cover, ByDensity && ByDensity->IsValidIndex(Density) ? (*ByDensity)[Density] : 1.0f);
	Consider(Objects.Decor, 1.0f);
	// and what was placed by hand, as a decor of its kind, on any land and whatever the biome, as a hand placed it
	if (IsPlacedLevel(Level))
	{
		for (const FHexPlacedObject& Object : Objects.Placed)
		{
			const int32 Set = int32(Object.Set) - 1;
			if (Sets.IsValidIndex(Set) && Sets[Set]->GlowStrength > 0.0f && Sets[Set]->GlowPool > OutStrength)
			{
				OutStrength = Sets[Set]->GlowPool;
				OutColor = Sets[Set]->GlowColor;
			}
		}
	}
	return OutStrength > 0.0f;
}

int32 UHexPlanetObjects::GetCellSpores(int32 Cell, FLinearColor& OutColor) const
{
	if (IsClear(Cell))
	{
		return 0;
	}
	const FHexCellObjects& Objects = GetCell(Cell);
	const int32 Level = Planet->GetCellLevel(Cell);
	const TCHAR Letter = Planet->GetCellBiomeLetter(Cell);
	int32 Most = 0;
	const auto Consider = [&](int32 Stored)
	{
		const int32 Set = Stored - 1;
		if (Set >= 0 && IsAllowed(Set, Level) && Sets[Set]->Spores > Most && Sets[Set]->FindVariant(Letter))
		{
			Most = Sets[Set]->Spores;
			OutColor = Sets[Set]->GlowColor;
		}
	};
	Consider(Objects.Cover);
	Consider(Objects.Decor);
	if (IsPlacedLevel(Level))
	{
		for (const FHexPlacedObject& Object : Objects.Placed)
		{
			const int32 Set = int32(Object.Set) - 1;
			if (Sets.IsValidIndex(Set) && Sets[Set]->Spores > Most)
			{
				Most = Sets[Set]->Spores;
				OutColor = Sets[Set]->GlowColor;
			}
		}
	}
	return Most;
}

bool UHexPlanetObjects::BlocksMovement(int32 Cell) const
{
	const FHexCellObjects& Objects = GetCell(Cell);
	const int32 Level = Planet->GetCellLevel(Cell);
	const auto Blocks = [&](int32 Set) { return Set > 0 && IsAllowed(Set - 1, Level) && Sets[Set - 1]->bBlocksMovement; };
	return !IsClear(Cell) && (Blocks(Objects.Cover) || Blocks(Objects.Decor));
}

FString UHexPlanetObjects::GetCellText(int32 Cell) const
{
	const FHexCellObjects& Objects = GetCell(Cell);
	const int32 Level = Planet->GetCellLevel(Cell);
	TArray<FString> Parts;
	if (Objects.Cover != 0)
	{
		Parts.Add(FString::Printf(TEXT("%s (%s)%s"), *Sets[Objects.Cover - 1]->DisplayName, DensityNames[FMath::Clamp(Objects.Density, 1, 3) - 1],
			IsAllowed(Objects.Cover - 1, Level) ? TEXT("") : TEXT(", not on this level")));
	}
	if (Objects.Decor != 0)
	{
		Parts.Add(FString::Printf(TEXT("%s%s"), *Sets[Objects.Decor - 1]->DisplayName, IsAllowed(Objects.Decor - 1, Level) ? TEXT("") : TEXT(", not on this level")));
	}
	// what was placed by hand, by kind
	if (!Objects.Placed.IsEmpty())
	{
		TMap<int32, int32> ByKind;
		for (const FHexPlacedObject& Object : Objects.Placed)
		{
			++ByKind.FindOrAdd(Object.Set);
		}
		TArray<FString> Kinds;
		for (const TPair<int32, int32>& Each : ByKind)
		{
			const bool bKnown = Sets.IsValidIndex(Each.Key - 1);
			Kinds.Add(FString::Printf(TEXT("%d %s%s"), Each.Value, bKnown ? *Sets[Each.Key - 1]->DisplayName : TEXT("unknown"),
				bKnown && IsPlacedLevel(Level) ? TEXT("") : TEXT(" not on this level")));
		}
		Parts.Add(FString::Printf(TEXT("placed by hand: %s"), *FString::Join(Kinds, TEXT(", "))));
	}
	if (Parts.IsEmpty())
	{
		return TEXT("none");
	}
	if (IsClear(Cell))
	{
		// a save keeps an entrance that can be used, and leaves out what stands on its cell; one that cannot is left out itself
		const int32 Entrance = Planet->FindEntrance(Cell);
		const bool bKept = Entrance != INDEX_NONE && Planet->GetEntranceProblem(Entrance) == HexPlanetUnderground::EEntranceProblem::None;
		return FString::Join(Parts, TEXT(", ")) + (bKept ? TEXT(", not drawn on the entrance, left out of the next save") : TEXT(", not drawn on the entrance"));
	}
	return FString::Join(Parts, TEXT(", ")) + (BlocksMovement(Cell) ? TEXT(", blocks movement") : TEXT(", passable"));
}

bool UHexPlanetObjects::SetClearCells(TConstArrayView<int32> Cells)
{
	TSet<int32> NewCells;
	NewCells.Append(Cells);
	const bool bSetChanged = NewCells.Num() != ClearCells.Num() || !NewCells.Includes(ClearCells);

	// the cells that hold something and were cleared or given back, and every cell cleared or given back, whose sides take rims or not
	TArray<int32> Changed;
	TArray<int32> Toggled;
	for (const int32 Cell : NewCells)
	{
		if (!ClearCells.Contains(Cell))
		{
			Toggled.Add(Cell);
			if (!GetCell(Cell).IsEmpty())
			{
				Changed.Add(Cell);
			}
		}
	}
	for (const int32 Cell : ClearCells)
	{
		if (!NewCells.Contains(Cell))
		{
			Toggled.Add(Cell);
			if (!GetCell(Cell).IsEmpty())
			{
				Changed.Add(Cell);
			}
		}
	}
	ClearCells = MoveTemp(NewCells);
	UpdateClearRings();

	// before the planet is built its objects are placed with the surface, which reads them then
	if (Planet && Planet->IsBuilt())
	{
		// a rim leaves out the sides of an exit and of the cells near it, and the void beside a passage may be the cleared cell, so the chunks of
		// every cell that far from a cell that changed place their rims again, though nothing is stored in them
		if (HasRims() && !Toggled.IsEmpty())
		{
			const TArray<int32> Near = HexPlanetUnderground::GetRingDistances(Planet->GetGrid(), Toggled, RimClearRings + 1);
			for (int32 Cell = 0; Cell < Near.Num(); ++Cell)
			{
				if (Near[Cell] != MAX_int32)
				{
					Changed.Add(Cell);
				}
			}
		}
		if (!Changed.IsEmpty())
		{
			RebuildCells(Changed);
		}
	}
	return bSetChanged;
}

void UHexPlanetObjects::UpdateClearRings()
{
	if (!Planet || RimSets.IsEmpty())
	{
		ClearRings.Reset();
		return;
	}
	ClearRings = HexPlanetUnderground::GetRingDistances(Planet->GetGrid(), ClearCells.Array(), RimClearRings);
}

FString UHexPlanetObjects::GetRimText(int32 Cell) const
{
	if (!Planet)
	{
		return FString();
	}
	const TArray<int8>& Levels = Planet->GetShownLevels();
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	if (!Cells.IsValidIndex(Cell) || Levels.Num() != Cells.Num())
	{
		return FString();
	}
	TArray<FString> Parts;
	for (const int32 SetIndex : RimSets)
	{
		const UHexPlanetObjectSet& Set = *Sets[SetIndex];
		const int32 Rings = FMath::Clamp(Set.ExitClearRings, 0, 3);
		const uint32 IdHash = FCrc::StrCrc32(*Set.Id.ToString());
		const auto IsSkipped = [this, Rings](int32 Each) { return ClearRings.IsValidIndex(Each) ? ClearRings[Each] <= Rings : IsClear(Each); };
		int32 OverVoid = 0;
		int32 NearExit = 0;
		int32 Taken = 0;
		for (int32 Side = 0; Side < Cells[Cell].Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Cells[Cell].Neighbors[Side];
			if (int32(Levels[Neighbor]) > int32(Levels[Cell]) - FMath::Max(Set.RimMinDrop, 1))
			{
				continue;
			}
			++OverVoid;
			if (IsSkipped(Cell) || IsSkipped(Neighbor))
			{
				++NearExit;
				continue;
			}
			// the share of the sides, by the first draw of the stream of the side, as PlaceRims takes it
			Taken += FRandomStream(int32(HashCombine(GetTypeHash(Cell * 8 + Side), IdHash))).FRand() < Set.RimSideShare ? 1 : 0;
		}
		if (OverVoid > 0)
		{
			Parts.Add(FString::Printf(TEXT("%s on %d of %d sides over a void%s"), *Set.Id.ToString(), Taken, OverVoid,
				NearExit > 0 ? *FString::Printf(TEXT(", %d left out at an exit or within %d rings of one"), NearExit, Rings) : TEXT("")));
		}
	}
	return FString::Join(Parts, TEXT("; "));
}

bool UHexPlanetObjects::IsSlotShown(int32 Slot) const
{
	return bObjectsShown && (!Slots.IsValidIndex(Slot) || Sets[Slots[Slot].Set]->Layer != EHexObjectLayer::Rim || bRimsShown);
}

void UHexPlanetObjects::SetShown(bool bInObjects, bool bInRims)
{
	if (bInObjects == bObjectsShown && bInRims == bRimsShown)
	{
		return;
	}
	bObjectsShown = bInObjects;
	bRimsShown = bInRims;
	SetVisibility(bObjectsShown, false);
	for (const TPair<int64, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Each : Components)
	{
		if (Each.Value)
		{
			Each.Value->SetVisibility(IsSlotShown(int32(Each.Key % 65536)));
		}
	}
}

void UHexPlanetObjects::SetFadeFocus(const FLinearColor& Center)
{
	if (Center == FadeCenter)
	{
		return;
	}
	FadeCenter = Center;
	for (UMaterialInstanceDynamic* Instance : FadeMaterials)
	{
		if (Instance)
		{
			Instance->SetVectorParameterValue(TEXT("WallFadeCenter"), Center);
		}
	}
}

void UHexPlanetObjects::SetCells(const TMap<int32, FHexCellObjects>& Changes)
{
	// what blocks a hero may have changed
	UHexHeroes::NoteMapChanged();
	TArray<int32> Changed;
	for (const TPair<int32, FHexCellObjects>& Each : Changes)
	{
		if (CellObjects.IsValidIndex(Each.Key) && !(CellObjects[Each.Key] == Each.Value))
		{
			CellObjects[Each.Key] = Each.Value;
			Changed.Add(Each.Key);
		}
	}
	RebuildCells(Changed);
}

void UHexPlanetObjects::RebuildCells(TConstArrayView<int32> Cells)
{
	TArray<int32> Chunks;
	for (const int32 Cell : Cells)
	{
		if (Planet->GetGrid().GetCells().IsValidIndex(Cell))
		{
			Chunks.AddUnique(Planet->GetGrid().GetCells()[Cell].Chunk);
		}
	}
	if (!Chunks.IsEmpty())
	{
		RebuildChunks(Chunks);
	}
}

void UHexPlanetObjects::RebuildChunks(TConstArrayView<int32> Chunks)
{
	if (Slots.IsEmpty() || CellObjects.IsEmpty())
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();
	TArray<TArray<FInstance>> Placed;
	TArray<FPlaceStats> Stats;
	Placed.SetNum(Chunks.Num());
	Stats.SetNum(Chunks.Num());
	ParallelFor(Chunks.Num(), [&](int32 Index)
	{
		Placed[Index] = PlaceChunk(Chunks[Index], Stats[Index]);
	});
	const double ApplyTime = FPlatformTime::Seconds();

	int32 Total = 0;
	int32 PlacedLeftOut = 0;
	for (int32 Index = 0; Index < Chunks.Num(); ++Index)
	{
		ApplyChunk(Chunks[Index], Placed[Index]);
		Total += Placed[Index].Num();
		PlacedLeftOut += Stats[Index].PlacedLeftOut;
	}
	if (PlacedLeftOut > 0)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: %d objects placed by hand in %d chunks are not drawn, as their level, biome or ground no longer allows them"), PlacedLeftOut, Chunks.Num());
	}

	if (Chunks.Num() > 1)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: placed %d objects in %d chunks in %.1f ms, shown in %.1f ms"), Total, Chunks.Num(),
			(ApplyTime - StartTime) * 1000.0, (FPlatformTime::Seconds() - ApplyTime) * 1000.0);
	}
	{
		// underground, where the meshes keep off the voids (bClipFootprint), how many each kind placed and in how many of its cells none fitted,
		// and the rims apart, by the sides they stand on, after every rebuild, as an edit of the maze or the entrances moves the rims
		if (Planet->GetLayer() == EHexPlanetLayer::Underground)
		{
			TArray<int32> Instances;
			TArray<int32> Cells;
			TArray<int32> Sides;
			int32 HangMissed = 0;
			Instances.Init(0, Sets.Num());
			Cells.Init(0, Sets.Num());
			Sides.Init(0, Sets.Num());
			for (int32 Index = 0; Index < Chunks.Num(); ++Index)
			{
				for (const FInstance& Instance : Placed[Index])
				{
					++Instances[Slots[Instance.Slot].Set];
				}
				for (int32 Set = 0; Set < Stats[Index].EmptyCells.Num(); ++Set)
				{
					Cells[Set] += Stats[Index].EmptyCells[Set];
					Sides[Set] += Stats[Index].RimSides[Set];
				}
				HangMissed += Stats[Index].HangMissed;
			}
			TArray<FString> Parts;
			TArray<FString> RimParts;
			int32 RimTotal = 0;
			for (int32 Set = 0; Set < Sets.Num(); ++Set)
			{
				if (Sets[Set]->Layer == EHexObjectLayer::Rim)
				{
					RimParts.Add(FString::Printf(TEXT("%s %d on %d sides"), *Sets[Set]->Id.ToString(), Instances[Set], Sides[Set]));
					RimTotal += Instances[Set];
					continue;
				}
				Parts.Add(FString::Printf(TEXT("%s %d (none fitted in %d cells)"), *Sets[Set]->Id.ToString(), Instances[Set], Cells[Set]));
			}
			UE_LOG(LogHexHero, Log, TEXT("Planet: objects of the underground by kind in %d chunks: %s"), Chunks.Num(), *FString::Join(Parts, TEXT(", ")));
			if (!RimParts.IsEmpty())
			{
				UE_LOG(LogHexHero, Log, TEXT("Planet: rims of the underground in %d chunks, %d objects: %s; %d tries to hang one found no cliff below the lip"), Chunks.Num(),
					RimTotal, *FString::Join(RimParts, TEXT(", ")), HangMissed);
			}
		}
	}

	// the ground of the cells shows their covers from far away, which changes with the objects, their biomes and their levels
	Planet->UpdateCanopy();
}

TArray<UHexPlanetObjects::FInstance> UHexPlanetObjects::PlaceChunk(int32 Chunk, FPlaceStats& OutStats) const
{
	TArray<int32>& OutEmptyCells = OutStats.EmptyCells;
	OutEmptyCells.Init(0, Sets.Num());
	OutStats.RimSides.Init(0, Sets.Num());
	// the cells with something to place, whose ground is found in parallel, as the planet may build it again for that, keeping its surface on
	// the GPU alone (AHexPlanet::GetCellTriangles)
	TArray<int32> Placed;
	for (const int32 Cell : ChunkCells[Chunk])
	{
		const FHexCellObjects& Objects = CellObjects[Cell];
		const int32 Level = Planet->GetCellLevel(Cell);
		// an entrance keeps its cell free, so trees do not cover its cave mouth
		if (!Objects.IsEmpty() && !IsClear(Cell) && ((Objects.Cover != 0 && IsAllowed(Objects.Cover - 1, Level)) || (Objects.Decor != 0 && IsAllowed(Objects.Decor - 1, Level))
			|| !Objects.Placed.IsEmpty()))
		{
			Placed.Add(Cell);
		}
	}
	TArray<TArray<FVector3f>> Grounds;
	Grounds.SetNum(Placed.Num());
	ParallelFor(Placed.Num(), [&](int32 Index)
	{
		Planet->GetCellTriangles(Placed[Index], Grounds[Index]);
	});

	TArray<FInstance> Instances;
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		const int32 Cell = Placed[Index];
		const FHexCellObjects& Objects = CellObjects[Cell];
		const int32 Level = Planet->GetCellLevel(Cell);
		if (Objects.Cover != 0 && IsAllowed(Objects.Cover - 1, Level))
		{
			const int32 Before = Instances.Num();
			PlaceInCell(Cell, Objects.Cover - 1, Objects.Density, false, Grounds[Index], Instances);
			OutEmptyCells[Objects.Cover - 1] += Instances.Num() == Before ? 1 : 0;
		}
		if (Objects.Decor != 0 && IsAllowed(Objects.Decor - 1, Level))
		{
			const int32 Before = Instances.Num();
			PlaceInCell(Cell, Objects.Decor - 1, 1, true, Grounds[Index], Instances);
			OutEmptyCells[Objects.Decor - 1] += Instances.Num() == Before ? 1 : 0;
		}
	}
	// what was placed by hand after all the covers and decor of the chunk, as an instance takes its random (its tint, the phase of its sway) by its
	// place among those of its mesh, so placing, moving or taking away one by hand leaves those of the forests as they were
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		OutStats.PlacedLeftOut += PlacePlaced(Placed[Index], Grounds[Index], Instances);
	}
	PlaceRims(Chunk, Instances, OutStats);
	return Instances;
}

void UHexPlanetObjects::GetCellAxes(const FVector3d& Center, FVector3d& OutX, FVector3d& OutY)
{
	// as FCellGround flattens the ground of the cell
	OutX = FVector3d::CrossProduct(FMath::Abs(Center.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX(), Center).GetSafeNormal();
	OutY = FVector3d::CrossProduct(Center, OutX);
}

FVector2f UHexPlanetObjects::GetPlacedFlat(const FHexPlacedObject& Object) const
{
	// the middle of a cell flattens to the origin of its plane, as its axes are across its direction
	return Object.Offset * Settings->HexRadius;
}

FVector2f UHexPlanetObjects::GetPlacedOffset(int32 Cell, const FVector3d& Point) const
{
	FVector3d AxisX;
	FVector3d AxisY;
	GetCellAxes(Planet->GetGrid().GetCells()[Cell].Center, AxisX, AxisY);
	// as objects.json keeps it, so a save and a load put an object back where it was
	const auto Round = [](double Value) { return float(FMath::RoundToDouble(Value * 1000.0) / 1000.0); };
	return FVector2f(Round(FVector3d::DotProduct(Point, AxisX) / Settings->HexRadius), Round(FVector3d::DotProduct(Point, AxisY) / Settings->HexRadius));
}

FVector3d UHexPlanetObjects::GetPlacedPoint(int32 Cell, const FHexPlacedObject& Object, double AtRadius) const
{
	const FVector3d Center = Planet->GetGrid().GetCells()[Cell].Center;
	FVector3d AxisX;
	FVector3d AxisY;
	GetCellAxes(Center, AxisX, AxisY);
	const FVector2f Flat = GetPlacedFlat(Object);
	return (Center * AtRadius + AxisX * Flat.X + AxisY * Flat.Y).GetSafeNormal() * AtRadius;
}

int32 UHexPlanetObjects::GetMeshCount(int32 Cell, int32 Set) const
{
	return AllSlots.IsValidIndex(Set) ? AllSlots[Set].Num() : 0;
}

int32 UHexPlanetObjects::GetPlacedSlot(int32 Cell, const FHexPlacedObject& Object) const
{
	// among all the meshes of its kind, so a change of the biome keeps the object, and a kind of the other layer has its meshes here too
	const int32 Set = int32(Object.Set) - 1;
	if (!AllSlots.IsValidIndex(Set) || AllSlots[Set].IsEmpty())
	{
		return INDEX_NONE;
	}
	return AllSlots[Set][int32(Object.Mesh) % AllSlots[Set].Num()];
}

void UHexPlanetObjects::GetSlotLook(int32 Cell, int32 Slot, float& OutScaleFactor, float& OutSnow) const
{
	// as the variant of the biome draws the mesh, so an object unpacked from a cover stands as it stood, else as the first variant that has it
	const int32 Set = Slots[Slot].Set;
	const FHexObjectVariant* Variant = Sets[Set]->FindVariant(Planet->GetCellBiomeLetter(Cell));
	const int32 VariantIndex = Variant ? int32(Variant - Sets[Set]->Variants.GetData()) : INDEX_NONE;
	if (VariantSlots[Set].IsValidIndex(VariantIndex) && VariantSlots[Set][VariantIndex].Contains(Slot))
	{
		OutScaleFactor = Variant->ScaleFactor;
		OutSnow = Variant->Snow;
		return;
	}
	OutScaleFactor = Slots[Slot].ScaleFactor;
	OutSnow = Slots[Slot].Snow;
}

bool UHexPlanetObjects::IsFootprintFree(int32 Cell, int32 Slot, float Scale, const FVector3d& Position, bool bRivers, FString* OutProblem) const
{
	const UHexPlanetObjectSet& Kind = *Sets[Slots[Slot].Set];
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const double Radius = Planet->GetRadius();
	const double Reach = Slots[Slot].FootprintRadius * Scale + Settings->ObjectFootprintMargin;
	const auto Refuse = [OutProblem](const TCHAR* Why)
	{
		if (OutProblem)
		{
			*OutProblem = Why;
		}
		return false;
	};
	// underground every mesh keeps off the void, as a tree of the surface there would hang over it as much as a stalagmite
	if (Kind.bClipFootprint || Planet->GetLayer() == EHexPlanetLayer::Underground)
	{
		// off the sides towards a void, whose cliff the ground of the void carries, and towards a cleared cell, an exit, as PlaceInCell keeps them
		FVector3d AxisX;
		FVector3d AxisY;
		GetCellAxes(Each.Center, AxisX, AxisY);
		const auto Flatten = [&AxisX, &AxisY](const FVector3d& Point) { return FVector2f(float(FVector3d::DotProduct(Point, AxisX)), float(FVector3d::DotProduct(Point, AxisY))); };
		uint8 ClipSides = 0;
		TArray<FVector2f, TInlineAllocator<6>> FlatCorners;
		const int32 Level = Planet->GetCellLevel(Cell);
		for (int32 Side = 0; Side < Each.Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Each.Neighbors[Side];
			if (Planet->GetCellLevel(Neighbor) <= Level - 2 || IsClear(Neighbor))
			{
				ClipSides |= uint8(1u << Side);
			}
		}
		for (const int32 Corner : Each.Corners)
		{
			FlatCorners.Add(Flatten(Grid.GetCorners()[Corner] * Radius));
		}
		if (ClipSides != 0 && !HexPlanetUnderground::IsFootprintClear(Flatten(Position), float(Reach), FlatCorners, ClipSides))
		{
			return Refuse(TEXT("it would hang over the void or stand on an exit"));
		}
		// nor in a wall on a lip or on its foot
		if (Planet->HasWalls() && HexPlanetWalls::GetDistanceToLine(Planet->GetWalls(), Cell, Position) < HexPlanetWalls::GetFootReach(Planet->GetWallParams()) + Reach)
		{
			return Refuse(TEXT("it would stand in a wall"));
		}
	}
	// not in a river, nor on its banks
	if (bRivers && Planet->GetRivers() && !Planet->GetRivers()->GetShownNet().IsEmpty()
		&& Planet->GetRivers()->GetRiverDistance(Cell, Position) < Settings->RiverWidth * 0.5 + Settings->RiverMeander + Settings->RiverBankWidth + Kind.Spacing * Scale * 0.5)
	{
		return Refuse(TEXT("it would stand in a river"));
	}
	return true;
}

bool UHexPlanetObjects::CanPlace(int32 Cell, const FHexPlacedObject& Object, FString* OutProblem) const
{
	const auto Refuse = [OutProblem](const FString& Why)
	{
		if (OutProblem)
		{
			*OutProblem = Why;
		}
		return false;
	};
	const int32 Set = int32(Object.Set) - 1;
	if (!Sets.IsValidIndex(Set) || !CellObjects.IsValidIndex(Cell) || Sets[Set]->Layer == EHexObjectLayer::Rim)
	{
		return Refuse(TEXT("there is no such kind of object"));
	}
	const UHexPlanetObjectSet& Kind = *Sets[Set];
	// a mass of the underground fills its cell, which would stand in the way of a passage it does not close; a tree of a forest or a boulder may
	// stand alone on either layer, and nothing placed by hand blocks
	if (Kind.IsCellMass())
	{
		return Refuse(FString::Printf(TEXT("%s fills and blocks its cell, so it is painted on cells and not placed by hand"), *Kind.DisplayName));
	}
	if (!IsPlacedLevel(Planet->GetCellLevel(Cell)))
	{
		return Refuse(Planet->GetLayer() == EHexPlanetLayer::Underground ? TEXT("nothing stands in the void") : TEXT("nothing stands in the sea"));
	}
	if (IsClear(Cell))
	{
		return Refuse(TEXT("nothing stands on an entrance"));
	}
	const int32 Slot = GetPlacedSlot(Cell, Object);
	if (Slot == INDEX_NONE)
	{
		return Refuse(FString::Printf(TEXT("%s has no mesh"), *Kind.DisplayName));
	}

	// on the ground of the cell, not under the sea
	TArray<FVector3f> Triangles;
	Planet->GetCellTriangles(Cell, Triangles);
	FCellGround Ground;
	Ground.Build(FVector3f(Planet->GetGrid().GetCells()[Cell].Center), Triangles);
	FVector3f Position;
	FVector3f Normal;
	if (!Ground.Find(GetPlacedFlat(Object), Position, Normal))
	{
		return Refuse(TEXT("there is no ground there"));
	}
	if (Settings->bSea && Position.Length() - Planet->GetRadius() < 0.5)
	{
		return Refuse(TEXT("it would stand under the sea"));
	}
	float ScaleFactor = 1.0f;
	float Snow = 0.0f;
	GetSlotLook(Cell, Slot, ScaleFactor, Snow);
	FString Problem;
	if (!IsFootprintFree(Cell, Slot, Object.Scale * ScaleFactor, FVector3d(Position), true, &Problem))
	{
		return Refuse(Problem);
	}
	return true;
}

bool UHexPlanetObjects::MakePlaced(int32 Cell, int32 Set, const FVector3d& Point, FHexPlacedObject& OutObject, FString& OutProblem) const
{
	if (!Sets.IsValidIndex(Set) || !CellObjects.IsValidIndex(Cell))
	{
		OutProblem = TEXT("there is no such kind of object");
		return false;
	}
	const UHexPlanetObjectSet& Kind = *Sets[Set];
	const auto Round = [](double Value, double Step) { return float(FMath::RoundToDouble(Value / Step) * Step); };
	OutObject = FHexPlacedObject();
	OutObject.Set = uint8(Set + 1);
	OutObject.Offset = GetPlacedOffset(Cell, Point);

	// its turn, scale and mesh by a hash of where it stands and its kind, the Id as text, as the hash of an FName changes between runs; a mesh of
	// the variant of the biome where the kind has one, else any of its meshes
	const FIntPoint Spot(FMath::RoundToInt32(OutObject.Offset.X * 1000.0f), FMath::RoundToInt32(OutObject.Offset.Y * 1000.0f));
	FRandomStream Random(int32(HashCombine(HashCombine(GetTypeHash(Cell), GetTypeHash(Spot)), FCrc::StrCrc32(*Kind.Id.ToString()))));
	OutObject.Yaw = Round(Random.FRandRange(0.0f, 360.0f), 0.1);
	OutObject.Scale = Round(Random.FRandRange(Kind.ScaleMin, FMath::Max(Kind.ScaleMin, Kind.ScaleMax)), 0.001);
	const FHexObjectVariant* Variant = Kind.FindVariant(Planet->GetCellBiomeLetter(Cell));
	const TArray<int32>* VariantMeshes = Variant ? &VariantSlots[Set][int32(Variant - Kind.Variants.GetData())] : nullptr;
	if (VariantMeshes && !VariantMeshes->IsEmpty())
	{
		OutObject.Mesh = uint8(FMath::Max(AllSlots[Set].IndexOfByKey((*VariantMeshes)[Random.RandHelper(VariantMeshes->Num())]), 0));
	}
	else if (!AllSlots[Set].IsEmpty())
	{
		OutObject.Mesh = uint8(Random.RandHelper(AllSlots[Set].Num()));
	}
	return CanPlace(Cell, OutObject, &OutProblem);
}

bool UHexPlanetObjects::UnpackCell(int32 Cell, FHexCellObjects& OutObjects) const
{
	if (!CellObjects.IsValidIndex(Cell) || IsClear(Cell))
	{
		return false;
	}
	const FHexCellObjects& Objects = CellObjects[Cell];
	const int32 Level = Planet->GetCellLevel(Cell);
	const bool bCover = Objects.Cover != 0 && IsAllowed(Objects.Cover - 1, Level);
	const bool bDecor = Objects.Decor != 0 && IsAllowed(Objects.Decor - 1, Level);
	if (!bCover && !bDecor)
	{
		return false;
	}
	// each of its objects where PlaceInCell stands it, in front of what was placed by hand, as they are drawn first
	TArray<FVector3f> Triangles;
	Planet->GetCellTriangles(Cell, Triangles);
	TArray<FInstance> Ignored;
	TArray<FHexPlacedObject> Unpacked;
	if (bCover)
	{
		PlaceInCell(Cell, Objects.Cover - 1, Objects.Density, false, Triangles, Ignored, &Unpacked);
	}
	if (bDecor)
	{
		PlaceInCell(Cell, Objects.Decor - 1, 1, true, Triangles, Ignored, &Unpacked);
	}
	OutObjects = Objects;
	if (bCover)
	{
		OutObjects.Cover = 0;
		OutObjects.Density = 0;
	}
	if (bDecor)
	{
		OutObjects.Decor = 0;
	}
	Unpacked.Append(Objects.Placed);
	OutObjects.Placed = MoveTemp(Unpacked);
	return true;
}

bool UHexPlanetObjects::FindObjectNear(const FVector3d& Point, double MaxDistance, int32& OutCell, int32& OutIndex, FHexCellObjects& OutObjects) const
{
	OutCell = INDEX_NONE;
	OutIndex = INDEX_NONE;
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const int32 Middle = Planet->GetGrid().FindCell(Point.GetSafeNormal(), 0);
	if (!Cells.IsValidIndex(Middle))
	{
		return false;
	}
	TArray<int32, TInlineAllocator<7>> Near{Middle};
	Near.Append(Cells[Middle].Neighbors);
	double Best = MaxDistance;
	for (const int32 Cell : Near)
	{
		// what a cover or decor stands there as, unpacked, and what was placed by hand, as nothing stands on an entrance
		if (IsClear(Cell))
		{
			continue;
		}
		FHexCellObjects Objects;
		if (!UnpackCell(Cell, Objects))
		{
			Objects = CellObjects[Cell];
		}
		for (int32 Index = 0; Index < Objects.Placed.Num(); ++Index)
		{
			const double Distance = FVector3d::Dist(GetPlacedPoint(Cell, Objects.Placed[Index], Point.Size()), Point);
			if (Distance <= Best)
			{
				Best = Distance;
				OutCell = Cell;
				OutIndex = Index;
				OutObjects = Objects;
			}
		}
	}
	return OutCell != INDEX_NONE;
}

bool UHexPlanetObjects::MakePlacedInstance(int32 Cell, const FHexPlacedObject& Object, const FCellGround& Ground, FInstance& Out) const
{
	const int32 Set = int32(Object.Set) - 1;
	const int32 Slot = GetPlacedSlot(Cell, Object);
	FVector3f Position;
	FVector3f Normal;
	if (Slot == INDEX_NONE || !Ground.Find(GetPlacedFlat(Object), Position, Normal))
	{
		return false;
	}
	const UHexPlanetObjectSet& Kind = *Sets[Set];
	float ScaleFactor = 1.0f;
	float Snow = 0.0f;
	GetSlotLook(Cell, Slot, ScaleFactor, Snow);
	const float Scale = Object.Scale * ScaleFactor;

	// up the planet, leaning with the ground by SlopeAlign as PlaceInCell stands them, and turned by the yaw placed
	const FVector3f Up = Position.GetSafeNormal();
	const FVector3f Axis = FMath::Lerp(Up, Normal, Kind.SlopeAlign).GetSafeNormal();
	const FVector3f Side = FVector3f::CrossProduct(Axis, FMath::Abs(Axis.Z) < 0.9f ? FVector3f::UnitZ() : FVector3f::UnitX()).GetSafeNormal();
	const FQuat4f Base = FRotationMatrix44f::MakeFromZX(Axis, Side).ToQuat();
	const FQuat4f Rotation = FQuat4f(Axis, FMath::DegreesToRadians(Object.Yaw)) * Base;
	Out.Slot = Slot;
	Out.Transform = FTransform(FQuat(Rotation), FVector(Position - Axis * Kind.Sink * Scale), FVector(Scale));
	Out.Rock = float(Planet->GetCellRockLayer(Cell));
	Out.Snow = Snow;
	return true;
}

int32 UHexPlanetObjects::PlacePlaced(int32 Cell, const TArray<FVector3f>& Triangles, TArray<FInstance>& Out) const
{
	const TArray<FHexPlacedObject>& Placed = CellObjects[Cell].Placed;
	if (Placed.IsEmpty())
	{
		return 0;
	}
	if (Triangles.IsEmpty() || !IsPlacedLevel(Planet->GetCellLevel(Cell)))
	{
		return Placed.Num();
	}
	FCellGround Ground;
	Ground.Build(FVector3f(Planet->GetGrid().GetCells()[Cell].Center), Triangles);
	int32 LeftOut = 0;
	for (const FHexPlacedObject& Object : Placed)
	{
		FInstance Instance;
		// underground a mesh does not hang over a void that the maze opened beside it since it was placed
		if (!MakePlacedInstance(Cell, Object, Ground, Instance)
			|| ((Sets[Object.Set - 1]->bClipFootprint || Planet->GetLayer() == EHexPlanetLayer::Underground)
				&& !IsFootprintFree(Cell, Instance.Slot, float(Instance.Transform.GetScale3D().X), Instance.Transform.GetLocation(), false, nullptr)))
		{
			++LeftOut;
			continue;
		}
		Out.Add(Instance);
	}
	return LeftOut;
}

void UHexPlanetObjects::SetDragPreview(int32 Cell, const FHexPlacedObject* Object)
{
	FInstance Instance;
	bool bShown = false;
	if (Object && CellObjects.IsValidIndex(Cell))
	{
		TArray<FVector3f> Triangles;
		Planet->GetCellTriangles(Cell, Triangles);
		FCellGround Ground;
		Ground.Build(FVector3f(Planet->GetGrid().GetCells()[Cell].Center), Triangles);
		bShown = !Triangles.IsEmpty() && MakePlacedInstance(Cell, *Object, Ground, Instance);
	}
	if (!bShown)
	{
		if (DragPreview)
		{
			DragPreview->SetVisibility(false);
		}
		return;
	}
	if (!DragPreview)
	{
		DragPreview = NewObject<UInstancedStaticMeshComponent>(this, TEXT("DragPreview"));
		DragPreview->SetupAttachment(this);
		DragPreview->NumCustomDataFloats = 2;
		DragPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DragPreview->SetCanEverAffectNavigation(false);
		DragPreview->bAffectDistanceFieldLighting = false;
		DragPreview->RegisterComponent();
	}
	// a mesh of its own, and its one instance moved, which keeps the shadows and the motion of the frames before
	if (DragPreviewSlot != Instance.Slot || DragPreview->GetInstanceCount() != 1)
	{
		DragPreview->ClearInstances();
		DragPreview->SetStaticMesh(Slots[Instance.Slot].Mesh);
		SetSlotMaterials(DragPreview, Instance.Slot);
		DragPreview->SetCastShadow(Sets[Slots[Instance.Slot].Set]->bCastShadow);
		DragPreview->AddInstance(Instance.Transform, true);
		DragPreviewSlot = Instance.Slot;
	}
	else
	{
		DragPreview->UpdateInstanceTransform(0, Instance.Transform, true, true);
	}
	DragPreview->SetCustomDataValue(0, 0, Instance.Rock, false);
	DragPreview->SetCustomDataValue(0, 1, Instance.Snow, true);
	DragPreview->SetVisibility(bObjectsShown);
}

void UHexPlanetObjects::SetSlotMaterials(UStaticMeshComponent* Component, int32 Slot)
{
	const UHexPlanetObjectSet& Set = *Sets[Slots[Slot].Set];
	// what stands on the walls takes their material for objects, which dissolves with them in front of the point under the cursor
	UMaterialInterface* WallObjectMaterial = Set.Layer == EHexObjectLayer::Rim && Set.RimSeat != EHexRimSeat::Lip && Planet->HasWalls()
		? Planet->GetWallObjectMaterial() : nullptr;
	UMaterialInterface* SetMaterial = WallObjectMaterial ? nullptr : Set.Material.LoadSynchronous();
	for (int32 Element = 0; Element < Slots[Slot].Mesh->GetStaticMaterials().Num(); ++Element)
	{
		UMaterialInterface* Parent = WallObjectMaterial ? WallObjectMaterial : SetMaterial ? SetMaterial : Slots[Slot].Mesh->GetMaterial(Element);
		Component->SetMaterial(Element, GetMaterial(Parent, &Set));
	}
}

void UHexPlanetObjects::PlaceRims(int32 Chunk, TArray<FInstance>& Out, FPlaceStats& Stats) const
{
	const TArray<int8>& Levels = Planet->GetShownLevels();
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (RimSets.IsEmpty() || Levels.Num() != Cells.Num())
	{
		return;
	}

	// the sides of each kind, by the levels of the surface shown, whose ground the rims stand on, without a cleared cell or, for a kind that keeps
	// clear of exits, a cell near one; a side takes a kind by a hash, of which the stream of the side draws first
	const auto GetSideStream = [](int32 Cell, int32 Side, uint32 IdHash) { return FRandomStream(int32(HashCombine(GetTypeHash(Cell * 8 + Side), IdHash))); };
	TArray<TArray<FIntPoint>> SidesBySet;
	SidesBySet.SetNum(RimSets.Num());
	TArray<int32> GroundCells;
	TMap<int32, int32> GroundOf;
	const auto NeedGround = [&GroundCells, &GroundOf](int32 Cell)
	{
		if (!GroundOf.Contains(Cell))
		{
			GroundOf.Add(Cell, GroundCells.Add(Cell));
		}
	};
	// on a layer with walls a kind with a seat on them stands at points of the walls instead of along the sides
	const bool bWalls = Planet->HasWalls();
	for (int32 Rim = 0; Rim < RimSets.Num(); ++Rim)
	{
		const UHexPlanetObjectSet& Set = *Sets[RimSets[Rim]];
		if (bWalls && Set.RimSeat != EHexRimSeat::Lip)
		{
			continue;
		}
		const int32 Rings = FMath::Clamp(Set.ExitClearRings, 0, 3);
		const uint32 IdHash = FCrc::StrCrc32(*Set.Id.ToString());
		HexPlanetUnderground::FindRimSides(Grid, Levels, FMath::Max(Set.RimMinDrop, 1), [this, Rings](int32 Cell)
		{
			return ClearRings.IsValidIndex(Cell) ? ClearRings[Cell] <= Rings : IsClear(Cell);
		}, ChunkCells[Chunk], SidesBySet[Rim]);
		SidesBySet[Rim].RemoveAll([&](const FIntPoint& Pair)
		{
			return !Set.AllowedLevels.Contains(int32(Levels[Pair.X])) || !Set.FindVariant(Planet->GetCellBiomeLetter(Pair.X))
				|| GetSideStream(Pair.X, Pair.Y, IdHash).FRand() >= Set.RimSideShare;
		});
		for (const FIntPoint& Pair : SidesBySet[Rim])
		{
			NeedGround(Pair.X);
			if (Set.bRimHang)
			{
				NeedGround(Cells[Pair.X].Neighbors[Pair.Y]);
			}
		}
		Stats.RimSides[RimSets[Rim]] = SidesBySet[Rim].Num();
	}
	if (bWalls)
	{
		PlaceOnWalls(Chunk, Out, Stats);
	}
	if (GroundCells.IsEmpty())
	{
		return;
	}

	// the ground of those cells, found in parallel as the planet may build it again, flattened onto the plane of each
	TArray<TArray<FVector3f>> Triangles;
	TArray<FCellGround> Grounds;
	Triangles.SetNum(GroundCells.Num());
	Grounds.SetNum(GroundCells.Num());
	const double Radius = Planet->GetRadius();
	ParallelFor(GroundCells.Num(), [&](int32 Index)
	{
		Planet->GetCellTriangles(GroundCells[Index], Triangles[Index]);
		Grounds[Index].Build(FVector3f(Cells[GroundCells[Index]].Center), Triangles[Index]);
	});

	// a side of a cell on its plane: its corners, the way into the cell across it and the apothem there
	struct FFlatSide
	{
		FVector2f From;
		FVector2f To;
		FVector2f Inward;
		float Apothem = 0.0f;
	};
	const auto Flatten = [](const FCellGround& Ground, const FVector3f& From, const FVector3f& To, const FVector3f& Center)
	{
		FFlatSide Flat;
		Flat.From = Ground.Flatten(From);
		Flat.To = Ground.Flatten(To);
		const FVector2f Middle = Ground.Flatten(Center);
		const FVector2f Along = (Flat.To - Flat.From).GetSafeNormal();
		Flat.Inward = FVector2f(-Along.Y, Along.X);
		Flat.Apothem = FVector2f::DotProduct(Middle - Flat.From, Flat.Inward);
		if (Flat.Apothem < 0.0f)
		{
			Flat.Inward = -Flat.Inward;
			Flat.Apothem = -Flat.Apothem;
		}
		return Flat;
	};
	// the lip lies this far onto the passage, where its ground is found, so a point on the side does not fall between its triangles
	constexpr float LipInset = 0.5f;

	for (int32 Rim = 0; Rim < RimSets.Num(); ++Rim)
	{
		const int32 SetIndex = RimSets[Rim];
		const UHexPlanetObjectSet& Set = *Sets[SetIndex];
		const uint32 IdHash = FCrc::StrCrc32(*Set.Id.ToString());
		const float MinUp = FMath::Clamp(Set.MinUp, 0.0f, 1.0f);
		const float TurnRange = FMath::DegreesToRadians(Set.RimTurn);

		// the rims of the kind on the cell, from side to side, kept Spacing apart by their sizes
		int32 SpacedCell = INDEX_NONE;
		TArray<FVector3f, TInlineAllocator<32>> Taken;
		TArray<float, TInlineAllocator<32>> TakenSizes;

		for (const FIntPoint& Pair : SidesBySet[Rim])
		{
			const int32 Cell = Pair.X;
			const int32 Side = Pair.Y;
			const FHexSphereCell& Each = Cells[Cell];
			const int32 Lower = Each.Neighbors[Side];
			const FHexObjectVariant* Variant = Set.FindVariant(Planet->GetCellBiomeLetter(Cell));
			const TArray<int32>& Meshes = VariantSlots[SetIndex][int32(Variant - Set.Variants.GetData())];
			if (Meshes.IsEmpty() || Set.CountByDensity.IsEmpty())
			{
				continue;
			}
			if (SpacedCell != Cell)
			{
				SpacedCell = Cell;
				Taken.Reset();
				TakenSizes.Reset();
			}

			FRandomStream Random = GetSideStream(Cell, Side, IdHash);
			Random.FRand();
			const int32 Most = FMath::Max(FMath::RoundToInt32(Set.CountByDensity[0] * Variant->CountFactor), 0);
			const int32 Least = FMath::Clamp(Set.RimCountMin, 0, Most);
			const int32 Count = Least + Random.RandHelper(Most - Least + 1);
			if (Count <= 0)
			{
				continue;
			}

			// the side on the plane of the passage and, for a rim that hangs, on that of the void, the same way along it on both
			const FVector3f CornerFrom = FVector3f(Grid.GetCorners()[Each.Corners[(Side + Each.Corners.Num() - 1) % Each.Corners.Num()]] * Radius);
			const FVector3f CornerTo = FVector3f(Grid.GetCorners()[Each.Corners[Side]] * Radius);
			const FVector3f Inside = FVector3f(Each.Center * Radius);
			const FCellGround& Upper = Grounds[GroundOf[Cell]];
			const FCellGround* Below = Set.bRimHang ? &Grounds[GroundOf[Lower]] : nullptr;
			const FFlatSide UpperSide = Flatten(Upper, CornerFrom, CornerTo, Inside);
			const FFlatSide LowerSide = Below ? Flatten(*Below, CornerFrom, CornerTo, FVector3f(Cells[Lower].Center * Radius)) : FFlatSide();

			// the sides of the cell towards a cleared cell, which a rim keeps its footprint off, as nothing stands on or along an exit
			uint8 ClearSides = 0;
			TArray<FVector2f, TInlineAllocator<6>> FlatCorners;
			for (int32 Other = 0; Other < Each.Neighbors.Num(); ++Other)
			{
				ClearSides |= IsClear(Each.Neighbors[Other]) ? uint8(1u << Other) : uint8(0);
			}
			if (ClearSides != 0)
			{
				for (const int32 Corner : Each.Corners)
				{
					FlatCorners.Add(Upper.Flatten(FVector3f(Grid.GetCorners()[Corner] * Radius)));
				}
			}

			for (int32 Index = 0; Index < Count; ++Index)
			{
				// spread along the side, one to each stretch of it, with a few tries for the spacing
				for (int32 Try = 0; Try < 3; ++Try)
				{
					const float Gap = FMath::Clamp(Set.RimCornerGap, 0.0f, 0.45f);
					const float Along = Gap + (1.0f - 2.0f * Gap) * (float(Index) + Random.FRand()) / float(Count);
					float Scale = Random.FRandRange(Set.ScaleMin, FMath::Max(Set.ScaleMin, Set.ScaleMax)) * Variant->ScaleFactor;
					if (Random.FRand() < Set.GiantChance)
					{
						Scale *= Set.GiantScale;
					}
					const int32 Slot = Meshes[Random.RandHelper(Meshes.Num())];
					const float Turn = Random.FRandRange(-TurnRange, TurnRange);
					const float TiltAngle = FMath::DegreesToRadians(Random.FRandRange(0.0f, Set.Tilt));
					const float TiltDirection = Random.FRandRange(0.0f, 2.0f * UE_PI);
					const float Inset = LipInset + Random.FRand() * Set.RimInset * UpperSide.Apothem;
					const FVector2f OnSide = FMath::Lerp(UpperSide.From, UpperSide.To, Along);

					FVector3f Position;
					FQuat4f Rotation;
					if (!Below)
					{
						// on the lip, its -Y over the void, as the carved face of a block of runes, its X along the side
						const FVector2f Point = OnSide + UpperSide.Inward * Inset;
						if (ClearSides != 0 && !HexPlanetUnderground::IsFootprintClear(Point, Slots[Slot].FootprintRadius * Scale + Settings->ObjectFootprintMargin, FlatCorners, ClearSides))
						{
							continue;
						}
						FVector3f Normal;
						if (!Upper.Find(Point, Position, Normal))
						{
							continue;
						}
						const FVector3f Up = FVector3f(Planet->GetUpAt(FVector3d(Position)));
						if (FVector3f::DotProduct(Normal, Up) < MinUp)
						{
							continue;
						}
						const FVector3f Axis = FMath::Lerp(Up, Normal, Set.SlopeAlign).GetSafeNormal();
						FVector3f X = (CornerTo - CornerFrom) - Axis * FVector3f::DotProduct(CornerTo - CornerFrom, Axis);
						X = X.GetSafeNormal();
						if (FVector3f::DotProduct(FVector3f::CrossProduct(Axis, X), Inside - Position) < 0.0f)
						{
							X = -X;
						}
						X = FQuat4f(Axis, Turn).RotateVector(X);
						const FQuat4f Base = FRotationMatrix44f::MakeFromZX(Axis, X).ToQuat();
						const FVector3f TiltAxis = Base.GetAxisX() * FMath::Cos(TiltDirection) + Base.GetAxisY() * FMath::Sin(TiltDirection);
						Rotation = FQuat4f(TiltAxis, TiltAngle) * Base;
						Position -= Axis * Set.Sink * Scale;
					}
					else
					{
						// down the cliff of the void below the lip: the face a little past the side, and its plane taken up to the height of the
						// lip, found on the passage, so the top of the mesh lies there whatever the slope of the face
						const FVector2f Point = FMath::Lerp(LowerSide.From, LowerSide.To, Along) + LowerSide.Inward * Set.RimHangOut;
						FVector3f Face;
						FVector3f Normal;
						if (!Below->Find(Point, Face, Normal))
						{
							++Stats.HangMissed;
							continue;
						}
						const FVector3f Up = FVector3f(Planet->GetUpAt(FVector3d(Face)));
						if (FVector3f::DotProduct(Normal, Up) > Set.RimHangMaxUp)
						{
							++Stats.HangMissed;
							continue;
						}
						const FVector3f UpFace = (Up - Normal * FVector3f::DotProduct(Up, Normal)).GetSafeNormal();
						FVector3f Lip;
						FVector3f LipNormal;
						const float Rise = Upper.Find(OnSide + UpperSide.Inward * LipInset, Lip, LipNormal)
							? (Lip.Length() - Face.Length()) / FMath::Max(FVector3f::DotProduct(UpFace, Up), 0.05f) : 0.0f;
						Position = Face + UpFace * FMath::Max(Rise, 0.0f);
						const FQuat4f Base = FRotationMatrix44f::MakeFromZY(UpFace, Normal).ToQuat();
						const FVector3f TiltAxis = Base.GetAxisX() * FMath::Cos(TiltDirection) + Base.GetAxisZ() * FMath::Sin(TiltDirection);
						Rotation = FQuat4f(TiltAxis, TiltAngle) * FQuat4f(Normal, Turn) * Base;
						Position -= Normal * Set.Sink * Scale;
					}

					bool bFree = true;
					for (int32 Other = 0; Other < Taken.Num() && bFree; ++Other)
					{
						bFree = FVector3f::DistSquared(Taken[Other], Position) >= FMath::Square(Set.Spacing * (Scale + TakenSizes[Other]) * 0.5f);
					}
					if (!bFree)
					{
						continue;
					}
					Taken.Add(Position);
					TakenSizes.Add(Scale);

					// the rock of the cliff it caps, that of the lower cell
					FInstance& Instance = Out.AddDefaulted_GetRef();
					Instance.Slot = Slot;
					Instance.Transform = FTransform(FQuat(Rotation), FVector(Position), FVector(Scale));
					Instance.Rock = float(Planet->GetCellRockLayer(Lower));
					Instance.Snow = Variant->Snow;
					break;
				}
			}
		}
	}
}

void UHexPlanetObjects::PlaceOnWalls(int32 Chunk, TArray<FInstance>& Out, FPlaceStats& Stats) const
{
	const HexPlanetWalls::FWalls& Walls = Planet->GetWalls();
	const HexPlanetWalls::FParams& WallParams = Planet->GetWallParams();
	if (!Walls.ChunkSamples.IsValidIndex(Chunk))
	{
		return;
	}
	for (const int32 SetIndex : RimSets)
	{
		const UHexPlanetObjectSet& Set = *Sets[SetIndex];
		if (Set.RimSeat == EHexRimSeat::Lip)
		{
			continue;
		}
		const uint32 IdHash = FCrc::StrCrc32(*Set.Id.ToString());
		const float TurnRange = FMath::DegreesToRadians(Set.RimTurn);
		int32 Taken = 0;
		for (const FIntPoint& At : Walls.ChunkSamples[Chunk])
		{
			// a sample takes the kind by a hash of where it lies and the Id, so a rebuild that moves the start of its line keeps the objects
			// where they stood, and no pattern repeats along the walls
			const HexPlanetWalls::FSample& Sample = Walls.Get(At);
			// a wall lower than a parapet keeps nothing on its face, and one that is nearly gone nothing at its foot either
			const double Presence = HexPlanetWalls::GetPresence(Sample, WallParams);
			if (Presence < (Set.RimSeat == EHexRimSeat::WallFoot ? 0.5 : 0.999))
			{
				continue;
			}
			const FIntVector Key(FMath::RoundToInt32(Sample.Position.X * 0.5), FMath::RoundToInt32(Sample.Position.Y * 0.5), FMath::RoundToInt32(Sample.Position.Z * 0.5));
			FRandomStream Random(int32(HashCombine(GetTypeHash(Key), IdHash)));
			// more at the foot of an inside corner, where the line turns onto the passage about as tightly as its corners are rounded
			const double Corner = Set.RimSeat == EHexRimSeat::WallFoot && Sample.InnerReach < 1e9
				? FMath::Clamp(WallParams.CornerRound * 1.2 / FMath::Max(Sample.InnerReach, 1.0), 0.0, 1.0) : 0.0;
			if (Random.FRand() >= Set.RimSideShare * (1.0 + Set.RimCornerBoost * Corner))
			{
				continue;
			}
			const FHexObjectVariant* Variant = Set.FindVariant(Planet->GetCellBiomeLetter(Sample.Cell));
			if (!Variant || !Set.AllowedLevels.Contains(Planet->GetCellLevel(Sample.Cell)) || IsClear(Sample.Cell))
			{
				continue;
			}
			const TArray<int32>& Meshes = VariantSlots[SetIndex][int32(Variant - Set.Variants.GetData())];
			if (Meshes.IsEmpty())
			{
				continue;
			}
			float Scale = Random.FRandRange(Set.ScaleMin, FMath::Max(Set.ScaleMin, Set.ScaleMax)) * Variant->ScaleFactor;
			if (Random.FRand() < Set.GiantChance)
			{
				Scale *= Set.GiantScale;
			}
			const int32 Slot = Meshes[Random.RandHelper(Meshes.Num())];
			const float Turn = Random.FRandRange(-TurnRange, TurnRange);
			const float TiltAngle = FMath::DegreesToRadians(Random.FRandRange(0.0f, Set.Tilt));
			const float TiltDirection = Random.FRandRange(0.0f, 2.0f * UE_PI);
			const FVector3d Slide = Sample.Along * (Random.FRandRange(-0.5f, 0.5f) * WallParams.Step);

			FVector3d Position;
			FQuat4f Rotation;
			if (Set.RimSeat == EHexRimSeat::WallFoot)
			{
				// upright on the rounded foot, from its upper half to the floor before it, where the line cut a corner the floor the foot lays
				const double FootReach = WallParams.FootReach * Presence;
				const double FootEnd = FMath::Max(FootReach, Sample.Gap);
				const double Reach = FMath::Lerp(FootReach - WallParams.Fillet * Presence * 0.6, FootEnd + 3.0, double(Random.FRand()));
				const FVector3d FootDirection = (Sample.Position + Sample.Inward * Reach + Slide).GetSafeNormal();
				Position = FootDirection * HexPlanetWalls::GetFloorRadius(WallParams, FootDirection);
				const FVector3f Up = FVector3f(Planet->GetUpAt(Position));
				const FVector3f X = FQuat4f(Up, Turn).RotateVector(FVector3f(Sample.Along));
				const FQuat4f Base = FRotationMatrix44f::MakeFromZX(Up, X).ToQuat();
				const FVector3f TiltAxis = Base.GetAxisX() * FMath::Cos(TiltDirection) + Base.GetAxisY() * FMath::Sin(TiltDirection);
				Rotation = FQuat4f(TiltAxis, TiltAngle) * Base;
				Position -= FVector3d(Up) * (Set.Sink * Scale);
			}
			else
			{
				// out of the inner face at a height of the wall: one that hangs along the face down it in front of it, else standing out of it
				const double Height = HexPlanetWalls::GetHeight(Sample, WallParams)
					* Random.FRandRange(float(Set.RimWallHeights.X), float(FMath::Max(Set.RimWallHeights.X, Set.RimWallHeights.Y)));
				FVector3d Face;
				FVector3d Normal;
				HexPlanetWalls::GetInnerFace(Walls, At, Height, WallParams, Face, Normal);
				const FVector3f Out3 = FVector3f(Normal);
				const FVector3f Up = FVector3f(Sample.Up);
				if (Set.bRimHang)
				{
					const FVector3f UpFace = (Up - Out3 * FVector3f::DotProduct(Up, Out3)).GetSafeNormal();
					const FQuat4f Base = FRotationMatrix44f::MakeFromZY(UpFace, Out3).ToQuat();
					const FVector3f TiltAxis = Base.GetAxisX() * FMath::Cos(TiltDirection) + Base.GetAxisZ() * FMath::Sin(TiltDirection);
					Rotation = FQuat4f(TiltAxis, TiltAngle) * FQuat4f(Out3, Turn) * Base;
				}
				else
				{
					const FQuat4f Base = FRotationMatrix44f::MakeFromZX(Out3, Up).ToQuat();
					const FVector3f TiltAxis = Base.GetAxisX() * FMath::Cos(TiltDirection) + Base.GetAxisY() * FMath::Sin(TiltDirection);
					Rotation = FQuat4f(TiltAxis, TiltAngle) * FQuat4f(Out3, Turn) * Base;
				}
				Position = Face + Slide * 0.3 - Normal * (Set.Sink * Scale);
			}

			FInstance& Instance = Out.AddDefaulted_GetRef();
			Instance.Slot = Slot;
			Instance.Transform = FTransform(FQuat(Rotation), Position, FVector(Scale));
			Instance.Rock = float(Planet->GetCellRockLayer(Sample.Cell));
			Instance.Snow = Variant->Snow;
			++Taken;
		}
		Stats.RimSides[SetIndex] = Taken;
	}
}

void UHexPlanetObjects::PlaceInCell(int32 Cell, int32 SetIndex, int32 Density, bool bDecor, const TArray<FVector3f>& Triangles, TArray<FInstance>& Out,
	TArray<FHexPlacedObject>* OutPlaced) const
{
	const UHexPlanetObjectSet& Set = *Sets[SetIndex];
	const FHexObjectVariant* Variant = Set.FindVariant(Planet->GetCellBiomeLetter(Cell));
	if (!Variant || Triangles.IsEmpty())
	{
		return;
	}
	const TArray<int32>& Meshes = VariantSlots[SetIndex][int32(Variant - Set.Variants.GetData())];
	if (Meshes.IsEmpty() || Set.CountByDensity.IsEmpty())
	{
		return;
	}

	const FHexSphereGrid& Grid = Planet->GetGrid();
	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const double Radius = Planet->GetRadius();
	const int32 Count = FMath::RoundToInt32(Set.CountByDensity[FMath::Clamp(Density, 1, Set.CountByDensity.Num()) - 1] * Variant->CountFactor
		* (Each.IsPentagon() ? 5.0f / 6.0f : 1.0f));
	if (Count <= 0)
	{
		return;
	}

	FCellGround Ground;
	Ground.Build(FVector3f(Each.Center), Triangles);

	// the cell on the plane of its center, shrunk to the reach of the set around its middle
	const FVector2f Middle = Ground.Flatten(FVector3f(Each.Center * Radius));
	TArray<FVector2f, TInlineAllocator<6>> Polygon;
	FVector2f Low(TNumericLimits<float>::Max());
	FVector2f High(-TNumericLimits<float>::Max());
	for (const int32 Corner : Each.Corners)
	{
		const FVector2f Point = Middle + (Ground.Flatten(FVector3f(Grid.GetCorners()[Corner] * Radius)) - Middle) * Set.Reach;
		Polygon.Add(Point);
		Low = FVector2f::Min(Low, Point);
		High = FVector2f::Max(High, Point);
	}

	// a kind that keeps its meshes inside the cell clips the sides towards a void, a cell at least two levels lower, whose cliff the ground of
	// the void carries, so a mesh there would hang over the abyss, and towards a cleared cell, an exit, whose marker it would cover
	uint8 ClipSides = 0;
	TArray<FVector2f, TInlineAllocator<6>> FlatCorners;
	if (Set.bClipFootprint)
	{
		const int32 Level = Planet->GetCellLevel(Cell);
		for (int32 Side = 0; Side < Each.Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Each.Neighbors[Side];
			if (Planet->GetCellLevel(Neighbor) <= Level - 2 || IsClear(Neighbor))
			{
				ClipSides |= uint8(1u << Side);
			}
		}
		for (const int32 Corner : Each.Corners)
		{
			FlatCorners.Add(Ground.Flatten(FVector3f(Grid.GetCorners()[Corner] * Radius)));
		}
	}

	// the hash of an FName changes between runs, so the name is hashed as text, and a cell keeps its objects where they stood
	FRandomStream Random(int32(HashCombine(GetTypeHash(Cell), FCrc::StrCrc32(*Set.Id.ToString())) + (bDecor ? 7 : 0)));
	const float Rock = float(Planet->GetCellRockLayer(Cell));
	const float MinUp = FMath::Clamp(Set.MinUp, 0.0f, 1.0f);
	const double WallClearance = Planet->HasWalls() ? HexPlanetWalls::GetFootReach(Planet->GetWallParams()) : 0.0;
	const double RiverClearance = Planet->GetRivers() && !Planet->GetRivers()->GetShownNet().IsEmpty()
		? Settings->RiverWidth * 0.5 + Settings->RiverMeander + Settings->RiverBankWidth : 0.0;
	TArray<FVector3f, TInlineAllocator<64>> Taken;
	TArray<float, TInlineAllocator<64>> TakenSizes;
	const int32 Tries = Count * 6 + 4;

	for (int32 Try = 0; Try < Tries && Taken.Num() < Count; ++Try)
	{
		const FVector2f Point(Random.FRandRange(Low.X, High.X), Random.FRandRange(Low.Y, High.Y));
		float Scale = Random.FRandRange(Set.ScaleMin, FMath::Max(Set.ScaleMin, Set.ScaleMax)) * Variant->ScaleFactor;
		if (Random.FRand() < Set.GiantChance)
		{
			Scale *= Set.GiantScale;
		}
		const int32 Slot = Meshes[Random.RandHelper(Meshes.Num())];
		const float Yaw = Random.FRandRange(0.0f, 2.0f * UE_PI);
		const float TiltAngle = FMath::DegreesToRadians(Random.FRandRange(0.0f, Set.Tilt));
		const float TiltDirection = Random.FRandRange(0.0f, 2.0f * UE_PI);

		if (!IsInside(Point, Polygon))
		{
			continue;
		}
		if (ClipSides != 0 && !HexPlanetUnderground::IsFootprintClear(Point, Slots[Slot].FootprintRadius * Scale + Settings->ObjectFootprintMargin, FlatCorners, ClipSides))
		{
			continue;
		}

		FVector3f Position;
		FVector3f Normal;
		if (!Ground.Find(Point, Position, Normal))
		{
			continue;
		}
		const FVector3f Up = Position.GetSafeNormal();
		if (FVector3f::DotProduct(Normal, Up) < MinUp || Position.Length() - Radius < Set.MinHeight)
		{
			continue;
		}
		// not in a river, nor on its banks
		if (RiverClearance > 0.0 && Planet->GetRivers()->GetRiverDistance(Cell, FVector3d(Position)) < RiverClearance + Set.Spacing * Scale * 0.5)
		{
			continue;
		}
		// nor in the wall on a lip or on its foot; a mass that blocks its cell fills it and may lean on the foot, as long as it stays off the line
		const double Clearance = Set.bBlocksMovement ? 0.0 : WallClearance + Slots[Slot].FootprintRadius * Scale + Settings->ObjectFootprintMargin;
		if (WallClearance > 0.0 && Set.bClipFootprint && Clearance > 0.0
			&& HexPlanetWalls::GetDistanceToLine(Planet->GetWalls(), Cell, FVector3d(Position)) < Clearance)
		{
			continue;
		}

		bool bFree = true;
		for (int32 Index = 0; Index < Taken.Num() && bFree; ++Index)
		{
			bFree = FVector3f::DistSquared(Taken[Index], Position) >= FMath::Square(Set.Spacing * (Scale + TakenSizes[Index]) * 0.5f);
		}
		if (!bFree)
		{
			continue;
		}
		Taken.Add(Position);
		TakenSizes.Add(Scale);

		// up the planet, leaning with the ground by SlopeAlign and at random by up to Tilt, then turned around that at random
		FVector3f Axis = FMath::Lerp(Up, Normal, Set.SlopeAlign).GetSafeNormal();
		const FVector3f Side = FVector3f::CrossProduct(Axis, FMath::Abs(Axis.Z) < 0.9f ? FVector3f::UnitZ() : FVector3f::UnitX()).GetSafeNormal();
		const FVector3f Across = FVector3f::CrossProduct(Axis, Side);
		Axis = (Axis * FMath::Cos(TiltAngle) + (Side * FMath::Cos(TiltDirection) + Across * FMath::Sin(TiltDirection)) * FMath::Sin(TiltAngle)).GetSafeNormal();

		const FQuat4f Base = FRotationMatrix44f::MakeFromZX(Axis, Side).ToQuat();
		const FQuat4f Rotation = FQuat4f(Axis, Yaw) * Base;
		FInstance& Instance = Out.AddDefaulted_GetRef();
		Instance.Slot = Slot;
		Instance.Transform = FTransform(FQuat(Rotation), FVector(Position - Axis * Set.Sink * Scale), FVector(Scale));
		Instance.Rock = Rock;
		Instance.Snow = Variant->Snow;

		// and as an object placed by hand would stand there, its scale before the factor of its variant, which it takes again on this biome
		if (OutPlaced)
		{
			const auto Round = [](double Value, double Step) { return float(FMath::RoundToDouble(Value / Step) * Step); };
			FHexPlacedObject& Placed = OutPlaced->AddDefaulted_GetRef();
			Placed.Set = uint8(SetIndex + 1);
			Placed.Mesh = uint8(FMath::Max(AllSlots[SetIndex].IndexOfByKey(Slot), 0));
			Placed.Offset = FVector2f(Round(Point.X / Settings->HexRadius, 0.001), Round(Point.Y / Settings->HexRadius, 0.001));
			Placed.Yaw = Round(FMath::Fmod(double(FMath::RadiansToDegrees(Yaw)) + 360.0, 360.0), 0.1);
			Placed.Scale = Round(Scale / FMath::Max(Variant->ScaleFactor, 0.01f), 0.001);
		}
	}
}

UMaterialInterface* UHexPlanetObjects::GetMaterial(UMaterialInterface* Parent, const UHexPlanetObjectSet* Set)
{
	if (!Parent)
	{
		return nullptr;
	}

	// an instance of the material of what stands on the walls dissolves with them, around the point SetFadeFocus gives
	const auto KeepFading = [this, Parent](UMaterialInstanceDynamic* Material)
	{
		if (Parent == Planet->GetWallObjectMaterial())
		{
			Material->SetVectorParameterValue(TEXT("WallFadeCenter"), FadeCenter);
			FadeMaterials.Add(Material);
		}
	};

	// a set that glows takes its values of the glow (M_HexPlanetCave), so it has an instance of its own; every other set shares one per parent
	if (Set && Set->GlowStrength > 0.0f)
	{
		const int32 Index = GlowMaterialKeys.IndexOfByKey(TPair<const UMaterialInterface*, const UHexPlanetObjectSet*>(Parent, Set));
		if (Index != INDEX_NONE)
		{
			return GlowMaterials[Index];
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
		HexMaterialConfig::Apply(Material, Settings);
		HexMaterialConfig::Apply(Material, Set);
		KeepFading(Material);
		GlowMaterialKeys.Add({Parent, Set});
		GlowMaterials.Add(Material);
		return Material;
	}

	if (const TObjectPtr<UMaterialInterface>* Found = Materials.Find(Parent))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
	HexMaterialConfig::Apply(Material, Settings);
	KeepFading(Material);
	Materials.Add(Parent, Material);
	return Material;
}

void UHexPlanetObjects::ApplyChunk(int32 Chunk, const TArray<FInstance>& Instances)
{
	TMap<int32, TArray<int32>> BySlot;
	for (int32 Index = 0; Index < Instances.Num(); ++Index)
	{
		BySlot.FindOrAdd(Instances[Index].Slot).Add(Index);
	}

	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		const TArray<int32>* Indices = BySlot.Find(Slot);
		TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Found = Components.Find(GetComponentKey(Chunk, Slot));
		if (!Indices && !Found)
		{
			continue;
		}

		UHierarchicalInstancedStaticMeshComponent* Component = Found ? Found->Get() : nullptr;
		if (!Component)
		{
			const UHexPlanetObjectSet& Set = *Sets[Slots[Slot].Set];
			Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Objects_%d_%d"), Chunk, Slot));
			Component->SetupAttachment(this);
			Component->SetStaticMesh(Slots[Slot].Mesh);
			SetSlotMaterials(Component, Slot);
			Component->NumCustomDataFloats = 2;
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			Component->SetCastShadow(Set.bCastShadow);
			// its shadow is kept in the cache of the virtual shadow maps though its leaves sway: redrawn every frame for a sway nobody sees in
			// a shadow, the trees of a dense forest cost most of what they cost at all (hex.PlanetObjectShadowCache 0 for the old way)
			if (CVarHexPlanetObjectShadowCache.GetValueOnGameThread())
			{
				Component->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
			}
			Component->bAffectDistanceFieldLighting = false;
			Component->SetCullDistances(0, int32(Set.CullDistance));
			// hidden with the rest while hex.PlanetObjects hides them, a rim also while hex.PlanetRims does
			Component->SetVisibility(IsSlotShown(Slot));
			Component->RegisterComponent();
			Components.Add(GetComponentKey(Chunk, Slot), Component);
		}

		Component->ClearInstances();
		if (Indices)
		{
			TArray<FTransform> Transforms;
			Transforms.Reserve(Indices->Num());
			for (const int32 Index : *Indices)
			{
				Transforms.Add(Instances[Index].Transform);
			}
			Component->AddInstances(Transforms, false, false, false);
			for (int32 Instance = 0; Instance < Indices->Num(); ++Instance)
			{
				Component->SetCustomDataValue(Instance, 0, Instances[(*Indices)[Instance]].Rock, false);
				Component->SetCustomDataValue(Instance, 1, Instances[(*Indices)[Instance]].Snow, false);
			}

			// the whole component goes past the cull distance of its farthest instance, measured from the middle of its bounds, so the chunks
			// towards the horizon cost no draw calls; a cull distance on the instances alone left every component in view drawing
			const float CullDistance = Sets[Slots[Slot].Set]->CullDistance;
			if (CullDistance > 0.0f)
			{
				Component->UpdateBounds();
				Component->SetCullDistance(CullDistance + Component->Bounds.SphereRadius);
			}
		}
		Component->MarkRenderStateDirty();
	}
}
