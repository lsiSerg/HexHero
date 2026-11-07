// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetGameMode.h"
#include "HexPlanetCameraPawn.h"
#include "HexPlanetPlayerController.h"

AHexPlanetGameMode::AHexPlanetGameMode()
{
	DefaultPawnClass = AHexPlanetCameraPawn::StaticClass();
	PlayerControllerClass = AHexPlanetPlayerController::StaticClass();
}
