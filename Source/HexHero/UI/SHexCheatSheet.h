// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UHexHeroes;

/**
 *  The sheet of the cheats and console commands for trying the game (HexCheats::GetLines), as the developer asked on 2026-11-12: it covers the
 *  screen with a see-through black (CheatSheetOpacity of DA_HexUi) and lists them under their headings, the command and what it does, and
 *  after them what their arguments take on the planet open (HexCheats::GetArgumentLines, 2026-11-14); Ctrl+H shows and hides it. It is
 *  modal, as the developer asked on 2026-11-15 that any UI over the map be: while it shows it takes the keys and the mouse, so nothing on the
 *  map is chosen nor ordered, and Escape, Ctrl+H or a click closes it (OnClose)
 */
class SHexCheatSheet : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexCheatSheet) {}
		/** Asked to close: Escape, Ctrl+H or a click on it */
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Lists the lines again with the values of the arguments of the planet of these heroes, none without them, as they change with the planet */
	void Refresh(const UHexHeroes* Heroes);

	bool IsOpen() const { return GetVisibility().IsVisible(); }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }

private:

	FSimpleDelegate OnClose;
	TSharedPtr<SVerticalBox> List;
};
