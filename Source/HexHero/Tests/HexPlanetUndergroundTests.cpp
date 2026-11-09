// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHero.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "HexPlanetConfig.h"
#include "HexPlanetFile.h"
#include "HexPlanetGenerator.h"
#include "HexPlanetObjects.h"
#include "HexPlanetObjectSet.h"
#include "HexPlanetUnderground.h"
#include "HexSphereGrid.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A surface and the underground generated for it */
	struct FUndergroundCase
	{
		FString Name;
		FHexSphereGrid Grid;
		FHexPlanetManifest Manifest;
		TArray<int8> Surface;
		HexPlanetUnderground::FResult Result;
	};

	/** Returns a surface of the generation of the prototype, with the land of a share */
	FUndergroundCase MakeSurface(int32 Frequency, int32 Seed, double LandShare)
	{
		FUndergroundCase Case;
		Case.Name = FString::Printf(TEXT("frequency %d, seed %d, land %.2f"), Frequency, Seed, LandShare);
		Case.Grid = FHexSphereGrid::Build(Frequency);
		Case.Manifest.Frequency = Frequency;
		Case.Manifest.Seed = Seed;
		Case.Manifest.Generation.LandShare = LandShare;
		HexPlanetGenerator::GenerateLevels(Case.Grid, Case.Manifest, *GetDefault<UHexPlanetConfig>(), Case.Surface);
		return Case;
	}

	/** Returns planets of several sizes and seeds with their undergrounds generated, each placing its entrances: by the defaults of the manifest,
	 *  which place them by the size of the planet, and crowded with more entrances and networks than those */
	TArray<FUndergroundCase> MakeCases()
	{
		TArray<FUndergroundCase> Cases;
		Cases.Add(MakeSurface(20, 1, 0.56));
		Cases.Add(MakeSurface(20, 2, 0.4));
		Cases.Add(MakeSurface(32, 3, 0.35));
		Cases.Add(MakeSurface(48, 4, 0.56));
		Cases.Add(MakeSurface(32, 5, 0.5));
		Cases.Last().Manifest.Underground.EntranceCount = 12;
		Cases.Last().Manifest.Underground.NetworkCount = 4;
		Cases.Last().Name += TEXT(", 12 entrances in 4 networks");
		Cases.Add(MakeSurface(40, 6, 0.56));
		Cases.Last().Manifest.Underground.EntranceCount = 30;
		Cases.Last().Manifest.Underground.NetworkCount = 8;
		Cases.Last().Name += TEXT(", 30 entrances in 8 networks");
		for (FUndergroundCase& Case : Cases)
		{
			HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Case.Result);
		}
		return Cases;
	}

	/** Gives how many entrances and networks the generation places on a case */
	void GetExpectedCounts(const FUndergroundCase& Case, int32& OutEntrances, int32& OutNetworks)
	{
		int32 Candidates = 0;
		for (int32 Cell = 0; Cell < Case.Grid.GetCells().Num(); ++Cell)
		{
			Candidates += HexPlanetUnderground::CanHoldEntrance(Case.Grid, Case.Surface, Cell) ? 1 : 0;
		}
		HexPlanetUnderground::GetEntranceCounts(Case.Manifest.Underground, Case.Grid.GetCells().Num(), Candidates, OutEntrances, OutNetworks);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundFilesTest, "HexHero.Planet.Underground.Files",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundFilesTest::RunTest(const FString& Parameters)
{
	// the underground writes its levels with its own letters, V void and P passage, in lines as the surface does
	const TConstArrayView<FHexLevelLetter> Letters = HexPlanetFile::GetLevelLetters(EHexPlanetLayer::Underground);
	const int32 CellCount = FHexSphereGrid::CountCells(5);
	TArray<int8> Levels;
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		Levels.Add(Cell % 3 == 0 ? HexPlanetUnderground::VoidLevel : HexPlanetUnderground::PassageLevel);
	}
	const FString Text = HexPlanetFile::EncodeLevels(Levels, Letters);
	TestTrue(TEXT("Letters"), Text.StartsWith(TEXT("VPPVPP")));
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("Lines"), Lines.Num(), FMath::DivideAndRoundUp(CellCount, HexPlanetFile::LevelsLineLength));

	TArray<int8> Decoded;
	FString Error;
	TestTrue(TEXT("Decodes"), HexPlanetFile::DecodeLevels(Text, CellCount, Decoded, Error, Letters) && Decoded == Levels);
	TestTrue(TEXT("Lower case"), HexPlanetFile::DecodeLevels(TEXT("vp"), 2, Decoded, Error, Letters) && Decoded == TArray<int8>({-2, 0}));
	TestFalse(TEXT("A letter of the surface is no level underground"), HexPlanetFile::DecodeLevels(TEXT("VD"), 2, Decoded, Error, Letters));
	TestTrue(TEXT("Names the letters"), Error.Contains(TEXT("V or P")));
	TestEqual(TEXT("A level without a letter takes the nearest"), HexPlanetFile::EncodeLevels(TArray<int8>({-1, 1, 2}), Letters), FString(TEXT("VPP\n")));

	// the surface keeps its letters by default
	const TArray<int8> SurfaceLevels = {-2, -1, 0, 1, 2};
	TestEqual(TEXT("Surface letters"), HexPlanetFile::EncodeLevels(SurfaceLevels), FString(TEXT("DSPHM\n")));
	TestEqual(TEXT("Surface by its table"), HexPlanetFile::EncodeLevels(SurfaceLevels, HexPlanetFile::GetLevelLetters(EHexPlanetLayer::Surface)), FString(TEXT("DSPHM\n")));
	TestFalse(TEXT("V is no level of the surface"), HexPlanetFile::DecodeLevels(TEXT("V"), 1, Decoded, Error));

	// the underground keeps its files in its own folder of the planet, in the root of the planet
	TestEqual(TEXT("Data folder"), HexPlanetFile::GetDataFolder(TEXT("Terra"), EHexPlanetLayer::Underground), FString(TEXT("Terra/underground")));
	TestEqual(TEXT("Surface folder"), HexPlanetFile::GetDataFolder(TEXT("Terra"), EHexPlanetLayer::Surface), FString(TEXT("Terra")));
	TestTrue(TEXT("No folder of a new planet"), HexPlanetFile::GetDataFolder(FString(), EHexPlanetLayer::Underground).IsEmpty());
	TestEqual(TEXT("Under the planet"), FPaths::GetPath(HexPlanetFile::GetFilePath(TEXT("Terra/underground"), HexPlanetFile::LevelsFile)),
		FPaths::Combine(FPaths::GetPath(HexPlanetFile::GetFilePath(TEXT("Terra"), HexPlanetFile::LevelsFile)), TEXT("underground")));

	// entrances.json: a line per entrance, sorted, by the letters of the networks; one with a letter that is none is kept in memory, not written
	const TArray<FHexEntrance> Entrances = {{120, 1}, {7, 0}, {55, 7}};
	const FString Json = HexPlanetFile::EncodeEntrances(Entrances);
	TestTrue(TEXT("A line each, sorted"), Json.Contains(TEXT("\t\t{\"cell\": 7, \"network\": \"A\"},\n\t\t{\"cell\": 55, \"network\": \"H\"},\n\t\t{\"cell\": 120, \"network\": \"B\"}\n")));
	TArray<FHexEntrance> Read;
	TestTrue(TEXT("Entrances decode"), HexPlanetFile::DecodeEntrances(Json, Read, Error));
	TestTrue(TEXT("Same entrances"), Read == TArray<FHexEntrance>({{7, 0}, {55, 7}, {120, 1}}));
	TestTrue(TEXT("Unknown network"), HexPlanetFile::DecodeEntrances(TEXT("{\"version\": 1, \"entrances\": [{\"cell\": 3, \"network\": \"z\"}, {\"cell\": 4, \"network\": \"c\"}]}"), Read, Error)
		&& Read.Num() == 2 && Read[0].Network == INDEX_NONE && Read[1].Network == 2);
	TestFalse(TEXT("Not written"), HexPlanetFile::EncodeEntrances(Read).Contains(TEXT("\"cell\": 3")));
	TestEqual(TEXT("Empty"), HexPlanetFile::EncodeEntrances({}), FString(TEXT("{\n\t\"version\": 1,\n\t\"entrances\": []\n}\n")));
	TestTrue(TEXT("Letters of networks"), HexPlanetFile::GetNetworkLetter(3) == TEXT('D') && HexPlanetFile::GetNetworkLetter(8) == TEXT('?'));
	TestEqual(TEXT("Network of a letter"), HexPlanetFile::GetNetworkIndex(TEXT('h')), 7);
	TestEqual(TEXT("No network past H"), HexPlanetFile::GetNetworkIndex(TEXT('I')), int32(INDEX_NONE));

	// the block of the manifest keeps its defaults for missing fields, and the version stays 1
	FHexPlanetManifest Manifest;
	const FHexUndergroundGeneration Defaults;
	TestTrue(TEXT("Manifest without the block"), HexPlanetFile::ParseManifest(TEXT("{\"version\": 1, \"frequency\": 32}"), Manifest, Error)
		&& Manifest.Underground.EntranceCount == Defaults.EntranceCount && Manifest.Underground.PassageWidth == Defaults.PassageWidth);
	TestTrue(TEXT("Manifest with part of the block"), HexPlanetFile::ParseManifest(
		TEXT("{\"version\": 1, \"frequency\": 32, \"underground\": {\"entranceCount\": 3, \"passageWidth\": 3, \"wiggle\": 0.5}}"), Manifest, Error)
		&& Manifest.Underground.EntranceCount == 3 && Manifest.Underground.PassageWidth == 3 && Manifest.Underground.Wiggle == 0.5
		&& Manifest.Underground.NetworkCount == Defaults.NetworkCount && Manifest.Underground.HallRings == Defaults.HallRings);

	// objects.json underground names the cave kinds, a line per cell, and reads back the same
	{
		const FString Folder = TEXT("UndergroundObjectsTest_DeleteMe");
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), HexPlanetFile::SavedFolder, Folder);
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
		IFileManager::Get().MakeDirectory(*FPaths::Combine(Directory, HexPlanetFile::UndergroundFolder), true);
		ON_SCOPE_EXIT
		{
			IFileManager::Get().DeleteDirectory(*Directory, false, true);
		};
		const FString DataFolder = HexPlanetFile::GetDataFolder(Folder, EHexPlanetLayer::Underground);
		const auto MakeLine = [](int32 Cell, const TCHAR* Cover, int32 Density, const TCHAR* Decor)
		{
			FHexObjectCell Line;
			Line.Cell = Cell;
			Line.Cover = FName(Cover);
			Line.Density = Density;
			Line.Decor = FName(Decor);
			return Line;
		};
		TArray<FHexObjectCell> Cells;
		Cells.Add(MakeLine(12, TEXT("CaveMushrooms"), 3, TEXT("")));
		Cells.Add(MakeLine(40, TEXT("CaveStalagmites"), 1, TEXT("CaveStalagmiteField")));
		Cells.Add(MakeLine(97, TEXT(""), 0, TEXT("CaveRuneStone")));
		TestTrue(TEXT("Cave objects written"), HexPlanetFile::SaveObjects(DataFolder, Cells, Error));
		TestTrue(TEXT("Underground, not at the root"), IFileManager::Get().FileExists(*FPaths::Combine(Directory, HexPlanetFile::UndergroundFolder, HexPlanetFile::ObjectsFile))
			&& !IFileManager::Get().FileExists(*FPaths::Combine(Directory, HexPlanetFile::ObjectsFile)));
		TArray<FHexObjectCell> ReadCells;
		TestTrue(TEXT("Cave objects read"), HexPlanetFile::LoadObjects(DataFolder, ReadCells, Error));
		bool bSame = ReadCells.Num() == Cells.Num();
		for (int32 Index = 0; bSame && Index < Cells.Num(); ++Index)
		{
			bSame = ReadCells[Index].Cell == Cells[Index].Cell && ReadCells[Index].Cover == Cells[Index].Cover && ReadCells[Index].Decor == Cells[Index].Decor
				&& (Cells[Index].Cover.IsNone() || ReadCells[Index].Density == Cells[Index].Density);
		}
		TestTrue(TEXT("Cave objects round-trip"), bSame);
	}
	return true;
}

namespace
{
	/** Returns the rings of every cell from the nearest exit, MAX_int32 past MaxRings */
	TArray<int32> GetExitRings(const FHexSphereGrid& Grid, TConstArrayView<FHexEntrance> Entrances, int32 MaxRings)
	{
		const TArray<FHexSphereCell>& Cells = Grid.GetCells();
		TArray<int32> Rings;
		Rings.Init(MAX_int32, Cells.Num());
		TArray<int32> Frontier;
		for (const FHexEntrance& Entrance : Entrances)
		{
			Rings[Entrance.Cell] = 0;
			Frontier.Add(Entrance.Cell);
		}
		for (int32 Index = 0; Index < Frontier.Num(); ++Index)
		{
			if (Rings[Frontier[Index]] >= MaxRings)
			{
				continue;
			}
			for (const int32 Neighbor : Cells[Frontier[Index]].Neighbors)
			{
				if (Rings[Neighbor] == MAX_int32)
				{
					Rings[Neighbor] = Rings[Frontier[Index]] + 1;
					Frontier.Add(Neighbor);
				}
			}
		}
		return Rings;
	}

	/** Returns the mean number of neighbors of the cells of a set that are in it too */
	double GetMeanSameNeighbors(const FHexSphereGrid& Grid, const TSet<int32>& Set)
	{
		int32 Same = 0;
		for (const int32 Cell : Set)
		{
			for (const int32 Neighbor : Grid.GetCells()[Cell].Neighbors)
			{
				Same += Set.Contains(Neighbor) ? 1 : 0;
			}
		}
		return Set.IsEmpty() ? 0.0 : double(Same) / Set.Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundObjectsTest, "HexHero.Planet.Underground.Objects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundObjectsTest::RunTest(const FString& Parameters)
{
	using namespace HexPlanetUnderground;

	FUndergroundCase Case = MakeSurface(12, 7, 0.55);
	Case.Manifest.Underground.EntranceCount = 8;
	Case.Manifest.Underground.NetworkCount = 2;
	Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Case.Result);
	const TArray<FHexSphereCell>& Cells = Case.Grid.GetCells();
	const TArray<int8>& Levels = Case.Result.Levels;
	const TArray<FHexEntrance>& Entrances = Case.Result.Entrances;
	TestTrue(TEXT("A good maze"), Case.Result.Report.IsGood() && Case.Result.Report.PassageCells > 100);

	// the kinds by the indices of their sets, which are not their places in the list: set 3 is a rim of the walls, which nothing generates
	const auto MakeKind = [](int32 SetIndex, const TCHAR* Id, bool bDecor, float Share, float PatchCells, const TCHAR* Group, int32 ClearRings, bool bBlocks)
	{
		FObjectKind Kind;
		Kind.SetIndex = SetIndex;
		Kind.Id = Id;
		Kind.bDecor = bDecor;
		Kind.bBlocks = bBlocks;
		Kind.Levels = {PassageLevel};
		Kind.Variants.Add({FString(), Share});
		Kind.PatchCells = PatchCells;
		Kind.Group = Group;
		Kind.ClearRings = ClearRings;
		return Kind;
	};
	const TArray<FObjectKind> Kinds = {MakeKind(1, TEXT("Grove"), false, 0.5f, 3.0f, TEXT("G"), 0, false), MakeKind(2, TEXT("GroveHeart"), true, 0.2f, 3.0f, TEXT("G"), 0, false),
		MakeKind(4, TEXT("Anywhere"), true, 0.1f, 0.0f, TEXT(""), 2, false), MakeKind(5, TEXT("Wall"), false, 0.5f, 0.0f, TEXT(""), 0, true)};
	TArray<TCHAR> Letters;
	const int32 Seed = GetSeed(Case.Manifest);
	GenerateBiomes(Case.Grid, Levels, Entrances, Case.Manifest.Underground, Seed, Letters);
	TArray<FHexCellObjects> Objects;
	FObjectReport Report;
	GenerateObjects(Case.Grid, Levels, Letters, Entrances, Seed, Kinds, Objects, &Report);
	TestEqual(TEXT("A line per cell"), Objects.Num(), Cells.Num());

	const TArray<int32> Rings = GetExitRings(Case.Grid, Entrances, 3);
	TSet<int32> Grove;
	TSet<int32> Heart;
	int32 Anywhere = 0;
	int32 OffPassage = 0;
	int32 OnExits = 0;
	int32 OnPentagons = 0;
	int32 BadIndex = 0;
	int32 NearExit = 0;
	int32 Blocking = 0;
	int32 Candidates = 0;
	int32 AnywhereCandidates = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const FHexCellObjects& Each = Objects[Cell];
		const bool bCandidate = IsPassage(Levels[Cell]) && Rings[Cell] > 0 && !Cells[Cell].IsPentagon();
		Candidates += bCandidate ? 1 : 0;
		AnywhereCandidates += bCandidate && Rings[Cell] > 2 && Each.Decor != 3 ? 1 : 0;
		if (Each.IsEmpty())
		{
			continue;
		}
		OffPassage += IsPassage(Levels[Cell]) ? 0 : 1;
		OnExits += Rings[Cell] == 0 ? 1 : 0;
		OnPentagons += Cells[Cell].IsPentagon() ? 1 : 0;
		// what is written is the index of a set plus one, never a place in the list nor the rim set between
		BadIndex += (Each.Cover != 0 && Each.Cover != 2 && Each.Cover != 6) || (Each.Decor != 0 && Each.Decor != 3 && Each.Decor != 5) ? 1 : 0;
		Blocking += Each.Cover == 6 ? 1 : 0;
		if (Each.Cover == 2)
		{
			Grove.Add(Cell);
			TestTrue(TEXT("A density of 1 to 3"), Each.Density >= 1 && Each.Density <= 3);
		}
		if (Each.Decor == 3)
		{
			Heart.Add(Cell);
		}
		if (Each.Decor == 5)
		{
			++Anywhere;
			NearExit += Rings[Cell] <= 2 ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d passage cells, %d candidates: grove %d, heart %d, anywhere %d (patch at %d)"), Case.Result.Report.PassageCells, Candidates, Grove.Num(),
		Heart.Num(), Anywhere, Report.PatchCenters[0]));
	TestEqual(TEXT("Only on passages"), OffPassage, 0);
	TestEqual(TEXT("Never on an exit"), OnExits, 0);
	TestEqual(TEXT("Never on a pentagon"), OnPentagons, 0);
	TestEqual(TEXT("Indices of the sets"), BadIndex, 0);
	TestEqual(TEXT("Kept two rings from the exits"), NearExit, 0);
	TestEqual(TEXT("The blocking kind gets nothing"), Blocking, 0);
	TestEqual(TEXT("Cover count"), Grove.Num(), FMath::RoundToInt32(0.5f * Candidates));
	TestEqual(TEXT("Decor count"), Heart.Num(), FMath::RoundToInt32(0.2f * Candidates));
	TestEqual(TEXT("Decor anywhere count"), Anywhere, FMath::RoundToInt32(0.1f * AnywhereCandidates));
	TestEqual(TEXT("Report counts"), Report.Cells[0], Grove.Num());
	TestTrue(TEXT("Report has the patch of the cover"), Grove.Contains(Report.PatchCenters[0]));
	TestTrue(TEXT("The blocking kind is reported empty"), Report.Cells[3] == 0 && Report.PatchCenters[3] == INDEX_NONE);
	TestTrue(TEXT("The decor of a group stands in the patches of its cover"), Grove.Includes(Heart));

	// patches: the cells of a cover have more neighbors of their own than the same number of cells chosen by a hash among the candidates. In
	// corridors two cells wide a cell has about four neighbors on passages, so a random half of them already has two; a sparse cover, as the cave
	// kinds are (0.03 to 0.3), shows it the clearer
	TArray<int32> Shuffled;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (IsPassage(Levels[Cell]) && Rings[Cell] > 0 && !Cells[Cell].IsPentagon())
		{
			Shuffled.Add(Cell);
		}
	}
	Shuffled.Sort([](int32 A, int32 B) { return HashCombine(GetTypeHash(A), 0x9E3779B9u) < HashCombine(GetTypeHash(B), 0x9E3779B9u); });
	const auto GetPatchRatio = [&](const TSet<int32>& Chosen)
	{
		TSet<int32> Random;
		for (int32 Index = 0; Index < Chosen.Num() && Index < Shuffled.Num(); ++Index)
		{
			Random.Add(Shuffled[Index]);
		}
		const double PatchNeighbors = GetMeanSameNeighbors(Case.Grid, Chosen);
		const double RandomNeighbors = GetMeanSameNeighbors(Case.Grid, Random);
		AddInfo(FString::Printf(TEXT("Neighbors in the set of %d cells: %.2f in patches, %.2f at random"), Chosen.Num(), PatchNeighbors, RandomNeighbors));
		return RandomNeighbors > 0.0 ? PatchNeighbors / RandomNeighbors : 0.0;
	};
	TestTrue(TEXT("Patches form at half the cells"), GetPatchRatio(Grove) >= 1.5);
	{
		TArray<FHexCellObjects> Sparse;
		GenerateObjects(Case.Grid, Levels, Letters, Entrances, Seed, {MakeKind(1, TEXT("Grove"), false, 0.15f, 3.0f, TEXT("G"), 0, false)}, Sparse);
		TSet<int32> Chosen;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			if (Sparse[Cell].Cover == 2)
			{
				Chosen.Add(Cell);
			}
		}
		TestTrue(TEXT("Patches form"), GetPatchRatio(Chosen) >= 1.5);
	}

	// the same input gives the same, another seed another
	TArray<FHexCellObjects> Again;
	GenerateObjects(Case.Grid, Levels, Letters, Entrances, Seed, Kinds, Again);
	TestTrue(TEXT("Deterministic"), Again == Objects);
	GenerateObjects(Case.Grid, Levels, Letters, Entrances, Seed + 1, Kinds, Again);
	TestFalse(TEXT("Another seed, other patches"), Again == Objects);

	// nothing it places cuts a passage: with every cell that blocks made void the maze is the same
	TArray<int8> Blocked = Levels;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const bool bBlocks = Objects[Cell].Cover == 6;
		Blocked[Cell] = bBlocks ? VoidLevel : Blocked[Cell];
	}
	const FReport Before = Check(Case.Grid, Case.Surface, Levels, Entrances);
	const FReport After = Check(Case.Grid, Case.Surface, Blocked, Entrances);
	bool bSamePieces = After.Necks.Num() == Before.Necks.Num();
	for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
	{
		bSamePieces &= After.Networks[Network].Components == Before.Networks[Network].Components;
	}
	TestTrue(TEXT("No passage cut"), bSamePieces);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundFootprintTest, "HexHero.Planet.Underground.Footprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundFootprintTest::RunTest(const FString& Parameters)
{
	using namespace HexPlanetUnderground;

	// a regular hexagon of radius 100, corner K at 60 K degrees, so side 0 runs from corner 5 to corner 0 with its middle at 330 degrees
	const auto MakeCorners = [](int32 Count, bool bReversed)
	{
		TArray<FVector2f> Corners;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = 2.0f * UE_PI * (bReversed ? -Index : Index) / Count;
			Corners.Add(FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * 100.0f);
		}
		return Corners;
	};
	const auto At = [](float Degrees, float Distance) { return FVector2f(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees))) * Distance; };
	for (const bool bReversed : {false, true})
	{
		const TArray<FVector2f> Hexagon = MakeCorners(6, bReversed);
		const float Toward = bReversed ? -330.0f : 330.0f;
		const float Away = bReversed ? -30.0f : 30.0f;
		const FString Way = bReversed ? TEXT(", corners the other way") : TEXT("");
		// 70 from the middle towards side 0 lies 16.6 from its line
		TestFalse(*(TEXT("Over a clipped side") + Way), IsFootprintClear(At(Toward, 70.0f), 20.0f, Hexagon, 1u << 0));
		TestTrue(*(TEXT("Within a clipped side") + Way), IsFootprintClear(At(Toward, 70.0f), 15.0f, Hexagon, 1u << 0));
		TestTrue(*(TEXT("Over a side that is not clipped") + Way), IsFootprintClear(At(Toward, 70.0f), 20.0f, Hexagon, 1u << 1));
		TestTrue(*(TEXT("Nothing clipped") + Way), IsFootprintClear(At(Toward, 99.0f), 50.0f, Hexagon, 0));
		TestTrue(*(TEXT("Middle, all sides") + Way), IsFootprintClear(FVector2f::ZeroVector, 86.0f, Hexagon, 0x3F));
		TestFalse(*(TEXT("Middle, too wide") + Way), IsFootprintClear(FVector2f::ZeroVector, 87.0f, Hexagon, 0x3F));
		// past a corner: side 1 runs from corner 0 to corner 1 with its middle at 30 degrees
		TestFalse(*(TEXT("Side 1 clipped") + Way), IsFootprintClear(At(Away, 80.0f), 10.0f, Hexagon, 1u << 1));
		TestTrue(*(TEXT("Side 1 not clipped") + Way), IsFootprintClear(At(Away, 80.0f), 10.0f, Hexagon, 1u << 4));

		// a pentagon of radius 100 has an apothem of 80.9
		const TArray<FVector2f> Pentagon = MakeCorners(5, bReversed);
		TestTrue(*(TEXT("Pentagon, all sides") + Way), IsFootprintClear(FVector2f::ZeroVector, 80.5f, Pentagon, 0x1F));
		TestFalse(*(TEXT("Pentagon, too wide") + Way), IsFootprintClear(FVector2f::ZeroVector, 81.5f, Pentagon, 0x1F));
		TestFalse(*(TEXT("Pentagon, its last side") + Way), IsFootprintClear(At(bReversed ? -324.0f : 324.0f, 70.0f), 15.0f, Pentagon, 1u << 0));
		TestTrue(*(TEXT("Pentagon, another side") + Way), IsFootprintClear(At(bReversed ? -324.0f : 324.0f, 70.0f), 15.0f, Pentagon, 1u << 2));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundRimsTest, "HexHero.Planet.Underground.Rims",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundRimsTest::RunTest(const FString& Parameters)
{
	using namespace HexPlanetUnderground;

	FUndergroundCase Case = MakeSurface(20, 9, 0.55);
	Case.Manifest.Underground.EntranceCount = 10;
	Case.Manifest.Underground.NetworkCount = 3;
	Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Case.Result);
	const FHexSphereGrid& Grid = Case.Grid;
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const TArray<int8>& Levels = Case.Result.Levels;
	TestTrue(TEXT("A good maze"), Case.Result.Report.IsGood() && Case.Result.Report.PassageCells > 100);

	// the exits stay clear, as UHexPlanetObjects::SetClearCells keeps them
	TArray<int32> Exits;
	for (const FHexEntrance& Entrance : Case.Result.Entrances)
	{
		Exits.Add(Entrance.Cell);
	}
	const TSet<int32> Clear(Exits);
	TArray<TArray<int32>> ChunkCells;
	ChunkCells.SetNum(Grid.GetNumChunks());
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		ChunkCells[Cells[Cell].Chunk].Add(Cell);
	}

	// the rings, which the search over neighbors counts whatever the level
	const TArray<int32> Rings = GetRingDistances(Grid, Exits, 2);
	bool bRings = true;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		int32 Nearest = Clear.Contains(Cell) ? 0 : MAX_int32;
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			Nearest = Clear.Contains(Neighbor) ? FMath::Min(Nearest, 1) : Nearest;
		}
		bRings &= Nearest <= 1 ? Rings[Cell] == Nearest : Rings[Cell] >= 2;
	}
	TestTrue(TEXT("Rings from the exits"), bRings);

	// every side of a passage towards a void, by chunk as the objects place them, against the sides of every cell
	const auto Collect = [&](int32 MinDrop, TFunctionRef<bool(int32)> IsSkipped)
	{
		TArray<FIntPoint> Found;
		for (const TArray<int32>& Chunk : ChunkCells)
		{
			FindRimSides(Grid, Levels, MinDrop, IsSkipped, Chunk, Found);
		}
		return Found;
	};
	const auto Expect = [&](TFunctionRef<bool(int32)> IsSkipped)
	{
		TSet<FIntPoint> Expected;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			for (int32 Side = 0; Side < Cells[Cell].Neighbors.Num(); ++Side)
			{
				const int32 Neighbor = Cells[Cell].Neighbors[Side];
				if (IsPassage(Levels[Cell]) && !IsPassage(Levels[Neighbor]) && !IsSkipped(Cell) && !IsSkipped(Neighbor))
				{
					Expected.Add(FIntPoint(Cell, Side));
				}
			}
		}
		return Expected;
	};
	const auto Compare = [&](const TCHAR* What, const TArray<FIntPoint>& Found, const TSet<FIntPoint>& Expected)
	{
		TMap<FIntPoint, int32> Counts;
		int32 Wrong = 0;
		for (const FIntPoint& Pair : Found)
		{
			++Counts.FindOrAdd(Pair);
			const int32 Neighbor = Cells[Pair.X].Neighbors[Pair.Y];
			// the passage first and its void second, never two voids nor two passages
			Wrong += IsPassage(Levels[Pair.X]) && !IsPassage(Levels[Neighbor]) ? 0 : 1;
		}
		int32 Twice = 0;
		for (const TPair<FIntPoint, int32>& Each : Counts)
		{
			Twice += Each.Value > 1 ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("%s: %d sides of %d"), What, Found.Num(), Expected.Num()));
		TestEqual(*FString::Printf(TEXT("%s: a passage and its void"), What), Wrong, 0);
		TestEqual(*FString::Printf(TEXT("%s: each side once"), What), Twice, 0);
		TestEqual(*FString::Printf(TEXT("%s: as many as there are"), What), Found.Num(), Expected.Num());
		TestTrue(*FString::Printf(TEXT("%s: every one of them"), What), Expected.Num() == Counts.Num() && Expected.Includes(TSet<FIntPoint>(Found)));
	};

	const auto IsExit = [&Clear](int32 Cell) { return Clear.Contains(Cell); };
	const TArray<FIntPoint> Sides = Collect(2, IsExit);
	TestTrue(TEXT("The maze has sides over the void"), Sides.Num() > 100);
	Compare(TEXT("Rims"), Sides, Expect(IsExit));
	int32 OnExits = 0;
	for (const FIntPoint& Pair : Sides)
	{
		OnExits += Clear.Contains(Pair.X) || Clear.Contains(Cells[Pair.X].Neighbors[Pair.Y]) ? 1 : 0;
	}
	TestEqual(TEXT("No side of an exit"), OnExits, 0);

	// a void lies only two levels below a passage
	TestEqual(TEXT("None three levels down"), Collect(3, IsExit).Num(), 0);

	// a kind that keeps two rings clear of the exits
	const auto IsNearExit = [&Rings](int32 Cell) { return Rings[Cell] <= 2; };
	const TArray<FIntPoint> Far = Collect(2, IsNearExit);
	Compare(TEXT("Rims two rings from the exits"), Far, Expect(IsNearExit));
	int32 Near = 0;
	for (const FIntPoint& Pair : Far)
	{
		Near += Rings[Pair.X] <= 2 || Rings[Cells[Pair.X].Neighbors[Pair.Y]] <= 2 ? 1 : 0;
	}
	TestEqual(TEXT("No side within two rings of an exit"), Near, 0);
	TestTrue(TEXT("Fewer sides away from the exits"), Far.Num() < Sides.Num() && Far.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundObjectSetsTest, "HexHero.Planet.Underground.ObjectSets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundObjectSetsTest::RunTest(const FString& Parameters)
{
	// the real kinds of objects: the cave kinds stand only underground, block nothing, keep off the voids and are culled, the surface kinds stand
	// only on the surface, and every mesh of either has its LODs
	const TArray<TObjectPtr<UHexPlanetObjectSet>> Surface = UHexPlanetObjects::FindLayerSets(EHexPlanetLayer::Surface);
	const TArray<TObjectPtr<UHexPlanetObjectSet>> Underground = UHexPlanetObjects::FindLayerSets(EHexPlanetLayer::Underground);
	const auto CheckMeshes = [this](const UHexPlanetObjectSet& Set)
	{
		int32 Meshes = 0;
		for (const FHexObjectVariant& Variant : Set.Variants)
		{
			for (const TSoftObjectPtr<UStaticMesh>& Soft : Variant.Meshes)
			{
				const UStaticMesh* Mesh = Soft.LoadSynchronous();
#if WITH_EDITOR
				const int32 Lods = Mesh ? Mesh->GetNumSourceModels() : 0;
#else
				const int32 Lods = Mesh ? Mesh->GetNumLODs() : 0;
#endif
				TestTrue(*FString::Printf(TEXT("%s: mesh %s with 3 LODs"), *Set.Id.ToString(), *Soft.ToString()), Lods >= 3);
				++Meshes;
			}
		}
		TestTrue(*FString::Printf(TEXT("%s has meshes"), *Set.Id.ToString()), Meshes > 0);
	};

	const TCHAR* CaveIds[] = {TEXT("CaveMushrooms"), TEXT("CaveStalagmites"), TEXT("CaveRubble"), TEXT("CaveCrystals"), TEXT("CaveStalagmiteField"), TEXT("CaveBones"), TEXT("CaveRuneStone")};
	for (const TCHAR* Id : CaveIds)
	{
		const TObjectPtr<UHexPlanetObjectSet>* Found = Underground.FindByPredicate([Id](const UHexPlanetObjectSet* Each) { return Each->Id == FName(Id); });
		if (!TestNotNull(*FString::Printf(TEXT("%s exists underground"), Id), Found))
		{
			continue;
		}
		const UHexPlanetObjectSet& Set = **Found;
		TestEqual(*FString::Printf(TEXT("%s only underground"), Id), Set.PlanetLayers, GetPlanetLayerBit(EHexPlanetLayer::Underground));
		TestFalse(*FString::Printf(TEXT("%s blocks nothing"), Id), Set.bBlocksMovement);
		TestTrue(*FString::Printf(TEXT("%s keeps off the voids"), Id), Set.bClipFootprint);
		TestTrue(*FString::Printf(TEXT("%s is culled"), Id), Set.CullDistance > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s only on passages"), Id), Set.AllowedLevels == TArray<int32>({HexPlanetUnderground::PassageLevel}));
		CheckMeshes(Set);
	}
	TestEqual(TEXT("The kinds the generation places"), UHexPlanetObjects::MakeObjectKinds(Underground).Num(), int32(UE_ARRAY_COUNT(CaveIds)));
	TestEqual(TEXT("The kinds the palette paints"), UHexPlanetObjects::GetPaintableSets(Underground).Num(), int32(UE_ARRAY_COUNT(CaveIds)));

	// the rims of the chasms: placed along the sides by the planet, never stored, painted or generated, so they sort after every other kind and
	// the indices of the cave kinds in objects.json stay as they are
	const TCHAR* RimIds[] = {TEXT("CaveRim"), TEXT("CaveRimSpires"), TEXT("CaveStalactites"), TEXT("CaveRimRunes")};
	int32 HighestOther = TNumericLimits<int32>::Lowest();
	for (const UHexPlanetObjectSet* Set : Underground)
	{
		HighestOther = Set->Layer != EHexObjectLayer::Rim ? FMath::Max(HighestOther, Set->SortOrder) : HighestOther;
	}
	for (const TCHAR* Id : RimIds)
	{
		const TObjectPtr<UHexPlanetObjectSet>* Found = Underground.FindByPredicate([Id](const UHexPlanetObjectSet* Each) { return Each->Id == FName(Id); });
		if (!TestNotNull(*FString::Printf(TEXT("%s exists underground"), Id), Found))
		{
			continue;
		}
		const UHexPlanetObjectSet& Set = **Found;
		TestTrue(*FString::Printf(TEXT("%s is a rim"), Id), Set.Layer == EHexObjectLayer::Rim);
		TestEqual(*FString::Printf(TEXT("%s only underground"), Id), Set.PlanetLayers, GetPlanetLayerBit(EHexPlanetLayer::Underground));
		TestFalse(*FString::Printf(TEXT("%s blocks nothing"), Id), Set.bBlocksMovement);
		TestTrue(*FString::Printf(TEXT("%s is culled"), Id), Set.CullDistance > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s sorts after the other kinds"), Id), Set.SortOrder > 300 && Set.SortOrder > HighestOther);
		TestTrue(*FString::Printf(TEXT("%s stands on the lips of passages over voids"), Id), Set.AllowedLevels.Contains(HexPlanetUnderground::PassageLevel) && Set.RimMinDrop == 2);
		TestTrue(*FString::Printf(TEXT("%s hangs only if it is stalactites"), Id), Set.bRimHang == (FCString::Strcmp(Id, TEXT("CaveStalactites")) == 0));
		CheckMeshes(Set);
	}

	TestEqual(TEXT("The surface has its ten kinds"), Surface.Num(), 10);
	for (const UHexPlanetObjectSet* Set : Surface)
	{
		TestFalse(*FString::Printf(TEXT("%s not underground"), *Set->Id.ToString()), Set->IsOnLayer(EHexPlanetLayer::Underground));
		TestFalse(*FString::Printf(TEXT("%s keeps its placement"), *Set->Id.ToString()), Set->bClipFootprint);
		CheckMeshes(*Set);
	}
	for (const UHexPlanetObjectSet* Set : Underground)
	{
		TestFalse(*FString::Printf(TEXT("%s is not on both layers"), *Set->Id.ToString()), Surface.Contains(Set));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundSeedTest, "HexHero.Planet.Underground.Seed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundSeedTest::RunTest(const FString& Parameters)
{
	// the same planet gives the same files, another seed or offset another maze
	FUndergroundCase Case = MakeSurface(32, 3, 0.35);
	const TConstArrayView<FHexLevelLetter> Letters = HexPlanetFile::GetLevelLetters(EHexPlanetLayer::Underground);
	const TArray<TCHAR> BiomeLetters = {HexPlanetUnderground::Biome::Abyss, HexPlanetUnderground::Biome::Floor, HexPlanetUnderground::Biome::Lake, HexPlanetUnderground::Biome::Lava};
	const auto Files = [&Case, &Letters, &BiomeLetters](const HexPlanetUnderground::FResult& Result)
	{
		TArray<uint8> Biomes;
		for (const TCHAR Letter : Result.Biomes)
		{
			Biomes.Add(uint8(BiomeLetters.Find(Letter)));
		}
		return HexPlanetFile::EncodeEntrances(Result.Entrances) + HexPlanetFile::EncodeLevels(Result.Levels, Letters) + HexPlanetFile::EncodeBiomes(Biomes, BiomeLetters);
	};

	HexPlanetUnderground::FResult First;
	HexPlanetUnderground::FResult Again;
	HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, First);
	HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Again);
	TestEqual(TEXT("Same seed, same files"), Files(Again), Files(First));
	TestTrue(TEXT("Entrances placed"), First.bPlacedEntrances && !First.Entrances.IsEmpty());
	TestTrue(TEXT("Lava and lakes in the files"), First.Biomes.Contains(HexPlanetUnderground::Biome::Lava) && First.Biomes.Contains(HexPlanetUnderground::Biome::Lake));
	{
		// and they survive a write and a read of biomes.txt
		TArray<uint8> Biomes;
		for (const TCHAR Letter : First.Biomes)
		{
			Biomes.Add(uint8(BiomeLetters.Find(Letter)));
		}
		TArray<uint8> Read;
		FString Error;
		TestTrue(TEXT("Biomes read back"), HexPlanetFile::DecodeBiomes(HexPlanetFile::EncodeBiomes(Biomes, BiomeLetters), Biomes.Num(), BiomeLetters, Read, Error) && Read == Biomes);
	}

	// given the entrances it placed, it carves the same maze
	HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, &First.Entrances, Again);
	TestTrue(TEXT("Same maze for its entrances"), !Again.bPlacedEntrances && Again.Levels == First.Levels);

	Case.Manifest.Underground.SeedOffset = 1;
	HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, &First.Entrances, Again);
	TestFalse(TEXT("Another offset, another maze"), Again.Levels == First.Levels);
	TestTrue(TEXT("Still good"), Again.Report.IsGood());

	Case.Manifest.Underground.SeedOffset = 0;
	Case.Manifest.Seed = 4;
	HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Again);
	TestFalse(TEXT("Another seed, other entrances"), Again.Entrances == First.Entrances);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundNetworksTest, "HexHero.Planet.Underground.Networks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundNetworksTest::RunTest(const FString& Parameters)
{
	// every network is one piece of passage, and no passage of one touches another
	for (const FUndergroundCase& Case : MakeCases())
	{
		const HexPlanetUnderground::FReport& Report = Case.Result.Report;
		const FHexUndergroundGeneration& Generation = Case.Manifest.Underground;
		AddInfo(FString::Printf(TEXT("%s: %s, %.0f ms"), *Case.Name, *Report.ToString(), Case.Result.Seconds * 1000.0));
		int32 ExpectedEntrances = 0;
		int32 ExpectedNetworks = 0;
		GetExpectedCounts(Case, ExpectedEntrances, ExpectedNetworks);
		TestTrue(*FString::Printf(TEXT("%s is good"), *Case.Name), Report.IsGood());
		TestTrue(*FString::Printf(TEXT("%s has networks"), *Case.Name), ExpectedNetworks > 0);
		TestEqual(*FString::Printf(TEXT("%s has its networks"), *Case.Name), Report.CountNetworks(), ExpectedNetworks);
		TestTrue(*FString::Printf(TEXT("%s joins no networks"), *Case.Name), Report.Merged.IsEmpty());
		TestEqual(*FString::Printf(TEXT("%s reaches every passage"), *Case.Name), Report.OrphanCells, 0);

		for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
		{
			const HexPlanetUnderground::FNetworkReport& Each = Report.Networks[Network];
			if (Each.Entrances > 0)
			{
				TestEqual(*FString::Printf(TEXT("%s: network %c is connected"), *Case.Name, HexPlanetFile::GetNetworkLetter(Network)), Each.Components, 1);
				TestTrue(*FString::Printf(TEXT("%s: network %c has %d entrances"), *Case.Name, HexPlanetFile::GetNetworkLetter(Network), Each.Entrances),
					Each.Entrances >= Generation.MinEntrancesPerNetwork && Each.Entrances <= Generation.MaxEntrancesPerNetwork);
			}
		}

		// apart: a passage cell of one network never has a neighbor of another
		const TArray<FHexSphereCell>& Cells = Case.Grid.GetCells();
		int32 Touching = 0;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				const int8 A = Report.CellNetworks[Cell];
				const int8 B = Report.CellNetworks[Neighbor];
				Touching += A >= 0 && B >= 0 && A != B ? 1 : 0;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: networks touch"), *Case.Name), Touching, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundSweepTest, "HexHero.Planet.Underground.Sweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundSweepTest::RunTest(const FString& Parameters)
{
	// many seeds, by the defaults and crowded, with passages of 2 and 3, all good: networks joined and apart, exits on passages, no necks
	int32 Bad = 0;
	for (int32 Seed = 0; Seed < 24; ++Seed)
	{
		FUndergroundCase Case = MakeSurface(Seed % 2 == 0 ? 24 : 32, 100 + Seed, 0.3 + 0.05 * (Seed % 6));
		if (Seed % 3 != 0)
		{
			Case.Manifest.Underground.EntranceCount = 8 + Seed % 7;
			Case.Manifest.Underground.NetworkCount = 2 + Seed % 5;
		}
		Case.Manifest.Underground.PassageWidth = Seed % 4 == 3 ? 3 : 2;
		HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Case.Result);
		if (!Case.Result.Report.IsGood())
		{
			++Bad;
			AddError(FString::Printf(TEXT("%s, width %d: %s"), *Case.Name, Case.Manifest.Underground.PassageWidth, *Case.Result.Report.ToString()));
		}
	}
	TestEqual(TEXT("Seeds with problems"), Bad, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundEntrancesTest, "HexHero.Planet.Underground.Entrances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundEntrancesTest::RunTest(const FString& Parameters)
{
	// placed entrances stand on plains or hills, never on a pentagon, and each exit below is a passage
	for (const FUndergroundCase& Case : MakeCases())
	{
		const TArray<FHexSphereCell>& Cells = Case.Grid.GetCells();
		int32 ExpectedEntrances = 0;
		int32 ExpectedNetworks = 0;
		GetExpectedCounts(Case, ExpectedEntrances, ExpectedNetworks);
		TestEqual(*FString::Printf(TEXT("%s places its entrances"), *Case.Name), Case.Result.Entrances.Num(), ExpectedEntrances);
		TSet<int32> Seen;
		for (const FHexEntrance& Entrance : Case.Result.Entrances)
		{
			const int32 Level = Case.Surface[Entrance.Cell];
			TestTrue(*FString::Printf(TEXT("%s: entrance %d on plains or hills"), *Case.Name, Entrance.Cell), Level == 0 || Level == 1);
			TestFalse(*FString::Printf(TEXT("%s: entrance %d on a pentagon"), *Case.Name, Entrance.Cell), Cells[Entrance.Cell].IsPentagon());
			TestTrue(*FString::Printf(TEXT("%s: exit %d is a passage"), *Case.Name, Entrance.Cell), HexPlanetUnderground::IsPassage(Case.Result.Levels[Entrance.Cell]));
			TestTrue(*FString::Printf(TEXT("%s: entrance %d has a network"), *Case.Name, Entrance.Cell), Entrance.Network >= 0 && Entrance.Network < HexPlanetFile::MaxNetworks);
			TestFalse(*FString::Printf(TEXT("%s: one entrance on %d"), *Case.Name, Entrance.Cell), Seen.Contains(Entrance.Cell));
			Seen.Add(Entrance.Cell);
		}
		TestTrue(*FString::Printf(TEXT("%s: no exit in the void"), *Case.Name), Case.Result.Report.ExitsInVoid.IsEmpty());
		TestEqual(*FString::Printf(TEXT("%s: every entrance saved"), *Case.Name),
			HexPlanetUnderground::GetSavedEntrances(Case.Grid, Case.Surface, Case.Result.Levels, Case.Result.Entrances).Num(), Case.Result.Entrances.Num());
	}

	// entrances given with problems are kept, reported and left out of a save, while the others are joined
	FUndergroundCase Case = MakeSurface(24, 5, 0.5);
	const TArray<FHexSphereCell>& Cells = Case.Grid.GetCells();
	TArray<int32> Land;
	int32 Sea = INDEX_NONE;
	int32 Pentagon = INDEX_NONE;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (HexPlanetUnderground::CanHoldEntrance(Case.Grid, Case.Surface, Cell))
		{
			Land.Add(Cell);
		}
		Sea = Sea == INDEX_NONE && Case.Surface[Cell] < 0 ? Cell : Sea;
		Pentagon = Pentagon == INDEX_NONE && Cells[Cell].IsPentagon() && Case.Surface[Cell] >= 0 && Case.Surface[Cell] <= 1 ? Cell : Pentagon;
	}
	if (!TestTrue(TEXT("Surface has land and sea"), Land.Num() > 100 && Sea != INDEX_NONE))
	{
		return true;
	}

	TArray<FHexEntrance> Given = {{Land[0], 0}, {Land[Land.Num() / 3], 0}, {Land[Land.Num() * 2 / 3], 1}, {Land.Last(), 1},
		{Sea, 0}, {Land[0], 1}, {Land[10], INDEX_NONE}, {Cells.Num() + 5, 0}};
	if (Pentagon != INDEX_NONE)
	{
		Given.Add({Pentagon, 1});
	}
	HexPlanetUnderground::FResult Result;
	HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, &Given, Result);
	TestTrue(TEXT("Keeps the given entrances"), Result.Entrances == Given && !Result.bPlacedEntrances);

	const auto Problem = [&](int32 Index) { return HexPlanetUnderground::GetEntranceProblem(Case.Grid, Case.Surface, Result.Levels, Given, Index); };
	TestTrue(TEXT("Sea"), Problem(4) == HexPlanetUnderground::EEntranceProblem::NotLand);
	TestTrue(TEXT("Duplicate"), Problem(5) == HexPlanetUnderground::EEntranceProblem::Duplicate);
	TestTrue(TEXT("No network"), Problem(6) == HexPlanetUnderground::EEntranceProblem::NoNetwork);
	TestTrue(TEXT("No cell"), Problem(7) == HexPlanetUnderground::EEntranceProblem::NoCell);
	if (Pentagon != INDEX_NONE)
	{
		TestTrue(TEXT("Pentagon"), Problem(8) == HexPlanetUnderground::EEntranceProblem::Pentagon);
	}
	TestEqual(TEXT("Bad entrances reported"), Result.Report.BadEntrances.Num(), Given.Num() - 4);
	TestEqual(TEXT("Two networks"), Result.Report.CountNetworks(), 2);
	TestEqual(TEXT("A joined"), Result.Report.Networks[0].Components, 1);
	TestEqual(TEXT("B joined"), Result.Report.Networks[1].Components, 1);
	TestTrue(TEXT("Apart"), Result.Report.Merged.IsEmpty());
	TestEqual(TEXT("A save keeps the good ones"), HexPlanetUnderground::GetSavedEntrances(Case.Grid, Case.Surface, Result.Levels, Given).Num(), 4);

	// an exit painted into the void is reported and left out of a save
	TArray<int8> Painted = Result.Levels;
	Painted[Given[0].Cell] = HexPlanetUnderground::VoidLevel;
	const HexPlanetUnderground::FReport Report = HexPlanetUnderground::Check(Case.Grid, Case.Surface, Painted, Given);
	TestTrue(TEXT("Exit in the void"), Report.ExitsInVoid == TArray<int32>({Given[0].Cell}));
	TestTrue(TEXT("Not good"), !Report.IsGood());
	TestEqual(TEXT("Left out of a save"), HexPlanetUnderground::GetSavedEntrances(Case.Grid, Case.Surface, Painted, Given).Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundNecksTest, "HexHero.Planet.Underground.Necks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundNecksTest::RunTest(const FString& Parameters)
{
	// passages are at least two cells wide: no passage hexagon has void on both neighbors K and K + 3
	for (FUndergroundCase& Case : MakeCases())
	{
		const TArray<FHexSphereCell>& Cells = Case.Grid.GetCells();
		const TArray<int8>& Levels = Case.Result.Levels;
		int32 Necks = 0;
		int32 FirstNeck = INDEX_NONE;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			if (!HexPlanetUnderground::IsPassage(Levels[Cell]))
			{
				continue;
			}
			bool bNeck = Cells[Cell].IsPentagon() && HexPlanetUnderground::IsNeck(Case.Grid, Levels, Cell);
			for (int32 Side = 0; Side < 3 && !Cells[Cell].IsPentagon(); ++Side)
			{
				bNeck |= !HexPlanetUnderground::IsPassage(Levels[Cells[Cell].Neighbors[Side]]) && !HexPlanetUnderground::IsPassage(Levels[Cells[Cell].Neighbors[Side + 3]]);
			}
			Necks += bNeck ? 1 : 0;
			FirstNeck = bNeck && FirstNeck == INDEX_NONE ? Cell : FirstNeck;
		}
		TestEqual(*FString::Printf(TEXT("%s: necks (first %d): %s"), *Case.Name, FirstNeck, *Case.Result.Report.ToString()), Necks, 0);
		TestEqual(*FString::Printf(TEXT("%s: necks in the report"), *Case.Name), Case.Result.Report.Necks.Num(), Necks);

		// wider passages too
		Case.Manifest.Underground.PassageWidth = 3;
		HexPlanetUnderground::FResult Wide;
		HexPlanetUnderground::Generate(Case.Grid, Case.Surface, Case.Manifest, &Case.Result.Entrances, Wide);
		TestTrue(*FString::Printf(TEXT("%s: wide passages are good: %s"), *Case.Name, *Wide.Report.ToString()), Wide.Report.IsGood());
		TestTrue(*FString::Printf(TEXT("%s: wide passages are wider"), *Case.Name), Wide.Report.PassageCells > Case.Result.Report.PassageCells);
	}

	// the check finds a line of single cells, and a network in two pieces
	const FHexSphereGrid Grid = FHexSphereGrid::Build(12);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<int8> Surface;
	Surface.Init(0, Cells.Num());
	TArray<int8> Levels;
	Levels.Init(HexPlanetUnderground::VoidLevel, Cells.Num());
	// a straight line of six cells, each going on the way the one before came
	const int32 Start = 12;
	int32 Previous = Cells[Start].Neighbors[0];
	int32 Cell = Start;
	for (int32 Step = 0; Step < 6; ++Step)
	{
		Levels[Cell] = HexPlanetUnderground::PassageLevel;
		const FVector3d Way = Cells[Cell].Center - Cells[Previous].Center;
		int32 Next = INDEX_NONE;
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (Next == INDEX_NONE || FVector3d::DotProduct(Cells[Neighbor].Center - Cells[Cell].Center, Way) > FVector3d::DotProduct(Cells[Next].Center - Cells[Cell].Center, Way))
			{
				Next = Neighbor;
			}
		}
		Previous = Cell;
		Cell = Next;
	}
	// and a cell of the same network on the far side of the planet
	int32 Far = INDEX_NONE;
	for (int32 Each = 12; Each < Cells.Num(); ++Each)
	{
		if (Far == INDEX_NONE || FVector3d::DotProduct(Cells[Each].Center, Cells[Start].Center) < FVector3d::DotProduct(Cells[Far].Center, Cells[Start].Center))
		{
			Far = Each;
		}
	}
	Levels[Far] = HexPlanetUnderground::PassageLevel;
	const TArray<FHexEntrance> Entrances = {{Start, 0}, {Far, 0}};
	const HexPlanetUnderground::FReport Report = HexPlanetUnderground::Check(Grid, Surface, Levels, Entrances);
	TestTrue(TEXT("A line is necks"), Report.Necks.Num() >= 4);
	TestEqual(TEXT("In two pieces"), Report.Networks[0].Components, 2);
	TestFalse(TEXT("Not good"), Report.IsGood());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundLoadTest, "HexHero.Planet.Underground.Load",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundLoadTest::RunTest(const FString& Parameters)
{
	// a planet without the files of its underground generates them and writes them at once, and they read back to the same data
	const FString Folder = TEXT("UndergroundTest_DeleteMe");
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), HexPlanetFile::SavedFolder, Folder);
	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	IFileManager::Get().MakeDirectory(*Directory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};

	const FUndergroundCase Case = MakeSurface(20, 6, 0.5);
	const TArray<TCHAR> BiomeLetters = {HexPlanetUnderground::Biome::Abyss, HexPlanetUnderground::Biome::Floor, HexPlanetUnderground::Biome::Lake, HexPlanetUnderground::Biome::Lava};
	HexPlanetUnderground::FLoaded First;
	HexPlanetUnderground::Load(Folder, Case.Grid, Case.Surface, Case.Manifest, BiomeLetters, {}, First);
	TestTrue(TEXT("Generated"), First.bGeneratedLevels && First.bGeneratedBiomes && First.bPlacedEntrances);
	TestEqual(TEXT("Wrote three files"), First.Written.Num(), 3);
	TestTrue(TEXT("Entrances at the root"), IFileManager::Get().FileExists(*FPaths::Combine(Directory, HexPlanetFile::EntrancesFile)));
	TestTrue(TEXT("Levels underground"), IFileManager::Get().FileExists(*FPaths::Combine(Directory, HexPlanetFile::UndergroundFolder, HexPlanetFile::LevelsFile)));
	TestTrue(TEXT("Biomes underground"), IFileManager::Get().FileExists(*FPaths::Combine(Directory, HexPlanetFile::UndergroundFolder, HexPlanetFile::BiomesFile)));
	TestFalse(TEXT("The surface untouched"), IFileManager::Get().FileExists(*FPaths::Combine(Directory, HexPlanetFile::LevelsFile)));
	TestTrue(TEXT("Good"), First.Report.IsGood());

	HexPlanetUnderground::FLoaded Again;
	HexPlanetUnderground::Load(Folder, Case.Grid, Case.Surface, Case.Manifest, BiomeLetters, {}, Again);
	TestTrue(TEXT("Read, not generated"), !Again.bGeneratedLevels && !Again.bGeneratedBiomes && !Again.bPlacedEntrances && Again.Written.IsEmpty());
	TestTrue(TEXT("Same levels"), Again.Levels == First.Levels);
	TestTrue(TEXT("Same biomes"), Again.Biomes == First.Biomes);
	TestTrue(TEXT("Lava and lakes read back"), First.Biomes.Contains(uint8(BiomeLetters.Find(HexPlanetUnderground::Biome::Lava)))
		&& First.Biomes.Contains(uint8(BiomeLetters.Find(HexPlanetUnderground::Biome::Lake))));
	TestTrue(TEXT("Same entrances"), Again.Entrances == First.Entrances);

	// a new maze for the same entrances, once the developer asks for it
	HexPlanetUnderground::FLoadOptions NewMaze;
	NewMaze.bNewMaze = true;
	FHexPlanetManifest Changed = Case.Manifest;
	Changed.Underground.SeedOffset = 3;
	HexPlanetUnderground::Load(Folder, Case.Grid, Case.Surface, Changed, BiomeLetters, NewMaze, Again);
	TestTrue(TEXT("New maze, same entrances"), Again.bGeneratedLevels && !Again.bPlacedEntrances && Again.Entrances == First.Entrances && Again.Levels != First.Levels);
	TestEqual(TEXT("Wrote the maze"), Again.Written.Num(), 2);

	// a file that is there but not valid is left alone
	const FString LevelsPath = FPaths::Combine(Directory, HexPlanetFile::UndergroundFolder, HexPlanetFile::LevelsFile);
	FFileHelper::SaveStringToFile(TEXT("VVX"), *LevelsPath);
	AddExpectedError(TEXT("which is not a level"), EAutomationExpectedErrorFlags::Contains, 1);
	HexPlanetUnderground::Load(Folder, Case.Grid, Case.Surface, Case.Manifest, BiomeLetters, {}, Again);
	FString Kept;
	FFileHelper::LoadFileToString(Kept, *LevelsPath);
	TestTrue(TEXT("Not valid, generated"), Again.bGeneratedLevels && Again.Levels == First.Levels);
	TestEqual(TEXT("Not valid, left alone"), Kept, FString(TEXT("VVX")));

	// a planet without a folder makes everything in memory
	HexPlanetUnderground::Load(FString(), Case.Grid, Case.Surface, Case.Manifest, BiomeLetters, {}, Again);
	TestTrue(TEXT("In memory"), Again.bGeneratedLevels && Again.Written.IsEmpty() && Again.Levels == First.Levels);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundEditTest, "HexHero.Planet.Underground.Edit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundEditTest::RunTest(const FString& Parameters)
{
	using namespace HexPlanetUnderground;

	FUndergroundCase Case = MakeSurface(32, 5, 0.5);
	Case.Manifest.Underground.EntranceCount = 12;
	Case.Manifest.Underground.NetworkCount = 4;
	Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Case.Result);
	const TArray<FHexSphereCell>& Cells = Case.Grid.GetCells();
	const TArray<int8>& Levels = Case.Result.Levels;
	TestTrue(TEXT("Generated good"), Case.Result.Report.IsGood());

	// the shortest way runs over neighbors from one end to the other, and is no longer than the steps of a search between them
	const int32 From = 100;
	const int32 To = Cells.Num() / 2;
	const TArray<int32> Path = FindPath(Case.Grid, From, To);
	TestTrue(TEXT("Path from and to"), Path.Num() > 1 && Path[0] == From && Path.Last() == To);
	bool bJoined = true;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		bJoined &= Cells[Path[Index - 1]].Neighbors.Contains(Path[Index]);
	}
	TestTrue(TEXT("Path runs over neighbors"), bJoined);
	TestEqual(TEXT("A path to itself"), FindPath(Case.Grid, From, From).Num(), 1);
	TestTrue(TEXT("No path to no cell"), FindPath(Case.Grid, From, -1).IsEmpty());

	// a passage carved along it in the void is one piece with no cell one cell wide, at width 2 and 3
	for (const int32 Width : {2, 3})
	{
		TArray<int8> Void;
		Void.Init(VoidLevel, Cells.Num());
		const TArray<int32> Carved = WidenPath(Case.Grid, Void, Path, Width);
		for (const int32 Cell : Carved)
		{
			Void[Cell] = PassageLevel;
		}
		const FReport Report = Check(Case.Grid, Case.Surface, Void, {});
		bool bPath = true;
		for (const int32 Cell : Path)
		{
			bPath &= IsPassage(Void[Cell]);
		}
		TestTrue(*FString::Printf(TEXT("Width %d: the way is a passage"), Width), bPath);
		TestEqual(*FString::Printf(TEXT("Width %d: no necks"), Width), Report.Necks.Num(), 0);
		TestTrue(*FString::Printf(TEXT("Width %d: wider than the way"), Width), Carved.Num() > Path.Num() + (Width - 2) * Path.Num());
		TestTrue(*FString::Printf(TEXT("Width %d: carves only void"), Width), WidenPath(Case.Grid, Void, Path, Width).IsEmpty());
	}

	// a network cut in two by void, and joined to another, is carved again whole and apart, and every other passage stays as it was
	int32 Network = INDEX_NONE;
	for (int32 Each = 0; Each < HexPlanetFile::MaxNetworks && Network == INDEX_NONE; ++Each)
	{
		Network = Case.Result.Report.Networks[Each].Entrances >= 2 ? Each : INDEX_NONE;
	}
	TestTrue(TEXT("A network with entrances"), Network != INDEX_NONE);
	if (Network == INDEX_NONE)
	{
		return true;
	}
	TArray<int8> Broken = Levels;
	int32 Cut = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		// every third cell of the network, but its exits, which keeps no piece whole
		if (Case.Result.Report.CellNetworks[Cell] == Network && Cell % 3 == 0 && !Case.Result.Entrances.ContainsByPredicate([Cell](const FHexEntrance& Each) { return Each.Cell == Cell; }))
		{
			Broken[Cell] = VoidLevel;
			++Cut;
		}
	}
	const FReport BrokenReport = Check(Case.Grid, Case.Surface, Broken, Case.Result.Entrances);
	AddInfo(FString::Printf(TEXT("Network %c cut in %d cells: %s"), HexPlanetFile::GetNetworkLetter(Network), Cut, *BrokenReport.ToString()));
	TestTrue(TEXT("Cut in pieces"), BrokenReport.Networks[Network].Components > 1);

	FResult Again;
	RegenerateNetwork(Case.Grid, Case.Surface, Case.Manifest, Case.Result.Entrances, Broken, Network, Again);
	AddInfo(FString::Printf(TEXT("Network %c generated again in %.1f ms: %s"), HexPlanetFile::GetNetworkLetter(Network), Again.Seconds * 1000.0, *Again.Report.ToString()));
	TestEqual(TEXT("Again one piece"), Again.Report.Networks[Network].Components, 1);
	TestTrue(TEXT("Again apart"), Again.Report.Merged.IsEmpty());
	TestEqual(TEXT("Again no necks"), Again.Report.Necks.Num(), 0);
	TestEqual(TEXT("Again no leftovers"), Again.Report.OrphanCells, 0);
	int32 OthersChanged = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const int8 Before = Case.Result.Report.CellNetworks[Cell];
		OthersChanged += Before >= 0 && Before != Network && Again.Levels[Cell] != Levels[Cell] ? 1 : 0;
	}
	TestEqual(TEXT("Other networks stay"), OthersChanged, 0);

	// and the same again gives the same
	FResult Twice;
	RegenerateNetwork(Case.Grid, Case.Surface, Case.Manifest, Case.Result.Entrances, Broken, Network, Twice);
	TestTrue(TEXT("Deterministic"), Twice.Levels == Again.Levels);

	// a new layout of the Links tab (UHexPlanetPainter::NewLayout): the next seed offset carves another maze for the same entrances, the same
	// each time, and offset 0 gives back the maze of the start
	{
		FHexPlanetManifest NextOffset = Case.Manifest;
		NextOffset.Underground.SeedOffset = Case.Manifest.Underground.SeedOffset + 1;
		FResult Layout;
		FResult LayoutAgain;
		FResult Back;
		Generate(Case.Grid, Case.Surface, NextOffset, &Case.Result.Entrances, Layout);
		Generate(Case.Grid, Case.Surface, NextOffset, &Case.Result.Entrances, LayoutAgain);
		Generate(Case.Grid, Case.Surface, Case.Manifest, &Case.Result.Entrances, Back);
		TestTrue(TEXT("New layout, another maze"), Layout.Levels != Levels);
		TestTrue(TEXT("New layout, same entrances"), !Layout.bPlacedEntrances && Layout.Entrances == Case.Result.Entrances);
		TestTrue(TEXT("New layout, deterministic"), LayoutAgain.Levels == Layout.Levels && LayoutAgain.Biomes == Layout.Biomes);
		TestTrue(TEXT("New layout, good"), Layout.Report.IsGood());
		TestTrue(TEXT("The old offset, the old maze"), Back.Levels == Levels);
	}

	// an exit painted into the void comes back with its hall
	TArray<int8> NoExit = Levels;
	const FHexEntrance* Exit = Case.Result.Entrances.FindByPredicate([Network](const FHexEntrance& Each) { return Each.Network == Network; });
	NoExit[Exit->Cell] = VoidLevel;
	for (const int32 Neighbor : Cells[Exit->Cell].Neighbors)
	{
		NoExit[Neighbor] = VoidLevel;
	}
	RegenerateNetwork(Case.Grid, Case.Surface, Case.Manifest, Case.Result.Entrances, NoExit, Network, Again);
	TestTrue(TEXT("Exit back on a passage"), IsPassage(Again.Levels[Exit->Cell]) && Again.Report.ExitsInVoid.IsEmpty());
	TestEqual(TEXT("Exit network one piece"), Again.Report.Networks[Network].Components, 1);

	// an entrance of a new network whose exit lies in the void beside a passage of another, far from that one's exits: its exit is always carved,
	// so the other network is carved again with it, and they end apart
	const int32 NewNetwork = HexPlanetFile::MaxNetworks - 1;
	TestEqual(TEXT("A network without entrances"), Case.Result.Report.Networks[NewNetwork].Entrances, 0);
	int32 Beside = INDEX_NONE;
	for (int32 Cell = 0; Cell < Cells.Num() && Beside == INDEX_NONE; ++Cell)
	{
		if (IsPassage(Levels[Cell]) || !CanHoldEntrance(Case.Grid, Case.Surface, Cell)
			|| !Cells[Cell].Neighbors.ContainsByPredicate([&](int32 Neighbor) { return Case.Result.Report.CellNetworks[Neighbor] == Network; }))
		{
			continue;
		}
		bool bFar = true;
		for (const FHexEntrance& Each : Case.Result.Entrances)
		{
			bFar &= FindPath(Case.Grid, Cell, Each.Cell).Num() > 5;
		}
		Beside = bFar ? Cell : INDEX_NONE;
	}
	TestTrue(TEXT("A cell beside the network"), Beside != INDEX_NONE);
	if (Beside != INDEX_NONE)
	{
		TArray<FHexEntrance> More = Case.Result.Entrances;
		More.Add({Beside, NewNetwork});
		TestEqual(TEXT("The new exit is in the void"), Check(Case.Grid, Case.Surface, Levels, More).ExitsInVoid.Num(), 1);
		RegenerateNetwork(Case.Grid, Case.Surface, Case.Manifest, More, Levels, NewNetwork, Again);
		AddInfo(FString::Printf(TEXT("New network %c beside %c at cell %d: %s"), HexPlanetFile::GetNetworkLetter(NewNetwork), HexPlanetFile::GetNetworkLetter(Network), Beside,
			*Again.Report.ToString()));
		TestTrue(TEXT("New network apart"), Again.Report.Merged.IsEmpty() && Again.Report.ExitsInVoid.IsEmpty());
		TestEqual(TEXT("New network one piece"), Again.Report.Networks[NewNetwork].Components, 1);
		TestEqual(TEXT("Its neighbor one piece"), Again.Report.Networks[Network].Components, 1);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundVoidStylesTest, "HexHero.Planet.Underground.VoidStyles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundVoidStylesTest::RunTest(const FString& Parameters)
{
	using namespace HexPlanetUnderground;

	// the floor on every passage, and on the voids the abyss, lava or a lake, these only near passages and away from the exits, by their shares
	FUndergroundCase Case = MakeSurface(32, 3, 0.35);
	Generate(Case.Grid, Case.Surface, Case.Manifest, nullptr, Case.Result);
	const TArray<FHexSphereCell>& Cells = Case.Grid.GetCells();
	const TArray<int8>& Levels = Case.Result.Levels;
	const TArray<FHexEntrance>& Entrances = Case.Result.Entrances;
	const FHexUndergroundGeneration& Generation = Case.Manifest.Underground;
	const int32 Seed = GetSeed(Case.Manifest);
	TArray<TCHAR> Letters;
	FBiomeReport Report;
	GenerateBiomes(Case.Grid, Levels, Entrances, Generation, Seed, Letters, &Report);
	AddInfo(Report.ToString());
	TestTrue(TEXT("As Generate gives them"), Letters == Case.Result.Biomes);

	TArray<int32> Passages;
	TArray<int32> Exits;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (IsPassage(Levels[Cell]))
		{
			Passages.Add(Cell);
		}
	}
	for (const FHexEntrance& Entrance : Entrances)
	{
		Exits.Add(Entrance.Cell);
	}
	const TArray<int32> PassageRings = GetRingDistances(Case.Grid, Passages, Generation.PoolRings);
	const TArray<int32> ExitRings = GetRingDistances(Case.Grid, Exits, Generation.PoolExitRings);
	int32 Candidates = 0;
	int32 Lava = 0;
	int32 Lake = 0;
	int32 WrongFloor = 0;
	int32 WrongVoid = 0;
	int32 TooFar = 0;
	int32 NearExit = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const TCHAR Letter = Letters[Cell];
		if (IsPassage(Levels[Cell]))
		{
			WrongFloor += Letter != Biome::Floor ? 1 : 0;
			continue;
		}
		WrongVoid += Letter != Biome::Abyss && Letter != Biome::Lava && Letter != Biome::Lake ? 1 : 0;
		const bool bCandidate = PassageRings[Cell] <= Generation.PoolRings && ExitRings[Cell] > Generation.PoolExitRings;
		Candidates += bCandidate ? 1 : 0;
		if (Letter == Biome::Lava || Letter == Biome::Lake)
		{
			Lava += Letter == Biome::Lava ? 1 : 0;
			Lake += Letter == Biome::Lake ? 1 : 0;
			TooFar += PassageRings[Cell] > Generation.PoolRings ? 1 : 0;
			NearExit += ExitRings[Cell] <= Generation.PoolExitRings ? 1 : 0;
		}
	}
	TestEqual(TEXT("Floor on every passage"), WrongFloor, 0);
	TestEqual(TEXT("Only A, L or K on voids"), WrongVoid, 0);
	TestEqual(TEXT("Lava and lakes near passages"), TooFar, 0);
	TestEqual(TEXT("Lava and lakes away from exits"), NearExit, 0);
	TestEqual(TEXT("Candidates as reported"), Report.Candidates, Candidates);
	TestTrue(TEXT("Some candidates"), Candidates > 200);
	TestTrue(*FString::Printf(TEXT("Lava %d of %d candidates at %.3f"), Lava, Candidates, Generation.LavaShare),
		FMath::Abs(Lava - FMath::RoundToInt32(Generation.LavaShare * Candidates)) <= 1 && Lava > 0);
	TestTrue(*FString::Printf(TEXT("Lakes %d of %d candidates at %.3f"), Lake, Candidates, Generation.LakeShare),
		FMath::Abs(Lake - FMath::RoundToInt32(Generation.LakeShare * Candidates)) <= 1 && Lake > 0);
	TestTrue(TEXT("Counts as reported"), Report.LavaCells == Lava && Report.LakeCells == Lake);
	TestTrue(TEXT("In patches"), Report.LavaPatches > 0 && Report.LavaPatches < Lava && Report.LakePatches > 0 && Report.LakePatches < Lake);
	TestTrue(TEXT("Patch centers"), Letters.IsValidIndex(Report.LavaCenter) && Letters[Report.LavaCenter] == Biome::Lava
		&& Letters.IsValidIndex(Report.LakeCenter) && Letters[Report.LakeCenter] == Biome::Lake);

	// deterministic, and another seed places them elsewhere
	TArray<TCHAR> Again;
	GenerateBiomes(Case.Grid, Levels, Entrances, Generation, Seed, Again);
	TestTrue(TEXT("Same seed, same biomes"), Again == Letters);
	GenerateBiomes(Case.Grid, Levels, Entrances, Generation, Seed + 1, Again);
	TestFalse(TEXT("Another seed, other biomes"), Again == Letters);
	FHexUndergroundGeneration None = Generation;
	None.LavaShare = 0.0;
	None.LakeShare = 0.0;
	GenerateBiomes(Case.Grid, Levels, Entrances, None, Seed, Again);
	TestFalse(TEXT("Shares of 0, only floor and abyss"), Again.Contains(Biome::Lava) || Again.Contains(Biome::Lake));

	// the manifest keeps the five fields and their defaults for missing ones
	FHexPlanetManifest Manifest;
	FString Error;
	const FHexUndergroundGeneration Defaults;
	TestTrue(TEXT("Manifest without them"), HexPlanetFile::ParseManifest(TEXT("{\"version\": 1, \"frequency\": 32, \"underground\": {\"wiggle\": 2.0}}"), Manifest, Error)
		&& Manifest.Underground.LavaShare == Defaults.LavaShare && Manifest.Underground.LakeShare == Defaults.LakeShare && Manifest.Underground.PoolRings == Defaults.PoolRings
		&& Manifest.Underground.PoolCells == Defaults.PoolCells && Manifest.Underground.PoolExitRings == Defaults.PoolExitRings);
	TestTrue(TEXT("Manifest with them"), HexPlanetFile::ParseManifest(
		TEXT("{\"version\": 1, \"frequency\": 32, \"underground\": {\"lavaShare\": 0.05, \"lakeShare\": 0.1, \"poolRings\": 2, \"poolCells\": 4.0, \"poolExitRings\": 3}}"), Manifest, Error)
		&& Manifest.Underground.LavaShare == 0.05 && Manifest.Underground.LakeShare == 0.1 && Manifest.Underground.PoolRings == 2 && Manifest.Underground.PoolCells == 4.0
		&& Manifest.Underground.PoolExitRings == 3);

	// biomes alone again: biomes.txt is written anew, levels.txt and entrances.json stay byte for byte
	const FString Folder = TEXT("UndergroundBiomesTest_DeleteMe");
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), HexPlanetFile::SavedFolder, Folder);
	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	IFileManager::Get().MakeDirectory(*Directory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};
	const TArray<TCHAR> BiomeLetters = {Biome::Abyss, Biome::Floor, Biome::Lake, Biome::Lava};
	FLoaded First;
	Load(Folder, Case.Grid, Case.Surface, Case.Manifest, BiomeLetters, {}, First);
	const FString LevelsPath = FPaths::Combine(Directory, HexPlanetFile::UndergroundFolder, HexPlanetFile::LevelsFile);
	const FString BiomesPath = FPaths::Combine(Directory, HexPlanetFile::UndergroundFolder, HexPlanetFile::BiomesFile);
	const FString EntrancesPath = FPaths::Combine(Directory, HexPlanetFile::EntrancesFile);
	FString LevelsBefore, EntrancesBefore, BiomesBefore;
	FFileHelper::LoadFileToString(LevelsBefore, *LevelsPath);
	FFileHelper::LoadFileToString(EntrancesBefore, *EntrancesPath);
	FFileHelper::LoadFileToString(BiomesBefore, *BiomesPath);
	FHexPlanetManifest More = Case.Manifest;
	More.Underground.LavaShare = 0.05;
	FLoadOptions NewBiomes;
	NewBiomes.bNewBiomes = true;
	FLoaded Again2;
	Load(Folder, Case.Grid, Case.Surface, More, BiomeLetters, NewBiomes, Again2);
	FString LevelsAfter, EntrancesAfter, BiomesAfter;
	FFileHelper::LoadFileToString(LevelsAfter, *LevelsPath);
	FFileHelper::LoadFileToString(EntrancesAfter, *EntrancesPath);
	FFileHelper::LoadFileToString(BiomesAfter, *BiomesPath);
	TestTrue(TEXT("Biomes generated, maze read"), Again2.bGeneratedBiomes && !Again2.bGeneratedLevels && !Again2.bPlacedEntrances);
	TestEqual(TEXT("Wrote biomes.txt alone"), Again2.Written.Num(), 1);
	TestTrue(TEXT("levels.txt as it was"), LevelsAfter == LevelsBefore && !LevelsBefore.IsEmpty());
	TestTrue(TEXT("entrances.json as it was"), EntrancesAfter == EntrancesBefore && !EntrancesBefore.IsEmpty());
	TestTrue(TEXT("biomes.txt anew, with more lava"), BiomesAfter != BiomesBefore && Again2.BiomeReport.LavaCells > First.BiomeReport.LavaCells);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundSeedOffsetTextTest, "HexHero.Planet.Underground.SeedOffsetText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundSeedOffsetTextTest::RunTest(const FString& Parameters)
{
	// a new layout changes only the seed offset in planet.json, in the style of the file, so Git sees one line or one block change
	const FString Spaces = TEXT("{\n  \"version\": 1,\n  \"name\": \"Terra\",\n  \"frequency\": 80,\n  \"generation\": {\n    \"start\": \"Random\",\n    \"landShare\": 0.558233\n  }\n}\n");
	FString Edited;
	TestTrue(TEXT("Text without a block read"), HexPlanetFile::SetSeedOffsetText(Spaces, 3, Edited));
	TestEqual(TEXT("Block added in its style"), Edited, FString(TEXT("{\n  \"version\": 1,\n  \"name\": \"Terra\",\n  \"frequency\": 80,\n  \"generation\": {\n    \"start\": \"Random\",\n    \"landShare\": 0.558233\n  },\n  \"underground\": {\n    \"seedOffset\": 3\n  }\n}\n")));
	FHexPlanetManifest Before;
	FHexPlanetManifest After;
	FString Error;
	TestTrue(TEXT("Both read"), HexPlanetFile::ParseManifest(Spaces, Before, Error) && HexPlanetFile::ParseManifest(Edited, After, Error));
	TestEqual(TEXT("Offset read back"), After.Underground.SeedOffset, 3);
	TestEqual(TEXT("Rest kept"), After.Generation.LandShare, Before.Generation.LandShare);
	FString Zero;
	TestTrue(TEXT("Offset 0 on a text without one"), HexPlanetFile::SetSeedOffsetText(Spaces, 0, Zero) && Zero.Equals(Spaces, ESearchCase::CaseSensitive));
	FString Back;
	TestTrue(TEXT("Offset 0 again"), HexPlanetFile::SetSeedOffsetText(Edited, 0, Back));
	TestTrue(TEXT("Number alone changed"), Back.Equals(Edited.Replace(TEXT("\"seedOffset\": 3"), TEXT("\"seedOffset\": 0")), ESearchCase::CaseSensitive));

	// the style of FJsonObjectConverter, tabs and an object on the line after its key, with a block and other fields in it
	const FString Tabs = TEXT("{\r\n\t\"version\": 1,\r\n\t\"generation\":\r\n\t{\r\n\t\t\"start\": \"Random\"\r\n\t},\r\n\t\"underground\":\r\n\t{\r\n\t\t\"lavaShare\": 0.01\r\n\t}\r\n}");
	TestTrue(TEXT("Member added to the block"), HexPlanetFile::SetSeedOffsetText(Tabs, 2, Edited)
		&& Edited.Equals(TEXT("{\r\n\t\"version\": 1,\r\n\t\"generation\":\r\n\t{\r\n\t\t\"start\": \"Random\"\r\n\t},\r\n\t\"underground\":\r\n\t{\r\n\t\t\"lavaShare\": 0.01,\r\n\t\t\"seedOffset\": 2\r\n\t}\r\n}"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Number replaced"), HexPlanetFile::SetSeedOffsetText(Edited, -7, Back) && Back.Equals(Edited.Replace(TEXT("\"seedOffset\": 2"), TEXT("\"seedOffset\": -7")), ESearchCase::CaseSensitive));
	const FString Tabs2 = TEXT("{\n\t\"version\": 1,\n\t\"underground\": {}\n}");
	TestTrue(TEXT("Empty block filled"), HexPlanetFile::SetSeedOffsetText(Tabs2, 5, Edited) && Edited.Equals(TEXT("{\n\t\"version\": 1,\n\t\"underground\": {\n\t\t\"seedOffset\": 5\n\t}\n}"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Read back"), HexPlanetFile::ParseManifest(Edited, After, Error) && After.Underground.SeedOffset == 5);

	TestFalse(TEXT("Not an object"), HexPlanetFile::SetSeedOffsetText(TEXT("[1, 2]"), 1, Edited));
	TestFalse(TEXT("Block not an object"), HexPlanetFile::SetSeedOffsetText(TEXT("{\"underground\": 4}"), 1, Edited));
	TestFalse(TEXT("Broken text"), HexPlanetFile::SetSeedOffsetText(TEXT("{\"version\": 1, \"name\": "), 1, Edited));
	return true;
}

#endif
