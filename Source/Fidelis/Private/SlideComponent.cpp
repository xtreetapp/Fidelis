#include "SlideComponent.h"

#include "Fidelis.h"
#include "AK47Weapon.h"
#include "DiveComponent.h"
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

	// --- Sprint ---
	const float GroundSpeed = CMC->Velocity.Size2D();
	const bool bMoving = GroundSpeed > 10.f && !CMC->GetCurrentAcceleration().IsNearlyZero();
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
		SetSprinting(false, TEXT("visee"));
	}
	if (bWasAiming && !bAimingNow && bMoving)
	{
		bSprintPendingAfterSlide = true;
		SprintPendingSince = Now;
	}
	bWasAiming = bAimingNow;
	const bool bCanRun = bStillMoving && !bCrouchedByHold && !CMC->IsCrouching() && !bStandingUp && !bAimingNow;

	if (bIsSprinting)
	{
		SprintDistance += FVector::Dist2D(Char->GetActorLocation(), LastSprintLocation);
		LastSprintLocation = Char->GetActorLocation();
	}

	if (bIsSprinting && !bCanRun)
	{
		SetSprinting(false, TEXT("arret"));
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

	// --- Crouch pressed while running -> slide ---
	const bool bCooldownOver = GetWorld()->GetTimeSeconds() - LastSlideEndTime >= SlideCooldown;
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
		SprintDistance = 0.f;
		SprintStartTime = GetWorld()->GetTimeSeconds();
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

void USlideComponent::StartSlide()
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	SlideDirection = CMC->Velocity.GetSafeNormal2D();
	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = Char->GetActorForwardVector().GetSafeNormal2D();
	}

	// Relative to the running speed in the current direction (slower sideways / backward), set by
	// ApplyDirectionalSpeed this tick, so side slides start 10% faster than running sideways.
	SlideStartSpeed = CMC->MaxWalkSpeed * SlideSpeedMultiplier;

	// Linear speed decay from Start to End: distance = average speed * duration.
	const float AvgSpeed = FMath::Max((SlideStartSpeed + SlideEndSpeed) * 0.5f, 1.f);
	// Forward slide (within ~45 degrees of the view) goes further than a sideways / backward one.
	const FVector LocalDir = FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).UnrotateVector(SlideDirection);
	const float Distance = LocalDir.X >= 0.7f ? SlideDistance : SlideDistanceSideBack;
	SlideDuration = Distance / AvgSpeed;
	SlideElapsed = 0.f;

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
	Char->Crouch();

	// Steering is locked during the slide; looking around still works.
	if (AController* Controller = Char->GetController())
	{
		Controller->SetIgnoreMoveInput(true);
	}

	UE_LOG(LogFidelis, Log, TEXT("Slide: debut (vitesse %.0f -> %.0f, %.0f cm en %.2f s)."),
		SlideStartSpeed, SlideEndSpeed, Distance, SlideDuration);
}

void USlideComponent::TickSlide(float DeltaTime)
{
	UCharacterMovementComponent* CMC = Movement.Get();

	if (!CMC->IsMovingOnGround())
	{
		EndSlide(TEXT("plus au sol"));
		return;
	}

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

	const float Alpha = SlideElapsed / SlideDuration;
	const float Speed = FMath::Lerp(SlideStartSpeed, SlideEndSpeed, Alpha);

	// Left / right held: drift slightly sideways (the move input itself is locked during the slide,
	// so read the move action directly).
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
