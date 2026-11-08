// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHero.h"
#include "HexLastView.h"
#include "HexPlanetCameraPawn.h"
#include "JsonObjectConverter.h"
#include "HexPlanetConfig.h"
#include "HexPlanetFile.h"
#include "HexPlanetGenerator.h"
#include "HexPlanetKit.h"
#include "HexPlanetKitConfig.h"
#include "HexPlanetRivers.h"
#include "HexPlanetSeaField.h"
#include "HexPlanetSurface.h"
#include "HexSphereGrid.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexSphereGridBlendTest, "HexHero.Planet.Grid.Blend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexSphereGridBlendTest::RunTest(const FString& Parameters)
{
	// the weights of the three cells blend their centers back into the direction, as they blend values smoothly between them
	const FHexSphereGrid Grid = FHexSphereGrid::Build(16);
	FRandomStream Random(5);

	for (int32 Sample = 0; Sample < 2000; ++Sample)
	{
		const FVector3d Direction = FVector3d(Random.GetUnitVector());
		int32 Cells[3];
		double Weights[3];
		const int32 Nearest = Grid.FindBlend(Direction, INDEX_NONE, Cells, Weights);

		FVector3d Blended = FVector3d::ZeroVector;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Blended += Grid.GetCells()[Cells[Index]].Center * Weights[Index];
		}

		const bool bGood = Nearest == Cells[0] && FMath::IsNearlyEqual(Weights[0] + Weights[1] + Weights[2], 1.0, 1.0e-9)
			&& FMath::Min3(Weights[0], Weights[1], Weights[2]) >= 0.0 && FVector3d::DotProduct(Blended.GetSafeNormal(), Direction) > 1.0 - 1.0e-6;

		if (!bGood)
		{
			AddError(FString::Printf(TEXT("FindBlend at %s gives weights %.3f, %.3f, %.3f"), *Direction.ToString(), Weights[0], Weights[1], Weights[2]));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexSphereGridChunksTest, "HexHero.Planet.Grid.Chunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexSphereGridChunksTest::RunTest(const FString& Parameters)
{
	// each face splits into Divisions^2 chunks, each a compact patch of cells that lies on one face and holds about as many cells as the others
	for (const FIntPoint& Case : {FIntPoint(12, 0), FIntPoint(20, 20), FIntPoint(32, 20), FIntPoint(80, 20), FIntPoint(80, 16)})
	{
		const FHexSphereGrid Grid = FHexSphereGrid::Build(Case.X, Case.Y);
		const TArray<FHexSphereCell>& Cells = Grid.GetCells();
		const int32 Divisions = Case.Y > 0 ? FMath::Max(FMath::RoundToInt32(double(Case.X) / Case.Y), 1) : 1;
		TestEqual(FString::Printf(TEXT("Frequency %d, chunk steps %d: chunks"), Case.X, Case.Y), Grid.GetNumChunks(), 20 * Divisions * Divisions);

		TArray<int32> Counts;
		Counts.Init(0, Grid.GetNumChunks());
		TArray<FVector3d> Middles;
		Middles.Init(FVector3d::ZeroVector, Grid.GetNumChunks());
		for (const FHexSphereCell& Cell : Cells)
		{
			++Counts[Cell.Chunk];
			Middles[Cell.Chunk] += Cell.Center;
		}

		// no cell lies further from the middle of its chunk than the corner of a chunk triangle, plus a cell
		const double Reach = FMath::Acos(1.0 / FMath::Sqrt(5.0)) / Divisions * 0.7 + 2.0 / Case.X;
		for (const FHexSphereCell& Cell : Cells)
		{
			const double Angle = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Cell.Center, Middles[Cell.Chunk].GetSafeNormal()), -1.0, 1.0));
			if (Angle > Reach)
			{
				AddError(FString::Printf(TEXT("Frequency %d: a cell lies %.4f from the middle of chunk %d, more than %.4f"), Case.X, Angle, Cell.Chunk, Reach));
				break;
			}
		}

		const int32 Fewest = FMath::Min(Counts);
		const int32 Most = FMath::Max(Counts);
		TestTrue(FString::Printf(TEXT("Frequency %d: chunks hold %d to %d cells"), Case.X, Fewest, Most), Fewest > 0 && Most <= Fewest * 2 + 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetSeaFieldFacesTest, "HexHero.Planet.SeaField.Faces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetSeaFieldFacesTest::RunTest(const FString& Parameters)
{
	// the middle of each face looks along its axis, in the order +X, -X, +Y, -Y, +Z, -Z
	const FVector3d Axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

	for (int32 Face = 0; Face < 6; ++Face)
	{
		const FVector3d Middle = (HexPlanetSeaField::GetTexelDirection(Face, 31, 31, 64) + HexPlanetSeaField::GetTexelDirection(Face, 32, 32, 64)).GetSafeNormal();
		TestTrue(FString::Printf(TEXT("Face %d looks along its axis"), Face), FVector3d::DotProduct(Middle, Axes[Face]) > 0.9999);
	}

	// neighboring texels of one face lie next to each other, and every direction belongs to a face
	const double Step = 2.0 / 64.0 * 1.5;

	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < 63; ++Y)
		{
			const FVector3d Here = HexPlanetSeaField::GetTexelDirection(Face, 10, Y, 64);
			const FVector3d Below = HexPlanetSeaField::GetTexelDirection(Face, 10, Y + 1, 64);

			if (FVector3d::Distance(Here, Below) > Step)
			{
				AddError(FString::Printf(TEXT("Texels %d and %d of face %d are apart"), Y, Y + 1, Face));
				break;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexSphereGridTopologyTest, "HexHero.Planet.Grid.Topology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexSphereGridTopologyTest::RunTest(const FString& Parameters)
{
	for (const int32 Frequency : {1, 2, 5, 16})
	{
		const FHexSphereGrid Grid = FHexSphereGrid::Build(Frequency);
		const TArray<FHexSphereCell>& Cells = Grid.GetCells();
		TestEqual(FString::Printf(TEXT("Cells at frequency %d"), Frequency), Cells.Num(), FHexSphereGrid::CountCells(Frequency));

		int32 Pentagons = 0;
		TArray<int32> CornerUses;
		CornerUses.SetNumZeroed(Grid.GetCorners().Num());

		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			const FHexSphereCell& Each = Cells[Cell];
			Pentagons += Each.IsPentagon() ? 1 : 0;

			if (Each.Neighbors.Num() != 5 && Each.Neighbors.Num() != 6)
			{
				AddError(FString::Printf(TEXT("Cell %d has %d neighbors"), Cell, Each.Neighbors.Num()));
			}

			for (int32 Index = 0; Index < Each.Neighbors.Num(); ++Index)
			{
				const int32 Neighbor = Each.Neighbors[Index];
				const FHexSphereCell& Other = Cells[Neighbor];
				const int32 Back = Other.Neighbors.Find(Cell);

				if (Back == INDEX_NONE)
				{
					AddError(FString::Printf(TEXT("Cell %d is a neighbor of %d, but not the other way"), Neighbor, Cell));
					continue;
				}

				// the side runs from corner K - 1 to corner K, and the neighbor sees the same two corners the other way around
				const int32 Count = Each.Corners.Num();
				const int32 OtherCount = Other.Corners.Num();
				const bool bSameSide = Each.Corners[(Index + Count - 1) % Count] == Other.Corners[Back]
					&& Each.Corners[Index] == Other.Corners[(Back + OtherCount - 1) % OtherCount];

				if (!bSameSide)
				{
					AddError(FString::Printf(TEXT("Cells %d and %d do not share the corners of their side"), Cell, Neighbor));
				}
			}

			// counterclockwise seen from outside
			for (int32 Index = 0; Index < Each.Corners.Num(); ++Index)
			{
				const FVector3d& A = Grid.GetCorners()[Each.Corners[Index]];
				const FVector3d& B = Grid.GetCorners()[Each.Corners[(Index + 1) % Each.Corners.Num()]];

				if (FVector3d::DotProduct(FVector3d::CrossProduct(A - Each.Center, B - Each.Center), Each.Center) <= 0.0)
				{
					AddError(FString::Printf(TEXT("Corners of cell %d do not go counterclockwise"), Cell));
					break;
				}
			}

			for (const int32 Corner : Each.Corners)
			{
				++CornerUses[Corner];
			}
		}

		TestEqual(FString::Printf(TEXT("Pentagons at frequency %d"), Frequency), Pentagons, 12);

		for (int32 Index = 0; Index < 12; ++Index)
		{
			TestTrue(TEXT("The first 12 cells are the pentagons"), Cells[Index].IsPentagon());
		}

		for (int32 Corner = 0; Corner < CornerUses.Num(); ++Corner)
		{
			if (CornerUses[Corner] != 3)
			{
				AddError(FString::Printf(TEXT("Corner %d is shared by %d cells"), Corner, CornerUses[Corner]));
				break;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexSphereGridFindCellTest, "HexHero.Planet.Grid.FindCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexSphereGridFindCellTest::RunTest(const FString& Parameters)
{
	const FHexSphereGrid Grid = FHexSphereGrid::Build(16);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	FRandomStream Random(3);

	for (int32 Sample = 0; Sample < 2000; ++Sample)
	{
		const FVector3d Direction = FVector3d(Random.GetUnitVector());
		int32 Nearest = 0;

		for (int32 Cell = 1; Cell < Cells.Num(); ++Cell)
		{
			if (FVector3d::DotProduct(Cells[Cell].Center, Direction) > FVector3d::DotProduct(Cells[Nearest].Center, Direction))
			{
				Nearest = Cell;
			}
		}

		const int32 Found = Grid.FindCell(Direction, Sample % 2 ? Random.RandRange(0, Cells.Num() - 1) : INDEX_NONE);

		if (Found != Nearest)
		{
			AddError(FString::Printf(TEXT("FindCell gives %d where %d is nearest"), Found, Nearest));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexSphereGridEvennessTest, "HexHero.Planet.Grid.Evenness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexSphereGridEvennessTest::RunTest(const FString& Parameters)
{
	// a straight projection makes the largest hex about 1.9 times the smallest, and the blend by sines must keep it near 1.2
	const FHexSphereGrid Grid = FHexSphereGrid::Build(80);
	double Smallest = TNumericLimits<double>::Max();
	double Largest = 0.0;
	double WorstSides = 1.0;

	for (int32 Cell = 12; Cell < Grid.GetCells().Num(); ++Cell)
	{
		const double Area = Grid.GetCellArea(Cell);
		Smallest = FMath::Min(Smallest, Area);
		Largest = FMath::Max(Largest, Area);
		WorstSides = FMath::Max(WorstSides, Grid.GetCellSideRatio(Cell));
	}

	AddInfo(FString::Printf(TEXT("Frequency 80: largest hex x%.3f of the smallest, worst sides x%.3f"), Largest / Smallest, WorstSides));
	TestTrue(TEXT("Hexes differ in area by less than 1.25"), Largest / Smallest < 1.25);
	TestTrue(TEXT("Sides of a hex differ by less than 1.25"), WorstSides < 1.25);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetCameraPlacementTest, "HexHero.Planet.Camera.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetCameraPlacementTest::RunTest(const FString& Parameters)
{
	// the place hex.CopyCamera prints brings the camera back to within a unit of the surface and a tenth of a degree, also at the poles
	const double Radius = 11503.0;
	FRandomStream Random(11);

	for (int32 Sample = 0; Sample < 2000; ++Sample)
	{
		FVector Up = Random.GetUnitVector();

		if (Sample < 4)
		{
			Up = FVector(0.0, 0.0, Sample % 2 ? 1.0 : -1.0);
		}

		const FVector Side = FVector::CrossProduct(Up, Random.GetUnitVector()).GetSafeNormal();
		const FQuat Orientation = FRotationMatrix::MakeFromXZ(Side, Up).ToQuat();

		const FHexPlanetCameraPlacement Placement = FHexPlanetCameraPlacement::FromOrientation(Orientation, 1234.0);
		const FString Text = FString::Printf(TEXT("%.4f %.4f %.1f"), Placement.Latitude, Placement.Longitude, Placement.Heading);

		TArray<FString> Parts;
		Text.ParseIntoArrayWS(Parts);
		FHexPlanetCameraPlacement Parsed;
		LexFromString(Parsed.Latitude, *Parts[0]);
		LexFromString(Parsed.Longitude, *Parts[1]);
		LexFromString(Parsed.Heading, *Parts[2]);

		const FQuat Back = Parsed.ToOrientation();
		const double Shift = FVector::Distance(Back.GetAxisZ(), Up) * Radius;
		const double Turn = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Back.GetAxisX(), Orientation.GetAxisX()), -1.0, 1.0)));

		if (Shift > 1.0 || Turn > 0.1)
		{
			AddError(FString::Printf(TEXT("%s comes back %.2f away and turned %.3f degrees"), *Text, Shift, Turn));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexLastViewBlocksTest, "HexHero.Planet.LastView.Blocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexLastViewBlocksTest::RunTest(const FString& Parameters)
{
	// the flat layout of before the blocks still reads
	FHexLastView Old;
	const bool bOld = HexLastView::Parse(TEXT(R"({"map": "RiverTest", "x": -806, "y": 742, "yaw": 23.6, "zoom": 1109, "bFreeCamera": true,
		"bPlanet": true, "planet": "Islet", "planetLatitude": 87.3749, "planetLongitude": 86.4121, "planetHeading": -94, "planetDistance": 3124})"), Old);
	TestTrue(TEXT("The flat layout reads"), bOld);
	TestEqual(TEXT("Map"), Old.FlatMap.Map, FString(TEXT("RiverTest")));
	TestEqual(TEXT("X"), Old.FlatMap.X, -806.0);
	TestTrue(TEXT("Free camera"), Old.FlatMap.bFreeCamera);
	TestTrue(TEXT("Planet open"), Old.bPlanetOpen);
	TestEqual(TEXT("Planet"), Old.Planet.Planet, FString(TEXT("Islet")));
	TestEqual(TEXT("Distance"), Old.Planet.Distance, 3124.0);
	TestEqual(TEXT("Heading"), Old.Planet.Heading, -94.0);

	// and a view written in blocks reads back the same
	FHexLastView Written;
	FString Json;
	TestTrue(TEXT("The blocks write"), FJsonObjectConverter::UStructToJsonObjectString(Old, Json));
	TSharedPtr<FJsonObject> Object;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
	TestTrue(TEXT("The written file has blocks of the flat map and the planet"), Object.IsValid()
		&& Object->HasTypedField<EJson::Object>(TEXT("flatMap")) && Object->HasTypedField<EJson::Object>(TEXT("planet")));
	TestTrue(TEXT("The blocks read"), HexLastView::Parse(Json, Written));
	TestTrue(TEXT("The blocks read back the same"), Written == Old);

	FHexLastView Empty;
	TestFalse(TEXT("A file with no view is not valid"), HexLastView::Parse(TEXT("{}"), Empty));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetKitSectorTest, "HexHero.Planet.Kit.Sector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetKitSectorTest::RunTest(const FString& Parameters)
{
	// the weights of the triangle that holds a point blend its corners back into the point, and are all positive inside the hex
	FRandomStream Random(3);

	for (int32 Sample = 0; Sample < 1000; ++Sample)
	{
		const FVector2d Point(Random.FRandRange(-0.86, 0.86), Random.FRandRange(-0.86, 0.86));
		int32 Sector;
		double Center, From, To;
		HexPlanetKit::FindSector(Point, Sector, Center, From, To);

		const FVector2d CornerFrom(FMath::Cos(Sector * UE_DOUBLE_PI / 3.0), FMath::Sin(Sector * UE_DOUBLE_PI / 3.0));
		const FVector2d CornerTo(FMath::Cos((Sector + 1) * UE_DOUBLE_PI / 3.0), FMath::Sin((Sector + 1) * UE_DOUBLE_PI / 3.0));
		const FVector2d Blended = CornerFrom * From + CornerTo * To;

		if (!Blended.Equals(Point, 1.0e-9) || FMath::Min(From, To) < -1.0e-9 || (Point.Length() < 0.8 && Center < -1.0e-9))
		{
			AddError(FString::Printf(TEXT("Point %s gives sector %d with weights %.3f, %.3f, %.3f"), *Point.ToString(), Sector, Center, From, To));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetKitSeamsTest, "HexHero.Planet.Kit.Seams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetKitSeamsTest::RunTest(const FString& Parameters)
{
	// two cells place the points of their shared side at the same place, whatever levels they and the cells at its ends have,
	// as long as their pieces follow the rules of the kit: on a side towards the same level anything from the edge to 0 for land, and the same
	// profile on both sides between mountains. A side towards another level and its corners go on the side line whatever a piece models there,
	// so they get any height, as a mountain models its notches
	const FHexSphereGrid Grid = FHexSphereGrid::Build(6);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	FRandomStream Random(11);

	const int32 MountainsLevel = 2;
	TArray<int32> Levels;
	TArray<int32> MassifSteps;
	TArray<float> Relief;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		Levels.Add(Cells[Cell].IsPentagon() ? Random.RandRange(-2, 1) : Random.RandRange(-2, MountainsLevel));
		MassifSteps.Add(Random.RandRange(0, 3));
		// each cell stretches its relief by its own factor
		Relief.Add(Random.FRandRange(0.4f, 1.8f));
	}

	const auto GetLevel = [&](int32 Cell) { return Levels[Cell]; };
	const auto GetTop = [&](int32 Cell) { return 1000.0 + Levels[Cell] * 25.0; };
	const TArray<double> CornerHeights = HexPlanetKit::FindCornerHeights(Grid, GetTop);

	HexPlanetKit::FPlacement Land;
	HexPlanetKit::FPlacement Mountain;
	Mountain.FootDepth = 75.0;
	Mountain.FadeHeight = 55.0;
	Mountain.bTopFollowsCorners = false;
	Mountain.MassifStep = 0.2;
	Mountain.MassifRise = 30.0;

	// both stretch by the factor of their cells, and the slopes below the plains wobble
	Land.bRelief = true;
	Land.ScaleBase = -20.0;
	Mountain.bRelief = true;
	Mountain.ScaleBase = -45.0;
	// and mountains have the shape of their inside, crags and faces of split stone
	Mountain.ShapeHeight = 105.0;
	Mountain.CragHeight = 14.0;
	Mountain.CragScale = 45.0;
	Mountain.RidgeLift = 25.0;
	Mountain.BulkLift = 28.0;
	Mountain.BulkReach = 0.3;
	Mountain.SplitDepth = 0.3;
	Mountain.SplitBlade = 0.1;
	Mountain.RidgeWidth = 0.3;
	Mountain.JaggedHeight = 7.0;
	Mountain.JaggedScale = 34.0;
	Mountain.FacetLow = 1000.0 + 40.0;
	Mountain.FacetHigh = 1000.0 + 90.0;

	for (HexPlanetKit::FPlacement* Placement : {&Land, &Mountain})
	{
		Placement->Wobble = 12.0;
		Placement->WobbleScale = 100.0;
		Placement->WobbleDetail = 0.35;
		Placement->WobbleTops[0] = 1000.0 - 50.0;
		Placement->WobbleTops[1] = 1000.0 - 25.0;
		Placement->WobbleTops[2] = 1000.0;
	}

	const auto IsOpen = [&](int32 Cell, int32 Side) { return Levels[Cells[Cell].Neighbors[Side % Cells[Cell].Neighbors.Num()]] != Levels[Cell]; };

	// the height a piece models at a point of a side, from 0 at its corner From to 1 at its corner To
	const auto GetModelled = [&](int32 Cell, int32 Side, double S)
	{
		const int32 NumSides = Cells[Cell].Neighbors.Num();
		const bool bMountain = Levels[Cell] == MountainsLevel;
		const double Foot = bMountain ? -Mountain.FootDepth : -Land.FootDepth;

		if (IsOpen(Cell, Side))
		{
			return Random.FRandRange(Foot, 60.0);
		}
		if (bMountain)
		{
			return -30.0 + 40.0 * FMath::Sin(UE_DOUBLE_PI * S);
		}
		if ((S < 1.0e-9 && IsOpen(Cell, Side + NumSides - 1)) || (S > 1.0 - 1.0e-9 && IsOpen(Cell, Side + 1)))
		{
			return Random.FRandRange(Foot, 60.0);
		}
		return Random.FRandRange(Foot, 0.0);
	};

	// each cell has a shape of its own inside, a dome or a needle and turned
	const auto MakeFrame = [&](int32 Cell)
	{
		HexPlanetKit::FCellFrame Frame = HexPlanetKit::BuildFrame(Grid, Cell, GetTop, GetLevel, CornerHeights, MassifSteps, Relief);
		Frame.Sharpness = 0.6 + (Cell % 7) * 0.2;
		Frame.Twist = ((Cell % 5) - 2) * 0.2;
		Frame.Split = (Cell % 3) * 0.5;
		return Frame;
	};

	double Largest = 0.0;
	int32 Compared = 0;

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const HexPlanetKit::FCellFrame Frame = MakeFrame(Cell);
		const int32 NumSides = Cells[Cell].Neighbors.Num();

		for (int32 Side = 0; Side < NumSides; ++Side)
		{
			const int32 Neighbor = Cells[Cell].Neighbors[Side];
			const int32 NeighborSide = Cells[Neighbor].Neighbors.IndexOfByKey(Cell);
			const HexPlanetKit::FCellFrame NeighborFrame = MakeFrame(Neighbor);

			for (int32 Step = 0; Step <= 10; ++Step)
			{
				const double S = Step / 10.0;
				const FVector3d Here = HexPlanetKit::PlaceVertex(Grid, Cell, Frame, Levels[Cell] == MountainsLevel ? Mountain : Land,
					Side, 0.0, 1.0 - S, S, GetModelled(Cell, Side, S));
				// the neighbor walks the same side the other way
				const FVector3d There = HexPlanetKit::PlaceVertex(Grid, Neighbor, NeighborFrame, Levels[Neighbor] == MountainsLevel ? Mountain : Land,
					NeighborSide, 0.0, S, 1.0 - S, GetModelled(Neighbor, NeighborSide, 1.0 - S));

				const double Distance = FVector3d::Distance(Here, There);
				++Compared;
				if (Distance > Largest)
				{
					Largest = Distance;
					if (Distance > 1.0e-6)
					{
						AddError(FString::Printf(TEXT("Cell %d (level %d) and %d (level %d) are %.4f apart at %.1f of their side"),
							Cell, Levels[Cell], Neighbor, Levels[Neighbor], Distance, S));
					}
				}
			}
		}
	}

	TestTrue(TEXT("Sides were compared"), Compared > Cells.Num() * 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetFileLevelsTest, "HexHero.Planet.File.Levels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetFileLevelsTest::RunTest(const FString& Parameters)
{
	// every level survives the text of levels.txt, which breaks into lines of the same length
	const int32 CellCount = FHexSphereGrid::CountCells(5);
	TArray<int8> Levels;
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		Levels.Add(int8(Cell % 5 - 2));
	}

	const FString Text = HexPlanetFile::EncodeLevels(Levels);
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("Lines"), Lines.Num(), FMath::DivideAndRoundUp(CellCount, HexPlanetFile::LevelsLineLength));
	TestEqual(TEXT("First line"), Lines[0].Len(), HexPlanetFile::LevelsLineLength);
	TestTrue(TEXT("Letters"), Text.StartsWith(TEXT("DSPHMDSPHM")));

	TArray<int8> Decoded;
	FString Error;
	TestTrue(TEXT("Decodes"), HexPlanetFile::DecodeLevels(Text.Replace(TEXT("\n"), TEXT("\r\n")), CellCount, Decoded, Error));
	TestTrue(TEXT("Same levels"), Decoded == Levels);

	// the count of cells and the letters are checked
	TestFalse(TEXT("Too few cells"), HexPlanetFile::DecodeLevels(Text.LeftChop(2), CellCount, Decoded, Error));
	TestTrue(TEXT("Names the counts"), Error.Contains(FString::FromInt(CellCount)));
	TestFalse(TEXT("Unknown letter"), HexPlanetFile::DecodeLevels(TEXT("DSX"), 3, Decoded, Error));
	TestTrue(TEXT("Names the letter"), Error.Contains(TEXT("'X'")));
	TestTrue(TEXT("Lower case"), HexPlanetFile::DecodeLevels(TEXT("dsphm"), 5, Decoded, Error) && Decoded == TArray<int8>({-2, -1, 0, 1, 2}));

	// the manifest needs the current version and a frequency the grid can build
	FHexPlanetManifest Manifest;
	TestTrue(TEXT("Manifest"), HexPlanetFile::ParseManifest(TEXT("{\"version\": 1, \"name\": \"A\", \"frequency\": 32, \"seed\": 3}"), Manifest, Error)
		&& Manifest.Frequency == 32 && Manifest.Seed == 3);
	TestFalse(TEXT("Old version"), HexPlanetFile::ParseManifest(TEXT("{\"version\": 0, \"frequency\": 32}"), Manifest, Error));
	TestFalse(TEXT("Frequency 0"), HexPlanetFile::ParseManifest(TEXT("{\"version\": 1, \"frequency\": 0}"), Manifest, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetGeneratorSharesTest, "HexHero.Planet.Generator.Shares",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetGeneratorSharesTest::RunTest(const FString& Parameters)
{
	// the levels split the cells by the shares on any planet, and the same seed gives the same planet
	const FHexSphereGrid Grid = FHexSphereGrid::Build(20);
	const int32 CellCount = Grid.GetCells().Num();
	const UHexPlanetConfig& Config = *GetDefault<UHexPlanetConfig>();

	FHexPlanetManifest Manifest;
	Manifest.Seed = 11;
	Manifest.Generation.LandShare = 0.4f;
	Manifest.Generation.ShallowsShare = 0.25f;
	Manifest.Generation.HillShare = 0.3f;
	Manifest.Generation.MountainShare = 0.1f;

	TArray<int8> Levels;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, Levels);
	TestEqual(TEXT("A level per cell"), Levels.Num(), CellCount);

	int32 Counts[5] = {};
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		++Counts[Levels[Cell] + 2];
		if (Grid.GetCells()[Cell].IsPentagon() && Levels[Cell] == 2)
		{
			AddError(FString::Printf(TEXT("Pentagon %d is a mountain"), Cell));
		}
	}

	const int32 Land = FMath::RoundToInt32(0.4 * CellCount);
	TestEqual(TEXT("Land"), Counts[2] + Counts[3] + Counts[4], Land);
	TestEqual(TEXT("Shallows"), Counts[1], FMath::RoundToInt32(0.25 * (CellCount - Land)));
	TestEqual(TEXT("Hills"), Counts[3], FMath::RoundToInt32(0.3 * Land));
	TestEqual(TEXT("Mountains"), Counts[4], FMath::RoundToInt32(0.1 * Land));

	TArray<int8> Again;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, Again);
	TestTrue(TEXT("Same seed, same planet"), Again == Levels);

	Manifest.Seed = 12;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, Again);
	TestFalse(TEXT("Other seed, other planet"), Again == Levels);

	Manifest.Generation.Start = EHexPlanetStart::Water;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, Again);
	TestTrue(TEXT("All water"), !Again.ContainsByPredicate([](int8 Level) { return Level != -2; }));

	Manifest.Generation.Start = EHexPlanetStart::Land;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, Again);
	TestTrue(TEXT("All land"), !Again.ContainsByPredicate([](int8 Level) { return Level != 0; }));

	// the folder of a new planet keeps only what a console argument can hold
	TestEqual(TEXT("Folder of a name"), HexPlanetFile::SanitizeFolderName(TEXT(" My new world! ")), FString(TEXT("My_new_world_")));
	// letters of any alphabet stay, here a Cyrillic name written as code points, as the sources keep to ASCII
	TestEqual(TEXT("Any alphabet"), HexPlanetFile::SanitizeFolderName(TEXT("Нова земля")), FString(TEXT("Нова_земля")));
	TestTrue(TEXT("No folder of an empty name"), HexPlanetFile::SanitizeFolderName(TEXT(" ! ")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetFileBiomesTest, "HexHero.Planet.File.Biomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetFileBiomesTest::RunTest(const FString& Parameters)
{
	// every biome survives the text of biomes.txt, written by the letters of the biomes and read in any case
	const TArray<TCHAR> Letters = {TEXT('D'), TEXT('G'), TEXT('I'), TEXT('W')};
	const int32 CellCount = FHexSphereGrid::CountCells(5);
	TArray<uint8> Biomes;
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		Biomes.Add(uint8(Cell % Letters.Num()));
	}

	const FString Text = HexPlanetFile::EncodeBiomes(Biomes, Letters);
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("Lines"), Lines.Num(), FMath::DivideAndRoundUp(CellCount, HexPlanetFile::LevelsLineLength));
	TestTrue(TEXT("Letters"), Text.StartsWith(TEXT("DGIWDGIW")));

	TArray<uint8> Decoded;
	FString Error;
	TestTrue(TEXT("Decodes"), HexPlanetFile::DecodeBiomes(Text.Replace(TEXT("\n"), TEXT("\r\n")), CellCount, Letters, Decoded, Error));
	TestTrue(TEXT("Same biomes"), Decoded == Biomes);
	TestTrue(TEXT("Lower case"), HexPlanetFile::DecodeBiomes(TEXT("wigd"), 4, Letters, Decoded, Error) && Decoded == TArray<uint8>({3, 2, 1, 0}));
	TestFalse(TEXT("Unknown letter"), HexPlanetFile::DecodeBiomes(TEXT("WX"), 2, Letters, Decoded, Error));
	TestTrue(TEXT("Names the letter"), Error.Contains(TEXT("'X'")));
	TestFalse(TEXT("Too few cells"), HexPlanetFile::DecodeBiomes(TEXT("WG"), 3, Letters, Decoded, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetGeneratorBiomesTest, "HexHero.Planet.Generator.Biomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetGeneratorBiomesTest::RunTest(const FString& Parameters)
{
	// the climate of the prototype planet: water and ice on the sea only, ice and snow near the poles, a varied land, and the same seed gives the same biomes
	const FHexSphereGrid Grid = FHexSphereGrid::Build(40);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const UHexPlanetConfig& Config = *GetDefault<UHexPlanetConfig>();
	FHexPlanetManifest Manifest;
	Manifest.Frequency = 40;

	TArray<int8> Levels;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, Levels);
	TArray<TCHAR> Biomes;
	HexPlanetGenerator::GenerateBiomes(Grid, Manifest, Config, Levels, Biomes);
	TestEqual(TEXT("A biome per cell"), Biomes.Num(), Cells.Num());

	namespace Biome = HexPlanetGenerator::Biome;
	TMap<TCHAR, int32> LandCounts;
	int32 Ice = 0;
	int32 PolarIce = 0;
	int32 PolarSea = 0;
	double IceLatitude = 0.0;

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const bool bSea = Levels[Cell] < 0;
		const bool bWaterBiome = Biomes[Cell] == Biome::Water || Biomes[Cell] == Biome::Ice;
		if (bSea != bWaterBiome)
		{
			AddError(FString::Printf(TEXT("Cell %d of level %d has the biome %c"), Cell, Levels[Cell], Biomes[Cell]));
		}
		if (Biomes[Cell] == Biome::Swamp && Levels[Cell] != 0)
		{
			AddError(FString::Printf(TEXT("Cell %d is a swamp on level %d"), Cell, Levels[Cell]));
		}

		const double Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Abs(Cells[Cell].Center.Z)));
		if (!bSea)
		{
			++LandCounts.FindOrAdd(Biomes[Cell]);
		}
		else if (Latitude > 85.0)
		{
			++PolarSea;
			PolarIce += Biomes[Cell] == Biome::Ice ? 1 : 0;
		}
		if (Biomes[Cell] == Biome::Ice)
		{
			++Ice;
			IceLatitude += Latitude;
		}
	}

	TestTrue(TEXT("There is ice"), Ice > 0);
	TestTrue(TEXT("Ice lies near the poles"), Ice > 0 && IceLatitude / Ice > 70.0);
	TestTrue(TEXT("The sea at the poles is frozen"), PolarSea == 0 || PolarIce * 2 > PolarSea);

	// the land has most of the biomes, and none takes it all: grass, which has the wet land since forests are to be objects, takes about half
	int32 Land = 0;
	for (const TPair<TCHAR, int32>& Count : LandCounts)
	{
		Land += Count.Value;
	}
	FString Shares;
	for (const TPair<TCHAR, int32>& Count : LandCounts)
	{
		Shares += FString::Printf(TEXT(" %c %.1f%%"), Count.Key, 100.0 * Count.Value / Land);
		TestTrue(FString::Printf(TEXT("Biome %c takes less than two thirds of the land"), Count.Key), Count.Value * 3 < Land * 2);
	}
	AddInfo(FString::Printf(TEXT("Land biomes:%s"), *Shares));
	for (const TCHAR Letter : {Biome::Grass, Biome::Desert, Biome::Steppe, Biome::Tundra, Biome::Snow})
	{
		TestTrue(FString::Printf(TEXT("There is %c"), Letter), LandCounts.Contains(Letter));
	}

	TArray<TCHAR> Again;
	HexPlanetGenerator::GenerateBiomes(Grid, Manifest, Config, Levels, Again);
	TestTrue(TEXT("Same seed, same biomes"), Again == Biomes);

	// a colder planet has more ice
	Manifest.Generation.Temperature = -0.2;
	HexPlanetGenerator::GenerateBiomes(Grid, Manifest, Config, Levels, Again);
	TestTrue(TEXT("Colder, more ice"), Again.FilterByPredicate([](TCHAR Letter) { return Letter == HexPlanetGenerator::Biome::Ice; }).Num() > Ice);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetSurfaceRebuildTest, "HexHero.Planet.Surface.Rebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetSurfaceRebuildTest::RunTest(const FString& Parameters)
{
	// rebuilding only the chunks that a change of levels reaches gives those chunks as a full build of the new levels does, normals at the
	// borders to the chunks left alone included, and the chunks left alone are as a full build of the new levels would make them
	const FHexSphereGrid Grid = FHexSphereGrid::Build(12, 4);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const UHexPlanetConfig& Config = *GetDefault<UHexPlanetConfig>();

	// the real kit when it loads, so mountains and their massifs take part, else flat tiles
	const UHexPlanetKitConfig* KitConfig = LoadObject<UHexPlanetKitConfig>(nullptr, TEXT("/Game/HexPlanet/Kit/DA_HexPlanetKit.DA_HexPlanetKit"));
	const TSharedRef<const HexPlanetSurface::FKit> Kit = HexPlanetSurface::LoadKit(KitConfig, Config.HexRadius);
	const double Radius = FMath::Sqrt(Cells.Num() * 1.5 * FMath::Sqrt(3.0) * Config.HexRadius * Config.HexRadius / (4.0 * UE_DOUBLE_PI));
	const HexPlanetSurface::FParams Params = HexPlanetSurface::FParams::Make(Config, *Kit, Radius);

	FHexPlanetManifest Manifest;
	Manifest.Frequency = Grid.GetFrequency();
	TArray<int8> OldLevels;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, OldLevels);

	// a patch of mountains around a cell near the border of two chunks, and a cell of land sunk into the deep sea
	TArray<int8> NewLevels = OldLevels;
	int32 BorderCell = INDEX_NONE;
	for (int32 Cell = 12; Cell < Cells.Num() && BorderCell == INDEX_NONE; ++Cell)
	{
		if (Cells[Cells[Cell].Neighbors[0]].Chunk != Cells[Cell].Chunk)
		{
			BorderCell = Cell;
		}
	}
	NewLevels[BorderCell] = 2;
	for (const int32 Neighbor : Cells[BorderCell].Neighbors)
	{
		NewLevels[Neighbor] = Cells[Neighbor].IsPentagon() ? 1 : 2;
	}
	const int32 SunkCell = OldLevels.IndexOfByPredicate([](int8 Level) { return Level >= 0; });
	NewLevels[SunkCell] = -2;

	const TArray<int32> AllChunks = HexPlanetSurface::GetAllChunks(Grid);

	const TArray<int32> OldMassif = HexPlanetSurface::FindMassifSteps(Grid, *Kit, OldLevels);
	const TArray<int32> NewMassif = HexPlanetSurface::FindMassifSteps(Grid, *Kit, NewLevels);
	const HexPlanetSurface::FBuild Old = HexPlanetSurface::Build(Grid, *Kit, Params, OldLevels, OldMassif, AllChunks, 0.0);
	// and the relief of the mountain in the middle set by hand: another version, turn and mirror, and higher, which reaches its neighbors
	HexPlanetSurface::FReliefOverrides Overrides;
	HexPlanetSurface::FReliefOverride& Override = Overrides.Add(BorderCell);
	Override.Version = 1;
	Override.Turn = 1;
	Override.Mirror = 1;
	Override.Scale = 1.4f;

	const HexPlanetSurface::FBuild Full = HexPlanetSurface::Build(Grid, *Kit, Params, NewLevels, NewMassif, AllChunks, Old.MeanPeak, &Overrides);
	const TArray<int32> Chunks = HexPlanetSurface::FindAffectedChunks(Grid, OldLevels, NewLevels, OldMassif, NewMassif,
		HexPlanetSurface::FindChangedOverrides({}, Overrides));
	const HexPlanetSurface::FBuild Partial = HexPlanetSurface::Build(Grid, *Kit, Params, NewLevels, NewMassif, Chunks, Old.MeanPeak, &Overrides);

	TestTrue(TEXT("The change reaches some chunks, not all"), Chunks.Num() > 0 && Chunks.Num() < Grid.GetNumChunks());
	TestTrue(TEXT("The change reaches the chunks of its cells"), Chunks.Contains(Cells[BorderCell].Chunk) && Chunks.Contains(Cells[SunkCell].Chunk));

	const auto Compare = [this](const HexPlanetSurface::FSection& A, const HexPlanetSurface::FSection& B, const FString& What)
	{
		if (A.Positions.Num() != B.Positions.Num() || A.Triangles != B.Triangles)
		{
			AddError(FString::Printf(TEXT("%s: %d and %d vertices, or other triangles"), *What, A.Positions.Num(), B.Positions.Num()));
			return;
		}
		for (int32 Vertex = 0; Vertex < A.Positions.Num(); ++Vertex)
		{
			if (!A.Positions[Vertex].Equals(B.Positions[Vertex], 1.0e-6) || !A.Normals[Vertex].Equals(B.Normals[Vertex], 1.0e-4)
				|| !A.Colors[Vertex].Equals(B.Colors[Vertex], 1.0e-4f) || !A.GridUVs[Vertex].Equals(B.GridUVs[Vertex], 1.0e-5))
			{
				AddError(FString::Printf(TEXT("%s: vertex %d differs, normal %s and %s"), *What, Vertex, *A.Normals[Vertex].ToString(), *B.Normals[Vertex].ToString()));
				return;
			}
		}
	};

	for (int32 Chunk = 0; Chunk < Grid.GetNumChunks(); ++Chunk)
	{
		const int32 Slot = Chunks.IndexOfByKey(Chunk);
		for (int32 Part = 0; Part < 2; ++Part)
		{
			const HexPlanetSurface::FSection& Expected = Full.Sections[Chunk * 2 + Part];
			if (Slot != INDEX_NONE)
			{
				Compare(Partial.Sections[Slot * 2 + Part], Expected, FString::Printf(TEXT("Rebuilt section %d"), Chunk * 2 + Part));
			}
			else
			{
				Compare(Old.Sections[Chunk * 2 + Part], Expected, FString::Printf(TEXT("Section %d left alone"), Chunk * 2 + Part));
			}
		}

		// and so do the coarse sections
		const HexPlanetSurface::FSection& ExpectedCoarse = Full.CoarseSections[Chunk];
		if (Slot != INDEX_NONE)
		{
			Compare(Partial.CoarseSections[Slot], ExpectedCoarse, FString::Printf(TEXT("Rebuilt coarse section %d"), Chunk));
		}
		else
		{
			Compare(Old.CoarseSections[Chunk], ExpectedCoarse, FString::Printf(TEXT("Coarse section %d left alone"), Chunk));
		}
	}

	AddInfo(FString::Printf(TEXT("%d pieces, %d of %d chunks rebuilt"), Kit->Pieces.Num(), Chunks.Num(), Grid.GetNumChunks()));

	// the pieces of mountains, mirrored, turned and stretched by cell, meet their neighbors exactly on their shared sides
	TestEqual(TEXT("Every point on a side between mountains is built by both cells"), Full.UnmatchedRidgePoints, 0);
	TestEqual(TEXT("Every point on a side between mountains is built by both cells, before the change"), Old.UnmatchedRidgePoints, 0);
	AddInfo(FString::Printf(TEXT("%d points on sides between mountains, %d mirrored cells"), Full.RidgePoints,
		Full.CellMirrored.FilterByPredicate([](bool bMirrored) { return bMirrored; }).Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexPlanetRiversTest, "HexHero.Planet.Rivers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexPlanetRiversTest::RunTest(const FString& Parameters)
{
	const FHexSphereGrid Grid = FHexSphereGrid::Build(16, 4);
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const UHexPlanetConfig& Config = *GetDefault<UHexPlanetConfig>();
	FHexPlanetManifest Manifest;
	Manifest.Frequency = Grid.GetFrequency();
	TArray<int8> Levels;
	HexPlanetGenerator::GenerateLevels(Grid, Manifest, Config, Levels);

	// many rivers, so the small planet has some
	HexPlanetRivers::FGenerateParams GenerateParams;
	GenerateParams.Seed = 7;
	GenerateParams.Density = 40.0f;
	GenerateParams.MinLength = 3;
	GenerateParams.SourceSeaDistance = 2;
	TSet<uint64> Edges;
	TSet<uint64> Bridges;
	HexPlanetRivers::Generate(Grid, Levels, GenerateParams, Edges, Bridges);
	AddInfo(FString::Printf(TEXT("%d sides of rivers, %d bridges"), Edges.Num(), Bridges.Num()));
	TestTrue(TEXT("The generator lays rivers"), Edges.Num() > 0);
	TestTrue(TEXT("Bridges stand on rivers"), Edges.Includes(Bridges));

	TSet<uint64> Again;
	TSet<uint64> AgainBridges;
	HexPlanetRivers::Generate(Grid, Levels, GenerateParams, Again, AgainBridges);
	TestTrue(TEXT("Same seed, same rivers"), Again.Num() == Edges.Num() && Again.Includes(Edges));

	for (const uint64 Key : Edges)
	{
		int32 A, B;
		HexPlanetRivers::GetEdgeCells(Key, A, B);
		if (!Cells[A].Neighbors.Contains(B) || Levels[A] != Levels[B] || !HexPlanetRivers::IsLevelAllowed(Levels[A]))
		{
			AddError(FString::Printf(TEXT("A river between cells %d and %d of levels %d and %d"), A, B, Levels[A], Levels[B]));
			break;
		}
	}

	// a river runs straight: at each corner it turns left or right, and a side whose two corners both turn it the same way takes it off to the
	// side, which the generator shuns (TurnPenalty), so fewer than half of the sides do, as a random walk would
	{
		TMap<int32, TArray<int32, TInlineAllocator<3>>> Ends;
		for (const uint64 Key : Edges)
		{
			int32 A, B;
			HexPlanetRivers::GetEdgeCells(Key, A, B);
			const int32 NumSides = Cells[A].Corners.Num();
			const int32 Side = Cells[A].Neighbors.IndexOfByKey(B);
			const int32 From = Cells[A].Corners[(Side + NumSides - 1) % NumSides];
			const int32 To = Cells[A].Corners[Side];
			Ends.FindOrAdd(From).Add(To);
			Ends.FindOrAdd(To).Add(From);
		}
		const TArray<FVector3d>& Points = Grid.GetCorners();
		const auto TurnAt = [&](int32 Before, int32 At, int32 After)
		{
			return FVector3d::DotProduct(FVector3d::CrossProduct(Points[At] - Points[Before], Points[After] - Points[At]), Points[At]) > 0.0;
		};
		int32 Same = 0;
		int32 Turning = 0;
		for (const TPair<int32, TArray<int32, TInlineAllocator<3>>>& Pair : Ends)
		{
			for (const int32 Other : Pair.Value)
			{
				const TArray<int32, TInlineAllocator<3>>& OtherEnds = Ends[Other];
				if (Pair.Key > Other || Pair.Value.Num() != 2 || OtherEnds.Num() != 2)
				{
					continue;
				}
				const int32 Before = Pair.Value[0] == Other ? Pair.Value[1] : Pair.Value[0];
				const int32 After = OtherEnds[0] == Pair.Key ? OtherEnds[1] : OtherEnds[0];
				Same += TurnAt(Before, Pair.Key, Other) == TurnAt(Pair.Key, Other, After) ? 1 : 0;
				++Turning;
			}
		}
		AddInfo(FString::Printf(TEXT("%d of %d sides turn the river the same way at both corners"), Same, Turning));
		TestTrue(TEXT("Rivers run straight rather than off to the side"), Turning > 0 && Same * 10 < Turning * 4);
	}

	// both cells of a side hold its river, the one flowing from its corner K - 1 to K and the other the other way
	const TArray<uint16> Net = HexPlanetRivers::BuildNet(Grid, Levels, Edges);
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		for (int32 Side = 0; Side < Cells[Cell].Neighbors.Num(); ++Side)
		{
			const int32 Neighbor = Cells[Cell].Neighbors[Side];
			const int32 Back = Cells[Neighbor].Neighbors.IndexOfByKey(Cell);
			const bool bRiver = (Net[Cell] >> Side & 1) != 0;
			const bool bFlow = (Net[Cell] >> (Side + HexPlanetRivers::FlowShift) & 1) != 0;
			if (bRiver != ((Net[Neighbor] >> Back & 1) != 0) || bRiver != Edges.Contains(HexPlanetRivers::GetEdgeKey(Cell, Neighbor))
				|| (bRiver && bFlow == ((Net[Neighbor] >> (Back + HexPlanetRivers::FlowShift) & 1) != 0)))
			{
				AddError(FString::Printf(TEXT("Cells %d and %d disagree on their side"), Cell, Neighbor));
				return true;
			}
		}
	}

	// the line of the rivers: a point on a side lies as far from it for either cell, so both lower it alike and the surface has no seam, and the
	// line keeps near the sides it belongs to, cutting their corners
	const double Radius = FMath::Sqrt(Cells.Num() * 1.5 * FMath::Sqrt(3.0) * Config.HexRadius * Config.HexRadius / (4.0 * UE_DOUBLE_PI));
	HexPlanetRivers::FCurveParams CurveParams;
	CurveParams.Radius = Radius;
	CurveParams.HexRadius = Config.HexRadius;
	CurveParams.RiverWidth = Config.RiverWidth;
	CurveParams.Meander = Config.RiverMeander;
	CurveParams.MeanderScale = Config.RiverMeanderScale;
	CurveParams.MeanderFade = Config.RiverMeanderFade;
	CurveParams.SourceTaper = Config.RiverSourceTaper;
	CurveParams.MouthLength = Config.RiverMouthLength;
	CurveParams.Smoothing = Config.RiverSmoothing;
	CurveParams.Reach = HexPlanetRivers::GetReach(Config);
	CurveParams.CornerCut = Config.RiverCornerCut;
	CurveParams.Relax = Config.RiverRelax;
	CurveParams.MaxOffset = Config.RiverMaxOffset;
	const TSharedRef<const HexPlanetRivers::FCurve> Curve = HexPlanetRivers::BuildCurve(Grid, Levels, Net, CurveParams);
	AddInfo(FString::Printf(TEXT("%d points on the line of the rivers, %d mouths"), Curve->Points.Num(), Curve->Mouths.Num()));
	TestTrue(TEXT("The rivers have a line"), Curve->Points.Num() > Edges.Num());

	int32 Checked = 0;
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const int32 NumSides = Cells[Cell].Corners.Num();
		for (int32 Side = 0; Side < NumSides; ++Side)
		{
			const int32 Neighbor = Cells[Cell].Neighbors[Side];
			const FVector3d From = Grid.GetCorners()[Cells[Cell].Corners[(Side + NumSides - 1) % NumSides]];
			const FVector3d To = Grid.GetCorners()[Cells[Cell].Corners[Side]];
			for (int32 Step = 0; Step <= 8; ++Step)
			{
				const FVector3d Point = FMath::Lerp(From, To, Step / 8.0).GetSafeNormal();
				HexPlanetRivers::FCurveHit Own;
				HexPlanetRivers::FCurveHit Other;
				HexPlanetRivers::FindNearest(*Curve, Cell, Point, Own);
				HexPlanetRivers::FindNearest(*Curve, Neighbor, Point, Other);
				const double Near = FMath::Min(Own.Distance, CurveParams.Reach);
				const double Far = FMath::Min(Other.Distance, CurveParams.Reach);
				if (FMath::Min(Near, Far) < CurveParams.Reach)
				{
					++Checked;
					if (!FMath::IsNearlyEqual(Near, Far, 1.0e-6))
					{
						AddError(FString::Printf(TEXT("Cells %d and %d see a river at %g and %g on their side"), Cell, Neighbor, Near, Far));
						return true;
					}
				}
			}
		}
	}
	TestTrue(TEXT("Points near rivers were checked"), Checked > 0);

	// the middle of every side with a river lies near its line, which leaves the sides by less than a quarter of a cell
	double Farthest = 0.0;
	for (const uint64 Key : Edges)
	{
		int32 A, B;
		HexPlanetRivers::GetEdgeCells(Key, A, B);
		if (!HexPlanetRivers::IsLevelAllowed(Levels[A]) || Levels[A] != Levels[B])
		{
			continue;
		}
		const int32 NumSides = Cells[A].Corners.Num();
		const int32 Side = Cells[A].Neighbors.IndexOfByKey(B);
		const FVector3d Middle = (Grid.GetCorners()[Cells[A].Corners[(Side + NumSides - 1) % NumSides]] + Grid.GetCorners()[Cells[A].Corners[Side]]).GetSafeNormal();
		HexPlanetRivers::FCurveHit Hit;
		HexPlanetRivers::FindNearest(*Curve, A, Middle, Hit);
		Farthest = FMath::Max(Farthest, Hit.Distance);
	}
	AddInfo(FString::Printf(TEXT("the middles of the sides lie up to %.1f from the line of their river"), Farthest));
	TestTrue(TEXT("The line of a river keeps near its sides"), Farthest < Config.HexRadius * 0.25 + Config.RiverWidth * 0.5);

	// how straight a line runs: how far its points lie from the sides of its river, with no corners cut, with a limit, and straightened more
	const auto GetOffset = [&](double CornerCut, int32 Relax, double MaxOffset)
	{
		HexPlanetRivers::FCurveParams Shape = CurveParams;
		Shape.CornerCut = CornerCut;
		Shape.Relax = Relax;
		Shape.MaxOffset = MaxOffset;
		Shape.Meander = 0.0;
		const TSharedRef<const HexPlanetRivers::FCurve> Shaped = HexPlanetRivers::BuildCurve(Grid, Levels, Net, Shape);
		double Most = 0.0;
		for (int32 Point = 0; Point < Shaped->Points.Num(); ++Point)
		{
			// past a mouth the line goes on into the sea, off its sides, so the cells by the sea are left out
			const int32 Home = Grid.FindCell(Shaped->Points[Point]);
			if (Home == INDEX_NONE || Levels[Home] < 0 || Cells[Home].Neighbors.ContainsByPredicate([&](int32 Neighbor) { return Levels[Neighbor] < 0; }))
			{
				continue;
			}
			double Nearest = TNumericLimits<double>::Max();
			for (const uint64 Key : Edges)
			{
				int32 A, B;
				HexPlanetRivers::GetEdgeCells(Key, A, B);
				const int32 NumSides = Cells[A].Corners.Num();
				const int32 Side = Cells[A].Neighbors.IndexOfByKey(B);
				if (Side == INDEX_NONE || !(Net[A] >> Side & 1))
				{
					continue;
				}
				const FVector3d From = Grid.GetCorners()[Cells[A].Corners[(Side + NumSides - 1) % NumSides]];
				const FVector3d To = Grid.GetCorners()[Cells[A].Corners[Side]];
				const double T = FMath::Clamp(FVector3d::DotProduct(Shaped->Points[Point] - From, To - From) / FMath::Max((To - From).SizeSquared(), 1e-20), 0.0, 1.0);
				Nearest = FMath::Min(Nearest, FVector3d::Distance(Shaped->Points[Point], From + (To - From) * T) * Radius);
			}
			// the end of a river that joins another lies on the line of that one
			const bool bEnd = Point == 0 || !Shaped->Links[Point] || !Shaped->Links[Point - 1];
			Most = bEnd ? Most : FMath::Max(Most, Nearest);
		}
		return Most;
	};
	const double AlongSides = GetOffset(0.0, 0, 0.0);
	const double Straight = GetOffset(1.0, 0, 0.0);
	const double Limited = GetOffset(1.0, 0, 8.0);
	const double Relaxed = GetOffset(1.0, 6, 0.0);
	AddInfo(FString::Printf(TEXT("lines leave their sides by up to %.1f along the sides, %.1f through their middles, %.1f with a limit of 8, %.1f relaxed"),
		AlongSides, Straight, Limited, Relaxed));
	TestTrue(TEXT("RiverCornerCut 0 keeps the line on its sides"), AlongSides < Straight * 0.5);
	TestTrue(TEXT("RiverMaxOffset limits how far the line leaves its sides"), Limited < 8.0 + 1.0);
	TestTrue(TEXT("RiverRelax straightens the line further"), Relaxed >= Straight - 0.5);

	// a build with the rivers lowers the ground, and a rebuild of the chunks they reach gives what a full build does
	const UHexPlanetKitConfig* KitConfig = LoadObject<UHexPlanetKitConfig>(nullptr, TEXT("/Game/HexPlanet/Kit/DA_HexPlanetKit.DA_HexPlanetKit"));
	const TSharedRef<const HexPlanetSurface::FKit> Kit = HexPlanetSurface::LoadKit(KitConfig, Config.HexRadius);
	const HexPlanetSurface::FParams Params = HexPlanetSurface::FParams::Make(Config, *Kit, Radius);
	const TArray<int32> Massif = HexPlanetSurface::FindMassifSteps(Grid, *Kit, Levels);
	const TArray<int32> AllChunks = HexPlanetSurface::GetAllChunks(Grid);
	TArray<uint16> NoRivers;
	NoRivers.Init(0, Cells.Num());
	const HexPlanetSurface::FBuild Old = HexPlanetSurface::Build(Grid, *Kit, Params, Levels, Massif, AllChunks, 0.0, nullptr, nullptr);
	const HexPlanetSurface::FBuild Full = HexPlanetSurface::Build(Grid, *Kit, Params, Levels, Massif, AllChunks, Old.MeanPeak, nullptr, &Curve.Get());
	const TArray<int32> Chunks = HexPlanetSurface::FindAffectedChunks(Grid, Levels, Levels, Massif, Massif, HexPlanetRivers::FindChangedCells(NoRivers, Net));
	const HexPlanetSurface::FBuild Partial = HexPlanetSurface::Build(Grid, *Kit, Params, Levels, Massif, Chunks, Old.MeanPeak, nullptr, &Curve.Get());

	int32 Lowered = 0;
	for (int32 Index = 0; Index < Full.Sections.Num(); ++Index)
	{
		for (int32 Vertex = 0; Vertex < Full.Sections[Index].Positions.Num() && Vertex < Old.Sections[Index].Positions.Num(); ++Vertex)
		{
			Lowered += Full.Sections[Index].Positions[Vertex].Length() < Old.Sections[Index].Positions[Vertex].Length() - 0.1 ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d vertices lowered into valleys, %d of %d chunks rebuilt"), Lowered, Chunks.Num(), Grid.GetNumChunks()));
	TestTrue(TEXT("The rivers lower the ground"), Lowered > 0);

	for (int32 Slot = 0; Slot < Chunks.Num(); ++Slot)
	{
		for (int32 Part = 0; Part < 2; ++Part)
		{
			const HexPlanetSurface::FSection& A = Partial.Sections[Slot * 2 + Part];
			const HexPlanetSurface::FSection& B = Full.Sections[Chunks[Slot] * 2 + Part];
			bool bSame = A.Positions.Num() == B.Positions.Num();
			for (int32 Vertex = 0; bSame && Vertex < A.Positions.Num(); ++Vertex)
			{
				bSame = A.Positions[Vertex].Equals(B.Positions[Vertex], 1.0e-6) && A.Normals[Vertex].Equals(B.Normals[Vertex], 1.0e-4)
					&& A.Colors[Vertex].Equals(B.Colors[Vertex], 1.0e-4f);
			}
			if (!bSame)
			{
				AddError(FString::Printf(TEXT("Rebuilt section %d differs from the full build"), Chunks[Slot] * 2 + Part));
			}
		}
	}
	return true;
}

#endif
