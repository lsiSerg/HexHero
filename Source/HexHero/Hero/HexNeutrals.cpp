// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexNeutrals.h"
#include "HexBattleConfig.h"
#include "HexCastles.h"
#include "HexHero.h"
#include "HexHeroes.h"
#include "HexPlanet.h"
#include "HexPlanetFile.h"
#include "HexSites.h"
#include "HexSiteConfig.h"
#include "HexUnitFigure.h"
#include "JsonObjectConverter.h"
#include "Components/SceneComponent.h"
#include "Misc/FileHelper.h"

namespace
{
	const TCHAR* NeutralsFile = TEXT("neutrals.json");

	FString EscapeText(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}
}

void UHexNeutrals::Init(AHexPlanet* InSurface, UHexHeroes* InHeroes)
{
	Surface = InSurface;
	Heroes = InHeroes;
	Castles = UHexCastleConfig::Load();
	Config = UHexBattleConfig::Load();
	Neutrals.Reset();
	SavedLines.Reset();
	NextId = 1;
	if (!Surface || !Surface->IsSaved())
	{
		return;
	}
	const FString FilePath = HexPlanetFile::GetFilePath(Surface->GetPlanetFolder(), NeutralsFile);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		return;
	}
	FHexNeutralsFile File;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Text, &File))
	{
		UE_LOG(LogHexHero, Error, TEXT("Neutrals: %s is not valid JSON"), *FilePath);
		return;
	}
	SetLines(File.Neutrals);
	SavedLines = SortLines(MakeLines());
	UE_LOG(LogHexHero, Log, TEXT("Neutrals: %d stacks on the planet"), Neutrals.Num());
}

TArray<FHexNeutralLine> UHexNeutrals::SortLines(TArray<FHexNeutralLine> Lines)
{
	Lines.StableSort([](const FHexNeutralLine& A, const FHexNeutralLine& B) { return A.Id < B.Id; });
	return Lines;
}

TArray<FHexNeutralLine> UHexNeutrals::MakeLines() const
{
	TArray<FHexNeutralLine> Lines;
	for (const FHexNeutral& Neutral : Neutrals)
	{
		if (!Castles || !Castles->Factions.IsValidIndex(Neutral.Stack.Faction) || !Castles->Factions[Neutral.Stack.Faction].Creatures.IsValidIndex(Neutral.Stack.Creature))
		{
			continue;
		}
		FHexNeutralLine& Line = Lines.AddDefaulted_GetRef();
		Line.Id = Neutral.Id;
		Line.Faction = Castles->Factions[Neutral.Stack.Faction].Id.ToString();
		Line.Creature = Castles->Factions[Neutral.Stack.Faction].Creatures[Neutral.Stack.Creature].Id.ToString();
		Line.Upgraded = Neutral.Stack.bUpgraded;
		Line.Count = Neutral.Stack.Count;
		Line.Layer = GetPlanetLayerName(Neutral.Layer);
		Line.Cell = Neutral.Cell;
	}
	return Lines;
}

void UHexNeutrals::SetLines(const TArray<FHexNeutralLine>& Lines)
{
	for (const FHexNeutral& Neutral : Neutrals)
	{
		RemoveBody(Neutral.Id);
	}
	Neutrals.Reset();
	NextId = 1;
	const int32 NumCells = Surface ? Surface->GetGrid().GetCells().Num() : 0;
	for (const FHexNeutralLine& Line : Lines)
	{
		FHexNeutral Neutral;
		Neutral.Id = Line.Id;
		Neutral.Cell = Line.Cell;
		const int32 Faction = Castles ? Castles->FindFaction(Line.Faction) : INDEX_NONE;
		const int32 Creature = Faction != INDEX_NONE ? Castles->Factions[Faction].Creatures.IndexOfByPredicate([&Line](const FHexCreature& Each) { return Each.Id.ToString() == Line.Creature; })
			: INDEX_NONE;
		if (Line.Id <= 0 || Creature == INDEX_NONE || Line.Count <= 0 || Line.Cell < 0 || Line.Cell >= NumCells || !ParsePlanetLayer(Line.Layer, Neutral.Layer)
			|| Neutrals.ContainsByPredicate([&Line](const FHexNeutral& Each) { return Each.Id == Line.Id; }))
		{
			UE_LOG(LogHexHero, Warning, TEXT("Neutrals: line %d is left out, as it names no creature, cell or layer the game has"), Line.Id);
			continue;
		}
		Neutral.Stack = FHexStack{Faction, Creature, Line.Upgraded, Line.Count};
		Neutrals.Add(Neutral);
		NextId = FMath::Max(NextId, Line.Id + 1);
	}
	UHexHeroes::NoteMapChanged();
}

bool UHexNeutrals::Save(const FString& PlanetFolder, FString& OutError)
{
	const FString FilePath = HexPlanetFile::GetFilePath(PlanetFolder, NeutralsFile);
	const TArray<FHexNeutralLine> Lines = SortLines(MakeLines());
	if (Lines.IsEmpty())
	{
		if (IFileManager::Get().FileExists(*FilePath) && !IFileManager::Get().Delete(*FilePath))
		{
			OutError = FString::Printf(TEXT("could not delete %s"), *FilePath);
			return false;
		}
		SavedLines = Lines;
		return true;
	}
	FString Text = TEXT("{\n\t\"version\": 1,\n\t\"neutrals\": [");
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FHexNeutralLine& Line = Lines[Index];
		Text += FString::Printf(TEXT("%s\n\t\t{\"id\": %d, \"faction\": \"%s\", \"creature\": \"%s\", \"upgraded\": %s, \"count\": %d, \"layer\": \"%s\", \"cell\": %d}"),
			Index == 0 ? TEXT("") : TEXT(","), Line.Id, *EscapeText(Line.Faction), *EscapeText(Line.Creature), Line.Upgraded ? TEXT("true") : TEXT("false"), Line.Count,
			*EscapeText(Line.Layer), Line.Cell);
	}
	Text += TEXT("\n\t]\n}\n");
	if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	SavedLines = Lines;
	return true;
}

void UHexNeutrals::Restart()
{
	SetLines(SavedLines);
}

const FHexNeutral* UHexNeutrals::FindAt(EHexPlanetLayer Layer, int32 Cell) const
{
	return Neutrals.FindByPredicate([Layer, Cell](const FHexNeutral& Each) { return Each.Layer == Layer && Each.Cell == Cell; });
}

const FHexNeutral* UHexNeutrals::Find(int32 Id) const
{
	return Neutrals.FindByPredicate([Id](const FHexNeutral& Each) { return Each.Id == Id; });
}

bool UHexNeutrals::IsFree(EHexPlanetLayer Layer, int32 Cell) const
{
	const AHexPlanet* Planet = Heroes ? Heroes->GetLayerPlanet(Layer) : nullptr;
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell) || !Heroes->IsOpenGround(Layer, Cell) || Planet->GetGrid().GetCells()[Cell].IsPentagon())
	{
		return false;
	}
	return !FindAt(Layer, Cell) && !Heroes->FindHeroAt(Layer, Cell) && !(Heroes->GetSites() && Heroes->GetSites()->FindSiteAt(Layer, Cell))
		&& !(Heroes->GetCastles() && Heroes->GetCastles()->FindCastleAt(Layer, Cell)) && Planet->FindEntrance(Cell) == INDEX_NONE;
}

int32 UHexNeutrals::Add(EHexPlanetLayer Layer, int32 Cell, const FHexStack& Stack, FString& OutProblem)
{
	if (!Castles || !Castles->GetStats(Stack) || Stack.Count <= 0)
	{
		OutProblem = TEXT("there is no such creature");
		return 0;
	}
	if (!IsFree(Layer, Cell))
	{
		OutProblem = TEXT("a neutral stack stands only on open ground with nothing else on it, and not on an entrance");
		return 0;
	}
	FHexNeutral& Neutral = Neutrals.AddDefaulted_GetRef();
	Neutral.Id = NextId++;
	Neutral.Layer = Layer;
	Neutral.Cell = Cell;
	Neutral.Stack = Stack;
	UHexHeroes::NoteMapChanged();
	return Neutral.Id;
}

bool UHexNeutrals::RemoveAt(EHexPlanetLayer Layer, int32 Cell)
{
	const FHexNeutral* Found = FindAt(Layer, Cell);
	return Found && Remove(Found->Id);
}

bool UHexNeutrals::Remove(int32 Id)
{
	const int32 Index = Neutrals.IndexOfByPredicate([Id](const FHexNeutral& Each) { return Each.Id == Id; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	RemoveBody(Id);
	Neutrals.RemoveAt(Index);
	UHexHeroes::NoteMapChanged();
	return true;
}

void UHexNeutrals::SetCount(int32 Id, int32 Count)
{
	if (Count <= 0)
	{
		Remove(Id);
		return;
	}
	if (FHexNeutral* Neutral = Neutrals.FindByPredicate([Id](const FHexNeutral& Each) { return Each.Id == Id; }))
	{
		Neutral->Stack.Count = Count;
	}
}

bool UHexNeutrals::Clear()
{
	if (Neutrals.IsEmpty())
	{
		return false;
	}
	for (const FHexNeutral& Neutral : Neutrals)
	{
		RemoveBody(Neutral.Id);
	}
	Neutrals.Reset();
	NextId = 1;
	UHexHeroes::NoteMapChanged();
	return true;
}

FString UHexNeutrals::GetCreatureName(const FHexStack& Stack) const
{
	const FHexCreatureStats* Stats = Castles ? Castles->GetStats(Stack) : nullptr;
	return Stats ? Stats->DisplayName : FString(TEXT("creatures"));
}

FString UHexNeutrals::DescribeCount(int32 Count)
{
	// as a hero sees a stack from afar, not the number itself; the words are Claude's
	struct FWord { int32 From; const TCHAR* Word; };
	static const FWord Words[] = {{1000, TEXT("A legion of")}, {500, TEXT("A host of")}, {250, TEXT("A swarm of")}, {100, TEXT("A throng of")}, {50, TEXT("A horde of")},
		{20, TEXT("Many")}, {10, TEXT("A pack of")}, {5, TEXT("Several")}, {1, TEXT("A few")}};
	for (const FWord& Each : Words)
	{
		if (Count >= Each.From)
		{
			return Each.Word;
		}
	}
	return TEXT("A few");
}

FString UHexNeutrals::Plural(const FString& Name)
{
	// the Decayed and the Oathsworn stay as they are, a huntsman becomes huntsmen
	if (Name.EndsWith(TEXT("s")) || Name.EndsWith(TEXT("ed")) || Name.EndsWith(TEXT("sworn")) || Name.EndsWith(TEXT("born")))
	{
		return Name;
	}
	if (Name.EndsWith(TEXT("man")))
	{
		return Name.LeftChop(3) + TEXT("men");
	}
	return Name + TEXT("s");
}

FString UHexNeutrals::GetCellText(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexNeutral* Neutral = FindAt(Layer, Cell);
	if (!Neutral)
	{
		return FString();
	}
	// the number itself while the game is not played, for editing
	return Heroes && Heroes->IsPlaying()
		? FString::Printf(TEXT("Neutral: %s %s"), *DescribeCount(Neutral->Stack.Count), *Plural(GetCreatureName(Neutral->Stack)).ToLower())
		: FString::Printf(TEXT("Neutral: %d %s (%s)"), Neutral->Stack.Count, *GetCreatureName(Neutral->Stack), *DescribeCount(Neutral->Stack.Count).ToLower());
}

void UHexNeutrals::RemoveBody(int32 Id)
{
	if (FBody* Body = Bodies.Find(Id))
	{
		for (const TWeakObjectPtr<USceneComponent>& Figure : Body->Figures)
		{
			if (Figure.IsValid())
			{
				Figure->DestroyComponent();
			}
		}
	}
	Bodies.Remove(Id);
}

void UHexNeutrals::OnLayerRebuilt(EHexPlanetLayer Layer)
{
	for (const FHexNeutral& Neutral : Neutrals)
	{
		if (Neutral.Layer == Layer)
		{
			RemoveBody(Neutral.Id);
		}
	}
}

void UHexNeutrals::PlaceBody(const FHexNeutral& Neutral)
{
	AHexPlanet* Planet = Heroes ? Heroes->GetPlanet(Neutral.Layer) : nullptr;
	if (!Planet || !Planet->IsBuilt() || !Config)
	{
		return;
	}
	RemoveBody(Neutral.Id);
	FBody& Body = Bodies.Add(Neutral.Id);
	Body.Cell = Neutral.Cell;
	Body.Layer = Neutral.Layer;
	Body.Count = Neutral.Stack.Count;
	const FHexSphereCell& Cell = Planet->GetGrid().GetCells()[Neutral.Cell];
	const FVector3d Up = Cell.Center;
	// they face the south, towards the camera of the game, turned a little by the stack
	const FVector3d South = (-FVector3d::UpVector + Up * Up.Z).GetSafeNormal();
	const FVector3d East = FVector3d::CrossProduct(Up, South).GetSafeNormal();
	// more figures for more creatures
	const int32 NumFigures = FMath::Clamp(Neutral.Stack.Count >= 50 ? 3 : Neutral.Stack.Count >= 10 ? 2 : 1, 1, FMath::Max(Config->NeutralMaxFigures, 1));
	const double Step = Cell.Neighbors.IsEmpty() ? 20.0 : FVector3d::Distance(Cell.Center, Planet->GetGrid().GetCells()[Cell.Neighbors[0]].Center) * Planet->GetRadius() * 0.18;
	const FVector2D Offsets[3] = {FVector2D(0.0, 0.0), FVector2D(-0.8, 0.7), FVector2D(0.8, 0.7)};
	const FLinearColor Neutral3(0.55f, 0.55f, 0.55f, 1.0f);
	for (int32 Index = 0; Index < NumFigures; ++Index)
	{
		float Scale = 1.0f;
		float Yaw = 0.0f;
		float Hover = 0.0f;
		USceneComponent* Figure = HexUnitFigure::Make(Planet, Planet->GetRootComponent(), Config, Castles->Factions[Neutral.Stack.Faction].Creatures[Neutral.Stack.Creature].Id,
			Neutral.Stack.bUpgraded, Neutral.Stack.Creature, Neutral3, Config->NeutralScale * (Index == 0 ? 1.0f : 0.85f), Scale, Yaw, Hover);
		if (!Figure)
		{
			continue;
		}
		const FVector3d Along = (South * Offsets[Index].Y + East * Offsets[Index].X) * (NumFigures > 1 ? Step : 0.0);
		const FVector3d Direction = (Up * Planet->GetRadius() + Along).GetSafeNormal();
		const double Ground = Planet->FindGroundRadius(Neutral.Cell, Direction);
		const FVector3d Point = Direction * (Ground + Hover);
		const FQuat Turn = FRotator(0.0, Yaw + float((Neutral.Id * 37 + Index * 53) % 40) - 20.0f, 0.0).Quaternion();
		Figure->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZX(FVector(Up), FVector(South)).ToQuat() * Turn, FVector(Point), FVector(Scale)));
		Figure->RegisterComponent();
		HexUnitFigure::Play(Figure, Config, Castles->Factions[Neutral.Stack.Faction].Creatures[Neutral.Stack.Creature].Id, EHexFigureAction::Idle,
			0.85f + 0.1f * float(Index));
		Body.Figures.Add(Figure);
	}
}

void UHexNeutrals::Update()
{
	if (!Heroes)
	{
		return;
	}
	const int32 Player = Heroes->GetShroudPlayer();
	for (const FHexNeutral& Neutral : Neutrals)
	{
		const AHexPlanet* Planet = Heroes->GetLayerPlanet(Neutral.Layer);
		if (!Planet || !Planet->IsBuilt())
		{
			continue;
		}
		const FBody* Body = Bodies.Find(Neutral.Id);
		if (!Body || Body->Cell != Neutral.Cell || Body->Layer != Neutral.Layer || (Body->Count >= 10) != (Neutral.Stack.Count >= 10)
			|| (Body->Count >= 50) != (Neutral.Stack.Count >= 50))
		{
			PlaceBody(Neutral);
			Body = Bodies.Find(Neutral.Id);
		}
		// nothing under the fog of war is drawn
		const bool bShown = !Heroes->IsHidden(Player, Neutral.Layer, Neutral.Cell);
		if (Body && Body->bShown != bShown)
		{
			for (const TWeakObjectPtr<USceneComponent>& Figure : Body->Figures)
			{
				if (Figure.IsValid())
				{
					Figure->SetVisibility(bShown, true);
				}
			}
			Bodies[Neutral.Id].bShown = bShown;
		}
	}
}

int32 UHexNeutrals::Generate(FString& OutReport, const int32* Seed)
{
	if (!Heroes || !Config || !Castles || Castles->Factions.IsEmpty())
	{
		OutReport = TEXT("nothing to generate with");
		return 0;
	}
	Clear();
	const FHexNeutralGeneration& Rules = Config->NeutralGeneration;
	FRandomStream Random(Seed ? *Seed : int32(FPlatformTime::Cycles() & 0x7fffffff));
	int32 Placed = 0;
	int32 Guards = 0;
	for (const EHexPlanetLayer Layer : {EHexPlanetLayer::Surface, EHexPlanetLayer::Underground})
	{
		const AHexPlanet* Planet = Heroes->GetLayerPlanet(Layer);
		if (!Planet || Planet->GetGrid().GetCells().IsEmpty() || (Layer == EHexPlanetLayer::Underground && !Planet->IsDataLoaded()))
		{
			continue;
		}
		const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
		// how far each open cell lies in steps from where the heroes and castles start
		TArray<int32> Steps;
		Steps.Init(INDEX_NONE, Cells.Num());
		TArray<int32> Front;
		const auto Seed0 = [&](int32 Cell)
		{
			if (Cells.IsValidIndex(Cell) && Steps[Cell] == INDEX_NONE)
			{
				Steps[Cell] = 0;
				Front.Add(Cell);
			}
		};
		for (const FHexHero& Hero : Heroes->GetHeroes())
		{
			if (Hero.StartLayer == Layer)
			{
				Seed0(Hero.StartCell);
			}
		}
		if (const UHexCastles* AllCastles = Heroes->GetCastles())
		{
			for (const FHexCastle& Castle : AllCastles->GetCastles())
			{
				if (Castle.Layer == Layer && Castle.StartOwner >= 0)
				{
					for (const int32 Cell : Castle.Cells)
					{
						for (const int32 Neighbor : Cells[Cell].Neighbors)
						{
							Seed0(Neighbor);
						}
					}
				}
			}
		}
		for (int32 Index = 0; Index < Front.Num(); ++Index)
		{
			for (const int32 Neighbor : Cells[Front[Index]].Neighbors)
			{
				if (Steps[Neighbor] == INDEX_NONE && Heroes->IsOpenGround(Layer, Neighbor))
				{
					Steps[Neighbor] = Steps[Front[Index]] + 1;
					Front.Add(Neighbor);
				}
			}
		}
		TArray<int32> Taken;
		// Spacing cells apart, by the distance between the middles of neighbors
		const double CellStep = Cells[0].Neighbors.IsEmpty() ? 0.01 : FVector3d::Distance(Cells[0].Center, Cells[Cells[0].Neighbors[0]].Center);
		const auto FarFromTaken = [&](int32 Cell)
		{
			for (const int32 Other : Taken)
			{
				if (FVector3d::Distance(Cells[Other].Center, Cells[Cell].Center) < CellStep * (Rules.Spacing - 0.5))
				{
					return false;
				}
			}
			return true;
		};
		const auto PlaceAt = [&](int32 Cell)
		{
			const int32 Away = Steps[Cell];
			if (Away != INDEX_NONE && Away < Rules.StartClearSteps)
			{
				return false;
			}
			const int32 Tier = FMath::Clamp((Away == INDEX_NONE ? Rules.MaxTier * Rules.TierSteps : Away) / FMath::Max(Rules.TierSteps, 1) + (Layer == EHexPlanetLayer::Underground ? 1 : 0),
				0, Rules.MaxTier);
			const int32 Faction = Random.RandRange(0, Castles->Factions.Num() - 1);
			const int32 MaxLevel = FMath::Min(Castles->Factions[Faction].Creatures.Num() - 1, NumCreatureLevels - 1);
			const int32 Level = FMath::Clamp(Tier + Random.RandRange(0, 1), 0, MaxLevel);
			const FHexCreature& Creature = Castles->Factions[Faction].Creatures[Level];
			const float Weeks = (Rules.WeeksBase + Rules.WeeksPerTier * Tier) * (1.0f + Random.FRandRange(-Rules.WeeksSpread, Rules.WeeksSpread));
			const FHexStack Stack{Faction, Level, Random.FRand() < Rules.UpgradedPerTier * Tier, FMath::Max(FMath::RoundToInt32(Creature.Growth * Weeks), 1)};
			FString Problem;
			if (Add(Layer, Cell, Stack, Problem) == 0)
			{
				return false;
			}
			Taken.Add(Cell);
			++Placed;
			return true;
		};
		// beside the mines and mills, on the cell before them that is the furthest from where the heroes start, so it stands in the way
		if (const UHexSites* Sites = Heroes->GetSites())
		{
			for (const FHexSite& Site : Sites->GetSites())
			{
				if (Site.Layer != Layer || Sites->FindBlockerAt(Layer, Site.Cell) != &Site || Random.FRand() >= Rules.MineShare)
				{
					continue;
				}
				int32 Best = INDEX_NONE;
				for (const int32 Neighbor : Cells[Site.Cell].Neighbors)
				{
					if (IsFree(Layer, Neighbor) && (Best == INDEX_NONE || Steps[Neighbor] < Steps[Best]))
					{
						Best = Neighbor;
					}
				}
				Guards += Best != INDEX_NONE && PlaceAt(Best) ? 1 : 0;
			}
		}
		// and in the narrow places of the ways: an open cell whose open neighbors lie in two runs or more, which it would part
		int32 Open = 0;
		TArray<int32> Narrow;
		TArray<int32> Anywhere;
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			if (!Heroes->IsOpenGround(Layer, Cell))
			{
				continue;
			}
			++Open;
			if (IsFree(Layer, Cell))
			{
				Anywhere.Add(Cell);
			}
			const auto& Around = Cells[Cell].Neighbors;
			int32 Runs = 0;
			for (int32 Index = 0; Index < Around.Num(); ++Index)
			{
				const bool bHere = Heroes->IsOpenGround(Layer, Around[Index]);
				const bool bBefore = Heroes->IsOpenGround(Layer, Around[(Index + Around.Num() - 1) % Around.Num()]);
				Runs += bHere && !bBefore ? 1 : 0;
			}
			if (Runs >= 2 && IsFree(Layer, Cell))
			{
				Narrow.Add(Cell);
			}
		}
		for (int32 Index = Narrow.Num() - 1; Index > 0; --Index)
		{
			Narrow.Swap(Index, Random.RandRange(0, Index));
		}
		const float Density = Layer == EHexPlanetLayer::Underground ? Rules.UndergroundDensity : 1.0f;
		const int32 Wanted = FMath::RoundToInt32(Open * Rules.PassesPerThousand * Density / 1000.0f);
		int32 Passes = 0;
		for (const int32 Cell : Narrow)
		{
			if (Passes >= Wanted)
			{
				break;
			}
			if (FarFromTaken(Cell) && PlaceAt(Cell))
			{
				++Passes;
			}
		}
		// and on open ground anywhere
		for (int32 Index = Anywhere.Num() - 1; Index > 0; --Index)
		{
			Anywhere.Swap(Index, Random.RandRange(0, Index));
		}
		const int32 WantedOpen = FMath::RoundToInt32(Open * Rules.OpenPerThousand * Density / 1000.0f);
		int32 Roaming = 0;
		for (const int32 Cell : Anywhere)
		{
			if (Roaming >= WantedOpen)
			{
				break;
			}
			if (IsFree(Layer, Cell) && FarFromTaken(Cell) && PlaceAt(Cell))
			{
				++Roaming;
			}
		}
		UE_LOG(LogHexHero, Log, TEXT("Neutrals: the %s has %d open cells, %d stacks in narrow places of %d wanted, %d on open ground of %d wanted"), GetPlanetLayerName(Layer), Open, Passes,
			Wanted, Roaming, WantedOpen);
	}
	OutReport = FString::Printf(TEXT("%d neutral stacks, %d of them beside mines and mills"), Placed, Guards);
	UE_LOG(LogHexHero, Log, TEXT("Neutrals: generated %s"), *OutReport);
	return Placed;
}

namespace
{
	void RunNeutralCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		AHexPlanet* Planet = Layers ? Layers->GetPresentedBuilt() : nullptr;
		UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
		UHexNeutrals* Neutrals = Heroes ? Heroes->GetNeutrals() : nullptr;
		const UHexCastleConfig* Castles = UHexCastleConfig::Load();
		if (!Neutrals || Args.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Neutral <list|add <Cell> <Faction> <Level 1-7> <Count> [up]|remove <Cell>|generate [Seed]|clear>, on a built planet; a cell is its number or near:<Steps>[:<Side>] from the hero chosen"));
			return;
		}
		const EHexPlanetLayer Layer = Planet->GetLayer();
		const auto ParseCell = [&](const FString& Text)
		{
			if (!Text.StartsWith(TEXT("near"), ESearchCase::IgnoreCase))
			{
				return Text.IsNumeric() ? FCString::Atoi(*Text) : int32(INDEX_NONE);
			}
			const FHexHero* Hero = Heroes->GetSelected() ? Heroes->GetSelected() : (Heroes->GetHeroes().IsEmpty() ? nullptr : &Heroes->GetHeroes()[0]);
			const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT(":"));
			int32 Cell = Hero ? Hero->Cell : INDEX_NONE;
			const int32 Steps = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 1;
			const int32 Side = Parts.IsValidIndex(2) ? FCString::Atoi(*Parts[2]) : 0;
			for (int32 Step = 0; Step < Steps && Cells.IsValidIndex(Cell); ++Step)
			{
				Cell = Cells[Cell].Neighbors[FMath::Abs(Side) % Cells[Cell].Neighbors.Num()];
			}
			return Cell;
		};
		if (Args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogHexHero, Log, TEXT("Neutrals: %d on the planet"), Neutrals->GetNeutrals().Num());
			for (const FHexNeutral& Neutral : Neutrals->GetNeutrals())
			{
				UE_LOG(LogHexHero, Log, TEXT("  %d: %d %s%s, cell %d of the %s"), Neutral.Id, Neutral.Stack.Count, *Neutrals->GetCreatureName(Neutral.Stack),
					Neutral.Stack.bUpgraded ? TEXT(" (upgraded)") : TEXT(""), Neutral.Cell, GetPlanetLayerName(Neutral.Layer));
			}
		}
		else if (Args[0].Equals(TEXT("add"), ESearchCase::IgnoreCase) && Args.Num() >= 5)
		{
			const int32 Faction = Castles->FindFaction(Args[2]);
			const FHexStack Stack{Faction, FCString::Atoi(*Args[3]) - 1, Args.IsValidIndex(5) && Args[5].Equals(TEXT("up"), ESearchCase::IgnoreCase), FCString::Atoi(*Args[4])};
			FString Problem;
			const int32 Cell = ParseCell(Args[1]);
			if (Faction == INDEX_NONE || Neutrals->Add(Layer, Cell, Stack, Problem) == 0)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Neutral: no stack on cell %d: %s"), Cell, Faction == INDEX_NONE ? TEXT("no such faction") : *Problem);
			}
			else
			{
				UE_LOG(LogHexHero, Log, TEXT("hex.Neutral: %d %s on cell %d"), Stack.Count, *Neutrals->GetCreatureName(Stack), Cell);
			}
		}
		else if (Args[0].Equals(TEXT("remove"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			Neutrals->RemoveAt(Layer, ParseCell(Args[1]));
		}
		else if (Args[0].Equals(TEXT("generate"), ESearchCase::IgnoreCase))
		{
			FString Report;
			const int32 Seed = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 0;
			Neutrals->Generate(Report, Args.IsValidIndex(1) ? &Seed : nullptr);
			UE_LOG(LogHexHero, Log, TEXT("hex.Neutral: %s"), *Report);
		}
		else if (Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			Neutrals->Clear();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs NeutralCommand(TEXT("hex.Neutral"),
		TEXT("The neutral stacks of the planet: list, add <Cell> <Faction> <Level 1-7> <Count> [up] on the layer shown, remove <Cell>, generate [Seed] (beside mines and in narrow places of both layers, by NeutralGeneration of DA_HexBattle), clear. A cell is its number, or near:<Steps>[:<Side>] from the hero chosen; a faction is its Id, Dawn or Decay."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNeutralCommand));
}
