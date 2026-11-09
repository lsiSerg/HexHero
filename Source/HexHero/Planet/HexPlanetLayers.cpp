// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetLayers.h"
#include "Engine/World.h"
#include "HexHero.h"
#include "HexPlanet.h"

const TCHAR* GetPlanetLayerName(EHexPlanetLayer Layer)
{
	return Layer == EHexPlanetLayer::Underground ? TEXT("Underground") : TEXT("Surface");
}

bool ParsePlanetLayer(const FString& Text, EHexPlanetLayer& OutLayer)
{
	for (const EHexPlanetLayer Layer : {EHexPlanetLayer::Surface, EHexPlanetLayer::Underground})
	{
		if (Text.Equals(GetPlanetLayerName(Layer), ESearchCase::IgnoreCase))
		{
			OutLayer = Layer;
			return true;
		}
	}
	return false;
}

UHexPlanetLayers* UHexPlanetLayers::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UHexPlanetLayers>() : nullptr;
}

AHexPlanet* UHexPlanetLayers::GetPresentedBuilt() const
{
	AHexPlanet* Planet = GetPresented();
	return Planet && Planet->IsBuilt() ? Planet : nullptr;
}

void UHexPlanetLayers::Register(AHexPlanet* Planet)
{
	if (!Planet)
	{
		return;
	}
	TWeakObjectPtr<AHexPlanet>& Slot = Planets[int32(Planet->GetLayer())];
	if (Slot.IsValid() && Slot.Get() != Planet)
	{
		UE_LOG(LogHexHero, Warning, TEXT("Planet: a second planet of the layer %s, %s, takes the place of %s"), GetPlanetLayerName(Planet->GetLayer()),
			*Planet->GetName(), *Slot->GetName());
	}
	Slot = Planet;
}

void UHexPlanetLayers::Unregister(AHexPlanet* Planet)
{
	for (TWeakObjectPtr<AHexPlanet>& Slot : Planets)
	{
		if (Slot.Get() == Planet)
		{
			Slot.Reset();
		}
	}
}

bool UHexPlanetLayers::Present(EHexPlanetLayer Layer)
{
	AHexPlanet* Target = GetPlanet(Layer);
	if (!Target)
	{
		return false;
	}

	// the other layer goes first, so two sky lights are never in the scene at once
	for (const TWeakObjectPtr<AHexPlanet>& Slot : Planets)
	{
		if (Slot.IsValid() && Slot.Get() != Target)
		{
			Slot->SetPresented(false);
		}
	}
	Target->SetPresented(true);
	Presented = Layer;
	OnPresented.Broadcast(Layer);
	return true;
}
