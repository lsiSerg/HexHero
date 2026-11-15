// Copyright Epic Games, Inc. All Rights Reserved.

// The artifacts heroes wear and the boats they sail, as the developer asked on 2026-11-15 ("додай артефакти. додай човники")

#include "HexHeroes.h"
#include "HexCastles.h"
#include "HexHero.h"
#include "HexHeroConfig.h"
#include "HexNeutrals.h"
#include "HexMaterialConfig.h"
#include "HexPlanet.h"
#include "HexSites.h"
#include "HexSiteConfig.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

const FHexArtifact* UHexHeroes::GetArtifact(int32 Index) const
{
	return Config && Config->Artifacts.IsValidIndex(Index) ? &Config->Artifacts[Index] : nullptr;
}

FHexArtifactBonus UHexHeroes::GetArtifactBonus(const FHexHero& Hero) const
{
	FHexArtifactBonus Bonus;
	for (const int32 Each : Hero.Worn)
	{
		if (const FHexArtifact* Artifact = GetArtifact(Each))
		{
			Bonus.Add(*Artifact);
		}
	}
	return Bonus;
}

void UHexHeroes::Wear(FHexHero& Hero, int32 Artifact, int32 Sign) const
{
	// its movement counts from the next day (GetMaxMovePoints), so passing boots from hand to hand gives no steps today
	if (const FHexArtifact* Each = GetArtifact(Artifact))
	{
		Hero.Attack += Sign * Each->Attack;
		Hero.Defense += Sign * Each->Defense;
		Hero.SpellPower += Sign * Each->SpellPower;
		Hero.Knowledge += Sign * Each->Knowledge;
	}
}

bool UHexHeroes::GiveArtifact(FHexHero& Hero, int32 Artifact)
{
	const FHexArtifact* Each = GetArtifact(Artifact);
	if (!Each)
	{
		return false;
	}
	if (Hero.Worn.Num() != NumWornSlots)
	{
		Hero.Worn.Init(INDEX_NONE, NumWornSlots);
	}
	// in a free slot of its kind, else in the backpack
	for (int32 Slot = 0; Slot < NumWornSlots; ++Slot)
	{
		if (Hero.Worn[Slot] == INDEX_NONE && GetWornSlotKind(Slot) == Each->Slot)
		{
			Hero.Worn[Slot] = Artifact;
			Wear(Hero, Artifact, 1);
			++ListRevision;
			return true;
		}
	}
	if (Hero.Backpack.Num() >= Config->BackpackSize)
	{
		return false;
	}
	Hero.Backpack.Add(Artifact);
	++ListRevision;
	return true;
}

bool UHexHeroes::GiveArtifact(int32 HeroId, int32 Artifact)
{
	FHexHero* Hero = FindHeroMutable(HeroId);
	return Hero && GiveArtifact(*Hero, Artifact);
}

bool UHexHeroes::WearArtifact(int32 HeroId, int32 BackpackIndex, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(HeroId);
	if (!Hero || !Hero->Backpack.IsValidIndex(BackpackIndex))
	{
		return false;
	}
	if (Hero->Worn.Num() != NumWornSlots)
	{
		Hero->Worn.Init(INDEX_NONE, NumWornSlots);
	}
	const int32 Artifact = Hero->Backpack[BackpackIndex];
	const FHexArtifact* Each = GetArtifact(Artifact);
	if (!Each)
	{
		return false;
	}
	// a free slot of its kind, else the first of its kind, whose artifact goes into the backpack in its place
	int32 Free = INDEX_NONE;
	int32 First = INDEX_NONE;
	for (int32 Slot = 0; Slot < NumWornSlots; ++Slot)
	{
		if (GetWornSlotKind(Slot) == Each->Slot)
		{
			First = First == INDEX_NONE ? Slot : First;
			if (Hero->Worn[Slot] == INDEX_NONE && Free == INDEX_NONE)
			{
				Free = Slot;
			}
		}
	}
	const int32 Slot = Free != INDEX_NONE ? Free : First;
	if (Slot == INDEX_NONE)
	{
		return false;
	}
	const int32 Was = Hero->Worn[Slot];
	if (Was != INDEX_NONE)
	{
		Wear(*Hero, Was, -1);
		Hero->Backpack[BackpackIndex] = Was;
	}
	else
	{
		Hero->Backpack.RemoveAt(BackpackIndex);
	}
	Hero->Worn[Slot] = Artifact;
	Wear(*Hero, Artifact, 1);
	OutMessage = FString::Printf(TEXT("%s puts on the %s"), *Hero->Name, *Each->DisplayName.ToLower());
	++ListRevision;
	return true;
}

bool UHexHeroes::TakeOffArtifact(int32 HeroId, int32 WornSlot, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(HeroId);
	if (!Hero || !Hero->Worn.IsValidIndex(WornSlot) || Hero->Worn[WornSlot] == INDEX_NONE)
	{
		return false;
	}
	if (Hero->Backpack.Num() >= Config->BackpackSize)
	{
		OutMessage = FString::Printf(TEXT("The backpack of %s is full"), *Hero->Name);
		return false;
	}
	const int32 Artifact = Hero->Worn[WornSlot];
	Wear(*Hero, Artifact, -1);
	Hero->Worn[WornSlot] = INDEX_NONE;
	Hero->Backpack.Add(Artifact);
	OutMessage = FString::Printf(TEXT("%s takes off the %s"), *Hero->Name, *GetArtifact(Artifact)->DisplayName.ToLower());
	++ListRevision;
	return true;
}

bool UHexHeroes::PassArtifact(int32 FromHero, bool bWorn, int32 Index, int32 ToHero, FString& OutMessage)
{
	FHexHero* From = FindHeroMutable(FromHero);
	FHexHero* To = FindHeroMutable(ToHero);
	const AHexPlanet* Planet = From ? GetPlanet(From->Layer) : nullptr;
	if (!From || !To || From == To || !Planet || From->Player != To->Player || From->Layer != To->Layer)
	{
		return false;
	}
	// beside each other, as for an exchange of armies
	if (From->Cell != To->Cell && !Planet->GetGrid().GetCells()[From->Cell].Neighbors.Contains(To->Cell))
	{
		OutMessage = FString::Printf(TEXT("%s and %s stand apart"), *From->Name, *To->Name);
		return false;
	}
	const TArray<int32>& Source = bWorn ? From->Worn : From->Backpack;
	if (!Source.IsValidIndex(Index) || Source[Index] == INDEX_NONE)
	{
		return false;
	}
	const int32 Artifact = Source[Index];
	if (!GiveArtifact(*To, Artifact))
	{
		OutMessage = FString::Printf(TEXT("The backpack of %s is full"), *To->Name);
		return false;
	}
	if (bWorn)
	{
		Wear(*From, Artifact, -1);
		From->Worn[Index] = INDEX_NONE;
	}
	else
	{
		From->Backpack.RemoveAt(Index);
	}
	OutMessage = FString::Printf(TEXT("%s gives %s the %s"), *From->Name, *To->Name, *GetArtifact(Artifact)->DisplayName.ToLower());
	++ListRevision;
	return true;
}

int32 UHexHeroes::TakeArtifacts(int32 WinnerId, int32 LoserId)
{
	FHexHero* Winner = FindHeroMutable(WinnerId);
	FHexHero* Loser = FindHeroMutable(LoserId);
	if (!Winner || !Loser || Winner == Loser || !Config->bWinnerTakesArtifacts)
	{
		return 0;
	}
	TArray<int32> All;
	for (int32 Slot = 0; Slot < Loser->Worn.Num(); ++Slot)
	{
		if (Loser->Worn[Slot] != INDEX_NONE)
		{
			Wear(*Loser, Loser->Worn[Slot], -1);
			All.Add(Loser->Worn[Slot]);
			Loser->Worn[Slot] = INDEX_NONE;
		}
	}
	All.Append(Loser->Backpack);
	Loser->Backpack.Reset();
	int32 Taken = 0;
	for (const int32 Artifact : All)
	{
		Taken += GiveArtifact(*Winner, Artifact) ? 1 : 0;
	}
	if (Taken > 0)
	{
		UE_LOG(LogHexHero, Log, TEXT("Heroes: %s takes %d artifacts from %s"), *Winner->Name, Taken, *Loser->Name);
	}
	return Taken;
}

int32 UHexHeroes::PickArtifact(uint32 Hash, EHexArtifactRank Low, EHexArtifactRank High) const
{
	if (!Config || Config->Artifacts.IsEmpty())
	{
		return INDEX_NONE;
	}
	// the relics are a third as common as the others
	TArray<int32> Candidates;
	TArray<int32> Weights;
	int32 Total = 0;
	for (int32 Index = 0; Index < Config->Artifacts.Num(); ++Index)
	{
		const EHexArtifactRank Rank = Config->Artifacts[Index].Rank;
		if (Rank >= FMath::Min(Low, High) && Rank <= FMath::Max(Low, High))
		{
			const int32 Weight = Rank == EHexArtifactRank::Relic ? 1 : 3;
			Candidates.Add(Index);
			Weights.Add(Weight);
			Total += Weight;
		}
	}
	if (Candidates.IsEmpty())
	{
		return int32(Hash % uint32(Config->Artifacts.Num()));
	}
	int32 Pick = int32(Hash % uint32(Total));
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Pick -= Weights[Index];
		if (Pick < 0)
		{
			return Candidates[Index];
		}
	}
	return Candidates.Last();
}

int32 UHexHeroes::GetSiteArtifact(const FHexSite& Site) const
{
	const UHexSiteConfig* SiteConfig = Sites ? Sites->GetConfig() : nullptr;
	if (!SiteConfig || !SiteConfig->Kinds.IsValidIndex(Site.Kind))
	{
		return INDEX_NONE;
	}
	const FHexSiteKind& Kind = SiteConfig->Kinds[Site.Kind];
	const uint32 Hash = Sites->GetSiteHash(Site);
	if (Kind.Type == EHexSiteType::Artifact)
	{
		return PickArtifact(Hash, Kind.ArtifactRankMin, Kind.ArtifactRankMax);
	}
	// a chest holds an artifact of the lowest ranks in place of its gold now and then, a wreck more often
	if (Kind.Type == EHexSiteType::Chest)
	{
		FRandomStream Stream{int32(Hash)};
		if (Stream.FRand() < (Kind.ArtifactShare >= 0.0f ? Kind.ArtifactShare : SiteConfig->ChestArtifactShare))
		{
			return PickArtifact(uint32(Stream.GetUnsignedInt()), EHexArtifactRank::Treasure, EHexArtifactRank::Minor);
		}
	}
	return INDEX_NONE;
}

bool UHexHeroes::IsSea(EHexPlanetLayer Layer, int32 Cell) const
{
	const AHexPlanet* Planet = Layer == EHexPlanetLayer::Surface ? GetPlanet(Layer) : nullptr;
	return Planet && Planet->GetGrid().GetCells().IsValidIndex(Cell) && Planet->GetCellLevel(Cell) < 0;
}

bool UHexHeroes::GetSeaStep(const FHexHero* Hero, const AHexPlanet& Planet, int32 From, int32 To, float& OutCost) const
{
	if (Planet.GetLayer() != EHexPlanetLayer::Surface || !Config)
	{
		return false;
	}
	const bool bFromSea = Planet.GetCellLevel(From) < 0;
	const bool bToSea = Planet.GetCellLevel(To) < 0;
	const bool bSailing = Hero && Hero->BoatId != 0;
	if (!bFromSea && !bToSea && !bSailing)
	{
		return false;
	}
	const float Step = FMath::Max(Config->SeaStepCost, 0.01f);
	if (bSailing)
	{
		// ashore its way ends; at sea a boat that lies there is in the way
		if (!bFromSea)
		{
			OutCost = -1.0f;
		}
		else if (bToSea)
		{
			// a boat that lies there, a stack of the sea or a hero in its boat are in the way; a wreck is taken there
			OutCost = (Sites && Sites->FindBoatAt(Planet.GetLayer(), To)) || (Neutrals && Neutrals->FindAt(Planet.GetLayer(), To))
				|| FindHeroAt(Planet.GetLayer(), To) ? -1.0f : Step;
		}
		else
		{
			OutCost = GetCellCost(Planet, To) >= 0.0f ? Step : -1.0f;
		}
		return true;
	}
	// on land: aboard a boat nobody sails its way ends there, for a hero a person plays
	OutCost = !bFromSea && Hero && !IsPlayerAi(Hero->Player) && Sites && Sites->FindBoatAt(Planet.GetLayer(), To) ? Step : -1.0f;
	return true;
}

void UHexHeroes::Board(FHexHero& Hero, int32 BoatCell)
{
	const FHexSite* Boat = Sites ? Sites->FindBoatAt(Hero.Layer, BoatCell) : nullptr;
	if (!Boat)
	{
		return;
	}
	Hero.BoatId = Boat->Id;
	Sites->SetCaptain(Boat->Id, Hero.Id);
	// going aboard takes what is left of the day, as in Heroes III
	Hero.MovePoints = 0.0f;
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s goes aboard the boat %d on cell %d"), *Hero.Name, Hero.BoatId, BoatCell);
	if (IsCellSeen(Hero.Layer, BoatCell) && !IsPlayerAi(Hero.Player))
	{
		OnMessage.Broadcast(FString::Printf(TEXT("%s goes aboard a boat"), *Hero.Name));
	}
}

void UHexHeroes::GoAshore(FHexHero& Hero)
{
	if (Hero.BoatId == 0)
	{
		return;
	}
	// the boat lies on the cell of the sea it leaves, and going ashore takes what is left of the day
	if (Sites)
	{
		Sites->LeaveBoat(Hero.BoatId, Hero.Cell);
	}
	UE_LOG(LogHexHero, Log, TEXT("Heroes: %s goes ashore from cell %d"), *Hero.Name, Hero.Cell);
	Hero.BoatId = 0;
	Hero.MovePoints = 0.0f;
	if (TObjectPtr<UInstancedStaticMeshComponent>* Body = BoatBodies.Find(Hero.Id); Body && *Body)
	{
		(*Body)->SetVisibility(false);
	}
}

int32 UHexHeroes::FindBoatCell(EHexPlanetLayer Layer, int32 ShipyardCell) const
{
	const AHexPlanet* Planet = GetPlanet(Layer);
	if (!Planet || !Planet->GetGrid().GetCells().IsValidIndex(ShipyardCell))
	{
		return INDEX_NONE;
	}
	for (const int32 Neighbor : Planet->GetGrid().GetCells()[ShipyardCell].Neighbors)
	{
		if (IsSea(Layer, Neighbor) && !(Sites && Sites->FindSiteAt(Layer, Neighbor)) && !FindHeroAt(Layer, Neighbor))
		{
			return Neighbor;
		}
	}
	return INDEX_NONE;
}

int32 UHexHeroes::FindFreeSea(const FHexHero& Hero, int32 MaxRings) const
{
	const AHexPlanet* Planet = GetPlanet(Hero.Layer);
	if (!Planet || !Sites || !Planet->GetGrid().GetCells().IsValidIndex(Hero.Cell))
	{
		return INDEX_NONE;
	}
	// ring by ring, the cells beside it first
	const TArray<FHexSphereCell>& Cells = Planet->GetGrid().GetCells();
	TSet<int32> Seen = {Hero.Cell};
	TArray<int32> Ring = {Hero.Cell};
	for (int32 Step = 0; Step < MaxRings && !Ring.IsEmpty(); ++Step)
	{
		TArray<int32> Next;
		for (const int32 Cell : Ring)
		{
			for (const int32 Neighbor : Cells[Cell].Neighbors)
			{
				if (Seen.Contains(Neighbor))
				{
					continue;
				}
				Seen.Add(Neighbor);
				if (IsSea(Hero.Layer, Neighbor) && !Sites->FindSiteAt(Hero.Layer, Neighbor) && !FindHeroAt(Hero.Layer, Neighbor))
				{
					return Neighbor;
				}
				Next.Add(Neighbor);
			}
		}
		Ring = MoveTemp(Next);
	}
	return INDEX_NONE;
}

bool UHexHeroes::SummonBoat(int32 HeroId, FString& OutMessage, int32 MaxRings)
{
	const FHexHero* Hero = FindHero(HeroId);
	const AHexPlanet* Planet = Hero ? GetPlanet(Hero->Layer) : nullptr;
	if (!Hero || !Planet || !Sites || !Planet->GetGrid().GetCells().IsValidIndex(Hero->Cell))
	{
		OutMessage = TEXT("No such hero, and none is chosen");
		return false;
	}
	if (Hero->BoatId != 0)
	{
		OutMessage = FString::Printf(TEXT("%s is in a boat already"), *Hero->Name);
		return false;
	}
	if (const int32 Sea = FindFreeSea(*Hero, MaxRings); Sea != INDEX_NONE)
	{
		const FString Name = Hero->Name;
		Sites->AddBuiltBoat(Hero->Layer, Sea);
		OutMessage = FString::Printf(TEXT("A boat comes to %s at cell %d"), *Name, Sea);
		++ListRevision;
		++MapRevision;
		return true;
	}
	OutMessage = FString::Printf(TEXT("No free sea within %d cells of %s"), MaxRings, *Hero->Name);
	return false;
}

bool UHexHeroes::BuildBoat(const FHexHeroOffer& Offer, FString& OutMessage)
{
	FHexHero* Hero = FindHeroMutable(Offer.HeroId);
	if (!Hero || !Sites)
	{
		return false;
	}
	const int32 Cell = FindBoatCell(Offer.Layer, Offer.Cell);
	if (Cell == INDEX_NONE)
	{
		OutMessage = TEXT("The shipyard has no free water beside it");
		return false;
	}
	if (!Sites->CanPay(Hero->Player, Offer.BoatCost))
	{
		OutMessage = FString::Printf(TEXT("A boat costs %s"), *Sites->DescribeCost(Offer.BoatCost));
		return false;
	}
	Sites->Pay(Hero->Player, Offer.BoatCost);
	Sites->AddBuiltBoat(Offer.Layer, Cell);
	OutMessage = FString::Printf(TEXT("The shipyard builds %s a boat"), *Hero->Name);
	++ListRevision;
	++MapRevision;
	return true;
}

void UHexHeroes::UpdateBoatBody(const FHexHero& Hero, bool bShown)
{
	TObjectPtr<UInstancedStaticMeshComponent>* Found = BoatBodies.Find(Hero.Id);
	UInstancedStaticMeshComponent* Body = Found ? Found->Get() : nullptr;
	AHexPlanet* Planet = GetPlanet(Hero.Layer);
	// under a hero in its boat, at sea or still at the coast as it goes aboard
	const bool bAtSea = Hero.BoatId != 0 && Planet && (Hero.bOnDeck || IsSea(Hero.Layer, Hero.Cell));
	if (!bShown || !bAtSea)
	{
		if (Body && Body->IsVisible())
		{
			Body->SetVisibility(false);
		}
		return;
	}
	if (!Body || Body->GetOwner() != Planet)
	{
		if (Body)
		{
			Body->DestroyComponent();
		}
		UStaticMesh* Mesh = Config->BoatMesh.LoadSynchronous();
		const UHexSiteConfig* SiteConfig = Sites ? Sites->GetConfig() : nullptr;
		if (!Mesh || !SiteConfig)
		{
			return;
		}
		Body = NewObject<UInstancedStaticMeshComponent>(Planet);
		Body->SetupAttachment(Planet->GetRootComponent());
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetStaticMesh(Mesh);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetNumCustomDataFloats(7);
		Body->bAffectDynamicIndirectLighting = Planet->GetSettings() && Planet->GetSettings()->AreSmallObjectsInLumen();
		// the material of the sites, its sail in the color of its player
		if (UMaterialInterface* Parent = SiteConfig->Material.LoadSynchronous())
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, this);
			HexMaterialConfig::Apply(Instance, SiteConfig);
			Body->SetMaterial(0, Instance);
		}
		Body->RegisterComponent();
		Body->AddInstance(FTransform::Identity, true);
		const FLinearColor Color = Config->GetPlayer(Hero.Player).Color;
		const float Data[7] = {1.0f, 1.0f, 1.0f, Color.R, Color.G, Color.B, -1000.0f};
		Body->SetCustomData(0, MakeArrayView(Data), true);
		BoatBodies.Add(Hero.Id, Body);
	}
	// on the water under it, or on the cell it goes aboard, rising, falling and rolling a little
	const FVector3d Where = Hero.bOnDeck ? Hero.Position : Planet->GetGrid().GetCells()[Hero.Cell].Center;
	const FVector3d Up = Where.GetSafeNormal();
	const UWorld* World = Planet->GetWorld();
	const double Time = World ? World->GetTimeSeconds() : 0.0;
	const double Phase = Time * double(Config->BoatBobSpeed) * UE_DOUBLE_TWO_PI + Hero.Id * 1.7;
	const double Bob = double(Config->BoatBob) * FMath::Sin(Phase);
	FVector3d Facing = Hero.Facing - Up * FVector3d::DotProduct(Hero.Facing, Up);
	Facing = Facing.Normalize() ? Facing : FVector3d::CrossProduct(Up, FVector3d::UnitZ()).GetSafeNormal();
	const FQuat Roll = FQuat(FVector(Facing), FMath::DegreesToRadians(double(Config->BoatRoll) * FMath::Sin(Phase * 0.8)));
	const FTransform Transform(Roll * FRotationMatrix::MakeFromZX(FVector(Up), FVector(Facing)).ToQuat(), FVector(Up * (Planet->GetRadius() + Bob)),
		FVector(Config->BoatScale * Hero.ViewScale));
	Body->UpdateInstanceTransform(0, Transform, true, true, true);
	if (!Body->IsVisible())
	{
		Body->SetVisibility(true);
	}
	// foam behind it while it sails
	if (Hero.bWalking && Hero.bOnDeck)
	{
		DropWake(Hero, *Planet, Where, Up, Facing, Time);
	}
}

void UHexHeroes::DropWake(const FHexHero& Hero, AHexPlanet& Planet, const FVector3d& Where, const FVector3d& Up, const FVector3d& Facing, double Time)
{
	double& Last = WakeTimes.FindOrAdd(Hero.Id, -1000.0);
	if (Time - Last < double(Config->WakeInterval))
	{
		return;
	}
	Last = Time;
	const int32 Layer = FMath::Clamp(int32(Hero.Layer), 0, 1);
	UInstancedStaticMeshComponent* Wake = Wakes[Layer];
	if (!Wake || Wake->GetOwner() != &Planet)
	{
		if (Wake)
		{
			Wake->DestroyComponent();
		}
		UStaticMesh* Mesh = Config->PathMesh.LoadSynchronous();
		UMaterialInterface* Parent = Config->WakeMaterial.LoadSynchronous();
		if (!Mesh || !Parent)
		{
			Wakes[Layer] = nullptr;
			return;
		}
		Wake = NewObject<UInstancedStaticMeshComponent>(&Planet);
		Wake->SetupAttachment(Planet.GetRootComponent());
		Wake->SetMobility(EComponentMobility::Movable);
		Wake->SetStaticMesh(Mesh);
		Wake->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wake->SetCastShadow(false);
		// a ring spreads WakeSpread times as wide in the material
		Wake->SetBoundsScale(FMath::Max(Config->WakeSpread, 1.0f));
		Wake->bAffectDynamicIndirectLighting = false;
		Wake->SetVisibleInRayTracing(false);
		// over the water, as the grid lines of the sea are, and under the clouds
		Wake->SetTranslucentSortPriority(1);
		Wake->SetNumCustomDataFloats(2);
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, this);
		HexMaterialConfig::Apply(Instance, Config);
		Wake->SetMaterial(0, Instance);
		Wake->RegisterComponent();
		Wakes[Layer] = Wake;
		WakeNext[Layer] = 0;
	}
	// behind the stern, on the water: the churn in its track, and a patch to each side, which drifts out into an arm of the V
	const double Stern = double(Config->WakeStern * Config->BoatScale * Hero.ViewScale);
	const double Size = double(Config->WakeSize * Config->BoatScale * Hero.ViewScale);
	const FVector3d Back = (Where.GetSafeNormal() * Planet.GetRadius() - Facing * Stern).GetSafeNormal();
	const FVector3d Side = FVector3d::CrossProduct(Back, Facing).GetSafeNormal();
	const int32 Pool = FMath::Max(Config->WakePool, 4);
	for (const float Arm : {0.0f, -1.0f, 1.0f})
	{
		const FVector3d Direction = (Back * Planet.GetRadius() + Side * (double(Arm) * Size * 0.5)).GetSafeNormal();
		// the churn wide, the arms long and thin along the way of the boat
		const FVector Scale = Arm == 0.0f ? FVector(Size * 1.6, Size * 1.0, Size) : FVector(Size * 2.2, Size * 0.5, Size);
		const FTransform Transform(FRotationMatrix::MakeFromZX(FVector(Direction), FVector(Facing)).ToQuat(), FVector(Direction * (Planet.GetRadius() + double(Config->WakeLift))), Scale);
		// a patch is added where it lies, never at a scale of nothing, and once the pool is full the oldest is laid again
		int32 Slot = WakeNext[Layer];
		if (Wake->GetInstanceCount() < Pool)
		{
			Slot = Wake->AddInstance(Transform, true);
		}
		else
		{
			Wake->UpdateInstanceTransform(Slot, Transform, true, false, true);
		}
		WakeNext[Layer] = (Slot + 1) % Pool;
		Wake->SetCustomDataValue(Slot, 0, float(Time), false);
		Wake->SetCustomDataValue(Slot, 1, Arm, true);
	}
	// its bounds take the rings laid since, or those further along the way than the first are culled
	Wake->UpdateBounds();
	Wake->MarkRenderTransformDirty();
}

void UHexCastles::RollMerchantStock(FHexCastle& Castle, int32 Week) const
{
	Castle.MerchantStock.Reset();
	if (!Heroes || !Config)
	{
		return;
	}
	// by the castle and the week, mostly of the lower ranks, no two alike
	FRandomStream Random(int32(HashCombine(GetTypeHash(Castle.Id * 7919 + 101), GetTypeHash(Week * 104729 + 7))));
	const EHexArtifactRank Ranks[4] = {EHexArtifactRank::Treasure, EHexArtifactRank::Minor, EHexArtifactRank::Minor, EHexArtifactRank::Major};
	for (int32 Try = 0; Try < Config->MerchantStockSize * 6 && Castle.MerchantStock.Num() < Config->MerchantStockSize; ++Try)
	{
		const EHexArtifactRank Rank = Ranks[Random.RandHelper(4)];
		const int32 Artifact = Heroes->PickArtifact(uint32(Random.GetUnsignedInt()), Rank, Rank);
		if (Artifact != INDEX_NONE && !Castle.MerchantStock.Contains(Artifact))
		{
			Castle.MerchantStock.Add(Artifact);
		}
	}
}

int32 UHexCastles::GetArtifactPrice(int32 Artifact) const
{
	const FHexArtifact* Each = Heroes ? Heroes->GetArtifact(Artifact) : nullptr;
	return Each ? FMath::Max(FMath::RoundToInt32(float(Each->Cost) * FMath::Max(Config->MerchantPriceFactor, 0.1f) / 50.0f) * 50, 0) : 0;
}

bool UHexCastles::BuyArtifact(int32 CastleId, int32 StockIndex, int32 HeroId, FString& OutMessage)
{
	FHexCastle* Castle = FindCastleMutable(CastleId);
	UHexSites* Sites = Heroes ? Heroes->GetSites() : nullptr;
	if (!Castle || !Sites || Castle->Owner == INDEX_NONE || !HasMerchant(*Castle))
	{
		OutMessage = TEXT("There is no artifact merchant there");
		return false;
	}
	if (!Castle->MerchantStock.IsValidIndex(StockIndex) || Castle->MerchantStock[StockIndex] == INDEX_NONE)
	{
		OutMessage = TEXT("That was sold");
		return false;
	}
	// for a hero of the owner in the castle or beside it
	const bool bThere = FindVisitors(*Castle).ContainsByPredicate([HeroId](const FHexHero* Each) { return Each->Id == HeroId; });
	FHexHero* Hero = bThere ? Heroes->FindHeroMutable(HeroId) : nullptr;
	if (!Hero)
	{
		OutMessage = TEXT("Only a hero of the owner in the castle or beside it buys an artifact there");
		return false;
	}
	const int32 Artifact = Castle->MerchantStock[StockIndex];
	TMap<FName, int32> Price;
	Price.Add(TEXT("Gold"), GetArtifactPrice(Artifact));
	if (!Sites->CanPay(Castle->Owner, Price))
	{
		OutMessage = FString::Printf(TEXT("The %s costs %s"), *Heroes->GetArtifact(Artifact)->DisplayName.ToLower(), *Sites->DescribeCost(Price));
		return false;
	}
	if (!Heroes->GiveArtifact(*Hero, Artifact))
	{
		OutMessage = FString::Printf(TEXT("The backpack of %s is full"), *Hero->Name);
		return false;
	}
	Sites->Pay(Castle->Owner, Price);
	Castle->MerchantStock[StockIndex] = INDEX_NONE;
	++Revision;
	OutMessage = FString::Printf(TEXT("%s buys the %s"), *Hero->Name, *Heroes->GetArtifact(Artifact)->DisplayName.ToLower());
	return true;
}

namespace
{
	/** hex.Hero artifact: gives the hero chosen, or one named, an artifact by its Id, or every artifact with all, for trying them */
	void RunArtifactCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UHexPlanetLayers* Layers = UHexPlanetLayers::Get(World);
		AHexPlanet* Planet = Layers ? Layers->GetPresentedBuilt() : nullptr;
		UHexHeroes* Heroes = Planet ? Planet->GetHeroes() : nullptr;
		const UHexHeroConfig* Config = Heroes ? Heroes->GetConfig() : nullptr;
		if (!Config || Args.IsEmpty())
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Artifact <Id|all|list|wear <Backpack>|off <Slot>|pass <Hero> <Worn|Pack> <Index>> [Hero], on a built planet"));
			return;
		}
		const FHexHero* Hero = Args.Num() >= 2 && !Args[0].Equals(TEXT("wear"), ESearchCase::IgnoreCase) && !Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)
			&& !Args[0].Equals(TEXT("pass"), ESearchCase::IgnoreCase) ? Heroes->FindHeroByText(Args.Last()) : nullptr;
		Hero = Hero ? Hero : Heroes->GetSelected();
		if (!Hero)
		{
			UE_LOG(LogHexHero, Warning, TEXT("hex.Artifact: no hero chosen"));
			return;
		}
		const int32 Id = Hero->Id;
		FString Message;
		if (Args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			for (int32 Index = 0; Index < Config->Artifacts.Num(); ++Index)
			{
				const FHexArtifact& Each = Config->Artifacts[Index];
				UE_LOG(LogHexHero, Log, TEXT("  %d %s (%s, %s, %d gold): %s"), Index, *Each.Id.ToString(), *GetArtifactSlotName(Each.Slot), *GetArtifactRankName(Each.Rank), Each.Cost,
					*Each.DescribeEffect());
			}
			for (int32 Slot = 0; Slot < Hero->Worn.Num(); ++Slot)
			{
				UE_LOG(LogHexHero, Log, TEXT("  %s wears in slot %d: %s"), *Hero->Name, Slot, Heroes->GetArtifact(Hero->Worn[Slot]) ? *Heroes->GetArtifact(Hero->Worn[Slot])->DisplayName : TEXT("-"));
			}
			for (const int32 Each : Hero->Backpack)
			{
				UE_LOG(LogHexHero, Log, TEXT("  %s carries %s"), *Hero->Name, Heroes->GetArtifact(Each) ? *Heroes->GetArtifact(Each)->DisplayName : TEXT("?"));
			}
			return;
		}
		if (Args[0].Equals(TEXT("wear"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			Heroes->WearArtifact(Id, FCString::Atoi(*Args[1]), Message);
		}
		else if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase) && Args.Num() >= 2)
		{
			Heroes->TakeOffArtifact(Id, FCString::Atoi(*Args[1]), Message);
		}
		else if (Args[0].Equals(TEXT("pass"), ESearchCase::IgnoreCase) && Args.Num() >= 4)
		{
			const FHexHero* To = Heroes->FindHeroByText(Args[1]);
			Heroes->PassArtifact(Id, Args[2].Equals(TEXT("worn"), ESearchCase::IgnoreCase), FCString::Atoi(*Args[3]), To ? To->Id : 0, Message);
		}
		else if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			int32 Given = 0;
			for (int32 Index = 0; Index < Config->Artifacts.Num(); ++Index)
			{
				Given += Heroes->GiveArtifact(Id, Index) ? 1 : 0;
			}
			Message = FString::Printf(TEXT("%s takes %d artifacts"), *Hero->Name, Given);
		}
		else
		{
			const int32 Artifact = Args[0].IsNumeric() ? FCString::Atoi(*Args[0]) : Config->FindArtifact(Args[0]);
			Message = Heroes->GiveArtifact(Id, Artifact) ? FString::Printf(TEXT("%s takes the %s"), *Hero->Name, *Config->Artifacts[Artifact].DisplayName.ToLower())
				: FString::Printf(TEXT("No artifact %s, or the backpack is full"), *Args[0]);
		}
		UE_LOG(LogHexHero, Log, TEXT("hex.Artifact: %s"), *Message);
		if (!Message.IsEmpty())
		{
			Heroes->OnMessage.Broadcast(Message);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ArtifactCommand(TEXT("hex.Artifact"),
		TEXT("The artifacts of the hero chosen, or of one named last: <Id or index> gives one, all gives every artifact, list lists them and what it wears and carries, wear <Backpack> puts on what lies at that place of its backpack, off <Slot> takes off what it wears in a slot (0 head to 11), pass <Hero> <worn|pack> <Index> gives it to a hero beside it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunArtifactCommand));
}
