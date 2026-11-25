#include "BunnyHopComponent.h"

#include "Fidelis.h"
#include "DiveComponent.h"
#include "SlideComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"

UBunnyHopComponent::UBunnyHopComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Run after the movement component so landings and jump starts of this frame are visible.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UBunnyHopComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!JumpAction)
	{
		JumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	}

	Character = Cast<ACharacter>(GetOwner());
	if (ACharacter* Char = Character.Get())
	{
		Movement = Char->GetCharacterMovement();
	}
	UE_LOG(LogFidelis, Log, TEXT("BunnyHop: actif sur %s (fenetre %.2f s, gain %.0f, max %.0f cm/s)."),
		*GetNameSafe(GetOwner()), HopWindow, SpeedGainPerHop, MaxBunnyHopSpeed);
}

void UBunnyHopComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	if (!Char || !CMC || !bEnableBunnyHop)
	{
		return;
	}

	const bool bFalling = CMC->IsFalling();
	const double Now = GetWorld()->GetTimeSeconds();

	if (bFalling && !bWasFalling)
	{
		// Just left the ground going up = a jump (walking off a ledge has no upward speed).
		// bPressedJump can't be used: the engine clears it as soon as the jump is performed.
		const bool bIsJump = CMC->Velocity.Z > 1.f;
		const USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
		const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>();
		const bool bBusy = (Slide && Slide->IsSliding()) || (Dive && Dive->IsBusy());

		if (bIsJump && !bBusy && Now - LandTime <= HopWindow && LastAirSpeed >= MinSpeedToHop)
		{
			const float CurrentSpeed = CMC->Velocity.Size2D();
			const float Target = FMath::Max(CurrentSpeed, FMath::Min(LastAirSpeed + SpeedGainPerHop, MaxBunnyHopSpeed));
			FVector Dir = CMC->Velocity.GetSafeNormal2D();
			if (Dir.IsNearlyZero())
			{
				Dir = Char->GetActorForwardVector().GetSafeNormal2D();
			}
			CMC->Velocity = Dir * Target + FVector(0.f, 0.f, CMC->Velocity.Z);
			HopSpeed = Target;
			++ChainedHops;
			UE_LOG(LogFidelis, Log, TEXT("BunnyHop: saut %d enchaine, %.0f -> %.0f cm/s (max %.0f)."),
				ChainedHops, CurrentSpeed, Target, MaxBunnyHopSpeed);
		}
		else if (bIsJump)
		{
			ChainedHops = 0;
			HopSpeed = 0.f;
		}
	}
	else if (!bFalling && bWasFalling)
	{
		LandTime = Now;

		// Jump key still held on landing -> jump again right away (applied by the movement component next frame).
		if (bAutoHopWhileJumpHeld && JumpAction)
		{
			const APlayerController* PC = Cast<APlayerController>(Char->GetController());
			const UEnhancedPlayerInput* PlayerInput = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
			if (PlayerInput && PlayerInput->GetActionValue(JumpAction).Get<bool>())
			{
				Char->Jump();
			}
		}
	}
	else if (!bFalling && Now - LandTime > HopWindow)
	{
		ChainedHops = 0;
		HopSpeed = 0.f;
	}

	if (bFalling)
	{
		// The character brakes in the air above its max walk speed (BrakingDecelerationFalling):
		// keep the hop speed for the whole air phase, while still following air steering.
		if (HopSpeed > 0.f)
		{
			const float Speed2D = CMC->Velocity.Size2D();
			if (Speed2D < HopSpeed * 0.5f)
			{
				HopSpeed = 0.f; // hit something: stop maintaining
			}
			else if (Speed2D < HopSpeed)
			{
				const FVector Dir = CMC->Velocity.GetSafeNormal2D();
				CMC->Velocity = Dir * HopSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
			}
		}
		LastAirSpeed = CMC->Velocity.Size2D();
	}
	bWasFalling = bFalling;
}
