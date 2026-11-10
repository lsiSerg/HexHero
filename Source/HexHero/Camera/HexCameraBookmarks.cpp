// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexCameraBookmarks.h"
#include "HexHero.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** Returns the bookmark in a slot of the block of a folder in a list of blocks, by the name of the folder whatever its case, or null */
	template <typename BlockType, typename BookmarkType>
	const BookmarkType* FindSlot(const TArray<BlockType>& Blocks, const FString& Folder, FString BlockType::*Name, int32 Slot)
	{
		const BlockType* Block = Blocks.FindByPredicate([&Folder, Name](const BlockType& Each) { return (Each.*Name).Equals(Folder, ESearchCase::IgnoreCase); });
		return Block ? Block->Slots.FindByPredicate([Slot](const BookmarkType& Each) { return Each.Slot == Slot; }) : nullptr;
	}

	/** Puts a bookmark into its slot of the block of a folder, which it adds when missing, the slots kept in their order */
	template <typename BlockType, typename BookmarkType>
	void SetSlot(TArray<BlockType>& Blocks, const FString& Folder, FString BlockType::*Name, const BookmarkType& Bookmark)
	{
		BlockType* Block = Blocks.FindByPredicate([&Folder, Name](const BlockType& Each) { return (Each.*Name).Equals(Folder, ESearchCase::IgnoreCase); });
		if (!Block)
		{
			Block = &Blocks.AddDefaulted_GetRef();
			(Block->*Name) = Folder;
		}
		Block->Slots.RemoveAll([&Bookmark](const BookmarkType& Each) { return Each.Slot == Bookmark.Slot; });
		Block->Slots.Add(Bookmark);
		Block->Slots.Sort([](const BookmarkType& A, const BookmarkType& B) { return A.Slot < B.Slot; });
	}
}

FString HexCameraBookmarks::GetFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CameraBookmarks.json"));
}

bool HexCameraBookmarks::Load(FHexCameraBookmarks& OutBookmarks)
{
	OutBookmarks = FHexCameraBookmarks();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetFilePath()))
	{
		return true;
	}
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutBookmarks))
	{
		UE_LOG(LogHexHero, Warning, TEXT("Ignoring %s, which is not valid"), *GetFilePath());
		OutBookmarks = FHexCameraBookmarks();
		return false;
	}
	return true;
}

bool HexCameraBookmarks::Save(const FHexCameraBookmarks& Bookmarks)
{
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Bookmarks, Json) || !FFileHelper::SaveStringToFile(Json, *GetFilePath()))
	{
		UE_LOG(LogHexHero, Warning, TEXT("Could not write %s"), *GetFilePath());
		return false;
	}
	return true;
}

int32 HexCameraBookmarks::GetSlotKey(int32 Slot)
{
	return Slot % NumSlots;
}

const FHexPlanetBookmark* HexCameraBookmarks::FindPlanet(const FHexCameraBookmarks& Bookmarks, const FString& Planet, int32 Slot)
{
	return FindSlot<FHexPlanetBookmarks, FHexPlanetBookmark>(Bookmarks.Planets, Planet, &FHexPlanetBookmarks::Planet, Slot);
}

const FHexMapBookmark* HexCameraBookmarks::FindMap(const FHexCameraBookmarks& Bookmarks, const FString& Map, int32 Slot)
{
	return FindSlot<FHexMapBookmarks, FHexMapBookmark>(Bookmarks.Maps, Map, &FHexMapBookmarks::Map, Slot);
}

void HexCameraBookmarks::SetPlanet(FHexCameraBookmarks& Bookmarks, const FString& Planet, const FHexPlanetBookmark& Bookmark)
{
	SetSlot(Bookmarks.Planets, Planet, &FHexPlanetBookmarks::Planet, Bookmark);
}

void HexCameraBookmarks::SetMap(FHexCameraBookmarks& Bookmarks, const FString& Map, const FHexMapBookmark& Bookmark)
{
	SetSlot(Bookmarks.Maps, Map, &FHexMapBookmarks::Map, Bookmark);
}
