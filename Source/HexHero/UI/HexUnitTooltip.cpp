// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexUnitTooltip.h"
#include "HexCastleConfig.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FSlateFontInfo TipFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	/** The brushes of the frames of the icons, made once, and how fast each plays; their pictures are kept from the collector, as nothing
	 *  else holds them */
	struct FIconFrames
	{
		TArray<TSharedPtr<FSlateBrush>> Frames;
		float Rate = 8.0f;
	};

	TArray<FIconFrames>& GetBrushes()
	{
		static TArray<FIconFrames> Brushes;
		if (Brushes.IsEmpty())
		{
			const UHexUiConfig* Ui = UHexUiConfig::Load();
			Brushes.SetNum(int32(EHexStatIcon::Num));
			for (int32 Index = 0; Index < int32(EHexStatIcon::Num); ++Index)
			{
				const FHexStatIconLook* Look = Ui->FindIconLook(EHexStatIcon(Index));
				if (UTexture2D* Texture = Look ? Look->Texture.LoadSynchronous() : nullptr)
				{
					Texture->AddToRoot();
					UHexUiConfig::MakeFrameBrushes(Texture, Look->Frames, Ui->StatIconSize, Brushes[Index].Frames);
					Brushes[Index].Rate = Look->FramesPerSecond;
				}
			}
		}
		return Brushes;
	}

	/** An icon, its first frame, as the icons of the panels of creatures stand still, as the developer asked on 2026-11-14; with bAnimated
	 *  it plays its frames */
	TSharedRef<SWidget> MakeIcon(EHexStatIcon Icon, float Size, bool bAnimated = false)
	{
		const int32 Index = int32(Icon);
		if (!GetBrushes().IsValidIndex(Index) || GetBrushes()[Index].Frames.IsEmpty())
		{
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size);
		}
		if (!bAnimated)
		{
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[SNew(SImage).Image(GetBrushes()[Index].Frames[0].Get())];
		}
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SImage).Image_Lambda([Index]()
			{
				const FIconFrames& Each = GetBrushes()[Index];
				return Each.Frames[UHexUiConfig::GetFrame(Each.Frames.Num(), Each.Rate)].Get();
			})
		];
	}
}

TSharedRef<SWidget> HexUnitTooltip::MakeAnimatedIcon(EHexStatIcon Icon, float Size)
{
	return MakeIcon(Icon, Size, true);
}

const FSlateBrush* HexUnitTooltip::GetIcon(EHexStatIcon Icon)
{
	const TArray<FIconFrames>& Brushes = GetBrushes();
	const int32 Index = int32(Icon);
	return Brushes.IsValidIndex(Index) && !Brushes[Index].Frames.IsEmpty() ? Brushes[Index].Frames[0].Get() : nullptr;
}

FString HexUnitTooltip::DescribeAbilities(const FHexCreatureStats& Stats)
{
	FString Can;
	const UEnum* Abilities = StaticEnum<EHexAbility>();
	for (int32 Index = 0; Abilities && Index < Abilities->NumEnums() - 1; ++Index)
	{
		if (Stats.Abilities & (1 << Index))
		{
			Can += (Can.IsEmpty() ? TEXT("") : TEXT(", ")) + Abilities->GetDisplayNameTextByIndex(Index).ToString();
		}
	}
	return Can;
}

void HexUnitTooltip::SetRank(FHexUnitTooltipData& Data, int32 Creature, bool bUpgraded)
{
	Data.RankLevel = Creature + 1;
	Data.RankTier = bUpgraded ? 2 : 1;
}

namespace
{
	/** Chevrons pointing up, one close over another, as the stripes of a rank */
	class SHexChevrons : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SHexChevrons) {}
			SLATE_ARGUMENT(int32, Count)
			SLATE_ARGUMENT(float, Size)
			SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Count = FMath::Clamp(InArgs._Count, 1, 3);
			Size = InArgs._Size;
			Color = InArgs._Color;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(Size, Size * (0.45f + 0.32f * float(Count)));
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2D Area = AllottedGeometry.GetLocalSize();
			const float Width = Size * 0.18f;
			const float Step = Size * 0.32f;
			const float Bottom = Area.Y - Width;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				// from the lowest up, each a wide V turned over
				const float Y = Bottom - Step * float(Index);
				TArray<FVector2D> Points = {FVector2D(Width, Y), FVector2D(Area.X * 0.5f, Y - Size * 0.4f), FVector2D(Area.X - Width, Y)};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Width);
			}
			return LayerId;
		}

	private:
		int32 Count = 1;
		float Size = 12.0f;
		FLinearColor Color = FLinearColor::White;
	};
}

TSharedRef<SWidget> HexUnitTooltip::MakeRankBadge(int32 Level, int32 Tier, float Size, const FLinearColor& Color)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt32(Size))).ColorAndOpacity(Color).Text(FText::AsNumber(Level))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SHexChevrons).Count(Tier).Size(Size).Color(Color)
		];
}

FHexUnitTooltipData HexUnitTooltip::FromStats(const FHexCreatureStats& Stats)
{
	FHexUnitTooltipData Data;
	Data.Title = Stats.DisplayName;
	Data.Attack = Stats.Attack;
	Data.Defense = Stats.Defense;
	Data.DamageMin = Stats.DamageMin;
	Data.DamageMax = Stats.DamageMax;
	Data.Health = Stats.Health;
	Data.Speed = Stats.Speed;
	Data.Shots = Stats.Shots > 0 ? Stats.Shots : -1;
	Data.Morale = Stats.Morale;
	Data.Luck = Stats.Luck;
	Data.Abilities = DescribeAbilities(Stats);
	return Data;
}

TSharedRef<SWidget> HexUnitTooltip::Make(const FHexUnitTooltipData& Data)
{
	const float Size = UHexUiConfig::Load()->StatIconSize;
	// a stat: its icon, its name and its number
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
	int32 Cell = 0;
	const auto AddStat = [&](EHexStatIcon Icon, const TCHAR* Name, const FString& Value, int32 Bonus = 0)
	{
		const int32 Column = (Cell % 2) * 4;
		const int32 Row = Cell / 2;
		++Cell;
		Grid->AddSlot(Column, Row).Padding(0.0f, 1.0f, 4.0f, 1.0f).VAlign(VAlign_Center)
		[
			MakeIcon(Icon, Size)
		];
		Grid->AddSlot(Column + 1, Row).Padding(0.0f, 1.0f, 8.0f, 1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(TipFont(9)).ColorAndOpacity(FLinearColor(0.72f, 0.74f, 0.8f, 1.0f)).Text(FText::FromString(Name))
		];
		// its own value, and what its hero and the spells on it add in green or take in red
		Grid->AddSlot(Column + 2, Row).Padding(0.0f, 1.0f, 18.0f, 1.0f).VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(TipFont(11, true)).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(Value))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(TipFont(11, true)).Visibility(Bonus != 0 ? EVisibility::Visible : EVisibility::Collapsed)
				.ColorAndOpacity(Bonus > 0 ? FLinearColor(0.3f, 0.95f, 0.35f, 1.0f) : FLinearColor(1.0f, 0.3f, 0.25f, 1.0f))
				.Text(FText::FromString(FString::Printf(TEXT("%+d"), Bonus)))
			]
		];
	};
	AddStat(EHexStatIcon::Attack, TEXT("Attack"), FString::FromInt(Data.Attack), Data.AttackBonus);
	AddStat(EHexStatIcon::Defense, TEXT("Defense"), FString::FromInt(Data.Defense), Data.DefenseBonus);
	AddStat(EHexStatIcon::Damage, TEXT("Damage"), Data.DamageMin == Data.DamageMax ? FString::FromInt(Data.DamageMin) : FString::Printf(TEXT("%d-%d"), Data.DamageMin, Data.DamageMax));
	AddStat(EHexStatIcon::Health, TEXT("Health"), Data.TopHealth >= 0 && Data.TopHealth < Data.Health ? FString::Printf(TEXT("%d of %d"), Data.TopHealth, Data.Health) : FString::FromInt(Data.Health));
	AddStat(EHexStatIcon::Speed, TEXT("Speed"), FString::FromInt(Data.Speed), Data.SpeedBonus);
	AddStat(EHexStatIcon::Morale, TEXT("Morale"), FString::Printf(TEXT("%+d"), Data.Morale));
	AddStat(EHexStatIcon::Luck, TEXT("Luck"), FString::Printf(TEXT("%+d"), Data.Luck));

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(TipFont(12, true)).ColorAndOpacity(Data.TitleColor).Text(FText::FromString(Data.Title))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Data.RankLevel > 0 ? 8.0f : 0.0f, 0.0f, 0.0f, 0.0f)
			[
				Data.RankLevel > 0 ? MakeRankBadge(Data.RankLevel, Data.RankTier, 11.0f, FLinearColor(0.95f, 0.8f, 0.35f, 1.0f)) : StaticCastSharedRef<SWidget>(SNew(SBox))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(TipFont(9)).ColorAndOpacity(FLinearColor(0.7f, 0.72f, 0.78f, 1.0f)).Text(FText::FromString(Data.Subtitle))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)[Grid];

	// what kind of fighter it is: it flies, it shoots and how many shots it has, each with its icon
	TSharedRef<SHorizontalBox> Kinds = SNew(SHorizontalBox);
	const auto AddKind = [&](EHexStatIcon Icon, const FString& Text)
	{
		if (GetIcon(Icon))
		{
			Kinds->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)[MakeIcon(Icon, Size)];
		}
		Kinds->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f)
		[
			SNew(STextBlock).Font(TipFont(10, true)).ColorAndOpacity(FLinearColor(0.75f, 0.95f, 1.0f, 1.0f)).Text(FText::FromString(Text))
		];
	};
	if (Data.bFlies)
	{
		AddKind(EHexStatIcon::Flies, TEXT("Flies"));
	}
	if (Data.Shots >= 0)
	{
		AddKind(EHexStatIcon::Shots, FString::Printf(TEXT("Shoots, %d %s"), Data.Shots, Data.Shots == 1 ? TEXT("shot") : TEXT("shots")));
	}
	else
	{
		AddKind(EHexStatIcon::Attack, TEXT("Fights in melee"));
	}
	Content->AddSlot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)[Kinds];
	const auto AddLine = [&](const FString& Text, const FLinearColor& Color)
	{
		if (!Text.IsEmpty())
		{
			Content->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(SBox).MaxDesiredWidth(420.0f)[SNew(STextBlock).Font(TipFont(9)).ColorAndOpacity(Color).AutoWrapText(true).Text(FText::FromString(Text))]
			];
		}
	};
	AddLine(Data.Abilities, FLinearColor(1.0f, 0.88f, 0.6f, 1.0f));
	AddLine(Data.Effects, FLinearColor(0.6f, 0.9f, 1.0f, 1.0f));
	AddLine(Data.Extra, FLinearColor(0.82f, 0.82f, 0.82f, 1.0f));
	return Content;
}
