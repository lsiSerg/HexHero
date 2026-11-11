// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexModeBar.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexModeBar"

namespace
{
	const float ModeBarMargin = 16.0f;
	const float ModeBarWidth = 270.0f;
	const float ModeBarHeight = 30.0f;
	const float ModeBarGap = 6.0f;
	const FLinearColor ModeOnColor(1.0f, 0.75f, 0.1f, 1.0f);
	const FLinearColor ModeOffColor(0.3f, 0.3f, 0.3f, 1.0f);
}

float SHexModeBar::GetPaletteTop()
{
	return ModeBarMargin + ModeBarHeight + ModeBarGap;
}

void SHexModeBar::Construct(const FArguments& InArgs)
{
	// the widget covers the viewport, of which only the buttons take the mouse
	SetVisibility(EVisibility::SelfHitTestInvisible);

	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const auto MakeModeButton = [&Font](const FText& Label, const FString& Keys, const FText& Tip, const FSimpleDelegate& OnClicked, const TAttribute<bool>& IsOn)
	{
		return SNew(SButton)
			.IsFocusable(false)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTipText(Tip)
			.ButtonColorAndOpacity_Lambda([IsOn]() { return IsOn.Get(false) ? ModeOnColor : ModeOffColor; })
			.OnClicked_Lambda([OnClicked]()
			{
				OnClicked.ExecuteIfBound();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(Font)
				.Text(Keys.IsEmpty() ? Label : FText::Format(LOCTEXT("ModeKeys", "{0} ({1})"), Label, FText::FromString(Keys)))
			];
	};

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(ModeBarMargin, ModeBarMargin, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(ModeBarWidth)
			.HeightOverride(ModeBarHeight)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					MakeModeButton(LOCTEXT("Edit", "Edit"), InArgs._EditKeys, LOCTEXT("EditTip", "Paint the planet and place heroes with the palette"), InArgs._OnEdit, InArgs._IsEditing)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(3.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeModeButton(LOCTEXT("Play", "Play"), InArgs._PlayKeys, LOCTEXT("PlayTip", "Play the game: choose a hero, point it to a cell and send it; it needs a hero on the planet"), InArgs._OnPlay, InArgs._IsPlaying)
				]
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
