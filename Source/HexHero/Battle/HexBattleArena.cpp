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
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* SkyMaterialPath = TEXT("/Game/HexPlanet/Battle/M_HexBattleSky.M_HexBattleSky");

	/** A hash of a few numbers, for what stands where, the same every time */
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
			SunColor = Light->GetLightColor();
			Light->SetIntensity(SunIntensity * (Each ? Each->SunShare : 1.0f));
			Light->SetLightColor(Each ? Each->SunColor : FLinearColor::White);
		}
		It->SetActorRotation(FRotator(-48.0, 25.0, 0.0));
		break;
	}
	if (UMaterialInterface* SkyMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(SkyMaterial, this);
		const FLinearColor Back = Each ? Each->BackgroundColor : FLinearColor(0.3f, 0.45f, 0.65f, 1.0f);
		Instance->SetVectorParameterValue(TEXT("Color"), Back);
		Instance->SetVectorParameterValue(TEXT("TopColor"), Back * 1.6f);
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
		}
	}
	ClearField();
	SkyLight->SetVisibility(false);
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
	GateComponent.Reset();
	GateInstance = INDEX_NONE;
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
	const double Margin = Config->GroundMargin * Config->HexRadius * 1.7;
	const double HalfX = Battle->GetRows() * Config->HexRadius * 0.75 + Margin;
	const double HalfY = Battle->GetColumns() * Config->HexRadius * 0.87 + Margin * 1.6;
	const int32 Steps = 24;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	for (int32 I = 0; I <= Steps; ++I)
	{
		for (int32 J = 0; J <= Steps; ++J)
		{
			const FVector Point(FMath::Lerp(-HalfX, HalfX, I / double(Steps)), FMath::Lerp(-HalfY, HalfY, J / double(Steps)), 0.0);
			Vertices.Add(Point);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(Point.X, Point.Y) / 300.0);
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
	Ground->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, {}, {}, false);
	if (UMaterialInterface* Material = Config->GroundMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		const FHexBattleGround* Each = GetGround();
		const int32 Layer = Each && Each->TerrainLayer >= 0 ? Each->TerrainLayer : Battles->GetTerrainLayer();
		Instance->SetScalarParameterValue(TEXT("TerrainLayer"), float(Layer));
		Instance->SetScalarParameterValue(TEXT("Underground"), Battles->IsUnderground() ? 1.0f : 0.0f);
		Instance->SetVectorParameterValue(TEXT("FieldHalf"), FLinearColor(float(Battle->GetRows() * Config->HexRadius * 0.75 + Config->HexRadius), float(Battle->GetColumns() * Config->HexRadius * 0.87 + Config->HexRadius), 0.0f, 0.0f));
		Instance->SetVectorParameterValue(TEXT("FieldCenter"), FLinearColor(GetActorLocation()));
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

void AHexBattleArena::BuildProps()
{
	FHexBattle* Battle = GetBattle();
	const FHexBattleGround* Each = GetGround();
	const uint32 Seed = Mix(Battle->GetRound(), Battles->GetGroundLetter(), int32(Battle->GetObstacles().Num()));
	const auto MakeProp = [&](UStaticMesh* Mesh, const FVector& At, double Size, int32 Hash)
	{
		if (!Mesh)
		{
			return;
		}
		UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(this);
		Prop->SetStaticMesh(Mesh);
		Prop->SetupAttachment(Root);
		Prop->SetMobility(EComponentMobility::Movable);
		Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const double Extent = FMath::Max3(Bounds.BoxExtent.X, Bounds.BoxExtent.Y, 1.0);
		const double Scale = Size / Extent;
		Prop->SetWorldTransform(FTransform(FRotator(0.0, double(Hash % 360), 0.0), At - FVector(0.0, 0.0, (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale), FVector(Scale)));
		Prop->RegisterComponent();
		Props.Add(Prop);
	};
	UStaticMesh* Fallback = Config->FallbackMesh.LoadSynchronous();
	int32 Index = 0;
	for (const FHexCoord& Hex : Battle->GetObstacles())
	{
		const uint32 Hash = Mix(Hex.Q, Hex.R, int32(Seed));
		UStaticMesh* Mesh = Each && !Each->Obstacles.IsEmpty() ? Each->Obstacles[Hash % Each->Obstacles.Num()].LoadSynchronous() : Fallback;
		MakeProp(Mesh, GetHexPoint(Hex), Config->HexRadius * 0.8, int32(Hash));
		++Index;
	}
	// what stands around the field, outside it
	if (Each && !Each->Decor.IsEmpty())
	{
		FRandomStream Random(int32(Seed & 0x7fffffff));
		const double HalfX = Battle->GetRows() * Config->HexRadius * 0.75;
		const double HalfY = Battle->GetColumns() * Config->HexRadius * 0.87;
		for (int32 Count = 0; Count < 70; ++Count)
		{
			const double Angle = Random.FRandRange(0.0, UE_TWO_PI);
			const double Spread = Random.FRandRange(1.12, 1.9);
			const FVector At = GetActorLocation() + FVector(FMath::Cos(Angle) * HalfX * Spread + (FMath::Cos(Angle) > 0.0 ? Config->HexRadius * 2.0 : -Config->HexRadius * 2.0),
				FMath::Sin(Angle) * HalfY * Spread, 0.0);
			MakeProp(Each->Decor[Random.RandRange(0, Each->Decor.Num() - 1)].LoadSynchronous(), At, Config->HexRadius * Random.FRandRange(0.6, 1.6), Random.RandRange(0, 359));
		}
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
			Component = HexUnitFigure::Make(this, Root, Config, Unit.CreatureId, Unit.Kind.bUpgraded, Unit.Level, Battles->GetSideColor(Unit.Side), Config->UnitScale,
				Figure.Scale, Figure.Yaw, Figure.Hover);
			// the shooters of a tower stand on top of it
			if (Unit.bTower)
			{
				Figure.Hover += Config->TowerFigureLift;
			}
		}
		Figure.Component = Component;
		Figure.Position = GetHexPoint(Unit.Hex);
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
	OutWorld = GetHexPoint(Battle->GetWalls()[Wall].Hex) + FVector(0.0, 0.0, Config->SiegeWallHeight * 1.05);
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
	GateComponent.Reset();
	GateInstance = INDEX_NONE;
	if (!CastleMaterial)
	{
		if (UMaterialInterface* Parent = CastleConfig->Material.LoadSynchronous())
		{
			CastleMaterial = UMaterialInstanceDynamic::Create(Parent, this);
			HexMaterialConfig::Apply(CastleMaterial, UHexSiteConfig::Load());
			HexMaterialConfig::Apply(CastleMaterial, CastleConfig);
		}
	}
	const int32 Fort = Battles->GetFortLevel();
	const FHexCastle* Castle = Battles->GetCastle();
	const FLinearColor OwnerColor = Castle ? Battles->GetSideColor(1) : FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	const float Style = Castle && CastleConfig->Factions.IsValidIndex(Castle->Faction) ? CastleConfig->Factions[Castle->Faction].WallStyle : 0.0f;
	const auto GetMesh = [Fort](const TArray<TSoftObjectPtr<UStaticMesh>>& Meshes) { return Meshes.IsValidIndex(Fort) ? Meshes[Fort].LoadSynchronous() : nullptr; };
	UStaticMesh* WallMesh = GetMesh(CastleConfig->WallMeshes);
	UStaticMesh* TowerMesh = GetMesh(CastleConfig->TowerMeshes);
	UStaticMesh* GateMesh = GetMesh(CastleConfig->GateMeshes);
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
	const auto AddInstance = [&](UInstancedStaticMeshComponent* Component, const FVector& At, double Turn) -> int32
	{
		if (!Component)
		{
			return INDEX_NONE;
		}
		const float Data[11] = {OwnerColor.R, OwnerColor.G, OwnerColor.B, OwnerColor.R, OwnerColor.G, OwnerColor.B, -1000.0f, Style, -1000.0f, 0.0f, 0.0f};
		const int32 Index = Component->AddInstance(FTransform(FRotator(0.0, Turn, 0.0), At - GetActorLocation(), FVector(Scale)), false);
		Component->SetCustomData(Index, MakeArrayView(Data), true);
		return Index;
	};
	UInstancedStaticMeshComponent* Walls = MakeInstances(WallMesh);
	UInstancedStaticMeshComponent* Towers = MakeInstances(TowerMesh);
	UInstancedStaticMeshComponent* Gates = MakeInstances(GateMesh);
	const TArray<FHexBattleWall>& Parts = Battle->GetWalls();
	// the stretches between two parts that stand, along the line from one to the next, as many as fill it; the mesh of the castle lies along its
	// Y with its X out of the castle, here towards the attacker
	const double StretchLength = WallMesh ? FMath::Max(WallMesh->GetBounds().BoxExtent.Y * 2.0 * Scale, 1.0) : 1.0;
	for (int32 Index = 0; Index + 1 < Parts.Num(); ++Index)
	{
		if (!Parts[Index].IsStanding() || !Parts[Index + 1].IsStanding())
		{
			continue;
		}
		const FVector A = GetHexPoint(Parts[Index].Hex);
		const FVector B = GetHexPoint(Parts[Index + 1].Hex);
		const FVector Along = (B - A).GetSafeNormal2D();
		const double Turn = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)) - 90.0;
		const int32 Count = FMath::Max(FMath::CeilToInt32(FVector::Dist2D(A, B) / StretchLength), 1);
		for (int32 Piece = 0; Piece < Count; ++Piece)
		{
			AddInstance(Walls, FMath::Lerp(A, B, (Piece + 0.5) / Count), Turn);
		}
	}
	FRandomStream Random(int32(Battle->GetRound() * 31 + 7));
	for (const FHexBattleWall& Part : Parts)
	{
		const FVector At = GetHexPoint(Part.Hex);
		if (!Part.IsStanding())
		{
			// rubble where it fell
			const FHexBattleGround* Each = GetGround();
			UStaticMesh* Rubble = Each && !Each->Obstacles.IsEmpty() ? Each->Obstacles[0].LoadSynchronous() : Config->FallbackMesh.LoadSynchronous();
			if (Rubble)
			{
				UStaticMeshComponent* Pile = NewObject<UStaticMeshComponent>(this);
				Pile->SetStaticMesh(Rubble);
				Pile->SetupAttachment(Root);
				Pile->SetMobility(EComponentMobility::Movable);
				Pile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				const double Extent = FMath::Max(Rubble->GetBounds().BoxExtent.GetMax(), 1.0);
				Pile->SetWorldTransform(FTransform(FRotator(0.0, Random.FRandRange(0.0, 360.0), 0.0), At, FVector(Config->HexRadius * 0.45 / Extent, Config->HexRadius * 0.45 / Extent, Config->HexRadius * 0.2 / Extent)));
				Pile->RegisterComponent();
				SiegeProps.Add(Pile);
			}
			continue;
		}
		if (Part.Part == EHexWallPart::Tower)
		{
			AddInstance(Towers, At, -90.0);
		}
		else if (Part.Part == EHexWallPart::Gate)
		{
			// its passage runs from the attacker to the defender
			GateInstance = AddInstance(Gates, At, -90.0);
			GateComponent = Gates;
		}
	}
	bGateShownOpen = false;
}

void AHexBattleArena::UpdateGate()
{
	FHexBattle* Battle = GetBattle();
	UInstancedStaticMeshComponent* Gate = GateComponent.Get();
	const FHexBattleWall* Part = Battle ? Battle->GetWalls().FindByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Gate; }) : nullptr;
	if (!Gate || GateInstance == INDEX_NONE || !Part || !Part->IsStanding())
	{
		return;
	}
	// open while a defender goes through it or stands in it, and for good once jammed: its leaves are held open by an opening that seems
	// always to have just ended
	bool bOpen = Battle->IsGateJammed() || Battle->FindUnitAt(Part->Hex) != INDEX_NONE;
	if (Current.IsSet() && Current->Type == FHexBattleEvent::EType::Move && Current->Path.Contains(Part->Hex))
	{
		bOpen = true;
	}
	const UHexCastleConfig* CastleConfig = UHexCastleConfig::Load();
	if (bOpen)
	{
		const float Since = GetWorld()->GetTimeSeconds() - (bGateShownOpen ? CastleConfig->GateRiseTime : 0.0f);
		Gate->SetCustomDataValue(GateInstance, 8, Since, true);
	}
	bGateShownOpen = bOpen;
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
	// the catapult shoots at the wall, a stack of the attacker strikes it from beside it
	if (const FHexBattleWall* Wall = Battle->FindWall(HoverHex); Wall && Wall->IsStanding())
	{
		FHexCoord WallFrom;
		if (Battle->CanFireCatapult(HoverHex))
		{
			Battle->FireCatapult(HoverHex);
		}
		else if (FindWallStrikeFrom(HoverHex, WallFrom))
		{
			Battle->StrikeWall(HoverHex, WallFrom);
		}
		else if (Battle->FindUnitAt(HoverHex) == INDEX_NONE)
		{
			AddLog(TEXT("The wall is out of reach"));
		}
		if (Battle->FindUnitAt(HoverHex) == INDEX_NONE)
		{
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
	if (const FHexBattleWall* Wall = Battle->FindWall(HoverHex); Wall && Wall->IsStanding() && Unit == INDEX_NONE)
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
		return FString::Printf(TEXT("%s here, %d %s"), Active.bFlies ? TEXT("Fly") : TEXT("Walk"), Hexes, Hexes == 1 ? TEXT("hex") : TEXT("hexes"));
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
	if (const FHexBattleWall* Wall = Battle->FindWall(HoverHex); Wall && Wall->IsStanding() && Battle->FindUnitAt(HoverHex) == INDEX_NONE)
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
	UpdateGate();
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
