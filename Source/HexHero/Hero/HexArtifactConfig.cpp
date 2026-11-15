// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexArtifactConfig.h"
#include "Engine/Texture2D.h"

EHexArtifactSlot GetWornSlotKind(int32 WornSlot)
{
	static const EHexArtifactSlot Kinds[NumWornSlots] = {EHexArtifactSlot::Head, EHexArtifactSlot::Neck, EHexArtifactSlot::Body, EHexArtifactSlot::Weapon,
		EHexArtifactSlot::Shield, EHexArtifactSlot::Cloak, EHexArtifactSlot::Feet, EHexArtifactSlot::Ring, EHexArtifactSlot::Ring, EHexArtifactSlot::Misc,
		EHexArtifactSlot::Misc, EHexArtifactSlot::Misc};
	return WornSlot >= 0 && WornSlot < NumWornSlots ? Kinds[WornSlot] : EHexArtifactSlot::Misc;
}

FString GetArtifactSlotName(EHexArtifactSlot Slot)
{
	switch (Slot)
	{
	case EHexArtifactSlot::Head: return TEXT("Head");
	case EHexArtifactSlot::Neck: return TEXT("Neck");
	case EHexArtifactSlot::Body: return TEXT("Body");
	case EHexArtifactSlot::Weapon: return TEXT("Weapon");
	case EHexArtifactSlot::Shield: return TEXT("Shield");
	case EHexArtifactSlot::Cloak: return TEXT("Cloak");
	case EHexArtifactSlot::Feet: return TEXT("Feet");
	case EHexArtifactSlot::Ring: return TEXT("Ring");
	default: return TEXT("Misc");
	}
}

FString GetArtifactRankName(EHexArtifactRank Rank)
{
	switch (Rank)
	{
	case EHexArtifactRank::Treasure: return TEXT("Treasure");
	case EHexArtifactRank::Minor: return TEXT("Minor");
	case EHexArtifactRank::Major: return TEXT("Major");
	default: return TEXT("Relic");
	}
}

FString FHexArtifact::DescribeEffect() const
{
	TArray<FString> Parts;
	const auto AddInt = [&Parts](int32 Value, const TCHAR* Name)
	{
		if (Value != 0)
		{
			Parts.Add(FString::Printf(TEXT("%s %+d"), Name, Value));
		}
	};
	AddInt(Attack, TEXT("Attack"));
	AddInt(Defense, TEXT("Defense"));
	AddInt(SpellPower, TEXT("Spell power"));
	AddInt(Knowledge, TEXT("Knowledge"));
	if (Movement != 0.0f)
	{
		Parts.Add(FString::Printf(TEXT("Movement %+g a day"), Movement));
	}
	if (ManaPerDay != 0)
	{
		Parts.Add(FString::Printf(TEXT("Mana %+d a day"), ManaPerDay));
	}
	AddInt(Morale, TEXT("Morale"));
	AddInt(Luck, TEXT("Luck"));
	static const TCHAR* Schools[4] = {TEXT("air"), TEXT("earth"), TEXT("fire"), TEXT("water")};
	if (School >= 0 && School < 4 && SchoolPower > 0.0f)
	{
		Parts.Add(FString::Printf(TEXT("Spells of %s %+d%%"), Schools[School], FMath::RoundToInt32(SchoolPower * 100.0f)));
	}
	return FString::Join(Parts, TEXT(", "));
}

void FHexArtifactBonus::Add(const FHexArtifact& Artifact)
{
	Attack += Artifact.Attack;
	Defense += Artifact.Defense;
	SpellPower += Artifact.SpellPower;
	Knowledge += Artifact.Knowledge;
	Movement += Artifact.Movement;
	ManaPerDay += Artifact.ManaPerDay;
	Morale += Artifact.Morale;
	Luck += Artifact.Luck;
	if (Artifact.School >= 0 && Artifact.School < 4)
	{
		SchoolPower[Artifact.School] += Artifact.SchoolPower;
	}
}

TArray<FHexArtifact> MakeDefaultArtifacts()
{
	TArray<FHexArtifact> Artifacts;
	// Claude's names and numbers, as the developer asked on 2026-11-15 for about thirty of the game's own
	const auto Add = [&Artifacts](const TCHAR* Id, const TCHAR* Name, EHexArtifactSlot Slot, EHexArtifactRank Rank, int32 Cost, const TCHAR* Lore) -> FHexArtifact&
	{
		FHexArtifact& Artifact = Artifacts.AddDefaulted_GetRef();
		Artifact.Id = Id;
		Artifact.DisplayName = Name;
		Artifact.Slot = Slot;
		Artifact.Rank = Rank;
		Artifact.Cost = Cost;
		Artifact.Lore = Lore;
		Artifact.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(TEXT("/Game/HexPlanet/UI/Artifacts/T_Art%s.T_Art%s"), Id, Id)));
		return Artifact;
	};
	using S = EHexArtifactSlot;
	using R = EHexArtifactRank;
	// the head
	Add(TEXT("SentinelHelm"), TEXT("Helm of the sentinel"), S::Head, R::Treasure, 1000, TEXT("Dented, and the better for it.")).Defense = 1;
	{
		FHexArtifact& Each = Add(TEXT("WatcherHood"), TEXT("Hood of the watcher"), S::Head, R::Minor, 2500, TEXT("Its wearer hears what the wind keeps."));
		Each.Knowledge = 1;
		Each.SpellPower = 1;
	}
	{
		FHexArtifact& Each = Add(TEXT("CrownOfDawn"), TEXT("Crown of dawn"), S::Head, R::Relic, 12000, TEXT("Forged at first light, it never cools."));
		Each.Knowledge = 3;
		Each.SpellPower = 2;
		Each.Morale = 1;
	}
	// the neck
	Add(TEXT("PendantOfCourage"), TEXT("Pendant of courage"), S::Neck, R::Treasure, 1500, TEXT("A soldier's token, worn smooth by thumbs.")).Morale = 1;
	{
		FHexArtifact& Each = Add(TEXT("EmberAmulet"), TEXT("Ember amulet"), S::Neck, R::Minor, 3000, TEXT("Warm to the touch in the coldest night."));
		Each.School = 2;
		Each.SchoolPower = 0.25f;
	}
	{
		FHexArtifact& Each = Add(TEXT("TideTorc"), TEXT("Torc of tides"), S::Neck, R::Minor, 3000, TEXT("It rises and falls with the sea."));
		Each.School = 3;
		Each.SchoolPower = 0.25f;
	}
	// the body
	Add(TEXT("RivetedHauberk"), TEXT("Riveted hauberk"), S::Body, R::Minor, 2500, TEXT("Ten thousand rings, each closed by hand.")).Defense = 2;
	Add(TEXT("RobeOfStars"), TEXT("Robe of stars"), S::Body, R::Minor, 3000, TEXT("Its stitches are the shape of the winter sky.")).SpellPower = 2;
	{
		FHexArtifact& Each = Add(TEXT("DragonscaleCuirass"), TEXT("Dragonscale cuirass"), S::Body, R::Major, 7500, TEXT("The beast did not give it willingly."));
		Each.Attack = 2;
		Each.Defense = 3;
	}
	// the weapon
	Add(TEXT("SoldierBlade"), TEXT("Soldier's blade"), S::Weapon, R::Treasure, 1000, TEXT("Plain steel that never failed its owner.")).Attack = 1;
	Add(TEXT("NorthernAxe"), TEXT("Axe of the north"), S::Weapon, R::Minor, 3000, TEXT("Its edge was ground on glacier stone.")).Attack = 2;
	{
		FHexArtifact& Each = Add(TEXT("StaffOfAsh"), TEXT("Staff of ash"), S::Weapon, R::Minor, 3500, TEXT("Cut from a tree struck by lightning."));
		Each.SpellPower = 2;
		Each.Knowledge = 1;
	}
	{
		FHexArtifact& Each = Add(TEXT("ThunderSpear"), TEXT("Thunder spear"), S::Weapon, R::Major, 8000, TEXT("It hums before a storm."));
		Each.Attack = 3;
		Each.School = 0;
		Each.SchoolPower = 0.25f;
	}
	{
		FHexArtifact& Each = Add(TEXT("SunforgedBlade"), TEXT("Sunforged blade"), S::Weapon, R::Relic, 14000, TEXT("No shadow falls where it is drawn."));
		Each.Attack = 5;
		Each.Defense = 2;
	}
	// the shield
	Add(TEXT("OakBuckler"), TEXT("Oak buckler"), S::Shield, R::Treasure, 1000, TEXT("Light, round and stubborn.")).Defense = 1;
	Add(TEXT("TowerShield"), TEXT("Tower shield"), S::Shield, R::Minor, 3000, TEXT("Two men could hide behind it.")).Defense = 3;
	{
		FHexArtifact& Each = Add(TEXT("AegisOfDawn"), TEXT("Aegis of dawn"), S::Shield, R::Major, 8500, TEXT("Arrows turn from it like moths from a flame."));
		Each.Defense = 4;
		Each.Morale = 1;
	}
	// the cloak
	Add(TEXT("TravellerCloak"), TEXT("Traveller's cloak"), S::Cloak, R::Minor, 2500, TEXT("Patched by every road it knows.")).Movement = 2.0f;
	{
		FHexArtifact& Each = Add(TEXT("CloakOfStone"), TEXT("Cloak of stone"), S::Cloak, R::Minor, 3500, TEXT("Heavy as a hill, and as steady."));
		Each.Defense = 1;
		Each.School = 1;
		Each.SchoolPower = 0.25f;
	}
	{
		FHexArtifact& Each = Add(TEXT("WindMantle"), TEXT("Mantle of winds"), S::Cloak, R::Major, 7000, TEXT("It is never still, even indoors."));
		Each.Movement = 1.0f;
		Each.School = 0;
		Each.SchoolPower = 0.35f;
	}
	// the feet
	{
		FHexArtifact& Each = Add(TEXT("PilgrimSandals"), TEXT("Pilgrim's sandals"), S::Feet, R::Treasure, 1500, TEXT("Worn thin on the way to every shrine."));
		Each.Movement = 1.0f;
		Each.ManaPerDay = 1;
	}
	Add(TEXT("RoadBoots"), TEXT("Boots of the road"), S::Feet, R::Minor, 3000, TEXT("They know the way better than their wearer.")).Movement = 2.0f;
	Add(TEXT("LeagueBoots"), TEXT("Seven-league boots"), S::Feet, R::Major, 7000, TEXT("Each stride is a morning's walk.")).Movement = 4.0f;
	// the rings
	Add(TEXT("RingOfInsight"), TEXT("Ring of insight"), S::Ring, R::Treasure, 1500, TEXT("A clear stone that shows what is hidden.")).Knowledge = 1;
	Add(TEXT("RingOfFortune"), TEXT("Ring of fortune"), S::Ring, R::Treasure, 1500, TEXT("Won at dice, and never lost since.")).Luck = 1;
	{
		FHexArtifact& Each = Add(TEXT("RingOfVigour"), TEXT("Ring of vigour"), S::Ring, R::Minor, 3000, TEXT("Its wearer wakes before the drums."));
		Each.Attack = 1;
		Each.Defense = 1;
	}
	{
		FHexArtifact& Each = Add(TEXT("SignetOfKings"), TEXT("Signet of kings"), S::Ring, R::Major, 7500, TEXT("Soldiers stand straighter when they see it."));
		Each.Morale = 1;
		Each.Luck = 1;
		Each.Attack = 1;
	}
	// what is carried besides
	Add(TEXT("CloverCharm"), TEXT("Four-leaf charm"), S::Misc, R::Treasure, 1000, TEXT("Pressed in a book for a hundred years.")).Luck = 1;
	Add(TEXT("EverfullFlask"), TEXT("Ever-full flask"), S::Misc, R::Minor, 3000, TEXT("Whatever was drunk from it comes back by dawn.")).ManaPerDay = 3;
	Add(TEXT("HornOfTheHost"), TEXT("Horn of the host"), S::Misc, R::Major, 7000, TEXT("When it sounds, nobody runs.")).Morale = 2;
	{
		FHexArtifact& Each = Add(TEXT("OrbOfDeepEarth"), TEXT("Orb of the deep earth"), S::Misc, R::Major, 8000, TEXT("Hold it still and feel the mountains breathe."));
		Each.School = 1;
		Each.SchoolPower = 0.5f;
	}
	{
		FHexArtifact& Each = Add(TEXT("HeartOfEmbers"), TEXT("Heart of embers"), S::Misc, R::Major, 8000, TEXT("It beats when fire is near."));
		Each.School = 2;
		Each.SchoolPower = 0.5f;
	}
	{
		FHexArtifact& Each = Add(TEXT("BookOfMists"), TEXT("Book of mists"), S::Misc, R::Major, 8000, TEXT("Its pages are damp, and its ink never dries."));
		Each.School = 3;
		Each.SchoolPower = 0.5f;
		Each.Knowledge = 1;
	}
	{
		FHexArtifact& Each = Add(TEXT("SeekerLantern"), TEXT("Lantern of the seeker"), S::Misc, R::Minor, 2500, TEXT("Its flame leans towards what was lost."));
		Each.Movement = 1.0f;
		Each.SpellPower = 1;
	}
	return Artifacts;
}
