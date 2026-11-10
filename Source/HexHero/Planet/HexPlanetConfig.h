// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetConfig.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Where the corners and the sides of the cells lie between two levels (HexPlanetKit::FindCornerHeights, HexPlanetKit::BuildFrame) */
UENUM()
enum class EHexEdgeRule : uint8
{
	/** A corner at the mean top of its three cells and the middle of a side at the mean of its two, so two levels share the slope between
	 *  them and the side of a cell lies halfway down it, as on the surface */
	Shared,
	/** A corner at the highest top of its three cells and a side at the higher of its two, so a higher cell stays flat up to its sides and a
	 *  lower one carries the whole slope inside itself, as the underground does, whose passages are ledges over the voids */
	HigherHolds,
};

/**
 *  A style of the walls of the underground (UHexPlanetConfig::WallStyles): factors on the values of the walls where it holds, and how the material
 *  shades its stone. The styles hold in regions of a noise of the place each (WallStyleScale), blending where they meet (WallStyleSharpness), so a
 *  stretch of wall is a curtain of stalactites, the next smooth flowstone, then broken rock or a row of columns
 */
USTRUCT(BlueprintType)
struct FHexWallStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Style")
	FName Name;

	/** How much of the walls it takes, against the weights of the others */
	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float Weight = 1.0f;

	/** Factors on WallHeight, WallThickness, WallLean, WallOverhang, WallBulge, WallCrestJag, WallCrestNotch, WallRibSize, WallRibDepth, the tips of
	 *  the stalactites (WallRibTipMin and WallRibTipMax, so below 1 they hang lower), WallColumnShare and WallFluteDepth */
	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0.1))
	float Height = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0.1))
	float Thickness = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float Lean = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float Overhang = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float Bulge = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float CrestJag = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float CrestNotch = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0.2))
	float RibSize = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float RibDepth = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0.1))
	float RibTip = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float ColumnShare = 1.0f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0))
	float FluteDepth = 1.0f;

	/** How much its stone shades as flowstone, lighter, warmer and wetter (CaveWallFlow*), and how much it shows its joints, layers, cracks and
	 *  veins (CaveWallFacet*, CaveWallStrata, CaveWallVein*), each 0 to 1 */
	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0, ClampMax=1))
	float Flowstone = 0.3f;

	UPROPERTY(EditAnywhere, Category="Style", meta=(ClampMin=0, ClampMax=1))
	float Fracture = 0.5f;
};

/** The styles of the walls a config starts with: a curtain of stalactites under an overhang, smooth flowstone in long drapes, broken rock that
 *  splits along its joints, and organ pipes, a row of columns down to the floor */
inline TArray<FHexWallStyle> MakeDefaultWallStyles()
{
	TArray<FHexWallStyle> Styles;
	FHexWallStyle& Curtain = Styles.AddDefaulted_GetRef();
	Curtain.Name = TEXT("Stalactites");

	FHexWallStyle& Flowstone = Styles.AddDefaulted_GetRef();
	Flowstone.Name = TEXT("Flowstone");
	Flowstone.Weight = 0.8f;
	Flowstone.Height = 0.9f;
	Flowstone.Thickness = 1.1f;
	Flowstone.Lean = 0.6f;
	Flowstone.Overhang = 0.4f;
	Flowstone.Bulge = 1.8f;
	Flowstone.CrestJag = 0.4f;
	Flowstone.CrestNotch = 0.5f;
	Flowstone.RibSize = 1.8f;
	Flowstone.RibDepth = 1.4f;
	Flowstone.RibTip = 0.55f;
	Flowstone.ColumnShare = 1.5f;
	Flowstone.FluteDepth = 0.5f;
	Flowstone.Flowstone = 1.0f;
	Flowstone.Fracture = 0.1f;

	FHexWallStyle& Broken = Styles.AddDefaulted_GetRef();
	Broken.Name = TEXT("BrokenRock");
	Broken.Weight = 0.9f;
	Broken.Height = 0.85f;
	Broken.Thickness = 0.95f;
	Broken.Lean = 2.0f;
	Broken.Overhang = 0.3f;
	Broken.Bulge = 1.4f;
	Broken.CrestJag = 1.8f;
	Broken.CrestNotch = 1.6f;
	Broken.RibSize = 1.3f;
	Broken.RibDepth = 0.35f;
	Broken.RibTip = 1.15f;
	Broken.ColumnShare = 0.3f;
	Broken.FluteDepth = 0.4f;
	Broken.Flowstone = 0.0f;
	Broken.Fracture = 1.0f;

	FHexWallStyle& Columns = Styles.AddDefaulted_GetRef();
	Columns.Name = TEXT("OrganPipes");
	Columns.Weight = 0.6f;
	Columns.Height = 1.2f;
	Columns.Overhang = 0.7f;
	Columns.Bulge = 0.8f;
	Columns.CrestJag = 1.4f;
	Columns.CrestNotch = 1.2f;
	Columns.RibSize = 0.7f;
	Columns.RibDepth = 1.3f;
	Columns.RibTip = 0.4f;
	Columns.ColumnShare = 4.0f;
	Columns.FluteDepth = 1.4f;
	Columns.Flowstone = 0.6f;
	Columns.Fracture = 0.3f;
	return Styles;
}

/**
 *  The planets: the size of their cells, how levels are generated for a planet without levels.txt, the colors of its surface, the outlines of the cell under the mouse and the values of
 *  M_HexPlanet and M_HexPlanetWireframe. AHexPlanet gives every float and color of it to those materials as parameters of the same names,
 *  once when they are made. Tools/create_planet_assets.py creates DA_HexPlanet only when it is missing.
 */
UCLASS(BlueprintType)
class UHexPlanetConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Distance from the center of a hex to its corners, the same on every planet, which with the frequency of a planet sets its radius */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=1))
	float HexRadius = 100.0f;

	/** Lattice steps along a side of a chunk of the surface, a triangle of a face of the icosahedron with a mesh component of its own, so what
	 *  lies behind the planet or off the screen is culled and a change of levels rebuilds less; smaller chunks cull more but cost more draw
	 *  calls, and 0 makes each face one chunk */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0))
	int32 ChunkSteps = 27;

	/** Distance from the camera beyond which the surface draws the coarse mesh of its chunks, 12 triangles a cell at the heights of its
	 *  piece at the center, corners and side middles, in place of the pieces; 0 always draws the pieces */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0))
	float CoarseDistance = 9000.0f;

	/** Half the width of the band around CoarseDistance over which the pieces dissolve into the coarse mesh pixel by pixel, so the switch
	 *  does not pop; a chunk that reaches into it draws both with the masked M_HexPlanet_Cut */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=1))
	float CoarseBand = 1500.0f;

	/** Only the chunks near the camera keep their pieces on the GPU, as the others draw their coarse meshes: a chunk whose nearest point
	 *  comes within this much past CoarseDistance + CoarseBand has them built in the background, ahead of the camera */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0))
	float PieceStreamAhead = 1500.0f;

	/** How much further than that a chunk keeps its pieces before they are freed, so a camera that moves to and fro does not build them again */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0))
	float PieceStreamKeep = 1500.0f;

	/** How many chunks one build of pieces in the background takes at most, the nearest first */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=1))
	int32 PieceStreamChunks = 8;

	/** Height of one level of land above the sea */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0))
	float LevelHeight = 25.0f;

	/** Where the corners and sides of the cells lie between two levels: Shared on the surface, HigherHolds in the underground (DA_HexUnderground),
	 *  so a passage stays flat up to its sides and a void carries its whole cliff inside its own cell */
	UPROPERTY(EditAnywhere, Category="Grid")
	EHexEdgeRule EdgeRule = EHexEdgeRule::Shared;

	/** Frequency of the planet that the values of scale and speed in DA_HexPlanet and DA_HexPlanetClouds are tuned on, that of Terra.
	 *  On another planet the ranges of mountains and the clouds scale by its frequency over this one, so they keep their size and speed in
	 *  cells, and a smaller planet has fewer of them rather than the same ones, larger. GetSizeRatio gives that ratio */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=1))
	int32 ReferenceFrequency = 80;

	/** Returns the frequency of a planet over ReferenceFrequency, by which sizes of noise on the unit sphere scale */
	double GetSizeRatio(int32 Frequency) const { return double(Frequency) / FMath::Max(ReferenceFrequency, 1); }

	/** What a layer of the planet has, so the underground (DA_HexUnderground) turns off what only the surface needs: the sea, which is the cap
	 *  of water, its grid lines, the sea and shore fields, the floes, the shore ice and the mouths of rivers; materials of the surface that read
	 *  their textures get empty ones */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bSea = true;

	/** The shells of clouds and their shadows */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bClouds = true;

	/** The haze of the air, a post process */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bHaze = true;

	/** The auroras over the pentagons */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bAuroras = true;

	/** Rivers and bridges, read from rivers.json of the data folder of the layer or generated */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bRivers = true;

	/** Generates forests and decor from the shares of the variants of the object sets when the layer has no objects.json; underground the cave
	 *  kinds in patches on the passages (HexPlanetUnderground::GenerateObjects) */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bGenerateObjects = true;

	/** The fog of the underground, a post process (M_HexUndergroundFog) in place of the haze, with the Fog values of the category Underground */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bFog = false;

	/** Faint motes drifting over the passages around the point the camera looks at, and spores rising from the kinds of objects with Spores
	 *  (UHexPlanetMotes, M_HexPlanetMote), with the values of the categories Underground|Motes and Underground|Spores */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bMotes = false;

	/** Scale of the largest octave of the noise of levels, on the unit sphere, divided by ContinentSize of a planet; the shares of levels are in planet.json */
	UPROPERTY(EditAnywhere, Category="Levels", meta=(ClampMin=0.01))
	float LevelNoiseScale = 1.6f;

	/** Scale of the noise whose zero lines are the ranges of mountains, on a planet of ReferenceFrequency */
	UPROPERTY(EditAnywhere, Category="Levels|Ranges", meta=(ClampMin=0.01))
	float RangeNoiseScale = 2.4f;

	/** Scale of the noise that wobbles the ranges, on a planet of ReferenceFrequency */
	UPROPERTY(EditAnywhere, Category="Levels|Ranges", meta=(ClampMin=0.01))
	float RangeWobbleScale = 7.0f;

	/** How far the ranges wobble */
	UPROPERTY(EditAnywhere, Category="Levels|Ranges", meta=(ClampMin=0))
	float RangeWobble = 0.08f;

	/** Noise of levels above the lowest land from which land can hold mountains, so they keep off the coast */
	UPROPERTY(EditAnywhere, Category="Levels|Ranges")
	float RangeMinLand = 0.04f;

	/** How much wider ranges grow inland, per unit of the noise of levels above RangeMinLand */
	UPROPERTY(EditAnywhere, Category="Levels|Ranges", meta=(ClampMin=0))
	float RangeInlandWidening = 0.05f;

	/** Noise of levels above RangeMinLand past which ranges grow no wider */
	UPROPERTY(EditAnywhere, Category="Levels|Ranges", meta=(ClampMin=0))
	float RangeInlandReach = 0.4f;

	/** Scale of the noises of temperature and moisture of the biomes, on the unit sphere, so climate zones keep their share of any planet */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0.01))
	float ClimateNoiseScale = 2.2f;

	/** How far the noise moves the temperature, which is 1 at the equator and 0 at the poles */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0))
	float TemperatureNoise = 0.1f;

	/** How much colder each level above plains is */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0))
	float HeightCooling = 0.05f;

	/** Temperature below which the sea freezes, an ice cap around each pole */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0, ClampMax=1))
	float IceTemperature = 0.2f;

	/** Temperature below which land is snow */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0, ClampMax=1))
	float SnowTemperature = 0.13f;

	/** Temperature below which land is tundra */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0, ClampMax=1))
	float TundraTemperature = 0.27f;

	/** Cells from the sea over which the moisture of the land falls to a third, on a planet of ReferenceFrequency */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0.1))
	float MoistureReach = 12.0f;

	/** How far the noise moves the moisture, which is 1 at the coast */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0))
	float MoistureNoise = 0.3f;

	/** Temperature of the dry belts on both sides of the equator, where the moisture falls by DryBelt, as deserts lie on Earth */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0, ClampMax=1))
	float DryBeltTemperature = 0.7f;

	/** Half the width of the dry belts in temperature */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0.01))
	float DryBeltWidth = 0.12f;

	/** How much drier the dry belts are */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0))
	float DryBelt = 0.3f;

	/** Moisture below which warm land is desert */
	UPROPERTY(EditAnywhere, Category="Biomes")
	float DesertMoisture = 0.15f;

	/** Temperature above which dry land is desert and not steppe */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0, ClampMax=1))
	float DesertTemperature = 0.5f;

	/** Moisture below which land is steppe */
	UPROPERTY(EditAnywhere, Category="Biomes")
	float SteppeMoisture = 0.32f;

	/** Moisture above which warm plains are swamp */
	UPROPERTY(EditAnywhere, Category="Biomes")
	float SwampMoisture = 0.88f;

	/** Temperature above which wet plains are swamp */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0, ClampMax=1))
	float SwampTemperature = 0.55f;

	/** Moisture below which mountains that are not cold or desert are bare rock */
	UPROPERTY(EditAnywhere, Category="Biomes")
	float MountainRockMoisture = 0.5f;

	/** Size of a texture tile of the biome layers in world units, which M_HexPlanet projects from three planes of the world */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=1))
	float BiomeTileSize = 320.0f;

	/** Width of the blend of two biomes on each side of the side between their cells, in units of a cell whose corners lie at 1 (so its apothem
	 *  is 0.87); the width and the shift by the noise below together stop short of the apothem, which BIOME_CODE enforces */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0.01, ClampMax=0.5))
	float BiomeBlendWidth = 0.3f;

	/** How far a noise moves the line between two biomes off the side, in the same units, so the line winds and is not straight */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=0.5))
	float BiomeBlendNoise = 0.32f;

	/** Size of that noise in world units */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=1))
	float BiomeBlendNoiseScale = 45.0f;

	/** A finer noise added to it, so the line is ragged: its strength as a share of the first, and its size as a share of BiomeBlendNoiseScale */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=1))
	float BiomeBlendDetail = 0.5f;

	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0.05, ClampMax=1))
	float BiomeBlendDetailScale = 0.3f;

	/** Height above the sea up to which the slopes of the coast take the beach of their layer (sand, snow, the rock of a mountain) instead of the
	 *  rock of slopes, and over which it fades out: under the top of the plains, so the slope from the plains to the sea is beach and the foot of a hill is not */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0))
	float ShoreSlopeTop = 16.0f;

	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0))
	float ShoreSlopeFade = 6.0f;

	/** How much the height of a layer, its alpha, adds to its share where layers blend, so the higher parts of one cover the other first and
	 *  the blend is ragged; under 1 - BiomeBlendDepth, so a layer never shows past the end of its blend */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=0.8))
	float BiomeHeightBlend = 0.5f;

	/** A noise of each layer of its own added to that, so the edge between two layers runs in patches and not along a line; with
	 *  BiomeHeightBlend under 1 - BiomeBlendDepth, for the same reason. Its size in world units */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=0.5))
	float BiomeEdgeNoise = 0.3f;

	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=1))
	float BiomeEdgeNoiseScale = 18.0f;

	/** How far the line between a mountain and a neighbor that is not one moves into the neighbor, in the units of BiomeBlendWidth, so the rock
	 *  of the mountain covers its foot and its shore */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=0.6))
	float MountainBlendShift = 0.3f;

	/** How far the line between land and a neighbor under the sea moves into the sea, as MountainBlendShift, so the part of the cell of the
	 *  sea that its winding coast lifts above the water draws the land (a snowy coast snow, not the sand of the seabed), and below the water
	 *  the beach of the land */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=0.6))
	float CoastBlendShift = 0.35f;

	/** Height below which the beach of a layer turns into its seabed (PlanetSeabedLayer of the biomes), fully SeabedFade lower, so snow does
	 *  not go on under the sea; at the water, so a snowy shore stays snow above it and the sand only shows a little below it */
	UPROPERTY(EditAnywhere, Category="Textures")
	float SeabedHeight = 0.0f;

	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0.01))
	float SeabedFade = 6.0f;

	/** How much the heights of the beach and the seabed decide which lies where in that band, so snow stays in the hollows of the sand */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0))
	float SeabedHeightBlend = 0.6f;

	/** Width of the height over which they blend there */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0.01))
	float SeabedBlendDepth = 0.15f;

	/** How much the beach (snow) dusts the seabed in the upper part of that band, so the shore reads as snowy sand */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=1))
	float SeabedDusting = 0.7f;

	/** Cells of sea from snow land that freeze into shore ice when biomes are generated (HexPlanetGenerator::AddShoreIce), 0 for none */
	UPROPERTY(EditAnywhere, Category="Biomes", meta=(ClampMin=0, ClampMax=4))
	int32 ShoreIceRings = 1;

	/** Below the highest share plus height, how far another layer still shows: smaller makes the edges between layers sharper */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0.01, ClampMax=1))
	float BiomeBlendDepth = 0.15f;

	/** Strength of the normals of the layers */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0))
	float BiomeNormalStrength = 0.7f;

	/** Power of the normal by which the three planes of the projection blend; higher blends less, and samples fewer planes */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=1))
	float TriplanarSharpness = 6.0f;

	/** Share of its color the seabed keeps at DeepDepth below the sea */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=1))
	float SeabedDarkening = 0.45f;

	/** How far a noise moves the height of the line of the beach and of snow, in world units */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0))
	float BeachNoise = 3.0f;

	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0))
	float SnowNoise = 40.0f;

	/** The layer of the biome textures that the tint of grass below changes (Grass in LAYERS of build_biome_textures.py) */
	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0))
	float GrassTintLayer = 0.0f;

	/** Color that dry grass turns towards, multiplied by 0.75 to 1.25 by the height in the alpha of the grass, so it keeps its texture */
	UPROPERTY(EditAnywhere, Category="Textures|Grass")
	FLinearColor GrassDryColor = FLinearColor(0.2f, 0.16f, 0.065f);

	/** Height above the sea from which grass dries towards GrassDryColor, and where it reaches GrassHillDry: the top of the plains to above the
	 *  top of the hills, so hills are a little sandier the higher they are */
	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0))
	float GrassDryStart = 27.0f;

	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0))
	float GrassDryEnd = 70.0f;

	/** Share of GrassDryColor at GrassDryEnd; small, so it still reads as grass */
	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0, ClampMax=1))
	float GrassHillDry = 0.3f;

	/** Share of GrassDryColor in the streaks on the flat tops of hills, which come in from GrassDryStart to GrassDryEnd */
	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0, ClampMax=1))
	float GrassStreak = 0.45f;

	/** Size of the streaks in world units, how far a noise bends them, in cells of that noise, and how wide they are (0 to 1) */
	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=1))
	float GrassStreakScale = 60.0f;

	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0))
	float GrassStreakWarp = 2.5f;

	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0.01, ClampMax=1))
	float GrassStreakWidth = 0.35f;

	/** Rare sandy patches on any grass: their share of GrassDryColor, their size in world units, and the value of a noise of 0 to 1 above which
	 *  they lie, so higher makes them rarer */
	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0, ClampMax=1))
	float GrassPatch = 0.4f;

	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=1))
	float GrassPatchScale = 140.0f;

	UPROPERTY(EditAnywhere, Category="Textures|Grass", meta=(ClampMin=0, ClampMax=1))
	float GrassPatchCover = 0.68f;

	/** Depth of the seabed of the shallows below the sea */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=0))
	float ShallowDepth = 22.0f;

	/** Depth of the seabed of the deep sea below the sea */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=0))
	float DeepDepth = 70.0f;

	/**
	 *  How far up and down a noise of the place moves the ground on the slopes below the top of the plains (from the deep seabed to the shallows
	 *  and from the shallows or the seabed to the land), most halfway down a slope and not at all on the tops, so the coast and the edge of the
	 *  shelf wind across the cells instead of following their sides
	 */
	UPROPERTY(EditAnywhere, Category="Sea|Coast", meta=(ClampMin=0))
	float CoastWobble = 20.0f;

	/** Size of the large waves of the coast, in world units */
	UPROPERTY(EditAnywhere, Category="Sea|Coast", meta=(ClampMin=1))
	float CoastWobbleScale = 140.0f;

	/** Share of the wobble that a finer noise, about a third of the size, adds */
	UPROPERTY(EditAnywhere, Category="Sea|Coast", meta=(ClampMin=0, ClampMax=1))
	float CoastWobbleDetail = 0.35f;

	/** Passes of averaging with the neighbors that smooth the depth of the seabed, as the water sees it */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=0))
	int32 DepthSmoothing = 2;

	/** Size of each face of the cubemap of the sea */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=16, ClampMax=2048))
	int32 SeaFieldSize = 512;

	/** The distance to the coast in alpha of the sea field, which the breakers of the water follow, is a soft minimum of the distances to the
	 *  land cells around (-ShoreSoftness * ln of the sum of exp(-distance / ShoreSoftness)), so further out it follows the run of the coast
	 *  instead of wrapping around each cape, while at the shore it stays close to the distance to the nearest cell. 0 takes the nearest */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=0))
	float ShoreSoftness = 60.0f;

	/** The coast is cut into stretches that break at their own phase (the breakers of the water): each land cell on the coast is a stretch,
	 *  and with this chance takes a neighbor on the coast into it, so a stretch is one or two cells long; 0 keeps every cell a stretch of its own,
	 *  so each breaker breaks at its own time over a cell, and SurfLength of DA_HexPlanetWater caps how long it is */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=0, ClampMax=1))
	float ShoreStretchJoin = 0.0f;

	/** The real coast winds across the cells (land rises out of the shallows, the sea floods parts of land cells), so near it the distance to the
	 *  coast moves to be 0 just where the ground crosses the sea: by the value the field of the cells has at the points of the real coast nearby,
	 *  weighted towards the nearest over ShoreCoastBlend world units, fully within a third of ShoreCoastFade of that coast and fading out by it,
	 *  so further out the field of the cells stays */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=1))
	float ShoreCoastFade = 200.0f;

	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=1))
	float ShoreCoastBlend = 15.0f;

	/** Rings of the water cap from the point under the camera out to the horizon */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=4))
	int32 WaterRings = 300;

	/** Vertices around each ring of the water cap */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=8))
	int32 WaterSegments = 512;

	/** Width of the first ring of the water cap, at the point under the camera */
	UPROPERTY(EditAnywhere, Category="Sea", meta=(ClampMin=1))
	float WaterInnerStep = 6.0f;

	/** Color of each level from the deep sea up to mountains, where its kit gives none, for the palette and the tiles of painting, as the surface draws the biomes; the slope darkens it by SlopeDarkening */
	UPROPERTY(EditAnywhere, Category="Colors", EditFixedSize)
	TArray<FLinearColor> LevelColors = {FLinearColor(0.10f, 0.11f, 0.09f), FLinearColor(0.20f, 0.19f, 0.11f), FLinearColor(0.18f, 0.35f, 0.08f),
		FLinearColor(0.30f, 0.30f, 0.12f), FLinearColor(0.36f, 0.31f, 0.28f)};

	/** Share of its color a slope keeps where the kit gives no slope color */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0, ClampMax=1))
	float SlopeDarkening = 0.55f;

	/** Flatness, the cosine of the slope, below which the color is fully that of a slope */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0, ClampMax=1))
	float SlopeStart = 0.66f;

	/** How much the ground that FHexPlanetKit::FacetHeightStart and FacetHeightEnd break into faces is shaded flat by triangle, like split stone */
	UPROPERTY(EditAnywhere, Category="Colors|Facets", meta=(ClampMin=0, ClampMax=1))
	float FacetStrength = 0.3f;

	/** How far the faces lean off their triangles at random, by regions of FacetCell, so neighboring faces catch the light unlike */
	UPROPERTY(EditAnywhere, Category="Colors|Facets", meta=(ClampMin=0))
	float FacetTilt = 0.12f;

	/** Size of those regions, in world units */
	UPROPERTY(EditAnywhere, Category="Colors|Facets", meta=(ClampMin=1))
	float FacetCell = 22.0f;

	/** How dark the channel of a gully is, as a share of the color */
	UPROPERTY(EditAnywhere, Category="Colors|Facets", meta=(ClampMin=0, ClampMax=1))
	float CreviceDarkening = 0.55f;

	/** How far below SnowHeight snow reaches down a gully, in world units, and how much less flat the ground there may be */
	UPROPERTY(EditAnywhere, Category="Colors|Facets", meta=(ClampMin=0))
	float GullySnowReach = 60.0f;

	UPROPERTY(EditAnywhere, Category="Colors|Facets", meta=(ClampMin=0, ClampMax=1))
	float GullySnowFlatness = 0.4f;

	/** How much lighter or darker each boulder at the foot of mountains is at most (M_HexPlanetBoulder) */
	UPROPERTY(EditAnywhere, Category="Colors|Boulders", meta=(ClampMin=0, ClampMax=1))
	float BoulderTint = 0.18f;

	/** Scale of the noise whose highs get the forests, bushes and other covers of a generated planet (FHexObjectVariant::GenerateShare), on the
	 *  unit sphere of a planet of ReferenceFrequency, so they lie in patches of a few cells; a second octave twice as fine breaks their edges */
	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0.1))
	float ObjectNoiseScale = 9.0f;

	/** How much lighter or darker each tree, bush or flower is at most, and how much its hue moves (M_HexPlanetFoliage) */
	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0, ClampMax=1))
	float FoliageTint = 0.22f;

	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0, ClampMax=1))
	float FoliageHueShift = 0.12f;

	/** Size in world units and strength of the noise that breaks the leaves into lighter and darker clumps */
	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0.1))
	float FoliageNoiseScale = 9.0f;

	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0, ClampMax=1))
	float FoliageNoiseStrength = 0.45f;

	/** Leaves facing the sun get lighter, those facing away darker, as a canopy scatters light (FoliageTranslucency on the dark side) */
	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0, ClampMax=1))
	float FoliageTranslucency = 0.25f;

	/** Snow on objects whose variant has snow: from how much the surface faces up it starts and is full, and how much a noise breaks it */
	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=-1, ClampMax=1))
	float FoliageSnowUpStart = 0.05f;

	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=-1, ClampMax=1))
	float FoliageSnowUpEnd = 0.55f;

	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0, ClampMax=1))
	float FoliageSnowBreakup = 0.35f;

	/** Sway of the tops of trees and bushes in the wind, in world units, and its speed; the alpha of the vertex color says how much a vertex sways */
	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0))
	float WindStrength = 0.5f;

	UPROPERTY(EditAnywhere, Category="Objects", meta=(ClampMin=0))
	float WindSpeed = 1.3f;

	/** Share of the frozen sea that floes cover on the edge of the ice, next to open sea, and FloeDenseRings cells further in, between which it
	 *  grows. The sea between them stays open, so ships pass anywhere */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=1))
	float FloeEdgeCover = 0.12f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=1))
	float FloeInnerCover = 0.6f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=1))
	float FloeDenseRings = 3.0f;

	/** How much a noise of the place moves the cover up or down, so the pack has denser and looser patches */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=1))
	float FloeCoverNoise = 0.15f;

	/** Size of the patches of that noise, in cells */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0.1))
	float FloeCoverNoiseCells = 2.5f;

	/** Radius of the smallest and the largest floe, in world units, and the power on a random share between them, so higher gives more small ones */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0.1))
	float FloeSizeMin = 3.0f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0.1))
	float FloeSizeMax = 60.0f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0.1))
	float FloeSizePower = 2.5f;

	/** Radius of the largest floe on the edge of the ice, which grows to FloeSizeMax over FloeDenseRings, so the edge is loose and broken and the
	 *  inside holds large plates */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0.1))
	float FloeEdgeSizeMax = 22.0f;

	/** Radius below which a floe is a shard (ShardMeshes of FHexPlanetFloes) */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeShardSize = 9.0f;

	/** Least water between two floes, as a share of the radius of the smaller one; below 0 they may touch, which with their drift grinds them together */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=-0.5, ClampMax=2))
	float FloeGap = 0.08f;

	/** Tries to place a floe per cell, which sets how closely the cover is reached */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=1, ClampMax=500))
	int32 FloeTries = 120;

	/** Least distance of a floe from a side towards land, as a share of its radius, so it does not stand in the beach */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=2))
	float FloeShoreGap = 0.6f;

	/** How far a floe leans from the up of the planet at most, in degrees */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=30))
	float FloeTilt = 2.0f;

	/** Distance from the camera past which floes are not drawn, where the water takes their color instead (IceFar* of DA_HexPlanetWater), 0 always */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeCullDistance = 9000.0f;

	/** Distance from the camera past which floes stop moving, which saves the shadows they would cast anew every frame */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeMotionDistance = 4000.0f;

	UPROPERTY(EditAnywhere, Category="Floes")
	bool bFloeShadows = true;

	/** Colors of floes (M_HexPlanetFloe): their top, of snow and old ice, their sides, blue ice, and what lies under the water */
	UPROPERTY(EditAnywhere, Category="Floes")
	FLinearColor FloeTopColor = FLinearColor(0.66f, 0.72f, 0.78f);

	UPROPERTY(EditAnywhere, Category="Floes")
	FLinearColor FloeSideColor = FLinearColor(0.22f, 0.45f, 0.62f);

	UPROPERTY(EditAnywhere, Category="Floes")
	FLinearColor FloeUnderColor = FLinearColor(0.25f, 0.62f, 0.72f);

	/** How much of the snow layer of the biomes shows on the top of floes, and how much lighter or darker each floe is at most */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=1))
	float FloeSnowDetail = 0.5f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=1))
	float FloeTint = 0.08f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0, ClampMax=1))
	float FloeRoughness = 0.55f;

	/** How far each floe drifts around its place, in world units, and how fast, in radians a second; neighbors drift apart and together, so
	 *  they seem to grind */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeDrift = 7.0f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeDriftSpeed = 0.3f;

	/** How far each floe turns back and forth around its up, in radians, and how fast */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeSpin = 0.25f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeSpinSpeed = 0.18f;

	/** How high a floe of radius 10 bobs, in world units, and how fast; a larger one bobs less */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeBob = 0.6f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeBobSpeed = 0.9f;

	/** How far a floe of radius 10 rocks, in radians, and how fast; a larger one rocks less */
	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeRock = 0.06f;

	UPROPERTY(EditAnywhere, Category="Floes", meta=(ClampMin=0))
	float FloeRockSpeed = 0.7f;

	/** Flatness above which the color is fully that of flat ground */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0, ClampMax=1))
	float SlopeEnd = 0.8f;

	/** Color of the beach and of the seabed close to the sea */
	UPROPERTY(EditAnywhere, Category="Colors")
	FLinearColor SandColor = FLinearColor(0.42f, 0.38f, 0.24f);

	/** Height above the sea up to which land is sand */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0))
	float BeachHeight = 5.0f;

	/** Height over which the sand blends into the land above BeachHeight */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0.01))
	float BeachFade = 5.0f;

	/** Color of snow on mountains */
	UPROPERTY(EditAnywhere, Category="Colors")
	FLinearColor SnowColor = FLinearColor(0.85f, 0.87f, 0.92f);

	/** Height above the sea where snow starts on mountains */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0))
	float SnowHeight = 190.0f;

	/** Height over which snow grows to full above SnowHeight */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0.01))
	float SnowFade = 60.0f;

	/** Flatness below which no snow holds */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0, ClampMax=1))
	float SnowFlatStart = 0.5f;

	/** Flatness above which snow is full */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0, ClampMax=1))
	float SnowFlatEnd = 0.8f;

	/** Background of the loading screen shown while the planet is built */
	UPROPERTY(EditAnywhere, Category="Loading")
	FLinearColor LoadingBackgroundColor = FLinearColor(0.004f, 0.006f, 0.012f, 1.0f);

	/** Text of the loading screen */
	UPROPERTY(EditAnywhere, Category="Loading")
	FLinearColor LoadingTextColor = FLinearColor(0.75f, 0.8f, 0.9f, 1.0f);

	/** Outline of the cell under the mouse */
	UPROPERTY(EditAnywhere, Category="Highlight")
	FLinearColor OutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Outlines of its neighbors, while the debug wireframe draws them too */
	UPROPERTY(EditAnywhere, Category="Highlight")
	FLinearColor NeighborOutlineColor = FLinearColor(0.05f, 0.7f, 1.0f, 1.0f);

	/** Outline of the hole that hex.Water 3 makes in the water around the cell under the mouse, drawn on the water around it, while the
	 *  seabed shows the outline of the cell itself */
	UPROPERTY(EditAnywhere, Category="Highlight")
	FLinearColor WaterHideOutlineColor = FLinearColor(0.55f, 0.9f, 1.0f, 1.0f);

	/** Outline of the edge of the brush while painting */
	UPROPERTY(EditAnywhere, Category="Highlight")
	FLinearColor BrushOutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Width of each half of an outline, which the cells on both sides of its edge draw on their grid lines, as a share of the way from a side to the center */
	UPROPERTY(EditAnywhere, Category="Highlight", meta=(ClampMin=0.01, ClampMax=0.5))
	float MarkWidth = 0.05f;

	/** Width of an outline in pixels at least, so it shows on cells far away */
	UPROPERTY(EditAnywhere, Category="Highlight", meta=(ClampMin=0))
	float MarkMinPixels = 1.5f;

	/** How much an outline glows, so it shows on the night side too */
	UPROPERTY(EditAnywhere, Category="Highlight", meta=(ClampMin=0))
	float MarkGlow = 0.5f;

	/** Height above the sea at least of the tile of a painted cell of the sea, so it shows over the waves */
	UPROPERTY(EditAnywhere, Category="Painting", meta=(ClampMin=0))
	float PaintSeaLift = 8.0f;

	/** Height of a painted cell, shown flat in the color of its new level until the surface is rebuilt, above the top of the higher of its old
	 *  and new levels, or above the sea for a cell of the sea */
	UPROPERTY(EditAnywhere, Category="Painting", meta=(ClampMin=0))
	float PaintLift = 2.0f;

	/** Height above the sea taken as the top of a mountain cell for a painted cell, so its flat tile covers the mountain that was there */
	UPROPERTY(EditAnywhere, Category="Painting", meta=(ClampMin=0))
	float PaintMountainTop = 120.0f;

	/** Most changes of levels that Undo keeps */
	UPROPERTY(EditAnywhere, Category="Painting", meta=(ClampMin=1))
	int32 PaintUndoSteps = 100;

	/** How much a click of the brush of relief makes a mountain or hill higher, or lower by its inverse */
	UPROPERTY(EditAnywhere, Category="Painting", meta=(ClampMin=1.01))
	float PaintReliefStep = 1.2f;

	/** Height above the sea from which the rock of the biome covers the ground whatever its slope, and where it covers all of it, so a low
	 *  mountain reads as rock and not grass; hills stay below it */
	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0))
	float HighRockStart = 85.0f;

	UPROPERTY(EditAnywhere, Category="Colors", meta=(ClampMin=0))
	float HighRockEnd = 115.0f;

	/** A rock with a ScreeLayer or GravelLayer (UHexTerrainLayersConfig) is its cliff where the flatness (the up component of the normal) is
	 *  below RockCliffFlatStart, fading into the scree by RockCliffFlatEnd */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float RockCliffFlatStart = 0.6f;

	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float RockCliffFlatEnd = 0.78f;

	/** Flatness from which the scree of a rock turns into its gravel, all of it by RockGravelFlatEnd */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float RockGravelFlatStart = 0.86f;

	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float RockGravelFlatEnd = 0.95f;

	/** How much a gully (Y of the second UV) fills with scree in place of cliff, as loose stone gathers in it */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float RockGullyScree = 0.6f;

	/** How far a noise moves the flatness at which cliff, scree and gravel meet, so they do not follow the slope like contour lines */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=0.5))
	float RockSetNoise = 0.12f;

	/** World units of that noise */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=1))
	float RockSetNoiseScale = 45.0f;

	/** How much the height of cliff, scree and gravel decides which covers the other where they meet, as BiomeHeightBlend does for layers */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0))
	float RockSetHeightBlend = 0.6f;

	/** Width of the height over which they blend, as BiomeBlendDepth */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0.01))
	float RockSetBlendDepth = 0.15f;

	/** How much of the rock on the slopes of snow ground (the snow layer) is snowed over, so a steep hill of snow stays snowy */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float SnowRockCover = 0.95f;

	/** How much the high edges of that rock, by its height, peek out of the snow; little, as on the slope of a hill to the sea more read as
	 *  patches of sand */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float SnowRockPeek = 0.25f;

	/** Colors of the debug wireframe in the cell under the mouse and in its neighbors, as on the flat map */
	UPROPERTY(EditAnywhere, Category="Wireframe")
	FLinearColor WireHoverColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Wireframe")
	FLinearColor WireNeighborColor = FLinearColor(0.05f, 0.7f, 1.0f, 1.0f);

	/** Color of the debug wireframe of the whole surface (M_HexPlanetWireframe) */
	UPROPERTY(EditAnywhere, Category="Wireframe")
	FLinearColor WireColor = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);

	/** Opacity of the debug wireframe */
	UPROPERTY(EditAnywhere, Category="Wireframe", meta=(ClampMin=0, ClampMax=1))
	float WireOpacity = 1.0f;

	/** How far the wireframe is drawn towards the camera, per 1000 units of distance, so it does not fight with the surface */
	UPROPERTY(EditAnywhere, Category="Wireframe", meta=(ClampMin=0))
	float WireBias = 0.5f;

	/** Width of a grid line of M_HexPlanet as a share of the way from the center of a cell to its side */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float GridWidth = 0.012f;

	/** Narrowest a grid line gets, in pixels, so it does not flicker far away */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float GridMinPixels = 0.7f;

	/** Widest half of a grid line gets, in pixels (each cell draws the half inside its side), so a line on a slope facing the camera is no wider
	 *  than one on flat ground seen at a slant */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float GridMaxPixels = 1.2f;

	/** Opacity of the grid lines, which G hides */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float GridOpacity = 0.7f;

	/** Share of its color a grid line keeps */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float GridDarkness = 0.3f;

	/** Height above the sea, as a share of the mean peak of mountains, where the grid line starts to fade */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float GridFadeStart = 0.15f;

	/** Height where the grid line is gone */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float GridFadeEnd = 0.5f;

	/** Depth below the sea, as a share of the mean peak of mountains, over which the grid line of the seabed fades out, as the water draws its own */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0.001))
	float GridSeaFade = 0.02f;

	/** Share of its color the water keeps under a grid line of M_HexPlanetWaterGrid */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float WaterGridDarkness = 0.3f;

	/** Share of the tint of a cover (UHexPlanetObjectSet::FarCover) the ground keeps under its objects close by, darker under a forest */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=0, ClampMax=1))
	float CanopyNearShare = 0.35f;

	/** Distance from the camera from which the tint grows to all of it, at CanopyFadeEnd, about where the forests are culled */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=0))
	float CanopyFadeStart = 3500.0f;

	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=0))
	float CanopyFadeEnd = 6500.0f;

	/** Where the tint ends towards a side whose neighbor has none, as U of the cell, 1 on the side */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=0, ClampMax=1.2))
	float CanopyEdge = 0.95f;

	/** Width over which it fades out there */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=0.01, ClampMax=1))
	float CanopyEdgeSoftness = 0.3f;

	/** How far a noise moves that edge, so the edge of a forest is ragged */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=0))
	float CanopyEdgeNoise = 0.35f;

	/** Size of the noise of the edge, in world units */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=1))
	float CanopyEdgeNoiseScale = 45.0f;

	/** How much a noise lightens and darkens the tint, so a forest from far away is not one flat color */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=0, ClampMax=1))
	float CanopyMottle = 0.3f;

	/** Size of that noise, in world units */
	UPROPERTY(EditAnywhere, Category="Canopy", meta=(ClampMin=1))
	float CanopyMottleScale = 30.0f;

	/** Color of the light of the sky, the same from every direction (AHexPlanet::SkyLight), which lights what the sun does not reach */
	UPROPERTY(EditAnywhere, Category="Light")
	FLinearColor SkyLightColor = FLinearColor(0.62f, 0.74f, 1.0f, 1.0f);

	/** Intensity of the light of the sky */
	UPROPERTY(EditAnywhere, Category="Light", meta=(ClampMin=0))
	float SkyLightIntensity = 1.0f;

	/** Light the surface gives by itself, so the night side is not black */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float Ambient = 0.06f;

	/** Glow of the outlines of the cell under the mouse */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float Glow = 1.5f;

	/** Roughness of the surface */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float Roughness = 0.85f;

	/** Rivers of a generated planet per thousand cells of land (UHexPlanetRivers, HexPlanetRivers::Generate); each runs along the sides of cells
	 *  from the foot of mountains or hills down to the sea or into another river */
	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=0))
	float RiverDensity = 1.2f;

	/** Fewest and most sides of cells a generated river runs along; a shorter one is left out */
	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=1))
	int32 RiverMinLength = 6;

	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=1))
	int32 RiverMaxLength = 120;

	/** How many sides in a row a generated river may run without coming nearer the sea, so it winds over flat land */
	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=0))
	int32 RiverFlatSteps = 3;

	/** How much a generated river shuns turning the same way at two corners in a row, which takes it off to the side, against how much nearer
	 *  the sea a side takes it (1 for a side straight towards it), so it runs straight */
	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=0))
	float RiverStraightness = 0.5f;

	/** Randomness in the side a generated river takes at a corner, on the same scale */
	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=0))
	float RiverWander = 0.2f;

	/** Fewest rings of cells from the sea at which a generated river may start */
	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=1))
	int32 RiverSourceSeaDistance = 4;

	/** Sides of a river between the bridges a generated planet puts on it, about; 0 puts none */
	UPROPERTY(EditAnywhere, Category="Rivers|Generation", meta=(ClampMin=0))
	int32 BridgeSpacing = 9;

	/** Factor on the cost of a move across a side with a river and no bridge; a bridge takes it away (UHexPlanetRivers::GetCrossingFactor) */
	UPROPERTY(EditAnywhere, Category="Rivers|Movement", meta=(ClampMin=1))
	float RiverCrossCost = 2.0f;

	/** Width of the water of a river, in world units; every river has the same width, so a bridge fits any of them */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=1))
	float RiverWidth = 28.0f;

	/** How many times the line of a river through the middles of its sides is rounded off (HexPlanetRivers::BuildCurve), so it cuts the
	 *  corners of the zigzag of the sides and runs straight where they do */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=6))
	int32 RiverSmoothing = 3;

	/** How straight a river runs, and so how far it leaves the sides of the cells it belongs to: how far each corner between two of its sides is
	 *  pulled onto the chord between their middles, 0 along the sides and 1 straight through their middles, where a zigzag of sides becomes a
	 *  straight line about a quarter of a cell from the corners; then how many passes straighten it further over several sides, and how far it
	 *  may leave its sides at most, in world units, 0 for no limit. hex.PlanetRiverShape tries them while the game runs */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=1))
	float RiverCornerCut = 1.0f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0, ClampMax=50))
	int32 RiverRelax = 0;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverMaxOffset = 0.0f;

	/** How far the line of a river winds off that, in world units, and the size of the noise of its bends; it fades in over RiverMeanderFade
	 *  of a cell from the ends of a line, so lines meet where rivers join */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverMeander = 8.0f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=1))
	float RiverMeanderScale = 150.0f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverMeanderFade = 0.35f;

	/** How far from its source a river widens from a spring to its full width, as a share of the radius of a cell */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0.01))
	float RiverSourceTaper = 0.9f;

	/** How far a river goes on into the sea past the corner on the coast where it ends, in world units, so its water and its valley reach the
	 *  water of the sea */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverMouthLength = 40.0f;


	/** The valley the surface lowers along a river (HexPlanetSurface): its depth, the half width of its flat bottom from the line of the river,
	 *  and the width of its slopes beyond, in world units, so the river lies a little below the cells and the ground falls to its banks */
	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverValleyDepth = 4.5f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=0))
	float RiverValleyBottom = 19.0f;

	UPROPERTY(EditAnywhere, Category="Rivers", meta=(ClampMin=1))
	float RiverValleySlope = 26.0f;

	/** Colors of the water of a river: in the middle and at its edges, where it is shallow */
	UPROPERTY(EditAnywhere, Category="Rivers|Look")
	FLinearColor RiverDeepColor = FLinearColor(0.025f, 0.1f, 0.12f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Rivers|Look")
	FLinearColor RiverShallowColor = FLinearColor(0.07f, 0.24f, 0.23f, 1.0f);

	/** The bed seen through the water, as the sand of the shallows of the sea: its texture layer (of DA_HexTerrainLayers), its tint under the
	 *  water, and how much of the color of the water lies over it at the edge and in the middle */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	int32 RiverBedLayer = 1;

	UPROPERTY(EditAnywhere, Category="Rivers|Look")
	FLinearColor RiverBedTint = FLinearColor(0.5f, 0.6f, 0.54f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverWaterEdge = 0.3f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverWaterMiddle = 0.72f;

	/** Color of the wet earth of the banks, how far it reaches past the water in world units, and how much it darkens the ground beyond */
	UPROPERTY(EditAnywhere, Category="Rivers|Look")
	FLinearColor RiverBankColor = FLinearColor(0.2f, 0.16f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverBankWidth = 4.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverBankDarkening = 0.25f;

	/** How much the banks seem to rise out of the water, as a bend of the normal across them: up from the edge of the water to a crest
	 *  RiverBankWidth out, and down to the ground over RiverLeveeWidth from the edge, in world units */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverBankRelief = 2.0f;

	/** The volume of the banks, alike on both sides whatever the light: how much darker their foot at the water is and how much lighter their
	 *  crest */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverBankShade = 0.45f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverBankHighlight = 0.18f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=1))
	float RiverLeveeWidth = 14.0f;

	/** How far past its banks a river keeps the run-up of the sea off (RUNUP_CODE), fading over that, in world units, so at a mouth the foam
	 *  of the sea does not roll up the river */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverRunupGap = 20.0f;

	/** How dark the water is right under its banks, as a share of its color */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverEdgeShade = 0.65f;

	/** How much a noise moves the edge of the water and of the banks, in world units, and its size */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverEdgeNoise = 1.2f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=1))
	float RiverEdgeNoiseScale = 14.0f;

	/** Speed of the water downstream in world units a second, the size of its ripples and how much they bend the normal */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverFlowSpeed = 9.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0.1))
	float RiverRippleScale = 7.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverRippleStrength = 0.08f;

	/** How much ripples stretch along the flow */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=1))
	float RiverRippleStretch = 2.5f;

	/** Lines that drift downstream on the water, so it shows that it flows: their length along the flow and their spacing across it in world
	 *  units, how thin they are (the share of a line that is lit) and how light */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0.1))
	float RiverStreakLength = 22.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0.1))
	float RiverStreakSpacing = 3.5f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0.01, ClampMax=1))
	float RiverStreakThin = 0.18f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverStreakStrength = 0.22f;

	/** Foam of the rapids where the ground under a river is steep: from which flatness it starts and is full, and its color */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverRapidsFlatStart = 0.99f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverRapidsFlatEnd = 0.95f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look")
	FLinearColor RiverFoamColor = FLinearColor(0.75f, 0.8f, 0.8f, 1.0f);

	/** How far below the sea a river fades out at its mouth, so the water of the sea meets its bed, and how far from the coast its rapids
	 *  calm down, in world units; the sea field reaches 50 under the land, so no further */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverMouthDepth = 1.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverMouthCalm = 45.0f;

	/** The PlanetLetters of the biomes where rivers freeze: a side is frozen when either of its cells has one, and so ice, still and cracked */
	UPROPERTY(EditAnywhere, Category="Rivers|Look")
	FString RiverFrozenBiomes = TEXT("N");

	/** Color of the ice of a frozen river, how dark its cracks are, the size of their noise in world units, and its roughness */
	UPROPERTY(EditAnywhere, Category="Rivers|Look")
	FLinearColor RiverIceColor = FLinearColor(0.42f, 0.52f, 0.58f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverIceCrack = 0.35f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0.1))
	float RiverIceCrackScale = 9.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverIceRoughness = 0.35f;

	/** Roughness of the water of a river, rough enough that the sun makes no glints on it */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0, ClampMax=1))
	float RiverRoughness = 0.65f;

	/** Where a frozen river reaches the sea, its ice goes on as broken floes (AHexPlanet::UpdateFloes): how many, how far out, in world units,
	 *  and the radius of the largest */
	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	int32 RiverMouthFloes = 8;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0))
	float RiverMouthFloeReach = 80.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Look", meta=(ClampMin=0.1))
	float RiverMouthFloeSize = 17.0f;

	/** The bridge over a river (UHexPlanetRivers), a mesh whose X runs across the river and Z up, with its middle over the water; it is scaled
	 *  so its X fills BridgeLength, and drawn with M_HexPlanetBoulder in the rock layer BridgeRockLayer */
	UPROPERTY(EditAnywhere, Category="Rivers|Bridges")
	TSoftObjectPtr<UStaticMesh> BridgeMesh;

	UPROPERTY(EditAnywhere, Category="Rivers|Bridges", meta=(ClampMin=1))
	float BridgeLength = 64.0f;

	/** How far the bridge sinks into the ground, in world units, so its ends sit in the banks */
	UPROPERTY(EditAnywhere, Category="Rivers|Bridges")
	float BridgeSink = 1.0f;

	UPROPERTY(EditAnywhere, Category="Rivers|Bridges", meta=(ClampMin=0))
	int32 BridgeRockLayer = 2;

	/** Distance from the camera past which bridges are not drawn */
	UPROPERTY(EditAnywhere, Category="Rivers|Bridges", meta=(ClampMin=0))
	float BridgeCullDistance = 4500.0f;

	/** The voids of the underground in M_HexUnderground, all measured in world units below the top of the passages: the rock of a cliff darkens
	 *  from AbyssFadeStart to AbyssFadeEnd, where it keeps AbyssDarkness of its color, so a cliff falls into the dark */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssFadeStart = 4.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssFadeEnd = 32.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0, ClampMax=1))
	float AbyssDarkness = 0.025f;

	/** The mist the floor of a void draws as its light, teal grey, from AbyssMistStart below the passages to all of it at AbyssMistEnd: a noise
	 *  of AbyssScale world units, AbyssDetail of a finer octave, taken in AbyssSlices layers AbyssSliceDepth apart under the floor along the view
	 *  and laid front to back, so it moves against the floor as the camera moves and the deeper ones are darker and hidden by those above:
	 *  each slice is as thick as its noise lies above AbyssMistThreshold, sharpened by AbyssContrast, times AbyssSliceOpacity, and keeps
	 *  2 to the power of -depth / AbyssDepthHalf of its light, and what no slice covers shows AbyssBottomColor, nearly black; it drifts
	 *  AbyssSpeed units a second */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss")
	FLinearColor AbyssColor = FLinearColor(0.05f, 0.09f, 0.095f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssBrightness = 0.24f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0, ClampMax=1))
	float AbyssSliceOpacity = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=1))
	float AbyssDepthHalf = 160.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss")
	FLinearColor AbyssBottomColor = FLinearColor(0.001f, 0.0015f, 0.002f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0, ClampMax=1))
	float AbyssMistThreshold = 0.5f;

	/** Far away, past CoarseDistance - CoarseBand, the coarse mesh draws a void next to passages as a shallow cone, so it is shaded as if it fell
	 *  the whole LevelHeight + DeepDepth within AbyssCliffBand of the apothem from its sides towards passages, by AbyssFarCliff (0 off) */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0.01, ClampMax=1))
	float AbyssCliffBand = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0, ClampMax=1))
	float AbyssFarCliff = 1.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssMistStart = 8.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssMistEnd = 30.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=1))
	float AbyssScale = 150.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0, ClampMax=1))
	float AbyssDetail = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssContrast = 2.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=1, ClampMax=6))
	int32 AbyssSlices = 4;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=1))
	float AbyssSliceDepth = 110.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssSpeed = 6.0f;

	/** The slices lie AbyssSliceDepth apart down the normal, so a slanted view goes 1 / cosine of that through them, but at most 1 / AbyssViewFloor,
	 *  so at grazing angles they do not smear far along the view */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0.05, ClampMax=1))
	float AbyssViewFloor = 0.3f;

	/** The finer octave of the mist is AbyssDetailScale times as fine as AbyssScale, and fades out as a pixel grows from AbyssDetailFadeStart to
	 *  AbyssDetailFadeEnd of its cells, as from far away it only speckles */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=1))
	float AbyssDetailScale = 2.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssDetailFadeStart = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssDetailFadeEnd = 0.45f;

	/** Each deeper slice drifts AbyssSliceDrift faster than the one above, so they slide against each other */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0))
	float AbyssSliceDrift = 0.4f;

	/** How much of the color of the floor of a void gives way to the mist where it lies, so the floor does not show through it */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0, ClampMax=1))
	float AbyssFloorHide = 1.0f;

	/** The lip of a passage, a faint light along its sides towards a void, AbyssLipWidth of the radius of a cell wide and gone AbyssLipDepth down
	 *  the cliff, so a passage reads as a ledge */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss")
	FLinearColor AbyssLipColor = FLinearColor(0.06f, 0.075f, 0.075f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0.001))
	float AbyssLipWidth = 0.06f;

	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0.1))
	float AbyssLipDepth = 10.0f;

	/** How far below the passages the grid lines fade out, so they show on passages and not down the cliffs; outlines show on the voids too */
	UPROPERTY(EditAnywhere, Category="Underground|Abyss", meta=(ClampMin=0.1))
	float AbyssGridFade = 6.0f;

	/** The fog of the underground (M_HexUndergroundFog, bFog): the scene fades to FogColor with the length of the view past FogStart, by
	 *  FogDensity a unit and at most FogMax, and the space past the globe is FogSpaceColor */
	UPROPERTY(EditAnywhere, Category="Underground|Fog")
	FLinearColor FogColor = FLinearColor(0.002f, 0.003f, 0.0035f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Fog", meta=(ClampMin=0))
	float FogStart = 1500.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Fog", meta=(ClampMin=0))
	float FogDensity = 0.00012f;

	UPROPERTY(EditAnywhere, Category="Underground|Fog", meta=(ClampMin=0, ClampMax=1))
	float FogMax = 0.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Fog")
	FLinearColor FogSpaceColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	/** And the mist in the voids: below FogMistTop under the passages the air thickens over FogMistDepth to FogMistDensity a unit, at most FogMistMax,
	 *  in FogMistColor, so a view into a chasm meets more mist the deeper it goes */
	UPROPERTY(EditAnywhere, Category="Underground|Fog")
	FLinearColor FogMistColor = FLinearColor(0.014f, 0.024f, 0.028f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Fog")
	float FogMistTop = 50.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Fog", meta=(ClampMin=1))
	float FogMistDepth = 170.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Fog", meta=(ClampMin=0))
	float FogMistDensity = 0.005f;

	UPROPERTY(EditAnywhere, Category="Underground|Fog", meta=(ClampMin=0, ClampMax=1))
	float FogMistMax = 0.15f;

	/** The colors of the networks of entrances A to H, of the chips of the editor and the markers of entrances and exits; the surface's are used
	 *  for both layers */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances")
	TArray<FLinearColor> NetworkColors = {FLinearColor(1.0f, 0.55f, 0.1f), FLinearColor(0.1f, 0.7f, 1.0f), FLinearColor(0.95f, 0.25f, 0.8f), FLinearColor(0.45f, 0.95f, 0.15f),
		FLinearColor(0.6f, 0.4f, 1.0f), FLinearColor(1.0f, 0.22f, 0.18f), FLinearColor(0.15f, 0.95f, 0.75f), FLinearColor(1.0f, 0.9f, 0.25f)};

	/** The marker of an entrance on the surface, a cave mouth, and of its exit underground, a ring of rocks with a shaft of light, each drawn with
	 *  the network color as their glow (M_HexPlanetMarker, M_HexPlanetShaft; Tools/HexPlanetKit/import_planet_markers.py), scaled so their radius
	 *  is MarkerSize and the shaft ShaftHeight high; their glow is MarkerGlow and that of the shaft ShaftBrightness */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances")
	TSoftObjectPtr<UStaticMesh> EntranceMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Markers/SM_PlanetCaveMouth.SM_PlanetCaveMouth")));

	UPROPERTY(EditAnywhere, Category="Underground|Entrances")
	TSoftObjectPtr<UStaticMesh> ExitMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Markers/SM_PlanetExitRing.SM_PlanetExitRing")));

	UPROPERTY(EditAnywhere, Category="Underground|Entrances")
	TSoftObjectPtr<UStaticMesh> ShaftMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Markers/SM_PlanetExitShaft.SM_PlanetExitShaft")));

	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=1))
	float MarkerSize = 55.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=1))
	float ShaftHeight = 150.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0))
	float MarkerGlow = 4.0f;

	/** The glow breathes by MarkerPulse of it, MarkerPulseSpeed radians a second, each marker at its own phase */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0, ClampMax=1))
	float MarkerPulse = 0.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0))
	float MarkerPulseSpeed = 1.7f;

	/** How much of the color of the rock a marker loses where it glows, so the glow is not washed out by the rock under it */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0, ClampMax=1))
	float MarkerRockDarkening = 0.85f;

	/** Light the rock of a marker gives by itself besides Ambient, times the color of its network, as if lit by its glow, so the stones of an
	 *  exit ring do not stand black in the dim light of the underground */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0))
	float MarkerRockLight = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0))
	float ShaftBrightness = 0.5f;

	/** The shaft flickers by ShaftFlicker of its light, ShaftFlickerSpeed radians a second, the phase moving ShaftFlickerRise radians up its height,
	 *  so the flicker runs up it */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0, ClampMax=1))
	float ShaftFlicker = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0))
	float ShaftFlickerSpeed = 2.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Entrances")
	float ShaftFlickerRise = 5.0f;

	/** How sharply the shaft fades towards its edges, where its surface turns away from the view: the facing to this power */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0.1))
	float ShaftFacingPower = 1.5f;

	/** Distances from the camera over which the shaft fades out towards it, gone within ShaftNearFadeStart, so it does not fill the screen when the
	 *  camera is close */
	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0))
	float ShaftNearFadeStart = 200.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Entrances", meta=(ClampMin=0))
	float ShaftNearFade = 400.0f;

	/** The color of what is wrong in the editor of the underground: the markers of entrances that cannot be used, of the surface's config on both
	 *  layers, and in the Links tab of the palette an exit in the void, the pieces of a network split in two but its largest, passages where
	 *  networks join and passages one cell wide (M_HexUnderground, by the blue of CellMarks, AHexPlanet::UpdateLinkMarks) */
	UPROPERTY(EditAnywhere, Category="Underground|Editor")
	FLinearColor ProblemColor = FLinearColor(1.0f, 0.06f, 0.04f, 1.0f);

	/** The Links tab tints each passage by the color of its network (NetworkColors of the surface): LinksTint of its color, and it glows by
	 *  LinksGlow of it, so it shows in the dim light; a passage no exit reaches takes LinksOrphanColor */
	UPROPERTY(EditAnywhere, Category="Underground|Editor")
	FLinearColor LinksOrphanColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Editor", meta=(ClampMin=0, ClampMax=1))
	float LinksTint = 0.45f;

	UPROPERTY(EditAnywhere, Category="Underground|Editor", meta=(ClampMin=0))
	float LinksGlow = 0.2f;

	/** The cave objects of M_HexPlanetCave (stalagmites, mushrooms, crystals, bones, rubble, rune stones): the color of their vertices, lighter or
	 *  darker by CaveObjectTint by instance, broken by a value noise of the world position CaveObjectNoiseScale across, which multiplies it by
	 *  CaveObjectNoiseMin to CaveObjectNoiseMin + CaveObjectNoiseSpan, CaveObjectNoise of the way; where a vertex glows (its alpha) its color
	 *  darkens by CaveObjectGlowDarkening, so the glow of its set (UHexPlanetObjectSet::GlowColor) is not washed out */
	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0, ClampMax=1))
	float CaveObjectTint = 0.18f;

	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0, ClampMax=1))
	float CaveObjectNoise = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0.1))
	float CaveObjectNoiseScale = 12.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0))
	float CaveObjectNoiseMin = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0))
	float CaveObjectNoiseSpan = 0.8f;

	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0, ClampMax=1))
	float CaveObjectGlowDarkening = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0, ClampMax=1))
	float CaveObjectRoughness = 0.7f;

	/** World units kept between a mesh and a side it must not cross (UHexPlanetObjectSet::bClipFootprint), besides its own radius */
	UPROPERTY(EditAnywhere, Category="Underground|Objects", meta=(ClampMin=0))
	float ObjectFootprintMargin = 2.0f;

	/** The walls on the lips of the passages (HexPlanetWalls, AHexPlanet::UpdateWalls), one mesh per chunk. Their line follows the sides of the
	 *  passages towards a void, keeping the tips of the hexes, cuts across each corner of a void between two passages by WallCornerCut of its
	 *  sides, as if the floor went on into that void (0 follows the sides, 1 runs from tip to tip), so the walls still show the hexes they stand on,
	 *  rounds every corner by WallCornerRound and is sampled every WallStep world units. The wall stands on the void side of the line, WallHeight
	 *  high, about a mountain of the surface, higher or lower by WallHeightNoise in stretches of WallHeightNoiseScale, and WallThickness thick at
	 *  the floor: its inner face rises from a rounded foot of WallFillet that reaches WallFootReach onto the floor, and from a flat floor where the
	 *  line cut a corner, leans back by WallLean of its height and bulges out by WallOverhang at its top; from that overhang hang stalactites, ribs
	 *  of a cellular pattern of the place about WallRibSize across that stand out up to WallRibDepth and end between WallRibTipMin and WallRibTipMax
	 *  of the height, WallColumnShare of them reaching the floor as columns, with flutes WallFluteDepth deep scooped down the face between them
	 *  (WallFluteWidth wide, a share of a rib), as with a spoon; its crest rises and falls by WallCrestJag over each stalactite and dips by
	 *  WallCrestNotch between them, and its outer face goes down WallOuterDepth into the chasm, flared by WallOuterFlare, from the line smoothed over
	 *  WallOuterSmooth each way, so a thick wall does not pinch at a corner. A 3D noise of the place bulges the faces (WallBulge at WallBulgeScale,
	 *  WallDetail at WallDetailScale) and lifts the crest (WallCrestNoise), so no stretch repeats from any distance. Not drawn past
	 *  WallCullDistance; the rims of EHexRimSeat::WallFoot and WallFace stand on it */
	UPROPERTY(EditAnywhere, Category="Features")
	bool bWalls = false;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float WallCornerCut = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallCornerRound = 30.0f;

	/** Where a wall runs straight over a row of hexes its protrusions, the cut corners of the voids, and its hollows, the tips of the hexes between
	 *  them, alternate every cell, so they are made to differ: the cut of each corner differs from WallCornerCut by up to WallCornerCutVariation
	 *  either way, by a hash of the corner, never past half a side unless WallCornerCut is, and WallBridgeShare of the hollows carry a canopy, the
	 *  top of the wall reaching from WallBridgeFillMin to all of the way across the hollow towards the line between the protrusions on both sides,
	 *  by a hash of the tip, its underside going back to the face from under the overhang down to WallBridgeUnder of the height, while the foot
	 *  stays on the sides of the hex; so seen from above the top of a wall runs on over two or three cells in places */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.5))
	float WallCornerCutVariation = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float WallBridgeShare = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float WallBridgeFillMin = 0.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.3, ClampMax=0.78))
	float WallBridgeUnder = 0.65f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallOuterSmooth = 90.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float WallHeight = 170.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.9))
	float WallHeightNoise = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float WallHeightNoiseScale = 320.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float WallThickness = 75.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float WallStep = 6.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallFootReach = 14.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallFillet = 45.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.5))
	float WallLean = 0.05f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallOverhang = 12.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallOuterDepth = 60.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallOuterFlare = 25.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallBulge = 6.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float WallBulgeScale = 55.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallDetail = 2.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float WallDetailScale = 15.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallCrestNoise = 0.1f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.5))
	float WallCrestJag = 0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.5))
	float WallCrestNotch = 0.08f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=4))
	float WallRibSize = 50.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallRibDepth = 12.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.85))
	float WallRibTipMin = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.85))
	float WallRibTipMax = 0.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float WallColumnShare = 0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallFluteDepth = 7.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.01, ClampMax=1))
	float WallFluteWidth = 0.3f;

	/** The styles of the walls (FHexWallStyle), each holding in regions of a noise of the place about WallStyleScale across, blending where they
	 *  meet over less the higher WallStyleSharpness, so the walls do not look alike all along a passage */
	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	TArray<FHexWallStyle> WallStyles = MakeDefaultWallStyles();

	/** The height of the walls beside a void, a factor on WallHeight: set by hand for the void (AHexPlanet::SetWallHeights, underground/walls.json),
	 *  else given by its style (EHexVoidStyle), WallPoolHeight beside lava and lakes, so the pool shows over a low parapet from above, and 1 beside
	 *  the mist. A lower wall is thinner, with less of an overhang and smaller stalactites, down to a parapet of WallPoolThickness of its thickness
	 *  without an overhang at WallPoolHeight; below that it narrows towards its line, into a low curb at WallHeightMin, below which no wall goes,
	 *  so a passage never opens onto the abyss. The heights ease from one void to the next over WallHeightBlend world units each way along the
	 *  wall. A click of the brush of walls raises or lowers the height of a void by WallHeightStep, from WallHeightMin to WallHeightMax */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.1, ClampMax=1))
	float WallPoolHeight = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.1, ClampMax=1))
	float WallPoolThickness = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallHeightBlend = 60.0f;

	/** The walls along WallHeightJitterShare of the voids, by a hash of the void, stand higher or lower by up to WallHeightJitter of their height,
	 *  and the rest as they are, so the walls of neighboring voids differ a little; eased from one void to the next over WallHeightBlend as the
	 *  heights are, and never making a wall a parapet */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.5))
	float WallHeightJitter = 0.14f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float WallHeightJitterShare = 0.65f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.01, ClampMax=1))
	float WallHeightStep = 0.1f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.05, ClampMax=1))
	float WallHeightMin = 0.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1, ClampMax=4))
	float WallHeightMax = 2.0f;

	/** Returns a height of the walls along a void as the planet keeps it: within WallHeightMin and WallHeightMax, in thousandths, as walls.json
	 *  writes it */
	float ClampWallHeight(float Height) const
	{
		const float Low = FMath::Clamp(WallHeightMin, 0.05f, 1.0f);
		return FMath::RoundToFloat(FMath::Clamp(Height, Low, FMath::Max(WallHeightMax, 1.0f)) * 1000.0f) / 1000.0f;
	}

	/** The lip of the walls in runs of a few cells along them, by a noise of the place about WallLipScale across (from -0.5 to 0.5 mostly):
	 *  below WallLipPlain the top runs plain, its stalactites, flutes and overhang nearly gone, above WallLipBold it stands out boldly, its
	 *  stalactites WallLipBoldDepth times as deep and jutting out WallLipBoldOverhang as a ledge even on a parapet, and between them it is broken,
	 *  WallLipBrokenShare of its stalactites gone at random; each change eases over WallLipBlend of the noise. The width of the stalactites and
	 *  the thickness of the wall change by up to WallRibWidthVariation and WallThicknessVariation each way in runs of their own, so a low wall
	 *  seen from above does not wave alike all along a passage */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=10))
	float WallLipScale = 420.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=-1, ClampMax=1))
	float WallLipPlain = -0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=-1, ClampMax=1))
	float WallLipBold = 0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.001, ClampMax=0.5))
	float WallLipBlend = 0.08f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallLipBoldDepth = 1.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallLipBoldOverhang = 8.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float WallLipBrokenShare = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.7))
	float WallRibWidthVariation = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=0.7))
	float WallThicknessVariation = 0.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=10))
	float WallStyleScale = 450.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=20))
	float WallStyleSharpness = 5.0f;

	/** Beyond WallCullDistance (0 for never) no wall is drawn; further than WallLodDistance a chunk draws its coarse walls (every third sample
	 *  and 14 of the rows, about a fifth of the triangles), dissolving into the whole ones over WallLodBand each way; the whole walls of a chunk are
	 *  built in the background once it comes within WallStreamAhead past that band and freed past WallStreamKeep further, so only those near the
	 *  camera take their memory */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallCullDistance = 20000.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallLodDistance = 2200.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float WallLodBand = 300.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallStreamAhead = 800.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float WallStreamKeep = 1500.0f;

	/** The look of the walls (M_HexUndergroundWall): the rock layer of the cliff below them (the rock of the abyss) with the values of that layer
	 *  (LayerParams), times CaveWallColor, and lighter and warmer on the stalactites by CaveWallRibTint, as dripstone; streaked down the faces by
	 *  CaveWallStreaks in streaks about CaveWallStreakScale wide, lighter or darker by CaveWallTone in patches of CaveWallToneScale, darker by
	 *  CaveWallCavityShade in the flutes, under the overhang and between the stalactites, and from CaveWallTopStart of the height up to the crest by
	 *  CaveWallTopShade, as the light of a cave comes from its floor, and down the outer face below the floor as the cliffs below it are
	 *  (AbyssFadeStart, AbyssFadeEnd, AbyssDarkness), so its lower edge meets them without a seam where a lake or lava lets one see it;
	 *  its normal bent by CaveWallNormalStrength of the layer, CaveWallRoughness rough and CaveWallRibRoughness on the stalactites. The rounded foot
	 *  and the floor before it take the floor of the passages (the layer of their biome, CaveFloorTint, CaveFloorBrightness and CaveFloorPools), the
	 *  edge between them moved by CaveWallFloorNoise, so the wall rises out of the floor. Lighter and warmer than 1, as a wall is steep, so it draws
	 *  the cliff of the basalt alone, a dark photo scan, where the cliffs mix in its lighter scree (tune it again with the light of the game) */
	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	FLinearColor CaveWallColor = FLinearColor(2.6f, 2.3f, 2.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	FLinearColor CaveWallRibTint = FLinearColor(1.3f, 1.2f, 1.05f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallStreaks = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.5))
	float CaveWallStreakScale = 9.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallTone = 0.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float CaveWallToneScale = 260.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallCavityShade = 0.55f;

	/** The stone of the walls splits along its joints into slabs, cells of a pattern of the place CaveWallFacetCell across and CaveWallFacetStretch
	 *  times as tall, each leaning its normal its own way by CaveWallFacetTilt, with cracks between them in patches, CaveWallCrackWidth of a slab
	 *  wide and darkened by CaveWallCrackShade, fading out where a slab covers only a few pixels; and it lies in layers CaveWallStrataSpacing
	 *  apart, each lighter or darker by CaveWallStrata, but on the stalactites */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float CaveWallFacetCell = 18.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.2, ClampMax=10))
	float CaveWallFacetStretch = 3.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=2))
	float CaveWallFacetTilt = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallCrackShade = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.01, ClampMax=1))
	float CaveWallCrackWidth = 0.05f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallStrata = 0.18f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float CaveWallStrataSpacing = 24.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallTopShade = 0.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallTopStart = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float CaveWallNormalStrength = 1.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallRoughness = 0.85f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallRibRoughness = 0.55f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=2))
	float CaveWallFloorNoise = 0.6f;

	/** The stone of the walls varies by the place: CaveWallAltShare of it, in regions about CaveWallAltScale across, is the rock layer named
	 *  CaveWallAltLayerName (DA_HexTerrainLayers) instead of the rock of the abyss; where a style is flowstone it is lighter and warmer by
	 *  CaveWallFlowTint and CaveWallFlowRoughness rough; where it is broken rock thin veins CaveWallVeinWidth wide of CaveWallVeinColor run through
	 *  it in a pattern about CaveWallVeinScale across, by CaveWallVeins; and rusty stains of CaveWallStainColor run down it by CaveWallStains */
	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	FName CaveWallAltLayerName = TEXT("OchreRock");

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallAltShare = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=10))
	float CaveWallAltScale = 380.0f;

	/** The tint of the other rock, in place of CaveWallColor, as a rock lighter than the basalt needs less */
	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	FLinearColor CaveWallAltColor = FLinearColor(1.0f, 0.92f, 0.82f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	FLinearColor CaveWallFlowTint = FLinearColor(1.35f, 1.22f, 1.05f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallFlowRoughness = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallVeins = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.001, ClampMax=0.5))
	float CaveWallVeinWidth = 0.02f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=1))
	float CaveWallVeinScale = 45.0f;

	/** Lighter where the rock around is lighter, as a pale vein in dark basalt drew a white line */
	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	FLinearColor CaveWallVeinColor = FLinearColor(0.6f, 0.55f, 0.48f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallStains = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls")
	FLinearColor CaveWallStainColor = FLinearColor(0.8f, 0.42f, 0.2f, 1.0f);

	/** The glowing moss of the rims grows on the foot of the walls instead of the floor beside them (which it would poke out from under), up to
	 *  CaveWallMossTop of their height on the inner face alone, in the patches, speckles and colors of CaveMoss*, by CaveWallMoss; and lava beyond
	 *  a wall lights its rock in LavaLightColor: the low parapet by CaveWallLavaGlow, fading up it and less on its inner face, the outer face as
	 *  the cliffs of the void (LavaCliff*), and the floor at its foot as the passage there (GlowPoolEdge of LavaPool) */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallMoss = 1.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.01, ClampMax=1))
	float CaveWallMossTop = 0.22f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float CaveWallLavaGlow = 0.8f;

	/** The walls dissolve around the line from the camera to the point under the cursor, as in a game seen from above, where they stand in front
	 *  of that point: within CaveWallFadeRadius world units of the line, softer over CaveWallFadeSoftness of it at its edge, fully from
	 *  CaveWallFadeDepth units nearer the camera than the point, keeping CaveWallFadeKeep of their pixels, which the temporal filter blends into a
	 *  ghost; their shadows and what ray tracing sees stay whole. hex.PlanetWallFade 0 turns it off */
	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0))
	float CaveWallFadeRadius = 150.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.01, ClampMax=1))
	float CaveWallFadeSoftness = 0.4f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0.1))
	float CaveWallFadeDepth = 15.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Walls", meta=(ClampMin=0, ClampMax=1))
	float CaveWallFadeKeep = 0.15f;

	/** The floor of the passages in M_HexUnderground, on their tops (within CaveFloorTopDepth below them): its layer (OchreScree, earth with
	 *  pebbles) times CaveFloorTint and CaveFloorBrightness, warm, as the placeholder light of the underground is cool (tune both again once the
	 *  light of the game is decided, UndergroundLighting of DA_HexPlanetCamera), and lighter or darker by CaveFloorPools in pools of tone of a noise
	 *  CaveFloorPoolScale across */
	UPROPERTY(EditAnywhere, Category="Underground|Floor")
	FLinearColor CaveFloorTint = FLinearColor(1.25f, 1.0f, 0.78f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0))
	float CaveFloorBrightness = 1.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveFloorPools = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=1))
	float CaveFloorPoolScale = 180.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0.01))
	float CaveFloorTopDepth = 4.0f;

	/** The floor of the passages rises by up to FloorNoise world units, 0 for a flat floor (the surface): a noise of the place FloorNoiseScale
	 *  across with FloorNoiseDetail of one finer, which the cells that share a point see alike, so the unevenness runs on across their sides
	 *  (HexPlanetKit::GetFloorRise). It only rises, so the floor stays on the top of its level (CaveFloorTopDepth), and fades out over
	 *  FloorNoiseFade below that top, so the cliffs of the voids keep their feet. The walls rise with it (HexPlanetWalls::FParams), and the cells
	 *  of the passages are built of a lattice as fine as the slopes of the kit, as its flat piece has too few points and would show a bump in the
	 *  middle of each cell */
	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0))
	float FloorNoise = 0.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=10))
	float FloorNoiseScale = 95.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float FloorNoiseDetail = 0.8f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=1))
	float FloorNoiseFade = 20.0f;

	/** How much darker the hollows of the floor that rises (FloorNoise) are and how much lighter its swells, in M_HexUnderground and on the foot of
	 *  the walls, so its unevenness shows under the even light of a cave */
	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveFloorRiseShade = 0.15f;

	/** Lumps of a floor dug and trodden unevenly, too small for the lattice of its cells, which M_HexUnderground draws as a normal bent by a noise
	 *  CaveFloorBumpScale across, CaveFloorBumps strong, fading out as the moss does where a pixel grows past them */
	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=4))
	float CaveFloorBumps = 1.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=1))
	float CaveFloorBumpScale = 18.0f;

	/** The floor of the passages is not one ground nor one tone (CAVE_FLOOR_CODE of create_planet_assets.py, which the foot of the walls takes
	 *  too, so they meet without a seam): CaveFloorAltShare of it, in patches of a noise about CaveFloorAltScale across, is the terrain layer named
	 *  CaveFloorAltLayerName (DA_HexTerrainLayers, none when not found) times CaveFloorAltColor and CaveFloorBrightness, its edge eased over
	 *  CaveFloorAltEdge of the noise and made ragged by CaveFloorAltRagged of the heights of both layers, the higher covering the lower; and the
	 *  floor turns warmer or cooler, towards CaveFloorWarmColor and CaveFloorCoolColor, by CaveFloorHue in regions about CaveFloorHueScale across */
	UPROPERTY(EditAnywhere, Category="Underground|Floor")
	FName CaveFloorAltLayerName = TEXT("RedScree");

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveFloorAltShare = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=1))
	float CaveFloorAltScale = 240.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0.001, ClampMax=0.5))
	float CaveFloorAltEdge = 0.08f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=4))
	float CaveFloorAltRagged = 1.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor")
	FLinearColor CaveFloorAltColor = FLinearColor(1.6f, 1.75f, 1.75f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveFloorHue = 0.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=1))
	float CaveFloorHueScale = 520.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor")
	FLinearColor CaveFloorWarmColor = FLinearColor(1.08f, 0.97f, 0.84f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Floor")
	FLinearColor CaveFloorCoolColor = FLinearColor(0.88f, 0.92f, 0.98f, 1.0f);

	/** Old flagstones on the floor of the passages, here and there (CAVE_FLOOR_CODE, on the foot of the walls too): a cellular pattern of the place
	 *  about CaveSlabSize across, its points moved off the middles of their cells by up to CaveSlabJitter, over CaveSlabShare of the floor in
	 *  patches of a noise about CaveSlabPatchScale across, which sand buries towards their edge over CaveSlabPatchEdge of the noise. Each slab is
	 *  the terrain layer named CaveSlabLayerName (none when not found) at a place of its own, times CaveSlabColor and CaveFloorBrightness, lighter
	 *  or darker by up to CaveSlabTone and dusted by about CaveSlabDust of the dirt of the floor, more where the sand drifts thicken, with
	 *  CaveSlabNormal of the normal of the layer and leaning its own way by CaveSlabTilt, its edges worn round
	 *  over CaveSlabBevel world units (CaveSlabBevelTilt) and joints CaveSlabJointWidth wide of the dirt of the floor, darker by CaveSlabJointShade;
	 *  CaveSlabMissing of them are gone and CaveSlabCracked cracked across, the cracks CaveSlabCrackWidth wide and darker by CaveSlabCrackShade.
	 *  Sand drifts over them in a noise about CaveSlabSandScale across, over CaveSlabSand of them, more in the hollows of the floor
	 *  (CaveSlabSandHollow), the high parts of the stone showing through by CaveSlabSandRagged, its edge CaveSlabSandSoftness soft. Joints, cracks
	 *  and bevels fade out where a pixel grows past them */
	UPROPERTY(EditAnywhere, Category="Underground|Floor")
	FName CaveSlabLayerName = TEXT("GrayRock");

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabShare = 0.16f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=1))
	float CaveSlabPatchScale = 360.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0.001, ClampMax=0.5))
	float CaveSlabPatchEdge = 0.08f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=4))
	float CaveSlabSize = 34.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabJitter = 0.75f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor")
	FLinearColor CaveSlabColor = FLinearColor(1.1f, 0.95f, 0.75f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=0.5))
	float CaveSlabTone = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabDust = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=2))
	float CaveSlabNormal = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabTilt = 0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0.1))
	float CaveSlabBevel = 3.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=2))
	float CaveSlabBevelTilt = 0.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0))
	float CaveSlabJointWidth = 1.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabJointShade = 0.45f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabMissing = 0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabCracked = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0))
	float CaveSlabCrackWidth = 0.9f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabCrackShade = 0.55f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabSand = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=1))
	float CaveSlabSandScale = 90.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=1))
	float CaveSlabSandHollow = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0, ClampMax=2))
	float CaveSlabSandRagged = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Floor", meta=(ClampMin=0.001, ClampMax=1))
	float CaveSlabSandSoftness = 0.12f;

	/** Glowing moss: on the floor within CaveMossReach of the cell radius from a side towards a void, and down the top CaveMossWallDepth of the
	 *  cliffs of a void, in patches where a noise CaveMossPatchScale across lies above CaveMossCover, speckled where a finer one
	 *  CaveMossSpeckleScale across lies above CaveMossSpeckle; the speckles fade as a pixel grows from CaveMossDetailFadeStart to
	 *  CaveMossDetailFadeEnd of their size to CaveMossFarGlow of the glow, so they never shimmer far away. The ground there takes
	 *  CaveMossAlbedoShare of CaveMossAlbedo and glows by CaveMossColor times CaveMossGlow, an accent, not a light */
	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0.01, ClampMax=1))
	float CaveMossReach = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=1))
	float CaveMossWallDepth = 20.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=1))
	float CaveMossPatchScale = 90.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0, ClampMax=1))
	float CaveMossCover = 0.55f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0.5))
	float CaveMossSpeckleScale = 8.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0, ClampMax=1))
	float CaveMossSpeckle = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0))
	float CaveMossDetailFadeStart = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0))
	float CaveMossDetailFadeEnd = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0, ClampMax=1))
	float CaveMossFarGlow = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss")
	FLinearColor CaveMossAlbedo = FLinearColor(0.05f, 0.12f, 0.04f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0, ClampMax=1))
	float CaveMossAlbedoShare = 0.7f;

	UPROPERTY(EditAnywhere, Category="Underground|Moss")
	FLinearColor CaveMossColor = FLinearColor(0.2f, 0.9f, 0.35f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0))
	float CaveMossGlow = 0.06f;

	/** How soft the edges of the patches and speckles are, as the share of the way from their threshold (CaveMossCover, CaveMossSpeckle) to 1 */
	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0.01, ClampMax=1))
	float CaveMossSoftness = 0.5f;

	/** Share of CaveMossReach and of CaveMossWallDepth over which the moss fades out at their ends */
	UPROPERTY(EditAnywhere, Category="Underground|Moss", meta=(ClampMin=0.01, ClampMax=1))
	float CaveMossFade = 0.5f;

	/** Pools of light under the kinds that glow (UHexPlanetObjectSet::GlowPool) and beside lava (LavaPool), from CellCanopy, which the underground
	 *  fills with the glow of each cell (AHexPlanet::UpdateCanopy): the floor of such a cell is lit by its glow color times GlowPoolStrength,
	 *  all of it within GlowPoolInner of the cell radius from its middle and GlowPoolEdge of it at its sides, the line moved by a noise
	 *  (GlowPoolNoise, GlowPoolNoiseScale), and a cell beside it takes GlowPoolEdge of it, fading out GlowPoolSpill of the radius into it; the
	 *  light fades out GlowPoolDepth down the cliffs. A glowing cell falls off only towards the sides of neighbors that do not glow, and its
	 *  strength and color are GlowPoolBlend of the way to the mean of it and its glowing neighbors, so a patch is one pool and no hex shows; the
	 *  pool lights the floor by its albedo and GlowPoolLight of it whatever the albedo, so a tan floor turns the color of the glow */
	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0))
	float GlowPoolStrength = 0.8f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0, ClampMax=1))
	float GlowPoolEdge = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0, ClampMax=1))
	float GlowPoolInner = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0))
	float GlowPoolNoise = 0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=1))
	float GlowPoolNoiseScale = 70.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0.01, ClampMax=1))
	float GlowPoolSpill = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=1))
	float GlowPoolDepth = 60.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0, ClampMax=1))
	float GlowPoolBlend = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0))
	float GlowPoolLight = 0.2f;

	/** How strongly a lava void lights the passages beside it, its glow in CellCanopy, in LavaLightColor at its brightest channel 1 */
	UPROPERTY(EditAnywhere, Category="Underground|Glow pools", meta=(ClampMin=0, ClampMax=1))
	float LavaPool = 1.0f;

	/** Small still pools of water on the passages, drawn by the material: where a noise CavePuddleScale across lies above CavePuddleCover, raised
	 *  in the hollows of a floor that rises (FloorNoise) and lowered on its swells by up to CavePuddleHollow of the noise, so they gather in the
	 *  hollows, and further than CavePuddleRimGap of the cell radius from a side towards a void or towards a cell with objects or of an exit, in
	 *  which none lies, so a side never cuts one; a rim CavePuddleRim of the noise wide around them is wet earth, darker by
	 *  CavePuddleWetDarkening, and inside the water shows that earth at its shore and deepens to CavePuddleColor CavePuddleDepth of the noise in,
	 *  with CavePuddleRoughness, CavePuddleSpecular, the up of the cell and a faint sheen CavePuddleSheen that does not depend on the view, so it
	 *  reads as shallow dark water and not as a hole; its shore is at least CavePuddleEdge of the noise soft. No grid line and no pool of light
	 *  lie on it. They fade as the moss does */
	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=1))
	float CavePuddleScale = 70.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0, ClampMax=1))
	float CavePuddleHollow = 0.2f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0, ClampMax=1))
	float CavePuddleCover = 0.9f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0, ClampMax=1))
	float CavePuddleRimGap = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0.001))
	float CavePuddleRim = 0.04f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0, ClampMax=1))
	float CavePuddleWetDarkening = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles")
	FLinearColor CavePuddleColor = FLinearColor(0.006f, 0.014f, 0.03f, 1.0f);

	/** Rougher than Lumen traces reflections for (0.4), so a pool takes the cheap reflection of the sky and reads as dark water, not chrome */
	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0, ClampMax=1))
	float CavePuddleRoughness = 0.45f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0, ClampMax=1))
	float CavePuddleSpecular = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0.001))
	float CavePuddleDepth = 0.035f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles", meta=(ClampMin=0))
	float CavePuddleEdge = 0.01f;

	UPROPERTY(EditAnywhere, Category="Underground|Puddles")
	FLinearColor CavePuddleSheen = FLinearColor(0.004f, 0.009f, 0.018f, 1.0f);

	/** Runes carved into the cliffs of the voids: a side of a void towards a passage whose hash is below CaveRuneShare holds a panel from
	 *  CaveRuneTop to CaveRuneTop + CaveRuneSize below the passages, CaveRuneSize wide, with a hexagon and two or three strokes of one of eight
	 *  runes CaveRuneStroke wide, darker by CaveRuneDarkening, with a lighter lip CaveRuneBevel on their upper edge and a faint pale glow
	 *  CaveRuneColor times CaveRuneGlow; they fade out as a pixel grows from CaveRuneFadeStart to CaveRuneFadeEnd world units */
	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0, ClampMax=1))
	float CaveRuneShare = 0.04f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0))
	float CaveRuneTop = 4.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=1))
	float CaveRuneSize = 24.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0.1))
	float CaveRuneStroke = 4.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0, ClampMax=1))
	float CaveRuneDarkening = 0.45f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0))
	float CaveRuneBevel = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes")
	FLinearColor CaveRuneColor = FLinearColor(0.8f, 0.85f, 0.9f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0))
	float CaveRuneGlow = 0.05f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0))
	float CaveRuneFadeStart = 2.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Runes", meta=(ClampMin=0))
	float CaveRuneFadeEnd = 4.0f;

	/** A void of the lava style (EHexVoidStyle): its floor is LavaCrustColor, glowing in LavaColor times LavaBrightness in the cracks of the crust,
	 *  the low parts of a noise LavaCrustScale across below LavaCrustShare that flows LavaFlowSpeed units a second, with a finer octave of
	 *  LavaCrustDetail, LavaCrustDetailScale times finer and drifting LavaCrustDetailDrift against it, fading out over the last LavaCrackEdge of a
	 *  crack towards the crust, brighter by LavaCoreBrightness and turning to LavaCoreColor towards its middle (the lowest LavaCoreShare of it), so
	 *  it runs from red at the crust to yellow far from it, breathing by LavaPulse at LavaPulseSpeed; as a pixel grows from
	 *  LavaDetailFadeStart to LavaDetailFadeEnd of LavaCrustScale the cracks fade to LavaFarGlow of their light, so far away it glows as a pool and
	 *  does not speckle. Its cliffs glow up to LavaCliffReach above its floor in LavaLightColor, by their rock times LavaCliffLight and by
	 *  LavaCliffGlow whatever the rock, keeping LavaCliffDaylight of the light of the sky and the sun there and taking LavaCliffTint of that color
	 *  into their rock, so they read lit by the lava and not pink, and the passages beside it take a pool of that color (LavaPool). Where it meets
	 *  a void of another style
	 *  both fade to plain floor over VoidStyleEdge of the cell radius from a little inside the side, the line moved by up to twice
	 *  VoidStyleEdgeNoise of a noise VoidStyleEdgeNoiseScale across, so neither ends on the straight side; VoidStyleEdge plus twice
	 *  VoidStyleEdgeNoise stays within the apothem (0.866) */
	UPROPERTY(EditAnywhere, Category="Underground|Lava")
	FLinearColor LavaColor = FLinearColor(1.0f, 0.12f, 0.015f, 1.0f);

	/** Low enough that the red of the cracks does not clip, so their cores show brighter and yellower */
	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaBrightness = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava")
	FLinearColor LavaCoreColor = FLinearColor(1.0f, 0.5f, 0.12f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0.01, ClampMax=1))
	float LavaCrackEdge = 0.25f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0, ClampMax=1))
	float LavaCliffDaylight = 0.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0, ClampMax=1))
	float LavaCliffTint = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava")
	FLinearColor LavaLightColor = FLinearColor(1.0f, 0.36f, 0.08f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0, ClampMax=1))
	float LavaCrustDetail = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=1))
	float LavaCrustDetailScale = 2.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaCrustDetailDrift = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaDetailFadeStart = 0.12f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaDetailFadeEnd = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0, ClampMax=2))
	float LavaFarGlow = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0.01, ClampMax=1))
	float LavaCoreShare = 0.6f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaCoreBrightness = 1.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava")
	FLinearColor LavaCrustColor = FLinearColor(0.02f, 0.015f, 0.012f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=1))
	float LavaCrustScale = 32.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0.01, ClampMax=1))
	float LavaCrustShare = 0.55f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaFlowSpeed = 1.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0, ClampMax=1))
	float LavaPulse = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaPulseSpeed = 0.8f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaCliffLight = 0.8f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0))
	float LavaCliffGlow = 0.04f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=1))
	float LavaCliffReach = 40.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0.01, ClampMax=0.85))
	float VoidStyleEdge = 0.35f;

	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=0, ClampMax=0.4))
	float VoidStyleEdgeNoise = 0.2f;

	/** Size of the noise that moves the line where two styles of voids meet, so a pool of lava or a lake does not follow the sides of its cells */
	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=1))
	float VoidStyleEdgeNoiseScale = 55.0f;

	/** Height above the floor of a void up to which its lava or lake lies, fading out over it, so the cliffs above show their rock, lit by the lava */
	UPROPERTY(EditAnywhere, Category="Underground|Lava", meta=(ClampMin=1))
	float VoidFloorHeight = 12.0f;

	/** A void of the lake style: its floor is LakeColor, deep and dark, lighter towards LakeShoreColor within LakeShoreReach of the cell radius of
	 *  its shore (the sides towards a passage or a void of another style), LakeRoughness, with a sheen LakeSheenColor, LakeSheenBase of it from any
	 *  view and the rest as the view grazes it (to the power LakeSheenPower), moved by a slow ripple (LakeRipple, LakeRippleScale, LakeRippleSpeed);
	 *  it keeps LakeMistKeep of the mist. Dark, so a lake sits a little above the void around it and reads as deep water */
	UPROPERTY(EditAnywhere, Category="Underground|Lake")
	FLinearColor LakeColor = FLinearColor(0.0015f, 0.0035f, 0.008f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Lake")
	FLinearColor LakeShoreColor = FLinearColor(0.003f, 0.007f, 0.014f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0.01, ClampMax=1))
	float LakeShoreReach = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Lake")
	FLinearColor LakeSheenColor = FLinearColor(0.002f, 0.005f, 0.011f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0, ClampMax=1))
	float LakeSheenBase = 0.4f;

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0.1))
	float LakeSheenPower = 1.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0))
	float LakeRipple = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=1))
	float LakeRippleScale = 40.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0))
	float LakeRippleSpeed = 0.3f;

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0, ClampMax=1))
	float LakeRoughness = 0.45f;

	/** Specular of the water of a lake, as the floor of the other voids reflects nothing (0.5 is that of the engine for the rest) */
	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0, ClampMax=1))
	float LakeSpecular = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Lake", meta=(ClampMin=0, ClampMax=1))
	float LakeMistKeep = 0.05f;

	/** Motes (bMotes, UHexPlanetMotes): MoteCount quads of MoteMesh with MoteMaterial (M_HexPlanetMote) in a box MoteBox wide around the point the
	 *  camera looks at, from MoteHeightBelow below the tops of the passages to MoteHeightAbove above them, fixed in the world as the box follows
	 *  the camera and drifting MoteDrift units a second, wobbling MoteWobble at MoteWobbleSpeed; each is MoteSize wide and never under
	 *  MoteMinPixels, MoteColor times MoteBrightness, twinkling by MoteTwinkle, faded within MoteEdgeFade of the box, deeper in a chasm by
	 *  AbyssDepthHalf, within MoteNearFade of the camera, and gone as the camera is from MoteFadeStart to MoteFadeEnd from the point it looks at */
	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0, ClampMax=4000))
	int32 MoteCount = 400;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=100))
	float MoteBox = 2400.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteHeightBelow = 35.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteHeightAbove = 90.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0.1))
	float MoteSize = 2.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteMinPixels = 1.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes")
	FLinearColor MoteColor = FLinearColor(0.5f, 0.7f, 0.65f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteBrightness = 0.4f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes")
	FVector MoteDrift = FVector(6.0, -3.0, 2.0);

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteWobble = 8.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteWobbleSpeed = 0.4f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0, ClampMax=1))
	float MoteTwinkle = 0.5f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0.01, ClampMax=0.5))
	float MoteEdgeFade = 0.15f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteNearFade = 250.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteFadeStart = 2500.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes", meta=(ClampMin=0))
	float MoteFadeEnd = 4000.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Motes")
	TSoftObjectPtr<UStaticMesh> MoteMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane")));

	UPROPERTY(EditAnywhere, Category="Underground|Motes")
	TSoftObjectPtr<UMaterialInterface> MoteMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/M_HexPlanetMote.M_HexPlanetMote")));

	/** Spores of the kinds of objects with Spores (UHexPlanetObjectSet::Spores), at most SporeMax around the point the camera looks at, of the cells
	 *  within SporeRadius, nearest first, spread SporeSpread of the radius of a cell around its middle: each rises SporeRise over SporeLife
	 *  seconds in a loop, swirling SporeSwirl around the up of its cell, SporeSize wide and never under SporeMinPixels, in the glow color of its
	 *  kind times SporeBrightness; at 1.5 pixels, as the motes, their soft disc lit less than a tenth of a pixel and none showed */
	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0, ClampMax=4000))
	int32 SporeMax = 240;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0))
	float SporeRadius = 2500.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0.1))
	float SporeSize = 4.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0))
	float SporeMinPixels = 3.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0))
	float SporeBrightness = 3.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0))
	float SporeRise = 35.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0.1))
	float SporeLife = 6.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0))
	float SporeSwirl = 6.0f;

	UPROPERTY(EditAnywhere, Category="Underground|Spores", meta=(ClampMin=0, ClampMax=1))
	float SporeSpread = 0.6f;

	/** The bound M_HexPlanetMote must give the offsets of its vertices (max_world_position_offset_displacement, which create_planet_assets.py
	 *  sets from DA_HexUnderground), as a mote may move from where its instance stands anywhere in its box */
	float GetMoteWpoBound() const { return 1.75f * MoteBox + MoteHeightBelow + MoteHeightAbove + SporeRise + SporeSwirl; }

	/** Returns the color of a network, white for none */
	FLinearColor GetNetworkColor(int32 Network) const
	{
		return NetworkColors.IsValidIndex(Network) ? NetworkColors[Network] : FLinearColor::White;
	}
};
