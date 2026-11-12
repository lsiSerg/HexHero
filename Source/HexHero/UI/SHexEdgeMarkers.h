// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "HexPlanetLayers.h"

class UHexHeroes;

/**
 *  Marks at the edges of the screen that point to the heroes and castles of the person who plays, as the developer asked on 2026-11-12: one
 *  for each on the layer shown that does not show in the view, on the edge in its direction, a round one with a banner for a hero and a square one with
 *  battlements for a castle, in the color of the player, larger and ringed for the one chosen. The mark under the cursor shows the name of
 *  its hero or castle; marks that lie in a heap move apart along the edge while the cursor is on them, so their names do not cover each other,
 *  and the mark of the one chosen always keeps the others off itself. It takes no input itself: the controller asks it what lies under the
 *  cursor when the button is clicked (GetHovered), and a click there chooses and shows that hero or castle
 */
class SHexEdgeMarkers : public SLeafWidget
{
public:

	/** Where a point of a layer shows: on the viewport, in its pixels, when it is in view, else the way to it across the screen. Returns
	 *  true when it is in view */
	using FLocate = TFunction<bool(const FVector3d& Point, EHexPlanetLayer Layer, FVector2D& OutScreen, FVector2D& OutDirection)>;

	SLATE_BEGIN_ARGS(SHexEdgeMarkers) {}
		SLATE_ARGUMENT(FLocate, OnLocate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetHeroes(UHexHeroes* InHeroes) { Heroes = InHeroes; }

	/** Returns true while the cursor is on a mark or on a heap of them that moved apart, when the camera must not scroll at the edge of the
	 *  screen under it, as the developer asked on 2026-11-12 */
	bool IsCursorOnMarks() const { return Marks.IsValidIndex(Hovered) || bSpread; }

	/** Gives the hero or castle whose mark lies under the cursor. Returns false when none does */
	bool GetHovered(bool& bOutCastle, int32& OutId) const;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:

	struct FMark
	{
		bool bCastle = false;
		int32 Id = 0;
		FString Name;
		FLinearColor Color = FLinearColor::White;
		bool bSelected = false;
		/** Where it lies along the edge, as a length around it from the top left corner, clockwise: where it points, and where it is drawn,
		 *  which moves apart from its neighbors */
		float Target = 0.0f;
		float Shown = 0.0f;
		float LabelWidth = 0.0f;
		bool bLabel = false;
	};

	TWeakObjectPtr<UHexHeroes> Heroes;
	FLocate OnLocate;
	TArray<FMark> Marks;

	/** The mark under the cursor, and the one the heap that moved apart is held around, by index, or INDEX_NONE */
	int32 Hovered = INDEX_NONE;
	bool bAnchorCastle = false;
	int32 AnchorId = 0;
	bool bSpread = false;

	/** How far from where it points the mark the heap is held around was drawn when the cursor came to it: it stays there, under the cursor,
	 *  while the heap is apart. Taken to where it points instead, a mark the one chosen had moved off itself went away from under the
	 *  cursor, the heap closed, the mark came back, and so it jumped to and fro, as the developer found on 2026-11-12 */
	float AnchorOffset = 0.0f;

	/** The size of the widget the marks were laid out for */
	FVector2D LaidSize = FVector2D::ZeroVector;

	/** Gives the point of the edge a length around it lies at, and the way into the screen there */
	FVector2D GetEdgePoint(float Along, FVector2D& OutInward) const;
};
