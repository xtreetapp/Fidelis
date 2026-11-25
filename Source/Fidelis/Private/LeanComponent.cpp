#include "LeanComponent.h"

#include "AK47Weapon.h"
#include "Fidelis.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

ULeanComponent::ULeanComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Update before the camera manager reads the camera transform at the end of the frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void ULeanComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!LeanLeftAction)
	{
		LeanLeftAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_LeanLeft.IA_LeanLeft"));
	}
	if (!LeanRightAction)
	{
		LeanRightAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_LeanRight.IA_LeanRight"));
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<USceneComponent*> Components;
	Owner->GetComponents<USceneComponent>(Components);
	for (USceneComponent* Comp : Components)
	{
		if (Comp->GetFName() == LeanComponentName)
		{
			LeanTarget = Comp;
		}
		if (!CameraComponent.IsValid() && Comp->IsA<UCameraComponent>())
		{
			CameraComponent = Comp;
		}
	}

	// Fallback: move the camera itself.
	if (!LeanTarget.IsValid())
	{
		LeanTarget = CameraComponent;
	}

	if (USceneComponent* Target = LeanTarget.Get())
	{
		BaseRelativeLocation = Target->GetRelativeLocation();
	}

	UE_LOG(LogFidelis, Log, TEXT("Lean: actif sur %s (composant deplace: %s, IA_LeanLeft=%s, IA_LeanRight=%s)."),
		*GetNameSafe(Owner), *GetNameSafe(LeanTarget.Get()), *GetNameSafe(LeanLeftAction), *GetNameSafe(LeanRightAction));
	if (!LeanLeftAction || !LeanRightAction)
	{
		UE_LOG(LogFidelis, Error, TEXT("Lean: IA_LeanLeft / IA_LeanRight introuvables dans /Game/Input/Actions."));
	}
}

void ULeanComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ApplyOffset(0.f);
	Super::EndPlay(EndPlayReason);
}

bool ULeanComponent::IsActionHeld(APlayerController* PC, const UInputAction* Action) const
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

bool ULeanComponent::IsFireHeld(APlayerController* PC) const
{
	TArray<AActor*> Attached;
	GetOwner()->GetAttachedActors(Attached);
	for (AActor* Actor : Attached)
	{
		if (const AAK47Weapon* Weapon = Cast<AAK47Weapon>(Actor))
		{
			return IsActionHeld(PC, Weapon->GetFireAction());
		}
	}
	return false;
}

float ULeanComponent::ClampOffsetAgainstWalls(float DesiredOffset) const
{
	const USceneComponent* Camera = CameraComponent.Get();
	if (!Camera || FMath::IsNearlyZero(DesiredOffset))
	{
		return DesiredOffset;
	}

	// Camera position with no lean applied, then sweep towards the desired side.
	const FVector Right = GetOwner()->GetActorRightVector();
	const FVector Start = Camera->GetComponentLocation() - Right * CurrentOffset;
	const FVector End = Start + Right * DesiredOffset;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LeanProbe), false, GetOwner());
	TArray<AActor*> Attached;
	GetOwner()->GetAttachedActors(Attached);
	Params.AddIgnoredActors(Attached);

	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(CollisionProbeRadius), Params))
	{
		return FMath::Sign(DesiredOffset) * FMath::Max(0.f, Hit.Distance - 1.f);
	}
	return DesiredOffset;
}

void ULeanComponent::ApplyOffset(float Offset)
{
	USceneComponent* Target = LeanTarget.Get();
	if (!Target || !GetOwner())
	{
		return;
	}

	// Lean along the character's right vector, converted into the target's parent space.
	const FVector WorldOffset = GetOwner()->GetActorRightVector() * Offset;
	FVector LocalOffset = WorldOffset;
	if (const USceneComponent* Parent = Target->GetAttachParent())
	{
		LocalOffset = Parent->GetComponentTransform().InverseTransformVector(WorldOffset);
	}
	Target->SetRelativeLocation(BaseRelativeLocation + LocalOffset);
}

void ULeanComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;

	int32 Side = 0;
	if (PC && (!bLeanOnlyWhileFiring || IsFireHeld(PC)))
	{
		const bool bLeft = IsActionHeld(PC, LeanLeftAction);
		const bool bRight = IsActionHeld(PC, LeanRightAction);
		Side = (bRight ? 1 : 0) - (bLeft ? 1 : 0); // both held -> stay centered
	}

	if (Side != LastLoggedSide)
	{
		UE_LOG(LogFidelis, Log, TEXT("Lean: %s"), Side < 0 ? TEXT("gauche") : Side > 0 ? TEXT("droite") : TEXT("centre"));
		LastLoggedSide = Side;
	}

	const float Desired = ClampOffsetAgainstWalls(Side * LeanDistance);
	const float NewOffset = FMath::FInterpTo(CurrentOffset, Desired, DeltaTime, LeanInterpSpeed);
	if (!FMath::IsNearlyEqual(NewOffset, CurrentOffset, 0.01f) || (Desired == 0.f && CurrentOffset != 0.f))
	{
		CurrentOffset = FMath::Abs(NewOffset) < 0.01f && Desired == 0.f ? 0.f : NewOffset;
		ApplyOffset(CurrentOffset);
	}
}
