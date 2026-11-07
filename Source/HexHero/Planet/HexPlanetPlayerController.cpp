// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "EnhancedInputComponent.h"
#include "HAL/IConsoleManager.h"
#include "HexInputConfig.h"
#include "HexKeyLegendConfig.h"
#include "HexLastView.h"
#include "HexMapLoader.h"
#include "HexMapSettings.h"
#include "Kismet/GameplayStatics.h"
#include "SHexMapMenu.h"
#include "HexPlanet.h"
#include "HexPlanetFile.h"
#include "HexPlanetCameraPawn.h"
#include "HexWorldMap.h"
#include "SHexDebugOverlay.h"
#include "SHexDialog.h"
#include "SHexHoverInfo.h"
#include "SHexPlanetPalette.h"
#include "HexPlanetPainter.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"

AHexPlanetPlayerController::AHexPlanetPlayerController()
{
	bShowMouseCursor = true;
}

void AHexPlanetPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// a run without a map opens the planet next time
	HexLastView::SetPlanetOpen(true);

	SetGameInputMode();

	if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
	{
		SAssignNew(HoverInfo, SHexHoverInfo);
		ViewportClient->AddViewportWidgetContent(HoverInfo.ToSharedRef());

		SAssignNew(DebugOverlay, SHexDebugOverlay);
		ViewportClient->AddViewportWidgetContent(DebugOverlay.ToSharedRef());

		// the same panels of settings as on the flat map
		DebugOverlay->AddViewStatus(&AHexWorldMap::GetDebugViewStatus);
		DebugOverlay->AddViewStatus([this](TArray<TPair<FString, FString>>& OutLines)
		{
			const AHexPlanetCameraPawn* CameraPawn = Cast<AHexPlanetCameraPawn>(GetPawn());
			OutLines.Reset();
			return !CameraPawn || CameraPawn->GetViewStatus(OutLines);
		});
		DebugOverlay->AddViewStatus(&AHexPlanetCameraPawn::GetSunStatus);
	}
}

void AHexPlanetPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
	{
		if (HoverInfo.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(HoverInfo.ToSharedRef());
		}

		if (DebugOverlay.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(DebugOverlay.ToSharedRef());
		}

		if (MapMenu.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(MapMenu.ToSharedRef());
		}

		if (Dialog.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(Dialog.ToSharedRef());
		}

		if (Palette.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(Palette.ToSharedRef());
		}
	}

	Dialog.Reset();
	HoverInfo.Reset();
	Palette.Reset();
	DebugOverlay.Reset();
	MapMenu.Reset();

	Super::EndPlay(EndPlayReason);
}

void AHexPlanetPlayerController::OpenMapMenu()
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
		.OnMapChosen_UObject(this, &AHexPlanetPlayerController::OnMapChosen)
		.OnPlanetChosen_UObject(this, &AHexPlanetPlayerController::OnPlanetChosen)
		.OnNewPlanet_UObject(this, &AHexPlanetPlayerController::OnNewPlanet)
		.OnSave_UObject(this, &AHexPlanetPlayerController::SaveFromMenu)
		.CanSave_Lambda([this]()
		{
			const AHexPlanet* Planet = GetPlanet();
			return Planet && Planet->HasUnsavedChanges();
		})
		.OnClosed_UObject(this, &AHexPlanetPlayerController::CloseMapMenu);

	ViewportClient->AddViewportWidgetContent(MapMenu.ToSharedRef());

	// the game gets no release of the keys held now, so the camera would keep moving
	FlushPressedKeys();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MapMenu);
	SetInputMode(InputMode);
}

void AHexPlanetPlayerController::CloseMapMenu()
{
	if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
	{
		ViewportClient->RemoveViewportWidgetContent(MapMenu.ToSharedRef());
	}

	MapMenu.Reset();
	SetGameInputMode();
}

void AHexPlanetPlayerController::OnMapChosen(const FString& MapFolder)
{
	LeavePlanet([this, MapFolder]()
	{
		// the map asked for wins over the planet of the last run
		HexLastView::SetPlanetOpen(false);
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/HexMap/Lvl_HexWorld")), true, FString::Printf(TEXT("%s=%s"), HexMapLoader::MapOption, *MapFolder));
	});
}

AHexPlanetCameraPawn* AHexPlanetPlayerController::GetCameraPawn() const
{
	return Cast<AHexPlanetCameraPawn>(GetPawn());
}

void AHexPlanetPlayerController::LeavePlanet(TFunction<void()> Leave)
{
	const AHexPlanet* Planet = GetPlanet();

	if (!Planet || !Planet->HasUnsavedChanges())
	{
		Leave();
		return;
	}

	if (MapMenu.IsValid())
	{
		CloseMapMenu();
	}

	if (Dialog.IsValid())
	{
		return;
	}

	SHexDialog::FButton Save;
	Save.Label = Planet->IsSaved() ? NSLOCTEXT("HexPlanet", "SaveAndLeave", "Save") : NSLOCTEXT("HexPlanet", "SaveAsAndLeave", "Save...");
	Save.OnClicked = FOnHexDialogButton::CreateLambda([this, Leave](const FString&)
	{
		// the dialog of the name of a new planet opens once this one has closed, and leaving waits for the save
		GetWorldTimerManager().SetTimerForNextTick([this, Leave]()
		{
			if (AHexPlanetCameraPawn* CameraPawn = GetCameraPawn())
			{
				CameraPawn->SavePlanet(Leave);
			}
		});
		return FText::GetEmpty();
	});

	SHexDialog::FButton Discard;
	Discard.Label = NSLOCTEXT("HexPlanet", "Discard", "Leave without saving");
	Discard.OnClicked = FOnHexDialogButton::CreateLambda([this, Leave](const FString&)
	{
		GetWorldTimerManager().SetTimerForNextTick([Leave]() { Leave(); });
		return FText::GetEmpty();
	});

	SHexDialog::FButton Cancel;
	Cancel.Label = NSLOCTEXT("HexPlanet", "Stay", "Stay");

	ShowDialog(SNew(SHexDialog)
		.Title(Planet->IsSaved() ? NSLOCTEXT("HexPlanet", "UnsavedTitle", "Unsaved changes") : NSLOCTEXT("HexPlanet", "NotSavedTitle", "Planet not saved"))
		.Message(Planet->IsSaved()
			? NSLOCTEXT("HexPlanet", "UnsavedMessage", "The painted levels and biomes of this planet are not saved and will be lost. Save them first?")
			: NSLOCTEXT("HexPlanet", "NotSavedMessage", "This new planet is not saved and will be lost. Save it first?"))
		.Buttons(TArray<SHexDialog::FButton>{Save, Discard, Cancel})
		.OnClosed_UObject(this, &AHexPlanetPlayerController::CloseDialog));
}

void AHexPlanetPlayerController::SaveFromMenu()
{
	const AHexPlanet* Planet = GetPlanet();
	AHexPlanetCameraPawn* CameraPawn = GetCameraPawn();

	if (!Planet || !CameraPawn)
	{
		return;
	}

	// the name of a new planet is asked in a dialog, which needs the menu closed
	if (!Planet->IsSaved())
	{
		CloseMapMenu();
	}

	CameraPawn->SavePlanet(nullptr);
}

void AHexPlanetPlayerController::SetGameInputMode()
{
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

void AHexPlanetPlayerController::ToggleKeyLegend()
{
	if (DebugOverlay.IsValid())
	{
		DebugOverlay->SetLegendVisible(!DebugOverlay->IsLegendVisible());
	}
}

void AHexPlanetPlayerController::UpdateLegend()
{
	const APawn* ControlledPawn = GetPawn();
	const UEnhancedInputComponent* PawnInput = ControlledPawn ? Cast<UEnhancedInputComponent>(ControlledPawn->InputComponent) : nullptr;

	if (bLegendSet || !DebugOverlay.IsValid() || !PawnInput || PawnInput->GetActionEventBindings().IsEmpty())
	{
		return;
	}

	// the legend asset lists the keys of the flat map, of which the planet uses only some
	TSet<const UInputAction*> BoundActions;

	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : PawnInput->GetActionEventBindings())
	{
		BoundActions.Add(Binding->GetAction());
	}

	const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();
	const UHexKeyLegendConfig* KeyLegend = GetDefault<UHexMapSettings>()->LoadKeyLegend();
	DebugOverlay->SetLegend(KeyLegend->GetSections(*InputConfig, &BoundActions), KeyLegend->GetHint(*InputConfig));
	bLegendSet = true;
}

void AHexPlanetPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdateLegend();

	const AHexPlanetCameraPawn* CameraPawn = Cast<AHexPlanetCameraPawn>(GetPawn());
	AHexPlanet* Planet = CameraPawn ? CameraPawn->GetPlanet() : nullptr;

	if (DebugOverlay.IsValid())
	{
		DebugOverlay->UpdateViewStatus(GetWorld()->GetRealTimeSeconds());

		const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();
		const FString SaveKeys = InputConfig->GetKeysText(InputConfig->SaveViewAction);
		const AHexPlanet* ShownPlanet = CameraPawn ? CameraPawn->GetPlanet() : nullptr;
		const bool bSaved = !CameraPawn || CameraPawn->IsLastViewSaved();
		DebugOverlay->SetWarning(ShownPlanet && !ShownPlanet->IsSaved() ? FString::Printf(TEXT("Planet not saved (%s)"), *SaveKeys)
			: ShownPlanet && ShownPlanet->HasUnsavedLevels() ? FString::Printf(TEXT("Levels not saved (%s)"), *SaveKeys)
			: bSaved ? FString() : FString::Printf(TEXT("View not saved (%s)"), *SaveKeys));
	}

	if (!Planet)
	{
		return;
	}

	FVector Origin;
	FVector Direction;
	const int32 Cell = DeprojectMousePositionToWorld(Origin, Direction) ? Planet->FindCellAlongRay(Origin, Direction) : INDEX_NONE;
	MouseCell = Cell;

	if (Painter && Painter->IsPainting())
	{
		Painter->ContinueStroke(Cell);
	}

	Planet->SetHoveredCell(Cell, IsPaintModeOn() ? Painter->GetBrushSize() : 0);

	// the panel follows hex.HoverInfo of the flat map
	static const IConsoleVariable* HoverInfoVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("hex.HoverInfo"));

	if (HoverInfo.IsValid())
	{
		HoverInfo->SetInfo(!HoverInfoVariable || HoverInfoVariable->GetBool() ? Planet->GetCellDebugText(Cell) : FString());
	}
}

void AHexPlanetPlayerController::OnPlanetChosen(const FString& PlanetFolder)
{
	LeavePlanet([this, PlanetFolder]() { HexPlanetFile::OpenPlanet(this, PlanetFolder); });
}

void AHexPlanetPlayerController::OnNewPlanet(const FHexPlanetManifest& Manifest)
{
	LeavePlanet([this, Manifest]() { HexPlanetFile::OpenNewPlanet(this, Manifest); });
}

void AHexPlanetPlayerController::ShowDialog(const TSharedRef<SHexDialog>& NewDialog)
{
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();

	if (!ViewportClient)
	{
		return;
	}

	Dialog = NewDialog;
	ViewportClient->AddViewportWidgetContent(NewDialog);

	// the game gets no release of the keys held now, so the camera would keep moving
	FlushPressedKeys();

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(NewDialog->GetFocusTarget());
	SetInputMode(InputMode);
}

void AHexPlanetPlayerController::CloseDialog()
{
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();

	if (ViewportClient && Dialog.IsValid())
	{
		ViewportClient->RemoveViewportWidgetContent(Dialog.ToSharedRef());
	}

	Dialog.Reset();
	SetGameInputMode();

	// the keyboard goes back to the game, whatever widget had it, so its keys work without a click
	FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void AHexPlanetPlayerController::OpenSavePlanetDialog(TFunction<void()> OnSaved)
{
	const AHexPlanetCameraPawn* CameraPawn = Cast<AHexPlanetCameraPawn>(GetPawn());
	AHexPlanet* Planet = CameraPawn ? CameraPawn->GetPlanet() : nullptr;

	if (!Planet || Dialog.IsValid() || MapMenu.IsValid())
	{
		return;
	}

	// the folder the player agreed to write over, and the dialog, which its own callbacks change
	TSharedRef<FString> ConfirmedFolder = MakeShared<FString>();
	TSharedRef<TWeakPtr<SHexDialog>> DialogRef = MakeShared<TWeakPtr<SHexDialog>>();
	TWeakObjectPtr<AHexPlanet> WeakPlanet(Planet);

	const FOnHexDialogCheck Check = FOnHexDialogCheck::CreateLambda([ConfirmedFolder, DialogRef](const FString& Name)
	{
		const FString Folder = HexPlanetFile::SanitizeFolderName(Name);

		// another name has to be agreed to again
		if (!Folder.Equals(*ConfirmedFolder, ESearchCase::IgnoreCase) && DialogRef->IsValid())
		{
			ConfirmedFolder->Reset();
			DialogRef->Pin()->SetButtonLabel(0, NSLOCTEXT("HexPlanet", "Save", "Save"));
		}

		if (Folder.IsEmpty())
		{
			return NSLOCTEXT("HexPlanet", "NeedsName", "The name needs a letter or a digit.");
		}

		return HexPlanetFile::PlanetExists(Folder)
			? FText::Format(NSLOCTEXT("HexPlanet", "Exists", "A planet is already saved as {0}, which saving would write over."), FText::FromString(Folder))
			: FText::Format(NSLOCTEXT("HexPlanet", "Folder", "Saves as {0}."), FText::FromString(Folder));
	});

	SHexDialog::FButton Save;
	Save.Label = NSLOCTEXT("HexPlanet", "Save", "Save");
	Save.OnClicked = FOnHexDialogButton::CreateLambda([ConfirmedFolder, DialogRef, WeakPlanet, OnSaved](const FString& Name)
	{
		const FString Folder = HexPlanetFile::SanitizeFolderName(Name);

		if (Folder.IsEmpty())
		{
			return NSLOCTEXT("HexPlanet", "NeedsName", "The name needs a letter or a digit.");
		}

		// a planet that is there already is written over only on a second press
		if (HexPlanetFile::PlanetExists(Folder) && !Folder.Equals(*ConfirmedFolder, ESearchCase::IgnoreCase))
		{
			*ConfirmedFolder = Folder;
			if (DialogRef->IsValid())
			{
				DialogRef->Pin()->SetButtonLabel(0, NSLOCTEXT("HexPlanet", "Overwrite", "Overwrite"));
			}
			return FText::Format(NSLOCTEXT("HexPlanet", "Confirm", "{0} is already saved. Press Overwrite to replace it with this planet."), FText::FromString(Folder));
		}

		FString Error;
		if (!WeakPlanet.IsValid() || !WeakPlanet->SaveAs(Name, Error))
		{
			return FText::FromString(Error);
		}

		if (OnSaved)
		{
			OnSaved();
		}

		return FText::GetEmpty();
	});

	SHexDialog::FButton Cancel;
	Cancel.Label = NSLOCTEXT("HexPlanet", "Cancel", "Cancel");

	TSharedRef<SHexDialog> NewDialog = SNew(SHexDialog)
		.Title(NSLOCTEXT("HexPlanet", "SaveTitle", "Save planet"))
		.Message(NSLOCTEXT("HexPlanet", "SaveMessage", "Name of the planet:"))
		.bHasText(true)
		.InitialText(Planet->GetManifest().Name.IsEmpty() ? FString(TEXT("New planet")) : Planet->GetManifest().Name)
		.OnCheckText(Check)
		.Buttons(TArray<SHexDialog::FButton>{Save, Cancel})
		.OnClosed_UObject(this, &AHexPlanetPlayerController::CloseDialog);

	*DialogRef = NewDialog;
	ShowDialog(NewDialog);
}

void AHexPlanetPlayerController::OpenCopyCameraDialog()
{
	if (Dialog.IsValid() || MapMenu.IsValid())
	{
		return;
	}

	SHexDialog::FButton Save;
	Save.Label = NSLOCTEXT("HexPlanet", "SaveFirst", "Save...");
	Save.OnClicked = FOnHexDialogButton::CreateLambda([this](const FString&)
	{
		// the save dialog opens once this one has closed
		GetWorldTimerManager().SetTimerForNextTick([this]()
		{
			OpenSavePlanetDialog([this]()
			{
				if (const AHexPlanetCameraPawn* CameraPawn = Cast<AHexPlanetCameraPawn>(GetPawn()))
				{
					CameraPawn->CopyCameraToClipboard();
				}
			});
		});
		return FText::GetEmpty();
	});

	SHexDialog::FButton Copy;
	Copy.Label = NSLOCTEXT("HexPlanet", "CopyAnyway", "Copy camera only");
	Copy.OnClicked = FOnHexDialogButton::CreateLambda([this](const FString&)
	{
		if (const AHexPlanetCameraPawn* CameraPawn = Cast<AHexPlanetCameraPawn>(GetPawn()))
		{
			CameraPawn->CopyCameraToClipboard();
		}
		return FText::GetEmpty();
	});

	SHexDialog::FButton Cancel;
	Cancel.Label = NSLOCTEXT("HexPlanet", "Cancel", "Cancel");

	ShowDialog(SNew(SHexDialog)
		.Title(NSLOCTEXT("HexPlanet", "CopyTitle", "Planet not saved"))
		.Message(NSLOCTEXT("HexPlanet", "CopyMessage", "This planet is not saved yet, so a copied command could not open it again. Save it first?"))
		.Buttons(TArray<SHexDialog::FButton>{Save, Copy, Cancel})
		.OnClosed_UObject(this, &AHexPlanetPlayerController::CloseDialog));
}
AHexPlanet* AHexPlanetPlayerController::GetPlanet() const
{
	const AHexPlanetCameraPawn* CameraPawn = Cast<AHexPlanetCameraPawn>(GetPawn());
	return CameraPawn ? CameraPawn->GetPlanet() : nullptr;
}

UHexPlanetPainter* AHexPlanetPlayerController::GetPainter()
{
	AHexPlanet* Planet = GetPlanet();

	if (!Painter && Planet)
	{
		Painter = NewObject<UHexPlanetPainter>(this);
		Painter->Init(Planet);
	}
	return Painter;
}

bool AHexPlanetPlayerController::IsPaintModeOn() const
{
	return Painter && Painter->IsActive();
}

void AHexPlanetPlayerController::SetPaintMode(bool bOn)
{
	UHexPlanetPainter* Brush = GetPainter();
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();

	if (!Brush || !ViewportClient)
	{
		return;
	}

	Brush->SetActive(bOn);

	if (bOn && !Palette.IsValid())
	{
		const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();

		SAssignNew(Palette, SHexPlanetPalette)
			.Painter(Brush)
			.PaintModeKeys(InputConfig->GetKeysText(InputConfig->PaintModeAction))
			.BrushSizeKeys(InputConfig->GetKeysText(InputConfig->BrushSizeAction))
			.UndoKeys(InputConfig->GetKeysText(InputConfig->UndoAction))
			.RedoKeys(InputConfig->GetKeysText(InputConfig->RedoAction))
			.SaveKeys(InputConfig->GetKeysText(InputConfig->SaveViewAction));

		ViewportClient->AddViewportWidgetContent(Palette.ToSharedRef());
	}
	else if (!bOn && Palette.IsValid())
	{
		ViewportClient->RemoveViewportWidgetContent(Palette.ToSharedRef());
		Palette.Reset();
	}
}

void AHexPlanetPlayerController::SetPaintLevel(int32 Level)
{
	if (UHexPlanetPainter* Brush = GetPainter())
	{
		Brush->SetLevel(Level);
		SetPaintMode(true);
	}
}

void AHexPlanetPlayerController::CycleBrushSize()
{
	if (IsPaintModeOn())
	{
		Painter->SetBrushSize(Painter->GetBrushSize() % UHexPlanetPainter::MaxBrushSize + 1);
	}
}

void AHexPlanetPlayerController::BeginPaintStroke()
{
	if (IsPaintModeOn())
	{
		Painter->BeginStroke(MouseCell);
	}
}

void AHexPlanetPlayerController::EndPaintStroke()
{
	if (Painter)
	{
		Painter->EndStroke();
	}
}

void AHexPlanetPlayerController::UndoPaint()
{
	if (Painter)
	{
		Painter->Undo();
	}
}

void AHexPlanetPlayerController::RedoPaint()
{
	if (Painter)
	{
		Painter->Redo();
	}
}
