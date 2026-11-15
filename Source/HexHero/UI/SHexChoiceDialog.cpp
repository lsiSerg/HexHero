// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexChoiceDialog.h"
#include "SHexSpellBook.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SHexChoiceDialog::Construct(const FArguments& InArgs)
{
	OnChosen = InArgs._OnChosen;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	SetVisibility(EVisibility::Collapsed);
	ChildSlot
	[
		SNew(SOverlay)
		// the shade takes every click, so nothing behind it is picked until the person answers
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f))
			.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(540.0f)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.05f, 0.04f, 0.035f, 0.97f)).Padding(FMargin(18.0f, 14.0f))
				[
					SAssignNew(Body, SVerticalBox)
				]
			]
		]
	];
}

bool SHexChoiceDialog::Cancel()
{
	if (!bOpen || CancelChoice == INDEX_NONE)
	{
		return false;
	}
	const int32 Index = CancelChoice;
	Close();
	OnChosen.ExecuteIfBound(Index);
	return true;
}

FReply SHexChoiceDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Cancel();
	}
	// nothing else reaches the game while a choice is up
	return FReply::Handled();
}

void SHexChoiceDialog::Open(const FString& Key, const FString& Title, const FString& Text, const TArray<FHexChoice>& Choices, int32 CancelIndex)
{
	bOpen = true;
	ShownKey = Key;
	CancelChoice = Choices.IsValidIndex(CancelIndex) ? CancelIndex : int32(INDEX_NONE);
	SetVisibility(EVisibility::Visible);
	Body->ClearChildren();
	Body->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15)).ColorAndOpacity(FLinearColor(1.0f, 0.82f, 0.35f, 1.0f)).Text(FText::FromString(Title))
		.AutoWrapText(true)
	];
	if (!Text.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 10)).ColorAndOpacity(FLinearColor(0.9f, 0.88f, 0.82f, 1.0f)).Text(FText::FromString(Text))
			.WrapTextAt(500.0f)
		];
	}
	for (int32 Index = 0; Index < Choices.Num(); ++Index)
	{
		const FHexChoice& Choice = Choices[Index];
		const FSlateBrush* Icon = HexUiBrushes::Get(Choice.Icon.LoadSynchronous(), 48.0f, FMath::Max(Choice.IconFrames, 1));
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(8.0f, 6.0f)).ButtonColorAndOpacity(FLinearColor(0.16f, 0.13f, 0.1f, 1.0f))
			.OnClicked_Lambda([this, Index]()
			{
				Close();
				OnChosen.ExecuteIfBound(Index);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(48.0f).HeightOverride(48.0f)
					[
						Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon)) : StaticCastSharedRef<SWidget>(SNew(SBox))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 11)).ColorAndOpacity(Choice.Color).Text(FText::FromString(Choice.Title))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(FLinearColor(0.75f, 0.72f, 0.66f, 1.0f)).Text(FText::FromString(Choice.Text))
						.WrapTextAt(400.0f)
					]
				]
			]
		];
	}
}

void SHexChoiceDialog::Close()
{
	bOpen = false;
	ShownKey.Reset();
	SetVisibility(EVisibility::Collapsed);
}
