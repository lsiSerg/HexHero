// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetCameraConfig.generated.h"

/**
 *  The orbit camera of the planet, AHexPlanetCameraPawn. Its inertia, edge scroll and turn limits come from DA_HexCamera, shared with the flat map.
 *  Tools/create_planet_assets.py creates DA_HexPlanetCamera only when it is missing.
 */
UCLASS(BlueprintType)
class UHexPlanetCameraConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Downward angle of the camera at its farthest, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=10, ClampMax=90))
	float FarPitch = 90.0f;

	/** Downward angle of the camera at its closest, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=10, ClampMax=90))
	float ClosePitch = 40.0f;

	/** Closest distance from the point the camera looks at */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=100))
	float MinDistance = 600.0f;

	/** How far, in pixels, the mouse may move while the middle button is down for its release to count as a click, which centers the cell
	 *  under the mouse; further it drags the planet, as the left button does when the brush is off */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0))
	float ClickTolerance = 6.0f;

	/** Farthest distance, as a multiple of the radius of the planet, where all of the half it sees fits on the screen */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.5))
	float MaxDistanceRadii = 1.8f;

	/** Share of the distance added or removed by one mouse wheel step */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01, ClampMax=0.9))
	float ZoomStep = 0.15f;

	/** How fast the camera reaches the requested distance */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.1))
	float ZoomInterpSpeed = 10.0f;

	/** Movement over the surface per second, as a multiple of the distance */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01))
	float MoveSpeed = 1.0f;

	/** Turn per second of the turn keys, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=1))
	float TurnSpeed = 90.0f;

	/** Turn per pixel of the mouse while the right button is held, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01))
	float MouseTurnSpeed = 0.3f;

	/** How fast hex.PlanetFocus and the middle mouse button glide to a cell */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.1))
	float FocusInterpSpeed = 5.0f;

	/** Width of the zone at the screen edge where the mouse moves the camera, in pixels */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=1))
	float EdgeScrollMargin = 80.0f;

	/** Exposure bias of the camera, as the level has a fixed exposure */
	UPROPERTY(EditAnywhere, Category="Light")
	float ExposureBias = -1.0f;

	/** Direction of the sunlight in the frame of the camera: forward, right and up, where it comes from */
	UPROPERTY(EditAnywhere, Category="Light")
	FVector SunFromCamera = FVector(-0.3, 0.6, 1.0);
};
