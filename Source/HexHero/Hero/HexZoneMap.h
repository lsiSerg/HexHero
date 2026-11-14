// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexHeroConfig.h"
#include "HexPlanetFile.h"
#include "HexZoneMap.generated.h"

class FHexSphereGrid;

/** A zone of zones.json: who it is for, its castles as three cells each, where its hero starts and how many ways lead into it */
USTRUCT()
struct FHexZoneLine
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Index = 0;

	/** Person, Ai or Nobody */
	UPROPERTY()
	FString Role;

	/** The player of its castle and hero, -1 for nobody */
	UPROPERTY()
	int32 Player = -1;

	/** The cells of its castles, three to a castle */
	UPROPERTY()
	TArray<int32> Castles;

	/** Where its hero starts, -1 for none */
	UPROPERTY()
	int32 Hero = -1;

	UPROPERTY()
	int32 Accesses = 0;
};

/** A way into a zone and the cell its guard stands on: a pass over a range between two zones, or the cell before an entrance of a network that
 *  leads to another zone, which an entrance in a pocket of mountains opens to */
USTRUCT()
struct FHexZoneGuardLine
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Cell = 0;

	/** pass or network */
	UPROPERTY()
	FString Way;

	/** The zones it joins: both of a pass, the zone of an entrance and -1 */
	UPROPERTY()
	int32 From = 0;

	UPROPERTY()
	int32 To = -1;

	/** The network of an entrance, A to H */
	UPROPERTY()
	FString Network;
};

/** zones.json of a map of zones: where its castles, heroes and guards went (HexZonePieces), for the developer and the log */
USTRUCT()
struct FHexZonesFile
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	int32 Seed = 0;

	/** Which ways join which zones, for the log and the developer */
	UPROPERTY()
	FString Ways;

	UPROPERTY()
	TArray<FHexZoneLine> Zones;

	UPROPERTY()
	TArray<FHexZoneGuardLine> Guards;

	/** Cells that stay free of objects, sites and neutral stacks: the passes, the pockets of the entrances and around the castles */
	UPROPERTY()
	TArray<int32> Clear;
};

/**
 *  A map of zones (Zones of FHexPlanetGeneration, 2 to 8), pure and deterministic, so the same grid, levels, settings and seed give the same map
 *  (the HexHero.Hero.ZoneMap test). The zones lie around the equator, up to four in one ring and more in two rings north and south of it
 *  (RingLatitude), each the cells nearest its middle, the borders moved by a noise; the poles are sea, and a range of mountains two to four
 *  cells wide runs between each two zones. Each zone is reached from MinAccesses to MaxAccesses ways, passes over a range to a zone beside it,
 *  a way one cell wide that a guard closes, and networks of the underground to any zone, whose entrance in a zone stands in a pocket of
 *  mountains open only towards the cell of its guard, so a hero coming up from below fights the guard too. The land inside a zone is the
 *  generated land of the planet, carved where it would leave a piece of the zone out of reach. Zones have a network of their own besides, for
 *  their mines below, as far as the eight networks go. Zone 0 is the person's, the AiZones furthest from it the game's, the rest nobody's
 */
namespace HexZoneMap
{
	enum class ERole : uint8
	{
		Person,
		Ai,
		Nobody,
	};

	const TCHAR* GetRoleName(ERole Role);

	struct FZone
	{
		ERole Role = ERole::Nobody;
		/** The middle of the zone on the unit sphere */
		FVector3d Center = FVector3d::ZeroVector;
		/** Its castles, three cells each */
		TArray<TArray<int32>> Castles;
		int32 HeroCell = INDEX_NONE;
		int32 Accesses = 0;
		/** Its player, INDEX_NONE for nobody */
		int32 Player = INDEX_NONE;
	};

	struct FGuard
	{
		int32 Cell = INDEX_NONE;
		int32 From = INDEX_NONE;
		/** The other zone of a pass, INDEX_NONE for an entrance */
		int32 To = INDEX_NONE;
		/** The network of an entrance, INDEX_NONE for a pass */
		int32 Network = INDEX_NONE;
	};

	struct FResult
	{
		TArray<int8> Levels;
		/** The zone of each cell, INDEX_NONE for the polar sea */
		TArray<int8> ZoneOf;
		/** The cells of the ranges between the zones */
		TBitArray<> Range;
		TArray<FZone> Zones;
		TArray<FGuard> Guards;
		TArray<FHexEntrance> Entrances;
		TArray<int32> Clear;
		FString Ways;
		TArray<FString> Problems;

		bool IsGood() const { return Problems.IsEmpty(); }
	};

	/** Makes the map from the generated levels of the planet */
	void Generate(const FHexSphereGrid& Grid, TConstArrayView<int8> BaseLevels, const FHexZoneMapGeneration& Settings, int32 NumZones, int32 AiZones,
		int32 Seed, FResult& Out);

	/** Generate with Seed and, while the map has problems, a few seeds after it; the last one is kept when none is good */
	void GenerateGood(const FHexSphereGrid& Grid, TConstArrayView<int8> BaseLevels, const FHexZoneMapGeneration& Settings, int32 NumZones, int32 AiZones,
		int32 Seed, FResult& Out, int32* OutSeed = nullptr);

	/** The player of the zones that are not the person's, by the index of the zone among them: those of DA_HexHeroes after the person's */
	int32 GetAiPlayer(const FHexZoneMapGeneration& Settings, int32 Index);

	/** Checks a map: each zone reaches all its land, every guard is reached from each side it closes, and with the guards standing no zone
	 *  reaches another; and the ways into each zone. Fills Problems */
	void Check(const FHexSphereGrid& Grid, const FHexZoneMapGeneration& Settings, FResult& InOut);

	FHexZonesFile MakeFile(const FResult& Result, int32 Seed);

	constexpr const TCHAR* ZonesFile = TEXT("zones.json");
	bool LoadFile(const FString& PlanetFolder, FHexZonesFile& OutFile, FString& OutError);
}
