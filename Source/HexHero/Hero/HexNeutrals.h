// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HexCastleConfig.h"
#include "HexPlanetLayers.h"
#include "HexNeutrals.generated.h"

class AHexPlanet;
class UHexHeroes;
class UHexBattleConfig;
class USceneComponent;

/** A line of neutrals.json: a neutral stack, its creature by the Ids of its faction and of the creature */
USTRUCT()
struct FHexNeutralLine
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	FString Faction;

	UPROPERTY()
	FString Creature;

	UPROPERTY()
	bool Upgraded = false;

	UPROPERTY()
	int32 Count = 1;

	UPROPERTY()
	FString Layer;

	UPROPERTY()
	int32 Cell = 0;

	bool operator==(const FHexNeutralLine& Other) const
	{
		return Id == Other.Id && Faction == Other.Faction && Creature == Other.Creature && Upgraded == Other.Upgraded && Count == Other.Count && Layer == Other.Layer
			&& Cell == Other.Cell;
	}
};

/** neutrals.json of a planet, at the root of its folder, a line a stack, which Git can merge */
USTRUCT()
struct FHexNeutralsFile
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	TArray<FHexNeutralLine> Neutrals;
};

/** A neutral stack on the planet */
struct FHexNeutral
{
	int32 Id = 0;
	EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
	int32 Cell = INDEX_NONE;
	FHexStack Stack;
};

/**
 *  Stacks of neutral creatures that stand on the planet, as the developer asked on 2026-11-12: a stack fills its cell, so nothing walks
 *  through it, and a hero sent onto it fights it from the cell beside it (UHexBattles); one that wins takes nothing but the way, one that
 *  loses leaves the map. They guard mines and narrow places of the ways (Generate, NeutralGeneration of DA_HexBattle), are placed by the
 *  Neutrals tab of the palette on either layer, kept in neutrals.json, and drawn as one to NeutralMaxFigures figures of their creature
 *  (FHexUnitLook of DA_HexBattle) on the ground of their cell, hidden under the fog of war of the person who plays
 */
UCLASS()
class UHexNeutrals : public UObject
{
	GENERATED_BODY()

public:

	void Init(AHexPlanet* InSurface, UHexHeroes* InHeroes);

	/** Places the figures on the layers that are built and shows those the fog of war does not hide, every frame */
	void Update();

	const TArray<FHexNeutral>& GetNeutrals() const { return Neutrals; }

	/** Returns the stack on a cell of a layer, or null */
	const FHexNeutral* FindAt(EHexPlanetLayer Layer, int32 Cell) const;
	const FHexNeutral* Find(int32 Id) const;

	/** Puts a stack on a cell. Returns its Id, or 0 and why not */
	int32 Add(EHexPlanetLayer Layer, int32 Cell, const FHexStack& Stack, FString& OutProblem);

	bool RemoveAt(EHexPlanetLayer Layer, int32 Cell);
	bool Remove(int32 Id);

	/** What is left of a stack after a battle it won */
	void SetCount(int32 Id, int32 Count);

	/** Scatters stacks over both layers in place of all there are, by NeutralGeneration of DA_HexBattle; another layout each time without a
	 *  seed. Returns how many it placed and says how */
	int32 Generate(FString& OutReport, const int32* Seed = nullptr);
	bool Clear();

	/** A line for the panel at the cursor about the stack on a cell, empty without one */
	FString GetCellText(EHexPlanetLayer Layer, int32 Cell) const;

	/** The name of a creature, by the stack of it, and how many of a stack there are in words, as a hero sees them from afar */
	FString GetCreatureName(const FHexStack& Stack) const;
	static FString DescribeCount(int32 Count);

	/** The name of many of a creature, by the simple rule of English */
	static FString Plural(const FString& Name);

	bool IsEdited() const { return SortLines(MakeLines()) != SavedLines; }
	bool Save(const FString& PlanetFolder, FString& OutError);
	TArray<FHexNeutralLine> MakeLines() const;
	void SetLines(const TArray<FHexNeutralLine>& Lines);

	/** A new game: every stack as neutrals.json has it */
	void Restart();

	/** The planet of a layer changed its ground: the figures there stand anew */
	void OnLayerRebuilt(EHexPlanetLayer Layer);

private:

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Surface;

	UPROPERTY(Transient)
	TObjectPtr<UHexHeroes> Heroes;

	UPROPERTY(Transient)
	TObjectPtr<const UHexCastleConfig> Castles;

	UPROPERTY(Transient)
	TObjectPtr<const UHexBattleConfig> Config;

	TArray<FHexNeutral> Neutrals;
	TArray<FHexNeutralLine> SavedLines;
	int32 NextId = 1;

	/** The figures of each stack by its Id, components of the planet of its layer, and what they were placed for */
	struct FBody
	{
		TArray<TWeakObjectPtr<USceneComponent>> Figures;
		int32 Cell = INDEX_NONE;
		EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
		int32 Count = 0;
		bool bShown = false;
	};
	TMap<int32, FBody> Bodies;

	static TArray<FHexNeutralLine> SortLines(TArray<FHexNeutralLine> Lines);
	void RemoveBody(int32 Id);
	void PlaceBody(const FHexNeutral& Neutral);
	bool IsFree(EHexPlanetLayer Layer, int32 Cell) const;
};
