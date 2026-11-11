// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UHexHeroes;

DECLARE_DELEGATE_OneParam(FOnHexHeroChosen, int32);

/**
 *  The panel of the heroes at the bottom right of the planet, shown while the game is played: the day, a line for each hero with its player,
 *  its class and the movement it has left, the attributes of the one chosen, what its way to the cell it is pointed to costs, and the buttons
 *  that end the day and start the game again. A click on a line chooses that hero and shows it, and its buttons never take the keyboard. It
 *  also draws the numbers of the days over the marks of that way
 */
class SHexHeroPanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexHeroPanel) {}
		/** A line of a hero was clicked */
		SLATE_EVENT(FOnHexHeroChosen, OnHeroChosen)
		/** The button that ends the day was clicked */
		SLATE_EVENT(FSimpleDelegate, OnEndTurn)
		/** The button that starts the game again was clicked */
		SLATE_EVENT(FSimpleDelegate, OnRestart)
		/** Gives where a point of the world lies on the viewport, in its pixels, or false when it does not show: for the numbers of the days
		 *  on the way of the hero chosen */
		SLATE_ARGUMENT(TFunction<bool(const FVector&, FVector2D&)>, OnProject)
		/** The keys that end the day and choose the next hero, for the labels */
		SLATE_ARGUMENT(FString, EndTurnKeys)
		SLATE_ARGUMENT(FString, NextHeroKeys)
		SLATE_ARGUMENT(FString, GoKeys)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Gives the panel the heroes of the planet shown, or null */
	void SetHeroes(UHexHeroes* InHeroes);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Draws the numbers of the days over the marks of the way shown (UHexHeroes::GetPathLabels), under the panel itself */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	TWeakObjectPtr<UHexHeroes> Heroes;
	TSharedPtr<SVerticalBox> List;
	FOnHexHeroChosen OnHeroChosen;
	FSimpleDelegate OnEndTurn;
	FSimpleDelegate OnRestart;
	TFunction<bool(const FVector&, FVector2D&)> OnProject;

	/** The list revision the lines were made for (UHexHeroes::GetListRevision), -1 for none */
	int32 ListedRevision = -1;

	/** Makes the lines of the heroes again */
	void RebuildList();
};
