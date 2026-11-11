// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexHeroes.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexBiomeConfig.h"
#include "HexPlanet.h"
#include "HexPlanetObjects.h"
#include "HexPlanetRivers.h"
#include "HexPlanetUnderground.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
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
	Hero.StepAlpha = 0.0f;
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

void UHexHeroes::Init(AHexPlanet* InSurface)
{
	Surface = InSurface;
	Config = UHexHeroConfig::Load();
	Heroes.Reset();
	Day = 1;
	NextId = 1;
	bEdited = false;

	if (!Surface || !Surface->IsSaved() || !HexPlanetFile::HasHeroes(Surface->GetPlanetFolder()))
	{
		return;
	}
	TArray<FHexHeroLine> Lines;
	FString Error;
	if (!HexPlanetFile::LoadHeroes(Surface->GetPlanetFolder(), Lines, Error))
	{
		UE_LOG(LogHexHero, Error, TEXT("Heroes: %s"), *Error);
		return;
	}

	const int32 NumCells = Surface->GetGrid().GetCells().Num();
	for (const FHexHeroLine& Line : Lines)
	{
		FHexHero Hero;
		Hero.Id = Line.Id;
		Hero.Name = Line.Name;
		Hero.Class = Config->FindClass(Line.Class);
		Hero.Player = FMath::Clamp(Line.Player, 0, Config->Players.Num() - 1);
		Hero.StartCell = Line.Cell;
		if (Hero.Id <= 0 || FindHero(Hero.Id) || Line.Cell < 0 || Line.Cell >= NumCells || !ParsePlanetLayer(Line.Layer, Hero.StartLayer))
		{
			UE_LOG(LogHexHero, Error, TEXT("Heroes: the hero %d (%s) of heroes.json has no Id of its own, cell or layer, and is left out"), Line.Id, *Line.Name);
			bEdited = true;
			continue;
		}
		if (Hero.Class == INDEX_NONE)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Heroes: %s has the class %s, which DA_HexHeroes does not have, and takes the first"), *Line.Name, *Line.Class);
			Hero.Class = 0;
		}
		ResetHero(Hero);
		NextId = FMath::Max(NextId, Hero.Id + 1);
		Heroes.Add(MoveTemp(Hero));
	}
	++ListRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %d on the planet, %.0f points of movement a day"), Heroes.Num(), Config->MovePoints);
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
	if (!HexPlanetFile::SaveHeroes(PlanetFolder, MakeLines(), OutError))
	{
		return false;
	}
	bEdited = false;
	return true;
}

bool UHexHeroes::IsUsableEntrance(const AHexPlanet& Planet, int32 Cell)
{
	const int32 Entrance = Planet.FindEntrance(Cell);
	return Entrance != INDEX_NONE && Planet.GetEntranceProblem(Entrance) == HexPlanetUnderground::EEntranceProblem::None;
}

float UHexHeroes::GetCellCost(const AHexPlanet& Planet, int32 Cell) const
{
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
	const FHexHero* Other = FindHeroAt(Layer, Cell);
	if (Other && Other->Id != IgnoreHero)
	{
		if (OutProblem)
		{
			*OutProblem = FString::Printf(TEXT("%s stands there"), *Other->Name);
		}
		return false;
	}
	if (GetCellCost(*Planet, Cell) < 0.0f)
	{
		const int32 Level = Planet->GetCellLevel(Cell);
		return Fail(Layer == EHexPlanetLayer::Underground ? (HexPlanetUnderground::IsPassage(int8(Level)) ? TEXT("what stands there blocks the way") : TEXT("nothing crosses the void"))
			: Level < 0 ? TEXT("a hero does not cross the sea yet") : Level > 1 ? TEXT("mountains cannot be crossed") : TEXT("what stands there blocks the way"));
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
	bEdited = true;
	++ListRevision;
	++MapRevision;
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
	if (SelectedId == Id)
	{
		SelectedId = 0;
	}
	bEdited = true;
	++ListRevision;
	++MapRevision;
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
	if (Heroes.IsEmpty())
	{
		return nullptr;
	}
	// the lowest Id above the one chosen, else the lowest of all
	const FHexHero* Next = nullptr;
	const FHexHero* First = nullptr;
	for (const FHexHero& Hero : Heroes)
	{
		if (!First || Hero.Id < First->Id)
		{
			First = &Hero;
		}
		if (Hero.Id > SelectedId && (!Next || Hero.Id < Next->Id))
		{
			Next = &Hero;
		}
	}
	SelectedId = (Next ? Next : First)->Id;
	return GetSelected();
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
	const auto GetCost = [&](int32 From, int32 Side, int32 To)
	{
		float& Cost = CellCosts[To];
		if (Cost == 0.0f)
		{
			Cost = Occupied.Contains(To) ? -1.0f : GetCellCost(*Planet, To);
		}
		return Cost < 0.0f ? -1.0f : Cost * (Rivers ? Rivers->GetCrossingFactor(From, To) : 1.0f);
	};
	HexHeroPath::FindField(Planet->GetGrid(), Hero.Cell, GetMaxMovePoints(Hero) * Config->PathMaxDays, GetCost, OutField);
	UE_LOG(LogHexHero, Verbose, TEXT("Heroes: the ways of %s from cell %d found in %.2f ms"), *Hero.Name, Hero.Cell, (FPlatformTime::Seconds() - StartTime) * 1000.0);
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
		return false;
	}
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
	return true;
}

void UHexHeroes::Arrive(FHexHero& Hero)
{
	const bool bEnter = Hero.bEnterAtEnd && Hero.Route.IsEmpty();
	Hero.bEnterAtEnd = false;
	Hero.bPlaced = false;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s stops on cell %d of the %s with %.2f points of movement left"), *Hero.Name, Hero.Cell, GetPlanetLayerName(Hero.Layer), Hero.MovePoints);
	if (bEnter)
	{
		FString Message;
		if (!Travel(Hero, Message) && !Message.IsEmpty())
		{
			OnMessage.Broadcast(Message);
		}
	}
}

bool UHexHeroes::Travel(FHexHero& Hero, FString& OutMessage)
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
	Hero.Facing = FVector3d::ZeroVector;
	++MapRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s goes %s through cell %d"), *Hero.Name, Other == EHexPlanetLayer::Underground ? TEXT("down into the underground") : TEXT("up to the surface"), Hero.Cell);
	OnHeroTravelled.Broadcast(Hero.Id);
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

	// its own cell: through the entrance or exit it stands on, and a hero walking onto it stops there
	if (Cell == Hero->Cell)
	{
		if (Hero->bWalking)
		{
			Hero->Route.Reset();
			Hero->bEnterAtEnd = IsUsableEntrance(*Planet, Cell);
			return true;
		}
		return Travel(*Hero, OutMessage);
	}

	HexHeroPath::FField Ways;
	FindFieldFor(*Hero, Ways);
	TArray<int32> Path;
	if (!Ways.GetPath(Cell, Path) || Path.IsEmpty())
	{
		FString Problem;
		OutMessage = CanStand(Hero->Layer, Cell, Hero->Id, &Problem) ? FString::Printf(TEXT("%s finds no way there within %d days"), *Hero->Name, Config->PathMaxDays)
			: FString::Printf(TEXT("%s cannot go there: %s"), *Hero->Name, *Problem);
		return false;
	}

	Hero->Route = MoveTemp(Path);
	Hero->bEnterAtEnd = IsUsableEntrance(*Planet, Cell);
	if (!Hero->bWalking)
	{
		Hero->FromCell = Hero->Cell;
		Hero->StepAlpha = 0.0f;
		const int32 Steps = Hero->Route.Num();
		if (!StartNextStep(*Hero))
		{
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
	++Day;
	for (FHexHero& Hero : Heroes)
	{
		// a hero still walking ends the step it is on
		Hero.Route.Reset();
		Hero.bEnterAtEnd = false;
		Hero.MovePoints = GetMaxMovePoints(Hero);
	}
	++MapRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: day %d begins"), Day);
}

void UHexHeroes::Restart()
{
	Day = 1;
	for (FHexHero& Hero : Heroes)
	{
		RemoveBody(Hero.Id);
		ResetHero(Hero);
	}
	++MapRevision;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: back where the game starts, day 1"));
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
	if (!Planet || !Hero.bPlaced)
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
			PathMaterial->SetScalarParameterValue(TEXT("MarkBrightness"), Config->MarkBrightness);
			PathMaterial->SetScalarParameterValue(TEXT("MarkPulse"), 0.0f);
			// the marks grow from afar in the material, as each is an instance
			PathMaterial->SetScalarParameterValue(TEXT("MarkFarStart"), Config->FarStart);
			PathMaterial->SetScalarParameterValue(TEXT("MarkFarScale"), Config->FarScale);
		}
	}
	return PathMaterial;
}

void UHexHeroes::UpdateRing()
{
	const FHexHero* Hero = GetSelected();
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
			UStaticMesh* Mesh = Config->RingMesh.LoadSynchronous();
			if (!Mesh)
			{
				continue;
			}
			if (!RingMaterial)
			{
				if (UMaterialInterface* Material = Config->MarkMaterial.LoadSynchronous())
				{
					RingMaterial = UMaterialInstanceDynamic::Create(Material, this);
					RingMaterial->SetScalarParameterValue(TEXT("MarkBrightness"), Config->MarkBrightness);
					RingMaterial->SetScalarParameterValue(TEXT("MarkPulse"), Config->RingPulse);
					RingMaterial->SetScalarParameterValue(TEXT("MarkPulseSpeed"), Config->RingPulseSpeed);
				}
			}
			UStaticMeshComponent* Ring = NewObject<UStaticMeshComponent>(Planet);
			Ring->SetupAttachment(Planet->GetRootComponent());
			Ring->SetMobility(EComponentMobility::Movable);
			Ring->SetStaticMesh(Mesh);
			Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ring->SetCastShadow(false);
			Ring->SetVisibleInRayTracing(false);
			Ring->bReceivesDecals = false;
			if (RingMaterial)
			{
				Ring->SetMaterial(0, RingMaterial);
			}
			Ring->RegisterComponent();
			Each.Ring = Ring;
		}
		if (RingMaterial)
		{
			RingMaterial->SetVectorParameterValue(TEXT("MarkColor"), Config->GetPlayer(Hero->Player).Color);
		}
		const FVector3d Up = Hero->Position.GetSafeNormal();
		const FTransform Transform = GetMarkTransform(Hero->Position + Up * Config->PathLift, Up, double(Config->RingRadius) * Hero->ViewScale);
		if (!Each.Ring->GetComponentTransform().Equals(Transform, 1.0e-3))
		{
			Each.Ring->SetWorldTransform(Transform);
		}
		if (!Each.Ring->IsVisible())
		{
			Each.Ring->SetVisibility(true);
		}
	}
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

void UHexHeroes::SetHoverCell(EHexPlanetLayer Layer, int32 Cell)
{
	if (!bHoverOverride)
	{
		HoverLayer = Layer;
		HoverCell = Cell;
	}
}

void UHexHeroes::SetHoverOverride(EHexPlanetLayer Layer, int32 Cell)
{
	bHoverOverride = Cell != INDEX_NONE;
	HoverLayer = Layer;
	HoverCell = Cell;
}

void UHexHeroes::UpdateHover()
{
	const FHexHero* Hero = GetSelected();
	AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	const bool bShow = Hero && Planet && Planet->IsBuilt() && Hero->Layer == HoverLayer && !Hero->bWalking && Planet->GetGrid().GetCells().IsValidIndex(HoverCell);
	const int32 Cell = bShow ? HoverCell : INDEX_NONE;
	if (Cell == ShownHoverCell && (!bShow || (Hero->Id == ShownHoverHero && MapRevision == ShownHoverRevision && Hero->MovePoints == ShownHoverPoints)))
	{
		return;
	}
	ShownHoverCell = Cell;
	ShownHoverHero = Hero ? Hero->Id : 0;
	ShownHoverRevision = MapRevision;
	ShownHoverPoints = Hero ? Hero->MovePoints : -1.0f;
	HoverText.Reset();
	ClearMarks(EHexPlanetLayer::Surface);
	ClearMarks(EHexPlanetLayer::Underground);
	if (!bShow)
	{
		return;
	}

	// the marks of the layer, made when a way is first shown on it
	FLayerMarks& Each = GetMarks(Hero->Layer);
	const auto MakeMarks = [&](TSoftObjectPtr<UStaticMesh> MeshAsset) -> UInstancedStaticMeshComponent*
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
		if (UMaterialInstanceDynamic* Material = GetPathMaterial())
		{
			Component->SetMaterial(0, Material);
		}
		// drawn after the water and the clouds, as the marks lie over everything
		Component->SetTranslucentSortPriority(100);
		Component->RegisterComponent();
		return Component;
	};
	if (!Each.Dots.IsValid())
	{
		Each.Dots = MakeMarks(Config->PathMesh);
	}
	if (!Each.End.IsValid())
	{
		Each.End = MakeMarks(Config->PathEndMesh);
	}
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const auto AddMark = [&](UInstancedStaticMeshComponent* Component, int32 MarkCell, double MarkRadius, const FLinearColor& Color)
	{
		if (!Component)
		{
			return;
		}
		const FVector3d Up = Cells[MarkCell].Center;
		const int32 Instance = Component->AddInstance(GetMarkTransform(Up * (GetGroundRadius(*Planet, MarkCell) + Config->PathLift), Up, MarkRadius), true);
		const float Data[3] = {Color.R, Color.G, Color.B};
		Component->SetCustomData(Instance, MakeArrayView(Data), true);
	};

	if (HoverCell == Hero->Cell)
	{
		if (IsUsableEntrance(*Planet, HoverCell))
		{
			HoverText = FString::Printf(TEXT("%s: a right click goes %s"), *Hero->Name, Hero->Layer == EHexPlanetLayer::Surface ? TEXT("down into the underground") : TEXT("up to the surface"));
		}
		return;
	}

	// the cheapest ways from where the hero stands, found again when it moved or the map changed
	if (FieldHero != Hero->Id || FieldLayer != Hero->Layer || FieldRevision != MapRevision || Field.Start != Hero->Cell)
	{
		FindFieldFor(*Hero, Field);
		FieldHero = Hero->Id;
		FieldLayer = Hero->Layer;
		FieldRevision = MapRevision;
	}
	TArray<int32> Path;
	if (!Field.GetPath(HoverCell, Path) || Path.IsEmpty())
	{
		FString Problem;
		HoverText = CanStand(Hero->Layer, HoverCell, Hero->Id, &Problem) ? FString::Printf(TEXT("%s: no way there within %d days"), *Hero->Name, Config->PathMaxDays)
			: FString::Printf(TEXT("%s: %s"), *Hero->Name, *Problem);
		AddMark(Each.End.Get(), HoverCell, Config->PathEndRadius, Config->PathBlockedColor);
		return;
	}

	TArray<float> Costs;
	GetStepCosts(*Planet, *Hero, Path, Costs);
	const HexHeroPath::FSpend Spend = HexHeroPath::Spend(Costs, Hero->MovePoints, GetMaxMovePoints(*Hero));
	for (int32 Index = 0; Index < Path.Num(); ++Index)
	{
		const FLinearColor& Color = Index < Spend.StepsToday ? Config->PathColor : Config->PathFarColor;
		const bool bLast = Index == Path.Num() - 1;
		AddMark(bLast ? Each.End.Get() : Each.Dots.Get(), Path[Index], bLast ? Config->PathEndRadius : Config->PathDotRadius, Color);
	}
	HoverText = FString::Printf(TEXT("%s: %d %s, %.1f points, %s"), *Hero->Name, Path.Num(), Path.Num() == 1 ? TEXT("step") : TEXT("steps"), Spend.Total,
		Spend.Days <= 1 ? TEXT("there today") : *FString::Printf(TEXT("%d days"), Spend.Days));
	if (IsUsableEntrance(*Planet, HoverCell))
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

void UHexHeroes::OnLayerRebuilt(EHexPlanetLayer Layer)
{
	GroundRadii[int32(Layer)].Reset();
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
		if (Hero.bWalking)
		{
			Hero.StepAlpha += DeltaTime * Config->WalkSpeed;
			while (Hero.bWalking && Hero.StepAlpha >= 1.0f)
			{
				Hero.StepAlpha -= 1.0f;
				Hero.FromCell = Hero.Cell;
				if (!StartNextStep(Hero))
				{
					Hero.bWalking = false;
					Hero.StepAlpha = 0.0f;
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

	UpdateRing();
	UpdateHover();
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
		if (Hero.Layer == Layer && Hero.bPlaced)
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
	const FHexHero* Hero = GetSelected();
	if (!Hero || !Config || !Config->bCameraFollows || !Hero->bWalking || !Hero->bPlaced || Hero->Layer != Layer)
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
			UE_LOG(LogHexHero, Warning, TEXT("hex.Hero <list|add <Cell> [Class] [Player]|remove <Hero>|select <Hero>|next|send <Cell> [Hero]|hover <Cell|off>|endturn|restart>, on a built planet; a hero is its Id or its name"));
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
				UE_LOG(LogHexHero, Log, TEXT("  %d %s, %s of %s, cell %d of the %s, movement %.2f of %.0f%s%s"), Hero.Id, *Hero.Name, *Config->GetClass(Hero.Class).DisplayName,
					*Config->GetPlayer(Hero.Player).Name, Hero.Cell, GetPlanetLayerName(Hero.Layer), Hero.MovePoints, Heroes->GetMaxMovePoints(Hero),
					Hero.bWalking ? TEXT(", walking") : TEXT(""), Hero.Id == Heroes->GetSelectedId() ? TEXT(", chosen") : TEXT(""));
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
		else if (Action.Equals(TEXT("hover"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			// the way to a cell as if the cursor were over it, until hover off
			Heroes->SetHoverOverride(Layer, Args[1].Equals(TEXT("off"), ESearchCase::IgnoreCase) ? INDEX_NONE : ParseHeroCell(Args[1], *Planet, World));
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
		TEXT("Drives the heroes of the planet: list, add <Cell> [Class] [Player] on the layer shown, remove <Hero>, select <Hero>, next, send <Cell> [Hero], hover <Cell|off>, endturn, restart. A hero is its Id or its name; a cell is its number, focus for the one the camera looks at, or focus:<Steps>[:<Side>] for one that many steps from it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHeroCommand));
}
