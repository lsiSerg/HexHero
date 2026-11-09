// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetLayers.h"
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

/**
 *  How the underground of a planet without underground/levels.txt is generated (HexPlanetUnderground::Generate), the block underground of
 *  planet.json. Entrances are placed only when the planet has no entrances.json. Missing fields keep these defaults
 */
USTRUCT()
struct FHexUndergroundGeneration
{
	GENERATED_BODY()

	/** Entrances placed on the plains and hills of the surface, as far apart as they can be, at most the networks times
	 *  MaxEntrancesPerNetwork; 0 places one per CellsPerEntrance cells of the planet, so a larger planet has more */
	UPROPERTY()
	int32 EntranceCount = 0;

	/** Cells of the planet per entrance when EntranceCount is 0, at least two entrances */
	UPROPERTY()
	int32 CellsPerEntrance = 2000;

	/** Networks the entrances are grouped into, nearby ones together, at most HexPlanetFile::MaxNetworks (A to H); 0 makes networks of about
	 *  the middle of MinEntrancesPerNetwork and MaxEntrancesPerNetwork */
	UPROPERTY()
	int32 NetworkCount = 0;

	/** Fewest and most entrances of a network */
	UPROPERTY()
	int32 MinEntrancesPerNetwork = 2;

	UPROPERTY()
	int32 MaxEntrancesPerNetwork = 4;

	/** Width of a passage in cells, at least 2, so no passage cell has void on two opposite sides */
	UPROPERTY()
	int32 PassageWidth = 2;

	/** Rings of cells of the hall carved around each exit */
	UPROPERTY()
	int32 HallRings = 2;

	/** Share of the cells of the outer ring of a hall that are carved, so a hall is no hexagon */
	UPROPERTY()
	double HallEdgeShare = 0.6;

	/** How much a passage winds: the cost of a cell on its way is 1 + Wiggle times a noise from 0 on the winding lines where it crosses zero
	 *  to 1 away from them, so the ways follow those lines */
	UPROPERTY()
	double Wiggle = 6.0;

	/** Spacing of those lines, in cells */
	UPROPERTY()
	double WiggleCells = 8.0;

	/** How closely the ways keep to those lines: the noise is taken this many times before it is held at 1, so a higher value narrows the
	 *  cheap band along each line */
	UPROPERTY()
	double WiggleSharpness = 2.5;

	/** Void cells kept between two networks, so they never join */
	UPROPERTY()
	int32 NetworkGap = 3;

	/** Cost of a step into a cell within NetworkGap of another network, so a way keeps clear of it unless there is no other; it never comes
	 *  next to one */
	UPROPERTY()
	double GapPenalty = 200.0;

	/** Short branches that end in nothing, added to each network */
	UPROPERTY()
	int32 DeadEnds = 8;

	/** Most cells of such a branch; each takes from half of it up */
	UPROPERTY()
	int32 DeadEndLength = 12;

	/** How much a dead end wanders: at each step the most a random choice adds to the score of a neighbor, against 1 for going straight on */
	UPROPERTY()
	double DeadEndWander = 1.2;

	/** How far the heading of a dead end turns towards each step it takes, so it bends rather than zigzags */
	UPROPERTY()
	double DeadEndTurn = 0.5;

	/** Added to the seed of the planet, so the maze can change while the surface stays */
	UPROPERTY()
	int32 SeedOffset = 0;

	/** Share of the voids that may hold lava or a lake (HexPlanetUnderground::GenerateBiomes), those within PoolRings of a passage, where they can
	 *  be seen, and further than PoolExitRings from every exit, so the marker of an exit stays the brightest thing of its hall, that become lava
	 *  (L), in patches of about PoolCells cells */
	UPROPERTY()
	double LavaShare = 0.01;

	/** Share of the same voids that become dark lakes (K), of those left */
	UPROPERTY()
	double LakeShare = 0.02;

	UPROPERTY()
	int32 PoolRings = 3;

	UPROPERTY()
	double PoolCells = 5.0;

	UPROPERTY()
	int32 PoolExitRings = 2;
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

/** The objects of a cell: its cover and decor as an index into the sets of its layer (UHexPlanetObjects::GetSets) plus one, 0 for none, and the
 *  density of the cover, 1 to 3. Here, beside the lines of objects.json, so the pure generation of the underground (HexPlanetUnderground) uses it */
struct FHexCellObjects
{
	uint8 Cover = 0;
	uint8 Density = 0;
	uint8 Decor = 0;

	bool IsEmpty() const { return Cover == 0 && Decor == 0; }
	bool operator==(const FHexCellObjects& Other) const { return Cover == Other.Cover && Density == Other.Density && Decor == Other.Decor; }
};

/**
 *  Returns the variant of a kind of object that a biome draws, by the letter of the biome in the files of its layer: the first variant whose
 *  letters hold it, else the first with no letters, which serves every biome without a variant of its own, else INDEX_NONE. GetBiomes gives the
 *  letters of variant I. The rule of UHexPlanetObjectSet::FindVariant, shared with the generation of the underground, which reads no UObject
 */
template <typename FGetBiomes>
int32 FindHexObjectVariant(int32 NumVariants, TCHAR BiomeLetter, FGetBiomes&& GetBiomes)
{
	int32 Fallback = INDEX_NONE;
	const TCHAR Letter = FChar::ToUpper(BiomeLetter);
	for (int32 Index = 0; Index < NumVariants; ++Index)
	{
		const FString& Biomes = GetBiomes(Index);
		if (Biomes.IsEmpty())
		{
			Fallback = Fallback == INDEX_NONE ? Index : Fallback;
			continue;
		}
		for (const TCHAR Each : Biomes)
		{
			if (FChar::ToUpper(Each) == Letter)
			{
				return Index;
			}
		}
	}
	return Fallback;
}

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

/** A side of two cells with a river, a line of rivers.json, and whether a bridge crosses it there */
USTRUCT()
struct FHexRiverEdge
{
	GENERATED_BODY()

	/** The lower of the two cells, and its neighbor across the side */
	UPROPERTY()
	int32 Cell = 0;

	UPROPERTY()
	int32 Neighbor = 0;

	/** Named without the b of a bool, so the key in the file is "bridge" */
	UPROPERTY()
	bool Bridge = false;
};

/** rivers.json of a planet: the sides of cells that hold rivers (UHexPlanetRivers), a list only of them, which Git can merge */
USTRUCT()
struct FHexRiversFile
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TArray<FHexRiverEdge> Edges;
};

/** An entrance of the underground as a line of entrances.json: its cell, the same on both layers, and the letter of its network, A to H */
USTRUCT()
struct FHexEntranceLine
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Cell = 0;

	UPROPERTY()
	FString Network;
};

/** entrances.json of a planet, at the root of its folder: its entrances to the underground, a line each, which Git can merge */
USTRUCT()
struct FHexEntrancesFile
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TArray<FHexEntranceLine> Entrances;
};

/**
 *  An entrance of the underground on the surface. Its exit is the same cell underground and is not stored. It is used only on plains or hills,
 *  never on a pentagon, and only when its exit is a passage (HexPlanetUnderground::GetEntranceProblem); one that is not is kept in memory, so
 *  the editor can show it, but left out on save
 */
struct FHexEntrance
{
	int32 Cell = INDEX_NONE;

	/** Network, 0 to HexPlanetFile::MaxNetworks - 1 for A to H, or INDEX_NONE for a letter of the file that is none */
	int32 Network = INDEX_NONE;

	bool operator==(const FHexEntrance& Other) const { return Cell == Other.Cell && Network == Other.Network; }
};

/** A letter of levels.txt and the level it stands for */
struct FHexLevelLetter
{
	TCHAR Letter = 0;
	int8 Level = 0;
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

	/** How the underground is generated when the planet has no underground/levels.txt */
	UPROPERTY()
	FHexUndergroundGeneration Underground;
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
 *  levels.txt holds one letter per cell in the order of FHexSphereGrid (D deep sea, S shallows, P plains, H hills, M mountains on the surface,
 *  V void and P passage underground, GetLevelLetters), in lines of LevelsLineLength, so an edit changes only the lines of its cells.
 *  The files of a layer lie in its data folder (GetDataFolder): the folder of the planet for the surface, its subfolder underground for the
 *  underground. planet.json and entrances.json lie in the folder of the planet, which stays the name of the planet.
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

	/** Returns the full path of a file of a planet, in the first root that has the folder of the planet, or in the writable root. Folder is
	 *  the folder of the planet or a data folder in it (GetDataFolder), which need not exist yet: saving a file makes it */
	FString GetFilePath(const FString& Folder, const TCHAR* File);

	/** Subfolder of a planet with the files of its underground */
	constexpr const TCHAR* UndergroundFolder = TEXT("underground");

	/** Returns the folder of the files of a layer of a planet, to give the functions below in place of the folder of the planet: the folder
	 *  itself for the surface, Folder/underground for the underground; empty for a planet without a folder */
	FString GetDataFolder(const FString& PlanetFolder, EHexPlanetLayer Layer);

	/** Turns a name into the folder of its planet: letters of any alphabet, digits, - and _, as hex.LoadPlanet takes it as one argument; empty if nothing is left */
	FString SanitizeFolderName(const FString& Name);

	/** Returns true if a planet folder of this name exists in any root */
	bool PlanetExists(const FString& Folder);

	/** Writes planet.json of a folder, in the root that has it or else the writable root. Returns false with an error if it could not */
	bool SaveManifest(const FString& Folder, const FHexPlanetManifest& Manifest, FString& OutError);

	/** Sets seedOffset of the underground block in the text of a planet.json and leaves the rest of the text as it is, so Git sees one line
	 *  change: the number when it is there, else a member added in the style of the file (its indents, line ends and the separators after its
	 *  colons), and the block when it is missing. An offset of 0 on a text without one keeps the text, as 0 is the default. Returns false if the
	 *  text is not an object of members it can read */
	bool SetSeedOffsetText(const FString& Json, int32 SeedOffset, FString& OutJson);

	/** Writes the seed offset of the underground of a manifest into planet.json of a folder by SetSeedOffsetText, so a new layout changes only
	 *  that, when the file reads back as the manifest; else, or without a file, the whole manifest (SaveManifest) */
	bool SaveSeedOffset(const FString& Folder, const FHexPlanetManifest& Manifest, FString& OutError);

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

	/** Returns the letters of levels.txt of a layer and their levels: D deep sea (-2), S shallows (-1), P plains (0), H hills (1) and
	 *  M mountains (2) on the surface; V void (-2), the abyss nothing crosses, and P passage (0) underground */
	TConstArrayView<FHexLevelLetter> GetLevelLetters(EHexPlanetLayer Layer);

	/** Returns the letter of a level in levels.txt, or 0 for a level that has none */
	TCHAR GetLevelLetter(int32 Level, TConstArrayView<FHexLevelLetter> Letters = GetLevelLetters(EHexPlanetLayer::Surface));

	/** Writes the levels as the text of levels.txt with the letters of a layer; a level without a letter takes the letter of the nearest one */
	FString EncodeLevels(TConstArrayView<int8> Levels, TConstArrayView<FHexLevelLetter> Letters = GetLevelLetters(EHexPlanetLayer::Surface));

	/** Reads the text of levels.txt, which must hold exactly CellCount letters of a layer, any case; whitespace is ignored */
	bool DecodeLevels(const FString& Text, int32 CellCount, TArray<int8>& OutLevels, FString& OutError,
		TConstArrayView<FHexLevelLetter> Letters = GetLevelLetters(EHexPlanetLayer::Surface));

	/** Returns true if the folder has levels.txt */
	bool HasLevels(const FString& Folder);

	/** Reads levels.txt of a folder, with the letters of its layer */
	bool LoadLevels(const FString& Folder, int32 CellCount, TArray<int8>& OutLevels, FString& OutError,
		TConstArrayView<FHexLevelLetter> Letters = GetLevelLetters(EHexPlanetLayer::Surface));

	/** Writes levels.txt of a folder, with the letters of its layer */
	bool SaveLevels(const FString& Folder, TConstArrayView<int8> Levels, FString& OutError,
		TConstArrayView<FHexLevelLetter> Letters = GetLevelLetters(EHexPlanetLayer::Surface));

	/** entrances.json holds the entrances of the underground, at the root of the folder of the planet */
	constexpr const TCHAR* EntrancesFile = TEXT("entrances.json");

	/** Networks of entrances, A to H */
	constexpr int32 MaxNetworks = 8;

	/** Returns the letter of a network, A to H, or ? for none */
	TCHAR GetNetworkLetter(int32 Network);

	/** Returns the network of a letter, any case, or INDEX_NONE */
	int32 GetNetworkIndex(TCHAR Letter);

	/** Writes entrances as the text of entrances.json, a line each, sorted by cell; one without a network is left out, as the file cannot hold it */
	FString EncodeEntrances(TConstArrayView<FHexEntrance> Entrances);

	/** Reads the text of entrances.json. An entrance whose network is not a letter from A to H is kept with the network INDEX_NONE */
	bool DecodeEntrances(const FString& Text, TArray<FHexEntrance>& OutEntrances, FString& OutError);

	/** Returns true if the folder of a planet has entrances.json */
	bool HasEntrances(const FString& PlanetFolder);

	/** Reads entrances.json of the folder of a planet */
	bool LoadEntrances(const FString& PlanetFolder, TArray<FHexEntrance>& OutEntrances, FString& OutError);

	/** Writes entrances.json of the folder of a planet, even empty; leaving out the entrances that cannot be used is for the caller
	 *  (HexPlanetUnderground::GetSavedEntrances) */
	bool SaveEntrances(const FString& PlanetFolder, TConstArrayView<FHexEntrance> Entrances, FString& OutError);

	/** biomes.txt holds one letter per cell in the order of FHexSphereGrid, the PlanetLetter of its biome on the surface and its
	 *  UndergroundLetter underground, in lines of LevelsLineLength */
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

	/** rivers.json holds the sides of cells with rivers and bridges; a planet without it generates them and writes it, even empty */
	constexpr const TCHAR* RiversFile = TEXT("rivers.json");

	/** Returns true if the folder has rivers.json */
	bool HasRivers(const FString& Folder);

	/** Reads rivers.json of a folder */
	bool LoadRivers(const FString& Folder, TArray<FHexRiverEdge>& OutEdges, FString& OutError);

	/** Writes rivers.json of a folder, a line per side, sorted */
	bool SaveRivers(const FString& Folder, const TArray<FHexRiverEdge>& Edges, FString& OutError);

	/** Writes relief.json of a folder, or deletes it when there are no cells */
	bool SaveRelief(const FString& Folder, const TArray<FHexReliefCell>& Cells, FString& OutError);

	/** Deletes biomes.txt of a folder, so the planet generates its biomes again. Returns false if it is there and could not be deleted */
	bool DeleteBiomes(const FString& Folder);

	/** Opens a planet, a folder in Content/Data/Planets, in the level of the planet; an empty folder opens the one FindPlanetToOpen finds */
	void OpenPlanet(const UObject* WorldContext, const FString& Folder);

	/** Returns the folder of the planet asked for in the URL of the world, or the one saved with the last view, or the first one, or an empty string if there is none */
	FString FindPlanetToOpen(const UWorld* World);
}
