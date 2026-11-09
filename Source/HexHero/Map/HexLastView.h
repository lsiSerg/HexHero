// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetLayers.h"
#include "HexLastView.generated.h"

struct FHexMapEntry;
class UWorld;

/** Camera of the flat map, the block flatMap of LastView.json */
USTRUCT()
struct FHexLastFlatMapView
{
	GENERATED_BODY()

	/** Folder of the map in Content/Data/Maps */
	UPROPERTY()
	FString Map;

	/** Point of the ground under the camera pawn, in the world */
	UPROPERTY()
	double X = 0.0;

	UPROPERTY()
	double Y = 0.0;

	/** Camera turn away from the default angle, in degrees */
	UPROPERTY()
	float Yaw = 0.0f;

	/** Camera distance */
	UPROPERTY()
	float Zoom = 0.0f;

	/** True if the camera turned freely */
	UPROPERTY()
	bool bFreeCamera = false;

	bool operator==(const FHexLastFlatMapView& Other) const = default;
};

/** Camera of the planet, the block planet of LastView.json, as hex.PlanetSetCamera takes it */
USTRUCT()
struct FHexLastPlanetView
{
	GENERATED_BODY()

	/** Folder in Content/Data/Planets of the planet whose camera is saved, which opens when no planet is asked for */
	UPROPERTY()
	FString Planet;

	UPROPERTY()
	double Latitude = 0.0;

	UPROPERTY()
	double Longitude = 0.0;

	UPROPERTY()
	double Heading = 0.0;

	/** Distance of the camera, where 0 means none was saved */
	UPROPERTY()
	double Distance = 0.0;

	/** Layer of the planet the camera showed, the surface when the file has none */
	UPROPERTY()
	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;

	bool operator==(const FHexLastPlanetView& Other) const = default;
};

/**
 *  Map and camera of the last run, stored as LastView.json in the Saved folder, so the next run opens there, in a block per kind of view.
 *  Whether the planet or a flat map was open last is written whenever either opens, and each camera only with the view key.
 */
USTRUCT()
struct FHexLastView
{
	GENERATED_BODY()

	/** True if the planet was open last, so a run without a map opens it */
	UPROPERTY()
	bool bPlanetOpen = false;

	/** Camera of the flat map */
	UPROPERTY()
	FHexLastFlatMapView FlatMap;

	/** Camera of the planet */
	UPROPERTY()
	FHexLastPlanetView Planet;

	bool operator==(const FHexLastView& Other) const = default;
};

namespace HexLastView
{
	/** Returns the full path of the file */
	FString GetFilePath();

	/** Reads the file, also in the flat layout of before the blocks. Returns false if there is none or it is not valid */
	bool Load(FHexLastView& OutView);

	/** Reads the view from the text of the file. Returns false if it is not valid */
	bool Parse(const FString& Json, FHexLastView& OutView);

	/** Writes the file. Returns false if it could not */
	bool Save(const FHexLastView& View);

	/** Returns the folder of the last map if it is still one of these maps, or an empty string */
	FString FindLastMap(const TArray<FHexMapEntry>& Maps);

	/** Writes whether the planet or a flat map is open now, keeping the rest of the file */
	void SetPlanetOpen(bool bPlanet);

	/** Returns true if the level of the flat map should open the planet instead: no map was asked for and the planet was open last */
	bool ShouldOpenPlanet(const UWorld* World);
}
