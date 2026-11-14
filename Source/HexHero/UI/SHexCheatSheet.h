// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UHexHeroes;

/**
 *  The sheet of the cheats and console commands for trying the game (HexCheats::GetLines), as the developer asked on 2026-11-12: it covers the
 *  screen with a see-through black (CheatSheetOpacity of DA_HexUi) and lists them under their headings, the command and what it does, and
 *  after them what their arguments take on the planet open (HexCheats::GetArgumentLines, 2026-11-14); Ctrl+H shows and hides it, and it
 *  takes no input
 */
class SHexCheatSheet : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexCheatSheet) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Lists the lines again with the values of the arguments of the planet of these heroes, none without them, as they change with the planet */
	void Refresh(const UHexHeroes* Heroes);

private:

	TSharedPtr<SVerticalBox> List;
};
