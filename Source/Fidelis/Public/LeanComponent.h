#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LeanComponent.generated.h"

class AAK47Weapon;
class ACharacter;
class APlayerController;
class USkeletalMeshComponent;
class UInputAction;
class USceneComponent;

/**
 * Lean: while the player holds a lean button (Thumb01 = left, Thumb02 = right), firing or not,
 * the first-person view (arms + camera + weapon) slides smoothly sideways, the arms + weapon cant
 * around the line of sight (WeaponLeanRollAngle) and the camera tilts (CameraLeanRollAngle).
 * Released -> everything slides back.
 *
 * Also drives aim down sights: while the AK-47's aim button (RMB) is held, the arms are moved so the
 * rifle's bore lines up with the line of sight just below the eye, and the camera zooms in. Leaning
 * and aiming combine (the cant then pivots around the sight line).
 * Set bLeanOnlyWhileFiring to restrict leaning to while the fire button is held (lean-fire).
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
	bool bLeanOnlyWhileFiring = false;

	/** Sideways camera offset at full lean, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = 0))
	float LeanDistance = 35.f;

	/** Cant of the arms + weapon at full lean, in degrees, around the line of sight (Rainbow Six style).
	 *  Positive = the weapon tilts towards the lean side. 0 disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = -90, ClampMax = 90))
	float WeaponLeanRollAngle = 45.f;

	/** Camera tilt at full lean, in degrees (applied as control rotation roll). 0 keeps the view level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = -45, ClampMax = 45))
	float CameraLeanRollAngle = 15.f;

	/** Lying down, the view tilts exactly like the rifle (lean buttons and lying on the side), even
	 *  without aiming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	bool bProneViewFollowsWeaponCant = true;

	/** Swap the two lean buttons (the one mapped to IA_LeanLeft leans right, and the other way round). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean")
	bool bSwapLeanButtons = true;

	/** Interpolation speed towards the target offset (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = 0))
	float LeanInterpSpeed = 10.f;

	/** Radius of the collision probe that stops the camera from leaning through walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lean", meta = (ClampMin = 0))
	float CollisionProbeRadius = 12.f;

	/** Name of the character component that carries the camera (moved to produce the lean). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lean")
	FName LeanComponentName = TEXT("FirstPersonMesh");

	// ---- Crouch / prone view height ----

	/** Lowers the first-person view while crouched / lying down. The arms mesh rides on the body mesh,
	 *  which the engine keeps at standing height when the capsule shrinks, so without this the view
	 *  would not move. Eye height = standing eye height * CrouchViewHeightRatio / ProneViewHeightRatio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crouch")
	bool bAdjustViewForCrouch = true;

	/** Eye height while crouched, as a fraction of the standing eye height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crouch", meta = (ClampMin = 0, ClampMax = 1))
	float CrouchViewHeightRatio = 0.5f;

	/** Eye height while lying down (dive key), as a fraction of the standing eye height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crouch", meta = (ClampMin = 0, ClampMax = 1))
	float ProneViewHeightRatio = 0.25f;

	/** Time for the view to go down when crouching (crouch key held, not running) (s). 0 = uses
	 *  CrouchViewInterpSpeed like the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crouch", meta = (ClampMin = 0))
	float CrouchDownDuration = 0.2f;

	/** Speed of the view going down / back up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crouch", meta = (ClampMin = 0))
	float CrouchViewInterpSpeed = 12.f;

	// ---- Weapon sway / view while moving ----

	/** How fast the rifle follows the arms animation, relative to the view (1/s). Lower = smoother,
	 *  softer sway; higher = closer to the raw (jerky) third-person run animation. 0 = no smoothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway", meta = (ClampMin = 0))
	float WeaponSwaySmoothing = 8.f;

	/** Share of the arms animation's rifle motion kept while moving (0 = rifle fixed at its resting
	 *  pose, 1 = full third-person run animation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway", meta = (ClampMin = 0, ClampMax = 1))
	float WeaponSwayAmount = 0.15f;

	/** The view doesn't bob with the arms animation's head while walking / running (always steady while aiming). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sway")
	bool bSteadyCamera = true;

	// ---- Sprint pose (rifle raised, held in one hand) ----

	/** Raise the rifle and hold it in the right hand only while sprinting (not aiming). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose")
	bool bSprintPose = true;

	/** Where the right hand holds the rifle while sprinting, relative to the eye in torso space (cm):
	 *  X forward, Y right, Z up. The rifle stands straight up from it (90 degrees to the torso, muzzle
	 *  to the sky), whatever the view pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose")
	FVector SprintGripOffset = FVector(45.f, 18.f, -22.f);

	/** Turn of the upright rifle around its own vertical axis (degrees): 0 = top of the rifle facing the
	 *  player, positive = shows more of its left side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose", meta = (ClampMin = -180, ClampMax = 180))
	float SprintRifleTwist = 0.f;

	/** Raised rifle moving with the body while running, in torso space (cm): Y = side sway once per
	 *  stride, Z = bounce on every step. 0 = rigid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose")
	FVector SprintBobAmplitude = FVector(0.f, 1.2f, 1.5f);

	/** Raised rifle tilt with the body while running (degrees): pitch on every step, roll once per stride. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose")
	FRotator SprintBobRotation = FRotator(2.f, 0.f, 2.f);

	/** Distance covered by one running stride (two steps), sets the rhythm of the movement (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose", meta = (ClampMin = 1))
	float SprintStrideLength = 400.f;

	/** Bones kept visible (with all their children) on the first-person arms while sprinting: the arms
	 *  mesh is a full body, and tilting it to raise the rifle brings the torso, head and legs into view.
	 *  Everything else is hidden, so only the arm holding the rifle shows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose")
	TArray<FName> SprintVisibleBones = { TEXT("clavicle_r") };

	/** Bones always hidden (with their children) on the first-person arms: the arms mesh is a full body,
	 *  and when it tilts (hands out in a dive, lying down) its head comes in front of the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose")
	TArray<FName> FirstPersonHiddenBones = { TEXT("head") };

	// ---- Climbing onto a ledge (BunnyHopComponent ledge grab) ----

	/** Both hands grab the top of the wall while climbing onto a ledge (IK on the first-person arms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ledge Grab")
	bool bMantleHands = true;

	/** In the air (jumps, falls): the arms animation is paused, holding the take-off walking pose (view,
	 *  arms and rifle exactly as walking), instead of its jump / fall pose that moves the whole upper body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air")
	bool bKeepGroundArmPoseInAir = true;

	/** Gap between the two hands on the edge (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ledge Grab", meta = (ClampMin = 0))
	float MantleHandSpacing = 45.f;

	/** Wrist position relative to the middle of the edge (cm): X = over the top (past the edge),
	 *  Z = above the top surface. The palm and fingers lie on the top from there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ledge Grab")
	FVector MantleWristOffset = FVector(2.f, 0.f, 5.f);

	/** Elbows pushed out / down while hanging (cm, from where the animation has them). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ledge Grab")
	FVector2D MantleElbowOffset = FVector2D(25.f, -20.f);

	/** Time to raise the rifle into the sprint pose (s, eased in and out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose", meta = (ClampMin = 0))
	float SprintPoseRaiseDuration = 0.3f;

	/** Time to lower the rifle out of the sprint pose (s, eased in and out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint Pose", meta = (ClampMin = 0))
	float SprintPoseLowerDuration = 0.27f;

	/** Current sprint pose blend, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Sprint Pose")
	float GetSprintPoseAlpha() const { return SprintPoseAlpha; }

	// ---- Aim down sights (RMB on the AK-47) ----

	/** Current aim blend, 0 (hip) .. 1 (looking through the sights). */
	UFUNCTION(BlueprintPure, Category = "Aim")
	float GetAimAlpha() const { return AimAlpha; }

	/** Blend speed between the hip pose and the sights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = 0))
	float AimInterpSpeed = 14.f;

	/** While aiming, the view tilts with the rifle (lean, lying on the side) so the sights stay
	 *  upright on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	bool bAimViewFollowsWeaponCant = true;

	/** Camera field of view while aiming, in degrees (zoom). 0 keeps the normal FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim", meta = (ClampMin = 0, ClampMax = 170))
	float AimFieldOfView = 65.f;

	/** Height of the sight line above the bore, in cm: the barrel ends up this far below the eye. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float AimSightHeight = 6.f;

	/** Moves the rifle along the line of sight while aiming, in cm (negative = closer to the eye). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float AimDistanceOffset = 0.f;

	/** Barrel-end socket of the rifle mesh, used to find the bore axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	FName MuzzleSocketName = TEXT("Muzzle");

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool IsActionHeld(APlayerController* PC, const UInputAction* Action) const;
	bool IsFireHeld(APlayerController* PC) const;
	AAK47Weapon* FindWeapon() const;
	float ClampOffsetAgainstWalls(float DesiredOffset) const;
	/** DeltaTime < 0 = re-apply the current pose without advancing any blend. */
	void ApplyOffset(float Offset, float DeltaTime);
	void OnArmsBonesFinalized();
	void ApplyAimPose(USceneComponent* Arms, const FVector& Eye, const FQuat& ViewQuat);
	void CacheAimAxes(const USkeletalMeshComponent* GunMesh, const FVector& ViewUp);

	TWeakObjectPtr<USceneComponent> LeanTarget;
	TWeakObjectPtr<USceneComponent> CameraComponent;
	FVector BaseRelativeLocation = FVector::ZeroVector;
	FRotator BaseRelativeRotation = FRotator::ZeroRotator;
	FVector CameraBaseRelativeLocation = FVector::ZeroVector;
	float CurrentOffset = 0.f;
	float AppliedCameraRoll = 0.f;

	float StandEyeHeight = -1.f;
	float CurrentCrouchDrop = 0.f;
	/** Last dive stand-up seen, and the view drop it started from. */
	double SeenStandTime = -100.0;
	double SeenSlideEndTime = -100.0;
	double SeenCrouchStartTime = -100.0;
	double SeenCrouchEndTime = -100.0;
	float CrouchDownStartDrop = 0.f;
	float StandUpStartDrop = 0.f;

	float AimAlpha = 0.f;
	float SprintPoseAlpha = 0.f;
	/** Linear progress of the sprint pose blend (0..1), eased into SprintPoseAlpha. */
	float SprintPoseProgress = 0.f;
	float SprintBobPhase = 0.f;
	bool bLeftArmHidden = false;
	void SetLeftArmHidden(bool bHidden);
	/** Hides FirstPersonHiddenBones on the arms once their mesh is ready. */
	void HideFirstPersonBones();
	bool bFirstPersonBonesHidden = false;
	/** Climbing onto a ledge: puts both hands on the edge (two-bone IK on the rendered arms pose) and
	 *  hides the rifle meanwhile. Rebuilt from the animated local pose, so it can run more than once a frame. */
	void ApplyMantleHandsIK();
	/** Air: pause the arms animation (holds the take-off walking pose). */
	void ApplyAirArmPose();
	/** When the last jump was pressed (world time): the arms animation is paused from that moment. */
	double AirPauseRequestTime = -100.0;
public:
	/** Jump pressed: pause the arms animation right away, before the jump even starts, so the animation
	 *  never sees a single frame in the air (bunny hops land and take off again within a frame or two). */
	void NotifyJumpPressed();
private:
	float AirArmAlpha = 0.f;
	bool bAirArmWanted = false;
	bool bWeaponHiddenForMantle = false;
	/** Writes the sprint visibility mask into the arms' bone visibility (both buffers). */
	void ApplySprintBoneMask();
	/** Arms bone visibility before the sprint mask, restored afterwards. */
	TArray<uint8> SavedBoneVisibility;
	/** Arms bone visibility while sprinting: only SprintVisibleBones and their children are visible. */
	TArray<uint8> SprintBoneMask;
	/** Kept bone the hidden part of the body is collapsed onto (inside the shoulder), INDEX_NONE if none. */
	int32 SprintCollapseBoneIndex = INDEX_NONE;
	float BaseFieldOfView = 90.f;
	bool bAimAxesValid = false;
	bool bAimDistanceCached = false;
	bool bHasRestEye = false;
	bool bHasTicked = false;
	bool bHasSmoothedGun = false;
	bool bHasRestGun = false;
	FTransform RestGunInView = FTransform::Identity;
	FTransform SmoothedGunInView = FTransform::Identity;
	bool bApplyingOffset = false;
	/** How long the character has been standing still (s). */
	float StillTime = 0.f;
	/** 0..1: how much the view follows the rifle's tilt while diving / lying down. */
	float ProneViewBlend = 0.f;
	FDelegateHandle BonesFinalizedHandle;
	FVector RestEyeInArms = FVector::ZeroVector;
	float AimMuzzleDistance = 0.f;
	FVector AimLocalForward = FVector::ForwardVector;
	FVector AimLocalUp = FVector::UpVector;
	int32 LastLoggedSide = 0;
};
