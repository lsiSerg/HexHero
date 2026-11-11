// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexSiteConfig.h"
#include "HexHero.h"

namespace
{
	const TCHAR* SiteConfigPath = TEXT("/Game/HexPlanet/Sites/DA_HexSites.DA_HexSites");
}

TArray<FHexResourceInfo> MakeDefaultResources()
{
	return {
		// a player starts with 2 days of a gold mine, 4 of a basic one and 2 of a rare one
		{TEXT("Gold"), TEXT("Gold"), FLinearColor(1.0f, 0.72f, 0.1f, 1.0f), 2000},
		{TEXT("Wood"), TEXT("Wood"), FLinearColor(0.42f, 0.25f, 0.1f, 1.0f), 8},
		{TEXT("Ore"), TEXT("Ore"), FLinearColor(0.42f, 0.42f, 0.46f, 1.0f), 8},
		{TEXT("Mercury"), TEXT("Mercury"), FLinearColor(0.75f, 0.8f, 0.88f, 1.0f), 2},
		{TEXT("Sulfur"), TEXT("Sulfur"), FLinearColor(0.9f, 0.85f, 0.15f, 1.0f), 2},
		{TEXT("Crystal"), TEXT("Crystals"), FLinearColor(0.85f, 0.15f, 0.2f, 1.0f), 2},
		{TEXT("Gems"), TEXT("Gems"), FLinearColor(0.2f, 0.75f, 0.45f, 1.0f), 2},
	};
}

TArray<FHexSiteKind> MakeDefaultSiteKinds()
{
	TArray<FHexSiteKind> Kinds;
	const auto AddMine = [&Kinds](const TCHAR* Id, const TCHAR* Name, const TCHAR* Resource, int32 PerDay)
	{
		FHexSiteKind& Kind = Kinds.AddDefaulted_GetRef();
		Kind.Id = Id;
		Kind.DisplayName = Name;
		Kind.Type = EHexSiteType::Mine;
		Kind.Resource = Resource;
		Kind.PerDay = PerDay;
	};
	const auto AddPile = [&Kinds](const TCHAR* Id, const TCHAR* Name, const TCHAR* Resource, int32 Min, int32 Max)
	{
		FHexSiteKind& Kind = Kinds.AddDefaulted_GetRef();
		Kind.Id = Id;
		Kind.DisplayName = Name;
		Kind.Type = EHexSiteType::Pile;
		Kind.Resource = Resource;
		Kind.PerDay = 0;
		Kind.AmountMin = Min;
		Kind.AmountMax = Max;
	};
	// a mine for each resource: the gold mine gives 1000 a day, the basic ones 2, the rare ones 1
	AddMine(TEXT("GoldMine"), TEXT("Gold mine"), TEXT("Gold"), 1000);
	AddMine(TEXT("Sawmill"), TEXT("Sawmill"), TEXT("Wood"), 2);
	AddMine(TEXT("OreMine"), TEXT("Quarry"), TEXT("Ore"), 2);
	AddMine(TEXT("MercuryLab"), TEXT("Alchemist's lab"), TEXT("Mercury"), 1);
	AddMine(TEXT("SulfurMine"), TEXT("Sulfur mine"), TEXT("Sulfur"), 1);
	AddMine(TEXT("CrystalCave"), TEXT("Crystal cave"), TEXT("Crystal"), 1);
	AddMine(TEXT("GemMine"), TEXT("Gem mine"), TEXT("Gems"), 1);
	// a pile of any resource: 5 to 10 of a basic one, 3 to 6 of a rare one; the document gives no numbers for gold
	AddPile(TEXT("GoldPile"), TEXT("Pile of gold"), TEXT("Gold"), 500, 1000);
	AddPile(TEXT("WoodPile"), TEXT("Pile of wood"), TEXT("Wood"), 5, 10);
	AddPile(TEXT("OrePile"), TEXT("Pile of ore"), TEXT("Ore"), 5, 10);
	AddPile(TEXT("MercuryPile"), TEXT("Flasks of mercury"), TEXT("Mercury"), 3, 6);
	AddPile(TEXT("SulfurPile"), TEXT("Pile of sulfur"), TEXT("Sulfur"), 3, 6);
	AddPile(TEXT("CrystalPile"), TEXT("Pile of crystals"), TEXT("Crystal"), 3, 6);
	AddPile(TEXT("GemPile"), TEXT("Pile of gems"), TEXT("Gems"), 3, 6);
	// a mill, with a resource of its own
	FHexSiteKind& Mill = Kinds.AddDefaulted_GetRef();
	Mill.Id = TEXT("Mill");
	Mill.DisplayName = TEXT("Mill");
	Mill.Type = EHexSiteType::Mill;
	Mill.PerDay = 0;
	return Kinds;
}

TArray<FHexSiteShare> MakeDefaultSiteShares()
{
	// to a thousand cells a hero can stand on: about four mines, ten piles and a mill on the surface; the underground has no wood and no
	// mills but more of the rare resources, so going down pays
	const auto Make = [](const TCHAR* Kind, float PerThousand, float Underground, bool bNearStart = false)
	{
		FHexSiteShare Share;
		Share.Kind = Kind;
		Share.PerThousand = PerThousand;
		Share.UndergroundPerThousand = Underground;
		Share.bNearStart = bNearStart;
		return Share;
	};
	return {
		Make(TEXT("GoldMine"), 0.5f, 1.0f),
		Make(TEXT("Sawmill"), 1.0f, 0.0f, true),
		Make(TEXT("OreMine"), 1.0f, 1.0f, true),
		Make(TEXT("MercuryLab"), 0.4f, 0.8f),
		Make(TEXT("SulfurMine"), 0.4f, 0.8f),
		Make(TEXT("CrystalCave"), 0.4f, 0.8f),
		Make(TEXT("GemMine"), 0.4f, 0.8f),
		Make(TEXT("GoldPile"), 2.0f, 2.5f),
		Make(TEXT("WoodPile"), 2.0f, 0.0f),
		Make(TEXT("OrePile"), 2.0f, 2.5f),
		Make(TEXT("MercuryPile"), 1.0f, 2.0f),
		Make(TEXT("SulfurPile"), 1.0f, 2.0f),
		Make(TEXT("CrystalPile"), 1.0f, 2.0f),
		Make(TEXT("GemPile"), 1.0f, 2.0f),
		Make(TEXT("Mill"), 0.6f, 0.0f),
	};
}

UHexSiteConfig::UHexSiteConfig()
{
	Resources = MakeDefaultResources();
	Kinds = MakeDefaultSiteKinds();
	Generation.Shares = MakeDefaultSiteShares();
}

void UHexSiteConfig::PostLoad()
{
	Super::PostLoad();

	if (Resources.IsEmpty())
	{
		Resources = MakeDefaultResources();
	}
	if (Kinds.IsEmpty())
	{
		Kinds = MakeDefaultSiteKinds();
	}
	if (Generation.Shares.IsEmpty())
	{
		Generation.Shares = MakeDefaultSiteShares();
	}
}

int32 UHexSiteConfig::FindResource(FName Id) const
{
	return Resources.IndexOfByPredicate([Id](const FHexResourceInfo& Each) { return Each.Id == Id; });
}

int32 UHexSiteConfig::FindKind(const FString& Id) const
{
	return Kinds.IndexOfByPredicate([&Id](const FHexSiteKind& Each) { return Each.Id.ToString().Equals(Id, ESearchCase::IgnoreCase); });
}

int32 UHexSiteConfig::GetMineIncome(int32 Resource) const
{
	for (const FHexSiteKind& Kind : Kinds)
	{
		if (Kind.IsMine() && Resources.IsValidIndex(Resource) && Kind.Resource == Resources[Resource].Id)
		{
			return Kind.PerDay;
		}
	}
	return 0;
}

const UHexSiteConfig* UHexSiteConfig::Load()
{
	if (const UHexSiteConfig* Config = LoadObject<UHexSiteConfig>(nullptr, SiteConfigPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Config;
	}
	UE_LOG(LogHexHero, Warning, TEXT("Sites: %s is missing, taking the defaults of its class; run Tools/HexPlanetSites/import_planet_sites.py"), SiteConfigPath);
	return GetDefault<UHexSiteConfig>();
}
