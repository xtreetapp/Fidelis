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
	/** Slide started after a dive roll: it plays out in full, only crouch or jump stop it. */
	bool IsSlideFromRoll() const { return bIsSliding && bSlideFromRoll; }
	/** Slide after a roll done standing (crouch not held at the end of the roll). */
	bool IsStandingSlideFromRoll() const { return IsSlideFromRoll() && !bSlideFromRollCrouched; }

	/** Called by the dive component the very frame its roll ends on the ground: the standing slide takes
	 *  over right away (no frame where the normal ground braking hits the roll's speed). */
	void StartSlideAfterRoll(double RollEndTime, float Speed, float StartLeanRoll = 0.f);

	/** Slide after a roll: side sway of the view and arms, in view space (roll around the line of sight).
	 *  Identity while RecoverySwayAngle is 0. */
	FQuat GetRecoveryLeanViewRotation() const;
	/** Progress of the current slide (0 at the start, 1 at the end of its distance). */
	float GetSlideProgress() const { return SlideDuration > 0.f ? FMath::Clamp(SlideElapsed / SlideDuration, 0.f, 1.f) : 1.f; }

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

	/** Time left in the current slide (s), 0 when not sliding. */
	float GetSlideTimeLeft() const { return bIsSliding ? FMath::Max(SlideDuration - SlideElapsed, 0.f) : 0.f; }

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
	float SlideStandUpDuration = 0.3f;

	/** A movement key to the side of the slide turns it towards that key (any slide: forward, side, back,
	 *  after a roll). Replaces the sideways drift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	bool bSlideSteering = true;

	/** Most the slide can turn from its start direction (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0, ClampMax = 180, EditCondition = "bSlideSteering"))
	float SlideMaxTurnAngle = 90.f;

	/** How fast the slide turns while the key is held (degrees per second). A normal slide lasts about
	 *  0.6 s: 150 lets it turn the full 90 degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0, EditCondition = "bSlideSteering"))
	float SlideTurnRate = 150.f;

	/** Sideways drift while sliding, holding left / right (cm/s, along the view's right), without steering. 0 = none. */
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
	float WalkSidewaysSpeedMultiplier = 1.f;

	/** Walking speed multiplier when moving backward, relative to forward (600 -> 450). Diagonals blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float WalkBackwardSpeedMultiplier = 1.f;

	/** Sprint speed multiplier when moving sideways, relative to forward (900 -> 600). Diagonals blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float SprintSidewaysSpeedMultiplier = 1.f;

	/** Sprint speed multiplier when moving backward, relative to forward (900 -> 600). Diagonals blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float SprintBackwardSpeedMultiplier = 1.f;

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
	float SlideDistanceSideBack = 600.f;

	/** AVERAGE speed over the first half of the slide distance, relative to the running speed in the
	 *  slide direction (1.2 = 900 -> 1080 cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideFirstHalfSpeedMultiplier = 1.2f;

	/** AVERAGE speed over the second half of the slide distance, relative to the running speed
	 *  (1.0 = 900 -> 900 cm/s). It ends at SlideEndSpeed. Each half has its peak at its start and slows
	 *  down along SlideSpeedCurve; the second half starts at the speed the first one ends at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideSecondHalfSpeedMultiplier = 1.f;

	/** Shape of the slowdown in each half: speed = start - (start - end) * progress^curve. 1 = steady
	 *  (linear) slowdown, 2 = stays fast and slows down mostly at the end (peak at the start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0.1))
	float SlideSpeedCurve = 2.f;

	/** Same, for the second half only. Higher = stays fast longer and brakes harder at the very end, which
	 *  keeps the second half's start (= end of the first half) under the first half's average, so the
	 *  slide still peaks at its very start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0.1))
	float SlideSecondHalfSpeedCurve = 4.f;

	/** Slide speed (cm/s) after Elapsed seconds of the current / last slide (curve above). */
	float GetSlideSpeedAt(float Elapsed) const;

	/** Time since the current slide started (s). */
	float GetSlideElapsed() const { return SlideElapsed; }

	/** The sprint survives stops shorter than this (s): quick direction changes, left / right strafing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0))
	float SprintStopGrace = 0.35f;

	/** Crouched walking speed, every direction (cm/s). Replaces the character's MaxWalkSpeedCrouched.
	 *  0 = keep the character's own value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float CrouchSpeed = 450.f;

	/** A crouch lasts at least this long (s), even on a quick tap of the key: the full crouch
	 *  movement plays before standing back up. Match it to the Lean component's CrouchDownDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float CrouchMinDuration = 0.2f;

	/** Time to stand back up from a plain crouch (s): the full movement plays (the crouch key can't
	 *  interrupt it) and the character can't run until it is over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float CrouchStandUpDuration = 0.2f;

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
	float SlideEndSpeed = 450.f;

	/** Crouch pressed in the air (after a jump): slide as soon as the character lands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	bool bSlideOnLanding = true;

	/** A dive roll ending on the ground always carries on into a slide: crouched if crouch is held at
	 *  the end of the roll, standing otherwise (no key needed).
	 *  Wall-bounce air roll: crouch still held on landing -> normal slide, otherwise none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
	bool bSlideAfterRoll = true;

	/** Slide after a dive roll: starts at the roll's speed and slows down steadily to SlideEndSpeed over
	 *  this distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 1, EditCondition = "bSlideAfterRoll"))
	float SlideAfterRollDistance = 325.f;

	/** Same, for the crouched slide after a roll (crouch held at the end of the roll) (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 1, EditCondition = "bSlideAfterRoll"))
	float SlideAfterRollCrouchedDistance = 450.f;

	/** Any slide: a movement key pushed against the slide direction brakes it steadily to a stop over
	 *  this distance (cm), then the character gets up. 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideBrakeDistance = 100.f;

	/** Slide after a side roll braked or cut short while still leaning: time to straighten up (s), eased. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float RecoveryReleaseDuration = 0.4f;

	/** Slide after a roll (done standing, the view rising from the roll's height to standing): the view
	 *  sways side to side to regain balance (roll around the line of sight, so the crosshair stays put),
	 *  dying down to straight by the end of the slide. Largest tilt (deg), 0 = off (current choice;
	 *  6 was tried). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0, ClampMax = 30, EditCondition = "bSlideAfterRoll"))
	float RecoverySwayAngle = 0.f;

	/** Number of full left-right sways over the slide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0.25, ClampMax = 5, EditCondition = "bSlideAfterRoll"))
	float RecoverySwayCycles = 1.5f;

	/** Smallest horizontal speed on landing for that slide (cm/s); slower, it is a normal landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0, EditCondition = "bSlideOnLanding"))
	float MinLandingSlideSpeed = 300.f;

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
	void StartSlide(float BaseSpeed = 0.f);
	/** Slide starting at StartSpeed and slowing down steadily (linear) to SlideEndSpeed over Distance. */
	void StartSlideFrom(float StartSpeed, float Distance, bool bCrouched = true);
	/** Shared slide start: sliding state, speed caps, crouch, locked steering. */
	void BeginSlideMovement(bool bCrouchDown = true);
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
	/** How far the current slide has turned from its start direction (degrees, positive = right). */
	float SlideTurnAngle = 0.f;
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
	/** Speed at the start of the current slide (peak, cm/s). */
	float SlideStartSpeed = 0.f;
	/** Speed where the first half of the distance ends and the second half starts (cm/s). */
	float SlideMidSpeed = 0.f;
	/** Time spent on the first half of the distance (s). */
	float SlideFirstHalfDuration = 0.f;
	/** Slowdown curve of the second half for the current slide (1 = linear). */
	float CurrentSecondHalfCurve = 4.f;
	bool bIsSliding = false;
	bool bCrouchWasHeld = false;
	bool bCrouchedByHold = false;
	/** Crouch pressed in the air: slide on landing. */
	bool bLandingSlideQueued = false;
	/** The queued landing slide comes from an air roll: only slide if crouch is still held on landing. */
	bool bLandingSlideNeedsHold = false;
	/** Current slide was started after a dive roll. */
	bool bSlideFromRoll = false;
	/** Was running when the character left the ground; falling last tick. */
	bool bSprintAtTakeoff = false;
	bool bWasFalling = false;
	/** That slide is done crouched (crouch held at the end of the roll). */
	bool bSlideFromRollCrouched = false;
	/** Roll end already turned into a slide. */
	double HandledRollEndTime = -100.0;
	/** Current side sway of the slide after a roll (deg, smoothed) and the side of its first lean. */
	float RecoverySwayRoll = 0.f;
	float RecoverySwaySign = 1.f;
	/** Side lean the slide after a roll starts on (the roll's end lean, deg), straightening by its end. */
	float RecoveryStartRoll = 0.f;
	/** Straightening up after the slide was braked / cut short while leaning: time so far (-1 = not
	 *  straightening) and the lean it started from. */
	float RecoveryReleaseElapsed = -1.f;
	float RecoveryReleaseFrom = 0.f;
	/** Braking to a stop (opposite key held). */
	bool bSlideBraking = false;
	float SlideBrakeElapsed = 0.f;
	float SlideBrakeDuration = 0.f;
	float SlideBrakeStartSpeed = 0.f;

	FVector SlideDirection = FVector::ForwardVector;
	float SlideElapsed = 0.f;
	float SlideDuration = 0.f;
	double LastSlideEndTime = -100.0;

	// Auto sprint: distance run since the sprint started, and when the last sprint ended.
	float SprintDistance = 0.f;
	double SprintStartTime = 0.0;
	/** Run interrupted by aiming: it resumes where it was (auto run time / distance not reset). */
	bool bSprintPausedByAim = false;
	float PausedSprintElapsed = 0.f;
	float PausedSprintDistance = 0.f;
	FVector LastSprintLocation = FVector::ZeroVector;
	double LastSprintEndTime = -100.0;
};
