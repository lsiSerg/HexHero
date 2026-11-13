// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHero.h"
#include "HexBattle.h"
#include "HexBattleConfig.h"
#include "HexCastleConfig.h"
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
	Rules.Battle->CatapultDamageMin = Rules.Battle->CatapultDamageMax = 200;
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
	// the machines count for nobody: the battle goes on while stacks are left
	TestFalse(TEXT("it goes on"), Battle.IsOver());
	return true;
}

#endif
