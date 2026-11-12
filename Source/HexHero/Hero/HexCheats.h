// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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

	/** All of them as lines of text, for the log */
	FString GetHelpText();

	/** Logs them and shows that they are in the log */
	void LogHelp();

	/** Whether the stacks and heroes of the other players are marked, even under the fog of war (hex.Cheat enemies) */
	bool AreEnemiesMarked();
	void SetEnemiesMarked(bool bMarked);
}
