// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexKeyLegendConfig.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HexHero.h"
#include "HexInputConfig.h"

bool UHexKeyLegendConfig::GetBoundActions(const APlayerController& Controller, TSet<const UInputAction*>& OutActions)
{
	OutActions.Reset();
	const APawn* Pawn = Controller.GetPawn();
	const UEnhancedInputComponent* PawnInput = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (!PawnInput || PawnInput->GetActionEventBindings().IsEmpty())
	{
		return false;
	}

	const UEnhancedInputComponent* ControllerInput = Cast<UEnhancedInputComponent>(Controller.InputComponent);
	for (const UEnhancedInputComponent* Input : {PawnInput, ControllerInput})
	{
		if (!Input)
		{
			continue;
		}
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			OutActions.Add(Binding->GetAction());
		}
	}
	return true;
}

void UHexKeyLegendConfig::LogSections(const TArray<FHexLegendSection>& Sections, const TCHAR* Owner)
{
	for (const FHexLegendSection& Section : Sections)
	{
		for (const TPair<FString, FString>& Line : Section.Lines)
		{
			UE_LOG(LogHexHero, Verbose, TEXT("Legend of %s: %s | %s | %s"), Owner, *Section.Title, *Line.Key, *Line.Value);
		}
	}
}

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
