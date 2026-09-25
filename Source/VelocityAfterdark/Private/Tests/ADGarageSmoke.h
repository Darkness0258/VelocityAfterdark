#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ADGarageSmoke.generated.h"

class AADPlayerController;
class AADVehiclePawn;
class UADGarageSessionComponent;
class UADOwnershipSubsystem;

/** Explicit development-only packaged save/relaunch verification. Never selects a normal player profile. */
UCLASS()
class UADGarageSmoke : public UActorComponent
{
    GENERATED_BODY()
public:
    UADGarageSmoke();
    void Initialize(bool bInVerify);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
    bool Start();
    bool ValidateCommitted(FString& OutError) const;
    void Capture(const TCHAR* Name);
    void StopCapture();
    void Finish(bool bSuccess, const FString& Reason);
    FString GetOutputDirectory() const;

    TWeakObjectPtr<AADPlayerController> Controller;
    TWeakObjectPtr<AADVehiclePawn> Player;
    TWeakObjectPtr<UADGarageSessionComponent> Garage;
    TWeakObjectPtr<UADOwnershipSubsystem> Ownership;
    FVector ReturnPosition = FVector::ZeroVector;
    double BootWallSeconds = 0.;
    double StageWallSeconds = 0.;
    double CaptureStartWall = 0.;
    double CaptureWallSeconds = 0.;
    int32 Stage = 0;
    int32 CapturedFrames = 0;
    bool bVerify = false;
    bool bCapturing = false;
    bool bFinished = false;
};

void ADStartGarageSmoke(UWorld* World);
