#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LeanComponent.generated.h"

class ACharacter;
class APlayerController;
class UInputAction;
class USceneComponent;

/**
 * Lean-fire: while the player holds a lean button (Thumb01 = left, Thumb02 = right) AND holds fire,
 * the first-person view (arms + camera + weapon) slides smoothly sideways. Released -> slides back.
 *
 * Reads input from IA_LeanLeft / IA_LeanRight (mapped in IMC_Default) and the AK-47's fire action.
 * Does not touch the weapon itself.
 */
UCLASS(ClassGroup = (Fidelis), meta = (BlueprintSpawnableComponent))
class FIDELIS_API ULeanComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULeanComponent();

	/** Current lean, -1 (full left) .. +1 (full right). */
	UFUNCTION(BlueprintPure, Category = "Lean")
	float GetLeanAlpha() const { return LeanDistance > 0.f ? CurrentOffset / LeanDistance : 0.f; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lean|Input")
	TObjectPtr<UInputAction> LeanLeftAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lean|Input")
	TObjectPtr<UInputAction> LeanRightAction;

	/** If true, leaning only happens while the fire button is held (lean-fire). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	bool bLeanOnlyWhileFiring = true;

	/** Sideways camera offset at full lean, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = 0))
	float LeanDistance = 35.f;

	/** Interpolation speed towards the target offset (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = 0))
	float LeanInterpSpeed = 10.f;

	/** Radius of the collision probe that stops the camera from leaning through walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = 0))
	float CollisionProbeRadius = 12.f;

	/** Name of the character component that carries the camera (moved to produce the lean). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lean")
	FName LeanComponentName = TEXT("FirstPersonMesh");

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool IsActionHeld(APlayerController* PC, const UInputAction* Action) const;
	bool IsFireHeld(APlayerController* PC) const;
	float ClampOffsetAgainstWalls(float DesiredOffset) const;
	void ApplyOffset(float Offset);

	TWeakObjectPtr<USceneComponent> LeanTarget;
	TWeakObjectPtr<USceneComponent> CameraComponent;
	FVector BaseRelativeLocation = FVector::ZeroVector;
	float CurrentOffset = 0.f;
	int32 LastLoggedSide = 0;
};
