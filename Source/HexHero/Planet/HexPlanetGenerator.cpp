// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetGenerator.h"
#include "HexPlanetConfig.h"
#include "HexPlanetFile.h"
#include "HexSphereGrid.h"

namespace
{
	/** Returns the given cells sorted by a key, highest first, ties by index so the order never depends on the sort */
	void SortByKey(TArray<int32>& Cells, const TArray<double>& Keys)
	{
		Cells.Sort([&Keys](int32 A, int32 B) { return Keys[A] != Keys[B] ? Keys[A] > Keys[B] : A < B; });
	}

	/** Returns how many of Count cells a share is */
	int32 CountShare(double Share, int32 Count)
	{
		return FMath::Clamp(FMath::RoundToInt32(FMath::Clamp(Share, 0.0, 1.0) * Count), 0, Count);
	}
}

void HexPlanetGenerator::GenerateLevels(const FHexSphereGrid& Grid, const FHexPlanetManifest& Manifest, const UHexPlanetConfig& Config, TArray<int8>& OutLevels)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const FHexPlanetGeneration& Generation = Manifest.Generation;
	OutLevels.Init(0, Cells.Num());

	if (Generation.Start != EHexPlanetStart::Random)
	{
		OutLevels.Init(Generation.Start == EHexPlanetStart::Water ? -2 : 0, Cells.Num());
		return;
	}

	FRandomStream Random(Manifest.Seed);
	const FVector Offset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
	const int32 Octaves = FMath::Clamp(Generation.CoastDetail, 1, 10);
	const double BaseScale = Config.LevelNoiseScale / FMath::Max(Generation.ContinentSize, 0.01);
	const double RangeScale = Config.GetSizeRatio(Manifest.Frequency);

	TArray<double> Values;
	Values.SetNumUninitialized(Cells.Num());

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		double Value = 0.0;
		double Amplitude = 1.0;
		double Scale = BaseScale;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Value += FMath::PerlinNoise3D(Cells[Cell].Center * Scale + Offset * (Octave + 1)) * Amplitude;
			Amplitude *= 0.5;
			Scale *= 2.0;
		}

		Values[Cell] = Value;
	}

	// the highest cells are land, and the highest of the sea are shallows
	TArray<int32> Order;
	Order.Reserve(Cells.Num());
	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		Order.Add(Cell);
	}
	SortByKey(Order, Values);

	const int32 LandCount = CountShare(Generation.LandShare, Cells.Num());
	const int32 ShallowsCount = CountShare(Generation.ShallowsShare, Cells.Num() - LandCount);

	for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
	{
		OutLevels[Order[Rank]] = Rank < LandCount ? 0 : Rank < LandCount + ShallowsCount ? -1 : -2;
	}

	if (LandCount == 0)
	{
		return;
	}

	// mountains are ranges along the lines where a second noise crosses zero, wobbled by a third, kept off the coast and wider further inland
	const double Coast = Values[Order[LandCount - 1]];
	TArray<double> RangeKeys;
	RangeKeys.Init(0.0, Cells.Num());
	TArray<int32> RangeCells;

	for (int32 Rank = 0; Rank < LandCount; ++Rank)
	{
		const int32 Cell = Order[Rank];
		const double Inland = Values[Cell] - Coast - Config.RangeMinLand;

		if (Inland <= 0.0 || Cells[Cell].IsPentagon())
		{
			continue;
		}

		const FVector3d& Center = Cells[Cell].Center;
		const double Wobble = FMath::PerlinNoise3D(Center * Config.RangeWobbleScale * RangeScale + Offset * 5.0) * Config.RangeWobble;
		const double Ridge = 1.0 - FMath::Abs(FMath::PerlinNoise3D(Center * Config.RangeNoiseScale * RangeScale + Offset * 3.0 + FVector3d(Wobble)));
		RangeKeys[Cell] = Ridge + FMath::Min(Inland, double(Config.RangeInlandReach)) * Config.RangeInlandWidening;
		RangeCells.Add(Cell);
	}

	SortByKey(RangeCells, RangeKeys);
	const int32 MountainCount = FMath::Min(CountShare(Generation.MountainShare, LandCount), RangeCells.Num());

	for (int32 Rank = 0; Rank < MountainCount; ++Rank)
	{
		OutLevels[RangeCells[Rank]] = 2;
	}

	// hills are the highest of the other land
	int32 HillsLeft = FMath::Min(CountShare(Generation.HillShare, LandCount), LandCount - MountainCount);

	for (int32 Rank = 0; Rank < LandCount && HillsLeft > 0; ++Rank)
	{
		if (OutLevels[Order[Rank]] == 0)
		{
			OutLevels[Order[Rank]] = 1;
			--HillsLeft;
		}
	}
}

void HexPlanetGenerator::GenerateBiomes(const FHexSphereGrid& Grid, const FHexPlanetManifest& Manifest, const UHexPlanetConfig& Config, TConstArrayView<int8> Levels, TArray<TCHAR>& OutBiomes)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const FHexPlanetGeneration& Generation = Manifest.Generation;
	OutBiomes.Init(Biome::Water, Cells.Num());

	// how many steps of cells each land cell lies from the sea, found inwards from the coast
	TArray<int32> Steps;
	Steps.Init(INDEX_NONE, Cells.Num());
	TArray<int32> Front;

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Levels[Cell] < 0)
		{
			Steps[Cell] = 0;
			Front.Add(Cell);
		}
	}

	for (int32 Index = 0; Index < Front.Num(); ++Index)
	{
		for (const int32 Neighbor : Cells[Front[Index]].Neighbors)
		{
			if (Steps[Neighbor] == INDEX_NONE)
			{
				Steps[Neighbor] = Steps[Front[Index]] + 1;
				Front.Add(Neighbor);
			}
		}
	}

	// the noises are of the direction from the center, so the climate zones keep their share of a planet of any size, while the reach of the
	// moisture is in cells, as the coast is
	FRandomStream Random(Manifest.Seed * 31 + 17);
	const FVector TemperatureOffset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
	const FVector MoistureOffset(Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0), Random.FRandRange(-100.0, 100.0));
	const double Scale = Config.ClimateNoiseScale;
	const double Reach = FMath::Max(Config.MoistureReach * Config.GetSizeRatio(Manifest.Frequency), 0.1);

	const auto Noise = [Scale](const FVector3d& Center, const FVector& Offset)
	{
		// two octaves, so the edges of the zones are not smooth arcs
		return FMath::PerlinNoise3D(Center * Scale + Offset) + FMath::PerlinNoise3D(Center * Scale * 2.7 + Offset * 1.3) * 0.4;
	};

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		const FVector3d& Center = Cells[Cell].Center;
		const int32 Level = Levels[Cell];
		const double Latitude = FMath::Abs(FMath::Asin(FMath::Clamp(Center.Z, -1.0, 1.0))) / UE_DOUBLE_HALF_PI;
		const double Temperature = 1.0 - Latitude + Generation.Temperature + Noise(Center, TemperatureOffset) * Config.TemperatureNoise
			- FMath::Max(Level, 0) * Config.HeightCooling;

		if (Level < 0)
		{
			OutBiomes[Cell] = Temperature < Config.IceTemperature ? Biome::Ice : Biome::Water;
			continue;
		}

		const double Belt = (Temperature - Config.DryBeltTemperature) / FMath::Max(Config.DryBeltWidth, 0.01);
		// a planet without sea is dry everywhere
		const double Moisture = (Steps[Cell] == INDEX_NONE ? 0.0 : FMath::Exp(-Steps[Cell] / Reach)) + Generation.Moisture + Noise(Center, MoistureOffset) * Config.MoistureNoise
			- Config.DryBelt * FMath::Exp(-Belt * Belt);

		TCHAR Letter = Biome::Grass;
		if (Temperature < Config.SnowTemperature)
		{
			Letter = Biome::Snow;
		}
		else if (Temperature < Config.TundraTemperature)
		{
			Letter = Biome::Tundra;
		}
		else if (Moisture < Config.DesertMoisture && Temperature > Config.DesertTemperature)
		{
			Letter = Biome::Desert;
		}
		else if (Moisture < Config.SteppeMoisture)
		{
			Letter = Biome::Steppe;
		}
		else if (Level == 0 && Moisture > Config.SwampMoisture && Temperature > Config.SwampTemperature)
		{
			Letter = Biome::Swamp;
		}

		if (Level >= 2 && Moisture < Config.MountainRockMoisture && (Letter == Biome::Grass || Letter == Biome::Steppe))
		{
			Letter = Biome::Rock;
		}

		OutBiomes[Cell] = Letter;
	}

	AddShoreIce(Grid, Levels, OutBiomes, Config.ShoreIceRings);
}

TArray<int32> HexPlanetGenerator::AddShoreIce(const FHexSphereGrid& Grid, TConstArrayView<int8> Levels, TArray<TCHAR>& Biomes, int32 Rings)
{
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	TArray<int32> Frozen;
	TArray<int32> Steps;
	Steps.Init(INDEX_NONE, Cells.Num());
	TArray<int32> Front;

	for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
	{
		if (Levels[Cell] >= 0 && Biomes[Cell] == Biome::Snow)
		{
			Steps[Cell] = 0;
			Front.Add(Cell);
		}
	}

	// outwards over the sea only, so the ice follows the coast of the snow and does not cross land
	for (int32 Index = 0; Index < Front.Num(); ++Index)
	{
		const int32 Cell = Front[Index];
		if (Steps[Cell] >= Rings)
		{
			continue;
		}
		for (const int32 Neighbor : Cells[Cell].Neighbors)
		{
			if (Steps[Neighbor] == INDEX_NONE && Levels[Neighbor] < 0)
			{
				Steps[Neighbor] = Steps[Cell] + 1;
				Front.Add(Neighbor);
				if (Biomes[Neighbor] != Biome::Ice)
				{
					Biomes[Neighbor] = Biome::Ice;
					Frozen.Add(Neighbor);
				}
			}
		}
	}
	return Frozen;
}
