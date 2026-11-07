// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexMapManifest.h"

class UHexBiomeConfig;
class UHexLevelConfig;
struct FHexMapData;

/**
 *  Reads hex maps. A map is a folder with a description and images, where one pixel is one hex and its color selects a level or a biome.
 */
namespace HexMapLoader
{
	/** Largest supported map width and height */
	constexpr int32 MaxMapSize = 256;

	/** Version of map.json that this code reads */
	constexpr int32 ManifestVersion = 1;

	/** Folder of the map folders, relative to the Content folder, which packaging stages as files */
	constexpr const TCHAR* RootFolder = TEXT("Data/Maps");

	/** Description of a map in its folder */
	constexpr const TCHAR* ManifestFile = TEXT("map.json");

	/** Image with the levels in the folder of a map */
	constexpr const TCHAR* LevelsFile = TEXT("levels.png");

	/** Image with the biomes in the folder of a map. A map without it has the default biome of each level */
	constexpr const TCHAR* BiomesFile = TEXT("biomes.png");

	/** Rivers in the folder of a map, which is optional. See ParseRivers */
	constexpr const TCHAR* RiversFile = TEXT("rivers.json");

	/** Level URL option with the folder of the map to load instead of the first one in the map menu */
	constexpr const TCHAR* MapOption = TEXT("Map");

	/** Returns the full path of a file in the folder of a map */
	FString GetMapFilePath(const FString& Folder, const TCHAR* File);

	/** Returns the maps in the root folder in the order of the map menu. A folder with a broken description is logged and left out */
	TArray<FHexMapEntry> FindMaps();

	/** Reads the description of a map from the text of its file. Returns false and sets OutError if it is not valid */
	bool ParseManifest(const FString& Json, FHexMapManifest& OutManifest, FString& OutError);

	/** Loads the map in a folder. Returns false and sets OutError if it is not valid */
	bool LoadMap(const FString& Folder, const TArray<const UHexLevelConfig*>& Levels, const TArray<const UHexBiomeConfig*>& Biomes, FHexMapData& OutMap, FString& OutError);

	/** Converts pixels to levels. Returns false and sets OutError if the size or a pixel color is not valid */
	bool DecodeLevels(TConstArrayView<FColor> Pixels, int32 Width, int32 Height, const TArray<const UHexLevelConfig*>& Levels, FHexMapData& OutMap, FString& OutError);

	/** Converts pixels to the biomes of a map that has its levels. Returns false and sets OutError if the size or a pixel color is not valid or a biome cannot be on its level */
	bool DecodeBiomes(TConstArrayView<FColor> Pixels, int32 Width, int32 Height, const TArray<const UHexBiomeConfig*>& Biomes, FHexMapData& InOutMap, FString& OutError);

	/**
	 *  Reads the rivers of a map that has its levels from the text of rivers.json, and finds the river of each hex:
	 *  {"rivers": [{"name": "North", "hexes": [[6, 5], [6, 6]], "meander": 1, "seed": 3}]}, where a hex is [column, row], the hexes go downstream
	 *  and all but the hexes are optional. Every river has one width, RiverWidth of DA_HexMap, so a width of its own is an error.
	 *  A river ends at the water next to its last hex, or in another river that flows through its last hex. Returns false and sets OutError
	 *  if it is not valid, a hex is off the map or water, a river skips a hex, flows uphill, ends nowhere or flows through a hex of another.
	 */
	bool ParseRivers(const FString& Json, FHexMapData& InOutMap, FString& OutError);

	/** Checks that no river is on a level with ready-made or joined meshes, such as mountains. Returns false and sets OutError if one is */
	bool ValidateRiverLevels(const FHexMapData& Map, const TArray<const UHexLevelConfig*>& Levels, FString& OutError);

	/** Gives every hex of a map that has its levels the default biome of its level. Returns false and sets OutError if a level has none */
	bool SetDefaultBiomes(const TArray<const UHexBiomeConfig*>& Biomes, FHexMapData& InOutMap, FString& OutError);

	/**
	 *  Returns one pixel per hex for the materials, which read it from the world position. The channels must match build_hex_materials.py:
	 *  R is the terrain layer, G is the level plus 128, and B is the mountain rock layer. A is the snow line from -200 to 310 on land.
	 *  Below level 0 it is the water opacity from 0 to 254, and 255 means frozen.
	 */
	TArray<FColor> BuildHexData(const FHexMapData& Map, const TArray<const UHexBiomeConfig*>& Biomes);

	/** Checks that biomes have different file colors, and each level has exactly one default biome that it allows. Returns false and sets OutError if not */
	bool ValidateBiomes(const TArray<const UHexBiomeConfig*>& Biomes, const TArray<const UHexLevelConfig*>& Levels, FString& OutError);
}
