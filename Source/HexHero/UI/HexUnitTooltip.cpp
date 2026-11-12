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

	/** An icon that plays its frames */
	TSharedRef<SWidget> MakeIcon(EHexStatIcon Icon, float Size)
	{
		const int32 Index = int32(Icon);
		if (!GetBrushes().IsValidIndex(Index) || GetBrushes()[Index].Frames.IsEmpty())
		{
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size);
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
	const auto AddStat = [&](EHexStatIcon Icon, const TCHAR* Name, const FString& Value)
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
		Grid->AddSlot(Column + 2, Row).Padding(0.0f, 1.0f, 18.0f, 1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(TipFont(11, true)).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(Value))
		];
	};
	AddStat(EHexStatIcon::Attack, TEXT("Attack"), FString::FromInt(Data.Attack));
	AddStat(EHexStatIcon::Defense, TEXT("Defense"), FString::FromInt(Data.Defense));
	AddStat(EHexStatIcon::Damage, TEXT("Damage"), Data.DamageMin == Data.DamageMax ? FString::FromInt(Data.DamageMin) : FString::Printf(TEXT("%d-%d"), Data.DamageMin, Data.DamageMax));
	AddStat(EHexStatIcon::Health, TEXT("Health"), Data.TopHealth >= 0 && Data.TopHealth < Data.Health ? FString::Printf(TEXT("%d of %d"), Data.TopHealth, Data.Health) : FString::FromInt(Data.Health));
	AddStat(EHexStatIcon::Speed, TEXT("Speed"), FString::FromInt(Data.Speed));
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
