// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexSites.h"
#include "HexPlanetConfig.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexCastles.h"
#include "HexHeroes.h"
#include "HexNeutrals.h"
#include "HexPlanetSurface.h"
#include "HexCastleConfig.h"
#include "HexMaterialConfig.h"
#include "HexPlanet.h"
#include "HexPlanetObjects.h"
#include "HexPlanetObjectSet.h"
#include "HexShroud.h"
#include "HexSiteConfig.h"
#include "HexSiteGenerator.h"
#include "HexBattleConfig.h"
#include "HexUnitFigure.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** Returns lines of sites.json in the order of their Ids, to compare two sets of them */
	TArray<FHexSiteLine> SortSiteLines(TArray<FHexSiteLine> Lines)
	{
		Lines.Sort([](const FHexSiteLine& A, const FHexSiteLine& B) { return A.Id < B.Id; });
		return Lines;
	}
}

AHexPlanet* UHexSites::GetPlanet(EHexPlanetLayer Layer) const
{
	return Surface ? Surface->GetLayerPlanet(Layer) : nullptr;
}

FLinearColor UHexSites::GetPlayerColor(int32 Player) const
{
	const UHexHeroConfig* Heroes = UHexHeroConfig::Load();
	return Heroes->Players.IsValidIndex(Player) ? Heroes->Players[Player].Color : Config->NeutralColor;
}

FString UHexSites::GetPlayerName(int32 Player) const
{
	const UHexHeroConfig* Heroes = UHexHeroConfig::Load();
	return Heroes->Players.IsValidIndex(Player) ? Heroes->Players[Player].Name : FString(TEXT("nobody"));
}

void UHexSites::Init(AHexPlanet* InSurface, int32 NumPlayers)
{
	Surface = InSurface;
	Config = UHexSiteConfig::Load();
	Sites.Reset();
	SavedLines.Reset();
	NextId = 1;
	bEdited = false;
	Amounts.SetNum(FMath::Max(NumPlayers, 1));

	if (Surface && Surface->IsSaved() && HexPlanetFile::HasSites(Surface->GetPlanetFolder()))
	{
		TArray<FHexSiteLine> Lines;
		FString Error;
		if (HexPlanetFile::LoadSites(Surface->GetPlanetFolder(), Lines, Error))
		{
			SetLines(Lines);
			SavedLines = SortSiteLines(Lines);
			bEdited = SortSiteLines(MakeLines()) != SavedLines;
			UE_LOG(LogHexHero, Log, TEXT("Sites: %d on the planet"), Sites.Num());
		}
		else
		{
			UE_LOG(LogHexHero, Error, TEXT("Sites: %s"), *Error);
		}
	}
	Restart();
}

TArray<FHexSiteLine> UHexSites::MakeLines() const
{
	TArray<FHexSiteLine> Lines;
	for (const FHexSite& Site : Sites)
	{
		// a boat a shipyard built in the game is not the map's
		if (Site.bBuilt)
		{
			continue;
		}
		FHexSiteLine& Line = Lines.AddDefaulted_GetRef();
		Line.Id = Site.Id;
		Line.Kind = Config->Kinds[Site.Kind].Id.ToString();
		Line.Layer = GetPlanetLayerName(Site.Layer);
		Line.Cell = Site.StartCell != INDEX_NONE ? Site.StartCell : Site.Cell;
		Line.Yaw = Site.Yaw;
	}
	return Lines;
}

void UHexSites::SetLines(const TArray<FHexSiteLine>& Lines)
{
	const int32 NumCells = Surface ? Surface->GetGrid().GetCells().Num() : 0;
	const TArray<FHexSiteLine> Before = MakeLines();
	TArray<FHexSite> Kept;
	for (const FHexSiteLine& Line : Lines)
	{
		// a site whose line is the same stays as it is, with its owner
		const int32 Same = Before.IndexOfByKey(Line);
		const FHexSite* Was = Same != INDEX_NONE ? Sites.FindByPredicate([&Line](const FHexSite& Each) { return Each.Id == Line.Id && !Each.bBuilt; }) : nullptr;
		if (Was)
		{
			Kept.Add(*Was);
			continue;
		}
		FHexSite Site;
		Site.Id = Line.Id;
		Site.Kind = Config->FindKind(Line.Kind);
		Site.Cell = Line.Cell;
		Site.StartCell = Line.Cell;
		Site.Yaw = Line.Yaw;
		if (Site.Id <= 0 || Site.Kind == INDEX_NONE || Line.Cell < 0 || Line.Cell >= NumCells || !ParsePlanetLayer(Line.Layer, Site.Layer)
			|| Kept.ContainsByPredicate([&Site](const FHexSite& Each) { return Each.Id == Site.Id || (Each.Layer == Site.Layer && Each.Cell == Site.Cell); }))
		{
			UE_LOG(LogHexHero, Error, TEXT("Sites: the site %d (%s) of sites.json has no Id, kind, cell or layer of its own, and is left out"), Line.Id, *Line.Kind);
			continue;
		}
		Site.Guard = MakeStartGuard(Site);
		Kept.Add(Site);
	}
	Sites = MoveTemp(Kept);
	for (const FHexSite& Site : Sites)
	{
		NextId = FMath::Max(NextId, Site.Id + 1);
	}
	bEdited = SortSiteLines(MakeLines()) != SavedLines;
	bDirty[0] = bDirty[1] = true;
	UHexHeroes::NoteMapChanged();
}

bool UHexSites::Save(const FString& PlanetFolder, FString& OutError)
{
	if (!HexPlanetFile::SaveSites(PlanetFolder, MakeLines(), OutError))
	{
		return false;
	}
	SavedLines = SortSiteLines(MakeLines());
	bEdited = false;
	return true;
}

const FHexSite* UHexSites::FindSiteAt(EHexPlanetLayer Layer, int32 Cell) const
{
	// a boat a hero sails lies nowhere
	return Sites.FindByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell && !Each.bTaken && Each.Captain == 0; });
}

const FHexSite* UHexSites::FindBoatAt(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexSite* Site = FindSiteAt(Layer, Cell);
	return Site && Config->Kinds[Site->Kind].Type == EHexSiteType::Boat ? Site : nullptr;
}

void UHexSites::SetCaptain(int32 BoatId, int32 HeroId)
{
	if (FHexSite* Boat = FindSiteMutable(BoatId))
	{
		Boat->Captain = HeroId;
		bDirty[int32(Boat->Layer)] = true;
		UHexHeroes::NoteMapChanged();
	}
}

void UHexSites::LeaveBoat(int32 BoatId, int32 Cell)
{
	if (FHexSite* Boat = FindSiteMutable(BoatId))
	{
		Boat->Captain = 0;
		Boat->Cell = Cell;
		Boat->bSeated = false;
		bDirty[int32(Boat->Layer)] = true;
		UHexHeroes::NoteMapChanged();
	}
}

int32 UHexSites::AddBuiltBoat(EHexPlanetLayer Layer, int32 Cell)
{
	const int32 Kind = Config->Kinds.IndexOfByPredicate([](const FHexSiteKind& Each) { return Each.Type == EHexSiteType::Boat; });
	if (Kind == INDEX_NONE || FindSiteAt(Layer, Cell))
	{
		return 0;
	}
	FHexSite Site;
	Site.Id = NextId++;
	Site.Kind = Kind;
	Site.Layer = Layer;
	Site.Cell = Cell;
	Site.StartCell = Cell;
	Site.bBuilt = true;
	Site.Yaw = (Cell * 47) % 360;
	Sites.Add(Site);
	bDirty[int32(Layer)] = true;
	UHexHeroes::NoteMapChanged();
	return Site.Id;
}

bool UHexSites::TakePickup(EHexPlanetLayer Layer, int32 Cell, const FString& Text, const FLinearColor& Color, bool bShown)
{
	FHexSite* Site = Sites.FindByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell && !Each.bTaken && Each.Captain == 0; });
	if (!Site || !Config->Kinds[Site->Kind].IsPickup())
	{
		return false;
	}
	Site->bTaken = true;
	Site->EventTime = GetWorldTime();
	Site->bFading = Site->bSeated && Config->PickupFadeTime > 0.0f;
	bDirty[int32(Layer)] = true;
	if (bShown)
	{
		AddPopup(*Site, Text, Color);
	}
	UHexHeroes::NoteMapChanged();
	return true;
}

uint32 UHexSites::GetSiteHash(const FHexSite& Site) const
{
	return HashCombine(HashCombine(GetTypeHash(Site.Id), GetTypeHash(GameNumber * 7919 + 3)), GetTypeHash(Site.StartCell));
}

const FHexSite* UHexSites::FindSite(int32 Id) const
{
	return Sites.FindByPredicate([Id](const FHexSite& Each) { return Each.Id == Id; });
}

FHexSite* UHexSites::FindSiteMutable(int32 Id)
{
	return Sites.FindByPredicate([Id](const FHexSite& Each) { return Each.Id == Id; });
}

TArray<FHexStack> UHexSites::MakeStartGuard(const FHexSite& Site) const
{
	TArray<FHexStack> Guard;
	const UHexCastleConfig* Castles = UHexCastleConfig::Load();
	if (!Config || !Config->Kinds.IsValidIndex(Site.Kind) || !Castles || Castles->Factions.IsEmpty())
	{
		return Guard;
	}
	const FName KindId = Config->Kinds[Site.Kind].Id;
	const FHexSiteGuardRule* Rule = Config->Guards.FindByPredicate([KindId](const FHexSiteGuardRule& Each) { return Each.Kind == KindId; });
	// a site on a pentagon of the surface is guarded strongly, as the developer asked on 2026-11-15
	const AHexPlanet* Planet = GetPlanet(Site.Layer);
	if (Site.Layer == EHexPlanetLayer::Surface && Planet && Planet->GetGrid().GetCells().IsValidIndex(Site.Cell) && Planet->GetGrid().GetCells()[Site.Cell].IsPentagon())
	{
		Rule = &Config->PentagonGuard;
	}
	if (!Rule || Rule->Share <= 0.0f || Rule->Weeks <= 0.0f)
	{
		return Guard;
	}
	// by a hash of the site alone, so a site has the same guard in every game
	FRandomStream Random(int32(HashCombine(GetTypeHash(Site.Id), HashCombine(GetTypeHash(Site.Cell), GetTypeHash(int32(Site.Layer) * 7919 + 17)))));
	if (Random.FRand() >= Rule->Share)
	{
		return Guard;
	}
	FHexStack& Stack = Guard.AddDefaulted_GetRef();
	// the faction the rule names, else any of the land
	TArray<int32> Land;
	for (int32 Faction = 0; Faction < Castles->Factions.Num(); ++Faction)
	{
		if (!Castles->Factions[Faction].bSea)
		{
			Land.Add(Faction);
		}
	}
	const int32 Named = Rule->Faction.IsNone() ? INDEX_NONE : Castles->FindFaction(Rule->Faction.ToString());
	Stack.Faction = Named != INDEX_NONE ? Named : Land.IsEmpty() ? 0 : Land[Random.RandHelper(Land.Num())];
	const TArray<FHexCreature>& Creatures = Castles->Factions[Stack.Faction].Creatures;
	if (Creatures.IsEmpty())
	{
		Guard.Reset();
		return Guard;
	}
	const int32 Low = FMath::Clamp(FMath::Min(Rule->LevelMin, Rule->LevelMax), 1, Creatures.Num());
	const int32 High = FMath::Clamp(FMath::Max(Rule->LevelMin, Rule->LevelMax), 1, Creatures.Num());
	Stack.Creature = Random.RandRange(Low, High) - 1;
	Stack.Count = FMath::Max(1, FMath::RoundToInt32(float(Creatures[Stack.Creature].Growth) * Rule->Weeks * Random.FRandRange(0.8f, 1.2f)));
	return Guard;
}

void UHexSites::UpdateGuards(const UHexHeroes& Heroes)
{
	const UHexBattleConfig* BattleConfig = UHexBattleConfig::Load();
	const UHexCastleConfig* Castles = UHexCastleConfig::Load();
	TSet<int32> Kept;
	for (const FHexSite& Site : Sites)
	{
		// the strongest stack it holds
		const FHexStack* Strongest = nullptr;
		for (const FHexStack& Stack : Site.Guard)
		{
			const FHexCreatureStats* Stats = Stack.Count > 0 ? Castles->GetStats(Stack) : nullptr;
			const FHexCreatureStats* Best = Strongest ? Castles->GetStats(*Strongest) : nullptr;
			if (Stats && (!Best || Stats->GetPower() * Stack.Count > Best->GetPower() * Strongest->Count))
			{
				Strongest = &Stack;
			}
		}
		AHexPlanet* Planet = Strongest && Site.bSeated && !Site.bTaken ? GetPlanet(Site.Layer) : nullptr;
		if (!Planet || !Planet->IsBuilt() || !Castles->Factions.IsValidIndex(Strongest->Faction))
		{
			continue;
		}
		Kept.Add(Site.Id);
		FGuardBody& Body = GuardBodies.FindOrAdd(Site.Id);
		if (!Body.Figure.IsValid() || Body.Faction != Strongest->Faction || Body.Creature != Strongest->Creature || Body.bUpgraded != Strongest->bUpgraded || Body.Cell != Site.Cell)
		{
			if (Body.Figure.IsValid())
			{
				Body.Figure->DestroyComponent();
			}
			float Scale = 1.0f;
			float Yaw = 0.0f;
			float Hover = 0.0f;
			const FName Creature = Castles->Factions[Strongest->Faction].Creatures[Strongest->Creature].Id;
			USceneComponent* Figure = HexUnitFigure::Make(Planet, Planet->GetRootComponent(), BattleConfig, Creature, Strongest->bUpgraded, Strongest->Creature,
				GetPlayerColor(Site.Owner), BattleConfig->NeutralScale * 0.9f, Scale, Yaw, Hover);
			if (!Figure)
			{
				continue;
			}
			// beside the place of a hero there, facing out of the mine
			const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
			const FVector3d Center = Cells[Site.Cell].Center;
			const double CellRadius = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Center, Cells[Cells[Site.Cell].Neighbors[0]].Center), -1.0, 1.0)) / FMath::Sqrt(3.0);
			const FVector2D Place = Config->Kinds[Site.Kind].HeroPlace;
			const FVector3d Forward = FVector3d(Site.Seat.GetUnitAxis(EAxis::X));
			const FVector3d Left = FVector3d(Site.Seat.GetUnitAxis(EAxis::Y));
			const FVector3d Direction = (Center + (Forward * (Place.X * 0.75) + Left * 0.55) * CellRadius).GetSafeNormal();
			// on the water beside a wreck
			const FVector3d Point = Direction * ((Config->Kinds[Site.Kind].IsAtSea() ? Planet->GetRadius() : Planet->FindGroundRadius(Site.Cell, Direction)) + Hover);
			const FVector3d Facing = (Forward - Direction * FVector3d::DotProduct(Forward, Direction)).GetSafeNormal();
			const FQuat Turn = FRotator(0.0, Yaw, 0.0).Quaternion();
			Figure->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZX(FVector(Direction), FVector(Facing)).ToQuat() * Turn, FVector(Point), FVector(Scale)));
			Figure->RegisterComponent();
			HexUnitFigure::Play(Figure, BattleConfig, Creature, EHexFigureAction::Idle, 0.9f);
			HexUnitFigure::Settle(Figure);
			Body.Figure = Figure;
			Body.Faction = Strongest->Faction;
			Body.Creature = Strongest->Creature;
			Body.bUpgraded = Strongest->bUpgraded;
			Body.Cell = Site.Cell;
			Body.bShown = true;
		}
		// nothing under the fog of war is drawn
		const bool bShown = !Heroes.IsHidden(Heroes.GetShroudPlayer(), Site.Layer, Site.Cell);
		if (Body.bShown != bShown && Body.Figure.IsValid())
		{
			Body.Figure->SetVisibility(bShown, true);
			Body.bShown = bShown;
		}
	}
	for (auto It = GuardBodies.CreateIterator(); It; ++It)
	{
		if (!Kept.Contains(It.Key()))
		{
			if (It.Value().Figure.IsValid())
			{
				It.Value().Figure->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
}

const FHexSite* UHexSites::FindBlockerAt(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexSite* Site = FindSiteAt(Layer, Cell);
	return Site && Config->Kinds[Site->Kind].FillsCell() ? Site : nullptr;
}

const FHexSite* UHexSites::FindPileAt(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexSite* Site = FindSiteAt(Layer, Cell);
	return Site && Config->Kinds[Site->Kind].IsPickup() ? Site : nullptr;
}

int32 UHexSites::AddSite(EHexPlanetLayer Layer, int32 Cell, int32 Kind, FString& OutProblem)
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	const UHexHeroes* Heroes = Surface ? Surface->GetHeroes() : nullptr;
	if (!Config || !Config->Kinds.IsValidIndex(Kind) || !Planet || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		OutProblem = TEXT("there is no such kind or cell");
		return 0;
	}
	const FHexSiteKind& KindInfo = Config->Kinds[Kind];
	const auto IsSeaCell = [Planet, Layer](int32 Each) { return Layer == EHexPlanetLayer::Surface && Planet->GetCellLevel(Each) < 0; };
	if (KindInfo.IsAtSea())
	{
		// a boat lies on the sea, where no hero stands
		if (!IsSeaCell(Cell))
		{
			OutProblem = TEXT("a boat lies on the sea");
			return 0;
		}
		if (Heroes && Heroes->FindHeroAt(Layer, Cell))
		{
			OutProblem = TEXT("a hero is there");
			return 0;
		}
	}
	// where a hero could stand, and none does, as a site fills its cell
	else if (Heroes && !Heroes->CanStand(Layer, Cell, 0, &OutProblem))
	{
		return 0;
	}
	// a shipyard on a coast
	if (KindInfo.Type == EHexSiteType::Shipyard && !Planet->GetGrid().GetCells()[Cell].Neighbors.ContainsByPredicate(IsSeaCell))
	{
		OutProblem = TEXT("a shipyard stands beside the sea");
		return 0;
	}
	if (Planet->FindEntrance(Cell) != INDEX_NONE)
	{
		OutProblem = TEXT("an entrance stands there");
		return 0;
	}
	if (Sites.ContainsByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell; }))
	{
		OutProblem = TEXT("a site stands there already");
		return 0;
	}
	FHexSite Site;
	Site.Id = NextId++;
	Site.Kind = Kind;
	Site.Layer = Layer;
	Site.Cell = Cell;
	Site.StartCell = Cell;
	Site.Guard = MakeStartGuard(Site);
	Sites.Add(Site);
	bEdited = SortSiteLines(MakeLines()) != SavedLines;
	bDirty[int32(Layer)] = true;
	UHexHeroes::NoteMapChanged();
	UE_LOG(LogHexHero, Log, TEXT("Sites: %s stands on cell %d of the %s as site %d"), *Config->Kinds[Kind].DisplayName, Cell, GetPlanetLayerName(Layer), Site.Id);
	return Site.Id;
}

bool UHexSites::RemoveSiteAt(EHexPlanetLayer Layer, int32 Cell)
{
	const int32 Index = Sites.IndexOfByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Sites.RemoveAt(Index);
	bEdited = SortSiteLines(MakeLines()) != SavedLines;
	bDirty[int32(Layer)] = true;
	UHexHeroes::NoteMapChanged();
	return true;
}

bool UHexSites::Clear()
{
	if (Sites.IsEmpty())
	{
		return false;
	}
	SetLines({});
	return true;
}

int32 UHexSites::Generate(FString& OutReport, const int32* Seed, float DensityScale, const TBitArray<>* SurfaceAvoid, bool bAddPickups, int32 OnlyType, float ClusterShare)
{
	const UHexHeroes* Heroes = Surface ? Surface->GetHeroes() : nullptr;
	if (!Config || !Heroes || !Surface->IsBuilt())
	{
		OutReport = TEXT("the planet is not built yet");
		return Sites.Num();
	}
	const double StartTime = FPlatformTime::Seconds();
	const FHexSphereGrid& Grid = Surface->GetGrid();
	const int32 NumCells = Grid.GetCells().Num();
	const FHexSiteGeneration& Settings = Config->Generation;

	HexSiteGenerator::FInput Input;
	Input.Settings = Settings;
	// a map of zones asks for more of them (SiteDensity of its settings)
	Input.Settings.Density *= FMath::Max(DensityScale, 0.0f);
	// the share of the piles that lie beside another, as the palette asks for it
	if (ClusterShare >= 0.0f)
	{
		Input.Settings.PileClusterChance = FMath::Clamp(ClusterShare, 0.0f, 1.0f);
	}
	Input.Seed = Seed ? *Seed : Surface->GetManifest().Seed * 7919 + GenerateCount++ * 104729;
	for (const FHexSiteShare& Share : Settings.Shares)
	{
		const int32 Kind = Config->FindKind(Share.Kind.ToString());
		if (Kind == INDEX_NONE)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Sites: the generation names the kind %s, which DA_HexSites does not have"), *Share.Kind.ToString());
			continue;
		}
		// adding to what a planet has, as hex.Site scatter does, takes piles and chests alone
		if ((bAddPickups && !Config->Kinds[Kind].IsPickup()) || (OnlyType >= 0 && int32(Config->Kinds[Kind].Type) != OnlyType))
		{
			continue;
		}
		HexSiteGenerator::FKind& Each = Input.Kinds.AddDefaulted_GetRef();
		Each.Kind = Kind;
		Each.Type = Config->Kinds[Kind].Type;
		Each.PerThousand[int32(EHexPlanetLayer::Surface)] = Share.PerThousand;
		Each.PerThousand[int32(EHexPlanetLayer::Underground)] = Share.UndergroundPerThousand;
		Each.bNearStart = Share.bNearStart;
	}

	// what the pentagons of land hold, a kind of its own that nothing else scatters, as the rich gold mine, taken in with no share
	const auto FindInput = [&](FName Id)
	{
		int32 Found = Input.Kinds.IndexOfByPredicate([this, Id](const HexSiteGenerator::FKind& Each) { return Config->Kinds[Each.Kind].Id == Id; });
		const int32 Kind = Config->FindKind(Id.ToString());
		if (Found == INDEX_NONE && Kind != INDEX_NONE && !bAddPickups && OnlyType < 0)
		{
			HexSiteGenerator::FKind& Each = Input.Kinds.AddDefaulted_GetRef();
			Each.Kind = Kind;
			Each.Type = Config->Kinds[Kind].Type;
			Found = Input.Kinds.Num() - 1;
		}
		return Found;
	};
	Input.PentagonKind = FindInput(Config->PentagonKind);
	Input.PentagonFallbackKind = FindInput(Config->PentagonFallbackKind);
	TArray<int32> Ring;
	const auto Avoid = [&](int32 Layer, int32 Cell, int32 Rings)
	{
		if (Input.Avoid[Layer].Num() == NumCells && Grid.GetCells().IsValidIndex(Cell))
		{
			HexShroud::FindWithin(Grid, Cell, FMath::Max(Rings, 0), Ring);
			for (const int32 Each : Ring)
			{
				Input.Avoid[Layer][Each] = true;
			}
		}
	};
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		// a layer without its data yet gets no sites; an underground never shown has its levels, and its objects are read from its file
		const AHexPlanet* Planet = GetPlanet(EHexPlanetLayer(Layer));
		if (!Planet || !Planet->IsDataLoaded() || Planet->GetGrid().GetCells().Num() != NumCells)
		{
			continue;
		}
		Input.Walkable[Layer].Init(false, NumCells);
		Input.Avoid[Layer].Init(false, NumCells);
		const UHexPlanetObjects* Objects = Planet->IsBuilt() ? Planet->GetObjects() : nullptr;
		const TBitArray<> Saved = Settings.bAvoidObjects && !Objects ? UHexPlanetObjects::FindSavedCells(*Planet) : TBitArray<>();
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			Input.Walkable[Layer][Cell] = Heroes->IsOpenGround(EHexPlanetLayer(Layer), Cell);
			Input.Avoid[Layer][Cell] = Settings.bAvoidObjects && (Objects ? !Objects->GetCell(Cell).IsEmpty() : Saved.IsValidIndex(Cell) && Saved[Cell]);
		}
		for (const FHexEntrance& Entrance : Planet->GetEntrances())
		{
			Avoid(Layer, Entrance.Cell, Settings.EntranceClearRings);
		}
	}
	// the cells of the castles are closed, and what lies around them stays free
	if (const UHexCastles* Castles = Heroes->GetCastles())
	{
		for (const FHexCastle& Castle : Castles->GetCastles())
		{
			for (const int32 Cell : Castle.Cells)
			{
				Avoid(int32(Castle.Layer), Cell, Settings.EntranceClearRings);
				if (Input.Walkable[int32(Castle.Layer)].IsValidIndex(Cell))
				{
					Input.Walkable[int32(Castle.Layer)][Cell] = false;
				}
			}
		}
	}
	// adding to what there is, the sites there are keep their cells and their spacing, and nothing more comes near where a player starts
	if (bAddPickups)
	{
		Input.Settings.StartPiles = 0;
		for (const FHexSite& Site : Sites)
		{
			Avoid(int32(Site.Layer), Site.Cell, FMath::Max(FMath::CeilToInt32(Settings.Spacing) - 1, 0));
			if (Input.Walkable[int32(Site.Layer)].IsValidIndex(Site.Cell))
			{
				Input.Walkable[int32(Site.Layer)][Site.Cell] = false;
			}
		}
	}
	// and the cells the caller keeps free, as the passes of a map of zones
	if (SurfaceAvoid && SurfaceAvoid->Num() == NumCells && Input.Avoid[0].Num() == NumCells)
	{
		Input.Avoid[0].CombineWithBitwiseOR(*SurfaceAvoid, EBitwiseOperatorFlags::MaintainSize);
	}
	// where the heroes start, and for each player where its first one does
	TMap<int32, const FHexHero*> First;
	for (const FHexHero& Hero : Heroes->GetHeroes())
	{
		Avoid(int32(Hero.StartLayer), Hero.StartCell, Settings.StartClearRings);
		const FHexHero*& Each = First.FindOrAdd(Hero.Player);
		if (!Each || Hero.Id < Each->Id)
		{
			Each = &Hero;
		}
	}
	First.KeySort([](int32 A, int32 B) { return A < B; });
	for (const TPair<int32, const FHexHero*>& Each : First)
	{
		Input.Starts.Add({int32(Each.Value->StartLayer), Each.Value->StartCell});
	}
	// and every castle, from a cell before it, gets the mines every player needs beside it
	if (const UHexCastles* Castles = !bAddPickups ? Heroes->GetCastles() : nullptr)
	{
		for (const FHexCastle& Castle : Castles->GetCastles())
		{
			const TBitArray<>& Open = Input.Walkable[int32(Castle.Layer)];
			int32 Before = INDEX_NONE;
			for (int32 Index = 0; Index < 3 && Before == INDEX_NONE; ++Index)
			{
				for (const int32 Neighbor : Grid.GetCells()[Castle.Cells[Index]].Neighbors)
				{
					if (!Castle.HasCell(Neighbor) && Open.IsValidIndex(Neighbor) && Open[Neighbor])
					{
						Before = Neighbor;
						break;
					}
				}
			}
			if (Before != INDEX_NONE)
			{
				HexSiteGenerator::FStart& Start = Input.Starts.AddDefaulted_GetRef();
				Start.Layer = int32(Castle.Layer);
				Start.Cell = Before;
				Start.bMinesOnly = true;
			}
		}
	}

	TArray<HexSiteGenerator::FSite> Found;
	HexSiteGenerator::FReport Report;
	HexSiteGenerator::Generate(Grid, Input, Found, &Report);

	TArray<FHexSiteLine> Lines = bAddPickups ? MakeLines() : TArray<FHexSiteLine>();
	int32 NextLineId = 1;
	for (const FHexSiteLine& Line : Lines)
	{
		NextLineId = FMath::Max(NextLineId, Line.Id + 1);
	}
	int32 Counts[NumPlanetLayers][8] = {};
	for (const HexSiteGenerator::FSite& Each : Found)
	{
		FHexSiteLine& Line = Lines.AddDefaulted_GetRef();
		Line.Id = NextLineId++;
		Line.Kind = Config->Kinds[Each.Kind].Id.ToString();
		Line.Layer = GetPlanetLayerName(EHexPlanetLayer(Each.Layer));
		Line.Cell = Each.Cell;
		++Counts[Each.Layer][int32(Config->Kinds[Each.Kind].Type)];
	}
	// every site anew, as an old one whose line came out the same would keep its owner
	Sites.Reset();
	NextId = 1;
	SetLines(Lines);
	// shipyards on the coasts and boats beside them, besides
	const FIntPoint Coast = OnlyType >= 0 ? FIntPoint::ZeroValue : !bAddPickups || !Sites.ContainsByPredicate([this](const FHexSite& Each) { return Config->Kinds[Each.Kind].Type == EHexSiteType::Shipyard; })
		? ScatterCoast(Input.Seed, Input.Settings.Density) : FIntPoint::ZeroValue;
	Restart();

	const int32 Mine = int32(EHexSiteType::Mine);
	const int32 Pile = int32(EHexSiteType::Pile);
	const int32 Mill = int32(EHexSiteType::Mill);
	OutReport = FString::Printf(TEXT("%d sites: on the surface %d mines, %d piles and %d mills on %d cells, underground %d mines and %d piles on %d cells; %d near where %d players start%s"),
		Sites.Num(), Counts[0][Mine], Counts[0][Pile], Counts[0][Mill], Report.Cells[0], Counts[1][Mine], Counts[1][Pile] + Counts[1][Mill], Report.Cells[1],
		Report.NearStarts, Input.Starts.Num(), Report.NoRoom > 0 ? *FString::Printf(TEXT("; %d found no room"), Report.NoRoom) : TEXT(""));
	OutReport += FString::Printf(TEXT("; %d piles beside another, %d on pentagons"), Report.Clustered, Report.Pentagons);
	OutReport += FString::Printf(TEXT("; %d artifacts, %d shipyards and %d boats"), Counts[0][int32(EHexSiteType::Artifact)] + Counts[1][int32(EHexSiteType::Artifact)], Coast.X, Coast.Y);
	UE_LOG(LogHexHero, Log, TEXT("Sites: generated with the seed %d in %.1f ms, %s"), Input.Seed, (FPlatformTime::Seconds() - StartTime) * 1000.0, *OutReport);
	return Sites.Num();
}

FIntPoint UHexSites::ScatterCoast(int32 Seed, float DensityScale)
{
	const UHexHeroes* Heroes = Surface ? Surface->GetHeroes() : nullptr;
	const int32 Shipyard = Config->Kinds.IndexOfByPredicate([](const FHexSiteKind& Each) { return Each.Type == EHexSiteType::Shipyard; });
	const int32 Boat = Config->Kinds.IndexOfByPredicate([](const FHexSiteKind& Each) { return Each.Type == EHexSiteType::Boat; });
	if (!Heroes || !Surface->IsBuilt() || Shipyard == INDEX_NONE || Boat == INDEX_NONE)
	{
		return FIntPoint::ZeroValue;
	}
	const TArray<FHexSphereCell>& Cells = Surface->GetGrid().GetCells();
	const int32 NumCells = Cells.Num();
	const auto IsSeaCell = [this](int32 Cell) { return Surface->GetCellLevel(Cell) < 0; };
	// the cells that take nothing: what stands there and around it, the castles and where the heroes start
	TBitArray<> Taken(false, NumCells);
	TArray<int32> Ring;
	const auto Close = [&](int32 Cell, int32 Rings)
	{
		if (Cells.IsValidIndex(Cell))
		{
			HexShroud::FindWithin(Surface->GetGrid(), Cell, Rings, Ring);
			for (const int32 Each : Ring)
			{
				Taken[Each] = true;
			}
		}
	};
	for (const FHexSite& Site : Sites)
	{
		if (Site.Layer == EHexPlanetLayer::Surface)
		{
			Close(Site.Cell, 2);
		}
	}
	if (const UHexCastles* Castles = Heroes->GetCastles())
	{
		for (const FHexCastle& Castle : Castles->GetCastles())
		{
			for (const int32 Cell : Castle.Cells)
			{
				if (Castle.Layer == EHexPlanetLayer::Surface)
				{
					Close(Cell, 2);
				}
			}
		}
	}
	for (const FHexHero& Hero : Heroes->GetHeroes())
	{
		if (Hero.StartLayer == EHexPlanetLayer::Surface)
		{
			Close(Hero.StartCell, 2);
		}
	}
	for (const FHexEntrance& Entrance : Surface->GetEntrances())
	{
		Close(Entrance.Cell, 1);
	}
	// the open ground beside the sea, and the sea beside it, in a random order of the seed
	TBitArray<> Open(false, NumCells);
	TArray<int32> Coast;
	TArray<int32> Shore;
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		Open[Cell] = Heroes->IsOpenGround(EHexPlanetLayer::Surface, Cell);
	}
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		if (Open[Cell] && Cells[Cell].Neighbors.ContainsByPredicate(IsSeaCell))
		{
			Coast.Add(Cell);
		}
		else if (IsSeaCell(Cell) && Cells[Cell].Neighbors.ContainsByPredicate([&Open](int32 Each) { return Open[Each]; }))
		{
			Shore.Add(Cell);
		}
	}
	FRandomStream Stream(Seed * 31 + 977);
	for (TArray<int32>* List : {&Coast, &Shore})
	{
		for (int32 Index = List->Num() - 1; Index > 0; --Index)
		{
			List->Swap(Index, Stream.RandRange(0, Index));
		}
	}
	const auto Wanted = [&](float PerThousand)
	{
		const float Number = FMath::Max(PerThousand, 0.0f) * FMath::Max(DensityScale, 0.0f) * float(Coast.Num()) / 1000.0f;
		return FMath::FloorToInt32(Number) + (Stream.FRand() < Number - FMath::FloorToInt32(Number) ? 1 : 0);
	};
	FIntPoint Placed = FIntPoint::ZeroValue;
	FString Problem;
	// a shipyard where closing its cell cuts no way, with the sea beside it free for the boats it builds
	const int32 Yards = Wanted(Config->ShipyardsPerThousandCoast);
	for (int32 Index = 0; Index < Coast.Num() && Placed.X < Yards; ++Index)
	{
		const int32 Cell = Coast[Index];
		if (Taken[Cell] || !HexSiteGenerator::KeepsWays(Surface->GetGrid(), Open, Cell) || AddSite(EHexPlanetLayer::Surface, Cell, Shipyard, Problem) == 0)
		{
			continue;
		}
		Open[Cell] = false;
		Close(Cell, 4);
		++Placed.X;
	}
	// a boat on the sea beside open ground, away from the others
	const int32 Boats = Wanted(Config->BoatsPerThousandCoast);
	for (int32 Index = 0; Index < Shore.Num() && Placed.Y < Boats; ++Index)
	{
		const int32 Cell = Shore[Index];
		if (Taken[Cell] || AddSite(EHexPlanetLayer::Surface, Cell, Boat, Problem) == 0)
		{
			continue;
		}
		Close(Cell, 5);
		++Placed.Y;
	}
	// wrecks out on the sea, away from everything, as the developer asked on 2026-11-15
	const int32 Wreck = Config->Kinds.IndexOfByPredicate([](const FHexSiteKind& Each) { return Each.bAtSea && Each.Type == EHexSiteType::Chest; });
	TArray<int32> Flotsam;
	for (int32 Kind = 0; Kind < Config->Kinds.Num(); ++Kind)
	{
		if (Config->Kinds[Kind].bAtSea && Config->Kinds[Kind].Type == EHexSiteType::Pile)
		{
			Flotsam.Add(Kind);
		}
	}
	int32 Floating = 0;
	int32 Wrecks = 0;
	if (Wreck != INDEX_NONE)
	{
		TArray<int32> Sea;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (IsSeaCell(Cell) && !Cells[Cell].IsPentagon())
			{
				Sea.Add(Cell);
			}
		}
		for (int32 Index = Sea.Num() - 1; Index > 0; --Index)
		{
			Sea.Swap(Index, Stream.RandRange(0, Index));
		}
		const float Number = FMath::Max(Config->WrecksPerThousandSea, 0.0f) * FMath::Max(DensityScale, 0.0f) * float(Sea.Num()) / 1000.0f;
		const int32 WantedWrecks = FMath::FloorToInt32(Number) + (Stream.FRand() < Number - FMath::FloorToInt32(Number) ? 1 : 0);
		for (int32 Index = 0; Index < Sea.Num() && Wrecks < WantedWrecks; ++Index)
		{
			const int32 Cell = Sea[Index];
			if (Taken[Cell] || AddSite(EHexPlanetLayer::Surface, Cell, Wreck, Problem) == 0)
			{
				continue;
			}
			Close(Cell, FMath::Max(Config->WreckSpacing, 1));
			++Wrecks;
		}
		// and what floats, of its kinds by chance
		const float FloatNumber = FMath::Max(Config->FlotsamPerThousandSea, 0.0f) * FMath::Max(DensityScale, 0.0f) * float(Sea.Num()) / 1000.0f;
		const int32 WantedFloating = Flotsam.IsEmpty() ? 0 : FMath::FloorToInt32(FloatNumber);
		for (int32 Index = Sea.Num() - 1; Index >= 0 && Floating < WantedFloating; --Index)
		{
			const int32 Cell = Sea[Index];
			if (Taken[Cell] || AddSite(EHexPlanetLayer::Surface, Cell, Flotsam[Stream.RandRange(0, Flotsam.Num() - 1)], Problem) == 0)
			{
				continue;
			}
			Close(Cell, FMath::Max(Config->FlotsamSpacing, 1));
			++Floating;
		}
	}
	UE_LOG(LogHexHero, Log, TEXT("Sites: %d shipyards and %d boats on %d cells of the coast, %d wrecks and %d floating piles at sea"), Placed.X, Placed.Y, Coast.Num(), Wrecks, Floating);
	return Placed;
}

int32 UHexSites::MakeVaults(int32 Count, int32 Seed, FString& OutReport)
{
	UHexHeroes* Heroes = Surface ? Surface->GetHeroes() : nullptr;
	UHexNeutrals* Neutrals = Heroes ? Heroes->GetNeutrals() : nullptr;
	UHexPlanetObjects* Objects = Surface ? Surface->GetObjects() : nullptr;
	const UHexCastleConfig* CastleConfig = UHexCastleConfig::Load();
	if (!Heroes || !Neutrals || !Objects || !Surface->IsBuilt() || !CastleConfig || Count <= 0)
	{
		OutReport = TEXT("no planet to make vaults on");
		return 0;
	}
	const FHexSphereGrid& Grid = Surface->GetGrid();
	const TArray<FHexSphereCell>& Cells = Grid.GetCells();
	const int32 NumCells = Cells.Num();
	const EHexPlanetLayer Layer = EHexPlanetLayer::Surface;
	const int32 Forest = Objects->GetSets().IndexOfByPredicate([](const UHexPlanetObjectSet* Each) { return Each && Each->Id == TEXT("Forest"); });
	FRandomStream Random(Seed * 7919 + 4111);

	// what a vault keeps away from: the castles, where heroes start, the entrances, and the vaults made
	TBitArray<> Busy(false, NumCells);
	TArray<int32> Ring;
	const auto Keep = [&](int32 Cell, int32 Rings)
	{
		HexShroud::FindWithin(Grid, Cell, Rings, Ring);
		for (const int32 Each : Ring)
		{
			Busy[Each] = true;
		}
	};
	if (const UHexCastles* Castles = Heroes->GetCastles())
	{
		for (const FHexCastle& Castle : Castles->GetCastles())
		{
			for (const int32 Cell : Castle.Cells)
			{
				if (Castle.Layer == Layer)
				{
					Keep(Cell, 5);
				}
			}
		}
	}
	for (const FHexHero& Hero : Heroes->GetHeroes())
	{
		if (Hero.StartLayer == Layer)
		{
			Keep(Hero.StartCell, 6);
		}
	}
	for (const FHexEntrance& Entrance : Surface->GetEntrances())
	{
		Keep(Entrance.Cell, 3);
	}
	// the ground a hero walks; a cell free of everything for the inside and the gate
	TBitArray<> Walk(false, NumCells);
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		Walk[Cell] = Heroes->IsOpenGround(Layer, Cell);
	}
	const auto IsFree = [&](int32 Cell)
	{
		return Walk[Cell] && !Busy[Cell] && !Cells[Cell].IsPentagon() && !FindSiteAt(Layer, Cell) && !Neutrals->FindAt(Layer, Cell) && !Heroes->FindHeroAt(Layer, Cell);
	};
	TArray<int32> Order;
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		Order.Add(Cell);
	}
	for (int32 Index = Order.Num() - 1; Index > 0; --Index)
	{
		Order.Swap(Index, Random.RandRange(0, Index));
	}
	TMap<int32, int32> Levels;
	TMap<int32, FHexCellObjects> Changes;
	TArray<int32> Inside;
	TArray<int32> Wall;
	TArray<int32> Queue;
	TBitArray<> Seen;
	int32 Made = 0;
	FString Problem;
	for (const int32 Middle : Order)
	{
		if (Made >= Count)
		{
			break;
		}
		// the inside: a cell and its six, all free ground
		HexShroud::FindWithin(Grid, Middle, 1, Inside);
		if (Inside.Num() != 7 || Inside.ContainsByPredicate([&](int32 Cell) { return !IsFree(Cell); }))
		{
			continue;
		}
		// the ring around it, land that no site, stack or hero stands on and that is not kept free
		HexShroud::FindWithin(Grid, Middle, 2, Wall);
		Wall.RemoveAll([&Inside](int32 Cell) { return Inside.Contains(Cell); });
		if (Wall.ContainsByPredicate([&](int32 Cell) { return Busy[Cell] || Surface->GetCellLevel(Cell) < 0 || FindSiteAt(Layer, Cell) || Neutrals->FindAt(Layer, Cell)
			|| Heroes->FindHeroAt(Layer, Cell); }))
		{
			continue;
		}
		// the gate: a cell of the ring a hero walks with ground it walks outside it
		int32 Gate = INDEX_NONE;
		int32 Outside = INDEX_NONE;
		for (int32 Try = 0; Try < Wall.Num() && Gate == INDEX_NONE; ++Try)
		{
			const int32 Cell = Wall[(Try + Middle) % Wall.Num()];
			if (!IsFree(Cell))
			{
				continue;
			}
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (!Inside.Contains(Neighbor) && !Wall.Contains(Neighbor) && Walk[Neighbor])
				{
					Gate = Cell;
					Outside = Neighbor;
					break;
				}
			}
		}
		if (Gate == INDEX_NONE)
		{
			continue;
		}
		// the wall cuts no way of the land around it: every open cell beside it outside is still reached from the gate, around the vault
		TBitArray<> After = Walk;
		for (const int32 Cell : Wall)
		{
			if (Cell != Gate)
			{
				After[Cell] = false;
			}
		}
		for (const int32 Cell : Inside)
		{
			After[Cell] = false;
		}
		After[Gate] = false;
		Seen.Init(false, NumCells);
		Queue.Reset();
		Queue.Add(Outside);
		Seen[Outside] = true;
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			for (const int32 Neighbor : Cells[Queue[Index]].Neighbors)
			{
				if (After[Neighbor] && !Seen[Neighbor])
				{
					Seen[Neighbor] = true;
					Queue.Add(Neighbor);
				}
			}
		}
		bool bKeeps = Queue.Num() > 60;
		for (int32 Index = 0; bKeeps && Index < Wall.Num(); ++Index)
		{
			for (const int32 Neighbor : Cells[Wall[Index]].Neighbors)
			{
				bKeeps &= !After[Neighbor] || Seen[Neighbor];
			}
		}
		if (!bKeeps)
		{
			continue;
		}
		// the wall: forest or mountains a cell wide, by chance; a pentagon or a cell beside the sea takes forest
		for (const int32 Cell : Wall)
		{
			if (Cell == Gate)
			{
				continue;
			}
			const bool bMountain = Forest == INDEX_NONE || (!Cells[Cell].IsPentagon() && Surface->GetCellLevel(Cell) >= 0 && Random.FRand() < 0.5f);
			if (bMountain)
			{
				Levels.Add(Cell, HexPlanetSurface::MountainLevel);
				Changes.Add(Cell, FHexCellObjects());
			}
			else
			{
				FHexCellObjects Trees;
				Trees.Cover = uint8(Forest + 1);
				Trees.Density = 3;
				Changes.Add(Cell, Trees);
			}
			Walk[Cell] = false;
		}
		// the inside cleared of what grows there, the gate too
		for (const int32 Cell : Inside)
		{
			Changes.Add(Cell, FHexCellObjects());
		}
		Changes.Add(Gate, FHexCellObjects());
		// what it holds: an artifact in the middle, great now and then, a chest, gold and the rare resources
		const TCHAR* Around[6] = {TEXT("Chest"), TEXT("GoldPile"), TEXT("GemPile"), TEXT("CrystalPile"), TEXT("GoldPile"), TEXT("MercuryPile")};
		AddSite(Layer, Middle, Config->FindKind(Random.FRand() < 0.35f ? TEXT("GreatArtifact") : TEXT("Artifact")), Problem);
		int32 Slot = Random.RandRange(0, 5);
		for (const int32 Cell : Inside)
		{
			if (Cell != Middle)
			{
				AddSite(Layer, Cell, Config->FindKind(Around[Slot++ % 6]), Problem);
			}
		}
		// a strong stack of the land in the gate
		TArray<int32> Land;
		for (int32 Faction = 0; Faction < CastleConfig->Factions.Num(); ++Faction)
		{
			if (!CastleConfig->Factions[Faction].bSea)
			{
				Land.Add(Faction);
			}
		}
		if (!Land.IsEmpty())
		{
			const int32 Faction = Land[Random.RandRange(0, Land.Num() - 1)];
			const int32 Level = FMath::Clamp(Random.RandRange(3, 5), 0, CastleConfig->Factions[Faction].Creatures.Num() - 1);
			const FHexStack Stack{Faction, Level, Random.FRand() < 0.5f, FMath::Max(FMath::RoundToInt32(CastleConfig->Factions[Faction].Creatures[Level].Growth * 2.0f), 1)};
			Neutrals->Add(Layer, Gate, Stack, Problem);
		}
		Keep(Middle, 8);
		++Made;
		UE_LOG(LogHexHero, Log, TEXT("Sites: a vault around cell %d, its gate on cell %d"), Middle, Gate);
	}
	if (!Levels.IsEmpty())
	{
		Surface->SetCellLevels(Levels);
	}
	if (!Changes.IsEmpty())
	{
		Objects->SetCells(Changes);
	}
	OutReport = FString::Printf(TEXT("%d vaults of %d wanted, %d cells of mountains in their walls"), Made, Count, Levels.Num());
	UE_LOG(LogHexHero, Log, TEXT("Sites: %s"), *OutReport);
	return Made;
}

EHexSiteGoal UHexSites::GetGoal(const FHexSite& Site, int32 Player, int32 Week) const
{
	if (Site.bTaken || !Config->Kinds.IsValidIndex(Site.Kind))
	{
		return EHexSiteGoal::None;
	}
	switch (Config->Kinds[Site.Kind].Type)
	{
	case EHexSiteType::Mine:
		return Site.Owner == Player ? EHexSiteGoal::None
			: Player >= 0 && Player < 32 && (Site.FormerOwners & (1u << Player)) != 0 ? EHexSiteGoal::LostMine : EHexSiteGoal::Mine;
	case EHexSiteType::Mill:
		return Site.VisitedWeek == Week ? EHexSiteGoal::None : EHexSiteGoal::Gather;
	case EHexSiteType::Shipyard:
	case EHexSiteType::Boat:
		return EHexSiteGoal::None;
	default:
		return EHexSiteGoal::Gather;
	}
}

int32 UHexSites::GetPileAmount(const FHexSite& Site) const
{
	const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
	// by the site and the game, so a pile holds the same until a new game
	FRandomStream Stream(Site.Id * 7919 + GameNumber * 104729);
	const int32 Amount = Stream.RandRange(FMath::Min(Kind.AmountMin, Kind.AmountMax), FMath::Max(Kind.AmountMin, Kind.AmountMax));
	// a chest in steps of 500
	return Kind.Type == EHexSiteType::Chest ? FMath::Max(FMath::RoundToInt32(Amount / 500.0f), 1) * 500 : Amount;
}

int32 UHexSites::GetChestExperience(const FHexSite& Site) const
{
	return FMath::Max(GetPileAmount(Site) - Config->ChestExperienceLess, 0);
}

FString UHexSites::TakeChest(int32 Player, const FString& HeroName, EHexPlanetLayer Layer, int32 Cell, bool bGold, bool bShown)
{
	FHexSite* Site = Sites.FindByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell && !Each.bTaken; });
	if (!Site || !Amounts.IsValidIndex(Player) || Config->Kinds[Site->Kind].Type != EHexSiteType::Chest)
	{
		return FString();
	}
	const int32 Gold = GetPileAmount(*Site);
	const int32 Experience = GetChestExperience(*Site);
	Site->bTaken = true;
	Site->EventTime = GetWorldTime();
	Site->bFading = Site->bSeated && Config->PickupFadeTime > 0.0f;
	bDirty[int32(Layer)] = true;
	const int32 GoldIndex = Config->FindResource(TEXT("Gold"));
	if (bGold && Config->Resources.IsValidIndex(GoldIndex))
	{
		Amounts[Player][GoldIndex] += Gold;
	}
	if (bShown)
	{
		AddPopup(*Site, bGold ? FString::Printf(TEXT("+%d Gold"), Gold) : FString::Printf(TEXT("+%d Experience"), Experience),
			bGold ? Config->Resources[FMath::Max(GoldIndex, 0)].Color : FLinearColor(0.6f, 0.85f, 1.0f, 1.0f));
	}
	UHexHeroes::NoteMapChanged();
	return bGold ? FString::Printf(TEXT("%s opens a chest and takes %d gold"), *HeroName, Gold) : FString::Printf(TEXT("%s opens a chest and gains %d experience"), *HeroName, Experience);
}

int32 UHexSites::GetSiteResource(const FHexSite& Site) const
{
	const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
	if (Kind.Type != EHexSiteType::Mill)
	{
		return Config->FindResource(Kind.Resource);
	}
	// a mill has a resource of its own, by the site and the game
	FRandomStream Stream(Site.Id * 6151 + GameNumber * 15485863);
	return Config->Resources.IsEmpty() ? int32(INDEX_NONE) : Stream.RandRange(0, Config->Resources.Num() - 1);
}

double UHexSites::GetWorldTime() const
{
	const UWorld* World = Surface ? Surface->GetWorld() : nullptr;
	return World ? World->GetTimeSeconds() : 0.0;
}

void UHexSites::OnLayerRebuilt(EHexPlanetLayer Layer)
{
	bDirty[int32(Layer)] = true;
	for (FHexSite& Site : Sites)
	{
		if (Site.Layer == Layer)
		{
			Site.bSeated = false;
		}
	}
}

bool UHexSites::CanPay(int32 Player, const TMap<FName, int32>& Cost) const
{
	for (const TPair<FName, int32>& Each : Cost)
	{
		if (Each.Value > 0 && GetAmount(Player, Config->FindResource(Each.Key)) < Each.Value)
		{
			return false;
		}
	}
	return Amounts.IsValidIndex(Player);
}

void UHexSites::Pay(int32 Player, const TMap<FName, int32>& Cost)
{
	for (const TPair<FName, int32>& Each : Cost)
	{
		AddAmount(Player, Each.Key, -Each.Value);
	}
}

void UHexSites::AddAmount(int32 Player, FName Resource, int32 Amount)
{
	const int32 Index = Config->FindResource(Resource);
	if (Amounts.IsValidIndex(Player) && Amounts[Player].IsValidIndex(Index))
	{
		Amounts[Player][Index] = FMath::Max(Amounts[Player][Index] + Amount, 0);
	}
}

FString UHexSites::DescribeCost(const TMap<FName, int32>& Cost) const
{
	FString Text;
	for (const FHexResourceInfo& Resource : Config->Resources)
	{
		const int32* Amount = Cost.Find(Resource.Id);
		if (Amount && *Amount > 0)
		{
			Text += FString::Printf(TEXT("%s%d %s"), Text.IsEmpty() ? TEXT("") : TEXT(", "), *Amount, *Resource.DisplayName.ToLower());
		}
	}
	return Text.IsEmpty() ? FString(TEXT("nothing")) : Text;
}

void UHexSites::AddPopup(const FHexSite& Site, const FString& Text, const FLinearColor& Color)
{
	if (Site.bSeated)
	{
		const FVector3d Foot(Site.Seat.GetLocation());
		AddPopupAt(Site.Layer, Foot + Foot.GetSafeNormal() * double(Config->PopupHeight), Text, Color);
	}
}

void UHexSites::AddPopupAt(EHexPlanetLayer Layer, const FVector3d& Point, const FString& Text, const FLinearColor& Color)
{
	if (Text.IsEmpty())
	{
		return;
	}
	FHexSitePopup& Popup = Popups.AddDefaulted_GetRef();
	Popup.Layer = Layer;
	Popup.Point = Point;
	Popup.Text = Text;
	Popup.Color = FMath::Lerp(Color, FLinearColor::White, FMath::Clamp(Config->PopupWhite, 0.0f, 1.0f));
	Popup.Color.A = 1.0f;
	// after the lines that already wait over this site
	Popup.StartTime = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Popups.Num() - 1; ++Index)
	{
		if (Popups[Index].Layer == Popup.Layer && Popups[Index].Point.Equals(Popup.Point, 1.0))
		{
			Popup.StartTime = FMath::Max(Popup.StartTime, Popups[Index].StartTime + double(Config->PopupStagger));
		}
	}
}

bool UHexSites::GetHeroPoint(EHexPlanetLayer Layer, int32 Cell, FVector3d& OutPoint) const
{
	const FHexSite* Site = FindBlockerAt(Layer, Cell);
	const AHexPlanet* Planet = Site && Site->bSeated ? GetPlanet(Layer) : nullptr;
	if (!Planet || !Planet->IsBuilt() || !Planet->GetGrid().GetCells().IsValidIndex(Cell))
	{
		return false;
	}
	// before its mesh, as that is turned, a share of the radius of its cell out; the middles of two neighbors are the root of three radii apart
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	const FVector3d Center = Cells[Cell].Center;
	const double CellRadius = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Center, Cells[Cells[Cell].Neighbors[0]].Center), -1.0, 1.0)) / FMath::Sqrt(3.0);
	const FVector2D Place = Config->Kinds[Site->Kind].HeroPlace;
	const FVector3d Forward = FVector3d(Site->Seat.GetUnitAxis(EAxis::X));
	const FVector3d Left = FVector3d(Site->Seat.GetUnitAxis(EAxis::Y));
	const FVector3d Direction = (Center + (Forward * Place.X + Left * Place.Y) * CellRadius).GetSafeNormal();
	OutPoint = Direction * Planet->FindGroundRadius(Cell, Direction);
	return true;
}

FString UHexSites::Visit(int32 Player, const FString& HeroName, EHexPlanetLayer Layer, int32 Cell, int32 Week, bool bShown)
{
	FHexSite* Site = Sites.FindByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell && !Each.bTaken; });
	if (!Site || !Amounts.IsValidIndex(Player))
	{
		return FString();
	}
	const FHexSiteKind& Kind = Config->Kinds[Site->Kind];
	const int32 Resource = GetSiteResource(*Site);
	// an artifact, a shipyard and a boat are the heroes' to handle (UHexHeroes::TakePile, VisitSite)
	if (!Config->Resources.IsValidIndex(Resource) || Kind.Type == EHexSiteType::Artifact || Kind.Type == EHexSiteType::Shipyard || Kind.Type == EHexSiteType::Boat)
	{
		return FString();
	}
	const FString ResourceName = Config->Resources[Resource].DisplayName.ToLower();
	switch (Kind.Type)
	{
	case EHexSiteType::Mine:
		if (Site->Owner == Player)
		{
			return FString::Printf(TEXT("The %s is %s's already"), *Kind.DisplayName.ToLower(), *GetPlayerName(Player));
		}
	{
		// who lost it remembers that it was its own
		const int32 Before = Site->Owner;
		if (Before >= 0 && Before < 32)
		{
			Site->FormerOwners |= 1u << Before;
		}
		if (Player < 32)
		{
			Site->FormerOwners &= ~(1u << Player);
		}
		Site->Owner = Player;
		// it flashes
		Site->EventTime = GetWorldTime();
		bDirty[int32(Layer)] = true;
		if (bShown)
		{
			AddPopup(*Site, FString::Printf(TEXT("%s: %s"), *Kind.DisplayName, *GetPlayerName(Player)), GetPlayerColor(Player));
		}
		// taken, it gives what it gives in a day at once, if nobody took that this week, as the developer asked on 2026-11-11
		FString Now;
		if (Site->VisitedWeek != Week && Kind.PerDay > 0)
		{
			Site->VisitedWeek = Week;
			Amounts[Player][Resource] += Kind.PerDay;
			Now = FString::Printf(TEXT(", and %d now"), Kind.PerDay);
			if (bShown)
			{
				AddPopup(*Site, FString::Printf(TEXT("+%d %s"), Kind.PerDay, *Config->Resources[Resource].DisplayName), Config->Resources[Resource].Color);
			}
		}
		return Amounts.IsValidIndex(Before)
			? FString::Printf(TEXT("%s takes the %s of %s for %s: %d %s a day%s"), *HeroName, *Kind.DisplayName.ToLower(), *GetPlayerName(Before), *GetPlayerName(Player), Kind.PerDay, *ResourceName, *Now)
			: FString::Printf(TEXT("%s takes the %s for %s: %d %s a day%s"), *HeroName, *Kind.DisplayName.ToLower(), *GetPlayerName(Player), Kind.PerDay, *ResourceName, *Now);
	}
	case EHexSiteType::Mill:
	{
		if (Site->VisitedWeek == Week)
		{
			return FString::Printf(TEXT("The %s has no more %s this week"), *Kind.DisplayName.ToLower(), *ResourceName);
		}
		const int32 Amount = Config->GetMineIncome(Resource) * Config->MillDays;
		Site->VisitedWeek = Week;
		Amounts[Player][Resource] += Amount;
		if (bShown)
		{
			AddPopup(*Site, FString::Printf(TEXT("+%d %s"), Amount, *Config->Resources[Resource].DisplayName), Config->Resources[Resource].Color);
		}
		return FString::Printf(TEXT("%s gets %d %s at the %s"), *HeroName, Amount, *ResourceName, *Kind.DisplayName.ToLower());
	}
	default:
	{
		const int32 Amount = GetPileAmount(*Site);
		Site->bTaken = true;
		// it is gone for the game at once, and dissolves for the eye
		Site->EventTime = GetWorldTime();
		Site->bFading = Site->bSeated && Config->PickupFadeTime > 0.0f;
		Amounts[Player][Resource] += Amount;
		bDirty[int32(Layer)] = true;
		if (bShown)
		{
			AddPopup(*Site, FString::Printf(TEXT("+%d %s"), Amount, *Config->Resources[Resource].DisplayName), Config->Resources[Resource].Color);
		}
		// the cell is free again
		UHexHeroes::NoteMapChanged();
		return FString::Printf(TEXT("%s finds %d %s"), *HeroName, Amount, *ResourceName);
	}
	}
}

void UHexSites::EndDay()
{
	for (const FHexSite& Site : Sites)
	{
		const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
		const int32 Resource = Config->FindResource(Kind.Resource);
		if (Kind.IsMine() && Amounts.IsValidIndex(Site.Owner) && Resource != INDEX_NONE)
		{
			Amounts[Site.Owner][Resource] += Kind.PerDay;
		}
	}
}

void UHexSites::Restart()
{
	++GameNumber;
	// the boats built in the last game are gone, and the others lie where the map has them
	Sites.RemoveAll([](const FHexSite& Each) { return Each.bBuilt; });
	for (FHexSite& Site : Sites)
	{
		if (Site.Cell != Site.StartCell && Site.StartCell != INDEX_NONE)
		{
			Site.Cell = Site.StartCell;
			Site.bSeated = false;
		}
		Site.Captain = 0;
		Site.Owner = INDEX_NONE;
		Site.Guard = MakeStartGuard(Site);
		Site.bTaken = false;
		Site.VisitedWeek = 0;
		Site.FormerOwners = 0;
		Site.EventTime = -1000.0;
		Site.bFading = false;
	}
	for (TArray<int32>& Each : Amounts)
	{
		Each.Reset();
		for (const FHexResourceInfo& Resource : Config->Resources)
		{
			Each.Add(Resource.StartAmount);
		}
	}
	bDirty[0] = bDirty[1] = true;
}

int32 UHexSites::GetAmount(int32 Player, int32 Resource) const
{
	return Amounts.IsValidIndex(Player) && Amounts[Player].IsValidIndex(Resource) ? Amounts[Player][Resource] : 0;
}

int32 UHexSites::GetIncome(int32 Player, int32 Resource) const
{
	int32 Income = 0;
	for (const FHexSite& Site : Sites)
	{
		const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
		Income += Kind.IsMine() && Site.Owner == Player && Config->FindResource(Kind.Resource) == Resource ? Kind.PerDay : 0;
	}
	return Income;
}

FString UHexSites::GetCellText(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexSite* Site = FindSiteAt(Layer, Cell);
	if (!Site)
	{
		return FString();
	}
	const FHexSiteKind& Kind = Config->Kinds[Site->Kind];
	if (Kind.Type == EHexSiteType::Artifact || Kind.Type == EHexSiteType::Shipyard || Kind.Type == EHexSiteType::Boat)
	{
		return FString::Printf(TEXT("Site: %s"), *Kind.DisplayName);
	}
	const int32 Resource = GetSiteResource(*Site);
	const FString ResourceName = Config->Resources.IsValidIndex(Resource) ? Config->Resources[Resource].DisplayName.ToLower() : FString(TEXT("nothing"));
	return Kind.IsMine() ? FString::Printf(TEXT("Site: %s, %d %s a day, of %s"), *Kind.DisplayName, Kind.PerDay, *ResourceName, *GetPlayerName(Site->Owner))
		: Kind.Type == EHexSiteType::Mill ? FString::Printf(TEXT("Site: %s, %d %s a week"), *Kind.DisplayName, Config->GetMineIncome(Resource) * Config->MillDays, *ResourceName)
		: FString::Printf(TEXT("Site: %s, %d to %d %s"), *Kind.DisplayName, Kind.AmountMin, Kind.AmountMax, *ResourceName);
}

FTransform UHexSites::FindSeat(const AHexPlanet& Planet, int32 Cell, double Angle, float MaxTilt, float Scale) const
{
	const TArray<FHexSphereCell>& Cells = Planet.GetGrid().GetCells();
	const FVector3d Up = Cells[Cell].Center;
	FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
	if (!East.Normalize())
	{
		East = FVector3d::UnitX();
	}
	const FVector3d North = FVector3d::CrossProduct(Up, East);

	// the ground in the middle of the cell and at six points around it, a share of the way to its sides out
	double HalfStep = 0.0;
	for (const int32 Neighbor : Cells[Cell].Neighbors)
	{
		HalfStep += 0.5 * FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Up, Cells[Neighbor].Center), -1.0, 1.0)) / Cells[Cell].Neighbors.Num();
	}
	const double Out = FMath::Tan(HalfStep * FMath::Clamp(double(Config->SeatReach), 0.05, 0.9));
	// the triangles of the cell once for all the points, as AHexPlanet::FindGroundRadius would get them for each
	TArray<FVector3f> Triangles;
	Planet.GetCellTriangles(Cell, Triangles);
	const double Top = Planet.GetCellTopRadius(Cell);
	const auto FindGround = [&Triangles, Top](const FVector3d& Direction)
	{
		for (int32 Corner = 0; Corner + 2 < Triangles.Num(); Corner += 3)
		{
			const FVector3d A(Triangles[Corner]);
			const FVector3d B(Triangles[Corner + 1]);
			const FVector3d C(Triangles[Corner + 2]);
			const FVector3d Normal = FVector3d::CrossProduct(B - A, C - A);
			const double Along = FVector3d::DotProduct(Normal, Direction);
			if (FMath::Abs(Along) < 1.0e-9)
			{
				continue;
			}
			const double Distance = FVector3d::DotProduct(Normal, A) / Along;
			const FVector3d Point = Direction * Distance;
			const auto Inside = [&Normal, &Point](const FVector3d& From, const FVector3d& To) { return FVector3d::DotProduct(FVector3d::CrossProduct(To - From, Point - From), Normal) >= -1.0e-6; };
			if (Distance > 0.0 && Inside(A, B) && Inside(B, C) && Inside(C, A))
			{
				return Distance;
			}
		}
		return Top;
	};
	const int32 NumAround = 6;
	FVector3d Points[NumAround + 1];
	Points[0] = Up * FindGround(Up);
	for (int32 Index = 0; Index < NumAround; ++Index)
	{
		const double Turn = 2.0 * UE_DOUBLE_PI * Index / NumAround;
		const FVector3d Direction = (Up + (East * FMath::Cos(Turn) + North * FMath::Sin(Turn)) * Out).GetSafeNormal();
		Points[Index + 1] = Direction * FindGround(Direction);
	}
	// it leans as that ground does, but no more than its kind may
	FVector3d Normal = FVector3d::ZeroVector;
	for (int32 Index = 0; Index < NumAround; ++Index)
	{
		Normal += FVector3d::CrossProduct(Points[1 + Index] - Points[0], Points[1 + (Index + 1) % NumAround] - Points[0]);
	}
	if (!Normal.Normalize() || FVector3d::DotProduct(Normal, Up) <= 0.0)
	{
		Normal = Up;
	}
	const double Tilt = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Normal, Up), -1.0, 1.0));
	const double Most = FMath::DegreesToRadians(double(FMath::Max(MaxTilt, 0.0f)));
	if (Tilt > Most && Tilt > UE_DOUBLE_SMALL_NUMBER)
	{
		Normal = (Up * FMath::Sin(Tilt - Most) + Normal * FMath::Sin(Most)).GetSafeNormal();
	}
	// and stands between the lowest of it and its mean, so nothing of it hangs high over the ground; its mesh goes on under its foot
	double Low = TNumericLimits<double>::Max();
	double Mean = 0.0;
	for (const FVector3d& Point : Points)
	{
		const double Height = FVector3d::DotProduct(Point, Normal);
		Low = FMath::Min(Low, Height);
		Mean += Height / (NumAround + 1);
	}
	const double Height = Low + (Mean - Low) * FMath::Clamp(double(Config->SeatLowShare), 0.0, 1.0) - double(Config->SiteSink) * Scale;
	const FVector3d Forward = -North * FMath::Cos(Angle) + East * FMath::Sin(Angle);
	return FTransform(FRotationMatrix::MakeFromZX(FVector(Normal), FVector(Forward)).ToQuat(), FVector(Up * (Height / FMath::Max(FVector3d::DotProduct(Up, Normal), 0.1))),
		FVector(Scale));
}

void UHexSites::Rebuild(EHexPlanetLayer Layer)
{
	AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet || !Planet->IsBuilt())
	{
		return;
	}
	if (!Material)
	{
		if (UMaterialInterface* Parent = Config->Material.LoadSynchronous())
		{
			// SiteGlow, PileGlow and the values of the sparkles and of what happens to a site
			Material = UMaterialInstanceDynamic::Create(Parent, this);
			HexMaterialConfig::Apply(Material, Config);
		}
		if (UMaterialInterface* Parent = Config->FadeMaterial.LoadSynchronous())
		{
			FadeMaterial = UMaterialInstanceDynamic::Create(Parent, this);
			HexMaterialConfig::Apply(FadeMaterial, Config);
		}
	}
	TMap<FString, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Components = Instances[int32(Layer)];
	const auto GetComponent = [&](const TSoftObjectPtr<UStaticMesh>& MeshAsset, bool bFade) -> UInstancedStaticMeshComponent*
	{
		UMaterialInstanceDynamic* Shown = bFade ? FadeMaterial : Material;
		TWeakObjectPtr<UInstancedStaticMeshComponent>& Slot = Components.FindOrAdd(MeshAsset.ToSoftObjectPath().ToString() + (bFade ? TEXT("#fade") : TEXT("")));
		if (!Slot.IsValid())
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
			// the colors of the resource and of the owner, and when it was taken
			Component->SetNumCustomDataFloats(7);
			Component->SetCullDistances(0, int32(Config->CullDistance));
			// small, so no place in the scene of Lumen, as the objects of the planet
			Component->bAffectDynamicIndirectLighting = Planet->GetSettings() && Planet->GetSettings()->AreSmallObjectsInLumen();
			if (Shown)
			{
				Component->SetMaterial(0, Shown);
			}
			Component->RegisterComponent();
			Slot = Component;
		}
		return Slot.Get();
	};
	// by EHexSiteType: a kind without a mesh of its own takes that of its type
	const TSoftObjectPtr<UStaticMesh> Meshes[7] = {Config->MineMesh, Config->PileMesh, Config->MillMesh, Config->PileMesh, Config->PileMesh, Config->MillMesh, Config->PileMesh};
	const float Scales[7] = {Config->MineScale, Config->PileScale, Config->MillScale, Config->PileScale, Config->PileScale, Config->MillScale, Config->BoatScale};
	for (const TPair<FString, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Each : Components)
	{
		if (Each.Value.IsValid() && Each.Value->GetInstanceCount() > 0)
		{
			Each.Value->ClearInstances();
		}
	}

	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	for (FHexSite& Site : Sites)
	{
		// a pile that was taken is drawn while it dissolves
		const bool bFade = Site.bTaken && Site.bFading && FadeMaterial;
		// a boat a hero sails is drawn under it (UHexHeroes::UpdateBoatBody)
		if (Site.Layer != Layer || (Site.bTaken && !bFade) || Site.Captain != 0 || !Cells.IsValidIndex(Site.Cell))
		{
			continue;
		}
		const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
		UInstancedStaticMeshComponent* Component = Kind.Mesh.IsNull() ? nullptr : GetComponent(Kind.Mesh, bFade);
		if (!Component)
		{
			Component = GetComponent(Meshes[FMath::Min(int32(Kind.Type), 6)], bFade);
		}
		if (!Component)
		{
			continue;
		}
		// on the ground of its cell, a mine or a mill facing the south and a pile turned by its Id; found once until the ground changes,
		// as taking a pile or a mine places all of them again
		if (!Site.bSeated && Kind.IsAtSea())
		{
			// a boat floats on the water, turned by its Id
			const FVector3d Up = Cells[Site.Cell].Center;
			FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
			East = East.Normalize() ? East : FVector3d::UnitX();
			const FVector3d North = FVector3d::CrossProduct(Up, East);
			const double Angle = FMath::DegreesToRadians(double((Site.Id * 73) % 360) + double(Site.Yaw));
			const FVector3d Forward = -North * FMath::Cos(Angle) + East * FMath::Sin(Angle);
			Site.Seat = FTransform(FRotationMatrix::MakeFromZX(FVector(Up), FVector(Forward)).ToQuat(), FVector(Up * Planet->GetRadius()), FVector(Config->BoatScale));
			Site.bSeated = true;
		}
		if (!Site.bSeated)
		{
			const bool bPile = Kind.IsPickup();
			double Angle = FMath::DegreesToRadians((bPile ? double((Site.Id * 73) % 360) : 0.0) + double(Site.Yaw));
			// a shipyard turns its slipway to the sea beside it
			if (Kind.Type == EHexSiteType::Shipyard)
			{
				const FVector3d Up = Cells[Site.Cell].Center;
				FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
				East = East.Normalize() ? East : FVector3d::UnitX();
				const FVector3d North = FVector3d::CrossProduct(Up, East);
				for (const int32 Neighbor : Cells[Site.Cell].Neighbors)
				{
					if (Layer == EHexPlanetLayer::Surface && Planet->GetCellLevel(Neighbor) < 0)
					{
						const FVector3d Towards = Cells[Neighbor].Center - Up;
						Angle = FMath::Atan2(FVector3d::DotProduct(Towards, East), -FVector3d::DotProduct(Towards, North)) + FMath::DegreesToRadians(double(Site.Yaw));
						break;
					}
				}
			}
			Site.Seat = FindSeat(*Planet, Site.Cell, Angle, bPile ? Config->PileMaxTilt : Config->BuildingMaxTilt, Scales[FMath::Min(int32(Kind.Type), 6)]);
			Site.bSeated = true;
		}
		const int32 Instance = Component->AddInstance(Site.Seat, true);
		const int32 Resource = GetSiteResource(Site);
		// an artifact glows in gold, a great one in a royal violet
		const FLinearColor ResourceColor = Kind.Type == EHexSiteType::Artifact ? (Kind.ArtifactRankMin >= EHexArtifactRank::Major ? Config->GreatArtifactColor : Config->ArtifactColor)
			: Config->Resources.IsValidIndex(Resource) ? Config->Resources[Resource].Color : FLinearColor::White;
		const FLinearColor OwnerColor = GetPlayerColor(Site.Owner);
		const float Data[7] = {ResourceColor.R, ResourceColor.G, ResourceColor.B, OwnerColor.R, OwnerColor.G, OwnerColor.B, float(Site.EventTime)};
		Component->SetCustomData(Instance, MakeArrayView(Data), true);
	}
}

void UHexSites::Update()
{
	if (!Surface || !Config)
	{
		return;
	}
	// a pile that has dissolved is drawn no more, and a line that has floated up is gone
	const double WorldTime = GetWorldTime();
	for (FHexSite& Site : Sites)
	{
		if (Site.bFading && WorldTime - Site.EventTime >= double(Config->PickupFadeTime))
		{
			Site.bFading = false;
			bDirty[int32(Site.Layer)] = true;
		}
	}
	const double RealTime = FPlatformTime::Seconds();
	Popups.RemoveAll([this, RealTime](const FHexSitePopup& Each) { return RealTime - Each.StartTime > double(Config->PopupTime); });
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		const AHexPlanet* Planet = GetPlanet(EHexPlanetLayer(Layer));
		const bool bBuilt = Planet && Planet->IsBuilt();
		if (bBuilt != bLayerBuilt[Layer])
		{
			bLayerBuilt[Layer] = bBuilt;
			OnLayerRebuilt(EHexPlanetLayer(Layer));
		}
		if (bDirty[Layer] && bBuilt)
		{
			bDirty[Layer] = false;
			Rebuild(EHexPlanetLayer(Layer));
		}
	}
}

namespace
{
	/** hex.Site: places and lists the mines and piles, for trying them from the console and from -ExecCmds */
	void RunSiteCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		AHexPlanet* Planet = Layers ? Layers->GetPresentedBuilt() : nullptr;
		UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
		UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
		if (!Sites || !Sites->GetConfig() || Args.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Site <list|add <Cell> <Kind>|remove <Cell>|generate [Seed]|clear>, on a built planet; a cell is its number or how many steps it lies from the cell of the hero chosen, as near:<Steps>[:<Side>]"));
			return;
		}
		const UHexSiteConfig* Config = Sites->GetConfig();
		const EHexPlanetLayer Layer = Planet->GetLayer();
		// a cell by its number, or some steps from the hero chosen towards a side of each cell on the way
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
			UE_LOG(LogHexHero, Log, TEXT("Sites: %d on the planet"), Sites->GetSites().Num());
			for (const FHexSite& Site : Sites->GetSites())
			{
				UE_LOG(LogHexHero, Log, TEXT("  %d %s, cell %d of the %s, owner %d%s"), Site.Id, *Config->Kinds[Site.Kind].DisplayName, Site.Cell, GetPlanetLayerName(Site.Layer),
					Site.Owner, Site.bTaken ? TEXT(", taken") : TEXT(""));
			}
			const UHexHeroConfig* HeroConfig = Heroes->GetConfig();
			for (int32 Player = 0; Player < HeroConfig->Players.Num(); ++Player)
			{
				FString Line;
				for (int32 Resource = 0; Resource < Config->Resources.Num(); ++Resource)
				{
					Line += FString::Printf(TEXT(" %s %d (+%d)"), *Config->Resources[Resource].DisplayName, Sites->GetAmount(Player, Resource), Sites->GetIncome(Player, Resource));
				}
				UE_LOG(LogHexHero, Log, TEXT("  %s:%s"), *HeroConfig->Players[Player].Name, *Line);
			}
		}
		else if (Args[0].Equals(TEXT("add"), ESearchCase::IgnoreCase) && Args.Num() >= 3)
		{
			FString Problem;
			const int32 Kind = Config->FindKind(Args[2]);
			if (Sites->AddSite(Layer, ParseCell(Args[1]), Kind, Problem) == 0)
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Site: no %s on cell %s: %s"), *Args[2], *Args[1], *Problem);
			}
		}
		else if (Args[0].Equals(TEXT("generate"), ESearchCase::IgnoreCase))
		{
			// as Generate of the Mines tab, but no step of Undo
			FString Report;
			const int32 Seed = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0;
			Sites->Generate(Report, Args.Num() >= 2 ? &Seed : nullptr);
		}
		else if (Args[0].Equals(TEXT("scatter"), ESearchCase::IgnoreCase))
		{
			// more piles and chests besides the sites there are, as the developer found the map too empty on 2026-11-15: a share of the
			// numbers of the generation, half by default
			FString Report;
			const int32 Seed = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0;
			const float Density = Args.Num() >= 3 ? FCString::Atof(*Args[2]) : 0.5f;
			const int32 Before = Sites->GetSites().Num();
			Sites->Generate(Report, Args.Num() >= 2 ? &Seed : nullptr, Density, nullptr, true);
			UE_LOG(LogHexHero, Log, TEXT("Sites: scattered %d piles and chests more, %d in all"), Sites->GetSites().Num() - Before, Sites->GetSites().Num());
		}
		else if (Args[0].Equals(TEXT("artifacts"), ESearchCase::IgnoreCase))
		{
			// artifacts besides the sites there are, as many as the generation gives them by default
			FString Report;
			const int32 Seed = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0;
			const float Density = Args.Num() >= 3 ? FCString::Atof(*Args[2]) : 1.0f;
			const int32 Before = Sites->GetSites().Num();
			Sites->Generate(Report, Args.Num() >= 2 ? &Seed : nullptr, Density, nullptr, true, int32(EHexSiteType::Artifact));
			UE_LOG(LogHexHero, Log, TEXT("Sites: %d artifacts more, %d sites in all"), Sites->GetSites().Num() - Before, Sites->GetSites().Num());
		}
		else if (Args[0].Equals(TEXT("vaults"), ESearchCase::IgnoreCase))
		{
			// closed places with one guarded way in and much in them, as the developer asked on 2026-11-15
			FString Report;
			Sites->MakeVaults(Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 4, Args.Num() >= 3 ? FCString::Atoi(*Args[2]) : int32(FPlatformTime::Cycles() & 0xffff), Report);
		}
		else if (Args[0].Equals(TEXT("coast"), ESearchCase::IgnoreCase))
		{
			// shipyards and boats on the coasts besides what there is, for a map made before them
			const int32 Seed = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0;
			const float Density = Args.Num() >= 3 ? FCString::Atof(*Args[2]) : 1.0f;
			const FIntPoint Placed = Sites->ScatterCoast(Seed, Density);
			UE_LOG(LogHexHero, Log, TEXT("Sites: %d shipyards and %d boats more, %d sites in all"), Placed.X, Placed.Y, Sites->GetSites().Num());
		}
		else if (Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogHexHero, Log, TEXT("Sites: %s"), Sites->Clear() ? TEXT("every site is taken off the planet") : TEXT("there is no site to take off"));
		}
		else if (Args[0].Equals(TEXT("remove"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			if (!Sites->RemoveSiteAt(Layer, ParseCell(Args[1])))
			{
				UE_LOG(LogHexHero, Warning, TEXT("hex.Site: no site on cell %s"), *Args[1]);
			}
		}
		else
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Site: unknown action %s"), *Args[0]);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs SiteCommand(TEXT("hex.Site"),
		TEXT("The mines and piles of the planet: list (with what each player has and gets a day), add <Cell> <Kind> on the layer shown, remove <Cell>, generate [Seed] (scatters them over both layers in place of all there are, by Generation of DA_HexSites), scatter [Seed] [Density] (more piles, chests and artifacts besides those there are, half the generation by default), artifacts [Seed] [Density] (artifacts besides), coast [Seed] [Density] (shipyards and boats on the coasts besides), clear. A cell is its number, or near:<Steps>[:<Side>] for one that many steps from the hero chosen; a kind is its Id in DA_HexSites, such as GoldMine, Sawmill or OrePile."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSiteCommand));
}
