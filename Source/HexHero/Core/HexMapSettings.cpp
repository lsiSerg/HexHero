// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexMapSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HexBiomeConfig.h"
#include "HexCameraConfig.h"
#include "HexHero.h"
#include "HexInputConfig.h"
#include "HexKeyLegendConfig.h"
#include "HexLevelConfig.h"
#include "HexMapConfig.h"
#include "HexTerrainLayersConfig.h"

namespace
{
	template <typename ConfigType>
	const ConfigType* LoadConfigAsset(const TSoftObjectPtr<ConfigType>& Asset)
	{
		if (const ConfigType* Config = Asset.LoadSynchronous())
		{
			return Config;
		}

		UE_LOG(LogHexHero, Error, TEXT("Could not load the hex map config '%s'"), *Asset.ToString());
		return GetDefault<ConfigType>();
	}

	/** Loads all assets of a class in a folder, ordered by asset name */
	template <typename ConfigType>
	TArray<const ConfigType*> LoadFolderAssets(const FDirectoryPath& Folder)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

#if WITH_EDITOR
		// the background scan of the editor may not have reached the folder yet
		AssetRegistry.ScanPathsSynchronous({ Folder.Path });
#endif

		FARFilter Filter;
		Filter.PackagePaths.Add(*Folder.Path);
		Filter.ClassPaths.Add(ConfigType::StaticClass()->GetClassPathName());

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		Assets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});

		TArray<const ConfigType*> Configs;

		for (const FAssetData& Asset : Assets)
		{
			if (const ConfigType* Config = Cast<ConfigType>(Asset.GetAsset()))
			{
				Configs.Add(Config);
			}
			else
			{
				UE_LOG(LogHexHero, Error, TEXT("Could not load the hex map config '%s'"), *Asset.GetObjectPathString());
			}
		}

		if (Configs.IsEmpty())
		{
			UE_LOG(LogHexHero, Error, TEXT("There are no %s assets in %s"), *ConfigType::StaticClass()->GetName(), *Folder.Path);
		}

		return Configs;
	}
}

TArray<const UHexLevelConfig*> UHexMapSettings::LoadLevels() const
{
	TArray<const UHexLevelConfig*> Levels = LoadFolderAssets<UHexLevelConfig>(LevelsFolder);

	Levels.Sort([](const UHexLevelConfig& A, const UHexLevelConfig& B)
	{
		return A.Level < B.Level;
	});

	return Levels;
}

TArray<const UHexBiomeConfig*> UHexMapSettings::LoadBiomes() const
{
	return LoadFolderAssets<UHexBiomeConfig>(BiomesFolder);
}

const UHexMapConfig* UHexMapSettings::LoadMapConfig() const
{
	return LoadConfigAsset(MapConfig);
}

const UHexCameraConfig* UHexMapSettings::LoadCameraConfig() const
{
	return LoadConfigAsset(CameraConfig);
}

const UHexInputConfig* UHexMapSettings::LoadInputConfig() const
{
	return LoadConfigAsset(InputConfig);
}

const UHexKeyLegendConfig* UHexMapSettings::LoadKeyLegend() const
{
	return LoadConfigAsset(KeyLegend);
}

const UHexTerrainLayersConfig* UHexMapSettings::LoadTerrainLayers() const
{
	return LoadConfigAsset(TerrainLayers);
}
