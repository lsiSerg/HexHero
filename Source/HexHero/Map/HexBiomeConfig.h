// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexBiomeConfig.generated.h"

/** What the floor of a void of the underground holds (M_HexUnderground, VOID_STYLE_CODE): the teal grey mist of the abyss, glowing lava under a
 *  dark crust, or a dark still lake; a void stays impassable whatever its style */
UENUM()
enum class EHexVoidStyle : uint8
{
	Mist,
	Lava,
	Lake,
};

/**
 *  One biome: the surface of a hex, while its level gives the shape. A hex is a pair of a level and a biome.
 *  Every biome is its own asset, as binary assets cannot be merged. The game finds them in BiomesFolder of the project settings.
 */
UCLASS(BlueprintType)
class UHexBiomeConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Name shown in debug output */
	UPROPERTY(EditAnywhere, Category="Biome")
	FString DisplayName;

	/** Exact pixel color that means this biome in the biomes file of a map */
	UPROPERTY(EditAnywhere, Category="Biome")
	FColor FileColor = FColor::Black;

	/** Letter of this biome in biomes.txt of a planet, unique among the biomes; a biome without one is not used on planets */
	UPROPERTY(EditAnywhere, Category="Biome")
	FString PlanetLetter;

	/** Letter of this biome in underground/biomes.txt of a planet, unique among the biomes of the underground, which are apart from those of
	 *  the surface; a biome without one is not used underground */
	UPROPERTY(EditAnywhere, Category="Biome")
	FString UndergroundLetter;

	/** Levels that can have this biome */
	UPROPERTY(EditAnywhere, Category="Biome")
	TArray<int32> AllowedLevels;

	/** Levels whose hexes have this biome on a map without a biomes file. Each level needs exactly one such biome */
	UPROPERTY(EditAnywhere, Category="Biome")
	TArray<int32> DefaultForLevels;

	/** Layer of the biome textures that covers the terrain, in the order of LAYERS in build_biome_textures.py */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=255))
	int32 TerrainLayer = 0;

	/** Layer of the biome textures that mountains on this biome are made of, and steep terrain shows */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=255))
	int32 MountainRockLayer = 2;

	/** Layer of the biome textures on the beach of this biome on a planet, on the slope of its coast and on the seabed next to it, or -1 for
	 *  none, so its terrain goes down to the water. It belongs to the terrain layer: the planet takes it from the first biome with that layer
	 *  (AHexPlanet::SetBiomeLayers), so it blends between cells as the layers do, and the rock of a mountain is its own beach */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=-1, ClampMax=15))
	int32 PlanetBeachLayer = 1;

	/** Layer of the biome textures under the water at the coast of this biome on a planet, below SeabedHeight of DA_HexPlanet, or -1 for its
	 *  beach, so snow turns into coastal gravel under the sea. It belongs to the terrain layer, as the beach does */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=-1, ClampMax=255))
	int32 PlanetSeabedLayer = -1;

	/** World height above which mountains on this biome are covered with snow */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=-200, ClampMax=310))
	float SnowLine = 120.0f;

	/** How much the water of this biome hides what is under it on a water level. Deep water hides everything whatever this says */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=0.99))
	float WaterOpacity = 0.5f;

	/** If true, water of this biome is frozen */
	UPROPERTY(EditAnywhere, Category="Surface")
	bool bFrozen = false;

	/** On a void of the underground, what its floor holds: the mist of the abyss, lava or a lake (AHexPlanet::WriteBiomePixels gives it to
	 *  M_HexUnderground); a lava void also lights the passages beside it (AHexPlanet::UpdateCanopy, LavaPool of the config) */
	UPROPERTY(EditAnywhere, Category="Underground")
	EHexVoidStyle VoidStyle = EHexVoidStyle::Mist;

	/** Multiplies the movement cost of the level */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0))
	float MoveCostMultiplier = 1.0f;

	/** If true, ships cannot cross this biome. A biome can only forbid what the level allows */
	UPROPERTY(EditAnywhere, Category="Movement")
	bool bBlocksShips = false;

	/** If true, heroes cannot cross this biome */
	UPROPERTY(EditAnywhere, Category="Movement")
	bool bBlocksHeroes = false;

	/** If true, ordinary units cannot cross this biome */
	UPROPERTY(EditAnywhere, Category="Movement")
	bool bBlocksUnits = false;
};
