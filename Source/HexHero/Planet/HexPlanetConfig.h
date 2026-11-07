// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexPlanetConfig.generated.h"

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

	/** Height of one level of land above the sea */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=0))
	float LevelHeight = 25.0f;

	/** Frequency of the planet that the values of scale and speed in DA_HexPlanet and DA_HexPlanetClouds are tuned on, that of Terra.
	 *  On another planet the ranges of mountains and the clouds scale by its frequency over this one, so they keep their size and speed in
	 *  cells, and a smaller planet has fewer of them rather than the same ones, larger. GetSizeRatio gives that ratio */
	UPROPERTY(EditAnywhere, Category="Grid", meta=(ClampMin=1))
	int32 ReferenceFrequency = 80;

	/** Returns the frequency of a planet over ReferenceFrequency, by which sizes of noise on the unit sphere scale */
	double GetSizeRatio(int32 Frequency) const { return double(Frequency) / FMath::Max(ReferenceFrequency, 1); }

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
	 *  not go on under the sea */
	UPROPERTY(EditAnywhere, Category="Textures")
	float SeabedHeight = 6.0f;

	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0.01))
	float SeabedFade = 9.0f;

	/** How much the heights of the beach and the seabed decide which lies where in that band, so snow stays in the hollows of the sand */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0))
	float SeabedHeightBlend = 0.6f;

	/** Width of the height over which they blend there */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0.01))
	float SeabedBlendDepth = 0.15f;

	/** How much the beach (snow) dusts the seabed in the upper part of that band, so the shore reads as snowy sand */
	UPROPERTY(EditAnywhere, Category="Textures", meta=(ClampMin=0, ClampMax=1))
	float SeabedDusting = 0.55f;

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

	/** Outline of the cell under the mouse */
	UPROPERTY(EditAnywhere, Category="Highlight")
	FLinearColor OutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Outlines of its neighbors, while the debug wireframe draws them too */
	UPROPERTY(EditAnywhere, Category="Highlight")
	FLinearColor NeighborOutlineColor = FLinearColor(0.05f, 0.7f, 1.0f, 1.0f);

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
	float SnowRockCover = 0.85f;

	/** How much the high edges of that rock, by its height, peek out of the snow */
	UPROPERTY(EditAnywhere, Category="Colors|Rock", meta=(ClampMin=0, ClampMax=1))
	float SnowRockPeek = 0.5f;

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

	/** Light the surface gives by itself, so the night side is not black */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float Ambient = 0.06f;

	/** Glow of the outlines of the cell under the mouse */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0))
	float Glow = 1.5f;

	/** Roughness of the surface */
	UPROPERTY(EditAnywhere, Category="Surface", meta=(ClampMin=0, ClampMax=1))
	float Roughness = 0.85f;
};
