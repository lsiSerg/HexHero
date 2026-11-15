// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexCheatSheet.h"
#include "HexCheats.h"
#include "HexUiConfig.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexCheatSheet"

namespace
{
	FSlateFontInfo SheetFont(int32 Size, bool bBold)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	/** Widths of the columns of the sheet, in units of Slate before it is scaled to fit the screen: the cheats on the left and what their
	 *  arguments take on the right, each a name and a text that wraps at its width, so the sheet has a size of its own the scale box fits */
	constexpr float CheatNameWidth = 330.0f;
	constexpr float CheatTextWidth = 540.0f;
	constexpr float ArgumentNameWidth = 170.0f;
	constexpr float ArgumentTextWidth = 520.0f;
	constexpr float ColumnGap = 48.0f;
}

FReply SHexCheatSheet::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Escape, or Ctrl+H, which shows it, closes it; no other key reaches the game meanwhile
	if (InKeyEvent.GetKey() == EKeys::Escape || (InKeyEvent.GetKey() == EKeys::H && InKeyEvent.IsControlDown()))
	{
		OnClose.ExecuteIfBound();
	}
	return FReply::Handled();
}

FReply SHexCheatSheet::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	OnClose.ExecuteIfBound();
	return FReply::Handled();
}

void SHexCheatSheet::Construct(const FArguments& InArgs)
{
	OnClose = InArgs._OnClose;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	SAssignNew(List, SVerticalBox);
	Refresh(nullptr);
	// the whole sheet shrinks to fit the screen it is on, whatever its resolution and however many heroes and castles the planet lists, as the
	// developer asked on 2026-11-14 whether it fits one screen at every resolution; it never grows past its own size
	ChildSlot
	[
		SNew(SBorder).BorderImage(White).Visibility(EVisibility::HitTestInvisible)
		.BorderBackgroundColor_Lambda([]() { return FLinearColor(0.0f, 0.0f, 0.0f, UHexUiConfig::Load()->CheatSheetOpacity); })
		.Padding(24.0f)
		[
			SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				List.ToSharedRef()
			]
		]
	];
}

void SHexCheatSheet::Refresh(const UHexHeroes* Heroes)
{
	List->ClearChildren();
	List->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
	[
		SNew(STextBlock).Font(SheetFont(18, true)).ColorAndOpacity(FLinearColor(1.0f, 0.88f, 0.55f, 1.0f))
		.Text(LOCTEXT("Title", "Cheats and commands for trying the game"))
	];
	List->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
	[
		SNew(STextBlock).Font(SheetFont(10, false)).ColorAndOpacity(FLinearColor(0.75f, 0.78f, 0.85f, 1.0f))
		.Text(LOCTEXT("Hint", "Type them in the console (~). Ctrl+H hides this sheet; hex.Cheat help puts it in the log"))
	];

	const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
	const auto AddSection = [](const TSharedRef<SVerticalBox>& Column, const FString& Section)
	{
		Column->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 3.0f)
		[
			SNew(STextBlock).Font(SheetFont(12, true)).ColorAndOpacity(FLinearColor(0.6f, 0.85f, 1.0f, 1.0f)).Text(FText::FromString(Section))
		];
	};
	const auto AddLine = [](const TSharedRef<SVerticalBox>& Column, const FString& Name, const FString& Text, float NameWidth, float TextWidth)
	{
		Column->AddSlot().AutoHeight().Padding(10.0f, 2.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(NameWidth)
				[
					SNew(STextBlock).Font(SheetFont(10, true)).ColorAndOpacity(FLinearColor::White).WrapTextAt(NameWidth - 8.0f).Text(FText::FromString(Name))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(TextWidth)
				[
					SNew(STextBlock).Font(SheetFont(10, false)).ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f)).WrapTextAt(TextWidth).Text(FText::FromString(Text))
				]
			]
		];
	};

	const TCHAR* Section = nullptr;
	for (const HexCheats::FLine& Line : HexCheats::GetLines())
	{
		if (!Section || FCString::Strcmp(Section, Line.Section) != 0)
		{
			Section = Line.Section;
			AddSection(Left, Line.Section);
		}
		AddLine(Left, Line.Command, Line.Text, CheatNameWidth, CheatTextWidth);
	}

	const TArray<HexCheats::FArgumentLine> Arguments = HexCheats::GetArgumentLines(Heroes);
	if (!Arguments.IsEmpty())
	{
		AddSection(Right, HexCheats::ArgumentSection);
		for (const HexCheats::FArgumentLine& Line : Arguments)
		{
			AddLine(Right, Line.Argument, Line.Values, ArgumentNameWidth, ArgumentTextWidth);
		}
	}

	List->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Left
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(ColumnGap, 0.0f, 0.0f, 0.0f)
		[
			Right
		]
	];
}

#undef LOCTEXT_NAMESPACE
