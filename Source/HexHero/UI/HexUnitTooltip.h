// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexUiConfig.h"

class SWidget;
struct FHexCreatureStats;
struct FSlateBrush;

/** What the panel of a creature shows: in a battle of the stack under the cursor, in a castle of the creature offered to hire */
struct FHexUnitTooltipData
{
	FString Title;
	FString Subtitle;
	/** Its level, 1 to 7, and how far it is upgraded, 1 for the basic version and 2 for the upgraded one, drawn after the title as the number
	 *  and that many chevrons one over another, as epaulettes, as the developer asked on 2026-11-13 in place of letters; 0 for none (SetRank) */
	int32 RankLevel = 0;
	int32 RankTier = 0;
	FLinearColor TitleColor = FLinearColor::White;
	/** Its own Attack, Defense and Speed, as its creature has them */
	int32 Attack = 0;
	int32 Defense = 0;
	/** What its hero, the spells on it and defending add to them or take from them, drawn after the stat as +N in green or -N in red, as the
	 *  developer asked on 2026-11-14; 0 in a castle */
	int32 AttackBonus = 0;
	int32 DefenseBonus = 0;
	int32 SpeedBonus = 0;
	int32 DamageMin = 0;
	int32 DamageMax = 0;
	int32 Health = 0;
	/** The health of the top creature of a stack that is wounded, else -1 */
	int32 TopHealth = -1;
	int32 Speed = 0;
	/** The shots left of a shooter, or what it has, -1 for one that does not shoot */
	int32 Shots = -1;
	int32 Morale = 0;
	int32 Luck = 0;
	bool bFlies = false;
	/** What it can, the spells on it, and a line more such as its price */
	FString Abilities;
	FString Effects;
	FString Extra;
};

/**
 *  The panel of a creature, as the developer asked on 2026-11-12: its name in the color of its side, its stats as icons with their names
 *  and numbers in two columns, whether it flies and shoots, each with its icon, what it can and the spells on it; the same in a battle
 *  (SHexBattlePanel) and in a castle (SHexCastlePanel). The icons are those of DA_HexUi, loaded once and kept for the run
 */
namespace HexUnitTooltip
{
	TSharedRef<SWidget> Make(const FHexUnitTooltipData& Data);

	/** The data of a creature from its stats alone, as a castle offers it */
	FHexUnitTooltipData FromStats(const FHexCreatureStats& Stats);

	/** The rank of a creature of a level from 0: its level and one chevron for the basic version, two for the upgraded one */
	void SetRank(FHexUnitTooltipData& Data, int32 Creature, bool bUpgraded);

	/** The level as a number and the chevrons of how far it is upgraded, one close over another */
	TSharedRef<SWidget> MakeRankBadge(int32 Level, int32 Tier, float Size, const FLinearColor& Color);

	/** What a creature can, by its abilities (EHexAbility), as words */
	FString DescribeAbilities(const FHexCreatureStats& Stats);

	/** The brush of an icon, or null without its picture */
	const FSlateBrush* GetIcon(EHexStatIcon Icon);

	/** An icon that plays its frames, of a size */
	TSharedRef<SWidget> MakeAnimatedIcon(EHexStatIcon Icon, float Size);
}
