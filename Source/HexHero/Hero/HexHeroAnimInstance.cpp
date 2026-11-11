#include "HexHeroAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HexHeroAnimInstance)

namespace
{
	/** Moves the time of a sequence on, around its end when it loops and up to it when not */
	float Advance(const UAnimSequence* Sequence, float Time, float Delta, bool bLoop)
	{
		const float Length = Sequence ? Sequence->GetPlayLength() : 0.0f;
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		Time += Delta;
		return bLoop ? FMath::Fmod(Time, Length) : FMath::Min(Time, Length);
	}
}

void FHexHeroAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UHexHeroAnimInstance* Instance = Cast<UHexHeroAnimInstance>(InAnimInstance))
	{
		Current = Instance->Current;
		Previous = Instance->Previous;
		CurrentTime = Instance->CurrentTime;
		PreviousTime = Instance->PreviousTime;
		Blend = Instance->Blend;
	}
}

bool FHexHeroAnimProxy::Evaluate(FPoseContext& Output)
{
	if (!Current)
	{
		Output.ResetToRefPose();
		return true;
	}
	{
		FAnimationPoseData Data(Output);
		Current->GetAnimationPose(Data, FAnimExtractContext(double(CurrentTime), false));
	}
	if (Previous && Blend < 1.0f)
	{
		FPoseContext Old(Output);
		{
			FAnimationPoseData Data(Old);
			Previous->GetAnimationPose(Data, FAnimExtractContext(double(PreviousTime), false));
		}
		FPoseContext New(Output);
		New = Output;
		const FAnimationPoseData NewData(New);
		const FAnimationPoseData OldData(Old);
		FAnimationPoseData OutData(Output);
		// eased, so a blend neither starts nor ends with a jerk
		FAnimationRuntime::BlendTwoPosesTogether(NewData, OldData, FMath::SmoothStep(0.0f, 1.0f, Blend), OutData);
	}
	return true;
}

FAnimInstanceProxy* UHexHeroAnimInstance::CreateAnimInstanceProxy()
{
	return new FHexHeroAnimProxy(this);
}

void UHexHeroAnimInstance::Play(UAnimSequence* Sequence, bool bInLoop, float InRate, float InBlendTime, float StartShare, UAnimSequence* InNext, float InNextRate)
{
	if (!Sequence)
	{
		return;
	}
	Previous = Current;
	PreviousTime = CurrentTime;
	PreviousRate = Rate;
	bPreviousLoop = bLoop;
	Current = Sequence;
	CurrentTime = FMath::Clamp(StartShare, 0.0f, 1.0f) * Sequence->GetPlayLength();
	Rate = InRate;
	bLoop = bInLoop;
	Next = InNext;
	NextRate = InNextRate;
	BlendTime = InBlendTime;
	Blend = Previous && InBlendTime > UE_KINDA_SMALL_NUMBER ? 0.0f : 1.0f;
}

void UHexHeroAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (!Current)
	{
		return;
	}
	CurrentTime = Advance(Current, CurrentTime, DeltaSeconds * Rate, bLoop);
	if (Previous)
	{
		PreviousTime = Advance(Previous, PreviousTime, DeltaSeconds * PreviousRate, bPreviousLoop);
		Blend = FMath::Min(Blend + DeltaSeconds / FMath::Max(BlendTime, UE_KINDA_SMALL_NUMBER), 1.0f);
		if (Blend >= 1.0f)
		{
			Previous = nullptr;
		}
	}
	// what was played once is followed by what comes after it, blending in over its last moments
	if (!bLoop && Next && CurrentTime >= Current->GetPlayLength() - BlendTime * FMath::Max(Rate, 0.01f))
	{
		Play(Next, true, NextRate, BlendTime);
	}
}
