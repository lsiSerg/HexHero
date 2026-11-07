// Copyright Epic Games, Inc. All Rights Reserved.


#include "SHexHoverInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** Distance from the cursor, so the panel does not cover the hex under it */
	const FVector2D CursorOffset(20.0, 20.0);
}

void SHexHoverInfo::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);

	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	[
		SAssignNew(Panel, SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
		.Padding(8.0f)
		.Visibility(EVisibility::Collapsed)
		[
			SAssignNew(Text, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		]
	];
}

void SHexHoverInfo::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const FVector2D Cursor = AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	const FVector2D Size = Panel->GetDesiredSize();
	const FVector2D Available = AllottedGeometry.GetLocalSize();

	// near the right or bottom edge the panel goes to the other side of the cursor
	const FVector2D Position(
		Cursor.X + CursorOffset.X + Size.X > Available.X ? Cursor.X - CursorOffset.X - Size.X : Cursor.X + CursorOffset.X,
		Cursor.Y + CursorOffset.Y + Size.Y > Available.Y ? Cursor.Y - CursorOffset.Y - Size.Y : Cursor.Y + CursorOffset.Y);

	Panel->SetRenderTransform(FSlateRenderTransform(Position));
}

void SHexHoverInfo::SetInfo(const FString& Info)
{
	Text->SetText(FText::FromString(Info));
	Panel->SetVisibility(Info.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}
