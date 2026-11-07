// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexMapSurfaceConfig.generated.h"

/**
 *  The look of the land of the flat map: M_HexTerrain, M_HexMountain and M_HexCliff, and the grid they draw. AHexWorldMap gives every value of it to
 *  all its materials as parameters of the same names (HexMaterialConfig::Apply), and Tools/build_hex_materials.py adds a parameter for each of
 *  them that the code of a material names. Heights are in units of the world. Created by that script only when missing.
 */
UCLASS(BlueprintType)
class UHexMapSurfaceConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Size of one tile of the texture layers */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=1))
	float TileSize = 260.0f;

	/** Strength of the normals of the texture layers */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float DetailStrength = 0.8f;

	/** How much larger than a tile the slow changes of tone of a layer are, and how far they darken and lighten it */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=1))
	float MacroToneScale = 7.3f;

	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float MacroTone = 0.2f;

	/** How sharply the layers of neighboring hexes blend: by the weight of each hex to this power, and by the height in the alpha of a layer times this */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0.1))
	float SurfaceBlendPower = 3.0f;

	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float SurfaceHeightBlend = 3.0f;

	/** Heights where the sand of the seabed gives way to the layer of the land */
	UPROPERTY(EditAnywhere, Category="Surface")
	float SeabedSandStart = 2.0f;

	UPROPERTY(EditAnywhere, Category="Surface")
	float SeabedSandEnd = 4.5f;

	/** Width of the grid lines, in units of the world */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0))
	float GridWidth = 0.9f;

	/** Color of the grid lines and how strongly they show */
	UPROPERTY(EditAnywhere, Category="Grid")
	FLinearColor GridColor = FLinearColor(0.02f, 0.02f, 0.02f);

	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0, ClampMax=1))
	float GridStrength = 0.5f;

	/** Up component of the normal below which the terrain is fully the rock of its hex, and above which it is fully its layer */
	UPROPERTY(EditAnywhere, Category="Terrain", meta=(ClampMin=0, ClampMax=1))
	float SteepRockStart = 0.5f;

	UPROPERTY(EditAnywhere, Category="Terrain", meta=(ClampMin=0, ClampMax=1))
	float SteepRockEnd = 0.8f;

	/** Height around the water over which the bottom turns to soft color */
	UPROPERTY(EditAnywhere, Category="Terrain", meta=(ClampMin=0.01))
	float UnderwaterBlend = 2.0f;

	/** Scale and blur of the soft color of the seabed */
	UPROPERTY(EditAnywhere, Category="Terrain", meta=(ClampMin=0.01))
	float SeabedBlurScale = 0.35f;

	UPROPERTY(EditAnywhere, Category="Terrain", meta=(ClampMin=0))
	float SeabedBlurMip = 5.0f;

	/** How far up the beach a wave runs on average, how much that swings and how fast */
	UPROPERTY(EditAnywhere, Category="Beach", meta=(ClampMin=0))
	float BeachWaveReach = 2.2f;

	UPROPERTY(EditAnywhere, Category="Beach", meta=(ClampMin=0))
	float BeachWaveSwing = 1.8f;

	UPROPERTY(EditAnywhere, Category="Beach", meta=(ClampMin=0))
	float BeachWaveSpeed = 0.9f;

	/** How far above the water the sand is wet, and how dark that makes it */
	UPROPERTY(EditAnywhere, Category="Beach", meta=(ClampMin=0))
	float BeachWetReach = 5.5f;

	UPROPERTY(EditAnywhere, Category="Beach", meta=(ClampMin=0, ClampMax=1))
	float BeachWetDarkness = 0.62f;

	/** Seconds between the times the foam on the beach moves to new places, its color and how much it shows */
	UPROPERTY(EditAnywhere, Category="Beach", meta=(ClampMin=0.1))
	float BeachFoamPeriod = 9.0f;

	UPROPERTY(EditAnywhere, Category="Beach")
	FLinearColor BeachFoamColor = FLinearColor(0.74f, 0.8f, 0.8f);

	UPROPERTY(EditAnywhere, Category="Beach", meta=(ClampMin=0, ClampMax=1))
	float BeachFoamAmount = 0.6f;

	/** Scale and tint of the sand of a river bed, and how much darker its wet floor is */
	UPROPERTY(EditAnywhere, Category="River Banks", meta=(ClampMin=0.01))
	float RiverSandScale = 1.7f;

	UPROPERTY(EditAnywhere, Category="River Banks")
	FLinearColor RiverSandTint = FLinearColor(0.9f, 0.82f, 0.7f);

	UPROPERTY(EditAnywhere, Category="River Banks", meta=(ClampMin=0, ClampMax=1))
	float RiverFloorDarkness = 0.72f;

	/** Mean width of the sand on the land past the top of a bank, the wavelength of the noise that winds its edge, and how much lighter it gets there */
	UPROPERTY(EditAnywhere, Category="River Banks", meta=(ClampMin=0))
	float RiverBankRim = 2.5f;

	UPROPERTY(EditAnywhere, Category="River Banks", meta=(ClampMin=1))
	float RiverBankRimScale = 45.0f;

	UPROPERTY(EditAnywhere, Category="River Banks", meta=(ClampMin=0))
	float RiverBankRimLight = 0.08f;

	/** Shade of the rock of a mountain from the red of its mask atlas: the darkest and how much lighter the brightest is */
	UPROPERTY(EditAnywhere, Category="Mountains", meta=(ClampMin=0))
	float MountainShadeMin = 0.55f;

	UPROPERTY(EditAnywhere, Category="Mountains", meta=(ClampMin=0))
	float MountainShadeRange = 0.9f;

	/** How much the snow line of a biome contrasts on mountains: how far the green of the atlas moves it, how far it softens */
	UPROPERTY(EditAnywhere, Category="Mountains|Snow", meta=(ClampMin=0))
	float SnowLineWander = 30.0f;

	UPROPERTY(EditAnywhere, Category="Mountains|Snow", meta=(ClampMin=0.01))
	float SnowLineSoftness = 8.0f;

	/** How much higher flat ground holds snow than steep, per unit of its up normal */
	UPROPERTY(EditAnywhere, Category="Mountains|Snow")
	float SnowSlopeLift = 25.0f;

	/** Up component of the normal where snow starts to hold and where it holds fully */
	UPROPERTY(EditAnywhere, Category="Mountains|Snow", meta=(ClampMin=0, ClampMax=1))
	float SnowFlatStart = 0.45f;

	UPROPERTY(EditAnywhere, Category="Mountains|Snow", meta=(ClampMin=0, ClampMax=1))
	float SnowFlatEnd = 0.7f;

	/** Share of the rise of a massif that snow counts, and how far its line drops per hex inside a massif */
	UPROPERTY(EditAnywhere, Category="Mountains|Snow", meta=(ClampMin=0, ClampMax=1))
	float SnowMassifShare = 0.3f;

	UPROPERTY(EditAnywhere, Category="Mountains|Snow")
	float SnowMassifDrop = 40.0f;

	/** Tint of snow, a little blue in its hollows, and how much its drifts contrast */
	UPROPERTY(EditAnywhere, Category="Mountains|Snow")
	FLinearColor SnowTint = FLinearColor(0.94f, 0.97f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Mountains|Snow", meta=(ClampMin=0))
	float SnowContrast = 1.0f;

	/** How high moss climbs above the terrain, at most, the up normal where it starts and is full, and how much it covers */
	UPROPERTY(EditAnywhere, Category="Mountains|Foot", meta=(ClampMin=0))
	float MossHeight = 18.0f;

	UPROPERTY(EditAnywhere, Category="Mountains|Foot", meta=(ClampMin=0, ClampMax=1))
	float MossFlatStart = 0.55f;

	UPROPERTY(EditAnywhere, Category="Mountains|Foot", meta=(ClampMin=0, ClampMax=1))
	float MossFlatEnd = 0.85f;

	UPROPERTY(EditAnywhere, Category="Mountains|Foot", meta=(ClampMin=0, ClampMax=1))
	float MossAmount = 0.85f;

	/** How much of the grid shows on the rock of a mountain, as it would cut the mountain into pieces */
	UPROPERTY(EditAnywhere, Category="Mountains", meta=(ClampMin=0, ClampMax=1))
	float MountainGridHint = 0.2f;

	/** Depth below the water over which the rock of a mountain fades into the soft color of the seabed */
	UPROPERTY(EditAnywhere, Category="Mountains", meta=(ClampMin=0.01))
	float MountainUnderwaterBlend = 8.0f;

	/** Spray on rock above the water: its color, its height above the water, how much noise and a surge add to it */
	UPROPERTY(EditAnywhere, Category="Spray")
	FLinearColor SprayColor = FLinearColor(0.74f, 0.8f, 0.82f);

	UPROPERTY(EditAnywhere, Category="Spray", meta=(ClampMin=0))
	float SprayHeight = 2.0f;

	UPROPERTY(EditAnywhere, Category="Spray", meta=(ClampMin=0))
	float SprayNoiseHeight = 3.0f;

	UPROPERTY(EditAnywhere, Category="Spray", meta=(ClampMin=0))
	float SurgeSprayHeight = 26.0f;

	/** Bands of rock on cliffs: how far noise bends them, how close they lie and how much they contrast */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	float CliffStrataWobble = 22.0f;

	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	float CliffStrataFrequency = 0.42f;

	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	float CliffStrataContrast = 0.24f;

	/** How far above the water the wet line on cliffs reaches, and how dark it is */
	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0))
	float CliffWetLine = 4.0f;

	UPROPERTY(EditAnywhere, Category="Cliffs", meta=(ClampMin=0, ClampMax=1))
	float CliffWetDarkness = 0.7f;

	/** Roughness of the land, mountains and cliffs */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float SurfaceRoughness = 0.9f;
};
