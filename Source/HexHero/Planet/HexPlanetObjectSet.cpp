// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetObjectSet.h"
#include "Engine/StaticMesh.h"

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
	const FHexObjectVariant* Fallback = nullptr;
	const TCHAR Letter = FChar::ToUpper(BiomeLetter);
	for (const FHexObjectVariant& Variant : Variants)
	{
		if (Variant.Biomes.IsEmpty())
		{
			Fallback = Fallback ? Fallback : &Variant;
		}
		else
		{
			for (const TCHAR Each : Variant.Biomes)
			{
				if (FChar::ToUpper(Each) == Letter)
				{
					return &Variant;
				}
			}
		}
	}
	return Fallback;
}
