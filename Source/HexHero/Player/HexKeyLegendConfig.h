// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SHexDebugOverlay.h"
#include "HexKeyLegendConfig.generated.h"

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
};
