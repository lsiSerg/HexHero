// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexBattleConfig.generated.h"

class UAnimSequence;
class UMaterialInterface;
class UTexture2D;
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

	/** Its picture in the panel of a castle, a render of the figure (Tools/HexUi/render_portraits.py), as the developer asked on 2026-11-13
	 *  to see the creatures that are hired; the upgraded version shows it in UpgradedTint */
	UPROPERTY(EditAnywhere, Category="Unit")
	TSoftObjectPtr<UTexture2D> Portrait;

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

	/** The surface of the figure is not one smooth color, as the developer found the figures like plasticine on 2026-11-13: a noise fixed on
	 *  its body at rest, DetailScale units of its mesh across, makes its color DetailTone lighter and darker and bumps its normal by DetailBump,
	 *  as skin, scales, cloth or bone; Roughness how it shines */
	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=0.1))
	float DetailScale = 10.0f;

	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=0, ClampMax=1))
	float DetailTone = 0.12f;

	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=0))
	float DetailBump = 0.35f;

	UPROPERTY(EditAnywhere, Category="Unit", meta=(ClampMin=0, ClampMax=1))
	float Roughness = 0.65f;

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

	/** A creature that flies and has legs to walk on walks where its way runs straight over the ground, as the developer asked on 2026-11-14,
	 *  by Walk, and takes off only over what is in the way, by Fly (Move without it); one without Walk always flies */
	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Walk;

	UPROPERTY(EditAnywhere, Category="Unit|Animation")
	FHexUnitAnimation Fly;
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

	/** The tint of the obstacles of M_HexBattleScan on this ground (their third to fifth custom values), so the rocks of the underground are
	 *  darker than in the sun */
	UPROPERTY(EditAnywhere, Category="Ground")
	FLinearColor ObstacleTint = FLinearColor::White;

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

	/** The row each of the seven slots of an army stands on at the start of a battle, on a field of 11 rows (scaled to another), so stacks
	 *  fight where they were put in the army, as the developer asked on 2026-11-13; the slots of a garrison behind those of the hero in a
	 *  castle stand in the column before */
	UPROPERTY(EditAnywhere, Category="Field")
	TArray<int32> ArmySlotRows = {0, 2, 4, 5, 6, 8, 10};

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
	 *  level of its faction that shoot, part of the castle, which only a catapult harms. The towers stand symmetric, as the developer asked on
	 *  2026-11-13: none in a fort, two at the ends of the wall in a citadel and two more beside the gate in a castle, so TowerRows lists the
	 *  ends first */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=2))
	int32 SiegeWallFromEnd = 4;

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> TowerRows = {0, 10, 4, 6};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> WallHealthByFort = {0, 100, 160, 240};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> GateHealthByFort = {0, 120, 200, 300};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> TowerHealthByFort = {0, 150, 220, 320};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<int32> TowersByFort = {0, 0, 2, 4};

	UPROPERTY(EditAnywhere, Category="Siege")
	TArray<float> TowerWeeksByFort = {0.0f, 0.0f, 1.0f, 0.75f};

	/** A stack of the attacker breaks a part of the wall, a section, the gate or a tower, in WallStrikeStrongMin to WallStrikeStrongMax
	 *  turns when it is the strongest stack of its side (by the power of its creatures times their count) and in WallStrikeWeakMin to
	 *  WallStrikeWeakMax when it is WallStrikeWeakShare of that or weaker, between them by how strong it is: each turn it deals the health
	 *  of the part over a number of turns rolled between them, as the developer asked on 2026-11-13 ("сильні юніти за 3-4 рази, слабші за 5-6
	 *  разів"; 2 and 3-4 turns earlier that day). A stack that strikes twice deals half of it each time */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	float WallStrikeStrongMin = 3.0f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	float WallStrikeStrongMax = 4.0f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	float WallStrikeWeakMin = 5.0f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	float WallStrikeWeakMax = 6.0f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=0, ClampMax=0.99))
	float WallStrikeWeakShare = 0.25f;

	/** The catapult, which a hero buys in a castle (CatapultCost of DA_HexCastles): as fast as CatapultSpeed in the order of turns, it shoots
	 *  at a part of the wall anywhere, hitting with CatapultHitChance for the health of the part over a number of shots rolled from
	 *  CatapultShotsMin to CatapultShotsMax, so it breaks a part in two shots at most, as the developer asked on 2026-11-13 ("катапульта має
	 *  знищувати за 2 максимум, з деяким рандомом за раз"; it dealt 60 to 110 and missed one shot in five until then), and has
	 *  CatapultHealth; one destroyed is lost to its hero */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultSpeed = 6;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=0, ClampMax=1))
	float CatapultHitChance = 1.0f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	float CatapultShotsMin = 1.4f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	float CatapultShotsMax = 2.0f;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultHealth = 400;

	/** A catapult is driven as a stack is, as the developer asked on 2026-11-14 ("їзде не далеко"): up to CatapultMoveHexes hexes a turn, off
	 *  its own hex onto the field and back, in a siege and on an open field alike; it strikes nobody */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=0))
	int32 CatapultMoveHexes = 4;

	/** Every catapult, of the attacker too since 2026-11-14, throws a stone at a stack of the other side for CatapultStoneMin to
	 *  CatapultStoneMax at an Attack of CatapultAttack; that of the attacker also at a part of the wall */
	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=0))
	int32 CatapultAttack = 10;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultStoneMin = 60;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 CatapultStoneMax = 120;

	/** A wall has a moat on the hexes before it, as the developer asked on 2026-11-13, with a bridge before the gate: a step into it costs a
	 *  walker MoatStepCost of its speed, half its speed in the water, as they asked the same day ("штраф на мув 50%"; it stopped a walker until
	 *  then), and a flyer flies over it */
	UPROPERTY(EditAnywhere, Category="Siege")
	bool bSiegeMoat = true;

	UPROPERTY(EditAnywhere, Category="Siege", meta=(ClampMin=1))
	int32 MoatStepCost = 2;

	/** How the wall shows on the field: the meshes of the castle by the level of its fort (WallMeshes, GateMeshes, TowerMeshes of
	 *  DA_HexCastles) in its material, SiegeWallHeight high, the shooters of a tower TowerFigureScale of the size of a figure, standing
	 *  TowerStandShare of the way up its mesh, as at their full size they hung in the air over it (until 2026-11-13), and the catapult as
	 *  CatapultMesh, CatapultSize across */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=10))
	float SiegeWallHeight = 105.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1, ClampMax=2))
	float TowerFigureScale = 0.6f;

	/** The towers on the field are TowerWidthScale times as wide as their meshes, as high, as the developer asked on 2026-11-13 */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.5, ClampMax=3))
	float TowerWidthScale = 1.8f;

	/** The props of a siege that are no meshes of the castle, as the developer asked on 2026-11-13 for a gate and a bridge more real and steps
	 *  with a texture: PropMaterial (M_HexBattleProp) with the timber of the gate and of the bridge (fetch_battle_textures.ps1) and the masonry
	 *  of the castles by their style (StoneTextures, StoneNormals: men, the undead), WoodTileSize and StoneTileSize units a tile, the planks
	 *  turned upright on the gate and across on the bridge by bGatePlanksTurned and bBridgePlanksTurned */
	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UMaterialInterface> PropMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleProp.M_HexBattleProp")));

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UTexture2D> GateWoodTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/Textures/T_BattleGateWood.T_BattleGateWood")));

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UTexture2D> GateWoodNormal = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/Textures/T_BattleGateWood_N.T_BattleGateWood_N")));

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UTexture2D> BridgeWoodTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/Textures/T_BattleBridgeWood.T_BattleBridgeWood")));

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UTexture2D> BridgeWoodNormal = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/Textures/T_BattleBridgeWood_N.T_BattleBridgeWood_N")));

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TArray<TSoftObjectPtr<UTexture2D>> StoneTextures = {
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Castles/T_CastleWall_Human.T_CastleWall_Human"))),
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Castles/T_CastleWall_Undead.T_CastleWall_Undead")))};

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TArray<TSoftObjectPtr<UTexture2D>> StoneNormals = {
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Castles/T_CastleWall_Human_N.T_CastleWall_Human_N"))),
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/HexPlanet/Castles/T_CastleWall_Undead_N.T_CastleWall_Undead_N")))};

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor StoneTint = FLinearColor(0.9f, 0.88f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor BridgeWoodColor = FLinearColor(0.85f, 0.8f, 0.72f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=1))
	float WoodTileSize = 90.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=1))
	float StoneTileSize = 120.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	bool bGatePlanksTurned = true;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	bool bBridgePlanksTurned = false;

	/** The arch of the gate: pointed, its sides circles GateArchRadius times half its opening, springing GateSpringShare of the wall up; its
	 *  leaves GateLeafThickness thick, and the bridge rises BridgeRise in its middle */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=1, ClampMax=3))
	float GateArchRadius = 1.5f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1))
	float GateSpringShare = 0.3f;

	/** The wall over the gate is a mesh of the castle as thick as its walls themselves, ArchThickShare of the bounds of the mesh of a wall,
	 *  which hold its buttress and plinth, and as high, ArchTopShare of them, which hold its merlons; its UV in tiles of CastleTile units of a
	 *  mesh, TILE of generate_planet_castles.py */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1, ClampMax=1))
	float ArchThickShare = 0.6f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1, ClampMax=1.2))
	float ArchTopShare = 0.9f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=1))
	float CastleTile = 36.0f;

	/** The tone of the stone of the meshes of the castle (STONE of generate_planet_castles.py), which the wall over the gate takes, and how far
	 *  above the point of its arch it rises where that stands higher than the wall, in units of a mesh */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1))
	float CastleStoneTone = 0.4f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float GateArchCrown = 9.0f;

	/** What fell of the wall is a ruin, as the developer asked on 2026-11-13 when it left a gap whose walls ended as if cut by a laser: a stub of
	 *  the wall RuinThickShare of the bounds of its mesh thick, in columns RuinColumnWidth units of a mesh wide, from RuinShoulderShare of its
	 *  height where it meets a wall that stands down to RuinBreachShare at its breach, in courses of RuinCourse units, with loose blocks on top
	 *  of RuinLooseShare of them; a tower broken off at RuinTowerMinShare to RuinTowerMaxShare of its height in RuinTowerSegments blocks around,
	 *  and the towers of a gate that fell at RuinGateMinShare to RuinGateMaxShare, with RuinGatePlanks planks of its leaves lying in the way */
	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0.1, ClampMax=1.5))
	float RuinThickShare = 0.85f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=1))
	float RuinColumnWidth = 9.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RuinShoulderShare = 0.88f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RuinBreachShare = 0.1f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=1))
	float RuinCourse = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RuinLooseShare = 0.45f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RuinTowerMinShare = 0.18f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RuinTowerMaxShare = 0.55f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=6, ClampMax=64))
	int32 RuinTowerSegments = 14;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RuinGateMinShare = 0.4f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RuinGateMaxShare = 0.75f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0))
	int32 RuinGatePlanks = 7;

	/** The blocks of a heap of rubble, RubbleBlocks a half of a part that fell (and a tower), RubbleSizeMin to RubbleSizeMax units of a mesh,
	 *  spilled up to RubbleSpread hexes from the wall, RubbleInShare of them into the yard, the heap RubbleHeapShare of the wall high at most */
	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0))
	int32 RubbleBlocks = 34;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0.5))
	float RubbleSizeMin = 3.5f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0.5))
	float RubbleSizeMax = 8.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0))
	float RubbleSpread = 0.7f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RubbleInShare = 0.4f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float RubbleHeapShare = 0.22f;

	/** A part that stands shows how damaged it is, as the developer asked on 2026-11-13: below DamageLightShare of its health a section loses
	 *  merlons and its top is chipped down to DamageLightBreach of its height in the middle, below DamageHeavyShare it is broken down to
	 *  DamageHeavyBreach there, DamageLightMerlons and DamageHeavyMerlons of its merlons left; a tower that is damaged has rubble at its foot and
	 *  heavily its top broken off (DamageTowerMinShare to DamageTowerMaxShare of its height), and the towers of a gate the same; each with a heap
	 *  of DamageLightBlocks or DamageHeavyBlocks blocks */
	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageLightShare = 0.67f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageHeavyShare = 0.34f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageLightBreach = 0.8f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageHeavyBreach = 0.45f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageLightMerlons = 0.55f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageHeavyMerlons = 0.2f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageTowerMinShare = 0.78f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1))
	float DamageTowerMaxShare = 0.96f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0))
	int32 DamageLightBlocks = 8;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0))
	int32 DamageHeavyBlocks = 18;

	/** The damage is played: when a part shows a worse stage CrumbleBlocks blocks a stage fall from its top and tumble (CrumbleGravity),
	 *  and a part that falls sinks CrumbleSink of its height into its rubble, over CrumbleTime seconds, as the developer asked on 2026-11-13 */
	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0))
	int32 CrumbleBlocks = 12;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=1))
	float CrumbleGravity = 1400.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0, ClampMax=1.5))
	float CrumbleSink = 0.9f;

	UPROPERTY(EditAnywhere, Category="Look|Siege|Ruins", meta=(ClampMin=0.1))
	float CrumbleTime = 1.1f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=1))
	float GateLeafThickness = 5.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float BridgeRise = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1.5))
	float TowerStandShare = 0.78f;

	/** The castle is closed, as the developer asked on 2026-11-13, so it reads as a castle and not as a wall in a field one could walk around:
	 *  from the ends of the front wall, WallEndReach of a row past its first and last row, outside the field, its walls turn back past the
	 *  last column, BackWallReach hexes beyond its middles, where the back wall closes it, with the hall of the castle (its mesh of the level
	 *  built, HallScale times the size of the wall) behind its middle and, from a citadel on, a keep in it, a tower KeepScale times as large */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1))
	float BackWallReach = 0.9f;

	/** The figure of a catapult stands CatapultBackShift of a hex radius behind the middle of its hex, away from where it throws, so it does
	 *  not reach over the stacks beside it, as the developer asked on 2026-11-14 (its hex moved off the grid by that before) */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1.5))
	float CatapultBackShift = 0.45f;

	/** The catapult of a castle stands in a bay of its back wall, behind the stack of its last row, as the developer asked on 2026-11-14 ("стіна
	 *  її обходе"): the wall goes out CatapultBayDepth hexes around it, CatapultBayHalfWidth hexes along it each way */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float CatapultBayDepth = 1.25f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float CatapultBayHalfWidth = 0.95f;

	/** A stack that walks through the gate waits on the hex before it until its leaves are GateWaitOpen open, as the developer asked on
	 *  2026-11-14 (it walked through them shut before) */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1))
	float GateWaitOpen = 0.85f;

	/** The yard is the front of the castle: its side walls go SideWallBeyond hexes on past the palace at its back, the hall of the castle on
	 *  PalaceSteps steps PalaceStepRise high each in StepsColor, the dwellings built beside it, towers at the corners of the palace and a keep
	 *  behind the hall from a citadel on, as the developer asked on 2026-11-13 */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float SideWallBeyond = 2.5f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	int32 PalaceSteps = 3;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float PalaceStepRise = 5.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor StepsColor = FLinearColor(0.55f, 0.53f, 0.5f, 1.0f);

	/** The gate: an opening GateOpening hex radii wide, 2 the longest diagonal of a hex, between two towers GateTowerScale of a tower, with two
	 *  leaves of timber GateLeafHeight of the wall high, which swing GateSwing degrees into the yard at GateSwingSpeed a second */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.5))
	float GateOpening = 2.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1))
	float GateTowerScale = 0.8f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1, ClampMax=1.5))
	float GateLeafHeight = 0.72f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=170))
	float GateSwing = 80.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1))
	float GateSwingSpeed = 2.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor GateWoodColor = FLinearColor(0.42f, 0.27f, 0.15f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor GateIronColor = FLinearColor(0.13f, 0.13f, 0.14f, 1.0f);

	/** The ground of the field is flat over the field and the castle, HillFlatMargin hexes around them, and rises over HillRamp hexes into hills
	 *  up to HillHeight high, a noise HillScale across, as the developer asked on 2026-11-13 for hills and plains; the ground draws the terrain
	 *  layer in relief by its normal (GroundRelief) and patches of the rock of the biome (GroundRockShare), more on the hills */
	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0))
	float HillHeight = 110.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=1))
	float HillScale = 1100.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0.1))
	float HillRamp = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0))
	float HillFlatMargin = 1.5f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0, ClampMax=3))
	float GroundRelief = 1.0f;

	/** The field itself rises and falls in gentle hills, as the developer asked on 2026-11-14 when it was too flat, as the hills of the planet
	 *  do: up to FieldHillHeight high, a noise of the place FieldHillScale units across, so a hill takes several hexes and its slopes run
	 *  across their sides and not as bumps in their middles; flat over a castle with its moat, FieldHillFlat hexes around it, rising over
	 *  FieldHillRamp hexes. Units, obstacles and the marks of the hexes stand on it; it changes nothing of the rules */
	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0))
	float FieldHillHeight = 40.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=1))
	float FieldHillScale = 360.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0))
	float FieldHillFlat = 1.6f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0.1))
	float FieldHillRamp = 2.5f;

	/** The marks of the hexes are cut until no side of their triangles is longer than DrapeMaxEdge of a hex radius, so they follow the hills
	 *  (AHexBattleArena::DrapeOnGround); shorter follows them closer and costs more triangles */
	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0.1, ClampMax=2))
	float DrapeMaxEdge = 0.35f;

	/** The ground takes its tone by its relief, as the developer asked the same day when it was one color: darker in hollows (GroundHollowShade),
	 *  lighter and drier on tops (GroundTopLight, GroundDryTint), by how far a point lies above or below the ground GroundReliefReach hexes
	 *  around it, GroundReliefScale units for all of it, and more rock on slopes (GroundSlopeRock) */
	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0, ClampMax=1))
	float GroundHollowShade = 0.78f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=1, ClampMax=2))
	float GroundTopLight = 1.12f;

	UPROPERTY(EditAnywhere, Category="Look|Ground")
	FLinearColor GroundDryTint = FLinearColor(1.06f, 1.0f, 0.84f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0.1))
	float GroundReliefReach = 2.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0.1))
	float GroundReliefScale = 10.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0, ClampMax=4))
	float GroundSlopeRock = 1.5f;

	/** The ground sinks EdgeSinkDepth over the last EdgeSinkWidth hexes to its edge, so its end does not show against the sky from a low
	 *  camera, as the developer asked on 2026-11-14 */
	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0))
	float EdgeSinkDepth = 900.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0.5))
	float EdgeSinkWidth = 7.0f;

	UPROPERTY(EditAnywhere, Category="Look|Ground", meta=(ClampMin=0, ClampMax=1))
	float GroundRockShare = 0.08f;

	/** The field is small and closed, as the developer asked on 2026-11-13: a dense forest of the decor of the ground (ForestCount trees in
	 *  clumps, ForestClumping how much) from ForestStart hexes past the field and the castle, BorderRocks RockCount of them RockSize hexes large
	 *  on BorderReach hexes, and beyond them the fog of war (FogMaterial) at FogHeight over the field, from FogStart to FogEnd past the field,
	 *  in FogColor with wisps of FogWispColor that drift at FogSpeed, FogUndergroundShade as light underground, its color the sky too */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float ForestStart = 1.3f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	int32 ForestCount = 620;

	/** How far past the rocks the forest goes on, under the fog, in hexes, and the grounds by their letters whose forest is of rocks and
	 *  stones (RockyDecor), as a desert has no wood, as the developer asked on 2026-11-13 */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float ForestBeyond = 4.5f;

	UPROPERTY(EditAnywhere, Category="Look|Border")
	FString RockyGroundLetters = TEXT("D");

	/** The obstacles on the field on such a ground, in place of those of its FHexBattleGround, and how large an obstacle is: its largest
	 *  extent, across or up, ObstacleSize hex radii from its middle (the photo-scanned rocks of import_battle_obstacles.py, as the developer
	 *  asked on 2026-11-13 for obstacles real, textured and light) */
	UPROPERTY(EditAnywhere, Category="Look|Border")
	TArray<TSoftObjectPtr<UStaticMesh>> RockyObstacles;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.1, ClampMax=1.5))
	float ObstacleSize = 0.8f;

	/** What stands in a desert besides its rocks, as the developer asked on 2026-11-13: DesertPropCount of DesertProps (cacti, the skeleton of
	 *  a beast, skulls, of blender_build_desert_props.py) among the rocks around the field, DesertPropSizeMin to DesertPropSizeMax hex radii
	 *  at their largest extent; and on any field FieldDecorCount of FieldDecor (bones) lying on hexes nobody stands on, FieldDecorSize hex
	 *  radii large, which block nothing */
	UPROPERTY(EditAnywhere, Category="Look|Border")
	TArray<TSoftObjectPtr<UStaticMesh>> DesertProps;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	int32 DesertPropCount = 70;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.05))
	float DesertPropSizeMin = 0.5f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.05))
	float DesertPropSizeMax = 1.3f;

	/** The great things of a desert, the skeletons of beasts, DesertLandmarkCount of them DesertLandmarkSizeMin to DesertLandmarkSizeMax hex
	 *  radii long */
	UPROPERTY(EditAnywhere, Category="Look|Border")
	TArray<TSoftObjectPtr<UStaticMesh>> DesertLandmarks;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	int32 DesertLandmarkCount = 5;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.1))
	float DesertLandmarkSizeMin = 2.2f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.1))
	float DesertLandmarkSizeMax = 3.2f;

	UPROPERTY(EditAnywhere, Category="Look|Border")
	TArray<TSoftObjectPtr<UStaticMesh>> FieldDecor;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	int32 FieldDecorCount = 5;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.05))
	float FieldDecorSize = 0.35f;

	/** The share of the obstacles with bones of FieldDecor lying on top */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0, ClampMax=1))
	float ObstacleBonesShare = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look|Border")
	TArray<TSoftObjectPtr<UStaticMesh>> RockyDecor = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Objects/Meshes/SM_PlanetObject_Rock_0.SM_PlanetObject_Rock_0"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Objects/Meshes/SM_PlanetObject_Rock_1.SM_PlanetObject_Rock_1"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Objects/Meshes/SM_PlanetObject_Rock_2.SM_PlanetObject_Rock_2"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Objects/Meshes/SM_PlanetObject_Stone_0.SM_PlanetObject_Stone_0"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Objects/Meshes/SM_PlanetObject_Stone_1.SM_PlanetObject_Stone_1"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Kit/SM_PlanetBoulder_0.SM_PlanetBoulder_0"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Kit/SM_PlanetBoulder_2.SM_PlanetBoulder_2")))};

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0, ClampMax=1))
	float ForestClumping = 0.5f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=1))
	float BorderReach = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Border")
	TArray<TSoftObjectPtr<UStaticMesh>> BorderRocks = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Kit/SM_PlanetBoulder_0.SM_PlanetBoulder_0"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Kit/SM_PlanetBoulder_1.SM_PlanetBoulder_1"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Kit/SM_PlanetBoulder_2.SM_PlanetBoulder_2"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/HexPlanet/Kit/SM_PlanetBoulder_3.SM_PlanetBoulder_3")))};

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	int32 RockCount = 70;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.1))
	float RockSize = 1.3f;

	UPROPERTY(EditAnywhere, Category="Look|Border")
	TSoftObjectPtr<UMaterialInterface> FogMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleFog.M_HexBattleFog")));

	UPROPERTY(EditAnywhere, Category="Look|Border")
	float FogHeight = 95.0f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogStart = 120.0f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=1))
	float FogEnd = 640.0f;

	/** How far over what it lies on the fog thins out to nothing, so it meets hills and trees softly, as the developer found its edge too
	 *  abrupt on 2026-11-13 (170 to 430 before) */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=1))
	float FogSoftness = 90.0f;

	/** How high the skirt of the fog rises over the forest, from FogStart past the field to FogEnd, so tall trees and the end of the ground
	 *  sink into it; 0 leaves it out */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogSkirtHeight = 0.0f;

	/** The fog of war around the field as the fog of war of the planet (M_HexBattleShroud, a post process on the camera of the field), as the
	 *  developer asked on 2026-11-13: a bank of cloud ShroudTop high from FogStart past the box of the field, whole from FogEnd, marched
	 *  through the view; the banks of FogMaterial (FogLayers) and the skirt are left out for it (0) */
	UPROPERTY(EditAnywhere, Category="Look|Shroud")
	TSoftObjectPtr<UMaterialInterface> ShroudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleShroud.M_HexBattleShroud")));

	UPROPERTY(EditAnywhere, Category="Look|Shroud")
	FLinearColor ShroudColor = FLinearColor(0.07f, 0.085f, 0.125f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=1))
	float ShroudTop = 420.0f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudDensity = 0.012f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0, ClampMax=1))
	float ShroudTaper = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0, ClampMax=1))
	float ShroudBaseShade = 0.55f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudTone = 0.5f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0.01, ClampMax=0.5))
	float ShroudSoftness = 0.15f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0, ClampMax=1))
	float ShroudEdgeNoise = 0.6f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=1))
	float ShroudNoiseSize = 180.0f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudDrift = 0.07f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudChurn = 0.1f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudWisp = 0.8f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=1))
	float ShroudWispSize = 480.0f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudWispSpeed = 0.09f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudPulse = 0.5f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=1))
	float ShroudPulseSize = 900.0f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud", meta=(ClampMin=0))
	float ShroudPulseRate = 0.11f;

	UPROPERTY(EditAnywhere, Category="Look|Shroud")
	FLinearColor ShroudPulseColor = FLinearColor(0.16f, 0.26f, 0.5f, 1.0f);

	/** How dense the skirt is against a bank, above 1 so the end of the ground does not show through it far away */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogSkirtOpacity = 1.8f;

	/** The fog is clear within FogCameraClear of the camera and grows over FogCameraFade more, so what the camera looks through never hides
	 *  the field */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogCameraClear = 300.0f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=1))
	float FogCameraFade = 500.0f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogSpeed = 0.15f;

	/** The fog is alive as the fog of war of the map, as the developer asked on 2026-11-13: FogLayers banks one over another, each
	 *  FogLayerStep of FogHeight apart and FogLayerFade as dense as the one under it, churning in a noise FogScale across, with wisps that wind
	 *  through it (FogWisp) and glows that swell and fade in it (FogPulse, FogPulseRate, FogPulseColor) */
	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=1, ClampMax=5))
	int32 FogLayers = 0;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogLayerStep = 0.45f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0, ClampMax=1))
	float FogLayerFade = 0.65f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0.0001))
	float FogScale = 0.0016f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogWisp = 0.8f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogPulse = 0.6f;

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0))
	float FogPulseRate = 0.12f;

	UPROPERTY(EditAnywhere, Category="Look|Border")
	FLinearColor FogPulseColor = FLinearColor(0.16f, 0.22f, 0.34f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Border")
	FLinearColor FogColor = FLinearColor(0.11f, 0.13f, 0.19f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Border")
	FLinearColor FogWispColor = FLinearColor(0.2f, 0.23f, 0.31f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Border", meta=(ClampMin=0, ClampMax=1))
	float FogUndergroundShade = 0.35f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1))
	float HallScale = 1.5f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	float HallYaw = 0.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1))
	float KeepScale = 1.5f;

	/** The wall dissolves within SiegeFadeRadius of the line from the camera to the hex under the cursor where it stands in front of it, keeping
	 *  SiegeFadeKeep of its pixels, as the walls of the underground do, as the developer asked on 2026-11-13 (SiegeFade of M_HexPlanetCastle) */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float SiegeFadeRadius = 75.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1))
	float SiegeFadeKeep = 0.25f;

	/** The sun over the field, its pitch and its turn, the same in a siege, and a fill light without shadows from the other side, FillShare as
	 *  bright as the sun, FillPitch high, so what lies in a shadow, as behind the wall of a castle, is seen, as the developer asked on
	 *  2026-11-13 when a sun turned for the siege darkened the front of the wall in its place */
	UPROPERTY(EditAnywhere, Category="Look")
	float SunPitch = -48.0f;

	UPROPERTY(EditAnywhere, Category="Look")
	float SunYaw = 25.0f;

	UPROPERTY(EditAnywhere, Category="Look", meta=(ClampMin=0, ClampMax=2))
	float FillShare = 0.35f;

	/** The camera comes to show the one that strikes or shoots and the one struck when either lies off the screen or within CameraFrameMargin
	 *  of its width of an edge, as the developer asked on 2026-11-13, far enough that both stand CameraFrameRoom apart of the width of the view */
	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0, ClampMax=0.45))
	float CameraFrameMargin = 0.08f;

	UPROPERTY(EditAnywhere, Category="Camera", meta=(ClampMin=0.1, ClampMax=1))
	float CameraFrameRoom = 0.6f;

	UPROPERTY(EditAnywhere, Category="Look")
	float FillPitch = -35.0f;

	UPROPERTY(EditAnywhere, Category="Look")
	FLinearColor FillColor = FLinearColor(0.75f, 0.85f, 1.0f, 1.0f);

	/** The catapult: its frame (CatapultMesh) and its throwing arm (CatapultArmMesh, turning about its origin, which stands at CatapultArmPivot
	 *  of the frame in units of its mesh), modelled by blender_build_catapult.py and imported by import_battle_catapult.py as the developer
	 *  asked on 2026-11-13, with the material slots Wood, Iron and Rope in the planks of M_HexBattleProp (CatapultWoodColor, CatapultIronColor,
	 *  CatapultRopeColor), CatapultSize at its largest extent, throwing along its X towards the enemy; it fires by swinging its arm
	 *  CatapultArmSwing degrees about its Y over CatapultArmFireTime seconds and lets it back down over the rest of the shot. Without a mesh it
	 *  is put together of basic shapes */
	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UStaticMesh> CatapultMesh;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UStaticMesh> CatapultArmMesh;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FVector CatapultArmPivot = FVector(-25.0, 0.0, 78.0);

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	float CatapultArmSwing = -100.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.05))
	float CatapultArmFireTime = 0.25f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor CatapultWoodColor = FLinearColor(0.85f, 0.75f, 0.65f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor CatapultIronColor = FLinearColor(0.16f, 0.16f, 0.17f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor CatapultRopeColor = FLinearColor(0.75f, 0.62f, 0.42f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=10))
	float CatapultSize = 120.0f;

	/** The wall runs straight across the field, as the developer asked on 2026-11-13, through every hex of its column, which zigzag by half a
	 *  hex from row to row, and its corners stand WallEndReach of the step between two rows past the first and the last, where a tower at an
	 *  end of it stands; the water of the moat, part of the ground of the biome as the developer asked the same day, lies along the hexes of
	 *  the moat, under the front wall to its face and out to the far sides of its hexes, MoatEndReach of a row past the corners, where it
	 *  turns into a shore that cuts the corners of the hexes by a share of up to MoatCornerCut by chance, so it shows no pattern, and winds by
	 *  MoatShoreNoise of a hex, MoatLift above the ground and drawn with MoatMaterial over a bed the ground draws, MoatBedColor, with wet banks
	 *  MoatBankWidth wide, darker by MoatBankColor; it waves MoatWaveHeight up and down and lets MoatOpacity of the bed through at most; the
	 *  bridge before the gate is BridgeWidth of a hex wide */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float WallEndReach = 0.8f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatEndReach = 0.35f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=0.5))
	float MoatShoreNoise = 0.08f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1))
	float MoatCornerCut = 1.0f;

	/** The ends of the moat round off over MoatEndRound of a hex, raggedly and each its own way, as the developer asked on 2026-11-13 */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1))
	float MoatEndRound = 1.6f;

	/** The foam of the moat, as the developer asked on 2026-11-13: a line along the shore MoatFoamWidth wide that comes and goes, foam where the
	 *  water meets the wall and the water washing up the face of the wall in MoatSplashHeight */
	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor MoatFoamColor = FLinearColor(0.82f, 0.86f, 0.84f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatFoamWidth = 7.0f;

	/** The foam where the water meets the wall, thinner than along the shore, both as wide as a noise that drifts says, from 0.35 of these up */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatWallFoamWidth = 3.5f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatSplashHeight = 14.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatBankWidth = 26.0f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor MoatBankColor = FLinearColor(0.55f, 0.5f, 0.45f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor MoatBedColor = FLinearColor(0.2f, 0.17f, 0.12f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatWaveHeight = 0.5f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1))
	float MoatOpacity = 0.78f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	float MoatLift = 1.2f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	TSoftObjectPtr<UMaterialInterface> MoatMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/HexPlanet/Battle/M_HexBattleWater.M_HexBattleWater")));

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1))
	float BridgeWidth = 0.8f;

	/** The values of MoatMaterial: the color of the water at its banks and in its middle, its ripples, how fast they run and how rough it is */
	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor MoatShallowColor = FLinearColor(0.08f, 0.17f, 0.17f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor MoatDeepColor = FLinearColor(0.025f, 0.065f, 0.075f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatRippleScale = 0.08f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatRippleSpeed = 0.6f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatRippleStrength = 0.3f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=1))
	float MoatRoughness = 0.12f;

	/** The water lights itself, unlit, by the sun and the sky: MoatLight times its color, a glint of the sun MoatGlint bright and the sky
	 *  (MoatSkyColor) MoatSkyReflection of the way where it is seen edge on */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatLight = 2.2f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatGlint = 1.5f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0))
	float MoatSkyReflection = 0.6f;

	UPROPERTY(EditAnywhere, Category="Look|Siege")
	FLinearColor MoatSkyColor = FLinearColor(0.35f, 0.45f, 0.55f, 1.0f);

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

	/** The lines of the hexes, one along each side centered on it, GridLineWidth wide, as the grid of the planet is (2026-11-14) */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor GridColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.45f);

	/** The outer edge of the field, a line FieldBorderWidth wide in FieldBorderColor, as the developer missed an outline around it on the
	 *  bright sand on 2026-11-14, when the grid was lighter too (alpha 0.3) */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor FieldBorderColor = FLinearColor(0.02f, 0.012f, 0.005f, 0.7f);

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.2))
	float FieldBorderWidth = 3.2f;

	/** How light the ground of the field is against the ground around it, which is darker anyway, as the developer found the sand of the
	 *  field too bright for its grid on 2026-11-14 */
	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.3, ClampMax=1.5))
	float FieldGroundTone = 0.86f;

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.2))
	float GridLineWidth = 1.4f;

	/** The hexes a stack can go to, the one under the cursor and those of the enemies it can strike; the colors of the field are deep and
	 *  the dark of the field dark since 2026-11-14, as the developer found them pale, the red dirty and the green lime */

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ReachColor = FLinearColor(1.0f, 0.85f, 0.4f, 0.05f);

	/** Where the stack whose turn it is cannot go shows as on the planet, as the developer asked on 2026-11-12: the hexes of the field it does
	 *  not reach under ReachOutsideColor, a see-through black, and the edge of those it reaches as a line ReachLineWidth wide in
	 *  ReachLineColor, with a light of its color ReachGlowWidth wide inside it that fades inwards (ReachGlowColor its alpha at the edge) */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ReachOutsideColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.6f);

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

	/** The hex under the cursor the stack would walk to, and the one it would strike from: a deep green, as the developer asked on 2026-11-14
	 *  (a pale white before) */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor HoverColor = FLinearColor(0.01f, 0.28f, 0.05f, 0.6f);

	/** The hex under the cursor the stack cannot go to, as the developer asked on 2026-11-14 */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor HoverBlockedColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.7f);

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor TargetColor = FLinearColor(0.75f, 0.02f, 0.01f, 0.55f);

	/** The cheat key of a battle (K, as the developer asked on 2026-11-13) deals what is under the cursor, a unit, a part of the wall or the
	 *  catapult, CheatHitShare of its health */
	UPROPERTY(EditAnywhere, Category="Cheats", meta=(ClampMin=0.01, ClampMax=1))
	float CheatHitShare = 0.25f;

	/** A figure struck reels away from where the blow came from, as the developer asked on 2026-11-13 for reactions to hits from every
	 *  side: it leans HitLean degrees and is pushed HitPush units away over HitReelTime seconds, on top of its animation of a hit */
	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0, ClampMax=45))
	float HitLean = 14.0f;

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0))
	float HitPush = 7.0f;

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.05))
	float HitReelTime = 0.45f;

	/** How long the shooters of a tower that falls take to fall onto its rubble */
	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.05))
	float TowerFallTime = 0.6f;

	/** Under the stack of the person that takes orders now */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor ActiveColor = FLinearColor(0.02f, 0.5f, 0.1f, 0.55f);

	/** The hex under each unit, as the developer asked on 2026-11-13: an outline UnitLineWidth wide at UnitLineInset of a hex radius from its
	 *  middle in UnitFriendColor for the units of the person and UnitEnemyColor for the others, UnitActingWidth times as wide for the one that
	 *  acts on a turn of the game, over a hex in UnitShadeColor, laid over the dark of the field, but for the stack that takes orders
	 *  (ActiveColor) and an enemy it can strike or shoot (TargetColor) */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor UnitFriendColor = FLinearColor(0.01f, 0.3f, 0.05f, 0.9f);

	/** Also the outline of a part of the wall the stack can strike or the catapult can hit, as the developer asked on 2026-11-14 that those
	 *  show as the enemies do; both outlines darker since that day, as the developer found them too bright */
	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor UnitEnemyColor = FLinearColor(0.38f, 0.015f, 0.01f, 0.9f);

	UPROPERTY(EditAnywhere, Category="Look|Field")
	FLinearColor UnitShadeColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.35f);

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.1))
	float UnitLineWidth = 2.2f;

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=0.5, ClampMax=1))
	float UnitLineInset = 0.9f;

	UPROPERTY(EditAnywhere, Category="Look|Field", meta=(ClampMin=1))
	float UnitActingWidth = 1.8f;

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

	/** How fast a figure turns, in degrees a second: to where it walks before it sets out, back towards the enemy once it stands, and to
	 *  what it strikes, as the developer asked on 2026-11-14 (it turned at once before); a walk waits until it faces within TurnLeadAngle
	 *  degrees of its first step */
	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=30))
	float FigureTurnSpeed = 360.0f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0, ClampMax=180))
	float TurnLeadAngle = 25.0f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.1))
	float FlySpeed = 6.0f;

	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.05))
	float StrikeTime = 0.7f;

	/** A catapult destroyed leaves a heap of its wood where it stood, as the developer asked on 2026-11-14: WreckBeams beams and boards up
	 *  to WreckSize of CatapultSize long, in the wood of the catapult WreckTone as light, as it is broken and charred */
	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=1, ClampMax=40))
	int32 WreckBeams = 11;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0.1, ClampMax=2))
	float WreckSize = 0.85f;

	UPROPERTY(EditAnywhere, Category="Look|Siege", meta=(ClampMin=0, ClampMax=2))
	float WreckTone = 0.6f;

	/** The share of StrikeTime at which the blow lands, when the damage shows, the count over the stack changes and the one struck reels, as
	 *  the developer asked on 2026-11-14; the swing plays on meanwhile */
	UPROPERTY(EditAnywhere, Category="Look|Timing", meta=(ClampMin=0.1, ClampMax=1))
	float StrikeHitShare = 0.55f;

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
