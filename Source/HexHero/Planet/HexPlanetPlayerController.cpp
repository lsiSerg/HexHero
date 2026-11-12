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
#include "HexCastles.h"
#include "SHexCastlePanel.h"
#include "SHexHeroPanel.h"
#include "SHexEdgeMarkers.h"
#include "SHexModeBar.h"
#include "SHexHoverInfo.h"
#include "HexHeroes.h"
#include "HexSites.h"
#include "SHexPlanetPalette.h"
#include "HexPlanetPainter.h"
#include "HexPlanetUnderground.h"
#include "HexHero.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPlanetWallFade(TEXT("hex.PlanetWallFade"), 1,
		TEXT("1: the walls of the underground dissolve in front of the point under the cursor (CaveWallFade values of DA_HexUnderground); 2: in front of the point in the middle of the screen, or as high as hex.PlanetWallFadeScreenY says, for screenshots; 0: they stay whole"));

	TAutoConsoleVariable<int32> CVarPlanetHoverCell(TEXT("hex.PlanetHoverCell"), -1,
		TEXT("A cell taken to be under the cursor in place of the one the mouse is over, for screenshots of its outline and of what a click on it does; -1 for the mouse"));

	TAutoConsoleVariable<float> CVarPlanetWallFadeScreenY(TEXT("hex.PlanetWallFadeScreenY"), 0.5f,
		TEXT("Where hex.PlanetWallFade 2 puts the point the walls dissolve in front of, as a share of the height of the screen from its top"));
}

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
		DebugOverlay->AddViewStatus(&AHexPlanetCameraPawn::GetLightStatus);

		// the panel of the heroes, hidden on a planet without any
		const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();
		TWeakObjectPtr<AHexPlanetPlayerController> WeakThis(this);
		SAssignNew(HeroPanel, SHexHeroPanel)
			.OnHeroChosen(FOnHexHeroChosen::CreateUObject(this, &AHexPlanetPlayerController::ShowHero))
			.OnCastleChosen(FOnHexHeroChosen::CreateUObject(this, &AHexPlanetPlayerController::ShowCastle))
			.OnEndTurn(FSimpleDelegate::CreateUObject(this, &AHexPlanetPlayerController::EndHeroTurn))
			.OnRestart(FSimpleDelegate::CreateUObject(this, &AHexPlanetPlayerController::RestartGame))
			.OnProject([WeakThis](const FVector& Point, FVector2D& OutScreen)
			{
				// a point of the way of the hero chosen, where it shows: on the layer shown, on the side of the planet towards the camera
				const AHexPlanet* Planet = WeakThis.IsValid() ? WeakThis->GetPlanet() : nullptr;
				const UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
				const AActor* ViewTarget = Heroes ? WeakThis->GetViewTarget() : nullptr;
				return ViewTarget && Heroes->GetPathLayer() == Planet->GetLayer() && FVector::DotProduct(ViewTarget->GetActorLocation() - Point, Point) > 0.0
					&& WeakThis->ProjectWorldLocationToScreen(Point, OutScreen, true);
			})
			.OnSkip_Lambda([WeakThis]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->SkipHero();
				}
			})
			.SkipKeys(InputConfig && InputConfig->SkipHeroAction ? InputConfig->GetKeysText(InputConfig->SkipHeroAction) : FString())
			.EndTurnKeys(InputConfig && InputConfig->EndTurnAction ? InputConfig->GetKeysText(InputConfig->EndTurnAction) : FString())
			.NextHeroKeys(InputConfig && InputConfig->NextHeroAction ? InputConfig->GetKeysText(InputConfig->NextHeroAction) : FString())
			.GoKeys(InputConfig && InputConfig->HeroSendAction ? InputConfig->GetKeysText(InputConfig->HeroSendAction) : FString());
		ViewportClient->AddViewportWidgetContent(HeroPanel.ToSharedRef());

		// the marks at the edges of the screen that point to the heroes and castles out of view, over every other widget of the game, so a click
		// on a mark that lies over a button is the mark's; below the black of a trip, and gone while the menu or a dialog is open
		SAssignNew(EdgeMarkers, SHexEdgeMarkers)
			.OnLocate([WeakThis](const FVector3d& Point, EHexPlanetLayer Layer, FVector2D& OutScreen, FVector2D& OutDirection)
			{
				return WeakThis.IsValid() && WeakThis->LocateForMarker(Point, Layer, OutScreen, OutDirection);
			})
			.IsBlocked_Lambda([WeakThis]() { return WeakThis.IsValid() && (WeakThis->MapMenu.IsValid() || WeakThis->Dialog.IsValid()); });
		ViewportClient->AddViewportWidgetContent(EdgeMarkers.ToSharedRef(), 100);

		// the panel of a castle, shown while a person looks into one of its own
		SAssignNew(CastlePanel, SHexCastlePanel)
			.OnMessage([WeakThis](const FString& Message)
			{
				if (WeakThis.IsValid())
				{
					WeakThis->OnHeroMessage(Message);
				}
			});
		ViewportClient->AddViewportWidgetContent(CastlePanel.ToSharedRef());

		// Edit and Play at the top left, with the palette of the brush below them
		SAssignNew(ModeBar, SHexModeBar)
			.OnEdit_Lambda([WeakThis]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->SetPaintMode(!WeakThis->IsPaintModeOn());
				}
			})
			.OnPlay_Lambda([WeakThis]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->SetPlayMode(!WeakThis->IsPlayMode());
				}
			})
			.IsEditing_Lambda([WeakThis]() { return WeakThis.IsValid() && WeakThis->IsPaintModeOn(); })
			.IsPlaying_Lambda([WeakThis]() { return WeakThis.IsValid() && WeakThis->IsPlayMode(); })
			.EditKeys(InputConfig && InputConfig->PaintModeAction ? InputConfig->GetKeysText(InputConfig->PaintModeAction) : FString())
			.PlayKeys(InputConfig && InputConfig->PlayModeAction ? InputConfig->GetKeysText(InputConfig->PlayModeAction) : FString())
			.OnLayer_Lambda([WeakThis]()
			{
				// to the other layer at the same place, as the key does away from an entrance
				if (AHexPlanetCameraPawn* CameraPawn = WeakThis.IsValid() ? WeakThis->GetCameraPawn() : nullptr)
				{
					WeakThis->TravelToLayer(CameraPawn->GetShownLayer() == EHexPlanetLayer::Surface ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface);
				}
			})
			.IsUnderground_Lambda([WeakThis]()
			{
				const AHexPlanetCameraPawn* CameraPawn = WeakThis.IsValid() ? WeakThis->GetCameraPawn() : nullptr;
				return CameraPawn && CameraPawn->GetShownLayer() == EHexPlanetLayer::Underground;
			})
			.LayerKeys(InputConfig && InputConfig->LayerAction ? InputConfig->GetKeysText(InputConfig->LayerAction) : FString());
		ViewportClient->AddViewportWidgetContent(ModeBar.ToSharedRef());
	}
}

UHexHeroes* AHexPlanetPlayerController::GetHeroes() const
{
	const AHexPlanet* Planet = GetPlanet();
	return Planet && Planet->IsBuilt() ? Planet->GetHeroes() : nullptr;
}

void AHexPlanetPlayerController::OnHeroMessage(const FString& Message)
{
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 4.0f, FColor::Yellow, Message);
	}
}

void AHexPlanetPlayerController::OnHeroTravelled(int32 Id)
{
	// the camera goes down or up with the hero chosen; another hero goes alone
	UHexHeroes* Heroes = GetHeroes();
	const FHexHero* Hero = Heroes ? Heroes->FindHero(Id) : nullptr;
	AHexPlanetCameraPawn* CameraPawn = GetCameraPawn();
	if (Hero && CameraPawn && Heroes->GetSelectedId() == Id)
	{
		CameraPawn->ShowCell(Hero->Layer, Hero->Cell);
	}
}

void AHexPlanetPlayerController::OnHeroChosen(int32 Id)
{
	UHexHeroes* Heroes = GetHeroes();
	const FHexHero* Hero = Heroes ? Heroes->FindHero(Id) : nullptr;
	if (AHexPlanetCameraPawn* CameraPawn = Hero ? GetCameraPawn() : nullptr)
	{
		CameraPawn->ShowCell(Hero->Layer, Hero->Cell);
	}
}

bool AHexPlanetPlayerController::IsPlayMode() const
{
	const UHexHeroes* Heroes = GetHeroes();
	return Heroes && Heroes->IsPlaying();
}

void AHexPlanetPlayerController::SetPlayMode(bool bOn)
{
	UHexHeroes* Heroes = GetHeroes();
	if (!Heroes || Heroes->IsPlaying() == bOn)
	{
		return;
	}
	if (!bOn)
	{
		Heroes->StopPlay();
		return;
	}
	FString Problem;
	if (!Heroes->StartPlay(Problem))
	{
		OnHeroMessage(Problem);
		return;
	}
	// the game and the brush never run together
	if (IsPaintModeOn())
	{
		SetPaintMode(false);
	}
	ShowHero(Heroes->GetSelectedId());
}

void AHexPlanetPlayerController::ClickGame(bool bDouble)
{
	const AHexPlanet* Planet = GetPlanet();
	UHexHeroes* Heroes = GetHeroes();
	// a person plays on its turn alone
	if (!Heroes || !Heroes->IsPersonTurn() || IsBrushArmed())
	{
		return;
	}
	// a click on the panel of a castle is its own; one outside it has closed it already (PlayerTick)
	if (CastlePanel.IsValid() && CastlePanel->IsUnderCursor())
	{
		return;
	}
	// a click on a mark at the edge of the screen chooses its hero or castle and shows it
	if (bool bCastle = false; EdgeMarkers.IsValid())
	{
		if (int32 Id = 0; EdgeMarkers->GetHovered(bCastle, Id))
		{
			if (bCastle)
			{
				ShowCastle(Id);
			}
			else if (Heroes->CanChoose(Id))
			{
				Heroes->Select(Id);
				ShowHero(Id);
			}
			return;
		}
	}
	// a hero by its body, which stands over the cells behind it, else by its cell
	FVector Origin;
	FVector Direction;
	const FHexHero* Under = DeprojectMousePositionToWorld(Origin, Direction) ? Heroes->FindHeroAlongRay(Planet->GetLayer(), Origin, Direction) : nullptr;
	// a castle of the person is chosen by a click on it, with the hero that guards it, and two clicks open its panel; a click on the body
	// of that hero is a click on the castle, as the developer proposed on 2026-11-11, when telling the two apart by where a click landed
	// was not clear: an order then takes the hero out
	const UHexCastles* Castles = Heroes->GetCastles();
	const FHexCastle* Castle = !Castles ? nullptr : Under && Under->CastleId != 0 ? Castles->FindCastle(Under->CastleId)
		: !Under && MouseCell != INDEX_NONE ? Castles->FindCastleAt(Planet->GetLayer(), MouseCell) : nullptr;
	if (Castle && Castle->Owner == Heroes->GetShroudPlayer() && !Heroes->IsHidden(Castle->Owner, Castle->Layer, MouseCell))
	{
		Heroes->SelectCastle(Castle->Id);
		if (bDouble)
		{
			OnCastleOpened(Castle->Id);
		}
		return;
	}
	if (!Under)
	{
		Under = Heroes->FindHeroAt(Planet->GetLayer(), MouseCell);
	}
	// a hero the fog of war hides is not there to be chosen
	if (Under && Under->Id != Heroes->GetSelectedId() && Heroes->IsHidden(Heroes->GetShroudPlayer(), Under->Layer, Under->Cell))
	{
		Under = nullptr;
	}
	if (bDouble && Under && Heroes->CanChoose(Under->Id))
	{
		ShowHero(Under->Id);
		return;
	}
	if (!Under && MouseCell == INDEX_NONE)
	{
		return;
	}
	FString Message;
	// a click on the mouth of an entrance, in its mountain or wall, points the hero through it: to its cell on the other layer
	EHexPlanetLayer Layer = Planet->GetLayer();
	int32 Cell = MouseCell;
	if (!Under)
	{
		FindMouthUnderCursor(Layer, Cell);
	}
	Heroes->Click(Layer, Cell, Under, Message);
	if (!Message.IsEmpty())
	{
		OnHeroMessage(Message);
	}
}

bool AHexPlanetPlayerController::IsCursorOnUi() const
{
	return (EdgeMarkers.IsValid() && EdgeMarkers->IsCursorOnMarks()) || (CastlePanel.IsValid() && CastlePanel->IsUnderCursor());
}

bool AHexPlanetPlayerController::LocateForMarker(const FVector3d& Point, EHexPlanetLayer Layer, FVector2D& OutScreen, FVector2D& OutDirection) const
{
	const AHexPlanet* Planet = GetPlanet();
	const AActor* ViewTarget = GetViewTarget();
	OutDirection = FVector2D::ZeroVector;
	if (!Planet || !ViewTarget || !PlayerCameraManager)
	{
		return true;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	GetViewportSize(SizeX, SizeY);
	const FVector Camera = ViewTarget->GetActorLocation();
	const FRotationMatrix Axes(PlayerCameraManager->GetCameraRotation());
	// in view: on the layer shown, on the side of the planet towards the camera, within the screen
	const bool bFacing = FVector::DotProduct(Camera - FVector(Point), FVector(Point)) > 0.0;
	const bool bProjected = bFacing && ProjectWorldLocationToScreen(FVector(Point), OutScreen, true);
	const double Inset = 24.0;
	if (bProjected && Layer == Planet->GetLayer() && OutScreen.X > Inset && OutScreen.Y > Inset && OutScreen.X < SizeX - Inset && OutScreen.Y < SizeY - Inset)
	{
		return true;
	}
	if (bProjected)
	{
		// off the screen on the near side: the way to where it would show
		OutDirection = OutScreen - FVector2D(SizeX * 0.5, SizeY * 0.5);
	}
	if (OutDirection.IsNearlyZero())
	{
		// behind the planet, or right under the middle of the screen on the other layer: the way over the ground from what the camera looks at
		const FVector3d Focus = Planet->FindViewFocus(FVector3d(Camera), FVector3d(Axes.GetUnitAxis(EAxis::X))).GetSafeNormal();
		const FVector3d Target = Point.GetSafeNormal();
		FVector3d Along = Target - Focus * FVector3d::DotProduct(Target, Focus);
		if (Along.IsNearlyZero())
		{
			Along = FVector3d(Axes.GetUnitAxis(EAxis::Z));
		}
		OutDirection = FVector2D(FVector3d::DotProduct(Along, FVector3d(Axes.GetUnitAxis(EAxis::Y))), -FVector3d::DotProduct(Along, FVector3d(Axes.GetUnitAxis(EAxis::Z))));
		if (OutDirection.IsNearlyZero())
		{
			OutDirection = FVector2D(0.0, -1.0);
		}
	}
	return false;
}

bool AHexPlanetPlayerController::FindMouthUnderCursor(EHexPlanetLayer& OutLayer, int32& OutCell) const
{
	const AHexPlanet* Planet = GetPlanet();
	const UHexHeroes* Heroes = GetHeroes();
	if (!Planet || !Heroes || MouseCell == INDEX_NONE)
	{
		return false;
	}
	// of the entrances built into the cell under the cursor, a mountain may hold several, the one whose mouth is nearest the cursor
	const TArray<FHexEntrance>& Entrances = Planet->GetEntrances();
	double BestDistance = TNumericLimits<double>::Max();
	bool bFound = false;
	for (int32 Index = 0; Index < Entrances.Num(); ++Index)
	{
		const int32 Cell = Entrances[Index].Cell;
		if (Planet->GetEntranceProblem(Index) != HexPlanetUnderground::EEntranceProblem::None || Heroes->IsHidden(Heroes->GetShroudPlayer(), Planet->GetLayer(), Cell))
		{
			continue;
		}
		const FHexEntranceSeat Seat = Planet->GetEntranceSeat(Cell);
		if (!Seat.bEmbedded || (Seat.Behind != MouseCell && Seat.Behind2 != MouseCell))
		{
			continue;
		}
		const double Distance = FVector3d::DistSquared(Seat.Anchor, FVector3d(MouseSpot).GetSafeNormal());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			OutCell = Cell;
			OutLayer = Planet->GetLayer() == EHexPlanetLayer::Surface ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface;
			bFound = true;
		}
	}
	return bFound;
}

void AHexPlanetPlayerController::OnCastleOpened(int32 Id)
{
	const UHexHeroes* Heroes = GetHeroes();
	const FHexCastle* Castle = Heroes && Heroes->GetCastles() ? Heroes->GetCastles()->FindCastle(Id) : nullptr;
	if (!CastlePanel.IsValid() || !Castle || !Heroes->IsPersonTurn() || Castle->Owner != Heroes->GetShroudPlayer())
	{
		return;
	}
	CastlePanel->Open(Id);
	// the camera comes close, to where the castle shows a little right of the middle of the screen: it looks at the ground that far to
	// the left of the castle
	AHexPlanetCameraPawn* CameraPawn = GetCameraPawn();
	const AHexPlanet* Planet = GetPlanet();
	if (!CameraPawn || !Planet || Planet->GetLayer() != Castle->Layer || !PlayerCameraManager)
	{
		return;
	}
	const UHexCastleConfig* Config = Heroes->GetCastles()->GetConfig();
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const FVector Up = FVector(Cells[Castle->Cells[0]].Center + Cells[Castle->Cells[1]].Center + Cells[Castle->Cells[2]].Center).GetSafeNormal();
	const FVector Right = FVector::VectorPlaneProject(FRotationMatrix(PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y), Up).GetSafeNormal();
	const double Shift = double(Config->CastleScreenX - 0.5f) * 2.0 * double(Config->PanelCameraDistance) * FMath::Tan(FMath::DegreesToRadians(double(PlayerCameraManager->GetFOVAngle()) * 0.5));
	CameraPawn->FocusOn(Up * Planet->GetRadius() - Right * Shift, Config->PanelCameraDistance);
}

void AHexPlanetPlayerController::ShowCastle(int32 Id)
{
	UHexHeroes* Heroes = GetHeroes();
	const FHexCastle* Castle = Heroes && Heroes->GetCastles() ? Heroes->GetCastles()->FindCastle(Id) : nullptr;
	if (!Castle)
	{
		return;
	}
	// the one chosen already opens
	if (Heroes->GetSelectedCastleId() == Id)
	{
		OnCastleOpened(Id);
		return;
	}
	if (Heroes->SelectCastle(Id))
	{
		if (AHexPlanetCameraPawn* CameraPawn = GetCameraPawn())
		{
			CameraPawn->ShowCell(Castle->Layer, Castle->Cells[0]);
		}
	}
}

void AHexPlanetPlayerController::OnHeroSent(int32 Id)
{
	const AHexPlanet* Planet = GetPlanet();
	const UHexHeroes* Heroes = GetHeroes();
	const FHexHero* Hero = Heroes ? Heroes->FindHero(Id) : nullptr;
	AHexPlanetCameraPawn* CameraPawn = GetCameraPawn();
	if (Planet && Hero && Hero->Layer != Planet->GetLayer() && CameraPawn)
	{
		CameraPawn->ShowCell(Hero->Layer, Hero->Cell);
	}
}

void AHexPlanetPlayerController::OrderGame()
{
	const AHexPlanet* Planet = GetPlanet();
	UHexHeroes* Heroes = GetHeroes();
	if (!Planet || !Heroes || !Heroes->IsPersonTurn() || IsBrushArmed() || MouseCell == INDEX_NONE)
	{
		return;
	}
	// a click on the panel of a castle is its own
	if (CastlePanel.IsValid() && CastlePanel->IsUnderCursor())
	{
		return;
	}
	const FHexHero* Hero = Heroes->GetSelected();
	if (!Hero || !Heroes->CanChoose(Hero->Id))
	{
		return;
	}
	// a hero that walks stops, as any click stops it
	if (Hero->bWalking)
	{
		Heroes->Stop(Hero->Id);
		return;
	}
	FString Message;
	// a mark at the edge of the screen is no ground to go to
	if (bool bCastle = false; EdgeMarkers.IsValid())
	{
		if (int32 Id = 0; EdgeMarkers->GetHovered(bCastle, Id))
		{
			return;
		}
	}
	// onto the mouth of an entrance it goes through it
	EHexPlanetLayer Layer = Planet->GetLayer();
	int32 Cell = MouseCell;
	FindMouthUnderCursor(Layer, Cell);
	if (!Heroes->Send(Hero->Id, Layer, Cell, Message) && !Message.IsEmpty())
	{
		OnHeroMessage(Message);
	}
}

void AHexPlanetPlayerController::SkipHero()
{
	if (CastlePanel.IsValid())
	{
		CastlePanel->Close();
	}
	UHexHeroes* Heroes = GetHeroes();
	if (Heroes && Heroes->IsPersonTurn())
	{
		Heroes->Skip(Heroes->GetSelectedId());
	}
}

void AHexPlanetPlayerController::WalkHero()
{
	UHexHeroes* Heroes = GetHeroes();
	if (!Heroes || !Heroes->IsPlaying())
	{
		return;
	}
	// with a castle chosen the key opens its panel, unless the hero that guards it was shown a way, which it then walks
	if (const FHexHero* Guard = Heroes->GetSelected(); Heroes->GetSelectedCastleId() != 0 && (!Guard || Guard->TargetCell == INDEX_NONE))
	{
		OnCastleOpened(Heroes->GetSelectedCastleId());
		return;
	}
	// the key that sends a hero stops one that walks
	if (const FHexHero* Hero = Heroes->GetSelected(); Hero && Hero->bWalking && Heroes->IsPersonTurn())
	{
		Heroes->Stop(Hero->Id);
		return;
	}
	FString Message;
	if (!Heroes->Go(Heroes->GetSelectedId(), Message))
	{
		if (!Message.IsEmpty())
		{
			OnHeroMessage(Message);
		}
		// with nowhere to go the key brings the camera to the hero chosen, as the developer asked on 2026-11-12
		if (Heroes->IsPersonTurn())
		{
			ShowHero(Heroes->GetSelectedId());
		}
	}
}

void AHexPlanetPlayerController::RestartGame()
{
	UHexHeroes* Heroes = GetHeroes();
	if (Heroes && Heroes->IsPlaying())
	{
		Heroes->Restart();
		OnHeroMessage(TEXT("A new game: day 1"));
		ShowHero(Heroes->GetSelectedId());
	}
}

void AHexPlanetPlayerController::ShowHero(int32 Id)
{
	UHexHeroes* Heroes = GetHeroes();
	const FHexHero* Hero = Heroes ? Heroes->FindHero(Id) : nullptr;
	// while the game is played a person chooses only a hero of its own, on its turn
	if (!Hero || !Heroes->CanChoose(Id))
	{
		return;
	}
	Heroes->Select(Id);
	if (CastlePanel.IsValid())
	{
		CastlePanel->Close();
	}
	if (AHexPlanetCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->ShowCell(Hero->Layer, Hero->Cell);
	}
}

void AHexPlanetPlayerController::SelectNextHero()
{
	if (CastlePanel.IsValid())
	{
		CastlePanel->Close();
	}
	UHexHeroes* Heroes = GetHeroes();
	if (const FHexHero* Hero = Heroes && Heroes->IsPlaying() ? Heroes->SelectNext() : nullptr)
	{
		ShowHero(Hero->Id);
	}
}

void AHexPlanetPlayerController::EndHeroTurn()
{
	if (CastlePanel.IsValid())
	{
		CastlePanel->Close();
	}
	UHexHeroes* Heroes = GetHeroes();
	// its own turn alone; who moves next is said by the heroes
	if (Heroes && Heroes->IsPersonTurn())
	{
		Heroes->EndTurn();
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

		if (HeroPanel.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(HeroPanel.ToSharedRef());
		}

		if (EdgeMarkers.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(EdgeMarkers.ToSharedRef());
		}

		if (CastlePanel.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(CastlePanel.ToSharedRef());
		}

		if (ModeBar.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(ModeBar.ToSharedRef());
		}

		if (TravelScreen.IsValid())
		{
			ViewportClient->RemoveViewportWidgetContent(TravelScreen.ToSharedRef());
		}
	}

	TravelScreen.Reset();
	HeroPanel.Reset();
	EdgeMarkers.Reset();
	CastlePanel.Reset();
	ModeBar.Reset();
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
	// the legend asset lists the keys of both levels, of which each binds only some, once the pawn is possessed
	TSet<const UInputAction*> BoundActions;
	if (bLegendSet || !DebugOverlay.IsValid() || !UHexKeyLegendConfig::GetBoundActions(*this, BoundActions))
	{
		return;
	}

	const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();
	const UHexKeyLegendConfig* KeyLegend = GetDefault<UHexMapSettings>()->LoadKeyLegend();
	const TArray<FHexLegendSection> Sections = KeyLegend->GetSections(*InputConfig, &BoundActions);
	UHexKeyLegendConfig::LogSections(Sections, TEXT("the planet"));
	DebugOverlay->SetLegend(Sections, KeyLegend->GetHint(*InputConfig));
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
			: ShownPlanet && ShownPlanet->HasUnsavedLevels() ? FString::Printf(TEXT("%s not saved (%s)"), ShownPlanet->HasUnsavedCells() ? TEXT("Levels")
				: ShownPlanet->GetHeroes() && ShownPlanet->GetHeroes()->IsEdited() ? TEXT("Heroes and mines") : TEXT("Entrances"), *SaveKeys)
			: bSaved ? FString() : FString::Printf(TEXT("View not saved (%s)"), *SaveKeys));
	}

	if (!Planet || !Planet->IsBuilt())
	{
		return;
	}

	FVector Origin;
	FVector Direction;
	FVector MousePoint = FVector::ZeroVector;
	int32 Cell = DeprojectMousePositionToWorld(Origin, Direction) ? Planet->FindCellAlongRay(Origin, Direction, &MousePoint) : INDEX_NONE;
	MouseSpot = MousePoint;
	// hex.PlanetHoverCell stands in for the mouse
	if (const int32 Forced = CVarPlanetHoverCell.GetValueOnGameThread(); Planet->GetGrid().GetCells().IsValidIndex(Forced))
	{
		Cell = Forced;
		MousePoint = FVector(Planet->GetGrid().GetCells()[Forced].Center * Planet->GetCellTopRadius(Forced));
	}
	// the panel of a castle: under it nothing of the planet is picked, and anything done with the mouse outside it closes it, as the
	// developer asked on 2026-11-11
	if (CastlePanel.IsValid() && CastlePanel->IsOpen())
	{
		if (CastlePanel->IsUnderCursor())
		{
			Cell = INDEX_NONE;
		}
		else if (WasInputKeyJustPressed(EKeys::LeftMouseButton) || WasInputKeyJustPressed(EKeys::RightMouseButton) || WasInputKeyJustPressed(EKeys::MiddleMouseButton))
		{
			CastlePanel->Close();
		}
	}
	MouseCell = Cell;

	// the brush paints the layer shown, which it follows (UHexPlanetPainter::SetTarget); the river brush follows the corner nearest the point
	// under the mouse
	const bool bPainterOnPlanet = Painter && Painter->GetTarget() == Planet->GetLayer();
	if (bPainterOnPlanet && Cell != INDEX_NONE)
	{
		Painter->SetCursorPoint(MousePoint);
	}
	if (Painter)
	{
		// the arrow that shows where an entrance would stand and which way it would open
		Painter->UpdateEntranceHint(bPainterOnPlanet ? Cell : int32(INDEX_NONE));
	}

	if (bPainterOnPlanet && Painter->IsPainting())
	{
		Painter->ContinueStroke(Cell);
	}

	// while the game is played the cursor on a castle outlines its three cells as one, as the developer asked on 2026-11-11, and the
	// outline is thinner
	const UHexHeroes* Played = Planet->GetHeroes();
	const bool bPlayed = Played && Played->IsPlaying();
	TArray<int32, TInlineAllocator<3>> Group;
	if (const FHexCastle* Castle = bPlayed && Played->GetCastles() && Cell != INDEX_NONE ? Played->GetCastles()->FindCastleAt(Planet->GetLayer(), Cell) : nullptr;
		Castle && !(Played->IsShroudOn() && Played->IsHidden(Played->GetShroudPlayer(), Castle->Layer, Cell)))
	{
		Group.Append(Castle->Cells, 3);
	}
	Planet->SetHoveredCell(Cell, IsBrushArmed() ? Painter->GetOutlineSize() : 0, Group, bPlayed);

	// the heroes: the way of the one chosen to the cell under the cursor shows while no tool of the brush is chosen, and the camera glides
	// after it while it walks
	UHexHeroes* Heroes = Planet->GetHeroes();
	if (HeroPanel.IsValid())
	{
		HeroPanel->SetHeroes(Heroes);
	}
	if (EdgeMarkers.IsValid())
	{
		EdgeMarkers->SetHeroes(Heroes);
	}
	if (CastlePanel.IsValid())
	{
		CastlePanel->SetHeroes(Heroes);
	}
	if (Heroes)
	{
		if (BoundHeroes.Get() != Heroes)
		{
			BoundHeroes = Heroes;
			Heroes->GetCastles()->OnCastleOpened.AddUObject(this, &AHexPlanetPlayerController::OnCastleOpened);
			Heroes->OnMessage.AddUObject(this, &AHexPlanetPlayerController::OnHeroMessage);
			Heroes->OnHeroTravelled.AddUObject(this, &AHexPlanetPlayerController::OnHeroTravelled);
			Heroes->OnHeroChosen.AddUObject(this, &AHexPlanetPlayerController::OnHeroChosen);
			Heroes->OnHeroSent.AddUObject(this, &AHexPlanetPlayerController::OnHeroSent);
		}
		FVector3d FollowDirection;
		if (AHexPlanetCameraPawn* FollowPawn = GetCameraPawn(); FollowPawn && Heroes->GetFollowDirection(Planet->GetLayer(), FollowDirection))
		{
			FollowPawn->Follow(FVector(FollowDirection));
		}
	}
	// the walls of the underground dissolve in front of the point under the cursor, as in a game seen from above (M_HexUndergroundWall)
	const int32 WallFade = CVarPlanetWallFade.GetValueOnGameThread();
	if (WallFade == 2)
	{
		int32 Width = 0;
		int32 Height = 0;
		GetViewportSize(Width, Height);
		FVector MiddlePoint = FVector::ZeroVector;
		const bool bMiddle = DeprojectScreenPositionToWorld(Width * 0.5f, Height * FMath::Clamp(CVarPlanetWallFadeScreenY.GetValueOnGameThread(), 0.0f, 1.0f), Origin, Direction)
			&& Planet->FindCellAlongRay(Origin, Direction, &MiddlePoint) != INDEX_NONE;
		Planet->SetFadeFocus(bMiddle, MiddlePoint);
	}
	else
	{
		Planet->SetFadeFocus(Cell != INDEX_NONE && WallFade != 0, MousePoint);
	}

	// the panel follows hex.HoverInfo of the flat map
	static const IConsoleVariable* HoverInfoVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("hex.HoverInfo"));

	if (HoverInfo.IsValid())
	{
		FString Info = !HoverInfoVariable || HoverInfoVariable->GetBool() ? Planet->GetCellDebugText(Cell) : FString();
		// the fog of war tells nothing of a cell the player has not explored
		if (Heroes && !Info.IsEmpty() && Heroes->IsHidden(Heroes->GetShroudPlayer(), Planet->GetLayer(), Cell))
		{
			HoverInfo->SetInfo(TEXT("Unexplored"));
			return;
		}
		const FString HeroText = Heroes && !Info.IsEmpty() ? Heroes->GetCellText(Planet->GetLayer(), Cell) : FString();
		if (!HeroText.IsEmpty())
		{
			Info += TEXT("\n") + HeroText;
		}
		const FString SiteText = Heroes && !Info.IsEmpty() ? Heroes->GetSites()->GetCellText(Planet->GetLayer(), Cell) : FString();
		if (!SiteText.IsEmpty())
		{
			Info += TEXT("\n") + SiteText;
		}
		const FString CastleText = Heroes && !Info.IsEmpty() ? Heroes->GetCastles()->GetCellText(Planet->GetLayer(), Cell) : FString();
		if (!CastleText.IsEmpty())
		{
			Info += TEXT("\n") + CastleText;
		}
		HoverInfo->SetInfo(Info);
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
	// both layers, as the palette takes the biomes of each from its planet, so the underground's data is waited for if it is still read
	const UHexPlanetLayers* LayerSet = UHexPlanetLayers::Get(GetWorld());
	AHexPlanet* SurfacePlanet = LayerSet ? LayerSet->GetSurface() : GetPlanet();
	AHexPlanet* Underground = LayerSet ? LayerSet->GetUnderground() : nullptr;

	if (!Painter && SurfacePlanet && SurfacePlanet->IsBuilt())
	{
		if (Underground)
		{
			Underground->EnsureDataLoaded();
		}
		Painter = NewObject<UHexPlanetPainter>(this);
		Painter->Init(SurfacePlanet, Underground && Underground->IsDataLoaded() ? Underground : nullptr);
	}
	return Painter;
}

bool AHexPlanetPlayerController::IsPaintModeOn() const
{
	return Painter && Painter->IsActive();
}

bool AHexPlanetPlayerController::IsBrushArmed() const
{
	const AHexPlanet* Planet = GetPlanet();
	return IsPaintModeOn() && Painter->HasTool() && Planet && Painter->GetTarget() == Planet->GetLayer();
}

void AHexPlanetPlayerController::TravelToLayer(EHexPlanetLayer Layer)
{
	if (AHexPlanetCameraPawn* CameraPawn = GetCameraPawn(); CameraPawn && CameraPawn->GetShownLayer() != Layer && !CameraPawn->IsTravelling())
	{
		CameraPawn->TravelTo(Layer);
	}
}

void AHexPlanetPlayerController::SetTravelScreen(bool bShown)
{
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
	if (!ViewportClient || bShown == TravelScreen.IsValid())
	{
		return;
	}

	if (bShown)
	{
		// over the palette, the panels and the menu, as the loading screen of the planet
		TravelScreen = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Black);
		ViewportClient->AddViewportWidgetContent(TravelScreen.ToSharedRef(), 1000);
	}
	else
	{
		ViewportClient->RemoveViewportWidgetContent(TravelScreen.ToSharedRef());
		TravelScreen.Reset();
	}
}

void AHexPlanetPlayerController::SetPaintMode(bool bOn)
{
	UHexPlanetPainter* Brush = GetPainter();
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();

	if (!Brush || !ViewportClient)
	{
		return;
	}

	// the brush and the game never run together
	if (bOn && IsPlayMode())
	{
		SetPlayMode(false);
	}
	Brush->SetActive(bOn);

	if (bOn && !Palette.IsValid())
	{
		const UHexInputConfig* InputConfig = GetDefault<UHexMapSettings>()->LoadInputConfig();
		TWeakObjectPtr<AHexPlanetPlayerController> WeakThis(this);

		SAssignNew(Palette, SHexPlanetPalette)
			.Painter(Brush)
			.OnLayerClicked([WeakThis](EHexPlanetLayer Layer)
			{
				if (WeakThis.IsValid())
				{
					WeakThis->TravelToLayer(Layer);
				}
			})
			.PaintModeKeys(InputConfig->GetKeysText(InputConfig->PaintModeAction))
			.BrushSizeKeys(InputConfig->GetKeysText(InputConfig->BrushSizeAction))
			.UndoKeys(InputConfig->GetKeysText(InputConfig->UndoAction))
			.RedoKeys(InputConfig->GetKeysText(InputConfig->RedoAction))
			.SaveKeys(InputConfig->GetKeysText(InputConfig->SaveViewAction))
			.LayerKeys(InputConfig->LayerAction ? InputConfig->GetKeysText(InputConfig->LayerAction) : FString(TEXT("U")));

		ViewportClient->AddViewportWidgetContent(Palette.ToSharedRef());
	}
	else if (!bOn && Palette.IsValid())
	{
		ViewportClient->RemoveViewportWidgetContent(Palette.ToSharedRef());
		Palette.Reset();
	}
}

void AHexPlanetPlayerController::SetPaintLevelKey(int32 Number)
{
	UHexPlanetPainter* Brush = GetPainter();
	if (!Brush)
	{
		return;
	}

	// 1 to 5 choose deep sea to mountains on the surface, and underground 1 the void and 2 a passage
	if (Brush->GetTarget() == EHexPlanetLayer::Underground)
	{
		if (Number != 1 && Number != 2)
		{
			return;
		}
		Brush->SetLevel(Number == 1 ? HexPlanetUnderground::VoidLevel : HexPlanetUnderground::PassageLevel);
	}
	else
	{
		Brush->SetLevel(Number - 1 + UHexPlanetPainter::MinLevel);
	}
	SetPaintMode(true);
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
	if (IsBrushArmed())
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
	// the steps of the layer shown, each layer keeping its own
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

void AHexPlanetPlayerController::RunPaintCommand(const TArray<FString>& Args)
{
	UHexPlanetPainter* Brush = GetPainter();
	const AHexPlanet* Planet = GetPlanet();
	if (!Brush || !Planet || Args.IsEmpty())
	{
		UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetPaint: %s"), !Brush || !Planet ? TEXT("the planet is not built yet") : TEXT("needs an action, see its help"));
		return;
	}

	const FString& Action = Args[0];
	const auto GetInt = [&Args](int32 Index, int32 Default)
	{
		int32 Value = Default;
		return Args.IsValidIndex(Index) && LexTryParseString(Value, *Args[Index]) ? Value : Default;
	};
	const bool bCtrl = Args.ContainsByPredicate([](const FString& Each) { return Each.Equals(TEXT("ctrl"), ESearchCase::IgnoreCase); });

	if (Action == TEXT("on") || Action == TEXT("off"))
	{
		SetPaintMode(Action == TEXT("on"));
	}
	else if (Action == TEXT("tab") && Args.IsValidIndex(1))
	{
		for (int32 Tab = 0; Tab < NumPaintTabs; ++Tab)
		{
			if (UHexPlanetPainter::GetTabName(EHexPaintTab(Tab)).ToString().Equals(Args[1], ESearchCase::IgnoreCase))
			{
				SetPaintMode(true);
				Brush->SetTab(EHexPaintTab(Tab));
			}
		}
	}
	else if (Action == TEXT("key"))
	{
		SetPaintLevelKey(GetInt(1, 0));
	}
	else if (Action == TEXT("biome"))
	{
		Brush->SetBiome(GetInt(1, 0));
	}
	else if (Action == TEXT("size"))
	{
		Brush->SetBrushSize(GetInt(1, 1));
	}
	else if (Action == TEXT("network") && Args.IsValidIndex(1))
	{
		Brush->SetNetwork(HexPlanetFile::GetNetworkIndex(Args[1].IsEmpty() ? TEXT('A') : Args[1][0]));
	}
	else if (Action == TEXT("entrance"))
	{
		Brush->SetEntranceTool();
	}
	else if (Action == TEXT("unentrance"))
	{
		Brush->SetEntranceRemoveTool();
	}
	else if (Action == TEXT("turn"))
	{
		Brush->SetEntranceTurnTool();
	}
	else if (Action == TEXT("carve"))
	{
		Brush->SetCarveTool();
	}
	else if (Action == TEXT("walls") && Args.IsValidIndex(1))
	{
		Brush->SetWallTool(Args[1].Equals(TEXT("Auto"), ESearchCase::IgnoreCase) ? EHexPaintTool::WallAuto : EHexPaintTool::WallHeight);
	}
	else if (Action == TEXT("object"))
	{
		Brush->SetObjectSet(GetInt(1, 0));
	}
	else if (Action == TEXT("eraser"))
	{
		Brush->SetEraser();
	}
	else if (Action == TEXT("river"))
	{
		Brush->SetRiverTool();
	}
	else if (Action == TEXT("bridge"))
	{
		Brush->SetBridgeTool();
	}
	else if (Action == TEXT("relief") && Args.IsValidIndex(1))
	{
		const TCHAR* Names[] = {TEXT("Version"), TEXT("Turn"), TEXT("Mirror"), TEXT("Higher"), TEXT("Lower"), TEXT("Auto")};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			if (Args[1].Equals(Names[Index], ESearchCase::IgnoreCase))
			{
				Brush->SetReliefTool(EHexReliefTool(Index));
			}
		}
	}
	else if (Action == TEXT("none"))
	{
		Brush->ClearTool();
	}
	else if (Action == TEXT("stroke"))
	{
		// the cells of a stroke as the mouse would pass them, the first pressed and the last released
		TArray<int32> Cells;
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			// a cell, or @Latitude:Longitude in degrees for the cell there
			int32 Cell = 0;
			FString Latitude;
			FString Longitude;
			double LatitudeValue = 0.0;
			double LongitudeValue = 0.0;
			if (LexTryParseString(Cell, *Args[Index]))
			{
				Cells.Add(Cell);
			}
			else if (Args[Index].StartsWith(TEXT("@")) && Args[Index].Mid(1).Split(TEXT(":"), &Latitude, &Longitude) && LexTryParseString(LatitudeValue, *Latitude)
				&& LexTryParseString(LongitudeValue, *Longitude))
			{
				const double Phi = FMath::DegreesToRadians(LatitudeValue);
				const double Lambda = FMath::DegreesToRadians(LongitudeValue);
				Cells.Add(Planet->GetGrid().FindCell(FVector3d(FMath::Cos(Phi) * FMath::Cos(Lambda), FMath::Cos(Phi) * FMath::Sin(Lambda), FMath::Sin(Phi)), 0));
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("hex.PlanetPaint stroke over cells %s"), *FString::JoinBy(Cells, TEXT(" "), [](int32 Each) { return FString::FromInt(Each); }));
		if (Cells.IsEmpty() || !Brush->IsActive() || !Brush->HasTool() || Brush->GetTarget() != Planet->GetLayer())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetPaint stroke: needs cells, painting on with a tool, and the layer it paints shown"));
			return;
		}
		Brush->SetBackwards(bCtrl);
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			if (Cells.IsValidIndex(Index) && Planet->GetGrid().GetCells().IsValidIndex(Cells[Index]))
			{
				Brush->SetCursorPoint(FVector(Planet->GetGrid().GetCells()[Cells[Index]].Center * Planet->GetRadius()));
			}
			if (Index == 0)
			{
				Brush->BeginStroke(Cells[Index]);
			}
			else
			{
				Brush->ContinueStroke(Cells[Index]);
			}
		}
		Brush->EndStroke();
		Brush->SetBackwards(false);
	}
	else if (Action == TEXT("press") && Args.IsValidIndex(1))
	{
		// a stroke held as the left button holds it, which follows the cell under the mouse each frame until release
		if (!Brush->IsActive() || !Brush->HasTool() || Brush->GetTarget() != Planet->GetLayer() || !Planet->GetGrid().GetCells().IsValidIndex(GetInt(1, INDEX_NONE)))
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetPaint press: needs a cell, painting on with a tool, and the layer it paints shown"));
			return;
		}
		Brush->BeginStroke(GetInt(1, INDEX_NONE));
	}
	else if (Action == TEXT("release"))
	{
		Brush->EndStroke();
	}
	else if (Action == TEXT("regenerate") && Args.IsValidIndex(1) && Args[1].Equals(TEXT("sites"), ESearchCase::IgnoreCase))
	{
		Brush->GenerateSites();
	}
	else if (Action == TEXT("regenerate") && Args.IsValidIndex(1) && Args[1].Equals(TEXT("objects"), ESearchCase::IgnoreCase))
	{
		Brush->RegenerateObjects();
	}
	else if (Action == TEXT("regenerate"))
	{
		Brush->Regenerate(Args.IsValidIndex(1) && Args[1].Equals(TEXT("all"), ESearchCase::IgnoreCase) ? INDEX_NONE : Brush->GetNetwork());
	}
	else if (Action == TEXT("newlayout"))
	{
		Brush->NewLayout();
	}
	else if (Action == TEXT("byhand") && Args.IsValidIndex(1))
	{
		Brush->SetPlacingByHand(Args[1].Equals(TEXT("on"), ESearchCase::IgnoreCase));
	}
	else if (Action == TEXT("placed") && Args.IsValidIndex(1))
	{
		const TCHAR* Names[] = {TEXT("Place"), TEXT("Move"), TEXT("Turn"), TEXT("Size"), TEXT("Version"), TEXT("Erase")};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			if (Args[1].Equals(Names[Index], ESearchCase::IgnoreCase))
			{
				Brush->SetPlacedTool(EHexPlacedTool(Index));
			}
		}
	}
	else if ((Action == TEXT("place") || Action == TEXT("drag")) && Args.IsValidIndex(1))
	{
		// a click at a point, or a press there dragged to a second point and released, as @Latitude:Longitude in degrees or a cell for its middle
		TArray<FVector> Points;
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			FString Latitude;
			FString Longitude;
			double LatitudeValue = 0.0;
			double LongitudeValue = 0.0;
			int32 Cell = INDEX_NONE;
			if (LexTryParseString(Cell, *Args[Index]) && Planet->GetGrid().GetCells().IsValidIndex(Cell))
			{
				Points.Add(FVector(Planet->GetGrid().GetCells()[Cell].Center * Planet->GetRadius()));
			}
			else if (Args[Index].StartsWith(TEXT("@")) && Args[Index].Mid(1).Split(TEXT(":"), &Latitude, &Longitude) && LexTryParseString(LatitudeValue, *Latitude)
				&& LexTryParseString(LongitudeValue, *Longitude))
			{
				const double Phi = FMath::DegreesToRadians(LatitudeValue);
				const double Lambda = FMath::DegreesToRadians(LongitudeValue);
				Points.Add(FVector(FMath::Cos(Phi) * FMath::Cos(Lambda), FMath::Cos(Phi) * FMath::Sin(Lambda), FMath::Sin(Phi)) * Planet->GetRadius());
			}
		}
		if (Points.IsEmpty() || !Brush->IsActive() || !Brush->HasTool() || Brush->GetTarget() != Planet->GetLayer())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetPaint %s: needs @Latitude:Longitude, painting on with a tool, and the layer it paints shown"), *Action);
			return;
		}
		Brush->SetBackwards(bCtrl);
		for (int32 Index = 0; Index < (Action == TEXT("place") ? 1 : Points.Num()); ++Index)
		{
			const int32 Cell = Planet->GetGrid().FindCell(FVector3d(Points[Index]).GetSafeNormal(), 0);
			Brush->SetCursorPoint(Points[Index]);
			if (Index == 0)
			{
				Brush->BeginStroke(Cell);
			}
			else
			{
				Brush->ContinueStroke(Cell);
			}
		}
		Brush->EndStroke();
		Brush->SetBackwards(false);
	}
	else if (Action == TEXT("undo"))
	{
		Brush->Undo();
	}
	else if (Action == TEXT("redo"))
	{
		Brush->Redo();
	}
	else if (Action == TEXT("save"))
	{
		if (AHexPlanetCameraPawn* CameraPawn = GetCameraPawn())
		{
			CameraPawn->SavePlanet(nullptr);
		}
	}
	else
	{
		UE_LOG(LogHexHero, Warning, TEXT("hex.PlanetPaint: no action %s"), *Action);
		return;
	}

	const HexPlanetUnderground::FReport* Report = Planet->GetUndergroundReport();
	UE_LOG(LogHexHero, Log, TEXT("hex.PlanetPaint %s: painting %s on the %s, tab %s, tool %d, level %d, network %c, object %s%s, undo %s, unsaved %s, underground %s"), *FString::Join(Args, TEXT(" ")),
		Brush->IsActive() ? TEXT("on") : TEXT("off"), GetPlanetLayerName(Brush->GetTarget()), *UHexPlanetPainter::GetTabName(Brush->GetTab()).ToString(), int32(Brush->GetTool()),
		Brush->GetLevel(), HexPlanetFile::GetNetworkLetter(Brush->GetNetwork()), *Brush->GetObjectSetName(Brush->GetTarget(), Brush->GetObjectSet()).ToString(),
		Brush->IsPlacingByHand() ? TEXT(" by hand") : TEXT(""), Brush->CanUndo() ? TEXT("yes") : TEXT("no"), Planet->HasUnsavedChanges() ? TEXT("yes") : TEXT("no"),
		Report ? *Report->ToString() : TEXT("none"));
}

namespace
{
	void PlanetPaintCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (AHexPlanetPlayerController* PlayerController = World ? Cast<AHexPlanetPlayerController>(World->GetFirstPlayerController()) : nullptr)
		{
			PlayerController->RunPaintCommand(Args);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs PlanetPaintConsoleCommand(TEXT("hex.PlanetPaint"),
		TEXT("Drives the brush of the planet as the palette and the mouse would, for tests: on | off | tab <Ground|Relief|Objects|Rivers|Entrances|Maze|Links> (Objects of the layer shown) | ")
		TEXT("key <1-5> | biome <Index> | size <1-3> | network <A-H> | entrance | carve | walls <Height|Auto> | object <Index on the palette of the layer> | eraser | river | bridge | relief <Version|Turn|Mirror|Higher|Lower|Auto> | none | ")
		TEXT("byhand <on|off> | placed <Place|Move|Turn|Size|Version|Erase> | place @Latitude:Longitude [ctrl] | drag @Latitude:Longitude @Latitude:Longitude | ")
		TEXT("stroke <Cell or @Latitude:Longitude> [...] [ctrl] | press <Cell> | release | regenerate [all|objects|sites] | newlayout | undo | redo | save. A stroke presses on the first cell, passes the others and releases on the last; ctrl makes it take away; ")
		TEXT("press holds a stroke, which follows the mouse as the held left button does, until release; place clicks at a point, as the tools of objects placed by hand need, and drag presses at one and releases at the other"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlanetPaintCommand));
}
