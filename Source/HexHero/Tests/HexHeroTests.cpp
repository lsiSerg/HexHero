// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHero.h"
#include "HexCastles.h"
#include "HexHeroConfig.h"
#include "HexHeroPath.h"
#include "HexHeroes.h"
#include "HexPlanetFile.h"
#include "HexShroud.h"
#include "HexSiteConfig.h"
#include "HexSiteGenerator.h"
#include "HexSphereGrid.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Returns the sum of the costs of the steps of a way from a cell, as the field should have found it */
	float SumPath(const FHexSphereGrid& Grid, int32 Start, TConstArrayView<int32> Path, HexHeroPath::FStepCost GetStepCost, bool& bOutJoined)
	{
		float Sum = 0.0f;
		int32 From = Start;
		bOutJoined = true;
		for (const int32 To : Path)
		{
			const int32 Side = Grid.GetCells()[From].Neighbors.IndexOfByKey(To);
			bOutJoined &= Side != INDEX_NONE;
			Sum += Side != INDEX_NONE ? GetStepCost(From, Side, To) : 0.0f;
			From = To;
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexHeroPathTest, "HexHero.Hero.Path",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexHeroPathTest::RunTest(const FString& Parameters)
{
	const FHexSphereGrid Grid = FHexSphereGrid::Build(8);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const int32 Start = 100;

	// every step costs 1: the cost of a cell is how many steps away it lies, as a search by rings finds it
	const auto Even = [](int32, int32, int32) { return 1.0f; };
	HexHeroPath::FField Field;
	HexHeroPath::FindField(Grid, Start, 1000.0f, Even, Field);
	TArray<int32> Rings;
	Rings.Init(INDEX_NONE, Cells.Num());
	Rings[Start] = 0;
	TArray<int32> Open{Start};
	for (int32 Index = 0; Index < Open.Num(); ++Index)
	{
		for (const int32 Neighbor : Cells[Open[Index]].Neighbors)
		{
			if (Rings[Neighbor] == INDEX_NONE)
			{
				Rings[Neighbor] = Rings[Open[Index]] + 1;
				Open.Add(Neighbor);
			}
		}
	}
	int32 WrongCosts = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		WrongCosts += Field.IsReached(Cell) && FMath::IsNearlyEqual(Field.Costs[Cell], float(Rings[Cell]), 1.0e-3f) ? 0 : 1;
	}
	TestEqual(TEXT("With even steps every cell costs its distance in steps"), WrongCosts, 0);
	TestEqual(TEXT("The start costs nothing"), Field.Costs[Start], 0.0f);

	// a way holds neighbors in a row, ends on its cell and costs what the field says
	const int32 Far = Open.Last();
	TArray<int32> Path;
	bool bJoined = false;
	TestTrue(TEXT("A way leads to the farthest cell"), Field.GetPath(Far, Path));
	TestEqual(TEXT("The way is as long as the cell is far"), Path.Num(), Rings[Far]);
	TestTrue(TEXT("The way ends on its cell"), !Path.IsEmpty() && Path.Last() == Far);
	TestEqual(TEXT("The steps of the way add up to its cost"), SumPath(Grid, Start, Path, Even, bJoined), Field.Costs[Far]);
	TestTrue(TEXT("The way goes from neighbor to neighbor"), bJoined);
	TestTrue(TEXT("The way to the start is empty"), Field.GetPath(Start, Path) && Path.IsEmpty());

	// a limit of cost leaves the cells beyond it out
	HexHeroPath::FindField(Grid, Start, 3.0f, Even, Field);
	int32 WrongReach = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		WrongReach += Field.IsReached(Cell) == (Rings[Cell] <= 3) ? 0 : 1;
	}
	TestEqual(TEXT("A limit of 3 reaches just the cells within 3 steps"), WrongReach, 0);
	TestFalse(TEXT("No way leads past the limit"), Field.GetPath(Far, Path));

	// a wall around the start, but for one gap, sends every way through the gap; closed, nothing beyond is reached
	const int32 Gap = Cells[Start].Neighbors[0];
	const auto Walled = [&](int32 From, int32, int32 To)
	{
		return Rings[To] == 1 && To != Gap ? -1.0f : 1.0f;
	};
	HexHeroPath::FindField(Grid, Start, 1000.0f, Walled, Field);
	TestTrue(TEXT("The far cell is reached through the gap"), Field.GetPath(Far, Path) && Path[0] == Gap);
	for (const int32 Neighbor : Cells[Start].Neighbors)
	{
		TestEqual(TEXT("A cell of the wall is not entered"), Field.IsReached(Neighbor), Neighbor == Gap);
	}
	const auto Closed = [&](int32, int32, int32 To) { return Rings[To] == 1 ? -1.0f : 1.0f; };
	HexHeroPath::FindField(Grid, Start, 1000.0f, Closed, Field);
	int32 Reached = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		Reached += Field.IsReached(Cell) ? 1 : 0;
	}
	TestEqual(TEXT("Walled in, only the start is reached"), Reached, 1);

	// dear ground is walked around: the second ring costs 10 a step but for one cell, and the ways beyond pass that cell
	int32 Door = INDEX_NONE;
	for (int32 Cell = 0; Cell < Cells.Num() && Door == INDEX_NONE; ++Cell)
	{
		Door = Rings[Cell] == 2 ? Cell : INDEX_NONE;
	}
	const auto Swamp = [&](int32, int32, int32 To) { return Rings[To] == 2 && To != Door ? 10.0f : 1.0f; };
	HexHeroPath::FindField(Grid, Start, 1000.0f, Swamp, Field);
	TestTrue(TEXT("A way crosses the dear ring by its cheap cell"), Field.GetPath(Far, Path) && Path.Contains(Door));
	TestEqual(TEXT("Its steps add up to its cost"), SumPath(Grid, Start, Path, Swamp, bJoined), Field.Costs[Far]);
	// no neighbor offers a cheaper way than the one found
	int32 Cheaper = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		for (int32 Side = 0; Side < Cells[Cell].Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Cells[Cell].Neighbors[Side];
			Cheaper += Field.Costs[Cell] + Swamp(Cell, Side, Neighbor) < Field.Costs[Neighbor] - 1.0e-3f ? 1 : 0;
		}
	}
	TestEqual(TEXT("No step from a neighbor beats the cost of a cell"), Cheaper, 0);

	// the same search gives the same ways
	HexHeroPath::FField Again;
	HexHeroPath::FindField(Grid, Start, 1000.0f, Swamp, Again);
	TestTrue(TEXT("The search is the same in every run"), Again.Previous == Field.Previous && Again.Costs == Field.Costs);

	// a cell that is none gives an empty field
	HexHeroPath::FindField(Grid, INDEX_NONE, 1000.0f, Even, Field);
	TestFalse(TEXT("No start, no ways"), Field.IsReached(0));

	// two layers joined at the start and at the far cell: with the third ring closed on the first layer, the way to the far cell goes down
	// at the start, along the second layer and up at the far cell, and costs its steps and both ways through
	const int32 NumCells = Cells.Num();
	const float LinkCost = 2.0f;
	const auto Ringed = [&](int32 Layer, int32, int32, int32 To) { return Layer == 0 && Rings[To] == 3 ? -1.0f : 1.0f; };
	const auto Linked = [&](int32, int32 Cell) { return Cell == Start || Cell == Far ? LinkCost : -1.0f; };
	const auto Unlinked = [](int32, int32) { return -1.0f; };
	HexHeroPath::FField Both;
	HexHeroPath::FindLayerField(Grid, 0, Start, 1000.0f, Ringed, Linked, Both);
	TestEqual(TEXT("The field of two layers has a node for each cell of each"), Both.Costs.Num(), NumCells * 2);
	TestTrue(TEXT("A way leads to the far cell through the other layer"), Both.GetPath(Far, Path));
	TestEqual(TEXT("It costs its steps and both ways through"), Both.Costs[Far], float(Rings[Far]) + LinkCost * 2.0f);
	TestTrue(TEXT("It goes down at the start"), !Path.IsEmpty() && Path[0] == NumCells + Start);
	TestTrue(TEXT("It comes up at the far cell"), Path.Num() >= 2 && Path[Path.Num() - 2] == NumCells + Far && Path.Last() == Far);
	TestEqual(TEXT("It is as long as its steps and both ways through"), Path.Num(), Rings[Far] + 2);
	TestEqual(TEXT("The same cell of the other layer costs the way through"), Both.Costs[NumCells + Start], LinkCost);

	// without the ways through the other layer is not reached, nor what lies past the closed ring
	HexHeroPath::FindLayerField(Grid, 0, Start, 1000.0f, Ringed, Unlinked, Both);
	TestFalse(TEXT("Without a way through the far cell is not reached"), Both.IsReached(Far));
	TestFalse(TEXT("Nor is the other layer"), Both.IsReached(NumCells + Start));

	// where the first layer is open the way stays on it, as two ways through cost more, and its costs are those of the search of one layer
	const auto Open2 = [](int32, int32, int32, int32) { return 1.0f; };
	HexHeroPath::FindLayerField(Grid, 0, Start, 1000.0f, Open2, Linked, Both);
	HexHeroPath::FindField(Grid, Start, 1000.0f, Even, Field);
	int32 WrongLayerCosts = 0;
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		WrongLayerCosts += FMath::IsNearlyEqual(Both.Costs[Cell], Field.Costs[Cell], 1.0e-3f) ? 0 : 1;
	}
	TestEqual(TEXT("On open ground the costs of the first layer are those of one layer"), WrongLayerCosts, 0);
	TestTrue(TEXT("And the way to the far cell stays on it"), Both.GetPath(Far, Path) && !Path.ContainsByPredicate([NumCells](int32 Node) { return Node >= NumCells; }));

	// a search from the second layer starts at its node
	HexHeroPath::FindLayerField(Grid, 1, Start, 1000.0f, Open2, Linked, Both);
	TestEqual(TEXT("A search from the second layer starts at its node"), Both.Start, NumCells + Start);
	TestEqual(TEXT("And reaches the first through the way at its start"), Both.Costs[Start], LinkCost);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexHeroSpendTest, "HexHero.Hero.Spend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexHeroSpendTest::RunTest(const FString& Parameters)
{
	using namespace HexHeroPath;

	TestTrue(TEXT("Points pay for a step of their cost"), CanStep(2.5f, 2.5f, 10.0f));
	TestFalse(TEXT("Too few points do not"), CanStep(2.5f, 2.4f, 10.0f));
	TestTrue(TEXT("A step dearer than a day takes a whole day"), CanStep(12.0f, 10.0f, 10.0f));
	TestFalse(TEXT("But not a day already begun"), CanStep(12.0f, 9.0f, 10.0f));

	FSpend Spend = HexHeroPath::Spend({}, 10.0f, 10.0f);
	TestTrue(TEXT("No steps take no days"), Spend.Days == 0 && Spend.StepsToday == 0 && Spend.Total == 0.0f);

	// ten steps of 1 with 10 points end today, eleven take a second day
	TArray<float> Costs;
	Costs.Init(1.0f, 10);
	Spend = HexHeroPath::Spend(Costs, 10.0f, 10.0f);
	TestTrue(TEXT("Ten steps of 1 with 10 points end today"), Spend.Days == 1 && Spend.StepsToday == 10 && FMath::IsNearlyEqual(Spend.Total, 10.0f));
	Costs.Add(1.0f);
	Spend = HexHeroPath::Spend(Costs, 10.0f, 10.0f);
	TestTrue(TEXT("Eleven take a second day"), Spend.Days == 2 && Spend.StepsToday == 10);

	// with 3 points left, three steps today and the rest tomorrow
	Spend = HexHeroPath::Spend(Costs, 3.0f, 10.0f);
	TestTrue(TEXT("3 points pay for three steps today"), Spend.Days == 2 && Spend.StepsToday == 3);

	// what is left over is lost with the day: 4 points do not pay for a step of 2.5 after one of 2.5
	Spend = HexHeroPath::Spend({2.5f, 2.5f, 1.0f}, 4.0f, 10.0f);
	TestTrue(TEXT("Points left over are lost with the day"), Spend.Days == 2 && Spend.StepsToday == 1 && FMath::IsNearlyEqual(Spend.Total, 6.0f));

	// no points today: nothing today, and the days count from tomorrow
	Spend = HexHeroPath::Spend({1.0f, 1.0f}, 0.0f, 10.0f);
	TestTrue(TEXT("Without points the way begins tomorrow"), Spend.Days == 2 && Spend.StepsToday == 0);

	// a step dearer than a day takes a day of its own
	Spend = HexHeroPath::Spend({12.0f, 1.0f}, 10.0f, 10.0f);
	TestTrue(TEXT("A step dearer than a day takes that day"), Spend.Days == 2 && Spend.StepsToday == 1);

	// 25 steps of 1 take three days
	Costs.Init(1.0f, 25);
	Spend = HexHeroPath::Spend(Costs, 10.0f, 10.0f);
	TestTrue(TEXT("25 steps take three days"), Spend.Days == 3 && Spend.StepsToday == 10);

	// the day of each step, by which a way shows where each day ends: with 3 points left, three steps today, ten tomorrow, the rest after
	TArray<int32> Days;
	FindStepDays(Costs, 3.0f, 10.0f, Days);
	TestEqual(TEXT("A day for each step"), Days.Num(), 25);
	TestTrue(TEXT("The third step is still today and the fourth tomorrow"), Days[2] == 1 && Days[3] == 2);
	TestTrue(TEXT("The thirteenth ends the second day and the last step falls on the fourth"), Days[12] == 2 && Days[13] == 3 && Days.Last() == 4);
	int32 Backwards = 0;
	for (int32 Index = 1; Index < Days.Num(); ++Index)
	{
		Backwards += Days[Index] < Days[Index - 1] ? 1 : 0;
	}
	TestEqual(TEXT("The days never go back along a way"), Backwards, 0);
	FindStepDays({}, 3.0f, 10.0f, Days);
	TestTrue(TEXT("No steps have no days"), Days.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexHeroFileTest, "HexHero.Hero.File",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexHeroFileTest::RunTest(const FString& Parameters)
{
	TArray<FHexHeroLine> Heroes;
	FHexHeroLine& Second = Heroes.AddDefaulted_GetRef();
	Second.Id = 7;
	Second.Name = TEXT("Mavka \"the Quiet\" d'Or \\ II");
	Second.Class = TEXT("Wizard");
	Second.Player = 2;
	Second.Layer = TEXT("Underground");
	Second.Cell = 4242;
	FHexHeroLine& First = Heroes.AddDefaulted_GetRef();
	First.Id = 1;
	First.Name = TEXT("Bohdan");
	First.Class = TEXT("Knight");
	First.Player = 0;
	First.Layer = TEXT("Surface");
	First.Cell = 17;

	// a line per hero, sorted by Id, so Git merges it
	const FString Text = HexPlanetFile::EncodeHeroes(Heroes);
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("A line per hero between the brackets"), Lines.Num(), 7);
	TestTrue(TEXT("The lowest Id comes first"), Lines.IsValidIndex(4) && Lines[3].Contains(TEXT("\"id\": 1,")) && Lines[4].Contains(TEXT("\"id\": 7,")));

	TArray<FHexHeroLine> Read;
	FString Error;
	TestTrue(TEXT("The text reads back"), HexPlanetFile::DecodeHeroes(Text, Read, Error));
	TestTrue(TEXT("With the same heroes, a name with quotes, an apostrophe and a backslash too"), Read.Num() == 2 && Read[0] == Heroes[1] && Read[1] == Heroes[0]);
	TestEqual(TEXT("Written again it is the same text"), HexPlanetFile::EncodeHeroes(Read), Text);

	// the players the game moves by itself are a line of their own, there only when there are any, sorted
	TArray<int32> Ai = {7, 5};
	TestTrue(TEXT("Nobody is moved by the game in a file without that line"), HexPlanetFile::DecodeHeroes(Text, Read, Error, &Ai) && Ai.IsEmpty());
	const FString WithAi = HexPlanetFile::EncodeHeroes(Heroes, {3, 1});
	TArray<FString> AiLines;
	WithAi.ParseIntoArrayLines(AiLines);
	TestEqual(TEXT("One line more for those players"), AiLines.Num(), 8);
	TestTrue(TEXT("Sorted"), AiLines.IsValidIndex(2) && AiLines[2].Contains(TEXT("\"ai\": [1, 3],")));
	TestTrue(TEXT("They read back"), HexPlanetFile::DecodeHeroes(WithAi, Read, Error, &Ai) && Ai == TArray<int32>({1, 3}) && Read.Num() == 2);
	TestEqual(TEXT("Written again it is the same text"), HexPlanetFile::EncodeHeroes(Read, Ai), WithAi);

	TestTrue(TEXT("No heroes read back as none"), HexPlanetFile::DecodeHeroes(HexPlanetFile::EncodeHeroes({}), Read, Error) && Read.IsEmpty());
	TestFalse(TEXT("Text that is no JSON is refused"), HexPlanetFile::DecodeHeroes(TEXT("heroes"), Read, Error));

	// the classes and players a hero falls back to are there, and found by their names
	const UHexHeroConfig* Config = GetDefault<UHexHeroConfig>();
	TestTrue(TEXT("The config has classes and players"), Config->Classes.Num() >= 2 && Config->Players.Num() >= 2);
	TestEqual(TEXT("A class is found by its name, any case"), Config->FindClass(TEXT("wizard")), 1);
	TestEqual(TEXT("A name that is no class is none"), Config->FindClass(TEXT("Dragon")), int32(INDEX_NONE));
	for (const FHexHeroClass& Class : Config->Classes)
	{
		TestTrue(*FString::Printf(TEXT("The class %s has names for its heroes"), *Class.DisplayName), !Class.Names.IsEmpty() && !Class.Id.IsNone());
	}
	TestTrue(TEXT("A hero has points of movement, and lights within what the materials take"), Config->MovePoints >= 1.0f && Config->MaxLights <= MaxHeroLights);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexHeroSitesTest, "HexHero.Hero.Sites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexHeroSitesTest::RunTest(const FString& Parameters)
{
	// sites.json: a line per site, sorted by Id, read back as written
	TArray<FHexSiteLine> Sites;
	FHexSiteLine& Second = Sites.AddDefaulted_GetRef();
	Second.Id = 5;
	Second.Kind = TEXT("CrystalCave");
	Second.Layer = TEXT("Underground");
	Second.Cell = 812;
	FHexSiteLine& First = Sites.AddDefaulted_GetRef();
	First.Id = 2;
	First.Kind = TEXT("Sawmill");
	First.Layer = TEXT("Surface");
	First.Cell = 44;
	const FString Text = HexPlanetFile::EncodeSites(Sites);
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("A line per site between the brackets"), Lines.Num(), 7);
	TestTrue(TEXT("The lowest Id comes first"), Lines.IsValidIndex(4) && Lines[3].Contains(TEXT("\"id\": 2,")) && Lines[4].Contains(TEXT("\"id\": 5,")));
	TArray<FHexSiteLine> Read;
	FString Error;
	TestTrue(TEXT("The text reads back"), HexPlanetFile::DecodeSites(Text, Read, Error));
	TestTrue(TEXT("With the same sites"), Read.Num() == 2 && Read[0] == Sites[1] && Read[1] == Sites[0]);
	TestEqual(TEXT("Written again it is the same text"), HexPlanetFile::EncodeSites(Read), Text);
	TestFalse(TEXT("Text that is no JSON is refused"), HexPlanetFile::DecodeSites(TEXT("sites"), Read, Error));

	// the resources and sites of the developer's document: seven resources, a mine and a pile of each, and a mill
	const UHexSiteConfig* Config = GetDefault<UHexSiteConfig>();
	TestEqual(TEXT("Seven resources"), Config->Resources.Num(), 7);
	const TMap<FName, int32> Incomes = {{TEXT("Gold"), 1000}, {TEXT("Wood"), 2}, {TEXT("Ore"), 2}, {TEXT("Mercury"), 1}, {TEXT("Sulfur"), 1}, {TEXT("Crystal"), 1}, {TEXT("Gems"), 1}};
	// a player starts with 2 days of a gold mine, 4 of a basic one and 2 of a rare one
	const TMap<FName, int32> StartDays = {{TEXT("Gold"), 2}, {TEXT("Wood"), 4}, {TEXT("Ore"), 4}, {TEXT("Mercury"), 2}, {TEXT("Sulfur"), 2}, {TEXT("Crystal"), 2}, {TEXT("Gems"), 2}};
	int32 Mills = 0;
	for (int32 Resource = 0; Resource < Config->Resources.Num(); ++Resource)
	{
		const FHexResourceInfo& Info = Config->Resources[Resource];
		int32 Mines = 0;
		int32 Piles = 0;
		for (const FHexSiteKind& Kind : Config->Kinds)
		{
			Mines += Kind.Type == EHexSiteType::Mine && Kind.Resource == Info.Id ? 1 : 0;
			Piles += Kind.Type == EHexSiteType::Pile && Kind.Resource == Info.Id && Kind.AmountMin > 0 && Kind.AmountMax >= Kind.AmountMin ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("One mine of %s"), *Info.DisplayName), Mines, 1);
		TestEqual(*FString::Printf(TEXT("One pile of %s"), *Info.DisplayName), Piles, 1);
		TestEqual(*FString::Printf(TEXT("The mine of %s gives what the document says"), *Info.DisplayName), Config->GetMineIncome(Resource), Incomes.FindRef(Info.Id));
		TestEqual(*FString::Printf(TEXT("A player starts with the days of %s the developer chose"), *Info.DisplayName), Info.StartAmount, Incomes.FindRef(Info.Id) * StartDays.FindRef(Info.Id));
	}
	TSet<FName> Ids;
	for (const FHexSiteKind& Kind : Config->Kinds)
	{
		Mills += Kind.Type == EHexSiteType::Mill ? 1 : 0;
		TestTrue(*FString::Printf(TEXT("The kind %s has a name of its own"), *Kind.DisplayName), !Kind.Id.IsNone() && !Ids.Contains(Kind.Id));
		Ids.Add(Kind.Id);
		TestTrue(*FString::Printf(TEXT("The kind %s is found by its name"), *Kind.DisplayName), Config->Kinds.IsValidIndex(Config->FindKind(Kind.Id.ToString().ToLower())));
	}
	TestEqual(TEXT("A mill"), Mills, 1);
	TestEqual(TEXT("A mill gives three days of a mine"), Config->MillDays, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexHeroSiteGeneratorTest, "HexHero.Hero.SiteGenerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexHeroSiteGeneratorTest::RunTest(const FString& Parameters)
{
	const FHexSphereGrid Grid = FHexSphereGrid::Build(12);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const int32 NumCells = Cells.Num();
	const UHexSiteConfig* Config = GetDefault<UHexSiteConfig>();

	// a surface with obstacles scattered over it and a sea around one pole, and an underground that is a band around the equator
	HexSiteGenerator::FInput Input;
	Input.Settings = Config->Generation;
	Input.Settings.Density = 3.0f;
	Input.Seed = 17;
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		Input.Walkable[Layer].Init(false, NumCells);
		Input.Avoid[Layer].Init(false, NumCells);
	}
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		const uint32 Hash = MurmurFinalize32(uint32(Cell) * 2654435761u + 7u);
		Input.Walkable[0][Cell] = Cells[Cell].Center.Z < 0.7 && Hash % 6 != 0;
		Input.Avoid[0][Cell] = Hash % 11 == 1;
		Input.Walkable[1][Cell] = FMath::Abs(Cells[Cell].Center.Z) < 0.25 && Hash % 9 != 0;
	}
	// two players start far apart, each on open ground
	const auto FindStart = [&](const FVector3d& Towards)
	{
		int32 Best = INDEX_NONE;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			const bool bOpen = Input.Walkable[0][Cell] && !Cells[Cell].Neighbors.ContainsByPredicate([&](int32 Neighbor) { return !Input.Walkable[0][Neighbor]; });
			if (bOpen && (Best == INDEX_NONE || FVector3d::DotProduct(Cells[Cell].Center, Towards) > FVector3d::DotProduct(Cells[Best].Center, Towards)))
			{
				Best = Cell;
			}
		}
		return Best;
	};
	const int32 Starts[2] = {FindStart(FVector3d(1.0, 0.0, 0.0)), FindStart(FVector3d(-1.0, 0.0, 0.0))};
	TArray<int32> Ring;
	for (const int32 Start : Starts)
	{
		Input.Starts.Add({0, Start});
		HexShroud::FindWithin(Grid, Start, Input.Settings.StartClearRings, Ring);
		for (const int32 Cell : Ring)
		{
			Input.Avoid[0][Cell] = true;
		}
	}
	TArray<int32> NearStartKinds;
	for (const FHexSiteShare& Share : Config->Generation.Shares)
	{
		const int32 Kind = Config->FindKind(Share.Kind.ToString());
		TestTrue(*FString::Printf(TEXT("The generation names a kind there is: %s"), *Share.Kind.ToString()), Kind != INDEX_NONE);
		if (Kind == INDEX_NONE)
		{
			continue;
		}
		HexSiteGenerator::FKind& Each = Input.Kinds.AddDefaulted_GetRef();
		Each.Kind = Kind;
		Each.Type = Config->Kinds[Kind].Type;
		Each.PerThousand[0] = Share.PerThousand;
		Each.PerThousand[1] = Share.UndergroundPerThousand;
		Each.bNearStart = Share.bNearStart;
		if (Share.bNearStart)
		{
			NearStartKinds.Add(Kind);
		}
	}
	TestTrue(TEXT("Every player gets some mine near where it starts"), !NearStartKinds.IsEmpty());

	TArray<HexSiteGenerator::FSite> Sites;
	HexSiteGenerator::FReport Report;
	HexSiteGenerator::Generate(Grid, Input, Sites, &Report);
	TArray<HexSiteGenerator::FSite> Again;
	HexSiteGenerator::Generate(Grid, Input, Again);
	const auto IsSame = [](const TArray<HexSiteGenerator::FSite>& A, const TArray<HexSiteGenerator::FSite>& B)
	{
		bool bSame = A.Num() == B.Num();
		for (int32 Index = 0; bSame && Index < A.Num(); ++Index)
		{
			bSame = A[Index].Kind == B[Index].Kind && A[Index].Layer == B[Index].Layer && A[Index].Cell == B[Index].Cell;
		}
		return bSame;
	};
	TestTrue(TEXT("The same seed gives the same sites"), IsSame(Sites, Again));
	HexSiteGenerator::FInput Other = Input;
	Other.Seed = 18;
	HexSiteGenerator::Generate(Grid, Other, Again);
	TestFalse(TEXT("Another seed gives other sites"), IsSame(Sites, Again));
	TestTrue(TEXT("Both layers get sites"), Sites.ContainsByPredicate([](const HexSiteGenerator::FSite& Each) { return Each.Layer == 0; })
		&& Sites.ContainsByPredicate([](const HexSiteGenerator::FSite& Each) { return Each.Layer == 1; }));
	TestTrue(TEXT("Many sites on a planet this dense"), Sites.Num() > 30);

	// the pieces of ground a hero walks, with the cells of the sites closed or not
	const auto CountPieces = [&](const TBitArray<>& Open)
	{
		TBitArray<> Seen(false, NumCells);
		int32 Pieces = 0;
		TArray<int32> Queue;
		for (int32 First = 0; First < NumCells; ++First)
		{
			if (!Open[First] || Seen[First])
			{
				continue;
			}
			++Pieces;
			Queue.Reset();
			Queue.Add(First);
			Seen[First] = true;
			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				for (const int32 Neighbor : Cells[Queue[Index]].Neighbors)
				{
					if (Open[Neighbor] && !Seen[Neighbor])
					{
						Seen[Neighbor] = true;
						Queue.Add(Neighbor);
					}
				}
			}
		}
		return Pieces;
	};
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		TBitArray<> Open = Input.Walkable[Layer];
		TMap<int32, int32> KindAt;
		bool bOnGround = true;
		for (const HexSiteGenerator::FSite& Site : Sites)
		{
			if (Site.Layer != Layer)
			{
				continue;
			}
			bOnGround &= Cells.IsValidIndex(Site.Cell) && Input.Walkable[Layer][Site.Cell] && !Input.Avoid[Layer][Site.Cell] && !Cells[Site.Cell].IsPentagon() && !KindAt.Contains(Site.Cell);
			KindAt.Add(Site.Cell, Site.Kind);
			Open[Site.Cell] = false;
		}
		TestTrue(*FString::Printf(TEXT("Layer %d: every site stands on a cell of its own that a hero walks and that is not to avoid"), Layer), bOnGround);
		TestEqual(*FString::Printf(TEXT("Layer %d: the sites cut no ground a hero walks in two"), Layer), CountPieces(Open), CountPieces(Input.Walkable[Layer]));
		bool bApart = true;
		bool bVisited = true;
		bool bMinesApart = true;
		for (const TPair<int32, int32>& Each : KindAt)
		{
			bool bBeside = false;
			for (const int32 Neighbor : Cells[Each.Key].Neighbors)
			{
				bApart &= !KindAt.Contains(Neighbor);
				bBeside |= Open[Neighbor];
			}
			bVisited &= bBeside;
			if (Config->Kinds[Each.Value].IsMine())
			{
				HexShroud::FindWithin(Grid, Each.Key, 3, Ring);
				for (const int32 Cell : Ring)
				{
					const int32* Near = Cell != Each.Key ? KindAt.Find(Cell) : nullptr;
					bMinesApart &= !(Near && Config->Kinds[*Near].IsMine());
				}
			}
		}
		TestTrue(*FString::Printf(TEXT("Layer %d: no two sites stand side by side"), Layer), bApart);
		TestTrue(*FString::Printf(TEXT("Layer %d: every site has a cell beside it to be visited from"), Layer), bVisited);
		TestTrue(*FString::Printf(TEXT("Layer %d: mines keep apart"), Layer), bMinesApart);
	}
	// a kind with no number underground stays on the surface
	for (const HexSiteGenerator::FKind& Kind : Input.Kinds)
	{
		if (Kind.PerThousand[1] <= 0.0f)
		{
			TestFalse(*FString::Printf(TEXT("%s stays on the surface"), *Config->Kinds[Kind.Kind].DisplayName),
				Sites.ContainsByPredicate([&Kind](const HexSiteGenerator::FSite& Each) { return Each.Layer == 1 && Each.Kind == Kind.Kind; }));
		}
	}
	// each player has its mines within reach of where it starts
	for (const int32 Start : Starts)
	{
		HexShroud::FindWithin(Grid, Start, Input.Settings.StartMaxSteps, Ring);
		for (const int32 Kind : NearStartKinds)
		{
			TestTrue(*FString::Printf(TEXT("A %s stands near the start on cell %d"), *Config->Kinds[Kind].DisplayName, Start),
				Sites.ContainsByPredicate([&](const HexSiteGenerator::FSite& Each) { return Each.Layer == 0 && Each.Kind == Kind && Ring.Contains(Each.Cell); }));
		}
		HexShroud::FindWithin(Grid, Start, Input.Settings.StartClearRings, Ring);
		TestFalse(*FString::Printf(TEXT("No site stands right at the start on cell %d"), Start),
			Sites.ContainsByPredicate([&](const HexSiteGenerator::FSite& Each) { return Each.Layer == 0 && Ring.Contains(Each.Cell); }));
	}
	TestEqual(TEXT("The sites near the starts are counted"), Report.NearStarts, 2 * (NearStartKinds.Num() + Input.Settings.StartPiles));

	// closing a cell keeps the ways while the open cells around it lie in one run
	TBitArray<> Open(true, NumCells);
	const int32 Middle = Starts[0];
	TestTrue(TEXT("A cell in open ground may be closed"), HexSiteGenerator::KeepsWays(Grid, Open, Middle));
	Open[Cells[Middle].Neighbors[0]] = false;
	Open[Cells[Middle].Neighbors[3]] = false;
	TestFalse(TEXT("A cell between two closed ones is a neck, and stays open"), HexSiteGenerator::KeepsWays(Grid, Open, Middle));
	Open[Cells[Middle].Neighbors[1]] = false;
	Open[Cells[Middle].Neighbors[2]] = false;
	TestTrue(TEXT("With one run of open cells around it, it may be closed again"), HexSiteGenerator::KeepsWays(Grid, Open, Middle));
	for (const int32 Neighbor : Cells[Middle].Neighbors)
	{
		Open[Neighbor] = false;
	}
	TestFalse(TEXT("A cell nothing reaches takes no site"), HexSiteGenerator::KeepsWays(Grid, Open, Middle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexHeroCastlesTest, "HexHero.Hero.Castles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexHeroCastlesTest::RunTest(const FString& Parameters)
{
	// two factions, each with a hall, a fort, and seven levels of creatures with a dwelling each
	const UHexCastleConfig* Config = GetDefault<UHexCastleConfig>();
	const UHexSiteConfig* Sites = GetDefault<UHexSiteConfig>();
	TestEqual(TEXT("Two factions"), Config->Factions.Num(), 2);
	for (const FHexFaction& Faction : Config->Factions)
	{
		TestEqual(*FString::Printf(TEXT("%s has seven levels of creatures"), *Faction.DisplayName), Faction.Creatures.Num(), NumCreatureLevels);
		TestTrue(*FString::Printf(TEXT("%s has a hall that stands from the start, and a fort"), *Faction.DisplayName),
			Faction.Buildings.IsValidIndex(Faction.FindBuilding(EHexBuildingType::Hall)) && Faction.Buildings[Faction.FindBuilding(EHexBuildingType::Hall)].StartLevel == 1
			&& Faction.Buildings.IsValidIndex(Faction.FindBuilding(EHexBuildingType::Fort)));
		float Power = 0.0f;
		for (int32 Level = 0; Level < Faction.Creatures.Num(); ++Level)
		{
			const FHexCreature& Creature = Faction.Creatures[Level];
			TestTrue(*FString::Printf(TEXT("%s is stronger than the level before, and its upgraded version than itself"), *Creature.Base.DisplayName),
				Creature.Base.GetPower() > Power && Creature.Upgraded.GetPower() > Creature.Base.GetPower());
			TestTrue(*FString::Printf(TEXT("%s costs gold, the upgraded one more, and comes every week"), *Creature.Base.DisplayName),
				Creature.Base.Gold > 0 && Creature.Upgraded.Gold > Creature.Base.Gold && Creature.Growth >= 1);
			Power = Creature.Base.GetPower();
			const int32 Dwelling = Faction.FindBuilding(EHexBuildingType::Dwelling, Level);
			TestTrue(*FString::Printf(TEXT("%s has a dwelling that is built and upgraded"), *Creature.Base.DisplayName),
				Faction.Buildings.IsValidIndex(Dwelling) && Faction.Buildings[Dwelling].Levels.Num() == 2);
		}
		// what is paid with is a resource there is
		for (const FHexBuilding& Building : Faction.Buildings)
		{
			for (const FHexBuildingLevel& Level : Building.Levels)
			{
				for (const TPair<FName, int32>& Cost : Level.Cost)
				{
					TestTrue(*FString::Printf(TEXT("%s is paid for with a resource there is, %s"), *Level.DisplayName, *Cost.Key.ToString()), Sites->FindResource(Cost.Key) != INDEX_NONE);
				}
			}
		}
		for (const FHexCreature& Creature : Faction.Creatures)
		{
			TestTrue(*FString::Printf(TEXT("%s is paid for with a resource there is"), *Creature.Base.DisplayName), Creature.CostResource.IsNone() || Sites->FindResource(Creature.CostResource) != INDEX_NONE);
		}
	}

	// until there are battles the stronger army wins, and loses the more the nearer the weaker one came
	float Loss = -1.0f;
	TestTrue(TEXT("A castle nobody guards is taken without a loss"), UHexCastles::Resolve(0.0f, 0.0f, 1.5f, 0.9f, Loss) && Loss == 0.0f);
	TestFalse(TEXT("A weaker attacker turns back"), UHexCastles::Resolve(90.0f, 100.0f, 1.5f, 0.9f, Loss));
	TestFalse(TEXT("And so does one as strong as the guard"), UHexCastles::Resolve(100.0f, 100.0f, 1.5f, 0.9f, Loss));
	TestTrue(TEXT("A stronger attacker wins"), UHexCastles::Resolve(400.0f, 100.0f, 1.5f, 0.9f, Loss));
	TestEqual(TEXT("And loses the share the guard was of it, to the power"), Loss, 0.125f, 0.001f);
	float Close = 0.0f;
	UHexCastles::Resolve(101.0f, 100.0f, 1.5f, 0.9f, Close);
	TestEqual(TEXT("A narrow win costs at most the largest share"), Close, 0.9f, 0.001f);

	// creatures stack, an army holds seven kinds, and losses leave one creature at least
	TArray<FHexStack> Army;
	for (int32 Creature = 0; Creature < NumArmySlots; ++Creature)
	{
		TestTrue(TEXT("A new kind takes a free slot"), UHexCastles::AddToArmy(Army, {0, Creature, false, 10}));
	}
	TestTrue(TEXT("More of a kind join its stack"), UHexCastles::AddToArmy(Army, {0, 2, false, 5}) && Army.Num() == NumArmySlots && Army[2].Count == 15);
	TestFalse(TEXT("An eighth kind finds no slot"), UHexCastles::AddToArmy(Army, {1, 0, false, 1}));
	TestFalse(TEXT("And the upgraded version is a kind of its own"), UHexCastles::AddToArmy(Army, {0, 0, true, 1}));
	UHexCastles::ApplyLosses(Army, 0.5f);
	TestTrue(TEXT("Half of every stack is lost"), Army.Num() == NumArmySlots && Army[0].Count == 5 && Army[2].Count == 8);
	UHexCastles::ApplyLosses(Army, 1.0f);
	TestTrue(TEXT("One creature of the highest level is left at least"), Army.Num() == 1 && Army[0].Creature == NumArmySlots - 1 && Army[0].Count == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexHeroShroudTest, "HexHero.Hero.Shroud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexHeroShroudTest::RunTest(const FString& Parameters)
{
	const FHexSphereGrid Grid = FHexSphereGrid::Build(8);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const int32 Start = 100;

	// what a hero sees: the cells up to a number of steps away, each once, as the search of ways counts them
	const auto Even = [](int32, int32, int32) { return 1.0f; };
	HexHeroPath::FField Field;
	HexHeroPath::FindField(Grid, Start, 1000.0f, Even, Field);
	for (const int32 Rings : {0, 1, 3, 5})
	{
		TArray<int32> Seen;
		HexShroud::FindWithin(Grid, Start, Rings, Seen);
		int32 Expected = 0;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			Expected += Field.Costs[Cell] <= float(Rings) + 0.5f ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("The cells within %d steps"), Rings), Seen.Num(), Expected);
		TestEqual(*FString::Printf(TEXT("Each cell within %d steps once"), Rings), TSet<int32>(Seen).Num(), Seen.Num());
		TestEqual(TEXT("The cell itself comes first"), Seen[0], Start);
		bool bNear = true;
		for (const int32 Cell : Seen)
		{
			bNear &= Field.Costs[Cell] <= float(Rings) + 0.5f;
		}
		TestTrue(*FString::Printf(TEXT("No cell further than %d steps"), Rings), bNear);
	}
	TArray<int32> None;
	HexShroud::FindWithin(Grid, Cells.Num(), 3, None);
	TestEqual(TEXT("No cell around a cell that is none"), None.Num(), 0);

	// each player explores for itself, on each layer, and what is explored stays so
	UHexShroud* Shroud = NewObject<UHexShroud>();
	TArray<int32> Seen;
	HexShroud::FindWithin(Grid, Start, 2, Seen);
	TestFalse(TEXT("Nothing is explored at first"), Shroud->IsExplored(0, EHexPlanetLayer::Surface, Start));
	const int32 Before = Shroud->GetRevision();
	TestTrue(TEXT("New cells are explored"), Shroud->Explore(0, EHexPlanetLayer::Surface, Seen, Cells.Num()));
	TestTrue(TEXT("Exploring counts as a change"), Shroud->GetRevision() != Before);
	TestTrue(TEXT("The cell is explored"), Shroud->IsExplored(0, EHexPlanetLayer::Surface, Start));
	TestEqual(TEXT("As many cells as were seen"), Shroud->CountExplored(0, EHexPlanetLayer::Surface), Seen.Num());
	TestFalse(TEXT("Not by another player"), Shroud->IsExplored(1, EHexPlanetLayer::Surface, Start));
	TestFalse(TEXT("Not on the other layer"), Shroud->IsExplored(0, EHexPlanetLayer::Underground, Start));
	const int32 After = Shroud->GetRevision();
	TestFalse(TEXT("The same cells again are nothing new"), Shroud->Explore(0, EHexPlanetLayer::Surface, Seen, Cells.Num()));
	TestEqual(TEXT("Which is no change"), Shroud->GetRevision(), After);
	TestFalse(TEXT("A cell that is none is not explored"), Shroud->IsExplored(0, EHexPlanetLayer::Surface, Cells.Num() + 5));
	Shroud->Reset();
	TestFalse(TEXT("A new game forgets it"), Shroud->IsExplored(0, EHexPlanetLayer::Surface, Start));

	// the field of the fog is a map of the sphere: a texel lies where its direction is looked up, and every cell has a texel of its own
	const int32 Size = 64;
	double Worst = 0.0;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const FVector3d Direction = HexShroud::GetTexelDirection(X, Y, Size);
			TestTrue(TEXT("A texel looks along a unit direction"), FMath::IsNearlyEqual(Direction.Size(), 1.0, 1.0e-9));
			const FVector2d UV = HexShroud::GetFieldUV(Direction);
			Worst = FMath::Max(Worst, FMath::Max(FMath::Abs(UV.X * Size - (X + 0.5)), FMath::Abs(UV.Y * Size - (Y + 0.5))));
		}
	}
	TestTrue(*FString::Printf(TEXT("The direction of a texel is looked up in that texel (off by %.6f of a texel)"), Worst), Worst < 1.0e-6);
	for (const FVector3d& Axis : {FVector3d(1, 0, 0), FVector3d(-1, 0, 0), FVector3d(0, 1, 0), FVector3d(0, -1, 0), FVector3d(0, 0, 1), FVector3d(0, 0, -1)})
	{
		const FVector2d UV = HexShroud::GetFieldUV(Axis);
		TestTrue(TEXT("Every axis lies in the map"), UV.X >= 0.0 && UV.X <= 1.0 && UV.Y >= 0.0 && UV.Y <= 1.0);
	}
	return true;
}

#endif
