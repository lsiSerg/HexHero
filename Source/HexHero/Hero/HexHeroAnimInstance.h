#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "HexHeroAnimInstance.generated.h"

class UAnimSequence;

/** Blends the animation a hero plays with the one it played before, on the thread that evaluates poses */
USTRUCT()
struct FHexHeroAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FHexHeroAnimProxy() = default;
	explicit FHexHeroAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	UAnimSequence* Current = nullptr;
	UAnimSequence* Previous = nullptr;
	float CurrentTime = 0.0f;
	float PreviousTime = 0.0f;
	/** How much of the pose is the current animation, 1 once the blend from the one before is over */
	float Blend = 1.0f;
};

/** The animations of the mesh of a hero (UHexHeroes), without an animation blueprint: one sequence plays, looped or once, and blends in
 *  from the one before over a time; after a sequence played once another may follow, looped. The developer asked on 2026-11-11 for a hero
 *  that moves, stops, sets out and answers being chosen. */
UCLASS(Transient, NotBlueprintable)
class HEXHERO_API UHexHeroAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Plays a sequence from a share of its length, blending in from what plays now; one that does not loop is followed by Next, looped */
	void Play(UAnimSequence* Sequence, bool bInLoop, float InRate, float InBlendTime, float StartShare = 0.0f, UAnimSequence* InNext = nullptr, float InNextRate = 1.0f);

	void SetRate(float InRate) { Rate = InRate; }

	UAnimSequence* GetCurrent() const { return Current; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	friend struct FHexHeroAnimProxy;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Current;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Previous;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Next;

	float CurrentTime = 0.0f;
	float PreviousTime = 0.0f;
	float Rate = 1.0f;
	float PreviousRate = 1.0f;
	float NextRate = 1.0f;
	float Blend = 1.0f;
	float BlendTime = 0.2f;
	bool bLoop = true;
	bool bPreviousLoop = true;
};
