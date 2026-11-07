// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SButton;
class SEditableTextBox;
class STextBlock;

/** Checks the text of a dialog as it is typed and returns a warning to show under it, or an empty text */
DECLARE_DELEGATE_RetVal_OneParam(FText, FOnHexDialogCheck, const FString& /*Text*/);

/** Acts on a button of a dialog with its text, and returns an error to show, or an empty text when the dialog may close */
DECLARE_DELEGATE_RetVal_OneParam(FText, FOnHexDialogButton, const FString& /*Text*/);

/**
 *  Full screen dialog over the game: a title, a message, an optional line of text and a row of buttons. A button closes it once its action
 *  returns no error, and Escape closes it as Cancel would. The owner adds it to the viewport and removes it in OnClosed.
 */
class SHexDialog : public SCompoundWidget
{
public:

	/** One button of the dialog */
	struct FButton
	{
		FText Label;
		FOnHexDialogButton OnClicked;
	};

	SLATE_BEGIN_ARGS(SHexDialog) : _bHasText(false) {}
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(FText, Message)
		/** Shows a line of text, which the buttons get */
		SLATE_ARGUMENT(bool, bHasText)
		SLATE_ARGUMENT(FString, InitialText)
		/** Checks the text as it is typed */
		SLATE_EVENT(FOnHexDialogCheck, OnCheckText)
		/** Buttons from left to right, of which the first also answers Enter in the text */
		SLATE_ARGUMENT(TArray<FButton>, Buttons)
		/** Called when the dialog closes, by a button or by Escape */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	/** Creates the content of the dialog */
	void Construct(const FArguments& InArgs);

	/** Returns the widget to give the keyboard focus to */
	TSharedRef<SWidget> GetFocusTarget() const;

	/** Changes the label of a button, such as Save to Overwrite while the name is taken */
	void SetButtonLabel(int32 Index, const FText& Label);

	/** Keeps the keyboard focus, because the game gets no input while the dialog is open */
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Closes the dialog with Escape */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:

	/** Runs the action of a button and closes the dialog unless it returns an error */
	void Press(int32 Index);

	/** Shows the warning of OnCheckText for the text */
	void Check();

	TArray<FButton> Buttons;
	TArray<TSharedPtr<STextBlock>> ButtonLabels;
	TSharedPtr<SEditableTextBox> TextBox;
	TSharedPtr<STextBlock> WarningText;
	FString Text;
	FOnHexDialogCheck OnCheckText;
	FSimpleDelegate OnClosed;
	bool bClosed = false;
};
