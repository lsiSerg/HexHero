// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetMeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "LocalVertexFactory.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveDrawingUtils.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "RayTracingGeometry.h"
#include "RayTracingInstance.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HexPlanetMeshComponent)

/**
 *  One section on the GPU: a position, tangents and four UVs a vertex, the indices, the vertex factory and the ray tracing geometry. It is
 *  filled on any thread, initialized on the render thread, where the CPU frees what it filled, and released there
 */
class FHexPlanetMeshRenderData
{
public:
	FPositionVertexBuffer Positions;
	FStaticMeshVertexBuffer Attributes;
	FDynamicMeshIndexBuffer16 Indices16;
	FDynamicMeshIndexBuffer32 Indices32;
	FLocalVertexFactory VertexFactory;
#if RHI_RAYTRACING
	FRayTracingGeometry RayTracingGeometry;
#endif
	FName DebugName;
	FBox3f Bounds = FBox3f(ForceInit);
	int32 NumVertices = 0;
	int32 NumIndices = 0;
	bool bWideIndices = false;

	/** True once SetSection sent it to the render thread; before that it holds no resources and may be deleted on any thread */
	bool bSent = false;

	explicit FHexPlanetMeshRenderData(ERHIFeatureLevel::Type FeatureLevel)
		: VertexFactory(FeatureLevel, "FHexPlanetMeshRenderData")
	{
	}

	const FIndexBuffer& GetIndexBuffer() const
	{
		return bWideIndices ? static_cast<const FIndexBuffer&>(Indices32) : static_cast<const FIndexBuffer&>(Indices16);
	}

	int64 GetGpuBytes() const
	{
		// a position of 12 bytes, tangents of 8 and four UVs of half floats, 16
		return int64(NumVertices) * 36 + int64(NumIndices) * (bWideIndices ? 4 : 2);
	}

	void InitResources(FRHICommandListBase& RHICmdList)
	{
		FIndexBuffer& Indices = bWideIndices ? static_cast<FIndexBuffer&>(Indices32) : static_cast<FIndexBuffer&>(Indices16);
		Positions.InitResource(RHICmdList);
		Attributes.InitResource(RHICmdList);
		Indices.InitResource(RHICmdList);

		// as FStaticMeshVertexBuffers binds its buffers, with the plain white color of a mesh without vertex colors, which M_HexPlanet reads
		// only on the tiles of painting
		FLocalVertexFactory::FDataType Data;
		Positions.BindPositionVertexBuffer(&VertexFactory, Data);
		Attributes.BindTangentVertexBuffer(&VertexFactory, Data);
		Attributes.BindPackedTexCoordVertexBuffer(&VertexFactory, Data);
		Attributes.BindLightMapVertexBuffer(&VertexFactory, Data, 0);
		FColorVertexBuffer::BindDefaultColorVertexBuffer(&VertexFactory, Data, FColorVertexBuffer::NullBindStride::ZeroForDefaultBufferBind);
		VertexFactory.SetData(RHICmdList, Data);
		VertexFactory.InitResource(RHICmdList);

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			// built to be traced fast and compacted, as the surface stays as it is until it is built again
			FRayTracingGeometryInitializer Initializer;
			Initializer.DebugName = DebugName;
			Initializer.IndexBuffer = Indices.IndexBufferRHI;
			Initializer.TotalPrimitiveCount = NumIndices / 3;
			Initializer.GeometryType = RTGT_Triangles;
			Initializer.bFastBuild = false;
			Initializer.bAllowUpdate = false;
			Initializer.bAllowCompaction = true;

			FRayTracingGeometrySegment Segment;
			Segment.VertexBuffer = Positions.VertexBufferRHI;
			Segment.VertexBufferElementType = VET_Float3;
			Segment.VertexBufferStride = Positions.GetStride();
			Segment.VertexBufferOffset = 0;
			Segment.MaxVertices = NumVertices;
			Segment.FirstPrimitive = 0;
			Segment.NumPrimitives = NumIndices / 3;
			Initializer.Segments.Add(Segment);

			RayTracingGeometry.SetInitializer(Initializer);
			RayTracingGeometry.InitResource(RHICmdList);
		}
#endif

		// the GPU holds them now; a geometry evicted from ray tracing is built again from the buffers there
		Positions.CleanUp();
		Attributes.CleanUp();
		Indices16.Indices.Empty();
		Indices32.Indices.Empty();
	}

	void ReleaseResources()
	{
		Positions.ReleaseResource();
		Attributes.ReleaseResource();
		Indices16.ReleaseResource();
		Indices32.ReleaseResource();
		VertexFactory.ReleaseResource();
#if RHI_RAYTRACING
		RayTracingGeometry.ReleaseResource();
#endif
	}
};

namespace
{
	/** Deletes render data where it may be: at once if it never reached the render thread, else there, after releasing its resources */
	void DeleteRenderData(FHexPlanetMeshRenderData* Data)
	{
		if (!Data->bSent)
		{
			delete Data;
			return;
		}
		ENQUEUE_RENDER_COMMAND(ReleaseHexPlanetMesh)([Data](FRHICommandListImmediate&)
		{
			Data->ReleaseResources();
			delete Data;
		});
	}
}

/** Draws the sections of a UHexPlanetMeshComponent shown when it was made, sharing their render data with the component */
class FHexPlanetMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:

	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FHexPlanetMeshSceneProxy(UHexPlanetMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetShaderPlatform()))
	{
		for (int32 Index = 0; Index < Component->Sections.Num(); ++Index)
		{
			const UHexPlanetMeshComponent::FSection& Source = Component->Sections[Index];
			if (!Source.RenderData || !Source.bVisible)
			{
				continue;
			}
			FProxySection& Section = Sections.AddDefaulted_GetRef();
			Section.RenderData = Source.RenderData;
			Section.Index = Index;
			Section.Material = Component->GetMaterial(Index);
			if (!Section.Material)
			{
				Section.Material = UMaterial::GetDefaultMaterial(MD_Surface);
				bVerifyUsedMaterials = false;
			}
		}

		// the local vertex factory reads the primitive from GPUScene, as a static mesh does
		EnableGPUSceneSupportFlags();
	}

	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override
	{
		PDI->ReserveMemoryForMeshes(Sections.Num());
		for (const FProxySection& Section : Sections)
		{
			FMeshBatch Mesh;
			if (MakeMeshBatch(Section, Mesh))
			{
				PDI->DrawMesh(Mesh, FLT_MAX);
			}
		}
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		// only the views that static meshes do not serve come here, such as the wireframe of the editor
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		FColoredMaterialRenderProxy* WireframeMaterial = nullptr;
		if (bWireframe)
		{
			WireframeMaterial = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
				FLinearColor(0.0f, 0.5f, 1.0f));
			Collector.RegisterOneFrameMaterialProxy(WireframeMaterial);
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if (!(VisibilityMap & (1 << ViewIndex)))
			{
				continue;
			}
			for (const FProxySection& Section : Sections)
			{
				FMeshBatch& Mesh = Collector.AllocateMesh();
				if (!MakeMeshBatch(Section, Mesh))
				{
					continue;
				}
				if (bWireframe)
				{
					Mesh.bWireframe = true;
					Mesh.MaterialRenderProxy = WireframeMaterial;
				}
				Collector.AddMesh(ViewIndex, Mesh);
			}
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
#endif
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;

		// cached mesh draw commands, unless a view draws what they cannot, such as a wireframe or the bounds
		if (IsRichView(*View->Family) || View->Family->EngineShowFlags.Bounds || IsSelected() || IsHovered())
		{
			Result.bDynamicRelevance = true;
		}
		else
		{
			Result.bStaticRelevance = true;
		}

		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + GetAllocatedSize();
	}

#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const override { return true; }
	virtual bool IsRayTracingStaticRelevant() const override { return false; }
	virtual bool HasRayTracingRepresentation() const override { return true; }

	virtual void GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector) override
	{
		TConstArrayView<const FSceneView*> Views = Collector.GetViews();
		const uint32 VisibilityMap = Collector.GetVisibilityMap();
		const int32 FirstActiveViewIndex = FMath::CountTrailingZeros(VisibilityMap);
		if (!Views.IsValidIndex(FirstActiveViewIndex))
		{
			return;
		}
		const FSceneView* FirstActiveView = Views[FirstActiveViewIndex];

		for (FProxySection& Section : Sections)
		{
			const FRayTracingGeometry& Geometry = Section.RenderData->RayTracingGeometry;
			if (!Section.RenderData->VertexFactory.IsInitialized() || Geometry.GetInitializer().TotalPrimitiveCount == 0)
			{
				continue;
			}
			// the manager of ray tracing geometry evicts what it has no room for; a reference brings it back for the frames after
			if (Geometry.IsEvicted())
			{
				Collector.AddReferencedGeometry(Geometry.GetGeometryHandle());
				continue;
			}
			if (!Geometry.IsValid())
			{
				continue;
			}

			// the mesh batch of its material is made once, as for a static mesh
			FRayTracingInstance Instance;
			if (Section.RayTracingMaterials.IsEmpty())
			{
				FMeshBatch& Mesh = Section.RayTracingMaterials.AddDefaulted_GetRef();
				MakeMeshBatch(Section, Mesh);
				Mesh.SegmentIndex = 0;
				Mesh.CastRayTracedShadow = IsShadowCast(FirstActiveView);
			}
			else
			{
				Instance.bInstanceMaskAndFlagsDirty = false;
			}
			Instance.Geometry = &Geometry;
			Instance.MaterialsView = MakeArrayView(Section.RayTracingMaterials);
			Instance.InstanceTransformsView = MakeArrayView(&GetLocalToWorld(), 1);

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (VisibilityMap & (1 << ViewIndex))
				{
					Collector.AddRayTracingInstance(ViewIndex, Instance);
				}
			}
		}
	}
#endif

private:

	struct FProxySection
	{
		FHexPlanetMeshSectionRef RenderData;
		UMaterialInterface* Material = nullptr;
		int32 Index = 0;
#if RHI_RAYTRACING
		TArray<FMeshBatch, TInlineAllocator<1>> RayTracingMaterials;
#endif
	};

	/** Fills a mesh batch of a section, false before its vertex factory is initialized */
	bool MakeMeshBatch(const FProxySection& Section, FMeshBatch& Mesh) const
	{
		const FHexPlanetMeshRenderData& Data = *Section.RenderData;
		if (!Data.VertexFactory.IsInitialized())
		{
			return false;
		}
		FMeshBatchElement& Element = Mesh.Elements[0];
		Element.IndexBuffer = &Data.GetIndexBuffer();
		Element.FirstIndex = 0;
		Element.NumPrimitives = Data.NumIndices / 3;
		Element.MinVertexIndex = 0;
		Element.MaxVertexIndex = Data.NumVertices - 1;
		Mesh.VertexFactory = &Data.VertexFactory;
		Mesh.MaterialRenderProxy = Section.Material->GetRenderProxy();
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = SDPG_World;
		Mesh.LODIndex = 0;
		Mesh.MeshIdInPrimitive = uint16(Section.Index);
		Mesh.bCanApplyViewModeOverrides = false;
		return true;
	}

	TArray<FProxySection> Sections;
	FMaterialRelevance MaterialRelevance;
};

UHexPlanetMeshComponent::UHexPlanetMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// nothing collides with the surface, which finds the cell under a ray itself (AHexPlanet::FindCellAlongRay)
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	bUseAsOccluder = true;
}

FHexPlanetMeshSectionRef UHexPlanetMeshComponent::PrepareSection(const FHexPlanetMeshSource& Source, FName DebugName)
{
	const int32 NumVertices = Source.Positions.Num();
	if (NumVertices == 0 || Source.Triangles.Num() < 3 || Source.Normals.Num() != NumVertices)
	{
		return nullptr;
	}

	FHexPlanetMeshSectionRef Data(new FHexPlanetMeshRenderData(GMaxRHIFeatureLevel), &DeleteRenderData);
	Data->DebugName = DebugName;
	Data->NumVertices = NumVertices;
	Data->NumIndices = Source.Triangles.Num();

	// without CPU access, so a cooked game frees the data once it is on the GPU; InitResources frees it in any build
	Data->Positions.Init(NumVertices, false);
	Data->Attributes.Init(NumVertices, 4, false);
	for (int32 Vertex = 0; Vertex < NumVertices; ++Vertex)
	{
		const FVector3f& Position = Source.Positions[Vertex];
		Data->Positions.VertexPosition(Vertex) = Position;
		Data->Bounds += Position;

		// the tangents of UProceduralMeshComponent without any: X along the world X and Y across it and the normal, which the surface,
		// whose normal is in world space, does not read
		const FVector3f& Normal = Source.Normals[Vertex];
		const FVector3f TangentX(1.0f, 0.0f, 0.0f);
		Data->Attributes.SetVertexTangents(Vertex, TangentX, FVector3f::CrossProduct(Normal, TangentX), Normal);
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			Data->Attributes.SetVertexUV(Vertex, Channel, Source.UVs[Channel].IsValidIndex(Vertex) ? Source.UVs[Channel][Vertex] : FVector2f::ZeroVector);
		}
	}

	// 16 bits where every vertex fits, which halves the indices, also those the ray tracing geometry builds from
	Data->bWideIndices = NumVertices > int32(MAX_uint16) + 1;
	const EBufferUsageFlags IndexUsage = EBufferUsageFlags::Static | (IsRayTracingAllowed() ? EBufferUsageFlags::ShaderResource : EBufferUsageFlags::None);
	if (Data->bWideIndices)
	{
		Data->Indices32.UsageFlags = IndexUsage;
		Data->Indices32.Indices.SetNumUninitialized(Source.Triangles.Num());
		for (int32 Index = 0; Index < Source.Triangles.Num(); ++Index)
		{
			Data->Indices32.Indices[Index] = uint32(Source.Triangles[Index]);
		}
	}
	else
	{
		Data->Indices16.UsageFlags = IndexUsage;
		Data->Indices16.Indices.SetNumUninitialized(Source.Triangles.Num());
		for (int32 Index = 0; Index < Source.Triangles.Num(); ++Index)
		{
			Data->Indices16.Indices[Index] = uint16(Source.Triangles[Index]);
		}
	}
	return Data;
}

void UHexPlanetMeshComponent::SetSection(int32 Section, FHexPlanetMeshSectionRef RenderData)
{
	check(IsInGameThread());
	if (Section < 0)
	{
		return;
	}
	if (Sections.Num() <= Section)
	{
		if (!RenderData)
		{
			return;
		}
		Sections.SetNum(Section + 1);
	}
	if (!Sections[Section].RenderData && !RenderData)
	{
		return;
	}

	if (RenderData && !RenderData->bSent)
	{
		RenderData->bSent = true;
		ENQUEUE_RENDER_COMMAND(InitHexPlanetMesh)([RenderData](FRHICommandListImmediate& RHICmdList)
		{
			RenderData->InitResources(RHICmdList);
		});
	}
	Sections[Section].RenderData = MoveTemp(RenderData);

	// the scene proxy holds the sections it was made with, so it is made again, which uploads nothing
	UpdateBounds();
	MarkRenderStateDirty();
}

void UHexPlanetMeshComponent::ClearAllSections()
{
	bool bHadAny = false;
	for (FSection& Section : Sections)
	{
		bHadAny |= Section.RenderData.IsValid();
		Section.RenderData.Reset();
	}
	if (bHadAny)
	{
		UpdateBounds();
		MarkRenderStateDirty();
	}
}

void UHexPlanetMeshComponent::SetSectionVisible(int32 Section, bool bShow)
{
	if (Section < 0)
	{
		return;
	}
	if (Sections.Num() <= Section)
	{
		Sections.SetNum(Section + 1);
	}
	if (Sections[Section].bVisible != bShow)
	{
		Sections[Section].bVisible = bShow;
		MarkRenderStateDirty();
	}
}

bool UHexPlanetMeshComponent::HasSection(int32 Section) const
{
	return Sections.IsValidIndex(Section) && Sections[Section].RenderData.IsValid();
}

UHexPlanetMeshComponent::FSectionStats UHexPlanetMeshComponent::GetSectionStats(int32 Section) const
{
	FSectionStats Stats;
	if (HasSection(Section))
	{
		const FHexPlanetMeshRenderData& Data = *Sections[Section].RenderData;
		Stats.Vertices = Data.NumVertices;
		Stats.Indices = Data.NumIndices;
		Stats.GpuBytes = Data.GetGpuBytes();
	}
	return Stats;
}

FPrimitiveSceneProxy* UHexPlanetMeshComponent::CreateSceneProxy()
{
	for (const FSection& Section : Sections)
	{
		if (Section.RenderData && Section.bVisible)
		{
			return new FHexPlanetMeshSceneProxy(this);
		}
	}
	return nullptr;
}

int32 UHexPlanetMeshComponent::GetNumMaterials() const
{
	return FMath::Max(Sections.Num(), OverrideMaterials.Num());
}

FBoxSphereBounds UHexPlanetMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox3f Box(ForceInit);
	for (const FSection& Section : Sections)
	{
		if (Section.RenderData)
		{
			Box += Section.RenderData->Bounds;
		}
	}
	if (!Box.IsValid)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
	}
	return FBoxSphereBounds(FBox(Box)).TransformBy(LocalToWorld);
}
