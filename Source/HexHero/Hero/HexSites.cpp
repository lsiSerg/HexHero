// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexSites.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexPlanet.h"
#include "HexSiteConfig.h"
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
		FHexSiteLine& Line = Lines.AddDefaulted_GetRef();
		Line.Id = Site.Id;
		Line.Kind = Config->Kinds[Site.Kind].Id.ToString();
		Line.Layer = GetPlanetLayerName(Site.Layer);
		Line.Cell = Site.Cell;
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
		if (Same != INDEX_NONE)
		{
			Kept.Add(Sites[Same]);
			continue;
		}
		FHexSite Site;
		Site.Id = Line.Id;
		Site.Kind = Config->FindKind(Line.Kind);
		Site.Cell = Line.Cell;
		if (Site.Id <= 0 || Site.Kind == INDEX_NONE || Line.Cell < 0 || Line.Cell >= NumCells || !ParsePlanetLayer(Line.Layer, Site.Layer)
			|| Kept.ContainsByPredicate([&Site](const FHexSite& Each) { return Each.Id == Site.Id || (Each.Layer == Site.Layer && Each.Cell == Site.Cell); }))
		{
			UE_LOG(LogHexHero, Error, TEXT("Sites: the site %d (%s) of sites.json has no Id, kind, cell or layer of its own, and is left out"), Line.Id, *Line.Kind);
			continue;
		}
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
	return Sites.FindByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell && !Each.bTaken; });
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
	// where a hero could stand, and none does, as a site fills its cell
	if (Heroes && !Heroes->CanStand(Layer, Cell, 0, &OutProblem))
	{
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

int32 UHexSites::GetPileAmount(const FHexSite& Site) const
{
	const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
	// by the site and the game, so a pile holds the same until a new game
	FRandomStream Stream(Site.Id * 7919 + GameNumber * 104729);
	return Stream.RandRange(FMath::Min(Kind.AmountMin, Kind.AmountMax), FMath::Max(Kind.AmountMin, Kind.AmountMax));
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

FString UHexSites::Visit(int32 Player, const FString& HeroName, EHexPlanetLayer Layer, int32 Cell, int32 Week)
{
	FHexSite* Site = Sites.FindByPredicate([Layer, Cell](const FHexSite& Each) { return Each.Layer == Layer && Each.Cell == Cell && !Each.bTaken; });
	if (!Site || !Amounts.IsValidIndex(Player))
	{
		return FString();
	}
	const FHexSiteKind& Kind = Config->Kinds[Site->Kind];
	const int32 Resource = GetSiteResource(*Site);
	if (!Config->Resources.IsValidIndex(Resource))
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
		Site->Owner = Player;
		bDirty[int32(Layer)] = true;
		return FString::Printf(TEXT("%s takes the %s for %s: %d %s a day"), *HeroName, *Kind.DisplayName.ToLower(), *GetPlayerName(Player), Kind.PerDay, *ResourceName);
	case EHexSiteType::Mill:
	{
		if (Site->VisitedWeek == Week)
		{
			return FString::Printf(TEXT("The %s has no more %s this week"), *Kind.DisplayName.ToLower(), *ResourceName);
		}
		const int32 Amount = Config->GetMineIncome(Resource) * Config->MillDays;
		Site->VisitedWeek = Week;
		Amounts[Player][Resource] += Amount;
		return FString::Printf(TEXT("%s gets %d %s at the %s"), *HeroName, Amount, *ResourceName, *Kind.DisplayName.ToLower());
	}
	default:
	{
		const int32 Amount = GetPileAmount(*Site);
		Site->bTaken = true;
		Amounts[Player][Resource] += Amount;
		bDirty[int32(Layer)] = true;
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
	for (FHexSite& Site : Sites)
	{
		Site.Owner = INDEX_NONE;
		Site.bTaken = false;
		Site.VisitedWeek = 0;
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
	const int32 Resource = GetSiteResource(*Site);
	const FString ResourceName = Config->Resources.IsValidIndex(Resource) ? Config->Resources[Resource].DisplayName.ToLower() : FString(TEXT("nothing"));
	return Kind.IsMine() ? FString::Printf(TEXT("Site: %s, %d %s a day, of %s"), *Kind.DisplayName, Kind.PerDay, *ResourceName, *GetPlayerName(Site->Owner))
		: Kind.Type == EHexSiteType::Mill ? FString::Printf(TEXT("Site: %s, %d %s a week"), *Kind.DisplayName, Config->GetMineIncome(Resource) * Config->MillDays, *ResourceName)
		: FString::Printf(TEXT("Site: %s, %d to %d %s"), *Kind.DisplayName, Kind.AmountMin, Kind.AmountMax, *ResourceName);
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
			Material = UMaterialInstanceDynamic::Create(Parent, this);
			Material->SetScalarParameterValue(TEXT("SiteGlow"), Config->SiteGlow);
		}
	}
	const auto GetComponent = [&](TWeakObjectPtr<UInstancedStaticMeshComponent>& Slot, const TSoftObjectPtr<UStaticMesh>& MeshAsset) -> UInstancedStaticMeshComponent*
	{
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
			// the colors of the resource and of the owner
			Component->SetNumCustomDataFloats(6);
			Component->SetCullDistances(0, int32(Config->CullDistance));
			if (Material)
			{
				Component->SetMaterial(0, Material);
			}
			Component->RegisterComponent();
			Slot = Component;
		}
		return Slot.Get();
	};
	const TSoftObjectPtr<UStaticMesh> Meshes[3] = {Config->MineMesh, Config->PileMesh, Config->MillMesh};
	const float Scales[3] = {Config->MineScale, Config->PileScale, Config->MillScale};
	UInstancedStaticMeshComponent* Components[3];
	for (int32 Type = 0; Type < 3; ++Type)
	{
		Components[Type] = GetComponent(Instances[int32(Layer)][Type], Meshes[Type]);
		if (Components[Type] && Components[Type]->GetInstanceCount() > 0)
		{
			Components[Type]->ClearInstances();
		}
	}

	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	for (const FHexSite& Site : Sites)
	{
		if (Site.Layer != Layer || Site.bTaken || !Cells.IsValidIndex(Site.Cell))
		{
			continue;
		}
		const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
		UInstancedStaticMeshComponent* Component = Components[int32(Kind.Type)];
		if (!Component)
		{
			continue;
		}
		// on the ground in the middle of its cell, a mine or a mill facing the south and a pile turned by its Id
		const FVector3d Up = Cells[Site.Cell].Center;
		FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
		if (!East.Normalize())
		{
			East = FVector3d::UnitX();
		}
		const FVector3d North = FVector3d::CrossProduct(Up, East);
		const double Angle = Kind.Type != EHexSiteType::Pile ? 0.0 : FMath::DegreesToRadians(double((Site.Id * 73) % 360));
		const FVector3d Forward = -North * FMath::Cos(Angle) + East * FMath::Sin(Angle);
		const FTransform Transform(FRotationMatrix::MakeFromZX(FVector(Up), FVector(Forward)).ToQuat(), FVector(Up * Planet->FindGroundRadius(Site.Cell, Up)),
			FVector(Scales[int32(Kind.Type)]));
		const int32 Instance = Component->AddInstance(Transform, true);
		const int32 Resource = GetSiteResource(Site);
		const FLinearColor ResourceColor = Config->Resources.IsValidIndex(Resource) ? Config->Resources[Resource].Color : FLinearColor::White;
		const FLinearColor OwnerColor = GetPlayerColor(Site.Owner);
		const float Data[6] = {ResourceColor.R, ResourceColor.G, ResourceColor.B, OwnerColor.R, OwnerColor.G, OwnerColor.B};
		Component->SetCustomData(Instance, MakeArrayView(Data), true);
	}
}

void UHexSites::Update()
{
	if (!Surface || !Config)
	{
		return;
	}
	for (int32 Layer = 0; Layer < NumPlanetLayers; ++Layer)
	{
		const AHexPlanet* Planet = GetPlanet(EHexPlanetLayer(Layer));
		const bool bBuilt = Planet && Planet->IsBuilt();
		if (bBuilt != bLayerBuilt[Layer])
		{
			bLayerBuilt[Layer] = bBuilt;
			bDirty[Layer] = true;
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
			UE_LOG(LogHexHero, Warning, TEXT("hex.Site <list|add <Cell> <Kind>|remove <Cell>>, on a built planet; a cell is its number or how many steps it lies from the cell of the hero chosen, as near:<Steps>[:<Side>]"));
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
		TEXT("The mines and piles of the planet: list (with what each player has and gets a day), add <Cell> <Kind> on the layer shown, remove <Cell>. A cell is its number, or near:<Steps>[:<Side>] for one that many steps from the hero chosen; a kind is its Id in DA_HexSites, such as GoldMine, Sawmill or OrePile."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSiteCommand));
}
