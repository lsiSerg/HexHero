// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHero.h"
#include "HAL/FileManager.h"
#include "HexPlanetConfig.h"
#include "HexPlanetFile.h"
#include "HexPlanetGenerator.h"
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
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetUndergroundSeedTest, "HexHero.Planet.Underground.Seed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetUndergroundSeedTest::RunTest(const FString& Parameters)
{
	// the same planet gives the same files, another seed or offset another maze
	FUndergroundCase Case = MakeSurface(32, 3, 0.35);
	const TConstArrayView<FHexLevelLetter> Letters = HexPlanetFile::GetLevelLetters(EHexPlanetLayer::Underground);
	const TArray<TCHAR> BiomeLetters = {HexPlanetUnderground::Biome::Abyss, HexPlanetUnderground::Biome::Floor};
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
	const TArray<TCHAR> BiomeLetters = {HexPlanetUnderground::Biome::Abyss, HexPlanetUnderground::Biome::Floor};
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

#endif
