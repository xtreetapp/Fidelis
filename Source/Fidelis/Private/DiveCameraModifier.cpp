#include "DiveCameraModifier.h"

#include "Camera/PlayerCameraManager.h"
#include "BunnyHopComponent.h"
#include "DiveComponent.h"
#include "SlideComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

bool UDiveCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UDiveComponent* Dive = Pawn ? Pawn->FindComponentByClass<UDiveComponent>() : nullptr;
	const UBunnyHopComponent* Hop = Pawn ? Pawn->FindComponentByClass<UBunnyHopComponent>() : nullptr;
	const USlideComponent* Slide = Pawn ? Pawn->FindComponentByClass<USlideComponent>() : nullptr;
	if (Dive || Hop || Slide)
	{
		// Dive roll, air roll after a wall jump, and the lean of the slide after a roll.
		const FQuat Local = (Dive ? Dive->GetRollViewRotation() : FQuat::Identity)
			* (Hop ? Hop->GetAirRollViewRotation() : FQuat::Identity)
			* (Slide ? Slide->GetRecoveryLeanViewRotation() : FQuat::Identity);
		if (!Local.Equals(FQuat::Identity, 1.e-4f))
		{
			// Rotation expressed in view space: pitch around the view's right axis, roll around its forward axis.
			InOutPOV.Rotation = (InOutPOV.Rotation.Quaternion() * Local).Rotator();
		}
	}
	// Never stop the other modifiers.
	return false;
}
