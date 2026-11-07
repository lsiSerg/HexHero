// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexCameraConfig.generated.h"

class UCurveFloat;

/**
 *  Turn and scroll values of the camera over the hex world map.
 */
UCLASS(BlueprintType)
class UHexCameraConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** True if the camera starts free, turning all the way around and staying turned, and false if it starts top-down. hex.Camera set from the console wins */
	UPROPERTY(EditAnywhere, Category="Camera")
	bool bStartFree = true;

	/** Largest camera turn to either side while the turn input is held, in degrees. The free camera has no limit */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0, ClampMax=180))
	float MaxCameraTurn = 30.0f;

	/** Camera turn per pixel of mouse movement, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01))
	float CameraTurnPerPixel = 0.2f;

	/** Camera turn per second while a turn key is held, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=1))
	float CameraTurnKeySpeed = 90.0f;

	/** How fast the camera reaches the requested turn and returns to the default angle */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.1))
	float CameraTurnInterpSpeed = 10.0f;

	/** Scroll strength by the mouse position in the screen edge zone: X is 0 where the zone starts and 1 at the edge, Y is 1 at full speed */
	UPROPERTY(EditAnywhere, Category="Camera")
	TSoftObjectPtr<UCurveFloat> EdgeScrollCurve;

	/** Inertia after a map drag, as the time in seconds to lose most of the speed. Higher values make the map glide further */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01))
	float DragInertia = 0.6f;

	/** Inertia of the screen edge scroll, as the time in seconds to gain or lose most of the speed */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01))
	float EdgeScrollInertia = 0.12f;

	/** Inertia of the keyboard movement, as the time in seconds to gain or lose most of the speed */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01))
	float KeyboardInertia = 0.1f;

	/** How closely the speed kept after a map drag follows the mouse. Higher values follow the last moment before the release */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.1))
	float DragReleaseSmoothing = 15.0f;

	/** Downward angle of the camera of the flat map from far away, in degrees */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=10, ClampMax=90))
	float CameraPitch = 60.0f;

	/** Downward angle of the camera at its closest, which shows the sides of mountains and cliffs */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=10, ClampMax=90))
	float CloseCameraPitch = 38.0f;

	/** Camera distance from which the camera starts to lower towards its closest angle */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=100))
	float PitchChangeZoom = 2500.0f;

	/** Closest camera distance */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=100))
	float MinZoom = 600.0f;

	/** Farthest camera distance, which keeps most of the map out of view, as the terrain has no LODs */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=100))
	float MaxZoom = 6000.0f;

	/** Share of the current distance added or removed by one mouse wheel step */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=0.01, ClampMax=0.9))
	float ZoomStep = 0.15f;

	/** How fast the camera reaches the requested distance */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=0.1))
	float ZoomInterpSpeed = 10.0f;

	/** Movement per second as a multiple of the current distance */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=0.01))
	float MoveSpeed = 1.0f;

	/** How fast the camera glides to a hex it focuses on */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=0.1))
	float FocusInterpSpeed = 6.0f;

	/** Distance in pixels from the screen edge where the mouse starts moving the camera */
	UPROPERTY(EditAnywhere, Category="Flat Map", meta=(ClampMin=0))
	float EdgeScrollMargin = 80.0f;

	/** Exposure bias of the camera, as the level has a fixed exposure, which unlit water follows too */
	UPROPERTY(EditAnywhere, Category="Flat Map")
	float ExposureBias = -1.0f;
};
