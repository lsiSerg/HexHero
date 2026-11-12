// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHeroes.h"
#include "HexBattles.h"
#include "HexBattleConfig.h"
#include "HexNeutrals.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexBiomeConfig.h"
#include "HexPlanet.h"
#include "HexPlanetObjects.h"
#include "HexPlanetRivers.h"
#include "HexPlanetUnderground.h"
#include "HexShroud.h"
#include "HexCastles.h"
#include "HexSites.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "HexHeroAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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
	Hero.CheckCell = INDEX_NONE;
	Hero.WalkWait = 0.0f;
	Hero.TargetLayer = Hero.StartLayer;
	Hero.bSkipped = false;
	Hero.bAiDone = false;
	Hero.AiWait = Config->AiThinkTime;
	Hero.bPlaced = false;
	Hero.Facing = FVector3d::ZeroVector;
	// the creatures a hero of its player starts with
	Hero.Army = !Hero.StartArmy.IsEmpty() ? Hero.StartArmy : Castles ? Castles->MakeStartArmy(Hero.Player) : TArray<FHexStack>();
	Hero.CastleId = 0;
	Hero.GlideStart = -1000.0;
	Hero.bGliding = false;
	Hero.GlideScaleFrom = Hero.GlideScaleTo = Hero.PassScale = 1.0f;
	Hero.bEnteringMouth = false;
	Hero.GlideFade = 0;
	Hero.PassFade = 0.0f;
	Hero.GateStepFrom = INDEX_NONE;
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
	// before the heroes, which start with creatures of the faction of their player
	Castles = NewObject<UHexCastles>(this);
	Castles->Init(InSurface, this);
	Shroud = NewObject<UHexShroud>(this);
	// the neutral stacks of the map, read whether there are heroes or not, and the battles fought against them and between heroes
	Neutrals = NewObject<UHexNeutrals>(this);
	Neutrals->Init(InSurface, this);
	Battles = NewObject<UHexBattles>(this);
	Battles->Init(this);
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
		Hero.StartArmy = Castles ? Castles->ParseStackLines(Line.Army) : TArray<FHexStack>();
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
		Line.Army = Castles ? Castles->MakeStackLines(Hero.StartArmy) : TArray<FHexStackLine>();
	}
	return Lines;
}

bool UHexHeroes::AddUnits(int32 Id, const FHexStack& Stack)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero || Stack.Count <= 0 || !UHexCastles::AddToArmy(Hero->Army, Stack))
	{
		return false;
	}
	++ListRevision;
	return true;
}

bool UHexHeroes::KeepArmyAsStart(int32 Id)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero)
	{
		return false;
	}
	Hero->StartArmy = Hero->Army;
	UpdateEdited();
	return true;
}

bool UHexHeroes::Save(const FString& PlanetFolder, FString& OutError)
{
	if (!HexPlanetFile::SaveHeroes(PlanetFolder, MakeLines(), OutError, AiPlayers) || (Sites && !Sites->Save(PlanetFolder, OutError))
		|| (Castles && !Castles->Save(PlanetFolder, OutError)) || (Neutrals && !Neutrals->Save(PlanetFolder, OutError)))
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
	return bEdited || (Sites && Sites->IsEdited()) || (Castles && Castles->IsEdited()) || (Neutrals && Neutrals->IsEdited());
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

bool UHexHeroes::Stop(int32 Id)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero || !Hero->bWalking)
	{
		return false;
	}
	Hero->Route.Reset();
	Hero->bEnterAtEnd = false;
	Hero->TargetCell = INDEX_NONE;
	Hero->VisitCell = INDEX_NONE;
	Hero->CheckCell = INDEX_NONE;
	return true;
}

bool UHexHeroes::Skip(int32 Id)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero || !IsPersonTurn() || Hero->Player != TurnPlayer)
	{
		return false;
	}
	Stop(Id);
	Hero->bSkipped = true;
	Hero->TargetCell = INDEX_NONE;
	PendingNextFrom = 0;
	++ListRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s is left for today"), *Hero->Name);
	if (const FHexHero* Next = FindNextAble(Id))
	{
		ChooseHero(Next->Id);
		OnHeroChosen.Broadcast(SelectedId);
	}
	else
	{
		OnMessage.Broadcast(TEXT("No other hero can move today"));
	}
	return true;
}

bool UHexHeroes::AddMovePoints(int32 Id, float Amount)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero)
	{
		return false;
	}
	Hero->MovePoints = FMath::Max(Hero->MovePoints + Amount, 0.0f);
	Hero->bAiDone = false;
	++MapRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s has %.2f points of movement now"), *Hero->Name, Hero->MovePoints);
	return true;
}

bool UHexHeroes::CanChoose(int32 Id) const
{
	const FHexHero* Hero = FindHero(Id);
	return Hero && (!bPlaying || (IsPersonTurn() && Hero->Player == TurnPlayer));
}

bool UHexHeroes::IsCellSeen(EHexPlanetLayer Layer, int32 Cell) const
{
	// on either layer: to one on the layer that is not shown the camera goes first (WaitForView)
	const AHexPlanet* Planet = GetPlanet(Layer);
	return Planet && !IsHidden(ViewPlayer, Layer, Cell);
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
	SelectedCastleId = 0;
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
		// it builds in its castles and hires into their garrisons
		if (Castles)
		{
			Castles->RunAi(Player);
		}
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
		ChooseHero(Able ? Able->Id : First ? First->Id : 0);
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
		Hero.bSkipped = false;
	}
	++ListRevision;
	// the mines give their owners their resources
	if (Sites)
	{
		Sites->EndDay();
	}
	// the halls give gold, and on the first day of a week the dwellings offer more creatures
	if (Castles)
	{
		Castles->BeginDay(Day, Config->DaysPerWeek);
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
	if (Config)
	{
		ExploreAround(Hero.Player, Hero.Layer, Hero.Cell, Config->SightRadius);
	}
}

void UHexHeroes::ExploreAround(int32 Player, EHexPlanetLayer Layer, int32 Cell, int32 Radius)
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet || !Shroud || !Config || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return;
	}
	TArray<int32> Seen;
	HexShroud::FindWithin(Planet->GetGrid(), Cell, Radius, Seen);
	// underground only the passages open, as nobody walks the voids; the planet shows a strip of the void beside them (ShroudVoidMargin)
	if (Layer == EHexPlanetLayer::Underground)
	{
		Seen.RemoveAll([Planet](int32 Each) { return !HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Each))); });
	}
	if (Shroud->Explore(Player, Layer, Seen, Planet->GetGrid().GetCells().Num()))
	{
		// a hero may find a way over what was just seen
		++MapRevision;
	}
}

bool UHexHeroes::SetCatapult(int32 Id, bool bHas)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero)
	{
		return false;
	}
	Hero->bCatapult = bHas;
	return true;
}

int32 UHexHeroes::RevealAll(int32 Player)
{
	int32 Opened = 0;
	for (const EHexPlanetLayer Layer : {EHexPlanetLayer::Surface, EHexPlanetLayer::Underground})
	{
		const AHexPlanet* Planet = GetPlanet(Layer);
		if (!Planet || !Shroud || Planet->GetGrid().GetCells().IsEmpty())
		{
			continue;
		}
		const int32 NumCells = Planet->GetGrid().GetCells().Num();
		const int32 Before = Shroud->CountExplored(Player, Layer);
		// underground the passages, as a hero explores it
		TArray<int32> All;
		All.Reserve(NumCells);
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (Layer == EHexPlanetLayer::Surface || HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Cell))))
			{
				All.Add(Cell);
			}
		}
		Shroud->Explore(Player, Layer, All, NumCells);
		Opened += Shroud->CountExplored(Player, Layer) - Before;
	}
	++MapRevision;
	return Opened;
}

void UHexHeroes::ExploreCastles()
{
	// a castle sees for its owner, CastleSightFactor times as far as a hero, as the developer asked on 2026-11-11
	if (!Castles || !Config || Config->CastleSightFactor <= 0.0f)
	{
		return;
	}
	for (const FHexCastle& Castle : Castles->GetCastles())
	{
		if (Castle.Owner >= 0)
		{
			ExploreAround(Castle.Owner, Castle.Layer, Castle.Cells[0], FMath::RoundToInt32(Config->SightRadius * Config->CastleSightFactor));
		}
	}
}

void UHexHeroes::ExploreAll()
{
	for (const FHexHero& Hero : Heroes)
	{
		Explore(Hero);
	}
	ExploreCastles();
}

bool UHexHeroes::IsUsableEntrance(const AHexPlanet& Planet, int32 Cell)
{
	const int32 Entrance = Planet.FindEntrance(Cell);
	return Entrance != INDEX_NONE && Planet.GetEntranceProblem(Entrance) == HexPlanetUnderground::EEntranceProblem::None;
}

bool UHexHeroes::IsSeenVoid(int32 Player, EHexPlanetLayer Layer, int32 Cell) const
{
	const AHexPlanet* Planet = Layer == EHexPlanetLayer::Underground ? GetPlanet(Layer) : nullptr;
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell) || HexPlanetUnderground::IsPassage(int8(Planet->GetCellLevel(Cell))))
	{
		return false;
	}
	for (const int32 Neighbor : Planet->GetGrid().GetCells()[Cell].Neighbors)
	{
		if (!IsHidden(Player, Layer, Neighbor))
		{
			return true;
		}
	}
	return false;
}

bool UHexHeroes::IsBlockedUnbuilt(const AHexPlanet& Planet, int32 Cell) const
{
	// an underground that was never shown has its levels but not its objects yet, so what blocks there is read from its file, once
	const int32 Layer = int32(Planet.GetLayer());
	const int32 NumCells = Planet.GetGrid().GetCells().Num();
	if (NumCells == 0)
	{
		return false;
	}
	if (UnbuiltBlockers[Layer].Num() != NumCells)
	{
		UnbuiltBlockers[Layer] = UHexPlanetObjects::FindSavedBlockers(Planet);
		UE_LOG(LogHexHero, Log, TEXT("Heroes: the %s is not built yet, so %d cells that block movement there are read from its objects.json"), GetPlanetLayerName(Planet.GetLayer()),
			UnbuiltBlockers[Layer].CountSetBits());
	}
	return UnbuiltBlockers[Layer].IsValidIndex(Cell) && UnbuiltBlockers[Layer][Cell];
}

float UHexHeroes::GetCellCost(const AHexPlanet& Planet, int32 Cell, bool bIgnoreSite) const
{
	// a mine or a mill fills its cell, and a castle its three, as a neutral stack fills its; a pile lies on its, and is walked onto
	if (!bIgnoreSite && ((Sites && Sites->FindBlockerAt(Planet.GetLayer(), Cell)) || (Castles && Castles->FindCastleAt(Planet.GetLayer(), Cell))
		|| (Neutrals && Neutrals->FindAt(Planet.GetLayer(), Cell))))
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
	if (Planet.IsBuilt() ? Planet.GetObjects() && Planet.GetObjects()->BlocksMovement(Cell) : IsBlockedUnbuilt(Planet, Cell))
	{
		return -1.0f;
	}
	return (Level == 1 && !bUnderground ? Config->HillCost : 1.0f) * (Biome ? FMath::Max(Biome->MoveCostMultiplier, 0.01f) : 1.0f);
}

bool UHexHeroes::IsOpenGround(EHexPlanetLayer Layer, int32 Cell) const
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	return Planet && Config && Planet->GetGrid().GetCells().IsValidIndex(Cell) && GetCellCost(*Planet, Cell, true) >= 0.0f;
}

int32 UHexHeroes::GetWeek() const
{
	return (Day - 1) / FMath::Max(Config ? Config->DaysPerWeek : 7, 1) + 1;
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
	// the step out of the castle it guards costs nothing
	if (Hero && Hero->CastleId != 0 && Hero->Layer == Layer && From == Hero->Cell)
	{
		return 0.0f;
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
	// what its player has not explored is taken to be open ground, as nothing is known of it
	const FHexHero* Mover = FindHero(IgnoreHero);
	if (Mover && IsHidden(Mover->Player, Layer, Cell))
	{
		return true;
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
	if (Sites && Sites->FindBlockerAt(Layer, Cell))
	{
		return Fail(TEXT("a mine or a mill fills that cell"));
	}
	if (Castles && Castles->FindCastleAt(Layer, Cell))
	{
		return Fail(TEXT("a castle stands there"));
	}
	if (Neutrals && Neutrals->FindAt(Layer, Cell))
	{
		return Fail(TEXT("a neutral stack stands there"));
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
	// a hero takes a pile by stepping onto it, so none starts on one
	if (Sites && Sites->FindSiteAt(Layer, Cell))
	{
		OutProblem = TEXT("a pile lies there");
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
	ChooseHero(Id);
	return true;
}

void UHexHeroes::ChooseHero(int32 Id)
{
	// a hero in a castle is chosen with its castle, as the developer proposed on 2026-11-11, so the two are not told apart by where a click
	// lands: an order then takes the hero out, and two clicks open the castle
	const FHexHero* Hero = FindHero(Id);
	SelectedId = Id;
	SelectedCastleId = Hero && bPlaying ? Hero->CastleId : 0;
	++ListRevision;
}

bool UHexHeroes::SelectCastle(int32 CastleId)
{
	const FHexCastle* Castle = Castles && CastleId != 0 ? Castles->FindCastle(CastleId) : nullptr;
	// while the game is played a person chooses only a castle of its own, on its turn
	if (CastleId != 0 && (!Castle || !bPlaying || !IsPersonTurn() || Castle->Owner != TurnPlayer))
	{
		return false;
	}
	SelectedCastleId = CastleId;
	// with the hero that guards it
	if (const FHexHero* Guard = Castle ? Castles->FindGuard(*Castle) : nullptr; Guard && Guard->Player == TurnPlayer)
	{
		SelectedId = Guard->Id;
	}
	++ListRevision;
	return true;
}

const FHexHero* UHexHeroes::SelectNext()
{
	// the lowest Id above the one chosen, else the lowest of all; while the game is played only of the player whose turn it is, and of
	// those the ones not left for today while there is any
	const bool bFresh = bPlaying && Heroes.ContainsByPredicate([this](const FHexHero& Each) { return Each.Player == TurnPlayer && !Each.bSkipped; });
	const FHexHero* Next = nullptr;
	const FHexHero* First = nullptr;
	for (const FHexHero& Hero : Heroes)
	{
		if (bPlaying && (Hero.Player != TurnPlayer || (bFresh && Hero.bSkipped)))
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
	ChooseHero((Next ? Next : First)->Id);
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
		// of the player whose turn it is, and not one it left for today
		if (Hero->Id != After && !Hero->bWalking && Hero->Player == TurnPlayer && !Hero->bSkipped && CanMove(*Hero))
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
	// also the hero chosen before a castle was chosen in its place, which a click did not choose again until 2026-11-11
	if (Under && (Under->Id != SelectedId || !GetSelected()))
	{
		if (Under->Player == TurnPlayer)
		{
			ChooseHero(Under->Id);
			return EClick::Chosen;
		}
		// the hero of another player is not one to choose: its cell is pointed to as any other, on which it stands in the way
		Cell = Under->Cell;
		Under = nullptr;
	}

	// with a castle chosen the hero that guards it is pointed, and with none in it nobody
	FHexHero* Hero = GetSelected() ? FindHeroMutable(SelectedId) : nullptr;
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
	// a click on the body of the hero chosen is a click on its cell; the cell may be of the other layer than the hero is on, as its way may
	// go through the entrances
	if (Under)
	{
		Cell = Hero->Cell;
	}
	const AHexPlanet* Clicked = GetPlanet(Layer);
	if (!Clicked || !Clicked->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return EClick::None;
	}
	// any click stops a hero that walks, at the end of the step it is on
	if (Stop(Hero->Id))
	{
		return EClick::Stopped;
	}
	// and one it left for today is taken up again
	Hero->bSkipped = false;
	if (Cell == Hero->TargetCell && Layer == Hero->TargetLayer)
	{
		return Go(Hero->Id, OutMessage) ? EClick::Sent : EClick::None;
	}
	// its own cell is nowhere to go; through the entrance it stands before it goes by a click on the mouth, which is its cell on the other layer
	if (Cell == Hero->Cell && Layer == Hero->Layer)
	{
		Hero->TargetCell = INDEX_NONE;
		return EClick::None;
	}
	Hero->TargetCell = Cell;
	Hero->TargetLayer = Layer;
	return EClick::Targeted;
}

UHexHeroes::EOrder UHexHeroes::GetOrderAt(EHexPlanetLayer Layer, int32 Cell, const FHexHero* Under, bool bMouth)
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!IsPersonTurn() || !Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return EOrder::None;
	}
	const int32 Player = TurnPlayer;
	const bool bHidden = IsHidden(Player, Layer, Cell);
	if (!Under && !bMouth)
	{
		Under = FindHeroAt(Layer, Cell);
	}
	if (Under && Under->Player != Player && IsHidden(Player, Under->Layer, Under->Cell))
	{
		Under = nullptr;
	}
	const FHexHero* Hero = GetSelected();
	const FHexCastle* Castle = Castles && !bMouth ? (Under && Under->CastleId != 0 ? Castles->FindCastle(Under->CastleId) : Castles->FindCastleAt(Layer, Cell)) : nullptr;
	if (Castle && !bHidden && Castle->Owner == Player)
	{
		// a click on a castle of the person chooses it, and the hero chosen outside it is sent into it with the right button
		return Hero && Hero->CastleId != Castle->Id && !Hero->bWalking ? EOrder::Castle : EOrder::Choose;
	}
	if (Under && Under->Player == Player && (!Hero || Under->Id != Hero->Id))
	{
		int32 CastleCell = INDEX_NONE;
		return Hero && FindSallyTarget(*Hero, Layer, Cell, CastleCell) == Under ? EOrder::Swap : EOrder::Choose;
	}
	if (!Hero || Hero->bWalking || IsPlayerAi(Hero->Player) || (Cell == Hero->Cell && Layer == Hero->Layer && !bMouth))
	{
		return EOrder::None;
	}
	int32 CastleCell = INDEX_NONE;
	if (!bMouth && FindSallyTarget(*Hero, Layer, Cell, CastleCell))
	{
		return EOrder::Attack;
	}
	// the cheapest ways from where the hero stands, as the way shown takes them
	if (FieldHero != Hero->Id || FieldLayer != Hero->Layer || FieldRevision != MapRevision || Field.Start != GetNode(Hero->Layer, Hero->Cell))
	{
		FindFieldFor(*Hero, Field, true);
		FieldHero = Hero->Id;
		FieldLayer = Hero->Layer;
		FieldRevision = MapRevision;
	}
	TArray<int32> Path;
	bool bVisit = false;
	if (!FindWay(*Hero, Field, Layer, Cell, Path, bVisit))
	{
		return EOrder::Blocked;
	}
	float Cost = 0.0f;
	if (!GetWayCost(*Hero, Path, Cost))
	{
		return EOrder::Blocked;
	}
	if (bVisit)
	{
		Cost += GetVisitCost(*Planet, Path.IsEmpty() ? Hero->Cell : Path.Last(), Cell);
	}
	if (bMouth)
	{
		return EOrder::Portal;
	}
	if (!bHidden)
	{
		if (IsFoeAt(Player, Layer, Cell))
		{
			return EOrder::Attack;
		}
		if (Castle)
		{
			// a castle a garrison or a hero defends is fought for
			return !Castle->Garrison.IsEmpty() || Castles->FindGuard(*Castle) ? EOrder::Attack : EOrder::Castle;
		}
		if (Sites && (Sites->FindBlockerAt(Layer, Cell) || Sites->FindPileAt(Layer, Cell)))
		{
			return EOrder::Visit;
		}
	}
	return Cost <= Hero->MovePoints + 1.0e-3f ? EOrder::Move : EOrder::MoveLater;
}

bool UHexHeroes::SetTarget(int32 Id, EHexPlanetLayer Layer, int32 Cell)
{
	FHexHero* Hero = FindHeroMutable(Id);
	if (!Hero)
	{
		return false;
	}
	Hero->TargetCell = Cell;
	Hero->TargetLayer = Layer;
	return true;
}

bool UHexHeroes::Go(int32 Id, FString& OutMessage)
{
	const FHexHero* Hero = FindHero(Id);
	if (!Hero || Hero->bWalking || IsPlayerAi(Hero->Player))
	{
		return false;
	}
	if (Hero->TargetCell == INDEX_NONE)
	{
		return false;
	}
	return Send(Id, Hero->TargetLayer, Hero->TargetCell, OutMessage);
}

int32 UHexHeroes::GetNode(EHexPlanetLayer Layer, int32 Cell) const
{
	return int32(Layer) * (Surface ? Surface->GetGrid().GetCells().Num() : 0) + Cell;
}

void UHexHeroes::WalkWay(const FHexHero& Hero, TConstArrayView<int32> Path, TFunctionRef<bool(EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)> Visit) const
{
	EHexPlanetLayer Layer = Hero.Layer;
	int32 From = Hero.Cell;
	for (const int32 To : Path)
	{
		const bool bThrough = To == From;
		if (bThrough)
		{
			Layer = GetOtherLayer(Layer);
		}
		if (!Visit(Layer, From, To, bThrough))
		{
			return;
		}
		From = To;
	}
}

float UHexHeroes::GetWayStepCost(const FHexHero& Hero, EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough) const
{
	if (bThrough)
	{
		return FMath::Max(Config->EntranceCost, 0.0f);
	}
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet)
	{
		return -1.0f;
	}
	if (IsHidden(Hero.Player, Layer, To))
	{
		return IsSeenVoid(Hero.Player, Layer, To) ? -1.0f : Config->UnknownCost;
	}
	return GetStepCost(*Planet, &Hero, From, To);
}

bool UHexHeroes::IsWayUnknown(const FHexHero& Hero, TConstArrayView<int32> Path, EHexPlanetLayer TargetLayer, int32 Target) const
{
	if (!IsShroudOn())
	{
		return false;
	}
	if (IsHidden(Hero.Player, TargetLayer, Target))
	{
		return true;
	}
	bool bUnknown = false;
	WalkWay(Hero, Path, [&](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
	{
		bUnknown = IsHidden(Hero.Player, Layer, To);
		return !bUnknown;
	});
	return bUnknown;
}

void UHexHeroes::UpdateReach()
{
	const FHexHero* Hero = bPlaying && IsPersonTurn() && Config->bShowReach ? GetSelected() : nullptr;
	if (Hero && Hero->Player != TurnPlayer)
	{
		Hero = nullptr;
	}
	const auto Hide = [this]()
	{
		if (ReachHero != 0)
		{
			ReachHero = 0;
			for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
			{
				if (AHexPlanet* Planet = GetPlanet(EHexPlanetLayer(Layer)); Planet && Planet->IsBuilt())
				{
					Planet->SetReach({});
				}
			}
		}
	};
	if (!Hero)
	{
		Hide();
		return;
	}
	// while it walks what was shown stays, but not that of another hero
	if (Hero->bWalking || Hero->bGliding)
	{
		if (ReachHero != Hero->Id)
		{
			Hide();
		}
		return;
	}
	const int32 Node = GetNode(Hero->Layer, Hero->Cell);
	const bool bShroud = IsShroudOn();
	if (ReachHero == Hero->Id && ReachNode == Node && ReachPoints == Hero->MovePoints && ReachRevision == MapRevision && bReachShroud == bShroud)
	{
		return;
	}
	const AHexPlanet* Planet = GetPlanet(Hero->Layer);
	if (!Planet || !Planet->IsBuilt())
	{
		return;
	}
	const int32 NumCells = Planet->GetGrid().GetCells().Num();

	HexHeroPath::FField Ways;
	FindFieldFor(*Hero, Ways, !IsPlayerAi(Hero->Player), Hero->MovePoints);
	TArray<int32> Cells[NumPlanetLayers];
	TSet<int32> Added;
	const auto Add = [&](int32 Layer, int32 Cell)
	{
		bool bAlready = false;
		Added.Add(Layer * NumCells + Cell, &bAlready);
		if (!bAlready)
		{
			Cells[Layer].Add(Cell);
		}
	};
	int32 Steps = 0;
	for (int32 Each = 0; Each < Ways.Costs.Num(); ++Each)
	{
		if (Ways.Costs[Each] >= 0.0f && Ways.Costs[Each] <= Hero->MovePoints + KINDA_SMALL_NUMBER)
		{
			Add(Each / NumCells, Each % NumCells);
			Steps += Each % NumCells != Hero->Cell ? 1 : 0;
		}
	}
	// the mines, mills and castles it can visit today from a cell it reaches, a castle with its three cells
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		const AHexPlanet* Each = GetPlanet(EHexPlanetLayer(Layer));
		if (!Each || Each->GetGrid().GetCells().Num() != NumCells)
		{
			continue;
		}
		const TArray<FHexSphereCell>& GridCells = Each->GetGrid().GetCells();
		const int32 NumReached = Cells[Layer].Num();
		for (int32 Index = 0; Index < NumReached; ++Index)
		{
			for (const int32 Neighbor : GridCells[Cells[Layer][Index]].Neighbors)
			{
				if (Added.Contains(Layer * NumCells + Neighbor) || IsHidden(Hero->Player, EHexPlanetLayer(Layer), Neighbor))
				{
					continue;
				}
				const FHexCastle* Castle = Castles ? Castles->FindCastleAt(EHexPlanetLayer(Layer), Neighbor) : nullptr;
				if (!Castle && !(Sites && Sites->FindBlockerAt(EHexPlanetLayer(Layer), Neighbor)))
				{
					continue;
				}
				const float Cost = GetSiteWayCost(Ways, EHexPlanetLayer(Layer), Neighbor);
				if (Cost < 0.0f || Cost > Hero->MovePoints + KINDA_SMALL_NUMBER)
				{
					continue;
				}
				++Steps;
				if (Castle)
				{
					for (const int32 CastleCell : Castle->Cells)
					{
						Add(Layer, CastleCell);
					}
				}
				else
				{
					Add(Layer, Neighbor);
				}
			}
		}
	}
	// a cell another hero stands on beside one it reaches cuts no hole into what is shown, though nobody walks onto it
	for (const FHexHero& Other : Heroes)
	{
		const int32 OtherNode = GetNode(Other.Layer, Other.Cell);
		if (Other.Id == Hero->Id || Other.CastleId != 0 || Added.Contains(OtherNode) || IsHidden(Hero->Player, Other.Layer, Other.Cell))
		{
			continue;
		}
		const AHexPlanet* Each = GetPlanet(Other.Layer);
		if (!Each || !Each->GetGrid().GetCells().IsValidIndex(Other.Cell))
		{
			continue;
		}
		for (const int32 Neighbor : Each->GetGrid().GetCells()[Other.Cell].Neighbors)
		{
			if (Ways.IsReached(GetNode(Other.Layer, Neighbor)))
			{
				Add(int32(Other.Layer), Other.Cell);
				break;
			}
		}
	}
	// the castle it guards is one place with where it stands
	if (const FHexCastle* Own = Hero->CastleId != 0 && Castles ? Castles->FindCastle(Hero->CastleId) : nullptr)
	{
		for (const int32 CastleCell : Own->Cells)
		{
			Add(int32(Own->Layer), CastleCell);
		}
	}

	ReachHero = Hero->Id;
	ReachNode = Node;
	ReachPoints = Hero->MovePoints;
	ReachRevision = MapRevision;
	bReachShroud = bShroud;
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		if (AHexPlanet* Each = GetPlanet(EHexPlanetLayer(Layer)); Each && Each->IsBuilt())
		{
			// a hero that can take no step today shows none
			Each->SetReach(Steps > 0 ? TConstArrayView<int32>(Cells[Layer]) : TConstArrayView<int32>());
		}
	}
	UE_LOG(LogHexHero, Verbose, TEXT("Heroes: %s reaches %d cells of the surface and %d of the underground today"), *Hero->Name, Steps > 0 ? Cells[0].Num() : 0, Steps > 0 ? Cells[1].Num() : 0);
}

void UHexHeroes::FindFieldFor(const FHexHero& Hero, HexHeroPath::FField& OutField, bool bThroughUnknown, float MaxCost) const
{
	const AHexPlanet* Planets[NumPlanetLayers] = {GetPlanet(EHexPlanetLayer::Surface), GetPlanet(EHexPlanetLayer::Underground)};
	const int32 NumCells = Surface ? Surface->GetGrid().GetCells().Num() : 0;
	const AHexPlanet* Planet = Planets[int32(Hero.Layer)];
	if (!Planet || NumCells == 0 || Planet->GetGrid().GetCells().Num() != NumCells)
	{
		OutField = HexHeroPath::FField();
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	// the cells other heroes stand on, and the cost of each cell found once, as a cell is looked at from each of its sides
	TSet<int32> Occupied;
	for (const FHexHero& Other : Heroes)
	{
		if (Other.Id != Hero.Id)
		{
			Occupied.Add(int32(Other.Layer) * NumCells + Other.Cell);
		}
	}
	TArray<float> CellCosts;
	CellCosts.Init(0.0f, NumCells * NumPlanetLayers);
	const UHexPlanetRivers* Rivers = Planets[0] ? Planets[0]->GetRivers() : nullptr;
	// ground its player has not explored is open ground at UnknownCost, whatever lies or stands there, or no ground at all for a hero the
	// game moves
	const bool bShroud = IsShroudOn();
	const TBitArray<>* Explored[NumPlanetLayers] = {bShroud ? Shroud->GetExplored(Hero.Player, EHexPlanetLayer::Surface) : nullptr,
		bShroud ? Shroud->GetExplored(Hero.Player, EHexPlanetLayer::Underground) : nullptr};
	const float Unknown = bThroughUnknown ? FMath::Max(Config->UnknownCost, 0.01f) : -1.0f;
	const auto IsKnown = [&](int32 Layer, int32 Cell)
	{
		return !bShroud || (Explored[Layer] && Explored[Layer]->IsValidIndex(Cell) && (*Explored[Layer])[Cell]);
	};
	const auto GetCost = [&](int32 Layer, int32 From, int32 Side, int32 To)
	{
		float& Cost = CellCosts[Layer * NumCells + To];
		if (!IsKnown(Layer, To))
		{
			// but for a void seen from a passage beside it
			if (Cost == 0.0f)
			{
				Cost = IsSeenVoid(Hero.Player, EHexPlanetLayer(Layer), To) ? -1.0f : Unknown;
			}
			return Cost;
		}
		if (Cost == 0.0f)
		{
			Cost = Occupied.Contains(Layer * NumCells + To) ? -1.0f : GetCellCost(*Planets[Layer], To);
		}
		// the step out of the castle it guards costs nothing
		if (Cost >= 0.0f && Hero.CastleId != 0 && Layer == int32(Hero.Layer) && From == Hero.Cell)
		{
			return 0.0f;
		}
		return Cost < 0.0f ? -1.0f : Cost * (Layer == 0 && Rivers ? Rivers->GetCrossingFactor(From, To) : 1.0f);
	};
	// a hero may go through an entrance or exit its player has seen, to the same cell of the other layer, where no hero it knows of stands;
	// a hero the game moves only where its player has seen the other side too, as it keeps to what it explored
	const auto GetLink = [&](int32 Layer, int32 Cell)
	{
		const int32 Other = 1 - Layer;
		if (!Planets[Other] || Planets[Other]->GetGrid().GetCells().Num() != NumCells || !IsKnown(Layer, Cell) || !IsUsableEntrance(*Planets[Layer], Cell)
			|| (IsKnown(Other, Cell) ? Occupied.Contains(Other * NumCells + Cell) : !bThroughUnknown))
		{
			return -1.0f;
		}
		return FMath::Max(Config->EntranceCost, 0.0f);
	};
	HexHeroPath::FindLayerField(Planet->GetGrid(), int32(Hero.Layer), Hero.Cell, MaxCost >= 0.0f ? MaxCost : GetMaxMovePoints(Hero) * Config->PathMaxDays, GetCost, GetLink, OutField);
	UE_LOG(LogHexHero, Verbose, TEXT("Heroes: the ways of %s from cell %d found in %.2f ms"), *Hero.Name, Hero.Cell, (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

float UHexHeroes::GetVisitCost(const AHexPlanet& Planet, int32 From, int32 SiteCell) const
{
	const UHexPlanetRivers* Rivers = Planet.GetLayer() == EHexPlanetLayer::Surface ? Planet.GetRivers() : nullptr;
	return FMath::Max(GetCellCost(Planet, SiteCell, true), 0.0f) * (Rivers ? Rivers->GetCrossingFactor(From, SiteCell) : 1.0f);
}

bool UHexHeroes::FindWay(const FHexHero& Hero, const HexHeroPath::FField& Ways, EHexPlanetLayer TargetLayer, int32 Target, TArray<int32>& OutPath, bool& bOutVisit) const
{
	OutPath.Reset();
	bOutVisit = false;
	const AHexPlanet* Planet = GetPlanet(TargetLayer);
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Target))
	{
		return false;
	}
	const int32 NumCells = Planet->GetGrid().GetCells().Num();
	// what stands on ground its player has not explored is unknown, so the cell is only one the hero cannot stand on
	bOutVisit = ((Sites && Sites->FindBlockerAt(TargetLayer, Target)) || (Castles && Castles->FindCastleAt(TargetLayer, Target)) || IsFoeAt(Hero.Player, TargetLayer, Target))
		&& !IsHidden(Hero.Player, TargetLayer, Target);
	TArray<int32> Nodes;
	if (!bOutVisit)
	{
		if (!Ways.GetPath(GetNode(TargetLayer, Target), Nodes) || Nodes.IsEmpty())
		{
			return false;
		}
	}
	else
	{
		// to the cell beside the site from which the whole way, with the visit, is the cheapest; that may be where the hero stands
		int32 Best = INDEX_NONE;
		if (GetSiteWayCost(Ways, TargetLayer, Target, &Best, Hero.Player) < 0.0f || !Ways.GetPath(Best, Nodes))
		{
			return false;
		}
	}
	// the cells in the order they are entered; a cell that comes again is the step through the entrance or exit there to the other layer
	for (const int32 Node : Nodes)
	{
		OutPath.Add(Node % NumCells);
	}
	return true;
}

int32 UHexHeroes::GetCastleEntryCell(EHexPlanetLayer Layer, int32 From, int32 SiteCell) const
{
	const FHexCastle* Castle = Castles ? Castles->FindCastleAt(Layer, SiteCell) : nullptr;
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Castle || !Planet || !Planet->GetGrid().GetCells().IsValidIndex(From))
	{
		return SiteCell;
	}
	const auto& Around = Planet->GetGrid().GetCells()[From].Neighbors;
	if (Around.Contains(SiteCell))
	{
		return SiteCell;
	}
	for (const int32 Cell : Castle->Cells)
	{
		if (Around.Contains(Cell))
		{
			return Cell;
		}
	}
	return SiteCell;
}

bool UHexHeroes::IsFoeAt(int32 Player, EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexHero* Other = FindHeroAt(Layer, Cell);
	return (Neutrals && Neutrals->FindAt(Layer, Cell)) || (Other && Other->Player != Player && Other->CastleId == 0);
}

float UHexHeroes::GetSiteWayCost(const HexHeroPath::FField& Ways, EHexPlanetLayer Layer, int32 SiteCell, int32* OutNode, int32 ForPlayer) const
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(SiteCell))
	{
		return -1.0f;
	}
	int32 Best = INDEX_NONE;
	float BestCost = -1.0f;
	// a mine or a mill a hero stands on is guarded: a hero of another player fights it there, one of its own never comes
	const FHexHero* Guard = Sites && Sites->FindBlockerAt(Layer, SiteCell) ? FindHeroAt(Layer, SiteCell) : nullptr;
	if (Guard && (ForPlayer == INDEX_NONE || Guard->Player == ForPlayer))
	{
		if (OutNode)
		{
			*OutNode = INDEX_NONE;
		}
		return -1.0f;
	}
	// a pile is walked onto
	if (Sites && Sites->FindPileAt(Layer, SiteCell))
	{
		const int32 Node = GetNode(Layer, SiteCell);
		if (OutNode)
		{
			*OutNode = Ways.IsReached(Node) ? Node : int32(INDEX_NONE);
		}
		return Ways.IsReached(Node) ? Ways.Costs[Node] : -1.0f;
	}
	// a castle is gone into by the gate nearest the way there, from the cell before it, whichever of its cells was clicked, as the developer
	// asked on 2026-11-12; beside any of its cells only when no gate is reached
	if (const FHexCastle* Castle = Castles ? Castles->FindCastleAt(Layer, SiteCell) : nullptr)
	{
		TArray<int32> Outside;
		TArray<int32> Inside;
		Castles->GetGateCells(*Castle, Outside, Inside);
		for (int32 Gate = 0; Gate < Outside.Num(); ++Gate)
		{
			const int32 Node = GetNode(Layer, Outside[Gate]);
			if (Ways.IsReached(Node))
			{
				const float Cost = Ways.Costs[Node] + GetVisitCost(*Planet, Outside[Gate], Inside[Gate]);
				if (Best == INDEX_NONE || Cost < BestCost)
				{
					Best = Node;
					BestCost = Cost;
				}
			}
		}
		if (Best == INDEX_NONE)
		{
			for (const int32 Cell : Castle->Cells)
			{
				for (const int32 Neighbor : Planet->GetGrid().GetCells()[Cell].Neighbors)
				{
					const int32 Node = GetNode(Layer, Neighbor);
					if (!Castles->FindCastleAt(Layer, Neighbor) && Ways.IsReached(Node))
					{
						const float Cost = Ways.Costs[Node] + GetVisitCost(*Planet, Neighbor, Cell);
						if (Best == INDEX_NONE || Cost < BestCost)
						{
							Best = Node;
							BestCost = Cost;
						}
					}
				}
			}
		}
		if (OutNode)
		{
			*OutNode = Best;
		}
		return BestCost;
	}
	for (const int32 Neighbor : Planet->GetGrid().GetCells()[SiteCell].Neighbors)
	{
		const int32 Node = GetNode(Layer, Neighbor);
		if (Ways.IsReached(Node))
		{
			const float Cost = Ways.Costs[Node] + GetVisitCost(*Planet, Neighbor, SiteCell);
			if (Best == INDEX_NONE || Cost < BestCost)
			{
				Best = Node;
				BestCost = Cost;
			}
		}
	}
	if (OutNode)
	{
		*OutNode = Best;
	}
	return BestCost;
}

bool UHexHeroes::VisitSite(FHexHero& Hero, FString& OutMessage)
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	int32 SiteCell = Hero.VisitCell;
	const FHexCastle* Castle = Castles ? Castles->FindCastleAt(Hero.Layer, SiteCell) : nullptr;
	// into a castle by the cell of it beside the hero, at whichever of its gates the way ended
	SiteCell = Castle ? GetCastleEntryCell(Hero.Layer, Hero.Cell, SiteCell) : SiteCell;
	if (!Planet || !Sites || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell) || !Planet->GetGrid().GetCells()[Hero.Cell].Neighbors.Contains(SiteCell)
		|| (!Castle && !Sites->FindBlockerAt(Hero.Layer, SiteCell) && !IsFoeAt(Hero.Player, Hero.Layer, SiteCell)))
	{
		Hero.VisitCell = INDEX_NONE;
		Hero.TargetCell = INDEX_NONE;
		return false;
	}
	// a visit costs what a step onto that cell would, but going into a castle of its own costs nothing, as the developer asked on 2026-11-11
	const float Cost = Castle && Castle->Owner == Hero.Player ? 0.0f : GetVisitCost(*Planet, Hero.Cell, SiteCell);
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
	// a neutral stack, a hero of another player, or a castle of another that a garrison or a hero defends, is fought (UHexBattles), as the
	// developer decided on 2026-11-12; a mine a hero of another player guards is fought for there too
	const FHexNeutral* Neutral = Neutrals ? Neutrals->FindAt(Hero.Layer, SiteCell) : nullptr;
	const FHexHero* Foe = FindHeroAt(Hero.Layer, SiteCell);
	Foe = Foe && Foe->Player != Hero.Player && Foe->CastleId == 0 ? Foe : nullptr;
	const bool bDefended = Castle && Castle->Owner != Hero.Player
		&& (Castles->FindGuard(*Castle) || Castle->Garrison.ContainsByPredicate([](const FHexStack& Each) { return Each.Count > 0; }));
	if (Battles && (Neutral || Foe || bDefended))
	{
		FHexBattleRequest Request;
		Request.Kind = Neutral ? EHexBattleKind::Neutral : Foe ? EHexBattleKind::Hero : EHexBattleKind::Castle;
		Request.Attacker = Hero.Id;
		Request.DefenderHero = Foe ? Foe->Id : 0;
		Request.NeutralId = Neutral ? Neutral->Id : 0;
		Request.CastleId = Castle ? Castle->Id : 0;
		Request.Layer = Hero.Layer;
		Request.Cell = SiteCell;
		Hero.VisitCell = INDEX_NONE;
		Hero.TargetCell = INDEX_NONE;
		Hero.Route.Reset();
		FString Problem;
		if (!Battles->Begin(Request, Problem))
		{
			Hero.MovePoints += Cost;
			OutMessage = Problem.IsEmpty() ? FString(TEXT("there is nothing to fight")) : Problem;
			return false;
		}
		++MapRevision;
		return true;
	}
	// of what a hero the game moves does the person hears what it sees, and that a mine of its own was taken
	const FHexSite* Site = Sites->FindSiteAt(Hero.Layer, SiteCell);
	const bool bTold = !bPlaying || !IsPlayerAi(Hero.Player) || (Site && Site->Owner == ViewPlayer) || (Castle && Castle->Owner == ViewPlayer) || IsCellSeen(Hero.Layer, Hero.Cell);
	// a castle: its own opens to it, another is taken if the hero is strong enough
	const FString Found = Castle ? Castles->Visit(Hero, Castle->Id, SiteCell, bTold) : Sites->Visit(Hero.Player, Hero.Name, Hero.Layer, SiteCell, GetWeek(), bTold);
	if (Castle)
	{
		// a hero chosen that went in is chosen with the castle, and a castle that was taken sees for its new owner
		if (Hero.Id == SelectedId && bPlaying)
		{
			SelectedCastleId = Hero.CastleId;
		}
		ExploreCastles();
	}
	else if (Sites->FindBlockerAt(Hero.Layer, SiteCell) && !FindHeroAt(Hero.Layer, SiteCell))
	{
		// a mine or a mill is taken by going onto its cell, as the developer decided on 2026-11-11 (it was taken from beside before): the
		// hero stands at its place there and guards it, as no other hero comes onto a cell one stands on. Nothing walks through that cell
		const FVector3d Outside = Hero.Position;
		Hero.Trail.Add(Hero.Cell);
		Hero.FromCell = Hero.Cell = SiteCell;
		Hero.bPlaced = false;
		FVector3d Place;
		if (bTold && !Outside.IsNearlyZero() && Sites->GetHeroPoint(Hero.Layer, SiteCell, Place))
		{
			const UWorld* World = Surface ? Surface->GetWorld() : nullptr;
			Hero.GlideStart = World ? World->GetTimeSeconds() : 0.0;
			Hero.GlideTime = Config->SiteWalkTime;
			Hero.bGliding = true;
			Hero.GlidePoints[0] = Outside;
			Hero.GlidePoints[1] = (Outside + Place) * 0.5;
			Hero.GlidePoints[2] = Place;
		}
		Explore(Hero);
	}
	++ListRevision;
	if (!Found.IsEmpty())
	{
		if (bTold)
		{
			OnMessage.Broadcast(Found);
		}
		else
		{
			UE_LOG(LogHexHero, Log, TEXT("Heroes: unseen, %s"), *Found);
		}
	}
	Hero.VisitCell = INDEX_NONE;
	Hero.TargetCell = INDEX_NONE;
	++MapRevision;
	return true;
}

void UHexHeroes::TakePile(FHexHero& Hero)
{
	if (!Sites || !Sites->FindPileAt(Hero.Layer, Hero.Cell))
	{
		return;
	}
	// of what a hero the game moves does the person hears what it sees
	const bool bTold = !bPlaying || !IsPlayerAi(Hero.Player) || IsCellSeen(Hero.Layer, Hero.Cell);
	const FString Found = Sites->Visit(Hero.Player, Hero.Name, Hero.Layer, Hero.Cell, GetWeek(), bTold);
	if (Found.IsEmpty())
	{
		return;
	}
	if (bTold)
	{
		OnMessage.Broadcast(Found);
	}
	else
	{
		UE_LOG(LogHexHero, Log, TEXT("Heroes: unseen, %s"), *Found);
	}
	++MapRevision;
}

void UHexHeroes::GetStepCosts(const FHexHero& Hero, TConstArrayView<int32> Path, TArray<float>& OutCosts) const
{
	OutCosts.Reset(Path.Num());
	WalkWay(Hero, Path, [&](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
	{
		OutCosts.Add(FMath::Max(GetWayStepCost(Hero, Layer, From, To, bThrough), 0.0f));
		return true;
	});
}

int32 UHexHeroes::FindCheckCell(const FHexHero& Hero, TConstArrayView<int32> Path, EHexPlanetLayer& OutLayer) const
{
	int32 Found = INDEX_NONE;
	if (IsShroudOn() && !IsPlayerAi(Hero.Player))
	{
		WalkWay(Hero, Path, [&](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
		{
			if (IsHidden(Hero.Player, Layer, To))
			{
				Found = To;
				OutLayer = Layer;
			}
			return Found == INDEX_NONE;
		});
	}
	return Found;
}

bool UHexHeroes::CheckRoute(FHexHero& Hero)
{
	const AHexPlanet* Planet = GetPlanet(Hero.TargetLayer);
	const int32 Target = Hero.TargetCell;
	Hero.CheckCell = INDEX_NONE;
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(Target) || (Target == Hero.Cell && Hero.TargetLayer == Hero.Layer) || Hero.Route.IsEmpty())
	{
		return true;
	}
	const auto Stop = [&Hero]()
	{
		Hero.Route.Reset();
		Hero.bEnterAtEnd = false;
		Hero.VisitCell = INDEX_NONE;
	};

	// the best way from here as its player knows the ground now
	HexHeroPath::FField Ways;
	FindFieldFor(Hero, Ways, true);
	TArray<int32> Path;
	bool bVisit = false;
	const bool bFound = FindWay(Hero, Ways, Hero.TargetLayer, Target, Path, bVisit);
	if (!bFound)
	{
		FString Problem;
		if (bVisit || CanStand(Hero.TargetLayer, Target, Hero.Id, &Problem))
		{
			Problem = TEXT("there is no way there after all");
		}
		Stop();
		OnMessage.Broadcast(FString::Printf(TEXT("%s stops: %s"), *Hero.Name, *Problem));
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s stops on cell %d, as cell %d cannot be reached: %s"), *Hero.Name, Hero.Cell, Target, *Problem);
		return false;
	}

	// and what the rest of the way it has costs by the same knowledge, if it is open at all
	float Rest = 0.0f;
	bool bOpen = true;
	WalkWay(Hero, Hero.Route, [&](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
	{
		const float Cost = GetWayStepCost(Hero, Layer, From, To, bThrough);
		bOpen = Cost >= 0.0f;
		Rest += FMath::Max(Cost, 0.0f);
		return bOpen;
	});
	// a place that turned out to be a site is walked up to, not onto
	bOpen &= bVisit == (Hero.VisitCell != INDEX_NONE);
	float Best = 0.0f;
	GetWayCost(Hero, Path, Best);
	if (!bOpen || Best < Rest - FMath::Max(Config->RerouteStopPoints, 0.0f) - UE_KINDA_SMALL_NUMBER)
	{
		// the cell it was sent to is kept, so the new way shows and the person sends it on or elsewhere
		Stop();
		OnMessage.Broadcast(FString::Printf(TEXT("%s stops: %s"), *Hero.Name, bOpen ? TEXT("there is a better way now") : TEXT("the way it had is blocked")));
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s stops on cell %d to have its way to cell %d found again: %s, the best costs %.2f"), *Hero.Name, Hero.Cell, Target,
			bOpen ? *FString::Printf(TEXT("the rest of it costs %.2f"), Rest) : TEXT("it is blocked"), Best);
		return false;
	}

	// its way holds: it walks on, to the next cell of it that is still under the fog
	Hero.CheckCell = FindCheckCell(Hero, Hero.Route, Hero.CheckLayer);
	Hero.bEnterAtEnd = false;
	UE_LOG(LogHexHero, Verbose, TEXT("Heroes: the way of %s to cell %d holds at cell %d, %.2f points left, checked next at cell %d"), *Hero.Name, Target, Hero.Cell,
		Rest, Hero.CheckCell);
	return true;
}

bool UHexHeroes::GetWayCost(const FHexHero& Hero, TConstArrayView<int32> Path, float& OutCost) const
{
	OutCost = 0.0f;
	bool bOpen = true;
	WalkWay(Hero, Path, [&](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
	{
		const float Cost = GetWayStepCost(Hero, Layer, From, To, bThrough);
		bOpen = Cost >= 0.0f;
		OutCost += FMath::Max(Cost, 0.0f);
		return bOpen;
	});
	return bOpen;
}

bool UHexHeroes::EndsWithStep(const FHexHero& Hero, TConstArrayView<int32> Path) const
{
	// its last step is one over the ground, not one through an entrance or exit
	return !Path.IsEmpty() && Path.Last() != (Path.Num() >= 2 ? Path[Path.Num() - 2] : Hero.Cell);
}

bool UHexHeroes::StartNextStep(FHexHero& Hero)
{
	// at the first cell of its way that lay under the fog of war the way is checked, with what the hero has seen by now
	if (Hero.CheckCell != INDEX_NONE && Hero.Cell == Hero.CheckCell && Hero.Layer == Hero.CheckLayer && !CheckRoute(Hero))
	{
		return false;
	}
	if (Hero.Route.IsEmpty())
	{
		return false;
	}
	const int32 Next = Hero.Route[0];
	// a cell that comes again is the step through the entrance or exit the hero stands on, to the other layer; the camera goes with the hero
	// a person plays
	if (Next == Hero.Cell)
	{
		FString Message;
		const bool bFollowed = IsPersonTurn() && Hero.Id == SelectedId;
		const UWorld* World = Surface ? Surface->GetWorld() : nullptr;
		// where the person sees it, it walks into the mouth first, growing smaller, and goes through when it is there
		const AHexPlanet* Before = GetPlanet(Hero.Layer);
		const bool bShown = World && Before && Config->EntranceWalkTime > 0.0f && IsCellSeen(Hero.Layer, Hero.Cell) && Hero.bPlaced;
		const auto GetMouth = [&](const AHexPlanet& Planet) { return Planet.GetEntranceMouth(Hero.Cell); };
		if (bShown && !Hero.bEnteringMouth && IsUsableEntrance(*Before, Hero.Cell) && !FindHeroAt(GetOtherLayer(Hero.Layer), Hero.Cell)
			&& HexHeroPath::CanStep(FMath::Max(Config->EntranceCost, 0.0f), Hero.MovePoints, GetMaxMovePoints(Hero)))
		{
			const FVector3d Mouth = GetMouth(*Before);
			Hero.bEnteringMouth = true;
			Hero.GlideStart = World->GetTimeSeconds();
			Hero.GlideTime = Config->EntranceWalkTime;
			Hero.bGliding = true;
			Hero.GlidePoints[0] = Hero.Position;
			Hero.GlidePoints[1] = (Hero.Position + Mouth) * 0.5;
			Hero.GlidePoints[2] = Mouth;
			Hero.GlideScaleFrom = 1.0f;
			Hero.GlideScaleTo = Config->EntranceShrink;
			Hero.GlideFade = 1;
			Hero.PassColor = Before->GetEntranceColor(Hero.Cell);
			Hero.StepAlpha = 1.0f;
			Hero.Speed = 0.0f;
			Hero.WalkWait = Config->EntranceWalkTime;
			return true;
		}
		const bool bWalkedIn = Hero.bEnteringMouth;
		Hero.bEnteringMouth = false;
		Hero.bGliding = false;
		Hero.GlideScaleFrom = Hero.GlideScaleTo = Hero.PassScale = 1.0f;
		Hero.GlideFade = 0;
		Hero.PassFade = 0.0f;
		if (!PassEntrance(Hero, Message, bFollowed))
		{
			Hero.bPlaced = false;
			Hero.Route.Reset();
			Hero.bEnterAtEnd = false;
			if (!Hero.bOutOfPoints && !Message.IsEmpty())
			{
				OnMessage.Broadcast(Message);
			}
			return false;
		}
		Hero.bOutOfPoints = false;
		Hero.FromCell = Hero.Cell;
		Hero.Route.RemoveAt(0);
		// the step itself takes no time: it walks on once the camera has come after it
		Hero.StepAlpha = 1.0f;
		Hero.Speed = 0.0f;
		Hero.WalkWait = bFollowed ? Config->EntrancePause : 0.0f;
		// and out of the mouth on the other side, growing again, once the camera is there
		if (const AHexPlanet* After = GetPlanet(Hero.Layer); bWalkedIn && After && World)
		{
			const FVector3d Mouth = GetMouth(*After);
			const FVector3d Middle = After->GetGrid().GetCells()[Hero.Cell].Center * After->FindGroundRadius(Hero.Cell, After->GetGrid().GetCells()[Hero.Cell].Center);
			Hero.GlideStart = World->GetTimeSeconds() + double(Hero.WalkWait);
			Hero.GlideTime = Config->EntranceWalkTime;
			Hero.bGliding = true;
			Hero.GlidePoints[0] = Mouth;
			Hero.GlidePoints[1] = (Mouth + Middle) * 0.5;
			Hero.GlidePoints[2] = Middle;
			Hero.GlideScaleFrom = Config->EntranceShrink;
			Hero.GlideScaleTo = 1.0f;
			Hero.PassScale = Config->EntranceShrink;
			Hero.GlideFade = -1;
			Hero.PassFade = 1.0f;
			Hero.WalkWait += Config->EntranceWalkTime;
		}
		return true;
	}
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
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
	// the cells it left stay on the way shown until it stops
	if (!(Castles && Castles->FindCastleAt(Hero.Layer, Hero.Cell)))
	{
		Hero.Trail.Add(Hero.Cell);
	}
	Hero.FromCell = Hero.Cell;
	Hero.Cell = Next;
	Hero.Route.RemoveAt(0);
	// out of the castle it guarded, by the gate nearest where it goes, which opens for it: it waits for the door to lift
	Hero.GateStepFrom = INDEX_NONE;
	if (Hero.CastleId != 0)
	{
		const AHexPlanet* Left = GetPlanet(Hero.Layer);
		if (Left && Castles->OpenGate(Hero.CastleId, Left->GetGrid().GetCells()[Next].Center, Hero.GlidePoints[1], Hero.GlidePoints[0]))
		{
			Hero.GateStepFrom = Hero.FromCell;
			Hero.WalkWait = IsCellSeen(Hero.Layer, Next) ? Castles->GetConfig()->GateRiseTime * Castles->GetConfig()->GateWalkDelay : 0.0f;
		}
		// chosen with its castle, it is chosen alone once it is out
		if (SelectedId == Hero.Id && SelectedCastleId == Hero.CastleId)
		{
			SelectedCastleId = 0;
			++ListRevision;
		}
		Hero.CastleId = 0;
		Castles->NoteChanged();
	}
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
	if (Hero.Cell == Hero.TargetCell && Hero.Layer == Hero.TargetLayer)
	{
		// where it was sent to: the pile that lies there is its
		TakePile(Hero);
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
		// the way through costs points too
		bNoPoints |= Hero.bOutOfPoints;
		Hero.bOutOfPoints = false;
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
		ChooseHero(Next->Id);
		OnHeroChosen.Broadcast(SelectedId);
	}
}

bool UHexHeroes::PassEntrance(FHexHero& Hero, FString& OutMessage, bool bBroadcast)
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
	// the way through costs points of movement, as any step
	const float Cost = FMath::Max(Config->EntranceCost, 0.0f);
	if (!HexHeroPath::CanStep(Cost, Hero.MovePoints, GetMaxMovePoints(Hero)))
	{
		Hero.bOutOfPoints = true;
		OutMessage = FString::Printf(TEXT("%s has no points of movement left to go through today"), *Hero.Name);
		return false;
	}
	Hero.MovePoints = FMath::Max(Hero.MovePoints - Cost, 0.0f);
	RemoveBody(Hero.Id);
	Hero.Layer = Other;
	Hero.Trail.Reset();
	Hero.CameOutCell = Hero.Cell;
	Hero.FromCell = Hero.Cell;
	Hero.bPlaced = false;
	Hero.Facing = FVector3d::ZeroVector;
	Hero.bCameThrough = true;
	++MapRevision;
	Explore(Hero);
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s goes %s through cell %d"), *Hero.Name, Other == EHexPlanetLayer::Underground ? TEXT("down into the underground") : TEXT("up to the surface"), Hero.Cell);
	if (bBroadcast)
	{
		OnHeroTravelled.Broadcast(Hero.Id);
	}
	return true;
}

bool UHexHeroes::Travel(FHexHero& Hero, FString& OutMessage, bool bBroadcast)
{
	if (!PassEntrance(Hero, OutMessage, bBroadcast))
	{
		return false;
	}
	Hero.bWalking = false;
	Hero.StepAlpha = 0.0f;
	Hero.TargetCell = INDEX_NONE;
	Hero.VisitCell = INDEX_NONE;
	return true;
}

const FHexHero* UHexHeroes::FindSallyTarget(const FHexHero& Hero, EHexPlanetLayer Layer, int32 Cell, int32& OutCastleCell) const
{
	const FHexCastle* Castle = Castles && Hero.CastleId != 0 && Layer == Hero.Layer ? Castles->FindCastle(Hero.CastleId) : nullptr;
	const AHexPlanet* Planet = Castle ? GetPlanet(Layer) : nullptr;
	const FHexHero* Other = Planet && Planet->GetGrid().GetCells().IsValidIndex(Cell) ? FindHeroAt(Layer, Cell) : nullptr;
	if (!Other || Other->Id == Hero.Id || Other->CastleId != 0 || Other->bWalking)
	{
		return nullptr;
	}
	for (const int32 Neighbor : Planet->GetGrid().GetCells()[Cell].Neighbors)
	{
		if (Castle->HasCell(Neighbor))
		{
			OutCastleCell = Neighbor;
			return Other;
		}
	}
	return nullptr;
}

bool UHexHeroes::Sally(FHexHero& Hero, int32 Cell, FString& OutMessage)
{
	int32 CastleCell = INDEX_NONE;
	const FHexHero* Found = FindSallyTarget(Hero, Hero.Layer, Cell, CastleCell);
	FHexHero* Other = Found ? FindHeroMutable(Found->Id) : nullptr;
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Other || !Planet)
	{
		return false;
	}
	const int32 CastleId = Hero.CastleId;
	const bool bTold = !bPlaying || !IsPlayerAi(Hero.Player) || IsCellSeen(Hero.Layer, Cell);
	Hero.TargetCell = INDEX_NONE;
	FString Result;
	if (Other->Player == Hero.Player)
	{
		// one of its own: the two swap places, as when that one is sent into the castle, for no points
		Result = Castles->Visit(*Other, CastleId, CastleCell, bTold);
	}
	else
	{
		// of another player: this is the attack on the castle, as the developer decided on 2026-11-11, so the hero stays in it and that one
		// attacks it, fought as a battle since 2026-11-12 (UHexBattles); the order costs what the step out would
		const float Cost = GetVisitCost(*Planet, CastleCell, Cell);
		if (!HexHeroPath::CanStep(Cost, Hero.MovePoints, GetMaxMovePoints(Hero)))
		{
			Hero.bOutOfPoints = true;
			OutMessage = FString::Printf(TEXT("%s has no points of movement left for that today"), *Hero.Name);
			return false;
		}
		Hero.MovePoints = FMath::Max(Hero.MovePoints - Cost, 0.0f);
		FHexBattleRequest Request;
		Request.Kind = EHexBattleKind::Castle;
		Request.Attacker = Other->Id;
		Request.CastleId = CastleId;
		Request.Layer = Hero.Layer;
		Request.Cell = CastleCell;
		FString Problem;
		if (!Battles || !Battles->Begin(Request, Problem))
		{
			// with nothing to defend it the castle is taken as before
			Result = Castles->Visit(*Other, CastleId, CastleCell, bTold);
		}
		ExploreCastles();
	}
	++ListRevision;
	++MapRevision;
	if (bTold && !Result.IsEmpty())
	{
		OnMessage.Broadcast(Result);
	}
	return true;
}

bool UHexHeroes::Send(int32 Id, int32 Cell, FString& OutMessage)
{
	const FHexHero* Hero = FindHero(Id);
	return Send(Id, Hero ? Hero->Layer : EHexPlanetLayer::Surface, Cell, OutMessage);
}

bool UHexHeroes::Send(int32 Id, EHexPlanetLayer TargetLayer, int32 Cell, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(Id);
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	const AHexPlanet* TargetPlanet = GetPlanet(TargetLayer);
	if (!Hero || !Planet || !TargetPlanet || !TargetPlanet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		OutMessage = TEXT("There is no such hero or cell");
		return false;
	}
	Hero->TargetCell = Cell;
	Hero->TargetLayer = TargetLayer;
	Hero->bSkipped = false;

	// out of the castle it guards onto a hero beside it: one of its own swaps places with it, another attacks the castle
	if (int32 CastleCell = INDEX_NONE; !Hero->bWalking && FindSallyTarget(*Hero, TargetLayer, Cell, CastleCell))
	{
		return Sally(*Hero, Cell, OutMessage);
	}

	// its own cell: through the entrance or exit it stands on, and a hero walking onto it stops there
	if (Cell == Hero->Cell && TargetLayer == Hero->Layer)
	{
		if (Hero->bWalking)
		{
			Hero->Route.Reset();
			Hero->bEnterAtEnd = false;
			return true;
		}
		Hero->TargetCell = INDEX_NONE;
		// a pile it stopped on, on a way that went further
		if (Sites && Sites->FindPileAt(Hero->Layer, Cell))
		{
			TakePile(*Hero);
			return true;
		}
		// the mine or the mill it stands on: a mill gives again in a new week
		if (Sites && Sites->FindBlockerAt(Hero->Layer, Cell))
		{
			const FString Found = Sites->Visit(Hero->Player, Hero->Name, Hero->Layer, Cell, GetWeek(), true);
			if (!Found.IsEmpty())
			{
				OnMessage.Broadcast(Found);
			}
			++ListRevision;
			return true;
		}
		// the cell before an entrance is ground as any other since 2026-11-12: a hero goes through by being sent to the mouth, its cell on
		// the other layer
		return false;
	}

	// a hero a person moves may cross ground its player has not explored, which is taken to be open, and go through the underground
	HexHeroPath::FField Ways;
	FindFieldFor(*Hero, Ways, !IsPlayerAi(Hero->Player));
	TArray<int32> Path;
	bool bVisit = false;
	if (!FindWay(*Hero, Ways, TargetLayer, Cell, Path, bVisit))
	{
		FString Problem;
		// a mine or a mill a hero stands on is guarded
		const FHexHero* Guard = bVisit && Sites && Sites->FindBlockerAt(TargetLayer, Cell) ? FindHeroAt(TargetLayer, Cell) : nullptr;
		OutMessage = Guard ? FString::Printf(TEXT("%s cannot take it: %s guards it"), *Hero->Name, *Guard->Name)
			: bVisit || CanStand(TargetLayer, Cell, Hero->Id, &Problem) ? FString::Printf(TEXT("%s finds no way there within %d days"), *Hero->Name, Config->PathMaxDays)
			: FString::Printf(TEXT("%s cannot go there: %s"), *Hero->Name, *Problem);
		return false;
	}
	Hero->VisitCell = bVisit ? Cell : INDEX_NONE;
	// a site right beside the hero needs no way
	if (bVisit && Path.IsEmpty() && !Hero->bWalking)
	{
		const bool bVisited = VisitSite(*Hero, OutMessage);
		Hero->bOutOfPoints = false;
		return bVisited;
	}

	// over such ground the way is checked where the hero reaches the first cell of it that lies under the fog
	Hero->CheckCell = FindCheckCell(*Hero, Path, Hero->CheckLayer);
	Hero->bEnterAtEnd = false;
	Hero->Route = MoveTemp(Path);
	if (!Hero->bWalking)
	{
		Hero->FromCell = Hero->Cell;
		Hero->StepAlpha = 0.0f;
		Hero->Speed = 0.0f;
		Hero->WalkWait = 0.0f;
		const int32 Steps = Hero->Route.Num();
		const int32 FromCell = Hero->Cell;
		if (!StartNextStep(*Hero))
		{
			// a step that is blocked has said so itself
			if (Hero->bOutOfPoints)
			{
				OutMessage = FString::Printf(TEXT("%s has no points of movement left for that step today"), *Hero->Name);
			}
			Hero->bOutOfPoints = false;
			return false;
		}
		Hero->bWalking = true;
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s sets out from cell %d to cell %d of the %s, %d steps"), *Hero->Name, FromCell, Cell, GetPlanetLayerName(TargetLayer), Steps);
		if (IsPersonTurn() && Id == SelectedId)
		{
			OnHeroSent.Broadcast(Id);
		}
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
	if (Castles)
	{
		Castles->Restart();
	}
	for (FHexHero& Hero : Heroes)
	{
		RemoveBody(Hero.Id);
		ResetHero(Hero);
	}
	if (Sites)
	{
		Sites->Restart();
	}
	if (Neutrals)
	{
		Neutrals->Restart();
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
	// on the cell of a mine or a mill at its place there, which the mesh leaves free
	if (FVector3d Place; Sites && Sites->GetHeroPoint(Planet.GetLayer(), Cell, Place))
	{
		return Place.GetSafeNormal();
	}
	return Center;
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
	// a way of three points, walked by a share of it
	const auto AlongWay = [](const FVector3d Points[3], double Share, FVector3d& OutDirection)
	{
		const double First = FVector3d::Distance(Points[0], Points[1]);
		const double Second = FVector3d::Distance(Points[1], Points[2]);
		const double At = FMath::Clamp(Share, 0.0, 1.0) * (First + Second);
		const bool bFirst = At < First || Second <= UE_DOUBLE_SMALL_NUMBER;
		OutDirection = bFirst ? Points[1] - Points[0] : Points[2] - Points[1];
		return bFirst ? FMath::Lerp(Points[0], Points[1], First > UE_DOUBLE_SMALL_NUMBER ? At / First : 1.0) : FMath::Lerp(Points[1], Points[2], (At - First) / Second);
	};
	// in the castle it guards it stands in the yard; it walks in and out through a gate, which opens for it
	const UWorld* World = Surface ? Surface->GetWorld() : nullptr;
	const double Gliding = World ? (World->GetTimeSeconds() - Hero.GlideStart) / FMath::Max(double(Hero.GlideTime), 0.05) : 2.0;
	const FHexCastle* Inside = Castles && Hero.CastleId != 0 ? Castles->FindCastle(Hero.CastleId) : nullptr;
	FVector3d Yard;
	FVector3d Walked = FVector3d::ZeroVector;
	// one that walked into the mouth of an entrance stays there, small, until it goes through
	Hero.bGliding = Hero.bGliding && !bStep && (Gliding < 1.0 || Hero.bEnteringMouth);
	if (Hero.bGliding)
	{
		Hero.Position = AlongWay(Hero.GlidePoints, Gliding, Walked);
		const float Share = float(FMath::Clamp(Gliding, 0.0, 1.0));
		Hero.PassScale = FMath::Lerp(Hero.GlideScaleFrom, Hero.GlideScaleTo, Share);
		// into the mouth it dissolves over the last of its walk, and out of one it is whole again as early
		const float FadeStart = FMath::Clamp(Config->EntranceFadeStart, 0.0f, 0.95f);
		Hero.PassFade = Hero.GlideFade > 0 ? FMath::SmoothStep(FadeStart, 1.0f, Share) : Hero.GlideFade < 0 ? 1.0f - FMath::SmoothStep(0.0f, 1.0f - FadeStart, Share) : 0.0f;
	}
	else
	{
		Hero.PassScale = Hero.GlideScaleFrom = Hero.GlideScaleTo = 1.0f;
		Hero.GlideFade = 0;
		Hero.PassFade = 0.0f;
	}
	if (Hero.bGliding)
	{
	}
	else if (bStep && Hero.GateStepFrom != INDEX_NONE && Hero.FromCell == Hero.GateStepFrom)
	{
		const FVector3d Points[3] = {Hero.GlidePoints[0], Hero.GlidePoints[1], To * Planet->FindGroundRadius(Hero.Cell, To)};
		Hero.Position = AlongWay(Points, double(Hero.StepAlpha), Walked);
	}
	else if (!bStep && Inside && Castles->GetGuardPoint(*Inside, Yard))
	{
		Hero.Position = Yard;
	}
	if (!Walked.IsNearlyZero())
	{
		Hero.Facing = Walked;
	}

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
	if (TObjectPtr<UMeshComponent>* Body = Bodies.Find(Id))
	{
		if (*Body)
		{
			(*Body)->DestroyComponent();
		}
	}
	Bodies.Remove(Id);
	BodyStates.Remove(Id);
	BodyMaterials.Remove(Id);
}

void UHexHeroes::UpdateBody(FHexHero& Hero)
{
	AHexPlanet* Planet = GetPlanet(Hero.Layer);
	TObjectPtr<UMeshComponent>* Found = Bodies.Find(Hero.Id);
	UMeshComponent* Body = Found ? Found->Get() : nullptr;
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
		// the animated rider where a class names no mesh of its own and the rider is there, else a mesh that stands still
		USkeletalMesh* Rider = Class.Mesh.IsNull() ? Config->RiderMesh.LoadSynchronous() : nullptr;
		if (Rider)
		{
			USkeletalMeshComponent* Animated = NewObject<USkeletalMeshComponent>(Planet);
			Animated->SetSkeletalMesh(Rider);
			Animated->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Animated->SetAnimInstanceClass(UHexHeroAnimInstance::StaticClass());
			Animated->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			Body = Animated;
		}
		else
		{
			UStaticMesh* Mesh = Class.Mesh.IsNull() ? Config->HeroMesh.LoadSynchronous() : Class.Mesh.LoadSynchronous();
			if (!Mesh)
			{
				return;
			}
			UStaticMeshComponent* Still = NewObject<UStaticMeshComponent>(Planet);
			Still->SetStaticMesh(Mesh);
			Body = Still;
		}
		Body->SetupAttachment(Planet->GetRootComponent());
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetCanEverAffectNavigation(false);
		if (UMaterialInterface* Material = Config->HeroMaterial.LoadSynchronous())
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
			Instance->SetVectorParameterValue(TEXT("PlayerColor"), Config->GetPlayer(Hero.Player).Color);
			Instance->SetScalarParameterValue(TEXT("HeroColorShare"), Config->HeroColorShare);
			Instance->SetScalarParameterValue(TEXT("HeroGlow"), Config->HeroGlow);
			Instance->SetScalarParameterValue(TEXT("HeroFlagWave"), Config->HeroFlagWave);
			Instance->SetScalarParameterValue(TEXT("HeroFlagWaveSpeed"), Config->HeroFlagWaveSpeed);
			Instance->SetScalarParameterValue(TEXT("HeroFadeGrain"), Config->HeroFadeGrain);
			Instance->SetScalarParameterValue(TEXT("HeroFadeGlow"), Config->HeroFadeGlow);
			Instance->SetScalarParameterValue(TEXT("HeroFade"), 0.0f);
			BodyStates.FindOrAdd(Hero.Id).Fade = 0.0f;
			Body->SetMaterial(0, Instance);
			BodyMaterials.Add(Hero.Id, Instance);
		}
		Body->RegisterComponent();
		Bodies.Add(Hero.Id, Body);
	}

	// it dissolves in the mouth of an entrance it goes through
	if (TObjectPtr<UMaterialInstanceDynamic>* Material = BodyMaterials.Find(Hero.Id))
	{
		FBodyState& Drawn = BodyStates.FindOrAdd(Hero.Id);
		if (*Material && Drawn.Fade != Hero.PassFade)
		{
			Drawn.Fade = Hero.PassFade;
			(*Material)->SetScalarParameterValue(TEXT("HeroFade"), Hero.PassFade);
			(*Material)->SetVectorParameterValue(TEXT("HeroFadeColor"), Hero.PassColor);
		}
	}

	const FVector3d Up = Hero.Position.GetSafeNormal();
	USkeletalMeshComponent* Animated = Cast<USkeletalMeshComponent>(Body);
	const FQuat Turn = Animated ? FRotator(0.0, double(Config->RiderYaw), 0.0).Quaternion() : FQuat::Identity;
	const FTransform Transform(FRotationMatrix::MakeFromZX(FVector(Up), FVector(Hero.Facing)).ToQuat() * Turn, FVector(Hero.Position),
		FVector(Config->HeroScale * Hero.ViewScale * Hero.PassScale * (Animated ? Config->RiderScale : 1.0f)));
	if (UHexHeroAnimInstance* Anim = Animated ? Cast<UHexHeroAnimInstance>(Animated->GetAnimInstance()) : nullptr)
	{
		// it stands, sets out, walks and stops, and answers being chosen while it stands
		FBodyState& State = BodyStates.FindOrAdd(Hero.Id);
		const bool bMoving = Hero.bWalking || Hero.bGliding;
		const bool bChosen = bPlaying && SelectedId == Hero.Id;
		UAnimSequence* Idle = Config->IdleAnimation.LoadSynchronous();
		UAnimSequence* Move = Config->MoveAnimation.LoadSynchronous();
		const float Blend = Config->AnimBlendTime;
		if (!Anim->GetCurrent())
		{
			Anim->Play(bMoving && Move ? Move : Idle, true, bMoving ? Config->MoveAnimRate : Config->IdleAnimRate, 0.0f);
		}
		else if (bMoving != State.bMoving)
		{
			UAnimSequence* Start = Config->StartAnimation.LoadSynchronous();
			UAnimSequence* Stop = Config->StopAnimation.LoadSynchronous();
			if (bMoving)
			{
				Start ? Anim->Play(Start, false, Config->StartAnimRate, Blend, 0.0f, Move, Config->MoveAnimRate) : Anim->Play(Move, true, Config->MoveAnimRate, Blend);
			}
			else
			{
				Stop ? Anim->Play(Stop, false, Config->StopAnimRate, Blend, Config->StopAnimStart, Idle, Config->IdleAnimRate) : Anim->Play(Idle, true, Config->IdleAnimRate, Blend);
			}
		}
		else if (bChosen && !State.bChosen && !bMoving)
		{
			if (UAnimSequence* Chosen = Config->ChosenAnimation.LoadSynchronous())
			{
				Anim->Play(Chosen, false, Config->ChosenAnimRate, Blend, 0.0f, Idle, Config->IdleAnimRate);
			}
		}
		else if (bMoving && Anim->GetCurrent() == Move)
		{
			// its strides follow its speed
			Anim->SetRate(Config->MoveAnimRate * FMath::Clamp(Hero.Speed / FMath::Max(Config->WalkSpeed, 0.01f), 0.5f, 1.5f));
		}
		State.bMoving = bMoving;
		State.bChosen = bChosen;
	}
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
			// the hoops that run from the hoop of an entrance a way goes through
			PathMaterial->SetScalarParameterValue(TEXT("MarkTravelSpeed"), 1.0f / FMath::Max(Config->PathPortalPulseTime, 0.1f));
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
	// a castle that is chosen has the ring around its walls, as the developer asked on 2026-11-11, and the hero in it none of its own
	const FHexCastle* Castle = bPlaying && Castles && SelectedCastleId != 0 ? Castles->FindCastle(SelectedCastleId) : nullptr;
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		FLayerMarks& Each = Marks[Layer];
		const bool bCastle = Castle && Castle->bSeated && int32(Castle->Layer) == Layer;
		const bool bHere = bCastle || (!Castle && Hero && Hero->bPlaced && int32(Hero->Layer) == Layer);
		AHexPlanet* Planet = bHere ? GetPlanet(EHexPlanetLayer(Layer)) : nullptr;
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
			RingMaterial->SetVectorParameterValue(TEXT("MarkColor"), Config->RingColor);
		}
		// the band of the ring of a hero reaches from 0.7 to 1.06 of its radius
		const UHexCastleConfig* CastleConfig = Castles ? Castles->GetConfig() : nullptr;
		const double Unit = bCastle ? Castle->Seat.GetScale3D().X * 100.0 : 1.0;
		const FVector3d Center = bCastle ? FVector3d(Castle->Seat.GetLocation()) : Hero->Position;
		const double Outer = bCastle && CastleConfig ? double(CastleConfig->RingRadius) * Unit : double(Config->RingRadius) * Hero->ViewScale * 1.06;
		const double Width = bCastle && CastleConfig ? double(CastleConfig->RingWidth) * Unit : double(Config->RingRadius) * Hero->ViewScale * 0.36;
		const int32 Segments = FMath::Clamp(bCastle && CastleConfig ? CastleConfig->RingSegments : Config->RingSegments, 8, 256);
		// laid again only when what it lies around moved or grew or the ground changed
		if (!Center.Equals(Each.RingAt, 0.05) || Outer != Each.RingOuter || Each.RingGround != GroundRevision || Each.RingSegments != Segments)
		{
			BuildRing(*Planet, Each, Center, bCastle ? Castle->Cells[0] : Hero->Cell, Outer, Width, Segments);
		}
		if (!Each.Ring->IsVisible())
		{
			Each.Ring->SetVisibility(true);
		}
	}
}

void UHexHeroes::BuildRing(const AHexPlanet& Planet, FLayerMarks& Each, const FVector3d& Center, int32 Cell, double Outer, double Width, int32 Segments)
{
	// the band across: how far out a row lies, from 0.7 at its inner edge to 1.06 at its outer, how bright it is and how much it covers, a
	// bright band with a dark rim on both sides. Each change takes a hundredth of the radius, so its edges are sharp: over the wide steps
	// it had until 2026-11-12 the ring looked a little blurred to the developer ("обводка має бути читкішою")
	static const float Rows[][3] = {{0.7f, 0.0f, 0.0f}, {0.712f, 0.0f, 0.8f}, {0.772f, 0.0f, 0.8f}, {0.784f, 1.0f, 1.0f}, {0.958f, 1.0f, 1.0f}, {0.97f, 0.0f, 0.8f},
		{1.048f, 0.0f, 0.8f}, {1.06f, 0.0f, 0.0f}};
	const int32 NumRows = UE_ARRAY_COUNT(Rows);
	const auto GetReach = [Outer, Width, NumRows](int32 Row) { return Outer - Width * double(Rows[NumRows - 1][0] - Rows[Row][0]) / double(Rows[NumRows - 1][0] - Rows[0][0]); };
	const FVector3d Up = Center.GetSafeNormal();
	FVector3d AxisX;
	FVector3d AxisY;
	Up.FindBestAxisVectors(AxisX, AxisY);
	const FHexSphereGrid& Grid = Planet.GetGrid();

	TArray<FVector> Vertices;
	Vertices.SetNumUninitialized(Segments * NumRows);
	int32 Hint = Cell;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const double Angle = UE_DOUBLE_TWO_PI * Segment / Segments;
		const FVector3d Out = AxisX * FMath::Cos(Angle) + AxisY * FMath::Sin(Angle);
		// the ground under the inner and the outer edge of the band, of whichever cell lies there, and a straight line between them
		double Grounds[2];
		for (int32 Edge = 0; Edge < 2; ++Edge)
		{
			const FVector3d Direction = (Center + Out * GetReach(Edge == 0 ? 0 : NumRows - 1)).GetSafeNormal();
			Hint = Grid.FindCell(Direction, Hint);
			Grounds[Edge] = FindGroundPoint(Planet, Hint, Cell, Direction, Config->PathLift).Size();
		}
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			const double Share = (Rows[Row][0] - Rows[0][0]) / (Rows[NumRows - 1][0] - Rows[0][0]);
			const FVector3d Direction = (Center + Out * GetReach(Row)).GetSafeNormal();
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
	Each.RingAt = Center;
	Each.RingOuter = Outer;
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
	if (Each.Portals.IsValid() && Each.Portals->GetInstanceCount() > 0)
	{
		Each.Portals->ClearInstances();
	}
	if (Each.Rings.IsValid() && Each.Rings->GetInstanceCount() > 0)
	{
		Each.Rings->ClearInstances();
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
	AHexPlanet* Planets[NumPlanetLayers] = {GetPlanet(EHexPlanetLayer::Surface), GetPlanet(EHexPlanetLayer::Underground)};
	AHexPlanet* Planet = Hero ? Planets[int32(Hero->Layer)] : nullptr;
	const EHexPlanetLayer Presented = Planets[1] && Planets[1]->IsPresented() ? EHexPlanetLayer::Underground : EHexPlanetLayer::Surface;
	// a hero that walks shows the rest of its way, one that stands the way to the cell it is pointed to, which may be of the other layer
	EHexPlanetLayer TargetLayer = Hero ? Hero->TargetLayer : EHexPlanetLayer::Surface;
	if (Hero && Hero->bWalking)
	{
		TargetLayer = Hero->Layer;
		WalkWay(*Hero, Hero->Route, [&TargetLayer](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
		{
			TargetLayer = Layer;
			return true;
		});
	}
	const AHexPlanet* TargetPlanet = Planets[int32(TargetLayer)];
	const bool bShow = Hero && Planet && Planet->IsBuilt() && Hero->bPlaced && TargetPlanet
		&& (Hero->bWalking ? !Hero->Route.IsEmpty() || !Hero->Trail.IsEmpty() : TargetPlanet->GetGrid().GetCells().IsValidIndex(Hero->TargetCell));
	const int32 Target = !bShow ? INDEX_NONE : !Hero->bWalking ? Hero->TargetCell : Hero->Route.IsEmpty() ? Hero->Cell : Hero->Route.Last();
	const int32 RouteKey = bShow && Hero->bWalking ? Hero->Route.Num() : -1;
	const FVector3d Start = bShow && !Hero->bWalking ? Hero->Position : FVector3d::ZeroVector;
	if (Target == ShownTarget && (!bShow || (TargetLayer == ShownTargetLayer && Presented == ShownLayer && Hero->Id == ShownHero && MapRevision == ShownRevision
		&& Hero->MovePoints == ShownPoints && RouteKey == ShownRoute && Start.Equals(ShownStart, 0.01))))
	{
		return;
	}
	ShownTarget = Target;
	ShownTargetLayer = TargetLayer;
	ShownLayer = Presented;
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
	// the numbers of the way are drawn for the layer shown
	PathLayer = Presented;

	// the marks of a layer, made when a way is first shown on it; a layer whose ground is not built yet shows none
	const auto MakeMarks = [](AHexPlanet* Owner, TSoftObjectPtr<UStaticMesh> MeshAsset, UMaterialInstanceDynamic* Material, int32 SortPriority) -> UInstancedStaticMeshComponent*
	{
		UStaticMesh* Mesh = MeshAsset.LoadSynchronous();
		if (!Mesh)
		{
			return nullptr;
		}
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner);
		Component->SetupAttachment(Owner->GetRootComponent());
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetVisibleInRayTracing(false);
		Component->bReceivesDecals = false;
		// its color, and for a mark that runs how far along its Z, from which share of the time, and how much smaller it grows
		Component->SetNumCustomDataFloats(6);
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
		// drawn after the water and the clouds, as neither writes its depth, the dots over the line
		Component->SetTranslucentSortPriority(SortPriority);
		Component->RegisterComponent();
		return Component;
	};
	const auto FindMarks = [&](EHexPlanetLayer Layer) -> FLayerMarks*
	{
		AHexPlanet* Owner = Planets[int32(Layer)];
		if (!Owner || !Owner->IsBuilt())
		{
			return nullptr;
		}
		FLayerMarks& Each = GetMarks(Layer);
		if (!Each.Lines.IsValid())
		{
			Each.Lines = MakeMarks(Owner, Config->PathLineMesh, GetLineMaterial(), 99);
		}
		if (!Each.Dots.IsValid())
		{
			Each.Dots = MakeMarks(Owner, Config->PathMesh, GetPathMaterial(), 100);
		}
		if (!Each.End.IsValid())
		{
			Each.End = MakeMarks(Owner, Config->PathEndMesh, GetPathMaterial(), 100);
		}
		if (!Each.Portals.IsValid())
		{
			Each.Portals = MakeMarks(Owner, Config->PathPortalMesh, GetPathMaterial(), 100);
		}
		if (!Each.Rings.IsValid())
		{
			Each.Rings = MakeMarks(Owner, Config->PathPortalRingMesh, GetPathMaterial(), 101);
		}
		return &Each;
	};
	// the cells of both layers have the same middles
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const auto GetMarkPoint = [&](EHexPlanetLayer Layer, int32 MarkCell)
	{
		return Cells[MarkCell].Center * (GetGroundRadius(*Planets[int32(Layer)], MarkCell) + Config->PathLift);
	};
	const auto AddMark = [&](UInstancedStaticMeshComponent* Component, const FTransform& Transform, const FLinearColor& Color, float Travel = 0.0f, float Phase = 0.0f,
		float Shrink = 0.0f)
	{
		if (Component)
		{
			const int32 Instance = Component->AddInstance(Transform, true);
			const float Data[6] = {Color.R, Color.G, Color.B, Travel, Phase, Shrink};
			Component->SetCustomData(Instance, MakeArrayView(Data), true);
		}
	};
	// a flat mark on the middle of a cell, leaning as the ground around it does
	const auto AddFlatMark = [&](EHexPlanetLayer Layer, UInstancedStaticMeshComponent* Component, int32 MarkCell, double MarkRadius, const FLinearColor& Color)
	{
		AddMark(Component, GetMarkTransform(GetMarkPoint(Layer, MarkCell), FindGroundUp(*Planets[int32(Layer)], MarkCell, MarkRadius * 0.7), MarkRadius), Color);
	};
	// the line between two points of the ground of two cells that touch, in pieces that follow the relief between them
	const auto AddLine = [&](EHexPlanetLayer Layer, FLayerMarks& Each, const FVector3d& From, int32 FromCell, const FVector3d& To, int32 ToCell, const FLinearColor& Color)
	{
		const int32 Pieces = FMath::Clamp(FMath::CeilToInt32(FVector3d::Distance(From, To) / FMath::Max(double(Config->PathLineStep), 2.0)), 1, 64);
		FVector3d Last = From;
		for (int32 Piece = 1; Piece <= Pieces; ++Piece)
		{
			const double Alpha = double(Piece) / Pieces;
			const bool bFirstHalf = Alpha < 0.5;
			const FVector3d Next = Piece == Pieces ? To
				: FindGroundPoint(*Planets[int32(Layer)], bFirstHalf ? FromCell : ToCell, bFirstHalf ? ToCell : FromCell, FMath::Lerp(From, To, Alpha), Config->PathLift);
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

	// A way through an entrance shows it: a line between the middle of its cell and its mouth, into it on the layer the way leaves and out
	// of it on the layer it comes to, and a hoop that stands in the mouth, upright and facing out of it
	const auto AddPortal = [&](EHexPlanetLayer Of, int32 PortalCell, bool bInto, const FLinearColor& Color)
	{
		FLayerMarks* Each = FindMarks(Of);
		const AHexPlanet* OfPlanet = Planets[int32(Of)];
		if (!Each || !OfPlanet || !Cells.IsValidIndex(PortalCell))
		{
			return;
		}
		FVector3d Mouth = OfPlanet->GetEntranceMouth(PortalCell);
		const FVector3d MouthUp = Mouth.GetSafeNormal();
		Mouth += MouthUp * double(Config->PathLift);
		const FVector3d Middle = GetMarkPoint(Of, PortalCell);
		if (FVector3d::DistSquared(Mouth, Middle) > 1.0)
		{
			AddLine(Of, *Each, bInto ? Middle : Mouth, PortalCell, bInto ? Mouth : Middle, PortalCell, Color);
		}
		const FVector3d Out = ProjectToGround(OfPlanet->GetEntranceSeat(PortalCell).Front, MouthUp);
		if (!Out.IsNearlyZero())
		{
			// the hoop in the mouth, and hoops that run from it along the way, smaller and fading: out of the mouth where the way comes out,
			// and on into the passage where the way goes in. The material moves them, along the Z of the hoop, which points out, and draws
			// those that run into the rock over it
			const FVector3d Hub = Mouth + MouthUp * double(Config->PathPortalRadius * Config->PathPortalLift);
			AddMark(Each->Portals.Get(), GetMarkTransform(Hub, Out, Config->PathPortalRadius), Color);
			const int32 NumLayers = FMath::Clamp(Config->PathPortalLayers, 0, 8);
			const float Travel = FMath::Max(Config->PathPortalTravel, 0.0f);
			for (int32 Layer = 0; Layer < NumLayers && Travel > 0.0f; ++Layer)
			{
				AddMark(Each->Rings.Get(), GetMarkTransform(Hub, Out, Config->PathPortalRadius), Color, bInto ? -Travel : Travel,
					float(Layer) / float(NumLayers), 1.0f - FMath::Clamp(Config->PathPortalLayerScale, 0.05f, 1.0f));
			}
		}
	};

	TArray<int32> Path;
	bool bVisit = false;
	if (Hero->bWalking)
	{
		Path = Hero->Route;
		bVisit = Hero->VisitCell != INDEX_NONE;
	}
	else if (Hero->TargetCell == Hero->Cell && TargetLayer == Hero->Layer)
	{
		// pointed to the entrance or exit it stands on
		if (FLayerMarks* Each = FindMarks(Hero->Layer))
		{
			AddFlatMark(Hero->Layer, Each->End.Get(), Hero->Cell, Config->PathEndRadius, Config->PathColor);
		}
		HoverText = FString::Printf(TEXT("%s: click again to go %s"), *Hero->Name, Hero->Layer == EHexPlanetLayer::Surface ? TEXT("down into the underground") : TEXT("up to the surface"));
		return;
	}
	else if (int32 CastleCell = INDEX_NONE; const FHexHero* Other = FindSallyTarget(*Hero, TargetLayer, Hero->TargetCell, CastleCell))
	{
		// out of its castle onto a hero beside it
		if (FLayerMarks* Each = FindMarks(TargetLayer))
		{
			AddFlatMark(TargetLayer, Each->End.Get(), Hero->TargetCell, Config->PathEndRadius, Other->Player == Hero->Player ? Config->PathColor : Config->PathBlockedColor);
		}
		HoverText = Other->Player == Hero->Player ? FString::Printf(TEXT("%s: click again to swap places with %s"), *Hero->Name, *Other->Name)
			: FString::Printf(TEXT("%s: click again, and %s attacks the castle"), *Hero->Name, *Other->Name);
		return;
	}
	else
	{
		// the cheapest ways from where the hero stands, found again when it moved or the map changed, so a way remembered is always the best now
		if (FieldHero != Hero->Id || FieldLayer != Hero->Layer || FieldRevision != MapRevision || Field.Start != GetNode(Hero->Layer, Hero->Cell))
		{
			FindFieldFor(*Hero, Field, !IsPlayerAi(Hero->Player));
			FieldHero = Hero->Id;
			FieldLayer = Hero->Layer;
			FieldRevision = MapRevision;
		}
		if (!FindWay(*Hero, Field, TargetLayer, Hero->TargetCell, Path, bVisit))
		{
			FString Problem;
			if (const FHexHero* Guard = bVisit && Sites && Sites->FindBlockerAt(TargetLayer, Hero->TargetCell) ? FindHeroAt(TargetLayer, Hero->TargetCell) : nullptr)
			{
				// a mine or a mill a hero stands on is guarded
				Problem = FString::Printf(TEXT("%s guards it"), *Guard->Name);
			}
			else if (bVisit || CanStand(TargetLayer, Hero->TargetCell, Hero->Id, &Problem))
			{
				Problem = FString::Printf(TEXT("no way there within %d days"), Config->PathMaxDays);
			}
			HoverText = FString::Printf(TEXT("%s: %s"), *Hero->Name, *Problem);
			if (FLayerMarks* Each = FindMarks(TargetLayer))
			{
				AddFlatMark(TargetLayer, Each->End.Get(), Hero->TargetCell, Config->PathEndRadius, Config->PathBlockedColor);
				// why, at the mark itself, as a cell that cannot be entered may look as if it could
				if (TargetLayer == Presented)
				{
					Problem[0] = FChar::ToUpper(Problem[0]);
					PathLabels.Add({GetMarkPoint(TargetLayer, Hero->TargetCell), 0, true, Problem});
				}
			}
			return;
		}
	}

	TArray<float> Costs;
	TArray<int32> Days;
	GetStepCosts(*Hero, Path, Costs);
	// the visit of a site at the end of the way shows as its last step, onto the cell of the site
	if (bVisit)
	{
		// a castle by the cell of it at the gate the way ends before
		const int32 Before = Path.IsEmpty() ? Hero->Cell : Path.Last();
		const int32 SiteCell = GetCastleEntryCell(TargetLayer, Before, Hero->bWalking ? Hero->VisitCell : Hero->TargetCell);
		Costs.Add(GetVisitCost(*TargetPlanet, Before, SiteCell));
		Path.Add(SiteCell);
	}
	// the way starts at a node of its own, as the developer asked on 2026-11-11, and that node stays where the hero set out until it stops:
	// the cells it left since (Trail) stay on the way, with the step it takes, so the start is not a new cell with every step
	const TArray<FHexSphereCell>& PlanetCells = Planet->GetGrid().GetCells();
	if (FLayerMarks* Each = FindMarks(Hero->Layer))
	{
		int32 Before = INDEX_NONE;
		if (Hero->bWalking)
		{
			// the mouth it came out of since it set out stays on the way with the cells it left
			if (PlanetCells.IsValidIndex(Hero->CameOutCell))
			{
				AddPortal(Hero->Layer, Hero->CameOutCell, false, Config->PathColor);
				if (Hero->Trail.IsEmpty() && Hero->CameOutCell != Hero->Cell && PlanetCells[Hero->CameOutCell].Neighbors.Contains(Hero->Cell))
				{
					Before = Hero->CameOutCell;
				}
			}
			for (const int32 Left : Hero->Trail)
			{
				if (!PlanetCells.IsValidIndex(Left))
				{
					continue;
				}
				if (Before != INDEX_NONE && PlanetCells[Before].Neighbors.Contains(Left))
				{
					AddLine(Hero->Layer, *Each, GetMarkPoint(Hero->Layer, Before), Before, GetMarkPoint(Hero->Layer, Left), Left, Config->PathColor);
				}
				AddFlatMark(Hero->Layer, Each->Dots.Get(), Left, Config->PathDotRadius, Config->PathColor);
				Before = Left;
			}
		}
		if (Before != INDEX_NONE && PlanetCells[Before].Neighbors.Contains(Hero->Cell))
		{
			AddLine(Hero->Layer, *Each, GetMarkPoint(Hero->Layer, Before), Before, GetMarkPoint(Hero->Layer, Hero->Cell), Hero->Cell, Config->PathColor);
		}
		// the cell it stands on or steps onto, but not one of a castle it is in
		if (!(Castles && Castles->FindCastleAt(Hero->Layer, Hero->Cell)))
		{
			const bool bEnd = Hero->bWalking && Path.IsEmpty();
			AddFlatMark(Hero->Layer, bEnd ? Each->End.Get() : Each->Dots.Get(), Hero->Cell, bEnd ? Config->PathEndRadius : Config->PathDotRadius, Config->PathColor);
		}
	}
	if (Path.IsEmpty())
	{
		return;
	}
	// the layer each step ends on, and whether it goes through an entrance or exit
	TArray<EHexPlanetLayer> Layers;
	TArray<bool> Through;
	WalkWay(*Hero, Path, [&](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
	{
		Layers.Add(Layer);
		Through.Add(bThrough);
		return true;
	});
	HexHeroPath::FindStepDays(Costs, Hero->MovePoints, GetMaxMovePoints(*Hero), Days);
	// over ground its player has not explored the cost is the least it can be
	const bool bUnknown = IsWayUnknown(*Hero, Path, TargetLayer, Path.Last());
	float Total = 0.0f;
	int32 Passes = 0;
	// the line starts at the node under the hero, or at the cell it steps onto
	FVector3d Previous = GetMarkPoint(Hero->Layer, Hero->Cell);
	int32 PreviousCell = Hero->Cell;
	for (int32 Index = 0; Index < Path.Num(); ++Index)
	{
		const bool bLast = Index == Path.Num() - 1;
		const FLinearColor& Color = Days[Index] <= 1 ? Config->PathColor : Config->PathFarColor;
		const EHexPlanetLayer Layer = Layers[Index];
		Total += Costs[Index];
		Passes += Through[Index] ? 1 : 0;
		// the way into a castle ends on the place of its hero in the yard, through the gate nearest, as the developer asked on 2026-11-11
		// (the mark lay on the middle of the cell of the castle that was clicked before)
		const FHexCastle* Entered = bLast && bVisit && Castles ? Castles->FindCastleAt(Layer, Path[Index]) : nullptr;
		FVector3d Gate;
		FVector3d Yard;
		if (FLayerMarks* Inside = Entered && Castles->FindGate(*Entered, Previous, Gate, Yard) ? FindMarks(Layer) : nullptr)
		{
			Gate += Gate.GetSafeNormal() * Config->PathLift;
			Yard += Yard.GetSafeNormal() * Config->PathLift;
			AddLine(Layer, *Inside, Previous, PreviousCell, Gate, Path[Index], Color);
			AddLine(Layer, *Inside, Gate, Path[Index], Yard, Path[Index], Color);
			AddMark(Inside->End.Get(), GetMarkTransform(Yard, Yard.GetSafeNormal(), Config->PathEndRadius), Color);
			if (Layer == Presented && Days[Index] > 1)
			{
				PathLabels.Add({Yard, Days[Index], true, bUnknown ? FString::Printf(TEXT("%d+ days"), Days[Index]) : FString()});
			}
		}
		else if (FLayerMarks* Each = FindMarks(Layer))
		{
			const FVector3d Point = GetMarkPoint(Layer, Path[Index]);
			// the line from the point before, lying on the ground between them; through an entrance or exit the way goes on from the same
			// cell of the other layer, which gets a mark of its own
			if (Through[Index])
			{
				// through an entrance: to its mouth and a hoop in it on the layer the way leaves, and a hoop in the mouth and from it to the
				// middle of the cell on the layer it comes to
				AddPortal(Index > 0 ? Layers[Index - 1] : Hero->Layer, Path[Index], true, Color);
				AddPortal(Layer, Path[Index], false, Color);
			}
			else if (FVector3d::DistSquared(Point, Previous) > 1.0)
			{
				AddLine(Layer, *Each, Previous, PreviousCell, Point, Path[Index], Color);
			}
			AddFlatMark(Layer, bLast ? Each->End.Get() : Each->Dots.Get(), Path[Index], bLast ? Config->PathEndRadius : Config->PathDotRadius, Color);
			// the number of the day at the cell where a day ends, and at the last cell the days the way takes when it is not walked today
			if (Layer == Presented && (bLast ? Days[Index] > 1 : Days[Index + 1] != Days[Index]))
			{
				PathLabels.Add({Point, Days[Index], bLast, bLast && bUnknown ? FString::Printf(TEXT("%d+ days"), Days[Index]) : FString()});
			}
			Previous = Point;
		}
		PreviousCell = Path[Index];
	}
	HoverText = FString::Printf(TEXT("%s: %d %s, %s%.1f points, %s"), *Hero->Name, Path.Num(), Path.Num() == 1 ? TEXT("step") : TEXT("steps"),
		bUnknown ? TEXT("at least ") : TEXT(""), Total, Days.Last() <= 1 ? TEXT("there today") : *FString::Printf(TEXT("%d days"), Days.Last()));
	if (bUnknown)
	{
		HoverText += TEXT(" or more, over unexplored ground");
	}
	if (Passes > 0)
	{
		HoverText += Passes == 1 ? FString(TEXT(", through an entrance")) : FString::Printf(TEXT(", through %d entrances"), Passes);
	}
	if (bVisit || (Sites && !Path.IsEmpty() && !IsHidden(Hero->Player, TargetLayer, Path.Last()) && Sites->FindPileAt(TargetLayer, Path.Last())))
	{
		HoverText += FString::Printf(TEXT(", to %s"), *Sites->GetCellText(TargetLayer, Path.Last()).RightChop(6));
	}

}

FString UHexHeroes::GetCellText(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexHero* Hero = FindHeroAt(Layer, Cell);
	if (!Hero || !Config)
	{
		return Neutrals ? Neutrals->GetCellText(Layer, Cell) : FString();
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
	FindFieldFor(*Hero, Ways, !IsPlayerAi(Hero->Player));
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
			else if (FindWay(*Hero, Ways, Hero->Layer, Each, Path, bVisit))
			{
				float Cost = 0.0f;
				GetWayCost(*Hero, Path, Cost);
				Result = FString::Printf(TEXT("%d steps, %.2f points%s"), Path.Num(), Cost, bVisit ? TEXT(", then a visit") : TEXT(""));
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
	if (Neutrals)
	{
		Neutrals->OnLayerRebuilt(Layer);
	}
	if (Castles)
	{
		Castles->OnLayerRebuilt(Layer);
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

	if (Neutrals)
	{
		Neutrals->Update();
	}
	// while a battle is fought on its field the heroes and the game wait; the result of one that ended is applied here, never during the
	// step of the hero that set it off
	if (Battles && Battles->ApplyPending())
	{
		return;
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
		// and one the person would see on the layer that is not shown waits for the camera to come there
		bHeld |= WaitForView(Hero);
		if (bHeld)
		{
			Hero.Speed = 0.0f;
		}
		else if (Hero.bWalking && (Hero.WalkWait > 0.0f || (bViewTravelling && IsPersonTurn() && Hero.Id == SelectedId)))
		{
			// it went through an entrance or exit, or was sent on the layer that was not shown: the camera comes after it, and it waits
			// while the screen is black between the layers
			Hero.WalkWait = FMath::Max(Hero.WalkWait - DeltaTime, 0.0f);
			Hero.Speed = 0.0f;
			// and one that is to walk out of the mouth of an entrance does so only once the screen is no longer black
			const UWorld* TimeWorld = Surface ? Surface->GetWorld() : nullptr;
			if (TimeWorld && bViewTravelling && IsPersonTurn() && Hero.Id == SelectedId && Hero.bGliding && Hero.GlideScaleFrom < Hero.GlideScaleTo)
			{
				const double Soonest = TimeWorld->GetTimeSeconds() + double(Config->EntranceOutDelay);
				Hero.GlideStart = FMath::Max(Hero.GlideStart, Soonest);
				Hero.WalkWait = FMath::Max(Hero.WalkWait, float(Hero.GlideStart - TimeWorld->GetTimeSeconds()) + Hero.GlideTime);
			}
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
				// through an entrance or exit: it waits on the other side first
				if (Hero.WalkWait > 0.0f)
				{
					break;
				}
			}
		}
		// placed again every frame while it walks, also through a gate
		if (Hero.bWalking || !Hero.bPlaced || Hero.bGliding)
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
		if (!Hero.bWalking && !Hero.Trail.IsEmpty())
		{
			Hero.Trail.Reset();
		}
		if (!Hero.bWalking)
		{
			Hero.CameOutCell = INDEX_NONE;
		}
		Hero.ViewScale = ViewPoint && Hero.bPlaced ? float(FMath::Clamp(FVector3d::Distance(*ViewPoint, Hero.Position) / FMath::Max(double(Config->FarStart), 1.0), 1.0, double(FMath::Max(Config->FarScale, 1.0f)))) : 1.0f;
		UpdateBody(Hero);
	}

	// a castle is chosen only while it is the person's whose turn it is
	if (SelectedCastleId != 0)
	{
		const FHexCastle* Chosen = Castles->FindCastle(SelectedCastleId);
		if (!Chosen || !bPlaying || !IsPersonTurn() || Chosen->Owner != TurnPlayer)
		{
			SelectedCastleId = 0;
			++ListRevision;
		}
		else if (const FHexHero* Guard = Castles->FindGuard(*Chosen); Guard && Guard->Player == TurnPlayer && Guard->Id != SelectedId)
		{
			// the hero that guards it is chosen with it
			SelectedId = Guard->Id;
			++ListRevision;
		}
	}
	else if (const FHexHero* Chosen = bPlaying && IsPersonTurn() ? FindHero(SelectedId) : nullptr; Chosen && Chosen->CastleId != 0 && Chosen->Player == TurnPlayer)
	{
		// and a hero chosen that went into a castle with its castle
		SelectedCastleId = Chosen->CastleId;
		++ListRevision;
	}
	UpdatePendingNext(DeltaTime);
	UpdateAi(DeltaTime);
	// counted after the heroes and the game looked at it, so a hold of no time holds nothing
	WatchHold = FMath::Max(WatchHold - DeltaTime, 0.0f);
	UpdateRing();
	UpdateReach();
	UpdatePath();
	Sites->Update();
	Castles->Update();
}

bool UHexHeroes::NoteSeen(FHexHero& Hero)
{
	// only the heroes of the player the game moves now are watched
	if (!bPlaying || Hero.Player != TurnPlayer || !IsPlayerAi(TurnPlayer))
	{
		return false;
	}
	const bool bSeen = IsSeen(Hero);
	const bool bCameThrough = Hero.bCameThrough;
	Hero.bCameThrough = false;
	if (bSeen != Hero.bWasSeen)
	{
		Hero.bWasSeen = bSeen;
		const AHexPlanet* Planet = GetPlanet(Hero.Layer);
		// longer where it comes out of an entrance or exit
		const float Hold = bSeen && bCameThrough ? FMath::Max(Config->AiFogHoldTime, Config->AiEntranceHoldTime) : Config->AiFogHoldTime;
		if (IsShroudOn() && Hold > 0.0f && Planet && Planet->IsPresented() && Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
		{
			// the camera stays where it went into the fog, or goes where it is about to come out of it
			WatchHold = Hold;
			WatchHeroId = Hero.Id;
			WatchLayer = Hero.Layer;
			WatchDirection = !bSeen && Hero.bPlaced ? Hero.Position.GetSafeNormal() : Planet->GetGrid().GetCells()[Hero.Cell].Center;
			UE_LOG(LogHexHero, Log, TEXT("Heroes: %s %s the fog of war at cell %d, the camera stays %.1f s"), *Hero.Name,
				bSeen ? TEXT("is about to come out of") : TEXT("went into"), Hero.Cell, WatchHold);
		}
	}
	return WatchHold > 0.0f && WatchHeroId == Hero.Id;
}

bool UHexHeroes::WaitForView(const FHexHero& Hero)
{
	// only the hero the game moves now, where the person who watches would see it
	if (!bPlaying || !Config || !IsPlayerAi(TurnPlayer) || Hero.Id != AiHeroId || Hero.Player != TurnPlayer || !IsSeen(Hero))
	{
		return false;
	}
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet || (Planet->IsPresented() && !bViewTravelling))
	{
		ViewWaitHero = 0;
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (ViewWaitHero != Hero.Id || ViewWaitLayer != Hero.Layer)
	{
		ViewWaitHero = Hero.Id;
		ViewWaitLayer = Hero.Layer;
		ViewWaitStart = Now;
		bViewAsked = false;
	}
	if (!Planet->IsPresented() && !bViewAsked)
	{
		bViewAsked = true;
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, moved by the game, waits on cell %d for the camera to come to the %s"), *Hero.Name, Hero.Cell, GetPlanetLayerName(Hero.Layer));
		OnHeroSent.Broadcast(Hero.Id);
	}
	// without a camera that comes the game goes on
	return Now - ViewWaitStart < Config->AiViewWaitMax;
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
	WalkWay(Hero, Hero.Route, [&](EHexPlanetLayer Layer, int32 From, int32 To, bool bThrough)
	{
		const float Cost = GetWayStepCost(Hero, Layer, From, To, bThrough);
		if (Cost < 0.0f || !HexHeroPath::CanStep(Cost, Points, GetMaxMovePoints(Hero)))
		{
			return false;
		}
		Points = FMath::Max(Points - Cost, 0.0f);
		++Count;
		return true;
	});
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
	// nor while a hero walks in or out through the gate of a castle, which it does only where the person sees it: until 2026-11-12 the game
	// went on AiThinkTime later, sent the hero on or ended the turn, and the developer saw it go into castles at once, without the gate
	if (const UWorld* World = Surface ? Surface->GetWorld() : nullptr)
	{
		for (const FHexHero& Each : Heroes)
		{
			if (Each.bGliding && World->GetTimeSeconds() < Each.GlideStart + double(Each.GlideTime))
			{
				return;
			}
		}
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
			// on the layer that is not shown the camera goes to it first
			if (WaitForView(*Hero))
			{
				return;
			}
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

bool UHexHeroes::MoveAiToSite(int32 Id, const HexHeroPath::FField& Ways, bool& bOutSent)
{
	bOutSent = false;
	const FHexHero* Hero = FindHero(Id);
	if (!Hero || !Sites || !Config)
	{
		return false;
	}
	struct FGoal
	{
		float Share = 0.0f;
		float Cost = 0.0f;
		const FHexSite* Site = nullptr;
		EHexSiteGoal Goal = EHexSiteGoal::None;
	};
	TArray<FGoal> Goals;
	const float DayPoints = FMath::Max(GetMaxMovePoints(*Hero), 1.0f);
	const int32 Player = Hero->Player;
	for (const FHexSite& Site : Sites->GetSites())
	{
		// what its player has seen and does not own, and no other hero of its player goes for already
		const EHexSiteGoal Goal = Sites->GetGoal(Site, Player, GetWeek());
		if (Goal == EHexSiteGoal::None || IsHidden(Player, Site.Layer, Site.Cell)
			|| Heroes.ContainsByPredicate([&](const FHexHero& Other) { return Other.Id != Id && Other.Player == Player && Other.AiSiteId == Site.Id; }))
		{
			continue;
		}
		const float Reach = DayPoints * (Goal == EHexSiteGoal::LostMine ? Config->AiLostMineReachDays : Goal == EHexSiteGoal::Mine ? Config->AiMineReachDays : Config->AiPileReachDays);
		const float Cost = GetSiteWayCost(Ways, Site.Layer, Site.Cell);
		if (Reach > 0.0f && Cost >= 0.0f && Cost <= Reach)
		{
			Goals.Add({Cost / Reach, Cost, &Site, Goal});
		}
	}
	// and a castle of nobody or of another player that its army can take, by the cell of it the way to which is the cheapest
	if (Castles)
	{
		const float Reach = DayPoints * Castles->GetConfig()->AiCastleReachDays;
		for (const FHexCastle& Castle : Castles->GetCastles())
		{
			float LossShare = 0.0f;
			if (Castle.Owner == Player || IsHidden(Player, Castle.Layer, Castle.Cells[0])
				|| !UHexCastles::Resolve(Castles->GetAttack(*Hero), Castles->GetDefense(Castle), 1.0f, 1.0f, LossShare))
			{
				continue;
			}
			float Cost = -1.0f;
			int32 Cell = INDEX_NONE;
			for (const int32 Each : Castle.Cells)
			{
				const float Way = GetSiteWayCost(Ways, Castle.Layer, Each);
				if (Way >= 0.0f && (Cost < 0.0f || Way < Cost))
				{
					Cost = Way;
					Cell = Each;
				}
			}
			if (Reach > 0.0f && Cost >= 0.0f && Cost <= Reach)
			{
				FString Message;
				if (Send(Id, Castle.Layer, Cell, Message))
				{
					bOutSent = true;
					UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, moved by the game, goes for the castle on cell %d of the %s, %.1f points away"), *FindHero(Id)->Name, Cell,
						GetPlanetLayerName(Castle.Layer), Cost);
					return true;
				}
			}
		}
	}
	// and a hero of another player or a neutral stack its army outweighs enough, within a reach of its own
	if (Battles && Battles->GetConfig())
	{
		const UHexBattleConfig* BattleConfig = Battles->GetConfig();
		const float Reach = DayPoints * BattleConfig->AiAttackReachDays;
		const float Mine = Battles->GetArmyPower(Hero->Army) * (1.0f + 0.05f * float(FMath::Max(Hero->Attack, 0)));
		struct FFoe { float Cost; EHexPlanetLayer Layer; int32 Cell; };
		TArray<FFoe> Foes;
		const auto Consider = [&](EHexPlanetLayer Layer, int32 Cell, float Power)
		{
			if (IsHidden(Player, Layer, Cell) || Mine < Power * BattleConfig->AiAttackMargin)
			{
				return;
			}
			const float Cost = GetSiteWayCost(Ways, Layer, Cell, nullptr, Player);
			if (Reach > 0.0f && Cost >= 0.0f && Cost <= Reach)
			{
				Foes.Add({Cost, Layer, Cell});
			}
		};
		for (const FHexHero& Other : Heroes)
		{
			if (Other.Player != Player && Other.CastleId == 0)
			{
				Consider(Other.Layer, Other.Cell, Battles->GetArmyPower(Other.Army) * (1.0f + 0.05f * float(FMath::Max(Other.Defense, 0))));
			}
		}
		if (Neutrals)
		{
			for (const FHexNeutral& Neutral : Neutrals->GetNeutrals())
			{
				Consider(Neutral.Layer, Neutral.Cell, Battles->GetArmyPower(TArray<FHexStack>{Neutral.Stack}));
			}
		}
		Foes.Sort([](const FFoe& A, const FFoe& B) { return A.Cost < B.Cost; });
		for (const FFoe& Foe : Foes)
		{
			FString Message;
			if (Send(Id, Foe.Layer, Foe.Cell, Message))
			{
				bOutSent = true;
				UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, moved by the game, goes to fight on cell %d of the %s, %.1f points away"), *FindHero(Id)->Name, Foe.Cell,
					GetPlanetLayerName(Foe.Layer), Foe.Cost);
				return true;
			}
		}
	}
	// the nearest by the share of its reach first
	Goals.Sort([](const FGoal& A, const FGoal& B) { return A.Share != B.Share ? A.Share < B.Share : A.Site->Id < B.Site->Id; });
	for (int32 Index = 0; Index < FMath::Min(Goals.Num(), 4); ++Index)
	{
		const FGoal& Goal = Goals[Index];
		const int32 SiteId = Goal.Site->Id;
		const EHexPlanetLayer Layer = Goal.Site->Layer;
		const int32 Cell = Goal.Site->Cell;
		const TCHAR* What = Goal.Goal == EHexSiteGoal::LostMine ? TEXT("to take back the mine") : Goal.Goal == EHexSiteGoal::Mine ? TEXT("for the mine") : TEXT("to gather");
		FString Message;
		if (Send(Id, Layer, Cell, Message))
		{
			FHexHero* Sent = FindHeroMutable(Id);
			Sent->AiSiteId = SiteId;
			bOutSent = true;
			UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, moved by the game, goes %s on cell %d of the %s, %.1f points away"), *Sent->Name, What, Cell, GetPlanetLayerName(Layer), Goal.Cost);
			return true;
		}
		FHexHero* Kept = FindHeroMutable(Id);
		Kept->TargetCell = INDEX_NONE;
		Kept->VisitCell = INDEX_NONE;
		// its points do not pay for the next step of a way of more than a day: it goes on tomorrow, and does not wander off meanwhile
		if (Goal.Cost > Kept->MovePoints + UE_KINDA_SMALL_NUMBER)
		{
			Kept->AiSiteId = SiteId;
			UE_LOG(LogHexHero, Log, TEXT("Heroes: %s, moved by the game, waits for tomorrow to go on %s on cell %d of the %s"), *Kept->Name, What, Cell, GetPlanetLayerName(Layer));
			return true;
		}
	}
	return false;
}

bool UHexHeroes::MoveAi(int32 Id)
{
	const FHexHero* Hero = FindHero(Id);
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	if (!Planet || !Shroud || !Planet->GetGrid().GetCells().IsValidIndex(Hero->Cell) || !CanMove(*Hero))
	{
		return false;
	}
	// what it went for before is looked at anew; one that cannot move keeps it, so another hero of its player takes something else
	FindHeroMutable(Id)->AiSiteId = 0;
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const int32 NumCells = Cells.Num();

	HexHeroPath::FField Ways;
	FindFieldFor(*Hero, Ways, false);

	// first what it can gather and the mines to take, or to take back
	bool bSent = false;
	if (MoveAiToSite(Id, Ways, bSent))
	{
		return bSent;
	}

	// how many steps each cell lies from ground its player has not explored, as far as a hero sees: from such a cell it would open some.
	// Underground only the passages count, as a hero opens nothing else there. On both layers, as its ways go through the entrances and
	// exits its player has seen from both sides
	TArray<int8> Near;
	Near.Init(-1, NumCells * NumPlanetLayers);
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		const AHexPlanet* Each = GetPlanet(EHexPlanetLayer(Layer));
		if (!Each || !Each->IsDataLoaded() || Each->GetGrid().GetCells().Num() != NumCells)
		{
			continue;
		}
		const bool bUnderground = Layer == int32(EHexPlanetLayer::Underground);
		const TBitArray<>* Explored = Shroud->GetExplored(Hero->Player, EHexPlanetLayer(Layer));
		int8* LayerNear = Near.GetData() + Layer * NumCells;
		TArray<int32> Ring;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (!(Explored && Explored->IsValidIndex(Cell) && (*Explored)[Cell]) && (!bUnderground || HexPlanetUnderground::IsPassage(int8(Each->GetCellLevel(Cell)))))
			{
				LayerNear[Cell] = 0;
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
					if (LayerNear[Neighbor] < 0)
					{
						LayerNear[Neighbor] = int8(FMath::Min(Steps, 127));
						Next.Add(Neighbor);
					}
				}
			}
			Ring = MoveTemp(Next);
		}
	}

	// of the cells it reaches today the one nearest that ground, the cheapest of those, so it walks along the edge of what it knows; else
	// the cheapest it reaches on a later day
	const int32 Here = GetNode(Hero->Layer, Hero->Cell);
	int32 BestNode = INDEX_NONE;
	bool bBestToday = false;
	for (int32 Node = 0; Node < Near.Num(); ++Node)
	{
		if (Node == Here || Near[Node] <= 0 || !Ways.IsReached(Node))
		{
			continue;
		}
		const bool bToday = Ways.Costs[Node] <= Hero->MovePoints + UE_KINDA_SMALL_NUMBER;
		const bool bBetter = BestNode == INDEX_NONE || (bToday != bBestToday ? bToday
			: bToday && Near[Node] != Near[BestNode] ? Near[Node] < Near[BestNode] : Ways.Costs[Node] < Ways.Costs[BestNode]);
		if (bBetter)
		{
			BestNode = Node;
			bBestToday = bToday;
		}
	}
	if (BestNode != INDEX_NONE)
	{
		FString Message;
		return Send(Id, EHexPlanetLayer(BestNode / NumCells), BestNode % NumCells, Message);
	}

	// nothing left to open within reach: through the nearest entrance or exit of its layer whose other side its player has not seen
	int32 Best = INDEX_NONE;
	{
		const EHexPlanetLayer Other = GetOtherLayer(Hero->Layer);
		float BestCost = 0.0f;
		for (const FHexEntrance& Entrance : Planet->GetEntrances())
		{
			const int32 Cell = Entrance.Cell;
			if (!IsUsableEntrance(*Planet, Cell) || Shroud->IsExplored(Hero->Player, Other, Cell) || FindHeroAt(Other, Cell)
				|| (Cell != Hero->Cell && !Ways.IsReached(GetNode(Hero->Layer, Cell))))
			{
				continue;
			}
			const float Cost = Cell == Hero->Cell ? 0.0f : Ways.Costs[GetNode(Hero->Layer, Cell)];
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
	// through it: to the same cell of the other layer
	FString Message;
	return Send(Id, GetOtherLayer(Hero->Layer), Best, Message);
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
		// near:<Steps>[:<Side>] counts from the hero chosen, as hex.Neutral and hex.Site do
		const bool bNear = Text.StartsWith(TEXT("near"), ESearchCase::IgnoreCase);
		if (!Text.StartsWith(TEXT("focus"), ESearchCase::IgnoreCase) && !bNear)
		{
			return Text.IsNumeric() ? FCString::Atoi(*Text) : INDEX_NONE;
		}
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		const AActor* ViewTarget = PlayerController ? PlayerController->GetViewTarget() : nullptr;
		const UHexHeroes* Chosen = Planet.GetHeroes();
		if (!ViewTarget || (bNear && !(Chosen && Chosen->GetSelected())))
		{
			return INDEX_NONE;
		}
		const TArray<FHexSphereCell>& Cells = Planet.GetGrid().GetCells();
		int32 Cell = bNear ? Chosen->GetSelected()->Cell : Planet.GetGrid().FindCell(Planet.FindViewFocus(ViewTarget->GetActorLocation(), ViewTarget->GetActorForwardVector()).GetSafeNormal());
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
			UE_LOG(LogHexHero, Warning, TEXT("hex.Hero <list|play|stop|add <Cell> [Class] [Player]|remove <Hero>|select <Hero>|next|click <Cell>|target <Cell|off>|go|send <Cell> [Hero]|ways <Cell> [Rings]|ai <Player> [on|off]|points <Amount> [Hero]|skip [Hero]|endturn|restart>, on a built planet; a hero is its Id or its name"));
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
			else if (!Heroes->Send(Hero->Id, Layer, ParseHeroCell(Args[1], *Planet, World), Message))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: %s"), Message.IsEmpty() ? TEXT("the hero stays") : *Message);
			}
		}
		else if (Action.Equals(TEXT("through"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			// as a click on the mouth of the entrance before that cell: to the same cell of the other layer than the hero is on
			const FHexHero* Hero = Args.Num() >= 3 ? Heroes->FindHeroByText(Args[2]) : Heroes->GetSelected();
			if (!Hero)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: no hero is chosen"));
			}
			else if (!Heroes->Send(Hero->Id, GetOtherLayer(Hero->Layer), ParseHeroCell(Args[1], *Planet, World), Message))
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
			Heroes->SetTarget(Heroes->GetSelectedId(), Layer, Args[1].Equals(TEXT("off"), ESearchCase::IgnoreCase) ? INDEX_NONE : ParseHeroCell(Args[1], *Planet, World));
		}
		else if (Action.Equals(TEXT("go"), ESearchCase::IgnoreCase))
		{
			if (!Heroes->Go(Heroes->GetSelectedId(), Message))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: %s"), Message.IsEmpty() ? TEXT("the hero chosen is pointed nowhere") : *Message);
			}
		}
		else if (Action.Equals(TEXT("points"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			// more points of movement for the hero chosen or the one named, or fewer with a negative amount
			const FHexHero* Hero = Args.Num() >= 3 ? Heroes->FindHeroByText(Args[2]) : Heroes->GetSelected();
			if (!Hero || !Heroes->AddMovePoints(Hero->Id, FCString::Atof(*Args[1])))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: there is no such hero, or none is chosen"));
			}
		}
		else if (Action.Equals(TEXT("startarmy"), ESearchCase::IgnoreCase))
		{
			// the army of the hero chosen, or of the one named, or with all of every hero, becomes the one it starts with, which the save keeps
			TArray<int32> Ids;
			for (const FHexHero& Each : Heroes->GetHeroes())
			{
				if (Args.Num() >= 2 ? Args[1].Equals(TEXT("all"), ESearchCase::IgnoreCase) || Heroes->FindHeroByText(Args[1]) == &Each : Heroes->GetSelectedId() == Each.Id)
				{
					Ids.Add(Each.Id);
				}
			}
			for (const int32 Id : Ids)
			{
				Heroes->KeepArmyAsStart(Id);
			}
			UE_LOG(LogHexHero, Log, TEXT("hex.Hero: %d heroes start with the armies they have now"), Ids.Num());
		}
		else if (Action.Equals(TEXT("skip"), ESearchCase::IgnoreCase))
		{
			const FHexHero* Hero = Args.Num() >= 2 ? Heroes->FindHeroByText(Args[1]) : Heroes->GetSelected();
			if (!Hero || !Heroes->Skip(Hero->Id))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Hero: only a hero of the person whose turn it is can be left for today"));
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
		TEXT("Drives the heroes of the planet: list, play and stop (the game), add <Cell> [Class] [Player] on the layer shown, remove <Hero>, select <Hero>, next, click <Cell> (as the left button), target <Cell|off> and go (as a click and Space), send <Cell> [Hero], through <Cell> [Hero] (as a click on the mouth of the entrance before that cell), ways <Cell> [Rings] (logs the way of the hero chosen to each cell around one, or why it has none), ai <Player> [on|off] (the game moves the heroes of a player by itself), points <Amount> [Hero] (more points of movement for the hero chosen or named), skip [Hero] (leaves it for today), endturn, restart. A hero is its Id or its name; a cell is its number, focus for the one the camera looks at, or focus:<Steps>[:<Side>] for one that many steps from it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHeroCommand));
}
