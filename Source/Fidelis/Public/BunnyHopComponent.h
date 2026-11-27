#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BunnyHopComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UInputAction;

/**
 * Bunny hop, Warzone 1 style (defaults):
 *  - Chained jumps keep the run / slide speed (no slowdown on landing), without stacking speed
 *    (SpeedGainPerHop = 0; set it above 0 for a Quake / CS style gain capped by MaxBunnyHopSpeed).
 *  - Slide jump: jumping during a slide cancels it and carries its speed into the air.
 *  - Straight jumps (bLockAirMovement): the movement keys do nothing in the air, the jump keeps its
 *    take-off direction and speed until landing.
 *  - Wall jump: pressing jump in the air next to a wall bounces off it like a mirror: coming in at
 *    an angle A from the wall, it leaves at A on the other side (turn of 180 - 2A degrees).
 *  - Wall climb: coming at the wall head-on (WallClimbMinAngle or more), pressing jump goes up along
 *    the wall instead, MaxWallClimbs times, and over the top if the wall is low enough.
 *  - Ledge grab: in the air against a wall whose top is within reach of the hands, the character
 *    grabs the edge, pulls itself up and steps onto the top.
 * All settings are EditAnywhere: add a "Bunny Hop Component" to the character Blueprint to tweak them.
 */
UCLASS(ClassGroup = (Fidelis), meta = (BlueprintSpawnableComponent))
class FIDELIS_API UBunnyHopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBunnyHopComponent();

	UFUNCTION(BlueprintPure, Category = "Bunny Hop")
	int32 GetChainedHops() const { return ChainedHops; }

	/** Hanging on / pulling up onto a ledge. */
	UFUNCTION(BlueprintPure, Category = "Bunny Hop")
	bool IsMantling() const { return bMantling; }

	/** How much the hands are on the ledge (0..1): grab at the start, let go as the body goes over. */
	float GetMantleHandsAlpha() const;

	/** While mantling: middle of the grabbed edge (top of the wall) and the direction over it. */
	bool GetLedgeGrip(FVector& OutEdge, FVector& OutForward) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop")
	bool bEnableBunnyHop = true;

	/** If the jump key is still held on landing, jump again immediately (no time on the ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop")
	bool bAutoHopWhileJumpHeld = true;

	/** Jump pressed up to this long before touching down (s): the hop goes off the moment the character
	 *  lands, with no time on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float JumpBufferTime = 0.2f;

	/** Jump action (IA_Jump), used for bAutoHopWhileJumpHeld. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bunny Hop")
	TObjectPtr<UInputAction> JumpAction;

	/** Crouch action (IA_Crouch): air roll after a wall jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bunny Hop")
	TObjectPtr<UInputAction> CrouchAction;

	/** After a wall jump, pressing crouch in the air does a side roll away from the wall (the view turns a
	 *  full circle): wall on the right -> roll to the left, wall on the left -> roll to the right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump")
	bool bWallJumpAirRoll = true;

	/** Duration of the air roll (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump", meta = (ClampMin = 0.05, EditCondition = "bWallJumpAirRoll"))
	float AirRollDuration = 0.5f;

	/** Horizontal speed gained during the air roll only (0.1 = +10%); back to the bounce speed after it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump", meta = (ClampMin = 0, EditCondition = "bWallJumpAirRoll"))
	float AirRollSpeedBonus = 0.1f;

	/** View rotation of the air roll, in view space (identity when not rolling). Applied to the camera by
	 *  UDiveCameraModifier and to the arms by the LeanComponent, like the dive roll. */
	FQuat GetAirRollViewRotation() const;

	/** An air roll is available (after a wall bounce) or playing: a crouch press in the air is a roll. */
	bool IsAirRollReadyOrActive() const { return bAirRollReady || AirRollElapsed >= 0.f; }

	/** Max time between landing and the next jump for it to count as a hop (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float HopWindow = 0.3f;

	/** Speed added on each chained hop (cm/s). 0 = only keep the speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float SpeedGainPerHop = 0.f;

	/** Speed (hence distance: same air time) of each jump of a chain, as a share of the first jump's.
	 *  Entry 0 = first jump, 1 = second... hops past the end keep the last value. Running (900 cm/s,
	 *  0.6 s): 5.4 m, 5.4 m, 4 m, 3 m, 1.5 m, then on the spot (0) for every hop after that, until the character walks / runs on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop")
	TArray<float> ChainHopSpeedRatios = { 1.f, 1.f, 4.f / 5.4f, 3.f / 5.4f, 1.5f / 5.4f, 0.f };

	/** Upper limit for the speed reached through bunny hopping (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float MaxBunnyHopSpeed = 1000.f;

	/** Time in the air of a jump on flat ground (s). The jump keeps the height the character's
	 *  JumpZVelocity gives with normal gravity; a stronger gravity during jumps (only) and a faster
	 *  take-off make it last this long. 0 = engine jump unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float JumpAirTime = 0.6f;

	/** Below this horizontal speed, a jump is a normal jump (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop", meta = (ClampMin = 0))
	float MinSpeedToHop = 200.f;

	/** Jumps go straight: no steering with the movement keys in the air (direction and speed of the take-off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop")
	bool bLockAirMovement = true;

	/** Pressing jump in the air next to a wall bounces off it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump")
	bool bWallJump = true;

	/** Max gap between the character and the wall for a wall jump (cm, from the capsule's edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump", meta = (ClampMin = 0))
	float WallJumpMaxDistance = 40.f;

	/** Horizontal speed after the bounce (cm/s), the same whatever the speed coming in (walk, sprint,
	 *  slide jump...). Only the direction depends on the way in (mirror bounce). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump", meta = (ClampMin = 0))
	float WallJumpSpeed = 550.f;

	/** Upward speed of the wall jump (cm/s). 0 = the character's JumpZVelocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump", meta = (ClampMin = 0))
	float WallJumpUpSpeed = 0.f;

	/** Wall jumps allowed before touching the ground again. Never twice in a row off the same wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Jump", meta = (ClampMin = 1))
	int32 MaxWallJumps = 3;

	/** Climb a wall reached head-on instead of bouncing off it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Climb")
	bool bWallClimb = true;

	/** Smallest angle between the jump and the wall that counts as head-on (deg, 90 = straight at it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Climb", meta = (ClampMin = 0, ClampMax = 90))
	float WallClimbMinAngle = 60.f;

	/** Upward speed of each climb (cm/s). 0 = the character's JumpZVelocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Climb", meta = (ClampMin = 0))
	float WallClimbUpSpeed = 0.f;

	/** Speed toward the wall while climbing (cm/s): keeps the character against it and carries it over the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Climb", meta = (ClampMin = 0))
	float WallClimbHugSpeed = 200.f;

	/** Climbs allowed before touching the ground again. Once used up, jump bounces off the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Wall Climb", meta = (ClampMin = 0))
	int32 MaxWallClimbs = 2;

	/** Grab the top edge of a wall in reach while in the air, and climb onto it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Ledge Grab")
	bool bLedgeGrab = true;

	/** How far above the top of the head the hands reach for a ledge (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Ledge Grab", meta = (ClampMin = 0))
	float LedgeGrabReachAboveHead = 50.f;

	/** Max gap between the character and the wall to grab its edge (cm, from the capsule's edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Ledge Grab", meta = (ClampMin = 0))
	float LedgeGrabMaxDistance = 30.f;

	/** Time hanging on the edge before pulling up (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Ledge Grab", meta = (ClampMin = 0))
	float LedgeHangTime = 0.1f;

	/** Time to pull up from the hands to standing on the top (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop|Ledge Grab", meta = (ClampMin = 0.05))
	float LedgeClimbDuration = 0.5f;

	/** Jump during a slide: cancel the slide and jump, keeping the slide speed (slide cancel jump). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bunny Hop")
	bool bSlideJump = true;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool IsJumpHeld() const;
	/** Jump out of the current slide, keeping its speed. */
	void SlideJump();
	/** Bounce off a wall next to the character, if there is one. */
	bool TryWallJump();
	/** Grab the edge of the wall in front (Facing) if its top is within reach and there is room on it. */
	bool TryLedgeGrab(const FVector& Facing);
	void TickMantle(float DeltaTime);

	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;

	bool bWasFalling = false;
	bool bJumpWasHeld = false;
	bool bCrouchWasHeld = false;
	/** An air roll is available (from a wall jump until it is used or the character lands). */
	bool bAirRollReady = false;
	/** Air roll: elapsed time (-1 = none) and its axis in view space (pitch for flips, roll for sides). */
	float AirRollElapsed = -1.f;
	FRotator AirRollAxis = FRotator::ZeroRotator;
	/** Horizontal speed before the air roll's bonus, restored when the roll ends. */
	float AirRollBaseSpeed = 0.f;
	/** In the air from a jump (not walking off a ledge, not a dive). */
	bool bInJump = false;
	/** Direction of the jump that just landed: the chained hop goes the same way. */
	FVector ChainDir = FVector::ZeroVector;
	/** Last jump key press (world time), for the hop on landing. */
	double LastJumpPressTime = -100.0;
	/** Speed of the first jump of the current chain (cm/s). */
	float ChainBaseSpeed = 0.f;
	float GetChainHopSpeed(int32 HopIndex) const;
	bool CanChainHop() const;
	/** Jump timing: character's own gravity scale, the stronger one used during jumps, applied or not. */
	float BaseGravityScale = 1.f;
	float JumpGravityScale = 1.f;
	bool bJumpGravityOn = false;
	int32 WallJumpCount = 0;
	int32 WallClimbCount = 0;
	/** Climbing a wall: pushed against it (toward -ClimbNormal) until landing or a wall jump. */
	bool bWallClimbing = false;
	FVector ClimbNormal = FVector::ZeroVector;

	// Ledge grab: hang at MantleStart, rise to MantleUp (on the edge), move onto MantleEnd.
	bool bMantling = false;
	float MantleElapsed = 0.f;
	FVector MantleStart = FVector::ZeroVector;
	FVector MantleUp = FVector::ZeroVector;
	FVector MantleEnd = FVector::ZeroVector;
	FVector LedgeEdge = FVector::ZeroVector;
	FVector LedgeForward = FVector::ForwardVector;
	FVector LastWallNormal = FVector::ZeroVector;
	/** Horizontal take-off direction of the current jump (zero for a jump in place). */
	FVector JumpDir = FVector::ZeroVector;
	/** Horizontal take-off speed of the current jump, kept even after hitting a wall (cm/s). */
	float JumpSpeed = 0.f;
	float LastAirSpeed = 0.f;
	double LandTime = -100.0;
	int32 ChainedHops = 0;
	/** Horizontal speed kept during the air phase of a jump (0 = not maintained). */
	float HopSpeed = 0.f;
};
