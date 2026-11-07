// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexDialog.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const float DialogWidth = 460.0f;
	const float DialogButtonPadding = 8.0f;
}

void SHexDialog::Construct(const FArguments& InArgs)
{
	Buttons = InArgs._Buttons;
	Text = InArgs._InitialText;
	OnCheckText = InArgs._OnCheckText;
	OnClosed = InArgs._OnClosed;

	const FSlateBrush* Background = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	TSharedRef<SHorizontalBox> ButtonRow = SNew(SHorizontalBox);

	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
	{
		TSharedPtr<STextBlock> Label;

		ButtonRow->AddSlot()
		.FillWidth(1.0f)
		.Padding(Index == 0 ? 0.0f : DialogButtonPadding * 0.5f, 0.0f, Index + 1 == Buttons.Num() ? 0.0f : DialogButtonPadding * 0.5f, 0.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ContentPadding(DialogButtonPadding)
			.OnClicked_Lambda([this, Index]()
			{
				Press(Index);
				return FReply::Handled();
			})
			[
				SAssignNew(Label, STextBlock)
				.Text(Buttons[Index].Label)
			]
		];

		ButtonLabels.Add(Label);
	}

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			SNew(STextBlock)
			.Text(InArgs._Title)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(InArgs._Message)
			.AutoWrapText(true)
			.Visibility(InArgs._Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
		];

	if (InArgs._bHasText)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SAssignNew(TextBox, SEditableTextBox)
			.Text(FText::FromString(Text))
			.SelectAllTextWhenFocused(true)
			// Enter closes the dialog, which gives the keyboard back to the game, and the text box must not clear that focus after it
			.ClearKeyboardFocusOnCommit(false)
			// the text box would take Escape to drop its focus, which leaves the dialog without the keyboard
			.OnKeyDownHandler_Lambda([this](const FGeometry& Geometry, const FKeyEvent& KeyEvent)
			{
				return KeyEvent.GetKey() == EKeys::Escape ? OnKeyDown(Geometry, KeyEvent) : FReply::Unhandled();
			})
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				Text = NewText.ToString();
				Check();
			})
			.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
			{
				Text = NewText.ToString();
				if (CommitType == ETextCommit::OnEnter && !Buttons.IsEmpty())
				{
					Press(0);
				}
			})
		];
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SAssignNew(WarningText, STextBlock)
		.ColorAndOpacity(FLinearColor(1.0f, 0.55f, 0.3f))
		.AutoWrapText(true)
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		ButtonRow
	];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(Background)
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(Background)
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.95f))
			.Padding(24.0f)
			[
				SNew(SBox)
				.WidthOverride(DialogWidth)
				[
					Content
				]
			]
		]
	];

	Check();
}

TSharedRef<SWidget> SHexDialog::GetFocusTarget() const
{
	return TextBox.IsValid() ? StaticCastSharedRef<SWidget>(TextBox.ToSharedRef()) : ConstCastSharedRef<SWidget>(AsShared());
}

void SHexDialog::SetButtonLabel(int32 Index, const FText& Label)
{
	if (ButtonLabels.IsValidIndex(Index))
	{
		ButtonLabels[Index]->SetText(Label);
	}
}

FReply SHexDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && !bClosed)
	{
		bClosed = true;
		OnClosed.ExecuteIfBound();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SHexDialog::Press(int32 Index)
{
	if (bClosed || !Buttons.IsValidIndex(Index))
	{
		return;
	}

	const FText Error = Buttons[Index].OnClicked.IsBound() ? Buttons[Index].OnClicked.Execute(Text) : FText::GetEmpty();

	if (!Error.IsEmpty())
	{
		WarningText->SetText(Error);
		return;
	}

	bClosed = true;
	OnClosed.ExecuteIfBound();
}

void SHexDialog::Check()
{
	if (WarningText.IsValid())
	{
		WarningText->SetText(OnCheckText.IsBound() ? OnCheckText.Execute(Text) : FText::GetEmpty());
	}
}
