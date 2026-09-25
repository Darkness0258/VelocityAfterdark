#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ADDriveBenchmark.generated.h"

class AADVehiclePawn;
class AADPlayerController;
class UADVehiclePhysicsComponent;

/** Opt-in QA driver. Uses normal vehicle controls; never teleports during a run. */
UCLASS()
class UADDriveBenchmark : public UActorComponent
{
    GENERATED_BODY()
public:
    UADDriveBenchmark();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void Initialize();

private:
    bool LoadRoute(FString& Error);
    FVector2D PointAtDistance(double Distance) const;
    void Finish(bool bSuccess, const FString& Reason);

    TWeakObjectPtr<AADVehiclePawn> Car;
    TWeakObjectPtr<AADPlayerController> Controller;
    TWeakObjectPtr<UADVehiclePhysicsComponent> Physics;
    TArray<FVector2D> Points;
    TArray<double> Distances;
    TArray<double> CornerSpeeds;
    FString RouteId;
    double RouteLength = 0.;
    double BootTime = 0.;
    double DriveTime = 0.;
    double CaptureTime = 0.;
    double CaptureWorldTime = 0.;
    double WarmupSeconds = 20.;
    double DurationSeconds = 120.;
    double CruiseSpeedMps = 23.6;
    double BrakeDecelerationMps2 = 3.5;
    double MeasuredDistanceM = 0.;
    double MaxSpeedKmh = 0.;
    double MaxRouteErrorM = 0.;
    double AirborneSeconds = 0.;
    double LastProgress = 0.;
    double NextLogTime = 0.;
    FVector LastPosition = FVector::ZeroVector;
    int32 MeasuredFrames = 0;
    int32 StartLineCrossings = 0;
    bool bDriving = false;
    bool bCapturing = false;
    bool bFinished = false;
};

void ADStartDriveBenchmark(UWorld* World);
