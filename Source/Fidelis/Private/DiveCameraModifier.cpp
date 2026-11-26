#include "DiveCameraModifier.h"

#include "Camera/PlayerCameraManager.h"
#include "DiveComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

bool UDiveCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UDiveComponent* Dive = Pawn ? Pawn->FindComponentByClass<UDiveComponent>() : nullptr;
	if (Dive)
	{
		const FQuat Local = Dive->GetRollViewRotation();
		if (!Local.Equals(FQuat::Identity, 1.e-4f))
		{
			// Rotation expressed in view space: pitch around the view's right axis, roll around its forward axis.
			InOutPOV.Rotation = (InOutPOV.Rotation.Quaternion() * Local).Rotator();
		}
	}
	// Never stop the other modifiers.
	return false;
}
