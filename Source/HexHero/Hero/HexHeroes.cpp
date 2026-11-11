// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHeroes.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexBiomeConfig.h"
#include "HexPlanet.h"
#include "HexPlanetObjects.h"
#include "HexPlanetRivers.h"
#include "HexPlanetUnderground.h"
#include "HexShroud.h"
#include "HexSites.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

int32 UHexHeroes::MapRevision = 0;

namespace
{
	TAutoConsoleVariable<float> CVarHeroLightShare(TEXT("hex.HeroLightShare"), -1.0f,
		TEXT("How far the light of a hero reaches as a share of the cells of plains it walks in a day, for trying it while the game runs: 1 lights as far as it can go, 0 keeps HeroLightReach of the layer, and a negative value takes LightMoveShare of DA_HexHeroes"));

	TAutoConsoleVariable<int32> CVarPlanetShroud(TEXT("hex.PlanetShroud"), 1,
		TEXT("0 lifts the fog of war while the game is played, so everything shows and a hero finds ways over cells its player has not explored; 1 keeps it (bShroud of DA_HexHeroes)"));

	EHexPlanetLayer GetOtherLayer(EHexPlanetLayer Layer)
	{
		return Layer == EHexPlanetLayer::Surface ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface;
	}

	/** Returns the direction a hero faces at a point when nothing says otherwise: towards the south, where the camera mostly looks from,
	 *  turned a little by its Id so heroes side by side do not stand as a row of copies */
	FVector3d GetRestFacing(const FVector3d& Up, int32 Id)
	{
		FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
		if (!East.Normalize())
		{
			East = FVector3d::UnitX();
		}
		const FVector3d North = FVector3d::CrossProduct(Up, East);
		const double Angle = FMath::DegreesToRadians(double((Id * 47) % 81) - 40.0);
		return (-North * FMath::Cos(Angle) + East * FMath::Sin(Angle)).GetSafeNormal();
	}

	/** Returns a direction laid onto the ground at a point, or zero if it points along the up there */
	FVector3d ProjectToGround(const FVector3d& Direction, const FVector3d& Up)
	{
		return (Direction - Up * FVector3d::DotProduct(Direction, Up)).GetSafeNormal();
	}

	/** Returns the transform of a flat mark of radius 1 lying on the ground at a point, its Z up */
	FTransform GetMarkTransform(const FVector3d& Point, const FVector3d& Up, double MarkRadius)
	{
		return FTransform(FRotationMatrix::MakeFromZ(FVector(Up)).ToQuat(), FVector(Point), FVector(MarkRadius));
	}
}

AHexPlanet* UHexHeroes::GetPlanet(EHexPlanetLayer Layer) const
{
	return Surface ? Surface->GetLayerPlanet(Layer) : nullptr;
}

const FHexHero* UHexHeroes::FindHero(int32 Id) const
{
	return Id > 0 ? Heroes.FindByPredicate([Id](const FHexHero& Each) { return Each.Id == Id; }) : nullptr;
}

FHexHero* UHexHeroes::FindHeroMutable(int32 Id)
{
	return Id > 0 ? Heroes.FindByPredicate([Id](const FHexHero& Each) { return Each.Id == Id; }) : nullptr;
}

const FHexHero* UHexHeroes::FindHeroByText(const FString& Text) const
{
	if (Text.IsNumeric())
	{
		return FindHero(FCString::Atoi(*Text));
	}
	return Heroes.FindByPredicate([&Text](const FHexHero& Each) { return Each.Name.Equals(Text, ESearchCase::IgnoreCase); });
}

const FHexHero* UHexHeroes::FindHeroAt(EHexPlanetLayer Layer, int32 Cell) const
{
	return Heroes.FindByPredicate([Layer, Cell](const FHexHero& Each) { return Each.Layer == Layer && Each.Cell == Cell; });
}

const FHexHero* UHexHeroes::FindHeroAlongRay(EHexPlanetLayer Layer, const FVector3d& Origin, const FVector3d& Direction) const
{
	const FHexHero* Best = nullptr;
	double BestAlong = TNumericLimits<double>::Max();
	const FVector3d Ray = Direction.GetSafeNormal();
	for (const FHexHero& Hero : Heroes)
	{
		if (Hero.Layer != Layer || !Hero.bPlaced)
		{
			continue;
		}
		const double Scale = double(Config->HeroScale) * Hero.ViewScale;
		// the body as a segment from its feet to its head
		const FVector3d Up = Hero.Position.GetSafeNormal();
		FVector3d OnRay;
		FVector3d OnBody;
		FMath::SegmentDistToSegmentSafe(Origin, Origin + Ray * 1.0e7, Hero.Position, Hero.Position + Up * Config->PickHeight * Scale, OnRay, OnBody);
		const double Along = FVector3d::DotProduct(OnRay - Origin, Ray);
		if (FVector3d::Distance(OnRay, OnBody) <= Config->PickRadius * Scale && Along < BestAlong)
		{
			Best = &Hero;
			BestAlong = Along;
		}
	}
	return Best;
}

float UHexHeroes::GetMaxMovePoints(const FHexHero& Hero) const
{
	return Config ? Config->MovePoints : 1.0f;
}

void UHexHeroes::ResetHero(FHexHero& Hero) const
{
	const FHexHeroClass& Class = Config->GetClass(Hero.Class);
	Hero.Attack = Class.Attack;
	Hero.Defense = Class.Defense;
	Hero.SpellPower = Class.SpellPower;
	Hero.Knowledge = Class.Knowledge;
	Hero.Mana = Class.Knowledge * Config->ManaPerKnowledge;
	Hero.Level = 1;
	Hero.Experience = 0;
	Hero.MovePoints = GetMaxMovePoints(Hero);
	Hero.Layer = Hero.StartLayer;
	Hero.Cell = Hero.StartCell;
	Hero.FromCell = Hero.StartCell;
	Hero.Route.Reset();
	Hero.bWalking = false;
	Hero.bEnterAtEnd = false;
	Hero.TargetCell = INDEX_NONE;
	Hero.VisitCell = INDEX_NONE;
	Hero.bOutOfPoints = false;
	Hero.StepAlpha = 0.0f;
	Hero.Speed = 0.0f;
	Hero.bAiDone = false;
	Hero.AiWait = Config->AiThinkTime;
	Hero.bPlaced = false;
	Hero.Facing = FVector3d::ZeroVector;
	Hero.EntranceSide = FVector3d::ZeroVector;
}

FString UHexHeroes::MakeName(int32 Class, int32 Id) const
{
	const FHexHeroClass& Each = Config->GetClass(Class);
	for (const FString& Name : Each.Names)
	{
		if (!Heroes.ContainsByPredicate([&Name](const FHexHero& Hero) { return Hero.Name.Equals(Name, ESearchCase::IgnoreCase); }))
		{
			return Name;
		}
	}
	return FString::Printf(TEXT("%s %d"), *Each.DisplayName, Id);
}

namespace
{
	/** Returns lines of heroes.json in the order of their Ids, to compare two sets of them */
	TArray<FHexHeroLine> SortLines(TArray<FHexHeroLine> Lines)
	{
		Lines.Sort([](const FHexHeroLine& A, const FHexHeroLine& B) { return A.Id < B.Id; });
		return Lines;
	}
}

void UHexHeroes::Init(AHexPlanet* InSurface)
{
	Surface = InSurface;
	Config = UHexHeroConfig::Load();
	Heroes.Reset();
	SavedLines.Reset();
	Sites = NewObject<UHexSites>(this);
	Sites->Init(InSurface, Config->Players.Num());
	Shroud = NewObject<UHexShroud>(this);
	Day = 1;
	NextId = 1;
	bEdited = false;
	AiPlayers.Reset();
	SavedAiPlayers.Reset();

	if (!Surface || !Surface->IsSaved() || !HexPlanetFile::HasHeroes(Surface->GetPlanetFolder()))
	{
		return;
	}
	TArray<FHexHeroLine> Lines;
	FString Error;
	if (!HexPlanetFile::LoadHeroes(Surface->GetPlanetFolder(), Lines, Error, &AiPlayers))
	{
		UE_LOG(LogHexHero, Error, TEXT("Heroes: %s"), *Error);
		return;
	}
	AiPlayers.RemoveAll([this](int32 Player) { return !Config->Players.IsValidIndex(Player); });
	SavedAiPlayers = AiPlayers;
	SetLines(Lines);
	// what the file holds; a line that was left out leaves the planet unsaved, so the next save writes the file without it
	SavedLines = SortLines(Lines);
	UpdateEdited();
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %d on the planet, %.0f points of movement a day"), Heroes.Num(), Config->MovePoints);
}

void UHexHeroes::SetLines(const TArray<FHexHeroLine>& Lines)
{
	const int32 NumCells = Surface ? Surface->GetGrid().GetCells().Num() : 0;
	const TArray<FHexHeroLine> Before = MakeLines();
	TArray<FHexHero> Kept;
	for (const FHexHeroLine& Line : Lines)
	{
		// a hero whose line is the same stays as it is, where it walked to
		const int32 Same = Before.IndexOfByKey(Line);
		if (Same != INDEX_NONE && !Kept.ContainsByPredicate([&Line](const FHexHero& Each) { return Each.Id == Line.Id; }))
		{
			Kept.Add(Heroes[Same]);
			continue;
		}
		FHexHero Hero;
		Hero.Id = Line.Id;
		Hero.Name = Line.Name;
		Hero.Class = Config->FindClass(Line.Class);
		Hero.Player = FMath::Clamp(Line.Player, 0, Config->Players.Num() - 1);
		Hero.StartCell = Line.Cell;
		if (Hero.Id <= 0 || Kept.ContainsByPredicate([&Line](const FHexHero& Each) { return Each.Id == Line.Id; }) || Line.Cell < 0 || Line.Cell >= NumCells
			|| !ParsePlanetLayer(Line.Layer, Hero.StartLayer))
		{
			UE_LOG(LogHexHero, Error, TEXT("Heroes: the hero %d (%s) of heroes.json has no Id of its own, cell or layer, and is left out"), Line.Id, *Line.Name);
			continue;
		}
		if (Hero.Class == INDEX_NONE)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Heroes: %s has the class %s, which DA_HexHeroes does not have, and takes the first"), *Line.Name, *Line.Class);
			Hero.Class = 0;
		}
		ResetHero(Hero);
		Kept.Add(MoveTemp(Hero));
	}
	// the meshes of the heroes that go or start anew
	for (const FHexHero& Hero : Heroes)
	{
		const FHexHero* Still = Kept.FindByPredicate([&Hero](const FHexHero& Each) { return Each.Id == Hero.Id; });
		if (!Still || !Still->bPlaced)
		{
			RemoveBody(Hero.Id);
		}
	}
	Heroes = MoveTemp(Kept);
	for (const FHexHero& Hero : Heroes)
	{
		NextId = FMath::Max(NextId, Hero.Id + 1);
	}
	UpdateEdited();
	++ListRevision;
	++MapRevision;
	KeepChosen();
}

TArray<FHexHeroLine> UHexHeroes::MakeLines() const
{
	TArray<FHexHeroLine> Lines;
	for (const FHexHero& Hero : Heroes)
	{
		FHexHeroLine& Line = Lines.AddDefaulted_GetRef();
		Line.Id = Hero.Id;
		Line.Name = Hero.Name;
		Line.Class = Config->GetClass(Hero.Class).Id.ToString();
		Line.Player = Hero.Player;
		Line.Layer = GetPlanetLayerName(Hero.StartLayer);
		Line.Cell = Hero.StartCell;
	}
	return Lines;
}

bool UHexHeroes::Save(const FString& PlanetFolder, FString& OutError)
{
	if (!HexPlanetFile::SaveHeroes(PlanetFolder, MakeLines(), OutError, AiPlayers) || (Sites && !Sites->Save(PlanetFolder, OutError)))
	{
		return false;
	}
	SavedLines = SortLines(MakeLines());
	SavedAiPlayers = AiPlayers;
	bEdited = false;
	return true;
}

void UHexHeroes::UpdateEdited()
{
	bEdited = SortLines(MakeLines()) != SavedLines || AiPlayers != SavedAiPlayers;
}

void UHexHeroes::SetPlayerAi(int32 Player, bool bAi)
{
	if (!Config || !Config->Players.IsValidIndex(Player) || bAi == IsPlayerAi(Player))
	{
		return;
	}
	if (bAi)
	{
		AiPlayers.Add(Player);
		AiPlayers.Sort();
	}
	else
	{
		AiPlayers.Remove(Player);
	}
	for (FHexHero& Hero : Heroes)
	{
		if (Hero.Player == Player)
		{
			Hero.bAiDone = false;
			Hero.AiWait = 0.0f;
		}
	}
	UpdateEdited();
	++ListRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s is moved by %s"), *Config->GetPlayer(Player).Name, bAi ? TEXT("the game") : TEXT("a person"));
	// its turn goes on with who moves it now
	if (bPlaying && Player == TurnPlayer)
	{
		BeginTurn(Player);
	}
}

bool UHexHeroes::IsEdited() const
{
	return bEdited || (Sites && Sites->IsEdited());
}

bool UHexHeroes::IsShroudOn() const
{
	return bPlaying && Config && Config->bShroud && Shroud && CVarPlanetShroud.GetValueOnGameThread() != 0;
}

int32 UHexHeroes::GetShroudPlayer() const
{
	if (bPlaying)
	{
		return ViewPlayer;
	}
	const FHexHero* Hero = GetSelected();
	return Hero ? Hero->Player : 0;
}

bool UHexHeroes::CanChoose(int32 Id) const
{
	const FHexHero* Hero = FindHero(Id);
	return Hero && (!bPlaying || (IsPersonTurn() && Hero->Player == TurnPlayer));
}

bool UHexHeroes::IsCellSeen(EHexPlanetLayer Layer, int32 Cell) const
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	return Planet && Planet->IsPresented() && !IsHidden(ViewPlayer, Layer, Cell);
}

bool UHexHeroes::IsStepSeen(const FHexHero& Hero) const
{
	return IsCellSeen(Hero.Layer, Hero.Cell) || IsCellSeen(Hero.Layer, Hero.FromCell);
}

bool UHexHeroes::IsSeen(const FHexHero& Hero) const
{
	return Hero.bWalking ? IsStepSeen(Hero) : IsCellSeen(Hero.Layer, Hero.Cell);
}

TArray<int32> UHexHeroes::GetActivePlayers() const
{
	TArray<int32> Players;
	for (const FHexHero& Hero : Heroes)
	{
		Players.AddUnique(Hero.Player);
	}
	Players.Sort();
	return Players;
}

void UHexHeroes::BeginTurn(int32 Player)
{
	TurnPlayer = Player;
	PendingNextFrom = 0;
	AiHeroId = 0;
	WatchHold = 0.0f;
	for (FHexHero& Hero : Heroes)
	{
		if (Hero.Player == Player)
		{
			Hero.bAiDone = false;
			Hero.AiWait = Config->AiThinkTime;
			Hero.bWasSeen = IsSeen(Hero);
		}
	}
	const FString& Name = Config->GetPlayer(Player).Name;
	// the panel lists the heroes of who plays
	++ListRevision;
	if (IsPlayerAi(Player))
	{
		// with no person in the game the players it moves take their turns a second apart
		AiTurnWait = Heroes.ContainsByPredicate([this](const FHexHero& Each) { return !IsPlayerAi(Each.Player); }) ? 0.0f : 1.0f;
		UE_LOG(LogHexHero, Log, TEXT("Heroes: day %d, %s moves, by the game"), Day, *Name);
		OnMessage.Broadcast(FString::Printf(TEXT("%s moves"), *Name));
		return;
	}
	// a person sees its own fog of war and has a hero of its own chosen: the one it had if it is still its, else its first that can move
	ViewPlayer = Player;
	const FHexHero* Chosen = GetSelected();
	if (!Chosen || Chosen->Player != Player)
	{
		const FHexHero* First = nullptr;
		const FHexHero* Able = nullptr;
		for (const FHexHero& Hero : Heroes)
		{
			if (Hero.Player != Player)
			{
				continue;
			}
			if (!First || Hero.Id < First->Id)
			{
				First = &Hero;
			}
			if (CanMove(Hero) && (!Able || Hero.Id < Able->Id))
			{
				Able = &Hero;
			}
		}
		SelectedId = Able ? Able->Id : First ? First->Id : 0;
	}
	UE_LOG(LogHexHero, Log, TEXT("Heroes: day %d, %s plays"), Day, *Name);
	OnMessage.Broadcast(FString::Printf(TEXT("Day %d: %s plays"), Day, *Name));
	OnHeroChosen.Broadcast(SelectedId);
}

void UHexHeroes::BeginDay()
{
	++Day;
	for (FHexHero& Hero : Heroes)
	{
		Hero.MovePoints = GetMaxMovePoints(Hero);
	}
	// the mines give their owners their resources
	if (Sites)
	{
		Sites->EndDay();
	}
	++MapRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: day %d begins"), Day);
}

bool UHexHeroes::IsHidden(int32 Player, EHexPlanetLayer Layer, int32 Cell) const
{
	return IsShroudOn() && !Shroud->IsExplored(Player, Layer, Cell);
}

void UHexHeroes::Explore(const FHexHero& Hero)
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet || !Shroud || !Config || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
	{
		return;
	}
	TArray<int32> Seen;
	HexShroud::FindWithin(Planet->GetGrid(), Hero.Cell, Config->SightRadius, Seen);
	// underground only the passages open, as nobody walks the voids; the planet shows a strip of the void beside them (ShroudVoidMargin)
	if (Hero.Layer == EHexPlanetLayer::Underground)
	{
		Seen.RemoveAll([Planet](int32 Cell) { return !HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Cell))); });
	}
	if (Shroud->Explore(Hero.Player, Hero.Layer, Seen, Planet->GetGrid().GetCells().Num()))
	{
		// a hero may find a way over what it has just seen
		++MapRevision;
	}
}

void UHexHeroes::ExploreAll()
{
	for (const FHexHero& Hero : Heroes)
	{
		Explore(Hero);
	}
}

bool UHexHeroes::IsUsableEntrance(const AHexPlanet& Planet, int32 Cell)
{
	const int32 Entrance = Planet.FindEntrance(Cell);
	return Entrance != INDEX_NONE && Planet.GetEntranceProblem(Entrance) == HexPlanetUnderground::EEntranceProblem::None;
}

float UHexHeroes::GetCellCost(const AHexPlanet& Planet, int32 Cell, bool bIgnoreSite) const
{
	// a mine, a pile or a mill fills its cell
	if (!bIgnoreSite && Sites && Sites->FindSiteAt(Planet.GetLayer(), Cell))
	{
		return -1.0f;
	}
	const int32 Level = Planet.GetCellLevel(Cell);
	const bool bUnderground = Planet.GetLayer() == EHexPlanetLayer::Underground;
	// the passages of the underground, and the plains and hills of the surface: the sea waits for ships, and mountains are walls
	if (bUnderground ? !HexPlanetUnderground::IsPassage(int8(Level)) : (Level < 0 || Level > 1))
	{
		return -1.0f;
	}
	const TArray<TObjectPtr<UHexBiomeConfig>>& Biomes = Planet.GetBiomes();
	const UHexBiomeConfig* Biome = Biomes.IsValidIndex(Planet.GetCellBiome(Cell)) ? Biomes[Planet.GetCellBiome(Cell)].Get() : nullptr;
	if (Biome && Biome->bBlocksHeroes)
	{
		return -1.0f;
	}
	if (Planet.IsBuilt() && Planet.GetObjects() && Planet.GetObjects()->BlocksMovement(Cell))
	{
		return -1.0f;
	}
	return (Level == 1 && !bUnderground ? Config->HillCost : 1.0f) * (Biome ? FMath::Max(Biome->MoveCostMultiplier, 0.01f) : 1.0f);
}

float UHexHeroes::GetStepCost(const AHexPlanet& Planet, const FHexHero* Hero, int32 From, int32 To) const
{
	const float Cost = GetCellCost(Planet, To);
	if (Cost < 0.0f)
	{
		return -1.0f;
	}
	// one hero to a cell
	const EHexPlanetLayer Layer = Planet.GetLayer();
	for (const FHexHero& Other : Heroes)
	{
		if (Other.Layer == Layer && Other.Cell == To && (!Hero || Other.Id != Hero->Id))
		{
			return -1.0f;
		}
	}
	const UHexPlanetRivers* Rivers = Layer == EHexPlanetLayer::Surface ? Planet.GetRivers() : nullptr;
	return Cost * (Rivers ? Rivers->GetCrossingFactor(From, To) : 1.0f);
}

bool UHexHeroes::CanStand(EHexPlanetLayer Layer, int32 Cell, int32 IgnoreHero, FString* OutProblem) const
{
	const auto Fail = [OutProblem](const TCHAR* Problem)
	{
		if (OutProblem)
		{
			*OutProblem = Problem;
		}
		return false;
	};
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return Fail(TEXT("there is no such cell"));
	}
	// what its player has not explored is unknown ground
	const FHexHero* Mover = FindHero(IgnoreHero);
	if (Mover && IsHidden(Mover->Player, Layer, Cell))
	{
		return Fail(TEXT("unexplored"));
	}
	const FHexHero* Other = FindHeroAt(Layer, Cell);
	if (Other && Other->Id != IgnoreHero)
	{
		if (OutProblem)
		{
			*OutProblem = FString::Printf(TEXT("%s stands there"), *Other->Name);
		}
		return false;
	}
	if (Sites && Sites->FindSiteAt(Layer, Cell))
	{
		return Fail(TEXT("a mine, a pile or a mill fills that cell"));
	}
	if (GetCellCost(*Planet, Cell) < 0.0f)
	{
		const int32 Level = Planet->GetCellLevel(Cell);
		const bool bGround = Layer == EHexPlanetLayer::Underground ? HexPlanetUnderground::IsPassage(int8(Level)) : Level >= 0 && Level <= 1;
		if (!bGround)
		{
			return Fail(Layer == EHexPlanetLayer::Underground ? TEXT("nothing crosses the void") : Level < 0 ? TEXT("a hero does not cross the sea yet") : TEXT("mountains cannot be crossed"));
		}
		// what stands on the cell, by its name, as a kind that blocks may look as if one could walk through it
		const FString Blocker = Planet->GetObjects() ? Planet->GetObjects()->GetBlockerName(Cell) : FString();
		if (OutProblem)
		{
			*OutProblem = Blocker.IsEmpty() ? FString(TEXT("the ground there cannot be crossed")) : FString::Printf(TEXT("%s in the way"), *Blocker.ToLower());
		}
		return false;
	}
	return true;
}

int32 UHexHeroes::AddHero(EHexPlanetLayer Layer, int32 Cell, int32 Class, int32 Player, FString& OutProblem)
{
	if (!Config || !CanStand(Layer, Cell, 0, &OutProblem))
	{
		return 0;
	}
	FHexHero Hero;
	Hero.Id = NextId++;
	Hero.Class = Config->Classes.IsValidIndex(Class) ? Class : 0;
	Hero.Player = FMath::Clamp(Player, 0, Config->Players.Num() - 1);
	Hero.Name = MakeName(Hero.Class, Hero.Id);
	Hero.StartLayer = Layer;
	Hero.StartCell = Cell;
	ResetHero(Hero);
	const int32 Id = Hero.Id;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, a %s of %s, stands on cell %d of the %s as hero %d"), *Hero.Name, *Config->GetClass(Hero.Class).DisplayName,
		*Config->GetPlayer(Hero.Player).Name, Cell, GetPlanetLayerName(Layer), Id);
	Heroes.Add(MoveTemp(Hero));
	UpdateEdited();
	++ListRevision;
	++MapRevision;
	// a hero that joins a game in play sees around itself at once
	if (bPlaying)
	{
		Explore(Heroes.Last());
	}
	return Id;
}

bool UHexHeroes::RemoveHero(int32 Id)
{
	const int32 Index = Heroes.IndexOfByPredicate([Id](const FHexHero& Each) { return Each.Id == Id; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s leaves the planet"), *Heroes[Index].Name);
	RemoveBody(Id);
	Heroes.RemoveAt(Index);
	UpdateEdited();
	++ListRevision;
	++MapRevision;
	KeepChosen();
	return true;
}

bool UHexHeroes::Select(int32 Id)
{
	if (Id != 0 && !FindHero(Id))
	{
		return false;
	}
	SelectedId = Id;
	return true;
}

const FHexHero* UHexHeroes::SelectNext()
{
	// the lowest Id above the one chosen, else the lowest of all; while the game is played only of the player whose turn it is
	const FHexHero* Next = nullptr;
	const FHexHero* First = nullptr;
	for (const FHexHero& Hero : Heroes)
	{
		if (bPlaying && Hero.Player != TurnPlayer)
		{
			continue;
		}
		if (!First || Hero.Id < First->Id)
		{
			First = &Hero;
		}
		if (Hero.Id > SelectedId && (!Next || Hero.Id < Next->Id))
		{
			Next = &Hero;
		}
	}
	if (!First)
	{
		return nullptr;
	}
	SelectedId = (Next ? Next : First)->Id;
	return GetSelected();
}

bool UHexHeroes::StartPlay(FString& OutProblem)
{
	if (Heroes.IsEmpty())
	{
		OutProblem = TEXT("The game needs a hero: place one in Edit, on the Heroes tab");
		return false;
	}
	bPlaying = true;
	// every player knows the land around its heroes
	ExploreAll();
	// the players move in turns: the one whose turn it was when the game stopped for editing goes on, else the first; and the fog of war
	// is that of the first person, until a person plays
	const TArray<int32> Active = GetActivePlayers();
	const int32* Person = Active.FindByPredicate([this](int32 Player) { return !IsPlayerAi(Player); });
	ViewPlayer = Person ? *Person : Active[0];
	UE_LOG(LogHexHero, Log, TEXT("Heroes: the game is played, day %d"), Day);
	BeginTurn(Active.Contains(TurnPlayer) ? TurnPlayer : Active[0]);
	return true;
}

void UHexHeroes::StopPlay()
{
	bPlaying = false;
	PendingNextFrom = 0;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: the game stops"));
}

void UHexHeroes::KeepChosen()
{
	if (FindHero(SelectedId))
	{
		return;
	}
	SelectedId = 0;
	if (!bPlaying)
	{
		return;
	}
	// without a hero the game is lost
	if (Heroes.IsEmpty())
	{
		bPlaying = false;
		OnMessage.Broadcast(TEXT("No hero is left: the game is lost"));
		return;
	}
	// a player without heroes has no turn
	if (!Heroes.ContainsByPredicate([this](const FHexHero& Each) { return Each.Player == TurnPlayer; }))
	{
		EndTurn();
		return;
	}
	if (IsPersonTurn())
	{
		SelectNext();
		OnHeroChosen.Broadcast(SelectedId);
	}
}

bool UHexHeroes::CanMove(const FHexHero& Hero) const
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
	{
		return false;
	}
	for (const int32 Neighbor : Planet->GetGrid().GetCells()[Hero.Cell].Neighbors)
	{
		const float Cost = GetStepCost(*Planet, &Hero, Hero.Cell, Neighbor);
		if (Cost >= 0.0f && HexHeroPath::CanStep(Cost, Hero.MovePoints, GetMaxMovePoints(Hero)))
		{
			return true;
		}
	}
	return false;
}

const FHexHero* UHexHeroes::FindNextAble(int32 After) const
{
	// in the order of their Ids, from the one after it round to the one before it
	TArray<const FHexHero*> Sorted;
	for (const FHexHero& Hero : Heroes)
	{
		Sorted.Add(&Hero);
	}
	Sorted.Sort([](const FHexHero& A, const FHexHero& B) { return A.Id < B.Id; });
	const int32 Start = Sorted.IndexOfByPredicate([After](const FHexHero* Each) { return Each->Id > After; });
	for (int32 Step = 0; Step < Sorted.Num(); ++Step)
	{
		const FHexHero* Hero = Sorted[((Start == INDEX_NONE ? 0 : Start) + Step) % Sorted.Num()];
		// of the player whose turn it is
		if (Hero->Id != After && !Hero->bWalking && Hero->Player == TurnPlayer && CanMove(*Hero))
		{
			return Hero;
		}
	}
	return nullptr;
}

UHexHeroes::EClick UHexHeroes::Click(EHexPlanetLayer Layer, int32 Cell, const FHexHero* Under, FString& OutMessage)
{
	// a person plays only on its turn
	if (!IsPersonTurn())
	{
		return EClick::None;
	}
	if (!Under)
	{
		Under = FindHeroAt(Layer, Cell);
	}
	// a hero the fog of war hides is not there to be chosen
	if (Under && Under->Id != SelectedId && IsHidden(GetShroudPlayer(), Under->Layer, Under->Cell))
	{
		Under = nullptr;
	}
	if (Under && Under->Id != SelectedId)
	{
		if (Under->Player == TurnPlayer)
		{
			SelectedId = Under->Id;
			return EClick::Chosen;
		}
		// the hero of another player is not one to choose: its cell is pointed to as any other, on which it stands in the way
		Cell = Under->Cell;
		Under = nullptr;
	}

	FHexHero* Hero = FindHeroMutable(SelectedId);
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	if (!Hero || !Planet)
	{
		return EClick::None;
	}
	// the player goes on with the hero chosen, so the next one is not chosen in its place
	PendingNextFrom = 0;
	// a hero of a player the game moves is chosen only to be watched
	if (IsPlayerAi(Hero->Player))
	{
		OutMessage = FString::Printf(TEXT("%s is moved by the game"), *Hero->Name);
		return EClick::None;
	}
	if (Hero->Layer != Layer)
	{
		OutMessage = FString::Printf(TEXT("%s is on the other layer"), *Hero->Name);
		return EClick::None;
	}
	// a click on the body of the hero chosen is a click on its cell
	if (Under)
	{
		Cell = Hero->Cell;
	}
	if (!Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return EClick::None;
	}
	// any click stops a hero that walks, at the end of the step it is on
	if (Hero->bWalking)
	{
		Hero->Route.Reset();
		Hero->bEnterAtEnd = false;
		Hero->TargetCell = INDEX_NONE;
		Hero->VisitCell = INDEX_NONE;
		return EClick::Stopped;
	}
	if (Cell == Hero->TargetCell)
	{
		return Go(Hero->Id, OutMessage) ? EClick::Sent : EClick::None;
	}
	// its own cell is somewhere to go only through an entrance or exit
	if (Cell == Hero->Cell && !IsUsableEntrance(*Planet, Cell))
	{
		Hero->TargetCell = INDEX_NONE;
		return EClick::None;
	}
	Hero->TargetCell = Cell;
	return EClick::Targeted;
}

bool UHexHeroes::SetTarget(int32 Id, int32 Cell)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero)
	{
		return false;
	}
	Hero->TargetCell = Cell;
	return true;
}

bool UHexHeroes::Go(int32 Id, FString& OutMessage)
{
	const FHexHero* Hero = FindHero(Id);
	if (!Hero || Hero->TargetCell == INDEX_NONE || Hero->bWalking || IsPlayerAi(Hero->Player))
	{
		return false;
	}
	return Send(Id, Hero->TargetCell, OutMessage);
}

void UHexHeroes::FindFieldFor(const FHexHero& Hero, HexHeroPath::FField& OutField) const
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet)
	{
		OutField = HexHeroPath::FField();
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	// the cells other heroes stand on, and the cost of each cell found once, as a cell is looked at from each of its sides
	TSet<int32> Occupied;
	for (const FHexHero& Other : Heroes)
	{
		if (Other.Layer == Hero.Layer && Other.Id != Hero.Id)
		{
			Occupied.Add(Other.Cell);
		}
	}
	TArray<float> CellCosts;
	CellCosts.Init(0.0f, Planet->GetGrid().GetCells().Num());
	const UHexPlanetRivers* Rivers = Hero.Layer == EHexPlanetLayer::Surface ? Planet->GetRivers() : nullptr;
	// a hero finds no way over cells its player has not explored
	const TBitArray<>* Explored = IsShroudOn() ? Shroud->GetExplored(Hero.Player, Hero.Layer) : nullptr;
	const bool bShroud = IsShroudOn();
	const auto GetCost = [&](int32 From, int32 Side, int32 To)
	{
		float& Cost = CellCosts[To];
		if (Cost == 0.0f)
		{
			Cost = Occupied.Contains(To) || (bShroud && !(Explored && Explored->IsValidIndex(To) && (*Explored)[To])) ? -1.0f : GetCellCost(*Planet, To);
		}
		return Cost < 0.0f ? -1.0f : Cost * (Rivers ? Rivers->GetCrossingFactor(From, To) : 1.0f);
	};
	HexHeroPath::FindField(Planet->GetGrid(), Hero.Cell, GetMaxMovePoints(Hero) * Config->PathMaxDays, GetCost, OutField);
	UE_LOG(LogHexHero, Verbose, TEXT("Heroes: the ways of %s from cell %d found in %.2f ms"), *Hero.Name, Hero.Cell, (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

float UHexHeroes::GetVisitCost(const AHexPlanet& Planet, int32 From, int32 SiteCell) const
{
	const UHexPlanetRivers* Rivers = Planet.GetLayer() == EHexPlanetLayer::Surface ? Planet.GetRivers() : nullptr;
	return FMath::Max(GetCellCost(Planet, SiteCell, true), 0.0f) * (Rivers ? Rivers->GetCrossingFactor(From, SiteCell) : 1.0f);
}

bool UHexHeroes::FindWay(const AHexPlanet& Planet, const FHexHero& Hero, const HexHeroPath::FField& Ways, int32 Target, TArray<int32>& OutPath, bool& bOutVisit) const
{
	OutPath.Reset();
	// what stands on ground its player has not explored is unknown, so the cell is only one the hero cannot stand on
	bOutVisit = Sites && Sites->FindSiteAt(Hero.Layer, Target) && !IsHidden(Hero.Player, Hero.Layer, Target);
	if (!bOutVisit)
	{
		return Ways.GetPath(Target, OutPath) && !OutPath.IsEmpty();
	}
	// to the cell beside the site from which the whole way, with the visit, is the cheapest; that may be where the hero stands
	int32 Best = INDEX_NONE;
	float BestCost = 0.0f;
	for (const int32 Neighbor : Planet.GetGrid().GetCells()[Target].Neighbors)
	{
		if (Ways.IsReached(Neighbor))
		{
			const float Cost = Ways.Costs[Neighbor] + GetVisitCost(Planet, Neighbor, Target);
			if (Best == INDEX_NONE || Cost < BestCost)
			{
				Best = Neighbor;
				BestCost = Cost;
			}
		}
	}
	return Best != INDEX_NONE && Ways.GetPath(Best, OutPath);
}

bool UHexHeroes::VisitSite(FHexHero& Hero, FString& OutMessage)
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	const int32 SiteCell = Hero.VisitCell;
	if (!Planet || !Sites || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell) || !Planet->GetGrid().GetCells()[Hero.Cell].Neighbors.Contains(SiteCell)
		|| !Sites->FindSiteAt(Hero.Layer, SiteCell))
	{
		Hero.VisitCell = INDEX_NONE;
		Hero.TargetCell = INDEX_NONE;
		return false;
	}
	// a visit costs what a step onto that cell would
	const float Cost = GetVisitCost(*Planet, Hero.Cell, SiteCell);
	if (!HexHeroPath::CanStep(Cost, Hero.MovePoints, GetMaxMovePoints(Hero)))
	{
		Hero.bOutOfPoints = true;
		OutMessage = FString::Printf(TEXT("%s has no points of movement left to get there today"), *Hero.Name);
		return false;
	}
	Hero.MovePoints = FMath::Max(Hero.MovePoints - Cost, 0.0f);
	// it turns to what it visits
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const FVector3d Towards = ProjectToGround(Cells[SiteCell].Center - Cells[Hero.Cell].Center, Cells[Hero.Cell].Center);
	if (!Towards.IsNearlyZero())
	{
		Hero.Facing = Towards;
		Hero.bPlaced = false;
	}
	const int32 Week = (Day - 1) / FMath::Max(Config->DaysPerWeek, 1) + 1;
	const FString Found = Sites->Visit(Hero.Player, Hero.Name, Hero.Layer, SiteCell, Week);
	if (!Found.IsEmpty())
	{
		OnMessage.Broadcast(Found);
	}
	Hero.VisitCell = INDEX_NONE;
	Hero.TargetCell = INDEX_NONE;
	++MapRevision;
	return true;
}

void UHexHeroes::GetStepCosts(const AHexPlanet& Planet, const FHexHero& Hero, TConstArrayView<int32> Path, TArray<float>& OutCosts) const
{
	OutCosts.Reset(Path.Num());
	int32 From = Hero.Cell;
	for (const int32 To : Path)
	{
		OutCosts.Add(FMath::Max(GetStepCost(Planet, &Hero, From, To), 0.0f));
		From = To;
	}
}

bool UHexHeroes::StartNextStep(FHexHero& Hero)
{
	if (Hero.Route.IsEmpty())
	{
		return false;
	}
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	const int32 Next = Hero.Route[0];
	const float Cost = Planet ? GetStepCost(*Planet, &Hero, Hero.Cell, Next) : -1.0f;
	if (Cost < 0.0f)
	{
		// the map changed or another hero stepped into the way since it was found
		Hero.Route.Reset();
		Hero.bEnterAtEnd = false;
		OnMessage.Broadcast(FString::Printf(TEXT("%s: the way is blocked"), *Hero.Name));
		return false;
	}
	if (!HexHeroPath::CanStep(Cost, Hero.MovePoints, GetMaxMovePoints(Hero)))
	{
		Hero.Route.Reset();
		Hero.bEnterAtEnd = false;
		Hero.bOutOfPoints = true;
		return false;
	}
	Hero.bOutOfPoints = false;
	Hero.MovePoints = FMath::Max(Hero.MovePoints - Cost, 0.0f);
	// it stops beside an entrance on the side it comes from
	if (IsUsableEntrance(*Planet, Next))
	{
		const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
		Hero.EntranceSide = Cells[Hero.Cell].Center - Cells[Next].Center;
	}
	Hero.FromCell = Hero.Cell;
	Hero.Cell = Next;
	Hero.Route.RemoveAt(0);
	++MapRevision;
	Explore(Hero);
	return true;
}

void UHexHeroes::Arrive(FHexHero& Hero)
{
	const bool bEnter = Hero.bEnterAtEnd && Hero.Route.IsEmpty();
	const bool bOutOfPoints = Hero.bOutOfPoints;
	const int32 Id = Hero.Id;
	Hero.bEnterAtEnd = false;
	Hero.bOutOfPoints = false;
	Hero.bPlaced = false;
	if (Hero.Cell == Hero.TargetCell)
	{
		Hero.TargetCell = INDEX_NONE;
	}
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s stops on cell %d of the %s with %.2f points of movement left"), *Hero.Name, Hero.Cell, GetPlanetLayerName(Hero.Layer), Hero.MovePoints);
	bool bTravelled = false;
	bool bNoPoints = bOutOfPoints;
	if (bEnter)
	{
		FString Message;
		bTravelled = Travel(Hero, Message, false);
		if (!bTravelled && !Message.IsEmpty())
		{
			OnMessage.Broadcast(Message);
		}
	}
	else if (Hero.VisitCell != INDEX_NONE && Hero.Route.IsEmpty() && !bOutOfPoints)
	{
		// its way ended beside the site it was sent to
		FString Message;
		if (!VisitSite(Hero, Message) && !Message.IsEmpty())
		{
			OnMessage.Broadcast(Message);
		}
		bNoPoints = Hero.bOutOfPoints;
		Hero.bOutOfPoints = false;
	}

	// the hero chosen ran out of steps: the game goes on with the next that can still move, as the developer asked on 2026-11-11, after a
	// pause in which the player sees that it used its movement and where it stopped (UpdatePendingNext)
	if (IsPersonTurn() && Id == SelectedId && Hero.Player == TurnPlayer && (bNoPoints || !CanMove(Hero)) && FindNextAble(Id))
	{
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s has no steps left today, the next hero is chosen in %.1f s"), *Hero.Name, Config->NextHeroDelay);
		PendingNextFrom = Id;
		PendingNextTime = FMath::Max(Config->NextHeroDelay, 0.0f);
		// the camera stays on this layer for the next hero, and does not go through the entrance after this one
		return;
	}
	if (bTravelled)
	{
		OnHeroTravelled.Broadcast(Id);
	}
}

void UHexHeroes::UpdatePendingNext(float DeltaTime)
{
	if (PendingNextFrom == 0)
	{
		return;
	}
	const FHexHero* From = FindHero(PendingNextFrom);
	// the player chose another hero or sent this one on meanwhile
	if (!bPlaying || !From || PendingNextFrom != SelectedId || From->bWalking)
	{
		PendingNextFrom = 0;
		return;
	}
	PendingNextTime -= DeltaTime;
	if (PendingNextTime > 0.0f)
	{
		return;
	}
	PendingNextFrom = 0;
	if (const FHexHero* Next = FindNextAble(From->Id))
	{
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s is chosen after %s"), *Next->Name, *From->Name);
		SelectedId = Next->Id;
		OnHeroChosen.Broadcast(SelectedId);
	}
}

bool UHexHeroes::Travel(FHexHero& Hero, FString& OutMessage, bool bBroadcast)
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	const EHexPlanetLayer Other = GetOtherLayer(Hero.Layer);
	if (!Planet || !GetPlanet(Other) || !IsUsableEntrance(*Planet, Hero.Cell))
	{
		return false;
	}
	if (const FHexHero* Blocker = FindHeroAt(Other, Hero.Cell))
	{
		OutMessage = FString::Printf(TEXT("%s: %s stands on the other side"), *Hero.Name, *Blocker->Name);
		return false;
	}
	RemoveBody(Hero.Id);
	Hero.Layer = Other;
	Hero.FromCell = Hero.Cell;
	Hero.bWalking = false;
	Hero.StepAlpha = 0.0f;
	Hero.bPlaced = false;
	Hero.TargetCell = INDEX_NONE;
	Hero.VisitCell = INDEX_NONE;
	Hero.Facing = FVector3d::ZeroVector;
	++MapRevision;
	Explore(Hero);
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s goes %s through cell %d"), *Hero.Name, Other == EHexPlanetLayer::Underground ? TEXT("down into the underground") : TEXT("up to the surface"), Hero.Cell);
	if (bBroadcast)
	{
		OnHeroTravelled.Broadcast(Hero.Id);
	}
	return true;
}

bool UHexHeroes::Send(int32 Id, int32 Cell, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(Id);
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	if (!Hero || !Planet || !Planet->IsBuilt() || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		OutMessage = TEXT("There is no such hero or cell");
		return false;
	}
	Hero->TargetCell = Cell;

	// its own cell: through the entrance or exit it stands on, and a hero walking onto it stops there
	if (Cell == Hero->Cell)
	{
		if (Hero->bWalking)
		{
			Hero->Route.Reset();
			Hero->bEnterAtEnd = IsUsableEntrance(*Planet, Cell);
			return true;
		}
		Hero->TargetCell = INDEX_NONE;
		return Travel(*Hero, OutMessage);
	}

	HexHeroPath::FField Ways;
	FindFieldFor(*Hero, Ways);
	TArray<int32> Path;
	bool bVisit = false;
	if (!FindWay(*Planet, *Hero, Ways, Cell, Path, bVisit))
	{
		// a site right beside the hero needs no way
		if (!(bVisit && !Hero->bWalking && Planet->GetGrid().GetCells()[Hero->Cell].Neighbors.Contains(Cell)))
		{
			FString Problem;
			OutMessage = bVisit || CanStand(Hero->Layer, Cell, Hero->Id, &Problem) ? FString::Printf(TEXT("%s finds no way there within %d days"), *Hero->Name, Config->PathMaxDays)
				: FString::Printf(TEXT("%s cannot go there: %s"), *Hero->Name, *Problem);
			return false;
		}
	}
	Hero->VisitCell = bVisit ? Cell : INDEX_NONE;
	if (bVisit && Path.IsEmpty() && !Hero->bWalking)
	{
		const bool bVisited = VisitSite(*Hero, OutMessage);
		Hero->bOutOfPoints = false;
		return bVisited;
	}

	Hero->Route = MoveTemp(Path);
	Hero->bEnterAtEnd = !bVisit && IsUsableEntrance(*Planet, Cell);
	if (!Hero->bWalking)
	{
		Hero->FromCell = Hero->Cell;
		Hero->StepAlpha = 0.0f;
		Hero->Speed = 0.0f;
		const int32 Steps = Hero->Route.Num();
		if (!StartNextStep(*Hero))
		{
			Hero->bOutOfPoints = false;
			OutMessage = FString::Printf(TEXT("%s has no points of movement left for that step today"), *Hero->Name);
			return false;
		}
		Hero->bWalking = true;
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s sets out from cell %d to cell %d, %d steps"), *Hero->Name, Hero->FromCell, Cell, Steps);
	}
	return true;
}

void UHexHeroes::EndTurn()
{
	PendingNextFrom = 0;
	const TArray<int32> Active = GetActivePlayers();
	if (!bPlaying || Active.IsEmpty())
	{
		// outside the game the turn of nobody ends: the next day begins, as the console asks
		BeginDay();
		return;
	}
	// a hero of its still walking ends the step it is on
	for (FHexHero& Hero : Heroes)
	{
		if (Hero.Player == TurnPlayer)
		{
			Hero.Route.Reset();
			Hero.bEnterAtEnd = false;
		}
	}
	// the next player with heroes, and after the last of them the next day and the first again
	int32 Next = Active.IndexOfByPredicate([this](int32 Player) { return Player > TurnPlayer; });
	if (Next == INDEX_NONE)
	{
		BeginDay();
		Next = 0;
	}
	BeginTurn(Active[Next]);
}

void UHexHeroes::Restart()
{
	Day = 1;
	PendingNextFrom = 0;
	for (FHexHero& Hero : Heroes)
	{
		RemoveBody(Hero.Id);
		ResetHero(Hero);
	}
	if (Sites)
	{
		Sites->Restart();
	}
	// a new game knows only the land around where the heroes start
	if (Shroud)
	{
		Shroud->Reset();
		if (bPlaying)
		{
			ExploreAll();
		}
	}
	++MapRevision;
	++ListRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: back where the game starts, day 1"));
	// and the first player moves first
	const TArray<int32> Active = GetActivePlayers();
	if (bPlaying && !Active.IsEmpty())
	{
		SelectedId = 0;
		BeginTurn(Active[0]);
	}
}

FVector3d UHexHeroes::GetStandDirection(const AHexPlanet& Planet, const FHexHero& Hero, int32 Cell) const
{
	const TArray<FHexSphereCell>& Cells = Planet.GetGrid().GetCells();
	const FVector3d Center = Cells[Cell].Center;
	if (Config->EntranceStandOff <= 0.0f || !IsUsableEntrance(Planet, Cell))
	{
		return Center;
	}
	FVector3d Side = ProjectToGround(Hero.EntranceSide, Center);
	if (Side.IsNearlyZero())
	{
		Side = GetRestFacing(Center, Hero.Id);
	}
	// the middles of two neighbors are the root of three radii of a cell apart
	const double CellRadius = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Center, Cells[Cells[Cell].Neighbors[0]].Center), -1.0, 1.0)) / FMath::Sqrt(3.0);
	return (Center + Side * (CellRadius * Config->EntranceStandOff)).GetSafeNormal();
}

void UHexHeroes::PlaceHero(FHexHero& Hero)
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet || !Planet->IsBuilt() || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
	{
		Hero.bPlaced = false;
		return;
	}
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const FVector3d To = GetStandDirection(*Planet, Hero, Hero.Cell);
	const bool bStep = Hero.bWalking && Cells.IsValidIndex(Hero.FromCell) && Hero.FromCell != Hero.Cell;
	const FVector3d From = bStep ? GetStandDirection(*Planet, Hero, Hero.FromCell) : To;
	const FVector3d Up = bStep ? FMath::Lerp(From, To, double(FMath::Clamp(Hero.StepAlpha, 0.0f, 1.0f))).GetSafeNormal() : To;

	// on the ground under it, of whichever cell that is on the way from one middle to the next
	double Ground = Planet->FindGroundRadius(bStep ? Planet->GetGrid().FindCell(Up, Hero.Cell) : Hero.Cell, Up);
	if (bStep)
	{
		Ground += Config->WalkBob * FMath::Abs(FMath::Sin(double(Hero.StepAlpha) * UE_DOUBLE_PI * Config->WalkBobsPerCell));
	}
	Hero.Position = Up * Ground;

	// it faces where it walks, and keeps facing that way where it stops
	Hero.Facing = ProjectToGround(Hero.Facing, Up);
	if (Hero.Facing.IsNearlyZero())
	{
		Hero.Facing = GetRestFacing(Up, Hero.Id);
	}
	Hero.bPlaced = true;
}

void UHexHeroes::RemoveBody(int32 Id)
{
	if (TObjectPtr<UStaticMeshComponent>* Body = Bodies.Find(Id))
	{
		if (*Body)
		{
			(*Body)->DestroyComponent();
		}
	}
	Bodies.Remove(Id);
	BodyMaterials.Remove(Id);
}

void UHexHeroes::UpdateBody(FHexHero& Hero)
{
	AHexPlanet* Planet = GetPlanet(Hero.Layer);
	TObjectPtr<UStaticMeshComponent>* Found = Bodies.Find(Hero.Id);
	UStaticMeshComponent* Body = Found ? Found->Get() : nullptr;
	// a hero under the fog of war of the person who watches is not drawn
	const bool bFogged = IsShroudOn() && IsHidden(ViewPlayer, Hero.Layer, Hero.Cell) && (!Hero.bWalking || IsHidden(ViewPlayer, Hero.Layer, Hero.FromCell));
	if (!Planet || !Hero.bPlaced || bFogged)
	{
		if (Body)
		{
			Body->SetVisibility(false);
		}
		return;
	}

	if (!Body || Body->GetOwner() != Planet)
	{
		RemoveBody(Hero.Id);
		const FHexHeroClass& Class = Config->GetClass(Hero.Class);
		UStaticMesh* Mesh = Class.Mesh.IsNull() ? Config->HeroMesh.LoadSynchronous() : Class.Mesh.LoadSynchronous();
		if (!Mesh)
		{
			return;
		}
		Body = NewObject<UStaticMeshComponent>(Planet);
		Body->SetupAttachment(Planet->GetRootComponent());
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetStaticMesh(Mesh);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetCanEverAffectNavigation(false);
		if (UMaterialInterface* Material = Config->HeroMaterial.LoadSynchronous())
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
			Instance->SetVectorParameterValue(TEXT("PlayerColor"), Config->GetPlayer(Hero.Player).Color);
			Instance->SetScalarParameterValue(TEXT("HeroColorShare"), Config->HeroColorShare);
			Instance->SetScalarParameterValue(TEXT("HeroGlow"), Config->HeroGlow);
			Body->SetMaterial(0, Instance);
			BodyMaterials.Add(Hero.Id, Instance);
		}
		Body->RegisterComponent();
		Bodies.Add(Hero.Id, Body);
	}

	const FVector3d Up = Hero.Position.GetSafeNormal();
	const FTransform Transform(FRotationMatrix::MakeFromZX(FVector(Up), FVector(Hero.Facing)).ToQuat(), FVector(Hero.Position), FVector(Config->HeroScale * Hero.ViewScale));
	if (!Body->GetComponentTransform().Equals(Transform, 1.0e-3))
	{
		Body->SetWorldTransform(Transform);
	}
	if (!Body->IsVisible())
	{
		Body->SetVisibility(true);
	}
}

UMaterialInstanceDynamic* UHexHeroes::GetPathMaterial()
{
	if (!PathMaterial)
	{
		if (UMaterialInterface* Material = Config->PathMaterial.LoadSynchronous())
		{
			PathMaterial = UMaterialInstanceDynamic::Create(Material, this);
			PathMaterial->SetVectorParameterValue(TEXT("MarkColor"), FLinearColor::White);
			PathMaterial->SetScalarParameterValue(TEXT("MarkBrightness"), Config->PathBrightness);
			PathMaterial->SetScalarParameterValue(TEXT("MarkOpacity"), Config->PathMarkOpacity);
			PathMaterial->SetScalarParameterValue(TEXT("MarkDepthBias"), Config->PathDepthBias);
			PathMaterial->SetScalarParameterValue(TEXT("MarkPulse"), 0.0f);
			// the marks grow from afar in the material, as each is an instance
			PathMaterial->SetScalarParameterValue(TEXT("MarkFarStart"), Config->FarStart);
			PathMaterial->SetScalarParameterValue(TEXT("MarkFarScale"), Config->FarScale);
		}
	}
	return PathMaterial;
}

UMaterialInstanceDynamic* UHexHeroes::GetLineMaterial()
{
	if (!LineMaterial)
	{
		if (UMaterialInterface* Material = Config->PathMaterial.LoadSynchronous())
		{
			LineMaterial = UMaterialInstanceDynamic::Create(Material, this);
			LineMaterial->SetVectorParameterValue(TEXT("MarkColor"), FLinearColor::White);
			LineMaterial->SetScalarParameterValue(TEXT("MarkBrightness"), Config->PathBrightness);
			LineMaterial->SetScalarParameterValue(TEXT("MarkOpacity"), Config->PathLineOpacity);
			LineMaterial->SetScalarParameterValue(TEXT("MarkDepthBias"), Config->PathDepthBias);
			LineMaterial->SetScalarParameterValue(TEXT("MarkPulse"), 0.0f);
			// a line grows from afar in its width alone, or it would overshoot the dots at its ends
			LineMaterial->SetScalarParameterValue(TEXT("MarkFarStart"), Config->FarStart);
			LineMaterial->SetScalarParameterValue(TEXT("MarkFarScale"), Config->FarScale);
			LineMaterial->SetScalarParameterValue(TEXT("MarkAlong"), 1.0f);
		}
	}
	return LineMaterial;
}

void UHexHeroes::UpdateRing()
{
	const FHexHero* Hero = bPlaying ? GetSelected() : nullptr;
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		FLayerMarks& Each = Marks[Layer];
		const bool bHere = Hero && Hero->bPlaced && int32(Hero->Layer) == Layer;
		AHexPlanet* Planet = bHere ? GetPlanet(Hero->Layer) : nullptr;
		if (!Planet)
		{
			if (Each.Ring.IsValid() && Each.Ring->IsVisible())
			{
				Each.Ring->SetVisibility(false);
			}
			continue;
		}
		if (!Each.Ring.IsValid())
		{
			if (!RingMaterial)
			{
				if (UMaterialInterface* Material = Config->MarkMaterial.LoadSynchronous())
				{
					RingMaterial = UMaterialInstanceDynamic::Create(Material, this);
					RingMaterial->SetScalarParameterValue(TEXT("MarkBrightness"), Config->MarkBrightness);
					RingMaterial->SetScalarParameterValue(TEXT("MarkPulse"), Config->RingPulse);
					RingMaterial->SetScalarParameterValue(TEXT("MarkPulseSpeed"), Config->RingPulseSpeed);
					RingMaterial->SetScalarParameterValue(TEXT("MarkDepthBias"), Config->RingDepthBias);
				}
			}
			// a mesh made here, as its points lie on the ground around the hero; the planet stands at the origin, so they are its own
			UProceduralMeshComponent* Ring = NewObject<UProceduralMeshComponent>(Planet);
			Ring->SetupAttachment(Planet->GetRootComponent());
			Ring->SetMobility(EComponentMobility::Movable);
			Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ring->SetCastShadow(false);
			Ring->SetVisibleInRayTracing(false);
			Ring->bReceivesDecals = false;
			// under the marks of a way
			Ring->SetTranslucentSortPriority(98);
			Ring->RegisterComponent();
			Each.Ring = Ring;
			Each.RingSegments = 0;
		}
		if (RingMaterial)
		{
			RingMaterial->SetVectorParameterValue(TEXT("MarkColor"), Config->GetPlayer(Hero->Player).Color);
		}
		// laid again only when the hero moved or grew or the ground changed
		if (!Hero->Position.Equals(Each.RingAt, 0.05) || Hero->ViewScale != Each.RingScale || Each.RingGround != GroundRevision
			|| Each.RingSegments != FMath::Clamp(Config->RingSegments, 8, 256))
		{
			BuildRing(*Planet, *Hero, Each);
		}
		if (!Each.Ring->IsVisible())
		{
			Each.Ring->SetVisibility(true);
		}
	}
}

void UHexHeroes::BuildRing(const AHexPlanet& Planet, const FHexHero& Hero, FLayerMarks& Each)
{
	// the band across: how far out a row lies as a share of the radius, how bright it is and how much it covers, a bright band with a soft
	// dark rim on both sides
	static const float Rows[][3] = {{0.7f, 0.0f, 0.0f}, {0.77f, 0.0f, 0.55f}, {0.82f, 1.0f, 1.0f}, {0.94f, 1.0f, 1.0f}, {0.985f, 0.0f, 0.6f}, {1.06f, 0.0f, 0.0f}};
	const int32 NumRows = UE_ARRAY_COUNT(Rows);
	const int32 Segments = FMath::Clamp(Config->RingSegments, 8, 256);
	const double Radius = double(Config->RingRadius) * Hero.ViewScale;
	const FVector3d Up = Hero.Position.GetSafeNormal();
	FVector3d AxisX;
	FVector3d AxisY;
	Up.FindBestAxisVectors(AxisX, AxisY);
	const FHexSphereGrid& Grid = Planet.GetGrid();

	TArray<FVector> Vertices;
	Vertices.SetNumUninitialized(Segments * NumRows);
	int32 Hint = Hero.Cell;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const double Angle = UE_DOUBLE_TWO_PI * Segment / Segments;
		const FVector3d Out = AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle);
		// the ground under the inner and the outer edge of the band, of whichever cell lies there, and a straight line between them
		double Grounds[2];
		for (int32 Edge = 0; Edge < 2; ++Edge)
		{
			const FVector3d Direction = (Hero.Position + Out * (Radius * Rows[Edge == 0 ? 0 : NumRows - 1][0])).GetSafeNormal();
			Hint = Grid.FindCell(Direction, Hint);
			Grounds[Edge] = FindGroundPoint(Planet, Hint, Hero.Cell, Direction, Config->PathLift).Size();
		}
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			const double Share = (Rows[Row][0] - Rows[0][0]) / (Rows[NumRows - 1][0] - Rows[0][0]);
			const FVector3d Direction = (Hero.Position + Out * (Radius * Rows[Row][0])).GetSafeNormal();
			Vertices[Segment * NumRows + Row] = FVector(Direction * FMath::Lerp(Grounds[0], Grounds[1], Share));
		}
	}

	if (Each.RingSegments != Segments)
	{
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FLinearColor> Colors;
		Normals.Init(FVector(Up), Vertices.Num());
		Colors.SetNumUninitialized(Vertices.Num());
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 This = Segment * NumRows;
			const int32 Next = ((Segment + 1) % Segments) * NumRows;
			for (int32 Row = 0; Row < NumRows; ++Row)
			{
				Colors[This + Row] = FLinearColor(Rows[Row][1], Rows[Row][1], Rows[Row][1], Rows[Row][2]);
				if (Row + 1 < NumRows)
				{
					Triangles.Append({This + Row, Next + Row, This + Row + 1, This + Row + 1, Next + Row, Next + Row + 1});
				}
			}
		}
		Each.Ring->ClearAllMeshSections();
		Each.Ring->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), false, false);
		if (RingMaterial)
		{
			Each.Ring->SetMaterial(0, RingMaterial);
		}
		Each.RingSegments = Segments;
	}
	else
	{
		Each.Ring->UpdateMeshSection_LinearColor(0, Vertices, TArray<FVector>(), TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	}
	Each.RingAt = Hero.Position;
	Each.RingScale = Hero.ViewScale;
	Each.RingGround = GroundRevision;
}

void UHexHeroes::ClearMarks(EHexPlanetLayer Layer)
{
	FLayerMarks& Each = GetMarks(Layer);
	if (Each.Dots.IsValid() && Each.Dots->GetInstanceCount() > 0)
	{
		Each.Dots->ClearInstances();
	}
	if (Each.End.IsValid() && Each.End->GetInstanceCount() > 0)
	{
		Each.End->ClearInstances();
	}
	if (Each.Lines.IsValid() && Each.Lines->GetInstanceCount() > 0)
	{
		Each.Lines->ClearInstances();
	}
}

double UHexHeroes::GetGroundRadius(const AHexPlanet& Planet, int32 Cell)
{
	TMap<int32, double>& Radii = GroundRadii[int32(Planet.GetLayer())];
	if (const double* Found = Radii.Find(Cell))
	{
		return *Found;
	}
	const double Ground = Planet.FindGroundRadius(Cell, Planet.GetGrid().GetCells()[Cell].Center);
	Radii.Add(Cell, Ground);
	return Ground;
}

FVector3d UHexHeroes::FindGroundPoint(const AHexPlanet& Planet, int32 Cell, int32 OtherCell, const FVector3d& Direction, double Lift)
{
	const FVector3d Up = Direction.GetSafeNormal();
	TMap<int32, TArray<FVector3f>>& Cached = GroundTriangles[int32(Planet.GetLayer())];
	const auto FindIn = [&](int32 Each, double& OutGround)
	{
		if (!Planet.GetGrid().GetCells().IsValidIndex(Each))
		{
			return false;
		}
		const TArray<FVector3f>* Triangles = Cached.Find(Each);
		if (!Triangles)
		{
			// a long game walks many cells, and the triangles of each are some kilobytes
			if (Cached.Num() >= 256)
			{
				Cached.Reset();
			}
			TArray<FVector3f>& Added = Cached.Add(Each);
			Planet.GetCellTriangles(Each, Added);
			Triangles = &Added;
		}
		for (int32 Corner = 0; Corner + 2 < Triangles->Num(); Corner += 3)
		{
			const FVector3d A((*Triangles)[Corner]);
			const FVector3d B((*Triangles)[Corner + 1]);
			const FVector3d C((*Triangles)[Corner + 2]);
			const FVector3d Normal = FVector3d::CrossProduct(B - A, C - A);
			const double Along = FVector3d::DotProduct(Normal, Up);
			if (FMath::Abs(Along) < 1.0e-9)
			{
				continue;
			}
			const double Distance = FVector3d::DotProduct(Normal, A) / Along;
			const FVector3d Point = Up * Distance;
			const auto Inside = [&Normal, &Point](const FVector3d& From, const FVector3d& To) { return FVector3d::DotProduct(FVector3d::CrossProduct(To - From, Point - From), Normal) >= -1.0e-6; };
			if (Distance > 0.0 && Inside(A, B) && Inside(B, C) && Inside(C, A))
			{
				OutGround = Distance;
				return true;
			}
		}
		return false;
	};
	double Ground = 0.0;
	if (!FindIn(Cell, Ground) && !FindIn(OtherCell, Ground))
	{
		Ground = Planet.GetCellTopRadius(Cell);
	}
	return Up * (Ground + Lift);
}

FVector3d UHexHeroes::FindGroundUp(const AHexPlanet& Planet, int32 Cell, double Reach)
{
	const FVector3d Up = Planet.GetGrid().GetCells()[Cell].Center;
	FVector3d AxisX;
	FVector3d AxisY;
	Up.FindBestAxisVectors(AxisX, AxisY);
	const double Middle = GetGroundRadius(Planet, Cell);
	FVector3d Points[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const double Angle = Index * UE_DOUBLE_TWO_PI / 3.0;
		Points[Index] = FindGroundPoint(Planet, Cell, INDEX_NONE, Up * Middle + (AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle)) * Reach, 0.0);
	}
	const FVector3d Normal = FVector3d::CrossProduct(Points[1] - Points[0], Points[2] - Points[0]).GetSafeNormal();
	return Normal.IsNearlyZero() ? Up : FVector3d::DotProduct(Normal, Up) < 0.0 ? -Normal : Normal;
}

void UHexHeroes::UpdatePath()
{
	const FHexHero* Hero = bPlaying ? GetSelected() : nullptr;
	AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	// a hero that walks shows the rest of its way, one that stands the way to the cell it is pointed to
	const bool bShow = Hero && Planet && Planet->IsBuilt() && Hero->bPlaced
		&& (Hero->bWalking ? !Hero->Route.IsEmpty() : Planet->GetGrid().GetCells().IsValidIndex(Hero->TargetCell));
	const int32 Target = !bShow ? INDEX_NONE : Hero->bWalking ? Hero->Route.Last() : Hero->TargetCell;
	const int32 RouteKey = bShow && Hero->bWalking ? Hero->Route.Num() : -1;
	const FVector3d Start = bShow && !Hero->bWalking ? Hero->Position : FVector3d::ZeroVector;
	if (Target == ShownTarget && (!bShow || (Hero->Id == ShownHero && MapRevision == ShownRevision && Hero->MovePoints == ShownPoints && RouteKey == ShownRoute
		&& Start.Equals(ShownStart, 0.01))))
	{
		return;
	}
	ShownTarget = Target;
	ShownHero = Hero ? Hero->Id : 0;
	ShownRevision = MapRevision;
	ShownPoints = Hero ? Hero->MovePoints : -1.0f;
	ShownRoute = RouteKey;
	ShownStart = Start;
	HoverText.Reset();
	PathLabels.Reset();
	ClearMarks(EHexPlanetLayer::Surface);
	ClearMarks(EHexPlanetLayer::Underground);
	if (!bShow)
	{
		return;
	}
	PathLayer = Hero->Layer;

	// the marks of the layer, made when a way is first shown on it
	FLayerMarks& Each = GetMarks(Hero->Layer);
	const auto MakeMarks = [&](TSoftObjectPtr<UStaticMesh> MeshAsset, UMaterialInstanceDynamic* Material, int32 SortPriority) -> UInstancedStaticMeshComponent*
	{
		UStaticMesh* Mesh = MeshAsset.LoadSynchronous();
		if (!Mesh)
		{
			return nullptr;
		}
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Planet);
		Component->SetupAttachment(Planet->GetRootComponent());
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetVisibleInRayTracing(false);
		Component->bReceivesDecals = false;
		Component->SetNumCustomDataFloats(3);
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
		// drawn after the water and the clouds, as neither writes its depth, the dots over the line
		Component->SetTranslucentSortPriority(SortPriority);
		Component->RegisterComponent();
		return Component;
	};
	if (!Each.Lines.IsValid())
	{
		Each.Lines = MakeMarks(Config->PathLineMesh, GetLineMaterial(), 99);
	}
	if (!Each.Dots.IsValid())
	{
		Each.Dots = MakeMarks(Config->PathMesh, GetPathMaterial(), 100);
	}
	if (!Each.End.IsValid())
	{
		Each.End = MakeMarks(Config->PathEndMesh, GetPathMaterial(), 100);
	}
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const auto GetMarkPoint = [&](int32 MarkCell)
	{
		return Cells[MarkCell].Center * (GetGroundRadius(*Planet, MarkCell) + Config->PathLift);
	};
	const auto AddMark = [&](UInstancedStaticMeshComponent* Component, const FTransform& Transform, const FLinearColor& Color)
	{
		if (Component)
		{
			const int32 Instance = Component->AddInstance(Transform, true);
			const float Data[3] = {Color.R, Color.G, Color.B};
			Component->SetCustomData(Instance, MakeArrayView(Data), true);
		}
	};
	// a flat mark on the middle of a cell, leaning as the ground around it does
	const auto AddFlatMark = [&](UInstancedStaticMeshComponent* Component, int32 MarkCell, double MarkRadius, const FLinearColor& Color)
	{
		AddMark(Component, GetMarkTransform(GetMarkPoint(MarkCell), FindGroundUp(*Planet, MarkCell, MarkRadius * 0.7), MarkRadius), Color);
	};
	// the line between two points of the ground of two cells that touch, in pieces that follow the relief between them
	const auto AddLine = [&](const FVector3d& From, int32 FromCell, const FVector3d& To, int32 ToCell, const FLinearColor& Color)
	{
		const int32 Pieces = FMath::Clamp(FMath::CeilToInt32(FVector3d::Distance(From, To) / FMath::Max(double(Config->PathLineStep), 2.0)), 1, 64);
		FVector3d Last = From;
		for (int32 Piece = 1; Piece <= Pieces; ++Piece)
		{
			const double Alpha = double(Piece) / Pieces;
			const bool bFirstHalf = Alpha < 0.5;
			const FVector3d Next = Piece == Pieces ? To : FindGroundPoint(*Planet, bFirstHalf ? FromCell : ToCell, bFirstHalf ? ToCell : FromCell, FMath::Lerp(From, To, Alpha), Config->PathLift);
			const FVector3d Delta = Next - Last;
			if (Delta.SizeSquared() > 0.01)
			{
				const FVector3d Middle = (Next + Last) * 0.5;
				// across the whole line and not each piece, so the pieces of one line meet edge to edge where the ground bends
				const FVector3d Across = FVector3d::CrossProduct(Middle, To - From).GetSafeNormal();
				AddMark(Each.Lines.Get(), FTransform(FRotationMatrix::MakeFromXY(FVector(Delta), FVector(Across)).ToQuat(), FVector(Middle),
					FVector(Delta.Size(), Config->PathLineWidth, 1.0)), Color);
			}
			Last = Next;
		}
	};

	TArray<int32> Path;
	bool bVisit = false;
	if (Hero->bWalking)
	{
		Path = Hero->Route;
		bVisit = Hero->VisitCell != INDEX_NONE;
	}
	else if (Hero->TargetCell == Hero->Cell)
	{
		// pointed to the entrance or exit it stands on
		AddFlatMark(Each.End.Get(), Hero->Cell, Config->PathEndRadius, Config->PathColor);
		HoverText = FString::Printf(TEXT("%s: click again to go %s"), *Hero->Name, Hero->Layer == EHexPlanetLayer::Surface ? TEXT("down into the underground") : TEXT("up to the surface"));
		return;
	}
	else
	{
		// the cheapest ways from where the hero stands, found again when it moved or the map changed, so a way remembered is always the best now
		if (FieldHero != Hero->Id || FieldLayer != Hero->Layer || FieldRevision != MapRevision || Field.Start != Hero->Cell)
		{
			FindFieldFor(*Hero, Field);
			FieldHero = Hero->Id;
			FieldLayer = Hero->Layer;
			FieldRevision = MapRevision;
		}
		if (!FindWay(*Planet, *Hero, Field, Hero->TargetCell, Path, bVisit)
			&& !(bVisit && Cells[Hero->Cell].Neighbors.Contains(Hero->TargetCell)))
		{
			FString Problem;
			if (bVisit || CanStand(Hero->Layer, Hero->TargetCell, Hero->Id, &Problem))
			{
				Problem = FString::Printf(TEXT("no way there within %d days"), Config->PathMaxDays);
			}
			HoverText = FString::Printf(TEXT("%s: %s"), *Hero->Name, *Problem);
			AddFlatMark(Each.End.Get(), Hero->TargetCell, Config->PathEndRadius, Config->PathBlockedColor);
			// why, at the mark itself, as a cell that cannot be entered may look as if it could
			Problem[0] = FChar::ToUpper(Problem[0]);
			PathLabels.Add({GetMarkPoint(Hero->TargetCell), 0, true, Problem});
			return;
		}
	}

	TArray<float> Costs;
	TArray<int32> Days;
	GetStepCosts(*Planet, *Hero, Path, Costs);
	// the visit of a site at the end of the way shows as its last step, onto the cell of the site
	if (bVisit)
	{
		const int32 SiteCell = Hero->bWalking ? Hero->VisitCell : Hero->TargetCell;
		Costs.Add(GetVisitCost(*Planet, Path.IsEmpty() ? Hero->Cell : Path.Last(), SiteCell));
		Path.Add(SiteCell);
	}
	HexHeroPath::FindStepDays(Costs, Hero->MovePoints, GetMaxMovePoints(*Hero), Days);
	float Total = 0.0f;
	// the line starts under the hero, or at the cell it steps onto
	FVector3d Previous = Hero->bWalking ? GetMarkPoint(Hero->Cell) : Hero->Position + Hero->Position.GetSafeNormal() * Config->PathLift;
	int32 PreviousCell = Hero->Cell;
	for (int32 Index = 0; Index < Path.Num(); ++Index)
	{
		const bool bLast = Index == Path.Num() - 1;
		const FLinearColor& Color = Days[Index] <= 1 ? Config->PathColor : Config->PathFarColor;
		const FVector3d Point = GetMarkPoint(Path[Index]);
		const FVector3d Up = Cells[Path[Index]].Center;
		Total += Costs[Index];

		// the line from the point before, lying on the ground between them
		if (FVector3d::DistSquared(Point, Previous) > 1.0)
		{
			AddLine(Previous, PreviousCell, Point, Path[Index], Color);
		}
		AddFlatMark(bLast ? Each.End.Get() : Each.Dots.Get(), Path[Index], bLast ? Config->PathEndRadius : Config->PathDotRadius, Color);
		// the number of the day at the cell where a day ends, and at the last cell the days the way takes when it is not walked today
		if (bLast ? Days[Index] > 1 : Days[Index + 1] != Days[Index])
		{
			PathLabels.Add({Point, Days[Index], bLast, FString()});
		}
		Previous = Point;
		PreviousCell = Path[Index];
	}
	HoverText = FString::Printf(TEXT("%s: %d %s, %.1f points, %s"), *Hero->Name, Path.Num(), Path.Num() == 1 ? TEXT("step") : TEXT("steps"), Total,
		Days.Last() <= 1 ? TEXT("there today") : *FString::Printf(TEXT("%d days"), Days.Last()));
	if (bVisit)
	{
		HoverText += FString::Printf(TEXT(", to %s"), *Sites->GetCellText(Hero->Layer, Path.Last()).RightChop(6));
	}
	else if (IsUsableEntrance(*Planet, Path.Last()))
	{
		HoverText += Hero->Layer == EHexPlanetLayer::Surface ? TEXT(", then down into the underground") : TEXT(", then up to the surface");
	}
}

FString UHexHeroes::GetCellText(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexHero* Hero = FindHeroAt(Layer, Cell);
	if (!Hero || !Config)
	{
		return FString();
	}
	return FString::Printf(TEXT("Hero: %s, %s of %s, movement %.1f of %.0f"), *Hero->Name, *Config->GetClass(Hero->Class).DisplayName, *Config->GetPlayer(Hero->Player).Name,
		Hero->MovePoints, GetMaxMovePoints(*Hero));
}

void UHexHeroes::LogWays(int32 Cell, int32 Rings) const
{
	const FHexHero* Hero = GetSelected();
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		UE_LOG(LogHexHero, Warning, TEXT("Heroes: no hero is chosen, or there is no such cell on its layer"));
		return;
	}
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	HexHeroPath::FField Ways;
	FindFieldFor(*Hero, Ways);
	TArray<int32> Ring = {Cell};
	TSet<int32> Seen = {Cell};
	UE_LOG(LogHexHero, Log, TEXT("Heroes: the ways of %s from cell %d with %.2f points, up to %d rings around cell %d"), *Hero->Name, Hero->Cell, Hero->MovePoints, Rings, Cell);
	for (int32 Distance = 0; Distance <= Rings; ++Distance)
	{
		TArray<int32> Next;
		for (const int32 Each : Ring)
		{
			const TArray<TObjectPtr<UHexBiomeConfig>>& Biomes = Planet->GetBiomes();
			const UHexBiomeConfig* Biome = Biomes.IsValidIndex(Planet->GetCellBiome(Each)) ? Biomes[Planet->GetCellBiome(Each)].Get() : nullptr;
			TArray<int32> Path;
			bool bVisit = false;
			FString Result;
			if (Each == Hero->Cell)
			{
				Result = TEXT("the hero stands here");
			}
			else if (FindWay(*Planet, *Hero, Ways, Each, Path, bVisit))
			{
				Result = FString::Printf(TEXT("%d steps, %.2f points%s"), Path.Num(), Path.IsEmpty() ? 0.0f : Ways.Costs[Path.Last()], bVisit ? TEXT(", then a visit") : TEXT(""));
			}
			else
			{
				FString Problem;
				Result = bVisit ? TEXT("NO WAY to a cell beside this site") : CanStand(Hero->Layer, Each, Hero->Id, &Problem) ? TEXT("NO WAY within the days of a search")
					: *FString::Printf(TEXT("blocked: %s"), *Problem);
			}
			UE_LOG(LogHexHero, Log, TEXT("  ring %d cell %d: level %d, %s, cost %.2f: %s"), Distance, Each, Planet->GetCellLevel(Each), Biome ? *Biome->GetName() : TEXT("no biome"),
				GetCellCost(*Planet, Each), *Result);
			for (const int32 Neighbor : Cells[Each].Neighbors)
			{
				bool bAlready = false;
				Seen.Add(Neighbor, &bAlready);
				if (!bAlready)
				{
					Next.Add(Neighbor);
				}
			}
		}
		Ring = MoveTemp(Next);
	}
}

void UHexHeroes::OnLayerRebuilt(EHexPlanetLayer Layer)
{
	GroundRadii[int32(Layer)].Reset();
	GroundTriangles[int32(Layer)].Reset();
	++GroundRevision;
	if (bPlaying)
	{
		ExploreAll();
	}
	if (Sites)
	{
		Sites->OnLayerRebuilt(Layer);
	}
	for (FHexHero& Hero : Heroes)
	{
		if (Hero.Layer == Layer)
		{
			Hero.bPlaced = false;
		}
	}
	++MapRevision;
}

void UHexHeroes::Update(float DeltaTime, const FVector3d* ViewPoint)
{
	if (!Surface || !Config)
	{
		return;
	}

	// a layer just built, or whose ground was rebuilt, puts its heroes on the ground
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		AHexPlanet* Planet = GetPlanet(EHexPlanetLayer(Layer));
		const bool bBuilt = Planet && Planet->IsBuilt();
		if (bBuilt != bLayerBuilt[Layer])
		{
			bLayerBuilt[Layer] = bBuilt;
			OnLayerRebuilt(EHexPlanetLayer(Layer));
		}
		if (Planet && !bRebuildBound[Layer])
		{
			bRebuildBound[Layer] = true;
			Planet->OnLevelsRebuilt.AddWeakLambda(this, [this, Layer]() { OnLayerRebuilt(EHexPlanetLayer(Layer)); });
		}
	}

	for (int32 Index = 0; Index < Heroes.Num(); ++Index)
	{
		FHexHero& Hero = Heroes[Index];
		// where a hero of the player the game moves went into the fog of war, or is about to come out of it, the camera stays a moment and
		// the hero waits; what the person who watches does not see it walks at once
		bool bHeld = NoteSeen(Hero);
		if (!bHeld && Hero.bWalking && IsPlayerAi(Hero.Player) && !IsStepSeen(Hero))
		{
			WalkUnseen(Hero);
			bHeld = NoteSeen(Hero);
		}
		if (bHeld)
		{
			Hero.Speed = 0.0f;
		}
		else if (Hero.bWalking)
		{
			// it sets out and stops smoothly: faster by the step up to WalkSpeed, and no faster than lets it brake to a stop where its way or
			// its points end
			const float Full = FMath::Max(Config->WalkSpeed, 0.01f);
			const float Least = Full * FMath::Clamp(Config->WalkMinSpeedShare, 0.02f, 1.0f);
			const float Left = FMath::Max(1.0f - Hero.StepAlpha, 0.0f) + float(CountStepsAhead(Hero));
			const float Brake = Config->WalkStopTime > 0.0f ? FMath::Sqrt(2.0f * Full / Config->WalkStopTime * Left) : Full;
			const float Wanted = FMath::Clamp(Brake, Least, Full);
			Hero.Speed = Config->WalkStartTime > 0.0f ? FMath::Min(Hero.Speed + Full / Config->WalkStartTime * DeltaTime, Wanted) : Wanted;
			Hero.Speed = FMath::Max(Hero.Speed, Least);
			Hero.StepAlpha += DeltaTime * Hero.Speed;
			while (Hero.bWalking && Hero.StepAlpha >= 1.0f)
			{
				Hero.StepAlpha -= 1.0f;
				Hero.FromCell = Hero.Cell;
				if (!StartNextStep(Hero))
				{
					Hero.bWalking = false;
					Hero.StepAlpha = 0.0f;
					Hero.Speed = 0.0f;
					Arrive(Hero);
				}
			}
		}
		if (Hero.bWalking || !Hero.bPlaced)
		{
			PlaceHero(Hero);
		}
		// it turns towards where it walks
		if (Hero.bWalking && Hero.bPlaced)
		{
			const AHexPlanet* Planet = GetPlanet(Hero.Layer);
			const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
			if (Cells.IsValidIndex(Hero.FromCell) && Hero.FromCell != Hero.Cell)
			{
				const FVector3d Up = Hero.Position.GetSafeNormal();
				const FVector3d Wanted = ProjectToGround(Cells[Hero.Cell].Center - Cells[Hero.FromCell].Center, Up);
				if (!Wanted.IsNearlyZero())
				{
					const double Angle = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Hero.Facing, Wanted), -1.0, 1.0));
					const double Step = FMath::DegreesToRadians(double(Config->TurnSpeed)) * DeltaTime;
					Hero.Facing = Angle <= Step ? Wanted : ProjectToGround(FQuat4d(Up, FVector3d::DotProduct(FVector3d::CrossProduct(Hero.Facing, Wanted), Up) >= 0.0 ? Step : -Step).RotateVector(Hero.Facing), Up);
				}
			}
		}
		Hero.ViewScale = ViewPoint && Hero.bPlaced ? float(FMath::Clamp(FVector3d::Distance(*ViewPoint, Hero.Position) / FMath::Max(double(Config->FarStart), 1.0), 1.0, double(FMath::Max(Config->FarScale, 1.0f)))) : 1.0f;
		UpdateBody(Hero);
	}

	UpdatePendingNext(DeltaTime);
	UpdateAi(DeltaTime);
	// counted after the heroes and the game looked at it, so a hold of no time holds nothing
	WatchHold = FMath::Max(WatchHold - DeltaTime, 0.0f);
	UpdateRing();
	UpdatePath();
	Sites->Update();
}

bool UHexHeroes::NoteSeen(FHexHero& Hero)
{
	// only the heroes of the player the game moves now are watched
	if (!bPlaying || Hero.Player != TurnPlayer || !IsPlayerAi(TurnPlayer))
	{
		return false;
	}
	const bool bSeen = IsSeen(Hero);
	if (bSeen != Hero.bWasSeen)
	{
		Hero.bWasSeen = bSeen;
		const AHexPlanet* Planet = GetPlanet(Hero.Layer);
		if (IsShroudOn() && Config->AiFogHoldTime > 0.0f && Planet && Planet->IsPresented() && Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
		{
			// the camera stays where it went into the fog, or goes where it is about to come out of it
			WatchHold = Config->AiFogHoldTime;
			WatchHeroId = Hero.Id;
			WatchLayer = Hero.Layer;
			WatchDirection = !bSeen && Hero.bPlaced ? Hero.Position.GetSafeNormal() : Planet->GetGrid().GetCells()[Hero.Cell].Center;
			UE_LOG(LogHexHero, Verbose, TEXT("Heroes: %s %s the fog of war at cell %d, the camera stays %.1f s"), *Hero.Name,
				bSeen ? TEXT("is about to come out of") : TEXT("went into"), Hero.Cell, WatchHold);
		}
	}
	return WatchHold > 0.0f && WatchHeroId == Hero.Id;
}

void UHexHeroes::WalkUnseen(FHexHero& Hero)
{
	// each step is taken at once, until its way ends or a step of it shows
	for (int32 Step = 0; Step < 4096 && Hero.bWalking && !IsStepSeen(Hero); ++Step)
	{
		Hero.FromCell = Hero.Cell;
		if (!StartNextStep(Hero))
		{
			Hero.bWalking = false;
			Arrive(Hero);
		}
	}
	// it comes into view setting out
	Hero.StepAlpha = 0.0f;
	Hero.Speed = 0.0f;
	Hero.bPlaced = false;
}

int32 UHexHeroes::CountStepsAhead(const FHexHero& Hero) const
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet)
	{
		return 0;
	}
	int32 Count = 0;
	float Points = Hero.MovePoints;
	int32 From = Hero.Cell;
	for (const int32 To : Hero.Route)
	{
		const float Cost = GetStepCost(*Planet, &Hero, From, To);
		if (Cost < 0.0f || !HexHeroPath::CanStep(Cost, Points, GetMaxMovePoints(Hero)))
		{
			break;
		}
		Points = FMath::Max(Points - Cost, 0.0f);
		From = To;
		++Count;
	}
	return Count;
}

void UHexHeroes::UpdateAi(float DeltaTime)
{
	if (!bPlaying || !Config || !IsPlayerAi(TurnPlayer))
	{
		return;
	}
	if (AiTurnWait > 0.0f)
	{
		AiTurnWait -= DeltaTime;
		return;
	}
	// while the camera stays where a hero went into the fog of war or comes out of it, nothing moves on
	if (WatchHold > 0.0f)
	{
		return;
	}
	// its heroes move one after the other: the one that moves goes on until it has no more to do, then the next, and then its turn ends.
	// What the person who watches does not see takes no time, so several may move in a frame
	for (int32 Pass = 0; Pass < 64; ++Pass)
	{
		FHexHero* Hero = FindHeroMutable(AiHeroId);
		// one that stopped under the fog this frame is stayed with first
		if (Hero && (Hero->bWalking || NoteSeen(*Hero)))
		{
			return;
		}
		if (!Hero || Hero->Player != TurnPlayer || Hero->bAiDone)
		{
			Hero = nullptr;
			for (FHexHero& Each : Heroes)
			{
				if (Each.Player == TurnPlayer && !Each.bAiDone && (!Hero || Each.Id < Hero->Id))
				{
					Hero = &Each;
				}
			}
			if (!Hero)
			{
				EndTurn();
				return;
			}
			AiHeroId = Hero->Id;
			Hero->AiWait = Config->AiThinkTime;
		}
		// one the person sees stands a moment before it sets out, while the camera comes to it
		const bool bSeen = IsSeen(*Hero);
		if (bSeen)
		{
			Hero->AiWait -= DeltaTime;
			if (Hero->AiWait > 0.0f)
			{
				return;
			}
		}
		Hero->AiWait = Config->AiThinkTime;
		const int32 Id = Hero->Id;
		if (!MoveAi(Id))
		{
			// nothing more for it today
			if (FHexHero* Same = FindHeroMutable(Id))
			{
				Same->bAiDone = true;
			}
		}
		else if (bSeen)
		{
			return;
		}
		// under the fog of war its walk is over within this frame: the walk is taken at once below
		if (FHexHero* Same = FindHeroMutable(Id); Same && Same->bWalking)
		{
			if (IsStepSeen(*Same))
			{
				return;
			}
			WalkUnseen(*Same);
			if (Same->bWalking)
			{
				return;
			}
		}
		DeltaTime = 0.0f;
	}
}

bool UHexHeroes::MoveAi(int32 Id)
{
	const FHexHero* Hero = FindHero(Id);
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	if (!Planet || !Shroud || !Planet->GetGrid().GetCells().IsValidIndex(Hero->Cell) || !CanMove(*Hero))
	{
		return false;
	}
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const int32 NumCells = Cells.Num();
	const bool bUnderground = Hero->Layer == EHexPlanetLayer::Underground;
	const TBitArray<>* Explored = Shroud->GetExplored(Hero->Player, Hero->Layer);

	// how many steps each cell lies from ground its player has not explored, as far as a hero sees: from such a cell it would open some.
	// Underground only the passages count, as a hero opens nothing else there
	TArray<int8> Near;
	Near.Init(-1, NumCells);
	TArray<int32> Ring;
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		if (!(Explored && Explored->IsValidIndex(Cell) && (*Explored)[Cell]) && (!bUnderground || HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Cell)))))
		{
			Near[Cell] = 0;
			Ring.Add(Cell);
		}
	}
	for (int32 Steps = 1; Steps <= Config->SightRadius && !Ring.IsEmpty(); ++Steps)
	{
		TArray<int32> Next;
		for (const int32 Cell : Ring)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (Near[Neighbor] < 0)
				{
					Near[Neighbor] = int8(FMath::Min(Steps, 127));
					Next.Add(Neighbor);
				}
			}
		}
		Ring = MoveTemp(Next);
	}

	// of the cells it reaches today the one nearest that ground, the cheapest of those, so it walks along the edge of what it knows; else
	// the cheapest it reaches on a later day
	HexHeroPath::FField Ways;
	FindFieldFor(*Hero, Ways);
	int32 Best = INDEX_NONE;
	bool bBestToday = false;
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		if (Cell == Hero->Cell || Near[Cell] <= 0 || !Ways.IsReached(Cell))
		{
			continue;
		}
		const bool bToday = Ways.Costs[Cell] <= Hero->MovePoints + UE_KINDA_SMALL_NUMBER;
		const bool bBetter = Best == INDEX_NONE || (bToday != bBestToday ? bToday
			: bToday && Near[Cell] != Near[Best] ? Near[Cell] < Near[Best] : Ways.Costs[Cell] < Ways.Costs[Best]);
		if (bBetter)
		{
			Best = Cell;
			bBestToday = bToday;
		}
	}

	// nothing left to open on this layer within reach: through the nearest entrance or exit whose other side its player has not seen
	if (Best == INDEX_NONE)
	{
		const EHexPlanetLayer Other = GetOtherLayer(Hero->Layer);
		float BestCost = 0.0f;
		for (const FHexEntrance& Entrance : Planet->GetEntrances())
		{
			const int32 Cell = Entrance.Cell;
			if (!IsUsableEntrance(*Planet, Cell) || Shroud->IsExplored(Hero->Player, Other, Cell) || FindHeroAt(Other, Cell)
				|| (Cell != Hero->Cell && !Ways.IsReached(Cell)))
			{
				continue;
			}
			const float Cost = Cell == Hero->Cell ? 0.0f : Ways.Costs[Cell];
			if (Best == INDEX_NONE || Cost < BestCost)
			{
				Best = Cell;
				BestCost = Cost;
			}
		}
	}
	if (Best == INDEX_NONE)
	{
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, moved by the game, finds nothing more to explore from cell %d of the %s"), *Hero->Name, Hero->Cell, GetPlanetLayerName(Hero->Layer));
		return false;
	}
	FString Message;
	return Send(Id, Best, Message);
}

bool UHexHeroes::GetLights(EHexPlanetLayer Layer, const FVector3d& Focus, TArray<FHexHeroLight>& OutLights) const
{
	OutLights.Reset();
	if (Heroes.IsEmpty() || !Config)
	{
		return false;
	}
	const float TriedShare = CVarHeroLightShare.GetValueOnGameThread();
	const float Share = TriedShare >= 0.0f ? TriedShare : Config->LightMoveShare;
	for (const FHexHero& Hero : Heroes)
	{
		// a hero under the fog of war lights nothing the person sees
		if (Hero.Layer == Layer && Hero.bPlaced && !(IsShroudOn() && IsHidden(ViewPlayer, Hero.Layer, Hero.Cell)))
		{
			OutLights.Add({Hero.Position, double(Share) * GetMaxMovePoints(Hero)});
		}
	}
	OutLights.Sort([&Focus](const FHexHeroLight& A, const FHexHeroLight& B) { return FVector3d::DistSquared(A.Point, Focus) < FVector3d::DistSquared(B.Point, Focus); });
	OutLights.SetNum(FMath::Min(OutLights.Num(), FMath::Clamp(Config->MaxLights, 1, MaxHeroLights)), EAllowShrinking::No);
	return true;
}

bool UHexHeroes::GetFollowDirection(EHexPlanetLayer Layer, FVector3d& OutDirection) const
{
	if (!bPlaying || !Config || !Config->bCameraFollows)
	{
		return false;
	}
	// where a hero of the player the game moves went into the fog of war, or is about to come out of it, the camera stays a moment
	if (WatchHold > 0.0f)
	{
		OutDirection = WatchDirection;
		return Layer == WatchLayer && !WatchDirection.IsNearlyZero();
	}
	// the camera stays with the hero a person sent while it walks, and on the turn of a player the game moves with its hero that moves, as
	// long as the person sees it, as the developer asked on 2026-11-11
	const FHexHero* Hero = IsPlayerAi(TurnPlayer) ? FindHero(AiHeroId) : GetSelected();
	if (!Hero || !Hero->bPlaced || Hero->Layer != Layer || (IsPlayerAi(TurnPlayer) ? !IsSeen(*Hero) : !Hero->bWalking))
	{
		return false;
	}
	OutDirection = Hero->Position.GetSafeNormal();
	return true;
}

namespace
{
	/** Reads a cell of hex.Hero: its number, or focus for the cell the camera looks at, or focus:<Steps>[:<Side>] for the cell that many steps
	 *  from it towards a side of each cell on the way, 0 by default, so a run from -ExecCmds finds cells near a place without knowing them */
	int32 ParseHeroCell(const FString& Text, const AHexPlanet& Planet, const UWorld* World)
	{
		if (!Text.StartsWith(TEXT("focus"), ESearchCase::IgnoreCase))
		{
			return Text.IsNumeric() ? FCString::Atoi(*Text) : INDEX_NONE;
		}
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		const AActor* ViewTarget = PlayerController ? PlayerController->GetViewTarget() : nullptr;
		if (!ViewTarget)
		{
			return INDEX_NONE;
		}
		const TArray<FHexSphereCell>& Cells = Planet.GetGrid().GetCells();
		int32 Cell = Planet.GetGrid().FindCell(Planet.FindViewFocus(ViewTarget->GetActorLocation(), ViewTarget->GetActorForwardVector()).GetSafeNormal());
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(":"));
		const int32 Steps = Parts.IsValidIndex(1) ? FCString::Atoi(*Parts[1]) : 0;
		const int32 Side = Parts.IsValidIndex(2) ? FCString::Atoi(*Parts[2]) : 0;
		for (int32 Step = 0; Step < Steps && Cells.IsValidIndex(Cell); ++Step)
		{
			Cell = Cells[Cell].Neighbors[FMath::Abs(Side) % Cells[Cell].Neighbors.Num()];
		}
		return Cell;
	}

	/** hex.Hero: drives the heroes as the mouse, the keys and the palette do, for trying them from the console and from -ExecCmds */
	void RunHeroCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		AHexPlanet* Planet = Layers ? Layers->GetPresentedBuilt() : nullptr;
		UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
		if (!Heroes || !Heroes->GetConfig() || Args.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Hero <list|play|stop|add <Cell> [Class] [Player]|remove <Hero>|select <Hero>|next|click <Cell>|target <Cell|off>|go|send <Cell> [Hero]|ways <Cell> [Rings]|ai <Player> [on|off]|endturn|restart>, on a built planet; a hero is its Id or its name"));
			return;
		}
		const UHexHeroConfig* Config = Heroes->GetConfig();
		const EHexPlanetLayer Layer = Planet->GetLayer();
		const FString& Action = Args[0];
		FString Message;

		if (Action.Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogHexHero, Log, TEXT("Heroes: day %d, %d heroes"), Heroes->GetDay(), Heroes->GetHeroes().Num());
			for (const FHexHero& Hero : Heroes->GetHeroes())
			{
				UE_LOG(LogHexHero, Log, TEXT("  %d %s, %s of %s, cell %d of the %s, movement %.2f of %.0f%s%s%s"), Hero.Id, *Hero.Name, *Config->GetClass(Hero.Class).DisplayName,
					*Config->GetPlayer(Hero.Player).Name, Hero.Cell, GetPlanetLayerName(Hero.Layer), Hero.MovePoints, Heroes->GetMaxMovePoints(Hero),
					Hero.bWalking ? TEXT(", walking") : TEXT(""), Hero.Id == Heroes->GetSelectedId() ? TEXT(", chosen") : TEXT(""),
					Heroes->IsPlayerAi(Hero.Player) ? TEXT(", moved by the game") : TEXT(""));
			}
		}
		else if (Action.Equals(TEXT("add"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			const int32 Class = Args.Num() >= 3 ? FMath::Max(Config->FindClass(Args[2]), 0) : 0;
			const int32 Player = Args.Num() >= 4 ? FCString::Atoi(*Args[3]) : 0;
			if (Heroes->AddHero(Layer, ParseHeroCell(Args[1], *Planet, World), Class, Player, Message) == 0)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: no hero on cell %s: %s"), *Args[1], *Message);
			}
		}
		else if (Action.Equals(TEXT("remove"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			const FHexHero* Hero = Heroes->FindHeroByText(Args[1]);
			if (!Hero || !Heroes->RemoveHero(Hero->Id))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: there is no hero %s"), *Args[1]);
			}
		}
		else if (Action.Equals(TEXT("select"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			const FHexHero* Hero = Heroes->FindHeroByText(Args[1]);
			if (!Hero || !Heroes->Select(Hero->Id))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: there is no hero %s"), *Args[1]);
			}
		}
		else if (Action.Equals(TEXT("next"), ESearchCase::IgnoreCase))
		{
			Heroes->SelectNext();
		}
		else if (Action.Equals(TEXT("send"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			const FHexHero* Hero = Args.Num() >= 3 ? Heroes->FindHeroByText(Args[2]) : Heroes->GetSelected();
			if (!Hero)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: no hero is chosen"));
			}
			else if (!Heroes->Send(Hero->Id, ParseHeroCell(Args[1], *Planet, World), Message))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: %s"), Message.IsEmpty() ? TEXT("the hero stays") : *Message);
			}
		}
		else if (Action.Equals(TEXT("play"), ESearchCase::IgnoreCase))
		{
			if (!Heroes->StartPlay(Message))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: %s"), *Message);
			}
		}
		else if (Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			Heroes->StopPlay();
		}
		else if (Action.Equals(TEXT("click"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			// what a left click on a cell of the layer shown does
			static const TCHAR* Names[] = {TEXT("nothing"), TEXT("another hero is chosen"), TEXT("the way shows"), TEXT("the hero sets out"), TEXT("the hero stops")};
			const UHexHeroes::EClick Result = Heroes->Click(Layer, ParseHeroCell(Args[1], *Planet, World), nullptr, Message);
			UE_LOG(LogHexHero, Log, TEXT("hex.Hero: click, %s%s%s"), Names[int32(Result)], Message.IsEmpty() ? TEXT("") : TEXT(": "), *Message);
		}
		else if (Action.Equals(TEXT("target"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			Heroes->SetTarget(Heroes->GetSelectedId(), Args[1].Equals(TEXT("off"), ESearchCase::IgnoreCase) ? INDEX_NONE : ParseHeroCell(Args[1], *Planet, World));
		}
		else if (Action.Equals(TEXT("go"), ESearchCase::IgnoreCase))
		{
			if (!Heroes->Go(Heroes->GetSelectedId(), Message))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: %s"), Message.IsEmpty() ? TEXT("the hero chosen is pointed nowhere") : *Message);
			}
		}
		else if (Action.Equals(TEXT("ai"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			// which players the game moves by itself, as the Heroes tab of the palette sets
			Heroes->SetPlayerAi(FCString::Atoi(*Args[1]), Args.Num() < 3 || !Args[2].Equals(TEXT("off"), ESearchCase::IgnoreCase));
		}
		else if (Action.Equals(TEXT("ways"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			Heroes->LogWays(ParseHeroCell(Args[1], *Planet, World), Args.Num() >= 3 ? FMath::Clamp(FCString::Atoi(*Args[2]), 0, 8) : 2);
		}
		else if (Action.Equals(TEXT("endturn"), ESearchCase::IgnoreCase))
		{
			Heroes->EndTurn();
		}
		else if (Action.Equals(TEXT("restart"), ESearchCase::IgnoreCase))
		{
			Heroes->Restart();
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: unknown action %s"), *Action);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs HeroCommand(TEXT("hex.Hero"),
		TEXT("Drives the heroes of the planet: list, play and stop (the game), add <Cell> [Class] [Player] on the layer shown, remove <Hero>, select <Hero>, next, click <Cell> (as the left button), target <Cell|off> and go (as a click and Space), send <Cell> [Hero], ways <Cell> [Rings] (logs the way of the hero chosen to each cell around one, or why it has none), ai <Player> [on|off] (the game moves the heroes of a player by itself), endturn, restart. A hero is its Id or its name; a cell is its number, focus for the one the camera looks at, or focus:<Steps>[:<Side>] for one that many steps from it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHeroCommand));
}
