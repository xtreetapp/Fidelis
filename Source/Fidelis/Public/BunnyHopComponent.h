#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BunnyHopComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UInputAction;

/**
 * Bunny hop: jumping again right after landing keeps the horizontal speed the character had in the
 * air (ground friction is skipped), and each chained hop adds a small speed gain capped by
 * MaxBunnyHopSpeed. All settings are EditAnywhere: add a "Bunny Hop Component" to the character
 * Blueprint to tweak them in the editor.
 */
UCLASS(ClassGroup = (Fidelis), meta = (BlueprintSpawnableComponent))
class FIDELIS_API UBunnyHopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBunnyHopComponent();

	UFUNCTION(BlueprintPure, Category = "Bunny Hop")
	int32 GetChainedHops() const { return ChainedHops; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop")
	bool bEnableBunnyHop = true;

	/** If the jump key is still held on landing, jump again immediately (easy chaining). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop")
	bool bAutoHopWhileJumpHeld = true;

	/** Jump action (IA_Jump), used for bAutoHopWhileJumpHeld. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bunny Hop")
	TObjectPtr<UInputAction> JumpAction;

	/** Max time between landing and the next jump for it to count as a hop (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float HopWindow = 0.15f;

	/** Speed added on each chained hop (cm/s). 0 = only keep the speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float SpeedGainPerHop = 40.f;

	/** Upper limit for the speed reached through bunny hopping (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float MaxBunnyHopSpeed = 1200.f;

	/** Below this horizontal speed, a jump is a normal jump (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float MinSpeedToHop = 200.f;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;

	bool bWasFalling = false;
	float LastAirSpeed = 0.f;
	double LandTime = -100.0;
	int32 ChainedHops = 0;
	/** Horizontal speed kept during the air phase of a chained hop (0 = not hopping). */
	float HopSpeed = 0.f;
};
