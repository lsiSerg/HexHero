// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HexCameraConfig.h"
#include "HexCoord.h"
#include "HexHero.h"
#include "HexInputConfig.h"
#include "HexLastView.h"
#include "HexMapPlayerController.h"
#include "HexMapSettings.h"
#include "HexPlanetCameraPawn.h"
#include "HexWorldMap.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SViewport.h"

namespace
{
	TAutoConsoleVariable<int32> CVarHexCamera(TEXT("hex.Camera"), 0,
		TEXT("Camera mode. 0: top-down, which returns to the default angle, 1: free, which turns all the way around and stays turned."));

	TAutoConsoleVariable<bool> CVarHexEdgeScroll(TEXT("hex.EdgeScroll"), true,
		TEXT("Scrolls the camera while the mouse is near the edge of the screen."));

	/** Reads an optional number of the console command, where a missing argument or - keeps the current value */
	bool ParseOptionalArg(const TArray<FString>& Args, int32 Index, TOptional<float>& OutValue)
	{
		if (!Args.IsValidIndex(Index) || Args[Index] == TEXT("-"))
		{
			return true;
		}

		float Value = 0.0f;

		if (!LexTryParseString(Value, *Args[Index]))
		{
			return false;
		}

		OutValue = Value;
		return true;
	}

	void FocusHexCommand(const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		AHexCameraPawn* CameraPawn = PlayerController ? Cast<AHexCameraPawn>(PlayerController->GetPawn()) : nullptr;

		int32 Column = 0;
		int32 Row = 0;
		TOptional<float> Yaw;
		TOptional<float> Zoom;

		if (Args.Num() < 2 || !LexTryParseString(Column, *Args[0]) || !LexTryParseString(Row, *Args[1]) || !ParseOptionalArg(Args, 2, Yaw) || !ParseOptionalArg(Args, 3, Zoom))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.FocusHex <Column> <Row> [Yaw] [Zoom], where - keeps the current yaw or zoom"));
			return;
		}

		if (!CameraPawn)
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.FocusHex: no hex camera to move"));
			return;
		}

		if (!CameraPawn->FocusHex(FHexCoord::FromOffset(Column, Row), Yaw, Zoom))
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.FocusHex: hex %d, %d is not on the map"), Column, Row);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs FocusHexConsoleCommand(TEXT("hex.FocusHex"),
		TEXT("Moves the camera so that a hex is in the center of the screen. Arguments: column and row as the map file has them, ")
		TEXT("then optionally the turn in degrees away from the default angle, which makes the camera free, and the camera distance. - keeps the current value."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FocusHexCommand));

	/** Place of the camera that hex.SetCamera asked for, kept until a camera has its map, which may come only after the level opens again */
	struct FHexCameraPlacement
	{
		FVector2D Location = FVector2D::ZeroVector;
		TOptional<float> Yaw;
		TOptional<float> Zoom;
	};

	TOptional<FHexCameraPlacement> PendingPlacement;

	void SetCameraCommand(const TArray<FString>& Args)
	{
		FHexCameraPlacement Placement;

		if (Args.Num() < 2 || !LexTryParseString(Placement.Location.X, *Args[0]) || !LexTryParseString(Placement.Location.Y, *Args[1])
			|| !ParseOptionalArg(Args, 2, Placement.Yaw) || !ParseOptionalArg(Args, 3, Placement.Zoom))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.SetCamera <X> <Y> [Yaw] [Zoom], where - keeps the current yaw or zoom"));
			return;
		}

		PendingPlacement = Placement;
	}

	FAutoConsoleCommand SetCameraConsoleCommand(TEXT("hex.SetCamera"),
		TEXT("Puts the camera at once over a point of the ground, as hex.CopyCamera gives it. Arguments: X and Y in the world, ")
		TEXT("then optionally the turn in degrees away from the default angle, which makes the camera free unless it is 0, and the camera distance. - keeps the current value. ")
		TEXT("After hex.LoadMap it waits until the map has opened."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetCameraCommand));

	void CopyCameraCommand(UWorld* World)
	{
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;

		if (const AHexCameraPawn* CameraPawn = PlayerController ? Cast<AHexCameraPawn>(PlayerController->GetPawn()) : nullptr)
		{
			CameraPawn->CopyCameraToClipboard();
		}
		else if (const AHexPlanetCameraPawn* PlanetCameraPawn = PlayerController ? Cast<AHexPlanetCameraPawn>(PlayerController->GetPawn()) : nullptr)
		{
			PlanetCameraPawn->CopyCameraToClipboard();
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.CopyCamera: no hex camera to copy"));
		}
	}

	FAutoConsoleCommandWithWorld CopyCameraConsoleCommand(TEXT("hex.CopyCamera"),
		TEXT("Copies to the clipboard the console commands that open the current map and put the camera where it is now."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&CopyCameraCommand));

	/** Returns true while the level is about to open again, so a camera of it would be gone at once */
	bool IsTravelPending(const UWorld* World)
	{
		const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;
		return Context && !Context->TravelURL.IsEmpty();
	}
}

AHexCameraPawn::AHexCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);

	// keep the terrain colors stable, whatever share of the screen is lit
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	// its bias comes from the camera config in BeginPlay
}

void AHexCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	CameraConfig = GetDefault<UHexMapSettings>()->LoadCameraConfig();
	EdgeScrollCurve = CameraConfig->EdgeScrollCurve.LoadSynchronous();
	Camera->PostProcessSettings.AutoExposureBias = Settings()->ExposureBias;

	InitCameraMode(*CameraConfig);
}

void AHexCameraPawn::InitCameraMode(const UHexCameraConfig& Config)
{
	// a mode chosen in the console or by the key stays when another map opens the level again
	if ((CVarHexCamera->GetFlags() & ECVF_SetByMask) < ECVF_SetByConsole)
	{
		CVarHexCamera->Set(Config.bStartFree ? 1 : 0, ECVF_SetByGameSetting);
	}
}

bool AHexCameraPawn::GetCameraModeStatus(const UHexCameraConfig& Config, TArray<TPair<FString, FString>>& OutLines)
{
	const bool bFreeCamera = IsFreeCamera();
	OutLines.Reset();
	OutLines.Emplace(TEXT("Camera"), bFreeCamera ? TEXT("Free") : TEXT("Top-down"));

	// the mode the camera starts in is the usual view
	return bFreeCamera == Config.bStartFree;
}

void AHexCameraPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FindWorldMap();

	if (PendingPlacement.IsSet() && WorldMap && !IsTravelPending(GetWorld()))
	{
		const FHexCameraPlacement Placement = PendingPlacement.GetValue();
		PendingPlacement.Reset();
		SetCamera(Placement.Location, Placement.Yaw, Placement.Zoom);
	}

	// keep the zoom change independent from the frame rate
	CurrentZoom = FMath::FInterpTo(CurrentZoom, TargetZoom, DeltaTime, Settings()->ZoomInterpSpeed);
	UpdateCameraTurn(DeltaTime);
	UpdateCameraPosition();

	// the drag replaces the other movement, which would pull the map away from the mouse
	if (bDragging)
	{
		FVector2D GroundPoint;

		if (GetMouseGroundPoint(GroundPoint))
		{
			const FVector PreviousLocation = GetActorLocation();
			const FVector2D Offset = DragGroundPoint - GroundPoint;
			SetClampedLocation(PreviousLocation + FVector(Offset.X, Offset.Y, 0.0));

			// the map keeps this speed when it is released, so a pause before the release leaves it still
			if (DeltaTime > 0.0f)
			{
				const FVector2D DragVelocity = FVector2D(GetActorLocation() - PreviousLocation) / DeltaTime;
				ScrollVelocity = FMath::Vector2DInterpTo(ScrollVelocity, DragVelocity, DeltaTime, CameraConfig->DragReleaseSmoothing);
			}
		}

		return;
	}

	// the mouse may reach the screen edge while it turns the camera
	const FVector2D EdgeInput = bTurning ? FVector2D::ZeroVector : GetEdgeScrollInput(Cast<APlayerController>(GetController()), EdgeScrollCurve, Settings()->EdgeScrollMargin);

	// with no input the camera keeps the inertia of whatever moved it last
	if (!EdgeInput.IsNearlyZero())
	{
		ScrollInertia = CameraConfig->EdgeScrollInertia;
	}

	if (!MoveInput.IsNearlyZero())
	{
		ScrollInertia = CameraConfig->KeyboardInertia;
	}

	FVector2D Input = MoveInput + EdgeInput;
	Input.X = FMath::Clamp(Input.X, -1.0, 1.0);
	Input.Y = FMath::Clamp(Input.Y, -1.0, 1.0);

	// any movement takes the camera away from the hex it glides to
	if (!Input.IsNearlyZero())
	{
		bFocusing = false;
	}

	if (bFocusing)
	{
		const FVector FocusLocation = GetFocusLocation();
		SetClampedLocation(FMath::VInterpTo(GetActorLocation(), FocusLocation, DeltaTime, Settings()->FocusInterpSpeed));

		// the camera follows the hex until it has also reached its turn and distance, as both move the hex on screen
		const bool bArrived = FVector::Dist2D(GetActorLocation(), FocusLocation) < 1.0
			&& FMath::IsNearlyEqual(CurrentZoom, TargetZoom, 1.0f) && FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 0.1f);
		bFocusing = !bArrived;
		return;
	}

	// at the default angle right on screen is +Y and up on screen is +X
	const FVector Direction = FRotator(0.0f, CurrentYaw, 0.0f).RotateVector(FVector(Input.Y, Input.X, 0.0));
	const FVector2D TargetVelocity = FVector2D(Direction) * CurrentZoom * Settings()->MoveSpeed;

	ScrollVelocity = FMath::Vector2DInterpTo(ScrollVelocity, TargetVelocity, DeltaTime, 1.0f / ScrollInertia);

	if (Input.IsNearlyZero() && ScrollVelocity.IsNearlyZero(1.0))
	{
		ScrollVelocity = FVector2D::ZeroVector;
		return;
	}

	SetClampedLocation(GetActorLocation() + FVector(ScrollVelocity * DeltaTime, 0.0));
}

void AHexCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();

	// the default values of a missing asset have nothing to bind
	if (!InputConfig->MappingContext)
	{
		return;
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AHexCameraPawn::OnMove);
		EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Completed, this, &AHexCameraPawn::OnMove);
		EnhancedInputComponent->BindAction(InputConfig->ZoomAction, ETriggerEvent::Triggered, this, &AHexCameraPawn::OnZoom);
		EnhancedInputComponent->BindAction(InputConfig->ToggleGridAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnToggleGrid);
		EnhancedInputComponent->BindAction(InputConfig->DragAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnDragStarted);
		EnhancedInputComponent->BindAction(InputConfig->DragAction, ETriggerEvent::Completed, this, &AHexCameraPawn::OnDragCompleted);
		EnhancedInputComponent->BindAction(InputConfig->TurnAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnTurnStarted);
		EnhancedInputComponent->BindAction(InputConfig->TurnAction, ETriggerEvent::Completed, this, &AHexCameraPawn::OnTurnCompleted);
		EnhancedInputComponent->BindAction(InputConfig->TurnKeysAction, ETriggerEvent::Triggered, this, &AHexCameraPawn::OnTurnKeys);
		EnhancedInputComponent->BindAction(InputConfig->TurnKeysAction, ETriggerEvent::Completed, this, &AHexCameraPawn::OnTurnKeys);
		EnhancedInputComponent->BindAction(InputConfig->FreeCameraAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnToggleFreeCamera);
		EnhancedInputComponent->BindAction(InputConfig->FocusHexAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnFocusHexUnderMouse);
		EnhancedInputComponent->BindAction(InputConfig->CopyCameraAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnCopyCamera);
		EnhancedInputComponent->BindAction(InputConfig->SaveViewAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnSaveView);
		EnhancedInputComponent->BindAction(InputConfig->MapMenuAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnOpenMapMenu);
		EnhancedInputComponent->BindAction(InputConfig->WireframeModeAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnCycleWireframeMode);
		EnhancedInputComponent->BindAction(InputConfig->WireframeScopeAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnCycleWireframeScope);
		EnhancedInputComponent->BindAction(InputConfig->WireframeTargetAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnCycleWireframeTarget);
		EnhancedInputComponent->BindAction(InputConfig->WaterViewAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnCycleWaterView);
		EnhancedInputComponent->BindAction(InputConfig->KeyLegendAction, ETriggerEvent::Started, this, &AHexCameraPawn::OnToggleKeyLegend);
	}

	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputConfig->MappingContext, 0);
		}
	}
}

void AHexCameraPawn::OnMove(const FInputActionValue& Value)
{
	MoveInput = Value.Get<FVector2D>();
}

void AHexCameraPawn::OnZoom(const FInputActionValue& Value)
{
	const float Steps = Value.Get<float>();

	// scrolling up moves closer
	TargetZoom = FMath::Clamp(TargetZoom * FMath::Pow(1.0f - Settings()->ZoomStep, Steps), Settings()->MinZoom, Settings()->MaxZoom);
}

void AHexCameraPawn::OnToggleGrid()
{
	if (WorldMap)
	{
		WorldMap->SetGridVisible(!WorldMap->IsGridVisible());
	}
}

void AHexCameraPawn::OnCycleWireframeMode()
{
	AHexWorldMap::CycleWireframeMode();
}

void AHexCameraPawn::OnCycleWireframeScope()
{
	AHexWorldMap::CycleWireframeScope();
}

void AHexCameraPawn::OnCycleWireframeTarget()
{
	AHexWorldMap::CycleWireframeTarget();
}

void AHexCameraPawn::OnCycleWaterView()
{
	AHexWorldMap::CycleWaterView();
}

void AHexCameraPawn::OnToggleKeyLegend()
{
	if (AHexMapPlayerController* PlayerController = Cast<AHexMapPlayerController>(GetController()))
	{
		PlayerController->ToggleKeyLegend();
	}
}

void AHexCameraPawn::OnDragStarted()
{
	bDragging = GetMouseGroundPoint(DragGroundPoint);

	if (bDragging)
	{
		ScrollVelocity = FVector2D::ZeroVector;
		bFocusing = false;
	}
}

void AHexCameraPawn::OnDragCompleted()
{
	bDragging = false;
	ScrollInertia = CameraConfig->DragInertia;
}

void AHexCameraPawn::OnTurnStarted()
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	float MouseY = 0.0f;

	bTurning = PlayerController && PlayerController->GetMousePosition(TurnStartMouseX, MouseY);

	// a camera on its way back to the default angle is caught where it is
	TurnStartYaw = IsFreeCamera() || TurnKeyInput != 0.0f ? TargetYaw : CurrentYaw;
}

void AHexCameraPawn::OnTurnCompleted()
{
	bTurning = false;
}

void AHexCameraPawn::OnTurnKeys(const FInputActionValue& Value)
{
	TurnKeyInput = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
}

void AHexCameraPawn::OnToggleFreeCamera()
{
	SetFreeCamera(!IsFreeCamera());
}

bool AHexCameraPawn::IsFreeCamera()
{
	return CVarHexCamera.GetValueOnGameThread() != 0;
}

void AHexCameraPawn::SetFreeCamera(bool bFree)
{
	CVarHexCamera->Set(bFree ? 1 : 0, ECVF_SetByConsole);
}

bool AHexCameraPawn::FocusHex(const FHexCoord& Hex, TOptional<float> Yaw, TOptional<float> Zoom)
{
	FindWorldMap();

	if (!WorldMap || !WorldMap->GetHexCenter(Hex, FocusPoint))
	{
		return false;
	}

	if (Yaw.IsSet())
	{
		SetFreeCamera(true);

		// the camera turns the short way, and its turn update brings the angles back near zero
		TargetYaw = CurrentYaw + FRotator::NormalizeAxis(Yaw.GetValue() - CurrentYaw);
		TurnStartYaw = TargetYaw;
	}

	if (Zoom.IsSet())
	{
		TargetZoom = FMath::Clamp(Zoom.GetValue(), Settings()->MinZoom, Settings()->MaxZoom);
	}

	bFocusing = true;
	ScrollVelocity = FVector2D::ZeroVector;
	return true;
}

void AHexCameraPawn::SetCamera(const FVector2D& Location, TOptional<float> Yaw, TOptional<float> Zoom)
{
	if (Yaw.IsSet())
	{
		if (!FMath::IsNearlyZero(Yaw.GetValue()))
		{
			SetFreeCamera(true);
		}

		CurrentYaw = FRotator::NormalizeAxis(Yaw.GetValue());
		TargetYaw = CurrentYaw;
		TurnStartYaw = CurrentYaw;
	}

	if (Zoom.IsSet())
	{
		TargetZoom = FMath::Clamp(Zoom.GetValue(), Settings()->MinZoom, Settings()->MaxZoom);
		CurrentZoom = TargetZoom;
	}

	bFocusing = false;
	ScrollVelocity = FVector2D::ZeroVector;
	SetClampedLocation(FVector(Location, GetActorLocation().Z));
	UpdateCameraPosition();

	UE_LOG(LogHexHero, Log, TEXT("Put the camera at %.0f %.0f, turned %.1f at %.0f"), GetActorLocation().X, GetActorLocation().Y, CurrentYaw, CurrentZoom);
}

void AHexCameraPawn::CopyCameraToClipboard() const
{
	// the top-down camera goes back to the default angle anyway
	const float Yaw = IsFreeCamera() ? FRotator::NormalizeAxis(CurrentYaw) : 0.0f;
	FString Commands = FString::Printf(TEXT("hex.SetCamera %.0f %.0f %.1f %.0f"), GetActorLocation().X, GetActorLocation().Y, Yaw, TargetZoom);

	// the console runs commands joined by | one after another
	if (WorldMap && !WorldMap->GetMapFolder().IsEmpty())
	{
		Commands = FString::Printf(TEXT("hex.LoadMap %s | %s"), *WorldMap->GetMapFolder(), *Commands);
	}

	FPlatformApplicationMisc::ClipboardCopy(*Commands);
	UE_LOG(LogHexHero, Log, TEXT("Copied the camera: %s"), *Commands);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::White, FString::Printf(TEXT("Copied: %s"), *Commands));
	}
}

void AHexCameraPawn::OnCopyCamera()
{
	CopyCameraToClipboard();
}

void AHexCameraPawn::OnSaveView()
{
	if (!SaveLastView() || !GEngine)
	{
		return;
	}

	GEngine->AddOnScreenDebugMessage(INDEX_NONE, 2.0f, FColor::Green, FString::Printf(TEXT("Saved the view to %s"), *HexLastView::GetFilePath()));
}

TOptional<FHexLastView> AHexCameraPawn::GetLastView() const
{
	// before its map has loaded the camera is nowhere yet
	if (!WorldMap || WorldMap->GetMapFolder().IsEmpty())
	{
		return {};
	}

	// the camera of the planet in the file stays as it is
	FHexLastView View = SavedLastView;
	View.bPlanetOpen = false;
	View.FlatMap.Map = WorldMap->GetMapFolder();
	View.FlatMap.X = FMath::RoundToDouble(GetActorLocation().X);
	View.FlatMap.Y = FMath::RoundToDouble(GetActorLocation().Y);
	View.FlatMap.bFreeCamera = IsFreeCamera();
	View.FlatMap.Yaw = View.FlatMap.bFreeCamera ? FMath::RoundToFloat(FRotator::NormalizeAxis(CurrentYaw) * 10.0f) / 10.0f : 0.0f;
	View.FlatMap.Zoom = FMath::RoundToFloat(TargetZoom);
	return View;
}

bool AHexCameraPawn::SaveLastView()
{
	const TOptional<FHexLastView> View = GetLastView();

	if (!View.IsSet() || !HexLastView::Save(View.GetValue()))
	{
		return false;
	}

	SavedLastView = View.GetValue();
	return true;
}

bool AHexCameraPawn::IsLastViewSaved() const
{
	const TOptional<FHexLastView> View = GetLastView();
	return !View.IsSet() || View.GetValue() == SavedLastView;
}

FVector AHexCameraPawn::GetFocusLocation() const
{
	// the center of the screen looks along the camera through the pawn, so the pawn lies where that line through the focus point meets its height
	const FVector Direction = FRotator(-GetPitchAtZoom(CurrentZoom), CurrentYaw, 0.0f).Vector();
	const FVector Location = FocusPoint + Direction * ((GetActorLocation().Z - FocusPoint.Z) / Direction.Z);
	return FVector(Location.X, Location.Y, GetActorLocation().Z);
}

void AHexCameraPawn::OnFocusHexUnderMouse()
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());

	FVector RayOrigin;
	FVector RayDirection;
	FHexCoord Hex;

	if (WorldMap && PlayerController && PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection) && WorldMap->FindHexAlongRay(RayOrigin, RayDirection, Hex))
	{
		FocusHex(Hex);
	}
}

bool AHexCameraPawn::GetViewStatus(TArray<TPair<FString, FString>>& OutLines) const
{
	OutLines.Reset();

	// before its first play the camera has not chosen its mode, so the first mode shown is no change
	if (!CameraConfig)
	{
		return true;
	}

	return GetCameraModeStatus(*CameraConfig, OutLines);
}

void AHexCameraPawn::OnOpenMapMenu()
{
	if (AHexMapPlayerController* PlayerController = Cast<AHexMapPlayerController>(GetController()))
	{
		PlayerController->OpenMapMenu();
	}
}

void AHexCameraPawn::UpdateCameraTurn(float DeltaTime)
{
	const bool bFreeCamera = IsFreeCamera();
	const float MaxTurn = bFreeCamera ? UE_BIG_NUMBER : CameraConfig->MaxCameraTurn;
	const float KeyTurn = TurnKeyInput * CameraConfig->CameraTurnKeySpeed * DeltaTime;

	if (bTurning)
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		float MouseX = TurnStartMouseX;
		float MouseY = 0.0f;

		if (PlayerController)
		{
			PlayerController->GetMousePosition(MouseX, MouseY);
		}

		// the keys turn the camera along with the mouse
		TurnStartYaw = FMath::Clamp(TurnStartYaw + KeyTurn, -MaxTurn, MaxTurn);
		TargetYaw = TurnStartYaw + (MouseX - TurnStartMouseX) * CameraConfig->CameraTurnPerPixel;
	}
	else if (bFreeCamera || TurnKeyInput != 0.0f)
	{
		TargetYaw += KeyTurn;
	}
	else
	{
		TargetYaw = 0.0f;
	}

	TargetYaw = FMath::Clamp(TargetYaw, -MaxTurn, MaxTurn);

	// the free camera keeps its angles near zero, so it returns the short way when it stops being free
	const float Wrap = 360.0f * FMath::RoundToFloat(TargetYaw / 360.0f);
	TargetYaw -= Wrap;
	CurrentYaw -= Wrap;
	TurnStartYaw -= Wrap;

	CurrentYaw = FMath::FInterpTo(CurrentYaw, TargetYaw, DeltaTime, CameraConfig->CameraTurnInterpSpeed);
}

void AHexCameraPawn::FindWorldMap()
{
	if (WorldMap)
	{
		return;
	}

	for (TActorIterator<AHexWorldMap> It(GetWorld()); It; ++It)
	{
		if (It->IsMapLoaded())
		{
			WorldMap = *It;

			const FVector2D Center = WorldMap->GetMapBounds().GetCenter();
			SetActorLocation(FVector(Center.X, Center.Y, 0.0));

			// the camera of the last run on this map, unless hex.SetCamera asks for another place
			// what the file holds now, which the view is compared with to tell whether it is saved
			const bool bLoaded = HexLastView::Load(SavedLastView);

			// the flat map is open, which the player controller has already written
			SavedLastView.bPlanetOpen = false;

			const FHexLastFlatMapView& Saved = SavedLastView.FlatMap;
			if (bLoaded && !PendingPlacement.IsSet() && Saved.Map.Equals(WorldMap->GetMapFolder(), ESearchCase::IgnoreCase))
			{
				SetFreeCamera(Saved.bFreeCamera);
				SetCamera(FVector2D(Saved.X, Saved.Y), Saved.Yaw, Saved.Zoom);
			}
			break;
		}
	}
}

FVector2D AHexCameraPawn::GetEdgeScrollInput(const APlayerController* PlayerController, const UCurveFloat* Curve, float Margin)
{
	if (!CVarHexEdgeScroll.GetValueOnGameThread() || !PlayerController)
	{
		return FVector2D::ZeroVector;
	}

	// the last known mouse position means nothing while the mouse is over another window
	UGameViewportClient* ViewportClient = PlayerController->GetWorld()->GetGameViewport();
	const TSharedPtr<SViewport> ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;

	if (!FPlatformApplicationMisc::IsThisApplicationForeground() || !ViewportWidget.IsValid() || !ViewportWidget->IsHovered())
	{
		return FVector2D::ZeroVector;
	}

	// the game gets no input while a menu is open
	if (ViewportClient->IgnoreInput())
	{
		return FVector2D::ZeroVector;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;

	if (!PlayerController->GetMousePosition(MouseX, MouseY))
	{
		return FVector2D::ZeroVector;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);

	return FVector2D(
		GetEdgeScrollStrength(ViewportWidth - MouseX, Curve, Margin) - GetEdgeScrollStrength(MouseX, Curve, Margin),
		GetEdgeScrollStrength(MouseY, Curve, Margin) - GetEdgeScrollStrength(ViewportHeight - MouseY, Curve, Margin));
}

double AHexCameraPawn::GetEdgeScrollStrength(double EdgeDistance, const UCurveFloat* Curve, float Margin)
{
	if (EdgeDistance >= Margin)
	{
		return 0.0;
	}

	// 0 where the zone starts and 1 at the screen edge
	const float ZonePosition = 1.0f - FMath::Max(EdgeDistance, 0.0) / Margin;

	return Curve ? Curve->GetFloatValue(ZonePosition) : ZonePosition;
}

bool AHexCameraPawn::GetMouseGroundPoint(FVector2D& OutPoint) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());

	FVector RayOrigin;
	FVector RayDirection;

	if (!PlayerController || !PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	// a ray at or above the horizon never reaches the ground
	if (RayDirection.Z >= -UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector GroundPoint = RayOrigin + RayDirection * ((GetActorLocation().Z - RayOrigin.Z) / RayDirection.Z);
	OutPoint = FVector2D(GroundPoint.X, GroundPoint.Y);
	return true;
}

void AHexCameraPawn::SetClampedLocation(FVector Location)
{
	if (WorldMap && WorldMap->IsMapLoaded())
	{
		const FBox2D& Bounds = WorldMap->GetMapBounds();
		Location.X = FMath::Clamp(Location.X, Bounds.Min.X, Bounds.Max.X);
		Location.Y = FMath::Clamp(Location.Y, Bounds.Min.Y, Bounds.Max.Y);
	}

	SetActorLocation(Location);
}

float AHexCameraPawn::GetPitchAtZoom(float Zoom) const
{
	const float Closeness = FMath::Log2(Settings()->PitchChangeZoom / FMath::Max(Zoom, 1.0f)) / FMath::Log2(Settings()->PitchChangeZoom / Settings()->MinZoom);
	return FMath::Lerp(Settings()->CameraPitch, Settings()->CloseCameraPitch, FMath::SmoothStep(0.0f, 1.0f, Closeness));
}

void AHexCameraPawn::UpdateCameraPosition()
{
	const FRotator Rotation(-GetPitchAtZoom(CurrentZoom), CurrentYaw, 0.0f);
	Camera->SetRelativeLocationAndRotation(-Rotation.Vector() * CurrentZoom, Rotation);
}

const UHexCameraConfig* AHexCameraPawn::Settings() const
{
	return CameraConfig ? CameraConfig.Get() : GetDefault<UHexCameraConfig>();
}
