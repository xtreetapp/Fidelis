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
 *  - Hold IA_Sprint (Left Shift) while moving to run.
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide|Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** Max ground speed while sprinting (cm/s). Walking speed is the character's MaxWalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0))
	float SprintSpeed = 900.f;

	/** Minimum horizontal speed (fraction of SprintSpeed) to count as running when crouch is pressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Sprint", meta = (ClampMin = 0, ClampMax = 1))
	float MinSprintSpeedRatioToSlide = 0.7f;

	/** Distance covered by a slide on flat ground (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideDistance = 450.f;

	/** Speed at the start of the slide (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = 0))
	float SlideStartSpeed = 1150.f;

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
	bool bIsSliding = false;
	bool bCrouchWasHeld = false;
	bool bCrouchedByHold = false;

	FVector SlideDirection = FVector::ForwardVector;
	float SlideElapsed = 0.f;
	float SlideDuration = 0.f;
	double LastSlideEndTime = -100.0;
};
