// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexBattle.h"
#include "HexPlanetLayers.h"
#include "HexBattles.generated.h"

class UHexHeroes;
class UHexBattleConfig;
struct FHexCastle;

/** Who a battle is fought against */
enum class EHexBattleKind : uint8
{
	/** A hero of another player */
	Hero,
	/** A castle of another player or of nobody, its garrison and the hero that guards it */
	Castle,
	/** A neutral stack of the map */
	Neutral,
};

/** What a battle is about: the hero that attacks and what it attacks, where */
struct FHexBattleRequest
{
	EHexBattleKind Kind = EHexBattleKind::Hero;
	int32 Attacker = 0;
	int32 DefenderHero = 0;
	int32 CastleId = 0;
	int32 NeutralId = 0;
	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
	/** The cell attacked, and the cell of the castle it was attacked by */
	int32 Cell = INDEX_NONE;
};

/**
 *  The battles of the planet, an object of UHexHeroes (GetBattles), as the developer decided on 2026-11-12: a hero that is sent onto a hero
 *  of another player, a castle guarded by a garrison or a hero, or a neutral stack, fights it on a field of its own (FHexBattle). A battle a
 *  person fights or sees is shown (IsShown), on the field of AHexBattleArena while the planet waits; one nobody watches is fought at once
 *  (quick). What is left of the armies goes back, the hero that wins gains experience, the one that loses or retreats leaves the map with
 *  its army, a castle that falls is taken, and a neutral stack that loses is gone. The result is applied at the start of an update of the
 *  heroes, never during the step of a hero that set it off (ApplyPending)
 */
UCLASS()
class UHexBattles : public UObject
{
	GENERATED_BODY()

public:

	void Init(UHexHeroes* InHeroes);

	/** Starts a battle: shown when a person fights in it or sees it, else fought at once. Returns false and why when there is none to fight */
	bool Begin(const FHexBattleRequest& Request, FString& OutMessage);

	/** A battle is on the field */
	bool IsShown() const { return bShown; }

	FHexBattle& GetBattle() { return Battle; }
	const FHexBattle& GetBattle() const { return Battle; }
	const FHexBattleRequest& GetRequest() const { return Request; }

	/** The castle fought for, or null, and the level of its fort */
	const FHexCastle* GetCastle() const;
	int32 GetFortLevel() const;

	/** The side a person fights for, or INDEX_NONE when the game fights both */
	int32 GetPersonSide() const;

	/** The game gives the orders of a side: always for one no person fights, and for the person's once it asks for it (Auto) */
	bool IsSideAi(int32 Side) const { return Battle.GetSide(Side).bAi || bAuto[Side]; }
	void SetAuto(int32 Side, bool bOn) { bAuto[FMath::Clamp(Side, 0, 1)] = bOn; }

	/** The person ends the battle on the field: at once with its result (Quick), or by leaving it once it is over (Close) */
	void PlayQuick();
	void Close();

	/** Applies the result of a battle that ended and closed. Returns true while a battle is shown, when the heroes wait */
	bool ApplyPending();

	/** Where the battle is fought: the letter of the biome there and whether underground, for the ground of the field */
	TCHAR GetGroundLetter() const { return GroundLetter; }
	int32 GetTerrainLayer() const { return TerrainLayer; }
	bool IsUnderground() const { return Request.Layer == EHexPlanetLayer::Underground; }

	/** A line about how the battle ended, for the person */
	FString GetResultText() const;

	/** A name for a side, its hero or what it is */
	FString GetSideName(int32 Side) const;
	FLinearColor GetSideColor(int32 Side) const;

	/** The stacks of the army of the hero chosen against a hero, castle or neutral stack, weighed (FHexCreatureStats::GetPower) */
	float GetArmyPower(TConstArrayView<FHexStack> Army) const;

	/** Battles the game fought at once since the game started, for the log */
	int32 GetQuickCount() const { return QuickCount; }

	/** Broadcast when a battle comes onto the field, and when it leaves it */
	DECLARE_MULTICAST_DELEGATE(FOnBattle);
	FOnBattle OnShown;
	FOnBattle OnClosed;

	const UHexBattleConfig* GetConfig() const { return Config; }

private:

	UPROPERTY(Transient)
	TObjectPtr<UHexHeroes> Heroes;

	UPROPERTY(Transient)
	TObjectPtr<const UHexBattleConfig> Config;

	FHexBattle Battle;
	FHexBattleRequest Request;
	bool bShown = false;
	bool bPending = false;
	bool bAuto[2] = {false, false};
	TCHAR GroundLetter = 0;
	int32 TerrainLayer = 0;
	int32 Count = 0;
	int32 QuickCount = 0;

	/** How many stacks of the defender of a castle were its guard's, the rest its garrison's */
	int32 GuardSlots = 0;

	/** Gives the armies what is left of them, the experience, and the map what the battle changed */
	void Apply();
};
