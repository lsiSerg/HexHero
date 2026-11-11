// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class SWidget;
class UHexHeroes;

/**
 *  The panel of a castle, as the developer chose on 2026-11-11: it stands nearer the middle of the screen than its edge, with the castle
 *  a little right of the middle beside it, and opens when a person clicks a castle of its own twice, presses the key that sends a hero
 *  while one is chosen, or a hero of it goes into one; anything done with the mouse outside it closes it, and under it nothing of the
 *  planet is picked (AHexPlanetPlayerController). It lists the buildings of
 *  the castle with what the next level of each costs and a button that builds it, the creatures its dwellings offer with buttons that hire
 *  them, into the garrison or into the army of a hero of the owner that stands beside the castle, and the stacks of the garrison and of
 *  that hero with buttons that move them across. Its buttons never take the keyboard. It is made again whenever a castle, an army or what
 *  the player has changed (UHexCastles::GetRevision)
 */
class SHexCastlePanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexCastlePanel) {}
		/** A line for the player: what was built or hired, or why not */
		SLATE_ARGUMENT(TFunction<void(const FString&)>, OnMessage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Gives the panel the heroes of the planet shown, or null */
	void SetHeroes(UHexHeroes* InHeroes);

	/** Shows the castle of an Id, and hides the panel */
	void Open(int32 CastleId);
	void Close();
	bool IsOpen() const { return OpenId != 0; }

	/** Returns true while the mouse is over the panel, so a click there is no click outside it */
	bool IsUnderCursor() const;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	TWeakObjectPtr<UHexHeroes> Heroes;
	TSharedPtr<SWidget> Frame;
	TSharedPtr<SVerticalBox> Body;
	TFunction<void(const FString&)> OnMessage;

	/** The castle shown, 0 for none, and what it was made for last */
	int32 OpenId = 0;
	int32 ShownRevision = -1;
	uint32 ShownStock = 0;

	/** How high the panel may be, so it ends over the panel of the heroes: the height of the viewport less what lies over and under it;
	 *  and how far from the left of the viewport it starts, so its middle lies at PanelCenterX of DA_HexCastles */
	float MaxHeight = 440.0f;
	float PanelLeft = 240.0f;

	/** Whether what is hired goes to the hero that stands beside the castle, when one does, or to the garrison */
	bool bHireToHero = true;

	/** Makes the lines of the castle again */
	void RebuildBody();
};
