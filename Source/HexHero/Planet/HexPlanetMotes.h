// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HexPlanetMotes.generated.h"

class AHexPlanet;
class APlayerController;
class UHexPlanetConfig;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 *  The air of the underground (UHexPlanetConfig::bMotes): faint motes over the passages around the point the camera looks at, and spores rising
 *  from the cells of the kinds of objects with Spores near it. Both are quads of MoteMesh with M_HexPlanetMote, additive and unlit, which moves
 *  each vertex in its world position offset, so the CPU sends nothing a frame but a few parameters.
 *
 *  The motes are MoteCount instances, each standing once at a seeded spot of the box MoteBox wide around this component, so its bounds cover the
 *  box, and the component follows the point the camera looks at in steps of an eighth of the box. MOTE_OFFSET_CODE wraps each mote in a box of the
 *  world around MoteOrigin, so a mote stays where it is in the world as the box follows the camera, and lays it on the shell over the passages
 *  from MoteHeightBelow below their tops to MoteHeightAbove above them along the direction from the center, so every mote lies in that band on
 *  any part of the globe. A quad faces the camera and is never under MoteMinPixels. Far from the place it looks at the camera sees none, and
 *  the component is hidden. The spores stand at their cells and rise there (MoteAnchored 1); they are gathered again only when the cells near
 *  the focus that hold them change. Neither casts shadows nor is seen by ray tracing.
 */
UCLASS()
class UHexPlanetMotes : public USceneComponent
{
	GENERATED_BODY()

public:

	/** Makes the instances of the motes and the materials, once the planet is built */
	void Init(AHexPlanet* InPlanet, const UHexPlanetConfig* InSettings);

	/** Follows the camera of the player, a frame of the layer shown */
	void Update(const APlayerController* PlayerController, float DeltaTime);

	/** Shows the motes and spores (hex.PlanetMotes) or hides them */
	void SetShown(bool bInShown);

	/** Gives the motes the lights of the heroes, where each stands and how far it reaches in alpha, at most 8, in which they show as dust does
	 *  (MoteDarkShare, MoteLightTint); bOn is false under the forced light, when they are not darkened at all */
	void SetHeroLights(TConstArrayView<FLinearColor> Spheres, bool bOn);

	/** Returns how many spores stand now, for the log */
	int32 GetSporeCount() const;

private:

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetConfig> Settings;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Motes;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Spores;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MoteMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SporeMaterial;

	/** Where the box of the motes stands, which it leaves only when the point the camera looks at moved an eighth of the box from it */
	FVector3d PlacedOrigin = FVector3d::ZeroVector;
	bool bPlaced = false;

	bool bShown = true;

	/** The cell the camera looks at, from which the spores were gathered, and the cells that hold them, nearest first */
	int32 FocusCell = INDEX_NONE;
	int32 FocusHint = 0;
	double SporeTimer = 0.0;
	TArray<int32> SporeCells;

	/** Gathers the cells of spores within SporeRadius of the focus and places the spores again when they changed */
	void UpdateSpores(const FVector3d& Focus);

	/** Makes an instanced component of the mote mesh with four custom data per instance, without shadows, ray tracing or distance fields */
	UInstancedStaticMeshComponent* MakeComponent(const TCHAR* Name, USceneComponent* Parent);
};
