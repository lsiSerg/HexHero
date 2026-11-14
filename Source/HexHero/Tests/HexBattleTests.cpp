// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHero.h"
#include "HexBattle.h"
#include "HexBattleConfig.h"
#include "HexCastleConfig.h"
#include "HexCastles.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Configs of the rules with no chances, so a strike deals what it should: no morale, no luck, damage without spread */
	struct FTestRules
	{
		UHexCastleConfig* Castles = nullptr;
		UHexBattleConfig* Battle = nullptr;

		FTestRules()
		{
			Castles = NewObject<UHexCastleConfig>();
			Castles->AddToRoot();
			Battle = NewObject<UHexBattleConfig>();
			Battle->AddToRoot();
			Battle->MoraleChancePerPoint = 0.0f;
			Battle->BadMoraleChancePerPoint = 0.0f;
			Battle->LuckChancePerPoint = 0.0f;
			Battle->OneFactionMorale = 0;
			for (FHexFaction& Faction : Castles->Factions)
			{
				for (FHexCreature& Creature : Faction.Creatures)
				{
					for (FHexCreatureStats* Stats : {&Creature.Base, &Creature.Upgraded})
					{
						Stats->DamageMax = Stats->DamageMin;
						Stats->Morale = 0;
						Stats->Luck = 0;
					}
				}
			}
		}

		~FTestRules()
		{
			Castles->RemoveFromRoot();
			Battle->RemoveFromRoot();
		}

		FHexCreatureStats& Stats(int32 Faction, int32 Level, bool bUpgraded = false)
		{
			FHexCreature& Creature = Castles->Factions[Faction].Creatures[Level];
			return bUpgraded ? Creature.Upgraded : Creature.Base;
		}

		/** A battle of one stack against one, on no obstacles */
		FHexBattleSetup MakeDuel(int32 Level, int32 Count, int32 OtherLevel, int32 OtherCount) const
		{
			FHexBattleSetup Setup;
			Setup.bObstacles = false;
			Setup.Sides[0].Stacks = {FHexStack{0, Level, false, Count}};
			Setup.Sides[1].Stacks = {FHexStack{0, OtherLevel, false, OtherCount}};
			return Setup;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleQueueTest, "HexHero.Battle.Queue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleQueueTest::RunTest(const FString& Parameters)
{
	FTestRules Rules;
	// speeds: level 0 and 1 of the first faction at 4, the third at 7; the defender's stack as fast as the attacker's
	Rules.Stats(0, 0).Speed = 4;
	Rules.Stats(0, 1).Speed = 4;
	Rules.Stats(0, 2).Speed = 7;
	FHexBattleSetup Setup;
	Setup.bObstacles = false;
	Setup.Sides[0].Stacks = {FHexStack{0, 0, false, 10}, FHexStack{0, 2, false, 3}};
	Setup.Sides[1].Stacks = {FHexStack{0, 1, false, 10}};
	FHexBattle Battle;
	Battle.Start(Setup, Rules.Castles, Rules.Battle);
	const TArray<int32> Queue = Battle.GetQueue(3);
	TestEqual(TEXT("three units act"), Queue.Num(), 3);
	// the fastest first, then the attacker before the defender at equal speed
	TestEqual(TEXT("the fastest first"), Battle.GetUnits()[Queue[0]].Level, 2);
	TestEqual(TEXT("the attacker at equal speed"), Battle.GetUnits()[Queue[1]].Side, 0);
	TestEqual(TEXT("the defender last"), Battle.GetUnits()[Queue[2]].Side, 1);

	// one that waits acts after all the others
	const int32 Waiter = Battle.GetActive();
	TestTrue(TEXT("waits"), Battle.Wait());
	TestNotEqual(TEXT("another unit's turn"), Battle.GetActive(), Waiter);
	TestFalse(TEXT("one cannot wait twice"), Battle.GetUnits()[Battle.GetActive()].bWaited);
	TestTrue(TEXT("defends"), Battle.Defend());
	TestTrue(TEXT("defends"), Battle.Defend());
	TestEqual(TEXT("then the one that waited"), Battle.GetActive(), Waiter);
	TestEqual(TEXT("still the first round"), Battle.GetRound(), 1);
	TestTrue(TEXT("defends"), Battle.Defend());
	TestEqual(TEXT("a new round"), Battle.GetRound(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleDamageTest, "HexHero.Battle.Damage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleDamageTest::RunTest(const FString& Parameters)
{
	FTestRules Rules;
	FHexCreatureStats& Striker = Rules.Stats(0, 0);
	Striker.Attack = 5;
	Striker.Defense = 5;
	Striker.DamageMin = Striker.DamageMax = 2;
	Striker.Health = 10;
	Striker.Speed = 20;
	Striker.Abilities = 0;
	FHexCreatureStats& Struck = Rules.Stats(0, 3);
	Struck.Attack = 5;
	Struck.Defense = 5;
	Struck.DamageMin = Struck.DamageMax = 3;
	Struck.Health = 10;
	Struck.Speed = 3;
	Struck.Abilities = 0;
	FHexBattle Battle;
	Battle.Start(Rules.MakeDuel(0, 10, 3, 10), Rules.Castles, Rules.Battle);
	const int32 Me = Battle.GetActive();
	TestEqual(TEXT("the striker goes first"), Battle.GetUnits()[Me].Side, 0);
	const int32 Target = 1 - Me;
	const FHexCoord From = Battle.GetUnits()[Target].Hex.GetNeighbor(3);
	const FHexStrikePreview Preview = Battle.PreviewStrike(Me, Target, false, 0);
	TestEqual(TEXT("ten creatures of 2 at equal Attack and Defense deal 20"), Preview.DamageMin, 20);
	TestEqual(TEXT("which kills two"), Preview.KillsMin, 2);
	// the forecast of the hint: the eight left strike back with 24 and kill two
	TestTrue(TEXT("the forecast says it strikes back"), Preview.bStrikesBack);
	TestEqual(TEXT("with what eight of 3 deal"), Preview.BackDamageMax, 24);
	TestEqual(TEXT("which kills two of the striker"), Preview.BackKillsMax, 2);
	TestTrue(TEXT("strikes"), Battle.Strike(Target, From));
	// 10 of 10 health, 20 damage: 8 left; they strike back 8 x 3 = 24 into 10 of 10: 7 left, the top one of 6
	TestEqual(TEXT("two of them killed"), Battle.GetUnits()[Target].Count, 8);
	TestEqual(TEXT("struck back"), Battle.GetUnits()[Me].Count, 8);
	TestEqual(TEXT("its top creature wounded"), Battle.GetUnits()[Me].TopHealth, 6);
	TestEqual(TEXT("no more striking back this round"), Battle.GetUnits()[Target].RetaliationsLeft, 0);

	// Attack above Defense adds to the damage
	Striker.Attack = 15;
	FHexBattle Stronger;
	Stronger.Start(Rules.MakeDuel(0, 10, 3, 10), Rules.Castles, Rules.Battle);
	TestEqual(TEXT("ten points of Attack more add half"), Stronger.PreviewStrike(Stronger.GetActive(), 1 - Stronger.GetActive(), false, 0).DamageMin, 30);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleAbilitiesTest, "HexHero.Battle.Abilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleAbilitiesTest::RunTest(const FString& Parameters)
{
	FTestRules Rules;
	FHexCreatureStats& Striker = Rules.Stats(0, 0);
	Striker.Speed = 20;
	Striker.Abilities = 1 << int32(EHexAbility::NoRetaliation);
	Rules.Stats(0, 3).Speed = 3;
	Rules.Stats(0, 3).Abilities = 0;
	FHexBattle Battle;
	Battle.Start(Rules.MakeDuel(0, 10, 3, 10), Rules.Castles, Rules.Battle);
	const int32 Me = Battle.GetActive();
	const int32 Target = 1 - Me;
	TestTrue(TEXT("strikes"), Battle.Strike(Target, Battle.GetUnits()[Target].Hex.GetNeighbor(3)));
	TestEqual(TEXT("nothing strikes back"), Battle.GetUnits()[Me].Count, 10);

	// a shooter shoots from afar, and cannot with an enemy beside it
	FHexCreature& Shooter = Rules.Castles->Factions[0].Creatures[1];
	Shooter.bShoots = true;
	Shooter.Base.Shots = 3;
	Shooter.Base.Speed = 20;
	FHexBattle Shots;
	Shots.Start(Rules.MakeDuel(1, 10, 3, 10), Rules.Castles, Rules.Battle);
	const int32 Archer = Shots.GetActive();
	TestTrue(TEXT("can shoot"), Shots.CanShoot(Archer, 1 - Archer));
	TestTrue(TEXT("shoots"), Shots.Shoot(1 - Archer));
	TestEqual(TEXT("a shot spent"), Shots.GetUnits()[Archer].ShotsLeft, 2);
	TestEqual(TEXT("no shot struck back"), Shots.GetUnits()[Archer].Count, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleSpellTest, "HexHero.Battle.Spells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleSpellTest::RunTest(const FString& Parameters)
{
	FTestRules Rules;
	Rules.Stats(0, 0).Speed = 20;
	Rules.Stats(0, 3).Health = 10;
	FHexBattleSetup Setup = Rules.MakeDuel(0, 10, 3, 10);
	Setup.Sides[0].HeroId = 1;
	Setup.Sides[0].SpellPower = 1;
	Setup.Sides[0].Mana = 20;
	FHexBattle Battle;
	Battle.Start(Setup, Rules.Castles, Rules.Battle);
	const int32 Bolt = Rules.Battle->FindSpell(TEXT("Bolt"));
	const int32 Haste = Rules.Battle->FindSpell(TEXT("Haste"));
	const int32 Enemy = 1 - Battle.GetActive();
	TestTrue(TEXT("the bolt is cast"), Battle.Cast(0, Bolt, Battle.GetUnits()[Enemy].Hex));
	// 15 + 12 at Spell Power 1 into 10 of 10: two killed and the top one wounded
	TestEqual(TEXT("27 damage kills two"), Battle.GetUnits()[Enemy].Count, 8);
	TestEqual(TEXT("mana spent"), Battle.GetMana(0), 20 - Rules.Battle->Spells[Bolt].ManaCost);
	TestFalse(TEXT("one spell a round"), Battle.CanCast(0, Haste, Battle.GetUnits()[Battle.GetActive()].Hex));
	TestEqual(TEXT("the unit keeps its turn"), Battle.GetUnits()[Battle.GetActive()].Side, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleQuickTest, "HexHero.Battle.Quick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleQuickTest::RunTest(const FString& Parameters)
{
	// the real configs, with their chances: a strong army beats a weak one, the same each time from the same seed
	const UHexCastleConfig* Castles = GetDefault<UHexCastleConfig>();
	const UHexBattleConfig* Config = GetDefault<UHexBattleConfig>();
	FHexBattleSetup Setup;
	Setup.Seed = 77;
	Setup.Sides[0].Stacks = {FHexStack{0, 3, false, 20}, FHexStack{0, 1, false, 30}, FHexStack{0, 5, false, 4}};
	Setup.Sides[0].HeroId = 1;
	Setup.Sides[0].SpellPower = 2;
	Setup.Sides[0].Mana = 30;
	Setup.Sides[1].Stacks = {FHexStack{1, 0, false, 25}, FHexStack{1, 1, false, 10}};
	FHexBattle One;
	One.Start(Setup, Castles, Config);
	One.PlayToEnd();
	TestTrue(TEXT("it ends"), One.IsOver());
	TestEqual(TEXT("the stronger wins"), One.GetWinner(), 0);
	TestTrue(TEXT("the loser has nothing left"), One.GetSurvivors(1).IsEmpty());
	TestTrue(TEXT("the winner keeps an army"), !One.GetSurvivors(0).IsEmpty());
	TestTrue(TEXT("it killed something"), One.GetKilledHealth(0) > 0);
	FHexBattle Two;
	Two.Start(Setup, Castles, Config);
	Two.PlayToEnd();
	TestEqual(TEXT("the same rounds from the same seed"), Two.GetRound(), One.GetRound());
	TestEqual(TEXT("the same survivors"), Two.GetSurvivors(0).Num(), One.GetSurvivors(0).Num());
	for (int32 Index = 0; Index < One.GetSurvivors(0).Num() && Index < Two.GetSurvivors(0).Num(); ++Index)
	{
		TestEqual(TEXT("the same counts"), Two.GetSurvivors(0)[Index].Count, One.GetSurvivors(0)[Index].Count);
	}

	// obstacles leave a way across, on many seeds
	for (uint32 Seed = 1; Seed < 40; ++Seed)
	{
		FHexBattleSetup Each = Setup;
		Each.Seed = Seed;
		FHexBattle Field;
		Field.Start(Each, Castles, Config);
		TestTrue(TEXT("obstacles within their count"), Field.GetObstacles().Num() <= Config->ObstaclesMax);
		for (const FHexCoord& Hex : Field.GetObstacles())
		{
			TestTrue(TEXT("no obstacle by the edges"), Hex.GetColumn() >= 2 && Hex.GetColumn() <= Config->Columns - 3);
		}
	}

	// a side that retreats loses its army
	FHexBattle Fled;
	Fled.Start(Setup, Castles, Config);
	Fled.Retreat(0);
	TestEqual(TEXT("the other side wins"), Fled.GetWinner(), 1);
	TestTrue(TEXT("nothing left of who retreated"), Fled.GetSurvivors(0).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleWallTurnsTest, "HexHero.Battle.WallTurns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleWallTurnsTest::RunTest(const FString& Parameters)
{
	// two stacks of the attacker, one strong and one of a tenth of its power: two and four turns to break a part of the wall
	FTestRules Rules;
	FHexBattleSetup Setup = Rules.MakeDuel(0, 100, 0, 10);
	Setup.Sides[0].Stacks.Add(FHexStack{0, 0, false, 10});
	Setup.Siege.bWalls = true;
	Setup.Siege.WallHealth = 160;
	Setup.Siege.GateHealth = 200;
	FHexBattle Battle;
	Battle.Start(Setup, Rules.Castles, Rules.Battle);
	int32 Strong = INDEX_NONE;
	int32 Weak = INDEX_NONE;
	for (int32 Index = 0; Index < Battle.GetUnits().Num(); ++Index)
	{
		const FHexBattleUnit& Each = Battle.GetUnits()[Index];
		if (Each.Side == 0 && !Each.IsMachine())
		{
			(Each.Count == 100 ? Strong : Weak) = Index;
		}
	}
	TestTrue(TEXT("both stacks"), Strong != INDEX_NONE && Weak != INDEX_NONE);
	if (Strong == INDEX_NONE || Weak == INDEX_NONE)
	{
		return false;
	}
	int32 Min = 0;
	int32 Max = 0;
	Battle.GetWallStrikeRange(Strong, 160, Min, Max);
	TestTrue(TEXT("the strongest in three to four turns"), Min == 40 && Max == 54);
	Battle.GetWallStrikeRange(Weak, 160, Min, Max);
	TestTrue(TEXT("a weak one in five to six"), Min == 27 && Max == 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleBreachTest, "HexHero.Battle.Breach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleBreachTest::RunTest(const FString& Parameters)
{
	// the command for trying the ruins: a part falls at once, the gate stays open, and what fell does not fall again
	FTestRules Rules;
	FHexBattleSetup Setup = Rules.MakeDuel(0, 20, 0, 10);
	Setup.Siege.bWalls = true;
	Setup.Siege.WallHealth = 160;
	Setup.Siege.GateHealth = 200;
	FHexBattle Battle;
	Battle.Start(Setup, Rules.Castles, Rules.Battle);
	const int32 Gate = Battle.GetWalls().IndexOfByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Gate; });
	const int32 Section = Battle.GetWalls().IndexOfByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Wall; });
	TestTrue(TEXT("a gate and a section"), Gate != INDEX_NONE && Section != INDEX_NONE);
	if (Gate == INDEX_NONE || Section == INDEX_NONE)
	{
		return false;
	}
	const FHexCoord GateHex = Battle.GetWalls()[Gate].Hex;
	const FHexCoord SectionHex = Battle.GetWalls()[Section].Hex;
	TestTrue(TEXT("the section falls"), Battle.BreakWall(SectionHex));
	TestFalse(TEXT("it lies fallen"), Battle.GetWalls()[Section].IsStanding());
	TestFalse(TEXT("and does not fall again"), Battle.BreakWall(SectionHex));
	TestFalse(TEXT("the gate is not open yet"), Battle.IsGateJammed());
	TestTrue(TEXT("the gate falls"), Battle.BreakWall(GateHex));
	TestTrue(TEXT("and stays open"), Battle.IsGateJammed());
	TestFalse(TEXT("nothing on an open hex"), Battle.BreakWall(FHexCoord::FromOffset(1, 1)));
	// the cheat key: a quarter of a part of the wall, and of a stack, four blows killing it
	const int32 Other = Battle.GetWalls().IndexOfByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Wall && Each.IsStanding(); });
	if (Other != INDEX_NONE)
	{
		const int32 Before = Battle.GetWalls()[Other].Health;
		TestTrue(TEXT("the cheat hits a part"), Battle.CheatHit(Battle.GetWalls()[Other].Hex, 0.25f));
		TestEqual(TEXT("for a quarter of its health"), Battle.GetWalls()[Other].Health, Before - FMath::CeilToInt32(Battle.GetWalls()[Other].MaxHealth * 0.25f));
	}
	const int32 Enemy = Battle.GetUnits().IndexOfByPredicate([](const FHexBattleUnit& Each) { return Each.Side == 1 && !Each.IsMachine(); });
	if (Enemy != INDEX_NONE)
	{
		const FHexCoord Hex = Battle.GetUnits()[Enemy].Hex;
		for (int32 Blow = 0; Blow < 4; ++Blow)
		{
			Battle.CheatHit(Hex, 0.25f);
		}
		TestFalse(TEXT("four blows kill a stack"), Battle.GetUnits()[Enemy].IsAlive());
		TestTrue(TEXT("and the battle is over"), Battle.IsOver());
		TestFalse(TEXT("no blows once it is over"), Battle.CheatHit(Hex, 0.25f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattlePassiveTest, "HexHero.Battle.Passive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattlePassiveTest::RunTest(const FString& Parameters)
{
	// the side the cheat made passive defends on every turn of its own and deals no damage, for as long as the game plays both
	FTestRules Rules;
	FHexBattle Battle;
	Battle.Start(Rules.MakeDuel(0, 10, 3, 10), Rules.Castles, Rules.Battle);
	Battle.SetPassiveSide(1);
	bool bDefended = false;
	bool bHurt = false;
	for (int32 Step = 0; Step < 40 && !Battle.IsOver(); ++Step)
	{
		const int32 Side = Battle.GetUnits()[Battle.GetActive()].Side;
		if (Side == 1)
		{
			Battle.PlayAi();
			for (const FHexBattleEvent& Event : Battle.TakeEvents())
			{
				bDefended |= Event.Type == FHexBattleEvent::EType::Defend;
				bHurt |= (Event.Type == FHexBattleEvent::EType::Strike || Event.Type == FHexBattleEvent::EType::Shoot || Event.Type == FHexBattleEvent::EType::Spell);
			}
		}
		else
		{
			Battle.Wait() || Battle.Defend();
			Battle.TakeEvents();
		}
	}
	TestTrue(TEXT("it defends"), bDefended);
	TestFalse(TEXT("and attacks nobody"), bHurt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleSiegeTest, "HexHero.Battle.Siege",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleSiegeTest::RunTest(const FString& Parameters)
{
	FTestRules Rules;
	// the catapult acts first and always hits as hard as the gate is strong
	Rules.Battle->CatapultSpeed = 99;
	Rules.Battle->CatapultHitChance = 1.0f;
	Rules.Battle->CatapultShotsMin = Rules.Battle->CatapultShotsMax = 1.0f;
	// a walker fast enough to reach past the wall in one turn
	Rules.Stats(0, 0).Speed = 20;
	const int32 Shooter = Rules.Castles->Factions[0].Creatures.IndexOfByPredicate([](const FHexCreature& Each) { return Each.bShoots; });
	TestTrue(TEXT("the faction has a shooter"), Shooter != INDEX_NONE);
	FHexBattleSetup Setup = Rules.MakeDuel(0, 10, 0, 10);
	Setup.Siege.bWalls = true;
	Setup.Siege.WallHealth = 100;
	Setup.Siege.GateHealth = 120;
	Setup.Siege.TowerHealth = 150;
	Setup.Siege.Towers = 1;
	Setup.Siege.TowerStack = FHexStack{0, Shooter, false, 5};
	Setup.Siege.bCatapult = true;
	Setup.Siege.bMoat = true;
	FHexBattle Battle;
	Battle.Start(Setup, Rules.Castles, Rules.Battle);
	TestEqual(TEXT("a part of the wall on every row"), Battle.GetWalls().Num(), Battle.GetRows());
	const FHexBattleWall* Gate = Battle.GetWalls().FindByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Gate; });
	const FHexBattleWall* Tower = Battle.GetWalls().FindByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Tower; });
	TestTrue(TEXT("a gate and a tower"), Gate && Tower);
	if (!Gate || !Tower)
	{
		return false;
	}
	const FHexCoord GateHex = Gate->Hex;
	TestTrue(TEXT("shooters in the tower"), Battle.GetUnits().IsValidIndex(Tower->Unit) && Battle.GetUnits()[Tower->Unit].bTower);
	TestFalse(TEXT("the gate shuts the attacker out"), Battle.IsPassable(GateHex, 0));
	TestTrue(TEXT("and opens for the defender"), Battle.IsPassable(GateHex, 1));
	TestFalse(TEXT("a section of the wall is no way for anyone"), Battle.IsPassable(Battle.GetWalls()[0].Hex, 1));
	TestTrue(TEXT("the catapult has the first turn"), Battle.GetUnits().IsValidIndex(Battle.GetActive()) && Battle.GetUnits()[Battle.GetActive()].bCatapult);
	TestTrue(TEXT("it fires at the gate"), Battle.FireCatapult(GateHex));
	TestFalse(TEXT("which falls"), Battle.FindWall(GateHex)->IsStanding());
	TestTrue(TEXT("and stays open"), Battle.IsGateJammed());
	TestTrue(TEXT("for the attacker too"), Battle.IsPassable(GateHex, 0));
	// the moat before the wall, but for the bridge before the gate; a walker that steps into it goes no further, and over the bridge through
	// the open gate it goes on
	FHexCoord Bridge;
	TestTrue(TEXT("a bridge"), Battle.GetBridge(Bridge));
	TestEqual(TEXT("the moat on every row but that of the gate"), Battle.GetMoat().Num(), Battle.GetRows() - 1);
	TestFalse(TEXT("no moat under the bridge"), Battle.IsMoat(Bridge));
	TestEqual(TEXT("the bridge before the gate"), Bridge.GetColumn(), GateHex.GetColumn() - 1);
	const int32 Walker = Battle.GetUnits().IndexOfByPredicate([](const FHexBattleUnit& Each) { return Each.Side == 0 && !Each.IsMachine(); });
	TestTrue(TEXT("the attacker has a walker"), Walker != INDEX_NONE && !Battle.GetUnits()[Walker].bFlies);
	if (Walker != INDEX_NONE)
	{
		TMap<FHexCoord, int32> Steps;
		TMap<FHexCoord, FHexCoord> From;
		Battle.FindReach(Walker, Steps, &From);
		bool bIntoMoat = false;
		bool bDearer = true;
		bool bBehindWall = false;
		for (const TPair<FHexCoord, int32>& Each : Steps)
		{
			bIntoMoat |= Battle.IsMoat(Each.Key);
			// a step into the moat costs MoatStepCost
			if (From.Contains(Each.Key) && Battle.IsMoat(Each.Key))
			{
				bDearer &= Each.Value == Steps[From[Each.Key]] + Rules.Battle->MoatStepCost;
			}
			bBehindWall |= Each.Key.GetColumn() > GateHex.GetColumn();
		}
		TestTrue(TEXT("it walks into the moat"), bIntoMoat);
		TestTrue(TEXT("where a step costs more"), bDearer);
		TestTrue(TEXT("over the bridge through the fallen gate"), bBehindWall);
	}
	// the strongest stack of a side breaks a part of the wall in two turns, a weak one in four
	if (Walker != INDEX_NONE)
	{
		const int32 Section = Battle.GetWalls().IndexOfByPredicate([](const FHexBattleWall& Each) { return Each.Part == EHexWallPart::Wall; });
		const int32 Health = Battle.GetWalls()[Section].MaxHealth;
		int32 Min = 0;
		int32 Max = 0;
		Battle.GetWallStrikeRange(Walker, Health, Min, Max);
		TestEqual(TEXT("the strongest breaks a section in four turns at most"), Min, FMath::CeilToInt32(Health / 4.0));
		TestEqual(TEXT("and three at least"), Max, FMath::CeilToInt32(Health / 3.0));
	}
	// the machines count for nobody: the battle goes on while stacks are left
	TestFalse(TEXT("it goes on"), Battle.IsOver());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleCatapultTest, "HexHero.Battle.Catapult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleCatapultTest::RunTest(const FString& Parameters)
{
	// on an open field both sides bring their catapults, as the developer asked on 2026-11-14, which drive and throw stones at stacks
	FTestRules Rules;
	Rules.Battle->CatapultSpeed = 99;
	FHexBattleSetup Setup = Rules.MakeDuel(0, 10, 0, 10);
	Setup.Siege.bCatapult = true;
	Setup.Siege.bDefenderCatapult = true;
	FHexBattle Battle;
	Battle.Start(Setup, Rules.Castles, Rules.Battle);
	TestEqual(TEXT("the catapult of the defender behind its last row"), Battle.GetMachineHex(1), FHexCoord::FromOffset(Battle.GetColumns(), Battle.GetRows() - 1));
	const int32 Catapult = Battle.GetActive();
	TestTrue(TEXT("a catapult has the first turn"), Battle.GetUnits().IsValidIndex(Catapult) && Battle.GetUnits()[Catapult].bCatapult);
	if (!Battle.GetUnits().IsValidIndex(Catapult))
	{
		return false;
	}
	TMap<FHexCoord, int32> Steps;
	Battle.FindReach(Catapult, Steps);
	int32 Farthest = 0;
	for (const TPair<FHexCoord, int32>& Each : Steps)
	{
		Farthest = FMath::Max(Farthest, Each.Value);
	}
	TestTrue(TEXT("it drives onto the field"), Steps.Num() > 1);
	TestTrue(TEXT("no further than it may"), Farthest <= Rules.Battle->CatapultMoveHexes);
	const int32 Enemy = Battle.GetUnits().IndexOfByPredicate([&](const FHexBattleUnit& Each) { return Each.Side != Battle.GetUnits()[Catapult].Side && !Each.IsMachine(); });
	TestTrue(TEXT("it may throw a stone at a stack"), Enemy != INDEX_NONE && Battle.CanShoot(Catapult, Enemy));
	FHexCoord Away = Battle.GetUnits()[Catapult].Hex;
	for (const TPair<FHexCoord, int32>& Each : Steps)
	{
		if (Each.Value == Farthest)
		{
			Away = Each.Key;
			break;
		}
	}
	TestTrue(TEXT("and drives"), Battle.Move(Away));
	TestEqual(TEXT("to where it was sent"), Battle.GetUnits()[Catapult].Hex, Away);
	// its own hex off the field is part of it, which anyone may walk onto now it is empty, as the developer asked on 2026-11-14
	TestTrue(TEXT("the hex of a catapult is part of the field"), Battle.IsOnField(Battle.GetMachineHex(0)) && !Battle.IsInGrid(Battle.GetMachineHex(0)));
	TestTrue(TEXT("and free once it drove off"), Away == Battle.GetMachineHex(0) || Battle.IsPassable(Battle.GetMachineHex(0), 0));

	// a flyer on an open field walks the way over the ground, a step a hex, and flies only over what is in the way
	FTestRules FlyRules;
	const int32 Flyer = FlyRules.Castles->Factions[0].Creatures.IndexOfByPredicate([](const FHexCreature& Each) { return Each.bFlies; });
	TestTrue(TEXT("the faction has a flyer"), Flyer != INDEX_NONE);
	if (Flyer == INDEX_NONE)
	{
		return false;
	}
	FlyRules.Stats(0, Flyer).Speed = 99;
	FHexBattle Flight;
	Flight.Start(FlyRules.MakeDuel(Flyer, 5, 0, 5), FlyRules.Castles, FlyRules.Battle);
	const int32 Mover = Flight.GetActive();
	TestTrue(TEXT("the flyer acts first"), Flight.GetUnits().IsValidIndex(Mover) && Flight.GetUnits()[Mover].bFlies);
	if (Flight.GetUnits().IsValidIndex(Mover))
	{
		const FHexCoord Start = Flight.GetUnits()[Mover].Hex;
		const FHexCoord Goal = FHexCoord::FromOffset(Start.GetColumn() + 4, Start.GetRow());
		const TArray<FHexCoord> Path = Flight.FindPath(Mover, Goal);
		TestEqual(TEXT("over the ground, a step a hex"), Path.Num(), Start.DistanceTo(Goal) + 1);
	}
	return true;
}

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHexBattleSlotsTest, "HexHero.Battle.Slots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBattleSlotsTest::RunTest(const FString& Parameters)
{
	// a stack keeps the slot it is put in, an empty slot keeping its place, as the developer asked on 2026-11-13
	TArray<FHexStack> Army = {FHexStack{0, 0, false, 10}, FHexStack{0, 1, false, 6}};
	FString Message;
	TestTrue(TEXT("a stack moves to an empty slot further on"), UHexCastles::MoveBetween(Army, 0, Army, 5, 0, Message));
	TestEqual(TEXT("the army reaches that slot"), Army.Num(), 6);
	TestTrue(TEXT("its first slot is empty"), Army[0].Count == 0 && Army[1].Count == 6 && Army[5].Count == 10);
	TestTrue(TEXT("half of it splits into an empty slot"), UHexCastles::MoveBetween(Army, 5, Army, 3, 5, Message));
	TestTrue(TEXT("both halves stand where they were put"), Army[3].Count == 5 && Army[5].Count == 5);
	TestTrue(TEXT("a whole stack swaps with another kind"), UHexCastles::MoveBetween(Army, 1, Army, 3, 0, Message));
	TestTrue(TEXT("swapped"), Army[1].Creature == 0 && Army[3].Creature == 1);
	TestFalse(TEXT("part of a stack goes onto no other kind"), UHexCastles::MoveBetween(Army, 1, Army, 3, 2, Message));
	TestTrue(TEXT("one joins a stack of its kind"), UHexCastles::MoveBetween(Army, 1, Army, 5, 0, Message) && Army[5].Count == 10);
	TestEqual(TEXT("the first free slot"), UHexCastles::FindFreeSlot(Army), 0);
	TArray<FHexStack> Trailing = {FHexStack{0, 0, false, 3}, FHexStack{0, 0, false, 0}};
	UHexCastles::TrimArmy(Trailing);
	TestEqual(TEXT("empty slots at the end are dropped"), Trailing.Num(), 1);

	// and fights on the row of its slot
	FTestRules Rules;
	FHexBattleSetup Setup;
	Setup.bObstacles = false;
	Setup.Sides[0].Stacks = {FHexStack{0, 0, false, 0}, FHexStack{0, 0, false, 0}, FHexStack{0, 0, false, 0}, FHexStack{0, 0, false, 0}, FHexStack{0, 0, false, 0},
		FHexStack{0, 0, false, 0}, FHexStack{0, 0, false, 8}};
	Setup.Sides[1].Stacks = {FHexStack{0, 1, false, 8}};
	FHexBattle Battle;
	Battle.Start(Setup, Rules.Castles, Rules.Battle);
	const FHexBattleUnit* Last = Battle.GetUnits().FindByPredicate([](const FHexBattleUnit& Each) { return Each.Side == 0 && !Each.IsMachine(); });
	TestTrue(TEXT("the seventh slot stands on the last row of the first column"), Last && Last->Slot == 6 && Last->Hex == FHexCoord::FromOffset(0, Rules.Battle->ArmySlotRows[6]));
	return true;
}
