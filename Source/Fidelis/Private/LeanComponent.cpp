#include "LeanComponent.h"

#include "HAL/IConsoleManager.h"

#include "AK47Weapon.h"
#include "Fidelis.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "DiveComponent.h"
#include "SlideComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

ULeanComponent::ULeanComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Run after every animation of the frame (arms walk / run bob included), so the bones can't move the
	// rifle or the camera again after the aim pose is applied, and before the camera manager reads the
	// camera transform at the end of the frame. Keeps the sights dead centre while moving.
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
		BaseRelativeRotation = Target->GetRelativeRotation();

		// The arms animation can finish after this component's tick and move the head bone (camera)
		// and the hand bone (rifle) again. Re-apply the pose as soon as the bones are final for the
		// frame, which is still before the camera manager reads the view.
		if (USkeletalMeshComponent* ArmsMesh = Cast<USkeletalMeshComponent>(Target))
		{
			BonesFinalizedHandle = ArmsMesh->RegisterOnBoneTransformsFinalizedDelegate(
				FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ULeanComponent::OnArmsBonesFinalized));
		}
	}
	if (USceneComponent* Camera = CameraComponent.Get())
	{
		CameraBaseRelativeLocation = Camera->GetRelativeLocation();
		if (const UCameraComponent* Cam = Cast<UCameraComponent>(Camera))
		{
			BaseFieldOfView = Cam->FieldOfView;
		}
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
	if (USkeletalMeshComponent* ArmsMesh = Cast<USkeletalMeshComponent>(LeanTarget.Get()); ArmsMesh && BonesFinalizedHandle.IsValid())
	{
		ArmsMesh->UnregisterOnBoneTransformsFinalizedDelegate(BonesFinalizedHandle);
		BonesFinalizedHandle.Reset();
	}
	SetLeftArmHidden(false);
	AimAlpha = 0.f;
	CurrentCrouchDrop = 0.f;
	bAdjustViewForCrouch = false;
	ApplyOffset(0.f, 0.f);
	if (UCameraComponent* Cam = Cast<UCameraComponent>(CameraComponent.Get()))
	{
		Cam->SetFieldOfView(BaseFieldOfView);
	}
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

AAK47Weapon* ULeanComponent::FindWeapon() const
{
	TArray<AActor*> Attached;
	GetOwner()->GetAttachedActors(Attached);
	for (AActor* Actor : Attached)
	{
		if (AAK47Weapon* Weapon = Cast<AAK47Weapon>(Actor))
		{
			return Weapon;
		}
	}
	return nullptr;
}

bool ULeanComponent::IsFireHeld(APlayerController* PC) const
{
	const AAK47Weapon* Weapon = FindWeapon();
	return Weapon && IsActionHeld(PC, Weapon->GetFireAction());
}

void ULeanComponent::CacheAimAxes(const USkeletalMeshComponent* GunMesh, const FVector& ViewUp)
{
	// Bore axis = the rifle's local axis that points at the muzzle (the barrel dominates the offset).
	const FVector LocalMuzzle = GunMesh->GetSocketTransform(MuzzleSocketName, RTS_Component).GetLocation();
	const FVector Abs = LocalMuzzle.GetAbs();
	const int32 ForwardAxis = (Abs.X >= Abs.Y && Abs.X >= Abs.Z) ? 0 : (Abs.Y >= Abs.Z ? 1 : 2);
	AimLocalForward = FVector::ZeroVector;
	AimLocalForward[ForwardAxis] = LocalMuzzle[ForwardAxis] < 0.f ? -1.f : 1.f;

	// Up = the remaining local axis closest to the view's up while the rifle is held at the hip.
	const FTransform GunXf = GunMesh->GetComponentTransform();
	float BestDot = -2.f;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Axis == ForwardAxis)
		{
			continue;
		}
		for (const float Sign : { 1.f, -1.f })
		{
			FVector Local = FVector::ZeroVector;
			Local[Axis] = Sign;
			const float Dot = FVector::DotProduct(GunXf.TransformVectorNoScale(Local), ViewUp);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				AimLocalUp = Local;
			}
		}
	}
	bAimAxesValid = true;
	UE_LOG(LogFidelis, Log, TEXT("Aim: axes du fusil -> avant %s, haut %s (muzzle local %s)."),
		*AimLocalForward.ToString(), *AimLocalUp.ToString(), *LocalMuzzle.ToString());
}

void ULeanComponent::ApplyAimPose(USceneComponent* Arms, const FVector& Eye, const FQuat& ViewQuat)
{
	const AAK47Weapon* Weapon = FindWeapon();
	const USkeletalMeshComponent* GunMesh = Weapon ? Weapon->GetFirstPersonMesh() : nullptr;
	if (!GunMesh || !GunMesh->DoesSocketExist(MuzzleSocketName))
	{
		return;
	}

	const FVector ViewFwd = ViewQuat.GetForwardVector();
	const FVector ViewUp = ViewQuat.GetUpVector();
	if (!bAimAxesValid)
	{
		CacheAimAxes(GunMesh, ViewUp);
	}

	const FTransform GunXf = GunMesh->GetComponentTransform();
	const FVector Muzzle = GunMesh->GetSocketLocation(MuzzleSocketName);

	// Distance from the eye to the muzzle along the line of sight, taken once from the hip pose at
	// rest. Fixed afterwards so the walk / run animation can't push the rifle back and forth.
	// Not from the current pose: aiming straight out of a sprint, the rifle is still raised upright
	// and the muzzle would end up a few cm from the eye, the sights behind the camera (invisible).
	if (!bAimDistanceCached)
	{
		if (bHasRestGun)
		{
			const FVector LocalMuzzleSocket = GunMesh->GetSocketTransform(MuzzleSocketName, RTS_Component).GetLocation();
			AimMuzzleDistance = RestGunInView.TransformPosition(LocalMuzzleSocket).X;
			bAimDistanceCached = true;
		}
		else if (SprintPoseAlpha <= KINDA_SMALL_NUMBER)
		{
			AimMuzzleDistance = FVector::DotProduct(Muzzle - Eye, ViewFwd);
			bAimDistanceCached = true;
		}
		else
		{
			AimMuzzleDistance = 60.f;
		}
		if (bAimDistanceCached)
		{
			UE_LOG(LogFidelis, Log, TEXT("Aim: bouche du canon a %.1f cm de l'oeil en visee."), AimMuzzleDistance + AimDistanceOffset);
		}
	}

	// Rifle pose fully locked to the view: bore along the line of sight, upright, muzzle at a fixed
	// distance just below the eye. Built from scratch every frame, so whatever the arms animation does
	// (walk / run bob, sway), the rifle doesn't move on screen while aiming.
	const FQuat LocalBasis = FRotationMatrix::MakeFromXZ(AimLocalForward, AimLocalUp).ToQuat();
	const FQuat ViewBasis = FRotationMatrix::MakeFromXZ(ViewFwd, ViewUp).ToQuat();
	const FQuat TargetGunRot = ViewBasis * LocalBasis.Inverse();
	const FVector LocalMuzzle = GunXf.InverseTransformPosition(Muzzle) * GunXf.GetScale3D();
	const FVector TargetMuzzle = Eye + ViewFwd * (AimMuzzleDistance + AimDistanceOffset) - ViewUp * AimSightHeight;
	const FTransform TargetGunXf(TargetGunRot, TargetMuzzle - TargetGunRot.RotateVector(LocalMuzzle), GunXf.GetScale3D());

	// Move the arms (which carry the rifle) by the same correction, then blend with the hip pose.
	const FTransform ArmsXf = Arms->GetComponentTransform();
	const FTransform AimedArmsXf = ArmsXf.GetRelativeTransform(GunXf) * TargetGunXf;
	if (AimAlpha >= 1.f)
	{
		Arms->SetWorldLocationAndRotation(AimedArmsXf.GetLocation(), AimedArmsXf.GetRotation());
	}
	else
	{
		Arms->SetWorldLocationAndRotation(
			FMath::Lerp(ArmsXf.GetLocation(), AimedArmsXf.GetLocation(), AimAlpha),
			FQuat::Slerp(ArmsXf.GetRotation(), AimedArmsXf.GetRotation(), AimAlpha));
	}
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

static TAutoConsoleVariable<int32> CVarFidelisLeanReapply(TEXT("fidelis.LeanReapply"), 1,
	TEXT("1 = re-apply the first-person pose once the arms animation is final for the frame (default)."));

void ULeanComponent::SetLeftArmHidden(bool bHidden)
{
	if (bHidden == bLeftArmHidden)
	{
		return;
	}
	bLeftArmHidden = bHidden;
	USkeletalMeshComponent* Arms = Cast<USkeletalMeshComponent>(LeanTarget.Get());
	if (!Arms || !Arms->GetSkinnedAsset() || Arms->GetNumBones() == 0)
	{
		SavedBoneVisibility.Reset();
		SprintBoneMask.Reset();
		return;
	}

	if (bHidden)
	{
		// HideBone hides a bone with all its children, which can't keep the right arm while hiding the
		// spine it hangs from: build a per-bone mask instead (the renderer reads each bone's state alone).
		SavedBoneVisibility = Arms->GetEditableBoneVisibilityStates();
		const int32 NumBones = SavedBoneVisibility.Num();
		const FReferenceSkeleton& RefSkel = Arms->GetSkinnedAsset()->GetRefSkeleton();
		TArray<bool> Kept;
		Kept.Init(false, NumBones);
		int32 NumKeptRoots = 0;
		SprintCollapseBoneIndex = INDEX_NONE;
		for (const FName& Bone : SprintVisibleBones)
		{
			const int32 Index = Arms->GetBoneIndex(Bone);
			if (Index != INDEX_NONE && Index < NumBones)
			{
				Kept[Index] = true;
				++NumKeptRoots;
				if (SprintCollapseBoneIndex == INDEX_NONE && Index > 0)
				{
					SprintCollapseBoneIndex = Index;
				}
			}
		}
		if (NumKeptRoots == 0)
		{
			UE_LOG(LogFidelis, Warning, TEXT("Sprint: aucun os de SprintVisibleBones trouve sur %s, le corps reste visible."),
				*GetNameSafe(Arms));
			SavedBoneVisibility.Reset();
			SprintBoneMask.Reset();
			return;
		}
		// Parents always come before their children in the reference skeleton.
		SprintBoneMask.SetNumUninitialized(NumBones);
		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			const int32 Parent = RefSkel.GetParentIndex(Index);
			Kept[Index] = Kept[Index] || (Parent != INDEX_NONE && Parent < NumBones && Kept[Parent]);
			SprintBoneMask[Index] = Kept[Index]
				? (SavedBoneVisibility[Index] == BVS_ExplicitlyHidden ? uint8(BVS_ExplicitlyHidden) : uint8(BVS_Visible))
				: uint8(BVS_ExplicitlyHidden);
		}
		ApplySprintBoneMask();
	}
	else
	{
		if (SavedBoneVisibility.Num() == Arms->GetEditableBoneVisibilityStates().Num())
		{
			Arms->GetEditableBoneVisibilityStates() = SavedBoneVisibility;
			const_cast<TArray<uint8>&>(Arms->GetBoneVisibilityStates()) = SavedBoneVisibility;
		}
		Arms->RebuildVisibilityArray();
		Arms->MarkRenderDynamicDataDirty();
		SavedBoneVisibility.Reset();
		SprintBoneMask.Reset();
	}
	UE_LOG(LogFidelis, Log, TEXT("Sprint: fusil %s."), bHidden ? TEXT("leve, seul le bras droit est visible") : TEXT("tenu a deux mains"));
}

void ULeanComponent::ApplySprintBoneMask()
{
	USkeletalMeshComponent* Arms = Cast<USkeletalMeshComponent>(LeanTarget.Get());
	if (!bLeftArmHidden || !Arms || SprintBoneMask.Num() == 0)
	{
		return;
	}
	// The engine can rebuild the visibility from the parents (LOD change, buffer flip): re-write it in
	// both buffers every frame. Skipped if the bone count changed (mesh swapped).
	TArray<uint8>& Editable = Arms->GetEditableBoneVisibilityStates();
	TArray<uint8>& Read = const_cast<TArray<uint8>&>(Arms->GetBoneVisibilityStates());
	if (Editable.Num() == SprintBoneMask.Num())
	{
		Editable = SprintBoneMask;
	}
	if (&Read != &Editable && Read.Num() == SprintBoneMask.Num())
	{
		Read = SprintBoneMask;
	}

	// The renderer collapses every hidden bone onto the translation of its first visible ancestor, here
	// the skeleton root (at the feet): shoulder vertices partly skinned to the hidden spine then stretch
	// down towards it. Move the root (no vertex of its own) inside the shoulder so they only shrink a
	// little there. Only the rendered pose is touched; the next animation update rewrites it.
	if (SprintCollapseBoneIndex != INDEX_NONE && SprintBoneMask.IsValidIndex(0) && SprintBoneMask[0] != BVS_Visible)
	{
		TArray<FTransform>& Pose = const_cast<TArray<FTransform>&>(Arms->GetComponentSpaceTransforms());
		if (Pose.IsValidIndex(SprintCollapseBoneIndex) && Pose.Num() == SprintBoneMask.Num())
		{
			Pose[0].SetTranslation(Pose[SprintCollapseBoneIndex].GetTranslation());
		}
	}
	Arms->MarkRenderDynamicDataDirty();
}

void ULeanComponent::OnArmsBonesFinalized()
{
	if (bHasTicked && !bApplyingOffset && CVarFidelisLeanReapply.GetValueOnGameThread() != 0)
	{
		ApplyOffset(CurrentOffset, -1.f);
	}
	ApplySprintBoneMask();
}

void ULeanComponent::ApplyOffset(float Offset, float DeltaTime)
{
	// Moving the arms below can itself refresh their bones and fire OnArmsBonesFinalized: no recursion.
	TGuardValue<bool> ApplyingGuard(bApplyingOffset, true);
	USceneComponent* Target = LeanTarget.Get();
	AActor* Owner = GetOwner();
	if (!Target || !Owner)
	{
		return;
	}
	const float Alpha = LeanDistance > 0.f ? Offset / LeanDistance : 0.f;

	// Standing still for a moment (not just passing through zero speed while switching direction,
	// e.g. left / right strafing): only then are the resting eye / rifle poses re-captured. Capturing
	// them mid-stride snapped the view and the rifle to a running pose (a jolt on every turn).
	if (DeltaTime > 0.f)
	{
		StillTime = Owner->GetVelocity().Size2D() < 5.f ? StillTime + DeltaTime : 0.f;
	}
	const bool bSettled = StillTime >= 0.25f;

	// 1) Lean along the character's right vector, converted into the target's parent space.
	//    Always rebuilt from the base transform so the cant below never accumulates.
	const FVector WorldOffset = Owner->GetActorRightVector() * Offset;
	FVector LocalOffset = WorldOffset;
	if (const USceneComponent* Parent = Target->GetAttachParent())
	{
		LocalOffset = Parent->GetComponentTransform().InverseTransformVector(WorldOffset);
	}
	Target->SetRelativeLocationAndRotation(BaseRelativeLocation + LocalOffset, BaseRelativeRotation);

	// The camera rides on the arms mesh (head socket): reset it, then read the eye position this
	// frame. The aim pose and the cant below move the arms only; the camera is put back on Eye.
	USceneComponent* Camera = CameraComponent.Get();
	const bool bSeparateCamera = Camera && Camera != Target;
	if (bSeparateCamera)
	{
		Camera->SetRelativeLocation(CameraBaseRelativeLocation);
	}

	// Steady eye for aiming: the camera rides on the arms' head bone, so the walk / run animation bobs
	// it. Remember where the eye sits on the arms while the character stands still (not aiming), and
	// use that fixed point while aiming so neither the view nor the rifle moves.
	const bool bCameraRidesArms = bSeparateCamera && Camera->IsAttachedTo(Target);
	if (bCameraRidesArms && AimAlpha <= 0.f)
	{
		if (bSettled || !bHasRestEye)
		{
			RestEyeInArms = Target->GetComponentTransform().InverseTransformPosition(Camera->GetComponentLocation());
			bHasRestEye = true;
		}
	}

	// 1b) Crouch / prone: lower arms + camera so the eye height follows the capsule height.
	const ACharacter* Char = Cast<ACharacter>(Owner);
	const UCapsuleComponent* Capsule = Char ? Char->GetCapsuleComponent() : nullptr;
	float TargetDrop = 0.f;
	if (bAdjustViewForCrouch && Capsule)
	{
		const float StandHalfHeight = Char->GetDefaultHalfHeight();
		const float CurHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const float FloorZ = Capsule->GetComponentLocation().Z - CurHalfHeight;
		const float NaturalEyeZ = (Camera ? Camera : Target)->GetComponentLocation().Z;
		if (!Char->bIsCrouched)
		{
			StandEyeHeight = NaturalEyeZ - FloorZ;
		}
		else if (StandEyeHeight > 0.f && StandHalfHeight > 0.f)
		{
			const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>();
			const bool bProne = Dive && Dive->GetDiveState() == EFidelisDiveState::Prone;
			const float Ratio = bProne ? ProneViewHeightRatio : CrouchViewHeightRatio;
			const float WantedEyeZ = FloorZ + StandEyeHeight * Ratio;
			TargetDrop = FMath::Max(0.f, NaturalEyeZ - WantedEyeZ);
		}
	}
	// DeltaTime < 0: re-application after the arms animation of this frame, keep the current drop.
	if (DeltaTime >= 0.f)
	{
		// Getting back up after lying down / rolling: the view rises over the dive's StandUpDuration.
		const UDiveComponent* StandDive = Char ? Char->FindComponentByClass<UDiveComponent>() : nullptr;
		if (StandDive && StandDive->GetLastStandTime() != SeenStandTime)
		{
			SeenStandTime = StandDive->GetLastStandTime();
			StandUpStartDrop = CurrentCrouchDrop;
		}
		const USlideComponent* StandSlide = Char ? Char->FindComponentByClass<USlideComponent>() : nullptr;
		if (StandSlide && StandSlide->GetLastSlideEndTime() != SeenSlideEndTime)
		{
			SeenSlideEndTime = StandSlide->GetLastSlideEndTime();
			StandUpStartDrop = CurrentCrouchDrop;
		}
		float StandAlpha = (StandDive && !StandDive->IsBusy()) ? StandDive->GetStandUpAlpha() : 1.f;
		if (StandSlide && StandSlide->GetLastCrouchEndTime() != SeenCrouchEndTime)
		{
			SeenCrouchEndTime = StandSlide->GetLastCrouchEndTime();
			StandUpStartDrop = CurrentCrouchDrop;
		}
		if (StandSlide && !StandSlide->IsSliding() && !(StandDive && StandDive->IsBusy()))
		{
			StandAlpha = FMath::Min(StandAlpha, FMath::Min(StandSlide->GetStandUpAlpha(), StandSlide->GetCrouchStandUpAlpha()));
		}
		// Plain crouch: the view goes down over CrouchDownDuration, eased.
		float CrouchAlpha = 1.f;
		if (StandSlide && CrouchDownDuration > 0.f)
		{
			if (StandSlide->GetLastCrouchStartTime() != SeenCrouchStartTime)
			{
				SeenCrouchStartTime = StandSlide->GetLastCrouchStartTime();
				CrouchDownStartDrop = CurrentCrouchDrop;
			}
			if (const UWorld* World = GetWorld())
			{
				CrouchAlpha = FMath::Clamp(float(World->GetTimeSeconds() - SeenCrouchStartTime) / CrouchDownDuration, 0.f, 1.f);
			}
		}
		if (CrouchAlpha < 1.f && TargetDrop > CrouchDownStartDrop && !StandSlide->IsSliding())
		{
			CurrentCrouchDrop = FMath::Lerp(CrouchDownStartDrop, TargetDrop, FMath::SmoothStep(0.f, 1.f, CrouchAlpha));
		}
		else if (StandAlpha < 1.f && TargetDrop < StandUpStartDrop)
		{
			CurrentCrouchDrop = FMath::Lerp(StandUpStartDrop, TargetDrop, FMath::SmoothStep(0.f, 1.f, StandAlpha));
		}
		else
		{
			CurrentCrouchDrop = DeltaTime > 0.f ? FMath::FInterpTo(CurrentCrouchDrop, TargetDrop, DeltaTime, CrouchViewInterpSpeed) : TargetDrop;
		}
	}
	if (CurrentCrouchDrop > 0.01f)
	{
		Target->AddWorldOffset(FVector(0.f, 0.f, -CurrentCrouchDrop));
		if (bSeparateCamera && Camera->GetAttachParent() != Target)
		{
			// Camera not riding on the arms (custom setup): lower it too.
			Camera->AddWorldOffset(FVector(0.f, 0.f, -CurrentCrouchDrop));
		}
	}
	else
	{
		CurrentCrouchDrop = TargetDrop > 0.f ? CurrentCrouchDrop : 0.f;
	}

	FVector Eye = Camera ? Camera->GetComponentLocation() : Target->GetComponentLocation();
	const float SteadyWeight = bSteadyCamera ? 1.f : AimAlpha;
	if (bCameraRidesArms && bHasRestEye && SteadyWeight > 0.f)
	{
		// Arms already lowered for crouch / prone here, so the steady eye follows the height too.
		const FVector SteadyEye = Target->GetComponentTransform().TransformPosition(RestEyeInArms);
		Eye = FMath::Lerp(Eye, SteadyEye, SteadyWeight);
	}

	const APawn* Pawn = Char;
	FRotator ViewRot = Pawn ? Pawn->GetViewRotation() : Owner->GetActorRotation();
	ViewRot.Roll = 0.f;
	const FQuat ViewQuat = ViewRot.Quaternion();

	// 1c) Smooth weapon sway: the arms play a third-person run animation whose hand motion seen from
	//     the eye is jerky. Filter the rifle's pose relative to the view (critically damped follow),
	//     keeping a soft sway but no frame-to-frame jolts. Advanced once per frame (DeltaTime > 0);
	//     the re-application after the arms animation reuses the same filtered pose.
	if (const AAK47Weapon* SwayWeapon = FindWeapon(); bSeparateCamera && WeaponSwaySmoothing > 0.f && SwayWeapon && SwayWeapon->GetFirstPersonMesh())
	{
		const USkeletalMeshComponent* Gun = SwayWeapon->GetFirstPersonMesh();
		const FTransform ViewXf(ViewQuat, Eye);
		const FTransform GunXf = Gun->GetComponentTransform();
		const FTransform AnimInView = GunXf.GetRelativeTransform(ViewXf);

		// Resting pose (standing still, not aiming): the run animation only moves the rifle a fraction
		// (WeaponSwayAmount) of the way from it, so strafing no longer swings it across the screen.
		if (AimAlpha <= 0.f && (bSettled || !bHasRestGun))
		{
			RestGunInView = AnimInView;
			bHasRestGun = true;
		}
		FTransform RawInView = AnimInView;
		if (bHasRestGun && WeaponSwayAmount < 1.f)
		{
			RawInView.SetLocation(FMath::Lerp(RestGunInView.GetLocation(), AnimInView.GetLocation(), WeaponSwayAmount));
			RawInView.SetRotation(FQuat::Slerp(RestGunInView.GetRotation(), AnimInView.GetRotation(), WeaponSwayAmount).GetNormalized());
		}
		if (!bHasSmoothedGun)
		{
			SmoothedGunInView = RawInView;
			bHasSmoothedGun = true;
		}
		else if (DeltaTime > 0.f)
		{
			const float SwayAlpha = 1.f - FMath::Exp(-WeaponSwaySmoothing * DeltaTime);
			SmoothedGunInView.SetLocation(FMath::Lerp(SmoothedGunInView.GetLocation(), RawInView.GetLocation(), SwayAlpha));
			SmoothedGunInView.SetRotation(FQuat::Slerp(SmoothedGunInView.GetRotation(), RawInView.GetRotation(), SwayAlpha).GetNormalized());
			SmoothedGunInView.SetScale3D(RawInView.GetScale3D());
		}
		const FTransform TargetGunXf = SmoothedGunInView * ViewXf;
		const FTransform SmoothedArmsXf = Target->GetComponentTransform().GetRelativeTransform(GunXf) * TargetGunXf;
		Target->SetWorldLocationAndRotation(SmoothedArmsXf.GetLocation(), SmoothedArmsXf.GetRotation());
	}

	// 1d) Sprint pose (RoboCop style): rifle held upright in the right hand, muzzle to the sky at 90
	//     degrees to the torso, beside the head; the left arm is hidden. Built in torso space (view yaw
	//     only), so it doesn't follow the view pitch.
	if (const AAK47Weapon* SprintWeapon = FindWeapon(); bSeparateCamera && SprintPoseAlpha > KINDA_SMALL_NUMBER && SprintWeapon && SprintWeapon->GetFirstPersonMesh())
	{
		const USkeletalMeshComponent* Gun = SprintWeapon->GetFirstPersonMesh();
		if (!bAimAxesValid && Gun->DoesSocketExist(MuzzleSocketName))
		{
			CacheAimAxes(Gun, ViewQuat.GetUpVector());
		}
		if (bAimAxesValid)
		{
			const FQuat TorsoQuat = FRotator(0.f, ViewRot.Yaw, 0.f).Quaternion();
			// Bore straight up, top of the rifle towards the player (then twisted around the bore).
			const FVector TwistedBack = FQuat(FVector::UpVector, FMath::DegreesToRadians(SprintRifleTwist)).RotateVector(-TorsoQuat.GetForwardVector());
			const FQuat LocalBasis = FRotationMatrix::MakeFromXZ(AimLocalForward, AimLocalUp).ToQuat();
			const FQuat UprightBasis = FRotationMatrix::MakeFromXZ(FVector::UpVector, TwistedBack).ToQuat();
			const FQuat TargetGunRot = UprightBasis * LocalBasis.Inverse();
			const FTransform GunXf = Gun->GetComponentTransform();

			// Body movement: side sway once per stride, bounce and pitch on every step (two per stride),
			// scaled by how fast the character actually runs. Tilts pivot on the hand.
			const float SpeedRatio = FMath::Clamp(Owner->GetVelocity().Size2D() / 600.f, 0.f, 1.f);
			const float Stride = FMath::Sin(SprintBobPhase);
			const float Step = FMath::Sin(2.f * SprintBobPhase);
			const FVector BobOffset(0.f, SprintBobAmplitude.Y * Stride, SprintBobAmplitude.Z * Step);
			const FRotator BobTilt(SprintBobRotation.Pitch * Step, SprintBobRotation.Yaw * Stride, SprintBobRotation.Roll * Stride);
			const FQuat BobRot = TorsoQuat * (BobTilt * SpeedRatio).Quaternion() * TorsoQuat.Inverse();

			const FTransform TargetGunXf(BobRot * TargetGunRot,
				Eye + TorsoQuat.RotateVector(SprintGripOffset + BobOffset * SpeedRatio), GunXf.GetScale3D());

			const FTransform ArmsXf = Target->GetComponentTransform();
			const FTransform RaisedArmsXf = ArmsXf.GetRelativeTransform(GunXf) * TargetGunXf;
			Target->SetWorldLocationAndRotation(
				FMath::Lerp(ArmsXf.GetLocation(), RaisedArmsXf.GetLocation(), SprintPoseAlpha),
				FQuat::Slerp(ArmsXf.GetRotation(), RaisedArmsXf.GetRotation(), SprintPoseAlpha));
		}
	}

	// 2) Aim down sights: bring the rifle's bore onto the line of sight.
	if (bSeparateCamera && AimAlpha > KINDA_SMALL_NUMBER)
	{
		ApplyAimPose(Target, Eye, ViewQuat);
	}

	// 2b) Forward dive: arms thrown out and down to catch the fall, pivoting on the eye.
	const UDiveComponent* DiveComp = Char ? Char->FindComponentByClass<UDiveComponent>() : nullptr;
	if (DiveComp && DiveComp->GetHandsReachAlpha() > KINDA_SMALL_NUMBER)
	{
		const float Reach = DiveComp->GetHandsReachAlpha();
		const FQuat Tilt(ViewQuat.GetRightVector(), FMath::DegreesToRadians(DiveComp->HandsReachPitch * Reach));
		const FVector Push = (ViewQuat.GetForwardVector() * DiveComp->HandsReachForward
			- ViewQuat.GetUpVector() * DiveComp->HandsReachDown) * Reach;
		Target->SetWorldLocationAndRotation(
			Eye + Tilt.RotateVector(Target->GetComponentLocation() - Eye) + Push,
			Tilt * Target->GetComponentQuat());
	}

	// 2c) Dive roll: the arms turn with the view (the camera itself is turned by UDiveCameraModifier),
	//     pivoting on the eye so they stay in front of it the whole way round.
	if (DiveComp)
	{
		const FQuat RollLocal = DiveComp->GetRollViewRotation();
		if (!RollLocal.Equals(FQuat::Identity, 1.e-4f))
		{
			const FQuat RollWorld = ViewQuat * RollLocal * ViewQuat.Inverse();
			Target->SetWorldLocationAndRotation(
				Eye + RollWorld.RotateVector(Target->GetComponentLocation() - Eye),
				RollWorld * Target->GetComponentQuat());
		}
	}

	// 3) Cant arms + weapon around the line of sight, pivoting on the eye so the view position
	//    and the aim point don't move (the weapon stays in the hands since the whole arms mesh turns).
	//    Lying on the side after a side dive adds the dive's rifle tilt.
	const float WeaponRoll = Alpha * WeaponLeanRollAngle + (DiveComp ? DiveComp->GetWeaponCant() : 0.f);
	if (!FMath::IsNearlyZero(WeaponRoll, 0.01f))
	{
		// Roll expressed in view space, so it uses the same sign convention as the camera roll.
		const FQuat Cant = ViewQuat * FRotator(0.f, 0.f, WeaponRoll).Quaternion() * ViewQuat.Inverse();
		Target->SetWorldLocationAndRotation(
			Eye + Cant.RotateVector(Target->GetComponentLocation() - Eye),
			Cant * Target->GetComponentQuat());
	}

	if (bSeparateCamera)
	{
		Camera->SetWorldLocation(Eye);
	}

	// 4) Camera tilt through the control rotation roll (the first-person camera uses pawn control
	//    rotation). Roll doesn't change the aim direction, recoil keeps it.
	//    A side dive adds its own tilt on top of the lean.
	//    Looking through the sights, the view follows the rifle's tilt (lean / lying on the side) so the
	//    sights stay upright on screen and the view matches the angle the rifle fires at.
	//    Diving / lying down, the view follows the rifle's tilt, lean buttons included. Blended in and
	//    out (no snap when landing or standing up).
	const bool bDiving = DiveComp && (DiveComp->GetDiveState() == EFidelisDiveState::Prone
		|| DiveComp->GetDiveState() == EFidelisDiveState::Airborne);
	if (DeltaTime > 0.f)
	{
		ProneViewBlend = FMath::FInterpTo(ProneViewBlend, (bDiving && bProneViewFollowsWeaponCant) ? 1.f : 0.f, DeltaTime, 8.f);
	}
	const float FreeCameraRoll = FMath::Lerp(Alpha * CameraLeanRollAngle + (DiveComp ? DiveComp->GetViewRoll() : 0.f),
		WeaponRoll, ProneViewBlend);
	const float CameraRoll = bAimViewFollowsWeaponCant ? FMath::Lerp(FreeCameraRoll, WeaponRoll, AimAlpha) : FreeCameraRoll;
	if (!FMath::IsNearlyEqual(CameraRoll, AppliedCameraRoll, 0.001f))
	{
		if (APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
		{
			FRotator ControlRot = PC->GetControlRotation();
			ControlRot.Roll = CameraRoll;
			PC->SetControlRotation(ControlRot);
		}
		AppliedCameraRoll = CameraRoll;
	}
}

void ULeanComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;

	int32 Side = 0;
	if (PC && (!bLeanOnlyWhileFiring || IsFireHeld(PC)))
	{
		// bSwapLeanButtons: the button mapped to IA_LeanLeft leans right and vice versa.
		const bool bLeft = IsActionHeld(PC, bSwapLeanButtons ? LeanRightAction : LeanLeftAction);
		const bool bRight = IsActionHeld(PC, bSwapLeanButtons ? LeanLeftAction : LeanRightAction);
		Side = (bRight ? 1 : 0) - (bLeft ? 1 : 0); // both held -> stay centered
	}

	if (Side != LastLoggedSide)
	{
		UE_LOG(LogFidelis, Log, TEXT("Lean: %s"), Side < 0 ? TEXT("gauche") : Side > 0 ? TEXT("droite") : TEXT("centre"));
		LastLoggedSide = Side;
	}

	const float Desired = ClampOffsetAgainstWalls(Side * LeanDistance);
	const float NewOffset = FMath::FInterpTo(CurrentOffset, Desired, DeltaTime, LeanInterpSpeed);
	bool bChanged = !FMath::IsNearlyEqual(NewOffset, CurrentOffset, 0.01f) || (Desired == 0.f && CurrentOffset != 0.f);
	if (bChanged)
	{
		CurrentOffset = FMath::Abs(NewOffset) < 0.01f && Desired == 0.f ? 0.f : NewOffset;
	}

	// Sprint pose blend: running (auto sprint), on the ground, not aiming / diving / sliding.
	{
		const ACharacter* SprintChar = Cast<ACharacter>(GetOwner());
		const USlideComponent* Slide = SprintChar ? SprintChar->FindComponentByClass<USlideComponent>() : nullptr;
		const AAK47Weapon* PoseWeapon = FindWeapon();
		const bool bSprintingPose = bSprintPose && Slide && Slide->IsSprinting() && !Slide->IsSliding()
			&& !(PoseWeapon && PoseWeapon->IsAiming());
		const float SprintTarget = bSprintingPose ? 1.f : 0.f;
		SprintPoseAlpha = FMath::FInterpTo(SprintPoseAlpha, SprintTarget, DeltaTime, SprintPoseInterpSpeed);
		if (FMath::Abs(SprintPoseAlpha - SprintTarget) < 0.005f)
		{
			SprintPoseAlpha = SprintTarget;
		}
		// Running rhythm for the raised rifle: one full cycle per stride.
		if (const ACharacter* BobChar = Cast<ACharacter>(GetOwner()))
		{
			SprintBobPhase = FMath::Fmod(SprintBobPhase + BobChar->GetVelocity().Size2D() * DeltaTime / SprintStrideLength * UE_TWO_PI, UE_TWO_PI);
		}

		// The left hand lets go early in the raise and grabs the rifle again late in the lowering.
		SetLeftArmHidden(SprintPoseAlpha > 0.05f);
		ApplySprintBoneMask();
	}

	// Aim down sights blend (RMB on the AK-47).
	const AAK47Weapon* Weapon = FindWeapon();
	const float AimTarget = (Weapon && Weapon->IsAiming()) ? 1.f : 0.f;
	if (AimAlpha != AimTarget)
	{
		const float NewAim = FMath::FInterpTo(AimAlpha, AimTarget, DeltaTime, AimInterpSpeed);
		AimAlpha = FMath::Abs(NewAim - AimTarget) < 0.005f ? AimTarget : NewAim;
		bChanged = true;
		if (UCameraComponent* Cam = Cast<UCameraComponent>(CameraComponent.Get()); Cam && AimFieldOfView > 0.f)
		{
			Cam->SetFieldOfView(FMath::Lerp(BaseFieldOfView, AimFieldOfView, AimAlpha));
		}
	}

	// Re-applied every frame: the pose follows where the player looks and the crouch height.
	ApplyOffset(CurrentOffset, DeltaTime);
	bHasTicked = true;
}
