// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexUiConfig.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

/**
 *  The cursor of the game (EHexCursor, DA_HexUi), as the developer asked on 2026-11-12: drawn by Slate at the mouse over everything else of
 *  the game while the cursor of the system is hidden there (AHexPlanetPlayerController sets EMouseCursor::None), and the default arrow of the
 *  game where nothing special lies under it. It draws nothing where a widget asks for a cursor of its own, as a text box does, nor without a
 *  picture for the cursor, when the system cursor shows instead (UsesSystemCursor)
 */
class SHexGameCursor : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SHexGameCursor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The cursor to draw, and for one that turns (BattleStrike) the angle on the screen it points at, in degrees clockwise from the right */
	void SetCursor(EHexCursor InCursor, float InAngle = 0.0f) { Cursor = InCursor; Angle = InAngle; }
	EHexCursor GetGameCursor() const { return Cursor; }

	/** Whether the cursor of the system is to show in place of this one: the game cursors are off or this one has no picture */
	bool UsesSystemCursor() const;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:

	/** The frames of the picture of each cursor by EHexCursor, loaded once, how fast they play, its hot spot and its size */
	TArray<TArray<TSharedPtr<FSlateBrush>>> Brushes;
	TArray<float> Rates;
	TArray<TStrongObjectPtr<UTexture2D>> Textures;
	TArray<FVector2D> HotSpots;
	TArray<float> Sizes;

	EHexCursor Cursor = EHexCursor::Default;
	float Angle = 0.0f;

	const FSlateBrush* GetBrush(EHexCursor Which) const;
};
