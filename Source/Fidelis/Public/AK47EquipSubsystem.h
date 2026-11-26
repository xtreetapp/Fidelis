#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AK47EquipSubsystem.generated.h"

class AAK47Weapon;
class AController;
class APawn;
class APlayerController;
class UInputMappingContext;

/**
 * Gives every player-controlled character an AK-47 as soon as it is possessed
 * (game start and respawns). Works without modifying the character Blueprint.
 */
UCLASS()
class FIDELIS_API UAK47EquipSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Gives the player character its movement abilities (sprint/slide, dive) and the AK-47. */
	UFUNCTION(BlueprintCallable, Category = "AK47")
	void SetupPlayerCharacter(APawn* Pawn);

	/** Spawns an AK-47 and attaches it to the pawn, unless it already holds one. */
	UFUNCTION(BlueprintCallable, Category = "AK47")
	AAK47Weapon* EquipAK47(APawn* Pawn);

	/** Weapon class to spawn (can be set to a Blueprint child of AAK47Weapon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AK47")
	TSubclassOf<AAK47Weapon> WeaponClass;

private:
	void BindPossessionDelegate();

	/** Adds the custom keys on top of IMC_Default: I/K forward/back, J/L strafe, Z jump, W crouch, N dive/prone. */
	void AddMovementKeys(APlayerController* PC);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MovementKeysContext;

	UFUNCTION()
	void HandlePawnControllerChanged(APawn* Pawn, AController* Controller);
};
