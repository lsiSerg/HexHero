// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexNewPlanetPanel.h"
#include "HexSphereGrid.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexNewPlanetPanel"

namespace
{
	const float LabelWidth = 150.0f;
	const float RowPadding = 4.0f;
	const float PanelButtonPadding = 8.0f;

	/** Size chosen when the panel opens */
	const int32 DefaultSize = 1;

	/** Largest seed, so it stays a plain number in planet.json */
	const int32 MaxSeed = 999999;
}

TConstArrayView<SHexNewPlanetPanel::FSize> SHexNewPlanetPanel::GetSizes()
{
	static const FSize Sizes[] = {
		{TEXT("Small"), 20},
		{TEXT("Medium"), 32},
		{TEXT("Large"), 48},
		{TEXT("Huge"), 64},
		{TEXT("Giant"), 80},
	};
	return Sizes;
}

void SHexNewPlanetPanel::Construct(const FArguments& InArgs)
{
	OnCreated = InArgs._OnCreated;
	OnBack = InArgs._OnBack;

	Manifest.Name = TEXT("New planet");
	Manifest.Frequency = GetSizes()[DefaultSize].Frequency;
	Manifest.Seed = FMath::RandRange(1, MaxSeed);

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

	// the sizes and the starts are groups of check boxes that act as radio buttons
	TSharedRef<SVerticalBox> SizeBox = SNew(SVerticalBox);
	for (const FSize& Size : GetSizes())
	{
		const int32 Frequency = Size.Frequency;
		const int32 Side = FMath::RoundToInt32(FMath::Sqrt(double(FHexSphereGrid::CountCells(Frequency))));

		SizeBox->AddSlot()
		.AutoHeight()
		[
			SNew(SCheckBox)
			.Style(&FCoreStyle::Get(), TEXT("RadioButton"))
			.IsChecked_Lambda([this, Frequency]() { return Manifest.Frequency == Frequency ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Frequency](ECheckBoxState) { Manifest.Frequency = Frequency; })
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("Size", "{0}: {1} cells, like {2} x {2}"), FText::FromString(Size.Label),
					FText::AsNumber(FHexSphereGrid::CountCells(Frequency)), FText::AsNumber(Side)))
			]
		];
	}
	AddRow(Rows, LOCTEXT("SizeLabel", "Size"), SizeBox, LOCTEXT("SizeTip", "Cells have the same size on every planet, so a larger planet has more of them"));

	struct FStart
	{
		EHexPlanetStart Start;
		FText Label;
	};
	const FStart Starts[] = {
		{EHexPlanetStart::Random, LOCTEXT("Random", "Random continents")},
		{EHexPlanetStart::Water, LOCTEXT("Water", "All water")},
		{EHexPlanetStart::Land, LOCTEXT("Land", "All land")},
	};

	TSharedRef<SVerticalBox> StartBox = SNew(SVerticalBox);
	for (const FStart& Each : Starts)
	{
		const EHexPlanetStart Start = Each.Start;

		StartBox->AddSlot()
		.AutoHeight()
		[
			SNew(SCheckBox)
			.Style(&FCoreStyle::Get(), TEXT("RadioButton"))
			.IsChecked_Lambda([this, Start]() { return Manifest.Generation.Start == Start ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Start](ECheckBoxState) { Manifest.Generation.Start = Start; })
			[
				SNew(STextBlock)
				.Text(Each.Label)
			]
		];
	}
	AddRow(Rows, LOCTEXT("StartLabel", "Start"), StartBox);

	// the values of a random planet, shown only while it is chosen
	TSharedRef<SVerticalBox> RandomRows = SNew(SVerticalBox);

	AddRow(RandomRows, LOCTEXT("Seed", "Seed"),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSpinBox<int32>)
			.MinValue(0)
			.MaxValue(MaxSeed)
			.Value_Lambda([this]() { return Manifest.Seed; })
			.OnValueChanged_Lambda([this](int32 Value) { Manifest.Seed = Value; })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(RowPadding, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("Reroll", "Random"))
			.OnClicked_Lambda([this]()
			{
				Manifest.Seed = FMath::RandRange(1, MaxSeed);
				return FReply::Handled();
			})
		],
		LOCTEXT("SeedTip", "The same seed and values give the same planet"));

	AddShareRow(RandomRows, LOCTEXT("LandShare", "Land, %"), &FHexPlanetGeneration::LandShare, 1.0f, LOCTEXT("LandTip", "Share of the planet that is land"));
	AddShareRow(RandomRows, LOCTEXT("ShallowsShare", "Shallows, %"), &FHexPlanetGeneration::ShallowsShare, 1.0f, LOCTEXT("ShallowsTip", "Share of the sea that is shallows, along the coast"));
	AddShareRow(RandomRows, LOCTEXT("HillShare", "Hills, %"), &FHexPlanetGeneration::HillShare, 1.0f, LOCTEXT("HillTip", "Share of the land that is hills, inland"));
	AddShareRow(RandomRows, LOCTEXT("MountainShare", "Mountains, %"), &FHexPlanetGeneration::MountainShare, 0.5f, LOCTEXT("MountainTip", "Share of the land that is mountains, in ranges"));

	AddRow(RandomRows, LOCTEXT("ContinentSize", "Continent size"),
		SNew(SSpinBox<float>)
		.MinValue(0.25f)
		.MaxValue(4.0f)
		.Delta(0.05f)
		.Value_Lambda([this]() { return float(Manifest.Generation.ContinentSize); })
		.OnValueChanged_Lambda([this](float Value) { Manifest.Generation.ContinentSize = FMath::RoundToDouble(Value * 100.0) / 100.0; }),
		LOCTEXT("ContinentTip", "Larger makes fewer and larger continents"));

	AddRow(RandomRows, LOCTEXT("CoastDetail", "Coast detail"),
		SNew(SSpinBox<int32>)
		.MinValue(1)
		.MaxValue(8)
		.Value_Lambda([this]() { return Manifest.Generation.CoastDetail; })
		.OnValueChanged_Lambda([this](int32 Value) { Manifest.Generation.CoastDetail = Value; }),
		LOCTEXT("CoastTip", "Octaves of the noise: more makes ragged coasts and islands"));

	AddRow(RandomRows, LOCTEXT("Temperature", "Temperature"),
		SNew(SSpinBox<float>)
		.MinValue(-0.5f)
		.MaxValue(0.5f)
		.Delta(0.05f)
		.Value_Lambda([this]() { return float(Manifest.Generation.Temperature); })
		.OnValueChanged_Lambda([this](float Value) { Manifest.Generation.Temperature = FMath::RoundToDouble(Value * 100.0) / 100.0; }),
		LOCTEXT("TemperatureTip", "Warmer shrinks the ice caps, snow and tundra, colder grows them"));

	AddRow(RandomRows, LOCTEXT("Moisture", "Moisture"),
		SNew(SSpinBox<float>)
		.MinValue(-0.5f)
		.MaxValue(0.5f)
		.Delta(0.05f)
		.Value_Lambda([this]() { return float(Manifest.Generation.Moisture); })
		.OnValueChanged_Lambda([this](float Value) { Manifest.Generation.Moisture = FMath::RoundToDouble(Value * 100.0) / 100.0; }),
		LOCTEXT("MoistureTip", "Drier gives more desert and steppe, wetter more forest and swamp"));

	Rows->AddSlot()
	.AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return Manifest.Generation.Start == EHexPlanetStart::Random ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			RandomRows
		]
	];

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 0.0f, 0.0f, 16.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "New planet"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Rows
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 16.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, RowPadding, 0.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ContentPadding(PanelButtonPadding)
				.Text(LOCTEXT("Create", "Create"))
				.OnClicked(this, &SHexNewPlanetPanel::OnCreateClicked)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(RowPadding, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ContentPadding(PanelButtonPadding)
				.Text(LOCTEXT("Back", "Back"))
				.OnClicked_Lambda([this]()
				{
					OnBack.ExecuteIfBound();
					return FReply::Handled();
				})
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SAssignNew(ErrorText, STextBlock)
			.ColorAndOpacity(FLinearColor(1.0f, 0.3f, 0.3f))
			.AutoWrapText(true)
		]
	];
}

void SHexNewPlanetPanel::AddRow(const TSharedRef<SVerticalBox>& Rows, const FText& Label, const TSharedRef<SWidget>& Control, const FText& ToolTip)
{
	Rows->AddSlot()
	.AutoHeight()
	.Padding(0.0f, RowPadding)
	[
		SNew(SHorizontalBox)
		.ToolTipText(ToolTip)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(LabelWidth)
			[
				SNew(STextBlock)
				.Text(Label)
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			Control
		]
	];
}

void SHexNewPlanetPanel::AddShareRow(const TSharedRef<SVerticalBox>& Rows, const FText& Label, double FHexPlanetGeneration::* Share, float Max, const FText& ToolTip)
{
	// shown as a percentage, kept as a share
	AddRow(Rows, Label,
		SNew(SSpinBox<float>)
		.MinValue(0.0f)
		.MaxValue(Max * 100.0f)
		.Delta(1.0f)
		.MinFractionalDigits(0)
		.MaxFractionalDigits(0)
		.Value_Lambda([this, Share]() { return float(FMath::RoundToDouble(Manifest.Generation.*Share * 100.0)); })
		.OnValueChanged_Lambda([this, Share](float Value) { Manifest.Generation.*Share = Value / 100.0; }),
		ToolTip);
}

FReply SHexNewPlanetPanel::OnCreateClicked()
{
	// nothing is written until the planet is saved with the view key, which asks for its name
	OnCreated.ExecuteIfBound(Manifest);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
