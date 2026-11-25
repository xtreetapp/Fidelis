// Developer diagnostics for the AK-47 / player look input.
// Console: "fidelis.DiagLook [quit]" -- logs the look-input state, simulates mouse movement
// for a short time and reports whether the control rotation (the view) actually changed.

#include "Fidelis.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
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

	static void RunMoves(UWorld* World, bool bQuit)
	{
		TArray<FStep> Steps = {
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
