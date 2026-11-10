// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetFlocks.h"
#include "HexHero.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/HashTable.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HexPlanet.h"
#include "HexPlanetConfig.h"
#include "HexPlanetObjects.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** How often the flocks near the focus are gathered again, in seconds, and how far the focus may move, in cells, before they are at once */
	constexpr double GatherSeconds = 0.5;
	constexpr double GatherCells = 2.0;

	/** The steps of time from which the heading and the turn of an animal are found, in seconds */
	constexpr double HeadingStep = 0.08;

	TAutoConsoleVariable<float> CVarHexPlanetFlockSize(TEXT("hex.PlanetFlockSize"), 1.0f,
		TEXT("A factor on the sizes of the birds and bats (FHexFlockKind::Size), for trying sizes while the game runs."));

	/** A number from 0 to 1 of a cell and a salt, the same in every run; mixed well, as the cells near the camera are a run of numbers, whose
	 *  plain hashes fall in one narrow band and would all pass a share or all fail it */
	float HashUnit(int32 Cell, uint32 Salt)
	{
		return float(MurmurFinalize32(uint32(Cell) * 0x9E3779B9u ^ MurmurFinalize32(Salt)) & 0xFFFFFF) / float(0x1000000);
	}
}

void UHexPlanetFlocks::Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings)
{
	Planet = InPlanet;
	Settings = InSettings;
	UMaterialInterface* Material = Settings->FlockMaterial.LoadSynchronous();
	if (!Material)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: the flocks need %s, run Tools/create_planet_assets.py flocks"), *Settings->FlockMaterial.ToString());
		return;
	}

	Kinds.SetNum(Settings->Flocks.Num());
	Components.SetNum(Settings->Flocks.Num());
	Materials.SetNum(Settings->Flocks.Num());
	int32 Made = 0;
	for (int32 Index = 0; Index < Settings->Flocks.Num(); ++Index)
	{
		const FHexFlockKind& Kind = Settings->Flocks[Index];
		// a kind of the other layer has no homes here, so it loads nothing
		if ((Kind.Habitat == EHexFlockHabitat::Passage) != (Planet->GetLayer() == EHexPlanetLayer::Underground))
		{
			continue;
		}
		UStaticMesh* Mesh = Kind.Mesh.LoadSynchronous();
		if (!Mesh)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Planet: the flock %s lacks its mesh %s, run Tools/HexPlanetObjects/import_planet_flocks.py"), *Kind.Name.ToString(), *Kind.Mesh.ToString());
			continue;
		}

		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		Instance->SetScalarParameterValue(TEXT("FlapRate"), Kind.FlapRate);
		Instance->SetScalarParameterValue(TEXT("FlapAngle"), Kind.FlapAngle);
		Instance->SetScalarParameterValue(TEXT("GlideShare"), Kind.GlideShare);
		Instance->SetScalarParameterValue(TEXT("GlideAngle"), Kind.GlideAngle);
		Instance->SetScalarParameterValue(TEXT("FlockBrightness"), Settings->FlockBrightness);
		Materials[Index] = Instance;

		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Flock_%s"), *Kind.Name.ToString()));
		Component->SetupAttachment(this);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(Kind.bCastShadow);
		// small things that move every frame: not worth a place in the ray tracing scene nor in the distance fields
		Component->SetVisibleInRayTracing(false);
		Component->bAffectDistanceFieldLighting = false;
		Component->bAffectDynamicIndirectLighting = false;
		Component->SetNumCustomDataFloats(1);
		Component->SetStaticMesh(Mesh);
		Component->SetMaterial(0, Instance);
		Component->SetVisibility(false);
		Component->RegisterComponent();
		Components[Index] = Component;
		++Made;
	}
	UE_LOG(LogHexHero, Log, TEXT("Planet: %d kinds of flocks on the %s"), Made, GetPlanetLayerName(Planet->GetLayer()));
}

void UHexPlanetFlocks::SetShown(bool bInShown)
{
	bShown = bInShown;
}

int32 UHexPlanetFlocks::GetCount() const
{
	int32 Count = 0;
	for (const FKind& Kind : Kinds)
	{
		Count += Kind.Animals.Num();
	}
	return Count;
}

bool UHexPlanetFlocks::IsHome(const FHexFlockKind& Kind, int32 Cell) const
{
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const int32 Level = Planet->GetCellLevel(Cell);
	const auto HasNeighbor = [this, &Cells, Cell](TFunctionRef<bool(int32)> Test)
	{
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (Test(Planet->GetCellLevel(Neighbor)))
			{
				return true;
			}
		}
		return false;
	};
	// a flock of the land keeps away from the mountains, whose peaks stand higher than it flies
	const auto IsNearMountains = [this, &Cells, Cell]()
	{
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (Planet->GetCellLevel(Neighbor) >= 2)
			{
				return true;
			}
			for (const int32 Second : Cells[Neighbor].Neighbors)
			{
				if (Planet->GetCellLevel(Second) >= 2)
				{
					return true;
				}
			}
		}
		return false;
	};
	const auto HasCanopy = [this, Cell]()
	{
		FLinearColor Color;
		float Cover = 0.0f;
		return Planet->GetObjects() && Planet->GetObjects()->GetCellCanopy(Cell, Color, Cover) && Cover >= 0.5f;
	};

	switch (Kind.Habitat)
	{
	case EHexFlockHabitat::Coast:
		// over the sea beside land
		return Level < 0 && HasNeighbor([](int32 Each) { return Each >= 0; }) && !IsNearMountains();
	case EHexFlockHabitat::Forest:
		return (Level == 0 || Level == 1) && HasCanopy() && !IsNearMountains();
	case EHexFlockHabitat::Land:
		return (Level == 0 || Level == 1) && !HasCanopy() && !IsNearMountains();
	case EHexFlockHabitat::Mountains:
		return Level >= 2;
	case EHexFlockHabitat::Passage:
		// a hall: a passage whose neighbors are all passages, so a loop around it stays between the walls
		return Level == 0 && !HasNeighbor([](int32 Each) { return Each != 0; });
	}
	return false;
}

void UHexPlanetFlocks::Gather(const FVector3d& Focus, const FVector3d& Camera)
{
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	if (Cells.IsEmpty())
	{
		return;
	}
	const double ShellRadius = FMath::Max(Planet->GetRadius(), 1.0);
	const FVector3d Direction = Focus.GetSafeNormal();
	const double Reach = FMath::Cos(FMath::Min(Settings->FlockRadius / ShellRadius, UE_DOUBLE_PI));

	for (int32 Index = 0; Index < Kinds.Num(); ++Index)
	{
		UInstancedStaticMeshComponent* Component = Components[Index];
		if (!Component)
		{
			continue;
		}
		const FHexFlockKind& Kind = Settings->Flocks[Index];
		const uint32 Salt = GetTypeHash(Kind.Name);

		// the homes of the kind within reach, nearest first, as many as its animals fill
		TArray<TPair<double, int32>> Near;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			const double Dot = FVector3d::DotProduct(Cells[Cell].Center, Direction);
			if (Dot >= Reach && HashUnit(Cell, Salt) < Kind.CellShare && IsHome(Kind, Cell))
			{
				Near.Add({-Dot, Cell});
			}
		}
		Near.Sort();
		const auto GetNumAnimals = [&Kind, Salt](int32 Cell)
		{
			const int32 Low = FMath::Max(Kind.CountMin, 1);
			return Low + int32(HashUnit(Cell, Salt + 1) * float(FMath::Max(Kind.CountMax - Low + 1, 1)));
		};
		TArray<int32> Chosen;
		int32 Total = 0;
		for (const TPair<double, int32>& Each : Near)
		{
			if (Total >= Settings->FlockMax)
			{
				break;
			}
			Total += GetNumAnimals(Each.Value);
			Chosen.Add(Each.Value);
		}
		FKind& Flying = Kinds[Index];
		if (Chosen == Flying.Cells)
		{
			continue;
		}
		Flying.Cells = MoveTemp(Chosen);
		Flying.Flocks.Reset();
		Flying.Animals.Reset();

		for (const int32 Cell : Flying.Cells)
		{
			FFlock& Flock = Flying.Flocks.AddDefaulted_GetRef();
			Flock.Cell = Cell;
			Flock.Up = Planet->GetUpAt(Cells[Cell].Center);
			Flock.East = FVector3d::CrossProduct(FMath::Abs(Flock.Up.Z) < 0.99 ? FVector3d::UnitZ() : FVector3d::UnitX(), Flock.Up).GetSafeNormal();
			Flock.North = FVector3d::CrossProduct(Flock.Up, Flock.East);
			// its heights are measured from the highest top around its home, the sea for a cell of the sea
			Flock.BaseRadius = FMath::Max(Planet->GetCellTopRadius(Cell), Planet->GetRadius());
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				Flock.BaseRadius = FMath::Max(Flock.BaseRadius, Planet->GetCellTopRadius(Neighbor));
			}
			Flock.Phase = double(HashUnit(Cell, Salt + 2)) * UE_DOUBLE_TWO_PI;
			Flock.Turn = HashUnit(Cell, Salt + 3) < 0.5f ? -1.0 : 1.0;
			Flock.FirstAnimal = Flying.Animals.Num();
			Flock.NumAnimals = GetNumAnimals(Cell);

			FRandomStream Random(int32(HashCombineFast(GetTypeHash(Cell), Salt)));
			for (int32 Each = 0; Each < Flock.NumAnimals; ++Each)
			{
				FAnimal& Animal = Flying.Animals.AddDefaulted_GetRef();
				Animal.Flock = Flying.Flocks.Num() - 1;
				Animal.Seed = Random.FRand();
				Animal.Second = Random.FRand();
				const float Angle = Random.FRand() * UE_TWO_PI;
				Animal.Offset = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * FMath::Sqrt(Random.FRand());
			}
		}

		// the instances start where their animals fly now: one added scaled to nothing was never drawn, however it was moved afterwards
		Component->ClearInstances();
		if (!Flying.Animals.IsEmpty())
		{
			const double Time = GetWorld()->GetTimeSeconds();
			TArray<FTransform> Transforms;
			Transforms.Reserve(Flying.Animals.Num());
			for (const FAnimal& Animal : Flying.Animals)
			{
				Transforms.Add(GetTransform(Kind, Flying.Flocks[Animal.Flock], Animal, Time, Camera));
			}
			Component->AddInstances(Transforms, false, true);
			for (int32 Each = 0; Each < Flying.Animals.Num(); ++Each)
			{
				Component->SetCustomDataValue(Each, 0, Flying.Animals[Each].Seed, false);
			}
		}
		Component->MarkRenderStateDirty();
		UE_LOG(LogHexHero, Verbose, TEXT("Planet: %d %s in %d flocks near cell %d, at home at the cells %s"), Flying.Animals.Num(), *Kind.Name.ToString(), Flying.Flocks.Num(),
			FocusCell, *FString::JoinBy(Flying.Cells, TEXT(" "), [](int32 Each) { return FString::FromInt(Each); }));
	}
}

FVector3d UHexPlanetFlocks::GetPosition(const FHexFlockKind& Kind, const FFlock& Flock, const FAnimal& Animal, double Time) const
{
	// the angle along the loop: a flock that keeps together shares it, each animal a little behind the first, and otherwise each has its own
	// place on a loop of its own width
	const double Around = Flock.Turn * Time * Kind.Speed / FMath::Max(Kind.PathRadius, 1.0);
	const double Angle = Flock.Phase + (Kind.bTogether ? Around - Flock.Turn * Animal.Seed * 0.25 : Around * (0.75 + 0.5 * Animal.Second) + Animal.Seed * UE_DOUBLE_TWO_PI);
	const double Width = Kind.bTogether ? 1.0 : 0.55 + 0.6 * Animal.Second;

	// the loop bulges and pinches as the animal goes round it, so it is no ellipse
	const double Reach = Kind.PathRadius * Width * (1.0 + Kind.PathWander * FMath::Sin(Angle * 2.0 + Flock.Phase * 3.0));
	const double LoopX = FMath::Cos(Angle) * Reach;
	const double LoopY = FMath::Sin(Angle) * Reach * Kind.PathStretch * (1.0 + Kind.PathWander * FMath::Cos(Angle * 3.0 + Flock.Phase * 5.0));
	const double Turned = Flock.Phase * 7.0;
	double X = LoopX * FMath::Cos(Turned) - LoopY * FMath::Sin(Turned);
	double Y = LoopX * FMath::Sin(Turned) + LoopY * FMath::Cos(Turned);

	// its place in the flock, around which it drifts
	const double Drift = Time * 0.35 * (0.5 + Animal.Seed) + Animal.Seed * 40.0;
	X += Kind.Spread * (Animal.Offset.X + 0.3 * FMath::Sin(Drift));
	Y += Kind.Spread * (Animal.Offset.Y + 0.3 * FMath::Cos(Drift * 1.3));

	const double Height = FMath::Lerp(double(Kind.HeightMin), double(Kind.HeightMax), 0.5 + 0.5 * FMath::Sin(Angle * 0.7 + (Kind.bTogether ? 0.0 : Animal.Seed * 6.0) + Flock.Phase))
		+ Kind.Spread * 0.2 * FMath::Sin(Drift * 0.8 + Animal.Second * 6.0);
	const FVector3d Direction = (Flock.Up * Flock.BaseRadius + Flock.East * X + Flock.North * Y).GetSafeNormal();
	return Direction * (Flock.BaseRadius + Height);
}

FTransform UHexPlanetFlocks::GetTransform(const FHexFlockKind& Kind, const FFlock& Flock, const FAnimal& Animal, double Time, const FVector3d& Camera) const
{
	const FVector3d Position = GetPosition(Kind, Flock, Animal, Time);
	const FVector3d Next = GetPosition(Kind, Flock, Animal, Time + HeadingStep);
	const FVector3d After = GetPosition(Kind, Flock, Animal, Time + HeadingStep * 2.0);
	const FVector3d Up = Position.GetSafeNormal();
	const FVector3d Forward = (Next - Position).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Flock.East);
	const FVector3d Ahead = (After - Next).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Forward);

	// it leans into its turn, its up towards the middle of the turn
	const double TurnRate = FVector3d::DotProduct(FVector3d::CrossProduct(Forward, Ahead), Up) / HeadingStep;
	const double Roll = FMath::Clamp(TurnRate * Kind.Bank, -1.1, 1.1);
	const FVector3d Right = FVector3d::CrossProduct(Up, Forward).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, Flock.North);
	const FVector3d Leaning = Up * FMath::Cos(Roll) + Right * FMath::Sin(Roll);

	// larger or smaller by animal, and shrunk away towards FlockFadeEnd from the camera, but never to nothing
	const double Fade = 1.0 - FMath::SmoothStep(double(Settings->FlockFadeStart), FMath::Max(double(Settings->FlockFadeEnd), Settings->FlockFadeStart + 1.0),
		FVector3d::Distance(Camera, Position));
	const double Size = Kind.Size * FMath::Max(double(CVarHexPlanetFlockSize.GetValueOnGameThread()), 0.01) * (1.0 + Kind.SizeVariation * (Animal.Second * 2.0 - 1.0)) * FMath::Max(Fade, 0.02);
	return FTransform(FRotationMatrix::MakeFromXZ(Forward, Leaning).ToQuat(), Position, FVector(Size));
}

void UHexPlanetFlocks::Update(const APlayerController* PlayerController, float DeltaTime)
{
	const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
	if (Components.IsEmpty() || !CameraManager || !Planet || !Planet->IsBuilt())
	{
		return;
	}

	// a close-up effect: far from the place the camera looks at it sees none, and pays nothing
	const FVector3d Camera = CameraManager->GetCameraLocation();
	const FVector3d Focus = Planet->FindViewFocus(Camera, CameraManager->GetCameraRotation().Vector());
	const bool bNear = bShown && FVector3d::Distance(Camera, Focus) < Settings->FlockFadeEnd;
	for (UInstancedStaticMeshComponent* Component : Components)
	{
		if (Component && Component->IsVisible() != bNear)
		{
			Component->SetVisibility(bNear);
		}
	}
	if (!bNear)
	{
		return;
	}

	// the flocks again every half second, or at once when the focus moved a few cells
	GatherTimer += DeltaTime;
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const int32 Cell = FocusHint = Grid.FindCell(Focus.GetSafeNormal(), FocusHint);
	const double Spacing = FMath::Sqrt(4.0 * UE_DOUBLE_PI / FMath::Max(Grid.GetCells().Num(), 1));
	const bool bMoved = FocusCell == INDEX_NONE || !Grid.GetCells().IsValidIndex(Cell) || !Grid.GetCells().IsValidIndex(FocusCell)
		|| FVector3d::DotProduct(Grid.GetCells()[FocusCell].Center, Grid.GetCells()[Cell].Center) < FMath::Cos(GatherCells * Spacing);
	if (bMoved || GatherTimer >= GatherSeconds)
	{
		GatherTimer = 0.0;
		FocusCell = Cell;
		Gather(Focus, Camera);
	}

	const double Time = GetWorld()->GetTimeSeconds();
	TArray<FTransform> Transforms;
	for (int32 Index = 0; Index < Kinds.Num(); ++Index)
	{
		const FKind& Flying = Kinds[Index];
		UInstancedStaticMeshComponent* Component = Components[Index];
		if (!Component || Flying.Animals.IsEmpty() || Component->GetInstanceCount() != Flying.Animals.Num())
		{
			continue;
		}
		const FHexFlockKind& Kind = Settings->Flocks[Index];
		Transforms.Reset(Flying.Animals.Num());
		for (const FAnimal& Animal : Flying.Animals)
		{
			Transforms.Add(GetTransform(Kind, Flying.Flocks[Animal.Flock], Animal, Time, Camera));
		}
		// the transforms alone go to the renderer (MarkRenderInstancesDirty), as marking the whole render state dirty would make the proxy anew
		// every frame
		Component->BatchUpdateInstancesTransforms(0, Transforms, true, false, false);
		Component->MarkRenderInstancesDirty();
	}
}
