// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexCastlePanel.h"
#include "SHexSpellBook.h"
#include "SHexMapTip.h"
#include "HexHeroConfig.h"
#include "HexCastles.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "HexUnitTooltip.h"
#include "HexBattleConfig.h"
#include "HexCastleConfig.h"
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SWrapBox.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/SToolTip.h"
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

	// for test runs, which have no mouse: the tab of the panel open, 0 to 2, taken once and set back to -1
	TAutoConsoleVariable<int32> CVarCastlePanelTab(TEXT("hex.CastlePanelTab"), -1,
		TEXT("Shows a tab of the panel of the castle open: 0 Buildings, 1 Recruit, 2 Armies, 3 Artifacts; set back to -1 once taken"));
}

void SHexCastlePanel::Construct(const FArguments& InArgs)
{
	OnMessage = InArgs._OnMessage;
	OnSpellBook = InArgs._OnSpellBook;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const float Width = UHexCastleConfig::Load()->PanelWidth;
	const float Top = UHexCastleConfig::Load()->PanelTop;
	SetVisibility(EVisibility::Collapsed);
	ChildSlot
	[
		SNew(SOverlay)
		// a see-through shade over the planet takes the clicks outside the panel, which close it, so the map does not move or pick under
		// it, as the developer asked on 2026-11-15 for every panel the mouse chooses in
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f))
			.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { Close(); return FReply::Handled(); })
		]
		+ SOverlay::Slot()
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
		]
	];
}

FReply SHexCastlePanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
	}
	return FReply::Handled();
}

FString SHexCastlePanel::DescribeArtifact(int32 Artifact) const
{
	const FHexArtifact* Each = Heroes.IsValid() ? Heroes->GetArtifact(Artifact) : nullptr;
	return Each ? FString::Printf(TEXT("%s\n%s, worn on the %s\n%s\n\n%s"), *Each->DisplayName, *GetArtifactRankName(Each->Rank), *GetArtifactSlotName(Each->Slot).ToLower(),
		*Each->DescribeEffect(), *Each->Lore) : FString();
}

TSharedRef<SWidget> SHexCastlePanel::MakeArtifactIcon(int32 Artifact, float Size)
{
	const FHexArtifact* Each = Heroes.IsValid() ? Heroes->GetArtifact(Artifact) : nullptr;
	const FSlateBrush* Icon = Each ? GetBrush(Each->Icon.LoadSynchronous(), Size) : nullptr;
	// the rank in the color of its frame
	const FLinearColor RankColors[4] = {FLinearColor(0.35f, 0.32f, 0.28f, 1.0f), FLinearColor(0.2f, 0.4f, 0.75f, 1.0f), FLinearColor(0.6f, 0.25f, 0.8f, 1.0f),
		FLinearColor(0.95f, 0.7f, 0.15f, 1.0f)};
	const FLinearColor RankFrame = Each ? RankColors[FMath::Clamp(int32(Each->Rank), 0, 3)] : FLinearColor(0.12f, 0.12f, 0.14f, 1.0f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(RankFrame).Padding(2.0f)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.06f, 1.0f)).Padding(0.0f)
				[
					Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon)) : StaticCastSharedRef<SWidget>(SNew(SBox))
				]
			]
		];
}

TSharedRef<SWidget> SHexCastlePanel::MakeArtifacts(const FHexHero& Hero, int32 OtherId)
{
	const UHexHeroConfig* Config = Heroes.IsValid() ? Heroes->GetConfig() : nullptr;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (!Config)
	{
		return Box;
	}
	const FSlateFontInfo Tiny = FCoreStyle::GetDefaultFontStyle("Regular", 7);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	TWeakObjectPtr<UHexHeroes> WeakHeroes = Heroes;
	const TFunction<void(const FString&)> Tell = OnMessage;
	const int32 HeroId = Hero.Id;
	// a click: with Ctrl to the other hero, else on or off
	const auto Click = [WeakHeroes, Tell, HeroId, OtherId](bool bWorn, int32 Index)
	{
		FString Message;
		if (!WeakHeroes.IsValid())
		{
			return FReply::Handled();
		}
		const bool bPass = OtherId != 0 && FSlateApplication::Get().GetModifierKeys().IsControlDown();
		const bool bDone = bPass ? WeakHeroes->PassArtifact(HeroId, bWorn, Index, OtherId, Message)
			: bWorn ? WeakHeroes->TakeOffArtifact(HeroId, Index, Message) : WeakHeroes->WearArtifact(HeroId, Index, Message);
		if (!bDone && Tell && !Message.IsEmpty())
		{
			Tell(Message);
		}
		return FReply::Handled();
	};
	const float Size = 38.0f;
	const TSharedRef<SWrapBox> Worn = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3.0f, 3.0f));
	for (int32 Slot = 0; Slot < NumWornSlots; ++Slot)
	{
		const int32 Artifact = Hero.Worn.IsValidIndex(Slot) ? Hero.Worn[Slot] : INDEX_NONE;
		const FString Kind = GetArtifactSlotName(GetWornSlotKind(Slot));
		const FString Tip = Artifact != INDEX_NONE ? DescribeArtifact(Artifact) + TEXT("\n\nClick to take it off") + (OtherId != 0 ? TEXT(", Ctrl+click to give it to the other hero") : TEXT(""))
			: FString::Printf(TEXT("%s: nothing worn"), *Kind);
		Worn->AddSlot()
		[
			SNew(SButton).IsFocusable(false).ContentPadding(0.0f).ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.0f)).ToolTipText(FText::FromString(Tip))
			.IsEnabled(Artifact != INDEX_NONE)
			.OnClicked_Lambda([Click, Slot]() { return Click(true, Slot); })
			[
				Artifact != INDEX_NONE ? MakeArtifactIcon(Artifact, Size)
				: StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(Size).HeightOverride(Size).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(Tiny).ColorAndOpacity(FLinearColor(0.45f, 0.45f, 0.48f, 1.0f)).Text(FText::FromString(Kind))
					])
			]
		];
	}
	Box->AddSlot().AutoHeight()[Worn];
	Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock).Font(Small).ColorAndOpacity(CastleDimColor).Text(FText::FromString(FString::Printf(TEXT("Backpack, %d of %d%s"), Hero.Backpack.Num(), Config->BackpackSize,
			Hero.Backpack.IsEmpty() ? TEXT("") : TEXT(": click one to put it on"))))
	];
	if (!Hero.Backpack.IsEmpty())
	{
		const TSharedRef<SWrapBox> Pack = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3.0f, 3.0f));
		for (int32 Index = 0; Index < Hero.Backpack.Num(); ++Index)
		{
			const FString Tip = DescribeArtifact(Hero.Backpack[Index]) + TEXT("\n\nClick to put it on") + (OtherId != 0 ? TEXT(", Ctrl+click to give it to the other hero") : TEXT(""));
			Pack->AddSlot()
			[
				SNew(SButton).IsFocusable(false).ContentPadding(0.0f).ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.0f)).ToolTipText(FText::FromString(Tip))
				.OnClicked_Lambda([Click, Index]() { return Click(false, Index); })
				[
					MakeArtifactIcon(Hero.Backpack[Index], 32.0f)
				]
			];
		}
		Box->AddSlot().AutoHeight()[Pack];
	}
	return Box;
}

TSharedRef<SWidget> SHexCastlePanel::MakeHeroSheet(const FHexHero& Hero, int32 OtherId)
{
	const UHexHeroConfig* Config = Heroes.IsValid() ? Heroes->GetConfig() : nullptr;
	const TSharedRef<SVerticalBox> Sheet = SNew(SVerticalBox);
	if (!Config)
	{
		return Sheet;
	}
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo Tiny = FCoreStyle::GetDefaultFontStyle("Regular", 7);
	// its level and experience, and its spell book
	const int32 Next = Config->GetLevelExperience(Hero.Level + 1);
	const int32 Mana = Hero.Mana;
	const int32 MaxMana = Heroes->GetMaxMana(Hero);
	const int32 HeroId = Hero.Id;
	Sheet->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		// its portrait, as the developer asked on 2026-11-15
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			HexUiBrushes::MakeFramed(Heroes->GetHeroPortrait(Hero), 48.0f, Config->GetPlayer(Hero.Player).Color)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Small).ColorAndOpacity(CastleTextColor)
			.Text(FText::FromString(FString::Printf(TEXT("Level %d, %s. %d spells"), Hero.Level, *Config->GetClass(Hero.Class).DisplayName, Hero.Spells.Num())))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(8.0f, 2.0f)).ButtonColorAndOpacity(CastleButtonColor)
			.ToolTipText(LOCTEXT("BookTip", "The spells the hero knows"))
			.OnClicked_Lambda([this, HeroId]() { if (OnSpellBook) { OnSpellBook(HeroId); } return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Small).ColorAndOpacity(CastleTextColor).Text(LOCTEXT("SpellBook", "Spell book"))
			]
		]
	];
	// its attributes with their icons, as the developer asked on 2026-11-15, each saying in its tooltip what it gives and how much of it its
	// artifacts give
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	const FHexArtifactBonus Bonus = Heroes->GetArtifactBonus(Hero);
	const auto Parts = [](int32 Total, int32 FromArtifacts)
	{
		return FromArtifacts == 0 ? FString() : FString::Printf(TEXT(" (%d of its own, %+d from artifacts)"), Total - FromArtifacts, FromArtifacts);
	};
	const auto Percent = [](float Share) { return FMath::RoundToInt32(Share * 100.0f); };
	const int32 Back = Config->ManaPerDay + FMath::RoundToInt32(Heroes->GetSkillValue(Hero, EHexSkill::Mysticism)) + Bonus.ManaPerDay;
	const float MaxMove = Heroes->GetMaxMovePoints(Hero);
	struct FStatChip
	{
		EHexStatIcon Icon;
		FString Value;
		FString Tip;
		bool bRaised = false;
	};
	TArray<FStatChip> Chips;
	Chips.Add({EHexStatIcon::Attack, FString::FromInt(Hero.Attack), FString::Printf(TEXT("Attack %d%s.\nAdded to the Attack of every creature of its army in a battle: each point of Attack above the Defense of the creature struck adds %d%% to the damage, up to %d%% more."),
		Hero.Attack, *Parts(Hero.Attack, Bonus.Attack), Percent(Battle ? Battle->AttackBonusPerPoint : 0.05f), Percent(Battle ? Battle->MaxAttackBonus : 3.0f)), Bonus.Attack != 0});
	Chips.Add({EHexStatIcon::Defense, FString::FromInt(Hero.Defense), FString::Printf(TEXT("Defense %d%s.\nAdded to the Defense of every creature of its army in a battle: each point of Defense above the Attack of the one that strikes takes %d%% off the damage, down to %d%% less."),
		Hero.Defense, *Parts(Hero.Defense, Bonus.Defense), Percent(Battle ? Battle->DefenseCutPerPoint : 0.025f), Percent(Battle ? Battle->MaxDefenseCut : 0.7f)), Bonus.Defense != 0});
	Chips.Add({EHexStatIcon::SpellPower, FString::FromInt(Hero.SpellPower), FString::Printf(TEXT("Spell power %d%s.\nThe strength of its spells: each point raises the damage or healing of a spell and keeps its effects longer, by as much as the spell says in the spell book."),
		Hero.SpellPower, *Parts(Hero.SpellPower, Bonus.SpellPower)), Bonus.SpellPower != 0});
	Chips.Add({EHexStatIcon::Knowledge, FString::FromInt(Hero.Knowledge), FString::Printf(TEXT("Knowledge %d%s.\nIts mana: %d a point, so at most %d."),
		Hero.Knowledge, *Parts(Hero.Knowledge, Bonus.Knowledge), Config->ManaPerKnowledge, MaxMana), Bonus.Knowledge != 0});
	Chips.Add({EHexStatIcon::Mana, FString::Printf(TEXT("%d/%d"), Mana, MaxMana), FString::Printf(TEXT("Mana %d of %d.\nSpells cost it in a battle. %d comes back a day%s, and all of it in a castle of its own with a mage guild."),
		Mana, MaxMana, Back, Bonus.ManaPerDay != 0 ? *FString::Printf(TEXT(" (%+d from artifacts)"), Bonus.ManaPerDay) : TEXT("")), Bonus.ManaPerDay != 0});
	Chips.Add({EHexStatIcon::Movement, FString::Printf(TEXT("%.0f/%.0f"), Hero.MovePoints, MaxMove), FString::Printf(TEXT("Movement %.1f of %.1f today.\nA step costs by the ground it goes onto, a road the least and swamp and snow the most; what is left of a day is lost with it.%s"),
		Hero.MovePoints, MaxMove, Bonus.Movement != 0.0f ? *FString::Printf(TEXT(" Artifacts give %+.1f a day, from the next day."), Bonus.Movement) : TEXT("")), Bonus.Movement != 0.0f});
	Chips.Add({EHexStatIcon::Experience, FString::Printf(TEXT("%d/%d"), Hero.Experience, Next), FString::Printf(TEXT("Experience %d.\nLevel %d at %d: a new level raises one of its attributes and offers a skill. It comes from battles won and from chests."),
		Hero.Experience, Hero.Level + 1, Next)});
	Chips.Add({EHexStatIcon::Morale, FString::Printf(TEXT("%+d"), Bonus.Morale), FString::Printf(TEXT("Morale %+d from artifacts.\nAdded to the morale of every creature of its army in a battle: each point above 0 gives a stack %d%% of another turn after its turn (the undead have none)."),
		Bonus.Morale, Percent(Battle ? Battle->MoraleChancePerPoint : 1.0f / 24.0f)), Bonus.Morale != 0});
	Chips.Add({EHexStatIcon::Luck, FString::Printf(TEXT("%+d"), Bonus.Luck), FString::Printf(TEXT("Luck %+d from artifacts.\nAdded to the luck of every creature of its army in a battle: each point above 0 gives a strike %d%% of dealing double damage."),
		Bonus.Luck, Percent(Battle ? Battle->LuckChancePerPoint : 1.0f / 24.0f)), Bonus.Luck != 0});
	const TSharedRef<SWrapBox> Stats = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.0f, 4.0f));
	for (const FStatChip& Chip : Chips)
	{
		const FSlateBrush* Icon = HexUnitTooltip::GetIcon(Chip.Icon);
		Stats->AddSlot()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.1f, 1.0f)).Padding(FMargin(4.0f, 2.0f, 6.0f, 2.0f))
			.ToolTipText(FText::FromString(Chip.Tip))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)
					[
						Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon)) : StaticCastSharedRef<SWidget>(SNew(SBox))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					// what artifacts raise in green
					SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(Chip.bRaised ? FLinearColor(0.45f, 1.0f, 0.45f, 1.0f) : CastleTextColor).Text(FText::FromString(Chip.Value))
				]
			]
		];
	}
	Sheet->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[Stats];
	// its skills, MaxSkills slots
	const TSharedRef<SHorizontalBox> Slots = SNew(SHorizontalBox);
	for (int32 Slot = 0; Slot < Config->MaxSkills; ++Slot)
	{
		const FHexHeroSkill* Skill = Hero.Skills.IsValidIndex(Slot) ? &Hero.Skills[Slot] : nullptr;
		const FHexSkillInfo* Info = Skill ? Config->FindSkill(Skill->Skill) : nullptr;
		const FSlateBrush* Icon = Info ? HexUiBrushes::Get(Info->Icon.LoadSynchronous(), 40.0f) : nullptr;
		FString Tip = TEXT("An empty slot: a new level may offer a skill");
		if (Info)
		{
			const FString Value = FString::FromInt(FMath::RoundToInt32(Info->GetValue(Skill->Level) * (Info->Skill == EHexSkill::Mysticism ? 1.0f : 100.0f)));
			const FString Second = FString::FromInt(FMath::RoundToInt32(Info->GetValue(Skill->Level, true) * 100.0f));
			Tip = FString::Printf(TEXT("%s %s: %s"), GetSkillLevelName(Skill->Level), *Info->DisplayName, *Info->Description.Replace(TEXT("{0}"), *Value).Replace(TEXT("{1}"), *Second));
		}
		Slots->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(52.0f).ToolTipText(FText::FromString(Tip))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(40.0f).HeightOverride(40.0f)
					[
						SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.1f, 1.0f)).Padding(0.0f)
						[
							Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon)) : StaticCastSharedRef<SWidget>(SNew(SBox))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Tiny).ColorAndOpacity(CastleDimColor).Text(FText::FromString(Skill ? GetSkillLevelName(Skill->Level) : TEXT("")))
				]
			]
		];
	}
	Sheet->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[Slots];
	// and its artifacts
	Sheet->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)[MakeArtifacts(Hero, OtherId)];
	return Sheet;
}

void SHexCastlePanel::SetHeroes(UHexHeroes* InHeroes)
{
	if (Heroes.Get() != InHeroes)
	{
		Heroes = InHeroes;
		Close();
	}
}

void SHexCastlePanel::OpenArmy(int32 HeroId, int32 OtherId)
{
	Open(0);
	ArmyHero = HeroId;
	OtherHero = OtherId;
	ChosenStackHero = 0;
	ChosenStackSlot = INDEX_NONE;
	ShownRevision = -1;
	SetVisibility(HeroId != 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

void SHexCastlePanel::Open(int32 CastleId)
{
	OpenId = CastleId;
	ArmyHero = 0;
	OtherHero = 0;
	ShownRevision = -1;
	Tab = ETab::Buildings;
	bHireToGarrison = false;
	ChosenStackHero = 0;
	ChosenStackSlot = INDEX_NONE;
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
	// an army, while its heroes are there and it is the turn of their person
	if (ArmyHero != 0)
	{
		const FHexHero* Hero = Heroes.IsValid() ? Heroes->FindHero(ArmyHero) : nullptr;
		const FHexHero* Other = Heroes.IsValid() && OtherHero != 0 ? Heroes->FindHero(OtherHero) : nullptr;
		if (!Hero || !Heroes->IsPlaying() || !Heroes->IsPersonTurn() || Hero->Player != Heroes->GetShroudPlayer() || Hero->bWalking || (OtherHero != 0 && (!Other || Other->bWalking)))
		{
			Close();
			return;
		}
		if (ShownRevision != Heroes->GetListRevision())
		{
			ShownRevision = Heroes->GetListRevision();
			RebuildArmy();
		}
		return;
	}
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
	if (CVarCastlePanelTab.GetValueOnGameThread() >= 0)
	{
		Tab = ETab(FMath::Clamp(CVarCastlePanelTab.GetValueOnGameThread(), 0, 3));
		CVarCastlePanelTab->Set(-1, ECVF_SetByConsole);
		ShownRevision = -1;
	}
	if (ShownRevision != Castles->GetRevision() || ShownStock != Stock)
	{
		ShownRevision = Castles->GetRevision();
		ShownStock = Stock;
		RebuildBody();
	}
}

TSharedRef<SWidget> SHexCastlePanel::MakeCostRow(const TMap<FName, int32>& Cost, int32 Player, const FSlateFontInfo& Font)
{
	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	const UHexSites* Sites = Heroes.IsValid() ? Heroes->GetSites() : nullptr;
	if (!Sites)
	{
		return Row;
	}
	// in the order of the resources, each its icon and its amount
	const UHexSiteConfig* SiteConfig = Sites->GetConfig();
	for (int32 Resource = 0; Resource < SiteConfig->Resources.Num(); ++Resource)
	{
		const FHexResourceInfo& Info = SiteConfig->Resources[Resource];
		const int32* Amount = Cost.Find(Info.Id);
		if (!Amount || *Amount <= 0)
		{
			continue;
		}
		const bool bEnough = Sites->GetAmount(Player, Resource) >= *Amount;
		const FLinearColor Color = bEnough ? FLinearColor(0.45f, 0.95f, 0.4f, 1.0f) : FLinearColor(1.0f, 0.36f, 0.3f, 1.0f);
		const float IconSize = float(Font.Size) * 2.2f;
		const FSlateBrush* Brush = GetBrush(Info.Icon.LoadSynchronous(), IconSize, Info.IconFrames);
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 2.0f, 0.0f)
		[
			Brush ? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(IconSize).HeightOverride(IconSize)[SNew(SImage).Image(Brush)])
				: StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(Font).ColorAndOpacity(Color).Text(FText::FromString(Info.DisplayName)))
		];
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock).Font(Font).ColorAndOpacity(Color).Text(FText::AsNumber(*Amount)).ToolTipText(FText::FromString(Info.DisplayName))
		];
	}
	return Row;
}

const FSlateBrush* SHexCastlePanel::GetBrush(UTexture2D* Texture, float Size, int32 Frames)
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
	Brush->ImageSize = FVector2D(Size, Size);
	Brush->DrawAs = ESlateBrushDrawType::Image;
	// a strip of frames side by side, as the icons are, shows its first; the count is given, as the size of a texture an editor build still
	// compiles is not known yet
	if (Frames > 1)
	{
		Brush->SetUVRegion(FBox2f(FVector2f::ZeroVector, FVector2f(1.0f / float(Frames), 1.0f)));
	}
	Textures.Emplace(Texture);
	Brushes.Add(Texture, Brush);
	return Brush.Get();
}

TSharedRef<SWidget> SHexCastlePanel::MakeStackPortrait(int32 Faction, int32 Creature, bool bUpgraded, float Size)
{
	const UHexCastleConfig* Config = UHexCastleConfig::Load();
	const FHexCreature* Each = Config->Factions.IsValidIndex(Faction) && Config->Factions[Faction].Creatures.IsValidIndex(Creature) ? &Config->Factions[Faction].Creatures[Creature] : nullptr;
	const FHexUnitLook* Look = Each ? UHexBattleConfig::Load()->FindLook(Each->Id) : nullptr;
	const FSlateBrush* Brush = Look ? GetBrush(Look->Portrait.LoadSynchronous(), Size) : nullptr;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	if (!Brush)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.1f, 1.0f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::AsNumber(Creature + 1)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)).ColorAndOpacity(CastleDimColor)
				]
			];
	}
	const FLinearColor Tint = bUpgraded ? Look->UpgradedTint : Look->Tint;
	// its level and chevrons in the top left corner, as the developer asked on 2026-11-13; none on the small pictures of the buildings
	const TSharedRef<SOverlay> Picture = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.06f, 0.065f, 0.08f, 1.0f)).Padding(0.0f)
			[
				SNew(SImage).Image(Brush).ColorAndOpacity(FLinearColor(Tint.R, Tint.G, Tint.B, 1.0f))
			]
		];
	if (Size >= 30.0f)
	{
		Picture->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f)).Padding(FMargin(2.0f, 0.0f, 2.0f, 1.0f))
			[
				HexUnitTooltip::MakeRankBadge(Creature + 1, bUpgraded ? 2 : 1, Size >= 44.0f ? 8.0f : 7.0f, FLinearColor(0.98f, 0.82f, 0.35f, 1.0f))
			]
		];
	}
	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[Picture];
}

TSharedRef<SWidget> SHexCastlePanel::MakeSlotRow(TConstArrayView<FHexStack> Army, int32 Key, TFunction<void(int32, int32, int32, int32, int32)> Arrange,
	TFunction<const TArray<FHexStack>*(int32)> GetArmy, TFunction<TSharedPtr<SWidget>(int32, const FHexStack&)> MakeCorner)
{
	const UHexCastleConfig* Config = UHexCastleConfig::Load();
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateBrush* NoBorder = FCoreStyle::Get().GetBrush(TEXT("NoBorder"));
	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 Slot = 0; Slot < NumArmySlots; ++Slot)
	{
		// a slot keeps its place empty, so stacks are put where they will stand in a battle, as the developer asked on 2026-11-13
		const FHexStack* Stack = Army.IsValidIndex(Slot) && Army[Slot].Count > 0 ? &Army[Slot] : nullptr;
		const bool bChosen = ChosenStackSlot == Slot && ChosenStackHero == Key;
		const FHexCreatureStats* Stats = Stack ? Config->GetStats(*Stack) : nullptr;
		TSharedRef<SWidget> Inside = SNew(SBox).WidthOverride(46.0f).HeightOverride(58.0f);
		if (Stack)
		{
			Inside = SNew(SBox).WidthOverride(46.0f).HeightOverride(58.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[MakeStackPortrait(Stack->Faction, Stack->Creature, Stack->bUpgraded, 44.0f)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(FText::AsNumber(Stack->Count)).Font(Small).ColorAndOpacity(bChosen ? CastleHeadColor : CastleTextColor)
					]
				];
		}
		const TSharedRef<SButton> Button = SNew(SButton)
			.IsFocusable(false)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(0.0f)
			.OnClicked_Lambda([this, Key, Slot, Arrange, GetArmy, bHas = Stack != nullptr]()
			{
				const FModifierKeysState Keys = FSlateApplication::Get().GetModifierKeys();
				if (ChosenStackSlot == INDEX_NONE || (ChosenStackHero == Key && ChosenStackSlot == Slot))
				{
					// choose a stack, or put the one chosen down again
					const bool bAgain = ChosenStackHero == Key && ChosenStackSlot == Slot;
					ChosenStackHero = bAgain || !bHas ? 0 : Key;
					ChosenStackSlot = bAgain || !bHas ? INDEX_NONE : Slot;
				}
				else
				{
					// the stack chosen goes into this slot: all of it, half with Shift, one with Ctrl
					const TArray<FHexStack>* From = GetArmy(ChosenStackHero);
					const int32 Count = !From || !From->IsValidIndex(ChosenStackSlot) ? 0 : Keys.IsControlDown() ? 1
						: Keys.IsShiftDown() ? FMath::Max((*From)[ChosenStackSlot].Count / 2, 1) : 0;
					Arrange(ChosenStackHero, ChosenStackSlot, Key, Slot, Count);
					ChosenStackHero = 0;
					ChosenStackSlot = INDEX_NONE;
				}
				ShownRevision = -1;
				return FReply::Handled();
			})
			[
				SNew(SBorder).BorderImage(White).Padding(2.0f)
				.BorderBackgroundColor(bChosen ? FLinearColor(0.55f, 0.4f, 0.08f, 1.0f) : Stack ? FLinearColor(0.08f, 0.08f, 0.1f, 1.0f) : FLinearColor(0.04f, 0.04f, 0.05f, 1.0f))
				[
					Inside
				]
			];
		if (Stats)
		{
			FHexUnitTooltipData Data = HexUnitTooltip::FromStats(*Stats);
			Data.Title = FString::Printf(TEXT("%d %s"), Stack->Count, *Data.Title);
			HexUnitTooltip::SetRank(Data, Stack->Creature, Stack->bUpgraded);
			Data.Subtitle = Config->Factions[Stack->Faction].DisplayName;
			Data.TitleColor = CastleHeadColor;
			Data.bFlies = Config->Factions[Stack->Faction].Creatures[Stack->Creature].bFlies;
			// a stack of a hero: what the hero adds in a battle, its Attack and Defense with those its artifacts give, in green, and the morale
			// and luck of its artifacts, as the developer saw none of it on 2026-11-15
			if (const FHexHero* Leader = Key > 0 && Heroes.IsValid() ? Heroes->FindHero(Key) : nullptr)
			{
				const FHexArtifactBonus Bonus = Heroes->GetArtifactBonus(*Leader);
				Data.AttackBonus = Leader->Attack;
				Data.DefenseBonus = Leader->Defense;
				Data.Morale += Stats->HasAbility(EHexAbility::Undead) ? 0 : Bonus.Morale;
				Data.Luck += Bonus.Luck;
				Data.Extra = FString::Printf(TEXT("In a battle %s adds %+d Attack and %+d Defense%s"), *Leader->Name, Leader->Attack, Leader->Defense,
					Bonus.Morale != 0 || Bonus.Luck != 0 ? *FString::Printf(TEXT(", and its artifacts %+d morale and %+d luck"), Bonus.Morale, Bonus.Luck) : TEXT(""));
			}
			Button->SetToolTip(SNew(SToolTip).BorderImage(NoBorder)
				[
					SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f)).Padding(FMargin(10.0f, 7.0f))[HexUnitTooltip::Make(Data)]
				]);
		}
		// a button in its corner, as the one that upgrades it in a castle
		const TSharedPtr<SWidget> Corner = Stack && MakeCorner ? MakeCorner(Slot, *Stack) : nullptr;
		if (Corner.IsValid())
		{
			Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[Button]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top)[Corner.ToSharedRef()]
			];
		}
		else
		{
			Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)[Button];
		}
	}
	return Row;
}

TSharedRef<SWidget> SHexCastlePanel::MakeSplitButtons(TFunction<void(int32, int32, int32, int32, int32)> Arrange, TFunction<const TArray<FHexStack>*(int32)> GetArmy)
{
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	// the stack chosen split in its own army, into its first free slot
	const TArray<FHexStack>* Of = ChosenStackSlot != INDEX_NONE ? GetArmy(ChosenStackHero) : nullptr;
	const bool bSplit = Of && Of->IsValidIndex(ChosenStackSlot) && (*Of)[ChosenStackSlot].Count > 1 && UHexCastles::FindFreeSlot(*Of) != INDEX_NONE;
	const auto MakeSplit = [&](const FString& Label, bool bHalf)
	{
		return SNew(SButton)
			.IsFocusable(false)
			.IsEnabled(bSplit)
			.ContentPadding(FMargin(7.0f, 2.0f))
			.ButtonColorAndOpacity(CastleButtonColor)
			.OnClicked_Lambda([this, Arrange, GetArmy, bHalf]()
			{
				if (const TArray<FHexStack>* Army = GetArmy(ChosenStackHero); Army && Army->IsValidIndex(ChosenStackSlot))
				{
					Arrange(ChosenStackHero, ChosenStackSlot, ChosenStackHero, UHexCastles::FindFreeSlot(*Army), bHalf ? FMath::Max((*Army)[ChosenStackSlot].Count / 2, 1) : 1);
				}
				ChosenStackHero = 0;
				ChosenStackSlot = INDEX_NONE;
				ShownRevision = -1;
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(Small).ColorAndOpacity(CastleTextColor)
			];
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)[MakeSplit(TEXT("Split in half"), true)]
		+ SHorizontalBox::Slot().AutoWidth()[MakeSplit(TEXT("Split off one"), false)];
}

void SHexCastlePanel::RebuildArmy()
{
	Body->ClearChildren();
	const FHexHero* Hero = Heroes.IsValid() ? Heroes->FindHero(ArmyHero) : nullptr;
	if (!Hero)
	{
		return;
	}
	const FHexHero* Other = OtherHero != 0 ? Heroes->FindHero(OtherHero) : nullptr;
	const UHexCastleConfig* Config = UHexCastleConfig::Load();
	const UHexCastles* Castles = Heroes->GetCastles();
	const FSlateFontInfo Head = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo Section = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo Tiny = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateBrush* NoBorder = FCoreStyle::Get().GetBrush(TEXT("NoBorder"));
	TWeakObjectPtr<UHexHeroes> WeakHeroes = Heroes;
	const TFunction<void(const FString&)> Tell = OnMessage;
	// wrapped at the width of the panel and not by AutoWrapText, which a text made anew takes from the frame before, so each rebuild drew it on
	// one line for a frame, as the developer saw a hint jump when a stack was chosen
	const float WrapWidth = FMath::Max(UHexCastleConfig::Load()->PanelWidth - 30.0f, 100.0f);
	const auto MakeText = [WrapWidth](const FString& Text, const FSlateFontInfo& InFont, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(InFont).ColorAndOpacity(Color).WrapTextAt(WrapWidth);
	};
	// moves creatures of the stack chosen to a slot, and says why not
	const auto Arrange = [WeakHeroes, Tell](int32 FromHero, int32 FromSlot, int32 ToHero, int32 ToSlot, int32 Count)
	{
		FString Message;
		if (WeakHeroes.IsValid() && !WeakHeroes->ArrangeArmy(FromHero, FromSlot, ToHero, ToSlot, Count, Message) && Tell && !Message.IsEmpty())
		{
			Tell(Message);
		}
	};

	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeText(Other ? FString::Printf(TEXT("%s and %s meet"), *Hero->Name, *Other->Name) : FString::Printf(TEXT("The army of %s"), *Hero->Name), Head, CastleHeadColor)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(7.0f, 1.0f)).ButtonColorAndOpacity(CastleButtonColor)
			.OnClicked_Lambda([this]() { Close(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("CloseArmy", "x")).Font(Small).ColorAndOpacity(CastleTextColor)
			]
		]
	];

	// a row of seven slots for each hero, and buttons that split the stack chosen
	// a key below 0 is the guard of the site of that Id
	const TFunction<const TArray<FHexStack>*(int32)> GetArmy = [WeakHeroes](int32 Key) -> const TArray<FHexStack>*
	{
		if (Key < 0)
		{
			const FHexSite* Site = WeakHeroes.IsValid() && WeakHeroes->GetSites() ? WeakHeroes->GetSites()->FindSite(-Key) : nullptr;
			return Site ? &Site->Guard : nullptr;
		}
		const FHexHero* Of = WeakHeroes.IsValid() ? WeakHeroes->FindHero(Key) : nullptr;
		return Of ? &Of->Army : nullptr;
	};
	for (const FHexHero* Of : {Hero, Other})
	{
		if (Of)
		{
			Body->AddSlot().AutoHeight().Padding(0.0f, 9.0f, 0.0f, 3.0f)
			[
				MakeText(FString::Printf(TEXT("%s (%.0f)"), *Of->Name, Castles ? Castles->GetAttack(*Of) : 0.0f), Section, CastleHeadColor)
			];
			Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[MakeHeroSheet(*Of, Of == Hero ? OtherHero : ArmyHero)];
			Body->AddSlot().AutoHeight()[MakeSlotRow(Of->Army, Of->Id, Arrange, GetArmy)];
		}
	}
	// a hero alone on a mine or a mill of its player may leave creatures there to guard it, as the developer asked on 2026-11-13
	const FHexSite* Mine = !Other && Heroes->GetSites() ? Heroes->GetSites()->FindBlockerAt(Hero->Layer, Hero->Cell) : nullptr;
	if (Mine && Mine->Owner == Hero->Player)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 9.0f, 0.0f, 3.0f)
		[
			MakeText(FString::Printf(TEXT("Guard of the %s (%.0f)"), *Heroes->GetSites()->GetConfig()->Kinds[Mine->Kind].DisplayName.ToLower(), Castles ? Castles->GetPower(Mine->Guard) : 0.0f),
				Section, CastleHeadColor)
		];
		Body->AddSlot().AutoHeight()[MakeSlotRow(Mine->Guard, -Mine->Id, Arrange, GetArmy)];
		// buttons that leave the stack chosen to guard it and take one of the guard back, as the developer asked on 2026-11-15, so it shows
		// that a hero there can leave creatures
		const int32 HeroKey = Hero->Id;
		const int32 GuardKey = -Mine->Id;
		const bool bHeroStack = ChosenStackHero == HeroKey && Hero->Army.IsValidIndex(ChosenStackSlot) && Hero->Army[ChosenStackSlot].Count > 0;
		const bool bGuardStack = ChosenStackHero == GuardKey && Mine->Guard.IsValidIndex(ChosenStackSlot) && Mine->Guard[ChosenStackSlot].Count > 0;
		const auto MakeGuardButton = [&](const FString& Label, const FString& Tip, bool bEnabled, int32 FromKey, int32 ToKey)
		{
			return SNew(SButton)
				.IsFocusable(false)
				.IsEnabled(bEnabled)
				.ContentPadding(FMargin(7.0f, 2.0f))
				.ButtonColorAndOpacity(CastleButtonColor)
				.ToolTipText(FText::FromString(Tip))
				.OnClicked_Lambda([this, Arrange, GetArmy, FromKey, ToKey]()
				{
					const TArray<FHexStack>* To = GetArmy(ToKey);
					const TArray<FHexStack>* From = GetArmy(FromKey);
					if (To && From && From->IsValidIndex(ChosenStackSlot))
					{
						// onto a stack of its kind there, else into the first free slot
						const FHexStack& Moved = (*From)[ChosenStackSlot];
						int32 Slot = To->IndexOfByPredicate([&Moved](const FHexStack& Each)
						{
							return Each.Count > 0 && Each.Faction == Moved.Faction && Each.Creature == Moved.Creature && Each.bUpgraded == Moved.bUpgraded;
						});
						Slot = Slot != INDEX_NONE ? Slot : UHexCastles::FindFreeSlot(*To);
						Arrange(FromKey, ChosenStackSlot, ToKey, Slot, 0);
					}
					ChosenStackHero = 0;
					ChosenStackSlot = INDEX_NONE;
					ShownRevision = -1;
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(Small).ColorAndOpacity(CastleTextColor)
				];
		};
		Body->AddSlot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeGuardButton(TEXT("Leave to guard"), TEXT("The stack chosen of the hero stays here and guards the mine: a hero of another player fights it before it takes the mine"),
					bHeroStack, HeroKey, GuardKey)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeGuardButton(TEXT("Take back"), TEXT("The stack chosen of the guard goes back into the army of the hero"), bGuardStack, GuardKey, HeroKey)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				MakeText(bHeroStack || bGuardStack ? FString() : FString(TEXT("Choose a stack first")), Tiny, CastleDimColor)
			]
		];
	}
	Body->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[MakeSplitButtons(Arrange, GetArmy)];
	Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		MakeText(Other ? TEXT("Click a stack, then a slot of either army: an empty one takes it, one of its kind joins it, another swaps with it. Shift moves half, Ctrl one creature.")
			: TEXT("Click a stack, then a slot: an empty one takes it, one of its kind joins it, another swaps with it. Shift moves half, Ctrl one creature."), Tiny, CastleDimColor)
	];
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
	const UHexBattleConfig* BattleConfig = UHexBattleConfig::Load();
	const FHexFaction& Faction = Config->Factions[Castle->Faction];
	const FSlateFontInfo Head = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo Section = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo Tiny = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateBrush* NoBorder = FCoreStyle::Get().GetBrush(TEXT("NoBorder"));
	const int32 CastleId = Castle->Id;
	TWeakObjectPtr<UHexHeroes> WeakHeroes = Heroes;
	const TFunction<void(const FString&)> Tell = OnMessage;

	// wrapped at the width of the panel and not by AutoWrapText, which a text made anew takes from the frame before, so each rebuild drew it on
	// one line for a frame, as the developer saw a hint jump when a stack was chosen
	const float WrapWidth = FMath::Max(UHexCastleConfig::Load()->PanelWidth - 30.0f, 100.0f);
	const auto MakeText = [WrapWidth](const FString& Text, const FSlateFontInfo& InFont, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(InFont).ColorAndOpacity(Color).WrapTextAt(WrapWidth);
	};
	// what a button does to the castle, saying what came of it
	const auto MakeAction = [WeakHeroes, Tell](TFunction<void(UHexCastles&, FString&)> Do)
	{
		return [WeakHeroes, Tell, Do]()
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
		};
	};
	const auto MakeButton = [&](const FString& Label, bool bEnabled, const FString& Tip, TFunction<void(UHexCastles&, FString&)> Do)
	{
		return SNew(SButton)
			.IsFocusable(false)
			.IsEnabled(bEnabled)
			.ContentPadding(FMargin(7.0f, 2.0f))
			.ButtonColorAndOpacity(CastleButtonColor)
			.ToolTipText(FText::FromString(Tip))
			.OnClicked_Lambda(MakeAction(Do))
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(Small).ColorAndOpacity(CastleTextColor)
			];
	};
	// a chip of a row of choices, lit while it is the one chosen, with a mark of a color when Mark is not transparent
	const auto MakeChip = [&](const FString& Label, bool bLit, const FLinearColor& Mark, TFunction<void()> Choose, UTexture2D* Picture = nullptr)
	{
		const bool bMark = Mark.A > 0.0f;
		return SNew(SButton)
			.IsFocusable(false)
			.ContentPadding(FMargin(8.0f, 3.0f))
			.ButtonColorAndOpacity(bLit ? CastleHeadColor * 0.4f : CastleButtonColor)
			.OnClicked_Lambda([this, Choose]() { Choose(); ShownRevision = -1; return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				// the portrait of a hero in the color of its player, as the developer asked on 2026-11-15, else a mark of that color
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, bMark || Picture ? 5.0f : 0.0f, 0.0f)
				[
					Picture ? HexUiBrushes::MakeFramed(Picture, 24.0f, Mark) : StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(bMark ? 8.0f : 0.0f).HeightOverride(8.0f)
					[
						SNew(SBorder).BorderImage(White).BorderBackgroundColor(Mark)
					])
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(Small).ColorAndOpacity(CastleTextColor)
				]
			];
	};
	const auto AddSection = [&](const FString& Title)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 9.0f, 0.0f, 3.0f)[MakeText(Title, Section, CastleHeadColor)];
	};
	// the tooltip of a creature, the panel a battle shows, as the developer asked on 2026-11-12
	const auto MakeCreatureTip = [&](const FHexCreatureStats& Stats, bool bFlies, const FString& Extra, int32 Creature, bool bUpgraded)
	{
		FHexUnitTooltipData Data = HexUnitTooltip::FromStats(Stats);
		HexUnitTooltip::SetRank(Data, Creature, bUpgraded);
		Data.Subtitle = Faction.DisplayName;
		Data.TitleColor = CastleHeadColor;
		Data.bFlies = bFlies;
		Data.Extra = Extra;
		return SNew(SToolTip).BorderImage(NoBorder)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f))
				.Padding(FMargin(10.0f, 7.0f))[HexUnitTooltip::Make(Data)]
			];
	};
	// the portrait of a creature of the faction (Portrait of its look in DA_HexBattle), the upgraded version in its tint
	const auto MakePortrait = [&](int32 Creature, bool bUpgraded, float Size) -> TSharedRef<SWidget>
	{
		return MakeStackPortrait(Castle->Faction, Creature, bUpgraded, Size);
	};

	// the castle and its owner
	const UHexHeroConfig* HeroConfig = Heroes->GetConfig();
	const FString Owner = HeroConfig->Players.IsValidIndex(Castle->Owner) ? HeroConfig->Players[Castle->Owner].Name : FString(TEXT("nobody"));
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		// its picture by its fort and faction, as the developer asked on 2026-11-15, and its name
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			HexUiBrushes::MakeFramed(Castles->GetCastlePicture(*Castle), 40.0f, HeroConfig->Players.IsValidIndex(Castle->Owner) ? HeroConfig->Players[Castle->Owner].Color : FLinearColor(0.6f, 0.6f, 0.6f), Castles->GetCastleTint(*Castle))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[MakeText(FString::Printf(TEXT("%s, %s, of %s"), *Castles->GetCastleName(*Castle), *Faction.DisplayName, *Owner), Head, CastleHeadColor)]
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
		MakeText(FString::Printf(TEXT("%d gold a day. %s"), Gold, Castles->HasUnlimitedBuilds() ? TEXT("Builds without a limit (hex.Cheat buildnolimit).")
			: Castle->BuiltToday >= Config->BuildsPerDay ? TEXT("Built today already: the next building tomorrow.") : TEXT("May build today.")),
			Small, CastleDimColor)
	];

	// the tabs
	{
		const TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
		const TPair<ETab, const TCHAR*> AllTabs[] = {{ETab::Buildings, TEXT("Buildings")}, {ETab::Recruit, TEXT("Recruit")}, {ETab::Armies, TEXT("Armies")}, {ETab::Market, TEXT("Artifacts")}};
		for (const TPair<ETab, const TCHAR*>& One : AllTabs)
		{
			const ETab Which = One.Key;
			Tabs->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeChip(One.Value, Tab == Which, FLinearColor::Transparent, [this, Which]() { Tab = Which; })
			];
		}
		Body->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)[Tabs];
		Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(1.0f)[SNew(SBorder).BorderImage(White).BorderBackgroundColor(CastleHeadColor * 0.5f)]];
	}

	// the heroes in or beside the castle, the one that guards it first, each with a chip of its own, as the developer asked on 2026-11-13
	const TArray<const FHexHero*> Visitors = Castles->FindVisitors(*Castle);
	const FHexHero* Chosen = nullptr;
	for (const FHexHero* Visitor : Visitors)
	{
		Chosen = Visitor->Id == ChosenHero ? Visitor : Chosen;
	}
	Chosen = Chosen ? Chosen : (Visitors.IsEmpty() ? nullptr : Visitors[0]);
	ChosenHero = Chosen ? Chosen->Id : 0;
	const int32 ChosenId = ChosenHero;
	const auto AddHeroChips = [&](bool bWithGarrison)
	{
		// in rows that wrap, as three heroes and the garrison are wider than the panel
		const TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3.0f, 3.0f));
		Row->AddSlot().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(bWithGarrison ? TEXT("Into:") : TEXT("Hero:"))).Font(Small).ColorAndOpacity(CastleDimColor)];
		for (const FHexHero* Visitor : Visitors)
		{
			const int32 Id = Visitor->Id;
			const FString Label = Visitor->CastleId == CastleId ? FString::Printf(TEXT("%s (in)"), *Visitor->Name) : Visitor->Name;
			Row->AddSlot()
			[
				MakeChip(Label, Id == ChosenId && !(bWithGarrison && bHireToGarrison), HeroConfig->GetPlayer(Visitor->Player).Color,
					[this, Id]() { ChosenHero = Id; bHireToGarrison = false; }, Heroes->GetHeroPortrait(*Visitor))
			];
		}
		if (bWithGarrison)
		{
			Row->AddSlot()[MakeChip(TEXT("Garrison"), bHireToGarrison || Visitors.IsEmpty(), FLinearColor::Transparent, [this]() { bHireToGarrison = true; })];
		}
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 4.0f)[Row];
	};

	if (Tab == ETab::Market)
	{
		// the artifact merchant, as the developer asked on 2026-11-15: its stock of the week, bought for the hero chosen
		AddHeroChips(false);
		if (!Castles->HasMerchant(*Castle))
		{
			AddSection(TEXT("No artifact merchant"));
			Body->AddSlot().AutoHeight()[MakeText(TEXT("Build the artifact merchant (Buildings) to buy artifacts here. It has another stock every week."), Small, CastleDimColor)];
		}
		else
		{
			AddSection(TEXT("Artifact merchant: the stock of this week"));
			const TSharedRef<SWrapBox> Stock = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
			for (int32 Index = 0; Index < Castle->MerchantStock.Num(); ++Index)
			{
				const int32 Artifact = Castle->MerchantStock[Index];
				const FHexArtifact* Each = Heroes->GetArtifact(Artifact);
				TMap<FName, int32> Price;
				Price.Add(TEXT("Gold"), Castles->GetArtifactPrice(Artifact));
				const bool bCan = Each && Chosen && Sites->CanPay(Castle->Owner, Price);
				const TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);
				Card->AddSlot().AutoHeight().HAlign(HAlign_Center)
				[
					Each ? MakeArtifactIcon(Artifact, 56.0f) : StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(56.0f).HeightOverride(56.0f))
				];
				Card->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Font(Tiny).ColorAndOpacity(Each ? CastleTextColor : CastleDimColor).Text(FText::FromString(Each ? Each->DisplayName : TEXT("Sold")))
					.WrapTextAt(110.0f).Justification(ETextJustify::Center)
				];
				// what it is and gives, as the panel at the cursor on the map shows it, on its card and its button, as the developer asked on
				// 2026-11-15
				FHexMapTip ArtifactTip;
				if (Each)
				{
					ArtifactTip.Title = Each->DisplayName;
					ArtifactTip.TitleColor = FLinearColor(1.0f, 0.82f, 0.4f, 1.0f);
					ArtifactTip.Picture = Each->Icon.LoadSynchronous();
					DescribeArtifact(Artifact).ParseIntoArrayLines(ArtifactTip.Lines);
					ArtifactTip.Lines.Add(Chosen ? FString::Printf(TEXT("Buy it for %s, %d gold"), *Chosen->Name, Castles->GetArtifactPrice(Artifact))
						: FString(TEXT("A hero in the castle or beside it buys it")));
					Card->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 2.0f, 0.0f, 0.0f)[MakeCostRow(Price, Castle->Owner, Tiny)];
					const TSharedRef<SButton> Buy = MakeButton(TEXT("Buy"), bCan, FString(),
						[CastleId, Index, ChosenId](UHexCastles& Seller, FString& Message) { Seller.BuyArtifact(CastleId, Index, ChosenId, Message); });
					Buy->SetToolTip(SHexMapTip::MakeToolTip(ArtifactTip));
					Card->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 3.0f, 0.0f, 0.0f)[Buy];
				}
				const TSharedRef<SBox> CardBox = SNew(SBox).WidthOverride(118.0f)
					[
						SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.07f, 1.0f)).Padding(FMargin(4.0f, 5.0f))[Card]
					];
				if (Each)
				{
					CardBox->SetToolTip(SHexMapTip::MakeToolTip(ArtifactTip));
				}
				Stock->AddSlot()
				[
					CardBox
				];
			}
			Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[Stock];
			if (!Chosen)
			{
				Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)[MakeText(TEXT("A hero in the castle or beside it buys what the merchant has."), Small, CastleDimColor)];
			}
		}
		if (Chosen)
		{
			AddSection(FString::Printf(TEXT("What %s wears and carries"), *Chosen->Name));
			Body->AddSlot().AutoHeight()[MakeArtifacts(*Chosen, 0)];
		}
	}
	else if (Tab == ETab::Buildings)
	{
		// the tree of the buildings: a row for each, its levels as pictures one after the other, the next one a button that builds it
		for (int32 Building = 0; Building < Faction.Buildings.Num() && Building < Castle->Levels.Num(); ++Building)
		{
			const FHexBuilding& Each = Faction.Buildings[Building];
			const int32 Level = Castle->Levels[Building];
			// a building of more than four levels, the mage guild, in smaller pictures, so its row fits the panel
			const float Picture = Each.Levels.Num() > 4 ? 40.0f : 56.0f;
			const FString Label = Each.Type == EHexBuildingType::Hall ? FString(TEXT("Hall")) : Each.Type == EHexBuildingType::Fort ? FString(TEXT("Fort"))
				: Each.Type == EHexBuildingType::MageGuild ? FString(TEXT("Guild")) : Each.Type == EHexBuildingType::Merchant ? FString(TEXT("Market"))
				: FString::Printf(TEXT("Level %d"), Each.Creature + 1);
			const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SBox).WidthOverride(42.0f)[MakeText(Label, Tiny, CastleDimColor)]
				];
			UTexture2D* Last = nullptr;
			for (int32 Step = 0; Step < Each.Levels.Num(); ++Step)
			{
				const FHexBuildingLevel& Of = Each.Levels[Step];
				UTexture2D* Texture = Of.Picture.LoadSynchronous();
				Last = Texture ? Texture : Last;
				const bool bBuilt = Step < Level;
				const bool bNext = Step == Level;
				FString Why;
				const bool bCan = bNext && Castles->CanBuild(*Castle, Building, &Why);
				FString Tip = Of.DisplayName;
				if (Each.Type == EHexBuildingType::Dwelling && Faction.Creatures.IsValidIndex(Each.Creature))
				{
					Tip += FString::Printf(TEXT(": %s"), *Faction.Creatures[Each.Creature].GetStats(Step >= 1).DisplayName.ToLower());
				}
				else if (Each.Type == EHexBuildingType::Hall)
				{
					Tip += FString::Printf(TEXT(": %d gold a day"), Of.GoldPerDay);
				}
				else if (Each.Type == EHexBuildingType::Fort)
				{
					Tip += FString::Printf(TEXT(": dwellings x%.1f, walls x%.1f"), Of.GrowthFactor, Of.DefenseFactor);
				}
				else if (Each.Type == EHexBuildingType::MageGuild)
				{
					// the spells of this level the guild of this castle teaches, its heroes learning them as they go in
					const UHexBattleConfig* Battle = UHexBattleConfig::Load();
					TArray<FString> Names;
					for (const FName& Id : Castle->GuildSpells.IsValidIndex(Step) ? Castle->GuildSpells[Step] : TArray<FName>())
					{
						const int32 Spell = Battle ? Battle->FindSpell(Id.ToString()) : INDEX_NONE;
						Names.Add(Spell != INDEX_NONE ? Battle->Spells[Spell].DisplayName : Id.ToString());
					}
					Tip += FString::Printf(TEXT(": spells of level %d, %s; a hero who goes in learns them and gets all its mana back"), Step + 1,
						Names.IsEmpty() ? TEXT("none") : *FString::Join(Names, TEXT(", ")));
				}
				// what it gives, then what it costs as icons, green where there is enough and red where not, and why not
				const FSlateFontInfo TipFont = FCoreStyle::GetDefaultFontStyle("Regular", 11);
				const TSharedRef<SVerticalBox> TipBox = SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[MakeText(Tip, TipFont, CastleTextColor)];
				if (bBuilt)
				{
					TipBox->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)[MakeText(TEXT("Built"), TipFont, FLinearColor(0.45f, 0.95f, 0.4f, 1.0f))];
				}
				else
				{
					TipBox->AddSlot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)[MakeCostRow(Of.Cost, Castle->Owner, TipFont)];
					if (bNext && !bCan)
					{
						TipBox->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[MakeText(Why, TipFont, FLinearColor(1.0f, 0.36f, 0.3f, 1.0f))];
					}
				}
				const TSharedRef<SToolTip> BuildTip = SNew(SToolTip).BorderImage(NoBorder)
					[
						SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f)).Padding(FMargin(10.0f, 7.0f))[TipBox]
					];
				const FLinearColor Back = bBuilt ? FLinearColor(0.06f, 0.16f, 0.08f, 1.0f) : bCan ? FLinearColor(0.55f, 0.4f, 0.08f, 1.0f)
					: bNext ? FLinearColor(0.11f, 0.09f, 0.07f, 1.0f) : FLinearColor(0.05f, 0.05f, 0.06f, 1.0f);
				const float Fade = bBuilt || bNext ? 1.0f : 0.35f;
				const FSlateBrush* Brush = GetBrush(Last, Picture);
				const TSharedRef<SOverlay> Image = SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SBox).WidthOverride(Picture).HeightOverride(Picture)
						[
							Brush ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush).ColorAndOpacity(FLinearColor(Fade, Fade, Fade, Fade)))
								: StaticCastSharedRef<SWidget>(MakeText(Of.DisplayName, Tiny, CastleDimColor))
						]
					];
				if (Each.Type == EHexBuildingType::Dwelling)
				{
					// the creature it offers, at its corner
					Image->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)[MakePortrait(Each.Creature, Step >= 1, 26.0f)];
				}
				const TSharedRef<SWidget> Tile = SNew(SBorder).BorderImage(White).BorderBackgroundColor(Back).Padding(3.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Image]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(SBox).WidthOverride(Picture + 18.0f)
							[
								SNew(STextBlock).Text(FText::FromString(Of.DisplayName)).Font(Tiny).Justification(ETextJustify::Center).WrapTextAt(Picture + 18.0f)
								.ColorAndOpacity(bBuilt || bNext ? CastleTextColor : CastleDimColor)
							]
						]
					];
				if (Step > 0)
				{
					Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(1.0f, 0.0f)[MakeText(TEXT("›"), Head, bBuilt ? CastleTextColor : CastleDimColor)];
				}
				TFunction<FReply()> OnClick = [] { return FReply::Handled(); };
				if (bCan)
				{
					OnClick = MakeAction([CastleId, Building](UHexCastles& Do, FString& Message) { Do.Build(CastleId, Building, Message); });
				}
				Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 1.0f, 0.0f)
				[
					SNew(SButton)
					.IsFocusable(false)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(0.0f)
					.ToolTip(BuildTip)
					.OnClicked_Lambda([OnClick]() { return OnClick(); })
					[
						Tile
					]
				];
			}
			Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[Row];
		}
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeText(TEXT("Green is built and gold can be built now: click it. The cursor on a picture says what it gives and costs."), Tiny, CastleDimColor)
		];
	}
	else if (Tab == ETab::Recruit)
	{
		AddHeroChips(true);
		const bool bToHero = Chosen && !bHireToGarrison;
		const int32 Target = bToHero ? ChosenId : 0;
		const TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.0f));
		int32 Shown = 0;
		for (int32 Creature = 0; Creature < Faction.Creatures.Num(); ++Creature)
		{
			const int32 Level = Castles->GetLevel(*Castle, EHexBuildingType::Dwelling, Creature);
			if (Level <= 0)
			{
				continue;
			}
			const FHexCreature& Each = Faction.Creatures[Creature];
			const FHexCreatureStats& Stats = Each.GetStats(Level >= 2);
			const int32 Offered = Castle->Available.IsValidIndex(Creature) ? Castle->Available[Creature] : 0;
			const int32 Most = Castles->GetHireMax(*Castle, Creature);
			FString Price = FString::Printf(TEXT("%d gold"), Stats.Gold);
			if (!Each.CostResource.IsNone() && Each.CostResourceAmount > 0)
			{
				Price += FString::Printf(TEXT(" + %d %s"), Each.CostResourceAmount, *Each.CostResource.ToString().ToLower());
			}
			// what one costs, as icons green or red by whether there is enough for one
			TMap<FName, int32> OneCost;
			OneCost.Add(TEXT("Gold"), Stats.Gold);
			if (!Each.CostResource.IsNone() && Each.CostResourceAmount > 0)
			{
				OneCost.Add(Each.CostResource, Each.CostResourceAmount);
			}
			const FString Tip = Most > 0 ? FString::Printf(TEXT("Up to %d"), Most) : FString(Offered > 0 ? TEXT("Not enough to pay for one") : TEXT("None offered until next week"));
			const TSharedRef<SWidget> Card = SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.065f, 1.0f)).Padding(4.0f)
				.ToolTip(MakeCreatureTip(Stats, Each.bFlies, FString::Printf(TEXT("%s each; %d offered, %d more a week"), *Price, Offered, Castles->GetWeeklyGrowth(*Castle, Creature)), Creature, Level >= 2))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 5.0f, 0.0f)[MakePortrait(Creature, Level >= 2, 64.0f)]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[MakeText(Stats.DisplayName, Font, Offered > 0 ? CastleTextColor : CastleDimColor)]
						+ SVerticalBox::Slot().AutoHeight()[MakeText(FString::Printf(TEXT("%d to hire"), Offered), Small, Offered > 0 ? CastleHeadColor : CastleDimColor)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCostRow(OneCost, Castle->Owner, Small)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
							[
								MakeButton(TEXT("+1"), Most >= 1, Tip, [CastleId, Creature, Target](UHexCastles& Do, FString& Message) { Do.Hire(CastleId, Creature, 1, Target, Message); })
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MakeButton(FString::Printf(TEXT("All %d"), Most), Most >= 1, Tip, [CastleId, Creature, Target, Most](UHexCastles& Do, FString& Message) { Do.Hire(CastleId, Creature, Most, Target, Message); })
							]
						]
					]
				];
			Grid->AddSlot(Shown % 2, Shown / 2)[Card];
			++Shown;
		}
		if (Shown == 0)
		{
			Body->AddSlot().AutoHeight()[MakeText(TEXT("No dwelling is built yet."), Small, CastleDimColor)];
		}
		else
		{
			Body->AddSlot().AutoHeight()[Grid];
		}
		if (!Chosen)
		{
			Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)[MakeText(TEXT("What is hired goes to the garrison: a hero in or beside the castle could take it."), Tiny, CastleDimColor)];
		}

		// a war machine, as the developer asked on 2026-11-13 that it be bought here too: a catapult for the hero chosen or the garrison, by the
		// chip at the top
		AddSection(TEXT("War machines"));
		{
			const bool bForHero = Chosen && !bHireToGarrison;
			const bool bHas = bForHero ? Chosen->bCatapult : Castle->bCatapult;
			const bool bCanPay = Sites->CanPay(Castle->Owner, Config->CatapultCost);
			const FString Who = bForHero ? Chosen->Name : FString(TEXT("the garrison"));
			const FString Tip = bHas ? FString::Printf(TEXT("%s has one"), *Who) : bCanPay ? Sites->DescribeCost(Config->CatapultCost) : FString::Printf(TEXT("%s: not enough"), *Sites->DescribeCost(Config->CatapultCost));
			const int32 BuyFor = bForHero ? ChosenId : 0;
			Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					MakeText(FString::Printf(TEXT("Catapult for %s%s. %s"), *Who, bHas ? TEXT(": has one") : TEXT(""), *Sites->DescribeCost(Config->CatapultCost)), Small, bHas ? CastleDimColor : CastleTextColor)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(TEXT("Buy"), !bHas && bCanPay, Tip, [CastleId, BuyFor](UHexCastles& Do, FString& Message) { Do.BuyCatapult(CastleId, BuyFor, Message); })
				]
			];
		}

		// heroes to hire, one a day, as the developer asked on 2026-11-13: into the castle when nobody guards it, else before its gate
		AddSection(TEXT("Heroes for hire"));
		const bool bHeroPay = Sites->CanPay(Castle->Owner, Config->HeroCost);
		const FString HeroWhy = Castle->bHeroHiredToday ? FString(TEXT("A hero was hired here today")) : bHeroPay ? FString() : FString::Printf(TEXT("Costs %s"), *Sites->DescribeCost(Config->HeroCost));
		const TSharedRef<SHorizontalBox> Hire = SNew(SHorizontalBox);
		for (int32 Class = 0; Class < HeroConfig->Classes.Num(); ++Class)
		{
			// the panel at the cursor on the map of the hero it would be, as the developer asked on 2026-11-15
			FHexMapTip ClassTip;
			Heroes->GetClassTip(Class, Castle->Owner, ClassTip);
			ClassTip.Lines.Add(HeroWhy.IsEmpty() ? FString::Printf(TEXT("Hire for %s"), *Sites->DescribeCost(Config->HeroCost)) : HeroWhy);
			const TSharedRef<SButton> HireButton = MakeButton(HeroConfig->Classes[Class].DisplayName, HeroWhy.IsEmpty(), FString(),
				[CastleId, Class](UHexCastles& Do, FString& Message) { Do.HireHero(CastleId, Class, Message); });
			HireButton->SetToolTip(SHexMapTip::MakeToolTip(ClassTip));
			Hire->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)[HireButton];
		}
		Body->AddSlot().AutoHeight()[Hire];
		Body->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)[MakeText(FString::Printf(TEXT("%s, one a day, with the army a new hero starts with."), *Sites->DescribeCost(Config->HeroCost)), Tiny, CastleDimColor)];
	}
	else
	{
		// the garrison beside the hero chosen, their stacks and their catapults
		if (Visitors.Num() > 1)
		{
			AddHeroChips(false);
		}
		// the garrison and the hero chosen, slot by slot, as the developer asked on 2026-11-13 that stacks are put where they will stand in a
		// battle: a click chooses a stack and a click on a slot of either puts it there
		const TFunction<void(int32, int32, int32, int32, int32)> ArrangeHere = [WeakHeroes, Tell, CastleId](int32 FromKey, int32 FromSlot, int32 ToKey, int32 ToSlot, int32 Count)
		{
			FString Message;
			if (UHexCastles* Each = WeakHeroes.IsValid() ? WeakHeroes->GetCastles() : nullptr; Each && !Each->ArrangeStacks(CastleId, FromKey, FromSlot, ToKey, ToSlot, Count, Message)
				&& Tell && !Message.IsEmpty())
			{
				Tell(Message);
			}
		};
		const TFunction<const TArray<FHexStack>*(int32)> GetArmyHere = [WeakHeroes, CastleId](int32 Key) -> const TArray<FHexStack>*
		{
			const UHexCastles* Each = WeakHeroes.IsValid() ? WeakHeroes->GetCastles() : nullptr;
			const FHexCastle* Of = Each ? Each->FindCastle(CastleId) : nullptr;
			const FHexHero* Hero = Key != 0 && WeakHeroes.IsValid() ? WeakHeroes->FindHero(Key) : nullptr;
			return Key == 0 ? (Of ? &Of->Garrison : nullptr) : Hero ? &Hero->Army : nullptr;
		};
		// an arrow up in the corner of a stack whose dwelling here is upgraded, lit while it can be paid for, as the developer asked on 2026-11-13
		const auto MakeUpgradeCorner = [&](int32 Key)
		{
			return [this, Castles, Castle, Sites, CastleId, Key, WeakHeroes, Tell](int32 Slot, const FHexStack& Stack) -> TSharedPtr<SWidget>
			{
				if (Stack.bUpgraded || Stack.Faction != Castle->Faction || Castles->GetLevel(*Castle, EHexBuildingType::Dwelling, Stack.Creature) < 2)
				{
					return nullptr;
				}
				FString Why;
				const bool bCan = Castles->CanUpgrade(*Castle, Stack, &Why);
				const FString Tip = FString::Printf(TEXT("Upgrade for %s%s"), *Sites->DescribeCost(Castles->GetUpgradeCost(Stack)), bCan ? TEXT("") : *FString::Printf(TEXT(": %s"), *Why));
				return SNew(SButton)
					.IsFocusable(false)
					.IsEnabled(bCan)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(0.0f)
					.ToolTipText(FText::FromString(Tip))
					.OnClicked_Lambda([this, WeakHeroes, Tell, CastleId, Key, Slot]()
					{
						FString Message;
						if (UHexCastles* Each = WeakHeroes.IsValid() ? WeakHeroes->GetCastles() : nullptr)
						{
							Each->UpgradeStack(CastleId, Key, Slot, Message);
						}
						if (Tell && !Message.IsEmpty())
						{
							Tell(Message);
						}
						ChosenStackSlot = INDEX_NONE;
						return FReply::Handled();
					})
					[
						SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)
						.RenderOpacity(bCan ? 1.0f : 0.4f)
						[
							HexUnitTooltip::MakeAnimatedIcon(EHexStatIcon::LevelUp, 18.0f)
						]
					];
			};
		};
		AddSection(FString::Printf(TEXT("Garrison (%.0f)"), Castles->GetDefense(*Castle)));
		Body->AddSlot().AutoHeight()[MakeSlotRow(Castle->Garrison, 0, ArrangeHere, GetArmyHere, MakeUpgradeCorner(0))];
		if (Chosen)
		{
			AddSection(FString::Printf(TEXT("%s, %s (%.0f)"), *Chosen->Name, Chosen->CastleId == CastleId ? TEXT("in the castle") : TEXT("beside it"), Castles->GetAttack(*Chosen)));
			Body->AddSlot().AutoHeight()[MakeSlotRow(Chosen->Army, ChosenId, ArrangeHere, GetArmyHere, MakeUpgradeCorner(ChosenId))];
		}
		else
		{
			Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[MakeText(TEXT("No hero is in or beside the castle. One that comes takes stacks from the garrison and gives it its own."), Tiny, CastleDimColor)];
		}
		// the stack chosen split in its army
		Body->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[MakeSplitButtons(ArrangeHere, GetArmyHere)];
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeText(TEXT("Click a stack, then a slot: an empty one takes it, one of its kind joins it, another swaps with it. Shift moves half, Ctrl one creature. A stack stands in a battle on the row of its slot. The arrow up on a stack upgrades it."), Tiny, CastleDimColor)
		];

		// the catapults, as the developer asked on 2026-11-13: one for the garrison, which shoots from behind the walls, and one for each hero,
		// bought here or passed between them
		AddSection(TEXT("Catapults"));
		const bool bCanPay = Sites->CanPay(Castle->Owner, Config->CatapultCost);
		const FString Cost = Sites->DescribeCost(Config->CatapultCost);
		const auto AddCatapultRow = [&](const FString& Who, bool bHas, int32 HeroId, const TSharedRef<SWidget>& Pass)
		{
			const FString Tip = bHas ? FString::Printf(TEXT("%s has one"), *Who) : bCanPay ? Cost : FString::Printf(TEXT("%s: not enough"), *Cost);
			Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[MakeText(FString::Printf(TEXT("%s: %s"), *Who, bHas ? TEXT("has one") : TEXT("none")), Small, bHas ? CastleTextColor : CastleDimColor)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)[Pass]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeButton(TEXT("Buy"), !bHas && bCanPay, Tip, [CastleId, HeroId](UHexCastles& Do, FString& Message) { Do.BuyCatapult(CastleId, HeroId, Message); })
				]
			];
		};
		const bool bHeroHas = Chosen && Chosen->bCatapult;
		AddCatapultRow(TEXT("Garrison"), Castle->bCatapult, 0, Chosen
			? StaticCastSharedRef<SWidget>(MakeButton(TEXT("→"), Castle->bCatapult && !bHeroHas, FString::Printf(TEXT("To %s"), *Chosen->Name),
				[CastleId, ChosenId](UHexCastles& Do, FString& Message) { Do.MoveCatapult(CastleId, ChosenId, true, Message); }))
			: StaticCastSharedRef<SWidget>(SNew(SBox)));
		if (Chosen)
		{
			AddCatapultRow(Chosen->Name, bHeroHas, ChosenId, MakeButton(TEXT("←"), bHeroHas && !Castle->bCatapult, TEXT("To the garrison"),
				[CastleId, ChosenId](UHexCastles& Do, FString& Message) { Do.MoveCatapult(CastleId, ChosenId, false, Message); }));
		}
		Body->AddSlot().AutoHeight()[MakeText(FString::Printf(TEXT("%s each. A hero's shoots at the walls it attacks, the garrison's at the attackers."), *Cost), Tiny, CastleDimColor)];
	}
}

#undef LOCTEXT_NAMESPACE
