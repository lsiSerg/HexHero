// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UHexHeroes;

/**
 *  The economy of a player, as the developer asked on 2026-11-15 ("кнопка щоб дивитись економіку, надходження - шахти, поточні ресурси"): a
 *  window in the middle of the screen over a shade that blocks what lies behind it, with what the player has of each resource, what its mines
 *  and the halls of its castles give a day and so a week, and its mines and castles one by one. A click on the shade or Escape closes it
 */
class SHexEconomyPanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexEconomyPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows the economy of a player */
	void Open(UHexHeroes* InHeroes, int32 InPlayer);
	void Close();
	bool IsOpen() const { return bOpen; }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:

	TWeakObjectPtr<UHexHeroes> Heroes;
	int32 Player = 0;
	TSharedPtr<SVerticalBox> Body;
	bool bOpen = false;

	void Rebuild();
};
