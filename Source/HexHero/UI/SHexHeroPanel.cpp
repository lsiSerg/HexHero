// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexHeroPanel.h"
#include "HexHeroConfig.h"
#include "HexCastles.h"
#include "HexHeroes.h"
#include "HexHeroPath.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "HexUnitTooltip.h"
#include "SHexSpellBook.h"
#include "Widgets/SToolTip.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Engine/Texture2D.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexHeroPanel"

namespace
{
	/** A white gradient across the narrow side of a bar, lit at its top or left edge and darker towards the other, which the color of a bar
	 *  tints, as the developer asked on 2026-11-15 for bars drawn as a gradient across their width: down a bar that lies, or across the
	 *  column of movement that stands (bAcross). A texture of 8 texels made once and kept */
	const FSlateBrush* GetGradientBrush(bool bAcross)
	{
		static FSlateBrush Brushes[2];
		static bool bMade[2] = {false, false};
		const int32 Kind = bAcross ? 1 : 0;
		if (!bMade[Kind])
		{
			bMade[Kind] = true;
			constexpr int32 Texels = 8;
			const float Profile[Texels] = {0.82f, 1.0f, 0.96f, 0.86f, 0.76f, 0.67f, 0.59f, 0.5f};
			UTexture2D* Texture = UTexture2D::CreateTransient(bAcross ? Texels : 1, bAcross ? 1 : Texels, PF_B8G8R8A8);
			if (Texture && Texture->GetPlatformData() && !Texture->GetPlatformData()->Mips.IsEmpty())
			{
				Texture->Filter = TF_Bilinear;
				Texture->AddressX = TA_Clamp;
				Texture->AddressY = TA_Clamp;
				FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
				uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
				for (int32 Index = 0; Index < Texels; ++Index)
				{
					const uint8 Value = uint8(FMath::RoundToInt32(Profile[Index] * 255.0f));
					Data[Index * 4 + 0] = Value;
					Data[Index * 4 + 1] = Value;
					Data[Index * 4 + 2] = Value;
					Data[Index * 4 + 3] = 255;
				}
				Mip.BulkData.Unlock();
				Texture->UpdateResource();
				Texture->AddToRoot();
				Brushes[Kind].SetResourceObject(Texture);
				Brushes[Kind].ImageSize = bAcross ? FVector2D(Texels, 1.0) : FVector2D(1.0, Texels);
				Brushes[Kind].DrawAs = ESlateBrushDrawType::Image;
				Brushes[Kind].Tiling = ESlateBrushTileType::NoTile;
			}
			else
			{
				Brushes[Kind] = FSlateColorBrush(FLinearColor::White);
			}
		}
		return &Brushes[Kind];
	}

	/** The bars of movement and experience of a hero: a fill in their own color, a gradient across its width, over a dark ground, as the
	 *  striped bar of the engine turned every color into its teal */
	const FProgressBarStyle& GetBarStyle()
	{
		static const FProgressBarStyle Style = []()
		{
			FProgressBarStyle Each = FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
			Each.SetBackgroundImage(FSlateColorBrush(FLinearColor(0.04f, 0.04f, 0.05f, 0.95f)));
			Each.SetFillImage(*GetGradientBrush(false));
			return Each;
		}();
		return Style;
	}

	/** A number on a bar, 1000 and more as thousands with one decimal, as "12.5k", as the developer asked on 2026-11-15 */
	FText ShortNumber(int32 Value)
	{
		if (FMath::Abs(Value) < 1000)
		{
			return FText::AsNumber(Value);
		}
		const double Thousands = FMath::RoundToDouble(double(Value) / 100.0) / 10.0;
		FString Text = FString::Printf(TEXT("%.1f"), Thousands);
		Text.RemoveFromEnd(TEXT(".0"));
		return FText::FromString(Text + TEXT("k"));
	}

	/** A black outline a pixel wide around a bar, as the developer asked on 2026-11-15 */
	const FLinearColor BarOutlineColor(0.0f, 0.0f, 0.0f, 0.95f);

	/** The picture of a button beside a hero, as the developer asked on 2026-11-15 for buttons without words, which their tooltips name; a
	 *  letter while the icon is missing */
	TSharedRef<SWidget> MakeButtonIcon(EHexStatIcon Icon, const TCHAR* Letter, const TAttribute<FSlateColor>& Tint = FSlateColor(FLinearColor::White))
	{
		const FSlateBrush* Brush = HexUnitTooltip::GetIcon(Icon);
		return SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			Brush ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush).ColorAndOpacity(Tint))
				: StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(Tint).Text(FText::FromString(Letter)))
		];
	}
}

namespace
{
	const float HeroPanelWidth = 380.0f;
	// how wide the hint at the bottom in the middle of the screen is at most
	const float HintWidth = 560.0f;
	// the column of the movement of a hero beside its portrait: its height, how many pluses of points above that of every hero it shows at most and
	// how high each is, and the colors of movement, experience and mana
	const float MoveColumnHeight = 40.0f;
	const float MoveColumnWidth = 10.0f;
	const int32 MaxMovePluses = 4;
	const float MovePlusSize = 6.0f;
	const float MovePlusThickness = 2.0f;
	const FLinearColor MovePlusColor(0.0f, 0.0f, 0.0f, 0.4f);
	const FLinearColor MoveColor(0.3f, 0.85f, 0.25f, 1.0f);
	const FLinearColor ExperienceColor(0.95f, 0.72f, 0.2f, 1.0f);
	const FLinearColor ManaColor(0.85f, 0.18f, 0.15f, 1.0f);
	const FLinearColor HeroPanelColor(0.02f, 0.02f, 0.02f, 0.8f);
	const FLinearColor HeroChosenColor(1.0f, 0.75f, 0.1f, 1.0f);
	const FLinearColor HeroButtonColor(0.3f, 0.3f, 0.3f, 1.0f);
	const FLinearColor HeroDimColor(0.75f, 0.75f, 0.75f);
}

void SHexHeroPanel::Construct(const FArguments& InArgs)
{
	OnHeroChosen = InArgs._OnHeroChosen;
	OnCastleChosen = InArgs._OnCastleChosen;
	OnHeroGo = InArgs._OnHeroGo;
	OnHeroArmy = InArgs._OnHeroArmy;
	OnHeroMagic = InArgs._OnHeroMagic;
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
	// what to do with a hero chosen and with a castle chosen
	const FText OrGo = InArgs._GoKeys.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("OrGo", " or press {0}"), FText::FromString(InArgs._GoKeys));
	const FText HeroHow = FText::Format(LOCTEXT("How", "Click a cell to show the way, click it again{0} to walk. Click a hero{1} to choose it, twice to look at it."), OrGo,
		NextHeroKeys.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("OrNext", " or press {0}"), FText::FromString(NextHeroKeys)));
	const FText CastleHow = FText::Format(LOCTEXT("CastleHow", "Click the castle again{0} to open it. Click a cell to show the way of the hero in it, click it again to send it out."), OrGo);

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
				// of the person who plays, whatever is chosen: with a castle chosen no hero is, and the bar was empty until 2026-11-11
				const UHexSites* Sites = Heroes.IsValid() ? Heroes->GetSites() : nullptr;
				if (!Sites)
				{
					return FText::GetEmpty();
				}
				const int32 Player = Heroes->GetShroudPlayer();
				const int32 Income = Sites->GetIncome(Player, Resource);
				return Income > 0 ? FText::Format(LOCTEXT("ResourceIncome", "{0} {1} (+{2})"), FText::FromString(Name), FText::AsNumber(Sites->GetAmount(Player, Resource)), FText::AsNumber(Income))
					: FText::Format(LOCTEXT("Resource", "{0} {1}"), FText::FromString(Name), FText::AsNumber(Sites->GetAmount(Player, Resource)));
			})
		];
	}

	// the economy, what the mines and castles give, as the developer asked on 2026-11-15
	ResourceRow->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(14.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SButton)
		.IsFocusable(false)
		.ContentPadding(FMargin(6.0f, 0.0f))
		.ButtonColorAndOpacity(HeroButtonColor)
		.ToolTipText(LOCTEXT("EconomyTip", "Economy: what you have, what your mines and castles give a day and a week"))
		.OnClicked_Lambda([OnEconomy = InArgs._OnEconomy]()
		{
			OnEconomy.ExecuteIfBound();
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Font(Small).Text(LOCTEXT("Economy", "Economy"))
		]
	];

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
			.Visibility_Lambda([this]() { return Heroes.IsValid() && Heroes->IsPlaying() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
			[
				ResourceRow
			]
		]
		// what to do with what is chosen, a hero or a castle, or what a click where the cursor is would do, at the bottom in the middle of the
		// screen, as the developer asked on 2026-11-15 in place of the texts in the list of heroes, which the tips at the cursor say
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(16.0f)
		[
			SNew(SBox)
			.MaxDesiredWidth(HintWidth)
			.Visibility_Lambda([this, HeroHow, CastleHow]()
			{
				return Heroes.IsValid() && Heroes->IsPlaying() && !GetHint(HeroHow, CastleHow).IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SBorder)
				.BorderImage(White)
				.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.35f))
				.Padding(FMargin(12.0f, 6.0f))
				[
					SNew(STextBlock)
					.Font(Small)
					.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.7f))
					.ShadowOffset(FVector2D(1.0, 1.0))
					.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
					.Justification(ETextJustify::Center)
					.WrapTextAt(HintWidth - 24.0f)
					.Text_Lambda([this, HeroHow, CastleHow]() { return GetHint(HeroHow, CastleHow); })
				]
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
					// nothing in it changes its height under the mouse but the list itself, as the developer asked on 2026-11-15 (texts under the list
					// pushed it up and down as the cursor came onto the panel, and the buttons ran away from it)
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
					// the game plays for the person while Auto is lit, and how fast the game runs
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SButton)
							.IsFocusable(false)
							.HAlign(HAlign_Center)
							.ContentPadding(FMargin(6.0f, 4.0f))
							.ButtonColorAndOpacity_Lambda([IsAuto = InArgs._IsAuto]() { return IsAuto.Get(false) ? HeroChosenColor : HeroButtonColor; })
							.ToolTipText(LOCTEXT("AutoTip", "The game plays for you, as it plays for the players it moves, while this is lit; you see what you saw, and a click gives you the game back"))
							.OnClicked_Lambda([OnAuto = InArgs._OnAuto]()
							{
								OnAuto.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Font(Font)
								.Text(LOCTEXT("Auto", "Auto"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(3.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(64.0f)
							[
								SNew(SButton)
								.IsFocusable(false)
								.HAlign(HAlign_Center)
								.ContentPadding(FMargin(6.0f, 4.0f))
								.ButtonColorAndOpacity(HeroButtonColor)
								.ToolTipText(LOCTEXT("SpeedTip", "How fast the game runs: 1x, 2x or 5x in turn"))
								.OnClicked_Lambda([OnSpeed = InArgs._OnSpeed]()
								{
									OnSpeed.ExecuteIfBound();
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Font(Font)
									.Text(InArgs._SpeedText)
								]
							]
						]
					]
				]
			]
		]
	];
}

int32 SHexHeroPanel::GetHoveredHero(FVector2D& OutAnchor) const
{
	for (const TPair<int32, TSharedPtr<SWidget>>& Each : HeroRows)
	{
		if (Each.Value.IsValid() && Each.Value->IsHovered())
		{
			// left of the row, level with its top
			const FGeometry& Geometry = Each.Value->GetTickSpaceGeometry();
			OutAnchor = Geometry.LocalToAbsolute(FVector2D(-8.0, 0.0));
			return Each.Key;
		}
	}
	return 0;
}

FText SHexHeroPanel::GetHint(const FText& HeroHow, const FText& CastleHow) const
{
	if (!Heroes.IsValid() || !Heroes->IsPersonTurn() || bHintHidden)
	{
		return FText::GetEmpty();
	}
	if (!HintOverride.IsEmpty())
	{
		return FText::FromString(HintOverride);
	}
	if (const FString& Hover = Heroes->GetHoverText(); !Hover.IsEmpty())
	{
		return FText::FromString(Hover);
	}
	return Heroes->GetSelectedCastleId() != 0 ? CastleHow : Heroes->GetSelected() ? HeroHow : FText::GetEmpty();
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
	const int32 CastleRevision = Heroes.IsValid() && Heroes->GetCastles() ? Heroes->GetCastles()->GetRevision() : -1;
	if (Revision != ListedRevision || CastleRevision != ListedCastles)
	{
		ListedRevision = Revision;
		ListedCastles = CastleRevision;
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

		// the castle chosen in place of a hero is marked over it
		const UHexCastles* Chosen = Heroes->GetCastles();
		const FHexCastle* ChosenCastle = Chosen ? Chosen->FindCastle(Heroes->GetSelectedCastleId()) : nullptr;
		FVector2D CastleScreen;
		if (ChosenCastle && ChosenCastle->bSeated && ChosenCastle->Layer == Heroes->GetPathLayer()
			&& OnProject(ChosenCastle->Seat.GetLocation() + ChosenCastle->Seat.GetLocation().GetSafeNormal() * (ChosenCastle->Seat.GetScale3D().X * 230.0), CastleScreen))
		{
			// with the hero that guards it, which is chosen with it
			const FHexHero* Guard = Heroes->GetSelected();
			const FString Text = Guard ? FString::Printf(TEXT("%s castle, %s"), *Chosen->GetConfig()->Factions[ChosenCastle->Faction].DisplayName, *Guard->Name)
				: FString::Printf(TEXT("%s castle"), *Chosen->GetConfig()->Factions[ChosenCastle->Faction].DisplayName);
			const FVector2D TextSize = Measure->Measure(Text, Font);
			const FVector2D BoxSize = TextSize + Margin * 2.0;
			const FVector2D Position = CastleScreen / Scale - FVector2D(BoxSize.X * 0.5, BoxSize.Y);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)), White, ESlateDrawEffect::None,
				FLinearColor(1.0f, 0.75f, 0.1f, 0.9f));
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Position + Margin)), Text, Font,
				ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.02f, 1.0f));
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
	HeroRows.Reset();
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
		// a button beside the hero, as the developer asked on 2026-11-15: lit while it does something, dim else, and it takes the click either
		// way, so nothing behind it gets it
		const auto SideButton = [](EHexStatIcon Icon, const TCHAR* Letter, const FLinearColor& Tint, TFunction<bool()> CanUse, TFunction<FText()> Tip,
			TFunction<void()> Use) -> TSharedRef<SWidget>
		{
			const FLinearColor Dim(Tint.R * 0.35f, Tint.G * 0.35f, Tint.B * 0.35f, 1.0f);
			return SNew(SButton)
				.IsFocusable(false)
				.ContentPadding(FMargin(3.0f, 2.0f))
				.ButtonColorAndOpacity_Lambda([CanUse, Tint, Dim]() { return CanUse() ? Tint : Dim; })
				.ToolTipText_Lambda([Tip]() { return Tip(); })
				.OnClicked_Lambda([CanUse, Use]()
				{
					if (CanUse())
					{
						Use();
					}
					return FReply::Handled();
				})
				[
					MakeButtonIcon(Icon, Letter, TAttribute<FSlateColor>::CreateLambda([CanUse]() { return FSlateColor(CanUse() ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.35f)); }))
				];
		};
		// a thin bar with its value written over it, as the developer asked on 2026-11-15
		const auto Bar = [](const FLinearColor& Fill, TFunction<float()> GetPercent, TFunction<FText()> GetText) -> TSharedRef<SWidget>
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(HeroDimColor)
					.Text_Lambda([GetText]() { return GetText(); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
					.BorderBackgroundColor(BarOutlineColor)
					.Padding(1.0f)
					[
						SNew(SBox)
						.HeightOverride(4.0f)
						[
							SNew(SProgressBar)
							.Style(&GetBarStyle())
							.FillColorAndOpacity(Fill)
							.Percent_Lambda([GetPercent]() { return TOptional<float>(GetPercent()); })
						]
					]
				];
		};
		const auto Find = [WeakHeroes, Id]() { return WeakHeroes.IsValid() ? WeakHeroes->FindHero(Id) : nullptr; };
		// its movement as a column beside its portrait, as the developer asked on 2026-11-15: full at its own movement of a day (with Logistics
		// and artifacts) or more, and the points above that, which only what it was given today brings, as see-through black pluses on it from
		// its bottom, one a point while they fit and else each a share of the most it had above its own today, going one by one as it walks;
		// below its own movement the column goes down, gone with the last point
		const auto OwnPoints = [WeakHeroes, Find]()
		{
			const FHexHero* Each = Find();
			return Each ? FMath::Max(WeakHeroes->GetMaxMovePoints(*Each), 1.0f) : 1.0f;
		};
		const auto Pluses = [this, WeakHeroes, Find, OwnPoints, Id]()
		{
			const FHexHero* Each = Find();
			if (!Each)
			{
				return 0;
			}
			const float Above = WeakHeroes->GetShownMovePoints(*Each) - OwnPoints();
			// the most it had above its own today
			FMovePeak& Peak = MovePeaks.FindOrAdd(Id);
			if (Peak.Day != WeakHeroes->GetDay())
			{
				Peak.Day = WeakHeroes->GetDay();
				Peak.Above = 0.0f;
			}
			Peak.Above = FMath::Max(Peak.Above, Above);
			if (Above <= UE_KINDA_SMALL_NUMBER)
			{
				return 0;
			}
			const float Shown = FMath::Min(float(MaxMovePluses), FMath::CeilToFloat(Peak.Above - UE_KINDA_SMALL_NUMBER));
			return FMath::Clamp(FMath::CeilToInt32(Shown * Above / Peak.Above - UE_KINDA_SMALL_NUMBER), 1, MaxMovePluses);
		};
		// each plus in a cell of its own, the cells filling the column, so it stands in the middle of its cell, as far from the one under it as
		// from the one over it; two bars, as the glyph of a font sits off the middle
		const TSharedRef<SVerticalBox> PlusColumn = SNew(SVerticalBox);
		for (int32 Plus = MaxMovePluses; Plus >= 1; --Plus)
		{
			PlusColumn->AddSlot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SOverlay)
				.Visibility_Lambda([Pluses, Plus]() { return Pluses() >= Plus ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(MovePlusSize).HeightOverride(MovePlusThickness)[SNew(SImage).Image(White).ColorAndOpacity(MovePlusColor)]
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(MovePlusThickness).HeightOverride(MovePlusSize)[SNew(SImage).Image(White).ColorAndOpacity(MovePlusColor)]
				]
			];
		}
		// inside a black outline a pixel wide
		const float MoveInside = MoveColumnHeight - 2.0f;
		const TSharedRef<SWidget> MoveColumn = SNew(SBox)
			.WidthOverride(MoveColumnWidth)
			.HeightOverride(MoveColumnHeight)
			[
				SNew(SBorder)
				.BorderImage(White)
				.BorderBackgroundColor(BarOutlineColor)
				.Padding(1.0f)
				[
				SNew(SOverlay)
				// its ground, gone with the last point
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(White)
					.ColorAndOpacity(FLinearColor(0.04f, 0.04f, 0.05f, 0.95f))
					.Visibility_Lambda([WeakHeroes, Find]() { const FHexHero* Each = Find(); return Each && WeakHeroes->GetShownMovePoints(*Each) > UE_KINDA_SMALL_NUMBER ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
				]
				// its fill from the bottom, by what is left of its own movement of a day
				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.HeightOverride_Lambda([WeakHeroes, Find, OwnPoints, MoveInside]()
					{
						const FHexHero* Each = Find();
						return FOptionalSize(Each ? MoveInside * FMath::Clamp(WeakHeroes->GetShownMovePoints(*Each) / OwnPoints(), 0.0f, 1.0f) : 0.0f);
					})
					[
						SNew(SImage)
						.Image(GetGradientBrush(true))
						.ColorAndOpacity(MoveColor)
					]
				]
				// and the pluses on it
				+ SOverlay::Slot()
				[
					PlusColumn
				]
				]
			];
		TSharedPtr<SButton> Row;
		List->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			// what the hero is shows beside the row as the panel at the cursor of the map does over the hero (GetHoveredHero)
			SAssignNew(Row, SButton)
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
				// its portrait in the color of its player, as the developer asked on 2026-11-15 in place of a strip of that color
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					HexUiBrushes::MakeFramed(Heroes->GetHeroPortrait(*Hero), 40.0f, Color)
				]
				// and its movement beside it
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(3.0f, 0.0f, 6.0f, 0.0f)
				[
					MoveColumn
				]
				// its name and how far it is on its way to the next level, its bar on the bottom of the row as that of its mana
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Fill)
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
					.FillHeight(1.0f)
					[
						SNew(SSpacer)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						Bar(ExperienceColor,
							[WeakHeroes, Find]()
							{
								const FHexHero* Each = Find();
								const UHexHeroConfig* HeroConfig = WeakHeroes.IsValid() ? WeakHeroes->GetConfig() : nullptr;
								if (!Each || !HeroConfig)
								{
									return 0.0f;
								}
								const int32 From = HeroConfig->GetLevelExperience(Each->Level);
								const int32 To = HeroConfig->GetLevelExperience(Each->Level + 1);
								return To > From ? FMath::Clamp(float(Each->Experience - From) / float(To - From), 0.0f, 1.0f) : 1.0f;
							},
							[WeakHeroes, Find]()
							{
								const FHexHero* Each = Find();
								const UHexHeroConfig* HeroConfig = WeakHeroes.IsValid() ? WeakHeroes->GetConfig() : nullptr;
								return Each && HeroConfig ? FText::Format(LOCTEXT("ExpBar", "Level {0}: {1} / {2}"), FText::AsNumber(Each->Level), ShortNumber(Each->Experience),
									ShortNumber(HeroConfig->GetLevelExperience(Each->Level + 1))) : FText::GetEmpty();
							})
					]
				]
				// its buttons, and its mana under them on the bottom of the row
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Fill)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						// the army of the hero, its skills and its spell book
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SideButton(EHexStatIcon::Count, TEXT("A"), FLinearColor(0.25f, 0.35f, 0.55f, 1.0f),
								[WeakHeroes]() { return WeakHeroes.IsValid() && WeakHeroes->IsPersonTurn(); },
								[]() { return LOCTEXT("ArmyTip", "Army: the army of the hero, its stats, skills, artifacts and spell book"); },
								[this, Id]() { OnHeroArmy.ExecuteIfBound(Id); })
						]
						// the spells it may cast on the planet, lit while it knows any
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f, 0.0f, 0.0f)
						[
							SideButton(EHexStatIcon::SpellPower, TEXT("M"), FLinearColor(0.45f, 0.25f, 0.6f, 1.0f),
								[WeakHeroes, Find]() { const FHexHero* Each = Find(); return Each && WeakHeroes->IsPersonTurn() && !WeakHeroes->GetAdventureSpells(*Each).IsEmpty(); },
								[WeakHeroes, Find]()
								{
									const FHexHero* Each = Find();
									return Each && !WeakHeroes->GetAdventureSpells(*Each).IsEmpty()
										? LOCTEXT("MagicTip", "Magic: the spells the hero may cast on the planet, to summon a boat, step through a dimension door, open a town portal, view the land or the depths")
										: LOCTEXT("MagicNone", "Magic: the hero knows no spell of the planet yet; a mage guild teaches them");
								},
								[this, Id]() { OnHeroMagic.ExecuteIfBound(Id); })
						]
						// Go, lit while it has a way it does not walk and the turn is the person's
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f, 0.0f, 0.0f)
						[
							SideButton(EHexStatIcon::Movement, TEXT(">"), FLinearColor(0.25f, 0.55f, 0.2f, 1.0f),
								[WeakHeroes, Find]()
								{
									const FHexHero* Each = Find();
									return Each && WeakHeroes->IsPlaying() && WeakHeroes->IsPersonTurn() && Each->TargetCell != INDEX_NONE && !Each->bWalking && Each->MovePoints > 0.0f;
								},
								[]() { return LOCTEXT("GoTip", "Go: walk the way shown (Space); a way is shown by a click on a cell"); },
								[this, Id]() { OnHeroGo.ExecuteIfBound(Id); })
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SSpacer)
					]
					// its mana, in red, as the developer asked on 2026-11-15
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						Bar(ManaColor,
							[WeakHeroes, Find]()
							{
								const FHexHero* Each = Find();
								return Each ? FMath::Clamp(float(Each->Mana) / float(FMath::Max(WeakHeroes->GetMaxMana(*Each), 1)), 0.0f, 1.0f) : 0.0f;
							},
							[WeakHeroes, Find]()
							{
								const FHexHero* Each = Find();
								return Each ? FText::Format(LOCTEXT("ManaBar", "Mana {0} / {1}"), ShortNumber(Each->Mana), ShortNumber(WeakHeroes->GetMaxMana(*Each)))
									: FText::GetEmpty();
							})
					]
				]
			]
		];
		HeroRows.Add(Id, Row);
	}

	// the castles of the person who plays, chosen as a hero is: either a hero or a castle is chosen
	const UHexCastles* Castles = Heroes->IsPlaying() ? Heroes->GetCastles() : nullptr;
	for (int32 Index = 0; Castles && Index < Castles->GetCastles().Num(); ++Index)
	{
		const FHexCastle& Castle = Castles->GetCastles()[Index];
		if (Castle.Owner != Heroes->GetShroudPlayer())
		{
			continue;
		}
		const int32 Id = Castle.Id;
		const FHexHero* Guard = Castles->FindGuard(Castle);
		const FLinearColor Color = Config->GetPlayer(Castle.Owner).Color;
		List->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(FMargin(6.0f, 4.0f))
			.ToolTip(SNew(SToolTip)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					HexUiBrushes::MakeFramed(Castles->GetCastlePicture(Castle), 72.0f, Color, Castles->GetCastleTint(Castle))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.WrapTextAt(300.0f)
					.Text(FText::Format(LOCTEXT("CastleLineTip", "{0}, {1}\nGarrison: {2}{3}\n\nClick to choose the castle and look at it.\nClick again, or press the key that sends a hero, to open it."),
						FText::FromString(Castles->GetCastleName(Castle)), FText::FromString(Castles->GetConfig()->Factions[Castle.Faction].DisplayName),
						FText::FromString(Castles->DescribeArmy(Castle.Garrison)),
						Guard ? FText::Format(LOCTEXT("CastleGuardTip", "\n{0} guards it"), FText::FromString(Guard->Name)) : FText::GetEmpty()))
				]
			])
			.ButtonColorAndOpacity_Lambda([WeakHeroes, Id]() { return WeakHeroes.IsValid() && WeakHeroes->GetSelectedCastleId() == Id ? HeroChosenColor : HeroButtonColor; })
			.OnClicked_Lambda([this, Id]()
			{
				OnCastleChosen.ExecuteIfBound(Id);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				// the picture of its fort in the tint of its faction, framed in the color of its owner, as the developer asked on 2026-11-15
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 7.0f, 0.0f)
				[
					HexUiBrushes::MakeFramed(Castles->GetCastlePicture(Castle), 34.0f, Color, Castles->GetCastleTint(Castle))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Font)
						.Text(FText::Format(LOCTEXT("CastleLine", "{0}, {1}"), FText::FromString(Castles->GetCastleName(Castle)),
							FText::FromString(Castles->GetConfig()->Factions[Castle.Faction].DisplayName)))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Small)
						.ColorAndOpacity(HeroDimColor)
						.Text(Guard ? FText::Format(LOCTEXT("CastleGuard", "{0} guards it"), FText::FromString(Guard->Name))
							: FText::Format(LOCTEXT("CastleGarrison", "Garrison: {0}"), FText::FromString(Castles->DescribeArmy(Castle.Garrison))))
					]
				]
				// a hammer while it can build something this turn, with what it has, as the developer asked on 2026-11-15
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(20.0f)
					.HeightOverride(20.0f)
					.ToolTipText(LOCTEXT("CastleCanBuild", "Something can be built here this turn"))
					.Visibility_Lambda([WeakHeroes, Id]()
					{
						const UHexCastles* Each = WeakHeroes.IsValid() ? WeakHeroes->GetCastles() : nullptr;
						const FHexCastle* Found = Each ? Each->FindCastle(Id) : nullptr;
						return Found && WeakHeroes->IsPersonTurn() && Each->CanBuildAny(*Found) ? EVisibility::Visible : EVisibility::Hidden;
					})
					[
						MakeButtonIcon(EHexStatIcon::Build, TEXT("B"), FSlateColor(FLinearColor(1.0f, 0.85f, 0.4f, 1.0f)))
					]
				]
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE
