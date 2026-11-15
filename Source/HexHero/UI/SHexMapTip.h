// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexHeroes.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

class SBorder;
class SVerticalBox;
class UTexture2D;
struct FSlateBrush;

/**
 *  The panel at the cursor while the game is played, as the developer asked on 2026-11-13: what a hero of the person leads and has left, what
 *  a pile or a mine holds or gives, how many creatures a neutral stack, a hero of another player or a castle seems to have, and a rough
 *  forecast of a battle against it (UHexHeroes::GetMapTip). It takes no input and keeps on the screen
 */
class SHexMapTip : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexMapTip) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Shows a tip, or hides the panel for an empty one; made again only when it changed */
	void SetTip(const FHexMapTip& Tip);

	/** Where the panel stands instead of at the cursor: its right edge at a point of the screen, its top level with it, as beside the row of a
	 *  hero in the list, as the developer asked on 2026-11-15; unset gives it back to the cursor */
	void SetAnchor(const TOptional<FVector2D>& InAnchor) { Anchor = InAnchor; }

	/** What the panel shows of a tip, and a tooltip of a widget that shows the same, for the buttons that hire a hero or buy an artifact */
	static TSharedRef<SVerticalBox> MakeContent(const FHexMapTip& Tip);
	static TSharedRef<class SToolTip> MakeToolTip(const FHexMapTip& Tip);

private:

	TSharedPtr<SBorder> Panel;
	TSharedPtr<SVerticalBox> Body;
	FHexMapTip Shown;
	bool bShown = false;
	TOptional<FVector2D> Anchor;

	TMap<UTexture2D*, TSharedPtr<FSlateBrush>> Brushes;
	TArray<TStrongObjectPtr<UTexture2D>> Textures;
	const FSlateBrush* GetBrush(UTexture2D* Texture);
};
