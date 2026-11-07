// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;

/** Gives materials the values of a data asset whose properties are named as their parameters */
namespace HexMaterialConfig
{
	/**
	 *  Gives a material every float, FLinearColor, FVector and FVector4 of a config as parameters of the same names, which it ignores where it has none,
	 *  so a new value needs only the property and the parameter. Set them once when the material is made, as it costs nothing per frame then
	 */
	void Apply(UMaterialInstanceDynamic* Target, const UObject* Config);
}
