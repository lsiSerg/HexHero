// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexPlanetPainter.h"
#include "HexPlanet.h"
#include "HexPlanetConfig.h"
#include "HexBiomeConfig.h"
#include "HexPlanetObjectSet.h"
#include "HexPlanetRivers.h"
#include "HexSphereGrid.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#define LOCTEXT_NAMESPACE "HexPlanetPainter"

void UHexPlanetPainter::Init(AHexPlanet* InPlanet)
{
	Planet = InPlanet;

	if (!Planet)
	{
		return;
	}

	Settings = Planet->PlanetConfig.LoadSynchronous();
	if (!Settings)
	{
		Settings = GetDefault<UHexPlanetConfig>();
	}

	PaintPreview = NewObject<UProceduralMeshComponent>(Planet, TEXT("PaintPreview"));
	PaintPreview->SetupAttachment(Planet->GetRootComponent());
	PaintPreview->SetCastShadow(false);
	PaintPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PaintPreview->RegisterComponent();

	Planet->OnLevelsRebuilt.AddUObject(this, &UHexPlanetPainter::OnLevelsRebuilt);
}

void UHexPlanetPainter::SetActive(bool bInActive)
{
	if (!bInActive && bPainting)
	{
		EndStroke();
	}
	bActive = bInActive;
}

bool UHexPlanetPainter::IsRebuilding() const
{
	return Planet && Planet->IsRebuilding();
}

FLinearColor UHexPlanetPainter::GetLevelColor(int32 InLevel) const
{
	const UHexPlanetConfig* Config = Settings ? Settings.Get() : GetDefault<UHexPlanetConfig>();
	return Config->LevelColors.IsValidIndex(InLevel - MinLevel) ? Config->LevelColors[InLevel - MinLevel] : FLinearColor::Gray;
}

int32 UHexPlanetPainter::GetNumBiomes() const
{
	return Planet ? Planet->GetBiomes().Num() : 0;
}

FText UHexPlanetPainter::GetBiomeName(int32 InBiome) const
{
	return Planet && Planet->GetBiomes().IsValidIndex(InBiome) ? FText::FromString(Planet->GetBiomes()[InBiome]->DisplayName) : FText::GetEmpty();
}

FLinearColor UHexPlanetPainter::GetBiomeColor(int32 InBiome) const
{
	return Planet && Planet->GetBiomes().IsValidIndex(InBiome) ? FLinearColor(Planet->GetBiomes()[InBiome]->FileColor) : FLinearColor::Gray;
}

void UHexPlanetPainter::ClearTool()
{
	if (bPainting)
	{
		EndStroke();
	}
	Tool = EHexPaintTool::None;
}

int32 UHexPlanetPainter::GetNumObjectSets() const
{
	return Planet && Planet->GetObjects() ? Planet->GetObjects()->GetSets().Num() : 0;
}

FText UHexPlanetPainter::GetObjectSetName(int32 Set) const
{
	return Set < GetNumObjectSets() ? FText::FromString(Planet->GetObjects()->GetSets()[Set]->DisplayName) : FText::GetEmpty();
}

FLinearColor UHexPlanetPainter::GetObjectSetColor(int32 Set) const
{
	return Set < GetNumObjectSets() ? Planet->GetObjects()->GetSets()[Set]->PaletteColor : FLinearColor::Gray;
}

bool UHexPlanetPainter::IsObjectSetCover(int32 Set) const
{
	return Set < GetNumObjectSets() && Planet->GetObjects()->GetSets()[Set]->Layer == EHexObjectLayer::Cover;
}

FText UHexPlanetPainter::GetLevelName(int32 InLevel)
{
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
	return InLevel > 1 && Planet->GetGrid().GetCells()[Cell].IsPentagon() ? 1 : InLevel;
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
	Stroke = FStep();
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
	if (Cell == LastCell)
	{
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
		UpdatePreview();
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

	// objects go to the planet at once, once per cell and stroke, a kind only to the cells whose level allows it
	if (Tool == EHexPaintTool::Object || Tool == EHexPaintTool::Eraser)
	{
		UHexPlanetObjects* Objects = Planet->GetObjects();
		TMap<int32, FHexCellObjects> Changed;
		for (const int32 Painted : Brush)
		{
			if (Stroke.NewObjects.Contains(Painted))
			{
				continue;
			}
			const FHexCellObjects Old = Objects->GetCell(Painted);
			FHexCellObjects New = Old;
			if (Tool == EHexPaintTool::Eraser)
			{
				New = FHexCellObjects();
			}
			else if (Objects->IsAllowed(ObjectSet, Planet->GetCellLevel(Painted)))
			{
				if (IsObjectSetCover(ObjectSet))
				{
					New.Cover = uint8(ObjectSet + 1);
					New.Density = uint8(ObjectDensity);
				}
				else
				{
					New.Decor = uint8(ObjectSet + 1);
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
			Planet->MarkLevelsEdited();
		}
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
			Planet->SetReliefOverrides(Changed);
		}
		return;
	}

	// biomes go to the planet at once, only to the cells whose level allows them
	if (Tool == EHexPaintTool::Biome)
	{
		TMap<int32, int32> Changed;
		for (const int32 Painted : Brush)
		{
			const int32 Current = Planet->GetCellBiome(Painted);
			if (Current != Biome && Planet->IsBiomeAllowed(Biome, Planet->GetCellLevel(Painted)))
			{
				Stroke.OldBiomes.FindOrAdd(Painted, Current);
				Stroke.NewBiomes.Add(Painted, Biome);
				Changed.Add(Painted, Biome);
			}
		}

		if (!Changed.IsEmpty())
		{
			Planet->SetCells({}, Changed);
			Planet->MarkLevelsEdited();
		}
		return;
	}

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
	if (!Rivers)
	{
		return;
	}
	const FHexSphereGrid& Grid = Planet->GetGrid();
	const FHexSphereCell& Each = Grid.GetCells()[Cell];
	const FVector3d Direction = FVector3d(CursorPoint).GetSafeNormal();
	const bool bRemove = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsControlDown();
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
		Rivers->SetEdges(Changed);
	}
}

void UHexPlanetPainter::EndStroke()
{
	if (!bPainting)
	{
		return;
	}

	bPainting = false;
	LastCell = INDEX_NONE;

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

	if (Stroke.NewLevels.IsEmpty() && Stroke.NewBiomes.IsEmpty() && Stroke.NewRelief.IsEmpty() && Stroke.NewObjects.IsEmpty() && Stroke.NewRivers.IsEmpty())
	{
		UpdatePreview();
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
	}
	UndoSteps.Add(MoveTemp(Stroke));
	Stroke = FStep();
	RedoSteps.Reset();

	const int32 MaxSteps = Settings ? Settings->PaintUndoSteps : 100;
	if (UndoSteps.Num() > MaxSteps)
	{
		UndoSteps.RemoveAt(0, UndoSteps.Num() - MaxSteps);
	}
}

void UHexPlanetPainter::Undo()
{
	if (bPainting || UndoSteps.IsEmpty())
	{
		return;
	}

	FStep Step = UndoSteps.Pop();
	Apply(Step.OldLevels, Step.OldBiomes, Step.OldRelief, Step.OldObjects, Step.OldRivers);
	RedoSteps.Add(MoveTemp(Step));
}

void UHexPlanetPainter::Redo()
{
	if (bPainting || RedoSteps.IsEmpty())
	{
		return;
	}

	FStep Step = RedoSteps.Pop();
	Apply(Step.NewLevels, Step.NewBiomes, Step.NewRelief, Step.NewObjects, Step.NewRivers);
	UndoSteps.Add(MoveTemp(Step));
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
	const bool bBack = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsControlDown();
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

void UHexPlanetPainter::Apply(const TMap<int32, int32>& Levels, const TMap<int32, int32>& Biomes, const HexPlanetSurface::FReliefOverrides& Relief,
	const TMap<int32, FHexCellObjects>& Objects, const TMap<uint64, uint8>& Rivers)
{
	if (!Objects.IsEmpty())
	{
		Planet->GetObjects()->SetCells(Objects);
	}

	if (!Rivers.IsEmpty() && Planet->GetRivers())
	{
		Planet->GetRivers()->SetEdges(Rivers);
	}

	if (!Relief.IsEmpty())
	{
		Planet->SetReliefOverrides(Relief);
	}

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

	if (!Levels.IsEmpty() || !Biomes.IsEmpty())
	{
		Planet->SetCells(Levels, Biomes);
	}
	Planet->MarkLevelsEdited();
	UpdatePreview();
}

void UHexPlanetPainter::OnLevelsRebuilt()
{
	// a tile goes once the surface shows the level of its cell, so tiles of changes still waiting stay
	const TArray<int8>& Shown = Planet->GetShownLevels();
	const int32 NumBefore = PendingLevels.Num();

	for (auto It = PendingLevels.CreateIterator(); It; ++It)
	{
		if (!Shown.IsValidIndex(It.Key()) || Shown[It.Key()] == Planet->GetCellLevel(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	if (PendingLevels.Num() != NumBefore)
	{
		UpdatePreview();
	}
}

double UHexPlanetPainter::GetTileRadius(int32 OldLevel, int32 NewLevel) const
{
	const auto GetTop = [this](int32 Each)
	{
		return Each >= 2 ? Settings->PaintMountainTop : Each < 0 ? Settings->PaintSeaLift : (Each + 1) * Settings->LevelHeight;
	};
	return Planet->GetRadius() + FMath::Max(GetTop(OldLevel), GetTop(NewLevel)) + Settings->PaintLift;
}

void UHexPlanetPainter::UpdatePreview()
{
	if (!PaintPreview)
	{
		return;
	}

	PaintPreview->ClearAllMeshSections();

	TMap<int32, FIntPoint> Tiles = PendingLevels;
	for (const TPair<int32, int32>& Each : Stroke.NewLevels)
	{
		const FIntPoint* Pending = PendingLevels.Find(Each.Key);
		Tiles.Add(Each.Key, FIntPoint(Pending ? Pending->X : Stroke.OldLevels[Each.Key], Each.Value));
	}

	if (Tiles.IsEmpty())
	{
		return;
	}

	TArray<FVector> Positions;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;

	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const TArray<FVector3d>& Corners = Planet->GetGrid().GetCorners();

	for (const TPair<int32, FIntPoint>& Tile : Tiles)
	{
		const FHexSphereCell& Each = Cells[Tile.Key];
		const double Top = GetTileRadius(Tile.Value.X, Tile.Value.Y);
		FLinearColor Color = GetLevelColor(Tile.Value.Y);
		Color.A = 0.0f;

		// a fan from the center, whose U of the first UV is 0 there and 1 at the sides, so M_HexPlanet draws the grid line on it
		const int32 Center = Positions.Add(FVector(Each.Center * Top));
		Normals.Add(FVector(Each.Center));
		UVs.Add(FVector2D::ZeroVector);
		Colors.Add(Color);

		const int32 NumCorners = Each.Corners.Num();
		for (int32 Index = 0; Index < NumCorners; ++Index)
		{
			Positions.Add(FVector(Corners[Each.Corners[Index]] * Top));
			Normals.Add(FVector(Each.Center));
			UVs.Add(FVector2D(1.0, 0.0));
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

	PaintPreview->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, UVs, Colors, {}, false);
	// M_HexPlanet draws the textures of the biomes, and with PreviewTile the vertex colors of the tiles instead
	if (!PreviewMaterial)
	{
		if (UMaterialInterface* Base = Planet->Material.LoadSynchronous())
		{
			PreviewMaterial = UMaterialInstanceDynamic::Create(Base, this);
			PreviewMaterial->SetScalarParameterValue(TEXT("PreviewTile"), 1.0f);
		}
	}
	PaintPreview->SetMaterial(0, PreviewMaterial);
}

#undef LOCTEXT_NAMESPACE
