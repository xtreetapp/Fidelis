#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DiveComponent.generated.h"

class ACharacter;
class APlayerController;
class UCharacterMovementComponent;
class UInputAction;

UENUM(BlueprintType)
enum class EFidelisDiveState : uint8
{
	None,
	Airborne,	// launched, in the air
	Prone,		// landed, lying down while the dive key is held
	Rolling		// dive key then crouch key: ground roll in the movement direction
};

/**
 * Omnidirectional dive: while running (in any direction, backwards included), press the dive key
 * (IA_Dive) to dive in the direction you are moving: forward, sideways or backward.
 * The character stays lying down as long as the key is held and stands up as soon as it is released.
 *
 * Every tuning value is EditAnywhere: add a "Dive Component" to BP_FirstPersonCharacter to tweak it
 * in the editor (the runtime-added default is only used when the Blueprint has none).
 */
UCLASS(ClassGroup = (Fidelis), meta = (BlueprintSpawnableComponent))
class FIDELIS_API UDiveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDiveComponent();

	UFUNCTION(BlueprintPure, Category = "Dive")
	EFidelisDiveState GetDiveState() const { return State; }

	/** True while airborne or prone: other movement abilities should not interfere. */
	UFUNCTION(BlueprintPure, Category = "Dive")
	bool IsBusy() const { return State != EFidelisDiveState::None; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dive|Input")
	TObjectPtr<UInputAction> DiveAction;

	/** Crouch action: pressed shortly after the dive key, it turns the dive into a roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dive|Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** Only allow diving while running (sprint held, see SlideComponent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions")
	bool bRequireSprint = true;

	/** Minimum horizontal speed to dive (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions", meta = (ClampMin = 0))
	float MinSpeedToDive = 300.f;

	/** Delay after standing up before another dive (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions", meta = (ClampMin = 0))
	float DiveCooldown = 0.5f;

	/** Horizontal launch speed in the movement direction (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveHorizontalSpeed = 850.f;

	/** Vertical launch speed (cm/s): higher = longer, higher dive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveUpSpeed = 320.f;

	/** Air control during the dive (0 = locked trajectory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0, ClampMax = 1))
	float DiveAirControl = 0.f;

	/** Capsule half height while lying down (cm). Clamped to the capsule radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float ProneHalfHeight = 34.f;

	/** Crawl speed while lying down (cm/s). 0 = stays in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float ProneMoveSpeed = 0.f;

	/** Ground friction applied on landing (how fast the body stops sliding on its belly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float LandingFriction = 4.f;

	/** Braking deceleration after landing (cm/s²). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float LandingBraking = 1200.f;

	/** Enables the roll combo (dive key, then crouch key while running). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll")
	bool bEnableRoll = true;

	/** Time after the dive key during which the crouch key triggers a roll (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollComboWindow = 0.35f;

	/** Downward speed applied when the roll is triggered in the air, to reach the ground quickly (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollDropSpeed = 700.f;

	/** Distance covered by the roll on flat ground (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollDistance = 350.f;

	/** Speed at the start of the roll (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollStartSpeed = 850.f;

	/** Speed at the end of the roll (cm/s), before standing up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollEndSpeed = 300.f;

	/** Capsule half height while rolling (cm). Clamped to the capsule radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollHalfHeight = 45.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool IsActionHeld(APlayerController* PC, const UInputAction* Action) const;
	bool CanStartDive() const;
	void StartDive();
	void Land(bool bKeyHeld);
	void StandUp(const TCHAR* Reason);
	void RestoreMovementSettings();
	/** SetIgnoreMoveInput is a counter on the controller: only call it when our lock actually changes. */
	void SetMoveLocked(bool bLocked);
	void StartRoll();
	void TickRoll(float DeltaTime);
	void EndRoll(const TCHAR* Reason);

	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;

	EFidelisDiveState State = EFidelisDiveState::None;
	bool bDiveWasHeld = false;
	bool bMoveLocked = false;
	bool bCrouchWasHeld = false;
	bool bRollQueued = false;
	FVector DiveDirection = FVector::ForwardVector;
	const TCHAR* DiveDirectionName = TEXT("avant");
	float RollElapsed = 0.f;
	float RollDuration = 0.f;
	float AirTime = 0.f;
	double LastStandTime = -100.0;

	// Saved movement settings, restored when standing up.
	float SavedCrouchedHalfHeight = 40.f;
	float SavedMaxWalkSpeedCrouched = 300.f;
	float SavedGroundFriction = 8.f;
	float SavedBrakingDeceleration = 2048.f;
	float SavedAirControl = 0.05f;
};
