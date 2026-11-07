// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexMapLoader.h"
#include "HAL/FileManager.h"
#include "HexBiomeConfig.h"
#include "HexHero.h"
#include "HexLevelConfig.h"
#include "HexMapData.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	/** Returns a color that compares with others whatever its alpha, which map files ignore */
	uint32 GetColorKey(FColor Color)
	{
		Color.A = 255;
		return Color.DWColor();
	}

	FString GetPixelText(int32 Index, int32 Width)
	{
		return FString::Printf(TEXT("Pixel (%d, %d)"), Index % Width, Index / Width);
	}

	FString GetColorText(const FColor& Color)
	{
		return FString::Printf(TEXT("#%02X%02X%02X"), Color.R, Color.G, Color.B);
	}

	bool LoadImage(const FString& FilePath, TArray<uint8>& OutRawData, int32& OutWidth, int32& OutHeight, FString& OutError)
	{
		TArray<uint8> FileData;

		if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
		{
			OutError = FString::Printf(TEXT("Could not read map file: %s"), *FilePath);
			return false;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

		if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			OutError = FString::Printf(TEXT("Map file is not a valid PNG: %s"), *FilePath);
			return false;
		}

		if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, OutRawData))
		{
			OutError = FString::Printf(TEXT("Could not decode map file: %s"), *FilePath);
			return false;
		}

		OutWidth = ImageWrapper->GetWidth();
		OutHeight = ImageWrapper->GetHeight();
		return true;
	}

	/** Returns the index of the neighbor of a hex in a direction, or INDEX_NONE off the map */
	int32 GetNeighborIndex(const FHexMapData& Map, int32 Index, int32 Direction)
	{
		const FHexCoord Neighbor = FHexCoord::FromOffset(Index % Map.Width, Index / Map.Width).GetNeighbor(Direction);
		return Map.Contains(Neighbor) ? Neighbor.GetRow() * Map.Width + Neighbor.GetColumn() : INDEX_NONE;
	}

	/** Returns how many steps of 60 degrees lie between two directions */
	int32 GetTurnSteps(int32 A, int32 B)
	{
		const int32 Steps = FMath::Abs(A - B) % FHexCoord::NumDirections;
		return FMath::Min(Steps, FHexCoord::NumDirections - Steps);
	}

	TConstArrayView<FColor> GetPixels(const TArray<uint8>& RawData)
	{
		return TConstArrayView<FColor>(reinterpret_cast<const FColor*>(RawData.GetData()), RawData.Num() / sizeof(FColor));
	}

	bool ReadManifest(const FString& Folder, FHexMapManifest& OutManifest, FString& OutError)
	{
		const FString FilePath = HexMapLoader::GetMapFilePath(Folder, HexMapLoader::ManifestFile);
		FString Json;

		if (!FFileHelper::LoadFileToString(Json, *FilePath))
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *FilePath);
			return false;
		}

		if (!HexMapLoader::ParseManifest(Json, OutManifest, OutError))
		{
			OutError = FString::Printf(TEXT("%s: %s"), *FilePath, *OutError);
			return false;
		}

		return true;
	}
}

FString HexMapLoader::GetMapFilePath(const FString& Folder, const TCHAR* File)
{
	return FPaths::Combine(FPaths::ProjectContentDir(), RootFolder, Folder, File);
}

TArray<FHexMapEntry> HexMapLoader::FindMaps()
{
	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *FPaths::Combine(FPaths::ProjectContentDir(), RootFolder, TEXT("*")), false, true);

	TArray<FHexMapEntry> Maps;

	for (const FString& Folder : Folders)
	{
		FHexMapEntry Entry;
		Entry.Folder = Folder;
		FString Error;

		if (ReadManifest(Folder, Entry.Manifest, Error))
		{
			Maps.Add(MoveTemp(Entry));
		}
		else
		{
			UE_LOG(LogHexHero, Error, TEXT("Skipping a map: %s"), *Error);
		}
	}

	Maps.Sort([](const FHexMapEntry& A, const FHexMapEntry& B)
	{
		return A.Manifest.SortOrder != B.Manifest.SortOrder ? A.Manifest.SortOrder < B.Manifest.SortOrder : A.Folder < B.Folder;
	});

	return Maps;
}

bool HexMapLoader::ParseManifest(const FString& Json, FHexMapManifest& OutManifest, FString& OutError)
{
	FHexMapManifest Manifest;

	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Manifest))
	{
		OutError = TEXT("Not valid JSON");
		return false;
	}

	if (Manifest.Version != ManifestVersion)
	{
		OutError = FString::Printf(TEXT("Version %d is not supported. This code reads version %d"), Manifest.Version, ManifestVersion);
		return false;
	}

	if (Manifest.Name.IsEmpty())
	{
		OutError = TEXT("The map has no name");
		return false;
	}

	OutManifest = MoveTemp(Manifest);
	return true;
}

bool HexMapLoader::LoadMap(const FString& Folder, const TArray<const UHexLevelConfig*>& Levels, const TArray<const UHexBiomeConfig*>& Biomes, FHexMapData& OutMap, FString& OutError)
{
	FHexMapManifest Manifest;

	if (!ReadManifest(Folder, Manifest, OutError) || !ValidateBiomes(Biomes, Levels, OutError))
	{
		return false;
	}

	TArray<uint8> RawData;
	int32 Width = 0;
	int32 Height = 0;
	FHexMapData Map;

	if (!LoadImage(GetMapFilePath(Folder, LevelsFile), RawData, Width, Height, OutError) || !DecodeLevels(GetPixels(RawData), Width, Height, Levels, Map, OutError))
	{
		return false;
	}

	if (Map.Width != Manifest.Width || Map.Height != Manifest.Height)
	{
		OutError = FString::Printf(TEXT("%s says the map is %dx%d, but %s is %dx%d"), ManifestFile, Manifest.Width, Manifest.Height, LevelsFile, Map.Width, Map.Height);
		return false;
	}

	const FString BiomesPath = GetMapFilePath(Folder, BiomesFile);

	if (FPaths::FileExists(BiomesPath))
	{
		if (!LoadImage(BiomesPath, RawData, Width, Height, OutError) || !DecodeBiomes(GetPixels(RawData), Width, Height, Biomes, Map, OutError))
		{
			return false;
		}
	}
	else if (!SetDefaultBiomes(Biomes, Map, OutError))
	{
		return false;
	}

	const FString RiversPath = GetMapFilePath(Folder, RiversFile);

	if (FPaths::FileExists(RiversPath))
	{
		FString RiversJson;

		if (!FFileHelper::LoadFileToString(RiversJson, *RiversPath))
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *RiversPath);
			return false;
		}

		if (!ParseRivers(RiversJson, Map, OutError) || !ValidateRiverLevels(Map, Levels, OutError))
		{
			OutError = FString::Printf(TEXT("%s: %s"), RiversFile, *OutError);
			return false;
		}
	}

	OutMap = MoveTemp(Map);
	return true;
}

bool HexMapLoader::DecodeLevels(TConstArrayView<FColor> Pixels, int32 Width, int32 Height, const TArray<const UHexLevelConfig*>& Levels, FHexMapData& OutMap, FString& OutError)
{
	if (Width <= 0 || Height <= 0 || Width > MaxMapSize || Height > MaxMapSize)
	{
		OutError = FString::Printf(TEXT("Map size %dx%d is not supported. The largest map is %dx%d"), Width, Height, MaxMapSize, MaxMapSize);
		return false;
	}

	if (Pixels.Num() != Width * Height)
	{
		OutError = FString::Printf(TEXT("Map size %dx%d does not match its %d pixels"), Width, Height, Pixels.Num());
		return false;
	}

	TMap<uint32, int8> LevelByColor;
	LevelByColor.Reserve(Levels.Num());

	for (const UHexLevelConfig* Definition : Levels)
	{
		LevelByColor.Add(GetColorKey(Definition->FileColor), static_cast<int8>(Definition->Level));
	}

	FHexMapData Map;
	Map.Width = Width;
	Map.Height = Height;
	Map.Levels.SetNumUninitialized(Pixels.Num());

	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const int8* Level = LevelByColor.Find(GetColorKey(Pixels[Index]));

		if (!Level)
		{
			OutError = FString::Printf(TEXT("%s: unknown color %s"), *GetPixelText(Index, Width), *GetColorText(Pixels[Index]));
			return false;
		}

		Map.Levels[Index] = *Level;
	}

	OutMap = MoveTemp(Map);
	return true;
}

bool HexMapLoader::DecodeBiomes(TConstArrayView<FColor> Pixels, int32 Width, int32 Height, const TArray<const UHexBiomeConfig*>& Biomes, FHexMapData& InOutMap, FString& OutError)
{
	if (Width != InOutMap.Width || Height != InOutMap.Height || Pixels.Num() != Width * Height)
	{
		OutError = FString::Printf(TEXT("The biomes are %dx%d with %d pixels, but the map is %dx%d"), Width, Height, Pixels.Num(), InOutMap.Width, InOutMap.Height);
		return false;
	}

	TMap<uint32, uint8> BiomeByColor;
	BiomeByColor.Reserve(Biomes.Num());

	for (int32 Index = 0; Index < Biomes.Num(); ++Index)
	{
		BiomeByColor.Add(GetColorKey(Biomes[Index]->FileColor), static_cast<uint8>(Index));
	}

	TArray<uint8> Result;
	Result.SetNumUninitialized(Pixels.Num());

	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const uint8* Biome = BiomeByColor.Find(GetColorKey(Pixels[Index]));

		if (!Biome)
		{
			OutError = FString::Printf(TEXT("%s of the biomes: unknown color %s"), *GetPixelText(Index, Width), *GetColorText(Pixels[Index]));
			return false;
		}

		if (!Biomes[*Biome]->AllowedLevels.Contains(InOutMap.Levels[Index]))
		{
			OutError = FString::Printf(TEXT("%s of the biomes: %s cannot be on level %d"), *GetPixelText(Index, Width), *Biomes[*Biome]->DisplayName, InOutMap.Levels[Index]);
			return false;
		}

		Result[Index] = *Biome;
	}

	InOutMap.Biomes = MoveTemp(Result);
	return true;
}

bool HexMapLoader::ParseRivers(const FString& Json, FHexMapData& InOutMap, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;

	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutError = TEXT("Not valid JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* RiverValues = nullptr;

	if (!Root->TryGetArrayField(TEXT("rivers"), RiverValues))
	{
		OutError = TEXT("There is no list of rivers");
		return false;
	}

	const auto GetHexText = [](const FHexCoord& Hex)
	{
		return FString::Printf(TEXT("(%d, %d)"), Hex.GetColumn(), Hex.GetRow());
	};

	// a hex is [column, row] in map file order, as the debug panel shows it
	const auto ReadHex = [&InOutMap](const TSharedPtr<FJsonValue>& Value, FHexCoord& OutHex)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		double Column = 0.0;
		double Row = 0.0;

		if (!Value.IsValid() || !Value->TryGetArray(Pair) || Pair->Num() != 2 || !(*Pair)[0]->TryGetNumber(Column) || !(*Pair)[1]->TryGetNumber(Row))
		{
			return false;
		}

		OutHex = FHexCoord::FromOffset(FMath::RoundToInt32(Column), FMath::RoundToInt32(Row));
		return InOutMap.Contains(OutHex);
	};

	TArray<FHexRiverPath> Paths;

	for (int32 RiverIndex = 0; RiverIndex < RiverValues->Num(); ++RiverIndex)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FHexRiverPath Path;
		Path.Name = FString::Printf(TEXT("River %d"), RiverIndex + 1);
		Path.Seed = RiverIndex;

		if (!(*RiverValues)[RiverIndex]->TryGetObject(Object))
		{
			OutError = FString::Printf(TEXT("%s is not an object"), *Path.Name);
			return false;
		}

		(*Object)->TryGetStringField(TEXT("name"), Path.Name);
		const TArray<TSharedPtr<FJsonValue>>* HexValues = nullptr;

		if (!(*Object)->TryGetArrayField(TEXT("hexes"), HexValues) || HexValues->IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s has no hexes"), *Path.Name);
			return false;
		}

		for (int32 Index = 0; Index < HexValues->Num(); ++Index)
		{
			FHexCoord Hex;

			if (!ReadHex((*HexValues)[Index], Hex))
			{
				OutError = FString::Printf(TEXT("%s: hex %d is not a [column, row] on the map"), *Path.Name, Index + 1);
				return false;
			}

			Path.Hexes.Add(Hex);
		}

		// every river has the width of DA_HexMap, so bridges fit any of them
		for (const TCHAR* Field : { TEXT("width"), TEXT("widthVariation"), TEXT("pools") })
		{
			if ((*Object)->HasField(Field))
			{
				OutError = FString::Printf(TEXT("%s: \"%s\" is not supported, as every river has the width of RiverWidth in DA_HexMap"), *Path.Name, Field);
				return false;
			}
		}

		double Number = 0.0;

		if ((*Object)->TryGetNumberField(TEXT("meander"), Number))
		{
			Path.Meander = static_cast<float>(Number);
		}

		if ((*Object)->TryGetNumberField(TEXT("seed"), Number))
		{
			Path.Seed = FMath::RoundToInt32(Number);
		}

		Paths.Add(MoveTemp(Path));
	}

	// each hex flows on in one river only, but the last hex of a tributary lies in the river it meets
	TMap<FHexCoord, int32> PathOfHex;

	for (int32 PathIndex = 0; PathIndex < Paths.Num(); ++PathIndex)
	{
		const FHexRiverPath& Path = Paths[PathIndex];

		for (int32 Index = 0; Index < Path.Hexes.Num(); ++Index)
		{
			const FHexCoord& Hex = Path.Hexes[Index];

			if (InOutMap.GetLevel(Hex) < 0)
			{
				OutError = FString::Printf(TEXT("%s: hex %s is water"), *Path.Name, *GetHexText(Hex));
				return false;
			}

			if (Index > 0 && Path.Hexes[Index - 1].DistanceTo(Hex) != 1)
			{
				OutError = FString::Printf(TEXT("%s: hex %s does not touch %s before it"), *Path.Name, *GetHexText(Hex), *GetHexText(Path.Hexes[Index - 1]));
				return false;
			}

			if (Index > 0 && InOutMap.GetLevel(Hex) > InOutMap.GetLevel(Path.Hexes[Index - 1]))
			{
				OutError = FString::Printf(TEXT("%s: it would flow uphill from %s to %s"), *Path.Name, *GetHexText(Path.Hexes[Index - 1]), *GetHexText(Hex));
				return false;
			}

			if (Path.Hexes.Find(Hex) != Index)
			{
				OutError = FString::Printf(TEXT("%s: hex %s comes twice"), *Path.Name, *GetHexText(Hex));
				return false;
			}

			if (Index + 1 == Path.Hexes.Num())
			{
				continue;
			}

			if (const int32* Other = PathOfHex.Find(Hex))
			{
				OutError = FString::Printf(TEXT("%s and %s both flow through %s. Only a tributary may end in another river"), *Paths[*Other].Name, *Path.Name, *GetHexText(Hex));
				return false;
			}

			PathOfHex.Add(Hex, PathIndex);
		}
	}

	for (int32 PathIndex = 0; PathIndex < Paths.Num(); ++PathIndex)
	{
		FHexRiverPath& Path = Paths[PathIndex];
		const FHexCoord& Last = Path.Hexes.Last();

		if (const int32* Other = PathOfHex.Find(Last))
		{
			if (Path.Hexes.Num() < 2)
			{
				OutError = FString::Printf(TEXT("%s lies only in %s"), *Path.Name, *Paths[*Other].Name);
				return false;
			}

			Path.Joins = *Other;
			continue;
		}

		bool bTouchesWater = false;

		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			const FHexCoord Neighbor = Last.GetNeighbor(Direction);
			bTouchesWater |= InOutMap.Contains(Neighbor) && InOutMap.GetLevel(Neighbor) < 0;
		}

		if (!bTouchesWater)
		{
			OutError = FString::Printf(TEXT("%s: its last hex %s touches neither water nor another river"), *Path.Name, *GetHexText(Last));
			return false;
		}
	}

	// tributaries must end in the sea through other rivers, not go round in a ring
	for (int32 PathIndex = 0; PathIndex < Paths.Num(); ++PathIndex)
	{
		int32 Next = Paths[PathIndex].Joins;

		for (int32 Steps = 0; Next != INDEX_NONE; ++Steps)
		{
			if (Steps > Paths.Num())
			{
				OutError = FString::Printf(TEXT("%s flows into rivers that flow back into it"), *Paths[PathIndex].Name);
				return false;
			}

			Next = Paths[Next].Joins;
		}
	}

	if (Paths.IsEmpty())
	{
		InOutMap.Rivers.Reset();
		InOutMap.RiverPaths.Reset();
		return true;
	}

	const auto GetIndex = [&InOutMap](const FHexCoord& Hex) { return Hex.GetRow() * InOutMap.Width + Hex.GetColumn(); };
	const auto GetDirection = [](const FHexCoord& From, const FHexCoord& To)
	{
		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			if (From.GetNeighbor(Direction) == To)
			{
				return Direction;
			}
		}

		return static_cast<int32>(INDEX_NONE);
	};

	TArray<FHexRiver> Rivers;
	Rivers.SetNum(InOutMap.Width * InOutMap.Height);

	for (int32 PathIndex = 0; PathIndex < Paths.Num(); ++PathIndex)
	{
		const FHexRiverPath& Path = Paths[PathIndex];

		for (int32 Index = 0; Index + 1 < Path.Hexes.Num(); ++Index)
		{
			const int32 Outflow = GetDirection(Path.Hexes[Index], Path.Hexes[Index + 1]);
			FHexRiver& River = Rivers[GetIndex(Path.Hexes[Index])];
			River.Outflow = static_cast<int8>(Outflow);
			River.Path = PathIndex;
			Rivers[GetIndex(Path.Hexes[Index + 1])].InflowMask |= 1 << ((Outflow + 3) % FHexCoord::NumDirections);
		}

		if (Path.Joins != INDEX_NONE)
		{
			continue;
		}

		// the mouth goes straight on from the hex before it, or else into the deepest water
		const FHexCoord& Mouth = Path.Hexes.Last();
		const int32 Straight = Path.Hexes.Num() > 1 ? GetDirection(Path.Hexes[Path.Hexes.Num() - 2], Mouth) : INDEX_NONE;
		int32 Outflow = INDEX_NONE;
		int32 OutflowTurns = 0;

		for (int32 Direction = 0; Direction < FHexCoord::NumDirections; ++Direction)
		{
			const FHexCoord Neighbor = Mouth.GetNeighbor(Direction);

			if (!InOutMap.Contains(Neighbor) || InOutMap.GetLevel(Neighbor) >= 0)
			{
				continue;
			}

			const int32 Turns = Straight == INDEX_NONE ? 0 : GetTurnSteps(Direction, Straight);

			if (Outflow == INDEX_NONE || Turns < OutflowTurns || (Turns == OutflowTurns && InOutMap.GetLevel(Neighbor) < InOutMap.GetLevel(Mouth.GetNeighbor(Outflow))))
			{
				Outflow = Direction;
				OutflowTurns = Turns;
			}
		}

		FHexRiver& River = Rivers[GetIndex(Mouth)];
		River.Outflow = static_cast<int8>(Outflow);
		River.bMouth = true;
		River.Path = PathIndex;
	}

	// sizes add up from the sources down
	TArray<int32> PendingInflows;
	PendingInflows.Init(0, Rivers.Num());
	TArray<int32> Ready;

	for (int32 Index = 0; Index < Rivers.Num(); ++Index)
	{
		if (Rivers[Index].HasRiver())
		{
			PendingInflows[Index] = FMath::CountBits(Rivers[Index].InflowMask);

			if (PendingInflows[Index] == 0)
			{
				Ready.Add(Index);
			}
		}
	}

	for (int32 Next = 0; Next < Ready.Num(); ++Next)
	{
		const int32 Index = Ready[Next];
		FHexRiver& River = Rivers[Index];
		++River.Size;

		if (!River.bMouth)
		{
			const int32 Downstream = GetNeighborIndex(InOutMap, Index, River.Outflow);
			Rivers[Downstream].Size += River.Size;

			if (--PendingInflows[Downstream] == 0)
			{
				Ready.Add(Downstream);
			}
		}
	}

	InOutMap.Rivers = MoveTemp(Rivers);
	InOutMap.RiverPaths = MoveTemp(Paths);
	return true;
}

bool HexMapLoader::ValidateRiverLevels(const FHexMapData& Map, const TArray<const UHexLevelConfig*>& Levels, FString& OutError)
{
	for (int32 Index = 0; Index < Map.Rivers.Num(); ++Index)
	{
		if (!Map.Rivers[Index].HasRiver())
		{
			continue;
		}

		for (const UHexLevelConfig* Definition : Levels)
		{
			if (Definition->Level == Map.Levels[Index] && (!Definition->JoinedMeshes.IsEmpty() || !Definition->FeatureMesh.IsNull()))
			{
				OutError = FString::Printf(TEXT("%s of the rivers: a river cannot be on %s, which has meshes. Start it next to them"), *GetPixelText(Index, Map.Width), *Definition->DisplayName);
				return false;
			}
		}
	}

	return true;
}

bool HexMapLoader::SetDefaultBiomes(const TArray<const UHexBiomeConfig*>& Biomes, FHexMapData& InOutMap, FString& OutError)
{
	TMap<int8, uint8> BiomeByLevel;

	for (int32 Index = 0; Index < Biomes.Num(); ++Index)
	{
		for (const int32 Level : Biomes[Index]->DefaultForLevels)
		{
			BiomeByLevel.Add(static_cast<int8>(Level), static_cast<uint8>(Index));
		}
	}

	TArray<uint8> Result;
	Result.SetNumUninitialized(InOutMap.Levels.Num());

	for (int32 Index = 0; Index < Result.Num(); ++Index)
	{
		const uint8* Biome = BiomeByLevel.Find(InOutMap.Levels[Index]);

		if (!Biome)
		{
			OutError = FString::Printf(TEXT("Level %d has no default biome"), InOutMap.Levels[Index]);
			return false;
		}

		Result[Index] = *Biome;
	}

	InOutMap.Biomes = MoveTemp(Result);
	return true;
}

TArray<FColor> HexMapLoader::BuildHexData(const FHexMapData& Map, const TArray<const UHexBiomeConfig*>& Biomes)
{
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Map.Levels.Num());

	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const UHexBiomeConfig* Biome = Biomes[Map.Biomes[Index]];
		Pixels[Index].R = static_cast<uint8>(FMath::Clamp(Biome->TerrainLayer, 0, 255));
		Pixels[Index].G = static_cast<uint8>(FMath::Clamp(Map.Levels[Index] + 128, 0, 255));
		Pixels[Index].B = static_cast<uint8>(FMath::Clamp(Biome->MountainRockLayer, 0, 255));
		Pixels[Index].A = Map.Levels[Index] >= 0
			? static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32((Biome->SnowLine + 200.0f) * 0.5f), 0, 255))
			: Biome->bFrozen ? MAX_uint8 : static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Biome->WaterOpacity * 254.0f), 0, 254));
	}

	return Pixels;
}

bool HexMapLoader::ValidateBiomes(const TArray<const UHexBiomeConfig*>& Biomes, const TArray<const UHexLevelConfig*>& Levels, FString& OutError)
{
	if (Biomes.Num() > MAX_uint8 + 1)
	{
		OutError = FString::Printf(TEXT("There are %d biomes, but a map can only tell apart %d"), Biomes.Num(), MAX_uint8 + 1);
		return false;
	}

	TMap<uint32, const UHexBiomeConfig*> BiomeByColor;
	TMap<int32, const UHexBiomeConfig*> DefaultByLevel;

	for (const UHexBiomeConfig* Biome : Biomes)
	{
		if (const UHexBiomeConfig** Other = BiomeByColor.Find(GetColorKey(Biome->FileColor)))
		{
			OutError = FString::Printf(TEXT("Biomes %s and %s have the same file color %s"), *(*Other)->GetName(), *Biome->GetName(), *GetColorText(Biome->FileColor));
			return false;
		}

		BiomeByColor.Add(GetColorKey(Biome->FileColor), Biome);

		for (const int32 Level : Biome->DefaultForLevels)
		{
			if (const UHexBiomeConfig** Other = DefaultByLevel.Find(Level))
			{
				OutError = FString::Printf(TEXT("Biomes %s and %s are both the default of level %d"), *(*Other)->GetName(), *Biome->GetName(), Level);
				return false;
			}

			if (!Biome->AllowedLevels.Contains(Level))
			{
				OutError = FString::Printf(TEXT("Biome %s is the default of level %d, which it does not allow"), *Biome->GetName(), Level);
				return false;
			}

			DefaultByLevel.Add(Level, Biome);
		}
	}

	for (const UHexLevelConfig* Definition : Levels)
	{
		if (!DefaultByLevel.Contains(Definition->Level))
		{
			OutError = FString::Printf(TEXT("Level %d has no default biome"), Definition->Level);
			return false;
		}
	}

	return true;
}
