// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UTexture2D;

/** One answer of a choice: its picture, its name, a line about it, and its color */
struct FHexChoice
{
	TSoftObjectPtr<UTexture2D> Icon;
	/** How many frames side by side the picture holds, of which the first shows */
	int32 IconFrames = 1;
	FString Title;
	FString Text;
	FLinearColor Color = FLinearColor::White;
};

DECLARE_DELEGATE_OneParam(FOnHexChoice, int32);

/**
 *  A choice the person must make before the game goes on, as the developer asked on 2026-11-15 for the skills a hero gains with a level, the
 *  chests that give gold or experience and the neutral creatures that offer to join: a window in the middle of the screen over a shade that
 *  blocks what lies behind it, a title, a line and a button for each answer with its picture. Only an answer closes it
 */
class SHexChoiceDialog : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexChoiceDialog) {}
		SLATE_EVENT(FOnHexChoice, OnChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows a choice; Key tells it apart, so the same choice is not shown again while it is up. CancelIndex is the answer Escape gives, as
	 *  "Not now" of a shipyard, INDEX_NONE where the person must choose one (a skill, gold or experience, join or fight) */
	void Open(const FString& Key, const FString& Title, const FString& Text, const TArray<FHexChoice>& Choices, int32 CancelIndex = INDEX_NONE);
	void Close();
	bool IsOpen() const { return bOpen; }
	const FString& GetKey() const { return ShownKey; }

	/** Gives the answer of CancelIndex, as Escape does; false, and it stays open, without one */
	bool Cancel();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:

	FOnHexChoice OnChosen;
	int32 CancelChoice = INDEX_NONE;
	TSharedPtr<SVerticalBox> Body;
	bool bOpen = false;
	FString ShownKey;
};
