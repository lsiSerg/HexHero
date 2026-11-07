// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/StaticMesh.h"
#include "HexBiomeConfig.h"
#include "HexCameraPawn.h"
#include "HexCoord.h"
#include "HexInputConfig.h"
#include "HexKeyLegendConfig.h"
#include "HexLevelConfig.h"
#include "HexMapConfig.h"
#include "HexMapData.h"
#include "HexMapLoader.h"
#include "HexMapSettings.h"
#include "HexMeshBuilder.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FHexMapData MakeMap(int32 Width, int32 Height, TArray<int8> Levels)
	{
		FHexMapData Map;
		Map.Width = Width;
		Map.Height = Height;
		Map.Levels = MoveTemp(Levels);
		return Map;
	}

	/** Identifies a mesh edge regardless of its direction */
	uint64 HashEdge(const FVector& A, const FVector& B, TMap<FString, uint64>& PointIds)
	{
		const auto GetPointId = [&PointIds](const FVector& Point)
		{
			const FString Key = FString::Printf(TEXT("%.2f,%.2f,%.2f"), Point.X, Point.Y, Point.Z);
			const uint64* Existing = PointIds.Find(Key);
			return Existing ? *Existing : PointIds.Add(Key, PointIds.Num());
		};

		const uint64 IdA = GetPointId(A);
		const uint64 IdB = GetPointId(B);
		return (FMath::Min(IdA, IdB) << 32) | FMath::Max(IdA, IdB);
	}

	/** On a closed surface two triangles share each edge. Only the bottom of the map walls is open */
	void TestSurfaceIsClosed(FAutomationTestBase& Test, const FHexMeshData& Mesh, const FHexMeshParams& Params)
	{
		Test.TestEqual(TEXT("Triangle list is complete"), Mesh.Triangles.Num() % 3, 0);

		TMap<FString, uint64> PointIds;
		TMap<uint64, int32> EdgeUseCounts;
		TMap<uint64, bool> EdgeIsAtBottom;

		for (int32 Index = 0; Index + 2 < Mesh.Triangles.Num(); Index += 3)
		{
			for (int32 Side = 0; Side < 3; ++Side)
			{
				const FVector& A = Mesh.Vertices[Mesh.Triangles[Index + Side]];
				const FVector& B = Mesh.Vertices[Mesh.Triangles[Index + (Side + 1) % 3]];
				const uint64 Edge = HashEdge(A, B, PointIds);

				EdgeUseCounts.FindOrAdd(Edge)++;
				EdgeIsAtBottom.Add(Edge, FMath::IsNearlyEqual(A.Z, Params.BottomHeight) && FMath::IsNearlyEqual(B.Z, Params.BottomHeight));
			}
		}

		for (const TPair<uint64, int32>& Pair : EdgeUseCounts)
		{
			if (EdgeIsAtBottom[Pair.Key])
			{
				Test.TestEqual(TEXT("Bottom edge is used once"), Pair.Value, 1);
			}
			else
			{
				Test.TestEqual(TEXT("Surface edge is used twice"), Pair.Value, 2);
			}
		}
	}

	/** Levels with relief, so the tests also cover the noise */
	TMap<int8, FHexLevelSurface> MakeSurfaces()
	{
		TMap<int8, FHexLevelSurface> Surfaces;

		for (int8 Level = -2; Level <= 2; ++Level)
		{
			FHexLevelSurface& Surface = Surfaces.Add(Level);
			Surface.BumpHeight = 10.0f;
			Surface.RidgeHeight = Level == 2 ? 100.0f : 0.0f;
		}

		return Surfaces;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexCoordOffsetTest, "HexHero.HexMap.Coord.OffsetRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexCoordOffsetTest::RunTest(const FString& Parameters)
{
	for (int32 Row = 0; Row < 8; ++Row)
	{
		for (int32 Column = 0; Column < 8; ++Column)
		{
			const FHexCoord Coord = FHexCoord::FromOffset(Column, Row);
			TestEqual(TEXT("Column"), Coord.GetColumn(), Column);
			TestEqual(TEXT("Row"), Coord.GetRow(), Row);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexCoordNeighborTest, "HexHero.HexMap.Coord.Neighbors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexCoordNeighborTest::RunTest(const FString& Parameters)
{
	const FHexCoord Center(3, -2);
	const FHexLayout Layout(100.0f);

	for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
	{
		const FHexCoord Neighbor = Center.GetNeighbor(Direction);
		TestEqual(TEXT("Neighbor distance"), Center.DistanceTo(Neighbor), 1);
		TestTrue(TEXT("Opposite direction returns"), Neighbor.GetNeighbor(Direction + 3) == Center);

		// the shared edge of a direction lies between its two corners
		const FVector2D EdgeMiddle = (FHexLayout::GetCornerDirection(Direction) + FHexLayout::GetCornerDirection(Direction + 1)) * 50.0;
		const FVector2D ExpectedOffset = (Layout.HexToWorld(Neighbor) - Layout.HexToWorld(Center)) * 0.5;
		TestTrue(TEXT("Direction matches its corners"), EdgeMiddle.Equals(ExpectedOffset, 0.01));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexLayoutRoundTripTest, "HexHero.HexMap.Layout.WorldRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexLayoutRoundTripTest::RunTest(const FString& Parameters)
{
	const FHexLayout Layout(100.0f);

	for (int32 Row = 0; Row < 16; ++Row)
	{
		for (int32 Column = 0; Column < 16; ++Column)
		{
			const FHexCoord Coord = FHexCoord::FromOffset(Column, Row);
			const FVector2D Center = Layout.HexToWorld(Coord);
			TestTrue(TEXT("Center"), Layout.WorldToHex(Center) == Coord);

			// a point just inside each corner still belongs to the hex
			for (int32 Corner = 0; Corner < FHexCoord::NumDirections; ++Corner)
			{
				TestTrue(TEXT("Near corner"), Layout.WorldToHex(Center + FHexLayout::GetCornerDirection(Corner) * 95.0) == Coord);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexConfigMapFilesTest, "HexHero.HexMap.Config.MapFiles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexConfigMapFilesTest::RunTest(const FString& Parameters)
{
	const TArray<FHexMapEntry> Maps = HexMapLoader::FindMaps();
	const TArray<const UHexLevelConfig*> Levels = GetDefault<UHexMapSettings>()->LoadLevels();
	const TArray<const UHexBiomeConfig*> Biomes = GetDefault<UHexMapSettings>()->LoadBiomes();

	TestFalse(TEXT("There are maps"), Maps.IsEmpty());

	for (const FHexMapEntry& Entry : Maps)
	{
		FHexMapData Map;
		FString Error;
		const bool bLoaded = HexMapLoader::LoadMap(Entry.Folder, Levels, Biomes, Map, Error);
		TestTrue(*FString::Printf(TEXT("Map %s loads: %s"), *Entry.Folder, *Error), bLoaded);
		TestEqual(*FString::Printf(TEXT("Every hex of %s has a biome"), *Entry.Folder), Map.Biomes.Num(), Map.Levels.Num());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexCameraPitchTest, "HexHero.HexMap.Camera.PitchByZoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexCameraPitchTest::RunTest(const FString& Parameters)
{
	const AHexCameraPawn* Pawn = GetDefault<AHexCameraPawn>();

	TestEqual(TEXT("Far away it looks down steeply"), Pawn->GetPitchAtZoom(6000.0f), 60.0f, 0.01f);
	TestEqual(TEXT("It starts to lower at its distance"), Pawn->GetPitchAtZoom(2500.0f), 60.0f, 0.01f);
	TestEqual(TEXT("It is lowest at its closest"), Pawn->GetPitchAtZoom(600.0f), 38.0f, 0.01f);
	TestTrue(TEXT("Between them it is lower when closer"), Pawn->GetPitchAtZoom(1000.0f) < Pawn->GetPitchAtZoom(1800.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexConfigLevelsTest, "HexHero.HexMap.Config.LevelsAndBiomes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexConfigLevelsTest::RunTest(const FString& Parameters)
{
	const TArray<const UHexLevelConfig*> Levels = GetDefault<UHexMapSettings>()->LoadLevels();
	const TArray<const UHexBiomeConfig*> Biomes = GetDefault<UHexMapSettings>()->LoadBiomes();

	TestEqual(TEXT("Number of levels"), Levels.Num(), 5);

	for (int32 Index = 0; Index + 1 < Levels.Num(); ++Index)
	{
		TestTrue(TEXT("Levels are different and sorted"), Levels[Index]->Level < Levels[Index + 1]->Level);
	}

	FString Error;
	const bool bValid = HexMapLoader::ValidateBiomes(Biomes, Levels, Error);
	TestTrue(*FString::Printf(TEXT("The biomes are valid: %s"), *Error), bValid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexDataPixelsTest, "HexHero.HexMap.Loader.HexData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexDataPixelsTest::RunTest(const FString& Parameters)
{
	UHexBiomeConfig* Biome = NewObject<UHexBiomeConfig>();
	Biome->TerrainLayer = 3;
	Biome->MountainRockLayer = 5;
	Biome->SnowLine = 100.0f;
	const TArray<const UHexBiomeConfig*> Biomes = { Biome };

	FHexMapData Map = MakeMap(2, 1, { -2, 2 });
	Map.Biomes = { 0, 0 };

	const TArray<FColor> Pixels = HexMapLoader::BuildHexData(Map, Biomes);
	TestEqual(TEXT("One pixel per hex"), Pixels.Num(), 2);
	TestEqual(TEXT("Terrain layer"), static_cast<int32>(Pixels[0].R), 3);
	TestEqual(TEXT("Rock layer"), static_cast<int32>(Pixels[0].B), 5);
	TestEqual(TEXT("Low level"), static_cast<int32>(Pixels[0].G), 126);
	TestEqual(TEXT("High level"), static_cast<int32>(Pixels[1].G), 130);
	TestEqual(TEXT("Snow line on land"), static_cast<int32>(Pixels[1].A), 150);
	TestEqual(TEXT("Opacity on water"), static_cast<int32>(Pixels[0].A), 127);

	Biome->bFrozen = true;
	TestEqual(TEXT("Frozen water"), static_cast<int32>(HexMapLoader::BuildHexData(Map, Biomes)[0].A), 255);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexManifestTest, "HexHero.HexMap.Loader.Manifest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexManifestTest::RunTest(const FString& Parameters)
{
	FHexMapManifest Manifest;
	FString Error;

	TestTrue(TEXT("A valid description parses"), HexMapLoader::ParseManifest(TEXT("{\"version\": 1, \"name\": \"World\", \"width\": 16, \"height\": 8, \"sortOrder\": 3, \"future\": true}"), Manifest, Error));
	TestEqual(TEXT("Name"), Manifest.Name, FString(TEXT("World")));
	TestEqual(TEXT("Width"), Manifest.Width, 16);
	TestEqual(TEXT("Height"), Manifest.Height, 8);
	TestEqual(TEXT("Sort order"), Manifest.SortOrder, 3);

	TestFalse(TEXT("Broken JSON fails"), HexMapLoader::ParseManifest(TEXT("{ not json"), Manifest, Error));
	TestFalse(TEXT("Another version fails"), HexMapLoader::ParseManifest(TEXT("{\"version\": 99, \"name\": \"World\"}"), Manifest, Error));
	TestTrue(TEXT("The error names the version"), Error.Contains(TEXT("99")));
	TestFalse(TEXT("A missing name fails"), HexMapLoader::ParseManifest(TEXT("{\"version\": 1}"), Manifest, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexLoaderBiomesTest, "HexHero.HexMap.Loader.Biomes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexLoaderBiomesTest::RunTest(const FString& Parameters)
{
	UHexLevelConfig* Water = NewObject<UHexLevelConfig>();
	Water->Level = -1;
	UHexLevelConfig* Land = NewObject<UHexLevelConfig>();
	Land->Level = 0;
	const TArray<const UHexLevelConfig*> Levels = { Water, Land };

	UHexBiomeConfig* Sea = NewObject<UHexBiomeConfig>();
	Sea->FileColor = FColor(0, 0, 200);
	Sea->AllowedLevels = { -1 };
	Sea->DefaultForLevels = { -1 };
	UHexBiomeConfig* Grass = NewObject<UHexBiomeConfig>();
	Grass->FileColor = FColor(0, 200, 0);
	Grass->AllowedLevels = { 0 };
	Grass->DefaultForLevels = { 0 };
	UHexBiomeConfig* Ice = NewObject<UHexBiomeConfig>();
	Ice->FileColor = FColor(200, 200, 200);
	Ice->AllowedLevels = { -1 };
	const TArray<const UHexBiomeConfig*> Biomes = { Sea, Grass, Ice };

	FString Error;
	TestTrue(TEXT("Valid biomes pass"), HexMapLoader::ValidateBiomes(Biomes, Levels, Error));

	FHexMapData Map = MakeMap(2, 1, { -1, 0 });
	TestTrue(TEXT("Defaults are found"), HexMapLoader::SetDefaultBiomes(Biomes, Map, Error));
	TestEqual(TEXT("Default on water"), static_cast<int32>(Map.Biomes[0]), 0);
	TestEqual(TEXT("Default on land"), static_cast<int32>(Map.Biomes[1]), 1);

	const TArray<FColor> Pixels = { FColor(200, 200, 200), FColor(0, 200, 0, 0) };
	TestTrue(TEXT("Biome pixels decode"), HexMapLoader::DecodeBiomes(Pixels, 2, 1, Biomes, Map, Error));
	TestEqual(TEXT("Ice on water"), static_cast<int32>(Map.Biomes[0]), 2);
	TestEqual(TEXT("Alpha is ignored"), static_cast<int32>(Map.Biomes[1]), 1);

	const TArray<FColor> IceOnLand = { FColor(0, 0, 200), FColor(200, 200, 200) };
	TestFalse(TEXT("A biome on a level it does not allow fails"), HexMapLoader::DecodeBiomes(IceOnLand, 2, 1, Biomes, Map, Error));
	TestTrue(TEXT("Error names the pixel"), Error.Contains(TEXT("(1, 0)")));

	const TArray<FColor> Unknown = { FColor(1, 2, 3), FColor(0, 200, 0) };
	TestFalse(TEXT("An unknown color fails"), HexMapLoader::DecodeBiomes(Unknown, 2, 1, Biomes, Map, Error));
	TestFalse(TEXT("Another size fails"), HexMapLoader::DecodeBiomes(Pixels, 1, 2, Biomes, Map, Error));

	Ice->FileColor = Sea->FileColor;
	TestFalse(TEXT("Biomes with one color fail"), HexMapLoader::ValidateBiomes(Biomes, Levels, Error));
	Ice->FileColor = FColor(200, 200, 200);

	Ice->DefaultForLevels = { -1 };
	TestFalse(TEXT("Two defaults of a level fail"), HexMapLoader::ValidateBiomes(Biomes, Levels, Error));
	Ice->DefaultForLevels.Reset();

	Grass->DefaultForLevels.Reset();
	TestFalse(TEXT("A level without a default fails"), HexMapLoader::ValidateBiomes(Biomes, Levels, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexLoaderRiversTest, "HexHero.HexMap.Loader.Rivers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexLoaderRiversTest::RunTest(const FString& Parameters)
{
	FString Error;

	// a straight river from the hills east into the sea
	FHexMapData Map = MakeMap(5, 1, { 1, 1, 0, 0, -1 });
	TestTrue(*FString::Printf(TEXT("A straight river parses: %s"), *Error), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"name\": \"East\", \"hexes\": [[0, 0], [1, 0], [2, 0], [3, 0]]}]}"), Map, Error));
	TestEqual(TEXT("Source flows east"), static_cast<int32>(Map.Rivers[0].Outflow), 1);
	TestEqual(TEXT("Source has no inflow"), static_cast<int32>(Map.Rivers[0].InflowMask), 0);
	TestEqual(TEXT("Mouth flows east"), static_cast<int32>(Map.Rivers[3].Outflow), 1);
	TestEqual(TEXT("Mouth is fed from the west"), static_cast<int32>(Map.Rivers[3].InflowMask), 1 << 4);
	TestTrue(TEXT("Mouth is marked"), Map.Rivers[3].bMouth && !Map.Rivers[2].bMouth);
	TestEqual(TEXT("Mouth size counts the river"), Map.Rivers[3].Size, 4);
	TestFalse(TEXT("Water has no river"), Map.Rivers[4].HasRiver());
	TestEqual(TEXT("The name is kept"), Map.RiverPaths[0].Name, FString(TEXT("East")));
	TestEqual(TEXT("A hex knows its river"), Map.Rivers[1].Path, 0);

	TestTrue(TEXT("A map without rivers parses"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": []}"), Map, Error));
	TestTrue(TEXT("It keeps no rivers"), Map.Rivers.IsEmpty() && Map.RiverPaths.IsEmpty() && !Map.GetRiver(FHexCoord::FromOffset(0, 0)).HasRiver());

	// two rivers may run side by side, and a tributary ends in a hex of the river it meets
	Map = MakeMap(5, 2, { 0, 0, 0, 0, -1, 0, 0, 0, 0, -1 });
	TestTrue(*FString::Printf(TEXT("Rivers side by side parse: %s"), *Error), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0], [2, 0], [3, 0]]}, {\"hexes\": [[0, 1], [1, 1], [2, 1], [3, 1]]}]}"), Map, Error));
	TestEqual(TEXT("Neither is a tributary"), Map.RiverPaths[1].Joins, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("The second flows east"), static_cast<int32>(Map.Rivers[6].Outflow), 1);
	TestEqual(TEXT("The first does not flow into the second"), static_cast<int32>(Map.Rivers[7].InflowMask), 1 << 4);

	Map = MakeMap(5, 2, { 0, 0, 0, 0, -1, 0, 0, 0, 0, 0 });
	TestTrue(*FString::Printf(TEXT("A tributary parses: %s"), *Error), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0], [2, 0], [3, 0]]}, {\"hexes\": [[1, 1], [2, 0]], \"meander\": 0.5, \"seed\": 7}]}"), Map, Error));
	TestEqual(TEXT("It joins the river"), Map.RiverPaths[1].Joins, 0);
	TestEqual(TEXT("Both feed the hex they meet in"), static_cast<int32>(Map.Rivers[2].InflowMask), (1 << 4) | (1 << 3));
	TestEqual(TEXT("The mouth counts both"), Map.Rivers[3].Size, 5);
	TestEqual(TEXT("The hex they meet in flows on in the river"), Map.Rivers[2].Path, 0);
	TestTrue(TEXT("Its values are read"), Map.RiverPaths[1].Meander == 0.5f && Map.RiverPaths[1].Seed == 7);

	// every river has the width of DA_HexMap, so bridges fit any of them
	TestFalse(TEXT("A width of its own fails"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0], [2, 0], [3, 0]], \"width\": 40}]}"), Map, Error));
	TestTrue(TEXT("Error names the width"), Error.Contains(TEXT("RiverWidth")));
	TestFalse(TEXT("A pool fails"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0], [2, 0], [3, 0]], \"pools\": []}]}"), Map, Error));

	// a river cannot be on a level with meshes, such as mountains
	UHexLevelConfig* Plain = NewObject<UHexLevelConfig>();
	Plain->Level = 0;
	UHexLevelConfig* Mountains = NewObject<UHexLevelConfig>();
	Mountains->Level = 2;
	Mountains->DisplayName = TEXT("Mountains");
	Mountains->JoinedMeshes.Add(9, TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Missing.Missing"))));
	const TArray<const UHexLevelConfig*> RiverLevels = { Plain, Mountains };

	Map = MakeMap(3, 1, { 2, 0, -1 });
	TestTrue(*FString::Printf(TEXT("A river from a mountain parses: %s"), *Error), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0]]}]}"), Map, Error));
	TestFalse(TEXT("A river on a mountain fails"), HexMapLoader::ValidateRiverLevels(Map, RiverLevels, Error));
	TestTrue(TEXT("Error names the pixel"), Error.Contains(TEXT("(0, 0)")));
	TestTrue(TEXT("A river next to it passes"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[1, 0]]}]}"), Map, Error) && HexMapLoader::ValidateRiverLevels(Map, RiverLevels, Error));

	// what makes a file wrong, and the errors name the hex
	Map = MakeMap(4, 2, { 0, 1, 0, -1, 0, 0, 0, 0 });
	TestFalse(TEXT("Broken JSON fails"), HexMapLoader::ParseRivers(TEXT("{ not json"), Map, Error));
	TestFalse(TEXT("A file without rivers fails"), HexMapLoader::ParseRivers(TEXT("{}"), Map, Error));
	TestFalse(TEXT("A river that flows uphill fails"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0], [2, 0]]}]}"), Map, Error));
	TestTrue(TEXT("Error names the hexes"), Error.Contains(TEXT("(0, 0)")) && Error.Contains(TEXT("(1, 0)")));
	TestFalse(TEXT("A river that skips a hex fails"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 1], [2, 1]]}]}"), Map, Error));
	TestFalse(TEXT("A river that ends nowhere fails"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 1], [1, 1]]}]}"), Map, Error));
	TestFalse(TEXT("A river on water fails"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[3, 0]]}]}"), Map, Error));
	TestFalse(TEXT("A river off the map fails"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[9, 9]]}]}"), Map, Error));
	TestFalse(TEXT("Two rivers through one hex fail"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[2, 1], [2, 0]]}, {\"hexes\": [[1, 1], [2, 1], [3, 1]]}]}"), Map, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshRiversTest, "HexHero.HexMap.Mesh.Rivers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshRiversTest::RunTest(const FString& Parameters)
{
	const TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();
	FHexMeshParams Params;
	FString Error;

	// a river from the hills east into the sea, and a tributary from the south-west that meets it
	FHexMapData Map = MakeMap(6, 2, { 1, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0 });
	TestTrue(*FString::Printf(TEXT("Rivers parse: %s"), *Error), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0], [2, 0], [3, 0], [4, 0]]}, {\"hexes\": [[2, 1], [3, 0]]}]}"), Map, Error));

	FHexRiverData Rivers;
	HexMeshBuilder::BuildRivers(Map, Params, Surfaces, nullptr, Rivers);
	TestEqual(TEXT("A line for the river and one for the tributary"), Rivers.Lines.Num(), 2);

	const FHexRiverLine* Main = Rivers.Lines.FindByPredicate([](const FHexRiverLine& Line) { return Line.bEndsAtMouth; });
	const FHexRiverLine* Tributary = Rivers.Lines.FindByPredicate([](const FHexRiverLine& Line) { return !Line.bEndsAtMouth; });

	if (!Main || !Tributary)
	{
		AddError(TEXT("The river or the tributary is missing"));
		return true;
	}

	for (const FHexRiverLine& Line : Rivers.Lines)
	{
		for (int32 Index = 0; Index + 1 < Line.Points.Num(); ++Index)
		{
			TestTrue(TEXT("The water never rises downstream"), Line.Points[Index + 1].Surface <= Line.Points[Index].Surface + UE_KINDA_SMALL_NUMBER);
			TestTrue(TEXT("Points are close together"), FVector2D::Distance(Line.Points[Index].Position, Line.Points[Index + 1].Position) < 20.0);
		}

		TestTrue(TEXT("The water stays above the sea"), Line.Points.Last().Surface >= Params.WaterHeight - UE_KINDA_SMALL_NUMBER);
	}

	// every river has one width, narrowing only towards its source and where a tributary ends, and keeps it out into the sea
	const TArray<FHexRiverPoint>& MainPoints = Main->Points;
	TestEqual(TEXT("It is narrow at its source"), MainPoints[0].Width, Params.RiverWidth * 0.3, 0.01);
	TestEqual(TEXT("It is whole at its mouth"), MainPoints.Last().Width, static_cast<double>(Params.RiverWidth), 0.01);
	TestEqual(TEXT("It is whole in its middle"), MainPoints[MainPoints.Num() / 2].Width, static_cast<double>(Params.RiverWidth), 0.01);
	TestEqual(TEXT("A tributary narrows where it ends"), Tributary->Points.Last().Width, Params.RiverWidth * 0.3, 0.01);

	for (const FHexRiverPoint& Point : MainPoints)
	{
		TestTrue(TEXT("It is never wider"), Point.Width <= Params.RiverWidth + 0.01);
	}

	double ToMain = TNumericLimits<double>::Max();

	for (int32 Index = 0; Index + 1 < Main->Points.Num(); ++Index)
	{
		ToMain = FMath::Min(ToMain, FVector2D::Distance(Tributary->Points.Last().Position,
			FMath::ClosestPointOnSegment2D(Tributary->Points.Last().Position, Main->Points[Index].Position, Main->Points[Index + 1].Position)));
	}

	TestTrue(TEXT("The tributary ends on the river"), ToMain < 0.01);

	// the river cuts the terrain of its hexes: a flat floor under the water, the face of its bank split finely up to one height above the water,
	// and the land beside it untouched
	FHexMeshData Dry;
	FHexMeshData Wet;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 2, 0, 3, 1, Dry);
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 2, 0, 3, 1, Wet, nullptr, &Rivers);
	TestTrue(TEXT("The banks split the terrain"), Wet.Vertices.Num() > Dry.Vertices.Num());

	TMap<FString, double> DryHeights;

	for (int32 Index = 0; Index < Dry.Vertices.Num(); ++Index)
	{
		TestEqual(TEXT("Land without a river has no water"), Dry.Colors[Index].A, 0.0f);

		// the bottom of the walls along the edge of the map lies under the top of the terrain
		if (FMath::IsNearlyEqual(Dry.Vertices[Index].Z, static_cast<double>(Params.BottomHeight)))
		{
			continue;
		}

		DryHeights.Add(FString::Printf(TEXT("%.2f,%.2f"), Dry.Vertices[Index].X, Dry.Vertices[Index].Y), Dry.Vertices[Index].Z);
	}

	// the river a vertex belongs to is the one it lies deepest inside
	const auto FindPlace = [&Rivers](const FVector& Vertex, double& OutDistance, double& OutHalf, double& OutSurface)
	{
		const FVector2D Position(Vertex.X, Vertex.Y);
		double Inside = TNumericLimits<double>::Max();

		for (const FHexRiverLine& Line : Rivers.Lines)
		{
			for (int32 Index = 0; Index + 1 < Line.Points.Num(); ++Index)
			{
				const FHexRiverPoint& A = Line.Points[Index];
				const FHexRiverPoint& B = Line.Points[Index + 1];
				const FVector2D Along = B.Position - A.Position;
				const double Alpha = FMath::Clamp(FVector2D::DotProduct(Position - A.Position, Along) / FMath::Max(Along.SizeSquared(), UE_KINDA_SMALL_NUMBER), 0.0, 1.0);
				const double Distance = FVector2D::Distance(Position, A.Position + Along * Alpha);
				const double Half = FMath::Lerp(A.Width, B.Width, Alpha) * 0.5;

				if (Distance - Half < Inside)
				{
					Inside = Distance - Half;
					OutDistance = Distance;
					OutHalf = Half;
					OutSurface = FMath::Lerp(A.Surface, B.Surface, Alpha);
				}
			}
		}
	};

	int32 OnFloor = 0;
	int32 OnFace = 0;
	int32 AtTop = 0;
	int32 Unchanged = 0;

	// the distance past the top of a bank over which the land goes back to its own height, RiverBankSettle of HexMeshBuilder
	const double RiverBankSettleInTest = 30.0;

	for (int32 Index = 0; Index < Wet.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = Wet.Vertices[Index];
		double Distance = 0.0;
		double Half = 0.0;
		double Surface = 0.0;
		FindPlace(Vertex, Distance, Half, Surface);
		const double Top = Half + Params.RiverBankLean;

		if (Distance < Half - 2.0)
		{
			++OnFloor;
			TestTrue(TEXT("The floor lies under the water"), Vertex.Z < Surface - Params.RiverDepth * 0.5);
			TestTrue(TEXT("The floor is drawn as water"), Wet.Colors[Index].A > 0.9f);
		}
		else if (Distance > Half && Distance < Top - 0.5)
		{
			++OnFace;
		}
		else if (Distance > Top + 0.5 && Distance < Top + 1.5)
		{
			// the land at the top of the bank lies as high above the water on both sides, coming down from the level of the hills
			++AtTop;
			TestEqual(TEXT("The top of the bank lies RiverBankHeight above the water"), Vertex.Z, Surface + Params.RiverBankHeight, 0.6);
		}
		else if (Distance > Top + RiverBankSettleInTest && !FMath::IsNearlyEqual(Vertex.Z, static_cast<double>(Params.BottomHeight)))
		{
			// a point of the lattice past where the land settles back keeps the height it has without the river
			if (const double* DryHeight = DryHeights.Find(FString::Printf(TEXT("%.2f,%.2f"), Vertex.X, Vertex.Y)))
			{
				++Unchanged;
				TestEqual(TEXT("The land beside the bank keeps its height"), Vertex.Z, *DryHeight, 0.01);
			}
		}
	}

	TestTrue(TEXT("Some terrain lies under the water"), OnFloor > 0);
	TestTrue(TEXT("The face of the bank has points of its own"), OnFace > 4);
	TestTrue(TEXT("Some points lie at the top of the bank"), AtTop > 4);
	TestTrue(TEXT("Some land lies beside the bank"), Unchanged > 0);

	// on flat land the water lies just below the ground, not sunk into it, which only a slope down from the hills makes it
	FHexMapData Flat = MakeMap(6, 1, { 0, 0, 0, 0, 0, -1 });
	TestTrue(TEXT("A river on flat land parses"), HexMapLoader::ParseRivers(TEXT("{\"rivers\": [{\"hexes\": [[0, 0], [1, 0], [2, 0], [3, 0], [4, 0]]}]}"), Flat, Error));
	FHexRiverData FlatRivers;
	HexMeshBuilder::BuildRivers(Flat, Params, Surfaces, nullptr, FlatRivers);
	FHexMeshData FlatDry;
	FHexMeshData FlatWet;
	HexMeshBuilder::BuildRange(Flat, Params, Surfaces, 2, 0, 3, 1, FlatDry);
	HexMeshBuilder::BuildRange(Flat, Params, Surfaces, 2, 0, 3, 1, FlatWet, nullptr, &FlatRivers);
	double HighestDry = TNumericLimits<double>::Lowest();
	double HighestWet = TNumericLimits<double>::Lowest();
	double LowestWet = TNumericLimits<double>::Max();
	double LowestDry = TNumericLimits<double>::Max();

	for (const FVector& Vertex : FlatDry.Vertices)
	{
		HighestDry = FMath::Max(HighestDry, Vertex.Z);
		LowestDry = FMath::Min(LowestDry, Vertex.Z);
	}

	for (const FVector& Vertex : FlatWet.Vertices)
	{
		HighestWet = FMath::Max(HighestWet, Vertex.Z);
		LowestWet = FMath::Min(LowestWet, Vertex.Z);
	}

	TestTrue(TEXT("A river never raises the land"), HighestWet <= HighestDry + 0.01);
	TestTrue(*FString::Printf(TEXT("The water is not sunk deep into flat land: %.1f"), LowestDry - LowestWet), LowestDry - LowestWet < Params.RiverDepth + Params.RiverBankHeight + 12.0);

	// the water of the rivers faces up, lies no higher than their water and fades in at the source
	FHexMeshData Water;
	HexMeshBuilder::BuildRiverWater(Map, Params, Surfaces, nullptr, Rivers, Water);
	TestTrue(TEXT("Rivers have water"), Water.Triangles.Num() > 0 && Water.Triangles.Num() % 3 == 0);
	TestEqual(TEXT("Every vertex has coordinates"), Water.UVs.Num(), Water.Vertices.Num());

	double HighestSurface = TNumericLimits<double>::Lowest();

	for (const FHexRiverLine& Line : Rivers.Lines)
	{
		HighestSurface = FMath::Max(HighestSurface, Line.Points[0].Surface);
	}

	for (const FVector& Vertex : Water.Vertices)
	{
		TestTrue(TEXT("Water lies no higher than its source"), Vertex.Z <= HighestSurface + 0.001);
	}

	for (int32 Index = 0; Index + 2 < Water.Triangles.Num(); Index += 3)
	{
		const FVector& A = Water.Vertices[Water.Triangles[Index]];
		const FVector& B = Water.Vertices[Water.Triangles[Index + 1]];
		const FVector& C = Water.Vertices[Water.Triangles[Index + 2]];
		TestTrue(TEXT("Water faces up"), FVector::CrossProduct(C - A, B - A).Z >= 0.0);
	}

	TestEqual(TEXT("Water fades in at the source"), Water.Colors[0].A, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexConfigInputTest, "HexHero.HexMap.Config.Input", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexConfigInputTest::RunTest(const FString& Parameters)
{
	const UHexInputConfig* Config = GetDefault<UHexMapSettings>()->LoadInputConfig();

	if (!TestNotNull(TEXT("The config has a mapping context"), Config->MappingContext.Get()))
	{
		return false;
	}

	const auto CountKeys = [Config](const UInputAction* Action)
	{
		return Config->MappingContext->GetMappings().FilterByPredicate([Action](const FEnhancedActionKeyMapping& Mapping)
		{
			return Mapping.Action == Action;
		}).Num();
	};

	TestNotNull(TEXT("The config has a move action"), Config->MoveAction.Get());
	TestNotNull(TEXT("The config has a map menu action"), Config->MapMenuAction.Get());

	TestEqual(TEXT("Move keys"), CountKeys(Config->MoveAction), 8);
	TestEqual(TEXT("Zoom keys"), CountKeys(Config->ZoomAction), 1);
	TestEqual(TEXT("Grid keys"), CountKeys(Config->ToggleGridAction), 1);
	TestEqual(TEXT("Drag keys"), CountKeys(Config->DragAction), 1);
	TestEqual(TEXT("Turn keys"), CountKeys(Config->TurnAction), 1);
	TestEqual(TEXT("Wireframe mode keys"), CountKeys(Config->WireframeModeAction), 1);
	TestEqual(TEXT("Wireframe scope keys"), CountKeys(Config->WireframeScopeAction), 1);
	TestEqual(TEXT("Wireframe target keys"), CountKeys(Config->WireframeTargetAction), 1);
	TestEqual(TEXT("Water view keys"), CountKeys(Config->WaterViewAction), 1);
	TestEqual(TEXT("Key legend keys"), CountKeys(Config->KeyLegendAction), 1);

	// the keys of the legend are read from the actions, compactly and with chords
	TestEqual(TEXT("The move keys are compact"), Config->GetKeysText(Config->MoveAction), FString(TEXT("WASD, Arrows")));
	TestEqual(TEXT("A chord shows its modifier"), Config->GetKeysText(Config->WireframeScopeAction), FString(TEXT("Shift+F")));
	TestEqual(TEXT("The camera is copied with Ctrl+C"), Config->GetKeysText(Config->CopyCameraAction), FString(TEXT("Ctrl+C")));
	TestEqual(TEXT("The view is saved with Ctrl+S"), Config->GetKeysText(Config->SaveViewAction), FString(TEXT("Ctrl+S")));

	const UHexKeyLegendConfig* KeyLegend = GetDefault<UHexMapSettings>()->LoadKeyLegend();
	const TArray<FHexLegendSection> Sections = KeyLegend->GetSections(*Config);
	TArray<TPair<FString, FString>> Legend;

	for (const FHexLegendSection& Section : Sections)
	{
		TestFalse(TEXT("Every line of the legend has a heading"), Section.Title.IsEmpty());
		Legend.Append(Section.Lines);
	}

	TestEqual(TEXT("Every line of the legend has keys"), Legend.Num(), KeyLegend->Lines.Num());
	TestTrue(TEXT("The legend has the wireframe hexes"), Legend.Contains(TPair<FString, FString>(TEXT("Wireframe hexes"), TEXT("Shift+F"))));
	TestTrue(TEXT("The legend has the camera copy"), Legend.Contains(TPair<FString, FString>(TEXT("Copy camera"), TEXT("Ctrl+C"))));
	TestTrue(TEXT("The legend has the view save"), Legend.Contains(TPair<FString, FString>(TEXT("Save view"), TEXT("Ctrl+S"))));
	TestEqual(TEXT("The hint names the legend key"), KeyLegend->GetHint(*Config), FString(TEXT("Press H for keys")));

	// Shift+F and Ctrl+F are F chorded with another action, which also keeps plain F from firing with them
	for (const UInputAction* Chorded : { Config->WireframeScopeAction.Get(), Config->WireframeTargetAction.Get() })
	{
		const FEnhancedActionKeyMapping* Mapping = Config->MappingContext->GetMappings().FindByPredicate([Chorded](const FEnhancedActionKeyMapping& Candidate)
		{
			return Candidate.Action == Chorded;
		});

		TestTrue(TEXT("A wireframe action with a modifier is chorded"), Mapping && Mapping->Key == EKeys::F && Mapping->Triggers.ContainsByPredicate([](const UInputTrigger* Trigger)
		{
			return Trigger && Trigger->IsA<UInputTriggerChordAction>();
		}));
	}

	TestTrue(TEXT("Escape closes the menu"), Config->GetMapMenuKeys().Contains(EKeys::Escape));
	TestTrue(TEXT("M closes the menu"), Config->GetMapMenuKeys().Contains(EKeys::M));

	// up on screen is Y, which a key only reaches through a swizzle
	for (const FEnhancedActionKeyMapping& Mapping : Config->MappingContext->GetMappings())
	{
		if (Mapping.Key == EKeys::W)
		{
			TestEqual(TEXT("W has one modifier"), Mapping.Modifiers.Num(), 1);
			TestTrue(TEXT("W moves along Y"), !Mapping.Modifiers.IsEmpty() && Mapping.Modifiers[0]->IsA<UInputModifierSwizzleAxis>());
		}
		// Ctrl+S saves the view, and only the move keys have modifiers
		else if (Mapping.Key == EKeys::S && Mapping.Action == Config->MoveAction)
		{
			TestEqual(TEXT("S has two modifiers"), Mapping.Modifiers.Num(), 2);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexLoaderDecodeTest, "HexHero.HexMap.Loader.Decode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexLoaderDecodeTest::RunTest(const FString& Parameters)
{
	UHexLevelConfig* Low = NewObject<UHexLevelConfig>();
	Low->Level = -1;
	Low->FileColor = FColor(10, 20, 30);
	UHexLevelConfig* High = NewObject<UHexLevelConfig>();
	High->Level = 2;
	High->FileColor = FColor(40, 50, 60);
	const TArray<const UHexLevelConfig*> Levels = { Low, High };

	const TArray<FColor> Pixels = { FColor(10, 20, 30), FColor(40, 50, 60), FColor(40, 50, 60, 0), FColor(10, 20, 30) };

	FHexMapData Map;
	FString Error;
	TestTrue(TEXT("Valid pixels decode"), HexMapLoader::DecodeLevels(Pixels, 2, 2, Levels, Map, Error));
	TestEqual(TEXT("Level at (1, 0)"), static_cast<int32>(Map.GetLevel(1, 0)), 2);
	TestEqual(TEXT("Alpha is ignored"), static_cast<int32>(Map.GetLevel(0, 1)), 2);
	TestEqual(TEXT("Level at (1, 1)"), static_cast<int32>(Map.GetLevel(1, 1)), -1);

	const TArray<FColor> BadPixels = { FColor(10, 20, 30), FColor(1, 2, 3) };
	TestFalse(TEXT("Unknown color fails"), HexMapLoader::DecodeLevels(BadPixels, 2, 1, Levels, Map, Error));
	TestTrue(TEXT("Error names the pixel"), Error.Contains(TEXT("(1, 0)")) && Error.Contains(TEXT("#010203")));

	TestFalse(TEXT("Wrong size fails"), HexMapLoader::DecodeLevels(Pixels, 3, 2, Levels, Map, Error));

	const TArray<FColor> LargePixels;
	TestFalse(TEXT("Too large fails"), HexMapLoader::DecodeLevels(LargePixels, 257, 1, Levels, Map, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshSeamlessTest, "HexHero.HexMap.Mesh.Seamless", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshSeamlessTest::RunTest(const FString& Parameters)
{
	// includes a jump from level -1 to level 2 and rows of both parities
	const FHexMapData Map = MakeMap(4, 4, {
		-2, -1, 0, 1,
		-1, 2, 2, 0,
		0, -1, 2, 1,
		1, 0, -2, 0
	});

	const FHexMeshParams Params;

	FHexMeshData Mesh;
	HexMeshBuilder::BuildRange(Map, Params, MakeSurfaces(), 0, 0, Map.Width, Map.Height, Mesh);

	TestSurfaceIsClosed(*this, Mesh, Params);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshCenterHeightTest, "HexHero.HexMap.Mesh.CenterHeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshCenterHeightTest::RunTest(const FString& Parameters)
{
	const FHexMapData Map = MakeMap(2, 2, {
		-2, 2,
		1, 0
	});

	// without relief and warp a hex center sits exactly at the height of its level
	FHexMeshParams Params;
	Params.WarpDistance = 0.0f;

	FHexMeshData Mesh;
	HexMeshBuilder::BuildRange(Map, Params, {}, 0, 0, Map.Width, Map.Height, Mesh);

	const FHexLayout Layout(Params.HexRadius);

	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		for (int32 Column = 0; Column < Map.Width; ++Column)
		{
			const FVector2D Center = Layout.HexToWorld(FHexCoord::FromOffset(Column, Row));
			const FVector* Vertex = Mesh.Vertices.FindByPredicate([&Center](const FVector& Position)
			{
				return Center.Equals(FVector2D(Position.X, Position.Y), 0.01);
			});

			if (TestNotNull(TEXT("Hex center has a vertex"), Vertex))
			{
				TestEqual(TEXT("Hex center height"), Vertex->Z, static_cast<double>(Map.GetLevel(Column, Row) * Params.LevelHeight), 0.01);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshChunksTest, "HexHero.HexMap.Mesh.ChunksMatchFullMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshChunksTest::RunTest(const FString& Parameters)
{
	const FHexMapData Map = MakeMap(4, 2, {
		0, 1, 2, -1,
		-2, 0, 1, 2
	});

	const FHexMeshParams Params;
	const TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();

	FHexMeshData Full;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 0, 0, 4, 2, Full);

	FHexMeshData Chunks;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 0, 0, 2, 2, Chunks);
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 2, 0, 4, 2, Chunks);

	TestEqual(TEXT("Chunks build the same number of triangles"), Chunks.Triangles.Num(), Full.Triangles.Num());

	// a gap between the chunks would leave edges that are used once
	TestSurfaceIsClosed(*this, Chunks, Params);

	// a different normal on each side of the border would show as a visible line
	TMap<FString, FVector> NormalsByPosition;

	for (int32 Index = 0; Index < Chunks.Vertices.Num(); ++Index)
	{
		const FVector& Position = Chunks.Vertices[Index];

		// walls have their own vertices, which face away from the map
		if (FMath::Abs(Chunks.Normals[Index].Z) < UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FString Key = FString::Printf(TEXT("%.2f,%.2f,%.2f"), Position.X, Position.Y, Position.Z);

		if (const FVector* Normal = NormalsByPosition.Find(Key))
		{
			TestTrue(TEXT("Chunks agree on the normal at their border"), Normal->Equals(Chunks.Normals[Index], 0.0001));
		}
		else
		{
			NormalsByPosition.Add(Key, Chunks.Normals[Index]);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshFootTest, "HexHero.HexMap.Mesh.JoinedMeshFoot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshFootTest::RunTest(const FString& Parameters)
{
	// a mountain next to deep water and land, all lower than its foot
	const FHexMapData Map = MakeMap(5, 5, {
		-2, -2, -2, 0, 0,
		-2, -2, 2, 0, 0,
		-2, 0, 2, 2, 0,
		0, 0, 0, 0, 0,
		0, 0, 0, 0, 0
	});

	const FHexMeshParams Params;
	TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();
	Surfaces[0].BumpHeight = 0.0f;
	FHexLevelSurface& Mountain = Surfaces[2];
	Mountain.bHasJoinedMesh = true;
	Mountain.FootHeight = 5.0f;
	Mountain.Color = FLinearColor::Red;

	FHexMeshData Mesh;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 0, 0, Map.Width, Map.Height, Mesh);

	FHexTerrainHeights Terrain;
	HexMeshBuilder::BuildTerrainHeights(Map, Params, Surfaces, nullptr, Terrain);

	const FHexLayout Layout(Params.HexRadius);
	const double LatticeStep = Params.HexRadius / Params.Subdivisions;
	int32 EdgeVertices = 0;

	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		for (int32 Column = 0; Column < Map.Width; ++Column)
		{
			if (Map.GetLevel(Column, Row) == 2)
			{
				const FVector2D Center = Layout.HexToWorld(FHexCoord::FromOffset(Column, Row));

				for (const FVector& Vertex : Mesh.Vertices)
				{
					TestTrue(TEXT("No terrain inside a hex with a mesh"), FVector2D::Distance(FVector2D(Vertex.X, Vertex.Y), Center) > Params.HexRadius * 0.8);
				}
			}
		}
	}

	// what a mesh reads is exactly the terrain, at every vertex next to a mountain
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		const FVector& Vertex = Mesh.Vertices[Index];
		const FHexCoord Hex = Layout.WorldToHex(FVector2D(Vertex.X, Vertex.Y));
		bool bNextToMountain = false;

		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			const FHexCoord Neighbor = Hex.GetNeighbor(Direction);
			bNextToMountain |= Map.Contains(Neighbor) && Map.GetLevel(Neighbor) == 2;
		}

		if (bNextToMountain && Map.Contains(Hex) && FMath::Abs(Mesh.Normals[Index].Z) > UE_KINDA_SMALL_NUMBER)
		{
			const int32 Y = FMath::RoundToInt32(Vertex.Y / (FMath::Sqrt(3.0) * 0.5 * LatticeStep));
			const int32 X = FMath::RoundToInt32(Vertex.X / LatticeStep - Y * 0.5);
			++EdgeVertices;
			TestEqual(TEXT("The height for meshes is that of the terrain"), static_cast<double>(Terrain.Heights[Terrain.GetIndex(FIntPoint(X, Y))]), Vertex.Z, 0.01);
		}
	}

	TestTrue(TEXT("The hexes have edges"), EdgeVertices > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshFootHillsTest, "HexHero.HexMap.Mesh.JoinedMeshFootHills", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshFootHillsTest::RunTest(const FString& Parameters)
{
	// hills that are higher than the foot of the mountain next to them
	const FHexMapData Map = MakeMap(5, 1, { 1, 1, 2, 1, 1 });

	FHexMeshParams Params;
	Params.WarpDistance = 0.0f;
	TMap<int8, FHexLevelSurface> Surfaces;
	Surfaces.Add(1);
	FHexLevelSurface& Mountain = Surfaces.Add(2);
	Mountain.bHasJoinedMesh = true;
	Mountain.FootHeight = 5.0f;

	FHexMeshData Mesh;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 0, 0, Map.Width, Map.Height, Mesh);

	const FHexLayout Layout(Params.HexRadius);
	const FVector2D Hills = Layout.HexToWorld(FHexCoord::FromOffset(1, 0));
	const FVector2D Peak = Layout.HexToWorld(FHexCoord::FromOffset(2, 0));
	TMap<int32, double> HeightByStep;

	// walking from the middle of the hills to the mountain the ground only falls, and stays above the foot up to the side between them
	for (const FVector& Vertex : Mesh.Vertices)
	{
		const FVector2D Position(Vertex.X, Vertex.Y);
		const FVector2D Along = (Peak - Hills).GetSafeNormal();
		const double Distance = FVector2D::DotProduct(Position - Hills, Along);
		const double Sideways = FVector2D::Distance(Position, Hills + Along * Distance);

		if (Sideways < 1.0 && Distance >= -0.01 && Distance <= FMath::Sqrt(3.0) * 50.0 + 0.01)
		{
			HeightByStep.Add(FMath::RoundToInt32(Distance), Vertex.Z);
		}
	}

	HeightByStep.KeySort(TLess<int32>());
	TestTrue(TEXT("The line has vertices"), HeightByStep.Num() >= 3);
	double Previous = TNumericLimits<double>::Max();

	for (const TPair<int32, double>& Step : HeightByStep)
	{
		TestTrue(TEXT("No ditch in front of the mountain"), Step.Value <= Previous + 0.01);
		TestTrue(TEXT("The hills stay above the foot"), Step.Value > 5.0);
		Previous = Step.Value;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshRealMapsTest, "HexHero.HexMap.Mesh.NoPitsOnLand", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshRealMapsTest::RunTest(const FString& Parameters)
{
	const TArray<const UHexLevelConfig*> Levels = GetDefault<UHexMapSettings>()->LoadLevels();
	const TArray<const UHexBiomeConfig*> Biomes = GetDefault<UHexMapSettings>()->LoadBiomes();
	const UHexMapConfig* Config = GetDefault<UHexMapSettings>()->LoadMapConfig();

	FHexMeshParams Params;
	Params.CliffMinDifference = Config->CliffMinLevelDifference;
	Params.WaterHeight = Config->WaterHeight;
	Params.CliffSeabedDepth = Config->CliffSeabedDepth;
	Params.CliffBottomHeight = Config->CliffBottomHeight;
	Params.FootBlendDistance = Config->JoinedMeshFootBlend * Config->HexRadius;

	TMap<int8, FHexLevelSurface> Surfaces;

	for (const UHexLevelConfig* Level : Levels)
	{
		FHexLevelSurface& Surface = Surfaces.Add(static_cast<int8>(Level->Level));
		Surface.BumpHeight = Level->BumpHeight;
		Surface.RidgeHeight = Level->RidgeHeight;
		Surface.bHidesTerrain = Level->bHidesTerrain;
		Surface.bHasJoinedMesh = !Level->JoinedMeshes.IsEmpty();
		Surface.FootHeight = Level->Level * Config->LevelHeight + Level->JoinedMeshFootHeight;
		Surface.CliffTop = Surface.bHasJoinedMesh ? Surface.FootHeight : FMath::Max(Level->Level * Config->LevelHeight, Config->WaterHeight + Config->CliffMinRise);
	}

	const FHexLayout Layout(Params.HexRadius);

	for (const TCHAR* Folder : { TEXT("Test16"), TEXT("WaterTest"), TEXT("MountainTest") })
	{
		FHexMapData Map;
		FString Error;

		if (!TestTrue(*FString::Printf(TEXT("%s loads"), Folder), HexMapLoader::LoadMap(Folder, Levels, Biomes, Map, Error)))
		{
			continue;
		}

		FHexCliffData Cliffs;
		HexMeshBuilder::BuildCliffs(Map, Params, Surfaces, Cliffs);

		FHexMeshData Mesh;
		HexMeshBuilder::BuildRange(Map, Params, Surfaces, 0, 0, Map.Width, Map.Height, Mesh, &Cliffs);

		int32 Pits = 0;
		FString FirstPit;

		for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
		{
			const FVector& Vertex = Mesh.Vertices[Index];
			const FVector2D Position(Vertex.X, Vertex.Y);
			const FHexCoord Hex = Layout.WorldToHex(Position);

			if (!Map.Contains(Hex) || FMath::Abs(Mesh.Normals[Index].Z) < UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const int32 Level = Map.GetLevel(Hex);
			const FHexLevelSurface& Surface = Surfaces[static_cast<int8>(Level)];

			// the middle of a hex of open land keeps its level, whatever is around it, apart from its bumps
			if (Level >= 0 && !Surface.bHasJoinedMesh && FVector2D::Distance(Position, Layout.HexToWorld(Hex)) < 45.0
				&& Vertex.Z < Level * Params.LevelHeight - Surface.BumpHeight - 15.0)
			{
				if (Pits++ == 0)
				{
					FirstPit = FString::Printf(TEXT("hex %d, %d at height %.1f"), Hex.GetColumn(), Hex.GetRow(), Vertex.Z);
				}
			}
		}

		TestEqual(*FString::Printf(TEXT("Pits on land on %s, first: %s"), Folder, *FirstPit), Pits, 0);

		// the top of a cliff lies over the terrain, which would otherwise show through it as shards
		double WorstPoke = 0.0;
		FVector WorstPokeAt = FVector::ZeroVector;

		for (const FHexCliffChain& Chain : Cliffs.Chains)
		{
			for (const FHexCliffRing& Ring : Chain.Rings)
			{
				for (const FVector& Vertex : Mesh.Vertices)
				{
					const FVector2D Offset = FVector2D(Vertex.X, Vertex.Y) - Ring.Position;

					if (Offset.SizeSquared() > 40.0 * 40.0)
					{
						continue;
					}

					const double Across = FVector2D::DotProduct(Offset, Ring.Outward);
					const double Sideways = FMath::Abs(FVector2D::CrossProduct(Offset, Ring.Outward));
					const double Poke = Vertex.Z - (Ring.Top + 1.0);

					// where a cliff fades out it is buried on purpose
					if (Ring.Strength > 0.99 && Across > -28.0 && Across < -5.0 && Sideways < 4.0 && Poke > WorstPoke)
					{
						WorstPoke = Poke;
						WorstPokeAt = Vertex;
					}
				}
			}
		}

		TestTrue(*FString::Printf(TEXT("Terrain through the top of a cliff on %s: %.1f at %s"), Folder, WorstPoke, *WorstPokeAt.ToCompactString()), WorstPoke < 0.5);

		// open land has no sudden steps, away from the coast where it falls behind a cliff on purpose
		double WorstSlope = 0.0;
		FVector WorstSlopeAt = FVector::ZeroVector;

		for (int32 Index = 0; Index + 2 < Mesh.Triangles.Num(); Index += 3)
		{
			const FVector& A = Mesh.Vertices[Mesh.Triangles[Index]];
			const FVector& B = Mesh.Vertices[Mesh.Triangles[Index + 1]];
			const FVector& C = Mesh.Vertices[Mesh.Triangles[Index + 2]];
			const FVector Middle = (A + B + C) / 3.0;
			const FVector2D Position(Middle.X, Middle.Y);
			const FHexCoord Hex = Layout.WorldToHex(Position);

			// land that falls to the water is a beach
			if (!Map.Contains(Hex) || Map.GetLevel(Hex) < 0 || Middle.Z < 5.0)
			{
				continue;
			}

			// a joined mesh covers its hex, apart from the strip towards land before the mesh has risen
			if (Surfaces[static_cast<int8>(Map.GetLevel(Hex))].bHasJoinedMesh)
			{
				bool bNextToLand = false;

				for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
				{
					const FHexCoord Near = Layout.WorldToHex(Position + FHexLayout::GetCornerDirection(Direction) * 16.0);
					bNextToLand |= Map.Contains(Near) && !Surfaces[static_cast<int8>(Map.GetLevel(Near))].bHasJoinedMesh;
				}

				if (!bNextToLand)
				{
					continue;
				}
			}

			// the terrain falls behind the top of a cliff, which hides it
			double NearestRing = TNumericLimits<double>::Max();
			bool bBehindCliff = false;

			for (const FHexCliffChain& Chain : Cliffs.Chains)
			{
				for (const FHexCliffRing& Ring : Chain.Rings)
				{
					const double Distance = FVector2D::Distance(Position, Ring.Position);

					if (Distance < NearestRing)
					{
						NearestRing = Distance;
						bBehindCliff = FVector2D::DotProduct(Position - Ring.Position, Ring.Outward) > -30.0;
					}
				}
			}

			const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();

			if (!bBehindCliff && FMath::Abs(Normal.Z) > UE_KINDA_SMALL_NUMBER)
			{
				const double Slope = FVector2D(Normal.X, Normal.Y).Size() / FMath::Abs(Normal.Z);

				if (Slope > WorstSlope)
				{
					WorstSlope = Slope;
					WorstSlopeAt = Middle;
				}
			}
		}

		TestTrue(*FString::Printf(TEXT("Slope of open land on %s: %.2f at %s"), Folder, WorstSlope, *WorstSlopeAt.ToCompactString()), WorstSlope < 2.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexJoinedExtraTurnsTest, "HexHero.HexMap.Mesh.JoinedExtraTurns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexJoinedExtraTurnsTest::RunTest(const FString& Parameters)
{
	TSet<int32> TurnsOfFullSet;

	for (int32 Q = 0; Q < 20; ++Q)
	{
		const FHexCoord Hex(Q, Q * 3);
		TurnsOfFullSet.Add(HexMeshBuilder::GetJoinedExtraTurns(0b111111, Hex));
		TestEqual(TEXT("A set without symmetry cannot turn"), HexMeshBuilder::GetJoinedExtraTurns(0b000011, Hex), 0);
		TestEqual(TEXT("A straight line can only turn right round"), HexMeshBuilder::GetJoinedExtraTurns(0b001001, Hex) % 3, 0);
		TestEqual(TEXT("Every second neighbor turns by two"), HexMeshBuilder::GetJoinedExtraTurns(0b010101, Hex) % 2, 0);
	}

	TestTrue(TEXT("Hexes differ"), TurnsOfFullSet.Num() > 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMassifDepthsTest, "HexHero.HexMap.Mesh.MassifDepths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMassifDepthsTest::RunTest(const FString& Parameters)
{
	// a block of mountains inside land, two hexes deep in its middle
	const FHexMapData Map = MakeMap(7, 7, {
		0, 0, 0, 0, 0, 0, 0,
		0, 2, 2, 2, 2, 2, 0,
		0, 2, 2, 2, 2, 2, 0,
		0, 2, 2, 2, 2, 2, 0,
		0, 2, 2, 2, 2, 2, 0,
		0, 2, 2, 2, 2, 2, 0,
		0, 0, 0, 0, 0, 0, 0
	});

	TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();
	Surfaces[2].bHasJoinedMesh = true;

	const TArray<uint8> Depths = HexMeshBuilder::BuildMassifDepths(Map, Surfaces);
	TestEqual(TEXT("Land has no depth"), static_cast<int32>(Depths[0]), 0);
	TestEqual(TEXT("The edge of the group"), static_cast<int32>(Depths[1 * 7 + 1]), 0);
	TestEqual(TEXT("One hex in"), static_cast<int32>(Depths[2 * 7 + 2]), 1);
	TestEqual(TEXT("The middle"), static_cast<int32>(Depths[3 * 7 + 3]), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexShoreFieldTest, "HexHero.HexMap.Mesh.ShoreField", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexShoreFieldTest::RunTest(const FString& Parameters)
{
	// land in the west and deep water in the east, along a whole row
	const FHexMapData Map = MakeMap(8, 1, { 0, 0, -2, -2, -2, -2, -2, -2 });

	FHexMeshParams Params;
	Params.WarpDistance = 0.0f;
	TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();
	Surfaces[0].BumpHeight = 0.0f;

	const FHexLayout Layout(Params.HexRadius);
	const FBox2D Bounds(FVector2D(-100.0, -100.0), FVector2D(100.0, 1400.0));

	FHexShoreField Field;
	HexMeshBuilder::BuildShoreField(Map, Params, Surfaces, nullptr, Bounds, Field);

	TestEqual(TEXT("One pixel for each part of the map"), Field.Pixels.Num(), Field.Width * Field.Height);

	const auto PixelAt = [&Field](const FVector2D& Position) -> const FColor&
	{
		const int32 X = FMath::FloorToInt32((Position.X - Field.Origin.X) / Field.Step);
		const int32 Y = FMath::FloorToInt32((Position.Y - Field.Origin.Y) / Field.Step);
		return Field.Pixels[Y * Field.Width + X];
	};

	TestEqual(TEXT("No distance in the middle of the land"), static_cast<int32>(PixelAt(Layout.HexToWorld(FHexCoord::FromOffset(0, 0))).R), 0);

	// going east the water only gets further from the land
	int32 Previous = -1;

	for (int32 Column = 2; Column <= 4; ++Column)
	{
		const int32 Distance = PixelAt(Layout.HexToWorld(FHexCoord::FromOffset(Column, 0))).G;
		TestTrue(TEXT("Distance grows away from the land"), Distance > Previous);
		Previous = Distance;
	}

	TestEqual(TEXT("No cliff on this map"), static_cast<int32>(PixelAt(Layout.HexToWorld(FHexCoord::FromOffset(3, 0))).B), 0);

	// the height has its own pixels, above the middle value on land and below it in the water, and the edge lies between
	const auto HeightAt = [&Field](const FVector2D& Position)
	{
		const int32 X = FMath::FloorToInt32((Position.X - Field.Origin.X) / Field.Step);
		const int32 Y = FMath::FloorToInt32((Position.Y - Field.Origin.Y) / Field.Step);
		return static_cast<int32>(Field.HeightPixels[Y * Field.Width + X]);
	};

	TestEqual(TEXT("One height for each pixel"), Field.HeightPixels.Num(), Field.Pixels.Num());
	TestTrue(TEXT("Land lies above the water"), HeightAt(Layout.HexToWorld(FHexCoord::FromOffset(0, 0))) > 32768);
	TestTrue(TEXT("Deep water lies below it"), HeightAt(Layout.HexToWorld(FHexCoord::FromOffset(4, 0))) < 32768);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexJoinedNotchTest, "HexHero.HexMap.Mesh.JoinedNotch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexJoinedNotchTest::RunTest(const FString& Parameters)
{
	const FHexLayout Layout(100.0f);
	const FHexCoord Hex = FHexCoord::FromOffset(4, 3);
	const FVector2D Center = Layout.HexToWorld(Hex);

	for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
	{
		const FHexCoord Mountain = Hex.GetNeighbor(Direction);
		const FHexCoord Lower = Hex.GetNeighbor(Direction + 1);

		// from each of the two mountains the same corner is found once, so one mesh stands on it
		const int32 Mask = 1 << Direction;
		TestTrue(TEXT("This hex has the notch"), HexMeshBuilder::HasJoinedNotch(Mask, Direction));

		bool bOtherHasIt = false;

		for (int32 OtherDirection = 0; OtherDirection < FHexCoord::NumDirections; ++OtherDirection)
		{
			if (Mountain.GetNeighbor(OtherDirection) == Hex)
			{
				bOtherHasIt = HexMeshBuilder::HasJoinedNotch(1 << OtherDirection, OtherDirection) && Mountain.GetNeighbor(OtherDirection + 1) == Lower;
			}
		}

		TestFalse(TEXT("The neighbor does not place it again"), bOtherHasIt);

		const FTransform Transform = HexMeshBuilder::GetJoinedNotchTransform(Hex, Direction, 100.0f, 100.0f, 0.0);
		const FVector2D LowerCenter = Layout.HexToWorld(Lower);
		const FVector Pivot = Transform.TransformPosition(FVector::ZeroVector);
		const FVector Forward = Transform.TransformPosition(FVector(100.0, 0.0, 0.0));

		TestEqual(TEXT("The pivot is on a corner of the lower hex"), FVector2D::Distance(FVector2D(Pivot.X, Pivot.Y), LowerCenter), 100.0, 0.01);
		TestEqual(TEXT("The pivot is on a corner of this hex"), FVector2D::Distance(FVector2D(Pivot.X, Pivot.Y), Center), 100.0, 0.01);
		TestEqual(TEXT("The mesh points to the lower hex X"), Forward.X, LowerCenter.X, 0.01);
		TestEqual(TEXT("The mesh points to the lower hex Y"), Forward.Y, LowerCenter.Y, 0.01);
	}

	TestFalse(TEXT("No notch between two mountains"), HexMeshBuilder::HasJoinedNotch(0b000011, 0));
	TestTrue(TEXT("A notch after the last of them"), HexMeshBuilder::HasJoinedNotch(0b000011, 1));

	// a row of mountains with land above it, so the land has a covered corner
	const FHexMapData Map = MakeMap(5, 4, {
		0, 0, 0, 0, 0,
		0, 2, 2, 2, 0,
		0, 0, 0, 0, 0,
		0, 0, 0, 0, 0
	});

	const FHexMeshParams Params;
	TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();
	Surfaces[2].bHasJoinedMesh = true;
	Surfaces[2].FootHeight = 5.0f;

	FHexMeshData Mesh;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 0, 0, Map.Width, Map.Height, Mesh);

	// the corner of the land between mountains (1, 1) and (2, 1) and the first terrain vertex inside the land from it
	const FVector2D CenterA = Layout.HexToWorld(FHexCoord::FromOffset(1, 1));
	const FVector2D CenterB = Layout.HexToWorld(FHexCoord::FromOffset(2, 1));
	const FHexCoord Land = FHexCoord::FromOffset(2, 0);
	const FVector2D LandCenter = Layout.HexToWorld(Land);
	const FVector2D Corner = (CenterA + CenterB) * 0.5 + (LandCenter - (CenterA + CenterB) * 0.5).GetSafeNormal() * 50.0;
	const FVector2D Inside = Corner + (LandCenter - Corner).GetSafeNormal() * 100.0 / Params.Subdivisions;

	const FVector* Covered = Mesh.Vertices.FindByPredicate([&Inside](const FVector& Position)
	{
		return Inside.Equals(FVector2D(Position.X, Position.Y), 0.01);
	});

	if (TestNotNull(TEXT("The land has a vertex there"), Covered))
	{
		TestEqual(TEXT("The land under the base of a mountain keeps its height"), Covered->Z, 0.0, 0.01);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexMeshDeepWaterTest, "HexHero.HexMap.Mesh.DeepWater", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexMeshDeepWaterTest::RunTest(const FString& Parameters)
{
	// land in the west, with deep water growing away from it
	const FHexMapData Map = MakeMap(6, 3, {
		0, -2, -2, -2, -2, -2,
		0, -2, -2, -2, -2, -2,
		0, -2, -2, -2, -2, -2
	});

	const FHexMeshParams Params;
	TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();
	Surfaces[-2].bHidesTerrain = true;

	FHexMeshData Mesh;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 0, 0, Map.Width, Map.Height, Mesh);

	const FHexLayout Layout(Params.HexRadius);
	const auto HasCenterVertex = [&Mesh, &Layout](int32 Column, int32 Row)
	{
		const FVector2D Center = Layout.HexToWorld(FHexCoord::FromOffset(Column, Row));
		return Mesh.Vertices.ContainsByPredicate([&Center](const FVector& Position)
		{
			return Center.Equals(FVector2D(Position.X, Position.Y), 0.01);
		});
	};

	TestTrue(TEXT("Land has terrain"), HasCenterVertex(0, 1));
	TestTrue(TEXT("Deep water at the shore has terrain"), HasCenterVertex(1, 1));
	TestFalse(TEXT("Deep water away from the shore has none"), HasCenterVertex(3, 1));
	TestFalse(TEXT("Nor at the edge of the map"), HasCenterVertex(5, 1));

	// a chunk of only deep water has nothing to draw
	FHexMeshData Empty;
	HexMeshBuilder::BuildRange(Map, Params, Surfaces, 3, 0, 6, 3, Empty);
	TestEqual(TEXT("No vertices in open water"), Empty.Vertices.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexCliffsTest, "HexHero.HexMap.Mesh.Cliffs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexCliffsTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("A beach has no cliff"), HexMeshBuilder::IsCliffSide(0, -1, 2));
	TestTrue(TEXT("Land by deep water"), HexMeshBuilder::IsCliffSide(0, -2, 2));
	TestTrue(TEXT("Hills by shallows"), HexMeshBuilder::IsCliffSide(-1, 1, 2));
	TestTrue(TEXT("A mountain by deep water"), HexMeshBuilder::IsCliffSide(2, -2, 2));
	TestFalse(TEXT("Land to land"), HexMeshBuilder::IsCliffSide(0, 2, 2));
	TestFalse(TEXT("Water to water"), HexMeshBuilder::IsCliffSide(-2, -1, 2));
	TestFalse(TEXT("Cliffs can be off"), HexMeshBuilder::IsCliffSide(2, -2, 0));

	FHexMeshParams Params;
	Params.CliffMinDifference = 2;
	Params.WaterHeight = -15.0f;
	Params.CliffSeabedDepth = 25.0f;
	Params.CliffBottomHeight = -60.0f;
	TMap<int8, FHexLevelSurface> Surfaces = MakeSurfaces();
	Surfaces[0].CliffTop = 12.0f;
	Surfaces[1].CliffTop = 40.0f;

	// one hex of land in deep water has one closed line of cliffs around it, out in the water, and the mesh closes with it
	const FHexMapData Island = MakeMap(5, 5, {
		-2, -2, -2, -2, -2,
		-2, -2, -2, -2, -2,
		-2, -2, 0, -2, -2,
		-2, -2, -2, -2, -2,
		-2, -2, -2, -2, -2
	});

	FHexCliffData IslandCliffs;
	HexMeshBuilder::BuildCliffs(Island, Params, Surfaces, IslandCliffs);

	if (!TestEqual(TEXT("One line of cliffs"), IslandCliffs.Chains.Num(), 1))
	{
		return false;
	}

	const FHexCliffChain& Loop = IslandCliffs.Chains[0];
	TestTrue(TEXT("It closes"), Loop.bClosed);
	TestEqual(TEXT("A mesh cut per ring"), Loop.Vertices.Num(), Loop.Rings.Num() * FHexCliffData::RowsPerRing);

	const FHexLayout Layout(100.0f);
	const FHexCoord Center = FHexCoord::FromOffset(2, 2);
	const FVector2D HexCenter = Layout.HexToWorld(Center);
	double NearestToCenter = TNumericLimits<double>::Max();
	double LargestTurn = 0.0;

	for (int32 Index = 0; Index < Loop.Rings.Num(); ++Index)
	{
		const FHexCliffRing& Ring = Loop.Rings[Index];
		const FHexCliffRing& Next = Loop.Rings[(Index + 1) % Loop.Rings.Num()];
		TestFalse(TEXT("The coast lies out in the water"), Layout.WorldToHex(Ring.Position) == Center);
		TestTrue(TEXT("Outward points away from the land"), FVector2D::DotProduct(Ring.Outward, Ring.Position - HexCenter) > 0.0);
		TestEqual(TEXT("A whole cliff"), Ring.Strength, 1.0, 0.001);
		NearestToCenter = FMath::Min(NearestToCenter, FVector2D::Distance(Ring.Position, HexCenter));
		LargestTurn = FMath::Max(LargestTurn, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(Ring.Outward, Next.Outward), -1.0, 1.0))));

		for (int32 Row = 0; Row + 1 < FHexCliffData::RowsPerRing; ++Row)
		{
			TestTrue(TEXT("The mesh goes down from its top"), Row < 3 || Loop.Vertices[Index * FHexCliffData::RowsPerRing + Row + 1].Z < Loop.Vertices[Index * FHexCliffData::RowsPerRing + Row].Z + 8.0);
		}
	}

	TestTrue(TEXT("The corners of the hexes are rounded"), LargestTurn < 40.0);

	// the triangles of a range are the ones of that part of the coast
	FHexMeshData AllCliffs;
	HexMeshBuilder::AddCliffRange(IslandCliffs, 0, 0, Island.Width, Island.Height, AllCliffs);
	TestEqual(TEXT("Triangles of the coast"), AllCliffs.Triangles.Num(), Loop.Rings.Num() * (FHexCliffData::RowsPerRing - 1) * 6);
	FHexMeshData NoCliffs;
	HexMeshBuilder::AddCliffRange(IslandCliffs, 3, 0, 5, 5, NoCliffs);
	TestEqual(TEXT("None outside the hex of land"), NoCliffs.Vertices.Num(), 0);

	// the terrain is at the top of the cliff on the land and on the seabed in front of it
	FHexMeshData Mesh;
	HexMeshBuilder::BuildRange(Island, Params, Surfaces, 0, 0, Island.Width, Island.Height, Mesh, &IslandCliffs);
	TestSurfaceIsClosed(*this, Mesh, Params);

	for (const FVector& Vertex : Mesh.Vertices)
	{
		const double FromCenter = FVector2D::Distance(FVector2D(Vertex.X, Vertex.Y), HexCenter);

		if (Vertex.Z > Params.BottomHeight + 1.0 && FromCenter > 150.0 && FromCenter < 200.0)
		{
			TestTrue(TEXT("Under the water in front of the cliff"), Vertex.Z <= -39.9);
		}
	}

	// whatever lies inland of the coast is land, even out over the hexes of water, so no ditch forms behind the cliff
	for (const FVector& Vertex : Mesh.Vertices)
	{
		const FVector2D Position(Vertex.X, Vertex.Y);
		double Nearest = TNumericLimits<double>::Max();
		bool bInland = false;

		for (const FHexCliffRing& Ring : Loop.Rings)
		{
			const double Distance = FVector2D::Distance(Position, Ring.Position);

			if (Distance < Nearest)
			{
				Nearest = Distance;
				bInland = FVector2D::DotProduct(Position - Ring.Position, Ring.Outward) < 0.0;
			}
		}

		if (bInland && Nearest > 12.0)
		{
			TestTrue(TEXT("No ditch behind the cliff"), Vertex.Z > -12.0);
		}
	}

	// a long coast has all three kinds of cut, and where it meets a beach the cliff ends
	TArray<int8> Levels;
	const int32 Width = 40;
	const int32 Height = 20;

	for (int32 Row = 0; Row < Height; ++Row)
	{
		for (int32 Column = 0; Column < Width; ++Column)
		{
			Levels.Add(Column < 12 ? -1 : Row < 10 ? 1 : -2);
		}
	}

	const FHexMapData Coast = MakeMap(Width, Height, Levels);
	FHexCliffData CoastCliffs;
	HexMeshBuilder::BuildCliffs(Coast, Params, Surfaces, CoastCliffs);
	TestTrue(TEXT("The coast has cliffs"), !CoastCliffs.Chains.IsEmpty());

	bool bAnyLedges = false;
	bool bAnySlope = false;

	for (const FHexCliffChain& Chain : CoastCliffs.Chains)
	{
		for (int32 Index = 0; Index < Chain.Rings.Num(); ++Index)
		{
			const auto GetReach = [&Chain, Index](int32 Row)
			{
				const FVector& Vertex = Chain.Vertices[Index * FHexCliffData::RowsPerRing + Row];
				return FVector2D::DotProduct(FVector2D(Vertex.X, Vertex.Y) - Chain.Rings[Index].Position, Chain.Rings[Index].Outward);
			};

			// a shelf reaches far out between two rows that are close in height, as a wall does not
			for (int32 Row = 4; Row + 1 < FHexCliffData::RowsPerRing; ++Row)
			{
				bAnyLedges |= GetReach(Row + 1) - GetReach(Row) > 8.0;
			}

			bAnySlope |= GetReach(FHexCliffData::RowsPerRing - 1) > 60.0;
		}
	}

	TestTrue(TEXT("Some of the coast has ledges"), bAnyLedges);
	TestTrue(TEXT("Some of the coast is a slope"), bAnySlope);

	// hills that end at a beach have a line of cliffs that fades out
	const FHexMapData Mixed = MakeMap(3, 2, {
		1, 0, 0,
		-1, -1, -1
	});
	FHexCliffData MixedCliffs;
	HexMeshBuilder::BuildCliffs(Mixed, Params, Surfaces, MixedCliffs);

	if (TestEqual(TEXT("One line by the hills"), MixedCliffs.Chains.Num(), 1))
	{
		TestFalse(TEXT("It does not close"), MixedCliffs.Chains[0].bClosed);
		TestEqual(TEXT("It fades at its start"), MixedCliffs.Chains[0].Rings[0].Strength, 0.0, 0.001);
		TestEqual(TEXT("It fades at its end"), MixedCliffs.Chains[0].Rings.Last().Strength, 0.0, 0.001);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexRayTest, "HexHero.HexMap.Mesh.FindHexAlongRay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexRayTest::RunTest(const FString& Parameters)
{
	// a mountain in the first row, with deep water and land around it
	const FHexMapData Map = MakeMap(3, 2, {
		0, 2, -2,
		0, 0, -2
	});

	const FHexLayout Layout(100.0f);
	const auto FindHex = [&Map](const FVector& Origin, const FVector& Direction, FHexCoord& OutHex)
	{
		return HexMeshBuilder::FindHexAlongRay(Map, 100.0f, 40.0f, -15.0f, 80.0, Origin, Direction, OutHex);
	};

	FHexCoord Hex;

	for (int32 Row = 0; Row < Map.Height; ++Row)
	{
		for (int32 Column = 0; Column < Map.Width; ++Column)
		{
			const FHexCoord Expected = FHexCoord::FromOffset(Column, Row);
			const FVector2D Center = Layout.HexToWorld(Expected);
			TestTrue(TEXT("A ray straight down hits the map"), FindHex(FVector(Center.X, Center.Y, 1000.0), FVector::DownVector, Hex));
			TestTrue(TEXT("A ray straight down finds the hex below it"), Hex == Expected);
		}
	}

	// on the ground this ray ends on the hex behind the mountain, but it reaches the mountain first
	const FHexCoord Mountain = FHexCoord::FromOffset(1, 0);
	const FVector2D MountainCenter = Layout.HexToWorld(Mountain);
	const FVector Slanted = FVector(0.0, 1.0, -1.0);
	const FVector Origin(MountainCenter.X, MountainCenter.Y - 1000.0, 1050.0);
	TestTrue(TEXT("A slanted ray hits the map"), FindHex(Origin, Slanted, Hex));
	TestTrue(TEXT("A slanted ray stops at the mountain"), Hex == Mountain);

	TestFalse(TEXT("A ray that goes up misses"), FindHex(FVector(0.0, 0.0, 1000.0), FVector::UpVector, Hex));
	TestFalse(TEXT("A ray beside the map misses"), FindHex(FVector(5000.0, 5000.0, 1000.0), FVector::DownVector, Hex));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexFeatureTransformTest, "HexHero.HexMap.Mesh.FeatureTransform", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexFeatureTransformTest::RunTest(const FString& Parameters)
{
	// a mesh with an odd size and a pivot far from its middle
	const FBox MeshBounds(FVector(10.0, 30.0, -5.0), FVector(50.0, 110.0, 15.0));
	const FHexLayout Layout(100.0f);

	for (int32 Column = 0; Column < 8; ++Column)
	{
		const FHexCoord Hex = FHexCoord::FromOffset(Column, 3);
		const FTransform Transform = HexMeshBuilder::GetFeatureTransform(Hex, MeshBounds, 100.0f, 2.0f, 200.0f, -40.0);
		const FVector2D Center = Layout.HexToWorld(Hex);

		const FVector Middle = Transform.TransformPosition(MeshBounds.GetCenter());
		TestEqual(TEXT("Mesh middle is over the hex center X"), Middle.X, Center.X, 0.01);
		TestEqual(TEXT("Mesh middle is over the hex center Y"), Middle.Y, Center.Y, 0.01);

		TestEqual(TEXT("Lowest point"), Transform.TransformPosition(MeshBounds.Min).Z, -40.0, 0.01);
		TestEqual(TEXT("Highest point"), Transform.TransformPosition(MeshBounds.Max).Z, 160.0, 0.01);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexJoinedPieceTest, "HexHero.HexMap.Mesh.JoinedPieces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHexJoinedPieceTest::RunTest(const FString& Parameters)
{
	const FHexCoord Hex = FHexCoord::FromOffset(4, 3);
	const FVector2D Center = FHexLayout(100.0f).HexToWorld(Hex);
	TSet<int32> MeshMasks;

	for (int32 NeighborMask = 0; NeighborMask < 64; ++NeighborMask)
	{
		const FHexJoinedPiece Piece = HexMeshBuilder::GetJoinedPiece(NeighborMask);
		const FTransform Transform = HexMeshBuilder::GetJoinedTransform(Hex, Piece.Turns, 100.0f, 100.0f, 0.0);
		MeshMasks.Add(Piece.MeshMask);

		// a side a mesh joins, placed in the world, must face a neighbor in the mask
		for (int32 MeshDirection = 0; MeshDirection < FHexCoord::NumDirections; ++MeshDirection)
		{
			if (!(Piece.MeshMask >> MeshDirection & 1))
			{
				continue;
			}

			const double Angle = FMath::DegreesToRadians(30.0 + 60.0 * MeshDirection);
			const FVector Placed = Transform.TransformPosition(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * FMath::Sqrt(3.0) * 50.0);
			const FVector2D Neighbor = FHexLayout(100.0f).HexToWorld(Hex.GetNeighbor(MeshDirection + Piece.Turns));

			TestEqual(TEXT("The side is halfway to the neighbor X"), Placed.X, (Center.X + Neighbor.X) * 0.5, 0.01);
			TestEqual(TEXT("The side is halfway to the neighbor Y"), Placed.Y, (Center.Y + Neighbor.Y) * 0.5, 0.01);
			TestTrue(TEXT("The neighbor is in the mask"), (NeighborMask >> ((MeshDirection + Piece.Turns) % FHexCoord::NumDirections) & 1) != 0);
		}
	}

	TestEqual(TEXT("Number of meshes"), MeshMasks.Num(), 14);

	bool bAnyLevelJoins = false;

	for (const UHexLevelConfig* Level : GetDefault<UHexMapSettings>()->LoadLevels())
	{
		if (Level->JoinedMeshes.IsEmpty())
		{
			continue;
		}

		bAnyLevelJoins = true;

		for (const int32 MeshMask : MeshMasks)
		{
			const TSoftObjectPtr<UStaticMesh>* Mesh = Level->JoinedMeshes.Find(MeshMask);
			TestTrue(*FString::Printf(TEXT("Level %d has mesh %d"), Level->Level, MeshMask), Mesh && Mesh->LoadSynchronous());
		}
	}

	TestTrue(TEXT("A level has joined meshes"), bAnyLevelJoins);

	for (const UHexLevelConfig* Level : GetDefault<UHexMapSettings>()->LoadLevels())
	{
		for (const TPair<int32, FHexJoinedVariants>& Variants : Level->JoinedMeshVariants)
		{
			TestTrue(*FString::Printf(TEXT("Mesh %d of level %d has a first version"), Variants.Key, Level->Level), Level->JoinedMeshes.Contains(Variants.Key));

			for (const TSoftObjectPtr<UStaticMesh>& Variant : Variants.Value.Meshes)
			{
				TestTrue(*FString::Printf(TEXT("Level %d has variant %s"), Level->Level, *Variant.ToString()), Variant.LoadSynchronous() != nullptr);
			}
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
