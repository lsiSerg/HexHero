// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexCheatSheet.h"
#include "HexCheats.h"
#include "HexUiConfig.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexCheatSheet"

void SHexCheatSheet::Construct(const FArguments& InArgs)
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const auto Font = [](int32 Size, bool bBold) { return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size); };
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 14.0f)
		[
			SNew(STextBlock).Font(Font(18, true)).ColorAndOpacity(FLinearColor(1.0f, 0.88f, 0.55f, 1.0f))
			.Text(LOCTEXT("Title", "Cheats and commands for trying the game"))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 14.0f)
		[
			SNew(STextBlock).Font(Font(10, false)).ColorAndOpacity(FLinearColor(0.75f, 0.78f, 0.85f, 1.0f))
			.Text(LOCTEXT("Hint", "Type them in the console (~). Ctrl+H hides this sheet; hex.Cheat help puts it in the log"))
		];
	const TCHAR* Section = nullptr;
	for (const HexCheats::FLine& Line : HexCheats::GetLines())
	{
		if (!Section || FCString::Strcmp(Section, Line.Section) != 0)
		{
			Section = Line.Section;
			List->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock).Font(Font(12, true)).ColorAndOpacity(FLinearColor(0.6f, 0.85f, 1.0f, 1.0f)).Text(FText::FromString(Line.Section))
			];
		}
		List->AddSlot().AutoHeight().Padding(12.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(440.0f)
				[
					SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(Line.Command))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock).Font(Font(10, false)).ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f)).AutoWrapText(true).Text(FText::FromString(Line.Text))
			]
		];
	}
	ChildSlot
	[
		SNew(SBorder).BorderImage(White).Visibility(EVisibility::HitTestInvisible)
		.BorderBackgroundColor_Lambda([]() { return FLinearColor(0.0f, 0.0f, 0.0f, UHexUiConfig::Load()->CheatSheetOpacity); })
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).MaxDesiredWidth(1100.0f)
			[
				List
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
