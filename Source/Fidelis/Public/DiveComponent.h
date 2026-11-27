#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DiveComponent.generated.h"

class ACharacter;
class APlayerController;
class UCharacterMovementComponent;
class UInputAction;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EFidelisDiveState : uint8
{
	None,
	Airborne,	// launched, in the air
	Prone,		// landed, lying down while the dive key is held
	Rolling		// dive key then crouch key: ground roll in the movement direction
};

/** Direction of the last dive, relative to where the player was looking. */
UENUM(BlueprintType)
enum class EFidelisDiveDirection : uint8
{
	None,		// lying down on the spot, no dive
	Forward,
	Backward,
	Left,
	Right
};

/**
 * Omnidirectional dive: while running (in any direction, backwards included), press the dive key
 * (IA_Dive) to dive in the direction you are moving: forward, sideways or backward.
 * The dive always ends fully lying down (bAlwaysLandProne): with the dive key released, the character
 * gets back up on its own after ProneAfterDiveTime; with the key held, it stays down until it is released.
 * Not running: the same key lies the character down on the spot while it is held (bProneInPlace).
 * View per direction: forward = hands thrown forward to catch the fall, sideways = the view tilts
 * to that side, backward = lands on the back with the legs visible in front of the camera.
 * (The arms pose and the view tilt are applied by the LeanComponent, which owns the first-person view.)
 *
 * Every tuning value is EditAnywhere: add a "Dive Component" to BP_FirstPersonCharacter to tweak it
 * in the editor (the runtime-added default is only used when the Blueprint has none).
 */
UCLASS(ClassGroup = (Fidelis), meta = (BlueprintSpawnableComponent))
class FIDELIS_API UDiveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDiveComponent();

	UFUNCTION(BlueprintPure, Category = "Dive")
	EFidelisDiveState GetDiveState() const { return State; }

	/** Time the character last got back up from a dive (prone or roll), in world seconds. */
	double GetLastStandTime() const { return LastStandTime; }
	/** When the last roll ended on its feet (world time). */
	double GetLastRollEndTime() const { return LastRollEndTime; }
	/** View roll (degrees, view-space roll) the current / last roll ends on (0 = level). */
	float GetRollEndLean() const { return RollEndLean; }

	/** Getting back up after lying down / rolling: 0 = just started, 1 = back on its feet. */
	float GetStandUpAlpha() const;

	/** True while airborne or prone: other movement abilities should not interfere. */
	UFUNCTION(BlueprintPure, Category = "Dive")
	bool IsBusy() const { return State != EFidelisDiveState::None; }

	/** Direction of the current / last dive. */
	UFUNCTION(BlueprintPure, Category = "Dive")
	EFidelisDiveDirection GetDiveDirection() const { return DiveDir; }

	/** Lying on the back after a backward dive. */
	UFUNCTION(BlueprintPure, Category = "Dive")
	bool IsOnBack() const { return State == EFidelisDiveState::Prone && DiveDir == EFidelisDiveDirection::Backward; }

	/** Current view tilt from a side dive, in degrees (positive = right). */
	UFUNCTION(BlueprintPure, Category = "Dive|View")
	float GetViewRoll() const { return CurrentViewRoll; }

	/** Current rifle tilt while lying on the side after a side dive, in degrees (positive = right). */
	UFUNCTION(BlueprintPure, Category = "Dive|View")
	float GetWeaponCant() const { return CurrentWeaponCant; }

	/** Forward-dive "hands out to catch the fall" blend, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dive|View")
	float GetHandsReachAlpha() const { return HandsReachAlpha; }

	/** View rotation of the roll, in view space (identity when not rolling). Applied to the camera by
	 *  UDiveCameraModifier and to the arms by the LeanComponent. */
	FQuat GetRollViewRotation() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dive|Input")
	TObjectPtr<UInputAction> DiveAction;

	/** Crouch action: pressed shortly after the dive key, it turns the dive into a roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dive|Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** Only allow diving while running (sprint held, see SlideComponent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions")
	bool bRequireSprint = true;

	/** A dive always lands fully lying down, even with the dive key released in the air: the character
	 *  then gets back up on its own after ProneAfterDiveTime (held key: stays down until released).
	 *  Off: the dive only ends lying down if the key is still held on landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions")
	bool bAlwaysLandProne = true;

	/** Dive key released: time lying still once the belly slide has slowed down to crawl speed, before
	 *  getting back up on its own (s). Standing up while still sliding fast would stop the body dead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions", meta = (ClampMin = 0, EditCondition = "bAlwaysLandProne"))
	float ProneAfterDiveTime = 0.2f;

	/** Dive key pressed while NOT running: lie down on the spot (stays down while the key is held). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions")
	bool bProneInPlace = true;

	/** Minimum horizontal speed to dive (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions", meta = (ClampMin = 0))
	float MinSpeedToDive = 300.f;

	/** Time taken to get back up after lying down or rolling (s): the view rises over it and the
	 *  character can't run until it is over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions", meta = (ClampMin = 0))
	float StandUpDuration = 0.45f;

	/** Delay after standing up before another dive (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Conditions", meta = (ClampMin = 0))
	float DiveCooldown = 0.5f;

	/** Horizontal launch speed in the movement direction (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveHorizontalSpeed = 1500.f;

	/** Vertical launch speed (cm/s): higher = longer, higher dive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveUpSpeed = 450.f;

	/** Time in the air on flat ground (s). When > 0, the vertical launch speed is computed from it and
	 *  the character's gravity, and DiveUpSpeed is ignored. Distance ~= DiveHorizontalSpeed * DiveAirTime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveAirTime = 0.45f;

	/** Distance covered before touching the ground on flat ground (cm). When > 0 (with DiveAirTime > 0),
	 *  the horizontal launch speed is computed from it and DiveHorizontalSpeed is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveDistance = 700.f;

	/** Extra dive distance when looking up (fraction: 0.1 = +10%), reached at DiveLookUpPitch and
	 *  kept above it; scales in from the horizon. Same time in the air, so a slightly faster dive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveLookUpDistanceBonus = 0.1f;

	/** Forward dive while looking below the horizon: the dive lands where the player looks (on the
	 *  ground within DiveDistance), or heads down along the view if nothing is in reach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch")
	bool bDiveWhereLookingDown = true;

	/** Shortest dive when aiming at the ground close by (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0, EditCondition = "bDiveWhereLookingDown"))
	float DiveMinAimDistance = 150.f;

	/** View pitch above the horizon giving the full DiveLookUpDistanceBonus (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0.1, ClampMax = 89))
	float DiveLookUpPitch = 15.f;

	/** Air control during the dive (0 = locked trajectory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0, ClampMax = 1))
	float DiveAirControl = 0.f;

	/** The air braking (character's BrakingDecelerationFalling) never takes the dive's horizontal speed
	 *  below this (cm/s): a dive from a height keeps moving forward instead of dropping straight down.
	 *  Shorter dives (landing faster than this) are unchanged. 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Launch", meta = (ClampMin = 0))
	float DiveMinAirSpeed = 656.f;
	/** Capsule half height while lying down (cm). Clamped to the capsule radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float ProneHalfHeight = 34.f;

	/** Crawl speed while lying down, in every direction (forward, backward, sideways, diagonals) (cm/s). 0 = stays in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float ProneMoveSpeed = 150.f;

	/** Ground friction applied on landing (how fast the body stops sliding on its belly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float LandingFriction = 4.f;

	/** Braking deceleration after landing (cm/s²). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Prone", meta = (ClampMin = 0))
	float LandingBraking = 1200.f;

	/** Enables the roll combo (dive key, then crouch key while running). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll")
	bool bEnableRoll = true;

	/** Time after the dive key during which the crouch key triggers a roll (s). 0 = the whole dive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollComboWindow = 0.f;

	/** The player's view turns with the body during the roll (360 degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll")
	bool bRollTurnsView = true;

	/** Number of full turns of the view during a roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0, EditCondition = "bRollTurnsView"))
	float RollViewTurns = 1.f;

	/** When the roll turns fastest, as a fraction of the roll (0.5 = in the middle, small = right at the
	 *  start). The turning speed builds up to that moment, then slows down to 0 at the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0.01, ClampMax = 0.99, EditCondition = "bRollTurnsView"))
	float RollViewPeakTime = 0.15f;

	/** Side dive roll followed by a slide (standing or crouched): the roll stops turning short of level,
	 *  leaning to the side opposite to where it started, by the dive's view tilt (SideDiveRollAngle); the
	 *  slide then straightens up. Not when the dive key is held (lying down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (EditCondition = "bRollTurnsView"))
	bool bRollEndsLeaning = true;

	/** The roll starts at the speed the dive lands with (if faster than RollStartSpeed), so landing
	 *  doesn't brake the character. The dive itself always keeps its full trajectory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll")
	bool bRollKeepsLandingSpeed = false;

	/** Duration of the roll (s). When > 0, the roll lasts exactly this long (speed going from the start
	 *  speed to RollEndSpeed) and RollDistance is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollTime = 0.7f;

	/** Distance covered by the roll on flat ground (cm). Only used when RollTime is 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollDistance = 350.f;

	/** Speed at the start of the roll (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollStartSpeed = 881.f;

	/** Speed at the end of the roll (cm/s), before standing up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollEndSpeed = 881.f;

	/** Steering during the roll: turning the view left / right bends the roll the same way, up to this
	 *  angle from its starting direction (degrees). 0 = straight roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0, ClampMax = 180))
	float RollMaxTurnAngle = 45.f;

	/** Capsule half height while rolling (cm). Clamped to the capsule radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|Roll", meta = (ClampMin = 0))
	float RollHalfHeight = 45.f;

	// ---- View: forward dive (hands out) ----

	/** Arms pushed forward along the view at full reach (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Forward")
	float HandsReachForward = 30.f;

	/** Arms lowered at full reach (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Forward")
	float HandsReachDown = 12.f;

	/** Arms tilted down towards the ground at full reach (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Forward", meta = (ClampMin = -90, ClampMax = 90))
	float HandsReachPitch = 35.f;

	/** Speed of the hands going out (during the dive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Forward", meta = (ClampMin = 0))
	float HandsReachInSpeed = 14.f;

	/** Speed of the hands coming back (after landing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Forward", meta = (ClampMin = 0))
	float HandsReachOutSpeed = 5.f;

	// ---- View: side dive (view tilt) ----

	/** View tilt towards the dive side while in the air (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Side", meta = (ClampMin = 0, ClampMax = 90))
	float SideDiveRollAngle = 35.f;

	/** Lying on the side, the view tilts exactly like the rifle (SideProneWeaponCant), so the sights
	 *  stay upright on screen. Off: the view keeps SideProneRollAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Side")
	bool bSideProneViewFollowsWeapon = true;

	/** View tilt kept while lying on the side after landing (degrees), when bSideProneViewFollowsWeapon
	 *  is off. 0 = levels out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Side", meta = (ClampMin = 0, ClampMax = 90, EditCondition = "!bSideProneViewFollowsWeapon"))
	float SideProneRollAngle = 20.f;

	/** Rifle tilt towards the dive side while lying on the side after landing (degrees, around the line
	 *  of sight). 0 = stays upright. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Side", meta = (ClampMin = 0, ClampMax = 90))
	float SideProneWeaponCant = 45.f;

	/** Time to roll over from one side to the other while crawling sideways (s): the view and the
	 *  rifle level out halfway, then tilt to the new side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Side", meta = (ClampMin = 0))
	float SideSwitchDuration = 0.6f;

	/** Speed of the view tilt (and of the rifle tilt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Side", meta = (ClampMin = 0))
	float ViewRollInterpSpeed = 8.f;

	// ---- View: backward dive (on the back, legs visible) ----

	/** Show the character's legs in front of the camera while lying on the back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back")
	bool bShowLegsOnBack = true;

	/** Camera pitch set on landing on the back so the feet are in view (degrees, negative = down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back", meta = (ClampMin = -89, ClampMax = 89))
	float BackLandingPitch = -25.f;

	/** Bone hidden (with everything above it) on the legs copy of the body mesh: only the legs remain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back")
	FName LegsHiddenBone = TEXT("spine_01");

	/** Distance from the eye to the soles of the feet along the body (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back", meta = (ClampMin = 0))
	float LegsDistanceFromEye = 160.f;

	/** Height of the legs' axis above the floor while lying on the back (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back")
	float LegsHeightAboveGround = 10.f;

	/** Lying down (whatever the dive), the body keeps its orientation and the view turning away from it
	 *  rolls it over: 90 degrees -> on a flank, 180 -> belly <-> back (feet in view on the back).
	 *  Off: only after a backward dive; otherwise crawling sideways picks the flank. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back")
	bool bProneBodyFollowsView = true;

	/** On the back, moving towards the feet turns the character over onto the belly to crawl: time of that
	 *  turn (s), the legs sweeping round to behind while the body rolls over. 0 = instant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back", meta = (ClampMin = 0))
	float TurnOverToCrawlDuration = 0.4f;

	/** Lying on the back, turning the view this far left / right away from the feet (degrees) rolls the
	 *  character onto that flank (view and rifle tilted to that side, like after a side dive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back", meta = (ClampMin = 0, ClampMax = 180))
	float BackToSideTurnAngle = 45.f;

	/** Lying on the back, turning the view this far away from the feet (degrees) rolls the character
	 *  over onto the belly; turning back towards the feet rolls it onto its flank, then its back again.
	 *  0 = never rolls over (stays on the back whatever the view). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dive|View|Back", meta = (ClampMin = 0, ClampMax = 180))
	float BackToBellyTurnAngle = 135.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool IsActionHeld(APlayerController* PC, const UInputAction* Action) const;
	bool CanStartDive() const;
	bool CanGoProneInPlace() const;
	void SaveMovementSettings();
	void StartDive();
	void Land(bool bKeyHeld);
	void StandUp(const TCHAR* Reason);
	void RestoreMovementSettings();
	/** SetIgnoreMoveInput is a counter on the controller: only call it when our lock actually changes. */
	void SetMoveLocked(bool bLocked);
	void StartRoll();
	void TickRoll(float DeltaTime);
	/** Ends the roll lying down (dive key pressed during the roll, or held when it ends). */
	void ProneFromRoll(const TCHAR* Reason);
	bool bRollEndedOnGround = false;
	void EndRoll(const TCHAR* Reason);
	void UpdateDiveView(float DeltaTime);
	void UpdateBackLegs();

	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;

	EFidelisDiveState State = EFidelisDiveState::None;
	bool bDiveWasHeld = false;
	bool bMoveLocked = false;
	bool bCrouchWasHeld = false;
	/** Lying down after a dive with the key released: gets back up on its own at ProneAutoStandTime. */
	bool bProneAutoStand = false;
	double ProneAutoStandTime = -1.0;	// < 0: the belly slide is not over yet
	double ProneLandTime = 0.0;
	bool bRollQueued = false;
	float RollSpeedAtStart = 850.f;
	FVector DiveDirection = FVector::ForwardVector;
	const TCHAR* DiveDirectionName = TEXT("avant");
	EFidelisDiveDirection DiveDir = EFidelisDiveDirection::None;
	float CurrentViewRoll = 0.f;
	float CurrentWeaponCant = 0.f;
	/** Side roll-over in progress: elapsed time (-1 = none) and the tilts it started from. */
	float SideSwitchElapsed = -1.f;
	float SideSwitchStartRoll = 0.f;
	float SideSwitchStartCant = 0.f;
	/** Lying on the back: world yaw the feet point to (the body stays put while the view turns). */
	float BackBodyYaw = 0.f;
	/** Roll steering: direction and view yaw when the roll started. */
	FVector RollBaseDirection = FVector::ForwardVector;
	float RollStartYaw = 0.f;
	/** Lying down after a backward dive: can roll between back and belly by turning the view. */
	bool bProneFromBack = false;
	/** Lying down after a backward dive: body roll around its length (0 = on the back, -90 = left
	 *  flank, +90 = right flank, 180 = belly), applied to the legs shown to the player. */
	float CurrentBodyRoll = 0.f;
	/** Lying down from a side slide: the slide carries on lying down, with the slide's own speed curve
	 *  (current speed -> the slide's end speed over the slide's time left). */
	float ProneSlideTimeLeft = 0.f;
	float ProneSlideElapsed = 0.f;
	float ProneSlideStartSpeed = 0.f;
	/** Time into the slide when lying down: the lying slide carries on along the slide's speed curve. */
	float ProneSlideFromElapsed = 0.f;
	TWeakObjectPtr<class USlideComponent> ProneSlideSource;
	float ProneSlideEndSpeed = 0.f;
	FVector ProneSlideDir = FVector::ForwardVector;
	void EndProneSlide(const TCHAR* Reason);
	/** Turning over from the back to crawl: elapsed time (-1 = none), legs yaw at the start, sweep side. */
	float TurnOverElapsed = -1.f;
	float TurnOverStartYaw = 0.f;
	float TurnOverSign = 1.f;
	float HandsReachAlpha = 0.f;

	/** Legs-only copy of the body mesh, shown to the player while lying on the back. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BackLegsMesh;
	float RollElapsed = 0.f;
	float RollDuration = 0.f;
	float AirTime = 0.f;
	double LastStandTime = -100.0;
	double LastRollEndTime = -100.0;
	/** Lean the roll ends on (smoothed, follows the keys held during the roll). */
	float RollEndLean = 0.f;

	// Saved movement settings, restored when standing up.
	float SavedCrouchedHalfHeight = 40.f;
	float SavedMaxWalkSpeedCrouched = 300.f;
	float SavedGroundFriction = 8.f;
	float SavedBrakingDeceleration = 2048.f;
	float SavedAirControl = 0.05f;
};
