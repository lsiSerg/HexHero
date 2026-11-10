// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetLayers.h"
#include "HexCameraBookmarks.generated.h"

/** A camera of a planet saved in a slot, as hex.PlanetSetCamera takes it */
USTRUCT()
struct FHexPlanetBookmark
{
	GENERATED_BODY()

	/** 1 to 9, and 10 for the key 0 */
	UPROPERTY()
	int32 Slot = 0;

	UPROPERTY()
	double Latitude = 0.0;

	UPROPERTY()
	double Longitude = 0.0;

	/** Degrees clockwise from north */
	UPROPERTY()
	double Heading = 0.0;

	UPROPERTY()
	double Distance = 0.0;

	/** Layer the camera showed, which a jump travels to */
	UPROPERTY()
	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
};

/** The slots of one planet, by the folder of the planet */
USTRUCT()
struct FHexPlanetBookmarks
{
	GENERATED_BODY()

	UPROPERTY()
	FString Planet;

	UPROPERTY()
	TArray<FHexPlanetBookmark> Slots;
};

/** A camera of a flat map saved in a slot, as hex.SetCamera takes it */
USTRUCT()
struct FHexMapBookmark
{
	GENERATED_BODY()

	/** 1 to 9, and 10 for the key 0 */
	UPROPERTY()
	int32 Slot = 0;

	/** Point of the ground under the camera pawn, in the world */
	UPROPERTY()
	double X = 0.0;

	UPROPERTY()
	double Y = 0.0;

	/** Turn away from the default angle, in degrees */
	UPROPERTY()
	float Yaw = 0.0f;

	UPROPERTY()
	float Zoom = 0.0f;

	UPROPERTY()
	bool bFreeCamera = false;
};

/** The slots of one flat map, by the folder of the map */
USTRUCT()
struct FHexMapBookmarks
{
	GENERATED_BODY()

	UPROPERTY()
	FString Map;

	UPROPERTY()
	TArray<FHexMapBookmark> Slots;
};

/**
 *  CameraBookmarks.json in the Saved folder, of the player and not of the maps: the cameras saved with Ctrl+Shift and a number key, in a block per
 *  planet and per flat map, so each has its own slots, which Ctrl and the same key jump back to
 */
USTRUCT()
struct FHexCameraBookmarks
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TArray<FHexPlanetBookmarks> Planets;

	UPROPERTY()
	TArray<FHexMapBookmarks> Maps;
};

namespace HexCameraBookmarks
{
	/** Slots of a planet or map: the number keys 1 to 9 and 0, which is slot 10 */
	constexpr int32 NumSlots = 10;

	/** Returns the full path of the file */
	FString GetFilePath();

	/** Reads the file; none is no bookmarks. Returns false only if it is there but not valid */
	bool Load(FHexCameraBookmarks& OutBookmarks);

	/** Writes the file. Returns false if it could not */
	bool Save(const FHexCameraBookmarks& Bookmarks);

	/** Returns the number key of a slot: 1 to 9, and 0 for slot 10 */
	int32 GetSlotKey(int32 Slot);

	/** Returns the bookmark in a slot of a planet or a flat map, by its folder, or null */
	const FHexPlanetBookmark* FindPlanet(const FHexCameraBookmarks& Bookmarks, const FString& Planet, int32 Slot);
	const FHexMapBookmark* FindMap(const FHexCameraBookmarks& Bookmarks, const FString& Map, int32 Slot);

	/** Puts a bookmark into its slot of a planet or a flat map, replacing what the slot held */
	void SetPlanet(FHexCameraBookmarks& Bookmarks, const FString& Planet, const FHexPlanetBookmark& Bookmark);
	void SetMap(FHexCameraBookmarks& Bookmarks, const FString& Map, const FHexMapBookmark& Bookmark);
}
