// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class STextBlock;

/**
 *  Debug panel that follows the mouse cursor and shows a text. It never takes the input from the game.
 */
class SHexHoverInfo : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexHoverInfo) {}
	SLATE_END_ARGS()

	/** Creates the content of the panel */
	void Construct(const FArguments& InArgs);

	/** Moves the panel to the cursor and keeps it on screen */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Sets the text. An empty text hides the panel */
	void SetInfo(const FString& Info);

private:

	/** Panel that moves with the cursor */
	TSharedPtr<SBorder> Panel;

	/** Text of the panel */
	TSharedPtr<STextBlock> Text;
};
