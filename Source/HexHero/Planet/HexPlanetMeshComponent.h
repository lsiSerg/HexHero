// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "HexPlanetMeshComponent.generated.h"

class FHexPlanetMeshRenderData;

/** The render data of one section of UHexPlanetMeshComponent, shared by the component and its scene proxies */
using FHexPlanetMeshSectionRef = TSharedPtr<FHexPlanetMeshRenderData, ESPMode::ThreadSafe>;

/** A section to prepare for UHexPlanetMeshComponent: positions, normals, the four UVs of M_HexPlanet, a fifth that only the walls of the
 *  underground have (the pixel of their sample in AHexPlanet::WallGlowTexture), and three indices a triangle */
struct FHexPlanetMeshSource
{
	static constexpr int32 MaxUVs = 5;

	TConstArrayView<FVector3f> Positions;
	TConstArrayView<FVector3f> Normals;
	TConstArrayView<FVector2f> UVs[MaxUVs];
	TConstArrayView<int32> Triangles;
};

/**
 *  A mesh of sections for the surface of the planet that keeps nothing of them on the CPU. UProceduralMeshComponent held each section three
 *  times, in the component, in its scene proxy and on the GPU, and its vertices in 152 bytes each; here a section goes to the GPU once, as
 *  a position, packed tangents and four UVs of half floats, 36 bytes a vertex (the walls of the underground have a fifth, 40), with no vertex
 *  colors and indices of 16 bits when they fit,
 *  and the CPU frees it there. The render data outlives the scene proxies, so a change of material or visibility does not upload it again,
 *  and its ray tracing geometry is built for tracing and compacted.
 *
 *  It draws as a static mesh, so its mesh draw commands are cached, and gives ray tracing its sections every frame as a dynamic mesh does,
 *  referencing a geometry that was evicted so it comes back. Only the GPU knows where its vertices lie, so whatever needs the ground of a
 *  cell builds it again (HexPlanetSurface::BuildCellTriangles)
 */
UCLASS()
class UHexPlanetMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:

	UHexPlanetMeshComponent(const FObjectInitializer& ObjectInitializer);

	/** Packs a section as the GPU takes it, on any thread; nothing goes to the GPU until SetSection. Returns null for a section without triangles */
	static FHexPlanetMeshSectionRef PrepareSection(const FHexPlanetMeshSource& Source, FName DebugName);

	/** Sends a prepared section to the GPU, keeping whether it is visible, and frees its data on the CPU there; null clears it */
	void SetSection(int32 Section, FHexPlanetMeshSectionRef RenderData);

	/** Clears one section or all of them, which frees them on the GPU once no scene proxy draws them */
	void ClearSection(int32 Section) { SetSection(Section, nullptr); }
	void ClearAllSections();

	/** Shows or hides one section */
	void SetSectionVisible(int32 Section, bool bShow);

	/** Returns true if a section holds triangles */
	bool HasSection(int32 Section) const;

	/** Returns how many sections the component has, empty ones included */
	int32 GetNumSections() const { return Sections.Num(); }

	/** What a section holds on the GPU */
	struct FSectionStats
	{
		int64 Vertices = 0;
		int64 Indices = 0;
		/** Bytes of its vertex and index buffers, without its ray tracing geometry */
		int64 GpuBytes = 0;
	};
	FSectionStats GetSectionStats(int32 Section) const;

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual int32 GetNumMaterials() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UPrimitiveComponent Interface

private:

	struct FSection
	{
		FHexPlanetMeshSectionRef RenderData;
		bool bVisible = true;
	};
	TArray<FSection> Sections;

	friend class FHexPlanetMeshSceneProxy;
};
