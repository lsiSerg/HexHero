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
	// the gold mine of a pentagon, as the developer asked on 2026-11-15: 5000 a day (PentagonKind)
	AddMine(TEXT("RichGoldMine"), TEXT("Rich gold mine"), TEXT("Gold"), 5000);
	// a pile of any resource: 5 to 10 of a basic one, 3 to 6 of a rare one; the document gives no numbers for gold
	AddPile(TEXT("GoldPile"), TEXT("Pile of gold"), TEXT("Gold"), 500, 1000);
	AddPile(TEXT("WoodPile"), TEXT("Pile of wood"), TEXT("Wood"), 5, 10);
	AddPile(TEXT("OrePile"), TEXT("Pile of ore"), TEXT("Ore"), 5, 10);
	AddPile(TEXT("MercuryPile"), TEXT("Flasks of mercury"), TEXT("Mercury"), 3, 6);
	AddPile(TEXT("SulfurPile"), TEXT("Pile of sulfur"), TEXT("Sulfur"), 3, 6);
	AddPile(TEXT("CrystalPile"), TEXT("Pile of crystals"), TEXT("Crystal"), 3, 6);
	AddPile(TEXT("GemPile"), TEXT("Pile of gems"), TEXT("Gems"), 3, 6);
	// a chest of gold or experience, as the developer asked on 2026-11-15
	FHexSiteKind& Chest = Kinds.AddDefaulted_GetRef();
	Chest.Id = TEXT("Chest");
	Chest.DisplayName = TEXT("Treasure chest");
	Chest.Type = EHexSiteType::Chest;
	Chest.Resource = TEXT("Gold");
	Chest.PerDay = 0;
	Chest.AmountMin = 1000;
	Chest.AmountMax = 2000;
	// artifacts that lie on the map, as the developer asked on 2026-11-15, the great ones guarded
	const auto AddArtifact = [&Kinds](const TCHAR* Id, const TCHAR* Name, EHexArtifactRank Low, EHexArtifactRank High)
	{
		FHexSiteKind& Kind = Kinds.AddDefaulted_GetRef();
		Kind.Id = Id;
		Kind.DisplayName = Name;
		Kind.Type = EHexSiteType::Artifact;
		Kind.PerDay = 0;
		Kind.ArtifactRankMin = Low;
		Kind.ArtifactRankMax = High;
	};
	AddArtifact(TEXT("Artifact"), TEXT("Artifact"), EHexArtifactRank::Treasure, EHexArtifactRank::Minor);
	AddArtifact(TEXT("GreatArtifact"), TEXT("Great artifact"), EHexArtifactRank::Major, EHexArtifactRank::Relic);
	// a mill, with a resource of its own
	FHexSiteKind& Mill = Kinds.AddDefaulted_GetRef();
	Mill.Id = TEXT("Mill");
	Mill.DisplayName = TEXT("Mill");
	Mill.Type = EHexSiteType::Mill;
	Mill.PerDay = 0;
	// a shipyard and a boat, on the coasts
	FHexSiteKind& Shipyard = Kinds.AddDefaulted_GetRef();
	Shipyard.Id = TEXT("Shipyard");
	Shipyard.DisplayName = TEXT("Shipyard");
	Shipyard.Type = EHexSiteType::Shipyard;
	Shipyard.PerDay = 0;
	Shipyard.HeroPlace = FVector2D(0.55, 0.0);
	FHexSiteKind& Boat = Kinds.AddDefaulted_GetRef();
	Boat.Id = TEXT("Boat");
	Boat.DisplayName = TEXT("Boat");
	Boat.Type = EHexSiteType::Boat;
	Boat.PerDay = 0;
	// a wreck on the sea, a chest that holds an artifact more often, as the developer asked on 2026-11-15
	FHexSiteKind& Wreck = Kinds.AddDefaulted_GetRef();
	Wreck.Id = TEXT("Wreck");
	Wreck.DisplayName = TEXT("Shipwreck");
	Wreck.Type = EHexSiteType::Chest;
	Wreck.Resource = TEXT("Gold");
	Wreck.PerDay = 0;
	Wreck.AmountMin = 1000;
	Wreck.AmountMax = 2500;
	Wreck.bAtSea = true;
	Wreck.ArtifactShare = 0.4f;
	// what floats on the sea, taken as a pile by a hero in a boat whose way ends on it, as the developer asked for more on the water the same day
	const auto AddFlotsam = [&Kinds](const TCHAR* Id, const TCHAR* Name, const TCHAR* Resource, int32 Min, int32 Max)
	{
		FHexSiteKind& Kind = Kinds.AddDefaulted_GetRef();
		Kind.Id = Id;
		Kind.DisplayName = Name;
		Kind.Type = EHexSiteType::Pile;
		Kind.Resource = Resource;
		Kind.PerDay = 0;
		Kind.AmountMin = Min;
		Kind.AmountMax = Max;
		Kind.bAtSea = true;
	};
	AddFlotsam(TEXT("FlotsamWood"), TEXT("Floating timber"), TEXT("Wood"), 3, 6);
	AddFlotsam(TEXT("FlotsamGold"), TEXT("Floating cargo"), TEXT("Gold"), 300, 700);
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
		// the developer found the map too empty on 2026-11-15, so there are about twice as many piles as before, and chests
		Make(TEXT("GoldPile"), 3.5f, 4.0f),
		Make(TEXT("WoodPile"), 3.5f, 0.0f),
		Make(TEXT("OrePile"), 3.5f, 4.0f),
		Make(TEXT("MercuryPile"), 2.0f, 3.5f),
		Make(TEXT("SulfurPile"), 2.0f, 3.5f),
		Make(TEXT("CrystalPile"), 2.0f, 3.5f),
		Make(TEXT("GemPile"), 2.0f, 3.5f),
		// and more of them since the developer found the world empty again the same day
		Make(TEXT("Chest"), 6.0f, 5.0f),
		// the artifacts of 2026-11-15
		Make(TEXT("Artifact"), 1.4f, 1.8f),
		Make(TEXT("GreatArtifact"), 0.3f, 0.5f),
		Make(TEXT("Mill"), 0.6f, 0.0f),
	};
}

TArray<FHexSiteGuardRule> MakeDefaultSiteGuards()
{
	const auto Make = [](const TCHAR* Kind, float Share, int32 LevelMin, int32 LevelMax, float Weeks)
	{
		FHexSiteGuardRule Rule;
		Rule.Kind = Kind;
		Rule.Share = Share;
		Rule.LevelMin = LevelMin;
		Rule.LevelMax = LevelMax;
		Rule.Weeks = Weeks;
		return Rule;
	};
	return {
		Make(TEXT("GoldMine"), 1.0f, 2, 3, 1.0f),
		Make(TEXT("Sawmill"), 0.35f, 1, 1, 1.0f),
		Make(TEXT("OreMine"), 0.35f, 1, 1, 1.0f),
		Make(TEXT("MercuryLab"), 1.0f, 1, 2, 1.0f),
		Make(TEXT("SulfurMine"), 1.0f, 1, 2, 1.0f),
		Make(TEXT("CrystalCave"), 1.0f, 1, 2, 1.0f),
		Make(TEXT("GemMine"), 1.0f, 1, 2, 1.0f),
		Make(TEXT("Mill"), 0.5f, 1, 2, 0.75f),
		// an artifact now and then, a great one always, and strongly
		Make(TEXT("Artifact"), 0.4f, 1, 3, 1.0f),
		Make(TEXT("GreatArtifact"), 1.0f, 3, 5, 1.5f),
		// half of the wrecks, by the creatures of the sea
		[&Make]() { FHexSiteGuardRule Rule = Make(TEXT("Wreck"), 0.5f, 1, 4, 1.0f); Rule.Faction = TEXT("Deep"); return Rule; }(),
	};
}

UHexSiteConfig::UHexSiteConfig()
{
	Guards = MakeDefaultSiteGuards();
	Resources = MakeDefaultResources();
	Kinds = MakeDefaultSiteKinds();
	Generation.Shares = MakeDefaultSiteShares();
	BoatCost = {{TEXT("Gold"), 1000}, {TEXT("Wood"), 10}};
	PentagonGuard.Kind = TEXT("Pentagon");
	PentagonGuard.Share = 1.0f;
	PentagonGuard.LevelMin = 5;
	PentagonGuard.LevelMax = 7;
	PentagonGuard.Weeks = 2.0f;
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
	// the chests and the richer shares of 2026-11-15
	if (Generation.Shares.IsEmpty() || SitesVersion < 2)
	{
		Generation.Shares = MakeDefaultSiteShares();
	}
	if (SitesVersion < 2)
	{
		for (const FHexSiteKind& Default : MakeDefaultSiteKinds())
		{
			if (Default.Type == EHexSiteType::Chest && FindKind(Default.Id.ToString()) == INDEX_NONE)
			{
				// before the mill, so the palette lists it with the piles
				const int32 Mill = Kinds.IndexOfByPredicate([](const FHexSiteKind& Each) { return Each.Type == EHexSiteType::Mill; });
				Kinds.Insert(Default, Mill == INDEX_NONE ? Kinds.Num() : Mill);
			}
		}
		SitesVersion = 2;
	}
	// the artifacts, shipyards and boats of 2026-11-15, the guards of the artifacts and their shares
	if (SitesVersion < 3)
	{
		for (const FHexSiteKind& Default : MakeDefaultSiteKinds())
		{
			if ((Default.Type == EHexSiteType::Artifact || Default.Type == EHexSiteType::Shipyard || Default.Type == EHexSiteType::Boat) && FindKind(Default.Id.ToString()) == INDEX_NONE)
			{
				const int32 Mill = Kinds.IndexOfByPredicate([](const FHexSiteKind& Each) { return Each.Type == EHexSiteType::Mill; });
				Kinds.Insert(Default, Default.Type == EHexSiteType::Artifact && Mill != INDEX_NONE ? Mill : Kinds.Num());
			}
		}
		for (const FHexSiteShare& Default : MakeDefaultSiteShares())
		{
			if (!Generation.Shares.ContainsByPredicate([&Default](const FHexSiteShare& Each) { return Each.Kind == Default.Kind; }))
			{
				Generation.Shares.Add(Default);
			}
		}
		for (const FHexSiteGuardRule& Default : MakeDefaultSiteGuards())
		{
			if (!Guards.ContainsByPredicate([&Default](const FHexSiteGuardRule& Each) { return Each.Kind == Default.Kind; }))
			{
				Guards.Add(Default);
			}
		}
		SitesVersion = 3;
	}
	// the wrecks and their guards of the sea of 2026-11-15, and more chests while their share is as it was
	if (SitesVersion < 4)
	{
		for (FHexSiteShare& Share : Generation.Shares)
		{
			if (Share.Kind == TEXT("Chest") && FMath::IsNearlyEqual(Share.PerThousand, 2.5f) && FMath::IsNearlyEqual(Share.UndergroundPerThousand, 3.0f))
			{
				Share.PerThousand = 6.0f;
				Share.UndergroundPerThousand = 5.0f;
			}
		}
		for (const FHexSiteKind& Default : MakeDefaultSiteKinds())
		{
			if (Default.bAtSea && FindKind(Default.Id.ToString()) == INDEX_NONE)
			{
				Kinds.Add(Default);
			}
		}
		for (const FHexSiteGuardRule& Default : MakeDefaultSiteGuards())
		{
			if (!Guards.ContainsByPredicate([&Default](const FHexSiteGuardRule& Each) { return Each.Kind == Default.Kind; }))
			{
				Guards.Add(Default);
			}
		}
		SitesVersion = 4;
	}
	// what floats on the sea, of 2026-11-15
	if (SitesVersion < 6)
	{
		for (const FHexSiteKind& Default : MakeDefaultSiteKinds())
		{
			if (Default.bAtSea && FindKind(Default.Id.ToString()) == INDEX_NONE)
			{
				Kinds.Add(Default);
			}
		}
	}
	// the rich gold mine of the pentagons of 2026-11-15
	if (SitesVersion < 5)
	{
		for (const FHexSiteKind& Default : MakeDefaultSiteKinds())
		{
			if (Default.Id == TEXT("RichGoldMine") && FindKind(Default.Id.ToString()) == INDEX_NONE)
			{
				const int32 After = Kinds.IndexOfByPredicate([](const FHexSiteKind& Each) { return Each.Id == TEXT("GemMine"); });
				Kinds.Insert(Default, After == INDEX_NONE ? Kinds.Num() : After + 1);
			}
		}
		if (PentagonKind == TEXT("GoldMine"))
		{
			PentagonKind = TEXT("RichGoldMine");
		}
		SitesVersion = 5;
	}
	SitesVersion = FMath::Max(SitesVersion, 6);
	if (PentagonGuard.Kind.IsNone())
	{
		PentagonGuard.Kind = TEXT("Pentagon");
		PentagonGuard.Share = 1.0f;
		PentagonGuard.LevelMin = 5;
		PentagonGuard.LevelMax = 7;
		PentagonGuard.Weeks = 2.0f;
	}
	if (BoatCost.IsEmpty())
	{
		BoatCost = {{TEXT("Gold"), 1000}, {TEXT("Wood"), 10}};
	}
	if (Guards.IsEmpty())
	{
		Guards = MakeDefaultSiteGuards();
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
