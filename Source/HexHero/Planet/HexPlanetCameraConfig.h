// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetLayers.h"
#include "HexPlanetCameraConfig.generated.h"

/**
 *  The light of a layer of the planet, which the camera applies when the layer is shown (AHexPlanetCameraPawn::ApplyLighting): the sun of the
 *  level, which follows the point the camera looks at, and the exposure of the camera. The sky light of a layer is SkyLightColor and
 *  SkyLightIntensity of its UHexPlanetConfig, as each layer has its own. UHexPlanetCameraConfig::GetLighting gives the light of a layer: the
 *  surface from the values of the config as they were, the underground from UndergroundLighting, dim and cool since 2026-11-10, when the
 *  developer decided that the underground is dark but around the hero (UHexPlanetConfig::bHeroLight), what glows and the exits
 */
USTRUCT()
struct FHexLayerLighting
{
	GENERATED_BODY()

	/** Color of the sun and its intensity in lux; an intensity of 0 keeps those of the light placed in the level */
	UPROPERTY(EditAnywhere, Category="Light")
	FLinearColor SunColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float SunIntensity = 0.0f;

	/** Intensity of the sun under the forced light of L (hex.PlanetForcedLight), which lights the layer evenly, in lux, never below the intensity
	 *  above, so 0 keeps it */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float ForcedSunIntensity = 0.0f;

	/** Direction the sun shines from, as SunAzimuth of the config */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=-360, ClampMax=360))
	float SunAzimuth = 120.0f;

	/** Height of the sun with the camera at its closest and at its farthest, as SunCloseElevation and SunFarElevation of the config */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=1, ClampMax=90))
	float SunCloseElevation = 40.0f;

	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=1, ClampMax=90))
	float SunFarElevation = 60.0f;

	/** Exposure bias of the camera */
	UPROPERTY(EditAnywhere, Category="Light")
	float ExposureBias = -1.0f;

	/** Returns the light of the underground: a faint, cool key light, by which the maze stays readable in the dark (1.5 lux until 2026-11-10,
	 *  a placeholder that lit everything evenly, which the forced light of L brings back) */
	static FHexLayerLighting MakeUnderground()
	{
		FHexLayerLighting Lighting;
		Lighting.SunColor = FLinearColor(0.55f, 0.68f, 0.85f, 1.0f);
		Lighting.SunIntensity = 0.12f;
		Lighting.ForcedSunIntensity = 1.5f;
		Lighting.SunCloseElevation = 55.0f;
		Lighting.SunFarElevation = 70.0f;
		Lighting.ExposureBias = 0.5f;
		return Lighting;
	}
};

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

	/** Seconds within which two clicks of the left button on the same cell, each within ClickTolerance, are a double click, which goes through an
	 *  entrance or exit there to the other layer, as the key of the layer does over it; only while no tool of the brush is chosen, as its clicks
	 *  paint (the engine's own DoubleClickTime of the input settings is 0.2, too short for many players) */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.1, ClampMax=1))
	float DoubleClickTime = 0.4f;

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

	/** A glide of the camera that was asked for, to a hero, a castle or a cell, is not stopped by the cursor it left at the edge of the screen:
	 *  the edge scrolls again only once the mouse has moved this many pixels from where it was then. The developer found on 2026-11-12 that a
	 *  click on a mark at the edge sent the camera off, the mark went away from under the cursor, and the scroll of the edge stopped the
	 *  camera before it was there */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0))
	float EdgeScrollHoldDistance = 12.0f;

	/** With Shift held the keys and the edges of the screen move it this many times as fast, as the developer asked on 2026-11-12 */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=1))
	float ShiftMoveFactor = 2.5f;

	/** Turn per second of the turn keys, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=1))
	float TurnSpeed = 90.0f;

	/** Turn per pixel of the mouse while the right button is held, in degrees */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.01))
	float MouseTurnSpeed = 0.3f;

	/** How fast hex.PlanetFocus and the middle mouse button glide to a cell */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.1))
	float FocusInterpSpeed = 5.0f;

	/** How long a jump of the camera to another place (hex.PlanetSetCamera) waits at most, in seconds, while the planet builds the pieces of
	 *  the chunks around that place, so it lands on a whole surface rather than coarse meshes with gaps between them */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0))
	float JumpWaitMax = 2.0f;

	/** Width of the zone at the screen edge where the mouse moves the camera, in pixels */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=1))
	float EdgeScrollMargin = 80.0f;

	/** Exposure bias of the camera, as the level has a fixed exposure */
	UPROPERTY(EditAnywhere, Category="Light")
	float ExposureBias = -1.0f;

	/** How much the camera blurs what moves on the screen, 0 for none: a hero, a site or a castle placed anew, which the editor does as it
	 *  drags one, is drawn in its first frame from where its components were made, which the blur smeared over the whole screen for that
	 *  frame, as the developer saw on 2026-11-13 (the default of the engine, 0.5, before); the field of a battle takes it too */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0, ClampMax=1))
	float MotionBlurAmount = 0.0f;

	/** Direction the sun shines from over the point the camera looks at, in degrees from north towards the right of the screen of a camera
	 *  facing north, so turning the camera turns no shadow; 120 is from behind and to the right of the usual view */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=-360, ClampMax=360))
	float SunAzimuth = 120.0f;

	/** Height of the sun over the horizon of that point with the camera at its closest, in degrees, low so shadows are long */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=1, ClampMax=90))
	float SunCloseElevation = 40.0f;

	/** Height of the sun with the camera at its farthest, in degrees, high so all of the half it sees is lit */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=1, ClampMax=90))
	float SunFarElevation = 60.0f;

	/** How far the sun may lag behind where it should stand for the view before it moves, as the distance the point the camera looks at
	 *  travelled over the distance of the camera, so it waits longer from far away; while it lags less, shadows stand still */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float SunLagShare = 0.5f;

	/** Most the sun may lag before it moves, in degrees, so from far away it does not leave part of the view in the dark */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0, ClampMax=90))
	float SunLagMaxDegrees = 8.0f;

	/** How long the camera must stand still before a sun that lags too far moves, in seconds, so it does not move while the camera does */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float SunSettleTime = 0.3f;

	/** Lag at which the sun moves even while the camera still moves, as a multiple of the lag allowed, so it never falls far behind */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=1))
	float SunMaxLagFactor = 3.0f;

	/** How long the sun takes to move to where it should stand, in seconds */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0.01))
	float SunMoveTime = 1.5f;

	/** The light of the underground: a faint, cool key light that follows the camera as the sun does, so the dark maze stays readable, and its
	 *  own exposure; its dim sky light is that of DA_HexUnderground, and the light around the hero its HeroLight* */
	UPROPERTY(EditAnywhere, Category="Light|Underground")
	FHexLayerLighting UndergroundLighting = FHexLayerLighting::MakeUnderground();

	/** Returns the light of a layer: the surface keeps the sun of the level with SunAzimuth, SunCloseElevation, SunFarElevation and
	 *  ExposureBias above, the underground takes UndergroundLighting */
	FHexLayerLighting GetLighting(EHexPlanetLayer Layer) const
	{
		if (Layer == EHexPlanetLayer::Underground)
		{
			return UndergroundLighting;
		}
		FHexLayerLighting Surface;
		Surface.SunAzimuth = SunAzimuth;
		Surface.SunCloseElevation = SunCloseElevation;
		Surface.SunFarElevation = SunFarElevation;
		Surface.ExposureBias = ExposureBias;
		return Surface;
	}

	/** How long the screen fades to black before a trip between the layers (AHexPlanetCameraPawn::TravelTo), and back after it, in seconds */
	UPROPERTY(EditAnywhere, Category="Travel", meta=(ClampMin=0))
	float TravelFadeOutTime = 0.35f;

	UPROPERTY(EditAnywhere, Category="Travel", meta=(ClampMin=0))
	float TravelFadeInTime = 0.45f;

	/** Least time the screen stays black on a trip after the layers were swapped, in real seconds, while the layer shown draws its first frames
	 *  and its pieces around the camera are built; it waits for them at most JumpWaitMax. Counted from the end of the swap in real time, as
	 *  the long frame of the swap would use up a hold counted in game time from the start of the black screen */
	UPROPERTY(EditAnywhere, Category="Travel", meta=(ClampMin=0))
	float TravelHoldMin = 0.2f;

	/** Least frames drawn black after the swap, so the new layer has drawn before it fades in, however fast the frames come */
	UPROPERTY(EditAnywhere, Category="Travel", meta=(ClampMin=0))
	int32 TravelHoldFrames = 2;
};
