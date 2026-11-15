// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexEconomyPanel.h"
#include "HexCastleConfig.h"
#include "HexCastles.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "SHexSpellBook.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexEconomyPanel"

namespace
{
	const FLinearColor EconomyShade(0.0f, 0.0f, 0.0f, 0.45f);
	const FLinearColor EconomyBack(0.05f, 0.04f, 0.035f, 0.97f);
	const FLinearColor EconomyRow(0.1f, 0.085f, 0.07f, 1.0f);
	const FLinearColor EconomyText(0.92f, 0.88f, 0.8f, 1.0f);
	const FLinearColor EconomyDim(0.6f, 0.57f, 0.52f, 1.0f);
	const FLinearColor EconomyGold(1.0f, 0.82f, 0.35f, 1.0f);
	const FLinearColor EconomyGain(0.45f, 0.9f, 0.45f, 1.0f);
	const float EconomyIcon = 22.0f;
}

void SHexEconomyPanel::Construct(const FArguments& InArgs)
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	SetVisibility(EVisibility::Collapsed);
	ChildSlot
	[
		SNew(SOverlay)
		// the shade takes every click outside the panel, which closes it, so nothing behind it is picked
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(EconomyShade)
			.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { Close(); return FReply::Handled(); })
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(640.0f).MaxDesiredHeight(620.0f)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(EconomyBack).Padding(FMargin(14.0f, 10.0f))
				.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
				[
					SAssignNew(Body, SVerticalBox)
				]
			]
		]
	];
}

void SHexEconomyPanel::Open(UHexHeroes* InHeroes, int32 InPlayer)
{
	Heroes = InHeroes;
	Player = InPlayer;
	bOpen = true;
	SetVisibility(EVisibility::Visible);
	Rebuild();
}

void SHexEconomyPanel::Close()
{
	if (!bOpen)
	{
		return;
	}
	bOpen = false;
	SetVisibility(EVisibility::Collapsed);
}

FReply SHexEconomyPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
	}
	// nothing else reaches the game while the panel is open
	return FReply::Handled();
}

void SHexEconomyPanel::Rebuild()
{
	Body->ClearChildren();
	const UHexHeroes* Each = Heroes.Get();
	const UHexSites* Sites = Each ? Each->GetSites() : nullptr;
	const UHexCastles* Castles = Each ? Each->GetCastles() : nullptr;
	if (!Sites || !Castles || !Sites->GetConfig() || !Castles->GetConfig())
	{
		return;
	}
	const UHexSiteConfig* SiteConfig = Sites->GetConfig();
	const UHexCastleConfig* CastleConfig = Castles->GetConfig();
	const FSlateFontInfo Head = FCoreStyle::GetDefaultFontStyle("Bold", 14);
	const FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const int32 DaysPerWeek = Each->GetConfig() ? FMath::Max(Each->GetConfig()->DaysPerWeek, 1) : 7;
	const int32 Gold = SiteConfig->FindResource(TEXT("Gold"));

	// what the halls of its castles give a day, and the castles themselves
	int32 HallGold = 0;
	TArray<TPair<FString, int32>> CastleLines;
	for (const FHexCastle& Castle : Castles->GetCastles())
	{
		if (Castle.Owner != Player || !CastleConfig->Factions.IsValidIndex(Castle.Faction))
		{
			continue;
		}
		const FHexFaction& Faction = CastleConfig->Factions[Castle.Faction];
		const int32 Hall = Faction.FindBuilding(EHexBuildingType::Hall);
		const int32 Level = Castles->GetLevel(Castle, EHexBuildingType::Hall);
		const int32 Given = Faction.Buildings.IsValidIndex(Hall) && Faction.Buildings[Hall].Levels.IsValidIndex(Level - 1) ? Faction.Buildings[Hall].Levels[Level - 1].GoldPerDay : 0;
		HallGold += Given;
		const FString HallName = Faction.Buildings.IsValidIndex(Hall) && Faction.Buildings[Hall].Levels.IsValidIndex(Level - 1) ? Faction.Buildings[Hall].Levels[Level - 1].DisplayName : TEXT("no hall");
		CastleLines.Add({FString::Printf(TEXT("%s, %s: %s"), *Castles->GetCastleName(Castle), *Faction.DisplayName, *HallName), Given});
	}
	// its mines by kind
	TMap<int32, int32> MineCounts;
	for (const FHexSite& Site : Sites->GetSites())
	{
		if (Site.Owner == Player && SiteConfig->Kinds.IsValidIndex(Site.Kind) && SiteConfig->Kinds[Site.Kind].IsMine())
		{
			++MineCounts.FindOrAdd(Site.Kind);
		}
	}

	const FString PlayerName = Each->GetConfig() ? Each->GetConfig()->GetPlayer(Player).Name : FString::FromInt(Player);
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Head).ColorAndOpacity(EconomyGold).Text(FText::Format(LOCTEXT("Title", "The economy of {0}"), FText::FromString(PlayerName)))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(8.0f, 2.0f)).OnClicked_Lambda([this]() { Close(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Bold).Text(LOCTEXT("Close", "x"))
			]
		]
	];

	// a row a resource: what it has, what it gets a day from its mines and halls, and so a week
	const auto Cell = [&](const FText& Text, const FLinearColor& Color, float Width, const FSlateFontInfo& With)
	{
		return SNew(SBox).WidthOverride(Width).HAlign(HAlign_Right)[SNew(STextBlock).Font(With).ColorAndOpacity(Color).Text(Text)];
	};
	TSharedRef<SVerticalBox> Table = SNew(SVerticalBox);
	Table->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Font(Small).ColorAndOpacity(EconomyDim).Text(LOCTEXT("Resource", "Resource"))]
		+ SHorizontalBox::Slot().AutoWidth()[Cell(LOCTEXT("Has", "Has"), EconomyDim, 90.0f, Small)]
		+ SHorizontalBox::Slot().AutoWidth()[Cell(LOCTEXT("Mines", "Mines a day"), EconomyDim, 100.0f, Small)]
		+ SHorizontalBox::Slot().AutoWidth()[Cell(LOCTEXT("Halls", "Halls a day"), EconomyDim, 100.0f, Small)]
		+ SHorizontalBox::Slot().AutoWidth()[Cell(LOCTEXT("Week", "A week"), EconomyDim, 100.0f, Small)]
	];
	for (int32 Resource = 0; Resource < SiteConfig->Resources.Num(); ++Resource)
	{
		const FHexResourceInfo& Info = SiteConfig->Resources[Resource];
		const int32 Mines = Sites->GetIncome(Player, Resource);
		const int32 Halls = Resource == Gold ? HallGold : 0;
		const FSlateBrush* Icon = HexUiBrushes::Get(Info.Icon.LoadSynchronous(), EconomyIcon, Info.IconFrames);
		Table->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(EconomyRow).Padding(FMargin(6.0f, 3.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(EconomyIcon).HeightOverride(EconomyIcon)
					[
						Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon))
							: StaticCastSharedRef<SWidget>(SNew(SImage).Image(White).ColorAndOpacity(Info.Color))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Font(Bold).ColorAndOpacity(EconomyText).Text(FText::FromString(Info.DisplayName))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(FText::AsNumber(Sites->GetAmount(Player, Resource)), EconomyText, 90.0f, Font)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(Mines > 0 ? FText::Format(LOCTEXT("Plus", "+{0}"), FText::AsNumber(Mines)) : FText::FromString(TEXT("-")),
					Mines > 0 ? EconomyGain : EconomyDim, 100.0f, Font)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(Halls > 0 ? FText::Format(LOCTEXT("Plus", "+{0}"), FText::AsNumber(Halls)) : FText::FromString(TEXT("-")),
					Halls > 0 ? EconomyGain : EconomyDim, 100.0f, Font)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell((Mines + Halls) > 0 ? FText::Format(LOCTEXT("Plus", "+{0}"), FText::AsNumber((Mines + Halls) * DaysPerWeek))
					: FText::FromString(TEXT("-")), (Mines + Halls) > 0 ? EconomyGain : EconomyDim, 100.0f, Font)]
			]
		];
	}
	// its mines by kind, and its castles
	TSharedRef<SVerticalBox> Lists = SNew(SVerticalBox);
	Lists->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 3.0f)
	[
		SNew(STextBlock).Font(Bold).ColorAndOpacity(EconomyGold).Text(FText::Format(LOCTEXT("MinesHead", "Mines ({0})"), FText::AsNumber(MineCounts.Num() > 0 ? [&MineCounts]() { int32 All = 0; for (const TPair<int32, int32>& Pair : MineCounts) { All += Pair.Value; } return All; }() : 0)))
	];
	if (MineCounts.IsEmpty())
	{
		Lists->AddSlot().AutoHeight()[SNew(STextBlock).Font(Small).ColorAndOpacity(EconomyDim).Text(LOCTEXT("NoMines", "No mines yet: a hero takes one by going onto it"))];
	}
	MineCounts.KeySort([](int32 A, int32 B) { return A < B; });
	for (const TPair<int32, int32>& Pair : MineCounts)
	{
		const FHexSiteKind& Kind = SiteConfig->Kinds[Pair.Key];
		const int32 Resource = SiteConfig->FindResource(Kind.Resource);
		const FString ResourceName = SiteConfig->Resources.IsValidIndex(Resource) ? SiteConfig->Resources[Resource].DisplayName.ToLower() : FString();
		Lists->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(Font).ColorAndOpacity(EconomyText)
			.Text(FText::FromString(FString::Printf(TEXT("%s x%d: +%d %s a day"), *Kind.DisplayName, Pair.Value, Kind.PerDay * Pair.Value, *ResourceName)))
		];
	}
	Lists->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 3.0f)
	[
		SNew(STextBlock).Font(Bold).ColorAndOpacity(EconomyGold).Text(FText::Format(LOCTEXT("CastlesHead", "Castles ({0})"), FText::AsNumber(CastleLines.Num())))
	];
	for (const TPair<FString, int32>& Line : CastleLines)
	{
		Lists->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(Font).ColorAndOpacity(EconomyText)
			.Text(FText::FromString(FString::Printf(TEXT("%s: +%d gold a day"), *Line.Key, Line.Value)))
		];
	}
	Body->AddSlot().FillHeight(1.0f)
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()[Table]
		+ SScrollBox::Slot()[Lists]
	];
}

#undef LOCTEXT_NAMESPACE
