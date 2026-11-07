// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetObjectSet.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Which layer of a cell a kind of object fills: each cell holds at most one cover and one decor */
UENUM(BlueprintType)
enum class EHexObjectLayer : uint8
{
	/** Fills the cell with many objects, at a density of 1 to 3: forests, bushes, flowers, fields of stones */
	Cover,
	/** A few objects, usually one, near the middle of the cell: a fallen tree, a giant tree, a big boulder */
	Decor,
};

/** The look of a kind of object on some biomes: its meshes and how they stand there */
USTRUCT(BlueprintType)
struct FHexObjectVariant
{
	GENERATED_BODY()

	/** PlanetLetter of the biomes that draw this variant, such as NT for snow and tundra; empty for every biome without a variant of its own.
	 *  A biome with no variant here and no empty one draws nothing of this kind */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Variant")
	FString Biomes;

	/** Meshes, one taken at random by object; any static mesh, which keeps its own materials unless the set gives one */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Variant")
	TArray<TSoftObjectPtr<UStaticMesh>> Meshes;

	/** How much snow lies on the tops, from 0 to 1, which M_HexPlanetFoliage reads from the second custom data of an instance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Variant", meta=(ClampMin=0, ClampMax=1))
	float Snow = 0.0f;

	/** Multiplies the size of the set on these biomes, such as small trees of the tundra */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Variant", meta=(ClampMin=0.05))
	float ScaleFactor = 1.0f;

	/** Multiplies the number of objects of the set on these biomes, such as sparse trees of the steppe */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Variant", meta=(ClampMin=0))
	float CountFactor = 1.0f;

	/** Share of the land cells of these biomes that a generated planet gives this kind, in patches of a noise (ObjectNoiseScale of
	 *  DA_HexPlanet), the densest where the noise is highest; 0 for none, so it is only painted */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation", meta=(ClampMin=0, ClampMax=1))
	float GenerateShare = 0.0f;
};

/**
 *  A kind of object that stands on the cells of the planet: a forest, bushes, flowers, stones, or a piece of decor such as a fallen tree. One asset per
 *  kind in PlanetObjectsFolder of HexMapSettings, found through the asset registry, so a new kind needs no shared file, and objects.json of a
 *  planet names it by its Id. The objects are drawn as instances of their meshes (UHexPlanetObjects), so a whole planet of trees stays cheap,
 *  and an artist changes them here: other meshes, a material, sizes, counts and spacing, which biomes draw which variant.
 */
UCLASS(BlueprintType)
class UHexPlanetObjectSet : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Name of the kind in objects.json, which must not change once planets name it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	FName Id;

	/** Name on the palette of the brush and at the cursor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	FString DisplayName;

	/** Order on the palette and in the generation, lowest first: a cell keeps the first cover generated for it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	int32 SortOrder = 0;

	/** Color of its button on the palette */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	FLinearColor PaletteColor = FLinearColor(0.2f, 0.4f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	EHexObjectLayer Layer = EHexObjectLayer::Cover;

	/** A hero cannot cross a cell with it, as trees; bushes and flowers can be crossed. Nothing moves yet, so only the info at the cursor shows it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	bool bBlocksMovement = false;

	/** Levels it may stand on, 0 plains, 1 hills, 2 mountains; a cell on another level keeps it but draws nothing, and a save drops it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	TArray<int32> AllowedLevels = {0, 1};

	/** The looks of the kind by biome */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set")
	TArray<FHexObjectVariant> Variants;

	/** Material of all its meshes in place of their own, or none to keep theirs. It reads the rock layer of the biome of the cell from the first
	 *  custom data of an instance and the snow from the second, as M_HexPlanetBoulder and M_HexPlanetFoliage do */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look")
	TSoftObjectPtr<UMaterialInterface> Material;

	/** Objects in a cell at a density of 1, 2 and 3 (a decor takes the first), before CountFactor of the variant; a pentagon takes a sixth less */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement")
	TArray<int32> CountByDensity = {6, 14, 26};

	/** Size of an object as a factor on its mesh, at random between these, and now and then a giant GiantScale times larger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0.01))
	float ScaleMin = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0.01))
	float ScaleMax = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0, ClampMax=1))
	float GiantChance = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=1))
	float GiantScale = 2.0f;

	/** Least distance between the middles of two objects of the cell, in world units at a size of 1, growing with their sizes; a cell is about 200 across */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0))
	float Spacing = 14.0f;

	/** How far from its middle an object may stand, as a share of the way to the sides: 1 fills the cell, so forests of neighbors join, and
	 *  a decor near the middle takes less */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0.05, ClampMax=1.2))
	float Reach = 1.0f;

	/** Steepest ground it stands on, as the least dot of the normal of the ground and the up of the planet */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0, ClampMax=1))
	float MinUp = 0.7f;

	/** How much an object leans with the ground, 0 upright as trees, 1 lying on it as stones */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0, ClampMax=1))
	float SlopeAlign = 0.15f;

	/** Most random lean, in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0, ClampMax=90))
	float Tilt = 4.0f;

	/** How deep it sinks into the ground, in world units at a size of 1, so its foot does not float on a slope */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement", meta=(ClampMin=0))
	float Sink = 1.0f;

	/** How high above the sea the ground must lie, in world units, so nothing stands in the water of a winding coast */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement")
	float MinHeight = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
	bool bCastShadow = true;

	/** Distance from the camera past which it is not drawn, 0 always */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(ClampMin=0))
	float CullDistance = 9000.0f;

	/** Returns the variant a biome draws, its own or the one for every biome, else null */
	const FHexObjectVariant* FindVariant(TCHAR BiomeLetter) const;

#if WITH_EDITOR
	/** Gives a mesh a LOD per share of its triangles, reduced from the first, each drawn from its screen size, and builds it. Returns the number
	 *  of LODs. For import_planet_objects.py, as the editor subsystem that does it is missing in a commandlet */
	UFUNCTION(BlueprintCallable, Category="Objects")
	static int32 SetMeshLods(UStaticMesh* Mesh, const TArray<float>& TriangleShares, const TArray<float>& ScreenSizes);
#endif
};
