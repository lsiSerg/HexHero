// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetFile.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HexHero.h"
#include "HexLastView.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** Letters of the levels from -2 up */
	const TCHAR LevelLetters[] = {TEXT('D'), TEXT('S'), TEXT('P'), TEXT('H'), TEXT('M')};
	const int32 LowestLevel = -2;
}

TArray<FString> HexPlanetFile::GetRoots()
{
	return {FPaths::Combine(FPaths::ProjectContentDir(), RootFolder), FPaths::Combine(FPaths::ProjectSavedDir(), SavedFolder)};
}

FString HexPlanetFile::GetWritableRoot()
{
	return GetRoots()[WITH_EDITOR ? 0 : 1];
}

FString HexPlanetFile::GetFilePath(const FString& Folder, const TCHAR* File)
{
	for (const FString& Root : GetRoots())
	{
		const FString Directory = FPaths::Combine(Root, Folder);

		if (IFileManager::Get().DirectoryExists(*Directory))
		{
			return FPaths::Combine(Directory, File);
		}
	}

	return FPaths::Combine(GetWritableRoot(), Folder, File);
}

FString HexPlanetFile::SanitizeFolderName(const FString& Name)
{
	FString Folder;

	for (const TCHAR Char : Name.TrimStartAndEnd())
	{
		const bool bKept = FChar::IsAlnum(Char)
			|| Char == TEXT('-') || Char == TEXT('_');
		Folder.AppendChar(bKept ? Char : TEXT('_'));
	}

	return Folder.Replace(TEXT("_"), TEXT("")).IsEmpty() ? FString() : Folder;
}

bool HexPlanetFile::PlanetExists(const FString& Folder)
{
	for (const FString& Root : GetRoots())
	{
		if (IFileManager::Get().DirectoryExists(*FPaths::Combine(Root, Folder)))
		{
			return true;
		}
	}

	return false;
}

bool HexPlanetFile::SaveManifest(const FString& Folder, const FHexPlanetManifest& Manifest, FString& OutError)
{
	FHexPlanetManifest Written = Manifest;
	Written.Version = ManifestVersion;

	if (Folder.IsEmpty())
	{
		OutError = TEXT("the planet has no folder");
		return false;
	}

	if (Written.Frequency < MinFrequency || Written.Frequency > MaxFrequency)
	{
		OutError = FString::Printf(TEXT("frequency %d, which must be from %d to %d"), Written.Frequency, MinFrequency, MaxFrequency);
		return false;
	}

	FString Json;
	const FString FilePath = GetFilePath(Folder, ManifestFile);

	if (!FJsonObjectConverter::UStructToJsonObjectString(Written, Json) || !FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}

	UE_LOG(LogHexHero, Log, TEXT("Saved the planet %s to %s"), *Written.Name, *FilePath);
	return true;
}

bool HexPlanetFile::DeleteLevels(const FString& Folder)
{
	const FString FilePath = GetFilePath(Folder, LevelsFile);
	return !IFileManager::Get().FileExists(*FilePath) || IFileManager::Get().Delete(*FilePath);
}

namespace
{
	/** Planet made by the menu and not saved yet, which the level of the planet opens with NewPlanetOption */
	TOptional<FHexPlanetManifest> NewPlanet;
}

void HexPlanetFile::OpenNewPlanet(const UObject* WorldContext, const FHexPlanetManifest& Manifest)
{
	NewPlanet = Manifest;
	UGameplayStatics::OpenLevel(WorldContext, FName(PlanetLevel), true, NewPlanetOption);
}

bool HexPlanetFile::GetNewPlanet(const UWorld* World, FHexPlanetManifest& OutManifest)
{
	if (!World || !World->URL.HasOption(NewPlanetOption) || !NewPlanet.IsSet())
	{
		return false;
	}

	OutManifest = NewPlanet.GetValue();
	return true;
}

TArray<FHexPlanetEntry> HexPlanetFile::FindPlanets()
{
	// a folder in Content wins over one of the same name in Saved
	TArray<FString> Folders;

	for (const FString& Root : GetRoots())
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *FPaths::Combine(Root, TEXT("*")), false, true);

		for (const FString& Folder : Found)
		{
			if (!Folders.ContainsByPredicate([&Folder](const FString& Other) { return Other.Equals(Folder, ESearchCase::IgnoreCase); }))
			{
				Folders.Add(Folder);
			}
		}
	}

	TArray<FHexPlanetEntry> Planets;

	for (const FString& Folder : Folders)
	{
		FHexPlanetEntry Entry;
		Entry.Folder = Folder;
		FString Error;

		if (ReadManifest(Folder, Entry.Manifest, Error))
		{
			Planets.Add(MoveTemp(Entry));
		}
		else
		{
			UE_LOG(LogHexHero, Error, TEXT("Skipping a planet: %s"), *Error);
		}
	}

	Planets.StableSort([](const FHexPlanetEntry& A, const FHexPlanetEntry& B)
	{
		return A.Manifest.SortOrder != B.Manifest.SortOrder ? A.Manifest.SortOrder < B.Manifest.SortOrder : A.Folder < B.Folder;
	});

	return Planets;
}

bool HexPlanetFile::ParseManifest(const FString& Json, FHexPlanetManifest& OutManifest, FString& OutError)
{
	FHexPlanetManifest Manifest;

	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Manifest))
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	if (Manifest.Version != ManifestVersion)
	{
		OutError = FString::Printf(TEXT("version %d, expected %d"), Manifest.Version, ManifestVersion);
		return false;
	}

	if (Manifest.Frequency < MinFrequency || Manifest.Frequency > MaxFrequency)
	{
		OutError = FString::Printf(TEXT("frequency %d, which must be from %d to %d"), Manifest.Frequency, MinFrequency, MaxFrequency);
		return false;
	}

	OutManifest = MoveTemp(Manifest);
	return true;
}

bool HexPlanetFile::ReadManifest(const FString& Folder, FHexPlanetManifest& OutManifest, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, ManifestFile);
	FString Json;

	if (!FFileHelper::LoadFileToString(Json, *FilePath))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}

	if (!ParseManifest(Json, OutManifest, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *FilePath, *OutError);
		return false;
	}

	if (OutManifest.Name.IsEmpty())
	{
		OutManifest.Name = Folder;
	}

	return true;
}

TCHAR HexPlanetFile::GetLevelLetter(int32 Level)
{
	const int32 Index = Level - LowestLevel;
	return Index >= 0 && Index < UE_ARRAY_COUNT(LevelLetters) ? LevelLetters[Index] : TCHAR(0);
}

FString HexPlanetFile::EncodeLevels(TConstArrayView<int8> Levels)
{
	FString Text;
	Text.Reserve(Levels.Num() + Levels.Num() / LevelsLineLength + 2);

	for (int32 Cell = 0; Cell < Levels.Num(); ++Cell)
	{
		const TCHAR Letter = GetLevelLetter(Levels[Cell]);
		Text.AppendChar(Letter ? Letter : GetLevelLetter(0));

		if ((Cell + 1) % LevelsLineLength == 0 || Cell + 1 == Levels.Num())
		{
			Text.AppendChar(TEXT('\n'));
		}
	}

	return Text;
}

bool HexPlanetFile::DecodeLevels(const FString& Text, int32 CellCount, TArray<int8>& OutLevels, FString& OutError)
{
	TArray<int8> Levels;
	Levels.Reserve(CellCount);
	int32 Line = 1;

	for (const TCHAR Char : Text)
	{
		if (Char == TEXT('\n'))
		{
			++Line;
			continue;
		}

		if (FChar::IsWhitespace(Char))
		{
			continue;
		}

		int32 Index = 0;
		while (Index < UE_ARRAY_COUNT(LevelLetters) && FChar::ToUpper(Char) != LevelLetters[Index])
		{
			++Index;
		}

		if (Index == UE_ARRAY_COUNT(LevelLetters))
		{
			OutError = FString::Printf(TEXT("line %d has '%s', which is not a level (D, S, P, H or M)"), Line, *FString::Chr(Char));
			return false;
		}

		Levels.Add(int8(LowestLevel + Index));
	}

	if (Levels.Num() != CellCount)
	{
		OutError = FString::Printf(TEXT("%d cells, but the planet has %d"), Levels.Num(), CellCount);
		return false;
	}

	OutLevels = MoveTemp(Levels);
	return true;
}

bool HexPlanetFile::HasLevels(const FString& Folder)
{
	return IFileManager::Get().FileExists(*GetFilePath(Folder, LevelsFile));
}

bool HexPlanetFile::LoadLevels(const FString& Folder, int32 CellCount, TArray<int8>& OutLevels, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, LevelsFile);
	FString Text;

	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}

	if (!DecodeLevels(Text, CellCount, OutLevels, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *FilePath, *OutError);
		return false;
	}

	return true;
}

bool HexPlanetFile::LoadRelief(const FString& Folder, TArray<FHexReliefCell>& OutCells, FString& OutError)
{
	OutCells.Reset();
	const FString FilePath = GetFilePath(Folder, ReliefFile);
	if (!FPaths::FileExists(FilePath))
	{
		return true;
	}

	FString Text;
	FHexReliefFile File;
	if (!FFileHelper::LoadFileToString(Text, *FilePath) || !FJsonObjectConverter::JsonObjectStringToUStruct(Text, &File))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}
	OutCells = MoveTemp(File.Cells);
	return true;
}

bool HexPlanetFile::SaveRelief(const FString& Folder, const TArray<FHexReliefCell>& Cells, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, ReliefFile);
	if (Cells.IsEmpty())
	{
		if (FPaths::FileExists(FilePath) && !IFileManager::Get().Delete(*FilePath))
		{
			OutError = FString::Printf(TEXT("could not delete %s"), *FilePath);
			return false;
		}
		return true;
	}

	FHexReliefFile File;
	File.Cells = Cells;
	File.Cells.Sort([](const FHexReliefCell& A, const FHexReliefCell& B) { return A.Cell < B.Cell; });
	FString Text;
	if (!FJsonObjectConverter::UStructToJsonObjectString(File, Text) || !FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	return true;
}

bool HexPlanetFile::SaveLevels(const FString& Folder, TConstArrayView<int8> Levels, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, LevelsFile);

	if (!FFileHelper::SaveStringToFile(EncodeLevels(Levels), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}

	return true;
}

FString HexPlanetFile::EncodeBiomes(TConstArrayView<uint8> Biomes, TConstArrayView<TCHAR> Letters)
{
	FString Text;
	Text.Reserve(Biomes.Num() + Biomes.Num() / LevelsLineLength + 2);

	for (int32 Cell = 0; Cell < Biomes.Num(); ++Cell)
	{
		Text.AppendChar(Letters.IsValidIndex(Biomes[Cell]) ? Letters[Biomes[Cell]] : (Letters.IsEmpty() ? TEXT('?') : Letters[0]));

		if ((Cell + 1) % LevelsLineLength == 0 || Cell + 1 == Biomes.Num())
		{
			Text.AppendChar(TEXT('\n'));
		}
	}

	return Text;
}

bool HexPlanetFile::DecodeBiomes(const FString& Text, int32 CellCount, TConstArrayView<TCHAR> Letters, TArray<uint8>& OutBiomes, FString& OutError)
{
	TArray<uint8> Biomes;
	Biomes.Reserve(CellCount);
	int32 Line = 1;

	for (const TCHAR Char : Text)
	{
		if (Char == TEXT('\n'))
		{
			++Line;
			continue;
		}

		if (FChar::IsWhitespace(Char))
		{
			continue;
		}

		const int32 Index = Letters.IndexOfByPredicate([Char](TCHAR Letter) { return FChar::ToUpper(Letter) == FChar::ToUpper(Char); });

		if (Index == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("line %d has '%s', which is not the letter of a biome (%s)"), Line, *FString::Chr(Char),
				*FString::ConstructFromPtrSize(Letters.GetData(), Letters.Num()));
			return false;
		}

		Biomes.Add(uint8(Index));
	}

	if (Biomes.Num() != CellCount)
	{
		OutError = FString::Printf(TEXT("%d cells, but the planet has %d"), Biomes.Num(), CellCount);
		return false;
	}

	OutBiomes = MoveTemp(Biomes);
	return true;
}

bool HexPlanetFile::HasBiomes(const FString& Folder)
{
	return IFileManager::Get().FileExists(*GetFilePath(Folder, BiomesFile));
}

bool HexPlanetFile::LoadBiomes(const FString& Folder, int32 CellCount, TConstArrayView<TCHAR> Letters, TArray<uint8>& OutBiomes, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, BiomesFile);
	FString Text;

	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}

	if (!DecodeBiomes(Text, CellCount, Letters, OutBiomes, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *FilePath, *OutError);
		return false;
	}

	return true;
}

bool HexPlanetFile::SaveBiomes(const FString& Folder, TConstArrayView<uint8> Biomes, TConstArrayView<TCHAR> Letters, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, BiomesFile);

	if (!FFileHelper::SaveStringToFile(EncodeBiomes(Biomes, Letters), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}

	return true;
}

bool HexPlanetFile::DeleteBiomes(const FString& Folder)
{
	const FString FilePath = GetFilePath(Folder, BiomesFile);
	return !IFileManager::Get().FileExists(*FilePath) || IFileManager::Get().Delete(*FilePath);
}

FString HexPlanetFile::FindPlanetToOpen(const UWorld* World)
{
	const TArray<FHexPlanetEntry> Planets = FindPlanets();
	const auto Find = [&Planets](const FString& Folder)
	{
		const FHexPlanetEntry* Planet = Planets.FindByPredicate([&Folder](const FHexPlanetEntry& Entry) { return Entry.Folder.Equals(Folder, ESearchCase::IgnoreCase); });
		return Planet ? Planet->Folder : FString();
	};

	if (World && World->URL.HasOption(PlanetOption))
	{
		const FString Asked = World->URL.GetOption(*FString::Printf(TEXT("%s="), PlanetOption), TEXT(""));
		const FString Folder = Find(Asked);

		if (!Folder.IsEmpty())
		{
			return Folder;
		}

		UE_LOG(LogHexHero, Error, TEXT("There is no planet %s in %s"), *Asked, RootFolder);
	}

	FHexLastView View;
	if (HexLastView::Load(View) && !View.Planet.Planet.IsEmpty())
	{
		const FString Folder = Find(View.Planet.Planet);

		if (!Folder.IsEmpty())
		{
			return Folder;
		}
	}

	return Planets.IsEmpty() ? FString() : Planets[0].Folder;
}

void HexPlanetFile::OpenPlanet(const UObject* WorldContext, const FString& Folder)
{
	const FString Options = Folder.IsEmpty() ? FString() : FString::Printf(TEXT("%s=%s"), PlanetOption, *Folder);
	UGameplayStatics::OpenLevel(WorldContext, FName(PlanetLevel), true, Options);
}
