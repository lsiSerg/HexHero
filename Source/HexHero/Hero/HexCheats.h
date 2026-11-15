// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UHexHeroes;

/**
 *  What the cheats and the console commands for trying the game are, for hex.Cheat help (also hex.Cheat.Help and hex.Cheat with nothing
 *  after it) and for the sheet of Ctrl+H (SHexCheatSheet), as the developer asked on 2026-11-12, so both say the same; and the state of the
 *  cheats that only show something (hex.Cheat enemies)
 */
namespace HexCheats
{
	struct FLine
	{
		/** The heading the line stands under, the command with its arguments and what it does */
		const TCHAR* Section;
		const TCHAR* Command;
		const TCHAR* Text;
	};

	TConstArrayView<FLine> GetLines();

	/** What an argument of the cheats takes on the planet open, as the developer asked on 2026-11-14 for the arguments given as words: the
	 *  numbers of the players, the factions, the creatures of each faction by level, the heroes and castles by id, and the resources */
	struct FArgumentLine
	{
		FString Argument;
		FString Values;
	};

	/** The heading those values stand under on the sheet and in the log */
	inline const TCHAR* ArgumentSection = TEXT("What the arguments take");

	/** Empty without the heroes of a built planet */
	TArray<FArgumentLine> GetArgumentLines(const UHexHeroes* Heroes);

	/** All of them as lines of text, for the log, with the values of the arguments when the heroes of a planet are given */
	FString GetHelpText(const UHexHeroes* Heroes = nullptr);

	/** Logs them and shows that they are in the log */
	void LogHelp(const UHexHeroes* Heroes = nullptr);

	/** The kinds of things marked on the map with a line up and a label, under the fog of war too (hex.Cheat enemies and hex.Cheat show, as the
	 *  developer asked on 2026-11-15 for the artifacts, resources, mines, heroes and castles), a bit each, drawn by SHexEdgeMarkers */
	enum EMarkKind : uint32
	{
		/** The neutral stacks and the heroes of other players */
		MarkEnemies = 1 << 0,
		/** The artifacts lying on the map */
		MarkArtifacts = 1 << 1,
		/** The piles of resources and the chests */
		MarkResources = 1 << 2,
		/** The mines and mills */
		MarkMines = 1 << 3,
		/** Every hero, the person's too */
		MarkHeroes = 1 << 4,
		/** Every castle */
		MarkCastles = 1 << 5,
		/** The boats nobody sails and the shipyards */
		MarkBoats = 1 << 6,
		/** The neutral stacks alone */
		MarkNeutrals = 1 << 7,
	};
	inline constexpr uint32 AllMarks = (1 << 8) - 1;

	uint32 GetShownMarks();
	void SetShownMarks(uint32 Kinds);

	/** How far in cells from the point the camera looks at the marks of hex.Cheat show reach, as the developer asked on 2026-11-15; 0, the
	 *  default, the whole layer shown */
	int32 GetShownMarksReach();
	void SetShownMarksReach(int32 Cells);

	/** The bit of a kind by its name (enemies, artifacts, resources, mines, heroes, castles, boats, neutrals, or the singular), 0 for none */
	uint32 FindMarkKind(const FString& Name);

	/** The names of the kinds of a set of bits, joined */
	FString GetMarkKindNames(uint32 Kinds, const TCHAR* Separator);

	/** Whether the stacks and heroes of the other players are marked, even under the fog of war (hex.Cheat enemies) */
	bool AreEnemiesMarked();
}
