// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HexPlanetGameMode.generated.h"

/**
 *  Game mode of the planet prototype, set as the override of Lvl_HexPlanet.
 */
UCLASS()
class AHexPlanetGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	AHexPlanetGameMode();
};
