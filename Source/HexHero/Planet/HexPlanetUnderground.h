// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetFile.h"

class FHexSphereGrid;

/**
 *  The underground of a planet: a hex sphere of the same frequency as the surface, so its cell i lies under surface cell i, holding a maze of
 *  passages (level 0, P in underground/levels.txt) in the void (level -2, V), an abyss nothing crosses. An entrance on the plains or hills of the
 *  surface leads down to the same cell below, its exit, the entrances of one network (A to H) are joined by passages, and two networks never are.
 *
 *  Generate is pure and deterministic, so it runs on any thread and the same planet gives the same files. Without entrances it places them on the
 *  plains and hills as far apart as they can be and groups nearby ones into networks; it carves a hall around each exit, joins the exits of each
 *  network, largest first, by a tree whose edges are the cheapest ways over cells that cost 1 + Wiggle times a noise, so they wind, keeping
 *  NetworkGap cells from the networks before, widens every passage so no passage cell has void on two opposite sides (IsNeck), and adds short
 *  dead ends. Check reports how passages and entrances fit together, for the tests and for the editor.
 */
namespace HexPlanetUnderground
{
	/** Level of the void, the abyss between the passages */
	constexpr int8 VoidLevel = -2;

	/** Level of a passage */
	constexpr int8 PassageLevel = 0;

	/** Returns true for the level of a passage, anything but the void */
	inline bool IsPassage(int8 Level) { return Level >= 0; }

	/** UndergroundLetter of the biomes the generation places */
	namespace Biome
	{
		/** Floor of a cave, on passages */
		constexpr TCHAR Floor = TEXT('F');
		/** The abyss, on the void */
		constexpr TCHAR Abyss = TEXT('A');
	}

	/** What keeps an entrance from being used */
	enum class EEntranceProblem : uint8
	{
		None,
		/** Its cell is not one of the planet */
		NoCell,
		/** Its network is not a letter from A to H */
		NoNetwork,
		/** An entrance before it in the list stands on the same cell */
		Duplicate,
		/** The surface there is sea or mountains, not plains or hills */
		NotLand,
		/** It stands on a pentagon, which is kept for a gameplay element of its own */
		Pentagon,
		/** Its exit underground is in the void */
		ExitInVoid,
	};

	/** Returns a few words that say what is wrong */
	const TCHAR* GetProblemText(EEntranceProblem Problem);

	/** Returns true if a cell of the surface can hold an entrance: plains or hills, not a pentagon */
	bool CanHoldEntrance(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, int32 Cell);

	/** Returns what keeps an entrance of a list from being used; UndergroundLevels may be empty, which skips the check of the exit */
	EEntranceProblem GetEntranceProblem(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, TConstArrayView<int8> UndergroundLevels,
		TConstArrayView<FHexEntrance> Entrances, int32 Index);

	/** Returns the entrances a save writes, those without a problem, as invalid ones are kept in memory but left out of entrances.json */
	TArray<FHexEntrance> GetSavedEntrances(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, TConstArrayView<int8> UndergroundLevels,
		TConstArrayView<FHexEntrance> Entrances);

	/** Returns true if a cell is a passage only one cell wide: a hexagon with void on two opposite sides, or a pentagon, which has none, with
	 *  void on two sides that are not next to each other */
	bool IsNeck(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, int32 Cell);

	/** Network of a passage cell in FReport::CellNetworks where networks join */
	constexpr int8 MergedNetwork = -2;

	/** How one network fits together */
	struct FNetworkReport
	{
		/** Entrances of the network that can be used, whatever their exit */
		int32 Entrances = 0;

		/** Separate pieces of passage its exits lie in: 1 when it is connected, 0 when it has no exit on a passage */
		int32 Components = 0;

		/** Passage cells of those pieces */
		int32 Cells = 0;
	};

	/** How the passages of an underground and its entrances fit together */
	struct FReport
	{
		FNetworkReport Networks[HexPlanetFile::MaxNetworks];

		/** Pairs of networks, lower first, that one piece of passage joins */
		TArray<FIntPoint> Merged;

		/** Cells of entrances whose exit is in the void */
		TArray<int32> ExitsInVoid;

		/** Indices of the entrances with another problem, which are not used */
		TArray<int32> BadEntrances;

		/** Passage cells that are only one cell wide (IsNeck) */
		TArray<int32> Necks;

		/** Passage cells, and those of the pieces no exit reaches */
		int32 PassageCells = 0;
		int32 OrphanCells = 0;

		/** Network of each cell: of its piece of passage, INDEX_NONE for the void and pieces no exit reaches, MergedNetwork where networks join */
		TArray<int8> CellNetworks;

		/** Returns how many networks have an entrance that can be used */
		int32 CountNetworks() const;

		/** Returns true if every network is one piece apart from the others, every exit lies on a passage, no entrance has a problem, no passage
		 *  is a neck and no piece is out of reach */
		bool IsGood() const;

		/** Returns a line for the log */
		FString ToString() const;
	};

	/** Checks the passages of an underground against its entrances */
	FReport Check(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, TConstArrayView<int8> Levels, TConstArrayView<FHexEntrance> Entrances);

	/** Returns how many entrances PlaceEntrances places on a planet of so many cells with so many cells that can hold one, and in how many networks */
	void GetEntranceCounts(const FHexUndergroundGeneration& Generation, int32 CellCount, int32 CandidateCount, int32& OutEntrances, int32& OutNetworks);

	/** Places entrances on a surface as far apart as they can be, on plains and hills and never on a pentagon, and groups nearby ones into networks
	 *  of MinEntrancesPerNetwork to MaxEntrancesPerNetwork; sorted by cell */
	TArray<FHexEntrance> PlaceEntrances(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexUndergroundGeneration& Generation, int32 Seed);

	/** Returns the seed of the underground of a planet, from its seed and SeedOffset */
	int32 GetSeed(const FHexPlanetManifest& Manifest);

	/** Fills the UndergroundLetter of a biome for every cell: the floor of a cave on passages, the abyss on the void */
	void GenerateBiomes(TConstArrayView<int8> Levels, TArray<TCHAR>& OutBiomes);

	/** An underground made by Generate */
	struct FResult
	{
		TArray<int8> Levels;

		/** UndergroundLetter of the biome of each cell */
		TArray<TCHAR> Biomes;

		/** The entrances it was made for, given or placed, sorted by cell when placed */
		TArray<FHexEntrance> Entrances;

		/** True if it placed the entrances, as none were given */
		bool bPlacedEntrances = false;

		FReport Report;

		/** Time it took */
		double Seconds = 0.0;
	};

	/** Generates the underground of a surface from Underground of the manifest, for the given entrances, or for entrances it places when there
	 *  are none (null); an entrance with a problem other than its exit is left out */
	void Generate(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexPlanetManifest& Manifest, const TArray<FHexEntrance>* Entrances,
		FResult& OutResult);

	/**
	 *  Generates the passages of one network again in an underground whose other passages stay as they are, as the editor asks: the pieces of
	 *  passage that hold an exit of the network, or touch one in the void, as an exit is always carved, go, with every other network whose exits
	 *  lie in them, as a piece may join several, and the pieces no exit reaches within NetworkGap of them, its leftovers cut off from its exits,
	 *  and those networks are carved again in the void left, by the same costs as Generate, keeping clear of the passages kept. Its Levels are
	 *  those of the whole underground, and its Biomes the letters Generate gives them, of which only the cells whose level changed need to take
	 *  theirs
	 */
	void RegenerateNetwork(const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexPlanetManifest& Manifest,
		TConstArrayView<FHexEntrance> Entrances, TConstArrayView<int8> Levels, int32 Network, FResult& OutResult);

	/** Returns the shortest way over neighbors from one cell to another, both included, the first found by a search outwards from From; empty if
	 *  either is no cell. The brush of the editor that carves passages (Carve) takes it from the press of the button to its release */
	TArray<int32> FindPath(const FHexSphereGrid& Grid, int32 From, int32 To);

	/** Returns the void cells to make passages so a way of cells is a passage Width wide, as Generate widens one: the way, the rings a passage
	 *  wider than 2 grows by, and of each of these cells with void on two opposite sides (IsNeck) the void cell with more passage around it */
	TArray<int32> WidenPath(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<int32> Path, int32 Width);

	/** What Load does besides reading */
	struct FLoadOptions
	{
		/** Writes what it generated in place of a file that was missing: entrances.json, underground/levels.txt and underground/biomes.txt.
		 *  A file that is there but not valid is left alone */
		bool bWriteMissing = true;

		/** Generates the maze again for the entrances, ignoring underground/levels.txt and biomes.txt, and writes them */
		bool bNewMaze = false;

		/** Places the entrances again too, ignoring entrances.json, and writes it */
		bool bNewEntrances = false;
	};

	/** The underground of a planet, read from its files or generated */
	struct FLoaded
	{
		TArray<int8> Levels;

		/** Biome of each cell, an index into the letters given to Load, empty when it was given none */
		TArray<uint8> Biomes;

		/** The entrances, all of entrances.json, those with problems too */
		TArray<FHexEntrance> Entrances;

		/** What was generated in place of a file, and what was written */
		bool bGeneratedLevels = false;
		bool bGeneratedBiomes = false;
		bool bPlacedEntrances = false;
		TArray<FString> Written;

		FReport Report;

		/** Time the generation took, 0 if it did not run */
		double GenerateSeconds = 0.0;
	};

	/**
	 *  Reads the underground of a planet: entrances.json in its folder, levels.txt and biomes.txt in its data folder underground, and generates
	 *  what is missing or not valid, as the surface does, writing it at once by the options so the planet keeps it when the generation changes;
	 *  a planet without a folder, a new one of the menu, generates everything in memory. BiomeLetters are the UndergroundLetters of the biomes
	 *  by index (AHexPlanet::FindBiomes). Logs what it did. Any thread may call it
	 */
	void Load(const FString& PlanetFolder, const FHexSphereGrid& Grid, TConstArrayView<int8> SurfaceLevels, const FHexPlanetManifest& Manifest,
		TConstArrayView<TCHAR> BiomeLetters, const FLoadOptions& Options, FLoaded& Out);
}
