// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexMaterialConfig.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UnrealType.h"

void HexMaterialConfig::Apply(UMaterialInstanceDynamic* Target, const UObject* Config)
{
	if (!Target || !Config)
	{
		return;
	}

	for (TFieldIterator<FProperty> It(Config->GetClass()); It; ++It)
	{
		const FName Name = It->GetFName();

		if (const FFloatProperty* Float = CastField<FFloatProperty>(*It))
		{
			Target->SetScalarParameterValue(Name, Float->GetPropertyValue_InContainer(Config));
		}
		else if (const FIntProperty* Int = CastField<FIntProperty>(*It))
		{
			// as a float, such as the index of a texture layer
			Target->SetScalarParameterValue(Name, float(Int->GetPropertyValue_InContainer(Config)));
		}
		else if (const FStructProperty* Struct = CastField<FStructProperty>(*It))
		{
			const void* Value = Struct->ContainerPtrToValuePtr<void>(Config);

			if (Struct->Struct == TBaseStructure<FLinearColor>::Get())
			{
				Target->SetVectorParameterValue(Name, *static_cast<const FLinearColor*>(Value));
			}
			else if (Struct->Struct == TBaseStructure<FVector4>::Get())
			{
				Target->SetVectorParameterValue(Name, FLinearColor(*static_cast<const FVector4*>(Value)));
			}
			else if (Struct->Struct == TBaseStructure<FVector>::Get())
			{
				Target->SetVectorParameterValue(Name, FLinearColor(*static_cast<const FVector*>(Value)));
			}
		}
	}
}
