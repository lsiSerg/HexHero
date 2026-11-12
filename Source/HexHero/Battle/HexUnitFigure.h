// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UHexBattleConfig;
class USceneComponent;
struct FHexUnitAnimation;
struct FHexUnitLook;

/** What a figure of a creature does, by which it plays one of the animations of its look */
enum class EHexFigureAction : uint8
{
	Idle,
	Move,
	Attack,
	Shoot,
	Hit,
	Death,
};

/**
 *  The figure of a creature on the field of a battle or on the planet (AHexBattleArena, UHexNeutrals): the skeletal mesh of its look in
 *  DA_HexBattle (FHexUnitLook) with M_HexBattleUnit, its colors times its tint and, by the alpha of its vertices, the color of its side, scaled
 *  to the height of the look; a creature without a look is the fallback mesh of the config, as high as its level. Its animations play through
 *  UHexHeroAnimInstance, as the hero's do
 */
namespace HexUnitFigure
{
	/** Makes a figure as a component of an owner, attached to a parent, not registered yet; HeightScale on the height of the look. OutHover is
	 *  how far above the ground its origin stands, so its foot is on it, a flyer higher. Returns null when there is neither a look nor a
	 *  fallback mesh */
	USceneComponent* Make(UObject* Owner, USceneComponent* Parent, const UHexBattleConfig* Config, FName Creature, bool bUpgraded, int32 Level,
		const FLinearColor& SideColor, float HeightScale, float& OutScale, float& OutYaw, float& OutHover);

	/** Plays an action on a figure, looped for Idle and Move, else once and back to Idle; Death holds its last pose, at once with bAtEnd, so a
	 *  corpse lies on the field. Returns false when the figure has no animation of its own for the action */
	bool Play(USceneComponent* Figure, const UHexBattleConfig* Config, FName Creature, EHexFigureAction Action, float RateScale = 1.0f, bool bAtEnd = false);

	/** Returns how long an action lasts on a figure, in seconds, 0 without its animation */
	float GetLength(const UHexBattleConfig* Config, FName Creature, EHexFigureAction Action);
}
