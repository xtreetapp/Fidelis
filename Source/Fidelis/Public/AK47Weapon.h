#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AK47Weapon.generated.h"

class ACharacter;
class APlayerController;
class USkeletalMeshComponent;
class USoundBase;
class UAnimMontage;
class UAnimInstance;
class UInputAction;
class UInputMappingContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAK47AmmoChanged, int32, AmmoInMagazine, int32, ReserveAmmo);

/**
 * AK-47 assault rifle: full-auto hitscan weapon, 7.62x39mm, 30-round magazine, ~600 RPM.
 *
 * Has a first-person mesh (seen only by the owning player) and a world-space mesh
 * (seen by everyone else / in shadows), both attached to the owning character's hand.
 * Input (LMB = fire, R = reload) is created at runtime, so no input assets are required.
 */
UCLASS(Blueprintable)
class FIDELIS_API AAK47Weapon : public AActor
{
	GENERATED_BODY()

public:
	AAK47Weapon();

	/** Attaches the rifle to the character's hands, swaps the anim classes and binds input. */
	UFUNCTION(BlueprintCallable, Category = "AK47")
	void EquipTo(ACharacter* NewOwner);

	/** Detaches the rifle from its owner and removes its input. */
	UFUNCTION(BlueprintCallable, Category = "AK47")
	void Unequip();

	UFUNCTION(BlueprintCallable, Category = "AK47")
	void StartFire();

	UFUNCTION(BlueprintCallable, Category = "AK47")
	void StopFire();

	UFUNCTION(BlueprintCallable, Category = "AK47")
	void Reload();

	UFUNCTION(BlueprintPure, Category = "AK47")
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }

	UFUNCTION(BlueprintPure, Category = "AK47")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintPure, Category = "AK47")
	ACharacter* GetOwningCharacter() const { return OwningCharacter.Get(); }

	/** Runtime-created fire input action (read-only access, e.g. for lean-fire). */
	const UInputAction* GetFireAction() const { return FireAction; }

	UPROPERTY(BlueprintAssignable, Category = "AK47")
	FOnAK47AmmoChanged OnAmmoChanged;

protected:
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---- Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** Mesh rendered in first person for the owning player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** Mesh rendered for everyone else (and for the owner's shadow). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> ThirdPersonMesh;

	// ---- AK-47 stats ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 1))
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AK47|Stats", meta = (ClampMin = 0))
	int32 ReserveAmmo = 90;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 1))
	float RoundsPerMinute = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float Damage = 36.f;

	/** Effective range in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float Range = 15000.f;

	/** Base cone half-angle in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float BaseSpreadDegrees = 0.5f;

	/** Extra spread added per consecutive shot of a burst, capped at MaxSpreadDegrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float SpreadPerShotDegrees = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float MaxSpreadDegrees = 3.f;

	/** Camera kick per shot (degrees, applied upward). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float RecoilPitch = 0.45f;

	/** Random horizontal kick per shot (degrees, +/-). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float RecoilYaw = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float ReloadDuration = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Stats", meta = (ClampMin = 0))
	float ImpactImpulse = 30000.f;

	// ---- Presentation ----

	/** Hand socket on the character meshes the rifle attaches to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Attach")
	FName GripSocket = TEXT("HandGrip_R");

	/** Fine-tuning of the first-person rifle relative to the HandGrip_R socket (if it sits slightly off in the hands). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Attach")
	FTransform FirstPersonGripOffset = FTransform::Identity;

	/** Name of the character's first-person arms component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Attach")
	FName CharacterFirstPersonMeshName = TEXT("FirstPersonMesh");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|FX")
	TObjectPtr<USoundBase> FireSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|FX")
	TObjectPtr<UAnimMontage> FireMontage;

	/** Anim class applied to the character's first-person arms while holding the rifle (optional). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Anim")
	TSubclassOf<UAnimInstance> FirstPersonAnimClass;

	/** Anim class applied to the character's world-space body while holding the rifle (optional). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AK47|Anim")
	TSubclassOf<UAnimInstance> ThirdPersonAnimClass;

private:
	void FireShot();
	void FinishReload();
	void SetupInput(APlayerController* PC);
	void TeardownInput();
	void BroadcastAmmo();
	USkeletalMeshComponent* FindCharacterFirstPersonMesh(ACharacter* Character) const;

	UFUNCTION()
	void HandleOwnerDestroyed(AActor* DestroyedActor);

	TWeakObjectPtr<ACharacter> OwningCharacter;
	TWeakObjectPtr<APlayerController> InputController;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> PreviousFirstPersonAnimClass;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> PreviousThirdPersonAnimClass;

	FTimerHandle FireTimer;
	FTimerHandle ReloadTimer;

	int32 AmmoInMagazine = 30;
	int32 ConsecutiveShots = 0;
	double LastFireTime = -1.0;
	bool bWantsToFire = false;
	bool bIsReloading = false;
};
