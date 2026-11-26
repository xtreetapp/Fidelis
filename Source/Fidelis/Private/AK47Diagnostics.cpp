// Developer diagnostics for the AK-47 / player look input.
// Console: "fidelis.DiagLook [quit]" -- logs the look-input state, simulates mouse movement
// for a short time and reports whether the control rotation (the view) actually changed.

#include "Fidelis.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Misc/CoreDelegates.h"
#include "AK47Weapon.h"
#include "DiveComponent.h"
#include "LeanComponent.h"
#include "SlideComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"

#if !UE_BUILD_SHIPPING

namespace FidelisDiag
{
	static void LogLookState(APlayerController* PC, const TCHAR* Stage)
	{
		APawn* Pawn = PC->GetPawn();
		UE_LOG(LogFidelis, Display, TEXT("DiagLook [%s]: pawn=%s ControlRotation=%s LookIgnored=%d MoveIgnored=%d PawnInputComponent=%s"),
			Stage, *GetNameSafe(Pawn), *PC->GetControlRotation().ToString(), PC->IsLookInputIgnored(), PC->IsMoveInputIgnored(),
			Pawn ? *GetNameSafe(Pawn->InputComponent) : TEXT("-"));

		if (PC->PlayerCameraManager)
		{
			UE_LOG(LogFidelis, Display, TEXT("DiagLook [%s]: CameraManager rot=%s loc=%s viewtarget=%s"), Stage,
				*PC->PlayerCameraManager->GetCameraRotation().ToString(), *PC->PlayerCameraManager->GetCameraLocation().ToString(),
				*GetNameSafe(PC->GetViewTarget()));
		}
		if (Pawn)
		{
			UE_LOG(LogFidelis, Display, TEXT("DiagLook [%s]: Pawn rot=%s"), Stage, *Pawn->GetActorRotation().ToString());
			TArray<UCameraComponent*> Cams;
			Pawn->GetComponents<UCameraComponent>(Cams);
			for (UCameraComponent* Cam : Cams)
			{
				UE_LOG(LogFidelis, Display, TEXT("DiagLook [%s]: Camera %s active=%d usePawnRot=%d parent=%s socket=%s worldRot=%s worldLoc=%s"), Stage,
					*Cam->GetName(), Cam->IsActive(), Cam->bUsePawnControlRotation, *GetNameSafe(Cam->GetAttachParent()),
					*Cam->GetAttachSocketName().ToString(), *Cam->GetComponentRotation().ToString(), *Cam->GetComponentLocation().ToString());
			}
		}

		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		if (!Subsystem)
		{
			UE_LOG(LogFidelis, Display, TEXT("DiagLook [%s]: pas de EnhancedInputLocalPlayerSubsystem."), Stage);
			return;
		}

		for (const TCHAR* Path : { TEXT("/Game/Input/IMC_Default.IMC_Default"), TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook") })
		{
			const UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, Path);
			int32 Priority = -1;
			const bool bHas = IMC && Subsystem->HasMappingContext(IMC, Priority);
			UE_LOG(LogFidelis, Display, TEXT("DiagLook [%s]: %s actif=%d priorite=%d"), Stage, Path, bHas, Priority);
		}

		if (const UInputAction* MouseLook = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook")))
		{
			FString Keys;
			for (const FKey& Key : Subsystem->QueryKeysMappedToAction(MouseLook))
			{
				Keys += Key.ToString() + TEXT(" ");
			}
			UE_LOG(LogFidelis, Display, TEXT("DiagLook [%s]: touches de IA_MouseLook = [%s]"), Stage, *Keys);
		}
	}

	static void Run(UWorld* World, bool bQuitAfter, bool bShots)
	{
		struct FState
		{
			TWeakObjectPtr<UWorld> World;
			FRotator Start;
			int32 Frame = 0;
			bool bQuit = false;
			bool bShots = false;
			int32 QuitCountdown = -1;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->bQuit = bQuitAfter;
		State->bShots = bShots;

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) -> bool
		{
			if (State->QuitCountdown >= 0)
			{
				// Let the last screenshot render before quitting.
				if (State->QuitCountdown-- == 0)
				{
					FPlatformMisc::RequestExit(false, TEXT("fidelis.DiagLook"));
					return false;
				}
				return true;
			}

			UWorld* W = State->World.Get();
			APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
			if (!PC)
			{
				return ++State->Frame < 600; // wait for a player
			}

			// Wait ~1s for possession / BeginPlay / mapping contexts.
			if (State->Frame < 60 && State->Start.IsZero())
			{
				++State->Frame;
				if (State->Frame == 60)
				{
					LogLookState(PC, TEXT("avant"));
					if (State->bShots)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/DiagLook_avant.png"), false, false);
					}
					State->Start = PC->GetControlRotation();
					State->Frame = 100;
				}
				return true;
			}

			if (State->Frame < 160)
			{
				// Simulate the mouse moving right and up.
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 5.f, 1));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, 3.f, 1));
				++State->Frame;
				return true;
			}

			if (State->Frame < 175)
			{
				++State->Frame; // let the camera manager update
				return true;
			}
			LogLookState(PC, TEXT("apres"));
			if (State->bShots)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/DiagLook_apres.png"), false, false);
			}
			const FRotator Delta = (PC->GetControlRotation() - State->Start).GetNormalized();
			UE_LOG(LogFidelis, Display, TEXT("DiagLook RESULTAT: rotation de la vue apres souris simulee = %s -> %s"),
				*Delta.ToString(), Delta.IsNearlyZero(0.01) ? TEXT("LA SOURIS NE TOURNE PAS LA VUE") : TEXT("la souris tourne la vue"));

			if (State->bQuit)
			{
				State->QuitCountdown = 30;
				return true;
			}
			return false;
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFidelisDiagLookCmd(
	TEXT("fidelis.DiagLook"),
	TEXT("Simule la souris et verifie que la vue du joueur tourne. Arguments optionnels: quit (quitte apres), shot (captures Saved/Screenshots/DiagLook_*.png)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UWorld* GameWorld = World;
		if (!GameWorld || !GameWorld->IsGameWorld())
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World() && Ctx.World()->IsGameWorld())
				{
					GameWorld = Ctx.World();
					break;
				}
			}
		}
		if (GameWorld)
		{
			FidelisDiag::Run(GameWorld, Args.Contains(TEXT("quit")), Args.Contains(TEXT("shot")));
		}
		else
		{
			UE_LOG(LogFidelis, Warning, TEXT("DiagLook: aucun monde de jeu."));
		}
	}));

// ---------------------------------------------------------------------------
// fidelis.DiagMoves [quit]: scripted run of sprint / slide / dive (+ prone 360 fire) / backward roll.
// Inputs are injected as real key events, so IMC_Default mappings are exercised end to end.
// ---------------------------------------------------------------------------
namespace FidelisDiag
{
	static void Key(APlayerController* PC, const FKey& K, bool bDown)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}

	static void Report(APlayerController* PC, const TCHAR* Label)
	{
		ACharacter* Char = Cast<ACharacter>(PC->GetPawn());
		if (!Char) { return; }
		const UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
		const USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
		const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>();
		const AAK47Weapon* AK = nullptr;
		TArray<AActor*> Attached; Char->GetAttachedActors(Attached);
		for (AActor* A : Attached) { if (const AAK47Weapon* W = Cast<AAK47Weapon>(A)) { AK = W; } }

		static const TCHAR* DiveNames[] = { TEXT("None"), TEXT("Airborne"), TEXT("Prone"), TEXT("Rolling") };
		UE_LOG(LogFidelis, Display, TEXT("DiagMoves [%s]: pos=%s vitesse2D=%.0f sol=%d accroupi=%d capsuleHH=%.0f course=%d glisse=%d plongeon=%s yaw=%.1f pitch=%.1f munitions=%d"),
			Label, *Char->GetActorLocation().ToCompactString(), CMC->Velocity.Size2D(), CMC->IsMovingOnGround(), CMC->IsCrouching(),
			Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
			Slide && Slide->IsSprinting(), Slide && Slide->IsSliding(),
			Dive ? DiveNames[(int32)Dive->GetDiveState()] : TEXT("-"),
			PC->GetControlRotation().Yaw, FRotator::NormalizeAxis(PC->GetControlRotation().Pitch), AK ? AK->GetAmmoInMagazine() : -1);
	}

	static void Teleport(APlayerController* PC, const FVector& Loc, float Yaw)
	{
		if (ACharacter* Char = Cast<ACharacter>(PC->GetPawn()))
		{
			Char->GetCharacterMovement()->StopMovementImmediately();
			Char->TeleportTo(FVector(Loc.X, Loc.Y, Char->GetActorLocation().Z), FRotator(0.f, Yaw, 0.f));
			PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		}
	}

	static void LeanReport(APlayerController* PC, const TCHAR* Label)
	{
		APawn* Pawn = PC->GetPawn();
		const ULeanComponent* Lean = Pawn ? Pawn->FindComponentByClass<ULeanComponent>() : nullptr;
		const FVector CamLocal = Pawn && PC->PlayerCameraManager
			? Pawn->GetActorTransform().InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation()) : FVector::ZeroVector;
		UE_LOG(LogFidelis, Display, TEXT("DiagMoves [lean %s]: alpha=%.2f camera (repere perso) Y=%.1f"),
			Label, Lean ? Lean->GetLeanAlpha() : -9.f, CamLocal.Y);
	}

	struct FStep { float Time; TFunction<void(APlayerController*)> Do; };

	// Custom keys check: W crouch / N dive (new) vs C / V (IMC_Default), standing then running.
	static TArray<FStep> KeySteps()
	{
		auto Act = [](APlayerController* PC) {
			const UEnhancedPlayerInput* In = Cast<UEnhancedPlayerInput>(PC->PlayerInput);
			const ACharacter* Char = Cast<ACharacter>(PC->GetPawn());
			const USlideComponent* Slide = Char ? Char->FindComponentByClass<USlideComponent>() : nullptr;
			const UDiveComponent* Dive = Char ? Char->FindComponentByClass<UDiveComponent>() : nullptr;
			const UInputAction* Crouch = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
			const UInputAction* DiveA = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Dive.IA_Dive"));
			return FString::Printf(TEXT("IA_Crouch=%d IA_Dive=%d"),
				In && Crouch ? In->GetActionValue(Crouch).Get<bool>() : -1, In && DiveA ? In->GetActionValue(DiveA).Get<bool>() : -1);
		};
		auto R = [Act](const TCHAR* Label) { return [Act, Label](APlayerController* PC) {
			float EyeH = -1.f;
			if (const ACharacter* C = Cast<ACharacter>(PC->GetPawn()); C && PC->PlayerCameraManager)
			{
				const UCapsuleComponent* Cap = C->GetCapsuleComponent();
				EyeH = PC->PlayerCameraManager->GetCameraLocation().Z - (Cap->GetComponentLocation().Z - Cap->GetScaledCapsuleHalfHeight());
			}
			UE_LOG(LogFidelis, Display, TEXT("DiagKeys [%s]: %s hauteurYeux=%.1f"), Label, *Act(PC), EyeH); Report(PC, Label); }; };
		return {
			{ 1.0f, [R](APlayerController* PC) { R(TEXT("depart"))(PC); Key(PC, EKeys::W, true); } },
			{ 1.6f, [R](APlayerController* PC) { R(TEXT("W tenu a l'arret"))(PC); Key(PC, EKeys::W, false); } },
			{ 2.2f, [R](APlayerController* PC) { R(TEXT("W relache"))(PC); Key(PC, EKeys::C, true); } },
			{ 2.8f, [R](APlayerController* PC) { R(TEXT("C tenu a l'arret"))(PC); Key(PC, EKeys::C, false); } },
			{ 3.4f, [R](APlayerController* PC) { Key(PC, EKeys::N, true); } },
			{ 3.8f, [R](APlayerController* PC) { R(TEXT("N tenu a l'arret"))(PC); Key(PC, EKeys::N, false); } },
			{ 4.2f, [R](APlayerController* PC) { Teleport(PC, FVector(0.f, 0.f, 0.f), 0.f); Key(PC, EKeys::I, true); Key(PC, EKeys::LeftShift, true); } },
			{ 5.4f, [R](APlayerController* PC) { R(TEXT("course I+Maj"))(PC); Key(PC, EKeys::N, true); } },
			{ 5.7f, [R](APlayerController* PC) { R(TEXT("N en course"))(PC); } },
			{ 6.6f, [R](APlayerController* PC) { R(TEXT("N tenu apres plongeon"))(PC); Key(PC, EKeys::N, false); } },
			{ 7.4f, [R](APlayerController* PC) { R(TEXT("N relache"))(PC); } },
			{ 8.6f, [R](APlayerController* PC) { R(TEXT("course 2"))(PC); Key(PC, EKeys::W, true); } },
			{ 8.9f, [R](APlayerController* PC) { R(TEXT("W en course"))(PC); Key(PC, EKeys::W, false); } },
			{ 10.0f,[R](APlayerController* PC) { R(TEXT("fin"))(PC); Key(PC, EKeys::I, false); Key(PC, EKeys::LeftShift, false); } },
		};
	}

	static void RunMoves(UWorld* World, bool bQuit, bool bKeys = false)
	{
		TArray<FStep> Steps = bKeys ? KeySteps() : TArray<FStep>{
			{ 1.0f, [](APlayerController* PC) { Report(PC, TEXT("depart")); Key(PC, EKeys::Z, true); Key(PC, EKeys::LeftShift, true); } },
			{ 2.2f, [](APlayerController* PC) { Report(PC, TEXT("course avant")); Key(PC, EKeys::C, true); } },
			{ 2.25f,[](APlayerController* PC) { Key(PC, EKeys::C, false); } },
			{ 2.5f, [](APlayerController* PC) { Report(PC, TEXT("pendant glissade")); } },
			{ 3.4f, [](APlayerController* PC) { Report(PC, TEXT("apres glissade")); } },
			{ 3.5f, [](APlayerController* PC) { Teleport(PC, FVector(1300.f, 0.f, 0.f), 180.f); } },
			{ 4.4f, [](APlayerController* PC) { Report(PC, TEXT("course avant 2")); Key(PC, EKeys::V, true); } },
			{ 4.6f, [](APlayerController* PC) { Report(PC, TEXT("plongeon en l'air")); } },
			{ 5.8f, [](APlayerController* PC) { Report(PC, TEXT("allonge, V maintenue")); Key(PC, EKeys::LeftMouseButton, true); } },
			// Turn around and look down while lying down and firing.
			{ 5.81f,[](APlayerController* PC) { PC->SetControlRotation(PC->GetControlRotation() + FRotator(-20.f, 180.f, 0.f)); } },
			{ 6.3f, [](APlayerController* PC) { Key(PC, EKeys::LeftMouseButton, false); Report(PC, TEXT("allonge, tir a 180 deg")); } },
			{ 6.5f, [](APlayerController* PC) { Key(PC, EKeys::V, false); Key(PC, EKeys::Z, false); } },
			{ 7.0f, [](APlayerController* PC) { Report(PC, TEXT("V relachee")); Teleport(PC, FVector(1300.f, 0.f, 0.f), 0.f); Key(PC, EKeys::S, true); } },
			{ 8.2f, [](APlayerController* PC) { Report(PC, TEXT("course arriere")); Key(PC, EKeys::V, true); } },
			{ 8.3f, [](APlayerController* PC) { Key(PC, EKeys::C, true); } },
			{ 8.35f,[](APlayerController* PC) { Key(PC, EKeys::V, false); Key(PC, EKeys::C, false); } },
			{ 8.6f, [](APlayerController* PC) { Report(PC, TEXT("roulade")); } },
			{ 9.8f, [](APlayerController* PC) { Report(PC, TEXT("fin roulade")); Key(PC, EKeys::S, false); } },
			// Bunny hop: sprint forward, then hold jump (auto re-jump on each landing).
			{ 10.0f,[](APlayerController* PC) { Teleport(PC, FVector(1300.f, 0.f, 0.f), 180.f); Key(PC, EKeys::Z, true); } },
			{ 10.8f,[](APlayerController* PC) { Report(PC, TEXT("bhop depart")); Key(PC, EKeys::SpaceBar, true); } },
			{ 11.3f,[](APlayerController* PC) { Report(PC, TEXT("bhop 0.5s")); } },
			{ 11.8f,[](APlayerController* PC) { Report(PC, TEXT("bhop 1s")); } },
			{ 12.3f,[](APlayerController* PC) { Report(PC, TEXT("bhop 1.5s")); } },
			{ 12.8f,[](APlayerController* PC) { Report(PC, TEXT("bhop 2s")); Key(PC, EKeys::SpaceBar, false); } },
			{ 13.5f,[](APlayerController* PC) { Report(PC, TEXT("fin")); Key(PC, EKeys::Z, false); Key(PC, EKeys::LeftShift, false); } },
			// Lean-fire: Thumb01 alone (no lean), then Thumb01 + fire (left), then Thumb02 + fire (right).
			{ 14.0f,[](APlayerController* PC) { Teleport(PC, FVector(1300.f, 0.f, 0.f), 180.f); Key(PC, EKeys::ThumbMouseButton, true); } },
			{ 14.5f,[](APlayerController* PC) { LeanReport(PC, TEXT("Thumb01 sans tir")); Key(PC, EKeys::LeftMouseButton, true); } },
			{ 15.0f,[](APlayerController* PC) { LeanReport(PC, TEXT("Thumb01 + tir")); Key(PC, EKeys::ThumbMouseButton, false); Key(PC, EKeys::ThumbMouseButton2, true); } },
			{ 15.6f,[](APlayerController* PC) { LeanReport(PC, TEXT("Thumb02 + tir")); Key(PC, EKeys::LeftMouseButton, false); } },
			{ 16.2f,[](APlayerController* PC) { LeanReport(PC, TEXT("tir relache")); Key(PC, EKeys::ThumbMouseButton2, false); } },
		};


		struct FState { TWeakObjectPtr<UWorld> World; TArray<FStep> Steps; int32 Next = 0; float T = 0.f; bool bQuit = false; int32 QuitIn = -1; };
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World; State->Steps = MoveTemp(Steps); State->bQuit = bQuit;

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float Dt) -> bool
		{
			if (State->QuitIn >= 0)
			{
				if (State->QuitIn-- == 0) { FPlatformMisc::RequestExit(false, TEXT("fidelis.DiagMoves")); return false; }
				return true;
			}
			UWorld* W = State->World.Get();
			APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
			if (!PC || !PC->GetPawn()) { return true; }

			State->T += FMath::Min(Dt, 0.05f);
			while (State->Next < State->Steps.Num() && State->T >= State->Steps[State->Next].Time)
			{
				State->Steps[State->Next++].Do(PC);
			}
			if (State->Next >= State->Steps.Num())
			{
				if (State->bQuit) { State->QuitIn = 5; return true; }
				return false;
			}
			return true;
		}));
	}
}

// ---------------------------------------------------------------------------
// fidelis.DiagAim [quit]: holds aim (RMB) while standing, walking and running in several directions,
// and logs every frame where the rifle sits in camera space. A rifle locked to the view gives constant
// numbers; anything that varies while moving is the movement the player sees.
// ---------------------------------------------------------------------------
namespace FidelisDiagAim
{
	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float T = 0.f;
		int32 Phase = -1;
		bool bQuit = false;
		bool bAim = true;
		int32 QuitIn = -1;
		int32 Frame = 0;
		// Smoothness: frame-to-frame motion of the muzzle in view space.
		FVector PrevMuzzle = FVector::ZeroVector, PrevDelta = FVector::ZeroVector;
		bool bHasPrev = false;
		int32 Stalls = 0;
		float MaxJerk = 0.f, SumStep = 0.f, MaxStep = 0.f;
		// Per-phase min/max of the muzzle and rifle origin in view space.
		FVector MinMuzzle, MaxMuzzle, MinOrigin, MaxOrigin, MinCamRel, MaxCamRel;
		int32 Samples = 0;
	};

	struct FPhase { float Start; const TCHAR* Name; TArray<FKey> Keys; };

	static const TArray<FPhase>& Phases()
	{
		static const TArray<FPhase> P = {
			{ 0.0f, TEXT("arret (visee)"), {} },
			{ 1.5f, TEXT("avant"), { EKeys::I } },
			{ 5.5f, TEXT("arriere"), { EKeys::K } },
			{ 7.0f, TEXT("droite"), { EKeys::L } },
			{ 8.5f, TEXT("gauche"), { EKeys::J } },
			{ 10.0f, TEXT("fin"), {} },
		};
		return P;
	}

	static void ResetStats(FState& S)
	{
		S.MinMuzzle = S.MinOrigin = S.MinCamRel = FVector(BIG_NUMBER);
		S.MaxMuzzle = S.MaxOrigin = S.MaxCamRel = FVector(-BIG_NUMBER);
		S.Samples = 0;
		S.bHasPrev = false;
		S.Stalls = 0;
		S.MaxJerk = S.SumStep = S.MaxStep = 0.f;
	}

	static void Flush(FState& S, const TCHAR* Name)
	{
		if (S.Samples == 0) { return; }
		UE_LOG(LogFidelis, Display, TEXT("DiagAim [%s] %d images: bouche (vue: avant/droite/haut) min %s max %s ecart %s"),
			Name, S.Samples, *S.MinMuzzle.ToCompactString(), *S.MaxMuzzle.ToCompactString(), *(S.MaxMuzzle - S.MinMuzzle).ToCompactString());
		UE_LOG(LogFidelis, Display, TEXT("DiagAim [%s] origine fusil (vue) ecart %s | camera (repere perso) ecart %s"),
			Name, *(S.MaxOrigin - S.MinOrigin).ToCompactString(), *(S.MaxCamRel - S.MinCamRel).ToCompactString());
		UE_LOG(LogFidelis, Display, TEXT("DiagAim [%s] fluidite: pas moyen %.3f cm, pas max %.3f cm, a-coup max %.3f cm, images figees %d / %d"),
			Name, S.SumStep / FMath::Max(1, S.Samples - 1), S.MaxStep, S.MaxJerk, S.Stalls, S.Samples);
	}

	static void Run(UWorld* World, bool bQuit, bool bAim)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->bQuit = bQuit;
		State->bAim = bAim;
		ResetStats(*State);

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float Dt) -> bool
		{
			if (State->QuitIn >= 0)
			{
				if (State->QuitIn-- == 0) { FPlatformMisc::RequestExit(false, TEXT("fidelis.DiagAim")); return false; }
				return true;
			}
			UWorld* W = State->World.Get();
			APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
			ACharacter* Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			if (!Char || !PC->PlayerCameraManager) { return true; }

			const TArray<FPhase>& P = Phases();
			if (State->Phase < 0)
			{
				if (State->bAim) { FidelisDiag::Key(PC, EKeys::RightMouseButton, true); }
				State->Phase = 0;
				UE_LOG(LogFidelis, Display, TEXT("DiagAim: debut, visee maintenue."));
				TArray<USkeletalMeshComponent*> Meshes; Char->GetComponents<USkeletalMeshComponent>(Meshes);
				for (const USkeletalMeshComponent* M : Meshes)
				{
					UE_LOG(LogFidelis, Display, TEXT("DiagAim: mesh %s URO=%d visTick=%d anim=%s"), *M->GetName(), M->bEnableUpdateRateOptimizations,
						(int32)M->VisibilityBasedAnimTickOption, *GetNameSafe(M->GetAnimInstance()));
				}
			}

			State->T += FMath::Min(Dt, 0.05f);
			const int32 NextPhase = State->Phase + 1;
			if (NextPhase < P.Num() && State->T >= P[NextPhase].Start)
			{
				Flush(*State, P[State->Phase].Name);
				ResetStats(*State);
				for (const FKey& K : P[State->Phase].Keys) { FidelisDiag::Key(PC, K, false); }
				State->Phase = NextPhase;
				for (const FKey& K : P[State->Phase].Keys) { FidelisDiag::Key(PC, K, true); }
				if (State->Phase == P.Num() - 1)
				{
					FidelisDiag::Key(PC, EKeys::RightMouseButton, false);
					UE_LOG(LogFidelis, Display, TEXT("DiagAim: fin."));
					if (State->bQuit) { State->QuitIn = 5; return true; }
					return false;
				}
			}

			// Screenshots: running (sprint pose) early in the forward phase, walking later.
			{
				static const float ShotTimes[] = { 1.2f, 2.1f, 4.6f };
				static const TCHAR* ShotNames[] = { TEXT("arret"), TEXT("course"), TEXT("marche") };
				for (int32 i = 0; i < 3; ++i)
				{
					if (State->T >= ShotTimes[i] && State->T - FMath::Min(Dt, 0.05f) < ShotTimes[i])
					{
						const USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
						const ULeanComponent* Lean = Char->FindComponentByClass<ULeanComponent>();
						UE_LOG(LogFidelis, Display, TEXT("DiagAim capture %s: course=%d poseCourse=%.2f vitesse=%.0f controle=%s camera=%s pos=%s"), ShotNames[i],
							Slide && Slide->IsSprinting(), Lean ? Lean->GetSprintPoseAlpha() : -1.f, Char->GetVelocity().Size2D(),
							*PC->GetControlRotation().ToCompactString(), *PC->PlayerCameraManager->GetCameraRotation().ToCompactString(),
							*Char->GetActorLocation().ToCompactString());
						FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/DiagAim_%s_%s.png"),
							State->bAim ? TEXT("visee") : TEXT("hanche"), ShotNames[i]), false, false);
					}
				}
			}

			{
				static float LastPitch = 0.f;
				const float Pitch = FRotator::NormalizeAxis(PC->GetControlRotation().Pitch);
				if (!FMath::IsNearlyEqual(Pitch, LastPitch, 0.01f))
				{
					UE_LOG(LogFidelis, Display, TEXT("DiagAim t=%.2f pitch %.2f -> %.2f (vitesse %.0f, sol=%d, pos %s)"), State->T, LastPitch, Pitch,
						Char->GetVelocity().Size2D(), Char->GetCharacterMovement()->IsMovingOnGround(), *Char->GetActorLocation().ToCompactString());
					LastPitch = Pitch;
				}
			}

			// Skip the first 0.5 s of each phase (aim blend / acceleration), then measure.
			if (State->T - P[State->Phase].Start < 0.5f) { return true; }

			const AAK47Weapon* AK = nullptr;
			TArray<AActor*> Attached; Char->GetAttachedActors(Attached);
			for (AActor* A : Attached) { if (const AAK47Weapon* Wp = Cast<AAK47Weapon>(A)) { AK = Wp; } }
			const USkeletalMeshComponent* Gun = AK ? AK->GetFirstPersonMesh() : nullptr;
			if (!Gun) { return true; }

			const FTransform View(PC->PlayerCameraManager->GetCameraRotation(), PC->PlayerCameraManager->GetCameraLocation());
			const FVector Muzzle = View.InverseTransformPosition(Gun->DoesSocketExist(TEXT("Muzzle")) ? Gun->GetSocketLocation(TEXT("Muzzle")) : Gun->GetComponentLocation());
			const FVector Origin = View.InverseTransformPosition(Gun->GetComponentLocation());
			const FVector CamRel = Char->GetActorTransform().InverseTransformPosition(View.GetLocation());
			const UCameraComponent* CamComp = Char->FindComponentByClass<UCameraComponent>();
			const FVector CompRel = CamComp ? Char->GetActorTransform().InverseTransformPosition(CamComp->GetComponentLocation()) : FVector::ZeroVector;
			const FVector GunRel = Char->GetActorTransform().InverseTransformPosition(Gun->GetComponentLocation());

			State->MinMuzzle = State->MinMuzzle.ComponentMin(Muzzle); State->MaxMuzzle = State->MaxMuzzle.ComponentMax(Muzzle);
			State->MinOrigin = State->MinOrigin.ComponentMin(Origin); State->MaxOrigin = State->MaxOrigin.ComponentMax(Origin);
			State->MinCamRel = State->MinCamRel.ComponentMin(CamRel); State->MaxCamRel = State->MaxCamRel.ComponentMax(CamRel);
			if (State->bHasPrev)
			{
				const FVector Delta = Muzzle - State->PrevMuzzle;
				const float Step = Delta.Size();
				State->SumStep += Step;
				State->MaxStep = FMath::Max(State->MaxStep, Step);
				State->MaxJerk = FMath::Max(State->MaxJerk, (Delta - State->PrevDelta).Size());
				if (Step < 0.001f && State->PrevDelta.Size() > 0.02f) { ++State->Stalls; }
				State->PrevDelta = Delta;
				// Frame by frame dump for a short window while moving forward.
				if (FCString::Strcmp(P[State->Phase].Name, TEXT("avant")) == 0 && State->T > 2.5f && State->T < 2.9f)
				{
					UE_LOG(LogFidelis, Display, TEXT("DiagAim image dt=%.4f bouche=%s pas=%.3f"), Dt, *Muzzle.ToCompactString(), Step);
				}
			}
			State->PrevMuzzle = Muzzle;
			State->bHasPrev = true;
			++State->Samples;

			const bool bOdd = FMath::Abs(Muzzle.Z - State->MinMuzzle.Z) > 0.05f || FMath::Abs(Muzzle.Z - State->MaxMuzzle.Z) > 0.05f;
			if ((State->Frame++ % 10) == 0 && !bOdd)
			{
				UE_LOG(LogFidelis, Display, TEXT("DiagAim [%s] t=%.2f vitesse=%.0f visee=%d bouche=%s origine=%s camera=%s composantCamera=%s (%s, parent %s/%s) fusil(perso)=%s"),
					P[State->Phase].Name, State->T, Char->GetVelocity().Size2D(), AK->IsAiming(),
					*Muzzle.ToCompactString(), *Origin.ToCompactString(), *CamRel.ToCompactString(), *CompRel.ToCompactString(),
					*GetNameSafe(CamComp), *GetNameSafe(CamComp ? CamComp->GetAttachParent() : nullptr), CamComp ? *CamComp->GetAttachSocketName().ToString() : TEXT("-"),
					*GunRel.ToCompactString());
			}
			return true;
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFidelisDiagAimCmd(
	TEXT("fidelis.DiagAim"),
	TEXT("Visee + deplacements: mesure si le fusil bouge par rapport a la camera. Argument optionnel: quit."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World() && Ctx.World()->IsGameWorld())
			{
				FidelisDiagAim::Run(Ctx.World(), Args.Contains(TEXT("quit")), !Args.Contains(TEXT("noaim")));
				return;
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFidelisDiagKeysCmd(
	TEXT("fidelis.DiagKeys"),
	TEXT("Test des touches perso: W accroupi, N allonge (a l'arret et en course), compare a C / V. Argument optionnel: quit."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World() && Ctx.World()->IsGameWorld())
			{
				FidelisDiag::RunMoves(Ctx.World(), Args.Contains(TEXT("quit")), /*bKeys*/ true);
				return;
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFidelisDiagMovesCmd(
	TEXT("fidelis.DiagMoves"),
	TEXT("Scenario automatique: course, glissade, plongeon + tir allonge a 180 deg, roulade arriere. Argument optionnel: quit."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World() && Ctx.World()->IsGameWorld())
			{
				FidelisDiag::RunMoves(Ctx.World(), Args.Contains(TEXT("quit")));
				return;
			}
		}
	}));

#endif
