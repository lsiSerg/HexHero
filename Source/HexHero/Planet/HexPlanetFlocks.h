// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HexPlanetFlocks.generated.h"

class AHexPlanet;
class APlayerController;
class UHexPlanetConfig;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FHexFlockKind;

/**
 *  The birds and bats of a layer (UHexPlanetConfig::bFlocks): gulls circling off the coasts, flocks of birds over the forests, a raptor soaring
 *  over the mountains, and bats fluttering through the halls of the underground, as the kinds of UHexPlanetConfig::Flocks say. They are decor
 *  alone: nothing of the game reads them.
 *
 *  A flock has its home at a cell of the habitat of its kind, CellShare of them by a hash of the cell, so it is at the same place in every run,
 *  and only the flocks within FlockRadius of the point the camera looks at exist, at most FlockMax animals of a kind, gathered again every half
 *  second. Each animal is an instance of the mesh of its kind and flies a loop around its home that is a function of the time alone
 *  (GetPosition), so nothing is stored a frame: the transforms of the instances are set every frame, a few hundred at most, which keeps their
 *  bounds, their shadows and their motion vectors right, and M_HexPlanetFlock beats their wings in its world position offset by the first UV of
 *  a vertex, how far out on a wing it lies. Far from the place it looks at the camera sees none, and the components are hidden. An instance
 *  is added where its animal flies and never scaled to nothing: one added at a scale near 0 was not drawn again, however it was moved.
 */
UCLASS()
class UHexPlanetFlocks : public USceneComponent
{
	GENERATED_BODY()

public:

	/** Makes the components and materials of the kinds, once the planet is built */
	void Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings);

	/** Gathers the flocks near the camera of the player and moves their animals, a frame of the layer shown */
	void Update(const APlayerController* PlayerController, float DeltaTime);

	/** Shows the flocks (hex.PlanetFlocks) or hides them */
	void SetShown(bool bInShown);

	/** Returns how many animals fly now, for the log */
	int32 GetCount() const;

private:

	/** A flock at its home: the frame of the loop it flies, the radius its heights are measured from, and its animals */
	struct FFlock
	{
		int32 Cell = INDEX_NONE;
		FVector3d Up = FVector3d::UnitZ();
		FVector3d East = FVector3d::UnitX();
		FVector3d North = FVector3d::UnitY();
		double BaseRadius = 0.0;

		/** Where on its loop it is at time 0 and how the loop is turned, in radians, and which way round it flies, 1 or -1 */
		double Phase = 0.0;
		double Turn = 1.0;

		int32 FirstAnimal = 0;
		int32 NumAnimals = 0;
	};

	/** An animal of a flock: numbers of its own from 0 to 1 and its place in the flock, in a disc of radius 1 */
	struct FAnimal
	{
		int32 Flock = 0;
		float Seed = 0.0f;
		float Second = 0.0f;
		FVector2f Offset = FVector2f::ZeroVector;
	};

	/** What flies of one kind */
	struct FKind
	{
		TArray<int32> Cells;
		TArray<FFlock> Flocks;
		TArray<FAnimal> Animals;
	};

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetConfig> Settings;

	/** A component and a material per kind of Settings->Flocks, null for a kind without its mesh */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	TArray<FKind> Kinds;

	bool bShown = true;

	/** The cell the camera looks at, from which the flocks were gathered */
	int32 FocusCell = INDEX_NONE;
	int32 FocusHint = 0;
	double GatherTimer = 0.0;

	/** Returns true if a cell is a home of the habitat of a kind */
	bool IsHome(const FHexFlockKind& Kind, int32 Cell) const;

	/** Gathers the homes of each kind within FlockRadius of the focus, nearest first, and makes its animals again when they changed */
	void Gather(const FVector3d& Focus, const FVector3d& Camera);

	/** Returns where an animal is at a time, on its loop around the home of its flock */
	FVector3d GetPosition(const FHexFlockKind& Kind, const FFlock& Flock, const FAnimal& Animal, double Time) const;

	/** Returns the transform of the instance of an animal at a time: where it is, heading where it goes and leaning into its turn, at its
	 *  size, shrunk towards FlockFadeEnd from the camera */
	FTransform GetTransform(const FHexFlockKind& Kind, const FFlock& Flock, const FAnimal& Animal, double Time, const FVector3d& Camera) const;
};
