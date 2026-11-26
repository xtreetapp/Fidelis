#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SlideComponent.generated.h"

class ACharacter;
class APlayerController;
class UCharacterMovementComponent;
class UInputAction;

/**
 * Sprint + crouch + slide for the player character.
 *  - Auto sprint (bAutoSprint): moving in any direction runs automatically for AutoSprintDuration
 *    (or AutoSprintDistance when the duration is 0),
 *    then the sprint recharges for AutoSprintRecharge seconds after it ended.
 *  - Hold IA_Sprint (Left Shift) while moving to run (if the action is mapped).
 *  - Press IA_Crouch (C) while running on the ground -> slide forward a few meters, then stand up.
 *  - Hold IA_Crouch while not running -> crouch.
 * Input is read from the actions mapped in IMC_Default.
 */
UCLASS(ClassGroup = (Fidelis), meta = (BlueprintSpawnableComponent))
class FIDELIS_API USlideComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USlideComponent();

	UFUNCTION(BlueprintPure, Category = "Slide")
	bool IsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintPure, Category = "Slide")
	bool IsSliding() const { return bIsSliding; }

	/** Time the last plain crouch (crouch key held, not running) started, in world seconds. */
	double GetLastCrouchStartTime() const { return LastCrouchStartTime; }

	/** Time the last plain crouch ended (crouch key released), in world seconds. */
	double GetLastCrouchEndTime() const { return LastCrouchEndTime; }

	/** Getting back up from a plain crouch: 0 = just released, 1 = back on its feet. */
	float GetCrouchStandUpAlpha() const;

	/** Time the last slide ended, in world seconds. */
	double GetLastSlideEndTime() const { return LastSlideEndTime; }

	/** Getting back up after a slide: 0 = just ended, 1 = back on its feet. */
	float GetStandUpAlpha() const;

	/** Stops the current slide right away (e.g. to lie down from it). */
	void CancelSlide(const TCHAR* Reason) { if (bIsSliding) { EndSlide(Reason); } }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide|Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** Move action (2D: X = right / left), read during the slide for the sideways drift. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide|Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Time taken to get back up after a slide (s): the view rises over it and the character can't
	 *  run until it is over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideStandUpDuration = 0.4f;

	/** Sideways drift while sliding, holding left / right (cm/s, along the view's right). 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideStrafeSpeed = 400.f;

	/** How fast the sideways drift follows the keys. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideStrafeInterpSpeed = 8.f;

	/** Max ground speed while sprinting (cm/s). Walking speed is the character's MaxWalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0))
	float SprintSpeed = 900.f;

	/** Forward walking speed while aiming down sights (cm/s). 0 = same as walking without aiming.
	 *  Sideways / backward use the walk multipliers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0))
	float AimWalkSpeed = 0.f;

	/** Forward sprint speed while aiming down sights (cm/s). Sideways / backward use the sprint multipliers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0))
	float AimSprintSpeed = 750.f;

	/** Walking speed multiplier when moving sideways, relative to forward (600 -> 450). Diagonals blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float WalkSidewaysSpeedMultiplier = 0.75f;

	/** Walking speed multiplier when moving backward, relative to forward (600 -> 450). Diagonals blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float WalkBackwardSpeedMultiplier = 0.75f;

	/** Sprint speed multiplier when moving sideways, relative to forward (900 -> 600). Diagonals blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float SprintSidewaysSpeedMultiplier = 0.6667f;

	/** Sprint speed multiplier when moving backward, relative to forward (900 -> 600). Diagonals blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float SprintBackwardSpeedMultiplier = 0.6667f;

	/** Run automatically when moving (no sprint key needed), in every direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint")
	bool bAutoSprint = true;

	/** Distance covered by one automatic sprint before going back to walking (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, EditCondition = "bAutoSprint"))
	float AutoSprintDistance = 1500.f;

	/** Time after the end of the last sprint before an automatic sprint can start again (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, EditCondition = "bAutoSprint"))
	float AutoSprintRecharge = 3.f;

	/** Duration of one automatic sprint before going back to walking (s). 0 = use AutoSprintDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, EditCondition = "bAutoSprint"))
	float AutoSprintDuration = 6.f;

	/** Minimum horizontal speed (fraction of the running speed in the current direction) to count as
	 *  running when crouch is pressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float MinSprintSpeedRatioToSlide = 0.7f;

	/** Distance covered by a forward slide on flat ground (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideDistance = 600.f;

	/** Distance covered by a sideways or backward slide on flat ground (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideDistanceSideBack = 400.f;

	/** Speed at the start of the slide, relative to the running speed in the slide direction
	 *  (1.1 = 10% faster than running). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideSpeedMultiplier = 1.1f;

	/** The sprint survives stops shorter than this (s): quick direction changes, left / right strafing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0))
	float SprintStopGrace = 0.35f;

	/** A crouch lasts at least this long (s), even on a quick tap of the key: the full crouch
	 *  movement plays before standing back up. Match it to the Lean component's CrouchDownDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float CrouchMinDuration = 0.25f;

	/** Time to stand back up from a plain crouch (s): the full movement plays (the crouch key can't
	 *  interrupt it) and the character can't run until it is over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float CrouchStandUpDuration = 0.25f;

	/** No sprint while aiming down sights (walking at the aim walk speed); running resumes when the aim is
	 *  released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint")
	bool bNoSprintWhileAiming = true;

	/** Run straight away when standing up from a crouch walk (crouch key released while moving). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	bool bSprintAfterCrouch = true;

	/** Run straight away when standing up at the end of a slide, a dive roll or after lying down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	bool bSprintAfterSlide = true;

	/** Speed at the end of the slide (cm/s), before standing up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideEndSpeed = 350.f;

	/** Delay after a slide before another one can start (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideCooldown = 0.4f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool IsActionHeld(APlayerController* PC, const UInputAction* Action) const;
	void SetSprinting(bool bSprint, const TCHAR* Reason);
	/** Walk / sprint max speed scaled by the movement direction relative to where the player looks. */
	void ApplyDirectionalSpeed();
	void StartSlide();
	void TickSlide(float DeltaTime);
	void EndSlide(const TCHAR* Reason);

	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;

	float WalkSpeed = 600.f;
	float SavedGroundFriction = 8.f;
	float SavedBrakingDeceleration = 2048.f;
	float SavedMaxWalkSpeedCrouched = 300.f;

	bool bIsSprinting = false;
	/** Set at the end of a slide: start running again as soon as the character can. */
	bool bSprintPendingAfterSlide = false;
	/** Current sideways drift during the slide (cm/s, + = right). */
	float SlideStrafe = 0.f;
	/** Crouch pressed to leave a slide: ignored until the key is released. */
	bool bCrouchIgnoredUntilRelease = false;
	double LastCrouchStartTime = -100.0;
	double LastCrouchEndTime = -100.0;
	bool bWasAiming = false;
	bool IsAiming() const;
	/** Last time the character was moving (for SprintStopGrace). */
	double LastMovingTime = -100.0;
	/** When the pending run was owed (end of the slide / roll). */
	double SprintPendingSince = -100.0;
	/** The dive component was busy (diving, lying down, rolling) last tick. */
	bool bWasDiveBusy = false;
	/** Speed at the start of the current slide (cm/s): SprintSpeed * SlideSpeedMultiplier. */
	float SlideStartSpeed = 0.f;
	bool bIsSliding = false;
	bool bCrouchWasHeld = false;
	bool bCrouchedByHold = false;

	FVector SlideDirection = FVector::ForwardVector;
	float SlideElapsed = 0.f;
	float SlideDuration = 0.f;
	double LastSlideEndTime = -100.0;

	// Auto sprint: distance run since the sprint started, and when the last sprint ended.
	float SprintDistance = 0.f;
	double SprintStartTime = 0.0;
	FVector LastSprintLocation = FVector::ZeroVector;
	double LastSprintEndTime = -100.0;
};
