// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexPlanetPalette.h"
#include "SHexModeBar.h"
#include "HexHeroConfig.h"
#include "HexCastleConfig.h"
#include "HexSiteConfig.h"
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
	const EHexPaintTab SurfaceTabs[] = {EHexPaintTab::Ground, EHexPaintTab::Relief, EHexPaintTab::Objects, EHexPaintTab::Rivers, EHexPaintTab::Entrances, EHexPaintTab::Heroes, EHexPaintTab::Sites, EHexPaintTab::Castles};
	const EHexPaintTab UndergroundTabs[] = {EHexPaintTab::Maze, EHexPaintTab::Links, EHexPaintTab::CaveObjects, EHexPaintTab::CaveHeroes, EHexPaintTab::CaveSites};
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
			const TSharedRef<SWidget> Button = MakeButton(
				MakeSwatch(Painter->GetBiomeColor(Layer, Biome), SwatchSize * 0.75f, SwatchSize * 0.75f, 6.0f, Painter->GetBiomeName(Layer, Biome), Small, FText::GetEmpty()),
				[Biome](UHexPlanetPainter& Each) { Each.SetBiome(Biome); },
				[Biome, Layer](const UHexPlanetPainter& Each) { return Each.GetTarget() == Layer && Each.IsPaintingBiomes() && Each.GetBiome() == Biome; });
			Button->SetToolTipText(Painter->GetBiomeTip(Layer, Biome));
			BiomeGrid->AddSlot(Biome % 2, Biome / 2)[Button];
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

	// Objects: the kinds of objects of a layer, two to a row, which only go on the cells whose level allows them, the density of a cover, and the
	// eraser; the surface and the underground each have their own kinds (UHexPlanetObjectSet::PlanetLayers)
	const auto MakeObjects = [&](EHexPlanetLayer Layer, TSharedRef<SVerticalBox> Objects)
	{
		const int32 NumSets = Painter.IsValid() ? Painter->GetNumObjectSets(Layer) : 0;
		Objects->AddSlot().AutoHeight()[MakeHeading(Layer == EHexPlanetLayer::Underground ? LOCTEXT("CaveObjects", "Cave objects") : LOCTEXT("Objects", "Objects"), 0.0f)];
		if (NumSets > 0)
		{
			// the kinds of the layer, and below them those of the other layer, which a hand alone places here, so a click on one places by hand
			TSharedRef<SUniformGridPanel> ObjectGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
			TSharedRef<SUniformGridPanel> ForeignGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
			int32 NumOwn = 0;
			int32 NumForeign = 0;
			for (int32 Set = 0; Set < NumSets; ++Set)
			{
				const bool bCover = Painter->IsObjectSetCover(Layer, Set);
				const bool bForeign = Painter->IsObjectSetForeign(Layer, Set);
				const TSharedRef<SButton> Button = MakeButton(
					MakeSwatch(Painter->GetObjectSetColor(Layer, Set), SwatchSize * 0.75f, SwatchSize * (bCover ? 0.75f : 0.4f), 6.0f, Painter->GetObjectSetName(Layer, Set),
						Small, FText::GetEmpty()),
					[Set](UHexPlanetPainter& Each) { Each.SetObjectSet(Set); },
					[Set, Layer](const UHexPlanetPainter& Each)
					{
						return Each.GetTarget() == Layer && (Each.GetTool() == EHexPaintTool::Object || Each.IsUsingPlacedTool(EHexPlacedTool::Place)) && Each.GetObjectSet() == Set;
					});
				Button->SetToolTipText(bForeign
					? LOCTEXT("ForeignTip", "A kind of the other layer, which a hand alone places here: a click places one where you click")
					: bCover
					? LOCTEXT("CoverTip", "Fills the cells at the density below; a cell holds one cover. By hand, one of it stands where you click")
					: LOCTEXT("DecorTip", "A piece of decor near the middle of the cell; a cell holds one, beside its cover. By hand, one stands where you click"));
				int32& Count = bForeign ? NumForeign : NumOwn;
				(bForeign ? ForeignGrid : ObjectGrid)->AddSlot(Count % 2, Count / 2)
				[
					Button
				];
				++Count;
			}
			Objects->AddSlot().AutoHeight()[ObjectGrid];
			if (NumForeign > 0)
			{
				Objects->AddSlot().AutoHeight()[MakeHeading(Layer == EHexPlanetLayer::Underground
					? LOCTEXT("ForeignSurface", "From the surface, by hand") : LOCTEXT("ForeignUnderground", "From the underground, by hand"))];
				Objects->AddSlot().AutoHeight()[ForeignGrid];
			}

			// the kinds fill cells, or by hand each click places one where it falls, as many as a cell takes
			TSharedRef<SHorizontalBox> Modes = SNew(SHorizontalBox);
			const TSharedRef<SButton> OnCells = MakeButton(
				MakeLabel(LOCTEXT("OnCells", "On cells")),
				[](UHexPlanetPainter& Each) { Each.SetPlacingByHand(false); },
				[](const UHexPlanetPainter& Each) { return !Each.IsPlacingByHand(); });
			OnCells->SetToolTipText(LOCTEXT("OnCellsTip", "A kind fills the cells under the brush, a cover at the density below and a decor near the middle"));
			const TSharedRef<SButton> ByHand = MakeButton(
				MakeLabel(LOCTEXT("ByHand", "By hand")),
				[](UHexPlanetPainter& Each) { Each.SetPlacingByHand(true); },
				[](const UHexPlanetPainter& Each) { return Each.IsPlacingByHand(); });
			ByHand->SetToolTipText(LOCTEXT("ByHandTip", "A kind places one object where you click, as many as you like on a cell; Ctrl+click takes away the one nearest"));
			Modes->AddSlot().FillWidth(1.0f)[OnCells];
			Modes->AddSlot().FillWidth(1.0f).Padding(3.0f, 0.0f, 0.0f, 0.0f)[ByHand];
			Objects->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[Modes];

			// what a click does with what was placed by hand, the one nearest it
			struct FPlacedButton
			{
				EHexPlacedTool Tool;
				FText Label;
				FText Tip;
			};
			const FPlacedButton PlacedButtons[] = {
				{EHexPlacedTool::Place, LOCTEXT("PlacedPlace", "Place"), LOCTEXT("PlacedPlaceTip", "Click to place one of the kind chosen where you click, Ctrl+click to take away the one nearest")},
				{EHexPlacedTool::Move, LOCTEXT("PlacedMove", "Move"), LOCTEXT("PlacedMoveTip", "Drag the object placed by hand nearest where you press")},
				{EHexPlacedTool::Turn, LOCTEXT("PlacedTurn", "Turn"), LOCTEXT("PlacedTurnTip", "Click to turn the object placed by hand nearest the click, Ctrl+click to turn it back")},
				{EHexPlacedTool::Size, LOCTEXT("PlacedSize", "Size"), LOCTEXT("PlacedSizeTip", "Click to make the object placed by hand nearest the click larger, Ctrl+click smaller")},
				{EHexPlacedTool::Version, LOCTEXT("PlacedVersion", "Version"), LOCTEXT("PlacedVersionTip", "Click to give the object placed by hand nearest the click the next mesh of its kind, Ctrl+click the one before")},
			};
			TSharedRef<SUniformGridPanel> PlacedGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(PlacedButtons); ++Index)
			{
				const EHexPlacedTool Tool = PlacedButtons[Index].Tool;
				const TSharedRef<SButton> Button = MakeButton(
					MakeLabel(PlacedButtons[Index].Label),
					[Tool](UHexPlanetPainter& Each) { Each.SetPlacedTool(Tool); },
					[Tool, Layer](const UHexPlanetPainter& Each) { return Each.GetTarget() == Layer && Each.IsUsingPlacedTool(Tool); });
				Button->SetToolTipText(PlacedButtons[Index].Tip);
				PlacedGrid->AddSlot(Index % 3, Index / 3)
				[
					Button
				];
			}
			Objects->AddSlot().AutoHeight()[PlacedGrid];

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
			Eraser->SetToolTipText(LOCTEXT("EraserTip", "On cells it takes the cover, the decor and what was placed by hand off the cells under the brush; by hand it takes away the object nearest the cursor, and each one it passes while you drag"));
			Objects->AddSlot().AutoHeight()[Eraser];
			Objects->AddSlot().AutoHeight()[MakeHelp(Layer == EHexPlanetLayer::Underground
				? LOCTEXT("PlacedCaveHelp", "By hand an object may reach over into the next cell, but never over the void nor into a wall, and blocks nothing; a mass that blocks fills its cell and is painted on cells. Touching one of a cell painted on cells turns that cell into objects placed by hand.")
				: LOCTEXT("PlacedHelp", "By hand an object may reach over into the next cell, but not into a river, and blocks nothing, even a tree of a forest. Touching one of a cell painted on cells turns that cell into objects placed by hand."))];

			const TSharedRef<SButton> Again = MakeButton(
				MakeLabel(LOCTEXT("RegenerateObjects", "Generate again")),
				[](UHexPlanetPainter& Each) { Each.RegenerateObjects(); },
				nullptr,
				[](const UHexPlanetPainter& Each) { return !Each.IsPainting(); });
			Again->SetToolTipText(Layer == EHexPlanetLayer::Underground
				? LOCTEXT("RegenerateObjectsTip", "Gives the passages generated covers and decor again by the shares of the kinds; one step of Undo")
				: LOCTEXT("RegenerateSurfaceObjectsTip", "Gives the land generated covers and decor again by the shares of the kinds, in place of what was painted on cells; what was placed by hand stays; one step of Undo"));
			Objects->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)[Again];
			if (Layer == EHexPlanetLayer::Underground)
			{
				Objects->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("CaveObjectsHelp", "Cave objects stand on passages only and never hang over the void. None of them blocks movement, so the maze alone keeps heroes apart; nothing is drawn on an exit."))];
			}
		}
		else
		{
			Objects->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("NoObjects", "There are no kinds of objects (PlanetObjectsFolder of HexMapSettings)."))];
		}
	};
	TSharedRef<SVerticalBox> Objects = SNew(SVerticalBox);
	MakeObjects(EHexPlanetLayer::Surface, Objects);
	TSharedRef<SVerticalBox> CaveObjects = SNew(SVerticalBox);
	MakeObjects(EHexPlanetLayer::Underground, CaveObjects);

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
	{
		const TSharedRef<SButton> Turn = MakeButton(
			MakeLabel(LOCTEXT("EntranceTurn", "Turn")),
			[](UHexPlanetPainter& Each) { Each.SetEntranceTurnTool(); },
			[](const UHexPlanetPainter& Each) { return Each.IsTurningEntrances(); });
		Turn->SetToolTipText(LOCTEXT("EntranceTurnTip", "Click an entrance to turn its mouth to the next side of its cell; Ctrl+click turns it back. With a mountain behind it, it is built into the mountain"));
		EntranceBody->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 3.0f)[Turn];
		const TSharedRef<SButton> Remove = MakeButton(
			MakeLabel(LOCTEXT("EntranceRemove", "Remove")),
			[](UHexPlanetPainter& Each) { Each.SetEntranceRemoveTool(); },
			[](const UHexPlanetPainter& Each) { return Each.IsRemovingEntrances(); });
		Remove->SetToolTipText(LOCTEXT("EntranceRemoveTip", "Click an entrance to take it away, as Ctrl+click with a network chosen does"));
		EntranceBody->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[Remove];
	}
	EntranceBody->AddSlot().AutoHeight()[MakeHelp(FText::Format(LOCTEXT("EntranceHelp", "Click a cell of plains or hills to put an entrance of the chosen network there, or give it that network; Ctrl+click takes one away. Its exit is the same cell underground, which this does not carve: a red marker cannot be used. {0} over an entrance goes down."),
		FText::FromString(InArgs._LayerKeys)))];

	// Heroes, on either layer: the player and the class of the heroes a click places
	const UHexHeroConfig* HeroConfig = UHexHeroConfig::Load();
	const auto MakeHeroBody = [&]()
	{
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		Body->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("HeroPlayers", "Player"), 0.0f)];
		TSharedRef<SUniformGridPanel> Players = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Player = 0; Player < HeroConfig->Players.Num(); ++Player)
		{
			Players->AddSlot(Player % 2, Player / 2)
			[
				MakeButton(
					MakeSwatch(HeroConfig->Players[Player].Color, SwatchSize * 0.75f, SwatchSize * 0.75f, 6.0f, FText::FromString(HeroConfig->Players[Player].Name), Small, FText::GetEmpty()),
					[Player](UHexPlanetPainter& Each) { Each.SetHeroPlayer(Player); Each.SetHeroTool(); },
					[Player](const UHexPlanetPainter& Each) { return Each.IsPlacingHeroes() && Each.GetHeroPlayer() == Player; })
			];
		}
		Body->AddSlot().AutoHeight()[Players];
		Body->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("HeroClasses", "Class"))];
		TSharedRef<SUniformGridPanel> Classes = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Class = 0; Class < HeroConfig->Classes.Num(); ++Class)
		{
			Classes->AddSlot(Class % 2, Class / 2)
			[
				MakeButton(
					MakeLabel(FText::FromString(HeroConfig->Classes[Class].DisplayName)),
					[Class](UHexPlanetPainter& Each) { Each.SetHeroClass(Class); Each.SetHeroTool(); },
					[Class](const UHexPlanetPainter& Each) { return Each.IsPlacingHeroes() && Each.GetHeroClass() == Class; })
			];
		}
		Body->AddSlot().AutoHeight()[Classes];
		Body->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("HeroHelp", "Click a cell a hero can stand on to put one of the chosen player and class there; Ctrl+click takes one away, and Undo takes either back. That is where it stands when a game starts, which the save key writes. Play starts the game."))];

		// who moves the heroes of each player: a person, or the game by itself
		Body->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("HeroMovers", "Who plays"))];
		TSharedRef<SUniformGridPanel> Movers = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Player = 0; Player < HeroConfig->Players.Num(); ++Player)
		{
			TSharedRef<SHorizontalBox> Row = MakeSwatch(HeroConfig->Players[Player].Color, SwatchSize * 0.75f, SwatchSize * 0.75f, 6.0f,
				FText::FromString(HeroConfig->Players[Player].Name), Small, FText::GetEmpty());
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(Small)
				.Text_Lambda([WeakPainter, Player]()
				{
					return WeakPainter.IsValid() && WeakPainter->IsPlayerAi(Player) ? LOCTEXT("MoverAi", "AI") : LOCTEXT("MoverPerson", "Human");
				})
			];
			Movers->AddSlot(Player % 2, Player / 2)
			[
				MakeButton(Row,
					[Player](UHexPlanetPainter& Each) { Each.TogglePlayerAi(Player); },
					[Player](const UHexPlanetPainter& Each) { return Each.IsPlayerAi(Player); })
			];
		}
		Body->AddSlot().AutoHeight()[Movers];
		Body->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("MoverHelp", "Click a player to hand its heroes to the game or back to a person. The heroes of an AI player walk by themselves to explore the map; click one to watch what it sees. The save key writes this with the heroes."))];
		return Body;
	};
	TSharedRef<SVerticalBox> HeroBody = MakeHeroBody();
	TSharedRef<SVerticalBox> CaveHeroBody = MakeHeroBody();

	// Mines, on either layer: the kind of the mine or pile a click places
	const UHexSiteConfig* SiteConfig = UHexSiteConfig::Load();
	const auto MakeSiteBody = [&]()
	{
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		for (const bool bMines : {true, false})
		{
			Body->AddSlot().AutoHeight()[MakeHeading(bMines ? LOCTEXT("SiteMines", "Mines, owned by the hero's player") : LOCTEXT("SitePiles", "Piles, taken once, and mills"), bMines ? 0.0f : 8.0f)];
			TSharedRef<SUniformGridPanel> Kinds = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
			int32 Shown = 0;
			for (int32 Kind = 0; Kind < SiteConfig->Kinds.Num(); ++Kind)
			{
				const FHexSiteKind& Each = SiteConfig->Kinds[Kind];
				if (Each.IsMine() != bMines)
				{
					continue;
				}
				const int32 Resource = SiteConfig->FindResource(Each.Resource);
				const FLinearColor Color = SiteConfig->Resources.IsValidIndex(Resource) ? SiteConfig->Resources[Resource].Color : FLinearColor::White;
				const TSharedRef<SWidget> Button = MakeButton(
					MakeSwatch(Color, SwatchSize * 0.75f, SwatchSize * 0.75f, 6.0f, FText::FromString(Each.DisplayName), Small, FText::GetEmpty()),
					[Kind](UHexPlanetPainter& Brush) { Brush.SetSiteKind(Kind); Brush.SetSiteTool(); },
					[Kind](const UHexPlanetPainter& Brush) { return Brush.IsPlacingSites() && Brush.GetSiteKind() == Kind; });
				Button->SetToolTipText(bMines ? FText::Format(LOCTEXT("MineTip", "{0} a day of {1}"), FText::AsNumber(Each.PerDay), FText::FromName(Each.Resource))
					: Each.Type == EHexSiteType::Mill ? FText::Format(LOCTEXT("MillTip", "{0} days of the income of the mine of a resource of its own, once a week"), FText::AsNumber(SiteConfig->MillDays))
					: FText::Format(LOCTEXT("PileTip", "{0} to {1} of {2}, once"), FText::AsNumber(Each.AmountMin), FText::AsNumber(Each.AmountMax), FText::FromName(Each.Resource)));
				Kinds->AddSlot(Shown % 2, Shown / 2)[Button];
				++Shown;
			}
			Body->AddSlot().AutoHeight()[Kinds];
		}
		Body->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("SiteHelp", "Click a cell a hero can stand on to put a mine or a pile of the chosen kind there; Ctrl+click takes one away, and Undo takes either back. A mine or a mill fills its cell: in the game a hero beside it that is sent onto it takes it. A pile lies on its cell, and the hero that steps onto it takes it."))];

		Body->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("SiteGenerate", "Scatter over the planet"), 8.0f)];
		const TSharedRef<SButton> Generate = MakeButton(
			MakeLabel(LOCTEXT("GenerateSites", "Generate")),
			[](UHexPlanetPainter& Each) { Each.GenerateSites(); },
			nullptr,
			[](const UHexPlanetPainter& Each) { return !Each.IsPainting(); });
		Generate->SetToolTipText(LOCTEXT("GenerateSitesTip", "Scatters mines, piles and mills over both layers in place of all there are, by Generation of DA_HexSites: a sawmill, a quarry and some piles near where each player starts, the rest at random where a hero walks. Each press gives another layout; one step of Undo"));
		Body->AddSlot().AutoHeight()[Generate];
		const TSharedRef<SButton> Clear = MakeButton(
			MakeLabel(LOCTEXT("ClearSites", "Remove all")),
			[](UHexPlanetPainter& Each) { Each.GenerateSites(true); },
			nullptr,
			[](const UHexPlanetPainter& Each) { return !Each.IsPainting(); });
		Clear->SetToolTipText(LOCTEXT("ClearSitesTip", "Takes every mine, pile and mill off both layers; one step of Undo"));
		Body->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)[Clear];
		return Body;
	};
	TSharedRef<SVerticalBox> SiteBody = MakeSiteBody();
	TSharedRef<SVerticalBox> CaveSiteBody = MakeSiteBody();

	// Castles, on the surface: the faction of the castle a click places, and who has it when a game starts
	const UHexCastleConfig* CastleConfig = UHexCastleConfig::Load();
	TSharedRef<SVerticalBox> CastleBody = SNew(SVerticalBox);
	{
		CastleBody->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("CastleFactions", "Faction"), 0.0f)];
		TSharedRef<SUniformGridPanel> Factions = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Faction = 0; Faction < CastleConfig->Factions.Num(); ++Faction)
		{
			Factions->AddSlot(Faction % 2, Faction / 2)
			[
				MakeButton(
					MakeSwatch(CastleConfig->Factions[Faction].Color, SwatchSize * 0.75f, SwatchSize * 0.75f, 6.0f, FText::FromString(CastleConfig->Factions[Faction].DisplayName), Small, FText::GetEmpty()),
					[Faction](UHexPlanetPainter& Each) { Each.SetCastleFaction(Faction); Each.SetCastleTool(); },
					[Faction](const UHexPlanetPainter& Each) { return Each.IsPlacingCastles() && Each.GetCastleFaction() == Faction; })
			];
		}
		CastleBody->AddSlot().AutoHeight()[Factions];
		CastleBody->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("CastleOwners", "Who has it when a game starts"))];
		TSharedRef<SUniformGridPanel> Owners = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 0.0f, 3.0f, 3.0f));
		for (int32 Owner = -1; Owner < HeroConfig->Players.Num(); ++Owner)
		{
			const FLinearColor Color = Owner >= 0 ? HeroConfig->Players[Owner].Color : FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);
			const FText Name = Owner >= 0 ? FText::FromString(HeroConfig->Players[Owner].Name) : LOCTEXT("CastleNobody", "Nobody");
			Owners->AddSlot((Owner + 1) % 2, (Owner + 1) / 2)
			[
				MakeButton(
					MakeSwatch(Color, SwatchSize * 0.75f, SwatchSize * 0.75f, 6.0f, Name, Small, FText::GetEmpty()),
					[Owner](UHexPlanetPainter& Each) { Each.SetCastleOwner(Owner); Each.SetCastleTool(); },
					[Owner](const UHexPlanetPainter& Each) { return Each.IsPlacingCastles() && Each.GetCastleOwner() == Owner; })
			];
		}
		CastleBody->AddSlot().AutoHeight()[Owners];
		CastleBody->AddSlot().AutoHeight()[MakeHelp(LOCTEXT("CastleHelp", "Click a cell to put a castle on it and on the two cells around its corner nearest the cursor; Ctrl+click takes one away, and Undo takes either back. A castle fills its three cells: in the game a hero beside it that is sent onto it comes to its own, or takes another's if its army is the stronger. A castle of nobody is guarded."))];
	}

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

		// the walls along the voids, a height for each void: a click raises it, Ctrl+click lowers it, Auto gives it back to the style of the void
		const TSharedRef<SButton> WallHeight = MakeButton(
			MakeLabel(LOCTEXT("WallHeight", "Height")),
			[](UHexPlanetPainter& Each) { Each.SetWallTool(EHexPaintTool::WallHeight); },
			[](const UHexPlanetPainter& Each) { return Each.GetTool() == EHexPaintTool::WallHeight; });
		WallHeight->SetToolTipText(LOCTEXT("WallHeightTip", "Click a void, or a passage beside it, to raise the walls along it; Ctrl+click lowers them, down to a low curb"));
		const TSharedRef<SButton> WallAuto = MakeButton(
			MakeLabel(LOCTEXT("WallAuto", "Auto")),
			[](UHexPlanetPainter& Each) { Each.SetWallTool(EHexPaintTool::WallAuto); },
			[](const UHexPlanetPainter& Each) { return Each.GetTool() == EHexPaintTool::WallAuto; });
		WallAuto->SetToolTipText(LOCTEXT("WallAutoTip", "Click a void, or a passage beside it, to give the walls along it the height of its style: low beside lava and lakes, full beside the mist"));
		Maze->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("Walls", "Walls along the voids"))];
		Maze->AddSlot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				WallHeight
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(3.0f, 0.0f, 0.0f, 0.0f)
			[
				WallAuto
			]
		];
		Maze->AddSlot().AutoHeight()[MakeHelp(TAttribute<FText>::CreateLambda([WeakPainter]()
		{
			return FText::Format(LOCTEXT("MazeHelp", "Void and Passage paint with the brush. Carve makes a passage {0} cells wide along the shortest way from the cell pressed to the cell released. Each void has a height of the walls along it: painting it lava or a lake gives it a low one, which Height then tunes; the info at the cursor shows it."),
				FText::AsNumber(WeakPainter.IsValid() ? WeakPainter->GetPassageWidth() : 2));
		}))];
	}

	// Links: the passages in the colors of their networks, what is wrong in red, and generating a network again
	TSharedRef<SVerticalBox> Links = SNew(SVerticalBox);
	{
		Links->AddSlot().AutoHeight()[MakeHeading(LOCTEXT("LinkNetworks", "Networks"), 0.0f)];
		Links->AddSlot().AutoHeight()[MakeChips(false)];
		{
			const TSharedRef<SButton> Turn = MakeButton(
				MakeLabel(LOCTEXT("ExitTurn", "Turn an exit")),
				[](UHexPlanetPainter& Each) { Each.SetEntranceTurnTool(); },
				[](const UHexPlanetPainter& Each) { return Each.IsTurningEntrances(); });
			Turn->SetToolTipText(LOCTEXT("ExitTurnTip", "Click an exit to turn its mouth to the next side of its cell; Ctrl+click turns it back. With a void behind it, it is built into the wall there"));
			Links->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 3.0f)[Turn];
		}
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
		const TSharedRef<SButton> Layout = MakeButton(
			MakeLabel(LOCTEXT("NewLayout", "New layout")),
			[](UHexPlanetPainter& Each) { Each.NewLayout(); },
			nullptr,
			[](const UHexPlanetPainter& Each) { return !Each.IsPainting(); });
		Layout->SetToolTipText(LOCTEXT("NewLayoutTip", "Another maze for the same entrances: the seed offset of the underground goes up by one and is saved in planet.json; one step of Undo"));
		Links->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 3.0f)[One];
		Links->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[All];
		Links->AddSlot().AutoHeight()[Layout];
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

	// the size of the brush, for the tools that paint its rings; an entrance, a carved passage, a river and a bridge ignore it
	TSharedRef<SHorizontalBox> Sizes = SNew(SHorizontalBox);
	const auto TakesSize = [](const UHexPlanetPainter& Each) { return Each.UsesBrushSize() || !Each.HasTool(); };
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
				[Size, TakesSize](const UHexPlanetPainter& Each) { return TakesSize(Each) && Each.GetBrushSize() == Size; },
				TakesSize)
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
			+ SWidgetSwitcher::Slot()[HeroBody]
			+ SWidgetSwitcher::Slot()[SiteBody]
			+ SWidgetSwitcher::Slot()[Maze]
			+ SWidgetSwitcher::Slot()[Links]
			+ SWidgetSwitcher::Slot()[CaveObjects]
			+ SWidgetSwitcher::Slot()[CaveHeroBody]
			+ SWidgetSwitcher::Slot()[CaveSiteBody]
			+ SWidgetSwitcher::Slot()[CastleBody]
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

	// anchored to the top left corner, so it stays put as a tab or a tool changes its height, and a palette taller than the screen scrolls, as
	// the slot clamps it to the height it has
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(16.0f, SHexModeBar::GetPaletteTop(), 0.0f, 16.0f)
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
