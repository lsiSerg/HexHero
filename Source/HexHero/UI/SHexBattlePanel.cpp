// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexBattlePanel.h"
#include "HexBattleArena.h"
#include "HexBattleConfig.h"
#include "HexBattles.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "HexUnitTooltip.h"
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexBattlePanel"

namespace
{
	const FLinearColor PanelBack(0.02f, 0.02f, 0.03f, 0.78f);

	const FSlateBrush* WhiteBrush()
	{
		return FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	}

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}
}

TSharedRef<SWidget> SHexBattlePanel::MakeButton(const FText& Label, TFunction<void()> OnClick, TFunction<bool()> IsEnabled, const FText& Tip)
{
	return SNew(SBox).Padding(FMargin(3.0f, 0.0f))
	[
		SNew(SButton)
		.IsFocusable(false)
		.ToolTipText(Tip)
		.IsEnabled_Lambda([IsEnabled]() { return IsEnabled(); })
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		.ContentPadding(FMargin(10.0f, 5.0f))
		[
			SNew(STextBlock).Text(Label).Font(Font(10, true))
		]
	];
}

void SHexBattlePanel::Construct(const FArguments& InArgs)
{
	Arena = InArgs._Arena;
	OnProject = InArgs._OnProject;
	SetCanTick(true);
	TWeakObjectPtr<AHexBattleArena> Weak = Arena;
	const auto Can = [Weak]() { return Weak.IsValid() && Weak->CanOrder(); };
	const auto Running = [Weak]() { return Weak.IsValid() && Weak->GetBattles() && !Weak->GetBattles()->GetBattle().IsOver(); };
	const auto Person = [Weak]() { return Weak.IsValid() && Weak->GetBattles() && Weak->GetBattles()->GetPersonSide() != INDEX_NONE; };

	// the spells of the hero whose turn it is
	TSharedRef<SVerticalBox> Spells = SNew(SVerticalBox);
	if (const UHexBattleConfig* Config = UHexBattleConfig::Load())
	{
		for (int32 Index = 0; Index < Config->Spells.Num(); ++Index)
		{
			const FHexSpell& Spell = Config->Spells[Index];
			Spells->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				MakeButton(FText::FromString(FString::Printf(TEXT("%s  (%d mana)"), *Spell.DisplayName, Spell.ManaCost)),
					[this, Weak, Index]() { if (Weak.IsValid()) { Weak->AimSpell(Index); } bSpellsOpen = false; },
					[Weak, Index, Can]()
					{
						if (!Can())
						{
							return false;
						}
						const FHexBattle& Battle = Weak->GetBattles()->GetBattle();
						const int32 Side = Battle.GetUnits()[Battle.GetActive()].Side;
						return Battle.CanCastAny(Side) && Battle.GetConfig()->Spells[Index].ManaCost <= Battle.GetMana(Side);
					},
					FText::FromString(Spell.Description))
			];
		}
	}

	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		// the sides and the round
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 12.0f)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBack).Padding(FMargin(14.0f, 6.0f)).Visibility(EVisibility::HitTestInvisible)
			[
				SNew(STextBlock).Font(Font(12, true)).ColorAndOpacity(FLinearColor::White)
				.Text_Lambda([Weak]()
				{
					if (!Weak.IsValid() || !Weak->GetBattles())
					{
						return FText::GetEmpty();
					}
					const UHexBattles* Battles = Weak->GetBattles();
					const FHexBattle& Battle = Battles->GetBattle();
					const auto Side = [&](int32 Index)
					{
						const FHexBattleSideSetup& Each = Battle.GetSide(Index);
						return Each.HeroId != 0 ? FString::Printf(TEXT("%s (mana %d)"), *Battles->GetSideName(Index), Battle.GetMana(Index)) : Battles->GetSideName(Index);
					};
					return FText::FromString(FString::Printf(TEXT("%s   against   %s     Round %d"), *Side(0), *Side(1), Battle.GetRound()));
				})
			]
		]
		// the log of the last events
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(14.0f, 0.0f, 0.0f, 120.0f)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBack).Padding(8.0f).Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SBox).WidthOverride(340.0f)
				[
					SNew(STextBlock).Font(Font(9)).ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f)).AutoWrapText(true)
					.Text_Lambda([Weak]() { return Weak.IsValid() ? FText::FromString(FString::Join(Weak->GetLog(), TEXT("\n"))) : FText::GetEmpty(); })
				]
			]
		]
		// the unit under the cursor, beside it, as the developer asked on 2026-11-12: its name, its stats as icons with numbers, what it can
		// and the spells on it
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		[
			SAssignNew(TooltipBox, SBorder).BorderImage(WhiteBrush()).Padding(FMargin(10.0f, 7.0f))
			.BorderBackgroundColor_Lambda([]() { return FLinearColor(0.02f, 0.02f, 0.03f, UHexUiConfig::Load()->TooltipOpacity); })
			.RenderTransform_Lambda([this]() { return FSlateRenderTransform(TooltipPosition); })
			.Visibility_Lambda([this]() { return TooltipKey.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
		]
		// what a click does, the order of the stacks and the buttons
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SAssignNew(SpellBox, SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBack).Padding(6.0f)
				.Visibility_Lambda([this]() { return bSpellsOpen ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					Spells
				]
			]
			// whose turn it is, and what a click where the cursor is would do with what it is likely to deal and kill, as the developer asked on
			// 2026-11-12
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBack).Padding(FMargin(14.0f, 5.0f)).Visibility(EVisibility::HitTestInvisible)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(Font(9)).ColorAndOpacity(FLinearColor(0.8f, 0.85f, 0.9f, 1.0f))
						.Text_Lambda([Weak]() { return Weak.IsValid() ? FText::FromString(Weak->GetTurnText()) : FText::GetEmpty(); })
						.Visibility_Lambda([Weak]() { return Weak.IsValid() && !Weak->GetTurnText().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Font(Font(13, true)).ColorAndOpacity(FLinearColor(1.0f, 0.88f, 0.55f, 1.0f))
						.Text_Lambda([Weak]() { return Weak.IsValid() ? FText::FromString(Weak->GetActionText()) : FText::GetEmpty(); })
						.Visibility_Lambda([Weak]() { return Weak.IsValid() && !Weak->GetActionText().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SAssignNew(QueueBox, SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SAssignNew(ButtonBar, SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBack).Padding(6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("Wait", "Wait (Z)"), [Weak]() { if (Weak.IsValid()) { Weak->OrderWait(); } }, Can,
						LOCTEXT("WaitTip", "The stack acts after all the others this round, the slowest first"))]
					+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("Defend", "Defend (Space)"), [Weak]() { if (Weak.IsValid()) { Weak->OrderDefend(); } }, Can,
						LOCTEXT("DefendTip", "The stack ends its turn with more Defense until its next one"))]
					+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("Spell", "Spell"), [this]() { bSpellsOpen = !bSpellsOpen; },
						[Weak, Can]()
						{
							if (!Can())
							{
								return false;
							}
							const FHexBattle& Battle = Weak->GetBattles()->GetBattle();
							return Battle.CanCastAny(Battle.GetUnits()[Battle.GetActive()].Side);
						},
						LOCTEXT("SpellTip", "The hero casts a spell, once a round, for its mana; choose one, then click where it goes, the right button to keep it"))]
					+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("Auto", "Auto"), [Weak]() { if (Weak.IsValid()) { Weak->OrderAuto(); } },
						[Running, Person]() { return Running() && Person(); }, LOCTEXT("AutoTip", "The game fights for you, and again you with a second press"))]
					+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("Quick", "Quick"), [Weak]() { if (Weak.IsValid()) { Weak->OrderQuick(); } }, Running,
						LOCTEXT("QuickTip", "The game fights the rest of the battle at once and shows how it ended"))]
					+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("Retreat", "Retreat"), [Weak]() { if (Weak.IsValid()) { Weak->OrderRetreat(); } },
						[Running, Person]() { return Running() && Person(); }, LOCTEXT("RetreatTip", "The hero leaves the battle and the map, and its army is lost"))]
				]
			]
		]
		// how it ended
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(ResultBox, SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.9f)).Padding(18.0f)
			.Visibility_Lambda([Weak]()
			{
				return Weak.IsValid() && Weak->GetBattles() && Weak->GetBattles()->GetBattle().IsOver() && !Weak->IsPlaying() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Font(16, true)).ColorAndOpacity(FLinearColor(1.0f, 0.9f, 0.6f, 1.0f))
					.Text_Lambda([Weak]()
					{
						if (!Weak.IsValid() || !Weak->GetBattles())
						{
							return FText::GetEmpty();
						}
						const UHexBattles* Battles = Weak->GetBattles();
						const int32 Person = Battles->GetPersonSide();
						const int32 Winner = Battles->GetBattle().GetWinner();
						return Person == INDEX_NONE ? FText::FromString(FString::Printf(TEXT("%s wins"), *Battles->GetSideName(Winner)))
							: Person == Winner ? LOCTEXT("Victory", "Victory") : LOCTEXT("Defeat", "Defeat");
					})
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 8.0f)
				[
					SNew(SBox).MaxDesiredWidth(520.0f)
					[
						SNew(STextBlock).Font(Font(10)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true).Justification(ETextJustify::Center)
						.Text_Lambda([Weak]()
						{
							if (!Weak.IsValid() || !Weak->GetBattles())
							{
								return FText::GetEmpty();
							}
							const UHexBattles* Battles = Weak->GetBattles();
							const FHexBattle& Battle = Battles->GetBattle();
							FString Text = Battles->GetResultText();
							for (int32 Side = 0; Side < 2; ++Side)
							{
								FString Lost;
								for (const FHexBattleUnit& Unit : Battle.GetUnits())
								{
									if (Unit.Side == Side && Unit.StartCount > Unit.Count)
									{
										Lost += FString::Printf(TEXT("%s%d %s"), Lost.IsEmpty() ? TEXT("") : TEXT(", "), Unit.StartCount - Unit.Count, *Unit.Stats.DisplayName);
									}
								}
								Text += FString::Printf(TEXT("\n%s lost: %s"), *Battles->GetSideName(Side), Lost.IsEmpty() ? TEXT("nothing") : *Lost);
							}
							const int32 Winner = Battle.GetWinner();
							if (Battle.GetSide(Winner).HeroId != 0)
							{
								Text += FString::Printf(TEXT("\n%s gains %d experience"), *Battles->GetSideName(Winner),
									FMath::RoundToInt32(Battle.GetKilledHealth(Winner) * Battles->GetConfig()->ExperiencePerHealth));
							}
							return FText::FromString(Text);
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					MakeButton(LOCTEXT("Close", "OK"), [Weak]() { if (Weak.IsValid()) { Weak->OrderClose(); } }, [Weak]() { return Weak.IsValid(); },
						LOCTEXT("CloseTip", "Back to the planet"))
				]
			]
		]
	];
}

bool SHexBattlePanel::IsCursorOnButtons() const
{
	const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
	for (const TSharedPtr<SWidget>& Each : {ButtonBar, SpellBox, ResultBox})
	{
		if (Each.IsValid() && Each->GetVisibility().IsVisible() && Each->GetTickSpaceGeometry().IsUnderLocation(Cursor))
		{
			return true;
		}
	}
	return false;
}

void SHexBattlePanel::RebuildQueue()
{
	const UHexBattles* Battles = Arena.IsValid() ? Arena->GetBattles() : nullptr;
	if (!Battles || !QueueBox.IsValid())
	{
		return;
	}
	const FHexBattle& Battle = Battles->GetBattle();
	const TArray<int32> Queue = Battle.GetQueue(12);
	FString Key;
	for (const int32 Unit : Queue)
	{
		Key += FString::Printf(TEXT("%d:%d "), Unit, Arena->GetShownCount(Unit));
	}
	if (Key == QueueKey)
	{
		return;
	}
	QueueKey = Key;
	QueueBox->ClearChildren();
	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		const FHexBattleUnit& Unit = Battle.GetUnits()[Queue[Index]];
		FLinearColor Color = Battles->GetSideColor(Unit.Side);
		Color.A = Index == 0 ? 0.95f : 0.7f;
		QueueBox->AddSlot().AutoWidth().Padding(2.0f, 0.0f)
		[
			SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Color).Padding(FMargin(6.0f, 3.0f))
			[
				SNew(SBox).MinDesiredWidth(Index == 0 ? 96.0f : 72.0f)
				[
					SNew(STextBlock).Font(Font(Index == 0 ? 9 : 8, Index == 0)).ColorAndOpacity(FLinearColor::White)
					.Text(FText::FromString(FString::Printf(TEXT("%s\n%d"), *Unit.Stats.DisplayName, Arena->GetShownCount(Queue[Index]))))
				]
			]
		];
	}
}

void SHexBattlePanel::RebuildTooltip(const FGeometry& Geometry)
{
	const UHexBattles* Battles = Arena.IsValid() ? Arena->GetBattles() : nullptr;
	if (!Battles || !TooltipBox.IsValid())
	{
		return;
	}
	const FHexBattle& Battle = Battles->GetBattle();
	const int32 Unit = Arena->GetHoverUnit();
	const bool bObstacle = Unit == INDEX_NONE && Arena->HasHover() && Battle.IsObstacle(Arena->GetHoverHex());
	const FHexBattleWall* Wall = Unit == INDEX_NONE && Arena->HasHover() ? Battle.FindWall(Arena->GetHoverHex()) : nullptr;
	FString Key;
	if (Unit != INDEX_NONE)
	{
		const FHexBattleUnit& Each = Battle.GetUnits()[Unit];
		Key = FString::Printf(TEXT("%d %d %d %d %d %d %d %d %d"), Unit, Arena->GetShownCount(Unit), Each.TopHealth, Each.ShotsLeft, Each.Effects.Num(), Battle.GetAttack(Each),
			Battle.GetDefense(Each), Battle.GetSpeed(Each), Each.RetaliationsLeft);
	}
	else if (bObstacle)
	{
		Key = TEXT("obstacle");
	}
	else if (Wall)
	{
		Key = FString::Printf(TEXT("wall %d %d %d"), Wall->Hex.Q, Wall->Hex.R, Arena->GetShownWallHealth(int32(Wall - Battle.GetWalls().GetData())) * 2 + (Battle.IsGateJammed() ? 1 : 0));
	}
	// it stands beside the cursor, on the other side of it where it would leave the screen
	const UHexUiConfig* Ui = UHexUiConfig::Load();
	const FVector2D Cursor = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	const FVector2D Size = TooltipBox->GetDesiredSize();
	const FVector2D Screen = Geometry.GetLocalSize();
	FVector2D Position = Cursor + FVector2D(Ui->TooltipOffset, Ui->TooltipOffset);
	if (Position.X + Size.X > Screen.X - 8.0)
	{
		Position.X = Cursor.X - Ui->TooltipOffset - Size.X;
	}
	if (Position.Y + Size.Y > Screen.Y - 8.0)
	{
		Position.Y = Cursor.Y - Ui->TooltipOffset - Size.Y;
	}
	TooltipPosition = FVector2D::Max(Position, FVector2D(4.0, 4.0));
	if (Key == TooltipKey)
	{
		return;
	}
	TooltipKey = Key;
	if (Key.IsEmpty())
	{
		TooltipBox->SetContent(SNullWidget::NullWidget);
		return;
	}
	if (Wall)
	{
		// its health as the field shows it, which a blow takes down when it lands
		const int32 Shown = Arena->GetShownWallHealth(int32(Wall - Battle.GetWalls().GetData()));
		const FString Part = Wall->Part == EHexWallPart::Gate ? TEXT("The gate") : Wall->Part == EHexWallPart::Tower ? TEXT("A tower") : TEXT("The wall");
		const FString What = Shown <= 0 ? TEXT("fallen: anyone walks over the rubble")
			: Wall->Part == EHexWallPart::Gate ? (Battle.IsGateJammed() ? TEXT("jammed open: anyone goes through") : TEXT("opens for the defenders, shuts behind them"))
			: Wall->Part == EHexWallPart::Tower ? TEXT("its shooters only a catapult harms") : TEXT("a catapult or a stack beside it breaks it");
		TooltipBox->SetContent(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(12, true)).ColorAndOpacity(FLinearColor(1.0f, 0.88f, 0.6f, 1.0f))
				.Text(FText::FromString(Shown > 0 ? FString::Printf(TEXT("%s: %d of %d"), *Part, Shown, Wall->MaxHealth) : Part))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)[SNew(STextBlock).Font(Font(9)).ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f)).Text(FText::FromString(What))]);
		return;
	}
	if (bObstacle)
	{
		TooltipBox->SetContent(SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(FLinearColor::White).Text(LOCTEXT("Obstacle", "An obstacle: nothing walks onto it")));
		return;
	}
	const FHexBattleUnit& Each = Battle.GetUnits()[Unit];
	const int32 Count = Arena->GetShownCount(Unit);
	FHexUnitTooltipData Data = HexUnitTooltip::FromStats(Each.Stats);
	Data.Title = FString::Printf(TEXT("%d %s"), Count, *Each.Stats.DisplayName);
	if (!Each.IsMachine() || Each.bTower)
	{
		HexUnitTooltip::SetRank(Data, Each.Kind.Creature, Each.Kind.bUpgraded);
	}
	Data.Subtitle = Battles->GetSideName(Each.Side);
	Data.TitleColor = FLinearColor::LerpUsingHSV(Battles->GetSideColor(Each.Side), FLinearColor::White, 0.35f);
	// its own stats, and what its hero, its spells and defending change of them as it is now
	Data.AttackBonus = Battle.GetAttack(Each) - Each.Stats.Attack;
	Data.DefenseBonus = Battle.GetDefense(Each) - Each.Stats.Defense;
	Data.SpeedBonus = Battle.GetSpeed(Each) - Each.Stats.Speed;
	// the health of its top creature once the field has shown the blows that took it down
	Data.TopHealth = Count == Each.Count ? Each.TopHealth : -1;
	Data.Shots = Each.bShoots ? Each.ShotsLeft : -1;
	Data.Morale = Each.Morale;
	Data.Luck = Each.Luck;
	Data.bFlies = Each.bFlies;
	for (const FHexBattleEffect& Effect : Each.Effects)
	{
		if (Battle.GetConfig() && Battle.GetConfig()->Spells.IsValidIndex(Effect.Spell))
		{
			Data.Effects += FString::Printf(TEXT("%s%s (%d %s)"), Data.Effects.IsEmpty() ? TEXT("") : TEXT(", "), *Battle.GetConfig()->Spells[Effect.Spell].DisplayName, Effect.RoundsLeft,
				Effect.RoundsLeft == 1 ? TEXT("round") : TEXT("rounds"));
		}
	}
	if (Each.bDefending)
	{
		Data.Effects += (Data.Effects.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(TEXT("Defends"));
	}
	if (Each.RetaliationsLeft <= 0)
	{
		Data.Extra = TEXT("Has struck back this round");
	}
	TooltipBox->SetContent(HexUnitTooltip::Make(Data));
}

void SHexBattlePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	RebuildQueue();
	RebuildTooltip(AllottedGeometry);
	if (Arena.IsValid() && !Arena->CanOrder())
	{
		bSpellsOpen = false;
	}
}

int32 SHexBattlePanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const UHexBattles* Battles = Arena.IsValid() ? Arena->GetBattles() : nullptr;
	if (Battles && OnProject)
	{
		const float Scale = FMath::Max(AllottedGeometry.Scale, 0.01f);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo CountFont = Font(10, true);
		const FHexBattle& Battle = Battles->GetBattle();
		// the number of each stack at the foot of its figure, in the color of its side
		for (int32 Index = 0; Index < Battle.GetUnits().Num(); ++Index)
		{
			FVector World;
			FVector2D Screen;
			if (!Arena->GetUnitPoint(Index, World) || Arena->GetShownCount(Index) <= 0 || !OnProject(World, Screen))
			{
				continue;
			}
			const FString Text = FString::FromInt(Arena->GetShownCount(Index));
			const FVector2D TextSize = Measure->Measure(Text, CountFont);
			const FVector2D Box = TextSize + FVector2D(10.0, 4.0);
			const FVector2D Local = Screen / Scale + FVector2D(Battle.GetUnits()[Index].Side == 0 ? 16.0 : -16.0 - Box.X, -4.0);
			FLinearColor Color = Battles->GetSideColor(Battle.GetUnits()[Index].Side) * 0.7f;
			Color.A = 0.9f;
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Box, FSlateLayoutTransform(Local)), WhiteBrush(), ESlateDrawEffect::None, Color);
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Local + FVector2D(5.0, 2.0))), Text,
				CountFont, ESlateDrawEffect::None, FLinearColor::White);
		}
		// the health of each part of the wall that stands, over it
		const FSlateFontInfo WallFont = Font(9, true);
		for (int32 Index = 0; Index < Battle.GetWalls().Num(); ++Index)
		{
			FVector World;
			FVector2D Screen;
			const FHexBattleWall& Wall = Battle.GetWalls()[Index];
			// the gate, the towers and what was struck, by the health the field shows; a whole section of the wall says nothing
			const int32 Health = Arena->GetShownWallHealth(Index);
			if ((Wall.Part == EHexWallPart::Wall && Health >= Wall.MaxHealth) || !Arena->GetWallPoint(Index, World) || !OnProject(World, Screen))
			{
				continue;
			}
			const FString Text = FString::Printf(TEXT("%d"), Health);
			const FVector2D TextSize = Measure->Measure(Text, WallFont);
			const FVector2D Box = TextSize + FVector2D(8.0, 3.0);
			const FVector2D Local = Screen / Scale - Box * 0.5;
			const float Share = float(Health) / float(FMath::Max(Wall.MaxHealth, 1));
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Box, FSlateLayoutTransform(Local)), WhiteBrush(), ESlateDrawEffect::None,
				FLinearColor(0.05f, 0.05f, 0.06f, 0.75f));
			FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(FVector2D(Box.X * Share, 2.0), FSlateLayoutTransform(Local + FVector2D(0.0, Box.Y - 2.0))),
				WhiteBrush(), ESlateDrawEffect::None, FLinearColor::LerpUsingHSV(FLinearColor(1.0f, 0.25f, 0.15f, 1.0f), FLinearColor(0.5f, 1.0f, 0.4f, 1.0f), Share));
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Local + FVector2D(4.0, 1.0))), Text, WallFont,
				ESlateDrawEffect::None, FLinearColor(0.9f, 0.9f, 0.85f, 1.0f));
		}
		// what floats up
		const FSlateFontInfo PopupFont = Font(12, true);
		for (const FHexBattlePopup& Popup : Arena->GetPopups())
		{
			FVector2D Screen;
			if (!OnProject(Popup.World, Screen))
			{
				continue;
			}
			const float Share = Popup.Age / FMath::Max(Popup.Life, 0.01f);
			const FVector2D TextSize = Measure->Measure(Popup.Text, PopupFont);
			const FVector2D Local = Screen / Scale - FVector2D(TextSize.X * 0.5, 30.0 * Share + TextSize.Y);
			FLinearColor Color = Popup.Color;
			Color.A = 1.0f - FMath::Clamp((Share - 0.6f) / 0.4f, 0.0f, 1.0f);
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Local + FVector2D(1.0, 1.0))), Popup.Text,
				PopupFont, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, Color.A * 0.8f));
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Local)), Popup.Text, PopupFont,
				ESlateDrawEffect::None, Color);
		}
		Layer += 4;
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer, InWidgetStyle, bParentEnabled);
}

#undef LOCTEXT_NAMESPACE
