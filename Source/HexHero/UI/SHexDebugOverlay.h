// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class SGridPanel;
class STextBlock;
class SVerticalBox;

/** Lines of the legend of the keys under one heading */
struct FHexLegendSection
{
	/** Heading, or nothing for lines shown without one */
	FString Title;

	/** Pairs of action and keys */
	TArray<TPair<FString, FString>> Lines;
};

/**
 *  Compact half transparent panels in the top right corner: a red warning, the legend of the keys, and below it panels of settings,
 *  such as the debug view and the camera mode.
 *  It never takes the input from the game.
 */
class SHexDebugOverlay : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexDebugOverlay) {}
	SLATE_END_ARGS()

	/** Creates both panels, hidden */
	void Construct(const FArguments& InArgs);

	/** Fills the legend with pairs of action and keys under their headings, and sets the hint shown in its place while it is hidden */
	void SetLegend(const TArray<FHexLegendSection>& Sections, const FString& Hint);

	/** Shows or hides the legend */
	void SetLegendVisible(bool bVisible);

	/** Returns true if the legend is shown */
	bool IsLegendVisible() const;

	/** Shows a red line above the legend. An empty text hides it */
	void SetWarning(const FString& Text);

	/** Adds a hidden settings panel below the others, and returns its index */
	int32 AddStatusPanel();

	/** Fills one settings panel with pairs of name and value. No lines hide it */
	void SetStatus(int32 PanelIndex, const TArray<TPair<FString, FString>>& Lines);

	/**
	 *  Gives a mode of the view its own panel of settings, shown while they differ from the usual view and for a while after any change.
	 *  GetStatus fills the pairs of name and value and returns true for the usual view. No lines mean the mode is not there yet,
	 *  and its first lines count as no change
	 */
	void AddViewStatus(TFunction<bool(TArray<TPair<FString, FString>>&)> GetStatus);

	/** Shows the settings of each mode of the view while they differ from the usual view, and for a while after any change, at a real time in seconds */
	void UpdateViewStatus(double Now);

private:

	/** Settings of one mode of the view, shown in their own panel */
	struct FViewStatusSource
	{
		/** Gives the settings as pairs of name and value, and returns true if they are the usual view */
		TFunction<bool(TArray<TPair<FString, FString>>&)> GetStatus;

		/** Settings shown last, to find when they change */
		TArray<TPair<FString, FString>> Lines;

		/** Real time of the last change of the settings */
		double ChangeTime = -1.0e9;

		/** Panel of the settings */
		int32 PanelIndex = INDEX_NONE;
	};

	/** Modes of the view whose settings are shown */
	TArray<FViewStatusSource> ViewStatusSources;

	/** One panel of settings */
	struct FStatusPanel
	{
		/** Panel */
		TSharedPtr<SBorder> Border;

		/** Lines of the settings */
		TSharedPtr<SGridPanel> Grid;

		/** Lines shown, so the panel is rebuilt only when they change */
		TArray<TPair<FString, FString>> Lines;
	};

	/** Fills a grid with two columns of text */
	static void FillGrid(SGridPanel& Grid, const TArray<TPair<FString, FString>>& Lines);

	/** Adds one pair of name and value as a row of a grid, with the name moved right by the indent */
	static void AddGridLine(SGridPanel& Grid, int32 Row, const TPair<FString, FString>& Line, float Indent);

	/** Red line at the top, such as a view that is not saved */
	TSharedPtr<STextBlock> Warning;

	/** How to open the legend, shown while it is hidden */
	TSharedPtr<STextBlock> LegendHint;

	/** Panel of the legend */
	TSharedPtr<SBorder> LegendPanel;

	/** Lines of the legend */
	TSharedPtr<SGridPanel> LegendGrid;

	/** Column of the settings panels */
	TSharedPtr<SVerticalBox> StatusBox;

	/** Panels of settings, each shown on its own */
	TArray<FStatusPanel> StatusPanels;
};
