// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexMapPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HexCameraPawn.h"
#include "HexCoord.h"
#include "HexHero.h"
#include "HexInputConfig.h"
#include "HexKeyLegendConfig.h"
#include "HexLastView.h"
#include "HexMapLoader.h"
#include "HexMapSettings.h"
#include "HexWorldMap.h"
#include "Kismet/GameplayStatics.h"
#include "SHexDebugOverlay.h"
#include "SHexHoverInfo.h"
#include "SHexMapMenu.h"
#include "HexPlanetFile.h"

namespace
{
	TAutoConsoleVariable<bool> CVarHexHoverInfo(TEXT("hex.HoverInfo"), !UE_BUILD_SHIPPING, TEXT("Shows the debug panel with the hex under the mouse cursor."));

	void LoadMapCommand(const TArray<FString>& Args, UWorld* World)
	{
		AHexMapPlayerController* PlayerController = World ? Cast<AHexMapPlayerController>(World->GetFirstPlayerController()) : nullptr;

		if (Args.Num() != 1)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Usage: hex.LoadMap <Folder>, where the folder is one in Content/Data/Maps"));
			return;
		}

		if (!World)
		{
			return;
		}

		const TArray<FHexMapEntry> Maps = HexMapLoader::FindMaps();
		const FHexMapEntry* Map = Maps.FindByPredicate([&Args](const FHexMapEntry& Entry) { return Entry.Folder.Equals(Args[0], ESearchCase::IgnoreCase); });

		if (!Map)
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.LoadMap: there is no map %s in Content/Data/Maps"), *Args[0]);
			return;
		}

		// the map already open stays, so a camera command after this one moves the camera at once
		for (TActorIterator<AHexWorldMap> It(World); It; ++It)
		{
			if (It->IsMapLoaded() && It->GetMapFolder().Equals(Map->Folder, ESearchCase::IgnoreCase))
			{
				return;
			}
		}

		// from another level, such as the planet, the map opens in the level of the world map
		if (PlayerController)
		{
			PlayerController->OpenMap(Map->Folder);
		}
		else
		{
			UGameplayStatics::OpenLevel(World, FName(TEXT("/Game/HexMap/Lvl_HexWorld")), true, FString::Printf(TEXT("%s=%s"), HexMapLoader::MapOption, *Map->Folder));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs LoadMapConsoleCommand(TEXT("hex.LoadMap"),
		TEXT("Opens a map, a folder in Content/Data/Maps, unless it is already open."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoadMapCommand));
}

AHexMapPlayerController::AHexMapPlayerController()
{
	bShowMouseCursor = true;
}

void AHexMapPlayerController::OpenMapMenu()
{
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();

	if (MapMenu.IsValid() || !ViewportClient)
	{
		return;
	}

	SAssignNew(MapMenu, SHexMapMenu)
		.Maps(HexMapLoader::FindMaps())
		.Planets(HexPlanetFile::FindPlanets())
		.CloseKeys(GetDefault<UHexMapSettings>()->LoadInputConfig()->GetMapMenuKeys())
		.OnMapChosen_UObject(this, &AHexMapPlayerController::OnMapChosen)
		.OnPlanetChosen_UObject(this, &AHexMapPlayerController::OnPlanetChosen)
		.OnNewPlanet_Lambda([this](const FHexPlanetManifest& Manifest) { HexPlanetFile::OpenNewPlanet(this, Manifest); })
		.OnClosed_UObject(this, &AHexMapPlayerController::CloseMapMenu);

	ViewportClient->AddViewportWidgetContent(MapMenu.ToSharedRef());

	// the game gets no release of the keys held now, so the camera would keep moving
	FlushPressedKeys();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MapMenu);
	SetInputMode(InputMode);
}

void AHexMapPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// the planet was open last, and no map was asked for
	if (HexLastView::ShouldOpenPlanet(GetWorld()))
	{
		OnPlanetChosen(FString());
		return;
	}

	HexLastView::SetPlanetOpen(false);
	SetGameInputMode();

	if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
	{
		SAssignNew(HoverInfo, SHexHoverInfo);
		ViewportClient->AddViewportWidgetContent(HoverInfo.ToSharedRef());

		SAssignNew(DebugOverlay, SHexDebugOverlay);
		const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();
		const UHexKeyLegendConfig* KeyLegend = GetDefault<UHexMapSettings>()->LoadKeyLegend();
		DebugOverlay->SetLegend(KeyLegend->GetSections(*InputConfig), KeyLegend->GetHint(*InputConfig));
		ViewportClient->AddViewportWidgetContent(DebugOverlay.ToSharedRef());
	}

	AddViewStatus(&AHexWorldMap::GetDebugViewStatus);

	// the pawn may be possessed later, so it is found each time
	AddViewStatus([this](TArray<TPair<FString, FString>>& OutLines)
	{
		const AHexCameraPawn* CameraPawn = Cast<AHexCameraPawn>(GetPawn());
		OutLines.Reset();
		return !CameraPawn || CameraPawn->GetViewStatus(OutLines);
	});

	// the map of the last run opens without the menu, as does one chosen in it
	if (!GetWorld()->URL.HasOption(HexMapLoader::MapOption) && HexLastView::FindLastMap(HexMapLoader::FindMaps()).IsEmpty())
	{
		OpenMapMenu();
	}
}

void AHexMapPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MapMenu.IsValid())
	{
		if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
		{
			ViewportClient->RemoveViewportWidgetContent(MapMenu.ToSharedRef());
		}

		MapMenu.Reset();
	}

	if (HoverInfo.IsValid())
	{
		if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
		{
			ViewportClient->RemoveViewportWidgetContent(HoverInfo.ToSharedRef());
		}

		HoverInfo.Reset();
	}

	if (DebugOverlay.IsValid())
	{
		if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
		{
			ViewportClient->RemoveViewportWidgetContent(DebugOverlay.ToSharedRef());
		}

		DebugOverlay.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void AHexMapPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (HoverInfo.IsValid())
	{
		HoverInfo->SetInfo(GetHoverInfoText());
	}

	if (DebugOverlay.IsValid())
	{
		DebugOverlay->UpdateViewStatus(GetWorld()->GetRealTimeSeconds());

		const AHexCameraPawn* CameraPawn = Cast<AHexCameraPawn>(GetPawn());
		const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();
		const bool bSaved = !CameraPawn || CameraPawn->IsLastViewSaved();
		DebugOverlay->SetWarning(bSaved ? FString() : FString::Printf(TEXT("View not saved (%s)"), *InputConfig->GetKeysText(InputConfig->SaveViewAction)));
	}
}

void AHexMapPlayerController::ToggleKeyLegend()
{
	if (DebugOverlay.IsValid())
	{
		DebugOverlay->SetLegendVisible(!DebugOverlay->IsLegendVisible());
	}
}

void AHexMapPlayerController::AddViewStatus(TFunction<bool(TArray<TPair<FString, FString>>&)> GetStatus)
{
	if (DebugOverlay.IsValid())
	{
		DebugOverlay->AddViewStatus(MoveTemp(GetStatus));
	}
}

FString AHexMapPlayerController::GetHoverInfoText()
{
	if (MapMenu.IsValid() || !CVarHexHoverInfo.GetValueOnGameThread())
	{
		return FString();
	}

	if (!WorldMap.IsValid())
	{
		TActorIterator<AHexWorldMap> Iterator(GetWorld());
		WorldMap = Iterator ? *Iterator : nullptr;
	}

	FVector RayOrigin;
	FVector RayDirection;
	FHexCoord Hex;

	if (!WorldMap.IsValid() || !DeprojectMousePositionToWorld(RayOrigin, RayDirection) || !WorldMap->FindHexAlongRay(RayOrigin, RayDirection, Hex))
	{
		return FString();
	}

	return WorldMap->GetHexDebugText(Hex);
}

void AHexMapPlayerController::CloseMapMenu()
{
	if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
	{
		ViewportClient->RemoveViewportWidgetContent(MapMenu.ToSharedRef());
	}

	MapMenu.Reset();

	SetGameInputMode();
}

void AHexMapPlayerController::OnMapChosen(const FString& MapFolder)
{
	OpenMap(MapFolder);
}

void AHexMapPlayerController::OnPlanetChosen(const FString& PlanetFolder)
{
	HexPlanetFile::OpenPlanet(this, PlanetFolder);
}

void AHexMapPlayerController::OpenMap(const FString& MapFolder)
{
	const FName Level(*UGameplayStatics::GetCurrentLevelName(this));
	UGameplayStatics::OpenLevel(this, Level, true, FString::Printf(TEXT("%s=%s"), HexMapLoader::MapOption, *MapFolder));
}

void AHexMapPlayerController::SetGameInputMode()
{
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}
