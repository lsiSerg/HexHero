// Copyright Epic Games, Inc. All Rights Reserved.

#include "HexUnitFigure.h"
#include "HexBattleConfig.h"
#include "HexHeroAnimInstance.h"
#include "HexHero.h"
#include "AnimationRuntime.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FHexUnitAnimation* GetAnimation(const FHexUnitLook& Look, EHexFigureAction Action)
	{
		switch (Action)
		{
		case EHexFigureAction::Idle: return &Look.Idle;
		case EHexFigureAction::Move: return &Look.Move;
		case EHexFigureAction::Attack: return &Look.Attack;
		case EHexFigureAction::Shoot: return Look.Shoot.Sequence.IsNull() ? &Look.Attack : &Look.Shoot;
		case EHexFigureAction::Hit: return &Look.Hit;
		case EHexFigureAction::Death: return &Look.Death;
		case EHexFigureAction::Walk: return Look.Walk.Sequence.IsNull() ? &Look.Move : &Look.Walk;
		case EHexFigureAction::Fly: return Look.Fly.Sequence.IsNull() ? &Look.Move : &Look.Fly;
		}
		return nullptr;
	}
}

USceneComponent* HexUnitFigure::Make(UObject* Owner, USceneComponent* Parent, const UHexBattleConfig* Config, FName Creature, bool bUpgraded, int32 Level,
	const FLinearColor& SideColor, float HeightScale, float& OutScale, float& OutYaw, float& OutHover)
{
	OutScale = 1.0f;
	OutYaw = 0.0f;
	OutHover = 0.0f;
	if (!Config || !Owner)
	{
		return nullptr;
	}
	const FHexUnitLook* Look = Config->FindLook(Creature);
	USkeletalMesh* Mesh = nullptr;
	if (Look)
	{
		Mesh = bUpgraded && !Look->UpgradedMesh.IsNull() ? Look->UpgradedMesh.LoadSynchronous() : Look->Mesh.LoadSynchronous();
	}
	USceneComponent* Figure = nullptr;
	UMeshComponent* Drawn = nullptr;
	float MeshHeight = 100.0f;
	// where the foot of the mesh lies below its origin, so the figure stands on the ground
	float Foot = 0.0f;
	if (Mesh)
	{
		USkeletalMeshComponent* Animated = NewObject<USkeletalMeshComponent>(Owner);
		Animated->SetSkeletalMesh(Mesh);
		Animated->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Animated->SetAnimInstanceClass(UHexHeroAnimInstance::StaticClass());
		Animated->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
		MeshHeight = Look->MeshHeight > 0.0f ? Look->MeshHeight : FMath::Max(float(Bounds.BoxExtent.Z * 2.0), 1.0f);
		Foot = Look->MeshHeight > 0.0f ? Look->MeshFoot : float(Bounds.Origin.Z - Bounds.BoxExtent.Z);
		// the lowest and highest bone of its pose at rest, as the mesh stands in the engine, once a creature
		static TSet<FName> Logged;
		if (!Logged.Contains(Creature))
		{
			Logged.Add(Creature);
			TArray<FTransform> Pose;
			FAnimationRuntime::FillUpComponentSpaceTransforms(Mesh->GetRefSkeleton(), Mesh->GetRefSkeleton().GetRefBonePose(), Pose);
			double Low = TNumericLimits<double>::Max();
			double High = -TNumericLimits<double>::Max();
			for (const FTransform& Bone : Pose)
			{
				Low = FMath::Min(Low, Bone.GetLocation().Z);
				High = FMath::Max(High, Bone.GetLocation().Z);
			}
			UE_LOG(LogHexHero, Log, TEXT("Figure %s: bones from %.1f to %.1f, the look says the foot at %.1f and the height %.1f"), *Creature.ToString(), Low, High, Foot, MeshHeight);
		}
		OutYaw = Look->Yaw;
		OutHover = Look->Hover;
		Figure = Drawn = Animated;
	}
	else if (UStaticMesh* Fallback = Config->FallbackMesh.LoadSynchronous())
	{
		UStaticMeshComponent* Still = NewObject<UStaticMeshComponent>(Owner);
		Still->SetStaticMesh(Fallback);
		MeshHeight = FMath::Max(float(Fallback->GetBounds().BoxExtent.Z * 2.0), 1.0f);
		Foot = float(Fallback->GetBounds().Origin.Z - Fallback->GetBounds().BoxExtent.Z);
		Figure = Drawn = Still;
	}
	if (!Figure)
	{
		return nullptr;
	}
	// as high as its look says, or a fallback by its level
	const float Height = Look && Mesh ? Look->Height : 30.0f + 8.0f * float(Level);
	OutScale = Height * HeightScale / MeshHeight;
	OutHover = OutHover * HeightScale - Foot * OutScale;
	if (Parent)
	{
		Figure->SetupAttachment(Parent);
	}
	Figure->SetMobility(EComponentMobility::Movable);
	Drawn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Drawn->SetCanEverAffectNavigation(false);
	if (UMaterialInterface* Material = Config->UnitMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, Owner);
		const FLinearColor Tint = Look && Mesh ? (bUpgraded ? Look->UpgradedTint : Look->Tint) : SideColor;
		Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
		Instance->SetVectorParameterValue(TEXT("SideColor"), SideColor);
		Instance->SetScalarParameterValue(TEXT("SideColorShare"), Look ? Look->SideColorShare : 0.0f);
		if (Look)
		{
			Instance->SetScalarParameterValue(TEXT("DetailScale"), Look->DetailScale);
			Instance->SetScalarParameterValue(TEXT("DetailTone"), Look->DetailTone);
			Instance->SetScalarParameterValue(TEXT("DetailBump"), Look->DetailBump);
			Instance->SetScalarParameterValue(TEXT("Roughness"), Look->Roughness);
		}
		for (int32 Slot = 0; Slot < FMath::Max(Drawn->GetNumMaterials(), 1); ++Slot)
		{
			Drawn->SetMaterial(Slot, Instance);
		}
	}
	return Figure;
}

bool HexUnitFigure::Play(USceneComponent* Figure, const UHexBattleConfig* Config, FName Creature, EHexFigureAction Action, float RateScale, bool bAtEnd)
{
	USkeletalMeshComponent* Animated = Cast<USkeletalMeshComponent>(Figure);
	UHexHeroAnimInstance* Anim = Animated ? Cast<UHexHeroAnimInstance>(Animated->GetAnimInstance()) : nullptr;
	const FHexUnitLook* Look = Config ? Config->FindLook(Creature) : nullptr;
	if (!Anim || !Look)
	{
		return false;
	}
	const FHexUnitAnimation* Each = GetAnimation(*Look, Action);
	UAnimSequence* Sequence = Each ? Each->Sequence.LoadSynchronous() : nullptr;
	const bool bOwn = Sequence != nullptr;
	if (!Sequence && Action != EHexFigureAction::Idle)
	{
		// without its own animation an action keeps the figure as it is, or idle
		Each = &Look->Idle;
		Sequence = Each->Sequence.LoadSynchronous();
		Action = EHexFigureAction::Idle;
	}
	if (!Sequence)
	{
		return false;
	}
	const bool bLoop = Action == EHexFigureAction::Idle || Action == EHexFigureAction::Move || Action == EHexFigureAction::Walk || Action == EHexFigureAction::Fly;
	UAnimSequence* Idle = Look->Idle.Sequence.LoadSynchronous();
	// an idle of several figures each at its own place in it, so they do not breathe as one
	const float Start = bAtEnd ? 1.0f : bLoop ? FMath::Frac(RateScale * 7.31f) : 0.0f;
	Anim->Play(Sequence, bLoop, Each->Rate * RateScale, bAtEnd ? 0.0f : 0.15f, Start, bLoop || Action == EHexFigureAction::Death ? nullptr : Idle, Look->Idle.Rate);
	return bOwn;
}

void HexUnitFigure::Settle(USceneComponent* Figure)
{
	if (USkeletalMeshComponent* Animated = Cast<USkeletalMeshComponent>(Figure))
	{
		Animated->TickAnimation(0.0f, false);
		Animated->RefreshBoneTransforms();
	}
}

bool HexUnitFigure::CanWalk(const UHexBattleConfig* Config, FName Creature)
{
	const FHexUnitLook* Look = Config ? Config->FindLook(Creature) : nullptr;
	return Look && !Look->Walk.Sequence.IsNull();
}

float HexUnitFigure::GetLength(const UHexBattleConfig* Config, FName Creature, EHexFigureAction Action)
{
	const FHexUnitLook* Look = Config ? Config->FindLook(Creature) : nullptr;
	const FHexUnitAnimation* Each = Look ? GetAnimation(*Look, Action) : nullptr;
	const UAnimSequence* Sequence = Each ? Each->Sequence.LoadSynchronous() : nullptr;
	return Sequence ? Sequence->GetPlayLength() / FMath::Max(Each->Rate, 0.05f) : 0.0f;
}
