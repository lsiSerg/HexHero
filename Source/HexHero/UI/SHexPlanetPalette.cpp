// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexPlanetPalette.h"
#include "HexPlanetFile.h"
#include "HexPlanetPainter.h"
#include "HexPlanetUnderground.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexPlanetPalette"

namespace
{
	const float PaletteWidth = 250.0f;
	const float SwatchSize = 18.0f;
	const FLinearColor ChosenColor(1.0f, 0.75f, 0.1f, 1.0f);
	const FLinearColor ButtonColor(0.3f, 0.3f, 0.3f, 1.0f);
	const FLinearColor HelpColor(0.75f, 0.75f, 0.75f);
	const FLinearColor KeyColor(0.7f, 0.7f, 0.7f);

	/** The tabs of each layer, in the order of their buttons */
	const EHexPaintTab SurfaceTabs[] = {EHexPaintTab::Ground, EHexPaintTab::Relief, EHexPaintTab::Objects, EHexPaintTab::Rivers, EHexPaintTab::Entrances};
	const EHexPaintTab UndergroundTabs[] = {EHexPaintTab::Maze, EHexPaintTab::Links};
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

	// a square of a color, a label and the keys that choose it
	const auto MakeSwatch = [White, Small](const FLinearColor& Color, float Width, float Height, float Gap, const FText& Label, const FSlateFontInfo& LabelFont, const FText& Keys)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, Gap, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(Width)
				.HeightOverride(Height)
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
				.Text(Label)
				.Font(LabelFont)
			];
		if (!Keys.IsEmpty())
		{
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Keys)
				.Font(Small)
				.ColorAndOpacity(KeyColor)
			];
		}
		return Row;
	};

	const auto MakeHeading = [Font](const FText& Text, float Top = 8.0f)
	{
		return SNew(SBox)
			.Padding(FMargin(0.0f, Top, 0.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(Font)
			];
	};

	const auto MakeHelp = [Small](const TAttribute<FText>& Text)
	{
		return SNew(SBox)
			.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
			[
				SNew(STextBlock)
				.Font(Small)
				.AutoWrapText(true)
				.ColorAndOpacity(HelpColor)
				.Text(Text)
			];
	};

	const auto MakeLabel = [Small](const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(Small).Justification(ETextJustify::Center);
	};

	// the levels of a layer, highest first, with the number keys that choose them
	const auto AddLevels = [&](const TSharedRef<SVerticalBox>& Body, EHexPlanetLayer Layer, TConstArrayView<int32> LevelList, TConstArrayView<int32> Keys)
	{
		for (int32 Index = 0; Index < LevelList.Num(); ++Index)
		{
			const int32 Level = LevelList[Index];
			const FLinearColor Color = Painter.IsValid() ? Painter->GetLevelColor(Layer, Level) : FLinearColor::Gray;
			Body->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				MakeButton(
					MakeSwatch(Color, SwatchSize, SwatchSize, 8.0f, UHexPlanetPainter::GetLevelName(Layer, Level), Font, FText::AsNumber(Keys[Index])),
					[Level](UHexPlanetPainter& Each) { Each.SetLevel(Level); },
					[Level, Layer](const UHexPlanetPainter& Each) { return Each.GetTarget() == Layer && Each.IsPaintingLevels() && Each.GetLevel() == Level; })
			];
		}
	};

	// the biomes of a layer, two to a row, which only paint the cells whose level allows them
	const auto AddBiomes = [&](const TSharedRef<SVerticalBox>& Body, EHexPlanetLayer Layer, const FText& Heading)
	{
		const int32 NumBiomes = Painter.IsValid() ? Painter->GetNumBiomes(Layer) : 0;
		if (NumBiomes == 0)
		{
			return;
		}
		TSharedRef<SUniformGridPanel> BiomeGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Biome = 0; Biome < NumBiomes; ++Biome)
		{
			BiomeGrid->AddSlot(Biome % 2, Biome / 2)
			[
				MakeButton(
					MakeSwatch(Painter->GetBiomeColor(Layer, Biome), SwatchSize * 0.75f, SwatchSize * 0.75f, 6.0f, Painter->GetBiomeName(Layer, Biome), Small, FText::GetEmpty()),
					[Biome](UHexPlanetPainter& Each) { Each.SetBiome(Biome); },
					[Biome, Layer](const UHexPlanetPainter& Each) { return Each.GetTarget() == Layer && Each.IsPaintingBiomes() && Each.GetBiome() == Biome; })
			];
		}
		Body->AddSlot().AutoHeight()[MakeHeading(Heading)];
		Body->AddSlot().AutoHeight()[BiomeGrid];
	};

	// the chips of the networks A to H in their colors, with how many entrances each has; in the tab of the entrances a chip also takes the brush
	const auto MakeChips = [&](bool bArmsBrush)
	{
		TSharedRef<SUniformGridPanel> Chips = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Network = 0; Network < HexPlanetFile::MaxNetworks; ++Network)
		{
			// the letter in the color of its network, as a square beside it left no room for the count on a small screen
			const FLinearColor Color = Painter.IsValid() ? Painter->GetNetworkColor(Network) : FLinearColor::White;
			const TSharedRef<SButton> Chip = MakeButton(
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, 1.0f))
				.Text_Lambda([WeakPainter, Network]()
				{
					const int32 Count = WeakPainter.IsValid() ? WeakPainter->GetNetworkEntrances(Network) : 0;
					return FText::Format(LOCTEXT("Chip", "{0} {1}"), FText::FromString(FString::Chr(HexPlanetFile::GetNetworkLetter(Network))), FText::AsNumber(Count));
				}),
				[Network, bArmsBrush](UHexPlanetPainter& Each)
				{
					Each.SetNetwork(Network);
					if (bArmsBrush)
					{
						Each.SetEntranceTool();
					}
				},
				[Network, bArmsBrush](const UHexPlanetPainter& Each) { return Each.GetNetwork() == Network && (!bArmsBrush || Each.IsPlacingEntrances()); });
			Chip->SetToolTipText(LOCTEXT("ChipTip", "A network of entrances and the number of its entrances"));
			Chips->AddSlot(Network % 4, Network / 4)
			[
				Chip
			];
		}
		return Chips;
	};

	// Ground: the levels and biomes of the surface
	TSharedRef<SVerticalBox> Ground = SNew(SVerticalBox);
	{
		const int32 SurfaceLevels[] = {2, 1, 0, -1, -2};
		const int32 SurfaceKeys[] = {5, 4, 3, 2, 1};
		AddLevels(Ground, EHexPlanetLayer::Surface, SurfaceLevels, SurfaceKeys);
		AddBiomes(Ground, EHexPlanetLayer::Surface, LOCTEXT("Biomes", "Biomes, where the level allows"));
	}

	// Relief: each click on a cell, once per stroke, takes the next version or turn of its piece among those that keep its sides, mirrors it,
	// makes it higher or lower, or gives it back to the hash
	TSharedRef<SVerticalBox> Relief = SNew(SVerticalBox);
	{
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
				MakeLabel(ReliefButtons[Index].Label),
				[Tool](UHexPlanetPainter& Each) { Each.SetReliefTool(Tool); },
				[Tool](const UHexPlanetPainter& Each) { return Each.IsPaintingRelief() && Each.GetReliefTool() == Tool; });
			Button->SetToolTipText(ReliefButtons[Index].Tip);
			ReliefGrid->AddSlot(Index % 2, Index / 2)
			[
				Button
			];
		}
		Relief->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("Relief", "Relief of mountains and hills"), 0.0f)];
		Relief->AddSlot().AutoHeight()[ReliefGrid];
		Relief->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("ReliefHelp", "Click a cell, Ctrl+click to go back or lower. Version and Turn only offer what keeps the sides joined to the neighbors, so a cell whose neighbors fix it stays as it is; the info at the cursor shows how many it has. Mirror and the height always fit. The middle button moves the planet."))];
	}

	// Objects: the kinds of objects, two to a row, which only go on the cells whose level allows them, the density of a cover, and the eraser
	TSharedRef<SVerticalBox> Objects = SNew(SVerticalBox);
	{
		const int32 NumSets = Painter.IsValid() ? Painter->GetNumObjectSets() : 0;
		Objects->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("Objects", "Objects"), 0.0f)];
		if (NumSets > 0)
		{
			TSharedRef<SUniformGridPanel> ObjectGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
			for (int32 Set = 0; Set < NumSets; ++Set)
			{
				const TSharedRef<SButton> Button = MakeButton(
					MakeSwatch(Painter->GetObjectSetColor(Set), SwatchSize * 0.75f, SwatchSize * (Painter->IsObjectSetCover(Set) ? 0.75f : 0.4f), 6.0f, Painter->GetObjectSetName(Set),
						Small, FText::GetEmpty()),
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
			Objects->AddSlot().AutoHeight()[ObjectGrid];

			TSharedRef<SHorizontalBox> Densities = SNew(SHorizontalBox);
			for (int32 Density = 1; Density <= 3; ++Density)
			{
				const FText Labels[] = {LOCTEXT("Sparse", "Sparse"), LOCTEXT("Medium", "Medium"), LOCTEXT("Dense", "Dense")};
				Densities->AddSlot()
				.FillWidth(1.0f)
				.Padding(Density == 1 ? 0.0f : 3.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeButton(
						MakeLabel(Labels[Density - 1]),
						[Density](UHexPlanetPainter& Each) { Each.SetObjectDensity(Density); },
						[Density](const UHexPlanetPainter& Each) { return Each.GetObjectDensity() == Density; })
				];
			}
			Objects->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[Densities];

			const TSharedRef<SButton> Eraser = MakeButton(
				MakeLabel(LOCTEXT("Eraser", "Eraser")),
				[](UHexPlanetPainter& Each) { Each.SetEraser(); },
				[](const UHexPlanetPainter& Each) { return Each.IsErasing(); });
			Eraser->SetToolTipText(LOCTEXT("EraserTip", "Takes the cover and the decor off the cells"));
			Objects->AddSlot().AutoHeight()[Eraser];
		}
		else
		{
			Objects->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("NoObjects", "There are no kinds of objects (PlanetObjectsFolder of HexMapSettings)."))];
		}
	}

	// Rivers: they run along the sides of the cells from corner to corner, and bridges stand on their sides
	TSharedRef<SVerticalBox> RiverBody = SNew(SVerticalBox);
	{
		const TSharedRef<SButton> River = MakeButton(
			MakeLabel(LOCTEXT("River", "River")),
			[](UHexPlanetPainter& Each) { Each.SetRiverTool(); },
			[](const UHexPlanetPainter& Each) { return Each.IsPaintingRivers(); });
		River->SetToolTipText(LOCTEXT("RiverTip", "Drag along the sides of cells to lay a river from corner to corner; Ctrl+drag takes it away. Only between plains and hills"));
		const TSharedRef<SButton> Bridge = MakeButton(
			MakeLabel(LOCTEXT("Bridge", "Bridge")),
			[](UHexPlanetPainter& Each) { Each.SetBridgeTool(); },
			[](const UHexPlanetPainter& Each) { return Each.IsPlacingBridges(); });
		Bridge->SetToolTipText(LOCTEXT("BridgeTip", "Click the side of a cell with a river to put a bridge on it, which takes away the cost of crossing; Ctrl+click takes it away"));

		RiverBody->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("Rivers", "Rivers"), 0.0f)];
		RiverBody->AddSlot()
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
		RiverBody->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("RiverHelp", "A river runs along the sides of the cells between plains and hills; Ctrl takes a river or a bridge away."))];
	}

	// Entrances: the networks, and a click that puts an entrance of the chosen one on the surface
	TSharedRef<SVerticalBox> EntranceBody = SNew(SVerticalBox);
	EntranceBody->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("Networks", "Networks of entrances"), 0.0f)];
	EntranceBody->AddSlot().AutoHeight()[MakeChips(true)];
	EntranceBody->AddSlot().AutoHeight()[MakeHelp(FText::Format(LOCTEXT("EntranceHelp", "Click a cell of plains or hills to put an entrance of the chosen network there, or give it that network; Ctrl+click takes one away. Its exit is the same cell underground, which this does not carve: a red marker cannot be used. {0} over an entrance goes down."),
		FText::FromString(InArgs._LayerKeys)))];

	// Maze: the void and passages underground, carving, and the cave biomes
	TSharedRef<SVerticalBox> Maze = SNew(SVerticalBox);
	{
		Maze->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("Passages", "Void and passages"), 0.0f)];
		const int32 UndergroundLevels[] = {HexPlanetUnderground::PassageLevel, HexPlanetUnderground::VoidLevel};
		const int32 UndergroundKeys[] = {2, 1};
		AddLevels(Maze, EHexPlanetLayer::Underground, UndergroundLevels, UndergroundKeys);
		const TSharedRef<SButton> Carve = MakeButton(
			MakeLabel(LOCTEXT("Carve", "Carve a passage")),
			[](UHexPlanetPainter& Each) { Each.SetCarveTool(); },
			[](const UHexPlanetPainter& Each) { return Each.IsCarving(); });
		Carve->SetToolTipText(LOCTEXT("CarveTip", "Drag from one cell to another to carve a passage along the shortest way between them"));
		Maze->AddSlot().AutoHeight()[Carve];
		AddBiomes(Maze, EHexPlanetLayer::Underground, LOCTEXT("CaveBiomes", "Cave biomes, where the level allows"));
		Maze->AddSlot().AutoHeight()[MakeHelp(TAttribute<FText>::CreateLambda([WeakPainter]()
		{
			return FText::Format(LOCTEXT("MazeHelp", "Void and Passage paint with the brush. Carve makes a passage {0} cells wide along the shortest way from the cell pressed to the cell released."),
				FText::AsNumber(WeakPainter.IsValid() ? WeakPainter->GetPassageWidth() : 2));
		}))];
	}

	// Links: the passages in the colors of their networks, what is wrong in red, and generating a network again
	TSharedRef<SVerticalBox> Links = SNew(SVerticalBox);
	{
		Links->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("LinkNetworks", "Networks"), 0.0f)];
		Links->AddSlot().AutoHeight()[MakeChips(false)];
		Links->AddSlot().AutoHeight()[MakeHelp(TAttribute<FText>::CreateLambda([WeakPainter]()
		{
			return WeakPainter.IsValid() ? WeakPainter->GetLinksText() : FText::GetEmpty();
		}))];
		const TSharedRef<SButton> One = MakeButton(
			SNew(STextBlock)
			.Font(Small)
			.Justification(ETextJustify::Center)
			.Text_Lambda([WeakPainter]()
			{
				const int32 Network = WeakPainter.IsValid() ? WeakPainter->GetNetwork() : 0;
				return FText::Format(LOCTEXT("RegenerateOne", "Generate {0} again"), FText::FromString(FString::Chr(HexPlanetFile::GetNetworkLetter(Network))));
			}),
			[](UHexPlanetPainter& Each) { Each.Regenerate(Each.GetNetwork()); },
			nullptr,
			[](const UHexPlanetPainter& Each) { return !Each.IsPainting() && Each.GetNetworkEntrances(Each.GetNetwork()) > 0; });
		One->SetToolTipText(LOCTEXT("RegenerateOneTip", "Carves the passages of the chosen network again, keeping the others; one step of Undo"));
		const TSharedRef<SButton> All = MakeButton(
			MakeLabel(LOCTEXT("RegenerateAll", "Generate all again")),
			[](UHexPlanetPainter& Each) { Each.Regenerate(INDEX_NONE); },
			nullptr,
			[](const UHexPlanetPainter& Each) { return !Each.IsPainting(); });
		All->SetToolTipText(LOCTEXT("RegenerateAllTip", "Carves the whole maze again for the entrances, as a planet without one; one step of Undo"));
		Links->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 3.0f)[One];
		Links->AddSlot().AutoHeight()[All];
		Links->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("LinksHelp", "Passages take the color of their network, and grey where no exit reaches. Red: an exit in the void, the smaller pieces of a split network, networks joined, a passage one cell wide."))];
	}

	// the tabs of each layer, which the switcher below shows by the layer painted
	const auto MakeTabRow = [&](TConstArrayView<EHexPaintTab> Tabs)
	{
		// three to a row, the buttons of a shorter row sharing its width, so a longer name has room
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		TSharedPtr<SHorizontalBox> Row;
		for (int32 Index = 0; Index < Tabs.Num(); ++Index)
		{
			const EHexPaintTab Tab = Tabs[Index];
			if (Index % 3 == 0)
			{
				Row = SNew(SHorizontalBox);
				Rows->AddSlot().AutoHeight().Padding(0.0f, Index == 0 ? 0.0f : 3.0f, 0.0f, 0.0f)[Row.ToSharedRef()];
			}
			Row->AddSlot()
			.FillWidth(1.0f)
			.Padding(Index % 3 == 0 ? 0.0f : 3.0f, 0.0f, 0.0f, 0.0f)
			[
				MakeButton(
					MakeLabel(UHexPlanetPainter::GetTabName(Tab)),
					[Tab](UHexPlanetPainter& Each) { Each.SetTab(Tab); },
					[Tab](const UHexPlanetPainter& Each) { return Each.GetTab() == Tab; })
			];
		}
		return Rows;
	};

	// the layers, which follow the one shown, and a click travels to the other
	TSharedRef<SHorizontalBox> LayerRow = SNew(SHorizontalBox);
	for (const EHexPlanetLayer Layer : {EHexPlanetLayer::Surface, EHexPlanetLayer::Underground})
	{
		const TFunction<void(EHexPlanetLayer)> OnLayerClicked = InArgs._OnLayerClicked;
		const TSharedRef<SButton> Button = MakeButton(
			SNew(STextBlock).Text(Layer == EHexPlanetLayer::Surface ? LOCTEXT("Surface", "Surface") : LOCTEXT("Underground", "Underground")).Font(Font).Justification(ETextJustify::Center),
			[Layer, OnLayerClicked](UHexPlanetPainter& Each)
			{
				if (Each.GetTarget() != Layer && OnLayerClicked)
				{
					OnLayerClicked(Layer);
				}
			},
			[Layer](const UHexPlanetPainter& Each) { return Each.GetTarget() == Layer; },
			[Layer](const UHexPlanetPainter& Each) { return Each.HasLayer(Layer); });
		Button->SetToolTipText(FText::Format(LOCTEXT("LayerTip", "Goes to this layer at the same place, as {0} does"), FText::FromString(InArgs._LayerKeys)));
		LayerRow->AddSlot()
		.FillWidth(1.0f)
		.Padding(Layer == EHexPlanetLayer::Surface ? 0.0f : 3.0f, 0.0f, 0.0f, 0.0f)
		[
			Button
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

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("Title", "Paint ({0})"), FText::FromString(InArgs._PaintModeKeys)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			LayerRow
		]

		// no tool: the left button drags the planet, while the palette stays to pick a tool again
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			MakeButton(
				SNew(STextBlock).Text(LOCTEXT("NoTool", "Move the planet (no tool)")).Font(Font).Justification(ETextJustify::Center),
				[](UHexPlanetPainter& Each) { Each.ClearTool(); },
				[](const UHexPlanetPainter& Each) { return !Each.HasTool(); })
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 3.0f, 0.0f, 3.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("BrushSize", "Brush ({0})"), FText::FromString(InArgs._BrushSizeKeys)))
				.Font(Font)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				Sizes
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 6.0f)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([WeakPainter]() { return WeakPainter.IsValid() ? int32(WeakPainter->GetTarget()) : 0; })
			+ SWidgetSwitcher::Slot()
			[
				MakeTabRow(SurfaceTabs)
			]
			+ SWidgetSwitcher::Slot()
			[
				MakeTabRow(UndergroundTabs)
			]
		]

		// the body of the tab chosen, by EHexPaintTab
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([WeakPainter]() { return WeakPainter.IsValid() ? int32(WeakPainter->GetTab()) : 0; })
			+ SWidgetSwitcher::Slot()[Ground]
			+ SWidgetSwitcher::Slot()[Relief]
			+ SWidgetSwitcher::Slot()[Objects]
			+ SWidgetSwitcher::Slot()[RiverBody]
			+ SWidgetSwitcher::Slot()[EntranceBody]
			+ SWidgetSwitcher::Slot()[Maze]
			+ SWidgetSwitcher::Slot()[Links]
		]

		// Undo and Redo of the layer painted
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeButton(
					MakeLabel(FText::Format(LOCTEXT("Undo", "Undo ({0})"), FText::FromString(InArgs._UndoKeys))),
					[](UHexPlanetPainter& Each) { Each.Undo(); },
					nullptr,
					[](const UHexPlanetPainter& Each) { return Each.CanUndo(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(3.0f, 0.0f, 0.0f, 0.0f)
			[
				MakeButton(
					MakeLabel(FText::Format(LOCTEXT("Redo", "Redo ({0})"), FText::FromString(InArgs._RedoKeys))),
					[](UHexPlanetPainter& Each) { Each.Redo(); },
					nullptr,
					[](const UHexPlanetPainter& Each) { return Each.CanRedo(); })
			]
		]

		// what the mouse does, and whether the surface is still being rebuilt
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeHelp(TAttribute<FText>::CreateLambda([WeakPainter, SaveKeys = InArgs._SaveKeys]()
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
			}))
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
