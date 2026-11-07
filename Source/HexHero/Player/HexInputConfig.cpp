// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexInputConfig.h"
#include "Algo/AllOf.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"

namespace
{
	/** Returns the action that a key must be held with, or nothing */
	const UInputAction* GetChordAction(const FEnhancedActionKeyMapping& Mapping)
	{
		for (const UInputTrigger* Trigger : Mapping.Triggers)
		{
			if (const UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Trigger))
			{
				return Chord->ChordAction;
			}
		}

		return nullptr;
	}

	/** Returns the short name of a key, where a left or right modifier is named once, as in Shift */
	FString GetShortKeyName(const FKey& Key)
	{
		if (Key == EKeys::LeftShift || Key == EKeys::RightShift)
		{
			return TEXT("Shift");
		}

		if (Key == EKeys::LeftControl || Key == EKeys::RightControl)
		{
			return TEXT("Ctrl");
		}

		if (Key == EKeys::LeftAlt || Key == EKeys::RightAlt)
		{
			return TEXT("Alt");
		}

		return Key.GetDisplayName(false).ToString();
	}

	/** Returns the short name of a key, after the first key of the action it is chorded with, if any, as in Shift+F */
	FString GetKeyName(const UInputMappingContext& Context, const FEnhancedActionKeyMapping& Mapping)
	{
		const FString Name = GetShortKeyName(Mapping.Key);
		const UInputAction* ChordAction = GetChordAction(Mapping);
		const FEnhancedActionKeyMapping* ChordMapping = ChordAction ? Context.GetMappings().FindByPredicate([ChordAction](const FEnhancedActionKeyMapping& Candidate)
		{
			return Candidate.Action == ChordAction;
		}) : nullptr;

		return ChordMapping ? GetShortKeyName(ChordMapping->Key) + TEXT("+") + Name : Name;
	}

	/** Joins the keys of one action compactly: the four arrows become one word and letters stand together, as in WASD */
	FString JoinKeyNames(const UInputMappingContext& Context, const TArray<const FEnhancedActionKeyMapping*>& Mappings)
	{
		TArray<FString> Names;
		const TArray<FKey> Arrows = { EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right };
		const bool bHasArrows = Algo::AllOf(Arrows, [&Mappings](const FKey& Arrow)
		{
			return Mappings.ContainsByPredicate([&Arrow](const FEnhancedActionKeyMapping* Mapping) { return Mapping->Key == Arrow; });
		});

		for (const FEnhancedActionKeyMapping* Mapping : Mappings)
		{
			if (bHasArrows && Arrows.Contains(Mapping->Key))
			{
				Names.AddUnique(TEXT("Arrows"));
			}
			else
			{
				Names.AddUnique(GetKeyName(Context, *Mapping));
			}
		}

		FString Text;

		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			const bool bJoinsLetters = Index > 0 && Names[Index].Len() == 1 && Names[Index - 1].Len() == 1;
			Text += (Index == 0 || bJoinsLetters ? TEXT("") : TEXT(", ")) + Names[Index];
		}

		return Text;
	}
}

FString UHexInputConfig::GetKeysText(const UInputAction* Action) const
{
	if (!MappingContext || !Action)
	{
		return FString();
	}

	TArray<const FEnhancedActionKeyMapping*> Mappings;

	for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
	{
		if (Mapping.Action == Action)
		{
			Mappings.Add(&Mapping);
		}
	}

	return JoinKeyNames(*MappingContext, Mappings);
}

TArray<FKey> UHexInputConfig::GetMapMenuKeys() const
{
	TArray<FKey> Keys;

	if (MappingContext)
	{
		for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
		{
			if (Mapping.Action == MapMenuAction)
			{
				Keys.Add(Mapping.Key);
			}
		}
	}

	return Keys;
}
