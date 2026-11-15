// Copyright Epic Games, Inc. All Rights Reserved.

// The magic and the growth of heroes, as the developer asked on 2026-11-15: the skills they choose as they rise in level, the spells they
// learn in the mage guilds of castles, their mana, and what both give them in a battle (UHexHeroes, UHexCastles)

#include "HexBattle.h"
#include "HexBattleConfig.h"
#include "HexCastles.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexBattles.h"
#include "HexNeutrals.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "HexShroud.h"
#include "HexPlanet.h"
#include "HexPlanetUnderground.h"
#include "Engine/Texture2D.h"

namespace
{
	/** The skill of the school of magic */
	EHexSkill GetSchoolSkill(EHexSpellSchool School)
	{
		switch (School)
		{
		case EHexSpellSchool::Air: return EHexSkill::AirMagic;
		case EHexSpellSchool::Earth: return EHexSkill::EarthMagic;
		case EHexSpellSchool::Fire: return EHexSkill::FireMagic;
		default: return EHexSkill::WaterMagic;
		}
	}

	/** The names of the castles of each faction, by its Id, Claude's */
	const TArray<FString>& GetCastleNames(FName Faction)
	{
		static const TArray<FString> Dawn = {TEXT("Sunhold"), TEXT("Brightwatch"), TEXT("Aurelgard"), TEXT("Dawnspire"), TEXT("Lightmere"), TEXT("Goldhaven"),
			TEXT("Whitecliff"), TEXT("Halewick")};
		static const TArray<FString> Decay = {TEXT("Grimbarrow"), TEXT("Ashfen"), TEXT("Duskhollow"), TEXT("Rotmere"), TEXT("Gravenhall"), TEXT("Blightmoor"),
			TEXT("Mournkeep"), TEXT("Cinderveil")};
		return Faction == TEXT("Decay") ? Decay : Dawn;
	}
}

int32 UHexHeroes::GetSkillLevel(const FHexHero& Hero, EHexSkill Skill) const
{
	const FHexHeroSkill* Found = Hero.Skills.FindByPredicate([Skill](const FHexHeroSkill& Each) { return Each.Skill == Skill; });
	return Found ? Found->Level : 0;
}

float UHexHeroes::GetSkillValue(const FHexHero& Hero, EHexSkill Skill, bool bSecond) const
{
	const FHexSkillInfo* Info = Config ? Config->FindSkill(Skill) : nullptr;
	return Info ? Info->GetValue(GetSkillLevel(Hero, Skill), bSecond) : 0.0f;
}

int32 UHexHeroes::GetMaxMana(const FHexHero& Hero) const
{
	return FMath::Max(Hero.Knowledge, 0) * (Config ? Config->ManaPerKnowledge : 10);
}

float UHexHeroes::AdjustForPathfinding(const FHexHero* Hero, float Cost) const
{
	// what rough ground costs over plains, less what Pathfinding takes off
	if (!Hero || Cost <= 1.0f)
	{
		return Cost;
	}
	const float Cut = FMath::Clamp(GetSkillValue(*Hero, EHexSkill::Pathfinding), 0.0f, 1.0f);
	return Cut > 0.0f ? 1.0f + (Cost - 1.0f) * (1.0f - Cut) : Cost;
}

void UHexHeroes::RestoreMana(FHexHero& Hero) const
{
	const int32 Most = GetMaxMana(Hero);
	// in a castle of its own with a mage guild all of it comes back
	const FHexCastle* Castle = Castles && Hero.CastleId != 0 ? Castles->FindCastle(Hero.CastleId) : nullptr;
	if (Castle && Castle->Owner == Hero.Player && Castles->GetGuildLevel(*Castle) > 0)
	{
		Hero.Mana = FMath::Max(Hero.Mana, Most);
		return;
	}
	const int32 Back = (Config ? Config->ManaPerDay : 1) + FMath::RoundToInt32(GetSkillValue(Hero, EHexSkill::Mysticism)) + GetArtifactBonus(Hero).ManaPerDay;
	Hero.Mana = FMath::Max(Hero.Mana, FMath::Min(Hero.Mana + Back, Most));
}

int32 UHexHeroes::LearnSpells(FHexHero& Hero, const TArray<FName>& Spells)
{
	int32 Learned = 0;
	for (const FName& Spell : Spells)
	{
		if (!Spell.IsNone() && !Hero.Spells.Contains(Spell))
		{
			Hero.Spells.Add(Spell);
			++Learned;
		}
	}
	if (Learned > 0)
	{
		++ListRevision;
	}
	return Learned;
}

void UHexHeroes::FillBattleMagic(const FHexHero& Hero, FHexBattleSideSetup& Side, const UHexBattleConfig* Battle) const
{
	Side.bAllSpells = false;
	Side.KnownSpells.Reset();
	if (Battle)
	{
		for (const FName& Spell : Hero.Spells)
		{
			const int32 Index = Battle->FindSpell(Spell.ToString());
			if (Index != INDEX_NONE && !Battle->Spells[Index].IsAdventure())
			{
				Side.KnownSpells.AddUnique(Index);
			}
		}
	}
	for (int32 School = 0; School < int32(EHexSpellSchool::Num); ++School)
	{
		const EHexSkill Skill = GetSchoolSkill(EHexSpellSchool(School));
		Side.SchoolManaCut[School] = GetSkillValue(Hero, Skill);
		Side.SchoolPower[School] = 1.0f + GetSkillValue(Hero, Skill, true);
		Side.bSchoolMass[School] = GetSkillLevel(Hero, Skill) >= 3;
	}
	Side.SpellDamageBonus = GetSkillValue(Hero, EHexSkill::Sorcery);
	// and what its artifacts give: morale and luck to its stacks, and power to the spells of a school
	const FHexArtifactBonus Bonus = GetArtifactBonus(Hero);
	for (int32 School = 0; School < int32(EHexSpellSchool::Num); ++School)
	{
		Side.SchoolPower[School] += Bonus.SchoolPower[School];
	}
	Side.Morale = Bonus.Morale;
	Side.Luck = Bonus.Luck;
}

TArray<FHexHeroSkill> UHexHeroes::MakeLevelOffers(const FHexHero& Hero, FRandomStream& Random) const
{
	TArray<FHexHeroSkill> Offers;
	if (!Config)
	{
		return Offers;
	}
	const FHexHeroClass& Class = Config->GetClass(Hero.Class);
	const auto Weight = [&Class](EHexSkill Skill) { return Class.SkillWeights.IsValidIndex(int32(Skill)) ? FMath::Max(Class.SkillWeights[int32(Skill)], 0.0f) : 1.0f; };
	TArray<FHexHeroSkill> Upgrades;
	TArray<FHexHeroSkill> New;
	for (const FHexSkillInfo& Info : Config->Skills)
	{
		const int32 Level = GetSkillLevel(Hero, Info.Skill);
		if (Level > 0 && Level < 3)
		{
			Upgrades.Add({Info.Skill, Level + 1});
		}
		else if (Level == 0 && Hero.Skills.Num() < Config->MaxSkills)
		{
			New.Add({Info.Skill, 1});
		}
	}
	const auto Pick = [&](TArray<FHexHeroSkill>& From)
	{
		float Total = 0.0f;
		for (const FHexHeroSkill& Each : From)
		{
			Total += Weight(Each.Skill) + 0.01f;
		}
		float Roll = Random.FRand() * Total;
		for (int32 Index = 0; Index < From.Num(); ++Index)
		{
			Roll -= Weight(From[Index].Skill) + 0.01f;
			if (Roll <= 0.0f || Index == From.Num() - 1)
			{
				Offers.Add(From[Index]);
				From.RemoveAt(Index);
				return;
			}
		}
	};
	// one a level higher of those it has and one it lacks, as Heroes III offers them, else two of whichever there are
	if (!Upgrades.IsEmpty())
	{
		Pick(Upgrades);
	}
	if (!New.IsEmpty())
	{
		Pick(New);
	}
	while (Offers.Num() < 2 && (!Upgrades.IsEmpty() || !New.IsEmpty()))
	{
		Pick(Upgrades.IsEmpty() ? New : Upgrades);
	}
	return Offers;
}

void UHexHeroes::ApplySkill(FHexHero& Hero, const FHexHeroSkill& Offer) const
{
	if (FHexHeroSkill* Found = Hero.Skills.FindByPredicate([&Offer](const FHexHeroSkill& Each) { return Each.Skill == Offer.Skill; }))
	{
		Found->Level = FMath::Clamp(FMath::Max(Found->Level, Offer.Level), 1, 3);
	}
	else if (!Config || Hero.Skills.Num() < Config->MaxSkills)
	{
		Hero.Skills.Add({Offer.Skill, FMath::Clamp(Offer.Level, 1, 3)});
	}
}

void UHexHeroes::AddExperience(int32 HeroId, int32 Amount)
{
	if (FHexHero* Hero = FindHeroMutable(HeroId))
	{
		AddExperience(*Hero, Amount);
	}
}

void UHexHeroes::AddExperience(FHexHero& Hero, int32 Amount)
{
	if (!Config || Amount <= 0)
	{
		return;
	}
	Hero.Experience += Amount;
	const FHexHeroClass& Class = Config->GetClass(Hero.Class);
	while (Hero.Level < Config->MaxLevel && Hero.Experience >= Config->GetLevelExperience(Hero.Level + 1))
	{
		++Hero.Level;
		// one attribute rises by the chances of its class, the same for the same hero and level
		FRandomStream Random(int32(Hero.Id * 7919 + Hero.Level * 104729 + Hero.Class * 31));
		const float Chances[4] = {FMath::Max(Class.AttackChance, 0.0f), FMath::Max(Class.DefenseChance, 0.0f), FMath::Max(Class.SpellPowerChance, 0.0f),
			FMath::Max(Class.KnowledgeChance, 0.0f)};
		const float Total = Chances[0] + Chances[1] + Chances[2] + Chances[3];
		float Roll = Random.FRand() * FMath::Max(Total, 0.01f);
		int32 Attribute = 0;
		for (; Attribute < 3 && Roll > Chances[Attribute]; ++Attribute)
		{
			Roll -= Chances[Attribute];
		}
		FHexLevelUp LevelUp;
		LevelUp.Level = Hero.Level;
		switch (Attribute)
		{
		case 0: ++Hero.Attack; LevelUp.Raised = TEXT("Attack +1"); break;
		case 1: ++Hero.Defense; LevelUp.Raised = TEXT("Defense +1"); break;
		case 2: ++Hero.SpellPower; LevelUp.Raised = TEXT("Spell power +1"); break;
		default: ++Hero.Knowledge; LevelUp.Raised = TEXT("Knowledge +1"); break;
		}
		LevelUp.Offers = MakeLevelOffers(Hero, Random);
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s rises to level %d, %s, offered %d skills"), *Hero.Name, Hero.Level, *LevelUp.Raised, LevelUp.Offers.Num());
		if (LevelUp.Offers.IsEmpty())
		{
			OnMessage.Broadcast(FString::Printf(TEXT("%s rises to level %d: %s"), *Hero.Name, Hero.Level, *LevelUp.Raised));
		}
		else if (IsPlayerAi(Hero.Player))
		{
			// the game takes what its class likes most
			const FHexHeroSkill* Best = &LevelUp.Offers[0];
			for (const FHexHeroSkill& Offer : LevelUp.Offers)
			{
				const auto Weight = [&Class](EHexSkill Skill) { return Class.SkillWeights.IsValidIndex(int32(Skill)) ? Class.SkillWeights[int32(Skill)] : 1.0f; };
				if (Weight(Offer.Skill) * (Offer.Level > 1 ? 1.2f : 1.0f) > Weight(Best->Skill) * (Best->Level > 1 ? 1.2f : 1.0f))
				{
					Best = &Offer;
				}
			}
			ApplySkill(Hero, *Best);
		}
		else
		{
			Hero.PendingLevelUps.Add(MoveTemp(LevelUp));
		}
		OnLevelUp.Broadcast(Hero.Id);
	}
	++ListRevision;
}

bool UHexHeroes::ChooseLevelUp(int32 HeroId, int32 Offer, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(HeroId);
	if (!Hero || Hero->PendingLevelUps.IsEmpty() || !Hero->PendingLevelUps[0].Offers.IsValidIndex(Offer))
	{
		OutMessage = TEXT("There is no such choice");
		return false;
	}
	const FHexLevelUp LevelUp = Hero->PendingLevelUps[0];
	Hero->PendingLevelUps.RemoveAt(0);
	const FHexHeroSkill& Skill = LevelUp.Offers[Offer];
	ApplySkill(*Hero, Skill);
	const FHexSkillInfo* Info = Config->FindSkill(Skill.Skill);
	OutMessage = FString::Printf(TEXT("%s, level %d: %s %s"), *Hero->Name, LevelUp.Level, GetSkillLevelName(Skill.Level), Info ? *Info->DisplayName : TEXT("?"));
	// the levels it rose meanwhile offer anew from the skills it has now, as their offers, made as it rose, gave again what was just chosen and
	// its choice was lost, as the developer found on 2026-11-15; a level with nothing left to offer goes
	for (int32 Index = 0; Index < Hero->PendingLevelUps.Num(); ++Index)
	{
		FHexLevelUp& Next = Hero->PendingLevelUps[Index];
		FRandomStream Random(int32(Hero->Id * 7919 + Next.Level * 104729 + Hero->Class * 31 + 17));
		Next.Offers = MakeLevelOffers(*Hero, Random);
		if (Next.Offers.IsEmpty())
		{
			OnMessage.Broadcast(FString::Printf(TEXT("%s rises to level %d: %s"), *Hero->Name, Next.Level, *Next.Raised));
			Hero->PendingLevelUps.RemoveAt(Index--);
		}
	}
	++ListRevision;
	++MapRevision;
	return true;
}

const FHexHero* UHexHeroes::FindPendingLevelUp() const
{
	if (!IsPlaying() || !IsPersonTurn())
	{
		return nullptr;
	}
	const int32 Player = GetShroudPlayer();
	for (const FHexHero& Hero : Heroes)
	{
		if (Hero.Player == Player && !Hero.PendingLevelUps.IsEmpty())
		{
			return &Hero;
		}
	}
	return nullptr;
}

TArray<FName> UHexCastles::GetGuildSpells(const FHexCastle& Castle) const
{
	TArray<FName> Spells;
	const int32 Level = GetGuildLevel(Castle);
	for (int32 Each = 0; Each < Level && Each < Castle.GuildSpells.Num(); ++Each)
	{
		Spells.Append(Castle.GuildSpells[Each]);
	}
	return Spells;
}

void UHexCastles::RollGuildSpells(FHexCastle& Castle) const
{
	Castle.GuildSpells.Reset();
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	if (!Battle || !Config)
	{
		return;
	}
	// the same castle teaches the same spells in every game
	FRandomStream Random(int32(Castle.Id * 7919 + Castle.Faction * 131 + 4243));
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		TArray<FName> Of;
		for (const FHexSpell& Spell : Battle->Spells)
		{
			if (Spell.Level == Level)
			{
				Of.Add(Spell.Id);
			}
		}
		const int32 Count = Config->GuildSpellsPerLevel.IsValidIndex(Level - 1) ? Config->GuildSpellsPerLevel[Level - 1] : 1;
		TArray<FName>& Chosen = Castle.GuildSpells.AddDefaulted_GetRef();
		while (Chosen.Num() < Count && !Of.IsEmpty())
		{
			const int32 Pick = Random.RandRange(0, Of.Num() - 1);
			Chosen.Add(Of[Pick]);
			Of.RemoveAt(Pick);
		}
	}
}

int32 UHexCastles::TeachSpells(FHexHero& Hero, const FHexCastle& Castle, bool bShown) const
{
	if (!Heroes || Castle.Owner != Hero.Player || GetGuildLevel(Castle) <= 0)
	{
		return 0;
	}
	Hero.Mana = FMath::Max(Hero.Mana, Heroes->GetMaxMana(Hero));
	const int32 Learned = Heroes->LearnSpells(Hero, GetGuildSpells(Castle));
	if (Learned > 0 && bShown)
	{
		Heroes->OnMessage.Broadcast(FString::Printf(TEXT("%s learns %d spells in the mage guild of %s"), *Hero.Name, Learned, *GetCastleName(Castle)));
	}
	return Learned;
}

FString UHexCastles::GetCastleName(const FHexCastle& Castle) const
{
	if (!Config || !Config->Factions.IsValidIndex(Castle.Faction))
	{
		return TEXT("Castle");
	}
	// by its place among the castles of its faction, so no two of one planet share a name while there are names enough
	int32 Place = 0;
	for (const FHexCastle& Other : Castles)
	{
		if (Other.Faction == Castle.Faction && Other.Id < Castle.Id)
		{
			++Place;
		}
	}
	const TArray<FString>& Names = GetCastleNames(Config->Factions[Castle.Faction].Id);
	const FString& Name = Names[Place % Names.Num()];
	return Place < Names.Num() ? Name : FString::Printf(TEXT("%s %d"), *Name, Place / Names.Num() + 1);
}

bool UHexHeroes::MakeJoinOffer(const FHexHero& Hero, const FHexNeutral& Neutral, FHexHeroOffer& OutOffer) const
{
	if (!Config || !Castles || !Sites)
	{
		return false;
	}
	const UHexCastleConfig* CastleConfig = Castles->GetConfig();
	const FHexCreatureStats* Stats = CastleConfig ? CastleConfig->GetStats(Neutral.Stack) : nullptr;
	if (!Stats || Castles->GetPower(Hero.Army) < Config->JoinPowerRatio * Castles->GetPower({Neutral.Stack}))
	{
		return false;
	}
	// room for them in the army
	TArray<FHexStack> Army = Hero.Army;
	if (!UHexCastles::AddToArmy(Army, Neutral.Stack))
	{
		return false;
	}
	// the same hero and stack get the same answer
	FRandomStream Random(int32(Neutral.Id * 7919 + Hero.Id * 131 + 17));
	const int32 Diplomacy = GetSkillLevel(Hero, EHexSkill::Diplomacy);
	const float Roll = Random.FRand();
	if (Diplomacy <= 0 && Roll >= Config->JoinBuyChance)
	{
		return false;
	}
	OutOffer = FHexHeroOffer();
	OutOffer.HeroId = Hero.Id;
	OutOffer.NeutralId = Neutral.Id;
	OutOffer.Stack = Neutral.Stack;
	OutOffer.Layer = Neutral.Layer;
	OutOffer.Cell = Neutral.Cell;
	OutOffer.bFree = Diplomacy > 0 && Random.FRand() < GetSkillValue(Hero, EHexSkill::Diplomacy);
	const float Discount = FMath::Clamp(GetSkillValue(Hero, EHexSkill::Diplomacy, true), 0.0f, 1.0f);
	OutOffer.Gold = OutOffer.bFree ? 0 : FMath::Max(FMath::RoundToInt32(Stats->Gold * Neutral.Stack.Count * (1.0f - Discount) / 10.0f) * 10, 10);
	return true;
}

bool UHexHeroes::AnswerOffer(bool bYes, FString& OutMessage)
{
	if (!PendingOffer.IsSet())
	{
		OutMessage = TEXT("Nothing waits for an answer");
		return false;
	}
	// a shipyard builds a boat, or the hero goes on its way
	if (PendingOffer.IsShipyard())
	{
		const FHexHeroOffer Yard = PendingOffer;
		PendingOffer = FHexHeroOffer();
		++MapRevision;
		return bYes && BuildBoat(Yard, OutMessage);
	}
	if (PendingOffer.IsChest())
	{
		const FHexHeroOffer Chest = PendingOffer;
		PendingOffer = FHexHeroOffer();
		FHexHero* Hero = FindHeroMutable(Chest.HeroId);
		if (!Hero || !Sites)
		{
			return false;
		}
		OutMessage = Sites->TakeChest(Hero->Player, Hero->Name, Chest.Layer, Chest.Cell, bYes, true);
		if (!bYes)
		{
			AddExperience(*Hero, Chest.Experience);
		}
		++MapRevision;
		++ListRevision;
		return !OutMessage.IsEmpty();
	}
	return bYes ? JoinNeutral(OutMessage) : FightNeutral(OutMessage);
}

bool UHexHeroes::JoinNeutral(FString& OutMessage)
{
	const FHexHeroOffer Join = PendingOffer;
	PendingOffer = FHexHeroOffer();
	FHexHero* Hero = FindHeroMutable(Join.HeroId);
	const FHexNeutral* Neutral = Neutrals ? Neutrals->Find(Join.NeutralId) : nullptr;
	if (!Hero || !Neutral || !Sites)
	{
		return false;
	}
	TMap<FName, int32> Price;
	if (Join.Gold > 0)
	{
		Price.Add(TEXT("Gold"), Join.Gold);
		if (!Sites->CanPay(Hero->Player, Price))
		{
			OutMessage = FString::Printf(TEXT("%s has not the %d gold they ask"), *Hero->Name, Join.Gold);
			RefusedJoins.Add(Join.NeutralId);
			return false;
		}
	}
	if (!UHexCastles::AddToArmy(Hero->Army, Neutral->Stack))
	{
		OutMessage = FString::Printf(TEXT("The army of %s has no room for them"), *Hero->Name);
		return false;
	}
	if (Join.Gold > 0)
	{
		Sites->Pay(Hero->Player, Price);
	}
	const FString Name = Neutrals->GetCreatureName(Neutral->Stack);
	OutMessage = Join.bFree ? FString::Printf(TEXT("%d %s join %s"), Join.Stack.Count, *UHexNeutrals::Plural(Name).ToLower(), *Hero->Name)
		: FString::Printf(TEXT("%d %s join %s for %d gold"), Join.Stack.Count, *UHexNeutrals::Plural(Name).ToLower(), *Hero->Name, Join.Gold);
	Neutrals->Remove(Join.NeutralId);
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s"), *OutMessage);
	OnMessage.Broadcast(OutMessage);
	++ListRevision;
	++MapRevision;
	if (Castles)
	{
		Castles->NoteChanged();
	}
	return true;
}

bool UHexHeroes::FightNeutral(FString& OutMessage)
{
	const FHexHeroOffer Join = PendingOffer;
	PendingOffer = FHexHeroOffer();
	RefusedJoins.Add(Join.NeutralId);
	FHexHero* Hero = FindHeroMutable(Join.HeroId);
	if (!Hero || !Battles)
	{
		return false;
	}
	FHexBattleRequest Request;
	Request.Kind = EHexBattleKind::Neutral;
	Request.Attacker = Hero->Id;
	Request.NeutralId = Join.NeutralId;
	Request.Layer = Join.Layer;
	Request.Cell = Join.Cell;
	FString Problem;
	if (!Battles->Begin(Request, Problem))
	{
		Hero->MovePoints += Join.Cost;
		OutMessage = Problem.IsEmpty() ? FString(TEXT("there is nothing to fight")) : Problem;
		return false;
	}
	++MapRevision;
	return true;
}

// the spells of the planet, as the developer asked on 2026-11-15: summon a boat, step through a dimension door, a portal into a castle of
// its own and a view of the surface or of the underground; the game casts them too (AiCastViews, AiDoorAhead, AiPortalInto)

TArray<int32> UHexHeroes::GetAdventureSpells(const FHexHero& Hero) const
{
	TArray<int32> Out;
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	for (int32 Index = 0; Battle && Index < Battle->Spells.Num(); ++Index)
	{
		if (Battle->Spells[Index].IsAdventure() && Hero.Spells.Contains(Battle->Spells[Index].Id))
		{
			Out.Add(Index);
		}
	}
	return Out;
}

int32 UHexHeroes::FindAdventureSpell(const FHexHero& Hero, EHexAdventureSpell What) const
{
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	for (const int32 Index : GetAdventureSpells(Hero))
	{
		if (Battle->Spells[Index].Adventure == What)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UHexHeroes::GetAdventureManaCost(const FHexHero& Hero, int32 Spell) const
{
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	if (!Battle || !Battle->Spells.IsValidIndex(Spell))
	{
		return 0;
	}
	const FHexSpell& Each = Battle->Spells[Spell];
	const float Cut = FMath::Clamp(GetSkillValue(Hero, GetSchoolSkill(Each.School)), 0.0f, 0.9f);
	return FMath::Max(FMath::RoundToInt32(float(Each.ManaCost) * (1.0f - Cut)), 1);
}

int32 UHexHeroes::GetAdventureReach(const FHexHero& Hero, int32 Spell) const
{
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	if (!Battle || !Battle->Spells.IsValidIndex(Spell))
	{
		return 0;
	}
	const FHexSpell& Each = Battle->Spells[Spell];
	const int32 Level = GetSkillLevel(Hero, GetSchoolSkill(Each.School));
	// a view at Expert sees the whole layer
	if (Level >= 3 && (Each.Adventure == EHexAdventureSpell::ViewSurface || Each.Adventure == EHexAdventureSpell::ViewUnderground))
	{
		return MAX_int32;
	}
	return Each.Reach + Each.ReachPerLevel * Level;
}

TArray<int32> UHexHeroes::GetDoorCells(const FHexHero& Hero, int32 Spell) const
{
	TArray<int32> Out;
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
	{
		return Out;
	}
	TArray<int32> Within;
	HexShroud::FindWithin(Planet->GetGrid(), Hero.Cell, GetAdventureReach(Hero, Spell), Within);
	for (const int32 Cell : Within)
	{
		if (Cell != Hero.Cell && !IsHidden(Hero.Player, Hero.Layer, Cell) && CanStand(Hero.Layer, Cell, Hero.Id) && !(Sites && Sites->FindSiteAt(Hero.Layer, Cell))
			&& !IsSea(Hero.Layer, Cell))
		{
			Out.Add(Cell);
		}
	}
	return Out;
}

TArray<int32> UHexHeroes::GetPortalCastles(const FHexHero& Hero, int32 Spell) const
{
	TArray<int32> Out;
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Castles || !Battle || !Battle->Spells.IsValidIndex(Spell) || !Planet || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
	{
		return Out;
	}
	// nearest first, by the angle between their places on the sphere, a castle of the other layer a little further
	const FVector3d Here = Planet->GetGrid().GetCells()[Hero.Cell].Center;
	TArray<TPair<double, int32>> Found;
	for (const FHexCastle& Castle : Castles->GetCastles())
	{
		const AHexPlanet* There = GetPlanet(Castle.Layer);
		if (Castle.Owner != Hero.Player || Castle.Id == Hero.CastleId || Castles->FindGuard(Castle) || !There || !There->GetGrid().GetCells().IsValidIndex(Castle.Cells[0]))
		{
			continue;
		}
		const double Angle = FMath::Acos(FMath::Clamp(Here | There->GetGrid().GetCells()[Castle.Cells[0]].Center, -1.0, 1.0)) + (Castle.Layer != Hero.Layer ? 0.05 : 0.0);
		Found.Add({Angle, Castle.Id});
	}
	Found.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	for (const TPair<double, int32>& Each : Found)
	{
		Out.Add(Each.Value);
	}
	// below Advanced of its school the nearest alone
	if (GetSkillLevel(Hero, GetSchoolSkill(Battle->Spells[Spell].School)) < 2 && Out.Num() > 1)
	{
		Out.SetNum(1);
	}
	return Out;
}

bool UHexHeroes::CanCastAdventure(const FHexHero& Hero, int32 Spell, FString* OutWhy) const
{
	const auto Fail = [OutWhy](const FString& Why)
	{
		if (OutWhy)
		{
			*OutWhy = Why;
		}
		return false;
	};
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	if (!Battle || !Battle->Spells.IsValidIndex(Spell) || !Battle->Spells[Spell].IsAdventure() || !Hero.Spells.Contains(Battle->Spells[Spell].Id))
	{
		return Fail(TEXT("The hero does not know this spell"));
	}
	const FHexSpell& Each = Battle->Spells[Spell];
	if (!bPlaying || TurnPlayer != Hero.Player)
	{
		return Fail(TEXT("Only on the turn of its player"));
	}
	if (Hero.bWalking || Hero.bGliding || (Battles && Battles->IsShown()))
	{
		return Fail(TEXT("Not while the hero walks or fights"));
	}
	const int32 Cost = GetAdventureManaCost(Hero, Spell);
	if (Hero.Mana < Cost)
	{
		return Fail(FString::Printf(TEXT("It costs %d mana, the hero has %d"), Cost, Hero.Mana));
	}
	if (Each.MovementCost > 0.0f && Hero.MovePoints + UE_KINDA_SMALL_NUMBER < Each.MovementCost)
	{
		return Fail(FString::Printf(TEXT("It takes %g points of movement, the hero has %.1f"), Each.MovementCost, Hero.MovePoints));
	}
	const int32* Cast = Hero.CastsToday.Find(Each.Id);
	if (Each.CastsPerDay > 0 && Cast && *Cast >= Each.CastsPerDay)
	{
		return Fail(FString::Printf(TEXT("It is cast %d times a day at most"), Each.CastsPerDay));
	}
	switch (Each.Adventure)
	{
	case EHexAdventureSpell::SummonBoat:
		if (Hero.BoatId != 0)
		{
			return Fail(TEXT("The hero is in a boat already"));
		}
		if (FindFreeSea(Hero, GetAdventureReach(Hero, Spell)) == INDEX_NONE)
		{
			return Fail(FString::Printf(TEXT("No free sea within %d cells"), GetAdventureReach(Hero, Spell)));
		}
		break;
	case EHexAdventureSpell::DimensionDoor:
	case EHexAdventureSpell::Teleport:
		if (Hero.BoatId != 0)
		{
			return Fail(TEXT("Not from a boat"));
		}
		if (GetDoorCells(Hero, Spell).IsEmpty())
		{
			return Fail(TEXT("No cell to step to within its reach"));
		}
		break;
	case EHexAdventureSpell::TownPortal:
		if (Hero.BoatId != 0)
		{
			return Fail(TEXT("Not from a boat"));
		}
		if (GetPortalCastles(Hero, Spell).IsEmpty())
		{
			return Fail(TEXT("No castle of its own that no hero guards"));
		}
		break;
	case EHexAdventureSpell::ViewSurface:
	case EHexAdventureSpell::ViewUnderground:
		break;
	default:
		return Fail(TEXT("It does nothing yet"));
	}
	return true;
}

void UHexHeroes::Teleport(FHexHero& Hero, EHexPlanetLayer Layer, int32 Cell)
{
	if (Hero.Layer != Layer)
	{
		RemoveBody(Hero.Id);
	}
	Hero.Route.Reset();
	Hero.Trail.Reset();
	Hero.bWalking = false;
	Hero.TargetCell = INDEX_NONE;
	Hero.VisitCell = INDEX_NONE;
	Hero.CheckCell = INDEX_NONE;
	Hero.CameOutCell = INDEX_NONE;
	Hero.bGliding = false;
	Hero.CastleId = 0;
	Hero.Layer = Layer;
	Hero.Cell = Cell;
	Hero.FromCell = Cell;
	Hero.bPlaced = false;
	Hero.Facing = FVector3d::ZeroVector;
	if (SelectedId == Hero.Id)
	{
		SelectedCastleId = 0;
	}
	Explore(Hero);
	// a jump is no walk into or out of the fog of war: the camera of the person stays where it is, as the developer found it flown to a hero
	// the game moved far into the fog on 2026-11-15
	Hero.bWasSeen = IsSeen(Hero);
	Hero.bCameThrough = false;
	++MapRevision;
	++ListRevision;
}

bool UHexHeroes::CastAdventure(int32 HeroId, int32 Spell, int32 TargetCell, int32 TargetCastle, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(HeroId);
	if (!Hero)
	{
		OutMessage = TEXT("No such hero");
		return false;
	}
	if (!CanCastAdventure(*Hero, Spell, &OutMessage))
	{
		return false;
	}
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	const FHexSpell& Each = Battle->Spells[Spell];
	const int32 Cost = GetAdventureManaCost(*Hero, Spell);
	const int32 Reach = GetAdventureReach(*Hero, Spell);
	const FString Name = Hero->Name;
	const EHexPlanetLayer FromLayer = Hero->Layer;
	switch (Each.Adventure)
	{
	case EHexAdventureSpell::SummonBoat:
		if (!SummonBoat(HeroId, OutMessage, Reach))
		{
			return false;
		}
		break;
	case EHexAdventureSpell::DimensionDoor:
	case EHexAdventureSpell::Teleport:
		if (!GetDoorCells(*Hero, Spell).Contains(TargetCell))
		{
			OutMessage = FString::Printf(TEXT("%s cannot step there: a cell it may stand on, within %d cells"), *Name, Reach);
			return false;
		}
		Teleport(*Hero, Hero->Layer, TargetCell);
		OutMessage = Each.Adventure == EHexAdventureSpell::Teleport ? FString::Printf(TEXT("%s teleports"), *Name)
			: FString::Printf(TEXT("%s steps through a dimension door"), *Name);
		break;
	case EHexAdventureSpell::TownPortal:
	{
		const TArray<int32> Allowed = GetPortalCastles(*Hero, Spell);
		const int32 CastleId = TargetCastle != 0 ? TargetCastle : Allowed[0];
		const FHexCastle* Castle = Allowed.Contains(CastleId) ? Castles->FindCastle(CastleId) : nullptr;
		if (!Castle)
		{
			OutMessage = FString::Printf(TEXT("%s cannot go into that castle"), *Name);
			return false;
		}
		Teleport(*Hero, Castle->Layer, Castle->Cells[0]);
		OutMessage = FString::Printf(TEXT("%s goes through a portal into %s"), *Name, *Castles->GetCastleName(*Castle));
		Castles->Visit(*Hero, CastleId, Castle->Cells[0], false);
		break;
	}
	case EHexAdventureSpell::ViewSurface:
	case EHexAdventureSpell::ViewUnderground:
	{
		const EHexPlanetLayer Layer = Each.Adventure == EHexAdventureSpell::ViewSurface ? EHexPlanetLayer::Surface : EHexPlanetLayer::Underground;
		const AHexPlanet* Planet = GetPlanet(Layer);
		if (!Planet || Planet->GetGrid().GetCells().IsEmpty())
		{
			OutMessage = TEXT("That layer is not there");
			return false;
		}
		const int32 NumCells = Planet->GetGrid().GetCells().Num();
		const TCHAR* LayerName = Layer == EHexPlanetLayer::Surface ? TEXT("surface") : TEXT("underground");
		ExploreAround(Hero->Player, Layer, Hero->Cell, FMath::Min(Reach, NumCells));
		OutMessage = Reach >= NumCells ? FString::Printf(TEXT("%s sees the whole of the %s"), *Name, LayerName)
			: FString::Printf(TEXT("%s sees the %s within %d cells"), *Name, LayerName, Reach);
		break;
	}
	default:
		return false;
	}
	// what it cost
	Hero = FindHeroMutable(HeroId);
	if (Hero)
	{
		Hero->Mana = FMath::Max(Hero->Mana - Cost, 0);
		Hero->MovePoints = FMath::Max(Hero->MovePoints - Each.MovementCost, 0.0f);
		++Hero->CastsToday.FindOrAdd(Each.Id);
		if (Hero->Layer != FromLayer)
		{
			OnHeroTravelled.Broadcast(HeroId);
		}
		// a portal opens the castle for a person (UHexCastles::Visit), whose camera comes to it
		if (IsStepSpell(Each.Adventure))
		{
			OnHeroTeleported.Broadcast(HeroId);
		}
	}
	++ListRevision;
	++MapRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s casts %s for %d mana: %s"), *Name, *Each.DisplayName, Cost, *OutMessage);
	return true;
}

bool UHexHeroes::AiCastViews(int32 Id)
{
	const FHexHero* Hero = FindHero(Id);
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	if (!Hero || !Shroud || !Battle)
	{
		return false;
	}
	for (const EHexAdventureSpell What : {EHexAdventureSpell::ViewSurface, EHexAdventureSpell::ViewUnderground})
	{
		const int32 Spell = FindAdventureSpell(*Hero, What);
		if (Spell == INDEX_NONE || !CanCastAdventure(*Hero, Spell) || Hero->CastsToday.Contains(Battle->Spells[Spell].Id))
		{
			continue;
		}
		// worth it when a good part of what it would see lies unexplored
		const EHexPlanetLayer Layer = What == EHexAdventureSpell::ViewSurface ? EHexPlanetLayer::Surface : EHexPlanetLayer::Underground;
		const AHexPlanet* Planet = GetPlanet(Layer);
		if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Hero->Cell))
		{
			continue;
		}
		TArray<int32> Within;
		HexShroud::FindWithin(Planet->GetGrid(), Hero->Cell, FMath::Min(GetAdventureReach(*Hero, Spell), 40), Within);
		int32 Unknown = 0;
		int32 Counted = 0;
		for (const int32 Cell : Within)
		{
			if (Layer == EHexPlanetLayer::Underground && !HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Cell))))
			{
				continue;
			}
			++Counted;
			Unknown += Shroud->IsExplored(Hero->Player, Layer, Cell) ? 0 : 1;
		}
		if (Unknown < FMath::Max(12, Counted / 4))
		{
			continue;
		}
		FString Message;
		if (CastAdventure(Id, Spell, INDEX_NONE, 0, Message))
		{
			return true;
		}
	}
	return false;
}

bool UHexHeroes::AiPortalInto(int32 Id, int32 CastleId, float WayCost)
{
	const FHexHero* Hero = FindHero(Id);
	const int32 Spell = Hero ? FindAdventureSpell(*Hero, EHexAdventureSpell::TownPortal) : int32(INDEX_NONE);
	// only for a castle it would not reach today on foot
	if (Spell == INDEX_NONE || (WayCost >= 0.0f && WayCost <= Hero->MovePoints) || !CanCastAdventure(*Hero, Spell) || !GetPortalCastles(*Hero, Spell).Contains(CastleId))
	{
		return false;
	}
	FString Message;
	return CastAdventure(Id, Spell, INDEX_NONE, CastleId, Message);
}

bool UHexHeroes::AiDoorAhead(int32 Id, const HexHeroPath::FField& Ways)
{
	const FHexHero* Hero = FindHero(Id);
	if (!Hero || Hero->TargetCell == INDEX_NONE)
	{
		return false;
	}
	// the way it was sent on, when it takes more than today
	int32 Node = INDEX_NONE;
	const float Cost = GetSiteWayCost(Ways, Hero->TargetLayer, Hero->TargetCell, &Node);
	TArray<int32> Path;
	if (Cost < 0.0f || Cost <= Hero->MovePoints || Node == INDEX_NONE || !Ways.GetPath(Node, Path))
	{
		return false;
	}
	// of the spells that step (Dimension door, Teleport) the one that saves the most of the way over what it takes: the furthest cell of the
	// way on its layer that it reaches, worth more than the spell takes
	const UHexBattleConfig* Battle = UHexBattleConfig::Load();
	const AHexPlanet* Planet = GetPlanet(Hero->Layer);
	const int32 NumCells = Planet ? Planet->GetGrid().GetCells().Num() : 0;
	int32 Spell = INDEX_NONE;
	int32 Best = INDEX_NONE;
	float BestGain = 1.5f;
	for (const EHexAdventureSpell What : {EHexAdventureSpell::Teleport, EHexAdventureSpell::DimensionDoor})
	{
		const int32 Each = FindAdventureSpell(*Hero, What);
		if (Each == INDEX_NONE || !CanCastAdventure(*Hero, Each))
		{
			continue;
		}
		const float StepCost = Battle->Spells[Each].MovementCost;
		const TArray<int32> Cells = GetDoorCells(*Hero, Each);
		for (const int32 Step : Path)
		{
			if (NumCells <= 0 || Step / NumCells != int32(Hero->Layer))
			{
				continue;
			}
			const float Gain = Ways.Costs[Step] - StepCost;
			if (Gain > BestGain && Cells.Contains(Step % NumCells))
			{
				Spell = Each;
				Best = Step % NumCells;
				BestGain = Gain;
			}
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	const EHexPlanetLayer TargetLayer = Hero->TargetLayer;
	const int32 TargetCell = Hero->TargetCell;
	FString Message;
	if (!CastAdventure(Id, Spell, Best, 0, Message))
	{
		return false;
	}
	// it goes on to where it was sent, from there
	FString Sent;
	Send(Id, TargetLayer, TargetCell, Sent);
	return true;
}

bool UHexHeroes::AiJumpTo(int32 Id, const HexHeroPath::FField& Ways, const TArray<int8>& Near)
{
	const FHexHero* Hero = FindHero(Id);
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	if (!Planet)
	{
		return false;
	}
	int32 Spell = INDEX_NONE;
	int32 Best = INDEX_NONE;
	int32 BestNear = MAX_int32;
	bool bBestWalled = false;
	for (const EHexAdventureSpell What : {EHexAdventureSpell::Teleport, EHexAdventureSpell::DimensionDoor})
	{
		const int32 Each = FindAdventureSpell(*Hero, What);
		if (Each == INDEX_NONE || !CanCastAdventure(*Hero, Each))
		{
			continue;
		}
		for (const int32 Cell : GetDoorCells(*Hero, Each))
		{
			const int32 Node = GetNode(Hero->Layer, Cell);
			if (!Near.IsValidIndex(Node) || Near[Node] < 0)
			{
				continue;
			}
			// one its feet reach today is walked to; one they never reach is past a wall, which it prefers
			const bool bReached = Ways.IsReached(Node);
			if (bReached && Ways.Costs[Node] <= Hero->MovePoints + UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const bool bWalled = !bReached;
			if (Near[Node] < BestNear || (Near[Node] == BestNear && bWalled && !bBestWalled))
			{
				Spell = Each;
				Best = Cell;
				BestNear = Near[Node];
				bBestWalled = bWalled;
			}
		}
		// the cheaper spell when it lands as near
		if (Best != INDEX_NONE)
		{
			break;
		}
	}
	FString Message;
	if (Best == INDEX_NONE || !CastAdventure(Id, Spell, Best, 0, Message))
	{
		return false;
	}
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, moved by the game, jumps to cell %d, %d steps from what its player has not explored%s"), *Hero->Name, Best, BestNear,
		bBestWalled ? TEXT(", past what its feet do not cross") : TEXT(""));
	return true;
}

UTexture2D* UHexCastles::GetCastlePicture(const FHexCastle& Castle) const
{
	if (!Config || !Config->Factions.IsValidIndex(Castle.Faction))
	{
		return nullptr;
	}
	const FHexFaction& Faction = Config->Factions[Castle.Faction];
	for (const EHexBuildingType Type : {EHexBuildingType::Fort, EHexBuildingType::Hall})
	{
		const int32 Building = Faction.FindBuilding(Type);
		const int32 Level = FMath::Max(GetLevel(Castle, Type), Type == EHexBuildingType::Hall ? 1 : 0);
		if (Level > 0 && Faction.Buildings.IsValidIndex(Building) && Faction.Buildings[Building].Levels.IsValidIndex(Level - 1))
		{
			if (UTexture2D* Picture = Faction.Buildings[Building].Levels[Level - 1].Picture.LoadSynchronous())
			{
				return Picture;
			}
		}
	}
	return nullptr;
}

FLinearColor UHexCastles::GetCastleTint(const FHexCastle& Castle) const
{
	const float Undead = Config && Config->Factions.IsValidIndex(Castle.Faction) ? Config->Factions[Castle.Faction].WallStyle : 0.0f;
	return FMath::Lerp(FLinearColor::White, FLinearColor(0.62f, 0.78f, 0.62f, 1.0f), FMath::Clamp(Undead, 0.0f, 1.0f));
}

UTexture2D* UHexHeroes::GetHeroPortrait(const FHexHero& Hero) const
{
	return Config && Config->Classes.IsValidIndex(Hero.Class) ? Config->Classes[Hero.Class].Portrait.LoadSynchronous() : nullptr;
}

int32 UHexHeroes::FindCoastCell(const FHexHero& Hero) const
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet || !Sites || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
	{
		return INDEX_NONE;
	}
	// ring by ring over the whole layer, the first ground a hero may stand on with free sea beside it
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	TBitArray<> Seen(false, Cells.Num());
	Seen[Hero.Cell] = true;
	TArray<int32> Ring = {Hero.Cell};
	while (!Ring.IsEmpty())
	{
		TArray<int32> Next;
		for (const int32 Cell : Ring)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (Seen[Neighbor])
				{
					continue;
				}
				Seen[Neighbor] = true;
				Next.Add(Neighbor);
				if (IsSea(Hero.Layer, Neighbor) || !CanStand(Hero.Layer, Neighbor, Hero.Id) || Sites->FindSiteAt(Hero.Layer, Neighbor))
				{
					continue;
				}
				for (const int32 Beside : Cells[Neighbor].Neighbors)
				{
					if (IsSea(Hero.Layer, Beside) && !Sites->FindSiteAt(Hero.Layer, Beside) && !FindHeroAt(Hero.Layer, Beside))
					{
						return Neighbor;
					}
				}
			}
		}
		Ring = MoveTemp(Next);
	}
	return INDEX_NONE;
}

bool UHexHeroes::CheatTeleport(int32 HeroId, EHexPlanetLayer Layer, int32 Cell, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(HeroId);
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Hero || !Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		OutMessage = TEXT("No such hero or cell");
		return false;
	}
	FString Problem;
	if (Hero->BoatId != 0 || !CanStand(Layer, Cell, HeroId, &Problem) || IsSea(Layer, Cell) || (Sites && Sites->FindSiteAt(Layer, Cell)))
	{
		OutMessage = FString::Printf(TEXT("%s cannot stand on cell %d%s"), *Hero->Name, Cell, Hero->BoatId != 0 ? TEXT(": it is in a boat") : Problem.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *Problem));
		return false;
	}
	const EHexPlanetLayer FromLayer = Hero->Layer;
	Teleport(*Hero, Layer, Cell);
	OutMessage = FString::Printf(TEXT("%s is on cell %d of the %s"), *Hero->Name, Cell, Layer == EHexPlanetLayer::Surface ? TEXT("surface") : TEXT("underground"));
	if (Layer != FromLayer)
	{
		OnHeroTravelled.Broadcast(HeroId);
	}
	OnHeroTeleported.Broadcast(HeroId);
	return true;
}

void UHexHeroes::GetClassTip(int32 Class, int32 Player, FHexMapTip& OutTip) const
{
	OutTip = FHexMapTip();
	if (!Config || !Config->Classes.IsValidIndex(Class))
	{
		return;
	}
	const FHexHeroClass& Each = Config->Classes[Class];
	OutTip.Title = FString::Printf(TEXT("A %s to hire"), *Each.DisplayName.ToLower());
	OutTip.TitleColor = FLinearColor::LerpUsingHSV(Config->GetPlayer(Player).Color, FLinearColor::White, 0.35f);
	OutTip.Picture = Each.Portrait.LoadSynchronous();
	OutTip.Stats.Add({EHexStatIcon::Attack, FString::FromInt(Each.Attack), TEXT("Attack")});
	OutTip.Stats.Add({EHexStatIcon::Defense, FString::FromInt(Each.Defense), TEXT("Defense")});
	OutTip.Stats.Add({EHexStatIcon::SpellPower, FString::FromInt(Each.SpellPower), TEXT("Spell power")});
	OutTip.Stats.Add({EHexStatIcon::Knowledge, FString::FromInt(Each.Knowledge), TEXT("Knowledge")});
	OutTip.Stats.Add({EHexStatIcon::Mana, FString::FromInt(Each.Knowledge * Config->ManaPerKnowledge), TEXT("Mana")});
	OutTip.Stats.Add({EHexStatIcon::Movement, FString::Printf(TEXT("%.0f"), Config->MovePoints), TEXT("Movement")});
	if (Each.StartSkill != EHexSkill::Num)
	{
		OutTip.Skills.Add({Each.StartSkill, 1});
	}
	OutTip.Lines.Add(FString::Printf(TEXT("Knows %d spells; its levels raise mostly %s"), Each.StartSpells.Num(),
		Each.AttackChance + Each.DefenseChance >= Each.SpellPowerChance + Each.KnowledgeChance ? TEXT("Attack and Defense") : TEXT("Spell Power and Knowledge")));
	if (Castles)
	{
		OutTip.Lines.Add(TEXT("It comes with:"));
		for (const FHexStack& Stack : Castles->MakeStartArmy(Player))
		{
			if (Stack.Count > 0)
			{
				OutTip.Army.Add(Stack);
				OutTip.ArmyCounts.Add(FString::FromInt(Stack.Count));
			}
		}
	}
}
