#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CollisionQueryParams.h"
#include "Racing/ADRaceDefinition.h"
#include "ADRaceDriverComponent.generated.h"

class AADVehiclePawn;
class UADVehiclePhysicsComponent;
/** Route-following racer using the same control inputs and physical limits as the player. */
UCLASS(ClassGroup=(Racing), meta=(BlueprintSpawnableComponent))
class VELOCITYAFTERDARK_API UADRaceDriverComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UADRaceDriverComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // The manager owns an immutable definition for the lifetime of this driver.
    bool Initialize(AADVehiclePawn* Car, const FADRaceDefinition* Route,
        float SpeedScale, float LaneOffsetCm);
    bool SetPersonality(const FString& StableDriverId, const FADDriverPersonality& Personality);
    void SetEmergencyYield(bool bYield, float ShoulderOffsetCm = 650.f);
    bool IsEmergencyYielding() const { return bEmergencyYield; }
    void SetCompetitors(const TArray<AADVehiclePawn*>& Cars);
    void SetDriving(bool bEnabled);
    void BeginRunOut(double DistanceM);
    void ResetDriver();
    bool NeedsRecovery() const { return bNeedsRecovery; }
    bool IsReversing() const;
    int32 GetAvoidanceCount() const { return AvoidanceCount; }
    int32 GetOvertakeCount() const { return OvertakeCount; }
    int32 GetRecoveryAttemptCount() const { return RecoveryAttemptCount; }
    float GetTargetSpeedKmh() const { return static_cast<float>(TargetSpeedMps * 3.6); }
    float GetRouteErrorM() const { return static_cast<float>(RouteErrorM); }

private:
    enum class ERecoveryState : uint8 { Driving, BrakeForReverse, Reverse, BrakeForForward };

    void Sense(double ProgressM, double SpeedMps, double DesiredSpeedMps);
    bool IsLaneClear(double OffsetCm, double ProgressM) const;
    double SweepClearanceM(const FVector& Start, const FVector& Direction, double LengthM) const;
    void BeginRecovery(float ForwardSteering);
    void TickRecovery(float DeltaTime, double SpeedMps);
    void RequestManagedRecovery();
    double SignedRouteGapM(double OtherProgressM, double OwnProgressM) const;
    FVector2D RouteRightAt(double ProgressM) const;

    TWeakObjectPtr<AADVehiclePawn> Vehicle;
    TWeakObjectPtr<UADVehiclePhysicsComponent> Physics;
    TArray<TWeakObjectPtr<AADVehiclePawn>> Competitors;
    TWeakObjectPtr<AADVehiclePawn> PassingVehicle;
    const FADRaceDefinition* Definition = nullptr;
    FString DriverIdentity;
    FADDriverPersonality Personality;
    FCollisionQueryParams ObstacleQuery;
    FVector ReverseStart = FVector::ZeroVector;
    FVector RunOutLastPosition = FVector::ZeroVector;
    double RunOutRemainingM = 0.;
    double RunOutSeconds = 0.;
    bool bRunOut = false;
    ERecoveryState RecoveryState = ERecoveryState::Driving;
    double DriverSpeedScale = 1.;
    double CruiseSpeedMps = 0.;
    double BaseLaneOffsetCm = 0.;
    double DesiredLaneOffsetCm = 0.;
    double CurrentLaneOffsetCm = 0.;
    double VehicleHalfLengthCm = 210.;
    double VehicleHalfWidthCm = 85.;
    double TargetSpeedMps = 0.;
    double RouteErrorM = 0.;
    double FollowingSpeedLimitMps = 100.;
    double ObstacleClearanceM = 100.;
    double SenseCountdown = 0.;
    float StuckSeconds = 0.f;
    float OffRouteSeconds = 0.f;
    float OverturnedSeconds = 0.f;
    float HealthyDrivingSeconds = 0.f;
    float RecoverySeconds = 0.f;
    float RecoverySteering = 0.f;
    float MistakeSeconds = 0.f;
    float EmergencyShoulderOffsetCm = 650.f;
    int32 ConsecutiveRecoveryAttempts = 0;
    int32 RecoveryAttemptCount = 0;
    int32 AvoidanceCount = 0;
    int32 OvertakeCount = 0;
    int32 LastMistakeWindow = INDEX_NONE;
    uint32 DriverSeed = 0;
    bool bDriving = false;
    bool bNeedsRecovery = false;
    bool bWasAvoiding = false;
    bool bUnderPressure = false;
    bool bFollowingStoppedVehicle = false;
    bool bEmergencyYield = false;
};
