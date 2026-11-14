// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

class SVerticalBox;
class SWidget;
class UHexHeroes;
class UTexture2D;
struct FHexStack;
struct FSlateBrush;

/**
 *  The panel of a castle, as the developer chose on 2026-11-11: it stands nearer the middle of the screen than its edge, with the castle
 *  a little right of the middle beside it, and opens when a person clicks a castle of its own twice, presses the key that sends a hero
 *  while one is chosen, or a hero of it goes into one; anything done with the mouse outside it closes it, and under it nothing of the
 *  planet is picked (AHexPlanetPlayerController). It lists the buildings of
 *  the castle with what the next level of each costs and a button that builds it, the creatures its dwellings offer with buttons that hire
 *  them, into the garrison or into the army of a hero of the owner that stands beside the castle, and the stacks of the garrison and of
 *  that hero with buttons that move them across. Its buttons never take the keyboard. It is made again whenever a castle, an army or what
 *  the player has changed (UHexCastles::GetRevision).
 *  Since 2026-11-13, as the developer asked for a panel that is clearer, it has three tabs: Buildings, a tree of the buildings by their
 *  levels as pictures, the next one a button that builds it; Recruit, the creatures offered as cards with their portraits, hired into the
 *  garrison or a hero of the owner chosen among those in or beside the castle; and Armies, the garrison beside the hero chosen, a tab for
 *  each hero in or beside the castle, with their stacks and their catapults
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
	bool IsOpen() const { return OpenId != 0 || ArmyHero != 0; }

	/** Shows the army of a hero, and of another of its player beside it it met (0 for none), as the developer asked on 2026-11-13: its seven
	 *  slots, whose stacks a click swaps, joins and moves, Shift half of one and Ctrl one creature, and buttons that split one in two */
	void OpenArmy(int32 HeroId, int32 OtherId);

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

	/** The hero whose army is shown, the other one it met, and the stack chosen in either, 0 and INDEX_NONE for none */
	int32 ArmyHero = 0;
	int32 OtherHero = 0;
	int32 ChosenStackHero = 0;
	int32 ChosenStackSlot = INDEX_NONE;
	void RebuildArmy();
	TSharedRef<SWidget> MakeStackPortrait(int32 Faction, int32 Creature, bool bUpgraded, float Size);
	/** Seven slots of an army (Key a hero, 0 the garrison) whose stacks a click chooses and moves (Arrange: from key and slot to key and slot,
	 *  a count, 0 for all), and the buttons that split the stack chosen; GetArmy gives the army of a key as it is now */
	TSharedRef<SWidget> MakeSlotRow(TConstArrayView<FHexStack> Army, int32 Key, TFunction<void(int32, int32, int32, int32, int32)> Arrange,
		TFunction<const TArray<FHexStack>*(int32)> GetArmy, TFunction<TSharedPtr<SWidget>(int32, const FHexStack&)> MakeCorner = nullptr);
	TSharedRef<SWidget> MakeSplitButtons(TFunction<void(int32, int32, int32, int32, int32)> Arrange, TFunction<const TArray<FHexStack>*(int32)> GetArmy);
	int32 ShownRevision = -1;
	uint32 ShownStock = 0;

	/** How high the panel may be, so it ends over the panel of the heroes: the height of the viewport less what lies over and under it;
	 *  and how far from the left of the viewport it starts, so its middle lies at PanelCenterX of DA_HexCastles */
	float MaxHeight = 440.0f;
	float PanelLeft = 240.0f;

	/** The tab shown, the hero chosen among those in or beside the castle, and whether what is hired goes to the garrison instead */
	enum class ETab : uint8 { Buildings, Recruit, Armies };
	ETab Tab = ETab::Buildings;
	int32 ChosenHero = 0;
	bool bHireToGarrison = false;

	/** The brushes of the pictures, kept with their textures while the panel lives */
	TMap<UTexture2D*, TSharedPtr<FSlateBrush>> Brushes;
	TArray<TStrongObjectPtr<UTexture2D>> Textures;
	const FSlateBrush* GetBrush(UTexture2D* Texture, float Size);

	/** Makes the lines of the castle again */
	void RebuildBody();
};
