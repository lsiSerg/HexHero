// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexHeroPanel.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexHeroPanel"

namespace
{
	const float HeroPanelWidth = 280.0f;
	const FLinearColor HeroPanelColor(0.02f, 0.02f, 0.02f, 0.8f);
	const FLinearColor HeroChosenColor(1.0f, 0.75f, 0.1f, 1.0f);
	const FLinearColor HeroButtonColor(0.3f, 0.3f, 0.3f, 1.0f);
	const FLinearColor HeroDimColor(0.75f, 0.75f, 0.75f);
}

void SHexHeroPanel::Construct(const FArguments& InArgs)
{
	OnHeroChosen = InArgs._OnHeroChosen;
	OnEndTurn = InArgs._OnEndTurn;

	// the widget covers the viewport, of which only the panel takes the mouse
	SetVisibility(EVisibility::SelfHitTestInvisible);

	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FString EndTurnKeys = InArgs._EndTurnKeys;
	const FString NextHeroKeys = InArgs._NextHeroKeys;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(16.0f)
		[
			SNew(SBox)
			.WidthOverride(HeroPanelWidth)
			.Visibility_Lambda([this]() { return Heroes.IsValid() && !Heroes->GetHeroes().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBorder)
				.BorderImage(White)
				.BorderBackgroundColor(HeroPanelColor)
				.Padding(10.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Bold)
						.Text_Lambda([this]()
						{
							if (!Heroes.IsValid() || !Heroes->GetConfig())
							{
								return FText::GetEmpty();
							}
							const int32 Day = Heroes->GetDay();
							const int32 PerWeek = FMath::Max(Heroes->GetConfig()->DaysPerWeek, 1);
							return FText::Format(LOCTEXT("Day", "Day {0}, week {1}"), FText::AsNumber((Day - 1) % PerWeek + 1), FText::AsNumber((Day - 1) / PerWeek + 1));
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SAssignNew(List, SVerticalBox)
					]
					// the attributes of the hero chosen
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(Small)
						.AutoWrapText(true)
						.ColorAndOpacity(HeroDimColor)
						.Text_Lambda([this, NextHeroKeys]()
						{
							const FHexHero* Hero = Heroes.IsValid() ? Heroes->GetSelected() : nullptr;
							if (!Hero)
							{
								return FText::Format(LOCTEXT("NoHero", "Left click a hero to choose it{0}; a right click sends it."),
									NextHeroKeys.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("OrKey", ", or {0}"), FText::FromString(NextHeroKeys)));
							}
							return FText::Format(LOCTEXT("Attributes", "Level {0}   Attack {1}   Defense {2}\nSpell power {3}   Knowledge {4}   Mana {5}"), FText::AsNumber(Hero->Level),
								FText::AsNumber(Hero->Attack), FText::AsNumber(Hero->Defense), FText::AsNumber(Hero->SpellPower), FText::AsNumber(Hero->Knowledge), FText::AsNumber(Hero->Mana));
						})
					]
					// what the way to the cell under the cursor costs
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(Small)
						.AutoWrapText(true)
						.Text_Lambda([this]() { return Heroes.IsValid() ? FText::FromString(Heroes->GetHoverText()) : FText::GetEmpty(); })
						.Visibility_Lambda([this]() { return Heroes.IsValid() && !Heroes->GetHoverText().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.IsFocusable(false)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(6.0f, 4.0f))
						.ButtonColorAndOpacity(HeroButtonColor)
						.ToolTipText(LOCTEXT("EndTurnTip", "The next day begins, and every hero gets its points of movement back"))
						.OnClicked_Lambda([this]()
						{
							OnEndTurn.ExecuteIfBound();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Font(Font)
							.Text(EndTurnKeys.IsEmpty() ? LOCTEXT("EndTurn", "End the day") : FText::Format(LOCTEXT("EndTurnKeys", "End the day ({0})"), FText::FromString(EndTurnKeys)))
						]
					]
				]
			]
		]
	];
}

void SHexHeroPanel::SetHeroes(UHexHeroes* InHeroes)
{
	if (Heroes.Get() != InHeroes)
	{
		Heroes = InHeroes;
		ListedRevision = -1;
	}
}

void SHexHeroPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const int32 Revision = Heroes.IsValid() ? Heroes->GetListRevision() : -1;
	if (Revision != ListedRevision)
	{
		ListedRevision = Revision;
		RebuildList();
	}
}

void SHexHeroPanel::RebuildList()
{
	if (!List.IsValid())
	{
		return;
	}
	List->ClearChildren();
	if (!Heroes.IsValid() || !Heroes->GetConfig())
	{
		return;
	}

	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const UHexHeroConfig* Config = Heroes->GetConfig();
	TWeakObjectPtr<UHexHeroes> WeakHeroes = Heroes;

	// in the order of their Ids, as the key of the next hero goes through them
	TArray<const FHexHero*> Sorted;
	for (const FHexHero& Hero : Heroes->GetHeroes())
	{
		Sorted.Add(&Hero);
	}
	Sorted.Sort([](const FHexHero& A, const FHexHero& B) { return A.Id < B.Id; });

	for (const FHexHero* Hero : Sorted)
	{
		const int32 Id = Hero->Id;
		const FLinearColor Color = Config->GetPlayer(Hero->Player).Color;
		List->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(FMargin(6.0f, 4.0f))
			.ButtonColorAndOpacity_Lambda([WeakHeroes, Id]() { return WeakHeroes.IsValid() && WeakHeroes->GetSelectedId() == Id ? HeroChosenColor : HeroButtonColor; })
			.OnClicked_Lambda([this, Id]()
			{
				OnHeroChosen.ExecuteIfBound(Id);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				// the color of its player
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(12.0f)
					.HeightOverride(30.0f)
					[
						SNew(SImage)
						.Image(White)
						.ColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, 1.0f))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Font)
						.Text(FText::Format(LOCTEXT("HeroLine", "{0}, {1}"), FText::FromString(Hero->Name), FText::FromString(Config->GetClass(Hero->Class).DisplayName)))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Small)
						.ColorAndOpacity(HeroDimColor)
						.Text_Lambda([WeakHeroes, Id]()
						{
							const FHexHero* Each = WeakHeroes.IsValid() ? WeakHeroes->FindHero(Id) : nullptr;
							if (!Each)
							{
								return FText::GetEmpty();
							}
							FNumberFormattingOptions Options;
							Options.SetMaximumFractionalDigits(1);
							return FText::Format(LOCTEXT("Movement", "Movement {0} of {1}{2}"), FText::AsNumber(Each->MovePoints, &Options),
								FText::AsNumber(WeakHeroes->GetMaxMovePoints(*Each), &Options),
								Each->Layer == EHexPlanetLayer::Underground ? LOCTEXT("Below", ", below") : FText::GetEmpty());
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.HeightOverride(4.0f)
						[
							SNew(SProgressBar)
							.FillColorAndOpacity(FLinearColor(0.35f, 1.0f, 0.3f, 1.0f))
							.Percent_Lambda([WeakHeroes, Id]()
							{
								const FHexHero* Each = WeakHeroes.IsValid() ? WeakHeroes->FindHero(Id) : nullptr;
								return TOptional<float>(Each ? Each->MovePoints / FMath::Max(WeakHeroes->GetMaxMovePoints(*Each), 0.01f) : 0.0f);
							})
						]
					]
				]
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE
