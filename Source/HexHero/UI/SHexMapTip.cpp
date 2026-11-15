// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexMapTip.h"
#include "HexBattleConfig.h"
#include "HexCastleConfig.h"
#include "HexUnitTooltip.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "SHexSpellBook.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SToolTip.h"

namespace
{
	const FVector2D TipCursorOffset(22.0, 22.0);
	constexpr float TipWrap = 260.0f;
	constexpr float TipPortrait = 30.0f;
}

void SHexMapTip::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	[
		SAssignNew(Panel, SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.9f))
		.Padding(FMargin(10.0f, 7.0f))
		.Visibility(EVisibility::Collapsed)
		[
			SAssignNew(Body, SVerticalBox)
		]
	];
}

void SHexMapTip::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const FVector2D Cursor = AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	const FVector2D Size = Panel->GetDesiredSize();
	const FVector2D Available = AllottedGeometry.GetLocalSize();
	// beside a row of the list of heroes: its right edge at the place given, its top level with it, kept on the screen
	if (Anchor.IsSet())
	{
		const FVector2D Place = AllottedGeometry.AbsoluteToLocal(Anchor.GetValue());
		Panel->SetRenderTransform(FSlateRenderTransform(FVector2D(FMath::Clamp(Place.X - Size.X, 0.0, FMath::Max(Available.X - Size.X, 0.0)),
			FMath::Clamp(Place.Y, 0.0, FMath::Max(Available.Y - Size.Y, 0.0)))));
		return;
	}
	// near the right or bottom edge it goes to the other side of the cursor
	const FVector2D Position(
		Cursor.X + TipCursorOffset.X + Size.X > Available.X ? Cursor.X - TipCursorOffset.X - Size.X : Cursor.X + TipCursorOffset.X,
		Cursor.Y + TipCursorOffset.Y + Size.Y > Available.Y ? Cursor.Y - TipCursorOffset.Y - Size.Y : Cursor.Y + TipCursorOffset.Y);
	Panel->SetRenderTransform(FSlateRenderTransform(Position));
}

const FSlateBrush* SHexMapTip::GetBrush(UTexture2D* Texture)
{
	if (!Texture)
	{
		return nullptr;
	}
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Texture))
	{
		return Found->Get();
	}
	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(TipPortrait, TipPortrait);
	Textures.Emplace(Texture);
	Brushes.Add(Texture, Brush);
	return Brush.Get();
}

void SHexMapTip::SetTip(const FHexMapTip& Tip)
{
	if (bShown && Tip == Shown)
	{
		return;
	}
	Shown = Tip;
	bShown = true;
	Body->ClearChildren();
	if (Tip.Title.IsEmpty())
	{
		Panel->SetVisibility(EVisibility::Collapsed);
		return;
	}
	Panel->SetVisibility(EVisibility::HitTestInvisible);
	Body->AddSlot().AutoHeight()[MakeContent(Tip)];
}

TSharedRef<SVerticalBox> SHexMapTip::MakeContent(const FHexMapTip& Tip)
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FLinearColor Text(0.88f, 0.88f, 0.9f, 1.0f);
	const FLinearColor Dim(0.62f, 0.63f, 0.68f, 1.0f);
	// the portrait of a hero or the picture of a castle beside its name, as the developer asked on 2026-11-15
	if (Tip.Picture)
	{
		Content->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				HexUiBrushes::MakeFramed(Tip.Picture, 48.0f, Tip.TitleColor, Tip.PictureTint)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Title).ColorAndOpacity(Tip.TitleColor).Text(FText::FromString(Tip.Title)).WrapTextAt(TipWrap - 56.0f)
			]
		];
	}
	else
	{
		Content->AddSlot().AutoHeight()[SNew(STextBlock).Font(Title).ColorAndOpacity(Tip.TitleColor).Text(FText::FromString(Tip.Title)).WrapTextAt(TipWrap)];
	}
	for (const FString& Line : Tip.Lines)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)[SNew(STextBlock).Font(Font).ColorAndOpacity(Text).Text(FText::FromString(Line)).WrapTextAt(TipWrap)];
	}
	// the attributes of a hero, each beside its icon
	if (!Tip.Stats.IsEmpty())
	{
		const TSharedRef<SWrapBox> Stats = SNew(SWrapBox).PreferredSize(TipWrap).InnerSlotPadding(FVector2D(8.0, 3.0));
		for (const FHexMapTipStat& Stat : Tip.Stats)
		{
			const FSlateBrush* Icon = HexUnitTooltip::GetIcon(Stat.Icon);
			Stats->AddSlot()
			[
				SNew(SHorizontalBox).ToolTipText(FText::FromString(Stat.Name))
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(20.0f).HeightOverride(20.0f)
					[
						Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon)) : StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(Font).ColorAndOpacity(Dim).Text(FText::FromString(Stat.Name.Left(2))))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(Text).Text(FText::FromString(Stat.Value))
				]
			];
		}
		Content->AddSlot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)[Stats];
	}
	// its skills as their pictures with their levels
	if (!Tip.Skills.IsEmpty())
	{
		const UHexHeroConfig* HeroConfig = UHexHeroConfig::Load();
		const TSharedRef<SHorizontalBox> Skills = SNew(SHorizontalBox);
		for (const FHexHeroSkill& Skill : Tip.Skills)
		{
			const FHexSkillInfo* Info = HeroConfig->FindSkill(Skill.Skill);
			const FSlateBrush* Icon = Info ? HexUiBrushes::Get(Info->Icon.LoadSynchronous(), 28.0f) : nullptr;
			Skills->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(28.0f).HeightOverride(28.0f)
					[
						Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon)) : StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(Font).ColorAndOpacity(Dim)
							.Text(FText::FromString(Info ? Info->DisplayName.Left(3) : FString(TEXT("?")))))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 7)).ColorAndOpacity(Dim).Text(FText::FromString(FString(GetSkillLevelName(Skill.Level)).Left(3)))
				]
			];
		}
		Content->AddSlot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)[Skills];
	}
	// the army as portraits with their numbers, or with words where the number is not known
	if (!Tip.Army.IsEmpty())
	{
		const UHexCastleConfig* Castles = UHexCastleConfig::Load();
		const UHexBattleConfig* Battle = UHexBattleConfig::Load();
		const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 Index = 0; Index < Tip.Army.Num(); ++Index)
		{
			const FHexStack& Stack = Tip.Army[Index];
			const FHexCreature* Creature = Castles->Factions.IsValidIndex(Stack.Faction) && Castles->Factions[Stack.Faction].Creatures.IsValidIndex(Stack.Creature)
				? &Castles->Factions[Stack.Faction].Creatures[Stack.Creature] : nullptr;
			const FHexUnitLook* Look = Creature ? Battle->FindLook(Creature->Id) : nullptr;
			const FSlateBrush* Brush = Look ? HexUiBrushes::Get(Look->Portrait.LoadSynchronous(), TipPortrait) : nullptr;
			const FLinearColor Tint = Look ? (Stack.bUpgraded ? Look->UpgradedTint : Look->Tint) : FLinearColor::White;
			const FString Count = Tip.ArmyCounts.IsValidIndex(Index) ? Tip.ArmyCounts[Index] : FString::FromInt(Stack.Count);
			Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(TipPortrait).HeightOverride(TipPortrait)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"))).BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.09f, 1.0f)).Padding(0.0f)
							[
								Brush ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush).ColorAndOpacity(FLinearColor(Tint.R, Tint.G, Tint.B, 1.0f)))
									: StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(Font).ColorAndOpacity(Dim).Text(FText::AsNumber(Stack.Creature + 1)))
							]
						]
						// its level and chevrons in the top left corner
						+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
						[
							SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"))).BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f)).Padding(FMargin(1.0f, 0.0f))
							[
								HexUnitTooltip::MakeRankBadge(Stack.Creature + 1, Stack.bUpgraded ? 2 : 1, 7.0f, FLinearColor(0.98f, 0.82f, 0.35f, 1.0f))
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(Text).Text(FText::FromString(Count))
				]
			];
		}
		Content->AddSlot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)[Row];
	}
	if (!Tip.Forecast.IsEmpty())
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)).ColorAndOpacity(Tip.ForecastColor).Text(FText::FromString(Tip.Forecast)).WrapTextAt(TipWrap)
		];
	}
	return Content;
}

TSharedRef<SToolTip> SHexMapTip::MakeToolTip(const FHexMapTip& Tip)
{
	// the same panel as at the cursor on the map, for a button that names a hero or what a castle offers, as the developer asked on 2026-11-15
	return SNew(SToolTip)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBorder")))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f))
			.Padding(FMargin(10.0f, 7.0f))
			[
				MakeContent(Tip)
			]
		];
}
