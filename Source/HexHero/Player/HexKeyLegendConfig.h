// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SHexDebugOverlay.h"
#include "HexKeyLegendConfig.generated.h"

class APlayerController;
class UHexInputConfig;
class UInputAction;

/** One line of the legend of the keys */
USTRUCT(BlueprintType)
struct FHexKeyLegendLine
{
	GENERATED_BODY()

	/** Heading the line is shown under. Lines with the same one stand together, in the order the first of them comes */
	UPROPERTY(EditAnywhere, Category="Legend")
	FText Category;

	/** What the keys do */
	UPROPERTY(EditAnywhere, Category="Legend")
	FText Label;

	/** Action whose keys the line shows, as the mapping context of the input config has them */
	UPROPERTY(EditAnywhere, Category="Legend")
	TObjectPtr<UInputAction> Action;
};

/**
 *  Lines of the legend of the keys, in the order they are shown. The keys themselves are read from the input actions,
 *  so this asset is edited by hand and no script resets it.
 */
UCLASS(BlueprintType)
class UHexKeyLegendConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Lines of the legend */
	UPROPERTY(EditAnywhere, Category="Legend")
	TArray<FHexKeyLegendLine> Lines;

	/** Hint shown in place of the legend while it is hidden. {Keys} becomes the keys of the legend action of the input config */
	UPROPERTY(EditAnywhere, Category="Legend")
	FText Hint = NSLOCTEXT("HexHero", "KeyLegendHint", "Press {Keys} for keys");

	/** Returns the lines as pairs of label and keys under their headings. A line whose action has no keys, or is not one of OnlyActions when given, is left out */
	TArray<FHexLegendSection> GetSections(const UHexInputConfig& InputConfig, const TSet<const UInputAction*>* OnlyActions = nullptr) const;

	/** Returns the hint with the keys of the legend action, or nothing if that action has no keys */
	FString GetHint(const UHexInputConfig& InputConfig) const;

	/** Gathers the actions that a player controller and its pawn bound on their enhanced input components, so a legend shows only the keys
	 *  that do something there. Returns false while the pawn has bound none yet, as it binds them once it is possessed */
	static bool GetBoundActions(const APlayerController& Controller, TSet<const UInputAction*>& OutActions);

	/** Logs each line of a legend, Verbose, so the legends of the levels can be compared */
	static void LogSections(const TArray<FHexLegendSection>& Sections, const TCHAR* Owner);
};
