// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexMapManifest.generated.h"

/**
 *  Description of a map, stored as map.json in the folder of the map.
 */
USTRUCT()
struct FHexMapManifest
{
	GENERATED_BODY()

	/** Version of this format */
	UPROPERTY()
	int32 Version = 0;

	/** Name shown in the map menu */
	UPROPERTY()
	FString Name;

	/** Longer description of the map */
	UPROPERTY()
	FString Description;

	/** Width in hexes, which must match the images of the map */
	UPROPERTY()
	int32 Width = 0;

	/** Height in hexes, which must match the images of the map */
	UPROPERTY()
	int32 Height = 0;

	/** Position in the map menu, lowest first. The first map is loaded until another is chosen */
	UPROPERTY()
	int32 SortOrder = 0;
};

/** One map that can be played */
struct FHexMapEntry
{
	/** Name of the folder of the map */
	FString Folder;

	FHexMapManifest Manifest;
};
