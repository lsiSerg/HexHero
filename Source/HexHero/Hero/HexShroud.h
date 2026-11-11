// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetLayers.h"
#include "UObject/Object.h"
#include "HexShroud.generated.h"

class FHexSphereGrid;

/** The pure pieces of the fog of war, which run without a world */
namespace HexShroud
{
	/** Finds the cells up to Rings steps over neighbors from a cell, that cell first */
	void FindWithin(const FHexSphereGrid& Grid, int32 Cell, int32 Rings, TArray<int32>& OutCells);

	/** Returns where a direction from the center lies in the field of a layer, from 0 to 1 each way: an octahedral map of the sphere, which the
	 *  materials repeat (FieldUV of SHROUD_COMMON_CODE in create_planet_assets.py), so change both together */
	FVector2d GetFieldUV(const FVector3d& Direction);

	/** Returns the direction of the middle of a texel of a field Size texels a side, the inverse of GetFieldUV */
	FVector3d GetTexelDirection(int32 X, int32 Y, int32 Size);
}

/**
 * The fog of war, as the developer decided on 2026-11-11: each player has a map of what it explored, on each layer, which its heroes open as they
 * walk (SightRadius of DA_HexHeroes), and what was explored once stays seen for good, as in Heroes III. It holds only which cells each player
 * explored; UHexHeroes opens them, and AHexPlanet draws the shroud over the rest (UpdateShroud). Nothing of it is saved yet, as where the heroes
 * walked to is not
 */
UCLASS()
class UHexShroud : public UObject
{
	GENERATED_BODY()

public:
	/** Forgets what every player explored */
	void Reset();

	/** Marks cells of a layer of NumCells cells as explored by a player, and returns whether any of them was new */
	bool Explore(int32 Player, EHexPlanetLayer Layer, TConstArrayView<int32> Cells, int32 NumCells);

	bool IsExplored(int32 Player, EHexPlanetLayer Layer, int32 Cell) const;

	/** Returns the cells a player explored on a layer, a bit per cell, or nullptr when it explored none there */
	const TBitArray<>* GetExplored(int32 Player, EHexPlanetLayer Layer) const;

	int32 CountExplored(int32 Player, EHexPlanetLayer Layer) const;

	/** Counts the changes of what is explored, so what draws it knows when to look again */
	int32 GetRevision() const { return Revision; }

private:
	struct FPlayerCells
	{
		TBitArray<> Explored[NumPlanetLayers];
	};

	TMap<int32, FPlayerCells> Players;
	int32 Revision = 0;
};
