// Copyright Epic Games, Inc. All Rights Reserved.


#include "SHexDebugOverlay.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor PanelColor(0.0f, 0.0f, 0.0f, 0.55f);
	const FLinearColor NameColor(0.65f, 0.65f, 0.65f, 1.0f);
	const FLinearColor ValueColor(1.0f, 0.9f, 0.5f, 1.0f);
	const FLinearColor HintColor(1.0f, 1.0f, 1.0f, 0.55f);
	const FLinearColor HeadingColor(0.55f, 0.8f, 1.0f, 1.0f);
	const FLinearColor WarningColor(1.0f, 0.2f, 0.15f, 1.0f);

	/** Seconds that the settings of a mode of the view stay on screen after a change back to the usual view */
	const double ViewStatusTime = 3.0;

	TSharedRef<SBorder> MakePanel(const TSharedRef<SGridPanel>& Grid)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
			.BorderBackgroundColor(PanelColor)
			.Padding(FMargin(8.0f, 5.0f))
			.Visibility(EVisibility::Collapsed)
			[
				Grid
			];
	}
}

void SHexDebugOverlay::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);

	SAssignNew(LegendGrid, SGridPanel);
	LegendPanel = MakePanel(LegendGrid.ToSharedRef());

	ChildSlot
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	.Padding(10.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		[
			SAssignNew(Warning, STextBlock)
			.ColorAndOpacity(WarningColor)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ShadowOffset(FVector2D(1.0, 1.0))
			.Visibility(EVisibility::Collapsed)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		[
			SAssignNew(LegendHint, STextBlock)
			.ColorAndOpacity(HintColor)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.Visibility(EVisibility::Collapsed)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		[
			LegendPanel.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		[
			SAssignNew(StatusBox, SVerticalBox)
		]
	];
}

void SHexDebugOverlay::SetLegend(const TArray<FHexLegendSection>& Sections, const FString& Hint)
{
	LegendGrid->ClearChildren();
	int32 Row = 0;

	for (const FHexLegendSection& Section : Sections)
	{
		if (!Section.Title.IsEmpty())
		{
			// a gap above each heading but the first sets the sections apart
			const float Gap = Row > 0 ? 6.0f : 0.0f;

			LegendGrid->AddSlot(0, Row++)
			.ColumnSpan(2)
			.Padding(0.0f, Gap, 0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Section.Title))
				.ColorAndOpacity(HeadingColor)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			];
		}

		for (const TPair<FString, FString>& Line : Section.Lines)
		{
			AddGridLine(*LegendGrid, Row++, Line, 8.0f);
		}
	}

	LegendHint->SetText(FText::FromString(Hint));
	SetLegendVisible(IsLegendVisible());
}

void SHexDebugOverlay::SetLegendVisible(bool bVisible)
{
	LegendPanel->SetVisibility(bVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	LegendHint->SetVisibility(bVisible || LegendHint->GetText().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

bool SHexDebugOverlay::IsLegendVisible() const
{
	return LegendPanel->GetVisibility() != EVisibility::Collapsed;
}

void SHexDebugOverlay::SetWarning(const FString& Text)
{
	if (Warning->GetText().ToString() != Text)
	{
		Warning->SetText(FText::FromString(Text));
	}

	Warning->SetVisibility(Text.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

int32 SHexDebugOverlay::AddStatusPanel()
{
	FStatusPanel& Panel = StatusPanels.AddDefaulted_GetRef();
	SAssignNew(Panel.Grid, SGridPanel);
	Panel.Border = MakePanel(Panel.Grid.ToSharedRef());

	StatusBox->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Right)
	.Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		Panel.Border.ToSharedRef()
	];

	return StatusPanels.Num() - 1;
}

void SHexDebugOverlay::SetStatus(int32 PanelIndex, const TArray<TPair<FString, FString>>& Lines)
{
	if (!StatusPanels.IsValidIndex(PanelIndex))
	{
		return;
	}

	FStatusPanel& Panel = StatusPanels[PanelIndex];

	if (Lines != Panel.Lines)
	{
		Panel.Lines = Lines;
		FillGrid(*Panel.Grid, Lines);
	}

	Panel.Border->SetVisibility(Lines.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

void SHexDebugOverlay::AddViewStatus(TFunction<bool(TArray<TPair<FString, FString>>&)> GetStatus)
{
	FViewStatusSource& Source = ViewStatusSources.AddDefaulted_GetRef();
	Source.GetStatus = MoveTemp(GetStatus);
	Source.PanelIndex = AddStatusPanel();

	// settings given on the command line count as no change, so they do not flash at startup
	Source.GetStatus(Source.Lines);
}

void SHexDebugOverlay::UpdateViewStatus(double Now)
{
	for (FViewStatusSource& Source : ViewStatusSources)
	{
		TArray<TPair<FString, FString>> Lines;
		const bool bUsualView = Source.GetStatus(Lines);

		if (Lines != Source.Lines)
		{
			// a mode that has just appeared has not changed
			if (!Source.Lines.IsEmpty())
			{
				Source.ChangeTime = Now;
			}

			Source.Lines = Lines;
		}

		const bool bVisible = !bUsualView || Now - Source.ChangeTime < ViewStatusTime;
		SetStatus(Source.PanelIndex, bVisible ? Lines : TArray<TPair<FString, FString>>());
	}
}

void SHexDebugOverlay::FillGrid(SGridPanel& Grid, const TArray<TPair<FString, FString>>& Lines)
{
	Grid.ClearChildren();

	for (int32 Row = 0; Row < Lines.Num(); ++Row)
	{
		AddGridLine(Grid, Row, Lines[Row], 0.0f);
	}
}

void SHexDebugOverlay::AddGridLine(SGridPanel& Grid, int32 Row, const TPair<FString, FString>& Line, float Indent)
{
	Grid.AddSlot(0, Row)
	.Padding(Indent, 0.0f, 10.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Line.Key))
		.ColorAndOpacity(NameColor)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
	];

	Grid.AddSlot(1, Row)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Line.Value))
		.ColorAndOpacity(ValueColor)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
	];
}
