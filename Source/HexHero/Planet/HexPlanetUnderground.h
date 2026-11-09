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
		/** A void of glowing lava, in rare patches near the passages */
		constexpr TCHAR Lava = TEXT('L');
		/** A void holding a dark still lake */
		constexpr TCHAR Lake = TEXT('K');
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

	/** What GenerateBiomes placed besides the floor and the abyss, for the log */
	struct FBiomeReport
	{
		/** The voids that could take lava or a lake */
		int32 Candidates = 0;

		/** Cells, patches joined over neighbors and the middle cell of the largest patch, of lava and of lakes */
		int32 LavaCells = 0;
		int32 LavaPatches = 0;
		int32 LavaCenter = INDEX_NONE;
		int32 LakeCells = 0;
		int32 LakePatches = 0;
		int32 LakeCenter = INDEX_NONE;

		/** Returns a line for the log */
		FString ToString() const;
	};

	/**
	 *  Fills the UndergroundLetter of a biome for every cell: the floor of a cave on passages, the abyss on the void, and on the voids within
	 *  PoolRings of a passage and further than PoolExitRings from every exit (the candidates) lava, the LavaShare of them highest by a noise of
	 *  patches PoolCells across, then dark lakes, LakeShare of the candidates from those left, by another noise. Pure and deterministic
	 */
	void GenerateBiomes(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<FHexEntrance> Entrances, const FHexUndergroundGeneration& Generation,
		int32 Seed, TArray<TCHAR>& OutBiomes, FBiomeReport* OutReport = nullptr);

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

	/** A kind of object the generation of the underground places, one per cover and decor set (UHexPlanetObjects::MakeObjectKinds), read from the
	 *  set so the generation reads no UObject */
	struct FObjectKind
	{
		/** Index of the set among the sets of the layer, of which FHexCellObjects holds SetIndex + 1 */
		int32 SetIndex = INDEX_NONE;

		/** Id of the set, which seeds its noise or its hash */
		FString Id;

		bool bDecor = false;

		/** It keeps heroes out, so it takes only an empty cell whose blocking leaves no passage one cell wide (IsNeck) and the passages around it
		 *  meeting without it, checked again as it takes each, so the generation never cuts a passage */
		bool bBlocks = false;

		/** Levels it may stand on */
		TArray<int8> Levels;

		/** The letters of the biomes of each variant (FindHexObjectVariant) and the share of the candidate cells it takes */
		TArray<TPair<FString, float>> Variants;

		/** Size of its patches in cells, 0 for a hash of the cell */
		float PatchCells = 0.0f;

		/** Kinds of one group share one noise of patches */
		FName Group;

		/** Cells within this many rings of an exit get none of it */
		int32 ClearRings = 0;

		/** A cover is dense (3) in the first DenseShare of the cells it takes, ranked highest first, medium (2) up to MediumShare, sparse (1) after */
		double DenseShare = 0.4;
		double MediumShare = 0.75;
	};

	/** What GenerateObjects placed, by kind: how many cells it got, and the middle cell of its largest patch, INDEX_NONE for none */
	struct FObjectReport
	{
		TArray<int32> Cells;
		TArray<int32> PatchCenters;
	};

	/**
	 *  Gives the passages of an underground their objects, by kind in the order given (SortOrder): a variant takes Share of its candidate cells,
	 *  those of a level the kind allows and a biome that draws the variant, no exit nor within ClearRings of one, no pentagon, and no cover yet for
	 *  a cover or no decor yet for a decor; a kind that blocks takes only empty cells it cannot cut a passage at (FObjectKind::bBlocks), the next
	 *  in rank taking the place of one that the cells taken before made a neck. They are ranked by a noise of patches PatchCells across, shared by the kinds of a Group and offset by
	 *  the seed and the group, or by a hash, and the highest taken; a cover gets the density 3, 2 or 1 by rank (the first DenseShare, up to
	 *  MediumShare, the rest). Pure and deterministic, so any thread may call it and a run of the game and hex.PlanetUnderground write the same file
	 */
	void GenerateObjects(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TConstArrayView<TCHAR> CellBiomeLetters, TConstArrayView<FHexEntrance> Entrances,
		int32 Seed, TConstArrayView<FObjectKind> Kinds, TArray<FHexCellObjects>& OutObjects, FObjectReport* OutReport = nullptr);

	/** Returns true if a disc of a radius around a point of the flattened plane of a cell stays inside every side of ClipSides, a bit per side: side
	 *  K runs from corner K - 1 to corner K of FlatCorners (UHexPlanetObjectSet::bClipFootprint) */
	bool IsFootprintClear(FVector2f Point, float Radius, TConstArrayView<FVector2f> FlatCorners, uint8 ClipSides);

	/** Returns how many rings over neighbors each cell lies from the nearest of Sources, whatever its level, up to MaxRings, and MAX_int32 past
	 *  them or with no source */
	TArray<int32> GetRingDistances(const FHexSphereGrid& Grid, TConstArrayView<int32> Sources, int32 MaxRings);

	/**
	 *  Lists the sides that take a rim (EHexObjectLayer::Rim of UHexPlanetObjectSet) among the sides of Cells, those of a chunk: (cell, side K) where
	 *  neighbor K lies at least MinDrop levels lower, so the passage comes first and its void second, and neither is skipped, as an exit, which
	 *  stays clear, or a cell near one. Each side between a passage and a void appears once over all chunks, with its passage
	 */
	void FindRimSides(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, int32 MinDrop, TFunctionRef<bool(int32)> IsSkipped, TConstArrayView<int32> Cells,
		TArray<FIntPoint>& Out);

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

		/** Generates the biomes again for the levels read, ignoring underground/biomes.txt, and writes it, leaving levels.txt and entrances.json
		 *  as they are */
		bool bNewBiomes = false;
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

		/** What the generation of the biomes placed, when it ran */
		FBiomeReport BiomeReport;

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
