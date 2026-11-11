// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHeroConfig.h"
#include "HexHero.h"

namespace
{
	const TCHAR* HeroConfigPath = TEXT("/Game/HexPlanet/Heroes/DA_HexHeroes.DA_HexHeroes");
}

TArray<FHexHeroClass> MakeDefaultHeroClasses()
{
	TArray<FHexHeroClass> Classes;

	// a warrior, whose levels mostly raise Attack and Defense
	FHexHeroClass& Knight = Classes.AddDefaulted_GetRef();
	Knight.Id = TEXT("Knight");
	Knight.DisplayName = TEXT("Knight");
	Knight.Attack = 2;
	Knight.Defense = 2;
	Knight.SpellPower = 1;
	Knight.Knowledge = 1;
	Knight.AttackChance = 35.0f;
	Knight.DefenseChance = 45.0f;
	Knight.SpellPowerChance = 10.0f;
	Knight.KnowledgeChance = 10.0f;
	Knight.Names = {TEXT("Bohdan"), TEXT("Yaroslava"), TEXT("Ostap"), TEXT("Myroslava"), TEXT("Taras"), TEXT("Zoryana"), TEXT("Danylo"), TEXT("Solomiya")};

	// a mage, whose levels mostly raise Spell Power and Knowledge
	FHexHeroClass& Wizard = Classes.AddDefaulted_GetRef();
	Wizard.Id = TEXT("Wizard");
	Wizard.DisplayName = TEXT("Wizard");
	Wizard.Attack = 0;
	Wizard.Defense = 0;
	Wizard.SpellPower = 2;
	Wizard.Knowledge = 3;
	Wizard.AttackChance = 10.0f;
	Wizard.DefenseChance = 10.0f;
	Wizard.SpellPowerChance = 40.0f;
	Wizard.KnowledgeChance = 40.0f;
	Wizard.Names = {TEXT("Veles"), TEXT("Lada"), TEXT("Svitozar"), TEXT("Mavka"), TEXT("Radomyr"), TEXT("Dzvenyslava"), TEXT("Lubomyr"), TEXT("Vesna")};

	return Classes;
}

TArray<FHexHeroPlayer> MakeDefaultHeroPlayers()
{
	return {
		{TEXT("Red"), FLinearColor(0.75f, 0.06f, 0.05f, 1.0f)},
		{TEXT("Blue"), FLinearColor(0.07f, 0.2f, 0.8f, 1.0f)},
		{TEXT("Green"), FLinearColor(0.1f, 0.5f, 0.1f, 1.0f)},
		{TEXT("Gold"), FLinearColor(0.85f, 0.6f, 0.08f, 1.0f)},
	};
}

UHexHeroConfig::UHexHeroConfig()
{
	Classes = MakeDefaultHeroClasses();
	Players = MakeDefaultHeroPlayers();
}

void UHexHeroConfig::PostLoad()
{
	Super::PostLoad();

	// the heroes count on a class and a player to fall back to
	if (Classes.IsEmpty())
	{
		Classes = MakeDefaultHeroClasses();
	}
	if (Players.IsEmpty())
	{
		Players = MakeDefaultHeroPlayers();
	}
}

int32 UHexHeroConfig::FindClass(const FString& Id) const
{
	return Classes.IndexOfByPredicate([&Id](const FHexHeroClass& Each) { return Each.Id.ToString().Equals(Id, ESearchCase::IgnoreCase); });
}

const UHexHeroConfig* UHexHeroConfig::Load()
{
	if (const UHexHeroConfig* Config = LoadObject<UHexHeroConfig>(nullptr, HeroConfigPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Config;
	}
	UE_LOG(LogHexHero, Warning, TEXT("Heroes: %s is missing, taking the defaults of its class; run Tools/HexPlanetHeroes/import_planet_heroes.py"), HeroConfigPath);
	return GetDefault<UHexHeroConfig>();
}
