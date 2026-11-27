#include "SlideComponent.h"

#include "Fidelis.h"
#include "AK47Weapon.h"
#include "DiveComponent.h"
#include "BunnyHopComponent.h"
#include "Engine/World.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

USlideComponent::USlideComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USlideComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!SprintAction)
	{
		SprintAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Sprint.IA_Sprint"));
	}
	if (!CrouchAction)
	{
		CrouchAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
	}
	if (!MoveAction)
	{
		MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	}

	Character = Cast<ACharacter>(GetOwner());
	if (ACharacter* Char = Character.Get())
	{
		if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			Movement = CMC;
			WalkSpeed = CMC->MaxWalkSpeed;
			SavedGroundFriction = CMC->GroundFriction;
			SavedBrakingDeceleration = CMC->BrakingDecelerationWalking;
			if (CrouchSpeed > 0.f)
			{
				// Before anything saves it (slide, dive), so they all restore this value.
				CMC->MaxWalkSpeedCrouched = CrouchSpeed;
			}
			CMC->GetNavAgentPropertiesRef().bCanCrouch = true;

			// Our slide velocity must be written before the movement component integrates it.
			CMC->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
		}
	}

	UE_LOG(LogFidelis, Log, TEXT("Slide: actif sur %s (marche %.0f, course %.0f, IA_Sprint=%s, IA_Crouch=%s)."),
		*GetNameSafe(GetOwner()), WalkSpeed, SprintSpeed, *GetNameSafe(SprintAction), *GetNameSafe(CrouchAction));
	if (!SprintAction || !CrouchAction)
	{
		UE_LOG(LogFidelis, Error, TEXT("Slide: IA_Sprint / IA_Crouch introuvables dans /Game/Input/Actions."));
	}
}

void USlideComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsSliding)
	{
		EndSlide(TEXT("fin de partie"));
	}
	if (UCharacterMovementComponent* CMC = Movement.Get())
	{
		CMC->MaxWalkSpeed = WalkSpeed;
	}
	Super::EndPlay(EndPlayReason);
}

bool USlideComponent::IsActionHeld(APlayerController* PC, const UInputAction* Action) const
{
	if (!PC || !Action)
	{
		return false;
	}
	if (const UEnhancedPlayerInput* PlayerInput = Cast<UEnhancedPlayerInput>(PC->PlayerInput))
	{
		return PlayerInput->GetActionValue(Action).Get<bool>();
	}
	return false;
}

bool USlideComponent::IsAiming() const
{
	const ACharacter* Char = Character.Get();
	if (!Char)
	{
		return false;
	}
	TArray<AActor*> Attached;
	Char->GetAttachedActors(Attached);
	for (const AActor* Actor : Attached)
	{
		if (const AAK47Weapon* Weapon = Cast<AAK47Weapon>(Actor); Weapon && Weapon->IsAiming())
		{
			return true;
		}
	}
	return false;
}

float USlideComponent::GetCrouchStandUpAlpha() const
{
	const UWorld* World = GetWorld();
	if (!World || CrouchStandUpDuration <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(float(World->GetTimeSeconds() - LastCrouchEndTime) / CrouchStandUpDuration, 0.f, 1.f);
}

float USlideComponent::GetStandUpAlpha() const
{
	const UWorld* World = GetWorld();
	if (!World || SlideStandUpDuration <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(float(World->GetTimeSeconds() - LastSlideEndTime) / SlideStandUpDuration, 0.f, 1.f);
}

void USlideComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Slide after a roll: side sway, smoothed so it never snaps when the slide is cut short.
	{
		const float Progress = GetSlideProgress();

		// Side sway: starts straight, leans one way, then the other, dying down to straight at the end.
		const float Envelope = 1.f - FMath::SmoothStep(0.f, 1.f, Progress);
		if (IsSlideFromRoll() && !bSlideBraking)
		{
			// Lean the roll ended on, straightening up by the end of the slide (eased), plus the optional sway.
			const float WantedSway = RecoveryStartRoll * Envelope
				+ (IsStandingSlideFromRoll()
					? RecoverySwaySign * RecoverySwayAngle * Envelope * FMath::Sin(2.f * PI * RecoverySwayCycles * Progress) : 0.f);
			RecoverySwayRoll = DeltaTime > 0.f ? FMath::FInterpTo(RecoverySwayRoll, WantedSway, DeltaTime, 20.f) : WantedSway;
			RecoveryReleaseElapsed = -1.f;
		}
		else if (RecoverySwayRoll != 0.f)
		{
			// Slide braked or cut short while still leaning: straighten up over RecoveryReleaseDuration,
			// eased, instead of snapping back level.
			if (RecoveryReleaseElapsed < 0.f)
			{
				RecoveryReleaseElapsed = 0.f;
				RecoveryReleaseFrom = RecoverySwayRoll;
			}
			RecoveryReleaseElapsed += DeltaTime;
			const float Alpha = RecoveryReleaseDuration > 0.f ? FMath::Clamp(RecoveryReleaseElapsed / RecoveryReleaseDuration, 0.f, 1.f) : 1.f;
			RecoverySwayRoll = RecoveryReleaseFrom * (1.f - FMath::SmoothStep(0.f, 1.f, Alpha));
			if (Alpha >= 1.f)
			{
				RecoverySwayRoll = 0.f;
				RecoveryReleaseElapsed = -1.f;
			}
		}
	}

	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	APlayerController* PC = Char ? Cast<APlayerController>(Char->GetController()) : nullptr;
	if (!Char || !CMC || !PC)
	{
		return;
	}

	const bool bSprintHeld = IsActionHeld(PC, SprintAction);
	bool bCrouchHeld = IsActionHeld(PC, CrouchAction);
	const bool bCrouchPressed = bCrouchHeld && !bCrouchWasHeld;
	bCrouchWasHeld = bCrouchHeld;

	if (bIsSliding)
	{
		// Crouch pressed again during the slide: stand up and run (EndSlide queues the run).
		if (bCrouchPressed)
		{
			EndSlide(TEXT("touche accroupir"));
			bCrouchIgnoredUntilRelease = true;
			return;
		}
		TickSlide(DeltaTime);
		return;
	}
	// The crouch key that cancelled the slide is still down: don't crouch again until it is released.
	if (bCrouchIgnoredUntilRelease)
	{
		if (bCrouchHeld)
		{
			bCrouchHeld = false;
		}
		else
		{
			bCrouchIgnoredUntilRelease = false;
		}
	}

	// Diving / lying down: the dive component owns the crouch state and movement settings.
	const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>();
	const bool bDiveBusy = Dive && Dive->IsBusy();
	if (bWasDiveBusy && !bDiveBusy && bSprintAfterSlide)
	{
		// Out of a dive (roll or getting up from lying down): run again as soon as the character is
		// back on its feet, like after a slide.
		bSprintPendingAfterSlide = true;
		SprintPendingSince = Dive->GetLastStandTime() + Dive->StandUpDuration;
	}
	bWasDiveBusy = bDiveBusy;
	if (bDiveBusy)
	{
		bSprintPendingAfterSlide = false;
		if (bIsSprinting)
		{
			// The dive already took over the movement settings: only record the end of the sprint.
			bIsSprinting = false;
			LastSprintEndTime = GetWorld()->GetTimeSeconds();
		}
		bCrouchedByHold = false;
		return;
	}

	// Running when leaving the ground (before aiming in the air cuts the run): the landing slide keeps
	// the running speeds.
	const bool bFallingNow = CMC->IsFalling();
	if (bFallingNow && !bWasFalling)
	{
		bSprintAtTakeoff = bIsSprinting;
	}
	bWasFalling = bFallingNow;

	// --- Sprint ---
	const float GroundSpeed = CMC->Velocity.Size2D();
	// In the air or climbing a ledge the movement keys are ignored (BunnyHopComponent): still moving.
	const bool bMoving = (GroundSpeed > 10.f && (!CMC->GetCurrentAcceleration().IsNearlyZero() || CMC->IsFalling()))
		|| CMC->IsFlying();
	// No running while still getting back up after lying down / rolling.
	const bool bStandingUp = (Dive && Dive->GetStandUpAlpha() < 1.f) || GetStandUpAlpha() < 1.f
		|| GetCrouchStandUpAlpha() < 1.f;
	const double Now = GetWorld()->GetTimeSeconds();
	// Quick direction changes (left / right strafing) pass through a near stop: keep the sprint
	// through short stops, only a real stop (longer than SprintStopGrace) ends it.
	if (bMoving)
	{
		LastMovingTime = Now;
	}
	const bool bStillMoving = bMoving || (bIsSprinting && Now - LastMovingTime <= SprintStopGrace);
	// No running while aiming down sights; the run comes back as soon as the aim is released.
	const bool bAimingNow = bNoSprintWhileAiming && IsAiming();
	if (bAimingNow && bIsSprinting)
	{
		// Aiming only pauses the run: it picks up again where it was once the aim is released.
		bSprintPausedByAim = true;
		PausedSprintElapsed = Now - SprintStartTime;
		PausedSprintDistance = SprintDistance;
		SetSprinting(false, TEXT("visee"));
		LastSprintEndTime = -100.0;
	}
	// Only a run the aim interrupted comes back (walking after the end of an auto run, aiming doesn't
	// start a new one).
	if (bWasAiming && !bAimingNow && bMoving && bSprintPausedByAim)
	{
		bSprintPendingAfterSlide = true;
		SprintPendingSince = Now;
	}
	bWasAiming = bAimingNow;
	// A real stop (or crouching) ends the paused run: the next one starts afresh.
	if (bSprintPausedByAim && (!bStillMoving || bCrouchedByHold || CMC->IsCrouching()))
	{
		bSprintPausedByAim = false;
	}
	const bool bCanRun = bStillMoving && !bCrouchedByHold && !CMC->IsCrouching() && !bStandingUp && !bAimingNow;

	if (bIsSprinting)
	{
		SprintDistance += FVector::Dist2D(Char->GetActorLocation(), LastSprintLocation);
		LastSprintLocation = Char->GetActorLocation();
	}

	if (bIsSprinting && !bCanRun)
	{
		SetSprinting(false, TEXT("arret"));
		// A stop (or aim, crouch...) isn't the end of a full auto run: no recharge, moving again runs
		// straight away. Only a run that lasted its full auto duration / distance waits AutoSprintRecharge.
		LastSprintEndTime = -100.0;
	}
	else if (bIsSprinting && bAutoSprint && !bSprintHeld && AutoSprintDuration > 0.f && Now - SprintStartTime >= AutoSprintDuration)
	{
		SetSprinting(false, TEXT("duree auto atteinte"));
	}
	else if (bIsSprinting && bAutoSprint && !bSprintHeld && AutoSprintDuration <= 0.f && SprintDistance >= AutoSprintDistance)
	{
		SetSprinting(false, TEXT("distance auto atteinte"));
	}
	else if (!bIsSprinting && bCanRun)
	{
		const bool bRecharged = Now - LastSprintEndTime >= AutoSprintRecharge;
		if (bSprintHeld || bSprintPendingAfterSlide || (bAutoSprint && bRecharged))
		{
			SetSprinting(true, bSprintHeld ? TEXT("touche") : bSprintPendingAfterSlide ? TEXT("fin de glissade") : TEXT("auto"));
		}
	}
	// The run after the slide waits for the stand-up and for the move input to come back (both lag a
	// frame or two behind the end of the slide), but only for a short while.
	if (bIsSprinting || Now - SprintPendingSince > 0.5)
	{
		bSprintPendingAfterSlide = false;
	}

	ApplyDirectionalSpeed();

	// --- Crouch pressed in the air -> slide on landing ---
	const bool bCooldownOver = GetWorld()->GetTimeSeconds() - LastSlideEndTime >= SlideCooldown;
	// --- Dive roll just ended on the ground -> standing slide, always (no key needed) ---
	if (bSlideAfterRoll && Dive && CMC->IsMovingOnGround() && Dive->GetLastRollEndTime() != HandledRollEndTime
		&& GetWorld()->GetTimeSeconds() - Dive->GetLastRollEndTime() < 0.2)
	{
		HandledRollEndTime = Dive->GetLastRollEndTime(); // once per roll
		UE_LOG(LogFidelis, Log, TEXT("Slide: fin de roulade -> glissade %s."), bCrouchHeld ? TEXT("accroupie") : TEXT("debout"));
		StartSlideFrom(Dive->RollEndSpeed, bCrouchHeld ? SlideAfterRollCrouchedDistance : SlideAfterRollDistance, bCrouchHeld);
		return;
	}
	if (bSlideOnLanding && bCrouchPressed && CMC->IsFalling())
	{
		// This press is the wall-bounce air roll: after the roll, slide only if crouch is still held.
		const UBunnyHopComponent* Hop = Char->FindComponentByClass<UBunnyHopComponent>();
		bLandingSlideNeedsHold = Hop && Hop->IsAirRollReadyOrActive();
		bLandingSlideQueued = !bLandingSlideNeedsHold || bSlideAfterRoll;
		UE_LOG(LogFidelis, Log, TEXT("Slide: accroupi en l'air -> glissade a l'atterrissage%s."),
			bLandingSlideNeedsHold ? TEXT(" si la touche reste maintenue (roulade)") : TEXT(""));
	}
	if (bLandingSlideQueued && CMC->IsMovingOnGround())
	{
		bLandingSlideQueued = false;
		if (bLandingSlideNeedsHold && !bCrouchHeld)
		{
			UE_LOG(LogFidelis, Log, TEXT("Slide: roulade terminee sans maintenir accroupi -> pas de glissade."));
		}
		else if (bCooldownOver && GroundSpeed >= MinLandingSlideSpeed)
		{
			UE_LOG(LogFidelis, Log, TEXT("Slide: atterrissage en glissade (%.0f cm/s%s)."), GroundSpeed,
				bSprintAtTakeoff ? TEXT(", vitesses de course") : TEXT(""));
			// Took off running: running speeds even if aiming in the air stopped the run.
			StartSlide(bSprintAtTakeoff ? SprintSpeed : 0.f);
			return;
		}
	}

	// --- Crouch pressed while running -> slide ---
	if (bCrouchPressed && bIsSprinting && CMC->IsMovingOnGround() && bCooldownOver
		&& GroundSpeed >= CMC->MaxWalkSpeed * MinSprintSpeedRatioToSlide)
	{
		StartSlide();
		return;
	}

	// --- Plain crouch (hold) ---
	//     Running but no slide possible (too slow, e.g. just starting to move sideways, or slide on
	//     cooldown): crouching wins over the run instead of doing nothing.
	// Not while still standing back up from the previous crouch: that movement plays in full.
	if (bCrouchHeld && !bCrouchedByHold && CMC->IsMovingOnGround() && GetCrouchStandUpAlpha() >= 1.f)
	{
		if (bIsSprinting)
		{
			SetSprinting(false, TEXT("accroupi"));
			bSprintPendingAfterSlide = false;
		}
		Char->Crouch();
		bCrouchedByHold = true;
		LastCrouchStartTime = GetWorld()->GetTimeSeconds();
	}
	// A quick tap still crouches all the way down before standing back up.
	else if (!bCrouchHeld && bCrouchedByHold && Now - LastCrouchStartTime >= CrouchMinDuration)
	{
		Char->UnCrouch();
		bCrouchedByHold = false;
		LastCrouchEndTime = Now;
		// Getting up from a crouch walk: run as soon as standing, like after a slide.
		if (bSprintAfterCrouch && bMoving)
		{
			bSprintPendingAfterSlide = true;
			SprintPendingSince = Now + CrouchStandUpDuration;
		}
	}
}

void USlideComponent::SetSprinting(bool bSprint, const TCHAR* Reason)
{
	if (bSprint == bIsSprinting)
	{
		return;
	}
	bIsSprinting = bSprint;
	if (UCharacterMovementComponent* CMC = Movement.Get())
	{
		CMC->MaxWalkSpeed = bSprint ? SprintSpeed : WalkSpeed;
	}
	if (bSprint)
	{
		// Back from aiming: carry on with the same run (time / distance), otherwise a new run.
		SprintDistance = bSprintPausedByAim ? PausedSprintDistance : 0.f;
		SprintStartTime = GetWorld()->GetTimeSeconds() - (bSprintPausedByAim ? PausedSprintElapsed : 0.f);
		bSprintPausedByAim = false;
		LastSprintLocation = GetOwner()->GetActorLocation();
	}
	else
	{
		LastSprintEndTime = GetWorld()->GetTimeSeconds();
	}
	UE_LOG(LogFidelis, Log, TEXT("Slide: course %s (%s, %.0f cm parcourus)."),
		bSprint ? TEXT("ON") : TEXT("OFF"), Reason, SprintDistance);
}

void USlideComponent::ApplyDirectionalSpeed()
{
	const ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	if (!Char || !CMC)
	{
		return;
	}

	// Aiming down sights (RMB on the AK-47): slower forward speeds, same direction multipliers.
	const bool bAiming = IsAiming();
	const float BaseSpeed = bIsSprinting
		? (bAiming ? AimSprintSpeed : SprintSpeed)
		: ((bAiming && AimWalkSpeed > 0.f) ? AimWalkSpeed : WalkSpeed);
	float Multiplier = 1.f;

	// Movement input direction relative to the view yaw: X = forward, Y = right (unit vector, X^2 + Y^2 = 1).
	const FVector Accel = CMC->GetCurrentAcceleration().GetSafeNormal2D();
	if (!Accel.IsNearlyZero())
	{
		const FRotator Yaw(0.f, Char->GetControlRotation().Yaw, 0.f);
		const FVector Local = Yaw.UnrotateVector(Accel);
		const float Backward = bIsSprinting ? SprintBackwardSpeedMultiplier : WalkBackwardSpeedMultiplier;
		const float Sideways = bIsSprinting ? SprintSidewaysSpeedMultiplier : WalkSidewaysSpeedMultiplier;
		const float FrontBack = Local.X >= 0.f ? 1.f : Backward;
		// Weighted by the squared components: pure directions get their exact multiplier, diagonals blend.
		Multiplier = FMath::Square(Local.X) * FrontBack + FMath::Square(Local.Y) * Sideways;
	}

	CMC->MaxWalkSpeed = BaseSpeed * Multiplier;
}

float USlideComponent::GetSlideSpeedAt(float Elapsed) const
{
	const float Curve = FMath::Max(SlideSpeedCurve, 0.1f);
	if (Elapsed < SlideFirstHalfDuration)
	{
		const float Progress = Elapsed / SlideFirstHalfDuration;
		return SlideStartSpeed - (SlideStartSpeed - SlideMidSpeed) * FMath::Pow(Progress, Curve);
	}
	const float SecondDuration = SlideDuration - SlideFirstHalfDuration;
	const float Progress = SecondDuration > 0.f ? FMath::Clamp((Elapsed - SlideFirstHalfDuration) / SecondDuration, 0.f, 1.f) : 1.f;
	return SlideMidSpeed - (SlideMidSpeed - SlideEndSpeed) * FMath::Pow(Progress, FMath::Max(CurrentSecondHalfCurve, 0.1f));
}

void USlideComponent::StartSlide(float BaseSpeed)
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	SlideDirection = CMC->Velocity.GetSafeNormal2D();
	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = Char->GetActorForwardVector().GetSafeNormal2D();
	}

	// Two halves of the distance, each with its own average speed relative to the running speed in the
	// current direction (set by ApplyDirectionalSpeed this tick). In each half the speed follows
	// v(p) = Start - (Start - End) * p^n over p = 0..1, whose average is Start - (Start - End) / (n + 1):
	// solve each half's start (peak) for its wanted average. Second half first: it ends at SlideEndSpeed;
	// the first half ends where the second one starts.
	const float Curve = FMath::Max(SlideSpeedCurve, 0.1f);
	// BaseSpeed given (landing slide after a running jump): use it instead of the current speed cap.
	const float Base = BaseSpeed > 0.f ? BaseSpeed : CMC->MaxWalkSpeed;
	const float Avg1 = FMath::Max(Base * SlideFirstHalfSpeedMultiplier, 1.f);
	const float Avg2 = FMath::Max(Base * SlideSecondHalfSpeedMultiplier, 1.f);
	const float Curve2 = FMath::Max(SlideSecondHalfSpeedCurve, 0.1f);
	SlideMidSpeed = FMath::Max(((Curve2 + 1.f) * Avg2 - SlideEndSpeed) / Curve2, Avg2);
	SlideStartSpeed = FMath::Max(((Curve + 1.f) * Avg1 - SlideMidSpeed) / Curve, Avg1);
	// Forward slide (within ~45 degrees of the view) goes further than a sideways / backward one.
	const FVector LocalDir = FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).UnrotateVector(SlideDirection);
	const float Distance = LocalDir.X >= 0.7f ? SlideDistance : SlideDistanceSideBack;
	// distance = average speed * duration, half the distance each.
	SlideFirstHalfDuration = Distance * 0.5f / Avg1;
	SlideDuration = SlideFirstHalfDuration + Distance * 0.5f / Avg2;
	CurrentSecondHalfCurve = Curve2;
	bSlideFromRoll = false;
	BeginSlideMovement();

	UE_LOG(LogFidelis, Log, TEXT("Slide: debut (vitesse %.0f -> %.0f a mi-distance -> %.0f, moyennes %.0f / %.0f, %.0f cm en %.2f s)."),
		SlideStartSpeed, SlideMidSpeed, SlideEndSpeed, Avg1, Avg2, Distance, SlideDuration);
}

void USlideComponent::StartSlideFrom(float StartSpeed, float Distance, bool bCrouched)
{
	const ACharacter* Char = Character.Get();
	const UCharacterMovementComponent* CMC = Movement.Get();
	SlideDirection = CMC->Velocity.GetSafeNormal2D();
	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = Char->GetActorForwardVector().GetSafeNormal2D();
	}
	// One steady slowdown: no first half, linear second half.
	SlideStartSpeed = FMath::Max(StartSpeed, SlideEndSpeed);
	SlideMidSpeed = SlideStartSpeed;
	SlideFirstHalfDuration = 0.f;
	CurrentSecondHalfCurve = 1.f;
	SlideDuration = FMath::Max(Distance, 1.f) / FMath::Max((SlideStartSpeed + SlideEndSpeed) * 0.5f, 1.f);
	bSlideFromRoll = true;
	bSlideFromRollCrouched = bCrouched;
	RecoveryStartRoll = 0.f; // StartSlideAfterRoll sets the roll's end lean right after
	// First sway towards the side the slide drifts to (slightly off the view), else to the right.
	{
		const float Side = FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).UnrotateVector(SlideDirection).Y;
		RecoverySwaySign = Side < -0.2f ? -1.f : 1.f;
	}
	// Crouched, or standing (straightening up along the slide).
	BeginSlideMovement(/*bCrouchDown*/ bCrouched);

	UE_LOG(LogFidelis, Log, TEXT("Slide: debut apres roulade (%.0f -> %.0f cm/s, %.0f cm en %.2f s)."),
		SlideStartSpeed, SlideEndSpeed, Distance, SlideDuration);
}

void USlideComponent::StartSlideAfterRoll(double RollEndTime, float Speed, float StartLeanRoll)
{
	if (!bSlideAfterRoll || bIsSliding || RollEndTime == HandledRollEndTime || !Character.IsValid() || !Movement.IsValid())
	{
		return;
	}
	HandledRollEndTime = RollEndTime;
	// Crouch held at the end of the roll -> crouched slide, otherwise standing.
	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	const bool bCrouched = PC && IsActionHeld(PC, CrouchAction);
	UE_LOG(LogFidelis, Log, TEXT("Slide: fin de roulade -> glissade %s."), bCrouched ? TEXT("accroupie") : TEXT("debout"));
	StartSlideFrom(Speed, bCrouched ? SlideAfterRollCrouchedDistance : SlideAfterRollDistance, bCrouched);
	// Carry on exactly from the lean the roll ended on (no smoothing delay at the hand-off).
	RecoveryStartRoll = StartLeanRoll;
	RecoverySwayRoll = StartLeanRoll;
	RecoveryReleaseElapsed = -1.f;
}

FQuat USlideComponent::GetRecoveryLeanViewRotation() const
{
	return RecoverySwayRoll != 0.f ? FRotator(0.f, 0.f, RecoverySwayRoll).Quaternion() : FQuat::Identity;
}

void USlideComponent::BeginSlideMovement(bool bCrouchDown)
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	SlideElapsed = 0.f;
	SlideTurnAngle = 0.f;
	bSlideBraking = false;

	bIsSliding = true;
	bSprintPendingAfterSlide = false;
	SlideStrafe = 0.f;
	SetSprinting(false, TEXT("glissade"));
	CMC->MaxWalkSpeed = SlideStartSpeed;
	// The character is crouched while sliding, so the crouched speed cap applies: lift it.
	SavedMaxWalkSpeedCrouched = CMC->MaxWalkSpeedCrouched;
	CMC->MaxWalkSpeedCrouched = SlideStartSpeed;
	CMC->GroundFriction = 0.f;
	CMC->BrakingDecelerationWalking = 0.f;
	if (bCrouchDown)
	{
		Char->Crouch();
		// Straight out of a roll the character is still crouched but the roll just asked to stand up:
		// Crouch() is ignored while crouched, so keep the crouch request on directly.
		CMC->bWantsToCrouch = true;
	}

	// Steering is locked during the slide; looking around still works.
	if (AController* Controller = Char->GetController())
	{
		Controller->SetIgnoreMoveInput(true);
	}

}

void USlideComponent::TickSlide(float DeltaTime)
{
	UCharacterMovementComponent* CMC = Movement.Get();

	if (!CMC->IsMovingOnGround())
	{
		EndSlide(TEXT("plus au sol"));
		return;
	}

	// A movement key pushed against the slide direction: brake to a stop over a short distance instead
	// of stopping dead (any slide).
	if (!bSlideBraking && SlideBrakeDistance > 0.f)
	{
		if (const ACharacter* Char = Character.Get())
		{
			const APlayerController* PC = Cast<APlayerController>(Char->GetController());
			if (const UEnhancedPlayerInput* PlayerInput = PC && MoveAction ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr)
			{
				const FVector2D Move = PlayerInput->GetActionValue(MoveAction).Get<FVector2D>();
				if (Move.SizeSquared() > 0.25f)
				{
					const FRotator YawRot(0.f, Char->GetControlRotation().Yaw, 0.f);
					const FVector WishDir = (YawRot.RotateVector(FVector::ForwardVector) * Move.Y
						+ YawRot.RotateVector(FVector::RightVector) * Move.X).GetSafeNormal2D();
					if (FVector::DotProduct(WishDir, SlideDirection) < -0.5f)
					{
						bSlideBraking = true;
						SlideBrakeElapsed = 0.f;
						SlideBrakeStartSpeed = FMath::Max(FVector::DotProduct(CMC->Velocity, SlideDirection), 1.f);
						// Steady slowdown to 0: distance = start speed / 2 * time.
						SlideBrakeDuration = 2.f * SlideBrakeDistance / SlideBrakeStartSpeed;
						UE_LOG(LogFidelis, Log, TEXT("Slide: touche opposee -> freinage (%.0f cm/s -> 0 sur %.0f cm, %.2f s)."),
							SlideBrakeStartSpeed, SlideBrakeDistance, SlideBrakeDuration);
					}
				}
			}
		}
	}

	float Speed = 0.f;
	if (bSlideBraking)
	{
		SlideBrakeElapsed += DeltaTime;
		if (SlideBrakeElapsed >= SlideBrakeDuration)
		{
			EndSlide(TEXT("freinage"));
			return;
		}
		Speed = SlideBrakeStartSpeed * (1.f - SlideBrakeElapsed / SlideBrakeDuration);
	}
	else
	{
		SlideElapsed += DeltaTime;
		if (SlideElapsed >= SlideDuration)
		{
			EndSlide(TEXT("distance atteinte"));
			return;
		}

		// Blocked by a wall: velocity collapsed after the previous move.
		if (SlideElapsed > 0.1f && CMC->Velocity.Size2D() < SlideEndSpeed * 0.5f)
		{
			EndSlide(TEXT("obstacle"));
			return;
		}
		Speed = GetSlideSpeedAt(SlideElapsed);
	}

	// Steering: a movement key to the side of the slide (left / right on a forward slide, forward /
	// back on a side slide...) turns it towards that key, up to SlideMaxTurnAngle from where it started.
	// The move input itself is locked during the slide, so read the move action directly.
	if (bSlideSteering && !bSlideBraking && SlideTurnRate > 0.f)
	{
		if (const ACharacter* Char = Character.Get())
		{
			const APlayerController* PC = Cast<APlayerController>(Char->GetController());
			if (const UEnhancedPlayerInput* PlayerInput = PC && MoveAction ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr)
			{
				const FVector2D Move = PlayerInput->GetActionValue(MoveAction).Get<FVector2D>();
				if (Move.SizeSquared() > 0.25f)
				{
					const FRotator YawRot(0.f, Char->GetControlRotation().Yaw, 0.f);
					const FVector WishDir = (YawRot.RotateVector(FVector::ForwardVector) * Move.Y
						+ YawRot.RotateVector(FVector::RightVector) * Move.X).GetSafeNormal2D();
					// Signed angle from the slide direction to the key's direction (positive = to the right).
					const float ToWish = FMath::RadiansToDegrees(FMath::Atan2(
						FVector::CrossProduct(SlideDirection, WishDir).Z, FVector::DotProduct(SlideDirection, WishDir)));
					const float Step = FMath::Clamp(ToWish, -SlideTurnRate * DeltaTime, SlideTurnRate * DeltaTime);
					const float NewTurn = FMath::Clamp(SlideTurnAngle + Step, -SlideMaxTurnAngle, SlideMaxTurnAngle);
					const float Applied = NewTurn - SlideTurnAngle;
					if (!FMath::IsNearlyZero(Applied))
					{
						SlideDirection = FRotator(0.f, Applied, 0.f).RotateVector(SlideDirection).GetSafeNormal2D();
						SlideTurnAngle = NewTurn;
					}
				}
			}
		}
	}

	// Left / right held: drift slightly sideways (only without steering).
	float StrafeInput = 0.f;
	FVector ViewRight = FVector::ZeroVector;
	if (const ACharacter* Char = Character.Get())
	{
		ViewRight = FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).RotateVector(FVector::RightVector);
		const APlayerController* PC = Cast<APlayerController>(Char->GetController());
		if (const UEnhancedPlayerInput* PlayerInput = PC && MoveAction ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr)
		{
			StrafeInput = FMath::Clamp(PlayerInput->GetActionValue(MoveAction).Get<FVector2D>().X, -1.f, 1.f);
		}
	}
	if (bSlideBraking || bSlideSteering)
	{
		StrafeInput = 0.f; // braking / steering: no sideways drift
	}
	SlideStrafe = FMath::FInterpTo(SlideStrafe, StrafeInput * SlideStrafeSpeed, DeltaTime, SlideStrafeInterpSpeed);
	CMC->Velocity = SlideDirection * Speed + ViewRight * SlideStrafe + FVector(0.f, 0.f, CMC->Velocity.Z);
}

void USlideComponent::EndSlide(const TCHAR* Reason)
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	bIsSliding = false;
	LastSlideEndTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (CMC)
	{
		CMC->GroundFriction = SavedGroundFriction;
		CMC->BrakingDecelerationWalking = SavedBrakingDeceleration;
		CMC->MaxWalkSpeed = WalkSpeed;
		CMC->MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
	}
	if (Char)
	{
		// Stand back up (the movement component keeps the character crouched if there is no headroom).
		Char->UnCrouch();
		bCrouchedByHold = false;
		if (AController* Controller = Char->GetController())
		{
			Controller->SetIgnoreMoveInput(false);
		}
		// Run again as soon as the character is back on its feet (the stand-up itself happens in the
		// movement component's update, not right here).
		bSprintPendingAfterSlide = bSprintAfterSlide;
		SprintPendingSince = LastSlideEndTime + SlideStandUpDuration;
	}

	UE_LOG(LogFidelis, Log, TEXT("Slide: fin (%s) apres %.2f s."), Reason, SlideElapsed);
}
