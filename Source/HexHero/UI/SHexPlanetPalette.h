// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UHexPlanetPainter;

/**
 *  Palette of the brush that paints the levels and biomes of a planet, at the left of the screen while painting is on: the levels in their colors with
 *  their keys, the size of the brush, Undo and Redo, and whether the surface is being rebuilt. Its buttons never take the keyboard, so the keys
 *  of the game keep working.
 */
class SHexPlanetPalette : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexPlanetPalette) {}
		/** Painter whose brush the palette shows and sets */
		SLATE_ARGUMENT(TWeakObjectPtr<UHexPlanetPainter>, Painter)
		/** Keys of the actions, shown next to their buttons */
		SLATE_ARGUMENT(FString, PaintModeKeys)
		SLATE_ARGUMENT(FString, BrushSizeKeys)
		SLATE_ARGUMENT(FString, UndoKeys)
		SLATE_ARGUMENT(FString, RedoKeys)
		SLATE_ARGUMENT(FString, SaveKeys)
	SLATE_END_ARGS()

	/** Creates the content of the palette */
	void Construct(const FArguments& InArgs);

private:

	/** Painter whose brush the palette shows and sets */
	TWeakObjectPtr<UHexPlanetPainter> Painter;
};
