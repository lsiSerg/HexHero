// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexArtifactConfig.generated.h"

class UTexture2D;

/** Where on a hero an artifact is worn, as the developer chose on 2026-11-15 ("Slots by kind"): a head, a neck, a body, a weapon, a shield,
 *  a cloak, the feet, two rings and three things besides, and a backpack for the rest */
UENUM()
enum class EHexArtifactSlot : uint8
{
	Head,
	Neck,
	Body,
	Weapon,
	Shield,
	Cloak,
	Feet,
	Ring,
	Misc,
	Num UMETA(Hidden)
};

/** How rare and strong an artifact is: what lies about on the map, in chests and the stock of a merchant is mostly of the lower ranks */
UENUM()
enum class EHexArtifactRank : uint8
{
	Treasure,
	Minor,
	Major,
	Relic,
	Num UMETA(Hidden)
};

/** The slots a hero wears its artifacts in, in this order: Head, Neck, Body, Weapon, Shield, Cloak, Feet, two Rings and three Misc */
constexpr int32 NumWornSlots = 12;

/** Returns the kind of a worn slot of a hero, 0 to NumWornSlots - 1 */
EHexArtifactSlot GetWornSlotKind(int32 WornSlot);

/** Returns the name of a kind of slot, and of a rank */
FString GetArtifactSlotName(EHexArtifactSlot Slot);
FString GetArtifactRankName(EHexArtifactRank Rank);

/**
 *  An artifact a hero wears, as the developer asked on 2026-11-15 ("додай артефакти"): about thirty of the game's own, each worn in a slot of its
 *  kind, which give the attributes of the hero, movement, mana a day, morale and luck to its army and power to the spells of a school. Its
 *  numbers and names are Claude's
 */
USTRUCT(BlueprintType)
struct FHexArtifact
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Artifact")
	FName Id;

	UPROPERTY(EditAnywhere, Category="Artifact")
	FString DisplayName;

	/** A line of its story, under what it gives */
	UPROPERTY(EditAnywhere, Category="Artifact")
	FString Lore;

	UPROPERTY(EditAnywhere, Category="Artifact")
	EHexArtifactSlot Slot = EHexArtifactSlot::Misc;

	UPROPERTY(EditAnywhere, Category="Artifact")
	EHexArtifactRank Rank = EHexArtifactRank::Treasure;

	/** What it costs in gold at a merchant */
	UPROPERTY(EditAnywhere, Category="Artifact", meta=(ClampMin=0))
	int32 Cost = 1000;

	/** What it adds to the attributes of the hero that wears it */
	UPROPERTY(EditAnywhere, Category="Effect")
	int32 Attack = 0;

	UPROPERTY(EditAnywhere, Category="Effect")
	int32 Defense = 0;

	UPROPERTY(EditAnywhere, Category="Effect")
	int32 SpellPower = 0;

	UPROPERTY(EditAnywhere, Category="Effect")
	int32 Knowledge = 0;

	/** Points of movement more a day */
	UPROPERTY(EditAnywhere, Category="Effect")
	float Movement = 0.0f;

	/** Mana more a day */
	UPROPERTY(EditAnywhere, Category="Effect")
	int32 ManaPerDay = 0;

	/** Morale and luck of every stack of its army in a battle */
	UPROPERTY(EditAnywhere, Category="Effect")
	int32 Morale = 0;

	UPROPERTY(EditAnywhere, Category="Effect")
	int32 Luck = 0;

	/** The school of magic (EHexSpellSchool: 0 Air, 1 Earth, 2 Fire, 3 Water) whose spells it makes stronger by SchoolPower, -1 for none */
	UPROPERTY(EditAnywhere, Category="Effect", meta=(ClampMin=-1, ClampMax=3))
	int32 School = -1;

	UPROPERTY(EditAnywhere, Category="Effect", meta=(ClampMin=0))
	float SchoolPower = 0.0f;

	/** Its picture, T_Art<Id> in /Game/HexPlanet/UI/Artifacts, drawn by Tools/HexUi/artifact_icons.py */
	UPROPERTY(EditAnywhere, Category="Artifact")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Returns what it gives, as "Attack +2, Defense +1" */
	FString DescribeEffect() const;
};

/** What the artifacts a hero wears give together */
struct FHexArtifactBonus
{
	int32 Attack = 0;
	int32 Defense = 0;
	int32 SpellPower = 0;
	int32 Knowledge = 0;
	float Movement = 0.0f;
	int32 ManaPerDay = 0;
	int32 Morale = 0;
	int32 Luck = 0;
	float SchoolPower[4] = {0.0f, 0.0f, 0.0f, 0.0f};

	void Add(const FHexArtifact& Artifact);
};

/** Returns the artifacts a config has when it holds none */
TArray<FHexArtifact> MakeDefaultArtifacts();
