// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexMapWaterConfig.generated.h"

/**
 *  The look of the water of the flat map: the sea, ice and foam of M_HexWater, the surge that M_HexCliff and M_HexMountain share, and the water of
 *  rivers in M_HexRiver. AHexWorldMap gives every value of it to all its materials as parameters of the same names (HexMaterialConfig::Apply), and
 *  Tools/build_hex_materials.py adds a parameter for each of them that the code of a material names. Distances are in units of the world, and
 *  depths and distances to the shore come from the shore field. A wave is (frequency, speed, slope, height). Created by that script only when missing.
 */
UCLASS(BlueprintType)
class UHexMapWaterConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Opacity of the water over land seen through it, the shore and shallows; the biome of water sets its own */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0, ClampMax=1))
	float ShoreOpacity = 0.25f;

	/** Clear water next to land, and the turquoise further out */
	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor LagoonColor = FLinearColor(0.03f, 0.56f, 0.52f);

	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor ShallowColor = FLinearColor(0.01f, 0.34f, 0.42f);

	/** Distance to the shore where the lagoon turns turquoise */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float LagoonDistanceStart = 10.0f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float LagoonDistanceEnd = 150.0f;

	/** Deep water near land, and the distance to land where it turns into the open sea */
	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor OffshoreColor = FLinearColor(0.012f, 0.15f, 0.24f);

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float OffshoreDistanceStart = 30.0f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float OffshoreDistanceEnd = 300.0f;

	/** The open sea, whose tone drifts between these in wide patches */
	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor DeepColorDark = FLinearColor(0.004f, 0.03f, 0.085f);

	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor DeepColorLight = FLinearColor(0.008f, 0.085f, 0.17f);

	/** Contrast of the wide and the finer patches of the open sea, and the share of the finer ones */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float PatchContrast = 1.8f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float PatchFineContrast = 1.2f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0, ClampMax=1))
	float PatchFineWeight = 0.25f;

	/** How unevenly by place the depth counts for the opacity, and the scale of that noise per tile */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float SeabedDepthNoise = 12.0f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0.001))
	float FadeNoiseScale = 0.09f;

	/** Scales of the wide and the finer noise that bend the swell and drift the tone, per tile, and how far they bend the swell */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0.001))
	float WideNoiseScale = 0.11f;

	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0.001))
	float MiddleNoiseScale = 0.37f;

	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float SwellBend = 420.0f;

	/** The three long swells, which the floes of ice also ride on */
	UPROPERTY(EditAnywhere, Category="Waves")
	FVector4 SwellWaveA = FVector4(0.042, 0.95, 0.5, 3.2);

	UPROPERTY(EditAnywhere, Category="Waves")
	FVector4 SwellWaveB = FVector4(0.071, 1.3, 0.32, 1.8);

	UPROPERTY(EditAnywhere, Category="Waves")
	FVector4 SwellWaveC = FVector4(0.118, 1.75, 0.2, 0.9);

	/** The two short waves seen only from close by */
	UPROPERTY(EditAnywhere, Category="Waves")
	FVector4 ChopWaveA = FVector4(0.19, 2.1, 0.3, 0.5);

	UPROPERTY(EditAnywhere, Category="Waves")
	FVector4 ChopWaveB = FVector4(0.27, 2.6, 0.22, 0.28);

	/** Scales the height the waves lift the vertices */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float WaveHeight = 1.0f;

	/** Size of a pixel on the water up to which the short waves and ripples are fully seen, and past which they are gone */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float CloseUpStart = 1.0f;

	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float CloseUpEnd = 3.0f;

	/** Distance from the camera up to which the short waves lift the vertices, and past which they no longer do */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float ChopLiftStart = 1200.0f;

	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float ChopLiftEnd = 2600.0f;

	/** How much everything seen on the water sways with the swell */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float WaterSway = 0.05f;

	/** Scales of the two layers of ripples and of the lace of foam, per tile */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0.01))
	float RippleScaleA = 2.3f;

	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0.01))
	float RippleScaleB = 1.1f;

	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0.01))
	float LaceScale = 2.6f;

	/** How much the normal tilts: by the swell far away and close by, by ripples far away and close by */
	UPROPERTY(EditAnywhere, Category="Waves")
	FVector4 NormalStrength = FVector4(0.14, 0.32, 0.12, 0.22);

	/** Depth of the seabed below which the water is calm over shallows, and from which it is the open sea */
	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0))
	float CalmDepthStart = 18.0f;

	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0))
	float CalmDepthEnd = 40.0f;

	/** What calm shallows keep: of the slope of waves, of ripples, of glints and of the height of waves */
	UPROPERTY(EditAnywhere, Category="Calm")
	FVector4 ShallowCalm = FVector4(0.45, 0.6, 0.45, 0.4);

	/** What calm shallows keep of the foam of waves and of rollers, and of the line of surf */
	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0, ClampMax=1))
	float ShallowFoamCalm = 0.3f;

	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0, ClampMax=1))
	float ShallowSurfCalm = 0.4f;

	/** Share of the height of waves left next to land, growing to full at NearLandDistance, and how much higher waves stand against cliffs */
	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0, ClampMax=1))
	float NearLandHeight = 0.3f;

	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=1))
	float NearLandDistance = 130.0f;

	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0))
	float CliffWaveHeight = 1.5f;

	/** Waves of the shore: the distance between them, seconds between two, how far out they grow from and are gone, and how much they tilt the water */
	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=1))
	float WaveLength = 85.0f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0.1))
	float WavePeriod = 5.5f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0))
	float WaveReachStart = 20.0f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0))
	float WaveReachEnd = 190.0f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0))
	float WaveSlope = 0.8f;

	/** Distance from the shore where most waves are born, and where the furthest are */
	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0))
	float WaveBirthNear = 50.0f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0))
	float WaveBirthFar = 185.0f;

	/** Width of the crest of foam far out and close in, as a share of a wave */
	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0, ClampMax=1))
	float CrestWidthFar = 0.04f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0, ClampMax=1))
	float CrestWidthNear = 0.18f;

	/** How much the crest of a wave foams, and how much lace it leaves behind */
	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0, ClampMax=1))
	float WaveFoamAmount = 0.7f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0, ClampMax=1))
	float WaveLaceAmount = 0.4f;

	/** Width of the line of surf at the edge of the water, and how much it foams */
	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0.01))
	float SurfWidth = 9.0f;

	UPROPERTY(EditAnywhere, Category="Shore Waves", meta=(ClampMin=0, ClampMax=1))
	float SurfAmount = 0.65f;

	/** Single waves that now and then roll in: how far out they start, how seldom (higher is rarer), seconds between two, how long they are and how much they foam */
	UPROPERTY(EditAnywhere, Category="Rollers", meta=(ClampMin=0))
	float RollerDistance = 95.0f;

	UPROPERTY(EditAnywhere, Category="Rollers", meta=(ClampMin=0, ClampMax=1))
	float RollerRarity = 0.6f;

	UPROPERTY(EditAnywhere, Category="Rollers", meta=(ClampMin=0.1))
	float RollerPeriod = 14.0f;

	UPROPERTY(EditAnywhere, Category="Rollers", meta=(ClampMin=0.01))
	float RollerWidth = 20.0f;

	UPROPERTY(EditAnywhere, Category="Rollers", meta=(ClampMin=0, ClampMax=1))
	float RollerAmount = 0.85f;

	/** The film at the edge: the least strength of its faint stretches, seconds of a swash, the least reach of a swash and how bright the line of touch is */
	UPROPERTY(EditAnywhere, Category="Edge Foam", meta=(ClampMin=0, ClampMax=1))
	float EdgeFoamMinStrength = 0.6f;

	UPROPERTY(EditAnywhere, Category="Edge Foam", meta=(ClampMin=0.1))
	float SwashPeriod = 9.0f;

	UPROPERTY(EditAnywhere, Category="Edge Foam", meta=(ClampMin=0, ClampMax=1))
	float SwashMinReach = 0.65f;

	UPROPERTY(EditAnywhere, Category="Edge Foam", meta=(ClampMin=0, ClampMax=1))
	float EdgeTouchAmount = 0.8f;

	/** Foam against cliffs: how fast it flows, the distance between its fronts, its reach, the noise on that and how far a surge adds */
	UPROPERTY(EditAnywhere, Category="Cliff Foam", meta=(ClampMin=0))
	float CliffFoamFlowSpeed = 0.3f;

	UPROPERTY(EditAnywhere, Category="Cliff Foam", meta=(ClampMin=1))
	float CliffFoamFrontSpacing = 24.0f;

	UPROPERTY(EditAnywhere, Category="Cliff Foam", meta=(ClampMin=0))
	float CliffFoamReach = 6.0f;

	UPROPERTY(EditAnywhere, Category="Cliff Foam", meta=(ClampMin=0))
	float CliffFoamReachNoise = 8.0f;

	UPROPERTY(EditAnywhere, Category="Cliff Foam", meta=(ClampMin=0))
	float CliffFoamSurgeReach = 36.0f;

	/** Seconds between surges of the two tracks, and how far a surge heaves the water against a cliff */
	UPROPERTY(EditAnywhere, Category="Surge", meta=(ClampMin=0.1))
	float SurgePeriodA = 11.0f;

	UPROPERTY(EditAnywhere, Category="Surge", meta=(ClampMin=0.1))
	float SurgePeriodB = 17.0f;

	UPROPERTY(EditAnywhere, Category="Surge", meta=(ClampMin=0))
	float SurgeHeave = 18.0f;

	/** Color of all foam on the water and on rivers */
	UPROPERTY(EditAnywhere, Category="Foam")
	FLinearColor FoamColor = FLinearColor(0.74f, 0.83f, 0.84f);

	/** Direction the sun comes from, which the lighter side of waves and glints use. AHexWorldMap takes it from the sun of the level */
	UPROPERTY(EditAnywhere, Category="Light")
	FVector SunDirection = FVector(-0.166, 0.621, 0.766);

	/** How much lighter the side of a wave towards the sun is */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float WaveLight = 1.5f;

	/** Color of the sky that the water reflects where it faces away from the camera, how much of it at a grazing view, and how close to the horizon */
	UPROPERTY(EditAnywhere, Category="Light")
	FLinearColor SkyColor = FLinearColor(0.3f, 0.46f, 0.62f);

	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0, ClampMax=1))
	float SkyReflection = 0.4f;

	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0.1))
	float FresnelPower = 4.0f;

	/** Strength and color of the glints of the sun */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0))
	float GlintAmount = 1.0f;

	UPROPERTY(EditAnywhere, Category="Glints")
	FLinearColor GlintColor = FLinearColor(0.9f, 0.88f, 0.8f);

	/** Scales of the two noises whose product makes the sparkles, per tile */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0.01))
	float GlintScaleA = 3.9f;

	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0.01))
	float GlintScaleB = 5.3f;

	/** Product of the two noises where a sparkle starts and where it is full: closer values make fewer and sharper ones */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0, ClampMax=1))
	float GlintThresholdStart = 0.36f;

	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0, ClampMax=1))
	float GlintThresholdEnd = 0.6f;

	/** Glints left off the path of the sun, and the cosine between the reflected view and the sun where the path starts and is full */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0, ClampMax=1))
	float SunPathMin = 0.2f;

	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=-1, ClampMax=1))
	float SunPathStart = 0.25f;

	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=-1, ClampMax=1))
	float SunPathEnd = 0.85f;

	/** Size of a pixel where the glints start to fade and where they are gone, as one-pixel sparkles only look like noise */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0))
	float GlintFarStart = 0.9f;

	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0))
	float GlintFarEnd = 2.0f;

	/** Opacity of frozen water, and the share of hexes of ice at the edge that crumble whole */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceOpacity = 0.72f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceCrumble = 0.18f;

	/** Small waves that run in to the ice: the distance between them, seconds between two, how far out they start and how much they foam */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=1))
	float IceWaveSpacing = 58.0f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0.1))
	float IceWavePeriod = 6.5f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=1))
	float IceWaveReach = 80.0f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceFoamAmount = 0.75f;

	/** Base tone of ice, which the surface below follows */
	UPROPERTY(EditAnywhere, Category="Ice")
	FLinearColor IceColor = FLinearColor(0.38f, 0.55f, 0.64f);

	/** Size of a floe, how much their tones differ, how far the swell tips them and how much lighter their rims are */
	UPROPERTY(EditAnywhere, Category="Ice|Floes", meta=(ClampMin=1))
	float FloeSize = 32.0f;

	UPROPERTY(EditAnywhere, Category="Ice|Floes", meta=(ClampMin=0))
	float FloeToneVariation = 0.18f;

	UPROPERTY(EditAnywhere, Category="Ice|Floes", meta=(ClampMin=0))
	float FloeTilt = 0.07f;

	UPROPERTY(EditAnywhere, Category="Ice|Floes", meta=(ClampMin=0))
	float FloeRimLight = 0.25f;

	/** The surface of ice: blue where bare, paler where clear, white under heaps of snow, and how much its relief shows */
	UPROPERTY(EditAnywhere, Category="Ice|Surface")
	FLinearColor IceBareColor = FLinearColor(0.62f, 0.72f, 0.78f);

	UPROPERTY(EditAnywhere, Category="Ice|Surface")
	FLinearColor IceClearColor = FLinearColor(0.72f, 0.79f, 0.83f);

	UPROPERTY(EditAnywhere, Category="Ice|Surface")
	FLinearColor IceSnowColor = FLinearColor(0.84f, 0.87f, 0.9f);

	UPROPERTY(EditAnywhere, Category="Ice|Surface", meta=(ClampMin=0))
	float IceRelief = 0.14f;

	/** Size of the cells whose sides are cracks, their colors, dark or letting the water through, and how strongly they show */
	UPROPERTY(EditAnywhere, Category="Ice|Cracks", meta=(ClampMin=1))
	float CrackSize = 125.0f;

	UPROPERTY(EditAnywhere, Category="Ice|Cracks")
	FLinearColor CrackDarkColor = FLinearColor(0.03f, 0.11f, 0.17f);

	UPROPERTY(EditAnywhere, Category="Ice|Cracks")
	FLinearColor CrackOpenColor = FLinearColor(0.05f, 0.42f, 0.46f);

	UPROPERTY(EditAnywhere, Category="Ice|Cracks", meta=(ClampMin=0, ClampMax=1))
	float CrackAmount = 0.9f;

	/** Size of the hard plates further in, their darkest and lightest tone and the color of their seams */
	UPROPERTY(EditAnywhere, Category="Ice|Plates", meta=(ClampMin=1))
	float PlateSize = 78.0f;

	UPROPERTY(EditAnywhere, Category="Ice|Plates")
	FLinearColor PlateColorDark = FLinearColor(0.64f, 0.74f, 0.8f);

	UPROPERTY(EditAnywhere, Category="Ice|Plates")
	FLinearColor PlateColorLight = FLinearColor(0.84f, 0.88f, 0.91f);

	UPROPERTY(EditAnywhere, Category="Ice|Plates")
	FLinearColor PlateSeamColor = FLinearColor(0.24f, 0.36f, 0.45f);

	/** Rivers: how fast the water flows, and how much faster down rapids */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverFlowSpeed = 22.0f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverRapidsSpeed = 70.0f;

	/** Color of a river in its middle and at its banks */
	UPROPERTY(EditAnywhere, Category="Rivers")
	FLinearColor RiverDeepColor = FLinearColor(0.008f, 0.11f, 0.17f);

	UPROPERTY(EditAnywhere, Category="Rivers")
	FLinearColor RiverBankWaterColor = FLinearColor(0.03f, 0.42f, 0.42f);

	/** Opacity of a river in its middle and at its banks */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=1))
	float RiverOpacity = 0.85f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=1))
	float RiverEdgeOpacity = 0.6f;

	/** How much a river foams where it meets the sea, and along its banks */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=1))
	float RiverMouthFoam = 0.85f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=1))
	float RiverBankFoam = 0.55f;
};
