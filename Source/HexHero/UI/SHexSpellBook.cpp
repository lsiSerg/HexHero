// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHexSpellBook.h"
#include "HexBattleConfig.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HexSpellBook"

namespace
{
	const FLinearColor BookShade(0.0f, 0.0f, 0.0f, 0.45f);
	const FLinearColor BookBack(0.05f, 0.04f, 0.035f, 0.97f);
	const FLinearColor BookCard(0.12f, 0.1f, 0.08f, 1.0f);
	const FLinearColor BookCardOff(0.08f, 0.075f, 0.07f, 1.0f);
	const FLinearColor BookText(0.92f, 0.88f, 0.8f, 1.0f);
	const FLinearColor BookDim(0.6f, 0.57f, 0.52f, 1.0f);
	const FLinearColor BookGold(1.0f, 0.82f, 0.35f, 1.0f);
	constexpr float BookIcon = 56.0f;

	FLinearColor GetSchoolColor(EHexSpellSchool School)
	{
		switch (School)
		{
		case EHexSpellSchool::Air: return FLinearColor(0.6f, 0.85f, 1.0f, 1.0f);
		case EHexSpellSchool::Earth: return FLinearColor(0.85f, 0.65f, 0.35f, 1.0f);
		case EHexSpellSchool::Fire: return FLinearColor(1.0f, 0.45f, 0.2f, 1.0f);
		default: return FLinearColor(0.35f, 0.7f, 1.0f, 1.0f);
		}
	}

	struct FBrushKey
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
	};
}

const FSlateBrush* HexUiBrushes::Get(UTexture2D* Texture, float Size, int32 Frames)
{
	if (!Texture)
	{
		return nullptr;
	}
	static TMap<FString, FBrushKey> Cache;
	const FString Key = FString::Printf(TEXT("%s/%.0f/%d"), *Texture->GetPathName(), Size, Frames);
	if (const FBrushKey* Found = Cache.Find(Key))
	{
		return Found->Brush.Get();
	}
	FBrushKey& Made = Cache.Add(Key);
	Made.Texture.Reset(Texture);
	Made.Brush = MakeShared<FSlateBrush>();
	Made.Brush->SetResourceObject(Texture);
	Made.Brush->ImageSize = FVector2D(Size, Size);
	Made.Brush->DrawAs = ESlateBrushDrawType::Image;
	if (Frames > 1)
	{
		Made.Brush->SetUVRegion(FBox2f(FVector2f::ZeroVector, FVector2f(1.0f / float(Frames), 1.0f)));
	}
	return Made.Brush.Get();
}

void SHexSpellBook::Construct(const FArguments& InArgs)
{
	OnCast = InArgs._OnCast;
	OnClosed = InArgs._OnClosed;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	SetVisibility(EVisibility::Collapsed);
	ChildSlot
	[
		SNew(SOverlay)
		// the shade takes every click outside the book, which closes it, so nothing behind it is picked
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(BookShade)
			.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) { Close(); return FReply::Handled(); })
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(760.0f).HeightOverride(520.0f)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(BookBack).Padding(FMargin(14.0f, 10.0f))
				.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
				[
					SAssignNew(Body, SVerticalBox)
				]
			]
		]
	];
}

void SHexSpellBook::Open(const FString& Title, int32 Mana, int32 MaxMana, const TArray<FHexSpellBookEntry>& Entries)
{
	ShownTitle = Title;
	ShownMana = Mana;
	ShownMaxMana = MaxMana;
	ShownEntries = Entries;
	bOpen = true;
	SetVisibility(EVisibility::Visible);
	Rebuild();
}

void SHexSpellBook::Close()
{
	if (!bOpen)
	{
		return;
	}
	bOpen = false;
	SetVisibility(EVisibility::Collapsed);
	OnClosed.ExecuteIfBound();
}

FReply SHexSpellBook::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
	}
	// nothing else reaches the game while the book is open
	return FReply::Handled();
}

void SHexSpellBook::Rebuild()
{
	Body->ClearChildren();
	const UHexBattleConfig* Config = UHexBattleConfig::Load();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Head = FCoreStyle::GetDefaultFontStyle("Bold", 15);
	const FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	// the title, the mana, and a button that closes it
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Head).ColorAndOpacity(BookGold).Text(FText::FromString(ShownTitle))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock).Font(Bold).ColorAndOpacity(FLinearColor(0.45f, 0.7f, 1.0f, 1.0f))
			.Text(FText::Format(LOCTEXT("Mana", "Mana {0} of {1}"), FText::AsNumber(ShownMana), FText::AsNumber(ShownMaxMana)))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(8.0f, 2.0f))
			.OnClicked_Lambda([this]() { Close(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Bold).Text(LOCTEXT("Close", "x"))
			]
		]
	];
	// the tabs of the schools
	const TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (int32 Each = -1; Each < int32(EHexSpellSchool::Num); ++Each)
	{
		const bool bShown = Each == School;
		const FLinearColor Color = Each < 0 ? BookText : GetSchoolColor(EHexSpellSchool(Each));
		Tabs->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton).IsFocusable(false).ContentPadding(FMargin(12.0f, 3.0f))
			.ButtonColorAndOpacity(bShown ? FLinearColor(0.55f, 0.42f, 0.18f, 1.0f) : FLinearColor(0.2f, 0.18f, 0.15f, 1.0f))
			.OnClicked_Lambda([this, Each]() { School = Each; Rebuild(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Bold).ColorAndOpacity(Color).Text(Each < 0 ? LOCTEXT("All", "All") : FText::FromString(GetSpellSchoolName(EHexSpellSchool(Each))))
			]
		];
	}
	Body->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 6.0f)[Tabs];

	// the spells as cards, by level
	TArray<FHexSpellBookEntry> Sorted = ShownEntries.FilterByPredicate([this, Config](const FHexSpellBookEntry& Entry)
	{
		return Config->Spells.IsValidIndex(Entry.Spell) && (School < 0 || int32(Config->Spells[Entry.Spell].School) == School);
	});
	Sorted.Sort([Config](const FHexSpellBookEntry& A, const FHexSpellBookEntry& B)
	{
		const FHexSpell& First = Config->Spells[A.Spell];
		const FHexSpell& Second = Config->Spells[B.Spell];
		return First.Level != Second.Level ? First.Level < Second.Level : First.School != Second.School ? First.School < Second.School : A.Spell < B.Spell;
	});
	const TSharedRef<SWrapBox> Cards = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0, 6.0));
	for (const FHexSpellBookEntry& Entry : Sorted)
	{
		const FHexSpell& Spell = Config->Spells[Entry.Spell];
		const FSlateBrush* Icon = HexUiBrushes::Get(Spell.Icon.LoadSynchronous(), BookIcon);
		const FLinearColor Tint = Entry.bCastable ? FLinearColor::White : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);
		const FString Tip = Spell.Description + (Entry.Detail.IsEmpty() ? FString() : TEXT("\n") + Entry.Detail);
		const int32 Index = Entry.Spell;
		Cards->AddSlot()
		[
			// a spell that cannot be cast now is see-through as a whole, as the developer found its red text hard to read on 2026-11-15; why
			// stands in its tooltip
			SNew(SBox).WidthOverride(172.0f).RenderOpacity(Entry.bCastable ? 1.0f : 0.45f)
			[
				SNew(SButton).IsFocusable(false).ContentPadding(FMargin(4.0f)).ButtonColorAndOpacity(Entry.bCastable ? BookCard : BookCardOff)
				.ToolTipText(FText::FromString(Tip))
				.OnClicked_Lambda([this, Index, bCastable = Entry.bCastable]()
				{
					if (bCastable)
					{
						OnCast.ExecuteIfBound(Index);
						Close();
					}
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(BookIcon).HeightOverride(BookIcon)
						[
							Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon).ColorAndOpacity(Tint))
								: StaticCastSharedRef<SWidget>(SNew(SBorder).BorderImage(White).BorderBackgroundColor(GetSchoolColor(Spell.School) * 0.5f))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Bold).ColorAndOpacity(Entry.bCastable ? BookText : BookDim).Text(FText::FromString(Spell.DisplayName)).AutoWrapText(true)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Small).ColorAndOpacity(GetSchoolColor(Spell.School))
							.Text(FText::Format(LOCTEXT("Level", "{0}, level {1}"), FText::FromString(GetSpellSchoolName(Spell.School)), FText::AsNumber(Spell.Level)))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Small).ColorAndOpacity(Entry.ManaCost <= ShownMana ? FLinearColor(0.45f, 0.7f, 1.0f, 1.0f) : FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
							.Text(FText::Format(LOCTEXT("Cost", "{0} mana"), FText::AsNumber(Entry.ManaCost)))
						]
					]
				]
			]
		];
	}
	if (Sorted.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 20.0f)
		[
			SNew(STextBlock).Font(Bold).ColorAndOpacity(BookDim).Text(LOCTEXT("None", "No spells of this school yet: a mage guild in a castle of your own teaches them"))
		];
		return;
	}
	Body->AddSlot().FillHeight(1.0f)
	[
		SNew(SScrollBox) + SScrollBox::Slot()[Cards]
	];
}

#undef LOCTEXT_NAMESPACE

TSharedRef<SWidget> HexUiBrushes::MakeFramed(UTexture2D* Texture, float Size, const FLinearColor& Frame, const FLinearColor& Tint)
{
	// the frame is the outline of a rounded box, drawn by its shader alike on every side, as a border of a few pixels of padding came out of
	// unlike widths once the interface was scaled to the screen, which the developer saw on 2026-11-15
	static TMap<uint32, TSharedPtr<FSlateBrush>> Outlines;
	const FLinearColor Opaque(Frame.R, Frame.G, Frame.B, 1.0f);
	const uint32 Key = Opaque.ToFColor(true).ToPackedARGB();
	TSharedPtr<FSlateBrush>& Outline = Outlines.FindOrAdd(Key);
	if (!Outline.IsValid())
	{
		Outline = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::Transparent, 2.0f, Opaque, 2.0f);
	}
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateBrush* Brush = Get(Texture, Size);
	return SNew(SBox)
		.WidthOverride(Size)
		.HeightOverride(Size)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(White).ColorAndOpacity(FLinearColor(0.07f, 0.07f, 0.09f, 1.0f))
			]
			+ SOverlay::Slot()
			[
				Brush ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush).ColorAndOpacity(Tint)) : StaticCastSharedRef<SWidget>(SNew(SBox))
			]
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(Outline.Get())
			]
		];
}
