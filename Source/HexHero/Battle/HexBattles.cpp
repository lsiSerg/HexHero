// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexBattles.h"
#include "HexBattleConfig.h"
#include "HexBiomeConfig.h"
#include "HexCastles.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexNeutrals.h"
#include "HexPlanet.h"

void UHexBattles::Init(UHexHeroes* InHeroes)
{
	Heroes = InHeroes;
	Config = UHexBattleConfig::Load();
	bShown = false;
	bPending = false;
}

float UHexBattles::GetArmyPower(TConstArrayView<FHexStack> Army) const
{
	const UHexCastleConfig* Castles = Heroes && Heroes->GetCastles() ? Heroes->GetCastles()->GetConfig() : nullptr;
	float Power = 0.0f;
	for (const FHexStack& Stack : Army)
	{
		const FHexCreatureStats* Stats = Castles ? Castles->GetStats(Stack) : nullptr;
		Power += Stats ? Stats->GetPower() * float(FMath::Max(Stack.Count, 0)) : 0.0f;
	}
	return Power;
}

bool UHexBattles::Begin(const FHexBattleRequest& InRequest, FString& OutMessage)
{
	UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
	FHexHero* Attacker = Heroes ? Heroes->FindHeroMutable(InRequest.Attacker) : nullptr;
	if (!Attacker || !Castles || !Config)
	{
		return false;
	}
	if (bShown || bPending)
	{
		OutMessage = TEXT("another battle is being fought");
		return false;
	}
	const auto MakeSide = [this](const FHexHero& Hero)
	{
		FHexBattleSideSetup Side;
		Side.Player = Hero.Player;
		Side.HeroId = Hero.Id;
		Side.HeroName = Hero.Name;
		Side.Name = Hero.Name;
		Side.Attack = Hero.Attack;
		Side.Defense = Hero.Defense;
		Side.SpellPower = Hero.SpellPower;
		Side.Mana = Hero.Mana;
		Side.Stacks = Hero.Army;
		Side.bAi = !Heroes->IsPlaying() || Heroes->IsPlayerAi(Hero.Player);
		return Side;
	};
	FHexBattleSetup Setup;
	Setup.Sides[0] = MakeSide(*Attacker);
	GuardSlots = 0;
	switch (InRequest.Kind)
	{
	case EHexBattleKind::Hero:
		{
			const FHexHero* Defender = Heroes->FindHero(InRequest.DefenderHero);
			if (!Defender)
			{
				return false;
			}
			Setup.Sides[1] = MakeSide(*Defender);
		}
		break;
	case EHexBattleKind::Castle:
		{
			const FHexCastle* Castle = Castles->FindCastle(InRequest.CastleId);
			if (!Castle)
			{
				return false;
			}
			const FHexHero* Guard = Castles->FindGuard(*Castle);
			FHexBattleSideSetup& Side = Setup.Sides[1];
			if (Guard)
			{
				Side = MakeSide(*Guard);
				GuardSlots = Guard->Army.Num();
			}
			Side.Player = Castle->Owner;
			Side.bAi = Castle->Owner < 0 || !Heroes->IsPlaying() || Heroes->IsPlayerAi(Castle->Owner);
			Side.Stacks.Append(Castle->Garrison);
			Side.DefenseFactor = Config->bCastleDefenseFactor ? Castles->GetFortFactor(*Castle) : 1.0f;
			// behind its wall, as strong as its fort, with towers and their shooters, as the developer asked on 2026-11-12
			const int32 Fort = FMath::Clamp(Castles->GetLevel(*Castle, EHexBuildingType::Fort), 0, 3);
			const auto ByFort = [Fort](const TArray<int32>& Values) { return Values.IsValidIndex(Fort) ? Values[Fort] : 0; };
			FHexBattleSiege& Siege = Setup.Siege;
			Siege.bWalls = Fort >= 1;
			Siege.WallHealth = ByFort(Config->WallHealthByFort);
			Siege.GateHealth = ByFort(Config->GateHealthByFort);
			Siege.TowerHealth = ByFort(Config->TowerHealthByFort);
			Siege.Towers = Siege.bWalls ? ByFort(Config->TowersByFort) : 0;
			const UHexCastleConfig* CastleConfig = Castles->GetConfig();
			if (Siege.Towers > 0 && CastleConfig->Factions.IsValidIndex(Castle->Faction))
			{
				// the shooters of the lowest level of the faction, so many weeks of what their dwelling offers
				const FHexFaction& Faction = CastleConfig->Factions[Castle->Faction];
				const int32 Shooter = Faction.Creatures.IndexOfByPredicate([](const FHexCreature& Each) { return Each.bShoots; });
				if (Shooter != INDEX_NONE)
				{
					const float Weeks = Config->TowerWeeksByFort.IsValidIndex(Fort) ? Config->TowerWeeksByFort[Fort] : 1.0f;
					Siege.TowerStack = FHexStack{Castle->Faction, Shooter, false, FMath::Max(FMath::RoundToInt32(Faction.Creatures[Shooter].Growth * Weeks), 1)};
				}
			}
			const FString Faction = Castles->GetConfig()->Factions.IsValidIndex(Castle->Faction) ? Castles->GetConfig()->Factions[Castle->Faction].DisplayName : FString();
			Side.Name = Guard ? FString::Printf(TEXT("%s in the castle"), *Guard->Name) : FString::Printf(TEXT("Castle of the %s"), *Faction);
		}
		break;
	case EHexBattleKind::Neutral:
		{
			const FHexNeutral* Neutral = Heroes->GetNeutrals() ? Heroes->GetNeutrals()->Find(InRequest.NeutralId) : nullptr;
			if (!Neutral)
			{
				return false;
			}
			FHexBattleSideSetup& Side = Setup.Sides[1];
			Side.Player = -1;
			Side.Stacks = {Neutral->Stack};
			Side.bAi = true;
			Side.Name = FString::Printf(TEXT("%s %s"), *UHexNeutrals::DescribeCount(Neutral->Stack.Count), *UHexNeutrals::Plural(Heroes->GetNeutrals()->GetCreatureName(Neutral->Stack)).ToLower());
		}
		break;
	}
	const auto HasArmy = [](const FHexBattleSideSetup& Side) { return Side.Stacks.ContainsByPredicate([](const FHexStack& Each) { return Each.Count > 0; }); };
	if (!HasArmy(Setup.Sides[1]))
	{
		OutMessage = TEXT("nothing defends it");
		return false;
	}
	if (!HasArmy(Setup.Sides[0]))
	{
		OutMessage = FString::Printf(TEXT("%s has no army to fight with"), *Attacker->Name);
		return false;
	}
	// the catapult of the attacker, at the walls of a castle
	Setup.Siege.bCatapult = Attacker->bCatapult && Setup.Siege.bWalls;
	Setup.Siege.bMoat = Setup.Siege.bWalls && Config->bSiegeMoat;
	Request = InRequest;
	Setup.Seed = HashCombine(HashCombine(GetTypeHash(Heroes->GetDay()), GetTypeHash(InRequest.Cell)), HashCombine(GetTypeHash(InRequest.Attacker), GetTypeHash(++Count)));
	// the ground of the field is the biome where it is fought
	GroundLetter = 0;
	if (const AHexPlanet* Planet = Heroes->GetPlanet(InRequest.Layer); Planet && Planet->GetGrid().GetCells().IsValidIndex(InRequest.Cell))
	{
		const int32 Biome = Planet->GetCellBiome(InRequest.Cell);
		if (Planet->GetBiomes().IsValidIndex(Biome) && Planet->GetBiomes()[Biome])
		{
			const FString& Letter = InRequest.Layer == EHexPlanetLayer::Underground ? Planet->GetBiomes()[Biome]->UndergroundLetter : Planet->GetBiomes()[Biome]->PlanetLetter;
			GroundLetter = Letter.IsEmpty() ? 0 : Letter[0];
			TerrainLayer = Planet->GetBiomes()[Biome]->TerrainLayer;
			RockLayer = Planet->GetBiomes()[Biome]->MountainRockLayer;
		}
	}
	Battle.Start(Setup, Castles->GetConfig(), Config);
	bAuto[0] = bAuto[1] = false;
	// only a battle a person fights is shown; one the game fights on both sides is fought at once, even where the person sees it, as the
	// developer asked on 2026-11-12 (it was shown to the person who saw it before)
	const bool bPerson = !Setup.Sides[0].bAi || !Setup.Sides[1].bAi;
	UE_LOG(LogHexHero, Log, TEXT("Battles: %s attacks %s on cell %d of the %s, %s"), *GetSideName(0), *GetSideName(1), InRequest.Cell, GetPlanetLayerName(InRequest.Layer),
		bPerson ? TEXT("a person fights") : TEXT("fought at once"));
	if (bPerson)
	{
		bShown = true;
		OnShown.Broadcast();
		return true;
	}
	Battle.PlayToEnd();
	++QuickCount;
	bPending = true;
	return true;
}

const FHexCastle* UHexBattles::GetCastle() const
{
	const UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
	return Castles && Request.Kind == EHexBattleKind::Castle ? Castles->FindCastle(Request.CastleId) : nullptr;
}

int32 UHexBattles::GetFortLevel() const
{
	const FHexCastle* Castle = GetCastle();
	return Castle ? FMath::Clamp(Heroes->GetCastles()->GetLevel(*Castle, EHexBuildingType::Fort), 0, 3) : 0;
}

int32 UHexBattles::GetPersonSide() const
{
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!Battle.GetSide(Side).bAi)
		{
			return Side;
		}
	}
	return INDEX_NONE;
}

void UHexBattles::PlayQuick()
{
	if (bShown && !Battle.IsOver())
	{
		Battle.PlayToEnd();
	}
}

void UHexBattles::Close()
{
	if (!bShown || !Battle.IsOver())
	{
		return;
	}
	bShown = false;
	bPending = true;
	OnClosed.Broadcast();
}

bool UHexBattles::ApplyPending()
{
	if (bPending && !bShown)
	{
		bPending = false;
		Apply();
	}
	return bShown;
}

FString UHexBattles::GetSideName(int32 Side) const
{
	const FHexBattleSideSetup& Each = Battle.GetSide(Side);
	return Each.Name.IsEmpty() ? Each.HeroName : Each.Name;
}

FLinearColor UHexBattles::GetSideColor(int32 Side) const
{
	const int32 Player = Battle.GetSide(Side).Player;
	const UHexHeroConfig* HeroConfig = Heroes ? Heroes->GetConfig() : nullptr;
	return Player >= 0 && HeroConfig && HeroConfig->Players.IsValidIndex(Player) ? HeroConfig->Players[Player].Color : FLinearColor(0.55f, 0.55f, 0.55f, 1.0f);
}

FString UHexBattles::GetResultText() const
{
	if (!Battle.IsOver())
	{
		return FString();
	}
	const int32 Winner = Battle.GetWinner();
	const int32 Loser = 1 - Winner;
	if (Battle.GetRetreated() != INDEX_NONE)
	{
		return FString::Printf(TEXT("%s retreats from %s and leaves the map without its army"), *GetSideName(Loser), *GetSideName(Winner));
	}
	int32 Lost = 0;
	int32 Had = 0;
	for (const FHexBattleUnit& Unit : Battle.GetUnits())
	{
		if (Unit.Side == Winner)
		{
			Lost += Unit.StartCount - Unit.Count;
			Had += Unit.StartCount;
		}
	}
	return FString::Printf(TEXT("%s wins against %s in %d rounds, losing %d of %d creatures"), *GetSideName(Winner), *GetSideName(Loser), Battle.GetRound(), Lost, Had);
}

void UHexBattles::Apply()
{
	UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
	if (!Castles || !Battle.IsOver())
	{
		return;
	}
	const int32 Winner = Battle.GetWinner();
	const FString Text = GetResultText();
	const bool bTold = !Battle.GetSide(0).bAi || !Battle.GetSide(1).bAi || Heroes->IsCellSeen(Request.Layer, Request.Cell);
	// what is left of an army goes back to it, by its stacks
	const auto Survivors = [this](int32 Side, int32 From, int32 To)
	{
		TArray<FHexStack> Left;
		const TArray<FHexStack>& Stacks = Battle.GetSide(Side).Stacks;
		for (int32 Slot = From; Slot < FMath::Min(To, Stacks.Num()); ++Slot)
		{
			FHexStack Stack = Stacks[Slot];
			Stack.Count = Battle.GetSlotCount(Side, Slot);
			if (Stack.Count > 0)
			{
				Left.Add(Stack);
			}
		}
		return Left;
	};
	const auto Reward = [&](FHexHero& Hero, int32 Side)
	{
		Hero.Army = Survivors(Side, 0, Battle.GetSide(Side).Stacks.Num());
		Hero.Mana = Battle.GetMana(Side);
		Hero.Experience += FMath::RoundToInt32(Battle.GetKilledHealth(Side) * Config->ExperiencePerHealth);
	};
	FHexHero* Attacker = Heroes->FindHeroMutable(Request.Attacker);
	if (Attacker && Winner == 0)
	{
		Reward(*Attacker, 0);
	}
	// a catapult destroyed at the walls is lost
	if (Attacker && Battle.GetUnits().ContainsByPredicate([](const FHexBattleUnit& Each) { return Each.bCatapult && !Each.IsAlive(); }))
	{
		Attacker->bCatapult = false;
	}
	TArray<int32> Leaving;
	switch (Request.Kind)
	{
	case EHexBattleKind::Hero:
		if (FHexHero* Defender = Heroes->FindHeroMutable(Request.DefenderHero))
		{
			if (Winner == 1)
			{
				Reward(*Defender, 1);
			}
			Leaving.Add(Winner == 0 ? Defender->Id : Request.Attacker);
		}
		break;
	case EHexBattleKind::Castle:
		if (FHexCastle* Castle = Castles->FindCastleMutable(Request.CastleId))
		{
			const FHexHero* Guard = Castles->FindGuard(*Castle);
			if (Winner == 0)
			{
				// the guard leaves the map with the army it lost, the garrison is gone, and the hero that won goes in and takes the castle
				if (Guard)
				{
					Leaving.Add(Guard->Id);
					if (FHexHero* Beaten = Heroes->FindHeroMutable(Guard->Id))
					{
						Beaten->CastleId = 0;
						Beaten->Army.Reset();
					}
				}
				Castle->Garrison.Reset();
				if (Attacker)
				{
					const FString Taken = Castles->Visit(*Attacker, Castle->Id, Request.Cell, bTold);
					if (Attacker->Id == Heroes->SelectedId && Heroes->IsPlaying())
					{
						Heroes->SelectedCastleId = Attacker->CastleId;
					}
					if (!Taken.IsEmpty() && bTold)
					{
						Heroes->OnMessage.Broadcast(Taken);
					}
				}
				Heroes->ExploreCastles();
			}
			else
			{
				if (Guard)
				{
					if (FHexHero* Kept = Heroes->FindHeroMutable(Guard->Id))
					{
						Kept->Army = Survivors(1, 0, GuardSlots);
						Kept->Mana = Battle.GetMana(1);
						Kept->Experience += FMath::RoundToInt32(Battle.GetKilledHealth(1) * Config->ExperiencePerHealth);
					}
				}
				Castle->Garrison = Survivors(1, GuardSlots, Battle.GetSide(1).Stacks.Num());
				Leaving.Add(Request.Attacker);
			}
			++Castles->Revision;
		}
		break;
	case EHexBattleKind::Neutral:
		if (UHexNeutrals* Neutrals = Heroes->GetNeutrals())
		{
			if (Winner == 0)
			{
				Neutrals->Remove(Request.NeutralId);
			}
			else
			{
				Neutrals->SetCount(Request.NeutralId, Battle.GetSlotCount(1, 0));
				Leaving.Add(Request.Attacker);
			}
		}
		break;
	}
	// who lost or retreated leaves the map with its army
	for (const int32 Id : Leaving)
	{
		if (FHexHero* Beaten = Heroes->FindHeroMutable(Id))
		{
			Beaten->Army.Reset();
		}
		Heroes->RemoveHero(Id);
	}
	if (bTold)
	{
		Heroes->OnMessage.Broadcast(Text);
	}
	UE_LOG(LogHexHero, Log, TEXT("Battles: %s"), *Text);
	UHexHeroes::NoteMapChanged();
	++Heroes->ListRevision;
	Heroes->KeepChosen();
}
