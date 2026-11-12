// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexUiConfig.generated.h"

class UTexture2D;
struct FSlateBrush;

/**
 *  The cursors of the game, which the developer asked for on 2026-11-12 in place of the cursor of the system: on the planet what sending the
 *  hero chosen there does (walk today or on a later day, no way, attack, go into a castle, visit a mine or take a pile, go through an
 *  entrance, swap with a hero of its own) or that a click chooses, and on the field of a battle what a click does (walk, fly, shoot, shoot
 *  from afar at half the damage, strike from a side, cast, nothing)
 */
UENUM(BlueprintType)
enum class EHexCursor : uint8
{
	Default,
	Choose,
	Move,
	MoveLater,
	Blocked,
	Attack,
	Castle,
	Visit,
	Portal,
	Swap,
	BattleWalk,
	BattleFly,
	BattleShoot,
	BattleShootFar,
	/** A sword that points right in its picture, turned to point from the hex the stack strikes from to the one it strikes */
	BattleStrike,
	BattleCast,
	BattleBlocked,
	Num UMETA(Hidden)
};

/** The icons of the stats of a creature in the panels of a battle */
UENUM(BlueprintType)
enum class EHexStatIcon : uint8
{
	Attack,
	Defense,
	Damage,
	Health,
	Speed,
	Shots,
	Morale,
	Luck,
	Mana,
	Kills,
	Count,
	Flies,
	Num UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct FHexCursorLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Cursor")
	EHexCursor Cursor = EHexCursor::Default;

	UPROPERTY(EditAnywhere, Category="Cursor")
	TSoftObjectPtr<UTexture2D> Texture;

	/** The point of the picture that is the point of the cursor, from 0 to 1 of its width and height */
	UPROPERTY(EditAnywhere, Category="Cursor")
	FVector2D HotSpot = FVector2D::ZeroVector;

	/** How large it shows, in pixels of Slate */
	UPROPERTY(EditAnywhere, Category="Cursor", meta=(ClampMin=4))
	float Size = 32.0f;

	/** It is animated, as the developer asked on 2026-11-12: the picture is a strip of this many frames side by side, played this many a
	 *  second; 1 for a picture that stands still */
	UPROPERTY(EditAnywhere, Category="Cursor", meta=(ClampMin=1))
	int32 Frames = 1;

	UPROPERTY(EditAnywhere, Category="Cursor", meta=(ClampMin=0.1))
	float FramesPerSecond = 8.0f;
};

USTRUCT(BlueprintType)
struct FHexStatIconLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Icon")
	EHexStatIcon Icon = EHexStatIcon::Attack;

	UPROPERTY(EditAnywhere, Category="Icon")
	TSoftObjectPtr<UTexture2D> Texture;

	/** A strip of frames side by side, played this many a second, as the cursors are */
	UPROPERTY(EditAnywhere, Category="Icon", meta=(ClampMin=1))
	int32 Frames = 1;

	UPROPERTY(EditAnywhere, Category="Icon", meta=(ClampMin=0.1))
	float FramesPerSecond = 8.0f;
};

/**
 *  How the panels and the cursor of the game look: DA_HexUi in Content/HexPlanet/UI, which Tools/HexUi/import_ui_icons.py creates when it is
 *  missing and gives the pictures of generate_ui_icons.py while it has none, so an artist replaces them there; a missing one falls back to the
 *  defaults of the class, and a cursor without a picture to the cursor of the system
 */
UCLASS(BlueprintType)
class UHexUiConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** The cursors of the game; the system cursor shows while this is off or a cursor has no picture */
	UPROPERTY(EditAnywhere, Category="Cursors")
	bool bGameCursors = true;

	UPROPERTY(EditAnywhere, Category="Cursors")
	TArray<FHexCursorLook> Cursors;

	UPROPERTY(EditAnywhere, Category="Icons")
	TArray<FHexStatIconLook> StatIcons;

	/** The panel of a battle at the cursor: the icons this large, this far from the cursor */
	UPROPERTY(EditAnywhere, Category="Tooltip", meta=(ClampMin=8))
	float StatIconSize = 24.0f;

	UPROPERTY(EditAnywhere, Category="Tooltip", meta=(ClampMin=0))
	float TooltipOffset = 26.0f;

	UPROPERTY(EditAnywhere, Category="Tooltip", meta=(ClampMin=0))
	float TooltipOpacity = 0.86f;

	/** The marks of hex.Cheat enemies over the stacks and heroes of the other players: a line this many pixels up from where each stands, in
	 *  its color, its name and number over it, drawn while the camera sees the place over the curve of the planet */
	UPROPERTY(EditAnywhere, Category="Cheats", meta=(ClampMin=0))
	float EnemyMarkHeight = 70.0f;

	UPROPERTY(EditAnywhere, Category="Cheats")
	FLinearColor NeutralMarkColor = FLinearColor(1.0f, 0.35f, 0.25f, 1.0f);

	/** The sheet of the cheats (Ctrl+H) covers the screen this dark */
	UPROPERTY(EditAnywhere, Category="Cheats", meta=(ClampMin=0, ClampMax=1))
	float CheatSheetOpacity = 0.72f;

	const FHexCursorLook* FindCursor(EHexCursor Cursor) const;
	TSoftObjectPtr<UTexture2D> FindIcon(EHexStatIcon Icon) const;
	const FHexStatIconLook* FindIconLook(EHexStatIcon Icon) const;

	/** Which frame of a strip of Frames played FramesPerSecond shows now */
	static int32 GetFrame(int32 Frames, float FramesPerSecond);

	/** Brushes for each frame of a strip of pictures, as UV regions of it, each Size large */
	static void MakeFrameBrushes(UTexture2D* Texture, int32 Frames, float Size, TArray<TSharedPtr<FSlateBrush>>& OutBrushes);

	/** Loads DA_HexUi, or returns the defaults of the class with a warning when it is missing */
	static const UHexUiConfig* Load();
};
