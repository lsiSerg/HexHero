// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexHeroPanel.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
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
	const float HeroPanelWidth = 340.0f;
	const FLinearColor HeroPanelColor(0.02f, 0.02f, 0.02f, 0.8f);
	const FLinearColor HeroChosenColor(1.0f, 0.75f, 0.1f, 1.0f);
	const FLinearColor HeroButtonColor(0.3f, 0.3f, 0.3f, 1.0f);
	const FLinearColor HeroDimColor(0.75f, 0.75f, 0.75f);
}

void SHexHeroPanel::Construct(const FArguments& InArgs)
{
	OnHeroChosen = InArgs._OnHeroChosen;
	OnEndTurn = InArgs._OnEndTurn;
	OnRestart = InArgs._OnRestart;
	OnSkip = InArgs._OnSkip;
	const FString SkipKeys = InArgs._SkipKeys;
	OnProject = InArgs._OnProject;

	// the widget covers the viewport, of which only the panel takes the mouse
	SetVisibility(EVisibility::SelfHitTestInvisible);

	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FString EndTurnKeys = InArgs._EndTurnKeys;
	const FString NextHeroKeys = InArgs._NextHeroKeys;

	// what the player of the hero chosen has of each resource, and gets from its mines every day
	TSharedRef<SHorizontalBox> ResourceRow = SNew(SHorizontalBox);
	const UHexSiteConfig* SiteConfig = UHexSiteConfig::Load();
	for (int32 Resource = 0; Resource < SiteConfig->Resources.Num(); ++Resource)
	{
		const FLinearColor Color = SiteConfig->Resources[Resource].Color;
		ResourceRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(Resource == 0 ? 0.0f : 14.0f, 0.0f, 5.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(10.0f)
			.HeightOverride(10.0f)
			[
				SNew(SImage)
				.Image(White)
				.ColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, 1.0f))
			]
		];
		ResourceRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(Font)
			.ToolTipText(FText::FromString(SiteConfig->Resources[Resource].DisplayName))
			.Text_Lambda([this, Resource, Name = SiteConfig->Resources[Resource].DisplayName]()
			{
				const FHexHero* Hero = Heroes.IsValid() ? Heroes->GetSelected() : nullptr;
				const UHexSites* Sites = Hero ? Heroes->GetSites() : nullptr;
				if (!Sites)
				{
					return FText::GetEmpty();
				}
				const int32 Income = Sites->GetIncome(Hero->Player, Resource);
				return Income > 0 ? FText::Format(LOCTEXT("ResourceIncome", "{0} {1} (+{2})"), FText::FromString(Name), FText::AsNumber(Sites->GetAmount(Hero->Player, Resource)), FText::AsNumber(Income))
					: FText::Format(LOCTEXT("Resource", "{0} {1}"), FText::FromString(Name), FText::AsNumber(Sites->GetAmount(Hero->Player, Resource)));
			})
		];
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(16.0f)
		[
			SNew(SBorder)
			.BorderImage(White)
			.BorderBackgroundColor(HeroPanelColor)
			.Padding(FMargin(12.0f, 6.0f))
			.Visibility_Lambda([this]() { return Heroes.IsValid() && Heroes->IsPlaying() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				ResourceRow
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(16.0f)
		[
			SNew(SBox)
			.WidthOverride(HeroPanelWidth)
			.Visibility_Lambda([this]() { return Heroes.IsValid() && Heroes->IsPlaying() && !Heroes->GetHeroes().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
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
							// and whose turn it is
							const FText Player = FText::FromString(Heroes->GetConfig()->GetPlayer(Heroes->GetTurnPlayer()).Name);
							return FText::Format(Heroes->IsPersonTurn() ? LOCTEXT("DayTurn", "Day {0}, week {1}: {2}") : LOCTEXT("DayWait", "Day {0}, week {1}: {2} moves..."),
								FText::AsNumber((Day - 1) % PerWeek + 1), FText::AsNumber((Day - 1) / PerWeek + 1), Player);
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
								return FText::GetEmpty();
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
					// how the mouse and the keys move a hero
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(Small)
						.AutoWrapText(true)
						.ColorAndOpacity(HeroDimColor)
						.Text(FText::Format(LOCTEXT("How", "Click a cell to show the way, click it again{0} to walk. Click a hero{1} to choose it, twice to look at it."),
							InArgs._GoKeys.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("OrGo", " or press {0}"), FText::FromString(InArgs._GoKeys)),
							NextHeroKeys.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("OrNext", " or press {0}"), FText::FromString(NextHeroKeys))))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 3.0f, 0.0f)
						[
							SNew(SButton)
							.IsFocusable(false)
							.HAlign(HAlign_Center)
							.ContentPadding(FMargin(6.0f, 4.0f))
							.ButtonColorAndOpacity(HeroButtonColor)
							.ToolTipText(LOCTEXT("SkipTip", "Leaves the hero chosen for today: the next hero is chosen, and this one is passed over until the next day or until it is sent again"))
							.IsEnabled_Lambda([this]() { return Heroes.IsValid() && Heroes->IsPersonTurn(); })
							.OnClicked_Lambda([this]()
							{
								OnSkip.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Font(Font)
								.Text(SkipKeys.IsEmpty() ? LOCTEXT("Skip", "Skip") : FText::Format(LOCTEXT("SkipKeys", "Skip ({0})"), FText::FromString(SkipKeys)))
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SButton)
							.IsFocusable(false)
							.HAlign(HAlign_Center)
							.ContentPadding(FMargin(6.0f, 4.0f))
							.ButtonColorAndOpacity(HeroButtonColor)
							.ToolTipText(LOCTEXT("EndTurnTip", "The next player moves, and after the last of them the next day begins, when every hero gets its points of movement back"))
							.IsEnabled_Lambda([this]() { return Heroes.IsValid() && Heroes->IsPersonTurn(); })
							.OnClicked_Lambda([this]()
							{
								OnEndTurn.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Font(Font)
								.Text(EndTurnKeys.IsEmpty() ? LOCTEXT("EndTurn", "End turn") : FText::Format(LOCTEXT("EndTurnKeys", "End turn ({0})"), FText::FromString(EndTurnKeys)))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(3.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.IsFocusable(false)
							.HAlign(HAlign_Center)
							.ContentPadding(FMargin(6.0f, 4.0f))
							.ButtonColorAndOpacity(HeroButtonColor)
							.ToolTipText(LOCTEXT("RestartTip", "Day 1 again, with every hero back where it starts"))
							.OnClicked_Lambda([this]()
							{
								OnRestart.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Font(Font)
								.Text(LOCTEXT("Restart", "New game"))
							]
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

int32 SHexHeroPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (Heroes.IsValid() && Heroes->IsPlaying() && Heroes->GetConfig() && OnProject)
	{
		const UHexHeroConfig* Config = Heroes->GetConfig();
		const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", Config->PathLabelSize);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const float Scale = FMath::Max(AllottedGeometry.Scale, 0.01f);
		const FVector2D Margin(6.0, 2.0);
		for (const UHexHeroes::FPathLabel& Label : Heroes->GetPathLabels())
		{
			FVector2D Screen;
			if (!OnProject(FVector(Label.Point), Screen))
			{
				continue;
			}
			// the day a hero ends at a mark, and at the end of a way the days all of it takes
			const FString Text = !Label.Text.IsEmpty() ? Label.Text : Label.bEnd ? FString::Printf(TEXT("%d days"), Label.Days) : FString::FromInt(Label.Days);
			const FVector2D TextSize = Measure->Measure(Text, Font);
			const FVector2D BoxSize = TextSize + Margin * 2.0;
			// above its mark, in the units of the widget, which fills the viewport
			const FVector2D Position = Screen / Scale - FVector2D(BoxSize.X * 0.5, BoxSize.Y + 10.0);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)), White,
				ESlateDrawEffect::None, Config->PathLabelBack);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Position + Margin)), Text, Font,
				ESlateDrawEffect::None, Config->PathLabelColor);
		}

		// what a hero gained at a site floats up over it and fades
		const UHexSites* Sites = Heroes->GetSites();
		const UHexSiteConfig* SiteConfig = Sites ? Sites->GetConfig() : nullptr;
		if (SiteConfig)
		{
			const FSlateFontInfo PopupFont = FCoreStyle::GetDefaultFontStyle("Bold", SiteConfig->PopupSize);
			const double Now = FPlatformTime::Seconds();
			for (const FHexSitePopup& Popup : Sites->GetPopups())
			{
				const float Age = float((Now - Popup.StartTime) / FMath::Max(double(SiteConfig->PopupTime), 0.1));
				FVector2D Screen;
				if (Popup.Layer != Heroes->GetPathLayer() || Age < 0.0f || Age > 1.0f || !OnProject(FVector(Popup.Point), Screen))
				{
					continue;
				}
				// it rises fast at first and slows, and fades over its last third
				const float Risen = 1.0f - FMath::Square(1.0f - Age);
				const float Alpha = FMath::Clamp((1.0f - Age) * 3.0f, 0.0f, 1.0f);
				const FVector2D TextSize = Measure->Measure(Popup.Text, PopupFont);
				const FVector2D Position = Screen / Scale - FVector2D(TextSize.X * 0.5, TextSize.Y + double(SiteConfig->PopupRise * Risen));
				FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Position + FVector2D(1.5, 1.5))), Popup.Text,
					PopupFont, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f * Alpha));
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Position)), Popup.Text, PopupFont,
					ESlateDrawEffect::None, Popup.Color.CopyWithNewOpacity(Alpha));
			}
		}
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
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
		// while the game is played, those of the person who plays, as the others are not its to know of
		if (!Heroes->IsPlaying() || Hero.Player == Heroes->GetShroudPlayer())
		{
			Sorted.Add(&Hero);
		}
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
						.Text(FText::Format(LOCTEXT("HeroLine", "{0}, {1}{2}"), FText::FromString(Hero->Name), FText::FromString(Config->GetClass(Hero->Class).DisplayName),
						Heroes->IsPlayerAi(Hero->Player) ? LOCTEXT("HeroAi", " (AI)") : FText::GetEmpty()))
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
							return FText::Format(LOCTEXT("Movement", "Movement {0} of {1}{2}{3}"), FText::AsNumber(Each->MovePoints, &Options),
								FText::AsNumber(WeakHeroes->GetMaxMovePoints(*Each), &Options),
								Each->Layer == EHexPlanetLayer::Underground ? LOCTEXT("Below", ", below") : FText::GetEmpty(),
								Each->bSkipped ? LOCTEXT("Skipped", ", skipped") : FText::GetEmpty());
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
