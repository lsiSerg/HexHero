// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexBattleConfig.h"
#include "HexHero.h"

namespace
{
	const TCHAR* BattleConfigPath = TEXT("/Game/HexPlanet/Battle/DA_HexBattle.DA_HexBattle");

	FHexSpell MakeSpell(const TCHAR* Id, const TCHAR* Name, const TCHAR* Description, EHexSpellKind Kind, EHexSpellTarget Target, int32 Mana, const FLinearColor& Color)
	{
		FHexSpell Spell;
		Spell.Id = Id;
		Spell.DisplayName = Name;
		Spell.Description = Description;
		Spell.Kind = Kind;
		Spell.Target = Target;
		Spell.ManaCost = Mana;
		Spell.Color = Color;
		return Spell;
	}
}

TArray<FHexSpell> MakeDefaultSpells()
{
	TArray<FHexSpell> Spells;
	FHexSpell& Bolt = Spells.Add_GetRef(MakeSpell(TEXT("Bolt"), TEXT("Bolt"), TEXT("A bolt of light strikes one enemy stack"), EHexSpellKind::Damage, EHexSpellTarget::Enemy, 5,
		FLinearColor(0.6f, 0.8f, 1.0f, 1.0f)));
	Bolt.Power = 15;
	Bolt.PowerPerSpellPower = 12;
	FHexSpell& Blast = Spells.Add_GetRef(MakeSpell(TEXT("Blast"), TEXT("Blast"), TEXT("Fire bursts over a hex and the hexes around it, burning friend and foe"),
		EHexSpellKind::AreaDamage, EHexSpellTarget::Hex, 12, FLinearColor(1.0f, 0.45f, 0.1f, 1.0f)));
	Blast.Power = 10;
	Blast.PowerPerSpellPower = 10;
	Blast.Radius = 1;
	FHexSpell& Mend = Spells.Add_GetRef(MakeSpell(TEXT("Mend"), TEXT("Mend"), TEXT("Heals the wounded of a stack of your own"), EHexSpellKind::Heal, EHexSpellTarget::Friend, 6,
		FLinearColor(0.4f, 1.0f, 0.5f, 1.0f)));
	Mend.Power = 20;
	Mend.PowerPerSpellPower = 15;
	FHexSpell& Haste = Spells.Add_GetRef(MakeSpell(TEXT("Haste"), TEXT("Haste"), TEXT("A stack of your own is faster"), EHexSpellKind::Effect, EHexSpellTarget::Friend, 6,
		FLinearColor(1.0f, 0.9f, 0.3f, 1.0f)));
	Haste.SpeedAdd = 3;
	FHexSpell& Slow = Spells.Add_GetRef(MakeSpell(TEXT("Slow"), TEXT("Slow"), TEXT("An enemy stack is half as fast"), EHexSpellKind::Effect, EHexSpellTarget::Enemy, 6,
		FLinearColor(0.5f, 0.5f, 0.9f, 1.0f)));
	Slow.SpeedShare = 0.5f;
	FHexSpell& Blessing = Spells.Add_GetRef(MakeSpell(TEXT("Blessing"), TEXT("Blessing"), TEXT("A stack of your own deals the most damage it can"), EHexSpellKind::Effect,
		EHexSpellTarget::Friend, 5, FLinearColor(1.0f, 0.95f, 0.7f, 1.0f)));
	Blessing.bMaxDamage = true;
	FHexSpell& Weakness = Spells.Add_GetRef(MakeSpell(TEXT("Weakness"), TEXT("Weakness"), TEXT("An enemy stack has less Attack"), EHexSpellKind::Effect, EHexSpellTarget::Enemy, 6,
		FLinearColor(0.6f, 0.3f, 0.7f, 1.0f)));
	Weakness.AttackAdd = -4;
	FHexSpell& Ward = Spells.Add_GetRef(MakeSpell(TEXT("Ward"), TEXT("Ward"), TEXT("A stack of your own takes less damage"), EHexSpellKind::Effect, EHexSpellTarget::Friend, 5,
		FLinearColor(0.7f, 0.9f, 1.0f, 1.0f)));
	Ward.DamageTakenCut = 0.3f;
	return Spells;
}

UHexBattleConfig::UHexBattleConfig()
{
	Spells = MakeDefaultSpells();
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
	if (Spells.IsEmpty())
	{
		Spells = MakeDefaultSpells();
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
