// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexKeyLegendConfig.h"
#include "HexInputConfig.h"

TArray<FHexLegendSection> UHexKeyLegendConfig::GetSections(const UHexInputConfig& InputConfig, const TSet<const UInputAction*>* OnlyActions) const
{
	TArray<FHexLegendSection> Result;

	for (const FHexKeyLegendLine& Line : Lines)
	{
		const FString Keys = InputConfig.GetKeysText(Line.Action);

		if (Keys.IsEmpty() || (OnlyActions && !OnlyActions->Contains(Line.Action.Get())))
		{
			continue;
		}

		const FString Title = Line.Category.ToString();
		FHexLegendSection* Section = Result.FindByPredicate([&Title](const FHexLegendSection& Candidate) { return Candidate.Title == Title; });

		if (!Section)
		{
			Section = &Result.AddDefaulted_GetRef();
			Section->Title = Title;
		}

		Section->Lines.Emplace(Line.Label.ToString(), Keys);
	}

	return Result;
}

FString UHexKeyLegendConfig::GetHint(const UHexInputConfig& InputConfig) const
{
	const FString Keys = InputConfig.GetKeysText(InputConfig.KeyLegendAction);

	if (Keys.IsEmpty())
	{
		return FString();
	}

	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("Keys"), FText::FromString(Keys));
	return FText::Format(Hint, Arguments).ToString();
}
