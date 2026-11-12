// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexUiConfig.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class AHexBattleArena;
class SBorder;
class SHorizontalBox;
class UTexture2D;

/**
 *  What a battle on its field shows (AHexBattleArena), as the developer chose on 2026-11-12: the number of creatures of each stack over its
 *  figure in the color of its side, the numbers that float up, the sides and the round at the top, the stacks in the order they act at the
 *  bottom with what a click does now, the buttons of the person (Wait, Defend, a spell of the hero, Auto, Quick, Retreat), the log of the last
 *  events at the left, what the unit under the cursor is at the right, and once it is over how it ended with OK, which takes the battle away
 */
class SHexBattlePanel : public SCompoundWidget
{
public:

	/** Where a point of the world shows on the viewport, in its pixels, or false behind the camera */
	using FProject = TFunction<bool(const FVector& World, FVector2D& OutScreen)>;

	SLATE_BEGIN_ARGS(SHexBattlePanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AHexBattleArena>, Arena)
		SLATE_ARGUMENT(FProject, OnProject)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Returns true while the cursor is on a button of the panel, where a click is not one on the field */
	bool IsCursorOnButtons() const;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	TWeakObjectPtr<AHexBattleArena> Arena;
	FProject OnProject;
	TSharedPtr<SHorizontalBox> QueueBox;
	TSharedPtr<SWidget> ButtonBar;
	TSharedPtr<SWidget> SpellBox;
	TSharedPtr<SWidget> ResultBox;
	FString QueueKey;
	bool bSpellsOpen = false;

	/** The panel at the cursor: what it shows now, made again when that changes, and where it stands */
	TSharedPtr<SBorder> TooltipBox;
	FString TooltipKey;
	FVector2D TooltipPosition = FVector2D::ZeroVector;

	void RebuildQueue();
	void RebuildTooltip(const FGeometry& Geometry);
	TSharedRef<SWidget> MakeButton(const FText& Label, TFunction<void()> OnClick, TFunction<bool()> IsEnabled, const FText& Tip);
};
