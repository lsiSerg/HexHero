// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexBattleConfig.h"
#include "HexHero.h"
#include "Engine/Texture2D.h"
#include "NiagaraSystem.h"

namespace
{
	const TCHAR* BattleConfigPath = TEXT("/Game/HexPlanet/Battle/DA_HexBattle.DA_HexBattle");

	/** The version of the spells of MakeDefaultSpells: an asset that holds an older one takes them again (PostLoad) */
	constexpr int32 CurrentSpellsVersion = 4;

	/** Makes a spell of a school and a level, with its picture by its Id */
	FHexSpell& AddSpell(TArray<FHexSpell>& Spells, const TCHAR* Id, const TCHAR* Name, EHexSpellSchool School, int32 Level, EHexSpellKind Kind, EHexSpellTarget Target,
		int32 Mana, EHexSpellLook Look, const FLinearColor& Color, const TCHAR* Description)
	{
		FHexSpell& Spell = Spells.AddDefaulted_GetRef();
		Spell.Id = Id;
		Spell.DisplayName = Name;
		Spell.Description = Description;
		Spell.School = School;
		Spell.Level = Level;
		Spell.Kind = Kind;
		Spell.Target = Target;
		Spell.ManaCost = Mana;
		Spell.Look = Look;
		Spell.Color = Color;
		Spell.Power = 0;
		Spell.PowerPerSpellPower = 0;
		const FString Picture = FString::Printf(TEXT("T_Spell%s"), Id);
		Spell.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(TEXT("/Game/HexPlanet/UI/Spells/%s.%s"), *Picture, *Picture)));
		return Spell;
	}

	FHexSpell& SetPower(FHexSpell& Spell, int32 Power, int32 PerSpellPower)
	{
		Spell.Power = Power;
		Spell.PowerPerSpellPower = PerSpellPower;
		return Spell;
	}
}

const TCHAR* GetSpellSchoolName(EHexSpellSchool School)
{
	switch (School)
	{
	case EHexSpellSchool::Air: return TEXT("Air");
	case EHexSpellSchool::Earth: return TEXT("Earth");
	case EHexSpellSchool::Fire: return TEXT("Fire");
	case EHexSpellSchool::Water: return TEXT("Water");
	default: return TEXT("?");
	}
}

TArray<FHexSpell> MakeDefaultSpells()
{
	using K = EHexSpellKind;
	using T = EHexSpellTarget;
	using L = EHexSpellLook;
	const EHexSpellSchool Air = EHexSpellSchool::Air;
	const EHexSpellSchool Earth = EHexSpellSchool::Earth;
	const EHexSpellSchool Fire = EHexSpellSchool::Fire;
	const EHexSpellSchool Water = EHexSpellSchool::Water;
	const FLinearColor AirColor(0.55f, 0.8f, 1.0f, 1.0f);
	const FLinearColor EarthColor(0.75f, 0.55f, 0.3f, 1.0f);
	const FLinearColor FireColor(1.0f, 0.42f, 0.1f, 1.0f);
	const FLinearColor WaterColor(0.3f, 0.65f, 1.0f, 1.0f);
	const FLinearColor DarkColor(0.55f, 0.3f, 0.85f, 1.0f);
	const FLinearColor HolyColor(1.0f, 0.92f, 0.6f, 1.0f);
	TArray<FHexSpell> Spells;

	// Air: speed, lightning, luck and the arrows of the wind
	AddSpell(Spells, TEXT("Haste"), TEXT("Haste"), Air, 1, K::Effect, T::Friend, 6, L::Rise, FLinearColor(1.0f, 0.9f, 0.35f, 1.0f),
		TEXT("A stack of your own is faster")).SpeedAdd = 3;
	Spells.Last().bMassAtExpert = true;
	SetPower(AddSpell(Spells, TEXT("MagicArrow"), TEXT("Magic arrow"), Air, 1, K::Damage, T::Enemy, 5, L::Missile, FLinearColor(0.7f, 0.85f, 1.0f, 1.0f),
		TEXT("An arrow of light strikes an enemy stack")), 10, 10);
	SetPower(AddSpell(Spells, TEXT("LightningBolt"), TEXT("Lightning bolt"), Air, 2, K::Damage, T::Enemy, 10, L::Strike, FLinearColor(0.75f, 0.85f, 1.0f, 1.0f),
		TEXT("Lightning strikes an enemy stack from the sky")), 10, 25);
	AddSpell(Spells, TEXT("DisruptingRay"), TEXT("Disrupting ray"), Air, 2, K::Effect, T::Enemy, 10, L::Missile, FLinearColor(0.4f, 1.0f, 0.75f, 1.0f),
		TEXT("An enemy stack loses 3 Defense for the rest of the battle; it adds up")).DefenseAdd = -3;
	Spells.Last().bPermanent = true;
	AddSpell(Spells, TEXT("Fortune"), TEXT("Fortune"), Air, 2, K::Effect, T::Friend, 7, L::Rise, FLinearColor(0.45f, 1.0f, 0.45f, 1.0f),
		TEXT("A stack of your own has 2 more luck")).LuckAdd = 2;
	Spells.Last().bMassAtExpert = true;
	AddSpell(Spells, TEXT("Precision"), TEXT("Precision"), Air, 2, K::Effect, T::Friend, 8, L::Rise, AirColor,
		TEXT("A stack of your own shoots with 3 more Attack")).RangedAttackAdd = 3;
	Spells.Last().bMassAtExpert = true;
	AddSpell(Spells, TEXT("AirShield"), TEXT("Air shield"), Air, 3, K::Effect, T::Friend, 12, L::Rise, AirColor,
		TEXT("A stack of your own takes a quarter less damage from shots")).RangedTakenCut = 0.25f;
	Spells.Last().bMassAtExpert = true;
	FHexSpell& DestroyUndead = SetPower(AddSpell(Spells, TEXT("DestroyUndead"), TEXT("Destroy undead"), Air, 3, K::FieldDamage, T::Hex, 15, L::Field, HolyColor,
		TEXT("Holy light burns every undead stack on the field")), 10, 10);
	DestroyUndead.Filter = EHexSpellFilter::Undead;
	FHexSpell& Chain = SetPower(AddSpell(Spells, TEXT("ChainLightning"), TEXT("Chain lightning"), Air, 5, K::ChainDamage, T::Enemy, 24, L::Strike, FLinearColor(0.7f, 0.8f, 1.0f, 1.0f),
		TEXT("Lightning strikes an enemy stack and leaps on to the nearest stacks, friend or foe, weaker each time")), 25, 40);
	Chain.ChainTargets = 4;
	AddSpell(Spells, TEXT("Counterstrike"), TEXT("Counterstrike"), Air, 4, K::Effect, T::Friend, 24, L::Rise, FLinearColor(0.95f, 0.75f, 0.4f, 1.0f),
		TEXT("A stack of your own strikes back once more a round")).RetaliationsAdd = 1;
	Spells.Last().bMassAtExpert = true;

	// Earth: slowness, stone, the dead raised and the ground shaken
	AddSpell(Spells, TEXT("Slow"), TEXT("Slow"), Earth, 1, K::Effect, T::Enemy, 6, L::Fall, FLinearColor(0.6f, 0.55f, 0.45f, 1.0f),
		TEXT("An enemy stack is half as fast")).SpeedShare = 0.5f;
	Spells.Last().bMassAtExpert = true;
	AddSpell(Spells, TEXT("StoneSkin"), TEXT("Stone skin"), Earth, 1, K::Effect, T::Friend, 5, L::Rise, FLinearColor(0.7f, 0.65f, 0.55f, 1.0f),
		TEXT("A stack of your own has 3 more Defense")).DefenseAdd = 3;
	Spells.Last().bMassAtExpert = true;
	AddSpell(Spells, TEXT("Shield"), TEXT("Shield"), Earth, 1, K::Effect, T::Friend, 5, L::Rise, EarthColor,
		TEXT("A stack of your own takes a sixth less damage from strikes")).MeleeTakenCut = 0.15f;
	Spells.Last().bMassAtExpert = true;
	FHexSpell& Ripple = SetPower(AddSpell(Spells, TEXT("DeathRipple"), TEXT("Death ripple"), Earth, 2, K::FieldDamage, T::Hex, 10, L::Field, DarkColor,
		TEXT("A wave of death harms every living stack on the field, friend or foe")), 10, 5);
	Ripple.Filter = EHexSpellFilter::Living;
	FHexSpell& Animate = SetPower(AddSpell(Spells, TEXT("AnimateDead"), TEXT("Animate dead"), Earth, 3, K::Raise, T::Friend, 15, L::Rise, FLinearColor(0.5f, 0.9f, 0.55f, 1.0f),
		TEXT("Raises the fallen of an undead stack of your own")), 30, 50);
	Animate.Filter = EHexSpellFilter::Undead;
	AddSpell(Spells, TEXT("AntiMagic"), TEXT("Anti-magic"), Earth, 3, K::Effect, T::Friend, 15, L::Rise, FLinearColor(0.85f, 0.85f, 0.9f, 1.0f),
		TEXT("No spell takes a stack of your own after this one until it runs out")).bSpellImmune = true;
	FHexSpell& Quake = SetPower(AddSpell(Spells, TEXT("Earthquake"), TEXT("Earthquake"), Earth, 3, K::Earthquake, T::Hex, 20, L::Ring, EarthColor,
		TEXT("The ground shakes the wall of the castle: two parts of it take damage")), 50, 15);
	Quake.WallParts = 2;
	Quake.Radius = 3;
	FHexSpell& Meteor = SetPower(AddSpell(Spells, TEXT("MeteorShower"), TEXT("Meteor shower"), Earth, 4, K::AreaDamage, T::Hex, 16, L::Strike, FLinearColor(1.0f, 0.55f, 0.25f, 1.0f),
		TEXT("Stones fall from the sky on a hex and those around it, friend or foe")), 25, 25);
	Meteor.Radius = 1;
	Meteor.EffectSize = 1.4f;
	FHexSpell& Resurrect = SetPower(AddSpell(Spells, TEXT("Resurrection"), TEXT("Resurrection"), Earth, 4, K::Raise, T::Friend, 20, L::Rise, HolyColor,
		TEXT("Brings back the fallen of a living stack of your own")), 40, 50);
	Resurrect.Filter = EHexSpellFilter::Living;
	SetPower(AddSpell(Spells, TEXT("Implosion"), TEXT("Implosion"), Earth, 5, K::Damage, T::Enemy, 30, L::Burst, FLinearColor(0.6f, 0.35f, 0.9f, 1.0f),
		TEXT("The air around an enemy stack crushes it")), 100, 75).EffectSize = 1.3f;

	// Fire: fury, curses and flame
	AddSpell(Spells, TEXT("Bloodlust"), TEXT("Bloodlust"), Fire, 1, K::Effect, T::Friend, 5, L::Rise, FLinearColor(1.0f, 0.2f, 0.15f, 1.0f),
		TEXT("A stack of your own strikes with 3 more Attack")).MeleeAttackAdd = 3;
	Spells.Last().bMassAtExpert = true;
	AddSpell(Spells, TEXT("Curse"), TEXT("Curse"), Fire, 1, K::Effect, T::Enemy, 6, L::Fall, FLinearColor(0.5f, 0.15f, 0.25f, 1.0f),
		TEXT("An enemy stack deals the least damage it can")).bMinDamage = true;
	Spells.Last().bMassAtExpert = true;
	FHexSpell& FireWard = AddSpell(Spells, TEXT("ProtectionFromFire"), TEXT("Protection from fire"), Fire, 1, K::Effect, T::Friend, 5, L::Rise, FLinearColor(1.0f, 0.6f, 0.3f, 1.0f),
		TEXT("A stack of your own takes half the damage of the spells of fire"));
	FireWard.bWardsSchool = true;
	FireWard.WardSchool = Fire;
	FireWard.bMassAtExpert = true;
	AddSpell(Spells, TEXT("Blind"), TEXT("Blind"), Fire, 2, K::Effect, T::Enemy, 10, L::Fall, FLinearColor(0.9f, 0.85f, 0.75f, 1.0f),
		TEXT("An enemy stack stands blind and loses its turns until it is struck, and does not strike back")).bBlind = true;
	FHexSpell& Fireball = SetPower(AddSpell(Spells, TEXT("Fireball"), TEXT("Fireball"), Fire, 3, K::AreaDamage, T::Hex, 15, L::Missile, FireColor,
		TEXT("A ball of fire bursts over a hex and those around it, burning friend and foe")), 15, 10);
	Fireball.Radius = 1;
	Fireball.EffectSize = 1.3f;
	AddSpell(Spells, TEXT("Misfortune"), TEXT("Misfortune"), Fire, 3, K::Effect, T::Enemy, 12, L::Fall, FLinearColor(0.6f, 0.3f, 0.2f, 1.0f),
		TEXT("An enemy stack has 2 less luck")).LuckAdd = -2;
	Spells.Last().bMassAtExpert = true;
	FHexSpell& Inferno = SetPower(AddSpell(Spells, TEXT("Inferno"), TEXT("Inferno"), Fire, 4, K::AreaDamage, T::Hex, 16, L::Burst, FLinearColor(1.0f, 0.3f, 0.05f, 1.0f),
		TEXT("Fire swallows a hex and two rings around it, friend and foe")), 20, 10);
	Inferno.Radius = 2;
	Inferno.EffectSize = 2.0f;
	FHexSpell& Frenzy = AddSpell(Spells, TEXT("Frenzy"), TEXT("Frenzy"), Fire, 4, K::Effect, T::Friend, 16, L::Rise, FLinearColor(1.0f, 0.15f, 0.1f, 1.0f),
		TEXT("A stack of your own puts its Defense into its Attack until its next turn"));
	Frenzy.FrenzyShare = 1.0f;
	Frenzy.BaseRounds = 1;
	Frenzy.RoundsPerSpellPower = 0;
	AddSpell(Spells, TEXT("FireShield"), TEXT("Fire shield"), Fire, 4, K::Effect, T::Friend, 16, L::Rise, FLinearColor(1.0f, 0.5f, 0.15f, 1.0f),
		TEXT("A fifth of the damage of a strike at a stack of your own burns the one that struck")).ReflectShare = 0.2f;
	Spells.Last().bMassAtExpert = true;
	SetPower(AddSpell(Spells, TEXT("Armageddon"), TEXT("Armageddon"), Fire, 5, K::FieldDamage, T::Hex, 24, L::Field, FLinearColor(1.0f, 0.35f, 0.1f, 1.0f),
		TEXT("Fire rains on every stack on the field, friend and foe")), 30, 50);

	// Water: blessings, cures and frost
	AddSpell(Spells, TEXT("Bless"), TEXT("Bless"), Water, 1, K::Effect, T::Friend, 5, L::Rise, HolyColor,
		TEXT("A stack of your own deals the most damage it can")).bMaxDamage = true;
	Spells.Last().bMassAtExpert = true;
	FHexSpell& Cure = SetPower(AddSpell(Spells, TEXT("Cure"), TEXT("Cure"), Water, 1, K::Heal, T::Friend, 6, L::Rise, FLinearColor(0.4f, 1.0f, 0.6f, 1.0f),
		TEXT("Heals the wounded of a stack of your own and takes the harmful spells off it")), 10, 5);
	Cure.bCure = true;
	Cure.bMassAtExpert = true;
	AddSpell(Spells, TEXT("Dispel"), TEXT("Dispel"), Water, 1, K::Dispel, T::Any, 5, L::Fall, FLinearColor(0.75f, 0.9f, 1.0f, 1.0f),
		TEXT("Takes every spell off a stack, friend or foe"));
	FHexSpell& WaterWard = AddSpell(Spells, TEXT("ProtectionFromWater"), TEXT("Protection from water"), Water, 1, K::Effect, T::Friend, 5, L::Rise, WaterColor,
		TEXT("A stack of your own takes half the damage of the spells of water"));
	WaterWard.bWardsSchool = true;
	WaterWard.WardSchool = Water;
	WaterWard.bMassAtExpert = true;
	SetPower(AddSpell(Spells, TEXT("IceBolt"), TEXT("Ice bolt"), Water, 2, K::Damage, T::Enemy, 8, L::Missile, FLinearColor(0.6f, 0.9f, 1.0f, 1.0f),
		TEXT("A bolt of ice strikes an enemy stack")), 10, 20);
	AddSpell(Spells, TEXT("Weakness"), TEXT("Weakness"), Water, 2, K::Effect, T::Enemy, 8, L::Fall, FLinearColor(0.55f, 0.35f, 0.7f, 1.0f),
		TEXT("An enemy stack has 3 less Attack")).AttackAdd = -3;
	Spells.Last().bMassAtExpert = true;
	AddSpell(Spells, TEXT("Forgetfulness"), TEXT("Forgetfulness"), Water, 3, K::Effect, T::Enemy, 12, L::Fall, FLinearColor(0.5f, 0.6f, 0.95f, 1.0f),
		TEXT("An enemy stack forgets how to shoot")).bNoShooting = true;
	Spells.Last().bMassAtExpert = true;
	FHexSpell& Frost = SetPower(AddSpell(Spells, TEXT("FrostRing"), TEXT("Frost ring"), Water, 3, K::RingDamage, T::Hex, 12, L::Ring, FLinearColor(0.7f, 0.95f, 1.0f, 1.0f),
		TEXT("Frost bites the hexes around the one aimed at, but not that one")), 15, 10);
	Frost.Radius = 1;
	AddSpell(Spells, TEXT("Mirth"), TEXT("Mirth"), Water, 3, K::Effect, T::Friend, 12, L::Rise, FLinearColor(1.0f, 0.85f, 0.3f, 1.0f),
		TEXT("A stack of your own has 2 more morale")).MoraleAdd = 2;
	Spells.Last().bMassAtExpert = true;
	FHexSpell& Prayer = AddSpell(Spells, TEXT("Prayer"), TEXT("Prayer"), Water, 4, K::Effect, T::Friend, 16, L::Rise, HolyColor,
		TEXT("A stack of your own has 2 more Attack, Defense and Speed"));
	Prayer.AttackAdd = 2;
	Prayer.DefenseAdd = 2;
	Prayer.SpeedAdd = 2;
	Prayer.bMassAtExpert = true;

	// cast on the planet, never in a battle, as the developer asked on 2026-11-15
	const auto Adventure = [&Spells](const TCHAR* Id, const TCHAR* Name, EHexSpellSchool School, int32 Level, int32 Mana, EHexAdventureSpell What, int32 Reach,
		int32 ReachPerLevel, float Movement, int32 PerDay, const FLinearColor& Color, const TCHAR* Description) -> FHexSpell&
	{
		FHexSpell& Spell = AddSpell(Spells, Id, Name, School, Level, K::Adventure, T::Hex, Mana, L::Rise, Color, Description);
		Spell.Adventure = What;
		Spell.Reach = Reach;
		Spell.ReachPerLevel = ReachPerLevel;
		Spell.MovementCost = Movement;
		Spell.CastsPerDay = PerDay;
		return Spell;
	};
	Adventure(TEXT("SummonBoat"), TEXT("Summon boat"), Water, 1, 8, EHexAdventureSpell::SummonBoat, 2, 1, 0.0f, 0, WaterColor,
		TEXT("A boat comes to the free sea nearest the hero, within 2 cells, and a cell more a level of Water magic"));
	Adventure(TEXT("ViewLand"), TEXT("View the land"), Air, 2, 8, EHexAdventureSpell::ViewSurface, 8, 4, 0.0f, 0, AirColor,
		TEXT("The hero sees the surface within 8 cells, and 4 more a level of Air magic; at Expert all of it"));
	Adventure(TEXT("ViewDepths"), TEXT("View the depths"), Earth, 2, 8, EHexAdventureSpell::ViewUnderground, 8, 4, 0.0f, 0, EarthColor,
		TEXT("The hero sees the passages of the underground within 8 cells of its place, and 4 more a level of Earth magic; at Expert all of them"));
	Adventure(TEXT("DimensionDoor"), TEXT("Dimension door"), Air, 4, 20, EHexAdventureSpell::DimensionDoor, 3, 1, 3.0f, 2, FLinearColor(0.75f, 0.5f, 1.0f, 1.0f),
		TEXT("The hero steps to a cell it may stand on within 3 cells, and a cell more a level of Air magic, over whatever lies between; it takes 3 points of movement, twice a day at most"));
	Adventure(TEXT("Teleport"), TEXT("Teleport"), Water, 3, 12, EHexAdventureSpell::Teleport, 5, 0, 2.0f, 0, FLinearColor(0.45f, 0.85f, 1.0f, 1.0f),
		TEXT("The hero steps to a cell it may stand on within 5 cells, over whatever lies between; it takes 2 points of movement"));
	Adventure(TEXT("TownPortal"), TEXT("Town portal"), Earth, 4, 16, EHexAdventureSpell::TownPortal, 0, 0, 3.0f, 0, FLinearColor(0.95f, 0.85f, 0.45f, 1.0f),
		TEXT("The hero goes into the nearest castle of its own that no hero guards, or from Advanced Earth magic into the one chosen; it takes 3 points of movement"));
	return Spells;
}

TArray<TSoftObjectPtr<UNiagaraSystem>> MakeDefaultSpellEffects()
{
	// a system for each look, in the order of EHexSpellLook, from Tools/HexBattle/build_spell_effects.md
	const TCHAR* Names[] = {TEXT("Missile"), TEXT("Strike"), TEXT("Burst"), TEXT("Ring"), TEXT("Rise"), TEXT("Fall"), TEXT("Field")};
	static_assert(UE_ARRAY_COUNT(Names) == int32(EHexSpellLook::Num), "a system for each look");
	TArray<TSoftObjectPtr<UNiagaraSystem>> Effects;
	for (const TCHAR* Name : Names)
	{
		Effects.Add(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(FString::Printf(TEXT("/Game/HexPlanet/Battle/Spells/NS_HexSpell%s.NS_HexSpell%s"), Name, Name))));
	}
	return Effects;
}

UHexBattleConfig::UHexBattleConfig()
{
	// the versions stay 0 in the class, so an asset saved before them, which takes the value of the class, is brought up to date (PostLoad)
	Spells = MakeDefaultSpells();
	SpellEffects = MakeDefaultSpellEffects();
	// the ground of the field, grass by default, and the cave underground
	FHexBattleGround& Open = Grounds.AddDefaulted_GetRef();
	FHexBattleGround& Cave = Grounds.AddDefaulted_GetRef();
	Cave.bUnderground = true;
	Cave.SunColor = FLinearColor(0.7f, 0.8f, 1.0f, 1.0f);
	Cave.SunShare = 0.3f;
	Cave.SkyIntensity = 0.35f;
	Cave.SkyColor = FLinearColor(0.35f, 0.5f, 0.55f, 1.0f);
	Cave.BackgroundColor = FLinearColor(0.02f, 0.03f, 0.035f, 1.0f);
	(void)Open;
}

void UHexBattleConfig::PostLoad()
{
	Super::PostLoad();
	// the forty spells of the four schools of 2026-11-15 take the place of the eight starters of before, which an artist did not tune
	if (Spells.IsEmpty() || SpellsVersion < CurrentSpellsVersion)
	{
		Spells = MakeDefaultSpells();
		SpellsVersion = CurrentSpellsVersion;
	}
	if (SpellEffects.Num() < int32(EHexSpellLook::Num))
	{
		const TArray<TSoftObjectPtr<UNiagaraSystem>> Defaults = MakeDefaultSpellEffects();
		for (int32 Look = SpellEffects.Num(); Look < Defaults.Num(); ++Look)
		{
			SpellEffects.Add(Defaults[Look]);
		}
	}
}

const FHexUnitLook* UHexBattleConfig::FindLook(FName Creature) const
{
	return UnitLooks.FindByPredicate([Creature](const FHexUnitLook& Each) { return Each.Creature == Creature; });
}

const FHexBattleGround* UHexBattleConfig::FindGround(TCHAR Letter, bool bUnderground) const
{
	const FHexBattleGround* Fallback = nullptr;
	for (const FHexBattleGround& Ground : Grounds)
	{
		if (Ground.bUnderground != bUnderground)
		{
			continue;
		}
		if (Letter != 0 && Ground.Letters.Contains(FString::Chr(Letter), ESearchCase::CaseSensitive))
		{
			return &Ground;
		}
		if (Ground.Letters.IsEmpty() && !Fallback)
		{
			Fallback = &Ground;
		}
	}
	return Fallback ? Fallback : (Grounds.IsEmpty() ? nullptr : &Grounds[0]);
}

int32 UHexBattleConfig::FindSpell(const FString& Id) const
{
	return Spells.IndexOfByPredicate([&Id](const FHexSpell& Each) { return Each.Id.ToString().Equals(Id, ESearchCase::IgnoreCase); });
}

const UHexBattleConfig* UHexBattleConfig::Load()
{
	if (const UHexBattleConfig* Config = LoadObject<UHexBattleConfig>(nullptr, BattleConfigPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Config;
	}
	UE_LOG(LogHexHero, Warning, TEXT("Battles: %s is missing, taking the defaults of its class; run Tools/HexBattle/import_battle_units.py"), BattleConfigPath);
	return GetDefault<UHexBattleConfig>();
}
