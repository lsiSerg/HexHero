// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HexMapSettings.generated.h"

class UHexCameraConfig;
class UHexInputConfig;
class UHexKeyLegendConfig;
class UHexBiomeConfig;
class UHexLevelConfig;
class UHexMapConfig;
class UHexTerrainLayersConfig;
class UHexPlanetObjectSet;

/**
 *  Project settings of the hex world map. Stored in Config/DefaultGame.ini.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Hex Map"))
class UHexMapSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	/** Folder with one asset per terrain level. It must be listed in the directories to always cook, as nothing refers to the assets */
	UPROPERTY(Config, EditAnywhere, Category="Map", meta=(ContentDir))
	FDirectoryPath LevelsFolder;

	/** Folder with one asset per biome. It must be listed in the directories to always cook, as nothing refers to the assets */
	UPROPERTY(Config, EditAnywhere, Category="Map", meta=(ContentDir))
	FDirectoryPath BiomesFolder;

	/** Asset with the mesh sizes and materials */
	UPROPERTY(Config, EditAnywhere, Category="Map")
	TSoftObjectPtr<UHexMapConfig> MapConfig;

	/** Asset with the camera turn and scroll values */
	UPROPERTY(Config, EditAnywhere, Category="Map")
	TSoftObjectPtr<UHexCameraConfig> CameraConfig;

	/** Asset with the input actions and their mapping context */
	UPROPERTY(Config, EditAnywhere, Category="Map")
	TSoftObjectPtr<UHexInputConfig> InputConfig;

	/** Asset with the lines of the legend of the keys */
	UPROPERTY(Config, EditAnywhere, Category="Map")
	TSoftObjectPtr<UHexKeyLegendConfig> KeyLegend;

	/** Asset with the texture layers of the terrain, shared by the flat map and the planet */
	UPROPERTY(Config, EditAnywhere, Category="Map")
	TSoftObjectPtr<UHexTerrainLayersConfig> TerrainLayers;

	/** Folder with one asset per kind of object of the planet (UHexPlanetObjectSet). It must be listed in the directories to always cook, as nothing refers to the assets */
	UPROPERTY(Config, EditAnywhere, Category="Planet", meta=(ContentDir))
	FDirectoryPath PlanetObjectsFolder;

	/** Loads the terrain levels, lowest first */
	TArray<const UHexLevelConfig*> LoadLevels() const;

	/** Loads the biomes, ordered by asset name */
	TArray<const UHexBiomeConfig*> LoadBiomes() const;

	/** Loads the map config. Without a valid asset it logs an error and returns the default values */
	const UHexMapConfig* LoadMapConfig() const;

	/** Loads the camera config. Without a valid asset it logs an error and returns the default values */
	const UHexCameraConfig* LoadCameraConfig() const;

	/** Loads the input config. Without a valid asset it logs an error and returns the default values, which have no actions */
	const UHexInputConfig* LoadInputConfig() const;

	/** Loads the legend of the keys. Without a valid asset it logs an error and returns the default values, which have no lines */
	const UHexKeyLegendConfig* LoadKeyLegend() const;

	/** Loads the texture layers. Without a valid asset it logs an error and returns the default values, which have no layers */
	const UHexTerrainLayersConfig* LoadTerrainLayers() const;

	/** Loads the kinds of objects of the planet, ordered by asset name */
	TArray<const UHexPlanetObjectSet*> LoadPlanetObjectSets() const;
};
