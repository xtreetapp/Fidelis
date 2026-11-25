#include "SlideComponent.h"

#include "Fidelis.h"
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
	const bool bCrouchHeld = IsActionHeld(PC, CrouchAction);
	const bool bCrouchPressed = bCrouchHeld && !bCrouchWasHeld;
	bCrouchWasHeld = bCrouchHeld;

	if (bIsSliding)
	{
		TickSlide(DeltaTime);
		return;
	}

	// Diving / lying down: the dive component owns the crouch state and movement settings.
	if (const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>(); Dive && Dive->IsBusy())
	{
		bIsSprinting = false;
		bCrouchedByHold = false;
		return;
	}

	// --- Sprint ---
	const float GroundSpeed = CMC->Velocity.Size2D();
	const bool bMoving = GroundSpeed > 10.f && !CMC->GetCurrentAcceleration().IsNearlyZero();
	const bool bWantsSprint = bSprintHeld && bMoving && !bCrouchedByHold && !CMC->IsCrouching();
	if (bWantsSprint != bIsSprinting)
	{
		bIsSprinting = bWantsSprint;
		CMC->MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
		UE_LOG(LogFidelis, Verbose, TEXT("Slide: course %s"), bIsSprinting ? TEXT("ON") : TEXT("OFF"));
	}

	// --- Crouch pressed while running -> slide ---
	const bool bCooldownOver = GetWorld()->GetTimeSeconds() - LastSlideEndTime >= SlideCooldown;
	if (bCrouchPressed && bIsSprinting && CMC->IsMovingOnGround() && bCooldownOver
		&& GroundSpeed >= SprintSpeed * MinSprintSpeedRatioToSlide)
	{
		StartSlide();
		return;
	}

	// --- Plain crouch while not running (hold) ---
	if (bCrouchHeld && !bIsSprinting && !bCrouchedByHold)
	{
		Char->Crouch();
		bCrouchedByHold = true;
	}
	else if (!bCrouchHeld && bCrouchedByHold)
	{
		Char->UnCrouch();
		bCrouchedByHold = false;
	}
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

	// Linear speed decay from Start to End: distance = average speed * duration.
	const float AvgSpeed = FMath::Max((SlideStartSpeed + SlideEndSpeed) * 0.5f, 1.f);
	SlideDuration = SlideDistance / AvgSpeed;
	SlideElapsed = 0.f;

	bIsSliding = true;
	bIsSprinting = false;
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
		SlideStartSpeed, SlideEndSpeed, SlideDistance, SlideDuration);
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
	CMC->Velocity = SlideDirection * Speed + FVector(0.f, 0.f, CMC->Velocity.Z);
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
	}

	UE_LOG(LogFidelis, Log, TEXT("Slide: fin (%s) apres %.2f s."), Reason, SlideElapsed);
}
