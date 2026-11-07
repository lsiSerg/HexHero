// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetFile.generated.h"

class UObject;
class UWorld;

/** How the levels of a planet without levels.txt begin */
UENUM()
enum class EHexPlanetStart : uint8
{
	/** Continents from noise, by the shares of FHexPlanetGeneration */
	Random,
	/** Deep sea everywhere */
	Water,
	/** Plains everywhere */
	Land,
};

/**
 *  How the levels of a planet without levels.txt are generated (HexPlanetGenerator). The shares are of cells, so they hold on a planet of any size,
 *  and the defaults give the planet of the prototype.
 */
USTRUCT()
struct FHexPlanetGeneration
{
	GENERATED_BODY()

	UPROPERTY()
	EHexPlanetStart Start = EHexPlanetStart::Random;

	/** Share of the cells that is land */
	UPROPERTY()
	double LandShare = 0.558233;

	/** Share of the sea that is shallows, along the coast */
	UPROPERTY()
	double ShallowsShare = 0.308411;

	/** Share of the land that is hills, inland */
	UPROPERTY()
	double HillShare = 0.357871;

	/** Share of the land that is mountains, in ranges away from the coast */
	UPROPERTY()
	double MountainShare = 0.101433;

	/** Size of the continents, which divides LevelNoiseScale of DA_HexPlanet */
	UPROPERTY()
	double ContinentSize = 1.0;

	/** Octaves of the noise of levels, each twice as fine and half as strong: more makes the coasts more ragged */
	UPROPERTY()
	int32 CoastDetail = 5;

	/** Added to the temperature of every cell, which is 1 at the equator and 0 at the poles: above 0 shrinks the ice caps, snow and tundra,
	 *  below 0 grows them. The biomes of a planet without biomes.txt are generated from it */
	UPROPERTY()
	double Temperature = 0.0;

	/** Added to the moisture of the land, which is 1 at the coast: below 0 gives more desert and steppe, above 0 more forest and swamp */
	UPROPERTY()
	double Moisture = 0.0;
};

/** The relief of one cell set by hand with the brush, a line of relief.json (HexPlanetSurface::FReliefOverride); -1 leaves a choice to the hash */
USTRUCT()
struct FHexReliefCell
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Cell = 0;

	/** Version of the piece, among those with the same sides */
	UPROPERTY()
	int32 Version = -1;

	/** Turn, among those that give the same set of neighbors */
	UPROPERTY()
	int32 Turn = -1;

	/** 1 mirrored, 0 not */
	UPROPERTY()
	int32 Mirror = -1;

	/** Factor on the height of the cell */
	UPROPERTY()
	float Scale = 1.0f;
};

/** relief.json of a planet: the cells whose relief was set by hand, few, so a list that Git can merge */
USTRUCT()
struct FHexReliefFile
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TArray<FHexReliefCell> Cells;
};

/** The objects of one cell, a line of objects.json: its cover and decor by the Id of their UHexPlanetObjectSet, None for none */
USTRUCT()
struct FHexObjectCell
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Cell = 0;

	UPROPERTY()
	FName Cover;

	/** Density of the cover, 1 to 3 */
	UPROPERTY()
	int32 Density = 0;

	UPROPERTY()
	FName Decor;
};

/** objects.json of a planet: the cells with forests, bushes, stones and decor, a list only of them, which Git can merge */
USTRUCT()
struct FHexObjectsFile
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TArray<FHexObjectCell> Cells;
};

/**
 *  Description of a planet, stored as planet.json in its folder in Content/Data/Planets. The planet takes its levels from levels.txt next to it,
 *  or generates them from Seed when that file is missing.
 */
USTRUCT()
struct FHexPlanetManifest
{
	GENERATED_BODY()

	/** Version of this format */
	UPROPERTY()
	int32 Version = 0;

	/** Name shown in the map menu */
	UPROPERTY()
	FString Name;

	/** Longer description of the planet */
	UPROPERTY()
	FString Description;

	/** Steps each face of the icosahedron is split into, which gives 10 * Frequency^2 + 2 cells; every cell has the same size on any planet */
	UPROPERTY()
	int32 Frequency = 80;

	/** Seed of the generation of the levels, used when the planet has no levels.txt */
	UPROPERTY()
	int32 Seed = 7;

	/** Position in the map menu, lowest first. The first planet opens until another is saved with the view */
	UPROPERTY()
	int32 SortOrder = 0;

	/** How the levels are generated when the planet has no levels.txt */
	UPROPERTY()
	FHexPlanetGeneration Generation;
};

/** One planet that can be opened */
struct FHexPlanetEntry
{
	/** Name of the folder of the planet */
	FString Folder;

	FHexPlanetManifest Manifest;
};

/**
 *  Planets in Content/Data/Planets, read at runtime as the maps in Content/Data/Maps.
 *  levels.txt holds one letter per cell in the order of FHexSphereGrid (D deep sea, S shallows, P plains, H hills, M mountains),
 *  in lines of LevelsLineLength, so an edit changes only the lines of its cells.
 */
namespace HexPlanetFile
{
	/** Current version of planet.json */
	constexpr int32 ManifestVersion = 1;

	/** Folder of the planet folders, relative to the Content folder, which packaging stages as files */
	constexpr const TCHAR* RootFolder = TEXT("Data/Planets");

	constexpr const TCHAR* ManifestFile = TEXT("planet.json");

	constexpr const TCHAR* LevelsFile = TEXT("levels.txt");

	/** Option of the level URL that names the folder of the planet to open */
	constexpr const TCHAR* PlanetOption = TEXT("Planet");

	/** Level that shows the planets */
	constexpr const TCHAR* PlanetLevel = TEXT("/Game/HexPlanet/Lvl_HexPlanet");

	/** Cells per line of levels.txt */
	constexpr int32 LevelsLineLength = 80;

	/** Lowest and highest frequency a planet may have */
	constexpr int32 MinFrequency = 1;
	constexpr int32 MaxFrequency = 200;

	/** Folder of the planets made in a packaged game, whose Content is read only, relative to the Saved folder */
	constexpr const TCHAR* SavedFolder = TEXT("Planets");

	/** Returns the folders that hold planets: the one in Content, then the one in Saved */
	TArray<FString> GetRoots();

	/** Returns the folder new planets are written to: Content in builds with the editor, so they can be committed, else Saved */
	FString GetWritableRoot();

	/** Returns the full path of a file of a planet, in the first root that has its folder, or in the writable root */
	FString GetFilePath(const FString& Folder, const TCHAR* File);

	/** Turns a name into the folder of its planet: letters of any alphabet, digits, - and _, as hex.LoadPlanet takes it as one argument; empty if nothing is left */
	FString SanitizeFolderName(const FString& Name);

	/** Returns true if a planet folder of this name exists in any root */
	bool PlanetExists(const FString& Folder);

	/** Writes planet.json of a folder, in the root that has it or else the writable root. Returns false with an error if it could not */
	bool SaveManifest(const FString& Folder, const FHexPlanetManifest& Manifest, FString& OutError);

	/** Deletes levels.txt of a folder, so the planet generates its levels again. Returns false if it is there and could not be deleted */
	bool DeleteLevels(const FString& Folder);

	/** Option of the level URL that opens the planet made by OpenNewPlanet, which nothing has written yet */
	constexpr const TCHAR* NewPlanetOption = TEXT("NewPlanet");

	/** Opens a planet that is not saved, made by the menu; it is kept in memory until it is saved with the view key, which asks for its name */
	void OpenNewPlanet(const UObject* WorldContext, const FHexPlanetManifest& Manifest);

	/** Returns the planet of OpenNewPlanet if the URL of the world asks for it */
	bool GetNewPlanet(const UWorld* World, FHexPlanetManifest& OutManifest);

	/** Returns every valid planet, lowest SortOrder first */
	TArray<FHexPlanetEntry> FindPlanets();

	/** Reads planet.json from a string. Returns false with an error if it is not valid */
	bool ParseManifest(const FString& Json, FHexPlanetManifest& OutManifest, FString& OutError);

	/** Reads planet.json of a folder */
	bool ReadManifest(const FString& Folder, FHexPlanetManifest& OutManifest, FString& OutError);

	/** Returns the letter of a level in levels.txt, or 0 for a level that has none */
	TCHAR GetLevelLetter(int32 Level);

	/** Writes the levels as the text of levels.txt */
	FString EncodeLevels(TConstArrayView<int8> Levels);

	/** Reads the text of levels.txt, which must hold exactly CellCount letters; whitespace is ignored */
	bool DecodeLevels(const FString& Text, int32 CellCount, TArray<int8>& OutLevels, FString& OutError);

	/** Returns true if the folder has levels.txt */
	bool HasLevels(const FString& Folder);

	/** Reads levels.txt of a folder */
	bool LoadLevels(const FString& Folder, int32 CellCount, TArray<int8>& OutLevels, FString& OutError);

	/** Writes levels.txt of a folder */
	bool SaveLevels(const FString& Folder, TConstArrayView<int8> Levels, FString& OutError);

	/** biomes.txt holds one letter per cell in the order of FHexSphereGrid, the PlanetLetter of its biome, in lines of LevelsLineLength */
	constexpr const TCHAR* BiomesFile = TEXT("biomes.txt");

	/** Writes biomes as the text of biomes.txt, each the letter of its index in Letters */
	FString EncodeBiomes(TConstArrayView<uint8> Biomes, TConstArrayView<TCHAR> Letters);

	/** Reads the text of biomes.txt, which must hold exactly CellCount letters of Letters, any case; whitespace is ignored. A biome is its index in Letters */
	bool DecodeBiomes(const FString& Text, int32 CellCount, TConstArrayView<TCHAR> Letters, TArray<uint8>& OutBiomes, FString& OutError);

	/** Returns true if the folder has biomes.txt */
	bool HasBiomes(const FString& Folder);

	/** Reads biomes.txt of a folder */
	bool LoadBiomes(const FString& Folder, int32 CellCount, TConstArrayView<TCHAR> Letters, TArray<uint8>& OutBiomes, FString& OutError);

	/** Writes biomes.txt of a folder */
	bool SaveBiomes(const FString& Folder, TConstArrayView<uint8> Biomes, TConstArrayView<TCHAR> Letters, FString& OutError);

	/** relief.json holds the cells whose relief was set by hand */
	constexpr const TCHAR* ReliefFile = TEXT("relief.json");

	/** Reads relief.json of a folder; no file is no cells */
	bool LoadRelief(const FString& Folder, TArray<FHexReliefCell>& OutCells, FString& OutError);

	/** objects.json holds the forests, bushes, stones and decor of the cells; a planet without it generates them and writes it, even empty */
	constexpr const TCHAR* ObjectsFile = TEXT("objects.json");

	/** Returns true if the folder has objects.json */
	bool HasObjects(const FString& Folder);

	/** Reads objects.json of a folder */
	bool LoadObjects(const FString& Folder, TArray<FHexObjectCell>& OutCells, FString& OutError);

	/** Writes objects.json of a folder, sorted by cell */
	bool SaveObjects(const FString& Folder, const TArray<FHexObjectCell>& Cells, FString& OutError);

	/** Writes relief.json of a folder, or deletes it when there are no cells */
	bool SaveRelief(const FString& Folder, const TArray<FHexReliefCell>& Cells, FString& OutError);

	/** Deletes biomes.txt of a folder, so the planet generates its biomes again. Returns false if it is there and could not be deleted */
	bool DeleteBiomes(const FString& Folder);

	/** Opens a planet, a folder in Content/Data/Planets, in the level of the planet; an empty folder opens the one FindPlanetToOpen finds */
	void OpenPlanet(const UObject* WorldContext, const FString& Folder);

	/** Returns the folder of the planet asked for in the URL of the world, or the one saved with the last view, or the first one, or an empty string if there is none */
	FString FindPlanetToOpen(const UWorld* World);
}
