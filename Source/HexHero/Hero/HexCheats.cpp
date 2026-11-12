// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexCheats.h"
#include "HexHero.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	bool bCheatEnemiesMarked = false;

	const HexCheats::FLine CheatLines[] = {
		{TEXT("Help"), TEXT("Ctrl+H"), TEXT("This sheet over the screen")},
		{TEXT("Help"), TEXT("hex.Cheat help"), TEXT("These lines in the log (also hex.Cheat.Help, or hex.Cheat alone)")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat resources [Amount] [Player]"), TEXT("That much of every resource, or CheatGold and CheatResources of DA_HexCastles, to the person who plays or the player named")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat build [on|off]"), TEXT("Castles build without the limit of a building a day")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat buildall [Castle|all]"), TEXT("Every building to its last level for nothing: the castle named, the one chosen, else those of the person")},
		{TEXT("Resources and castles"), TEXT("hex.Cheat buildhalf <Castle|all>"), TEXT("A castle built to the middle: hall and fort half way, the four lower dwellings upgraded, the fifth built")},
		{TEXT("Resources and castles"), TEXT("hex.Castle start <Castle|all>"), TEXT("What a castle has built and its garrison become what it starts with, which the save keeps")},
		{TEXT("Heroes"), TEXT("hex.Cheat units <Faction> <Level 1-7> <Count> [up] [Hero]"), TEXT("Creatures for the hero chosen or named, Dawn or Decay, upgraded with up")},
		{TEXT("Heroes"), TEXT("hex.Cheat catapult [Hero]"), TEXT("A catapult for the hero chosen or named, which shoots at the walls of castles")},
		{TEXT("Heroes"), TEXT("hex.Hero points <Amount> [Hero]"), TEXT("More points of movement today, or fewer with a negative amount")},
		{TEXT("Heroes"), TEXT("hex.Hero startarmy [Hero|all]"), TEXT("The army a hero has now becomes the one it starts with, which the save keeps")},
		{TEXT("Map"), TEXT("hex.Cheat reveal [Player]"), TEXT("The person who plays, or the player named, has explored all of both layers")},
		{TEXT("Map"), TEXT("hex.Cheat enemies [on|off]"), TEXT("Marks every neutral stack and hero of another player with a line and its name, under the fog of war too")},
		{TEXT("Map"), TEXT("hex.PlanetShroud 0"), TEXT("No fog of war while it is 0; 1 brings it back as it was")},
		{TEXT("Map"), TEXT("hex.Neutral generate [Seed]"), TEXT("Neutral stacks scattered anew; hex.Neutral list lists them")},
		{TEXT("Battle"), TEXT("hex.Battle quick"), TEXT("The rest of the battle shown at once; hex.Battle auto lets the game fight for you")},
		{TEXT("Battle"), TEXT("hex.Battle state"), TEXT("The stacks on the field in the log, by the index the other orders take")},
	};

	void RunCheatHelp(const TArray<FString>& Args)
	{
		HexCheats::LogHelp();
	}

	FAutoConsoleCommand CheatHelpCommand(TEXT("hex.Cheat.Help"), TEXT("Lists the cheats and console commands for trying the game, as hex.Cheat help and the sheet of Ctrl+H do"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunCheatHelp));
}

TConstArrayView<HexCheats::FLine> HexCheats::GetLines()
{
	return MakeArrayView(CheatLines);
}

FString HexCheats::GetHelpText()
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
	return Text;
}

void HexCheats::LogHelp()
{
	TArray<FString> Each;
	GetHelpText().ParseIntoArrayLines(Each);
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
