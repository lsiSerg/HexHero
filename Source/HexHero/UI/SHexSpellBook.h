// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UTexture2D;
struct FSlateBrush;

/** A spell as the spell book shows it: its index in DA_HexBattle, what it costs this hero, whether it may be cast now and a line of what it
 *  would do */
struct FHexSpellBookEntry
{
	int32 Spell = INDEX_NONE;
	int32 ManaCost = 0;
	bool bCastable = false;
	FString Detail;
};

/** The brush of a picture of a given size, cached for the run with its texture kept loaded; the first frame of a strip of Frames */
namespace HexUiBrushes
{
	const FSlateBrush* Get(UTexture2D* Texture, float Size, int32 Frames = 1);

	/** A picture in a frame of a color, as the portraits of heroes and the pictures of castles show in the lists, tips and panels, as the
	 *  developer asked on 2026-11-15: a dark square without a picture */
	TSharedRef<class SWidget> MakeFramed(UTexture2D* Texture, float Size, const FLinearColor& Frame, const FLinearColor& Tint = FLinearColor::White);
}

DECLARE_DELEGATE_OneParam(FOnHexSpellCast, int32);

/**
 *  The spell book of a hero, as the developer asked on 2026-11-15: a window in the middle of the screen over a shade that blocks what lies
 *  behind it, with the spells it knows as pictures with their names, levels and mana, by school in tabs (All, Air, Earth, Fire, Water); the
 *  cursor on one says what it does. In a battle a click on a spell it may cast now chooses it, and the next click on the field casts it; on
 *  the map the book only shows them. A click on the shade or Escape closes it
 */
class SHexSpellBook : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SHexSpellBook) {}
		/** A spell that may be cast was clicked, by its index */
		SLATE_EVENT(FOnHexSpellCast, OnCast)
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Shows the book: its title, the mana of the hero and its spells */
	void Open(const FString& Title, int32 Mana, int32 MaxMana, const TArray<FHexSpellBookEntry>& Entries);
	void Close();
	bool IsOpen() const { return bOpen; }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:

	FOnHexSpellCast OnCast;
	FSimpleDelegate OnClosed;
	TSharedPtr<SVerticalBox> Body;
	bool bOpen = false;
	FString ShownTitle;
	int32 ShownMana = 0;
	int32 ShownMaxMana = 0;
	TArray<FHexSpellBookEntry> ShownEntries;
	/** The school shown, -1 for all */
	int32 School = -1;

	void Rebuild();
};
