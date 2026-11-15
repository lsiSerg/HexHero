// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHeroConfig.h"
#include "HexHero.h"
#include "Engine/Texture2D.h"

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
	Knight.Portrait = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/UI/Portraits/T_Portrait_Rider.T_Portrait_Rider")));
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
	Wizard.Portrait = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/UI/Portraits/T_Portrait_Pilgrim.T_Portrait_Pilgrim")));
	Wizard.Attack = 0;
	Wizard.Defense = 0;
	Wizard.SpellPower = 2;
	Wizard.Knowledge = 3;
	Wizard.AttackChance = 10.0f;
	Wizard.DefenseChance = 10.0f;
	Wizard.SpellPowerChance = 40.0f;
	Wizard.KnowledgeChance = 40.0f;
	Wizard.Names = {TEXT("Veles"), TEXT("Lada"), TEXT("Svitozar"), TEXT("Mavka"), TEXT("Radomyr"), TEXT("Dzvenyslava"), TEXT("Lubomyr"), TEXT("Vesna")};

	// what each knows when it starts, and the skills its levels offer more often (in the order of EHexSkill: Sorcery, Diplomacy, Logistics,
	// Air, Earth, Fire, Water magic, Mysticism, Pathfinding), Claude's
	Knight.StartSpells = {TEXT("MagicArrow"), TEXT("Bless")};
	Knight.SkillWeights = {0.4f, 1.2f, 1.6f, 0.6f, 0.6f, 0.8f, 0.8f, 0.5f, 1.6f};
	Knight.StartSkill = EHexSkill::Logistics;
	Wizard.StartSpells = {TEXT("MagicArrow"), TEXT("Haste"), TEXT("Slow"), TEXT("Cure")};
	Wizard.SkillWeights = {1.4f, 0.6f, 0.6f, 1.3f, 1.3f, 1.3f, 1.3f, 1.4f, 0.6f};
	Wizard.StartSkill = EHexSkill::WaterMagic;

	return Classes;
}

const TCHAR* GetSkillLevelName(int32 Level)
{
	return Level >= 3 ? TEXT("Expert") : Level == 2 ? TEXT("Advanced") : Level == 1 ? TEXT("Basic") : TEXT("None");
}

TArray<FHexSkillInfo> MakeDefaultSkills()
{
	TArray<FHexSkillInfo> Skills;
	const auto Add = [&Skills](EHexSkill Skill, const TCHAR* Name, const TCHAR* Picture, const TCHAR* Description, TArray<float> Values, TArray<float> Second = {})
	{
		FHexSkillInfo& Info = Skills.AddDefaulted_GetRef();
		Info.Skill = Skill;
		Info.DisplayName = Name;
		Info.Description = Description;
		Info.Values = MoveTemp(Values);
		Info.SecondValues = MoveTemp(Second);
		const FString Texture = FString::Printf(TEXT("T_Skill%s"), Picture);
		Info.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(TEXT("/Game/HexPlanet/UI/Skills/%s.%s"), *Texture, *Texture)));
	};
	Add(EHexSkill::Sorcery, TEXT("Sorcery"), TEXT("Sorcery"), TEXT("Spells that deal damage deal {0}% more"), {0.05f, 0.1f, 0.15f});
	Add(EHexSkill::Diplomacy, TEXT("Diplomacy"), TEXT("Diplomacy"),
		TEXT("Neutral creatures much weaker than the army join without a fight by a chance of {0}%, else ask {1}% less to join"), {0.2f, 0.35f, 0.5f}, {0.25f, 0.5f, 0.75f});
	Add(EHexSkill::Logistics, TEXT("Logistics"), TEXT("Logistics"), TEXT("{0}% more points of movement on the map"), {0.1f, 0.2f, 0.3f});
	const TCHAR* SchoolText = TEXT("The spells of this school cost {0}% less mana and are {1}% stronger; at Expert many take every stack of a side");
	Add(EHexSkill::AirMagic, TEXT("Air magic"), TEXT("AirMagic"), SchoolText, {0.1f, 0.2f, 0.3f}, {0.15f, 0.3f, 0.5f});
	Add(EHexSkill::EarthMagic, TEXT("Earth magic"), TEXT("EarthMagic"), SchoolText, {0.1f, 0.2f, 0.3f}, {0.15f, 0.3f, 0.5f});
	Add(EHexSkill::FireMagic, TEXT("Fire magic"), TEXT("FireMagic"), SchoolText, {0.1f, 0.2f, 0.3f}, {0.15f, 0.3f, 0.5f});
	Add(EHexSkill::WaterMagic, TEXT("Water magic"), TEXT("WaterMagic"), SchoolText, {0.1f, 0.2f, 0.3f}, {0.15f, 0.3f, 0.5f});
	Add(EHexSkill::Mysticism, TEXT("Mysticism"), TEXT("Mysticism"), TEXT("{0} more mana back every day"), {2.0f, 3.0f, 4.0f});
	Add(EHexSkill::Pathfinding, TEXT("Pathfinding"), TEXT("Pathfinding"), TEXT("Rough ground (swamp, snow, sand, hills) costs {0}% less over plains"), {0.25f, 0.5f, 0.75f});
	return Skills;
}

const FHexSkillInfo* UHexHeroConfig::FindSkill(EHexSkill Skill) const
{
	return Skills.FindByPredicate([Skill](const FHexSkillInfo& Each) { return Each.Skill == Skill; });
}

int32 UHexHeroConfig::GetLevelExperience(int32 Level) const
{
	// each level needs LevelExperienceGrowth times what the one before it did
	double Total = 0.0;
	double Step = double(FMath::Max(LevelExperience, 1));
	for (int32 Each = 2; Each <= Level; ++Each)
	{
		Total += Step;
		Step *= double(FMath::Max(LevelExperienceGrowth, 1.0f));
	}
	return int32(FMath::Min(Total, 2.0e9));
}

TArray<FHexHeroPlayer> MakeDefaultHeroPlayers()
{
	return {
		{TEXT("Red"), FLinearColor(0.75f, 0.06f, 0.05f, 1.0f)},
		{TEXT("Blue"), FLinearColor(0.07f, 0.2f, 0.8f, 1.0f)},
		{TEXT("Green"), FLinearColor(0.1f, 0.5f, 0.1f, 1.0f)},
		{TEXT("Gold"), FLinearColor(0.85f, 0.6f, 0.08f, 1.0f)},
		// four more since 2026-11-14, as a map of zones has up to eight players
		{TEXT("Purple"), FLinearColor(0.42f, 0.1f, 0.62f, 1.0f)},
		{TEXT("Teal"), FLinearColor(0.05f, 0.5f, 0.5f, 1.0f)},
		{TEXT("Orange"), FLinearColor(0.9f, 0.35f, 0.05f, 1.0f)},
		{TEXT("Pink"), FLinearColor(0.85f, 0.3f, 0.55f, 1.0f)},
	};
}

UHexHeroConfig::UHexHeroConfig()
{
	Classes = MakeDefaultHeroClasses();
	Players = MakeDefaultHeroPlayers();
	Skills = MakeDefaultSkills();
	Artifacts = MakeDefaultArtifacts();
}

void UHexHeroConfig::PostLoad()
{
	Super::PostLoad();

	// the heroes count on a class and a player to fall back to
	if (Classes.IsEmpty())
	{
		Classes = MakeDefaultHeroClasses();
	}
	// a class saved before the spells and skills of 2026-11-15 takes those of the default class of its name
	const TArray<FHexHeroClass> DefaultClasses = MakeDefaultHeroClasses();
	for (FHexHeroClass& Class : Classes)
	{
		const FHexHeroClass* Default = DefaultClasses.FindByPredicate([&Class](const FHexHeroClass& Each) { return Each.Id == Class.Id; });
		if (Default && Class.StartSpells.IsEmpty() && Class.SkillWeights.IsEmpty() && Class.StartSkill == EHexSkill::Num)
		{
			Class.StartSpells = Default->StartSpells;
			Class.SkillWeights = Default->SkillWeights;
			Class.StartSkill = Default->StartSkill;
		}
		// and a portrait
		if (Default && Class.Portrait.IsNull())
		{
			Class.Portrait = Default->Portrait;
		}
	}
	if (Skills.IsEmpty())
	{
		Skills = MakeDefaultSkills();
	}
	// the artifacts of 2026-11-15
	if (Artifacts.IsEmpty() || ArtifactsVersion < 1)
	{
		Artifacts = MakeDefaultArtifacts();
		ArtifactsVersion = 1;
	}
	// an asset saved with fewer players takes the ones it lacks, as a map of zones has up to eight
	const TArray<FHexHeroPlayer> Defaults = MakeDefaultHeroPlayers();
	for (int32 Index = Players.Num(); Index < Defaults.Num(); ++Index)
	{
		Players.Add(Defaults[Index]);
	}
}

int32 UHexHeroConfig::FindArtifact(const FString& Id) const
{
	return Artifacts.IndexOfByPredicate([&Id](const FHexArtifact& Each) { return Each.Id.ToString().Equals(Id, ESearchCase::IgnoreCase); });
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
