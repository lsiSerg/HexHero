// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroPath.h"
#include "HexHeroes.h"
#include "HexPlanetFile.h"
#include "HexSiteConfig.h"
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

#endif
