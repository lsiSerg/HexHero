// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexTerrainLayersConfig.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "HexHero.h"

#if WITH_EDITOR
#include "ImageCore.h"
#endif

int32 UHexTerrainLayersConfig::FindLayer(FName Name) const
{
	if (Name.IsNone())
	{
		return INDEX_NONE;
	}
	return Layers.IndexOfByPredicate([Name](const FHexTerrainLayer& Layer) { return Layer.Name == Name; });
}

UTexture2D* UHexTerrainLayersConfig::CreateParamsTexture() const
{
	constexpr int32 Rows = 3;
	const int32 Columns = FMath::Max(Layers.Num(), 1);
	TArray<FLinearColor> Pixels;
	Pixels.Init(FLinearColor(1.0f, 1.0f, 1.0f, 320.0f), Columns * Rows);

	for (int32 Layer = 0; Layer < Layers.Num(); ++Layer)
	{
		const FHexTerrainLayer& Each = Layers[Layer];
		Pixels[Layer] = FLinearColor(Each.Tint.R, Each.Tint.G, Each.Tint.B, FMath::Max(Each.TileSize, 1.0f));
		Pixels[Columns + Layer] = FLinearColor(Each.NormalStrength, Each.HeightContrast, Each.HeightOffset, 0.0f);
		const int32 Scree = FindLayer(Each.ScreeLayer);
		const int32 Gravel = FindLayer(Each.GravelLayer);
		if (!Each.ScreeLayer.IsNone() && Scree == INDEX_NONE)
		{
			UE_LOG(LogHexHero, Error, TEXT("Terrain layer %s names the scree layer %s, which %s does not have"), *Each.Name.ToString(), *Each.ScreeLayer.ToString(), *GetName());
		}
		if (!Each.GravelLayer.IsNone() && Gravel == INDEX_NONE)
		{
			UE_LOG(LogHexHero, Error, TEXT("Terrain layer %s names the gravel layer %s, which %s does not have"), *Each.Name.ToString(), *Each.GravelLayer.ToString(), *GetName());
		}
		Pixels[Columns * 2 + Layer] = FLinearColor(float(Scree + 1), float(Gravel + 1), 0.0f, 0.0f);
	}

	const FName TextureName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("TerrainLayerParams"));
	UTexture2D* Texture = UTexture2D::CreateTransient(Columns, Rows, PF_A32B32G32R32F, TextureName);
	Texture->SRGB = false;
	Texture->Filter = TF_Nearest;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

#if WITH_EDITOR
namespace
{
	/** The first mip of the source of a texture, or false */
	bool GetSourceImage(UTexture2D* Texture, FImage& Image)
	{
		return Texture && Texture->Source.IsValid() && Texture->Source.GetMipImage(Image, 0, 0, 0);
	}

	/** The first mip of the source of a texture resized to Size, in Format, filtered as a tile, and its pixels added to Hash */
	bool GetSizedImage(UTexture2D* Texture, int32 Size, ERawImageFormat::Type Format, EGammaSpace Gamma, bool bAsStored, FImage& Image, uint32& Hash)
	{
		FImage Source;
		if (!GetSourceImage(Texture, Source))
		{
			return false;
		}
		Hash = FCrc::MemCrc32(Source.RawData.GetData(), Source.RawData.Num(), Hash);
		if (bAsStored)
		{
			// a normal or a height is read as it is stored, whatever its gamma says
			Source.GammaSpace = EGammaSpace::Linear;
		}
		FImageCore::ResizeImageAllocDest(Source, Image, Size, Size, Format, Gamma,
			FImageCore::EResizeImageFilter::Default | FImageCore::EResizeImageFilter::Flag_WrapX | FImageCore::EResizeImageFilter::Flag_WrapY);
		return true;
	}

	/** Writes the images as the slices of the source of the array, which then names no source textures */
	void FillArray(UTexture2DArray* Array, const TArray<FImage>& Images, bool bSRGB)
	{
		const int32 Size = Images[0].SizeX;
		Array->PreEditChange(nullptr);
		Array->SourceTextures.Empty();
		Array->SRGB = bSRGB;
		Array->Source.Init(Size, Size, Images.Num(), 1, TSF_BGRA8);
		uint8* Data = Array->Source.LockMip(0);
		const int64 SliceBytes = Array->Source.CalcMipSize(0) / Images.Num();
		for (int32 Slice = 0; Slice < Images.Num(); ++Slice)
		{
			check(Images[Slice].RawData.Num() == SliceBytes);
			FMemory::Memcpy(Data + SliceBytes * Slice, Images[Slice].RawData.GetData(), SliceBytes);
		}
		Array->Source.UnlockMip(0);
		Array->PostEditChange();
		Array->MarkPackageDirty();
	}
}

void UHexTerrainLayersConfig::BuildTextureArrays()
{
	if (!ColorArray || !NormalArray)
	{
		UE_LOG(LogHexHero, Error, TEXT("%s has no ColorArray or NormalArray to build"), *GetName());
		return;
	}

	const int32 Size = int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::Clamp(ArraySize, 64, 4096))));
	TArray<FImage> Colors;
	TArray<FImage> Normals;
	bool bComplete = true;
	uint32 Hash = FCrc::MemCrc32(&Size, sizeof(Size), uint32(Layers.Num()));

	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FHexTerrainLayer& Layer = Layers[Index];
		FImage& Color = Colors.AddDefaulted_GetRef();
		FImage& Normal = Normals.AddDefaulted_GetRef();
		if (!GetSizedImage(Layer.Color, Size, ERawImageFormat::BGRA8, EGammaSpace::sRGB, false, Color, Hash)
			|| !GetSizedImage(Layer.Normal, Size, ERawImageFormat::BGRA8, EGammaSpace::Linear, true, Normal, Hash))
		{
			UE_LOG(LogHexHero, Error, TEXT("Terrain layer %d (%s) of %s has no color or normal texture with a source"), Index, *Layer.Name.ToString(), *GetName());
			bComplete = false;
			continue;
		}
		Hash = FCrc::MemCrc32(&Layer.bNormalGreenUp, sizeof(Layer.bNormalGreenUp), Hash);

		// the height goes into the alpha of the color; without one the alpha of the color is the height
		FImage Height;
		if (GetSizedImage(Layer.Height, Size, ERawImageFormat::G8, EGammaSpace::Linear, true, Height, Hash))
		{
			TArrayView64<FColor> Pixels = Color.AsBGRA8();
			TArrayView64<const uint8> Heights = static_cast<const FImage&>(Height).AsG8();
			for (int64 Pixel = 0; Pixel < Pixels.Num(); ++Pixel)
			{
				Pixels[Pixel].A = Heights[Pixel];
			}
		}

		if (Layer.bNormalGreenUp)
		{
			for (FColor& Pixel : Normal.AsBGRA8())
			{
				Pixel.G = 255 - Pixel.G;
			}
		}
	}

	if (!bComplete)
	{
		return;
	}

	const int64 SourceHash = int64(Hash) + 1;
	if (SourceHash == BuiltSourceHash && ColorArray->SourceTextures.IsEmpty() && NormalArray->SourceTextures.IsEmpty()
		&& ColorArray->Source.GetNumSlices() == Colors.Num() && NormalArray->Source.GetNumSlices() == Normals.Num())
	{
		UE_LOG(LogHexHero, Log, TEXT("%s: no source of its %d layers changed, the arrays stay as they are"), *GetName(), Colors.Num());
		return;
	}
	BuiltSourceHash = SourceHash;
	MarkPackageDirty();
	FillArray(ColorArray, Colors, true);
	FillArray(NormalArray, Normals, false);
	UE_LOG(LogHexHero, Log, TEXT("%s built %d layers into %s and %s at %dx%d (mip settings %d, compression %d and %d)"), *GetName(), Colors.Num(),
		*ColorArray->GetName(), *NormalArray->GetName(), Size, Size, int32(ColorArray->MipGenSettings), int32(ColorArray->CompressionSettings),
		int32(NormalArray->CompressionSettings));
}
#endif
