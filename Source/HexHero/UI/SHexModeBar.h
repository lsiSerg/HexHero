// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 *  The buttons at the top left of the planet. Two choose what the mouse does, as the developer asked on 2026-11-11: Edit, the brush with
 *  its palette below them, and Play, the game, in which heroes are chosen and sent; each is lit while it is on. The third, which the
 *  developer asked for on 2026-11-12, takes the camera to the other layer, as its key does, and names the layer it goes to. Each shows its
 *  key, and none takes the keyboard
 */
class SHexModeBar : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexModeBar) {}
		SLATE_EVENT(FSimpleDelegate, OnEdit)
		SLATE_EVENT(FSimpleDelegate, OnPlay)
		SLATE_ATTRIBUTE(bool, IsEditing)
		SLATE_ATTRIBUTE(bool, IsPlaying)
		SLATE_ARGUMENT(FString, EditKeys)
		SLATE_ARGUMENT(FString, PlayKeys)
		SLATE_EVENT(FSimpleDelegate, OnLayer)
		SLATE_ATTRIBUTE(bool, IsUnderground)
		SLATE_ARGUMENT(FString, LayerKeys)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** How far below the top of the viewport the palette starts, so it stands under the bar */
	static float GetPaletteTop();
};
