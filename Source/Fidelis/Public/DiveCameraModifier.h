#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "DiveCameraModifier.generated.h"

/**
 * Turns the player's view with the body during a dive roll (full somersault forward / backward,
 * barrel roll sideways). The rotation comes from UDiveComponent::GetRollViewRotation(); it is applied
 * after the control rotation, so the camera pitch limits don't apply and aiming is left untouched.
 * Added automatically to the player camera manager by the DiveComponent.
 */
UCLASS()
class FIDELIS_API UDiveCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
};
