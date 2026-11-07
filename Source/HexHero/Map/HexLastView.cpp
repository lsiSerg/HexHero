// Copyright Epic Games, Inc. All Rights Reserved.


#include "HexLastView.h"
#include "HexHero.h"
#include "HexMapManifest.h"
#include "HexMapLoader.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString HexLastView::GetFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LastView.json"));
}

bool HexLastView::Parse(const FString& Json, FHexLastView& OutView)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return false;
	}

	FHexLastView View;

	if (Object->HasTypedField<EJson::Object>(TEXT("flatMap")) || Object->HasTypedField<EJson::Object>(TEXT("planet")))
	{
		if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &View))
		{
			return false;
		}
	}
	else
	{
		// the flat layout of before the blocks, where the planet was a folder and its camera had the prefix planet
		View.bPlanetOpen = Object->GetBoolField(TEXT("bPlanet"));
		Object->TryGetStringField(TEXT("map"), View.FlatMap.Map);
		Object->TryGetNumberField(TEXT("x"), View.FlatMap.X);
		Object->TryGetNumberField(TEXT("y"), View.FlatMap.Y);
		Object->TryGetNumberField(TEXT("yaw"), View.FlatMap.Yaw);
		Object->TryGetNumberField(TEXT("zoom"), View.FlatMap.Zoom);
		Object->TryGetBoolField(TEXT("bFreeCamera"), View.FlatMap.bFreeCamera);
		Object->TryGetStringField(TEXT("planet"), View.Planet.Planet);
		Object->TryGetNumberField(TEXT("planetLatitude"), View.Planet.Latitude);
		Object->TryGetNumberField(TEXT("planetLongitude"), View.Planet.Longitude);
		Object->TryGetNumberField(TEXT("planetHeading"), View.Planet.Heading);
		Object->TryGetNumberField(TEXT("planetDistance"), View.Planet.Distance);
	}

	if (View.FlatMap.Map.IsEmpty() && !View.bPlanetOpen && View.Planet.Planet.IsEmpty() && View.Planet.Distance <= 0.0)
	{
		return false;
	}

	OutView = MoveTemp(View);
	return true;
}

bool HexLastView::Load(FHexLastView& OutView)
{
	FString Json;

	if (!FFileHelper::LoadFileToString(Json, *GetFilePath()))
	{
		return false;
	}

	if (!Parse(Json, OutView))
	{
		UE_LOG(LogHexHero, Warning, TEXT("Ignoring %s, which is not valid"), *GetFilePath());
		return false;
	}

	return true;
}

bool HexLastView::Save(const FHexLastView& View)
{
	FString Json;

	if (!FJsonObjectConverter::UStructToJsonObjectString(View, Json) || !FFileHelper::SaveStringToFile(Json, *GetFilePath()))
	{
		UE_LOG(LogHexHero, Warning, TEXT("Could not write %s"), *GetFilePath());
		return false;
	}

	return true;
}

FString HexLastView::FindLastMap(const TArray<FHexMapEntry>& Maps)
{
	FHexLastView View;

	if (!Load(View))
	{
		return FString();
	}

	const FHexMapEntry* Map = Maps.FindByPredicate([&View](const FHexMapEntry& Entry) { return Entry.Folder.Equals(View.FlatMap.Map, ESearchCase::IgnoreCase); });
	return Map ? Map->Folder : FString();
}

void HexLastView::SetPlanetOpen(bool bPlanet)
{
	FHexLastView View;
	Load(View);

	if (View.bPlanetOpen != bPlanet)
	{
		View.bPlanetOpen = bPlanet;
		Save(View);
	}
}

bool HexLastView::ShouldOpenPlanet(const UWorld* World)
{
	FHexLastView View;
	return World && !World->URL.HasOption(HexMapLoader::MapOption) && Load(View) && View.bPlanetOpen;
}
