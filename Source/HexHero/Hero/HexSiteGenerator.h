// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetLayers.h"
#include "HexSiteConfig.h"

class FHexSphereGrid;

/**
 *  Scatters the mines, piles and mills over both layers of a planet, as the developer asked on 2026-11-11 ("ресурси, потрібно
 *  створити/розкидати їх по світу"). Pure and deterministic, so the same planet, heroes and seed give the same sites, and safe on any thread.
 *
 *  A site fills its cell, so it only stands where a hero could, on ground of at least MinRegionCells cells, off the cells to avoid, and
 *  where closing its cell cuts no way: the cells around it that stay open lie in one run (KeepsWays), so a hero walks around it and every
 *  site keeps a cell to be visited from. Two sites are never side by side (Spacing), and mines keep further apart (MineSpacing,
 *  SameMineSpacing; a mine near a start only MineSpacing, as two players may start beside each other).
 *
 *  First each player gets its sites near where it starts, within StartMinSteps to StartMaxSteps steps of a hero from its first hero: a mine
 *  of each kind marked bNearStart and StartPiles piles. Then each layer gets what is still missing of each kind to its number, PerThousand of
 *  the cells a hero can stand on there, the mines first, at random places.
 */
namespace HexSiteGenerator
{
	/** A kind of site the generation places */
	struct FKind
	{
		/** Its index in DA_HexSites, which the sites found carry */
		int32 Kind = 0;

		EHexSiteType Type = EHexSiteType::Pile;

		/** Sites of it to a thousand cells a hero can stand on, on each layer */
		float PerThousand[NumPlanetLayers] = {0.0f, 0.0f};

		bool bNearStart = false;
	};

	/** Where a player starts */
	struct FStart
	{
		int32 Layer = 0;
		int32 Cell = INDEX_NONE;
	};

	struct FInput
	{
		/** The cells of each layer a hero could stand on with no site there; empty for a layer that gets none */
		TBitArray<> Walkable[NumPlanetLayers];

		/** The cells that take no site though a hero walks them: an entrance and what lies around it, where a hero stands, a meadow */
		TBitArray<> Avoid[NumPlanetLayers];

		/** Where the first hero of each player stands */
		TArray<FStart> Starts;

		TArray<FKind> Kinds;

		/** Its Shares are not read, as Kinds holds them */
		FHexSiteGeneration Settings;

		int32 Seed = 0;
	};

	struct FSite
	{
		int32 Kind = 0;
		int32 Layer = 0;
		int32 Cell = INDEX_NONE;
	};

	struct FReport
	{
		/** The cells of each layer a hero can stand on that lie on ground large enough */
		int32 Cells[NumPlanetLayers] = {0, 0};

		/** The sites placed near where the players start, and those that found no room there or anywhere */
		int32 NearStarts = 0;
		int32 NoRoom = 0;
	};

	void Generate(const FHexSphereGrid& Grid, const FInput& Input, TArray<FSite>& OutSites, FReport* OutReport = nullptr);

	/** Returns true if closing a cell cuts no way between the cells around it and leaves one to visit it from: those of them that are open
	 *  lie in one run around it */
	bool KeepsWays(const FHexSphereGrid& Grid, const TBitArray<>& Open, int32 Cell);
}
