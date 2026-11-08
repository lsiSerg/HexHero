// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexPlanetPalette.h"
#include "HexPlanetPainter.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexPlanetPalette"

namespace
{
	const float PaletteWidth = 250.0f;
	const float SwatchSize = 18.0f;
	const FLinearColor ChosenColor(1.0f, 0.75f, 0.1f, 1.0f);
	const FLinearColor ButtonColor(0.3f, 0.3f, 0.3f, 1.0f);
}

void SHexPlanetPalette::Construct(const FArguments& InArgs)
{
	Painter = InArgs._Painter;

	// the widget covers the viewport, of which only the panel takes the mouse
	SetVisibility(EVisibility::SelfHitTestInvisible);

	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	TWeakObjectPtr<UHexPlanetPainter> WeakPainter = Painter;

	// a button that never takes the keyboard, tinted while its choice is the one made
	const auto MakeButton = [WeakPainter](const TSharedRef<SWidget>& Content, TFunction<void(UHexPlanetPainter&)> OnClicked,
		TFunction<bool(const UHexPlanetPainter&)> IsChosen, TFunction<bool(const UHexPlanetPainter&)> IsEnabled = nullptr)
	{
		return SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(FMargin(6.0f, 3.0f))
			.ButtonColorAndOpacity_Lambda([WeakPainter, IsChosen]()
			{
				return WeakPainter.IsValid() && IsChosen && IsChosen(*WeakPainter) ? ChosenColor : ButtonColor;
			})
			.IsEnabled_Lambda([WeakPainter, IsEnabled]()
			{
				return WeakPainter.IsValid() && (!IsEnabled || IsEnabled(*WeakPainter));
			})
			.OnClicked_Lambda([WeakPainter, OnClicked]()
			{
				if (WeakPainter.IsValid())
				{
					OnClicked(*WeakPainter);
				}
				return FReply::Handled();
			})
			[
				Content
			];
	};

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("Title", "Paint ({0})"), FText::FromString(InArgs._PaintModeKeys)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		]

		// no tool: the left button drags the planet, while the palette stays to pick a tool again
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			MakeButton(
				SNew(STextBlock).Text(LOCTEXT("NoTool", "Move the planet (no tool)")).Font(Font).Justification(ETextJustify::Center),
				[](UHexPlanetPainter& Each) { Each.ClearTool(); },
				[](const UHexPlanetPainter& Each) { return !Each.HasTool(); })
		];

	// the levels, highest first, with the number keys that choose them
	for (int32 Level = UHexPlanetPainter::MaxLevel; Level >= UHexPlanetPainter::MinLevel; --Level)
	{
		const FLinearColor Color = Painter.IsValid() ? Painter->GetLevelColor(Level) : FLinearColor::Gray;

		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			MakeButton(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(SwatchSize)
					.HeightOverride(SwatchSize)
					[
						SNew(SImage)
						.Image(White)
						.ColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, 1.0f))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(UHexPlanetPainter::GetLevelName(Level))
					.Font(Font)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(Level - UHexPlanetPainter::MinLevel + 1))
					.Font(Small)
					.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
				],
				[Level](UHexPlanetPainter& Each) { Each.SetLevel(Level); },
				[Level](const UHexPlanetPainter& Each) { return Each.IsPaintingLevels() && Each.GetLevel() == Level; })
		];
	}

	// the biomes, two to a row, which only paint the cells whose level allows them
	TSharedRef<SUniformGridPanel> BiomeGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
	const int32 NumBiomes = Painter.IsValid() ? Painter->GetNumBiomes() : 0;

	for (int32 Biome = 0; Biome < NumBiomes; ++Biome)
	{
		const FLinearColor Color = Painter->GetBiomeColor(Biome);

		BiomeGrid->AddSlot(Biome % 2, Biome / 2)
		[
			MakeButton(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(SwatchSize * 0.75f)
					.HeightOverride(SwatchSize * 0.75f)
					[
						SNew(SImage)
						.Image(White)
						.ColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, 1.0f))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Painter->GetBiomeName(Biome))
					.Font(Small)
				],
				[Biome](UHexPlanetPainter& Each) { Each.SetBiome(Biome); },
				[Biome](const UHexPlanetPainter& Each) { return Each.IsPaintingBiomes() && Each.GetBiome() == Biome; })
		];
	}

	if (NumBiomes > 0)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 3.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Biomes", "Biomes, where the level allows"))
			.Font(Font)
		];

		Content->AddSlot()
		.AutoHeight()
		[
			BiomeGrid
		];
	}

	// the relief of mountains and hills: each click on a cell, once per stroke, takes the next version or turn of its piece among those that keep its
	// sides, mirrors it, makes it higher or lower, or gives it back to the hash
	struct FReliefButton
	{
		EHexReliefTool Tool;
		FText Label;
		FText Tip;
	};
	const FReliefButton ReliefButtons[] = {
		{EHexReliefTool::Version, LOCTEXT("ReliefVersion", "Version"), LOCTEXT("ReliefVersionTip", "Click a cell for its next version, Ctrl+click for the one before")},
		{EHexReliefTool::Turn, LOCTEXT("ReliefTurn", "Turn"), LOCTEXT("ReliefTurnTip", "Click a cell to turn it on, Ctrl+click to turn it back, where its neighbors allow")},
		{EHexReliefTool::Mirror, LOCTEXT("ReliefMirror", "Mirror"), LOCTEXT("ReliefMirrorTip", "Click a cell to mirror it or not")},
		{EHexReliefTool::Higher, LOCTEXT("ReliefHeight", "Height"), LOCTEXT("ReliefHeightTip", "Click a cell to make it higher, Ctrl+click to make it lower")},
		{EHexReliefTool::Reset, LOCTEXT("ReliefReset", "Auto"), LOCTEXT("ReliefResetTip", "Click a cell to give its relief back to the planet")},
	};
	TSharedRef<SUniformGridPanel> ReliefGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ReliefButtons); ++Index)
	{
		const EHexReliefTool Tool = ReliefButtons[Index].Tool;
		const TSharedRef<SButton> Button = MakeButton(
			SNew(STextBlock).Text(ReliefButtons[Index].Label).Font(Small).Justification(ETextJustify::Center),
			[Tool](UHexPlanetPainter& Each) { Each.SetReliefTool(Tool); },
			[Tool](const UHexPlanetPainter& Each) { return Each.IsPaintingRelief() && Each.GetReliefTool() == Tool; });
		Button->SetToolTipText(ReliefButtons[Index].Tip);
		ReliefGrid->AddSlot(Index % 2, Index / 2)
		[
			Button
		];
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 3.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Relief", "Relief of mountains and hills"))
		.Font(Font)
	];

	Content->AddSlot()
	.AutoHeight()
	[
		ReliefGrid
	];

	Content->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.Font(Small)
		.AutoWrapText(true)
		.ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f))
		.Text(LOCTEXT("ReliefHelp", "Click a cell, Ctrl+click to go back or lower. Version and Turn only offer what keeps the sides joined to the neighbors, so a cell whose neighbors fix it stays as it is; the info at the cursor shows how many it has. Mirror and the height always fit. The middle button moves the planet."))
	];

	// the kinds of objects, two to a row, which only go on the cells whose level allows them, the density of a cover, and the eraser
	const int32 NumSets = Painter.IsValid() ? Painter->GetNumObjectSets() : 0;
	if (NumSets > 0)
	{
		TSharedRef<SUniformGridPanel> ObjectGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Set = 0; Set < NumSets; ++Set)
		{
			const FLinearColor Color = Painter->GetObjectSetColor(Set);
			const TSharedRef<SButton> Button = MakeButton(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(SwatchSize * 0.75f)
					.HeightOverride(SwatchSize * (Painter->IsObjectSetCover(Set) ? 0.75f : 0.4f))
					[
						SNew(SImage)
						.Image(White)
						.ColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, 1.0f))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Painter->GetObjectSetName(Set))
					.Font(Small)
				],
				[Set](UHexPlanetPainter& Each) { Each.SetObjectSet(Set); },
				[Set](const UHexPlanetPainter& Each) { return Each.GetTool() == EHexPaintTool::Object && Each.GetObjectSet() == Set; });
			Button->SetToolTipText(Painter->IsObjectSetCover(Set)
				? LOCTEXT("CoverTip", "Fills the cells at the density below; a cell holds one cover")
				: LOCTEXT("DecorTip", "A piece of decor near the middle of the cell; a cell holds one, beside its cover"));
			ObjectGrid->AddSlot(Set % 2, Set / 2)
			[
				Button
			];
		}

		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 3.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Objects", "Objects"))
			.Font(Font)
		];

		Content->AddSlot()
		.AutoHeight()
		[
			ObjectGrid
		];

		TSharedRef<SHorizontalBox> Densities = SNew(SHorizontalBox);
		for (int32 Density = 1; Density <= 3; ++Density)
		{
			const FText Labels[] = {LOCTEXT("Sparse", "Sparse"), LOCTEXT("Medium", "Medium"), LOCTEXT("Dense", "Dense")};
			Densities->AddSlot()
			.FillWidth(1.0f)
			.Padding(Density == 1 ? 0.0f : 3.0f, 0.0f, 0.0f, 0.0f)
			[
				MakeButton(
					SNew(STextBlock).Text(Labels[Density - 1]).Font(Small).Justification(ETextJustify::Center),
					[Density](UHexPlanetPainter& Each) { Each.SetObjectDensity(Density); },
					[Density](const UHexPlanetPainter& Each) { return Each.GetObjectDensity() == Density; })
			];
		}

		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			Densities
		];

		const TSharedRef<SButton> Eraser = MakeButton(
			SNew(STextBlock).Text(LOCTEXT("Eraser", "Eraser")).Font(Small).Justification(ETextJustify::Center),
			[](UHexPlanetPainter& Each) { Each.SetEraser(); },
			[](const UHexPlanetPainter& Each) { return Each.IsErasing(); });
		Eraser->SetToolTipText(LOCTEXT("EraserTip", "Takes the cover and the decor off the cells"));
		Content->AddSlot()
		.AutoHeight()
		[
			Eraser
		];
	}

	// rivers run along the sides of the cells from corner to corner, and bridges stand on their sides
	{
		const TSharedRef<SButton> River = MakeButton(
			SNew(STextBlock).Text(LOCTEXT("River", "River")).Font(Small).Justification(ETextJustify::Center),
			[](UHexPlanetPainter& Each) { Each.SetRiverTool(); },
			[](const UHexPlanetPainter& Each) { return Each.IsPaintingRivers(); });
		River->SetToolTipText(LOCTEXT("RiverTip", "Drag along the sides of cells to lay a river from corner to corner; Ctrl+drag takes it away. Only between plains and hills"));
		const TSharedRef<SButton> Bridge = MakeButton(
			SNew(STextBlock).Text(LOCTEXT("Bridge", "Bridge")).Font(Small).Justification(ETextJustify::Center),
			[](UHexPlanetPainter& Each) { Each.SetBridgeTool(); },
			[](const UHexPlanetPainter& Each) { return Each.IsPlacingBridges(); });
		Bridge->SetToolTipText(LOCTEXT("BridgeTip", "Click the side of a cell with a river to put a bridge on it, which takes away the cost of crossing; Ctrl+click takes it away"));

		Content->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 3.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Rivers", "Rivers"))
			.Font(Font)
		];

		Content->AddSlot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				River
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(3.0f, 0.0f, 0.0f, 0.0f)
			[
				Bridge
			]
		];
	}

	// the size of the brush
	TSharedRef<SHorizontalBox> Sizes = SNew(SHorizontalBox);
	for (int32 Size = 1; Size <= UHexPlanetPainter::MaxBrushSize; ++Size)
	{
		Sizes->AddSlot()
		.FillWidth(1.0f)
		.Padding(Size == 1 ? 0.0f : 3.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeButton(
				SNew(STextBlock)
				.Text(FText::AsNumber(Size))
				.Font(Font)
				.Justification(ETextJustify::Center),
				[Size](UHexPlanetPainter& Each) { Each.SetBrushSize(Size); },
				[Size](const UHexPlanetPainter& Each) { return Each.GetBrushSize() == Size; })
		];
	}

	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 3.0f)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("BrushSize", "Brush size ({0})"), FText::FromString(InArgs._BrushSizeKeys)))
		.Font(Font)
	];

	Content->AddSlot()
	.AutoHeight()
	[
		Sizes
	];

	// Undo and Redo
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			MakeButton(
				SNew(STextBlock).Text(FText::Format(LOCTEXT("Undo", "Undo ({0})"), FText::FromString(InArgs._UndoKeys))).Font(Small).Justification(ETextJustify::Center),
				[](UHexPlanetPainter& Each) { Each.Undo(); },
				nullptr,
				[](const UHexPlanetPainter& Each) { return Each.CanUndo(); })
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(3.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeButton(
				SNew(STextBlock).Text(FText::Format(LOCTEXT("Redo", "Redo ({0})"), FText::FromString(InArgs._RedoKeys))).Font(Small).Justification(ETextJustify::Center),
				[](UHexPlanetPainter& Each) { Each.Redo(); },
				nullptr,
				[](const UHexPlanetPainter& Each) { return Each.CanRedo(); })
		]
	];

	// what the mouse does, and whether the surface is still being rebuilt
	Content->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 8.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Font(Small)
		.AutoWrapText(true)
		.ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f))
		.Text_Lambda([WeakPainter, SaveKeys = InArgs._SaveKeys]()
		{
			if (WeakPainter.IsValid() && WeakPainter->IsRebuilding())
			{
				return LOCTEXT("Rebuilding", "Rebuilding the surface...");
			}
			if (WeakPainter.IsValid() && !WeakPainter->HasTool())
			{
				return FText::Format(LOCTEXT("HintNoTool", "The left button moves the planet; pick a tool to paint. Save with {0}."), FText::FromString(SaveKeys));
			}
			return FText::Format(LOCTEXT("Hint", "The left button paints. Save with {0}."), FText::FromString(SaveKeys));
		})
	];

	// a palette taller than the screen scrolls
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	.Padding(16.0f, 16.0f, 0.0f, 16.0f)
	[
		SNew(SBorder)
		.BorderImage(White)
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
		.Padding(10.0f)
		[
			SNew(SBox)
			.WidthOverride(PaletteWidth)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Content
				]
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
