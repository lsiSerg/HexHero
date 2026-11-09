// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/UObjectIterator.h"
#include "Engine/DirectionalLight.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HexHero.h"
#include "HexCameraConfig.h"
#include "HexCameraPawn.h"
#include "HexInputConfig.h"
#include "HexLastView.h"
#include "HexMapSettings.h"
#include "HexPlanet.h"
#include "HexPlanetCameraConfig.h"
#include "HexPlanetFile.h"
#include "HexPlanetPlayerController.h"
#include "HexWorldMap.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	TAutoConsoleVariable<bool> CVarHexPlanetSun(TEXT("hex.PlanetSun"), true,
		TEXT("Moves the sun of the planet lazily after the point the camera looks at, so the half it sees is lit. 0 leaves it where it is."));

	void PlanetFocusCommand(const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		AHexPlanetCameraPawn* CameraPawn = PlayerController ? Cast<AHexPlanetCameraPawn>(PlayerController->GetPawn()) : nullptr;

		int32 Cell = 0;
		float Distance = 0.0f;
		TOptional<float> OptionalDistance;

		if (Args.IsEmpty() || !LexTryParseString(Cell, *Args[0]) || (Args.IsValidIndex(1) && !LexTryParseString(Distance, *Args[1])))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetFocus <Cell> [Distance], where cells 0 to 11 are the pentagons"));
			return;
		}

		if (Args.IsValidIndex(1))
		{
			OptionalDistance = Distance;
		}

		if (!CameraPawn || !CameraPawn->FocusCell(Cell, OptionalDistance))
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetFocus: no planet camera, or no cell %d"), Cell);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs PlanetFocusConsoleCommand(TEXT("hex.PlanetFocus"),
		TEXT("Glides the planet camera to a cell, where cells 0 to 11 are the pentagons. Arguments: the cell, then optionally the distance from it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlanetFocusCommand));

	void LoadPlanetCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		FString Folder;

		if (Args.Num() > 1)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.LoadPlanet [Folder], where the folder is one in Content/Data/Planets"));
			return;
		}

		if (Args.Num() == 1)
		{
			const TArray<FHexPlanetEntry> Planets = HexPlanetFile::FindPlanets();
			const FHexPlanetEntry* Planet = Planets.FindByPredicate([&Args](const FHexPlanetEntry& Entry) { return Entry.Folder.Equals(Args[0], ESearchCase::IgnoreCase); });

			if (!Planet)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.LoadPlanet: there is no planet %s in %s"), *Args[0], HexPlanetFile::RootFolder);
				return;
			}

			Folder = Planet->Folder;
		}

		// the planet already open stays, so a camera command after this one moves the camera at once; both layers have its folder
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		const AHexPlanet* Open = Layers ? Layers->GetSurface() : nullptr;

		if (!Open || (!Folder.IsEmpty() && !Open->GetPlanetFolder().Equals(Folder, ESearchCase::IgnoreCase)))
		{
			HexPlanetFile::OpenPlanet(World, Folder);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs LoadPlanetConsoleCommand(TEXT("hex.LoadPlanet"),
		TEXT("Opens a planet, a folder in Content/Data/Planets, or without one the planet of the last view, unless it is already open."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoadPlanetCommand));

	/** Returns true if the planet camera of the last view belongs to this planet; a file from before planets had folders has it on any */
	bool IsSavedOnPlanet(const FHexLastView& View, const AHexPlanet& Planet)
	{
		return View.Planet.Distance > 0.0 && (View.Planet.Planet.IsEmpty() || View.Planet.Planet.Equals(Planet.GetPlanetFolder(), ESearchCase::IgnoreCase));
	}

	/** Place of the planet camera that hex.PlanetSetCamera asked for, and its layer, kept until a camera has its planet, which may come only
	 *  after the level opens */
	TOptional<FHexPlanetCameraPlacement> PendingPlanetPlacement;
	TOptional<EHexPlanetLayer> PendingPlanetLayer;

	void PlanetSetCameraCommand(const TArray<FString>& Args)
	{
		FHexPlanetCameraPlacement Placement;

		// the layer may come last, as hex.CopyCamera writes it
		TArray<FString> Numbers = Args;
		EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
		const bool bLayer = Numbers.Num() > 2 && ParsePlanetLayer(Numbers.Last(), Layer);
		if (bLayer)
		{
			Numbers.Pop();
		}

		if (Numbers.Num() < 2 || Numbers.Num() > 4 || !LexTryParseString(Placement.Latitude, *Numbers[0]) || !LexTryParseString(Placement.Longitude, *Numbers[1])
			|| (Numbers.IsValidIndex(2) && !LexTryParseString(Placement.Heading, *Numbers[2]))
			|| (Numbers.IsValidIndex(3) && !LexTryParseString(Placement.Distance, *Numbers[3])))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetSetCamera <Latitude> <Longitude> [Heading] [Distance] [Surface|Underground], in degrees, where heading 0 looks north"));
			return;
		}

		// a line without a layer is of the surface, as hex.CopyCamera writes it there and the last view reads it, so it brings the view back
		// from the underground too
		PendingPlanetPlacement = Placement;
		PendingPlanetLayer = Layer;
	}

	FAutoConsoleCommand PlanetSetCameraConsoleCommand(TEXT("hex.PlanetSetCamera"),
		TEXT("Puts the planet camera at once over a point, as hex.CopyCamera gives it. Arguments: latitude and longitude in degrees, ")
		TEXT("then optionally the heading in degrees clockwise from north, the distance and the layer, Surface (when left out) or Underground, ")
		TEXT("which it travels to from the other layer. After hex.LoadPlanet it waits until the planet is built."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PlanetSetCameraCommand));

	/** Goes to a layer of the planet at the same place, as the key of the layer does elsewhere than over an entrance */
	void PlanetLayerCommand(const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		AHexPlanetCameraPawn* CameraPawn = PlayerController ? Cast<AHexPlanetCameraPawn>(PlayerController->GetPawn()) : nullptr;
		EHexPlanetLayer Layer = EHexPlanetLayer::Surface;

		if (Args.Num() != 1 || !ParsePlanetLayer(Args[0], Layer))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.PlanetLayer <Surface|Underground>"));
			return;
		}
		if (!CameraPawn || !CameraPawn->GetPlanet() || !CameraPawn->GetPlanet()->IsBuilt())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetLayer: no planet camera, or its planet is not built yet"));
			return;
		}
		if (CameraPawn->IsTravelling())
		{
			UE_LOG(LogHexHero, Log, TEXT("hex.PlanetLayer: a trip between the layers runs, try again after it"));
			return;
		}
		if (CameraPawn->GetShownLayer() == Layer)
		{
			UE_LOG(LogHexHero, Log, TEXT("hex.PlanetLayer: the %s is shown already"), GetPlanetLayerName(Layer));
			return;
		}
		if (!CameraPawn->TravelTo(Layer))
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetLayer: cannot go to the %s now"), GetPlanetLayerName(Layer));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs PlanetLayerConsoleCommand(TEXT("hex.PlanetLayer"),
		TEXT("hex.PlanetLayer <Surface|Underground>: goes to a layer of the planet at the same place, through a fade to black"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlanetLayerCommand));

	/** Logs what of each layer is in the scene, where the hidden one must have nothing, the sky lights in the scene and the values of the sun */
	void PlanetLayerCheckCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		if (!Layers || !Layers->GetSurface())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetLayerCheck: no planet"));
			return;
		}

		int32 Problems = 0;
		for (const EHexPlanetLayer Layer : {EHexPlanetLayer::Surface, EHexPlanetLayer::Underground})
		{
			const AHexPlanet* Planet = Layers->GetPlanet(Layer);
			if (!Planet)
			{
				UE_LOG(LogHexHero, Log, TEXT("Layer check: the %s has no planet"), GetPlanetLayerName(Layer));
				continue;
			}

			// a primitive is in the scene while it has a scene proxy
			TInlineComponentArray<UPrimitiveComponent*> Primitives(Planet);
			int32 InScene = 0;
			TArray<FString> Names;
			for (const UPrimitiveComponent* Primitive : Primitives)
			{
				if (Primitive->SceneProxy)
				{
					++InScene;
					if (Names.Num() < 12)
					{
						Names.Add(Primitive->GetName());
					}
				}
			}
			const bool bShown = Planet->IsPresented();
			UE_LOG(LogHexHero, Log, TEXT("Layer check: the %s is %s, %s, %d of its %d primitives are in the scene%s%s"), GetPlanetLayerName(Layer),
				bShown ? TEXT("shown") : TEXT("hidden"), Planet->IsBuilt() ? TEXT("built") : Planet->IsDataLoaded() ? TEXT("with its data, its meshes not built") : TEXT("without its data yet"),
				InScene, Primitives.Num(), Names.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Names, TEXT(", ")));
			if (!bShown && InScene > 0)
			{
				UE_LOG(LogHexHero, Error, TEXT("Layer check: the hidden %s has %d primitives in the scene"), GetPlanetLayerName(Layer), InScene);
				++Problems;
			}

			// the panel at the cursor for a cell, as it reads on this layer
			int32 Cell = INDEX_NONE;
			if (!Args.IsEmpty() && LexTryParseString(Cell, *Args[0]) && Planet->IsBuilt() && Planet->GetGrid().GetCells().IsValidIndex(Cell))
			{
				UE_LOG(LogHexHero, Log, TEXT("Layer check: cell %d on the %s reads:\n%s"), Cell, GetPlanetLayerName(Layer), *Planet->GetCellDebugText(Cell));

				// picking from the camera underground, where it follows the triangles: rays to the ground of the cells up to three rings around the
				// cell, in their middles and towards the middles of their sides, on a void down its cliffs, must find the cell where they reach
				// that ground, or a cell in front of it that hides it, and never pass through the ground into a cell behind. The surface picks by
				// the tops of the cells, so it is not checked
				const APlayerController* Player = World->GetFirstPlayerController();
				if (Layer == EHexPlanetLayer::Underground && Planet->IsPresented() && Player && Player->PlayerCameraManager)
				{
					const FVector CameraLocation = Player->PlayerCameraManager->GetCameraLocation();
					const FHexSphereGrid& Grid = Planet->GetGrid();
					TArray<int32> Checked{Cell};
					for (int32 Ring = 0, First = 0; Ring < 3; ++Ring)
					{
						const int32 Last = Checked.Num();
						for (int32 Index = First; Index < Last; ++Index)
						{
							for (const int32 Neighbor : Grid.GetCells()[Checked[Index]].Neighbors)
							{
								Checked.AddUnique(Neighbor);
							}
						}
						First = Last;
					}
					int32 Rays = 0;
					int32 Found = 0;
					int32 Hidden = 0;
					int32 Wrong = 0;
					int32 Voids = 0;
					for (const int32 Each : Checked)
					{
						const FHexSphereCell& Checking = Grid.GetCells()[Each];
						Voids += Planet->GetCellLevel(Each) < 0 ? 1 : 0;
						TArray<FVector3d> Directions{Checking.Center};
						for (int32 Side = 0; Side < Checking.Corners.Num(); ++Side)
						{
							const FVector3d Middle = (Grid.GetCorners()[Checking.Corners[(Side + Checking.Corners.Num() - 1) % Checking.Corners.Num()]]
								+ Grid.GetCorners()[Checking.Corners[Side]]).GetSafeNormal();
							for (const double Share : {0.5, 0.8})
							{
								Directions.Add(FMath::Lerp(Checking.Center, Middle, Share).GetSafeNormal());
							}
						}
						for (const FVector3d& Direction : Directions)
						{
							const FVector Target(Direction * Planet->FindGroundRadius(Each, Direction));
							FVector Hit = Target;
							const int32 Picked = Planet->FindCellAlongRay(CameraLocation, Target - CameraLocation, &Hit);
							const double Short = FVector::Distance(CameraLocation, Target) - FVector::Distance(CameraLocation, Hit);
							++Rays;
							if (Picked == Each && FMath::Abs(Short) < 1.0)
							{
								++Found;
							}
							else if (Picked != INDEX_NONE && Short >= 1.0)
							{
								++Hidden;
							}
							else
							{
								++Wrong;
								UE_LOG(LogHexHero, Warning, TEXT("Layer check: a ray to %s on cell %d picked cell %d %.1f short of it"), *Target.ToString(), Each, Picked, Short);
							}
						}
					}
					UE_LOG(LogHexHero, Log, TEXT("Layer check: picking on the %s, %d cells up to three rings around cell %d (%d voids), %d rays to their ground: %d found their cell, %d met a cell in front of it, %d went wrong"),
						GetPlanetLayerName(Layer), Checked.Num(), Cell, Voids, Rays, Found, Hidden, Wrong);
					Problems += Wrong;
				}
			}
		}

		// the sky lights the scene could take, of which there must be one
		int32 SkyLights = 0;
		for (TObjectIterator<USkyLightComponent> It; It; ++It)
		{
			// as USkyLightComponent::CreateRenderState_Concurrent decides, which also hides it with its actor
			const bool bValid = It->SourceType != SLS_SpecifiedCubemap || It->Cubemap != nullptr || It->IsRealTimeCaptureEnabled();
			if (It->GetWorld() == World && It->IsRegistered() && It->bAffectsWorld && bValid && It->ShouldComponentAddToScene() && It->ShouldRender())
			{
				++SkyLights;
				const AActor* Owner = It->GetOwner();
				const AHexPlanet* OwnerPlanet = Cast<AHexPlanet>(Owner);
				UE_LOG(LogHexHero, Log, TEXT("Layer check: sky light %s of %s%s, intensity %.2f, color %s"), *It->GetName(), Owner ? *Owner->GetName() : TEXT("nothing"),
					OwnerPlanet ? *FString::Printf(TEXT(" (the %s)"), GetPlanetLayerName(OwnerPlanet->GetLayer())) : TEXT(""), It->Intensity, *It->GetLightColor().ToString());
			}
		}
		if (SkyLights != 1)
		{
			UE_LOG(LogHexHero, Error, TEXT("Layer check: %d sky lights are in the scene, not one"), SkyLights);
			++Problems;
		}

		// the sun, the exposure and what the materials read of the sun
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			const ULightComponent* Light = It->GetLightComponent();
			UE_LOG(LogHexHero, Log, TEXT("Layer check: sun %s shines from %s, intensity %.2f, color %s"), *It->GetName(), *(-It->GetActorForwardVector()).ToString(),
				Light ? Light->Intensity : 0.0f, Light ? *Light->GetLightColor().ToString() : TEXT("none"));
		}
		const APlayerController* PlayerController = World->GetFirstPlayerController();
		if (const AHexPlanetCameraPawn* CameraPawn = PlayerController ? Cast<AHexPlanetCameraPawn>(PlayerController->GetPawn()) : nullptr)
		{
			UE_LOG(LogHexHero, Log, TEXT("Layer check: the camera shows the %s with exposure bias %.2f%s"), GetPlanetLayerName(CameraPawn->GetShownLayer()),
				CameraPawn->GetExposureBias(), CameraPawn->IsTravelling() ? TEXT(", travelling") : TEXT(""));
		}
		if (const AHexPlanet* Shown = Layers->GetPresented(); Shown && Shown->ParameterCollection.Get())
		{
			if (const UMaterialParameterCollectionInstance* Parameters = World->GetParameterCollectionInstance(Shown->ParameterCollection.Get()))
			{
				FLinearColor SunDirection;
				float PlanetRadius = 0.0f;
				Parameters->GetVectorParameterValue(TEXT("SunDirection"), SunDirection);
				Parameters->GetScalarParameterValue(TEXT("PlanetRadius"), PlanetRadius);
				UE_LOG(LogHexHero, Log, TEXT("Layer check: MPC_HexPlanet has SunDirection %s and PlanetRadius %.0f"), *SunDirection.ToString(), PlanetRadius);
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("Layer check: %s"), Problems == 0 ? TEXT("nothing of the hidden layer is in the scene") : *FString::Printf(TEXT("%d problems"), Problems));
	}

	FAutoConsoleCommandWithWorldAndArgs PlanetLayerCheckConsoleCommand(TEXT("hex.PlanetLayerCheck"),
		TEXT("hex.PlanetLayerCheck [Cell]: logs the primitives of each layer of the planet in the scene, where the hidden one must have none, the sky lights in the scene and the values of the sun, and with a cell the panel at the cursor for it on each layer that is built"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlanetLayerCheckCommand));

	/** Returns the planet camera saved in the last view */
	FHexPlanetCameraPlacement GetSavedPlacement(const FHexLastView& View)
	{
		FHexPlanetCameraPlacement Placement;
		Placement.Latitude = View.Planet.Latitude;
		Placement.Longitude = View.Planet.Longitude;
		Placement.Heading = View.Planet.Heading;
		Placement.Distance = View.Planet.Distance;
		return Placement;
	}

	/** Returns north along the surface at a point, or the X axis at the poles */
	FVector GetNorthAt(const FVector& Up)
	{
		const FVector North = FVector::UpVector - Up * Up.Z;
		return North.SizeSquared() > 1.0e-8 ? North.GetSafeNormal() : (FVector::ForwardVector - Up * Up.X).GetSafeNormal();
	}
}

AHexPlanetCameraPawn::AHexPlanetCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// the camera sets its own rotation, which the controller would turn back each frame
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);

	// the same fixed exposure as the flat map, so colors do not change with how much of the screen the planet fills
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	// its bias comes from the planet camera config in BeginPlay
}

const UHexPlanetCameraConfig* AHexPlanetCameraPawn::Settings() const
{
	return LoadedPlanetCameraConfig ? LoadedPlanetCameraConfig.Get() : GetDefault<UHexPlanetCameraConfig>();
}

void AHexPlanetCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	LoadedPlanetCameraConfig = PlanetCameraConfig.LoadSynchronous();
	if (!LoadedPlanetCameraConfig)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet camera: config %s is missing, run Tools/create_planet_assets.py"), *PlanetCameraConfig.ToString());
	}
	Camera->PostProcessSettings.AutoExposureBias = Settings()->ExposureBias;

	CameraConfig = GetDefault<UHexMapSettings>()->LoadCameraConfig();
	EdgeScrollCurve = CameraConfig->EdgeScrollCurve.LoadSynchronous();
	AHexCameraPawn::InitCameraMode(*CameraConfig);

	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		Sun = *It;
		break;
	}

	// the light of the surface keeps the sun as the level has it, which the underground changes
	if (const ULightComponent* Light = Sun ? Sun->GetLightComponent() : nullptr)
	{
		LevelSunColor = Light->GetLightColor();
		LevelSunIntensity = Light->Intensity;
	}
}

EHexPlanetLayer AHexPlanetCameraPawn::GetShownLayer() const
{
	return Planet ? Planet->GetLayer() : EHexPlanetLayer::Surface;
}

float AHexPlanetCameraPawn::GetExposureBias() const
{
	return Camera->PostProcessSettings.AutoExposureBias;
}

void AHexPlanetCameraPawn::ApplyLighting(EHexPlanetLayer Layer)
{
	const FHexLayerLighting Lighting = Settings()->GetLighting(Layer);
	Camera->PostProcessSettings.AutoExposureBias = Lighting.ExposureBias;

	if (ULightComponent* Light = Sun ? Sun->GetLightComponent() : nullptr)
	{
		const bool bLevel = Lighting.SunIntensity <= 0.0f;
		Light->SetIntensity(bLevel ? LevelSunIntensity : Lighting.SunIntensity);
		Light->SetLightColor(bLevel ? LevelSunColor : Lighting.SunColor);
	}
	// the sun of the new light stands where it should at once
	SunFrom = FVector::ZeroVector;
	bSunPlaced = false;
}

FVector AHexPlanetCameraPawn::GetCameraLocationFor(const FHexPlanetCameraPlacement& Placement) const
{
	const double Distance = Placement.Distance > 0.0 ? FMath::Clamp(Placement.Distance, double(Settings()->MinDistance), GetMaxDistance()) : TargetDistance;
	return GetCameraLocationAt(Placement.ToOrientation(), Distance);
}

bool AHexPlanetCameraPawn::TravelTo(EHexPlanetLayer Layer, TOptional<FHexPlanetCameraPlacement> Placement, bool bStartBlack)
{
	const UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld());
	AHexPlanet* Target = LayerSet ? LayerSet->GetPlanet(Layer) : nullptr;
	if (IsTravelling() || !Target || !Planet || !Planet->IsBuilt())
	{
		return false;
	}
	if (Target == Planet)
	{
		if (Placement.IsSet())
		{
			SetPlacement(Placement.GetValue());
		}
		return true;
	}

	// the keys held now would get no release while the screen is black, and a stroke, drag or turn ends here
	AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController());
	if (PlayerController)
	{
		PlayerController->FlushPressedKeys();
		// the brush stays on and paints the layer the trip goes to, once it is shown (UHexPlanetPainter::SetTarget)
		PlayerController->EndPaintStroke();
	}
	bDragging = false;
	bTurning = false;
	bFocusing = false;
	bMiddleDown = false;
	MoveInput = FVector2D::ZeroVector;
	TurnKeyInput = 0.0f;
	AngularVelocity = FVector::ZeroVector;

	TravelLayer = Layer;
	TravelPlacement = Placement.Get(GetPlacement());
	if (TravelPlacement.Distance <= 0.0)
	{
		TravelPlacement.Distance = TargetDistance;
	}

	// a jump on the layer left behind that still waits for its pieces gives way to the trip, and that layer, hidden, streams nothing for it
	if (PendingPlanetPlacement.IsSet() && PendingPlanetLayer.Get(EHexPlanetLayer::Surface) == Planet->GetLayer())
	{
		UE_LOG(LogHexHero, Log, TEXT("Travel: the jump of hex.PlanetSetCamera on the %s that waited %.0f ms is dropped"), GetPlanetLayerName(Planet->GetLayer()), JumpWaitTime * 1000.0f);
		PendingPlanetPlacement.Reset();
		PendingPlanetLayer.Reset();
	}
	JumpWaitTime = 0.0f;
	Planet->SetPiecePrefetch({});

	// the hidden layer builds the pieces around the place while the screen fades, so it is whole when it is shown; a layer not built yet
	// builds all of it when it is first shown
	Target->SetPiecePrefetch(GetCameraLocationFor(TravelPlacement));

	TravelPhase = bStartBlack ? ETravelPhase::Black : ETravelPhase::FadingOut;
	TravelPhaseTime = 0.0f;
	TravelBlackFrames = 0;
	bTravelSwapped = false;
	TravelStartTime = FPlatformTime::Seconds();
	TravelSwapMs = TravelReadyMs = 0.0;
	if (APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr)
	{
		CameraManager->StartCameraFade(bStartBlack ? 1.0f : 0.0f, 1.0f, bStartBlack ? 0.0f : Settings()->TravelFadeOutTime, FLinearColor::Black, false, true);
	}
	if (bStartBlack && PlayerController)
	{
		PlayerController->SetTravelScreen(true);
	}
	UE_LOG(LogHexHero, Log, TEXT("Travel from the %s to the %s at %.4f %.4f, heading %.1f at %.0f%s"), GetPlanetLayerName(Planet->GetLayer()), GetPlanetLayerName(Layer),
		TravelPlacement.Latitude, TravelPlacement.Longitude, TravelPlacement.Heading, TravelPlacement.Distance, Target->IsBuilt() ? TEXT("") : TEXT(", which builds its meshes"));
	return true;
}

void AHexPlanetCameraPawn::UpdateTravel(float DeltaTime)
{
	const UHexPlanetCameraConfig* Config = Settings();
	AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController());
	UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld());
	AHexPlanet* Target = LayerSet ? LayerSet->GetPlanet(TravelLayer) : nullptr;
	TravelPhaseTime += DeltaTime;

	if (!Target)
	{
		// the layer went away, as when the level closes
		TravelPhase = ETravelPhase::None;
		if (PlayerController)
		{
			PlayerController->SetTravelScreen(false);
			if (PlayerController->PlayerCameraManager)
			{
				PlayerController->PlayerCameraManager->StopCameraFade();
			}
		}
		return;
	}

	switch (TravelPhase)
	{
	case ETravelPhase::FadingOut:
		// the black screen covers the widgets too, which the fade of the camera does not
		if (TravelPhaseTime >= Config->TravelFadeOutTime)
		{
			TravelPhase = ETravelPhase::Black;
			TravelPhaseTime = 0.0f;
			TravelBlackFrames = 0;
			if (PlayerController)
			{
				PlayerController->SetTravelScreen(true);
			}
		}
		break;

	case ETravelPhase::Black:
	{
		++TravelBlackFrames;
		const FVector CameraLocation = GetCameraLocationFor(TravelPlacement);
		if (!bTravelSwapped)
		{
			// once the black screen has been drawn, and the hidden layer has streamed the pieces around the place, which a layer not built has
			// none of yet; at most JumpWaitMax
			const bool bReady = !Target->IsBuilt() || Target->ArePiecesReadyAt(CameraLocation);
			if (TravelBlackFrames < 3 || (!bReady && TravelPhaseTime < Config->JumpWaitMax))
			{
				break;
			}
			TravelReadyMs = (FPlatformTime::Seconds() - TravelStartTime) * 1000.0;
			const double SwapStart = FPlatformTime::Seconds();
			LayerSet->Present(TravelLayer);
			Planet = Target;
			Target->SetPiecePrefetch({});
			ApplyLighting(TravelLayer);
			SetPlacement(TravelPlacement);
			TravelSwapMs = (FPlatformTime::Seconds() - SwapStart) * 1000.0;
			TravelSwapPhaseTime = TravelPhaseTime;
			bTravelSwapped = true;
			if (!bReady)
			{
				UE_LOG(LogHexHero, Warning, TEXT("Travel: the %s did not have all the pieces around the place after %.0f ms"), GetPlanetLayerName(TravelLayer), Config->JumpWaitMax * 1000.0f);
			}
			break;
		}

		// the layer shown holds black at least TravelHoldMin, and until its pieces around the camera are there
		const bool bReady = Target->ArePiecesReadyAt(CameraLocation) || TravelPhaseTime - TravelSwapPhaseTime >= Config->JumpWaitMax;
		if (TravelPhaseTime < Config->TravelHoldMin || !bReady)
		{
			break;
		}
		TravelPhase = ETravelPhase::FadingIn;
		TravelPhaseTime = 0.0f;
		if (PlayerController)
		{
			PlayerController->SetTravelScreen(false);
			if (PlayerController->PlayerCameraManager)
			{
				PlayerController->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, Config->TravelFadeInTime, FLinearColor::Black, false, false);
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("Travel: the %s is shown after %.0f ms, the old layer had faded and the new one had its pieces at %.0f ms, the swap took %.0f ms"),
			GetPlanetLayerName(TravelLayer), (FPlatformTime::Seconds() - TravelStartTime) * 1000.0, TravelReadyMs, TravelSwapMs);
		break;
	}

	case ETravelPhase::FadingIn:
		if (TravelPhaseTime >= Config->TravelFadeInTime)
		{
			TravelPhase = ETravelPhase::None;
		}
		break;

	default:
		break;
	}
}

void AHexPlanetCameraPawn::OnLayer()
{
	AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController());
	if (IsTravelling() || !Planet || !Planet->IsBuilt())
	{
		return;
	}
	const EHexPlanetLayer Target = Planet->GetLayer() == EHexPlanetLayer::Surface ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface;
	FHexPlanetCameraPlacement Placement = GetPlacement();

	// an entrance under the mouse leads down to its exit, the same cell below, and an exit up to its entrance, which the camera then looks at
	const int32 Cell = PlayerController ? PlayerController->GetMouseCell() : INDEX_NONE;
	const int32 Entrance = Planet->FindEntrance(Cell);
	if (Entrance != INDEX_NONE && Planet->GetEntranceProblem(Entrance) == HexPlanetUnderground::EEntranceProblem::None)
	{
		const FVector3d Center = Planet->GetGrid().GetCells()[Cell].Center;
		Placement.Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Center.Z, -1.0, 1.0)));
		Placement.Longitude = FMath::RadiansToDegrees(FMath::Atan2(Center.Y, Center.X));
		UE_LOG(LogHexHero, Log, TEXT("Through the %s %c at cell %d"), Target == EHexPlanetLayer::Underground ? TEXT("entrance") : TEXT("exit"),
			HexPlanetFile::GetNetworkLetter(Planet->GetEntrances()[Entrance].Network), Cell);
	}
	TravelTo(Target, Placement);
}

void AHexPlanetCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();

	if (!InputConfig->MappingContext)
	{
		return;
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AHexPlanetCameraPawn::OnMove);
		EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Completed, this, &AHexPlanetCameraPawn::OnMove);
		EnhancedInputComponent->BindAction(InputConfig->ZoomAction, ETriggerEvent::Triggered, this, &AHexPlanetCameraPawn::OnZoom);
		EnhancedInputComponent->BindAction(InputConfig->TurnKeysAction, ETriggerEvent::Triggered, this, &AHexPlanetCameraPawn::OnTurnKeys);
		EnhancedInputComponent->BindAction(InputConfig->TurnKeysAction, ETriggerEvent::Completed, this, &AHexPlanetCameraPawn::OnTurnKeys);
		EnhancedInputComponent->BindAction(InputConfig->DragAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnDragStarted);
		EnhancedInputComponent->BindAction(InputConfig->DragAction, ETriggerEvent::Completed, this, &AHexPlanetCameraPawn::OnDragCompleted);
		EnhancedInputComponent->BindAction(InputConfig->TurnAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnTurnStarted);
		EnhancedInputComponent->BindAction(InputConfig->TurnAction, ETriggerEvent::Completed, this, &AHexPlanetCameraPawn::OnTurnCompleted);
		EnhancedInputComponent->BindAction(InputConfig->FocusHexAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnMiddleStarted);
		EnhancedInputComponent->BindAction(InputConfig->FocusHexAction, ETriggerEvent::Completed, this, &AHexPlanetCameraPawn::OnMiddleCompleted);
		EnhancedInputComponent->BindAction(InputConfig->MapMenuAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnBackToWorldMap);
		EnhancedInputComponent->BindAction(InputConfig->CopyCameraAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnCopyCamera);
		EnhancedInputComponent->BindAction(InputConfig->KeyLegendAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnToggleKeyLegend);
		EnhancedInputComponent->BindAction(InputConfig->SaveViewAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnSaveView);
		EnhancedInputComponent->BindAction(InputConfig->ToggleGridAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnToggleGrid);
		EnhancedInputComponent->BindAction(InputConfig->FreeCameraAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnToggleFreeCamera);
		EnhancedInputComponent->BindAction(InputConfig->WireframeModeAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnCycleWireframeMode);
		EnhancedInputComponent->BindAction(InputConfig->WireframeScopeAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnCycleWireframeScope);
		EnhancedInputComponent->BindAction(InputConfig->WireframeTargetAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnCycleWireframeTarget);
		EnhancedInputComponent->BindAction(InputConfig->WaterViewAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnCycleWaterView);

		// actions added later are missing until Tools/create_input_assets.py has made them
		if (InputConfig->SunFollowAction)
		{
			EnhancedInputComponent->BindAction(InputConfig->SunFollowAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnToggleSunFollowing);
		}
		if (InputConfig->LayerAction)
		{
			EnhancedInputComponent->BindAction(InputConfig->LayerAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnLayer);
		}

		// painting the levels, whose actions are missing until Tools/create_input_assets.py has made them
		if (InputConfig->PaintModeAction && InputConfig->PaintLevelAction && InputConfig->BrushSizeAction && InputConfig->UndoAction && InputConfig->RedoAction)
		{
			EnhancedInputComponent->BindAction(InputConfig->PaintModeAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnTogglePaintMode);
			EnhancedInputComponent->BindAction(InputConfig->PaintLevelAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnPaintLevel);
			EnhancedInputComponent->BindAction(InputConfig->BrushSizeAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnBrushSize);
			EnhancedInputComponent->BindAction(InputConfig->UndoAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnUndo);
			EnhancedInputComponent->BindAction(InputConfig->RedoAction, ETriggerEvent::Started, this, &AHexPlanetCameraPawn::OnRedo);
		}
	}

	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputConfig->MappingContext, 0);
		}
	}
}

void AHexPlanetCameraPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// the layer shown, which a trip changes
	if (!Planet)
	{
		const UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld());
		Planet = LayerSet ? LayerSet->GetPresented() : nullptr;
	}

	// the planet builds itself in its BeginPlay, which may come after this one
	if (!Planet || Planet->GetRadius() <= 0.0)
	{
		return;
	}

	if (TargetDistance <= 0.0)
	{
		TargetDistance = CurrentDistance = GetMaxDistance();
		ApplyLighting(Planet->GetLayer());

		// the camera saved with the view key, unless a command asks for another place
		FHexLastView LastView;

		// on another planet the view counts as not saved
		const bool bSaved = HexLastView::Load(LastView) && IsSavedOnPlanet(LastView, *Planet);
		if (bSaved)
		{
			SavedPlacement = GetSavedPlacement(LastView);
			SavedLayer = LastView.Planet.Layer;
		}

		if (bSaved && !PendingPlanetPlacement.IsSet() && PendingFocusCell == INDEX_NONE)
		{
			SetPlacement(SavedPlacement);

			// a view saved underground goes there behind black, as the planet comes from its loading screen
			if (SavedLayer != Planet->GetLayer())
			{
				TravelTo(SavedLayer, SavedPlacement, true);
			}
		}
	}

	// a trip between the layers holds the camera where it is until it ends
	if (IsTravelling())
	{
		UpdateTravel(DeltaTime);
		UpdateCameraPosition();
		UpdateSun(DeltaTime);
		return;
	}

	// a place asked for before hex.LoadPlanet opened this level waits until then, as the camera of the level before would be gone at once
	const FWorldContext* WorldContext = GEngine ? GEngine->GetWorldContextFromWorld(GetWorld()) : nullptr;

	// on another layer, the camera travels there, which builds the pieces around the place and waits for them
	if (PendingPlanetPlacement.IsSet() && !(WorldContext && !WorldContext->TravelURL.IsEmpty()) && PendingPlanetLayer.IsSet()
		&& PendingPlanetLayer.GetValue() != Planet->GetLayer())
	{
		const FHexPlanetCameraPlacement Placement = PendingPlanetPlacement.GetValue();
		const EHexPlanetLayer Layer = PendingPlanetLayer.GetValue();
		PendingPlanetPlacement.Reset();
		PendingPlanetLayer.Reset();
		if (!TravelTo(Layer, Placement))
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetSetCamera: the planet has no %s, the camera stays on the %s"), GetPlanetLayerName(Layer), GetPlanetLayerName(Planet->GetLayer()));
			SetPlacement(Placement);
		}
		return;
	}

	if (PendingPlanetPlacement.IsSet() && !(WorldContext && !WorldContext->TravelURL.IsEmpty()))
	{
		// the camera jumps once the planet has built the pieces of the chunks around the place it goes to, which it keeps them only near the
		// camera, so it lands on a whole surface; until then it stays where it is, at most JumpWaitMax
		const FHexPlanetCameraPlacement Placement = PendingPlanetPlacement.GetValue();
		const double Distance = Placement.Distance > 0.0 ? FMath::Clamp(Placement.Distance, double(Settings()->MinDistance), GetMaxDistance()) : TargetDistance;
		const FVector Target = GetCameraLocationAt(Placement.ToOrientation(), Distance);
		const bool bReady = Planet->ArePiecesReadyAt(Target);

		if (!bReady && JumpWaitTime < Settings()->JumpWaitMax)
		{
			Planet->SetPiecePrefetch(Target);
			JumpWaitTime += DeltaTime;
		}
		else
		{
			if (JumpWaitTime > 0.0f)
			{
				UE_LOG(LogHexHero, Log, TEXT("The planet camera jumps after %.0f ms, while the planet built the pieces around its place%s"), JumpWaitTime * 1000.0f,
					bReady ? TEXT("") : TEXT(", not all of them"));
			}
			PendingPlanetPlacement.Reset();
			PendingPlanetLayer.Reset();
			Planet->SetPiecePrefetch({});
			JumpWaitTime = 0.0f;
			SetPlacement(Placement);
		}
	}

	// a focus asked for at startup puts the camera there at once, without a glide
	if (PendingFocusCell != INDEX_NONE)
	{
		const int32 Cell = PendingFocusCell;
		PendingFocusCell = INDEX_NONE;

		if (FocusCell(Cell, PendingFocusDistance))
		{
			RotateTowards(Orientation.GetAxisZ(), FocusDirection);
			CurrentDistance = TargetDistance;
			bFocusing = false;
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetFocus: no cell %d"), Cell);
		}
	}

	CurrentDistance = FMath::FInterpTo(CurrentDistance, TargetDistance, DeltaTime, Settings()->ZoomInterpSpeed);

	const FVector Up = Orientation.GetAxisZ();
	const FVector Forward = Orientation.GetAxisX();
	const FVector Right = Orientation.GetAxisY();

	// the keys and the mouse at the screen edge set the speed the planet rolls at, which it reaches and loses over the inertia of what moved it last, as on the flat map
	FVector TargetVelocity = FVector::ZeroVector;
	const FVector2D EdgeInput = bTurning || bDragging ? FVector2D::ZeroVector
		: AHexCameraPawn::GetEdgeScrollInput(Cast<APlayerController>(GetController()), EdgeScrollCurve, Settings()->EdgeScrollMargin);

	if (!EdgeInput.IsNearlyZero())
	{
		MoveInertia = CameraConfig ? CameraConfig->EdgeScrollInertia : 0.12f;
	}

	if (!MoveInput.IsNearlyZero())
	{
		MoveInertia = CameraConfig ? CameraConfig->KeyboardInertia : 0.1f;
	}

	const FVector2D Input(FMath::Clamp(MoveInput.X + EdgeInput.X, -1.0, 1.0), FMath::Clamp(MoveInput.Y + EdgeInput.Y, -1.0, 1.0));

	if (!Input.IsNearlyZero())
	{
		bFocusing = false;

		// turning the point under the camera towards a direction is turning around the axis across it
		const FVector Direction = (Forward * Input.Y + Right * Input.X).GetClampedToMaxSize(1.0);
		TargetVelocity = FVector::CrossProduct(Up, Direction) * (Settings()->MoveSpeed * CurrentDistance / Planet->GetRadius());
	}

	if (!bDragging && !bFocusing)
	{
		AngularVelocity = FMath::VInterpTo(AngularVelocity, TargetVelocity, DeltaTime, 1.0f / FMath::Max(MoveInertia, 0.01f));

		if (TargetVelocity.IsZero() && AngularVelocity.Size() * Planet->GetRadius() < 1.0)
		{
			AngularVelocity = FVector::ZeroVector;
		}

		const double Speed = AngularVelocity.Size();

		if (Speed > 0.0)
		{
			Orientation = (FQuat(AngularVelocity / Speed, Speed * DeltaTime) * Orientation).GetNormalized();
		}
	}

	if (TurnKeyInput != 0.0f)
	{
		Orientation = (FQuat(Orientation.GetAxisZ(), FMath::DegreesToRadians(Settings()->TurnSpeed * TurnKeyInput * DeltaTime)) * Orientation).GetNormalized();
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	float MouseX = 0.0f;
	float MouseY = 0.0f;

	if (bTurning && PlayerController && PlayerController->GetMousePosition(MouseX, MouseY))
	{
		Orientation = (FQuat(Orientation.GetAxisZ(), FMath::DegreesToRadians((MouseX - LastTurnMouseX) * Settings()->MouseTurnSpeed)) * Orientation).GetNormalized();
		LastTurnMouseX = MouseX;
	}

	UpdateTopDownHeading(DeltaTime);

	// the camera moves so that the ray under the mouse comes back to the grabbed point
	FVector MouseDirection;

	if (bDragging && GetMouseDirection(MouseDirection))
	{
		RotateTowards(MouseDirection, DragDirection);

		// the planet keeps this speed when it is released, so a pause before the release leaves it still
		if (DeltaTime > 0.0f)
		{
			FVector Axis;
			double Angle = 0.0;
			FQuat::FindBetweenNormals(MouseDirection.GetSafeNormal(), DragDirection.GetSafeNormal()).ToAxisAndAngle(Axis, Angle);
			const FVector DragVelocity = Angle > 1.0e-9 ? Axis * (Angle / DeltaTime) : FVector::ZeroVector;
			AngularVelocity = FMath::VInterpTo(AngularVelocity, DragVelocity, DeltaTime, CameraConfig ? CameraConfig->DragReleaseSmoothing : 15.0f);
		}
	}

	if (bFocusing)
	{
		AngularVelocity = FVector::ZeroVector;
		const FVector CurrentUp = Orientation.GetAxisZ();
		RotateTowards(CurrentUp, FocusDirection, 1.0 - FMath::Exp(-Settings()->FocusInterpSpeed * DeltaTime));
		bFocusing = FVector::DotProduct(Orientation.GetAxisZ(), FocusDirection) < 1.0 - 1.0e-9;
	}

	UpdateCameraPosition();
	UpdateSun(DeltaTime);
}

void AHexPlanetCameraPawn::UpdateTopDownHeading(float DeltaTime)
{
	if (AHexCameraPawn::IsFreeCamera() || !CameraConfig)
	{
		return;
	}

	const double Heading = FHexPlanetCameraPlacement::FromOrientation(Orientation, CurrentDistance).Heading;
	const double MaxTurn = CameraConfig->MaxCameraTurn;

	// released, the camera turns back to north as the flat one turns back to its default angle, and held it glides back within the limit,
	// so a camera that stops being free does not jump
	const double Goal = !bTurning && TurnKeyInput == 0.0f ? 0.0 : FMath::Clamp(Heading, -MaxTurn, MaxTurn);
	const double TargetHeading = FMath::FInterpTo(Heading, Goal, DeltaTime, CameraConfig->CameraTurnInterpSpeed);

	// a turn by a positive angle around the point under the camera lowers the heading, which goes clockwise
	const double Turn = Heading - TargetHeading;

	if (FMath::Abs(Turn) > 1.0e-6)
	{
		Orientation = (FQuat(Orientation.GetAxisZ(), FMath::DegreesToRadians(Turn)) * Orientation).GetNormalized();
	}
}

double AHexPlanetCameraPawn::GetMaxDistance() const
{
	return Planet ? Planet->GetRadius() * Settings()->MaxDistanceRadii : Settings()->MinDistance;
}

double AHexPlanetCameraPawn::GetPitchAtDistance(double Distance) const
{
	// changes evenly with each zoom step, as the distance changes by a share of itself
	const double MaxDistance = FMath::Max(GetMaxDistance(), Settings()->MinDistance * 1.01);
	const double Alpha = FMath::Clamp(FMath::Loge(Distance / Settings()->MinDistance) / FMath::Loge(MaxDistance / Settings()->MinDistance), 0.0, 1.0);
	return FMath::Lerp(Settings()->ClosePitch, Settings()->FarPitch, Alpha);
}

FVector AHexPlanetCameraPawn::GetCameraLocationAt(const FQuat& InOrientation, double Distance) const
{
	const FVector Up = InOrientation.GetAxisZ();
	const FVector Forward = InOrientation.GetAxisX();
	const double Pitch = FMath::DegreesToRadians(GetPitchAtDistance(Distance));
	return Up * (Planet ? Planet->GetRadius() : 0.0) + (Up * FMath::Sin(Pitch) - Forward * FMath::Cos(Pitch)) * Distance;
}

void AHexPlanetCameraPawn::UpdateCameraPosition()
{
	const FVector Up = Orientation.GetAxisZ();
	const FVector Forward = Orientation.GetAxisX();

	const double Pitch = FMath::DegreesToRadians(GetPitchAtDistance(CurrentDistance));
	const FVector Target = Up * Planet->GetRadius();
	const FVector Location = GetCameraLocationAt(Orientation, CurrentDistance);

	// looking straight down, the forward direction of the screen stays the forward direction of the orientation
	const FVector ScreenUp = (Up * FMath::Cos(Pitch) + Forward * FMath::Sin(Pitch)).GetSafeNormal();
	SetActorLocationAndRotation(Location, FRotationMatrix::MakeFromXZ(Target - Location, ScreenUp).Rotator());
}

FVector AHexPlanetCameraPawn::GetIdealSunFrom() const
{
	const UHexPlanetCameraConfig* Config = Settings();
	const FVector Up = Orientation.GetAxisZ();
	const FVector North = GetNorthAt(Up);
	// the right of the screen of a camera facing north, as the orientation has it
	const FVector Right = FVector::CrossProduct(Up, North);

	// the elevation changes with the zoom as the pitch does, by the light of the layer shown
	const FHexLayerLighting Lighting = Config->GetLighting(GetShownLayer());
	const double MaxDistance = FMath::Max(GetMaxDistance(), Config->MinDistance * 1.01);
	const double Alpha = FMath::Clamp(FMath::Loge(FMath::Max(CurrentDistance, 1.0) / Config->MinDistance) / FMath::Loge(MaxDistance / Config->MinDistance), 0.0, 1.0);
	const double Elevation = FMath::DegreesToRadians(FMath::Lerp(Lighting.SunCloseElevation, Lighting.SunFarElevation, Alpha));
	const double Azimuth = FMath::DegreesToRadians(Lighting.SunAzimuth);

	const FVector Horizontal = North * FMath::Cos(Azimuth) + Right * FMath::Sin(Azimuth);
	return (Horizontal * FMath::Cos(Elevation) + Up * FMath::Sin(Elevation)).GetSafeNormal();
}

void AHexPlanetCameraPawn::UpdateSun(float DeltaTime)
{
	if (!Sun || !Planet || !CVarHexPlanetSun.GetValueOnGameThread())
	{
		// a fixed sun moves on from where it stands once it follows again
		SunFrom = FVector::ZeroVector;
		return;
	}

	const UHexPlanetCameraConfig* Config = Settings();
	const FVector Ideal = GetIdealSunFrom();

	if (SunFrom.IsZero())
	{
		// placed at once the first time, and when it follows again it moves on from where the light stands, so L does not make it jump
		SunFrom = SunMoveStart = bSunPlaced ? FVector(-Sun->GetActorForwardVector()) : Ideal;
		SunMoveAlpha = bSunPlaced ? 0.0f : 1.0f;
		bSunPlaced = true;
	}

	const bool bMoving = bDragging || bFocusing || !AngularVelocity.IsZero() || FMath::Abs(CurrentDistance - TargetDistance) > TargetDistance * 0.01;
	StillTime = bMoving ? 0.0f : StillTime + DeltaTime;

	if (SunMoveAlpha >= 1.0f)
	{
		// the lag allowed is a share of what the camera sees, so it waits for a move of about the same part of the screen at any distance
		const double Allowed = FMath::Min(Config->SunLagShare * CurrentDistance / Planet->GetRadius(), FMath::DegreesToRadians(double(Config->SunLagMaxDegrees)));
		const double Lag = FMath::Acos(FMath::Clamp(FVector::DotProduct(SunFrom, Ideal), -1.0, 1.0));

		if ((Lag > Allowed && StillTime >= Config->SunSettleTime) || Lag > Allowed * Config->SunMaxLagFactor)
		{
			SunMoveStart = SunFrom;
			SunMoveAlpha = 0.0f;
			UE_LOG(LogHexHero, Verbose, TEXT("Sun moves by %.1f degrees; it will shine from forward %.2f, right %.2f, up %.2f of the camera, heading %.1f"),
				FMath::RadiansToDegrees(Lag), FVector::DotProduct(Ideal, Orientation.GetAxisX()), FVector::DotProduct(Ideal, Orientation.GetAxisY()),
				FVector::DotProduct(Ideal, Orientation.GetAxisZ()), FHexPlanetCameraPlacement::FromOrientation(Orientation, CurrentDistance).Heading);
		}
	}

	if (SunMoveAlpha < 1.0f)
	{
		// eases in and out towards where it should stand now, which may still change while it moves
		SunMoveAlpha = FMath::Min(SunMoveAlpha + DeltaTime / Config->SunMoveTime, 1.0f);
		const float Eased = FMath::SmoothStep(0.0f, 1.0f, SunMoveAlpha);
		SunFrom = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(SunMoveStart, Ideal), Eased).RotateVector(SunMoveStart).GetSafeNormal();
	}

	const FRotator Rotation = FRotationMatrix::MakeFromX(-SunFrom).Rotator();

	// a light that does not move keeps the cached shadows
	if (!Sun->GetActorRotation().Equals(Rotation, 1.0e-4))
	{
		Sun->SetActorRotation(Rotation);
	}
}

void AHexPlanetCameraPawn::RotateTowards(const FVector& From, const FVector& To, double Alpha)
{
	FQuat Rotation = FQuat::FindBetweenNormals(From.GetSafeNormal(), To.GetSafeNormal());

	if (Alpha < 1.0)
	{
		Rotation = FQuat::Slerp(FQuat::Identity, Rotation, Alpha);
	}
	Orientation = (Rotation * Orientation).GetNormalized();
}

bool AHexPlanetCameraPawn::GetMouseDirection(FVector& OutDirection) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	FVector Origin;
	FVector Direction;

	if (!Planet || !PlayerController || !PlayerController->DeprojectMousePositionToWorld(Origin, Direction))
	{
		return false;
	}

	const double Distance = FHexSphereGrid::IntersectSphere(Origin, Direction, Planet->GetRadius());

	if (Distance < 0.0)
	{
		return false;
	}

	OutDirection = (Origin + Direction * Distance).GetSafeNormal();
	return true;
}

void AHexPlanetCameraPawn::OnMove(const FInputActionValue& Value)
{
	MoveInput = Value.Get<FVector2D>();
}

void AHexPlanetCameraPawn::OnZoom(const FInputActionValue& Value)
{
	// a trip keeps the distance it goes with
	if (IsTravelling())
	{
		return;
	}

	// scrolling up moves closer
	TargetDistance = FMath::Clamp(TargetDistance * FMath::Pow(1.0f - Settings()->ZoomStep, Value.Get<float>()), Settings()->MinDistance, GetMaxDistance());
}

void AHexPlanetCameraPawn::OnTurnKeys(const FInputActionValue& Value)
{
	TurnKeyInput = Value.Get<float>();
}

void AHexPlanetCameraPawn::OnDragStarted()
{
	// nothing is grabbed while the screen is black between the layers
	if (IsTravelling())
	{
		return;
	}

	// while a tool of the brush is chosen the left button paints, and the camera moves with the keys, the edges of the screen and the other buttons
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()); PlayerController && PlayerController->IsBrushArmed())
	{
		PlayerController->BeginPaintStroke();
		return;
	}

	bDragging = GetMouseDirection(DragDirection);

	if (bDragging)
	{
		AngularVelocity = FVector::ZeroVector;
	}
	bFocusing = bFocusing && !bDragging;
}

void AHexPlanetCameraPawn::OnDragCompleted()
{
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		PlayerController->EndPaintStroke();
	}

	bDragging = false;
	MoveInertia = CameraConfig ? CameraConfig->DragInertia : 0.6f;
}

void AHexPlanetCameraPawn::OnMiddleStarted()
{
	if (IsTravelling())
	{
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	bMiddleDown = PlayerController && PlayerController->GetMousePosition(MouseX, MouseY);
	MiddleStart = FVector2D(MouseX, MouseY);

	// it drags as the left button does without the brush, so the planet moves while painting too
	bDragging = GetMouseDirection(DragDirection);
	if (bDragging)
	{
		AngularVelocity = FVector::ZeroVector;
	}
	bFocusing = bFocusing && !bDragging;
}

void AHexPlanetCameraPawn::OnMiddleCompleted()
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	const bool bClick = bMiddleDown && PlayerController && PlayerController->GetMousePosition(MouseX, MouseY)
		&& FVector2D::Distance(MiddleStart, FVector2D(MouseX, MouseY)) <= Settings()->ClickTolerance;
	bMiddleDown = false;
	bDragging = false;

	if (bClick)
	{
		// a click centers the cell under the mouse, as before, and leaves no spin
		AngularVelocity = FVector::ZeroVector;
		OnFocusCellUnderMouse();
	}
	else
	{
		MoveInertia = CameraConfig ? CameraConfig->DragInertia : 0.6f;
	}
}

void AHexPlanetCameraPawn::OnTurnStarted()
{
	if (IsTravelling())
	{
		return;
	}

	float MouseY = 0.0f;
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	bTurning = PlayerController && PlayerController->GetMousePosition(LastTurnMouseX, MouseY);
}

void AHexPlanetCameraPawn::OnTurnCompleted()
{
	bTurning = false;
}

bool AHexPlanetCameraPawn::FocusCell(int32 Cell, TOptional<float> Distance)
{
	// a command at startup comes before the planet is built, and waits for it
	if (!Planet || Planet->GetRadius() <= 0.0)
	{
		PendingFocusCell = Cell;
		PendingFocusDistance = Distance;
		return Cell >= 0;
	}

	if (!Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return false;
	}

	FocusDirection = FVector(Planet->GetGrid().GetCells()[Cell].Center);
	bFocusing = true;
	// for hex.PlanetSetCamera, which also takes a heading
	UE_LOG(LogHexHero, Log, TEXT("Planet: focus on cell %d at latitude %.4f, longitude %.4f"), Cell,
		FMath::RadiansToDegrees(FMath::Asin(FocusDirection.Z)), FMath::RadiansToDegrees(FMath::Atan2(FocusDirection.Y, FocusDirection.X)));

	if (Distance.IsSet())
	{
		TargetDistance = FMath::Clamp(double(Distance.GetValue()), double(Settings()->MinDistance), GetMaxDistance());
	}
	return true;
}

void AHexPlanetCameraPawn::OnFocusCellUnderMouse()
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	FVector Origin;
	FVector Direction;

	if (!Planet || !PlayerController || !PlayerController->DeprojectMousePositionToWorld(Origin, Direction))
	{
		return;
	}

	const int32 Cell = Planet->FindCellAlongRay(Origin, Direction);

	if (Cell != INDEX_NONE)
	{
		FocusDirection = FVector(Planet->GetGrid().GetCells()[Cell].Center);
		bFocusing = true;
	}
}

void AHexPlanetCameraPawn::OnBackToWorldMap()
{
	// the menu shows over the planet, so no map loads until one is chosen
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		PlayerController->OpenMapMenu();
	}
}

FHexPlanetCameraPlacement FHexPlanetCameraPlacement::FromOrientation(const FQuat& Orientation, double Distance)
{
	const FVector Up = Orientation.GetAxisZ();
	const FVector Forward = Orientation.GetAxisX();
	const FVector North = GetNorthAt(Up);
	const FVector East = FVector::CrossProduct(North, Up);

	FHexPlanetCameraPlacement Placement;
	Placement.Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0)));
	Placement.Longitude = FMath::RadiansToDegrees(FMath::Atan2(Up.Y, Up.X));
	Placement.Heading = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Forward, East), FVector::DotProduct(Forward, North)));
	Placement.Distance = Distance;
	return Placement;
}

FQuat FHexPlanetCameraPlacement::ToOrientation() const
{
	const double LatitudeRadians = FMath::DegreesToRadians(Latitude);
	const double LongitudeRadians = FMath::DegreesToRadians(Longitude);
	const double HeadingRadians = FMath::DegreesToRadians(Heading);

	const FVector Up(FMath::Cos(LatitudeRadians) * FMath::Cos(LongitudeRadians), FMath::Cos(LatitudeRadians) * FMath::Sin(LongitudeRadians), FMath::Sin(LatitudeRadians));
	const FVector North = GetNorthAt(Up);
	const FVector East = FVector::CrossProduct(North, Up);
	const FVector Forward = North * FMath::Cos(HeadingRadians) + East * FMath::Sin(HeadingRadians);
	return FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
}

FHexPlanetCameraPlacement AHexPlanetCameraPawn::GetPlacement() const
{
	return FHexPlanetCameraPlacement::FromOrientation(Orientation, TargetDistance);
}

void AHexPlanetCameraPawn::SetPlacement(const FHexPlanetCameraPlacement& Placement)
{
	Orientation = Placement.ToOrientation();
	AngularVelocity = FVector::ZeroVector;
	bFocusing = false;

	if (Placement.Distance > 0.0)
	{
		TargetDistance = CurrentDistance = FMath::Clamp(Placement.Distance, double(Settings()->MinDistance), GetMaxDistance());
	}

	UpdateCameraPosition();

	// a cut: the lazy sun stands where it should over the new place at once, as it did the first time, rather than gliding there in the dark,
	// and the renderer does not blend the frames of the old place into the new one
	if (CVarHexPlanetSun.GetValueOnGameThread())
	{
		SunFrom = FVector::ZeroVector;
		bSunPlaced = false;
	}
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (PlayerController->PlayerCameraManager)
		{
			PlayerController->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}

	const int32 Cell = Planet ? Planet->GetGrid().FindCell(FVector3d(Orientation.GetAxisZ())) : INDEX_NONE;
	UE_LOG(LogHexHero, Log, TEXT("Put the planet camera over cell %d at %.4f %.4f, heading %.1f at %.0f"),
		Cell, Placement.Latitude, Placement.Longitude, Placement.Heading, TargetDistance);
}

void AHexPlanetCameraPawn::CopyCameraToClipboard() const
{
	const FHexPlanetCameraPlacement Placement = GetPlacement();

	// the console runs commands joined by | one after another; a planet that is not saved has no folder to open, so only the camera is copied
	// the layer only when it is not the surface, so the commands of the surface stay as they were
	const FString SetCamera = FString::Printf(TEXT("hex.PlanetSetCamera %.4f %.4f %.1f %.0f%s"), Placement.Latitude, Placement.Longitude, Placement.Heading, Placement.Distance,
		GetShownLayer() == EHexPlanetLayer::Surface ? TEXT("") : *FString::Printf(TEXT(" %s"), GetPlanetLayerName(GetShownLayer())));
	const FString Commands = Planet && !Planet->IsSaved() ? SetCamera : FString::Printf(TEXT("hex.LoadPlanet %s | %s"), Planet ? *Planet->GetPlanetFolder() : TEXT(""), *SetCamera);

	FPlatformApplicationMisc::ClipboardCopy(*Commands);
	UE_LOG(LogHexHero, Log, TEXT("Copied the camera: %s"), *Commands);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::White, FString::Printf(TEXT("Copied: %s"), *Commands));
	}
}

void AHexPlanetCameraPawn::OnCopyCamera()
{
	AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController());

	// a planet that is not saved could not be opened by the copied command, so the player is asked to save it first
	if (Planet && !Planet->IsSaved() && PlayerController)
	{
		PlayerController->OpenCopyCameraDialog();
		return;
	}

	CopyCameraToClipboard();
}

void AHexPlanetCameraPawn::OnToggleKeyLegend()
{
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		PlayerController->ToggleKeyLegend();
	}
}

FHexPlanetCameraPlacement AHexPlanetCameraPawn::RoundPlacement(const FHexPlanetCameraPlacement& Placement)
{
	FHexPlanetCameraPlacement Rounded;
	Rounded.Latitude = FMath::RoundToDouble(Placement.Latitude * 1.0e4) / 1.0e4;
	Rounded.Longitude = FMath::RoundToDouble(Placement.Longitude * 1.0e4) / 1.0e4;
	Rounded.Heading = FMath::RoundToDouble(Placement.Heading * 10.0) / 10.0;
	Rounded.Distance = FMath::RoundToDouble(Placement.Distance);
	return Rounded;
}

void AHexPlanetCameraPawn::OnSaveView()
{
	SavePlanet(nullptr);
}

void AHexPlanetCameraPawn::SavePlanet(TFunction<void()> OnSaved)
{
	AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController());

	// a new planet is written only now, under the name the player gives it, and its view with it
	if (Planet && !Planet->IsSaved() && PlayerController)
	{
		PlayerController->OpenSavePlanetDialog([this, OnSaved]()
		{
			SaveView();
			if (OnSaved)
			{
				OnSaved();
			}
		});
		return;
	}

	// painted levels and biomes of a saved planet go to its levels.txt and biomes.txt
	if (Planet && Planet->HasUnsavedLevels())
	{
		FString Error;
		const bool bSaved = Planet->SaveLevels(Error);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, bSaved ? FColor::Green : FColor::Red, bSaved
				? FString::Printf(TEXT("Saved the levels and biomes to %s"), *Planet->GetPlanetFolder())
				: FString::Printf(TEXT("Could not save the levels: %s"), *Error));
		}

		if (!bSaved)
		{
			return;
		}
	}

	SaveView();

	if (OnSaved)
	{
		OnSaved();
	}
}

void AHexPlanetCameraPawn::SaveView()
{
	// the camera of the flat map in the file stays as it is
	FHexLastView View;
	HexLastView::Load(View);

	const FHexPlanetCameraPlacement Placement = RoundPlacement(GetPlacement());
	View.bPlanetOpen = true;
	View.Planet.Planet = Planet ? Planet->GetPlanetFolder() : FString();
	View.Planet.Latitude = Placement.Latitude;
	View.Planet.Longitude = Placement.Longitude;
	View.Planet.Heading = Placement.Heading;
	View.Planet.Distance = Placement.Distance;
	View.Planet.Layer = GetShownLayer();

	if (HexLastView::Save(View))
	{
		SavedPlacement = Placement;
		SavedLayer = View.Planet.Layer;

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 2.0f, FColor::Green, FString::Printf(TEXT("Saved the view to %s"), *HexLastView::GetFilePath()));
		}
	}
}

bool AHexPlanetCameraPawn::IsLastViewSaved() const
{
	if (!Planet || TargetDistance <= 0.0)
	{
		return true;
	}

	// the file keeps the place rounded, and a heading of -180 is the same as 180; a trip shows the view it goes to as saved
	if (IsTravelling())
	{
		return true;
	}
	const FHexPlanetCameraPlacement Placement = RoundPlacement(GetPlacement());
	return Placement.Latitude == SavedPlacement.Latitude && Placement.Longitude == SavedPlacement.Longitude && Placement.Distance == SavedPlacement.Distance
		&& FMath::IsNearlyZero(FRotator::NormalizeAxis(Placement.Heading - SavedPlacement.Heading), 0.01) && GetShownLayer() == SavedLayer;
}

bool AHexPlanetCameraPawn::GetViewStatus(TArray<TPair<FString, FString>>& OutLines) const
{
	OutLines.Reset();
	return !CameraConfig || AHexCameraPawn::GetCameraModeStatus(*CameraConfig, OutLines);
}

void AHexPlanetCameraPawn::OnToggleGrid()
{
	if (Planet)
	{
		Planet->SetGridVisible(!Planet->IsGridVisible());
	}
}

void AHexPlanetCameraPawn::OnToggleFreeCamera()
{
	AHexCameraPawn::SetFreeCamera(!AHexCameraPawn::IsFreeCamera());
}

bool AHexPlanetCameraPawn::IsSunFollowing()
{
	return CVarHexPlanetSun.GetValueOnGameThread();
}

void AHexPlanetCameraPawn::SetSunFollowing(bool bFollowing)
{
	CVarHexPlanetSun->Set(bFollowing, ECVF_SetByConsole);
}

bool AHexPlanetCameraPawn::GetSunStatus(TArray<TPair<FString, FString>>& OutLines)
{
	const bool bFollowing = IsSunFollowing();
	OutLines.Reset();
	OutLines.Emplace(TEXT("Sun"), bFollowing ? TEXT("Follows camera") : TEXT("Fixed"));
	return bFollowing;
}

void AHexPlanetCameraPawn::OnToggleSunFollowing()
{
	SetSunFollowing(!IsSunFollowing());
}

void AHexPlanetCameraPawn::OnCycleWireframeMode()
{
	AHexWorldMap::CycleWireframeMode();
}

void AHexPlanetCameraPawn::OnCycleWireframeScope()
{
	AHexWorldMap::CycleWireframeScope();
}

void AHexPlanetCameraPawn::OnCycleWireframeTarget()
{
	AHexWorldMap::CycleWireframeTarget();
}

void AHexPlanetCameraPawn::OnCycleWaterView()
{
	AHexWorldMap::CycleWaterView();
}

void AHexPlanetCameraPawn::OnTogglePaintMode()
{
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		// a drag held now would go on after the left button starts to paint
		bDragging = false;
		PlayerController->SetPaintMode(!PlayerController->IsPaintModeOn());
	}
}

void AHexPlanetCameraPawn::OnPaintLevel(const FInputActionValue& Value)
{
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		// the number keys give 1 for deep sea up to 5 for mountains, underground 1 the void and 2 a passage, and choosing a level turns painting on
		PlayerController->SetPaintLevelKey(FMath::RoundToInt32(Value.Get<float>()));
	}
}

void AHexPlanetCameraPawn::OnBrushSize()
{
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		PlayerController->CycleBrushSize();
	}
}

void AHexPlanetCameraPawn::OnUndo()
{
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		PlayerController->UndoPaint();
	}
}

void AHexPlanetCameraPawn::OnRedo()
{
	if (AHexPlanetPlayerController* PlayerController = Cast<AHexPlanetPlayerController>(GetController()))
	{
		PlayerController->RedoPaint();
	}
}
