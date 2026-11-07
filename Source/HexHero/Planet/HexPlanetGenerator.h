// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FHexSphereGrid;
class UHexPlanetConfig;
struct FHexPlanetManifest;

/**
 *  Levels of a planet without levels.txt. Random planets take a noise of levels from the seed and split the cells by its value into the shares of
 *  FHexPlanetGeneration, highest first: land, then shallows from the rest; mountains are the land where a second noise nears zero, ranked by it and
 *  by how far inland they lie, and hills the highest of the other land. So the shares hold on any planet, and the same seed gives the same planet.
 *
 *  Biomes of a planet without biomes.txt come from a climate: a temperature from the latitude, colder higher up, and a moisture of the land from
 *  how far it lies from the sea, drier in belts on both sides of the equator, both moved by noise and by Temperature and Moisture of the planet.
 */
namespace HexPlanetGenerator
{
	/** Fills a level for every cell of the grid: -2 deep sea, -1 shallows, 0 plains, 1 hills, 2 mountains; pentagons are never mountains */
	void GenerateLevels(const FHexSphereGrid& Grid, const FHexPlanetManifest& Manifest, const UHexPlanetConfig& Config, TArray<int8>& OutLevels);

	/** Letters of the biomes the generation places, their PlanetLetter */
	namespace Biome
	{
		constexpr TCHAR Water = TEXT('W');
		constexpr TCHAR Ice = TEXT('I');
		constexpr TCHAR Grass = TEXT('G');
		constexpr TCHAR Desert = TEXT('D');
		constexpr TCHAR Rock = TEXT('R');
		constexpr TCHAR Snow = TEXT('N');
		constexpr TCHAR Swamp = TEXT('M');
		constexpr TCHAR Tundra = TEXT('T');
		constexpr TCHAR Steppe = TEXT('P');
	}

	/** Fills the letter of a biome for every cell from its level and climate: ice on the sea near the poles, else water; on land snow, tundra,
	 *  desert, steppe, swamp (only plains) or grass, and mountains of grass or steppe are bare rock where they are dry */
	void GenerateBiomes(const FHexSphereGrid& Grid, const FHexPlanetManifest& Manifest, const UHexPlanetConfig& Config, TConstArrayView<int8> Levels, TArray<TCHAR>& OutBiomes);

	/** Freezes the open sea up to Rings cells across the water from snow land into ice, as fast ice along a cold coast, so a snowy coast never
	 *  meets open water. GenerateBiomes does it with ShoreIceRings, and hex.PlanetShoreIce on a planet that has its biomes. Returns the cells
	 *  it froze */
	TArray<int32> AddShoreIce(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TArray<TCHAR>& Biomes, int32 Rings);
}
