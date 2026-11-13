// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexPlanetFile.h"
#include "HexPlanetLayers.h"
#include "HexPlanetObjects.h"
#include "HexPlanetSurface.h"
#include "UObject/Object.h"
#include "HexCastles.h"
#include "HexNeutrals.h"
#include "HexPlanetPainter.generated.h"

class AHexPlanet;
class UHexPlanetConfig;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

/** What the left button does while painting is on */
enum class EHexPaintTool : uint8
{
	/** Nothing: it drags the planet, as with painting off, while the palette stays */
	None,
	Level,
	Biome,
	Relief,
	/** Puts the chosen kind of object on the cells (UHexPlanetObjects) */
	Object,
	/** Places objects by hand, one at the point clicked, and moves, turns, scales or changes the mesh of the one placed nearest it
	 *  (EHexPlacedTool) */
	Placed,
	/** Takes the cover and decor off the cells */
	Eraser,
	/** Lays rivers along the sides of cells, from the corner nearest the cursor to the next (UHexPlanetRivers) */
	River,
	/** Puts bridges on the sides with a river nearest the cursor */
	Bridge,
	/** Puts an entrance of the chosen network on a cell of the surface, or takes it away with Ctrl */
	Entrance,
	/** Takes the entrance on a cell of the surface away, as Ctrl with Entrance does, for which the developer wanted a button */
	EntranceRemove,
	/** Turns the entrance on a cell of the surface, or the exit on one underground, to the next side of its cell, or back with Ctrl
	 *  (FHexEntrance::Side, ExitSide), as the developer asked on 2026-11-12 */
	EntranceTurn,
	/** Carves a passage underground along the shortest way from the cell pressed to the cell released, as wide as the passages of the planet */
	Carve,
	/** Raises the walls along the voids under the brush, and along the voids beside its passages, by WallHeightStep, or lowers them with Ctrl,
	 *  down to the low curb of WallHeightMin (AHexPlanet::SetWallHeights) */
	WallHeight,
	/** Gives the walls along those voids back the height of their style */
	WallAuto,
	/** Puts a hero of the class and player chosen on a cell of either layer, where it stands when a game starts, or takes the one there away
	 *  with Ctrl (UHexHeroes), as a step of Undo */
	Hero,
	/** Puts a mine or a pile of the kind chosen on a cell of either layer, or takes the one there away with Ctrl (UHexSites), as a step of Undo */
	Site,
	/** Puts a castle of the faction and the owner chosen on the cell clicked and the two around its corner nearest the cursor, or takes the
	 *  one there away with Ctrl (UHexCastles), as a step of Undo */
	Castle,
	/** Puts a neutral stack of the creature and count chosen on a cell of either layer, or takes the one there away with Ctrl (UHexNeutrals),
	 *  as a step of Undo */
	Neutral,
};

/** What a click of the brush in relief mode does to the mountains and hills under it, once per cell and stroke */
enum class EHexReliefTool : uint8
{
	/** The next version of the piece, among those with the same sides */
	Version,
	/** The next turn, among those that give the same set of neighbors */
	Turn,
	/** Mirrored or not */
	Mirror,
	/** A higher or lower factor on the height */
	Higher,
	Lower,
	/** Back to the hash */
	Reset,
};

/** What a click does with objects placed by hand (EHexPaintTool::Placed), as the developer chose on 2026-11-10: a cell holds as many as it takes,
 *  each where it was placed, turned, scaled and of the mesh chosen; Ctrl goes the other way. The tools that change an object take the one
 *  nearest the cursor, and one of the cover or decor of a cell unpacks that cell into objects placed by hand first (UHexPlanetObjects::UnpackCell) */
enum class EHexPlacedTool : uint8
{
	/** Places one object of the kind chosen at the point clicked; Ctrl+click takes away the one nearest it */
	Place,
	/** Drags the object nearest the point pressed while the button is held, shown apart from the chunks, and puts it down on release */
	Move,
	/** Turns the one nearest the click on by PaintObjectTurn degrees, back with Ctrl */
	Turn,
	/** Makes it PaintObjectScale times larger, smaller with Ctrl */
	Size,
	/** Gives it the next mesh of its kind, the one before with Ctrl */
	Version,
	/** Takes away the one nearest the cursor, and as the button is dragged each one it passes, the Eraser of By hand */
	Erase,
};

/** The tabs of the palette: those of the surface, then those of the underground (GetTabLayer). Each keeps the tool chosen in it last */
enum class EHexPaintTab : uint8
{
	/** Levels and biomes of the surface */
	Ground,
	Relief,
	Objects,
	Rivers,
	/** The entrances to the underground and their networks */
	Entrances,
	/** The heroes that stand on the surface when a game starts */
	Heroes,
	/** The mines and piles of the surface */
	Sites,
	/** Void and passages underground, carving and the cave biomes */
	Maze,
	/** How the passages join the entrances of their networks, and generating them again */
	Links,
	/** The cave objects of the underground: stalagmites, mushrooms, crystals, bones and the rest, painted, erased and generated again */
	CaveObjects,
	/** The heroes that stand underground when a game starts */
	CaveHeroes,
	/** The mines and piles of the underground */
	CaveSites,
	/** The castles of the surface */
	Castles,
	/** The neutral stacks of the surface and of the underground */
	Neutrals,
	CaveNeutrals,
	/** The castles of the underground */
	CaveCastles,
};

/** Number of tabs of the palette */
constexpr int32 NumPaintTabs = 16;

/** Old and new levels, biomes, relief, objects, rivers and entrances of the cells of one step of Undo, a stroke or a button */
struct FHexPaintStep
{
	TMap<int32, int32> OldLevels;
	TMap<int32, int32> NewLevels;
	TMap<int32, int32> OldBiomes;
	TMap<int32, int32> NewBiomes;
	HexPlanetSurface::FReliefOverrides OldRelief;
	HexPlanetSurface::FReliefOverrides NewRelief;
	TMap<int32, FHexCellObjects> OldObjects;
	TMap<int32, FHexCellObjects> NewObjects;
	/** What stood on the sides of rivers before and after, by HexPlanetRivers::GetEdgeKey (UHexPlanetRivers::GetEdge) */
	TMap<uint64, uint8> OldRivers;
	TMap<uint64, uint8> NewRivers;
	/** The heights of the walls along voids set by hand before and after (AHexPlanet::SetWallHeights), -1 where the style of the void gives it */
	TMap<int32, float> OldWalls;
	TMap<int32, float> NewWalls;

	/** The cells of the underground a step of the surface changed with its own, the wall made under the mountain an entrance it put is built
	 *  into (AHexPlanet::FitEntrances), before and after, as the developer asked on 2026-11-12 for that wall to be undone with its entrance */
	TMap<int32, int32> OldCaveLevels;
	TMap<int32, int32> NewCaveLevels;
	TMap<int32, int32> OldCaveBiomes;
	TMap<int32, int32> NewCaveBiomes;

	/** All the entrances before and after, when the step changed them (AHexPlanet::SetEntrances) */
	bool bEntrances = false;
	TArray<FHexEntrance> OldEntrances;
	TArray<FHexEntrance> NewEntrances;

	/** All the heroes before and after, as lines of heroes.json, when the step placed or took away one (UHexHeroes::SetLines) */
	bool bHeroes = false;
	TArray<FHexHeroLine> OldHeroes;
	TArray<FHexHeroLine> NewHeroes;

	/** All the mines and piles before and after, as lines of sites.json, when the step placed or took away one (UHexSites::SetLines) */
	bool bSites = false;
	TArray<FHexSiteLine> OldSites;
	TArray<FHexSiteLine> NewSites;

	/** All the neutral stacks before and after, as lines of neutrals.json, when the step placed or took away one (UHexNeutrals::SetLines) */
	bool bNeutrals = false;
	TArray<FHexNeutralLine> OldNeutrals;
	TArray<FHexNeutralLine> NewNeutrals;

	/** All the castles before and after, as lines of castles.json, when the step placed or took away one (UHexCastles::SetLines) */
	bool bCastles = false;
	TArray<FHexCastleLine> OldCastles;
	TArray<FHexCastleLine> NewCastles;

	/** SeedOffset of the underground before and after, when the step is a new layout (UHexPlanetPainter::NewLayout) */
	bool bSeedOffset = false;
	int32 OldSeedOffset = 0;
	int32 NewSeedOffset = 0;

	/** Stamp of the step, from 1 in the order the steps were made (UHexPlanetPainter::AddStep), by which a layer knows it is back at the state
	 *  of its last save */
	int32 Id = 0;

	/** Returns true if the step changes nothing */
	bool IsEmpty() const
	{
		return NewLevels.IsEmpty() && NewBiomes.IsEmpty() && NewRelief.IsEmpty() && NewObjects.IsEmpty() && NewRivers.IsEmpty() && NewWalls.IsEmpty() && NewCaveLevels.IsEmpty()
			&& !(bEntrances && OldEntrances != NewEntrances) && !(bSeedOffset && OldSeedOffset != NewSeedOffset) && !(bHeroes && OldHeroes != NewHeroes)
			&& !(bSites && OldSites != NewSites) && !(bCastles && OldCastles != NewCastles) && !(bNeutrals && OldNeutrals != NewNeutrals);
	}
};

/** What the brush keeps of each layer it paints: its planet, the tiles of its painted cells, its own Undo and Redo, its tab, level and biome */
USTRUCT()
struct FHexPainterLayer
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	/** Values of the planet, with the colors of the levels and the height of the tiles */
	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetConfig> Settings;

	/** Flat tiles of the painted cells until the surface shows them, in AHexPlanet::GetPreviewMaterial */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> PaintPreview;

	EHexPaintTab Tab = EHexPaintTab::Ground;
	int32 Level = 0;
	int32 Biome = 0;

	/** The kinds of objects of the layer and after them those of the other layer that a hand places here (UHexPlanetObjects::FindPlaceableSets,
	 *  OwnSets of them of the layer), read once, so the palette has them before the layer is built, and those that can be painted among them
	 *  (GetPaintableSets), of which ObjectSet is the one the brush puts */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHexPlanetObjectSet>> LayerSets;
	TArray<int32> PaintableSets;
	int32 OwnSets = 0;
	int32 ObjectSet = 0;

	/** Cells given to the planet whose surface is not rebuilt yet, shown as tiles, with the level the surface still shows (X) and the new one (Y) */
	TMap<int32, FIntPoint> PendingLevels;

	TArray<FHexPaintStep> UndoSteps;
	TArray<FHexPaintStep> RedoSteps;

	/** Id of the last step that the cap of PaintUndoSteps dropped, 0 while none was, which stands for the top of Undo when it is empty, so an
	 *  emptied Undo whose dropped steps are still applied never reads as the saved state */
	int32 BaseId = 0;

	/** The top of Undo at the last save (UHexPlanetPainter::GetTop) */
	int32 SavedTop = 0;
};

/**
 *  Paints the cells of both layers of a planet, the one shown (SetTarget, which follows UHexPlanetLayers::OnPresented), with a brush of one to
 *  three rings of cells, each layer with its own tiles, Undo and Redo. While the button is held the cells painted a level show at once as flat
 *  tiles in the colors of their new levels (PaintPreview), and when it is released the stroke goes to AHexPlanet::SetCells, which rebuilds the
 *  surface around them in the background; the tiles go once it is shown. A cell whose new level does not allow its biome takes a fitting one.
 *  Biomes show at once, as the planet only uploads them to a texture, so a biome stroke goes to the planet while it is painted, and only to the
 *  cells whose level allows that biome. Each stroke is one step of Undo and Redo, with the levels and biomes it changed. The pentagons of the
 *  surface are never mountains, so a mountain brush paints them as hills. On the surface it also puts forests, bushes, stones and decor on cells
 *  and takes them off (UHexPlanetObjects), lays rivers and bridges and puts entrances to the underground; underground it paints void and passages,
 *  carves passages and generates the passages of a network again. With no tool the left button drags the planet while the palette stays. The
 *  tools sit in the tabs of the palette (EHexPaintTab), and each tab keeps the tool chosen in it last.
 */
UCLASS()
class UHexPlanetPainter : public UObject
{
	GENERATED_BODY()

public:

	/** Lowest and highest level a brush paints on the surface: deep sea and mountains */
	static constexpr int32 MinLevel = -2;
	static constexpr int32 MaxLevel = 2;

	/** Largest brush, in rings of cells around the one under the cursor plus one */
	static constexpr int32 MaxBrushSize = 3;

	/** Starts painting the layers of a planet, the underground null on a planet without one, with the tiles of the preview on each, and paints the
	 *  layer shown now */
	void Init(AHexPlanet* SurfacePlanet, AHexPlanet* UndergroundPlanet);

	/** Returns true if the painter has a layer */
	bool HasLayer(EHexPlanetLayer Layer) const;

	/** Returns the layer the brush paints, the one shown */
	EHexPlanetLayer GetTarget() const { return Target; }

	/** Paints another layer, as it was shown, with the tab and tool it had there; ends a stroke */
	void SetTarget(EHexPlanetLayer Layer);

	/** Returns the tab of the palette chosen on the layer painted */
	EHexPaintTab GetTab() const;

	/** Chooses a tab of the layer painted, which takes the tool chosen in it last */
	void SetTab(EHexPaintTab Tab);

	/** Returns the layer whose tools a tab holds, and its name */
	static EHexPlanetLayer GetTabLayer(EHexPaintTab Tab);
	static FText GetTabName(EHexPaintTab Tab);

	/** Returns true while the mouse paints instead of dragging the planet */
	bool IsActive() const { return bActive; }

	/** Turns painting on or off, which ends a stroke */
	void SetActive(bool bInActive);

	/** Returns the level the brush paints on the layer painted */
	int32 GetLevel() const;

	/** Returns the tool of the left button; one is chosen at a time, and choosing one clears the others */
	EHexPaintTool GetTool() const { return Tool; }

	/** Returns true while the left button paints with a tool, rather than dragging the planet */
	bool HasTool() const { return Tool != EHexPaintTool::None; }

	/** Puts the tool down, so the left button drags the planet while the palette stays */
	void ClearTool();

	/** Returns true while the brush paints levels */
	bool IsPaintingLevels() const { return Tool == EHexPaintTool::Level; }

	/** Sets the level the brush paints on the layer painted, and makes it paint levels in the tab of the levels; underground a level below 0 is
	 *  the void and any other a passage */
	void SetLevel(int32 InLevel);

	/** Returns true while the brush paints biomes */
	bool IsPaintingBiomes() const { return Tool == EHexPaintTool::Biome; }

	/** Returns true while the brush sets the relief of mountains and hills */
	bool IsPaintingRelief() const { return Tool == EHexPaintTool::Relief; }

	/** Returns the kinds of objects that can be put on cells of a layer, by their place on the palette (UHexPlanetObjects::GetPaintableSets), their
	 *  names, colors and whether they are a cover */
	int32 GetNumObjectSets(EHexPlanetLayer Layer) const;
	FText GetObjectSetName(EHexPlanetLayer Layer, int32 Set) const;
	FLinearColor GetObjectSetColor(EHexPlanetLayer Layer, int32 Set) const;
	bool IsObjectSetCover(EHexPlanetLayer Layer, int32 Set) const;

	/** Returns true if a kind of the palette of a layer is of the other layer, which a hand alone places there; choosing it places by hand */
	bool IsObjectSetForeign(EHexPlanetLayer Layer, int32 Set) const;

	/** Returns the kind of object the brush puts on the layer painted, by its place on the palette */
	int32 GetObjectSet() const;

	/** Sets the kind of object the brush puts on the layer painted, by its place on the palette, and makes it put objects */
	void SetObjectSet(int32 InSet);

	/** Returns the density a cover is put at, 1 to 3 */
	int32 GetObjectDensity() const { return ObjectDensity; }
	void SetObjectDensity(int32 InDensity) { ObjectDensity = FMath::Clamp(InDensity, 1, 3); }

	/** Returns true while the kinds of the palette place objects by hand, one at the point clicked, rather than putting them on the cells */
	bool IsPlacingByHand() const { return bPlacingByHand; }

	/** Places objects by hand or puts them on cells; a kind chosen goes on with the other, and so does its tool */
	void SetPlacingByHand(bool bByHand);

	/** Returns what a click does with the objects placed by hand, and chooses it, which places by hand */
	EHexPlacedTool GetPlacedTool() const { return PlacedTool; }
	void SetPlacedTool(EHexPlacedTool InTool);

	/** Returns true while the brush places by hand with the tool given */
	bool IsUsingPlacedTool(EHexPlacedTool InTool) const { return Tool == EHexPaintTool::Placed && PlacedTool == InTool; }

	/** Returns true while the brush takes objects off cells, or by hand takes away the one nearest the cursor (EHexPlacedTool::Erase) */
	bool IsErasing() const { return Tool == EHexPaintTool::Eraser || IsUsingPlacedTool(EHexPlacedTool::Erase); }
	void SetEraser();

	/** Returns the tab of the objects of the layer painted */
	EHexPaintTab GetObjectsTab() const { return Target == EHexPlanetLayer::Underground ? EHexPaintTab::CaveObjects : EHexPaintTab::Objects; }

	/** Gives the passages of the underground the covers and decor its generation gives them again (HexPlanetUnderground::GenerateObjects), by the
	 *  shares of the kinds, as one step of Undo; false if the underground is not painted or nothing changed */
	bool RegenerateObjects();

	/** Returns true while the brush lays rivers, or puts bridges; Ctrl takes them away */
	bool IsPaintingRivers() const { return Tool == EHexPaintTool::River; }
	void SetRiverTool() { SetTool(EHexPaintTool::River, EHexPaintTab::Rivers); }
	bool IsPlacingBridges() const { return Tool == EHexPaintTool::Bridge; }
	void SetBridgeTool() { SetTool(EHexPaintTool::Bridge, EHexPaintTab::Rivers); }

	/** Returns true while the brush puts entrances of the chosen network on the surface; Ctrl takes them away */
	bool IsPlacingEntrances() const { return Tool == EHexPaintTool::Entrance; }
	void SetEntranceTool() { SetTool(EHexPaintTool::Entrance, EHexPaintTab::Entrances); }
	bool IsRemovingEntrances() const { return Tool == EHexPaintTool::EntranceRemove; }
	void SetEntranceRemoveTool() { SetTool(EHexPaintTool::EntranceRemove, EHexPaintTab::Entrances); }
	bool IsTurningEntrances() const { return Tool == EHexPaintTool::EntranceTurn; }
	void SetEntranceTurnTool() { SetTool(EHexPaintTool::EntranceTurn, Target == EHexPlanetLayer::Underground ? EHexPaintTab::Links : EHexPaintTab::Entrances); }

	/** Returns true while a click puts a hero of the class and player chosen on a cell of the layer painted; Ctrl takes one away */
	bool IsPlacingHeroes() const { return Tool == EHexPaintTool::Hero; }
	void SetHeroTool() { SetTool(EHexPaintTool::Hero, GetHeroesTab()); }

	/** Returns the tab of the heroes of the layer painted */
	EHexPaintTab GetHeroesTab() const { return Target == EHexPlanetLayer::Underground ? EHexPaintTab::CaveHeroes : EHexPaintTab::Heroes; }

	/** The class and the player of the heroes a click places, indices into DA_HexHeroes */
	int32 GetHeroClass() const { return HeroClass; }
	void SetHeroClass(int32 InClass) { HeroClass = FMath::Max(InClass, 0); }
	/** Whether the game moves the heroes of a player by itself (UHexHeroes::IsPlayerAi), and the switch of the Heroes tab that turns it */
	bool IsPlayerAi(int32 Player) const;
	void TogglePlayerAi(int32 Player);

	int32 GetHeroPlayer() const { return HeroPlayer; }
	void SetHeroPlayer(int32 InPlayer) { HeroPlayer = FMath::Max(InPlayer, 0); }

	/** Returns true while a click puts a mine or a pile of the kind chosen on a cell of the layer painted; Ctrl takes one away */
	bool IsPlacingSites() const { return Tool == EHexPaintTool::Site; }
	void SetSiteTool() { SetTool(EHexPaintTool::Site, GetSitesTab()); }
	EHexPaintTab GetSitesTab() const { return Target == EHexPlanetLayer::Underground ? EHexPaintTab::CaveSites : EHexPaintTab::Sites; }

	/** Scatters the sites over both layers again in place of all there are (UHexSites::Generate), each time another layout, or takes them
	 *  all away, as one step of Undo; false if nothing changed */
	bool GenerateSites(bool bClear = false);

	/** Returns true while the brush places castles, of the faction and for the player chosen, -1 for a castle of nobody */
	bool IsPlacingCastles() const { return Tool == EHexPaintTool::Castle; }
	void SetCastleTool() { SetTool(EHexPaintTool::Castle, Target == EHexPlanetLayer::Underground ? EHexPaintTab::CaveCastles : EHexPaintTab::Castles); }
	int32 GetCastleFaction() const { return CastleFaction; }
	void SetCastleFaction(int32 InFaction) { CastleFaction = FMath::Max(InFaction, 0); }
	int32 GetCastleOwner() const { return CastleOwner; }
	void SetCastleOwner(int32 InOwner) { CastleOwner = FMath::Max(InOwner, -1); }

	/** Returns true while the brush places neutral stacks, and what it places: a creature of a faction by its level from 0, upgraded or not,
	 *  so many of it */
	bool IsPlacingNeutrals() const { return Tool == EHexPaintTool::Neutral; }
	void SetNeutralTool() { SetTool(EHexPaintTool::Neutral, Target == EHexPlanetLayer::Underground ? EHexPaintTab::CaveNeutrals : EHexPaintTab::Neutrals); }
	int32 GetNeutralFaction() const { return NeutralFaction; }
	void SetNeutralFaction(int32 InFaction) { NeutralFaction = FMath::Max(InFaction, 0); }
	int32 GetNeutralLevel() const { return NeutralLevel; }
	void SetNeutralLevel(int32 InLevel) { NeutralLevel = FMath::Clamp(InLevel, 0, NumCreatureLevels - 1); }
	int32 GetNeutralCount() const { return NeutralCount; }
	void SetNeutralCount(int32 InCount) { NeutralCount = FMath::Max(InCount, 1); }
	bool IsNeutralUpgraded() const { return bNeutralUpgraded; }
	void SetNeutralUpgraded(bool bOn) { bNeutralUpgraded = bOn; }

	/** Scatters neutral stacks over both layers in place of all there are (UHexNeutrals::Generate), or takes them all away, as one step of
	 *  Undo; false if nothing changed */
	bool GenerateNeutrals(bool bClear = false);

	/** The kind of the sites a click places, an index into DA_HexSites */
	int32 GetSiteKind() const { return SiteKind; }
	void SetSiteKind(int32 InKind) { SiteKind = FMath::Max(InKind, 0); }

	/** Returns true while the brush carves passages underground */
	bool IsCarving() const { return Tool == EHexPaintTool::Carve; }
	void SetCarveTool() { SetTool(EHexPaintTool::Carve, EHexPaintTab::Maze); }

	/** Returns true while the brush sets the heights of the walls along the voids underground, and chooses WallHeight or WallAuto */
	bool IsSettingWalls() const { return Tool == EHexPaintTool::WallHeight || Tool == EHexPaintTool::WallAuto; }
	void SetWallTool(EHexPaintTool InTool) { SetTool(InTool == EHexPaintTool::WallAuto ? EHexPaintTool::WallAuto : EHexPaintTool::WallHeight, EHexPaintTab::Maze); }

	/** Returns the network of the chips of the palette, 0 to 7 for A to H, which new entrances take and which Regenerate network makes again */
	int32 GetNetwork() const { return Network; }
	void SetNetwork(int32 InNetwork) { Network = FMath::Clamp(InNetwork, 0, HexPlanetFile::MaxNetworks - 1); }

	/** Returns the color of a network, NetworkColors of the surface */
	FLinearColor GetNetworkColor(int32 InNetwork) const;

	/** Returns how many entrances a network has, those that cannot be used too */
	int32 GetNetworkEntrances(int32 InNetwork) const;

	/** Returns the width of a passage the brush carves, PassageWidth of the underground of the planet */
	int32 GetPassageWidth() const;

	/** Generates the passages of a network underground again, the others kept, or of all networks for INDEX_NONE, as one step of Undo of the
	 *  underground; false if it is not painted or nothing changed */
	bool Regenerate(int32 InNetwork);

	/** Gives the underground another maze for the same entrances: SeedOffset of planet.json goes up by one, saved with the planet, and all
	 *  networks are generated again with it, as one step of Undo that also takes the offset back; false if it is not painted or nothing changed */
	bool NewLayout();

	/** Returns a few lines on how the passages and entrances of the underground fit together, for the Links tab */
	FText GetLinksText() const;

	/** Sets the point on the planet under the cursor, by which the river brush finds the nearest corner and side */
	void SetCursorPoint(const FVector& Point) { CursorPoint = Point; }

	/** Shows, for the cell under the cursor, where an entrance would be put and which way it would open, as an arrow, or hides it */
	void UpdateEntranceHint(int32 Cell);

	/** Returns the tool of relief */
	EHexReliefTool GetReliefTool() const { return ReliefTool; }

	/** Sets the tool of relief, and makes the brush set relief */
	void SetReliefTool(EHexReliefTool InTool) { ReliefTool = InTool; SetTool(EHexPaintTool::Relief, EHexPaintTab::Relief); }

	/** Returns the biome the brush paints on the layer painted, an index into AHexPlanet::GetBiomes */
	int32 GetBiome() const;

	/** Sets the biome the brush paints on the layer painted, and makes it paint biomes */
	void SetBiome(int32 InBiome);

	/** Returns the number of biomes that can be painted on a layer, their names and their colors, the FileColor of their asset */
	int32 GetNumBiomes(EHexPlanetLayer Layer) const;
	FText GetBiomeName(EHexPlanetLayer Layer, int32 InBiome) const;

	/** Returns the tooltip of the button of a biome: which cells it paints, as underground the styles of the voids paint only voids */
	FText GetBiomeTip(EHexPlanetLayer Layer, int32 InBiome) const;
	FLinearColor GetBiomeColor(EHexPlanetLayer Layer, int32 InBiome) const;

	/** Returns the size of the brush, 1 to MaxBrushSize */
	int32 GetBrushSize() const { return BrushSize; }

	/** Sets the size of the brush */
	void SetBrushSize(int32 InSize) { BrushSize = FMath::Clamp(InSize, 1, MaxBrushSize); }

	/** Returns true if the tool paints the rings of the brush; an entrance, a carved passage, a river, a bridge and an object placed by hand take a
	 *  cell, a way, a side or a point, whatever its size */
	bool UsesBrushSize() const
	{
		return Tool != EHexPaintTool::Entrance && Tool != EHexPaintTool::EntranceTurn && Tool != EHexPaintTool::EntranceRemove && Tool != EHexPaintTool::Carve && Tool != EHexPaintTool::River && Tool != EHexPaintTool::Bridge && Tool != EHexPaintTool::Placed
			&& Tool != EHexPaintTool::Hero && Tool != EHexPaintTool::Site && Tool != EHexPaintTool::Castle && Tool != EHexPaintTool::Neutral;
	}

	/** Returns how many rings of cells the outline of the brush shows: its size, or one cell for the tools that ignore the size */
	int32 GetOutlineSize() const { return UsesBrushSize() ? BrushSize : 1; }

	/** Makes the tools work as with Ctrl held, or not, whatever the keyboard says, for the console (hex.PlanetPaint) */
	void SetBackwards(bool bInBackwards) { bBackwards = bInBackwards; }

	/** Starts a stroke at the cell under the cursor */
	void BeginStroke(int32 Cell);

	/** Paints the cells on the way from the last cell of the stroke to this one */
	void ContinueStroke(int32 Cell);

	/** Gives the cells of the stroke their levels on the planet and keeps it as a step of Undo */
	void EndStroke();

	/** Returns true while a stroke is painted */
	bool IsPainting() const { return bPainting; }

	/** Returns true if there is a step of the layer painted to undo or redo */
	bool CanUndo() const;
	bool CanRedo() const;

	/** Undoes the last step of the layer painted, or redoes the last step undone */
	void Undo();
	void Redo();

	/** Returns true while the planet painted rebuilds the surface of changed levels */
	bool IsRebuilding() const;

	/** Returns the color of a level on a layer: of DA_HexPlanet on the surface, and of the biome the generation gives it underground */
	FLinearColor GetLevelColor(EHexPlanetLayer Layer, int32 InLevel) const;

	/** Returns the name of a level on a layer */
	static FText GetLevelName(EHexPlanetLayer Layer, int32 InLevel);

private:

	/** Both layers, by EHexPlanetLayer, the underground without a planet on a planet that has none */
	UPROPERTY(Transient)
	TArray<FHexPainterLayer> Layers;

	/** Planet painted and its values, those of the layer of Target */
	UPROPERTY(Transient)
	TObjectPtr<AHexPlanet> Planet;

	UPROPERTY(Transient)
	TObjectPtr<const UHexPlanetConfig> Settings;

	EHexPlanetLayer Target = EHexPlanetLayer::Surface;

	/** The tool each tab had last, by EHexPaintTab */
	EHexPaintTool TabTools[NumPaintTabs] = {EHexPaintTool::Level, EHexPaintTool::None, EHexPaintTool::None, EHexPaintTool::None, EHexPaintTool::None,
		EHexPaintTool::None, EHexPaintTool::None, EHexPaintTool::Level, EHexPaintTool::None, EHexPaintTool::None, EHexPaintTool::None, EHexPaintTool::None, EHexPaintTool::None,
		EHexPaintTool::None, EHexPaintTool::None, EHexPaintTool::None};

	bool bActive = false;
	bool bPainting = false;
	bool bBackwards = false;
	EHexPaintTool Tool = EHexPaintTool::Level;
	int32 ObjectDensity = 3;
	bool bPlacingByHand = false;
	EHexPlacedTool PlacedTool = EHexPlacedTool::Place;

	/** The object that a stroke of Move drags, taken out of its cell while it is held and shown apart (UHexPlanetObjects::SetDragPreview), and the
	 *  cell it would be put down on, INDEX_NONE while none is held */
	int32 HeldCell = INDEX_NONE;
	FHexPlacedObject HeldObject;

	/** Puts down the object Move holds, on release or when the stroke ends otherwise */
	void DropHeld();

	/** Places, takes away, drags, turns, scales or changes the mesh of an object placed by hand at the point under the cursor, the first time of a
	 *  stroke, and for Move every frame after it */
	void PaintPlaced(int32 Cell, bool bFirst);

	/** Puts the objects of a cell, changed, into the stroke and gives them to the planet at once */
	void SetStrokeObjects(int32 Cell, const FHexCellObjects& Objects);

	/** Shows why an object could not be placed or changed, for a few seconds */
	static void ShowPlacedProblem(const FString& Problem);
	EHexReliefTool ReliefTool = EHexReliefTool::Version;
	int32 BrushSize = 1;
	int32 Network = 0;
	int32 HeroClass = 0;
	int32 HeroPlayer = 0;
	int32 CastleFaction = 0;
	int32 CastleOwner = 0;
	int32 SiteKind = 0;
	int32 NeutralFaction = 0;
	int32 NeutralLevel = 0;
	int32 NeutralCount = 10;
	bool bNeutralUpgraded = false;

	/** Last cell under the cursor in the stroke, and the cell the stroke of the carving brush began at */
	int32 LastCell = INDEX_NONE;
	int32 CarveFrom = INDEX_NONE;

	/** Point on the planet under the cursor, and the corner the river brush stood at last in the stroke */
	FVector CursorPoint = FVector::ZeroVector;
	int32 LastCorner = INDEX_NONE;

	/** The cells that meet at each corner of the grid, made the first time the river brush needs them */
	TArray<TArray<int32, TInlineAllocator<3>>> CornerCells;

	/** Stroke painted now */
	FHexPaintStep Stroke;

	/** Id the next step of Undo takes, counting from 1 over both layers */
	int32 NextStepId = 1;

	/** Returns the Id of the top step of Undo of a layer, or its BaseId when Undo is empty */
	static int32 GetTop(const FHexPainterLayer& State) { return State.UndoSteps.IsEmpty() ? State.BaseId : State.UndoSteps.Last().Id; }

	/** Takes the tops of both layers as those of the saved state, after a save (AHexPlanet::OnSaved) */
	void OnSaved();

	/** After Undo or Redo: back at the tops of the last save, with nothing changed outside the steps, both layers count as saved again;
	 *  else the layer painted counts as edited */
	void UpdateSavedState();

	/** Returns the state of the layer painted */
	FHexPainterLayer& GetLayerState() { return Layers[int32(Target)]; }
	const FHexPainterLayer& GetLayerState() const { return Layers[int32(Target)]; }

	/** Chooses a tool and the tab that holds it, which keeps it as its last */
	void SetTool(EHexPaintTool InTool, EHexPaintTab Tab);

	/** Shows the links of the underground while its Links tab is chosen and painting is on */
	void UpdateLinksShown();

	/** Returns true while Ctrl is held or the console asked for it, which makes a tool take away or go back */
	bool IsBackwards() const;

	/** Paints the brush around a cell */
	void PaintAround(int32 Cell);

	/** Lays or takes away a river from the last corner to the corner of the cell nearest the cursor, or puts or takes away a bridge */
	void PaintRiver(int32 Cell);

	/** Puts an entrance of the network on a cell, or takes one away, at once, as part of the stroke */
	void PaintEntrance(int32 Cell);

	/** Turns the entrance or exit on a cell to the next side */
	void PaintEntranceTurn(int32 Cell);

	/** Returns the cell of the entrance a click on a cell means: the cell itself when one stands on it, else that of the entrance whose mouth
	 *  is built into the cell, the one nearest the cursor, so Turn and Remove take an entrance by its mountain or its wall too, as the
	 *  developer could do neither by the mountain on 2026-11-12; the cell itself when there is none */
	int32 FindEntranceCellAt(int32 Cell) const;

	/** Gives where a click on a cell of the surface would put an entrance: the cell clicked, the mountain its mouth is built into, and the
	 *  cell of plains or hills beside it nearest the cursor, on which it stands. Returns false when it cannot be put there */
	bool FindEntrancePlace(int32 Clicked, int32& OutMountain, int32& OutBefore) const;

	/** The arrow that shows it while the tool of the entrances is chosen and nothing is painted (UpdatePreview) */
	int32 HintMountain = INDEX_NONE;
	int32 HintBefore = INDEX_NONE;

	/** Puts a hero of the class and player chosen on a cell, or takes the one there away, at once, as part of the stroke */
	void PaintHero(int32 Cell);

	/** Puts a mine or a pile of the kind chosen on a cell, or takes the one there away, at once, as part of the stroke */
	void PaintSite(int32 Cell);
	void PaintCastle(int32 Cell);
	void PaintNeutral(int32 Cell);

	/** Makes the stroke of the carving brush the passage along the shortest way from where it began to a cell */
	void PaintCarve(int32 Cell);

	/** Returns the sides of the shortest way along sides of cells between two corners, by HexPlanetRivers::GetEdgeKey */
	TArray<uint64> FindCornerPath(int32 From, int32 To);

	/** Gives the planet painted the levels, biomes and the rest of a step and shows the levels as tiles until it is rebuilt */
	void Apply(const TMap<int32, int32>& Levels, const TMap<int32, int32>& Biomes, const HexPlanetSurface::FReliefOverrides& Relief = {},
		const TMap<int32, FHexCellObjects>& Objects = {}, const TMap<uint64, uint8>& Rivers = {}, const TArray<FHexEntrance>* Entrances = nullptr,
		const int32* SeedOffset = nullptr, const TMap<int32, float>& Walls = {});

	/** Gives the underground the cells of it a step of the surface changed (FHexPaintStep::OldCaveLevels and the rest) */
	void ApplyCave(const TMap<int32, int32>& Levels, const TMap<int32, int32>& Biomes);

	/** Puts into a step that the walls along a cell go back to the height of its style, where the step changes what the cell is to the walls (a
	 *  passage, or a void of another style, AHexPlanet::GetWallStyle) and a height set by hand stands for them, so painting a lake or lava lowers
	 *  them again; also into Changed, for the planet */
	void ClearWallHeight(FHexPaintStep& Step, int32 Cell, int32 OldLevel, int32 OldBiome, int32 NewLevel, int32 NewBiome, TMap<int32, float>* Changed = nullptr) const;

	/** Raises, lowers or gives back to their style the walls along the voids of the brush and those beside its passages, once per void and stroke */
	void PaintWalls(TConstArrayView<int32> Brush);

	/** Generates the maze of the underground again from the manifest of the surface, all networks or one, into a step of the cells that changed,
	 *  with the objects the generation gives the cells that became passages and none on those that became void */
	bool MakeRegenerateStep(int32 InNetwork, FHexPaintStep& OutStep, FString& OutReport) const;

	/** Returns the set index (UHexPlanetObjects::GetSets) of a place on the palette of a layer, or INDEX_NONE */
	int32 GetPaintableSet(EHexPlanetLayer Layer, int32 Set) const;

	/** Keeps a step as the last of Undo of the layer painted, which clears its Redo */
	void AddStep(FHexPaintStep&& Step);

	/** Returns the relief a click of the tool gives a cell, or false if it cannot change it, as it has no piece or its neighbors fix it */
	bool GetToolRelief(int32 Cell, HexPlanetSurface::FReliefOverride& OutRelief) const;

	/** Drops the tiles of a layer once its planet shows every level given to it */
	void OnLevelsRebuilt(int32 Layer);

	/** Returns the level a brush gives a cell, as a pentagon of the surface is never a mountain */
	int32 GetPaintedLevel(int32 Cell, int32 InLevel) const;

	/** Returns the radius at which the tile of a cell of a layer lies */
	double GetTileRadius(const FHexPainterLayer& State, int32 OldLevel, int32 NewLevel) const;

	/** Builds the tiles of the pending levels of a layer and, on the layer painted, of the stroke */
	void UpdatePreview(int32 Layer);
};
