// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetObjects.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* DensityNames[] = {TEXT("sparse"), TEXT("medium"), TEXT("dense")};

	int64 GetComponentKey(int32 Chunk, int32 Slot)
	{
		return int64(Chunk) * 65536 + Slot;
	}

	/** The triangles of the ground of a cell, flattened onto the plane of its center, in buckets, so a point finds its triangle at once */
	struct FCellGround
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
}

void UHexPlanetObjects::Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings)
{
	Planet = InPlanet;
	Settings = InSettings;

	for (const UHexPlanetObjectSet* Set : GetDefault<UHexMapSettings>()->LoadPlanetObjectSets())
	{
		if (Set->Id.IsNone() || Sets.ContainsByPredicate([Set](const UHexPlanetObjectSet* Each) { return Each->Id == Set->Id; }))
		{
			UE_LOG(LogHexHero, Error, TEXT("Planet: the object set %s has no Id or one another set has, so it is left out"), *Set->GetName());
			continue;
		}
		Sets.Add(const_cast<UHexPlanetObjectSet*>(Set));
	}
	// the palette, the generation and FHexCellObjects count by this order, so it is the same in every run
	Sets.StableSort([](const UHexPlanetObjectSet& A, const UHexPlanetObjectSet& B) { return A.SortOrder < B.SortOrder; });
	if (Sets.Num() > 254)
	{
		Sets.SetNum(254);
	}

	VariantSlots.SetNum(Sets.Num());
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
					Slot = Slots.Add(FSlot{SetIndex, Mesh});
					LoadedMeshes.AddUnique(Mesh);
				}
				Found.Add(Slot);
			}
		}
	}

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
	if (!Planet->IsSaved() || !HexPlanetFile::HasObjects(Planet->GetPlanetFolder()))
	{
		Generate();
		bGenerated = true;
		return;
	}
	if (!HexPlanetFile::LoadObjects(Planet->GetPlanetFolder(), Lines, Error))
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
		const int32 Cover = FindSet(Line.Cover);
		const int32 Decor = FindSet(Line.Decor);
		if (Cover != INDEX_NONE && Sets[Cover]->Layer == EHexObjectLayer::Cover)
		{
			Objects.Cover = uint8(Cover + 1);
			Objects.Density = uint8(FMath::Clamp(Line.Density, 1, 3));
		}
		else if (!Line.Cover.IsNone())
		{
			Unknown.Add(Line.Cover);
		}
		if (Decor != INDEX_NONE && Sets[Decor]->Layer == EHexObjectLayer::Decor)
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
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const FHexPlanetManifest& Manifest = Planet->GetManifest();
	FRandomStream Random(Manifest.Seed * 7919 + 17);
	const FVector3d Offset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
	const double Scale = Settings->ObjectNoiseScale * Settings->GetSizeRatio(Manifest.Frequency);
	int32 Placed = 0;

	for (int32 SetIndex = 0; SetIndex < Sets.Num(); ++SetIndex)
	{
		const UHexPlanetObjectSet& Set = *Sets[SetIndex];
		const bool bDecor = Set.Layer == EHexObjectLayer::Decor;
		const FVector3d SetOffset = Offset + FVector3d(SetIndex * 17.3, SetIndex * -9.1, SetIndex * 5.7);

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
				const FHexCellObjects& Objects = CellObjects[Cell];
				if (Cells[Cell].IsPentagon() || (bDecor ? Objects.Decor != 0 : Objects.Cover != 0) || !Set.AllowedLevels.Contains(Planet->GetCellLevel(Cell))
					|| Set.FindVariant(Planet->GetCellBiomeLetter(Cell)) != &Variant)
				{
					continue;
				}
				Candidates.Add(Cell);
				// a cover lies in patches, a decor anywhere
				Keys[Cell] = bDecor ? Random.FRand() : PatchNoise(Cells[Cell].Center, Scale, SetOffset);
			}

			Candidates.Sort([&Keys](int32 A, int32 B) { return Keys[A] > Keys[B]; });
			const int32 Count = FMath::RoundToInt32(Variant.GenerateShare * Candidates.Num());
			for (int32 Rank = 0; Rank < Count; ++Rank)
			{
				FHexCellObjects& Objects = CellObjects[Candidates[Rank]];
				if (bDecor)
				{
					Objects.Decor = uint8(SetIndex + 1);
				}
				else
				{
					// dense in the middle of a patch, sparse at its edge
					Objects.Cover = uint8(SetIndex + 1);
					Objects.Density = Rank < Count * 0.4 ? 3 : Rank < Count * 0.75 ? 2 : 1;
				}
				++Placed;
			}
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: generated objects on %d cells from %d object sets"), Placed, Sets.Num());
}

bool UHexPlanetObjects::Save(const FString& Folder, FString& OutError) const
{
	TArray<FHexObjectCell> Lines;
	for (int32 Cell = 0; Cell < CellObjects.Num(); ++Cell)
	{
		const FHexCellObjects& Objects = CellObjects[Cell];
		const int32 Level = Planet->GetCellLevel(Cell);
		const bool bCover = Objects.Cover != 0 && IsAllowed(Objects.Cover - 1, Level);
		const bool bDecor = Objects.Decor != 0 && IsAllowed(Objects.Decor - 1, Level);
		if (!bCover && !bDecor)
		{
			continue;
		}
		FHexObjectCell& Line = Lines.AddDefaulted_GetRef();
		Line.Cell = Cell;
		Line.Cover = bCover ? Sets[Objects.Cover - 1]->Id : NAME_None;
		Line.Density = bCover ? Objects.Density : 0;
		Line.Decor = bDecor ? Sets[Objects.Decor - 1]->Id : NAME_None;
	}
	return HexPlanetFile::SaveObjects(Folder, Lines, OutError);
}

bool UHexPlanetObjects::IsAllowed(int32 Set, int32 Level) const
{
	return Sets.IsValidIndex(Set) && Sets[Set]->AllowedLevels.Contains(Level);
}

bool UHexPlanetObjects::GetCellCanopy(int32 Cell, FLinearColor& OutColor, float& OutCover) const
{
	const FHexCellObjects Objects = GetCell(Cell);
	const int32 Set = int32(Objects.Cover) - 1;
	if (Set < 0 || !IsAllowed(Set, Planet->GetCellLevel(Cell)) || Sets[Set]->FarCover <= 0.0f)
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

bool UHexPlanetObjects::BlocksMovement(int32 Cell) const
{
	const FHexCellObjects Objects = GetCell(Cell);
	const int32 Level = Planet->GetCellLevel(Cell);
	const auto Blocks = [&](int32 Set) { return Set > 0 && IsAllowed(Set - 1, Level) && Sets[Set - 1]->bBlocksMovement; };
	return Blocks(Objects.Cover) || Blocks(Objects.Decor);
}

FString UHexPlanetObjects::GetCellText(int32 Cell) const
{
	const FHexCellObjects Objects = GetCell(Cell);
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
	if (Parts.IsEmpty())
	{
		return TEXT("none");
	}
	return FString::Join(Parts, TEXT(", ")) + (BlocksMovement(Cell) ? TEXT(", blocks movement") : TEXT(", passable"));
}

void UHexPlanetObjects::SetCells(const TMap<int32, FHexCellObjects>& Changes)
{
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
	Placed.SetNum(Chunks.Num());
	ParallelFor(Chunks.Num(), [&](int32 Index)
	{
		Placed[Index] = PlaceChunk(Chunks[Index]);
	});
	const double ApplyTime = FPlatformTime::Seconds();

	int32 Total = 0;
	for (int32 Index = 0; Index < Chunks.Num(); ++Index)
	{
		ApplyChunk(Chunks[Index], Placed[Index]);
		Total += Placed[Index].Num();
	}

	if (Chunks.Num() > 1)
	{
		UE_LOG(LogHexHero, Log, TEXT("Planet: placed %d objects in %d chunks in %.1f ms, shown in %.1f ms"), Total, Chunks.Num(),
			(ApplyTime - StartTime) * 1000.0, (FPlatformTime::Seconds() - ApplyTime) * 1000.0);
	}

	// the ground of the cells shows their covers from far away, which changes with the objects, their biomes and their levels
	Planet->UpdateCanopy();
}

TArray<UHexPlanetObjects::FInstance> UHexPlanetObjects::PlaceChunk(int32 Chunk) const
{
	TArray<FInstance> Instances;
	TArray<FVector3f> Triangles;
	for (const int32 Cell : ChunkCells[Chunk])
	{
		const FHexCellObjects& Objects = CellObjects[Cell];
		if (Objects.IsEmpty())
		{
			continue;
		}
		const int32 Level = Planet->GetCellLevel(Cell);
		const bool bCover = Objects.Cover != 0 && IsAllowed(Objects.Cover - 1, Level);
		const bool bDecor = Objects.Decor != 0 && IsAllowed(Objects.Decor - 1, Level);
		if (!bCover && !bDecor)
		{
			continue;
		}

		Planet->GetCellTriangles(Cell, Triangles);
		if (bCover)
		{
			PlaceInCell(Cell, Objects.Cover - 1, Objects.Density, false, Triangles, Instances);
		}
		if (bDecor)
		{
			PlaceInCell(Cell, Objects.Decor - 1, 1, true, Triangles, Instances);
		}
	}
	return Instances;
}

void UHexPlanetObjects::PlaceInCell(int32 Cell, int32 SetIndex, int32 Density, bool bDecor, const TArray<FVector3f>& Triangles, TArray<FInstance>& Out) const
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

	// the hash of an FName changes between runs, so the name is hashed as text, and a cell keeps its objects where they stood
	FRandomStream Random(int32(HashCombine(GetTypeHash(Cell), FCrc::StrCrc32(*Set.Id.ToString())) + (bDecor ? 7 : 0)));
	const float Rock = float(Planet->GetCellRockLayer(Cell));
	const float MinUp = FMath::Clamp(Set.MinUp, 0.0f, 1.0f);
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
	}
}

UMaterialInterface* UHexPlanetObjects::GetMaterial(UMaterialInterface* Parent)
{
	if (!Parent)
	{
		return nullptr;
	}
	if (const TObjectPtr<UMaterialInterface>* Found = Materials.Find(Parent))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
	HexMaterialConfig::Apply(Material, Settings);
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
			if (UMaterialInterface* SetMaterial = Set.Material.LoadSynchronous())
			{
				for (int32 Element = 0; Element < Slots[Slot].Mesh->GetStaticMaterials().Num(); ++Element)
				{
					Component->SetMaterial(Element, GetMaterial(SetMaterial));
				}
			}
			else
			{
				for (int32 Element = 0; Element < Slots[Slot].Mesh->GetStaticMaterials().Num(); ++Element)
				{
					Component->SetMaterial(Element, GetMaterial(Slots[Slot].Mesh->GetMaterial(Element)));
				}
			}
			Component->NumCustomDataFloats = 2;
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			Component->SetCastShadow(Set.bCastShadow);
			Component->bAffectDistanceFieldLighting = false;
			Component->SetCullDistances(0, int32(Set.CullDistance));
			// hidden with the rest while hex.PlanetObjects hides them
			Component->SetVisibility(IsVisible());
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
