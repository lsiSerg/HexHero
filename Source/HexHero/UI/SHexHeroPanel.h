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
		/** A line of a castle was clicked */
		SLATE_EVENT(FOnHexHeroChosen, OnCastleChosen)
		/** Go beside a hero that has a way: it walks it */
		SLATE_EVENT(FOnHexHeroChosen, OnHeroGo)
		/** Army beside a hero: its army, skills and spell book open */
		SLATE_EVENT(FOnHexHeroChosen, OnHeroArmy)
		/** Magic beside a hero: the spells it may cast on the planet */
		SLATE_EVENT(FOnHexHeroChosen, OnHeroMagic)
		/** The button that ends the day was clicked */
		SLATE_EVENT(FSimpleDelegate, OnEndTurn)
		/** The button that starts the game again was clicked */
		SLATE_EVENT(FSimpleDelegate, OnRestart)
		/** The button Economy on the bar of the resources was clicked */
		SLATE_EVENT(FSimpleDelegate, OnEconomy)
		/** Under the buttons of the turn, at the bottom right, as the developer asked on 2026-11-15 when at the top left they ran into the bar of
		 *  the resources: Auto, lit while the game plays for the person (UHexHeroes::SetPlayerAuto), and how fast the game runs, 1x, 2x or 5x
		 *  in turn */
		SLATE_EVENT(FSimpleDelegate, OnAuto)
		SLATE_ATTRIBUTE(bool, IsAuto)
		SLATE_EVENT(FSimpleDelegate, OnSpeed)
		SLATE_ATTRIBUTE(FText, SpeedText)
		/** The hero chosen is left for today */
		SLATE_EVENT(FSimpleDelegate, OnSkip)
		SLATE_ARGUMENT(FString, SkipKeys)
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

	/** A hint at the bottom of the screen in place of the one of what is chosen, while a spell of the planet is aimed; empty gives it back */
	void SetHint(const FString& InHint) { HintOverride = InHint; }

	/** The hero whose row of the list the cursor is on, 0 for none, and the point of the screen left of the row, level with its top, where the
	 *  panel at the cursor of the map shows what it is, as the developer asked on 2026-11-15 */
	int32 GetHoveredHero(FVector2D& OutAnchor) const;

	/** Hides the hint while a panel lies over the map */
	void SetHintHidden(bool bHidden) { bHintHidden = bHidden; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Draws the numbers of the days over the marks of the way shown (UHexHeroes::GetPathLabels), under the panel itself */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	TWeakObjectPtr<UHexHeroes> Heroes;
	TSharedPtr<SVerticalBox> List;
	FOnHexHeroChosen OnHeroChosen;
	FOnHexHeroChosen OnCastleChosen;
	FOnHexHeroChosen OnHeroGo;
	FOnHexHeroChosen OnHeroArmy;
	FOnHexHeroChosen OnHeroMagic;

	/** The hint at the bottom in the middle of the screen: what is set, else what a click where the cursor is would do, else what to do with
	 *  the hero or castle chosen, on the turn of the person */
	FString HintOverride;
	bool bHintHidden = false;
	FText GetHint(const FText& HeroHow, const FText& CastleHow) const;

	/** The rows of the heroes of the list by their Ids */
	TMap<int32, TSharedPtr<SWidget>> HeroRows;

	/** The revision of the castles the lines were made for (UHexCastles::GetRevision) */
	int32 ListedCastles = -1;
	FSimpleDelegate OnEndTurn;
	FSimpleDelegate OnRestart;
	FSimpleDelegate OnSkip;
	TFunction<bool(const FVector&, FVector2D&)> OnProject;

	/** The list revision the lines were made for (UHexHeroes::GetListRevision), -1 for none */
	int32 ListedRevision = -1;

	/** The most points of movement each hero had above its own of a day today, by its Id, of which each plus of its column is a share */
	struct FMovePeak
	{
		int32 Day = 0;
		float Above = 0.0f;
	};
	mutable TMap<int32, FMovePeak> MovePeaks;

	/** Makes the lines of the heroes again */
	void RebuildList();
};
