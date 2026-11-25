#include "DiveComponent.h"

#include "Fidelis.h"
#include "SlideComponent.h"
#include "Components/CapsuleComponent.h"
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

	SavedCrouchedHalfHeight = CMC->GetCrouchedHalfHeight();
	SavedMaxWalkSpeedCrouched = CMC->MaxWalkSpeedCrouched;
	SavedGroundFriction = CMC->GroundFriction;
	SavedBrakingDeceleration = CMC->BrakingDecelerationWalking;
	SavedAirControl = CMC->AirControl;

	CMC->AirControl = DiveAirControl;
	Char->LaunchCharacter(Dir * DiveHorizontalSpeed + FVector::UpVector * DiveUpSpeed, /*bXYOverride*/ true, /*bZOverride*/ true);
	SetMoveLocked(true);

	State = EFidelisDiveState::Airborne;
	AirTime = 0.f;
	bRollQueued = false;
	DiveDirection = Dir;

	// Readable direction relative to where the player looks.
	const FRotator Yaw(0.f, Char->GetControlRotation().Yaw, 0.f);
	const FVector Local = Yaw.UnrotateVector(Dir);
	const TCHAR* DirName = FMath::Abs(Local.X) >= FMath::Abs(Local.Y)
		? (Local.X >= 0.f ? TEXT("avant") : TEXT("arriere"))
		: (Local.Y >= 0.f ? TEXT("droite") : TEXT("gauche"));
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
	UE_LOG(LogFidelis, Log, TEXT("Dive: atterrissage, allonge (%.2f s en l'air)."), AirTime);
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
	RestoreMovementSettings();
	State = EFidelisDiveState::None;
	LastStandTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UE_LOG(LogFidelis, Log, TEXT("Dive: releve (%s)."), Reason);
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
	const bool bPressed = bHeld && !bDiveWasHeld;
	bDiveWasHeld = bHeld;

	const bool bCrouchHeld = IsActionHeld(PC, CrouchAction);
	const bool bCrouchPressed = bCrouchHeld && !bCrouchWasHeld;
	bCrouchWasHeld = bCrouchHeld;

	switch (State)
	{
	case EFidelisDiveState::None:
		if (bPressed && CanStartDive())
		{
			StartDive();
		}
		break;

	case EFidelisDiveState::Airborne:
		AirTime += DeltaTime;

		// Dive key then crouch key -> roll: bring the character down fast and roll on landing.
		if (bEnableRoll && !bRollQueued && bCrouchPressed && AirTime <= RollComboWindow)
		{
			bRollQueued = true;
			CMC->Velocity.Z = FMath::Min(CMC->Velocity.Z, -RollDropSpeed);
			UE_LOG(LogFidelis, Log, TEXT("Dive: combo plongeon + baisse -> roulade vers %s."), DiveDirectionName);
		}

		// Give the launch a moment to leave the ground before checking for landing.
		if ((AirTime > 0.1f || bRollQueued) && CMC->IsMovingOnGround())
		{
			if (bRollQueued)
			{
				StartRoll();
			}
			else
			{
				Land(bHeld);
			}
		}
		break;

	case EFidelisDiveState::Rolling:
		TickRoll(DeltaTime);
		break;

	case EFidelisDiveState::Prone:
		if (!bHeld)
		{
			StandUp(TEXT("touche relachee"));
		}
		break;
	}
}

void UDiveComponent::StartRoll()
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	const float Radius = Char->GetCapsuleComponent() ? Char->GetCapsuleComponent()->GetUnscaledCapsuleRadius() : RollHalfHeight;
	CMC->SetCrouchedHalfHeight(FMath::Max(RollHalfHeight, Radius));
	CMC->MaxWalkSpeedCrouched = RollStartSpeed;
	CMC->GroundFriction = 0.f;
	CMC->BrakingDecelerationWalking = 0.f;
	CMC->AirControl = SavedAirControl;
	Char->Crouch();

	// Linear speed decay: distance = average speed * duration.
	const float AvgSpeed = FMath::Max((RollStartSpeed + RollEndSpeed) * 0.5f, 1.f);
	RollDuration = RollDistance / AvgSpeed;
	RollElapsed = 0.f;
	bRollQueued = false;
	State = EFidelisDiveState::Rolling;

	UE_LOG(LogFidelis, Log, TEXT("Dive: roulade vers %s (%.0f cm en %.2f s)."), DiveDirectionName, RollDistance, RollDuration);
}

void UDiveComponent::TickRoll(float DeltaTime)
{
	UCharacterMovementComponent* CMC = Movement.Get();

	if (!CMC->IsMovingOnGround())
	{
		EndRoll(TEXT("plus au sol"));
		return;
	}

	RollElapsed += DeltaTime;
	if (RollElapsed >= RollDuration)
	{
		EndRoll(TEXT("distance atteinte"));
		return;
	}
	if (RollElapsed > 0.1f && CMC->Velocity.Size2D() < RollEndSpeed * 0.5f)
	{
		EndRoll(TEXT("obstacle"));
		return;
	}

	const float Speed = FMath::Lerp(RollStartSpeed, RollEndSpeed, RollElapsed / RollDuration);
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
	UE_LOG(LogFidelis, Log, TEXT("Dive: fin de roulade (%s) apres %.2f s."), Reason, RollElapsed);
}
