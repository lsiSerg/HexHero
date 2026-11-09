// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetObjectSet.h"
#include "Engine/StaticMesh.h"
#include "HexPlanetFile.h"

#if WITH_EDITOR
int32 UHexPlanetObjectSet::SetMeshLods(UStaticMesh* Mesh, const TArray<float>& TriangleShares, const TArray<float>& ScreenSizes)
{
	if (!Mesh || TriangleShares.IsEmpty() || TriangleShares.Num() != ScreenSizes.Num())
	{
		return 0;
	}
	Mesh->Modify();
	Mesh->SetNumSourceModels(TriangleShares.Num());
	for (int32 Lod = 0; Lod < TriangleShares.Num(); ++Lod)
	{
		FStaticMeshSourceModel& Model = Mesh->GetSourceModel(Lod);
		if (Lod > 0)
		{
			Model.BuildSettings = Mesh->GetSourceModel(0).BuildSettings;
		}
		Model.ReductionSettings.PercentTriangles = FMath::Clamp(TriangleShares[Lod], 0.01f, 1.0f);
		Model.ReductionSettings.PercentVertices = 1.0f;
		Model.ScreenSize.Default = ScreenSizes[Lod];
	}
	Mesh->bAutoComputeLODScreenSize = false;
	Mesh->PostEditChange();
	return Mesh->GetNumLODs();
}
#endif

const FHexObjectVariant* UHexPlanetObjectSet::FindVariant(TCHAR BiomeLetter) const
{
	const int32 Index = FindHexObjectVariant(Variants.Num(), BiomeLetter, [this](int32 Each) -> const FString& { return Variants[Each].Biomes; });
	return Variants.IsValidIndex(Index) ? &Variants[Index] : nullptr;
}
