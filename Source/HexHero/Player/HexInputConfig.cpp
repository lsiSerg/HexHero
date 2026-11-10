// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexInputConfig.h"
#include "Algo/AllOf.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"

namespace
{
	/** Returns the actions that a key must be held with, in the order of its triggers, as Ctrl and Shift */
	TArray<const UInputAction*, TInlineAllocator<2>> GetChordActions(const FEnhancedActionKeyMapping& Mapping)
	{
		TArray<const UInputAction*, TInlineAllocator<2>> Actions;
		for (const UInputTrigger* Trigger : Mapping.Triggers)
		{
			if (const UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Trigger))
			{
				Actions.Add(Chord->ChordAction);
			}
		}

		return Actions;
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

	/** Returns the keys a key is held with, as Ctrl+Shift+, the first key of each action it is chorded with, or nothing */
	FString GetChordPrefix(const UInputMappingContext& Context, const FEnhancedActionKeyMapping& Mapping)
	{
		FString Prefix;
		for (const UInputAction* ChordAction : GetChordActions(Mapping))
		{
			const FEnhancedActionKeyMapping* ChordMapping = Context.GetMappings().FindByPredicate([ChordAction](const FEnhancedActionKeyMapping& Candidate)
			{
				return Candidate.Action == ChordAction;
			});
			if (ChordMapping)
			{
				Prefix += GetShortKeyName(ChordMapping->Key) + TEXT("+");
			}
		}
		return Prefix;
	}

	/** Joins the keys of one action compactly: the four arrows become one word, and keys of one character held with the same keys stand together,
	 *  as in WASD or Ctrl+12345, all ten number keys as 0-9 */
	FString JoinKeyNames(const UInputMappingContext& Context, const TArray<const FEnhancedActionKeyMapping*>& Mappings)
	{
		// each name as the keys it is held with and its own key
		TArray<TPair<FString, FString>> Names;
		const TArray<FKey> Arrows = { EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right };
		const bool bHasArrows = Algo::AllOf(Arrows, [&Mappings](const FKey& Arrow)
		{
			return Mappings.ContainsByPredicate([&Arrow](const FEnhancedActionKeyMapping* Mapping) { return Mapping->Key == Arrow; });
		});

		for (const FEnhancedActionKeyMapping* Mapping : Mappings)
		{
			const TPair<FString, FString> Name = bHasArrows && Arrows.Contains(Mapping->Key) ? TPair<FString, FString>(FString(), TEXT("Arrows"))
				: TPair<FString, FString>(GetChordPrefix(Context, *Mapping), GetShortKeyName(Mapping->Key));
			Names.AddUnique(Name);
		}

		FString Text;
		for (int32 Index = 0; Index < Names.Num();)
		{
			// a run of keys of one character held with the same keys
			int32 End = Index + 1;
			FString Run = Names[Index].Value;
			while (Run.Len() == End - Index && End < Names.Num() && Names[End].Key == Names[Index].Key && Names[End].Value.Len() == 1)
			{
				Run += Names[End].Value;
				++End;
			}
			if (Run.Len() != End - Index)
			{
				// a name of more than one character stands alone
				End = Index + 1;
				Run = Names[Index].Value;
			}
			Text += (Index == 0 ? TEXT("") : TEXT(", ")) + Names[Index].Key + (Run == TEXT("1234567890") ? FString(TEXT("0-9")) : Run);
			Index = End;
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
