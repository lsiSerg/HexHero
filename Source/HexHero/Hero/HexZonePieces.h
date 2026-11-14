// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexZoneMap.h"

class AHexPlanet;
class UHexCastleConfig;
class UHexBattleConfig;
class UHexHeroConfig;
struct FHexStack;

/**
 *  Places what a map of zones (HexZoneMap) holds on its planet once it is built and its underground has its data: the castles of the players
 *  and the empty ones of nobody, a hero beside the castle of each player, who the game moves, the objects taken off the passes, the pockets of
 *  the entrances and wherever a forest would leave land out of reach, the mines and piles of both layers (SiteDensity times as many as DA_HexSites
 *  places), the neutral stacks of DA_HexBattle, and the guard of each way in (FindGuard). A saved planet writes them with zones.json at once
 */
namespace HexZonePieces
{
	bool Place(AHexPlanet& Surface, const FHexZonesFile& File, FString& OutReport);

	/** The guard of a way in, of a creature of the faction and level: as many as the army a hero starts with and GuardWeeks of what the first
	 *  GuardDwellings dwellings offer, of either faction, beats in at least half of GuardTries battles, times GuardShare */
	int32 FindGuardCount(const UHexCastleConfig& Castles, const UHexBattleConfig& Battle, const UHexHeroConfig& Heroes, int32 Faction, int32 Level, bool bUpgraded);

	/** The army of the faction a hero has after a week, against which a guard is weighed */
	TArray<FHexStack> MakeWeekArmy(const UHexCastleConfig& Castles, const FHexZoneMapGeneration& Settings, int32 Faction);
}
