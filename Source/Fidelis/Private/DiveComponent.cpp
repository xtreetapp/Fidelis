#include "DiveComponent.h"

#include "Fidelis.h"
#include "DiveCameraModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "SlideComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

UDiveComponent::UDiveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDiveComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!DiveAction)
	{
		DiveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Dive.IA_Dive"));
	}
	if (!CrouchAction)
	{
		CrouchAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
	}

	Character = Cast<ACharacter>(GetOwner());
	if (ACharacter* Char = Character.Get())
	{
		if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			Movement = CMC;
			CMC->GetNavAgentPropertiesRef().bCanCrouch = true;
			CMC->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
		}
	}

	UE_LOG(LogFidelis, Log, TEXT("Dive: actif sur %s (IA_Dive=%s, IA_Crouch=%s pour la roulade)."),
		*GetNameSafe(GetOwner()), *GetNameSafe(DiveAction), *GetNameSafe(CrouchAction));
	if (!DiveAction)
	{
		UE_LOG(LogFidelis, Error, TEXT("Dive: IA_Dive introuvable dans /Game/Input/Actions."));
	}
}

void UDiveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (State != EFidelisDiveState::None)
	{
		RestoreMovementSettings();
		State = EFidelisDiveState::None;
	}
	if (BackLegsMesh)
	{
		BackLegsMesh->DestroyComponent();
		BackLegsMesh = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool UDiveComponent::IsActionHeld(APlayerController* PC, const UInputAction* Action) const
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

bool UDiveComponent::CanStartDive() const
{
	const ACharacter* Char = Character.Get();
	const UCharacterMovementComponent* CMC = Movement.Get();
	if (!Char || !CMC || !CMC->IsMovingOnGround() || CMC->IsCrouching())
	{
		return false;
	}
	if (GetWorld()->GetTimeSeconds() - LastStandTime < DiveCooldown)
	{
		return false;
	}
	if (CMC->Velocity.Size2D() < MinSpeedToDive)
	{
		return false;
	}

	const USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
	if (Slide && Slide->IsSliding())
	{
		return false;
	}
	if (bRequireSprint && !(Slide && Slide->IsSprinting()))
	{
		return false;
	}
	return true;
}

bool UDiveComponent::CanGoProneInPlace() const
{
	const ACharacter* Char = Character.Get();
	const UCharacterMovementComponent* CMC = Movement.Get();
	// No DiveCooldown here: lying down right after a roll / getting up must work straight away.
	if (!Char || !CMC || !CMC->IsMovingOnGround())
	{
		return false;
	}
	const USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
	return !(Slide && Slide->IsSliding());
}

void UDiveComponent::SaveMovementSettings()
{
	if (const UCharacterMovementComponent* CMC = Movement.Get())
	{
		SavedCrouchedHalfHeight = CMC->GetCrouchedHalfHeight();
		SavedMaxWalkSpeedCrouched = CMC->MaxWalkSpeedCrouched;
		SavedGroundFriction = CMC->GroundFriction;
		SavedBrakingDeceleration = CMC->BrakingDecelerationWalking;
		SavedAirControl = CMC->AirControl;
	}
}

void UDiveComponent::StartDive()
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	// Dive towards the current movement input (forward, sideways, backward...), relative to the camera
	// since IA_Move is applied in control space. Fall back to the current velocity, then to forward.
	FVector Dir = CMC->GetCurrentAcceleration().GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = CMC->Velocity.GetSafeNormal2D();
	}
	if (Dir.IsNearlyZero())
	{
		Dir = Char->GetActorForwardVector().GetSafeNormal2D();
	}

	SaveMovementSettings();

	CMC->AirControl = DiveAirControl;
	// Flat-ground flight time T = 2 * Vz / g  ->  Vz = g * T / 2.
	const float UpSpeed = DiveAirTime > 0.f ? FMath::Abs(CMC->GetGravityZ()) * DiveAirTime * 0.5f : DiveUpSpeed;
	float HorizontalSpeed = (DiveDistance > 0.f && DiveAirTime > 0.f) ? DiveDistance / DiveAirTime : DiveHorizontalSpeed;
	// Looking up: a bit more distance (up to +DiveLookUpDistanceBonus at DiveLookUpPitch), same air time.
	const float ViewPitch = FRotator::NormalizeAxis(Char->GetControlRotation().Pitch);
	const float LookUp = FMath::Clamp(ViewPitch / FMath::Max(DiveLookUpPitch, 0.1f), 0.f, 1.f);
	HorizontalSpeed *= 1.f + DiveLookUpDistanceBonus * LookUp;
	if (LookUp > 0.f)
	{
		UE_LOG(LogFidelis, Log, TEXT("Dive: regard vers le haut (%.1f deg) -> distance +%.0f%%."),
			ViewPitch, DiveLookUpDistanceBonus * LookUp * 100.f);
	}
	FVector LaunchVelocity = Dir * HorizontalSpeed + FVector::UpVector * UpSpeed;

	// Forward dive looking below the horizon: dive to where the player looks.
	const FRotator ViewRot(ViewPitch, Char->GetControlRotation().Yaw, 0.f);
	const FVector ViewFlat = FRotator(0.f, ViewRot.Yaw, 0.f).Vector();
	const APlayerController* PC = Cast<APlayerController>(Char->GetController());
	if (bDiveWhereLookingDown && ViewPitch < -2.f && FVector::DotProduct(Dir, ViewFlat) > 0.7f && PC && PC->PlayerCameraManager)
	{
		const UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
		const float Gravity = FMath::Max(FMath::Abs(CMC->GetGravityZ()), 1.f);
		const FVector Feet = Char->GetActorLocation() - FVector(0.f, 0.f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f);
		const FVector ViewStart = PC->PlayerCameraManager->GetCameraLocation();
		const FVector ViewDir = ViewRot.Vector();
		// Long trace: a floor seen far away (looking just under the horizon) must count as a hit, which
		// keeps the normal dive. Only a view with nothing under it (over a drop) dives along the view.
		const float TraceLength = 5000.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DiveAim), false, Char);
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, ViewStart, ViewStart + ViewDir * TraceLength, ECC_Visibility, Params);
		const float AimDistance = bHit ? FVector::Dist2D(Feet, Hit.ImpactPoint) : 0.f;
		if (bHit && AimDistance <= DiveDistance)
		{
			Dir = ViewFlat;
			// Land on the aimed point: keep the normal horizontal speed, solve the vertical launch speed
			// for the height difference: dz = Vz * t - g * t^2 / 2 with t = d / Vh.
			const float Distance = FMath::Max(AimDistance, DiveMinAimDistance);
			const float Time = Distance / FMath::Max(HorizontalSpeed, 1.f);
			const float DeltaZ = Hit.ImpactPoint.Z - Feet.Z;
			const float AimUpSpeed = (DeltaZ + 0.5f * Gravity * Time * Time) / FMath::Max(Time, 0.01f);
			LaunchVelocity = Dir * HorizontalSpeed + FVector::UpVector * AimUpSpeed;
			UE_LOG(LogFidelis, Log, TEXT("Dive: regard vers le bas (%.1f deg) -> atterrissage vise a %.0f cm (%.0f cm de denivele)."),
				ViewPitch, Distance, DeltaZ);
		}
		else if (!bHit)
		{
			// Nothing under the view (looking down off a ledge): head down along the view at dive speed.
			Dir = ViewFlat;
			LaunchVelocity = ViewDir * LaunchVelocity.Size();
			UE_LOG(LogFidelis, Log, TEXT("Dive: regard vers le bas (%.1f deg) -> plongeon le long de la vue."), ViewPitch);
		}
	}
	Char->LaunchCharacter(LaunchVelocity, /*bXYOverride*/ true, /*bZOverride*/ true);
	SetMoveLocked(true);

	State = EFidelisDiveState::Airborne;
	AirTime = 0.f;
	bRollQueued = false;
	DiveDirection = Dir;

	// Readable direction relative to where the player looks.
	const FRotator Yaw(0.f, Char->GetControlRotation().Yaw, 0.f);
	const FVector Local = Yaw.UnrotateVector(Dir);
	DiveDir = FMath::Abs(Local.X) >= FMath::Abs(Local.Y)
		? (Local.X >= 0.f ? EFidelisDiveDirection::Forward : EFidelisDiveDirection::Backward)
		: (Local.Y >= 0.f ? EFidelisDiveDirection::Right : EFidelisDiveDirection::Left);
	const TCHAR* DirName = DiveDir == EFidelisDiveDirection::Forward ? TEXT("avant")
		: DiveDir == EFidelisDiveDirection::Backward ? TEXT("arriere")
		: DiveDir == EFidelisDiveDirection::Right ? TEXT("droite") : TEXT("gauche");
	DiveDirectionName = DirName;
	UE_LOG(LogFidelis, Log, TEXT("Dive: plongeon vers %s (%s)."), DirName, *Dir.ToString());
}

void UDiveComponent::Land(bool bKeyHeld)
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	if (!bKeyHeld)
	{
		StandUp(TEXT("touche relachee en l'air"));
		return;
	}

	// Lie down: crouch with a prone-sized capsule (can't be shorter than its radius).
	const float Radius = Char->GetCapsuleComponent() ? Char->GetCapsuleComponent()->GetUnscaledCapsuleRadius() : ProneHalfHeight;
	CMC->SetCrouchedHalfHeight(FMath::Max(ProneHalfHeight, Radius));
	CMC->MaxWalkSpeedCrouched = ProneMoveSpeed;
	CMC->GroundFriction = LandingFriction;
	CMC->BrakingDecelerationWalking = LandingBraking;
	CMC->AirControl = SavedAirControl;
	Char->Crouch();

	// Allow crawling only if a crawl speed is set. Looking around and firing stay available.
	SetMoveLocked(ProneMoveSpeed <= 0.f);

	State = EFidelisDiveState::Prone;
	// Lying down: the body keeps the orientation it landed with, and the view turning away from it rolls it
	// over (back / flanks / belly). BackBodyYaw is where its feet point, set so the landing position is the
	// current one: feet in front on the back, behind on the belly, to the side on a flank.
	bProneFromBack = bProneBodyFollowsView || DiveDir == EFidelisDiveDirection::Backward;
	{
		const AController* Controller = Char->GetController();
		const float ViewYaw = Controller ? Controller->GetControlRotation().Yaw : Char->GetActorRotation().Yaw;
		switch (DiveDir)
		{
		case EFidelisDiveDirection::Backward:	BackBodyYaw = Char->GetActorRotation().Yaw; CurrentBodyRoll = 0.f; break;
		case EFidelisDiveDirection::Right:		BackBodyYaw = ViewYaw - 90.f; CurrentBodyRoll = 90.f; break;
		case EFidelisDiveDirection::Left:		BackBodyYaw = ViewYaw + 90.f; CurrentBodyRoll = -90.f; break;
		default:								BackBodyYaw = ViewYaw + 180.f; CurrentBodyRoll = 180.f; break;
		}
		BackBodyYaw = FRotator::NormalizeAxis(BackBodyYaw);
	}

	if (DiveDir == EFidelisDiveDirection::Backward)
	{
		// On the back: look down along the body so the feet are in view.
		if (AController* Controller = Char->GetController())
		{
			FRotator ControlRot = Controller->GetControlRotation();
			ControlRot.Pitch = BackLandingPitch;
			Controller->SetControlRotation(ControlRot);
		}
	}

	UE_LOG(LogFidelis, Log, TEXT("Dive: atterrissage, allonge %s (%.2f s en l'air)."),
		DiveDir == EFidelisDiveDirection::Backward ? TEXT("sur le dos")
		: (DiveDir == EFidelisDiveDirection::Left || DiveDir == EFidelisDiveDirection::Right) ? TEXT("sur le cote")
		: TEXT("sur le ventre"), AirTime);
}

float UDiveComponent::GetStandUpAlpha() const
{
	const UWorld* World = GetWorld();
	if (!World || StandUpDuration <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(float(World->GetTimeSeconds() - LastStandTime) / StandUpDuration, 0.f, 1.f);
}

void UDiveComponent::StandUp(const TCHAR* Reason)
{
	if (ACharacter* Char = Character.Get())
	{
		if (State == EFidelisDiveState::Prone)
		{
			// The movement component keeps the character down if there is no headroom.
			Char->UnCrouch();
		}
	}
	ProneSlideTimeLeft = 0.f;
	RestoreMovementSettings();
	State = EFidelisDiveState::None;
	bProneAutoStand = false;
	LastStandTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UE_LOG(LogFidelis, Log, TEXT("Dive: releve (%s)."), Reason);
}

void UDiveComponent::EndProneSlide(const TCHAR* Reason)
{
	ProneSlideTimeLeft = 0.f;
	// Back to the normal lying down: the leftover speed brakes down like after a dive's belly slide, then
	// crawling takes over.
	if (UCharacterMovementComponent* CMC = Movement.Get())
	{
		CMC->MaxWalkSpeedCrouched = ProneMoveSpeed;
		CMC->GroundFriction = LandingFriction;
		CMC->BrakingDecelerationWalking = LandingBraking;
	}
	SetMoveLocked(ProneMoveSpeed <= 0.f);
	UE_LOG(LogFidelis, Log, TEXT("Dive: fin de la glissade allongee (%s) apres %.2f s."), Reason, ProneSlideElapsed);
}

void UDiveComponent::RestoreMovementSettings()
{
	if (UCharacterMovementComponent* CMC = Movement.Get())
	{
		CMC->SetCrouchedHalfHeight(SavedCrouchedHalfHeight);
		CMC->MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
		CMC->GroundFriction = SavedGroundFriction;
		CMC->BrakingDecelerationWalking = SavedBrakingDeceleration;
		CMC->AirControl = SavedAirControl;
	}
	SetMoveLocked(false);
}

void UDiveComponent::SetMoveLocked(bool bLocked)
{
	if (bLocked == bMoveLocked)
	{
		return;
	}
	if (ACharacter* Char = Character.Get())
	{
		if (AController* Controller = Char->GetController())
		{
			Controller->SetIgnoreMoveInput(bLocked);
			bMoveLocked = bLocked;
		}
	}
}

void UDiveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	APlayerController* PC = Char ? Cast<APlayerController>(Char->GetController()) : nullptr;
	if (!Char || !CMC || !PC)
	{
		return;
	}

	const bool bHeld = IsActionHeld(PC, DiveAction);
	bool bPressed = bHeld && !bDiveWasHeld;
	// Slide after a roll: it plays out in full, only crouch or jump stop it (not the dive key).
	if (const USlideComponent* SlideAfterRoll = Char->FindComponentByClass<USlideComponent>();
		SlideAfterRoll && SlideAfterRoll->IsSlideFromRoll())
	{
		bPressed = false;
	}
	bDiveWasHeld = bHeld;


	const bool bCrouchHeld = IsActionHeld(PC, CrouchAction);
	const bool bCrouchPressed = bCrouchHeld && !bCrouchWasHeld;
	bCrouchWasHeld = bCrouchHeld;

	switch (State)
	{
	case EFidelisDiveState::None:
		if (USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
			bPressed && bProneInPlace && Slide && Slide->IsSliding() && CMC->IsMovingOnGround())
		{
			// Sliding, whatever the direction: drop flat right away, carrying the slide's momentum into
			// the belly slide.
			const float SlideTimeLeft = Slide->GetSlideTimeLeft();
			const FVector SlideVel2D(CMC->Velocity.X, CMC->Velocity.Y, 0.f);
			Slide->CancelSlide(TEXT("allonge"));
			// Still crouched from the slide: the movement component would keep the slide-sized capsule.
			// Stand it up now so the crouch below takes the prone size.
			CMC->UnCrouch(false);
			SaveMovementSettings();
			// Side slide: the body leans over into the lying position on the far flank (slide to the left
			// -> ends lying on the right flank, and the other way round). Forward / backward: on the belly.
			const FVector SlideLocal = FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).UnrotateVector(CMC->Velocity.GetSafeNormal2D());
			DiveDir = SlideLocal.Y < -0.5f ? EFidelisDiveDirection::Right
				: SlideLocal.Y > 0.5f ? EFidelisDiveDirection::Left
				: EFidelisDiveDirection::None;
			UE_LOG(LogFidelis, Log, TEXT("Dive: allonge depuis la glissade%s."),
				DiveDir == EFidelisDiveDirection::Right ? TEXT(" vers la gauche -> sur le flanc droit")
				: DiveDir == EFidelisDiveDirection::Left ? TEXT(" vers la droite -> sur le flanc gauche") : TEXT(""));
			Land(/*bKeyHeld*/ true);
			// Shrink to the prone capsule in the same frame (no standing frame in between).
			CMC->Crouch(false);

			// Side slide: the slide carries on lying down with the same speeds (no crawl-speed cap, no
			// friction), from the current speed down to the slide's end speed over the time it had left.
			const bool bSideSlide = DiveDir == EFidelisDiveDirection::Left || DiveDir == EFidelisDiveDirection::Right;
			if (bSideSlide && SlideTimeLeft > 0.f && !SlideVel2D.IsNearlyZero())
			{
				ProneSlideTimeLeft = SlideTimeLeft;
				ProneSlideElapsed = 0.f;
				ProneSlideStartSpeed = SlideVel2D.Size();
				ProneSlideFromElapsed = Slide->GetSlideElapsed();
				ProneSlideSource = Slide;
				ProneSlideEndSpeed = Slide->SlideEndSpeed;
				ProneSlideDir = SlideVel2D.GetSafeNormal();
				CMC->MaxWalkSpeedCrouched = FMath::Max(ProneSlideStartSpeed, ProneSlideEndSpeed);
				CMC->GroundFriction = 0.f;
				CMC->BrakingDecelerationWalking = 0.f;
				SetMoveLocked(true); // no steering during the slide, like the slide itself
				UE_LOG(LogFidelis, Log, TEXT("Dive: la glissade continue allonge (%.0f -> %.0f cm/s en %.2f s)."),
					ProneSlideStartSpeed, ProneSlideEndSpeed, ProneSlideTimeLeft);
			}
		}
		else if (bPressed && CanStartDive())
		{
			StartDive();
		}
		else if (bPressed && bProneInPlace && CanGoProneInPlace())
		{
			// Not running: lie down on the spot instead of diving.
			SaveMovementSettings();
			DiveDir = EFidelisDiveDirection::None;
			UE_LOG(LogFidelis, Log, TEXT("Dive: allonge sur place."));
			Land(/*bKeyHeld*/ true);
		}
		break;

	case EFidelisDiveState::Airborne:
		AirTime += DeltaTime;

		// Long dive (from a height): the air braking doesn't take the horizontal speed below
		// DiveMinAirSpeed. A sudden drop below half of it is a wall: leave that alone.
		if (DiveMinAirSpeed > 0.f && CMC->IsFalling())
		{
			const float Speed2D = CMC->Velocity.Size2D();
			if (Speed2D < DiveMinAirSpeed && Speed2D > DiveMinAirSpeed * 0.5f)
			{
				const FVector Dir2D = CMC->Velocity.GetSafeNormal2D();
				CMC->Velocity = Dir2D * DiveMinAirSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
			}
		}

		// Dive key then crouch key -> roll right away, even in the air: the body tucks in and turns
		// now, and the roll carries on along the ground after landing.
		if (bEnableRoll && bCrouchPressed && (RollComboWindow <= 0.f || AirTime <= RollComboWindow))
		{
			UE_LOG(LogFidelis, Log, TEXT("Dive: combo plongeon + baisse -> roulade immediate vers %s."), DiveDirectionName);
			StartRoll();
			break;
		}

		// Give the launch a moment to leave the ground before checking for landing.
		if (AirTime > 0.1f && CMC->IsMovingOnGround())
		{
			if (bRollQueued)
			{
				StartRoll();
			}
			else
			{
				// The dive always ends fully lying down. Key released: back up on its own after a moment.
				Land(bHeld || bAlwaysLandProne);
				bProneAutoStand = !bHeld && State == EFidelisDiveState::Prone;
				ProneAutoStandTime = -1.0;
				ProneLandTime = GetWorld()->GetTimeSeconds();
			}
		}
		break;

	case EFidelisDiveState::Rolling:
		// Dive key pressed during the roll (on the ground): lie down right away, keeping the momentum.
		if (bPressed && CMC->IsMovingOnGround())
		{
			ProneFromRoll(TEXT("touche pendant la roulade"));
			break;
		}
		// Side roll: end leaning to the other side (dive's view tilt) if a slide follows (standing or
		// crouched), i.e. the dive key is not held. Smoothed so a key change never snaps the view.
		{
			const bool bSideRoll = DiveDir == EFidelisDiveDirection::Left || DiveDir == EFidelisDiveDirection::Right;
			const float RollSide = DiveDir == EFidelisDiveDirection::Right ? 1.f : -1.f;
			const float WantedLean = (bRollEndsLeaning && bSideRoll && !bHeld) ? -RollSide * SideDiveRollAngle : 0.f;
			RollEndLean = FMath::FInterpTo(RollEndLean, WantedLean, DeltaTime, 10.f);
		}
		TickRoll(DeltaTime);
		// Roll over with the dive key held: stay down instead of standing up.
		if (State == EFidelisDiveState::None && bHeld && bRollEndedOnGround)
		{
			ProneFromRoll(TEXT("touche tenue en fin de roulade"));
		}
		// Roll over on the ground: the standing slide takes over in this same frame.
		else if (State == EFidelisDiveState::None && bRollEndedOnGround)
		{
			if (USlideComponent* AfterRollSlide = Char->FindComponentByClass<USlideComponent>())
			{
				// The slide starts on the lean the roll ended on (view identity from the roll from now on).
				const bool bSideRoll = DiveDir == EFidelisDiveDirection::Left || DiveDir == EFidelisDiveDirection::Right;
				AfterRollSlide->StartSlideAfterRoll(LastRollEndTime, RollEndSpeed, bSideRoll && bRollTurnsView ? RollEndLean : 0.f);
			}
		}
		break;

	case EFidelisDiveState::Prone:
		if (bProneAutoStand)
		{
			if (bHeld)
			{
				bProneAutoStand = false; // key held again: stays down until it is released
			}
			else
			{
				// Get up once the belly slide is over (crawl speed) and the body has lain still a moment;
				// standing up mid-slide would stop it dead. Safety net: never more than 2 s down.
				const double Now = GetWorld()->GetTimeSeconds();
				if (ProneAutoStandTime < 0.0 && CMC->Velocity.Size2D() <= ProneMoveSpeed + 10.f)
				{
					ProneAutoStandTime = Now + ProneAfterDiveTime;
				}
				if ((ProneAutoStandTime >= 0.0 && Now >= ProneAutoStandTime) || Now - ProneLandTime > 2.0)
				{
					StandUp(TEXT("fin du plongeon"));
					break;
				}
			}
		}
		else if (!bHeld)
		{
			StandUp(TEXT("touche relachee"));
			break;
		}
		// Side slide carrying on lying down: same linear speed decay as the slide.
		if (ProneSlideTimeLeft > 0.f)
		{
			ProneSlideElapsed += DeltaTime;
			if (ProneSlideElapsed >= ProneSlideTimeLeft)
			{
				EndProneSlide(TEXT("distance atteinte"));
			}
			else if (ProneSlideElapsed > 0.1f && CMC->Velocity.Size2D() < ProneSlideEndSpeed * 0.5f)
			{
				EndProneSlide(TEXT("obstacle"));
			}
			else
			{
				// Same speed curve as the slide it comes from (peak at the start, slows down at the end).
				const USlideComponent* Source = ProneSlideSource.Get();
				const float Speed = Source ? Source->GetSlideSpeedAt(ProneSlideFromElapsed + ProneSlideElapsed)
					: FMath::Lerp(ProneSlideStartSpeed, ProneSlideEndSpeed, ProneSlideElapsed / ProneSlideTimeLeft);
				CMC->Velocity = ProneSlideDir * Speed + FVector(0.f, 0.f, CMC->Velocity.Z);
			}
			break;
		}
		// Once the belly slide has slowed down to crawl speed, give back the normal ground grip so
		// crawling responds immediately in every direction (no ice-skating when changing direction).
		if (ProneMoveSpeed > 0.f && CMC->GroundFriction != SavedGroundFriction
			&& CMC->Velocity.Size2D() <= ProneMoveSpeed + 10.f)
		{
			CMC->GroundFriction = SavedGroundFriction;
			CMC->BrakingDecelerationWalking = SavedBrakingDeceleration;
			UE_LOG(LogFidelis, Log, TEXT("Dive: glissade terminee, rampe possible dans toutes les directions (%.0f cm/s)."), ProneMoveSpeed);
		}
		break;
	}

	// The camera modifier turns the view during rolls (added once the camera manager exists).
	if (PC->PlayerCameraManager && !PC->PlayerCameraManager->FindCameraModifierByClass(UDiveCameraModifier::StaticClass()))
	{
		PC->PlayerCameraManager->AddNewCameraModifier(UDiveCameraModifier::StaticClass());
	}

	UpdateDiveView(DeltaTime);
}

FQuat UDiveComponent::GetRollViewRotation() const
{
	if (!bRollTurnsView || State != EFidelisDiveState::Rolling || RollDuration <= 0.f || DiveDir == EFidelisDiveDirection::None)
	{
		return FQuat::Identity;
	}
	// Eased so the turn starts and ends gently, and ends exactly level when the roll ends.
	const float T = FMath::Clamp(RollElapsed / RollDuration, 0.f, 1.f);
	// Turning speed rises steadily to its peak at RollViewPeakTime (fraction of the roll), then falls
	// steadily to 0 at the end (triangle speed profile): early peak = fast spin right from the start.
	const float Peak = FMath::Clamp(RollViewPeakTime, 0.01f, 0.99f);
	const float Ease = T < Peak ? T * T / Peak : 1.f - FMath::Square(1.f - T) / (1.f - Peak);
	const float Angle = 360.f * RollViewTurns * Ease;
	switch (DiveDir)
	{
	case EFidelisDiveDirection::Forward:	return FRotator(-Angle, 0.f, 0.f).Quaternion();	// somersault: view goes down first
	case EFidelisDiveDirection::Backward:	return FRotator(Angle, 0.f, 0.f).Quaternion();	// backward roll: view goes up first
	// Barrel rolls: end on RollEndLean (a little short of a full turn) when the standing slide follows.
	case EFidelisDiveDirection::Right:		return FRotator(0.f, 0.f, Angle + RollEndLean * Ease).Quaternion();	// barrel roll to the right
	case EFidelisDiveDirection::Left:		return FRotator(0.f, 0.f, -Angle + RollEndLean * Ease).Quaternion();
	default:								return FQuat::Identity;
	}
}

void UDiveComponent::UpdateDiveView(float DeltaTime)
{
	// Lying on the belly or the side: crawling sideways turns the body (view and rifle tilt) to the
	// crawl side. Not after a backward dive (that one turns over with the view instead).
	if (State == EFidelisDiveState::Prone && DiveDir != EFidelisDiveDirection::Backward && !bProneFromBack)
	{
		if (const ACharacter* CrawlChar = Character.Get())
		{
			const UCharacterMovementComponent* CrawlMove = Movement.Get();
			const FVector Accel = CrawlMove ? CrawlMove->GetCurrentAcceleration().GetSafeNormal2D() : FVector::ZeroVector;
			const FVector LocalAccel = FRotator(0.f, CrawlChar->GetControlRotation().Yaw, 0.f).UnrotateVector(Accel);
			const float Side = LocalAccel.Y;
			// Sideways -> on that side; straight ahead -> back on the belly (view level again).
			const EFidelisDiveDirection CrawlSide = Side > 0.5f ? EFidelisDiveDirection::Right
				: Side < -0.5f ? EFidelisDiveDirection::Left
				: LocalAccel.X > 0.8f ? EFidelisDiveDirection::Forward : DiveDir;
			if (CrawlSide != DiveDir)
			{
				DiveDir = CrawlSide;
				// Roll over smoothly: from the current tilt, through level, to the other side.
				SideSwitchElapsed = 0.f;
				SideSwitchStartRoll = CurrentViewRoll;
				SideSwitchStartCant = CurrentWeaponCant;
				UE_LOG(LogFidelis, Log, TEXT("Dive: rampe -> allonge %s."),
					CrawlSide == EFidelisDiveDirection::Right ? TEXT("sur le cote droit")
					: CrawlSide == EFidelisDiveDirection::Left ? TEXT("sur le cote gauche") : TEXT("sur le ventre"));
			}
		}
	}

	const bool bSide = DiveDir == EFidelisDiveDirection::Left || DiveDir == EFidelisDiveDirection::Right;
	const float SideSign = DiveDir == EFidelisDiveDirection::Right ? 1.f : -1.f;

	// Side dive: the view tilts towards the dive side in the air, and stays partly tilted lying on the side.
	float TargetRoll = 0.f;
	if (bSide && State == EFidelisDiveState::Airborne)
	{
		TargetRoll = SideSign * SideDiveRollAngle;
	}
	else if (bSide && State == EFidelisDiveState::Prone)
	{
		TargetRoll = SideSign * (bSideProneViewFollowsWeapon ? SideProneWeaponCant : SideProneRollAngle);
	}
	// Lying on the side: the rifle leans over to the dive side with the body.
	//   In the air the rifle already tilts like the view, so landing carries on smoothly from there.
	const float TargetCant = !bSide ? 0.f
		: State == EFidelisDiveState::Prone ? SideSign * SideProneWeaponCant
		: State == EFidelisDiveState::Airborne ? SideSign * SideDiveRollAngle : 0.f;

	// Rolling over to the other side while crawling: eased over SideSwitchDuration (slow start, level
	// halfway, slow end) instead of the snappy follow that would flip the view almost at once.
	if (SideSwitchElapsed >= 0.f && State == EFidelisDiveState::Prone && SideSwitchDuration > 0.f)
	{
		SideSwitchElapsed += DeltaTime;
		const float T = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(SideSwitchElapsed / SideSwitchDuration, 0.f, 1.f));
		CurrentViewRoll = FMath::Lerp(SideSwitchStartRoll, TargetRoll, T);
		CurrentWeaponCant = FMath::Lerp(SideSwitchStartCant, TargetCant, T);
		if (SideSwitchElapsed >= SideSwitchDuration)
		{
			SideSwitchElapsed = -1.f;
		}
	}
	else
	{
		SideSwitchElapsed = -1.f;
		CurrentViewRoll = FMath::FInterpTo(CurrentViewRoll, TargetRoll, DeltaTime, ViewRollInterpSpeed);
		if (FMath::Abs(CurrentViewRoll - TargetRoll) < 0.05f)
		{
			CurrentViewRoll = TargetRoll;
		}
		CurrentWeaponCant = FMath::FInterpTo(CurrentWeaponCant, TargetCant, DeltaTime, ViewRollInterpSpeed);
		if (FMath::Abs(CurrentWeaponCant - TargetCant) < 0.05f)
		{
			CurrentWeaponCant = TargetCant;
		}
	}

	// Forward dive: hands thrown out in the air, brought back once on the ground.
	const bool bReach = DiveDir == EFidelisDiveDirection::Forward && State == EFidelisDiveState::Airborne;
	HandsReachAlpha = FMath::FInterpTo(HandsReachAlpha, bReach ? 1.f : 0.f, DeltaTime, bReach ? HandsReachInSpeed : HandsReachOutSpeed);
	if (!bReach && HandsReachAlpha < 0.005f)
	{
		HandsReachAlpha = 0.f;
	}

	// After a backward dive: the body stays put while the view turns, and rolls around its length to
	// follow it: on the back looking at the feet, on the left / right flank looking 90 degrees to that
	// side (view and rifle tilted that way), on the belly looking away from the feet.
	//    A few degrees of margin around each limit so it doesn't flicker right on it.
	if (State == EFidelisDiveState::Prone && bProneFromBack)
	{
		const ACharacter* Char = Character.Get();
		const AController* Controller = Char ? Char->GetController() : nullptr;
		const float ViewYaw = Controller ? Controller->GetControlRotation().Yaw : BackBodyYaw;
		// On the back, moving towards the feet (where the view looks): turn over onto the belly to crawl
		// that way, the feet going back behind.
		if (DiveDir == EFidelisDiveDirection::Backward)
		{
			const UCharacterMovementComponent* CrawlMove = Movement.Get();
			const FVector Accel = CrawlMove ? CrawlMove->GetCurrentAcceleration().GetSafeNormal2D() : FVector::ZeroVector;
			if (FVector::DotProduct(Accel, FRotator(0.f, ViewYaw, 0.f).Vector()) > 0.5f)
			{
				// Side the legs sweep round to behind:
				//  1) the side the move leans to (diagonal forward-left / forward-right),
				//  2) else the side the feet already are on in the view (view turned right of the feet ->
				//     feet on the left -> they go round by the left, the short way),
				//  3) else (looking straight at them) the other side from last time.
				const float SideMove = FRotator(0.f, ViewYaw, 0.f).UnrotateVector(Accel).Y;
				const float FeetTurn = FMath::FindDeltaAngleDegrees(BackBodyYaw, ViewYaw);
				TurnOverSign = SideMove > 0.1f ? 1.f
					: SideMove < -0.1f ? -1.f
					: FeetTurn > 2.f ? -1.f
					: FeetTurn < -2.f ? 1.f
					: -TurnOverSign;
				TurnOverStartYaw = BackBodyYaw;
				TurnOverElapsed = TurnOverToCrawlDuration > 0.f ? 0.f : -1.f;
				BackBodyYaw = FRotator::NormalizeAxis(ViewYaw + 180.f);
				UE_LOG(LogFidelis, Log, TEXT("Dive: avance vers les pieds -> se retourne pour ramper."));
			}
		}
		// Negative = view turned left of the feet.
		const float Turn = FMath::FindDeltaAngleDegrees(BackBodyYaw, ViewYaw);
		const float Away = FMath::Abs(Turn);
		constexpr float Margin = 5.f;
		const bool bOnBack = DiveDir == EFidelisDiveDirection::Backward;
		const bool bOnBelly = DiveDir == EFidelisDiveDirection::Forward;
		const bool bBellyAllowed = BackToBellyTurnAngle > 0.f;
		const float SideAngle = BackToSideTurnAngle > 0.f ? BackToSideTurnAngle : 1000.f;
		const float BellyAngle = bBellyAllowed ? BackToBellyTurnAngle : 1000.f;

		EFidelisDiveDirection Wanted;
		if (Away >= BellyAngle + (bOnBelly ? -Margin : Margin))
		{
			Wanted = EFidelisDiveDirection::Forward;
		}
		else if (Away >= SideAngle + (bOnBack ? Margin : -Margin))
		{
			Wanted = Turn < 0.f ? EFidelisDiveDirection::Left : EFidelisDiveDirection::Right;
		}
		else
		{
			Wanted = EFidelisDiveDirection::Backward;
		}
		if (Wanted != DiveDir)
		{
			DiveDir = Wanted;
			// Roll over smoothly from the current tilt (same easing as rolling over while crawling).
			SideSwitchElapsed = 0.f;
			SideSwitchStartRoll = CurrentViewRoll;
			SideSwitchStartCant = CurrentWeaponCant;
			UE_LOG(LogFidelis, Log, TEXT("Dive: vue a %.0f deg des pieds -> allonge %s."), Turn,
				Wanted == EFidelisDiveDirection::Backward ? TEXT("sur le dos")
				: Wanted == EFidelisDiveDirection::Left ? TEXT("sur le flanc gauche")
				: Wanted == EFidelisDiveDirection::Right ? TEXT("sur le flanc droit") : TEXT("sur le ventre"));
		}

		// Body roll for the legs: towards the side the view turned (belly: on from the flank it came from,
		// so it never spins back through the back).
		const float TargetBodyRoll = DiveDir == EFidelisDiveDirection::Backward ? 0.f
			: DiveDir == EFidelisDiveDirection::Left ? -90.f
			: DiveDir == EFidelisDiveDirection::Right ? 90.f
			: (CurrentBodyRoll < 0.f ? -180.f : 180.f);
		CurrentBodyRoll = FMath::FInterpTo(CurrentBodyRoll, TargetBodyRoll, DeltaTime, ViewRollInterpSpeed);

		if (TurnOverElapsed >= 0.f)
		{
			TurnOverElapsed += DeltaTime;
			if (TurnOverElapsed >= TurnOverToCrawlDuration || DiveDir != EFidelisDiveDirection::Forward)
			{
				TurnOverElapsed = -1.f;
				CurrentBodyRoll = TurnOverSign * 180.f;
			}
		}
	}
	else
	{
		CurrentBodyRoll = 0.f;
		TurnOverElapsed = -1.f;
	}

	UpdateBackLegs();
}

void UDiveComponent::UpdateBackLegs()
{
	ACharacter* Char = Character.Get();
	// On the back, and on the flanks it rolls onto by turning the view (not on the belly: behind the view).
	// Also while turning over from the back to crawl: the legs sweep round to behind.
	const bool bTurningOver = TurnOverElapsed >= 0.f && TurnOverToCrawlDuration > 0.f;
	const bool bShow = bShowLegsOnBack && Char && State == EFidelisDiveState::Prone && bProneFromBack
		&& (DiveDir != EFidelisDiveDirection::Forward || bTurningOver);
	if (!bShow)
	{
		if (BackLegsMesh && BackLegsMesh->IsVisible())
		{
			BackLegsMesh->SetVisibility(false);
		}
		return;
	}

	// Lazily build a legs-only copy of the body mesh, seen only by the player (the body mesh itself
	// is hidden from its owner in first person).
	if (!BackLegsMesh)
	{
		const USkeletalMeshComponent* Body = Char->GetMesh();
		USkeletalMesh* Asset = Body ? Body->GetSkeletalMeshAsset() : nullptr;
		if (!Asset)
		{
			return;
		}
		BackLegsMesh = NewObject<USkeletalMeshComponent>(Char, TEXT("DiveBackLegs"));
		BackLegsMesh->SetupAttachment(Char->GetRootComponent());
		BackLegsMesh->SetSkeletalMesh(Asset);
		for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
		{
			BackLegsMesh->SetMaterial(i, Body->GetMaterial(i));
		}
		BackLegsMesh->SetRelativeScale3D(Body->GetRelativeScale3D());
		BackLegsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BackLegsMesh->SetCastShadow(false);
		BackLegsMesh->SetOnlyOwnerSee(true);
		BackLegsMesh->RegisterComponent();
		if (BackLegsMesh->GetBoneIndex(LegsHiddenBone) != INDEX_NONE)
		{
			BackLegsMesh->HideBoneByName(LegsHiddenBone, PBO_None);
		}
		else
		{
			UE_LOG(LogFidelis, Warning, TEXT("Dive: os '%s' introuvable sur %s, le corps entier est affiche sur le dos."),
				*LegsHiddenBone.ToString(), *GetNameSafe(Asset));
		}
		UE_LOG(LogFidelis, Log, TEXT("Dive: jambes visibles sur le dos (%s)."), *GetNameSafe(Asset));
	}

	// Lying on the back, in actor space: head towards -X (behind the view), feet towards +X (in front),
	// chest up. Mesh space: +Z = head, +Y = chest, root = soles of the feet.
	//   The body frame keeps the yaw of the landing (BackBodyYaw): turning the view doesn't drag the legs.
	const UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 34.f;
	// Turning over to crawl: the body pivots on the ground (feet sweeping from in front to behind, on the
	// TurnOverSign side) while rolling from the back onto the belly. Eased: slow start, slow end.
	float LegsYaw = BackBodyYaw;
	float LegsRoll = CurrentBodyRoll;
	if (bTurningOver)
	{
		const float E = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(TurnOverElapsed / TurnOverToCrawlDuration, 0.f, 1.f));
		LegsYaw = TurnOverStartYaw + TurnOverSign * 180.f * E;
		LegsRoll = TurnOverSign * 180.f * E;
	}
	const FTransform BodyXf(FRotator(0.f, LegsYaw, 0.f), Char->GetActorLocation());
	float EyeX = 0.f;
	if (const APlayerController* PC = Cast<APlayerController>(Char->GetController()); PC && PC->PlayerCameraManager)
	{
		EyeX = BodyXf.InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation()).X;
	}
	const FQuat LyingRot = FRotationMatrix::MakeFromYZ(FVector::UpVector, -FVector::ForwardVector).ToQuat();
	// On a flank: the body rolled around its length (CurrentBodyRoll -90 = left side down, chest towards
	// the left, where the view turned). Chest is +Z here; a quaternion angle of +90 about X takes it to -Y
	// (left), hence the minus.
	const FQuat BodyRollRot(FVector::ForwardVector, FMath::DegreesToRadians(-LegsRoll));
	BackLegsMesh->SetWorldLocationAndRotation(
		BodyXf.TransformPosition(FVector(EyeX + LegsDistanceFromEye, 0.f, -HalfHeight + LegsHeightAboveGround)),
		BodyXf.GetRotation() * BodyRollRot * LyingRot);
	if (!BackLegsMesh->IsVisible())
	{
		BackLegsMesh->SetVisibility(true);
	}
}

void UDiveComponent::StartRoll()
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	const float Radius = Char->GetCapsuleComponent() ? Char->GetCapsuleComponent()->GetUnscaledCapsuleRadius() : RollHalfHeight;
	CMC->SetCrouchedHalfHeight(FMath::Max(RollHalfHeight, Radius));
	// Carry the landing speed into the roll so touching the ground doesn't brake the character.
	const float LandingSpeed = CMC->Velocity.Size2D();
	RollSpeedAtStart = bRollKeepsLandingSpeed ? FMath::Max(RollStartSpeed, LandingSpeed) : RollStartSpeed;

	CMC->MaxWalkSpeedCrouched = RollSpeedAtStart;
	CMC->GroundFriction = 0.f;
	CMC->BrakingDecelerationWalking = 0.f;
	// Started in the air: the roll steers the trajectory itself until landing.
	CMC->AirControl = CMC->IsMovingOnGround() ? SavedAirControl : 0.f;
	Char->Crouch();

	// Linear speed decay: distance = average speed * duration.
	const float AvgSpeed = FMath::Max((RollSpeedAtStart + RollEndSpeed) * 0.5f, 1.f);
	RollDuration = RollTime > 0.f ? RollTime : RollDistance / AvgSpeed;
	RollElapsed = 0.f;
	bRollQueued = false;
	RollBaseDirection = DiveDirection;
	RollEndLean = 0.f;
	RollStartYaw = Char->GetControlRotation().Yaw;
	State = EFidelisDiveState::Rolling;

	UE_LOG(LogFidelis, Log, TEXT("Dive: roulade vers %s (%.0f cm en %.2f s, %.0f -> %.0f cm/s, %.2f s en l'air)."),
		DiveDirectionName, AvgSpeed * RollDuration, RollDuration, RollSpeedAtStart, RollEndSpeed, AirTime);
}

void UDiveComponent::ProneFromRoll(const TCHAR* Reason)
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	if (!Char || !CMC)
	{
		return;
	}
	// Still crouched at the roll size: stand the capsule up first so the crouch takes the prone size,
	// then shrink it in the same frame.
	CMC->UnCrouch(false);
	UE_LOG(LogFidelis, Log, TEXT("Dive: allonge apres la roulade (%s)."), Reason);
	Land(/*bKeyHeld*/ true);
	CMC->Crouch(false);
}

void UDiveComponent::TickRoll(float DeltaTime)
{
	bRollEndedOnGround = false;
	UCharacterMovementComponent* CMC = Movement.Get();
	const bool bOnGround = CMC->IsMovingOnGround();

	// Steering: the roll bends with the view's turn since it started, up to RollMaxTurnAngle each side.
	if (const ACharacter* Char = Character.Get(); Char && RollMaxTurnAngle > 0.f)
	{
		const float Turn = FMath::Clamp(FMath::FindDeltaAngleDegrees(RollStartYaw, Char->GetControlRotation().Yaw),
			-RollMaxTurnAngle, RollMaxTurnAngle);
		DiveDirection = FRotator(0.f, Turn, 0.f).RotateVector(RollBaseDirection);
	}

	RollElapsed += DeltaTime;
	if (RollElapsed >= RollDuration)
	{
		if (bOnGround)
		{
			bRollEndedOnGround = true;
			EndRoll(TEXT("distance atteinte"));
			return;
		}
		// Roll over before touching the ground (started early in a long dive, or off a ledge): keep
		// the end speed until landing, then stand up.
		RollElapsed = RollDuration;
		CMC->Velocity = DiveDirection * RollEndSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
		return;
	}
	if (bOnGround && RollElapsed > 0.1f && CMC->Velocity.Size2D() < RollEndSpeed * 0.5f)
	{
		EndRoll(TEXT("obstacle"));
		return;
	}

	const float Speed = FMath::Lerp(RollSpeedAtStart, RollEndSpeed, RollElapsed / RollDuration);
	CMC->Velocity = DiveDirection * Speed + FVector(0.f, 0.f, CMC->Velocity.Z);
}

void UDiveComponent::EndRoll(const TCHAR* Reason)
{
	if (ACharacter* Char = Character.Get())
	{
		Char->UnCrouch();
	}
	RestoreMovementSettings();
	State = EFidelisDiveState::None;
	LastStandTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	LastRollEndTime = LastStandTime;
	UE_LOG(LogFidelis, Log, TEXT("Dive: fin de roulade (%s) apres %.2f s."), Reason, RollElapsed);
}
