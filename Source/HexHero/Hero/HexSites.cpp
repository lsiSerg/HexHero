// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexSites.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexHeroes.h"
#include "HexMaterialConfig.h"
#include "HexPlanet.h"
#include "HexPlanetObjects.h"
#include "HexShroud.h"
#include "HexSiteConfig.h"
#include "HexSiteGenerator.h"
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

const FHexSite* UHexSites::FindBlockerAt(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexSite* Site = FindSiteAt(Layer, Cell);
	return Site && Config->Kinds[Site->Kind].FillsCell() ? Site : nullptr;
}

const FHexSite* UHexSites::FindPileAt(EHexPlanetLayer Layer, int32 Cell) const
{
	const FHexSite* Site = FindSiteAt(Layer, Cell);
	return Site && !Config->Kinds[Site->Kind].FillsCell() ? Site : nullptr;
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

bool UHexSites::Clear()
{
	if (Sites.IsEmpty())
	{
		return false;
	}
	SetLines({});
	return true;
}

int32 UHexSites::Generate(FString& OutReport, const int32* Seed)
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
	Input.Seed = Seed ? *Seed : Surface->GetManifest().Seed * 7919 + GenerateCount++ * 104729;
	for (const FHexSiteShare& Share : Settings.Shares)
	{
		const int32 Kind = Config->FindKind(Share.Kind.ToString());
		if (Kind == INDEX_NONE)
		{
			UE_LOG(LogHexHero, Warning, TEXT("Sites: the generation names the kind %s, which DA_HexSites does not have"), *Share.Kind.ToString());
			continue;
		}
		HexSiteGenerator::FKind& Each = Input.Kinds.AddDefaulted_GetRef();
		Each.Kind = Kind;
		Each.Type = Config->Kinds[Kind].Type;
		Each.PerThousand[int32(EHexPlanetLayer::Surface)] = Share.PerThousand;
		Each.PerThousand[int32(EHexPlanetLayer::Underground)] = Share.UndergroundPerThousand;
		Each.bNearStart = Share.bNearStart;
	}

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

	TArray<HexSiteGenerator::FSite> Found;
	HexSiteGenerator::FReport Report;
	HexSiteGenerator::Generate(Grid, Input, Found, &Report);

	TArray<FHexSiteLine> Lines;
	int32 Counts[NumPlanetLayers][3] = {};
	for (const HexSiteGenerator::FSite& Each : Found)
	{
		FHexSiteLine& Line = Lines.AddDefaulted_GetRef();
		Line.Id = Lines.Num();
		Line.Kind = Config->Kinds[Each.Kind].Id.ToString();
		Line.Layer = GetPlanetLayerName(EHexPlanetLayer(Each.Layer));
		Line.Cell = Each.Cell;
		++Counts[Each.Layer][int32(Config->Kinds[Each.Kind].Type)];
	}
	// every site anew, as an old one whose line came out the same would keep its owner
	Sites.Reset();
	NextId = 1;
	SetLines(Lines);
	Restart();

	const int32 Mine = int32(EHexSiteType::Mine);
	const int32 Pile = int32(EHexSiteType::Pile);
	const int32 Mill = int32(EHexSiteType::Mill);
	OutReport = FString::Printf(TEXT("%d sites: on the surface %d mines, %d piles and %d mills on %d cells, underground %d mines and %d piles on %d cells; %d near where %d players start%s"),
		Sites.Num(), Counts[0][Mine], Counts[0][Pile], Counts[0][Mill], Report.Cells[0], Counts[1][Mine], Counts[1][Pile] + Counts[1][Mill], Report.Cells[1],
		Report.NearStarts, Input.Starts.Num(), Report.NoRoom > 0 ? *FString::Printf(TEXT("; %d found no room"), Report.NoRoom) : TEXT(""));
	UE_LOG(LogHexHero, Log, TEXT("Sites: generated with the seed %d in %.1f ms, %s"), Input.Seed, (FPlatformTime::Seconds() - StartTime) * 1000.0, *OutReport);
	return Sites.Num();
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
	default:
		return EHexSiteGoal::Gather;
	}
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

void UHexSites::AddPopup(const FHexSite& Site, const FString& Text, const FLinearColor& Color)
{
	if (!Site.bSeated || Text.IsEmpty())
	{
		return;
	}
	FHexSitePopup& Popup = Popups.AddDefaulted_GetRef();
	Popup.Layer = Site.Layer;
	const FVector3d Foot(Site.Seat.GetLocation());
	Popup.Point = Foot + Foot.GetSafeNormal() * double(Config->PopupHeight);
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

FString UHexSites::Visit(int32 Player, const FString& HeroName, EHexPlanetLayer Layer, int32 Cell, int32 Week, bool bShown)
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
	for (FHexSite& Site : Sites)
	{
		Site.Owner = INDEX_NONE;
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
	const TSoftObjectPtr<UStaticMesh> Meshes[3] = {Config->MineMesh, Config->PileMesh, Config->MillMesh};
	const float Scales[3] = {Config->MineScale, Config->PileScale, Config->MillScale};
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
		if (Site.Layer != Layer || (Site.bTaken && !bFade) || !Cells.IsValidIndex(Site.Cell))
		{
			continue;
		}
		const FHexSiteKind& Kind = Config->Kinds[Site.Kind];
		UInstancedStaticMeshComponent* Component = Kind.Mesh.IsNull() ? nullptr : GetComponent(Kind.Mesh, bFade);
		if (!Component)
		{
			Component = GetComponent(Meshes[int32(Kind.Type)], bFade);
		}
		if (!Component)
		{
			continue;
		}
		// on the ground of its cell, a mine or a mill facing the south and a pile turned by its Id; found once until the ground changes,
		// as taking a pile or a mine places all of them again
		if (!Site.bSeated)
		{
			const bool bPile = Kind.Type == EHexSiteType::Pile;
			const double Angle = bPile ? FMath::DegreesToRadians(double((Site.Id * 73) % 360)) : 0.0;
			Site.Seat = FindSeat(*Planet, Site.Cell, Angle, bPile ? Config->PileMaxTilt : Config->BuildingMaxTilt, Scales[int32(Kind.Type)]);
			Site.bSeated = true;
		}
		const int32 Instance = Component->AddInstance(Site.Seat, true);
		const int32 Resource = GetSiteResource(Site);
		const FLinearColor ResourceColor = Config->Resources.IsValidIndex(Resource) ? Config->Resources[Resource].Color : FLinearColor::White;
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
		TEXT("The mines and piles of the planet: list (with what each player has and gets a day), add <Cell> <Kind> on the layer shown, remove <Cell>, generate [Seed] (scatters them over both layers in place of all there are, by Generation of DA_HexSites), clear. A cell is its number, or near:<Steps>[:<Side>] for one that many steps from the hero chosen; a kind is its Id in DA_HexSites, such as GoldMine, Sawmill or OrePile."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSiteCommand));
}
