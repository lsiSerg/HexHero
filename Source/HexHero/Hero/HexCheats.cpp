// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexCheats.h"
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
	bool bCheatEnemiesMarked = false;

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
		{TEXT("Heroes"), TEXT("hex.Hero points <Amount> [Hero]"), TEXT("More points of movement today, or fewer with a negative amount")},
		{TEXT("Heroes"), TEXT("hex.Hero startarmy [Hero|all]"), TEXT("The army a hero has now becomes the one it starts with, which the save keeps")},
		{TEXT("Map"), TEXT("hex.Cheat reveal [Player]"), TEXT("The person who plays, or the player named, has explored all of both layers")},
		{TEXT("Map"), TEXT("hex.Cheat enemies [on|off]"), TEXT("Marks every neutral stack and hero of another player with a line and its name, under the fog of war too")},
		{TEXT("Map"), TEXT("hex.PlanetShroud 0"), TEXT("No fog of war while it is 0; 1 brings it back as it was")},
		{TEXT("Map"), TEXT("hex.Neutral generate [Seed]"), TEXT("Neutral stacks scattered anew; hex.Neutral list lists them")},
		{TEXT("Battle"), TEXT("K (in a battle)"), TEXT("What is under the cursor, a unit, a part of the wall or the catapult, takes a quarter of its health as damage (CheatHitShare of DA_HexBattle); hex.Battle hit <Col> <Row> does the same at a hex")},
		{TEXT("Battle"), TEXT("hex.Cheat passive [on|off]"), TEXT("In the battle you fight now the enemy attacks nobody and defends on every turn, its towers too; for that battle alone, it still strikes back")},
		{TEXT("Battle"), TEXT("hex.Battle quick"), TEXT("The rest of the battle shown at once; hex.Battle auto lets the game fight for you")},
		{TEXT("Battle"), TEXT("hex.Battle state"), TEXT("The stacks on the field in the log, by the index the other orders take")},
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

bool HexCheats::AreEnemiesMarked()
{
	return bCheatEnemiesMarked;
}

void HexCheats::SetEnemiesMarked(bool bMarked)
{
	bCheatEnemiesMarked = bMarked;
}
