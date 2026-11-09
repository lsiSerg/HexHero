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
	/** Letters of the levels of each layer */
	const FHexLevelLetter SurfaceLetters[] = {{TEXT('D'), -2}, {TEXT('S'), -1}, {TEXT('P'), 0}, {TEXT('H'), 1}, {TEXT('M'), 2}};
	const FHexLevelLetter UndergroundLetters[] = {{TEXT('V'), -2}, {TEXT('P'), 0}};

	/** Returns the letters of a table as "D, S, P, H or M" */
	FString GetLettersText(TConstArrayView<FHexLevelLetter> Letters)
	{
		FString Text;
		for (int32 Index = 0; Index < Letters.Num(); ++Index)
		{
			Text += Index == 0 ? TEXT("") : Index + 1 == Letters.Num() ? TEXT(" or ") : TEXT(", ");
			Text.AppendChar(Letters[Index].Letter);
		}
		return Text;
	}
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
	// the root is the one of the planet, as a data folder in it may not be there yet
	FString Planet = Folder;
	int32 Slash = INDEX_NONE;
	if (Planet.FindChar(TEXT('/'), Slash) || Planet.FindChar(TEXT('\\'), Slash))
	{
		Planet.LeftInline(Slash);
	}

	for (const FString& Root : GetRoots())
	{
		if (IFileManager::Get().DirectoryExists(*FPaths::Combine(Root, Planet)))
		{
			return FPaths::Combine(Root, Folder, File);
		}
	}

	return FPaths::Combine(GetWritableRoot(), Folder, File);
}

FString HexPlanetFile::GetDataFolder(const FString& PlanetFolder, EHexPlanetLayer Layer)
{
	return PlanetFolder.IsEmpty() || Layer == EHexPlanetLayer::Surface ? PlanetFolder : FString::Printf(TEXT("%s/%s"), *PlanetFolder, UndergroundFolder);
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

TConstArrayView<FHexLevelLetter> HexPlanetFile::GetLevelLetters(EHexPlanetLayer Layer)
{
	return Layer == EHexPlanetLayer::Underground ? TConstArrayView<FHexLevelLetter>(UndergroundLetters) : TConstArrayView<FHexLevelLetter>(SurfaceLetters);
}

TCHAR HexPlanetFile::GetLevelLetter(int32 Level, TConstArrayView<FHexLevelLetter> Letters)
{
	const FHexLevelLetter* Found = Letters.FindByPredicate([Level](const FHexLevelLetter& Each) { return Each.Level == Level; });
	return Found ? Found->Letter : TCHAR(0);
}

FString HexPlanetFile::EncodeLevels(TConstArrayView<int8> Levels, TConstArrayView<FHexLevelLetter> Letters)
{
	FString Text;
	Text.Reserve(Levels.Num() + Levels.Num() / LevelsLineLength + 2);

	// the letter of each level from the lowest up, a level without one taking the nearest, the lower of two as near
	TCHAR ByLevel[256] = {};
	for (int32 Level = -128; Level < 128 && !Letters.IsEmpty(); ++Level)
	{
		const FHexLevelLetter* Best = nullptr;
		for (const FHexLevelLetter& Each : Letters)
		{
			const int32 Distance = FMath::Abs(Each.Level - Level);
			if (!Best || Distance < FMath::Abs(Best->Level - Level) || (Distance == FMath::Abs(Best->Level - Level) && Each.Level < Best->Level))
			{
				Best = &Each;
			}
		}
		ByLevel[Level + 128] = Best->Letter;
	}

	for (int32 Cell = 0; Cell < Levels.Num(); ++Cell)
	{
		Text.AppendChar(Letters.IsEmpty() ? TEXT('?') : ByLevel[Levels[Cell] + 128]);

		if ((Cell + 1) % LevelsLineLength == 0 || Cell + 1 == Levels.Num())
		{
			Text.AppendChar(TEXT('\n'));
		}
	}

	return Text;
}

bool HexPlanetFile::DecodeLevels(const FString& Text, int32 CellCount, TArray<int8>& OutLevels, FString& OutError, TConstArrayView<FHexLevelLetter> Letters)
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

		const FHexLevelLetter* Found = Letters.FindByPredicate([Char](const FHexLevelLetter& Each) { return FChar::ToUpper(Char) == FChar::ToUpper(Each.Letter); });

		if (!Found)
		{
			OutError = FString::Printf(TEXT("line %d has '%s', which is not a level (%s)"), Line, *FString::Chr(Char), *GetLettersText(Letters));
			return false;
		}

		Levels.Add(Found->Level);
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

bool HexPlanetFile::LoadLevels(const FString& Folder, int32 CellCount, TArray<int8>& OutLevels, FString& OutError, TConstArrayView<FHexLevelLetter> Letters)
{
	const FString FilePath = GetFilePath(Folder, LevelsFile);
	FString Text;

	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}

	if (!DecodeLevels(Text, CellCount, OutLevels, OutError, Letters))
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

bool HexPlanetFile::HasObjects(const FString& Folder)
{
	return IFileManager::Get().FileExists(*GetFilePath(Folder, ObjectsFile));
}

bool HexPlanetFile::LoadObjects(const FString& Folder, TArray<FHexObjectCell>& OutCells, FString& OutError)
{
	OutCells.Reset();
	const FString FilePath = GetFilePath(Folder, ObjectsFile);
	FString Text;
	FHexObjectsFile File;
	if (!FFileHelper::LoadFileToString(Text, *FilePath) || !FJsonObjectConverter::JsonObjectStringToUStruct(Text, &File))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}
	OutCells = MoveTemp(File.Cells);
	return true;
}

bool HexPlanetFile::SaveObjects(const FString& Folder, const TArray<FHexObjectCell>& Cells, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, ObjectsFile);
	FHexObjectsFile File;
	File.Cells = Cells;
	File.Cells.Sort([](const FHexObjectCell& A, const FHexObjectCell& B) { return A.Cell < B.Cell; });

	// a line per cell with only what it has, so an edit changes only the lines of its cells and Git merges them
	FString Text = FString::Printf(TEXT("{\n\t\"version\": %d,\n\t\"cells\": ["), File.Version);
	for (int32 Index = 0; Index < File.Cells.Num(); ++Index)
	{
		const FHexObjectCell& Cell = File.Cells[Index];
		FString Line = FString::Printf(TEXT("{\"cell\": %d"), Cell.Cell);
		if (!Cell.Cover.IsNone())
		{
			Line += FString::Printf(TEXT(", \"cover\": \"%s\", \"density\": %d"), *Cell.Cover.ToString(), Cell.Density);
		}
		if (!Cell.Decor.IsNone())
		{
			Line += FString::Printf(TEXT(", \"decor\": \"%s\""), *Cell.Decor.ToString());
		}
		Text += FString::Printf(TEXT("%s\n\t\t%s}"), Index == 0 ? TEXT("") : TEXT(","), *Line);
	}
	Text += File.Cells.IsEmpty() ? TEXT("]\n}\n") : TEXT("\n\t]\n}\n");

	if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	return true;
}

bool HexPlanetFile::HasRivers(const FString& Folder)
{
	return IFileManager::Get().FileExists(*GetFilePath(Folder, RiversFile));
}

bool HexPlanetFile::LoadRivers(const FString& Folder, TArray<FHexRiverEdge>& OutEdges, FString& OutError)
{
	OutEdges.Reset();
	const FString FilePath = GetFilePath(Folder, RiversFile);
	FString Text;
	FHexRiversFile File;
	if (!FFileHelper::LoadFileToString(Text, *FilePath) || !FJsonObjectConverter::JsonObjectStringToUStruct(Text, &File))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}
	OutEdges = MoveTemp(File.Edges);
	return true;
}

bool HexPlanetFile::SaveRivers(const FString& Folder, const TArray<FHexRiverEdge>& Edges, FString& OutError)
{
	const FString FilePath = GetFilePath(Folder, RiversFile);
	TArray<FHexRiverEdge> Sorted = Edges;
	Sorted.Sort([](const FHexRiverEdge& A, const FHexRiverEdge& B) { return A.Cell != B.Cell ? A.Cell < B.Cell : A.Neighbor < B.Neighbor; });

	// a line per side, so an edit changes only the lines of its sides and Git merges them
	FString Text = TEXT("{\n\t\"version\": 1,\n\t\"edges\": [");
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		const FHexRiverEdge& Edge = Sorted[Index];
		Text += FString::Printf(TEXT("%s\n\t\t{\"cell\": %d, \"neighbor\": %d%s}"), Index == 0 ? TEXT("") : TEXT(","), Edge.Cell, Edge.Neighbor,
			Edge.Bridge ? TEXT(", \"bridge\": true") : TEXT(""));
	}
	Text += Sorted.IsEmpty() ? TEXT("]\n}\n") : TEXT("\n\t]\n}\n");

	if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	return true;
}

bool HexPlanetFile::SaveLevels(const FString& Folder, TConstArrayView<int8> Levels, FString& OutError, TConstArrayView<FHexLevelLetter> Letters)
{
	const FString FilePath = GetFilePath(Folder, LevelsFile);

	if (!FFileHelper::SaveStringToFile(EncodeLevels(Levels, Letters), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
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

TCHAR HexPlanetFile::GetNetworkLetter(int32 Network)
{
	return Network >= 0 && Network < MaxNetworks ? TCHAR(TEXT('A') + Network) : TEXT('?');
}

int32 HexPlanetFile::GetNetworkIndex(TCHAR Letter)
{
	const int32 Network = int32(FChar::ToUpper(Letter)) - int32(TEXT('A'));
	return Network >= 0 && Network < MaxNetworks ? Network : INDEX_NONE;
}

FString HexPlanetFile::EncodeEntrances(TConstArrayView<FHexEntrance> Entrances)
{
	TArray<FHexEntrance> Sorted;
	for (const FHexEntrance& Entrance : Entrances)
	{
		if (Entrance.Network >= 0 && Entrance.Network < MaxNetworks)
		{
			Sorted.Add(Entrance);
		}
	}
	Sorted.Sort([](const FHexEntrance& A, const FHexEntrance& B) { return A.Cell != B.Cell ? A.Cell < B.Cell : A.Network < B.Network; });

	// a line per entrance, so an edit changes only the lines of its entrances and Git merges them
	FString Text = TEXT("{\n\t\"version\": 1,\n\t\"entrances\": [");
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		Text += FString::Printf(TEXT("%s\n\t\t{\"cell\": %d, \"network\": \"%c\"}"), Index == 0 ? TEXT("") : TEXT(","), Sorted[Index].Cell, GetNetworkLetter(Sorted[Index].Network));
	}
	Text += Sorted.IsEmpty() ? TEXT("]\n}\n") : TEXT("\n\t]\n}\n");
	return Text;
}

bool HexPlanetFile::DecodeEntrances(const FString& Text, TArray<FHexEntrance>& OutEntrances, FString& OutError)
{
	FHexEntrancesFile File;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Text, &File))
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	OutEntrances.Reset();
	for (const FHexEntranceLine& Line : File.Entrances)
	{
		FHexEntrance& Entrance = OutEntrances.AddDefaulted_GetRef();
		Entrance.Cell = Line.Cell;
		Entrance.Network = Line.Network.Len() == 1 ? GetNetworkIndex(Line.Network[0]) : INDEX_NONE;
	}
	return true;
}

bool HexPlanetFile::HasEntrances(const FString& PlanetFolder)
{
	return !PlanetFolder.IsEmpty() && IFileManager::Get().FileExists(*GetFilePath(PlanetFolder, EntrancesFile));
}

bool HexPlanetFile::LoadEntrances(const FString& PlanetFolder, TArray<FHexEntrance>& OutEntrances, FString& OutError)
{
	const FString FilePath = GetFilePath(PlanetFolder, EntrancesFile);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *FilePath);
		return false;
	}
	if (!DecodeEntrances(Text, OutEntrances, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *FilePath, *OutError);
		return false;
	}
	return true;
}

bool HexPlanetFile::SaveEntrances(const FString& PlanetFolder, TConstArrayView<FHexEntrance> Entrances, FString& OutError)
{
	const FString FilePath = GetFilePath(PlanetFolder, EntrancesFile);
	if (PlanetFolder.IsEmpty() || !FFileHelper::SaveStringToFile(EncodeEntrances(Entrances), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	return true;
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
