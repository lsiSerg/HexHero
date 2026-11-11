// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UHexHeroes;

DECLARE_DELEGATE_OneParam(FOnHexHeroChosen, int32);

/**
 *  The panel of the heroes at the bottom right of the planet: the day, a line for each hero with its player, its class and the movement it has
 *  left, the attributes of the one chosen, what its way to the cell under the cursor costs, and the button that ends the day. A click on a
 *  line chooses that hero and shows it. It is hidden on a planet without heroes, and its buttons never take the keyboard
 */
class SHexHeroPanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexHeroPanel) {}
		/** A line of a hero was clicked */
		SLATE_EVENT(FOnHexHeroChosen, OnHeroChosen)
		/** The button that ends the day was clicked */
		SLATE_EVENT(FSimpleDelegate, OnEndTurn)
		/** The keys that end the day and choose the next hero, for the labels */
		SLATE_ARGUMENT(FString, EndTurnKeys)
		SLATE_ARGUMENT(FString, NextHeroKeys)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Gives the panel the heroes of the planet shown, or null */
	void SetHeroes(UHexHeroes* InHeroes);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	TWeakObjectPtr<UHexHeroes> Heroes;
	TSharedPtr<SVerticalBox> List;
	FOnHexHeroChosen OnHeroChosen;
	FSimpleDelegate OnEndTurn;

	/** The list revision the lines were made for (UHexHeroes::GetListRevision), -1 for none */
	int32 ListedRevision = -1;

	/** Makes the lines of the heroes again */
	void RebuildList();
};
