// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexCastlePanel.h"
#include "HexCastles.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexCastlePanel"

namespace
{
	const FLinearColor CastleBackColor(0.015f, 0.015f, 0.02f, 0.88f);
	const FLinearColor CastleHeadColor(0.95f, 0.85f, 0.55f, 1.0f);
	const FLinearColor CastleTextColor(0.9f, 0.9f, 0.9f, 1.0f);
	const FLinearColor CastleDimColor(0.6f, 0.6f, 0.62f, 1.0f);
	const FLinearColor CastleButtonColor(0.1f, 0.1f, 0.12f, 1.0f);
	// room for the panel of the heroes under it
	constexpr float CastlePanelBottom = 40.0f;
}

void SHexCastlePanel::Construct(const FArguments& InArgs)
{
	OnMessage = InArgs._OnMessage;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const float Width = UHexCastleConfig::Load()->PanelWidth;
	const float Top = UHexCastleConfig::Load()->PanelTop;
	SetVisibility(EVisibility::Collapsed);
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(TAttribute<FMargin>::CreateLambda([this, Top]() { return FMargin(PanelLeft, Top, 0.0f, CastlePanelBottom); }))
	[
		SAssignNew(Frame, SBox)
		.WidthOverride(Width)
		.MaxDesiredHeight_Lambda([this]() { return FOptionalSize(MaxHeight); })
		[
			SNew(SBorder)
			.BorderImage(White)
			.BorderBackgroundColor(CastleBackColor)
			.Padding(FMargin(10.0f, 8.0f))
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(Body, SVerticalBox)
				]
			]
		]
	];
}

void SHexCastlePanel::SetHeroes(UHexHeroes* InHeroes)
{
	if (Heroes.Get() != InHeroes)
	{
		Heroes = InHeroes;
		Close();
	}
}

void SHexCastlePanel::Open(int32 CastleId)
{
	OpenId = CastleId;
	ShownRevision = -1;
	bHireToHero = true;
	// the frame takes the mouse, so the wheel scrolls it, and what lies around it lets the mouse through
	SetVisibility(CastleId != 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

void SHexCastlePanel::Close()
{
	Open(0);
}

bool SHexCastlePanel::IsUnderCursor() const
{
	return IsOpen() && Frame.IsValid() && Frame->GetTickSpaceGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
}

void SHexCastlePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!IsOpen())
	{
		return;
	}
	const UHexCastleConfig* PanelConfig = UHexCastleConfig::Load();
	MaxHeight = FMath::Max(float(AllottedGeometry.GetLocalSize().Y) - PanelConfig->PanelTop - CastlePanelBottom, 160.0f);
	PanelLeft = FMath::Max(float(AllottedGeometry.GetLocalSize().X) * PanelConfig->PanelCenterX - PanelConfig->PanelWidth * 0.5f, 8.0f);
	// only while the person whose turn it is owns the castle
	const UHexCastles* Castles = Heroes.IsValid() ? Heroes->GetCastles() : nullptr;
	const FHexCastle* Castle = Castles ? Castles->FindCastle(OpenId) : nullptr;
	if (!Castle || !Heroes->IsPlaying() || !Heroes->IsPersonTurn() || Castle->Owner != Heroes->GetShroudPlayer())
	{
		Close();
		return;
	}
	// what the player has decides which buttons are lit
	uint32 Stock = 0;
	const UHexSites* Sites = Heroes->GetSites();
	for (int32 Resource = 0; Sites && Resource < Sites->GetConfig()->Resources.Num(); ++Resource)
	{
		Stock = HashCombine(Stock, GetTypeHash(Sites->GetAmount(Castle->Owner, Resource)));
	}
	if (ShownRevision != Castles->GetRevision() || ShownStock != Stock)
	{
		ShownRevision = Castles->GetRevision();
		ShownStock = Stock;
		RebuildBody();
	}
}

void SHexCastlePanel::RebuildBody()
{
	Body->ClearChildren();
	UHexCastles* Castles = Heroes.IsValid() ? Heroes->GetCastles() : nullptr;
	const FHexCastle* Castle = Castles ? Castles->FindCastle(OpenId) : nullptr;
	const UHexSites* Sites = Heroes.IsValid() ? Heroes->GetSites() : nullptr;
	if (!Castle || !Sites)
	{
		return;
	}
	const UHexCastleConfig* Config = Castles->GetConfig();
	const FHexFaction& Faction = Config->Factions[Castle->Faction];
	const FSlateFontInfo Head = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo Section = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const int32 CastleId = Castle->Id;
	TWeakObjectPtr<UHexHeroes> WeakHeroes = Heroes;
	const TFunction<void(const FString&)> Tell = OnMessage;

	const auto MakeText = [](const FString& Text, const FSlateFontInfo& InFont, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(InFont).ColorAndOpacity(Color).AutoWrapText(true);
	};
	// a button that does something to the castle and says what came of it
	const auto MakeButton = [&](const FString& Label, bool bEnabled, const FString& Tip, TFunction<void(UHexCastles&, FString&)> Do)
	{
		return SNew(SButton)
			.IsFocusable(false)
			.IsEnabled(bEnabled)
			.ContentPadding(FMargin(7.0f, 2.0f))
			.ButtonColorAndOpacity(CastleButtonColor)
			.ToolTipText(FText::FromString(Tip))
			.OnClicked_Lambda([WeakHeroes, Tell, Do]()
			{
				if (UHexCastles* Each = WeakHeroes.IsValid() ? WeakHeroes->GetCastles() : nullptr)
				{
					FString Message;
					Do(*Each, Message);
					if (Tell && !Message.IsEmpty())
					{
						Tell(Message);
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(Small).ColorAndOpacity(CastleTextColor)
			];
	};
	const auto AddSection = [&](const FString& Title)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 9.0f, 0.0f, 3.0f)[MakeText(Title, Section, CastleHeadColor)];
	};
	// a line with what it says at the left and its buttons at the right
	const auto AddRow = [&](const TSharedRef<SWidget>& Left, const TSharedRef<SWidget>& Right)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[Left]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Right]
		];
	};

	// the castle and its owner
	const UHexHeroConfig* HeroConfig = Heroes->GetConfig();
	const FString Owner = HeroConfig->Players.IsValidIndex(Castle->Owner) ? HeroConfig->Players[Castle->Owner].Name : FString(TEXT("nobody"));
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[MakeText(FString::Printf(TEXT("%s, of %s"), *Faction.DisplayName, *Owner), Head, CastleHeadColor)]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(7.0f, 1.0f)).ButtonColorAndOpacity(CastleButtonColor)
			.OnClicked_Lambda([this]() { Close(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("Close", "x")).Font(Font).ColorAndOpacity(CastleTextColor)
			]
		]
	];
	const int32 HallIndex = Faction.FindBuilding(EHexBuildingType::Hall);
	const int32 HallLevel = Castles->GetLevel(*Castle, EHexBuildingType::Hall);
	const int32 Gold = Faction.Buildings.IsValidIndex(HallIndex) && Faction.Buildings[HallIndex].Levels.IsValidIndex(HallLevel - 1) ? Faction.Buildings[HallIndex].Levels[HallLevel - 1].GoldPerDay : 0;
	Body->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
	[
		MakeText(FString::Printf(TEXT("%d gold a day. %s"), Gold, Castles->HasUnlimitedBuilds() ? TEXT("Builds without a limit (hex.Cheat build).")
			: Castle->BuiltToday >= Config->BuildsPerDay ? TEXT("Built today already: the next building tomorrow.") : TEXT("May build today.")),
			Small, CastleDimColor)
	];

	// the buildings: what stands, and what the next level of each costs
	AddSection(TEXT("Buildings"));
	for (int32 Building = 0; Building < Faction.Buildings.Num() && Building < Castle->Levels.Num(); ++Building)
	{
		const FHexBuilding& Each = Faction.Buildings[Building];
		const int32 Level = Castle->Levels[Building];
		const FString Now = Level > 0 ? Each.Levels[Level - 1].DisplayName : FString();
		if (!Each.Levels.IsValidIndex(Level))
		{
			AddRow(MakeText(Now, Font, CastleTextColor), MakeText(TEXT("built"), Small, CastleDimColor));
			continue;
		}
		const FHexBuildingLevel& Next = Each.Levels[Level];
		FString Why;
		const bool bCan = Castles->CanBuild(*Castle, Building, &Why);
		FString What = Next.DisplayName;
		if (Each.Type == EHexBuildingType::Dwelling && Faction.Creatures.IsValidIndex(Each.Creature))
		{
			What += FString::Printf(TEXT(" (%s)"), *Faction.Creatures[Each.Creature].GetStats(Level >= 1).DisplayName.ToLower());
		}
		else if (Each.Type == EHexBuildingType::Hall)
		{
			What += FString::Printf(TEXT(" (%d gold a day)"), Next.GoldPerDay);
		}
		const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakeText(Level > 0 ? FString::Printf(TEXT("%s > %s"), *Now, *What) : What, Font, bCan ? CastleTextColor : CastleDimColor)]
			+ SVerticalBox::Slot().AutoHeight()[MakeText(Sites->DescribeCost(Next.Cost) + (bCan ? TEXT("") : *FString::Printf(TEXT(": %s"), *Why)), Small, CastleDimColor)];
		AddRow(Left, MakeButton(Level > 0 ? TEXT("Upgrade") : TEXT("Build"), bCan, Why, [CastleId, Building](UHexCastles& Do, FString& Message) { Do.Build(CastleId, Building, Message); }));
	}

	// the hero that guards the castle, else one of the owner beside it, may take what is hired, and swap stacks with the garrison
	const FHexHero* Visitor = Castles->FindVisitor(*Castle);
	const int32 VisitorId = Visitor ? Visitor->Id : 0;
	const bool bToHero = Visitor && bHireToHero;
	AddSection(TEXT("Hire"));
	if (Visitor)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[MakeText(TEXT("Into:"), Small, CastleDimColor)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
			[
				SNew(SButton).IsFocusable(false).ContentPadding(FMargin(7.0f, 2.0f)).ButtonColorAndOpacity(bToHero ? CastleHeadColor * 0.35f : CastleButtonColor)
				.OnClicked_Lambda([this]() { bHireToHero = true; ShownRevision = -1; return FReply::Handled(); })
				[
					SNew(STextBlock).Text(FText::FromString(Visitor->Name)).Font(Small).ColorAndOpacity(CastleTextColor)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).IsFocusable(false).ContentPadding(FMargin(7.0f, 2.0f)).ButtonColorAndOpacity(bToHero ? CastleButtonColor : CastleHeadColor * 0.35f)
				.OnClicked_Lambda([this]() { bHireToHero = false; ShownRevision = -1; return FReply::Handled(); })
				[
					SNew(STextBlock).Text(LOCTEXT("Garrison", "Garrison")).Font(Small).ColorAndOpacity(CastleTextColor)
				]
			]
		];
	}
	bool bAnyDwelling = false;
	for (int32 Creature = 0; Creature < Faction.Creatures.Num(); ++Creature)
	{
		const int32 Level = Castles->GetLevel(*Castle, EHexBuildingType::Dwelling, Creature);
		if (Level <= 0)
		{
			continue;
		}
		bAnyDwelling = true;
		const FHexCreature& Each = Faction.Creatures[Creature];
		const FHexCreatureStats& Stats = Each.GetStats(Level >= 2);
		const int32 Offered = Castle->Available.IsValidIndex(Creature) ? Castle->Available[Creature] : 0;
		const int32 Most = Castles->GetHireMax(*Castle, Creature);
		FString Price = FString::Printf(TEXT("%d gold"), Stats.Gold);
		if (!Each.CostResource.IsNone() && Each.CostResourceAmount > 0)
		{
			Price += FString::Printf(TEXT(" and %d %s"), Each.CostResourceAmount, *Each.CostResource.ToString().ToLower());
		}
		const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakeText(FString::Printf(TEXT("%s: %d to hire"), *Stats.DisplayName, Offered), Font, Offered > 0 ? CastleTextColor : CastleDimColor)]
			+ SVerticalBox::Slot().AutoHeight()[MakeText(FString::Printf(TEXT("%s each. Attack %d, defense %d, damage %d-%d, health %d, speed %d"), *Price, Stats.Attack, Stats.Defense,
				Stats.DamageMin, Stats.DamageMax, Stats.Health, Stats.Speed), Small, CastleDimColor)];
		const int32 Target = bToHero ? VisitorId : 0;
		const FString Tip = Most > 0 ? FString::Printf(TEXT("Up to %d"), Most) : FString(Offered > 0 ? TEXT("Not enough to pay for one") : TEXT("None offered until next week"));
		AddRow(Left, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
			[
				MakeButton(TEXT("+1"), Most >= 1, Tip, [CastleId, Creature, Target](UHexCastles& Do, FString& Message) { Do.Hire(CastleId, Creature, 1, Target, Message); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeButton(FString::Printf(TEXT("All %d"), Most), Most >= 1, Tip, [CastleId, Creature, Target, Most](UHexCastles& Do, FString& Message) { Do.Hire(CastleId, Creature, Most, Target, Message); })
			]);
	}
	if (!bAnyDwelling)
	{
		Body->AddSlot().AutoHeight()[MakeText(TEXT("No dwelling is built yet."), Small, CastleDimColor)];
	}

	// the garrison, and the army of the hero beside the castle
	AddSection(FString::Printf(TEXT("Garrison (counts %.0f behind the walls)"), Castles->GetDefense(*Castle)));
	if (Castle->Garrison.IsEmpty())
	{
		Body->AddSlot().AutoHeight()[MakeText(TEXT("Nobody guards the castle: any hero takes it."), Small, CastleDimColor)];
	}
	for (int32 Slot = 0; Slot < Castle->Garrison.Num(); ++Slot)
	{
		const TSharedRef<SWidget> Move = Visitor
			? StaticCastSharedRef<SWidget>(MakeButton(FString::Printf(TEXT("To %s"), *Visitor->Name), true, FString(), [CastleId, VisitorId, Slot](UHexCastles& Do, FString& Message) { Do.MoveStack(CastleId, VisitorId, true, Slot, Message); }))
			: StaticCastSharedRef<SWidget>(SNew(SBox));
		AddRow(MakeText(Castles->DescribeStack(Castle->Garrison[Slot]), Font, CastleTextColor), Move);
	}
	if (Visitor)
	{
		AddSection(FString::Printf(TEXT("%s, %s (counts %.0f)"), *Visitor->Name, Visitor->CastleId == CastleId ? TEXT("guards the castle") : TEXT("beside the castle"), Castles->GetAttack(*Visitor)));
		if (Visitor->Army.IsEmpty())
		{
			Body->AddSlot().AutoHeight()[MakeText(TEXT("Leads no creatures."), Small, CastleDimColor)];
		}
		for (int32 Slot = 0; Slot < Visitor->Army.Num(); ++Slot)
		{
			AddRow(MakeText(Castles->DescribeStack(Visitor->Army[Slot]), Font, CastleTextColor),
				MakeButton(TEXT("To garrison"), true, FString(), [CastleId, VisitorId, Slot](UHexCastles& Do, FString& Message) { Do.MoveStack(CastleId, VisitorId, false, Slot, Message); }));
		}
	}
	else
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[MakeText(TEXT("Send a hero into the castle to guard it, to hire into its army and to swap stacks with the garrison."), Small, CastleDimColor)];
	}
}

#undef LOCTEXT_NAMESPACE
