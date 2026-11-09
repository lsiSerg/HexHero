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

namespace
{
	/** A member of a JSON object in its text: its key, where its key starts, where the text after its colon starts, and where its value starts
	 *  and ends */
	struct FJsonTextMember
	{
		FString Key;
		int32 KeyStart = 0;
		int32 AfterColon = 0;
		int32 ValueStart = 0;
		int32 ValueEnd = 0;
	};

	/** Returns the index after the JSON string whose opening quote is at Index, or INDEX_NONE */
	int32 SkipJsonString(const FString& Text, int32 Index)
	{
		for (int32 At = Index + 1; At < Text.Len(); ++At)
		{
			if (Text[At] == TEXT('\\'))
			{
				++At;
			}
			else if (Text[At] == TEXT('"'))
			{
				return At + 1;
			}
		}
		return INDEX_NONE;
	}

	/** Returns the index after the JSON value that starts at Index, or INDEX_NONE */
	int32 SkipJsonValue(const FString& Text, int32 Index)
	{
		if (Index >= Text.Len())
		{
			return INDEX_NONE;
		}
		if (Text[Index] == TEXT('"'))
		{
			return SkipJsonString(Text, Index);
		}
		if (Text[Index] == TEXT('{') || Text[Index] == TEXT('['))
		{
			int32 Depth = 0;
			for (int32 At = Index; At < Text.Len(); ++At)
			{
				const TCHAR Char = Text[At];
				if (Char == TEXT('"'))
				{
					At = SkipJsonString(Text, At);
					if (At == INDEX_NONE)
					{
						return INDEX_NONE;
					}
					--At;
				}
				else if (Char == TEXT('{') || Char == TEXT('['))
				{
					++Depth;
				}
				else if ((Char == TEXT('}') || Char == TEXT(']')) && --Depth == 0)
				{
					return At + 1;
				}
			}
			return INDEX_NONE;
		}
		int32 At = Index;
		while (At < Text.Len() && !FChar::IsWhitespace(Text[At]) && Text[At] != TEXT(',') && Text[At] != TEXT('}') && Text[At] != TEXT(']'))
		{
			++At;
		}
		return At > Index ? At : INDEX_NONE;
	}

	/** Reads the members of the JSON object whose opening brace is at Open, and where its closing brace lies. Returns false if it cannot */
	bool ReadJsonMembers(const FString& Text, int32 Open, TArray<FJsonTextMember>& OutMembers, int32& OutClose)
	{
		int32 At = Open + 1;
		const auto SkipSpace = [&Text, &At]()
		{
			while (At < Text.Len() && FChar::IsWhitespace(Text[At]))
			{
				++At;
			}
		};
		SkipSpace();
		if (At < Text.Len() && Text[At] == TEXT('}'))
		{
			OutClose = At;
			return true;
		}
		while (At < Text.Len() && Text[At] == TEXT('"'))
		{
			FJsonTextMember& Member = OutMembers.AddDefaulted_GetRef();
			Member.KeyStart = At;
			const int32 KeyEnd = SkipJsonString(Text, At);
			if (KeyEnd == INDEX_NONE)
			{
				return false;
			}
			Member.Key = Text.Mid(At + 1, KeyEnd - At - 2);
			At = KeyEnd;
			SkipSpace();
			if (At >= Text.Len() || Text[At] != TEXT(':'))
			{
				return false;
			}
			Member.AfterColon = ++At;
			SkipSpace();
			Member.ValueStart = At;
			Member.ValueEnd = SkipJsonValue(Text, At);
			if (Member.ValueEnd == INDEX_NONE)
			{
				return false;
			}
			At = Member.ValueEnd;
			SkipSpace();
			if (At < Text.Len() && Text[At] == TEXT('}'))
			{
				OutClose = At;
				return true;
			}
			if (At >= Text.Len() || Text[At] != TEXT(','))
			{
				return false;
			}
			++At;
			SkipSpace();
		}
		return false;
	}

	/** Returns the whitespace from the start of the line of Index up to it */
	FString GetLineIndent(const FString& Text, int32 Index)
	{
		int32 Start = Index;
		while (Start > 0 && Text[Start - 1] != TEXT('\n'))
		{
			--Start;
		}
		const FString Indent = Text.Mid(Start, Index - Start);
		for (const TCHAR Char : Indent)
		{
			if (!FChar::IsWhitespace(Char))
			{
				return FString();
			}
		}
		return Indent;
	}

	/** Returns the member of a key, which FJsonObjectConverter reads whatever its case */
	const FJsonTextMember* FindJsonMember(const TArray<FJsonTextMember>& Members, const TCHAR* Key)
	{
		return Members.FindByPredicate([Key](const FJsonTextMember& Each) { return Each.Key.Equals(Key, ESearchCase::IgnoreCase); });
	}
}

bool HexPlanetFile::SetSeedOffsetText(const FString& Json, int32 SeedOffset, FString& OutJson)
{
	int32 Open = 0;
	while (Open < Json.Len() && Json[Open] != TEXT('{'))
	{
		if (!FChar::IsWhitespace(Json[Open]) && Json[Open] != TEXT('\xFEFF'))
		{
			return false;
		}
		++Open;
	}
	TArray<FJsonTextMember> Members;
	int32 Close = 0;
	if (Open >= Json.Len() || !ReadJsonMembers(Json, Open, Members, Close) || Members.IsEmpty())
	{
		return false;
	}

	const FString NewLine = Json.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	const FString Unit = GetLineIndent(Json, Members[0].KeyStart);
	const FString Number = FString::FromInt(SeedOffset);
	// the separators after the colons of the file, before a number and before an object
	const FJsonTextMember* Scalar = Members.FindByPredicate([&Json](const FJsonTextMember& Each) { return Json[Each.ValueStart] != TEXT('{') && Json[Each.ValueStart] != TEXT('['); });
	const FJsonTextMember* Object = Members.FindByPredicate([&Json](const FJsonTextMember& Each) { return Json[Each.ValueStart] == TEXT('{'); });
	const FString ScalarSeparator = Scalar ? Json.Mid(Scalar->AfterColon, Scalar->ValueStart - Scalar->AfterColon) : FString(TEXT(" "));
	const FString ObjectSeparator = Object ? Json.Mid(Object->AfterColon, Object->ValueStart - Object->AfterColon) : ScalarSeparator;

	const FJsonTextMember* Underground = FindJsonMember(Members, TEXT("underground"));
	if (!Underground)
	{
		if (SeedOffset == 0)
		{
			OutJson = Json;
			return true;
		}
		// the block after the last member, indented as the members of the file are
		const FString Block = TEXT(",") + NewLine + Unit + TEXT("\"underground\":") + ObjectSeparator + TEXT("{") + NewLine + Unit + Unit
			+ TEXT("\"seedOffset\":") + ScalarSeparator + Number + NewLine + Unit + TEXT("}");
		OutJson = Json.Left(Members.Last().ValueEnd) + Block + Json.Mid(Members.Last().ValueEnd);
		return true;
	}
	if (Json[Underground->ValueStart] != TEXT('{'))
	{
		return false;
	}

	TArray<FJsonTextMember> Inner;
	int32 InnerClose = 0;
	if (!ReadJsonMembers(Json, Underground->ValueStart, Inner, InnerClose))
	{
		return false;
	}
	if (const FJsonTextMember* Offset = FindJsonMember(Inner, TEXT("seedOffset")))
	{
		OutJson = Json.Left(Offset->ValueStart) + Number + Json.Mid(Offset->ValueEnd);
		return true;
	}
	if (SeedOffset == 0)
	{
		OutJson = Json;
		return true;
	}
	const FString OuterIndent = GetLineIndent(Json, Underground->KeyStart);
	if (Inner.IsEmpty())
	{
		OutJson = Json.Left(Underground->ValueStart + 1) + NewLine + OuterIndent + Unit + TEXT("\"seedOffset\":") + ScalarSeparator + Number + NewLine
			+ OuterIndent + Json.Mid(InnerClose);
		return true;
	}
	const FString Member = TEXT(",") + NewLine + GetLineIndent(Json, Inner.Last().KeyStart) + TEXT("\"seedOffset\":") + ScalarSeparator + Number;
	OutJson = Json.Left(Inner.Last().ValueEnd) + Member + Json.Mid(Inner.Last().ValueEnd);
	return true;
}

bool HexPlanetFile::SaveSeedOffset(const FString& Folder, const FHexPlanetManifest& Manifest, FString& OutError)
{
	const FString FilePath = Folder.IsEmpty() ? FString() : GetFilePath(Folder, ManifestFile);
	FString Old;
	if (!FilePath.IsEmpty() && FFileHelper::LoadFileToString(Old, *FilePath))
	{
		// only when the file is the manifest but for the offset, and the edit changed the offset alone
		FString New;
		FString Error;
		FHexPlanetManifest Before;
		FHexPlanetManifest After;
		if (SetSeedOffsetText(Old, Manifest.Underground.SeedOffset, New) && ParseManifest(Old, Before, Error) && ParseManifest(New, After, Error))
		{
			Before.Underground.SeedOffset = Manifest.Underground.SeedOffset;
			FHexPlanetManifest Expected = Manifest;
			Expected.Version = ManifestVersion;
			if (Before.Name.IsEmpty())
			{
				Before.Name = Expected.Name;
				After.Name = Expected.Name;
			}
			FString BeforeJson;
			FString AfterJson;
			FString ExpectedJson;
			if (FJsonObjectConverter::UStructToJsonObjectString(Before, BeforeJson) && FJsonObjectConverter::UStructToJsonObjectString(After, AfterJson)
				&& FJsonObjectConverter::UStructToJsonObjectString(Expected, ExpectedJson) && BeforeJson.Equals(AfterJson, ESearchCase::CaseSensitive)
				&& BeforeJson.Equals(ExpectedJson, ESearchCase::CaseSensitive))
			{
				if (New.Equals(Old, ESearchCase::CaseSensitive))
				{
					UE_LOG(LogHexHero, Log, TEXT("The seed offset %d of the underground of %s is in %s already"), Manifest.Underground.SeedOffset, *Manifest.Name, *FilePath);
					return true;
				}
				if (!FFileHelper::SaveStringToFile(New, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
				{
					OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
					return false;
				}
				UE_LOG(LogHexHero, Log, TEXT("Saved the seed offset %d of the underground of %s to %s"), Manifest.Underground.SeedOffset, *Manifest.Name, *FilePath);
				return true;
			}
		}
		UE_LOG(LogHexHero, Warning, TEXT("Could not change the seed offset alone in %s, so the whole manifest is written"), *FilePath);
	}
	return SaveManifest(Folder, Manifest, OutError);
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
