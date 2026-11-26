#include "AK47EquipSubsystem.h"

#include "AK47Weapon.h"
#include "LeanComponent.h"
#include "SlideComponent.h"
#include "DiveComponent.h"
#include "BunnyHopComponent.h"
#include "Fidelis.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarAK47AutoEquip(
	TEXT("fidelis.AK47.AutoEquip"), 1,
	TEXT("1 = donne automatiquement l'AK-47 au personnage joueur, 0 = desactive."));

bool UAK47EquipSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UAK47EquipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!WeaponClass)
	{
		WeaponClass = AAK47Weapon::StaticClass();
	}

	UE_LOG(LogFidelis, Log, TEXT("AK47EquipSubsystem: initialise pour le monde '%s'."), *GetNameSafe(GetWorld()));

	// Bind as early as possible: in PIE the player pawn is usually possessed *before* world BeginPlay.
	BindPossessionDelegate();
}

void UAK47EquipSubsystem::BindPossessionDelegate()
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			GI->GetOnPawnControllerChanged().AddUniqueDynamic(this, &UAK47EquipSubsystem::HandlePawnControllerChanged);
		}
	}
}

void UAK47EquipSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// In case the GameInstance wasn't set yet at Initialize time.
	BindPossessionDelegate();

	// Catch players possessed before the delegate was bound.
	int32 NumPlayers = 0;
	for (FConstPlayerControllerIterator It = InWorld.GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			++NumPlayers;
			APawn* Pawn = PC->GetPawn();
			UE_LOG(LogFidelis, Log, TEXT("AK47EquipSubsystem: BeginPlay, %s possede %s (%s)."),
				*GetNameSafe(PC), *GetNameSafe(Pawn), Pawn ? *Pawn->GetClass()->GetName() : TEXT("aucun pawn"));
			SetupPlayerCharacter(Pawn);
		}
	}
	if (NumPlayers == 0)
	{
		UE_LOG(LogFidelis, Log, TEXT("AK47EquipSubsystem: aucun PlayerController au BeginPlay, attente de la possession."));
	}
}

void UAK47EquipSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			GI->GetOnPawnControllerChanged().RemoveDynamic(this, &UAK47EquipSubsystem::HandlePawnControllerChanged);
		}
	}
	Super::Deinitialize();
}

void UAK47EquipSubsystem::HandlePawnControllerChanged(APawn* Pawn, AController* Controller)
{
	if (Pawn && Pawn->GetWorld() == GetWorld() && Cast<APlayerController>(Controller))
	{
		UE_LOG(LogFidelis, Log, TEXT("AK47EquipSubsystem: %s possede %s (%s)."),
			*GetNameSafe(Controller), *GetNameSafe(Pawn), *Pawn->GetClass()->GetName());
		SetupPlayerCharacter(Pawn);
	}
}

void UAK47EquipSubsystem::SetupPlayerCharacter(APawn* Pawn)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr)
	{
		AddMovementKeys(PC);

		// Movement abilities (independent of the AK-47). If the character Blueprint already has one of
		// these components (added in the editor to tweak its settings), that one is used as-is.
		if (!Character->FindComponentByClass<USlideComponent>())
		{
			NewObject<USlideComponent>(Character, TEXT("SlideComponent"))->RegisterComponent();
		}
		if (!Character->FindComponentByClass<UDiveComponent>())
		{
			NewObject<UDiveComponent>(Character, TEXT("DiveComponent"))->RegisterComponent();
		}
		if (!Character->FindComponentByClass<UBunnyHopComponent>())
		{
			NewObject<UBunnyHopComponent>(Character, TEXT("BunnyHopComponent"))->RegisterComponent();
		}
	}

	EquipAK47(Pawn);
}

void UAK47EquipSubsystem::AddMovementKeys(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (!MovementKeysContext)
	{
		const UInputAction* MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
		const UInputAction* JumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
		const UInputAction* CrouchAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
		const UInputAction* DiveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Dive.IA_Dive"));
		if (!MoveAction || !JumpAction || !CrouchAction || !DiveAction)
		{
			UE_LOG(LogFidelis, Error, TEXT("Touches: actions introuvables dans /Game/Input/Actions (move=%s jump=%s crouch=%s dive=%s)."),
				*GetNameSafe(MoveAction), *GetNameSafe(JumpAction), *GetNameSafe(CrouchAction), *GetNameSafe(DiveAction));
			return;
		}

		// Higher priority than IMC_Default (0): a key mapped here is consumed, so Z jumps instead of
		// moving forward, without editing the IMC_Default asset.
		MovementKeysContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Fidelis_CustomKeys"));
		MovementKeysContext->MapKey(JumpAction, EKeys::Z);   // sauter
		MovementKeysContext->MapKey(CrouchAction, EKeys::W); // s'accroupir (glissade en courant)
		MovementKeysContext->MapKey(DiveAction, EKeys::N);   // s'allonger (plongeon en courant)

		// Same convention as IMC_Default: IA_Move is a 2D axis, X = right, Y = forward.
		auto Swizzle = [this]() { return NewObject<UInputModifierSwizzleAxis>(MovementKeysContext); }; // X -> Y
		auto Negate = [this]() { return NewObject<UInputModifierNegate>(MovementKeysContext); };

		MovementKeysContext->MapKey(MoveAction, EKeys::I).Modifiers.Add(Swizzle());   // avancer
		FEnhancedActionKeyMapping& Back = MovementKeysContext->MapKey(MoveAction, EKeys::K); // reculer
		Back.Modifiers.Add(Swizzle());
		Back.Modifiers.Add(Negate());
		MovementKeysContext->MapKey(MoveAction, EKeys::J).Modifiers.Add(Negate());    // gauche
		MovementKeysContext->MapKey(MoveAction, EKeys::L);                             // droite
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MovementKeysContext, /*Priority*/ 1);
		UE_LOG(LogFidelis, Log, TEXT("Touches: I/K avant/arriere, J/L gauche/droite, Z sauter, W s'accroupir, N s'allonger."));
	}
}

AAK47Weapon* UAK47EquipSubsystem::EquipAK47(APawn* Pawn)
{
	if (CVarAK47AutoEquip.GetValueOnGameThread() == 0)
	{
		return nullptr;
	}

	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		if (Pawn)
		{
			UE_LOG(LogFidelis, Warning, TEXT("AK47: %s (%s) n'est pas un ACharacter, pas d'arme."), *GetNameSafe(Pawn), *Pawn->GetClass()->GetName());
		}
		return nullptr;
	}
	if (!Cast<APlayerController>(Character->GetController()))
	{
		return nullptr;
	}

	// Already armed?
	TArray<AActor*> Attached;
	Character->GetAttachedActors(Attached);
	for (AActor* Actor : Attached)
	{
		if (AAK47Weapon* Existing = Cast<AAK47Weapon>(Actor))
		{
			return Existing;
		}
	}

	FActorSpawnParameters Params;
	Params.Owner = Character;
	Params.Instigator = Character;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AAK47Weapon* Weapon = GetWorld()->SpawnActor<AAK47Weapon>(
		WeaponClass ? *WeaponClass : AAK47Weapon::StaticClass(),
		Character->GetActorTransform(), Params);

	if (Weapon)
	{
		UE_LOG(LogFidelis, Log, TEXT("AK47: arme creee '%s' (classe %s) pour %s (classe %s) a %s."),
			*Weapon->GetName(), *Weapon->GetClass()->GetName(),
			*Character->GetName(), *Character->GetClass()->GetName(),
			*Character->GetActorLocation().ToString());
		Weapon->EquipTo(Character);

		// Lean (Thumb01 / Thumb02, firing or not). Independent of the weapon logic.
		if (!Character->FindComponentByClass<ULeanComponent>())
		{
			ULeanComponent* Lean = NewObject<ULeanComponent>(Character, TEXT("LeanComponent"));
			Lean->RegisterComponent();
		}
	}
	else
	{
		UE_LOG(LogFidelis, Error, TEXT("AK47: failed to spawn weapon for %s."), *GetNameSafe(Character));
	}
	return Weapon;
}
