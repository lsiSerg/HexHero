// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexLevelConfig.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Other versions of one joined mesh, with the same sides and another inside */
USTRUCT(BlueprintType)
struct FHexJoinedVariants
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Feature")
	TArray<TSoftObjectPtr<UStaticMesh>> Meshes;
};

/**
 *  One terrain level: how it is written in map files, its shape and what can cross it.
 *  Every level is its own asset, as binary assets cannot be merged. The game finds them in LevelsFolder of the project settings.
 */
UCLASS(BlueprintType)
class UHexLevelConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Name shown in debug output */
	UPROPERTY(EditAnywhere, Category="Level")
	FString DisplayName;

	/** Height step of this level. Level 0 is the base land height */
	UPROPERTY(EditAnywhere, Category="Level")
	int32 Level = 0;

	/** Exact pixel color that means this level in a map file */
	UPROPERTY(EditAnywhere, Category="Level")
	FColor FileColor = FColor::Black;

	/** If true, this is water too deep to see through. A hex of this level with only such neighbors has no terrain, and the water hides the end of the terrain near a shore */
	UPROPERTY(EditAnywhere, Category="Level")
	bool bHidesTerrain = false;

	/** Color of the terrain surface */
	UPROPERTY(EditAnywhere, Category="Level")
	FLinearColor TerrainColor = FLinearColor::White;

	/** Height of the soft bumps on this level, up and down from its base height */
	UPROPERTY(EditAnywhere, Category="Level", meta=(ClampMin=0))
	float BumpHeight = 0.0f;

	/** Height of the sharp crests on this level, up from its base height */
	UPROPERTY(EditAnywhere, Category="Level", meta=(ClampMin=0))
	float RidgeHeight = 0.0f;

	/** Ready-made mesh placed on every hex of this level, such as a mountain. Its size comes from the values below */
	UPROPERTY(EditAnywhere, Category="Feature")
	TSoftObjectPtr<UStaticMesh> FeatureMesh;

	/** Material of the feature mesh. If not set, the mesh keeps its own */
	UPROPERTY(EditAnywhere, Category="Feature")
	TSoftObjectPtr<UMaterialInterface> FeatureMaterial;

	/** Width of the feature mesh as a multiple of the hex radius. Above 2 it reaches over the sides of the hex */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=0.1))
	float FeatureWidth = 2.0f;

	/** Height of the feature mesh */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=1))
	float FeatureHeight = 200.0f;

	/** How far the bottom of the feature mesh goes below the base height of the level, which hides where it meets the terrain */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=0))
	float FeatureSink = 40.0f;

	/**
	 *  Meshes that join across the sides of neighboring hexes of this level, by the mask from HexMeshBuilder::GetJoinedPiece.
	 *  They replace the feature mesh and keep their size, so the values above do not apply to them.
	 */
	UPROPERTY(EditAnywhere, Category="Feature")
	TMap<int32, TSoftObjectPtr<UStaticMesh>> JoinedMeshes;

	/** Other versions of some joined meshes by the same mask. Each hex keeps one of them, chosen from its position */
	UPROPERTY(EditAnywhere, Category="Feature")
	TMap<int32, FHexJoinedVariants> JoinedMeshVariants;

	/** Wedge that fills the corner of a lower hex where two neighboring hexes with joined meshes meet it, which evens out the outline of a range */
	UPROPERTY(EditAnywhere, Category="Feature")
	TSoftObjectPtr<UStaticMesh> JoinedNotchMesh;

	/** Hex radius for which the joined meshes were made. Their base height 0 stands at the base height of the level */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=1))
	float JoinedMeshRadius = 100.0f;

	/** Height of the edge of the joined meshes above their base height. assign_hex_mountains.py sets it from mountain_params.json */
	UPROPERTY(EditAnywhere, Category="Feature")
	float JoinedMeshFootHeight = -75.0f;

	/** Height above the foot over which a joined mesh stops following the terrain, so its top stays as it was made. assign_hex_mountains.py sets it from mountain_params.json */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=1))
	float JoinedMeshRimFadeHeight = 55.0f;

	/**
	 *  How much taller a joined mesh grows for each hex it lies further inside its group, as a part of its height above the foot,
	 *  so a group merges into one massif that is highest in its middle. assign_hex_mountains.py sets it from mountain_params.json
	 */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=0))
	float JoinedMeshMassifStep = 0.4f;

	/** How far a joined mesh rises as a whole for each such hex, above its foot, in world units of a hex of JoinedMeshRadius */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=0))
	float JoinedMeshMassifRise = 100.0f;

	/** How many such steps a joined mesh grows at most */
	UPROPERTY(EditAnywhere, Category="Feature", meta=(ClampMin=0))
	int32 JoinedMeshMassifMaxSteps = 4;

	/** If true, ships can cross this level */
	UPROPERTY(EditAnywhere, Category="Movement")
	bool bPassableByShips = false;

	/** If true, heroes can cross this level */
	UPROPERTY(EditAnywhere, Category="Movement")
	bool bPassableByHeroes = false;

	/** If true, ordinary units can cross this level */
	UPROPERTY(EditAnywhere, Category="Movement")
	bool bPassableByUnits = false;

	/** Movement cost of one hex of this level */
	UPROPERTY(EditAnywhere, Category="Movement", meta=(ClampMin=0))
	float MoveCost = 1.0f;
};
