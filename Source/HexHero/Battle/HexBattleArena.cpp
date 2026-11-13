// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexBattleArena.h"
#include "HexBattleConfig.h"
#include "HexBattles.h"
#include "HexHero.h"
#include "HexUnitFigure.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HexCastles.h"
#include "HexCastleConfig.h"
#include "HexMaterialConfig.h"
#include "HexSiteConfig.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HexPlanet.h"
#include "HexPlanetLayers.h"
#include "HexWorldMap.h"
#include "ShowFlags.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshAttributes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* SkyMaterialPath = TEXT("/Game/HexPlanet/Battle/M_HexBattleSky.M_HexBattleSky");

	/** A hash of a few numbers, for what stands where, the same every time */
	/** A material of timber for the planks of the bridge and the gate, from the material of the units */
	UMaterialInstanceDynamic* MakeWood(UObject* Outer, const UHexBattleConfig* Config)
	{
		UMaterialInterface* UnitMaterial = Config ? Config->UnitMaterial.LoadSynchronous() : nullptr;
		if (!UnitMaterial)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Wood = UMaterialInstanceDynamic::Create(UnitMaterial, Outer);
		Wood->SetVectorParameterValue(TEXT("Tint"), Config->GateWoodColor);
		Wood->SetScalarParameterValue(TEXT("SideColorShare"), 0.0f);
		return Wood;
	}

	/** A material of a prop of a siege that is no mesh of the castle (M_HexBattleProp): a color and a normal texture with a tint, by the first
	 *  UV of the mesh in world units when bUseUV, else projected from the three planes of the world, its planks turned by bTurn */
	UMaterialInstanceDynamic* MakeProp(UObject* Outer, const UHexBattleConfig* Config, const TSoftObjectPtr<UTexture2D>& Color, const TSoftObjectPtr<UTexture2D>& Normal,
		const FLinearColor& Tint, float TileSize, bool bUseUV, bool bTurn)
	{
		UMaterialInterface* Parent = Config ? Config->PropMaterial.LoadSynchronous() : nullptr;
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Outer);
		if (UTexture2D* Texture = Color.LoadSynchronous())
		{
			Material->SetTextureParameterValue(TEXT("ColorTex"), Texture);
		}
		if (UTexture2D* Texture = Normal.LoadSynchronous())
		{
			Material->SetTextureParameterValue(TEXT("NormalTex"), Texture);
		}
		Material->SetVectorParameterValue(TEXT("Tint"), Tint);
		Material->SetScalarParameterValue(TEXT("TileSize"), TileSize);
		Material->SetScalarParameterValue(TEXT("UseUV"), bUseUV ? 1.0f : 0.0f);
		Material->SetScalarParameterValue(TEXT("Turn"), bTurn ? 1.0f : 0.0f);
		return Material;
	}

	/** The triangles of a mesh the field builds, in quads, each with its UV in world units along two of its sides */
	struct FPropMesh
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<FLinearColor> Colors;
		/** The color of the vertices the next quads take, for a mesh of the castle its tone and in its alpha what it is */
		FLinearColor Color = FLinearColor::White;

		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal, const FVector2D& UvA, const FVector2D& UvB,
			const FVector2D& UvC, const FVector2D& UvD)
		{
			const int32 Base = Vertices.Num();
			Vertices.Append({A, B, C, D});
			UVs.Append({UvA, UvB, UvC, UvD});
			const FVector Tangent = (B - A).GetSafeNormal();
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Normals.Add(Normal);
				Tangents.Add(FProcMeshTangent(Tangent, false));
				Colors.Add(Color);
			}
			// the winding that faces the normal; the material is two sided anyway
			if (FVector::DotProduct(FVector::CrossProduct(C - A, B - A), Normal) >= 0.0)
			{
				Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
			}
			else
			{
				Triangles.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
			}
		}

		/** A box of five faces around a middle, its UV in tiles of Tile units */
		void Box(const FVector& Middle, const FVector& Half, double Tile)
		{
			const auto P = [&](double X, double Y, double Z) { return Middle + FVector(X * Half.X, Y * Half.Y, Z * Half.Z); };
			const auto Uv = [&](const FVector& Point, int32 A, int32 B) { return FVector2D(Point[A] / Tile, Point[B] / Tile); };
			const auto Face = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal, int32 U, int32 V)
			{
				Quad(A, B, C, D, Normal, Uv(A, U, V), Uv(B, U, V), Uv(C, U, V), Uv(D, U, V));
			};
			Face(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), FVector::UpVector, 0, 1);
			Face(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), -FVector::YAxisVector, 0, 2);
			Face(P(-1, 1, -1), P(1, 1, -1), P(1, 1, 1), P(-1, 1, 1), FVector::YAxisVector, 0, 2);
			Face(P(-1, -1, -1), P(-1, 1, -1), P(-1, 1, 1), P(-1, -1, 1), -FVector::XAxisVector, 1, 2);
			Face(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), FVector::XAxisVector, 1, 2);
		}

		/** A static mesh of these triangles, as a mesh of the castle is, so it draws as its instances with their custom values */
		UStaticMesh* Build(UObject* Outer, UMaterialInterface* Material) const
		{
			FMeshDescription Description;
			FStaticMeshAttributes Attributes(Description);
			Attributes.Register();
			TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector3f> InstanceTangents = Attributes.GetVertexInstanceTangents();
			TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
			TVertexInstanceAttributesRef<FVector2f> InstanceUVs = Attributes.GetVertexInstanceUVs();
			TVertexInstanceAttributesRef<FVector4f> InstanceColors = Attributes.GetVertexInstanceColors();
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Castle");
			TArray<FVertexInstanceID> Instances;
			for (int32 Index = 0; Index < Vertices.Num(); ++Index)
			{
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = FVector3f(Vertices[Index]);
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				InstanceNormals[Instance] = FVector3f(Normals[Index]);
				InstanceTangents[Instance] = FVector3f(Tangents[Index].TangentX);
				Signs[Instance] = 1.0f;
				InstanceUVs.Set(Instance, 0, FVector2f(UVs[Index]));
				const FLinearColor Each = Colors.IsValidIndex(Index) ? Colors[Index] : FLinearColor::White;
				InstanceColors[Instance] = FVector4f(Each.R, Each.G, Each.B, Each.A);
				Instances.Add(Instance);
			}
			for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
			{
				const FVertexInstanceID Corners[3] = {Instances[Triangles[Index]], Instances[Triangles[Index + 1]], Instances[Triangles[Index + 2]]};
				Description.CreateTriangle(Group, Corners);
			}
			UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer);
			Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Castle"), TEXT("Castle")));
			UStaticMesh::FBuildMeshDescriptionsParams Params;
			Params.bBuildSimpleCollision = false;
			Params.bFastBuild = true;
			Mesh->BuildFromMeshDescriptions({&Description}, Params);
			return Mesh;
		}

		UProceduralMeshComponent* Make(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, TArray<TObjectPtr<USceneComponent>>& List) const
		{
			UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
			Mesh->SetupAttachment(Parent);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, {}, Tangents, false);
			if (Material)
			{
				Mesh->SetMaterial(0, Material);
			}
			Mesh->RegisterComponent();
			List.Add(Mesh);
			return Mesh;
		}
	};

	/** A mesh of a prop at a transform of the world, kept in a list to be taken away with it */
	void AddBox(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform, TArray<TObjectPtr<USceneComponent>>& List)
	{
		if (!Mesh)
		{
			return;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(Parent);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetWorldTransform(Transform);
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		Part->RegisterComponent();
		List.Add(Part);
	}

	uint32 Mix(int32 A, int32 B, int32 C = 0)
	{
		return HashCombine(HashCombine(GetTypeHash(A), GetTypeHash(B)), GetTypeHash(C));
	}
}

AHexBattleArena::AHexBattleArena()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(Root);
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Grid = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Grid"));
	Grid->SetupAttachment(Root);
	Grid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Grid->SetCastShadow(false);
	Marks = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Marks"));
	Marks->SetupAttachment(Root);
	Marks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marks->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Sky = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sky"));
	Sky->SetupAttachment(Root);
	Sky->SetStaticMesh(SphereMesh.Object);
	Sky->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sky->SetCastShadow(false);
	Sky->SetRelativeScale3D(FVector(300.0));
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = SLS_CapturedScene;
	SkyLight->bLowerHemisphereIsBlack = false;
	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(Root);
	FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetCastShadows(false);
	FillLight->SetIntensity(0.0f);
	FillLight->SetVisibility(false);
	// the sun of the level, not the fill, shades the translucent water and the fog
	FillLight->ForwardShadingPriority = 0;
	Shot = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shot"));
	Shot->SetupAttachment(Root);
	Shot->SetStaticMesh(SphereMesh.Object);
	Shot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shot->SetCastShadow(false);
	SpellLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("SpellLight"));
	SpellLight->SetupAttachment(Root);
	SpellLight->SetAttenuationRadius(400.0f);
	SpellLight->SetCastShadows(false);
	SpellLight->SetIntensity(0.0f);
	SetActorHiddenInGame(true);
}

FHexBattle* AHexBattleArena::GetBattle() const
{
	return Battles.IsValid() ? &Battles->GetBattle() : nullptr;
}

const FHexBattleGround* AHexBattleArena::GetGround() const
{
	return Config && Battles.IsValid() ? Config->FindGround(Battles->GetGroundLetter(), Battles->IsUnderground()) : nullptr;
}

FVector AHexBattleArena::GetHexPoint(const FHexCoord& Hex) const
{
	const FVector2D Flat = FHexLayout(Config ? Config->HexRadius : 48.0f).HexToWorld(Hex) - FieldCenter;
	return GetActorLocation() + FVector(Flat.X, Flat.Y, 0.0);
}

FVector AHexBattleArena::GetWallLinePoint(const FHexCoord& Hex) const
{
	const int32 Column = Hex.GetColumn();
	FVector Point = GetHexPoint(Hex);
	Point.Y = (GetHexPoint(FHexCoord::FromOffset(Column, 0)).Y + GetHexPoint(FHexCoord::FromOffset(Column, 1)).Y) * 0.5;
	return Point;
}

FVector AHexBattleArena::GetWallPartPoint(int32 Wall) const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle || !Battle->GetWalls().IsValidIndex(Wall))
	{
		return GetActorLocation();
	}
	const TArray<FHexBattleWall>& Parts = Battle->GetWalls();
	if (Parts[Wall].Part == EHexWallPart::Tower && (Wall == 0 || Wall == Parts.Num() - 1))
	{
		const FSiegeShape Shape = GetSiegeShape();
		return Wall == 0 ? Shape.FrontStart : Shape.FrontEnd;
	}
	return GetWallLinePoint(Parts[Wall].Hex);
}

AHexBattleArena::FSiegeShape AHexBattleArena::GetSiegeShape() const
{
	FSiegeShape Shape;
	const FHexBattle* Battle = GetBattle();
	const UHexCastleConfig* CastleConfig = UHexCastleConfig::Load();
	if (!Battle || !Config || !Battles.IsValid() || Battle->GetWalls().IsEmpty())
	{
		return Shape;
	}
	const TArray<FHexBattleWall>& Parts = Battle->GetWalls();
	const FVector LineFirst = GetWallLinePoint(Parts[0].Hex);
	const FVector LineLast = GetWallLinePoint(Parts.Last().Hex);
	const FVector RowStep = Parts.Num() > 1 ? (LineLast - LineFirst) / double(Parts.Num() - 1) : FVector(-Config->HexRadius * 1.5, 0.0, 0.0);
	Shape.bValid = true;
	Shape.FrontStart = LineFirst - RowStep * Config->WallEndReach;
	Shape.FrontEnd = LineLast + RowStep * Config->WallEndReach;
	// into the castle, towards the higher columns, its palace past the middles of the last of them and its side walls on past that
	Shape.In = (GetHexPoint(FHexCoord::FromOffset(1, 0)) - GetHexPoint(FHexCoord::FromOffset(0, 0))).GetSafeNormal2D();
	const double HexWidth = Config->HexRadius * UE_DOUBLE_SQRT_3;
	const int32 Last = Battle->GetColumns() - 1;
	const double Back = FMath::Max(FVector::DotProduct(GetHexPoint(FHexCoord::FromOffset(Last, 0)), Shape.In), FVector::DotProduct(GetHexPoint(FHexCoord::FromOffset(Last, 1)), Shape.In))
		+ HexWidth * Config->BackWallReach;
	Shape.BackStart = Shape.FrontStart + Shape.In * (Back - FVector::DotProduct(Shape.FrontStart, Shape.In));
	Shape.BackEnd = Shape.FrontEnd + Shape.In * (Back - FVector::DotProduct(Shape.FrontEnd, Shape.In));
	Shape.SideStart = Shape.BackStart + Shape.In * (HexWidth * Config->SideWallBeyond);
	Shape.SideEnd = Shape.BackEnd + Shape.In * (HexWidth * Config->SideWallBeyond);
	Shape.GateHalf = Config->HexRadius * Config->GateOpening * 0.5;

	// the face of the front wall towards the attacker, which the water of the moat runs under, by the thickness of the mesh of the wall
	const int32 Fort = Battles->GetFortLevel();
	UStaticMesh* WallMesh = CastleConfig && CastleConfig->WallMeshes.IsValidIndex(Fort) ? CastleConfig->WallMeshes[Fort].LoadSynchronous() : nullptr;
	const double Scale = WallMesh ? Config->SiegeWallHeight / FMath::Max(WallMesh->GetBounds().BoxExtent.Z * 2.0, 1.0) : 1.0;
	const double HalfThick = WallMesh ? WallMesh->GetBounds().BoxExtent.X * Scale : Config->HexRadius * 0.15;
	Shape.WallFace = FVector::DotProduct(Shape.FrontStart, Shape.In) - HalfThick;

	// the moat along the far sides of its hexes, the column before the wall, whose middles shift half a hex from row to row; its shore the
	// material draws from these, the far flat edge of an even and an odd row
	Shape.bMoat = !Battle->GetMoat().IsEmpty();
	const int32 MoatColumn = FMath::Max(Parts[0].Hex.GetColumn() - 1, 0);
	const FVector Even = GetHexPoint(FHexCoord::FromOffset(MoatColumn, 0));
	const FVector Odd = GetHexPoint(FHexCoord::FromOffset(MoatColumn, 1));
	Shape.MoatRows = FVector4(Even.X, Odd.X - Even.X, FVector::DotProduct(Even, Shape.In) - HexWidth * 0.5, FVector::DotProduct(Odd, Shape.In) - HexWidth * 0.5);
	const double EndReach = RowStep.Size2D() * Config->MoatEndReach;
	Shape.MoatXMin = FMath::Min(Shape.FrontStart.X, Shape.FrontEnd.X) - EndReach;
	Shape.MoatXMax = FMath::Max(Shape.FrontStart.X, Shape.FrontEnd.X) + EndReach;
	return Shape;
}

double AHexBattleArena::GetGroundHeight(const FVector2D& World) const
{
	if (!Config || Config->HillHeight <= 0.0f)
	{
		return 0.0;
	}
	// flat over the field and the castle with its moat, as everything on them stands at the height of the field, hills beyond
	FBox2D Flat(FieldMin, FieldMax);
	const FSiegeShape Shape = GetSiegeShape();
	if (Shape.bValid)
	{
		for (const FVector& Corner : {Shape.FrontStart, Shape.FrontEnd, Shape.SideStart, Shape.SideEnd})
		{
			Flat += FVector2D(Corner.X, Corner.Y);
		}
	}
	Flat = Flat.ExpandBy(Config->HexRadius * Config->HillFlatMargin);
	const FVector2D Nearest(FMath::Clamp(World.X, Flat.Min.X, Flat.Max.X), FMath::Clamp(World.Y, Flat.Min.Y, Flat.Max.Y));
	const double Away = FVector2D::Distance(World, Nearest);
	const double Ramp = FMath::SmoothStep(0.0, double(Config->HexRadius * Config->HillRamp), Away);
	if (Ramp <= 0.0)
	{
		return 0.0;
	}
	const FVector2D Local = (World - FVector2D(GetActorLocation().X, GetActorLocation().Y)) / FMath::Max(double(Config->HillScale), 1.0) + FVector2D(GroundSeed * 0.37, GroundSeed * 0.91);
	const double Noise = 0.5 + 0.35 * FMath::PerlinNoise2D(Local) + 0.15 * FMath::PerlinNoise2D(Local * 2.3 + FVector2D(5.2, 1.3));
	return double(Config->HillHeight) * Ramp * FMath::Clamp(Noise, 0.0, 1.0);
}

void AHexBattleArena::Show(UHexBattles* InBattles, const FPostProcessSettings& PostProcess)
{
	Battles = InBattles;
	Config = InBattles ? InBattles->GetConfig() : nullptr;
	FHexBattle* Battle = GetBattle();
	if (!Config || !Battle)
	{
		return;
	}
	ClearField();
	SetActorLocation(Config->ArenaOrigin);
	// the middle of the field, so it stands around the origin of the arena
	const FHexLayout Layout(Config->HexRadius);
	FVector2D Min(TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max());
	for (int32 Column = 0; Column < Battle->GetColumns(); ++Column)
	{
		for (int32 Row = 0; Row < Battle->GetRows(); ++Row)
		{
			const FVector2D Point = Layout.HexToWorld(FHexCoord::FromOffset(Column, Row));
			Min = FVector2D::Min(Min, Point);
			Max = FVector2D::Max(Max, Point);
		}
	}
	FieldCenter = (Min + Max) * 0.5;
	FieldMin = FVector2D(GetActorLocation().X, GetActorLocation().Y) + (Min - FieldCenter) - FVector2D(Config->HexRadius);
	FieldMax = FVector2D(GetActorLocation().X, GetActorLocation().Y) + (Max - FieldCenter) + FVector2D(Config->HexRadius);
	GroundSeed = int32(Mix(int32(Battles->GetGroundLetter()), Battle->GetColumns(), Battle->GetRows()) % 1000);

	// the camera looks at it from the side of the south, the attacker on the left
	LookAt = LookAtGoal = FVector2D::ZeroVector;
	Yaw = YawGoal = 0.0f;
	Distance = DistanceGoal = Config->CameraDistance;
	PitchOverride = 0.0f;
	PlaceCamera();
	Camera->SetFieldOfView(Config->CameraFieldOfView);
	Camera->PostProcessSettings = PostProcess;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = PostProcess.AutoExposureBias + Config->ExposureBias;
	Camera->PostProcessBlendWeight = 1.0f;

	const FHexBattleGround* Each = GetGround();
	// the sun of the level lights the field while it is shown
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		Sun = *It;
		SunRotation = It->GetActorRotation();
		if (ULightComponent* Light = It->GetLightComponent())
		{
			SunIntensity = Light->Intensity;
			if (UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(Light))
			{
				SunPriority = Directional->ForwardShadingPriority;
				Directional->ForwardShadingPriority = 1;
				Directional->MarkRenderStateDirty();
			}
			SunColor = Light->GetLightColor();
			Light->SetIntensity(SunIntensity * (Each ? Each->SunShare : 1.0f));
			Light->SetLightColor(Each ? Each->SunColor : FLinearColor::White);
		}
		It->SetActorRotation(FRotator(Config->SunPitch, Config->SunYaw, 0.0));
		// the fill from the other side
		FillLight->SetWorldRotation(FRotator(Config->FillPitch, Config->SunYaw + 180.0f, 0.0f));
		FillLight->SetIntensity(SunIntensity * (Each ? Each->SunShare : 1.0f) * Config->FillShare);
		FillLight->SetLightColor(Config->FillColor);
		FillLight->SetVisibility(Config->FillShare > 0.0f);
		break;
	}
	if (UMaterialInterface* SkyMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(SkyMaterial, this);
		// of the color of the fog of war around the field, so it runs on into it
		const FLinearColor Back = Config->FogColor * (Battles->IsUnderground() ? Config->FogUndergroundShade : 1.0f);
		Instance->SetVectorParameterValue(TEXT("Color"), Back);
		Instance->SetVectorParameterValue(TEXT("TopColor"), Back * 1.3f);
		Sky->SetMaterial(0, Instance);
		UMaterialInstanceDynamic* ShotInstance = UMaterialInstanceDynamic::Create(SkyMaterial, this);
		ShotInstance->SetVectorParameterValue(TEXT("Color"), FLinearColor(4.0f, 3.2f, 1.6f, 1.0f));
		ShotInstance->SetVectorParameterValue(TEXT("TopColor"), FLinearColor(4.0f, 3.2f, 1.6f, 1.0f));
		Shot->SetMaterial(0, ShotInstance);
	}
	Shot->SetVisibility(false);
	Shot->SetWorldScale3D(FVector(Config->ShotSize / 100.0f));
	SkyLight->SetIntensity(Each ? Each->SkyIntensity : 1.0f);
	SkyLight->SetLightColor(Each ? Each->SkyColor : FLinearColor::White);

	BuildGround();
	BuildGrid();
	BuildProps();
	BuildSiege();
	PlaceFigures();
	Log.Reset();
	Popups.Reset();
	Pending.Reset();
	Current.Reset();
	Aimed = INDEX_NONE;
	MarksKey.Empty();
	ReachUnit = INDEX_NONE;
	AiWait = Config->TurnPause * 2.0f;
	AddLog(FString::Printf(TEXT("%s attacks %s"), *Battles->GetSideName(0), *Battles->GetSideName(1)));
	bShowing = true;
	SetActorHiddenInGame(false);
	SkyLight->SetVisibility(true);
	SkyLight->RecaptureSky();
}

void AHexBattleArena::SetCamera(float InDistance, float PitchDegrees)
{
	Distance = DistanceGoal = InDistance;
	PitchOverride = PitchDegrees;
	PlaceCamera();
}

void AHexBattleArena::PlaceCamera()
{
	if (!Config)
	{
		return;
	}
	// lower towards CameraClosePitch as it comes nearer than where it starts
	float PitchDegrees = PitchOverride;
	if (PitchDegrees <= 0.0f)
	{
		const float Near = FMath::Clamp((Distance - Config->CameraMinDistance) / FMath::Max(Config->CameraDistance - Config->CameraMinDistance, 1.0f), 0.0f, 1.0f);
		PitchDegrees = FMath::Lerp(Config->CameraClosePitch, Config->CameraPitch, Near);
	}
	const double Pitch = FMath::DegreesToRadians(double(PitchDegrees));
	const double Turn = FMath::DegreesToRadians(double(Yaw));
	const FVector Forward(FMath::Cos(Turn), FMath::Sin(Turn), 0.0);
	// it looks at a point CameraLookShift nearer to it than LookAt, so the field stands above the panel at the bottom of the screen
	const FVector Look = FVector(LookAt.X, LookAt.Y, 0.0) - Forward * Config->CameraLookShift;
	Camera->SetRelativeLocation(Look - Forward * Distance * FMath::Cos(Pitch) + FVector(0.0, 0.0, Distance * FMath::Sin(Pitch)));
	Camera->SetRelativeRotation(FRotator(-PitchDegrees, Yaw, 0.0));
}

void AHexBattleArena::MoveCamera(const FVector2D& Pan, float Turn, float DeltaSeconds)
{
	if (!Config || !bShowing)
	{
		return;
	}
	YawGoal += Turn;
	if (!Pan.IsNearlyZero())
	{
		const double Angle = FMath::DegreesToRadians(double(YawGoal));
		const FVector2D Forward(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2D Right(-Forward.Y, Forward.X);
		LookAtGoal += (Right * Pan.X + Forward * Pan.Y) * (Config->CameraScrollSpeed * DistanceGoal * DeltaSeconds);
	}
	// at most CameraScrollReach hexes from the middle of the field
	LookAtGoal = LookAtGoal.GetClampedToMaxSize(Config->CameraScrollReach * Config->HexRadius * 1.73);
}

void AHexBattleArena::FrameCamera(const FVector& A, const FVector& B)
{
	APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	int32 Width = 0;
	int32 Height = 0;
	if (!Player || !Config)
	{
		return;
	}
	Player->GetViewportSize(Width, Height);
	if (Width <= 0 || Height <= 0)
	{
		return;
	}
	const double Margin = double(Config->CameraFrameMargin) * Width;
	const auto IsSeen = [&](const FVector& Point)
	{
		FVector2D Screen;
		return Player->ProjectWorldLocationToScreen(Point, Screen) && Screen.X > Margin && Screen.X < Width - Margin && Screen.Y > Margin && Screen.Y < Height - Margin;
	};
	if (IsSeen(A) && IsSeen(B))
	{
		return;
	}
	const FVector Middle = (A + B) * 0.5 - GetActorLocation();
	LookAtGoal = FVector2D(Middle.X, Middle.Y).GetClampedToMaxSize(Config->CameraScrollReach * Config->HexRadius * 1.73);
	// far enough that the two take CameraFrameRoom of the width of the view
	const double HalfAngle = FMath::DegreesToRadians(double(Config->CameraFieldOfView) * 0.5);
	const double Needed = FVector::Dist2D(A, B) / FMath::Max(2.0 * FMath::Tan(HalfAngle) * double(Config->CameraFrameRoom), 0.01);
	DistanceGoal = float(FMath::Clamp(FMath::Max(double(DistanceGoal), Needed), double(Config->CameraMinDistance), double(Config->CameraMaxDistance)));
}

void AHexBattleArena::ZoomCamera(float Notches)
{
	if (Config && bShowing)
	{
		PitchOverride = 0.0f;
		DistanceGoal = FMath::Clamp(DistanceGoal * FMath::Pow(1.0f - Config->CameraZoomStep, Notches), Config->CameraMinDistance, Config->CameraMaxDistance);
	}
}

bool AHexBattleArena::GetGroundPoint(const FVector& Origin, const FVector& Direction, FVector& OutPoint) const
{
	if (FMath::IsNearlyZero(Direction.Z))
	{
		return false;
	}
	const double Along = (GetActorLocation().Z - Origin.Z) / Direction.Z;
	if (Along <= 0.0)
	{
		return false;
	}
	OutPoint = Origin + Direction * Along;
	return true;
}

void AHexBattleArena::DragCamera(const FVector& Grabbed, const FVector& Under)
{
	if (!Config || !bShowing)
	{
		return;
	}
	// the ground under the cursor goes back to the point grabbed; at once, as the drag follows the mouse
	const FVector2D Shift(Grabbed.X - Under.X, Grabbed.Y - Under.Y);
	LookAtGoal = (LookAtGoal + Shift).GetClampedToMaxSize(Config->CameraScrollReach * Config->HexRadius * 1.73);
	LookAt = LookAtGoal;
	PlaceCamera();
}

void AHexBattleArena::Hide()
{
	if (!bShowing)
	{
		return;
	}
	bShowing = false;
	if (ADirectionalLight* Light = Sun.Get())
	{
		Light->SetActorRotation(SunRotation);
		if (ULightComponent* Component = Light->GetLightComponent())
		{
			Component->SetIntensity(SunIntensity);
			Component->SetLightColor(SunColor);
			if (UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(Component))
			{
				Directional->ForwardShadingPriority = SunPriority;
				Directional->MarkRenderStateDirty();
			}
		}
	}
	UpdateDebugView(true);
	ClearField();
	SkyLight->SetVisibility(false);
	FillLight->SetVisibility(false);
	SetActorHiddenInGame(true);
}

void AHexBattleArena::ClearField()
{
	for (const TObjectPtr<USceneComponent>& Prop : Props)
	{
		if (Prop)
		{
			Prop->DestroyComponent();
		}
	}
	Props.Reset();
	for (const TObjectPtr<USceneComponent>& Prop : SiegeProps)
	{
		if (Prop)
		{
			Prop->DestroyComponent();
		}
	}
	SiegeProps.Reset();
	SiegeKey.Empty();
	ArchMeshes.Reset();
	for (const TObjectPtr<USceneComponent>& Part : GateParts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	GateParts.Reset();
	GateHinges[0] = GateHinges[1] = nullptr;
	if (HoverOutline)
	{
		HoverOutline->DestroyComponent();
		HoverOutline = nullptr;
	}
	OutlinedUnit = INDEX_NONE;
	for (const FFigure& Figure : Figures)
	{
		if (Figure.Component.IsValid())
		{
			Figure.Component->DestroyComponent();
		}
	}
	Figures.Reset();
	ShownCounts.Reset();
	Ground->ClearAllMeshSections();
	Grid->ClearAllMeshSections();
	Marks->ClearAllMeshSections();
}

void AHexBattleArena::BuildGround()
{
	FHexBattle* Battle = GetBattle();
	const FHexLayout Layout(Config->HexRadius);
	// out under the fog, which hides where it ends
	const FBox2D Covered = GetFieldBox().ExpandBy(double(Config->FogEnd) + Config->HexRadius * 2.0);
	const double HalfX = FMath::Max(FMath::Abs(Covered.Min.X - GetActorLocation().X), FMath::Abs(Covered.Max.X - GetActorLocation().X));
	const double HalfY = FMath::Max(FMath::Abs(Covered.Min.Y - GetActorLocation().Y), FMath::Abs(Covered.Max.Y - GetActorLocation().Y));
	// fine enough for the hills around the field, which the field and the castle leave flat (GetGroundHeight)
	const int32 Steps = Config->HillHeight > 0.0f ? 120 : 24;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	const FVector2D Origin(GetActorLocation().X, GetActorLocation().Y);
	const double Probe = FMath::Max(HalfX, HalfY) * 2.0 / Steps;
	for (int32 I = 0; I <= Steps; ++I)
	{
		for (int32 J = 0; J <= Steps; ++J)
		{
			const FVector2D Flat(FMath::Lerp(-HalfX, HalfX, I / double(Steps)), FMath::Lerp(-HalfY, HalfY, J / double(Steps)));
			const auto Height = [&](const FVector2D& Offset) { return GetGroundHeight(Origin + Flat + Offset); };
			Vertices.Add(FVector(Flat.X, Flat.Y, Height(FVector2D::ZeroVector)));
			const FVector AlongX(Probe * 2.0, 0.0, Height(FVector2D(Probe, 0.0)) - Height(FVector2D(-Probe, 0.0)));
			const FVector AlongY(0.0, Probe * 2.0, Height(FVector2D(0.0, Probe)) - Height(FVector2D(0.0, -Probe)));
			Normals.Add(FVector::CrossProduct(AlongX, AlongY).GetSafeNormal());
			Tangents.Add(FProcMeshTangent(AlongX.GetSafeNormal(), false));
			UVs.Add(Flat / 300.0);
		}
	}
	for (int32 I = 0; I < Steps; ++I)
	{
		for (int32 J = 0; J < Steps; ++J)
		{
			const int32 A = I * (Steps + 1) + J;
			const int32 B = A + 1;
			const int32 C = A + Steps + 1;
			const int32 D = C + 1;
			Triangles.Append({A, B, C, B, D, C});
		}
	}
	Ground->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, {}, Tangents, false);
	if (UMaterialInterface* Material = Config->GroundMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		const FHexBattleGround* Each = GetGround();
		const int32 Layer = Each && Each->TerrainLayer >= 0 ? Each->TerrainLayer : Battles->GetTerrainLayer();
		Instance->SetScalarParameterValue(TEXT("TerrainLayer"), float(Layer));
		Instance->SetScalarParameterValue(TEXT("Underground"), Battles->IsUnderground() ? 1.0f : 0.0f);
		Instance->SetVectorParameterValue(TEXT("FieldHalf"), FLinearColor(float(Battle->GetRows() * Config->HexRadius * 0.75 + Config->HexRadius), float(Battle->GetColumns() * Config->HexRadius * 0.87 + Config->HexRadius), 0.0f, 0.0f));
		Instance->SetVectorParameterValue(TEXT("FieldCenter"), FLinearColor(GetActorLocation()));
		const FSiegeShape Shape = GetSiegeShape();
		SetMoatParameters(Instance, Shape);
		HexMaterialConfig::Apply(Instance, Config);
		Instance->SetScalarParameterValue(TEXT("RockLayer"), float(Each && Each->TerrainLayer >= 0 ? Layer : Battles->GetRockLayer()));
		Ground->SetMaterial(0, Instance);
	}
	(void)Layout;
}

void AHexBattleArena::BuildGrid()
{
	FHexBattle* Battle = GetBattle();
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;
	const double Width = FMath::Max(Config->HexRadius * 0.035, 1.0);
	const double Inset = Config->HexRadius * 0.97;
	for (int32 Column = 0; Column < Battle->GetColumns(); ++Column)
	{
		for (int32 Row = 0; Row < Battle->GetRows(); ++Row)
		{
			const FVector Middle = GetHexPoint(FHexCoord::FromOffset(Column, Row)) - GetActorLocation() + FVector(0.0, 0.0, 0.6);
			for (int32 Corner = 0; Corner < 6; ++Corner)
			{
				const FVector2D A2 = FHexLayout::GetCornerDirection(Corner);
				const FVector2D B2 = FHexLayout::GetCornerDirection(Corner + 1);
				const FVector A = Middle + FVector(A2.X, A2.Y, 0.0) * Inset;
				const FVector B = Middle + FVector(B2.X, B2.Y, 0.0) * Inset;
				const FVector In = (Middle - (A + B) * 0.5).GetSafeNormal2D() * Width;
				const int32 Base = Vertices.Num();
				Vertices.Append({A, B, B + In, A + In});
				for (int32 Index = 0; Index < 4; ++Index)
				{
					Normals.Add(FVector::UpVector);
					Colors.Add(Config->GridColor);
				}
				Triangles.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
			}
		}
	}
	Grid->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, {}, Colors, {}, false);
	if (UMaterialInterface* Material = Config->MarkMaterial.LoadSynchronous())
	{
		Grid->SetMaterial(0, Material);
		Marks->SetMaterial(0, Material);
	}
}

FBox2D AHexBattleArena::GetFieldBox() const
{
	// the field, and the castle with its moat and its palace
	FBox2D Box(FieldMin, FieldMax);
	const FSiegeShape Shape = GetSiegeShape();
	if (Shape.bValid && Config)
	{
		const FVector2D In(Shape.In.X, Shape.In.Y);
		for (const FVector& Corner : {Shape.FrontStart, Shape.FrontEnd})
		{
			Box += FVector2D(Corner.X, Corner.Y) - In * Config->HexRadius * 2.0;
		}
		for (const FVector& Corner : {Shape.SideStart, Shape.SideEnd})
		{
			Box += FVector2D(Corner.X, Corner.Y) + In * Config->HexRadius * 1.0;
		}
	}
	return Box;
}

void AHexBattleArena::BuildProps()
{
	FHexBattle* Battle = GetBattle();
	const FHexBattleGround* Each = GetGround();
	const uint32 Seed = Mix(Battle->GetRound(), Battles->GetGroundLetter(), int32(Battle->GetObstacles().Num()));
	// a component of instances for each mesh, each instance with the rock layer of the biome as its first custom value, as the objects of the
	// planet have, so the rocks are of the rock of the biome (a static mesh of them drew the default layer, grass, until 2026-11-13)
	const FHexBattleGround* Fought = Each;
	const float RockLayer = float(Fought && Fought->TerrainLayer >= 0 ? Fought->TerrainLayer : Battles->GetRockLayer());
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> Instances;
	const auto MakeProp = [&](UStaticMesh* Mesh, const FVector& At, double Size, int32 Hash, double Sink = 0.0)
	{
		if (!Mesh)
		{
			return;
		}
		UInstancedStaticMeshComponent*& Component = Instances.FindOrAdd(Mesh);
		if (!Component)
		{
			Component = NewObject<UInstancedStaticMeshComponent>(this);
			Component->SetStaticMesh(Mesh);
			Component->SetupAttachment(Root);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetNumCustomDataFloats(2);
			Component->RegisterComponent();
			Props.Add(Component);
		}
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const double Extent = FMath::Max3(Bounds.BoxExtent.X, Bounds.BoxExtent.Y, 1.0);
		const double Scale = Size / Extent;
		const FVector Place = At - FVector(0.0, 0.0, (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale + Sink * Size) - GetActorLocation();
		const int32 Index = Component->AddInstance(FTransform(FRotator(0.0, double(Hash % 360), 0.0), Place, FVector(Scale)), false);
		const float Data[2] = {RockLayer, 0.0f};
		Component->SetCustomData(Index, MakeArrayView(Data), true);
	};
	UStaticMesh* Fallback = Config->FallbackMesh.LoadSynchronous();
	for (const FHexCoord& Hex : Battle->GetObstacles())
	{
		const uint32 Hash = Mix(Hex.Q, Hex.R, int32(Seed));
		UStaticMesh* Mesh = Each && !Each->Obstacles.IsEmpty() ? Each->Obstacles[Hash % Each->Obstacles.Num()].LoadSynchronous() : Fallback;
		MakeProp(Mesh, GetHexPoint(Hex), Config->HexRadius * 0.8, int32(Hash));
	}

	// the field is small and closed, as the developer asked on 2026-11-13: a dense forest around it and the castle, from ForestStart hexes out,
	// rocks on BorderReach hexes all around, and beyond them the fog of war on to the horizon
	const FBox2D Box = GetFieldBox();
	const double R = Config->HexRadius;
	const auto Away = [&Box](const FVector2D& Point)
	{
		const FVector2D Nearest(FMath::Clamp(Point.X, Box.Min.X, Box.Max.X), FMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y));
		return FVector2D::Distance(Point, Nearest);
	};
	FRandomStream Random(int32(Seed & 0x7fffffff));
	const FBox2D Outer = Box.ExpandBy(R * (Config->BorderReach + Config->ForestBeyond + 0.5));
	const auto Spot = [&]() { return FVector2D(Random.FRandRange(Outer.Min.X, Outer.Max.X), Random.FRandRange(Outer.Min.Y, Outer.Max.Y)); };
	const FVector2D NoiseOrigin(GroundSeed * 1.37, GroundSeed * 0.61);
	// a desert has rocks and stones in place of a wood
	TArray<TSoftObjectPtr<UStaticMesh>> Wood = Each ? Each->Decor : TArray<TSoftObjectPtr<UStaticMesh>>();
	const TCHAR Letter = Battles->GetGroundLetter();
	const bool bRocky = Letter != 0 && Config->RockyGroundLetters.Contains(FString::Chr(Letter));
	if (bRocky && !Config->RockyDecor.IsEmpty())
	{
		Wood = Config->RockyDecor;
	}
	if (!Wood.IsEmpty())
	{
		int32 Placed = 0;
		const int32 Count = bRocky ? Config->ForestCount / 2 : Config->ForestCount;
		for (int32 Try = 0; Try < Count * 8 && Placed < Count; ++Try)
		{
			const FVector2D Point = Spot();
			const double Gap = Away(Point);
			if (Gap < R * Config->ForestStart || Gap > R * (Config->BorderReach + Config->ForestBeyond))
			{
				continue;
			}
			// in clumps, denser further out, so it closes into a wood
			const double Clump = 0.5 + 0.5 * FMath::PerlinNoise2D(Point / (R * 3.0) + NoiseOrigin);
			const double Out = FMath::Clamp((Gap - R * Config->ForestStart) / FMath::Max(R * 2.5, 1.0), 0.0, 1.0);
			if (Random.FRand() > FMath::Clamp(Clump * (1.0 - Config->ForestClumping) + Config->ForestClumping * Clump * Clump * 2.0, 0.05, 1.0) * (0.35 + 0.65 * Out))
			{
				continue;
			}
			const FVector At(Point.X, Point.Y, GetActorLocation().Z + GetGroundHeight(Point) - 2.0);
			MakeProp(Wood[Random.RandRange(0, Wood.Num() - 1)].LoadSynchronous(), At, R * Random.FRandRange(0.7, 1.7) * (bRocky ? 0.8 : 1.0), Random.RandRange(0, 359), bRocky ? 0.15 : 0.0);
			++Placed;
		}
	}
	// the rocks, big and sunk into the ground, along the edge of the forest
	TArray<UStaticMesh*> Rocks;
	for (const TSoftObjectPtr<UStaticMesh>& Rock : Config->BorderRocks)
	{
		if (UStaticMesh* Mesh = Rock.LoadSynchronous())
		{
			Rocks.Add(Mesh);
		}
	}
	for (int32 Try = 0, Placed = 0; !Rocks.IsEmpty() && Try < Config->RockCount * 12 && Placed < Config->RockCount; ++Try)
	{
		const FVector2D Point = Spot();
		const double Gap = Away(Point);
		if (FMath::Abs(Gap - R * Config->BorderReach) > R * 0.9)
		{
			continue;
		}
		const FVector At(Point.X, Point.Y, GetActorLocation().Z + GetGroundHeight(Point));
		MakeProp(Rocks[Random.RandRange(0, Rocks.Num() - 1)], At, R * Config->RockSize * Random.FRandRange(0.6, 1.5), Random.RandRange(0, 359), 0.25);
		++Placed;
	}

	// the fog of war: banks one over another over the ground from FogStart past the box out to FogEnd, over a sky of its color
	for (int32 Layer = 0; Layer < Config->FogLayers; ++Layer)
	{
		UMaterialInterface* FogMaterial = Config->FogMaterial.LoadSynchronous();
		if (!FogMaterial)
		{
			break;
		}
		const FBox2D Cover = Box.ExpandBy(double(Config->FogEnd) * 4.0);
		const FVector2D Local(GetActorLocation().X, GetActorLocation().Y);
		FPropMesh Plane;
		const double Z = Config->FogHeight * (1.0 + Config->FogLayerStep * (Layer - 1));
		Plane.Quad(FVector(Cover.Min.X - Local.X, Cover.Min.Y - Local.Y, Z), FVector(Cover.Max.X - Local.X, Cover.Min.Y - Local.Y, Z), FVector(Cover.Max.X - Local.X, Cover.Max.Y - Local.Y, Z),
			FVector(Cover.Min.X - Local.X, Cover.Max.Y - Local.Y, Z), FVector::UpVector, FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), FVector2D(0.0, 1.0));
		UMaterialInstanceDynamic* Fog = UMaterialInstanceDynamic::Create(FogMaterial, this);
		HexMaterialConfig::Apply(Fog, Config);
		const float Shade = Battles->IsUnderground() ? Config->FogUndergroundShade : 1.0f;
		Fog->SetVectorParameterValue(TEXT("FogColor"), Config->FogColor * Shade);
		Fog->SetVectorParameterValue(TEXT("FogWispColor"), Config->FogWispColor * Shade);
		Fog->SetVectorParameterValue(TEXT("FogBox"), FLinearColor(float(Box.Min.X), float(Box.Min.Y), float(Box.Max.X), float(Box.Max.Y)));
		Fog->SetVectorParameterValue(TEXT("FogPulseColor"), Config->FogPulseColor * Shade);
		Fog->SetScalarParameterValue(TEXT("FogLayer"), float(Layer));
		Fog->SetScalarParameterValue(TEXT("FogLayerOpacity"), FMath::Pow(Config->FogLayerFade, float(Layer)));
		UProceduralMeshComponent* Bank = Plane.Make(this, Root, Fog, Props);
		Bank->SetCastShadow(false);
		Bank->TranslucencySortPriority = 2 + Layer;
	}
}

void AHexBattleArena::PlaceFigures()
{
	FHexBattle* Battle = GetBattle();
	const TArray<FHexBattleUnit>& Units = Battle->GetUnits();
	for (const FFigure& Figure : Figures)
	{
		if (Figure.Component.IsValid())
		{
			Figure.Component->DestroyComponent();
		}
	}
	Figures.SetNum(Units.Num());
	ShownCounts.SetNum(Units.Num());
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		const FHexBattleUnit& Unit = Units[Index];
		FFigure& Figure = Figures[Index];
		Figure = FFigure();
		Figure.Creature = Unit.CreatureId;
		USceneComponent* Component = nullptr;
		if (Unit.bCatapult)
		{
			// the catapult: the mesh an artist gave, else a frame, an arm and a bucket of basic shapes in the color of wood
			Component = NewObject<USceneComponent>(this);
			Component->SetupAttachment(Root);
			Component->SetMobility(EComponentMobility::Movable);
			UMaterialInstanceDynamic* Wood = nullptr;
			if (UMaterialInterface* UnitMaterial = Config->UnitMaterial.LoadSynchronous())
			{
				Wood = UMaterialInstanceDynamic::Create(UnitMaterial, this);
				Wood->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.45f, 0.3f, 0.18f, 1.0f));
				Wood->SetVectorParameterValue(TEXT("SideColor"), Battles->GetSideColor(Unit.Side));
				Wood->SetScalarParameterValue(TEXT("SideColorShare"), 0.0f);
			}
			const auto AddPart = [&](const TCHAR* Path, const FVector& Offset, const FVector& Size, const FRotator& Turn)
			{
				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
				if (!Mesh)
				{
					return;
				}
				UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
				Part->SetStaticMesh(Mesh);
				Part->SetupAttachment(Component);
				Part->SetMobility(EComponentMobility::Movable);
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Part->SetRelativeTransform(FTransform(Turn, Offset, Size / 100.0));
				if (Wood)
				{
					Part->SetMaterial(0, Wood);
				}
				Part->RegisterComponent();
				SiegeProps.Add(Part);
			};
			const double Size = Config->CatapultSize;
			if (UStaticMesh* Given = Config->CatapultMesh.LoadSynchronous())
			{
				UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
				Part->SetStaticMesh(Given);
				Part->SetupAttachment(Component);
				Part->SetMobility(EComponentMobility::Movable);
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				const double Extent = FMath::Max(Given->GetBounds().BoxExtent.GetMax(), 1.0);
				Part->SetRelativeScale3D(FVector(Size * 0.5 / Extent));
				Part->RegisterComponent();
				SiegeProps.Add(Part);
			}
			else
			{
				AddPart(TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(0.0, 0.0, Size * 0.12), FVector(Size, Size * 0.55, Size * 0.18), FRotator::ZeroRotator);
				AddPart(TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(-Size * 0.1, 0.0, Size * 0.35), FVector(Size * 0.12, Size * 0.5, Size * 0.4), FRotator::ZeroRotator);
				AddPart(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(Size * 0.12, 0.0, Size * 0.55), FVector(Size * 0.08, Size * 0.08, Size * 0.9), FRotator(-55.0, 0.0, 0.0));
				AddPart(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FVector(Size * 0.42, 0.0, Size * 0.85), FVector(Size * 0.22), FRotator::ZeroRotator);
				for (const double Side : {-1.0, 1.0})
				{
					AddPart(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(-Size * 0.35, Side * Size * 0.32, Size * 0.12), FVector(Size * 0.24, Size * 0.24, Size * 0.06), FRotator(0.0, 0.0, 90.0));
					AddPart(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(Size * 0.35, Side * Size * 0.32, Size * 0.12), FVector(Size * 0.24, Size * 0.24, Size * 0.06), FRotator(0.0, 0.0, 90.0));
				}
			}
		}
		else
		{
			// the shooters of a tower are smaller and stand on top of it, but a dead one lies on its rubble
			Component = HexUnitFigure::Make(this, Root, Config, Unit.CreatureId, Unit.Kind.bUpgraded, Unit.Level, Battles->GetSideColor(Unit.Side),
				Unit.bTower ? Config->UnitScale * Config->TowerFigureScale : Config->UnitScale, Figure.Scale, Figure.Yaw, Figure.Hover);
			if (Unit.bTower && Unit.IsAlive())
			{
				Figure.Hover += TowerTop;
			}
		}
		Figure.Component = Component;
		Figure.Position = GetHexPoint(Unit.Hex);
		if (Unit.bTower)
		{
			const int32 Wall = Battle->GetWalls().IndexOfByPredicate([Index](const FHexBattleWall& Each) { return Each.Unit == Index; });
			Figure.Position = Wall != INDEX_NONE ? GetWallPartPoint(Wall) : Figure.Position;
		}
		Figure.Facing = Unit.Side == 0 ? FVector(0.0, 1.0, 0.0) : FVector(0.0, -1.0, 0.0);
		Figure.bDead = !Unit.IsAlive();
		if (const FHexUnitLook* Look = Figure.bDead ? Config->FindLook(Unit.CreatureId) : nullptr)
		{
			// a dead flyer lies on the ground
			Figure.Hover -= Look->Hover * Config->UnitScale;
		}
		ShownCounts[Index] = Unit.Count;
		if (Component)
		{
			Component->RegisterComponent();
			PlaceFigure(Index);
			// a stack killed lies on the field as a corpse, as the developer asked on 2026-11-12, where its figure can die
			const bool bCorpse = Figure.bDead && HexUnitFigure::Play(Component, Config, Unit.CreatureId, EHexFigureAction::Death, 1.0f, true);
			Component->SetVisibility(!Figure.bDead || bCorpse, true);
			if (!Figure.bDead)
			{
				HexUnitFigure::Play(Component, Config, Unit.CreatureId, EHexFigureAction::Idle, 0.9f + 0.03f * float(Index % 5));
			}
		}
	}
}

void AHexBattleArena::PlaceFigure(int32 Unit)
{
	if (!Figures.IsValidIndex(Unit) || !Figures[Unit].Component.IsValid())
	{
		return;
	}
	const FFigure& Figure = Figures[Unit];
	const FRotator Turn = FRotationMatrix::MakeFromX(Figure.Facing.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Figure.Facing.GetSafeNormal2D()).Rotator()
		+ FRotator(0.0, Figure.Yaw, 0.0);
	Figure.Component->SetWorldTransform(FTransform(Turn, Figure.Position + FVector(0.0, 0.0, Figure.Hover), FVector(Figure.Scale)));
}

bool AHexBattleArena::GetWallPoint(int32 Wall, FVector& OutWorld) const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle || !Battle->GetWalls().IsValidIndex(Wall) || !Battle->GetWalls()[Wall].IsStanding())
	{
		return false;
	}
	OutWorld = GetWallPartPoint(Wall) + FVector(0.0, 0.0, Config->SiegeWallHeight * 1.05);
	return true;
}

void AHexBattleArena::BuildSiege()
{
	FHexBattle* Battle = GetBattle();
	const UHexCastleConfig* CastleConfig = UHexCastleConfig::Load();
	if (!Battle || !CastleConfig || Battle->GetWalls().IsEmpty())
	{
		return;
	}
	// made again only when a part fell
	FString Key;
	for (const FHexBattleWall& Wall : Battle->GetWalls())
	{
		Key += Wall.IsStanding() ? TEXT("1") : TEXT("0");
	}
	if (Key == SiegeKey)
	{
		return;
	}
	SiegeKey = Key;
	for (const TObjectPtr<USceneComponent>& Prop : SiegeProps)
	{
		if (Prop && !Prop->IsA<UStaticMeshComponent>())
		{
			continue;
		}
		// the parts of the catapult stay
		if (Prop && Prop->GetAttachParent() && Prop->GetAttachParent() != Root)
		{
			continue;
		}
		if (Prop)
		{
			Prop->DestroyComponent();
		}
	}
	SiegeProps.RemoveAll([](const TObjectPtr<USceneComponent>& Prop) { return !Prop || Prop->IsBeingDestroyed(); });
	ArchMeshes.Reset();
	if (!CastleMaterial)
	{
		if (UMaterialInterface* Parent = CastleConfig->Material.LoadSynchronous())
		{
			CastleMaterial = UMaterialInstanceDynamic::Create(Parent, this);
			HexMaterialConfig::Apply(CastleMaterial, UHexSiteConfig::Load());
			HexMaterialConfig::Apply(CastleMaterial, CastleConfig);
			CastleMaterial->SetScalarParameterValue(TEXT("SiegeFadeRadius"), Config->SiegeFadeRadius);
			CastleMaterial->SetScalarParameterValue(TEXT("SiegeFadeKeep"), Config->SiegeFadeKeep);
		}
	}
	const int32 Fort = Battles->GetFortLevel();
	const FHexCastle* Castle = Battles->GetCastle();
	const FLinearColor OwnerColor = Castle ? Battles->GetSideColor(1) : FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	const float Style = Castle && CastleConfig->Factions.IsValidIndex(Castle->Faction) ? CastleConfig->Factions[Castle->Faction].WallStyle : 0.0f;
	const auto GetMesh = [Fort](const TArray<TSoftObjectPtr<UStaticMesh>>& Meshes) { return Meshes.IsValidIndex(Fort) ? Meshes[Fort].LoadSynchronous() : nullptr; };
	UStaticMesh* WallMesh = GetMesh(CastleConfig->WallMeshes);
	UStaticMesh* TowerMesh = GetMesh(CastleConfig->TowerMeshes);
	// the scale of the wall, by its height, which the towers and the gate share
	const double Scale = WallMesh ? Config->SiegeWallHeight / FMath::Max(WallMesh->GetBounds().BoxExtent.Z * 2.0, 1.0) : 1.0;
	const auto MakeInstances = [&](UStaticMesh* Mesh) -> UInstancedStaticMeshComponent*
	{
		if (!Mesh)
		{
			return nullptr;
		}
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this);
		Component->SetupAttachment(Root);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetNumCustomDataFloats(11);
		if (CastleMaterial)
		{
			Component->SetMaterial(0, CastleMaterial);
		}
		Component->RegisterComponent();
		SiegeProps.Add(Component);
		return Component;
	};
	const auto AddInstance = [&](UInstancedStaticMeshComponent* Component, const FVector& At, double Turn, double Size = 1.0, double Wide = 1.0) -> int32
	{
		if (!Component)
		{
			return INDEX_NONE;
		}
		const float Data[11] = {OwnerColor.R, OwnerColor.G, OwnerColor.B, OwnerColor.R, OwnerColor.G, OwnerColor.B, -1000.0f, Style, -1000.0f, 0.0f, 0.0f};
		const int32 Index = Component->AddInstance(FTransform(FRotator(0.0, Turn, 0.0), At - GetActorLocation(), FVector(Scale * Size * Wide, Scale * Size * Wide, Scale * Size)), false);
		Component->SetCustomData(Index, MakeArrayView(Data), true);
		return Index;
	};
	UInstancedStaticMeshComponent* Walls = MakeInstances(WallMesh);
	UInstancedStaticMeshComponent* Towers = MakeInstances(TowerMesh);
	const TArray<FHexBattleWall>& Parts = Battle->GetWalls();
	const FSiegeShape Shape = GetSiegeShape();
	// the shooters of a tower stand this high on it
	TowerTop = TowerMesh ? (TowerMesh->GetBounds().Origin.Z + TowerMesh->GetBounds().BoxExtent.Z) * Scale * Config->TowerStandShare : 0.0;
	const double TowerRadius = (TowerMesh ? FMath::Max(TowerMesh->GetBounds().BoxExtent.X, TowerMesh->GetBounds().BoxExtent.Y) * Scale : Config->HexRadius * 0.3) * Config->TowerWidthScale;
	// the stretches between two points, as many as fill the way, turned along it; the mesh of the castle lies along its Y
	const double StretchLength = WallMesh ? FMath::Max(WallMesh->GetBounds().BoxExtent.Y * 2.0 * Scale, 1.0) : 1.0;
	const auto AddStretch = [&](const FVector& A, const FVector& B)
	{
		if (FVector::Dist2D(A, B) < 1.0)
		{
			return;
		}
		const FVector Along = (B - A).GetSafeNormal2D();
		const double Turn = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)) - 90.0;
		const int32 Count = FMath::Max(FMath::CeilToInt32(FVector::Dist2D(A, B) / StretchLength), 1);
		for (int32 Piece = 0; Piece < Count; ++Piece)
		{
			AddInstance(Walls, FMath::Lerp(A, B, (Piece + 0.5) / Count), Turn);
		}
	};
	const auto IsCorner = [&Parts](int32 Index) { return Parts[Index].Part == EHexWallPart::Tower && (Index == 0 || Index == Parts.Num() - 1); };
	// a stretch that meets the gate stops at the tower beside its opening
	const double GateFlank = Shape.GateHalf + TowerRadius * Config->GateTowerScale * 0.7;
	const auto EndAt = [&](int32 Index, const FVector& Towards)
	{
		const FVector At = GetWallPartPoint(Index);
		return Parts[Index].Part == EHexWallPart::Gate ? At + (Towards - At).GetSafeNormal2D() * GateFlank : At;
	};

	// the front wall between the parts that stand, from corner to corner, where a tower at an end of it stands
	for (int32 Index = 0; Index + 1 < Parts.Num(); ++Index)
	{
		if (Parts[Index].IsStanding() && Parts[Index + 1].IsStanding())
		{
			AddStretch(EndAt(Index, GetWallPartPoint(Index + 1)), EndAt(Index + 1, GetWallPartPoint(Index)));
		}
	}
	if (Parts[0].IsStanding() && !IsCorner(0))
	{
		AddStretch(Shape.FrontStart, GetWallPartPoint(0));
	}
	if (Parts.Last().IsStanding() && !IsCorner(Parts.Num() - 1))
	{
		AddStretch(GetWallPartPoint(Parts.Num() - 1), Shape.FrontEnd);
	}

	// the walls that close the yard outside the field, which nothing harms: the side walls go on past the palace, as the yard is only the
	// front of the castle, as the developer asked on 2026-11-13; all of them go around in one way, as the front wall does, so the meshes face
	// alike
	AddStretch(Shape.FrontEnd, Shape.SideEnd);
	AddStretch(Shape.SideStart, Shape.FrontStart);
	if (Fort >= 2 && Towers)
	{
		AddInstance(Towers, Shape.BackStart, -90.0, 1.0, Config->TowerWidthScale);
		AddInstance(Towers, Shape.BackEnd, -90.0, 1.0, Config->TowerWidthScale);
	}

	// the palace at the back of the yard: the hall of the castle in its middle on a flight of steps, the dwellings built beside it behind the
	// wall, and from a citadel on a keep behind the hall, as the developer asked for something like a guarded palace and not a plain wall
	{
		const FVector BackMiddle = (Shape.BackStart + Shape.BackEnd) * 0.5;
		const FVector BackAlong = (Shape.BackStart - Shape.BackEnd).GetSafeNormal2D();
		const double Facing = FMath::RadiansToDegrees(FMath::Atan2(-Shape.In.Y, -Shape.In.X)) + Config->HallYaw;
		const int32 Faction = Castle && CastleConfig->Factions.IsValidIndex(Castle->Faction) ? Castle->Faction : 0;
		const auto BuiltMesh = [&](int32 Building) -> UStaticMesh*
		{
			const TArray<FHexBuilding>& Buildings = CastleConfig->Factions[Faction].Buildings;
			const int32 Built = Castle && Castle->Levels.IsValidIndex(Building) ? Castle->Levels[Building] : (Buildings[Building].Type == EHexBuildingType::Hall ? 1 : 0);
			for (int32 Level = FMath::Min(Built, Buildings[Building].Levels.Num()) - 1; Level >= 0; --Level)
			{
				if (UStaticMesh* Mesh = Buildings[Building].Levels[Level].Mesh.LoadSynchronous())
				{
					return Mesh;
				}
			}
			return nullptr;
		};
		UStaticMesh* HallMesh = nullptr;
		TArray<UStaticMesh*> Dwellings;
		if (CastleConfig->Factions.IsValidIndex(Faction))
		{
			const TArray<FHexBuilding>& Buildings = CastleConfig->Factions[Faction].Buildings;
			for (int32 Building = 0; Building < Buildings.Num(); ++Building)
			{
				UStaticMesh* Mesh = BuiltMesh(Building);
				if (Buildings[Building].Type == EHexBuildingType::Hall && !HallMesh)
				{
					HallMesh = Mesh;
				}
				else if (Buildings[Building].Type == EHexBuildingType::Dwelling && Mesh)
				{
					Dwellings.Add(Mesh);
				}
			}
		}
		const auto Width = [&](UStaticMesh* Mesh) { return Mesh ? FMath::Max(Mesh->GetBounds().BoxExtent.X, Mesh->GetBounds().BoxExtent.Y) * 2.0 * Scale * Config->HallScale : 0.0; };
		const double HallWidth = Width(HallMesh);
		// the back wall with an opening for the steps up to the hall
		const double StepsHalf = FMath::Max(HallWidth * 0.32, Config->HexRadius * 0.8);
		AddStretch(Shape.BackEnd, BackMiddle - BackAlong * StepsHalf);
		AddStretch(BackMiddle + BackAlong * StepsHalf, Shape.BackStart);
		if (HallMesh)
		{
			AddInstance(MakeInstances(HallMesh), BackMiddle + Shape.In * (HallWidth * 0.5 + Config->HexRadius * 0.3), Facing, Config->HallScale);
		}
		if (Fort >= 2 && Towers)
		{
			AddInstance(Towers, BackMiddle + Shape.In * (HallWidth + TowerRadius * Config->KeepScale), -90.0, Config->KeepScale, Config->TowerWidthScale);
		}
		// the dwellings behind the wall, outwards from the hall by turns on either side
		const double BackLength = FVector::Dist2D(Shape.BackStart, Shape.BackEnd);
		double Taken[2] = {HallWidth * 0.5 + Config->HexRadius * 0.4, HallWidth * 0.5 + Config->HexRadius * 0.4};
		for (int32 Index = 0; Index < Dwellings.Num(); ++Index)
		{
			const int32 Side = Index % 2;
			const double DwellingWidth = Width(Dwellings[Index]);
			if (Taken[Side] + DwellingWidth > BackLength * 0.5)
			{
				continue;
			}
			const FVector At = BackMiddle + BackAlong * ((Side == 0 ? 1.0 : -1.0) * (Taken[Side] + DwellingWidth * 0.5)) + Shape.In * (DwellingWidth * 0.5 + Config->HexRadius * 0.15);
			AddInstance(MakeInstances(Dwellings[Index]), At, Facing, Config->HallScale);
			Taken[Side] += DwellingWidth + Config->HexRadius * 0.25;
		}
		// the steps up to the hall, out of the opening into the yard
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			const int32 StoneStyle = Style > 0.5f ? 1 : 0;
			UMaterialInstanceDynamic* Stone = MakeProp(this, Config, Config->StoneTextures.IsValidIndex(StoneStyle) ? Config->StoneTextures[StoneStyle] : TSoftObjectPtr<UTexture2D>(),
				Config->StoneNormals.IsValidIndex(StoneStyle) ? Config->StoneNormals[StoneStyle] : TSoftObjectPtr<UTexture2D>(), Config->StepsColor, Config->StoneTileSize * 0.5f, false, false);
			const FRotator Turn = FRotationMatrix::MakeFromX(Shape.In).Rotator();
			const int32 Count = FMath::Max(Config->PalaceSteps, 1);
			const double Tread = Config->HexRadius * 0.28;
			for (int32 Step = 0; Step < Count; ++Step)
			{
				// the lowest out in the yard, each higher one shorter towards the hall
				const double Depth = Tread * (Count - Step);
				const double Rise = Config->PalaceStepRise * (Step + 1);
				UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
				Part->SetStaticMesh(Cube);
				Part->SetupAttachment(Root);
				Part->SetMobility(EComponentMobility::Movable);
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Part->SetWorldTransform(FTransform(Turn, BackMiddle + Shape.In * (Config->HexRadius * 0.3 - Depth * 0.5) + FVector(0.0, 0.0, Rise * 0.5),
					FVector(Depth, StepsHalf * 2.0 * (1.0 - 0.06 * Step), Rise) / 100.0));
				if (Stone)
				{
					Part->SetMaterial(0, Stone);
				}
				Part->RegisterComponent();
				SiegeProps.Add(Part);
			}
		}
	}

	// the water of the moat under the front wall, part of the ground, which draws its bed and banks, and a bridge of planks over it before the gate
	if (Shape.bMoat)
	{
		if (!MoatWater.IsValid())
		{
			MoatWater = MakeMoatWater(Shape);
		}
		FHexCoord BridgeHex;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (Battle->GetBridge(BridgeHex))
		{
			// a deck of planks from the face of the wall out over the far shore, rising a little in its middle, with posts and rails along its sides
			const FVector Out = -Shape.In;
			const FVector Across = FVector::CrossProduct(FVector::UpVector, Out).GetSafeNormal();
			const FVector Gate = GetWallLinePoint(FHexCoord::FromOffset(BridgeHex.GetColumn() + 1, BridgeHex.GetRow()));
			const double Far = FMath::Min(Shape.MoatRows.Z, Shape.MoatRows.W);
			const double HalfThick = FVector::DotProduct(Shape.FrontStart, Shape.In) - Shape.WallFace;
			const double Span = FVector::DotProduct(Gate, Shape.In) - HalfThick - Far + Config->HexRadius * 0.35;
			const double Wide = Shape.GateHalf * 2.0 * Config->BridgeWidth;
			const FVector Start = Gate - GetActorLocation() + Out * HalfThick + FVector(0.0, 0.0, Config->MoatLift + Config->MoatWaveHeight + 2.0);
			const double Deck = 5.0;
			const auto Height = [&](double S) { return Config->BridgeRise * FMath::Sin(UE_PI * FMath::Clamp(S / Span, 0.0, 1.0)); };
			const auto At = [&](double S, double T, double Z) { return Start + Out * S + Across * T + FVector(0.0, 0.0, Height(S) + Z); };
			FPropMesh Planks;
			const int32 Steps = 10;
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const double S0 = Span * Step / Steps;
				const double S1 = Span * (Step + 1) / Steps;
				const FVector Slope = (At(S1, 0.0, 0.0) - At(S0, 0.0, 0.0)).GetSafeNormal();
				const FVector Up = FVector::CrossProduct(Slope, Across).GetSafeNormal() * (FVector::CrossProduct(Slope, Across).Z >= 0.0 ? 1.0 : -1.0);
				for (const double Z : {0.0, -Deck})
				{
					Planks.Quad(At(S0, -Wide * 0.5, Z), At(S0, Wide * 0.5, Z), At(S1, Wide * 0.5, Z), At(S1, -Wide * 0.5, Z), Z == 0.0 ? Up : -Up,
						FVector2D(-Wide * 0.5, S0), FVector2D(Wide * 0.5, S0), FVector2D(Wide * 0.5, S1), FVector2D(-Wide * 0.5, S1));
				}
				for (const double T : {-Wide * 0.5, Wide * 0.5})
				{
					Planks.Quad(At(S0, T, -Deck), At(S1, T, -Deck), At(S1, T, 0.0), At(S0, T, 0.0), Across * (T > 0.0 ? 1.0 : -1.0),
						FVector2D(S0, 0.0), FVector2D(S1, 0.0), FVector2D(S1, Deck), FVector2D(S0, Deck));
				}
			}
			Planks.Make(this, Root, MakeProp(this, Config, Config->BridgeWoodTexture, Config->BridgeWoodNormal, Config->BridgeWoodColor, Config->WoodTileSize, true, Config->bBridgePlanksTurned), SiegeProps);
			UMaterialInstanceDynamic* Beams = MakeProp(this, Config, Config->BridgeWoodTexture, Config->BridgeWoodNormal, Config->BridgeWoodColor * 0.8f, Config->WoodTileSize, false, false);
			const int32 Posts = 4;
			for (const double T : {-Wide * 0.5 + 2.5, Wide * 0.5 - 2.5})
			{
				for (int32 Post = 0; Post < Posts; ++Post)
				{
					const double S = Span * (0.04 + 0.92 * Post / (Posts - 1));
					AddBox(this, Root, Cube, Beams, FTransform(FRotationMatrix::MakeFromX(Out).Rotator(), GetActorLocation() + At(S, T, 8.0), FVector(4.0, 4.0, 18.0) / 100.0), SiegeProps);
				}
				for (int32 Post = 0; Post + 1 < Posts; ++Post)
				{
					const double S0 = Span * (0.04 + 0.92 * Post / (Posts - 1));
					const double S1 = Span * (0.04 + 0.92 * (Post + 1) / (Posts - 1));
					const FVector A = At(S0, T, 16.0);
					const FVector B = At(S1, T, 16.0);
					AddBox(this, Root, Cube, Beams, FTransform(FRotationMatrix::MakeFromX(B - A).Rotator(), GetActorLocation() + (A + B) * 0.5, FVector(FVector::Dist(A, B), 3.0, 3.0) / 100.0), SiegeProps);
				}
			}
		}
	}

	// the parts of the front wall, a tower in its place, rubble where one fell, and the gate: two towers beside an opening GateOpening hexes
	// wide, as wide as the longest diagonal of a hex, as the developer asked on 2026-11-13 when the gatehouse was too narrow and of stone, and two
	// leaves of timber in it, which swing into the yard while a defender goes through or stands in it
	for (const TObjectPtr<USceneComponent>& Part : GateParts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	GateParts.Reset();
	GateHinges[0] = GateHinges[1] = nullptr;
	FRandomStream Random(int32(Battle->GetRound() * 31 + 7));
	WallPicks.Reset();
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		const FHexBattleWall& Part = Parts[Index];
		const FVector At = GetWallPartPoint(Index);
		if (!Part.IsStanding())
		{
			// rubble where it fell
			const FHexBattleGround* Each = GetGround();
			UStaticMesh* Rubble = Each && !Each->Obstacles.IsEmpty() ? Each->Obstacles[0].LoadSynchronous() : Config->FallbackMesh.LoadSynchronous();
			if (Rubble)
			{
				const double Extent = FMath::Max(Rubble->GetBounds().BoxExtent.GetMax(), 1.0);
				AddBox(this, Root, Rubble, nullptr, FTransform(FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), At,
					FVector(Config->HexRadius * 0.45 / Extent, Config->HexRadius * 0.45 / Extent, Config->HexRadius * 0.2 / Extent)), SiegeProps);
			}
			continue;
		}
		if (Part.Part == EHexWallPart::Tower)
		{
			AddInstance(Towers, At, -90.0, 1.0, Config->TowerWidthScale);
			WallPicks.Add({At, TowerRadius, TowerTop / FMath::Max(double(Config->TowerStandShare), 0.1), Part.Hex});
		}
		else if (Part.Part == EHexWallPart::Wall)
		{
			WallPicks.Add({At, Config->HexRadius * 0.5, double(Config->SiegeWallHeight) * 0.7, Part.Hex});
		}
		else if (Part.Part == EHexWallPart::Gate)
		{
			// an arch of stone between two towers over an opening as wide as the longest diagonal of a hex, pointed, as the developer asked on
			// 2026-11-13 for a gate of the shape of an arch and Gothic, and two leaves of timber under it whose tops follow the arch
			const FVector Along = (Shape.FrontEnd - Shape.FrontStart).GetSafeNormal2D();
			const double Flank = GateFlank;
			for (const double Side : {-1.0, 1.0})
			{
				AddInstance(Towers, At + Along * (Side * Flank), -90.0, Config->GateTowerScale, Config->TowerWidthScale);
				const int32 Beside = Index + (Side > 0.0 ? 1 : -1);
				if (Parts.IsValidIndex(Beside) && Parts[Beside].IsStanding())
				{
					WallPicks.Add({At + Along * (Side * Flank), TowerRadius * Config->GateTowerScale, TowerTop / FMath::Max(double(Config->TowerStandShare), 0.1) * Config->GateTowerScale, Parts[Beside].Hex});
				}
			}
			const double W = Shape.GateHalf;
			const double Spring = Config->SiegeWallHeight * Config->GateSpringShare;
			const double Radius = W * FMath::Max(double(Config->GateArchRadius), 1.0);
			// the top of the opening at X from its edge towards its middle: a circle from the springing at the edge to the point in the middle
			const auto ArchTop = [&](double X) { return Spring + FMath::Sqrt(FMath::Max(Radius * Radius - FMath::Square(X - Radius), 0.0)); };
			const double HalfThick = FVector::DotProduct(Shape.FrontStart, Shape.In) - Shape.WallFace;
			const double Top = Config->SiegeWallHeight * 1.04;
			const FVector Middle = At - GetActorLocation();
			const auto Point = [&](double U, double Y, double Z) { return Middle + Along * U + Shape.In * Y + FVector(0.0, 0.0, Z); };
			{
				// the wall over the gate, a mesh of the castle built here, as the developer found it of another stone and thicker than the walls on
				// 2026-11-13: in the material of the castle with the custom values of the instance as its walls have, the stone marked as the
				// meshes of the castle mark it (the alpha of a vertex 0.1, its tone as its color), its UV in tiles of CastleTile units of a mesh, as
				// thick as the wall itself (ArchThickShare of the bounds of the mesh of the wall, which hold its buttress and plinth) and as high
				// (ArchTopShare of them, which hold its merlons), from the middle of one tower to that of the other, with merlons on top
				const double Unit = 1.0 / FMath::Max(Scale, 0.0001);
				const FBoxSphereBounds WallBounds = WallMesh ? WallMesh->GetBounds() : FBoxSphereBounds(FVector::ZeroVector, FVector(6.0, 10.0, 40.0), 40.0);
				const double Body = WallBounds.BoxExtent.X * Config->ArchThickShare;
				// as high as the wall, or above the point of the arch where that rises higher, as a gatehouse does
				const double WallTopZ = (WallBounds.Origin.Z + WallBounds.BoxExtent.Z) * Config->ArchTopShare;
				const double Wm = W * Unit;
				const double Fm = Flank * Unit;
				const double Rm = Radius * Unit;
				const double Sm = Spring * Unit;
				const auto ArchTopM = [&](double X) { return Sm + FMath::Sqrt(FMath::Max(Rm * Rm - FMath::Square(X - Rm), 0.0)); };
				const double TopZ = FMath::Max(WallTopZ, ArchTopM(Wm) + Config->GateArchCrown);
				const double Tile = FMath::Max(double(Config->CastleTile), 1.0);
				FPropMesh Arch;
				// the stone of the meshes of the castle (STONE of generate_planet_castles.py), which was twice as light here until 2026-11-13
				Arch.Color = FLinearColor(Config->CastleStoneTone, Config->CastleStoneTone, Config->CastleStoneTone, 0.1f);
				TArray<double> Us;
				for (int32 Step = 0; Step <= 4; ++Step)
				{
					Us.Add(FMath::Lerp(-Fm, -Wm, Step / 4.0));
				}
				for (int32 Step = 1; Step <= 20; ++Step)
				{
					Us.Add(FMath::Lerp(-Wm, Wm, Step / 20.0));
				}
				for (int32 Step = 1; Step <= 4; ++Step)
				{
					Us.Add(FMath::Lerp(Wm, Fm, Step / 4.0));
				}
				const auto Bottom = [&](double U) { return FMath::Abs(U) < Wm - 0.01 ? ArchTopM(Wm - FMath::Abs(U)) : 0.0; };
				const auto Uv = [Tile](double A, double B) { return FVector2D(A / Tile, B / Tile); };
				for (int32 Column = 0; Column + 1 < Us.Num(); ++Column)
				{
					const double U0 = Us[Column];
					const double U1 = Us[Column + 1];
					const double B0 = Bottom(U0);
					const double B1 = Bottom(U1);
					for (const double Side : {-1.0, 1.0})
					{
						const double Y = Side * Body;
						Arch.Quad(FVector(U0, Y, B0), FVector(U1, Y, B1), FVector(U1, Y, TopZ), FVector(U0, Y, TopZ), FVector(0.0, Side, 0.0),
							Uv(U0, B0), Uv(U1, B1), Uv(U1, TopZ), Uv(U0, TopZ));
					}
					Arch.Quad(FVector(U0, -Body, TopZ), FVector(U1, -Body, TopZ), FVector(U1, Body, TopZ), FVector(U0, Body, TopZ), FVector::UpVector,
						Uv(U0, -Body), Uv(U1, -Body), Uv(U1, Body), Uv(U0, Body));
					if (FMath::Abs(U0) < Wm + 0.01 && FMath::Abs(U1) < Wm + 0.01)
					{
						Arch.Quad(FVector(U0, -Body, B0), FVector(U1, -Body, B1), FVector(U1, Body, B1), FVector(U0, Body, B0), -FVector::UpVector,
							Uv(U0, -Body), Uv(U1, -Body), Uv(U1, Body), Uv(U0, Body));
					}
				}
				// the merlons along its outer edge, towards the attacker, lighter as the trim of the castle is
				Arch.Color = FLinearColor(Config->CastleStoneTone * 1.18f, Config->CastleStoneTone * 1.18f, Config->CastleStoneTone * 1.18f, 0.1f);
				const int32 Merlons = FMath::Max(FMath::RoundToInt32(2.0 * Fm / 14.0), 1);
				for (int32 Merlon = 0; Merlon < Merlons; ++Merlon)
				{
					const double U = -Fm + 2.0 * Fm * (Merlon + 0.5) / Merlons;
					Arch.Box(FVector(U, Body - 1.8, TopZ + 3.5), FVector(3.6, 1.8, 3.5), Tile);
				}
				// the gate is picked by its arch, up to its top
				WallPicks.Add({At, W, TopZ * Scale, Part.Hex});
				if (UStaticMesh* ArchMesh = Arch.Build(this, CastleMaterial))
				{
					ArchMeshes.Add(ArchMesh);
					const double Turn = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
					AddInstance(MakeInstances(ArchMesh), At, Turn);
				}
			}
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			UMaterialInstanceDynamic* Wood = MakeProp(this, Config, Config->GateWoodTexture, Config->GateWoodNormal, Config->GateWoodColor, Config->WoodTileSize, true, Config->bGatePlanksTurned);
			UMaterialInstanceDynamic* Iron = MakeProp(this, Config, Config->GateWoodTexture, Config->GateWoodNormal, Config->GateIronColor, Config->WoodTileSize, false, false);
			const double Thick = Config->GateLeafThickness;
			for (int32 Leaf = 0; Leaf < 2; ++Leaf)
			{
				// a hinge at the edge of the opening, the leaf reaching to its middle along the X of the hinge
				const double Side = Leaf == 0 ? -1.0 : 1.0;
				USceneComponent* Hinge = NewObject<USceneComponent>(this);
				Hinge->SetupAttachment(Root);
				Hinge->SetMobility(EComponentMobility::Movable);
				Hinge->SetWorldLocation(At + Along * (Side * W));
				Hinge->SetWorldRotation(FRotationMatrix::MakeFromX(-Along * Side).Rotator());
				Hinge->RegisterComponent();
				GateParts.Add(Hinge);
				GateHinges[Leaf] = Hinge;
				FPropMesh Boards;
				const int32 Steps = 12;
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					const double X0 = W * Step / Steps;
					const double X1 = FMath::Min(W * (Step + 1) / Steps, W - 0.6);
					const double T0 = ArchTop(X0);
					const double T1 = ArchTop(X1);
					for (const double Face : {-1.0, 1.0})
					{
						const double Y = Face * Thick * 0.5;
						Boards.Quad(FVector(X0, Y, 0.0), FVector(X1, Y, 0.0), FVector(X1, Y, T1), FVector(X0, Y, T0), FVector(0.0, Face, 0.0),
							FVector2D(X0 + Leaf * W, 0.0), FVector2D(X1 + Leaf * W, 0.0), FVector2D(X1 + Leaf * W, T1), FVector2D(X0 + Leaf * W, T0));
					}
					Boards.Quad(FVector(X0, -Thick * 0.5, T0), FVector(X1, -Thick * 0.5, T1), FVector(X1, Thick * 0.5, T1), FVector(X0, Thick * 0.5, T0),
						FVector(-(T1 - T0), 0.0, X1 - X0).GetSafeNormal(), FVector2D(X0, 0.0), FVector2D(X1, 0.0), FVector2D(X1, Thick), FVector2D(X0, Thick));
				}
				for (const double X : {0.0, W - 0.6})
				{
					const double H = ArchTop(X);
					Boards.Quad(FVector(X, -Thick * 0.5, 0.0), FVector(X, Thick * 0.5, 0.0), FVector(X, Thick * 0.5, H), FVector(X, -Thick * 0.5, H),
						FVector(X > 0.0 ? 1.0 : -1.0, 0.0, 0.0), FVector2D(0.0, 0.0), FVector2D(Thick, 0.0), FVector2D(Thick, H), FVector2D(0.0, H));
				}
				Boards.Make(this, Hinge, Wood, GateParts);
				// bands of iron across it
				for (const double Band : {0.22, 0.72})
				{
					if (Cube)
					{
						UStaticMeshComponent* Strap = NewObject<UStaticMeshComponent>(this);
						Strap->SetStaticMesh(Cube);
						Strap->SetupAttachment(Hinge);
						Strap->SetMobility(EComponentMobility::Movable);
						Strap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						Strap->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(W * 0.5, 0.0, Spring * Band), FVector(W * 0.96, Thick + 1.5, 3.2) / 100.0));
						if (Iron)
						{
							Strap->SetMaterial(0, Iron);
						}
						Strap->RegisterComponent();
						GateParts.Add(Strap);
					}
				}
			}
		}
	}
	GateOpenShown = 0.0f;

	// the materials of the props that dissolve in front of the hex under the cursor with the wall
	FadeMaterials.Reset();
	UMaterialInterface* PropParent = Config->PropMaterial.LoadSynchronous();
	for (const TArray<TObjectPtr<USceneComponent>>* List : {&GateParts})
	{
		for (const TObjectPtr<USceneComponent>& Each : *List)
		{
			const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Each.Get());
			UMaterialInstanceDynamic* Material = Primitive ? Cast<UMaterialInstanceDynamic>(Primitive->GetMaterial(0)) : nullptr;
			if (Material && PropParent && Material->Parent == PropParent)
			{
				Material->SetScalarParameterValue(TEXT("SiegeFadeRadius"), Config->SiegeFadeRadius);
				Material->SetScalarParameterValue(TEXT("SiegeFadeKeep"), Config->SiegeFadeKeep);
				FadeMaterials.AddUnique(Material);
			}
		}
	}
}

void AHexBattleArena::UpdateDebugView(bool bReset)
{
	const FHexDebugViewSettings Debug = AHexWorldMap::GetDebugViewSettings();
	const UHexPlanetLayers* PlanetLayers = UHexPlanetLayers::Get(GetWorld());
	const bool bGrid = bReset || !(PlanetLayers && PlanetLayers->GetSurface()) || PlanetLayers->GetSurface()->IsGridVisible();
	const EHexWireframeMode Mode = bReset ? EHexWireframeMode::Off : Debug.Mode;
	const bool bWater = bReset || Debug.Water == EHexWaterView::Shown;
	const int32 Key = int32(Mode) * 100 + (bWater ? 10 : 0) + (bGrid ? 1 : 0);
	if (Key == DebugKey)
	{
		return;
	}
	DebugKey = bReset ? -1 : Key;
	Grid->SetVisibility(bGrid);
	if (MoatWater.IsValid())
	{
		MoatWater->SetVisibility(bWater);
	}
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		const EViewModeIndex ViewMode = Mode == EHexWireframeMode::Only ? VMI_Wireframe : VMI_Lit;
		Viewport->ViewModeIndex = ViewMode;
		ApplyViewMode(ViewMode, true, Viewport->EngineShowFlags);
		Viewport->EngineShowFlags.SetMeshEdges(Mode == EHexWireframeMode::Overlay);
	}
}

UProceduralMeshComponent* AHexBattleArena::MakeMoatWater(const FSiegeShape& Shape)
{
	// a lattice over the water from beyond its far shore to the face of the wall, fine enough for its waves, and a band up the face of the wall,
	// whose second UV is 2 and more, on which the water washes up the wall
	UProceduralMeshComponent* Water = NewObject<UProceduralMeshComponent>(this);
	Water->SetupAttachment(Root);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetCastShadow(false);
	Water->TranslucencySortPriority = -1;
	const double Far = FMath::Min(Shape.MoatRows.Z, Shape.MoatRows.W) - Config->HexRadius * 0.4;
	const double Near = Shape.WallFace + Config->HexRadius * 0.05;
	const FVector2D Min(Shape.MoatXMin - Config->HexRadius, Far);
	const FVector2D Max(Shape.MoatXMax + Config->HexRadius, Near);
	const double Step = FMath::Max(Config->HexRadius * 0.12, 2.0);
	const int32 StepsX = FMath::Clamp(FMath::CeilToInt32((Max.X - Min.X) / Step), 1, 600);
	const int32 StepsY = FMath::Clamp(FMath::CeilToInt32((Max.Y - Min.Y) / Step), 1, 100);
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	const FVector Origin(GetActorLocation().X, GetActorLocation().Y, 0.0);
	for (int32 X = 0; X <= StepsX; ++X)
	{
		for (int32 Y = 0; Y <= StepsY; ++Y)
		{
			const FVector2D Point = Min + (Max - Min) * FVector2D(double(X) / StepsX, double(Y) / StepsY);
			Vertices.Add(FVector(Point.X, Point.Y, Config->MoatLift) - Origin);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(double(X) / StepsX, 0.0));
			Tangents.Add(FProcMeshTangent(1.0, 0.0, 0.0));
		}
	}
	for (int32 X = 0; X < StepsX; ++X)
	{
		for (int32 Y = 0; Y < StepsY; ++Y)
		{
			const int32 A = X * (StepsY + 1) + Y;
			const int32 B = A + 1;
			const int32 C = A + StepsY + 1;
			const int32 D = C + 1;
			Triangles.Append({A, B, C, B, D, C});
		}
	}
	// the band on the face of the wall, facing the attacker
	const int32 Base = Vertices.Num();
	const double Face = Shape.WallFace - 0.6;
	const int32 Rows = 4;
	for (int32 X = 0; X <= StepsX; ++X)
	{
		for (int32 Row = 0; Row <= Rows; ++Row)
		{
			const double Up = double(Row) / Rows;
			Vertices.Add(FVector(FMath::Lerp(Min.X, Max.X, double(X) / StepsX), Face, Config->MoatLift - 1.0 + Up * Config->MoatSplashHeight) - Origin);
			Normals.Add(-Shape.In);
			UVs.Add(FVector2D(double(X) / StepsX, 2.0 + Up));
			Tangents.Add(FProcMeshTangent(1.0, 0.0, 0.0));
		}
	}
	for (int32 X = 0; X < StepsX; ++X)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const int32 A = Base + X * (Rows + 1) + Row;
			const int32 B = A + 1;
			const int32 C = A + Rows + 1;
			const int32 D = C + 1;
			Triangles.Append({A, C, B, B, C, D});
		}
	}
	Water->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, {}, Tangents, false);
	if (UMaterialInterface* Material = Config->MoatMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		HexMaterialConfig::Apply(Instance, Config);
		SetMoatParameters(Instance, Shape);
		// towards the sun of the field
		const FVector Towards = -FRotator(Config->SunPitch, Config->SunYaw, 0.0).Vector();
		Instance->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(float(Towards.X), float(Towards.Y), float(Towards.Z), 0.0f));
		Water->SetMaterial(0, Instance);
	}
	Water->RegisterComponent();
	SiegeProps.Add(Water);
	return Water;
}

void AHexBattleArena::SetMoatParameters(UMaterialInstanceDynamic* Instance, const FSiegeShape& Shape) const
{
	Instance->SetVectorParameterValue(TEXT("MoatRows"), FLinearColor(float(Shape.MoatRows.X), float(Shape.MoatRows.Y), float(Shape.MoatRows.Z), float(Shape.MoatRows.W)));
	Instance->SetVectorParameterValue(TEXT("MoatWall"), FLinearColor(float(Shape.WallFace), float(Shape.MoatXMin), float(Shape.MoatXMax), Config->HexRadius));
	Instance->SetScalarParameterValue(TEXT("MoatOn"), Shape.bMoat ? 1.0f : 0.0f);
	Instance->SetScalarParameterValue(TEXT("MoatSeed"), float(GroundSeed % 97));
}

void AHexBattleArena::UpdateGate(float DeltaSeconds)
{
	FHexBattle* Battle = GetBattle();
	const FHexBattleWall* Part = Battle ? Battle->GetWalls().FindByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Gate; }) : nullptr;
	if (!Part || !Part->IsStanding() || !GateHinges[0] || !GateHinges[1])
	{
		return;
	}
	// open while a defender goes through it or stands in it, and for good once jammed
	bool bOpen = Battle->IsGateJammed() || Battle->FindUnitAt(Part->Hex) != INDEX_NONE;
	if (Current.IsSet() && Current->Type == FHexBattleEvent::EType::Move && Current->Path.Contains(Part->Hex))
	{
		bOpen = true;
	}
	GateOpenShown = FMath::FInterpConstantTo(GateOpenShown, bOpen ? 1.0f : 0.0f, DeltaSeconds, Config->GateSwingSpeed);
	const FSiegeShape Shape = GetSiegeShape();
	const FVector Along = (Shape.FrontEnd - Shape.FrontStart).GetSafeNormal2D();
	const float Eased = FMath::SmoothStep(0.0f, 1.0f, GateOpenShown);
	for (int32 Leaf = 0; Leaf < 2; ++Leaf)
	{
		// each leaf swings from along the wall into the yard
		const double Side = Leaf == 0 ? -1.0 : 1.0;
		const FVector Closed = -Along * Side;
		const double Angle = FMath::DegreesToRadians(double(Config->GateSwing * Eased));
		const FVector Way = Closed * FMath::Cos(Angle) + Shape.In * FMath::Sin(Angle);
		GateHinges[Leaf]->SetWorldRotation(FRotationMatrix::MakeFromX(Way).Rotator());
	}
}

bool AHexBattleArena::FindWallStrikeFrom(const FHexCoord& WallHex, FHexCoord& OutFrom) const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle)
	{
		return false;
	}
	double Nearest = TNumericLimits<double>::Max();
	bool bFound = false;
	for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
	{
		const FHexCoord From = WallHex.GetNeighbor(Direction);
		if (!Reach.Contains(From) || !Battle->CanStrikeWall(Battle->GetActive(), WallHex, From))
		{
			continue;
		}
		const double Gap = FVector::DistSquared2D(GetHexPoint(From), HoverPoint);
		if (Gap < Nearest)
		{
			Nearest = Gap;
			OutFrom = From;
			bFound = true;
		}
	}
	return bFound;
}

void AHexBattleArena::UpdateOutline()
{
	const FHexBattle* Battle = GetBattle();
	const int32 Unit = bShowing ? GetHoverUnit() : int32(INDEX_NONE);
	USkeletalMeshComponent* Figure = Figures.IsValidIndex(Unit) ? Cast<USkeletalMeshComponent>(Figures[Unit].Component.Get()) : nullptr;
	if (Unit == OutlinedUnit && (HoverOutline != nullptr) == (Figure != nullptr))
	{
		return;
	}
	OutlinedUnit = Unit;
	if (HoverOutline)
	{
		HoverOutline->DestroyComponent();
		HoverOutline = nullptr;
	}
	UMaterialInterface* Material = Config ? Config->OutlineMaterial.LoadSynchronous() : nullptr;
	if (!Figure || !Material || !Figure->GetSkeletalMeshAsset() || !Battle)
	{
		return;
	}
	// a copy of the figure that follows its pose, drawn a little larger by its material, only where it shows its back faces
	HoverOutline = NewObject<USkeletalMeshComponent>(this);
	HoverOutline->SetSkeletalMesh(Figure->GetSkeletalMeshAsset());
	HoverOutline->SetupAttachment(Figure);
	HoverOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoverOutline->SetCastShadow(false);
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
	const int32 Person = GetPersonSide();
	const bool bFriend = Battle->GetUnits()[Unit].Side == (Person != INDEX_NONE ? Person : 0);
	Instance->SetVectorParameterValue(TEXT("OutlineColor"), bFriend ? Config->OutlineFriendColor : Config->OutlineEnemyColor);
	Instance->SetScalarParameterValue(TEXT("OutlineWidth"), Config->OutlineWidth);
	Instance->SetScalarParameterValue(TEXT("OutlineWidthPerDistance"), Config->OutlineWidthPerDistance);
	for (int32 Slot = 0; Slot < FMath::Max(HoverOutline->GetNumMaterials(), 1); ++Slot)
	{
		HoverOutline->SetMaterial(Slot, Instance);
	}
	HoverOutline->RegisterComponent();
	HoverOutline->SetLeaderPoseComponent(Figure);
}

bool AHexBattleArena::GetUnitPoint(int32 Unit, FVector& OutWorld) const
{
	if (!Figures.IsValidIndex(Unit) || Figures[Unit].bDead)
	{
		return false;
	}
	OutWorld = Figures[Unit].Position;
	return true;
}

void AHexBattleArena::SetHover(const FVector& Origin, const FVector& Direction)
{
	bHover = false;
	if (!bShowing || !Config || FMath::IsNearlyZero(Direction.Z))
	{
		return;
	}
	const double Along = (GetActorLocation().Z - Origin.Z) / Direction.Z;
	if (Along <= 0.0)
	{
		return;
	}
	HoverPoint = Origin + Direction * Along;
	const FVector Local = HoverPoint - GetActorLocation();
	const FHexCoord Hex = FHexLayout(Config->HexRadius).WorldToHex(FVector2D(Local.X, Local.Y) + FieldCenter);
	if (GetBattle() && GetBattle()->IsOnField(Hex))
	{
		bHover = true;
		HoverHex = Hex;
	}
	// where the ground under the cursor is off the field, a part of the wall of a castle the cursor points at, its towers and the arch of its
	// gate, is the hex of that part, as the developer found the towers at the ends of the wall could not be struck, as they stand outside the
	// field; on the field the hex of the ground stays, as the wall in front of it dissolves (a pick there kept the wall whole under the cursor
	// and the hexes behind it out of reach, which the developer saw on 2026-11-13)
	double Nearest = Along;
	for (const FWallPick& Pick : bHover ? TArray<FWallPick>() : WallPicks)
	{
		const FVector2D From(Origin.X - Pick.Base.X, Origin.Y - Pick.Base.Y);
		const FVector2D Way(Direction.X, Direction.Y);
		const double A = Way.SizeSquared();
		const double B = 2.0 * FVector2D::DotProduct(From, Way);
		const double C = From.SizeSquared() - Pick.Radius * Pick.Radius;
		const double Discriminant = B * B - 4.0 * A * C;
		if (A <= 1.0e-8 || Discriminant < 0.0)
		{
			continue;
		}
		const double T = (-B - FMath::Sqrt(Discriminant)) / (2.0 * A);
		const double Z = Origin.Z + Direction.Z * T - Pick.Base.Z;
		if (T > 0.0 && T < Nearest && Z >= 0.0 && Z <= Pick.Height)
		{
			Nearest = T;
			bHover = true;
			HoverHex = Pick.Hex;
			HoverPoint = Origin + Direction * T;
		}
	}
}

int32 AHexBattleArena::GetHoverUnit() const
{
	const FHexBattle* Battle = GetBattle();
	return bHover && Battle ? Battle->FindUnitAt(HoverHex) : int32(INDEX_NONE);
}

int32 AHexBattleArena::GetPersonSide() const
{
	return Battles.IsValid() ? Battles->GetPersonSide() : INDEX_NONE;
}

bool AHexBattleArena::CanOrder() const
{
	const FHexBattle* Battle = GetBattle();
	if (!bShowing || !Battle || Battle->IsOver() || IsPlaying() || !Battle->GetUnits().IsValidIndex(Battle->GetActive()))
	{
		return false;
	}
	return !Battles->IsSideAi(Battle->GetUnits()[Battle->GetActive()].Side);
}

void AHexBattleArena::UpdateReach()
{
	FHexBattle* Battle = GetBattle();
	if (!Battle)
	{
		return;
	}
	if (ReachUnit != Battle->GetActive() || ReachRound != Battle->GetRound())
	{
		ReachUnit = Battle->GetActive();
		ReachRound = Battle->GetRound();
		Battle->FindReach(ReachUnit, Reach);
	}
}

bool AHexBattleArena::FindStrikeFrom(int32 Target, FHexCoord& OutFrom) const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle || !Battle->GetUnits().IsValidIndex(Target))
	{
		return false;
	}
	double Nearest = TNumericLimits<double>::Max();
	bool bFound = false;
	for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
	{
		const FHexCoord From = Battle->GetUnits()[Target].Hex.GetNeighbor(Direction);
		if (!Reach.Contains(From) || !Battle->CanStrike(Battle->GetActive(), Target, From))
		{
			continue;
		}
		const double Gap = FVector::DistSquared2D(GetHexPoint(From), HoverPoint);
		if (Gap < Nearest)
		{
			Nearest = Gap;
			OutFrom = From;
			bFound = true;
		}
	}
	return bFound;
}

void AHexBattleArena::Click()
{
	FHexBattle* Battle = GetBattle();
	if (!CanOrder() || !bHover)
	{
		return;
	}
	UpdateReach();
	const int32 Side = Battle->GetUnits()[Battle->GetActive()].Side;
	if (Aimed != INDEX_NONE)
	{
		if (Battle->CanCast(Side, Aimed, HoverHex))
		{
			Battle->Cast(Side, Aimed, HoverHex);
			Aimed = INDEX_NONE;
			ReachUnit = INDEX_NONE;
		}
		else
		{
			AddLog(TEXT("The spell cannot be cast there"));
		}
		return;
	}
	// the catapult shoots at the wall, a stack of the attacker strikes it from beside it, a tower too, though its shooters stand in it
	if (const FHexBattleWall* Wall = Battle->FindWall(HoverHex); Wall && Wall->IsStanding())
	{
		const int32 Inside = Battle->FindUnitAt(HoverHex);
		const bool bTower = Inside != INDEX_NONE && Battle->GetUnits()[Inside].bTower;
		FHexCoord WallFrom;
		if (Battle->CanFireCatapult(HoverHex))
		{
			Battle->FireCatapult(HoverHex);
			return;
		}
		if (FindWallStrikeFrom(HoverHex, WallFrom))
		{
			Battle->StrikeWall(HoverHex, WallFrom);
			return;
		}
		if (Inside == INDEX_NONE || bTower)
		{
			AddLog(TEXT("The wall is out of reach"));
			return;
		}
	}
	const int32 Unit = Battle->FindUnitAt(HoverHex);
	if (Unit != INDEX_NONE && Battle->GetUnits()[Unit].Side != Side)
	{
		FHexCoord From;
		if (Battle->CanShoot(Battle->GetActive(), Unit))
		{
			Battle->Shoot(Unit);
		}
		else if (FindStrikeFrom(Unit, From))
		{
			Battle->Strike(Unit, From);
		}
		else
		{
			AddLog(FString::Printf(TEXT("%s is out of reach"), *GetUnitName(Unit)));
		}
		return;
	}
	if (Unit == INDEX_NONE && Reach.Contains(HoverHex))
	{
		Battle->Move(HoverHex);
	}
}

void AHexBattleArena::Cancel()
{
	Aimed = INDEX_NONE;
}

void AHexBattleArena::AimSpell(int32 Spell)
{
	FHexBattle* Battle = GetBattle();
	if (!CanOrder())
	{
		return;
	}
	const int32 Side = Battle->GetUnits()[Battle->GetActive()].Side;
	Aimed = Battle->CanCastAny(Side) && Config->Spells.IsValidIndex(Spell) && Config->Spells[Spell].ManaCost <= Battle->GetMana(Side) ? Spell : INDEX_NONE;
}

void AHexBattleArena::OrderWait()
{
	if (CanOrder())
	{
		GetBattle()->Wait();
	}
}

void AHexBattleArena::OrderDefend()
{
	if (CanOrder())
	{
		GetBattle()->Defend();
	}
}

void AHexBattleArena::OrderAuto()
{
	const int32 Side = GetPersonSide();
	if (Battles.IsValid() && Side != INDEX_NONE && !GetBattle()->IsOver())
	{
		Battles->SetAuto(Side, !Battles->IsSideAi(Side));
		AddLog(Battles->IsSideAi(Side) ? TEXT("The game fights for you") : TEXT("You fight again"));
		Aimed = INDEX_NONE;
	}
}

void AHexBattleArena::OrderQuick()
{
	FHexBattle* Battle = GetBattle();
	if (!Battle || Battle->IsOver())
	{
		return;
	}
	Battles->PlayQuick();
	// what happened is not played: the field shows the end at once
	Battle->TakeEvents();
	Pending.Reset();
	Current.Reset();
	Shot->SetVisibility(false);
	PlaceFigures();
	AddLog(Battles->GetResultText());
}

void AHexBattleArena::OrderRetreat()
{
	const int32 Side = GetPersonSide();
	if (Side != INDEX_NONE && GetBattle() && !GetBattle()->IsOver())
	{
		GetBattle()->Retreat(Side);
	}
}

void AHexBattleArena::OrderClose()
{
	if (Battles.IsValid() && GetBattle() && GetBattle()->IsOver() && !IsPlaying())
	{
		Battles->Close();
	}
}

FString AHexBattleArena::GetUnitName(int32 Unit) const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle || !Battle->GetUnits().IsValidIndex(Unit))
	{
		return FString();
	}
	const FHexBattleUnit& Each = Battle->GetUnits()[Unit];
	return FString::Printf(TEXT("%s (%d)"), *Each.Stats.DisplayName, GetShownCount(Unit));
}

FString AHexBattleArena::GetHoverText() const
{
	const FHexBattle* Battle = GetBattle();
	const int32 Unit = GetHoverUnit();
	if (!Battle || Unit == INDEX_NONE)
	{
		return bHover && Battle && Battle->IsObstacle(HoverHex) ? FString(TEXT("An obstacle")) : FString();
	}
	const FHexBattleUnit& Each = Battle->GetUnits()[Unit];
	FString Text = FString::Printf(TEXT("%d %s, %s\nAttack %d, Defense %d, Damage %d-%d\nHealth %d (the top one %d), Speed %d"), Each.Count, *Each.Stats.DisplayName,
		*Battles->GetSideName(Each.Side), Battle->GetAttack(Each), Battle->GetDefense(Each), Each.Stats.DamageMin, Each.Stats.DamageMax, Each.Stats.Health, Each.TopHealth,
		Battle->GetSpeed(Each));
	if (Each.bShoots)
	{
		Text += FString::Printf(TEXT(", shots %d"), Each.ShotsLeft);
	}
	if (Each.bFlies)
	{
		Text += TEXT(", flies");
	}
	Text += FString::Printf(TEXT("\nMorale %+d, luck %+d"), Each.Morale, Each.Luck);
	const UEnum* Abilities = StaticEnum<EHexAbility>();
	FString Can;
	for (int32 Index = 0; Abilities && Index < Abilities->NumEnums() - 1; ++Index)
	{
		if (Each.Stats.Abilities & (1 << Index))
		{
			Can += (Can.IsEmpty() ? TEXT("") : TEXT(", ")) + Abilities->GetDisplayNameTextByIndex(Index).ToString();
		}
	}
	if (!Can.IsEmpty())
	{
		Text += TEXT("\n") + Can;
	}
	for (const FHexBattleEffect& Effect : Each.Effects)
	{
		if (Config->Spells.IsValidIndex(Effect.Spell))
		{
			Text += FString::Printf(TEXT("\n%s, %d rounds"), *Config->Spells[Effect.Spell].DisplayName, Effect.RoundsLeft);
		}
	}
	return Text;
}

FString AHexBattleArena::GetTurnText() const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle || Battle->IsOver() || !Battle->GetUnits().IsValidIndex(Battle->GetActive()))
	{
		return FString();
	}
	const FHexBattleUnit& Active = Battle->GetUnits()[Battle->GetActive()];
	if (!CanOrder())
	{
		return IsPlaying() ? FString() : FString::Printf(TEXT("%s acts"), *Battles->GetSideName(Active.Side));
	}
	return FString::Printf(TEXT("Turn of %s"), *GetUnitName(Battle->GetActive()));
}

FString AHexBattleArena::GetActionText() const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle)
	{
		return FString();
	}
	if (Battle->IsOver())
	{
		return IsPlaying() ? FString() : Battles->GetResultText();
	}
	if (!Battle->GetUnits().IsValidIndex(Battle->GetActive()) || !CanOrder() || !bHover)
	{
		return FString();
	}
	const int32 ActiveIndex = Battle->GetActive();
	const FHexBattleUnit& Active = Battle->GetUnits()[ActiveIndex];
	const auto Range = [](int32 Min, int32 Max) { return Min == Max ? FString::FromInt(Min) : FString::Printf(TEXT("%d-%d"), Min, Max); };
	const int32 Unit = Battle->FindUnitAt(HoverHex);
	if (Aimed != INDEX_NONE && Config->Spells.IsValidIndex(Aimed))
	{
		const FHexSpell& Spell = Config->Spells[Aimed];
		if (!Battle->CanCast(Active.Side, Aimed, HoverHex))
		{
			return FString::Printf(TEXT("%s cannot be cast here"), *Spell.DisplayName);
		}
		const FHexSpellPreview Preview = Battle->PreviewSpell(Active.Side, Aimed, HoverHex);
		const FString On = Unit != INDEX_NONE ? FString::Printf(TEXT(" on %s"), *GetUnitName(Unit)) : FString();
		if (Preview.Damage > 0)
		{
			return FString::Printf(TEXT("Cast %s%s: %d damage to %d %s, kills %d"), *Spell.DisplayName, *On, Preview.Damage, Preview.Units, Preview.Units == 1 ? TEXT("stack") : TEXT("stacks"),
				Preview.Kills);
		}
		if (Spell.Kind == EHexSpellKind::Heal)
		{
			return FString::Printf(TEXT("Cast %s%s: heals %d"), *Spell.DisplayName, *On, Preview.Heal);
		}
		return FString::Printf(TEXT("Cast %s%s, %d mana"), *Spell.DisplayName, *On, Spell.ManaCost);
	}
	if (const FHexBattleWall* Wall = Battle->FindWall(HoverHex); Wall && Wall->IsStanding() && (Unit == INDEX_NONE || Battle->GetUnits()[Unit].bTower))
	{
		const FString Part = Wall->Part == EHexWallPart::Gate ? TEXT("gate") : Wall->Part == EHexWallPart::Tower ? TEXT("tower") : TEXT("wall");
		FHexCoord From;
		if (Battle->CanFireCatapult(HoverHex))
		{
			return FString::Printf(TEXT("Catapult: fire at the %s (%d of %d), %d-%d damage, %.0f%% to hit"), *Part, Wall->Health, Wall->MaxHealth, Config->CatapultDamageMin, Config->CatapultDamageMax,
				Config->CatapultHitChance * 100.0f);
		}
		if (FindWallStrikeFrom(HoverHex, From))
		{
			int32 Min = 0;
			int32 Max = 0;
			Battle->PreviewWallStrike(ActiveIndex, Min, Max);
			return FString::Printf(TEXT("Strike the %s (%d of %d): %s damage%s"), *Part, Wall->Health, Wall->MaxHealth, *Range(Min, Max), Min >= Wall->Health ? TEXT(". It falls") : TEXT(""));
		}
		if (Wall->Part == EHexWallPart::Gate && Reach.Contains(HoverHex))
		{
			return TEXT("Walk through the gate");
		}
		return FString::Printf(TEXT("The %s, %d of %d: %s"), *Part, Wall->Health, Wall->MaxHealth,
			Active.Side == 0 ? (Active.bShoots ? TEXT("shooters cannot harm it") : TEXT("out of reach")) : TEXT("your own"));
	}
	if (Unit != INDEX_NONE && Battle->GetUnits()[Unit].Side != Active.Side)
	{
		FHexCoord From;
		const bool bShot = Battle->CanShoot(ActiveIndex, Unit);
		if (Battle->GetUnits()[Unit].bTower)
		{
			return TEXT("The shooters of a tower: only a catapult harms them");
		}
		if (!bShot && !FindStrikeFrom(Unit, From))
		{
			return FString::Printf(TEXT("%s is out of reach"), *GetUnitName(Unit));
		}
		const int32 Run = bShot ? 0 : FMath::Max(Reach.FindRef(From), 0);
		const FHexStrikePreview Preview = Battle->PreviewStrike(ActiveIndex, Unit, bShot, Run);
		FString Text = FString::Printf(TEXT("%s %s: %s damage, kills %s"), bShot ? TEXT("Shoot") : TEXT("Strike"), *GetUnitName(Unit), *Range(Preview.DamageMin, Preview.DamageMax),
			*Range(Preview.KillsMin, Preview.KillsMax));
		if (Preview.bFar)
		{
			Text += TEXT(" (far: half damage)");
		}
		if (Preview.bMeleePenalty)
		{
			Text += TEXT(" (a shooter in melee: half damage)");
		}
		if (Preview.KillsMin >= Battle->GetUnits()[Unit].Count)
		{
			Text += TEXT(". It dies");
		}
		else if (Preview.bStrikesBack)
		{
			Text += FString::Printf(TEXT(". It strikes back: %s damage, kills %s"), *Range(Preview.BackDamageMin, Preview.BackDamageMax), *Range(Preview.BackKillsMin, Preview.BackKillsMax));
		}
		else if (!bShot)
		{
			Text += TEXT(". No strike back");
		}
		return Text;
	}
	if (Unit == INDEX_NONE && Reach.Contains(HoverHex) && HoverHex != Active.Hex)
	{
		const int32 Hexes = Reach.FindRef(HoverHex);
		return FString::Printf(TEXT("%s here, %d %s%s"), Active.bFlies ? TEXT("Fly") : TEXT("Walk"), Hexes, Hexes == 1 ? TEXT("hex") : TEXT("hexes"),
			!Active.bFlies && Battle->IsMoat(HoverHex) ? TEXT(": into the moat, where a step costs double") : TEXT(""));
	}
	if (Battle->IsObstacle(HoverHex))
	{
		return TEXT("An obstacle");
	}
	if (Unit == INDEX_NONE && HoverHex != Active.Hex)
	{
		return FString::Printf(TEXT("Out of reach: %s goes %d hexes"), *Active.Stats.DisplayName, Battle->GetSpeed(Active));
	}
	return FString();
}

EHexCursor AHexBattleArena::GetCursor(FVector& OutFrom, FVector& OutTo) const
{
	const FHexBattle* Battle = GetBattle();
	if (!Battle || !bHover || !CanOrder())
	{
		return EHexCursor::Default;
	}
	const int32 ActiveIndex = Battle->GetActive();
	const FHexBattleUnit& Active = Battle->GetUnits()[ActiveIndex];
	if (Aimed != INDEX_NONE)
	{
		return Battle->CanCast(Active.Side, Aimed, HoverHex) ? EHexCursor::BattleCast : EHexCursor::BattleBlocked;
	}
	const int32 InWall = Battle->FindUnitAt(HoverHex);
	if (const FHexBattleWall* Wall = Battle->FindWall(HoverHex); Wall && Wall->IsStanding() && (InWall == INDEX_NONE || Battle->GetUnits()[InWall].bTower))
	{
		FHexCoord From;
		if (Battle->CanFireCatapult(HoverHex))
		{
			return EHexCursor::BattleShoot;
		}
		if (FindWallStrikeFrom(HoverHex, From))
		{
			OutFrom = GetHexPoint(From);
			OutTo = GetHexPoint(HoverHex);
			return EHexCursor::BattleStrike;
		}
		// the gate a defender goes through
		return Wall->Part == EHexWallPart::Gate && Reach.Contains(HoverHex) ? EHexCursor::BattleWalk : EHexCursor::BattleBlocked;
	}
	const int32 Unit = Battle->FindUnitAt(HoverHex);
	if (Unit != INDEX_NONE)
	{
		if (Battle->GetUnits()[Unit].Side == Active.Side)
		{
			return EHexCursor::Default;
		}
		if (Battle->CanShoot(ActiveIndex, Unit))
		{
			return Battle->PreviewStrike(ActiveIndex, Unit, true, 0).bFar ? EHexCursor::BattleShootFar : EHexCursor::BattleShoot;
		}
		FHexCoord From;
		if (FindStrikeFrom(Unit, From))
		{
			OutFrom = GetHexPoint(From);
			OutTo = GetHexPoint(Battle->GetUnits()[Unit].Hex);
			return EHexCursor::BattleStrike;
		}
		return EHexCursor::BattleBlocked;
	}
	if (HoverHex == Active.Hex)
	{
		return EHexCursor::Default;
	}
	if (Reach.Contains(HoverHex))
	{
		return Active.bFlies ? EHexCursor::BattleFly : EHexCursor::BattleWalk;
	}
	return EHexCursor::BattleBlocked;
}

void AHexBattleArena::AddPopup(const FVector& World, const FString& Text, const FLinearColor& Color, float Life)
{
	FHexBattlePopup& Popup = Popups.AddDefaulted_GetRef();
	Popup.World = World + FVector(0.0, 0.0, Config ? Config->HexRadius * 1.4 : 60.0);
	Popup.Text = Text;
	Popup.Color = Color;
	Popup.Life = Life;
}

void AHexBattleArena::AddLog(const FString& Line)
{
	if (Line.IsEmpty())
	{
		return;
	}
	Log.Add(Line);
	if (Log.Num() > 8)
	{
		Log.RemoveAt(0, Log.Num() - 8);
	}
	UE_LOG(LogHexHero, Log, TEXT("Battle: %s"), *Line);
}

void AHexBattleArena::Gather()
{
	if (FHexBattle* Battle = GetBattle())
	{
		Pending.Append(Battle->TakeEvents());
	}
}

void AHexBattleArena::BeginEvent(const FHexBattleEvent& Event)
{
	FHexBattle* Battle = GetBattle();
	Current = Event;
	CurrentTime = 0.0f;
	CurrentLength = 0.0f;
	const auto Figure = [this](int32 Unit) { return Figures.IsValidIndex(Unit) ? Figures[Unit].Component.Get() : nullptr; };
	const auto Face = [this](int32 Unit, const FVector& Towards)
	{
		if (Figures.IsValidIndex(Unit))
		{
			const FVector Way = (Towards - Figures[Unit].Position).GetSafeNormal2D();
			if (!Way.IsNearlyZero())
			{
				Figures[Unit].Facing = Way;
				PlaceFigure(Unit);
			}
		}
	};
	const FString Who = GetUnitName(Event.Unit);
	// the camera shows both the one that strikes or shoots and the one struck
	if ((Event.Type == FHexBattleEvent::EType::Strike || Event.Type == FHexBattleEvent::EType::Shoot || Event.Type == FHexBattleEvent::EType::Retaliate)
		&& Figures.IsValidIndex(Event.Unit) && Figures.IsValidIndex(Event.Target))
	{
		FrameCamera(Figures[Event.Unit].Position, Figures[Event.Target].Position);
	}
	switch (Event.Type)
	{
	case FHexBattleEvent::EType::RoundBegins:
		AddLog(FString::Printf(TEXT("Round %d"), Event.Amount));
		break;
	case FHexBattleEvent::EType::Move:
		if (Figures.IsValidIndex(Event.Unit) && Event.Path.Num() >= 2)
		{
			const bool bFlies = Battle->GetUnits()[Event.Unit].bFlies;
			CurrentLength = bFlies ? Event.Path[0].DistanceTo(Event.Path.Last()) / Config->FlySpeed : (Event.Path.Num() - 1) / Config->WalkSpeed;
			HexUnitFigure::Play(Figure(Event.Unit), Config, Figures[Event.Unit].Creature, EHexFigureAction::Move);
		}
		break;
	case FHexBattleEvent::EType::Strike:
	case FHexBattleEvent::EType::Retaliate:
		if (Event.Target == INDEX_NONE && Figures.IsValidIndex(Event.Unit) && Battle->FindWall(Event.Hex))
		{
			// at the wall
			Face(Event.Unit, GetHexPoint(Event.Hex));
			HexUnitFigure::Play(Figure(Event.Unit), Config, Figures[Event.Unit].Creature, EHexFigureAction::Attack);
			CurrentLength = Config->StrikeTime;
			AddLog(FString::Printf(TEXT("%s strikes the %s"), *Who, Battle->FindWall(Event.Hex)->Part == EHexWallPart::Gate ? TEXT("gate") : TEXT("wall")));
		}
		else if (Figures.IsValidIndex(Event.Target))
		{
			Face(Event.Unit, Figures[Event.Target].Position);
			HexUnitFigure::Play(Figure(Event.Unit), Config, Figures[Event.Unit].Creature, EHexFigureAction::Attack);
			CurrentLength = Config->StrikeTime;
			AddLog(FString::Printf(TEXT("%s %s %s"), *Who, Event.Type == FHexBattleEvent::EType::Retaliate ? TEXT("strikes back at") : TEXT("strikes"), *GetUnitName(Event.Target)));
		}
		break;
	case FHexBattleEvent::EType::Shoot:
		if (Figures.IsValidIndex(Event.Target) && Figures.IsValidIndex(Event.Unit))
		{
			Face(Event.Unit, Figures[Event.Target].Position);
			HexUnitFigure::Play(Figure(Event.Unit), Config, Figures[Event.Unit].Creature, EHexFigureAction::Shoot);
			const double Hexes = FVector::Dist(Figures[Event.Unit].Position, Figures[Event.Target].Position) / (Config->HexRadius * 1.73);
			CurrentLength = 0.3f + float(Hexes) / Config->ShotSpeed;
			AddLog(FString::Printf(TEXT("%s shoots %s"), *Who, *GetUnitName(Event.Target)));
		}
		break;
	case FHexBattleEvent::EType::Damage:
		if (ShownCounts.IsValidIndex(Event.Target))
		{
			if (Event.Killed > 0)
			{
				AddLog(FString::Printf(TEXT("%s loses %d"), *GetUnitName(Event.Target), Event.Killed));
			}
			ShownCounts[Event.Target] = Event.CountAfter >= 0 ? Event.CountAfter : ShownCounts[Event.Target] - Event.Killed;
			AddPopup(Figures[Event.Target].Position, Event.Killed > 0 ? FString::Printf(TEXT("-%d  (%d killed)"), Event.Amount, Event.Killed) : FString::Printf(TEXT("-%d"), Event.Amount),
				FLinearColor(1.0f, 0.35f, 0.25f, 1.0f));
			if (ShownCounts[Event.Target] > 0)
			{
				HexUnitFigure::Play(Figure(Event.Target), Config, Figures[Event.Target].Creature, EHexFigureAction::Hit);
			}
			CurrentLength = 0.25f;
		}
		break;
	case FHexBattleEvent::EType::Death:
		if (Figures.IsValidIndex(Event.Unit))
		{
			HexUnitFigure::Play(Figure(Event.Unit), Config, Figures[Event.Unit].Creature, EHexFigureAction::Death);
			CurrentLength = FMath::Max(Config->DeathTime, HexUnitFigure::GetLength(Config, Figures[Event.Unit].Creature, EHexFigureAction::Death) * 0.9f);
			AddLog(FString::Printf(TEXT("%s are all dead"), *Battle->GetUnits()[Event.Unit].Stats.DisplayName));
		}
		break;
	case FHexBattleEvent::EType::Wait:
		AddPopup(Figures.IsValidIndex(Event.Unit) ? Figures[Event.Unit].Position : GetActorLocation(), TEXT("Waits"), FLinearColor(0.8f, 0.8f, 1.0f, 1.0f), 0.8f);
		CurrentLength = 0.2f;
		break;
	case FHexBattleEvent::EType::Defend:
		AddPopup(Figures.IsValidIndex(Event.Unit) ? Figures[Event.Unit].Position : GetActorLocation(), TEXT("Defends"), FLinearColor(0.7f, 0.9f, 1.0f, 1.0f), 0.8f);
		AddLog(FString::Printf(TEXT("%s defends"), *Who));
		CurrentLength = 0.25f;
		break;
	case FHexBattleEvent::EType::Spell:
		if (Config->Spells.IsValidIndex(Event.Spell))
		{
			const FHexSpell& Spell = Config->Spells[Event.Spell];
			SpellLight->SetWorldLocation(GetHexPoint(Event.Hex) + FVector(0.0, 0.0, Config->HexRadius * 1.5));
			SpellLight->SetLightColor(Spell.Color);
			SpellLight->SetAttenuationRadius(Config->HexRadius * (3.0f + 2.0f * Spell.Radius));
			AddPopup(GetHexPoint(Event.Hex), Spell.DisplayName, Spell.Color, 1.4f);
			AddLog(FString::Printf(TEXT("%s casts %s"), *Battles->GetSideName(Event.Side), *Spell.DisplayName));
			CurrentLength = 0.8f;
		}
		break;
	case FHexBattleEvent::EType::Heal:
	case FHexBattleEvent::EType::Regenerate:
		if (Figures.IsValidIndex(Event.Unit))
		{
			const int32 Healed = Event.Target != INDEX_NONE ? Event.Target : Event.Unit;
			if (ShownCounts.IsValidIndex(Healed) && Event.CountAfter >= 0)
			{
				ShownCounts[Healed] = Event.CountAfter;
			}
			AddPopup(Figures[Healed].Position, FString::Printf(TEXT("+%d"), Event.Amount), FLinearColor(0.4f, 1.0f, 0.5f, 1.0f), 0.9f);
			CurrentLength = 0.2f;
		}
		break;
	case FHexBattleEvent::EType::Morale:
		AddPopup(Figures.IsValidIndex(Event.Unit) ? Figures[Event.Unit].Position : GetActorLocation(), TEXT("Morale! Another turn"), FLinearColor(1.0f, 0.9f, 0.3f, 1.0f), 1.1f);
		AddLog(FString::Printf(TEXT("%s takes another turn of good morale"), *Who));
		CurrentLength = 0.45f;
		break;
	case FHexBattleEvent::EType::BadMorale:
		AddPopup(Figures.IsValidIndex(Event.Unit) ? Figures[Event.Unit].Position : GetActorLocation(), TEXT("Freezes"), FLinearColor(0.6f, 0.6f, 1.0f, 1.0f), 1.1f);
		AddLog(FString::Printf(TEXT("%s loses its turn to bad morale"), *Who));
		CurrentLength = 0.45f;
		break;
	case FHexBattleEvent::EType::Luck:
		AddPopup(Figures.IsValidIndex(Event.Unit) ? Figures[Event.Unit].Position : GetActorLocation(), TEXT("Luck! Double damage"), FLinearColor(0.4f, 1.0f, 0.9f, 1.0f), 1.1f);
		CurrentLength = 0.3f;
		break;
	case FHexBattleEvent::EType::ManaDrain:
		AddPopup(Figures.IsValidIndex(Event.Unit) ? Figures[Event.Unit].Position : GetActorLocation(), FString::Printf(TEXT("-%d mana"), Event.Amount), FLinearColor(0.5f, 0.6f, 1.0f, 1.0f));
		CurrentLength = 0.2f;
		break;
	case FHexBattleEvent::EType::Retreat:
		AddLog(FString::Printf(TEXT("%s retreats"), *Battles->GetSideName(Event.Side)));
		CurrentLength = 0.5f;
		break;
	case FHexBattleEvent::EType::CatapultShot:
		if (Figures.IsValidIndex(Event.Unit))
		{
			const double Hexes = FVector::Dist(Figures[Event.Unit].Position, GetHexPoint(Event.Hex)) / (Config->HexRadius * 1.73);
			CurrentLength = 0.3f + float(Hexes) / Config->ShotSpeed * 1.5f;
			AddLog(Event.Amount > 0 ? TEXT("The catapult hits the wall") : TEXT("The catapult misses"));
			if (Event.Amount <= 0)
			{
				AddPopup(GetHexPoint(Event.Hex) + FVector(0.0, 0.0, Config->SiegeWallHeight), TEXT("Miss"), FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), 0.9f);
			}
		}
		break;
	case FHexBattleEvent::EType::WallHit:
		if (const FHexBattleWall* Wall = Battle->FindWall(Event.Hex))
		{
			const TCHAR* Part = Wall->Part == EHexWallPart::Gate ? TEXT("gate") : Wall->Part == EHexWallPart::Tower ? TEXT("tower") : TEXT("wall");
			AddPopup(GetHexPoint(Event.Hex) + FVector(0.0, 0.0, Config->SiegeWallHeight * 0.6), FString::Printf(TEXT("-%d"), Event.Amount), FLinearColor(1.0f, 0.75f, 0.3f, 1.0f));
			if (Event.Killed > 0)
			{
				AddLog(FString::Printf(TEXT("The %s falls"), Part));
				// what fell shows as rubble now, while the rules have it fallen already
				SiegeKey.Empty();
			}
			CurrentLength = Event.Killed > 0 ? 0.6f : 0.25f;
		}
		break;
	case FHexBattleEvent::EType::End:
		AddLog(Battles->GetResultText());
		CurrentLength = 0.5f;
		break;
	default:
		break;
	}
}

void AHexBattleArena::PlayEvent(float DeltaSeconds)
{
	if (!Current.IsSet())
	{
		return;
	}
	const FHexBattleEvent& Event = Current.GetValue();
	CurrentTime += DeltaSeconds;
	const float Alpha = CurrentLength > 0.0f ? FMath::Clamp(CurrentTime / CurrentLength, 0.0f, 1.0f) : 1.0f;
	if (Event.Type == FHexBattleEvent::EType::Move && Figures.IsValidIndex(Event.Unit) && Event.Path.Num() >= 2)
	{
		FFigure& Figure = Figures[Event.Unit];
		const bool bFlies = GetBattle()->GetUnits()[Event.Unit].bFlies;
		if (bFlies)
		{
			const FVector From = GetHexPoint(Event.Path[0]);
			const FVector To = GetHexPoint(Event.Path.Last());
			const float Eased = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
			Figure.Position = FMath::Lerp(From, To, double(Eased)) + FVector(0.0, 0.0, FMath::Sin(Alpha * PI) * Config->HexRadius * 1.2);
			Figure.Facing = To - From;
		}
		else
		{
			const float Along = Alpha * (Event.Path.Num() - 1);
			const int32 Segment = FMath::Min(FMath::FloorToInt32(Along), Event.Path.Num() - 2);
			const FVector From = GetHexPoint(Event.Path[Segment]);
			const FVector To = GetHexPoint(Event.Path[Segment + 1]);
			Figure.Position = FMath::Lerp(From, To, double(Along - Segment));
			Figure.Facing = To - From;
		}
		PlaceFigure(Event.Unit);
	}
	else if (Event.Type == FHexBattleEvent::EType::Shoot && Figures.IsValidIndex(Event.Unit) && Figures.IsValidIndex(Event.Target))
	{
		const float Flight = FMath::Clamp((CurrentTime - 0.3f) / FMath::Max(CurrentLength - 0.3f, 0.01f), 0.0f, 1.0f);
		Shot->SetVisibility(CurrentTime >= 0.3f && Flight < 1.0f);
		const FVector Lift(0.0, 0.0, Config->HexRadius * 0.9);
		Shot->SetWorldLocation(FMath::Lerp(Figures[Event.Unit].Position, Figures[Event.Target].Position, double(Flight)) + Lift + FVector(0.0, 0.0, FMath::Sin(Flight * PI) * Config->HexRadius));
	}
	else if (Event.Type == FHexBattleEvent::EType::CatapultShot && Figures.IsValidIndex(Event.Unit))
	{
		// a stone in a high arc
		const float Flight = FMath::Clamp((CurrentTime - 0.3f) / FMath::Max(CurrentLength - 0.3f, 0.01f), 0.0f, 1.0f);
		Shot->SetVisibility(CurrentTime >= 0.3f && Flight < 1.0f);
		const FVector From = Figures[Event.Unit].Position + FVector(0.0, 0.0, Config->CatapultSize);
		const FVector To = GetHexPoint(Event.Hex) + FVector(0.0, 0.0, Config->SiegeWallHeight * 0.6);
		Shot->SetWorldLocation(FMath::Lerp(From, To, double(Flight)) + FVector(0.0, 0.0, FMath::Sin(Flight * PI) * Config->HexRadius * 5.0));
	}
	else if (Event.Type == FHexBattleEvent::EType::Spell)
	{
		SpellLight->SetIntensity(FMath::Sin(Alpha * PI) * 60000.0f);
	}
	if (CurrentTime >= CurrentLength)
	{
		EndEvent();
	}
}

void AHexBattleArena::EndEvent()
{
	if (!Current.IsSet())
	{
		return;
	}
	const FHexBattleEvent Event = Current.GetValue();
	Current.Reset();
	switch (Event.Type)
	{
	case FHexBattleEvent::EType::Move:
		if (Figures.IsValidIndex(Event.Unit) && !Event.Path.IsEmpty())
		{
			Figures[Event.Unit].Position = GetHexPoint(Event.Path.Last());
			// it faces the other side again once it stands
			Figures[Event.Unit].Facing = GetBattle()->GetUnits()[Event.Unit].Side == 0 ? FVector(0.0, 1.0, 0.0) : FVector(0.0, -1.0, 0.0);
			PlaceFigure(Event.Unit);
			HexUnitFigure::Play(Figures[Event.Unit].Component.Get(), Config, Figures[Event.Unit].Creature, EHexFigureAction::Idle);
		}
		break;
	case FHexBattleEvent::EType::Shoot:
	case FHexBattleEvent::EType::CatapultShot:
		Shot->SetVisibility(false);
		break;
	case FHexBattleEvent::EType::Death:
		if (Figures.IsValidIndex(Event.Unit))
		{
			Figures[Event.Unit].bDead = true;
			// it stays as a corpse in the last pose of its death, which a creature without one has not
			const FHexUnitLook* Look = Config->FindLook(Figures[Event.Unit].Creature);
			if (Figures[Event.Unit].Component.IsValid() && (!Look || Look->Death.Sequence.IsNull()))
			{
				Figures[Event.Unit].Component->SetVisibility(false, true);
			}
			// a flyer falls to the ground
			if (Look && Look->Hover > 0.0f)
			{
				Figures[Event.Unit].Hover -= Look->Hover * Config->UnitScale;
				PlaceFigure(Event.Unit);
			}
		}
		break;
	case FHexBattleEvent::EType::Spell:
		SpellLight->SetIntensity(0.0f);
		break;
	default:
		break;
	}
}

void AHexBattleArena::UpdateMarks()
{
	FHexBattle* Battle = GetBattle();
	if (!Battle)
	{
		return;
	}
	const bool bOrder = CanOrder();
	if (bOrder)
	{
		UpdateReach();
	}
	const int32 Active = Battle->GetActive();
	const int32 HoverUnit = GetHoverUnit();
	FHexCoord From;
	const bool bStrike = bOrder && Aimed == INDEX_NONE && HoverUnit != INDEX_NONE && Battle->GetUnits()[HoverUnit].Side != Battle->GetUnits()[Active].Side
		&& !Battle->CanShoot(Active, HoverUnit) && FindStrikeFrom(HoverUnit, From);
	const FString Key = FString::Printf(TEXT("%d %d %d %d %d %d %d %d %d %d"), bOrder ? 1 : 0, Active, Battle->GetRound(), bHover ? HoverHex.Q : -99, bHover ? HoverHex.R : -99, Aimed,
		bStrike ? From.Q : -99, bStrike ? From.R : -99, IsPlaying() ? 1 : 0, Battle->IsOver() ? 1 : 0);
	if (Key == MarksKey)
	{
		return;
	}
	MarksKey = Key;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;
	// what is drawn later lies over what came before, as the marks are one translucent section drawn in the order of its triangles
	const auto Local = [this](const FHexCoord& Hex, double Lift) { return GetHexPoint(Hex) - GetActorLocation() + FVector(0.0, 0.0, Lift); };
	const auto Fill = [&](const FHexCoord& Hex, const FLinearColor& Color, double Inset, double Lift)
	{
		const FVector Middle = Local(Hex, Lift);
		const int32 Base = Vertices.Num();
		Vertices.Add(Middle);
		Normals.Add(FVector::UpVector);
		Colors.Add(Color);
		for (int32 Corner = 0; Corner < 6; ++Corner)
		{
			const FVector2D Direction = FHexLayout::GetCornerDirection(Corner);
			Vertices.Add(Middle + FVector(Direction.X, Direction.Y, 0.0) * Config->HexRadius * Inset);
			Normals.Add(FVector::UpVector);
			Colors.Add(Color);
		}
		for (int32 Corner = 0; Corner < 6; ++Corner)
		{
			Triangles.Append({Base, Base + 1 + (Corner + 1) % 6, Base + 1 + Corner});
		}
	};
	// a quad from A to B and Across from them, its color Near at A and B and Far across
	const auto Quad = [&](const FVector& A, const FVector& B, const FVector& Across, const FLinearColor& Near, const FLinearColor& Far)
	{
		const int32 Base = Vertices.Num();
		Vertices.Append({A, B, B + Across, A + Across});
		Normals.Append({FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector});
		Colors.Append({Near, Near, Far, Far});
		// both windings, so it shows whichever way Across points
		Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3, Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
	};
	const auto Segment = [&](const FVector& A, const FVector& B, double Width, const FLinearColor& Color)
	{
		const FVector Along = (B - A).GetSafeNormal2D();
		const FVector Side = FVector(-Along.Y, Along.X, 0.0) * Width;
		Quad(A - Along * Width * 0.5 - Side * 0.5, B + Along * Width * 0.5 - Side * 0.5, Side, Color, Color);
	};
	const auto Disc = [&](const FVector& Middle, double Inner, double Outer, const FLinearColor& Color)
	{
		const int32 Steps = 20;
		const int32 Base = Vertices.Num();
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const double Angle = UE_TWO_PI * Step / Steps;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			Vertices.Append({Middle + Out * Inner, Middle + Out * Outer});
			Normals.Append({FVector::UpVector, FVector::UpVector});
			Colors.Append({Color, Color});
		}
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const int32 A = Base + Step * 2;
			const int32 B = Base + ((Step + 1) % Steps) * 2;
			Triangles.Append({A, B + 1, A + 1, A, B, B + 1, A, A + 1, B + 1, A, B + 1, B});
		}
	};
	if (Battle->GetUnits().IsValidIndex(Active) && !Battle->IsOver())
	{
		const FHexBattleUnit& Unit = Battle->GetUnits()[Active];
		if (bOrder && Aimed == INDEX_NONE)
		{
			TSet<FHexCoord> Reached;
			TSet<FHexCoord> Targets;
			Reached.Add(Unit.Hex);
			for (const TPair<FHexCoord, int32>& Each : Reach)
			{
				Reached.Add(Each.Key);
			}
			for (const FHexBattleWall& Wall : Battle->GetWalls())
			{
				FHexCoord WallFrom;
				if (Wall.IsStanding() && (Battle->CanFireCatapult(Wall.Hex) || FindWallStrikeFrom(Wall.Hex, WallFrom)))
				{
					Targets.Add(Wall.Hex);
				}
			}
			// the enemies it can strike or shoot
			for (int32 Index = 0; Index < Battle->GetUnits().Num(); ++Index)
			{
				const FHexBattleUnit& Other = Battle->GetUnits()[Index];
				FHexCoord Any;
				if (Other.IsAlive() && Other.Side != Unit.Side && (Battle->CanShoot(Active, Index) || FindStrikeFrom(Index, Any)))
				{
					Targets.Add(Other.Hex);
				}
			}
			// the field it does not reach lies in the dark, as the ground outside the reach of a hero does on the planet
			for (int32 Column = 0; Column < Battle->GetColumns(); ++Column)
			{
				for (int32 Row = 0; Row < Battle->GetRows(); ++Row)
				{
					const FHexCoord Hex = FHexCoord::FromOffset(Column, Row);
					if (!Reached.Contains(Hex) && !Targets.Contains(Hex))
					{
						Fill(Hex, Config->ReachOutsideColor, 1.0, 0.7);
					}
				}
			}
			for (const FHexCoord& Hex : Reached)
			{
				if (Hex != Unit.Hex)
				{
					Fill(Hex, Config->ReachColor, 0.92, 0.75);
				}
			}
			// the edge of what it reaches: a light that fades inwards and a line along the sides towards what it does not
			const FHexLayout Layout(Config->HexRadius);
			for (const FHexCoord& Hex : Reached)
			{
				const FVector Middle = Local(Hex, 0.85);
				for (int32 Corner = 0; Corner < 6; ++Corner)
				{
					const FVector2D A2 = FHexLayout::GetCornerDirection(Corner);
					const FVector2D B2 = FHexLayout::GetCornerDirection(Corner + 1);
					const FHexCoord Across = Layout.WorldToHex(Layout.HexToWorld(Hex) + (A2 + B2) * Config->HexRadius);
					if (Reached.Contains(Across))
					{
						continue;
					}
					const FVector A = Middle + FVector(A2.X, A2.Y, 0.0) * Config->HexRadius;
					const FVector B = Middle + FVector(B2.X, B2.Y, 0.0) * Config->HexRadius;
					const FVector Inward = (Middle - (A + B) * 0.5).GetSafeNormal2D();
					FLinearColor Clear = Config->ReachGlowColor;
					Clear.A = 0.0f;
					Quad(A, B, Inward * Config->ReachGlowWidth, Config->ReachGlowColor, Clear);
					Segment(A + FVector(0.0, 0.0, 0.05), B + FVector(0.0, 0.0, 0.05), Config->ReachLineWidth, Config->ReachLineColor);
				}
			}
			for (const FHexCoord& Hex : Targets)
			{
				Fill(Hex, Config->TargetColor * FLinearColor(1.0f, 1.0f, 1.0f, 0.6f), 0.9, 0.9);
			}
		}
		Fill(Unit.Hex, Config->ActiveColor, 0.92, 0.95);
	}
	if (bHover && bOrder)
	{
		const FHexBattleUnit& Unit = Battle->GetUnits()[Active];
		if (Aimed != INDEX_NONE && Config->Spells.IsValidIndex(Aimed))
		{
			const FHexSpell& Spell = Config->Spells[Aimed];
			FLinearColor Color = Spell.Color;
			Color.A = Battle->CanCast(Unit.Side, Aimed, HoverHex) ? 0.45f : 0.15f;
			for (int32 Column = 0; Column < Battle->GetColumns(); ++Column)
			{
				for (int32 Row = 0; Row < Battle->GetRows(); ++Row)
				{
					const FHexCoord Hex = FHexCoord::FromOffset(Column, Row);
					if (Hex.DistanceTo(HoverHex) <= Spell.Radius)
					{
						Fill(Hex, Color, 0.95, 1.0);
					}
				}
			}
		}
		else
		{
			const bool bEnemy = HoverUnit != INDEX_NONE && Battle->GetUnits()[HoverUnit].Side != Unit.Side;
			Fill(HoverHex, bEnemy ? Config->TargetColor : Config->HoverColor * FLinearColor(1.0f, 1.0f, 1.0f, 0.5f), 0.95, 1.0);
			// the way there, as a way shows on the planet: a line through a dot on each hex to a ring on the last, straight for a flyer
			const bool bWalkTo = HoverUnit == INDEX_NONE && Reach.Contains(HoverHex) && HoverHex != Unit.Hex;
			const FHexCoord End = bStrike ? From : HoverHex;
			if ((bWalkTo || bStrike) && End != Unit.Hex)
			{
				const double Lift = 1.2;
				if (Unit.bFlies)
				{
					// a flyer's way is the arc it flies, as dots in the air, as the developer asked on 2026-11-12, and the ring where it lands
					const FVector A = Local(Unit.Hex, Lift);
					const FVector B = Local(End, Lift);
					const int32 Dots = FMath::Max(FMath::CeilToInt32(Unit.Hex.DistanceTo(End) * 2.5), 3);
					for (int32 Dot = 0; Dot < Dots; ++Dot)
					{
						const double Along = double(Dot) / Dots;
						const FVector Point = FMath::Lerp(A, B, Along) + FVector(0.0, 0.0, FMath::Sin(Along * UE_PI) * Config->HexRadius * 1.2);
						Disc(Point, 0.0, Config->PathDotRadius * (Dot == 0 ? 1.0 : 0.8), Config->PathColor);
					}
					Disc(B + FVector(0.0, 0.0, 0.05), Config->PathEndRadius - Config->PathLineWidth, Config->PathEndRadius, Config->PathColor);
				}
				else
				{
					TArray<FHexCoord> Path = Battle->FindPath(Active, End);
					if (!Path.IsEmpty() && Path[0] != Unit.Hex)
					{
						Path.Insert(Unit.Hex, 0);
					}
					for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
					{
						Segment(Local(Path[Index], Lift), Local(Path[Index + 1], Lift), Config->PathLineWidth, Config->PathColor);
					}
					for (int32 Index = 0; Index < Path.Num(); ++Index)
					{
						const bool bLast = Index == Path.Num() - 1;
						Disc(Local(Path[Index], Lift + 0.05), bLast ? Config->PathEndRadius - Config->PathLineWidth : 0.0, bLast ? Config->PathEndRadius : Config->PathDotRadius,
							Config->PathColor);
					}
				}
			}
			else if (bStrike)
			{
				Fill(From, Config->HoverColor, 0.95, 1.0);
			}
		}
	}
	Marks->ClearAllMeshSections();
	if (!Vertices.IsEmpty())
	{
		Marks->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, {}, Colors, {}, false);
		if (UMaterialInterface* Material = Config->MarkMaterial.LoadSynchronous())
		{
			Marks->SetMaterial(0, Material);
		}
	}
}

void AHexBattleArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FHexBattle* Battle = GetBattle();
	if (!bShowing || !Battle || !Config)
	{
		return;
	}
	Gather();
	UpdateDebugView();
	// the wall dissolves in front of the hex under the cursor
	if (CastleMaterial)
	{
		CastleMaterial->SetVectorParameterValue(TEXT("SiegeFade"), FLinearColor(float(HoverPoint.X), float(HoverPoint.Y), float(HoverPoint.Z), bHover ? 1.0f : 0.0f));
	}
	for (UMaterialInstanceDynamic* Material : FadeMaterials)
	{
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("SiegeFade"), FLinearColor(float(HoverPoint.X), float(HoverPoint.Y), float(HoverPoint.Z), bHover ? 1.0f : 0.0f));
		}
	}
	// the camera follows what the person asked of it
	const float Follow = 1.0f - FMath::Exp(-Config->CameraSmoothing * DeltaSeconds);
	LookAt = FMath::Lerp(LookAt, LookAtGoal, double(Follow));
	Yaw = FMath::Lerp(Yaw, YawGoal, Follow);
	Distance = FMath::Lerp(Distance, DistanceGoal, Follow);
	PlaceCamera();
	for (int32 Index = Popups.Num() - 1; Index >= 0; --Index)
	{
		Popups[Index].Age += DeltaSeconds;
		if (Popups[Index].Age >= Popups[Index].Life)
		{
			Popups.RemoveAt(Index);
		}
	}
	// what the person left to the game plays faster
	const int32 Person = GetPersonSide();
	const float Speed = Person != INDEX_NONE && Battles->IsSideAi(Person) ? Config->AutoSpeed : 1.0f;
	if (Current.IsSet())
	{
		PlayEvent(DeltaSeconds * Speed);
	}
	while (!Current.IsSet() && !Pending.IsEmpty())
	{
		const FHexBattleEvent Event = Pending[0];
		Pending.RemoveAt(0);
		BeginEvent(Event);
		if (CurrentLength <= 0.0f)
		{
			EndEvent();
		}
	}
	if (!IsPlaying() && !Battle->IsOver() && Battle->GetUnits().IsValidIndex(Battle->GetActive()) && Battles->IsSideAi(Battle->GetUnits()[Battle->GetActive()].Side))
	{
		AiWait -= DeltaSeconds * Speed;
		if (AiWait <= 0.0f)
		{
			Battle->PlayAi();
			AiWait = Config->TurnPause;
		}
	}
	BuildSiege();
	UpdateGate(DeltaSeconds);
	UpdateOutline();
	UpdateMarks();
}

bool AHexBattleArena::OrderAt(const FString& Order, const FHexCoord& Hex, int32 Unit, int32 Spell, FString& OutMessage)
{
	FHexBattle* Battle = GetBattle();
	if (!bShowing || !Battle)
	{
		OutMessage = TEXT("no battle is shown");
		return false;
	}
	const auto HoverAt = [this](const FHexCoord& At)
	{
		bHover = GetBattle()->IsOnField(At);
		HoverHex = At;
		HoverPoint = GetHexPoint(At);
	};
	if (Order == TEXT("hover"))
	{
		HoverAt(Hex);
		return true;
	}
	if (Order == TEXT("camera"))
	{
		// for trying the view: the distance and the pitch, as the column and the row of the order
		SetCamera(float(Hex.GetColumn()), float(Hex.GetRow()));
		return true;
	}
	if (Order == TEXT("turn"))
	{
		// for trying the camera without a mouse: degrees, and notches of the wheel, as the column and the row of the order
		MoveCamera(FVector2D::ZeroVector, float(Hex.GetColumn()), 0.0f);
		ZoomCamera(float(Hex.GetRow()));
		return true;
	}
	if (Order == TEXT("auto"))
	{
		OrderAuto();
		return true;
	}
	if (Order == TEXT("quick"))
	{
		OrderQuick();
		return true;
	}
	if (Order == TEXT("retreat"))
	{
		OrderRetreat();
		return true;
	}
	if (Order == TEXT("close"))
	{
		OrderClose();
		return !Battles->IsShown();
	}
	if (!CanOrder())
	{
		OutMessage = IsPlaying() ? TEXT("the field still plays what happened") : TEXT("it is not the turn of a unit the person fights with");
		return false;
	}
	UpdateReach();
	if (Order == TEXT("wait"))
	{
		return Battle->Wait();
	}
	if (Order == TEXT("defend"))
	{
		return Battle->Defend();
	}
	if (Order == TEXT("move"))
	{
		return Battle->Move(Hex);
	}
	if (Order == TEXT("shoot"))
	{
		return Battle->Shoot(Unit);
	}
	if (Order == TEXT("strike"))
	{
		FHexCoord From = Hex;
		if (!Battle->IsOnField(Hex) || !Battle->CanStrike(Battle->GetActive(), Unit, Hex))
		{
			HoverAt(Battle->GetUnits().IsValidIndex(Unit) ? Battle->GetUnits()[Unit].Hex : Hex);
			if (!FindStrikeFrom(Unit, From))
			{
				OutMessage = TEXT("no hex beside it can be reached");
				return false;
			}
		}
		return Battle->Strike(Unit, From);
	}
	if (Order == TEXT("cast"))
	{
		return Battle->Cast(Battle->GetUnits()[Battle->GetActive()].Side, Spell, Hex);
	}
	OutMessage = FString::Printf(TEXT("no order %s"), *Order);
	return false;
}

namespace
{
	void RunBattleCommand(const TArray<FString>& Args, UWorld* World)
	{
		AHexBattleArena* Arena = nullptr;
		for (TActorIterator<AHexBattleArena> It(World); It; ++It)
		{
			Arena = *It;
		}
		if (!Arena || !Arena->IsShowing() || !Arena->GetBattles() || Args.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Battle <state|move <Col> <Row>|strike <Unit> [<Col> <Row>]|shoot <Unit>|wait|defend|cast <Spell> <Col> <Row>|hover <Col> <Row>|auto|quick|retreat|close>, while a battle is shown"));
			return;
		}
		const FHexBattle& Battle = Arena->GetBattles()->GetBattle();
		const FString Order = Args[0].ToLower();
		if (Order == TEXT("state"))
		{
			UE_LOG(LogHexHero, Log, TEXT("hex.Battle: round %d, the turn of unit %d, %s, over %s"), Battle.GetRound(), Battle.GetActive(), Arena->IsPlaying() ? TEXT("playing") : TEXT("waiting"),
				Battle.IsOver() ? TEXT("yes") : TEXT("no"));
			for (int32 Index = 0; Index < Battle.GetUnits().Num(); ++Index)
			{
				const FHexBattleUnit& Unit = Battle.GetUnits()[Index];
				UE_LOG(LogHexHero, Log, TEXT("  %d: side %d, %d %s at %d %d, top %d, shots %d"), Index, Unit.Side, Unit.Count, *Unit.Stats.DisplayName, Unit.Hex.GetColumn(), Unit.Hex.GetRow(),
					Unit.TopHealth, Unit.ShotsLeft);
			}
			FString Obstacles;
			for (const FHexCoord& Hex : Battle.GetObstacles())
			{
				Obstacles += FString::Printf(TEXT(" %d,%d"), Hex.GetColumn(), Hex.GetRow());
			}
			UE_LOG(LogHexHero, Log, TEXT("  obstacles:%s; %s"), *Obstacles, *Arena->GetActionText());
			return;
		}
		const auto Hex = [&Args](int32 From) { return Args.IsValidIndex(From + 1) ? FHexCoord::FromOffset(FCString::Atoi(*Args[From]), FCString::Atoi(*Args[From + 1])) : FHexCoord(-99, -99); };
		const int32 Unit = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : INDEX_NONE;
		int32 Spell = INDEX_NONE;
		FHexCoord At = Hex(1);
		if (Order == TEXT("cast") && Args.Num() >= 4)
		{
			Spell = Arena->GetBattles()->GetConfig()->FindSpell(Args[1]);
			At = Hex(2);
		}
		else if (Order == TEXT("strike"))
		{
			At = Hex(2);
		}
		FString Message;
		const bool bDone = Arena->OrderAt(Order, At, Unit, Spell, Message);
		UE_LOG(LogHexHero, Log, TEXT("hex.Battle %s: %s%s"), *Order, bDone ? TEXT("done") : TEXT("not done"), Message.IsEmpty() ? TEXT("") : *(TEXT(", ") + Message));
	}

	FAutoConsoleCommandWithWorldAndArgs BattleCommand(TEXT("hex.Battle"),
		TEXT("The battle shown on its field: state, move <Col> <Row>, strike <Unit> [<Col> <Row> to strike from], shoot <Unit>, wait, defend, cast <Spell> <Col> <Row>, hover <Col> <Row> (for screenshots), camera <Distance> <Pitch>, turn <Degrees> <Notches> (turns the camera and zooms it by notches of the wheel), auto, quick, retreat, close. A unit is its index in state."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunBattleCommand));
}
