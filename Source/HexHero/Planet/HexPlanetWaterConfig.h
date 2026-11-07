// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetWaterConfig.generated.h"

/**
 *  The look of the water of the planet, M_HexPlanetWater. AHexPlanet gives every float, color and vector of it to that material as parameters of
 *  the same names, once when it is made. Depths and distances come from the cubemap of the sea, in units of the world. A wave is
 *  (frequency, speed, slope, height): how many crests per unit along its direction, how fast it runs, how much it tilts the normal and how high
 *  it lifts the vertices. Tools/create_planet_assets.py creates DA_HexPlanetWater only when it is missing.
 */
UCLASS(BlueprintType)
class UHexPlanetWaterConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Size of one tile of the noise and ripple textures */
	UPROPERTY(EditAnywhere, Category="Scale", meta=(ClampMin=1))
	float TileSize = 260.0f;

	/** Stretches every wave and ripple, as the camera of the planet looks from further than the flat one */
	UPROPERTY(EditAnywhere, Category="Scale", meta=(ClampMin=0.1))
	float WaveScale = 2.0f;

	/** Scales the height the waves lift the vertices */
	UPROPERTY(EditAnywhere, Category="Scale", meta=(ClampMin=0))
	float WaveHeight = 1.0f;

	/** Scale of the wide noise that bends the swell and drifts the tone of the open sea, per tile */
	UPROPERTY(EditAnywhere, Category="Scale", meta=(ClampMin=0.001))
	float WideNoiseScale = 0.11f;

	/** Scale of the finer noise that bends the swell */
	UPROPERTY(EditAnywhere, Category="Scale", meta=(ClampMin=0.001))
	float MiddleNoiseScale = 0.37f;

	/** How far the noise bends the swell, in units of the world before WaveScale. Much more than a wavelength crumples the waves into blotches */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float SwellBend = 150.0f;

	/**
	 * Mip of the noise that bends the swell, the same from any camera, as the bend moves the phase of the waves so far that the smallest change
	 * of a mip chosen by the size of a pixel makes them run as the camera moves. Higher bends them more smoothly
	 */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0, ClampMax=10))
	float SwellBendMip = 5.0f;

	/** The three long swells */
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

	/** Size of a pixel on the water, in units of the world, up to which the short waves and ripples are fully seen */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float CloseUpStart = 1.0f;

	/** Size of a pixel past which they are gone */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float CloseUpEnd = 3.0f;

	/** Distance from the camera up to which the short waves lift the vertices */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float ChopLiftStart = 1200.0f;

	/** Distance past which they no longer do */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float ChopLiftEnd = 2600.0f;

	/**
	 * Spacing of the vertices of the water cap per wavelength, from which a wave lifts them less. The cap turns with the camera, so its vertices
	 * slide over the waves, and the surface between them is flat, so a crest between two vertices is lower than on one: with few vertices per wave
	 * its height pulses as the camera moves, faster the faster it moves (about 1 - cos(180 * this) of the wave). It stays in the normal of the pixels
	 */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float VertexWaveFadeStart = 0.08f;

	/** Spacing per wavelength past which a wave no longer lifts the vertices */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float VertexWaveFadeEnd = 0.16f;

	/** How much everything seen on the water sways with the swell */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0))
	float WaterSway = 0.05f;

	/** Scales of the two layers of ripples, per tile */
	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0.01))
	float RippleScaleA = 2.3f;

	UPROPERTY(EditAnywhere, Category="Waves", meta=(ClampMin=0.01))
	float RippleScaleB = 1.1f;

	/** How much the normal tilts: by the swell far away and close by, by ripples far away and close by */
	UPROPERTY(EditAnywhere, Category="Waves")
	FVector4 NormalStrength = FVector4(0.14, 0.32, 0.12, 0.22);

	/** Size of a pixel from which the water calms, as dense waves only shimmer far away; it is fully calm at three times this */
	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0.01))
	float FarCalmStart = 2.0f;

	/** What full far calm keeps: of the tilt by the swell, of the tilt by ripples and of the lighter side of waves */
	UPROPERTY(EditAnywhere, Category="Calm")
	FVector FarCalm = FVector(0.5, 0.25, 0.35);

	/** Depth of the seabed below which the water is calm over shallows */
	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0))
	float CalmDepthStart = 26.0f;

	/** Depth from which it is the open sea */
	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0))
	float CalmDepthEnd = 45.0f;

	/** What calm shallows keep: of the slope of waves, of ripples, of glints and of the height of waves */
	UPROPERTY(EditAnywhere, Category="Calm")
	FVector4 ShallowCalm = FVector4(0.45, 0.6, 0.45, 0.4);

	/** Share of the height of waves left next to land, growing over LagoonDistanceStart to LagoonDistanceEnd */
	UPROPERTY(EditAnywhere, Category="Calm", meta=(ClampMin=0, ClampMax=1))
	float NearLandHeight = 0.3f;

	/** Opacity of the clearest water */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0, ClampMax=1))
	float Clearness = 0.65f;

	/** Depth of the seabed up to which the water is clear */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float WaterClearDepth = 3.0f;

	/** Depth from which it is opaque */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float WaterOpaqueDepth = 60.0f;

	/** How unevenly by place the depth counts, in units of the world */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float SeabedDepthNoise = 12.0f;

	/** Scale of the noise of that unevenness, per tile */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0.001))
	float FadeNoiseScale = 0.09f;

	/** Clear water next to land, and the turquoise further out */
	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor LagoonColor = FLinearColor(0.03f, 0.56f, 0.52f);

	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor ShallowColor = FLinearColor(0.01f, 0.34f, 0.42f);

	/** Distance to land where the lagoon turns turquoise */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float LagoonDistanceStart = 60.0f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float LagoonDistanceEnd = 220.0f;

	/** Deep water near land */
	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor OffshoreColor = FLinearColor(0.012f, 0.15f, 0.24f);

	/** Distance to land where it turns into the open sea */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float OffshoreDistanceStart = 90.0f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float OffshoreDistanceEnd = 400.0f;

	/** The open sea, whose tone drifts between these in wide patches */
	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor DeepColorDark = FLinearColor(0.004f, 0.03f, 0.085f);

	UPROPERTY(EditAnywhere, Category="Color")
	FLinearColor DeepColorLight = FLinearColor(0.008f, 0.085f, 0.17f);

	/** Contrast of the wide and the finer patches of the open sea */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float PatchContrast = 1.8f;

	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0))
	float PatchFineContrast = 1.2f;

	/** Share of the finer patches in the tone */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0, ClampMax=1))
	float PatchFineWeight = 0.25f;

	/**
	 * Mip of the noise of the patches and of the unevenness of the depth, the same from any camera. The noise texture holds fine detail too,
	 * which at a low mip speckles the tone; higher keeps only the wide patches
	 */
	UPROPERTY(EditAnywhere, Category="Color", meta=(ClampMin=0, ClampMax=10))
	float PatchMip = 6.0f;

	/** How much lighter the side of a wave towards the sun is, and the other side darker, which makes the open sea mottled when strong */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float WaveLight = 0.5f;

	/** Color of the sky that the water reflects where it faces away from the camera */
	UPROPERTY(EditAnywhere, Category="Light")
	FLinearColor SkyColor = FLinearColor(0.3f, 0.46f, 0.62f);

	/** How much of the sky it reflects at a grazing view */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0, ClampMax=1))
	float SkyReflection = 0.4f;

	/** Power of the fresnel term: higher keeps the sky closer to the horizon */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0.1))
	float FresnelPower = 4.0f;

	/** Brightness of the water on the night side */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0, ClampMax=1))
	float NightBrightness = 0.06f;

	/** Height of the sun, as the sine of its angle, where the water starts to brighten from night */
	UPROPERTY(EditAnywhere, Category="Light")
	float NightStart = -0.12f;

	/** Height of the sun where it is fully bright */
	UPROPERTY(EditAnywhere, Category="Light")
	float NightEnd = 0.3f;

	/** Strength of the glints of the sun */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0))
	float GlintAmount = 1.0f;

	/** Color of the glints */
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

	/** Glints left off the path of the sun */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0, ClampMax=1))
	float SunPathMin = 0.2f;

	/** Cosine between the reflected view and the sun where the path of the sun starts and where it is full */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=-1, ClampMax=1))
	float SunPathStart = 0.25f;

	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=-1, ClampMax=1))
	float SunPathEnd = 0.85f;

	/** Size of a pixel where the glints start to fade and where they are gone, as one-pixel sparkles only look like noise */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0))
	float GlintFarStart = 0.9f;

	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0))
	float GlintFarEnd = 2.0f;

	/** Height of the sun, as the sine of its angle, from which the glints are full */
	UPROPERTY(EditAnywhere, Category="Glints", meta=(ClampMin=0.001, ClampMax=1))
	float GlintSunHeight = 0.15f;

	/** The open water between the floes of the frozen sea (blue of the sea field): its color, darker and colder than the open sea, and the share
	 *  of the waves it keeps, so the floes float on calmer water */
	UPROPERTY(EditAnywhere, Category="Ice")
	FLinearColor IceWaterColor = FLinearColor(0.012f, 0.03f, 0.07f);

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceWaterShare = 0.75f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceWaveShare = 0.35f;

	/** Slush and brash ice, small bits floating between the floes: their color, how much of the water they cover and the scale of their noise */
	UPROPERTY(EditAnywhere, Category="Ice")
	FLinearColor IceSlushColor = FLinearColor(0.62f, 0.72f, 0.8f);

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceSlush = 0.25f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0))
	float IceSlushScale = 1.6f;

	/** Foam over the ledge of a floe, where the water over it is shallower than IceFoamWidth, laced by the slush */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceFoam = 0.7f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0.01))
	float IceFoamWidth = 2.0f;

	/** Opacity of the water of the frozen sea over what lies just under it, the ledges of floes, which grows to 1 at IceClearDepth behind it */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceUnderClearness = 0.3f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0.01))
	float IceClearDepth = 8.0f;

	/** From far away, where the floes are no longer drawn (FloeCullDistance of DA_HexPlanet), the frozen sea takes this color at IceFarCover,
	 *  blended in by the distance from the camera from IceFarStart to IceFarEnd, which should end at FloeCullDistance */
	UPROPERTY(EditAnywhere, Category="Ice")
	FLinearColor IceFarColor = FLinearColor(0.55f, 0.66f, 0.75f);

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceFarCover = 0.55f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0))
	float IceFarStart = 6000.0f;

	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0))
	float IceFarEnd = 9000.0f;

	/** Half the width, in the blend of the sea field between cells, over which the edge of the ice fades into the water */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0.001, ClampMax=0.5))
	float IceEdgeSoftness = 0.06f;

	/** Half the width, in the same blend, over which the dark water of the ice fades into the open sea, wider than the edge of the floes, so the
	 *  color changes over about a cell and not on a line */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0.001, ClampMax=0.5))
	float IceWaterFade = 0.35f;

	/** How far a noise moves the edge of the ice, in the same blend, so it does not follow the cells */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceEdgeNoise = 0.6f;

	/** Share of their color the slush and the far ice keep where the sun is low and on the night side, as the floes are lit by the sun while the water is unlit */
	UPROPERTY(EditAnywhere, Category="Ice", meta=(ClampMin=0, ClampMax=1))
	float IceShade = 0.35f;

	/** The shore, found by how deep the water is over the ground behind it (scene depth along the view, turned into the depth along the normal
	 *  of the planet), so it follows the real coast whatever the cells. The color of its foam, lit by the sun as the water is not */
	UPROPERTY(EditAnywhere, Category="Shore")
	FLinearColor ShoreFoamColor = FLinearColor(0.82f, 0.88f, 0.9f);

	/** A soft film of foam where the water touches the ground, and the depth of water over which it fades out */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float ShoreFoam = 0.75f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.01))
	float ShoreFoamDepth = 1.5f;

	/** The film is never narrower than this many times the change of the depth from one pixel to the next, so from far away it stays a line
	 *  instead of breaking into dots */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float ShoreFoamPixels = 1.5f;

	/** The foam is laced by streaks of this scale, which keep ShoreLace of it */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float ShoreLaceScale = 2.5f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float ShoreLace = 0.45f;

	/** The size of a pixel (as aa of WATER_CODE, in world units) from which the waves fade out, to none at ShoreFarEnd, while the film keeps
	 *  ShoreFarFilm of itself, as from far away waves only shimmer */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float ShoreFarStart = 1.5f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float ShoreFarEnd = 4.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float ShoreFarFilm = 0.5f;

	/** Breakers: the coast is cut into stretches, one land cell each unless ShoreStretchJoin of DA_HexPlanet joins two, each breaking at its own
	 *  phase (ShoreField of the sea field), so the waves come one by one along the coast. A stretch sends a wave every SurfPeriod seconds with
	 *  SurfChance: a thin white arc that forms SurfReach from land (the distance to the coast in alpha of the sea field, a cell step about 173)
	 *  and rolls in at an even speed over SurfRunShare of the period, from SurfWidthStart to SurfWidthEnd wide in world units, growing from nothing
	 *  to Surf of white, to SurfShoreDistance, two fifths of a cell, from the coast line of the field; over that its speed changes to SurfShoreSpeed
	 *  of what it was by the line, above 1 faster, as the white water of a broken wave rushes in, below 1 slower, and at full strength it rolls on
	 *  at that pace to the real edge of the water, wherever that lies against the line, where just then its water runs up the beach (Runup*), and
	 *  on under the land, whose ground hides it. The distance is divided by how fast it grows (alpha of the shore field), so a wave rolls at an
	 *  even speed instead of rushing in and swelling where the soft minimum grows slowly. It is at most SurfLength long, and fades
	 *  out from SurfBorderEnd to SurfBorderStart world units from the end of its stretch or of that length, where it starts SurfCurl further out
	 *  and catches up by the shore zone, so it bends into a C towards land. A noise moves the distance by up to SurfWobble */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float Surf = 1.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float SurfChance = 0.6f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float SurfReach = 260.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=1))
	float SurfShoreDistance = 70.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.05, ClampMax=4))
	float SurfShoreSpeed = 1.4f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.1))
	float SurfPeriod = 22.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.1, ClampMax=0.9))
	float SurfRunShare = 0.5f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.1))
	float SurfWidthStart = 2.5f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.1))
	float SurfWidthEnd = 4.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=1))
	float SurfLength = 170.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float SurfBorderStart = 15.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float SurfBorderEnd = 45.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float SurfCurl = 20.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float SurfWobble = 30.0f;

	/** The breakers fade out where the lines of the distance to the coast wrap around a cape, by how much they bend there (green of the sea
	 *  field: 1 around a lone cape, about 0 along a straight coast), from SurfCapeStart to SurfCapeEnd, as their arcs would cross the run of the coast */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float SurfCapeStart = 0.5f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float SurfCapeEnd = 0.85f;

	/** Now and then a wave is a roller, by a hash of the wave and its stretch: RollerChance of them, with foam of RollerFoam, running RollerScale
	 *  higher up the beach */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float RollerChance = 0.1f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=1))
	float RollerScale = 1.8f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float RollerFoam = 0.95f;

	/** The run-up on land (M_HexPlanet): when the crest of a breaker reaches the edge of the water below a point of the beach (from the distance
	 *  to the coast there and the slope of the ground), the water runs up there to RunupHeight above the sea, fast, over RunupRiseTime seconds,
	 *  then slowly back over RunupFallTime seconds, a rim of foam RunupEdge high at its edge, and the foam it left slides down after it and fades.
	 *  RunupFoam is how white it is, and the sand it wet, once the water has slid back off it, keeps WetDarkening of its color until it dries */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0))
	float RunupHeight = 3.0f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.05))
	float RunupRiseTime = 0.6f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.1))
	float RunupFallTime = 4.5f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.01))
	float RunupEdge = 0.2f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float RunupFoam = 0.85f;

	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0, ClampMax=1))
	float WetDarkening = 0.85f;

	/** As the run-up slides back down, its foam slides back into the water over the edge, a laced sheet out to BackwashDepth of water that thins
	 *  out and fades */
	UPROPERTY(EditAnywhere, Category="Shore", meta=(ClampMin=0.01))
	float BackwashDepth = 1.5f;
};
