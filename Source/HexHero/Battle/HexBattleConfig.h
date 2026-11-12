// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexBattleConfig.generated.h"

class UAnimSequence;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;

/** What a spell does */
UENUM()
enum class EHexSpellKind : uint8
{
	/** Damage to one stack of the enemy */
	Damage,
	/** Damage to every stack in the hex aimed at and around it, friend or foe */
	AreaDamage,
	/** Heals one stack of its own, never above the creatures it has */
	Heal,
	/** Changes one stack for some rounds: of its own for the better, of the enemy for the worse */
	Effect,
};

/** Which stacks a spell is cast on */
UENUM()
enum class EHexSpellTarget : uint8
{
	Enemy,
	Friend,
	/** Any hex of the field, for a spell over an area */
	Hex,
};

/**
 *  A spell a hero casts in a battle, once a round, for its mana, as the developer chose on 2026-11-12: every hero knows the starter spells of
 *  DA_HexBattle until there is a mage guild. Its power is Power + PowerPerSpellPower times the Spell Power of the hero, and an effect lasts
 *  BaseRounds + RoundsPerSpellPower times it. The eight of MakeDefaultSpells and their numbers are Claude's
 */
USTRUCT(BlueprintType)
struct FHexSpell
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Spell")
	FName Id;

	UPROPERTY(EditAnywhere, Category="Spell")
	FString DisplayName;

	UPROPERTY(EditAnywhere, Category="Spell")
	FString Description;

	UPROPERTY(EditAnywhere, Category="Spell")
	EHexSpellKind Kind = EHexSpellKind::Damage;

	UPROPERTY(EditAnywhere, Category="Spell")
	EHexSpellTarget Target = EHexSpellTarget::Enemy;

	UPROPERTY(EditAnywhere, Category="Spell", meta=(ClampMin=0))
	int32 ManaCost = 5;

	/** The damage or the healing */
	UPROPERTY(EditAnywhere, Category="Spell", meta=(ClampMin=0))
	int32 Power = 10;

	UPROPERTY(EditAnywhere, Category="Spell", meta=(ClampMin=0))
	int32 PowerPerSpellPower = 10;

	/** How far an area reaches from the hex aimed at, in hexes */
	UPROPERTY(EditAnywhere, Category="Spell", meta=(ClampMin=0, ClampMax=3))
	int32 Radius = 0;

	/** An effect: how long it lasts, in rounds */
	UPROPERTY(EditAnywhere, Category="Spell|Effect", meta=(ClampMin=0))
	int32 BaseRounds = 1;

	UPROPERTY(EditAnywhere, Category="Spell|Effect", meta=(ClampMin=0))
	int32 RoundsPerSpellPower = 1;

	/** What it changes: hexes of speed added, or the share of the speed kept (1 keeps it), Attack and Defense added */
	UPROPERTY(EditAnywhere, Category="Spell|Effect")
	int32 SpeedAdd = 0;

	UPROPERTY(EditAnywhere, Category="Spell|Effect", meta=(ClampMin=0, ClampMax=2))
	float SpeedShare = 1.0f;

	UPROPERTY(EditAnywhere, Category="Spell|Effect")
	int32 AttackAdd = 0;

	UPROPERTY(EditAnywhere, Category="Spell|Effect")
	int32 DefenseAdd = 0;

	/** It deals the most damage it can, as a blessing on its weapons */
	UPROPERTY(EditAnywhere, Category="Spell|Effect")
	bool bMaxDamage = false;

	/** The share of the damage it takes that a ward turns aside */
	UPROPERTY(EditAnywhere, Category="Spell|Effect", meta=(ClampMin=0, ClampMax=0.95))
	float DamageTakenCut = 0.0f;

	/** The color of the light the spell shows on the field */
	UPROPERTY(EditAnywhere, Category="Spell|Look")
	FLinearColor Color = FLinearColor(1.0f, 0.6f, 0.2f, 1.0f);

	bool IsHelpful() const { return Target == EHexSpellTarget::Friend; }
};

/** Returns the starter spells: a bolt, a blast, mending, haste, slowness, a blessing, weakness and a ward, with numbers of Claude's */
TArray<FHexSpell> MakeDefaultSpells();

/** An animation of a unit on the field and how fast it plays */
USTRUCT(BlueprintType)
struct FHexUnitAnimation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Animation")
	TSoftObjectPtr<UAnimSequence> Sequence;

	UPROPERTY(EditAnywhere, Category="Animation", meta=(ClampMin=0.05, ClampMax=5))
	float Rate = 1.0f;
};

/**
 *  How a creature looks on the field of a battle and on the planet, where neutral stacks stand: a skeletal mesh of a CC0 model, as the
 *  developer chose on 2026-11-12, with its animations, the size of the creature in units of the world, how it is turned, its colors and
 *  what it carries, by the Id of the creature (FHexCreature::Id); its upgraded version may have a mesh and a tint of its own
 */
USTRUCT(BlueprintType)
struct FHexUnitLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Unit")
	FName Creature;

	UPROPERTY(EditAnywhere, Category="Unit")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** For the upgraded version, none to take Mesh */
	UPROPERTY(EditAnywhere, Category="Unit")
	TSoftObjectPtr<USkeletalMesh> UpgradedMesh;

	/** How high it stands, in units of the world on the field, and how its mesh is turned, as meshes come in facing other ways */
	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=1))
	float Height = 60.0f;

	UPROPERTY(EditAnywhere, Category="Unit")
	float Yaw = 0.0f;

	/** How high the mesh is drawn and where its foot lies below its origin, in its own units, as the bounds of a mesh whose skeleton is scaled
	 *  do not say it (blender_build_units.py measures both); 0 takes them from its bounds */
	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=0))
	float MeshHeight = 0.0f;

	UPROPERTY(EditAnywhere, Category="Unit")
	float MeshFoot = 0.0f;

	/** How high above the ground a flyer floats */
	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=0))
	float Hover = 0.0f;

	/** The colors of the mesh are multiplied by these, the upgraded version by its own */
	UPROPERTY(EditAnywhere, Category="Unit")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, Category="Unit")
	FLinearColor UpgradedTint = FLinearColor::White;

	/** It glows a little in the color of its side by the alpha of its vertices, which the build of the meshes gives the cloth and banners */
	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=0, ClampMax=1))
	float SideColorShare = 0.6f;

	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Idle;

	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Move;

	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Attack;

	/** A shooter shoots with this, else with Attack */
	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Shoot;

	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Hit;

	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Death;
};

/** What stands on the field and around it on ground of a biome, by the letters of the biomes (UHexBiomeConfig::PlanetLetter or UndergroundLetter) */
USTRUCT(BlueprintType)
struct FHexBattleGround
{
	GENERATED_BODY()

	/** The letters of the biomes it is for, as a text; an empty text serves every biome without one of its own */
	UPROPERTY(EditAnywhere, Category="Ground")
	FString Letters;

	UPROPERTY(EditAnywhere, Category="Ground")
	bool bUnderground = false;

	/** Meshes of the obstacles on the field, one hex each, and of what stands around it */
	UPROPERTY(EditAnywhere, Category="Ground")
	TArray<TSoftObjectPtr<UStaticMesh>> Obstacles;

	UPROPERTY(EditAnywhere, Category="Ground")
	TArray<TSoftObjectPtr<UStaticMesh>> Decor;

	/** The light: the sun, the sky and the color of the air */
	UPROPERTY(EditAnywhere, Category="Ground")
	FLinearColor SunColor = FLinearColor(1.0f, 0.95f, 0.85f, 1.0f);

	/** How bright the sun of the level is over the field, as a share of what it is over the planet, and the light of the sky */
	UPROPERTY(EditAnywhere, Category="Ground", meta=(ClampMin=0))
	float SunShare = 1.0f;

	UPROPERTY(EditAnywhere, Category="Ground", meta=(ClampMin=0))
	float SkyIntensity = 1.0f;

	/** The terrain layer of DA_HexTerrainLayers the ground of the field is drawn with, -1 for that of the biome of the cell fought on */
	UPROPERTY(EditAnywhere, Category="Ground")
	int32 TerrainLayer = -1;

	UPROPERTY(EditAnywhere, Category="Ground")
	FLinearColor SkyColor = FLinearColor(0.55f, 0.7f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Ground")
	FLinearColor BackgroundColor = FLinearColor(0.32f, 0.45f, 0.65f, 1.0f);
};

/**
 *  How neutral stacks are scattered over the planet (UHexNeutrals::Generate), as the developer asked on 2026-11-12 for stacks of neutral
 *  creatures that guard mines and passes: one beside MineShare of the mines and mills, and in the narrow places of the ways, a cell that
 *  closed would part the open cells around it, PassesPerThousand to a thousand cells a hero walks, Spacing cells apart and StartClearSteps
 *  from where the heroes and castles start. A stack is stronger the further it stands from them: its tier is its steps from the nearest
 *  start over TierSteps, at most MaxTier, one more underground; its level is its tier or one above it, and it has WeeksBase and WeeksPerTier
 *  a tier of what the dwelling of its creature offers a week, more or less by WeeksSpread. The numbers are Claude's
 */
USTRUCT(BlueprintType)
struct FHexNeutralGeneration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0, ClampMax=1))
	float MineShare = 0.85f;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0))
	float PassesPerThousand = 8.0f;

	/** And on open ground anywhere, OpenPerThousand to a thousand cells a hero walks, as the developer found too few on 2026-11-12; the
	 *  underground has UndergroundDensity times as many of both, as its passages are few and narrow */
	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0))
	float OpenPerThousand = 10.0f;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0))
	float UndergroundDensity = 3.0f;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=1))
	int32 Spacing = 4;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0))
	int32 StartClearSteps = 4;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=1))
	int32 TierSteps = 14;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0, ClampMax=6))
	int32 MaxTier = 3;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0))
	float WeeksBase = 1.0f;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0))
	float WeeksPerTier = 0.5f;

	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0, ClampMax=1))
	float WeeksSpread = 0.3f;

	/** The share of the stacks of each tier that are of the upgraded creature */
	UPROPERTY(EditAnywhere, Category="Neutrals", meta=(ClampMin=0, ClampMax=1))
	float UpgradedPerTier = 0.15f;
};

/**
 *  Every value of the battles (HexBattle, UHexBattles, AHexBattleArena): DA_HexBattle in Content/HexPlanet/Battle, which
 *  import_battle_units.py creates when it is missing, so tune it by hand; a missing one falls back to the defaults of the class.
 *
 *  As the developer decided on 2026-11-12: a battle is fought on its own small field of hexes, apart from the planet, which waits; the stacks
 *  act in rounds from the fastest to the slowest whoever owns them, the attacker first at equal speed; they walk, strike and are struck back,
 *  shoot, wait and defend; morale gives another turn or takes one, luck doubles a strike, the hero casts a spell a round, and the creatures
 *  have abilities by their roles. A person fights by hand or leaves it to the game, may retreat, and a battle nobody watches is fought at once.
 *  The numbers are Claude's
 */
UCLASS(BlueprintType)
class UHexBattleConfig : public UDataAsset
{
	GENERATED_BODY()

public:

	UHexBattleConfig();

	/** The field: this many hexes across and along, pointy-top, the attacker on the first column and the defender on the last */
	UPROPERTY(EditAnywhere, Category="Field", meta=(ClampMin=5, ClampMax=31))
	int32 Columns = 15;

	UPROPERTY(EditAnywhere, Category="Field", meta=(ClampMin=5, ClampMax=21))
	int32 Rows = 11;

	/** Obstacles on the field, from ObstaclesMin to ObstaclesMax, never on the first two and last two columns, and never so that a side cannot
	 *  reach the other walking */
	UPROPERTY(EditAnywhere, Category="Field", meta=(ClampMin=0))
	int32 ObstaclesMin = 3;

	UPROPERTY(EditAnywhere, Category="Field", meta=(ClampMin=0))
	int32 ObstaclesMax = 8;

	/** The damage of a strike: each point the Attack of the striker is above the Defense of the one struck adds AttackBonusPerPoint of it,
	 *  up to MaxAttackBonus, and each point below takes DefenseCutPerPoint off it, down to MaxDefenseCut */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float AttackBonusPerPoint = 0.05f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float MaxAttackBonus = 3.0f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=1))
	float DefenseCutPerPoint = 0.025f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=0.95))
	float MaxDefenseCut = 0.7f;

	/** A stack that defends has this share more Defense until its next turn */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float DefendShare = 0.3f;

	/** A shot at a stack further than ShotFarHexes deals ShotFarShare of its damage, and a shooter that strikes in melee MeleePenaltyShare */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=1))
	int32 ShotFarHexes = 10;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=1))
	float ShotFarShare = 0.5f;

	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0, ClampMax=1))
	float MeleePenaltyShare = 0.5f;

	/** A creature that charges (EHexAbility::Charge) strikes this share harder for every hex it ran to strike */
	UPROPERTY(EditAnywhere, Category="Rules|Abilities", meta=(ClampMin=0))
	float ChargeBonusPerHex = 0.05f;

	/** A creature that drains mana (EHexAbility::ManaDrain) takes this much from the hero of the other side at each of its turns */
	UPROPERTY(EditAnywhere, Category="Rules|Abilities", meta=(ClampMin=0))
	int32 ManaDrainPerTurn = 2;

	/** The share of the damage a creature that drains life (EHexAbility::LifeDrain) heals itself by */
	UPROPERTY(EditAnywhere, Category="Rules|Abilities", meta=(ClampMin=0, ClampMax=2))
	float LifeDrainShare = 0.5f;

	/** Morale: each point above 0 gives a stack MoraleChancePerPoint of another turn after its turn, each point below 0 BadMoraleChancePerPoint
	 *  of losing its turn; an army of one faction has OneFactionMorale, and each faction more in it takes MixedFactionMorale */
	UPROPERTY(EditAnywhere, Category="Rules|Morale and luck", meta=(ClampMin=0, ClampMax=1))
	float MoraleChancePerPoint = 1.0f / 24.0f;

	UPROPERTY(EditAnywhere, Category="Rules|Morale and luck", meta=(ClampMin=0, ClampMax=1))
	float BadMoraleChancePerPoint = 1.0f / 12.0f;

	UPROPERTY(EditAnywhere, Category="Rules|Morale and luck")
	int32 OneFactionMorale = 1;

	UPROPERTY(EditAnywhere, Category="Rules|Morale and luck")
	int32 MixedFactionMorale = -1;

	/** Luck: each point above 0 gives a strike LuckChancePerPoint of dealing double damage */
	UPROPERTY(EditAnywhere, Category="Rules|Morale and luck", meta=(ClampMin=0, ClampMax=1))
	float LuckChancePerPoint = 1.0f / 24.0f;

	/** A battle that has gone on this many rounds ends, the defender keeping the field */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=5))
	int32 MaxRounds = 100;

	/** Defenders in a castle have their Defense times the DefenseFactor of its fort, until there are walls on the field */
	UPROPERTY(EditAnywhere, Category="Rules")
	bool bCastleDefenseFactor = true;

	/** A castle with a fort is fought for behind its wall, as the developer asked on 2026-11-12: the wall stands SiegeWallFromEnd columns
	 *  before the last, across every row, its gate in the middle row and its towers on TowerRows, as many as TowersByFort gives the level of
	 *  its fort (index 0 none, 1 fort, 2 citadel, 3 castle: no tower in the base fort), each section, the gate and a tower as strong as the
	 *  ...ByFort values say, so a castle built up has stronger walls; each tower holds TowerWeeksByFort weeks of the shooters of the lowest
	 *  level of its faction that shoot, part of the castle, which only a catapult harms */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=2))
	int32 SiegeWallFromEnd = 4;

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> TowerRows = {2, 8, 0};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> WallHealthByFort = {0, 100, 160, 240};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> GateHealthByFort = {0, 120, 200, 300};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> TowerHealthByFort = {0, 150, 220, 320};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> TowersByFort = {0, 0, 1, 3};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<float> TowerWeeksByFort = {0.0f, 0.0f, 1.0f, 1.5f};

	/** A unit that strikes the wall deals this share of what it would deal a creature, without Attack or Defense */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=0))
	float WallStrikeShare = 0.35f;

	/** The catapult, which a hero buys in a castle (CatapultCost of DA_HexCastles): as fast as CatapultSpeed in the order of turns, it shoots
	 *  at a part of the wall anywhere, hitting with CatapultHitChance for CatapultDamageMin to CatapultDamageMax, and has CatapultHealth;
	 *  one destroyed is lost to its hero */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultSpeed = 6;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=0, ClampMax=1))
	float CatapultHitChance = 0.8f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultDamageMin = 60;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultDamageMax = 110;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultHealth = 400;

	/** How the wall shows on the field: the meshes of the castle by the level of its fort (WallMeshes, GateMeshes, TowerMeshes of
	 *  DA_HexCastles) in its material, SiegeWallHeight high, the shooters of a tower TowerFigureLift above the ground, and the catapult as
	 *  CatapultMesh, CatapultSize across */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=10))
	float SiegeWallHeight = 105.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float TowerFigureLift = 70.0f;

	/** A mesh of the catapult, which an artist gives; without one it is put together of a frame, an arm and a bucket */
	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UStaticMesh> CatapultMesh;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=10))
	float CatapultSize = 80.0f;

	/** The hero that wins gains this much experience a point of health of the creatures it killed */
	UPROPERTY(EditAnywhere, Category="Rules", meta=(ClampMin=0))
	float ExperiencePerHealth = 1.0f;

	/** The spells every hero knows (MakeDefaultSpells when empty) */
	UPROPERTY(EditAnywhere, Category="Spells")
	TArray<FHexSpell> Spells;

	/** The game fights for a side: it casts a spell when it is worth this much more than its mana of the power of the stacks, and a stack that
	 *  can reach no enemy waits for them while it is a shooter or the enemy has shooters fewer than AiAdvanceShooters */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0))
	float AiSpellWorth = 0.5f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0))
	int32 AiAdvanceShooters = 1;

	/** A hero the game moves attacks a hero or a neutral stack whose army its own outweighs this many times (FHexCreatureStats::GetPower),
	 *  within AiAttackReachDays of its movement */
	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0.5))
	float AiAttackMargin = 1.4f;

	UPROPERTY(EditAnywhere, Category="AI", meta=(ClampMin=0))
	float AiAttackReachDays = 2.0f;

	/** Neutral stacks on the planet */
	UPROPERTY(EditAnywhere, Category="Neutrals")
	FHexNeutralGeneration NeutralGeneration;

	/** How the field shows: each hex this many units of the world from its middle to its corners, and the field this far from the middle of
	 *  the planet, so it is a world of its own while the planet waits */
	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=10))
	float HexRadius = 48.0f;

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FVector ArenaOrigin = FVector(0.0, 0.0, -400000.0);

	/** The ground around the field reaches this many hexes past it */
	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0))
	float GroundMargin = 14.0f;

	/** The lines of the hexes and the hexes a stack can go to, the one under the cursor and those of the enemies it can strike */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor GridColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.35f);

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ReachColor = FLinearColor(1.0f, 0.85f, 0.4f, 0.05f);

	/** Where the stack whose turn it is cannot go shows as on the planet, as the developer asked on 2026-11-12: the hexes of the field it does
	 *  not reach under ReachOutsideColor, a see-through black, and the edge of those it reaches as a line ReachLineWidth wide in
	 *  ReachLineColor, with a light of its color ReachGlowWidth wide inside it that fades inwards (ReachGlowColor its alpha at the edge) */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ReachOutsideColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.42f);

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ReachLineColor = FLinearColor(1.0f, 0.8f, 0.35f, 0.95f);

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.1))
	float ReachLineWidth = 2.2f;

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ReachGlowColor = FLinearColor(1.0f, 0.75f, 0.3f, 0.3f);

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0))
	float ReachGlowWidth = 10.0f;

	/** The way to the hex under the cursor, as a way shows on the planet: a line PathLineWidth wide through a dot PathDotRadius large on each
	 *  hex, a ring PathEndRadius large on the last, in PathColor; a flyer's is a straight line, and the hex a stack strikes from ends it */
	UPROPERTY(EditAnywhere, Category="Look|Way")
	FLinearColor PathColor = FLinearColor(1.0f, 0.72f, 0.22f, 0.9f);

	UPROPERTY(EditAnywhere, Category="Look|Way", meta=(ClampMin=0.1))
	float PathLineWidth = 3.0f;

	UPROPERTY(EditAnywhere, Category="Look|Way", meta=(ClampMin=0.1))
	float PathDotRadius = 5.0f;

	UPROPERTY(EditAnywhere, Category="Look|Way", meta=(ClampMin=0.1))
	float PathEndRadius = 13.0f;

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor HoverColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.4f);

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor TargetColor = FLinearColor(1.0f, 0.2f, 0.15f, 0.45f);

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ActiveColor = FLinearColor(0.3f, 1.0f, 0.4f, 0.5f);

	/** The material of the ground, which draws a layer of the terrain (TerrainLayer, a parameter) by the biome where the battle is, and that of
	 *  the marks of the hexes */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	TSoftObjectPtr<UMaterialInterface> GroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleGround.M_HexBattleGround")));

	UPROPERTY(EditAnywhere, Category="Look|Field")
	TSoftObjectPtr<UMaterialInterface> MarkMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleMark.M_HexBattleMark")));

	/** The unit under the cursor has a thin outline along its silhouette, as the developer asked on 2026-11-12: its figure drawn again a
	 *  little larger, only its back faces, with OutlineMaterial (M_HexBattleOutline), OutlineWidth units wide and OutlineWidthPerDistance more a
	 *  unit of distance from the camera, so it is as thin from afar, in OutlineFriendColor or OutlineEnemyColor */
	UPROPERTY(EditAnywhere, Category="Look|Units")
	TSoftObjectPtr<UMaterialInterface> OutlineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleOutline.M_HexBattleOutline")));

	UPROPERTY(EditAnywhere, Category="Look|Units", meta=(ClampMin=0))
	float OutlineWidth = 0.4f;

	UPROPERTY(EditAnywhere, Category="Look|Units", meta=(ClampMin=0))
	float OutlineWidthPerDistance = 0.0012f;

	UPROPERTY(EditAnywhere, Category="Look|Units")
	FLinearColor OutlineFriendColor = FLinearColor(0.75f, 1.0f, 0.6f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Units")
	FLinearColor OutlineEnemyColor = FLinearColor(1.0f, 0.45f, 0.3f, 1.0f);

	/** The material of the units, which takes their tint and the color of their side */
	UPROPERTY(EditAnywhere, Category="Look|Units")
	TSoftObjectPtr<UMaterialInterface> UnitMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleUnit.M_HexBattleUnit")));

	/** The camera looks at the middle of the field from this far, this many degrees down and this wide, at a point CameraLookShift nearer
	 *  to it than the middle, so the field stands above the panel at the bottom of the screen */
	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=100))
	float CameraDistance = 1900.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=10, ClampMax=89))
	float CameraPitch = 45.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera")
	float CameraLookShift = 110.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=10, ClampMax=120))
	float CameraFieldOfView = 50.0f;

	/** The person moves the camera of the field, as the developer asked on 2026-11-12: the wheel brings it from CameraMinDistance to
	 *  CameraMaxDistance by CameraZoomStep a notch, lower towards CameraClosePitch near the field; Q and E and the right button held turn it
	 *  around the point it looks at (CameraTurnSpeed degrees a second, CameraMouseTurnSpeed degrees a pixel); the keys, the edges of the screen
	 *  and the left or middle button held move that point over the field at CameraScrollSpeed of the distance a second, at most
	 *  CameraScrollReach hexes past the middle of the field; CameraSmoothing is how fast it follows */
	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=100))
	float CameraMinDistance = 700.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=100))
	float CameraMaxDistance = 3200.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=0.01, ClampMax=0.9))
	float CameraZoomStep = 0.12f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=10, ClampMax=89))
	float CameraClosePitch = 32.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=1))
	float CameraTurnSpeed = 90.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=0.01))
	float CameraMouseTurnSpeed = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=0.01))
	float CameraScrollSpeed = 0.8f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=0))
	float CameraScrollReach = 9.0f;

	UPROPERTY(EditAnywhere, Category="Look|Camera", meta=(ClampMin=0.5))
	float CameraSmoothing = 10.0f;

	/** The exposure of the field, fixed */
	UPROPERTY(EditAnywhere, Category="Look|Camera")
	float ExposureBias = 0.0f;

	/** How fast a unit walks, in hexes a second, flies, how long it strikes and dies, how fast a shot flies, and the pause between turns */
	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.1))
	float WalkSpeed = 3.5f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.1))
	float FlySpeed = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.05))
	float StrikeTime = 0.7f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.05))
	float ShotSpeed = 14.0f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.05))
	float DeathTime = 1.0f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.0))
	float TurnPause = 0.25f;

	/** The game plays its turns this many times as fast while the person watches a battle it leaves to it (Auto) */
	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=1))
	float AutoSpeed = 2.0f;

	/** The mesh a shot flies as, and a spell shows as */
	UPROPERTY(EditAnywhere, Category="Look|Units")
	TSoftObjectPtr<UStaticMesh> ShotMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));

	UPROPERTY(EditAnywhere, Category="Look|Units", meta=(ClampMin=1))
	float ShotSize = 9.0f;

	/** Every figure on the field is this many times the height of its look, so they read from the camera */
	UPROPERTY(EditAnywhere, Category="Look|Units", meta=(ClampMin=0.1))
	float UnitScale = 1.8f;

	/** How each creature looks, by its Id; one without a look is drawn as a figure of its own */
	UPROPERTY(EditAnywhere, Category="Look|Units")
	TArray<FHexUnitLook> UnitLooks;

	/** A creature without a look of its own is drawn as this mesh, its height by its level */
	UPROPERTY(EditAnywhere, Category="Look|Units")
	TSoftObjectPtr<UStaticMesh> FallbackMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));

	/** What stands on the field and around it by the biome of the battle; the first with no letters serves the rest */
	UPROPERTY(EditAnywhere, Category="Look|Ground")
	TArray<FHexBattleGround> Grounds;

	/** Neutral stacks on the planet stand this many times smaller than on the field, and so many times more creatures than this show as more
	 *  figures, up to NeutralMaxFigures */
	UPROPERTY(EditAnywhere, Category="Look|Neutrals", meta=(ClampMin=0.01))
	float NeutralScale = 1.0f;

	UPROPERTY(EditAnywhere, Category="Look|Neutrals", meta=(ClampMin=1, ClampMax=5))
	int32 NeutralMaxFigures = 3;

	/** Returns the look of a creature, or null */
	const FHexUnitLook* FindLook(FName Creature) const;

	/** Returns the ground of the field for a biome letter */
	const FHexBattleGround* FindGround(TCHAR Letter, bool bUnderground) const;

	/** Returns the index of the spell of an Id, any case, or INDEX_NONE */
	int32 FindSpell(const FString& Id) const;

	/** Loads DA_HexBattle, or returns the defaults of the class with a warning when it is missing */
	static const UHexBattleConfig* Load();

	virtual void PostLoad() override;
};
