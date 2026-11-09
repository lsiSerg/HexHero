// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HexPlanetLayers.generated.h"

class AHexPlanet;

/**
 *  The layers of a planet: its surface, and the underground, a second hex sphere of the same frequency whose cell i lies under surface cell i,
 *  a maze of passages between the entrances on the surface. Each layer keeps its files in its own data folder of the planet
 *  (HexPlanetFile::GetDataFolder) and writes levels.txt with its own letters (HexPlanetFile::GetLevelLetters).
 */
UENUM(meta=(Bitflags))
enum class EHexPlanetLayer : uint8
{
	Surface,
	Underground,
};

/** Number of layers */
constexpr int32 NumPlanetLayers = 2;

/** Returns the bit of a layer in a mask of layers, such as UHexPlanetObjectSet::PlanetLayers */
inline constexpr int32 GetPlanetLayerBit(EHexPlanetLayer Layer)
{
	return 1 << int32(Layer);
}

/** Returns the name of a layer, Surface or Underground, as the console takes it */
const TCHAR* GetPlanetLayerName(EHexPlanetLayer Layer);

/** Reads the name of a layer, any case. Returns false if it is none */
bool ParsePlanetLayer(const FString& Text, EHexPlanetLayer& OutLayer);

/**
 *  The planets of a world by layer and the one shown, which the console commands, the camera and the controller find the planet through. Each
 *  AHexPlanet registers itself in its BeginPlay; the surface placed in the level spawns the underground (AHexPlanet::SpawnUnderground). Only the
 *  presented layer is drawn and does the work of a frame, see AHexPlanet::SetPresented
 */
UCLASS()
class UHexPlanetLayers : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/** Returns the subsystem of a world, or null */
	static UHexPlanetLayers* Get(const UWorld* World);

	/** Returns the planet of a layer, or null */
	AHexPlanet* GetPlanet(EHexPlanetLayer Layer) const { return Planets[int32(Layer)].Get(); }

	AHexPlanet* GetSurface() const { return GetPlanet(EHexPlanetLayer::Surface); }
	AHexPlanet* GetUnderground() const { return GetPlanet(EHexPlanetLayer::Underground); }

	/** Returns the layer shown now */
	EHexPlanetLayer GetPresentedLayer() const { return Presented; }

	/** Returns the planet shown now, or null */
	AHexPlanet* GetPresented() const { return GetPlanet(Presented); }

	/** Returns the planet shown now once it is built, else null, for the console commands, which may come before it is */
	AHexPlanet* GetPresentedBuilt() const;

	/** Called by a planet in its BeginPlay and EndPlay */
	void Register(AHexPlanet* Planet);
	void Unregister(AHexPlanet* Planet);

	/** Shows a layer and hides the other (AHexPlanet::SetPresented), building the meshes of a layer shown the first time. Returns false if
	 *  that layer has no planet */
	bool Present(EHexPlanetLayer Layer);

	/** Broadcast after a layer was presented */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresented, EHexPlanetLayer);
	FOnPresented OnPresented;

private:

	TWeakObjectPtr<AHexPlanet> Planets[NumPlanetLayers];

	EHexPlanetLayer Presented = EHexPlanetLayer::Surface;
};
