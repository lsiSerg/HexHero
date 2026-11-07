// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexTerrainLayersConfig.generated.h"

class UTexture2D;
class UTexture2DArray;

/** One texture layer of the terrain: the textures an artist picks and how the planet draws them */
USTRUCT(BlueprintType)
struct FHexTerrainLayer
{
	GENERATED_BODY()

	/** Name of the layer, by which ScreeLayer and GravelLayer of rock layers find it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer")
	FName Name;

	/** Color in sRGB. Without a Height texture its alpha is the height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Textures")
	TObjectPtr<UTexture2D> Color;

	/** Height (displacement) in red, packed into the alpha of the color when the arrays are built. It decides which layer covers the other where
	 *  they blend, so stones stand out of sand and not the other way. Optional */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Textures")
	TObjectPtr<UTexture2D> Height;

	/** Normal map in the DirectX convention, as Unreal uses (green down) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Textures")
	TObjectPtr<UTexture2D> Normal;

	/** The normal map is in the OpenGL convention (green up), so its green is flipped when the arrays are built */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Textures")
	bool bNormalGreenUp = false;

	/** Multiplies the color on the planet */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet")
	FLinearColor Tint = FLinearColor::White;

	/** World units of one repeat of the textures on the planet, where a cell is about 200 across */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet", meta=(ClampMin=1))
	float TileSize = 320.0f;

	/** Multiplies how much the normal map bends the surface on the planet */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet", meta=(ClampMin=0))
	float NormalStrength = 1.0f;

	/** Contrast of the height around its middle, so a layer with flat heights still covers its neighbours in clear patches */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet", meta=(ClampMin=0))
	float HeightContrast = 1.0f;

	/** Added to the height, so the layer covers its neighbours more (above 0) or less */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet", meta=(ClampMin=-1, ClampMax=1))
	float HeightOffset = 0.0f;

	/** For a rock: the layer of loose stone on its middle slopes and in its gullies, between the cliff (this layer) on steep ground and the
	 *  gravel; None draws this layer everywhere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rock")
	FName ScreeLayer;

	/** For a rock: the layer on its flat ground and at its foot; None draws the scree there, or this layer without scree */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rock")
	FName GravelLayer;
};

/**
 *  The texture layers of the terrain, shared by the flat map and the planet. The index of a layer here is the layer number that the biome
 *  assets name (TerrainLayer, MountainRockLayer, PlanetBeachLayer), so append new layers and keep the order. Build Texture Arrays writes the
 *  textures into ColorArray (height in alpha) and NormalArray at ArraySize, which the materials sample, so press it after changing a texture
 *  and save.
 *  The other values of a layer reach the planet at runtime (CreateParamsTexture). The noise of the water is T_HexNoise, apart from the
 *  layers, so any layer, snow too, can be replaced.
 */
UCLASS(BlueprintType)
class UHexTerrainLayersConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layers")
	TArray<FHexTerrainLayer> Layers;

	/** The array of the colors with height in alpha, written by Build Texture Arrays */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arrays")
	TObjectPtr<UTexture2DArray> ColorArray;

	/** The array of the normals, written by Build Texture Arrays */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arrays")
	TObjectPtr<UTexture2DArray> NormalArray;

	/** Width and height of every layer in the arrays, a power of two: each source is resized to it, so a larger source costs no memory of
	 *  the GPU and no space in version control. At 1024 the 18 layers take about 48 MB of the GPU, at 2048 four times as much */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Arrays", meta=(ClampMin=64, ClampMax=4096))
	int32 ArraySize = 1024;

	/** A checksum of every source pixel and flag the arrays were last built from, so Build Texture Arrays leaves them alone when nothing
	 *  changed, as each holds a copy of all layers and rewriting it adds hundreds of MB to version control */
	UPROPERTY(VisibleAnywhere, Category="Arrays")
	int64 BuiltSourceHash = 0;

	/** The index of the layer with this name, or INDEX_NONE */
	int32 FindLayer(FName Name) const;

	/** Creates a transient float texture with a column per layer that M_HexPlanet reads as LayerParams: Tint and TileSize, then
	 *  NormalStrength, HeightContrast, HeightOffset, then the scree and gravel layers plus one (0 for none) */
	UTexture2D* CreateParamsTexture() const;

#if WITH_EDITOR
	/** Writes the layers into ColorArray and NormalArray at ArraySize, the height in the alpha of the color and a normal of the OpenGL
	 *  convention flipped, in memory, so no packed texture is saved. The arrays are left dirty, to be saved. A layer without a color or a
	 *  normal is logged as an error, and the arrays stay as they were */
	UFUNCTION(CallInEditor, BlueprintCallable, Category="Arrays")
	void BuildTextureArrays();
#endif
};
