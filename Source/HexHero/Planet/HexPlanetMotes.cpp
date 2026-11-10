// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetMotes.h"
#include "HexHero.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HexMaterialConfig.h"
#include "HexPlanet.h"
#include "HexPlanetConfig.h"
#include "HexPlanetObjects.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** How often the spores near the focus are gathered again, in seconds, and how far the focus may move, in cells, before they are at once */
	constexpr double SporeRefreshSeconds = 0.5;
	constexpr double SporeRefreshCells = 2.0;
}

UInstancedStaticMeshComponent* UHexPlanetMotes::MakeComponent(const TCHAR* Name, USceneComponent* Parent)
{
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(GetOwner(), Name);
	Component->SetupAttachment(Parent);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false);
	Component->SetVisibleInRayTracing(false);
	Component->bAffectDistanceFieldLighting = false;
	Component->bAffectDynamicIndirectLighting = false;
	Component->SetNumCustomDataFloats(4);
	Component->RegisterComponent();
	return Component;
}

void UHexPlanetMotes::Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings)
{
	Planet = InPlanet;
	Settings = InSettings;
	UStaticMesh* Mesh = Settings->MoteMesh.LoadSynchronous();
	UMaterialInterface* Material = Settings->MoteMaterial.LoadSynchronous();
	if (!Mesh || !Material)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: the motes need %s and %s, run Tools/create_planet_assets.py underground"), *Settings->MoteMesh.ToString(), *Settings->MoteMaterial.ToString());
		return;
	}

	const auto MakeMaterial = [this, Material](float Anchored)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		HexMaterialConfig::Apply(Instance, Settings);
		Instance->SetScalarParameterValue(TEXT("PlanetRadius"), float(Planet->GetRadius()));
		Instance->SetScalarParameterValue(TEXT("MoteAnchored"), Anchored);
		return Instance;
	};
	MoteMaterial = MakeMaterial(0.0f);
	SporeMaterial = MakeMaterial(1.0f);

	// the material pads the bounds of each instance by the bound it was made with, as its offset may take a mote anywhere in its box
	float Bound = 0.0f;
	MoteMaterial->GetScalarParameterValue(TEXT("MoteWpoBound"), Bound);
	if (Bound < Settings->GetMoteWpoBound())
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: %s was made for motes that move %.0f from their instances, the config needs %.0f, so they may blink out; run Tools/create_planet_assets.py underground"),
			*Material->GetName(), Bound, Settings->GetMoteWpoBound());
	}

	Motes = MakeComponent(TEXT("MoteInstances"), this);
	Motes->SetStaticMesh(Mesh);
	Motes->SetMaterial(0, MoteMaterial);
	// the spores stand at their cells, so they hang from the planet and not from the box that follows the camera
	Spores = MakeComponent(TEXT("SporeInstances"), Planet->GetRootComponent());
	Spores->SetStaticMesh(Mesh);
	Spores->SetMaterial(0, SporeMaterial);

	// each mote stands once at its seeded spot of the box, so the bounds of the component cover the box, curved as the shell over the passages
	const double Box = Settings->MoteBox;
	const double ShellRadius = FMath::Max(Planet->GetRadius() + Settings->LevelHeight, 1.0);
	FRandomStream Random(0x6D6F7465);
	TArray<FTransform> Transforms;
	TArray<float> Data;
	const int32 Count = FMath::Max(Settings->MoteCount, 0);
	Transforms.Reserve(Count);
	Data.Reserve(Count * 4);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector3f Seed(Random.FRand(), Random.FRand(), Random.FRand());
		const double X = (Seed.X - 0.5) * Box;
		const double Y = (Seed.Y - 0.5) * Box;
		const double Z = FMath::Lerp(-double(Settings->MoteHeightBelow), double(Settings->MoteHeightAbove), double(Seed.Z)) - (X * X + Y * Y) / (2.0 * ShellRadius);
		Transforms.Add(FTransform(FQuat::Identity, FVector(X, Y, Z), FVector(0.01)));
		Data.Append({Seed.X, Seed.Y, Seed.Z, Random.FRand()});
	}
	Motes->AddInstances(Transforms, false);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Motes->SetCustomData(Index, TArrayView<const float>(Data.GetData() + Index * 4, 4), false);
	}
	Motes->MarkRenderStateDirty();
	Motes->SetVisibility(false);
	Spores->SetVisibility(false);
	UE_LOG(LogHexHero, Log, TEXT("Planet: %d motes in a box %.0f wide, spores of up to %d"), Count, Box, Settings->SporeMax);
}

void UHexPlanetMotes::SetShown(bool bInShown)
{
	bShown = bInShown;
}

void UHexPlanetMotes::SetHeroLight(const FLinearColor& Sphere)
{
	// the spores glow by themselves
	if (MoteMaterial)
	{
		MoteMaterial->SetVectorParameterValue(TEXT("HeroLight"), Sphere);
	}
}

int32 UHexPlanetMotes::GetSporeCount() const
{
	return Spores ? Spores->GetInstanceCount() : 0;
}

void UHexPlanetMotes::Update(const APlayerController* PlayerController, float DeltaTime)
{
	const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
	if (!Motes || !CameraManager || !Planet || !Planet->IsBuilt())
	{
		return;
	}

	// the point the view through the middle of the screen meets the shell of the tops of the passages, else the one under the camera
	const FVector3d Camera = CameraManager->GetCameraLocation();
	const FVector3d Origin = Planet->FindViewFocus(Camera, CameraManager->GetCameraRotation().Vector());

	// a close-up effect: far from the place the camera looks at it sees none, and pays nothing
	const bool bNear = bShown && FVector3d::Distance(Camera, Origin) < Settings->MoteFadeEnd;
	if (Motes->IsVisible() != bNear)
	{
		Motes->SetVisibility(bNear);
		Spores->SetVisibility(bNear);
	}
	if (!bNear)
	{
		return;
	}

	if (!bPlaced || FVector3d::Distance(Origin, PlacedOrigin) > Settings->MoteBox / 8.0)
	{
		PlacedOrigin = Origin;
		bPlaced = true;
		SetWorldLocationAndRotation(Origin, FRotationMatrix::MakeFromZ(Planet->GetUpAt(Origin)).ToQuat());
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	PlayerController->GetViewportSize(SizeX, SizeY);
	const float PixelScale = 2.0f * FMath::Tan(FMath::DegreesToRadians(CameraManager->GetFOVAngle()) * 0.5f) / float(FMath::Max(SizeX, 1));
	for (UMaterialInstanceDynamic* Instance : {MoteMaterial.Get(), SporeMaterial.Get()})
	{
		Instance->SetVectorParameterValue(TEXT("MoteOrigin"), FLinearColor(float(Origin.X), float(Origin.Y), float(Origin.Z), 0.0f));
		Instance->SetScalarParameterValue(TEXT("MotePixelScale"), PixelScale);
	}

	// the spores again every half second, or at once when the focus moved a few cells
	SporeTimer += DeltaTime;
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const int32 Cell = FocusHint = Grid.FindCell(Origin.GetSafeNormal(), FocusHint);
	const double Spacing = FMath::Sqrt(4.0 * UE_DOUBLE_PI / FMath::Max(Grid.GetCells().Num(), 1));
	const bool bMoved = FocusCell == INDEX_NONE || !Grid.GetCells().IsValidIndex(Cell)
		|| FVector3d::DotProduct(Grid.GetCells()[FocusCell].Center, Grid.GetCells()[Cell].Center) < FMath::Cos(SporeRefreshCells * Spacing);
	if (bMoved || SporeTimer >= SporeRefreshSeconds)
	{
		SporeTimer = 0.0;
		FocusCell = Cell;
		UpdateSpores(Origin);
	}
}

void UHexPlanetMotes::UpdateSpores(const FVector3d& Focus)
{
	const UHexPlanetObjects* Objects = Planet->GetObjects();
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const int32 Max = FMath::Max(Settings->SporeMax, 0);
	if (!Objects || Cells.IsEmpty())
	{
		return;
	}

	// the cells within SporeRadius whose kinds rise spores, nearest first, as many as the spores fill
	const double ShellRadius = Planet->GetRadius() + Settings->LevelHeight;
	const FVector3d Direction = Focus.GetSafeNormal();
	const double Reach = FMath::Cos(FMath::Min(Settings->SporeRadius / FMath::Max(ShellRadius, 1.0), UE_DOUBLE_PI));
	TArray<TPair<double, int32>> Near;
	for (int32 Cell = 0; Cell < Cells.Num() && Max > 0; ++Cell)
	{
		const double Dot = FVector3d::DotProduct(Cells[Cell].Center, Direction);
		FLinearColor Color;
		if (Dot >= Reach && Objects->GetCellSpores(Cell, Color) > 0)
		{
			Near.Add({-Dot, Cell});
		}
	}
	Near.Sort();
	TArray<int32> Chosen;
	int32 Total = 0;
	for (const TPair<double, int32>& Each : Near)
	{
		if (Total >= Max)
		{
			break;
		}
		FLinearColor Color;
		Total += Objects->GetCellSpores(Each.Value, Color);
		Chosen.Add(Each.Value);
	}
	if (Chosen == SporeCells)
	{
		return;
	}
	SporeCells = MoveTemp(Chosen);

	// each spore stands at a hashed spot of its cell on the top of the passages, from which the material lets it rise and swirl
	TArray<FTransform> Transforms;
	TArray<float> Data;
	const double Spread = Settings->SporeSpread * Settings->HexRadius;
	for (const int32 Cell : SporeCells)
	{
		FLinearColor Color;
		const int32 Count = Objects->GetCellSpores(Cell, Color);
		const FVector3d Up = Planet->GetUpAt(Cells[Cell].Center);
		const FVector3d East = FVector3d::CrossProduct(FMath::Abs(Up.Z) < 0.99 ? FVector3d::UnitZ() : FVector3d::UnitX(), Up).GetSafeNormal();
		const FVector3d North = FVector3d::CrossProduct(Up, East);
		FRandomStream Random(int32(HashCombine(GetTypeHash(Cell), 0x53504F52u)));
		for (int32 Index = 0; Index < Count && Transforms.Num() < Max; ++Index)
		{
			const double Angle = Random.FRand() * UE_DOUBLE_TWO_PI;
			const double Distance = FMath::Sqrt(Random.FRand()) * Spread;
			const FVector3d Point = (Up * ShellRadius + (East * FMath::Cos(Angle) + North * FMath::Sin(Angle)) * Distance).GetSafeNormal() * ShellRadius;
			Transforms.Add(FTransform(FQuat::Identity, Point, FVector(0.01)));
			Data.Append({Color.R, Color.G, Color.B, Random.FRand()});
		}
	}
	Spores->ClearInstances();
	if (!Transforms.IsEmpty())
	{
		Spores->AddInstances(Transforms, false, true);
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			Spores->SetCustomData(Index, TArrayView<const float>(Data.GetData() + Index * 4, 4), false);
		}
	}
	Spores->MarkRenderStateDirty();
	UE_LOG(LogHexHero, Verbose, TEXT("Planet: %d spores over %d cells near cell %d"), Transforms.Num(), SporeCells.Num(), FocusCell);
}
