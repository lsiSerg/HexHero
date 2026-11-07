// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetCloudConfig.generated.h"

/**
 *  Clouds of the planet and their shadows. AHexPlanet builds the shell from it and gives every value of the material groups to M_HexPlanetClouds,
 *  M_HexPlanet, its masked copy and M_HexPlanetWater as parameters of the same names, once when they are made, so the shape of the clouds is
 *  the same everywhere and their shadows lie under them. Tools/create_planet_assets.py creates DA_HexPlanetClouds only when it is missing.
 */
UCLASS(BlueprintType)
class UHexPlanetCloudConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Height of the clouds above the sea, about twice the mean peak. The shadows fall from there along the sun */
	UPROPERTY(EditAnywhere, Category="Shell", meta=(ClampMin=0))
	float CloudHeight = 400.0f;

	/** Rings of the shell from pole to pole, and twice as many segments around */
	UPROPERTY(EditAnywhere, Category="Shell", meta=(ClampMin=8, ClampMax=512))
	int32 CloudRings = 96;

	/** How many clouds span a radius of a planet of ReferenceFrequency in DA_HexPlanet; on others it scales with the frequency, so clouds keep their size in cells */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0.1))
	float CloudScale = 10.0f;

	/** Share of the sky the clouds cover */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0, ClampMax=1))
	float CloudCoverage = 0.33f;

	/** How wide the edges of the clouds fade, in the units of the noise */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0.001))
	float CloudSoftness = 0.25f;

	/** How much the noise is stretched around its middle, which makes the clouds more distinct */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0.1))
	float CloudContrast = 2.2f;

	/** How far the large octave shifts the finer ones, which curls the edges into puffs. Too much draws streaks */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0))
	float CloudCurl = 1.4f;

	/** Weight of the two finest octaves, so 0 leaves smooth blobs */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0))
	float CloudDetail = 1.0f;

	/** Turn of the clouds around the axis of a planet of ReferenceFrequency, in radians per second, faster on a smaller planet by its frequency,
	 *  so they keep their speed over the ground. The finer octaves also slide, so they change shape */
	UPROPERTY(EditAnywhere, Category="Shape")
	float CloudSpeed = 0.004f;

	/** How far each octave of the noise moves along the radius and back, in its cells, so the clouds grow, part and dissolve in place */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0))
	float CloudMorph = 0.6f;

	/** Pace of that change, in radians per second of the largest octave; the finer ones change faster */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0))
	float CloudMorphSpeed = 0.08f;

	/** Cells of the finest octave under one pixel from where the two finest octaves are gone, as they only draw streaks from far away */
	UPROPERTY(EditAnywhere, Category="Shape", meta=(ClampMin=0.01))
	float CloudDetailFade = 0.6f;

	/** Share of the light the thickest cloud takes from the land and the water under it */
	UPROPERTY(EditAnywhere, Category="Shadow", meta=(ClampMin=0, ClampMax=1))
	float CloudShadowStrength = 0.45f;

	/** Longest way from a point to its cloud along the sun, in heights of the clouds, so a low sun does not throw shadows from far away */
	UPROPERTY(EditAnywhere, Category="Shadow", meta=(ClampMin=0))
	float CloudShadowReach = 4.0f;

	/** Opacity of the thickest cloud seen from CloudFarHeight or further, as from the farthest camera */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0, ClampMax=1))
	float CloudOpacity = 0.65f;

	/** Brightness of a cloud in full sun, as the clouds are unlit and the exposure of the level is fixed */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float CloudBrightness = 0.8f;

	/** Color of the clouds where the sun is low */
	UPROPERTY(EditAnywhere, Category="Look")
	FLinearColor CloudDuskColor = FLinearColor(1.0f, 0.7f, 0.5f);

	/** Height of the sun over the horizon, as the sine of its angle, below which the clouds turn to the dusk color */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.01, ClampMax=1))
	float CloudDuskWidth = 0.35f;

	/** Brightness left to clouds past the terminator, before they thin out at night */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0, ClampMax=1))
	float CloudNightBrightness = 0.15f;

	/** Below this sine of the angle between the shell and the view the clouds fade, so the rim of the planet shows no stripes against the sky */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.01, ClampMax=1))
	float CloudEdgeOnFade = 0.12f;

	/** CloudEdgeOnFade of the sky, which towards the horizon is seen nearly edge on */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0.001, ClampMax=1))
	float CloudEdgeOnFadeNear = 0.05f;

	/** Height above the sea of the shell while the camera is under CloudSkySwitch, so close by it is a sky with clouds over the horizon */
	UPROPERTY(EditAnywhere, Category="Sky", meta=(ClampMin=0))
	float CloudSkyHeight = 1500.0f;

	/** Camera height above the sea around which the clouds below it hand over to the sky, which AHexPlanet draws as a second shell. The camera
	 *  never looks along a shell from its own height, as it would only see streaks */
	UPROPERTY(EditAnywhere, Category="Sky", meta=(ClampMin=0))
	float CloudSkySwitch = 800.0f;

	/** Camera heights on both sides of CloudSkySwitch over which the one shell fades out as the other fades in; a shell faded out is not drawn */
	UPROPERTY(EditAnywhere, Category="Sky", meta=(ClampMin=1))
	float CloudSkySwitchFade = 200.0f;

	/** Distance from the camera to a cloud, plus CloudHeight, where it is gone, so close by the camera is under them; straight down it is the
	 *  height of the camera above the sea */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float CloudFadeStart = 300.0f;

	/** Distance, measured as CloudFadeStart, from where a cloud has CloudNearOpacity */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float CloudFadeEnd = 1500.0f;

	/** Share of CloudOpacity left at CloudFadeEnd, so above the clouds close by the land shows through them */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0, ClampMax=1))
	float CloudNearOpacity = 0.5f;

	/** Distance, measured as CloudFadeStart, from where a cloud has all of CloudOpacity, rising from CloudNearOpacity at CloudFadeEnd */
	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0))
	float CloudFarHeight = 10000.0f;

	/** How sharply a side is shaded where its cloud rises towards the sun */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=0))
	float CloudRelief = 4.0f;

	/** How far towards the sun the shading looks, as a share of a cloud */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=0.001))
	float CloudLightStep = 0.15f;

	/** Shading of a flat cloud, from 0 for fully shaded to 1 for fully lit */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=0, ClampMax=1))
	float CloudShadeBias = 0.6f;

	/** Color of the shaded side */
	UPROPERTY(EditAnywhere, Category="Volume")
	FLinearColor CloudShadeColor = FLinearColor(0.45f, 0.47f, 0.53f);

	/** Extra brightness of the sides towards the sun */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=0))
	float CloudSunlitBoost = 0.15f;

	/** Thickness of the clouds around their height, through which the view is marched, so seen from the side they stand up as heaps */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=1))
	float CloudThickness = 300.0f;

	/** Most looks of the noise along the view through the clouds, which the view along them at the horizon needs; from above it takes fewer */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=1, ClampMax=16))
	float CloudVolumeSteps = 8.0f;

	/** Length of the view through the clouds per look, so from above, where it is short, it takes only a few */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=1))
	float CloudVolumeStep = 150.0f;

	/** How much a cloud takes of the light per unit of its length, so a thicker view through it is denser */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=0))
	float CloudVolumeDensity = 0.008f;

	/** How much more cloud the noise needs at the top and the bottom of the thickness than in its middle, so big clouds fill it and small ones
	 *  stay thin, which rounds them */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=0))
	float CloudVolumeRound = 0.3f;

	/** Brightness of the bottom of a cloud as a share of its top, which takes CloudShadeColor */
	UPROPERTY(EditAnywhere, Category="Volume", meta=(ClampMin=0, ClampMax=1))
	float CloudVolumeBaseShade = 0.5f;
};
