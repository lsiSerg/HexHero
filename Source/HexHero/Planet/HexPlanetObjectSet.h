// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetLayers.h"
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
	/** Stands along every side of a cell of its levels towards a cell RimMinDrop levels lower, the lip of a passage over a void, placed by the
	 *  planet with the ground and not stored, painted or listed: rocks and spires on the lip, or stalactites down the cliff below it (bRimHang) */
	Rim,
};

/** Where a rim (EHexObjectLayer::Rim) stands: along the sides of a passage over a void, or, where the layer has walls on its lips
 *  (UHexPlanetConfig::bWalls, HexPlanetWalls), at points of those walls taken by a hash of the point and the Id, so no pattern repeats */
UENUM(BlueprintType)
enum class EHexRimSeat : uint8
{
	/** Along the sides: RimCountMin to the first of CountByDensity a side on the lip, or down the cliff below it with bRimHang */
	Lip,
	/** On the rounded foot of a wall inside the passage, upright, RimSideShare of the points of the walls */
	WallFoot,
	/** Out of the inner face of a wall, at RimWallHeights of its height, its +Y out of the face as a rim that hangs (bRimHang), else its Z;
	 *  RimSideShare of the points of the walls */
	WallFace,
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

	/** Layers of the planet this kind stands on, a mask of the bits of EHexPlanetLayer: the surface, the underground or both */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Set", meta=(Bitmask, BitmaskEnum="/Script/HexHero.EHexPlanetLayer"))
	int32 PlanetLayers = GetPlanetLayerBit(EHexPlanetLayer::Surface);

	/** Returns true if this kind stands on a layer of the planet */
	bool IsOnLayer(EHexPlanetLayer PlanetLayer) const { return (PlanetLayers & GetPlanetLayerBit(PlanetLayer)) != 0; }

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

	/** Objects in a cell at a density of 1, 2 and 3 (a decor takes the first), before CountFactor of the variant; a pentagon takes a sixth less.
	 *  A rim takes the first as the most on one side (RimCountMin) */
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

	/** Keeps the whole of each mesh inside its cell on the sides towards a cell at least 2 levels lower, a void underground, and towards the cell of
	 *  an entrance: a point nearer such a side than the horizontal radius of the mesh about its pivot times its size, plus ObjectFootprintMargin of
	 *  the config, takes no object, so nothing hangs over the abyss from the ledge of a passage. Off for the kinds of the surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Placement")
	bool bClipFootprint = false;

	/** A rim stands on the sides of its cells towards a neighbor at least this many levels lower: 2 from a passage to a void */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=1, ClampMax=4))
	int32 RimMinDrop = 2;

	/** How far onto its cell a rim stands from the line of the side at most, as a share of the apothem, at random from 0, so near 0 it overhangs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0, ClampMax=0.5))
	float RimInset = 0.12f;

	/** Hangs down the cliff of the lower cell from the lip, as stalactites, instead of standing on the lip: its mesh hangs along -Z in front of the
	 *  face along +Y, its top at the height of the lip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
	bool bRimHang = false;

	/** How far past the line of the side, onto the lower cell, the face of the cliff is looked up for a rim that hangs, in world units */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0.1))
	float RimHangOut = 2.0f;

	/** A rim that hangs needs a face there steeper than this: where the normal of the ground looked up past the side lies nearer the up of the
	 *  planet (its dot with it is higher), it is no cliff, and the try is skipped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0, ClampMax=1))
	float RimHangMaxUp = 0.9f;

	/** Most degrees the X of a mesh turns away from the line of its side, at random either way */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0, ClampMax=90))
	float RimTurn = 25.0f;

	/** Fewest objects on a side that takes the kind; the most is the first of CountByDensity, so no side stays a straight edge of a hex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0))
	int32 RimCountMin = 0;

	/** Share of the sides that take the kind at all, by a hash of the cell, the side and the Id */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0, ClampMax=1))
	float RimSideShare = 1.0f;

	/** Share of a side kept free of the kind at each of its corners, where the lip turns to the next side; the objects spread over the rest */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0, ClampMax=0.45))
	float RimCornerGap = 0.08f;

	/** Where the kind stands on a layer with walls; on a layer without them it stands on the lip */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
	EHexRimSeat RimSeat = EHexRimSeat::Lip;

	/** The heights out of the inner face of a wall a rim of EHexRimSeat::WallFace takes, at random between them, as shares of the height of the wall */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
	FVector2D RimWallHeights = FVector2D(0.15, 0.7);

	/** Kinds of one group share one noise of patches in the generation of the underground, so the decor of a group stands in the hearts of the
	 *  patches of its cover and a fungus grove or a crystal cavern is one place; None for a noise of its own */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation")
	FName GenerateGroup;

	/** Size of the patches of the generation of the underground, in cells; 0 places the kind anywhere at random. The surface keeps ObjectNoiseScale
	 *  of DA_HexPlanet and its own rules */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation", meta=(ClampMin=0))
	float GeneratePatchCells = 0.0f;

	/** The densities a cover takes in the generation of the underground, by its rank in the cells it takes, highest first: the first value is the
	 *  share of them that are dense (3), in the middle of a patch, the second the share that are dense or medium (2), and the rest are sparse (1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation")
	TArray<float> GenerateDensityShares = {0.4f, 0.75f};

	/** The generation of the underground keeps the cells within this many rings of an exit free of the kind, 0 for the exit cell alone, so the
	 *  marker of an exit stays the brightest thing of its hall and nothing tall hides it; a rim leaves out the sides of those cells */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation", meta=(ClampMin=0, ClampMax=3))
	int32 ExitClearRings = 0;

	/** Emissive color of the vertices whose alpha is 1, which M_HexPlanetCave takes as a parameter of the same name (HexMaterialConfig::Apply of
	 *  the set); no light of its own, so Lumen alone may carry it onto the ground around */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glow")
	FLinearColor GlowColor = FLinearColor::Black;

	/** Strength of the glow, 0 for none */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glow", meta=(ClampMin=0))
	float GlowStrength = 0.0f;

	/** Share of the glow that breathes, GlowPulseSpeed radians a second, each instance at its own phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glow", meta=(ClampMin=0, ClampMax=1))
	float GlowPulse = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glow", meta=(ClampMin=0))
	float GlowPulseSpeed = 1.0f;

	/** How strongly the kind lights the floor of its cell in GlowColor, a pool of its glow on the ground that spills a little into the cells beside it
	 *  (UHexPlanetObjects::GetCellGlow, CellCanopy of the underground, the Glow pools values of DA_HexUnderground) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glow", meta=(ClampMin=0, ClampMax=1))
	float GlowPool = 0.0f;

	/** The share of GlowPool a cover lights by its density, sparse (1) to dense (3), as a sparse cover lights less of its floor; a decor takes all */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glow")
	TArray<float> GlowPoolByDensity = {0.6f, 0.8f, 1.0f};

	/** Spores in GlowColor rising from each cell with this kind near the camera (UHexPlanetMotes, the Spores values of DA_HexUnderground) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Glow", meta=(ClampMin=0))
	int32 Spores = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering")
	bool bCastShadow = true;

	/** Distance from the camera past which it is not drawn, 0 always */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(ClampMin=0))
	float CullDistance = 9000.0f;

	/** Share of the ground a cover of this kind shades at its densest, by which M_HexPlanet tints the ground of its cells with FarColor, fully
	 *  from far away where the objects are culled and a little under them, so forests show from orbit; 0 for kinds that do not show (CellCanopy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Far", meta=(ClampMin=0, ClampMax=1))
	float FarCover = 0.0f;

	/** Color of the ground under a cover of this kind from far away */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Far")
	FLinearColor FarColor = FLinearColor(0.035f, 0.07f, 0.03f);

	/** How far the snow of a variant turns FarColor to white */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Far", meta=(ClampMin=0, ClampMax=1))
	float FarSnowShare = 0.6f;

	/** Returns the variant a biome draws, its own or the one for every biome, else null (FindHexObjectVariant) */
	const FHexObjectVariant* FindVariant(TCHAR BiomeLetter) const;

#if WITH_EDITOR
	/** Gives a mesh a LOD per share of its triangles, reduced from the first, each drawn from its screen size, and builds it. Returns the number
	 *  of LODs. For import_planet_objects.py, as the editor subsystem that does it is missing in a commandlet */
	UFUNCTION(BlueprintCallable, Category="Objects")
	static int32 SetMeshLods(UStaticMesh* Mesh, const TArray<float>& TriangleShares, const TArray<float>& ScreenSizes);
#endif
};
