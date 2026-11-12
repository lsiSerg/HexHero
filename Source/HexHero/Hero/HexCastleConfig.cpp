// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexCastleConfig.h"
#include "HexHero.h"

namespace
{
	const TCHAR* CastleConfigPath = TEXT("/Game/HexPlanet/Castles/DA_HexCastles.DA_HexCastles");
	const TCHAR* CastleMeshFolder = TEXT("/Game/HexPlanet/Castles/");

	// what a creature of each level is before its faction and its role change it; Claude's numbers, to tune in DA_HexCastles
	const int32 LevelHealth[NumCreatureLevels] = {8, 14, 24, 38, 60, 100, 180};
	const int32 LevelDamage[NumCreatureLevels] = {2, 3, 5, 8, 12, 20, 40};
	const int32 LevelAttack[NumCreatureLevels] = {3, 5, 7, 10, 12, 15, 20};
	const int32 LevelDefense[NumCreatureLevels] = {3, 4, 7, 10, 11, 15, 20};
	const int32 LevelGrowth[NumCreatureLevels] = {12, 8, 6, 4, 3, 2, 1};
	const int32 LevelGold[NumCreatureLevels] = {50, 90, 170, 280, 450, 900, 2400};
	const int32 DwellingGold[NumCreatureLevels] = {400, 800, 1200, 2000, 3000, 4500, 8000};
	const int32 DwellingFort[NumCreatureLevels] = {0, 0, 1, 1, 2, 2, 3};

	TSoftObjectPtr<UStaticMesh> MeshOf(const FString& Name)
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("%sSM_PlanetCastle%s.SM_PlanetCastle%s"), CastleMeshFolder, *Name, *Name)));
	}

	/** What tells the factions apart in numbers: the Order of Dawn has better and dearer creatures, the Realm of Decay more and cheaper ones */
	struct FFactionMake
	{
		float Quality = 1.0f;
		float Price = 1.0f;
		float LowGrowth = 1.0f;
		FName Rare;
		FName Rarest;
	};

	FHexCreature MakeCreature(const FFactionMake& Make, const TCHAR* Id, const TCHAR* Name, const TCHAR* UpgradedName, int32 Level, float Tough, float Hit, int32 Speed,
		bool bShoots = false, bool bFlies = false)
	{
		FHexCreature Creature;
		Creature.Id = Id;
		Creature.bShoots = bShoots;
		Creature.bFlies = bFlies;
		Creature.Growth = FMath::Max(FMath::RoundToInt32(LevelGrowth[Level] * (Level < 2 ? Make.LowGrowth : 1.0f)), 1);
		const float Damage = LevelDamage[Level] * Hit * Make.Quality;
		FHexCreatureStats& Base = Creature.Base;
		Base.DisplayName = Name;
		Base.Health = FMath::Max(FMath::RoundToInt32(LevelHealth[Level] * Tough * Make.Quality), 1);
		Base.DamageMin = FMath::Max(FMath::RoundToInt32(Damage * 0.75f), 1);
		Base.DamageMax = FMath::Max(FMath::RoundToInt32(Damage * 1.25f), Base.DamageMin);
		Base.Attack = FMath::RoundToInt32(LevelAttack[Level] * Hit * Make.Quality);
		Base.Defense = FMath::RoundToInt32(LevelDefense[Level] * Tough * Make.Quality);
		Base.Speed = Speed;
		Base.Gold = FMath::RoundToInt32(LevelGold[Level] * Make.Price / 5.0f) * 5;
		// the upgraded one is a little better at everything, and faster
		FHexCreatureStats& Upgraded = Creature.Upgraded;
		Upgraded = Base;
		Upgraded.DisplayName = UpgradedName;
		Upgraded.Health = FMath::RoundToInt32(Base.Health * 1.1f);
		Upgraded.DamageMin = FMath::Max(FMath::RoundToInt32(Damage * 0.9f), Base.DamageMin);
		Upgraded.DamageMax = FMath::Max(FMath::RoundToInt32(Damage * 1.4f), Base.DamageMax);
		Upgraded.Attack = Base.Attack + 1 + Level / 3;
		Upgraded.Defense = Base.Defense + 1 + Level / 4;
		Upgraded.Speed = Speed + (bFlies ? 2 : 1);
		Upgraded.Gold = FMath::RoundToInt32(Base.Gold * 1.25f / 5.0f) * 5;
		if (Level == NumCreatureLevels - 1)
		{
			Creature.CostResource = Make.Rarest;
			Creature.CostResourceAmount = 1;
		}
		return Creature;
	}

	/** The bits of abilities */
	int32 Bits(std::initializer_list<EHexAbility> Abilities)
	{
		int32 Mask = 0;
		for (const EHexAbility Ability : Abilities)
		{
			Mask |= 1 << int32(Ability);
		}
		return Mask;
	}

	/** What a creature can do in a battle, by its role, as the developer chose the roles on 2026-11-12 after references of the genre: the
	 *  abilities of its basic version and what its upgraded one has more, the shots of one that shoots (twice as many upgraded), its own
	 *  morale and the luck of its upgraded version; Claude's choice */
	FHexCreature With(FHexCreature Creature, int32 BaseAbilities, int32 UpgradedAbilities, int32 Shots = 0, int32 Morale = 0, int32 UpgradedLuck = 0)
	{
		Creature.Base.Abilities = BaseAbilities;
		Creature.Upgraded.Abilities = BaseAbilities | UpgradedAbilities;
		Creature.Base.Shots = Shots;
		Creature.Upgraded.Shots = Shots * 2;
		Creature.Base.Morale = Creature.Upgraded.Morale = Morale;
		Creature.Upgraded.Luck = UpgradedLuck;
		return Creature;
	}

	FHexBuilding MakeHall()
	{
		FHexBuilding Hall;
		Hall.Id = TEXT("Hall");
		Hall.Type = EHexBuildingType::Hall;
		Hall.StartLevel = 1;
		Hall.Scale = 1.1f;
		// against the wall between two gates, facing the middle, which stays open: the hero that guards the castle stands there on its paved
		// round, where it shows from any side, as the developer asked on 2026-11-11 (the hall stood in the middle at first and hid it, then
		// so near it that it reached onto the round, which they found looked stuck on)
		Hall.Place = FVector2D(0.86, 0.0);
		Hall.Yaw = 180.0f;
		// 500 gold a day at first, then 1000 and 4000, as the developer's document of the resources says
		const TCHAR* Names[3] = {TEXT("Village hall"), TEXT("Town hall"), TEXT("City hall")};
		const int32 Gold[3] = {500, 1000, 4000};
		const int32 Cost[3] = {0, 2500, 7000};
		const int32 Fort[3] = {0, 1, 3};
		for (int32 Level = 0; Level < 3; ++Level)
		{
			FHexBuildingLevel& Each = Hall.Levels.AddDefaulted_GetRef();
			Each.DisplayName = Names[Level];
			Each.GoldPerDay = Gold[Level];
			Each.NeedsFort = Fort[Level];
			Each.Mesh = MeshOf(FString::Printf(TEXT("Hall%d"), Level + 1));
			if (Cost[Level] > 0)
			{
				Each.Cost.Add(TEXT("Gold"), Cost[Level]);
			}
		}
		return Hall;
	}

	FHexBuilding MakeFort()
	{
		FHexBuilding Fort;
		Fort.Id = TEXT("Fort");
		Fort.Type = EHexBuildingType::Fort;
		const TCHAR* Names[3] = {TEXT("Fort"), TEXT("Citadel"), TEXT("Castle")};
		const int32 Gold[3] = {2000, 3000, 5500};
		const int32 Wood[3] = {5, 5, 10};
		const int32 Ore[3] = {10, 10, 15};
		const float Growth[3] = {1.0f, 1.5f, 2.0f};
		const float Defense[3] = {1.3f, 1.6f, 2.0f};
		for (int32 Level = 0; Level < 3; ++Level)
		{
			FHexBuildingLevel& Each = Fort.Levels.AddDefaulted_GetRef();
			Each.DisplayName = Names[Level];
			Each.GrowthFactor = Growth[Level];
			Each.DefenseFactor = Defense[Level];
			// seen from outside it is the wall, the towers and the gate of the castle (WallMeshes and the rest)
			Each.NeedsHall = Level > 0 ? 2 : 0;
			Each.Cost.Add(TEXT("Gold"), Gold[Level]);
			Each.Cost.Add(TEXT("Wood"), Wood[Level]);
			Each.Cost.Add(TEXT("Ore"), Ore[Level]);
		}
		return Fort;
	}

	/** A dwelling of a creature level at its place in the castle: built, it offers the basic creature, upgraded, the better one */
	FHexBuilding MakeDwelling(const FFactionMake& Make, const TCHAR* Name, const TCHAR* UpgradedName, int32 Level)
	{
		// the dwellings stand against the wall around the yard, each as far out as its depth lets it, in the three stretches between the
		// gates, so the ways from the gates to the middle and the round in the middle stay free: two beside the hall, three in the next
		// stretch and two in the last, which leaves room for the flag
		const double Angles[NumCreatureLevels] = {-34.0, 34.0, 118.0, 88.0, 148.0, 210.0, 264.0};
		const double Reaches[NumCreatureLevels] = {1.02, 1.05, 1.1, 1.04, 0.9, 1.02, 1.12};
		const double Reach = Reaches[Level];
		const double Angle = Angles[Level];
		FHexBuilding Dwelling;
		Dwelling.Id = *FString::Printf(TEXT("Dwelling%d"), Level + 1);
		Dwelling.Type = EHexBuildingType::Dwelling;
		Dwelling.Creature = Level;
		Dwelling.StartLevel = Level == 0 ? 1 : 0;
		Dwelling.NeedsDwelling = Level - 1;
		Dwelling.Scale = Level == NumCreatureLevels - 1 ? 1.0f : 1.2f;
		Dwelling.Place = FVector2D(Reach * FMath::Cos(FMath::DegreesToRadians(Angle)), Reach * FMath::Sin(FMath::DegreesToRadians(Angle)));
		// it faces the middle of the castle
		Dwelling.Yaw = float(Angle) + 180.0f;
		for (int32 Step = 0; Step < 2; ++Step)
		{
			FHexBuildingLevel& Each = Dwelling.Levels.AddDefaulted_GetRef();
			Each.DisplayName = Step == 0 ? Name : UpgradedName;
			Each.NeedsFort = DwellingFort[Level];
			const float Share = Step == 0 ? 1.0f : 0.7f;
			Each.Cost.Add(TEXT("Gold"), FMath::RoundToInt32(DwellingGold[Level] * Share * Make.Price / 50.0f) * 50);
			if (Level > 0)
			{
				Each.Cost.Add(TEXT("Wood"), FMath::RoundToInt32((3 + 2 * Level) * Share));
				Each.Cost.Add(TEXT("Ore"), FMath::RoundToInt32((3 + 2 * Level) * Share));
			}
			if (Level >= 4)
			{
				Each.Cost.Add(Level == NumCreatureLevels - 1 ? Make.Rarest : Make.Rare, FMath::RoundToInt32((Level - 3) * 2 * Share));
			}
			if (Step == 0)
			{
				Each.Mesh = MeshOf(FString::Printf(TEXT("Dwelling%d"), Level + 1));
			}
		}
		return Dwelling;
	}
}

float FHexCreatureStats::GetPower() const
{
	return FMath::Sqrt(float(FMath::Max(Health, 1)) * FMath::Max(0.5f * float(DamageMin + DamageMax), 0.5f)) * (1.0f + 0.05f * float(Attack + Defense))
		* (1.0f + 0.03f * float(Speed));
}

int32 FHexFaction::FindBuilding(EHexBuildingType Type, int32 Creature) const
{
	return Buildings.IndexOfByPredicate([Type, Creature](const FHexBuilding& Each) { return Each.Type == Type && (Type != EHexBuildingType::Dwelling || Each.Creature == Creature); });
}

TArray<FHexFaction> MakeDefaultFactions()
{
	TArray<FHexFaction> Factions;
	{
		// the Order of Dawn: fewer, better and dearer creatures
		FFactionMake Make;
		Make.Quality = 1.1f;
		Make.Price = 1.15f;
		Make.Rare = TEXT("Crystal");
		Make.Rarest = TEXT("Gems");
		FHexFaction& Order = Factions.AddDefaulted_GetRef();
		Order.Id = TEXT("Dawn");
		Order.DisplayName = TEXT("Order of Dawn");
		Order.Color = FLinearColor(0.1f, 0.25f, 0.75f, 1.0f);
		// the roles of the levels: a spearman, a shooter, a flyer that strikes back twice, a swordsman, a mage that shoots, a rider who charges
		// and a winged guard of high morale
		Order.Creatures = {
			With(MakeCreature(Make, TEXT("Sentinel"), TEXT("Sentinel"), TEXT("Shieldbearer"), 0, 1.15f, 0.9f, 4), 0, Bits({EHexAbility::RetaliateTwice})),
			With(MakeCreature(Make, TEXT("Huntsman"), TEXT("Huntsman"), TEXT("Master huntsman"), 1, 0.8f, 1.1f, 4, true), 0, Bits({EHexAbility::DoubleStrike}), 12),
			With(MakeCreature(Make, TEXT("Glowwing"), TEXT("Glowwing"), TEXT("Sunwing"), 2, 1.0f, 0.95f, 7, false, true), Bits({EHexAbility::RetaliateTwice}), 0),
			With(MakeCreature(Make, TEXT("Oathsworn"), TEXT("Oathsworn"), TEXT("Oathblade"), 3, 1.1f, 1.05f, 5), 0, Bits({EHexAbility::DoubleStrike})),
			With(MakeCreature(Make, TEXT("Pilgrim"), TEXT("Pilgrim"), TEXT("Seer"), 4, 0.75f, 1.15f, 5, true), Bits({EHexAbility::NoMeleePenalty}), 0, 12),
			With(MakeCreature(Make, TEXT("DawnRider"), TEXT("Dawn rider"), TEXT("Dawn herald"), 5, 1.05f, 1.1f, 8), Bits({EHexAbility::Charge}), 0),
			With(MakeCreature(Make, TEXT("SkyWarden"), TEXT("Sky warden"), TEXT("Eternal warden"), 6, 1.1f, 1.1f, 12, false, true), 0, Bits({EHexAbility::RetaliateTwice}), 0, 1)};
		Order.Buildings = {MakeHall(), MakeFort(),
			MakeDwelling(Make, TEXT("Guardhouse"), TEXT("Shield hall"), 0),
			MakeDwelling(Make, TEXT("Hunters' lodge"), TEXT("Masters' lodge"), 1),
			MakeDwelling(Make, TEXT("Aerie"), TEXT("Sun aerie"), 2),
			MakeDwelling(Make, TEXT("Oath hall"), TEXT("Hall of blades"), 3),
			MakeDwelling(Make, TEXT("Cloister"), TEXT("Seers' cloister"), 4),
			MakeDwelling(Make, TEXT("Dawn stables"), TEXT("Heralds' stables"), 5),
			MakeDwelling(Make, TEXT("Sky gate"), TEXT("Eternal gate"), 6)};
	}
	{
		// the Realm of Decay: more, weaker and cheaper creatures
		FFactionMake Make;
		Make.Quality = 0.92f;
		Make.Price = 0.9f;
		Make.LowGrowth = 1.3f;
		Make.Rare = TEXT("Sulfur");
		Make.Rarest = TEXT("Mercury");
		FHexFaction& Decay = Factions.AddDefaulted_GetRef();
		Decay.Id = TEXT("Decay");
		Decay.DisplayName = TEXT("Realm of Decay");
		Decay.Color = FLinearColor(0.3f, 0.6f, 0.2f, 1.0f);
		Decay.WallStyle = 1.0f;
		// the roles of the levels, all undead: a skeleton, a slow tough zombie, a spirit that heals itself and eats mana, a blood drinker
		// nothing strikes back, a mage whose shot hits an area, a dark rider who charges and a bone dragon whose breath goes on
		const int32 Undead = Bits({EHexAbility::Undead});
		Decay.Creatures = {
			With(MakeCreature(Make, TEXT("BoneWarrior"), TEXT("Bone warrior"), TEXT("Bone guard"), 0, 0.9f, 1.0f, 4), Undead, 0),
			With(MakeCreature(Make, TEXT("Decayed"), TEXT("Decayed"), TEXT("Decay bearer"), 1, 1.35f, 0.85f, 3), Undead, 0),
			With(MakeCreature(Make, TEXT("Shade"), TEXT("Shade"), TEXT("Mana eater"), 2, 0.9f, 1.0f, 7, false, true), Undead | Bits({EHexAbility::Regenerate}), Bits({EHexAbility::ManaDrain})),
			With(MakeCreature(Make, TEXT("BloodDrinker"), TEXT("Blood drinker"), TEXT("Eternal blood drinker"), 3, 1.0f, 1.1f, 6, false, true), Undead | Bits({EHexAbility::NoRetaliation}), Bits({EHexAbility::LifeDrain})),
			With(MakeCreature(Make, TEXT("AshMage"), TEXT("Ash mage"), TEXT("Ash lord"), 4, 0.8f, 1.2f, 5, true), Undead | Bits({EHexAbility::AreaShot}), 0, 12),
			With(MakeCreature(Make, TEXT("DecayRider"), TEXT("Decay rider"), TEXT("Abyss rider"), 5, 1.15f, 1.15f, 7), Undead | Bits({EHexAbility::Charge}), 0, 0, 0, 2),
			With(MakeCreature(Make, TEXT("BoneSerpent"), TEXT("Bone serpent"), TEXT("Dusk serpent"), 6, 1.0f, 1.0f, 10, false, true), Undead | Bits({EHexAbility::Breath}), 0)};
		Decay.Buildings = {MakeHall(), MakeFort(),
			MakeDwelling(Make, TEXT("Bone yard"), TEXT("Guards' crypt"), 0),
			MakeDwelling(Make, TEXT("Rot pit"), TEXT("Bearers' pit"), 1),
			MakeDwelling(Make, TEXT("Shade barrow"), TEXT("Hungry barrow"), 2),
			MakeDwelling(Make, TEXT("Blood manor"), TEXT("Eternal manor"), 3),
			MakeDwelling(Make, TEXT("Ash tower"), TEXT("Lords' tower"), 4),
			MakeDwelling(Make, TEXT("Decay stables"), TEXT("Abyss stables"), 5),
			MakeDwelling(Make, TEXT("Serpent nest"), TEXT("Dusk nest"), 6)};
	}
	return Factions;
}

UHexCastleConfig::UHexCastleConfig()
{
	Factions = MakeDefaultFactions();
	// by the level of the fort: a fence before one is built, a palisade, and stone with towers
	for (int32 Level = 0; Level < 4; ++Level)
	{
		WallMeshes.Add(MeshOf(FString::Printf(TEXT("Wall%d"), Level)));
		GateMeshes.Add(MeshOf(FString::Printf(TEXT("Gate%d"), Level)));
		TowerMeshes.Add(Level >= 2 ? MeshOf(FString::Printf(TEXT("Tower%d"), Level)) : TSoftObjectPtr<UStaticMesh>());
	}
}

void UHexCastleConfig::PostLoad()
{
	Super::PostLoad();

	if (Factions.IsEmpty())
	{
		Factions = MakeDefaultFactions();
		return;
	}
	// a config saved before the battles has its creatures without what they do in one: they take it from the defaults of the class by the Ids
	// of their faction and creature while they still hold none of it, so what was tuned by hand stays
	const TArray<FHexFaction> Defaults = MakeDefaultFactions();
	const auto IsBare = [](const FHexCreatureStats& Stats) { return Stats.Abilities == 0 && Stats.Shots == 0 && Stats.Morale == 0 && Stats.Luck == 0; };
	for (FHexFaction& Faction : Factions)
	{
		const FHexFaction* Default = Defaults.FindByPredicate([&Faction](const FHexFaction& Each) { return Each.Id == Faction.Id; });
		for (FHexCreature& Creature : Faction.Creatures)
		{
			const FHexCreature* Made = Default ? Default->Creatures.FindByPredicate([&Creature](const FHexCreature& Each) { return Each.Id == Creature.Id; }) : nullptr;
			if (!Made || !IsBare(Creature.Base) || !IsBare(Creature.Upgraded))
			{
				continue;
			}
			for (const bool bUpgraded : {false, true})
			{
				FHexCreatureStats& Stats = bUpgraded ? Creature.Upgraded : Creature.Base;
				const FHexCreatureStats& From = Made->GetStats(bUpgraded);
				Stats.Abilities = From.Abilities;
				Stats.Shots = From.Shots;
				Stats.Morale = From.Morale;
				Stats.Luck = From.Luck;
			}
		}
	}
}

int32 UHexCastleConfig::FindFaction(const FString& Id) const
{
	return Factions.IndexOfByPredicate([&Id](const FHexFaction& Each) { return Each.Id.ToString().Equals(Id, ESearchCase::IgnoreCase); });
}

const FHexCreatureStats* UHexCastleConfig::GetStats(const FHexStack& Stack) const
{
	return Factions.IsValidIndex(Stack.Faction) && Factions[Stack.Faction].Creatures.IsValidIndex(Stack.Creature)
		? &Factions[Stack.Faction].Creatures[Stack.Creature].GetStats(Stack.bUpgraded) : nullptr;
}

const UHexCastleConfig* UHexCastleConfig::Load()
{
	if (const UHexCastleConfig* Config = LoadObject<UHexCastleConfig>(nullptr, CastleConfigPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Config;
	}
	UE_LOG(LogHexHero, Warning, TEXT("Castles: %s is missing, taking the defaults of its class; run Tools/HexPlanetCastles/import_planet_castles.py"), CastleConfigPath);
	return GetDefault<UHexCastleConfig>();
}
