// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexCheats.h"
#include "HexBattleConfig.h"
#include "HexCastleConfig.h"
#include "HexCastles.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexPlanet.h"
#include "HexPlanetLayers.h"
#include "HexSiteConfig.h"
#include "HexSites.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	uint32 CheatShownMarks = 0;
	int32 CheatShownMarksReach = 0;

	struct FMarkKindName
	{
		uint32 Kind;
		const TCHAR* Plural;
		const TCHAR* Singular;
	};

	const FMarkKindName MarkKindNames[] = {
		{HexCheats::MarkEnemies, TEXT("enemies"), TEXT("enemy")},
		{HexCheats::MarkArtifacts, TEXT("artifacts"), TEXT("artifact")},
		{HexCheats::MarkResources, TEXT("resources"), TEXT("resource")},
		{HexCheats::MarkMines, TEXT("mines"), TEXT("mine")},
		{HexCheats::MarkHeroes, TEXT("heroes"), TEXT("hero")},
		{HexCheats::MarkCastles, TEXT("castles"), TEXT("castle")},
		{HexCheats::MarkBoats, TEXT("boats"), TEXT("boat")},
		{HexCheats::MarkNeutrals, TEXT("neutrals"), TEXT("neutral")},
	};

	const HexCheats::FLine CheatLines[] = {
		{TEXT("Help"), TEXT("Ctrl+H"), TEXT("This sheet over the screen")},
		{TEXT("Help"), TEXT("hex.Cheat help"), TEXT("These lines in the log (also hex.Cheat.Help, or hex.Cheat alone)")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat resources [Amount] [Player]"), TEXT("That much of every resource, or CheatGold and CheatResources of DA_HexCastles, to the person who plays or the player named")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat buildnolimit [on|off]"), TEXT("Castles build without the limit of a building a day")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat buildall [Castle|all]"), TEXT("Every building to its last level for nothing: the castle named, the one chosen, else those of the person")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat buildhalf <Castle|all>"), TEXT("A castle built to the middle: hall and fort half way, the four lower dwellings upgraded, the fifth built")},
		{TEXT("Resources and castles"), TEXT("hex.Castle start <Castle|all>"), TEXT("What a castle has built and its garrison become what it starts with, which the save keeps")},
		{TEXT("Resources and castles"), TEXT("hex.Castle give <Player> <Resource> <Amount>"), TEXT("That much of one resource to a player")},
		{TEXT("Heroes"), TEXT("hex.Cheat units <Faction> <Level 1-7> <Count> [up] [Hero]"), TEXT("Creatures for the hero chosen or named, Dawn or Decay, upgraded with up")},
		{TEXT("Heroes"), TEXT("hex.Cheat catapult [Hero]"), TEXT("A catapult for the hero chosen or named, which shoots at the walls of castles")},
		{TEXT("Heroes"), TEXT("hex.Cheat exp <Amount> [Hero]"), TEXT("Experience for the hero chosen or named, which may raise its level and offer skills")},
		{TEXT("Heroes"), TEXT("hex.Cheat skill <Skill|all> [1-3] [Hero]"), TEXT("A skill, or every one, at Basic, Advanced or Expert, or without a level one higher than the hero has")},
		{TEXT("Heroes"), TEXT("hex.Cheat spell <Spell|School|1-5|all> [Hero]"), TEXT("The hero chosen or named learns a spell, those of a school (Air, Earth, Fire, Water) or of a level, or every one")},
		{TEXT("Heroes"), TEXT("hex.Cheat spells [Hero|all]"), TEXT("Every spell and full mana for the hero chosen or named, or with all for every hero of yours")},
		{TEXT("Heroes"), TEXT("hex.Cheat mana [Amount] [Hero]"), TEXT("So much more mana, or without an amount up to its most, and another most when it is full")},
		{TEXT("Heroes"), TEXT("hex.Cheat artifact <Artifact|random|all> [Hero]"), TEXT("An artifact by its Id, name or number, a random one, or every one; worn in a free slot, else carried")},
		{TEXT("Heroes"), TEXT("hex.Cheat boat [Hero]"), TEXT("A boat on the free sea nearest the hero chosen or named, within 4 cells, as the spell does; the hero goes aboard by a step onto it")},
		{TEXT("Heroes"), TEXT("hex.Cheat teleport <Cell|focus|coast> [Hero]"), TEXT("The hero chosen or named on a cell at once: its number on the layer shown, focus for the one the camera looks at, coast for the ground nearest it beside free sea")},
		{TEXT("Heroes"), TEXT("hex.Hero cast <Spell> [Cell|Castle]"), TEXT("The hero chosen casts a spell of the planet it knows (SummonBoat, ViewLand, ViewDepths, DimensionDoor, Teleport, TownPortal): a door or teleport to the cell given, else the furthest it reaches, a portal into the castle given, else the nearest")},
		{TEXT("Heroes"), TEXT("hex.Artifact <Artifact|all|list|wear <Pack>|off <Slot>|pass <Hero> <worn|pack> <Index>> [Hero]"), TEXT("The artifacts of the hero chosen or named: give one or all, list them, put one on, take one off, pass one to a hero beside it")},
		{TEXT("Heroes"), TEXT("hex.Hero answer [yes|no]"), TEXT("Answers the choice that is up: creatures that want to join, a chest, a shipyard")},
		{TEXT("Heroes"), TEXT("hex.Hero points <Amount> [Hero]"), TEXT("More points of movement today, or fewer with a negative amount")},
		{TEXT("Heroes"), TEXT("hex.Hero startarmy [Hero|all]"), TEXT("The army a hero has now becomes the one it starts with, which the save keeps")},
		{TEXT("Game"), TEXT("hex.Cheat autoplay [on|off] [Player]"), TEXT("The game plays for you, or the player named, by the rules of its AI, until turned off; not saved")},
		{TEXT("Game"), TEXT("hex.Cheat speed [Times]"), TEXT("The game runs so many times as fast, up to 20, as slomo does; without a number at its own speed")},
		{TEXT("Map"), TEXT("hex.Cheat reveal [Player]"), TEXT("The person who plays, or the player named, has explored all of both layers")},
		{TEXT("Map"), TEXT("hex.Cheat enemies [on|off]"), TEXT("Marks every neutral stack and hero of another player with a line and its name, under the fog of war too")},
		{TEXT("Map"), TEXT("hex.Cheat show <Kind...|all|off> [Cells]"), TEXT("Marks on the layer shown, under the fog of war too, switched kind by kind: artifacts, resources (piles, chests), mines (and mills), heroes, castles, boats (and shipyards), neutrals, enemies; a number marks only what lies within that many cells of where the camera looks, 0 the whole layer, as by default")},
		{TEXT("Map"), TEXT("hex.PlanetShroud 0"), TEXT("No fog of war while it is 0; 1 brings it back as it was")},
		{TEXT("Map"), TEXT("hex.Neutral generate [Seed]"), TEXT("Neutral stacks scattered anew; hex.Neutral list lists them")},
		{TEXT("Map"), TEXT("hex.Site scatter [Seed] [Density]"), TEXT("More piles, chests and artifacts on the map besides those there are; hex.Site artifacts and hex.Site coast add artifacts, or shipyards and boats, alone")},
		{TEXT("Battle"), TEXT("K (in a battle)"), TEXT("What is under the cursor, a unit, a part of the wall or the catapult, takes a quarter of its health as damage (CheatHitShare of DA_HexBattle); hex.Battle hit <Col> <Row> does the same at a hex")},
		{TEXT("Battle"), TEXT("hex.Cheat seabattle [on|off]"), TEXT("Every battle on the deck of a ship until it is turned off, to try them anywhere; else a battle is at sea when a hero of it is in a boat")},
		{TEXT("Battle"), TEXT("hex.Cheat passive [on|off]"), TEXT("In the battle you fight now the enemy attacks nobody and defends on every turn, its towers too; for that battle alone, it still strikes back")},
		{TEXT("Battle"), TEXT("hex.Battle quick"), TEXT("The rest of the battle shown at once; hex.Battle auto lets the game fight for you")},
		{TEXT("Battle"), TEXT("hex.Battle state"), TEXT("The stacks on the field in the log, by the index the other orders take")},
		{TEXT("Test runs"), TEXT("hex.Key <Key> [ctrl] [shift]"), TEXT("A key pressed and released as the keyboard gives it (Escape, H, SpaceBar)")},
		{TEXT("Test runs"), TEXT("hex.Mouse <X> <Y>"), TEXT("The mouse at a point of the viewport; hex.Mouse alone logs what lies under it")},
		{TEXT("Test runs"), TEXT("hex.Order <Cell> [click] [ctrl]"), TEXT("A right click on a cell, or a left one, with Ctrl held, as the mouse gives them")},
		{TEXT("Test runs"), TEXT("hex.OpenPanel <army|book|castle> <Id>"), TEXT("Opens the army or spell book of a hero, or the panel of a castle; hex.CastlePanelTab <0-3> shows a tab of it")},
	};

	void RunCheatHelp(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		const AHexPlanet* Planet = Layers ? Layers->GetPresentedBuilt() : nullptr;
		HexCheats::LogHelp(Planet ? Planet->GetHeroes() : nullptr);
	}

	FAutoConsoleCommandWithWorldAndArgs CheatHelpCommand(TEXT("hex.Cheat.Help"), TEXT("Lists the cheats and console commands for trying the game, as hex.Cheat help and the sheet of Ctrl+H do"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCheatHelp));
}

TConstArrayView<HexCheats::FLine> HexCheats::GetLines()
{
	return MakeArrayView(CheatLines);
}

TArray<HexCheats::FArgumentLine> HexCheats::GetArgumentLines(const UHexHeroes* Heroes)
{
	TArray<FArgumentLine> Lines;
	const UHexHeroConfig* HeroConfig = Heroes ? Heroes->GetConfig() : nullptr;
	const UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
	const UHexCastleConfig* CastleConfig = Castles ? Castles->GetConfig() : nullptr;
	const UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	const UHexSiteConfig* SiteConfig = Sites ? Sites->GetConfig() : nullptr;
	if (!HeroConfig || !CastleConfig || !SiteConfig)
	{
		return Lines;
	}
	const auto Join = [](const TArray<FString>& Parts) { return FString::Join(Parts, TEXT(", ")); };
	const auto PlayerName = [HeroConfig](int32 Player)
	{
		return HeroConfig->Players.IsValidIndex(Player) ? HeroConfig->Players[Player].Name : FString(TEXT("nobody"));
	};

	TArray<FString> Parts;
	for (int32 Player = 0; Player < HeroConfig->Players.Num(); ++Player)
	{
		Parts.Add(FString::Printf(TEXT("%d %s"), Player, *HeroConfig->Players[Player].Name));
	}
	Lines.Add({TEXT("<Player>"), FString::Printf(TEXT("its number: %s"), *Join(Parts))});

	Parts.Reset();
	for (const FHexHero& Hero : Heroes->GetHeroes())
	{
		Parts.Add(FString::Printf(TEXT("%d %s (%s)"), Hero.Id, *Hero.Name, *PlayerName(Hero.Player)));
	}
	Lines.Add({TEXT("<Hero>"), Parts.IsEmpty() ? FString(TEXT("its id or name; no hero is on the planet")) : FString::Printf(TEXT("its id or name: %s"), *Join(Parts))});

	Parts.Reset();
	for (const FHexCastle& Castle : Castles->GetCastles())
	{
		const FString Faction = CastleConfig->Factions.IsValidIndex(Castle.Faction) ? CastleConfig->Factions[Castle.Faction].DisplayName : FString();
		Parts.Add(FString::Printf(TEXT("%d %s (%s)"), Castle.Id, *Faction, *PlayerName(Castle.Owner)));
	}
	Lines.Add({TEXT("<Castle>"), Parts.IsEmpty() ? FString(TEXT("its id; no castle is on the planet")) : FString::Printf(TEXT("its id: %s"), *Join(Parts))});

	Parts.Reset();
	for (const FHexFaction& Faction : CastleConfig->Factions)
	{
		Parts.Add(FString::Printf(TEXT("%s (%s)"), *Faction.Id.ToString(), *Faction.DisplayName));
	}
	Lines.Add({TEXT("<Faction>"), Join(Parts)});

	// the level of each creature, its basic version and the one up gives
	for (const FHexFaction& Faction : CastleConfig->Factions)
	{
		Parts.Reset();
		for (int32 Level = 0; Level < Faction.Creatures.Num(); ++Level)
		{
			const FHexCreature& Creature = Faction.Creatures[Level];
			Parts.Add(FString::Printf(TEXT("%d %s / %s"), Level + 1, *Creature.Base.DisplayName, *Creature.Upgraded.DisplayName));
		}
		Lines.Add({FString::Printf(TEXT("<Level 1-%d> of %s"), Faction.Creatures.Num(), *Faction.Id.ToString()), FString::Printf(TEXT("%s (basic / with up)"), *Join(Parts))});
	}

	Parts.Reset();
	for (const FHexResourceInfo& Resource : SiteConfig->Resources)
	{
		Parts.Add(Resource.Id.ToString());
	}
	Lines.Add({TEXT("<Resource>"), Join(Parts)});

	Parts.Reset();
	for (int32 Index = 0; Index < HeroConfig->Artifacts.Num(); ++Index)
	{
		Parts.Add(FString::Printf(TEXT("%d %s"), Index, *HeroConfig->Artifacts[Index].Id.ToString()));
	}
	Lines.Add({TEXT("<Artifact>"), Join(Parts)});

	if (const UHexBattleConfig* Battle = UHexBattleConfig::Load())
	{
		Parts.Reset();
		for (const FHexSpell& Spell : Battle->Spells)
		{
			Parts.Add(FString::Printf(TEXT("%s (%s %d)"), *Spell.Id.ToString(), *StaticEnum<EHexSpellSchool>()->GetNameStringByValue(int64(Spell.School)), Spell.Level));
		}
		Lines.Add({TEXT("<Spell>"), Join(Parts)});
	}

	Parts.Reset();
	for (int32 Skill = 0; Skill < int32(EHexSkill::Num); ++Skill)
	{
		Parts.Add(StaticEnum<EHexSkill>()->GetNameStringByValue(Skill));
	}
	Lines.Add({TEXT("<Skill>"), Join(Parts)});
	return Lines;
}

FString HexCheats::GetHelpText(const UHexHeroes* Heroes)
{
	FString Text;
	const TCHAR* Section = nullptr;
	for (const FLine& Line : CheatLines)
	{
		if (!Section || FCString::Strcmp(Section, Line.Section) != 0)
		{
			Section = Line.Section;
			Text += FString::Printf(TEXT("%s:\n"), Line.Section);
		}
		Text += FString::Printf(TEXT("  %s  -  %s\n"), Line.Command, Line.Text);
	}
	const TArray<FArgumentLine> Arguments = GetArgumentLines(Heroes);
	if (!Arguments.IsEmpty())
	{
		Text += FString::Printf(TEXT("%s:\n"), ArgumentSection);
		for (const FArgumentLine& Line : Arguments)
		{
			Text += FString::Printf(TEXT("  %s  -  %s\n"), *Line.Argument, *Line.Values);
		}
	}
	return Text;
}

void HexCheats::LogHelp(const UHexHeroes* Heroes)
{
	TArray<FString> Each;
	GetHelpText(Heroes).ParseIntoArrayLines(Each);
	UE_LOG(LogHexHero, Log, TEXT("Cheats and commands for trying the game:"));
	for (const FString& Line : Each)
	{
		UE_LOG(LogHexHero, Log, TEXT("%s"), *Line);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 4.0f, FColor::Yellow, TEXT("The cheats are in the log and on the sheet of Ctrl+H"));
	}
}

uint32 HexCheats::GetShownMarks()
{
	return CheatShownMarks;
}

void HexCheats::SetShownMarks(uint32 Kinds)
{
	CheatShownMarks = Kinds & AllMarks;
}

int32 HexCheats::GetShownMarksReach()
{
	return CheatShownMarksReach;
}

void HexCheats::SetShownMarksReach(int32 Cells)
{
	CheatShownMarksReach = FMath::Max(Cells, 0);
}

uint32 HexCheats::FindMarkKind(const FString& Name)
{
	for (const FMarkKindName& Each : MarkKindNames)
	{
		if (Name.Equals(Each.Plural, ESearchCase::IgnoreCase) || Name.Equals(Each.Singular, ESearchCase::IgnoreCase))
		{
			return Each.Kind;
		}
	}
	return 0;
}

FString HexCheats::GetMarkKindNames(uint32 Kinds, const TCHAR* Separator)
{
	TArray<FString> Names;
	for (const FMarkKindName& Each : MarkKindNames)
	{
		if (Kinds & Each.Kind)
		{
			Names.Add(Each.Plural);
		}
	}
	return FString::Join(Names, Separator);
}

bool HexCheats::AreEnemiesMarked()
{
	return (CheatShownMarks & MarkEnemies) != 0;
}
