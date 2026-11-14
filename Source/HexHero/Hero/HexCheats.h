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

	/** Whether the stacks and heroes of the other players are marked, even under the fog of war (hex.Cheat enemies) */
	bool AreEnemiesMarked();
	void SetEnemiesMarked(bool bMarked);
}
