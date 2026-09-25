#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ADRaceSmoke.generated.h"

class AADRaceManager;
class AADPlayerController;
class AADVehiclePawn;
class UADRaceDriverComponent;

/** Explicit QA-only full race with a physical driver attached to the player. */
UCLASS()
class UADRaceSmoke : public UActorComponent
{
    GENERATED_BODY()
public:
    UADRaceSmoke();
    void Initialize();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
    bool Start();
    bool ValidateFrame(float DeltaTime, FString& Error);
    bool ValidateClassification(FString& Error) const;
    void Capture(const TCHAR* Name);
    void StopCapture();
    void Finish(bool bSuccess, const FString& Reason);

    TWeakObjectPtr<AADRaceManager> Manager;
    TWeakObjectPtr<AADPlayerController> Controller;
    TWeakObjectPtr<AADVehiclePawn> Player;
    TWeakObjectPtr<UADRaceDriverComponent> PlayerDriver;
    TArray<FVector> PreviousPositions;
    TArray<double> DistanceMeters;
    TArray<double> FirstLapSeconds;
    double BootWallSeconds = 0.;
    double CaptureStartWall = 0.;
    double CaptureStartRace = 0.;
    double CaptureWallSeconds = 0.;
    double CaptureRaceSeconds = 0.;
    double NextProgressWall = 0.;
    int32 CapturedFrames = 0;
    bool bStarted = false;
    bool bDriving = false;
    bool bCapturing = false;
    bool bCaptureComplete = false;
    bool bFinished = false;
};

void ADStartRaceSmoke(UWorld* World);
