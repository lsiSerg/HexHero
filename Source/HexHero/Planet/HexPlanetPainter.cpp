// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetPainter.h"
#include "HexHero.h"
#include "HexHeroes.h"
#include "HexSites.h"
#include "HexPlanet.h"
#include "HexPlanetConfig.h"
#include "HexBiomeConfig.h"
#include "HexPlanetObjectSet.h"
#include "HexPlanetRivers.h"
#include "HexPlanetUnderground.h"
#include "HexSphereGrid.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#define LOCTEXT_NAMESPACE "HexPlanetPainter"

void UHexPlanetPainter::Init(AHexPlanet* SurfacePlanet, AHexPlanet* UndergroundPlanet)
{
	Layers.SetNum(NumPlanetLayers);
	Layers[int32(EHexPlanetLayer::Underground)].Tab = EHexPaintTab::Maze;
	Layers[int32(EHexPlanetLayer::Underground)].Level = HexPlanetUnderground::PassageLevel;

	AHexPlanet* const LayerPlanets[NumPlanetLayers] = {SurfacePlanet, UndergroundPlanet};
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		FHexPainterLayer& State = Layers[Layer];
		State.Planet = LayerPlanets[Layer];
		if (!State.Planet)
		{
			continue;
		}

		State.Settings = State.Planet->PlanetConfig.LoadSynchronous();
		if (!State.Settings)
		{
			State.Settings = GetDefault<UHexPlanetConfig>();
		}

		// each layer draws the tiles of its own cells, hidden with it
		State.PaintPreview = NewObject<UProceduralMeshComponent>(State.Planet, TEXT("PaintPreview"));
		State.PaintPreview->SetupAttachment(State.Planet->GetRootComponent());
		State.PaintPreview->SetCastShadow(false);
		State.PaintPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		State.PaintPreview->RegisterComponent();

		// the kinds of objects of the layer and those of the other layer a hand places here, so the palette shows them before the layer is built,
		// as the planet counts them (FHexCellObjects)
		State.LayerSets = UHexPlanetObjects::FindPlaceableSets(EHexPlanetLayer(Layer), &State.OwnSets);
		State.PaintableSets = UHexPlanetObjects::GetPaintableSets(State.LayerSets);

		State.Planet->OnLevelsRebuilt.AddUObject(this, &UHexPlanetPainter::OnLevelsRebuilt, Layer);
		State.Planet->OnSaved.AddUObject(this, &UHexPlanetPainter::OnSaved);
		// the state the planet was opened in counts as saved, at an empty Undo
		State.SavedTop = GetTop(State);
	}

	// the layer shown, and whichever is shown later
	UHexPlanetLayers* LayerSet = SurfacePlanet ? UHexPlanetLayers::Get(SurfacePlanet->GetWorld()) : nullptr;
	Target = EHexPlanetLayer::Surface;
	Planet = Layers[0].Planet;
	Settings = Layers[0].Settings;
	if (LayerSet)
	{
		SetTarget(LayerSet->GetPresentedLayer());
		LayerSet->OnPresented.AddUObject(this, &UHexPlanetPainter::SetTarget);
	}
}

bool UHexPlanetPainter::HasLayer(EHexPlanetLayer Layer) const
{
	return Layers.IsValidIndex(int32(Layer)) && Layers[int32(Layer)].Planet != nullptr;
}

void UHexPlanetPainter::SetTarget(EHexPlanetLayer Layer)
{
	if (!HasLayer(Layer) || (Layer == Target && Planet == Layers[int32(Layer)].Planet))
	{
		return;
	}
	if (bPainting)
	{
		EndStroke();
	}

	// each layer comes back to the tab it had, with the tool chosen there last
	TabTools[int32(GetLayerState().Tab)] = Tool;
	Target = Layer;
	Planet = GetLayerState().Planet;
	Settings = GetLayerState().Settings;
	Tool = TabTools[int32(GetLayerState().Tab)];
	LastCell = INDEX_NONE;
	LastCorner = INDEX_NONE;
	CornerCells.Reset();
	UpdateLinksShown();
}

EHexPaintTab UHexPlanetPainter::GetTab() const
{
	return Layers.IsValidIndex(int32(Target)) ? GetLayerState().Tab : EHexPaintTab::Ground;
}

void UHexPlanetPainter::SetTab(EHexPaintTab Tab)
{
	if (GetTabLayer(Tab) != Target || !Layers.IsValidIndex(int32(Target)) || GetLayerState().Tab == Tab)
	{
		return;
	}
	if (bPainting)
	{
		EndStroke();
	}
	TabTools[int32(GetLayerState().Tab)] = Tool;
	GetLayerState().Tab = Tab;
	Tool = TabTools[int32(Tab)];
	UpdateLinksShown();
}

EHexPlanetLayer UHexPlanetPainter::GetTabLayer(EHexPaintTab Tab)
{
	// by name, as the tabs added later stand after those of the underground in the enum (until 2026-11-13 the castles and the neutrals of the surface counted as
	// tabs of the underground, so their tools were never chosen there)
	switch (Tab)
	{
	case EHexPaintTab::Maze:
	case EHexPaintTab::Links:
	case EHexPaintTab::CaveObjects:
	case EHexPaintTab::CaveHeroes:
	case EHexPaintTab::CaveSites:
	case EHexPaintTab::CaveNeutrals:
	case EHexPaintTab::CaveCastles:
		return EHexPlanetLayer::Underground;
	default:
		return EHexPlanetLayer::Surface;
	}
}

void UHexPlanetPainter::SetTool(EHexPaintTool InTool, EHexPaintTab Tab)
{
	// a tool of the other layer waits until that layer is painted
	if (GetTabLayer(Tab) != Target)
	{
		return;
	}
	SetTab(Tab);
	Tool = InTool;
	TabTools[int32(Tab)] = InTool;
}

FText UHexPlanetPainter::GetTabName(EHexPaintTab Tab)
{
	switch (Tab)
	{
	case EHexPaintTab::Ground: return LOCTEXT("TabGround", "Ground");
	case EHexPaintTab::Relief: return LOCTEXT("TabRelief", "Relief");
	case EHexPaintTab::Objects: return LOCTEXT("TabObjects", "Objects");
	case EHexPaintTab::Rivers: return LOCTEXT("TabRivers", "Rivers");
	case EHexPaintTab::Entrances: return LOCTEXT("TabEntrances", "Entrances");
	case EHexPaintTab::Heroes:
	case EHexPaintTab::CaveHeroes: return LOCTEXT("TabHeroes", "Heroes");
	case EHexPaintTab::Sites:
	case EHexPaintTab::CaveSites: return LOCTEXT("TabSites", "Mines");
	case EHexPaintTab::Castles:
	case EHexPaintTab::CaveCastles: return LOCTEXT("TabCastles", "Castles");
	case EHexPaintTab::Neutrals:
	case EHexPaintTab::CaveNeutrals: return LOCTEXT("TabNeutrals", "Neutrals");
	case EHexPaintTab::Maze: return LOCTEXT("TabMaze", "Maze");
	case EHexPaintTab::Links: return LOCTEXT("TabLinks", "Links");
	default: return LOCTEXT("TabCaveObjects", "Objects");
	}
}

void UHexPlanetPainter::UpdateLinksShown()
{
	if (HasLayer(EHexPlanetLayer::Underground))
	{
		Layers[int32(EHexPlanetLayer::Underground)].Planet->SetLinksShown(bActive && Target == EHexPlanetLayer::Underground && GetTab() == EHexPaintTab::Links);
	}
}

void UHexPlanetPainter::SetActive(bool bInActive)
{
	if (!bInActive && bPainting)
	{
		EndStroke();
	}
	bActive = bInActive;
	UpdateLinksShown();
}

bool UHexPlanetPainter::IsBackwards() const
{
	return bBackwards || (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsControlDown());
}

bool UHexPlanetPainter::IsRebuilding() const
{
	return Planet && Planet->IsRebuilding();
}

FLinearColor UHexPlanetPainter::GetLevelColor(EHexPlanetLayer Layer, int32 InLevel) const
{
	if (!HasLayer(Layer))
	{
		return FLinearColor::Gray;
	}
	const FHexPainterLayer& State = Layers[int32(Layer)];

	// underground the color of the cave biome the generation gives the level, so the tiles look like what they become
	if (Layer == EHexPlanetLayer::Underground)
	{
		const TCHAR Letter = HexPlanetUnderground::IsPassage(int8(InLevel)) ? HexPlanetUnderground::Biome::Floor : HexPlanetUnderground::Biome::Abyss;
		const int32 Biome = State.Planet->GetBiomeLetters().Find(Letter);
		return Biome != INDEX_NONE ? GetBiomeColor(Layer, Biome) : FLinearColor::Gray;
	}
	const UHexPlanetConfig* Config = State.Settings ? State.Settings.Get() : GetDefault<UHexPlanetConfig>();
	return Config->LevelColors.IsValidIndex(InLevel - MinLevel) ? Config->LevelColors[InLevel - MinLevel] : FLinearColor::Gray;
}

int32 UHexPlanetPainter::GetLevel() const
{
	return Layers.IsValidIndex(int32(Target)) ? GetLayerState().Level : 0;
}

void UHexPlanetPainter::SetLevel(int32 InLevel)
{
	if (!Layers.IsValidIndex(int32(Target)))
	{
		return;
	}
	const bool bUnderground = Target == EHexPlanetLayer::Underground;
	GetLayerState().Level = bUnderground ? (InLevel < 0 ? HexPlanetUnderground::VoidLevel : HexPlanetUnderground::PassageLevel) : FMath::Clamp(InLevel, MinLevel, MaxLevel);
	SetTool(EHexPaintTool::Level, bUnderground ? EHexPaintTab::Maze : EHexPaintTab::Ground);
}

int32 UHexPlanetPainter::GetBiome() const
{
	return Layers.IsValidIndex(int32(Target)) ? GetLayerState().Biome : 0;
}

void UHexPlanetPainter::SetBiome(int32 InBiome)
{
	if (!Layers.IsValidIndex(int32(Target)))
	{
		return;
	}
	GetLayerState().Biome = InBiome;
	SetTool(EHexPaintTool::Biome, Target == EHexPlanetLayer::Underground ? EHexPaintTab::Maze : EHexPaintTab::Ground);
}

int32 UHexPlanetPainter::GetNumBiomes(EHexPlanetLayer Layer) const
{
	return HasLayer(Layer) ? Layers[int32(Layer)].Planet->GetBiomes().Num() : 0;
}

FText UHexPlanetPainter::GetBiomeName(EHexPlanetLayer Layer, int32 InBiome) const
{
	return InBiome >= 0 && InBiome < GetNumBiomes(Layer) ? FText::FromString(Layers[int32(Layer)].Planet->GetBiomes()[InBiome]->DisplayName) : FText::GetEmpty();
}

FText UHexPlanetPainter::GetBiomeTip(EHexPlanetLayer Layer, int32 InBiome) const
{
	if (InBiome < 0 || InBiome >= GetNumBiomes(Layer))
	{
		return FText::GetEmpty();
	}
	const UHexBiomeConfig* Biome = Layers[int32(Layer)].Planet->GetBiomes()[InBiome];
	if (Layer == EHexPlanetLayer::Underground && Biome && !Biome->AllowedLevels.Contains(HexPlanetUnderground::PassageLevel))
	{
		return LOCTEXT("VoidBiomeTip", "Paints the look of a void: mist, lava or a lake. It takes only voids, so make a passage a void first (key 1); along lava and lakes the walls sink to a low parapet, so they show");
	}
	return LOCTEXT("BiomeTip", "Paints the cells whose level allows this biome");
}

FLinearColor UHexPlanetPainter::GetBiomeColor(EHexPlanetLayer Layer, int32 InBiome) const
{
	return InBiome >= 0 && InBiome < GetNumBiomes(Layer) ? FLinearColor(Layers[int32(Layer)].Planet->GetBiomes()[InBiome]->FileColor) : FLinearColor::Gray;
}

void UHexPlanetPainter::ClearTool()
{
	if (bPainting)
	{
		EndStroke();
	}
	Tool = EHexPaintTool::None;
	if (Layers.IsValidIndex(int32(Target)))
	{
		TabTools[int32(GetLayerState().Tab)] = Tool;
	}
}

int32 UHexPlanetPainter::GetNumObjectSets(EHexPlanetLayer Layer) const
{
	return HasLayer(Layer) ? Layers[int32(Layer)].PaintableSets.Num() : 0;
}

int32 UHexPlanetPainter::GetPaintableSet(EHexPlanetLayer Layer, int32 Set) const
{
	return HasLayer(Layer) && Layers[int32(Layer)].PaintableSets.IsValidIndex(Set) ? Layers[int32(Layer)].PaintableSets[Set] : INDEX_NONE;
}

FText UHexPlanetPainter::GetObjectSetName(EHexPlanetLayer Layer, int32 Set) const
{
	const int32 Index = GetPaintableSet(Layer, Set);
	return Index != INDEX_NONE ? FText::FromString(Layers[int32(Layer)].LayerSets[Index]->DisplayName) : FText::GetEmpty();
}

FLinearColor UHexPlanetPainter::GetObjectSetColor(EHexPlanetLayer Layer, int32 Set) const
{
	const int32 Index = GetPaintableSet(Layer, Set);
	return Index != INDEX_NONE ? Layers[int32(Layer)].LayerSets[Index]->PaletteColor : FLinearColor::Gray;
}

bool UHexPlanetPainter::IsObjectSetCover(EHexPlanetLayer Layer, int32 Set) const
{
	const int32 Index = GetPaintableSet(Layer, Set);
	return Index != INDEX_NONE && Layers[int32(Layer)].LayerSets[Index]->Layer == EHexObjectLayer::Cover;
}

bool UHexPlanetPainter::IsObjectSetForeign(EHexPlanetLayer Layer, int32 Set) const
{
	const int32 Index = GetPaintableSet(Layer, Set);
	return Index != INDEX_NONE && Index >= Layers[int32(Layer)].OwnSets;
}

int32 UHexPlanetPainter::GetObjectSet() const
{
	return Layers.IsValidIndex(int32(Target)) ? GetLayerState().ObjectSet : 0;
}

void UHexPlanetPainter::SetObjectSet(int32 InSet)
{
	if (!Layers.IsValidIndex(int32(Target)))
	{
		return;
	}
	GetLayerState().ObjectSet = FMath::Clamp(InSet, 0, FMath::Max(GetLayerState().PaintableSets.Num() - 1, 0));
	// a kind of the other layer is placed by hand alone, and by hand a kind chosen is placed where a click falls
	if (IsObjectSetForeign(Target, GetLayerState().ObjectSet))
	{
		bPlacingByHand = true;
	}
	if (bPlacingByHand)
	{
		PlacedTool = EHexPlacedTool::Place;
	}
	SetTool(bPlacingByHand ? EHexPaintTool::Placed : EHexPaintTool::Object, GetObjectsTab());
}

void UHexPlanetPainter::SetPlacingByHand(bool bByHand)
{
	bPlacingByHand = bByHand;
	// on cells a kind of the other layer gives way to the first of the layer, as only a hand places it here
	if (!bByHand && Layers.IsValidIndex(int32(Target)) && IsObjectSetForeign(Target, GetLayerState().ObjectSet))
	{
		GetLayerState().ObjectSet = 0;
	}
	// the kind chosen goes on with the other, and so does the eraser
	if (Tool == EHexPaintTool::Object || (Tool == EHexPaintTool::Placed && PlacedTool != EHexPlacedTool::Erase))
	{
		if (bByHand)
		{
			PlacedTool = EHexPlacedTool::Place;
		}
		SetTool(bByHand ? EHexPaintTool::Placed : EHexPaintTool::Object, GetObjectsTab());
	}
	else if (IsErasing())
	{
		SetEraser();
	}
}

void UHexPlanetPainter::SetPlacedTool(EHexPlacedTool InTool)
{
	PlacedTool = InTool;
	bPlacingByHand = true;
	SetTool(EHexPaintTool::Placed, GetObjectsTab());
}

void UHexPlanetPainter::SetEraser()
{
	// by hand it takes away the object nearest the cursor, else the cover, decor and what was placed by hand of the cells under the brush
	if (bPlacingByHand)
	{
		SetPlacedTool(EHexPlacedTool::Erase);
		return;
	}
	SetTool(EHexPaintTool::Eraser, GetObjectsTab());
}

void UHexPlanetPainter::ShowPlacedProblem(const FString& Problem)
{
	UE_LOG(LogHexHero, Log, TEXT("Painter: %s"), *Problem);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor(255, 190, 120), Problem);
	}
}

void UHexPlanetPainter::SetStrokeObjects(int32 Cell, const FHexCellObjects& Objects)
{
	UHexPlanetObjects* Planted = Planet->GetObjects();
	if (!Stroke.OldObjects.Contains(Cell))
	{
		Stroke.OldObjects.Add(Cell, Planted->GetCell(Cell));
	}
	Stroke.NewObjects.Add(Cell, Objects);
	TMap<int32, FHexCellObjects> Changed;
	Changed.Add(Cell, Objects);
	Planted->SetCells(Changed);
	Planet->MarkLevelsEdited(true);
}

void UHexPlanetPainter::DropHeld()
{
	UHexPlanetObjects* Planted = Planet ? Planet->GetObjects() : nullptr;
	if (!Planted)
	{
		HeldCell = INDEX_NONE;
		return;
	}
	Planted->SetDragPreview(INDEX_NONE, nullptr);
	if (HeldCell == INDEX_NONE)
	{
		return;
	}
	// where it was last shown, a place it may stand on
	FHexCellObjects To = Planted->GetCell(HeldCell);
	To.Placed.Add(HeldObject);
	SetStrokeObjects(HeldCell, To);
	UE_LOG(LogHexHero, Log, TEXT("Painter: put down %s on cell %d at %.3f, %.3f"), Planted->GetSets().IsValidIndex(HeldObject.Set - 1) ? *Planted->GetSets()[HeldObject.Set - 1]->DisplayName : TEXT("?"),
		HeldCell, HeldObject.Offset.X, HeldObject.Offset.Y);
	HeldCell = INDEX_NONE;
}

void UHexPlanetPainter::PaintPlaced(int32 Cell, bool bFirst)
{
	UHexPlanetObjects* Planted = Planet->GetObjects();
	if (!Planted)
	{
		return;
	}
	const FVector3d Point(CursorPoint);
	const double PickRadius = Settings->PaintObjectPickRadius * Settings->HexRadius;
	int32 FoundCell = INDEX_NONE;
	int32 FoundIndex = INDEX_NONE;
	FHexCellObjects Found;

	// the nearest object, placed by hand or of a cover or decor, whose cell is then unpacked into objects placed by hand
	const auto FindNear = [&]()
	{
		return Planted->FindObjectNear(Point, PickRadius, FoundCell, FoundIndex, Found);
	};

	// the Eraser of By hand, and Ctrl with Place, take away the one nearest the cursor; the Eraser each one it passes while the button is held
	if (PlacedTool == EHexPlacedTool::Erase || (PlacedTool == EHexPlacedTool::Place && IsBackwards()))
	{
		if ((bFirst || PlacedTool == EHexPlacedTool::Erase) && FindNear())
		{
			Found.Placed.RemoveAt(FoundIndex);
			SetStrokeObjects(FoundCell, Found);
		}
		return;
	}

	if (PlacedTool == EHexPlacedTool::Place)
	{
		if (!bFirst)
		{
			return;
		}
		FHexPlacedObject Object;
		FString Problem;
		if (!Planted->MakePlaced(Cell, GetPaintableSet(Target, GetObjectSet()), Point, Object, Problem))
		{
			ShowPlacedProblem(FString::Printf(TEXT("Not placed: %s"), *Problem));
			return;
		}
		FHexCellObjects New = Planted->GetCell(Cell);
		New.Placed.Add(Object);
		SetStrokeObjects(Cell, New);
		return;
	}

	if (PlacedTool == EHexPlacedTool::Move)
	{
		// taken out of its cell once, which places that chunk again, and shown apart while it is dragged, so no chunk is placed again every frame
		if (bFirst)
		{
			HeldCell = INDEX_NONE;
			if (!FindNear())
			{
				ShowPlacedProblem(TEXT("No object near the cursor"));
				return;
			}
			HeldObject = Found.Placed[FoundIndex];
			Found.Placed.RemoveAt(FoundIndex);
			SetStrokeObjects(FoundCell, Found);
			HeldCell = FoundCell;
			Planted->SetDragPreview(HeldCell, &HeldObject);
			UE_LOG(LogHexHero, Log, TEXT("Painter: took up an object of cell %d at %.3f, %.3f"), HeldCell, HeldObject.Offset.X, HeldObject.Offset.Y);
			return;
		}
		if (HeldCell == INDEX_NONE)
		{
			return;
		}
		// to the point under the cursor, onto the cell there, if it may stand there; else it stays where it was shown last
		FHexPlacedObject Moved = HeldObject;
		Moved.Offset = Planted->GetPlacedOffset(Cell, Point);
		FString Problem;
		if (Cell == HeldCell && Moved == HeldObject)
		{
			return;
		}
		if (!Planted->CanPlace(Cell, Moved, &Problem))
		{
			UE_LOG(LogHexHero, Verbose, TEXT("Painter: the object held does not go on cell %d: %s"), Cell, *Problem);
			return;
		}
		HeldObject = Moved;
		HeldCell = Cell;
		Planted->SetDragPreview(HeldCell, &HeldObject);
		return;
	}

	// Turn, Size and Version change the one nearest the click, once a stroke
	if (!bFirst)
	{
		return;
	}
	if (!FindNear())
	{
		ShowPlacedProblem(TEXT("No object near the cursor"));
		return;
	}
	FHexPlacedObject& Object = Found.Placed[FoundIndex];
	const bool bBack = IsBackwards();
	if (PlacedTool == EHexPlacedTool::Turn)
	{
		Object.Yaw = float(FMath::RoundToDouble(FMath::Fmod(double(Object.Yaw) + (bBack ? -1.0 : 1.0) * Settings->PaintObjectTurn + 360.0, 360.0) * 10.0) / 10.0);
	}
	else if (PlacedTool == EHexPlacedTool::Size)
	{
		const double Factor = bBack ? 1.0 / Settings->PaintObjectScale : Settings->PaintObjectScale;
		Object.Scale = float(FMath::RoundToDouble(FMath::Clamp(double(Object.Scale) * Factor, 0.05, 20.0) * 1000.0) / 1000.0);
		// a larger mesh may reach over a void or into a wall
		FString Problem;
		if (!Planted->CanPlace(FoundCell, Object, &Problem))
		{
			ShowPlacedProblem(FString::Printf(TEXT("Not scaled: %s"), *Problem));
			return;
		}
	}
	else if (PlacedTool == EHexPlacedTool::Version)
	{
		const int32 Count = Planted->GetMeshCount(FoundCell, int32(Object.Set) - 1);
		if (Count <= 1)
		{
			ShowPlacedProblem(TEXT("That kind has one mesh"));
			return;
		}
		Object.Mesh = uint8((int32(Object.Mesh) % Count + (bBack ? Count - 1 : 1)) % Count);
	}
	SetStrokeObjects(FoundCell, Found);
}

FLinearColor UHexPlanetPainter::GetNetworkColor(int32 InNetwork) const
{
	const UHexPlanetConfig* Config = HasLayer(EHexPlanetLayer::Surface) && Layers[0].Settings ? Layers[0].Settings.Get() : GetDefault<UHexPlanetConfig>();
	return Config->GetNetworkColor(InNetwork);
}

int32 UHexPlanetPainter::GetNetworkEntrances(int32 InNetwork) const
{
	int32 Count = 0;
	if (HasLayer(EHexPlanetLayer::Surface))
	{
		for (const FHexEntrance& Entrance : Layers[0].Planet->GetEntrances())
		{
			Count += Entrance.Network == InNetwork ? 1 : 0;
		}
	}
	return Count;
}

int32 UHexPlanetPainter::GetPassageWidth() const
{
	return HasLayer(EHexPlanetLayer::Surface) ? FMath::Max(Layers[0].Planet->GetManifest().Underground.PassageWidth, 2) : 2;
}

FText UHexPlanetPainter::GetLinksText() const
{
	const HexPlanetUnderground::FReport* Report = HasLayer(EHexPlanetLayer::Underground) ? Layers[int32(EHexPlanetLayer::Underground)].Planet->GetUndergroundReport() : nullptr;
	if (!Report)
	{
		return LOCTEXT("NoReport", "The underground has no data yet.");
	}

	TArray<FString> Lines;
	TArray<FString> Split;
	TArray<FString> Empty;
	for (int32 Each = 0; Each < HexPlanetFile::MaxNetworks; ++Each)
	{
		const HexPlanetUnderground::FNetworkReport& Part = Report->Networks[Each];
		if (Part.Entrances > 0 && Part.Components > 1)
		{
			Split.Add(FString::Printf(TEXT("%c in %d"), HexPlanetFile::GetNetworkLetter(Each), Part.Components));
		}
		else if (Part.Entrances > 0 && Part.Components == 0)
		{
			Empty.Add(FString::Chr(HexPlanetFile::GetNetworkLetter(Each)));
		}
	}
	Lines.Add(FString::Printf(TEXT("%d networks, %d passage cells"), Report->CountNetworks(), Report->PassageCells));
	if (Report->IsGood())
	{
		Lines.Add(TEXT("Every network is one piece of its own."));
	}
	if (!Split.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Split: %s"), *FString::Join(Split, TEXT(", "))));
	}
	if (!Empty.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("No exit on a passage: %s"), *FString::Join(Empty, TEXT(", "))));
	}
	if (!Report->Merged.IsEmpty())
	{
		TArray<FString> Pairs;
		for (const FIntPoint& Pair : Report->Merged)
		{
			Pairs.Add(FString::Printf(TEXT("%c+%c"), HexPlanetFile::GetNetworkLetter(Pair.X), HexPlanetFile::GetNetworkLetter(Pair.Y)));
		}
		Lines.Add(FString::Printf(TEXT("Joined: %s"), *FString::Join(Pairs, TEXT(", "))));
	}
	if (!Report->ExitsInVoid.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Exits in the void: %d"), Report->ExitsInVoid.Num()));
	}
	if (!Report->Necks.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("One cell wide: %d cells"), Report->Necks.Num()));
	}
	if (!Report->BadEntrances.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Entrances not used: %d"), Report->BadEntrances.Num()));
	}
	if (Report->OrphanCells > 0)
	{
		Lines.Add(FString::Printf(TEXT("No exit reaches %d cells"), Report->OrphanCells));
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

FText UHexPlanetPainter::GetLevelName(EHexPlanetLayer Layer, int32 InLevel)
{
	if (Layer == EHexPlanetLayer::Underground)
	{
		return HexPlanetUnderground::IsPassage(int8(InLevel)) ? LOCTEXT("Passage", "Passage") : LOCTEXT("Void", "Void, the abyss");
	}
	switch (InLevel)
	{
	case -2: return LOCTEXT("DeepSea", "Deep sea");
	case -1: return LOCTEXT("Shallows", "Shallows");
	case 0: return LOCTEXT("Plains", "Plains");
	case 1: return LOCTEXT("Hills", "Hills");
	default: return LOCTEXT("Mountains", "Mountains");
	}
}

int32 UHexPlanetPainter::GetPaintedLevel(int32 Cell, int32 InLevel) const
{
	return Target == EHexPlanetLayer::Surface && InLevel > 1 && Planet->GetGrid().GetCells()[Cell].IsPentagon() ? 1 : InLevel;
}

void UHexPlanetPainter::BeginStroke(int32 Cell)
{
	if (!Planet || !bActive || Tool == EHexPaintTool::None)
	{
		return;
	}

	bPainting = true;
	LastCell = INDEX_NONE;
	LastCorner = INDEX_NONE;
	CarveFrom = Cell;
	HeldCell = INDEX_NONE;
	Stroke = FHexPaintStep();
	ContinueStroke(Cell);
}

void UHexPlanetPainter::ContinueStroke(int32 Cell)
{
	if (!bPainting || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return;
	}
	// rivers follow the corners under the cursor, which change within a cell
	if (Tool == EHexPaintTool::River || Tool == EHexPaintTool::Bridge)
	{
		PaintRiver(Cell);
		return;
	}
	// an object placed by hand goes where the click falls, and one dragged follows the cursor within a cell too
	if (Tool == EHexPaintTool::Placed)
	{
		PaintPlaced(Cell, LastCell == INDEX_NONE);
		LastCell = Cell;
		return;
	}
	if (Cell == LastCell)
	{
		return;
	}
	// an entrance goes on the cell pressed alone, and a carved passage runs from it to the cell under the cursor
	if (Tool == EHexPaintTool::Entrance || Tool == EHexPaintTool::EntranceRemove)
	{
		if (LastCell == INDEX_NONE)
		{
			PaintEntrance(Cell);
		}
		LastCell = Cell;
		return;
	}
	if (Tool == EHexPaintTool::EntranceTurn)
	{
		if (LastCell == INDEX_NONE)
		{
			PaintEntranceTurn(Cell);
		}
		LastCell = Cell;
		return;
	}
	if (Tool == EHexPaintTool::Carve)
	{
		PaintCarve(Cell);
		LastCell = Cell;
		return;
	}
	// a hero goes on the cell pressed alone, and so do a mine, a pile, a castle and a neutral stack
	if (Tool == EHexPaintTool::Hero || Tool == EHexPaintTool::Site || Tool == EHexPaintTool::Castle || Tool == EHexPaintTool::Neutral)
	{
		if (LastCell == INDEX_NONE)
		{
			Tool == EHexPaintTool::Hero ? PaintHero(Cell) : Tool == EHexPaintTool::Site ? PaintSite(Cell) : Tool == EHexPaintTool::Neutral ? PaintNeutral(Cell) : PaintCastle(Cell);
		}
		LastCell = Cell;
		return;
	}

	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const int32 NumBefore = Stroke.NewLevels.Num();

	// a fast mouse skips cells between two frames, so the brush walks the arc between them in steps of half a cell
	if (LastCell != INDEX_NONE)
	{
		const FVector3d From = Cells[LastCell].Center;
		const FVector3d To = Cells[Cell].Center;
		const double CellAngle = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Cells[Cell].Center, Cells[Cells[Cell].Neighbors[0]].Center), -1.0, 1.0));
		const double Angle = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(From, To), -1.0, 1.0));
		const int32 Steps = FMath::Clamp(FMath::CeilToInt32(Angle / FMath::Max(CellAngle * 0.5, 1.0e-6)), 1, 1000);
		int32 Walked = LastCell;

		for (int32 Step = 1; Step < Steps; ++Step)
		{
			Walked = Planet->GetGrid().FindCell(FMath::Lerp(From, To, double(Step) / Steps).GetSafeNormal(), Walked);
			PaintAround(Walked);
		}
	}

	PaintAround(Cell);
	LastCell = Cell;

	if (Stroke.NewLevels.Num() != NumBefore)
	{
		UpdatePreview(int32(Target));
	}
}

void UHexPlanetPainter::PaintAround(int32 Cell)
{
	if (Cell == INDEX_NONE)
	{
		return;
	}

	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();

	// the rings of the brush, found through the neighbors
	TArray<int32, TInlineAllocator<64>> Brush{Cell};
	int32 RingStart = 0;

	for (int32 Ring = 1; Ring < BrushSize; ++Ring)
	{
		const int32 RingEnd = Brush.Num();
		for (int32 Index = RingStart; Index < RingEnd; ++Index)
		{
			for (const int32 Neighbor : Cells[Brush[Index]].Neighbors)
			{
				Brush.AddUnique(Neighbor);
			}
		}
		RingStart = RingEnd;
	}

	// objects go to the planet at once, once per cell and stroke, a kind only to the cells whose level allows it, the kinds of the layer painted
	if (Tool == EHexPaintTool::Object || Tool == EHexPaintTool::Eraser)
	{
		UHexPlanetObjects* Objects = Planet->GetObjects();
		const int32 SetIndex = GetPaintableSet(Target, GetObjectSet());
		const bool bCover = IsObjectSetCover(Target, GetObjectSet());
		TMap<int32, FHexCellObjects> Changed;
		for (const int32 Painted : Brush)
		{
			// nothing is drawn on the cell of an entrance, so the stroke holds no change there that cannot be seen
			if (Stroke.NewObjects.Contains(Painted) || Objects->IsClear(Painted))
			{
				continue;
			}
			const FHexCellObjects Old = Objects->GetCell(Painted);
			FHexCellObjects New = Old;
			if (Tool == EHexPaintTool::Eraser)
			{
				New = FHexCellObjects();
			}
			// a kind of the layer alone fills cells, as a hand alone places those of the other layer
			else if (SetIndex != INDEX_NONE && SetIndex < GetLayerState().OwnSets && Objects->IsAllowed(SetIndex, Planet->GetCellLevel(Painted)))
			{
				if (bCover)
				{
					New.Cover = uint8(SetIndex + 1);
					New.Density = uint8(ObjectDensity);
				}
				else
				{
					New.Decor = uint8(SetIndex + 1);
				}
			}
			if (!(New == Old))
			{
				Stroke.OldObjects.Add(Painted, Old);
				Stroke.NewObjects.Add(Painted, New);
				Changed.Add(Painted, New);
			}
		}

		if (!Changed.IsEmpty())
		{
			Objects->SetCells(Changed);
			Planet->MarkLevelsEdited(true);
		}
		return;
	}

	if (IsSettingWalls())
	{
		PaintWalls(Brush);
		return;
	}

	// relief goes to the planet at once, once per cell and stroke, only to cells with a piece the tool can change
	if (Tool == EHexPaintTool::Relief)
	{
		HexPlanetSurface::FReliefOverrides Changed;
		for (const int32 Painted : Brush)
		{
			HexPlanetSurface::FReliefOverride NewRelief;
			if (Stroke.NewRelief.Contains(Painted) || !GetToolRelief(Painted, NewRelief))
			{
				continue;
			}
			Stroke.OldRelief.Add(Painted, Planet->GetReliefOverride(Painted));
			Stroke.NewRelief.Add(Painted, NewRelief);
			Changed.Add(Painted, NewRelief);
		}

		if (!Changed.IsEmpty())
		{
			Planet->SetReliefOverrides(Changed, true);
		}
		return;
	}

	// biomes go to the planet at once, only to the cells whose level allows them; a void painted another style, as a lake, gives its walls the
	// height of that style, in the same build of the walls
	if (Tool == EHexPaintTool::Biome)
	{
		const int32 Biome = GetBiome();
		TMap<int32, int32> Changed;
		TMap<int32, float> Walls;
		for (const int32 Painted : Brush)
		{
			const int32 Current = Planet->GetCellBiome(Painted);
			const int32 CellLevel = Planet->GetCellLevel(Painted);
			if (Current != Biome && Planet->IsBiomeAllowed(Biome, CellLevel))
			{
				Stroke.OldBiomes.FindOrAdd(Painted, Current);
				Stroke.NewBiomes.Add(Painted, Biome);
				Changed.Add(Painted, Biome);
				ClearWallHeight(Stroke, Painted, CellLevel, Current, CellLevel, Biome, &Walls);
			}
		}

		if (!Changed.IsEmpty())
		{
			Planet->SetCells({}, Changed, Walls);
			Planet->MarkLevelsEdited(true);
		}
		return;
	}

	const int32 Level = GetLevel();
	for (const int32 Painted : Brush)
	{
		const int32 NewLevel = GetPaintedLevel(Painted, Level);
		const int32* Current = Stroke.NewLevels.Find(Painted);
		const int32 Shown = Current ? *Current : Planet->GetCellLevel(Painted);

		if (Shown == NewLevel)
		{
			continue;
		}

		Stroke.OldLevels.FindOrAdd(Painted, Planet->GetCellLevel(Painted));
		Stroke.NewLevels.Add(Painted, NewLevel);
	}
}

void UHexPlanetPainter::PaintHero(int32 Cell)
{
	UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
	if (!Heroes)
	{
		return;
	}
	// the heroes before, for Undo
	if (!Stroke.bHeroes)
	{
		Stroke.bHeroes = true;
		Stroke.OldHeroes = Heroes->MakeLines();
	}
	if (IsBackwards())
	{
		if (const FHexHero* Hero = Heroes->FindHeroAt(Target, Cell))
		{
			Heroes->RemoveHero(Hero->Id);
		}
	}
	else
	{
		FString Problem;
		if (Heroes->AddHero(Target, Cell, HeroClass, HeroPlayer, Problem) == 0 && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::Yellow, FString::Printf(TEXT("No hero stands there: %s"), *Problem));
		}
	}
	Stroke.NewHeroes = Heroes->MakeLines();
}

void UHexPlanetPainter::PaintSite(int32 Cell)
{
	UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
	UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	if (!Sites)
	{
		return;
	}
	// the sites before, for Undo
	if (!Stroke.bSites)
	{
		Stroke.bSites = true;
		Stroke.OldSites = Sites->MakeLines();
	}
	if (IsBackwards())
	{
		Sites->RemoveSiteAt(Target, Cell);
	}
	else
	{
		FString Problem;
		if (Sites->AddSite(Target, Cell, SiteKind, Problem) == 0 && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::Yellow, FString::Printf(TEXT("Nothing is built there: %s"), *Problem));
		}
	}
	Stroke.NewSites = Sites->MakeLines();
}

void UHexPlanetPainter::PaintNeutral(int32 Cell)
{
	UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
	UHexNeutrals* Neutrals = Heroes ? Heroes->GetNeutrals() : nullptr;
	if (!Neutrals)
	{
		return;
	}
	// the stacks before, for Undo
	if (!Stroke.bNeutrals)
	{
		Stroke.bNeutrals = true;
		Stroke.OldNeutrals = Neutrals->MakeLines();
	}
	if (IsBackwards())
	{
		Neutrals->RemoveAt(Target, Cell);
	}
	else
	{
		FString Problem;
		if (Neutrals->Add(Target, Cell, FHexStack{NeutralFaction, NeutralLevel, bNeutralUpgraded, NeutralCount}, Problem) == 0)
		{
			UE_LOG(LogHexHero, Log, TEXT("Painter: no neutral stack on cell %d of the %s: %s"), Cell, GetPlanetLayerName(Target), *Problem);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::Yellow, FString::Printf(TEXT("No stack stands there: %s"), *Problem));
			}
		}
	}
	Stroke.NewNeutrals = Neutrals->MakeLines();
}

bool UHexPlanetPainter::GenerateNeutrals(bool bClear)
{
	UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
	UHexNeutrals* Neutrals = Heroes ? Heroes->GetNeutrals() : nullptr;
	if (bPainting || !Neutrals || Heroes->IsPlaying())
	{
		return false;
	}
	FHexPaintStep Step;
	Step.bNeutrals = true;
	Step.OldNeutrals = Neutrals->MakeLines();
	FString Report = TEXT("all taken away");
	if (bClear)
	{
		Neutrals->Clear();
	}
	else
	{
		Neutrals->Generate(Report);
	}
	Step.NewNeutrals = Neutrals->MakeLines();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 6.0f, FColor::White, FString::Printf(TEXT("Neutrals: %s"), *Report));
	}
	if (Step.IsEmpty())
	{
		return false;
	}
	AddStep(MoveTemp(Step));
	UpdateSavedState();
	return true;
}

void UHexPlanetPainter::PaintCastle(int32 Cell)
{
	UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
	UHexCastles* Castles = Heroes ? Heroes->GetCastles() : nullptr;
	if (!Castles)
	{
		return;
	}
	// the castles before, for Undo
	if (!Stroke.bCastles)
	{
		Stroke.bCastles = true;
		Stroke.OldCastles = Castles->MakeLines();
	}
	if (IsBackwards())
	{
		Castles->RemoveCastleAt(Target, Cell);
	}
	else
	{
		// the cell clicked and the two around its corner nearest the cursor
		int32 Cells[3];
		FString Problem = TEXT("there are no three cells around a corner there");
		if (!Castles->FindCells(Target, Cell, FVector3d(CursorPoint), Cells) || Castles->AddCastle(Target, Cells, CastleFaction, CastleOwner, Problem) == 0)
		{
			UE_LOG(LogHexHero, Log, TEXT("Painter: no castle on cell %d of the %s: %s"), Cell, GetPlanetLayerName(Target), *Problem);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::Yellow, FString::Printf(TEXT("No castle stands there: %s"), *Problem));
			}
		}
	}
	Stroke.NewCastles = Castles->MakeLines();
}

bool UHexPlanetPainter::GenerateSites(bool bClear)
{
	UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
	UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	if (bPainting || !Sites || Heroes->IsPlaying())
	{
		return false;
	}
	FHexPaintStep Step;
	Step.bSites = true;
	Step.OldSites = Sites->MakeLines();
	FString Report;
	if (bClear)
	{
		Sites->Clear();
		Report = TEXT("every mine, pile and mill is taken off the planet");
	}
	else
	{
		Sites->Generate(Report);
	}
	Step.NewSites = Sites->MakeLines();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 6.0f, FColor::White, FString::Printf(TEXT("Sites: %s"), *Report));
	}
	if (Step.IsEmpty())
	{
		return false;
	}
	AddStep(MoveTemp(Step));
	UpdateSavedState();
	return true;
}

void UHexPlanetPainter::PaintEntrance(int32 Cell)
{
	if (Target != EHexPlanetLayer::Surface)
	{
		return;
	}

	// An entrance is put on a mountain, as the developer decided on 2026-11-12 ("переходи тепер окремо не ставимо - тільки на горі - якщо я
	// пробую поставити перехід на пустоту - це автоматично добудує стіну чи гору під неї"): the cell clicked is the mountain its mouth is
	// built into, raised by this step where it is plains or hills, and the entrance stands on the cell of plains or hills beside it that is
	// nearest the cursor, its mouth opening that way
	const TArray<FHexSphereCell>& GridCells = Planet->GetGrid().GetCells();
	const bool bRemove = IsBackwards() || Tool == EHexPaintTool::EntranceRemove;
	int32 Side = INDEX_NONE;
	int32 Mountain = INDEX_NONE;
	if (bRemove)
	{
		// an entrance is taken away by its cell, or by the mountain its mouth is built into
		Cell = FindEntranceCellAt(Cell);
	}
	if (GridCells.IsValidIndex(Cell) && !bRemove)
	{
		int32 Before = INDEX_NONE;
		FindEntrancePlace(Cell, Mountain, Before);
		const int32 Level = Planet->GetCellLevel(Cell);
		Mountain = Cell;
		if (Before == INDEX_NONE)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::Yellow, TEXT("An entrance is built into a mountain with plains or hills beside it: click such a mountain, or land that may become one"));
			}
			return;
		}
		Cell = Before;
		const int32 Back = GridCells[Cell].Neighbors.Find(Mountain);
		Side = (Back + GridCells[Cell].Neighbors.Num() / 2) % GridCells[Cell].Neighbors.Num();
		// the mountain, with the stroke, so Undo takes it back
		if (Level < 2)
		{
			if (!Stroke.OldLevels.Contains(Mountain))
			{
				Stroke.OldLevels.Add(Mountain, Level);
			}
			Stroke.NewLevels.Add(Mountain, 2);
		}
	}

	// a copy, as the planet keeps the list it returns and changes it
	const TArray<FHexEntrance> Old = Planet->GetEntrances();
	TArray<FHexEntrance> New = Old;
	const int32 Index = New.IndexOfByPredicate([Cell](const FHexEntrance& Each) { return Each.Cell == Cell; });
	if (bRemove)
	{
		if (Index == INDEX_NONE)
		{
			return;
		}
		New.RemoveAt(Index);
	}
	else
	{
		// on plains or hills and never on a pentagon; its exit is the same cell below, which nothing carves, so it may be in the void, which the
		// Links tab underground shows
		if (!HexPlanetUnderground::CanHoldEntrance(Planet->GetGrid(), Planet->GetLevels(), Cell))
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::Yellow, TEXT("An entrance stands only on plains or hills, and never on a pentagon"));
			}
			return;
		}
		if (Index != INDEX_NONE)
		{
			if (New[Index].Network == Network && New[Index].Side == Side && !New[Index].bCorner)
			{
				return;
			}
			New[Index].Network = Network;
			New[Index].Side = Side;
			New[Index].bCorner = false;
			New[Index].ExitSide = Side;
			New[Index].bExitCorner = false;
		}
		else
		{
			// its exit opens the same way, so its wall lies under the mountain
			FHexEntrance Added{Cell, Network};
			Added.Side = Side;
			Added.ExitSide = Side;
			New.Add(Added);
			New.Sort([](const FHexEntrance& A, const FHexEntrance& B) { return A.Cell < B.Cell; });
		}
	}

	if (!Stroke.bEntrances)
	{
		Stroke.bEntrances = true;
		Stroke.OldEntrances = Old;
	}
	Stroke.NewEntrances = New;
	Planet->SetEntrances(New, true);
	// and the wall its exit is built into: the cell under the mountain becomes void, in the same step, which keeps what it changed
	// underground apart from the cells of its own layer, so Undo takes it back with the entrance. The mountain itself is of the stroke:
	// raised here too, it left the tile of the stroke standing as a grey hex, as nothing was built again to take it away
	if (!bRemove)
	{
		TMap<int32, int32> OldCaveLevels;
		TMap<int32, int32> OldCaveBiomes;
		Planet->FitEntrances(Cell, false, &OldCaveLevels, &OldCaveBiomes);
		if (const AHexPlanet* Underground = OldCaveLevels.IsEmpty() ? nullptr : Planet->GetLayerPlanet(EHexPlanetLayer::Underground))
		{
			for (const TPair<int32, int32>& Each : OldCaveLevels)
			{
				if (!Stroke.OldCaveLevels.Contains(Each.Key))
				{
					Stroke.OldCaveLevels.Add(Each.Key, Each.Value);
					Stroke.OldCaveBiomes.Add(Each.Key, OldCaveBiomes.FindRef(Each.Key));
				}
				Stroke.NewCaveLevels.Add(Each.Key, Underground->GetCellLevel(Each.Key));
				Stroke.NewCaveBiomes.Add(Each.Key, Underground->GetCellBiome(Each.Key));
			}
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Painter: %s the entrance of network %c on cell %d, %d entrances"), bRemove ? TEXT("took away") : TEXT("put"),
		HexPlanetFile::GetNetworkLetter(bRemove ? Old[Index].Network : Network), Cell, New.Num());
}

int32 UHexPlanetPainter::FindEntranceCellAt(int32 Cell) const
{
	if (Planet->FindEntrance(Cell) != INDEX_NONE)
	{
		return Cell;
	}
	// the entrance whose mouth is built into the cell, of several the one nearest the cursor
	int32 Found = INDEX_NONE;
	double Nearest = TNumericLimits<double>::Max();
	const FVector3d Cursor = FVector3d(CursorPoint).GetSafeNormal();
	for (const FHexEntrance& Entrance : Planet->GetEntrances())
	{
		const FHexEntranceSeat Seat = Planet->GetEntranceSeat(Entrance.Cell);
		const double Distance = FVector3d::DistSquared(Seat.Anchor, Cursor);
		if (Seat.bEmbedded && (Seat.Behind == Cell || Seat.Behind2 == Cell) && Distance < Nearest)
		{
			Nearest = Distance;
			Found = Entrance.Cell;
		}
	}
	const bool bNoneBuiltIn = Found == INDEX_NONE;
	// else, on a mountain or a void, which holds no entrance itself, the entrance beside it nearest the cursor: one turned away from the cell
	// it was put by, or one that cannot be used, is still taken by that cell
	const TArray<FHexSphereCell>& GridCells = Planet->GetGrid().GetCells();
	const bool bRock = GridCells.IsValidIndex(Cell)
		&& (Target == EHexPlanetLayer::Surface ? Planet->GetCellLevel(Cell) >= 2 : !HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Cell))));
	const bool bBeside = bNoneBuiltIn && bRock;
	for (int32 Each = 0; bBeside && Each < GridCells[Cell].Neighbors.Num(); ++Each)
	{
		const int32 Neighbor = GridCells[Cell].Neighbors[Each];
		const double Distance = FVector3d::DistSquared(GridCells[Neighbor].Center, Cursor);
		if (Planet->FindEntrance(Neighbor) != INDEX_NONE && Distance < Nearest)
		{
			Nearest = Distance;
			Found = Neighbor;
		}
	}
	return Found != INDEX_NONE ? Found : Cell;
}

bool UHexPlanetPainter::FindEntrancePlace(int32 Clicked, int32& OutMountain, int32& OutBefore) const
{
	OutMountain = OutBefore = INDEX_NONE;
	const TArray<FHexSphereCell>& GridCells = Planet->GetGrid().GetCells();
	if (Target != EHexPlanetLayer::Surface || !GridCells.IsValidIndex(Clicked) || Planet->GetCellLevel(Clicked) < 0 || GridCells[Clicked].IsPentagon())
	{
		return false;
	}
	const FVector3d Cursor = FVector3d(CursorPoint).GetSafeNormal();
	double BestDot = -2.0;
	for (const int32 Neighbor : GridCells[Clicked].Neighbors)
	{
		const double Dot = FVector3d::DotProduct((GridCells[Neighbor].Center - GridCells[Clicked].Center).GetSafeNormal(), (Cursor - GridCells[Clicked].Center).GetSafeNormal());
		if (HexPlanetUnderground::CanHoldEntrance(Planet->GetGrid(), Planet->GetLevels(), Neighbor) && Dot > BestDot)
		{
			BestDot = Dot;
			OutBefore = Neighbor;
		}
	}
	OutMountain = Clicked;
	return OutBefore != INDEX_NONE;
}

void UHexPlanetPainter::UpdateEntranceHint(int32 Cell)
{
	int32 Mountain = INDEX_NONE;
	int32 Before = INDEX_NONE;
	if (Tool == EHexPaintTool::Entrance && !bPainting && !IsBackwards())
	{
		FindEntrancePlace(Cell, Mountain, Before);
	}
	if (Mountain != HintMountain || Before != HintBefore)
	{
		HintMountain = Mountain;
		HintBefore = Before;
		UpdatePreview(int32(Target));
	}
}

void UHexPlanetPainter::PaintEntranceTurn(int32 Cell)
{
	// by its cell, or by the mountain or the void its mouth is built into
	Cell = FindEntranceCellAt(Cell);
	const TArray<FHexEntrance> Old = Planet->GetEntrances();
	TArray<FHexEntrance> New = Old;
	const int32 Index = New.IndexOfByPredicate([Cell](const FHexEntrance& Each) { return Each.Cell == Cell; });
	if (Index == INDEX_NONE || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return;
	}
	// from the way it opens now, whoever chose that, to the next around the cell: its sides and the corners between them in turn
	const int32 Num = Planet->GetGrid().GetCells()[Cell].Neighbors.Num() * 2;
	const int32 Turn = (Planet->GetEntranceTurn(Cell) + (IsBackwards() ? Num - 1 : 1)) % Num;
	const int32 Side = Turn / 2;
	(Target == EHexPlanetLayer::Surface ? New[Index].Side : New[Index].ExitSide) = Side;
	(Target == EHexPlanetLayer::Surface ? New[Index].bCorner : New[Index].bExitCorner) = Turn % 2 == 1;
	if (!Stroke.bEntrances)
	{
		Stroke.bEntrances = true;
		Stroke.OldEntrances = Old;
	}
	Stroke.NewEntrances = New;
	Planet->SetEntrances(New, true);
	UE_LOG(LogHexHero, Log, TEXT("Painter: turned the %s on cell %d to %s %d"), Target == EHexPlanetLayer::Surface ? TEXT("entrance") : TEXT("exit"), Cell,
		Turn % 2 == 1 ? TEXT("the corner after side") : TEXT("side"), Side);
}

void UHexPlanetPainter::PaintCarve(int32 Cell)
{
	if (Target != EHexPlanetLayer::Underground || !Planet->GetGrid().GetCells().IsValidIndex(CarveFrom))
	{
		return;
	}

	// the whole way again from where the stroke began, so the passage follows the cursor and drops what it no longer needs
	Stroke.OldLevels.Reset();
	Stroke.NewLevels.Reset();
	const TArray<int32> Path = HexPlanetUnderground::FindPath(Planet->GetGrid(), CarveFrom, Cell);
	for (const int32 Carved : HexPlanetUnderground::WidenPath(Planet->GetGrid(), Planet->GetLevels(), Path, GetPassageWidth()))
	{
		Stroke.OldLevels.Add(Carved, Planet->GetCellLevel(Carved));
		Stroke.NewLevels.Add(Carved, HexPlanetUnderground::PassageLevel);
	}
	UpdatePreview(int32(Target));
}

TArray<uint64> UHexPlanetPainter::FindCornerPath(int32 From, int32 To)
{
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	if (CornerCells.IsEmpty())
	{
		CornerCells.SetNum(Grid.GetCorners().Num());
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			for (const int32 Corner : Cells[Cell].Corners)
			{
				CornerCells[Corner].Add(Cell);
			}
		}
	}

	// a search outwards from From, which a fast mouse keeps short, as it runs every frame
	TMap<int32, TPair<int32, uint64>> Came;
	TArray<int32> Queue{From};
	Came.Add(From, {INDEX_NONE, 0});
	for (int32 Index = 0; Index < Queue.Num() && !Came.Contains(To) && Queue.Num() < 4000; ++Index)
	{
		const int32 Corner = Queue[Index];
		const TArray<int32, TInlineAllocator<3>>& Around = CornerCells[Corner];
		for (int32 First = 0; First < Around.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < Around.Num(); ++Second)
			{
				const FHexSphereCell& Cell = Cells[Around[First]];
				const int32 Side = Cell.Neighbors.IndexOfByKey(Around[Second]);
				if (Side == INDEX_NONE)
				{
					continue;
				}
				const int32 NumSides = Cell.Corners.Num();
				const int32 SideFrom = Cell.Corners[(Side + NumSides - 1) % NumSides];
				const int32 Next = SideFrom == Corner ? Cell.Corners[Side] : SideFrom;
				if (!Came.Contains(Next))
				{
					Came.Add(Next, {Corner, HexPlanetRivers::GetEdgeKey(Around[First], Around[Second])});
					Queue.Add(Next);
				}
			}
		}
	}

	TArray<uint64> Path;
	if (!Came.Contains(To))
	{
		return Path;
	}
	for (int32 Corner = To; Corner != From; Corner = Came[Corner].Key)
	{
		Path.Add(Came[Corner].Value);
	}
	return Path;
}

void UHexPlanetPainter::PaintRiver(int32 Cell)
{
	UHexPlanetRivers* Rivers = Planet->GetRivers();
	if (!Rivers || Target != EHexPlanetLayer::Surface)
	{
		return;
	}
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const FVector3d Direction = FVector3d(CursorPoint).GetSafeNormal();
	const bool bRemove = IsBackwards();
	TMap<uint64, uint8> Changed;

	const auto Change = [&](uint64 Key, uint8 New)
	{
		const uint8 Old = Rivers->GetEdge(Key);
		if (Old != New)
		{
			Stroke.OldRivers.FindOrAdd(Key, Old);
			Stroke.NewRivers.Add(Key, New);
			Changed.Add(Key, New);
		}
	};

	if (Tool == EHexPaintTool::Bridge)
	{
		// the side whose middle lies nearest the cursor, once per stroke
		int32 Best = 0;
		double BestDistance = TNumericLimits<double>::Max();
		const int32 NumSides = Each.Corners.Num();
		for (int32 Side = 0; Side < NumSides; ++Side)
		{
			const FVector3d Middle = (Grid.GetCorners()[Each.Corners[(Side + NumSides - 1) % NumSides]] + Grid.GetCorners()[Each.Corners[Side]]).GetSafeNormal();
			const double Distance = FVector3d::DistSquared(Middle, Direction);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Side;
			}
		}
		const uint64 Key = HexPlanetRivers::GetEdgeKey(Cell, Each.Neighbors[Best]);
		if (!Stroke.NewRivers.Contains(Key) && Rivers->GetEdge(Key) > 0)
		{
			Change(Key, bRemove ? 1 : 2);
		}
	}
	else
	{
		int32 Nearest = INDEX_NONE;
		double NearestDistance = TNumericLimits<double>::Max();
		for (const int32 Corner : Each.Corners)
		{
			const double Distance = FVector3d::DistSquared(Grid.GetCorners()[Corner], Direction);
			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
				Nearest = Corner;
			}
		}
		if (LastCorner != INDEX_NONE && Nearest != LastCorner)
		{
			for (const uint64 Key : FindCornerPath(LastCorner, Nearest))
			{
				int32 A, B;
				HexPlanetRivers::GetEdgeCells(Key, A, B);
				// a bridge stays on a river that is laid again
				if (bRemove)
				{
					Change(Key, 0);
				}
				else if (Rivers->IsEdgeAllowed(A, B))
				{
					Change(Key, FMath::Max<uint8>(Rivers->GetEdge(Key), 1));
				}
			}
		}
		LastCorner = Nearest;
	}

	if (!Changed.IsEmpty())
	{
		Rivers->SetEdges(Changed, true);
	}
}

void UHexPlanetPainter::EndStroke()
{
	if (!bPainting)
	{
		return;
	}

	// the object Move drags goes down where it was shown last, as part of the stroke
	DropHeld();
	bPainting = false;
	LastCell = INDEX_NONE;
	CarveFrom = INDEX_NONE;

	// a cell painted back to its level or biome is no change
	for (auto It = Stroke.NewLevels.CreateIterator(); It; ++It)
	{
		if (Stroke.OldLevels[It.Key()] == It.Value())
		{
			Stroke.OldLevels.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
	for (auto It = Stroke.NewBiomes.CreateIterator(); It; ++It)
	{
		if (Stroke.OldBiomes[It.Key()] == It.Value())
		{
			Stroke.OldBiomes.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	for (auto It = Stroke.NewRivers.CreateIterator(); It; ++It)
	{
		if (Stroke.OldRivers[It.Key()] == It.Value())
		{
			Stroke.OldRivers.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
	// and an object dragged back to where it stood
	for (auto It = Stroke.NewObjects.CreateIterator(); It; ++It)
	{
		if (Stroke.OldObjects[It.Key()] == It.Value())
		{
			Stroke.OldObjects.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	if (Stroke.IsEmpty())
	{
		Stroke = FHexPaintStep();
		UpdatePreview(int32(Target));
		return;
	}

	// a stroke of levels goes to the planet now, which may change the biomes of its cells too, and those go into the step, so Undo puts them back
	if (!Stroke.NewLevels.IsEmpty())
	{
		for (const TPair<int32, int32>& Each : Stroke.NewLevels)
		{
			Stroke.OldBiomes.Add(Each.Key, Planet->GetCellBiome(Each.Key));
		}

		Apply(Stroke.NewLevels, {});

		for (const TPair<int32, int32>& Each : Stroke.NewLevels)
		{
			Stroke.NewBiomes.Add(Each.Key, Planet->GetCellBiome(Each.Key));
		}

		// a passage made void, or a void made passage, has walls along it of the height of its style again, which the rebuild of the levels
		// builds
		TMap<int32, float> Walls;
		for (const TPair<int32, int32>& Each : Stroke.NewLevels)
		{
			ClearWallHeight(Stroke, Each.Key, Stroke.OldLevels[Each.Key], Stroke.OldBiomes[Each.Key], Planet->GetCellLevel(Each.Key), Stroke.NewBiomes[Each.Key], &Walls);
		}
		Planet->SetWallHeights(Walls, true);
	}
	AddStep(MoveTemp(Stroke));
	Stroke = FHexPaintStep();
}

void UHexPlanetPainter::AddStep(FHexPaintStep&& Step)
{
	FHexPainterLayer& State = GetLayerState();
	Step.Id = NextStepId++;
	State.UndoSteps.Add(MoveTemp(Step));
	State.RedoSteps.Reset();

	// the oldest steps drop out, still applied, so an Undo emptied down to them stands at the last of them
	const int32 MaxSteps = FMath::Max(Settings ? Settings->PaintUndoSteps : 100, 1);
	if (State.UndoSteps.Num() > MaxSteps)
	{
		const int32 Dropped = State.UndoSteps.Num() - MaxSteps;
		State.BaseId = State.UndoSteps[Dropped - 1].Id;
		State.UndoSteps.RemoveAt(0, Dropped);
	}
}

void UHexPlanetPainter::OnSaved()
{
	for (FHexPainterLayer& State : Layers)
	{
		State.SavedTop = GetTop(State);
	}
}

void UHexPlanetPainter::UpdateSavedState()
{
	bool bAtSaved = true;
	for (const FHexPainterLayer& State : Layers)
	{
		bAtSaved &= GetTop(State) == State.SavedTop && !(State.Planet && State.Planet->HasEditedOutsideSteps());
	}
	if (bAtSaved)
	{
		for (const FHexPainterLayer& State : Layers)
		{
			if (State.Planet)
			{
				State.Planet->ClearEditedBySteps();
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("Painter: back at the state of the last save"));
	}
	else if (Planet && !Planet->HasUnsavedLevels())
	{
		// a step whose change Apply did not mark, so the planet still tells it differs from the files
		Planet->MarkLevelsEdited(true);
	}
}

bool UHexPlanetPainter::CanUndo() const
{
	return Layers.IsValidIndex(int32(Target)) && !GetLayerState().UndoSteps.IsEmpty();
}

bool UHexPlanetPainter::CanRedo() const
{
	return Layers.IsValidIndex(int32(Target)) && !GetLayerState().RedoSteps.IsEmpty();
}

bool UHexPlanetPainter::IsPlayerAi(int32 Player) const
{
	const UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
	return Heroes && Heroes->IsPlayerAi(Player);
}

void UHexPlanetPainter::TogglePlayerAi(int32 Player)
{
	if (UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->SetPlayerAi(Player, !Heroes->IsPlayerAi(Player));
	}
}

void UHexPlanetPainter::Undo()
{
	if (bPainting || !CanUndo())
	{
		return;
	}

	FHexPaintStep Step = GetLayerState().UndoSteps.Pop();
	Apply(Step.OldLevels, Step.OldBiomes, Step.OldRelief, Step.OldObjects, Step.OldRivers, Step.bEntrances ? &Step.OldEntrances : nullptr,
		Step.bSeedOffset ? &Step.OldSeedOffset : nullptr, Step.OldWalls);
	ApplyCave(Step.OldCaveLevels, Step.OldCaveBiomes);
	if (UHexHeroes* Heroes = Step.bHeroes && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->SetLines(Step.OldHeroes);
	}
	if (UHexHeroes* Heroes = Step.bSites && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->GetSites()->SetLines(Step.OldSites);
	}
	if (UHexHeroes* Heroes = Step.bCastles && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->GetCastles()->SetLines(Step.OldCastles);
	}
	if (UHexHeroes* Heroes = Step.bNeutrals && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->GetNeutrals()->SetLines(Step.OldNeutrals);
	}
	GetLayerState().RedoSteps.Add(MoveTemp(Step));
	UpdateSavedState();
}

void UHexPlanetPainter::Redo()
{
	if (bPainting || !CanRedo())
	{
		return;
	}

	FHexPaintStep Step = GetLayerState().RedoSteps.Pop();
	Apply(Step.NewLevels, Step.NewBiomes, Step.NewRelief, Step.NewObjects, Step.NewRivers, Step.bEntrances ? &Step.NewEntrances : nullptr,
		Step.bSeedOffset ? &Step.NewSeedOffset : nullptr, Step.NewWalls);
	ApplyCave(Step.NewCaveLevels, Step.NewCaveBiomes);
	if (UHexHeroes* Heroes = Step.bHeroes && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->SetLines(Step.NewHeroes);
	}
	if (UHexHeroes* Heroes = Step.bSites && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->GetSites()->SetLines(Step.NewSites);
	}
	if (UHexHeroes* Heroes = Step.bCastles && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->GetCastles()->SetLines(Step.NewCastles);
	}
	if (UHexHeroes* Heroes = Step.bNeutrals && Planet ? Planet->GetHeroes() : nullptr)
	{
		Heroes->GetNeutrals()->SetLines(Step.NewNeutrals);
	}
	GetLayerState().UndoSteps.Add(MoveTemp(Step));
	UpdateSavedState();
}

bool UHexPlanetPainter::Regenerate(int32 InNetwork)
{
	FHexPaintStep Step;
	FString What;
	if (!MakeRegenerateStep(InNetwork, Step, What))
	{
		return false;
	}
	if (Step.IsEmpty())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::White, FString::Printf(TEXT("Generating %s again changed nothing"), *What));
		}
		return false;
	}

	Apply(Step.NewLevels, Step.NewBiomes, {}, Step.NewObjects, {}, nullptr, nullptr, Step.NewWalls);
	AddStep(MoveTemp(Step));
	return true;
}

bool UHexPlanetPainter::RegenerateObjects()
{
	if (bPainting || !Planet || !Planet->IsBuilt() || !Planet->GetObjects())
	{
		return false;
	}

	// the whole layer again, painted objects too, as a planet without objects.json gets them
	UHexPlanetObjects* Objects = Planet->GetObjects();
	TArray<FHexCellObjects> Generated;
	if (!Objects->GenerateCells(Planet->GetLevels(), Planet->GetCellBiomes(), Generated))
	{
		return false;
	}
	FHexPaintStep Step;
	for (int32 Cell = 0; Cell < Generated.Num(); ++Cell)
	{
		const FHexCellObjects Old = Objects->GetCell(Cell);
		// what was placed by hand stays, as the generation gives only covers and decor
		Generated[Cell].Placed = Old.Placed;
		if (!(Old == Generated[Cell]))
		{
			Step.OldObjects.Add(Cell, Old);
			Step.NewObjects.Add(Cell, Generated[Cell]);
		}
	}
	if (Step.IsEmpty())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.0f, FColor::White, TEXT("Generating the objects again changed nothing"));
		}
		return false;
	}
	UE_LOG(LogHexHero, Log, TEXT("Painter: generated the objects of the %s again, %d cells changed"), GetPlanetLayerName(Target), Step.NewObjects.Num());
	Apply({}, {}, {}, Step.NewObjects);
	AddStep(MoveTemp(Step));
	return true;
}

bool UHexPlanetPainter::NewLayout()
{
	if (bPainting || Target != EHexPlanetLayer::Underground || !HasLayer(EHexPlanetLayer::Surface) || !Planet->IsDataLoaded())
	{
		return false;
	}

	// the offset first, as the generation reads it from the manifest, then all networks again for the same entrances
	const int32 OldOffset = Layers[0].Planet->GetManifest().Underground.SeedOffset;
	const int32 NewOffset = OldOffset + 1;
	Planet->SetUndergroundSeedOffset(NewOffset);

	FHexPaintStep Step;
	FString What;
	if (!MakeRegenerateStep(INDEX_NONE, Step, What))
	{
		Planet->SetUndergroundSeedOffset(OldOffset);
		return false;
	}
	Step.bSeedOffset = true;
	Step.OldSeedOffset = OldOffset;
	Step.NewSeedOffset = NewOffset;
	UE_LOG(LogHexHero, Log, TEXT("Painter: new layout of the underground, seed offset %d to %d, %d cells changed"), OldOffset, NewOffset, Step.NewLevels.Num());

	Apply(Step.NewLevels, Step.NewBiomes, {}, Step.NewObjects, {}, nullptr, &NewOffset, Step.NewWalls);
	AddStep(MoveTemp(Step));
	return true;
}

bool UHexPlanetPainter::MakeRegenerateStep(int32 InNetwork, FHexPaintStep& OutStep, FString& OutWhat) const
{
	if (bPainting || Target != EHexPlanetLayer::Underground || !HasLayer(EHexPlanetLayer::Surface) || !Planet->IsDataLoaded())
	{
		return false;
	}

	// from the levels of the surface and the entrances, which stay as they are, so the step touches the underground alone
	const AHexPlanet* SurfacePlanet = Layers[0].Planet;
	const double StartTime = FPlatformTime::Seconds();
	HexPlanetUnderground::FResult Result;
	if (InNetwork == INDEX_NONE)
	{
		HexPlanetUnderground::Generate(Planet->GetGrid(), SurfacePlanet->GetLevels(), SurfacePlanet->GetManifest(), &Planet->GetEntrances(), Result);
	}
	else
	{
		HexPlanetUnderground::RegenerateNetwork(Planet->GetGrid(), SurfacePlanet->GetLevels(), SurfacePlanet->GetManifest(), Planet->GetEntrances(), Planet->GetLevels(),
			InNetwork, Result);
	}

	// the cells whose level changed take the biome the generation gives them; the others keep theirs, painted by hand or not
	FHexPaintStep& Step = OutStep;
	const TArray<TCHAR>& Letters = Planet->GetBiomeLetters();
	for (int32 Cell = 0; Cell < Result.Levels.Num(); ++Cell)
	{
		if (Result.Levels[Cell] == Planet->GetCellLevel(Cell))
		{
			continue;
		}
		Step.OldLevels.Add(Cell, Planet->GetCellLevel(Cell));
		Step.NewLevels.Add(Cell, Result.Levels[Cell]);
		const int32 Biome = Result.Biomes.IsValidIndex(Cell) ? Letters.Find(Result.Biomes[Cell]) : INDEX_NONE;
		if (Biome != INDEX_NONE)
		{
			Step.OldBiomes.Add(Cell, Planet->GetCellBiome(Cell));
			Step.NewBiomes.Add(Cell, Biome);
		}
		// a void that became a passage or the other way round has walls along it of the height of its style again
		ClearWallHeight(Step, Cell, Planet->GetCellLevel(Cell), Planet->GetCellBiome(Cell), Result.Levels[Cell], Biome != INDEX_NONE ? Biome : Planet->GetCellBiome(Cell));
	}

	// the cells that became passages take the objects a generation on the new levels gives them, and those that became void lose theirs, so the
	// passages do not stand bare nor the void keep what it cannot show
	UHexPlanetObjects* Objects = Planet->GetObjects();
	if (Objects && !Objects->GetSets().IsEmpty() && !Step.NewLevels.IsEmpty())
	{
		TArray<int8> NewLevels = Planet->GetLevels();
		TArray<uint8> NewBiomes = Planet->GetCellBiomes();
		for (const TPair<int32, int32>& Each : Step.NewLevels)
		{
			NewLevels[Each.Key] = int8(Each.Value);
		}
		for (const TPair<int32, int32>& Each : Step.NewBiomes)
		{
			NewBiomes[Each.Key] = uint8(Each.Value);
		}
		TArray<FHexCellObjects> Generated;
		if (Objects->GenerateCells(NewLevels, NewBiomes, Generated))
		{
			for (const TPair<int32, int32>& Each : Step.NewLevels)
			{
				const FHexCellObjects Old = Objects->GetCell(Each.Key);
				FHexCellObjects New = HexPlanetUnderground::IsPassage(int8(Each.Value)) ? Generated[Each.Key] : FHexCellObjects();
				// a passage keeps what was placed on it by hand, the void loses it with the rest
				if (HexPlanetUnderground::IsPassage(int8(Each.Value)))
				{
					New.Placed = Old.Placed;
				}
				if (!(Old == New))
				{
					Step.OldObjects.Add(Each.Key, Old);
					Step.NewObjects.Add(Each.Key, New);
				}
			}
		}
	}

	OutWhat = InNetwork == INDEX_NONE ? FString(TEXT("all networks")) : FString::Printf(TEXT("network %c"), HexPlanetFile::GetNetworkLetter(InNetwork));
	UE_LOG(LogHexHero, Log, TEXT("Painter: generated %s again in %.1f ms, %d cells changed: %s"), *OutWhat, (FPlatformTime::Seconds() - StartTime) * 1000.0,
		Step.NewLevels.Num(), *Result.Report.ToString());
	return true;
}

bool UHexPlanetPainter::GetToolRelief(int32 Cell, HexPlanetSurface::FReliefOverride& OutRelief) const
{
	// only a cell with a piece of the kit, mountains and hills, which the planet shows now
	const int32 CellLevel = Planet->GetCellLevel(Cell);
	const FIntPoint Version = Planet->GetCellVersion(Cell);
	if (CellLevel < 1 || Version.Y <= 0)
	{
		return false;
	}

	OutRelief = Planet->GetReliefOverride(Cell);
	const FIntPoint TurnChoice = Planet->GetCellTurnChoice(Cell);
	// Ctrl held reverses the tool: back through versions and turns, and lower
	const bool bBack = IsBackwards();
	const int32 Step = bBack ? -1 : 1;

	switch (ReliefTool)
	{
	case EHexReliefTool::Version:
		// only pieces with the same sides, so the neighbors still meet it
		if (Version.Y <= 1)
		{
			return false;
		}
		OutRelief.Version = int8((Version.X + Step + Version.Y) % Version.Y);
		return true;
	case EHexReliefTool::Turn:
		// only turns that give the same set of neighbors; a cell whose neighbors allow one keeps it
		if (TurnChoice.Y <= 1)
		{
			return false;
		}
		OutRelief.Turn = int8((TurnChoice.X + Step + TurnChoice.Y) % TurnChoice.Y);
		return true;
	case EHexReliefTool::Mirror:
		// a mirrored piece is looked up by the mirrored set of neighbors, so it always fits. Its other pieces may be fewer, so the version restarts
		OutRelief.Mirror = Planet->IsCellMirrored(Cell) ? 0 : 1;
		OutRelief.Version = -1;
		return true;
	case EHexReliefTool::Higher:
	case EHexReliefTool::Lower:
	{
		const float Factor = Settings ? Settings->PaintReliefStep : 1.2f;
		const bool bHigher = (ReliefTool == EHexReliefTool::Higher) != bBack;
		const float Scale = bHigher ? OutRelief.Scale * Factor : OutRelief.Scale / Factor;
		OutRelief.Scale = FMath::Clamp(Scale, 0.3f, 3.0f);
		return true;
	}
	default:
		if (OutRelief.IsEmpty())
		{
			return false;
		}
		OutRelief = HexPlanetSurface::FReliefOverride();
		return true;
	}
}

void UHexPlanetPainter::ClearWallHeight(FHexPaintStep& Step, int32 Cell, int32 OldLevel, int32 OldBiome, int32 NewLevel, int32 NewBiome, TMap<int32, float>* Changed) const
{
	if (!Planet || Step.NewWalls.Contains(Cell) || Planet->GetWallStyle(OldLevel, OldBiome) == Planet->GetWallStyle(NewLevel, NewBiome))
	{
		return;
	}
	const float Override = Planet->GetWallHeightOverride(Cell);
	if (Override < 0.0f)
	{
		return;
	}
	Step.OldWalls.Add(Cell, Override);
	Step.NewWalls.Add(Cell, -1.0f);
	if (Changed)
	{
		Changed->Add(Cell, -1.0f);
	}
}

void UHexPlanetPainter::PaintWalls(TConstArrayView<int32> Brush)
{
	// the voids of the brush, and the voids beside its passages, so a click on the floor before a wall reaches it too; only those with a passage
	// beside them, which have walls along them
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const auto IsVoid = [this](int32 Cell) { return !HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Cell))); };
	TArray<int32, TInlineAllocator<64>> Voids;
	for (const int32 Painted : Brush)
	{
		if (IsVoid(Painted))
		{
			Voids.AddUnique(Painted);
			continue;
		}
		for (const int32 Neighbor : Cells[Painted].Neighbors)
		{
			if (IsVoid(Neighbor))
			{
				Voids.AddUnique(Neighbor);
			}
		}
	}

	const float Step = Settings ? FMath::Max(Settings->WallHeightStep, 0.01f) : 0.1f;
	TMap<int32, float> Changed;
	for (const int32 Void : Voids)
	{
		if (Stroke.NewWalls.Contains(Void) || !Cells[Void].Neighbors.ContainsByPredicate([&IsVoid](int32 Neighbor) { return !IsVoid(Neighbor); }))
		{
			continue;
		}
		const float Old = Planet->GetWallHeightOverride(Void);
		float New = -1.0f;
		if (Tool == EHexPaintTool::WallAuto)
		{
			if (Old < 0.0f)
			{
				continue;
			}
		}
		else
		{
			// from the height shown, whether set by hand or by the style, within WallHeightMin and WallHeightMax; nothing at a limit
			const float Current = Planet->GetWallHeight(Void);
			New = Settings ? Settings->ClampWallHeight(Current + (IsBackwards() ? -Step : Step)) : Current;
			if (FMath::IsNearlyEqual(New, Current, 1e-4f))
			{
				continue;
			}
		}
		Stroke.OldWalls.Add(Void, Old);
		Stroke.NewWalls.Add(Void, New);
		Changed.Add(Void, New);
	}

	if (!Changed.IsEmpty())
	{
		UE_LOG(LogHexHero, Log, TEXT("Painter: the walls along %d voids to %s"), Changed.Num(), *FString::JoinBy(Changed, TEXT(", "), [](const TPair<int32, float>& Each)
		{
			return Each.Value < 0.0f ? FString::Printf(TEXT("%d by its style"), Each.Key) : FString::Printf(TEXT("%d x%.2f"), Each.Key, Each.Value);
		}));
		Planet->SetWallHeights(Changed, true);
	}
}

void UHexPlanetPainter::Apply(const TMap<int32, int32>& Levels, const TMap<int32, int32>& Biomes, const HexPlanetSurface::FReliefOverrides& Relief,
	const TMap<int32, FHexCellObjects>& Objects, const TMap<uint64, uint8>& Rivers, const TArray<FHexEntrance>* Entrances, const int32* SeedOffset,
	const TMap<int32, float>& Walls)
{
	// every change here comes from a step of Undo, so going back to the step of the last save brings the saved state back
	if (SeedOffset)
	{
		Planet->SetUndergroundSeedOffset(*SeedOffset);
	}

	if (!Objects.IsEmpty())
	{
		Planet->GetObjects()->SetCells(Objects);
	}

	if (!Rivers.IsEmpty() && Planet->GetRivers())
	{
		Planet->GetRivers()->SetEdges(Rivers, true);
	}

	if (!Relief.IsEmpty())
	{
		Planet->SetReliefOverrides(Relief, true);
	}

	TMap<int32, FIntPoint>& PendingLevels = GetLayerState().PendingLevels;
	for (const TPair<int32, int32>& Each : Levels)
	{
		// the surface still shows the level it had before the first change that is not rebuilt yet
		const FIntPoint* Pending = PendingLevels.Find(Each.Key);
		const int32 Shown = Pending ? Pending->X : Planet->GetCellLevel(Each.Key);
		if (Shown != Each.Value || Pending)
		{
			PendingLevels.Add(Each.Key, FIntPoint(Shown, Each.Value));
		}
	}

	if (!Levels.IsEmpty() || !Biomes.IsEmpty() || !Walls.IsEmpty())
	{
		Planet->SetCells(Levels, Biomes, Walls);
	}

	// the entrances mark themselves as edited, and a step of them alone changes no cell
	if (Entrances)
	{
		Planet->SetEntrances(*Entrances, true);
	}
	if (!Entrances || !Levels.IsEmpty() || !Biomes.IsEmpty() || !Relief.IsEmpty() || !Objects.IsEmpty() || !Rivers.IsEmpty() || !Walls.IsEmpty())
	{
		Planet->MarkLevelsEdited(true);
	}
	UpdatePreview(int32(Target));
}

void UHexPlanetPainter::ApplyCave(const TMap<int32, int32>& Levels, const TMap<int32, int32>& Biomes)
{
	AHexPlanet* Underground = Levels.IsEmpty() || !Planet ? nullptr : Planet->GetLayerPlanet(EHexPlanetLayer::Underground);
	if (Underground)
	{
		Underground->SetCells(Levels, Biomes);
		Underground->MarkLevelsEdited(true);
	}
}

void UHexPlanetPainter::OnLevelsRebuilt(int32 Layer)
{
	if (!Layers.IsValidIndex(Layer) || !Layers[Layer].Planet)
	{
		return;
	}

	// a tile goes once the surface shows the level of its cell, so tiles of changes still waiting stay
	FHexPainterLayer& State = Layers[Layer];
	const TArray<int8>& Shown = State.Planet->GetShownLevels();
	const int32 NumBefore = State.PendingLevels.Num();

	for (auto It = State.PendingLevels.CreateIterator(); It; ++It)
	{
		if (!Shown.IsValidIndex(It.Key()) || Shown[It.Key()] == State.Planet->GetCellLevel(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	if (State.PendingLevels.Num() != NumBefore)
	{
		UpdatePreview(Layer);
	}
}

double UHexPlanetPainter::GetTileRadius(const FHexPainterLayer& State, int32 OldLevel, int32 NewLevel) const
{
	const UHexPlanetConfig* Config = State.Settings;
	const auto GetTop = [Config](int32 Each)
	{
		return Each >= 2 ? Config->PaintMountainTop : Each < 0 ? Config->PaintSeaLift : (Each + 1) * Config->LevelHeight;
	};
	return State.Planet->GetRadius() + FMath::Max(GetTop(OldLevel), GetTop(NewLevel)) + Config->PaintLift;
}

void UHexPlanetPainter::UpdatePreview(int32 Layer)
{
	if (!Layers.IsValidIndex(Layer) || !Layers[Layer].PaintPreview || !Layers[Layer].Settings)
	{
		return;
	}
	FHexPainterLayer& State = Layers[Layer];

	State.PaintPreview->ClearAllMeshSections();

	TMap<int32, FIntPoint> Tiles = State.PendingLevels;
	if (Layer == int32(Target))
	{
		for (const TPair<int32, int32>& Each : Stroke.NewLevels)
		{
			const FIntPoint* Pending = State.PendingLevels.Find(Each.Key);
			Tiles.Add(Each.Key, FIntPoint(Pending ? Pending->X : Stroke.OldLevels[Each.Key], Each.Value));
		}
	}

	// the arrow of an entrance about to be put: from the mountain its mouth would be built into to the cell it would stand on
	const bool bHint = Layer == int32(Target) && Tool == EHexPaintTool::Entrance && HintBefore != INDEX_NONE && HintMountain != INDEX_NONE;
	if (Tiles.IsEmpty() && !bHint)
	{
		return;
	}

	TArray<FVector> Positions;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FVector2D> CellUVs;
	TArray<FVector2D> LocalUVs;
	TArray<FLinearColor> Colors;

	const TArray<FHexSphereCell>& Cells = State.Planet->GetGrid().GetCells();
	const TArray<FVector3d>& Corners = State.Planet->GetGrid().GetCorners();
	const EHexPlanetLayer TileLayer = EHexPlanetLayer(Layer);

	for (const TPair<int32, FIntPoint>& Tile : Tiles)
	{
		const FHexSphereCell& Each = Cells[Tile.Key];
		const double Top = GetTileRadius(State, Tile.Value.X, Tile.Value.Y);
		FLinearColor Color = GetLevelColor(TileLayer, Tile.Value.Y);
		Color.A = 0.0f;

		// a fan from the center, whose U of the first UV is 0 there and 1 at the sides, so M_HexPlanet draws the grid line on it, and whose third
		// and fourth UVs are the pixel of its cell in CellMarks and where a vertex lies in the cell, as on the surface, so it draws the marks of
		// its own cell, such as the outline of the brush
		const FVector2D CellUV = HexPlanetSurface::GetCellUV(Tile.Key);
		const int32 Center = Positions.Add(FVector(Each.Center * Top));
		Normals.Add(FVector(Each.Center));
		UVs.Add(FVector2D::ZeroVector);
		CellUVs.Add(CellUV);
		LocalUVs.Add(FVector2D::ZeroVector);
		Colors.Add(Color);

		const int32 NumCorners = Each.Corners.Num();
		for (int32 Index = 0; Index < NumCorners; ++Index)
		{
			// corner K ends the side towards neighbor K
			Positions.Add(FVector(Corners[Each.Corners[Index]] * Top));
			Normals.Add(FVector(Each.Center));
			UVs.Add(FVector2D(1.0, 0.0));
			CellUVs.Add(CellUV);
			LocalUVs.Add(HexPlanetSurface::GetLocalUV(NumCorners, Index, 0.0, 1.0));
			Colors.Add(Color);
		}

		for (int32 Index = 0; Index < NumCorners; ++Index)
		{
			const int32 A = Center + 1 + Index;
			const int32 B = Center + 1 + (Index + 1) % NumCorners;

			// a triangle shows on the side where (C - A) x (B - A) points, which must be outwards
			const FVector3d Facing = FVector3d::CrossProduct(FVector3d(Positions[B]) - FVector3d(Positions[Center]), FVector3d(Positions[A]) - FVector3d(Positions[Center]));
			if (FVector3d::DotProduct(Facing, Each.Center) >= 0.0)
			{
				Triangles.Append({Center, A, B});
			}
			else
			{
				Triangles.Append({Center, B, A});
			}
		}
	}

	if (bHint)
	{
		// As the developer asked on 2026-11-12 ("малюй стрілку підказку куди стане і напрямок куди вихід буде"): a flat arrow over the
		// ground of the cell before the mouth, in the color of the network, from the side the mouth is built at to the middle of that cell
		const FVector3d From = Cells[HintMountain].Center;
		const FVector3d To = Cells[HintBefore].Center;
		const double Radius = FMath::Max(State.Planet->GetCellTopRadius(HintBefore), State.Planet->GetCellTopRadius(HintMountain) * 0.0) + double(State.Settings->PaintLift) * 3.0 + 8.0;
		const FVector3d Along = (To - From).GetSafeNormal();
		const FVector3d Across = FVector3d::CrossProduct(From, Along).GetSafeNormal();
		const double Width = FVector3d::Distance(From, To) * 0.09;
		FLinearColor Color = GetNetworkColor(Network);
		// its alpha is how much a tile glows
		Color.A = 1.0f;
		const auto Add = [&](double Share, double Side)
		{
			const FVector3d Point = (FMath::Lerp(From, To, Share) + Across * (Side * Width)).GetSafeNormal();
			const int32 Index = Positions.Add(FVector(Point * Radius));
			Normals.Add(FVector(Point));
			UVs.Add(FVector2D::ZeroVector);
			CellUVs.Add(HexPlanetSurface::GetCellUV(HintBefore));
			LocalUVs.Add(FVector2D::ZeroVector);
			Colors.Add(Color);
			return Index;
		};
		const int32 Shaft[4] = {Add(0.5, -1.0), Add(0.5, 1.0), Add(0.8, 1.0), Add(0.8, -1.0)};
		const int32 Head[3] = {Add(0.8, -2.6), Add(0.8, 2.6), Add(1.05, 0.0)};
		// both ways round, so it shows whichever way its triangles face
		for (const TArray<int32>& Triangle : {TArray<int32>{Shaft[0], Shaft[1], Shaft[2]}, TArray<int32>{Shaft[0], Shaft[2], Shaft[3]}, TArray<int32>{Head[0], Head[1], Head[2]}})
		{
			Triangles.Append({Triangle[0], Triangle[1], Triangle[2]});
			Triangles.Append({Triangle[0], Triangle[2], Triangle[1]});
		}
	}

	// the second UV, of the piece, is not drawn on a tile
	TArray<FVector2D> PieceUVs;
	PieceUVs.Init(FVector2D::ZeroVector, Positions.Num());
	State.PaintPreview->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, UVs, PieceUVs, CellUVs, LocalUVs, Colors, {}, false);
	// the material of the cells draws the textures of the biomes, and with PreviewTile the vertex colors of the tiles instead, with the marks of
	// CellMarks of the planet, which keeps it up to date
	State.PaintPreview->SetMaterial(0, State.Planet->GetPreviewMaterial());
}

#undef LOCTEXT_NAMESPACE
