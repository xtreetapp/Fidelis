#include "AK47Weapon.h"

#include "Fidelis.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarAK47DebugShots(
	TEXT("fidelis.AK47.DebugShots"), 0,
	TEXT("1 = dessine la trajectoire de chaque balle de l'AK-47 (rouge = impact, vert = rien touche)."));

namespace AK47Assets
{
	// Placeholder visuals: the project has no AK-47 mesh yet, so the template rifle is used.
	// Swap these in a Blueprint child of AAK47Weapon (or here) once an AK-47 model is imported.
	static const TCHAR* Mesh = TEXT("/Game/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle");
	static const TCHAR* FireSound = TEXT("/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02.FirstPersonTemplateWeaponFire02");
	static const TCHAR* FireMontage = TEXT("/Game/Variant_Shooter/Anims/FP_Rifle_Shoot_Montage.FP_Rifle_Shoot_Montage");
	// ABP_FP_Weapon can't be used here: it casts the owner to BP_ShooterCharacter and reads its
	// FirstPersonCamera, which fails on BP_FirstPersonCharacter (arms never take the rifle pose).
	// ABP_TP_Rifle only needs an ACharacter and works on both the arms and the body.
	static const TCHAR* FirstPersonAnim = TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Rifle");
	static const TCHAR* ThirdPersonAnim = TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Rifle");
}

AAK47Weapon::AAK47Weapon()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(Root);
	FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;
	FirstPersonMesh->CastShadow = false;

	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ThirdPersonMesh"));
	ThirdPersonMesh->SetupAttachment(Root);
	ThirdPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(AK47Assets::Mesh);
	if (MeshFinder.Succeeded())
	{
		FirstPersonMesh->SetSkeletalMeshAsset(MeshFinder.Object);
		ThirdPersonMesh->SetSkeletalMeshAsset(MeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> SoundFinder(AK47Assets::FireSound);
	FireSound = SoundFinder.Object;

	static ConstructorHelpers::FObjectFinder<UAnimMontage> MontageFinder(AK47Assets::FireMontage);
	FireMontage = MontageFinder.Object;

	static ConstructorHelpers::FClassFinder<UAnimInstance> FPAnimFinder(AK47Assets::FirstPersonAnim);
	FirstPersonAnimClass = FPAnimFinder.Class;

	static ConstructorHelpers::FClassFinder<UAnimInstance> TPAnimFinder(AK47Assets::ThirdPersonAnim);
	ThirdPersonAnimClass = TPAnimFinder.Class;
}

USkeletalMeshComponent* AAK47Weapon::FindCharacterFirstPersonMesh(ACharacter* Character) const
{
	TArray<USkeletalMeshComponent*> Meshes;
	Character->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetFName() == CharacterFirstPersonMeshName)
		{
			return Mesh;
		}
	}
	// Fallback: any mesh flagged as first-person that isn't the main body.
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh != Character->GetMesh() && Mesh->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson)
		{
			return Mesh;
		}
	}
	return nullptr;
}

void AAK47Weapon::EquipTo(ACharacter* NewOwner)
{
	if (!NewOwner)
	{
		return;
	}
	if (OwningCharacter.IsValid())
	{
		Unequip();
	}

	OwningCharacter = NewOwner;
	SetOwner(NewOwner);
	SetInstigator(NewOwner);
	AttachToActor(NewOwner, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	NewOwner->OnDestroyed.AddUniqueDynamic(this, &AAK47Weapon::HandleOwnerDestroyed);

	// Attaches a weapon mesh to the character mesh's grip socket and reports the result in the log.
	auto AttachToGrip = [this](const TCHAR* Label, USkeletalMeshComponent* WeaponMesh, USkeletalMeshComponent* CharMesh, const FTransform& Offset)
	{
		const FAttachmentTransformRules SnapRules(EAttachmentRule::SnapToTarget, true);
		const bool bHasSocket = CharMesh && CharMesh->DoesSocketExist(GripSocket);
		const bool bAttached = CharMesh && WeaponMesh->AttachToComponent(CharMesh, SnapRules, GripSocket);
		WeaponMesh->SetRelativeTransform(Offset);
		if (!bHasSocket)
		{
			UE_LOG(LogFidelis, Error, TEXT("AK47 [%s]: socket '%s' introuvable sur %s (mesh %s) ; l'arme est attachee a l'origine du mesh."),
				Label, *GripSocket.ToString(), *GetNameSafe(CharMesh),
				CharMesh ? *GetNameSafe(CharMesh->GetSkeletalMeshAsset()) : TEXT("aucun"));
		}
		UE_LOG(LogFidelis, Log, TEXT("AK47 [%s]: %s attache=%d a %s, socket '%s' (existe=%d), position %s."),
			Label, *WeaponMesh->GetName(), bAttached, *GetNameSafe(CharMesh), *WeaponMesh->GetAttachSocketName().ToString(),
			bHasSocket, *WeaponMesh->GetComponentLocation().ToString());
	};

	// First-person: rifle in the arms' right hand, arms switch to a rifle pose.
	if (USkeletalMeshComponent* CharFPMesh = FindCharacterFirstPersonMesh(NewOwner))
	{
		if (FirstPersonAnimClass)
		{
			PreviousFirstPersonAnimClass = CharFPMesh->GetAnimClass();
			CharFPMesh->SetAnimInstanceClass(FirstPersonAnimClass);
			UE_LOG(LogFidelis, Log, TEXT("AK47 [vue 1re personne]: anim des bras -> %s."), *FirstPersonAnimClass->GetName());
		}
		AttachToGrip(TEXT("vue 1re personne"), FirstPersonMesh, CharFPMesh, FirstPersonGripOffset);
	}
	else
	{
		UE_LOG(LogFidelis, Warning, TEXT("AK47: no first-person mesh '%s' on %s, attaching to body mesh only."),
			*CharacterFirstPersonMeshName.ToString(), *GetNameSafe(NewOwner));
		AttachToGrip(TEXT("vue 1re personne (fallback corps)"), FirstPersonMesh, NewOwner->GetMesh(), FirstPersonGripOffset);
	}

	// World-space representation: rifle in the body's right hand.
	if (USkeletalMeshComponent* Body = NewOwner->GetMesh())
	{
		if (ThirdPersonAnimClass)
		{
			PreviousThirdPersonAnimClass = Body->GetAnimClass();
			Body->SetAnimInstanceClass(ThirdPersonAnimClass);
		}
		AttachToGrip(TEXT("vue monde"), ThirdPersonMesh, Body, FTransform::Identity);
	}

	if (APlayerController* PC = Cast<APlayerController>(NewOwner->GetController()))
	{
		SetupInput(PC);
	}

	BroadcastAmmo();
	UE_LOG(LogFidelis, Log, TEXT("AK47 equipped on %s (%d/%d)."), *GetNameSafe(NewOwner), AmmoInMagazine, ReserveAmmo);
}

void AAK47Weapon::Unequip()
{
	StopFire();
	GetWorldTimerManager().ClearTimer(ReloadTimer);
	bIsReloading = false;
	TeardownInput();

	if (ACharacter* Character = OwningCharacter.Get())
	{
		Character->OnDestroyed.RemoveDynamic(this, &AAK47Weapon::HandleOwnerDestroyed);

		if (USkeletalMeshComponent* CharFPMesh = FindCharacterFirstPersonMesh(Character); CharFPMesh && PreviousFirstPersonAnimClass)
		{
			CharFPMesh->SetAnimInstanceClass(PreviousFirstPersonAnimClass);
		}
		if (USkeletalMeshComponent* Body = Character->GetMesh(); Body && PreviousThirdPersonAnimClass)
		{
			Body->SetAnimInstanceClass(PreviousThirdPersonAnimClass);
		}
	}

	FirstPersonMesh->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	ThirdPersonMesh->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	OwningCharacter.Reset();
	SetOwner(nullptr);
	SetInstigator(nullptr);
	PreviousFirstPersonAnimClass = nullptr;
	PreviousThirdPersonAnimClass = nullptr;
}

void AAK47Weapon::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	AmmoInMagazine = MagazineSize;
}

void AAK47Weapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TeardownInput();
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

void AAK47Weapon::HandleOwnerDestroyed(AActor* /*DestroyedActor*/)
{
	Destroy();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void AAK47Weapon::SetupInput(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (!MappingContext)
	{
		FireAction = NewObject<UInputAction>(this, TEXT("IA_AK47_Fire"));
		FireAction->ValueType = EInputActionValueType::Boolean;

		ReloadAction = NewObject<UInputAction>(this, TEXT("IA_AK47_Reload"));
		ReloadAction->ValueType = EInputActionValueType::Boolean;

		MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_AK47"));
		MappingContext->MapKey(FireAction, EKeys::LeftMouseButton);
		MappingContext->MapKey(FireAction, EKeys::Gamepad_RightTrigger);
		MappingContext->MapKey(ReloadAction, EKeys::R);
		MappingContext->MapKey(ReloadAction, EKeys::Gamepad_FaceButton_Left);
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, /*Priority*/ 1);
	}

	EnableInput(PC);
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->ClearActionBindings();
		EIC->BindAction(FireAction, ETriggerEvent::Started, this, &AAK47Weapon::StartFire);
		EIC->BindAction(FireAction, ETriggerEvent::Completed, this, &AAK47Weapon::StopFire);
		EIC->BindAction(FireAction, ETriggerEvent::Canceled, this, &AAK47Weapon::StopFire);
		EIC->BindAction(ReloadAction, ETriggerEvent::Started, this, &AAK47Weapon::Reload);
	}
	else
	{
		UE_LOG(LogFidelis, Error, TEXT("AK47: input component is not an EnhancedInputComponent; check DefaultInputComponentClass."));
	}

	InputController = PC;
}

void AAK47Weapon::TeardownInput()
{
	APlayerController* PC = InputController.Get();
	if (!PC)
	{
		return;
	}

	if (MappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(MappingContext);
		}
	}
	DisableInput(PC);
	InputController.Reset();
}

// ---------------------------------------------------------------------------
// Firing
// ---------------------------------------------------------------------------

void AAK47Weapon::StartFire()
{
	if (!OwningCharacter.IsValid())
	{
		return;
	}

	bWantsToFire = true;
	ConsecutiveShots = 0;

	const float Interval = 60.f / FMath::Max(RoundsPerMinute, 1.f);
	const double Now = GetWorld()->GetTimeSeconds();
	const float FirstDelay = LastFireTime < 0.0 ? 0.f : FMath::Max(0.f, float(LastFireTime + Interval - Now));

	if (FirstDelay <= KINDA_SMALL_NUMBER)
	{
		FireShot();
		GetWorldTimerManager().SetTimer(FireTimer, this, &AAK47Weapon::FireShot, Interval, true);
	}
	else
	{
		// Respect the refire rate so tapping the trigger can't beat full-auto.
		GetWorldTimerManager().SetTimer(FireTimer, this, &AAK47Weapon::FireShot, Interval, true, FirstDelay);
	}
}

void AAK47Weapon::StopFire()
{
	bWantsToFire = false;
	ConsecutiveShots = 0;
	GetWorldTimerManager().ClearTimer(FireTimer);
}

void AAK47Weapon::FireShot()
{
	ACharacter* Character = OwningCharacter.Get();
	if (!Character || !bWantsToFire || bIsReloading)
	{
		return;
	}

	if (AmmoInMagazine <= 0)
	{
		StopFire();
		Reload();
		return;
	}

	--AmmoInMagazine;
	LastFireTime = GetWorld()->GetTimeSeconds();

	// Aim from the real camera (PlayerCameraManager) so shots go exactly where the player looks.
	// Pawn::GetActorEyesViewPoint uses BaseEyeHeight, which doesn't match the head-mounted camera.
	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	FVector ViewLoc;
	FRotator ViewRot;
	if (PC)
	{
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	}
	else
	{
		Character->GetActorEyesViewPoint(ViewLoc, ViewRot);
	}

	const float SpreadDeg = FMath::Min(BaseSpreadDegrees + SpreadPerShotDegrees * ConsecutiveShots, MaxSpreadDegrees);
	const FVector ShotDir = FMath::VRandCone(ViewRot.Vector(), FMath::DegreesToRadians(SpreadDeg));
	const FVector TraceEnd = ViewLoc + ShotDir * Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AK47Shot), /*bTraceComplex*/ true, this);
	Params.AddIgnoredActor(Character);
	Params.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, TraceEnd, ECC_Visibility, Params);
	if (CVarAK47DebugShots.GetValueOnGameThread() > 0)
	{
		const FVector MuzzleLoc = FirstPersonMesh->DoesSocketExist(TEXT("Muzzle"))
			? FirstPersonMesh->GetSocketLocation(TEXT("Muzzle")) : ViewLoc;
		DrawDebugLine(GetWorld(), MuzzleLoc, bHit ? Hit.ImpactPoint : TraceEnd, bHit ? FColor::Red : FColor::Green, false, 2.f, 0, 0.5f);
		if (bHit)
		{
			DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 10.f, FColor::Red, false, 2.f);
		}
	}
	if (bHit)
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			UGameplayStatics::ApplyPointDamage(HitActor, Damage, ShotDir, Hit, Character->GetController(), this, nullptr);
		}
		if (UPrimitiveComponent* HitComp = Hit.GetComponent(); HitComp && HitComp->IsSimulatingPhysics())
		{
			HitComp->AddImpulseAtLocation(ShotDir * ImpactImpulse, Hit.ImpactPoint, Hit.BoneName);
		}
	}

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, FirstPersonMesh->GetComponentLocation());
	}

	if (FireMontage)
	{
		if (USkeletalMeshComponent* CharFPMesh = FindCharacterFirstPersonMesh(Character))
		{
			if (UAnimInstance* Anim = CharFPMesh->GetAnimInstance())
			{
				Anim->Montage_Play(FireMontage);
			}
		}
	}

	// Recoil: the AK climbs and drifts sideways.
	// Applied straight to the control rotation (positive pitch = up). AddPitchInput was multiplied by
	// the legacy InputPitchScale (bEnableLegacyInputScales=True in this project), which can flip its sign
	// and made the view kick downward.
	if (PC)
	{
		FRotator ControlRot = PC->GetControlRotation();
		ControlRot.Pitch = FRotator::NormalizeAxis(ControlRot.Pitch) + RecoilPitch;
		ControlRot.Yaw += FMath::FRandRange(-RecoilYaw, RecoilYaw);
		PC->SetControlRotation(ControlRot);
	}

	++ConsecutiveShots;
	BroadcastAmmo();

	if (AmmoInMagazine <= 0)
	{
		StopFire();
		Reload();
	}
}

// ---------------------------------------------------------------------------
// Reload
// ---------------------------------------------------------------------------

void AAK47Weapon::Reload()
{
	if (bIsReloading || AmmoInMagazine >= MagazineSize || ReserveAmmo <= 0)
	{
		return;
	}

	StopFire();
	bIsReloading = true;
	GetWorldTimerManager().SetTimer(ReloadTimer, this, &AAK47Weapon::FinishReload, FMath::Max(ReloadDuration, 0.01f), false);
}

void AAK47Weapon::FinishReload()
{
	const int32 Needed = MagazineSize - AmmoInMagazine;
	const int32 Taken = FMath::Min(Needed, ReserveAmmo);
	AmmoInMagazine += Taken;
	ReserveAmmo -= Taken;
	bIsReloading = false;
	BroadcastAmmo();
}

void AAK47Weapon::BroadcastAmmo()
{
	OnAmmoChanged.Broadcast(AmmoInMagazine, ReserveAmmo);
}
