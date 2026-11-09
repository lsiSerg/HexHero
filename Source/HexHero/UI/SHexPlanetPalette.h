// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetLayers.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UHexPlanetPainter;

/**
 *  Palette of the brush that paints a planet, at the left of the screen while painting is on. At the top two tabs, Surface and Underground, which
 *  follow the layer shown and travel to the other when clicked, the tool that moves the planet and the size of the brush; then the tabs of the
 *  layer (EHexPaintTab), each with its own body in a widget switcher built once: Ground (levels and biomes), Relief, Objects, Rivers and Entrances
 *  on the surface, Maze and Links underground; at the bottom Undo and Redo of the layer and what the mouse does. Its buttons never take the
 *  keyboard, so the keys of the game keep working.
 */
class SHexPlanetPalette : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexPlanetPalette) {}
		/** Painter whose brush the palette shows and sets */
		SLATE_ARGUMENT(TWeakObjectPtr<UHexPlanetPainter>, Painter)
		/** Asks to travel to a layer, as its tab was clicked */
		SLATE_ARGUMENT(TFunction<void(EHexPlanetLayer)>, OnLayerClicked)
		/** Keys of the actions, shown next to their buttons */
		SLATE_ARGUMENT(FString, PaintModeKeys)
		SLATE_ARGUMENT(FString, BrushSizeKeys)
		SLATE_ARGUMENT(FString, UndoKeys)
		SLATE_ARGUMENT(FString, RedoKeys)
		SLATE_ARGUMENT(FString, SaveKeys)
		SLATE_ARGUMENT(FString, LayerKeys)
	SLATE_END_ARGS()

	/** Creates the content of the palette */
	void Construct(const FArguments& InArgs);

private:

	/** Painter whose brush the palette shows and sets */
	TWeakObjectPtr<UHexPlanetPainter> Painter;
};
