// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexMapGameMode.h"
#include "HexCameraPawn.h"
#include "HexMapPlayerController.h"

AHexMapGameMode::AHexMapGameMode()
{
	DefaultPawnClass = AHexCameraPawn::StaticClass();
	PlayerControllerClass = AHexMapPlayerController::StaticClass();
}
