#include "BunnyHopComponent.h"

#include "Fidelis.h"
#include "DiveComponent.h"
#include "LeanComponent.h"
#include "SlideComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"

UBunnyHopComponent::UBunnyHopComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Run before the movement component: the air movement input must be dropped before it is applied.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UBunnyHopComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!JumpAction)
	{
		JumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	}
	if (!CrouchAction)
	{
		CrouchAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
	}

	Character = Cast<ACharacter>(GetOwner());
	if (ACharacter* Char = Character.Get())
	{
		Movement = Char->GetCharacterMovement();
		if (UCharacterMovementComponent* CMC = Movement.Get())
		{
			CMC->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);

			// Same jump height, shorter air time: height h = Vz^2 / (2 g). For air time T on flat
			// ground: g' = 8 h / T^2 and Vz' = 4 h / T (g' only while jumping).
			BaseGravityScale = CMC->GravityScale;
			const float Gravity = FMath::Abs(CMC->GetGravityZ());
			if (JumpAirTime > 0.f && Gravity > 0.f && CMC->JumpZVelocity > 0.f)
			{
				const float Height = FMath::Square(CMC->JumpZVelocity) / (2.f * Gravity);
				const float JumpGravity = 8.f * Height / FMath::Square(JumpAirTime);
				JumpGravityScale = BaseGravityScale * JumpGravity / Gravity;
				const float OldJumpZ = CMC->JumpZVelocity;
				CMC->JumpZVelocity = 4.f * Height / JumpAirTime;
				UE_LOG(LogFidelis, Log, TEXT("BunnyHop: saut %.0f cm de haut en %.2f s (vitesse verticale %.0f -> %.0f, gravite x%.2f en saut)."),
					Height, JumpAirTime, OldJumpZ, CMC->JumpZVelocity, JumpGravityScale);
			}
		}
		// After the controller, which adds this frame's movement input.
		if (AController* Controller = Char->GetController())
		{
			PrimaryComponentTick.AddPrerequisite(Controller, Controller->PrimaryActorTick);
		}
	}
	UE_LOG(LogFidelis, Log, TEXT("BunnyHop: actif sur %s (fenetre %.2f s, gain %.0f, max %.0f cm/s, saut glisse %s)."),
		*GetNameSafe(GetOwner()), HopWindow, SpeedGainPerHop, MaxBunnyHopSpeed,
		bSlideJump ? TEXT("oui") : TEXT("non"));
}

bool UBunnyHopComponent::IsJumpHeld() const
{
	const ACharacter* Char = Character.Get();
	const APlayerController* PC = Char ? Cast<APlayerController>(Char->GetController()) : nullptr;
	const UEnhancedPlayerInput* PlayerInput = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
	return PlayerInput && JumpAction && PlayerInput->GetActionValue(JumpAction).Get<bool>();
}

bool UBunnyHopComponent::CanChainHop() const
{
	// Fast enough, or the chain has run out of speed (on-the-spot hops): every hop after that stays on
	// the spot until the character walks / runs on the ground again (longer than HopWindow).
	return LastAirSpeed >= MinSpeedToHop || (ChainedHops > 0 && GetChainHopSpeed(ChainedHops) <= 0.f);
}

float UBunnyHopComponent::GetChainHopSpeed(int32 HopIndex) const
{
	// Share of the chain's first jump speed (hence distance, same air time) for this hop.
	const float Ratio = ChainHopSpeedRatios.Num() == 0 ? 1.f
		: ChainHopSpeedRatios[FMath::Clamp(HopIndex, 0, ChainHopSpeedRatios.Num() - 1)];
	return FMath::Min(ChainBaseSpeed * Ratio + SpeedGainPerHop * HopIndex, MaxBunnyHopSpeed);
}

void UBunnyHopComponent::SlideJump()
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();

	const FVector Horizontal = FVector(CMC->Velocity.X, CMC->Velocity.Y, 0.f);
	const float Speed = FMath::Min(Horizontal.Size(), MaxBunnyHopSpeed);

	// The engine refuses to jump while crouched (and the slide crouches): end the slide, stand up and
	// launch the jump directly. The movement component applies it on its next update.
	Slide->CancelSlide(TEXT("saut"));
	CMC->Velocity = Horizontal.GetSafeNormal() * Speed + FVector(0.f, 0.f, CMC->JumpZVelocity);
	CMC->SetMovementMode(MOVE_Falling);

	HopSpeed = Speed;
	LastAirSpeed = Speed;
	bInJump = true;
	JumpDir = Horizontal.GetSafeNormal();
	JumpSpeed = Speed;
	// A jump out of a slide starts a new chain.
	ChainedHops = 0;
	ChainBaseSpeed = Speed;
	bWasFalling = true;
	UE_LOG(LogFidelis, Log, TEXT("BunnyHop: saut depuis la glissade a %.0f cm/s."), Speed);
}

bool UBunnyHopComponent::TryWallJump()
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	UPrimitiveComponent* Body = CMC->UpdatedPrimitive;
	const UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
	if (!Body || !Capsule)
	{
		return false;
	}

	// Same collision as the character's own movement, ignoring itself and what it carries (the rifle).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BunnyHopWallJump), false, Char);
	FCollisionResponseParams Response;
	Body->InitSweepCollisionParams(Params, Response);
	TArray<AActor*> Attached;
	Char->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);

	// Look for the nearest wall all around the character, at waist height.
	const FVector Start = Char->GetActorLocation();
	const float Reach = Capsule->GetScaledCapsuleRadius() + WallJumpMaxDistance;
	FHitResult Best;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const FVector Dir = FRotator(0.f, Index * 30.f, 0.f).Vector();
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * Reach, Body->GetCollisionObjectType(), Params, Response)
			&& FMath::Abs(Hit.ImpactNormal.Z) < 0.3f // a wall, not a floor / ceiling / steep slope
			&& (!Best.bBlockingHit || Hit.Distance < Best.Distance))
		{
			Best = Hit;
		}
	}
	if (!Best.bBlockingHit)
	{
		return false;
	}

	const FVector Normal = FVector(Best.ImpactNormal.X, Best.ImpactNormal.Y, 0.f).GetSafeNormal();

	// Incoming velocity = the jump's take-off velocity: once the character touches the wall, the
	// collision has already removed the part going into it.
	const FVector Vel2D = (bInJump && !JumpDir.IsNearlyZero() && JumpSpeed > 0.f)
		? JumpDir * JumpSpeed
		: FVector(CMC->Velocity.X, CMC->Velocity.Y, 0.f);

	// Head-on (or jumping in place while looking at the wall) -> climb.
	const FVector Facing = Vel2D.IsNearlyZero()
		? FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).Vector()
		: Vel2D.GetSafeNormal();
	const float HeadOnAngle = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Facing, -Normal), -1.f, 1.f)));
	if (bWallClimb && HeadOnAngle >= WallClimbMinAngle && WallClimbCount < MaxWallClimbs)
	{
		const float ClimbUp = WallClimbUpSpeed > 0.f ? WallClimbUpSpeed : CMC->JumpZVelocity;
		CMC->Velocity = -Normal * WallClimbHugSpeed + FVector(0.f, 0.f, ClimbUp);
		// JumpDir / JumpSpeed stay the incoming ones: once the climbs are used up, the bounce sends the
		// character straight back.
		bInJump = true;
		bWallClimbing = true;
		ClimbNormal = Normal;
		HopSpeed = 0.f;
		++WallClimbCount;
		UE_LOG(LogFidelis, Log, TEXT("BunnyHop: escalade du mur %s (%d/%d), arrivee a %.0f deg du mur."),
			*GetNameSafe(Best.GetActor()), WallClimbCount, MaxWallClimbs, HeadOnAngle);
		return true;
	}

	if (!bWallJump || WallJumpCount >= MaxWallJumps)
	{
		return false;
	}
	if (WallJumpCount > 0 && FVector::DotProduct(Normal, LastWallNormal) > 0.7f)
	{
		return false; // same wall as the previous wall jump
	}

	// Mirror bounce: the part of the velocity going into the wall is reversed, the part along it is
	// kept, so the angle out of the wall equals the angle in (45 -> turn of 90, 30 -> turn of 120,
	// head-on -> straight back).
	const float Into = FVector::DotProduct(Vel2D, Normal);
	FVector Dir = Into < 0.f ? (Vel2D - 2.f * Into * Normal).GetSafeNormal() : Vel2D.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = Normal; // no horizontal speed: straight out from the wall
	}
	const float Speed = WallJumpSpeed;
	const FVector Horizontal = Dir * Speed;
	const float Up = WallJumpUpSpeed > 0.f ? WallJumpUpSpeed : CMC->JumpZVelocity;
	CMC->Velocity = Horizontal + FVector(0.f, 0.f, Up);

	bInJump = true;
	bWallClimbing = false;
	JumpDir = Horizontal.GetSafeNormal();
	JumpSpeed = Speed;
	HopSpeed = Speed;
	LastAirSpeed = Speed;
	++WallJumpCount;
	LastWallNormal = Normal;
	bAirRollReady = bWallJumpAirRoll;
	UE_LOG(LogFidelis, Log, TEXT("BunnyHop: rebond sur le mur %s (%d/%d), arrivee a %.0f deg du mur, virage de %.0f deg, %.0f cm/s."),
		*GetNameSafe(Best.GetActor()), WallJumpCount, MaxWallJumps,
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(-Into / FMath::Max(Vel2D.Size(), 1.f), -1.f, 1.f))),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Vel2D.GetSafeNormal(), Dir), -1.f, 1.f))),
		Speed);
	return true;
}

bool UBunnyHopComponent::TryLedgeGrab(const FVector& Facing)
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	UPrimitiveComponent* Body = CMC->UpdatedPrimitive;
	const UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
	if (!Body || !Capsule || Facing.IsNearlyZero())
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BunnyHopLedgeGrab), false, Char);
	FCollisionResponseParams Response;
	Body->InitSweepCollisionParams(Params, Response);
	TArray<AActor*> Attached;
	Char->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	const ECollisionChannel Channel = Body->GetCollisionObjectType();

	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Location = Char->GetActorLocation();
	const float FeetZ = Location.Z - HalfHeight;
	const float HandsZ = Location.Z + HalfHeight + LedgeGrabReachAboveHead;

	// 1) A wall right in front, at chest height.
	FHitResult WallHit;
	const FVector Chest = Location + FVector(0.f, 0.f, HalfHeight * 0.5f);
	if (!GetWorld()->LineTraceSingleByChannel(WallHit, Chest, Chest + Facing * (Radius + LedgeGrabMaxDistance), Channel, Params, Response)
		|| FMath::Abs(WallHit.ImpactNormal.Z) > 0.3f)
	{
		return false;
	}
	const FVector Forward = FVector(-WallHit.ImpactNormal.X, -WallHit.ImpactNormal.Y, 0.f).GetSafeNormal();

	// 2) Its top, within reach of the hands: trace down just behind the wall's face.
	const FVector OnTop = FVector(WallHit.ImpactPoint.X, WallHit.ImpactPoint.Y, 0.f) + Forward * 5.f;
	FHitResult TopHit;
	if (!GetWorld()->LineTraceSingleByChannel(TopHit, FVector(OnTop.X, OnTop.Y, HandsZ),
			FVector(OnTop.X, OnTop.Y, FeetZ + CMC->MaxStepHeight), Channel, Params, Response)
		|| TopHit.bStartPenetrating || !CMC->IsWalkable(TopHit))
	{
		return false;
	}
	const float TopZ = TopHit.ImpactPoint.Z;

	// 3) Room to stand on the top, past the edge.
	const FVector End = FVector(WallHit.ImpactPoint.X, WallHit.ImpactPoint.Y, TopZ + HalfHeight + 2.f) + Forward * (Radius + 5.f);
	if (GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, Channel, Capsule->GetCollisionShape(), Params, Response))
	{
		return false;
	}

	bMantling = true;
	MantleElapsed = 0.f;
	LedgeEdge = FVector(WallHit.ImpactPoint.X, WallHit.ImpactPoint.Y, TopZ);
	LedgeForward = Forward;
	MantleStart = Location;
	MantleUp = FVector(Location.X, Location.Y, End.Z);
	MantleEnd = End;
	bWallClimbing = false;
	HopSpeed = 0.f;
	CMC->StopMovementImmediately();
	CMC->SetMovementMode(MOVE_Flying);
	UE_LOG(LogFidelis, Log, TEXT("BunnyHop: rebord attrape sur %s (%.0f cm au-dessus des pieds)."),
		*GetNameSafe(WallHit.GetActor()), TopZ - FeetZ);
	return true;
}

FQuat UBunnyHopComponent::GetAirRollViewRotation() const
{
	if (AirRollElapsed < 0.f || AirRollDuration <= 0.f)
	{
		return FQuat::Identity;
	}
	// Eased so the turn starts and ends gently, and ends exactly level.
	const float T = FMath::Clamp(AirRollElapsed / AirRollDuration, 0.f, 1.f);
	const float Angle = 360.f * FMath::InterpEaseInOut(0.f, 1.f, T, 2.f);
	return FRotator(AirRollAxis.Pitch * Angle, 0.f, AirRollAxis.Roll * Angle).Quaternion();
}

float UBunnyHopComponent::GetMantleHandsAlpha() const
{
	if (!bMantling)
	{
		return 0.f;
	}
	// Hands reach the edge quickly, stay on it while the body pulls up, and let go while it steps over.
	const float GrabIn = FMath::Clamp(MantleElapsed / 0.08f, 0.f, 1.f);
	const float Climb = FMath::Clamp((MantleElapsed - LedgeHangTime) / FMath::Max(LedgeClimbDuration, 0.01f), 0.f, 1.f);
	const float LetGo = 1.f - FMath::SmoothStep(0.45f, 0.85f, Climb);
	return GrabIn * LetGo;
}

bool UBunnyHopComponent::GetLedgeGrip(FVector& OutEdge, FVector& OutForward) const
{
	OutEdge = LedgeEdge;
	OutForward = LedgeForward;
	return bMantling;
}

void UBunnyHopComponent::TickMantle(float DeltaTime)
{
	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();

	// The movement keys do nothing while hanging / pulling up.
	Char->ConsumeMovementInputVector();
	CMC->Velocity = FVector::ZeroVector;
	MantleElapsed += DeltaTime;

	FVector Target;
	const float ClimbTime = MantleElapsed - LedgeHangTime;
	if (ClimbTime <= 0.f)
	{
		Target = MantleStart; // hanging on the edge
	}
	else
	{
		// Pull up (first 70%), then step forward onto the top (last 30%).
		const float Alpha = FMath::Clamp(ClimbTime / LedgeClimbDuration, 0.f, 1.f);
		constexpr float UpShare = 0.7f;
		Target = Alpha < UpShare
			? FMath::Lerp(MantleStart, MantleUp, FMath::InterpEaseInOut(0.f, 1.f, Alpha / UpShare, 2.f))
			: FMath::Lerp(MantleUp, MantleEnd, FMath::InterpEaseInOut(0.f, 1.f, (Alpha - UpShare) / (1.f - UpShare), 2.f));
		if (Alpha >= 1.f)
		{
			Char->SetActorLocation(MantleEnd, false, nullptr, ETeleportType::TeleportPhysics);
			bMantling = false;
			// Drop the last 2 cm onto the top: the normal landing follows.
			CMC->SetMovementMode(MOVE_Falling);
			UE_LOG(LogFidelis, Log, TEXT("BunnyHop: monte sur le rebord."));
			return;
		}
	}
	Char->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
}

void UBunnyHopComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Char = Character.Get();
	UCharacterMovementComponent* CMC = Movement.Get();
	if (!Char || !CMC || !bEnableBunnyHop)
	{
		return;
	}

	// Stronger gravity only during a jump (and wall climb), so it lasts JumpAirTime; normal gravity for
	// plain falls, dives and on the ground.
	{
		const bool bWantJumpGravity = JumpAirTime > 0.f && CMC->IsFalling() && (bInJump || bWallClimbing);
		if (bWantJumpGravity != bJumpGravityOn)
		{
			bJumpGravityOn = bWantJumpGravity;
			CMC->GravityScale = bWantJumpGravity ? JumpGravityScale : BaseGravityScale;
		}
	}

	const bool bJumpHeld = IsJumpHeld();
	const bool bJumpPressed = bJumpHeld && !bJumpWasHeld;
	// Jump pressed: freeze the arms animation now, before the take-off (see ULeanComponent).
	if (bJumpPressed)
	{
		LastJumpPressTime = GetWorld()->GetTimeSeconds();
		if (ULeanComponent* Lean = Char->FindComponentByClass<ULeanComponent>())
		{
			Lean->NotifyJumpPressed();
		}
	}
	bJumpWasHeld = bJumpHeld;

	bool bCrouchHeld = false;
	{
		const APlayerController* PC = Cast<APlayerController>(Char->GetController());
		const UEnhancedPlayerInput* PlayerInput = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
		bCrouchHeld = PlayerInput && CrouchAction && PlayerInput->GetActionValue(CrouchAction).Get<bool>();
	}
	const bool bCrouchPressed = bCrouchHeld && !bCrouchWasHeld;
	bCrouchWasHeld = bCrouchHeld;

	// Air roll after a wall jump: crouch pressed in the air. Only the view turns (camera + arms), the
	// bounce keeps its trajectory. Once started it plays to the end, even if the character lands.
	if (AirRollElapsed >= 0.f)
	{
		AirRollElapsed += DeltaTime;
		if (AirRollElapsed >= AirRollDuration)
		{
			AirRollElapsed = -1.f;
			// End of the roll: the bonus is over, back to the bounce speed (still in the air: right away).
			if (AirRollSpeedBonus > 0.f && AirRollBaseSpeed > 0.f)
			{
				if (CMC->IsFalling() && HopSpeed > 0.f)
				{
					const FVector HDir = JumpDir.IsNearlyZero() ? CMC->Velocity.GetSafeNormal2D() : JumpDir;
					CMC->Velocity = HDir * AirRollBaseSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
					HopSpeed = AirRollBaseSpeed;
				}
				JumpSpeed = AirRollBaseSpeed;
				LastAirSpeed = FMath::Min(LastAirSpeed, AirRollBaseSpeed);
				UE_LOG(LogFidelis, Log, TEXT("BunnyHop: fin de la roulade, vitesse %.0f cm/s."), AirRollBaseSpeed);
			}
			AirRollBaseSpeed = 0.f;
		}
	}
	else if (bAirRollReady && bCrouchPressed && CMC->IsFalling())
	{
		bAirRollReady = false;
		AirRollElapsed = 0.f;
		// Side roll away from the wall bounced off: wall on the player's right -> roll to the left, and
		// the other way round (relative to where the player looks now). No front / back flips.
		const FVector AwayFromWall = FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).UnrotateVector(LastWallNormal);
		const bool bRollRight = AwayFromWall.Y >= 0.f;
		AirRollAxis = bRollRight ? FRotator(0.f, 0.f, 1.f) : FRotator(0.f, 0.f, -1.f);
		const TCHAR* Kind = bRollRight ? TEXT("roulade a droite (mur a gauche)") : TEXT("roulade a gauche (mur a droite)");
		// Speed bonus on the bounce's horizontal speed for the duration of the roll only (the air lock
		// maintains HopSpeed); back to the bounce speed when the roll ends.
		AirRollBaseSpeed = HopSpeed > 0.f ? HopSpeed : CMC->Velocity.Size2D();
		if (AirRollSpeedBonus > 0.f)
		{
			const float NewSpeed = AirRollBaseSpeed * (1.f + AirRollSpeedBonus);
			const FVector HDir = JumpDir.IsNearlyZero() ? CMC->Velocity.GetSafeNormal2D() : JumpDir;
			HopSpeed = NewSpeed;
			JumpSpeed = NewSpeed;
			LastAirSpeed = NewSpeed;
			CMC->Velocity = HDir * NewSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
		}
		UE_LOG(LogFidelis, Log, TEXT("BunnyHop: %s en l'air apres le rebond (%.2f s), vitesse %.0f cm/s."),
			Kind, AirRollDuration, CMC->Velocity.Size2D());
	}

	if (bMantling)
	{
		TickMantle(DeltaTime);
		bWasFalling = true; // the landing on the top is handled like any other
		return;
	}

	// Jump pressed during a slide -> slide jump.
	if (bSlideJump && bJumpPressed && CMC->IsMovingOnGround())
	{
		const USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
		if (Slide && Slide->IsSliding())
		{
			SlideJump();
			return;
		}
	}

	bool bFalling = CMC->IsFalling();
	const double Now = GetWorld()->GetTimeSeconds();

	// Jump pressed in the air next to a wall -> bounce off it (not during a dive).
	if ((bWallJump || bWallClimb) && bJumpPressed && bFalling && bWasFalling
		&& (WallJumpCount < MaxWallJumps || WallClimbCount < MaxWallClimbs))
	{
		const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>();
		if (!(Dive && Dive->IsBusy()) && TryWallJump())
		{
			return;
		}
	}

	if (bFalling && !bWasFalling)
	{
		// Just left the ground going up = a jump (walking off a ledge has no upward speed).
		// bPressedJump can't be used: the engine clears it as soon as the jump is performed.
		const bool bIsJump = CMC->Velocity.Z > 1.f;
		const USlideComponent* Slide = Char->FindComponentByClass<USlideComponent>();
		const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>();
		const bool bBusy = (Slide && Slide->IsSliding()) || (Dive && Dive->IsBusy());

		if (bIsJump && !bBusy && Now - LandTime <= HopWindow && CanChainHop())
		{
			const float CurrentSpeed = CMC->Velocity.Size2D();
			const float Target = GetChainHopSpeed(ChainedHops + 1);
			// Chained hop: same direction as the previous jump (the first one of the chain, or the last
			// wall bounce), whatever the keys or the view do during the short ground contact.
			FVector Dir = !ChainDir.IsNearlyZero() ? ChainDir : CMC->Velocity.GetSafeNormal2D();
			if (Dir.IsNearlyZero())
			{
				Dir = Char->GetActorForwardVector().GetSafeNormal2D();
			}
			CMC->Velocity = Dir * Target + FVector(0.f, 0.f, CMC->Velocity.Z);
			HopSpeed = Target;
			bInJump = true;
			JumpDir = Dir;
			JumpSpeed = Target;
			++ChainedHops;
			UE_LOG(LogFidelis, Log, TEXT("BunnyHop: saut %d enchaine, %.0f -> %.0f cm/s (max %.0f)."),
				ChainedHops, CurrentSpeed, Target, MaxBunnyHopSpeed);
		}
		else if (bIsJump)
		{
			// First jump of a chain: still keep the take-off speed in the air.
			ChainedHops = 0;
			bInJump = !bBusy;
			JumpDir = CMC->Velocity.GetSafeNormal2D();
			JumpSpeed = CMC->Velocity.Size2D();
			HopSpeed = (bInJump && !JumpDir.IsNearlyZero()) ? FMath::Min(CMC->Velocity.Size2D(), MaxBunnyHopSpeed) : 0.f;
			ChainBaseSpeed = HopSpeed;
		}
	}
	else if (!bFalling && bWasFalling)
	{
		LandTime = Now;
		// Direction the next chained hop keeps.
		ChainDir = bInJump ? JumpDir.GetSafeNormal2D() : FVector::ZeroVector;
		bInJump = false;
		WallJumpCount = 0;
		WallClimbCount = 0;
		bWallClimbing = false;
		bAirRollReady = false;

		// Jump held on landing, or pressed just before touching down: hop again right away, no time on
		// the ground (applied by the movement component on its next update, i.e. this same frame).
		const bool bBuffered = Now - LastJumpPressTime <= JumpBufferTime;
		const USlideComponent* LandSlide = Char->FindComponentByClass<USlideComponent>();
		const UDiveComponent* LandDive = Char->FindComponentByClass<UDiveComponent>();
		const bool bLandBusy = (LandSlide && LandSlide->IsSliding()) || (LandDive && LandDive->IsBusy());
		if (((bAutoHopWhileJumpHeld && bJumpHeld) || bBuffered) && !bLandBusy && !ChainDir.IsNearlyZero()
			&& CanChainHop())
		{
			const float Target = GetChainHopSpeed(ChainedHops + 1);
			CMC->Velocity = ChainDir * Target + FVector(0.f, 0.f, CMC->JumpZVelocity);
			CMC->SetMovementMode(MOVE_Falling);
			HopSpeed = Target;
			LastAirSpeed = Target;
			bInJump = true;
			JumpDir = ChainDir;
			JumpSpeed = Target;
			++ChainedHops;
			LastJumpPressTime = -100.0;
			bFalling = true; // still in the air for the rest of this tick
			if (ULeanComponent* Lean = Char->FindComponentByClass<ULeanComponent>())
			{
				Lean->NotifyJumpPressed();
			}
			UE_LOG(LogFidelis, Log, TEXT("BunnyHop: saut %d enchaine des l'atterrissage, %.0f cm/s."), ChainedHops, Target);
		}
	}
	else if (!bFalling && Now - LandTime > HopWindow)
	{
		ChainedHops = 0;
		HopSpeed = 0.f;
	}

	// In the air against a wall with its top in reach -> grab the edge and climb onto it.
	if (bLedgeGrab && bFalling && bWasFalling)
	{
		const UDiveComponent* Dive = Char->FindComponentByClass<UDiveComponent>();
		const FVector Facing = bWallClimbing ? -ClimbNormal
			: !JumpDir.IsNearlyZero() ? JumpDir
			: FRotator(0.f, Char->GetControlRotation().Yaw, 0.f).Vector();
		if (!(Dive && Dive->IsBusy()) && TryLedgeGrab(Facing))
		{
			return;
		}
	}

	if (bFalling)
	{
		// Straight jump: drop the movement keys' input before the movement component applies it as air control.
		if (bInJump && bLockAirMovement)
		{
			Char->ConsumeMovementInputVector();
		}
		// The character brakes in the air above its max walk speed (BrakingDecelerationFalling):
		// keep the take-off speed for the whole air phase.
		if (bWallClimbing)
		{
			// Against the wall while climbing; over the top, this carries the character onto it.
			CMC->Velocity = -ClimbNormal * WallClimbHugSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
		}
		else if (HopSpeed > 0.f)
		{
			const FVector Vel2D(CMC->Velocity.X, CMC->Velocity.Y, 0.f);
			const float Speed2D = Vel2D.Size();
			const bool bLocked = bInJump && bLockAirMovement && !JumpDir.IsNearlyZero();
			// Hit something (slowed down hard, or turned by a wall): stop maintaining.
			if (Speed2D < HopSpeed * 0.5f || (bLocked && FVector::DotProduct(Vel2D / FMath::Max(Speed2D, 1.f), JumpDir) < 0.9f))
			{
				HopSpeed = 0.f;
			}
			else if (bLocked)
			{
				CMC->Velocity = JumpDir * HopSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
			}
			else if (Speed2D < HopSpeed)
			{
				CMC->Velocity = Vel2D.GetSafeNormal() * HopSpeed + FVector(0.f, 0.f, CMC->Velocity.Z);
			}
		}
		LastAirSpeed = CMC->Velocity.Size2D();
	}
	bWasFalling = bFalling;
}
