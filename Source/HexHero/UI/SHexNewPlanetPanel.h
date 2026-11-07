// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetFile.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

DECLARE_DELEGATE_OneParam(FOnHexPlanetCreated, const FHexPlanetManifest& /*Manifest*/);

/**
 *  Page of the map menu that makes a new planet: its size and how its levels begin, with the seed and the shares of a random one.
 *  Create passes the planet on, which opens it without writing anything; the view key then asks for its name and saves it.
 */
class SHexNewPlanetPanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexNewPlanetPanel) {}
		/** Called with the planet to open */
		SLATE_EVENT(FOnHexPlanetCreated, OnCreated)
		/** Called when the page is left without making a planet */
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	/** One size of planet in the panel */
	struct FSize
	{
		const TCHAR* Label;
		int32 Frequency;
	};

	/** Sizes the panel offers, smallest first */
	static TConstArrayView<FSize> GetSizes();

	/** Creates the content of the page */
	void Construct(const FArguments& InArgs);

private:

	/** Passes the planet on */
	FReply OnCreateClicked();

	/** Adds a row with a label and a control to a box of rows */
	static void AddRow(const TSharedRef<class SVerticalBox>& Rows, const FText& Label, const TSharedRef<SWidget>& Control, const FText& ToolTip = FText::GetEmpty());

	/** Adds a row with a spin box for a share from 0 to 1, shown as a percentage */
	void AddShareRow(const TSharedRef<class SVerticalBox>& Rows, const FText& Label, double FHexPlanetGeneration::* Share, float Max, const FText& ToolTip);

	/** The planet as it will be written */
	FHexPlanetManifest Manifest;

	/** Error of the last try, shown under the buttons */
	TSharedPtr<STextBlock> ErrorText;

	FOnHexPlanetCreated OnCreated;
	FSimpleDelegate OnBack;
};
